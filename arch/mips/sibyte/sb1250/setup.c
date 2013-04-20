/*
 * Copyright (C) 2000, 2001, 2002, 2003 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/string.h>

#include <asm/bootinfo.h>
#include <asm/mipsregs.h>
#include <asm/io.h>
#include <asm/sibyte/sb1250.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_scd.h>

unsigned int sb1_pass;
unsigned int soc_pass;
unsigned int soc_type;
EXPORT_SYMBOL(soc_type);
unsigned int periph_rev;
unsigned int zbbus_mhz;
EXPORT_SYMBOL(zbbus_mhz);

static char *soc_str;
static char *pass_str;
static unsigned int war_pass;	/* XXXKW don't overload PASS defines? */

static int __init setup_bcm1250(void)
{
	int ret = 0;

	switch (soc_pass) {
	case K_SYS_REVISION_BCM1250_PASS1:
		periph_rev = 1;
		pass_str = "Pass 1";
		break;
	case K_SYS_REVISION_BCM1250_A10:
		periph_rev = 2;
		pass_str = "A8/A10";
		/* XXXKW different war_pass? */
		war_pass = K_SYS_REVISION_BCM1250_PASS2;
		break;
	case K_SYS_REVISION_BCM1250_PASS2_2:
		periph_rev = 2;
		pass_str = "B1";
		break;
	case K_SYS_REVISION_BCM1250_B2:
		periph_rev = 2;
		pass_str = "B2";
		war_pass = K_SYS_REVISION_BCM1250_PASS2_2;
		break;
	case K_SYS_REVISION_BCM1250_PASS3:
		periph_rev = 3;
		pass_str = "C0";
		break;
	case K_SYS_REVISION_BCM1250_C1:
		periph_rev = 3;
		pass_str = "C1";
		break;
	default:
		if (soc_pass < K_SYS_REVISION_BCM1250_PASS2_2) {
			periph_rev = 2;
			pass_str = "A0-A6";
			war_pass = K_SYS_REVISION_BCM1250_PASS2;
		} else {
			printk("Unknown BCM1250 rev %x\n", soc_pass);
			ret = 1;
		}
		break;
	}

	return ret;
}

int sb1250_m3_workaround_needed(void)
{
	switch (soc_type) {
	case K_SYS_SOC_TYPE_BCM1250:
	case K_SYS_SOC_TYPE_BCM1250_ALT:
	case K_SYS_SOC_TYPE_BCM1250_ALT2:
	case K_SYS_SOC_TYPE_BCM1125:
	case K_SYS_SOC_TYPE_BCM1125H:
		return soc_pass < K_SYS_REVISION_BCM1250_C0;

	default:
		return 0;
	}
}

static int __init setup_bcm112x(void)
{
	int ret = 0;

	switch (soc_pass) {
	case 0:
		/* Early build didn't have revid set */
		periph_rev = 3;
		pass_str = "A1";
		war_pass = K_SYS_REVISION_BCM112x_A1;
		break;
	case K_SYS_REVISION_BCM112x_A1:
		periph_rev = 3;
		pass_str = "A1";
		break;
	case K_SYS_REVISION_BCM112x_A2:
		periph_rev = 3;
		pass_str = "A2";
		break;
	case K_SYS_REVISION_BCM112x_A3:
		periph_rev = 3;
		pass_str = "A3";
		break;
	case K_SYS_REVISION_BCM112x_A4:
		periph_rev = 3;
		pass_str = "A4";
		break;
	case K_SYS_REVISION_BCM112x_B0:
		periph_rev = 3;
		pass_str = "B0";
		break;
	default:
		printk("Unknown %s rev %x\n", soc_str, soc_pass);
		ret = 1;
	}

	return ret;
}

/* Setup code likely to be common to all SiByte platforms */

static int __init sys_rev_decode(void)
{
	int ret = 0;

	war_pass = soc_pass;
	switch (soc_type) {
	case K_SYS_SOC_TYPE_BCM1250:
	case K_SYS_SOC_TYPE_BCM1250_ALT:
	case K_SYS_SOC_TYPE_BCM1250_ALT2:
		soc_str = "BCM1250";
		ret = setup_bcm1250();
		break;
	case K_SYS_SOC_TYPE_BCM1120:
		soc_str = "BCM1120";
		ret = setup_bcm112x();
		break;
	case K_SYS_SOC_TYPE_BCM1125:
		soc_str = "BCM1125";
		ret = setup_bcm112x();
		break;
	case K_SYS_SOC_TYPE_BCM1125H:
		soc_str = "BCM1125H";
		ret = setup_bcm112x();
		break;
	default:
		printk("Unknown SOC type %x\n", soc_type);
		ret = 1;
		break;
	}

	return ret;
}

void __init sb1250_setup(void)
{
	uint64_t sys_rev;
	int plldiv;
	int bad_config = 0;

	sb1_pass = read_c0_prid() & 0xff;
	sys_rev = __raw_readq(IOADDR(A_SCD_SYSTEM_REVISION));
	soc_type = SYS_SOC_TYPE(sys_rev);
	soc_pass = G_SYS_REVISION(sys_rev);

	if (sys_rev_decode()) {
		printk("Restart after failure to identify SiByte chip\n");
		machine_restart(NULL);
	}

	plldiv = G_SYS_PLL_DIV(__raw_readq(IOADDR(A_SCD_SYSTEM_CFG)));
	zbbus_mhz = ((plldiv >> 1) * 50) + ((plldiv & 1) * 25);

	printk("Broadcom SiByte %s %s @ %d MHz (SB1 rev %d)\n",
		    soc_str, pass_str, zbbus_mhz * 2, sb1_pass);
	printk("Board type: %s\n", get_system_type());

	switch (war_pass) {
	case K_SYS_REVISION_BCM1250_PASS1:
#ifndef CONFIG_SB1_PASS_1_WORKAROUNDS
		printk("@@@@ This is a BCM1250 A0-A2 (Pass 1) board, "
			    "and the kernel doesn't have the proper "
			    "workarounds compiled in. @@@@\n");
		bad_config = 1;
#endif
		break;
	case K_SYS_REVISION_BCM1250_PASS2:
		/* Pass 2 - easiest as default for now - so many numbers */
#if !defined(CONFIG_SB1_PASS_2_WORKAROUNDS) || \
    !defined(CONFIG_SB1_PASS_2_1_WORKAROUNDS)
		printk("@@@@ This is a BCM1250 A3-A10 board, and the "
			    "kernel doesn't have the proper workarounds "
			    "compiled in. @@@@\n");
		bad_config = 1;
#endif
#ifdef CONFIG_CPU_HAS_PREFETCH
		printk("@@@@ Prefetches may be enabled in this kernel, "
			    "but are buggy on this board.  @@@@\n");
		bad_config = 1;
#endif
		break;
	case K_SYS_REVISION_BCM1250_PASS2_2:
#ifndef CONFIG_SB1_PASS_2_WORKAROUNDS
		printk("@@@@ This is a BCM1250 B1/B2. board, and the "
			    "kernel doesn't have the proper workarounds "
			    "compiled in. @@@@\n");
		bad_config = 1;
#endif
#if defined(CONFIG_SB1_PASS_2_1_WORKAROUNDS) || \
    !defined(CONFIG_CPU_HAS_PREFETCH)
		printk("@@@@ This is a BCM1250 B1/B2, but the kernel is "
			    "conservatively configured for an 'A' stepping. "
			    "@@@@\n");
#endif
		break;
	default:
		break;
	}
	if (bad_config) {
		printk("Invalid configuration for this chip.\n");
		machine_restart(NULL);
	}
}
