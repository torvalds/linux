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
 * You should have received a copy of the GNU General Public License
 * along with this program.
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

#define FWRT_DEBUGFS_READ_FILE_OPS(name)				\
static ssize_t iwl_dbgfs_##name##_read(struct iwl_fw_runtime *fwrt,	\
				       char *buf, size_t count,		\
				       loff_t *ppos);			\
static const struct file_operations iwl_dbgfs_##name##_ops = {		\
	.read = iwl_dbgfs_##name##_read,				\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
}

#define FWRT_DEBUGFS_WRITE_WRAPPER(name, buflen)			\
static ssize_t iwl_dbgfs_##name##_write(struct iwl_fw_runtime *fwrt,	\
					char *buf, size_t count,	\
					loff_t *ppos);			\
static ssize_t _iwl_dbgfs_##name##_write(struct file *file,		\
					 const char __user *user_buf,	\
					 size_t count, loff_t *ppos)	\
{									\
	struct iwl_fw_runtime *fwrt = file->private_data;		\
	char buf[buflen] = {};						\
	size_t buf_size = min(count, sizeof(buf) -  1);			\
									\
	if (copy_from_user(buf, user_buf, buf_size))			\
		return -EFAULT;						\
									\
	return iwl_dbgfs_##name##_write(fwrt, buf, buf_size, ppos);	\
}

#define FWRT_DEBUGFS_READ_WRITE_FILE_OPS(name, buflen)			\
FWRT_DEBUGFS_WRITE_WRAPPER(name, buflen)				\
static const struct file_operations iwl_dbgfs_##name##_ops = {		\
	.write = _iwl_dbgfs_##name##_write,				\
	.read = iwl_dbgfs_##name##_read,				\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
}

#define FWRT_DEBUGFS_WRITE_FILE_OPS(name, buflen)			\
FWRT_DEBUGFS_WRITE_WRAPPER(name, buflen)				\
static const struct file_operations iwl_dbgfs_##name##_ops = {		\
	.write = _iwl_dbgfs_##name##_write,				\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
}

#define FWRT_DEBUGFS_ADD_FILE_ALIAS(alias, name, parent, mode) do {	\
		if (!debugfs_create_file(alias, mode, parent, fwrt,	\
					 &iwl_dbgfs_##name##_ops))	\
			goto err;					\
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

static ssize_t iwl_dbgfs_timestamp_marker_write(struct iwl_fw_runtime *fwrt,
						char *buf, size_t count,
						loff_t *ppos)
{
	int ret;
	u32 delay;

	ret = kstrtou32(buf, 10, &delay);
	if (ret < 0)
		return ret;

	IWL_INFO(fwrt,
		 "starting timestamp_marker trigger with delay: %us\n",
		 delay);

	iwl_fw_cancel_timestamp(fwrt);

	fwrt->timestamp.delay = msecs_to_jiffies(delay * 1000);

	schedule_delayed_work(&fwrt->timestamp.wk,
			      round_jiffies_relative(fwrt->timestamp.delay));
	return count;
}

FWRT_DEBUGFS_WRITE_FILE_OPS(timestamp_marker, 10);

int iwl_fwrt_dbgfs_register(struct iwl_fw_runtime *fwrt,
			    struct dentry *dbgfs_dir)
{
	INIT_DELAYED_WORK(&fwrt->timestamp.wk, iwl_fw_timestamp_marker_wk);
	FWRT_DEBUGFS_ADD_FILE(timestamp_marker, dbgfs_dir, 0200);
	return 0;
err:
	IWL_ERR(fwrt, "Can't create the fwrt debugfs directory\n");
	return -ENOMEM;
}
