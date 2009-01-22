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
#define DRV_MODULE_VERSION	"1.0"
#define DRV_MODULE_RELDATE	"June 25, 2007"

static char version[] __devinitdata =
	DRV_MODULE_NAME ".c:v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";
MODULE_AUTHOR("David S. Miller (davem@davemloft.net)");
MODULE_DESCRIPTION("Sun LDOM virtual disk client driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

#define VDC_TX_RING_SIZE	256

#define WAITING_FOR_LINK_UP	0x01
#define WAITING_FOR_TX_SPACE	0x02
#define WAITING_FOR_GEN_CMD	0x04
#define WAITING_FOR_ANY		-1

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

	/* The server fills these in for us in the disk attribute
	 * ACK packet.
	 */
	u64			operations;
	u32			vdisk_size;
	u8			vdisk_type;

	char			disk_name[32];

	struct vio_disk_geom	geom;
	struct vio_disk_vtoc	label;
};

static inline struct vdc_port *to_vdc_port(struct vio_driver_state *vio)
{
	return container_of(vio, struct vdc_port, vio);
}

/* Ordered from largest major to lowest */
static struct vio_version vdc_versions[] = {
	{ .major = 1, .minor = 0 },
};

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
	struct vdc_port *port = disk->private_data;

	geo->heads = (u8) port->geom.num_hd;
	geo->sectors = (u8) port->geom.num_sec;
	geo->cylinders = port->geom.num_cyl;

	return 0;
}

static struct block_device_operations vdc_fops = {
	.owner		= THIS_MODULE,
	.getgeo		= vdc_getgeo,
};

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
	vdc_finish(vio, 0, WAITING_FOR_LINK_UP);
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
	       "xfer_mode[0x%x] blksz[%u] max_xfer[%llu]\n",
	       pkt->tag.stype, pkt->operations,
	       pkt->vdisk_size, pkt->vdisk_type,
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
		port->vdisk_size = pkt->vdisk_size;
		port->vdisk_type = pkt->vdisk_type;
		if (pkt->max_xfer_size < port->max_xfer_size)
			port->max_xfer_size = pkt->max_xfer_size;
		port->vdisk_block_size = pkt->vdisk_block_size;
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

static void vdc_end_request(struct request *req, int error, int num_sectors)
{
	__blk_end_request(req, error, num_sectors << 9);
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
	dr->cons = (index + 1) & (VDC_TX_RING_SIZE - 1);

	req = rqe->req;
	if (req == NULL) {
		vdc_end_special(port, desc);
		return;
	}

	rqe->req = NULL;

	vdc_end_request(req, (desc->status ? -EIO : 0), desc->size >> 9);

	if (blk_queue_stopped(port->disk->queue))
		blk_start_queue(port->disk->queue);
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

	if (unlikely(event == LDC_EVENT_RESET ||
		     event == LDC_EVENT_UP)) {
		vio_link_state_change(vio, event);
		spin_unlock_irqrestore(&vio->lock, flags);
		return;
	}

	if (unlikely(event != LDC_EVENT_DATA_READY)) {
		printk(KERN_WARNING PFX "Unexpected LDC event %d\n", event);
		spin_unlock_irqrestore(&vio->lock, flags);
		return;
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

	return err;
}

static int __send_request(struct request *req)
{
	struct vdc_port *port = req->rq_disk->private_data;
	struct vio_dring_state *dr = &port->vio.drings[VIO_DRIVER_TX_RING];
	struct scatterlist sg[port->ring_cookies];
	struct vdc_req_entry *rqe;
	struct vio_disk_desc *desc;
	unsigned int map_perm;
	int nsg, err, i;
	u64 len;
	u8 op;

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

	if (unlikely(vdc_tx_dring_avail(dr) < 1)) {
		blk_stop_queue(port->disk->queue);
		err = -ENOMEM;
		goto out;
	}

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
	desc->offset = (req->sector << 9) / port->vdisk_block_size;
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
		dr->prod = (dr->prod + 1) & (VDC_TX_RING_SIZE - 1);
	}
out:

	return err;
}

static void do_vdc_request(struct request_queue *q)
{
	while (1) {
		struct request *req = elv_next_request(q);

		if (!req)
			break;

		blkdev_dequeue_request(req);
		if (__send_request(req) < 0)
			vdc_end_request(req, -EIO, req->hard_nr_sectors);
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

	if (!(((u64)1 << ((u64)op - 1)) & port->operations))
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
		dr->prod = (dr->prod + 1) & (VDC_TX_RING_SIZE - 1);
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

static int __devinit vdc_alloc_tx_ring(struct vdc_port *port)
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

static int probe_disk(struct vdc_port *port)
{
	struct vio_completion comp;
	struct request_queue *q;
	struct gendisk *g;
	int err;

	init_completion(&comp.com);
	comp.err = 0;
	comp.waiting_for = WAITING_FOR_LINK_UP;
	port->vio.cmp = &comp;

	vio_port_up(&port->vio);

	wait_for_completion(&comp.com);
	if (comp.err)
		return comp.err;

	err = generic_request(port, VD_OP_GET_VTOC,
			      &port->label, sizeof(port->label));
	if (err < 0) {
		printk(KERN_ERR PFX "VD_OP_GET_VTOC returns error %d\n", err);
		return err;
	}

	err = generic_request(port, VD_OP_GET_DISKGEOM,
			      &port->geom, sizeof(port->geom));
	if (err < 0) {
		printk(KERN_ERR PFX "VD_OP_GET_DISKGEOM returns "
		       "error %d\n", err);
		return err;
	}

	port->vdisk_size = ((u64)port->geom.num_cyl *
			    (u64)port->geom.num_hd *
			    (u64)port->geom.num_sec);

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

	blk_queue_max_hw_segments(q, port->ring_cookies);
	blk_queue_max_phys_segments(q, port->ring_cookies);
	blk_queue_max_sectors(q, port->max_xfer_size);
	g->major = vdc_major;
	g->first_minor = port->vio.vdev->dev_no << PARTITION_SHIFT;
	strcpy(g->disk_name, port->disk_name);

	g->fops = &vdc_fops;
	g->queue = q;
	g->private_data = port;
	g->driverfs_dev = &port->vio.vdev->dev;

	set_capacity(g, port->vdisk_size);

	printk(KERN_INFO PFX "%s: %u sectors (%u MB)\n",
	       g->disk_name,
	       port->vdisk_size, (port->vdisk_size >> (20 - 9)));

	add_disk(g);

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

static void __devinit print_version(void)
{
	static int version_printed;

	if (version_printed++ == 0)
		printk(KERN_INFO "%s", version);
}

static int __devinit vdc_port_probe(struct vio_dev *vdev,
				    const struct vio_device_id *id)
{
	struct mdesc_handle *hp;
	struct vdc_port *port;
	int err;

	print_version();

	hp = mdesc_grab();

	err = -ENODEV;
	if ((vdev->dev_no << PARTITION_SHIFT) & ~(u64)MINORMASK) {
		printk(KERN_ERR PFX "Port id [%llu] too large.\n",
		       vdev->dev_no);
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

	err = vio_driver_init(&port->vio, vdev, VDEV_DISK,
			      vdc_versions, ARRAY_SIZE(vdc_versions),
			      &vdc_vio_ops, port->disk_name);
	if (err)
		goto err_out_free_port;

	port->vdisk_block_size = 512;
	port->max_xfer_size = ((128 * 1024) / port->vdisk_block_size);
	port->ring_cookies = ((port->max_xfer_size *
			       port->vdisk_block_size) / PAGE_SIZE) + 2;

	err = vio_ldc_alloc(&port->vio, &vdc_ldc_cfg, port);
	if (err)
		goto err_out_free_port;

	err = vdc_alloc_tx_ring(port);
	if (err)
		goto err_out_free_ldc;

	err = probe_disk(port);
	if (err)
		goto err_out_free_tx_ring;

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
		del_timer_sync(&port->vio.timer);

		vdc_free_tx_ring(port);
		vio_ldc_free(&port->vio);

		dev_set_drvdata(&vdev->dev, NULL);

		kfree(port);
	}
	return 0;
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
	.driver		= {
		.name	= "vdc_port",
		.owner	= THIS_MODULE,
	}
};

static int __init vdc_init(void)
{
	int err;

	err = register_blkdev(0, VDCBLK_NAME);
	if (err < 0)
		goto out_err;

	vdc_major = err;

	err = vio_register_driver(&vdc_port_driver);
	if (err)
		goto out_unregister_blkdev;

	return 0;

out_unregister_blkdev:
	unregister_blkdev(vdc_major, VDCBLK_NAME);
	vdc_major = 0;

out_err:
	return err;
}

static void __exit vdc_exit(void)
{
	vio_unregister_driver(&vdc_port_driver);
	unregister_blkdev(vdc_major, VDCBLK_NAME);
}

module_init(vdc_init);
module_exit(vdc_exit);
