/*
 *  linux/arch/m68k/atari/config.c
 *
 *  Copyright (C) 1994 Bjoern Brauel
 *
 *  5/2/94 Roman Hodek:
 *    Added setting of time_adj to get a better clock.
 *
 *  5/14/94 Roman Hodek:
 *    gettod() for TT
 *
 *  5/15/94 Roman Hodek:
 *    hard_reset_now() for Atari (and others?)
 *
 *  94/12/30 Andreas Schwab:
 *    atari_sched_init fixed to get precise clock.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/*
 * Miscellaneous atari stuff
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/usb/isp116x.h>
#include <linux/vt_kern.h>
#include <linux/module.h>

#include <asm/bootinfo.h>
#include <asm/bootinfo-atari.h>
#include <asm/byteorder.h>
#include <asm/setup.h>
#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/atari_stram.h>
#include <asm/machdep.h>
#include <asm/hwtest.h>
#include <asm/io.h>

u_long atari_mch_cookie;
EXPORT_SYMBOL(atari_mch_cookie);

u_long atari_mch_type;
EXPORT_SYMBOL(atari_mch_type);

struct atari_hw_present atari_hw_present;
EXPORT_SYMBOL(atari_hw_present);

u_long atari_switches;
EXPORT_SYMBOL(atari_switches);

int atari_dont_touch_floppy_select;
EXPORT_SYMBOL(atari_dont_touch_floppy_select);

int atari_rtc_year_offset;

/* local function prototypes */
static void atari_reset(void);
static void atari_get_model(char *model);
static void atari_get_hardware_list(struct seq_file *m);

/* atari specific irq functions */
extern void atari_init_IRQ (void);
extern void atari_mksound(unsigned int count, unsigned int ticks);
#ifdef CONFIG_HEARTBEAT
static void atari_heartbeat(int on);
#endif

/* atari specific timer functions (in time.c) */
extern void atari_sched_init(irq_handler_t);
extern u32 atari_gettimeoffset(void);
extern int atari_mste_hwclk (int, struct rtc_time *);
extern int atari_tt_hwclk (int, struct rtc_time *);

/* ++roman: This is a more elaborate test for an SCC chip, since the plain
 * Medusa board generates DTACK at the SCC's standard addresses, but a SCC
 * board in the Medusa is possible. Also, the addresses where the ST_ESCC
 * resides generate DTACK without the chip, too.
 * The method is to write values into the interrupt vector register, that
 * should be readable without trouble (from channel A!).
 */

static int __init scc_test(volatile char *ctla)
{
	if (!hwreg_present(ctla))
		return 0;
	MFPDELAY();

	*ctla = 2;
	MFPDELAY();
	*ctla = 0x40;
	MFPDELAY();

	*ctla = 2;
	MFPDELAY();
	if (*ctla != 0x40)
		return 0;
	MFPDELAY();

	*ctla = 2;
	MFPDELAY();
	*ctla = 0x60;
	MFPDELAY();

	*ctla = 2;
	MFPDELAY();
	if (*ctla != 0x60)
		return 0;

	return 1;
}


    /*
     *  Parse an Atari-specific record in the bootinfo
     */

int __init atari_parse_bootinfo(const struct bi_record *record)
{
	int unknown = 0;
	const void *data = record->data;

	switch (be16_to_cpu(record->tag)) {
	case BI_ATARI_MCH_COOKIE:
		atari_mch_cookie = be32_to_cpup(data);
		break;
	case BI_ATARI_MCH_TYPE:
		atari_mch_type = be32_to_cpup(data);
		break;
	default:
		unknown = 1;
		break;
	}
	return unknown;
}


/* Parse the Atari-specific switches= option. */
static int __init atari_switches_setup(char *str)
{
	char switches[strlen(str) + 1];
	char *p;
	int ovsc_shift;
	char *args = switches;

	if (!MACH_IS_ATARI)
		return 0;

	/* copy string to local array, strsep works destructively... */
	strcpy(switches, str);
	atari_switches = 0;

	/* parse the options */
	while ((p = strsep(&args, ",")) != NULL) {
		if (!*p)
			continue;
		ovsc_shift = 0;
		if (strncmp(p, "ov_", 3) == 0) {
			p += 3;
			ovsc_shift = ATARI_SWITCH_OVSC_SHIFT;
		}

		if (strcmp(p, "ikbd") == 0) {
			/* RTS line of IKBD ACIA */
			atari_switches |= ATARI_SWITCH_IKBD << ovsc_shift;
		} else if (strcmp(p, "midi") == 0) {
			/* RTS line of MIDI ACIA */
			atari_switches |= ATARI_SWITCH_MIDI << ovsc_shift;
		} else if (strcmp(p, "snd6") == 0) {
			atari_switches |= ATARI_SWITCH_SND6 << ovsc_shift;
		} else if (strcmp(p, "snd7") == 0) {
			atari_switches |= ATARI_SWITCH_SND7 << ovsc_shift;
		}
	}
	return 0;
}

early_param("switches", atari_switches_setup);


    /*
     *  Setup the Atari configuration info
     */

void __init config_atari(void)
{
	unsigned short tos_version;

	memset(&atari_hw_present, 0, sizeof(atari_hw_present));

	/* Change size of I/O space from 64KB to 4GB. */
	ioport_resource.end  = 0xFFFFFFFF;

	mach_sched_init      = atari_sched_init;
	mach_init_IRQ        = atari_init_IRQ;
	mach_get_model	 = atari_get_model;
	mach_get_hardware_list = atari_get_hardware_list;
	arch_gettimeoffset   = atari_gettimeoffset;
	mach_reset           = atari_reset;
	mach_max_dma_address = 0xffffff;
#if IS_ENABLED(CONFIG_INPUT_M68K_BEEP)
	mach_beep          = atari_mksound;
#endif
#ifdef CONFIG_HEARTBEAT
	mach_heartbeat = atari_heartbeat;
#endif

	/* Set switches as requested by the user */
	if (atari_switches & ATARI_SWITCH_IKBD)
		acia.key_ctrl = ACIA_DIV64 | ACIA_D8N1S | ACIA_RHTID;
	if (atari_switches & ATARI_SWITCH_MIDI)
		acia.mid_ctrl = ACIA_DIV16 | ACIA_D8N1S | ACIA_RHTID;
	if (atari_switches & (ATARI_SWITCH_SND6|ATARI_SWITCH_SND7)) {
		sound_ym.rd_data_reg_sel = 14;
		sound_ym.wd_data = sound_ym.rd_data_reg_sel |
				   ((atari_switches&ATARI_SWITCH_SND6) ? 0x40 : 0) |
				   ((atari_switches&ATARI_SWITCH_SND7) ? 0x80 : 0);
	}

	/* ++bjoern:
	 * Determine hardware present
	 */

	pr_info("Atari hardware found:");
	if (MACH_IS_MEDUSA) {
		/* There's no Atari video hardware on the Medusa, but all the
		 * addresses below generate a DTACK so no bus error occurs! */
	} else if (hwreg_present(f030_xreg)) {
		ATARIHW_SET(VIDEL_SHIFTER);
		pr_cont(" VIDEL");
		/* This is a temporary hack: If there is Falcon video
		 * hardware, we assume that the ST-DMA serves SCSI instead of
		 * ACSI. In the future, there should be a better method for
		 * this...
		 */
		ATARIHW_SET(ST_SCSI);
		pr_cont(" STDMA-SCSI");
	} else if (hwreg_present(tt_palette)) {
		ATARIHW_SET(TT_SHIFTER);
		pr_cont(" TT_SHIFTER");
	} else if (hwreg_present(&shifter.bas_hi)) {
		if (hwreg_present(&shifter.bas_lo) &&
		    (shifter.bas_lo = 0x0aau, shifter.bas_lo == 0x0aau)) {
			ATARIHW_SET(EXTD_SHIFTER);
			pr_cont(" EXTD_SHIFTER");
		} else {
			ATARIHW_SET(STND_SHIFTER);
			pr_cont(" STND_SHIFTER");
		}
	}
	if (hwreg_present(&st_mfp.par_dt_reg)) {
		ATARIHW_SET(ST_MFP);
		pr_cont(" ST_MFP");
	}
	if (hwreg_present(&tt_mfp.par_dt_reg)) {
		ATARIHW_SET(TT_MFP);
		pr_cont(" TT_MFP");
	}
	if (hwreg_present(&tt_scsi_dma.dma_addr_hi)) {
		ATARIHW_SET(SCSI_DMA);
		pr_cont(" TT_SCSI_DMA");
	}
	/*
	 * The ST-DMA address registers aren't readable
	 * on all Medusas, so the test below may fail
	 */
	if (MACH_IS_MEDUSA ||
	    (hwreg_present(&st_dma.dma_vhi) &&
	     (st_dma.dma_vhi = 0x55) && (st_dma.dma_hi = 0xaa) &&
	     st_dma.dma_vhi == 0x55 && st_dma.dma_hi == 0xaa &&
	     (st_dma.dma_vhi = 0xaa) && (st_dma.dma_hi = 0x55) &&
	     st_dma.dma_vhi == 0xaa && st_dma.dma_hi == 0x55)) {
		ATARIHW_SET(EXTD_DMA);
		pr_cont(" EXTD_DMA");
	}
	if (hwreg_present(&tt_scsi.scsi_data)) {
		ATARIHW_SET(TT_SCSI);
		pr_cont(" TT_SCSI");
	}
	if (hwreg_present(&sound_ym.rd_data_reg_sel)) {
		ATARIHW_SET(YM_2149);
		pr_cont(" YM2149");
	}
	if (!MACH_IS_MEDUSA && hwreg_present(&tt_dmasnd.ctrl)) {
		ATARIHW_SET(PCM_8BIT);
		pr_cont(" PCM");
	}
	if (hwreg_present(&falcon_codec.unused5)) {
		ATARIHW_SET(CODEC);
		pr_cont(" CODEC");
	}
	if (hwreg_present(&dsp56k_host_interface.icr)) {
		ATARIHW_SET(DSP56K);
		pr_cont(" DSP56K");
	}
	if (hwreg_present(&tt_scc_dma.dma_ctrl) &&
#if 0
	    /* This test sucks! Who knows some better? */
	    (tt_scc_dma.dma_ctrl = 0x01, (tt_scc_dma.dma_ctrl & 1) == 1) &&
	    (tt_scc_dma.dma_ctrl = 0x00, (tt_scc_dma.dma_ctrl & 1) == 0)
#else
	    !MACH_IS_MEDUSA
#endif
	    ) {
		ATARIHW_SET(SCC_DMA);
		pr_cont(" SCC_DMA");
	}
	if (scc_test(&atari_scc.cha_a_ctrl)) {
		ATARIHW_SET(SCC);
		pr_cont(" SCC");
	}
	if (scc_test(&st_escc.cha_b_ctrl)) {
		ATARIHW_SET(ST_ESCC);
		pr_cont(" ST_ESCC");
	}
	if (hwreg_present(&tt_scu.sys_mask)) {
		ATARIHW_SET(SCU);
		/* Assume a VME bus if there's a SCU */
		ATARIHW_SET(VME);
		pr_cont(" VME SCU");
	}
	if (hwreg_present((void *)(0xffff9210))) {
		ATARIHW_SET(ANALOG_JOY);
		pr_cont(" ANALOG_JOY");
	}
	if (hwreg_present(blitter.halftone)) {
		ATARIHW_SET(BLITTER);
		pr_cont(" BLITTER");
	}
	if (hwreg_present((void *)0xfff00039)) {
		ATARIHW_SET(IDE);
		pr_cont(" IDE");
	}
#if 1 /* This maybe wrong */
	if (!MACH_IS_MEDUSA && hwreg_present(&tt_microwire.data) &&
	    hwreg_present(&tt_microwire.mask) &&
	    (tt_microwire.mask = 0x7ff,
	     udelay(1),
	     tt_microwire.data = MW_LM1992_PSG_HIGH | MW_LM1992_ADDR,
	     udelay(1),
	     tt_microwire.data != 0)) {
		ATARIHW_SET(MICROWIRE);
		while (tt_microwire.mask != 0x7ff)
			;
		pr_cont(" MICROWIRE");
	}
#endif
	if (hwreg_present(&tt_rtc.regsel)) {
		ATARIHW_SET(TT_CLK);
		pr_cont(" TT_CLK");
		mach_hwclk = atari_tt_hwclk;
	}
	if (hwreg_present(&mste_rtc.sec_ones)) {
		ATARIHW_SET(MSTE_CLK);
		pr_cont(" MSTE_CLK");
		mach_hwclk = atari_mste_hwclk;
	}
	if (!MACH_IS_MEDUSA && hwreg_present(&dma_wd.fdc_speed) &&
	    hwreg_write(&dma_wd.fdc_speed, 0)) {
		ATARIHW_SET(FDCSPEED);
		pr_cont(" FDC_SPEED");
	}
	if (!ATARIHW_PRESENT(ST_SCSI)) {
		ATARIHW_SET(ACSI);
		pr_cont(" ACSI");
	}
	pr_cont("\n");

	if (CPU_IS_040_OR_060)
		/* Now it seems to be safe to turn of the tt0 transparent
		 * translation (the one that must not be turned off in
		 * head.S...)
		 */
		asm volatile ("\n"
			"	moveq	#0,%%d0\n"
			"	.chip	68040\n"
			"	movec	%%d0,%%itt0\n"
			"	movec	%%d0,%%dtt0\n"
			"	.chip	68k"
			: /* no outputs */
			: /* no inputs */
			: "d0");

	/* allocator for memory that must reside in st-ram */
	atari_stram_init();

	/* Set up a mapping for the VMEbus address region:
	 *
	 * VME is either at phys. 0xfexxxxxx (TT) or 0xa00000..0xdfffff
	 * (MegaSTE) In both cases, the whole 16 MB chunk is mapped at
	 * 0xfe000000 virt., because this can be done with a single
	 * transparent translation. On the 68040, lots of often unused
	 * page tables would be needed otherwise. On a MegaSTE or similar,
	 * the highest byte is stripped off by hardware due to the 24 bit
	 * design of the bus.
	 */

	if (CPU_IS_020_OR_030) {
		unsigned long tt1_val;
		tt1_val = 0xfe008543;	/* Translate 0xfexxxxxx, enable, cache
					 * inhibit, read and write, FDC mask = 3,
					 * FDC val = 4 -> Supervisor only */
		asm volatile ("\n"
			"	.chip	68030\n"
			"	pmove	%0,%/tt1\n"
			"	.chip	68k"
			: : "m" (tt1_val));
	} else {
	        asm volatile ("\n"
			"	.chip	68040\n"
			"	movec	%0,%%itt1\n"
			"	movec	%0,%%dtt1\n"
			"	.chip	68k"
			:
			: "d" (0xfe00a040));	/* Translate 0xfexxxxxx, enable,
						 * supervisor only, non-cacheable/
						 * serialized, writable */

	}

	/* Fetch tos version at Physical 2 */
	/*
	 * We my not be able to access this address if the kernel is
	 * loaded to st ram, since the first page is unmapped.  On the
	 * Medusa this is always the case and there is nothing we can do
	 * about this, so we just assume the smaller offset.  For the TT
	 * we use the fact that in head.S we have set up a mapping
	 * 0xFFxxxxxx -> 0x00xxxxxx, so that the first 16MB is accessible
	 * in the last 16MB of the address space.
	 */
	tos_version = (MACH_IS_MEDUSA) ?
			0xfff : *(unsigned short *)0xff000002;
	atari_rtc_year_offset = (tos_version < 0x306) ? 70 : 68;
}

#ifdef CONFIG_HEARTBEAT
static void atari_heartbeat(int on)
{
	unsigned char tmp;
	unsigned long flags;

	if (atari_dont_touch_floppy_select)
		return;

	local_irq_save(flags);
	sound_ym.rd_data_reg_sel = 14;	/* Select PSG Port A */
	tmp = sound_ym.rd_data_reg_sel;
	sound_ym.wd_data = on ? (tmp & ~0x02) : (tmp | 0x02);
	local_irq_restore(flags);
}
#endif

/* ++roman:
 *
 * This function does a reset on machines that lack the ability to
 * assert the processor's _RESET signal somehow via hardware. It is
 * based on the fact that you can find the initial SP and PC values
 * after a reset at physical addresses 0 and 4. This works pretty well
 * for Atari machines, since the lowest 8 bytes of physical memory are
 * really ROM (mapped by hardware). For other 680x0 machines: don't
 * know if it works...
 *
 * To get the values at addresses 0 and 4, the MMU better is turned
 * off first. After that, we have to jump into physical address space
 * (the PC before the pmove statement points to the virtual address of
 * the code). Getting that physical address is not hard, but the code
 * becomes a bit complex since I've tried to ensure that the jump
 * statement after the pmove is in the cache already (otherwise the
 * processor can't fetch it!). For that, the code first jumps to the
 * jump statement with the (virtual) address of the pmove section in
 * an address register . The jump statement is surely in the cache
 * now. After that, that physical address of the reset code is loaded
 * into the same address register, pmove is done and the same jump
 * statements goes to the reset code. Since there are not many
 * statements between the two jumps, I hope it stays in the cache.
 *
 * The C code makes heavy use of the GCC features that you can get the
 * address of a C label. No hope to compile this with another compiler
 * than GCC!
 */

/* ++andreas: no need for complicated code, just depend on prefetch */

static void atari_reset(void)
{
	long tc_val = 0;
	long reset_addr;

	/*
	 * On the Medusa, phys. 0x4 may contain garbage because it's no
	 * ROM.  See above for explanation why we cannot use PTOV(4).
	 */
	reset_addr = MACH_IS_MEDUSA || MACH_IS_AB40 ? 0xe00030 :
		     *(unsigned long *) 0xff000004;

	/* reset ACIA for switch off OverScan, if it's active */
	if (atari_switches & ATARI_SWITCH_OVSC_IKBD)
		acia.key_ctrl = ACIA_RESET;
	if (atari_switches & ATARI_SWITCH_OVSC_MIDI)
		acia.mid_ctrl = ACIA_RESET;

	/* processor independent: turn off interrupts and reset the VBR;
	 * the caches must be left enabled, else prefetching the final jump
	 * instruction doesn't work.
	 */
	local_irq_disable();
	asm volatile ("movec	%0,%%vbr"
			: : "d" (0));

	if (CPU_IS_040_OR_060) {
		unsigned long jmp_addr040 = virt_to_phys(&&jmp_addr_label040);
		if (CPU_IS_060) {
			/* 68060: clear PCR to turn off superscalar operation */
			asm volatile ("\n"
				"	.chip 68060\n"
				"	movec %0,%%pcr\n"
				"	.chip 68k"
				: : "d" (0));
		}

		asm volatile ("\n"
			"	move.l	%0,%%d0\n"
			"	and.l	#0xff000000,%%d0\n"
			"	or.w	#0xe020,%%d0\n"   /* map 16 MB, enable, cacheable */
			"	.chip	68040\n"
			"	movec	%%d0,%%itt0\n"
			"	movec	%%d0,%%dtt0\n"
			"	.chip	68k\n"
			"	jmp	%0@"
			: : "a" (jmp_addr040)
			: "d0");
	jmp_addr_label040:
		asm volatile ("\n"
			"	moveq	#0,%%d0\n"
			"	nop\n"
			"	.chip	68040\n"
			"	cinva	%%bc\n"
			"	nop\n"
			"	pflusha\n"
			"	nop\n"
			"	movec	%%d0,%%tc\n"
			"	nop\n"
			/* the following setup of transparent translations is needed on the
			 * Afterburner040 to successfully reboot. Other machines shouldn't
			 * care about a different tt regs setup, they also didn't care in
			 * the past that the regs weren't turned off. */
			"	move.l	#0xffc000,%%d0\n" /* whole insn space cacheable */
			"	movec	%%d0,%%itt0\n"
			"	movec	%%d0,%%itt1\n"
			"	or.w	#0x40,%/d0\n" /* whole data space non-cacheable/ser. */
			"	movec	%%d0,%%dtt0\n"
			"	movec	%%d0,%%dtt1\n"
			"	.chip	68k\n"
			"	jmp	%0@"
			: /* no outputs */
			: "a" (reset_addr)
			: "d0");
	} else
		asm volatile ("\n"
			"	pmove	%0,%%tc\n"
			"	jmp	%1@"
			: /* no outputs */
			: "m" (tc_val), "a" (reset_addr));
}


static void atari_get_model(char *model)
{
	strcpy(model, "Atari ");
	switch (atari_mch_cookie >> 16) {
	case ATARI_MCH_ST:
		if (ATARIHW_PRESENT(MSTE_CLK))
			strcat(model, "Mega ST");
		else
			strcat(model, "ST");
		break;
	case ATARI_MCH_STE:
		if (MACH_IS_MSTE)
			strcat(model, "Mega STE");
		else
			strcat(model, "STE");
		break;
	case ATARI_MCH_TT:
		if (MACH_IS_MEDUSA)
			/* Medusa has TT _MCH cookie */
			strcat(model, "Medusa");
		else
			strcat(model, "TT");
		break;
	case ATARI_MCH_FALCON:
		strcat(model, "Falcon");
		if (MACH_IS_AB40)
			strcat(model, " (with Afterburner040)");
		break;
	default:
		sprintf(model + strlen(model), "(unknown mach cookie 0x%lx)",
			atari_mch_cookie);
		break;
	}
}


static void atari_get_hardware_list(struct seq_file *m)
{
	int i;

	for (i = 0; i < m68k_num_memory; i++)
		seq_printf(m, "\t%3ld MB at 0x%08lx (%s)\n",
				m68k_memory[i].size >> 20, m68k_memory[i].addr,
				(m68k_memory[i].addr & 0xff000000 ?
				 "alternate RAM" : "ST-RAM"));

#define ATARIHW_ANNOUNCE(name, str)			\
	if (ATARIHW_PRESENT(name))			\
		seq_printf(m, "\t%s\n", str)

	seq_puts(m, "Detected hardware:\n");
	ATARIHW_ANNOUNCE(STND_SHIFTER, "ST Shifter");
	ATARIHW_ANNOUNCE(EXTD_SHIFTER, "STe Shifter");
	ATARIHW_ANNOUNCE(TT_SHIFTER, "TT Shifter");
	ATARIHW_ANNOUNCE(VIDEL_SHIFTER, "Falcon Shifter");
	ATARIHW_ANNOUNCE(YM_2149, "Programmable Sound Generator");
	ATARIHW_ANNOUNCE(PCM_8BIT, "PCM 8 Bit Sound");
	ATARIHW_ANNOUNCE(CODEC, "CODEC Sound");
	ATARIHW_ANNOUNCE(TT_SCSI, "SCSI Controller NCR5380 (TT style)");
	ATARIHW_ANNOUNCE(ST_SCSI, "SCSI Controller NCR5380 (Falcon style)");
	ATARIHW_ANNOUNCE(ACSI, "ACSI Interface");
	ATARIHW_ANNOUNCE(IDE, "IDE Interface");
	ATARIHW_ANNOUNCE(FDCSPEED, "8/16 Mhz Switch for FDC");
	ATARIHW_ANNOUNCE(ST_MFP, "Multi Function Peripheral MFP 68901");
	ATARIHW_ANNOUNCE(TT_MFP, "Second Multi Function Peripheral MFP 68901");
	ATARIHW_ANNOUNCE(SCC, "Serial Communications Controller SCC 8530");
	ATARIHW_ANNOUNCE(ST_ESCC, "Extended Serial Communications Controller SCC 85230");
	ATARIHW_ANNOUNCE(ANALOG_JOY, "Paddle Interface");
	ATARIHW_ANNOUNCE(MICROWIRE, "MICROWIRE(tm) Interface");
	ATARIHW_ANNOUNCE(STND_DMA, "DMA Controller (24 bit)");
	ATARIHW_ANNOUNCE(EXTD_DMA, "DMA Controller (32 bit)");
	ATARIHW_ANNOUNCE(SCSI_DMA, "DMA Controller for NCR5380");
	ATARIHW_ANNOUNCE(SCC_DMA, "DMA Controller for SCC");
	ATARIHW_ANNOUNCE(TT_CLK, "Clock Chip MC146818A");
	ATARIHW_ANNOUNCE(MSTE_CLK, "Clock Chip RP5C15");
	ATARIHW_ANNOUNCE(SCU, "System Control Unit");
	ATARIHW_ANNOUNCE(BLITTER, "Blitter");
	ATARIHW_ANNOUNCE(VME, "VME Bus");
	ATARIHW_ANNOUNCE(DSP56K, "DSP56001 processor");
}

/*
 * MSch: initial platform device support for Atari,
 * required for EtherNAT/EtherNEC/NetUSBee drivers
 */

#if defined(CONFIG_ATARI_ETHERNAT) || defined(CONFIG_ATARI_ETHERNEC)
static void isp1160_delay(struct device *dev, int delay)
{
	ndelay(delay);
}
#endif

#ifdef CONFIG_ATARI_ETHERNAT
/*
 * EtherNAT: SMC91C111 Ethernet chipset, handled by smc91x driver
 */

#define ATARI_ETHERNAT_IRQ		140

static struct resource smc91x_resources[] = {
	[0] = {
		.name	= "smc91x-regs",
		.start	= ATARI_ETHERNAT_PHYS_ADDR,
		.end	= ATARI_ETHERNAT_PHYS_ADDR + 0xfffff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "smc91x-irq",
		.start	= ATARI_ETHERNAT_IRQ,
		.end	= ATARI_ETHERNAT_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

/*
 * ISP 1160 - using the isp116x-hcd module
 */

#define ATARI_USB_PHYS_ADDR	0x80000012
#define ATARI_USB_IRQ		139

static struct resource isp1160_resources[] = {
	[0] = {
		.name	= "isp1160-data",
		.start	= ATARI_USB_PHYS_ADDR,
		.end	= ATARI_USB_PHYS_ADDR + 0x1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "isp1160-regs",
		.start	= ATARI_USB_PHYS_ADDR + 0x4,
		.end	= ATARI_USB_PHYS_ADDR + 0x5,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.name	= "isp1160-irq",
		.start	= ATARI_USB_IRQ,
		.end	= ATARI_USB_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

/* (DataBusWidth16|AnalogOCEnable|DREQOutputPolarity|DownstreamPort15KRSel ) */
static struct isp116x_platform_data isp1160_platform_data = {
	/* Enable internal resistors on downstream ports */
	.sel15Kres		= 1,
	/* On-chip overcurrent protection */
	.oc_enable		= 1,
	/* INT output polarity */
	.int_act_high		= 1,
	/* INT edge or level triggered */
	.int_edge_triggered	= 0,

	/* WAKEUP pin connected - NOT SUPPORTED  */
	/* .remote_wakeup_connected = 0, */
	/* Wakeup by devices on usb bus enabled */
	.remote_wakeup_enable	= 0,
	.delay			= isp1160_delay,
};

static struct platform_device isp1160_device = {
	.name		= "isp116x-hcd",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(isp1160_resources),
	.resource	= isp1160_resources,
	.dev			= {
		.platform_data	= &isp1160_platform_data,
	},
};

static struct platform_device *atari_ethernat_devices[] __initdata = {
	&smc91x_device,
	&isp1160_device
};
#endif /* CONFIG_ATARI_ETHERNAT */

#ifdef CONFIG_ATARI_ETHERNEC
/*
 * EtherNEC: RTL8019 (NE2000 compatible) Ethernet chipset,
 * handled by ne.c driver
 */

#define ATARI_ETHERNEC_PHYS_ADDR	0xfffa0000
#define ATARI_ETHERNEC_BASE		0x300
#define ATARI_ETHERNEC_IRQ		IRQ_MFP_TIMER1

static struct resource rtl8019_resources[] = {
	[0] = {
		.name	= "rtl8019-regs",
		.start	= ATARI_ETHERNEC_BASE,
		.end	= ATARI_ETHERNEC_BASE + 0x20 - 1,
		.flags	= IORESOURCE_IO,
	},
	[1] = {
		.name	= "rtl8019-irq",
		.start	= ATARI_ETHERNEC_IRQ,
		.end	= ATARI_ETHERNEC_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device rtl8019_device = {
	.name		= "ne",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rtl8019_resources),
	.resource	= rtl8019_resources,
};

/*
 * NetUSBee: ISP1160 USB host adapter via ROM-port adapter
 */

#define ATARI_NETUSBEE_PHYS_ADDR	0xfffa8000
#define ATARI_NETUSBEE_BASE		0x340
#define ATARI_NETUSBEE_IRQ		IRQ_MFP_TIMER2

static struct resource netusbee_resources[] = {
	[0] = {
		.name	= "isp1160-data",
		.start	= ATARI_NETUSBEE_BASE,
		.end	= ATARI_NETUSBEE_BASE + 0x1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "isp1160-regs",
		.start	= ATARI_NETUSBEE_BASE + 0x20,
		.end	= ATARI_NETUSBEE_BASE + 0x21,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.name	= "isp1160-irq",
		.start	= ATARI_NETUSBEE_IRQ,
		.end	= ATARI_NETUSBEE_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

/* (DataBusWidth16|AnalogOCEnable|DREQOutputPolarity|DownstreamPort15KRSel ) */
static struct isp116x_platform_data netusbee_platform_data = {
	/* Enable internal resistors on downstream ports */
	.sel15Kres		= 1,
	/* On-chip overcurrent protection */
	.oc_enable		= 1,
	/* INT output polarity */
	.int_act_high		= 1,
	/* INT edge or level triggered */
	.int_edge_triggered	= 0,

	/* WAKEUP pin connected - NOT SUPPORTED  */
	/* .remote_wakeup_connected = 0, */
	/* Wakeup by devices on usb bus enabled */
	.remote_wakeup_enable	= 0,
	.delay			= isp1160_delay,
};

static struct platform_device netusbee_device = {
	.name		= "isp116x-hcd",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(netusbee_resources),
	.resource	= netusbee_resources,
	.dev			= {
		.platform_data	= &netusbee_platform_data,
	},
};

static struct platform_device *atari_netusbee_devices[] __initdata = {
	&rtl8019_device,
	&netusbee_device
};
#endif /* CONFIG_ATARI_ETHERNEC */

#if IS_ENABLED(CONFIG_ATARI_SCSI)
static const struct resource atari_scsi_st_rsrc[] __initconst = {
	{
		.flags = IORESOURCE_IRQ,
		.start = IRQ_MFP_FSCSI,
		.end   = IRQ_MFP_FSCSI,
	},
};

static const struct resource atari_scsi_tt_rsrc[] __initconst = {
	{
		.flags = IORESOURCE_IRQ,
		.start = IRQ_TT_MFP_SCSI,
		.end   = IRQ_TT_MFP_SCSI,
	},
};
#endif

int __init atari_platform_init(void)
{
	int rv = 0;

	if (!MACH_IS_ATARI)
		return -ENODEV;

#ifdef CONFIG_ATARI_ETHERNAT
	{
		unsigned char *enatc_virt;
		enatc_virt = (unsigned char *)ioremap((ATARI_ETHERNAT_PHYS_ADDR+0x23), 0xf);
		if (hwreg_present(enatc_virt)) {
			rv = platform_add_devices(atari_ethernat_devices,
						ARRAY_SIZE(atari_ethernat_devices));
		}
		iounmap(enatc_virt);
	}
#endif

#ifdef CONFIG_ATARI_ETHERNEC
	{
		int error;
		unsigned char *enec_virt;
		enec_virt = (unsigned char *)ioremap((ATARI_ETHERNEC_PHYS_ADDR), 0xf);
		if (hwreg_present(enec_virt)) {
			error = platform_add_devices(atari_netusbee_devices,
						ARRAY_SIZE(atari_netusbee_devices));
			if (error && !rv)
				rv = error;
		}
		iounmap(enec_virt);
	}
#endif

#if IS_ENABLED(CONFIG_ATARI_SCSI)
	if (ATARIHW_PRESENT(ST_SCSI))
		platform_device_register_simple("atari_scsi", -1,
			atari_scsi_st_rsrc, ARRAY_SIZE(atari_scsi_st_rsrc));
	else if (ATARIHW_PRESENT(TT_SCSI))
		platform_device_register_simple("atari_scsi", -1,
			atari_scsi_tt_rsrc, ARRAY_SIZE(atari_scsi_tt_rsrc));
#endif

	return rv;
}

arch_initcall(atari_platform_init);
