// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. All rights reserved. */
#include <linux/security.h>
#include <linux/debugfs.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <asm/unaligned.h>
#include <cxlpci.h>
#include <cxlmem.h>
#include <cxl.h>

#include "core.h"
#include "trace.h"

static bool cxl_raw_allow_all;

/**
 * DOC: cxl mbox
 *
 * Core implementation of the CXL 2.0 Type-3 Memory Device Mailbox. The
 * implementation is used by the cxl_pci driver to initialize the device
 * and implement the cxl_mem.h IOCTL UAPI. It also implements the
 * backend of the cxl_pmem_ctl() transport for LIBNVDIMM.
 */

#define cxl_for_each_cmd(cmd)                                                  \
	for ((cmd) = &cxl_mem_commands[0];                                     \
	     ((cmd) - cxl_mem_commands) < ARRAY_SIZE(cxl_mem_commands); (cmd)++)

#define CXL_CMD(_id, sin, sout, _flags)                                        \
	[CXL_MEM_COMMAND_ID_##_id] = {                                         \
	.info =	{                                                              \
			.id = CXL_MEM_COMMAND_ID_##_id,                        \
			.size_in = sin,                                        \
			.size_out = sout,                                      \
		},                                                             \
	.opcode = CXL_MBOX_OP_##_id,                                           \
	.flags = _flags,                                                       \
	}

#define CXL_VARIABLE_PAYLOAD	~0U
/*
 * This table defines the supported mailbox commands for the driver. This table
 * is made up of a UAPI structure. Non-negative values as parameters in the
 * table will be validated against the user's input. For example, if size_in is
 * 0, and the user passed in 1, it is an error.
 */
static struct cxl_mem_command cxl_mem_commands[CXL_MEM_COMMAND_ID_MAX] = {
	CXL_CMD(IDENTIFY, 0, 0x43, CXL_CMD_FLAG_FORCE_ENABLE),
#ifdef CONFIG_CXL_MEM_RAW_COMMANDS
	CXL_CMD(RAW, CXL_VARIABLE_PAYLOAD, CXL_VARIABLE_PAYLOAD, 0),
#endif
	CXL_CMD(GET_SUPPORTED_LOGS, 0, CXL_VARIABLE_PAYLOAD, CXL_CMD_FLAG_FORCE_ENABLE),
	CXL_CMD(GET_FW_INFO, 0, 0x50, 0),
	CXL_CMD(GET_PARTITION_INFO, 0, 0x20, 0),
	CXL_CMD(GET_LSA, 0x8, CXL_VARIABLE_PAYLOAD, 0),
	CXL_CMD(GET_HEALTH_INFO, 0, 0x12, 0),
	CXL_CMD(GET_LOG, 0x18, CXL_VARIABLE_PAYLOAD, CXL_CMD_FLAG_FORCE_ENABLE),
	CXL_CMD(GET_LOG_CAPS, 0x10, 0x4, 0),
	CXL_CMD(CLEAR_LOG, 0x10, 0, 0),
	CXL_CMD(GET_SUP_LOG_SUBLIST, 0x2, CXL_VARIABLE_PAYLOAD, 0),
	CXL_CMD(SET_PARTITION_INFO, 0x0a, 0, 0),
	CXL_CMD(SET_LSA, CXL_VARIABLE_PAYLOAD, 0, 0),
	CXL_CMD(GET_ALERT_CONFIG, 0, 0x10, 0),
	CXL_CMD(SET_ALERT_CONFIG, 0xc, 0, 0),
	CXL_CMD(GET_SHUTDOWN_STATE, 0, 0x1, 0),
	CXL_CMD(SET_SHUTDOWN_STATE, 0x1, 0, 0),
	CXL_CMD(GET_SCAN_MEDIA_CAPS, 0x10, 0x4, 0),
	CXL_CMD(GET_TIMESTAMP, 0, 0x8, 0),
};

/*
 * Commands that RAW doesn't permit. The rationale for each:
 *
 * CXL_MBOX_OP_ACTIVATE_FW: Firmware activation requires adjustment /
 * coordination of transaction timeout values at the root bridge level.
 *
 * CXL_MBOX_OP_SET_PARTITION_INFO: The device memory map may change live
 * and needs to be coordinated with HDM updates.
 *
 * CXL_MBOX_OP_SET_LSA: The label storage area may be cached by the
 * driver and any writes from userspace invalidates those contents.
 *
 * CXL_MBOX_OP_SET_SHUTDOWN_STATE: Set shutdown state assumes no writes
 * to the device after it is marked clean, userspace can not make that
 * assertion.
 *
 * CXL_MBOX_OP_[GET_]SCAN_MEDIA: The kernel provides a native error list that
 * is kept up to date with patrol notifications and error management.
 *
 * CXL_MBOX_OP_[GET_,INJECT_,CLEAR_]POISON: These commands require kernel
 * driver orchestration for safety.
 */
static u16 cxl_disabled_raw_commands[] = {
	CXL_MBOX_OP_ACTIVATE_FW,
	CXL_MBOX_OP_SET_PARTITION_INFO,
	CXL_MBOX_OP_SET_LSA,
	CXL_MBOX_OP_SET_SHUTDOWN_STATE,
	CXL_MBOX_OP_SCAN_MEDIA,
	CXL_MBOX_OP_GET_SCAN_MEDIA,
	CXL_MBOX_OP_GET_POISON,
	CXL_MBOX_OP_INJECT_POISON,
	CXL_MBOX_OP_CLEAR_POISON,
};

/*
 * Command sets that RAW doesn't permit. All opcodes in this set are
 * disabled because they pass plain text security payloads over the
 * user/kernel boundary. This functionality is intended to be wrapped
 * behind the keys ABI which allows for encrypted payloads in the UAPI
 */
static u8 security_command_sets[] = {
	0x44, /* Sanitize */
	0x45, /* Persistent Memory Data-at-rest Security */
	0x46, /* Security Passthrough */
};

static bool cxl_is_security_command(u16 opcode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(security_command_sets); i++)
		if (security_command_sets[i] == (opcode >> 8))
			return true;
	return false;
}

static void cxl_set_security_cmd_enabled(struct cxl_security_state *security,
					 u16 opcode)
{
	switch (opcode) {
	case CXL_MBOX_OP_SANITIZE:
		set_bit(CXL_SEC_ENABLED_SANITIZE, security->enabled_cmds);
		break;
	case CXL_MBOX_OP_SECURE_ERASE:
		set_bit(CXL_SEC_ENABLED_SECURE_ERASE,
			security->enabled_cmds);
		break;
	case CXL_MBOX_OP_GET_SECURITY_STATE:
		set_bit(CXL_SEC_ENABLED_GET_SECURITY_STATE,
			security->enabled_cmds);
		break;
	case CXL_MBOX_OP_SET_PASSPHRASE:
		set_bit(CXL_SEC_ENABLED_SET_PASSPHRASE,
			security->enabled_cmds);
		break;
	case CXL_MBOX_OP_DISABLE_PASSPHRASE:
		set_bit(CXL_SEC_ENABLED_DISABLE_PASSPHRASE,
			security->enabled_cmds);
		break;
	case CXL_MBOX_OP_UNLOCK:
		set_bit(CXL_SEC_ENABLED_UNLOCK, security->enabled_cmds);
		break;
	case CXL_MBOX_OP_FREEZE_SECURITY:
		set_bit(CXL_SEC_ENABLED_FREEZE_SECURITY,
			security->enabled_cmds);
		break;
	case CXL_MBOX_OP_PASSPHRASE_SECURE_ERASE:
		set_bit(CXL_SEC_ENABLED_PASSPHRASE_SECURE_ERASE,
			security->enabled_cmds);
		break;
	default:
		break;
	}
}

static bool cxl_is_poison_command(u16 opcode)
{
#define CXL_MBOX_OP_POISON_CMDS 0x43

	if ((opcode >> 8) == CXL_MBOX_OP_POISON_CMDS)
		return true;

	return false;
}

static void cxl_set_poison_cmd_enabled(struct cxl_poison_state *poison,
				       u16 opcode)
{
	switch (opcode) {
	case CXL_MBOX_OP_GET_POISON:
		set_bit(CXL_POISON_ENABLED_LIST, poison->enabled_cmds);
		break;
	case CXL_MBOX_OP_INJECT_POISON:
		set_bit(CXL_POISON_ENABLED_INJECT, poison->enabled_cmds);
		break;
	case CXL_MBOX_OP_CLEAR_POISON:
		set_bit(CXL_POISON_ENABLED_CLEAR, poison->enabled_cmds);
		break;
	case CXL_MBOX_OP_GET_SCAN_MEDIA_CAPS:
		set_bit(CXL_POISON_ENABLED_SCAN_CAPS, poison->enabled_cmds);
		break;
	case CXL_MBOX_OP_SCAN_MEDIA:
		set_bit(CXL_POISON_ENABLED_SCAN_MEDIA, poison->enabled_cmds);
		break;
	case CXL_MBOX_OP_GET_SCAN_MEDIA:
		set_bit(CXL_POISON_ENABLED_SCAN_RESULTS, poison->enabled_cmds);
		break;
	default:
		break;
	}
}

static struct cxl_mem_command *cxl_mem_find_command(u16 opcode)
{
	struct cxl_mem_command *c;

	cxl_for_each_cmd(c)
		if (c->opcode == opcode)
			return c;

	return NULL;
}

static const char *cxl_mem_opcode_to_name(u16 opcode)
{
	struct cxl_mem_command *c;

	c = cxl_mem_find_command(opcode);
	if (!c)
		return NULL;

	return cxl_command_names[c->info.id].name;
}

/**
 * cxl_internal_send_cmd() - Kernel internal interface to send a mailbox command
 * @mds: The driver data for the operation
 * @mbox_cmd: initialized command to execute
 *
 * Context: Any context.
 * Return:
 *  * %>=0	- Number of bytes returned in @out.
 *  * %-E2BIG	- Payload is too large for hardware.
 *  * %-EBUSY	- Couldn't acquire exclusive mailbox access.
 *  * %-EFAULT	- Hardware error occurred.
 *  * %-ENXIO	- Command completed, but device reported an error.
 *  * %-EIO	- Unexpected output size.
 *
 * Mailbox commands may execute successfully yet the device itself reported an
 * error. While this distinction can be useful for commands from userspace, the
 * kernel will only be able to use results when both are successful.
 */
int cxl_internal_send_cmd(struct cxl_memdev_state *mds,
			  struct cxl_mbox_cmd *mbox_cmd)
{
	size_t out_size, min_out;
	int rc;

	if (mbox_cmd->size_in > mds->payload_size ||
	    mbox_cmd->size_out > mds->payload_size)
		return -E2BIG;

	out_size = mbox_cmd->size_out;
	min_out = mbox_cmd->min_out;
	rc = mds->mbox_send(mds, mbox_cmd);
	/*
	 * EIO is reserved for a payload size mismatch and mbox_send()
	 * may not return this error.
	 */
	if (WARN_ONCE(rc == -EIO, "Bad return code: -EIO"))
		return -ENXIO;
	if (rc)
		return rc;

	if (mbox_cmd->return_code != CXL_MBOX_CMD_RC_SUCCESS &&
	    mbox_cmd->return_code != CXL_MBOX_CMD_RC_BACKGROUND)
		return cxl_mbox_cmd_rc2errno(mbox_cmd);

	if (!out_size)
		return 0;

	/*
	 * Variable sized output needs to at least satisfy the caller's
	 * minimum if not the fully requested size.
	 */
	if (min_out == 0)
		min_out = out_size;

	if (mbox_cmd->size_out < min_out)
		return -EIO;
	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_internal_send_cmd, CXL);

static bool cxl_mem_raw_command_allowed(u16 opcode)
{
	int i;

	if (!IS_ENABLED(CONFIG_CXL_MEM_RAW_COMMANDS))
		return false;

	if (security_locked_down(LOCKDOWN_PCI_ACCESS))
		return false;

	if (cxl_raw_allow_all)
		return true;

	if (cxl_is_security_command(opcode))
		return false;

	for (i = 0; i < ARRAY_SIZE(cxl_disabled_raw_commands); i++)
		if (cxl_disabled_raw_commands[i] == opcode)
			return false;

	return true;
}

/**
 * cxl_payload_from_user_allowed() - Check contents of in_payload.
 * @opcode: The mailbox command opcode.
 * @payload_in: Pointer to the input payload passed in from user space.
 *
 * Return:
 *  * true	- payload_in passes check for @opcode.
 *  * false	- payload_in contains invalid or unsupported values.
 *
 * The driver may inspect payload contents before sending a mailbox
 * command from user space to the device. The intent is to reject
 * commands with input payloads that are known to be unsafe. This
 * check is not intended to replace the users careful selection of
 * mailbox command parameters and makes no guarantee that the user
 * command will succeed, nor that it is appropriate.
 *
 * The specific checks are determined by the opcode.
 */
static bool cxl_payload_from_user_allowed(u16 opcode, void *payload_in)
{
	switch (opcode) {
	case CXL_MBOX_OP_SET_PARTITION_INFO: {
		struct cxl_mbox_set_partition_info *pi = payload_in;

		if (pi->flags & CXL_SET_PARTITION_IMMEDIATE_FLAG)
			return false;
		break;
	}
	case CXL_MBOX_OP_CLEAR_LOG: {
		const uuid_t *uuid = (uuid_t *)payload_in;

		/*
		 * Restrict the ‘Clear log’ action to only apply to
		 * Vendor debug logs.
		 */
		return uuid_equal(uuid, &DEFINE_CXL_VENDOR_DEBUG_UUID);
	}
	default:
		break;
	}
	return true;
}

static int cxl_mbox_cmd_ctor(struct cxl_mbox_cmd *mbox,
			     struct cxl_memdev_state *mds, u16 opcode,
			     size_t in_size, size_t out_size, u64 in_payload)
{
	*mbox = (struct cxl_mbox_cmd) {
		.opcode = opcode,
		.size_in = in_size,
	};

	if (in_size) {
		mbox->payload_in = vmemdup_user(u64_to_user_ptr(in_payload),
						in_size);
		if (IS_ERR(mbox->payload_in))
			return PTR_ERR(mbox->payload_in);

		if (!cxl_payload_from_user_allowed(opcode, mbox->payload_in)) {
			dev_dbg(mds->cxlds.dev, "%s: input payload not allowed\n",
				cxl_mem_opcode_to_name(opcode));
			kvfree(mbox->payload_in);
			return -EBUSY;
		}
	}

	/* Prepare to handle a full payload for variable sized output */
	if (out_size == CXL_VARIABLE_PAYLOAD)
		mbox->size_out = mds->payload_size;
	else
		mbox->size_out = out_size;

	if (mbox->size_out) {
		mbox->payload_out = kvzalloc(mbox->size_out, GFP_KERNEL);
		if (!mbox->payload_out) {
			kvfree(mbox->payload_in);
			return -ENOMEM;
		}
	}
	return 0;
}

static void cxl_mbox_cmd_dtor(struct cxl_mbox_cmd *mbox)
{
	kvfree(mbox->payload_in);
	kvfree(mbox->payload_out);
}

static int cxl_to_mem_cmd_raw(struct cxl_mem_command *mem_cmd,
			      const struct cxl_send_command *send_cmd,
			      struct cxl_memdev_state *mds)
{
	if (send_cmd->raw.rsvd)
		return -EINVAL;

	/*
	 * Unlike supported commands, the output size of RAW commands
	 * gets passed along without further checking, so it must be
	 * validated here.
	 */
	if (send_cmd->out.size > mds->payload_size)
		return -EINVAL;

	if (!cxl_mem_raw_command_allowed(send_cmd->raw.opcode))
		return -EPERM;

	dev_WARN_ONCE(mds->cxlds.dev, true, "raw command path used\n");

	*mem_cmd = (struct cxl_mem_command) {
		.info = {
			.id = CXL_MEM_COMMAND_ID_RAW,
			.size_in = send_cmd->in.size,
			.size_out = send_cmd->out.size,
		},
		.opcode = send_cmd->raw.opcode
	};

	return 0;
}

static int cxl_to_mem_cmd(struct cxl_mem_command *mem_cmd,
			  const struct cxl_send_command *send_cmd,
			  struct cxl_memdev_state *mds)
{
	struct cxl_mem_command *c = &cxl_mem_commands[send_cmd->id];
	const struct cxl_command_info *info = &c->info;

	if (send_cmd->flags & ~CXL_MEM_COMMAND_FLAG_MASK)
		return -EINVAL;

	if (send_cmd->rsvd)
		return -EINVAL;

	if (send_cmd->in.rsvd || send_cmd->out.rsvd)
		return -EINVAL;

	/* Check that the command is enabled for hardware */
	if (!test_bit(info->id, mds->enabled_cmds))
		return -ENOTTY;

	/* Check that the command is not claimed for exclusive kernel use */
	if (test_bit(info->id, mds->exclusive_cmds))
		return -EBUSY;

	/* Check the input buffer is the expected size */
	if ((info->size_in != CXL_VARIABLE_PAYLOAD) &&
	    (info->size_in != send_cmd->in.size))
		return -ENOMEM;

	/* Check the output buffer is at least large enough */
	if ((info->size_out != CXL_VARIABLE_PAYLOAD) &&
	    (send_cmd->out.size < info->size_out))
		return -ENOMEM;

	*mem_cmd = (struct cxl_mem_command) {
		.info = {
			.id = info->id,
			.flags = info->flags,
			.size_in = send_cmd->in.size,
			.size_out = send_cmd->out.size,
		},
		.opcode = c->opcode
	};

	return 0;
}

/**
 * cxl_validate_cmd_from_user() - Check fields for CXL_MEM_SEND_COMMAND.
 * @mbox_cmd: Sanitized and populated &struct cxl_mbox_cmd.
 * @mds: The driver data for the operation
 * @send_cmd: &struct cxl_send_command copied in from userspace.
 *
 * Return:
 *  * %0	- @out_cmd is ready to send.
 *  * %-ENOTTY	- Invalid command specified.
 *  * %-EINVAL	- Reserved fields or invalid values were used.
 *  * %-ENOMEM	- Input or output buffer wasn't sized properly.
 *  * %-EPERM	- Attempted to use a protected command.
 *  * %-EBUSY	- Kernel has claimed exclusive access to this opcode
 *
 * The result of this command is a fully validated command in @mbox_cmd that is
 * safe to send to the hardware.
 */
static int cxl_validate_cmd_from_user(struct cxl_mbox_cmd *mbox_cmd,
				      struct cxl_memdev_state *mds,
				      const struct cxl_send_command *send_cmd)
{
	struct cxl_mem_command mem_cmd;
	int rc;

	if (send_cmd->id == 0 || send_cmd->id >= CXL_MEM_COMMAND_ID_MAX)
		return -ENOTTY;

	/*
	 * The user can never specify an input payload larger than what hardware
	 * supports, but output can be arbitrarily large (simply write out as
	 * much data as the hardware provides).
	 */
	if (send_cmd->in.size > mds->payload_size)
		return -EINVAL;

	/* Sanitize and construct a cxl_mem_command */
	if (send_cmd->id == CXL_MEM_COMMAND_ID_RAW)
		rc = cxl_to_mem_cmd_raw(&mem_cmd, send_cmd, mds);
	else
		rc = cxl_to_mem_cmd(&mem_cmd, send_cmd, mds);

	if (rc)
		return rc;

	/* Sanitize and construct a cxl_mbox_cmd */
	return cxl_mbox_cmd_ctor(mbox_cmd, mds, mem_cmd.opcode,
				 mem_cmd.info.size_in, mem_cmd.info.size_out,
				 send_cmd->in.payload);
}

int cxl_query_cmd(struct cxl_memdev *cxlmd,
		  struct cxl_mem_query_commands __user *q)
{
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlmd->cxlds);
	struct device *dev = &cxlmd->dev;
	struct cxl_mem_command *cmd;
	u32 n_commands;
	int j = 0;

	dev_dbg(dev, "Query IOCTL\n");

	if (get_user(n_commands, &q->n_commands))
		return -EFAULT;

	/* returns the total number if 0 elements are requested. */
	if (n_commands == 0)
		return put_user(ARRAY_SIZE(cxl_mem_commands), &q->n_commands);

	/*
	 * otherwise, return max(n_commands, total commands) cxl_command_info
	 * structures.
	 */
	cxl_for_each_cmd(cmd) {
		struct cxl_command_info info = cmd->info;

		if (test_bit(info.id, mds->enabled_cmds))
			info.flags |= CXL_MEM_COMMAND_FLAG_ENABLED;
		if (test_bit(info.id, mds->exclusive_cmds))
			info.flags |= CXL_MEM_COMMAND_FLAG_EXCLUSIVE;

		if (copy_to_user(&q->commands[j++], &info, sizeof(info)))
			return -EFAULT;

		if (j == n_commands)
			break;
	}

	return 0;
}

/**
 * handle_mailbox_cmd_from_user() - Dispatch a mailbox command for userspace.
 * @mds: The driver data for the operation
 * @mbox_cmd: The validated mailbox command.
 * @out_payload: Pointer to userspace's output payload.
 * @size_out: (Input) Max payload size to copy out.
 *            (Output) Payload size hardware generated.
 * @retval: Hardware generated return code from the operation.
 *
 * Return:
 *  * %0	- Mailbox transaction succeeded. This implies the mailbox
 *		  protocol completed successfully not that the operation itself
 *		  was successful.
 *  * %-ENOMEM  - Couldn't allocate a bounce buffer.
 *  * %-EFAULT	- Something happened with copy_to/from_user.
 *  * %-EINTR	- Mailbox acquisition interrupted.
 *  * %-EXXX	- Transaction level failures.
 *
 * Dispatches a mailbox command on behalf of a userspace request.
 * The output payload is copied to userspace.
 *
 * See cxl_send_cmd().
 */
static int handle_mailbox_cmd_from_user(struct cxl_memdev_state *mds,
					struct cxl_mbox_cmd *mbox_cmd,
					u64 out_payload, s32 *size_out,
					u32 *retval)
{
	struct device *dev = mds->cxlds.dev;
	int rc;

	dev_dbg(dev,
		"Submitting %s command for user\n"
		"\topcode: %x\n"
		"\tsize: %zx\n",
		cxl_mem_opcode_to_name(mbox_cmd->opcode),
		mbox_cmd->opcode, mbox_cmd->size_in);

	rc = mds->mbox_send(mds, mbox_cmd);
	if (rc)
		goto out;

	/*
	 * @size_out contains the max size that's allowed to be written back out
	 * to userspace. While the payload may have written more output than
	 * this it will have to be ignored.
	 */
	if (mbox_cmd->size_out) {
		dev_WARN_ONCE(dev, mbox_cmd->size_out > *size_out,
			      "Invalid return size\n");
		if (copy_to_user(u64_to_user_ptr(out_payload),
				 mbox_cmd->payload_out, mbox_cmd->size_out)) {
			rc = -EFAULT;
			goto out;
		}
	}

	*size_out = mbox_cmd->size_out;
	*retval = mbox_cmd->return_code;

out:
	cxl_mbox_cmd_dtor(mbox_cmd);
	return rc;
}

int cxl_send_cmd(struct cxl_memdev *cxlmd, struct cxl_send_command __user *s)
{
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlmd->cxlds);
	struct device *dev = &cxlmd->dev;
	struct cxl_send_command send;
	struct cxl_mbox_cmd mbox_cmd;
	int rc;

	dev_dbg(dev, "Send IOCTL\n");

	if (copy_from_user(&send, s, sizeof(send)))
		return -EFAULT;

	rc = cxl_validate_cmd_from_user(&mbox_cmd, mds, &send);
	if (rc)
		return rc;

	rc = handle_mailbox_cmd_from_user(mds, &mbox_cmd, send.out.payload,
					  &send.out.size, &send.retval);
	if (rc)
		return rc;

	if (copy_to_user(s, &send, sizeof(send)))
		return -EFAULT;

	return 0;
}

static int cxl_xfer_log(struct cxl_memdev_state *mds, uuid_t *uuid,
			u32 *size, u8 *out)
{
	u32 remaining = *size;
	u32 offset = 0;

	while (remaining) {
		u32 xfer_size = min_t(u32, remaining, mds->payload_size);
		struct cxl_mbox_cmd mbox_cmd;
		struct cxl_mbox_get_log log;
		int rc;

		log = (struct cxl_mbox_get_log) {
			.uuid = *uuid,
			.offset = cpu_to_le32(offset),
			.length = cpu_to_le32(xfer_size),
		};

		mbox_cmd = (struct cxl_mbox_cmd) {
			.opcode = CXL_MBOX_OP_GET_LOG,
			.size_in = sizeof(log),
			.payload_in = &log,
			.size_out = xfer_size,
			.payload_out = out,
		};

		rc = cxl_internal_send_cmd(mds, &mbox_cmd);

		/*
		 * The output payload length that indicates the number
		 * of valid bytes can be smaller than the Log buffer
		 * size.
		 */
		if (rc == -EIO && mbox_cmd.size_out < xfer_size) {
			offset += mbox_cmd.size_out;
			break;
		}

		if (rc < 0)
			return rc;

		out += xfer_size;
		remaining -= xfer_size;
		offset += xfer_size;
	}

	*size = offset;

	return 0;
}

/**
 * cxl_walk_cel() - Walk through the Command Effects Log.
 * @mds: The driver data for the operation
 * @size: Length of the Command Effects Log.
 * @cel: CEL
 *
 * Iterate over each entry in the CEL and determine if the driver supports the
 * command. If so, the command is enabled for the device and can be used later.
 */
static void cxl_walk_cel(struct cxl_memdev_state *mds, size_t size, u8 *cel)
{
	struct cxl_cel_entry *cel_entry;
	const int cel_entries = size / sizeof(*cel_entry);
	struct device *dev = mds->cxlds.dev;
	int i;

	cel_entry = (struct cxl_cel_entry *) cel;

	for (i = 0; i < cel_entries; i++) {
		u16 opcode = le16_to_cpu(cel_entry[i].opcode);
		struct cxl_mem_command *cmd = cxl_mem_find_command(opcode);
		int enabled = 0;

		if (cmd) {
			set_bit(cmd->info.id, mds->enabled_cmds);
			enabled++;
		}

		if (cxl_is_poison_command(opcode)) {
			cxl_set_poison_cmd_enabled(&mds->poison, opcode);
			enabled++;
		}

		if (cxl_is_security_command(opcode)) {
			cxl_set_security_cmd_enabled(&mds->security, opcode);
			enabled++;
		}

		dev_dbg(dev, "Opcode 0x%04x %s\n", opcode,
			enabled ? "enabled" : "unsupported by driver");
	}
}

static struct cxl_mbox_get_supported_logs *cxl_get_gsl(struct cxl_memdev_state *mds)
{
	struct cxl_mbox_get_supported_logs *ret;
	struct cxl_mbox_cmd mbox_cmd;
	int rc;

	ret = kvmalloc(mds->payload_size, GFP_KERNEL);
	if (!ret)
		return ERR_PTR(-ENOMEM);

	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_GET_SUPPORTED_LOGS,
		.size_out = mds->payload_size,
		.payload_out = ret,
		/* At least the record number field must be valid */
		.min_out = 2,
	};
	rc = cxl_internal_send_cmd(mds, &mbox_cmd);
	if (rc < 0) {
		kvfree(ret);
		return ERR_PTR(rc);
	}


	return ret;
}

enum {
	CEL_UUID,
	VENDOR_DEBUG_UUID,
};

/* See CXL 2.0 Table 170. Get Log Input Payload */
static const uuid_t log_uuid[] = {
	[CEL_UUID] = DEFINE_CXL_CEL_UUID,
	[VENDOR_DEBUG_UUID] = DEFINE_CXL_VENDOR_DEBUG_UUID,
};

/**
 * cxl_enumerate_cmds() - Enumerate commands for a device.
 * @mds: The driver data for the operation
 *
 * Returns 0 if enumerate completed successfully.
 *
 * CXL devices have optional support for certain commands. This function will
 * determine the set of supported commands for the hardware and update the
 * enabled_cmds bitmap in the @mds.
 */
int cxl_enumerate_cmds(struct cxl_memdev_state *mds)
{
	struct cxl_mbox_get_supported_logs *gsl;
	struct device *dev = mds->cxlds.dev;
	struct cxl_mem_command *cmd;
	int i, rc;

	gsl = cxl_get_gsl(mds);
	if (IS_ERR(gsl))
		return PTR_ERR(gsl);

	rc = -ENOENT;
	for (i = 0; i < le16_to_cpu(gsl->entries); i++) {
		u32 size = le32_to_cpu(gsl->entry[i].size);
		uuid_t uuid = gsl->entry[i].uuid;
		u8 *log;

		dev_dbg(dev, "Found LOG type %pU of size %d", &uuid, size);

		if (!uuid_equal(&uuid, &log_uuid[CEL_UUID]))
			continue;

		log = kvmalloc(size, GFP_KERNEL);
		if (!log) {
			rc = -ENOMEM;
			goto out;
		}

		rc = cxl_xfer_log(mds, &uuid, &size, log);
		if (rc) {
			kvfree(log);
			goto out;
		}

		cxl_walk_cel(mds, size, log);
		kvfree(log);

		/* In case CEL was bogus, enable some default commands. */
		cxl_for_each_cmd(cmd)
			if (cmd->flags & CXL_CMD_FLAG_FORCE_ENABLE)
				set_bit(cmd->info.id, mds->enabled_cmds);

		/* Found the required CEL */
		rc = 0;
	}
out:
	kvfree(gsl);
	return rc;
}
EXPORT_SYMBOL_NS_GPL(cxl_enumerate_cmds, CXL);

void cxl_event_trace_record(const struct cxl_memdev *cxlmd,
			    enum cxl_event_log_type type,
			    enum cxl_event_type event_type,
			    const uuid_t *uuid, union cxl_event *evt)
{
	if (event_type == CXL_CPER_EVENT_MEM_MODULE) {
		trace_cxl_memory_module(cxlmd, type, &evt->mem_module);
		return;
	}
	if (event_type == CXL_CPER_EVENT_GENERIC) {
		trace_cxl_generic_event(cxlmd, type, uuid, &evt->generic);
		return;
	}

	if (trace_cxl_general_media_enabled() || trace_cxl_dram_enabled()) {
		u64 dpa, hpa = ULLONG_MAX;
		struct cxl_region *cxlr;

		/*
		 * These trace points are annotated with HPA and region
		 * translations. Take topology mutation locks and lookup
		 * { HPA, REGION } from { DPA, MEMDEV } in the event record.
		 */
		guard(rwsem_read)(&cxl_region_rwsem);
		guard(rwsem_read)(&cxl_dpa_rwsem);

		dpa = le64_to_cpu(evt->common.phys_addr) & CXL_DPA_MASK;
		cxlr = cxl_dpa_to_region(cxlmd, dpa);
		if (cxlr)
			hpa = cxl_trace_hpa(cxlr, cxlmd, dpa);

		if (event_type == CXL_CPER_EVENT_GEN_MEDIA)
			trace_cxl_general_media(cxlmd, type, cxlr, hpa,
						&evt->gen_media);
		else if (event_type == CXL_CPER_EVENT_DRAM)
			trace_cxl_dram(cxlmd, type, cxlr, hpa, &evt->dram);
	}
}
EXPORT_SYMBOL_NS_GPL(cxl_event_trace_record, CXL);

static void __cxl_event_trace_record(const struct cxl_memdev *cxlmd,
				     enum cxl_event_log_type type,
				     struct cxl_event_record_raw *record)
{
	enum cxl_event_type ev_type = CXL_CPER_EVENT_GENERIC;
	const uuid_t *uuid = &record->id;

	if (uuid_equal(uuid, &CXL_EVENT_GEN_MEDIA_UUID))
		ev_type = CXL_CPER_EVENT_GEN_MEDIA;
	else if (uuid_equal(uuid, &CXL_EVENT_DRAM_UUID))
		ev_type = CXL_CPER_EVENT_DRAM;
	else if (uuid_equal(uuid, &CXL_EVENT_MEM_MODULE_UUID))
		ev_type = CXL_CPER_EVENT_MEM_MODULE;

	cxl_event_trace_record(cxlmd, type, ev_type, uuid, &record->event);
}

static int cxl_clear_event_record(struct cxl_memdev_state *mds,
				  enum cxl_event_log_type log,
				  struct cxl_get_event_payload *get_pl)
{
	struct cxl_mbox_clear_event_payload *payload;
	u16 total = le16_to_cpu(get_pl->record_count);
	u8 max_handles = CXL_CLEAR_EVENT_MAX_HANDLES;
	size_t pl_size = struct_size(payload, handles, max_handles);
	struct cxl_mbox_cmd mbox_cmd;
	u16 cnt;
	int rc = 0;
	int i;

	/* Payload size may limit the max handles */
	if (pl_size > mds->payload_size) {
		max_handles = (mds->payload_size - sizeof(*payload)) /
			      sizeof(__le16);
		pl_size = struct_size(payload, handles, max_handles);
	}

	payload = kvzalloc(pl_size, GFP_KERNEL);
	if (!payload)
		return -ENOMEM;

	*payload = (struct cxl_mbox_clear_event_payload) {
		.event_log = log,
	};

	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_CLEAR_EVENT_RECORD,
		.payload_in = payload,
		.size_in = pl_size,
	};

	/*
	 * Clear Event Records uses u8 for the handle cnt while Get Event
	 * Record can return up to 0xffff records.
	 */
	i = 0;
	for (cnt = 0; cnt < total; cnt++) {
		struct cxl_event_record_raw *raw = &get_pl->records[cnt];
		struct cxl_event_generic *gen = &raw->event.generic;

		payload->handles[i++] = gen->hdr.handle;
		dev_dbg(mds->cxlds.dev, "Event log '%d': Clearing %u\n", log,
			le16_to_cpu(payload->handles[i - 1]));

		if (i == max_handles) {
			payload->nr_recs = i;
			rc = cxl_internal_send_cmd(mds, &mbox_cmd);
			if (rc)
				goto free_pl;
			i = 0;
		}
	}

	/* Clear what is left if any */
	if (i) {
		payload->nr_recs = i;
		mbox_cmd.size_in = struct_size(payload, handles, i);
		rc = cxl_internal_send_cmd(mds, &mbox_cmd);
		if (rc)
			goto free_pl;
	}

free_pl:
	kvfree(payload);
	return rc;
}

static void cxl_mem_get_records_log(struct cxl_memdev_state *mds,
				    enum cxl_event_log_type type)
{
	struct cxl_memdev *cxlmd = mds->cxlds.cxlmd;
	struct device *dev = mds->cxlds.dev;
	struct cxl_get_event_payload *payload;
	u8 log_type = type;
	u16 nr_rec;

	mutex_lock(&mds->event.log_lock);
	payload = mds->event.buf;

	do {
		int rc, i;
		struct cxl_mbox_cmd mbox_cmd = (struct cxl_mbox_cmd) {
			.opcode = CXL_MBOX_OP_GET_EVENT_RECORD,
			.payload_in = &log_type,
			.size_in = sizeof(log_type),
			.payload_out = payload,
			.size_out = mds->payload_size,
			.min_out = struct_size(payload, records, 0),
		};

		rc = cxl_internal_send_cmd(mds, &mbox_cmd);
		if (rc) {
			dev_err_ratelimited(dev,
				"Event log '%d': Failed to query event records : %d",
				type, rc);
			break;
		}

		nr_rec = le16_to_cpu(payload->record_count);
		if (!nr_rec)
			break;

		for (i = 0; i < nr_rec; i++)
			__cxl_event_trace_record(cxlmd, type,
						 &payload->records[i]);

		if (payload->flags & CXL_GET_EVENT_FLAG_OVERFLOW)
			trace_cxl_overflow(cxlmd, type, payload);

		rc = cxl_clear_event_record(mds, type, payload);
		if (rc) {
			dev_err_ratelimited(dev,
				"Event log '%d': Failed to clear events : %d",
				type, rc);
			break;
		}
	} while (nr_rec);

	mutex_unlock(&mds->event.log_lock);
}

/**
 * cxl_mem_get_event_records - Get Event Records from the device
 * @mds: The driver data for the operation
 * @status: Event Status register value identifying which events are available.
 *
 * Retrieve all event records available on the device, report them as trace
 * events, and clear them.
 *
 * See CXL rev 3.0 @8.2.9.2.2 Get Event Records
 * See CXL rev 3.0 @8.2.9.2.3 Clear Event Records
 */
void cxl_mem_get_event_records(struct cxl_memdev_state *mds, u32 status)
{
	dev_dbg(mds->cxlds.dev, "Reading event logs: %x\n", status);

	if (status & CXLDEV_EVENT_STATUS_FATAL)
		cxl_mem_get_records_log(mds, CXL_EVENT_TYPE_FATAL);
	if (status & CXLDEV_EVENT_STATUS_FAIL)
		cxl_mem_get_records_log(mds, CXL_EVENT_TYPE_FAIL);
	if (status & CXLDEV_EVENT_STATUS_WARN)
		cxl_mem_get_records_log(mds, CXL_EVENT_TYPE_WARN);
	if (status & CXLDEV_EVENT_STATUS_INFO)
		cxl_mem_get_records_log(mds, CXL_EVENT_TYPE_INFO);
}
EXPORT_SYMBOL_NS_GPL(cxl_mem_get_event_records, CXL);

/**
 * cxl_mem_get_partition_info - Get partition info
 * @mds: The driver data for the operation
 *
 * Retrieve the current partition info for the device specified.  The active
 * values are the current capacity in bytes.  If not 0, the 'next' values are
 * the pending values, in bytes, which take affect on next cold reset.
 *
 * Return: 0 if no error: or the result of the mailbox command.
 *
 * See CXL @8.2.9.5.2.1 Get Partition Info
 */
static int cxl_mem_get_partition_info(struct cxl_memdev_state *mds)
{
	struct cxl_mbox_get_partition_info pi;
	struct cxl_mbox_cmd mbox_cmd;
	int rc;

	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_GET_PARTITION_INFO,
		.size_out = sizeof(pi),
		.payload_out = &pi,
	};
	rc = cxl_internal_send_cmd(mds, &mbox_cmd);
	if (rc)
		return rc;

	mds->active_volatile_bytes =
		le64_to_cpu(pi.active_volatile_cap) * CXL_CAPACITY_MULTIPLIER;
	mds->active_persistent_bytes =
		le64_to_cpu(pi.active_persistent_cap) * CXL_CAPACITY_MULTIPLIER;
	mds->next_volatile_bytes =
		le64_to_cpu(pi.next_volatile_cap) * CXL_CAPACITY_MULTIPLIER;
	mds->next_persistent_bytes =
		le64_to_cpu(pi.next_volatile_cap) * CXL_CAPACITY_MULTIPLIER;

	return 0;
}

/**
 * cxl_dev_state_identify() - Send the IDENTIFY command to the device.
 * @mds: The driver data for the operation
 *
 * Return: 0 if identify was executed successfully or media not ready.
 *
 * This will dispatch the identify command to the device and on success populate
 * structures to be exported to sysfs.
 */
int cxl_dev_state_identify(struct cxl_memdev_state *mds)
{
	/* See CXL 2.0 Table 175 Identify Memory Device Output Payload */
	struct cxl_mbox_identify id;
	struct cxl_mbox_cmd mbox_cmd;
	u32 val;
	int rc;

	if (!mds->cxlds.media_ready)
		return 0;

	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_IDENTIFY,
		.size_out = sizeof(id),
		.payload_out = &id,
	};
	rc = cxl_internal_send_cmd(mds, &mbox_cmd);
	if (rc < 0)
		return rc;

	mds->total_bytes =
		le64_to_cpu(id.total_capacity) * CXL_CAPACITY_MULTIPLIER;
	mds->volatile_only_bytes =
		le64_to_cpu(id.volatile_capacity) * CXL_CAPACITY_MULTIPLIER;
	mds->persistent_only_bytes =
		le64_to_cpu(id.persistent_capacity) * CXL_CAPACITY_MULTIPLIER;
	mds->partition_align_bytes =
		le64_to_cpu(id.partition_align) * CXL_CAPACITY_MULTIPLIER;

	mds->lsa_size = le32_to_cpu(id.lsa_size);
	memcpy(mds->firmware_version, id.fw_revision,
	       sizeof(id.fw_revision));

	if (test_bit(CXL_POISON_ENABLED_LIST, mds->poison.enabled_cmds)) {
		val = get_unaligned_le24(id.poison_list_max_mer);
		mds->poison.max_errors = min_t(u32, val, CXL_POISON_LIST_MAX);
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_dev_state_identify, CXL);

static int __cxl_mem_sanitize(struct cxl_memdev_state *mds, u16 cmd)
{
	int rc;
	u32 sec_out = 0;
	struct cxl_get_security_output {
		__le32 flags;
	} out;
	struct cxl_mbox_cmd sec_cmd = {
		.opcode = CXL_MBOX_OP_GET_SECURITY_STATE,
		.payload_out = &out,
		.size_out = sizeof(out),
	};
	struct cxl_mbox_cmd mbox_cmd = { .opcode = cmd };
	struct cxl_dev_state *cxlds = &mds->cxlds;

	if (cmd != CXL_MBOX_OP_SANITIZE && cmd != CXL_MBOX_OP_SECURE_ERASE)
		return -EINVAL;

	rc = cxl_internal_send_cmd(mds, &sec_cmd);
	if (rc < 0) {
		dev_err(cxlds->dev, "Failed to get security state : %d", rc);
		return rc;
	}

	/*
	 * Prior to using these commands, any security applied to
	 * the user data areas of the device shall be DISABLED (or
	 * UNLOCKED for secure erase case).
	 */
	sec_out = le32_to_cpu(out.flags);
	if (sec_out & CXL_PMEM_SEC_STATE_USER_PASS_SET)
		return -EINVAL;

	if (cmd == CXL_MBOX_OP_SECURE_ERASE &&
	    sec_out & CXL_PMEM_SEC_STATE_LOCKED)
		return -EINVAL;

	rc = cxl_internal_send_cmd(mds, &mbox_cmd);
	if (rc < 0) {
		dev_err(cxlds->dev, "Failed to sanitize device : %d", rc);
		return rc;
	}

	return 0;
}


/**
 * cxl_mem_sanitize() - Send a sanitization command to the device.
 * @cxlmd: The device for the operation
 * @cmd: The specific sanitization command opcode
 *
 * Return: 0 if the command was executed successfully, regardless of
 * whether or not the actual security operation is done in the background,
 * such as for the Sanitize case.
 * Error return values can be the result of the mailbox command, -EINVAL
 * when security requirements are not met or invalid contexts, or -EBUSY
 * if the sanitize operation is already in flight.
 *
 * See CXL 3.0 @8.2.9.8.5.1 Sanitize and @8.2.9.8.5.2 Secure Erase.
 */
int cxl_mem_sanitize(struct cxl_memdev *cxlmd, u16 cmd)
{
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlmd->cxlds);
	struct cxl_port  *endpoint;
	int rc;

	/* synchronize with cxl_mem_probe() and decoder write operations */
	device_lock(&cxlmd->dev);
	endpoint = cxlmd->endpoint;
	down_read(&cxl_region_rwsem);
	/*
	 * Require an endpoint to be safe otherwise the driver can not
	 * be sure that the device is unmapped.
	 */
	if (endpoint && cxl_num_decoders_committed(endpoint) == 0)
		rc = __cxl_mem_sanitize(mds, cmd);
	else
		rc = -EBUSY;
	up_read(&cxl_region_rwsem);
	device_unlock(&cxlmd->dev);

	return rc;
}

static int add_dpa_res(struct device *dev, struct resource *parent,
		       struct resource *res, resource_size_t start,
		       resource_size_t size, const char *type)
{
	int rc;

	res->name = type;
	res->start = start;
	res->end = start + size - 1;
	res->flags = IORESOURCE_MEM;
	if (resource_size(res) == 0) {
		dev_dbg(dev, "DPA(%s): no capacity\n", res->name);
		return 0;
	}
	rc = request_resource(parent, res);
	if (rc) {
		dev_err(dev, "DPA(%s): failed to track %pr (%d)\n", res->name,
			res, rc);
		return rc;
	}

	dev_dbg(dev, "DPA(%s): %pr\n", res->name, res);

	return 0;
}

int cxl_mem_create_range_info(struct cxl_memdev_state *mds)
{
	struct cxl_dev_state *cxlds = &mds->cxlds;
	struct device *dev = cxlds->dev;
	int rc;

	if (!cxlds->media_ready) {
		cxlds->dpa_res = DEFINE_RES_MEM(0, 0);
		cxlds->ram_res = DEFINE_RES_MEM(0, 0);
		cxlds->pmem_res = DEFINE_RES_MEM(0, 0);
		return 0;
	}

	cxlds->dpa_res = DEFINE_RES_MEM(0, mds->total_bytes);

	if (mds->partition_align_bytes == 0) {
		rc = add_dpa_res(dev, &cxlds->dpa_res, &cxlds->ram_res, 0,
				 mds->volatile_only_bytes, "ram");
		if (rc)
			return rc;
		return add_dpa_res(dev, &cxlds->dpa_res, &cxlds->pmem_res,
				   mds->volatile_only_bytes,
				   mds->persistent_only_bytes, "pmem");
	}

	rc = cxl_mem_get_partition_info(mds);
	if (rc) {
		dev_err(dev, "Failed to query partition information\n");
		return rc;
	}

	rc = add_dpa_res(dev, &cxlds->dpa_res, &cxlds->ram_res, 0,
			 mds->active_volatile_bytes, "ram");
	if (rc)
		return rc;
	return add_dpa_res(dev, &cxlds->dpa_res, &cxlds->pmem_res,
			   mds->active_volatile_bytes,
			   mds->active_persistent_bytes, "pmem");
}
EXPORT_SYMBOL_NS_GPL(cxl_mem_create_range_info, CXL);

int cxl_set_timestamp(struct cxl_memdev_state *mds)
{
	struct cxl_mbox_cmd mbox_cmd;
	struct cxl_mbox_set_timestamp_in pi;
	int rc;

	pi.timestamp = cpu_to_le64(ktime_get_real_ns());
	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_SET_TIMESTAMP,
		.size_in = sizeof(pi),
		.payload_in = &pi,
	};

	rc = cxl_internal_send_cmd(mds, &mbox_cmd);
	/*
	 * Command is optional. Devices may have another way of providing
	 * a timestamp, or may return all 0s in timestamp fields.
	 * Don't report an error if this command isn't supported
	 */
	if (rc && (mbox_cmd.return_code != CXL_MBOX_CMD_RC_UNSUPPORTED))
		return rc;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_set_timestamp, CXL);

int cxl_mem_get_poison(struct cxl_memdev *cxlmd, u64 offset, u64 len,
		       struct cxl_region *cxlr)
{
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlmd->cxlds);
	struct cxl_mbox_poison_out *po;
	struct cxl_mbox_poison_in pi;
	int nr_records = 0;
	int rc;

	rc = mutex_lock_interruptible(&mds->poison.lock);
	if (rc)
		return rc;

	po = mds->poison.list_out;
	pi.offset = cpu_to_le64(offset);
	pi.length = cpu_to_le64(len / CXL_POISON_LEN_MULT);

	do {
		struct cxl_mbox_cmd mbox_cmd = (struct cxl_mbox_cmd){
			.opcode = CXL_MBOX_OP_GET_POISON,
			.size_in = sizeof(pi),
			.payload_in = &pi,
			.size_out = mds->payload_size,
			.payload_out = po,
			.min_out = struct_size(po, record, 0),
		};

		rc = cxl_internal_send_cmd(mds, &mbox_cmd);
		if (rc)
			break;

		for (int i = 0; i < le16_to_cpu(po->count); i++)
			trace_cxl_poison(cxlmd, cxlr, &po->record[i],
					 po->flags, po->overflow_ts,
					 CXL_POISON_TRACE_LIST);

		/* Protect against an uncleared _FLAG_MORE */
		nr_records = nr_records + le16_to_cpu(po->count);
		if (nr_records >= mds->poison.max_errors) {
			dev_dbg(&cxlmd->dev, "Max Error Records reached: %d\n",
				nr_records);
			break;
		}
	} while (po->flags & CXL_POISON_FLAG_MORE);

	mutex_unlock(&mds->poison.lock);
	return rc;
}
EXPORT_SYMBOL_NS_GPL(cxl_mem_get_poison, CXL);

static void free_poison_buf(void *buf)
{
	kvfree(buf);
}

/* Get Poison List output buffer is protected by mds->poison.lock */
static int cxl_poison_alloc_buf(struct cxl_memdev_state *mds)
{
	mds->poison.list_out = kvmalloc(mds->payload_size, GFP_KERNEL);
	if (!mds->poison.list_out)
		return -ENOMEM;

	return devm_add_action_or_reset(mds->cxlds.dev, free_poison_buf,
					mds->poison.list_out);
}

int cxl_poison_state_init(struct cxl_memdev_state *mds)
{
	int rc;

	if (!test_bit(CXL_POISON_ENABLED_LIST, mds->poison.enabled_cmds))
		return 0;

	rc = cxl_poison_alloc_buf(mds);
	if (rc) {
		clear_bit(CXL_POISON_ENABLED_LIST, mds->poison.enabled_cmds);
		return rc;
	}

	mutex_init(&mds->poison.lock);
	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_poison_state_init, CXL);

struct cxl_memdev_state *cxl_memdev_state_create(struct device *dev)
{
	struct cxl_memdev_state *mds;

	mds = devm_kzalloc(dev, sizeof(*mds), GFP_KERNEL);
	if (!mds) {
		dev_err(dev, "No memory available\n");
		return ERR_PTR(-ENOMEM);
	}

	mutex_init(&mds->mbox_mutex);
	mutex_init(&mds->event.log_lock);
	mds->cxlds.dev = dev;
	mds->cxlds.reg_map.host = dev;
	mds->cxlds.reg_map.resource = CXL_RESOURCE_NONE;
	mds->cxlds.type = CXL_DEVTYPE_CLASSMEM;
	mds->ram_perf.qos_class = CXL_QOS_CLASS_INVALID;
	mds->pmem_perf.qos_class = CXL_QOS_CLASS_INVALID;

	return mds;
}
EXPORT_SYMBOL_NS_GPL(cxl_memdev_state_create, CXL);

void __init cxl_mbox_init(void)
{
	struct dentry *mbox_debugfs;

	mbox_debugfs = cxl_debugfs_create_dir("mbox");
	debugfs_create_bool("raw_allow_all", 0600, mbox_debugfs,
			    &cxl_raw_allow_all);
}
