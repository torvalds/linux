/*
 *  arch/ppc/platforms/apus_setup.c
 *
 *  Copyright (C) 1998, 1999  Jesper Skov
 *
 *  Basically what is needed to replace functionality found in
 *  arch/m68k allowing Amiga drivers to work under APUS.
 *  Bits of code and/or ideas from arch/m68k and arch/ppc files.
 *
 * TODO:
 *  This file needs a *really* good cleanup. Restructure and optimize.
 *  Make sure it can be compiled for non-APUS configs. Begin to move
 *  Amiga specific stuff into mach/amiga.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/seq_file.h>

/* Needs INITSERIAL call in head.S! */
#undef APUS_DEBUG

#include <asm/bootinfo.h>
#include <asm/setup.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/amigappc.h>
#include <asm/pgtable.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/time.h>

unsigned long m68k_machtype;
char debug_device[6] = "";

extern void amiga_init_IRQ(void);

extern void apus_setup_pci_ptrs(void);

void (*mach_sched_init) (void (*handler)(int, void *, struct pt_regs *)) __initdata = NULL;
/* machine dependent irq functions */
void (*mach_init_IRQ) (void) __initdata = NULL;
void (*(*mach_default_handler)[]) (int, void *, struct pt_regs *) = NULL;
void (*mach_get_model) (char *model) = NULL;
int (*mach_get_hardware_list) (char *buffer) = NULL;
int (*mach_get_irq_list) (struct seq_file *, void *) = NULL;
void (*mach_process_int) (int, struct pt_regs *) = NULL;
/* machine dependent timer functions */
unsigned long (*mach_gettimeoffset) (void);
void (*mach_gettod) (int*, int*, int*, int*, int*, int*);
int (*mach_hwclk) (int, struct hwclk_time*) = NULL;
int (*mach_set_clock_mmss) (unsigned long) = NULL;
void (*mach_reset)( void );
long mach_max_dma_address = 0x00ffffff; /* default set to the lower 16MB */
#ifdef CONFIG_HEARTBEAT
void (*mach_heartbeat) (int) = NULL;
extern void apus_heartbeat (void);
#endif

extern unsigned long amiga_model;
extern unsigned decrementer_count;/* count value for 1e6/HZ microseconds */
extern unsigned count_period_num; /* 1 decrementer count equals */
extern unsigned count_period_den; /* count_period_num / count_period_den us */

int num_memory = 0;
struct mem_info memory[NUM_MEMINFO];/* memory description */
/* FIXME: Duplicate memory data to avoid conflicts with m68k shared code. */
int m68k_realnum_memory = 0;
struct mem_info m68k_memory[NUM_MEMINFO];/* memory description */

struct mem_info ramdisk;

extern void config_amiga(void);

static int __60nsram = 0;

/* for cpuinfo */
static int __bus_speed = 0;
static int __speed_test_failed = 0;

/********************************************** COMPILE PROTECTION */
/* Provide some stubs that links to Amiga specific functions.
 * This allows CONFIG_APUS to be removed from generic PPC files while
 * preventing link errors for other PPC targets.
 */
unsigned long apus_get_rtc_time(void)
{
#ifdef CONFIG_APUS
	extern unsigned long m68k_get_rtc_time(void);

	return m68k_get_rtc_time ();
#else
	return 0;
#endif
}

int apus_set_rtc_time(unsigned long nowtime)
{
#ifdef CONFIG_APUS
	extern int m68k_set_rtc_time(unsigned long nowtime);

	return m68k_set_rtc_time (nowtime);
#else
	return 0;
#endif
}

/*********************************************************** SETUP */
/* From arch/m68k/kernel/setup.c. */
void __init apus_setup_arch(void)
{
#ifdef CONFIG_APUS
	extern char cmd_line[];
	int i;
	char *p, *q;

	/* Let m68k-shared code know it should do the Amiga thing. */
	m68k_machtype = MACH_AMIGA;

	/* Parse the command line for arch-specific options.
	 * For the m68k, this is currently only "debug=xxx" to enable printing
	 * certain kernel messages to some machine-specific device.  */
	for( p = cmd_line; p && *p; ) {
	    i = 0;
	    if (!strncmp( p, "debug=", 6 )) {
		    strlcpy( debug_device, p+6, sizeof(debug_device) );
		    if ((q = strchr( debug_device, ' ' ))) *q = 0;
		    i = 1;
	    } else if (!strncmp( p, "60nsram", 7 )) {
		    APUS_WRITE (APUS_REG_WAITSTATE,
				REGWAITSTATE_SETRESET
				|REGWAITSTATE_PPCR
				|REGWAITSTATE_PPCW);
		    __60nsram = 1;
		    i = 1;
	    }

	    if (i) {
		/* option processed, delete it */
		if ((q = strchr( p, ' ' )))
		    strcpy( p, q+1 );
		else
		    *p = 0;
	    } else {
		if ((p = strchr( p, ' ' ))) ++p;
	    }
	}

	config_amiga();

#if 0 /* Enable for logging - also include logging.o in Makefile rule */
	{
#define LOG_SIZE 4096
		void* base;

		/* Throw away some memory - the P5 firmare stomps on top
		 * of CHIP memory during bootup.
		 */
		amiga_chip_alloc(0x1000);

		base = amiga_chip_alloc(LOG_SIZE+sizeof(klog_data_t));
		LOG_INIT(base, base+sizeof(klog_data_t), LOG_SIZE);
	}
#endif
#endif
}

int
apus_show_cpuinfo(struct seq_file *m)
{
	extern int __map_without_bats;
	extern unsigned long powerup_PCI_present;

	seq_printf(m, "machine\t\t: Amiga\n");
	seq_printf(m, "bus speed\t: %d%s", __bus_speed,
		   (__speed_test_failed) ? " [failed]\n" : "\n");
	seq_printf(m, "using BATs\t: %s\n",
		   (__map_without_bats) ? "No" : "Yes");
	seq_printf(m, "ram speed\t: %dns\n", (__60nsram) ? 60 : 70);
	seq_printf(m, "PCI bridge\t: %s\n",
		   (powerup_PCI_present) ? "Yes" : "No");
	return 0;
}

static void get_current_tb(unsigned long long *time)
{
	__asm __volatile ("1:mftbu 4      \n\t"
			  "  mftb  5      \n\t"
			  "  mftbu 6      \n\t"
			  "  cmpw  4,6    \n\t"
			  "  bne   1b     \n\t"
			  "  stw   4,0(%0)\n\t"
			  "  stw   5,4(%0)\n\t"
			  :
			  : "r" (time)
			  : "r4", "r5", "r6");
}


void apus_calibrate_decr(void)
{
#ifdef CONFIG_APUS
	unsigned long freq;

	/* This algorithm for determining the bus speed was
           contributed by Ralph Schmidt. */
	unsigned long long start, stop;
	int bus_speed;
	int speed_test_failed = 0;

	{
		unsigned long loop = amiga_eclock / 10;

		get_current_tb (&start);
		while (loop--) {
			unsigned char tmp;

			tmp = ciaa.pra;
		}
		get_current_tb (&stop);
	}

	bus_speed = (((unsigned long)(stop-start))*10*4) / 1000000;
	if (AMI_1200 == amiga_model)
		bus_speed /= 2;

	if ((bus_speed >= 47) && (bus_speed < 53)) {
		bus_speed = 50;
		freq = 12500000;
	} else if ((bus_speed >= 57) && (bus_speed < 63)) {
		bus_speed = 60;
		freq = 15000000;
	} else if ((bus_speed >= 63) && (bus_speed < 69)) {
		bus_speed = 67;
		freq = 16666667;
	} else {
		printk ("APUS: Unable to determine bus speed (%d). "
			"Defaulting to 50MHz", bus_speed);
		bus_speed = 50;
		freq = 12500000;
		speed_test_failed = 1;
	}

	/* Ease diagnostics... */
	{
		extern int __map_without_bats;
		extern unsigned long powerup_PCI_present;

		printk ("APUS: BATs=%d, BUS=%dMHz",
			(__map_without_bats) ? 0 : 1,
			bus_speed);
		if (speed_test_failed)
			printk ("[FAILED - please report]");

		printk (", RAM=%dns, PCI bridge=%d\n",
			(__60nsram) ? 60 : 70,
			(powerup_PCI_present) ? 1 : 0);

		/* print a bit more if asked politely... */
		if (!(ciaa.pra & 0x40)){
			extern unsigned int bat_addrs[4][3];
			int b;
			for (b = 0; b < 4; ++b) {
				printk ("APUS: BAT%d ", b);
				printk ("%08x-%08x -> %08x\n",
					bat_addrs[b][0],
					bat_addrs[b][1],
					bat_addrs[b][2]);
			}
		}

	}

        printk("time_init: decrementer frequency = %lu.%.6lu MHz\n",
	       freq/1000000, freq%1000000);
	tb_ticks_per_jiffy = freq / HZ;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);

	__bus_speed = bus_speed;
	__speed_test_failed = speed_test_failed;
#endif
}

void arch_gettod(int *year, int *mon, int *day, int *hour,
		 int *min, int *sec)
{
#ifdef CONFIG_APUS
	if (mach_gettod)
		mach_gettod(year, mon, day, hour, min, sec);
	else
		*year = *mon = *day = *hour = *min = *sec = 0;
#endif
}

/* for "kbd-reset" cmdline param */
__init
void kbd_reset_setup(char *str, int *ints)
{
}

/*********************************************************** MEMORY */
#define KMAP_MAX 32
unsigned long kmap_chunks[KMAP_MAX*3];
int kmap_chunk_count = 0;

/* From pgtable.h */
static __inline__ pte_t *my_find_pte(struct mm_struct *mm,unsigned long va)
{
	pgd_t *dir = 0;
	pmd_t *pmd = 0;
	pte_t *pte = 0;

	va &= PAGE_MASK;

	dir = pgd_offset( mm, va );
	if (dir)
	{
		pmd = pmd_offset(dir, va & PAGE_MASK);
		if (pmd && pmd_present(*pmd))
		{
			pte = pte_offset(pmd, va);
		}
	}
	return pte;
}


/* Again simulating an m68k/mm/kmap.c function. */
void kernel_set_cachemode( unsigned long address, unsigned long size,
			   unsigned int cmode )
{
	unsigned long mask, flags;

	switch (cmode)
	{
	case IOMAP_FULL_CACHING:
		mask = ~(_PAGE_NO_CACHE | _PAGE_GUARDED);
		flags = 0;
		break;
	case IOMAP_NOCACHE_SER:
		mask = ~0;
		flags = (_PAGE_NO_CACHE | _PAGE_GUARDED);
		break;
	default:
		panic ("kernel_set_cachemode() doesn't support mode %d\n",
		       cmode);
		break;
	}

	size /= PAGE_SIZE;
	address &= PAGE_MASK;
	while (size--)
	{
		pte_t *pte;

		pte = my_find_pte(&init_mm, address);
		if ( !pte )
		{
			printk("pte NULL in kernel_set_cachemode()\n");
			return;
		}

                pte_val (*pte) &= mask;
                pte_val (*pte) |= flags;
                flush_tlb_page(find_vma(&init_mm,address),address);

		address += PAGE_SIZE;
	}
}

unsigned long mm_ptov (unsigned long paddr)
{
	unsigned long ret;
	if (paddr < 16*1024*1024)
		ret = ZTWO_VADDR(paddr);
	else {
		int i;

		for (i = 0; i < kmap_chunk_count;){
			unsigned long phys = kmap_chunks[i++];
			unsigned long size = kmap_chunks[i++];
			unsigned long virt = kmap_chunks[i++];
			if (paddr >= phys
			    && paddr < (phys + size)){
				ret = virt + paddr - phys;
				goto exit;
			}
		}

		ret = (unsigned long) __va(paddr);
	}
exit:
#ifdef DEBUGPV
	printk ("PTOV(%lx)=%lx\n", paddr, ret);
#endif
	return ret;
}

int mm_end_of_chunk (unsigned long addr, int len)
{
	if (memory[0].addr + memory[0].size == addr + len)
		return 1;
	return 0;
}

/*********************************************************** CACHE */

#define L1_CACHE_BYTES 32
#define MAX_CACHE_SIZE 8192
void cache_push(__u32 addr, int length)
{
	addr = mm_ptov(addr);

	if (MAX_CACHE_SIZE < length)
		length = MAX_CACHE_SIZE;

	while(length > 0){
		__asm ("dcbf 0,%0\n\t"
		       : : "r" (addr));
		addr += L1_CACHE_BYTES;
		length -= L1_CACHE_BYTES;
	}
	/* Also flush trailing block */
	__asm ("dcbf 0,%0\n\t"
	       "sync \n\t"
	       : : "r" (addr));
}

void cache_clear(__u32 addr, int length)
{
	if (MAX_CACHE_SIZE < length)
		length = MAX_CACHE_SIZE;

	addr = mm_ptov(addr);

	__asm ("dcbf 0,%0\n\t"
	       "sync \n\t"
	       "icbi 0,%0 \n\t"
	       "isync \n\t"
	       : : "r" (addr));

	addr += L1_CACHE_BYTES;
	length -= L1_CACHE_BYTES;

	while(length > 0){
		__asm ("dcbf 0,%0\n\t"
		       "sync \n\t"
		       "icbi 0,%0 \n\t"
		       "isync \n\t"
		       : : "r" (addr));
		addr += L1_CACHE_BYTES;
		length -= L1_CACHE_BYTES;
	}

	__asm ("dcbf 0,%0\n\t"
	       "sync \n\t"
	       "icbi 0,%0 \n\t"
	       "isync \n\t"
	       : : "r" (addr));
}

/****************************************************** from setup.c */
void
apus_restart(char *cmd)
{
	local_irq_disable();

	APUS_WRITE(APUS_REG_LOCK,
		   REGLOCK_BLACKMAGICK1|REGLOCK_BLACKMAGICK2);
	APUS_WRITE(APUS_REG_LOCK,
		   REGLOCK_BLACKMAGICK1|REGLOCK_BLACKMAGICK3);
	APUS_WRITE(APUS_REG_LOCK,
		   REGLOCK_BLACKMAGICK2|REGLOCK_BLACKMAGICK3);
	APUS_WRITE(APUS_REG_SHADOW, REGSHADOW_SELFRESET);
	APUS_WRITE(APUS_REG_RESET, REGRESET_AMIGARESET);
	for(;;);
}

void
apus_power_off(void)
{
	for (;;);
}

void
apus_halt(void)
{
   apus_restart(NULL);
}

/****************************************************** IRQ stuff */

static unsigned char last_ipl[8];

int apus_get_irq(struct pt_regs* regs)
{
	unsigned char ipl_emu, mask;
	unsigned int level;

	APUS_READ(APUS_IPL_EMU, ipl_emu);
	level = (ipl_emu >> 3) & IPLEMU_IPLMASK;
	mask = IPLEMU_SETRESET|IPLEMU_DISABLEINT|level;
	level ^= 7;

	/* Save previous IPL value */
	if (last_ipl[level])
		return -2;
	last_ipl[level] = ipl_emu;

	/* Set to current IPL value */
	APUS_WRITE(APUS_IPL_EMU, mask);
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_DISABLEINT|level);


#ifdef __INTERRUPT_DEBUG
	printk("<%d:%d>", level, ~ipl_emu & IPLEMU_IPLMASK);
#endif
	return level + IRQ_AMIGA_AUTO;
}

void apus_end_irq(unsigned int irq)
{
	unsigned char ipl_emu;
	unsigned int level = irq - IRQ_AMIGA_AUTO;
#ifdef __INTERRUPT_DEBUG
	printk("{%d}", ~last_ipl[level] & IPLEMU_IPLMASK);
#endif
	/* Restore IPL to the previous value */
	ipl_emu = last_ipl[level] & IPLEMU_IPLMASK;
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_SETRESET|IPLEMU_DISABLEINT|ipl_emu);
	last_ipl[level] = 0;
	ipl_emu ^= 7;
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_DISABLEINT|ipl_emu);
}

/****************************************************** debugging */

/* some serial hardware definitions */
#define SDR_OVRUN   (1<<15)
#define SDR_RBF     (1<<14)
#define SDR_TBE     (1<<13)
#define SDR_TSRE    (1<<12)

#define AC_SETCLR   (1<<15)
#define AC_UARTBRK  (1<<11)

#define SER_DTR     (1<<7)
#define SER_RTS     (1<<6)
#define SER_DCD     (1<<5)
#define SER_CTS     (1<<4)
#define SER_DSR     (1<<3)

static __inline__ void ser_RTSon(void)
{
    ciab.pra &= ~SER_RTS; /* active low */
}

int __debug_ser_out( unsigned char c )
{
	amiga_custom.serdat = c | 0x100;
	mb();
	while (!(amiga_custom.serdatr & 0x2000))
		barrier();
	return 1;
}

unsigned char __debug_ser_in( void )
{
	unsigned char c;

	/* XXX: is that ok?? derived from amiga_ser.c... */
	while( !(amiga_custom.intreqr & IF_RBF) )
		barrier();
	c = amiga_custom.serdatr;
	/* clear the interrupt, so that another character can be read */
	amiga_custom.intreq = IF_RBF;
	return c;
}

int __debug_serinit( void )
{
	unsigned long flags;

	local_irq_save(flags);

	/* turn off Rx and Tx interrupts */
	amiga_custom.intena = IF_RBF | IF_TBE;

	/* clear any pending interrupt */
	amiga_custom.intreq = IF_RBF | IF_TBE;

	local_irq_restore(flags);

	/*
	 * set the appropriate directions for the modem control flags,
	 * and clear RTS and DTR
	 */
	ciab.ddra |= (SER_DTR | SER_RTS);   /* outputs */
	ciab.ddra &= ~(SER_DCD | SER_CTS | SER_DSR);  /* inputs */

#ifdef CONFIG_KGDB
	/* turn Rx interrupts on for GDB */
	amiga_custom.intena = IF_SETCLR | IF_RBF;
	ser_RTSon();
#endif

	return 0;
}

void __debug_print_hex(unsigned long x)
{
	int i;
	char hexchars[] = "0123456789ABCDEF";

	for (i = 0; i < 8; i++) {
		__debug_ser_out(hexchars[(x >> 28) & 15]);
		x <<= 4;
	}
	__debug_ser_out('\n');
	__debug_ser_out('\r');
}

void __debug_print_string(char* s)
{
	unsigned char c;
	while((c = *s++))
		__debug_ser_out(c);
	__debug_ser_out('\n');
	__debug_ser_out('\r');
}

static void apus_progress(char *s, unsigned short value)
{
	__debug_print_string(s);
}

/****************************************************** init */

/* The number of spurious interrupts */
volatile unsigned int num_spurious;

extern struct irqaction amiga_sys_irqaction[AUTO_IRQS];


extern void amiga_enable_irq(unsigned int irq);
extern void amiga_disable_irq(unsigned int irq);

struct hw_interrupt_type amiga_sys_irqctrl = {
	.typename = "Amiga IPL",
	.end = apus_end_irq,
};

struct hw_interrupt_type amiga_irqctrl = {
	.typename = "Amiga    ",
	.enable = amiga_enable_irq,
	.disable = amiga_disable_irq,
};

#define HARDWARE_MAPPED_SIZE (512*1024)
unsigned long __init apus_find_end_of_memory(void)
{
	int shadow = 0;
	unsigned long total;

	/* The memory size reported by ADOS excludes the 512KB
	   reserved for PPC exception registers and possibly 512KB
	   containing a shadow of the ADOS ROM. */
	{
		unsigned long size = memory[0].size;

		/* If 2MB aligned, size was probably user
                   specified. We can't tell anything about shadowing
                   in this case so skip shadow assignment. */
		if (0 != (size & 0x1fffff)){
			/* Align to 512KB to ensure correct handling
			   of both memfile and system specified
			   sizes. */
			size = ((size+0x0007ffff) & 0xfff80000);
			/* If memory is 1MB aligned, assume
                           shadowing. */
			shadow = !(size & 0x80000);
		}

		/* Add the chunk that ADOS does not see. by aligning
                   the size to the nearest 2MB limit upwards.  */
		memory[0].size = ((size+0x001fffff) & 0xffe00000);
	}

	ppc_memstart = memory[0].addr;
	ppc_memoffset = PAGE_OFFSET - PPC_MEMSTART;
	total = memory[0].size;

	/* Remove the memory chunks that are controlled by special
           Phase5 hardware. */

	/* Remove the upper 512KB if it contains a shadow of
	   the ADOS ROM. FIXME: It might be possible to
	   disable this shadow HW. Check the booter
	   (ppc_boot.c) */
	if (shadow)
		total -= HARDWARE_MAPPED_SIZE;

	/* Remove the upper 512KB where the PPC exception
	   vectors are mapped. */
	total -= HARDWARE_MAPPED_SIZE;

	/* Linux/APUS only handles one block of memory -- the one on
	   the PowerUP board. Other system memory is horrible slow in
	   comparison. The user can use other memory for swapping
	   using the z2ram device. */
	return total;
}

static void __init
apus_map_io(void)
{
	/* Map PPC exception vectors. */
	io_block_mapping(0xfff00000, 0xfff00000, 0x00020000, _PAGE_KERNEL);
	/* Map chip and ZorroII memory */
	io_block_mapping(zTwoBase,   0x00000000, 0x01000000, _PAGE_IO);
}

__init
void apus_init_IRQ(void)
{
	struct irqaction *action;
	int i;

#ifdef CONFIG_PCI
        apus_setup_pci_ptrs();
#endif

	for ( i = 0 ; i < AMI_IRQS; i++ ) {
		irq_desc[i].status = IRQ_LEVEL;
		if (i < IRQ_AMIGA_AUTO) {
			irq_desc[i].handler = &amiga_irqctrl;
		} else {
			irq_desc[i].handler = &amiga_sys_irqctrl;
			action = &amiga_sys_irqaction[i-IRQ_AMIGA_AUTO];
			if (action->name)
				setup_irq(i, action);
		}
	}

	amiga_init_IRQ();

}

__init
void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
		   unsigned long r6, unsigned long r7)
{
	extern int parse_bootinfo(const struct bi_record *);
	extern char _end[];

	/* Parse bootinfo. The bootinfo is located right after
           the kernel bss */
	parse_bootinfo((const struct bi_record *)&_end);
#ifdef CONFIG_BLK_DEV_INITRD
	/* Take care of initrd if we have one. Use data from
	   bootinfo to avoid the need to initialize PPC
	   registers when kernel is booted via a PPC reset. */
	if ( ramdisk.addr ) {
		initrd_start = (unsigned long) __va(ramdisk.addr);
		initrd_end = (unsigned long)
			__va(ramdisk.size + ramdisk.addr);
	}
#endif /* CONFIG_BLK_DEV_INITRD */

	ISA_DMA_THRESHOLD = 0x00ffffff;

	ppc_md.setup_arch     = apus_setup_arch;
	ppc_md.show_cpuinfo   = apus_show_cpuinfo;
	ppc_md.init_IRQ       = apus_init_IRQ;
	ppc_md.get_irq        = apus_get_irq;

#ifdef CONFIG_HEARTBEAT
	ppc_md.heartbeat      = apus_heartbeat;
	ppc_md.heartbeat_count = 1;
#endif
#ifdef APUS_DEBUG
	__debug_serinit();
	ppc_md.progress       = apus_progress;
#endif
	ppc_md.init           = NULL;

	ppc_md.restart        = apus_restart;
	ppc_md.power_off      = apus_power_off;
	ppc_md.halt           = apus_halt;

	ppc_md.time_init      = NULL;
	ppc_md.set_rtc_time   = apus_set_rtc_time;
	ppc_md.get_rtc_time   = apus_get_rtc_time;
	ppc_md.calibrate_decr = apus_calibrate_decr;

	ppc_md.find_end_of_memory = apus_find_end_of_memory;
	ppc_md.setup_io_mappings = apus_map_io;
}
