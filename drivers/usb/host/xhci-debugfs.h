/*
 * xhci-debugfs.h - xHCI debugfs interface
 *
 * Copyright (C) 2017 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_XHCI_DEBUGFS_H
#define __LINUX_XHCI_DEBUGFS_H

#include <linux/debugfs.h>

#define DEBUGFS_NAMELEN 32

#define REG_CAPLENGTH					0x00
#define REG_HCSPARAMS1					0x04
#define REG_HCSPARAMS2					0x08
#define REG_HCSPARAMS3					0x0c
#define REG_HCCPARAMS1					0x10
#define REG_DOORBELLOFF					0x14
#define REG_RUNTIMEOFF					0x18
#define REG_HCCPARAMS2					0x1c

#define	REG_USBCMD					0x00
#define REG_USBSTS					0x04
#define REG_PAGESIZE					0x08
#define REG_DNCTRL					0x14
#define REG_CRCR					0x18
#define REG_DCBAAP_LOW					0x30
#define REG_DCBAAP_HIGH					0x34
#define REG_CONFIG					0x38

#define REG_MFINDEX					0x00
#define REG_IR0_IMAN					0x20
#define REG_IR0_IMOD					0x24
#define REG_IR0_ERSTSZ					0x28
#define REG_IR0_ERSTBA_LOW				0x30
#define REG_IR0_ERSTBA_HIGH				0x34
#define REG_IR0_ERDP_LOW				0x38
#define REG_IR0_ERDP_HIGH				0x3c

#define REG_EXTCAP_USBLEGSUP				0x00
#define REG_EXTCAP_USBLEGCTLSTS				0x04

#define REG_EXTCAP_REVISION				0x00
#define REG_EXTCAP_NAME					0x04
#define REG_EXTCAP_PORTINFO				0x08
#define REG_EXTCAP_PORTTYPE				0x0c
#define REG_EXTCAP_MANTISSA1				0x10
#define REG_EXTCAP_MANTISSA2				0x14
#define REG_EXTCAP_MANTISSA3				0x18
#define REG_EXTCAP_MANTISSA4				0x1c
#define REG_EXTCAP_MANTISSA5				0x20
#define REG_EXTCAP_MANTISSA6				0x24

#define REG_EXTCAP_DBC_CAPABILITY			0x00
#define REG_EXTCAP_DBC_DOORBELL				0x04
#define REG_EXTCAP_DBC_ERSTSIZE				0x08
#define REG_EXTCAP_DBC_ERST_LOW				0x10
#define REG_EXTCAP_DBC_ERST_HIGH			0x14
#define REG_EXTCAP_DBC_ERDP_LOW				0x18
#define REG_EXTCAP_DBC_ERDP_HIGH			0x1c
#define REG_EXTCAP_DBC_CONTROL				0x20
#define REG_EXTCAP_DBC_STATUS				0x24
#define REG_EXTCAP_DBC_PORTSC				0x28
#define REG_EXTCAP_DBC_CONT_LOW				0x30
#define REG_EXTCAP_DBC_CONT_HIGH			0x34
#define REG_EXTCAP_DBC_DEVINFO1				0x38
#define REG_EXTCAP_DBC_DEVINFO2				0x3c

#define dump_register(nm)				\
{							\
	.name	= __stringify(nm),			\
	.offset	= REG_ ##nm,				\
}

struct xhci_regset {
	char			name[DEBUGFS_NAMELEN];
	struct debugfs_regset32	regset;
	size_t			nregs;
	struct dentry		*parent;
	struct list_head	list;
};

struct xhci_file_map {
	const char		*name;
	int			(*show)(struct seq_file *s, void *unused);
};

struct xhci_ep_priv {
	char			name[DEBUGFS_NAMELEN];
	struct dentry		*root;
};

struct xhci_slot_priv {
	char			name[DEBUGFS_NAMELEN];
	struct dentry		*root;
	struct xhci_ep_priv	*eps[31];
	struct xhci_virt_device	*dev;
};

#ifdef CONFIG_DEBUG_FS
void xhci_debugfs_init(struct xhci_hcd *xhci);
void xhci_debugfs_exit(struct xhci_hcd *xhci);
void __init xhci_debugfs_create_root(void);
void __exit xhci_debugfs_remove_root(void);
void xhci_debugfs_create_slot(struct xhci_hcd *xhci, int slot_id);
void xhci_debugfs_remove_slot(struct xhci_hcd *xhci, int slot_id);
void xhci_debugfs_create_endpoint(struct xhci_hcd *xhci,
				  struct xhci_virt_device *virt_dev,
				  int ep_index);
void xhci_debugfs_remove_endpoint(struct xhci_hcd *xhci,
				  struct xhci_virt_device *virt_dev,
				  int ep_index);
#else
static inline void xhci_debugfs_init(struct xhci_hcd *xhci) { }
static inline void xhci_debugfs_exit(struct xhci_hcd *xhci) { }
static inline void __init xhci_debugfs_create_root(void) { }
static inline void __exit xhci_debugfs_remove_root(void) { }
static inline void xhci_debugfs_create_slot(struct xhci_hcd *x, int s) { }
static inline void xhci_debugfs_remove_slot(struct xhci_hcd *x, int s) { }
static inline void
xhci_debugfs_create_endpoint(struct xhci_hcd *xhci,
			     struct xhci_virt_device *virt_dev,
			     int ep_index) { }
static inline void
xhci_debugfs_remove_endpoint(struct xhci_hcd *xhci,
			     struct xhci_virt_device *virt_dev,
			     int ep_index) { }
#endif /* CONFIG_DEBUG_FS */

#endif /* __LINUX_XHCI_DEBUGFS_H */
