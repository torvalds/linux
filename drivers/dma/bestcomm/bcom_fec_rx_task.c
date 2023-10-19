// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bestcomm FEC RX task microcode
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 *
 * Automatically created based on BestCommAPI-2.2/code_dma/image_rtos1/dma_image.hex
 * on Tue Mar 22 11:19:38 2005 GMT
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

u32 bcom_fec_rx_task[] = {
	/* header */
	0x4243544b,
	0x18060709,
	0x00000000,
	0x00000000,

	/* Task descriptors */
	0x808220e3, /* LCD: idx0 = var1, idx1 = var4; idx1 <= var3; idx0 += inc4, idx1 += inc3 */
	0x10601010, /*   DRD1A: var4 = var2; FN=0 MORE init=3 WS=0 RS=0 */
	0xb8800264, /*   LCD: idx2 = *idx1, idx3 = var0; idx2 < var9; idx2 += inc4, idx3 += inc4 */
	0x10001308, /*     DRD1A: var4 = idx1; FN=0 MORE init=0 WS=0 RS=0 */
	0x60140002, /*     DRD2A: EU0=0 EU1=0 EU2=0 EU3=2 EXT init=0 WS=2 RS=2 */
	0x0cccfcca, /*     DRD2B1: *idx3 = EU3(); EU3(*idx3,var10)  */
	0x80004000, /*   LCDEXT: idx2 = 0x00000000; ; */
	0xb8c58029, /*   LCD: idx3 = *(idx1 + var00000015); idx3 once var0; idx3 += inc5 */
	0x60000002, /*     DRD2A: EU0=0 EU1=0 EU2=0 EU3=2 EXT init=0 WS=0 RS=0 */
	0x088cf8cc, /*     DRD2B1: idx2 = EU3(); EU3(idx3,var12)  */
	0x991982f2, /*   LCD: idx2 = idx2, idx3 = idx3; idx2 > var11; idx2 += inc6, idx3 += inc2 */
	0x006acf80, /*     DRD1A: *idx3 = *idx0; FN=0 init=3 WS=1 RS=1 */
	0x80004000, /*   LCDEXT: idx2 = 0x00000000; ; */
	0x9999802d, /*   LCD: idx3 = idx3; idx3 once var0; idx3 += inc5 */
	0x70000002, /*     DRD2A: EU0=0 EU1=0 EU2=0 EU3=2 EXT MORE init=0 WS=0 RS=0 */
	0x034cfc4e, /*     DRD2B1: var13 = EU3(); EU3(*idx1,var14)  */
	0x00008868, /*     DRD1A: idx2 = var13; FN=0 init=0 WS=0 RS=0 */
	0x99198341, /*   LCD: idx2 = idx2, idx3 = idx3; idx2 > var13; idx2 += inc0, idx3 += inc1 */
	0x007ecf80, /*     DRD1A: *idx3 = *idx0; FN=0 init=3 WS=3 RS=3 */
	0x99198272, /*   LCD: idx2 = idx2, idx3 = idx3; idx2 > var9; idx2 += inc6, idx3 += inc2 */
	0x046acf80, /*     DRD1A: *idx3 = *idx0; FN=0 INT init=3 WS=1 RS=1 */
	0x9819002d, /*   LCD: idx2 = idx0; idx2 once var0; idx2 += inc5 */
	0x0060c790, /*     DRD1A: *idx1 = *idx2; FN=0 init=3 WS=0 RS=0 */
	0x000001f8, /*   NOP */

	/* VAR[9]-VAR[14] */
	0x40000000,
	0x7fff7fff,
	0x00000000,
	0x00000003,
	0x40000008,
	0x43ffffff,

	/* INC[0]-INC[6] */
	0x40000000,
	0xe0000000,
	0xe0000000,
	0xa0000008,
	0x20000000,
	0x00000000,
	0x4000ffff,
};

