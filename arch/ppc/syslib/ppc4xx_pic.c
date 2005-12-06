/*
 * arch/ppc/syslib/ppc4xx_pic.c
 *
 * Interrupt controller driver for PowerPC 4xx-based processors.
 *
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 * Copyright (c) 2004, 2005 Zultys Technologies
 *
 * Based on original code by
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *    Armin Custer <akuster@mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
*/
#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/stddef.h>

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/ppc4xx_pic.h>
#include <asm/machdep.h>

/* See comment in include/arch-ppc/ppc4xx_pic.h
 * for more info about these two variables
 */
extern struct ppc4xx_uic_settings ppc4xx_core_uic_cfg[NR_UICS]
    __attribute__ ((weak));
extern unsigned char ppc4xx_uic_ext_irq_cfg[] __attribute__ ((weak));

#define IRQ_MASK_UIC0(irq)		(1 << (31 - (irq)))
#define IRQ_MASK_UICx(irq)		(1 << (31 - ((irq) & 0x1f)))
#define IRQ_MASK_UIC1(irq)		IRQ_MASK_UICx(irq)
#define IRQ_MASK_UIC2(irq)		IRQ_MASK_UICx(irq)
#define IRQ_MASK_UIC3(irq)		IRQ_MASK_UICx(irq)

#define UIC_HANDLERS(n)							\
static void ppc4xx_uic##n##_enable(unsigned int irq)			\
{									\
	u32 mask = IRQ_MASK_UIC##n(irq);				\
	if (irq_desc[irq].status & IRQ_LEVEL)				\
		mtdcr(DCRN_UIC_SR(UIC##n), mask);			\
	ppc_cached_irq_mask[n] |= mask;					\
	mtdcr(DCRN_UIC_ER(UIC##n), ppc_cached_irq_mask[n]);		\
}									\
									\
static void ppc4xx_uic##n##_disable(unsigned int irq)			\
{									\
	ppc_cached_irq_mask[n] &= ~IRQ_MASK_UIC##n(irq);		\
	mtdcr(DCRN_UIC_ER(UIC##n), ppc_cached_irq_mask[n]);		\
	ACK_UIC##n##_PARENT						\
}									\
									\
static void ppc4xx_uic##n##_ack(unsigned int irq)			\
{									\
	u32 mask = IRQ_MASK_UIC##n(irq);				\
	ppc_cached_irq_mask[n] &= ~mask;				\
	mtdcr(DCRN_UIC_ER(UIC##n), ppc_cached_irq_mask[n]);		\
	mtdcr(DCRN_UIC_SR(UIC##n), mask);				\
	ACK_UIC##n##_PARENT						\
}									\
									\
static void ppc4xx_uic##n##_end(unsigned int irq)			\
{									\
	unsigned int status = irq_desc[irq].status;			\
	u32 mask = IRQ_MASK_UIC##n(irq);				\
	if (status & IRQ_LEVEL) {					\
		mtdcr(DCRN_UIC_SR(UIC##n), mask);			\
		ACK_UIC##n##_PARENT					\
	}								\
	if (!(status & (IRQ_DISABLED | IRQ_INPROGRESS))) {		\
		ppc_cached_irq_mask[n] |= mask;				\
		mtdcr(DCRN_UIC_ER(UIC##n), ppc_cached_irq_mask[n]);	\
	}								\
}

#define DECLARE_UIC(n)							\
{									\
	.typename 	= "UIC"#n,					\
	.enable 	= ppc4xx_uic##n##_enable,			\
	.disable 	= ppc4xx_uic##n##_disable,			\
	.ack 		= ppc4xx_uic##n##_ack,				\
	.end 		= ppc4xx_uic##n##_end,				\
}									\

#if NR_UICS == 4
#define ACK_UIC0_PARENT
#define ACK_UIC1_PARENT	mtdcr(DCRN_UIC_SR(UIC0), UIC0_UIC1NC);
#define ACK_UIC2_PARENT	mtdcr(DCRN_UIC_SR(UIC0), UIC0_UIC2NC);
#define ACK_UIC3_PARENT	mtdcr(DCRN_UIC_SR(UIC0), UIC0_UIC3NC);
UIC_HANDLERS(0);
UIC_HANDLERS(1);
UIC_HANDLERS(2);
UIC_HANDLERS(3);

static int ppc4xx_pic_get_irq(struct pt_regs *regs)
{
	u32 uic0 = mfdcr(DCRN_UIC_MSR(UIC0));
	if (uic0 & UIC0_UIC1NC)
		return 64 - ffs(mfdcr(DCRN_UIC_MSR(UIC1)));
	else if (uic0 & UIC0_UIC2NC)
		return 96 - ffs(mfdcr(DCRN_UIC_MSR(UIC2)));
	else if (uic0 & UIC0_UIC3NC)
		return 128 - ffs(mfdcr(DCRN_UIC_MSR(UIC3)));
	else
		return uic0 ? 32 - ffs(uic0) : -1;
}

static void __init ppc4xx_pic_impl_init(void)
{
	/* Enable cascade interrupts in UIC0 */
	ppc_cached_irq_mask[0] |= UIC0_UIC1NC | UIC0_UIC2NC | UIC0_UIC3NC;
	mtdcr(DCRN_UIC_SR(UIC0), UIC0_UIC1NC | UIC0_UIC2NC | UIC0_UIC3NC);
	mtdcr(DCRN_UIC_ER(UIC0), ppc_cached_irq_mask[0]);
}

#elif NR_UICS == 3
#define ACK_UIC0_PARENT	mtdcr(DCRN_UIC_SR(UICB), UICB_UIC0NC);
#define ACK_UIC1_PARENT	mtdcr(DCRN_UIC_SR(UICB), UICB_UIC1NC);
#define ACK_UIC2_PARENT	mtdcr(DCRN_UIC_SR(UICB), UICB_UIC2NC);
UIC_HANDLERS(0);
UIC_HANDLERS(1);
UIC_HANDLERS(2);

static int ppc4xx_pic_get_irq(struct pt_regs *regs)
{
	u32 uicb = mfdcr(DCRN_UIC_MSR(UICB));
	if (uicb & UICB_UIC0NC)
		return 32 - ffs(mfdcr(DCRN_UIC_MSR(UIC0)));
	else if (uicb & UICB_UIC1NC)
		return 64 - ffs(mfdcr(DCRN_UIC_MSR(UIC1)));
	else if (uicb & UICB_UIC2NC)
		return 96 - ffs(mfdcr(DCRN_UIC_MSR(UIC2)));
	else
		return -1;
}

static void __init ppc4xx_pic_impl_init(void)
{
#if defined(CONFIG_440GX)
	/* Disable 440GP compatibility mode if it was enabled in firmware */
	SDR_WRITE(DCRN_SDR_MFR, SDR_READ(DCRN_SDR_MFR) & ~DCRN_SDR_MFR_PCM);
#endif
	/* Configure Base UIC */
	mtdcr(DCRN_UIC_CR(UICB), 0);
	mtdcr(DCRN_UIC_TR(UICB), 0);
	mtdcr(DCRN_UIC_PR(UICB), 0xffffffff);
	mtdcr(DCRN_UIC_SR(UICB), 0xffffffff);
	mtdcr(DCRN_UIC_ER(UICB), UICB_UIC0NC | UICB_UIC1NC | UICB_UIC2NC);
}

#elif NR_UICS == 2
#define ACK_UIC0_PARENT
#define ACK_UIC1_PARENT	mtdcr(DCRN_UIC_SR(UIC0), UIC0_UIC1NC);
UIC_HANDLERS(0);
UIC_HANDLERS(1);

static int ppc4xx_pic_get_irq(struct pt_regs *regs)
{
	u32 uic0 = mfdcr(DCRN_UIC_MSR(UIC0));
	if (uic0 & UIC0_UIC1NC)
		return 64 - ffs(mfdcr(DCRN_UIC_MSR(UIC1)));
	else
		return uic0 ? 32 - ffs(uic0) : -1;
}

static void __init ppc4xx_pic_impl_init(void)
{
	/* Enable cascade interrupt in UIC0 */
	ppc_cached_irq_mask[0] |= UIC0_UIC1NC;
	mtdcr(DCRN_UIC_SR(UIC0), UIC0_UIC1NC);
	mtdcr(DCRN_UIC_ER(UIC0), ppc_cached_irq_mask[0]);
}

#elif NR_UICS == 1
#define ACK_UIC0_PARENT
UIC_HANDLERS(0);

static int ppc4xx_pic_get_irq(struct pt_regs *regs)
{
	u32 uic0 = mfdcr(DCRN_UIC_MSR(UIC0));
	return uic0 ? 32 - ffs(uic0) : -1;
}

static inline void ppc4xx_pic_impl_init(void)
{
}
#endif

static struct ppc4xx_uic_impl {
	struct hw_interrupt_type decl;
	int base;			/* Base DCR number */
} __uic[] = {
	{ .decl = DECLARE_UIC(0), .base = UIC0 },
#if NR_UICS > 1
	{ .decl = DECLARE_UIC(1), .base = UIC1 },
#if NR_UICS > 2
	{ .decl = DECLARE_UIC(2), .base = UIC2 },
#if NR_UICS > 3
	{ .decl = DECLARE_UIC(3), .base = UIC3 },
#endif
#endif
#endif
};

static inline int is_level_sensitive(int irq)
{
	u32 tr = mfdcr(DCRN_UIC_TR(__uic[irq >> 5].base));
	return (tr & IRQ_MASK_UICx(irq)) == 0;
}

void __init ppc4xx_pic_init(void)
{
	int i;
	unsigned char *eirqs = ppc4xx_uic_ext_irq_cfg;

	for (i = 0; i < NR_UICS; ++i) {
		int base = __uic[i].base;

		/* Disable everything by default */
		ppc_cached_irq_mask[i] = 0;
		mtdcr(DCRN_UIC_ER(base), 0);

		/* We don't use critical interrupts */
		mtdcr(DCRN_UIC_CR(base), 0);

		/* Configure polarity and triggering */
		if (ppc4xx_core_uic_cfg) {
			struct ppc4xx_uic_settings *p = ppc4xx_core_uic_cfg + i;
			u32 mask = p->ext_irq_mask;
			u32 pr = mfdcr(DCRN_UIC_PR(base)) & mask;
			u32 tr = mfdcr(DCRN_UIC_TR(base)) & mask;

			/* "Fixed" interrupts (on-chip devices) */
			pr |= p->polarity & ~mask;
			tr |= p->triggering & ~mask;

			/* Merge external IRQs settings if board port
			 * provided them
			 */
			if (eirqs && mask) {
				pr &= ~mask;
				tr &= ~mask;
				while (mask) {
					/* Extract current external IRQ mask */
					u32 eirq_mask = 1 << __ilog2(mask);

					if (!(*eirqs & IRQ_SENSE_LEVEL))
						tr |= eirq_mask;

					if (*eirqs & IRQ_POLARITY_POSITIVE)
						pr |= eirq_mask;

					mask &= ~eirq_mask;
					++eirqs;
				}
			}
			mtdcr(DCRN_UIC_PR(base), pr);
			mtdcr(DCRN_UIC_TR(base), tr);
		}

		/* ACK any pending interrupts to prevent false
		 * triggering after first enable
		 */
		mtdcr(DCRN_UIC_SR(base), 0xffffffff);
	}

	/* Perform optional implementation specific setup
	 * (e.g. enable cascade interrupts for multi-UIC configurations)
	 */
	ppc4xx_pic_impl_init();

	/* Attach low-level handlers */
	for (i = 0; i < (NR_UICS << 5); ++i) {
		irq_desc[i].handler = &__uic[i >> 5].decl;
		if (is_level_sensitive(i))
			irq_desc[i].status |= IRQ_LEVEL;
	}

	ppc_md.get_irq = ppc4xx_pic_get_irq;
}
