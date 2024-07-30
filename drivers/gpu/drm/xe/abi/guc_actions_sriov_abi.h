/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _ABI_GUC_ACTIONS_SRIOV_ABI_H
#define _ABI_GUC_ACTIONS_SRIOV_ABI_H

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

/**
 * DOC: GUC2PF_ADVERSE_EVENT
 *
 * This message is used by the GuC to notify PF about adverse events.
 *
 * This G2H message must be sent as `CTB HXG Message`_.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_EVENT_                                   |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | DATA0 = MBZ                                                  |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_GUC2PF_ADVERSE_EVENT` = 0x5104         |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | DATA1 = **VFID** - VF identifier                             |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | DATA2 = **THRESHOLD** - key of the exceeded threshold        |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_GUC2PF_ADVERSE_EVENT			0x5104

#define GUC2PF_ADVERSE_EVENT_EVENT_MSG_LEN		(GUC_HXG_EVENT_MSG_MIN_LEN + 2u)
#define GUC2PF_ADVERSE_EVENT_EVENT_MSG_0_MBZ		GUC_HXG_EVENT_MSG_0_DATA0
#define GUC2PF_ADVERSE_EVENT_EVENT_MSG_1_VFID		GUC_HXG_EVENT_MSG_n_DATAn
#define GUC2PF_ADVERSE_EVENT_EVENT_MSG_2_THRESHOLD	GUC_HXG_EVENT_MSG_n_DATAn

/**
 * DOC: GUC2PF_VF_STATE_NOTIFY
 *
 * The GUC2PF_VF_STATE_NOTIFY message is used by the GuC to notify PF about change
 * of the VF state.
 *
 * This G2H message is sent as `CTB HXG Message`_.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_EVENT_                                   |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | DATA0 = MBZ                                                  |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_GUC2PF_VF_STATE_NOTIFY` = 0x5106       |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | DATA1 = **VFID** - VF identifier                             |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | DATA2 = **EVENT** - notification event:                      |
 *  |   |       |                                                              |
 *  |   |       |   - _`GUC_PF_NOTIFY_VF_ENABLE` = 1 (only if VFID = 0)        |
 *  |   |       |   - _`GUC_PF_NOTIFY_VF_FLR` = 1                              |
 *  |   |       |   - _`GUC_PF_NOTIFY_VF_FLR_DONE` = 2                         |
 *  |   |       |   - _`GUC_PF_NOTIFY_VF_PAUSE_DONE` = 3                       |
 *  |   |       |   - _`GUC_PF_NOTIFY_VF_FIXUP_DONE` = 4                       |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_GUC2PF_VF_STATE_NOTIFY		0x5106u

#define GUC2PF_VF_STATE_NOTIFY_EVENT_MSG_LEN		(GUC_HXG_EVENT_MSG_MIN_LEN + 2u)
#define GUC2PF_VF_STATE_NOTIFY_EVENT_MSG_0_MBZ		GUC_HXG_EVENT_MSG_0_DATA0
#define GUC2PF_VF_STATE_NOTIFY_EVENT_MSG_1_VFID		GUC_HXG_EVENT_MSG_n_DATAn
#define GUC2PF_VF_STATE_NOTIFY_EVENT_MSG_2_EVENT	GUC_HXG_EVENT_MSG_n_DATAn
#define   GUC_PF_NOTIFY_VF_ENABLE			1u
#define   GUC_PF_NOTIFY_VF_FLR				1u
#define   GUC_PF_NOTIFY_VF_FLR_DONE			2u
#define   GUC_PF_NOTIFY_VF_PAUSE_DONE			3u
#define   GUC_PF_NOTIFY_VF_FIXUP_DONE			4u

/**
 * DOC: VF2GUC_MATCH_VERSION
 *
 * This action is used to match VF interface version used by VF and GuC.
 *
 * This message must be sent as `MMIO HXG Message`_.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | DATA0 = MBZ                                                  |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_VF2GUC_MATCH_VERSION` = 0x5500         |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 | 31:24 | **BRANCH** - branch ID of the VF interface                   |
 *  |   |       | (use BRANCH_ANY to request latest version supported by GuC)  |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 23:16 | **MAJOR** - major version of the VF interface                |
 *  |   |       | (use MAJOR_ANY to request latest version supported by GuC)   |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:8 | **MINOR** - minor version of the VF interface                |
 *  |   |       | (use MINOR_ANY to request latest version supported by GuC)   |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |   7:0 | **MBZ**                                                      |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | DATA0 = MBZ                                                  |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 | 31:24 | **BRANCH** - branch ID of the VF interface                   |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 23:16 | **MAJOR** - major version of the VF interface                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:8 | **MINOR** - minor version of the VF interface                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |   7:0 | **PATCH** - patch version of the VF interface                |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_VF2GUC_MATCH_VERSION			0x5500u

#define VF2GUC_MATCH_VERSION_REQUEST_MSG_LEN		(GUC_HXG_REQUEST_MSG_MIN_LEN + 1u)
#define VF2GUC_MATCH_VERSION_REQUEST_MSG_0_MBZ		GUC_HXG_REQUEST_MSG_0_DATA0
#define VF2GUC_MATCH_VERSION_REQUEST_MSG_1_BRANCH	(0xffu << 24)
#define   GUC_VERSION_BRANCH_ANY			0
#define VF2GUC_MATCH_VERSION_REQUEST_MSG_1_MAJOR	(0xffu << 16)
#define   GUC_VERSION_MAJOR_ANY				0
#define VF2GUC_MATCH_VERSION_REQUEST_MSG_1_MINOR	(0xffu << 8)
#define   GUC_VERSION_MINOR_ANY				0
#define VF2GUC_MATCH_VERSION_REQUEST_MSG_1_MBZ		(0xffu << 0)

#define VF2GUC_MATCH_VERSION_RESPONSE_MSG_LEN		(GUC_HXG_RESPONSE_MSG_MIN_LEN + 1u)
#define VF2GUC_MATCH_VERSION_RESPONSE_MSG_0_MBZ		GUC_HXG_RESPONSE_MSG_0_DATA0
#define VF2GUC_MATCH_VERSION_RESPONSE_MSG_1_BRANCH	(0xffu << 24)
#define VF2GUC_MATCH_VERSION_RESPONSE_MSG_1_MAJOR	(0xffu << 16)
#define VF2GUC_MATCH_VERSION_RESPONSE_MSG_1_MINOR	(0xffu << 8)
#define VF2GUC_MATCH_VERSION_RESPONSE_MSG_1_PATCH	(0xffu << 0)

/**
 * DOC: PF2GUC_UPDATE_VGT_POLICY
 *
 * This message is used by the PF to set `GuC VGT Policy KLVs`_.
 *
 * This message must be sent as `CTB HXG Message`_.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_PF2GUC_UPDATE_VGT_POLICY` = 0x5502     |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **CFG_ADDR_LO** - dword aligned GGTT offset that             |
 *  |   |       | represents the start of `GuC VGT Policy KLVs`_ list.         |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **CFG_ADDR_HI** - upper 32 bits of above offset.             |
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | **CFG_SIZE** - size (in dwords) of the config buffer         |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | **COUNT** - number of KLVs successfully applied              |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_PF2GUC_UPDATE_VGT_POLICY			0x5502u

#define PF2GUC_UPDATE_VGT_POLICY_REQUEST_MSG_LEN		(GUC_HXG_REQUEST_MSG_MIN_LEN + 3u)
#define PF2GUC_UPDATE_VGT_POLICY_REQUEST_MSG_0_MBZ		GUC_HXG_REQUEST_MSG_0_DATA0
#define PF2GUC_UPDATE_VGT_POLICY_REQUEST_MSG_1_CFG_ADDR_LO	GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_UPDATE_VGT_POLICY_REQUEST_MSG_2_CFG_ADDR_HI	GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_UPDATE_VGT_POLICY_REQUEST_MSG_3_CFG_SIZE		GUC_HXG_REQUEST_MSG_n_DATAn

#define PF2GUC_UPDATE_VGT_POLICY_RESPONSE_MSG_LEN		GUC_HXG_RESPONSE_MSG_MIN_LEN
#define PF2GUC_UPDATE_VGT_POLICY_RESPONSE_MSG_0_COUNT		GUC_HXG_RESPONSE_MSG_0_DATA0

/**
 * DOC: PF2GUC_UPDATE_VF_CFG
 *
 * The `PF2GUC_UPDATE_VF_CFG`_ message is used by PF to provision single VF in GuC.
 *
 * This message must be sent as `CTB HXG Message`_.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_PF2GUC_UPDATE_VF_CFG` = 0x5503         |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **VFID** - identifier of the VF that the KLV                 |
 *  |   |       | configurations are being applied to                          |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **CFG_ADDR_LO** - dword aligned GGTT offset that represents  |
 *  |   |       | the start of a list of virtualization related KLV configs    |
 *  |   |       | that are to be applied to the VF.                            |
 *  |   |       | If this parameter is zero, the list is not parsed.           |
 *  |   |       | If full configs address parameter is zero and configs_size is|
 *  |   |       | zero associated VF config shall be reset to its default state|
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | **CFG_ADDR_HI** - upper 32 bits of configs address.          |
 *  +---+-------+--------------------------------------------------------------+
 *  | 4 |  31:0 | **CFG_SIZE** - size (in dwords) of the config buffer         |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | **COUNT** - number of KLVs successfully applied              |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_PF2GUC_UPDATE_VF_CFG			0x5503u

#define PF2GUC_UPDATE_VF_CFG_REQUEST_MSG_LEN		(GUC_HXG_REQUEST_MSG_MIN_LEN + 4u)
#define PF2GUC_UPDATE_VF_CFG_REQUEST_MSG_0_MBZ		GUC_HXG_REQUEST_MSG_0_DATA0
#define PF2GUC_UPDATE_VF_CFG_REQUEST_MSG_1_VFID		GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_UPDATE_VF_CFG_REQUEST_MSG_2_CFG_ADDR_LO	GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_UPDATE_VF_CFG_REQUEST_MSG_3_CFG_ADDR_HI	GUC_HXG_REQUEST_MSG_n_DATAn
#define PF2GUC_UPDATE_VF_CFG_REQUEST_MSG_4_CFG_SIZE	GUC_HXG_REQUEST_MSG_n_DATAn

#define PF2GUC_UPDATE_VF_CFG_RESPONSE_MSG_LEN		GUC_HXG_RESPONSE_MSG_MIN_LEN
#define PF2GUC_UPDATE_VF_CFG_RESPONSE_MSG_0_COUNT	GUC_HXG_RESPONSE_MSG_0_DATA0

/**
 * DOC: PF2GUC_VF_CONTROL
 *
 * The PF2GUC_VF_CONTROL message is used by the PF to trigger VF state change
 * maintained by the GuC.
 *
 * This H2G message must be sent as `CTB HXG Message`_.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | DATA0 = MBZ                                                  |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_PF2GUC_VF_CONTROL_CMD` = 0x5506        |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | DATA1 = **VFID** - VF identifier                             |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | DATA2 = **COMMAND** - control command:                       |
 *  |   |       |                                                              |
 *  |   |       |   - _`GUC_PF_TRIGGER_VF_PAUSE` = 1                           |
 *  |   |       |   - _`GUC_PF_TRIGGER_VF_RESUME` = 2                          |
 *  |   |       |   - _`GUC_PF_TRIGGER_VF_STOP` = 3                            |
 *  |   |       |   - _`GUC_PF_TRIGGER_VF_FLR_START` = 4                       |
 *  |   |       |   - _`GUC_PF_TRIGGER_VF_FLR_FINISH` = 5                      |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | DATA0 = MBZ                                                  |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_PF2GUC_VF_CONTROL			0x5506u

#define PF2GUC_VF_CONTROL_REQUEST_MSG_LEN		(GUC_HXG_EVENT_MSG_MIN_LEN + 2u)
#define PF2GUC_VF_CONTROL_REQUEST_MSG_0_MBZ		GUC_HXG_EVENT_MSG_0_DATA0
#define PF2GUC_VF_CONTROL_REQUEST_MSG_1_VFID		GUC_HXG_EVENT_MSG_n_DATAn
#define PF2GUC_VF_CONTROL_REQUEST_MSG_2_COMMAND		GUC_HXG_EVENT_MSG_n_DATAn
#define   GUC_PF_TRIGGER_VF_PAUSE			1u
#define   GUC_PF_TRIGGER_VF_RESUME			2u
#define   GUC_PF_TRIGGER_VF_STOP			3u
#define   GUC_PF_TRIGGER_VF_FLR_START			4u
#define   GUC_PF_TRIGGER_VF_FLR_FINISH			5u

/**
 * DOC: VF2GUC_VF_RESET
 *
 * This action is used by VF to reset GuC's VF state.
 *
 * This message must be sent as `MMIO HXG Message`_.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | DATA0 = MBZ                                                  |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_VF2GUC_VF_RESET` = 0x5507              |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | DATA0 = MBZ                                                  |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_VF2GUC_VF_RESET			0x5507u

#define VF2GUC_VF_RESET_REQUEST_MSG_LEN			GUC_HXG_REQUEST_MSG_MIN_LEN
#define VF2GUC_VF_RESET_REQUEST_MSG_0_MBZ		GUC_HXG_REQUEST_MSG_0_DATA0

#define VF2GUC_VF_RESET_RESPONSE_MSG_LEN		GUC_HXG_RESPONSE_MSG_MIN_LEN
#define VF2GUC_VF_RESET_RESPONSE_MSG_0_MBZ		GUC_HXG_RESPONSE_MSG_0_DATA0

/**
 * DOC: VF2GUC_QUERY_SINGLE_KLV
 *
 * This action is used by VF to query value of the single KLV data.
 *
 * This message must be sent as `MMIO HXG Message`_.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_VF2GUC_QUERY_SINGLE_KLV` = 0x5509      |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 | 31:16 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | **KEY** - key for which value is requested                   |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | MBZ                                                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | **LENGTH** - length of data in dwords                        |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **VALUE32** - bits 31:0 of value if **LENGTH** >= 1          |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **VALUE64** - bits 63:32 of value if **LENGTH** >= 2         |
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | **VALUE96** - bits 95:64 of value if **LENGTH** >= 3         |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_VF2GUC_QUERY_SINGLE_KLV		0x5509u

#define VF2GUC_QUERY_SINGLE_KLV_REQUEST_MSG_LEN		(GUC_HXG_REQUEST_MSG_MIN_LEN + 1u)
#define VF2GUC_QUERY_SINGLE_KLV_REQUEST_MSG_0_MBZ	GUC_HXG_REQUEST_MSG_0_DATA0
#define VF2GUC_QUERY_SINGLE_KLV_REQUEST_MSG_1_MBZ	(0xffffu << 16)
#define VF2GUC_QUERY_SINGLE_KLV_REQUEST_MSG_1_KEY	(0xffffu << 0)

#define VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_MIN_LEN	GUC_HXG_RESPONSE_MSG_MIN_LEN
#define VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_MAX_LEN	(GUC_HXG_RESPONSE_MSG_MIN_LEN + 3u)
#define VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_0_MBZ	(0xfffu << 16)
#define VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_0_LENGTH	(0xffffu << 0)
#define VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_1_VALUE32	GUC_HXG_REQUEST_MSG_n_DATAn
#define VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_2_VALUE64	GUC_HXG_REQUEST_MSG_n_DATAn
#define VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_3_VALUE96	GUC_HXG_REQUEST_MSG_n_DATAn

#endif
