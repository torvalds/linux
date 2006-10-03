/* $Id: setup.c,v 1.1.2.4 2002/03/02 21:57:07 lethal Exp $
 *
 * linux/arch/sh/boards/se/770x/setup.c
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * Hitachi SolutionEngine Support.
 *
 */
#include <linux/init.h>
#include <asm/machvec.h>
#include <asm/se.h>
#include <asm/io.h>
#include <asm/smc37c93x.h>

void heartbeat_se(void);
void init_se_IRQ(void);

/*
 * Configure the Super I/O chip
 */
static void __init smsc_config(int index, int data)
{
	outb_p(index, INDEX_PORT);
	outb_p(data, DATA_PORT);
}

/* XXX: Another candidate for a more generic cchip machine vector */
static void __init smsc_setup(char **cmdline_p)
{
	outb_p(CONFIG_ENTER, CONFIG_PORT);
	outb_p(CONFIG_ENTER, CONFIG_PORT);

	/* FDC */
	smsc_config(CURRENT_LDN_INDEX, LDN_FDC);
	smsc_config(ACTIVATE_INDEX, 0x01);
	smsc_config(IRQ_SELECT_INDEX, 6); /* IRQ6 */

	/* IDE1 */
	smsc_config(CURRENT_LDN_INDEX, LDN_IDE1);
	smsc_config(ACTIVATE_INDEX, 0x01);
	smsc_config(IRQ_SELECT_INDEX, 14); /* IRQ14 */

	/* AUXIO (GPIO): to use IDE1 */
	smsc_config(CURRENT_LDN_INDEX, LDN_AUXIO);
	smsc_config(GPIO46_INDEX, 0x00); /* nIOROP */
	smsc_config(GPIO47_INDEX, 0x00); /* nIOWOP */

	/* COM1 */
	smsc_config(CURRENT_LDN_INDEX, LDN_COM1);
	smsc_config(ACTIVATE_INDEX, 0x01);
	smsc_config(IO_BASE_HI_INDEX, 0x03);
	smsc_config(IO_BASE_LO_INDEX, 0xf8);
	smsc_config(IRQ_SELECT_INDEX, 4); /* IRQ4 */

	/* COM2 */
	smsc_config(CURRENT_LDN_INDEX, LDN_COM2);
	smsc_config(ACTIVATE_INDEX, 0x01);
	smsc_config(IO_BASE_HI_INDEX, 0x02);
	smsc_config(IO_BASE_LO_INDEX, 0xf8);
	smsc_config(IRQ_SELECT_INDEX, 3); /* IRQ3 */

	/* RTC */
	smsc_config(CURRENT_LDN_INDEX, LDN_RTC);
	smsc_config(ACTIVATE_INDEX, 0x01);
	smsc_config(IRQ_SELECT_INDEX, 8); /* IRQ8 */

	/* XXX: PARPORT, KBD, and MOUSE will come here... */
	outb_p(CONFIG_EXIT, CONFIG_PORT);
}

/*
 * The Machine Vector
 */
struct sh_machine_vector mv_se __initmv = {
	.mv_name		= "SolutionEngine",
	.mv_setup		= smsc_setup,
#if defined(CONFIG_CPU_SH4)
	.mv_nr_irqs		= 48,
#elif defined(CONFIG_CPU_SUBTYPE_SH7708)
	.mv_nr_irqs		= 32,
#elif defined(CONFIG_CPU_SUBTYPE_SH7709)
	.mv_nr_irqs		= 61,
#elif defined(CONFIG_CPU_SUBTYPE_SH7705)
	.mv_nr_irqs		= 86,
#endif

	.mv_inb			= se_inb,
	.mv_inw			= se_inw,
	.mv_inl			= se_inl,
	.mv_outb		= se_outb,
	.mv_outw		= se_outw,
	.mv_outl		= se_outl,

	.mv_inb_p		= se_inb_p,
	.mv_inw_p		= se_inw,
	.mv_inl_p		= se_inl,
	.mv_outb_p		= se_outb_p,
	.mv_outw_p		= se_outw,
	.mv_outl_p		= se_outl,

	.mv_insb		= se_insb,
	.mv_insw		= se_insw,
	.mv_insl		= se_insl,
	.mv_outsb		= se_outsb,
	.mv_outsw		= se_outsw,
	.mv_outsl		= se_outsl,

	.mv_init_irq		= init_se_IRQ,
#ifdef CONFIG_HEARTBEAT
	.mv_heartbeat		= heartbeat_se,
#endif
};
ALIAS_MV(se)
