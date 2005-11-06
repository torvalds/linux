/*
 *  arch/ppc/platforms/gemini_setup.c
 *
 *  Copyright (C) 1995 Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  Synergy Microsystems board support by Dan Cox (dan@synergymicro.com)
 *
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/time.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/major.h>
#include <linux/initrd.h>
#include <linux/console.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/bcd.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/m48t35.h>
#include <platforms/gemini.h>
#include <asm/time.h>
#include <asm/open_pic.h>
#include <asm/bootinfo.h>
#include <asm/machdep.h>

void gemini_find_bridges(void);
static int gemini_get_clock_speed(void);
extern void gemini_pcibios_fixup(void);

static char *gemini_board_families[] = {
  "VGM", "VSS", "KGM", "VGR", "VCM", "VCS", "KCM", "VCR"
};
static int gemini_board_count = sizeof(gemini_board_families) /
                                 sizeof(gemini_board_families[0]);

static unsigned int cpu_7xx[16] = {
	0, 15, 14, 0, 0, 13, 5, 9, 6, 11, 8, 10, 16, 12, 7, 0
};
static unsigned int cpu_6xx[16] = {
	0, 0, 14, 0, 0, 13, 5, 9, 6, 11, 8, 10, 0, 12, 7, 0
};

/*
 * prom_init is the Gemini version of prom.c:prom_init.  We only need
 * the BSS clearing code, so I copied that out of prom.c.  This is a
 * lot simpler than hacking prom.c so it will build with Gemini. -VAL
 */

#define PTRRELOC(x)	((typeof(x))((unsigned long)(x) + offset))

unsigned long
prom_init(void)
{
	unsigned long offset = reloc_offset();
	unsigned long phys;
	extern char __bss_start, _end;

	/* First zero the BSS -- use memset, some arches don't have
	 * caches on yet */
	memset_io(PTRRELOC(&__bss_start),0 , &_end - &__bss_start);

 	/* Default */
 	phys = offset + KERNELBASE;

	gemini_prom_init();

	return phys;
}

int
gemini_show_cpuinfo(struct seq_file *m)
{
	unsigned char reg, rev;
	char *family;
	unsigned int type;

	reg = readb(GEMINI_FEAT);
	family = gemini_board_families[((reg>>4) & 0xf)];
	if (((reg>>4) & 0xf) > gemini_board_count)
		printk(KERN_ERR "cpuinfo(): unable to determine board family\n");

	reg = readb(GEMINI_BREV);
	type = (reg>>4) & 0xf;
	rev = reg & 0xf;

	reg = readb(GEMINI_BECO);

	seq_printf(m, "machine\t\t: Gemini %s%d, rev %c, eco %d\n",
		   family, type, (rev + 'A'), (reg & 0xf));

	seq_printf(m, "board\t\t: Gemini %s", family);
	if (type > 9)
		seq_printf(m, "%c", (type - 10) + 'A');
	else
		seq_printf(m, "%d", type);

	seq_printf(m, ", rev %c, eco %d\n", (rev + 'A'), (reg & 0xf));

	seq_printf(m, "clock\t\t: %dMhz\n", gemini_get_clock_speed());

	return 0;
}

static u_char gemini_openpic_initsenses[] = {
	1,
	1,
	1,
	1,
	0,
	0,
	1, /* remainder are level-triggered */
};

#define GEMINI_MPIC_ADDR (0xfcfc0000)
#define GEMINI_MPIC_PCI_CFG (0x80005800)

void __init gemini_openpic_init(void)
{

	OpenPIC_Addr = (volatile struct OpenPIC *)
		grackle_read(GEMINI_MPIC_PCI_CFG + 0x10);
	OpenPIC_InitSenses = gemini_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof( gemini_openpic_initsenses );

	ioremap( GEMINI_MPIC_ADDR, OPENPIC_SIZE);
}


extern unsigned long loops_per_jiffy;
extern int root_mountflags;
extern char cmd_line[];

void
gemini_heartbeat(void)
{
	static unsigned long led = GEMINI_LEDBASE+(4*8);
	static char direction = 8;


	/* We only want to do this on 1 CPU */
	if (smp_processor_id())
		return;
	*(char *)led = 0;
	if ( (led + direction) > (GEMINI_LEDBASE+(7*8)) ||
	     (led + direction) < (GEMINI_LEDBASE+(4*8)) )
		direction *= -1;
	led += direction;
	*(char *)led = 0xff;
	ppc_md.heartbeat_count = ppc_md.heartbeat_reset;
}

void __init gemini_setup_arch(void)
{
	extern char cmd_line[];


	loops_per_jiffy = 50000000/HZ;

#ifdef CONFIG_BLK_DEV_INITRD
	/* bootable off CDROM */
	if (initrd_start)
		ROOT_DEV = Root_SR0;
	else
#endif
		ROOT_DEV = Root_SDA1;

	/* nothing but serial consoles... */
	sprintf(cmd_line, "%s console=ttyS0", cmd_line);

	printk("Boot arguments: %s\n", cmd_line);

	ppc_md.heartbeat = gemini_heartbeat;
	ppc_md.heartbeat_reset = HZ/8;
	ppc_md.heartbeat_count = 1;

	/* Lookup PCI hosts */
	gemini_find_bridges();
	/* take special pains to map the MPIC, since it isn't mapped yet */
	gemini_openpic_init();
	/* start the L2 */
	gemini_init_l2();
}


int
gemini_get_clock_speed(void)
{
	unsigned long hid1, pvr;
	int clock;

	pvr = mfspr(SPRN_PVR);
	hid1 = (mfspr(SPRN_HID1) >> 28) & 0xf;
	if (PVR_VER(pvr) == 8 ||
	    PVR_VER(pvr) == 12)
		hid1 = cpu_7xx[hid1];
	else
		hid1 = cpu_6xx[hid1];

	switch((readb(GEMINI_BSTAT) & 0xc) >> 2) {

	case 0:
	default:
		clock = (hid1*100)/3;
		break;

	case 1:
		clock = (hid1*125)/3;
		break;

	case 2:
		clock = (hid1*50);
		break;
	}

	return clock;
}

void __init gemini_init_l2(void)
{
        unsigned char reg, brev, fam, creg;
        unsigned long cache;
        unsigned long pvr;

        reg = readb(GEMINI_L2CFG);
        brev = readb(GEMINI_BREV);
        fam = readb(GEMINI_FEAT);
        pvr = mfspr(SPRN_PVR);

        switch(PVR_VER(pvr)) {

        case 8:
                if (reg & 0xc0)
                        cache = (((reg >> 6) & 0x3) << 28);
                else
                        cache = 0x3 << 28;

#ifdef CONFIG_SMP
                /* Pre-3.0 processor revs had snooping errata.  Leave
                   their L2's disabled with SMP. -- Dan */
                if (PVR_CFG(pvr) < 3) {
                        printk("Pre-3.0 750; L2 left disabled!\n");
                        return;
                }
#endif /* CONFIG_SMP */

                /* Special case: VGM5-B's came before L2 ratios were set on
                   the board.  Processor speed shouldn't be too high, so
                   set L2 ratio to 1:1.5.  */
                if ((brev == 0x51) && ((fam & 0xa0) >> 4) == 0)
                        reg |= 1;

                /* determine best cache ratio based upon what the board
                   tells us (which sometimes _may_ not be true) and
                   the processor speed. */
                else {
                        if (gemini_get_clock_speed() > 250)
                                reg = 2;
                }
                break;
        case 12:
	{
		static unsigned long l2_size_val = 0;

		if (!l2_size_val)
			l2_size_val = _get_L2CR();
		cache = l2_size_val;
                break;
	}
        case 4:
        case 9:
                creg = readb(GEMINI_CPUSTAT);
                if (((creg & 0xc) >> 2) != 1)
                        printk("Dual-604 boards don't support the use of L2\n");
                else
                        writeb(1, GEMINI_L2CFG);
                return;
        default:
                printk("Unknown processor; L2 left disabled\n");
                return;
        }

        cache |= ((1<<reg) << 25);
        cache |= (L2CR_L2RAM_MASK|L2CR_L2CTL|L2CR_L2DO);
        _set_L2CR(0);
        _set_L2CR(cache | L2CR_L2E);

}

void
gemini_restart(char *cmd)
{
	local_irq_disable();
	/* make a clean restart, not via the MPIC */
	_gemini_reboot();
	for(;;);
}

void
gemini_power_off(void)
{
	for(;;);
}

void
gemini_halt(void)
{
	gemini_restart(NULL);
}

void __init gemini_init_IRQ(void)
{
	/* gemini has no 8259 */
	openpic_init(1, 0, 0, -1);
}

#define gemini_rtc_read(x)       (readb(GEMINI_RTC+(x)))
#define gemini_rtc_write(val,x)  (writeb((val),(GEMINI_RTC+(x))))

/* ensure that the RTC is up and running */
long __init gemini_time_init(void)
{
	unsigned char reg;

	reg = gemini_rtc_read(M48T35_RTC_CONTROL);

	if ( reg & M48T35_RTC_STOPPED ) {
		printk(KERN_INFO "M48T35 real-time-clock was stopped. Now starting...\n");
		gemini_rtc_write((reg & ~(M48T35_RTC_STOPPED)), M48T35_RTC_CONTROL);
		gemini_rtc_write((reg | M48T35_RTC_SET), M48T35_RTC_CONTROL);
	}
	return 0;
}

#undef DEBUG_RTC

unsigned long
gemini_get_rtc_time(void)
{
	unsigned int year, mon, day, hour, min, sec;
	unsigned char reg;

	reg = gemini_rtc_read(M48T35_RTC_CONTROL);
	gemini_rtc_write((reg|M48T35_RTC_READ), M48T35_RTC_CONTROL);
#ifdef DEBUG_RTC
	printk("get rtc: reg = %x\n", reg);
#endif

	do {
		sec = gemini_rtc_read(M48T35_RTC_SECONDS);
		min = gemini_rtc_read(M48T35_RTC_MINUTES);
		hour = gemini_rtc_read(M48T35_RTC_HOURS);
		day = gemini_rtc_read(M48T35_RTC_DOM);
		mon = gemini_rtc_read(M48T35_RTC_MONTH);
		year = gemini_rtc_read(M48T35_RTC_YEAR);
	} while( sec != gemini_rtc_read(M48T35_RTC_SECONDS));
#ifdef DEBUG_RTC
	printk("get rtc: sec=%x, min=%x, hour=%x, day=%x, mon=%x, year=%x\n",
	       sec, min, hour, day, mon, year);
#endif

	gemini_rtc_write(reg, M48T35_RTC_CONTROL);

	BCD_TO_BIN(sec);
	BCD_TO_BIN(min);
	BCD_TO_BIN(hour);
	BCD_TO_BIN(day);
	BCD_TO_BIN(mon);
	BCD_TO_BIN(year);

	if ((year += 1900) < 1970)
		year += 100;
#ifdef DEBUG_RTC
	printk("get rtc: sec=%x, min=%x, hour=%x, day=%x, mon=%x, year=%x\n",
	       sec, min, hour, day, mon, year);
#endif

	return mktime( year, mon, day, hour, min, sec );
}


int
gemini_set_rtc_time( unsigned long now )
{
	unsigned char reg;
	struct rtc_time tm;

	to_tm( now, &tm );

	reg = gemini_rtc_read(M48T35_RTC_CONTROL);
#ifdef DEBUG_RTC
	printk("set rtc: reg = %x\n", reg);
#endif

	gemini_rtc_write((reg|M48T35_RTC_SET), M48T35_RTC_CONTROL);
#ifdef DEBUG_RTC
	printk("set rtc: tm vals - sec=%x, min=%x, hour=%x, mon=%x, mday=%x, year=%x\n",
	       tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mon, tm.tm_mday, tm.tm_year);
#endif

	tm.tm_year -= 1900;
	BIN_TO_BCD(tm.tm_sec);
	BIN_TO_BCD(tm.tm_min);
	BIN_TO_BCD(tm.tm_hour);
	BIN_TO_BCD(tm.tm_mon);
	BIN_TO_BCD(tm.tm_mday);
	BIN_TO_BCD(tm.tm_year);
#ifdef DEBUG_RTC
	printk("set rtc: tm vals - sec=%x, min=%x, hour=%x, mon=%x, mday=%x, year=%x\n",
	       tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mon, tm.tm_mday, tm.tm_year);
#endif

	gemini_rtc_write(tm.tm_sec, M48T35_RTC_SECONDS);
	gemini_rtc_write(tm.tm_min, M48T35_RTC_MINUTES);
	gemini_rtc_write(tm.tm_hour, M48T35_RTC_HOURS);
	gemini_rtc_write(tm.tm_mday, M48T35_RTC_DOM);
	gemini_rtc_write(tm.tm_mon, M48T35_RTC_MONTH);
	gemini_rtc_write(tm.tm_year, M48T35_RTC_YEAR);

	/* done writing */
	gemini_rtc_write(reg, M48T35_RTC_CONTROL);

	return 0;
}

/*  use the RTC to determine the decrementer count */
void __init gemini_calibrate_decr(void)
{
	int freq, divisor;
	unsigned char reg;

	/* determine processor bus speed */
	reg = readb(GEMINI_BSTAT);

	switch(((reg & 0x0c)>>2)&0x3) {
	case 0:
	default:
		freq = 66667;
		break;
	case 1:
		freq = 83000;
		break;
	case 2:
		freq = 100000;
		break;
	}

	freq *= 1000;
	divisor = 4;
	tb_ticks_per_jiffy = freq / HZ / divisor;
	tb_to_us = mulhwu_scale_factor(freq/divisor, 1000000);
}

unsigned long __init gemini_find_end_of_memory(void)
{
	unsigned long total;
	unsigned char reg;

	reg = readb(GEMINI_MEMCFG);
	total = ((1<<((reg & 0x7) - 1)) *
		 (8<<((reg >> 3) & 0x7)));
	total *= (1024*1024);
	return total;
}

static void __init
gemini_map_io(void)
{
	io_block_mapping(0xf0000000, 0xf0000000, 0x10000000, _PAGE_IO);
	io_block_mapping(0x80000000, 0x80000000, 0x10000000, _PAGE_IO);
}

#ifdef CONFIG_SMP
static int
smp_gemini_probe(void)
{
	int i, nr;

        nr = (readb(GEMINI_CPUSTAT) & GEMINI_CPU_COUNT_MASK) >> 2;
	if (nr == 0)
		nr = 4;

	if (nr > 1) {
		openpic_request_IPIs();
		for (i = 1; i < nr; ++i)
			smp_hw_index[i] = i;
	}

	return nr;
}

static void
smp_gemini_kick_cpu(int nr)
{
	openpic_reset_processor_phys(1 << nr);
	openpic_reset_processor_phys(0);
}

static void
smp_gemini_setup_cpu(int cpu_nr)
{
	if (OpenPIC_Addr)
		do_openpic_setup_cpu();
	if (cpu_nr > 0)
		gemini_init_l2();
}

static struct smp_ops_t gemini_smp_ops = {
	smp_openpic_message_pass,
	smp_gemini_probe,
	smp_gemini_kick_cpu,
	smp_gemini_setup_cpu,
	.give_timebase = smp_generic_give_timebase,
	.take_timebase = smp_generic_take_timebase,
};
#endif /* CONFIG_SMP */

void __init platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
			  unsigned long r6, unsigned long r7)
{
	int i;

	/* Restore BATs for now */
	mtspr(SPRN_DBAT3U, 0xf0001fff);
	mtspr(SPRN_DBAT3L, 0xf000002a);

	parse_bootinfo(find_bootinfo());

	for(i = 0; i < GEMINI_LEDS; i++)
		gemini_led_off(i);

	ISA_DMA_THRESHOLD = 0;
	DMA_MODE_READ = 0;
	DMA_MODE_WRITE = 0;

#ifdef CONFIG_BLK_DEV_INITRD
	if ( r4 )
	{
		initrd_start = r4 + KERNELBASE;
		initrd_end = r5 + KERNELBASE;
	}
#endif

	ppc_md.setup_arch = gemini_setup_arch;
	ppc_md.show_cpuinfo = gemini_show_cpuinfo;
	ppc_md.init_IRQ = gemini_init_IRQ;
	ppc_md.get_irq = openpic_get_irq;
	ppc_md.init = NULL;

	ppc_md.restart = gemini_restart;
	ppc_md.power_off = gemini_power_off;
	ppc_md.halt = gemini_halt;

	ppc_md.time_init = gemini_time_init;
	ppc_md.set_rtc_time = gemini_set_rtc_time;
	ppc_md.get_rtc_time = gemini_get_rtc_time;
	ppc_md.calibrate_decr = gemini_calibrate_decr;

	ppc_md.find_end_of_memory = gemini_find_end_of_memory;
	ppc_md.setup_io_mappings = gemini_map_io;

	ppc_md.pcibios_fixup_bus = gemini_pcibios_fixup;

#ifdef CONFIG_SMP
	smp_ops = &gemini_smp_ops;
#endif /* CONFIG_SMP */
}
