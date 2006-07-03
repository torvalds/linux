/*
 * PPC440GP system library
 *
 * Matt Porter <mporter@mvista.com>
 * Copyright 2002-2003 MontaVista Software Inc.
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
#include <linux/types.h>
#include <asm/reg.h>
#include <asm/ibm44x.h>
#include <asm/mmu.h>

/*
 * Calculate 440GP clocks
 */
void __init ibm440gp_get_clocks(struct ibm44x_clocks* p,
				unsigned int sys_clk,
				unsigned int ser_clk)
{
	u32 cpc0_sys0 = mfdcr(DCRN_CPC0_SYS0);
	u32 cpc0_cr0 = mfdcr(DCRN_CPC0_CR0);
	u32 opdv = ((cpc0_sys0 >> 10) & 0x3) + 1;
	u32 epdv = ((cpc0_sys0 >> 8) & 0x3) + 1;

	if (cpc0_sys0 & 0x2){
		/* Bypass system PLL */
		p->cpu = p->plb = sys_clk;
	}
	else {
		u32 fbdv, fwdva, fwdvb, m, vco;

		fbdv = (cpc0_sys0 >> 18) & 0x0f;
		if (!fbdv)
			fbdv = 16;

		fwdva = 8 - ((cpc0_sys0 >> 15) & 0x7);
		fwdvb = 8 - ((cpc0_sys0 >> 12) & 0x7);

    		/* Feedback path */	
		if (cpc0_sys0 & 0x00000080){
			/* PerClk */
			m = fwdvb * opdv * epdv;
		}
		else {
			/* CPU clock */
			m = fbdv * fwdva;
    		}
		vco = sys_clk * m;
		p->cpu = vco / fwdva;
		p->plb = vco / fwdvb;
	}

	p->opb = p->plb / opdv;
	p->ebc = p->opb / epdv;

	if (cpc0_cr0 & 0x00400000){
		/* External UART clock */
		p->uart0 = p->uart1 = ser_clk;
	}
	else {
		/* Internal UART clock */
    		u32 uart_div = ((cpc0_cr0 >> 16) & 0x1f) + 1;
		p->uart0 = p->uart1 = p->plb / uart_div;
	}
}
