// SPDX-License-Identifier: GPL-2.0
/*
 * DebugFS interface for the NVMe target.
 * Copyright (c) 2022-2024 Shadow
 * Copyright (c) 2024 SUSE LLC
 */

#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include "nvmet.h"
#include "debugfs.h"

static struct dentry *nvmet_debugfs;

#define NVMET_DEBUGFS_ATTR(field) \
	static int field##_open(struct inode *inode, struct file *file) \
	{ return single_open(file, field##_show, inode->i_private); } \
	\
	static const struct file_operations field##_fops = { \
		.open = field##_open, \
		.read = seq_read, \
		.release = single_release, \
	}

#define NVMET_DEBUGFS_RW_ATTR(field) \
	static int field##_open(struct inode *inode, struct file *file) \
	{ return single_open(file, field##_show, inode->i_private); } \
	\
	static const struct file_operations field##_fops = { \
		.open = field##_open, \
		.read = seq_read, \
		.write = field##_write, \
		.release = single_release, \
	}

static int nvmet_ctrl_hostnqn_show(struct seq_file *m, void *p)
{
	struct nvmet_ctrl *ctrl = m->private;

	seq_puts(m, ctrl->hostnqn);
	return 0;
}
NVMET_DEBUGFS_ATTR(nvmet_ctrl_hostnqn);

static int nvmet_ctrl_kato_show(struct seq_file *m, void *p)
{
	struct nvmet_ctrl *ctrl = m->private;

	seq_printf(m, "%d\n", ctrl->kato);
	return 0;
}
NVMET_DEBUGFS_ATTR(nvmet_ctrl_kato);

static int nvmet_ctrl_port_show(struct seq_file *m, void *p)
{
	struct nvmet_ctrl *ctrl = m->private;

	seq_printf(m, "%d\n", le16_to_cpu(ctrl->port->disc_addr.portid));
	return 0;
}
NVMET_DEBUGFS_ATTR(nvmet_ctrl_port);

static const char *const csts_state_names[] = {
	[NVME_CSTS_RDY]		= "ready",
	[NVME_CSTS_CFS]		= "fatal",
	[NVME_CSTS_NSSRO]	= "reset",
	[NVME_CSTS_SHST_OCCUR]	= "shutdown",
	[NVME_CSTS_SHST_CMPLT]	= "completed",
	[NVME_CSTS_PP]		= "paused",
};

static int nvmet_ctrl_state_show(struct seq_file *m, void *p)
{
	struct nvmet_ctrl *ctrl = m->private;
	bool sep = false;
	int i;

	for (i = 0; i < 7; i++) {
		int state = BIT(i);

		if (!(ctrl->csts & state))
			continue;
		if (sep)
			seq_puts(m, "|");
		sep = true;
		if (csts_state_names[state])
			seq_puts(m, csts_state_names[state]);
		else
			seq_printf(m, "%d", state);
	}
	if (sep)
		seq_printf(m, "\n");
	return 0;
}

static ssize_t nvmet_ctrl_state_write(struct file *file, const char __user *buf,
				      size_t count, loff_t *ppos)
{
	struct seq_file *m = file->private_data;
	struct nvmet_ctrl *ctrl = m->private;
	char reset[16];

	if (count >= sizeof(reset))
		return -EINVAL;
	if (copy_from_user(reset, buf, count))
		return -EFAULT;
	if (!memcmp(reset, "fatal", 5))
		nvmet_ctrl_fatal_error(ctrl);
	else
		return -EINVAL;
	return count;
}
NVMET_DEBUGFS_RW_ATTR(nvmet_ctrl_state);

static int nvmet_ctrl_host_traddr_show(struct seq_file *m, void *p)
{
	struct nvmet_ctrl *ctrl = m->private;
	ssize_t size;
	char buf[NVMF_TRADDR_SIZE + 1];

	size = nvmet_ctrl_host_traddr(ctrl, buf, NVMF_TRADDR_SIZE);
	if (size < 0) {
		buf[0] = '\0';
		size = 0;
	}
	buf[size] = '\0';
	seq_printf(m, "%s\n", buf);
	return 0;
}
NVMET_DEBUGFS_ATTR(nvmet_ctrl_host_traddr);

#ifdef CONFIG_NVME_TARGET_TCP_TLS
static int nvmet_ctrl_tls_key_show(struct seq_file *m, void *p)
{
	struct nvmet_ctrl *ctrl = m->private;
	key_serial_t keyid = nvmet_queue_tls_keyid(ctrl->sqs[0]);

	seq_printf(m, "%08x\n", keyid);
	return 0;
}
NVMET_DEBUGFS_ATTR(nvmet_ctrl_tls_key);

static int nvmet_ctrl_tls_concat_show(struct seq_file *m, void *p)
{
	struct nvmet_ctrl *ctrl = m->private;

	seq_printf(m, "%d\n", ctrl->concat);
	return 0;
}
NVMET_DEBUGFS_ATTR(nvmet_ctrl_tls_concat);
#endif

int nvmet_debugfs_ctrl_setup(struct nvmet_ctrl *ctrl)
{
	char name[32];
	struct dentry *parent = ctrl->subsys->debugfs_dir;
	int ret;

	if (!parent)
		return -ENODEV;
	snprintf(name, sizeof(name), "ctrl%d", ctrl->cntlid);
	ctrl->debugfs_dir = debugfs_create_dir(name, parent);
	if (IS_ERR(ctrl->debugfs_dir)) {
		ret = PTR_ERR(ctrl->debugfs_dir);
		ctrl->debugfs_dir = NULL;
		return ret;
	}
	debugfs_create_file("port", S_IRUSR, ctrl->debugfs_dir, ctrl,
			    &nvmet_ctrl_port_fops);
	debugfs_create_file("hostnqn", S_IRUSR, ctrl->debugfs_dir, ctrl,
			    &nvmet_ctrl_hostnqn_fops);
	debugfs_create_file("kato", S_IRUSR, ctrl->debugfs_dir, ctrl,
			    &nvmet_ctrl_kato_fops);
	debugfs_create_file("state", S_IRUSR | S_IWUSR, ctrl->debugfs_dir, ctrl,
			    &nvmet_ctrl_state_fops);
	debugfs_create_file("host_traddr", S_IRUSR, ctrl->debugfs_dir, ctrl,
			    &nvmet_ctrl_host_traddr_fops);
#ifdef CONFIG_NVME_TARGET_TCP_TLS
	debugfs_create_file("tls_concat", S_IRUSR, ctrl->debugfs_dir, ctrl,
			    &nvmet_ctrl_tls_concat_fops);
	debugfs_create_file("tls_key", S_IRUSR, ctrl->debugfs_dir, ctrl,
			    &nvmet_ctrl_tls_key_fops);
#endif
	return 0;
}

void nvmet_debugfs_ctrl_free(struct nvmet_ctrl *ctrl)
{
	debugfs_remove_recursive(ctrl->debugfs_dir);
}

int nvmet_debugfs_subsys_setup(struct nvmet_subsys *subsys)
{
	int ret = 0;

	subsys->debugfs_dir = debugfs_create_dir(subsys->subsysnqn,
						 nvmet_debugfs);
	if (IS_ERR(subsys->debugfs_dir)) {
		ret = PTR_ERR(subsys->debugfs_dir);
		subsys->debugfs_dir = NULL;
	}
	return ret;
}

void nvmet_debugfs_subsys_free(struct nvmet_subsys *subsys)
{
	debugfs_remove_recursive(subsys->debugfs_dir);
}

int __init nvmet_init_debugfs(void)
{
	struct dentry *parent;

	parent = debugfs_create_dir("nvmet", NULL);
	if (IS_ERR(parent)) {
		pr_warn("%s: failed to create debugfs directory\n", "nvmet");
		return PTR_ERR(parent);
	}
	nvmet_debugfs = parent;
	return 0;
}

void nvmet_exit_debugfs(void)
{
	debugfs_remove_recursive(nvmet_debugfs);
}
