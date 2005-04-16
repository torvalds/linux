/*
 * linux/arch/sh/kernel/mach_se.c
 *
 * Copyright (C) 2000 Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Machine vector for the Hitachi SolutionEngine
 */

#include <linux/config.h>
#include <linux/init.h>

#include <asm/machvec.h>
#include <asm/rtc.h>
#include <asm/machvec_init.h>

#include <asm/se/io.h>

void heartbeat_se(void);
void setup_se(void);
void init_se_IRQ(void);

/*
 * The Machine Vector
 */

struct sh_machine_vector mv_se __initmv = {
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

	.mv_isa_port2addr	= se_isa_port2addr,

	.mv_init_irq		= init_se_IRQ,
#ifdef CONFIG_HEARTBEAT
	.mv_heartbeat		= heartbeat_se,
#endif
};
ALIAS_MV(se)
