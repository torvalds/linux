/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  eseries-irq.h
 *
 *  Copyright (C) Ian Molton <spyro@f2s.com>
 */

#define ANGELX_IRQ_BASE (IRQ_BOARD_START+8)
#define IRQ_ANGELX(n) (ANGELX_IRQ_BASE + (n))

#define ANGELX_RDY0_IRQ IRQ_ANGELX(0)
#define ANGELX_ST0_IRQ  IRQ_ANGELX(1)
#define ANGELX_CD0_IRQ  IRQ_ANGELX(2)
#define ANGELX_RDY1_IRQ IRQ_ANGELX(3)
#define ANGELX_ST1_IRQ  IRQ_ANGELX(4)
#define ANGELX_CD1_IRQ  IRQ_ANGELX(5)

#define TMIO_IRQ_BASE (IRQ_BOARD_START+0)
#define IRQ_TMIO(n) (TMIO_IRQ_BASE + (n))

#define TMIO_SD_IRQ     IRQ_TMIO(1)
#define TMIO_USB_IRQ    IRQ_TMIO(2)

#define ESERIES_NR_IRQS	(IRQ_BOARD_START + 16)
