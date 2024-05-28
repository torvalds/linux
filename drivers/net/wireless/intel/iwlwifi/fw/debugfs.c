// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2012-2014, 2018-2023 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#include "api/commands.h"
#include "debugfs.h"
#include "dbg.h"
#include <linux/seq_file.h>

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

static int iwl_dbgfs_enabled_severities_write(struct iwl_fw_runtime *fwrt,
					      char *buf, size_t count)
{
	struct iwl_dbg_host_event_cfg_cmd event_cfg;
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(DEBUG_GROUP, HOST_EVENT_CFG),
		.flags = CMD_ASYNC,
		.data[0] = &event_cfg,
		.len[0] = sizeof(event_cfg),
	};
	u32 enabled_severities;
	int ret = kstrtou32(buf, 10, &enabled_severities);

	if (ret < 0)
		return ret;

	event_cfg.enabled_severities = cpu_to_le32(enabled_severities);

	if (fwrt->ops && fwrt->ops->send_hcmd)
		ret = fwrt->ops->send_hcmd(fwrt->ops_ctx, &hcmd);
	else
		ret = -EPERM;

	IWL_INFO(fwrt,
		 "sent host event cfg with enabled_severities: %u, ret: %d\n",
		 enabled_severities, ret);

	return ret ?: count;
}

FWRT_DEBUGFS_WRITE_FILE_OPS(enabled_severities, 16);

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
	u8 data[];
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

static ssize_t iwl_dbgfs_fw_dbg_domain_read(struct iwl_fw_runtime *fwrt,
					    size_t size, char *buf)
{
	return scnprintf(buf, size, "0x%08x\n",
			 fwrt->trans->dbg.domains_bitmap);
}

FWRT_DEBUGFS_READ_FILE_OPS(fw_dbg_domain, 20);

struct iwl_dbgfs_fw_info_priv {
	struct iwl_fw_runtime *fwrt;
};

struct iwl_dbgfs_fw_info_state {
	loff_t pos;
};

static void *iwl_dbgfs_fw_info_seq_next(struct seq_file *seq,
					void *v, loff_t *pos)
{
	struct iwl_dbgfs_fw_info_state *state = v;
	struct iwl_dbgfs_fw_info_priv *priv = seq->private;
	const struct iwl_fw *fw = priv->fwrt->fw;

	*pos = ++state->pos;
	if (*pos >= fw->ucode_capa.n_cmd_versions) {
		kfree(state);
		return NULL;
	}

	return state;
}

static void iwl_dbgfs_fw_info_seq_stop(struct seq_file *seq,
				       void *v)
{
	kfree(v);
}

static void *iwl_dbgfs_fw_info_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct iwl_dbgfs_fw_info_priv *priv = seq->private;
	const struct iwl_fw *fw = priv->fwrt->fw;
	struct iwl_dbgfs_fw_info_state *state;

	if (*pos >= fw->ucode_capa.n_cmd_versions)
		return NULL;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;
	state->pos = *pos;
	return state;
};

static int iwl_dbgfs_fw_info_seq_show(struct seq_file *seq, void *v)
{
	struct iwl_dbgfs_fw_info_state *state = v;
	struct iwl_dbgfs_fw_info_priv *priv = seq->private;
	const struct iwl_fw *fw = priv->fwrt->fw;
	const struct iwl_fw_cmd_version *ver;
	u32 cmd_id;
	int has_capa;

	if (!state->pos) {
		seq_puts(seq, "fw_capa:\n");
		has_capa = fw_has_capa(&fw->ucode_capa,
				       IWL_UCODE_TLV_CAPA_PPAG_CHINA_BIOS_SUPPORT) ? 1 : 0;
		seq_printf(seq,
			   "    %d: %d\n",
			   IWL_UCODE_TLV_CAPA_PPAG_CHINA_BIOS_SUPPORT,
			   has_capa);
		has_capa = fw_has_capa(&fw->ucode_capa,
				       IWL_UCODE_TLV_CAPA_CHINA_22_REG_SUPPORT) ? 1 : 0;
		seq_printf(seq,
			   "    %d: %d\n",
			   IWL_UCODE_TLV_CAPA_CHINA_22_REG_SUPPORT,
			   has_capa);
		seq_puts(seq, "fw_api_ver:\n");
	}

	ver = &fw->ucode_capa.cmd_versions[state->pos];

	cmd_id = WIDE_ID(ver->group, ver->cmd);

	seq_printf(seq, "  0x%04x:\n", cmd_id);
	seq_printf(seq, "    name: %s\n",
		   iwl_get_cmd_string(priv->fwrt->trans, cmd_id));
	seq_printf(seq, "    cmd_ver: %d\n", ver->cmd_ver);
	seq_printf(seq, "    notif_ver: %d\n", ver->notif_ver);
	return 0;
}

static const struct seq_operations iwl_dbgfs_info_seq_ops = {
	.start = iwl_dbgfs_fw_info_seq_start,
	.next = iwl_dbgfs_fw_info_seq_next,
	.stop = iwl_dbgfs_fw_info_seq_stop,
	.show = iwl_dbgfs_fw_info_seq_show,
};

static int iwl_dbgfs_fw_info_open(struct inode *inode, struct file *filp)
{
	struct iwl_dbgfs_fw_info_priv *priv;

	priv = __seq_open_private(filp, &iwl_dbgfs_info_seq_ops,
				  sizeof(*priv));

	if (!priv)
		return -ENOMEM;

	priv->fwrt = inode->i_private;
	return 0;
}

static const struct file_operations iwl_dbgfs_fw_info_ops = {
	.owner = THIS_MODULE,
	.open = iwl_dbgfs_fw_info_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_private,
};

void iwl_fwrt_dbgfs_register(struct iwl_fw_runtime *fwrt,
			    struct dentry *dbgfs_dir)
{
	INIT_DELAYED_WORK(&fwrt->timestamp.wk, iwl_fw_timestamp_marker_wk);
	FWRT_DEBUGFS_ADD_FILE(timestamp_marker, dbgfs_dir, 0200);
	FWRT_DEBUGFS_ADD_FILE(fw_info, dbgfs_dir, 0200);
	FWRT_DEBUGFS_ADD_FILE(send_hcmd, dbgfs_dir, 0200);
	FWRT_DEBUGFS_ADD_FILE(enabled_severities, dbgfs_dir, 0200);
	FWRT_DEBUGFS_ADD_FILE(fw_dbg_domain, dbgfs_dir, 0400);
}
