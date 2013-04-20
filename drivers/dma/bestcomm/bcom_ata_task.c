/*
 * Bestcomm ATA task microcode
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Created based on bestcom/code_dma/image_rtos1/dma_image.hex
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

u32 bcom_ata_task[] = {
	/* header */
	0x4243544b,
	0x0e060709,
	0x00000000,
	0x00000000,

	/* Task descriptors */
	0x8198009b, /* LCD: idx0 = var3; idx0 <= var2; idx0 += inc3 */
	0x13e00c08, /*   DRD1A: var3 = var1; FN=0 MORE init=31 WS=0 RS=0 */
	0xb8000264, /*   LCD: idx1 = *idx0, idx2 = var0; idx1 < var9; idx1 += inc4, idx2 += inc4 */
	0x10000f00, /*     DRD1A: var3 = idx0; FN=0 MORE init=0 WS=0 RS=0 */
	0x60140002, /*     DRD2A: EU0=0 EU1=0 EU2=0 EU3=2 EXT init=0 WS=2 RS=2 */
	0x0c8cfc8a, /*     DRD2B1: *idx2 = EU3(); EU3(*idx2,var10)  */
	0xd8988240, /*   LCDEXT: idx1 = idx1; idx1 > var9; idx1 += inc0 */
	0xf845e011, /*   LCDEXT: idx2 = *(idx0 + var00000015); ; idx2 += inc2 */
	0xb845e00a, /*   LCD: idx3 = *(idx0 + var00000019); ; idx3 += inc1 */
	0x0bfecf90, /*     DRD1A: *idx3 = *idx2; FN=0 TFD init=31 WS=3 RS=3 */
	0x9898802d, /*   LCD: idx1 = idx1; idx1 once var0; idx1 += inc5 */
	0x64000005, /*     DRD2A: EU0=0 EU1=0 EU2=0 EU3=5 INT EXT init=0 WS=0 RS=0 */
	0x0c0cf849, /*     DRD2B1: *idx0 = EU3(); EU3(idx1,var9)  */
	0x000001f8, /* NOP */

	/* VAR[9]-VAR[14] */
	0x40000000,
	0x7fff7fff,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,

	/* INC[0]-INC[6] */
	0x40000000,
	0xe0000000,
	0xe0000000,
	0xa000000c,
	0x20000000,
	0x00000000,
	0x00000000,
};

