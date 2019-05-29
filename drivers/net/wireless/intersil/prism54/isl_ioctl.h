/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2002 Intersil Americas Inc.
 *            (C) 2003 Aurelien Alleaume <slts@free.fr>
 *            (C) 2003 Luis R. Rodriguez <mcgrof@ruslug.rutgers.edu>
 */

#ifndef _ISL_IOCTL_H
#define _ISL_IOCTL_H

#include "islpci_mgt.h"
#include "islpci_dev.h"

#include <net/iw_handler.h>	/* New driver API */

#define SUPPORTED_WIRELESS_EXT                  19

void prism54_mib_init(islpci_private *);

struct iw_statistics *prism54_get_wireless_stats(struct net_device *);
void prism54_update_stats(struct work_struct *);

void prism54_acl_init(struct islpci_acl *);
void prism54_acl_clean(struct islpci_acl *);

void prism54_process_trap(struct work_struct *);

void prism54_wpa_bss_ie_init(islpci_private *priv);
void prism54_wpa_bss_ie_clean(islpci_private *priv);

int prism54_set_mac_address(struct net_device *, void *);

extern const struct iw_handler_def prism54_handler_def;

#endif				/* _ISL_IOCTL_H */
