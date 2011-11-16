/*
 * SCSI target kernel/user interface functions
 *
 * Copyright (C) 2005 FUJITA Tomonori <tomof@acm.org>
 * Copyright (C) 2005 Mike Christie <michaelc@cs.wisc.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#include <linux/miscdevice.h>
#include <linux/gfp.h>
#include <linux/file.h>
#include <linux/export.h>
#include <net/tcp.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tgt.h>
#include <scsi/scsi_tgt_if.h>

#include <asm/cacheflush.h>

#include "scsi_tgt_priv.h"

#if TGT_RING_SIZE < PAGE_SIZE
#  define TGT_RING_SIZE PAGE_SIZE
#endif

#define TGT_RING_PAGES (TGT_RING_SIZE >> PAGE_SHIFT)
#define TGT_EVENT_PER_PAGE (PAGE_SIZE / sizeof(struct tgt_event))
#define TGT_MAX_EVENTS (TGT_EVENT_PER_PAGE * TGT_RING_PAGES)

struct tgt_ring {
	u32 tr_idx;
	unsigned long tr_pages[TGT_RING_PAGES];
	spinlock_t tr_lock;
};

/* tx_ring : kernel->user, rx_ring : user->kernel */
static struct tgt_ring tx_ring, rx_ring;
static DECLARE_WAIT_QUEUE_HEAD(tgt_poll_wait);

static inline void tgt_ring_idx_inc(struct tgt_ring *ring)
{
	if (ring->tr_idx == TGT_MAX_EVENTS - 1)
		ring->tr_idx = 0;
	else
		ring->tr_idx++;
}

static struct tgt_event *tgt_head_event(struct tgt_ring *ring, u32 idx)
{
	u32 pidx, off;

	pidx = idx / TGT_EVENT_PER_PAGE;
	off = idx % TGT_EVENT_PER_PAGE;

	return (struct tgt_event *)
		(ring->tr_pages[pidx] + sizeof(struct tgt_event) * off);
}

static int tgt_uspace_send_event(u32 type, struct tgt_event *p)
{
	struct tgt_event *ev;
	struct tgt_ring *ring = &tx_ring;
	unsigned long flags;
	int err = 0;

	spin_lock_irqsave(&ring->tr_lock, flags);

	ev = tgt_head_event(ring, ring->tr_idx);
	if (!ev->hdr.status)
		tgt_ring_idx_inc(ring);
	else
		err = -BUSY;

	spin_unlock_irqrestore(&ring->tr_lock, flags);

	if (err)
		return err;

	memcpy(ev, p, sizeof(*ev));
	ev->hdr.type = type;
	mb();
	ev->hdr.status = 1;

	flush_dcache_page(virt_to_page(ev));

	wake_up_interruptible(&tgt_poll_wait);

	return 0;
}

int scsi_tgt_uspace_send_cmd(struct scsi_cmnd *cmd, u64 itn_id,
			     struct scsi_lun *lun, u64 tag)
{
	struct Scsi_Host *shost = scsi_tgt_cmd_to_host(cmd);
	struct tgt_event ev;
	int err;

	memset(&ev, 0, sizeof(ev));
	ev.p.cmd_req.host_no = shost->host_no;
	ev.p.cmd_req.itn_id = itn_id;
	ev.p.cmd_req.data_len = scsi_bufflen(cmd);
	memcpy(ev.p.cmd_req.scb, cmd->cmnd, sizeof(ev.p.cmd_req.scb));
	memcpy(ev.p.cmd_req.lun, lun, sizeof(ev.p.cmd_req.lun));
	ev.p.cmd_req.attribute = cmd->tag;
	ev.p.cmd_req.tag = tag;

	dprintk("%p %d %u %x %llx\n", cmd, shost->host_no,
		ev.p.cmd_req.data_len, cmd->tag,
		(unsigned long long) ev.p.cmd_req.tag);

	err = tgt_uspace_send_event(TGT_KEVENT_CMD_REQ, &ev);
	if (err)
		eprintk("tx buf is full, could not send\n");

	return err;
}

int scsi_tgt_uspace_send_status(struct scsi_cmnd *cmd, u64 itn_id, u64 tag)
{
	struct Scsi_Host *shost = scsi_tgt_cmd_to_host(cmd);
	struct tgt_event ev;
	int err;

	memset(&ev, 0, sizeof(ev));
	ev.p.cmd_done.host_no = shost->host_no;
	ev.p.cmd_done.itn_id = itn_id;
	ev.p.cmd_done.tag = tag;
	ev.p.cmd_done.result = cmd->result;

	dprintk("%p %d %llu %u %x\n", cmd, shost->host_no,
		(unsigned long long) ev.p.cmd_req.tag,
		ev.p.cmd_req.data_len, cmd->tag);

	err = tgt_uspace_send_event(TGT_KEVENT_CMD_DONE, &ev);
	if (err)
		eprintk("tx buf is full, could not send\n");

	return err;
}

int scsi_tgt_uspace_send_tsk_mgmt(int host_no, u64 itn_id, int function,
				  u64 tag, struct scsi_lun *scsilun, void *data)
{
	struct tgt_event ev;
	int err;

	memset(&ev, 0, sizeof(ev));
	ev.p.tsk_mgmt_req.host_no = host_no;
	ev.p.tsk_mgmt_req.itn_id = itn_id;
	ev.p.tsk_mgmt_req.function = function;
	ev.p.tsk_mgmt_req.tag = tag;
	memcpy(ev.p.tsk_mgmt_req.lun, scsilun, sizeof(ev.p.tsk_mgmt_req.lun));
	ev.p.tsk_mgmt_req.mid = (u64) (unsigned long) data;

	dprintk("%d %x %llx %llx\n", host_no, function, (unsigned long long) tag,
		(unsigned long long) ev.p.tsk_mgmt_req.mid);

	err = tgt_uspace_send_event(TGT_KEVENT_TSK_MGMT_REQ, &ev);
	if (err)
		eprintk("tx buf is full, could not send\n");

	return err;
}

int scsi_tgt_uspace_send_it_nexus_request(int host_no, u64 itn_id,
					  int function, char *initiator_id)
{
	struct tgt_event ev;
	int err;

	memset(&ev, 0, sizeof(ev));
	ev.p.it_nexus_req.host_no = host_no;
	ev.p.it_nexus_req.function = function;
	ev.p.it_nexus_req.itn_id = itn_id;
	if (initiator_id)
		strncpy(ev.p.it_nexus_req.initiator_id, initiator_id,
			sizeof(ev.p.it_nexus_req.initiator_id));

	dprintk("%d %x %llx\n", host_no, function, (unsigned long long)itn_id);

	err = tgt_uspace_send_event(TGT_KEVENT_IT_NEXUS_REQ, &ev);
	if (err)
		eprintk("tx buf is full, could not send\n");

	return err;
}

static int event_recv_msg(struct tgt_event *ev)
{
	int err = 0;

	switch (ev->hdr.type) {
	case TGT_UEVENT_CMD_RSP:
		err = scsi_tgt_kspace_exec(ev->p.cmd_rsp.host_no,
					   ev->p.cmd_rsp.itn_id,
					   ev->p.cmd_rsp.result,
					   ev->p.cmd_rsp.tag,
					   ev->p.cmd_rsp.uaddr,
					   ev->p.cmd_rsp.len,
					   ev->p.cmd_rsp.sense_uaddr,
					   ev->p.cmd_rsp.sense_len,
					   ev->p.cmd_rsp.rw);
		break;
	case TGT_UEVENT_TSK_MGMT_RSP:
		err = scsi_tgt_kspace_tsk_mgmt(ev->p.tsk_mgmt_rsp.host_no,
					       ev->p.tsk_mgmt_rsp.itn_id,
					       ev->p.tsk_mgmt_rsp.mid,
					       ev->p.tsk_mgmt_rsp.result);
		break;
	case TGT_UEVENT_IT_NEXUS_RSP:
		err = scsi_tgt_kspace_it_nexus_rsp(ev->p.it_nexus_rsp.host_no,
						   ev->p.it_nexus_rsp.itn_id,
						   ev->p.it_nexus_rsp.result);
		break;
	default:
		eprintk("unknown type %d\n", ev->hdr.type);
		err = -EINVAL;
	}

	return err;
}

static ssize_t tgt_write(struct file *file, const char __user * buffer,
			 size_t count, loff_t * ppos)
{
	struct tgt_event *ev;
	struct tgt_ring *ring = &rx_ring;

	while (1) {
		ev = tgt_head_event(ring, ring->tr_idx);
		/* do we need this? */
		flush_dcache_page(virt_to_page(ev));

		if (!ev->hdr.status)
			break;

		tgt_ring_idx_inc(ring);
		event_recv_msg(ev);
		ev->hdr.status = 0;
	};

	return count;
}

static unsigned int tgt_poll(struct file * file, struct poll_table_struct *wait)
{
	struct tgt_event *ev;
	struct tgt_ring *ring = &tx_ring;
	unsigned long flags;
	unsigned int mask = 0;
	u32 idx;

	poll_wait(file, &tgt_poll_wait, wait);

	spin_lock_irqsave(&ring->tr_lock, flags);

	idx = ring->tr_idx ? ring->tr_idx - 1 : TGT_MAX_EVENTS - 1;
	ev = tgt_head_event(ring, idx);
	if (ev->hdr.status)
		mask |= POLLIN | POLLRDNORM;

	spin_unlock_irqrestore(&ring->tr_lock, flags);

	return mask;
}

static int uspace_ring_map(struct vm_area_struct *vma, unsigned long addr,
			   struct tgt_ring *ring)
{
	int i, err;

	for (i = 0; i < TGT_RING_PAGES; i++) {
		struct page *page = virt_to_page(ring->tr_pages[i]);
		err = vm_insert_page(vma, addr, page);
		if (err)
			return err;
		addr += PAGE_SIZE;
	}

	return 0;
}

static int tgt_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long addr;
	int err;

	if (vma->vm_pgoff)
		return -EINVAL;

	if (vma->vm_end - vma->vm_start != TGT_RING_SIZE * 2) {
		eprintk("mmap size must be %lu, not %lu \n",
			TGT_RING_SIZE * 2, vma->vm_end - vma->vm_start);
		return -EINVAL;
	}

	addr = vma->vm_start;
	err = uspace_ring_map(vma, addr, &tx_ring);
	if (err)
		return err;
	err = uspace_ring_map(vma, addr + TGT_RING_SIZE, &rx_ring);

	return err;
}

static int tgt_open(struct inode *inode, struct file *file)
{
	tx_ring.tr_idx = rx_ring.tr_idx = 0;

	return 0;
}

static const struct file_operations tgt_fops = {
	.owner		= THIS_MODULE,
	.open		= tgt_open,
	.poll		= tgt_poll,
	.write		= tgt_write,
	.mmap		= tgt_mmap,
	.llseek		= noop_llseek,
};

static struct miscdevice tgt_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "tgt",
	.fops = &tgt_fops,
};

static void tgt_ring_exit(struct tgt_ring *ring)
{
	int i;

	for (i = 0; i < TGT_RING_PAGES; i++)
		free_page(ring->tr_pages[i]);
}

static int tgt_ring_init(struct tgt_ring *ring)
{
	int i;

	spin_lock_init(&ring->tr_lock);

	for (i = 0; i < TGT_RING_PAGES; i++) {
		ring->tr_pages[i] = get_zeroed_page(GFP_KERNEL);
		if (!ring->tr_pages[i]) {
			eprintk("out of memory\n");
			return -ENOMEM;
		}
	}

	return 0;
}

void scsi_tgt_if_exit(void)
{
	tgt_ring_exit(&tx_ring);
	tgt_ring_exit(&rx_ring);
	misc_deregister(&tgt_miscdev);
}

int scsi_tgt_if_init(void)
{
	int err;

	err = tgt_ring_init(&tx_ring);
	if (err)
		return err;

	err = tgt_ring_init(&rx_ring);
	if (err)
		goto free_tx_ring;

	err = misc_register(&tgt_miscdev);
	if (err)
		goto free_rx_ring;

	return 0;
free_rx_ring:
	tgt_ring_exit(&rx_ring);
free_tx_ring:
	tgt_ring_exit(&tx_ring);

	return err;
}
