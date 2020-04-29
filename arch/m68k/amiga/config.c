/*
 *  linux/arch/m68k/amiga/config.c
 *
 *  Copyright (C) 1993 Hamish Macdonald
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/*
 * Miscellaneous Amiga stuff
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/tty.h>
#include <linux/clocksource.h>
#include <linux/console.h>
#include <linux/rtc.h>
#include <linux/init.h>
#include <linux/vt_kern.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/zorro.h>
#include <linux/module.h>
#include <linux/keyboard.h>

#include <asm/bootinfo.h>
#include <asm/bootinfo-amiga.h>
#include <asm/byteorder.h>
#include <asm/setup.h>
#include <asm/pgtable.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/irq.h>
#include <asm/machdep.h>
#include <asm/io.h>

static unsigned long amiga_model;

unsigned long amiga_eclock;
EXPORT_SYMBOL(amiga_eclock);

unsigned long amiga_colorclock;
EXPORT_SYMBOL(amiga_colorclock);

unsigned long amiga_chipset;
EXPORT_SYMBOL(amiga_chipset);

unsigned char amiga_vblank;
EXPORT_SYMBOL(amiga_vblank);

static unsigned char amiga_psfreq;

struct amiga_hw_present amiga_hw_present;
EXPORT_SYMBOL(amiga_hw_present);

static char s_a500[] __initdata = "A500";
static char s_a500p[] __initdata = "A500+";
static char s_a600[] __initdata = "A600";
static char s_a1000[] __initdata = "A1000";
static char s_a1200[] __initdata = "A1200";
static char s_a2000[] __initdata = "A2000";
static char s_a2500[] __initdata = "A2500";
static char s_a3000[] __initdata = "A3000";
static char s_a3000t[] __initdata = "A3000T";
static char s_a3000p[] __initdata = "A3000+";
static char s_a4000[] __initdata = "A4000";
static char s_a4000t[] __initdata = "A4000T";
static char s_cdtv[] __initdata = "CDTV";
static char s_cd32[] __initdata = "CD32";
static char s_draco[] __initdata = "Draco";
static char *amiga_models[] __initdata = {
	[AMI_500-AMI_500]	= s_a500,
	[AMI_500PLUS-AMI_500]	= s_a500p,
	[AMI_600-AMI_500]	= s_a600,
	[AMI_1000-AMI_500]	= s_a1000,
	[AMI_1200-AMI_500]	= s_a1200,
	[AMI_2000-AMI_500]	= s_a2000,
	[AMI_2500-AMI_500]	= s_a2500,
	[AMI_3000-AMI_500]	= s_a3000,
	[AMI_3000T-AMI_500]	= s_a3000t,
	[AMI_3000PLUS-AMI_500]	= s_a3000p,
	[AMI_4000-AMI_500]	= s_a4000,
	[AMI_4000T-AMI_500]	= s_a4000t,
	[AMI_CDTV-AMI_500]	= s_cdtv,
	[AMI_CD32-AMI_500]	= s_cd32,
	[AMI_DRACO-AMI_500]	= s_draco,
};

static char amiga_model_name[13] = "Amiga ";

static void amiga_sched_init(irq_handler_t handler);
static void amiga_get_model(char *model);
static void amiga_get_hardware_list(struct seq_file *m);
extern void amiga_mksound(unsigned int count, unsigned int ticks);
static void amiga_reset(void);
extern void amiga_init_sound(void);
static void amiga_mem_console_write(struct console *co, const char *b,
				    unsigned int count);
#ifdef CONFIG_HEARTBEAT
static void amiga_heartbeat(int on);
#endif

static struct console amiga_console_driver = {
	.name	= "debug",
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
};


    /*
     *  Motherboard Resources present in all Amiga models
     */

static struct {
	struct resource _ciab, _ciaa, _custom, _kickstart;
} mb_resources = {
	._ciab = {
		.name = "CIA B", .start = 0x00bfd000, .end = 0x00bfdfff
	},
	._ciaa = {
		.name = "CIA A", .start = 0x00bfe000, .end = 0x00bfefff
	},
	._custom = {
		.name = "Custom I/O", .start = 0x00dff000, .end = 0x00dfffff
	},
	._kickstart = {
		.name = "Kickstart ROM", .start = 0x00f80000, .end = 0x00ffffff
	}
};

static struct resource ram_resource[NUM_MEMINFO];


    /*
     *  Parse an Amiga-specific record in the bootinfo
     */

int __init amiga_parse_bootinfo(const struct bi_record *record)
{
	int unknown = 0;
	const void *data = record->data;

	switch (be16_to_cpu(record->tag)) {
	case BI_AMIGA_MODEL:
		amiga_model = be32_to_cpup(data);
		break;

	case BI_AMIGA_ECLOCK:
		amiga_eclock = be32_to_cpup(data);
		break;

	case BI_AMIGA_CHIPSET:
		amiga_chipset = be32_to_cpup(data);
		break;

	case BI_AMIGA_CHIP_SIZE:
		amiga_chip_size = be32_to_cpup(data);
		break;

	case BI_AMIGA_VBLANK:
		amiga_vblank = *(const __u8 *)data;
		break;

	case BI_AMIGA_PSFREQ:
		amiga_psfreq = *(const __u8 *)data;
		break;

	case BI_AMIGA_AUTOCON:
#ifdef CONFIG_ZORRO
		if (zorro_num_autocon < ZORRO_NUM_AUTO) {
			const struct ConfigDev *cd = data;
			struct zorro_dev_init *dev = &zorro_autocon_init[zorro_num_autocon++];
			dev->rom = cd->cd_Rom;
			dev->slotaddr = be16_to_cpu(cd->cd_SlotAddr);
			dev->slotsize = be16_to_cpu(cd->cd_SlotSize);
			dev->boardaddr = be32_to_cpu(cd->cd_BoardAddr);
			dev->boardsize = be32_to_cpu(cd->cd_BoardSize);
		} else
			pr_warn("amiga_parse_bootinfo: too many AutoConfig devices\n");
#endif /* CONFIG_ZORRO */
		break;

	case BI_AMIGA_SERPER:
		/* serial port period: ignored here */
		break;

	default:
		unknown = 1;
	}
	return unknown;
}

    /*
     *  Identify builtin hardware
     */

static void __init amiga_identify(void)
{
	/* Fill in some default values, if necessary */
	if (amiga_eclock == 0)
		amiga_eclock = 709379;

	memset(&amiga_hw_present, 0, sizeof(amiga_hw_present));

	pr_info("Amiga hardware found: ");
	if (amiga_model >= AMI_500 && amiga_model <= AMI_DRACO) {
		pr_cont("[%s] ", amiga_models[amiga_model-AMI_500]);
		strcat(amiga_model_name, amiga_models[amiga_model-AMI_500]);
	}

	switch (amiga_model) {
	case AMI_UNKNOWN:
		goto Generic;

	case AMI_600:
	case AMI_1200:
		AMIGAHW_SET(A1200_IDE);
		AMIGAHW_SET(PCMCIA);
	case AMI_500:
	case AMI_500PLUS:
	case AMI_1000:
	case AMI_2000:
	case AMI_2500:
		AMIGAHW_SET(A2000_CLK);	/* Is this correct for all models? */
		goto Generic;

	case AMI_3000:
	case AMI_3000T:
		AMIGAHW_SET(AMBER_FF);
		AMIGAHW_SET(MAGIC_REKICK);
		/* fall through */
	case AMI_3000PLUS:
		AMIGAHW_SET(A3000_SCSI);
		AMIGAHW_SET(A3000_CLK);
		AMIGAHW_SET(ZORRO3);
		goto Generic;

	case AMI_4000T:
		AMIGAHW_SET(A4000_SCSI);
		/* fall through */
	case AMI_4000:
		AMIGAHW_SET(A4000_IDE);
		AMIGAHW_SET(A3000_CLK);
		AMIGAHW_SET(ZORRO3);
		goto Generic;

	case AMI_CDTV:
	case AMI_CD32:
		AMIGAHW_SET(CD_ROM);
		AMIGAHW_SET(A2000_CLK);             /* Is this correct? */
		goto Generic;

	Generic:
		AMIGAHW_SET(AMI_VIDEO);
		AMIGAHW_SET(AMI_BLITTER);
		AMIGAHW_SET(AMI_AUDIO);
		AMIGAHW_SET(AMI_FLOPPY);
		AMIGAHW_SET(AMI_KEYBOARD);
		AMIGAHW_SET(AMI_MOUSE);
		AMIGAHW_SET(AMI_SERIAL);
		AMIGAHW_SET(AMI_PARALLEL);
		AMIGAHW_SET(CHIP_RAM);
		AMIGAHW_SET(PAULA);

		switch (amiga_chipset) {
		case CS_OCS:
		case CS_ECS:
		case CS_AGA:
			switch (amiga_custom.deniseid & 0xf) {
			case 0x0c:
				AMIGAHW_SET(DENISE_HR);
				break;
			case 0x08:
				AMIGAHW_SET(LISA);
				break;
			}
			break;
		default:
			AMIGAHW_SET(DENISE);
			break;
		}
		switch ((amiga_custom.vposr>>8) & 0x7f) {
		case 0x00:
			AMIGAHW_SET(AGNUS_PAL);
			break;
		case 0x10:
			AMIGAHW_SET(AGNUS_NTSC);
			break;
		case 0x20:
		case 0x21:
			AMIGAHW_SET(AGNUS_HR_PAL);
			break;
		case 0x30:
		case 0x31:
			AMIGAHW_SET(AGNUS_HR_NTSC);
			break;
		case 0x22:
		case 0x23:
			AMIGAHW_SET(ALICE_PAL);
			break;
		case 0x32:
		case 0x33:
			AMIGAHW_SET(ALICE_NTSC);
			break;
		}
		AMIGAHW_SET(ZORRO);
		break;

	case AMI_DRACO:
		panic("No support for Draco yet");

	default:
		panic("Unknown Amiga Model");
	}

#define AMIGAHW_ANNOUNCE(name, str)		\
	if (AMIGAHW_PRESENT(name))		\
		pr_cont(str)

	AMIGAHW_ANNOUNCE(AMI_VIDEO, "VIDEO ");
	AMIGAHW_ANNOUNCE(AMI_BLITTER, "BLITTER ");
	AMIGAHW_ANNOUNCE(AMBER_FF, "AMBER_FF ");
	AMIGAHW_ANNOUNCE(AMI_AUDIO, "AUDIO ");
	AMIGAHW_ANNOUNCE(AMI_FLOPPY, "FLOPPY ");
	AMIGAHW_ANNOUNCE(A3000_SCSI, "A3000_SCSI ");
	AMIGAHW_ANNOUNCE(A4000_SCSI, "A4000_SCSI ");
	AMIGAHW_ANNOUNCE(A1200_IDE, "A1200_IDE ");
	AMIGAHW_ANNOUNCE(A4000_IDE, "A4000_IDE ");
	AMIGAHW_ANNOUNCE(CD_ROM, "CD_ROM ");
	AMIGAHW_ANNOUNCE(AMI_KEYBOARD, "KEYBOARD ");
	AMIGAHW_ANNOUNCE(AMI_MOUSE, "MOUSE ");
	AMIGAHW_ANNOUNCE(AMI_SERIAL, "SERIAL ");
	AMIGAHW_ANNOUNCE(AMI_PARALLEL, "PARALLEL ");
	AMIGAHW_ANNOUNCE(A2000_CLK, "A2000_CLK ");
	AMIGAHW_ANNOUNCE(A3000_CLK, "A3000_CLK ");
	AMIGAHW_ANNOUNCE(CHIP_RAM, "CHIP_RAM ");
	AMIGAHW_ANNOUNCE(PAULA, "PAULA ");
	AMIGAHW_ANNOUNCE(DENISE, "DENISE ");
	AMIGAHW_ANNOUNCE(DENISE_HR, "DENISE_HR ");
	AMIGAHW_ANNOUNCE(LISA, "LISA ");
	AMIGAHW_ANNOUNCE(AGNUS_PAL, "AGNUS_PAL ");
	AMIGAHW_ANNOUNCE(AGNUS_NTSC, "AGNUS_NTSC ");
	AMIGAHW_ANNOUNCE(AGNUS_HR_PAL, "AGNUS_HR_PAL ");
	AMIGAHW_ANNOUNCE(AGNUS_HR_NTSC, "AGNUS_HR_NTSC ");
	AMIGAHW_ANNOUNCE(ALICE_PAL, "ALICE_PAL ");
	AMIGAHW_ANNOUNCE(ALICE_NTSC, "ALICE_NTSC ");
	AMIGAHW_ANNOUNCE(MAGIC_REKICK, "MAGIC_REKICK ");
	AMIGAHW_ANNOUNCE(PCMCIA, "PCMCIA ");
	if (AMIGAHW_PRESENT(ZORRO))
		pr_cont("ZORRO%s ", AMIGAHW_PRESENT(ZORRO3) ? "3" : "");
	pr_cont("\n");

#undef AMIGAHW_ANNOUNCE
}


static unsigned long amiga_random_get_entropy(void)
{
	/* VPOSR/VHPOSR provide at least 17 bits of data changing at 1.79 MHz */
	return *(unsigned long *)&amiga_custom.vposr;
}


    /*
     *  Setup the Amiga configuration info
     */

void __init config_amiga(void)
{
	int i;

	amiga_identify();

	/* Yuk, we don't have PCI memory */
	iomem_resource.name = "Memory";
	for (i = 0; i < 4; i++)
		request_resource(&iomem_resource, &((struct resource *)&mb_resources)[i]);

	mach_sched_init      = amiga_sched_init;
	mach_init_IRQ        = amiga_init_IRQ;
	mach_get_model       = amiga_get_model;
	mach_get_hardware_list = amiga_get_hardware_list;

	/*
	 * default MAX_DMA=0xffffffff on all machines. If we don't do so, the SCSI
	 * code will not be able to allocate any mem for transfers, unless we are
	 * dealing with a Z2 mem only system.                  /Jes
	 */
	mach_max_dma_address = 0xffffffff;

	mach_reset           = amiga_reset;
#if IS_ENABLED(CONFIG_INPUT_M68K_BEEP)
	mach_beep            = amiga_mksound;
#endif

#ifdef CONFIG_HEARTBEAT
	mach_heartbeat = amiga_heartbeat;
#endif

	mach_random_get_entropy = amiga_random_get_entropy;

	/* Fill in the clock value (based on the 700 kHz E-Clock) */
	amiga_colorclock = 5*amiga_eclock;	/* 3.5 MHz */

	/* clear all DMA bits */
	amiga_custom.dmacon = DMAF_ALL;
	/* ensure that the DMA master bit is set */
	amiga_custom.dmacon = DMAF_SETCLR | DMAF_MASTER;

	/* don't use Z2 RAM as system memory on Z3 capable machines */
	if (AMIGAHW_PRESENT(ZORRO3)) {
		int i, j;
		u32 disabled_z2mem = 0;

		for (i = 0; i < m68k_num_memory; i++) {
			if (m68k_memory[i].addr < 16*1024*1024) {
				if (i == 0) {
					/* don't cut off the branch we're sitting on */
					pr_warn("Warning: kernel runs in Zorro II memory\n");
					continue;
				}
				disabled_z2mem += m68k_memory[i].size;
				m68k_num_memory--;
				for (j = i; j < m68k_num_memory; j++)
					m68k_memory[j] = m68k_memory[j+1];
				i--;
			}
		}
		if (disabled_z2mem)
			pr_info("%dK of Zorro II memory will not be used as system memory\n",
				disabled_z2mem>>10);
	}

	/* request all RAM */
	for (i = 0; i < m68k_num_memory; i++) {
		ram_resource[i].name =
			(m68k_memory[i].addr >= 0x01000000) ? "32-bit Fast RAM" :
			(m68k_memory[i].addr < 0x00c00000) ? "16-bit Fast RAM" :
			"16-bit Slow RAM";
		ram_resource[i].start = m68k_memory[i].addr;
		ram_resource[i].end = m68k_memory[i].addr+m68k_memory[i].size-1;
		request_resource(&iomem_resource, &ram_resource[i]);
	}

	/* initialize chipram allocator */
	amiga_chip_init();

	/* our beloved beeper */
	if (AMIGAHW_PRESENT(AMI_AUDIO))
		amiga_init_sound();

	/*
	 * if it is an A3000, set the magic bit that forces
	 * a hard rekick
	 */
	if (AMIGAHW_PRESENT(MAGIC_REKICK))
		*(unsigned char *)ZTWO_VADDR(0xde0002) |= 0x80;
}

static u64 amiga_read_clk(struct clocksource *cs);

static struct clocksource amiga_clk = {
	.name   = "ciab",
	.rating = 250,
	.read   = amiga_read_clk,
	.mask   = CLOCKSOURCE_MASK(32),
	.flags  = CLOCK_SOURCE_IS_CONTINUOUS,
};

static unsigned short jiffy_ticks;
static u32 clk_total, clk_offset;

static irqreturn_t ciab_timer_handler(int irq, void *dev_id)
{
	irq_handler_t timer_routine = dev_id;

	clk_total += jiffy_ticks;
	clk_offset = 0;
	timer_routine(0, NULL);

	return IRQ_HANDLED;
}

static void __init amiga_sched_init(irq_handler_t timer_routine)
{
	static struct resource sched_res = {
		.name = "timer", .start = 0x00bfd400, .end = 0x00bfd5ff,
	};
	jiffy_ticks = DIV_ROUND_CLOSEST(amiga_eclock, HZ);

	if (request_resource(&mb_resources._ciab, &sched_res))
		pr_warn("Cannot allocate ciab.ta{lo,hi}\n");
	ciab.cra &= 0xC0;   /* turn off timer A, continuous mode, from Eclk */
	ciab.talo = jiffy_ticks % 256;
	ciab.tahi = jiffy_ticks / 256;

	/* install interrupt service routine for CIAB Timer A
	 *
	 * Please don't change this to use ciaa, as it interferes with the
	 * SCSI code. We'll have to take a look at this later
	 */
	if (request_irq(IRQ_AMIGA_CIAB_TA, ciab_timer_handler, IRQF_TIMER,
			"timer", timer_routine))
		pr_err("Couldn't register timer interrupt\n");
	/* start timer */
	ciab.cra |= 0x11;

	clocksource_register_hz(&amiga_clk, amiga_eclock);
}

static u64 amiga_read_clk(struct clocksource *cs)
{
	unsigned short hi, lo, hi2;
	unsigned long flags;
	u32 ticks;

	local_irq_save(flags);

	/* read CIA B timer A current value */
	hi  = ciab.tahi;
	lo  = ciab.talo;
	hi2 = ciab.tahi;

	if (hi != hi2) {
		lo = ciab.talo;
		hi = hi2;
	}

	ticks = hi << 8 | lo;

	if (ticks > jiffy_ticks / 2)
		/* check for pending interrupt */
		if (cia_set_irq(&ciab_base, 0) & CIA_ICR_TA)
			clk_offset = jiffy_ticks;

	ticks = jiffy_ticks - ticks;
	ticks += clk_offset + clk_total;

	local_irq_restore(flags);

	return ticks;
}

static void amiga_reset(void)  __noreturn;

static void amiga_reset(void)
{
	unsigned long jmp_addr040 = virt_to_phys(&&jmp_addr_label040);
	unsigned long jmp_addr = virt_to_phys(&&jmp_addr_label);

	local_irq_disable();
	if (CPU_IS_040_OR_060)
		/* Setup transparent translation registers for mapping
		 * of 16 MB kernel segment before disabling translation
		 */
		asm volatile ("\n"
			"	move.l	%0,%%d0\n"
			"	and.l	#0xff000000,%%d0\n"
			"	or.w	#0xe020,%%d0\n"   /* map 16 MB, enable, cacheable */
			"	.chip	68040\n"
			"	movec	%%d0,%%itt0\n"
			"	movec	%%d0,%%dtt0\n"
			"	.chip	68k\n"
			"	jmp	%0@\n"
			: /* no outputs */
			: "a" (jmp_addr040)
			: "d0");
	else
		/* for 680[23]0, just disable translation and jump to the physical
		 * address of the label
		 */
		asm volatile ("\n"
			"	pmove	%%tc,%@\n"
			"	bclr	#7,%@\n"
			"	pmove	%@,%%tc\n"
			"	jmp	%0@\n"
			: /* no outputs */
			: "a" (jmp_addr));
jmp_addr_label040:
	/* disable translation on '040 now */
	asm volatile ("\n"
		"	moveq	#0,%%d0\n"
		"	.chip	68040\n"
		"	movec	%%d0,%%tc\n"	/* disable MMU */
		"	.chip	68k\n"
		: /* no outputs */
		: /* no inputs */
		: "d0");

	jmp_addr_label:
	/* pickup reset address from AmigaOS ROM, reset devices and jump
	 * to reset address
	 */
	asm volatile ("\n"
		"	move.w	#0x2700,%sr\n"
		"	lea	0x01000000,%a0\n"
		"	sub.l	%a0@(-0x14),%a0\n"
		"	move.l	%a0@(4),%a0\n"
		"	subq.l	#2,%a0\n"
		"	jra	1f\n"
		/* align on a longword boundary */
		"	" __ALIGN_STR "\n"
		"1:\n"
		"	reset\n"
		"	jmp   %a0@");

	for (;;)
		;
}


    /*
     *  Debugging
     */

#define SAVEKMSG_MAXMEM		128*1024

#define SAVEKMSG_MAGIC1		0x53415645	/* 'SAVE' */
#define SAVEKMSG_MAGIC2		0x4B4D5347	/* 'KMSG' */

struct savekmsg {
	unsigned long magic1;		/* SAVEKMSG_MAGIC1 */
	unsigned long magic2;		/* SAVEKMSG_MAGIC2 */
	unsigned long magicptr;		/* address of magic1 */
	unsigned long size;
	char data[0];
};

static struct savekmsg *savekmsg;

static void amiga_mem_console_write(struct console *co, const char *s,
				    unsigned int count)
{
	if (savekmsg->size + count <= SAVEKMSG_MAXMEM-sizeof(struct savekmsg)) {
		memcpy(savekmsg->data + savekmsg->size, s, count);
		savekmsg->size += count;
	}
}

static int __init amiga_savekmsg_setup(char *arg)
{
	bool registered;

	if (!MACH_IS_AMIGA || strcmp(arg, "mem"))
		return 0;

	if (amiga_chip_size < SAVEKMSG_MAXMEM) {
		pr_err("Not enough chipram for debugging\n");
		return -ENOMEM;
	}

	/* Just steal the block, the chipram allocator isn't functional yet */
	amiga_chip_size -= SAVEKMSG_MAXMEM;
	savekmsg = ZTWO_VADDR(CHIP_PHYSADDR + amiga_chip_size);
	savekmsg->magic1 = SAVEKMSG_MAGIC1;
	savekmsg->magic2 = SAVEKMSG_MAGIC2;
	savekmsg->magicptr = ZTWO_PADDR(savekmsg);
	savekmsg->size = 0;

	registered = !!amiga_console_driver.write;
	amiga_console_driver.write = amiga_mem_console_write;
	if (!registered)
		register_console(&amiga_console_driver);
	return 0;
}

early_param("debug", amiga_savekmsg_setup);

static void amiga_serial_putc(char c)
{
	amiga_custom.serdat = (unsigned char)c | 0x100;
	while (!(amiga_custom.serdatr & 0x2000))
		;
}

static void amiga_serial_console_write(struct console *co, const char *s,
				       unsigned int count)
{
	while (count--) {
		if (*s == '\n')
			amiga_serial_putc('\r');
		amiga_serial_putc(*s++);
	}
}

#if 0
void amiga_serial_puts(const char *s)
{
	amiga_serial_console_write(NULL, s, strlen(s));
}

int amiga_serial_console_wait_key(struct console *co)
{
	int ch;

	while (!(amiga_custom.intreqr & IF_RBF))
		barrier();
	ch = amiga_custom.serdatr & 0xff;
	/* clear the interrupt, so that another character can be read */
	amiga_custom.intreq = IF_RBF;
	return ch;
}

void amiga_serial_gets(struct console *co, char *s, int len)
{
	int ch, cnt = 0;

	while (1) {
		ch = amiga_serial_console_wait_key(co);

		/* Check for backspace. */
		if (ch == 8 || ch == 127) {
			if (cnt == 0) {
				amiga_serial_putc('\007');
				continue;
			}
			cnt--;
			amiga_serial_puts("\010 \010");
			continue;
		}

		/* Check for enter. */
		if (ch == 10 || ch == 13)
			break;

		/* See if line is too long. */
		if (cnt >= len + 1) {
			amiga_serial_putc(7);
			cnt--;
			continue;
		}

		/* Store and echo character. */
		s[cnt++] = ch;
		amiga_serial_putc(ch);
	}
	/* Print enter. */
	amiga_serial_puts("\r\n");
	s[cnt] = 0;
}
#endif

static int __init amiga_debug_setup(char *arg)
{
	bool registered;

	if (!MACH_IS_AMIGA || strcmp(arg, "ser"))
		return 0;

	/* no initialization required (?) */
	registered = !!amiga_console_driver.write;
	amiga_console_driver.write = amiga_serial_console_write;
	if (!registered)
		register_console(&amiga_console_driver);
	return 0;
}

early_param("debug", amiga_debug_setup);

#ifdef CONFIG_HEARTBEAT
static void amiga_heartbeat(int on)
{
	if (on)
		ciaa.pra &= ~2;
	else
		ciaa.pra |= 2;
}
#endif

    /*
     *  Amiga specific parts of /proc
     */

static void amiga_get_model(char *model)
{
	strcpy(model, amiga_model_name);
}


static void amiga_get_hardware_list(struct seq_file *m)
{
	if (AMIGAHW_PRESENT(CHIP_RAM))
		seq_printf(m, "Chip RAM:\t%ldK\n", amiga_chip_size>>10);
	seq_printf(m, "PS Freq:\t%dHz\nEClock Freq:\t%ldHz\n",
			amiga_psfreq, amiga_eclock);
	if (AMIGAHW_PRESENT(AMI_VIDEO)) {
		char *type;
		switch (amiga_chipset) {
		case CS_OCS:
			type = "OCS";
			break;
		case CS_ECS:
			type = "ECS";
			break;
		case CS_AGA:
			type = "AGA";
			break;
		default:
			type = "Old or Unknown";
			break;
		}
		seq_printf(m, "Graphics:\t%s\n", type);
	}

#define AMIGAHW_ANNOUNCE(name, str)			\
	if (AMIGAHW_PRESENT(name))			\
		seq_printf (m, "\t%s\n", str)

	seq_puts(m, "Detected hardware:\n");
	AMIGAHW_ANNOUNCE(AMI_VIDEO, "Amiga Video");
	AMIGAHW_ANNOUNCE(AMI_BLITTER, "Blitter");
	AMIGAHW_ANNOUNCE(AMBER_FF, "Amber Flicker Fixer");
	AMIGAHW_ANNOUNCE(AMI_AUDIO, "Amiga Audio");
	AMIGAHW_ANNOUNCE(AMI_FLOPPY, "Floppy Controller");
	AMIGAHW_ANNOUNCE(A3000_SCSI, "SCSI Controller WD33C93 (A3000 style)");
	AMIGAHW_ANNOUNCE(A4000_SCSI, "SCSI Controller NCR53C710 (A4000T style)");
	AMIGAHW_ANNOUNCE(A1200_IDE, "IDE Interface (A1200 style)");
	AMIGAHW_ANNOUNCE(A4000_IDE, "IDE Interface (A4000 style)");
	AMIGAHW_ANNOUNCE(CD_ROM, "Internal CD ROM drive");
	AMIGAHW_ANNOUNCE(AMI_KEYBOARD, "Keyboard");
	AMIGAHW_ANNOUNCE(AMI_MOUSE, "Mouse Port");
	AMIGAHW_ANNOUNCE(AMI_SERIAL, "Serial Port");
	AMIGAHW_ANNOUNCE(AMI_PARALLEL, "Parallel Port");
	AMIGAHW_ANNOUNCE(A2000_CLK, "Hardware Clock (A2000 style)");
	AMIGAHW_ANNOUNCE(A3000_CLK, "Hardware Clock (A3000 style)");
	AMIGAHW_ANNOUNCE(CHIP_RAM, "Chip RAM");
	AMIGAHW_ANNOUNCE(PAULA, "Paula 8364");
	AMIGAHW_ANNOUNCE(DENISE, "Denise 8362");
	AMIGAHW_ANNOUNCE(DENISE_HR, "Denise 8373");
	AMIGAHW_ANNOUNCE(LISA, "Lisa 8375");
	AMIGAHW_ANNOUNCE(AGNUS_PAL, "Normal/Fat PAL Agnus 8367/8371");
	AMIGAHW_ANNOUNCE(AGNUS_NTSC, "Normal/Fat NTSC Agnus 8361/8370");
	AMIGAHW_ANNOUNCE(AGNUS_HR_PAL, "Fat Hires PAL Agnus 8372");
	AMIGAHW_ANNOUNCE(AGNUS_HR_NTSC, "Fat Hires NTSC Agnus 8372");
	AMIGAHW_ANNOUNCE(ALICE_PAL, "PAL Alice 8374");
	AMIGAHW_ANNOUNCE(ALICE_NTSC, "NTSC Alice 8374");
	AMIGAHW_ANNOUNCE(MAGIC_REKICK, "Magic Hard Rekick");
	AMIGAHW_ANNOUNCE(PCMCIA, "PCMCIA Slot");
#ifdef CONFIG_ZORRO
	if (AMIGAHW_PRESENT(ZORRO))
		seq_printf(m, "\tZorro II%s AutoConfig: %d Expansion "
				"Device%s\n",
				AMIGAHW_PRESENT(ZORRO3) ? "I" : "",
				zorro_num_autocon, zorro_num_autocon == 1 ? "" : "s");
#endif /* CONFIG_ZORRO */

#undef AMIGAHW_ANNOUNCE
}

/*
 * The Amiga keyboard driver needs key_maps, but we cannot export it in
 * drivers/char/defkeymap.c, as it is autogenerated
 */
#ifdef CONFIG_HW_CONSOLE
EXPORT_SYMBOL_GPL(key_maps);
#endif
