/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright (C) 2018 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright (C) 2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#include "api/commands.h"
#include "debugfs.h"
#include "dbg.h"

#define FWRT_DEBUGFS_OPEN_WRAPPER(name, buflen, argtype)		\
struct dbgfs_##name##_data {						\
	argtype *arg;							\
	bool read_done;							\
	ssize_t rlen;							\
	char rbuf[buflen];						\
};									\
static int _iwl_dbgfs_##name##_open(struct inode *inode,		\
				    struct file *file)			\
{									\
	struct dbgfs_##name##_data *data;				\
									\
	data = kzalloc(sizeof(*data), GFP_KERNEL);			\
	if (!data)							\
		return -ENOMEM;						\
									\
	data->read_done = false;					\
	data->arg = inode->i_private;					\
	file->private_data = data;					\
									\
	return 0;							\
}

#define FWRT_DEBUGFS_READ_WRAPPER(name)					\
static ssize_t _iwl_dbgfs_##name##_read(struct file *file,		\
					char __user *user_buf,		\
					size_t count, loff_t *ppos)	\
{									\
	struct dbgfs_##name##_data *data = file->private_data;		\
									\
	if (!data->read_done) {						\
		data->read_done = true;					\
		data->rlen = iwl_dbgfs_##name##_read(data->arg,		\
						     sizeof(data->rbuf),\
						     data->rbuf);	\
	}								\
									\
	if (data->rlen < 0)						\
		return data->rlen;					\
	return simple_read_from_buffer(user_buf, count, ppos,		\
				       data->rbuf, data->rlen);		\
}

static int _iwl_dbgfs_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);

	return 0;
}

#define _FWRT_DEBUGFS_READ_FILE_OPS(name, buflen, argtype)		\
FWRT_DEBUGFS_OPEN_WRAPPER(name, buflen, argtype)			\
FWRT_DEBUGFS_READ_WRAPPER(name)						\
static const struct file_operations iwl_dbgfs_##name##_ops = {		\
	.read = _iwl_dbgfs_##name##_read,				\
	.open = _iwl_dbgfs_##name##_open,				\
	.llseek = generic_file_llseek,					\
	.release = _iwl_dbgfs_release,					\
}

#define FWRT_DEBUGFS_WRITE_WRAPPER(name, buflen, argtype)		\
static ssize_t _iwl_dbgfs_##name##_write(struct file *file,		\
					 const char __user *user_buf,	\
					 size_t count, loff_t *ppos)	\
{									\
	argtype *arg =							\
		((struct dbgfs_##name##_data *)file->private_data)->arg;\
	char buf[buflen] = {};						\
	size_t buf_size = min(count, sizeof(buf) -  1);			\
									\
	if (copy_from_user(buf, user_buf, buf_size))			\
		return -EFAULT;						\
									\
	return iwl_dbgfs_##name##_write(arg, buf, buf_size);		\
}

#define _FWRT_DEBUGFS_READ_WRITE_FILE_OPS(name, buflen, argtype)	\
FWRT_DEBUGFS_OPEN_WRAPPER(name, buflen, argtype)			\
FWRT_DEBUGFS_WRITE_WRAPPER(name, buflen, argtype)			\
FWRT_DEBUGFS_READ_WRAPPER(name)						\
static const struct file_operations iwl_dbgfs_##name##_ops = {		\
	.write = _iwl_dbgfs_##name##_write,				\
	.read = _iwl_dbgfs_##name##_read,				\
	.open = _iwl_dbgfs_##name##_open,				\
	.llseek = generic_file_llseek,					\
	.release = _iwl_dbgfs_release,					\
}

#define _FWRT_DEBUGFS_WRITE_FILE_OPS(name, buflen, argtype)		\
FWRT_DEBUGFS_OPEN_WRAPPER(name, buflen, argtype)			\
FWRT_DEBUGFS_WRITE_WRAPPER(name, buflen, argtype)			\
static const struct file_operations iwl_dbgfs_##name##_ops = {		\
	.write = _iwl_dbgfs_##name##_write,				\
	.open = _iwl_dbgfs_##name##_open,				\
	.llseek = generic_file_llseek,					\
	.release = _iwl_dbgfs_release,					\
}

#define FWRT_DEBUGFS_READ_FILE_OPS(name, bufsz)				\
	_FWRT_DEBUGFS_READ_FILE_OPS(name, bufsz, struct iwl_fw_runtime)

#define FWRT_DEBUGFS_WRITE_FILE_OPS(name, bufsz)			\
	_FWRT_DEBUGFS_WRITE_FILE_OPS(name, bufsz, struct iwl_fw_runtime)

#define FWRT_DEBUGFS_READ_WRITE_FILE_OPS(name, bufsz)			\
	_FWRT_DEBUGFS_READ_WRITE_FILE_OPS(name, bufsz, struct iwl_fw_runtime)

#define FWRT_DEBUGFS_ADD_FILE_ALIAS(alias, name, parent, mode) do {	\
	debugfs_create_file(alias, mode, parent, fwrt,			\
			    &iwl_dbgfs_##name##_ops);			\
	} while (0)
#define FWRT_DEBUGFS_ADD_FILE(name, parent, mode) \
	FWRT_DEBUGFS_ADD_FILE_ALIAS(#name, name, parent, mode)

static int iwl_fw_send_timestamp_marker_cmd(struct iwl_fw_runtime *fwrt)
{
	struct iwl_mvm_marker marker = {
		.dw_len = sizeof(struct iwl_mvm_marker) / 4,
		.marker_id = MARKER_ID_SYNC_CLOCK,

		/* the real timestamp is taken from the ftrace clock
		 * this is for finding the match between fw and kernel logs
		 */
		.timestamp = cpu_to_le64(fwrt->timestamp.seq++),
	};

	struct iwl_host_cmd hcmd = {
		.id = MARKER_CMD,
		.flags = CMD_ASYNC,
		.data[0] = &marker,
		.len[0] = sizeof(marker),
	};

	return iwl_trans_send_cmd(fwrt->trans, &hcmd);
}

static void iwl_fw_timestamp_marker_wk(struct work_struct *work)
{
	int ret;
	struct iwl_fw_runtime *fwrt =
		container_of(work, struct iwl_fw_runtime, timestamp.wk.work);
	unsigned long delay = fwrt->timestamp.delay;

	ret = iwl_fw_send_timestamp_marker_cmd(fwrt);
	if (!ret && delay)
		schedule_delayed_work(&fwrt->timestamp.wk,
				      round_jiffies_relative(delay));
	else
		IWL_INFO(fwrt,
			 "stopping timestamp_marker, ret: %d, delay: %u\n",
			 ret, jiffies_to_msecs(delay) / 1000);
}

void iwl_fw_trigger_timestamp(struct iwl_fw_runtime *fwrt, u32 delay)
{
	IWL_INFO(fwrt,
		 "starting timestamp_marker trigger with delay: %us\n",
		 delay);

	iwl_fw_cancel_timestamp(fwrt);

	fwrt->timestamp.delay = msecs_to_jiffies(delay * 1000);

	schedule_delayed_work(&fwrt->timestamp.wk,
			      round_jiffies_relative(fwrt->timestamp.delay));
}

static ssize_t iwl_dbgfs_timestamp_marker_write(struct iwl_fw_runtime *fwrt,
						char *buf, size_t count)
{
	int ret;
	u32 delay;

	ret = kstrtou32(buf, 10, &delay);
	if (ret < 0)
		return ret;

	iwl_fw_trigger_timestamp(fwrt, delay);

	return count;
}

static ssize_t iwl_dbgfs_timestamp_marker_read(struct iwl_fw_runtime *fwrt,
					       size_t size, char *buf)
{
	u32 delay_secs = jiffies_to_msecs(fwrt->timestamp.delay) / 1000;

	return scnprintf(buf, size, "%d\n", delay_secs);
}

FWRT_DEBUGFS_READ_WRITE_FILE_OPS(timestamp_marker, 16);

struct hcmd_write_data {
	__be32 cmd_id;
	__be32 flags;
	__be16 length;
	u8 data[0];
} __packed;

static ssize_t iwl_dbgfs_send_hcmd_write(struct iwl_fw_runtime *fwrt, char *buf,
					 size_t count)
{
	size_t header_size = (sizeof(u32) * 2 + sizeof(u16)) * 2;
	size_t data_size = (count - 1) / 2;
	int ret;
	struct hcmd_write_data *data;
	struct iwl_host_cmd hcmd = {
		.len = { 0, },
		.data = { NULL, },
	};

	if (fwrt->ops && fwrt->ops->fw_running &&
	    !fwrt->ops->fw_running(fwrt->ops_ctx))
		return -EIO;

	if (count < header_size + 1 || count > 1024 * 4)
		return -EINVAL;

	data = kmalloc(data_size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = hex2bin((u8 *)data, buf, data_size);
	if (ret)
		goto out;

	hcmd.id = be32_to_cpu(data->cmd_id);
	hcmd.flags = be32_to_cpu(data->flags);
	hcmd.len[0] = be16_to_cpu(data->length);
	hcmd.data[0] = data->data;

	if (count != header_size + hcmd.len[0] * 2 + 1) {
		IWL_ERR(fwrt,
			"host command data size does not match header length\n");
		ret = -EINVAL;
		goto out;
	}

	if (fwrt->ops && fwrt->ops->send_hcmd)
		ret = fwrt->ops->send_hcmd(fwrt->ops_ctx, &hcmd);
	else
		ret = -EPERM;

	if (ret < 0)
		goto out;

	if (hcmd.flags & CMD_WANT_SKB)
		iwl_free_resp(&hcmd);
out:
	kfree(data);
	return ret ?: count;
}

FWRT_DEBUGFS_WRITE_FILE_OPS(send_hcmd, 512);

static ssize_t iwl_dbgfs_fw_dbg_domain_write(struct iwl_fw_runtime *fwrt,
					     char *buf, size_t count)
{
	u32 new_domain;
	int ret;

	if (!iwl_trans_fw_running(fwrt->trans))
		return -EIO;

	ret = kstrtou32(buf, 0, &new_domain);
	if (ret)
		return ret;

	if (new_domain != fwrt->trans->dbg.domains_bitmap) {
		ret = iwl_dbg_tlv_gen_active_trigs(fwrt, new_domain);
		if (ret)
			return ret;

		iwl_dbg_tlv_time_point(fwrt, IWL_FW_INI_TIME_POINT_PERIODIC,
				       NULL);
	}

	return count;
}

static ssize_t iwl_dbgfs_fw_dbg_domain_read(struct iwl_fw_runtime *fwrt,
					    size_t size, char *buf)
{
	return scnprintf(buf, size, "0x%08x\n",
			 fwrt->trans->dbg.domains_bitmap);
}

FWRT_DEBUGFS_READ_WRITE_FILE_OPS(fw_dbg_domain, 20);

void iwl_fwrt_dbgfs_register(struct iwl_fw_runtime *fwrt,
			    struct dentry *dbgfs_dir)
{
	INIT_DELAYED_WORK(&fwrt->timestamp.wk, iwl_fw_timestamp_marker_wk);
	FWRT_DEBUGFS_ADD_FILE(timestamp_marker, dbgfs_dir, 0200);
	FWRT_DEBUGFS_ADD_FILE(send_hcmd, dbgfs_dir, 0200);
	FWRT_DEBUGFS_ADD_FILE(fw_dbg_domain, dbgfs_dir, 0600);
}
