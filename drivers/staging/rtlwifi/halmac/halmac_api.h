/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef _HALMAC_API_H_
#define _HALMAC_API_H_

#define HALMAC_SVN_VER "13348M"

#define HALMAC_MAJOR_VER 0x0001 /* major version, ver_1 for async_api */
/* For halmac_api num change or prototype change, increment prototype version.
 * Otherwise, increase minor version
 */
#define HALMAC_PROTOTYPE_VER 0x0003 /* prototype version */
#define HALMAC_MINOR_VER 0x0005 /* minor version */
#define HALMAC_PATCH_VER 0x0000 /* patch version */

#include "halmac_2_platform.h"
#include "halmac_type.h"

#include "halmac_usb_reg.h"
#include "halmac_sdio_reg.h"

#include "halmac_bit2.h"
#include "halmac_reg2.h"

#include "halmac_tx_desc_nic.h"
#include "halmac_rx_desc_nic.h"
#include "halmac_tx_bd_nic.h"
#include "halmac_rx_bd_nic.h"
#include "halmac_fw_offload_c2h_nic.h"
#include "halmac_fw_offload_h2c_nic.h"
#include "halmac_h2c_extra_info_nic.h"
#include "halmac_original_c2h_nic.h"
#include "halmac_original_h2c_nic.h"

#include "halmac_tx_desc_chip.h"
#include "halmac_rx_desc_chip.h"
#include "halmac_tx_bd_chip.h"
#include "halmac_rx_bd_chip.h"
#include "halmac_88xx/halmac_88xx_cfg.h"

#include "halmac_88xx/halmac_8822b/halmac_8822b_cfg.h"
#include "halmac_reg_8822b.h"
#include "halmac_bit_8822b.h"

enum halmac_ret_status
halmac_init_adapter(void *driver_adapter,
		    struct halmac_platform_api *halmac_platform_api,
		    enum halmac_interface halmac_interface,
		    struct halmac_adapter **pp_halmac_adapter,
		    struct halmac_api **pp_halmac_api);

enum halmac_ret_status
halmac_deinit_adapter(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status halmac_halt_api(struct halmac_adapter *halmac_adapter);

enum halmac_ret_status halmac_get_version(struct halmac_ver *version);

#endif
