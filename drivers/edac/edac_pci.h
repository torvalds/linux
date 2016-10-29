/*
 * Defines, structures, APIs for edac_pci and edac_pci_sysfs
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
 * Please look at Documentation/driver-api/edac.rst for more info about
 * EDAC core structs and functions.
 */

#ifndef _EDAC_PCI_H_
#define _EDAC_PCI_H_

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/edac.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/workqueue.h>

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

extern struct edac_pci_ctl_info *edac_pci_alloc_ctl_info(unsigned int sz_pvt,
				const char *edac_pci_name);

extern void edac_pci_free_ctl_info(struct edac_pci_ctl_info *pci);

extern int edac_pci_alloc_index(void);
extern int edac_pci_add_device(struct edac_pci_ctl_info *pci, int edac_idx);
extern struct edac_pci_ctl_info *edac_pci_del_device(struct device *dev);

extern struct edac_pci_ctl_info *edac_pci_create_generic_ctl(
				struct device *dev,
				const char *mod_name);

extern void edac_pci_release_generic_ctl(struct edac_pci_ctl_info *pci);
extern int edac_pci_create_sysfs(struct edac_pci_ctl_info *pci);
extern void edac_pci_remove_sysfs(struct edac_pci_ctl_info *pci);

#endif
