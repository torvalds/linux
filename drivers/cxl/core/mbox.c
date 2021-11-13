// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. All rights reserved. */
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/security.h>
#include <linux/debugfs.h>
#include <linux/mutex.h>
#include <cxlmem.h>
#include <cxl.h>

#include "core.h"

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

/*
 * This table defines the supported mailbox commands for the driver. This table
 * is made up of a UAPI structure. Non-negative values as parameters in the
 * table will be validated against the user's input. For example, if size_in is
 * 0, and the user passed in 1, it is an error.
 */
static struct cxl_mem_command cxl_mem_commands[CXL_MEM_COMMAND_ID_MAX] = {
	CXL_CMD(IDENTIFY, 0, 0x43, CXL_CMD_FLAG_FORCE_ENABLE),
#ifdef CONFIG_CXL_MEM_RAW_COMMANDS
	CXL_CMD(RAW, ~0, ~0, 0),
#endif
	CXL_CMD(GET_SUPPORTED_LOGS, 0, ~0, CXL_CMD_FLAG_FORCE_ENABLE),
	CXL_CMD(GET_FW_INFO, 0, 0x50, 0),
	CXL_CMD(GET_PARTITION_INFO, 0, 0x20, 0),
	CXL_CMD(GET_LSA, 0x8, ~0, 0),
	CXL_CMD(GET_HEALTH_INFO, 0, 0x12, 0),
	CXL_CMD(GET_LOG, 0x18, ~0, CXL_CMD_FLAG_FORCE_ENABLE),
	CXL_CMD(SET_PARTITION_INFO, 0x0a, 0, 0),
	CXL_CMD(SET_LSA, ~0, 0, 0),
	CXL_CMD(GET_ALERT_CONFIG, 0, 0x10, 0),
	CXL_CMD(SET_ALERT_CONFIG, 0xc, 0, 0),
	CXL_CMD(GET_SHUTDOWN_STATE, 0, 0x1, 0),
	CXL_CMD(SET_SHUTDOWN_STATE, 0x1, 0, 0),
	CXL_CMD(GET_POISON, 0x10, ~0, 0),
	CXL_CMD(INJECT_POISON, 0x8, 0, 0),
	CXL_CMD(CLEAR_POISON, 0x48, 0, 0),
	CXL_CMD(GET_SCAN_MEDIA_CAPS, 0x10, 0x4, 0),
	CXL_CMD(SCAN_MEDIA, 0x11, 0, 0),
	CXL_CMD(GET_SCAN_MEDIA, 0, ~0, 0),
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
 */
static u16 cxl_disabled_raw_commands[] = {
	CXL_MBOX_OP_ACTIVATE_FW,
	CXL_MBOX_OP_SET_PARTITION_INFO,
	CXL_MBOX_OP_SET_LSA,
	CXL_MBOX_OP_SET_SHUTDOWN_STATE,
	CXL_MBOX_OP_SCAN_MEDIA,
	CXL_MBOX_OP_GET_SCAN_MEDIA,
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

static struct cxl_mem_command *cxl_mem_find_command(u16 opcode)
{
	struct cxl_mem_command *c;

	cxl_for_each_cmd(c)
		if (c->opcode == opcode)
			return c;

	return NULL;
}

/**
 * cxl_mbox_send_cmd() - Send a mailbox command to a device.
 * @cxlds: The device data for the operation
 * @opcode: Opcode for the mailbox command.
 * @in: The input payload for the mailbox command.
 * @in_size: The length of the input payload
 * @out: Caller allocated buffer for the output.
 * @out_size: Expected size of output.
 *
 * Context: Any context. Will acquire and release mbox_mutex.
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
int cxl_mbox_send_cmd(struct cxl_dev_state *cxlds, u16 opcode, void *in,
		      size_t in_size, void *out, size_t out_size)
{
	const struct cxl_mem_command *cmd = cxl_mem_find_command(opcode);
	struct cxl_mbox_cmd mbox_cmd = {
		.opcode = opcode,
		.payload_in = in,
		.size_in = in_size,
		.size_out = out_size,
		.payload_out = out,
	};
	int rc;

	if (out_size > cxlds->payload_size)
		return -E2BIG;

	rc = cxlds->mbox_send(cxlds, &mbox_cmd);
	if (rc)
		return rc;

	/* TODO: Map return code to proper kernel style errno */
	if (mbox_cmd.return_code != CXL_MBOX_SUCCESS)
		return -ENXIO;

	/*
	 * Variable sized commands can't be validated and so it's up to the
	 * caller to do that if they wish.
	 */
	if (cmd->info.size_out >= 0 && mbox_cmd.size_out != out_size)
		return -EIO;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_mbox_send_cmd, CXL);

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
 * cxl_validate_cmd_from_user() - Check fields for CXL_MEM_SEND_COMMAND.
 * @cxlds: The device data for the operation
 * @send_cmd: &struct cxl_send_command copied in from userspace.
 * @out_cmd: Sanitized and populated &struct cxl_mem_command.
 *
 * Return:
 *  * %0	- @out_cmd is ready to send.
 *  * %-ENOTTY	- Invalid command specified.
 *  * %-EINVAL	- Reserved fields or invalid values were used.
 *  * %-ENOMEM	- Input or output buffer wasn't sized properly.
 *  * %-EPERM	- Attempted to use a protected command.
 *  * %-EBUSY	- Kernel has claimed exclusive access to this opcode
 *
 * The result of this command is a fully validated command in @out_cmd that is
 * safe to send to the hardware.
 *
 * See handle_mailbox_cmd_from_user()
 */
static int cxl_validate_cmd_from_user(struct cxl_dev_state *cxlds,
				      const struct cxl_send_command *send_cmd,
				      struct cxl_mem_command *out_cmd)
{
	const struct cxl_command_info *info;
	struct cxl_mem_command *c;

	if (send_cmd->id == 0 || send_cmd->id >= CXL_MEM_COMMAND_ID_MAX)
		return -ENOTTY;

	/*
	 * The user can never specify an input payload larger than what hardware
	 * supports, but output can be arbitrarily large (simply write out as
	 * much data as the hardware provides).
	 */
	if (send_cmd->in.size > cxlds->payload_size)
		return -EINVAL;

	/*
	 * Checks are bypassed for raw commands but a WARN/taint will occur
	 * later in the callchain
	 */
	if (send_cmd->id == CXL_MEM_COMMAND_ID_RAW) {
		const struct cxl_mem_command temp = {
			.info = {
				.id = CXL_MEM_COMMAND_ID_RAW,
				.flags = 0,
				.size_in = send_cmd->in.size,
				.size_out = send_cmd->out.size,
			},
			.opcode = send_cmd->raw.opcode
		};

		if (send_cmd->raw.rsvd)
			return -EINVAL;

		/*
		 * Unlike supported commands, the output size of RAW commands
		 * gets passed along without further checking, so it must be
		 * validated here.
		 */
		if (send_cmd->out.size > cxlds->payload_size)
			return -EINVAL;

		if (!cxl_mem_raw_command_allowed(send_cmd->raw.opcode))
			return -EPERM;

		memcpy(out_cmd, &temp, sizeof(temp));

		return 0;
	}

	if (send_cmd->flags & ~CXL_MEM_COMMAND_FLAG_MASK)
		return -EINVAL;

	if (send_cmd->rsvd)
		return -EINVAL;

	if (send_cmd->in.rsvd || send_cmd->out.rsvd)
		return -EINVAL;

	/* Convert user's command into the internal representation */
	c = &cxl_mem_commands[send_cmd->id];
	info = &c->info;

	/* Check that the command is enabled for hardware */
	if (!test_bit(info->id, cxlds->enabled_cmds))
		return -ENOTTY;

	/* Check that the command is not claimed for exclusive kernel use */
	if (test_bit(info->id, cxlds->exclusive_cmds))
		return -EBUSY;

	/* Check the input buffer is the expected size */
	if (info->size_in >= 0 && info->size_in != send_cmd->in.size)
		return -ENOMEM;

	/* Check the output buffer is at least large enough */
	if (info->size_out >= 0 && send_cmd->out.size < info->size_out)
		return -ENOMEM;

	memcpy(out_cmd, c, sizeof(*c));
	out_cmd->info.size_in = send_cmd->in.size;
	/*
	 * XXX: out_cmd->info.size_out will be controlled by the driver, and the
	 * specified number of bytes @send_cmd->out.size will be copied back out
	 * to userspace.
	 */

	return 0;
}

int cxl_query_cmd(struct cxl_memdev *cxlmd,
		  struct cxl_mem_query_commands __user *q)
{
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
		const struct cxl_command_info *info = &cmd->info;

		if (copy_to_user(&q->commands[j++], info, sizeof(*info)))
			return -EFAULT;

		if (j == n_commands)
			break;
	}

	return 0;
}

/**
 * handle_mailbox_cmd_from_user() - Dispatch a mailbox command for userspace.
 * @cxlds: The device data for the operation
 * @cmd: The validated command.
 * @in_payload: Pointer to userspace's input payload.
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
 * Creates the appropriate mailbox command and dispatches it on behalf of a
 * userspace request. The input and output payloads are copied between
 * userspace.
 *
 * See cxl_send_cmd().
 */
static int handle_mailbox_cmd_from_user(struct cxl_dev_state *cxlds,
					const struct cxl_mem_command *cmd,
					u64 in_payload, u64 out_payload,
					s32 *size_out, u32 *retval)
{
	struct device *dev = cxlds->dev;
	struct cxl_mbox_cmd mbox_cmd = {
		.opcode = cmd->opcode,
		.size_in = cmd->info.size_in,
		.size_out = cmd->info.size_out,
	};
	int rc;

	if (cmd->info.size_out) {
		mbox_cmd.payload_out = kvzalloc(cmd->info.size_out, GFP_KERNEL);
		if (!mbox_cmd.payload_out)
			return -ENOMEM;
	}

	if (cmd->info.size_in) {
		mbox_cmd.payload_in = vmemdup_user(u64_to_user_ptr(in_payload),
						   cmd->info.size_in);
		if (IS_ERR(mbox_cmd.payload_in)) {
			kvfree(mbox_cmd.payload_out);
			return PTR_ERR(mbox_cmd.payload_in);
		}
	}

	dev_dbg(dev,
		"Submitting %s command for user\n"
		"\topcode: %x\n"
		"\tsize: %ub\n",
		cxl_command_names[cmd->info.id].name, mbox_cmd.opcode,
		cmd->info.size_in);

	dev_WARN_ONCE(dev, cmd->info.id == CXL_MEM_COMMAND_ID_RAW,
		      "raw command path used\n");

	rc = cxlds->mbox_send(cxlds, &mbox_cmd);
	if (rc)
		goto out;

	/*
	 * @size_out contains the max size that's allowed to be written back out
	 * to userspace. While the payload may have written more output than
	 * this it will have to be ignored.
	 */
	if (mbox_cmd.size_out) {
		dev_WARN_ONCE(dev, mbox_cmd.size_out > *size_out,
			      "Invalid return size\n");
		if (copy_to_user(u64_to_user_ptr(out_payload),
				 mbox_cmd.payload_out, mbox_cmd.size_out)) {
			rc = -EFAULT;
			goto out;
		}
	}

	*size_out = mbox_cmd.size_out;
	*retval = mbox_cmd.return_code;

out:
	kvfree(mbox_cmd.payload_in);
	kvfree(mbox_cmd.payload_out);
	return rc;
}

int cxl_send_cmd(struct cxl_memdev *cxlmd, struct cxl_send_command __user *s)
{
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct device *dev = &cxlmd->dev;
	struct cxl_send_command send;
	struct cxl_mem_command c;
	int rc;

	dev_dbg(dev, "Send IOCTL\n");

	if (copy_from_user(&send, s, sizeof(send)))
		return -EFAULT;

	rc = cxl_validate_cmd_from_user(cxlmd->cxlds, &send, &c);
	if (rc)
		return rc;

	/* Prepare to handle a full payload for variable sized output */
	if (c.info.size_out < 0)
		c.info.size_out = cxlds->payload_size;

	rc = handle_mailbox_cmd_from_user(cxlds, &c, send.in.payload,
					  send.out.payload, &send.out.size,
					  &send.retval);
	if (rc)
		return rc;

	if (copy_to_user(s, &send, sizeof(send)))
		return -EFAULT;

	return 0;
}

static int cxl_xfer_log(struct cxl_dev_state *cxlds, uuid_t *uuid, u32 size, u8 *out)
{
	u32 remaining = size;
	u32 offset = 0;

	while (remaining) {
		u32 xfer_size = min_t(u32, remaining, cxlds->payload_size);
		struct cxl_mbox_get_log log = {
			.uuid = *uuid,
			.offset = cpu_to_le32(offset),
			.length = cpu_to_le32(xfer_size)
		};
		int rc;

		rc = cxl_mbox_send_cmd(cxlds, CXL_MBOX_OP_GET_LOG, &log, sizeof(log),
				       out, xfer_size);
		if (rc < 0)
			return rc;

		out += xfer_size;
		remaining -= xfer_size;
		offset += xfer_size;
	}

	return 0;
}

/**
 * cxl_walk_cel() - Walk through the Command Effects Log.
 * @cxlds: The device data for the operation
 * @size: Length of the Command Effects Log.
 * @cel: CEL
 *
 * Iterate over each entry in the CEL and determine if the driver supports the
 * command. If so, the command is enabled for the device and can be used later.
 */
static void cxl_walk_cel(struct cxl_dev_state *cxlds, size_t size, u8 *cel)
{
	struct cxl_cel_entry *cel_entry;
	const int cel_entries = size / sizeof(*cel_entry);
	int i;

	cel_entry = (struct cxl_cel_entry *) cel;

	for (i = 0; i < cel_entries; i++) {
		u16 opcode = le16_to_cpu(cel_entry[i].opcode);
		struct cxl_mem_command *cmd = cxl_mem_find_command(opcode);

		if (!cmd) {
			dev_dbg(cxlds->dev,
				"Opcode 0x%04x unsupported by driver", opcode);
			continue;
		}

		set_bit(cmd->info.id, cxlds->enabled_cmds);
	}
}

static struct cxl_mbox_get_supported_logs *cxl_get_gsl(struct cxl_dev_state *cxlds)
{
	struct cxl_mbox_get_supported_logs *ret;
	int rc;

	ret = kvmalloc(cxlds->payload_size, GFP_KERNEL);
	if (!ret)
		return ERR_PTR(-ENOMEM);

	rc = cxl_mbox_send_cmd(cxlds, CXL_MBOX_OP_GET_SUPPORTED_LOGS, NULL, 0, ret,
			       cxlds->payload_size);
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
 * @cxlds: The device data for the operation
 *
 * Returns 0 if enumerate completed successfully.
 *
 * CXL devices have optional support for certain commands. This function will
 * determine the set of supported commands for the hardware and update the
 * enabled_cmds bitmap in the @cxlds.
 */
int cxl_enumerate_cmds(struct cxl_dev_state *cxlds)
{
	struct cxl_mbox_get_supported_logs *gsl;
	struct device *dev = cxlds->dev;
	struct cxl_mem_command *cmd;
	int i, rc;

	gsl = cxl_get_gsl(cxlds);
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

		rc = cxl_xfer_log(cxlds, &uuid, size, log);
		if (rc) {
			kvfree(log);
			goto out;
		}

		cxl_walk_cel(cxlds, size, log);
		kvfree(log);

		/* In case CEL was bogus, enable some default commands. */
		cxl_for_each_cmd(cmd)
			if (cmd->flags & CXL_CMD_FLAG_FORCE_ENABLE)
				set_bit(cmd->info.id, cxlds->enabled_cmds);

		/* Found the required CEL */
		rc = 0;
	}

out:
	kvfree(gsl);
	return rc;
}
EXPORT_SYMBOL_NS_GPL(cxl_enumerate_cmds, CXL);

/**
 * cxl_mem_get_partition_info - Get partition info
 * @cxlds: The device data for the operation
 *
 * Retrieve the current partition info for the device specified.  The active
 * values are the current capacity in bytes.  If not 0, the 'next' values are
 * the pending values, in bytes, which take affect on next cold reset.
 *
 * Return: 0 if no error: or the result of the mailbox command.
 *
 * See CXL @8.2.9.5.2.1 Get Partition Info
 */
static int cxl_mem_get_partition_info(struct cxl_dev_state *cxlds)
{
	struct cxl_mbox_get_partition_info {
		__le64 active_volatile_cap;
		__le64 active_persistent_cap;
		__le64 next_volatile_cap;
		__le64 next_persistent_cap;
	} __packed pi;
	int rc;

	rc = cxl_mbox_send_cmd(cxlds, CXL_MBOX_OP_GET_PARTITION_INFO, NULL, 0,
			       &pi, sizeof(pi));

	if (rc)
		return rc;

	cxlds->active_volatile_bytes =
		le64_to_cpu(pi.active_volatile_cap) * CXL_CAPACITY_MULTIPLIER;
	cxlds->active_persistent_bytes =
		le64_to_cpu(pi.active_persistent_cap) * CXL_CAPACITY_MULTIPLIER;
	cxlds->next_volatile_bytes =
		le64_to_cpu(pi.next_volatile_cap) * CXL_CAPACITY_MULTIPLIER;
	cxlds->next_persistent_bytes =
		le64_to_cpu(pi.next_volatile_cap) * CXL_CAPACITY_MULTIPLIER;

	return 0;
}

/**
 * cxl_dev_state_identify() - Send the IDENTIFY command to the device.
 * @cxlds: The device data for the operation
 *
 * Return: 0 if identify was executed successfully.
 *
 * This will dispatch the identify command to the device and on success populate
 * structures to be exported to sysfs.
 */
int cxl_dev_state_identify(struct cxl_dev_state *cxlds)
{
	/* See CXL 2.0 Table 175 Identify Memory Device Output Payload */
	struct cxl_mbox_identify id;
	int rc;

	rc = cxl_mbox_send_cmd(cxlds, CXL_MBOX_OP_IDENTIFY, NULL, 0, &id,
			       sizeof(id));
	if (rc < 0)
		return rc;

	cxlds->total_bytes =
		le64_to_cpu(id.total_capacity) * CXL_CAPACITY_MULTIPLIER;
	cxlds->volatile_only_bytes =
		le64_to_cpu(id.volatile_capacity) * CXL_CAPACITY_MULTIPLIER;
	cxlds->persistent_only_bytes =
		le64_to_cpu(id.persistent_capacity) * CXL_CAPACITY_MULTIPLIER;
	cxlds->partition_align_bytes =
		le64_to_cpu(id.partition_align) * CXL_CAPACITY_MULTIPLIER;

	dev_dbg(cxlds->dev,
		"Identify Memory Device\n"
		"     total_bytes = %#llx\n"
		"     volatile_only_bytes = %#llx\n"
		"     persistent_only_bytes = %#llx\n"
		"     partition_align_bytes = %#llx\n",
		cxlds->total_bytes, cxlds->volatile_only_bytes,
		cxlds->persistent_only_bytes, cxlds->partition_align_bytes);

	cxlds->lsa_size = le32_to_cpu(id.lsa_size);
	memcpy(cxlds->firmware_version, id.fw_revision, sizeof(id.fw_revision));

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_dev_state_identify, CXL);

int cxl_mem_create_range_info(struct cxl_dev_state *cxlds)
{
	int rc;

	if (cxlds->partition_align_bytes == 0) {
		cxlds->ram_range.start = 0;
		cxlds->ram_range.end = cxlds->volatile_only_bytes - 1;
		cxlds->pmem_range.start = cxlds->volatile_only_bytes;
		cxlds->pmem_range.end = cxlds->volatile_only_bytes +
				       cxlds->persistent_only_bytes - 1;
		return 0;
	}

	rc = cxl_mem_get_partition_info(cxlds);
	if (rc) {
		dev_err(cxlds->dev, "Failed to query partition information\n");
		return rc;
	}

	dev_dbg(cxlds->dev,
		"Get Partition Info\n"
		"     active_volatile_bytes = %#llx\n"
		"     active_persistent_bytes = %#llx\n"
		"     next_volatile_bytes = %#llx\n"
		"     next_persistent_bytes = %#llx\n",
		cxlds->active_volatile_bytes, cxlds->active_persistent_bytes,
		cxlds->next_volatile_bytes, cxlds->next_persistent_bytes);

	cxlds->ram_range.start = 0;
	cxlds->ram_range.end = cxlds->active_volatile_bytes - 1;

	cxlds->pmem_range.start = cxlds->active_volatile_bytes;
	cxlds->pmem_range.end =
		cxlds->active_volatile_bytes + cxlds->active_persistent_bytes - 1;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_mem_create_range_info, CXL);

struct cxl_dev_state *cxl_dev_state_create(struct device *dev)
{
	struct cxl_dev_state *cxlds;

	cxlds = devm_kzalloc(dev, sizeof(*cxlds), GFP_KERNEL);
	if (!cxlds) {
		dev_err(dev, "No memory available\n");
		return ERR_PTR(-ENOMEM);
	}

	mutex_init(&cxlds->mbox_mutex);
	cxlds->dev = dev;

	return cxlds;
}
EXPORT_SYMBOL_NS_GPL(cxl_dev_state_create, CXL);

static struct dentry *cxl_debugfs;

void __init cxl_mbox_init(void)
{
	struct dentry *mbox_debugfs;

	cxl_debugfs = debugfs_create_dir("cxl", NULL);
	mbox_debugfs = debugfs_create_dir("mbox", cxl_debugfs);
	debugfs_create_bool("raw_allow_all", 0600, mbox_debugfs,
			    &cxl_raw_allow_all);
}

void cxl_mbox_exit(void)
{
	debugfs_remove_recursive(cxl_debugfs);
}
