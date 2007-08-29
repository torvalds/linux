/*
 * PowerPC 4xx related functions
 *
 * Copyright 2007 IBM Corporation.
 * Josh Boyer <jwboyer@linux.vnet.ibm.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef _POWERPC_BOOT_4XX_H_
#define _POWERPC_BOOT_4XX_H_

void ibm4xx_fixup_memsize(void);
void ibm4xx_denali_fixup_memsize(void);
void ibm44x_dbcr_reset(void);
void ibm40x_dbcr_reset(void);
void ibm4xx_quiesce_eth(u32 *emac0, u32 *emac1);
void ibm4xx_fixup_ebc_ranges(const char *ebc);
void ibm440ep_fixup_clocks(unsigned int sysclk, unsigned int ser_clk);

#endif /* _POWERPC_BOOT_4XX_H_ */
