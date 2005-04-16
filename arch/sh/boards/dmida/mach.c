/*
 * linux/arch/sh/boards/dmida/mach.c
 *
 * by Greg Banks <gbanks@pocketpenguins.com>
 * (c) 2000 PocketPenguins Inc
 *
 * Derived from mach_hp600.c, which bore the message:
 * Copyright (C) 2000 Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Machine vector for the DataMyte Industrial Digital Assistant(tm).
 * See http://www.dmida.com
 *
 */

#include <linux/init.h>

#include <asm/machvec.h>
#include <asm/rtc.h>
#include <asm/machvec_init.h>

#include <asm/io.h>
#include <asm/hd64465/hd64465.h>
#include <asm/irq.h>

/*
 * The Machine Vector
 */

struct sh_machine_vector mv_dmida __initmv = {
	.mv_nr_irqs		= HD64465_IRQ_BASE+HD64465_IRQ_NUM,

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

	.mv_irq_demux		= hd64465_irq_demux,
};
ALIAS_MV(dmida)

