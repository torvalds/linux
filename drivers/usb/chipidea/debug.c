// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/phy.h>
#include <linux/usb/otg.h>
#include <linux/usb/otg-fsm.h>
#include <linux/usb/chipidea.h>

#include "ci.h"
#include "udc.h"
#include "bits.h"
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
DEFINE_SHOW_ATTRIBUTE(ci_device);

/**
 * ci_port_test_show: reads port test mode
 */
static int ci_port_test_show(struct seq_file *s, void *data)
{
	struct ci_hdrc *ci = s->private;
	unsigned long flags;
	unsigned mode;

	pm_runtime_get_sync(ci->dev);
	spin_lock_irqsave(&ci->lock, flags);
	mode = hw_port_test_get(ci);
	spin_unlock_irqrestore(&ci->lock, flags);
	pm_runtime_put_sync(ci->dev);

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

	count = min_t(size_t, sizeof(buf) - 1, count);
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;

	/* sscanf requires a zero terminated string */
	buf[count] = '\0';

	if (sscanf(buf, "%u", &mode) != 1)
		return -EINVAL;

	if (mode > 255)
		return -EBADRQC;

	pm_runtime_get_sync(ci->dev);
	spin_lock_irqsave(&ci->lock, flags);
	ret = hw_port_test_set(ci, mode);
	spin_unlock_irqrestore(&ci->lock, flags);
	pm_runtime_put_sync(ci->dev);

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
DEFINE_SHOW_ATTRIBUTE(ci_qheads);

/**
 * ci_requests_show: DMA contents of all requests currently queued (all endpts)
 */
static int ci_requests_show(struct seq_file *s, void *data)
{
	struct ci_hdrc *ci = s->private;
	unsigned long flags;
	struct ci_hw_req *req = NULL;
	struct td_node *node, *tmpnode;
	unsigned i, j, qsize = sizeof(struct ci_hw_td)/sizeof(u32);

	if (ci->role != CI_ROLE_GADGET) {
		seq_printf(s, "not in gadget mode\n");
		return 0;
	}

	spin_lock_irqsave(&ci->lock, flags);
	for (i = 0; i < ci->hw_ep_max; i++)
		list_for_each_entry(req, &ci->ci_hw_ep[i].qh.queue, queue) {
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
DEFINE_SHOW_ATTRIBUTE(ci_requests);

static int ci_otg_show(struct seq_file *s, void *unused)
{
	struct ci_hdrc *ci = s->private;
	struct otg_fsm *fsm;

	if (!ci || !ci_otg_is_fsm_mode(ci))
		return 0;

	fsm = &ci->fsm;

	/* ------ State ----- */
	seq_printf(s, "OTG state: %s\n\n",
			usb_otg_state_string(ci->otg.state));

	/* ------ State Machine Variables ----- */
	seq_printf(s, "a_bus_drop: %d\n", fsm->a_bus_drop);

	seq_printf(s, "a_bus_req: %d\n", fsm->a_bus_req);

	seq_printf(s, "a_srp_det: %d\n", fsm->a_srp_det);

	seq_printf(s, "a_vbus_vld: %d\n", fsm->a_vbus_vld);

	seq_printf(s, "b_conn: %d\n", fsm->b_conn);

	seq_printf(s, "adp_change: %d\n", fsm->adp_change);

	seq_printf(s, "power_up: %d\n", fsm->power_up);

	seq_printf(s, "a_bus_resume: %d\n", fsm->a_bus_resume);

	seq_printf(s, "a_bus_suspend: %d\n", fsm->a_bus_suspend);

	seq_printf(s, "a_conn: %d\n", fsm->a_conn);

	seq_printf(s, "b_bus_req: %d\n", fsm->b_bus_req);

	seq_printf(s, "b_bus_suspend: %d\n", fsm->b_bus_suspend);

	seq_printf(s, "b_se0_srp: %d\n", fsm->b_se0_srp);

	seq_printf(s, "b_ssend_srp: %d\n", fsm->b_ssend_srp);

	seq_printf(s, "b_sess_vld: %d\n", fsm->b_sess_vld);

	seq_printf(s, "b_srp_done: %d\n", fsm->b_srp_done);

	seq_printf(s, "drv_vbus: %d\n", fsm->drv_vbus);

	seq_printf(s, "loc_conn: %d\n", fsm->loc_conn);

	seq_printf(s, "loc_sof: %d\n", fsm->loc_sof);

	seq_printf(s, "adp_prb: %d\n", fsm->adp_prb);

	seq_printf(s, "id: %d\n", fsm->id);

	seq_printf(s, "protocol: %d\n", fsm->protocol);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ci_otg);

static int ci_role_show(struct seq_file *s, void *data)
{
	struct ci_hdrc *ci = s->private;

	if (ci->role != CI_ROLE_END)
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

	pm_runtime_get_sync(ci->dev);
	disable_irq(ci->irq);
	ci_role_stop(ci);
	ret = ci_role_start(ci, role);
	enable_irq(ci->irq);
	pm_runtime_put_sync(ci->dev);

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

static int ci_registers_show(struct seq_file *s, void *unused)
{
	struct ci_hdrc *ci = s->private;
	u32 tmp_reg;

	if (!ci || ci->in_lpm)
		return -EPERM;

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
DEFINE_SHOW_ATTRIBUTE(ci_registers);

/**
 * dbg_create_files: initializes the attribute interface
 * @ci: device
 *
 * This function returns an error code
 */
void dbg_create_files(struct ci_hdrc *ci)
{
	ci->debugfs = debugfs_create_dir(dev_name(ci->dev), usb_debug_root);

	debugfs_create_file("device", S_IRUGO, ci->debugfs, ci,
			    &ci_device_fops);
	debugfs_create_file("port_test", S_IRUGO | S_IWUSR, ci->debugfs, ci,
			    &ci_port_test_fops);
	debugfs_create_file("qheads", S_IRUGO, ci->debugfs, ci,
			    &ci_qheads_fops);
	debugfs_create_file("requests", S_IRUGO, ci->debugfs, ci,
			    &ci_requests_fops);

	if (ci_otg_is_fsm_mode(ci)) {
		debugfs_create_file("otg", S_IRUGO, ci->debugfs, ci,
				    &ci_otg_fops);
	}

	debugfs_create_file("role", S_IRUGO | S_IWUSR, ci->debugfs, ci,
			    &ci_role_fops);
	debugfs_create_file("registers", S_IRUGO, ci->debugfs, ci,
			    &ci_registers_fops);
}

/**
 * dbg_remove_files: destroys the attribute interface
 * @ci: device
 */
void dbg_remove_files(struct ci_hdrc *ci)
{
	debugfs_remove_recursive(ci->debugfs);
}
