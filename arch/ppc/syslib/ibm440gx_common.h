/*
 * arch/ppc/kernel/ibm440gx_common.h
 *
 * PPC440GX system library
 *
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 * Copyright (c) 2003, 2004 Zultys Technologies
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#ifdef __KERNEL__
#ifndef __PPC_SYSLIB_IBM440GX_COMMON_H
#define __PPC_SYSLIB_IBM440GX_COMMON_H

#ifndef __ASSEMBLY__

#include <linux/config.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <syslib/ibm44x_common.h>

/*
 * Please, refer to the Figure 14.1 in 440GX user manual
 *
 * if internal UART clock is used, ser_clk is ignored
 */
void ibm440gx_get_clocks(struct ibm44x_clocks*, unsigned int sys_clk,
	unsigned int ser_clk) __init;

/* Enable L2 cache */
void ibm440gx_l2c_enable(void) __init;

/* Disable L2 cache */
void ibm440gx_l2c_disable(void) __init;

/* Enable/disable L2 cache for a particular chip revision */
void ibm440gx_l2c_setup(struct ibm44x_clocks*) __init;

/* Get Ethernet Group */
int ibm440gx_get_eth_grp(void) __init;

/* Set Ethernet Group */
void ibm440gx_set_eth_grp(int group) __init;

/* Enable TAH devices */
void ibm440gx_tah_enable(void) __init;

/* Add L2C info to /proc/cpuinfo */
int ibm440gx_show_cpuinfo(struct seq_file*);

#endif /* __ASSEMBLY__ */
#endif /* __PPC_SYSLIB_IBM440GX_COMMON_H */
#endif /* __KERNEL__ */
