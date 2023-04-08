/*
 *
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: $
 *
 */

#ifndef _dhd_bt_interface_h_
#define _dhd_bt_interface_h_
#include <linux/mmc/sdio_func.h>

typedef enum {
	WLAN_MODULE = 0,
	BT_MODULE
} bus_owner_t;

typedef  void * wlan_bt_handle_t;
typedef void (*f3intr_handler)(struct sdio_func *func);
typedef void (*dhd_hang_notification)(struct sdio_func *func, bool wifi_state);

extern void bcmsdh_btsdio_interface_init(struct sdio_func *func,
	f3intr_handler f3intr_fun, dhd_hang_notification hang_notification);
void bcmsdh_btsdio_process_f3_intr(void);
void bcmsdh_btsdio_process_dhd_hang_notification(bool wifi_recovery_completed);

extern int dhd_bus_recv_buf(void *h, uint32 addr, uint fn, uint8 *buf, uint nbytes);
extern int dhd_bus_send_buf(void *h, uint32 addr, uint fn, uint8 *buf, uint nbytes);
extern int dhd_bus_set_blocksize(void *h, unsigned int fun_num, unsigned int block_size);

/* Shared Layer Init function */
extern wlan_bt_handle_t dhd_bt_get_pub_hndl(void);
extern int dhd_download_btfw(wlan_bt_handle_t handle, char* btfw_path);
extern int dhd_bus_get(wlan_bt_handle_t handle, bus_owner_t owner);
extern int dhd_bus_put(wlan_bt_handle_t handle, bus_owner_t owner);

extern unsigned char dhd_bus_cfg_read(void *h, unsigned int fun_num, unsigned int addr, int *err);
extern void dhd_bus_cfg_write(void *h, unsigned int fun_num, unsigned int addr,
	unsigned char val, int *err);

/*
 * Functions to be called from other layers to enable/disable Bus clock
 * can_wait - Callers pass TRUE, if they want & can wait until the
 * clock configuration takes effect (there is a register poll until the
 * PLLs are locked). If the caller cannot wait they can simply pass
 * FALSE.
 */
extern int dhd_bus_clk_enable(wlan_bt_handle_t handle, bus_owner_t owner);
extern int dhd_bus_clk_disable(wlan_bt_handle_t handle, bus_owner_t owner);
extern void dhd_bus_reset_bt_use_count(wlan_bt_handle_t handle);
extern void dhd_bus_retry_hang_recovery(wlan_bt_handle_t handle);
#endif /* _dhd_bt_interface_h_ */
