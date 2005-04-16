/*
 * linux/arch/sh/overdrive/mach.c
 *
 * Copyright (C) 2000 Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Machine vector for the STMicroelectronics Overdrive
 */

#include <linux/init.h>

#include <asm/machvec.h>
#include <asm/rtc.h>
#include <asm/machvec_init.h>

#include <asm/io_unknown.h>
#include <asm/io_generic.h>
#include <asm/overdrive/io.h>

void heartbeat_od(void);
void init_overdrive_irq(void);
void galileo_pcibios_init(void);

/*
 * The Machine Vector
 */

struct sh_machine_vector mv_od __initmv = {
	.mv_nr_irqs		= 48,

	.mv_inb			= od_inb,
	.mv_inw			= od_inw,
	.mv_inl			= od_inl,
	.mv_outb		= od_outb,
	.mv_outw		= od_outw,
	.mv_outl		= od_outl,

	.mv_inb_p		= od_inb_p,
	.mv_inw_p		= od_inw_p,
	.mv_inl_p		= od_inl_p,
	.mv_outb_p		= od_outb_p,
	.mv_outw_p		= od_outw_p,
	.mv_outl_p		= od_outl_p,

	.mv_insb		= od_insb,
	.mv_insw		= od_insw,
	.mv_insl		= od_insl,
	.mv_outsb		= od_outsb,
	.mv_outsw		= od_outsw,
	.mv_outsl		= od_outsl,

#ifdef CONFIG_PCI
	.mv_init_irq		= init_overdrive_irq,
#endif
#ifdef CONFIG_HEARTBEAT
	.mv_heartbeat		= heartbeat_od,
#endif
};

ALIAS_MV(od)
