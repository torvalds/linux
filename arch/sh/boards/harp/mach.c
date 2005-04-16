/*
 * linux/arch/sh/boards/harp/mach.c
 *
 * Copyright (C) 2000 Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Machine vector for the STMicroelectronics STB1 HARP and compatible boards
 */

#include <linux/init.h>

#include <asm/machvec.h>
#include <asm/rtc.h>
#include <asm/machvec_init.h>
#include <asm/hd64465/io.h>
#include <asm/hd64465/hd64465.h>

void setup_harp(void);
void init_harp_irq(void);
void heartbeat_harp(void);

/*
 * The Machine Vector
 */

struct sh_machine_vector mv_harp __initmv = {
	.mv_nr_irqs		= 89 + HD64465_IRQ_NUM,

	.mv_inb			= hd64465_inb,
	.mv_inw			= hd64465_inw,
	.mv_inl			= hd64465_inl,
	.mv_outb		= hd64465_outb,
	.mv_outw		= hd64465_outw,
	.mv_outl		= hd64465_outl,

	.mv_inb_p		= hd64465_inb_p,
	.mv_inw_p		= hd64465_inw,
	.mv_inl_p		= hd64465_inl,
	.mv_outb_p		= hd64465_outb_p,
	.mv_outw_p		= hd64465_outw,
	.mv_outl_p		= hd64465_outl,

	.mv_insb		= hd64465_insb,
	.mv_insw		= hd64465_insw,
	.mv_insl		= hd64465_insl,
	.mv_outsb		= hd64465_outsb,
	.mv_outsw		= hd64465_outsw,
	.mv_outsl		= hd64465_outsl,

        .mv_isa_port2addr       = hd64465_isa_port2addr,

#ifdef CONFIG_PCI
	.mv_init_irq		= init_harp_irq,
#endif
#ifdef CONFIG_HEARTBEAT
	.mv_heartbeat		= heartbeat_harp,
#endif
};

ALIAS_MV(harp)
