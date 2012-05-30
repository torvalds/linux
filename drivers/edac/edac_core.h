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
#include <linux/workqueue.h>
#include <linux/edac.h>

#define EDAC_DEVICE_NAME_LEN	31
#define EDAC_ATTRIB_VALUE_LEN	15

#if PAGE_SHIFT < 20
#define PAGES_TO_MiB(pages)	((pages) >> (20 - PAGE_SHIFT))
#define MiB_TO_PAGES(mb)	((mb) << (20 - PAGE_SHIFT))
#else				/* PAGE_SHIFT > 20 */
#define PAGES_TO_MiB(pages)	((pages) << (PAGE_SHIFT - 20))
#define MiB_TO_PAGES(mb)	((mb) >> (PAGE_SHIFT - 20))
#endif

#define edac_printk(level, prefix, fmt, arg...) \
	printk(level "EDAC " prefix ": " fmt, ##arg)

#define edac_mc_printk(mci, level, fmt, arg...) \
	printk(level "EDAC MC%d: " fmt, mci->mc_idx, ##arg)

#define edac_mc_chipset_printk(mci, level, prefix, fmt, arg...) \
	printk(level "EDAC " prefix " MC%d: " fmt, mci->mc_idx, ##arg)

#define edac_device_printk(ctl, level, fmt, arg...) \
	printk(level "EDAC DEVICE%d: " fmt, ctl->dev_idx, ##arg)

#define edac_pci_printk(ctl, level, fmt, arg...) \
	printk(level "EDAC PCI%d: " fmt, ctl->pci_idx, ##arg)

/* prefixes for edac_printk() and edac_mc_printk() */
#define EDAC_MC "MC"
#define EDAC_PCI "PCI"
#define EDAC_DEBUG "DEBUG"

extern const char *edac_mem_types[];

#ifdef CONFIG_EDAC_DEBUG
extern int edac_debug_level;

#define edac_debug_printk(level, fmt, arg...)                           \
	do {                                                            \
		if (level <= edac_debug_level)                          \
			edac_printk(KERN_DEBUG, EDAC_DEBUG,		\
				    "%s: " fmt, __func__, ##arg);	\
	} while (0)

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

#define edac_dev_name(dev) (dev)->dev_name

/*
 * The following are the structures to provide for a generic
 * or abstract 'edac_device'. This set of structures and the
 * code that implements the APIs for the same, provide for
 * registering EDAC type devices which are NOT standard memory.
 *
 * CPU caches (L1 and L2)
 * DMA engines
 * Core CPU switches
 * Fabric switch units
 * PCIe interface controllers
 * other EDAC/ECC type devices that can be monitored for
 * errors, etc.
 *
 * It allows for a 2 level set of hierarchy. For example:
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

	/* pointer to main 'edac' subsys in sysfs */
	struct bus_type *edac_subsys;

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

	struct bus_type *edac_subsys;	/* pointer to subsystem */

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

/*
 * pci_write_bits32
 *
 * edac local routine to do pci_write_config_dword, but adds
 * a mask parameter. If mask is all ones, ignore the mask.
 * Otherwise utilize the mask to isolate specified bits
 *
 * write all or some bits in a dword-register
 */
static inline void pci_write_bits32(struct pci_dev *pdev, int offset,
				    u32 value, u32 mask)
{
	if (mask != 0xffffffff) {
		u32 buf;

		pci_read_config_dword(pdev, offset, &buf);
		value &= mask;
		buf &= ~mask;
		value |= buf;
	}

	pci_write_config_dword(pdev, offset, value);
}

#endif				/* CONFIG_PCI */

struct mem_ctl_info *edac_mc_alloc(unsigned mc_num,
				   unsigned n_layers,
				   struct edac_mc_layer *layers,
				   unsigned sz_pvt);
extern int edac_mc_add_mc(struct mem_ctl_info *mci);
extern void edac_mc_free(struct mem_ctl_info *mci);
extern struct mem_ctl_info *edac_mc_find(int idx);
extern struct mem_ctl_info *find_mci_by_dev(struct device *dev);
extern struct mem_ctl_info *edac_mc_del_mc(struct device *dev);
extern int edac_mc_find_csrow_by_page(struct mem_ctl_info *mci,
				      unsigned long page);
void edac_mc_handle_error(const enum hw_event_mc_err_type type,
			  struct mem_ctl_info *mci,
			  const unsigned long page_frame_number,
			  const unsigned long offset_in_page,
			  const unsigned long syndrome,
			  const int layer0,
			  const int layer1,
			  const int layer2,
			  const char *msg,
			  const char *other_detail,
			  const void *mcelog);

/*
 * edac_device APIs
 */
extern int edac_device_add_device(struct edac_device_ctl_info *edac_dev);
extern struct edac_device_ctl_info *edac_device_del_device(struct device *dev);
extern void edac_device_handle_ue(struct edac_device_ctl_info *edac_dev,
				int inst_nr, int block_nr, const char *msg);
extern void edac_device_handle_ce(struct edac_device_ctl_info *edac_dev,
				int inst_nr, int block_nr, const char *msg);
extern int edac_device_alloc_index(void);
extern const char *edac_layer_name[];

/*
 * edac_pci APIs
 */
extern struct edac_pci_ctl_info *edac_pci_alloc_ctl_info(unsigned int sz_pvt,
				const char *edac_pci_name);

extern void edac_pci_free_ctl_info(struct edac_pci_ctl_info *pci);

extern void edac_pci_reset_delay_period(struct edac_pci_ctl_info *pci,
				unsigned long value);

extern int edac_pci_alloc_index(void);
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
