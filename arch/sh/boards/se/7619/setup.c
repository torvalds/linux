/*
 * arch/sh/boards/se/7619/setup.c
 *
 * Copyright (C) 2006 Yoshinori Sato
 *
 * Hitachi SH7619 SolutionEngine Support.
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <asm/se7619.h>
#include <asm/io.h>
#include <asm/machvec.h>

/*
 * The Machine Vector
 */

struct sh_machine_vector mv_se __initmv = {
	.mv_name		= "SolutionEngine",
	.mv_nr_irqs		= 108,
	.mv_inb			= se7619_inb,
	.mv_inw			= se7619_inw,
	.mv_inl			= se7619_inl,
	.mv_outb		= se7619_outb,
	.mv_outw		= se7619_outw,
	.mv_outl		= se7619_outl,

	.mv_inb_p		= se7619_inb_p,
	.mv_inw_p		= se7619_inw,
	.mv_inl_p		= se7619_inl,
	.mv_outb_p		= se7619_outb_p,
	.mv_outw_p		= se7619_outw,
	.mv_outl_p		= se7619_outl,

	.mv_insb		= se7619_insb,
	.mv_insw		= se7619_insw,
	.mv_insl		= se7619_insl,
	.mv_outsb		= se7619_outsb,
	.mv_outsw		= se7619_outsw,
	.mv_outsl		= se7619_outsl,
};
ALIAS_MV(se)
