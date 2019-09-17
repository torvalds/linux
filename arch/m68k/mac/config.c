/*
 *  linux/arch/m68k/mac/config.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/*
 * Miscellaneous linux stuff
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/interrupt.h>
/* keyb */
#include <linux/random.h>
#include <linux/delay.h>
/* keyb */
#include <linux/init.h>
#include <linux/vt_kern.h>
#include <linux/platform_device.h>
#include <linux/adb.h>
#include <linux/cuda.h>
#include <linux/pmu.h>
#include <linux/rtc.h>

#include <asm/setup.h>
#include <asm/bootinfo.h>
#include <asm/bootinfo-mac.h>
#include <asm/byteorder.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/machdep.h>

#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/machw.h>

#include <asm/mac_iop.h>
#include <asm/mac_via.h>
#include <asm/mac_oss.h>
#include <asm/mac_psc.h>

/* Mac bootinfo struct */
struct mac_booter_data mac_bi_data;

/* The phys. video addr. - might be bogus on some machines */
static unsigned long mac_orig_videoaddr;

extern int mac_hwclk(int, struct rtc_time *);
extern void iop_preinit(void);
extern void iop_init(void);
extern void via_init(void);
extern void via_init_clock(irq_handler_t func);
extern void via_flush_cache(void);
extern void oss_init(void);
extern void psc_init(void);
extern void baboon_init(void);

extern void mac_mksound(unsigned int, unsigned int);

static void mac_get_model(char *str);
static void mac_identify(void);
static void mac_report_hardware(void);

static void __init mac_sched_init(irq_handler_t vector)
{
	via_init_clock(vector);
}

/*
 * Parse a Macintosh-specific record in the bootinfo
 */

int __init mac_parse_bootinfo(const struct bi_record *record)
{
	int unknown = 0;
	const void *data = record->data;

	switch (be16_to_cpu(record->tag)) {
	case BI_MAC_MODEL:
		mac_bi_data.id = be32_to_cpup(data);
		break;
	case BI_MAC_VADDR:
		mac_bi_data.videoaddr = be32_to_cpup(data);
		break;
	case BI_MAC_VDEPTH:
		mac_bi_data.videodepth = be32_to_cpup(data);
		break;
	case BI_MAC_VROW:
		mac_bi_data.videorow = be32_to_cpup(data);
		break;
	case BI_MAC_VDIM:
		mac_bi_data.dimensions = be32_to_cpup(data);
		break;
	case BI_MAC_VLOGICAL:
		mac_orig_videoaddr = be32_to_cpup(data);
		mac_bi_data.videological =
			VIDEOMEMBASE + (mac_orig_videoaddr & ~VIDEOMEMMASK);
		break;
	case BI_MAC_SCCBASE:
		mac_bi_data.sccbase = be32_to_cpup(data);
		break;
	case BI_MAC_BTIME:
		mac_bi_data.boottime = be32_to_cpup(data);
		break;
	case BI_MAC_GMTBIAS:
		mac_bi_data.gmtbias = be32_to_cpup(data);
		break;
	case BI_MAC_MEMSIZE:
		mac_bi_data.memsize = be32_to_cpup(data);
		break;
	case BI_MAC_CPUID:
		mac_bi_data.cpuid = be32_to_cpup(data);
		break;
	case BI_MAC_ROMBASE:
		mac_bi_data.rombase = be32_to_cpup(data);
		break;
	default:
		unknown = 1;
		break;
	}
	return unknown;
}

/*
 * Flip into 24bit mode for an instant - flushes the L2 cache card. We
 * have to disable interrupts for this. Our IRQ handlers will crap
 * themselves if they take an IRQ in 24bit mode!
 */

static void mac_cache_card_flush(int writeback)
{
	unsigned long flags;

	local_irq_save(flags);
	via_flush_cache();
	local_irq_restore(flags);
}

void __init config_mac(void)
{
	if (!MACH_IS_MAC)
		pr_err("ERROR: no Mac, but config_mac() called!!\n");

	mach_sched_init = mac_sched_init;
	mach_init_IRQ = mac_init_IRQ;
	mach_get_model = mac_get_model;
	mach_hwclk = mac_hwclk;
	mach_reset = mac_reset;
	mach_halt = mac_poweroff;
	mach_power_off = mac_poweroff;
	mach_max_dma_address = 0xffffffff;
#if IS_ENABLED(CONFIG_INPUT_M68K_BEEP)
	mach_beep = mac_mksound;
#endif

	/*
	 * Determine hardware present
	 */

	mac_identify();
	mac_report_hardware();

	/*
	 * AFAIK only the IIci takes a cache card.  The IIfx has onboard
	 * cache ... someone needs to figure out how to tell if it's on or
	 * not.
	 */

	if (macintosh_config->ident == MAC_MODEL_IICI
	    || macintosh_config->ident == MAC_MODEL_IIFX)
		mach_l2_flush = mac_cache_card_flush;
}


/*
 * Macintosh Table: hardcoded model configuration data.
 *
 * Much of this was defined by Alan, based on who knows what docs.
 * I've added a lot more, and some of that was pure guesswork based
 * on hardware pages present on the Mac web site. Possibly wildly
 * inaccurate, so look here if a new Mac model won't run. Example: if
 * a Mac crashes immediately after the VIA1 registers have been dumped
 * to the screen, it probably died attempting to read DirB on a RBV.
 * Meaning it should have MAC_VIA_IICI here :-)
 */

struct mac_model *macintosh_config;
EXPORT_SYMBOL(macintosh_config);

static struct mac_model mac_data_table[] = {
	/*
	 * We'll pretend to be a Macintosh II, that's pretty safe.
	 */

	{
		.ident		= MAC_MODEL_II,
		.name		= "Unknown",
		.adb_type	= MAC_ADB_II,
		.via_type	= MAC_VIA_II,
		.scsi_type	= MAC_SCSI_OLD,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_NUBUS,
		.floppy_type	= MAC_FLOPPY_UNSUPPORTED, /* IWM */
	},

	/*
	 * Original Mac II hardware
	 */

	{
		.ident		= MAC_MODEL_II,
		.name		= "II",
		.adb_type	= MAC_ADB_II,
		.via_type	= MAC_VIA_II,
		.scsi_type	= MAC_SCSI_OLD,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_NUBUS,
		.floppy_type	= MAC_FLOPPY_UNSUPPORTED, /* IWM */
	}, {
		.ident		= MAC_MODEL_IIX,
		.name		= "IIx",
		.adb_type	= MAC_ADB_II,
		.via_type	= MAC_VIA_II,
		.scsi_type	= MAC_SCSI_OLD,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_NUBUS,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM */
	}, {
		.ident		= MAC_MODEL_IICX,
		.name		= "IIcx",
		.adb_type	= MAC_ADB_II,
		.via_type	= MAC_VIA_II,
		.scsi_type	= MAC_SCSI_OLD,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_NUBUS,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM */
	}, {
		.ident		= MAC_MODEL_SE30,
		.name		= "SE/30",
		.adb_type	= MAC_ADB_II,
		.via_type	= MAC_VIA_II,
		.scsi_type	= MAC_SCSI_OLD,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_PDS,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM */
	},

	/*
	 * Weirdified Mac II hardware - all subtly different. Gee thanks
	 * Apple. All these boxes seem to have VIA2 in a different place to
	 * the Mac II (+1A000 rather than +4000)
	 * CSA: see http://developer.apple.com/technotes/hw/hw_09.html
	 */

	{
		.ident		= MAC_MODEL_IICI,
		.name		= "IIci",
		.adb_type	= MAC_ADB_II,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_OLD,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_NUBUS,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM */
	}, {
		.ident		= MAC_MODEL_IIFX,
		.name		= "IIfx",
		.adb_type	= MAC_ADB_IOP,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_IIFX,
		.scc_type	= MAC_SCC_IOP,
		.expansion_type	= MAC_EXP_PDS_NUBUS,
		.floppy_type	= MAC_FLOPPY_SWIM_IOP, /* SWIM */
	}, {
		.ident		= MAC_MODEL_IISI,
		.name		= "IIsi",
		.adb_type	= MAC_ADB_EGRET,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_OLD,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_PDS_NUBUS,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM */
	}, {
		.ident		= MAC_MODEL_IIVI,
		.name		= "IIvi",
		.adb_type	= MAC_ADB_EGRET,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_LC,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_NUBUS,
		.floppy_type	= MAC_FLOPPY_LC, /* SWIM */
	}, {
		.ident		= MAC_MODEL_IIVX,
		.name		= "IIvx",
		.adb_type	= MAC_ADB_EGRET,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_LC,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_NUBUS,
		.floppy_type	= MAC_FLOPPY_LC, /* SWIM */
	},

	/*
	 * Classic models (guessing: similar to SE/30? Nope, similar to LC...)
	 */

	{
		.ident		= MAC_MODEL_CLII,
		.name		= "Classic II",
		.adb_type	= MAC_ADB_EGRET,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_LC,
		.scc_type	= MAC_SCC_II,
		.floppy_type	= MAC_FLOPPY_LC, /* SWIM */
	}, {
		.ident		= MAC_MODEL_CCL,
		.name		= "Color Classic",
		.adb_type	= MAC_ADB_CUDA,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_LC,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_PDS,
		.floppy_type	= MAC_FLOPPY_LC, /* SWIM 2 */
	}, {
		.ident		= MAC_MODEL_CCLII,
		.name		= "Color Classic II",
		.adb_type	= MAC_ADB_CUDA,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_LC,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_PDS,
		.floppy_type	= MAC_FLOPPY_LC, /* SWIM 2 */
	},

	/*
	 * Some Mac LC machines. Basically the same as the IIci, ADB like IIsi
	 */

	{
		.ident		= MAC_MODEL_LC,
		.name		= "LC",
		.adb_type	= MAC_ADB_EGRET,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_LC,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_PDS,
		.floppy_type	= MAC_FLOPPY_LC, /* SWIM */
	}, {
		.ident		= MAC_MODEL_LCII,
		.name		= "LC II",
		.adb_type	= MAC_ADB_EGRET,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_LC,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_PDS,
		.floppy_type	= MAC_FLOPPY_LC, /* SWIM */
	}, {
		.ident		= MAC_MODEL_LCIII,
		.name		= "LC III",
		.adb_type	= MAC_ADB_EGRET,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_LC,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_PDS,
		.floppy_type	= MAC_FLOPPY_LC, /* SWIM 2 */
	},

	/*
	 * Quadra. Video is at 0xF9000000, via is like a MacII. We label it
	 * differently as some of the stuff connected to VIA2 seems different.
	 * Better SCSI chip and onboard ethernet using a NatSemi SONIC except
	 * the 660AV and 840AV which use an AMD 79C940 (MACE).
	 * The 700, 900 and 950 have some I/O chips in the wrong place to
	 * confuse us. The 840AV has a SCSI location of its own (same as
	 * the 660AV).
	 */

	{
		.ident		= MAC_MODEL_Q605,
		.name		= "Quadra 605",
		.adb_type	= MAC_ADB_CUDA,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_QUADRA,
		.scc_type	= MAC_SCC_QUADRA,
		.expansion_type	= MAC_EXP_PDS,
		.floppy_type	= MAC_FLOPPY_QUADRA, /* SWIM 2 */
	}, {
		.ident		= MAC_MODEL_Q605_ACC,
		.name		= "Quadra 605",
		.adb_type	= MAC_ADB_CUDA,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_QUADRA,
		.scc_type	= MAC_SCC_QUADRA,
		.expansion_type	= MAC_EXP_PDS,
		.floppy_type	= MAC_FLOPPY_QUADRA, /* SWIM 2 */
	}, {
		.ident		= MAC_MODEL_Q610,
		.name		= "Quadra 610",
		.adb_type	= MAC_ADB_II,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_QUADRA,
		.scc_type	= MAC_SCC_QUADRA,
		.ether_type	= MAC_ETHER_SONIC,
		.expansion_type	= MAC_EXP_PDS_NUBUS,
		.floppy_type	= MAC_FLOPPY_QUADRA, /* SWIM 2 */
	}, {
		.ident		= MAC_MODEL_Q630,
		.name		= "Quadra 630",
		.adb_type	= MAC_ADB_CUDA,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_QUADRA,
		.ide_type	= MAC_IDE_QUADRA,
		.scc_type	= MAC_SCC_QUADRA,
		.expansion_type	= MAC_EXP_PDS_COMM,
		.floppy_type	= MAC_FLOPPY_QUADRA, /* SWIM 2 */
	}, {
		.ident		= MAC_MODEL_Q650,
		.name		= "Quadra 650",
		.adb_type	= MAC_ADB_II,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_QUADRA,
		.scc_type	= MAC_SCC_QUADRA,
		.ether_type	= MAC_ETHER_SONIC,
		.expansion_type	= MAC_EXP_PDS_NUBUS,
		.floppy_type	= MAC_FLOPPY_QUADRA, /* SWIM 2 */
	},
	/* The Q700 does have a NS Sonic */
	{
		.ident		= MAC_MODEL_Q700,
		.name		= "Quadra 700",
		.adb_type	= MAC_ADB_II,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_QUADRA2,
		.scc_type	= MAC_SCC_QUADRA,
		.ether_type	= MAC_ETHER_SONIC,
		.expansion_type	= MAC_EXP_PDS_NUBUS,
		.floppy_type	= MAC_FLOPPY_QUADRA, /* SWIM */
	}, {
		.ident		= MAC_MODEL_Q800,
		.name		= "Quadra 800",
		.adb_type	= MAC_ADB_II,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_QUADRA,
		.scc_type	= MAC_SCC_QUADRA,
		.ether_type	= MAC_ETHER_SONIC,
		.expansion_type	= MAC_EXP_PDS_NUBUS,
		.floppy_type	= MAC_FLOPPY_QUADRA, /* SWIM 2 */
	}, {
		.ident		= MAC_MODEL_Q840,
		.name		= "Quadra 840AV",
		.adb_type	= MAC_ADB_CUDA,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_QUADRA3,
		.scc_type	= MAC_SCC_PSC,
		.ether_type	= MAC_ETHER_MACE,
		.expansion_type	= MAC_EXP_NUBUS,
		.floppy_type	= MAC_FLOPPY_UNSUPPORTED, /* New Age */
	}, {
		.ident		= MAC_MODEL_Q900,
		.name		= "Quadra 900",
		.adb_type	= MAC_ADB_IOP,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_QUADRA2,
		.scc_type	= MAC_SCC_IOP,
		.ether_type	= MAC_ETHER_SONIC,
		.expansion_type	= MAC_EXP_PDS_NUBUS,
		.floppy_type	= MAC_FLOPPY_SWIM_IOP, /* SWIM */
	}, {
		.ident		= MAC_MODEL_Q950,
		.name		= "Quadra 950",
		.adb_type	= MAC_ADB_IOP,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_QUADRA2,
		.scc_type	= MAC_SCC_IOP,
		.ether_type	= MAC_ETHER_SONIC,
		.expansion_type	= MAC_EXP_PDS_NUBUS,
		.floppy_type	= MAC_FLOPPY_SWIM_IOP, /* SWIM */
	},

	/*
	 * Performa - more LC type machines
	 */

	{
		.ident		= MAC_MODEL_P460,
		.name		= "Performa 460",
		.adb_type	= MAC_ADB_EGRET,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_LC,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_PDS,
		.floppy_type	= MAC_FLOPPY_LC, /* SWIM 2 */
	}, {
		.ident		= MAC_MODEL_P475,
		.name		= "Performa 475",
		.adb_type	= MAC_ADB_CUDA,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_QUADRA,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_PDS,
		.floppy_type	= MAC_FLOPPY_QUADRA, /* SWIM 2 */
	}, {
		.ident		= MAC_MODEL_P475F,
		.name		= "Performa 475",
		.adb_type	= MAC_ADB_CUDA,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_QUADRA,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_PDS,
		.floppy_type	= MAC_FLOPPY_QUADRA, /* SWIM 2 */
	}, {
		.ident		= MAC_MODEL_P520,
		.name		= "Performa 520",
		.adb_type	= MAC_ADB_CUDA,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_LC,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_PDS,
		.floppy_type	= MAC_FLOPPY_LC, /* SWIM 2 */
	}, {
		.ident		= MAC_MODEL_P550,
		.name		= "Performa 550",
		.adb_type	= MAC_ADB_CUDA,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_LC,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_PDS,
		.floppy_type	= MAC_FLOPPY_LC, /* SWIM 2 */
	},
	/* These have the comm slot, and therefore possibly SONIC ethernet */
	{
		.ident		= MAC_MODEL_P575,
		.name		= "Performa 575",
		.adb_type	= MAC_ADB_CUDA,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_QUADRA,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_PDS_COMM,
		.floppy_type	= MAC_FLOPPY_QUADRA, /* SWIM 2 */
	}, {
		.ident		= MAC_MODEL_P588,
		.name		= "Performa 588",
		.adb_type	= MAC_ADB_CUDA,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_QUADRA,
		.ide_type	= MAC_IDE_QUADRA,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_PDS_COMM,
		.floppy_type	= MAC_FLOPPY_QUADRA, /* SWIM 2 */
	}, {
		.ident		= MAC_MODEL_TV,
		.name		= "TV",
		.adb_type	= MAC_ADB_CUDA,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_LC,
		.scc_type	= MAC_SCC_II,
		.floppy_type	= MAC_FLOPPY_LC, /* SWIM 2 */
	}, {
		.ident		= MAC_MODEL_P600,
		.name		= "Performa 600",
		.adb_type	= MAC_ADB_EGRET,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_LC,
		.scc_type	= MAC_SCC_II,
		.expansion_type	= MAC_EXP_NUBUS,
		.floppy_type	= MAC_FLOPPY_LC, /* SWIM */
	},

	/*
	 * Centris - just guessing again; maybe like Quadra.
	 * The C610 may or may not have SONIC. We probe to make sure.
	 */

	{
		.ident		= MAC_MODEL_C610,
		.name		= "Centris 610",
		.adb_type	= MAC_ADB_II,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_QUADRA,
		.scc_type	= MAC_SCC_QUADRA,
		.ether_type	= MAC_ETHER_SONIC,
		.expansion_type	= MAC_EXP_PDS_NUBUS,
		.floppy_type	= MAC_FLOPPY_QUADRA, /* SWIM 2 */
	}, {
		.ident		= MAC_MODEL_C650,
		.name		= "Centris 650",
		.adb_type	= MAC_ADB_II,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_QUADRA,
		.scc_type	= MAC_SCC_QUADRA,
		.ether_type	= MAC_ETHER_SONIC,
		.expansion_type	= MAC_EXP_PDS_NUBUS,
		.floppy_type	= MAC_FLOPPY_QUADRA, /* SWIM 2 */
	}, {
		.ident		= MAC_MODEL_C660,
		.name		= "Centris 660AV",
		.adb_type	= MAC_ADB_CUDA,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_QUADRA3,
		.scc_type	= MAC_SCC_PSC,
		.ether_type	= MAC_ETHER_MACE,
		.expansion_type	= MAC_EXP_PDS_NUBUS,
		.floppy_type	= MAC_FLOPPY_UNSUPPORTED, /* New Age */
	},

	/*
	 * The PowerBooks all the same "Combo" custom IC for SCSI and SCC
	 * and a PMU (in two variations?) for ADB. Most of them use the
	 * Quadra-style VIAs. A few models also have IDE from hell.
	 */

	{
		.ident		= MAC_MODEL_PB140,
		.name		= "PowerBook 140",
		.adb_type	= MAC_ADB_PB1,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_OLD,
		.scc_type	= MAC_SCC_QUADRA,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM */
	}, {
		.ident		= MAC_MODEL_PB145,
		.name		= "PowerBook 145",
		.adb_type	= MAC_ADB_PB1,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_OLD,
		.scc_type	= MAC_SCC_QUADRA,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM */
	}, {
		.ident		= MAC_MODEL_PB150,
		.name		= "PowerBook 150",
		.adb_type	= MAC_ADB_PB2,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_OLD,
		.ide_type	= MAC_IDE_PB,
		.scc_type	= MAC_SCC_QUADRA,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM */
	}, {
		.ident		= MAC_MODEL_PB160,
		.name		= "PowerBook 160",
		.adb_type	= MAC_ADB_PB1,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_OLD,
		.scc_type	= MAC_SCC_QUADRA,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM */
	}, {
		.ident		= MAC_MODEL_PB165,
		.name		= "PowerBook 165",
		.adb_type	= MAC_ADB_PB1,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_OLD,
		.scc_type	= MAC_SCC_QUADRA,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM */
	}, {
		.ident		= MAC_MODEL_PB165C,
		.name		= "PowerBook 165c",
		.adb_type	= MAC_ADB_PB1,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_OLD,
		.scc_type	= MAC_SCC_QUADRA,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM */
	}, {
		.ident		= MAC_MODEL_PB170,
		.name		= "PowerBook 170",
		.adb_type	= MAC_ADB_PB1,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_OLD,
		.scc_type	= MAC_SCC_QUADRA,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM */
	}, {
		.ident		= MAC_MODEL_PB180,
		.name		= "PowerBook 180",
		.adb_type	= MAC_ADB_PB1,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_OLD,
		.scc_type	= MAC_SCC_QUADRA,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM */
	}, {
		.ident		= MAC_MODEL_PB180C,
		.name		= "PowerBook 180c",
		.adb_type	= MAC_ADB_PB1,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_OLD,
		.scc_type	= MAC_SCC_QUADRA,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM */
	}, {
		.ident		= MAC_MODEL_PB190,
		.name		= "PowerBook 190",
		.adb_type	= MAC_ADB_PB2,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_OLD,
		.ide_type	= MAC_IDE_BABOON,
		.scc_type	= MAC_SCC_QUADRA,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM 2 */
	}, {
		.ident		= MAC_MODEL_PB520,
		.name		= "PowerBook 520",
		.adb_type	= MAC_ADB_PB2,
		.via_type	= MAC_VIA_QUADRA,
		.scsi_type	= MAC_SCSI_OLD,
		.scc_type	= MAC_SCC_QUADRA,
		.ether_type	= MAC_ETHER_SONIC,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM 2 */
	},

	/*
	 * PowerBook Duos are pretty much like normal PowerBooks
	 * All of these probably have onboard SONIC in the Dock which
	 * means we'll have to probe for it eventually.
	 */

	{
		.ident		= MAC_MODEL_PB210,
		.name		= "PowerBook Duo 210",
		.adb_type	= MAC_ADB_PB2,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_DUO,
		.scc_type	= MAC_SCC_QUADRA,
		.expansion_type	= MAC_EXP_NUBUS,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM */
	}, {
		.ident		= MAC_MODEL_PB230,
		.name		= "PowerBook Duo 230",
		.adb_type	= MAC_ADB_PB2,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_DUO,
		.scc_type	= MAC_SCC_QUADRA,
		.expansion_type	= MAC_EXP_NUBUS,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM */
	}, {
		.ident		= MAC_MODEL_PB250,
		.name		= "PowerBook Duo 250",
		.adb_type	= MAC_ADB_PB2,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_DUO,
		.scc_type	= MAC_SCC_QUADRA,
		.expansion_type	= MAC_EXP_NUBUS,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM */
	}, {
		.ident		= MAC_MODEL_PB270C,
		.name		= "PowerBook Duo 270c",
		.adb_type	= MAC_ADB_PB2,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_DUO,
		.scc_type	= MAC_SCC_QUADRA,
		.expansion_type	= MAC_EXP_NUBUS,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM */
	}, {
		.ident		= MAC_MODEL_PB280,
		.name		= "PowerBook Duo 280",
		.adb_type	= MAC_ADB_PB2,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_DUO,
		.scc_type	= MAC_SCC_QUADRA,
		.expansion_type	= MAC_EXP_NUBUS,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM */
	}, {
		.ident		= MAC_MODEL_PB280C,
		.name		= "PowerBook Duo 280c",
		.adb_type	= MAC_ADB_PB2,
		.via_type	= MAC_VIA_IICI,
		.scsi_type	= MAC_SCSI_DUO,
		.scc_type	= MAC_SCC_QUADRA,
		.expansion_type	= MAC_EXP_NUBUS,
		.floppy_type	= MAC_FLOPPY_OLD, /* SWIM */
	},

	/*
	 * Other stuff?
	 */

	{
		.ident		= -1
	}
};

static struct resource scc_a_rsrcs[] = {
	{ .flags = IORESOURCE_MEM },
	{ .flags = IORESOURCE_IRQ },
};

static struct resource scc_b_rsrcs[] = {
	{ .flags = IORESOURCE_MEM },
	{ .flags = IORESOURCE_IRQ },
};

struct platform_device scc_a_pdev = {
	.name           = "scc",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(scc_a_rsrcs),
	.resource       = scc_a_rsrcs,
};
EXPORT_SYMBOL(scc_a_pdev);

struct platform_device scc_b_pdev = {
	.name           = "scc",
	.id             = 1,
	.num_resources  = ARRAY_SIZE(scc_b_rsrcs),
	.resource       = scc_b_rsrcs,
};
EXPORT_SYMBOL(scc_b_pdev);

static void __init mac_identify(void)
{
	struct mac_model *m;

	/* Penguin data useful? */
	int model = mac_bi_data.id;
	if (!model) {
		/* no bootinfo model id -> NetBSD booter was used! */
		/* XXX FIXME: breaks for model > 31 */
		model = (mac_bi_data.cpuid >> 2) & 63;
		pr_warn("No bootinfo model ID, using cpuid instead (obsolete bootloader?)\n");
	}

	macintosh_config = mac_data_table;
	for (m = macintosh_config; m->ident != -1; m++) {
		if (m->ident == model) {
			macintosh_config = m;
			break;
		}
	}

	/* Set up serial port resources for the console initcall. */

	scc_a_rsrcs[0].start = (resource_size_t) mac_bi_data.sccbase + 2;
	scc_a_rsrcs[0].end   = scc_a_rsrcs[0].start;
	scc_b_rsrcs[0].start = (resource_size_t) mac_bi_data.sccbase;
	scc_b_rsrcs[0].end   = scc_b_rsrcs[0].start;

	switch (macintosh_config->scc_type) {
	case MAC_SCC_PSC:
		scc_a_rsrcs[1].start = scc_a_rsrcs[1].end = IRQ_MAC_SCC_A;
		scc_b_rsrcs[1].start = scc_b_rsrcs[1].end = IRQ_MAC_SCC_B;
		break;
	default:
		/* On non-PSC machines, the serial ports share an IRQ. */
		if (macintosh_config->ident == MAC_MODEL_IIFX) {
			scc_a_rsrcs[1].start = scc_a_rsrcs[1].end = IRQ_MAC_SCC;
			scc_b_rsrcs[1].start = scc_b_rsrcs[1].end = IRQ_MAC_SCC;
		} else {
			scc_a_rsrcs[1].start = scc_a_rsrcs[1].end = IRQ_AUTO_4;
			scc_b_rsrcs[1].start = scc_b_rsrcs[1].end = IRQ_AUTO_4;
		}
		break;
	}

	/*
	 * We need to pre-init the IOPs, if any. Otherwise
	 * the serial console won't work if the user had
	 * the serial ports set to "Faster" mode in MacOS.
	 */
	iop_preinit();

	pr_info("Detected Macintosh model: %d\n", model);

	/*
	 * Report booter data:
	 */
	printk(KERN_DEBUG " Penguin bootinfo data:\n");
	printk(KERN_DEBUG " Video: addr 0x%lx row 0x%lx depth %lx dimensions %ld x %ld\n",
		mac_bi_data.videoaddr, mac_bi_data.videorow,
		mac_bi_data.videodepth, mac_bi_data.dimensions & 0xFFFF,
		mac_bi_data.dimensions >> 16);
	printk(KERN_DEBUG " Videological 0x%lx phys. 0x%lx, SCC at 0x%lx\n",
		mac_bi_data.videological, mac_orig_videoaddr,
		mac_bi_data.sccbase);
	printk(KERN_DEBUG " Boottime: 0x%lx GMTBias: 0x%lx\n",
		mac_bi_data.boottime, mac_bi_data.gmtbias);
	printk(KERN_DEBUG " Machine ID: %ld CPUid: 0x%lx memory size: 0x%lx\n",
		mac_bi_data.id, mac_bi_data.cpuid, mac_bi_data.memsize);

	iop_init();
	oss_init();
	via_init();
	psc_init();
	baboon_init();

#ifdef CONFIG_ADB_CUDA
	find_via_cuda();
#endif
#ifdef CONFIG_ADB_PMU
	find_via_pmu();
#endif
}

static void __init mac_report_hardware(void)
{
	pr_info("Apple Macintosh %s\n", macintosh_config->name);
}

static void mac_get_model(char *str)
{
	strcpy(str, "Macintosh ");
	strcat(str, macintosh_config->name);
}

static const struct resource mac_scsi_iifx_rsrc[] __initconst = {
	{
		.flags = IORESOURCE_IRQ,
		.start = IRQ_MAC_SCSI,
		.end   = IRQ_MAC_SCSI,
	}, {
		.flags = IORESOURCE_MEM,
		.start = 0x50008000,
		.end   = 0x50009FFF,
	}, {
		.flags = IORESOURCE_MEM,
		.start = 0x50008000,
		.end   = 0x50009FFF,
	},
};

static const struct resource mac_scsi_duo_rsrc[] __initconst = {
	{
		.flags = IORESOURCE_MEM,
		.start = 0xFEE02000,
		.end   = 0xFEE03FFF,
	},
};

static const struct resource mac_scsi_old_rsrc[] __initconst = {
	{
		.flags = IORESOURCE_IRQ,
		.start = IRQ_MAC_SCSI,
		.end   = IRQ_MAC_SCSI,
	}, {
		.flags = IORESOURCE_MEM,
		.start = 0x50010000,
		.end   = 0x50011FFF,
	}, {
		.flags = IORESOURCE_MEM,
		.start = 0x50006000,
		.end   = 0x50007FFF,
	},
};

static const struct resource mac_scsi_ccl_rsrc[] __initconst = {
	{
		.flags = IORESOURCE_IRQ,
		.start = IRQ_MAC_SCSI,
		.end   = IRQ_MAC_SCSI,
	}, {
		.flags = IORESOURCE_MEM,
		.start = 0x50F10000,
		.end   = 0x50F11FFF,
	}, {
		.flags = IORESOURCE_MEM,
		.start = 0x50F06000,
		.end   = 0x50F07FFF,
	},
};

int __init mac_platform_init(void)
{
	phys_addr_t swim_base = 0;

	if (!MACH_IS_MAC)
		return -ENODEV;

	/*
	 * Serial devices
	 */

	platform_device_register(&scc_a_pdev);
	platform_device_register(&scc_b_pdev);

	/*
	 * Floppy device
	 */

	switch (macintosh_config->floppy_type) {
	case MAC_FLOPPY_QUADRA:
		swim_base = 0x5001E000;
		break;
	case MAC_FLOPPY_OLD:
		swim_base = 0x50016000;
		break;
	case MAC_FLOPPY_LC:
		swim_base = 0x50F16000;
		break;
	}

	if (swim_base) {
		struct resource swim_rsrc = {
			.flags = IORESOURCE_MEM,
			.start = swim_base,
			.end   = swim_base + 0x1FFF,
		};

		platform_device_register_simple("swim", -1, &swim_rsrc, 1);
	}

	/*
	 * SCSI device(s)
	 */

	switch (macintosh_config->scsi_type) {
	case MAC_SCSI_QUADRA:
	case MAC_SCSI_QUADRA3:
		platform_device_register_simple("mac_esp", 0, NULL, 0);
		break;
	case MAC_SCSI_QUADRA2:
		platform_device_register_simple("mac_esp", 0, NULL, 0);
		if ((macintosh_config->ident == MAC_MODEL_Q900) ||
		    (macintosh_config->ident == MAC_MODEL_Q950))
			platform_device_register_simple("mac_esp", 1, NULL, 0);
		break;
	case MAC_SCSI_IIFX:
		/* Addresses from The Guide to Mac Family Hardware.
		 * $5000 8000 - $5000 9FFF: SCSI DMA
		 * $5000 A000 - $5000 BFFF: Alternate SCSI
		 * $5000 C000 - $5000 DFFF: Alternate SCSI (DMA)
		 * $5000 E000 - $5000 FFFF: Alternate SCSI (Hsk)
		 * The A/UX header file sys/uconfig.h says $50F0 8000.
		 * The "SCSI DMA" custom IC embeds the 53C80 core and
		 * supports Programmed IO, DMA and PDMA (hardware handshake).
		 */
		platform_device_register_simple("mac_scsi", 0,
			mac_scsi_iifx_rsrc, ARRAY_SIZE(mac_scsi_iifx_rsrc));
		break;
	case MAC_SCSI_DUO:
		/* Addresses from the Duo Dock II Developer Note.
		 * $FEE0 2000 - $FEE0 3FFF: normal mode
		 * $FEE0 4000 - $FEE0 5FFF: pseudo DMA without /DRQ
		 * $FEE0 6000 - $FEE0 7FFF: pseudo DMA with /DRQ
		 * The NetBSD code indicates that both 5380 chips share
		 * an IRQ (?) which would need careful handling (see mac_esp).
		 */
		platform_device_register_simple("mac_scsi", 1,
			mac_scsi_duo_rsrc, ARRAY_SIZE(mac_scsi_duo_rsrc));
		/* fall through */
	case MAC_SCSI_OLD:
		/* Addresses from Developer Notes for Duo System,
		 * PowerBook 180 & 160, 140 & 170, Macintosh IIsi
		 * and also from The Guide to Mac Family Hardware for
		 * SE/30, II, IIx, IIcx, IIci.
		 * $5000 6000 - $5000 7FFF: pseudo-DMA with /DRQ
		 * $5001 0000 - $5001 1FFF: normal mode
		 * $5001 2000 - $5001 3FFF: pseudo-DMA without /DRQ
		 * GMFH says that $5000 0000 - $50FF FFFF "wraps
		 * $5000 0000 - $5001 FFFF eight times" (!)
		 * mess.org says IIci and Color Classic do not alias
		 * I/O address space.
		 */
		platform_device_register_simple("mac_scsi", 0,
			mac_scsi_old_rsrc, ARRAY_SIZE(mac_scsi_old_rsrc));
		break;
	case MAC_SCSI_LC:
		/* Addresses from Mac LC data in Designing Cards & Drivers 3ed.
		 * Also from the Developer Notes for Classic II, LC III,
		 * Color Classic and IIvx.
		 * $50F0 6000 - $50F0 7FFF: SCSI handshake
		 * $50F1 0000 - $50F1 1FFF: SCSI
		 * $50F1 2000 - $50F1 3FFF: SCSI DMA
		 */
		platform_device_register_simple("mac_scsi", 0,
			mac_scsi_ccl_rsrc, ARRAY_SIZE(mac_scsi_ccl_rsrc));
		break;
	}

	/*
	 * Ethernet device
	 */

	if (macintosh_config->ether_type == MAC_ETHER_SONIC ||
	    macintosh_config->expansion_type == MAC_EXP_PDS_COMM)
		platform_device_register_simple("macsonic", -1, NULL, 0);

	if (macintosh_config->expansion_type == MAC_EXP_PDS ||
	    macintosh_config->expansion_type == MAC_EXP_PDS_COMM)
		platform_device_register_simple("mac89x0", -1, NULL, 0);

	if (macintosh_config->ether_type == MAC_ETHER_MACE)
		platform_device_register_simple("macmace", -1, NULL, 0);

	return 0;
}

arch_initcall(mac_platform_init);
