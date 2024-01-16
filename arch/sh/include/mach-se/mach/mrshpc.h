/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MACH_SE_MRSHPC_H
#define __MACH_SE_MRSHPC_H

#include <linux/io.h>

static inline void __init mrshpc_setup_windows(void)
{
	if ((__raw_readw(MRSHPC_CSR) & 0x000c) != 0)
		return;	/* Not detected */

	if ((__raw_readw(MRSHPC_CSR) & 0x0080) == 0) {
		__raw_writew(0x0674, MRSHPC_CPWCR); /* Card Vcc is 3.3v? */
	} else {
		__raw_writew(0x0678, MRSHPC_CPWCR); /* Card Vcc is 5V */
	}

	/*
	 *  PC-Card window open
	 *  flag == COMMON/ATTRIBUTE/IO
	 */
	/* common window open */
	__raw_writew(0x8a84, MRSHPC_MW0CR1);
	if((__raw_readw(MRSHPC_CSR) & 0x4000) != 0)
		/* common mode & bus width 16bit SWAP = 1*/
		__raw_writew(0x0b00, MRSHPC_MW0CR2);
	else
		/* common mode & bus width 16bit SWAP = 0*/
		__raw_writew(0x0300, MRSHPC_MW0CR2);

	/* attribute window open */
	__raw_writew(0x8a85, MRSHPC_MW1CR1);
	if ((__raw_readw(MRSHPC_CSR) & 0x4000) != 0)
		/* attribute mode & bus width 16bit SWAP = 1*/
		__raw_writew(0x0a00, MRSHPC_MW1CR2);
	else
		/* attribute mode & bus width 16bit SWAP = 0*/
		__raw_writew(0x0200, MRSHPC_MW1CR2);

	/* I/O window open */
	__raw_writew(0x8a86, MRSHPC_IOWCR1);
	__raw_writew(0x0008, MRSHPC_CDCR);	 /* I/O card mode */
	if ((__raw_readw(MRSHPC_CSR) & 0x4000) != 0)
		__raw_writew(0x0a00, MRSHPC_IOWCR2); /* bus width 16bit SWAP = 1*/
	else
		__raw_writew(0x0200, MRSHPC_IOWCR2); /* bus width 16bit SWAP = 0*/

	__raw_writew(0x2000, MRSHPC_ICR);
	__raw_writeb(0x00, PA_MRSHPC_MW2 + 0x206);
	__raw_writeb(0x42, PA_MRSHPC_MW2 + 0x200);
}

#endif /* __MACH_SE_MRSHPC_H */
