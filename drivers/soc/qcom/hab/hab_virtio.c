// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <uapi/linux/virtio_ids.h>
#include <linux/version.h>
#include <linux/cma.h>

#include "hab_virtio.h" /* requires hab.h */
#include "hab_trace_os.h"

#define HAB_VIRTIO_DEVICE_ID_HAB	88
#define HAB_VIRTIO_DEVICE_ID_BUFFERQ	89
#define HAB_VIRTIO_DEVICE_ID_MISC	90
#define HAB_VIRTIO_DEVICE_ID_AUDIO	91
#define HAB_VIRTIO_DEVICE_ID_CAMERA	92
#define HAB_VIRTIO_DEVICE_ID_DISPLAY	93
#define HAB_VIRTIO_DEVICE_ID_GRAPHICS	94
#define HAB_VIRTIO_DEVICE_ID_VIDEO	95
#define HAB_VIRTIO_DEVICE_ID_VNW	96
#define HAB_VIRTIO_DEVICE_ID_EXT	97
#define HAB_VIRTIO_DEVICE_ID_GPCE	98

/* all probed virtio_hab stored in this list */
static struct list_head vhab_list = LIST_HEAD_INIT(vhab_list);
static DEFINE_SPINLOCK(vh_lock);

static struct virtio_device_tbl {
	int32_t mmid;
	__u32 device;
	struct virtio_device *vdev;
} vdev_tbl[] = {
	{ HAB_MMID_ALL_AREA, HAB_VIRTIO_DEVICE_ID_HAB, NULL }, /* hab */
	{ MM_BUFFERQ_1, HAB_VIRTIO_DEVICE_ID_BUFFERQ, NULL },
	{ MM_MISC, HAB_VIRTIO_DEVICE_ID_MISC, NULL },
	{ MM_AUD_1, HAB_VIRTIO_DEVICE_ID_AUDIO, NULL },
	{ MM_CAM_1, HAB_VIRTIO_DEVICE_ID_CAMERA, NULL },
	{ MM_DISP_1, HAB_VIRTIO_DEVICE_ID_DISPLAY, NULL },
	{ MM_GFX, HAB_VIRTIO_DEVICE_ID_GRAPHICS, NULL },
	{ MM_VID, HAB_VIRTIO_DEVICE_ID_VIDEO, NULL },
	{ MM_VNW_1, HAB_VIRTIO_DEVICE_ID_VNW, NULL },
	{ MM_EXT_1, HAB_VIRTIO_DEVICE_ID_EXT, NULL },
	{ MM_GPCE_1, HAB_VIRTIO_DEVICE_ID_GPCE, NULL },
};

enum pool_type_t {
	PT_OUT_SMALL = 0, /* 512 bytes */
	PT_OUT_MEDIUM,    /* 5120 bytes */
	PT_OUT_LARGE,     /* 51200 bytes */
	PT_IN,          /* 5120 bytes */
	PT_MAX
};

#define GUARD_BAND_SZ 20

struct vh_buf_header {
	char *buf; /* buffer starting address */
	int size; /* the maximum payload size */
	enum pool_type_t pool_type;
	int index; /* debugging only */

	int payload_size; /* actual payload used size */
	struct list_head node;
	char padding[GUARD_BAND_SZ];
};

/* Such below *_BUF_SIZEs are the sizes used to store the payload */
#define IN_BUF_SIZE         5120
#define OUT_SMALL_BUF_SIZE  512
#define OUT_MEDIUM_BUF_SIZE 5120
#define OUT_LARGE_BUF_SIZE  (64 * 1024)

#define IN_BUF_NUM         100 /*64*/
#define OUT_SMALL_BUF_NUM  200 /*64*/
#define OUT_MEDIUM_BUF_NUM 100 /*20*/
#define OUT_LARGE_BUF_NUM  10

#define IN_BUF_POOL_SLOT    (GUARD_BAND_SZ + \
			     sizeof(struct vh_buf_header) + \
			     sizeof(struct hab_header) + \
			     IN_BUF_SIZE)
#define OUT_SMALL_BUF_SLOT  (GUARD_BAND_SZ + \
			     sizeof(struct vh_buf_header) + \
			     sizeof(struct hab_header) + \
			     OUT_SMALL_BUF_SIZE)
#define OUT_MEDIUM_BUF_SLOT (GUARD_BAND_SZ + \
			     sizeof(struct vh_buf_header) + \
			     sizeof(struct hab_header) + \
			     OUT_MEDIUM_BUF_SIZE)
#define OUT_LARGE_BUF_SLOT  (GUARD_BAND_SZ + \
			     sizeof(struct vh_buf_header) + \
			     sizeof(struct hab_header) + \
			     OUT_LARGE_BUF_SIZE)

#define IN_POOL_SIZE (IN_BUF_POOL_SLOT * IN_BUF_NUM)
#define OUT_SMALL_POOL_SIZE (OUT_SMALL_BUF_SLOT * OUT_SMALL_BUF_NUM)
#define OUT_MEDIUM_POOL_SIZE (OUT_MEDIUM_BUF_SLOT * OUT_MEDIUM_BUF_NUM)
#define OUT_LARGE_POOL_SIZE (OUT_LARGE_BUF_SLOT * OUT_LARGE_BUF_NUM)

struct virtio_hab *get_vh(struct virtio_device *vdev)
{
	struct virtio_hab *vh = NULL;
	unsigned long flags;

	spin_lock_irqsave(&vh_lock, flags);
	list_for_each_entry(vh, &vhab_list, node) {
		if (vdev == vh->vdev)
			break;
	}
	spin_unlock_irqrestore(&vh_lock, flags);

	return vh;
}

static struct vq_pchan *get_virtio_pchan(struct virtio_hab *vhab,
						struct virtqueue *vq)
{
	int index = vq->index - vhab->vqs_offset;

	if (index > hab_driver.ndevices * HAB_PCHAN_VQ_MAX || index < 0) {
		pr_err("wrong vq index %d total hab device %d\n",
			index, hab_driver.ndevices);
		return NULL;
	} else
		return &vhab->vqpchans[index/2];
}

/* vq event callback - send/out buf returns */
static void virthab_recv_txq(struct virtqueue *vq)
{
	struct virtio_hab *vh = get_vh(vq->vdev);
	struct vq_pchan *vpc = get_virtio_pchan(vh, vq);
	struct vh_buf_header *hd = NULL;
	unsigned long flags;
	unsigned int len;

	if (!vpc)
		return;

	trace_hab_recv_txq_start(vpc->pchan);

	spin_lock_irqsave(&vpc->lock[HAB_PCHAN_TX_VQ], flags);
	if (vpc->pchan_ready) {
		if (vq != vpc->vq[HAB_PCHAN_TX_VQ])
			pr_err("failed to match txq %pK expecting %pK\n",
				vq, vpc->vq[HAB_PCHAN_TX_VQ]);

		while ((hd = (struct vh_buf_header *)virtqueue_get_buf(vq, &len)) != NULL) {
			if ((hd->index < 0) || (hd->pool_type < 0) ||
				(hd->pool_type > PT_OUT_LARGE))
				pr_err("corrupted outbuf %pK %d %d %d\n",
					hd->buf, hd->size, hd->pool_type,
					hd->index);

			hd->payload_size = 0;
			switch (hd->pool_type) {
			case PT_OUT_SMALL:
				if ((hd->index > OUT_SMALL_BUF_NUM) ||
					(hd->size != OUT_SMALL_BUF_SIZE))
					pr_err("small buf index corrupted %pK %pK %d %d\n",
						hd, hd->buf, hd->index,
						hd->size);
				list_add_tail(&hd->node, &vpc->s_list);
				vpc->s_cnt++;
				break;

			case PT_OUT_MEDIUM:
				if ((hd->index > OUT_MEDIUM_BUF_NUM) ||
					(hd->size != OUT_MEDIUM_BUF_SIZE))
					pr_err("medium buf index corrupted %pK %pK %d %d\n",
						hd, hd->buf, hd->index,
						hd->size);
				list_add_tail(&hd->node, &vpc->m_list);
				vpc->m_cnt++;
				break;

			case PT_OUT_LARGE:
				if ((hd->index > OUT_MEDIUM_BUF_NUM) ||
					(hd->size != OUT_LARGE_BUF_SIZE))
					pr_err("large buf index corrupted %pK %pK %d %d\n",
						hd, hd->buf, hd->index,
						hd->size);
				list_add_tail(&hd->node, &vpc->l_list);
				vpc->l_cnt++;
				break;
			default:
				pr_err("invalid pool type %d received on txq\n",
					hd->pool_type);
			}

		}
	}
	spin_unlock_irqrestore(&vpc->lock[HAB_PCHAN_TX_VQ], flags);

	trace_hab_recv_txq_end(vpc->pchan);

	wake_up(&vpc->out_wq);
}

/* vq sts callback - recv/in buf returns */
static void virthab_recv_rxq(unsigned long p)
{
	struct virtqueue *vq = (struct virtqueue *)p;
	struct virtio_hab *vh = get_vh(vq->vdev);
	struct vq_pchan *vpc = get_virtio_pchan(vh, vq);
	char *inbuf;
	unsigned int len;
	struct physical_channel *pchan = NULL;
	struct scatterlist sg[1];
	int rc;
	struct vh_buf_header *hd = NULL;

	if (!vpc)
		return;

	pchan = vpc->pchan;
	if (vq != vpc->vq[HAB_PCHAN_RX_VQ])
		pr_err("%s failed to match rxq %pK expecting %pK\n",
			vq->name, vq, vpc->vq[HAB_PCHAN_RX_VQ]);

	trace_hab_recv_rxq_start(vpc->pchan);

	spin_lock(&vpc->lock[HAB_PCHAN_RX_VQ]);

	while ((hd = virtqueue_get_buf(vpc->vq[HAB_PCHAN_RX_VQ], &len)) != NULL) {
		vpc->in_cnt--;

		/* sanity check */
		if ((hd->index < 0) || (hd->index > IN_BUF_NUM) ||
			hd->pool_type != PT_IN || hd->size != IN_BUF_SIZE) {
			pr_err("corrupted inbuf %pK %pK %d %d %d\n",
				hd, hd->buf, hd->size, hd->pool_type,
				hd->index);
			break;
		}

		hd->payload_size = 0;

		inbuf = hd->buf;

		/* inbuf should be one of the in1/in2 msg should will be
		 * consumed before inbuf is kicked to PVM
		 */
		vpc->read_data = inbuf;
		vpc->read_size = len;
		vpc->read_offset = 0;

		if (!pchan)
			pr_err("failed to find matching pchan for vq %s %pK\n",
				vq->name, vq);
		else {
			/* parse and handle the input */
			spin_unlock(&vpc->lock[HAB_PCHAN_RX_VQ]);
			trace_hab_pchan_recv_start(pchan);
			rc = hab_msg_recv(pchan, (struct hab_header *)inbuf);

			spin_lock(&vpc->lock[HAB_PCHAN_RX_VQ]);
			if (pchan->sequence_rx + 1 != ((struct hab_header *)inbuf)->sequence)
				pr_err("%s: expected sequence_rx is %u, received is %u\n",
						pchan->name,
						pchan->sequence_rx,
						((struct hab_header *)inbuf)->sequence);
			pchan->sequence_rx = ((struct hab_header *)inbuf)->sequence;

			if (rc && rc != -EINVAL)
				pr_err("%s hab_msg_recv wrong %d\n",
					pchan->name, rc);
		}

		/* return the inbuf to PVM after consuming */
		sg_init_one(sg, hd->buf, IN_BUF_SIZE + sizeof(struct hab_header));
		if (vpc->pchan_ready) {
			rc = virtqueue_add_inbuf(vq, sg, 1, hd, GFP_ATOMIC);
			if (rc)
				pr_err("failed to queue inbuf to PVM %d\n",
					rc);

			/* bundle kick? */
			vpc->in_cnt++;
			rc = virtqueue_kick(vq);
			if (!rc)
				pr_err("failed to kick inbuf to PVM %d\n", rc);
		} else {
			pr_err("vq not ready %d\n", vpc->pchan_ready);
			rc = -ENODEV;
		}

	}
	spin_unlock(&vpc->lock[HAB_PCHAN_RX_VQ]);

	trace_hab_recv_rxq_end(vpc->pchan);
}

static void virthab_recv_rxq_task(struct virtqueue *vq)
{
	struct virtio_hab *vh = get_vh(vq->vdev);
	struct vq_pchan *vpc = get_virtio_pchan(vh, vq);

	if (!vpc)
		return;

	tasklet_hi_schedule(&vpc->task);
}

static void init_pool_list(void *pool, int buf_size, int buf_num,
				enum pool_type_t pool_type,
				struct list_head *pool_head,
				wait_queue_head_t *wq, int *cnt)
{
	char *ptr;
	struct vh_buf_header *hd;
	int i;

	INIT_LIST_HEAD(pool_head);
	if (wq)
		init_waitqueue_head(wq);

	ptr = pool;
	for (i = 0; i < buf_num; i++) {
		hd = (struct vh_buf_header *)(ptr + GUARD_BAND_SZ);
		hd->buf = ptr + GUARD_BAND_SZ + sizeof(struct vh_buf_header);
		hd->size = buf_size;
		hd->pool_type = pool_type;
		hd->index = i;
		hd->payload_size = 0;
		list_add_tail(&hd->node, pool_head);
		ptr = hd->buf + sizeof(struct hab_header) + buf_size;
		(*cnt)++;
	}
}

/* queue all the inbufs on all pchans/vqs */
int virthab_queue_inbufs(struct virtio_hab *vh, int alloc)
{
	int ret, size;
	int i;
	struct scatterlist sg[1];
	struct vh_buf_header *hd, *hd_tmp;

	for (i = 0; i < vh->mmid_range; i++) {
		struct vq_pchan *vpc = &vh->vqpchans[i];

		if (alloc) {
			vpc->in_cnt = 0;
			vpc->s_cnt = 0;
			vpc->m_cnt = 0;
			vpc->l_cnt = 0;

			vpc->in_pool = kmalloc(IN_POOL_SIZE, GFP_KERNEL);
			vpc->s_pool = kmalloc(OUT_SMALL_POOL_SIZE, GFP_KERNEL);
			vpc->m_pool = kmalloc(OUT_MEDIUM_POOL_SIZE, GFP_KERNEL);
			vpc->l_pool = kmalloc(OUT_LARGE_POOL_SIZE, GFP_KERNEL);
			if (!vpc->in_pool || !vpc->s_pool || !vpc->m_pool ||
				!vpc->l_pool) {
				pr_err("failed to alloc buf %d %pK %d %pK %d %pK %d %pK\n",
					IN_POOL_SIZE, vpc->in_pool,
					OUT_SMALL_POOL_SIZE, vpc->s_pool,
					OUT_MEDIUM_POOL_SIZE, vpc->m_pool,
					OUT_LARGE_POOL_SIZE, vpc->l_pool);
				return -ENOMEM;
			}

			init_waitqueue_head(&vpc->out_wq);
			init_pool_list(vpc->in_pool, IN_BUF_SIZE,
					IN_BUF_NUM, PT_IN,
					&vpc->in_list, NULL,
					&vpc->in_cnt);
			init_pool_list(vpc->s_pool, OUT_SMALL_BUF_SIZE,
					OUT_SMALL_BUF_NUM, PT_OUT_SMALL,
					&vpc->s_list,  NULL,
					&vpc->s_cnt);
			init_pool_list(vpc->m_pool, OUT_MEDIUM_BUF_SIZE,
					OUT_MEDIUM_BUF_NUM, PT_OUT_MEDIUM,
					&vpc->m_list, NULL,
					&vpc->m_cnt);
			init_pool_list(vpc->l_pool, OUT_LARGE_BUF_SIZE,
					OUT_LARGE_BUF_NUM, PT_OUT_LARGE,
					&vpc->l_list, NULL,
					&vpc->l_cnt);

			pr_debug("VQ buf allocated %s %d %d %d %d %d %d %d %d %pK %pK %pK %pK\n",
				vpc->vq[HAB_PCHAN_RX_VQ]->name,
				IN_POOL_SIZE, OUT_SMALL_POOL_SIZE,
				OUT_MEDIUM_POOL_SIZE, OUT_LARGE_POOL_SIZE,
				vpc->in_cnt, vpc->s_cnt, vpc->m_cnt,
				vpc->l_cnt, vpc->in_list, vpc->s_list,
				vpc->m_list, vpc->l_list);
		}

		spin_lock(&vpc->lock[HAB_PCHAN_RX_VQ]);
		size = virtqueue_get_vring_size(vpc->vq[HAB_PCHAN_RX_VQ]);
		pr_debug("vq %s vring index %d num %d pchan %s\n",
			vpc->vq[HAB_PCHAN_RX_VQ]->name,
			vpc->vq[HAB_PCHAN_RX_VQ]->index, size,
			vpc->pchan->name);

		list_for_each_entry_safe(hd, hd_tmp, &vpc->in_list, node) {
			list_del(&hd->node);
			sg_init_one(sg, hd->buf, IN_BUF_SIZE + sizeof(struct hab_header));
			ret = virtqueue_add_inbuf(vpc->vq[HAB_PCHAN_RX_VQ], sg,
							1, hd, GFP_ATOMIC);
			if (ret) {
				pr_err("failed to queue %s inbuf %d to PVM %d\n",
					vpc->vq[HAB_PCHAN_RX_VQ]->name,
					vpc->in_cnt, ret);
			}
			vpc->in_cnt--;
		}

		ret = virtqueue_kick(vpc->vq[HAB_PCHAN_RX_VQ]);
		if (!ret)
			pr_err("failed to kick %d %s ret %d cnt %d\n", i,
				vpc->vq[HAB_PCHAN_RX_VQ]->name, ret,
				vpc->in_cnt);
		spin_unlock(&vpc->lock[HAB_PCHAN_RX_VQ]);
	}
	return 0;
}
EXPORT_SYMBOL(virthab_queue_inbufs);

int virthab_init_vqs_pre(struct virtio_hab *vh)
{
	struct vq_pchan *vpchans = vh->vqpchans;
	vq_callback_t **cbs = vh->cbs;
	char **names = vh->names;
	char *temp;
	int i, idx = 0;
	struct hab_device *habdev = NULL;

	pr_debug("2 callbacks %pK %pK\n", (void *)virthab_recv_txq,
		(void *)virthab_recv_rxq);

	habdev = find_hab_device(vh->mmid_start);
	if (!habdev) {
		pr_err("failed to locate mmid %d range %d\n",
			vh->mmid_start, vh->mmid_range);
		return -ENODEV;
	}

	/* do sanity check */
	for (i = 0; i < hab_driver.ndevices; i++)
		if (habdev == &hab_driver.devp[i])
			break;
	if (i + vh->mmid_range > hab_driver.ndevices) {
		pr_err("invalid mmid %d range %d total %d\n",
			vh->mmid_start, vh->mmid_range,
			hab_driver.ndevices);
		return -EINVAL;
	}

	idx = i;

	for (i = 0; i < vh->mmid_range; i++) {
		habdev = &hab_driver.devp[idx + i];

		/* ToDo: each cb should only apply to one vq */
		cbs[i * HAB_PCHAN_VQ_MAX + HAB_PCHAN_TX_VQ] = virthab_recv_txq;
		cbs[i * HAB_PCHAN_VQ_MAX + HAB_PCHAN_RX_VQ] = virthab_recv_rxq_task;

		strscpy(names[i * HAB_PCHAN_VQ_MAX + HAB_PCHAN_TX_VQ],
				habdev->name, sizeof(habdev->name));
		temp = names[i * HAB_PCHAN_VQ_MAX + HAB_PCHAN_TX_VQ];
		temp[0] = 't'; temp[1] = 'x'; temp[2] = 'q';

		strscpy(names[i * HAB_PCHAN_VQ_MAX + HAB_PCHAN_RX_VQ],
				habdev->name, sizeof(habdev->name));
		temp = names[i * HAB_PCHAN_VQ_MAX + HAB_PCHAN_RX_VQ];
		temp[0] = 'r'; temp[1] = 'x'; temp[2] = 'q';

		vpchans[i].mmid = habdev->id;
		if (list_empty(&habdev->pchannels))
			pr_err("pchan is not initialized %s slot %d mmid %d\n",
				habdev->name, i, habdev->id);
		else
			/* GVM only has one instance of the pchan for each mmid
			 * (no multi VMs)
			 */
			vpchans[i].pchan = list_first_entry(&habdev->pchannels,
							struct physical_channel,
							node);
	}

	return 0;
}
EXPORT_SYMBOL(virthab_init_vqs_pre);

int virthab_init_vqs_post(struct virtio_hab *vh)
{
	struct vq_pchan *vpchans = vh->vqpchans;
	int i;
	struct virtio_pchan_link *link;

	/* map all the vqs to pchans */
	for (i = 0; i < vh->mmid_range; i++) {
		vpchans[i].vhab = vh;

		spin_lock_init(&vpchans[i].lock[HAB_PCHAN_TX_VQ]);
		spin_lock_init(&vpchans[i].lock[HAB_PCHAN_RX_VQ]);

		vpchans[i].vq[HAB_PCHAN_TX_VQ] =
			vh->vqs[i * HAB_PCHAN_VQ_MAX + HAB_PCHAN_TX_VQ];
		vpchans[i].vq[HAB_PCHAN_RX_VQ] =
			vh->vqs[i * HAB_PCHAN_VQ_MAX + HAB_PCHAN_RX_VQ];

		vpchans[i].index[HAB_PCHAN_TX_VQ] =
			vh->vqs[i * HAB_PCHAN_VQ_MAX + HAB_PCHAN_TX_VQ]->index;
		vpchans[i].index[HAB_PCHAN_RX_VQ] =
			vh->vqs[i * HAB_PCHAN_VQ_MAX + HAB_PCHAN_RX_VQ]->index;

		tasklet_init(&vpchans[i].task, virthab_recv_rxq,
			(unsigned long)vpchans[i].vq[HAB_PCHAN_RX_VQ]);

		vpchans[i].pchan_ready = true;
		link = (struct virtio_pchan_link *)vpchans[i].pchan->hyp_data;
		link->vpc = &vpchans[i];
		link->vhab = vh;
		pr_debug("mmid %d slot %d vhab %pK vdev %pK vq tx %pK pchan %pK\n",
			vpchans[i].mmid, i, vpchans[i].vhab,
			vpchans[i].vhab->vdev, vpchans[i].vq[HAB_PCHAN_TX_VQ],
			vpchans[i].pchan);
	}

	return 0;
}
EXPORT_SYMBOL(virthab_init_vqs_post);

static int virthab_init_vqs(struct virtio_hab *vh)
{
	int ret;
	vq_callback_t **cbs = vh->cbs;
	char **names = vh->names;

	ret = virthab_init_vqs_pre(vh);
	if (ret)
		return ret;

	pr_debug("mmid %d request %d vqs\n", vh->mmid_start,
		vh->mmid_range * HAB_PCHAN_VQ_MAX);

	ret = virtio_find_vqs(vh->vdev, vh->mmid_range * HAB_PCHAN_VQ_MAX,
				vh->vqs, cbs, (const char * const*)names, NULL);
	if (ret) {
		pr_err("failed to find vqs %d\n", ret);
		return ret;
	}

	pr_debug("find vqs OK %d\n", ret);
	vh->vqs_offset = 0; /* this virtio device has all the vqs to itself */

	ret = virthab_init_vqs_post(vh);
	if (ret)
		return ret;

	return 0;
}

static int virthab_alloc_mmid_device(struct virtio_hab *vh,
			uint32_t mmid_start, int mmid_range)
{
	int i;

	vh->vqs = kzalloc(sizeof(struct virtqueue *) * mmid_range *
				HAB_PCHAN_VQ_MAX, GFP_KERNEL);
	if (!vh->vqs)
		return -ENOMEM;

	vh->cbs = kzalloc(sizeof(vq_callback_t *) * mmid_range *
				HAB_PCHAN_VQ_MAX, GFP_KERNEL);
	if (!vh->vqs)
		return -ENOMEM;

	vh->names = kzalloc(sizeof(char *) * mmid_range *
				HAB_PCHAN_VQ_MAX, GFP_KERNEL);
	if (!vh->names)
		return -ENOMEM;

	vh->vqpchans = kcalloc(hab_driver.ndevices, sizeof(struct vq_pchan),
				GFP_KERNEL);
	if (!vh->vqpchans)
		return -ENOMEM;

	/* loop through all pchans before vq registration for name creation */
	for (i = 0; i < mmid_range * HAB_PCHAN_VQ_MAX; i++) {
		vh->names[i] = kzalloc(MAX_VMID_NAME_SIZE + 2, GFP_KERNEL);
		if (!vh->names[i])
			return -ENOMEM;
	}

	vh->mmid_start = mmid_start;
	vh->mmid_range = mmid_range;

	return 0;
}

int virthab_alloc(struct virtio_device *vdev, struct virtio_hab **pvh,
				  uint32_t mmid_start, int mmid_range)
{
	struct virtio_hab *vh;
	int ret;
	unsigned long flags;

	vh = kzalloc(sizeof(*vh), GFP_KERNEL);
	if (!vh)
		return -ENOMEM;

	ret = virthab_alloc_mmid_device(vh, mmid_start, mmid_range);

	if (!ret)
		pr_debug("alloc done mmid %d range %d\n",
				mmid_start, mmid_range);
	else
		return ret;

	vh->vdev = vdev; /* store virtio device locally */

	*pvh = vh;
	spin_lock_irqsave(&vh_lock, flags);
	list_add_tail(&vh->node, &vhab_list);
	spin_unlock_irqrestore(&vh_lock, flags);

	spin_lock_init(&vh->mlock);
	pr_debug("start vqs init vh list empty %d\n", list_empty(&vhab_list));

	return 0;
}
EXPORT_SYMBOL(virthab_alloc);

static void taken_range_calc(uint32_t mmid_start, int mmid_range,
				uint32_t *taken_start, uint32_t *taken_end)
{
	int i;

	*taken_start = 0;
	*taken_end = 0;
	for (i = 0; i < hab_driver.ndevices; i++) {
		if (mmid_start == hab_driver.devp[i].id) {
			*taken_start = mmid_start;
			*taken_end = hab_driver.devp[i + mmid_range - 1].id;
			pr_debug("taken range %d %d\n", *taken_start, *taken_end);
		}
	}
}

static int virthab_pchan_avail_check(__u32 id, uint32_t mmid_start, int mmid_range)
{
	int avail = 1; /* available */
	struct virtio_hab *vh = NULL;
	uint32_t taken_start = 0, taken_end = 0;

	list_for_each_entry(vh, &vhab_list, node) {
		if (vh->vdev->id.device == id) { /* virtio device id check */
			avail = 0;
			break;
		}
		taken_range_calc(vh->mmid_start, vh->mmid_range,
				&taken_start, &taken_end);
		if (mmid_start >= taken_start && mmid_start <= taken_end) {
			avail = 0;
			break;
		}
	}
	pr_debug("avail check input %d %d %d ret %d\n", id, mmid_start, mmid_range, avail);
	return avail;
}

static void virthab_store_vdev(int32_t mmid, struct virtio_device *vdev)
{
	int i;
	int sz = ARRAY_SIZE(vdev_tbl);

	for (i = 0; i < sz; i++) {
		if (vdev_tbl[i].mmid == mmid) {
			vdev_tbl[i].vdev = vdev;
			break;
		}
	}
}

struct virtio_device *virthab_get_vdev(int32_t mmid)
{
	int i;
	struct virtio_device *ret = NULL;
	int sz = ARRAY_SIZE(vdev_tbl);

	for (i = 0; i < sz; i++) {
		if (vdev_tbl[i].mmid == mmid) {
			ret = vdev_tbl[i].vdev;
			break;
		}
	}

	return ret;
}
EXPORT_SYMBOL(virthab_get_vdev);

/* probe is called when GVM detects virtio device from devtree */
static int virthab_probe(struct virtio_device *vdev)
{
	struct virtio_hab *vh = NULL;
	int err = 0, ret = 0;
	int mmid_range = hab_driver.ndevices;
	uint32_t mmid_start = hab_driver.devp[0].id;

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1)) {
		pr_err("virtio has feature missing\n");
		return -ENODEV;
	}
	pr_info("virtio has feature %llX virtio devid %X vid %d empty %d\n",
		vdev->features, vdev->id.device, vdev->id.vendor,
		list_empty(&vhab_list));

	/* find out which virtio device is calling us.
	 * if this is hab's own virtio device, all the pchans are available
	 */
	if (vdev->id.device == HAB_VIRTIO_DEVICE_ID_HAB) {
		/* all MMIDs are taken cannot co-exist with others */
		mmid_start = hab_driver.devp[0].id;
		mmid_range = hab_driver.ndevices;
		virthab_store_vdev(HAB_MMID_ALL_AREA, vdev);
	} else if (vdev->id.device == HAB_VIRTIO_DEVICE_ID_BUFFERQ) {
		mmid_start = MM_BUFFERQ_1;
		mmid_range = MM_BUFFERQ_END - MM_BUFFERQ_START - 1;
		virthab_store_vdev(MM_BUFFERQ_1, vdev);
	} else if (vdev->id.device == HAB_VIRTIO_DEVICE_ID_MISC) {
		mmid_start = MM_MISC;
		mmid_range = MM_MISC_END - MM_MISC_START - 1;
		virthab_store_vdev(MM_MISC, vdev);
	} else if (vdev->id.device == HAB_VIRTIO_DEVICE_ID_AUDIO) {
		mmid_start = MM_AUD_1;
		mmid_range = MM_AUD_END - MM_AUD_START - 1;
		virthab_store_vdev(MM_AUD_1, vdev);
	} else if (vdev->id.device == HAB_VIRTIO_DEVICE_ID_CAMERA) {
		mmid_start = MM_CAM_1;
		mmid_range = MM_CAM_END - MM_CAM_START - 1;
		virthab_store_vdev(MM_CAM_1, vdev);
	} else if (vdev->id.device == HAB_VIRTIO_DEVICE_ID_DISPLAY) {
		mmid_start = MM_DISP_1;
		mmid_range = MM_DISP_END - MM_DISP_START - 1;
		virthab_store_vdev(MM_DISP_1, vdev);
	} else if (vdev->id.device == HAB_VIRTIO_DEVICE_ID_GRAPHICS) {
		mmid_start = MM_GFX;
		mmid_range = MM_GFX_END - MM_GFX_START - 1;
		virthab_store_vdev(MM_GFX, vdev);
	} else if (vdev->id.device == HAB_VIRTIO_DEVICE_ID_VIDEO) {
		mmid_start = MM_VID;
		mmid_range = MM_VID_END - MM_VID_START - 1;
		virthab_store_vdev(MM_VID, vdev);
	} else if (vdev->id.device == HAB_VIRTIO_DEVICE_ID_VNW) {
		mmid_start = MM_VNW_1;
		mmid_range = MM_VNW_END - MM_VNW_START - 1;
		virthab_store_vdev(MM_VNW_1, vdev);
	} else if (vdev->id.device == HAB_VIRTIO_DEVICE_ID_EXT) {
		mmid_start = MM_EXT_1;
		mmid_range = MM_EXT_END - MM_EXT_START - 1;
		virthab_store_vdev(MM_EXT_1, vdev);
	} else if (vdev->id.device == HAB_VIRTIO_DEVICE_ID_GPCE) {
		mmid_start = MM_GPCE_1;
		mmid_range = MM_GPCE_END - MM_GPCE_START - 1;
		virthab_store_vdev(MM_GPCE_1, vdev);
	} else {
		pr_err("unknown virtio device is detected %d\n",
			vdev->id.device);
		mmid_start = 0;
		mmid_range = 0;
	}
	pr_debug("virtio device id %d mmid %d range %d\n",
			vdev->id.device, mmid_start, mmid_range);

	if (!virthab_pchan_avail_check(vdev->id.device, mmid_start, mmid_range))
		return -EINVAL;

	ret = virthab_alloc(vdev, &vh, mmid_start, mmid_range);
	if (!ret)
		pr_debug("alloc done %d mmid %d range %d\n",
			ret, mmid_start, mmid_range);
	else {
		pr_err("probe failed mmid %d range %d\n",
			mmid_start, mmid_range);
		return ret;
	}

	err = virthab_init_vqs(vh);
	if (err)
		goto err_init_vq;

	virtio_device_ready(vdev);
	pr_info("virto device ready\n");

	vh->ready = true;
	pr_debug("store virto device %pK empty %d\n", vh, list_empty(&vhab_list));

	ret = virthab_queue_inbufs(vh, 1);
	if (ret)
		return ret;

	return 0;

err_init_vq:
	kfree(vh);
	pr_err("virtio input probe failed %d\n", err);
	return err;
}

static void virthab_remove(struct virtio_device *vdev)
{
	struct virtio_hab *vh = get_vh(vdev);
	void *buf;
	unsigned long flags;
	int i, j;
	struct virtio_pchan_link *link;

	spin_lock_irqsave(&vh->mlock, flags);
	vh->ready = false;
	spin_unlock_irqrestore(&vh->mlock, flags);

	vdev->config->reset(vdev);
	for (i = 0; i < vh->mmid_range; i++) {
		struct vq_pchan *vpc = &vh->vqpchans[i];

		j = 0;
		while ((buf =
			virtqueue_detach_unused_buf(vpc->vq[HAB_PCHAN_RX_VQ]))
								!= NULL) {
			pr_debug("free vq-pchan %s %d buf %d %pK\n",
				vpc->vq[HAB_PCHAN_RX_VQ]->name, i, j, buf);
		}
		kfree(vpc->in_pool);
		kfree(vpc->s_pool);
		kfree(vpc->m_pool);
		kfree(vpc->l_pool);

		link = vpc->pchan->hyp_data;
		link->vhab = NULL;
		link->vpc = NULL;
	}

	vdev->config->del_vqs(vdev);
	kfree(vh->vqs);
	kfree(vh->cbs);
	for (i = 0; i < vh->mmid_range * HAB_PCHAN_VQ_MAX; i++)
		kfree(vh->names[i]);
	kfree(vh->names);
	kfree(vh->vqpchans);

	spin_lock_irqsave(&vh_lock, flags);
	list_del(&vh->node);
	spin_unlock_irqrestore(&vh_lock, flags);
	pr_info("remove virthab mmid %d range %d empty %d\n",
		vh->mmid_start, vh->mmid_range, list_empty(&vhab_list));
	kfree(vh);
}

#ifdef CONFIG_PM_SLEEP
static int virthab_freeze(struct virtio_device *vdev)
{
	struct virtio_hab *vh = get_vh(vdev);
	unsigned long flags;

	spin_lock_irqsave(&vh->mlock, flags);
	vh->ready = false;
	spin_unlock_irqrestore(&vh->mlock, flags);

	vdev->config->del_vqs(vdev);
	return 0;
}

static int virthab_restore(struct virtio_device *vdev)
{
	struct virtio_hab *vh = get_vh(vdev);
	int err;

	err = virthab_init_vqs(vh);
	if (err)
		return err;

	virtio_device_ready(vdev);
	vh->ready = true;
	virthab_queue_inbufs(vh, 0);
	return 0;
}
#endif

static unsigned int features[] = {
	/* none */
};
static struct virtio_device_id id_table[] = {
	{ HAB_VIRTIO_DEVICE_ID_HAB, VIRTIO_DEV_ANY_ID }, /* virtio hab with all mmids */
	{ HAB_VIRTIO_DEVICE_ID_BUFFERQ, VIRTIO_DEV_ANY_ID }, /* virtio bufferq only */
	{ HAB_VIRTIO_DEVICE_ID_MISC, VIRTIO_DEV_ANY_ID }, /* virtio misc */
	{ HAB_VIRTIO_DEVICE_ID_AUDIO, VIRTIO_DEV_ANY_ID }, /* virtio audio */
	{ HAB_VIRTIO_DEVICE_ID_CAMERA, VIRTIO_DEV_ANY_ID }, /* virtio camera */
	{ HAB_VIRTIO_DEVICE_ID_DISPLAY, VIRTIO_DEV_ANY_ID }, /* virtio display */
	{ HAB_VIRTIO_DEVICE_ID_GRAPHICS, VIRTIO_DEV_ANY_ID }, /* virtio graphics */
	{ HAB_VIRTIO_DEVICE_ID_VIDEO, VIRTIO_DEV_ANY_ID }, /* virtio video */
	{ HAB_VIRTIO_DEVICE_ID_VNW, VIRTIO_DEV_ANY_ID }, /* virtio vnw */
	{ HAB_VIRTIO_DEVICE_ID_EXT, VIRTIO_DEV_ANY_ID }, /* virtio external */
	{ HAB_VIRTIO_DEVICE_ID_GPCE, VIRTIO_DEV_ANY_ID }, /* virtio gpce */
	{ 0 },
};

static struct virtio_driver virtio_hab_driver = {
	.driver.name         = KBUILD_MODNAME,
	.driver.owner        = THIS_MODULE,
	.feature_table       = features,
	.feature_table_size  = ARRAY_SIZE(features),
	.id_table            = id_table,
	.probe               = virthab_probe,
	.remove              = virthab_remove,
#ifdef CONFIG_PM_SLEEP
	.freeze              = virthab_freeze,
	.restore             = virthab_restore,
#endif
};

/* register / unregister */
#ifdef HAB_DESKTOP
extern struct cma *dma_contiguous_default_area;
static struct cma *c;
static struct page *cma_pgs;
#endif

int hab_hypervisor_register(void)
{
#ifdef HAB_DESKTOP
	/* Just need a device for memory allocation */
	c = dev_get_cma_area(hab_driver.dev[0]);
	cma_pgs = cma_alloc(c, (16 * 1024 * 1024) >> PAGE_SHIFT, 0
	, false
	); /* better from cmdline parsing */

	if (!c || !cma_pgs)
		pr_err("failed to reserve 16MB cma region base_pfn %lX cnt %lX\n",
			   cma_get_base(c), cma_get_size(c));
#endif
	pr_info("alloc virtio_pchan_array of %d devices\n",
			hab_driver.ndevices);
	return 0;
}

void hab_hypervisor_unregister(void)
{
	hab_hypervisor_unregister_common();

	unregister_virtio_driver(&virtio_hab_driver);
#ifdef HAB_DESKTOP
	if (c && cma_pgs)
		cma_release(c, cma_pgs, (16 * 1024 * 1024) >> PAGE_SHIFT);
#endif
}

void hab_pipe_read_dump(struct physical_channel *pchan) {};

void dump_hab_wq(struct physical_channel *pchan) {};

static struct vh_buf_header *get_vh_buf_header(spinlock_t *lock,
			unsigned long *irq_flags, struct list_head *list,
			wait_queue_head_t *wq, int *cnt,
			int nonblocking_flag)
{
	struct vh_buf_header *hd = NULL;
	unsigned long flags = *irq_flags;

	if (list_empty(list) && nonblocking_flag)
		return ERR_PTR(-EAGAIN);

	while (list_empty(list)) {
		spin_unlock_irqrestore(lock, flags);
		wait_event(*wq, !list_empty(list));
		spin_lock_irqsave(lock, flags);
	}

	hd = list_first_entry(list, struct vh_buf_header, node);
	list_del(&hd->node);

	*irq_flags = flags;
	(*cnt)--;
	return hd;
}

int physical_channel_send(struct physical_channel *pchan,
			struct hab_header *header, void *payload,
			unsigned int flags)
{
	size_t sizebytes = HAB_HEADER_GET_SIZE(*header);
	struct virtio_pchan_link *link =
			(struct virtio_pchan_link *)pchan->hyp_data;
	struct vq_pchan *vpc = link->vpc;
	struct scatterlist sgout[1];
	char *outbuf = NULL;
	int rc;
	unsigned long lock_flags;
	struct vh_buf_header *hd = NULL;
	int nonblocking_flag = flags & HABMM_SOCKET_SEND_FLAGS_NON_BLOCKING;

	if (link->vpc == NULL) {
		pr_err("%s: %s link->vpc not ready\n", __func__, pchan->name);
		return -ENODEV;
	}

	if (sizebytes > OUT_LARGE_BUF_SIZE) {
		pr_err("send size %zd overflow %d available %d %d %d\n",
			   sizebytes, OUT_LARGE_BUF_SIZE,
			   vpc->s_cnt, vpc->m_cnt, vpc->l_cnt);
		return -EINVAL;
	}

	trace_hab_pchan_send_start(pchan);

	spin_lock_irqsave(&vpc->lock[HAB_PCHAN_TX_VQ], lock_flags);
	if (vpc->pchan_ready) {
		/* pick the available outbuf */
		if (sizebytes <= OUT_SMALL_BUF_SIZE) {
			hd = get_vh_buf_header(&vpc->lock[HAB_PCHAN_TX_VQ],
						&lock_flags, &vpc->s_list,
						&vpc->out_wq, &vpc->s_cnt,
						nonblocking_flag);
		} else if (sizebytes <= OUT_MEDIUM_BUF_SIZE) {
			hd = get_vh_buf_header(&vpc->lock[HAB_PCHAN_TX_VQ],
						&lock_flags, &vpc->m_list,
						&vpc->out_wq, &vpc->m_cnt,
						nonblocking_flag);
		} else {
			hd = get_vh_buf_header(&vpc->lock[HAB_PCHAN_TX_VQ],
						&lock_flags, &vpc->l_list,
						&vpc->out_wq, &vpc->l_cnt,
						nonblocking_flag);
		}

		if (IS_ERR(hd) && nonblocking_flag) {
			spin_unlock_irqrestore(&vpc->lock[HAB_PCHAN_TX_VQ], lock_flags);
			pr_info("get_vh_buf_header failed in non-blocking mode\n");
			return PTR_ERR(hd);
		}

		if (HAB_HEADER_GET_TYPE(*header) == HAB_PAYLOAD_TYPE_PROFILE) {
			struct habmm_xing_vm_stat *pstat =
				(struct habmm_xing_vm_stat *)payload;
			struct timespec64 ts;

			ktime_get_ts64(&ts);
			pstat->tx_sec = ts.tv_sec;
			pstat->tx_usec = ts.tv_nsec/NSEC_PER_USEC;
		}

		header->sequence = ++pchan->sequence_tx;
		header->signature = HAB_HEAD_SIGNATURE;

		outbuf = hd->buf;
		hd->payload_size = sizebytes;

		memcpy(outbuf, header, sizeof(*header));
		memcpy(&outbuf[sizeof(*header)], payload, sizebytes);

		sg_init_one(sgout, outbuf, sizeof(*header) + sizebytes);

		rc = virtqueue_add_outbuf(vpc->vq[HAB_PCHAN_TX_VQ], sgout, 1,
							hd, GFP_ATOMIC);
		if (!rc) {
			trace_hab_pchan_send_done(pchan);
			rc = virtqueue_kick(vpc->vq[HAB_PCHAN_TX_VQ]);
			if (!rc)
				pr_err("failed to kick outbuf to PVM %d\n", rc);
		} else
			pr_err("failed to add outbuf %d %zd bytes\n",
				rc, sizeof(*header) + sizebytes);
	} else {
		pr_err("%s pchan not ready\n", pchan->name);
		rc = -ENODEV;
	}
	spin_unlock_irqrestore(&vpc->lock[HAB_PCHAN_TX_VQ], lock_flags);

	return 0;
}

/* this read is called by hab-msg-recv from physical_channel_rx_dispatch or cb */
int physical_channel_read(struct physical_channel *pchan,
						void *payload,
						size_t read_size)
{
	struct virtio_pchan_link *link =
			(struct virtio_pchan_link *)pchan->hyp_data;
	struct vq_pchan *vpc = link->vpc;

	if (link->vpc == NULL) {
		pr_info("%s: %s link->vpc not ready\n", __func__, pchan->name);
		return -ENODEV;
	}

	if (!payload || !vpc->read_data) {
		pr_err("%s invalid parameters %pK %pK offset %d read %zd %s dev %pK\n",
			pchan->name, payload, vpc->read_data, vpc->read_offset,
			read_size, pchan->name, vpc);
		return 0;
	}

	/* size in header is only for payload excluding the header itself */
	if (vpc->read_size < read_size + sizeof(struct hab_header) +
		vpc->read_offset) {
		pr_warn("%s read %zd is less than requested %zd header %zd offset %d\n",
			pchan->name, vpc->read_size, read_size,
			sizeof(struct hab_header), vpc->read_offset);
		read_size = vpc->read_size - vpc->read_offset -
					sizeof(struct hab_header);
	}

	/* always skip the header */
	memcpy(payload, (unsigned char *)vpc->read_data +
		sizeof(struct hab_header) + vpc->read_offset, read_size);
	vpc->read_offset += (int)read_size;

	return (int)read_size;
}

/* called by hab recv() to act like ISR to poll msg from remote VM */
/* ToDo: need to change the callback to here */
void physical_channel_rx_dispatch(unsigned long data)
{
	struct physical_channel *pchan = (struct physical_channel *)data;
	struct virtio_pchan_link *link =
				(struct virtio_pchan_link *)pchan->hyp_data;
	struct vq_pchan *vpc = link->vpc;

	if (link->vpc == NULL) {
		pr_info("%s: %s link->vpc not ready\n", __func__, pchan->name);
		return;
	}

	virthab_recv_rxq_task(vpc->vq[HAB_PCHAN_RX_VQ]);
}

/* pchan is directly added into the hab_device */
static int habvirtio_pchan_create(struct hab_device *dev, char *pchan_name)
{
	int result = 0;
	struct physical_channel *pchan = NULL;
	struct virtio_pchan_link *link = NULL;

	pchan = hab_pchan_alloc(dev, LOOPBACK_DOM);
	if (!pchan) {
		result = -ENOMEM;
		goto err;
	}

	pchan->closed = 0;
	strscpy(pchan->name, pchan_name, sizeof(pchan->name));

	link = kmalloc(sizeof(*link), GFP_KERNEL);
	if (!link) {
		result = -ENOMEM;
		goto err;
	}

	link->pchan = pchan;
	link->mmid = dev->id;
	pchan->hyp_data = link;

	link->vpc = NULL;
	link->vhab = NULL;

	/* create PCHAN first then wait for virtq later during probe */
	pr_debug("virtio device has NOT been initialized yet. %s has to wait for probe\n",
			pchan->name);

	return 0;
err:
	kfree(pchan);

	return result;
}

int habhyp_commdev_alloc(void **commdev, int is_be, char *name, int vmid_remote,
				struct hab_device *mmid_device)
{
	struct physical_channel *pchan;
	int ret = habvirtio_pchan_create(mmid_device, name);

	if (ret) {
		pr_err("failed to create %s pchan in mmid device %s, ret %d, pchan cnt %d\n",
			name, mmid_device->name, ret, mmid_device->pchan_cnt);
		*commdev = NULL;
		return ret;
	}

	pr_debug("create virtio pchan on %s return %d, loopback mode(%d), total pchan %d\n",
		name, ret, hab_driver.b_loopback, mmid_device->pchan_cnt);
	pchan = hab_pchan_find_domid(mmid_device, HABCFG_VMID_DONT_CARE);
	/* in this implementation, commdev is same as pchan */
	*commdev = pchan;

	return ret;
}

int habhyp_commdev_dealloc(void *commdev)
{
	struct virtio_pchan_link *link = commdev;
	struct physical_channel *pchan = link->pchan;

	pr_info("free commdev %s\n", pchan->name);
	link->pchan = NULL;
	kfree(link);
	hab_pchan_put(pchan);

	return 0;
}

int hab_stat_log(struct physical_channel **pchans, int pchan_cnt, char *dest,
			int dest_size)
{
	struct virtio_pchan_link *link;
	struct vq_pchan *vpc;
	int i, ret = 0;
	bool tx_pending, rx_pending;
	void *tx_buf, *rx_buf;
	unsigned int tx_len = 0, rx_len = 0;

	for (i = 0; i < pchan_cnt; i++) {
		link = (struct virtio_pchan_link *)pchans[i]->hyp_data;
		vpc = link->vpc;
		if (!vpc) {
			pr_err("%s: %s vpc not ready\n", __func__, pchans[i]->name);
			continue;
		}

		tx_pending = !virtqueue_enable_cb(vpc->vq[HAB_PCHAN_TX_VQ]);
		rx_pending = !virtqueue_enable_cb(vpc->vq[HAB_PCHAN_RX_VQ]);
		tx_buf = virtqueue_get_buf(vpc->vq[HAB_PCHAN_TX_VQ], &tx_len);
		rx_buf = virtqueue_get_buf(vpc->vq[HAB_PCHAN_RX_VQ], &rx_len);

		pr_info("pchan %d tx cnt %d %d %d rx %d txpend %d rxpend %d txlen %d rxlen %d\n",
			i, vpc->s_cnt, vpc->m_cnt, vpc->l_cnt, vpc->in_cnt, tx_pending, rx_pending,
			tx_len, rx_len);
		ret = hab_stat_buffer_print(dest, dest_size,
			"tx cnt %d %d %d rx %d txpend %d rxpend %d txlen %d rxlen %d\n",
			vpc->s_cnt, vpc->m_cnt, vpc->l_cnt, vpc->in_cnt, tx_pending, rx_pending,
			tx_len, rx_len);
		if (ret)
			break;
	}

	return ret;
}

int hab_hypervisor_register_post(void)
{
	/* one virtio device */
	register_virtio_driver(&virtio_hab_driver);
	return 0;
}
