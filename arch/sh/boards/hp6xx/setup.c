/*
 * linux/arch/sh/boards/hp6xx/setup.c
 *
 * Copyright (C) 2002 Andriy Skulysh
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Setup code for an HP680  (internal peripherials only)
 */
#include <linux/types.h>
#include <linux/init.h>
#include <asm/hd64461.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/hp6xx.h>
#include <asm/cpu/dac.h>

#define	SCPCR	0xa4000116
#define SCPDR	0xa4000136

static void __init hp6xx_setup(char **cmdline_p)
{
	u8 v8;
	u16 v;

	v = inw(HD64461_STBCR);
	v |= HD64461_STBCR_SURTST | HD64461_STBCR_SIRST |
	    HD64461_STBCR_STM1ST | HD64461_STBCR_STM0ST |
	    HD64461_STBCR_SAFEST | HD64461_STBCR_SPC0ST |
	    HD64461_STBCR_SMIAST | HD64461_STBCR_SAFECKE_OST |
	    HD64461_STBCR_SAFECKE_IST;
#ifndef CONFIG_HD64461_ENABLER
	v |= HD64461_STBCR_SPC1ST;
#endif
	outw(v, HD64461_STBCR);
	v = inw(HD64461_GPADR);
	v |= HD64461_GPADR_SPEAKER | HD64461_GPADR_PCMCIA0;
	outw(v, HD64461_GPADR);

	outw(HD64461_PCCGCR_VCC0 | HD64461_PCCSCR_VCC1, HD64461_PCC0GCR);

#ifndef CONFIG_HD64461_ENABLER
	outw(HD64461_PCCGCR_VCC0 | HD64461_PCCSCR_VCC1, HD64461_PCC1GCR);
#endif

	sh_dac_output(0, DAC_SPEAKER_VOLUME);
	sh_dac_disable(DAC_SPEAKER_VOLUME);
	v8 = ctrl_inb(DACR);
	v8 &= ~DACR_DAE;
	ctrl_outb(v8,DACR);

	v8 = ctrl_inb(SCPDR);
	v8 |= SCPDR_TS_SCAN_X | SCPDR_TS_SCAN_Y;
	v8 &= ~SCPDR_TS_SCAN_ENABLE;
	ctrl_outb(v8, SCPDR);

	v = ctrl_inw(SCPCR);
	v &= ~SCPCR_TS_MASK;
	v |= SCPCR_TS_ENABLE;
	ctrl_outw(v, SCPCR);
}

/*
 * XXX: This is stupid, we should have a generic machine vector for the cchips
 * and just wrap the platform setup code in to this, as it's the only thing
 * that ends up being different.
 */
struct sh_machine_vector mv_hp6xx __initmv = {
	.mv_name = "hp6xx",
	.mv_setup = hp6xx_setup,
	.mv_nr_irqs = HD64461_IRQBASE + HD64461_IRQ_NUM,

	.mv_inb = hd64461_inb,
	.mv_inw = hd64461_inw,
	.mv_inl = hd64461_inl,
	.mv_outb = hd64461_outb,
	.mv_outw = hd64461_outw,
	.mv_outl = hd64461_outl,

	.mv_inb_p = hd64461_inb_p,
	.mv_inw_p = hd64461_inw,
	.mv_inl_p = hd64461_inl,
	.mv_outb_p = hd64461_outb_p,
	.mv_outw_p = hd64461_outw,
	.mv_outl_p = hd64461_outl,

	.mv_insb = hd64461_insb,
	.mv_insw = hd64461_insw,
	.mv_insl = hd64461_insl,
	.mv_outsb = hd64461_outsb,
	.mv_outsw = hd64461_outsw,
	.mv_outsl = hd64461_outsl,

	.mv_readw = hd64461_readw,
	.mv_writew = hd64461_writew,

	.mv_irq_demux = hd64461_irq_demux,
};
ALIAS_MV(hp6xx)
