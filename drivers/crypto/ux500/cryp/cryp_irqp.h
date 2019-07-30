/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Shujuan Chen <shujuan.chen@stericsson.com> for ST-Ericsson.
 * Author: Jonas Linde <jonas.linde@stericsson.com> for ST-Ericsson.
 * Author: Joakim Bech <joakim.xx.bech@stericsson.com> for ST-Ericsson.
 * Author: Berne Hebark <berne.herbark@stericsson.com> for ST-Ericsson.
 * Author: Niklas Hernaeus <niklas.hernaeus@stericsson.com> for ST-Ericsson.
 */

#ifndef __CRYP_IRQP_H_
#define __CRYP_IRQP_H_

#include "cryp_irq.h"

/**
 *
 * CRYP Registers - Offset mapping
 *     +-----------------+
 * 00h | CRYP_CR         |  Configuration register
 *     +-----------------+
 * 04h | CRYP_SR         |  Status register
 *     +-----------------+
 * 08h | CRYP_DIN        |  Data In register
 *     +-----------------+
 * 0ch | CRYP_DOUT       |  Data out register
 *     +-----------------+
 * 10h | CRYP_DMACR      |  DMA control register
 *     +-----------------+
 * 14h | CRYP_IMSC       |  IMSC
 *     +-----------------+
 * 18h | CRYP_RIS        |  Raw interrupt status
 *     +-----------------+
 * 1ch | CRYP_MIS        |  Masked interrupt status.
 *     +-----------------+
 *       Key registers
 *       IVR registers
 *       Peripheral
 *       Cell IDs
 *
 *       Refer data structure for other register map
 */

/**
 * struct cryp_register
 * @cr			- Configuration register
 * @status		- Status register
 * @din			- Data input register
 * @din_size		- Data input size register
 * @dout		- Data output register
 * @dout_size		- Data output size register
 * @dmacr		- Dma control register
 * @imsc		- Interrupt mask set/clear register
 * @ris			- Raw interrupt status
 * @mis			- Masked interrupt statu register
 * @key_1_l		- Key register 1 L
 * @key_1_r		- Key register 1 R
 * @key_2_l		- Key register 2 L
 * @key_2_r		- Key register 2 R
 * @key_3_l		- Key register 3 L
 * @key_3_r		- Key register 3 R
 * @key_4_l		- Key register 4 L
 * @key_4_r		- Key register 4 R
 * @init_vect_0_l	- init vector 0 L
 * @init_vect_0_r	- init vector 0 R
 * @init_vect_1_l	- init vector 1 L
 * @init_vect_1_r	- init vector 1 R
 * @cryp_unused1	- unused registers
 * @itcr		- Integration test control register
 * @itip		- Integration test input register
 * @itop		- Integration test output register
 * @cryp_unused2	- unused registers
 * @periphId0		- FE0 CRYP Peripheral Identication Register
 * @periphId1		- FE4
 * @periphId2		- FE8
 * @periphId3		- FEC
 * @pcellId0		- FF0  CRYP PCell Identication Register
 * @pcellId1		- FF4
 * @pcellId2		- FF8
 * @pcellId3		- FFC
 */
struct cryp_register {
	u32 cr;			/* Configuration register   */
	u32 sr;			/* Status register          */
	u32 din;		/* Data input register      */
	u32 din_size;		/* Data input size register */
	u32 dout;		/* Data output register     */
	u32 dout_size;		/* Data output size register */
	u32 dmacr;		/* Dma control register     */
	u32 imsc;		/* Interrupt mask set/clear register */
	u32 ris;		/* Raw interrupt status             */
	u32 mis;		/* Masked interrupt statu register  */

	u32 key_1_l;		/*Key register 1 L */
	u32 key_1_r;		/*Key register 1 R */
	u32 key_2_l;		/*Key register 2 L */
	u32 key_2_r;		/*Key register 2 R */
	u32 key_3_l;		/*Key register 3 L */
	u32 key_3_r;		/*Key register 3 R */
	u32 key_4_l;		/*Key register 4 L */
	u32 key_4_r;		/*Key register 4 R */

	u32 init_vect_0_l;	/*init vector 0 L */
	u32 init_vect_0_r;	/*init vector 0 R */
	u32 init_vect_1_l;	/*init vector 1 L */
	u32 init_vect_1_r;	/*init vector 1 R */

	u32 cryp_unused1[(0x80 - 0x58) / sizeof(u32)];	/* unused registers */
	u32 itcr;		/*Integration test control register */
	u32 itip;		/*Integration test input register */
	u32 itop;		/*Integration test output register */
	u32 cryp_unused2[(0xFE0 - 0x8C) / sizeof(u32)];	/* unused registers */

	u32 periphId0;		/* FE0  CRYP Peripheral Identication Register */
	u32 periphId1;		/* FE4 */
	u32 periphId2;		/* FE8 */
	u32 periphId3;		/* FEC */

	u32 pcellId0;		/* FF0  CRYP PCell Identication Register */
	u32 pcellId1;		/* FF4 */
	u32 pcellId2;		/* FF8 */
	u32 pcellId3;		/* FFC */
};

#endif
