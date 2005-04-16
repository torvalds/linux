/*
 * Setup pointers to hardware-dependent routines.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 97, 98, 2000, 03, 04 Ralf Baechle (ralf@linux-mips.org)
 */
#include <linux/config.h>
#include <linux/eisa.h>
#include <linux/hdreg.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/mc146818rtc.h>
#include <linux/pci.h>
#include <linux/console.h>
#include <linux/fb.h>
#include <linux/tty.h>

#include <asm/arc/types.h>
#include <asm/sgialib.h>
#include <asm/bcache.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mc146818-time.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/reboot.h>
#include <asm/sni.h>
#include <asm/time.h>
#include <asm/traps.h>

extern void sni_machine_restart(char *command);
extern void sni_machine_halt(void);
extern void sni_machine_power_off(void);

static void __init sni_rm200_pci_timer_setup(struct irqaction *irq)
{
	/* set the clock to 100 Hz */
	outb_p(0x34,0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	outb(LATCH >> 8 , 0x40);	/* MSB */
	setup_irq(0, irq);
}

/*
 * A bit more gossip about the iron we're running on ...
 */
static inline void sni_pcimt_detect(void)
{
	char boardtype[80];
	unsigned char csmsr;
	char *p = boardtype;
	unsigned int asic;

	csmsr = *(volatile unsigned char *)PCIMT_CSMSR;

	p += sprintf(p, "%s PCI", (csmsr & 0x80) ? "RM200" : "RM300");
	if ((csmsr & 0x80) == 0)
		p += sprintf(p, ", board revision %s",
		             (csmsr & 0x20) ? "D" : "C");
	asic = csmsr & 0x80;
	asic = (csmsr & 0x08) ? asic : !asic;
	p += sprintf(p, ", ASIC PCI Rev %s", asic ? "1.0" : "1.1");
	printk("%s.\n", boardtype);
}

static void __init sni_display_setup(void)
{
#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	struct screen_info *si = &screen_info;
	DISPLAY_STATUS *di;

	di = ArcGetDisplayStatus(1);

	if (di) {
		si->orig_x		= di->CursorXPosition;
		si->orig_y		= di->CursorYPosition;
		si->orig_video_cols	= di->CursorMaxXPosition;
		si->orig_video_lines	= di->CursorMaxYPosition;
		si->orig_video_isVGA	= VIDEO_TYPE_VGAC;
		si->orig_video_points	= 16;
	}
#endif
#endif
}

static struct resource sni_io_resource = {
	"PCIMT IO MEM", 0x00001000UL, 0x03bfffffUL, IORESOURCE_IO,
};

static struct resource pcimt_io_resources[] = {
	{ "dma1", 0x00, 0x1f, IORESOURCE_BUSY },
	{ "timer", 0x40, 0x5f, IORESOURCE_BUSY },
	{ "keyboard", 0x60, 0x6f, IORESOURCE_BUSY },
	{ "dma page reg", 0x80, 0x8f, IORESOURCE_BUSY },
	{ "dma2", 0xc0, 0xdf, IORESOURCE_BUSY },
	{ "PCI config data", 0xcfc, 0xcff, IORESOURCE_BUSY }
};

static struct resource sni_mem_resource = {
	"PCIMT PCI MEM", 0x10000000UL, 0xffffffffUL, IORESOURCE_MEM
};

/*
 * The RM200/RM300 has a few holes in it's PCI/EISA memory address space used
 * for other purposes.  Be paranoid and allocate all of the before the PCI
 * code gets a chance to to map anything else there ...
 * 
 * This leaves the following areas available:
 *
 * 0x10000000 - 0x1009ffff (640kB) PCI/EISA/ISA Bus Memory
 * 0x10100000 - 0x13ffffff ( 15MB) PCI/EISA/ISA Bus Memory
 * 0x18000000 - 0x1fbfffff (124MB) PCI/EISA Bus Memory
 * 0x1ff08000 - 0x1ffeffff (816kB) PCI/EISA Bus Memory
 * 0xa0000000 - 0xffffffff (1.5GB) PCI/EISA Bus Memory
 */
static struct resource pcimt_mem_resources[] = {
	{ "Video RAM area", 0x100a0000, 0x100bffff, IORESOURCE_BUSY },
	{ "ISA Reserved", 0x100c0000, 0x100fffff, IORESOURCE_BUSY },
	{ "PCI IO", 0x14000000, 0x17bfffff, IORESOURCE_BUSY },
	{ "Cache Replacement Area", 0x17c00000, 0x17ffffff, IORESOURCE_BUSY},
	{ "PCI INT Acknowledge", 0x1a000000, 0x1a000003, IORESOURCE_BUSY },
	{ "Boot PROM", 0x1fc00000, 0x1fc7ffff, IORESOURCE_BUSY},
	{ "Diag PROM", 0x1fc80000, 0x1fcfffff, IORESOURCE_BUSY},
	{ "X-Bus", 0x1fd00000, 0x1fdfffff, IORESOURCE_BUSY},
	{ "BIOS map", 0x1fe00000, 0x1fefffff, IORESOURCE_BUSY},
	{ "NVRAM / EEPROM", 0x1ff00000, 0x1ff7ffff, IORESOURCE_BUSY},
	{ "ASIC PCI", 0x1fff0000, 0x1fffefff, IORESOURCE_BUSY},
	{ "MP Agent", 0x1ffff000, 0x1fffffff, IORESOURCE_BUSY},
	{ "Main Memory", 0x20000000, 0x9fffffff, IORESOURCE_BUSY}
};

static void __init sni_resource_init(void)
{
	int i;

	/* request I/O space for devices used on all i[345]86 PCs */
	for (i = 0; i < ARRAY_SIZE(pcimt_io_resources); i++)
		request_resource(&ioport_resource, pcimt_io_resources + i);

	/* request mem space for pcimt-specific devices */
	for (i = 0; i < ARRAY_SIZE(pcimt_mem_resources); i++)
		request_resource(&sni_mem_resource, pcimt_mem_resources + i);

	ioport_resource.end = sni_io_resource.end;
}

extern struct pci_ops sni_pci_ops;

static struct pci_controller sni_controller = {
	.pci_ops	= &sni_pci_ops,
	.mem_resource	= &sni_mem_resource,
	.mem_offset	= 0x10000000UL,
	.io_resource	= &sni_io_resource,
	.io_offset	= 0x00000000UL
};

static inline void sni_pcimt_time_init(void)
{
	rtc_get_time = mc146818_get_cmos_time;
	rtc_set_time = mc146818_set_rtc_mmss;
}

static int __init sni_rm200_pci_setup(void)
{
	sni_pcimt_detect();
	sni_pcimt_sc_init();
	sni_pcimt_time_init();

	set_io_port_base(SNI_PORT_BASE);
	ioport_resource.end = sni_io_resource.end;

	/*
	 * Setup (E)ISA I/O memory access stuff
	 */
	isa_slot_offset = 0xb0000000;
#ifdef CONFIG_EISA
	EISA_bus = 1;
#endif

	sni_resource_init();
	board_timer_setup = sni_rm200_pci_timer_setup;

	_machine_restart = sni_machine_restart;
	_machine_halt = sni_machine_halt;
	_machine_power_off = sni_machine_power_off;

	sni_display_setup();

#ifdef CONFIG_PCI
	register_pci_controller(&sni_controller);
#endif

	return 0;
}

early_initcall(sni_rm200_pci_setup);
