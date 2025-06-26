/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 Linaro Limited, All rights reserved.
 * Author: Mike Leach <mike.leach@linaro.org>
 */

#ifndef _CORESIGHT_CORESIGHT_CTI_H
#define _CORESIGHT_CORESIGHT_CTI_H

#include <linux/coresight.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include "coresight-priv.h"

struct fwnode_handle;

/*
 * Device registers
 * 0x000 - 0x144: CTI programming and status
 * 0xEDC - 0xEF8: CTI integration test.
 * 0xF00 - 0xFFC: Coresight management registers.
 */
/* CTI programming registers */
#define CTICONTROL		0x000
#define CTIINTACK		0x010
#define CTIAPPSET		0x014
#define CTIAPPCLEAR		0x018
#define CTIAPPPULSE		0x01C
#define CTIINEN(n)		(0x020 + (4 * n))
#define CTIOUTEN(n)		(0x0A0 + (4 * n))
#define CTITRIGINSTATUS		0x130
#define CTITRIGOUTSTATUS	0x134
#define CTICHINSTATUS		0x138
#define CTICHOUTSTATUS		0x13C
#define CTIGATE			0x140
#define ASICCTL			0x144
/* Integration test registers */
#define ITCHINACK		0xEDC /* WO CTI CSSoc 400 only*/
#define ITTRIGINACK		0xEE0 /* WO CTI CSSoc 400 only*/
#define ITCHOUT			0xEE4 /* WO RW-600 */
#define ITTRIGOUT		0xEE8 /* WO RW-600 */
#define ITCHOUTACK		0xEEC /* RO CTI CSSoc 400 only*/
#define ITTRIGOUTACK		0xEF0 /* RO CTI CSSoc 400 only*/
#define ITCHIN			0xEF4 /* RO */
#define ITTRIGIN		0xEF8 /* RO */
/* management registers */
#define CTIDEVAFF0		0xFA8
#define CTIDEVAFF1		0xFAC

/*
 * CTI CSSoc 600 has a max of 32 trigger signals per direction.
 * CTI CSSoc 400 has 8 IO triggers - other CTIs can be impl def.
 * Max of in and out defined in the DEVID register.
 * - pick up actual number used from .dts parameters if present.
 */
#define CTIINOUTEN_MAX		32

/**
 * Group of related trigger signals
 *
 * @nr_sigs: number of signals in the group.
 * @used_mask: bitmask representing the signal indexes in the group.
 * @sig_types: array of types for the signals, length nr_sigs.
 */
struct cti_trig_grp {
	int nr_sigs;
	u32 used_mask;
	int sig_types[];
};

/**
 * Trigger connection - connection between a CTI and other (coresight) device
 * lists input and output trigger signals for the device
 *
 * @con_in: connected CTIIN signals for the device.
 * @con_out: connected CTIOUT signals for the device.
 * @con_dev: coresight device connected to the CTI, NULL if not CS device
 * @con_dev_name: name of connected device (CS or CPU)
 * @node: entry node in list of connections.
 * @con_attrs: Dynamic sysfs attributes specific to this connection.
 * @attr_group: Dynamic attribute group created for this connection.
 */
struct cti_trig_con {
	struct cti_trig_grp *con_in;
	struct cti_trig_grp *con_out;
	struct coresight_device *con_dev;
	const char *con_dev_name;
	struct list_head node;
	struct attribute **con_attrs;
	struct attribute_group *attr_group;
};

/**
 * struct cti_device - description of CTI device properties.
 *
 * @nt_trig_con: Number of external devices connected to this device.
 * @ctm_id: which CTM this device is connected to (by default it is
 *          assumed there is a single CTM per SoC, ID 0).
 * @trig_cons: list of connections to this device.
 * @cpu: CPU ID if associated with CPU, -1 otherwise.
 * @con_groups: combined static and dynamic sysfs groups for trigger
 *		connections.
 */
struct cti_device {
	int nr_trig_con;
	u32 ctm_id;
	struct list_head trig_cons;
	int cpu;
	const struct attribute_group **con_groups;
};

/**
 * struct cti_config - configuration of the CTI device hardware
 *
 * @nr_trig_max: Max number of trigger signals implemented on device.
 *		 (max of trig_in or trig_out) - from ID register.
 * @nr_ctm_channels: number of available CTM channels - from ID register.
 * @enable_req_count: CTI is enabled alongside >=1 associated devices.
 * @hw_enabled: true if hw is currently enabled.
 * @hw_powered: true if associated cpu powered on, or no cpu.
 * @trig_in_use: bitfield of in triggers registered as in use.
 * @trig_out_use: bitfield of out triggers registered as in use.
 * @trig_out_filter: bitfield of out triggers that are blocked if filter
 *		     enabled. Typically this would be dbgreq / restart on
 *		     a core CTI.
 * @trig_filter_enable: 1 if filtering enabled.
 * @xtrig_rchan_sel: channel selection for xtrigger connection show.
 * @ctiappset: CTI Software application channel set.
 * @ctiinout_sel: register selector for INEN and OUTEN regs.
 * @ctiinen: enable input trigger to a channel.
 * @ctiouten: enable output trigger from a channel.
 * @ctigate: gate channel output from CTI to CTM.
 * @asicctl: asic control register.
 */
struct cti_config {
	/* hardware description */
	int nr_ctm_channels;
	int nr_trig_max;

	/* cti enable control */
	int enable_req_count;
	bool hw_enabled;
	bool hw_powered;

	/* registered triggers and filtering */
	u32 trig_in_use;
	u32 trig_out_use;
	u32 trig_out_filter;
	bool trig_filter_enable;
	u8 xtrig_rchan_sel;

	/* cti cross trig programmable regs */
	u32 ctiappset;
	u8 ctiinout_sel;
	u32 ctiinen[CTIINOUTEN_MAX];
	u32 ctiouten[CTIINOUTEN_MAX];
	u32 ctigate;
	u32 asicctl;
};

/**
 * struct cti_drvdata - specifics for the CTI device
 * @base:	Memory mapped base address for this component..
 * @csdev:	Standard CoreSight device information.
 * @ctidev:	Extra information needed by the CTI/CTM framework.
 * @spinlock:	Control data access to one at a time.
 * @config:	Configuration data for this CTI device.
 * @node:	List entry of this device in the list of CTI devices.
 * @csdev_release: release function for underlying coresight_device.
 */
struct cti_drvdata {
	void __iomem *base;
	struct coresight_device	*csdev;
	struct cti_device ctidev;
	raw_spinlock_t spinlock;
	struct cti_config config;
	struct list_head node;
	void (*csdev_release)(struct device *dev);
};

/*
 * Channel operation types.
 */
enum cti_chan_op {
	CTI_CHAN_ATTACH,
	CTI_CHAN_DETACH,
};

enum cti_trig_dir {
	CTI_TRIG_IN,
	CTI_TRIG_OUT,
};

enum cti_chan_gate_op {
	CTI_GATE_CHAN_ENABLE,
	CTI_GATE_CHAN_DISABLE,
};

enum cti_chan_set_op {
	CTI_CHAN_SET,
	CTI_CHAN_CLR,
	CTI_CHAN_PULSE,
};

/* private cti driver fns & vars */
extern const struct attribute_group *coresight_cti_groups[];
int cti_add_default_connection(struct device *dev,
			       struct cti_drvdata *drvdata);
int cti_add_connection_entry(struct device *dev, struct cti_drvdata *drvdata,
			     struct cti_trig_con *tc,
			     struct coresight_device *csdev,
			     const char *assoc_dev_name);
struct cti_trig_con *cti_allocate_trig_con(struct device *dev, int in_sigs,
					   int out_sigs);
int cti_enable(struct coresight_device *csdev, enum cs_mode mode, void *data);
int cti_disable(struct coresight_device *csdev, void *data);
void cti_write_all_hw_regs(struct cti_drvdata *drvdata);
void cti_write_intack(struct device *dev, u32 ackval);
void cti_write_single_reg(struct cti_drvdata *drvdata, int offset, u32 value);
int cti_channel_trig_op(struct device *dev, enum cti_chan_op op,
			enum cti_trig_dir direction, u32 channel_idx,
			u32 trigger_idx);
int cti_channel_gate_op(struct device *dev, enum cti_chan_gate_op op,
			u32 channel_idx);
int cti_channel_setop(struct device *dev, enum cti_chan_set_op op,
		      u32 channel_idx);
int cti_create_cons_sysfs(struct device *dev, struct cti_drvdata *drvdata);
struct coresight_platform_data *
coresight_cti_get_platform_data(struct device *dev);
const char *cti_plat_get_node_name(struct fwnode_handle *fwnode);

/* cti powered and enabled */
static inline bool cti_active(struct cti_config *cfg)
{
	return cfg->hw_powered && cfg->hw_enabled;
}

#endif  /* _CORESIGHT_CORESIGHT_CTI_H */
