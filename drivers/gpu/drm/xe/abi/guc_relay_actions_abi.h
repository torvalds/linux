/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _ABI_GUC_RELAY_ACTIONS_ABI_H_
#define _ABI_GUC_RELAY_ACTIONS_ABI_H_

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
