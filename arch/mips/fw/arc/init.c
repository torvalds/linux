/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * PROM library initialisation code.
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 */
#include <linux/init.h>
#include <linux/kernel.h>

#include <asm/bootinfo.h>
#include <asm/sgialib.h>
#include <asm/smp-ops.h>

#undef DEBUG_PROM_INIT

/* Master romvec interface. */
struct linux_romvec *romvec;

#if defined(CONFIG_64BIT) && defined(CONFIG_FW_ARC32)
/* stack for calling 32bit ARC prom */
u64 o32_stk[4096];
#endif

void __init prom_init(void)
{
	PSYSTEM_PARAMETER_BLOCK pb = PROMBLOCK;

	romvec = ROMVECTOR;

	if (pb->magic != 0x53435241) {
		printk(KERN_CRIT "Aieee, bad prom vector magic %08lx\n",
		       (unsigned long) pb->magic);
		while(1)
			;
	}

	prom_init_cmdline(fw_arg0, (LONG *)fw_arg1);
	prom_identify_arch();
	printk(KERN_INFO "PROMLIB: ARC firmware Version %d Revision %d\n",
	       pb->ver, pb->rev);
	prom_meminit();

#ifdef DEBUG_PROM_INIT
	pr_info("Press a key to reboot\n");
	ArcRead(0, &c, 1, &cnt);
	ArcEnterInteractiveMode();
#endif
}
