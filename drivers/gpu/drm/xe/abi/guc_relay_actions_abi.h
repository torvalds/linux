/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _ABI_GUC_RELAY_ACTIONS_ABI_H_
#define _ABI_GUC_RELAY_ACTIONS_ABI_H_

#include "abi/guc_relay_communication_abi.h"

/**
 * DOC: GuC Relay VF/PF ABI Version
 *
 * The _`GUC_RELAY_VERSION_BASE` defines minimum VF/PF ABI version that
 * drivers must support. Currently this is version 1.0.
 *
 * The _`GUC_RELAY_VERSION_LATEST` defines latest VF/PF ABI version that
 * drivers may use. Currently this is version 1.0.
 *
 * Some platforms may require different base VF/PF ABI version.
 * No supported VF/PF ABI version can be 0.0.
 */

#define GUC_RELAY_VERSION_BASE_MAJOR			1
#define GUC_RELAY_VERSION_BASE_MINOR			0

#define GUC_RELAY_VERSION_LATEST_MAJOR			1
#define GUC_RELAY_VERSION_LATEST_MINOR			0

/**
 * DOC: GuC Relay Actions
 *
 * The following actions are supported from VF/PF ABI version 1.0:
 *
 *  * `VF2PF_HANDSHAKE`_
 *  * `VF2PF_QUERY_RUNTIME`_
 */

/**
 * DOC: VF2PF_HANDSHAKE
 *
 * This `Relay Message`_ is used by the VF to establish ABI version with the PF.
 *
 * Prior to exchanging any other messages, both VF driver and PF driver must
 * negotiate the VF/PF ABI version that will be used in their communication.
 *
 * The VF driver shall use @MAJOR and @MINOR fields to pass requested ABI version.
 * The VF driver may use special version 0.0 (both @MAJOR and @MINOR set to 0)
 * to request latest (or any) ABI version that is supported by the PF driver.
 *
 * This message definition shall be supported by all future ABI versions.
 * This message definition shall not be changed by future ABI versions.
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
 *  |   |  15:0 | ACTION = _`GUC_RELAY_ACTION_VF2PF_HANDSHAKE` = 0x0001        |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 | 31:16 | **MAJOR** - requested major version of the VFPF interface    |
 *  |   |       | (use MAJOR_ANY to request latest version supported by PF)    |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | **MINOR** - requested minor version of the VFPF interface    |
 *  |   |       | (use MINOR_ANY to request latest version supported by PF)    |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | DATA0 = MBZ                                                  |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 | 31:16 | **MAJOR** - agreed major version of the VFPF interface       |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | **MINOR** - agreed minor version of the VFPF interface       |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_RELAY_ACTION_VF2PF_HANDSHAKE		0x0001u

#define VF2PF_HANDSHAKE_REQUEST_MSG_LEN			2u
#define VF2PF_HANDSHAKE_REQUEST_MSG_0_MBZ		GUC_HXG_REQUEST_MSG_0_DATA0
#define VF2PF_HANDSHAKE_REQUEST_MSG_1_MAJOR		(0xffffu << 16)
#define   VF2PF_HANDSHAKE_MAJOR_ANY			0
#define VF2PF_HANDSHAKE_REQUEST_MSG_1_MINOR		(0xffffu << 0)
#define   VF2PF_HANDSHAKE_MINOR_ANY			0

#define VF2PF_HANDSHAKE_RESPONSE_MSG_LEN		2u
#define VF2PF_HANDSHAKE_RESPONSE_MSG_0_MBZ		GUC_HXG_RESPONSE_MSG_0_DATA0
#define VF2PF_HANDSHAKE_RESPONSE_MSG_1_MAJOR		(0xffffu << 16)
#define VF2PF_HANDSHAKE_RESPONSE_MSG_1_MINOR		(0xffffu << 0)

/**
 * DOC: VF2PF_QUERY_RUNTIME
 *
 * This `Relay Message`_ is used by the VF to query values of runtime registers.
 *
 * On some platforms, VF drivers may not have access to the some fuse registers
 * (referred here as 'runtime registers') and therefore VF drivers need to ask
 * the PF driver to obtain their values.
 *
 * However, the list of such registers, and their values, is fully owned and
 * maintained by the PF driver and the VF driver may only initiate the query
 * sequence and indicate in the @START field the starting index of the next
 * requested register from this predefined list.
 *
 * In the response, the PF driver will return tuple of 32-bit register offset and
 * the 32-bit value of that register (respectively @REG_OFFSET and @REG_VALUE).
 *
 * The VF driver can use @LIMIT field to limit number of returned register tuples.
 * If @LIMIT is unset then PF decides about number of returned register tuples.
 *
 * This message definition is supported from ABI version 1.0.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | DATA0 = **LIMIT** - limit number of returned entries         |
 *  |   |       | (use zero to not enforce any limits on the response)         |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_RELAY_ACTION_VF2PF_QUERY_RUNTIME` = 0x0101    |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | DATA1 = **START** - index of the first requested entry       |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | DATA0 = **COUNT** - number of entries included in response   |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | DATA1 = **REMAINING** - number of remaining entries          |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | DATA2 = **REG_OFFSET** - offset of register[START]           |
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | DATA3 = **REG_VALUE** - value of register[START]             |
 *  +---+-------+--------------------------------------------------------------+
 *  |   |       |                                                              |
 *  +---+-------+--------------------------------------------------------------+
 *  |n-1|  31:0 | REG_OFFSET - offset of register[START + x]                   |
 *  +---+-------+--------------------------------------------------------------+
 *  | n |  31:0 | REG_VALUE - value of register[START + x]                     |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_RELAY_ACTION_VF2PF_QUERY_RUNTIME		0x0101u

#define VF2PF_QUERY_RUNTIME_REQUEST_MSG_LEN		2u
#define VF2PF_QUERY_RUNTIME_REQUEST_MSG_0_LIMIT		GUC_HXG_REQUEST_MSG_0_DATA0
#define   VF2PF_QUERY_RUNTIME_NO_LIMIT			0u
#define VF2PF_QUERY_RUNTIME_REQUEST_MSG_1_START		GUC_HXG_REQUEST_MSG_n_DATAn

#define VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN	(GUC_HXG_MSG_MIN_LEN + 1u)
#define VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MAX_LEN	\
		(VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN + VF2PF_QUERY_RUNTIME_MAX_COUNT * 2)
#define VF2PF_QUERY_RUNTIME_RESPONSE_MSG_0_COUNT	GUC_HXG_RESPONSE_MSG_0_DATA0
#define   VF2PF_QUERY_RUNTIME_MIN_COUNT			0
#define   VF2PF_QUERY_RUNTIME_MAX_COUNT			\
		((GUC_RELAY_MSG_MAX_LEN - VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN) / 2)
#define VF2PF_QUERY_RUNTIME_RESPONSE_MSG_1_REMAINING	GUC_HXG_RESPONSE_MSG_n_DATAn
#define VF2PF_QUERY_RUNTIME_RESPONSE_DATAn_REG_OFFSETx	GUC_HXG_RESPONSE_MSG_n_DATAn
#define VF2PF_QUERY_RUNTIME_RESPONSE_DATAn_REG_VALUEx	GUC_HXG_RESPONSE_MSG_n_DATAn

/**
 * DOC: GuC Relay Debug Actions
 *
 * This range of action codes is reserved for debugging purposes only and should
 * be used only on debug builds. These actions may not be supported by the
 * production drivers. Their definitions could be changed in the future.
 *
 *  _`GUC_RELAY_ACTION_DEBUG_ONLY_START` = 0xDEB0
 *  _`GUC_RELAY_ACTION_DEBUG_ONLY_END` = 0xDEFF
 */

#define GUC_RELAY_ACTION_DEBUG_ONLY_START	0xDEB0
#define GUC_RELAY_ACTION_DEBUG_ONLY_END		0xDEFF

/**
 * DOC: VFXPF_TESTLOOP
 *
 * This `Relay Message`_ is used to selftest the `GuC Relay Communication`_.
 *
 * The following opcodes are defined:
 * VFXPF_TESTLOOP_OPCODE_NOP_ will return no data.
 * VFXPF_TESTLOOP_OPCODE_BUSY_ will reply with BUSY response first.
 * VFXPF_TESTLOOP_OPCODE_RETRY_ will reply with RETRY response instead.
 * VFXPF_TESTLOOP_OPCODE_ECHO_ will return same data as received.
 * VFXPF_TESTLOOP_OPCODE_FAIL_ will always fail with error.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_ or GUC_HXG_TYPE_FAST_REQUEST_   |
 *  |   |       | or GUC_HXG_TYPE_EVENT_                                       |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | **OPCODE**                                                   |
 *  |   |       |    - _`VFXPF_TESTLOOP_OPCODE_NOP` = 0x0                      |
 *  |   |       |    - _`VFXPF_TESTLOOP_OPCODE_BUSY` = 0xB                     |
 *  |   |       |    - _`VFXPF_TESTLOOP_OPCODE_RETRY` = 0xD                    |
 *  |   |       |    - _`VFXPF_TESTLOOP_OPCODE_ECHO` = 0xE                     |
 *  |   |       |    - _`VFXPF_TESTLOOP_OPCODE_FAIL` = 0xF                     |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`IOV_ACTION_SELFTEST_RELAY`                        |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **DATA1** = optional, depends on **OPCODE**:                 |
 *  |   |       | for VFXPF_TESTLOOP_OPCODE_BUSY_: time in ms for reply        |
 *  |   |       | for VFXPF_TESTLOOP_OPCODE_FAIL_: expected error              |
 *  |   |       | for VFXPF_TESTLOOP_OPCODE_ECHO_: payload                     |
 *  +---+-------+--------------------------------------------------------------+
 *  |...|  31:0 | **DATAn** = only for **OPCODE** VFXPF_TESTLOOP_OPCODE_ECHO_  |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | DATA0 = MBZ                                                  |
 *  +---+-------+--------------------------------------------------------------+
 *  |...|  31:0 | DATAn = only for **OPCODE** VFXPF_TESTLOOP_OPCODE_ECHO_      |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_RELAY_ACTION_VFXPF_TESTLOOP		(GUC_RELAY_ACTION_DEBUG_ONLY_START + 1)
#define   VFXPF_TESTLOOP_OPCODE_NOP		0x0
#define   VFXPF_TESTLOOP_OPCODE_BUSY		0xB
#define   VFXPF_TESTLOOP_OPCODE_RETRY		0xD
#define   VFXPF_TESTLOOP_OPCODE_ECHO		0xE
#define   VFXPF_TESTLOOP_OPCODE_FAIL		0xF

#endif
