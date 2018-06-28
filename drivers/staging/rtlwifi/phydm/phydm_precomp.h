/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __ODM_PRECOMP_H__
#define __ODM_PRECOMP_H__

#include "phydm_types.h"

/* 2 Config Flags and Structs - defined by each ODM type */

#include "../wifi.h"
#include "rtl_phydm.h"

/* 2 OutSrc Header Files */

#include "phydm.h"
#include "phydm_hwconfig.h"
#include "phydm_debug.h"
#include "phydm_regdefine11ac.h"
#include "phydm_regdefine11n.h"
#include "phydm_interface.h"
#include "phydm_reg.h"

#include "phydm_adc_sampling.h"

/* JJ ADD 20161014 */

#include "../halmac/halmac_reg2.h"

#define LDPC_HT_ENABLE_RX BIT(0)
#define LDPC_HT_ENABLE_TX BIT(1)
#define LDPC_HT_TEST_TX_ENABLE BIT(2)
#define LDPC_HT_CAP_TX BIT(3)

#define STBC_HT_ENABLE_RX BIT(0)
#define STBC_HT_ENABLE_TX BIT(1)
#define STBC_HT_TEST_TX_ENABLE BIT(2)
#define STBC_HT_CAP_TX BIT(3)

#define LDPC_VHT_ENABLE_RX BIT(0)
#define LDPC_VHT_ENABLE_TX BIT(1)
#define LDPC_VHT_TEST_TX_ENABLE BIT(2)
#define LDPC_VHT_CAP_TX BIT(3)

#define STBC_VHT_ENABLE_RX BIT(0)
#define STBC_VHT_ENABLE_TX BIT(1)
#define STBC_VHT_TEST_TX_ENABLE BIT(2)
#define STBC_VHT_CAP_TX BIT(3)

#include "rtl8822b/halhwimg8822b_mac.h"
#include "rtl8822b/halhwimg8822b_rf.h"
#include "rtl8822b/halhwimg8822b_bb.h"
#include "rtl8822b/phydm_regconfig8822b.h"
#include "rtl8822b/halphyrf_8822b.h"
#include "rtl8822b/phydm_rtl8822b.h"
#include "rtl8822b/phydm_hal_api8822b.h"
#include "rtl8822b/version_rtl8822b.h"

#include "../halmac/halmac_reg_8822b.h"

/* JJ ADD 20161014 */

#endif /* __ODM_PRECOMP_H__ */
