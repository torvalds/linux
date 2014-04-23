#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

#include "ci.h"
#include "udc.h"
#include "bits.h"
#include "debug.h"
#include "otg.h"

/**
 * ci_device_show: prints information about device capabilities and status
 */
static int ci_device_show(struct seq_file *s, void *data)
{
	struct ci_hdrc *ci = s->private;
	struct usb_gadget *gadget = &ci->gadget;

	seq_printf(s, "speed             = %d\n", gadget->speed);
	seq_printf(s, "max_speed         = %d\n", gadget->max_speed);
	seq_printf(s, "is_otg            = %d\n", gadget->is_otg);
	seq_printf(s, "is_a_peripheral   = %d\n", gadget->is_a_peripheral);
	seq_printf(s, "b_hnp_enable      = %d\n", gadget->b_hnp_enable);
	seq_printf(s, "a_hnp_support     = %d\n", gadget->a_hnp_support);
	seq_printf(s, "a_alt_hnp_support = %d\n", gadget->a_alt_hnp_support);
	seq_printf(s, "name              = %s\n",
		   (gadget->name ? gadget->name : ""));

	if (!ci->driver)
		return 0;

	seq_printf(s, "gadget function   = %s\n",
		       (ci->driver->function ? ci->driver->function : ""));
	seq_printf(s, "gadget max speed  = %d\n", ci->driver->max_speed);

	return 0;
}

static int ci_device_open(struct inode *inode, struct file *file)
{
	return single_open(file, ci_device_show, inode->i_private);
}

static const struct file_operations ci_device_fops = {
	.open		= ci_device_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/**
 * ci_port_test_show: reads port test mode
 */
static int ci_port_test_show(struct seq_file *s, void *data)
{
	struct ci_hdrc *ci = s->private;
	unsigned long flags;
	unsigned mode;

	spin_lock_irqsave(&ci->lock, flags);
	mode = hw_port_test_get(ci);
	spin_unlock_irqrestore(&ci->lock, flags);

	seq_printf(s, "mode = %u\n", mode);

	return 0;
}

/**
 * ci_port_test_write: writes port test mode
 */
static ssize_t ci_port_test_write(struct file *file, const char __user *ubuf,
				  size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct ci_hdrc *ci = s->private;
	unsigned long flags;
	unsigned mode;
	char buf[32];
	int ret;

	if (copy_from_user(buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (sscanf(buf, "%u", &mode) != 1)
		return -EINVAL;

	spin_lock_irqsave(&ci->lock, flags);
	ret = hw_port_test_set(ci, mode);
	spin_unlock_irqrestore(&ci->lock, flags);

	return ret ? ret : count;
}

static int ci_port_test_open(struct inode *inode, struct file *file)
{
	return single_open(file, ci_port_test_show, inode->i_private);
}

static const struct file_operations ci_port_test_fops = {
	.open		= ci_port_test_open,
	.write		= ci_port_test_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/**
 * ci_qheads_show: DMA contents of all queue heads
 */
static int ci_qheads_show(struct seq_file *s, void *data)
{
	struct ci_hdrc *ci = s->private;
	unsigned long flags;
	unsigned i, j;

	if (ci->role != CI_ROLE_GADGET) {
		seq_printf(s, "not in gadget mode\n");
		return 0;
	}

	spin_lock_irqsave(&ci->lock, flags);
	for (i = 0; i < ci->hw_ep_max/2; i++) {
		struct ci_hw_ep *hweprx = &ci->ci_hw_ep[i];
		struct ci_hw_ep *hweptx =
			&ci->ci_hw_ep[i + ci->hw_ep_max/2];
		seq_printf(s, "EP=%02i: RX=%08X TX=%08X\n",
			   i, (u32)hweprx->qh.dma, (u32)hweptx->qh.dma);
		for (j = 0; j < (sizeof(struct ci_hw_qh)/sizeof(u32)); j++)
			seq_printf(s, " %04X:    %08X    %08X\n", j,
				   *((u32 *)hweprx->qh.ptr + j),
				   *((u32 *)hweptx->qh.ptr + j));
	}
	spin_unlock_irqrestore(&ci->lock, flags);

	return 0;
}

static int ci_qheads_open(struct inode *inode, struct file *file)
{
	return single_open(file, ci_qheads_show, inode->i_private);
}

static const struct file_operations ci_qheads_fops = {
	.open		= ci_qheads_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/**
 * ci_requests_show: DMA contents of all requests currently queued (all endpts)
 */
static int ci_requests_show(struct seq_file *s, void *data)
{
	struct ci_hdrc *ci = s->private;
	unsigned long flags;
	struct list_head   *ptr = NULL;
	struct ci_hw_req *req = NULL;
	struct td_node *node, *tmpnode;
	unsigned i, j, qsize = sizeof(struct ci_hw_td)/sizeof(u32);

	if (ci->role != CI_ROLE_GADGET) {
		seq_printf(s, "not in gadget mode\n");
		return 0;
	}

	spin_lock_irqsave(&ci->lock, flags);
	for (i = 0; i < ci->hw_ep_max; i++)
		list_for_each(ptr, &ci->ci_hw_ep[i].qh.queue) {
			req = list_entry(ptr, struct ci_hw_req, queue);

			list_for_each_entry_safe(node, tmpnode, &req->tds, td) {
				seq_printf(s, "EP=%02i: TD=%08X %s\n",
					   i % (ci->hw_ep_max / 2),
					   (u32)node->dma,
					   ((i < ci->hw_ep_max/2) ?
					   "RX" : "TX"));

				for (j = 0; j < qsize; j++)
					seq_printf(s, " %04X:    %08X\n", j,
						   *((u32 *)node->ptr + j));
			}
		}
	spin_unlock_irqrestore(&ci->lock, flags);

	return 0;
}

static int ci_requests_open(struct inode *inode, struct file *file)
{
	return single_open(file, ci_requests_show, inode->i_private);
}

static const struct file_operations ci_requests_fops = {
	.open		= ci_requests_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int ci_role_show(struct seq_file *s, void *data)
{
	struct ci_hdrc *ci = s->private;

	seq_printf(s, "%s\n", ci_role(ci)->name);

	return 0;
}

static ssize_t ci_role_write(struct file *file, const char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct ci_hdrc *ci = s->private;
	enum ci_role role;
	char buf[8];
	int ret;

	if (copy_from_user(buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	for (role = CI_ROLE_HOST; role < CI_ROLE_END; role++)
		if (ci->roles[role] &&
		    !strncmp(buf, ci->roles[role]->name,
			     strlen(ci->roles[role]->name)))
			break;

	if (role == CI_ROLE_END || role == ci->role)
		return -EINVAL;

	ci_role_stop(ci);
	ret = ci_role_start(ci, role);

	return ret ? ret : count;
}

static int ci_role_open(struct inode *inode, struct file *file)
{
	return single_open(file, ci_role_show, inode->i_private);
}

static const struct file_operations ci_role_fops = {
	.open		= ci_role_open,
	.write		= ci_role_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int ci_registers_show(struct seq_file *s, void *unused)
{
	struct ci_hdrc *ci = s->private;
	u32 tmp_reg;

	if (!ci)
		return 0;

	/* ------ Registers ----- */
	tmp_reg = hw_read_intr_enable(ci);
	seq_printf(s, "USBINTR reg: %08x\n", tmp_reg);

	tmp_reg = hw_read_intr_status(ci);
	seq_printf(s, "USBSTS reg: %08x\n", tmp_reg);

	tmp_reg = hw_read(ci, OP_USBMODE, ~0);
	seq_printf(s, "USBMODE reg: %08x\n", tmp_reg);

	tmp_reg = hw_read(ci, OP_USBCMD, ~0);
	seq_printf(s, "USBCMD reg: %08x\n", tmp_reg);

	tmp_reg = hw_read(ci, OP_PORTSC, ~0);
	seq_printf(s, "PORTSC reg: %08x\n", tmp_reg);

	if (ci->is_otg) {
		tmp_reg = hw_read_otgsc(ci, ~0);
		seq_printf(s, "OTGSC reg: %08x\n", tmp_reg);
	}

	return 0;
}

static int ci_registers_open(struct inode *inode, struct file *file)
{
	return single_open(file, ci_registers_show, inode->i_private);
}

static const struct file_operations ci_registers_fops = {
	.open			= ci_registers_open,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

/**
 * dbg_create_files: initializes the attribute interface
 * @ci: device
 *
 * This function returns an error code
 */
int dbg_create_files(struct ci_hdrc *ci)
{
	struct dentry *dent;

	ci->debugfs = debugfs_create_dir(dev_name(ci->dev), NULL);
	if (!ci->debugfs)
		return -ENOMEM;

	dent = debugfs_create_file("device", S_IRUGO, ci->debugfs, ci,
				   &ci_device_fops);
	if (!dent)
		goto err;

	dent = debugfs_create_file("port_test", S_IRUGO | S_IWUSR, ci->debugfs,
				   ci, &ci_port_test_fops);
	if (!dent)
		goto err;

	dent = debugfs_create_file("qheads", S_IRUGO, ci->debugfs, ci,
				   &ci_qheads_fops);
	if (!dent)
		goto err;

	dent = debugfs_create_file("requests", S_IRUGO, ci->debugfs, ci,
				   &ci_requests_fops);
	if (!dent)
		goto err;

	dent = debugfs_create_file("role", S_IRUGO | S_IWUSR, ci->debugfs, ci,
				   &ci_role_fops);
	if (!dent)
		goto err;

	dent = debugfs_create_file("registers", S_IRUGO, ci->debugfs, ci,
				&ci_registers_fops);

	if (dent)
		return 0;
err:
	debugfs_remove_recursive(ci->debugfs);
	return -ENOMEM;
}

/**
 * dbg_remove_files: destroys the attribute interface
 * @ci: device
 */
void dbg_remove_files(struct ci_hdrc *ci)
{
	debugfs_remove_recursive(ci->debugfs);
}
