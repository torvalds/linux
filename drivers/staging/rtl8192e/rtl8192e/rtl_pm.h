/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/

#ifndef R8192E_PM_H
#define R8192E_PM_H

#include <linux/types.h>
#include <linux/pci.h>

int rtl92e_suspend(struct pci_dev *dev, pm_message_t state);
int rtl92e_resume(struct pci_dev *dev);

#endif
