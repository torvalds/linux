/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _GUC_ACTIONS_PF_ABI_H
#define _GUC_ACTIONS_PF_ABI_H

#include "guc_communication_ctb_abi.h"

/**
 * DOC: GUC2PF_RELAY_FROM_VF
 *
 * This message is used by the GuC firmware to forward a VF2PF `Relay Message`_
 * received from the Virtual Function (VF) driver to this Physical Function (PF)
 * driver.
 *
 * This message is always sent as `CTB HXG Message`_.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_EVENT_                                   |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`XE_GUC_ACTION_GUC2PF_RELAY_FROM_VF` = 0x5100      |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **VFID** - source VF identifier                              |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **RELAY_ID** - VF/PF message ID                              |
 *  +---+-------+-----------------+--------------------------------------------+
 *  | 3 |  31:0 | **RELAY_DATA1** |                                            |
 *  +---+-------+-----------------+                                            |
 *  |...|       |                 |       [Embedded `Relay Message`_]          |
 *  +---+-------+-----------------+                                            |
 *  | n |  31:0 | **RELAY_DATAx** |                                            |
 *  +---+-------+-----------------+--------------------------------------------+
 */
#define XE_GUC_ACTION_GUC2PF_RELAY_FROM_VF		0x5100

#define GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN		(GUC_HXG_EVENT_MSG_MIN_LEN + 2u)
#define GUC2PF_RELAY_FROM_VF_EVENT_MSG_MAX_LEN \
	(GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN + GUC_RELAY_MSG_MAX_LEN)
#define GUC2PF_RELAY_FROM_VF_EVENT_MSG_0_MBZ		GUC_HXG_EVENT_MSG_0_DATA0
#define GUC2PF_RELAY_FROM_VF_EVENT_MSG_1_VFID		GUC_HXG_EVENT_MSG_n_DATAn
#define GUC2PF_RELAY_FROM_VF_EVENT_MSG_2_RELAY_ID	GUC_HXG_EVENT_MSG_n_DATAn
#define GUC2PF_RELAY_FROM_VF_EVENT_MSG_3_RELAY_DATA1	GUC_HXG_EVENT_MSG_n_DATAn
#define GUC2PF_RELAY_FROM_VF_EVENT_MSG_n_RELAY_DATAx	GUC_HXG_EVENT_MSG_n_DATAn
#define GUC2PF_RELAY_FROM_VF_EVENT_MSG_NUM_RELAY_DATA	GUC_RELAY_MSG_MAX_LEN

/**
 * DOC: PF2GUC_RELAY_TO_VF
 *
 * This H2G message is used by the Physical Function (PF) driver to send embedded
 * VF2PF `Relay Message`_ to the VF.
 *
 * This action message must be sent over CTB as `CTB HXG Message`_.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = `GUC_HXG_TYPE_FAST_REQUEST`_                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`XE_GUC_ACTION_PF2GUC_RELAY_TO_VF` = 0x5101        |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **VFID** - target VF identifier                              |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **RELAY_ID** - VF/PF message ID                              |
 *  +---+-------+-----------------+--------------------------------------------+
 *  | 3 |  31:0 | **RELAY_DATA1** |                                            |
 *  +---+-------+-----------------+                                            |
 *  |...|       |                 |       [Embedded `Relay Message`_]          |
 *  +---+-------+-----------------+                                            |
 *  | n |  31:0 | **RELAY_DATAx** |                                            |
 *  +---+-------+-----------------+--------------------------------------------+
 */
#define XE_GUC_ACTION_PF2GUC_RELAY_TO_VF		0x5101

#define PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN		(GUC_HXG_REQUEST_MSG_MIN_LEN + 2u)
#define PF2GUC_RELAY_TO_VF_REQUEST_MSG_MAX_LEN \
	(PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN + GUC_RELAY_MSG_MAX_LEN)
#define PF2GUC_RELAY_TO_VF_REQUEST_MSG_0_MBZ		GUC_HXG_REQUEST_MSG_0_DATA0
#define PF2GUC_RELAY_TO_VF_REQUEST_MSG_1_VFID		GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_RELAY_TO_VF_REQUEST_MSG_2_RELAY_ID	GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_RELAY_TO_VF_REQUEST_MSG_3_RELAY_DATA1	GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_RELAY_TO_VF_REQUEST_MSG_n_RELAY_DATAx	GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_RELAY_TO_VF_REQUEST_MSG_NUM_RELAY_DATA	GUC_RELAY_MSG_MAX_LEN

/**
 * DOC: GUC2VF_RELAY_FROM_PF
 *
 * This message is used by the GuC firmware to deliver `Relay Message`_ from the
 * Physical Function (PF) driver to this Virtual Function (VF) driver.
 * See `GuC Relay Communication`_ for details.
 *
 * This message is always sent over CTB.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_EVENT_                                   |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`XE_GUC_ACTION_GUC2VF_RELAY_FROM_PF` = 0x5102      |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **RELAY_ID** - VF/PF message ID                              |
 *  +---+-------+-----------------+--------------------------------------------+
 *  | 2 |  31:0 | **RELAY_DATA1** |                                            |
 *  +---+-------+-----------------+                                            |
 *  |...|       |                 |       [Embedded `Relay Message`_]          |
 *  +---+-------+-----------------+                                            |
 *  | n |  31:0 | **RELAY_DATAx** |                                            |
 *  +---+-------+-----------------+--------------------------------------------+
 */
#define XE_GUC_ACTION_GUC2VF_RELAY_FROM_PF		0x5102

#define GUC2VF_RELAY_FROM_PF_EVENT_MSG_MIN_LEN		(GUC_HXG_EVENT_MSG_MIN_LEN + 1u)
#define GUC2VF_RELAY_FROM_PF_EVENT_MSG_MAX_LEN \
	(GUC2VF_RELAY_FROM_PF_EVENT_MSG_MIN_LEN + GUC_RELAY_MSG_MAX_LEN)
#define GUC2VF_RELAY_FROM_PF_EVENT_MSG_0_MBZ		GUC_HXG_EVENT_MSG_0_DATA0
#define GUC2VF_RELAY_FROM_PF_EVENT_MSG_1_RELAY_ID	GUC_HXG_EVENT_MSG_n_DATAn
#define GUC2VF_RELAY_FROM_PF_EVENT_MSG_n_RELAY_DATAx	GUC_HXG_EVENT_MSG_n_DATAn
#define GUC2VF_RELAY_FROM_PF_EVENT_MSG_NUM_RELAY_DATA	GUC_RELAY_MSG_MAX_LEN

/**
 * DOC: VF2GUC_RELAY_TO_PF
 *
 * This message is used by the Virtual Function (VF) drivers to communicate with
 * the Physical Function (PF) driver and send `Relay Message`_ to the PF driver.
 * See `GuC Relay Communication`_ for details.
 *
 * This message must be sent over CTB.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_ or GUC_HXG_TYPE_FAST_REQUEST_   |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`XE_GUC_ACTION_VF2GUC_RELAY_TO_PF` = 0x5103        |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **RELAY_ID** - VF/PF message ID                              |
 *  +---+-------+-----------------+--------------------------------------------+
 *  | 2 |  31:0 | **RELAY_DATA1** |                                            |
 *  +---+-------+-----------------+                                            |
 *  |...|       |                 |       [Embedded `Relay Message`_]          |
 *  +---+-------+-----------------+                                            |
 *  | n |  31:0 | **RELAY_DATAx** |                                            |
 *  +---+-------+-----------------+--------------------------------------------+
 */
#define XE_GUC_ACTION_VF2GUC_RELAY_TO_PF		0x5103

#define VF2GUC_RELAY_TO_PF_REQUEST_MSG_MIN_LEN		(GUC_HXG_REQUEST_MSG_MIN_LEN + 1u)
#define VF2GUC_RELAY_TO_PF_REQUEST_MSG_MAX_LEN \
	(VF2GUC_RELAY_TO_PF_REQUEST_MSG_MIN_LEN + GUC_RELAY_MSG_MAX_LEN)
#define VF2GUC_RELAY_TO_PF_REQUEST_MSG_0_MBZ		GUC_HXG_REQUEST_MSG_0_DATA0
#define VF2GUC_RELAY_TO_PF_REQUEST_MSG_1_RELAY_ID	GUC_HXG_REQUEST_MSG_n_DATAn
#define VF2GUC_RELAY_TO_PF_REQUEST_MSG_n_RELAY_DATAx	GUC_HXG_REQUEST_MSG_n_DATAn
#define VF2GUC_RELAY_TO_PF_REQUEST_MSG_NUM_RELAY_DATA	GUC_RELAY_MSG_MAX_LEN

#endif
