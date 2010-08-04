/***************************************************************************
 * Copyright (c) 2005-2009, Broadcom Corporation.
 *
 *  Name: crystalhd_lnx . c
 *
 *  Description:
 *		BCM70012 Linux driver
 *
 *  HISTORY:
 *
 **********************************************************************
 * This file is part of the crystalhd device driver.
 *
 * This driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * This driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this driver.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/

#ifndef _CRYSTALHD_LNX_H_
#define _CRYSTALHD_LNX_H_

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>

#include <linux/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <linux/uaccess.h>

#include "crystalhd_cmds.h"

#define CRYSTAL_HD_NAME		"Broadcom Crystal HD Decoder (BCM70012) Driver"


/* OS specific PCI information structure and adapter information. */
struct crystalhd_adp {
	/* Hardware borad/PCI specifics */
	char			name[32];
	struct pci_dev		*pdev;

	unsigned long		pci_mem_start;
	uint32_t		pci_mem_len;
	void			*addr;

	unsigned long		pci_i2o_start;
	uint32_t		pci_i2o_len;
	void			*i2o_addr;

	unsigned int		drv_data;
	unsigned int		dmabits;	/* 32 | 64 */
	unsigned int		registered;
	unsigned int		present;
	unsigned int		msi;

	spinlock_t		lock;

	/* API Related */
	unsigned int		chd_dec_major;
	unsigned int		cfg_users;

	struct crystalhd_ioctl_data	*idata_free_head;	/* ioctl data pool */
	struct crystalhd_elem		*elem_pool_head;	/* Queue element pool */

	struct crystalhd_cmd	cmds;

	struct crystalhd_dio_req	*ua_map_free_head;
	struct pci_pool		*fill_byte_pool;
};


struct crystalhd_adp *chd_get_adp(void);
void chd_set_log_level(struct crystalhd_adp *adp, char *arg);

#endif

