/*
 * linux/arch/sh/boards/se/7300/setup.c
 *
 * Copyright (C) 2003 Takashi Kusuda <kusuda-takashi@hitachi-ul.co.jp>
 *
 * SH-Mobile SolutionEngine 7300 Support.
 *
 */

#include <linux/init.h>
#include <asm/machvec.h>
#include <asm/se7300.h>

void heartbeat_7300se(void);
void init_7300se_IRQ(void);

/*
 * The Machine Vector
 */
struct sh_machine_vector mv_7300se __initmv = {
	.mv_name = "SolutionEngine 7300",
	.mv_nr_irqs = 109,
	.mv_inb = sh7300se_inb,
	.mv_inw = sh7300se_inw,
	.mv_inl = sh7300se_inl,
	.mv_outb = sh7300se_outb,
	.mv_outw = sh7300se_outw,
	.mv_outl = sh7300se_outl,

	.mv_inb_p = sh7300se_inb_p,
	.mv_inw_p = sh7300se_inw,
	.mv_inl_p = sh7300se_inl,
	.mv_outb_p = sh7300se_outb_p,
	.mv_outw_p = sh7300se_outw,
	.mv_outl_p = sh7300se_outl,

	.mv_insb = sh7300se_insb,
	.mv_insw = sh7300se_insw,
	.mv_insl = sh7300se_insl,
	.mv_outsb = sh7300se_outsb,
	.mv_outsw = sh7300se_outsw,
	.mv_outsl = sh7300se_outsl,

	.mv_init_irq = init_7300se_IRQ,
#ifdef CONFIG_HEARTBEAT
	.mv_heartbeat = heartbeat_7300se,
#endif
};
ALIAS_MV(7300se)
