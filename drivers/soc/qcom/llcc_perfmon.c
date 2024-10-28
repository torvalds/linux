// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/hrtimer.h>
#include <linux/regmap.h>
#include <linux/soc/qcom/llcc-qcom.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/of.h>
#include "llcc_events.h"
#include "llcc_perfmon.h"

#define LLCC_PERFMON_NAME		"qcom_llcc_perfmon"
#define MAX_CNTR			16
#define MAX_NUMBER_OF_PORTS		8
#define MAX_FILTERS			16
#define MAX_FILTERS_TYPE		2
#define NUM_CHANNELS			16
#define DELIM_CHAR			" "
#define UNKNOWN_EVENT			255

#define MAX_CLOCK_CNTR			1
#define FEAC_RD_BYTES_FIL0		14
#define FEAC_WR_BEATS_FIL0		17
#define FEAC_RD_BYTES_FIL1		111
#define FEAC_WR_BEATS_FIL1		114
#define BEAC_MC_RD_BEAT_FIL0		24
#define BEAC_MC_WR_BEAT_FIL0		25
#define BEAC_MC_RD_BEAT_FIL1		38
#define BEAC_MC_WR_BEAT_FIL1		39

#define MCPROF_FEAC_FLTR_0		0
#define MCPROF_FEAC_FLTR_1		1
#define MCPROF_BEAC_FLTR_0		2
#define MCPROF_BEAC_FLTR_1		3

/**
 * struct llcc_perfmon_counter_map	- llcc perfmon counter map info
 * @port_sel:		Port selected for configured counter
 * @event_sel:		Event selected for configured counter
 * @filter_en:		Filter activation flag for configured counter
 * @filter_sel:		Filter applied for configured counter
 * @counter_dump:	Cumulative counter dump
 */
struct llcc_perfmon_counter_map {
	unsigned int port_sel;
	unsigned int event_sel;
	bool filter_en;
	u8 filter_sel;
	unsigned long long counter_dump[NUM_CHANNELS];
};

struct llcc_perfmon_private;
/**
 * struct event_port_ops		- event port operation
 * @event_config:		Counter config support for port &  event
 * @event_enable:		Counter enable support for port
 * @event_filter_config:	Port filter config support
 */
struct event_port_ops {
	bool (*event_config)(struct llcc_perfmon_private *priv, unsigned int type,
			unsigned int *num, bool enable);
	void (*event_enable)(struct llcc_perfmon_private *priv, bool enable);
	bool (*event_filter_config)(struct llcc_perfmon_private *priv, enum filter_type filter,
			unsigned long long match, unsigned long long mask, bool enable,
			u8 filter_sel);
};

enum fltr_config {
	no_fltr,
	fltr_0_only,
	multiple_filtr,
};

/**
 * struct llcc_perfmon_private	- llcc perfmon private
 * @llcc_map:		llcc register address space map
 * @llcc_bcast_map:	llcc broadcast register address space map
 * @bank_off:		Offset of llcc banks
 * @num_banks:		Number of banks supported
 * @port_ops:		struct event_port_ops
 * @configured:		Mapping of configured event counters
 * @configured_cntrs:	Count of configured counters.
 * @removed_cntrs:	Count of removed counter configurations.
 * @enables_port:	Port enabled for perfmon configuration
 * @filtered_ports:	Port filter enabled
 * @port_filter_sel:	Port filter enabled for Filter0 and Filter1
 * @filters_applied:	List of all filters applied on ports
 * @fltr_logic:		Filter selection logic to check if Filter 0 applied
 * @port_configd:	Number of perfmon port configuration supported
 * @mutex:		mutex to protect this structure
 * @hrtimer:		hrtimer instance for timer functionality
 * @expires:		timer expire time in nano seconds
 * @num_mc:		number of MCS
 * @version:		Version information of llcc block
 * @clock:		clock node to enable qdss
 * @clock_enabled:	flag to control profiling enable and disable
 * @drv_ver:		driver version of llcc-qcom
 * @mc_proftag:		Prof tag to MC
 * @snapshot_feature:	flag to indicate occupancy snapshot feature
 */
struct llcc_perfmon_private {
	struct regmap *llcc_map;
	struct regmap *llcc_bcast_map;
	unsigned int bank_off[NUM_CHANNELS];
	unsigned int num_banks;
	struct event_port_ops *port_ops[MAX_NUMBER_OF_PORTS];
	struct llcc_perfmon_counter_map configured[MAX_CNTR];
	unsigned int configured_cntrs;
	unsigned int removed_cntrs;
	unsigned int enables_port;
	unsigned int filtered_ports;
	unsigned int port_filter_sel[MAX_FILTERS_TYPE];
	u8 filters_applied[MAX_NUMBER_OF_PORTS][MAX_FILTERS][MAX_FILTERS_TYPE];
	enum fltr_config fltr_logic;
	unsigned int port_configd;
	struct mutex mutex;
	struct hrtimer hrtimer;
	ktime_t expires;
	unsigned int num_mc;
	unsigned int version;
	struct clk *clock;
	bool clock_enabled;
	int drv_ver;
	unsigned long mc_proftag;
	bool snapshot_feature;
};

static inline void llcc_bcast_write(struct llcc_perfmon_private *llcc_priv,
			unsigned int offset, uint32_t val)
{
	regmap_write(llcc_priv->llcc_bcast_map, offset, val);
}

static inline void llcc_bcast_read(struct llcc_perfmon_private *llcc_priv,
		unsigned int offset, uint32_t *val)
{
	regmap_read(llcc_priv->llcc_bcast_map, offset, val);
}

static void llcc_bcast_modify(struct llcc_perfmon_private *llcc_priv,
		unsigned int offset, uint32_t val, uint32_t mask)
{
	uint32_t readval;

	llcc_bcast_read(llcc_priv, offset, &readval);
	readval &= ~mask;
	readval |= val & mask;
	llcc_bcast_write(llcc_priv, offset, readval);
}

static void perfmon_counter_dump(struct llcc_perfmon_private *llcc_priv)
{
	struct llcc_perfmon_counter_map *counter_map;
	uint32_t val;
	unsigned int i, j, offset;

	if (!llcc_priv->configured_cntrs)
		return;

	offset = PERFMON_DUMP(llcc_priv->drv_ver);
	llcc_bcast_write(llcc_priv, offset, MONITOR_DUMP);
	for (i = 0; i < llcc_priv->configured_cntrs; i++) {
		counter_map = &llcc_priv->configured[i];
		offset = LLCC_COUNTER_n_VALUE(llcc_priv->drv_ver, i);
		for (j = 0; j < llcc_priv->num_banks; j++) {
			regmap_read(llcc_priv->llcc_map, llcc_priv->bank_off[j] + offset, &val);
			counter_map->counter_dump[j] += val;
		}
	}
}

static ssize_t perfmon_counter_dump_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct llcc_perfmon_private *llcc_priv = dev_get_drvdata(dev);
	struct llcc_perfmon_counter_map *counter_map;
	unsigned int i, j, val, offset;
	unsigned long long total;
	ssize_t cnt = 0;

	if (llcc_priv->configured_cntrs == 0) {
		pr_err("counters not configured\n");
		return cnt;
	}

	perfmon_counter_dump(llcc_priv);
	for (i = 0; i < llcc_priv->configured_cntrs; i++) {
		total = 0;
		counter_map = &llcc_priv->configured[i];

		if (counter_map->port_sel == EVENT_PORT_BEAC && llcc_priv->num_mc > 1) {
			/* DBX uses 2 counters for BEAC 0 & 1 */
			i++;
			for (j = 0; j < llcc_priv->num_banks; j++) {
				total += counter_map->counter_dump[j];
				counter_map->counter_dump[j] = 0;
				total += counter_map[1].counter_dump[j];
				counter_map[1].counter_dump[j] = 0;
			}
		} else {
			for (j = 0; j < llcc_priv->num_banks; j++) {
				total += counter_map->counter_dump[j];
				counter_map->counter_dump[j] = 0;
			}
		}

		/* Checking if the last counter is configured as clock cycle counter */
		if (i == llcc_priv->configured_cntrs - 1) {
			offset = PERFMON_COUNTER_n_CONFIG(llcc_priv->drv_ver, i);
			llcc_bcast_read(llcc_priv, offset, &val);
			if (val & COUNT_CLOCK_EVENT) {
				cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt, "CYCLE COUNT, ,");
				cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt, "0x%016llx\n", total);
				break;
			}
		}

		cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt, "Port %02d,Event %03d,",
				counter_map->port_sel, counter_map->event_sel);
		cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt, "0x%016llx\n", total);
	}

	if (llcc_priv->expires)
		hrtimer_forward_now(&llcc_priv->hrtimer, llcc_priv->expires);

	return cnt;
}

static void remove_filters(struct llcc_perfmon_private *llcc_priv)
{
	struct event_port_ops *port_ops;
	u32 i, j, port_filter_sel;
	u8 filter0_applied, filter1_applied;

	/* Capturing filtered ports info for filter0 and filter1 */
	port_filter_sel = llcc_priv->port_filter_sel[FILTER_0] |
		llcc_priv->port_filter_sel[FILTER_1];
	if (!port_filter_sel) {
		pr_err("No filter configuration found!\n");
		return;
	}

	for (i = 0; i < MAX_NUMBER_OF_PORTS; i++) {
		/* If filter is not present on port then check next port bit */
		if (!(port_filter_sel & (1 << i)))
			continue;

		for (j = 0; j < MAX_FILTERS; j++) {
			filter0_applied = llcc_priv->filters_applied[i][j][FILTER_0];
			filter1_applied = llcc_priv->filters_applied[i][j][FILTER_1];
			port_ops = llcc_priv->port_ops[i];

			if ((!filter0_applied && !filter1_applied) ||
					!port_ops->event_filter_config)
				continue;

			/* Removing FILTER0 configuration if present */
			if (filter0_applied) {
				port_ops->event_filter_config(llcc_priv, filter0_applied, 0, 0,
						false, FILTER_0);
				llcc_priv->filters_applied[i][j][FILTER_0] = UNKNOWN_FILTER;
			}

			/* Removing FILTER1 configuration if present */
			if (filter1_applied) {
				port_ops->event_filter_config(llcc_priv, filter1_applied, 0, 0,
						false, FILTER_1);
				llcc_priv->filters_applied[i][j][FILTER_1] = UNKNOWN_FILTER;
			}
		}
	}

	/* Clearing internal info for filters and counters */
	llcc_priv->port_filter_sel[FILTER_0] = 0;
	llcc_priv->port_filter_sel[FILTER_1] = 0;
	llcc_priv->fltr_logic = no_fltr;
	for (i = 0; i < MAX_CNTR; i++)
		llcc_priv->configured[i].filter_en = false;

	pr_info("All Filters removed\n");
}

static void remove_counters(struct llcc_perfmon_private *llcc_priv)
{
	u32 i, offset, val;
	struct event_port_ops *port_ops;
	struct llcc_perfmon_counter_map *counter_map;

	if (!llcc_priv->configured_cntrs) {
		pr_err("Counters are not configured\n");
		return;
	}

	/* Remove the counters configured for ports */
	for (i = 0; i < llcc_priv->configured_cntrs; i++) {
		/*Checking if the last counter is configured as cyclic counter. Removing if found*/
		if (i == llcc_priv->configured_cntrs - 1) {
			offset = PERFMON_COUNTER_n_CONFIG(llcc_priv->drv_ver, i);
			llcc_bcast_read(llcc_priv, offset, &val);
			if (val & COUNT_CLOCK_EVENT) {
				llcc_bcast_write(llcc_priv, offset, 0);
				break;
			}
		}

		counter_map = &llcc_priv->configured[i];
		/* In case the port configuration is already removed, skip */
		if (counter_map->port_sel == MAX_NUMBER_OF_PORTS)
			continue;

		port_ops = llcc_priv->port_ops[counter_map->port_sel];
		port_ops->event_config(llcc_priv, 0, &i, false);
		pr_info("removed counter %2d for event %3ld from port %2ld\n", i,
				counter_map->event_sel, counter_map->port_sel);
		if ((llcc_priv->enables_port & (1 << counter_map->port_sel)) &&
				port_ops->event_enable)
			port_ops->event_enable(llcc_priv, false);

		llcc_priv->enables_port &= ~(1 << counter_map->port_sel);
		/*Setting unknown values for port and event for error handling*/
		counter_map->port_sel = MAX_NUMBER_OF_PORTS;
		counter_map->event_sel = UNKNOWN_EVENT;
	}

	llcc_priv->configured_cntrs = 0;
	pr_info("Counters removed\n");

	/* Remove the filters if applied */
	if (llcc_priv->fltr_logic != no_fltr) {
		pr_info("Removing filters\n");
		remove_filters(llcc_priv);
	}
}

static bool find_filter_index(const char *token, u8 *filter_idx)
{
	if (sysfs_streq(token, "FILTER0")) {
		*filter_idx = FILTER_0;
		return true;
	} else if (sysfs_streq(token, "FILTER1")) {
		*filter_idx = FILTER_1;
		return true;
	} else {
		return false;
	}
}

static ssize_t perfmon_configure_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct llcc_perfmon_private *llcc_priv = dev_get_drvdata(dev);
	struct event_port_ops *port_ops;
	struct llcc_perfmon_counter_map *counter_map;
	unsigned int j = 0, k;
	unsigned long port_sel, event_sel;
	uint32_t val, offset;
	char *token, *delim = DELIM_CHAR;
	u8 filter_idx = FILTER_0, beac_res_cntrs = 0;
	bool multi_fltr_flag = false;

	mutex_lock(&llcc_priv->mutex);
	if (llcc_priv->configured_cntrs == MAX_CNTR) {
		offset = PERFMON_COUNTER_n_CONFIG(llcc_priv->drv_ver,
				llcc_priv->configured_cntrs - 1);
		llcc_bcast_read(llcc_priv, offset, &val);
		/* Check if last counter is clock counter. If yes, let overwrite last counter */
		if (!(val & COUNT_CLOCK_EVENT)) {
			pr_err("Counters configured already, remove & try again\n");
			mutex_unlock(&llcc_priv->mutex);
			return -EINVAL;
		}
	}

	/* Cheking whether an existing counter configuration is present, if present initializing
	 * the count to number of configured counters - 1 to overwrite the last configured cyclic
	 * counter. Cyclic count will be configured to the last counter available.
	 */
	if (llcc_priv->configured_cntrs)
		j = llcc_priv->configured_cntrs - 1;

	token = strsep((char **)&buf, delim);
	/* Getting filter information if provided */
	if (token && strlen(token) == strlen("FILTERX")) {
		if (llcc_priv->fltr_logic != multiple_filtr) {
			pr_err("Error Multifilter configuration not present\n");
			goto out_configure;
		}

		if (!find_filter_index(token, &filter_idx)) {
			pr_err("Invalid Filter Index, supported are FILTER0/1\n");
			goto out_configure;
		}

		multi_fltr_flag = true;
		token = strsep((char **)&buf, delim);
	}

	while (token != NULL) {
		if (kstrtoul(token, 0, &port_sel))
			break;

		if (port_sel >= llcc_priv->port_configd)
			break;

		/* Checking whether given filter is enabled for the port */
		if (multi_fltr_flag &&
				!(llcc_priv->port_filter_sel[filter_idx] & (1 << port_sel))) {
			pr_err("Filter not configured for given port, removing configurations\n");
			remove_counters(llcc_priv);
			goto out_configure;
		}

		token = strsep((char **)&buf, delim);
		if (token == NULL)
			break;

		if (kstrtoul(token, 0, &event_sel))
			break;

		token = strsep((char **)&buf, delim);
		if (event_sel >= EVENT_NUM_MAX) {
			pr_err("unsupported event num %ld\n", event_sel);
			continue;
		}

		/* If BEAC is getting configured, then counters equal to number of Memory Controller
		 * are needed for BEAC configuration. Hence, setting the same.
		 */
		beac_res_cntrs = 0;
		if (port_sel == EVENT_PORT_BEAC)
			beac_res_cntrs = llcc_priv->num_mc;

		if (j == (MAX_CNTR - beac_res_cntrs)) {
			pr_err("All counters already used\n");
			break;
		}

		counter_map = &llcc_priv->configured[j];
		counter_map->port_sel = port_sel;
		counter_map->event_sel = event_sel;
		if (multi_fltr_flag) {
			counter_map->filter_en = true;
			counter_map->filter_sel = filter_idx;
		}

		for (k = 0; k < llcc_priv->num_banks; k++)
			counter_map->counter_dump[k] = 0;

		port_ops = llcc_priv->port_ops[port_sel];
		/* if any perfmon configuration fails, remove the existing configurations */
		if (!port_ops->event_config(llcc_priv, event_sel, &j, true)) {
			llcc_priv->configured_cntrs = ++j;
			remove_counters(llcc_priv);
			goto out_configure;
		}

		pr_info("counter %2d configured for event %3ld from port %ld\n", j++, event_sel,
				port_sel);
		if (((llcc_priv->enables_port & (1 << port_sel)) == 0) && port_ops->event_enable)
			port_ops->event_enable(llcc_priv, true);

		llcc_priv->enables_port |= (1 << port_sel);
	}

	if (!j) {
		pr_err("Port/Event number not provided, counters not configured\n");
		goto out_configure;
	}

	/* Configure cycle counter as last one if any counter available */
	if (j < MAX_CNTR) {
		val = COUNT_CLOCK_EVENT | CLEAR_ON_ENABLE | CLEAR_ON_DUMP;
		offset = PERFMON_COUNTER_n_CONFIG(llcc_priv->drv_ver, j++);
		llcc_bcast_write(llcc_priv, offset, val);
	}

	llcc_priv->configured_cntrs = j;

out_configure:
	mutex_unlock(&llcc_priv->mutex);
	return count;
}

static ssize_t perfmon_remove_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct llcc_perfmon_private *llcc_priv = dev_get_drvdata(dev);
	struct event_port_ops *port_ops;
	struct llcc_perfmon_counter_map *counter_map;
	unsigned int j = 0, val;
	unsigned long port_sel, event_sel;
	char *token, *delim = DELIM_CHAR;
	uint32_t offset;
	u8 filter_idx = FILTER_0, beac_res_cntrs = 0;
	bool multi_fltr_flag = false, filter_en = false;

	mutex_lock(&llcc_priv->mutex);
	if (!llcc_priv->configured_cntrs) {
		pr_err("Counters not configured\n");
		mutex_unlock(&llcc_priv->mutex);
		return -EINVAL;
	}

	/* Checking if counters were removed earlier, setting the count value to next counter */
	if (llcc_priv->removed_cntrs && llcc_priv->removed_cntrs < llcc_priv->configured_cntrs)
		j  = llcc_priv->removed_cntrs;

	token = strsep((char **)&buf, delim);

	/* Case for removing all the counters at once */
	if (token && sysfs_streq(token, "REMOVE")) {
		pr_info("Removing all configured counters and filters\n");
		remove_counters(llcc_priv);
		goto out_remove_store;
	}

	/* Getting filter information if provided */
	if (token && strlen(token) == strlen("FILTERX")) {
		if (llcc_priv->fltr_logic != multiple_filtr) {
			pr_err("Error! Multifilter configuration not present\n");
			goto out_remove_store_err;
		}

		if (!find_filter_index(token, &filter_idx)) {
			pr_err("Error! Invalid Filter Index, supported are FILTER0/1\n");
			goto out_remove_store_err;
		}

		multi_fltr_flag = true;
		token = strsep((char **)&buf, delim);
	}

	while (token != NULL) {
		if (kstrtoul(token, 0, &port_sel))
			break;

		if (port_sel >= llcc_priv->port_configd)
			break;

		/* Counter mapping for last removed counter */
		counter_map = &llcc_priv->configured[j];

		/* Getting filter activation status for given port and filter type 0/1 */
		if (counter_map->filter_en)
			filter_en = llcc_priv->port_filter_sel[counter_map->filter_sel] &
				(1 << port_sel);

		/* If multifilters format is used to remove perfmon configuration checking if given
		 * port is same as configured port on current counter checking if filter is enabled
		 * on current counter for given port with same filter type FILTER0/FILTER1
		 */
		if (counter_map->port_sel == port_sel) {
			if (multi_fltr_flag && !filter_en) {
				pr_err("Error! filter not present counter:%u for port:%u\n", j,
						port_sel);
				goto out_remove_store_err;
			} else if (!multi_fltr_flag && filter_en) {
				pr_err("Error! Filter is present on counter:%u for port:%u\n", j,
						port_sel);
				goto out_remove_store_err;
			}
		} else {
			pr_err("Error! Given port %u is not configured on counter %u\n", port_sel,
					j);
			goto out_remove_store_err;
		}

		token = strsep((char **)&buf, delim);
		if (token == NULL)
			break;

		if (kstrtoul(token, 0, &event_sel))
			break;

		token = strsep((char **)&buf, delim);
		if (event_sel >= EVENT_NUM_MAX) {
			pr_err("unsupported event num %ld\n", event_sel);
			continue;
		}

		beac_res_cntrs = 0;
		if (port_sel == EVENT_PORT_BEAC)
			beac_res_cntrs = llcc_priv->num_mc;

		if (j == (llcc_priv->configured_cntrs - beac_res_cntrs))
			break;

		/* put dummy values */
		counter_map = &llcc_priv->configured[j];
		counter_map->port_sel = MAX_NUMBER_OF_PORTS;
		counter_map->event_sel = UNKNOWN_EVENT;
		port_ops = llcc_priv->port_ops[port_sel];
		port_ops->event_config(llcc_priv, event_sel, &j, false);
		llcc_priv->removed_cntrs++;
		pr_info("removed counter %2d for event %3ld from port %2ld\n", j++, event_sel,
				port_sel);
		if ((llcc_priv->enables_port & (1 << port_sel)) && port_ops->event_enable)
			port_ops->event_enable(llcc_priv, false);

		llcc_priv->enables_port &= ~(1 << port_sel);
	}

	/* If count reached to last counters then checking whether last counter is used as cycle
	 * counter, remove the same.
	 */
	if (j == (llcc_priv->configured_cntrs - 1)) {
		offset = PERFMON_COUNTER_n_CONFIG(llcc_priv->drv_ver, j);
		llcc_bcast_read(llcc_priv, offset, &val);
		if (val & COUNT_CLOCK_EVENT) {
			llcc_bcast_write(llcc_priv, offset, 0);
			j++;
		}
	}

	if (j == llcc_priv->configured_cntrs) {
		pr_info("All counters removed\n");
		llcc_priv->configured_cntrs = 0;
		llcc_priv->removed_cntrs = 0;
	}

out_remove_store:
	mutex_unlock(&llcc_priv->mutex);
	return count;

out_remove_store_err:
	remove_counters(llcc_priv);
	mutex_unlock(&llcc_priv->mutex);
	return -EINVAL;
}

static enum filter_type find_filter_type(char *filter)
{
	enum filter_type ret = UNKNOWN_FILTER;

	if (!strcmp(filter, "SCID"))
		ret = SCID;
	else if (!strcmp(filter, "MID"))
		ret = MID;
	else if (!strcmp(filter, "PROFILING_TAG"))
		ret = PROFILING_TAG;
	else if (!strcmp(filter, "WAY_ID"))
		ret = WAY_ID;
	else if (!strcmp(filter, "OPCODE"))
		ret = OPCODE;
	else if (!strcmp(filter, "CACHEALLOC"))
		ret = CACHEALLOC;
	else if (!strcmp(filter, "MEMTAGOPS"))
		ret = MEMTAGOPS;
	else if (!strcmp(filter, "MULTISCID"))
		ret = MULTISCID;
	else if (!strcmp(filter, "DIRTYINFO"))
		ret = DIRTYINFO;
	else if (!strcmp(filter, "ADDR_MASK"))
		ret = ADDR_MASK;

	return ret;
}

static ssize_t perfmon_filter_config_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct llcc_perfmon_private *llcc_priv = dev_get_drvdata(dev);
	unsigned long long mask, match;
	unsigned long port, port_filter_en = 0;
	struct event_port_ops *port_ops;
	char *token, *delim = DELIM_CHAR;
	enum filter_type fil_applied = UNKNOWN_FILTER;
	u8 filter_idx = FILTER_0, i;
	bool filter_status;

	if (llcc_priv->configured_cntrs) {
		pr_err("remove configured events and try\n");
		return count;
	}

	mutex_lock(&llcc_priv->mutex);
	token = strsep((char **)&buf, delim);
	if (token != NULL)
		fil_applied = find_filter_type(token);

	if (fil_applied == UNKNOWN_FILTER) {
		pr_err("filter configuration failed, Unsupported filter\n");
		goto filter_config_free;
	}

	token = strsep((char **)&buf, delim);
	if (token == NULL) {
		pr_err("filter configuration failed, Wrong input\n");
		goto filter_config_free;
	}

	if (kstrtoull(token, 0, &match)) {
		pr_err("filter configuration failed, Wrong format\n");
		goto filter_config_free;
	}

	if ((fil_applied == SCID) && (match >= SCID_MAX)) {
		pr_err("filter configuration failed, SCID above MAX value\n");
		goto filter_config_free;
	}

	token = strsep((char **)&buf, delim);
	if (token == NULL) {
		pr_err("filter configuration failed, Wrong input\n");
		goto filter_config_free;
	}

	if (kstrtoull(token, 0, &mask)) {
		pr_err("filter configuration failed, Wrong format\n");
		goto filter_config_free;
	}

	while (token != NULL) {
		/* With Selective filter enabled, all filter config expected to be in
		 * selective mode
		 */
		token = strsep((char **)&buf, delim);
		if (token == NULL) {
			if (llcc_priv->fltr_logic == multiple_filtr) {
				pr_err("Multiple Filter aleady present, try again\n");
				goto filter_config_free;
			} else {
				break;
			}
		}

		if (find_filter_index(token, &filter_idx)) {
			if (llcc_priv->fltr_logic ==  fltr_0_only) {
				pr_err("Filter 0 config already present, try again\n");
				goto filter_config_free;
			}

			llcc_priv->fltr_logic = multiple_filtr;
			pr_info("Selective filter configuration selected\n");
			break;
		}

		if (kstrtoul(token, 0, &port)) {
			pr_err("filter configuration failed, Wrong format. Try again!\n");
			goto filter_config_free;
		}

		if (port >= MAX_NUMBER_OF_PORTS) {
			pr_err("filter configuration failed, port number above MAX value\n");
			goto filter_config_free;
		}

		port_filter_en |= 1 << port;
	}

	if (!port_filter_en) {
		pr_err("No port number input for filter config, try again\n");
		goto filter_config_free;
	}

	/* Defaults to Filter 0 config if multiple filter not selected */
	if (llcc_priv->fltr_logic != multiple_filtr) {
		llcc_priv->fltr_logic = fltr_0_only;
		pr_info("Using Filter 0 settings\n");
	}

	for (i = 0; i < MAX_NUMBER_OF_PORTS; i++) {
		port_ops = llcc_priv->port_ops[i];
		if (!port_ops->event_filter_config)
			continue;

		if (port_filter_en & (1 << i)) {
			/* Updating the applied filter information for the port */
			llcc_priv->filters_applied[i][fil_applied][filter_idx] = fil_applied;
			filter_status = port_ops->event_filter_config(llcc_priv, fil_applied,
					match, mask, true, filter_idx);
			if (!filter_status)
				goto filter_config_free;
		}
	}

	llcc_priv->port_filter_sel[filter_idx] |= port_filter_en;
	mutex_unlock(&llcc_priv->mutex);
	return count;

filter_config_free:
	remove_filters(llcc_priv);
	mutex_unlock(&llcc_priv->mutex);
	return -EINVAL;
}

static void reset_flags(struct llcc_perfmon_private *llcc_priv)
{
	/* Check for removing the flags for Filter 0 alone and Multiple filters.
	 * Checking if port configuration for FILTER0 is clear
	 */
	if (!llcc_priv->port_filter_sel[FILTER_0]) {
		/* Remove Multiple filter if set and port configuration for FILTER1 is clear */
		if (!llcc_priv->port_filter_sel[FILTER_1] &&
				llcc_priv->fltr_logic == multiple_filtr)
			llcc_priv->fltr_logic = no_fltr;
		/* Remove Filter 0 alone if set and selective filter unset */
		else if (llcc_priv->fltr_logic == fltr_0_only)
			llcc_priv->fltr_logic = no_fltr;
	}
}

static ssize_t perfmon_filter_remove_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct llcc_perfmon_private *llcc_priv = dev_get_drvdata(dev);
	struct event_port_ops *port_ops;
	unsigned long long mask, match;
	unsigned long port, port_filter_en = 0;
	char *token, *delim = DELIM_CHAR;
	enum filter_type fil_applied = UNKNOWN_FILTER;
	u8 filter_idx = FILTER_0, i, j;

	if (llcc_priv->fltr_logic == no_fltr) {
		pr_err("Filters are not applied\n");
		return count;
	}

	if (llcc_priv->configured_cntrs) {
		pr_err("remove configured events and try\n");
		return count;
	}

	mutex_lock(&llcc_priv->mutex);
	token = strsep((char **)&buf, delim);
	if (token && sysfs_streq(token, "REMOVE")) {
		remove_filters(llcc_priv);
		goto filter_remove_free;
	}

	if (token)
		fil_applied = find_filter_type(token);

	if (fil_applied == UNKNOWN_FILTER) {
		pr_err("filter configuration failed, Unsupported filter\n");
		goto filter_remove_free;
	}

	token = strsep((char **)&buf, delim);
	if (token == NULL) {
		pr_err("filter configuration failed, Wrong input\n");
		goto filter_remove_free;
	}

	if (kstrtoull(token, 0, &match)) {
		pr_err("filter configuration failed, Wrong format\n");
		goto filter_remove_free;
	}

	if (fil_applied == SCID && match >= SCID_MAX) {
		pr_err("filter configuration failed, SCID above MAX value\n");
		goto filter_remove_free;
	}

	token = strsep((char **)&buf, delim);
	if (token == NULL) {
		pr_err("filter configuration failed, Wrong input\n");
		goto filter_remove_free;
	}

	if (kstrtoull(token, 0, &mask)) {
		pr_err("filter configuration failed, Wrong format\n");
		goto filter_remove_free;
	}

	while (token != NULL) {
		token = strsep((char **)&buf, delim);
		if (token == NULL) {
		/* Filter 0 config rejected as Multiple filter config already present */
			if (llcc_priv->fltr_logic ==  multiple_filtr) {
				pr_err("Mismatch! Selective configuration present\n");
				goto filter_remove_free;
			} else {
				break;
			}
		}

		if (find_filter_index(token, &filter_idx)) {
			/* Multiple filter logic rejected as Filter 0 alone already present */
			if (llcc_priv->fltr_logic ==  fltr_0_only) {
				pr_err("Mismatch! Filter 0 alone configuration present\n");
				goto filter_remove_free;
			}

			break;
		}

		if (kstrtoul(token, 0, &port))
			break;

		if (port >= MAX_NUMBER_OF_PORTS) {
			pr_err("filter configuration failed, port number above MAX value\n");
			goto filter_remove_free;
		}

		/* Updating bit field for filtered port */
		port_filter_en |= 1 << port;
	}

	for (i = 0; i < MAX_NUMBER_OF_PORTS; i++) {
		if (!(port_filter_en & (1 << i)))
			continue;

		port_ops = llcc_priv->port_ops[i];
		if (!port_ops->event_filter_config)
			continue;

		port_ops->event_filter_config(llcc_priv, fil_applied, 0, 0, false, filter_idx);
		llcc_priv->filters_applied[i][fil_applied][filter_idx] = UNKNOWN_FILTER;

		/* Checking if any filter is present on given port */
		for (j = 0; j < MAX_FILTERS; j++)
			if (llcc_priv->filters_applied[i][j][filter_idx])
				break;
		/* Clearing the port filter en bit if all filter fields are UNKNOWN for port
		 * same will be used to clear the global filter flag in llcc_priv
		 */
		if (j == MAX_FILTERS) {
			port_filter_en &= ~(1 << i);
			llcc_priv->port_filter_sel[filter_idx] &= ~(1 << i);
		}
	}

	reset_flags(llcc_priv);

filter_remove_free:
	mutex_unlock(&llcc_priv->mutex);
	return count;
}

static ssize_t perfmon_start_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct llcc_perfmon_private *llcc_priv = dev_get_drvdata(dev);
	uint32_t val = 0, mask_val, offset, cntr_num = DUMP_NUM_COUNTERS_MASK;
	unsigned long start;
	int ret = 0;

	if (kstrtoul(buf, 0, &start))
		return -EINVAL;

	if (!llcc_priv->configured_cntrs) {
		pr_err("Perfmon not configured\n");
		return -EINVAL;
	}

	mutex_lock(&llcc_priv->mutex);
	if (start) {
		if (llcc_priv->clock) {
			ret = clk_prepare_enable(llcc_priv->clock);
			if (ret) {
				mutex_unlock(&llcc_priv->mutex);
				pr_err("clock not enabled\n");
				return -EINVAL;
			}
			llcc_priv->clock_enabled = true;
		}
		val = MANUAL_MODE | MONITOR_EN;
		val &= ~DUMP_SEL;
		if (llcc_priv->expires) {
			if (hrtimer_is_queued(&llcc_priv->hrtimer))
				hrtimer_forward_now(&llcc_priv->hrtimer, llcc_priv->expires);
			else
				hrtimer_start(&llcc_priv->hrtimer, llcc_priv->expires,
						HRTIMER_MODE_REL_PINNED);
		}

		cntr_num = (((llcc_priv->configured_cntrs - 1) & DUMP_NUM_COUNTERS_MASK) <<
				DUMP_NUM_COUNTERS_SHIFT);
	} else {
		if (llcc_priv->expires)
			hrtimer_cancel(&llcc_priv->hrtimer);
	}

	mask_val = PERFMON_MODE_MONITOR_MODE_MASK | PERFMON_MODE_MONITOR_EN_MASK |
		PERFMON_MODE_DUMP_SEL_MASK;
	offset = PERFMON_MODE(llcc_priv->drv_ver);

	/* Check to ensure that register write for stopping perfmon should only happen
	 * if clock is already prepared.
	 */
	if (llcc_priv->clock) {
		if (llcc_priv->clock_enabled) {
			llcc_bcast_modify(llcc_priv, offset, val, mask_val);
			if (!start) {
				clk_disable_unprepare(llcc_priv->clock);
				llcc_priv->clock_enabled = false;
			}
		}
	} else {
		/* For RUMI environment where clock node is not available */
		llcc_bcast_modify(llcc_priv, offset, val, mask_val);
	}

	/* Updating total counters to dump info, based on configured counters */
	offset = PERFMON_NUM_CNTRS_DUMP_CFG(llcc_priv->drv_ver);
	llcc_bcast_write(llcc_priv, offset, cntr_num);

	mutex_unlock(&llcc_priv->mutex);
	return count;
}

static ssize_t perfmon_ns_periodic_dump_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct llcc_perfmon_private *llcc_priv = dev_get_drvdata(dev);

	if (kstrtos64(buf, 0, &llcc_priv->expires))
		return -EINVAL;

	mutex_lock(&llcc_priv->mutex);
	if (!llcc_priv->expires) {
		hrtimer_cancel(&llcc_priv->hrtimer);
		mutex_unlock(&llcc_priv->mutex);
		return count;
	}

	if (hrtimer_is_queued(&llcc_priv->hrtimer))
		hrtimer_forward_now(&llcc_priv->hrtimer, llcc_priv->expires);
	else
		hrtimer_start(&llcc_priv->hrtimer, llcc_priv->expires, HRTIMER_MODE_REL_PINNED);

	mutex_unlock(&llcc_priv->mutex);
	return count;
}

static ssize_t perfmon_scid_status_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct llcc_perfmon_private *llcc_priv = dev_get_drvdata(dev);
	uint32_t val;
	unsigned int i, j, offset;
	ssize_t cnt = 0;
	unsigned long total;

	/*
	 * On platforms where occupancy snapshot feature is present, snapshot of scid occupancy is
	 * captured in status registers only by setting and deasserting trigger bit in
	 * TRP_CAP_COUNTERS_DUMP_CFG register.
	 */
	if (llcc_priv->snapshot_feature) {
		regmap_write(llcc_priv->llcc_bcast_map, TRP_CAP_COUNTERS_DUMP_CFG, 1);
		regmap_write(llcc_priv->llcc_bcast_map, TRP_CAP_COUNTERS_DUMP_CFG, 0);
	}

	for (i = 0; i < SCID_MAX; i++) {
		total = 0;
		offset = TRP_SCID_n_STATUS(i);
		for (j = 0; j < llcc_priv->num_banks; j++) {
			regmap_read(llcc_priv->llcc_map, llcc_priv->bank_off[j] + offset, &val);
			val = (val & TRP_SCID_STATUS_CURRENT_CAP_MASK) >>
				TRP_SCID_STATUS_CURRENT_CAP_SHIFT;
			total += val;
		}

		llcc_bcast_read(llcc_priv, offset, &val);
		if (val & TRP_SCID_STATUS_ACTIVE_MASK)
			cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt, "SCID %02d %10s", i, "ACTIVE");
		else
			cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt,
					"SCID %02d %10s", i, "DEACTIVE");

		cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt, ",0x%08lx\n", total);
	}

	return cnt;
}

static ssize_t perfmon_beac_mc_proftag_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct llcc_perfmon_private *llcc_priv = dev_get_drvdata(dev);

	if (kstrtoul(buf, 0, &llcc_priv->mc_proftag))
		return -EINVAL;

	mutex_lock(&llcc_priv->mutex);

	switch (llcc_priv->mc_proftag) {
	case MCPROF_FEAC_FLTR_0:
		if (llcc_priv->port_filter_sel[0] & (1 << EVENT_PORT_FEAC)) {
			llcc_priv->mc_proftag = MCPROF_FEAC_FLTR_0;
		} else {
			llcc_priv->mc_proftag = MCPROF_BEAC_FLTR_0;
			pr_err("FEAC Filter 0 not enabled, deafulting to BEAC FILTER 0\n");
		}
		break;

	case MCPROF_FEAC_FLTR_1:
		if (llcc_priv->port_filter_sel[1] & (1 << EVENT_PORT_FEAC)) {
			llcc_priv->mc_proftag = MCPROF_FEAC_FLTR_1;
		} else {
			llcc_priv->mc_proftag = MCPROF_BEAC_FLTR_0;
			pr_err("FEAC Filter 1 not enabled, deafulting to BEAC FILTER 0\n");
		}
		break;

	case MCPROF_BEAC_FLTR_0:
		if (llcc_priv->port_filter_sel[0] & (1 << EVENT_PORT_BEAC)) {
			llcc_priv->mc_proftag = MCPROF_BEAC_FLTR_0;
		} else {
			llcc_priv->mc_proftag = MCPROF_BEAC_FLTR_0;
			pr_err("BEAC Filter 0 not enabled, deafulting to BEAC FILTER 0\n");
		}
		break;

	case MCPROF_BEAC_FLTR_1:
		if (llcc_priv->port_filter_sel[1] & (1 << EVENT_PORT_BEAC)) {
			llcc_priv->mc_proftag = MCPROF_BEAC_FLTR_1;
		} else {
			llcc_priv->mc_proftag = MCPROF_BEAC_FLTR_0;
			pr_err("BEAC Filter 1 not enabled, deafulting to BEAC FILTER 0\n");
		}
		break;
	}

	mutex_unlock(&llcc_priv->mutex);
	return count;
}

static DEVICE_ATTR_RO(perfmon_counter_dump);
static DEVICE_ATTR_WO(perfmon_configure);
static DEVICE_ATTR_WO(perfmon_remove);
static DEVICE_ATTR_WO(perfmon_filter_config);
static DEVICE_ATTR_WO(perfmon_filter_remove);
static DEVICE_ATTR_WO(perfmon_start);
static DEVICE_ATTR_RO(perfmon_scid_status);
static DEVICE_ATTR_WO(perfmon_ns_periodic_dump);
static DEVICE_ATTR_WO(perfmon_beac_mc_proftag);

static struct attribute *llcc_perfmon_attrs[] = {
	&dev_attr_perfmon_counter_dump.attr,
	&dev_attr_perfmon_configure.attr,
	&dev_attr_perfmon_remove.attr,
	&dev_attr_perfmon_filter_config.attr,
	&dev_attr_perfmon_filter_remove.attr,
	&dev_attr_perfmon_start.attr,
	&dev_attr_perfmon_scid_status.attr,
	&dev_attr_perfmon_ns_periodic_dump.attr,
	&dev_attr_perfmon_beac_mc_proftag.attr,
	NULL,
};

static struct attribute_group llcc_perfmon_group = {
	.attrs	= llcc_perfmon_attrs,
};

static void perfmon_cntr_config(struct llcc_perfmon_private *llcc_priv, unsigned int port,
		unsigned int counter_num, bool enable)
{
	uint32_t val = 0, offset;

	if (counter_num >= MAX_CNTR)
		return;

	if (enable)
		val = (port & PERFMON_PORT_SELECT_MASK) |
			((counter_num << EVENT_SELECT_SHIFT) & PERFMON_EVENT_SELECT_MASK) |
			CLEAR_ON_ENABLE | CLEAR_ON_DUMP;

	offset = PERFMON_COUNTER_n_CONFIG(llcc_priv->drv_ver, counter_num);
	llcc_bcast_write(llcc_priv, offset, val);
}

static bool feac_event_config(struct llcc_perfmon_private *llcc_priv, unsigned int event_type,
		unsigned int *counter_num, bool enable)
{
	uint32_t val = 0, mask_val, offset;
	u8 filter_en, filter_sel = FILTER_0;

	filter_en = llcc_priv->port_filter_sel[filter_sel] & (1 << EVENT_PORT_FEAC);
	if (llcc_priv->fltr_logic ==  multiple_filtr) {
		filter_en = llcc_priv->configured[*counter_num].filter_en;
		filter_sel = llcc_priv->configured[*counter_num].filter_sel;
	}

	mask_val = EVENT_SEL_MASK;

	if (llcc_priv->version >= REV_2) {
		mask_val = EVENT_SEL_MASK7;

		if (llcc_priv->version == REV_5)
			mask_val = EVENT_SEL_MASK8;
	}

	if (enable) {
		val = (event_type << EVENT_SEL_SHIFT) & mask_val;
		if (filter_en) {
			/* In case Feac events Read_/Write_ beat/byte, filter selection
			 * logic should not apply as these 4 events do not use the FILTER_SEL and
			 * FILTER_EN fields from LLCC_*_PROF_EVENT_n_CFG. Instead, they use
			 * exclusive filters that need to be configured for that specific events.
			 */
			if (((event_type >= FEAC_RD_BYTES_FIL0 &&
				event_type <= FEAC_WR_BEATS_FIL0) && filter_sel == FILTER_1) ||
				((event_type >= FEAC_RD_BYTES_FIL1 &&
				  event_type <= FEAC_WR_BEATS_FIL1) && filter_sel == FILTER_0)) {
				pr_err("Invalid configuration for FEAC, removing\n");
				return false;
			} else if (!(event_type >= FEAC_RD_BYTES_FIL0 &&
					event_type <= FEAC_WR_BEATS_FIL0) &&
					!(event_type >= FEAC_RD_BYTES_FIL1 &&
					event_type <= FEAC_WR_BEATS_FIL1)) {
				val |= (filter_sel << FILTER_SEL_SHIFT) | FILTER_EN;
			}
		}
	}

	if (filter_en)
		mask_val |= FILTER_SEL_MASK | FILTER_EN_MASK;

	offset = FEAC_PROF_EVENT_n_CFG(llcc_priv->drv_ver, *counter_num);
	llcc_bcast_modify(llcc_priv, offset, val, mask_val);
	perfmon_cntr_config(llcc_priv, EVENT_PORT_FEAC, *counter_num, enable);
	return true;
}

static void feac_event_enable(struct llcc_perfmon_private *llcc_priv, bool enable)
{
	uint32_t val = 0, val_cfg1 = 0, mask_val = 0, offset;
	bool prof_cfg_filter = false, prof_cfg1_filter1 = false;

	prof_cfg_filter = llcc_priv->port_filter_sel[FILTER_0] & (1 << EVENT_PORT_FEAC);
	prof_cfg1_filter1 = llcc_priv->port_filter_sel[FILTER_1] & (1 << EVENT_PORT_FEAC);

	val = (BYTE_SCALING << BYTE_SCALING_SHIFT) | (BEAT_SCALING << BEAT_SCALING_SHIFT);
	val_cfg1 = (BYTE_SCALING << BYTE_SCALING_SHIFT) | (BEAT_SCALING << BEAT_SCALING_SHIFT);
	mask_val = PROF_CFG_BEAT_SCALING_MASK | PROF_CFG_BYTE_SCALING_MASK | PROF_CFG_EN_MASK;

	if (prof_cfg_filter || prof_cfg1_filter1) {
		if (llcc_priv->version == REV_0) {
			mask_val |= FEAC_SCALING_FILTER_SEL_MASK | FEAC_SCALING_FILTER_EN_MASK;
			val |= (FILTER_0 << FEAC_SCALING_FILTER_SEL_SHIFT) |
				FEAC_SCALING_FILTER_EN;
		} else {
			mask_val |= FEAC_WR_BEAT_FILTER_SEL_MASK | FEAC_WR_BEAT_FILTER_EN_MASK |
				FEAC_WR_BYTE_FILTER_SEL_MASK | FEAC_WR_BYTE_FILTER_EN_MASK |
				FEAC_RD_BEAT_FILTER_SEL_MASK | FEAC_RD_BEAT_FILTER_EN_MASK |
				FEAC_RD_BYTE_FILTER_SEL_MASK | FEAC_RD_BYTE_FILTER_EN_MASK;
			val |= FEAC_WR_BEAT_FILTER_EN | FEAC_WR_BYTE_FILTER_EN |
				FEAC_RD_BEAT_FILTER_EN | FEAC_RD_BYTE_FILTER_EN;

			if (prof_cfg_filter && prof_cfg1_filter1) {
				val_cfg1 = val;
				val |= (FILTER_0 << FEAC_WR_BEAT_FILTER_SEL_SHIFT) |
					(FILTER_0 << FEAC_WR_BYTE_FILTER_SEL_SHIFT) |
					(FILTER_0 << FEAC_RD_BEAT_FILTER_SEL_SHIFT) |
					(FILTER_0 << FEAC_RD_BYTE_FILTER_SEL_SHIFT);
				val_cfg1 |= (FILTER_1 << FEAC_WR_BEAT_FILTER_SEL_SHIFT) |
					(FILTER_1 << FEAC_WR_BYTE_FILTER_SEL_SHIFT) |
					(FILTER_1 << FEAC_RD_BEAT_FILTER_SEL_SHIFT) |
					(FILTER_1 << FEAC_RD_BYTE_FILTER_SEL_SHIFT);
			} else if (prof_cfg1_filter1) {
				val |= (FILTER_1 << FEAC_WR_BEAT_FILTER_SEL_SHIFT) |
					(FILTER_1 << FEAC_WR_BYTE_FILTER_SEL_SHIFT) |
					(FILTER_1 << FEAC_RD_BEAT_FILTER_SEL_SHIFT) |
					(FILTER_1 << FEAC_RD_BYTE_FILTER_SEL_SHIFT);
			} else if (prof_cfg_filter) {
				val |= (FILTER_0 << FEAC_WR_BEAT_FILTER_SEL_SHIFT) |
					(FILTER_0 << FEAC_WR_BYTE_FILTER_SEL_SHIFT) |
					(FILTER_0 << FEAC_RD_BEAT_FILTER_SEL_SHIFT) |
					(FILTER_0 << FEAC_RD_BYTE_FILTER_SEL_SHIFT);
			}
		}
	}

	val |= PROF_EN;
	mask_val |= PROF_CFG_EN_MASK;
	if (!enable) {
		val = 0;
		val_cfg1 = 0;
	}

	/* Hardware version based filtering capabilities, if cache version v31 or higher, both
	 * filter0 & 1 can be applied on PROF_CFG and PROG_CFG1 respectively. Otherwise for a
	 * single applied filter only PROF_CFG will be used for either filter 0 or 1
	 */
	if (llcc_priv->version >= REV_2 && (prof_cfg_filter && prof_cfg1_filter1)) {
		offset = FEAC_PROF_CFG(llcc_priv->drv_ver);
		llcc_bcast_modify(llcc_priv, offset, val, mask_val);
		mask_val &= ~PROF_CFG_EN_MASK;
		offset = FEAC_PROF_CFG1(llcc_priv->drv_ver);
		llcc_bcast_modify(llcc_priv, offset, val_cfg1, mask_val);
	} else {
		offset = FEAC_PROF_CFG(llcc_priv->drv_ver);
		llcc_bcast_modify(llcc_priv, offset, val, mask_val);
	}
}

static bool feac_event_filter_config(struct llcc_perfmon_private *llcc_priv,
		enum filter_type filter, unsigned long long match, unsigned long long mask,
		bool enable, u8 filter_sel)
{
	uint64_t val = 0;
	uint32_t mask_val = 0, offset;
	u32 lower_val_mask = 0, lower_val_match = 0, upper_val_match = 0, upper_val_mask = 0;
	u32 lower_offset_match, lower_offset_mask;

	switch (filter) {
	case SCID:
		if (llcc_priv->version == REV_0) {
			if (enable)
				val = (match << SCID_MATCH_SHIFT) | (mask << SCID_MASK_SHIFT);

			mask_val = SCID_MATCH_MASK | SCID_MASK_MASK;
		} else {
			val = SCID_MULTI_MATCH_MASK;
			mask_val = SCID_MULTI_MATCH_MASK;
			if (enable)
				val = (1 << match);
		}

		offset = FEAC_PROF_FILTER_0_CFG6(llcc_priv->drv_ver);
		if (filter_sel)
			offset = FEAC_PROF_FILTER_1_CFG6(llcc_priv->drv_ver);
		break;
	case MULTISCID:
		if (llcc_priv->version != REV_0) {
			/* Clear register for multi scid filter settings */
			val = SCID_MULTI_MATCH_MASK;
			mask_val = SCID_MULTI_MATCH_MASK;
			if (enable)
				val = match;
		}

		offset = FEAC_PROF_FILTER_0_CFG6(llcc_priv->drv_ver);
		if (filter_sel)
			offset = FEAC_PROF_FILTER_1_CFG6(llcc_priv->drv_ver);
		break;
	case MID:
		if (enable)
			val = (match << MID_MATCH_SHIFT) | (mask << MID_MASK_SHIFT);

		mask_val = MID_MATCH_MASK | MID_MASK_MASK;
		offset = FEAC_PROF_FILTER_0_CFG5(llcc_priv->drv_ver);
		if (filter_sel)
			offset = FEAC_PROF_FILTER_1_CFG5(llcc_priv->drv_ver);
		break;
	case OPCODE:
		if (enable)
			val = (match << OPCODE_MATCH_SHIFT) | (mask << OPCODE_MASK_SHIFT);

		mask_val = OPCODE_MATCH_MASK | OPCODE_MASK_MASK;
		offset = FEAC_PROF_FILTER_0_CFG3(llcc_priv->drv_ver);
		if (filter_sel)
			offset = FEAC_PROF_FILTER_1_CFG3(llcc_priv->drv_ver);
		break;
	case CACHEALLOC:
		if (enable)
			val = (match << CACHEALLOC_MATCH_SHIFT) | (mask << CACHEALLOC_MASK_SHIFT);

		mask_val = CACHEALLOC_MATCH_MASK | CACHEALLOC_MASK_MASK;
		offset = FEAC_PROF_FILTER_0_CFG3(llcc_priv->drv_ver);
		if (filter_sel)
			offset = FEAC_PROF_FILTER_1_CFG3(llcc_priv->drv_ver);
		break;
	case MEMTAGOPS:
		if (enable)
			val = (match << MEMTAGOPS_MATCH_SHIFT) | (mask << MEMTAGOPS_MASK_SHIFT);

		mask_val = MEMTAGOPS_MATCH_MASK | MEMTAGOPS_MASK_MASK;
		offset = FEAC_PROF_FILTER_0_CFG7(llcc_priv->drv_ver);
		if (filter_sel)
			offset = FEAC_PROF_FILTER_1_CFG7(llcc_priv->drv_ver);
		break;
	case DIRTYINFO:
		if (enable)
			val = (match << DIRTYINFO_MATCH_SHIFT) | (mask << DIRTYINFO_MASK_SHIFT);

		mask_val = DIRTYINFO_MATCH_MASK | DIRTYINFO_MASK_MASK;
		offset = FEAC_PROF_FILTER_0_CFG7(llcc_priv->drv_ver);
		if (filter_sel)
			offset = FEAC_PROF_FILTER_1_CFG7(llcc_priv->drv_ver);
		break;
	case ADDR_MASK:
		if (enable) {
			lower_val_match = (match & ADDR_LOWER_MASK) << FEAC_ADDR_LOWER_MATCH_SHIFT;
			lower_val_mask = (mask & ADDR_LOWER_MASK) << FEAC_ADDR_LOWER_MASK_SHIFT;
			upper_val_match = (match & ADDR_UPPER_MASK) >> ADDR_UPPER_SHIFT;
			upper_val_mask = (mask & ADDR_UPPER_MASK) >> ADDR_UPPER_SHIFT;
			val = (upper_val_match << FEAC_ADDR_UPPER_MATCH_SHIFT) |
				(upper_val_mask << FEAC_ADDR_UPPER_MASK_SHIFT);
		}

		lower_offset_match = FEAC_PROF_FILTER_0_CFG1(llcc_priv->drv_ver);
		lower_offset_mask = FEAC_PROF_FILTER_0_CFG2(llcc_priv->drv_ver);
		offset = FEAC_PROF_FILTER_0_CFG3(llcc_priv->drv_ver);
		if (filter_sel) {
			lower_offset_match = FEAC_PROF_FILTER_1_CFG1(llcc_priv->drv_ver);
			lower_offset_mask = FEAC_PROF_FILTER_1_CFG2(llcc_priv->drv_ver);
			offset = FEAC_PROF_FILTER_1_CFG3(llcc_priv->drv_ver);
		}

		mask_val = FEAC_ADDR_LOWER_MATCH_MASK;
		llcc_bcast_modify(llcc_priv, lower_offset_match, lower_val_match, mask_val);

		mask_val = FEAC_ADDR_LOWER_MASK_MASK;
		llcc_bcast_modify(llcc_priv, lower_offset_mask, lower_val_mask, mask_val);

		mask_val = FEAC_ADDR_UPPER_MATCH_MASK | FEAC_ADDR_UPPER_MASK_MASK;
		break;
	default:
		pr_err("unknown filter/not supported\n");
		return false;
	}

	llcc_bcast_modify(llcc_priv, offset, val, mask_val);
	return true;
}

static struct event_port_ops feac_port_ops = {
	.event_config	= feac_event_config,
	.event_enable	= feac_event_enable,
	.event_filter_config	= feac_event_filter_config,
};

static bool ferc_event_config(struct llcc_perfmon_private *llcc_priv, unsigned int event_type,
		unsigned int *counter_num, bool enable)
{
	uint32_t val = 0, mask_val, offset;
	u8 filter_en, filter_sel = FILTER_0;

	filter_en = llcc_priv->port_filter_sel[filter_sel] & (1 << EVENT_PORT_FERC);
	if (llcc_priv->fltr_logic ==  multiple_filtr) {
		filter_en = llcc_priv->configured[*counter_num].filter_en;
		filter_sel = llcc_priv->configured[*counter_num].filter_sel;
	}

	mask_val = EVENT_SEL_MASK;

	if (filter_en)
		mask_val |= FILTER_SEL_MASK | FILTER_EN_MASK;

	if (enable) {
		val = (event_type << EVENT_SEL_SHIFT) & EVENT_SEL_MASK;
		if (filter_en)
			val |= (filter_sel << FILTER_SEL_SHIFT) | FILTER_EN;

	}

	offset = FERC_PROF_EVENT_n_CFG(llcc_priv->drv_ver, *counter_num);
	llcc_bcast_modify(llcc_priv, offset, val, mask_val);
	perfmon_cntr_config(llcc_priv, EVENT_PORT_FERC, *counter_num, enable);
	return true;
}

static void ferc_event_enable(struct llcc_perfmon_private *llcc_priv, bool enable)
{
	uint32_t val = 0, mask_val, offset;

	if (enable)
		val = (BYTE_SCALING << BYTE_SCALING_SHIFT) | (BEAT_SCALING << BEAT_SCALING_SHIFT) |
			PROF_EN;

	mask_val = PROF_CFG_BEAT_SCALING_MASK | PROF_CFG_BYTE_SCALING_MASK | PROF_CFG_EN_MASK;
	offset = FERC_PROF_CFG(llcc_priv->drv_ver);
	llcc_bcast_modify(llcc_priv, offset, val, mask_val);
}

static bool ferc_event_filter_config(struct llcc_perfmon_private *llcc_priv,
		enum filter_type filter, unsigned long long match,
		unsigned long long mask, bool enable, u8 filter_sel)
{
	uint32_t val = 0, mask_val, offset;

	if (filter != PROFILING_TAG) {
		pr_err("unknown filter/not supported\n");
		return false;
	}

	if (enable)
		val = (match << PROFTAG_MATCH_SHIFT) | (mask << PROFTAG_MASK_SHIFT);

	mask_val = PROFTAG_MATCH_MASK | PROFTAG_MASK_MASK;
	offset = FERC_PROF_FILTER_0_CFG0(llcc_priv->drv_ver);
	if (filter_sel)
		offset = FERC_PROF_FILTER_1_CFG0(llcc_priv->drv_ver);

	llcc_bcast_modify(llcc_priv, offset, val, mask_val);
	return true;
}

static struct event_port_ops ferc_port_ops = {
	.event_config	= ferc_event_config,
	.event_enable	= ferc_event_enable,
	.event_filter_config	= ferc_event_filter_config,
};

static bool fewc_event_config(struct llcc_perfmon_private *llcc_priv, unsigned int event_type,
		unsigned int *counter_num, bool enable)
{
	uint32_t val = 0, mask_val, offset;
	u8 filter_en, filter_sel = FILTER_0;

	filter_en = llcc_priv->port_filter_sel[filter_sel] & (1 << EVENT_PORT_FEWC);
	if (llcc_priv->fltr_logic ==  multiple_filtr) {
		filter_en = llcc_priv->configured[*counter_num].filter_en;
		filter_sel = llcc_priv->configured[*counter_num].filter_sel;
	}

	mask_val = EVENT_SEL_MASK;
	if (filter_en)
		mask_val |= FILTER_SEL_MASK | FILTER_EN_MASK;

	if (enable) {
		val = (event_type << EVENT_SEL_SHIFT) & EVENT_SEL_MASK;
		if (filter_en)
			val |= (filter_sel << FILTER_SEL_SHIFT) | FILTER_EN;

	}

	offset = FEWC_PROF_EVENT_n_CFG(llcc_priv->drv_ver, *counter_num);
	llcc_bcast_modify(llcc_priv, offset, val, mask_val);
	perfmon_cntr_config(llcc_priv, EVENT_PORT_FEWC, *counter_num, enable);
	return true;
}

static bool fewc_event_filter_config(struct llcc_perfmon_private *llcc_priv,
		enum filter_type filter, unsigned long long match,
		unsigned long long mask, bool enable, u8 filter_sel)
{
	uint32_t val = 0, mask_val, offset;

	if (filter != PROFILING_TAG) {
		pr_err("unknown filter/not supported\n");
		return false;
	}

	if (enable)
		val = (match << PROFTAG_MATCH_SHIFT) | (mask << PROFTAG_MASK_SHIFT);

	mask_val = PROFTAG_MATCH_MASK | PROFTAG_MASK_MASK;
	offset = FEWC_PROF_FILTER_0_CFG0(llcc_priv->drv_ver);
	if (filter_sel)
		offset = FEWC_PROF_FILTER_1_CFG0(llcc_priv->drv_ver);

	llcc_bcast_modify(llcc_priv, offset, val, mask_val);
	return true;
}

static struct event_port_ops fewc_port_ops = {
	.event_config	= fewc_event_config,
	.event_filter_config	= fewc_event_filter_config,
};

static bool beac_event_config(struct llcc_perfmon_private *llcc_priv, unsigned int event_type,
		unsigned int *counter_num, bool enable)
{
	uint32_t val = 0, mask_val, offset;
	u8 filter_en, filter_sel = FILTER_0;
	unsigned int mc_cnt;
	struct llcc_perfmon_counter_map *counter_map;

	filter_en = llcc_priv->port_filter_sel[filter_sel] & (1 << EVENT_PORT_BEAC);
	if (llcc_priv->fltr_logic ==  multiple_filtr) {
		filter_en = llcc_priv->configured[*counter_num].filter_en;
		filter_sel = llcc_priv->configured[*counter_num].filter_sel;
	}

	mask_val = EVENT_SEL_MASK;

	if (enable) {
		val = (event_type << EVENT_SEL_SHIFT) & EVENT_SEL_MASK;
		if (filter_en) {
			/* In case BEAC, events Read Write beat byte filter selection logic should
			 * not apply as these events do not use FILTER_SEL and FILTER_EN fields
			 * from LLCC_*_PROF_EVENT_n_CFG. Instead, they use exclusive filters that
			 * need to be configured for that specific event using PROF_CFG, PROF_CFG0,
			 * PROF_CFG1.
			 */
			if (((event_type >= BEAC_MC_RD_BEAT_FIL0 &&
				event_type <= BEAC_MC_WR_BEAT_FIL0) && filter_sel == FILTER_1) ||
				((event_type >= BEAC_MC_RD_BEAT_FIL1 &&
				  event_type <= BEAC_MC_WR_BEAT_FIL1) && filter_sel == FILTER_0)) {
				pr_err("Invalid configuration for BEAC, removing\n");
				return false;
			} else {
				val |= (filter_sel << FILTER_SEL_SHIFT) | FILTER_EN;
			}
		}
	}

	if (filter_en)
		mask_val |= FILTER_SEL_MASK | FILTER_EN_MASK;

	for (mc_cnt = 0; mc_cnt < llcc_priv->num_mc; mc_cnt++) {
		offset = BEAC0_PROF_EVENT_n_CFG(llcc_priv->drv_ver, *counter_num + mc_cnt) +
			mc_cnt * BEAC_INST_OFF;
		llcc_bcast_modify(llcc_priv, offset, val, mask_val);
		perfmon_cntr_config(llcc_priv, EVENT_PORT_BEAC, *counter_num, enable);
		/* DBX uses 2 counters for BEAC 0 & 1 */
		if (mc_cnt == 1)
			perfmon_cntr_config(llcc_priv, EVENT_PORT_BEAC1, *counter_num + mc_cnt,
					enable);
	}

	/* DBX uses 2 counters for BEAC 0 & 1 */
	if (llcc_priv->num_mc > 1) {
		counter_map = &llcc_priv->configured[(*counter_num)++];
		counter_map->port_sel = MAX_NUMBER_OF_PORTS;
		counter_map->event_sel = UNKNOWN_EVENT;
		if (enable) {
			counter_map->port_sel = EVENT_PORT_BEAC;
			counter_map->event_sel = event_type;
		}
	}

	return true;
}

static void beac_event_enable(struct llcc_perfmon_private *llcc_priv, bool enable)
{
	uint32_t val = 0, val_cfg0 = 0, val_cfg1 = 0, mask_val = 0, mask_val0, mask_val1, offset;
	bool prof_cfg_filter = false, prof_cfg1_filter1 = false;
	unsigned int mc_cnt;

	prof_cfg_filter = llcc_priv->port_filter_sel[FILTER_0] & (1 << EVENT_PORT_BEAC);
	prof_cfg1_filter1 = llcc_priv->port_filter_sel[FILTER_1] & (1 << EVENT_PORT_BEAC);

	val = val_cfg0 = val_cfg1 = (BEAT_SCALING << BEAT_SCALING_SHIFT);
	mask_val = PROF_CFG_BEAT_SCALING_MASK;

	if (prof_cfg_filter || prof_cfg1_filter1) {
		mask_val |=  BEAC_WR_BEAT_FILTER_SEL_MASK | BEAC_WR_BEAT_FILTER_EN_MASK |
			BEAC_RD_BEAT_FILTER_SEL_MASK | BEAC_RD_BEAT_FILTER_EN_MASK;

		val |= BEAC_WR_BEAT_FILTER_EN | BEAC_RD_BEAT_FILTER_EN;
		if (prof_cfg1_filter1) {
			val_cfg1 |= (FILTER_1 << BEAC_WR_BEAT_FILTER_SEL_SHIFT) |
				(FILTER_1 << BEAC_RD_BEAT_FILTER_SEL_SHIFT) |
				BEAC_RD_BEAT_FILTER_EN | BEAC_WR_BEAT_FILTER_EN;
		}

		if (prof_cfg_filter) {
			val_cfg0 |= (FILTER_0 << BEAC_WR_BEAT_FILTER_SEL_SHIFT) |
				(FILTER_0 << BEAC_RD_BEAT_FILTER_SEL_SHIFT) |
				BEAC_RD_BEAT_FILTER_EN | BEAC_WR_BEAT_FILTER_EN;
		}
	}

	val |= (BYTE_SCALING << BYTE_SCALING_SHIFT) | PROF_EN;
	mask_val0 = mask_val1 = mask_val;
	mask_val |= PROF_CFG_BYTE_SCALING_MASK | PROF_CFG_EN_MASK | BEAC_MC_PROFTAG_MASK;
	if (!enable)
		val = val_cfg0 = val_cfg1 = 0;

	for (mc_cnt = 0; mc_cnt < llcc_priv->num_mc; mc_cnt++) {
		offset = BEAC0_PROF_CFG0(llcc_priv->drv_ver) + mc_cnt * BEAC_INST_OFF;
		llcc_bcast_modify(llcc_priv, offset, val_cfg0, mask_val0);
		offset = BEAC0_PROF_CFG1(llcc_priv->drv_ver) + mc_cnt * BEAC_INST_OFF;
		llcc_bcast_modify(llcc_priv, offset, val_cfg1, mask_val1);
		offset = BEAC0_PROF_CFG(llcc_priv->drv_ver) + mc_cnt * BEAC_INST_OFF;
		llcc_bcast_modify(llcc_priv, offset, val, mask_val);
	}

}

static bool beac_event_filter_config(struct llcc_perfmon_private *llcc_priv,
		enum filter_type filter, unsigned long long match,
		unsigned long long mask, bool enable, u8 filter_sel)
{
	uint64_t val = 0;
	uint32_t mask_val;
	unsigned int mc_cnt, offset;

	switch (filter) {
	case PROFILING_TAG:
		if (enable)
			val = (match << BEAC_PROFTAG_MATCH_SHIFT) |
				(mask << BEAC_PROFTAG_MASK_SHIFT);

		mask_val = BEAC_PROFTAG_MASK_MASK | BEAC_PROFTAG_MATCH_MASK;
		llcc_priv->mc_proftag = MCPROF_FEAC_FLTR_0;
		if (match == 2)
			llcc_priv->mc_proftag = MCPROF_FEAC_FLTR_1;

		offset = BEAC0_PROF_FILTER_0_CFG5(llcc_priv->drv_ver);
		if (filter_sel)
			offset = BEAC0_PROF_FILTER_1_CFG5(llcc_priv->drv_ver);
		break;
	case MID:
		if (enable)
			val = (match << MID_MATCH_SHIFT) | (mask << MID_MASK_SHIFT);

		mask_val = MID_MATCH_MASK | MID_MASK_MASK;
		llcc_priv->mc_proftag = MCPROF_BEAC_FLTR_0 + filter_sel;
		offset = BEAC0_PROF_FILTER_0_CFG2(llcc_priv->drv_ver);
		if (filter_sel)
			offset = BEAC0_PROF_FILTER_1_CFG2(llcc_priv->drv_ver);
		break;
	case ADDR_MASK:
		if (enable)
			val = (match & ADDR_LOWER_MASK) << BEAC_ADDR_LOWER_MATCH_SHIFT;

		mask_val = BEAC_ADDR_LOWER_MATCH_MASK;
		offset = BEAC0_PROF_FILTER_0_CFG4(llcc_priv->drv_ver);
		if (filter_sel)
			offset = BEAC0_PROF_FILTER_1_CFG4(llcc_priv->drv_ver);

		for (mc_cnt = 0; mc_cnt < llcc_priv->num_mc; mc_cnt++) {
			llcc_bcast_modify(llcc_priv, offset, val, mask_val);
			offset = offset + BEAC_INST_OFF;
		}
		if (enable)
			val = (mask & ADDR_LOWER_MASK) << BEAC_ADDR_LOWER_MASK_SHIFT;

		mask_val = BEAC_ADDR_LOWER_MASK_MASK;
		offset = BEAC0_PROF_FILTER_0_CFG3(llcc_priv->drv_ver);
		if (filter_sel)
			offset = BEAC0_PROF_FILTER_1_CFG3(llcc_priv->drv_ver);

		for (mc_cnt = 0; mc_cnt < llcc_priv->num_mc; mc_cnt++) {
			llcc_bcast_modify(llcc_priv, offset, val, mask_val);
			offset = offset + BEAC_INST_OFF;
		}
		if (enable) {
			match = (match & ADDR_UPPER_MASK) >> ADDR_UPPER_SHIFT;
			mask = (mask & ADDR_UPPER_MASK) >> ADDR_UPPER_SHIFT;
			val = (match << FEAC_ADDR_UPPER_MATCH_SHIFT) |
				(mask << FEAC_ADDR_UPPER_MASK_SHIFT);
		}

		mask_val = BEAC_ADDR_UPPER_MATCH_MASK | BEAC_ADDR_UPPER_MASK_MASK;
		llcc_priv->mc_proftag = MCPROF_BEAC_FLTR_0 + filter_sel;
		offset = BEAC0_PROF_FILTER_0_CFG5(llcc_priv->drv_ver);
		if (filter_sel)
			offset = BEAC0_PROF_FILTER_1_CFG5(llcc_priv->drv_ver);
		break;
	default:
		pr_err("unknown filter/not supported\n");
		return false;
	}

	for (mc_cnt = 0; mc_cnt < llcc_priv->num_mc; mc_cnt++) {
		llcc_bcast_modify(llcc_priv, offset, val, mask_val);
		offset = offset + BEAC_INST_OFF;
	}

	/* mc_prf tag set to reset value */
	if (!enable)
		llcc_priv->mc_proftag = MCPROF_BEAC_FLTR_0;

	return true;
}

static struct event_port_ops beac_port_ops = {
	.event_config	= beac_event_config,
	.event_enable	= beac_event_enable,
	.event_filter_config	= beac_event_filter_config,
};

static bool berc_event_config(struct llcc_perfmon_private *llcc_priv, unsigned int event_type,
		unsigned int *counter_num, bool enable)
{
	uint64_t val = 0;
	uint32_t mask_val, offset;
	u8 filter_en, filter_sel = FILTER_0;

	filter_en = llcc_priv->port_filter_sel[filter_sel] & (1 << EVENT_PORT_BERC);
	if (llcc_priv->fltr_logic ==  multiple_filtr) {
		filter_en = llcc_priv->configured[*counter_num].filter_en;
		filter_sel = llcc_priv->configured[*counter_num].filter_sel;
	}

	mask_val = EVENT_SEL_MASK;

	if (filter_en)
		mask_val |= FILTER_SEL_MASK | FILTER_EN_MASK;

	if (enable) {
		val = (event_type << EVENT_SEL_SHIFT) & EVENT_SEL_MASK;
		if (filter_en)
			val |= (filter_sel << FILTER_SEL_SHIFT) | FILTER_EN;
	}

	offset = BERC_PROF_EVENT_n_CFG(llcc_priv->drv_ver, *counter_num);
	llcc_bcast_modify(llcc_priv, offset, val, mask_val);
	perfmon_cntr_config(llcc_priv, EVENT_PORT_BERC, *counter_num, enable);
	return true;
}

static void berc_event_enable(struct llcc_perfmon_private *llcc_priv, bool enable)
{
	uint32_t val = 0, mask_val, offset;

	if (enable)
		val = (BYTE_SCALING << BYTE_SCALING_SHIFT) | (BEAT_SCALING << BEAT_SCALING_SHIFT) |
			PROF_EN;

	mask_val = PROF_CFG_BEAT_SCALING_MASK | PROF_CFG_BYTE_SCALING_MASK | PROF_CFG_EN_MASK;
	offset = BERC_PROF_CFG(llcc_priv->drv_ver);
	llcc_bcast_modify(llcc_priv, offset, val, mask_val);
}

static bool berc_event_filter_config(struct llcc_perfmon_private *llcc_priv,
		enum filter_type filter, unsigned long long match,
		unsigned long long mask, bool enable, u8 filter_sel)
{
	uint32_t val = 0, mask_val, offset;

	if (filter != PROFILING_TAG) {
		pr_err("unknown filter/not supported\n");
		return true;
	}

	if (enable)
		val = (match << PROFTAG_MATCH_SHIFT) | (mask << PROFTAG_MASK_SHIFT);

	mask_val = PROFTAG_MATCH_MASK | PROFTAG_MASK_MASK;
	offset = BERC_PROF_FILTER_0_CFG0(llcc_priv->drv_ver);
	if (filter_sel)
		offset = BERC_PROF_FILTER_1_CFG0(llcc_priv->drv_ver);

	llcc_bcast_modify(llcc_priv, offset, val, mask_val);
	return true;
}

static struct event_port_ops berc_port_ops = {
	.event_config	= berc_event_config,
	.event_enable	= berc_event_enable,
	.event_filter_config	= berc_event_filter_config,
};

static bool trp_event_config(struct llcc_perfmon_private *llcc_priv, unsigned int event_type,
		unsigned int *counter_num, bool enable)
{
	uint64_t val = 0;
	uint32_t mask_val;
	u8 filter_en, filter_sel = FILTER_0;

	filter_en = llcc_priv->port_filter_sel[filter_sel] & (1 << EVENT_PORT_TRP);
	if (llcc_priv->fltr_logic ==  multiple_filtr) {
		filter_en = llcc_priv->configured[*counter_num].filter_en;
		filter_sel = llcc_priv->configured[*counter_num].filter_sel;
	}

	mask_val = EVENT_SEL_MASK;
	if (llcc_priv->version >= REV_2)
		mask_val = EVENT_SEL_MASK7;

	if (enable) {
		val = (event_type << EVENT_SEL_SHIFT) & mask_val;
		if (llcc_priv->version >= REV_2)
			val = (event_type << EVENT_SEL_SHIFT) & EVENT_SEL_MASK7;

		if (filter_en)
			val |= (filter_sel << FILTER_SEL_SHIFT) | FILTER_EN;
	}

	if (filter_en)
		mask_val |= FILTER_SEL_MASK | FILTER_EN_MASK;


	llcc_bcast_modify(llcc_priv, TRP_PROF_EVENT_n_CFG(*counter_num), val, mask_val);
	perfmon_cntr_config(llcc_priv, EVENT_PORT_TRP, *counter_num, enable);
	return true;
}

static bool trp_event_filter_config(struct llcc_perfmon_private *llcc_priv, enum filter_type filter,
		unsigned long long match, unsigned long long mask, bool enable, u8 filter_sel)
{
	uint64_t val = 0;
	uint32_t mask_val, offset;

	switch (filter) {
	case SCID:
		if (llcc_priv->version >= REV_2) {
			val = SCID_MULTI_MATCH_MASK;
			if (enable)
				val = (1 << match);

			mask_val = SCID_MULTI_MATCH_MASK;
			offset = TRP_PROF_FILTER_0_CFG2;
			if (filter_sel)
				offset = TRP_PROF_FILTER_1_CFG2;
		} else {
			if (enable)
				val = (match << TRP_SCID_MATCH_SHIFT) |
					(mask << TRP_SCID_MASK_SHIFT);

			mask_val = TRP_SCID_MATCH_MASK | TRP_SCID_MASK_MASK;
			offset = TRP_PROF_FILTER_0_CFG1;
			if (filter_sel)
				offset = TRP_PROF_FILTER_1_CFG1;
		}

		break;
	case MULTISCID:
		if (llcc_priv->version >= REV_2) {
			val = SCID_MULTI_MATCH_MASK;
			if (enable)
				val = match;

			mask_val = SCID_MULTI_MATCH_MASK;
			offset = TRP_PROF_FILTER_0_CFG2;
			if (filter_sel)
				offset = TRP_PROF_FILTER_1_CFG2;
		} else {
			pr_err("unknown filter/not supported\n");
			return false;
		}

		break;
	case WAY_ID:
		if (enable)
			val = (match << TRP_WAY_ID_MATCH_SHIFT) | (mask << TRP_WAY_ID_MASK_SHIFT);

		mask_val = TRP_WAY_ID_MATCH_MASK | TRP_WAY_ID_MASK_MASK;
		offset = TRP_PROF_FILTER_0_CFG1;
		if (filter_sel)
			offset = TRP_PROF_FILTER_1_CFG1;
		break;
	case PROFILING_TAG:
		if (enable)
			val = (match << TRP_PROFTAG_MATCH_SHIFT) | (mask << TRP_PROFTAG_MASK_SHIFT);

		mask_val = TRP_PROFTAG_MATCH_MASK | TRP_PROFTAG_MASK_MASK;
		offset = TRP_PROF_FILTER_0_CFG1;
		if (filter_sel)
			offset = TRP_PROF_FILTER_1_CFG1;
		break;
	default:
		pr_err("unknown filter/not supported\n");
		return true;
	}

	llcc_bcast_modify(llcc_priv, offset, val, mask_val);
	return true;
}

static struct event_port_ops  trp_port_ops = {
	.event_config	= trp_event_config,
	.event_filter_config	= trp_event_filter_config,
};

static bool drp_event_config(struct llcc_perfmon_private *llcc_priv, unsigned int event_type,
		unsigned int *counter_num, bool enable)
{
	uint32_t val = 0, mask_val, offset;
	u8 filter_en, filter_sel = FILTER_0;

	filter_en = llcc_priv->port_filter_sel[filter_sel] & (1 << EVENT_PORT_DRP);
	if (llcc_priv->fltr_logic ==  multiple_filtr) {
		filter_en = llcc_priv->configured[*counter_num].filter_en;
		filter_sel = llcc_priv->configured[*counter_num].filter_sel;
	}

	mask_val = EVENT_SEL_MASK;

	if (llcc_priv->version >= REV_2)
		mask_val = EVENT_SEL_MASK7;

	if (enable) {
		val = (event_type << EVENT_SEL_SHIFT) & mask_val;
		if (filter_en)
			val |= (filter_sel << FILTER_SEL_SHIFT) | FILTER_EN;
	}

	if (filter_en)
		mask_val |= FILTER_SEL_MASK | FILTER_EN_MASK;

	offset = DRP_PROF_EVENT_n_CFG(llcc_priv->drv_ver, *counter_num);
	llcc_bcast_modify(llcc_priv, offset, val, mask_val);
	perfmon_cntr_config(llcc_priv, EVENT_PORT_DRP, *counter_num, enable);
	return true;
}

static void drp_event_enable(struct llcc_perfmon_private *llcc_priv, bool enable)
{
	uint32_t val = 0, mask_val, offset;

	if (enable)
		val = (BEAT_SCALING << BEAT_SCALING_SHIFT) | PROF_EN;

	mask_val = PROF_CFG_BEAT_SCALING_MASK | PROF_CFG_EN_MASK;
	offset = DRP_PROF_CFG(llcc_priv->drv_ver);
	llcc_bcast_modify(llcc_priv, offset, val, mask_val);
}

static struct event_port_ops drp_port_ops = {
	.event_config	= drp_event_config,
	.event_enable	= drp_event_enable,
};

static bool pmgr_event_config(struct llcc_perfmon_private *llcc_priv, unsigned int event_type,
		unsigned int *counter_num, bool enable)
{
	uint32_t val = 0, mask_val, offset;
	u8 filter_en, filter_sel = FILTER_0;

	filter_en = llcc_priv->port_filter_sel[filter_sel] & (1 << EVENT_PORT_PMGR);
	if (llcc_priv->fltr_logic ==  multiple_filtr) {
		filter_en = llcc_priv->configured[*counter_num].filter_en;
		filter_sel = llcc_priv->configured[*counter_num].filter_sel;
	}

	mask_val = EVENT_SEL_MASK;
	if (filter_en)
		mask_val |= FILTER_SEL_MASK | FILTER_EN_MASK;

	if (enable) {
		val = (event_type << EVENT_SEL_SHIFT) & EVENT_SEL_MASK;
		if (filter_en)
			val |= (filter_sel << FILTER_SEL_SHIFT) | FILTER_EN;
	}

	offset = PMGR_PROF_EVENT_n_CFG(llcc_priv->drv_ver, *counter_num);
	llcc_bcast_modify(llcc_priv, offset, val, mask_val);
	perfmon_cntr_config(llcc_priv, EVENT_PORT_PMGR, *counter_num, enable);
	return true;
}

static struct event_port_ops pmgr_port_ops = {
	.event_config	= pmgr_event_config,
};

static void llcc_register_event_port(struct llcc_perfmon_private *llcc_priv,
		struct event_port_ops *ops, unsigned int event_port_num)
{
	if (llcc_priv->port_configd >= MAX_NUMBER_OF_PORTS) {
		pr_err("Register port Failure!\n");
		return;
	}

	llcc_priv->port_configd = llcc_priv->port_configd + 1;
	llcc_priv->port_ops[event_port_num] = ops;
}

static enum hrtimer_restart llcc_perfmon_timer_handler(struct hrtimer *hrtimer)
{
	struct llcc_perfmon_private *llcc_priv = container_of(hrtimer, struct llcc_perfmon_private,
			hrtimer);

	perfmon_counter_dump(llcc_priv);
	hrtimer_forward_now(&llcc_priv->hrtimer, llcc_priv->expires);
	return HRTIMER_RESTART;
}

static int llcc_perfmon_probe(struct platform_device *pdev)
{
	int result = 0;
	struct llcc_perfmon_private *llcc_priv;
	struct llcc_drv_data *llcc_driv_data;
	uint32_t val, offset;

	llcc_driv_data = dev_get_drvdata(pdev->dev.parent);

	llcc_priv = devm_kzalloc(&pdev->dev, sizeof(*llcc_priv), GFP_KERNEL);
	if (llcc_priv == NULL)
		return -ENOMEM;

	if (!llcc_driv_data)
		return -ENOMEM;

	if (llcc_driv_data->regmap == NULL || llcc_driv_data->bcast_regmap == NULL)
		return -ENODEV;

	llcc_priv->llcc_map = llcc_driv_data->regmap;
	llcc_priv->llcc_bcast_map = llcc_driv_data->bcast_regmap;
	llcc_priv->drv_ver = llcc_driv_data->llcc_ver;
	offset = LLCC_COMMON_STATUS0(llcc_priv->drv_ver);
	llcc_bcast_read(llcc_priv, offset, &val);
	llcc_priv->num_mc = (val & NUM_MC_MASK) >> NUM_MC_SHIFT;
	/* Setting to 1, as some platforms it read as 0 */
	if (llcc_priv->num_mc == 0)
		llcc_priv->num_mc = 1;

	llcc_priv->num_banks = (val & LB_CNT_MASK) >> LB_CNT_SHIFT;
	for (val = 0; val < llcc_priv->num_banks; val++)
		llcc_priv->bank_off[val] = llcc_driv_data->offsets[val];

	llcc_priv->clock = devm_clk_get(&pdev->dev, "qdss_clk");
	if (IS_ERR_OR_NULL(llcc_priv->clock)) {
		pr_warn("failed to get qdss clock node\n");
		llcc_priv->clock = NULL;
	}

	result = sysfs_create_group(&pdev->dev.kobj, &llcc_perfmon_group);
	if (result) {
		pr_err("Unable to create sysfs group\n");
		return result;
	}

	mutex_init(&llcc_priv->mutex);
	platform_set_drvdata(pdev, llcc_priv);
	llcc_register_event_port(llcc_priv, &feac_port_ops, EVENT_PORT_FEAC);
	llcc_register_event_port(llcc_priv, &ferc_port_ops, EVENT_PORT_FERC);
	llcc_register_event_port(llcc_priv, &fewc_port_ops, EVENT_PORT_FEWC);
	llcc_register_event_port(llcc_priv, &beac_port_ops, EVENT_PORT_BEAC);
	llcc_register_event_port(llcc_priv, &berc_port_ops, EVENT_PORT_BERC);
	llcc_register_event_port(llcc_priv, &trp_port_ops, EVENT_PORT_TRP);
	llcc_register_event_port(llcc_priv, &drp_port_ops, EVENT_PORT_DRP);
	llcc_register_event_port(llcc_priv, &pmgr_port_ops, EVENT_PORT_PMGR);
	hrtimer_init(&llcc_priv->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	llcc_priv->hrtimer.function = llcc_perfmon_timer_handler;
	llcc_priv->expires = 0;
	llcc_priv->clock_enabled = false;
	offset = LLCC_COMMON_HW_INFO(llcc_priv->drv_ver);
	llcc_bcast_read(llcc_priv, offset, &val);
	llcc_priv->version = REV_0;
	if (val == LLCC_VERSION_1)
		llcc_priv->version = REV_1;
	else if ((val & MAJOR_VER_MASK) >= LLCC_VERSION_2)
		llcc_priv->version = REV_2;

	if ((val & MAJOR_VER_MASK) == LLCC_VERSION_5)
		llcc_priv->version = REV_5;
	pr_info("Revision <%x.%x.%x>, %d MEMORY CNTRLRS connected with LLCC\n",
			MAJOR_REV_NO(val), BRANCH_NO(val), MINOR_NO(val), llcc_priv->num_mc);

	/* Set snapshot_feature flag on supported platforms */
	llcc_priv->snapshot_feature = of_property_read_bool(pdev->dev.of_node,
							    "llcc-occupancy-snapshot");

	return 0;
}

static int llcc_perfmon_remove(struct platform_device *pdev)
{
	struct llcc_perfmon_private *llcc_priv = platform_get_drvdata(pdev);

	while (hrtimer_active(&llcc_priv->hrtimer))
		hrtimer_cancel(&llcc_priv->hrtimer);

	mutex_destroy(&llcc_priv->mutex);
	sysfs_remove_group(&pdev->dev.kobj, &llcc_perfmon_group);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id of_match_llcc_perfmon[] = {
	{
		.compatible = "qcom,llcc-perfmon",
	},
	{},
};
MODULE_DEVICE_TABLE(of, of_match_llcc_perfmon);

static struct platform_driver llcc_perfmon_driver = {
	.probe = llcc_perfmon_probe,
	.remove	= llcc_perfmon_remove,
	.driver	= {
		.name = LLCC_PERFMON_NAME,
		.of_match_table = of_match_llcc_perfmon,
	}
};
module_platform_driver(llcc_perfmon_driver);

MODULE_DESCRIPTION("QCOM LLCC PMU MONITOR");
MODULE_LICENSE("GPL");
