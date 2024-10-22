/* SPDX-License-Identifier: GPL-2.0 */
/* Texas Instruments ICSSG Industrial Ethernet Peripheral (IEP) Driver
 *
 * Copyright (C) 2023 Texas Instruments Incorporated - https://www.ti.com/
 *
 */

#ifndef __NET_TI_ICSS_IEP_H
#define __NET_TI_ICSS_IEP_H

#include <linux/mutex.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/regmap.h>

enum {
	ICSS_IEP_GLOBAL_CFG_REG,
	ICSS_IEP_GLOBAL_STATUS_REG,
	ICSS_IEP_COMPEN_REG,
	ICSS_IEP_SLOW_COMPEN_REG,
	ICSS_IEP_COUNT_REG0,
	ICSS_IEP_COUNT_REG1,
	ICSS_IEP_CAPTURE_CFG_REG,
	ICSS_IEP_CAPTURE_STAT_REG,

	ICSS_IEP_CAP6_RISE_REG0,
	ICSS_IEP_CAP6_RISE_REG1,

	ICSS_IEP_CAP7_RISE_REG0,
	ICSS_IEP_CAP7_RISE_REG1,

	ICSS_IEP_CMP_CFG_REG,
	ICSS_IEP_CMP_STAT_REG,
	ICSS_IEP_CMP0_REG0,
	ICSS_IEP_CMP0_REG1,
	ICSS_IEP_CMP1_REG0,
	ICSS_IEP_CMP1_REG1,

	ICSS_IEP_CMP8_REG0,
	ICSS_IEP_CMP8_REG1,
	ICSS_IEP_SYNC_CTRL_REG,
	ICSS_IEP_SYNC0_STAT_REG,
	ICSS_IEP_SYNC1_STAT_REG,
	ICSS_IEP_SYNC_PWIDTH_REG,
	ICSS_IEP_SYNC0_PERIOD_REG,
	ICSS_IEP_SYNC1_DELAY_REG,
	ICSS_IEP_SYNC_START_REG,
	ICSS_IEP_MAX_REGS,
};

/**
 * struct icss_iep_plat_data - Plat data to handle SoC variants
 * @config: Regmap configuration data
 * @reg_offs: register offsets to capture offset differences across SoCs
 * @flags: Flags to represent IEP properties
 */
struct icss_iep_plat_data {
	const struct regmap_config *config;
	u32 reg_offs[ICSS_IEP_MAX_REGS];
	u32 flags;
};

struct icss_iep {
	struct device *dev;
	void __iomem *base;
	const struct icss_iep_plat_data *plat_data;
	struct regmap *map;
	struct device_node *client_np;
	unsigned long refclk_freq;
	int clk_tick_time;	/* one refclk tick time in ns */
	struct ptp_clock_info ptp_info;
	struct ptp_clock *ptp_clock;
	struct mutex ptp_clk_mutex;	/* PHC access serializer */
	u32 def_inc;
	s16 slow_cmp_inc;
	u32 slow_cmp_count;
	const struct icss_iep_clockops *ops;
	void *clockops_data;
	u32 cycle_time_ns;
	u32 perout_enabled;
	bool pps_enabled;
	int cap_cmp_irq;
	u64 period;
	u32 latch_enable;
	struct work_struct work;
};

extern const struct icss_iep_clockops prueth_iep_clockops;

/* Firmware specific clock operations */
struct icss_iep_clockops {
	void (*settime)(void *clockops_data, u64 ns);
	void (*adjtime)(void *clockops_data, s64 delta);
	u64 (*gettime)(void *clockops_data, struct ptp_system_timestamp *sts);
	int (*perout_enable)(void *clockops_data,
			     struct ptp_perout_request *req, int on,
			     u64 *cmp);
	int (*extts_enable)(void *clockops_data, u32 index, int on);
};

struct icss_iep *icss_iep_get(struct device_node *np);
struct icss_iep *icss_iep_get_idx(struct device_node *np, int idx);
void icss_iep_put(struct icss_iep *iep);
int icss_iep_init(struct icss_iep *iep, const struct icss_iep_clockops *clkops,
		  void *clockops_data, u32 cycle_time_ns);
int icss_iep_exit(struct icss_iep *iep);
int icss_iep_get_count_low(struct icss_iep *iep);
int icss_iep_get_count_hi(struct icss_iep *iep);
int icss_iep_get_ptp_clock_idx(struct icss_iep *iep);
void icss_iep_init_fw(struct icss_iep *iep);
void icss_iep_exit_fw(struct icss_iep *iep);

#endif /* __NET_TI_ICSS_IEP_H */
