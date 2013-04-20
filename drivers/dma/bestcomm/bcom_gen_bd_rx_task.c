/*
 * Bestcomm GenBD RX task microcode
 *
 * Copyright (C) 2006 AppSpec Computer Technologies Corp.
 *                    Jeff Gibbons <jeff.gibbons@appspec.com>
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Based on BestCommAPI-2.2/code_dma/image_rtos1/dma_image.hex
 * on Tue Mar 4 10:14:12 2006 GMT
 *
 */

#include <asm/types.h>

/*
 * The header consists of the following fields:
 *	u32	magic;
 *	u8	desc_size;
 *	u8	var_size;
 *	u8	inc_size;
 *	u8	first_var;
 *	u8	reserved[8];
 *
 * The size fields contain the number of 32-bit words.
 */

u32 bcom_gen_bd_rx_task[] = {
	/* header */
	0x4243544b,
	0x0d020409,
	0x00000000,
	0x00000000,

	/* Task descriptors */
	0x808220da, /* LCD: idx0 = var1, idx1 = var4; idx1 <= var3; idx0 += inc3, idx1 += inc2 */
	0x13e01010, /*   DRD1A: var4 = var2; FN=0 MORE init=31 WS=0 RS=0 */
	0xb880025b, /*   LCD: idx2 = *idx1, idx3 = var0; idx2 < var9; idx2 += inc3, idx3 += inc3 */
	0x10001308, /*     DRD1A: var4 = idx1; FN=0 MORE init=0 WS=0 RS=0 */
	0x60140002, /*     DRD2A: EU0=0 EU1=0 EU2=0 EU3=2 EXT init=0 WS=2 RS=2 */
	0x0cccfcca, /*     DRD2B1: *idx3 = EU3(); EU3(*idx3,var10)  */
	0xd9190240, /*   LCDEXT: idx2 = idx2; idx2 > var9; idx2 += inc0 */
	0xb8c5e009, /*   LCD: idx3 = *(idx1 + var00000015); ; idx3 += inc1 */
	0x07fecf80, /*     DRD1A: *idx3 = *idx0; FN=0 INT init=31 WS=3 RS=3 */
	0x99190024, /*   LCD: idx2 = idx2; idx2 once var0; idx2 += inc4 */
	0x60000005, /*     DRD2A: EU0=0 EU1=0 EU2=0 EU3=5 EXT init=0 WS=0 RS=0 */
	0x0c4cf889, /*     DRD2B1: *idx1 = EU3(); EU3(idx2,var9)  */
	0x000001f8, /*   NOP */

	/* VAR[9]-VAR[10] */
	0x40000000,
	0x7fff7fff,

	/* INC[0]-INC[3] */
	0x40000000,
	0xe0000000,
	0xa0000008,
	0x20000000,
};

