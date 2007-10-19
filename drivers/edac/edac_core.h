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
#include <linux/sysdev.h>
#include <linux/workqueue.h>
#include <linux/version.h>

#define EDAC_MC_LABEL_LEN	31
#define EDAC_DEVICE_NAME_LEN	31
#define EDAC_ATTRIB_VALUE_LEN	15
#define MC_PROC_NAME_MAX_LEN	7

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

/* edac_device printk */
#define edac_device_printk(ctl, level, fmt, arg...) \
	printk(level "EDAC DEVICE%d: " fmt, ctl->dev_idx, ##arg)

/* edac_pci printk */
#define edac_pci_printk(ctl, level, fmt, arg...) \
	printk(level "EDAC PCI%d: " fmt, ctl->pci_idx, ##arg)

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

#else				/* !CONFIG_EDAC_DEBUG */

#define debugf0( ... )
#define debugf1( ... )
#define debugf2( ... )
#define debugf3( ... )
#define debugf4( ... )

#endif				/* !CONFIG_EDAC_DEBUG */

#define PCI_VEND_DEV(vend, dev) PCI_VENDOR_ID_ ## vend, \
	PCI_DEVICE_ID_ ## vend ## _ ## dev

#define dev_name(dev) (dev)->dev_name

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
	MEM_DDR2,		/* DDR2 RAM */
	MEM_FB_DDR2,		/* fully buffered DDR2 */
	MEM_RDDR2,		/* Registered DDR2 RAM */
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
#define SCRUB_FLAG_SW_SRC	BIT(SCRUB_SW_SRC)
#define SCRUB_FLAG_SW_PROG_SRC	BIT(SCRUB_SW_PROG_SRC)
#define SCRUB_FLAG_SW_TUN	BIT(SCRUB_SW_SCRUB_TUNABLE)
#define SCRUB_FLAG_HW_PROG	BIT(SCRUB_HW_PROG)
#define SCRUB_FLAG_HW_SRC	BIT(SCRUB_HW_SRC)
#define SCRUB_FLAG_HW_PROG_SRC	BIT(SCRUB_HW_PROG_SRC)
#define SCRUB_FLAG_HW_TUN	BIT(SCRUB_HW_TUNABLE)

/* FIXME - should have notify capabilities: NMI, LOG, PROC, etc */

/* EDAC internal operation states */
#define	OP_ALLOC		0x100
#define OP_RUNNING_POLL		0x201
#define OP_RUNNING_INTERRUPT	0x202
#define OP_RUNNING_POLL_INTR	0x203
#define OP_OFFLINE		0x300

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
	char label[EDAC_MC_LABEL_LEN + 1];	/* DIMM label on motherboard */
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

	/* channel information for this csrow */
	u32 nr_channels;
	struct channel_info *channels;
};

/* mcidev_sysfs_attribute structure
 *	used for driver sysfs attributes and in mem_ctl_info
 * 	sysfs top level entries
 */
struct mcidev_sysfs_attribute {
        struct attribute attr;
        ssize_t (*show)(struct mem_ctl_info *,char *);
        ssize_t (*store)(struct mem_ctl_info *, const char *,size_t);
};

/* MEMORY controller information structure
 */
struct mem_ctl_info {
	struct list_head link;	/* for global list of mem_ctl_info structs */

	struct module *owner;	/* Module owner of this control struct */

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
	int (*set_sdram_scrub_rate) (struct mem_ctl_info * mci, u32 * bw);

	/* Get the current sdram memory scrub rate from the internal
	   representation and converts it to the closest matching
	   bandwith in bytes/sec.
	 */
	int (*get_sdram_scrub_rate) (struct mem_ctl_info * mci, u32 * bw);


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
	const char *dev_name;
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

	/* Additional top controller level attributes, but specified
	 * by the low level driver.
	 *
	 * Set by the low level driver to provide attributes at the
	 * controller level, same level as 'ue_count' and 'ce_count' above.
	 * An array of structures, NULL terminated
	 *
	 * If attributes are desired, then set to array of attributes
	 * If no attributes are desired, leave NULL
	 */
	struct mcidev_sysfs_attribute *mc_driver_sysfs_attributes;

	/* work struct for this MC */
	struct delayed_work work;

	/* the internal state of this controller instance */
	int op_state;
};

/*
 * The following are the structures to provide for a generic
 * or abstract 'edac_device'. This set of structures and the
 * code that implements the APIs for the same, provide for
 * registering EDAC type devices which are NOT standard memory.
 *
 * CPU caches (L1 and L2)
 * DMA engines
 * Core CPU swithces
 * Fabric switch units
 * PCIe interface controllers
 * other EDAC/ECC type devices that can be monitored for
 * errors, etc.
 *
 * It allows for a 2 level set of hiearchry. For example:
 *
 * cache could be composed of L1, L2 and L3 levels of cache.
 * Each CPU core would have its own L1 cache, while sharing
 * L2 and maybe L3 caches.
 *
 * View them arranged, via the sysfs presentation:
 * /sys/devices/system/edac/..
 *
 *	mc/		<existing memory device directory>
 *	cpu/cpu0/..	<L1 and L2 block directory>
 *		/L1-cache/ce_count
 *			 /ue_count
 *		/L2-cache/ce_count
 *			 /ue_count
 *	cpu/cpu1/..	<L1 and L2 block directory>
 *		/L1-cache/ce_count
 *			 /ue_count
 *		/L2-cache/ce_count
 *			 /ue_count
 *	...
 *
 *	the L1 and L2 directories would be "edac_device_block's"
 */

struct edac_device_counter {
	u32 ue_count;
	u32 ce_count;
};

/* forward reference */
struct edac_device_ctl_info;
struct edac_device_block;

/* edac_dev_sysfs_attribute structure
 *	used for driver sysfs attributes in mem_ctl_info
 *	for extra controls and attributes:
 *		like high level error Injection controls
 */
struct edac_dev_sysfs_attribute {
	struct attribute attr;
	ssize_t (*show)(struct edac_device_ctl_info *, char *);
	ssize_t (*store)(struct edac_device_ctl_info *, const char *, size_t);
};

/* edac_dev_sysfs_block_attribute structure
 *
 *	used in leaf 'block' nodes for adding controls/attributes
 *
 *	each block in each instance of the containing control structure
 *	can have an array of the following. The show and store functions
 *	will be filled in with the show/store function in the
 *	low level driver.
 *
 *	The 'value' field will be the actual value field used for
 *	counting
 */
struct edac_dev_sysfs_block_attribute {
	struct attribute attr;
	ssize_t (*show)(struct kobject *, struct attribute *, char *);
	ssize_t (*store)(struct kobject *, struct attribute *,
			const char *, size_t);
	struct edac_device_block *block;

	unsigned int value;
};

/* device block control structure */
struct edac_device_block {
	struct edac_device_instance *instance;	/* Up Pointer */
	char name[EDAC_DEVICE_NAME_LEN + 1];

	struct edac_device_counter counters;	/* basic UE and CE counters */

	int nr_attribs;		/* how many attributes */

	/* this block's attributes, could be NULL */
	struct edac_dev_sysfs_block_attribute *block_attributes;

	/* edac sysfs device control */
	struct kobject kobj;
};

/* device instance control structure */
struct edac_device_instance {
	struct edac_device_ctl_info *ctl;	/* Up pointer */
	char name[EDAC_DEVICE_NAME_LEN + 4];

	struct edac_device_counter counters;	/* instance counters */

	u32 nr_blocks;		/* how many blocks */
	struct edac_device_block *blocks;	/* block array */

	/* edac sysfs device control */
	struct kobject kobj;
};


/*
 * Abstract edac_device control info structure
 *
 */
struct edac_device_ctl_info {
	/* for global list of edac_device_ctl_info structs */
	struct list_head link;

	struct module *owner;	/* Module owner of this control struct */

	int dev_idx;

	/* Per instance controls for this edac_device */
	int log_ue;		/* boolean for logging UEs */
	int log_ce;		/* boolean for logging CEs */
	int panic_on_ue;	/* boolean for panic'ing on an UE */
	unsigned poll_msec;	/* number of milliseconds to poll interval */
	unsigned long delay;	/* number of jiffies for poll_msec */

	/* Additional top controller level attributes, but specified
	 * by the low level driver.
	 *
	 * Set by the low level driver to provide attributes at the
	 * controller level, same level as 'ue_count' and 'ce_count' above.
	 * An array of structures, NULL terminated
	 *
	 * If attributes are desired, then set to array of attributes
	 * If no attributes are desired, leave NULL
	 */
	struct edac_dev_sysfs_attribute *sysfs_attributes;

	/* pointer to main 'edac' class in sysfs */
	struct sysdev_class *edac_class;

	/* the internal state of this controller instance */
	int op_state;
	/* work struct for this instance */
	struct delayed_work work;

	/* pointer to edac polling checking routine:
	 *      If NOT NULL: points to polling check routine
	 *      If NULL: Then assumes INTERRUPT operation, where
	 *              MC driver will receive events
	 */
	void (*edac_check) (struct edac_device_ctl_info * edac_dev);

	struct device *dev;	/* pointer to device structure */

	const char *mod_name;	/* module name */
	const char *ctl_name;	/* edac controller  name */
	const char *dev_name;	/* pci/platform/etc... name */

	void *pvt_info;		/* pointer to 'private driver' info */

	unsigned long start_time;	/* edac_device load start time (jiffies) */

	/* these are for safe removal of mc devices from global list while
	 * NMI handlers may be traversing list
	 */
	struct rcu_head rcu;
	struct completion removal_complete;

	/* sysfs top name under 'edac' directory
	 * and instance name:
	 *      cpu/cpu0/...
	 *      cpu/cpu1/...
	 *      cpu/cpu2/...
	 *      ...
	 */
	char name[EDAC_DEVICE_NAME_LEN + 1];

	/* Number of instances supported on this control structure
	 * and the array of those instances
	 */
	u32 nr_instances;
	struct edac_device_instance *instances;

	/* Event counters for the this whole EDAC Device */
	struct edac_device_counter counters;

	/* edac sysfs device control for the 'name'
	 * device this structure controls
	 */
	struct kobject kobj;
};

/* To get from the instance's wq to the beginning of the ctl structure */
#define to_edac_mem_ctl_work(w) \
		container_of(w, struct mem_ctl_info, work)

#define to_edac_device_ctl_work(w) \
		container_of(w,struct edac_device_ctl_info,work)

/*
 * The alloc() and free() functions for the 'edac_device' control info
 * structure. A MC driver will allocate one of these for each edac_device
 * it is going to control/register with the EDAC CORE.
 */
extern struct edac_device_ctl_info *edac_device_alloc_ctl_info(
		unsigned sizeof_private,
		char *edac_device_name, unsigned nr_instances,
		char *edac_block_name, unsigned nr_blocks,
		unsigned offset_value,
		struct edac_dev_sysfs_block_attribute *block_attributes,
		unsigned nr_attribs,
		int device_index);

/* The offset value can be:
 *	-1 indicating no offset value
 *	0 for zero-based block numbers
 *	1 for 1-based block number
 *	other for other-based block number
 */
#define	BLOCK_OFFSET_VALUE_OFF	((unsigned) -1)

extern void edac_device_free_ctl_info(struct edac_device_ctl_info *ctl_info);

#ifdef CONFIG_PCI

struct edac_pci_counter {
	atomic_t pe_count;
	atomic_t npe_count;
};

/*
 * Abstract edac_pci control info structure
 *
 */
struct edac_pci_ctl_info {
	/* for global list of edac_pci_ctl_info structs */
	struct list_head link;

	int pci_idx;

	struct sysdev_class *edac_class;	/* pointer to class */

	/* the internal state of this controller instance */
	int op_state;
	/* work struct for this instance */
	struct delayed_work work;

	/* pointer to edac polling checking routine:
	 *      If NOT NULL: points to polling check routine
	 *      If NULL: Then assumes INTERRUPT operation, where
	 *              MC driver will receive events
	 */
	void (*edac_check) (struct edac_pci_ctl_info * edac_dev);

	struct device *dev;	/* pointer to device structure */

	const char *mod_name;	/* module name */
	const char *ctl_name;	/* edac controller  name */
	const char *dev_name;	/* pci/platform/etc... name */

	void *pvt_info;		/* pointer to 'private driver' info */

	unsigned long start_time;	/* edac_pci load start time (jiffies) */

	/* these are for safe removal of devices from global list while
	 * NMI handlers may be traversing list
	 */
	struct rcu_head rcu;
	struct completion complete;

	/* sysfs top name under 'edac' directory
	 * and instance name:
	 *      cpu/cpu0/...
	 *      cpu/cpu1/...
	 *      cpu/cpu2/...
	 *      ...
	 */
	char name[EDAC_DEVICE_NAME_LEN + 1];

	/* Event counters for the this whole EDAC Device */
	struct edac_pci_counter counters;

	/* edac sysfs device control for the 'name'
	 * device this structure controls
	 */
	struct kobject kobj;
	struct completion kobj_complete;
};

#define to_edac_pci_ctl_work(w) \
		container_of(w, struct edac_pci_ctl_info,work)

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

#endif				/* CONFIG_PCI */

extern struct mem_ctl_info *edac_mc_alloc(unsigned sz_pvt, unsigned nr_csrows,
					  unsigned nr_chans, int edac_index);
extern int edac_mc_add_mc(struct mem_ctl_info *mci);
extern void edac_mc_free(struct mem_ctl_info *mci);
extern struct mem_ctl_info *edac_mc_find(int idx);
extern struct mem_ctl_info *edac_mc_del_mc(struct device *dev);
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
			      unsigned long page_frame_number,
			      unsigned long offset_in_page,
			      unsigned long syndrome, int row, int channel,
			      const char *msg);
extern void edac_mc_handle_ce_no_info(struct mem_ctl_info *mci,
				      const char *msg);
extern void edac_mc_handle_ue(struct mem_ctl_info *mci,
			      unsigned long page_frame_number,
			      unsigned long offset_in_page, int row,
			      const char *msg);
extern void edac_mc_handle_ue_no_info(struct mem_ctl_info *mci,
				      const char *msg);
extern void edac_mc_handle_fbd_ue(struct mem_ctl_info *mci, unsigned int csrow,
				  unsigned int channel0, unsigned int channel1,
				  char *msg);
extern void edac_mc_handle_fbd_ce(struct mem_ctl_info *mci, unsigned int csrow,
				  unsigned int channel, char *msg);

/*
 * edac_device APIs
 */
extern int edac_device_add_device(struct edac_device_ctl_info *edac_dev);
extern struct edac_device_ctl_info *edac_device_del_device(struct device *dev);
extern void edac_device_handle_ue(struct edac_device_ctl_info *edac_dev,
				int inst_nr, int block_nr, const char *msg);
extern void edac_device_handle_ce(struct edac_device_ctl_info *edac_dev,
				int inst_nr, int block_nr, const char *msg);

/*
 * edac_pci APIs
 */
extern struct edac_pci_ctl_info *edac_pci_alloc_ctl_info(unsigned int sz_pvt,
				const char *edac_pci_name);

extern void edac_pci_free_ctl_info(struct edac_pci_ctl_info *pci);

extern void edac_pci_reset_delay_period(struct edac_pci_ctl_info *pci,
				unsigned long value);

extern int edac_pci_add_device(struct edac_pci_ctl_info *pci, int edac_idx);
extern struct edac_pci_ctl_info *edac_pci_del_device(struct device *dev);

extern struct edac_pci_ctl_info *edac_pci_create_generic_ctl(
				struct device *dev,
				const char *mod_name);

extern void edac_pci_release_generic_ctl(struct edac_pci_ctl_info *pci);
extern int edac_pci_create_sysfs(struct edac_pci_ctl_info *pci);
extern void edac_pci_remove_sysfs(struct edac_pci_ctl_info *pci);

/*
 * edac misc APIs
 */
extern char *edac_op_state_to_string(int op_state);

#endif				/* _EDAC_CORE_H_ */
