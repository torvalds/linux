/*
 * Bluetooth CSR GPIO and Low Power Mode control
 *
 *  Copyright (C) 2011 Samsung, Inc.
 *  Copyright (C) 2011 Google, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#ifndef __BOARD_BLUETOOTH_CSR8811_H__
#define __BOARD_BLUETOOTH_CSR8811_H__

#include <linux/serial_core.h>

extern void csr_bt_lpm_exit_lpm_locked(struct uart_port *uport);

#endif /*  __BOARD_BLUETOOTH_BCM4334_H__  */
