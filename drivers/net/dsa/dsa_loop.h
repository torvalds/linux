/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DSA_LOOP_H
#define __DSA_LOOP_H

struct dsa_chip_data;

struct dsa_loop_pdata {
	/* Must be first, such that dsa_register_switch() can access this
	 * without gory pointer manipulations
	 */
	struct dsa_chip_data cd;
	const char *name;
	unsigned int enabled_ports;
	const char *netdev;
};

#define DSA_LOOP_NUM_PORTS	6
#define DSA_LOOP_CPU_PORT	(DSA_LOOP_NUM_PORTS - 1)

#endif /* __DSA_LOOP_H */
