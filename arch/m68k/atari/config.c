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
#include <linux/console.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/vt_kern.h>

#include <asm/bootinfo.h>
#include <asm/setup.h>
#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/atari_stram.h>
#include <asm/system.h>
#include <asm/machdep.h>
#include <asm/hwtest.h>
#include <asm/io.h>

u_long atari_mch_cookie;
u_long atari_mch_type;
struct atari_hw_present atari_hw_present;
u_long atari_switches;
int atari_dont_touch_floppy_select;
int atari_rtc_year_offset;

/* local function prototypes */
static void atari_reset( void );
static void atari_get_model(char *model);
static int atari_get_hardware_list(char *buffer);

/* atari specific irq functions */
extern void atari_init_IRQ (void);
extern void atari_mksound( unsigned int count, unsigned int ticks );
#ifdef CONFIG_HEARTBEAT
static void atari_heartbeat( int on );
#endif

/* atari specific timer functions (in time.c) */
extern void atari_sched_init(irq_handler_t );
extern unsigned long atari_gettimeoffset (void);
extern int atari_mste_hwclk (int, struct rtc_time *);
extern int atari_tt_hwclk (int, struct rtc_time *);
extern int atari_mste_set_clock_mmss (unsigned long);
extern int atari_tt_set_clock_mmss (unsigned long);

/* atari specific debug functions (in debug.c) */
extern void atari_debug_init(void);


/* I've moved hwreg_present() and hwreg_present_bywrite() out into
 * mm/hwtest.c, to avoid having multiple copies of the same routine
 * in the kernel [I wanted them in hp300 and they were already used
 * in the nubus code. NB: I don't have an Atari so this might (just
 * conceivably) break something.
 * I've preserved the #if 0 version of hwreg_present_bywrite() here
 * for posterity.
 *   -- Peter Maydell <pmaydell@chiark.greenend.org.uk>, 05/1998
 */

#if 0
static int __init
hwreg_present_bywrite(volatile void *regp, unsigned char val)
{
    int		ret;
    long	save_sp, save_vbr;
    static long tmp_vectors[3] = { [2] = (long)&&after_test };

    __asm__ __volatile__
	(	"movec	%/vbr,%2\n\t"	/* save vbr value            */
                "movec	%4,%/vbr\n\t"	/* set up temporary vectors  */
		"movel	%/sp,%1\n\t"	/* save sp                   */
		"moveq	#0,%0\n\t"	/* assume not present        */
		"moveb	%5,%3@\n\t"	/* write the hardware reg    */
		"cmpb	%3@,%5\n\t"	/* compare it                */
		"seq	%0"		/* comes here only if reg    */
                                        /* is present                */
		: "=d&" (ret), "=r&" (save_sp), "=r&" (save_vbr)
		: "a" (regp), "r" (tmp_vectors), "d" (val)
                );
  after_test:
    __asm__ __volatile__
      (	"movel	%0,%/sp\n\t"		/* restore sp                */
        "movec	%1,%/vbr"			/* restore vbr               */
        : : "r" (save_sp), "r" (save_vbr) : "sp"
	);

    return( ret );
}
#endif


/* ++roman: This is a more elaborate test for an SCC chip, since the plain
 * Medusa board generates DTACK at the SCC's standard addresses, but a SCC
 * board in the Medusa is possible. Also, the addresses where the ST_ESCC
 * resides generate DTACK without the chip, too.
 * The method is to write values into the interrupt vector register, that
 * should be readable without trouble (from channel A!).
 */

static int __init scc_test( volatile char *ctla )
{
	if (!hwreg_present( ctla ))
		return( 0 );
	MFPDELAY();

	*ctla = 2; MFPDELAY();
	*ctla = 0x40; MFPDELAY();

	*ctla = 2; MFPDELAY();
	if (*ctla != 0x40) return( 0 );
	MFPDELAY();

	*ctla = 2; MFPDELAY();
	*ctla = 0x60; MFPDELAY();

	*ctla = 2; MFPDELAY();
	if (*ctla != 0x60) return( 0 );

	return( 1 );
}


    /*
     *  Parse an Atari-specific record in the bootinfo
     */

int __init atari_parse_bootinfo(const struct bi_record *record)
{
    int unknown = 0;
    const u_long *data = record->data;

    switch (record->tag) {
	case BI_ATARI_MCH_COOKIE:
	    atari_mch_cookie = *data;
	    break;
	case BI_ATARI_MCH_TYPE:
	    atari_mch_type = *data;
	    break;
	default:
	    unknown = 1;
    }
    return(unknown);
}


/* Parse the Atari-specific switches= option. */
void __init atari_switches_setup( const char *str, unsigned len )
{
    char switches[len+1];
    char *p;
    int ovsc_shift;
    char *args = switches;

    /* copy string to local array, strsep works destructively... */
    strlcpy( switches, str, sizeof(switches) );
    atari_switches = 0;

    /* parse the options */
    while ((p = strsep(&args, ",")) != NULL) {
	if (!*p) continue;
	ovsc_shift = 0;
	if (strncmp( p, "ov_", 3 ) == 0) {
	    p += 3;
	    ovsc_shift = ATARI_SWITCH_OVSC_SHIFT;
	}

	if (strcmp( p, "ikbd" ) == 0) {
	    /* RTS line of IKBD ACIA */
	    atari_switches |= ATARI_SWITCH_IKBD << ovsc_shift;
	}
	else if (strcmp( p, "midi" ) == 0) {
	    /* RTS line of MIDI ACIA */
	    atari_switches |= ATARI_SWITCH_MIDI << ovsc_shift;
	}
	else if (strcmp( p, "snd6" ) == 0) {
	    atari_switches |= ATARI_SWITCH_SND6 << ovsc_shift;
	}
	else if (strcmp( p, "snd7" ) == 0) {
	    atari_switches |= ATARI_SWITCH_SND7 << ovsc_shift;
	}
    }
}


    /*
     *  Setup the Atari configuration info
     */

void __init config_atari(void)
{
    unsigned short tos_version;

    memset(&atari_hw_present, 0, sizeof(atari_hw_present));

    atari_debug_init();

    ioport_resource.end  = 0xFFFFFFFF;  /* Change size of I/O space from 64KB
                                           to 4GB. */

    mach_sched_init      = atari_sched_init;
    mach_init_IRQ        = atari_init_IRQ;
    mach_get_model	 = atari_get_model;
    mach_get_hardware_list = atari_get_hardware_list;
    mach_gettimeoffset   = atari_gettimeoffset;
    mach_reset           = atari_reset;
    mach_max_dma_address = 0xffffff;
#if defined(CONFIG_INPUT_M68K_BEEP) || defined(CONFIG_INPUT_M68K_BEEP_MODULE)
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

    printk( "Atari hardware found: " );
    if (MACH_IS_MEDUSA || MACH_IS_HADES) {
        /* There's no Atari video hardware on the Medusa, but all the
         * addresses below generate a DTACK so no bus error occurs! */
    }
    else if (hwreg_present( f030_xreg )) {
	ATARIHW_SET(VIDEL_SHIFTER);
        printk( "VIDEL " );
        /* This is a temporary hack: If there is Falcon video
         * hardware, we assume that the ST-DMA serves SCSI instead of
         * ACSI. In the future, there should be a better method for
         * this...
         */
	ATARIHW_SET(ST_SCSI);
        printk( "STDMA-SCSI " );
    }
    else if (hwreg_present( tt_palette )) {
	ATARIHW_SET(TT_SHIFTER);
        printk( "TT_SHIFTER " );
    }
    else if (hwreg_present( &shifter.bas_hi )) {
        if (hwreg_present( &shifter.bas_lo ) &&
	    (shifter.bas_lo = 0x0aau, shifter.bas_lo == 0x0aau)) {
	    ATARIHW_SET(EXTD_SHIFTER);
            printk( "EXTD_SHIFTER " );
        }
        else {
	    ATARIHW_SET(STND_SHIFTER);
            printk( "STND_SHIFTER " );
        }
    }
    if (hwreg_present( &mfp.par_dt_reg )) {
	ATARIHW_SET(ST_MFP);
        printk( "ST_MFP " );
    }
    if (hwreg_present( &tt_mfp.par_dt_reg )) {
	ATARIHW_SET(TT_MFP);
        printk( "TT_MFP " );
    }
    if (hwreg_present( &tt_scsi_dma.dma_addr_hi )) {
	ATARIHW_SET(SCSI_DMA);
        printk( "TT_SCSI_DMA " );
    }
    if (!MACH_IS_HADES && hwreg_present( &st_dma.dma_hi )) {
	ATARIHW_SET(STND_DMA);
        printk( "STND_DMA " );
    }
    if (MACH_IS_MEDUSA || /* The ST-DMA address registers aren't readable
			   * on all Medusas, so the test below may fail */
        (hwreg_present( &st_dma.dma_vhi ) &&
         (st_dma.dma_vhi = 0x55) && (st_dma.dma_hi = 0xaa) &&
         st_dma.dma_vhi == 0x55 && st_dma.dma_hi == 0xaa &&
         (st_dma.dma_vhi = 0xaa) && (st_dma.dma_hi = 0x55) &&
         st_dma.dma_vhi == 0xaa && st_dma.dma_hi == 0x55)) {
	ATARIHW_SET(EXTD_DMA);
        printk( "EXTD_DMA " );
    }
    if (hwreg_present( &tt_scsi.scsi_data )) {
	ATARIHW_SET(TT_SCSI);
        printk( "TT_SCSI " );
    }
    if (hwreg_present( &sound_ym.rd_data_reg_sel )) {
	ATARIHW_SET(YM_2149);
        printk( "YM2149 " );
    }
    if (!MACH_IS_MEDUSA && !MACH_IS_HADES &&
	hwreg_present( &tt_dmasnd.ctrl )) {
	ATARIHW_SET(PCM_8BIT);
        printk( "PCM " );
    }
    if (!MACH_IS_HADES && hwreg_present( &falcon_codec.unused5 )) {
	ATARIHW_SET(CODEC);
        printk( "CODEC " );
    }
    if (hwreg_present( &dsp56k_host_interface.icr )) {
	ATARIHW_SET(DSP56K);
        printk( "DSP56K " );
    }
    if (hwreg_present( &tt_scc_dma.dma_ctrl ) &&
#if 0
	/* This test sucks! Who knows some better? */
	(tt_scc_dma.dma_ctrl = 0x01, (tt_scc_dma.dma_ctrl & 1) == 1) &&
	(tt_scc_dma.dma_ctrl = 0x00, (tt_scc_dma.dma_ctrl & 1) == 0)
#else
	!MACH_IS_MEDUSA && !MACH_IS_HADES
#endif
	) {
	ATARIHW_SET(SCC_DMA);
        printk( "SCC_DMA " );
    }
    if (scc_test( &scc.cha_a_ctrl )) {
	ATARIHW_SET(SCC);
        printk( "SCC " );
    }
    if (scc_test( &st_escc.cha_b_ctrl )) {
	ATARIHW_SET( ST_ESCC );
	printk( "ST_ESCC " );
    }
    if (MACH_IS_HADES)
    {
        ATARIHW_SET( VME );
        printk( "VME " );
    }
    else if (hwreg_present( &tt_scu.sys_mask )) {
	ATARIHW_SET(SCU);
	/* Assume a VME bus if there's a SCU */
	ATARIHW_SET( VME );
        printk( "VME SCU " );
    }
    if (hwreg_present( (void *)(0xffff9210) )) {
	ATARIHW_SET(ANALOG_JOY);
        printk( "ANALOG_JOY " );
    }
    if (!MACH_IS_HADES && hwreg_present( blitter.halftone )) {
	ATARIHW_SET(BLITTER);
        printk( "BLITTER " );
    }
    if (hwreg_present((void *)0xfff00039)) {
	ATARIHW_SET(IDE);
        printk( "IDE " );
    }
#if 1 /* This maybe wrong */
    if (!MACH_IS_MEDUSA && !MACH_IS_HADES &&
	hwreg_present( &tt_microwire.data ) &&
	hwreg_present( &tt_microwire.mask ) &&
	(tt_microwire.mask = 0x7ff,
	 udelay(1),
	 tt_microwire.data = MW_LM1992_PSG_HIGH | MW_LM1992_ADDR,
	 udelay(1),
	 tt_microwire.data != 0)) {
	ATARIHW_SET(MICROWIRE);
	while (tt_microwire.mask != 0x7ff) ;
        printk( "MICROWIRE " );
    }
#endif
    if (hwreg_present( &tt_rtc.regsel )) {
	ATARIHW_SET(TT_CLK);
        printk( "TT_CLK " );
        mach_hwclk = atari_tt_hwclk;
        mach_set_clock_mmss = atari_tt_set_clock_mmss;
    }
    if (!MACH_IS_HADES && hwreg_present( &mste_rtc.sec_ones)) {
	ATARIHW_SET(MSTE_CLK);
        printk( "MSTE_CLK ");
        mach_hwclk = atari_mste_hwclk;
        mach_set_clock_mmss = atari_mste_set_clock_mmss;
    }
    if (!MACH_IS_MEDUSA && !MACH_IS_HADES &&
	hwreg_present( &dma_wd.fdc_speed ) &&
	hwreg_write( &dma_wd.fdc_speed, 0 )) {
	    ATARIHW_SET(FDCSPEED);
	    printk( "FDC_SPEED ");
    }
    if (!MACH_IS_HADES && !ATARIHW_PRESENT(ST_SCSI)) {
	ATARIHW_SET(ACSI);
        printk( "ACSI " );
    }
    printk("\n");

    if (CPU_IS_040_OR_060)
        /* Now it seems to be safe to turn of the tt0 transparent
         * translation (the one that must not be turned off in
         * head.S...)
         */
        __asm__ volatile ("moveq #0,%/d0\n\t"
                          ".chip 68040\n\t"
			  "movec %%d0,%%itt0\n\t"
			  "movec %%d0,%%dtt0\n\t"
			  ".chip 68k"
						  : /* no outputs */
						  : /* no inputs */
						  : "d0");

    /* allocator for memory that must reside in st-ram */
    atari_stram_init ();

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
        unsigned long	tt1_val;
        tt1_val = 0xfe008543;	/* Translate 0xfexxxxxx, enable, cache
                                 * inhibit, read and write, FDC mask = 3,
                                 * FDC val = 4 -> Supervisor only */
        __asm__ __volatile__ ( ".chip 68030\n\t"
				"pmove	%0@,%/tt1\n\t"
				".chip 68k"
				: : "a" (&tt1_val) );
    }
    else {
        __asm__ __volatile__
            ( "movel %0,%/d0\n\t"
	      ".chip 68040\n\t"
	      "movec %%d0,%%itt1\n\t"
	      "movec %%d0,%%dtt1\n\t"
	      ".chip 68k"
              :
              : "g" (0xfe00a040)	/* Translate 0xfexxxxxx, enable,
                                         * supervisor only, non-cacheable/
                                         * serialized, writable */
              : "d0" );

    }

    /* Fetch tos version at Physical 2 */
    /* We my not be able to access this address if the kernel is
       loaded to st ram, since the first page is unmapped.  On the
       Medusa this is always the case and there is nothing we can do
       about this, so we just assume the smaller offset.  For the TT
       we use the fact that in head.S we have set up a mapping
       0xFFxxxxxx -> 0x00xxxxxx, so that the first 16MB is accessible
       in the last 16MB of the address space. */
    tos_version = (MACH_IS_MEDUSA || MACH_IS_HADES) ?
		  0xfff : *(unsigned short *)0xff000002;
    atari_rtc_year_offset = (tos_version < 0x306) ? 70 : 68;
}

#ifdef CONFIG_HEARTBEAT
static void atari_heartbeat( int on )
{
    unsigned char tmp;
    unsigned long flags;

    if (atari_dont_touch_floppy_select)
	return;

    local_irq_save(flags);
    sound_ym.rd_data_reg_sel = 14; /* Select PSG Port A */
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

static void atari_reset (void)
{
    long tc_val = 0;
    long reset_addr;

    /* On the Medusa, phys. 0x4 may contain garbage because it's no
       ROM.  See above for explanation why we cannot use PTOV(4). */
    reset_addr = MACH_IS_HADES ? 0x7fe00030 :
                 MACH_IS_MEDUSA || MACH_IS_AB40 ? 0xe00030 :
		 *(unsigned long *) 0xff000004;

    /* reset ACIA for switch off OverScan, if it's active */
    if (atari_switches & ATARI_SWITCH_OVSC_IKBD)
	acia.key_ctrl = ACIA_RESET;
    if (atari_switches & ATARI_SWITCH_OVSC_MIDI)
	acia.mid_ctrl = ACIA_RESET;

    /* processor independent: turn off interrupts and reset the VBR;
     * the caches must be left enabled, else prefetching the final jump
     * instruction doesn't work. */
    local_irq_disable();
    __asm__ __volatile__
	("moveq	#0,%/d0\n\t"
	 "movec	%/d0,%/vbr"
	 : : : "d0" );

    if (CPU_IS_040_OR_060) {
        unsigned long jmp_addr040 = virt_to_phys(&&jmp_addr_label040);
	if (CPU_IS_060) {
	    /* 68060: clear PCR to turn off superscalar operation */
	    __asm__ __volatile__
		("moveq	#0,%/d0\n\t"
		 ".chip 68060\n\t"
		 "movec %%d0,%%pcr\n\t"
		 ".chip 68k"
		 : : : "d0" );
	}

        __asm__ __volatile__
            ("movel    %0,%/d0\n\t"
             "andl     #0xff000000,%/d0\n\t"
             "orw      #0xe020,%/d0\n\t"   /* map 16 MB, enable, cacheable */
             ".chip 68040\n\t"
	     "movec    %%d0,%%itt0\n\t"
             "movec    %%d0,%%dtt0\n\t"
	     ".chip 68k\n\t"
             "jmp   %0@\n\t"
             : /* no outputs */
             : "a" (jmp_addr040)
             : "d0" );
      jmp_addr_label040:
        __asm__ __volatile__
          ("moveq #0,%/d0\n\t"
	   "nop\n\t"
	   ".chip 68040\n\t"
	   "cinva %%bc\n\t"
	   "nop\n\t"
	   "pflusha\n\t"
	   "nop\n\t"
	   "movec %%d0,%%tc\n\t"
	   "nop\n\t"
	   /* the following setup of transparent translations is needed on the
	    * Afterburner040 to successfully reboot. Other machines shouldn't
	    * care about a different tt regs setup, they also didn't care in
	    * the past that the regs weren't turned off. */
	   "movel #0xffc000,%%d0\n\t" /* whole insn space cacheable */
	   "movec %%d0,%%itt0\n\t"
	   "movec %%d0,%%itt1\n\t"
	   "orw   #0x40,%/d0\n\t" /* whole data space non-cacheable/ser. */
	   "movec %%d0,%%dtt0\n\t"
	   "movec %%d0,%%dtt1\n\t"
	   ".chip 68k\n\t"
           "jmp %0@"
           : /* no outputs */
           : "a" (reset_addr)
           : "d0");
    }
    else
        __asm__ __volatile__
            ("pmove %0@,%/tc\n\t"
             "jmp %1@"
             : /* no outputs */
             : "a" (&tc_val), "a" (reset_addr));
}


static void atari_get_model(char *model)
{
    strcpy(model, "Atari ");
    switch (atari_mch_cookie >> 16) {
	case ATARI_MCH_ST:
	    if (ATARIHW_PRESENT(MSTE_CLK))
		strcat (model, "Mega ST");
	    else
		strcat (model, "ST");
	    break;
	case ATARI_MCH_STE:
	    if (MACH_IS_MSTE)
		strcat (model, "Mega STE");
	    else
		strcat (model, "STE");
	    break;
	case ATARI_MCH_TT:
	    if (MACH_IS_MEDUSA)
		/* Medusa has TT _MCH cookie */
		strcat (model, "Medusa");
	    else if (MACH_IS_HADES)
		strcat(model, "Hades");
	    else
		strcat (model, "TT");
	    break;
	case ATARI_MCH_FALCON:
	    strcat (model, "Falcon");
	    if (MACH_IS_AB40)
		strcat (model, " (with Afterburner040)");
	    break;
	default:
	    sprintf (model + strlen (model), "(unknown mach cookie 0x%lx)",
		     atari_mch_cookie);
	    break;
    }
}


static int atari_get_hardware_list(char *buffer)
{
    int len = 0, i;

    for (i = 0; i < m68k_num_memory; i++)
	len += sprintf (buffer+len, "\t%3ld MB at 0x%08lx (%s)\n",
			m68k_memory[i].size >> 20, m68k_memory[i].addr,
			(m68k_memory[i].addr & 0xff000000 ?
			 "alternate RAM" : "ST-RAM"));

#define ATARIHW_ANNOUNCE(name,str)				\
    if (ATARIHW_PRESENT(name))			\
	len += sprintf (buffer + len, "\t%s\n", str)

    len += sprintf (buffer + len, "Detected hardware:\n");
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

    return(len);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  tab-width: 8
 * End:
 */
