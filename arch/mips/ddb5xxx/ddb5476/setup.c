/*
 *  arch/mips/ddb5476/setup.c -- NEC DDB Vrc-5476 setup routines
 *
 *  Copyright (C) 2000 Geert Uytterhoeven <geert@sonycom.com>
 *                     Sony Software Development Center Europe (SDCE), Brussels
 */
#include <linux/init.h>
#include <linux/kbd_ll.h>
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/pm.h>

#include <asm/addrspace.h>
#include <asm/bcache.h>
#include <asm/irq.h>
#include <asm/reboot.h>
#include <asm/gdb-stub.h>
#include <asm/time.h>
#include <asm/debug.h>
#include <asm/traps.h>

#include <asm/ddb5xxx/ddb5xxx.h>

// #define USE_CPU_COUNTER_TIMER	/* whether we use cpu counter */

#ifdef USE_CPU_COUNTER_TIMER

#define CPU_COUNTER_FREQUENCY           83000000
#else
/* otherwise we use general purpose timer */
#define TIMER_FREQUENCY			83000000
#define TIMER_BASE			DDB_T2CTRL
#define TIMER_IRQ			(VRC5476_IRQ_BASE + VRC5476_IRQ_GPT)
#endif

static void (*back_to_prom) (void) = (void (*)(void)) 0xbfc00000;

static void ddb_machine_restart(char *command)
{
	u32 t;

	/* PCI cold reset */
	t = ddb_in32(DDB_PCICTRL + 4);
	t |= 0x40000000;
	ddb_out32(DDB_PCICTRL + 4, t);
	/* CPU cold reset */
	t = ddb_in32(DDB_CPUSTAT);
	t |= 1;
	ddb_out32(DDB_CPUSTAT, t);
	/* Call the PROM */
	back_to_prom();
}

static void ddb_machine_halt(void)
{
	printk(KERN_NOTICE "DDB Vrc-5476 halted.\n");
	while (1);
}

static void ddb_machine_power_off(void)
{
	printk(KERN_NOTICE "DDB Vrc-5476 halted. Please turn off the power.\n");
	while (1);
}

extern void rtc_ds1386_init(unsigned long base);

static void __init ddb_time_init(void)
{
#if defined(USE_CPU_COUNTER_TIMER)
	mips_hpt_frequency = CPU_COUNTER_FREQUENCY;
#endif

	/* we have ds1396 RTC chip */
	rtc_ds1386_init(KSEG1ADDR(DDB_PCI_MEM_BASE));
}


extern int setup_irq(unsigned int irq, struct irqaction *irqaction);
static void __init ddb_timer_setup(struct irqaction *irq)
{
#if defined(USE_CPU_COUNTER_TIMER)

	unsigned int count;

	/* we are using the cpu counter for timer interrupts */
	setup_irq(CPU_IRQ_BASE + 7, irq);

	/* to generate the first timer interrupt */
	count = read_c0_count();
	write_c0_compare(count + 1000);

#else

	ddb_out32(TIMER_BASE, TIMER_FREQUENCY/HZ);
	ddb_out32(TIMER_BASE+4, 0x1);	/* enable timer */
	setup_irq(TIMER_IRQ, irq);
#endif
}

static struct {
	struct resource dma1;
	struct resource timer;
	struct resource rtc;
	struct resource dma_page_reg;
	struct resource dma2;
} ddb5476_ioport = {
	{
	"dma1", 0x00, 0x1f, IORESOURCE_BUSY}, {
	"timer", 0x40, 0x5f, IORESOURCE_BUSY}, {
	"rtc", 0x70, 0x7f, IORESOURCE_BUSY}, {
	"dma page reg", 0x80, 0x8f, IORESOURCE_BUSY}, {
	"dma2", 0xc0, 0xdf, IORESOURCE_BUSY}
};

static struct {
	struct resource nile4;
} ddb5476_iomem = {
	{ "Nile 4", DDB_BASE, DDB_BASE + DDB_SIZE - 1, IORESOURCE_BUSY}
};


static void ddb5476_board_init(void);

void __init plat_setup(void)
{
	set_io_port_base(KSEG1ADDR(DDB_PCI_IO_BASE));

	board_time_init = ddb_time_init;
	board_timer_setup = ddb_timer_setup;

	_machine_restart = ddb_machine_restart;
	_machine_halt = ddb_machine_halt;
	pm_power_off = ddb_machine_power_off;

	/* request io port/mem resources  */
	if (request_resource(&ioport_resource, &ddb5476_ioport.dma1) ||
	    request_resource(&ioport_resource, &ddb5476_ioport.timer) ||
	    request_resource(&ioport_resource, &ddb5476_ioport.rtc) ||
	    request_resource(&ioport_resource,
			     &ddb5476_ioport.dma_page_reg)
	    || request_resource(&ioport_resource, &ddb5476_ioport.dma2)
	    || request_resource(&iomem_resource, &ddb5476_iomem.nile4)) {
		printk
		    ("ddb_setup - requesting oo port resources failed.\n");
		for (;;);
	}

	/* Reboot on panic */
	panic_timeout = 180;

	/* [jsun] we need to set BAR0 so that SDRAM 0 appears at 0x0 in PCI */
	/* *(long*)0xbfa00218 = 0x8; */

	/* board initialization stuff */
	ddb5476_board_init();
}

/*
 * We don't trust bios.  We essentially does hardware re-initialization
 * as complete as possible, as far as we know we can safely do.
 */
static void ddb5476_board_init(void)
{
	/* ----------- setup PDARs ------------ */
	/* check SDRAM0, whether we are on MEM bus does not matter */
	db_assert((ddb_in32(DDB_SDRAM0) & 0xffffffef) ==
		  ddb_calc_pdar(DDB_SDRAM_BASE, DDB_SDRAM_SIZE, 32, 0, 1));

	/* SDRAM1 should be turned off.  What is this for anyway ? */
	db_assert( (ddb_in32(DDB_SDRAM1) & 0xf) == 0);

	/* flash 1&2, DDB status, DDB control */
	ddb_set_pdar(DDB_DCS2, DDB_DCS2_BASE, DDB_DCS2_SIZE, 16, 0, 0);
	ddb_set_pdar(DDB_DCS3, DDB_DCS3_BASE, DDB_DCS3_SIZE, 16, 0, 0);
	ddb_set_pdar(DDB_DCS4, DDB_DCS4_BASE, DDB_DCS4_SIZE, 8, 0, 0);
	ddb_set_pdar(DDB_DCS5, DDB_DCS5_BASE, DDB_DCS5_SIZE, 8, 0, 0);

	/* shut off other pdar so they don't accidentally get into the way */
	ddb_set_pdar(DDB_DCS6, 0xffffffff, 0, 32, 0, 0);
	ddb_set_pdar(DDB_DCS7, 0xffffffff, 0, 32, 0, 0);
	ddb_set_pdar(DDB_DCS8, 0xffffffff, 0, 32, 0, 0);

	/* verify VRC5477 base addr */
	/* don't care about some details */
	db_assert((ddb_in32(DDB_INTCS) & 0xffffff0f) ==
		  ddb_calc_pdar(DDB_INTCS_BASE, DDB_INTCS_SIZE, 8, 0, 0));

	/* verify BOOT ROM addr */
	/* don't care about some details */
	db_assert((ddb_in32(DDB_BOOTCS) & 0xffffff0f) ==
		  ddb_calc_pdar(DDB_BOOTCS_BASE, DDB_BOOTCS_SIZE, 8, 0, 0));

	/* setup PCI windows - window1 for MEM/config, window0 for IO */
	ddb_set_pdar(DDB_PCIW0, DDB_PCI_IO_BASE, DDB_PCI_IO_SIZE, 32, 0, 1);
	ddb_set_pmr(DDB_PCIINIT0, DDB_PCICMD_IO, 0, DDB_PCI_ACCESS_32);

	ddb_set_pdar(DDB_PCIW1, DDB_PCI_MEM_BASE, DDB_PCI_MEM_SIZE, 32, 0, 1);
	ddb_set_pmr(DDB_PCIINIT1, DDB_PCICMD_MEM, DDB_PCI_MEM_BASE, DDB_PCI_ACCESS_32);

	/* ----------- setup PDARs ------------ */
	/* this is problematic - it will reset Aladin which cause we loose
	 * serial port, and we don't know how to set up Aladin chip again.
	 */
	// ddb_pci_reset_bus();

	ddb_out32(DDB_BAR0, 0x00000008);

	ddb_out32(DDB_BARC, 0xffffffff);
	ddb_out32(DDB_BARB, 0xffffffff);
	ddb_out32(DDB_BAR1, 0xffffffff);
	ddb_out32(DDB_BAR2, 0xffffffff);
	ddb_out32(DDB_BAR3, 0xffffffff);
	ddb_out32(DDB_BAR4, 0xffffffff);
	ddb_out32(DDB_BAR5, 0xffffffff);
	ddb_out32(DDB_BAR6, 0xffffffff);
	ddb_out32(DDB_BAR7, 0xffffffff);
	ddb_out32(DDB_BAR8, 0xffffffff);

	/* ----------- switch PCI1 to PCI CONFIG space  ------------ */
	ddb_set_pdar(DDB_PCIW1, DDB_PCI_CONFIG_BASE, DDB_PCI_CONFIG_SIZE, 32, 0, 1);
	ddb_set_pmr(DDB_PCIINIT1, DDB_PCICMD_CFG, 0x0, DDB_PCI_ACCESS_32);

	/* ----- M1543 PCI setup ------ */

	/* we know M1543 PCI-ISA controller is at addr:18 */
	/* xxxx1010 makes USB at addr:13 and PMU at addr:14 */
	*(volatile unsigned char *) 0xa8040072 &= 0xf0;
	*(volatile unsigned char *) 0xa8040072 |= 0xa;

	/* setup USB interrupt to IRQ 9, (bit 0:3 - 0001)
	 * no IOCHRDY signal, (bit 7 - 1)
	 * M1543C & M7101 VID and Subsys Device ID are read-only (bit 6 - 1)
	 * Make USB Master INTAJ level to edge conversion (bit 4 - 1)
	 */
	*(unsigned char *) 0xa8040074 = 0xd1;

	/* setup PMU(SCI to IRQ 10 (bit 0:3 - 0011)
	 * SCI routing to IRQ 13 disabled (bit 7 - 1)
	 * SCI interrupt level to edge conversion bypassed (bit 4 - 0)
	 */
	*(unsigned char *) 0xa8040076 = 0x83;

	/* setup IDE controller
	 * enable IDE controller (bit 6 - 1)
	 * IDE IDSEL to be addr:24 (bit 4:5 - 11)
	 * no IDE ATA Secondary Bus Signal Pad Control (bit 3 - 0)
	 * no IDE ATA Primary Bus Signal Pad Control (bit 2 - 0)
	 * primary IRQ is 14, secondary is 15 (bit 1:0 - 01
	 */
	// *(unsigned char*)0xa8040058 = 0x71;
	// *(unsigned char*)0xa8040058 = 0x79;
	// *(unsigned char*)0xa8040058 = 0x74;              // use SIRQ, primary tri-state
	*(unsigned char *) 0xa8040058 = 0x75;	// primary tri-state

#if 0
	/* this is not necessary if M5229 does not use SIRQ */
	*(unsigned char *) 0xa8040044 = 0x0d;	// primary to IRQ 14
	*(unsigned char *) 0xa8040075 = 0x0d;	// secondary to IRQ 14
#endif

	/* enable IDE in the M5229 config register 0x50 (bit 0 - 1) */
	/* M5229 IDSEL is addr:24; see above setting */
	*(unsigned char *) 0xa9000050 |= 0x1;

	/* enable bus master (bit 2)  and IO decoding  (bit 0) */
	*(unsigned char *) 0xa9000004 |= 0x5;

	/* enable native, copied from arch/ppc/k2boot/head.S */
	/* TODO - need volatile, need to be portable */
	*(unsigned char *) 0xa9000009 = 0xff;

	/* ----- end of M1543 PCI setup ------ */

	/* ----- reset on-board ether chip  ------ */
	*((volatile u32 *) 0xa8020004) |= 1;	/* decode I/O */
	*((volatile u32 *) 0xa8020010) = 0;	/* set BAR address */

	/* send reset command */
	*((volatile u32 *) 0xa6000000) = 1;	/* do a soft reset */

	/* disable ether chip */
	*((volatile u32 *) 0xa8020004) = 0;	/* disable any decoding */

	/* put it into sleep */
	*((volatile u32 *) 0xa8020040) = 0x80000000;

	/* ----- end of reset on-board ether chip  ------ */

	/* ----------- switch PCI1 back to PCI MEM space  ------------ */
	ddb_set_pdar(DDB_PCIW1, DDB_PCI_MEM_BASE, DDB_PCI_MEM_SIZE, 32, 0, 1);
	ddb_set_pmr(DDB_PCIINIT1, DDB_PCICMD_MEM, DDB_PCI_MEM_BASE, DDB_PCI_ACCESS_32);
}
