/*
 * linux/arch/sh/boards/hp6xx/hp680/setup.c
 *
 * Copyright (C) 2002 Andriy Skulysh
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Setup code for an HP680  (internal peripherials only)
 */

#include <linux/config.h>
#include <linux/init.h>
#include <asm/hd64461/hd64461.h>
#include <asm/io.h>
#include <asm/hp6xx/hp6xx.h>
#include <asm/cpu/dac.h>

const char *get_system_type(void)
{
	return "HP680";
}

int __init platform_setup(void)
{
	u16 v;
	v = inw(HD64461_STBCR);
	v |= HD64461_STBCR_SURTST | HD64461_STBCR_SIRST |
	    HD64461_STBCR_STM1ST | HD64461_STBCR_STM0ST |
	    HD64461_STBCR_SAFEST | HD64461_STBCR_SPC0ST |
	    HD64461_STBCR_SMIAST | HD64461_STBCR_SAFECKE_OST |
	    HD64461_STBCR_SAFECKE_IST;
	outw(v, HD64461_STBCR);
	v = inw(HD64461_GPADR);
	v |= HD64461_GPADR_SPEAKER | HD64461_GPADR_PCMCIA0;
	outw(v, HD64461_GPADR);

	sh_dac_disable(DAC_SPEAKER_VOLUME);

	return 0;
}
