/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PowerPC 4xx related functions
 *
 * Copyright 2007 IBM Corporation.
 * Josh Boyer <jwboyer@linux.vnet.ibm.com>
 */
#ifndef _POWERPC_BOOT_4XX_H_
#define _POWERPC_BOOT_4XX_H_

void ibm4xx_sdram_fixup_memsize(void);
void ibm440spe_fixup_memsize(void);
void ibm4xx_denali_fixup_memsize(void);
void ibm44x_dbcr_reset(void);
void ibm4xx_quiesce_eth(u32 *emac0, u32 *emac1);
void ibm4xx_fixup_ebc_ranges(const char *ebc);

void ibm440gp_fixup_clocks(unsigned int sys_clk, unsigned int ser_clk);
void ibm440ep_fixup_clocks(unsigned int sys_clk, unsigned int ser_clk,
			   unsigned int tmr_clk);
void ibm440gx_fixup_clocks(unsigned int sys_clk, unsigned int ser_clk,
			   unsigned int tmr_clk);
void ibm440spe_fixup_clocks(unsigned int sys_clk, unsigned int ser_clk,
			    unsigned int tmr_clk);

#endif /* _POWERPC_BOOT_4XX_H_ */
