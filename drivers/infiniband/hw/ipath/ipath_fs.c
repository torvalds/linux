/*
 * Copyright (c) 2006 QLogic, Inc. All rights reserved.
 * Copyright (c) 2006 PathScale, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/namei.h>
#include <linux/pci.h>

#include "ipath_kernel.h"

#define IPATHFS_MAGIC 0x726a77

static struct super_block *ipath_super;

static int ipathfs_mknod(struct inode *dir, struct dentry *dentry,
			 int mode, struct file_operations *fops,
			 void *data)
{
	int error;
	struct inode *inode = new_inode(dir->i_sb);

	if (!inode) {
		error = -EPERM;
		goto bail;
	}

	inode->i_mode = mode;
	inode->i_uid = 0;
	inode->i_gid = 0;
	inode->i_blocks = 0;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_private = data;
	if ((mode & S_IFMT) == S_IFDIR) {
		inode->i_op = &simple_dir_inode_operations;
		inc_nlink(inode);
		inc_nlink(dir);
	}

	inode->i_fop = fops;

	d_instantiate(dentry, inode);
	error = 0;

bail:
	return error;
}

static int create_file(const char *name, mode_t mode,
		       struct dentry *parent, struct dentry **dentry,
		       struct file_operations *fops, void *data)
{
	int error;

	*dentry = NULL;
	mutex_lock(&parent->d_inode->i_mutex);
	*dentry = lookup_one_len(name, parent, strlen(name));
	if (!IS_ERR(dentry))
		error = ipathfs_mknod(parent->d_inode, *dentry,
				      mode, fops, data);
	else
		error = PTR_ERR(dentry);
	mutex_unlock(&parent->d_inode->i_mutex);

	return error;
}

static ssize_t atomic_stats_read(struct file *file, char __user *buf,
				 size_t count, loff_t *ppos)
{
	return simple_read_from_buffer(buf, count, ppos, &ipath_stats,
				       sizeof ipath_stats);
}

static struct file_operations atomic_stats_ops = {
	.read = atomic_stats_read,
};

#define NUM_COUNTERS sizeof(struct infinipath_counters) / sizeof(u64)

static ssize_t atomic_counters_read(struct file *file, char __user *buf,
				    size_t count, loff_t *ppos)
{
	u64 counters[NUM_COUNTERS];
	u16 i;
	struct ipath_devdata *dd;

	dd = file->f_path.dentry->d_inode->i_private;

	for (i = 0; i < NUM_COUNTERS; i++)
		counters[i] = ipath_snap_cntr(dd, i);

	return simple_read_from_buffer(buf, count, ppos, counters,
				       sizeof counters);
}

static struct file_operations atomic_counters_ops = {
	.read = atomic_counters_read,
};

static ssize_t atomic_node_info_read(struct file *file, char __user *buf,
				     size_t count, loff_t *ppos)
{
	u32 nodeinfo[10];
	struct ipath_devdata *dd;
	u64 guid;

	dd = file->f_path.dentry->d_inode->i_private;

	guid = be64_to_cpu(dd->ipath_guid);

	nodeinfo[0] =			/* BaseVersion is SMA */
		/* ClassVersion is SMA */
		(1 << 8)		/* NodeType  */
		| (1 << 0);		/* NumPorts */
	nodeinfo[1] = (u32) (guid >> 32);
	nodeinfo[2] = (u32) (guid & 0xffffffff);
	/* PortGUID == SystemImageGUID for us */
	nodeinfo[3] = nodeinfo[1];
	/* PortGUID == SystemImageGUID for us */
	nodeinfo[4] = nodeinfo[2];
	/* PortGUID == NodeGUID for us */
	nodeinfo[5] = nodeinfo[3];
	/* PortGUID == NodeGUID for us */
	nodeinfo[6] = nodeinfo[4];
	nodeinfo[7] = (4 << 16) /* we support 4 pkeys */
		| (dd->ipath_deviceid << 0);
	/* our chip version as 16 bits major, 16 bits minor */
	nodeinfo[8] = dd->ipath_minrev | (dd->ipath_majrev << 16);
	nodeinfo[9] = (dd->ipath_unit << 24) | (dd->ipath_vendorid << 0);

	return simple_read_from_buffer(buf, count, ppos, nodeinfo,
				       sizeof nodeinfo);
}

static struct file_operations atomic_node_info_ops = {
	.read = atomic_node_info_read,
};

static ssize_t atomic_port_info_read(struct file *file, char __user *buf,
				     size_t count, loff_t *ppos)
{
	u32 portinfo[13];
	u32 tmp, tmp2;
	struct ipath_devdata *dd;

	dd = file->f_path.dentry->d_inode->i_private;

	/* so we only initialize non-zero fields. */
	memset(portinfo, 0, sizeof portinfo);

	/*
	 * Notimpl yet M_Key (64)
	 * Notimpl yet GID (64)
	 */

	portinfo[4] = (dd->ipath_lid << 16);

	/*
	 * Notimpl yet SMLID.
	 * CapabilityMask is 0, we don't support any of these
	 * DiagCode is 0; we don't store any diag info for now Notimpl yet
	 * M_KeyLeasePeriod (we don't support M_Key)
	 */

	/* LocalPortNum is whichever port number they ask for */
	portinfo[7] = (dd->ipath_unit << 24)
		/* LinkWidthEnabled */
		| (2 << 16)
		/* LinkWidthSupported (really 2, but not IB valid) */
		| (3 << 8)
		/* LinkWidthActive */
		| (2 << 0);
	tmp = dd->ipath_lastibcstat & IPATH_IBSTATE_MASK;
	tmp2 = 5;
	if (tmp == IPATH_IBSTATE_INIT)
		tmp = 2;
	else if (tmp == IPATH_IBSTATE_ARM)
		tmp = 3;
	else if (tmp == IPATH_IBSTATE_ACTIVE)
		tmp = 4;
	else {
		tmp = 0;	/* down */
		tmp2 = tmp & 0xf;
	}

	portinfo[8] = (1 << 28)	/* LinkSpeedSupported */
		| (tmp << 24)	/* PortState */
		| (tmp2 << 20)	/* PortPhysicalState */
		| (2 << 16)

		/* LinkDownDefaultState */
		/* M_KeyProtectBits == 0 */
		/* NotImpl yet LMC == 0 (we can support all values) */
		| (1 << 4)	/* LinkSpeedActive */
		| (1 << 0);	/* LinkSpeedEnabled */
	switch (dd->ipath_ibmtu) {
	case 4096:
		tmp = 5;
		break;
	case 2048:
		tmp = 4;
		break;
	case 1024:
		tmp = 3;
		break;
	case 512:
		tmp = 2;
		break;
	case 256:
		tmp = 1;
		break;
	default:		/* oops, something is wrong */
		ipath_dbg("Problem, ipath_ibmtu 0x%x not a valid IB MTU, "
			  "treat as 2048\n", dd->ipath_ibmtu);
		tmp = 4;
		break;
	}
	portinfo[9] = (tmp << 28)
		/* NeighborMTU */
		/* Notimpl MasterSMSL */
		| (1 << 20)

		/* VLCap */
		/* Notimpl InitType (actually, an SMA decision) */
		/* VLHighLimit is 0 (only one VL) */
		; /* VLArbitrationHighCap is 0 (only one VL) */
	portinfo[10] = 	/* VLArbitrationLowCap is 0 (only one VL) */
		/* InitTypeReply is SMA decision */
		(5 << 16)	/* MTUCap 4096 */
		| (7 << 13)	/* VLStallCount */
		| (0x1f << 8)	/* HOQLife */
		| (1 << 4)

		/* OperationalVLs 0 */
		/* PartitionEnforcementInbound */
		/* PartitionEnforcementOutbound not enforced */
		/* FilterRawinbound not enforced */
		;		/* FilterRawOutbound not enforced */
	/* M_KeyViolations are not counted by hardware, SMA can count */
	tmp = ipath_read_creg32(dd, dd->ipath_cregs->cr_errpkey);
	/* P_KeyViolations are counted by hardware. */
	portinfo[11] = ((tmp & 0xffff) << 0);
	portinfo[12] =
		/* Q_KeyViolations are not counted by hardware */
		(1 << 8)

		/* GUIDCap */
		/* SubnetTimeOut handled by SMA */
		/* RespTimeValue handled by SMA */
		;
	/* LocalPhyErrors are programmed to max */
	portinfo[12] |= (0xf << 20)
		| (0xf << 16)   /* OverRunErrors are programmed to max */
		;

	return simple_read_from_buffer(buf, count, ppos, portinfo,
				       sizeof portinfo);
}

static struct file_operations atomic_port_info_ops = {
	.read = atomic_port_info_read,
};

static ssize_t flash_read(struct file *file, char __user *buf,
			  size_t count, loff_t *ppos)
{
	struct ipath_devdata *dd;
	ssize_t ret;
	loff_t pos;
	char *tmp;

	pos = *ppos;

	if ( pos < 0) {
		ret = -EINVAL;
		goto bail;
	}

	if (pos >= sizeof(struct ipath_flash)) {
		ret = 0;
		goto bail;
	}

	if (count > sizeof(struct ipath_flash) - pos)
		count = sizeof(struct ipath_flash) - pos;

	tmp = kmalloc(count, GFP_KERNEL);
	if (!tmp) {
		ret = -ENOMEM;
		goto bail;
	}

	dd = file->f_path.dentry->d_inode->i_private;
	if (ipath_eeprom_read(dd, pos, tmp, count)) {
		ipath_dev_err(dd, "failed to read from flash\n");
		ret = -ENXIO;
		goto bail_tmp;
	}

	if (copy_to_user(buf, tmp, count)) {
		ret = -EFAULT;
		goto bail_tmp;
	}

	*ppos = pos + count;
	ret = count;

bail_tmp:
	kfree(tmp);

bail:
	return ret;
}

static ssize_t flash_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct ipath_devdata *dd;
	ssize_t ret;
	loff_t pos;
	char *tmp;

	pos = *ppos;

	if (pos != 0) {
		ret = -EINVAL;
		goto bail;
	}

	if (count != sizeof(struct ipath_flash)) {
		ret = -EINVAL;
		goto bail;
	}

	tmp = kmalloc(count, GFP_KERNEL);
	if (!tmp) {
		ret = -ENOMEM;
		goto bail;
	}

	if (copy_from_user(tmp, buf, count)) {
		ret = -EFAULT;
		goto bail_tmp;
	}

	dd = file->f_path.dentry->d_inode->i_private;
	if (ipath_eeprom_write(dd, pos, tmp, count)) {
		ret = -ENXIO;
		ipath_dev_err(dd, "failed to write to flash\n");
		goto bail_tmp;
	}

	*ppos = pos + count;
	ret = count;

bail_tmp:
	kfree(tmp);

bail:
	return ret;
}

static struct file_operations flash_ops = {
	.read = flash_read,
	.write = flash_write,
};

static int create_device_files(struct super_block *sb,
			       struct ipath_devdata *dd)
{
	struct dentry *dir, *tmp;
	char unit[10];
	int ret;

	snprintf(unit, sizeof unit, "%02d", dd->ipath_unit);
	ret = create_file(unit, S_IFDIR|S_IRUGO|S_IXUGO, sb->s_root, &dir,
			  (struct file_operations *) &simple_dir_operations,
			  dd);
	if (ret) {
		printk(KERN_ERR "create_file(%s) failed: %d\n", unit, ret);
		goto bail;
	}

	ret = create_file("atomic_counters", S_IFREG|S_IRUGO, dir, &tmp,
			  &atomic_counters_ops, dd);
	if (ret) {
		printk(KERN_ERR "create_file(%s/atomic_counters) "
		       "failed: %d\n", unit, ret);
		goto bail;
	}

	ret = create_file("node_info", S_IFREG|S_IRUGO, dir, &tmp,
			  &atomic_node_info_ops, dd);
	if (ret) {
		printk(KERN_ERR "create_file(%s/node_info) "
		       "failed: %d\n", unit, ret);
		goto bail;
	}

	ret = create_file("port_info", S_IFREG|S_IRUGO, dir, &tmp,
			  &atomic_port_info_ops, dd);
	if (ret) {
		printk(KERN_ERR "create_file(%s/port_info) "
		       "failed: %d\n", unit, ret);
		goto bail;
	}

	ret = create_file("flash", S_IFREG|S_IWUSR|S_IRUGO, dir, &tmp,
			  &flash_ops, dd);
	if (ret) {
		printk(KERN_ERR "create_file(%s/flash) "
		       "failed: %d\n", unit, ret);
		goto bail;
	}

bail:
	return ret;
}

static void remove_file(struct dentry *parent, char *name)
{
	struct dentry *tmp;

	tmp = lookup_one_len(name, parent, strlen(name));

	spin_lock(&dcache_lock);
	spin_lock(&tmp->d_lock);
	if (!(d_unhashed(tmp) && tmp->d_inode)) {
		dget_locked(tmp);
		__d_drop(tmp);
		spin_unlock(&tmp->d_lock);
		spin_unlock(&dcache_lock);
		simple_unlink(parent->d_inode, tmp);
	} else {
		spin_unlock(&tmp->d_lock);
		spin_unlock(&dcache_lock);
	}
}

static int remove_device_files(struct super_block *sb,
			       struct ipath_devdata *dd)
{
	struct dentry *dir, *root;
	char unit[10];
	int ret;

	root = dget(sb->s_root);
	mutex_lock(&root->d_inode->i_mutex);
	snprintf(unit, sizeof unit, "%02d", dd->ipath_unit);
	dir = lookup_one_len(unit, root, strlen(unit));

	if (IS_ERR(dir)) {
		ret = PTR_ERR(dir);
		printk(KERN_ERR "Lookup of %s failed\n", unit);
		goto bail;
	}

	remove_file(dir, "flash");
	remove_file(dir, "port_info");
	remove_file(dir, "node_info");
	remove_file(dir, "atomic_counters");
	d_delete(dir);
	ret = simple_rmdir(root->d_inode, dir);

bail:
	mutex_unlock(&root->d_inode->i_mutex);
	dput(root);
	return ret;
}

static int ipathfs_fill_super(struct super_block *sb, void *data,
			      int silent)
{
	struct ipath_devdata *dd, *tmp;
	unsigned long flags;
	int ret;

	static struct tree_descr files[] = {
		[1] = {"atomic_stats", &atomic_stats_ops, S_IRUGO},
		{""},
	};

	ret = simple_fill_super(sb, IPATHFS_MAGIC, files);
	if (ret) {
		printk(KERN_ERR "simple_fill_super failed: %d\n", ret);
		goto bail;
	}

	spin_lock_irqsave(&ipath_devs_lock, flags);

	list_for_each_entry_safe(dd, tmp, &ipath_dev_list, ipath_list) {
		spin_unlock_irqrestore(&ipath_devs_lock, flags);
		ret = create_device_files(sb, dd);
		if (ret) {
			deactivate_super(sb);
			goto bail;
		}
		spin_lock_irqsave(&ipath_devs_lock, flags);
	}

	spin_unlock_irqrestore(&ipath_devs_lock, flags);

bail:
	return ret;
}

static int ipathfs_get_sb(struct file_system_type *fs_type, int flags,
			const char *dev_name, void *data, struct vfsmount *mnt)
{
	int ret = get_sb_single(fs_type, flags, data,
				    ipathfs_fill_super, mnt);
	if (ret >= 0)
		ipath_super = mnt->mnt_sb;
	return ret;
}

static void ipathfs_kill_super(struct super_block *s)
{
	kill_litter_super(s);
	ipath_super = NULL;
}

int ipathfs_add_device(struct ipath_devdata *dd)
{
	int ret;

	if (ipath_super == NULL) {
		ret = 0;
		goto bail;
	}

	ret = create_device_files(ipath_super, dd);

bail:
	return ret;
}

int ipathfs_remove_device(struct ipath_devdata *dd)
{
	int ret;

	if (ipath_super == NULL) {
		ret = 0;
		goto bail;
	}

	ret = remove_device_files(ipath_super, dd);

bail:
	return ret;
}

static struct file_system_type ipathfs_fs_type = {
	.owner =	THIS_MODULE,
	.name =		"ipathfs",
	.get_sb =	ipathfs_get_sb,
	.kill_sb =	ipathfs_kill_super,
};

int __init ipath_init_ipathfs(void)
{
	return register_filesystem(&ipathfs_fs_type);
}

void __exit ipath_exit_ipathfs(void)
{
	unregister_filesystem(&ipathfs_fs_type);
}
