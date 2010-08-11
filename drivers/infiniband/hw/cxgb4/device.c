/*
 * Copyright (c) 2009-2010 Chelsio, Inc. All rights reserved.
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
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
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
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/debugfs.h>

#include <rdma/ib_verbs.h>

#include "iw_cxgb4.h"

#define DRV_VERSION "0.1"

MODULE_AUTHOR("Steve Wise");
MODULE_DESCRIPTION("Chelsio T4 RDMA Driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRV_VERSION);

static LIST_HEAD(dev_list);
static DEFINE_MUTEX(dev_mutex);

static struct dentry *c4iw_debugfs_root;

struct debugfs_qp_data {
	struct c4iw_dev *devp;
	char *buf;
	int bufsize;
	int pos;
};

static int count_qps(int id, void *p, void *data)
{
	struct c4iw_qp *qp = p;
	int *countp = data;

	if (id != qp->wq.sq.qid)
		return 0;

	*countp = *countp + 1;
	return 0;
}

static int dump_qps(int id, void *p, void *data)
{
	struct c4iw_qp *qp = p;
	struct debugfs_qp_data *qpd = data;
	int space;
	int cc;

	if (id != qp->wq.sq.qid)
		return 0;

	space = qpd->bufsize - qpd->pos - 1;
	if (space == 0)
		return 1;

	if (qp->ep)
		cc = snprintf(qpd->buf + qpd->pos, space, "qp id %u state %u "
			     "ep tid %u state %u %pI4:%u->%pI4:%u\n",
			     qp->wq.sq.qid, (int)qp->attr.state,
			     qp->ep->hwtid, (int)qp->ep->com.state,
			     &qp->ep->com.local_addr.sin_addr.s_addr,
			     ntohs(qp->ep->com.local_addr.sin_port),
			     &qp->ep->com.remote_addr.sin_addr.s_addr,
			     ntohs(qp->ep->com.remote_addr.sin_port));
	else
		cc = snprintf(qpd->buf + qpd->pos, space, "qp id %u state %u\n",
			      qp->wq.sq.qid, (int)qp->attr.state);
	if (cc < space)
		qpd->pos += cc;
	return 0;
}

static int qp_release(struct inode *inode, struct file *file)
{
	struct debugfs_qp_data *qpd = file->private_data;
	if (!qpd) {
		printk(KERN_INFO "%s null qpd?\n", __func__);
		return 0;
	}
	kfree(qpd->buf);
	kfree(qpd);
	return 0;
}

static int qp_open(struct inode *inode, struct file *file)
{
	struct debugfs_qp_data *qpd;
	int ret = 0;
	int count = 1;

	qpd = kmalloc(sizeof *qpd, GFP_KERNEL);
	if (!qpd) {
		ret = -ENOMEM;
		goto out;
	}
	qpd->devp = inode->i_private;
	qpd->pos = 0;

	spin_lock_irq(&qpd->devp->lock);
	idr_for_each(&qpd->devp->qpidr, count_qps, &count);
	spin_unlock_irq(&qpd->devp->lock);

	qpd->bufsize = count * 128;
	qpd->buf = kmalloc(qpd->bufsize, GFP_KERNEL);
	if (!qpd->buf) {
		ret = -ENOMEM;
		goto err1;
	}

	spin_lock_irq(&qpd->devp->lock);
	idr_for_each(&qpd->devp->qpidr, dump_qps, qpd);
	spin_unlock_irq(&qpd->devp->lock);

	qpd->buf[qpd->pos++] = 0;
	file->private_data = qpd;
	goto out;
err1:
	kfree(qpd);
out:
	return ret;
}

static ssize_t qp_read(struct file *file, char __user *buf, size_t count,
			loff_t *ppos)
{
	struct debugfs_qp_data *qpd = file->private_data;
	loff_t pos = *ppos;
	loff_t avail = qpd->pos;

	if (pos < 0)
		return -EINVAL;
	if (pos >= avail)
		return 0;
	if (count > avail - pos)
		count = avail - pos;

	while (count) {
		size_t len = 0;

		len = min((int)count, (int)qpd->pos - (int)pos);
		if (copy_to_user(buf, qpd->buf + pos, len))
			return -EFAULT;
		if (len == 0)
			return -EINVAL;

		buf += len;
		pos += len;
		count -= len;
	}
	count = pos - *ppos;
	*ppos = pos;
	return count;
}

static const struct file_operations qp_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = qp_open,
	.release = qp_release,
	.read    = qp_read,
};

static int setup_debugfs(struct c4iw_dev *devp)
{
	struct dentry *de;

	if (!devp->debugfs_root)
		return -1;

	de = debugfs_create_file("qps", S_IWUSR, devp->debugfs_root,
				 (void *)devp, &qp_debugfs_fops);
	if (de && de->d_inode)
		de->d_inode->i_size = 4096;
	return 0;
}

void c4iw_release_dev_ucontext(struct c4iw_rdev *rdev,
			       struct c4iw_dev_ucontext *uctx)
{
	struct list_head *pos, *nxt;
	struct c4iw_qid_list *entry;

	mutex_lock(&uctx->lock);
	list_for_each_safe(pos, nxt, &uctx->qpids) {
		entry = list_entry(pos, struct c4iw_qid_list, entry);
		list_del_init(&entry->entry);
		if (!(entry->qid & rdev->qpmask))
			c4iw_put_resource(&rdev->resource.qid_fifo, entry->qid,
					  &rdev->resource.qid_fifo_lock);
		kfree(entry);
	}

	list_for_each_safe(pos, nxt, &uctx->qpids) {
		entry = list_entry(pos, struct c4iw_qid_list, entry);
		list_del_init(&entry->entry);
		kfree(entry);
	}
	mutex_unlock(&uctx->lock);
}

void c4iw_init_dev_ucontext(struct c4iw_rdev *rdev,
			    struct c4iw_dev_ucontext *uctx)
{
	INIT_LIST_HEAD(&uctx->qpids);
	INIT_LIST_HEAD(&uctx->cqids);
	mutex_init(&uctx->lock);
}

/* Caller takes care of locking if needed */
static int c4iw_rdev_open(struct c4iw_rdev *rdev)
{
	int err;

	c4iw_init_dev_ucontext(rdev, &rdev->uctx);

	/*
	 * qpshift is the number of bits to shift the qpid left in order
	 * to get the correct address of the doorbell for that qp.
	 */
	rdev->qpshift = PAGE_SHIFT - ilog2(rdev->lldi.udb_density);
	rdev->qpmask = rdev->lldi.udb_density - 1;
	rdev->cqshift = PAGE_SHIFT - ilog2(rdev->lldi.ucq_density);
	rdev->cqmask = rdev->lldi.ucq_density - 1;
	PDBG("%s dev %s stag start 0x%0x size 0x%0x num stags %d "
	     "pbl start 0x%0x size 0x%0x rq start 0x%0x size 0x%0x "
	     "qp qid start %u size %u cq qid start %u size %u\n",
	     __func__, pci_name(rdev->lldi.pdev), rdev->lldi.vr->stag.start,
	     rdev->lldi.vr->stag.size, c4iw_num_stags(rdev),
	     rdev->lldi.vr->pbl.start,
	     rdev->lldi.vr->pbl.size, rdev->lldi.vr->rq.start,
	     rdev->lldi.vr->rq.size,
	     rdev->lldi.vr->qp.start,
	     rdev->lldi.vr->qp.size,
	     rdev->lldi.vr->cq.start,
	     rdev->lldi.vr->cq.size);
	PDBG("udb len 0x%x udb base %p db_reg %p gts_reg %p qpshift %lu "
	     "qpmask 0x%x cqshift %lu cqmask 0x%x\n",
	     (unsigned)pci_resource_len(rdev->lldi.pdev, 2),
	     (void *)pci_resource_start(rdev->lldi.pdev, 2),
	     rdev->lldi.db_reg,
	     rdev->lldi.gts_reg,
	     rdev->qpshift, rdev->qpmask,
	     rdev->cqshift, rdev->cqmask);

	if (c4iw_num_stags(rdev) == 0) {
		err = -EINVAL;
		goto err1;
	}

	err = c4iw_init_resource(rdev, c4iw_num_stags(rdev), T4_MAX_NUM_PD);
	if (err) {
		printk(KERN_ERR MOD "error %d initializing resources\n", err);
		goto err1;
	}
	err = c4iw_pblpool_create(rdev);
	if (err) {
		printk(KERN_ERR MOD "error %d initializing pbl pool\n", err);
		goto err2;
	}
	err = c4iw_rqtpool_create(rdev);
	if (err) {
		printk(KERN_ERR MOD "error %d initializing rqt pool\n", err);
		goto err3;
	}
	return 0;
err3:
	c4iw_pblpool_destroy(rdev);
err2:
	c4iw_destroy_resource(&rdev->resource);
err1:
	return err;
}

static void c4iw_rdev_close(struct c4iw_rdev *rdev)
{
	c4iw_pblpool_destroy(rdev);
	c4iw_rqtpool_destroy(rdev);
	c4iw_destroy_resource(&rdev->resource);
}

static void c4iw_remove(struct c4iw_dev *dev)
{
	PDBG("%s c4iw_dev %p\n", __func__,  dev);
	cancel_delayed_work_sync(&dev->db_drop_task);
	list_del(&dev->entry);
	if (dev->registered)
		c4iw_unregister_device(dev);
	c4iw_rdev_close(&dev->rdev);
	idr_destroy(&dev->cqidr);
	idr_destroy(&dev->qpidr);
	idr_destroy(&dev->mmidr);
	ib_dealloc_device(&dev->ibdev);
}

static struct c4iw_dev *c4iw_alloc(const struct cxgb4_lld_info *infop)
{
	struct c4iw_dev *devp;
	int ret;

	devp = (struct c4iw_dev *)ib_alloc_device(sizeof(*devp));
	if (!devp) {
		printk(KERN_ERR MOD "Cannot allocate ib device\n");
		return NULL;
	}
	devp->rdev.lldi = *infop;

	mutex_lock(&dev_mutex);

	ret = c4iw_rdev_open(&devp->rdev);
	if (ret) {
		mutex_unlock(&dev_mutex);
		printk(KERN_ERR MOD "Unable to open CXIO rdev err %d\n", ret);
		ib_dealloc_device(&devp->ibdev);
		return NULL;
	}

	idr_init(&devp->cqidr);
	idr_init(&devp->qpidr);
	idr_init(&devp->mmidr);
	spin_lock_init(&devp->lock);
	list_add_tail(&devp->entry, &dev_list);
	mutex_unlock(&dev_mutex);

	if (c4iw_debugfs_root) {
		devp->debugfs_root = debugfs_create_dir(
					pci_name(devp->rdev.lldi.pdev),
					c4iw_debugfs_root);
		setup_debugfs(devp);
	}
	return devp;
}

static void *c4iw_uld_add(const struct cxgb4_lld_info *infop)
{
	struct c4iw_dev *dev;
	static int vers_printed;
	int i;

	if (!vers_printed++)
		printk(KERN_INFO MOD "Chelsio T4 RDMA Driver - version %s\n",
		       DRV_VERSION);

	dev = c4iw_alloc(infop);
	if (!dev)
		goto out;

	PDBG("%s found device %s nchan %u nrxq %u ntxq %u nports %u\n",
	     __func__, pci_name(dev->rdev.lldi.pdev),
	     dev->rdev.lldi.nchan, dev->rdev.lldi.nrxq,
	     dev->rdev.lldi.ntxq, dev->rdev.lldi.nports);

	for (i = 0; i < dev->rdev.lldi.nrxq; i++)
		PDBG("rxqid[%u] %u\n", i, dev->rdev.lldi.rxq_ids[i]);
out:
	return dev;
}

static struct sk_buff *t4_pktgl_to_skb(const struct pkt_gl *gl,
				       unsigned int skb_len,
				       unsigned int pull_len)
{
	struct sk_buff *skb;
	struct skb_shared_info *ssi;

	if (gl->tot_len <= 512) {
		skb = alloc_skb(gl->tot_len, GFP_ATOMIC);
		if (unlikely(!skb))
			goto out;
		__skb_put(skb, gl->tot_len);
		skb_copy_to_linear_data(skb, gl->va, gl->tot_len);
	} else {
		skb = alloc_skb(skb_len, GFP_ATOMIC);
		if (unlikely(!skb))
			goto out;
		__skb_put(skb, pull_len);
		skb_copy_to_linear_data(skb, gl->va, pull_len);

		ssi = skb_shinfo(skb);
		ssi->frags[0].page = gl->frags[0].page;
		ssi->frags[0].page_offset = gl->frags[0].page_offset + pull_len;
		ssi->frags[0].size = gl->frags[0].size - pull_len;
		if (gl->nfrags > 1)
			memcpy(&ssi->frags[1], &gl->frags[1],
			       (gl->nfrags - 1) * sizeof(skb_frag_t));
		ssi->nr_frags = gl->nfrags;

		skb->len = gl->tot_len;
		skb->data_len = skb->len - pull_len;
		skb->truesize += skb->data_len;

		/* Get a reference for the last page, we don't own it */
		get_page(gl->frags[gl->nfrags - 1].page);
	}
out:
	return skb;
}

static int c4iw_uld_rx_handler(void *handle, const __be64 *rsp,
			const struct pkt_gl *gl)
{
	struct c4iw_dev *dev = handle;
	struct sk_buff *skb;
	const struct cpl_act_establish *rpl;
	unsigned int opcode;

	if (gl == NULL) {
		/* omit RSS and rsp_ctrl at end of descriptor */
		unsigned int len = 64 - sizeof(struct rsp_ctrl) - 8;

		skb = alloc_skb(256, GFP_ATOMIC);
		if (!skb)
			goto nomem;
		__skb_put(skb, len);
		skb_copy_to_linear_data(skb, &rsp[1], len);
	} else if (gl == CXGB4_MSG_AN) {
		const struct rsp_ctrl *rc = (void *)rsp;

		u32 qid = be32_to_cpu(rc->pldbuflen_qid);
		c4iw_ev_handler(dev, qid);
		return 0;
	} else {
		skb = t4_pktgl_to_skb(gl, 128, 128);
		if (unlikely(!skb))
			goto nomem;
	}

	rpl = cplhdr(skb);
	opcode = rpl->ot.opcode;

	if (c4iw_handlers[opcode])
		c4iw_handlers[opcode](dev, skb);
	else
		printk(KERN_INFO "%s no handler opcode 0x%x...\n", __func__,
		       opcode);

	return 0;
nomem:
	return -1;
}

static int c4iw_uld_state_change(void *handle, enum cxgb4_state new_state)
{
	struct c4iw_dev *dev = handle;

	PDBG("%s new_state %u\n", __func__, new_state);
	switch (new_state) {
	case CXGB4_STATE_UP:
		printk(KERN_INFO MOD "%s: Up\n", pci_name(dev->rdev.lldi.pdev));
		if (!dev->registered) {
			int ret;
			ret = c4iw_register_device(dev);
			if (ret)
				printk(KERN_ERR MOD
				       "%s: RDMA registration failed: %d\n",
				       pci_name(dev->rdev.lldi.pdev), ret);
		}
		break;
	case CXGB4_STATE_DOWN:
		printk(KERN_INFO MOD "%s: Down\n",
		       pci_name(dev->rdev.lldi.pdev));
		if (dev->registered)
			c4iw_unregister_device(dev);
		break;
	case CXGB4_STATE_START_RECOVERY:
		printk(KERN_INFO MOD "%s: Fatal Error\n",
		       pci_name(dev->rdev.lldi.pdev));
		if (dev->registered)
			c4iw_unregister_device(dev);
		break;
	case CXGB4_STATE_DETACH:
		printk(KERN_INFO MOD "%s: Detach\n",
		       pci_name(dev->rdev.lldi.pdev));
		mutex_lock(&dev_mutex);
		c4iw_remove(dev);
		mutex_unlock(&dev_mutex);
		break;
	}
	return 0;
}

static struct cxgb4_uld_info c4iw_uld_info = {
	.name = DRV_NAME,
	.add = c4iw_uld_add,
	.rx_handler = c4iw_uld_rx_handler,
	.state_change = c4iw_uld_state_change,
};

static int __init c4iw_init_module(void)
{
	int err;

	err = c4iw_cm_init();
	if (err)
		return err;

	c4iw_debugfs_root = debugfs_create_dir(DRV_NAME, NULL);
	if (!c4iw_debugfs_root)
		printk(KERN_WARNING MOD
		       "could not create debugfs entry, continuing\n");

	cxgb4_register_uld(CXGB4_ULD_RDMA, &c4iw_uld_info);

	return 0;
}

static void __exit c4iw_exit_module(void)
{
	struct c4iw_dev *dev, *tmp;

	mutex_lock(&dev_mutex);
	list_for_each_entry_safe(dev, tmp, &dev_list, entry) {
		c4iw_remove(dev);
	}
	mutex_unlock(&dev_mutex);
	cxgb4_unregister_uld(CXGB4_ULD_RDMA);
	c4iw_cm_term();
	debugfs_remove_recursive(c4iw_debugfs_root);
}

module_init(c4iw_init_module);
module_exit(c4iw_exit_module);
