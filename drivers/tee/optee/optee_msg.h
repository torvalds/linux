/*
 * Copyright (c) 2015-2016, Linaro Limited
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _OPTEE_MSG_H
#define _OPTEE_MSG_H

#include <linux/types.h>

/*
 * This file defines the OP-TEE message protocol used to communicate
 * with an instance of OP-TEE running in secure world.
 *
 * This file is divided into three sections.
 * 1. Formatting of messages.
 * 2. Requests from normal world
 * 3. Requests from secure world, Remote Procedure Call (RPC), handled by
 *    tee-supplicant.
 */

/*****************************************************************************
 * Part 1 - formatting of messages
 *****************************************************************************/

#define OPTEE_MSG_ATTR_TYPE_NONE		0x0
#define OPTEE_MSG_ATTR_TYPE_VALUE_INPUT		0x1
#define OPTEE_MSG_ATTR_TYPE_VALUE_OUTPUT	0x2
#define OPTEE_MSG_ATTR_TYPE_VALUE_INOUT		0x3
#define OPTEE_MSG_ATTR_TYPE_RMEM_INPUT		0x5
#define OPTEE_MSG_ATTR_TYPE_RMEM_OUTPUT		0x6
#define OPTEE_MSG_ATTR_TYPE_RMEM_INOUT		0x7
#define OPTEE_MSG_ATTR_TYPE_TMEM_INPUT		0x9
#define OPTEE_MSG_ATTR_TYPE_TMEM_OUTPUT		0xa
#define OPTEE_MSG_ATTR_TYPE_TMEM_INOUT		0xb

#define OPTEE_MSG_ATTR_TYPE_MASK		0xff

/*
 * Meta parameter to be absorbed by the Secure OS and not passed
 * to the Trusted Application.
 *
 * Currently only used with OPTEE_MSG_CMD_OPEN_SESSION.
 */
#define OPTEE_MSG_ATTR_META			(1 << 8)

/*
 * The temporary shared memory object is not physically contigous and this
 * temp memref is followed by another fragment until the last temp memref
 * that doesn't have this bit set.
 */
#define OPTEE_MSG_ATTR_FRAGMENT			(1 << 9)

/*
 * Memory attributes for caching passed with temp memrefs. The actual value
 * used is defined outside the message protocol with the exception of
 * OPTEE_MSG_ATTR_CACHE_PREDEFINED which means the attributes already
 * defined for the memory range should be used. If optee_smc.h is used as
 * bearer of this protocol OPTEE_SMC_SHM_* is used for values.
 */
#define OPTEE_MSG_ATTR_CACHE_SHIFT		16
#define OPTEE_MSG_ATTR_CACHE_MASK		0x7
#define OPTEE_MSG_ATTR_CACHE_PREDEFINED		0

/*
 * Same values as TEE_LOGIN_* from TEE Internal API
 */
#define OPTEE_MSG_LOGIN_PUBLIC			0x00000000
#define OPTEE_MSG_LOGIN_USER			0x00000001
#define OPTEE_MSG_LOGIN_GROUP			0x00000002
#define OPTEE_MSG_LOGIN_APPLICATION		0x00000004
#define OPTEE_MSG_LOGIN_APPLICATION_USER	0x00000005
#define OPTEE_MSG_LOGIN_APPLICATION_GROUP	0x00000006

/**
 * struct optee_msg_param_tmem - temporary memory reference
 * @buf_ptr:	Address of the buffer
 * @size:	Size of the buffer
 * @shm_ref:	Temporary shared memory reference, pointer to a struct tee_shm
 *
 * Secure and normal world communicates pointers as physical address
 * instead of the virtual address. This is because secure and normal world
 * have completely independent memory mapping. Normal world can even have a
 * hypervisor which need to translate the guest physical address (AKA IPA
 * in ARM documentation) to a real physical address before passing the
 * structure to secure world.
 */
struct optee_msg_param_tmem {
	u64 buf_ptr;
	u64 size;
	u64 shm_ref;
};

/**
 * struct optee_msg_param_rmem - registered memory reference
 * @offs:	Offset into shared memory reference
 * @size:	Size of the buffer
 * @shm_ref:	Shared memory reference, pointer to a struct tee_shm
 */
struct optee_msg_param_rmem {
	u64 offs;
	u64 size;
	u64 shm_ref;
};

/**
 * struct optee_msg_param_value - values
 * @a: first value
 * @b: second value
 * @c: third value
 */
struct optee_msg_param_value {
	u64 a;
	u64 b;
	u64 c;
};

/**
 * struct optee_msg_param - parameter
 * @attr: attributes
 * @memref: a memory reference
 * @value: a value
 *
 * @attr & OPTEE_MSG_ATTR_TYPE_MASK indicates if tmem, rmem or value is used in
 * the union. OPTEE_MSG_ATTR_TYPE_VALUE_* indicates value,
 * OPTEE_MSG_ATTR_TYPE_TMEM_* indicates tmem and
 * OPTEE_MSG_ATTR_TYPE_RMEM_* indicates rmem.
 * OPTEE_MSG_ATTR_TYPE_NONE indicates that none of the members are used.
 */
struct optee_msg_param {
	u64 attr;
	union {
		struct optee_msg_param_tmem tmem;
		struct optee_msg_param_rmem rmem;
		struct optee_msg_param_value value;
	} u;
};

/**
 * struct optee_msg_arg - call argument
 * @cmd: Command, one of OPTEE_MSG_CMD_* or OPTEE_MSG_RPC_CMD_*
 * @func: Trusted Application function, specific to the Trusted Application,
 *	     used if cmd == OPTEE_MSG_CMD_INVOKE_COMMAND
 * @session: In parameter for all OPTEE_MSG_CMD_* except
 *	     OPTEE_MSG_CMD_OPEN_SESSION where it's an output parameter instead
 * @cancel_id: Cancellation id, a unique value to identify this request
 * @ret: return value
 * @ret_origin: origin of the return value
 * @num_params: number of parameters supplied to the OS Command
 * @params: the parameters supplied to the OS Command
 *
 * All normal calls to Trusted OS uses this struct. If cmd requires further
 * information than what these field holds it can be passed as a parameter
 * tagged as meta (setting the OPTEE_MSG_ATTR_META bit in corresponding
 * attrs field). All parameters tagged as meta has to come first.
 *
 * Temp memref parameters can be fragmented if supported by the Trusted OS
 * (when optee_smc.h is bearer of this protocol this is indicated with
 * OPTEE_SMC_SEC_CAP_UNREGISTERED_SHM). If a logical memref parameter is
 * fragmented then has all but the last fragment the
 * OPTEE_MSG_ATTR_FRAGMENT bit set in attrs. Even if a memref is fragmented
 * it will still be presented as a single logical memref to the Trusted
 * Application.
 */
struct optee_msg_arg {
	u32 cmd;
	u32 func;
	u32 session;
	u32 cancel_id;
	u32 pad;
	u32 ret;
	u32 ret_origin;
	u32 num_params;

	/*
	 * this struct is 8 byte aligned since the 'struct optee_msg_param'
	 * which follows requires 8 byte alignment.
	 *
	 * Commented out element used to visualize the layout dynamic part
	 * of the struct. This field is not available at all if
	 * num_params == 0.
	 *
	 * params is accessed through the macro OPTEE_MSG_GET_PARAMS
	 *
	 * struct optee_msg_param params[num_params];
	 */
} __aligned(8);

/**
 * OPTEE_MSG_GET_PARAMS - return pointer to struct optee_msg_param *
 *
 * @x: Pointer to a struct optee_msg_arg
 *
 * Returns a pointer to the params[] inside a struct optee_msg_arg.
 */
#define OPTEE_MSG_GET_PARAMS(x) \
	(struct optee_msg_param *)(((struct optee_msg_arg *)(x)) + 1)

/**
 * OPTEE_MSG_GET_ARG_SIZE - return size of struct optee_msg_arg
 *
 * @num_params: Number of parameters embedded in the struct optee_msg_arg
 *
 * Returns the size of the struct optee_msg_arg together with the number
 * of embedded parameters.
 */
#define OPTEE_MSG_GET_ARG_SIZE(num_params) \
	(sizeof(struct optee_msg_arg) + \
	 sizeof(struct optee_msg_param) * (num_params))

/*****************************************************************************
 * Part 2 - requests from normal world
 *****************************************************************************/

/*
 * Return the following UID if using API specified in this file without
 * further extentions:
 * 384fb3e0-e7f8-11e3-af63-0002a5d5c51b.
 * Represented in 4 32-bit words in OPTEE_MSG_UID_0, OPTEE_MSG_UID_1,
 * OPTEE_MSG_UID_2, OPTEE_MSG_UID_3.
 */
#define OPTEE_MSG_UID_0			0x384fb3e0
#define OPTEE_MSG_UID_1			0xe7f811e3
#define OPTEE_MSG_UID_2			0xaf630002
#define OPTEE_MSG_UID_3			0xa5d5c51b
#define OPTEE_MSG_FUNCID_CALLS_UID	0xFF01

/*
 * Returns 2.0 if using API specified in this file without further
 * extentions. Represented in 2 32-bit words in OPTEE_MSG_REVISION_MAJOR
 * and OPTEE_MSG_REVISION_MINOR
 */
#define OPTEE_MSG_REVISION_MAJOR	2
#define OPTEE_MSG_REVISION_MINOR	0
#define OPTEE_MSG_FUNCID_CALLS_REVISION	0xFF03

/*
 * Get UUID of Trusted OS.
 *
 * Used by non-secure world to figure out which Trusted OS is installed.
 * Note that returned UUID is the UUID of the Trusted OS, not of the API.
 *
 * Returns UUID in 4 32-bit words in the same way as
 * OPTEE_MSG_FUNCID_CALLS_UID described above.
 */
#define OPTEE_MSG_OS_OPTEE_UUID_0	0x486178e0
#define OPTEE_MSG_OS_OPTEE_UUID_1	0xe7f811e3
#define OPTEE_MSG_OS_OPTEE_UUID_2	0xbc5e0002
#define OPTEE_MSG_OS_OPTEE_UUID_3	0xa5d5c51b
#define OPTEE_MSG_FUNCID_GET_OS_UUID	0x0000

/*
 * Get revision of Trusted OS.
 *
 * Used by non-secure world to figure out which version of the Trusted OS
 * is installed. Note that the returned revision is the revision of the
 * Trusted OS, not of the API.
 *
 * Returns revision in 2 32-bit words in the same way as
 * OPTEE_MSG_CALLS_REVISION described above.
 */
#define OPTEE_MSG_OS_OPTEE_REVISION_MAJOR	1
#define OPTEE_MSG_OS_OPTEE_REVISION_MINOR	0
#define OPTEE_MSG_FUNCID_GET_OS_REVISION	0x0001

/*
 * Do a secure call with struct optee_msg_arg as argument
 * The OPTEE_MSG_CMD_* below defines what goes in struct optee_msg_arg::cmd
 *
 * OPTEE_MSG_CMD_OPEN_SESSION opens a session to a Trusted Application.
 * The first two parameters are tagged as meta, holding two value
 * parameters to pass the following information:
 * param[0].u.value.a-b uuid of Trusted Application
 * param[1].u.value.a-b uuid of Client
 * param[1].u.value.c Login class of client OPTEE_MSG_LOGIN_*
 *
 * OPTEE_MSG_CMD_INVOKE_COMMAND invokes a command a previously opened
 * session to a Trusted Application.  struct optee_msg_arg::func is Trusted
 * Application function, specific to the Trusted Application.
 *
 * OPTEE_MSG_CMD_CLOSE_SESSION closes a previously opened session to
 * Trusted Application.
 *
 * OPTEE_MSG_CMD_CANCEL cancels a currently invoked command.
 *
 * OPTEE_MSG_CMD_REGISTER_SHM registers a shared memory reference. The
 * information is passed as:
 * [in] param[0].attr			OPTEE_MSG_ATTR_TYPE_TMEM_INPUT
 *					[| OPTEE_MSG_ATTR_FRAGMENT]
 * [in] param[0].u.tmem.buf_ptr		physical address (of first fragment)
 * [in] param[0].u.tmem.size		size (of first fragment)
 * [in] param[0].u.tmem.shm_ref		holds shared memory reference
 * ...
 * The shared memory can optionally be fragmented, temp memrefs can follow
 * each other with all but the last with the OPTEE_MSG_ATTR_FRAGMENT bit set.
 *
 * OPTEE_MSG_CMD_UNREGISTER_SHM unregisteres a previously registered shared
 * memory reference. The information is passed as:
 * [in] param[0].attr			OPTEE_MSG_ATTR_TYPE_RMEM_INPUT
 * [in] param[0].u.rmem.shm_ref		holds shared memory reference
 * [in] param[0].u.rmem.offs		0
 * [in] param[0].u.rmem.size		0
 */
#define OPTEE_MSG_CMD_OPEN_SESSION	0
#define OPTEE_MSG_CMD_INVOKE_COMMAND	1
#define OPTEE_MSG_CMD_CLOSE_SESSION	2
#define OPTEE_MSG_CMD_CANCEL		3
#define OPTEE_MSG_CMD_REGISTER_SHM	4
#define OPTEE_MSG_CMD_UNREGISTER_SHM	5
#define OPTEE_MSG_FUNCID_CALL_WITH_ARG	0x0004

/*****************************************************************************
 * Part 3 - Requests from secure world, RPC
 *****************************************************************************/

/*
 * All RPC is done with a struct optee_msg_arg as bearer of information,
 * struct optee_msg_arg::arg holds values defined by OPTEE_MSG_RPC_CMD_* below
 *
 * RPC communication with tee-supplicant is reversed compared to normal
 * client communication desribed above. The supplicant receives requests
 * and sends responses.
 */

/*
 * Load a TA into memory, defined in tee-supplicant
 */
#define OPTEE_MSG_RPC_CMD_LOAD_TA	0

/*
 * Reserved
 */
#define OPTEE_MSG_RPC_CMD_RPMB		1

/*
 * File system access, defined in tee-supplicant
 */
#define OPTEE_MSG_RPC_CMD_FS		2

/*
 * Get time
 *
 * Returns number of seconds and nano seconds since the Epoch,
 * 1970-01-01 00:00:00 +0000 (UTC).
 *
 * [out] param[0].u.value.a	Number of seconds
 * [out] param[0].u.value.b	Number of nano seconds.
 */
#define OPTEE_MSG_RPC_CMD_GET_TIME	3

/*
 * Wait queue primitive, helper for secure world to implement a wait queue
 *
 * Waiting on a key
 * [in] param[0].u.value.a OPTEE_MSG_RPC_WAIT_QUEUE_SLEEP
 * [in] param[0].u.value.b wait key
 *
 * Waking up a key
 * [in] param[0].u.value.a OPTEE_MSG_RPC_WAIT_QUEUE_WAKEUP
 * [in] param[0].u.value.b wakeup key
 */
#define OPTEE_MSG_RPC_CMD_WAIT_QUEUE	4
#define OPTEE_MSG_RPC_WAIT_QUEUE_SLEEP	0
#define OPTEE_MSG_RPC_WAIT_QUEUE_WAKEUP	1

/*
 * Suspend execution
 *
 * [in] param[0].value	.a number of milliseconds to suspend
 */
#define OPTEE_MSG_RPC_CMD_SUSPEND	5

/*
 * Allocate a piece of shared memory
 *
 * Shared memory can optionally be fragmented, to support that additional
 * spare param entries are allocated to make room for eventual fragments.
 * The spare param entries has .attr = OPTEE_MSG_ATTR_TYPE_NONE when
 * unused. All returned temp memrefs except the last should have the
 * OPTEE_MSG_ATTR_FRAGMENT bit set in the attr field.
 *
 * [in]  param[0].u.value.a		type of memory one of
 *					OPTEE_MSG_RPC_SHM_TYPE_* below
 * [in]  param[0].u.value.b		requested size
 * [in]  param[0].u.value.c		required alignment
 *
 * [out] param[0].u.tmem.buf_ptr	physical address (of first fragment)
 * [out] param[0].u.tmem.size		size (of first fragment)
 * [out] param[0].u.tmem.shm_ref	shared memory reference
 * ...
 * [out] param[n].u.tmem.buf_ptr	physical address
 * [out] param[n].u.tmem.size		size
 * [out] param[n].u.tmem.shm_ref	shared memory reference (same value
 *					as in param[n-1].u.tmem.shm_ref)
 */
#define OPTEE_MSG_RPC_CMD_SHM_ALLOC	6
/* Memory that can be shared with a non-secure user space application */
#define OPTEE_MSG_RPC_SHM_TYPE_APPL	0
/* Memory only shared with non-secure kernel */
#define OPTEE_MSG_RPC_SHM_TYPE_KERNEL	1

/*
 * Free shared memory previously allocated with OPTEE_MSG_RPC_CMD_SHM_ALLOC
 *
 * [in]  param[0].u.value.a		type of memory one of
 *					OPTEE_MSG_RPC_SHM_TYPE_* above
 * [in]  param[0].u.value.b		value of shared memory reference
 *					returned in param[0].u.tmem.shm_ref
 *					above
 */
#define OPTEE_MSG_RPC_CMD_SHM_FREE	7


#endif /* _OPTEE_MSG_H */
