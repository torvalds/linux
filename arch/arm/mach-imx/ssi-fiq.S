/*
 *  Copyright (C) 2009 Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/export.h>

/*
 * r8  = bit 0-15: tx offset, bit 16-31: tx buffer size
 * r9  = bit 0-15: rx offset, bit 16-31: rx buffer size
 */

#define SSI_STX0	0x00
#define SSI_SRX0	0x08
#define SSI_SISR	0x14
#define SSI_SIER	0x18
#define SSI_SACNT	0x38

#define SSI_SACNT_AC97EN	(1 << 0)

#define SSI_SIER_TFE0_EN	(1 << 0)
#define SSI_SISR_TFE0		(1 << 0)
#define SSI_SISR_RFF0		(1 << 2)
#define SSI_SIER_RFF0_EN	(1 << 2)

		.text
		.global	imx_ssi_fiq_start
		.global	imx_ssi_fiq_end
		.global imx_ssi_fiq_base
		.global imx_ssi_fiq_rx_buffer
		.global imx_ssi_fiq_tx_buffer

/*
 * imx_ssi_fiq_start is _intentionally_ not marked as a function symbol
 * using ENDPROC().  imx_ssi_fiq_start and imx_ssi_fiq_end are used to
 * mark the function body so that it can be copied to the FIQ vector in
 * the vectors page.  imx_ssi_fiq_start should only be called as the result
 * of an FIQ: calling it directly will not work.
 */
imx_ssi_fiq_start:
		ldr r12, .L_imx_ssi_fiq_base

		/* TX */
		ldr r13, .L_imx_ssi_fiq_tx_buffer

		/* shall we send? */
		ldr r11, [r12, #SSI_SIER]
		tst r11, #SSI_SIER_TFE0_EN
		beq 1f

		/* TX FIFO empty? */
		ldr r11, [r12, #SSI_SISR]
		tst r11, #SSI_SISR_TFE0
		beq 1f

		mov r10, #0x10000
		sub r10, #1
		and r10, r10, r8	/* r10: current buffer offset */

		add r13, r13, r10

		ldrh r11, [r13]
		strh r11, [r12, #SSI_STX0]

		ldrh r11, [r13, #2]
		strh r11, [r12, #SSI_STX0]

		ldrh r11, [r13, #4]
		strh r11, [r12, #SSI_STX0]

		ldrh r11, [r13, #6]
		strh r11, [r12, #SSI_STX0]

		add r10, #8
		lsr r11, r8, #16	/* r11: buffer size */
		cmp r10, r11
		lslgt r8, r11, #16
		addle r8, #8
1:
		/* RX */

		/* shall we receive? */
		ldr r11, [r12, #SSI_SIER]
		tst r11, #SSI_SIER_RFF0_EN
		beq 1f

		/* RX FIFO full? */
		ldr r11, [r12, #SSI_SISR]
		tst r11, #SSI_SISR_RFF0
		beq 1f

		ldr r13, .L_imx_ssi_fiq_rx_buffer

		mov r10, #0x10000
		sub r10, #1
		and r10, r10, r9	/* r10: current buffer offset */

		add r13, r13, r10

		ldr r11, [r12, #SSI_SACNT]
		tst r11, #SSI_SACNT_AC97EN

		ldr r11, [r12, #SSI_SRX0]
		strh r11, [r13]

		ldr r11, [r12, #SSI_SRX0]
		strh r11, [r13, #2]

		/* dummy read to skip slot 12 */
		ldrne r11, [r12, #SSI_SRX0]

		ldr r11, [r12, #SSI_SRX0]
		strh r11, [r13, #4]

		ldr r11, [r12, #SSI_SRX0]
		strh r11, [r13, #6]

		/* dummy read to skip slot 12 */
		ldrne r11, [r12, #SSI_SRX0]

		add r10, #8
		lsr r11, r9, #16	/* r11: buffer size */
		cmp r10, r11
		lslgt r9, r11, #16
		addle r9, #8

1:
		@ return from FIQ
		subs	pc, lr, #4

		.align
.L_imx_ssi_fiq_base:
imx_ssi_fiq_base:
		.word 0x0
.L_imx_ssi_fiq_rx_buffer:
imx_ssi_fiq_rx_buffer:
		.word 0x0
.L_imx_ssi_fiq_tx_buffer:
imx_ssi_fiq_tx_buffer:
		.word 0x0
.L_imx_ssi_fiq_end:
imx_ssi_fiq_end:
EXPORT_SYMBOL(imx_ssi_fiq_tx_buffer)
EXPORT_SYMBOL(imx_ssi_fiq_rx_buffer)
EXPORT_SYMBOL(imx_ssi_fiq_start)
EXPORT_SYMBOL(imx_ssi_fiq_end)
EXPORT_SYMBOL(imx_ssi_fiq_base)
