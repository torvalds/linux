/*
 * arch/sh/boards/se/7619/setup.c
 *
 * Copyright (C) 2006 Yoshinori Sato
 *
 * Hitachi SH7619 SolutionEngine Support.
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <asm/se7619.h>
#include <asm/machvec.h>

/*
 * The Machine Vector
 */

struct sh_machine_vector mv_se __initmv = {
	.mv_name		= "SolutionEngine",
	.mv_nr_irqs		= 108,
	.mv_inb			= se7619___inb,
	.mv_inw			= se7619___inw,
	.mv_inl			= se7619___inl,
	.mv_outb		= se7619___outb,
	.mv_outw		= se7619___outw,
	.mv_outl		= se7619___outl,

	.mv_inb_p		= se7619___inb_p,
	.mv_inw_p		= se7619___inw,
	.mv_inl_p		= se7619___inl,
	.mv_outb_p		= se7619___outb_p,
	.mv_outw_p		= se7619___outw,
	.mv_outl_p		= se7619___outl,

	.mv_insb		= se7619___insb,
	.mv_insw		= se7619___insw,
	.mv_insl		= se7619___insl,
	.mv_outsb		= se7619___outsb,
	.mv_outsw		= se7619___outsw,
	.mv_outsl		= se7619___outsl,
};
ALIAS_MV(se)
