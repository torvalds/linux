/*
 * arch/ppc/platforms/adir_setup.c
 *
 * Board setup routines for SBS Adirondack
 *
 * By Michael Sokolov <msokolov@ivan.Harhan.ORG>
 * based on the K2 version by Matt Porter <mporter@mvista.com>
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/major.h>
#include <linux/initrd.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <asm/todc.h>
#include <asm/bootinfo.h>

#include "adir.h"

extern void adir_init_IRQ(void);
extern int adir_get_irq(struct pt_regs *);
extern void adir_find_bridges(void);
extern unsigned long loops_per_jiffy;

static unsigned int cpu_750cx[16] = {
	5, 15, 14, 0, 4, 13, 0, 9, 6, 11, 8, 10, 16, 12, 7, 0
};

static int
adir_get_bus_speed(void)
{
	if (!(*((u_char *) ADIR_CLOCK_REG) & ADIR_CLOCK_REG_SEL133))
		return 100000000;
	else
		return 133333333;
}

static int
adir_get_cpu_speed(void)
{
	unsigned long hid1;
	int cpu_speed;

	hid1 = mfspr(SPRN_HID1) >> 28;

	hid1 = cpu_750cx[hid1];

	cpu_speed = adir_get_bus_speed()*hid1/2;
	return cpu_speed;
}

static void __init
adir_calibrate_decr(void)
{
	int freq, divisor = 4;

	/* determine processor bus speed */
	freq = adir_get_bus_speed();
	tb_ticks_per_jiffy = freq / HZ / divisor;
	tb_to_us = mulhwu_scale_factor(freq/divisor, 1000000);
}

static int
adir_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: SBS\n");
	seq_printf(m, "machine\t\t: Adirondack\n");
	seq_printf(m, "cpu speed\t: %dMhz\n", adir_get_cpu_speed()/1000000);
	seq_printf(m, "bus speed\t: %dMhz\n", adir_get_bus_speed()/1000000);
	seq_printf(m, "memory type\t: SDRAM\n");

	return 0;
}

extern char cmd_line[];

TODC_ALLOC();

static void __init
adir_setup_arch(void)
{
	unsigned int cpu;

	/* Setup TODC access */
	TODC_INIT(TODC_TYPE_MC146818, ADIR_NVRAM_RTC_ADDR, 0,
		  ADIR_NVRAM_RTC_DATA, 8);

	/* init to some ~sane value until calibrate_delay() runs */
        loops_per_jiffy = 50000000/HZ;

       /* Setup PCI host bridges */
        adir_find_bridges();

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_SDA1;
#endif

	/* Identify the system */
	printk("System Identification: SBS Adirondack - PowerPC 750CXe @ %d Mhz\n", adir_get_cpu_speed()/1000000);
	printk("SBS Adirondack port (C) 2001 SBS Technologies, Inc.\n");

	/* Identify the CPU manufacturer */
	cpu = mfspr(SPRN_PVR);
	printk("CPU manufacturer: IBM [rev=%04x]\n", (cpu & 0xffff));
}

static void
adir_restart(char *cmd)
{
	local_irq_disable();
	/* SRR0 has system reset vector, SRR1 has default MSR value */
	/* rfi restores MSR from SRR1 and sets the PC to the SRR0 value */
	__asm__ __volatile__
	("lis	3,0xfff0\n\t"
	 "ori	3,3,0x0100\n\t"
	 "mtspr	26,3\n\t"
	 "li	3,0\n\t"
	 "mtspr	27,3\n\t"
	 "rfi\n\t");
	for(;;);
}

static void
adir_power_off(void)
{
	for(;;);
}

static void
adir_halt(void)
{
	adir_restart(NULL);
}

static unsigned long __init
adir_find_end_of_memory(void)
{
	return boot_mem_size;
}

static void __init
adir_map_io(void)
{
	io_block_mapping(ADIR_PCI32_VIRT_IO_BASE, ADIR_PCI32_IO_BASE,
				ADIR_PCI32_VIRT_IO_SIZE, _PAGE_IO);
	io_block_mapping(ADIR_PCI64_VIRT_IO_BASE, ADIR_PCI64_IO_BASE,
				ADIR_PCI64_VIRT_IO_SIZE, _PAGE_IO);
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	/*
	 * On the Adirondack we use bi_recs and pass the pointer to them in R3.
	 */
	parse_bootinfo((struct bi_record *) (r3 + KERNELBASE));

	/* Remember, isa_io_base is virtual but isa_mem_base is physical! */
	isa_io_base = ADIR_PCI32_VIRT_IO_BASE;
	isa_mem_base = ADIR_PCI32_MEM_BASE;
	pci_dram_offset = ADIR_PCI_SYS_MEM_BASE;

	ppc_md.setup_arch = adir_setup_arch;
	ppc_md.show_cpuinfo = adir_show_cpuinfo;
	ppc_md.irq_canonicalize = NULL;
	ppc_md.init_IRQ = adir_init_IRQ;
	ppc_md.get_irq = adir_get_irq;
	ppc_md.init = NULL;

	ppc_md.find_end_of_memory = adir_find_end_of_memory;
	ppc_md.setup_io_mappings = adir_map_io;

	ppc_md.restart = adir_restart;
	ppc_md.power_off = adir_power_off;
	ppc_md.halt = adir_halt;

	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.nvram_read_val = todc_mc146818_read_val;
	ppc_md.nvram_write_val = todc_mc146818_write_val;
	ppc_md.calibrate_decr = adir_calibrate_decr;
}
