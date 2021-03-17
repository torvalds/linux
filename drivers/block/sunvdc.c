/* sunvdc.c: Sun LDOM Virtual Disk Client.
 *
 * Copyright (C) 2007, 2008 David S. Miller <davem@davemloft.net>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/genhd.h>
#include <linux/cdrom.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/scatterlist.h>

#include <asm/vio.h>
#include <asm/ldc.h>

#define DRV_MODULE_NAME		"sunvdc"
#define PFX DRV_MODULE_NAME	": "
#define DRV_MODULE_VERSION	"1.2"
#define DRV_MODULE_RELDATE	"November 24, 2014"

static char version[] =
	DRV_MODULE_NAME ".c:v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";
MODULE_AUTHOR("David S. Miller (davem@davemloft.net)");
MODULE_DESCRIPTION("Sun LDOM virtual disk client driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

#define VDC_TX_RING_SIZE	512
#define VDC_DEFAULT_BLK_SIZE	512

#define MAX_XFER_BLKS		(128 * 1024)
#define MAX_XFER_SIZE		(MAX_XFER_BLKS / VDC_DEFAULT_BLK_SIZE)
#define MAX_RING_COOKIES	((MAX_XFER_BLKS / PAGE_SIZE) + 2)

#define WAITING_FOR_LINK_UP	0x01
#define WAITING_FOR_TX_SPACE	0x02
#define WAITING_FOR_GEN_CMD	0x04
#define WAITING_FOR_ANY		-1

static struct workqueue_struct *sunvdc_wq;

struct vdc_req_entry {
	struct request		*req;
};

struct vdc_port {
	struct vio_driver_state	vio;

	struct gendisk		*disk;

	struct vdc_completion	*cmp;

	u64			req_id;
	u64			seq;
	struct vdc_req_entry	rq_arr[VDC_TX_RING_SIZE];

	unsigned long		ring_cookies;

	u64			max_xfer_size;
	u32			vdisk_block_size;

	u64			ldc_timeout;
	struct timer_list	ldc_reset_timer;
	struct work_struct	ldc_reset_work;

	/* The server fills these in for us in the disk attribute
	 * ACK packet.
	 */
	u64			operations;
	u32			vdisk_size;
	u8			vdisk_type;
	u8			vdisk_mtype;
	u32			vdisk_phys_blksz;

	char			disk_name[32];
};

static void vdc_ldc_reset(struct vdc_port *port);
static void vdc_ldc_reset_work(struct work_struct *work);
static void vdc_ldc_reset_timer(struct timer_list *t);

static inline struct vdc_port *to_vdc_port(struct vio_driver_state *vio)
{
	return container_of(vio, struct vdc_port, vio);
}

/* Ordered from largest major to lowest */
static struct vio_version vdc_versions[] = {
	{ .major = 1, .minor = 2 },
	{ .major = 1, .minor = 1 },
	{ .major = 1, .minor = 0 },
};

static inline int vdc_version_supported(struct vdc_port *port,
					u16 major, u16 minor)
{
	return port->vio.ver.major == major && port->vio.ver.minor >= minor;
}

#define VDCBLK_NAME	"vdisk"
static int vdc_major;
#define PARTITION_SHIFT	3

static inline u32 vdc_tx_dring_avail(struct vio_dring_state *dr)
{
	return vio_dring_avail(dr, VDC_TX_RING_SIZE);
}

static int vdc_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct gendisk *disk = bdev->bd_disk;
	sector_t nsect = get_capacity(disk);
	sector_t cylinders = nsect;

	geo->heads = 0xff;
	geo->sectors = 0x3f;
	sector_div(cylinders, geo->heads * geo->sectors);
	geo->cylinders = cylinders;
	if ((sector_t)(geo->cylinders + 1) * geo->heads * geo->sectors < nsect)
		geo->cylinders = 0xffff;

	return 0;
}

/* Add ioctl/CDROM_GET_CAPABILITY to support cdrom_id in udev
 * when vdisk_mtype is VD_MEDIA_TYPE_CD or VD_MEDIA_TYPE_DVD.
 * Needed to be able to install inside an ldom from an iso image.
 */
static int vdc_ioctl(struct block_device *bdev, fmode_t mode,
		     unsigned command, unsigned long argument)
{
	int i;
	struct gendisk *disk;

	switch (command) {
	case CDROMMULTISESSION:
		pr_debug(PFX "Multisession CDs not supported\n");
		for (i = 0; i < sizeof(struct cdrom_multisession); i++)
			if (put_user(0, (char __user *)(argument + i)))
				return -EFAULT;
		return 0;

	case CDROM_GET_CAPABILITY:
		disk = bdev->bd_disk;

		if (bdev->bd_disk && (disk->flags & GENHD_FL_CD))
			return 0;
		return -EINVAL;

	default:
		pr_debug(PFX "ioctl %08x not supported\n", command);
		return -EINVAL;
	}
}

static const struct block_device_operations vdc_fops = {
	.owner		= THIS_MODULE,
	.getgeo		= vdc_getgeo,
	.ioctl		= vdc_ioctl,
};

static void vdc_blk_queue_start(struct vdc_port *port)
{
	struct vio_dring_state *dr = &port->vio.drings[VIO_DRIVER_TX_RING];

	/* restart blk queue when ring is half emptied. also called after
	 * handshake completes, so check for initial handshake before we've
	 * allocated a disk.
	 */
	if (port->disk && blk_queue_stopped(port->disk->queue) &&
	    vdc_tx_dring_avail(dr) * 100 / VDC_TX_RING_SIZE >= 50) {
		blk_start_queue(port->disk->queue);
	}

}

static void vdc_finish(struct vio_driver_state *vio, int err, int waiting_for)
{
	if (vio->cmp &&
	    (waiting_for == -1 ||
	     vio->cmp->waiting_for == waiting_for)) {
		vio->cmp->err = err;
		complete(&vio->cmp->com);
		vio->cmp = NULL;
	}
}

static void vdc_handshake_complete(struct vio_driver_state *vio)
{
	struct vdc_port *port = to_vdc_port(vio);

	del_timer(&port->ldc_reset_timer);
	vdc_finish(vio, 0, WAITING_FOR_LINK_UP);
	vdc_blk_queue_start(port);
}

static int vdc_handle_unknown(struct vdc_port *port, void *arg)
{
	struct vio_msg_tag *pkt = arg;

	printk(KERN_ERR PFX "Received unknown msg [%02x:%02x:%04x:%08x]\n",
	       pkt->type, pkt->stype, pkt->stype_env, pkt->sid);
	printk(KERN_ERR PFX "Resetting connection.\n");

	ldc_disconnect(port->vio.lp);

	return -ECONNRESET;
}

static int vdc_send_attr(struct vio_driver_state *vio)
{
	struct vdc_port *port = to_vdc_port(vio);
	struct vio_disk_attr_info pkt;

	memset(&pkt, 0, sizeof(pkt));

	pkt.tag.type = VIO_TYPE_CTRL;
	pkt.tag.stype = VIO_SUBTYPE_INFO;
	pkt.tag.stype_env = VIO_ATTR_INFO;
	pkt.tag.sid = vio_send_sid(vio);

	pkt.xfer_mode = VIO_DRING_MODE;
	pkt.vdisk_block_size = port->vdisk_block_size;
	pkt.max_xfer_size = port->max_xfer_size;

	viodbg(HS, "SEND ATTR xfer_mode[0x%x] blksz[%u] max_xfer[%llu]\n",
	       pkt.xfer_mode, pkt.vdisk_block_size, pkt.max_xfer_size);

	return vio_ldc_send(&port->vio, &pkt, sizeof(pkt));
}

static int vdc_handle_attr(struct vio_driver_state *vio, void *arg)
{
	struct vdc_port *port = to_vdc_port(vio);
	struct vio_disk_attr_info *pkt = arg;

	viodbg(HS, "GOT ATTR stype[0x%x] ops[%llx] disk_size[%llu] disk_type[%x] "
	       "mtype[0x%x] xfer_mode[0x%x] blksz[%u] max_xfer[%llu]\n",
	       pkt->tag.stype, pkt->operations,
	       pkt->vdisk_size, pkt->vdisk_type, pkt->vdisk_mtype,
	       pkt->xfer_mode, pkt->vdisk_block_size,
	       pkt->max_xfer_size);

	if (pkt->tag.stype == VIO_SUBTYPE_ACK) {
		switch (pkt->vdisk_type) {
		case VD_DISK_TYPE_DISK:
		case VD_DISK_TYPE_SLICE:
			break;

		default:
			printk(KERN_ERR PFX "%s: Bogus vdisk_type 0x%x\n",
			       vio->name, pkt->vdisk_type);
			return -ECONNRESET;
		}

		if (pkt->vdisk_block_size > port->vdisk_block_size) {
			printk(KERN_ERR PFX "%s: BLOCK size increased "
			       "%u --> %u\n",
			       vio->name,
			       port->vdisk_block_size, pkt->vdisk_block_size);
			return -ECONNRESET;
		}

		port->operations = pkt->operations;
		port->vdisk_type = pkt->vdisk_type;
		if (vdc_version_supported(port, 1, 1)) {
			port->vdisk_size = pkt->vdisk_size;
			port->vdisk_mtype = pkt->vdisk_mtype;
		}
		if (pkt->max_xfer_size < port->max_xfer_size)
			port->max_xfer_size = pkt->max_xfer_size;
		port->vdisk_block_size = pkt->vdisk_block_size;

		port->vdisk_phys_blksz = VDC_DEFAULT_BLK_SIZE;
		if (vdc_version_supported(port, 1, 2))
			port->vdisk_phys_blksz = pkt->phys_block_size;

		return 0;
	} else {
		printk(KERN_ERR PFX "%s: Attribute NACK\n", vio->name);

		return -ECONNRESET;
	}
}

static void vdc_end_special(struct vdc_port *port, struct vio_disk_desc *desc)
{
	int err = desc->status;

	vdc_finish(&port->vio, -err, WAITING_FOR_GEN_CMD);
}

static void vdc_end_one(struct vdc_port *port, struct vio_dring_state *dr,
			unsigned int index)
{
	struct vio_disk_desc *desc = vio_dring_entry(dr, index);
	struct vdc_req_entry *rqe = &port->rq_arr[index];
	struct request *req;

	if (unlikely(desc->hdr.state != VIO_DESC_DONE))
		return;

	ldc_unmap(port->vio.lp, desc->cookies, desc->ncookies);
	desc->hdr.state = VIO_DESC_FREE;
	dr->cons = vio_dring_next(dr, index);

	req = rqe->req;
	if (req == NULL) {
		vdc_end_special(port, desc);
		return;
	}

	rqe->req = NULL;

	__blk_end_request(req, (desc->status ? BLK_STS_IOERR : 0), desc->size);

	vdc_blk_queue_start(port);
}

static int vdc_ack(struct vdc_port *port, void *msgbuf)
{
	struct vio_dring_state *dr = &port->vio.drings[VIO_DRIVER_TX_RING];
	struct vio_dring_data *pkt = msgbuf;

	if (unlikely(pkt->dring_ident != dr->ident ||
		     pkt->start_idx != pkt->end_idx ||
		     pkt->start_idx >= VDC_TX_RING_SIZE))
		return 0;

	vdc_end_one(port, dr, pkt->start_idx);

	return 0;
}

static int vdc_nack(struct vdc_port *port, void *msgbuf)
{
	/* XXX Implement me XXX */
	return 0;
}

static void vdc_event(void *arg, int event)
{
	struct vdc_port *port = arg;
	struct vio_driver_state *vio = &port->vio;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&vio->lock, flags);

	if (unlikely(event == LDC_EVENT_RESET)) {
		vio_link_state_change(vio, event);
		queue_work(sunvdc_wq, &port->ldc_reset_work);
		goto out;
	}

	if (unlikely(event == LDC_EVENT_UP)) {
		vio_link_state_change(vio, event);
		goto out;
	}

	if (unlikely(event != LDC_EVENT_DATA_READY)) {
		pr_warn(PFX "Unexpected LDC event %d\n", event);
		goto out;
	}

	err = 0;
	while (1) {
		union {
			struct vio_msg_tag tag;
			u64 raw[8];
		} msgbuf;

		err = ldc_read(vio->lp, &msgbuf, sizeof(msgbuf));
		if (unlikely(err < 0)) {
			if (err == -ECONNRESET)
				vio_conn_reset(vio);
			break;
		}
		if (err == 0)
			break;
		viodbg(DATA, "TAG [%02x:%02x:%04x:%08x]\n",
		       msgbuf.tag.type,
		       msgbuf.tag.stype,
		       msgbuf.tag.stype_env,
		       msgbuf.tag.sid);
		err = vio_validate_sid(vio, &msgbuf.tag);
		if (err < 0)
			break;

		if (likely(msgbuf.tag.type == VIO_TYPE_DATA)) {
			if (msgbuf.tag.stype == VIO_SUBTYPE_ACK)
				err = vdc_ack(port, &msgbuf);
			else if (msgbuf.tag.stype == VIO_SUBTYPE_NACK)
				err = vdc_nack(port, &msgbuf);
			else
				err = vdc_handle_unknown(port, &msgbuf);
		} else if (msgbuf.tag.type == VIO_TYPE_CTRL) {
			err = vio_control_pkt_engine(vio, &msgbuf);
		} else {
			err = vdc_handle_unknown(port, &msgbuf);
		}
		if (err < 0)
			break;
	}
	if (err < 0)
		vdc_finish(&port->vio, err, WAITING_FOR_ANY);
out:
	spin_unlock_irqrestore(&vio->lock, flags);
}

static int __vdc_tx_trigger(struct vdc_port *port)
{
	struct vio_dring_state *dr = &port->vio.drings[VIO_DRIVER_TX_RING];
	struct vio_dring_data hdr = {
		.tag = {
			.type		= VIO_TYPE_DATA,
			.stype		= VIO_SUBTYPE_INFO,
			.stype_env	= VIO_DRING_DATA,
			.sid		= vio_send_sid(&port->vio),
		},
		.dring_ident		= dr->ident,
		.start_idx		= dr->prod,
		.end_idx		= dr->prod,
	};
	int err, delay;

	hdr.seq = dr->snd_nxt;
	delay = 1;
	do {
		err = vio_ldc_send(&port->vio, &hdr, sizeof(hdr));
		if (err > 0) {
			dr->snd_nxt++;
			break;
		}
		udelay(delay);
		if ((delay <<= 1) > 128)
			delay = 128;
	} while (err == -EAGAIN);

	if (err == -ENOTCONN)
		vdc_ldc_reset(port);
	return err;
}

static int __send_request(struct request *req)
{
	struct vdc_port *port = req->rq_disk->private_data;
	struct vio_dring_state *dr = &port->vio.drings[VIO_DRIVER_TX_RING];
	struct scatterlist sg[MAX_RING_COOKIES];
	struct vdc_req_entry *rqe;
	struct vio_disk_desc *desc;
	unsigned int map_perm;
	int nsg, err, i;
	u64 len;
	u8 op;

	if (WARN_ON(port->ring_cookies > MAX_RING_COOKIES))
		return -EINVAL;

	map_perm = LDC_MAP_SHADOW | LDC_MAP_DIRECT | LDC_MAP_IO;

	if (rq_data_dir(req) == READ) {
		map_perm |= LDC_MAP_W;
		op = VD_OP_BREAD;
	} else {
		map_perm |= LDC_MAP_R;
		op = VD_OP_BWRITE;
	}

	sg_init_table(sg, port->ring_cookies);
	nsg = blk_rq_map_sg(req->q, req, sg);

	len = 0;
	for (i = 0; i < nsg; i++)
		len += sg[i].length;

	desc = vio_dring_cur(dr);

	err = ldc_map_sg(port->vio.lp, sg, nsg,
			 desc->cookies, port->ring_cookies,
			 map_perm);
	if (err < 0) {
		printk(KERN_ERR PFX "ldc_map_sg() failure, err=%d.\n", err);
		return err;
	}

	rqe = &port->rq_arr[dr->prod];
	rqe->req = req;

	desc->hdr.ack = VIO_ACK_ENABLE;
	desc->req_id = port->req_id;
	desc->operation = op;
	if (port->vdisk_type == VD_DISK_TYPE_DISK) {
		desc->slice = 0xff;
	} else {
		desc->slice = 0;
	}
	desc->status = ~0;
	desc->offset = (blk_rq_pos(req) << 9) / port->vdisk_block_size;
	desc->size = len;
	desc->ncookies = err;

	/* This has to be a non-SMP write barrier because we are writing
	 * to memory which is shared with the peer LDOM.
	 */
	wmb();
	desc->hdr.state = VIO_DESC_READY;

	err = __vdc_tx_trigger(port);
	if (err < 0) {
		printk(KERN_ERR PFX "vdc_tx_trigger() failure, err=%d\n", err);
	} else {
		port->req_id++;
		dr->prod = vio_dring_next(dr, dr->prod);
	}

	return err;
}

static void do_vdc_request(struct request_queue *rq)
{
	struct request *req;

	while ((req = blk_peek_request(rq)) != NULL) {
		struct vdc_port *port;
		struct vio_dring_state *dr;

		port = req->rq_disk->private_data;
		dr = &port->vio.drings[VIO_DRIVER_TX_RING];
		if (unlikely(vdc_tx_dring_avail(dr) < 1))
			goto wait;

		blk_start_request(req);

		if (__send_request(req) < 0) {
			blk_requeue_request(rq, req);
wait:
			/* Avoid pointless unplugs. */
			blk_stop_queue(rq);
			break;
		}
	}
}

static int generic_request(struct vdc_port *port, u8 op, void *buf, int len)
{
	struct vio_dring_state *dr;
	struct vio_completion comp;
	struct vio_disk_desc *desc;
	unsigned int map_perm;
	unsigned long flags;
	int op_len, err;
	void *req_buf;

	if (!(((u64)1 << (u64)op) & port->operations))
		return -EOPNOTSUPP;

	switch (op) {
	case VD_OP_BREAD:
	case VD_OP_BWRITE:
	default:
		return -EINVAL;

	case VD_OP_FLUSH:
		op_len = 0;
		map_perm = 0;
		break;

	case VD_OP_GET_WCE:
		op_len = sizeof(u32);
		map_perm = LDC_MAP_W;
		break;

	case VD_OP_SET_WCE:
		op_len = sizeof(u32);
		map_perm = LDC_MAP_R;
		break;

	case VD_OP_GET_VTOC:
		op_len = sizeof(struct vio_disk_vtoc);
		map_perm = LDC_MAP_W;
		break;

	case VD_OP_SET_VTOC:
		op_len = sizeof(struct vio_disk_vtoc);
		map_perm = LDC_MAP_R;
		break;

	case VD_OP_GET_DISKGEOM:
		op_len = sizeof(struct vio_disk_geom);
		map_perm = LDC_MAP_W;
		break;

	case VD_OP_SET_DISKGEOM:
		op_len = sizeof(struct vio_disk_geom);
		map_perm = LDC_MAP_R;
		break;

	case VD_OP_SCSICMD:
		op_len = 16;
		map_perm = LDC_MAP_RW;
		break;

	case VD_OP_GET_DEVID:
		op_len = sizeof(struct vio_disk_devid);
		map_perm = LDC_MAP_W;
		break;

	case VD_OP_GET_EFI:
	case VD_OP_SET_EFI:
		return -EOPNOTSUPP;
		break;
	};

	map_perm |= LDC_MAP_SHADOW | LDC_MAP_DIRECT | LDC_MAP_IO;

	op_len = (op_len + 7) & ~7;
	req_buf = kzalloc(op_len, GFP_KERNEL);
	if (!req_buf)
		return -ENOMEM;

	if (len > op_len)
		len = op_len;

	if (map_perm & LDC_MAP_R)
		memcpy(req_buf, buf, len);

	spin_lock_irqsave(&port->vio.lock, flags);

	dr = &port->vio.drings[VIO_DRIVER_TX_RING];

	/* XXX If we want to use this code generically we have to
	 * XXX handle TX ring exhaustion etc.
	 */
	desc = vio_dring_cur(dr);

	err = ldc_map_single(port->vio.lp, req_buf, op_len,
			     desc->cookies, port->ring_cookies,
			     map_perm);
	if (err < 0) {
		spin_unlock_irqrestore(&port->vio.lock, flags);
		kfree(req_buf);
		return err;
	}

	init_completion(&comp.com);
	comp.waiting_for = WAITING_FOR_GEN_CMD;
	port->vio.cmp = &comp;

	desc->hdr.ack = VIO_ACK_ENABLE;
	desc->req_id = port->req_id;
	desc->operation = op;
	desc->slice = 0;
	desc->status = ~0;
	desc->offset = 0;
	desc->size = op_len;
	desc->ncookies = err;

	/* This has to be a non-SMP write barrier because we are writing
	 * to memory which is shared with the peer LDOM.
	 */
	wmb();
	desc->hdr.state = VIO_DESC_READY;

	err = __vdc_tx_trigger(port);
	if (err >= 0) {
		port->req_id++;
		dr->prod = vio_dring_next(dr, dr->prod);
		spin_unlock_irqrestore(&port->vio.lock, flags);

		wait_for_completion(&comp.com);
		err = comp.err;
	} else {
		port->vio.cmp = NULL;
		spin_unlock_irqrestore(&port->vio.lock, flags);
	}

	if (map_perm & LDC_MAP_W)
		memcpy(buf, req_buf, len);

	kfree(req_buf);

	return err;
}

static int vdc_alloc_tx_ring(struct vdc_port *port)
{
	struct vio_dring_state *dr = &port->vio.drings[VIO_DRIVER_TX_RING];
	unsigned long len, entry_size;
	int ncookies;
	void *dring;

	entry_size = sizeof(struct vio_disk_desc) +
		(sizeof(struct ldc_trans_cookie) * port->ring_cookies);
	len = (VDC_TX_RING_SIZE * entry_size);

	ncookies = VIO_MAX_RING_COOKIES;
	dring = ldc_alloc_exp_dring(port->vio.lp, len,
				    dr->cookies, &ncookies,
				    (LDC_MAP_SHADOW |
				     LDC_MAP_DIRECT |
				     LDC_MAP_RW));
	if (IS_ERR(dring))
		return PTR_ERR(dring);

	dr->base = dring;
	dr->entry_size = entry_size;
	dr->num_entries = VDC_TX_RING_SIZE;
	dr->prod = dr->cons = 0;
	dr->pending = VDC_TX_RING_SIZE;
	dr->ncookies = ncookies;

	return 0;
}

static void vdc_free_tx_ring(struct vdc_port *port)
{
	struct vio_dring_state *dr = &port->vio.drings[VIO_DRIVER_TX_RING];

	if (dr->base) {
		ldc_free_exp_dring(port->vio.lp, dr->base,
				   (dr->entry_size * dr->num_entries),
				   dr->cookies, dr->ncookies);
		dr->base = NULL;
		dr->entry_size = 0;
		dr->num_entries = 0;
		dr->pending = 0;
		dr->ncookies = 0;
	}
}

static int vdc_port_up(struct vdc_port *port)
{
	struct vio_completion comp;

	init_completion(&comp.com);
	comp.err = 0;
	comp.waiting_for = WAITING_FOR_LINK_UP;
	port->vio.cmp = &comp;

	vio_port_up(&port->vio);
	wait_for_completion(&comp.com);
	return comp.err;
}

static void vdc_port_down(struct vdc_port *port)
{
	ldc_disconnect(port->vio.lp);
	ldc_unbind(port->vio.lp);
	vdc_free_tx_ring(port);
	vio_ldc_free(&port->vio);
}

static int probe_disk(struct vdc_port *port)
{
	struct request_queue *q;
	struct gendisk *g;
	int err;

	err = vdc_port_up(port);
	if (err)
		return err;

	/* Using version 1.2 means vdisk_phys_blksz should be set unless the
	 * disk is reserved by another system.
	 */
	if (vdc_version_supported(port, 1, 2) && !port->vdisk_phys_blksz)
		return -ENODEV;

	if (vdc_version_supported(port, 1, 1)) {
		/* vdisk_size should be set during the handshake, if it wasn't
		 * then the underlying disk is reserved by another system
		 */
		if (port->vdisk_size == -1)
			return -ENODEV;
	} else {
		struct vio_disk_geom geom;

		err = generic_request(port, VD_OP_GET_DISKGEOM,
				      &geom, sizeof(geom));
		if (err < 0) {
			printk(KERN_ERR PFX "VD_OP_GET_DISKGEOM returns "
			       "error %d\n", err);
			return err;
		}
		port->vdisk_size = ((u64)geom.num_cyl *
				    (u64)geom.num_hd *
				    (u64)geom.num_sec);
	}

	q = blk_init_queue(do_vdc_request, &port->vio.lock);
	if (!q) {
		printk(KERN_ERR PFX "%s: Could not allocate queue.\n",
		       port->vio.name);
		return -ENOMEM;
	}
	g = alloc_disk(1 << PARTITION_SHIFT);
	if (!g) {
		printk(KERN_ERR PFX "%s: Could not allocate gendisk.\n",
		       port->vio.name);
		blk_cleanup_queue(q);
		return -ENOMEM;
	}

	port->disk = g;

	/* Each segment in a request is up to an aligned page in size. */
	blk_queue_segment_boundary(q, PAGE_SIZE - 1);
	blk_queue_max_segment_size(q, PAGE_SIZE);

	blk_queue_max_segments(q, port->ring_cookies);
	blk_queue_max_hw_sectors(q, port->max_xfer_size);
	g->major = vdc_major;
	g->first_minor = port->vio.vdev->dev_no << PARTITION_SHIFT;
	strcpy(g->disk_name, port->disk_name);

	g->fops = &vdc_fops;
	g->queue = q;
	g->private_data = port;

	set_capacity(g, port->vdisk_size);

	if (vdc_version_supported(port, 1, 1)) {
		switch (port->vdisk_mtype) {
		case VD_MEDIA_TYPE_CD:
			pr_info(PFX "Virtual CDROM %s\n", port->disk_name);
			g->flags |= GENHD_FL_CD;
			g->flags |= GENHD_FL_REMOVABLE;
			set_disk_ro(g, 1);
			break;

		case VD_MEDIA_TYPE_DVD:
			pr_info(PFX "Virtual DVD %s\n", port->disk_name);
			g->flags |= GENHD_FL_CD;
			g->flags |= GENHD_FL_REMOVABLE;
			set_disk_ro(g, 1);
			break;

		case VD_MEDIA_TYPE_FIXED:
			pr_info(PFX "Virtual Hard disk %s\n", port->disk_name);
			break;
		}
	}

	blk_queue_physical_block_size(q, port->vdisk_phys_blksz);

	pr_info(PFX "%s: %u sectors (%u MB) protocol %d.%d\n",
	       g->disk_name,
	       port->vdisk_size, (port->vdisk_size >> (20 - 9)),
	       port->vio.ver.major, port->vio.ver.minor);

	device_add_disk(&port->vio.vdev->dev, g);

	return 0;
}

static struct ldc_channel_config vdc_ldc_cfg = {
	.event		= vdc_event,
	.mtu		= 64,
	.mode		= LDC_MODE_UNRELIABLE,
};

static struct vio_driver_ops vdc_vio_ops = {
	.send_attr		= vdc_send_attr,
	.handle_attr		= vdc_handle_attr,
	.handshake_complete	= vdc_handshake_complete,
};

static void print_version(void)
{
	static int version_printed;

	if (version_printed++ == 0)
		printk(KERN_INFO "%s", version);
}

struct vdc_check_port_data {
	int	dev_no;
	char	*type;
};

static int vdc_device_probed(struct device *dev, void *arg)
{
	struct vio_dev *vdev = to_vio_dev(dev);
	struct vdc_check_port_data *port_data;

	port_data = (struct vdc_check_port_data *)arg;

	if ((vdev->dev_no == port_data->dev_no) &&
	    (!(strcmp((char *)&vdev->type, port_data->type))) &&
		dev_get_drvdata(dev)) {
		/* This device has already been configured
		 * by vdc_port_probe()
		 */
		return 1;
	} else {
		return 0;
	}
}

/* Determine whether the VIO device is part of an mpgroup
 * by locating all the virtual-device-port nodes associated
 * with the parent virtual-device node for the VIO device
 * and checking whether any of these nodes are vdc-ports
 * which have already been configured.
 *
 * Returns true if this device is part of an mpgroup and has
 * already been probed.
 */
static bool vdc_port_mpgroup_check(struct vio_dev *vdev)
{
	struct vdc_check_port_data port_data;
	struct device *dev;

	port_data.dev_no = vdev->dev_no;
	port_data.type = (char *)&vdev->type;

	dev = device_find_child(vdev->dev.parent, &port_data,
				vdc_device_probed);

	if (dev)
		return true;

	return false;
}

static int vdc_port_probe(struct vio_dev *vdev, const struct vio_device_id *id)
{
	struct mdesc_handle *hp;
	struct vdc_port *port;
	int err;
	const u64 *ldc_timeout;

	print_version();

	hp = mdesc_grab();

	err = -ENODEV;
	if ((vdev->dev_no << PARTITION_SHIFT) & ~(u64)MINORMASK) {
		printk(KERN_ERR PFX "Port id [%llu] too large.\n",
		       vdev->dev_no);
		goto err_out_release_mdesc;
	}

	/* Check if this device is part of an mpgroup */
	if (vdc_port_mpgroup_check(vdev)) {
		printk(KERN_WARNING
			"VIO: Ignoring extra vdisk port %s",
			dev_name(&vdev->dev));
		goto err_out_release_mdesc;
	}

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	err = -ENOMEM;
	if (!port) {
		printk(KERN_ERR PFX "Cannot allocate vdc_port.\n");
		goto err_out_release_mdesc;
	}

	if (vdev->dev_no >= 26)
		snprintf(port->disk_name, sizeof(port->disk_name),
			 VDCBLK_NAME "%c%c",
			 'a' + ((int)vdev->dev_no / 26) - 1,
			 'a' + ((int)vdev->dev_no % 26));
	else
		snprintf(port->disk_name, sizeof(port->disk_name),
			 VDCBLK_NAME "%c", 'a' + ((int)vdev->dev_no % 26));
	port->vdisk_size = -1;

	/* Actual wall time may be double due to do_generic_file_read() doing
	 * a readahead I/O first, and once that fails it will try to read a
	 * single page.
	 */
	ldc_timeout = mdesc_get_property(hp, vdev->mp, "vdc-timeout", NULL);
	port->ldc_timeout = ldc_timeout ? *ldc_timeout : 0;
	timer_setup(&port->ldc_reset_timer, vdc_ldc_reset_timer, 0);
	INIT_WORK(&port->ldc_reset_work, vdc_ldc_reset_work);

	err = vio_driver_init(&port->vio, vdev, VDEV_DISK,
			      vdc_versions, ARRAY_SIZE(vdc_versions),
			      &vdc_vio_ops, port->disk_name);
	if (err)
		goto err_out_free_port;

	port->vdisk_block_size = VDC_DEFAULT_BLK_SIZE;
	port->max_xfer_size = MAX_XFER_SIZE;
	port->ring_cookies = MAX_RING_COOKIES;

	err = vio_ldc_alloc(&port->vio, &vdc_ldc_cfg, port);
	if (err)
		goto err_out_free_port;

	err = vdc_alloc_tx_ring(port);
	if (err)
		goto err_out_free_ldc;

	err = probe_disk(port);
	if (err)
		goto err_out_free_tx_ring;

	/* Note that the device driver_data is used to determine
	 * whether the port has been probed.
	 */
	dev_set_drvdata(&vdev->dev, port);

	mdesc_release(hp);

	return 0;

err_out_free_tx_ring:
	vdc_free_tx_ring(port);

err_out_free_ldc:
	vio_ldc_free(&port->vio);

err_out_free_port:
	kfree(port);

err_out_release_mdesc:
	mdesc_release(hp);
	return err;
}

static int vdc_port_remove(struct vio_dev *vdev)
{
	struct vdc_port *port = dev_get_drvdata(&vdev->dev);

	if (port) {
		unsigned long flags;

		spin_lock_irqsave(&port->vio.lock, flags);
		blk_stop_queue(port->disk->queue);
		spin_unlock_irqrestore(&port->vio.lock, flags);

		flush_work(&port->ldc_reset_work);
		del_timer_sync(&port->ldc_reset_timer);
		del_timer_sync(&port->vio.timer);

		del_gendisk(port->disk);
		blk_cleanup_queue(port->disk->queue);
		put_disk(port->disk);
		port->disk = NULL;

		vdc_free_tx_ring(port);
		vio_ldc_free(&port->vio);

		dev_set_drvdata(&vdev->dev, NULL);

		kfree(port);
	}
	return 0;
}

static void vdc_requeue_inflight(struct vdc_port *port)
{
	struct vio_dring_state *dr = &port->vio.drings[VIO_DRIVER_TX_RING];
	u32 idx;

	for (idx = dr->cons; idx != dr->prod; idx = vio_dring_next(dr, idx)) {
		struct vio_disk_desc *desc = vio_dring_entry(dr, idx);
		struct vdc_req_entry *rqe = &port->rq_arr[idx];
		struct request *req;

		ldc_unmap(port->vio.lp, desc->cookies, desc->ncookies);
		desc->hdr.state = VIO_DESC_FREE;
		dr->cons = vio_dring_next(dr, idx);

		req = rqe->req;
		if (req == NULL) {
			vdc_end_special(port, desc);
			continue;
		}

		rqe->req = NULL;
		blk_requeue_request(port->disk->queue, req);
	}
}

static void vdc_queue_drain(struct vdc_port *port)
{
	struct request *req;

	while ((req = blk_fetch_request(port->disk->queue)) != NULL)
		__blk_end_request_all(req, BLK_STS_IOERR);
}

static void vdc_ldc_reset_timer(struct timer_list *t)
{
	struct vdc_port *port = from_timer(port, t, ldc_reset_timer);
	struct vio_driver_state *vio = &port->vio;
	unsigned long flags;

	spin_lock_irqsave(&vio->lock, flags);
	if (!(port->vio.hs_state & VIO_HS_COMPLETE)) {
		pr_warn(PFX "%s ldc down %llu seconds, draining queue\n",
			port->disk_name, port->ldc_timeout);
		vdc_queue_drain(port);
		vdc_blk_queue_start(port);
	}
	spin_unlock_irqrestore(&vio->lock, flags);
}

static void vdc_ldc_reset_work(struct work_struct *work)
{
	struct vdc_port *port;
	struct vio_driver_state *vio;
	unsigned long flags;

	port = container_of(work, struct vdc_port, ldc_reset_work);
	vio = &port->vio;

	spin_lock_irqsave(&vio->lock, flags);
	vdc_ldc_reset(port);
	spin_unlock_irqrestore(&vio->lock, flags);
}

static void vdc_ldc_reset(struct vdc_port *port)
{
	int err;

	assert_spin_locked(&port->vio.lock);

	pr_warn(PFX "%s ldc link reset\n", port->disk_name);
	blk_stop_queue(port->disk->queue);
	vdc_requeue_inflight(port);
	vdc_port_down(port);

	err = vio_ldc_alloc(&port->vio, &vdc_ldc_cfg, port);
	if (err) {
		pr_err(PFX "%s vio_ldc_alloc:%d\n", port->disk_name, err);
		return;
	}

	err = vdc_alloc_tx_ring(port);
	if (err) {
		pr_err(PFX "%s vio_alloc_tx_ring:%d\n", port->disk_name, err);
		goto err_free_ldc;
	}

	if (port->ldc_timeout)
		mod_timer(&port->ldc_reset_timer,
			  round_jiffies(jiffies + HZ * port->ldc_timeout));
	mod_timer(&port->vio.timer, round_jiffies(jiffies + HZ));
	return;

err_free_ldc:
	vio_ldc_free(&port->vio);
}

static const struct vio_device_id vdc_port_match[] = {
	{
		.type = "vdc-port",
	},
	{},
};
MODULE_DEVICE_TABLE(vio, vdc_port_match);

static struct vio_driver vdc_port_driver = {
	.id_table	= vdc_port_match,
	.probe		= vdc_port_probe,
	.remove		= vdc_port_remove,
	.name		= "vdc_port",
};

static int __init vdc_init(void)
{
	int err;

	sunvdc_wq = alloc_workqueue("sunvdc", 0, 0);
	if (!sunvdc_wq)
		return -ENOMEM;

	err = register_blkdev(0, VDCBLK_NAME);
	if (err < 0)
		goto out_free_wq;

	vdc_major = err;

	err = vio_register_driver(&vdc_port_driver);
	if (err)
		goto out_unregister_blkdev;

	return 0;

out_unregister_blkdev:
	unregister_blkdev(vdc_major, VDCBLK_NAME);
	vdc_major = 0;

out_free_wq:
	destroy_workqueue(sunvdc_wq);
	return err;
}

static void __exit vdc_exit(void)
{
	vio_unregister_driver(&vdc_port_driver);
	unregister_blkdev(vdc_major, VDCBLK_NAME);
	destroy_workqueue(sunvdc_wq);
}

module_init(vdc_init);
module_exit(vdc_exit);
