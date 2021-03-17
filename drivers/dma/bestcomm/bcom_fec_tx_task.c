// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bestcomm FEC TX task microcode
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 *
 * Automatically created based on BestCommAPI-2.2/code_dma/image_rtos1/dma_image.hex
 * on Tue Mar 22 11:19:29 2005 GMT
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

u32 bcom_fec_tx_task[] = {
	/* header */
	0x4243544b,
	0x2407070d,
	0x00000000,
	0x00000000,

	/* Task descriptors */
	0x8018001b, /* LCD: idx0 = var0; idx0 <= var0; idx0 += inc3 */
	0x60000005, /*   DRD2A: EU0=0 EU1=0 EU2=0 EU3=5 EXT init=0 WS=0 RS=0 */
	0x01ccfc0d, /*   DRD2B1: var7 = EU3(); EU3(*idx0,var13)  */
	0x8082a123, /* LCD: idx0 = var1, idx1 = var5; idx1 <= var4; idx0 += inc4, idx1 += inc3 */
	0x10801418, /*   DRD1A: var5 = var3; FN=0 MORE init=4 WS=0 RS=0 */
	0xf88103a4, /*   LCDEXT: idx2 = *idx1, idx3 = var2; idx2 < var14; idx2 += inc4, idx3 += inc4 */
	0x801a6024, /*   LCD: idx4 = var0; ; idx4 += inc4 */
	0x10001708, /*     DRD1A: var5 = idx1; FN=0 MORE init=0 WS=0 RS=0 */
	0x60140002, /*     DRD2A: EU0=0 EU1=0 EU2=0 EU3=2 EXT init=0 WS=2 RS=2 */
	0x0cccfccf, /*     DRD2B1: *idx3 = EU3(); EU3(*idx3,var15)  */
	0x991a002c, /*   LCD: idx2 = idx2, idx3 = idx4; idx2 once var0; idx2 += inc5, idx3 += inc4 */
	0x70000002, /*     DRD2A: EU0=0 EU1=0 EU2=0 EU3=2 EXT MORE init=0 WS=0 RS=0 */
	0x024cfc4d, /*     DRD2B1: var9 = EU3(); EU3(*idx1,var13)  */
	0x60000003, /*     DRD2A: EU0=0 EU1=0 EU2=0 EU3=3 EXT init=0 WS=0 RS=0 */
	0x0cccf247, /*     DRD2B1: *idx3 = EU3(); EU3(var9,var7)  */
	0x80004000, /*   LCDEXT: idx2 = 0x00000000; ; */
	0xb8c80029, /*   LCD: idx3 = *(idx1 + var0000001a); idx3 once var0; idx3 += inc5 */
	0x70000002, /*     DRD2A: EU0=0 EU1=0 EU2=0 EU3=2 EXT MORE init=0 WS=0 RS=0 */
	0x088cf8d1, /*     DRD2B1: idx2 = EU3(); EU3(idx3,var17)  */
	0x00002f10, /*     DRD1A: var11 = idx2; FN=0 init=0 WS=0 RS=0 */
	0x99198432, /*   LCD: idx2 = idx2, idx3 = idx3; idx2 > var16; idx2 += inc6, idx3 += inc2 */
	0x008ac398, /*     DRD1A: *idx0 = *idx3; FN=0 init=4 WS=1 RS=1 */
	0x80004000, /*   LCDEXT: idx2 = 0x00000000; ; */
	0x9999802d, /*   LCD: idx3 = idx3; idx3 once var0; idx3 += inc5 */
	0x70000002, /*     DRD2A: EU0=0 EU1=0 EU2=0 EU3=2 EXT MORE init=0 WS=0 RS=0 */
	0x048cfc53, /*     DRD2B1: var18 = EU3(); EU3(*idx1,var19)  */
	0x60000008, /*     DRD2A: EU0=0 EU1=0 EU2=0 EU3=8 EXT init=0 WS=0 RS=0 */
	0x088cf48b, /*     DRD2B1: idx2 = EU3(); EU3(var18,var11)  */
	0x99198481, /*   LCD: idx2 = idx2, idx3 = idx3; idx2 > var18; idx2 += inc0, idx3 += inc1 */
	0x009ec398, /*     DRD1A: *idx0 = *idx3; FN=0 init=4 WS=3 RS=3 */
	0x991983b2, /*   LCD: idx2 = idx2, idx3 = idx3; idx2 > var14; idx2 += inc6, idx3 += inc2 */
	0x088ac398, /*     DRD1A: *idx0 = *idx3; FN=0 TFD init=4 WS=1 RS=1 */
	0x9919002d, /*   LCD: idx2 = idx2; idx2 once var0; idx2 += inc5 */
	0x60000005, /*     DRD2A: EU0=0 EU1=0 EU2=0 EU3=5 EXT init=0 WS=0 RS=0 */
	0x0c4cf88e, /*     DRD2B1: *idx1 = EU3(); EU3(idx2,var14)  */
	0x000001f8, /*   NOP */

	/* VAR[13]-VAR[19] */
	0x0c000000,
	0x40000000,
	0x7fff7fff,
	0x00000000,
	0x00000003,
	0x40000004,
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

