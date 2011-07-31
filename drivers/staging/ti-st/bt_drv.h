/*
 *  Texas Instrument's Bluetooth Driver For Shared Transport.
 *
 *  Bluetooth Driver acts as interface between HCI CORE and
 *  TI Shared Transport Layer.
 *
 *  Copyright (C) 2009 Texas Instruments
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _BT_DRV_H
#define _BT_DRV_H

/* Bluetooth Driver Version */
#define VERSION               "1.0"

/* Defines number of seconds to wait for reg completion
 * callback getting called from ST (in case,registration
 * with ST returns PENDING status)
 */
#define BT_REGISTER_TIMEOUT   msecs_to_jiffies(6000)	/* 6 sec */

/* BT driver's local status */
#define BT_DRV_RUNNING        0
#define BT_ST_REGISTERED      1

/* BT driver operation structure */
struct hci_st {

	/* hci device pointer which binds to bt driver */
	struct hci_dev *hdev;

	/* used locally,to maintain various BT driver status */
	unsigned long flags;

	/* to hold ST registration callback  status */
	char streg_cbdata;

	/* write function pointer of ST driver */
	long (*st_write) (struct sk_buff *);

	/* Wait on comepletion handler needed to synchronize
	 * hci_st_open() and hci_st_registration_completion_cb()
	 * functions.*/
	struct completion wait_for_btdrv_reg_completion;
};

#endif
