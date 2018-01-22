/*
 * Defines, structures, APIs for edac_mc module
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

#ifndef _EDAC_MC_H_
#define _EDAC_MC_H_

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

extern const char * const edac_mem_types[];

#ifdef CONFIG_EDAC_DEBUG
extern int edac_debug_level;

#define edac_dbg(level, fmt, ...)					\
do {									\
	if (level <= edac_debug_level)					\
		edac_printk(KERN_DEBUG, EDAC_DEBUG,			\
			    "%s: " fmt, __func__, ##__VA_ARGS__);	\
} while (0)

#else				/* !CONFIG_EDAC_DEBUG */

#define edac_dbg(level, fmt, ...)					\
do {									\
	if (0)								\
		edac_printk(KERN_DEBUG, EDAC_DEBUG,			\
			    "%s: " fmt, __func__, ##__VA_ARGS__);	\
} while (0)

#endif				/* !CONFIG_EDAC_DEBUG */

#define PCI_VEND_DEV(vend, dev) PCI_VENDOR_ID_ ## vend, \
	PCI_DEVICE_ID_ ## vend ## _ ## dev

#define edac_dev_name(dev) (dev)->dev_name

#define to_mci(k) container_of(k, struct mem_ctl_info, dev)

/**
 * edac_mc_alloc() - Allocate and partially fill a struct &mem_ctl_info.
 *
 * @mc_num:		Memory controller number
 * @n_layers:		Number of MC hierarchy layers
 * @layers:		Describes each layer as seen by the Memory Controller
 * @sz_pvt:		size of private storage needed
 *
 *
 * Everything is kmalloc'ed as one big chunk - more efficient.
 * Only can be used if all structures have the same lifetime - otherwise
 * you have to allocate and initialize your own structures.
 *
 * Use edac_mc_free() to free mc structures allocated by this function.
 *
 * .. note::
 *
 *   drivers handle multi-rank memories in different ways: in some
 *   drivers, one multi-rank memory stick is mapped as one entry, while, in
 *   others, a single multi-rank memory stick would be mapped into several
 *   entries. Currently, this function will allocate multiple struct dimm_info
 *   on such scenarios, as grouping the multiple ranks require drivers change.
 *
 * Returns:
 *	On success, return a pointer to struct mem_ctl_info pointer;
 *	%NULL otherwise
 */
struct mem_ctl_info *edac_mc_alloc(unsigned mc_num,
				   unsigned n_layers,
				   struct edac_mc_layer *layers,
				   unsigned sz_pvt);

/**
 * edac_get_owner - Return the owner's mod_name of EDAC MC
 *
 * Returns:
 *	Pointer to mod_name string when EDAC MC is owned. NULL otherwise.
 */
extern const char *edac_get_owner(void);

/*
 * edac_mc_add_mc_with_groups() - Insert the @mci structure into the mci
 *	global list and create sysfs entries associated with @mci structure.
 *
 * @mci: pointer to the mci structure to be added to the list
 * @groups: optional attribute groups for the driver-specific sysfs entries
 *
 * Returns:
 *	0 on Success, or an error code on failure
 */
extern int edac_mc_add_mc_with_groups(struct mem_ctl_info *mci,
				      const struct attribute_group **groups);
#define edac_mc_add_mc(mci)	edac_mc_add_mc_with_groups(mci, NULL)

/**
 * edac_mc_free() -  Frees a previously allocated @mci structure
 *
 * @mci: pointer to a struct mem_ctl_info structure
 */
extern void edac_mc_free(struct mem_ctl_info *mci);

/**
 * edac_has_mcs() - Check if any MCs have been allocated.
 *
 * Returns:
 *	True if MC instances have been registered successfully.
 *	False otherwise.
 */
extern bool edac_has_mcs(void);

/**
 * edac_mc_find() - Search for a mem_ctl_info structure whose index is @idx.
 *
 * @idx: index to be seek
 *
 * If found, return a pointer to the structure.
 * Else return NULL.
 */
extern struct mem_ctl_info *edac_mc_find(int idx);

/**
 * find_mci_by_dev() - Scan list of controllers looking for the one that
 *	manages the @dev device.
 *
 * @dev: pointer to a struct device related with the MCI
 *
 * Returns: on success, returns a pointer to struct &mem_ctl_info;
 * %NULL otherwise.
 */
extern struct mem_ctl_info *find_mci_by_dev(struct device *dev);

/**
 * edac_mc_del_mc() - Remove sysfs entries for mci structure associated with
 *	@dev and remove mci structure from global list.
 *
 * @dev: Pointer to struct &device representing mci structure to remove.
 *
 * Returns: pointer to removed mci structure, or %NULL if device not found.
 */
extern struct mem_ctl_info *edac_mc_del_mc(struct device *dev);

/**
 * edac_mc_find_csrow_by_page() - Ancillary routine to identify what csrow
 *	contains a memory page.
 *
 * @mci: pointer to a struct mem_ctl_info structure
 * @page: memory page to find
 *
 * Returns: on success, returns the csrow. -1 if not found.
 */
extern int edac_mc_find_csrow_by_page(struct mem_ctl_info *mci,
				      unsigned long page);

/**
 * edac_raw_mc_handle_error() - Reports a memory event to userspace without
 *	doing anything to discover the error location.
 *
 * @type:		severity of the error (CE/UE/Fatal)
 * @mci:		a struct mem_ctl_info pointer
 * @e:			error description
 *
 * This raw function is used internally by edac_mc_handle_error(). It should
 * only be called directly when the hardware error come directly from BIOS,
 * like in the case of APEI GHES driver.
 */
void edac_raw_mc_handle_error(const enum hw_event_mc_err_type type,
			      struct mem_ctl_info *mci,
			      struct edac_raw_error_desc *e);

/**
 * edac_mc_handle_error() - Reports a memory event to userspace.
 *
 * @type:		severity of the error (CE/UE/Fatal)
 * @mci:		a struct mem_ctl_info pointer
 * @error_count:	Number of errors of the same type
 * @page_frame_number:	mem page where the error occurred
 * @offset_in_page:	offset of the error inside the page
 * @syndrome:		ECC syndrome
 * @top_layer:		Memory layer[0] position
 * @mid_layer:		Memory layer[1] position
 * @low_layer:		Memory layer[2] position
 * @msg:		Message meaningful to the end users that
 *			explains the event
 * @other_detail:	Technical details about the event that
 *			may help hardware manufacturers and
 *			EDAC developers to analyse the event
 */
void edac_mc_handle_error(const enum hw_event_mc_err_type type,
			  struct mem_ctl_info *mci,
			  const u16 error_count,
			  const unsigned long page_frame_number,
			  const unsigned long offset_in_page,
			  const unsigned long syndrome,
			  const int top_layer,
			  const int mid_layer,
			  const int low_layer,
			  const char *msg,
			  const char *other_detail);

/*
 * edac misc APIs
 */
extern char *edac_op_state_to_string(int op_state);

#endif				/* _EDAC_MC_H_ */
