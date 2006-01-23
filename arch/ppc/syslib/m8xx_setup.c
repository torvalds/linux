/*
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  Modified for MBX using prep/chrp/pmac functions by Dan (dmalek@jlc.net)
 *  Further modified for generic 8xx by Dan.
 */

/*
 * bootup setup stuff..
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/major.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/ioport.h>
#include <linux/bootmem.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>

#if defined(CONFIG_MTD) && defined(CONFIG_MTD_PHYSMAP)
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#endif

#include <asm/mmu.h>
#include <asm/reg.h>
#include <asm/residual.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/mpc8xx.h>
#include <asm/8xx_immap.h>
#include <asm/machdep.h>
#include <asm/bootinfo.h>
#include <asm/time.h>
#include <asm/xmon.h>
#include <asm/ppc_sys.h>

#include "ppc8xx_pic.h"

#ifdef CONFIG_MTD_PHYSMAP
#define MPC8xxADS_BANK_WIDTH 4
#endif

#define MPC8xxADS_U_BOOT_SIZE          0x80000
#define MPC8xxADS_FREE_AREA_OFFSET     MPC8xxADS_U_BOOT_SIZE

#if defined(CONFIG_MTD_PARTITIONS)
 /*
   NOTE: bank width and interleave relative to the installed flash
   should have been chosen within MTD_CFI_GEOMETRY options.
 */
static struct mtd_partition mpc8xxads_partitions[] = {
	{
		.name = "bootloader",
		.size = MPC8xxADS_U_BOOT_SIZE,
		.offset = 0,
		.mask_flags   = MTD_WRITEABLE,  /* force read-only */
	}, {
		.name = "User FS",
		.offset = MPC8xxADS_FREE_AREA_OFFSET
	}
};

#define mpc8xxads_part_num (sizeof (mpc8xxads_partitions) / sizeof (mpc8xxads_partitions[0]))

#endif

static int m8xx_set_rtc_time(unsigned long time);
static unsigned long m8xx_get_rtc_time(void);
void m8xx_calibrate_decr(void);

unsigned char __res[sizeof(bd_t)];

extern void m8xx_ide_init(void);

extern unsigned long find_available_memory(void);
extern void m8xx_cpm_reset(void);
extern void m8xx_wdt_handler_install(bd_t *bp);
extern void rpxfb_alloc_pages(void);
extern void cpm_interrupt_init(void);

void __attribute__ ((weak))
board_init(void)
{
}

void __init
m8xx_setup_arch(void)
{
#if defined(CONFIG_MTD) && defined(CONFIG_MTD_PHYSMAP)
	bd_t *binfo = (bd_t *)__res;
#endif

	/* Reset the Communication Processor Module.
	*/
	m8xx_cpm_reset();

#ifdef CONFIG_FB_RPX
	rpxfb_alloc_pages();
#endif

#ifdef notdef
	ROOT_DEV = Root_HDA1; /* hda1 */
#endif

#ifdef CONFIG_BLK_DEV_INITRD
#if 0
	ROOT_DEV = Root_FD0; /* floppy */
	rd_prompt = 1;
	rd_doload = 1;
	rd_image_start = 0;
#endif
#if 0	/* XXX this may need to be updated for the new bootmem stuff,
	   or possibly just deleted (see set_phys_avail() in init.c).
	   - paulus. */
	/* initrd_start and size are setup by boot/head.S and kernel/head.S */
	if ( initrd_start )
	{
		if (initrd_end > *memory_end_p)
		{
			printk("initrd extends beyond end of memory "
			       "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			       initrd_end,*memory_end_p);
			initrd_start = 0;
		}
	}
#endif
#endif

#if defined (CONFIG_MPC86XADS) || defined (CONFIG_MPC885ADS)
#if defined(CONFIG_MTD_PHYSMAP)
       physmap_configure(binfo->bi_flashstart, binfo->bi_flashsize,
                                               MPC8xxADS_BANK_WIDTH, NULL);
#ifdef CONFIG_MTD_PARTITIONS
       physmap_set_partitions(mpc8xxads_partitions, mpc8xxads_part_num);
#endif /* CONFIG_MTD_PARTITIONS */
#endif /* CONFIG_MTD_PHYSMAP */
#endif

	board_init();
}

void
abort(void)
{
#ifdef CONFIG_XMON
	xmon(0);
#endif
	machine_restart(NULL);

	/* not reached */
	for (;;);
}

/* A place holder for time base interrupts, if they are ever enabled. */
irqreturn_t timebase_interrupt(int irq, void * dev, struct pt_regs * regs)
{
	printk ("timebase_interrupt()\n");

	return IRQ_HANDLED;
}

static struct irqaction tbint_irqaction = {
	.handler = timebase_interrupt,
	.mask = CPU_MASK_NONE,
	.name = "tbint",
};

/* per-board overridable init_internal_rtc() function. */
void __init __attribute__ ((weak))
init_internal_rtc(void)
{
	/* Disable the RTC one second and alarm interrupts. */
	clrbits16(&((immap_t *)IMAP_ADDR)->im_sit.sit_rtcsc, (RTCSC_SIE | RTCSC_ALE));

	/* Enable the RTC */
	setbits16(&((immap_t *)IMAP_ADDR)->im_sit.sit_rtcsc, (RTCSC_RTF | RTCSC_RTE));

}

/* The decrementer counts at the system (internal) clock frequency divided by
 * sixteen, or external oscillator divided by four.  We force the processor
 * to use system clock divided by sixteen.
 */
void __init m8xx_calibrate_decr(void)
{
	bd_t	*binfo = (bd_t *)__res;
	int freq, fp, divisor;

	/* Unlock the SCCR. */
	out_be32(&((immap_t *)IMAP_ADDR)->im_clkrstk.cark_sccrk, ~KAPWR_KEY);
	out_be32(&((immap_t *)IMAP_ADDR)->im_clkrstk.cark_sccrk, KAPWR_KEY);

	/* Force all 8xx processors to use divide by 16 processor clock. */
	setbits32(&((immap_t *)IMAP_ADDR)->im_clkrst.car_sccr, 0x02000000);
	/* Processor frequency is MHz.
	 * The value 'fp' is the number of decrementer ticks per second.
	 */
	fp = binfo->bi_intfreq / 16;
	freq = fp*60;	/* try to make freq/1e6 an integer */
        divisor = 60;
        printk("Decrementer Frequency = %d/%d\n", freq, divisor);
        tb_ticks_per_jiffy = freq / HZ / divisor;
	tb_to_us = mulhwu_scale_factor(freq / divisor, 1000000);

	/* Perform some more timer/timebase initialization.  This used
	 * to be done elsewhere, but other changes caused it to get
	 * called more than once....that is a bad thing.
	 *
	 * First, unlock all of the registers we are going to modify.
	 * To protect them from corruption during power down, registers
	 * that are maintained by keep alive power are "locked".  To
	 * modify these registers we have to write the key value to
	 * the key location associated with the register.
	 * Some boards power up with these unlocked, while others
	 * are locked.  Writing anything (including the unlock code?)
	 * to the unlocked registers will lock them again.  So, here
	 * we guarantee the registers are locked, then we unlock them
	 * for our use.
	 */
	out_be32(&((immap_t *)IMAP_ADDR)->im_sitk.sitk_tbscrk, ~KAPWR_KEY);
	out_be32(&((immap_t *)IMAP_ADDR)->im_sitk.sitk_rtcsck, ~KAPWR_KEY);
	out_be32(&((immap_t *)IMAP_ADDR)->im_sitk.sitk_tbk, ~KAPWR_KEY);
	out_be32(&((immap_t *)IMAP_ADDR)->im_sitk.sitk_tbscrk, KAPWR_KEY);
	out_be32(&((immap_t *)IMAP_ADDR)->im_sitk.sitk_rtcsck, KAPWR_KEY);
	out_be32(&((immap_t *)IMAP_ADDR)->im_sitk.sitk_tbk, KAPWR_KEY);

	init_internal_rtc();

	/* Enabling the decrementer also enables the timebase interrupts
	 * (or from the other point of view, to get decrementer interrupts
	 * we have to enable the timebase).  The decrementer interrupt
	 * is wired into the vector table, nothing to do here for that.
	 */
	out_be16(&((immap_t *)IMAP_ADDR)->im_sit.sit_tbscr, (mk_int_int_mask(DEC_INTERRUPT) << 8) | (TBSCR_TBF | TBSCR_TBE));

	if (setup_irq(DEC_INTERRUPT, &tbint_irqaction))
		panic("Could not allocate timer IRQ!");

#ifdef CONFIG_8xx_WDT
	/* Install watchdog timer handler early because it might be
	 * already enabled by the bootloader
	 */
	m8xx_wdt_handler_install(binfo);
#endif
}

/* The RTC on the MPC8xx is an internal register.
 * We want to protect this during power down, so we need to unlock,
 * modify, and re-lock.
 */
static int
m8xx_set_rtc_time(unsigned long time)
{
	out_be32(&((immap_t *)IMAP_ADDR)->im_sitk.sitk_rtck, KAPWR_KEY);
	out_be32(&((immap_t *)IMAP_ADDR)->im_sit.sit_rtc, time);
	out_be32(&((immap_t *)IMAP_ADDR)->im_sitk.sitk_rtck, ~KAPWR_KEY);
	return(0);
}

static unsigned long
m8xx_get_rtc_time(void)
{
	/* Get time from the RTC. */
	return (unsigned long) in_be32(&((immap_t *)IMAP_ADDR)->im_sit.sit_rtc);
}

static void
m8xx_restart(char *cmd)
{
	__volatile__ unsigned char dummy;

	local_irq_disable();

	setbits32(&((immap_t *)IMAP_ADDR)->im_clkrst.car_plprcr, 0x00000080);
	/* Clear the ME bit in MSR to cause checkstop on machine check
	*/
	mtmsr(mfmsr() & ~0x1000);

	dummy = in_8(&((immap_t *)IMAP_ADDR)->im_clkrst.res[0]);
	printk("Restart failed\n");
	while(1);
}

static void
m8xx_power_off(void)
{
   m8xx_restart(NULL);
}

static void
m8xx_halt(void)
{
   m8xx_restart(NULL);
}


static int
m8xx_show_percpuinfo(struct seq_file *m, int i)
{
	bd_t	*bp;

	bp = (bd_t *)__res;

	seq_printf(m, "clock\t\t: %uMHz\n"
		   "bus clock\t: %uMHz\n",
		   bp->bi_intfreq / 1000000,
		   bp->bi_busfreq / 1000000);

	return 0;
}

#ifdef CONFIG_PCI
static struct irqaction mbx_i8259_irqaction = {
	.handler = mbx_i8259_action,
	.mask = CPU_MASK_NONE,
	.name = "i8259 cascade",
};
#endif

/* Initialize the internal interrupt controller.  The number of
 * interrupts supported can vary with the processor type, and the
 * 82xx family can have up to 64.
 * External interrupts can be either edge or level triggered, and
 * need to be initialized by the appropriate driver.
 */
static void __init
m8xx_init_IRQ(void)
{
	int i;

	for (i = SIU_IRQ_OFFSET ; i < SIU_IRQ_OFFSET + NR_SIU_INTS ; i++)
		irq_desc[i].handler = &ppc8xx_pic;

	cpm_interrupt_init();

#if defined(CONFIG_PCI)
	for (i = I8259_IRQ_OFFSET ; i < I8259_IRQ_OFFSET + NR_8259_INTS ; i++)
		irq_desc[i].handler = &i8259_pic;

	i8259_pic_irq_offset = I8259_IRQ_OFFSET;
	i8259_init(0);

	/* The i8259 cascade interrupt must be level sensitive. */

	clrbits32(&((immap_t *)IMAP_ADDR)->im_siu_conf.sc_siel, (0x80000000 >> ISA_BRIDGE_INT));
	if (setup_irq(ISA_BRIDGE_INT, &mbx_i8259_irqaction))
		enable_irq(ISA_BRIDGE_INT);
#endif	/* CONFIG_PCI */
}

/* -------------------------------------------------------------------- */

/*
 * This is a big hack right now, but it may turn into something real
 * someday.
 *
 * For the 8xx boards (at this time anyway), there is nothing to initialize
 * associated the PROM.  Rather than include all of the prom.c
 * functions in the image just to get prom_init, all we really need right
 * now is the initialization of the physical memory region.
 */
static unsigned long __init
m8xx_find_end_of_memory(void)
{
	bd_t	*binfo;
	extern unsigned char __res[];

	binfo = (bd_t *)__res;

	return binfo->bi_memsize;
}

/*
 * Now map in some of the I/O space that is generically needed
 * or shared with multiple devices.
 * All of this fits into the same 4Mbyte region, so it only
 * requires one page table page.  (or at least it used to  -- paulus)
 */
static void __init
m8xx_map_io(void)
{
        io_block_mapping(IMAP_ADDR, IMAP_ADDR, IMAP_SIZE, _PAGE_IO);
#ifdef CONFIG_MBX
        io_block_mapping(NVRAM_ADDR, NVRAM_ADDR, NVRAM_SIZE, _PAGE_IO);
        io_block_mapping(MBX_CSR_ADDR, MBX_CSR_ADDR, MBX_CSR_SIZE, _PAGE_IO);
        io_block_mapping(PCI_CSR_ADDR, PCI_CSR_ADDR, PCI_CSR_SIZE, _PAGE_IO);

	/* Map some of the PCI/ISA I/O space to get the IDE interface.
	*/
        io_block_mapping(PCI_ISA_IO_ADDR, PCI_ISA_IO_ADDR, 0x4000, _PAGE_IO);
        io_block_mapping(PCI_IDE_ADDR, PCI_IDE_ADDR, 0x4000, _PAGE_IO);
#endif
#if defined(CONFIG_RPXLITE) || defined(CONFIG_RPXCLASSIC)
	io_block_mapping(RPX_CSR_ADDR, RPX_CSR_ADDR, RPX_CSR_SIZE, _PAGE_IO);
#if !defined(CONFIG_PCI)
	io_block_mapping(_IO_BASE,_IO_BASE,_IO_BASE_SIZE, _PAGE_IO);
#endif
#endif
#if defined(CONFIG_HTDMSOUND) || defined(CONFIG_RPXTOUCH) || defined(CONFIG_FB_RPX)
	io_block_mapping(HIOX_CSR_ADDR, HIOX_CSR_ADDR, HIOX_CSR_SIZE, _PAGE_IO);
#endif
#ifdef CONFIG_FADS
	io_block_mapping(BCSR_ADDR, BCSR_ADDR, BCSR_SIZE, _PAGE_IO);
#endif
#ifdef CONFIG_PCI
        io_block_mapping(PCI_CSR_ADDR, PCI_CSR_ADDR, PCI_CSR_SIZE, _PAGE_IO);
#endif
#if defined(CONFIG_NETTA)
	io_block_mapping(_IO_BASE,_IO_BASE,_IO_BASE_SIZE, _PAGE_IO);
#endif
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
		unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	if ( r3 )
		memcpy( (void *)__res,(void *)(r3+KERNELBASE), sizeof(bd_t) );

#ifdef CONFIG_PCI
	m8xx_setup_pci_ptrs();
#endif

#ifdef CONFIG_BLK_DEV_INITRD
	/* take care of initrd if we have one */
	if ( r4 )
	{
		initrd_start = r4 + KERNELBASE;
		initrd_end = r5 + KERNELBASE;
	}
#endif /* CONFIG_BLK_DEV_INITRD */
	/* take care of cmd line */
	if ( r6 )
	{
		*(char *)(r7+KERNELBASE) = 0;
		strcpy(cmd_line, (char *)(r6+KERNELBASE));
	}

	identify_ppc_sys_by_name(BOARD_CHIP_NAME);

	ppc_md.setup_arch		= m8xx_setup_arch;
	ppc_md.show_percpuinfo		= m8xx_show_percpuinfo;
	ppc_md.init_IRQ			= m8xx_init_IRQ;
	ppc_md.get_irq			= m8xx_get_irq;
	ppc_md.init			= NULL;

	ppc_md.restart			= m8xx_restart;
	ppc_md.power_off		= m8xx_power_off;
	ppc_md.halt			= m8xx_halt;

	ppc_md.time_init		= NULL;
	ppc_md.set_rtc_time		= m8xx_set_rtc_time;
	ppc_md.get_rtc_time		= m8xx_get_rtc_time;
	ppc_md.calibrate_decr		= m8xx_calibrate_decr;

	ppc_md.find_end_of_memory	= m8xx_find_end_of_memory;
	ppc_md.setup_io_mappings	= m8xx_map_io;

#if defined(CONFIG_BLK_DEV_MPC8xx_IDE)
	m8xx_ide_init();
#endif
}
