/*
 * Setup pointers to hardware-dependent routines.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1998, 2001 by Ralf Baechle
 * Copyright (C) 2001 MIPS Technologies, Inc.
 */
#include <linux/config.h>
#include <linux/eisa.h>
#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/fb.h>
#include <linux/ide.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/jazz.h>
#include <asm/jazzdma.h>
#include <asm/ptrace.h>
#include <asm/reboot.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/time.h>
#include <asm/traps.h>

extern asmlinkage void jazz_handle_int(void);

extern void jazz_machine_restart(char *command);
extern void jazz_machine_halt(void);
extern void jazz_machine_power_off(void);

static void __init jazz_time_init(struct irqaction *irq)
{
	/* set the clock to 100 Hz */
	r4030_write_reg32(JAZZ_TIMER_INTERVAL, 9);
	setup_irq(JAZZ_TIMER_IRQ, irq);
}

static struct resource jazz_io_resources[] = {
	{ "dma1", 0x00, 0x1f, IORESOURCE_BUSY },
	{ "timer", 0x40, 0x5f, IORESOURCE_BUSY },
	{ "dma page reg", 0x80, 0x8f, IORESOURCE_BUSY },
	{ "dma2", 0xc0, 0xdf, IORESOURCE_BUSY },
};

static void __init jazz_setup(void)
{
	int i;

	/* Map 0xe0000000 -> 0x0:800005C0, 0xe0010000 -> 0x1:30000580 */
	add_wired_entry (0x02000017, 0x03c00017, 0xe0000000, PM_64K);

	/* Map 0xe2000000 -> 0x0:900005C0, 0xe3010000 -> 0x0:910005C0 */
	add_wired_entry (0x02400017, 0x02440017, 0xe2000000, PM_16M);

	/* Map 0xe4000000 -> 0x0:600005C0, 0xe4100000 -> 400005C0 */
	add_wired_entry (0x01800017, 0x01000017, 0xe4000000, PM_4M);

	set_io_port_base(JAZZ_PORT_BASE);
#ifdef CONFIG_EISA
	if (mips_machtype == MACH_MIPS_MAGNUM_4000)
		EISA_bus = 1;
#endif
	isa_slot_offset = 0xe3000000;

	/* request I/O space for devices used on all i[345]86 PCs */
	for (i = 0; i < ARRAY_SIZE(jazz_io_resources); i++)
		request_resource(&ioport_resource, jazz_io_resources + i);

        board_timer_setup = jazz_time_init;
	/* The RTC is outside the port address space */

	_machine_restart = jazz_machine_restart;
	_machine_halt = jazz_machine_halt;
	_machine_power_off = jazz_machine_power_off;

#warning "Somebody should check if screen_info is ok for Jazz."

	screen_info = (struct screen_info) {
		0, 0,		/* orig-x, orig-y */
		0,		/* unused */
		0,		/* orig_video_page */
		0,		/* orig_video_mode */
		160,		/* orig_video_cols */
		0, 0, 0,	/* unused, ega_bx, unused */
		64,		/* orig_video_lines */
		0,		/* orig_video_isVGA */
		16		/* orig_video_points */
	};

	vdma_init();
}

early_initcall(jazz_setup);
