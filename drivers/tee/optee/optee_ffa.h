/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2019-2021, Linaro Limited
 */

/*
 * This file is exported by OP-TEE and is kept in sync between secure world
 * and normal world drivers. We're using ARM FF-A 1.0 specification.
 */

#ifndef __OPTEE_FFA_H
#define __OPTEE_FFA_H

#include <linux/arm_ffa.h>

/*
 * Normal world sends requests with FFA_MSG_SEND_DIRECT_REQ and
 * responses are returned with FFA_MSG_SEND_DIRECT_RESP for normal
 * messages.
 *
 * All requests with FFA_MSG_SEND_DIRECT_REQ and FFA_MSG_SEND_DIRECT_RESP
 * are using the AArch32 SMC calling convention with register usage as
 * defined in FF-A specification:
 * w0:    Function ID (0x8400006F or 0x84000070)
 * w1:    Source/Destination IDs
 * w2:    Reserved (MBZ)
 * w3-w7: Implementation defined, free to be used below
 */

#define OPTEE_FFA_VERSION_MAJOR	1
#define OPTEE_FFA_VERSION_MINOR	0

#define OPTEE_FFA_BLOCKING_CALL(id)	(id)
#define OPTEE_FFA_YIELDING_CALL_BIT	31
#define OPTEE_FFA_YIELDING_CALL(id)	((id) | BIT(OPTEE_FFA_YIELDING_CALL_BIT))

/*
 * Returns the API version implemented, currently follows the FF-A version.
 * Call register usage:
 * w3:    Service ID, OPTEE_FFA_GET_API_VERSION
 * w4-w7: Not used (MBZ)
 *
 * Return register usage:
 * w3:    OPTEE_FFA_VERSION_MAJOR
 * w4:    OPTEE_FFA_VERSION_MINOR
 * w5-w7: Not used (MBZ)
 */
#define OPTEE_FFA_GET_API_VERSION	OPTEE_FFA_BLOCKING_CALL(0)

/*
 * Returns the revision of OP-TEE.
 *
 * Used by non-secure world to figure out which version of the Trusted OS
 * is installed. Note that the returned revision is the revision of the
 * Trusted OS, not of the API.
 *
 * Call register usage:
 * w3:    Service ID, OPTEE_FFA_GET_OS_VERSION
 * w4-w7: Unused (MBZ)
 *
 * Return register usage:
 * w3:    CFG_OPTEE_REVISION_MAJOR
 * w4:    CFG_OPTEE_REVISION_MINOR
 * w5:    TEE_IMPL_GIT_SHA1 (or zero if not supported)
 */
#define OPTEE_FFA_GET_OS_VERSION	OPTEE_FFA_BLOCKING_CALL(1)

/*
 * Exchange capabilities between normal world and secure world.
 *
 * Currently there are no defined capabilities. When features are added new
 * capabilities may be added.
 *
 * Call register usage:
 * w3:    Service ID, OPTEE_FFA_EXCHANGE_CAPABILITIES
 * w4-w7: Note used (MBZ)
 *
 * Return register usage:
 * w3:    Error code, 0 on success
 * w4:    Bit[7:0]:  Number of parameters needed for RPC to be supplied
 *                   as the second MSG arg struct for
 *                   OPTEE_FFA_YIELDING_CALL_WITH_ARG.
 *        Bit[31:8]: Reserved (MBZ)
 * w5:	  Bitfield of secure world capabilities OPTEE_FFA_SEC_CAP_* below,
 *	  unused bits MBZ.
 * w6-w7: Not used (MBZ)
 */
/*
 * Secure world supports giving an offset into the argument shared memory
 * object, see also OPTEE_FFA_YIELDING_CALL_WITH_ARG
 */
#define OPTEE_FFA_SEC_CAP_ARG_OFFSET	BIT(0)

#define OPTEE_FFA_EXCHANGE_CAPABILITIES OPTEE_FFA_BLOCKING_CALL(2)

/*
 * Unregister shared memory
 *
 * Call register usage:
 * w3:    Service ID, OPTEE_FFA_YIELDING_CALL_UNREGISTER_SHM
 * w4:    Shared memory handle, lower bits
 * w5:    Shared memory handle, higher bits
 * w6-w7: Not used (MBZ)
 *
 * Return register usage:
 * w3:    Error code, 0 on success
 * w4-w7: Note used (MBZ)
 */
#define OPTEE_FFA_UNREGISTER_SHM	OPTEE_FFA_BLOCKING_CALL(3)

/*
 * Call with struct optee_msg_arg as argument in the supplied shared memory
 * with a zero internal offset and normal cached memory attributes.
 * Register usage:
 * w3:    Service ID, OPTEE_FFA_YIELDING_CALL_WITH_ARG
 * w4:    Lower 32 bits of a 64-bit Shared memory handle
 * w5:    Upper 32 bits of a 64-bit Shared memory handle
 * w6:    Offset into shared memory pointing to a struct optee_msg_arg
 *	  right after the parameters of this struct (at offset
 *	  OPTEE_MSG_GET_ARG_SIZE(num_params) follows a struct optee_msg_arg
 *	  for RPC, this struct has reserved space for the number of RPC
 *	  parameters as returned by OPTEE_FFA_EXCHANGE_CAPABILITIES.
 *	  MBZ unless the bit OPTEE_FFA_SEC_CAP_ARG_OFFSET is received with
 *	  OPTEE_FFA_EXCHANGE_CAPABILITIES.
 * w7:    Not used (MBZ)
 * Resume from RPC. Register usage:
 * w3:    Service ID, OPTEE_FFA_YIELDING_CALL_RESUME
 * w4-w6: Not used (MBZ)
 * w7:    Resume info
 *
 * Normal return (yielding call is completed). Register usage:
 * w3:    Error code, 0 on success
 * w4:    OPTEE_FFA_YIELDING_CALL_RETURN_DONE
 * w5-w7: Not used (MBZ)
 *
 * RPC interrupt return (RPC from secure world). Register usage:
 * w3:    Error code == 0
 * w4:    Any defined RPC code but OPTEE_FFA_YIELDING_CALL_RETURN_DONE
 * w5-w6: Not used (MBZ)
 * w7:    Resume info
 *
 * Possible error codes in register w3:
 * 0:                       Success
 * FFA_DENIED:              w4 isn't one of OPTEE_FFA_YIELDING_CALL_START
 *                          OPTEE_FFA_YIELDING_CALL_RESUME
 *
 * Possible error codes for OPTEE_FFA_YIELDING_CALL_START,
 * FFA_BUSY:               Number of OP-TEE OS threads exceeded,
 *                         try again later
 * FFA_DENIED:             RPC shared memory object not found
 * FFA_INVALID_PARAMETER:  Bad shared memory handle or offset into the memory
 *
 * Possible error codes for OPTEE_FFA_YIELDING_CALL_RESUME
 * FFA_INVALID_PARAMETER:  Bad resume info
 */
#define OPTEE_FFA_YIELDING_CALL_WITH_ARG	OPTEE_FFA_YIELDING_CALL(0)
#define OPTEE_FFA_YIELDING_CALL_RESUME		OPTEE_FFA_YIELDING_CALL(1)

#define OPTEE_FFA_YIELDING_CALL_RETURN_DONE		0
#define OPTEE_FFA_YIELDING_CALL_RETURN_RPC_CMD		1
#define OPTEE_FFA_YIELDING_CALL_RETURN_INTERRUPT	2

#endif /*__OPTEE_FFA_H*/
