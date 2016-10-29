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

#include "edac_pci.h"
#include "edac_device.h"

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

struct mem_ctl_info *edac_mc_alloc(unsigned mc_num,
				   unsigned n_layers,
				   struct edac_mc_layer *layers,
				   unsigned sz_pvt);
extern int edac_mc_add_mc_with_groups(struct mem_ctl_info *mci,
				      const struct attribute_group **groups);
#define edac_mc_add_mc(mci)	edac_mc_add_mc_with_groups(mci, NULL)
extern void edac_mc_free(struct mem_ctl_info *mci);
extern struct mem_ctl_info *edac_mc_find(int idx);
extern struct mem_ctl_info *find_mci_by_dev(struct device *dev);
extern struct mem_ctl_info *edac_mc_del_mc(struct device *dev);
extern int edac_mc_find_csrow_by_page(struct mem_ctl_info *mci,
				      unsigned long page);

void edac_raw_mc_handle_error(const enum hw_event_mc_err_type type,
			      struct mem_ctl_info *mci,
			      struct edac_raw_error_desc *e);

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

#endif				/* _EDAC_CORE_H_ */
