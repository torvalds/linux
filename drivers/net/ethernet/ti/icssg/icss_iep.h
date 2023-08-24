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

struct icss_iep;
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
