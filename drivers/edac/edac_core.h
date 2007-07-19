/*
 * Defines, structures, APIs for edac_core module
 *
 * (C) 2007 Linux Networx (http://lnxi.com)
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * Written by Thayne Harbaugh
 * Based on work by Dan Hollis <goemon at anime dot net> and others.
 *	http://www.anime.net/~goemon/linux-ecc/
 *
 * NMI handling support added by
 *     Dave Peterson <dsp@llnl.gov> <dave_peterson@pobox.com>
 *
 * Refactored for multi-source files:
 *	Doug Thompson <norsk5@xmission.com>
 *
 */

#ifndef _EDAC_CORE_H_
#define _EDAC_CORE_H_

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/pci.h>
#include <linux/time.h>
#include <linux/nmi.h>
#include <linux/rcupdate.h>
#include <linux/completion.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>

#define EDAC_MC_LABEL_LEN	31
#define MC_PROC_NAME_MAX_LEN 7

#if PAGE_SHIFT < 20
#define PAGES_TO_MiB( pages )	( ( pages ) >> ( 20 - PAGE_SHIFT ) )
#else				/* PAGE_SHIFT > 20 */
#define PAGES_TO_MiB( pages )	( ( pages ) << ( PAGE_SHIFT - 20 ) )
#endif

#define edac_printk(level, prefix, fmt, arg...) \
	printk(level "EDAC " prefix ": " fmt, ##arg)

#define edac_mc_printk(mci, level, fmt, arg...) \
	printk(level "EDAC MC%d: " fmt, mci->mc_idx, ##arg)

#define edac_mc_chipset_printk(mci, level, prefix, fmt, arg...) \
	printk(level "EDAC " prefix " MC%d: " fmt, mci->mc_idx, ##arg)

/* prefixes for edac_printk() and edac_mc_printk() */
#define EDAC_MC "MC"
#define EDAC_PCI "PCI"
#define EDAC_DEBUG "DEBUG"

#ifdef CONFIG_EDAC_DEBUG
extern int edac_debug_level;

#define edac_debug_printk(level, fmt, arg...)                            \
	do {                                                             \
		if (level <= edac_debug_level)                           \
			edac_printk(KERN_DEBUG, EDAC_DEBUG, fmt, ##arg); \
	} while(0)

#define debugf0( ... ) edac_debug_printk(0, __VA_ARGS__ )
#define debugf1( ... ) edac_debug_printk(1, __VA_ARGS__ )
#define debugf2( ... ) edac_debug_printk(2, __VA_ARGS__ )
#define debugf3( ... ) edac_debug_printk(3, __VA_ARGS__ )
#define debugf4( ... ) edac_debug_printk(4, __VA_ARGS__ )

#else  /* !CONFIG_EDAC_DEBUG */

#define debugf0( ... )
#define debugf1( ... )
#define debugf2( ... )
#define debugf3( ... )
#define debugf4( ... )

#endif  /* !CONFIG_EDAC_DEBUG */

#define BIT(x) (1 << (x))

#define PCI_VEND_DEV(vend, dev) PCI_VENDOR_ID_ ## vend, \
	PCI_DEVICE_ID_ ## vend ## _ ## dev

#if defined(CONFIG_X86) && defined(CONFIG_PCI)
#define dev_name(dev) pci_name(to_pci_dev(dev))
#else
#define dev_name(dev) to_platform_device(dev)->name
#endif

/* memory devices */
enum dev_type {
	DEV_UNKNOWN = 0,
	DEV_X1,
	DEV_X2,
	DEV_X4,
	DEV_X8,
	DEV_X16,
	DEV_X32,		/* Do these parts exist? */
	DEV_X64			/* Do these parts exist? */
};

#define DEV_FLAG_UNKNOWN	BIT(DEV_UNKNOWN)
#define DEV_FLAG_X1		BIT(DEV_X1)
#define DEV_FLAG_X2		BIT(DEV_X2)
#define DEV_FLAG_X4		BIT(DEV_X4)
#define DEV_FLAG_X8		BIT(DEV_X8)
#define DEV_FLAG_X16		BIT(DEV_X16)
#define DEV_FLAG_X32		BIT(DEV_X32)
#define DEV_FLAG_X64		BIT(DEV_X64)

/* memory types */
enum mem_type {
	MEM_EMPTY = 0,		/* Empty csrow */
	MEM_RESERVED,		/* Reserved csrow type */
	MEM_UNKNOWN,		/* Unknown csrow type */
	MEM_FPM,		/* Fast page mode */
	MEM_EDO,		/* Extended data out */
	MEM_BEDO,		/* Burst Extended data out */
	MEM_SDR,		/* Single data rate SDRAM */
	MEM_RDR,		/* Registered single data rate SDRAM */
	MEM_DDR,		/* Double data rate SDRAM */
	MEM_RDDR,		/* Registered Double data rate SDRAM */
	MEM_RMBS,		/* Rambus DRAM */
	MEM_DDR2,               /* DDR2 RAM */
	MEM_FB_DDR2,            /* fully buffered DDR2 */
	MEM_RDDR2,              /* Registered DDR2 RAM */
};

#define MEM_FLAG_EMPTY		BIT(MEM_EMPTY)
#define MEM_FLAG_RESERVED	BIT(MEM_RESERVED)
#define MEM_FLAG_UNKNOWN	BIT(MEM_UNKNOWN)
#define MEM_FLAG_FPM		BIT(MEM_FPM)
#define MEM_FLAG_EDO		BIT(MEM_EDO)
#define MEM_FLAG_BEDO		BIT(MEM_BEDO)
#define MEM_FLAG_SDR		BIT(MEM_SDR)
#define MEM_FLAG_RDR		BIT(MEM_RDR)
#define MEM_FLAG_DDR		BIT(MEM_DDR)
#define MEM_FLAG_RDDR		BIT(MEM_RDDR)
#define MEM_FLAG_RMBS		BIT(MEM_RMBS)
#define MEM_FLAG_DDR2           BIT(MEM_DDR2)
#define MEM_FLAG_FB_DDR2        BIT(MEM_FB_DDR2)
#define MEM_FLAG_RDDR2          BIT(MEM_RDDR2)

/* chipset Error Detection and Correction capabilities and mode */
enum edac_type {
	EDAC_UNKNOWN = 0,	/* Unknown if ECC is available */
	EDAC_NONE,		/* Doesnt support ECC */
	EDAC_RESERVED,		/* Reserved ECC type */
	EDAC_PARITY,		/* Detects parity errors */
	EDAC_EC,		/* Error Checking - no correction */
	EDAC_SECDED,		/* Single bit error correction, Double detection */
	EDAC_S2ECD2ED,		/* Chipkill x2 devices - do these exist? */
	EDAC_S4ECD4ED,		/* Chipkill x4 devices */
	EDAC_S8ECD8ED,		/* Chipkill x8 devices */
	EDAC_S16ECD16ED,	/* Chipkill x16 devices */
};

#define EDAC_FLAG_UNKNOWN	BIT(EDAC_UNKNOWN)
#define EDAC_FLAG_NONE		BIT(EDAC_NONE)
#define EDAC_FLAG_PARITY	BIT(EDAC_PARITY)
#define EDAC_FLAG_EC		BIT(EDAC_EC)
#define EDAC_FLAG_SECDED	BIT(EDAC_SECDED)
#define EDAC_FLAG_S2ECD2ED	BIT(EDAC_S2ECD2ED)
#define EDAC_FLAG_S4ECD4ED	BIT(EDAC_S4ECD4ED)
#define EDAC_FLAG_S8ECD8ED	BIT(EDAC_S8ECD8ED)
#define EDAC_FLAG_S16ECD16ED	BIT(EDAC_S16ECD16ED)

/* scrubbing capabilities */
enum scrub_type {
	SCRUB_UNKNOWN = 0,	/* Unknown if scrubber is available */
	SCRUB_NONE,		/* No scrubber */
	SCRUB_SW_PROG,		/* SW progressive (sequential) scrubbing */
	SCRUB_SW_SRC,		/* Software scrub only errors */
	SCRUB_SW_PROG_SRC,	/* Progressive software scrub from an error */
	SCRUB_SW_TUNABLE,	/* Software scrub frequency is tunable */
	SCRUB_HW_PROG,		/* HW progressive (sequential) scrubbing */
	SCRUB_HW_SRC,		/* Hardware scrub only errors */
	SCRUB_HW_PROG_SRC,	/* Progressive hardware scrub from an error */
	SCRUB_HW_TUNABLE	/* Hardware scrub frequency is tunable */
};

#define SCRUB_FLAG_SW_PROG	BIT(SCRUB_SW_PROG)
#define SCRUB_FLAG_SW_SRC	BIT(SCRUB_SW_SRC_CORR)
#define SCRUB_FLAG_SW_PROG_SRC	BIT(SCRUB_SW_PROG_SRC_CORR)
#define SCRUB_FLAG_SW_TUN	BIT(SCRUB_SW_SCRUB_TUNABLE)
#define SCRUB_FLAG_HW_PROG	BIT(SCRUB_HW_PROG)
#define SCRUB_FLAG_HW_SRC	BIT(SCRUB_HW_SRC_CORR)
#define SCRUB_FLAG_HW_PROG_SRC	BIT(SCRUB_HW_PROG_SRC_CORR)
#define SCRUB_FLAG_HW_TUN	BIT(SCRUB_HW_TUNABLE)

/* FIXME - should have notify capabilities: NMI, LOG, PROC, etc */

/*
 * There are several things to be aware of that aren't at all obvious:
 *
 *
 * SOCKETS, SOCKET SETS, BANKS, ROWS, CHIP-SELECT ROWS, CHANNELS, etc..
 *
 * These are some of the many terms that are thrown about that don't always
 * mean what people think they mean (Inconceivable!).  In the interest of
 * creating a common ground for discussion, terms and their definitions
 * will be established.
 *
 * Memory devices:	The individual chip on a memory stick.  These devices
 *			commonly output 4 and 8 bits each.  Grouping several
 *			of these in parallel provides 64 bits which is common
 *			for a memory stick.
 *
 * Memory Stick:	A printed circuit board that agregates multiple
 *			memory devices in parallel.  This is the atomic
 *			memory component that is purchaseable by Joe consumer
 *			and loaded into a memory socket.
 *
 * Socket:		A physical connector on the motherboard that accepts
 *			a single memory stick.
 *
 * Channel:		Set of memory devices on a memory stick that must be
 *			grouped in parallel with one or more additional
 *			channels from other memory sticks.  This parallel
 *			grouping of the output from multiple channels are
 *			necessary for the smallest granularity of memory access.
 *			Some memory controllers are capable of single channel -
 *			which means that memory sticks can be loaded
 *			individually.  Other memory controllers are only
 *			capable of dual channel - which means that memory
 *			sticks must be loaded as pairs (see "socket set").
 *
 * Chip-select row:	All of the memory devices that are selected together.
 *			for a single, minimum grain of memory access.
 *			This selects all of the parallel memory devices across
 *			all of the parallel channels.  Common chip-select rows
 *			for single channel are 64 bits, for dual channel 128
 *			bits.
 *
 * Single-Ranked stick:	A Single-ranked stick has 1 chip-select row of memmory.
 *			Motherboards commonly drive two chip-select pins to
 *			a memory stick. A single-ranked stick, will occupy
 *			only one of those rows. The other will be unused.
 *
 * Double-Ranked stick:	A double-ranked stick has two chip-select rows which
 *			access different sets of memory devices.  The two
 *			rows cannot be accessed concurrently.
 *
 * Double-sided stick:	DEPRECATED TERM, see Double-Ranked stick.
 *			A double-sided stick has two chip-select rows which
 *			access different sets of memory devices.  The two
 *			rows cannot be accessed concurrently.  "Double-sided"
 *			is irrespective of the memory devices being mounted
 *			on both sides of the memory stick.
 *
 * Socket set:		All of the memory sticks that are required for for
 *			a single memory access or all of the memory sticks
 *			spanned by a chip-select row.  A single socket set
 *			has two chip-select rows and if double-sided sticks
 *			are used these will occupy those chip-select rows.
 *
 * Bank:		This term is avoided because it is unclear when
 *			needing to distinguish between chip-select rows and
 *			socket sets.
 *
 * Controller pages:
 *
 * Physical pages:
 *
 * Virtual pages:
 *
 *
 * STRUCTURE ORGANIZATION AND CHOICES
 *
 *
 *
 * PS - I enjoyed writing all that about as much as you enjoyed reading it.
 */

struct channel_info {
	int chan_idx;		/* channel index */
	u32 ce_count;		/* Correctable Errors for this CHANNEL */
	char label[EDAC_MC_LABEL_LEN + 1];  /* DIMM label on motherboard */
	struct csrow_info *csrow;	/* the parent */
};

struct csrow_info {
	unsigned long first_page;	/* first page number in dimm */
	unsigned long last_page;	/* last page number in dimm */
	unsigned long page_mask;	/* used for interleaving -
					 * 0UL for non intlv
					 */
	u32 nr_pages;		/* number of pages in csrow */
	u32 grain;		/* granularity of reported error in bytes */
	int csrow_idx;		/* the chip-select row */
	enum dev_type dtype;	/* memory device type */
	u32 ue_count;		/* Uncorrectable Errors for this csrow */
	u32 ce_count;		/* Correctable Errors for this csrow */
	enum mem_type mtype;	/* memory csrow type */
	enum edac_type edac_mode;	/* EDAC mode for this csrow */
	struct mem_ctl_info *mci;	/* the parent */

	struct kobject kobj;	/* sysfs kobject for this csrow */
	struct completion kobj_complete;

	/* FIXME the number of CHANNELs might need to become dynamic */
	u32 nr_channels;
	struct channel_info *channels;
};

struct mem_ctl_info {
	struct list_head link;  /* for global list of mem_ctl_info structs */
	unsigned long mtype_cap;	/* memory types supported by mc */
	unsigned long edac_ctl_cap;	/* Mem controller EDAC capabilities */
	unsigned long edac_cap;	/* configuration capabilities - this is
				 * closely related to edac_ctl_cap.  The
				 * difference is that the controller may be
				 * capable of s4ecd4ed which would be listed
				 * in edac_ctl_cap, but if channels aren't
				 * capable of s4ecd4ed then the edac_cap would
				 * not have that capability.
				 */
	unsigned long scrub_cap;	/* chipset scrub capabilities */
	enum scrub_type scrub_mode;	/* current scrub mode */

	/* Translates sdram memory scrub rate given in bytes/sec to the
	   internal representation and configures whatever else needs
	   to be configured.
	*/
	int (*set_sdram_scrub_rate) (struct mem_ctl_info *mci, u32 *bw);

	/* Get the current sdram memory scrub rate from the internal
	   representation and converts it to the closest matching
	   bandwith in bytes/sec.
	*/
	int (*get_sdram_scrub_rate) (struct mem_ctl_info *mci, u32 *bw);

	/* pointer to edac checking routine */
	void (*edac_check) (struct mem_ctl_info * mci);

	/*
	 * Remaps memory pages: controller pages to physical pages.
	 * For most MC's, this will be NULL.
	 */
	/* FIXME - why not send the phys page to begin with? */
	unsigned long (*ctl_page_to_phys) (struct mem_ctl_info * mci,
					unsigned long page);
	int mc_idx;
	int nr_csrows;
	struct csrow_info *csrows;
	/*
	 * FIXME - what about controllers on other busses? - IDs must be
	 * unique.  dev pointer should be sufficiently unique, but
	 * BUS:SLOT.FUNC numbers may not be unique.
	 */
	struct device *dev;
	const char *mod_name;
	const char *mod_ver;
	const char *ctl_name;
	char proc_name[MC_PROC_NAME_MAX_LEN + 1];
	void *pvt_info;
	u32 ue_noinfo_count;	/* Uncorrectable Errors w/o info */
	u32 ce_noinfo_count;	/* Correctable Errors w/o info */
	u32 ue_count;		/* Total Uncorrectable Errors for this MC */
	u32 ce_count;		/* Total Correctable Errors for this MC */
	unsigned long start_time;	/* mci load start time (in jiffies) */

	/* this stuff is for safe removal of mc devices from global list while
	 * NMI handlers may be traversing list
	 */
	struct rcu_head rcu;
	struct completion complete;

	/* edac sysfs device control */
	struct kobject edac_mci_kobj;
	struct completion kobj_complete;
};

#ifdef CONFIG_PCI

/* write all or some bits in a byte-register*/
static inline void pci_write_bits8(struct pci_dev *pdev, int offset, u8 value,
		u8 mask)
{
	if (mask != 0xff) {
		u8 buf;

		pci_read_config_byte(pdev, offset, &buf);
		value &= mask;
		buf &= ~mask;
		value |= buf;
	}

	pci_write_config_byte(pdev, offset, value);
}

/* write all or some bits in a word-register*/
static inline void pci_write_bits16(struct pci_dev *pdev, int offset,
		u16 value, u16 mask)
{
	if (mask != 0xffff) {
		u16 buf;

		pci_read_config_word(pdev, offset, &buf);
		value &= mask;
		buf &= ~mask;
		value |= buf;
	}

	pci_write_config_word(pdev, offset, value);
}

/* write all or some bits in a dword-register*/
static inline void pci_write_bits32(struct pci_dev *pdev, int offset,
		u32 value, u32 mask)
{
	if (mask != 0xffff) {
		u32 buf;

		pci_read_config_dword(pdev, offset, &buf);
		value &= mask;
		buf &= ~mask;
		value |= buf;
	}

	pci_write_config_dword(pdev, offset, value);
}

#endif /* CONFIG_PCI */

extern struct mem_ctl_info * edac_mc_find(int idx);
extern int edac_mc_add_mc(struct mem_ctl_info *mci,int mc_idx);
extern struct mem_ctl_info * edac_mc_del_mc(struct device *dev);
extern int edac_mc_find_csrow_by_page(struct mem_ctl_info *mci,
					unsigned long page);

/*
 * The no info errors are used when error overflows are reported.
 * There are a limited number of error logging registers that can
 * be exausted.  When all registers are exhausted and an additional
 * error occurs then an error overflow register records that an
 * error occured and the type of error, but doesn't have any
 * further information.  The ce/ue versions make for cleaner
 * reporting logic and function interface - reduces conditional
 * statement clutter and extra function arguments.
 */
extern void edac_mc_handle_ce(struct mem_ctl_info *mci,
		unsigned long page_frame_number, unsigned long offset_in_page,
		unsigned long syndrome, int row, int channel,
		const char *msg);
extern void edac_mc_handle_ce_no_info(struct mem_ctl_info *mci,
		const char *msg);
extern void edac_mc_handle_ue(struct mem_ctl_info *mci,
		unsigned long page_frame_number, unsigned long offset_in_page,
		int row, const char *msg);
extern void edac_mc_handle_ue_no_info(struct mem_ctl_info *mci,
		const char *msg);
extern void edac_mc_handle_fbd_ue(struct mem_ctl_info *mci,
		unsigned int csrow,
		unsigned int channel0,
		unsigned int channel1,
		char *msg);
extern void edac_mc_handle_fbd_ce(struct mem_ctl_info *mci,
		unsigned int csrow,
		unsigned int channel,
		char *msg);

/*
 * This kmalloc's and initializes all the structures.
 * Can't be used if all structures don't have the same lifetime.
 */
extern struct mem_ctl_info *edac_mc_alloc(unsigned sz_pvt, unsigned nr_csrows,
		unsigned nr_chans);

/* Free an mc previously allocated by edac_mc_alloc() */
extern void edac_mc_free(struct mem_ctl_info *mci);

#endif				/* _EDAC_CORE_H_ */
