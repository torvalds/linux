/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#ifndef _RTL_PCI_H
#define _RTL_PCI_H

#include <linux/types.h>
#include <linux/pci.h>

struct net_device;

bool rtl92e_check_adapter(struct pci_dev *pdev, struct net_device *dev);

#endif
