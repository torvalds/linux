/** @file moal_sdio.h
  *
  * @brief This file contains definitions for SDIO interface.
  * driver.
  *
  * Copyright (C) 2008-2017, Marvell International Ltd.
  *
  * This software file (the "File") is distributed by Marvell International
  * Ltd. under the terms of the GNU General Public License Version 2, June 1991
  * (the "License").  You may use, redistribute and/or modify this File in
  * accordance with the terms and conditions of the License, a copy of which
  * is available by writing to the Free Software Foundation, Inc.,
  * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
  * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
  *
  * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
  * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
  * this warranty disclaimer.
  *
  */
/****************************************************
Change log:
****************************************************/

#ifndef	_MOAL_SDIO_H
#define	_MOAL_SDIO_H

#include        <linux/mmc/sdio.h>
#include        <linux/mmc/sdio_ids.h>
#include        <linux/mmc/sdio_func.h>
#include        <linux/mmc/card.h>
#include        <linux/mmc/host.h>

#include "moal_main.h"

#ifndef BLOCK_MODE
/** Block mode */
#define BLOCK_MODE	1
#endif

#ifndef BYTE_MODE
/** Byte Mode */
#define BYTE_MODE	0
#endif

#ifndef FIXED_ADDRESS
/** Fixed address mode */
#define FIXED_ADDRESS	0
#endif

#define SD8977_V0       0x0
#define SD8977_V1       0x8
#define SD8977_V2       0x9
#define SD8977_V0_FW_NAME "mrvl/sdsd8977_combo.bin"
#define SD8977_V1_FW_NAME "mrvl/sdsd8977_combo_v1.bin"
#define SD8977_V2_FW_NAME "mrvl/sdsd8977_combo_v2.bin"
#define SD8977_WLAN_V2_FW_NAME "mrvl/sd8977_wlan_v2.bin"
#define SD8977_WLAN_V1_FW_NAME "mrvl/sd8977_wlan_v1.bin"
#define SD8977_WLAN_V0_FW_NAME "mrvl/sd8977_wlan.bin"

#ifdef STA_SUPPORT
/** Default firmware name */

#define DEFAULT_FW_NAME	"mrvl/sdsd8977_combo.bin"

#ifndef DEFAULT_FW_NAME
#define DEFAULT_FW_NAME ""
#endif
#endif /* STA_SUPPORT */

#ifdef UAP_SUPPORT
/** Default firmware name */

#define DEFAULT_AP_FW_NAME "mrvl/sdsd8977_combo.bin"
#define DEFAULT_WLAN_FW_NAME "mrvl/sd8977_wlan.bin"

#ifndef DEFAULT_AP_FW_NAME
#define DEFAULT_AP_FW_NAME ""
#endif
#endif /* UAP_SUPPORT */

/** Default firmaware name */

#define DEFAULT_AP_STA_FW_NAME "mrvl/sdsd8977_combo.bin"
#define DEFAULT_WLAN_FW_NAME "mrvl/sd8977_wlan.bin"

#ifndef DEFAULT_AP_STA_FW_NAME
#define DEFAULT_AP_STA_FW_NAME ""
#endif

/********************************************************
		Global Functions
********************************************************/

/** Function to write register */
mlan_status woal_write_reg(moal_handle *handle, t_u32 reg, t_u32 data);
/** Function to read register */
mlan_status woal_read_reg(moal_handle *handle, t_u32 reg, t_u32 *data);
/** Function to write data to IO memory */
mlan_status woal_write_data_sync(moal_handle *handle, mlan_buffer *pmbuf,
				 t_u32 port, t_u32 timeout);
/** Function to read data from IO memory */
mlan_status woal_read_data_sync(moal_handle *handle, mlan_buffer *pmbuf,
				t_u32 port, t_u32 timeout);

/** Register to bus driver function */
mlan_status woal_bus_register(void);
/** Unregister from bus driver function */
void woal_bus_unregister(void);

/** Register device function */
mlan_status woal_register_dev(moal_handle *handle);
/** Unregister device function */
void woal_unregister_dev(moal_handle *handle);

int woal_sdio_set_bus_clock(moal_handle *handle, t_u8 option);

#ifdef SDIO_SUSPEND_RESUME
#ifdef MMC_PM_FUNC_SUSPENDED
/** Notify SDIO bus driver that WLAN is suspended */
void woal_wlan_is_suspended(moal_handle *handle);
#endif
/** SDIO Suspend */
int woal_sdio_suspend(struct device *dev);
/** SDIO Resume */
int woal_sdio_resume(struct device *dev);
#endif /* SDIO_SUSPEND_RESUME */

/** Structure: SDIO MMC card */
struct sdio_mmc_card {
	/** sdio_func structure pointer */
	struct sdio_func *func;
	/** moal_handle structure pointer */
	moal_handle *handle;
	/** saved host clock value */
	unsigned int host_clock;
};

/** cmd52 read write */
int woal_sdio_read_write_cmd52(moal_handle *handle, int func, int reg, int val);

#endif /* _MOAL_SDIO_H */
