/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/tty.h>

#ifdef CONFIG_MTD
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#endif

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/mips-boards/generic.h>
#include <asm/mips-boards/prom.h>
#include <asm/mips-boards/malta.h>
#include <asm/mips-boards/maltaint.h>
#include <asm/dma.h>
#include <asm/time.h>
#include <asm/traps.h>
#ifdef CONFIG_VT
#include <linux/console.h>
#endif

extern void mips_reboot_setup(void);
extern void mips_time_init(void);
extern void mips_timer_setup(struct irqaction *irq);
extern unsigned long mips_rtc_get_time(void);

#ifdef CONFIG_KGDB
extern void kgdb_config(void);
#endif

struct resource standard_io_resources[] = {
	{ .name = "dma1", .start = 0x00, .end = 0x1f, .flags = IORESOURCE_BUSY },
	{ .name = "timer", .start = 0x40, .end = 0x5f, .flags = IORESOURCE_BUSY },
	{ .name = "keyboard", .start = 0x60, .end = 0x6f, .flags = IORESOURCE_BUSY },
	{ .name = "dma page reg", .start = 0x80, .end = 0x8f, .flags = IORESOURCE_BUSY },
	{ .name = "dma2", .start = 0xc0, .end = 0xdf, .flags = IORESOURCE_BUSY },
};

#ifdef CONFIG_MTD
static struct mtd_partition malta_mtd_partitions[] = {
	{
		.name =		"YAMON",
		.offset =	0x0,
		.size =		0x100000,
		.mask_flags =	MTD_WRITEABLE
	},
	{
		.name =		"User FS",
		.offset = 	0x100000,
		.size =		0x2e0000
	},
	{
		.name =		"Board Config",
		.offset =	0x3e0000,
		.size =		0x020000,
		.mask_flags =	MTD_WRITEABLE
	}
};

#define number_partitions	(sizeof(malta_mtd_partitions)/sizeof(struct mtd_partition))
#endif

const char *get_system_type(void)
{
	return "MIPS Malta";
}

#ifdef CONFIG_BLK_DEV_FD
void __init fd_activate(void)
{
	/*
	 * Activate Floppy Controller in the SMSC FDC37M817 Super I/O
	 * Controller.
	 * Done by YAMON 2.00 onwards
	 */
	/* Entering config state. */
	SMSC_WRITE(SMSC_CONFIG_ENTER, SMSC_CONFIG_REG);

	/* Activate floppy controller. */
	SMSC_WRITE(SMSC_CONFIG_DEVNUM, SMSC_CONFIG_REG);
	SMSC_WRITE(SMSC_CONFIG_DEVNUM_FLOPPY, SMSC_DATA_REG);
	SMSC_WRITE(SMSC_CONFIG_ACTIVATE, SMSC_CONFIG_REG);
	SMSC_WRITE(SMSC_CONFIG_ACTIVATE_ENABLE, SMSC_DATA_REG);

	/* Exit config state. */
	SMSC_WRITE(SMSC_CONFIG_EXIT, SMSC_CONFIG_REG);
}
#endif

void __init plat_setup(void)
{
	unsigned int i;

	mips_pcibios_init();

	/* Request I/O space for devices used on the Malta board. */
	for (i = 0; i < ARRAY_SIZE(standard_io_resources); i++)
		request_resource(&ioport_resource, standard_io_resources+i);

	/*
	 * Enable DMA channel 4 (cascade channel) in the PIIX4 south bridge.
	 */
	enable_dma(4);

#ifdef CONFIG_KGDB
	kgdb_config ();
#endif

	if ((mips_revision_corid == MIPS_REVISION_CORID_BONITO64) ||
	    (mips_revision_corid == MIPS_REVISION_CORID_CORE_20K) ||
	    (mips_revision_corid == MIPS_REVISION_CORID_CORE_EMUL_BON)) {
		char *argptr;

		argptr = prom_getcmdline();
		if (strstr(argptr, "debug")) {
			BONITO_BONGENCFG |= BONITO_BONGENCFG_DEBUGMODE;
			printk ("Enabled Bonito debug mode\n");
		}
		else
			BONITO_BONGENCFG &= ~BONITO_BONGENCFG_DEBUGMODE;

#ifdef CONFIG_DMA_COHERENT
		if (BONITO_PCICACHECTRL & BONITO_PCICACHECTRL_CPUCOH_PRES) {
			BONITO_PCICACHECTRL |= BONITO_PCICACHECTRL_CPUCOH_EN;
			printk("Enabled Bonito CPU coherency\n");

			argptr = prom_getcmdline();
			if (strstr(argptr, "iobcuncached")) {
				BONITO_PCICACHECTRL &= ~BONITO_PCICACHECTRL_IOBCCOH_EN;
				BONITO_PCIMEMBASECFG = BONITO_PCIMEMBASECFG &
					~(BONITO_PCIMEMBASECFG_MEMBASE0_CACHED |
					  BONITO_PCIMEMBASECFG_MEMBASE1_CACHED);
				printk("Disabled Bonito IOBC coherency\n");
			}
			else {
				BONITO_PCICACHECTRL |= BONITO_PCICACHECTRL_IOBCCOH_EN;
				BONITO_PCIMEMBASECFG |=
					(BONITO_PCIMEMBASECFG_MEMBASE0_CACHED |
					 BONITO_PCIMEMBASECFG_MEMBASE1_CACHED);
				printk("Disabled Bonito IOBC coherency\n");
			}
		}
		else
			panic("Hardware DMA cache coherency not supported");

#endif
	}
#ifdef CONFIG_DMA_COHERENT
	else {
		panic("Hardware DMA cache coherency not supported");
	}
#endif

#ifdef CONFIG_BLK_DEV_IDE
	/* Check PCI clock */
	{
		int jmpr = (*((volatile unsigned int *)ioremap(MALTA_JMPRS_REG, sizeof(unsigned int))) >> 2) & 0x07;
		static const int pciclocks[] __initdata = {
			33, 20, 25, 30, 12, 16, 37, 10
		};
		int pciclock = pciclocks[jmpr];
		char *argptr = prom_getcmdline();

		if (pciclock != 33 && !strstr (argptr, "idebus=")) {
			printk("WARNING: PCI clock is %dMHz, setting idebus\n", pciclock);
			argptr += strlen(argptr);
			sprintf (argptr, " idebus=%d", pciclock);
			if (pciclock < 20 || pciclock > 66)
				printk ("WARNING: IDE timing calculations will be incorrect\n");
		}
	}
#endif
#ifdef CONFIG_BLK_DEV_FD
	fd_activate ();
#endif
#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	screen_info = (struct screen_info) {
		0, 25,			/* orig-x, orig-y */
		0,			/* unused */
		0,			/* orig-video-page */
		0,			/* orig-video-mode */
		80,			/* orig-video-cols */
		0,0,0,			/* ega_ax, ega_bx, ega_cx */
		25,			/* orig-video-lines */
		VIDEO_TYPE_VGAC,	/* orig-video-isVGA */
		16			/* orig-video-points */
	};
#endif
#endif

#ifdef CONFIG_MTD
	/*
	 * Support for MTD on Malta. Use the generic physmap driver
	 */
	physmap_configure(0x1e000000, 0x400000, 4, NULL);
	physmap_set_partitions(malta_mtd_partitions, number_partitions);
#endif

	mips_reboot_setup();

	board_time_init = mips_time_init;
	board_timer_setup = mips_timer_setup;
	rtc_mips_get_time = mips_rtc_get_time;
}
