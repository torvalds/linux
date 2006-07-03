/*
 *
 *    Copyright (c) 1999-2000 Grant Erickson <grant@lcse.umn.edu>
 *
 *    Copyright 2000-2001 MontaVista Software Inc.
 *      Completed implementation.
 *      Author: MontaVista Software, Inc.  <source@mvista.com>
 *              Frank Rowand <frank_rowand@mvista.com>
 *              Debbie Chu   <debbie_chu@mvista.com>
 *	Further modifications by Armin Kuster
 *
 *    Module name: ppc4xx_setup.c
 *
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/spinlock.h>
#include <linux/reboot.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/initrd.h>
#include <linux/pci.h>
#include <linux/rtc.h>
#include <linux/console.h>
#include <linux/ide.h>
#include <linux/serial_reg.h>
#include <linux/seq_file.h>

#include <asm/system.h>
#include <asm/processor.h>
#include <asm/machdep.h>
#include <asm/page.h>
#include <asm/kgdb.h>
#include <asm/ibm4xx.h>
#include <asm/time.h>
#include <asm/todc.h>
#include <asm/ppc4xx_pic.h>
#include <asm/pci-bridge.h>
#include <asm/bootinfo.h>

#include <syslib/gen550.h>

/* Function Prototypes */
extern void abort(void);
extern void ppc4xx_find_bridges(void);

/* Global Variables */
bd_t __res;

void __init
ppc4xx_setup_arch(void)
{
#if !defined(CONFIG_BDI_SWITCH)
	/*
	 * The Abatron BDI JTAG debugger does not tolerate others
	 * mucking with the debug registers.
	 */
        mtspr(SPRN_DBCR0, (DBCR0_IDM));
	mtspr(SPRN_DBSR, 0xffffffff);
#endif

	/* Setup PCI host bridges */
#ifdef CONFIG_PCI
	ppc4xx_find_bridges();
#endif
}

/*
 *   This routine pretty-prints the platform's internal CPU clock
 *   frequencies into the buffer for usage in /proc/cpuinfo.
 */

static int
ppc4xx_show_percpuinfo(struct seq_file *m, int i)
{
	seq_printf(m, "clock\t\t: %ldMHz\n", (long)__res.bi_intfreq / 1000000);

	return 0;
}

/*
 *   This routine pretty-prints the platform's internal bus clock
 *   frequencies into the buffer for usage in /proc/cpuinfo.
 */
static int
ppc4xx_show_cpuinfo(struct seq_file *m)
{
	bd_t *bip = &__res;

	seq_printf(m, "machine\t\t: %s\n", PPC4xx_MACHINE_NAME);
	seq_printf(m, "plb bus clock\t: %ldMHz\n",
		   (long) bip->bi_busfreq / 1000000);
#ifdef CONFIG_PCI
	seq_printf(m, "pci bus clock\t: %dMHz\n",
		   bip->bi_pci_busfreq / 1000000);
#endif

	return 0;
}

/*
 * Return the virtual address representing the top of physical RAM.
 */
static unsigned long __init
ppc4xx_find_end_of_memory(void)
{
	return ((unsigned long) __res.bi_memsize);
}

void __init
ppc4xx_map_io(void)
{
	io_block_mapping(PPC4xx_ONB_IO_VADDR,
			 PPC4xx_ONB_IO_PADDR, PPC4xx_ONB_IO_SIZE, _PAGE_IO);
#ifdef CONFIG_PCI
	io_block_mapping(PPC4xx_PCI_IO_VADDR,
			 PPC4xx_PCI_IO_PADDR, PPC4xx_PCI_IO_SIZE, _PAGE_IO);
	io_block_mapping(PPC4xx_PCI_CFG_VADDR,
			 PPC4xx_PCI_CFG_PADDR, PPC4xx_PCI_CFG_SIZE, _PAGE_IO);
	io_block_mapping(PPC4xx_PCI_LCFG_VADDR,
			 PPC4xx_PCI_LCFG_PADDR, PPC4xx_PCI_LCFG_SIZE, _PAGE_IO);
#endif
}

void __init
ppc4xx_init_IRQ(void)
{
	ppc4xx_pic_init();
}

static void
ppc4xx_restart(char *cmd)
{
	printk("%s\n", cmd);
	abort();
}

static void
ppc4xx_power_off(void)
{
	printk("System Halted\n");
	local_irq_disable();
	while (1) ;
}

static void
ppc4xx_halt(void)
{
	printk("System Halted\n");
	local_irq_disable();
	while (1) ;
}

/*
 * This routine retrieves the internal processor frequency from the board
 * information structure, sets up the kernel timer decrementer based on
 * that value, enables the 4xx programmable interval timer (PIT) and sets
 * it up for auto-reload.
 */
static void __init
ppc4xx_calibrate_decr(void)
{
	unsigned int freq;
	bd_t *bip = &__res;

#if defined(CONFIG_WALNUT) || defined(CONFIG_SYCAMORE)
	/* Walnut boot rom sets DCR CHCR1 (aka CPC0_CR1) bit CETE to 1 */
	mtdcr(DCRN_CHCR1, mfdcr(DCRN_CHCR1) & ~CHR1_CETE);
#endif
	freq = bip->bi_tbfreq;
	tb_ticks_per_jiffy = freq / HZ;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);

	/* Set the time base to zero.
	   ** At 200 Mhz, time base will rollover in ~2925 years.
	 */

	mtspr(SPRN_TBWL, 0);
	mtspr(SPRN_TBWU, 0);

	/* Clear any pending timer interrupts */

	mtspr(SPRN_TSR, TSR_ENW | TSR_WIS | TSR_PIS | TSR_FIS);
	mtspr(SPRN_TCR, TCR_PIE | TCR_ARE);

	/* Set the PIT reload value and just let it run. */
	mtspr(SPRN_PIT, tb_ticks_per_jiffy);
}

/*
 * IDE stuff.
 * should be generic for every IDE PCI chipset
 */
#if defined(CONFIG_PCI) && defined(CONFIG_IDE)
static void
ppc4xx_ide_init_hwif_ports(hw_regs_t * hw, unsigned long data_port,
			   unsigned long ctrl_port, int *irq)
{
	int i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; ++i)
		hw->io_ports[i] = data_port + i - IDE_DATA_OFFSET;

	hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
}
#endif /* defined(CONFIG_PCI) && defined(CONFIG_IDE) */

TODC_ALLOC();

/*
 * Input(s):
 *   r3 - Optional pointer to a board information structure.
 *   r4 - Optional pointer to the physical starting address of the init RAM
 *        disk.
 *   r5 - Optional pointer to the physical ending address of the init RAM
 *        disk.
 *   r6 - Optional pointer to the physical starting address of any kernel
 *        command-line parameters.
 *   r7 - Optional pointer to the physical ending address of any kernel
 *        command-line parameters.
 */
void __init
ppc4xx_init(unsigned long r3, unsigned long r4, unsigned long r5,
	    unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	/*
	 * If we were passed in a board information, copy it into the
	 * residual data area.
	 */
	if (r3)
		__res = *(bd_t *)(r3 + KERNELBASE);

#if defined(CONFIG_BLK_DEV_INITRD)
	/*
	 * If the init RAM disk has been configured in, and there's a valid
	 * starting address for it, set it up.
	 */
	if (r4) {
		initrd_start = r4 + KERNELBASE;
		initrd_end = r5 + KERNELBASE;
	}
#endif				/* CONFIG_BLK_DEV_INITRD */

	/* Copy the kernel command line arguments to a safe place. */

	if (r6) {
		*(char *) (r7 + KERNELBASE) = 0;
		strcpy(cmd_line, (char *) (r6 + KERNELBASE));
	}

	/* Initialize machine-dependent vectors */

	ppc_md.setup_arch = ppc4xx_setup_arch;
	ppc_md.show_percpuinfo = ppc4xx_show_percpuinfo;
	ppc_md.show_cpuinfo = ppc4xx_show_cpuinfo;
	ppc_md.init_IRQ = ppc4xx_init_IRQ;

	ppc_md.restart = ppc4xx_restart;
	ppc_md.power_off = ppc4xx_power_off;
	ppc_md.halt = ppc4xx_halt;

	ppc_md.calibrate_decr = ppc4xx_calibrate_decr;

	ppc_md.find_end_of_memory = ppc4xx_find_end_of_memory;
	ppc_md.setup_io_mappings = ppc4xx_map_io;

#ifdef CONFIG_SERIAL_TEXT_DEBUG
	ppc_md.progress = gen550_progress;
#endif

#if defined(CONFIG_PCI) && defined(CONFIG_IDE)
	ppc_ide_md.ide_init_hwif = ppc4xx_ide_init_hwif_ports;
#endif /* defined(CONFIG_PCI) && defined(CONFIG_IDE) */
}

/* Called from machine_check_exception */
void platform_machine_check(struct pt_regs *regs)
{
#if defined(DCRN_PLB0_BEAR)
	printk("PLB0: BEAR= 0x%08x ACR=   0x%08x BESR=  0x%08x\n",
	    mfdcr(DCRN_PLB0_BEAR), mfdcr(DCRN_PLB0_ACR),
	    mfdcr(DCRN_PLB0_BESR));
#endif
#if defined(DCRN_POB0_BEAR)
	printk("PLB0 to OPB: BEAR= 0x%08x BESR0= 0x%08x BESR1= 0x%08x\n",
	    mfdcr(DCRN_POB0_BEAR), mfdcr(DCRN_POB0_BESR0),
	    mfdcr(DCRN_POB0_BESR1));
#endif

}
