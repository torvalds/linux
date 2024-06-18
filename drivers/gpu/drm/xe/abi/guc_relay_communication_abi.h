/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _ABI_GUC_RELAY_COMMUNICATION_ABI_H
#define _ABI_GUC_RELAY_COMMUNICATION_ABI_H

#include <linux/build_bug.h>

#include "guc_actions_sriov_abi.h"
#include "guc_communication_ctb_abi.h"
#include "guc_messages_abi.h"

/**
 * DOC: GuC Relay Communication
 *
 * The communication between Virtual Function (VF) drivers and Physical Function
 * (PF) drivers is based on the GuC firmware acting as a proxy (relay) agent.
 *
 * To communicate with the PF driver, VF's drivers use `VF2GUC_RELAY_TO_PF`_
 * action that takes the `Relay Message`_ as opaque payload and requires the
 * relay message identifier (RID) as additional parameter.
 *
 * This identifier is used by the drivers to match related messages.
 *
 * The GuC forwards this `Relay Message`_ and its identifier to the PF driver
 * in `GUC2PF_RELAY_FROM_VF`_ action. This event message additionally contains
 * the identifier of the origin VF (VFID).
 *
 * Likewise, to communicate with the VF drivers, PF driver use
 * `VF2GUC_RELAY_TO_PF`_ action that in addition to the `Relay Message`_
 * and the relay message identifier (RID) also takes the target VF identifier.
 *
 * The GuC uses this target VFID from the message to select where to send the
 * `GUC2VF_RELAY_FROM_PF`_ with the embedded `Relay Message`_ with response::
 *
 *      VF                             GuC                              PF
 *      |                               |                               |
 *     [ ] VF2GUC_RELAY_TO_PF           |                               |
 *     [ ]---------------------------> [ ]                              |
 *     [ ] { rid, msg }                [ ]                              |
 *     [ ]                             [ ] GUC2PF_RELAY_FROM_VF         |
 *     [ ]                             [ ]---------------------------> [ ]
 *     [ ]                              |  { VFID, rid, msg }          [ ]
 *     [ ]                              |                              [ ]
 *     [ ]                              |           PF2GUC_RELAY_TO_VF [ ]
 *     [ ]                             [ ] <---------------------------[ ]
 *     [ ]                             [ ]        { VFID, rid, reply }  |
 *     [ ]        GUC2VF_RELAY_FROM_PF [ ]                              |
 *     [ ] <---------------------------[ ]                              |
 *      |               { rid, reply }  |                               |
 *      |                               |                               |
 *
 * It is also possible that PF driver will initiate communication with the
 * selected VF driver. The same GuC action messages will be used::
 *
 *      VF                             GuC                              PF
 *      |                               |                               |
 *      |                               |           PF2GUC_RELAY_TO_VF [ ]
 *      |                              [ ] <---------------------------[ ]
 *      |                              [ ]          { VFID, rid, msg } [ ]
 *      |         GUC2VF_RELAY_FROM_PF [ ]                             [ ]
 *     [ ] <---------------------------[ ]                             [ ]
 *     [ ]                { rid, msg }  |                              [ ]
 *     [ ]                              |                              [ ]
 *     [ ] VF2GUC_RELAY_TO_PF           |                              [ ]
 *     [ ]---------------------------> [ ]                             [ ]
 *      |  { rid, reply }              [ ]                             [ ]
 *      |                              [ ] GUC2PF_RELAY_FROM_VF        [ ]
 *      |                              [ ]---------------------------> [ ]
 *      |                               | { VFID, rid, reply }          |
 *      |                               |                               |
 */

/**
 * DOC: Relay Message
 *
 * The `Relay Message`_ is used by Physical Function (PF) driver and Virtual
 * Function (VF) drivers to communicate using `GuC Relay Communication`_.
 *
 * Format of the `Relay Message`_ follows format of the generic `HXG Message`_.
 *
 *  +--------------------------------------------------------------------------+
 *  |  `Relay Message`_                                                        |
 *  +==========================================================================+
 *  |  `HXG Message`_                                                          |
 *  +--------------------------------------------------------------------------+
 *
 * Maximum length of the `Relay Message`_ is limited by the maximum length of
 * the `CTB HXG Message`_ and format of the `GUC2PF_RELAY_FROM_VF`_ message.
 */

#define GUC_RELAY_MSG_MIN_LEN GUC_HXG_MSG_MIN_LEN
#define GUC_RELAY_MSG_MAX_LEN \
	(GUC_CTB_MAX_DWORDS - GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN)

static_assert(PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN >
	      VF2GUC_RELAY_TO_PF_REQUEST_MSG_MIN_LEN);

/**
 * DOC: Relay Error Codes
 *
 * The `GuC Relay Communication`_ can be used to pass `Relay Message`_ between
 * drivers that run on different Operating Systems. To help in troubleshooting,
 * `GuC Relay Communication`_ uses error codes that mostly match errno values.
 */

#define GUC_RELAY_ERROR_UNDISCLOSED			0
#define GUC_RELAY_ERROR_OPERATION_NOT_PERMITTED		1	/* EPERM */
#define GUC_RELAY_ERROR_PERMISSION_DENIED		13	/* EACCES */
#define GUC_RELAY_ERROR_INVALID_ARGUMENT		22	/* EINVAL */
#define GUC_RELAY_ERROR_INVALID_REQUEST_CODE		56	/* EBADRQC */
#define GUC_RELAY_ERROR_NO_DATA_AVAILABLE		61	/* ENODATA */
#define GUC_RELAY_ERROR_PROTOCOL_ERROR			71	/* EPROTO */
#define GUC_RELAY_ERROR_MESSAGE_SIZE			90	/* EMSGSIZE */

#endif
