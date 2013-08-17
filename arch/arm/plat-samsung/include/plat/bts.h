/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef	__EXYNOS_BTS_H_
#define	__EXYNOS_BTS_H_

/* BTS priority values for master IP from 0 to 15 */
enum bts_priority {
	BTS_FBM_DDR_R1 = 0,
	/* Best Effort */
	BTS_PRIOR_BE = 8,
	/* The highest priority */
	BTS_PRIOR_HARDTIME = 15,
};

/*
 * Some use-cases need to change the bus bandwidth of some devices to manipulate
 * the bus traffic. So bts_bw_change says how to change the bandwidth of BTS's
 * master IP.
 */
enum bts_bw_change {
	BTS_DECREASE_BW,
	BTS_INCREASE_BW,
	BTS_MIXER_BW,
};

/*
 * This order is a hardware specific recommendation about selecting deblock
 * sources. If a BTS device needs only one FBM source for deblocking, it should
 * select BTS_1ST_FBM_SRC first. If two FBM source needed, should select
 * both of BTS_1ST_FBM_SRC and BTS_2ND_FBM_SRC.
 */
enum bts_deblock_src_order {
	BTS_1ST_FBM_SRC = (1<<1),
	BTS_2ND_FBM_SRC = (1<<2),
};

/*
 * To select FBM soruce for deblocking, BTS should select the number of group
 * and one of left and right the selected FBM belongs to. A BTS can has 6 FBM
 * input port as an idle signal.
 * enum bts_fbm_input_port lists up all 6 FBM input port names.
 */
enum bts_fbm_input_port {
	BTS_FBM_G0_L = (1<<1),
	BTS_FBM_G0_R = (1<<2),
	BTS_FBM_G1_L = (1<<3),
	BTS_FBM_G1_R = (1<<4),
	BTS_FBM_G2_L = (1<<5),
	BTS_FBM_G2_R = (1<<6),
};

/*
 * Each BTS device has an action for bus traffic contorl. The BTS driver
 * reconfigures the BTS device with it.
 * BTS_NO_ACTION means no contol, nothing changed.
 * BTS_ON_OFF	 means enabling blocking feature or not. The BTS master IP has
 * 		 no blocking feature if the BTS device was disabled. That's
 * 		 increase the bus bandwidth.
 * BTS_CHANGE_OTHER_DEBLOCK
 * 		 means controling with others' BTS deviece. To increase bus
 *		 bandwidth decrease others' bus bandwidth.
 */
enum bts_traffic_control {
	BTS_NO_ACTION,
	BTS_ON_OFF,
	BTS_CHANGE_OTHER_DEBLOCK,
};

struct exynos_fbm_resource {
	enum bts_fbm_input_port port_name;
	enum bts_deblock_src_order deblock_src_order;
};

struct exynos_fbm_pdata {
	struct exynos_fbm_resource *res;
	int res_num;
};

struct exynos_bts_pdata {
	enum bts_priority def_priority;
	char *pd_name;
	char *clk_name;
	struct exynos_fbm_pdata *fbm;
	int res_num;
	bool deblock_changable;
	bool threshold_changable;
	enum bts_traffic_control traffic_control_act;
};

/* BTS API */
/* Initialize BTS drivers only included in the same pd_block */
void exynos_bts_initialize(char *pd_name, bool power_on);

/* Change bus traffic on BTS drivers to contol bus bandwidth */
void exynos_bts_change_bus_traffic(struct device *dev,
				   enum bts_bw_change bw_change);

/* Change threshold FBM */
void exynos_bts_change_threshold(enum bts_bw_change bw_change);

#ifdef CONFIG_S5P_DEV_BTS
#define bts_initialize(a, b) exynos_bts_initialize(a, b)
#define bts_change_bus_traffic(a, b) exynos_bts_change_bus_traffic(a, b)
#define bts_change_threshold(a) exynos_bts_change_threshold(a)
#else
#define bts_initialize(a, b) do {} while (0)
#define bts_change_bus_traffic(a, b) do {} while (0)
#define bts_change_threshold(a) do {} while (0)
#endif
#endif	/* __EXYNOS_BTS_H_ */
