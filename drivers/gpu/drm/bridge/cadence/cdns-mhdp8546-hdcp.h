/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cadence MHDP8546 DP bridge driver.
 *
 * Copyright (C) 2020 Cadence Design Systems, Inc.
 *
 */

#ifndef CDNS_MHDP8546_HDCP_H
#define CDNS_MHDP8546_HDCP_H

#include "cdns-mhdp8546-core.h"

#define HDCP_MAX_RECEIVERS 32
#define HDCP_RECEIVER_ID_SIZE_BYTES 5
#define HDCP_STATUS_SIZE         0x5
#define HDCP_PORT_STS_AUTH       0x1
#define HDCP_PORT_STS_LAST_ERR_SHIFT 0x5
#define HDCP_PORT_STS_LAST_ERR_MASK  (0x0F << 5)
#define GET_HDCP_PORT_STS_LAST_ERR(__sts__) \
	(((__sts__) & HDCP_PORT_STS_LAST_ERR_MASK) >> \
	HDCP_PORT_STS_LAST_ERR_SHIFT)

#define HDCP_CONFIG_1_4     BIT(0) /* use HDCP 1.4 only */
#define HDCP_CONFIG_2_2     BIT(1) /* use HDCP 2.2 only */
/* use All HDCP versions */
#define HDCP_CONFIG_ALL     (BIT(0) | BIT(1))
#define HDCP_CONFIG_NONE    0

enum {
	HDCP_GENERAL_SET_LC_128,
	HDCP_SET_SEED,
};

enum {
	HDCP_TRAN_CONFIGURATION,
	HDCP2X_TX_SET_PUBLIC_KEY_PARAMS,
	HDCP2X_TX_SET_DEBUG_RANDOM_NUMBERS,
	HDCP2X_TX_RESPOND_KM,
	HDCP1_TX_SEND_KEYS,
	HDCP1_TX_SEND_RANDOM_AN,
	HDCP_TRAN_STATUS_CHANGE,
	HDCP2X_TX_IS_KM_STORED,
	HDCP2X_TX_STORE_KM,
	HDCP_TRAN_IS_REC_ID_VALID,
	HDCP_TRAN_RESPOND_RECEIVER_ID_VALID,
	HDCP_TRAN_TEST_KEYS,
	HDCP2X_TX_SET_KM_KEY_PARAMS,
	HDCP_NUM_OF_SUPPORTED_MESSAGES
};

enum {
	HDCP_CONTENT_TYPE_0,
	HDCP_CONTENT_TYPE_1,
};

#define DRM_HDCP_CHECK_PERIOD_MS (128 * 16)

#define HDCP_PAIRING_R_ID 5
#define HDCP_PAIRING_M_LEN 16
#define HDCP_KM_LEN 16
#define HDCP_PAIRING_M_EKH 16

struct cdns_hdcp_pairing_data {
	u8 receiver_id[HDCP_PAIRING_R_ID];
	u8 m[HDCP_PAIRING_M_LEN];
	u8 km[HDCP_KM_LEN];
	u8 ekh[HDCP_PAIRING_M_EKH];
};

enum {
	HDCP_TX_2,
	HDCP_TX_1,
	HDCP_TX_BOTH,
};

#define DLP_MODULUS_N 384
#define DLP_E 3

struct cdns_hdcp_tx_public_key_param {
	u8 N[DLP_MODULUS_N];
	u8 E[DLP_E];
};

int cdns_mhdp_hdcp_enable(struct cdns_mhdp_device *mhdp, u8 content_type);
int cdns_mhdp_hdcp_disable(struct cdns_mhdp_device *mhdp);
void cdns_mhdp_hdcp_init(struct cdns_mhdp_device *mhdp);

#endif
