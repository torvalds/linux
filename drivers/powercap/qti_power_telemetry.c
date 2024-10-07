// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"qti_qpt: %s: " fmt, __func__

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/sched/clock.h>

#include "qti_power_telemetry.h"

#define QPT_CONFIG_SDAM_BASE_OFF	0x45
#define QPT_DATA_SDAM_BASE_OFF		0x45
#define QPT_CH_ENABLE_MASK		BIT(7)
#define QPT_SID_MASK		GENMASK(3, 0)
#define QPT_GANG_NUM_MASK               0x70
#define QPT_DATA_BYTE_SIZE			2
#define QPT_DATA_TO_POWER_UW			1500L  /* 1 LSB = 1.5 mW */

#define QPT_GET_POWER_UW_FROM_ADC(adc)	((adc) * QPT_DATA_TO_POWER_UW)
#define QPT_SDAM_SAMPLING_MS		1280

static int qpt_sdam_nvmem_read(struct qpt_priv *qpt, struct qpt_sdam *sdam,
		uint16_t offset, size_t bytes, void *data)
{
	int rc = 0;

	mutex_lock(&sdam->lock);
	rc = nvmem_device_read(sdam->nvmem, offset, bytes, data);
	mutex_unlock(&sdam->lock);
	if (rc < 0)
		dev_err(qpt->dev,
			"Failed to read sdam[%d] off:%#x,size:%ld rc=%d\n",
			sdam->id, offset, bytes, rc);
	return rc;
}

static int qti_qpt_read_rtc_time(struct qpt_priv *qpt, u64 *rtc_ts)
{
	int rc = -1;

	rc = qpt_sdam_nvmem_read(qpt, &qpt->sdam[DATA_AVG_SDAM],
			QPT_DATA_SDAM_BASE_OFF + DATA_SDAM_RTC0, 4, &rtc_ts);
	if (rc < 0)
		return rc;

	return 0;
}

static void qpt_channel_avg_data_update(struct qpt_device *qpt_dev,
		uint8_t lsb, uint8_t msb, u64 ts)
{
	mutex_lock(&qpt_dev->lock);
	qpt_dev->last_data = (msb << 8) | lsb;
	qpt_dev->last_data_uw = QPT_GET_POWER_UW_FROM_ADC(qpt_dev->last_data);
	mutex_unlock(&qpt_dev->lock);
	QPT_DBG(qpt_dev->priv, "qpt[%s]: power:%lluuw msb:0x%x lsb:0x%x",
			qpt_dev->name, qpt_dev->last_data_uw, msb, lsb);
}

static int qti_qpt_read_seq_count(struct qpt_priv *qpt, int *seq_count)
{
	int rc = -1;

	rc = qpt_sdam_nvmem_read(qpt, &qpt->sdam[DATA_AVG_SDAM],
			QPT_DATA_SDAM_BASE_OFF + DATA_SDAM_SEQ_START, 1, &seq_count);
	if (rc < 0)
		return rc;

	return 0;
}

static int qti_qpt_read_all_data(struct qpt_priv *qpt, uint16_t offset, size_t size)
{
	uint8_t data_sdam_avg[DATA_SDAM_POWER_MSB_CH48 + 1] = {0};
	int seq_count = 0;
	int rc = 0;
	struct qpt_device *qpt_dev;
	int seq_count_start = -1;

	rc = qti_qpt_read_seq_count(qpt, &seq_count);
	if (rc < 0)
		return rc;

	do {
		seq_count_start = seq_count;
		rc = qpt_sdam_nvmem_read(qpt, &qpt->sdam[DATA_AVG_SDAM], offset,
				size, data_sdam_avg);
		if (rc < 0)
			return rc;

		rc = qti_qpt_read_seq_count(qpt, &seq_count);
		if (rc < 0)
			return rc;

	} while (seq_count < seq_count_start);

	qpt->hw_read_ts = ktime_get();
	qti_qpt_read_rtc_time(qpt, &qpt->rtc_ts);
	list_for_each_entry(qpt_dev, &qpt->qpt_dev_head, qpt_node) {
		if (!qpt_dev->enabled)
			continue;
		if (qpt_dev->data_offset >= (offset + size))
			continue;
		qpt_channel_avg_data_update(qpt_dev,
			data_sdam_avg[qpt_dev->data_offset],
			data_sdam_avg[qpt_dev->data_offset + 1],
			qpt->hw_read_ts);
	}
	QPT_DBG(qpt, "Time(us) to read all channel:%lldus & RTC Time:%lld",
		ktime_to_us(ktime_sub(ktime_get(), qpt->hw_read_ts)),
		qpt->rtc_ts);

	return 0;
}

static void qti_qpt_get_power(struct qpt_device *qpt_dev, u64 *power_uw)
{
	mutex_lock(&qpt_dev->lock);
	*power_uw = qpt_dev->last_data_uw;
	mutex_unlock(&qpt_dev->lock);
}

static int qti_qpt_read_data_update(struct qpt_priv *qpt)
{
	int rc = 0;

	mutex_lock(&qpt->hw_read_lock);
	rc = qti_qpt_read_all_data(qpt,
		QPT_DATA_SDAM_BASE_OFF + DATA_SDAM_POWER_LSB_CH1,
		qpt->last_ch_offset + 2);
	mutex_unlock(&qpt->hw_read_lock);

	if (rc < 0)
		return rc;

	return 0;
}

static irqreturn_t qpt_sdam_irq_handler(int irq, void *data)
{
	struct qpt_priv *qpt = data;

	qti_qpt_read_data_update(qpt);

	return IRQ_HANDLED;
}

static int get_dt_index_from_ppid(struct qpt_device *qpt_dev)
{
	uint16_t  ppid = 0, i = 0;
	struct qpt_priv *qpt = qpt_dev->priv;

	if (!qpt_dev->enabled || !qpt->dt_reg_cnt)
		return -EINVAL;

	ppid = qpt_dev->sid << 8 | qpt_dev->pid;

	for (i = 0; i < qpt->dt_reg_cnt; i++) {
		if (ppid == qpt->reg_ppid_map[i])
			return i;
	}

	return -ENODEV;
}

static int qti_qpt_config_sdam_initialize(struct qpt_priv *qpt)
{
	uint8_t *config_sdam = NULL;
	struct qpt_device *qpt_dev = NULL;
	int rc = 0;
	uint8_t conf_idx, data_idx;

	if (!qpt->sdam[CONFIG_SDAM].nvmem) {
		dev_err(qpt->dev, "Invalid sdam nvmem\n");
		return -EINVAL;
	}

	config_sdam = devm_kcalloc(qpt->dev, MAX_CONFIG_SDAM_DATA,
				sizeof(*config_sdam), GFP_KERNEL);
	if (!config_sdam)
		return -ENOMEM;

	rc = qpt_sdam_nvmem_read(qpt, &qpt->sdam[CONFIG_SDAM],
			QPT_CONFIG_SDAM_BASE_OFF,
			MAX_CONFIG_SDAM_DATA, config_sdam);
	if (rc < 0)
		return rc;

	if (!(config_sdam[CONFIG_SDAM_QPT_MODE] & BIT(7))) {
		dev_err(qpt->dev, "pmic qpt is in disabled state, reg:0x%x\n",
			config_sdam[CONFIG_SDAM_QPT_MODE]);
		return -ENODEV;
	}
	qpt->mode = config_sdam[CONFIG_SDAM_QPT_MODE] & BIT(0);
	qpt->max_data = config_sdam[CONFIG_SDAM_MAX_DATA];
	qpt->config_sdam_data = config_sdam;

	/* logic to read number of channels and die_temps */
	for (conf_idx = CONFIG_SDAM_CONFIG_1, data_idx = 0;
	 conf_idx <= CONFIG_SDAM_CONFIG_48;
	 conf_idx += 2, data_idx += QPT_DATA_BYTE_SIZE) {
		const char *reg_name;

		if (!(config_sdam[conf_idx] & QPT_CH_ENABLE_MASK))
			continue;

		qpt->num_reg++;
		qpt_dev = devm_kzalloc(qpt->dev, sizeof(*qpt_dev), GFP_KERNEL);
		if (!qpt_dev)
			return -ENOMEM;
		qpt_dev->enabled = true;
		qpt_dev->sid = config_sdam[conf_idx] & QPT_SID_MASK;
		qpt_dev->gang_num = config_sdam[conf_idx] & QPT_GANG_NUM_MASK;
		qpt_dev->pid = config_sdam[conf_idx + 1];
		qpt_dev->priv = qpt;
		qpt_dev->data_offset = data_idx;
		mutex_init(&qpt_dev->lock);
		if (data_idx > qpt->last_ch_offset)
			qpt->last_ch_offset = data_idx;

		rc = get_dt_index_from_ppid(qpt_dev);
		if (rc < 0 || rc >= qpt->dt_reg_cnt) {
			dev_err(qpt->dev, "No matching channel ppid, rc:%d\n",
				rc);
			return rc;
		}
		of_property_read_string_index(qpt->dev->of_node,
				"qcom,reg-ppid-names", rc, &reg_name);
		dev_dbg(qpt->dev, "%s: qpt channel:%s off:0x%x\n", __func__,
				reg_name, data_idx);
		strscpy(qpt_dev->name, reg_name, sizeof(qpt_dev->name));

		list_add(&qpt_dev->qpt_node, &qpt->qpt_dev_head);
	}

	return 0;
}

static int qpt_get_sdam_nvmem(struct device *dev, struct qpt_sdam *sdam,
			char *sdam_name)
{
	int rc = 0;

	sdam->nvmem = devm_nvmem_device_get(dev, sdam_name);
	if (IS_ERR(sdam->nvmem)) {
		rc = PTR_ERR(sdam->nvmem);
		if (rc != -EPROBE_DEFER)
			dev_err(dev, "Failed to get nvmem device, rc=%d\n",
				rc);
		sdam->nvmem = NULL;
		return rc;
	}

	return rc;
}

static int qpt_parse_sdam_data(struct qpt_priv *qpt)
{
	int rc = 0;
	char buf[20];

	rc = of_property_count_strings(qpt->dev->of_node, "nvmem-names");
	if (rc < 0) {
		dev_err(qpt->dev, "Could not find nvmem device\n");
		return rc;
	}
	if (rc != MAX_QPT_SDAM) {
		dev_err(qpt->dev, "Invalid num of SDAMs:%d\n", rc);
		return -EINVAL;
	}

	qpt->num_sdams = rc;
	qpt->sdam = devm_kcalloc(qpt->dev, qpt->num_sdams,
				sizeof(*qpt->sdam), GFP_KERNEL);
	if (!qpt->sdam)
		return -ENOMEM;

	/* Check for config sdam */
	qpt->sdam[0].id = CONFIG_SDAM;
	scnprintf(buf, sizeof(buf), "qpt-config-sdam");
	mutex_init(&qpt->sdam[0].lock);
	rc = qpt_get_sdam_nvmem(qpt->dev, &qpt->sdam[0], buf);
	if (rc < 0)
		return rc;

	/* Check data sdam */
	qpt->sdam[1].id = DATA_AVG_SDAM;
	mutex_init(&qpt->sdam[1].lock);
	scnprintf(buf, sizeof(buf), "qpt-data-sdam");
	rc = qpt_get_sdam_nvmem(qpt->dev, &qpt->sdam[1], buf);
	if (rc < 0)
		return rc;

	return 0;
}

static int qpt_pd_callback(struct notifier_block *nfb,
				unsigned long action, void *v)
{
	struct qpt_priv *qpt = container_of(nfb, struct qpt_priv, genpd_nb);
	ktime_t now;
	s64 diff;
	struct qpt_device *qpt_dev;

	if (atomic_read(&qpt->in_suspend))
		goto cb_exit;

	switch (action) {
	case GENPD_NOTIFY_OFF:
		if (qpt->irq_enabled) {
			disable_irq_nosync(qpt->irq);
			qpt->irq_enabled = false;
		}
		break;
	case GENPD_NOTIFY_ON:
		if (qpt->irq_enabled)
			break;
		now = ktime_get();
		diff = ktime_to_ms(ktime_sub(now, qpt->hw_read_ts));
		if (diff > QPT_SDAM_SAMPLING_MS) {
			list_for_each_entry(qpt_dev, &qpt->qpt_dev_head,
						qpt_node) {
				qpt_dev->last_data = 0;
				qpt_dev->last_data_uw = 0;
			}
		}
		enable_irq(qpt->irq);
		qpt->irq_enabled = true;
		break;
	default:
		break;
	}
cb_exit:
	return NOTIFY_OK;
}

static int qti_qpt_pd_notifier_register(struct qpt_priv *qpt, struct device *dev)
{
	int ret;

	pm_runtime_enable(dev);
	qpt->genpd_nb.notifier_call = qpt_pd_callback;
	qpt->genpd_nb.priority = INT_MIN;
	ret = dev_pm_genpd_add_notifier(dev, &qpt->genpd_nb);
	if (ret)
		pm_runtime_disable(dev);
	return ret;
}

static int qpt_parse_dt(struct qpt_priv *qpt)
{
	struct platform_device *pdev;
	int rc = 0;
	struct device_node *np = qpt->dev->of_node;

	pdev = of_find_device_by_node(np);
	if (!pdev) {
		dev_err(qpt->dev, "Invalid pdev\n");
		return -ENODEV;
	}

	rc = of_property_count_strings(np, "qcom,reg-ppid-names");
	if (rc < 1 || rc >= QPT_POWER_CH_MAX) {
		dev_err(qpt->dev,
			"Invalid ppid name mapping count, rc=%d\n", rc);
		return rc;
	}
	qpt->dt_reg_cnt = rc;

	rc = of_property_count_elems_of_size(np, "qcom,reg-ppid-ids",
						sizeof(u16));
	if (rc < 1 || rc >= QPT_POWER_CH_MAX || rc != qpt->dt_reg_cnt) {
		dev_err(qpt->dev,
			"Invalid ppid mapping count, rc = %d strings:%d\n",
				rc, qpt->dt_reg_cnt);
		return rc;
	}

	rc = of_property_read_u16_array(np, "qcom,reg-ppid-ids",
			qpt->reg_ppid_map, qpt->dt_reg_cnt);
	if (rc < 0) {
		dev_err(qpt->dev,
			"Failed to read ppid mapping array, rc = %d\n", rc);
		return rc;
	}

	rc = qpt_parse_sdam_data(qpt);
	if (rc < 0)
		return rc;

	rc = platform_get_irq(pdev, 0);
	if (rc <= 0) {
		dev_err(qpt->dev, "Failed to get qpt irq, rc=%d\n", rc);
		return -EINVAL;
	}
	qpt->irq = rc;

	if (of_find_property(np, "power-domains", NULL) && pdev->dev.pm_domain) {
		rc = qti_qpt_pd_notifier_register(qpt, &pdev->dev);
		if (rc) {
			dev_err(qpt->dev, "Failed to register for pd notifier\n");
			return rc;
		}
	}

	return 0;
}

static int qti_qpt_hw_init(struct qpt_priv *qpt)
{
	int rc;

	if (qpt->initialized)
		return 0;

	mutex_init(&qpt->hw_read_lock);
	INIT_LIST_HEAD(&qpt->qpt_dev_head);

	rc = qpt_parse_dt(qpt);
	if (rc < 0) {
		dev_err(qpt->dev, "Failed to parse qpt rc=%d\n", rc);
		return rc;
	}

	rc = qti_qpt_config_sdam_initialize(qpt);
	if (rc < 0) {
		dev_err(qpt->dev, "Failed to parse config sdam rc=%d\n", rc);
		return rc;
	}
	atomic_set(&qpt->in_suspend, 0);

	rc = devm_request_threaded_irq(qpt->dev, qpt->irq,
			NULL, qpt_sdam_irq_handler,
			IRQF_ONESHOT, "qti_qpt_irq", qpt);
	if (rc < 0) {
		dev_err(qpt->dev,
			"Failed to request IRQ for qpt, rc=%d\n", rc);
		return rc;
	}
	irq_set_status_flags(qpt->irq, IRQ_DISABLE_UNLAZY);
	qpt->irq_enabled = true;

	qpt->initialized = true;
	/* Update first reading for all channels */
	qti_qpt_read_data_update(qpt);

	return 0;
}

static int qti_qpt_suspend(struct qpt_priv *qpt)
{
	atomic_set(&qpt->in_suspend, 1);

	if (qpt->irq_enabled) {
		disable_irq_nosync(qpt->irq);
		qpt->irq_enabled = false;
	}

	return 0;
}

static int qti_qpt_resume(struct qpt_priv *qpt)
{
	struct qpt_device *qpt_dev = NULL;
	ktime_t now;
	s64 diff;

	now = ktime_get();
	diff = ktime_to_ms(ktime_sub(now, qpt->hw_read_ts));
	if (diff > QPT_SDAM_SAMPLING_MS) {
		list_for_each_entry(qpt_dev, &qpt->qpt_dev_head,
					qpt_node) {
			qpt_dev->last_data = 0;
			qpt_dev->last_data_uw = 0;
		}
	}
	if (!qpt->irq_enabled) {
		enable_irq(qpt->irq);
		qpt->irq_enabled = true;
	}
	atomic_set(&qpt->in_suspend, 0);

	return 0;
}

static void qti_qpt_hw_release(struct qpt_priv *qpt)
{
	pm_runtime_disable(qpt->dev);
	dev_pm_genpd_remove_notifier(qpt->dev);
}

struct qpt_ops qpt_hw_ops = {
	.init = qti_qpt_hw_init,
	.get_power = qti_qpt_get_power,
	.suspend   = qti_qpt_suspend,
	.resume   = qti_qpt_resume,
	.release = qti_qpt_hw_release,
};

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Power Telemetry driver");
MODULE_LICENSE("GPL");
