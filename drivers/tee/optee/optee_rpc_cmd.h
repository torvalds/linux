/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2016-2021, Linaro Limited
 */

#ifndef __OPTEE_RPC_CMD_H
#define __OPTEE_RPC_CMD_H

/*
 * All RPC is done with a struct optee_msg_arg as bearer of information,
 * struct optee_msg_arg::arg holds values defined by OPTEE_RPC_CMD_* below.
 * Only the commands handled by the kernel driver are defined here.
 *
 * RPC communication with tee-supplicant is reversed compared to normal
 * client communication described above. The supplicant receives requests
 * and sends responses.
 */

/*
 * Get time
 *
 * Returns number of seconds and nano seconds since the Epoch,
 * 1970-01-01 00:00:00 +0000 (UTC).
 *
 * [out]    value[0].a	    Number of seconds
 * [out]    value[0].b	    Number of nano seconds.
 */
#define OPTEE_RPC_CMD_GET_TIME		3

/*
 * Notification from/to secure world.
 *
 * If secure world needs to wait for something, for instance a mutex, it
 * does a notification wait request instead of spinning in secure world.
 * Conversely can a synchronous notification can be sent when a secure
 * world mutex with a thread waiting thread is unlocked.
 *
 * This interface can also be used to wait for a asynchronous notification
 * which instead is sent via a non-secure interrupt.
 *
 * Waiting on notification
 * [in]    value[0].a	    OPTEE_RPC_NOTIFICATION_WAIT
 * [in]    value[0].b	    notification value
 * [in]    value[0].c	    timeout in milliseconds or 0 if no timeout
 *
 * Sending a synchronous notification
 * [in]    value[0].a	    OPTEE_RPC_NOTIFICATION_SEND
 * [in]    value[0].b	    notification value
 */
#define OPTEE_RPC_CMD_NOTIFICATION	4
#define OPTEE_RPC_NOTIFICATION_WAIT	0
#define OPTEE_RPC_NOTIFICATION_SEND	1

/*
 * Suspend execution
 *
 * [in]    value[0].a	Number of milliseconds to suspend
 */
#define OPTEE_RPC_CMD_SUSPEND		5

/*
 * Allocate a piece of shared memory
 *
 * [in]    value[0].a	    Type of memory one of
 *			    OPTEE_RPC_SHM_TYPE_* below
 * [in]    value[0].b	    Requested size
 * [in]    value[0].c	    Required alignment
 * [out]   memref[0]	    Buffer
 */
#define OPTEE_RPC_CMD_SHM_ALLOC		6
/* Memory that can be shared with a non-secure user space application */
#define OPTEE_RPC_SHM_TYPE_APPL		0
/* Memory only shared with non-secure kernel */
#define OPTEE_RPC_SHM_TYPE_KERNEL	1

/*
 * Free shared memory previously allocated with OPTEE_RPC_CMD_SHM_ALLOC
 *
 * [in]     value[0].a	    Type of memory one of
 *			    OPTEE_RPC_SHM_TYPE_* above
 * [in]     value[0].b	    Value of shared memory reference or cookie
 */
#define OPTEE_RPC_CMD_SHM_FREE		7

/*
 * Issue master requests (read and write operations) to an I2C chip.
 *
 * [in]     value[0].a	    Transfer mode (OPTEE_RPC_I2C_TRANSFER_*)
 * [in]     value[0].b	    The I2C bus (a.k.a adapter).
 *				16 bit field.
 * [in]     value[0].c	    The I2C chip (a.k.a address).
 *				16 bit field (either 7 or 10 bit effective).
 * [in]     value[1].a	    The I2C master control flags (ie, 10 bit address).
 *				16 bit field.
 * [in/out] memref[2]	    Buffer used for data transfers.
 * [out]    value[3].a	    Number of bytes transferred by the REE.
 */
#define OPTEE_RPC_CMD_I2C_TRANSFER	21

/* I2C master transfer modes */
#define OPTEE_RPC_I2C_TRANSFER_RD	0
#define OPTEE_RPC_I2C_TRANSFER_WR	1

/* I2C master control flags */
#define OPTEE_RPC_I2C_FLAGS_TEN_BIT	BIT(0)

#endif /*__OPTEE_RPC_CMD_H*/
