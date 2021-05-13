// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"AMOLED_ECM: %s: " fmt, __func__

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>

#include <linux/soc/qcom/panel_event_notifier.h>

/* AMOLED AB register definitions */
#define AB_REVISION2				0x01

/* AMOLED ECM register definitions */
#define AB_ECM_EN_CTL				0xA0
 #define ECM_EN					BIT(7)

#define AB_ECM_COUNTER_CTL			0xA1
 #define ECM_COUNTER_START			BIT(7)

/* AMOLED ECM SDAM Offsets */
#define ECM_SDAM_START_BASE			0x40
#define ECM_SDAMX_SAMPLE_START_ADDR		0x46

#define ECM_FAULT_LOG				0x48
#define ECM_ROUTINE_LOG				0x49

#define ECM_ACTIVE_SDAM				0x4D
 #define ECM_SDAM0_ACTIVE			BIT(0)
 #define ECM_SDAM1_ACTIVE			BIT(1)

#define ECM_SAMPLE_CNT_LSB			0x4E
#define ECM_SAMPLE_CNT_MSB			0x4F

#define ECM_STATUS_SET				0x50
#define ECM_STATUS_CLR				0x51
 #define ECM_ONGOING				BIT(0)
 #define ECM_DONE				BIT(1)
 #define ECM_ABORT				BIT(2)
 #define ECM_SDAM0_FULL				BIT(3)
 #define ECM_SDAM1_FULL				BIT(4)

#define ECM_SDAM0_INDEX				0x52
#define ECM_SDAM1_INDEX				0x53
#define ECM_SDAM2_INDEX				0x61

#define ECM_MODE				0x54
 #define ECM_CONTINUOUS				0
 #define ECM_N_ESWIRE				1
 #define ECM_M_ASWIRE				2
 #define ECM_ESWIRE_ASWIRE			3
 #define ECM_USE_TIMER				4

#define ECM_N_ESWIRE_COUNT_LSB			0x55
#define ECM_N_ESWIRE_COUNT_MSB			0x56
#define ECM_M_ASWIRE_COUNT_LSB			0x57
#define ECM_M_ASWIRE_COUNT_MSB			0x58
#define ECM_ESWIRE_ASWIRE_SKIP_COUNT_LSB	0x59
#define ECM_ESWIRE_ASWIRE_SKIP_COUNT_MSB	0x5A
#define ECM_TIMER_LSB				0x5B
#define ECM_TIMER_MSB				0x5C
#define ECM_TIMER_SKIP_LSB			0x5D
#define ECM_TIMER_SKIP_MSB			0x5E

#define ECM_SEND_IRQ				0x5F
 #define SEND_SDAM0_IRQ				BIT(0)
 #define SEND_SDAM1_IRQ				BIT(1)
 #define SEND_SDAM2_IRQ				BIT(2)

#define ECM_WRITE_TO_SDAM			0x60
 #define WRITE_SDAM0_DATA			BIT(0)
 #define WRITE_SDAM1_DATA			BIT(1)
 #define WRITE_SDAM2_DATA			BIT(2)
 #define OVERWRITE_SDAM0_DATA			BIT(4)
 #define OVERWRITE_SDAM1_DATA			BIT(5)
 #define OVERWRITE_SDAM2_DATA			BIT(6)

#define ECM_AVERAGE_LSB				0x61
#define ECM_AVERAGE_MSB				0x62
#define ECM_MIN_LSB				0x63
#define ECM_MIN_MSB				0x64
#define ECM_MAX_LSB				0x65
#define ECM_MAX_MSB				0x66

#define ECM_SDAM0_SAMPLE_START_ADDR		0x6C
#define ECM_SDAM_SAMPLE_END_ADDR		0xBF

/* ECM specific definitions */
#define ECM_SAMPLE_GAIN_V1			15
#define ECM_SAMPLE_GAIN_V2			16
#define ECM_MIN_M_SAMPLES			10
#define AMOLED_AB_REVISION_1P0			0
#define AMOLED_AB_REVISION_2P0			1

enum amoled_ecm_mode {
	ECM_MODE_CONTINUOUS = 0,
	ECM_MODE_MULTI_FRAMES,
	ECM_MODE_IDLE,
};

struct amoled_ecm_sdam_config {
	u8 reg;
	u8 reset_val;
};

/**
 * struct amoled_ecm_sdam - AMOLED ECM sdam data structure
 * @nvmem:		Pointer to nvmem device
 * @start_addr:		Start address of ECM samples in SDAM
 * @irq_name:		Interrupt name for SDAM
 * @irq:		Interrupt associated with the SDAM
 */
struct amoled_ecm_sdam {
	struct nvmem_device	*nvmem;
	u32			start_addr;
	const char		*irq_name;
	int			irq;
};

/**
 * struct amoled_ecm_data - Structure for AMOLED ECM data
 * @m_cumulative:	Cumulative of M sample values
 * @num_m_samples:	Number of M samples available
 * @time_period_ms:	Time period(in milli seconds) for ECM request
 * @frames:		Number of frames for ECM request
 * @avg_current:	AMOLED ECM average calculated
 * @mode:		AMOLED ECM mode of operation
 */
struct amoled_ecm_data {
	unsigned long long	m_cumulative;
	u32			num_m_samples;
	u32			time_period_ms;
	u16			frames;
	u16			avg_current;
	enum amoled_ecm_mode	mode;
};

/**
 * struct amoled ecm - Structure for AMOLED ECM device
 * @regmap:		Pointer for regmap structure
 * @dev:		Pointer for AMOLED ECM device
 * @data:		AMOLED ECM data structure
 * @sdam:		Pointer for array of ECM sdams
 * @sdam_lock:		Locking for mutual exclusion
 * @average_work:	Delayed work to calculate ECM average
 * @active_panel:	Active DRM panel which sends panel notifications
 * @notifier_cookie:	The cookie from panel notifier
 * @num_sdams:		Number of SDAMs used for AMOLED ECM
 * @base:		Base address of the AMOLED ECM module
 * @ab_revision:	Revision of the AMOLED AB module
 * @enable:		Flag to enable/disable AMOLED ECM
 * @abort:		Flag to indicated AMOLED ECM has aborted
 * @reenable:		Flag to reenable ECM when display goes unblank
 */
struct amoled_ecm {
	struct regmap		*regmap;
	struct device		*dev;
	struct amoled_ecm_data	data;
	struct amoled_ecm_sdam	*sdam;
	struct mutex		sdam_lock;
	struct delayed_work	average_work;
	struct drm_panel	*active_panel;
	void			*notifier_cookie;
	u32			num_sdams;
	u32			base;
	u8			ab_revision;
	bool			enable;
	bool			abort;
	bool			reenable;
};

static struct amoled_ecm_sdam_config ecm_reset_config[] = {
	{ ECM_FAULT_LOG,	0x00 },
	{ ECM_ROUTINE_LOG,	0x00 },
	{ ECM_ACTIVE_SDAM,	0x01 },
	{ ECM_SAMPLE_CNT_LSB,	0x00 },
	{ ECM_SAMPLE_CNT_MSB,	0x00 },
	{ ECM_STATUS_SET,	0x00 },
	{ ECM_STATUS_CLR,	0xFF },
	{ ECM_SDAM0_INDEX,	0x6C },
	{ ECM_SDAM1_INDEX,	0x46 },
	{ ECM_SDAM2_INDEX,	0x46 },
	{ ECM_MODE,		0x00 },
};

static int ecm_nvmem_device_write(struct nvmem_device *nvmem,
		       unsigned int offset,
		       size_t bytes, void *buf)
{
	size_t i;
	u8 *ptr = buf;

	for (i = 0; i < bytes; i++)
		pr_debug("Wrote %#x to %#x\n", *ptr++, offset + i);

	return nvmem_device_write(nvmem, offset, bytes, buf);
}

static int ecm_reset_sdam_config(struct amoled_ecm *ecm)
{
	int rc, i;
	u8 val = 0, val2 = 0;

	for (i = 0; i < ARRAY_SIZE(ecm_reset_config); i++) {
		rc = ecm_nvmem_device_write(ecm->sdam[0].nvmem,
				ecm_reset_config[i].reg,
				1, &ecm_reset_config[i].reset_val);
		if (rc < 0) {
			pr_err("Failed to write %u to SDAM, rc=%d\n",
				ecm_reset_config[i].reg, rc);
			return rc;
		}
	}

	for (i = 0; i < ecm->num_sdams; i++) {
		val |= (SEND_SDAM0_IRQ << i);
		val2 |= (WRITE_SDAM0_DATA << i);
	}

	rc = ecm_nvmem_device_write(ecm->sdam[0].nvmem, ECM_SEND_IRQ, 1, &val);
	if (rc < 0) {
		pr_err("Failed to write %u to ECM_SEND_IRQ, rc=%d\n", val, rc);
		return rc;
	}

	rc = ecm_nvmem_device_write(ecm->sdam[0].nvmem, ECM_WRITE_TO_SDAM, 1,
					&val2);
	if (rc < 0)
		pr_err("Failed to write %u to ECM_WRITE_TO_SDAM, rc=%d\n", val2,
			rc);

	usleep_range(10000, 12000);

	return rc;
}

static int amoled_ecm_enable(struct amoled_ecm *ecm)
{
	struct amoled_ecm_data *data = &ecm->data;
	int rc;

	if (data->frames) {
		rc = ecm_nvmem_device_write(ecm->sdam[0].nvmem,
				ECM_N_ESWIRE_COUNT_LSB, 2, &data->frames);
		if (rc < 0) {
			pr_err("Failed to write swire count to SDAM, rc=%d\n",
				rc);
			return rc;
		}

		data->mode = ECM_MODE_MULTI_FRAMES;
	} else {
		if (!data->time_period_ms)
			return -EINVAL;

		data->mode = ECM_MODE_CONTINUOUS;
	}

	if ((ecm->ab_revision != AMOLED_AB_REVISION_1P0) &&
			(ecm->ab_revision != AMOLED_AB_REVISION_2P0)) {
		pr_err("ECM is not supported for AB version %u\n",
			ecm->ab_revision);
		return -ENODEV;
	}

	rc = ecm_reset_sdam_config(ecm);
	if (rc < 0) {
		pr_err("Failed to reset ECM SDAM configuration, rc=%d\n", rc);
		return rc;
	}

	rc = ecm_nvmem_device_write(ecm->sdam[0].nvmem, ECM_MODE, 1,
				&data->mode);
	if (rc < 0) {
		pr_err("Failed to write ECM mode to SDAM, rc=%d\n", rc);
		return rc;
	}

	rc = regmap_write(ecm->regmap, ecm->base + AB_ECM_EN_CTL, ECM_EN);
	if (rc < 0) {
		pr_err("Failed to enable ECM, rc=%d\n", rc);
		return rc;
	}

	rc = regmap_write(ecm->regmap, ecm->base + AB_ECM_COUNTER_CTL,
		ECM_COUNTER_START);
	if (rc < 0) {
		pr_err("Failed to enable ECM counter, rc=%d\n", rc);
		return rc;
	}

	if (data->mode == ECM_MODE_CONTINUOUS)
		schedule_delayed_work(&ecm->average_work,
			msecs_to_jiffies(data->time_period_ms));

	ecm->enable = true;

	return rc;
}

static int amoled_ecm_disable(struct amoled_ecm *ecm)
{
	int rc;

	rc = regmap_write(ecm->regmap, ecm->base + AB_ECM_COUNTER_CTL, 0);
	if (rc < 0) {
		pr_err("Failed to disable ECM counter, rc=%d\n", rc);
		return rc;
	}

	rc = regmap_write(ecm->regmap, ecm->base + AB_ECM_EN_CTL, 0);
	if (rc < 0) {
		pr_err("Failed to disable ECM, rc=%d\n", rc);
		return rc;
	}

	rc = ecm_nvmem_device_write(ecm->sdam[0].nvmem, ECM_AVERAGE_LSB, 2,
				&ecm->data.avg_current);
	if (rc < 0) {
		pr_err("Failed to write ECM average to SDAM, rc=%d\n", rc);
		return rc;
	}
	pr_debug("ECM_AVERAGE:%u\n", ecm->data.avg_current);

	cancel_delayed_work(&ecm->average_work);

	ecm->data.avg_current = 0;
	ecm->data.m_cumulative = 0;
	ecm->data.num_m_samples = 0;
	ecm->data.mode = ECM_MODE_IDLE;

	ecm->abort = false;
	ecm->enable = false;

	return rc;
}

static void ecm_average_work(struct work_struct *work)
{
	struct amoled_ecm *ecm = container_of(work, struct amoled_ecm,
			average_work.work);
	struct amoled_ecm_data *data = &ecm->data;

	mutex_lock(&ecm->sdam_lock);

	if (!data->num_m_samples || !data->m_cumulative) {
		pr_warn_ratelimited("Invalid data, num_m_samples=%u m_cumulative:%u\n",
			data->num_m_samples, data->m_cumulative);
		data->avg_current = 0;
	} else {
		data->avg_current = data->m_cumulative / data->num_m_samples;
		pr_debug("avg_current=%u mA\n", data->avg_current);
	}

	data->m_cumulative = 0;
	data->num_m_samples = 0;

	mutex_unlock(&ecm->sdam_lock);

	/*
	 * If ECM is not aborted and still enabled, run it one more time
	 */

	if (!ecm->abort && ecm->enable)
		schedule_delayed_work(&ecm->average_work,
			msecs_to_jiffies(ecm->data.time_period_ms));
}

static ssize_t enable_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct amoled_ecm *ecm = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", ecm->enable);
}

static ssize_t enable_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct amoled_ecm *ecm = dev_get_drvdata(dev);
	bool val;
	int rc;

	rc = kstrtobool(buf, &val);
	if (rc < 0)
		return rc;

	if (ecm->enable == val) {
		pr_err("AMOLED ECM is already %s\n",
			val ? "enabled" : "disabled");
		return -EINVAL;
	}

	if (val) {
		rc = amoled_ecm_enable(ecm);
		if (rc < 0) {
			pr_err("Failed to enable AMOLED ECM, rc=%d\n", rc);
			return rc;
		}
	} else {
		rc = amoled_ecm_disable(ecm);
		if (rc < 0) {
			pr_err("Failed to disable AMOLED ECM, rc=%d\n", rc);
			return rc;
		}

		ecm->data.frames = 0;
		ecm->data.time_period_ms = 0;
	}

	return count;
}
static DEVICE_ATTR_RW(enable);

static ssize_t frames_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct amoled_ecm *ecm = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n", ecm->data.frames);
}

static ssize_t frames_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct amoled_ecm *ecm = dev_get_drvdata(dev);
	u16 val;
	int rc;

	if (ecm->enable) {
		pr_err("Failed to configure frames, ECM is already running\n");
		return -EINVAL;
	}

	rc = kstrtou16(buf, 0, &val);
	if ((rc < 0) || !val)
		return -EINVAL;

	ecm->data.frames = val;

	return count;
}
static DEVICE_ATTR_RW(frames);

static ssize_t time_period_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct amoled_ecm *ecm = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n", ecm->data.time_period_ms);
}

static ssize_t time_period_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct amoled_ecm *ecm = dev_get_drvdata(dev);
	u32 val;
	int rc;

	if (ecm->enable) {
		pr_err("Failed to configure time_period, ECM is already running\n");
		return -EINVAL;
	}

	rc = kstrtou32(buf, 0, &val);
	if ((rc < 0) || !val)
		return -EINVAL;

	ecm->data.time_period_ms = val;

	return count;
}
static DEVICE_ATTR_RW(time_period);

static ssize_t avg_current_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct amoled_ecm *ecm = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n", ecm->data.avg_current);
}
static DEVICE_ATTR_RO(avg_current);

static struct attribute *amoled_ecm_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_frames.attr,
	&dev_attr_time_period.attr,
	&dev_attr_avg_current.attr,
	NULL,
};

static const struct attribute_group amoled_ecm_group = {
	.name	= "amoled_ecm",
	.attrs	= amoled_ecm_attrs,
};
__ATTRIBUTE_GROUPS(amoled_ecm);

static int get_sdam_from_irq(struct amoled_ecm *ecm, int irq)
{
	int i;

	for (i = 0; i < ecm->num_sdams; i++)
		if (ecm->sdam[i].irq == irq)
			return i;

	return -ENOENT;
}

static int handle_ecm_abort(struct amoled_ecm *ecm)
{
	struct amoled_ecm_data *data = &ecm->data;
	int rc;
	u8 mode = data->mode;

	switch (mode) {
	case ECM_MODE_MULTI_FRAMES:
		pr_warn_ratelimited("Multiple frames mode is not supported\n");
		data->avg_current = 0;
		break;
	case ECM_MODE_CONTINUOUS:
		if (data->num_m_samples < ECM_MIN_M_SAMPLES) {
			pr_warn_ratelimited("Too few samples %u for continuous mode\n",
					data->num_m_samples);
			data->avg_current = 0;
			break;
		}

		ecm->abort = true;
		schedule_delayed_work(&ecm->average_work, 0);
		break;
	default:
		pr_err_ratelimited("Invalid ECM operation mode: %u\n", mode);
		data->avg_current = 0;
		return -EINVAL;
	}

	rc = amoled_ecm_disable(ecm);
	if (rc < 0)
		pr_err("Failed to disable AMOLED ECM, rc=%d\n", rc);

	return rc;
}

static int get_sdam_index(struct nvmem_device *nvmem, int sdam_num, u8 *index)
{
	unsigned int addr;

	switch (sdam_num) {
	case 0:
		addr = ECM_SDAM0_INDEX;
		break;
	case 1:
		addr = ECM_SDAM1_INDEX;
		break;
	case 2:
		addr = ECM_SDAM2_INDEX;
		break;
	default:
		return -EINVAL;
	}

	return nvmem_device_read(nvmem, addr, 1, index);
}

static irqreturn_t sdam_full_irq_handler(int irq, void *_ecm)
{
	struct amoled_ecm *ecm = _ecm;
	struct amoled_ecm_data *data = &ecm->data;
	u64 cumulative = 0, m_sample;
	int rc, i, sdam_num, sdam_start, num_ecm_samples, max_samples;
	u16 ecm_sample, gain;
	u8 buf[2], int_status, sdam_index, overwrite;

	sdam_num = get_sdam_from_irq(ecm, irq);
	if (sdam_num < 0) {
		pr_err("Invalid SDAM interrupt, err=%d\n", sdam_num);
		return IRQ_HANDLED;
	}

	rc = nvmem_device_read(ecm->sdam[0].nvmem, ECM_STATUS_SET, 1,
			&int_status);
	if (rc < 0) {
		pr_err("Failed to read interrupt status from SDAM, rc=%d\n",
			rc);
		return IRQ_HANDLED;
	}

	pr_debug("ECM_STATUS_SET: %#x\n", int_status);

	if (data->mode != ECM_MODE_CONTINUOUS &&
		data->mode != ECM_MODE_MULTI_FRAMES)
		return IRQ_HANDLED;

	if (int_status & ECM_ABORT) {
		rc = handle_ecm_abort(ecm);
		if (rc < 0) {
			pr_err("Failed to handle ECM_ABORT interrupt, rc=%d\n",
				rc);
			return IRQ_HANDLED;
		}
	}

	rc = get_sdam_index(ecm->sdam[0].nvmem, sdam_num, &sdam_index);
	if (rc < 0) {
		pr_err("Failed to read SDAM index, rc=%d\n", rc);
		goto irq_exit;
	}

	pr_debug("sdam_num:%d sdam_index:%#x\n", sdam_num, sdam_index);

	sdam_start = ecm->sdam[sdam_num].start_addr;
	max_samples = (ECM_SDAM_SAMPLE_END_ADDR + 1 - sdam_start) / 2;
	num_ecm_samples = (sdam_index + 1 - sdam_start) / 2;

	if (!num_ecm_samples || (num_ecm_samples > max_samples)) {
		pr_err("Incorrect number of ECM samples, num_ecm_samples:%d max_samples:%d\n",
				num_ecm_samples, max_samples);
		return IRQ_HANDLED;
	}

	mutex_lock(&ecm->sdam_lock);

	rc = nvmem_device_read(ecm->sdam[0].nvmem, ECM_WRITE_TO_SDAM, 1,
		&overwrite);
	if (rc < 0) {
		pr_err("Failed to read ECM_WRITE_TO_SDAM from SDAM, rc=%d\n",
			rc);
		goto irq_exit;
	}

	overwrite &= ~(OVERWRITE_SDAM0_DATA << sdam_num);
	rc = ecm_nvmem_device_write(ecm->sdam[0].nvmem, ECM_WRITE_TO_SDAM,
		1, &overwrite);
	if (rc < 0) {
		pr_err("Failed to write ECM_WRITE_TO_SDAM to SDAM, rc=%d\n",
			rc);
		goto irq_exit;
	}

	/*
	 * For AMOLED AB peripheral,
	 * Revision 1.0:
	 * ECM measured current = 15 times of each LSB
	 *
	 * Revision 2.0:
	 * ECM measured current = 16 times of each LSB
	 */

	if (ecm->ab_revision == AMOLED_AB_REVISION_1P0)
		gain = ECM_SAMPLE_GAIN_V1;
	else
		gain = ECM_SAMPLE_GAIN_V2;

	for (i = sdam_start; i < sdam_index; i += 2) {
		rc = nvmem_device_read(ecm->sdam[sdam_num].nvmem, i, 2, buf);
		if (rc < 0) {
			pr_err("Failed to read SDAM sample, rc=%d\n", rc);
			goto irq_exit;
		}

		ecm_sample = (buf[1] << 8) | buf[0];

		cumulative += ((ecm_sample * 1000) / gain) / 1000;
	}

	overwrite |= (OVERWRITE_SDAM0_DATA << sdam_num);
	rc = ecm_nvmem_device_write(ecm->sdam[0].nvmem, ECM_WRITE_TO_SDAM,
		1, &overwrite);
	if (rc < 0) {
		pr_err("Failed to write ECM_WRITE_TO_SDAM to SDAM, rc=%d\n",
			rc);
		goto irq_exit;
	}

	if (!cumulative) {
		pr_err("Error, No ECM samples captured. Cumulative:%lu\n",
			cumulative);
		goto irq_exit;
	}

	m_sample = cumulative / num_ecm_samples;
	data->m_cumulative += m_sample;
	data->num_m_samples++;

	buf[0] = (ECM_SDAM0_FULL << sdam_num);
	rc = ecm_nvmem_device_write(ecm->sdam[0].nvmem, ECM_STATUS_CLR, 1,
			&buf[0]);
	if (rc < 0) {
		pr_err("Failed to clear interrupt status in SDAM, rc=%d\n",
			rc);
		goto irq_exit;
	}

	if ((data->mode == ECM_MODE_MULTI_FRAMES) &&
			(sdam_index < max_samples))
		schedule_delayed_work(&ecm->average_work, 0);

irq_exit:
	mutex_unlock(&ecm->sdam_lock);

	return IRQ_HANDLED;
}

static int amoled_ecm_parse_dt(struct amoled_ecm *ecm)
{
	int rc = 0, i;
	u32 val;
	u8 buf[20];

	rc = of_property_read_u32(ecm->dev->of_node, "reg", &val);
	if (rc < 0) {
		pr_err("Failed to get reg, rc = %d\n", rc);
		return rc;
	}
	ecm->base = val;

	rc = of_property_count_strings(ecm->dev->of_node, "nvmem-names");
	if (rc < 0) {
		pr_err("Could not find nvmem device\n");
		return rc;
	}
	ecm->num_sdams = rc;

	ecm->sdam = devm_kcalloc(ecm->dev, ecm->num_sdams,
				sizeof(*ecm->sdam), GFP_KERNEL);
	if (!ecm->sdam)
		return -ENOMEM;

	for (i = 0; i < ecm->num_sdams; i++) {
		scnprintf(buf, sizeof(buf), "ecm-sdam%d", i);

		rc = of_irq_get_byname(ecm->dev->of_node, buf);
		if (rc < 0) {
			pr_err("Failed to get irq for ecm sdam, err=%d\n", rc);
			return -EINVAL;
		}

		ecm->sdam[i].irq_name = devm_kstrdup(ecm->dev, buf,
						GFP_KERNEL);
		if (!ecm->sdam[i].irq_name)
			return -ENOMEM;

		ecm->sdam[i].irq = rc;

		scnprintf(buf, sizeof(buf), "amoled-ecm-sdam%d", i);

		ecm->sdam[i].nvmem = devm_nvmem_device_get(ecm->dev, buf);
		if (IS_ERR(ecm->sdam[i].nvmem)) {
			rc = PTR_ERR(ecm->sdam[i].nvmem);
			if (rc != -EPROBE_DEFER)
				pr_err("Failed to get nvmem device, rc=%d\n",
					rc);
			ecm->sdam[i].nvmem = NULL;
			return rc;
		}
	}

	return 0;
}

#if IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
static void panel_event_notifier_callback(enum panel_event_notifier_tag tag,
		struct panel_event_notification *notification, void *data)
{
	struct amoled_ecm *ecm = data;
	int rc;

	if (!notification) {
		pr_err("Invalid panel notification\n");
		return;
	}

	pr_debug("panel event received, type: %d\n", notification->notif_type);
	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_BLANK:
		if (ecm->enable) {
			rc = amoled_ecm_disable(ecm);
			if (rc < 0) {
				pr_err("Failed to disable ECM for display BLANK, rc=%d\n",
						rc);
				return;
			}

			ecm->reenable = true;
			pr_debug("Disabled ECM for display BLANK\n");
		}
		break;
	case DRM_PANEL_EVENT_UNBLANK:
		if (ecm->reenable) {
			rc = amoled_ecm_enable(ecm);
			if (rc < 0) {
				pr_err("Failed to re-enable ECM for display UNBLANK, rc=%d\n",
						rc);
				return;
			}

			ecm->reenable = false;
			pr_debug("Enabled ECM for display UNBLANK\n");
		}
		break;
	default:
		pr_debug("Ignore panel event: %d\n", notification->notif_type);
		break;
	}
}

static int qti_amoled_register_panel_notifier(struct amoled_ecm *ecm)
{
	struct device_node *np = ecm->dev->of_node;
	struct device_node *pnode;
	struct drm_panel *panel;
	void *cookie = NULL;
	int i, count, rc;

	count = of_count_phandle_with_args(np, "display-panels", NULL);
	if (count <= 0)
		return 0;

	for (i = 0; i < count; i++) {
		pnode = of_parse_phandle(np, "display-panels", i);
		if (!pnode)
			return -ENODEV;

		panel = of_drm_find_panel(pnode);
		of_node_put(pnode);
		if (!IS_ERR(panel)) {
			ecm->active_panel = panel;
			break;
		}
	}

	if (!ecm->active_panel) {
		rc = PTR_ERR(panel);
		if (rc != -EPROBE_DEFER)
			pr_err("failed to find active panel, rc=%d\n", rc);

		return rc;
	}

	cookie = panel_event_notifier_register(
			PANEL_EVENT_NOTIFICATION_PRIMARY,
			PANEL_EVENT_NOTIFIER_CLIENT_ECM,
			ecm->active_panel,
			panel_event_notifier_callback,
			(void *)ecm);
	if (IS_ERR(cookie)) {
		rc = PTR_ERR(cookie);
		pr_err("failed to register panel event notifier, rc=%d\n", rc);
		return rc;
	}

	pr_debug("register panel notifier successfully\n");
	ecm->notifier_cookie = cookie;
	return 0;
}

static int qti_amoled_unregister_panel_notifier(struct amoled_ecm *ecm)
{
	if (ecm->notifier_cookie)
		panel_event_notifier_unregister(ecm->notifier_cookie);

	return 0;
}
#else
static inline int qti_amoled_register_panel_notifier(struct amoled_ecm *ecm)
{
	return 0;
}

static inline int qti_amoled_unregister_panel_notifier(struct amoled_ecm *ecm)
{
	return 0;
}
#endif

static int qti_amoled_ecm_probe(struct platform_device *pdev)
{
	struct device *hwmon_dev;
	struct amoled_ecm *ecm;
	int rc, i;
	unsigned int temp;

	ecm = devm_kzalloc(&pdev->dev, sizeof(*ecm), GFP_KERNEL);
	if (!ecm)
		return -ENOMEM;

	ecm->dev = &pdev->dev;

	ecm->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!ecm->regmap) {
		dev_err(&pdev->dev, "Failed to get regmap\n");
		return -EINVAL;
	}

	rc = amoled_ecm_parse_dt(ecm);
	if (rc < 0) {
		dev_err(&pdev->dev, "Failed to parse AMOLED ECM rc=%d\n", rc);
		return rc;
	}

	rc = regmap_read(ecm->regmap, ecm->base + AB_REVISION2, &temp);
	if (rc < 0) {
		dev_err(&pdev->dev, "Failed to read AB revision, rc=%d\n", rc);
		return rc;
	}
	ecm->ab_revision = temp;

	ecm->enable = false;
	ecm->abort = false;

	ecm->data.m_cumulative = 0;
	ecm->data.num_m_samples = 0;
	ecm->data.time_period_ms = 0;
	ecm->data.frames = 0;
	ecm->data.avg_current = 0;
	ecm->data.mode = ECM_MODE_IDLE;

	INIT_DELAYED_WORK(&ecm->average_work, ecm_average_work);

	mutex_init(&ecm->sdam_lock);

	dev_set_drvdata(ecm->dev, ecm);

	for (i = 0; i < ecm->num_sdams; i++) {
		rc = devm_request_threaded_irq(ecm->dev, ecm->sdam[i].irq,
				NULL, sdam_full_irq_handler,
				IRQF_ONESHOT, ecm->sdam[i].irq_name, ecm);
		if (rc < 0) {
			dev_err(&pdev->dev, "Failed to request IRQ(%s), rc=%d\n",
				ecm->sdam[i].irq_name, rc);
			return rc;
		}

		ecm->sdam[i].start_addr = i ? ECM_SDAMX_SAMPLE_START_ADDR
				: ECM_SDAM0_SAMPLE_START_ADDR;
	}

	hwmon_dev = devm_hwmon_device_register_with_groups(&pdev->dev,
				"amoled_ecm", ecm, amoled_ecm_groups);
	if (IS_ERR_OR_NULL(hwmon_dev)) {
		rc = PTR_ERR(hwmon_dev);
		pr_err("failed to register hwmon device for amoled-ecm, rc=%d\n",
				rc);
		return rc;
	}

	return qti_amoled_register_panel_notifier(ecm);
}

static int qti_amoled_ecm_remove(struct platform_device *pdev)
{
	struct amoled_ecm *ecm = dev_get_drvdata(&pdev->dev);

	return qti_amoled_unregister_panel_notifier(ecm);
}

static const struct of_device_id amoled_ecm_match_table[] = {
	{ .compatible = "qcom,amoled-ecm", },
	{ },
};

static struct platform_driver qti_amoled_ecm_driver = {
	.driver = {
		.name = "qti_amoled_ecm",
		.of_match_table = amoled_ecm_match_table,
	},
	.probe = qti_amoled_ecm_probe,
	.remove = qti_amoled_ecm_remove,
};
module_platform_driver(qti_amoled_ecm_driver);

MODULE_DESCRIPTION("QTI AMOLED ECM driver");
MODULE_LICENSE("GPL v2");
