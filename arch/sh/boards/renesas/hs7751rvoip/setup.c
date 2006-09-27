/*
 * linux/arch/sh/kernel/setup_hs7751rvoip.c
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * Renesas Technology Sales HS7751RVoIP Support.
 *
 * Modified for HS7751RVoIP by
 * Atom Create Engineering Co., Ltd. 2002.
 * Lineo uSolutions, Inc. 2003.
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/pm.h>
#include <asm/hs7751rvoip/hs7751rvoip.h>
#include <asm/hs7751rvoip/io.h>
#include <asm/io.h>
#include <asm/machvec.h>
#include <asm/rtc.h>
#include <asm/irq.h>

unsigned int debug_counter;

static void __init hs7751rvoip_init_irq(void)
{
#if defined(CONFIG_HS7751RVOIP_CODEC)
	make_ipr_irq(DMTE0_IRQ, DMA_IPR_ADDR, DMA_IPR_POS, DMA_PRIORITY);
	make_ipr_irq(DMTE1_IRQ, DMA_IPR_ADDR, DMA_IPR_POS, DMA_PRIORITY);
#endif

	init_hs7751rvoip_IRQ();
}

struct sh_machine_vector mv_hs7751rvoip __initmv = {
	.mv_nr_irqs		= 72,

	.mv_inb			= hs7751rvoip_inb,
	.mv_inw			= hs7751rvoip_inw,
	.mv_inl			= hs7751rvoip_inl,
	.mv_outb		= hs7751rvoip_outb,
	.mv_outw		= hs7751rvoip_outw,
	.mv_outl		= hs7751rvoip_outl,

	.mv_inb_p		= hs7751rvoip_inb_p,
	.mv_inw_p		= hs7751rvoip_inw,
	.mv_inl_p		= hs7751rvoip_inl,
	.mv_outb_p		= hs7751rvoip_outb_p,
	.mv_outw_p		= hs7751rvoip_outw,
	.mv_outl_p		= hs7751rvoip_outl,

	.mv_insb		= hs7751rvoip_insb,
	.mv_insw		= hs7751rvoip_insw,
	.mv_insl		= hs7751rvoip_insl,
	.mv_outsb		= hs7751rvoip_outsb,
	.mv_outsw		= hs7751rvoip_outsw,
	.mv_outsl		= hs7751rvoip_outsl,

	.mv_ioremap		= hs7751rvoip_ioremap,
	.mv_isa_port2addr	= hs7751rvoip_isa_port2addr,
	.mv_init_irq		= hs7751rvoip_init_irq,
};
ALIAS_MV(hs7751rvoip)

const char *get_system_type(void)
{
	return "HS7751RVoIP";
}

static void hs7751rvoip_power_off(void)
{
	ctrl_outw(ctrl_inw(PA_OUTPORTR) & 0xffdf, PA_OUTPORTR);
}

/*
 * Initialize the board
 */
void __init platform_setup(void)
{
	printk(KERN_INFO "Renesas Technology Sales HS7751RVoIP-2 support.\n");
	ctrl_outb(0xf0, PA_OUTPORTR);
	pm_power_off = hs7751rvoip_power_off;
	debug_counter = 0;
}

void *area5_io8_base;
void *area6_io8_base;
void *area5_io16_base;
void *area6_io16_base;

static int __init hs7751rvoip_cf_init(void)
{
	pgprot_t prot;
	unsigned long paddrbase;

	/* open I/O area window */
	paddrbase = virt_to_phys((void *)(PA_AREA5_IO+0x00000800));
	prot = PAGE_KERNEL_PCC(1, _PAGE_PCC_COM16);
	area5_io16_base = p3_ioremap(paddrbase, PAGE_SIZE, prot.pgprot);
	if (!area5_io16_base) {
		printk("allocate_cf_area : can't open CF I/O window!\n");
		return -ENOMEM;
	}

	/* XXX : do we need attribute and common-memory area also? */

	paddrbase = virt_to_phys((void *)PA_AREA6_IO);
#if defined(CONFIG_HS7751RVOIP_CODEC)
	prot = PAGE_KERNEL_PCC(0, _PAGE_PCC_COM8);
#else
	prot = PAGE_KERNEL_PCC(0, _PAGE_PCC_IO8);
#endif
	area6_io8_base = p3_ioremap(paddrbase, PAGE_SIZE, prot.pgprot);
	if (!area6_io8_base) {
		printk("allocate_cf_area : can't open CODEC I/O 8bit window!\n");
		return -ENOMEM;
	}
	prot = PAGE_KERNEL_PCC(0, _PAGE_PCC_IO16);
	area6_io16_base = p3_ioremap(paddrbase, PAGE_SIZE, prot.pgprot);
	if (!area6_io16_base) {
		printk("allocate_cf_area : can't open CODEC I/O 16bit window!\n");
		return -ENOMEM;
	}

	return 0;
}

__initcall(hs7751rvoip_cf_init);
