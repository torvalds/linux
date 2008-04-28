/*
 * linux/arch/mips/tx4938/common/prom.c
 *
 * common tx4938 memory interface
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for TX4938 in 2.6 - Manish Lachwani (mlachwani@mvista.com)
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/io.h>

static unsigned int __init
tx4938_process_sdccr(u64 * addr)
{
	u64 val;
	unsigned int sdccr_ce;
	unsigned int sdccr_rs;
	unsigned int sdccr_cs;
	unsigned int sdccr_mw;
	unsigned int rs = 0;
	unsigned int cs = 0;
	unsigned int mw = 0;
	unsigned int bc = 4;
	unsigned int msize = 0;

	val = ____raw_readq((void __iomem *)addr);

	/* MVMCP -- need #defs for these bits masks */
	sdccr_ce = ((val & (1 << 10)) >> 10);
	sdccr_rs = ((val & (3 << 5)) >> 5);
	sdccr_cs = ((val & (7 << 2)) >> 2);
	sdccr_mw = ((val & (1 << 0)) >> 0);

	if (sdccr_ce) {
		switch (sdccr_rs) {
		case 0:{
				rs = 2048;
				break;
			}
		case 1:{
				rs = 4096;
				break;
			}
		case 2:{
				rs = 8192;
				break;
			}
		default:{
				rs = 0;
				break;
			}
		}
		switch (sdccr_cs) {
		case 0:{
				cs = 256;
				break;
			}
		case 1:{
				cs = 512;
				break;
			}
		case 2:{
				cs = 1024;
				break;
			}
		case 3:{
				cs = 2048;
				break;
			}
		case 4:{
				cs = 4096;
				break;
			}
		default:{
				cs = 0;
				break;
			}
		}
		switch (sdccr_mw) {
		case 0:{
				mw = 8;
				break;
			}	/* 8 bytes = 64 bits */
		case 1:{
				mw = 4;
				break;
			}	/* 4 bytes = 32 bits */
		}
	}

	/*           bytes per chip    MB per chip          bank count */
	msize = (((rs * cs * mw) / (1024 * 1024)) * (bc));

	/* MVMCP -- bc hard coded to 4 from table 9.3.1     */
	/*          boad supports bc=2 but no way to detect */

	return (msize);
}

unsigned int __init
tx4938_get_mem_size(void)
{
	unsigned int c0;
	unsigned int c1;
	unsigned int c2;
	unsigned int c3;
	unsigned int total;

	/* MVMCP -- need #defs for these registers */
	c0 = tx4938_process_sdccr((u64 *) 0xff1f8000);
	c1 = tx4938_process_sdccr((u64 *) 0xff1f8008);
	c2 = tx4938_process_sdccr((u64 *) 0xff1f8010);
	c3 = tx4938_process_sdccr((u64 *) 0xff1f8018);
	total = c0 + c1 + c2 + c3;

	return (total);
}
