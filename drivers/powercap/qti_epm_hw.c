// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"qti_epm: %s: " fmt, __func__

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sched/clock.h>
#include <linux/workqueue.h>
#include "qti_epm.h"
#include <linux/delay.h>

#define EPM_CONFIG_SDAM_BASE_OFF	0x45
#define EPM_DATA_SDAM_BASE_OFF		0x45
#define EPM_CH_ENABLE_MASK		BIT(7)
#define EPM_SID_MASK                    0xf
#define EPM_GANG_NUM_MASK               0x70
#define EPM_DATA_BYTE_SIZE		2
#define EPM_NSEC_PER_SEC		1000000000L
#define EPM_DATA_TO_POWER_UW            1500L  /* 1 LSB = 1.5 mW */
#define EPM_TZ_BYTE_SIZE		1
#define EPM_TZ_MIN_VAL_IN_C		50

#define EPM_GET_POWER_UW_FROM_ADC(adc) ((adc) * EPM_DATA_TO_POWER_UW)
#define EPM_GET_TEMP_MC_FROM_ADC(adc) (((adc) - EPM_TZ_MIN_VAL_IN_C) * 1000)
#define EPM_AVG_SDAM_RETRY_DELAY msecs_to_jiffies(200)

static int epm_sdam_nvmem_read(struct epm_priv *epm, struct epm_sdam *sdam,
		uint16_t offset, size_t bytes, uint8_t *data)
{
	int rc = 0;

	mutex_lock(&sdam->lock);
	rc = nvmem_device_read(sdam->nvmem, offset, bytes, data);
	mutex_unlock(&sdam->lock);
	EPM_DBG(epm, "sdam[%d] off:0x%x,size:%d rc=%d data[0]:0x%x data[1]:0x%x",
			sdam->id, offset, bytes, rc, data[0], data[1]);
	if (rc < 0)
		dev_err(epm->dev,
			"Failed to read sdam[%d] off:0x%x,size:%d rc=%d\n",
			sdam->id, offset, bytes, rc);

	return rc;
}

static struct epm_sdam *get_data_sdam_from_pid(struct epm_priv *epm, uint8_t pid)
{
	if (!epm->data_1s_base_pid ||
		(pid - epm->data_1s_base_pid) >= (MAX_EPM_SDAM - DATA_1_SDAM)) {
		dev_err(epm->dev, "Invalid sdam pids, base=0x%x curr=0x%x\n",
			epm->data_1s_base_pid, pid);
		return ERR_PTR(-EINVAL);
	}

	if (pid  < epm->data_1s_base_pid)
		pid = epm->data_1s_base_pid;

	return &epm->sdam[DATA_1_SDAM + (pid - epm->data_1s_base_pid)];
}

static int epm_validate_data_sdam_sequence_matching(struct epm_priv *epm,
		struct epm_sdam *sdam)
{
	int rc = 0;
	uint8_t data_counter[2] = {0};

	rc = epm_sdam_nvmem_read(epm, sdam,
			EPM_DATA_SDAM_BASE_OFF + DATA_SDAM_SEQ_START,
			2, data_counter);
	if (rc < 0)
		return rc;

	if (!data_counter[0] ||
		data_counter[0] !=
			data_counter[1]) {
		EPM_DBG(epm,
			"sdam[%d] No matching counter START:%d END:%d, rc=%d",
			sdam->id, data_counter[0], data_counter[1], rc);
		return -EBUSY;
	}

	return 0;
}

static struct epm_sdam *get_prev_data_sdam(struct epm_priv *epm,
		struct epm_sdam *cur_sdam)
{
	enum epm_sdam_id id;
	struct epm_sdam *prev;

	if (cur_sdam->id - 1 < DATA_1_SDAM)
		id = DATA_11_SDAM;
	else
		id = cur_sdam->id - 1;

	 prev = &epm->sdam[id];
	if (prev && (prev != cur_sdam))
		return prev;

	return NULL;
}

static enum epm_mode qti_epm_get_mode(struct epm_priv *epm)
{
	if (!epm || !epm->initialized)
		return -ENODEV;

	return epm->mode;
}

static bool epm_is_need_hw_read(u64 last_timestamp)
{
	if (sched_clock() - last_timestamp < EPM_NSEC_PER_SEC)
		return false;
	return true;
}

static void epm_channel_avg_data_update(struct epm_device *epm_dev,
		uint8_t lsb, uint8_t msb, u64 ts)
{
	epm_dev->last_avg_data = (msb << 8) | lsb;
	epm_dev->last_avg_data_uw = EPM_GET_POWER_UW_FROM_ADC((msb << 8) | lsb);
	epm_dev->avg_time_stamp = ts;
	EPM_DBG(epm_dev->priv, "epm[%s]:avg power:%duw msb:0x%x lsb:0x%x",
			epm_dev->name, epm_dev->last_avg_data_uw, msb, lsb);
}

static int qti_epm_read_acat_10s_avg_data_common(struct epm_priv *epm,
					struct epm_device *epm_dev,
					uint16_t offset, size_t size)
{
	uint8_t data_sdam_avg[DATA_SDAM_POWER_MSB_CH48 + 1] = {0};
	int rc = 0;
	struct epm_device *epm_dev_tmp;

	rc = epm_validate_data_sdam_sequence_matching(epm,
			&epm->sdam[DATA_AVG_SDAM]);
	if (rc < 0) {
		if (rc == -EBUSY) {
			dev_dbg(epm->dev,
				"Retry avg data update after sometime\n");
			schedule_delayed_work(&epm->avg_data_work,
				EPM_AVG_SDAM_RETRY_DELAY);
		}
		return rc;
	}
	rc = epm_sdam_nvmem_read(epm, &epm->sdam[DATA_AVG_SDAM], offset,
			size, data_sdam_avg);
	if (rc < 0)
		return rc;

	if (!epm_dev && size > EPM_DATA_BYTE_SIZE) {
		epm->all_avg_read_ts = sched_clock();
		list_for_each_entry(epm_dev_tmp, &epm->epm_dev_head, epm_node) {
			if (!epm_dev_tmp->enabled)
				continue;
			if (epm_dev_tmp->data_offset >= (offset + size))
				continue;
			epm_channel_avg_data_update(epm_dev_tmp,
				data_sdam_avg[epm_dev_tmp->data_offset],
				data_sdam_avg[epm_dev_tmp->data_offset + 1],
				epm->all_avg_read_ts);
		}
	} else if (epm_dev && size == EPM_DATA_BYTE_SIZE) {
		epm_channel_avg_data_update(epm_dev, data_sdam_avg[0],
				data_sdam_avg[1], sched_clock());
	}

	return 0;
}

static int qti_epm_update_acat_10s_avg_full_data(struct epm_priv *epm)
{
	int rc = 0;

	mutex_lock(&epm->avg_read_lock);
	if (epm_is_need_hw_read(epm->all_avg_read_ts))
		rc = qti_epm_read_acat_10s_avg_data_common(epm, NULL,
			EPM_DATA_SDAM_BASE_OFF + DATA_SDAM_POWER_LSB_CH1,
			epm->last_ch_offset + 2);
	mutex_unlock(&epm->avg_read_lock);
	if (rc < 0)
		return rc;

	return 0;
}

static int qti_epm_read_acat_10s_avg_channel(struct epm_device *epm_dev,
		u64 *power_uw)
{
	struct epm_priv *epm = epm_dev->priv;
	int rc = 0;

	if (epm_dev->data_offset >= DATA_SDAM_POWER_MSB_CH48)
		return -EINVAL;

	mutex_lock(&epm->avg_read_lock);
	if (epm_is_need_hw_read(epm_dev->avg_time_stamp))
		rc = qti_epm_read_acat_10s_avg_data_common(epm, epm_dev,
				EPM_DATA_SDAM_BASE_OFF + DATA_SDAM_POWER_LSB_CH1
				+ epm_dev->data_offset, EPM_DATA_BYTE_SIZE);
	mutex_unlock(&epm->avg_read_lock);
	if (rc >= 0 || rc == -EBUSY) {
		rc = 0;
		*power_uw = epm_dev->last_avg_data_uw;
	}

	return rc;
}

static int epm_get_latest_sdam_pid(struct epm_priv *epm, uint8_t *pid)
{
	int rc = 0;

	rc = epm_sdam_nvmem_read(epm, &epm->sdam[CONFIG_SDAM],
			EPM_CONFIG_SDAM_BASE_OFF + CONFIG_SDAM_LAST_FULL_SDAM,
			1, pid);
	if (rc < 0)
		return rc;

	return rc;
}

static struct epm_sdam *get_next_valid_data_1s_sdam(struct epm_priv *epm,
				struct epm_sdam *sdam)
{
	uint8_t data_sdam_pid;
	int rc = 0, idx = 0;

	if (!sdam) {
		/* get latest data sdam pid */
		rc = epm_get_latest_sdam_pid(epm, &data_sdam_pid);
		if (rc < 0)
			return ERR_PTR(-ENODEV);

		/* Better save last sdam */
		epm->last_sdam_pid = data_sdam_pid;

		/* Get data sdam from sdam pid */
		sdam = get_data_sdam_from_pid(epm, data_sdam_pid);
		if (IS_ERR(sdam))
			return sdam;
	} else {
		sdam = get_prev_data_sdam(epm, sdam);
		if (!sdam)
			return ERR_PTR(-ENODEV);
	}

	rc = epm_validate_data_sdam_sequence_matching(epm, sdam);
	while (idx < (MAX_EPM_SDAM - 2) && rc != 0) {
		sdam = get_prev_data_sdam(epm, sdam);
		if (!sdam)
			return ERR_PTR(-ENODEV);
		rc = epm_validate_data_sdam_sequence_matching(epm, sdam);
		if (!rc)
			break;
		idx++;
	}

	if (idx >= (MAX_EPM_SDAM - 2)) {
		dev_err(epm->dev, "No matching data sdam\n");
		return ERR_PTR(-EBUSY);
	}
	return sdam;
}

static void epm_channel_data_update(struct epm_device *epm_dev,
		uint8_t lsb, uint8_t msb, int idx, u64 ts)
{
	epm_dev->last_data[idx] =  (msb << 8) | lsb;
	epm_dev->last_data_uw[idx] = EPM_GET_POWER_UW_FROM_ADC((msb << 8) | lsb);
	epm_dev->time_stamp = ts;
	EPM_DBG(epm_dev->priv, "epm[%s]:1s power[%d]:%duw msb:0x%x lsb:0x%x",
			epm_dev->name, idx, epm_dev->last_data_uw[idx],
			msb, lsb);
}

static int qti_epm_read_acat_data_common(struct epm_priv *epm,
			struct epm_device *epm_dev, uint16_t offset,
			size_t size, bool epm_full)
{
	uint8_t data[MAX_SDAM_DATA] = {0};
	struct epm_sdam *sdam = NULL;
	int rc = 0, data_idx = 0;
	struct epm_device *epm_dev_tmp;

	do  {
		sdam = get_next_valid_data_1s_sdam(epm, sdam);
		if (IS_ERR(sdam))
			return PTR_ERR(sdam);

		rc = epm_sdam_nvmem_read(epm, sdam, offset, size, data);
		if (rc < 0)
			return rc;

		if (!epm_dev && size > EPM_DATA_BYTE_SIZE) {
			epm->all_1s_read_ts = sched_clock();
			list_for_each_entry(
				epm_dev_tmp, &epm->epm_dev_head, epm_node) {
				if (!epm_dev_tmp->enabled)
					continue;
				if (epm_dev_tmp->data_offset >= (offset + size))
					continue;
				epm_channel_data_update(epm_dev_tmp,
					data[epm_dev_tmp->data_offset],
					data[epm_dev_tmp->data_offset + 1],
					data_idx, epm->all_1s_read_ts);
			}
		} else if (epm_dev && size == EPM_DATA_BYTE_SIZE) {
			epm_channel_data_update(epm_dev, data[0], data[1],
					0, sched_clock());
		}
		data_idx++;
	} while (epm_full && data_idx < EPM_MAX_DATA_MAX &&
		data_idx < epm->max_data);

	return rc;
}

int qti_epm_update_acat_full_data(struct epm_priv *epm)
{
	int rc = 0;

	mutex_lock(&epm->sec_read_lock);
	if (epm_is_need_hw_read(epm->all_1s_read_ts))
		rc = qti_epm_read_acat_data_common(epm, NULL,
			EPM_DATA_SDAM_BASE_OFF + DATA_SDAM_POWER_LSB_CH1,
			epm->last_ch_offset + 2, true);
	mutex_unlock(&epm->sec_read_lock);
	if (rc < 0)
		return rc;

	return rc;
}

static int qti_epm_read_acat_1s_channel(struct epm_device *epm_dev, u64 *power_uw)
{
	struct epm_priv *epm = epm_dev->priv;
	int rc = 0;

	if (epm_dev->data_offset > epm->last_ch_offset)
		return -EINVAL;

	mutex_lock(&epm->sec_read_lock);
	if (epm_is_need_hw_read(epm_dev->time_stamp))
		rc = qti_epm_read_acat_data_common(epm, epm_dev,
				EPM_DATA_SDAM_BASE_OFF + DATA_SDAM_POWER_LSB_CH1
				+ epm_dev->data_offset, EPM_DATA_BYTE_SIZE,
				false);
	mutex_unlock(&epm->sec_read_lock);
	if (rc >= 0 || rc == -EBUSY) {
		rc = 0;
		*power_uw = epm_dev->last_data_uw[0];
	}

	return rc;
}

static int qti_epm_get_power(struct epm_device *epm_dev,
			enum epm_data_type type, u64 *power_uw)
{
	int rc = 0;

	mutex_lock(&epm_dev->lock);
	switch (type) {
	case EPM_1S_DATA:
		rc = qti_epm_read_acat_1s_channel(epm_dev, power_uw);
		break;
	case EPM_10S_AVG_DATA:
		rc = qti_epm_read_acat_10s_avg_channel(epm_dev, power_uw);
		break;
	default:
		dev_err(epm_dev->priv->dev,
			"No valid epm data type, type:%d\n", type);
		rc = -EINVAL;
		break;
	}
	mutex_unlock(&epm_dev->lock);

	return rc;
}

static void epm_temp_data_update(struct epm_tz_device *epm_tz,
		uint8_t data, u64 ts)
{
	epm_tz->last_temp =  EPM_GET_TEMP_MC_FROM_ADC(data);
	epm_tz->time_stamp = ts;
	EPM_DBG(epm_tz->priv, "epm tz[%d]:temp_adc:0x%x temp:%d mC",
			epm_tz->offset + 1, data, epm_tz->last_temp);
}

static int qti_epm_read_tz_common(struct epm_priv *epm,
		struct epm_tz_device *epm_tz, uint16_t offset, size_t size)
{
	uint8_t data[EPM_TZ_CH_MAX] = {0};
	struct epm_sdam *sdam = NULL;
	int rc = 0, data_idx = 0;

	sdam = get_next_valid_data_1s_sdam(epm, sdam);
	if (IS_ERR(sdam))
		return PTR_ERR(sdam);

	rc = epm_sdam_nvmem_read(epm, sdam, offset, size, data);
	if (rc < 0)
		return rc;

	if (!epm_tz && size > EPM_TZ_BYTE_SIZE) {
		epm->all_tz_read_ts = sched_clock();
		for (data_idx = 0; data_idx < epm->dt_tz_cnt; data_idx++) {
			epm_tz = &epm->epm_tz[data_idx];
			if (epm_tz->offset >= (offset + size))
				continue;
			epm_temp_data_update(epm_tz,
				data[epm_tz->offset], epm->all_tz_read_ts);
		}
	} else if (epm_tz && size == EPM_TZ_BYTE_SIZE) {
		epm_temp_data_update(epm_tz, data[0], sched_clock());
	}

	return rc;
}

static int qti_epm_read_1s_temp(struct epm_tz_device *epm_tz, int *temp)
{
	struct epm_priv *epm = epm_tz->priv;
	int rc = 0;

	if (epm_is_need_hw_read(epm_tz->time_stamp))
		rc = qti_epm_read_tz_common(epm, epm_tz,
			EPM_DATA_SDAM_BASE_OFF + DATA_SDAM_DIE_TEMP_SID1
			+ epm_tz->offset, 1);
	if (rc >= 0 || rc == -EBUSY) {
		rc = 0;
		*temp = epm_tz->last_temp;
	}

	return rc;
}

static int qti_epm_get_temp(struct epm_tz_device *epm_tz, int *temp)
{
	int rc = 0;

	mutex_lock(&epm_tz->lock);
	rc = qti_epm_read_1s_temp(epm_tz, temp);
	mutex_unlock(&epm_tz->lock);

	return rc;
}

static int qti_epm_read_data_update(struct epm_priv *epm)
{
	switch (qti_epm_get_mode(epm)) {
	case EPM_ACAT_MODE:
		qti_epm_update_acat_10s_avg_full_data(epm);
		break;
	default:
		break;
	}

	return 0;
}

static void qti_epm_update_avg_data(struct work_struct *work)
{
	struct epm_priv *epm = container_of(work, struct epm_priv,
					avg_data_work.work);

	qti_epm_update_acat_10s_avg_full_data(epm);
}

static irqreturn_t epm_sdam_irq_handler(int irq, void *data)
{
	struct epm_priv *epm = data;

	qti_epm_read_data_update(epm);

	return IRQ_HANDLED;
}

static int get_dt_index_from_ppid(struct epm_device *epm_dev)
{
	uint16_t  ppid = 0, i = 0;
	struct epm_priv *epm = epm_dev->priv;

	if (!epm_dev->enabled || !epm->dt_reg_cnt)
		return -EINVAL;

	ppid = epm_dev->sid << 8 | epm_dev->pid;

	for (i = 0; i < epm->dt_reg_cnt; i++) {
		if (ppid == epm->reg_ppid_map[i])
			return i;
	}

	return -ENODEV;
}

static int qti_epm_config_sdam_read(struct epm_priv *epm)
{
	uint8_t *config_sdam = NULL;
	struct epm_device *epm_dev = NULL;
	int rc = 0;
	uint8_t conf_idx, data_idx;

	if (!epm->sdam[CONFIG_SDAM].nvmem) {
		dev_err(epm->dev, "Invalid sdam nvmem\n");
		return -EINVAL;
	}

	config_sdam = devm_kcalloc(epm->dev, MAX_CONFIG_SDAM_DATA,
				sizeof(*config_sdam), GFP_KERNEL);
	if (!config_sdam)
		return -ENOMEM;

	rc = epm_sdam_nvmem_read(epm, &epm->sdam[CONFIG_SDAM],
			EPM_CONFIG_SDAM_BASE_OFF,
			MAX_CONFIG_SDAM_DATA, config_sdam);
	if (rc < 0)
		return rc;

	epm->g_enabled = config_sdam[CONFIG_SDAM_EPM_MODE] & BIT(7);
	if (!epm->g_enabled) {
		dev_err(epm->dev, "pmic epm is in disabled state, reg:0x%x\n",
			config_sdam[CONFIG_SDAM_EPM_MODE]);
		return -ENODEV;
	}
	epm->mode = config_sdam[CONFIG_SDAM_EPM_MODE] & BIT(0);
	epm->max_data = config_sdam[CONFIG_SDAM_MAX_DATA];
	epm->last_sdam_pid = config_sdam[CONFIG_SDAM_LAST_FULL_SDAM];
	epm->config_sdam_data = config_sdam;

	/* logic to read number of channels and die_temps */
	for (conf_idx = CONFIG_SDAM_CONFIG_1, data_idx = 0;
		conf_idx <= CONFIG_SDAM_CONFIG_48;
		conf_idx += 2, data_idx += EPM_DATA_BYTE_SIZE) {
		const char *reg_name;

		if (!(config_sdam[conf_idx] & EPM_CH_ENABLE_MASK))
			continue;

		epm->num_reg++;
		epm_dev = devm_kzalloc(epm->dev, sizeof(*epm_dev), GFP_KERNEL);
		if (!epm_dev)
			return -ENOMEM;
		epm_dev->enabled = config_sdam[conf_idx] & EPM_CH_ENABLE_MASK ?
						true : false;
		epm_dev->sid = config_sdam[conf_idx] & EPM_SID_MASK;
		epm_dev->gang_num = config_sdam[conf_idx] & EPM_GANG_NUM_MASK;
		epm_dev->pid = config_sdam[conf_idx + 1];
		epm_dev->priv = epm;
		epm_dev->data_offset = data_idx;
		mutex_init(&epm_dev->lock);
		if (data_idx > epm->last_ch_offset)
			epm->last_ch_offset = data_idx;

		rc = get_dt_index_from_ppid(epm_dev);
		if (rc < 0 || rc >= epm->dt_reg_cnt) {
			dev_err(epm->dev, "No matching channel ppid, rc:%d\n",
				rc);
			return rc;
		}
		of_property_read_string_index(epm->dev->of_node,
				"qcom,reg-ppid-names", rc, &reg_name);
		dev_dbg(epm->dev, "%s: epm channel:%s off:0x%x\n", __func__,
				reg_name, data_idx);
		strscpy(epm_dev->name, reg_name, sizeof(epm_dev->name));

		list_add(&epm_dev->epm_node, &epm->epm_dev_head);
	}

	return 0;
}

static int initialize_epm_tz(struct epm_priv *epm)
{
	struct epm_tz_device *epm_tz = NULL;
	int tz_idx = 0;

	if (!epm->dt_tz_cnt || epm->dt_tz_cnt > EPM_TZ_CH_MAX)
		return 0;

	epm_tz = devm_kzalloc(epm->dev,
			sizeof(*epm_tz) * epm->dt_tz_cnt, GFP_KERNEL);
	if (!epm_tz)
		return -ENOMEM;

	for (tz_idx = 0; tz_idx < epm->dt_tz_cnt; tz_idx++) {
		epm_tz[tz_idx].priv = epm;
		epm_tz[tz_idx].offset = tz_idx;
		mutex_init(&epm_tz[tz_idx].lock);
	}

	epm->epm_tz = epm_tz;

	return 0;
}

static int epm_get_sdam_nvmem(struct device *dev, struct epm_sdam *sdam,
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
	mutex_init(&sdam->lock);

	return rc;
}

static int epm_parse_sdam_data(struct epm_priv *epm)
{
	int rc = 0;
	char buf[20];

	rc = of_property_count_strings(epm->dev->of_node, "nvmem-names");
	if (rc < 0) {
		dev_err(epm->dev, "Could not find nvmem device\n");
		return rc;
	}
	if (rc > MAX_EPM_SDAM) {
		dev_err(epm->dev, "Invalid num of SDAMs:%d\n", rc);
		return -EINVAL;
	}

	epm->num_sdams = rc;
	epm->sdam = devm_kcalloc(epm->dev, epm->num_sdams,
				sizeof(*epm->sdam), GFP_KERNEL);
	if (!epm->sdam)
		return -ENOMEM;

	/* Check for config sdam */
	epm->sdam[0].id = CONFIG_SDAM;
	scnprintf(buf, sizeof(buf), "epm-config-sdam");
	rc = epm_get_sdam_nvmem(epm->dev, &epm->sdam[0], buf);
	if (rc < 0)
		return rc;

	/* Check 10s avg sdam */
	epm->sdam[1].id = DATA_AVG_SDAM;
	scnprintf(buf, sizeof(buf), "epm-10s-avg-sdam");
	rc = epm_get_sdam_nvmem(epm->dev, &epm->sdam[1], buf);
	if (rc < 0)
		return rc;

	return 0;
}

static int epm_parse_dt(struct epm_priv *epm)
{
	struct platform_device *pdev;
	int rc = 0;
	uint32_t val = 0;
	struct device_node *np = epm->dev->of_node;

	/* 1s data is not enabled yet, hence below DT is optional for now */
	rc = of_property_read_u32(np, "qcom,data-sdam-base-id", &val);
	if (rc < 0)
		dev_dbg(epm->dev, "Failed to get sdam base, rc = %d\n", rc);

	epm->data_1s_base_pid = val;

	rc = of_property_count_strings(np, "qcom,reg-ppid-names");
	if (rc < 1 || rc >= EPM_POWER_CH_MAX) {
		dev_err(epm->dev,
			"Invalid ppid name mapping count, rc=%d\n", rc);
		return rc;
	}
	epm->dt_reg_cnt = rc;

	rc = of_property_count_elems_of_size(np, "qcom,reg-ppid-ids",
						sizeof(u16));
	if (rc < 1 || rc >= EPM_POWER_CH_MAX || rc != epm->dt_reg_cnt) {
		dev_err(epm->dev,
			"Invalid ppid mapping count, rc = %d strings:%d\n",
				rc, epm->dt_reg_cnt);
		return rc;
	}

	rc = of_property_read_u16_array(np, "qcom,reg-ppid-ids",
			epm->reg_ppid_map, epm->dt_reg_cnt);
	if (rc < 0) {
		dev_err(epm->dev,
			"Failed to read ppid mapping array, rc = %d\n", rc);
		return rc;
	}

	rc = of_property_read_u8(np, "#qcom,epm-tz-sensor", &epm->dt_tz_cnt);
	if (rc < 0)
		dev_dbg(epm->dev,
			"Failed to read epm tz sensor count, rc = %d\n", rc);

	rc = epm_parse_sdam_data(epm);
	if (rc < 0)
		return rc;

	pdev = of_find_device_by_node(np);
	if (!pdev) {
		dev_err(epm->dev, "Invalid pdev\n");
		return -ENODEV;
	}

	rc = platform_get_irq(pdev, 0);
	if (rc <= 0)
		dev_dbg(epm->dev, "Failed to get epm irq, rc=%d\n", rc);
	epm->irq = rc;

	return 0;
}

static int qti_epm_hw_init(struct epm_priv *epm)
{
	int rc;

	if (epm->initialized)
		return 0;

	mutex_init(&epm->sec_read_lock);
	mutex_init(&epm->avg_read_lock);
	INIT_LIST_HEAD(&epm->epm_dev_head);
	INIT_DELAYED_WORK(&epm->avg_data_work, qti_epm_update_avg_data);

	rc = epm_parse_dt(epm);
	if (rc < 0) {
		dev_err(epm->dev, "Failed to parse epm rc=%d\n", rc);
		return rc;
	}

	rc = qti_epm_config_sdam_read(epm);
	if (rc < 0) {
		dev_err(epm->dev, "Failed to parse config sdam rc=%d\n", rc);
		return rc;
	}

	if (epm->irq > 0) {
		rc = devm_request_threaded_irq(epm->dev, epm->irq,
				NULL, epm_sdam_irq_handler,
				IRQF_ONESHOT, "qti_epm_irq", epm);
		if (rc < 0) {
			dev_err(epm->dev,
				"Failed to request IRQ for epm, rc=%d\n", rc);
			return rc;
		}
	}

	rc = initialize_epm_tz(epm);

	epm->initialized = true;
	/* Update first reading for all channels */
	qti_epm_read_data_update(epm);

	return 0;
}

static void qti_epm_hw_release(struct epm_priv *epm)
{
}

struct epm_ops epm_hw_ops = {
	.init = qti_epm_hw_init,
	.get_mode = qti_epm_get_mode,
	.get_power = qti_epm_get_power,  // only for ACAT mode
	.get_temp = qti_epm_get_temp,
	.release = qti_epm_hw_release,
};
