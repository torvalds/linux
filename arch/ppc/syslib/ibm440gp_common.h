/*
 * PPC440GP system library
 *
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 * Copyright (c) 2003 Zultys Technologies
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#ifdef __KERNEL__
#ifndef __PPC_SYSLIB_IBM440GP_COMMON_H
#define __PPC_SYSLIB_IBM440GP_COMMON_H

#ifndef __ASSEMBLY__

#include <linux/init.h>
#include <syslib/ibm44x_common.h>

/*
 * Please, refer to the Figure 13.1 in 440GP user manual
 *
 * if internal UART clock is used, ser_clk is ignored
 */
void ibm440gp_get_clocks(struct ibm44x_clocks*, unsigned int sys_clk,
	unsigned int ser_clk) __init;

#endif /* __ASSEMBLY__ */
#endif /* __PPC_SYSLIB_IBM440GP_COMMON_H */
#endif /* __KERNEL__ */
