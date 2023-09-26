// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/compat.h>
#include <linux/eventfd.h>
#include <linux/file.h>
#include <linux/miscdevice.h>
#include <linux/mmu_context.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/vhost.h>
#include <linux/workqueue.h>

#include "hab.h"
#include "vhost.h"

/* Max number of bytes transferred before requeueing the job.
 * Using this limit prevents one virtqueue from starving others.
 */
#define VHOST_HAB_WEIGHT 0x80000

/* Max number of packets transferred before requeueing the job.
 * Using this limit prevents one virtqueue from starving others with
 * pkts.
 */
#define VHOST_HAB_PKT_WEIGHT 256

enum {
	VHOST_HAB_PCHAN_TX_VQ = 0, /* receive data from gvm */
	VHOST_HAB_PCHAN_RX_VQ, /* send data to gvm */
	VHOST_HAB_PCHAN_VQ_MAX,
};

struct vhost_hab_pchannel { /* per pchan */
	struct physical_channel *pchan; /* hab physical channel */
	struct hab_device *habdev; /* hab device for the mmid */
	struct list_head node;

	struct vhost_virtqueue vqs[VHOST_HAB_PCHAN_VQ_MAX]; /* vqs for pchan */
	struct iov_iter out_iter; /* iter to read data */
	struct vhost_work tx_recv_work;

	struct list_head send_list; /* list of node to be sent to rxq */
	struct mutex send_list_mutex; /* protect send_list */
	struct vhost_work rx_send_work;

	int tx_empty; /* cached value for out of context access */
	int rx_empty; /* ditto */
};

struct vhost_hab_send_node {
	struct list_head node;
	struct hab_header header;
	u8 payload[];
} __packed;

struct vhost_hab_dev { /* per user requested domain */
	struct vhost_dev dev; /* vhost base device */
	struct vhost_hab_cdev *vh_cdev;
	int started;
	struct list_head vh_pchan_list; /* pchannels on this vhost device */
};

struct vhost_hab_cdev { /* per domain, per gvm */
	struct device *dev;
	dev_t dev_no;
	struct cdev cdev;
	uint32_t domain_id;
	struct hab_device *habdevs[HABCFG_MMID_NUM];
};

struct vhost_hab_stat_work {
	struct work_struct work;
	struct physical_channel **pchans;
	int pchan_count;
};

struct vhost_hab { /* global */
	dev_t major;
	struct class *class;
	uint32_t num_cdevs; /* total number of cdevs created */
	/*
	 * all vhost hab char devices on the system.
	 * mmid area starts from 1. Slot 0 is for
	 * vhost-hab which controls all the pchannels.
	 */
	struct vhost_hab_cdev *vh_cdevs[HABCFG_MMID_AREA_MAX + 1];
	struct list_head vh_pchan_list; /* avalible pchannels on the system */
	struct mutex pchan_mutex;
	struct workqueue_struct *wq;
};

static struct vhost_hab g_vh;

#define HAB_AREA_NAME_MAX 32
static char hab_area_names[HABCFG_MMID_AREA_MAX + 1][HAB_AREA_NAME_MAX] = {
	[HAB_MMID_ALL_AREA] = "hab",
	[MM_AUD_START / 100] = "aud",
	[MM_CAM_START / 100] = "cam",
	[MM_DISP_START / 100] = "disp",
	[MM_GFX_START / 100] = "ogles",
	[MM_VID_START / 100] = "vid",
	[MM_MISC_START / 100] = "misc",
	[MM_QCPE_START / 100] = "qcpe",
	[MM_CLK_START / 100] = "clock",
	[MM_FDE_START / 100] = "fde",
	[MM_BUFFERQ_START / 100] = "bufferq",
	[MM_DATA_START / 100] = "network",
	[MM_HSI2S_START / 100] = "hsi2s",
	[MM_XVM_START / 100] = "xvm"
};

static int rx_worker(struct vhost_hab_pchannel *vh_pchan);

static void stat_worker(struct work_struct *work);

static void do_rx_send_work(struct vhost_work *work)
{
	struct vhost_hab_pchannel *vh_pchan = container_of(work,
				struct vhost_hab_pchannel, rx_send_work);

	rx_worker(vh_pchan);
}

static int rx_send_list_empty(struct vhost_hab_pchannel *vh_pchan)
{
	int ret;

	mutex_lock(&vh_pchan->send_list_mutex);
	ret = list_empty(&vh_pchan->send_list);
	mutex_unlock(&vh_pchan->send_list_mutex);

	return ret;
}

static void tx_worker(struct vhost_hab_pchannel *vh_pchan)
{
	struct vhost_virtqueue *vq = vh_pchan->vqs + VHOST_HAB_PCHAN_TX_VQ;
	struct vhost_hab_dev *vh_dev = container_of(vq->dev,
						struct vhost_hab_dev, dev);
	unsigned int out_num = 0, in_num = 0;
	int head, ret;
	size_t out_len, in_len, total_len = 0;
	ssize_t copy_size;
	struct hab_header header;

	mutex_lock(&vq->mutex);
	if (!vq->private_data) {
		mutex_unlock(&vq->mutex);
		return;
	}

	vhost_disable_notify(&vh_dev->dev, vq);

	while (1) {
		head = vhost_get_vq_desc(vq, vq->iov, ARRAY_SIZE(vq->iov),
					 &out_num, &in_num, NULL, NULL);

		if (head == vq->num) {
			if (unlikely(vhost_enable_notify(&vh_dev->dev, vq))) {
				vhost_disable_notify(&vh_dev->dev, vq);
				continue;
			}
			break; /* no more tx buf wait for next round */
		} else if (unlikely(head < 0)) {
			pr_err("%s error head %d out %d in %d\n",
				vh_pchan->pchan->name, head, out_num, in_num);
			break;
		}

		out_len = iov_length(vq->iov, out_num);

		if ((out_num > 0) && (out_len > 0)) {
			iov_iter_init(&vh_pchan->out_iter, WRITE, vq->iov,
						out_num, out_len);
			copy_size = copy_from_iter(&header, sizeof(header),
						&vh_pchan->out_iter);
			if (unlikely(copy_size != sizeof(header)))
				pr_err("fault on copy_from_iter, out_len %lu, ret %lu\n",
					out_len, copy_size);

			ret = hab_msg_recv(vh_pchan->pchan, &header);
			if (ret)
				pr_err("hab_msg_recv error %d\n", ret);

			total_len += out_len;

			if (vh_pchan->pchan->sequence_rx + 1 != header.sequence)
				pr_err("%s: expected sequence_rx is %u, received is %u\n",
						vh_pchan->pchan->name,
						vh_pchan->pchan->sequence_rx,
						header.sequence);
			vh_pchan->pchan->sequence_rx = header.sequence;
		}

		if (in_num) {
			in_len = iov_length(&vq->iov[out_num], in_num);
			total_len += in_len;
			pr_warn("unexpected in buf in tx vq, in_num %d, in_len %lu\n",
				in_num, in_len);
		}

		vhost_add_used_and_signal(&vh_dev->dev, vq, head, 0);
		if (unlikely(vhost_exceeds_weight(vq, 0, total_len))) {
			pr_err("total_len %lu > hab vq weight %d\n",
					total_len, VHOST_HAB_WEIGHT);
			break;
		}
	}

	mutex_unlock(&vq->mutex);
}

static void do_tx_recv_work(struct vhost_work *work)
{
	struct vhost_hab_pchannel *vh_pchan = container_of(work,
				struct vhost_hab_pchannel, tx_recv_work);

	tx_worker(vh_pchan);
}

static void handle_tx_vq_kick(struct vhost_work *work)
{
	struct vhost_virtqueue *vq = container_of(work, struct vhost_virtqueue,
						  poll.work);
	struct vhost_hab_pchannel *vh_pchan = container_of(vq,
			struct vhost_hab_pchannel, vqs[VHOST_HAB_PCHAN_TX_VQ]);

	tx_worker(vh_pchan);
}

static void handle_rx_vq_kick(struct vhost_work *work)
{
	struct vhost_virtqueue *vq = container_of(work, struct vhost_virtqueue,
						  poll.work);
	struct vhost_hab_pchannel *vh_pchan = container_of(vq,
			struct vhost_hab_pchannel, vqs[VHOST_HAB_PCHAN_RX_VQ]);

	rx_worker(vh_pchan);
}

static int vhost_hab_open(struct inode *inode, struct file *f)
{
	struct vhost_hab_cdev *vh_cdev = container_of(inode->i_cdev,
						struct vhost_hab_cdev, cdev);
	struct vhost_hab_dev *vh_dev;
	struct vhost_hab_pchannel *vh_pchan, *vh_pchan_t;
	struct vhost_virtqueue **vqs;
	struct hab_device *habdev;
	int num_pchan = 0;
	bool vh_pchan_found;
	int i, j = 0;
	int ret;

	vh_dev = kmalloc(sizeof(*vh_dev), GFP_KERNEL);
	if (!vh_dev)
		return -ENOMEM;

	INIT_LIST_HEAD(&vh_dev->vh_pchan_list);
	mutex_lock(&g_vh.pchan_mutex);
	for (i = 0; i < HABCFG_MMID_NUM; i++) {
		habdev = vh_cdev->habdevs[i];
		if (habdev == NULL)
			break;

		pr_info("%s: i=%d, mmid=%d\n", __func__, i, habdev->id);
		vh_pchan_found = false;
		list_for_each_entry_safe(vh_pchan, vh_pchan_t,
					&g_vh.vh_pchan_list, node) {

			pr_debug("%s: vh-pchan id %d\n", __func__,
				vh_pchan->habdev->id);

			if (vh_pchan->habdev == habdev) {
				pr_debug("%s: find vh_pchan for mmid %d\n",
					__func__, habdev->id);
				list_move_tail(&vh_pchan->node,
						&vh_dev->vh_pchan_list);
				vh_pchan_found = true;
				pr_debug("%s: num_pchan %d\n", __func__,
					num_pchan);
				num_pchan++;
				break;
			}
		}

		if (!vh_pchan_found) {
			pr_err("no vh_pchan is available for mmid %d\n",
				habdev->id);
			goto err;
		}
	}

	vqs = kmalloc_array(num_pchan * VHOST_HAB_PCHAN_VQ_MAX, sizeof(*vqs),
								GFP_KERNEL);
	if (!vqs) {
		ret = -ENOMEM;
		goto err;
	}

	pr_info("num_pchan=%d\n", num_pchan);

	list_for_each_entry(vh_pchan, &vh_dev->vh_pchan_list, node) {
		vqs[j++] = &vh_pchan->vqs[VHOST_HAB_PCHAN_TX_VQ];
		vqs[j++] = &vh_pchan->vqs[VHOST_HAB_PCHAN_RX_VQ];
	}

	vhost_dev_init(&vh_dev->dev, vqs, VHOST_HAB_PCHAN_VQ_MAX * num_pchan,
			UIO_MAXIOV, VHOST_HAB_PKT_WEIGHT, VHOST_HAB_WEIGHT);

	list_for_each_entry(vh_pchan, &vh_dev->vh_pchan_list, node) {
		vhost_work_init(&vh_pchan->rx_send_work, do_rx_send_work);
		vhost_work_init(&vh_pchan->tx_recv_work, do_tx_recv_work);
	}

	vh_dev->vh_cdev = vh_cdev;
	f->private_data = vh_dev;

	mutex_unlock(&g_vh.pchan_mutex);
	return 0;

err:
	/* return vh_pchans back to system */
	list_for_each_entry_safe(vh_pchan, vh_pchan_t,
				&vh_dev->vh_pchan_list, node)
		list_move_tail(&vh_pchan->node, &g_vh.vh_pchan_list);

	kfree(vh_dev);

	mutex_unlock(&g_vh.pchan_mutex);
	return ret;
}

static void *vhost_hab_stop_vq(struct vhost_hab_dev *vh_dev,
				struct vhost_virtqueue *vq)
{
	struct vhost_hab_pchannel *vh_pchan = vq->private_data;

	mutex_lock(&vq->mutex);
	vq->private_data = NULL;
	mutex_unlock(&vq->mutex);
	return (void *)vh_pchan;
}

static void vhost_hab_stop(struct vhost_hab_dev *vh_dev)
{
	struct vhost_hab_pchannel *vh_pchan;

	list_for_each_entry(vh_pchan, &vh_dev->vh_pchan_list, node) {
		vhost_hab_stop_vq(vh_dev,
				vh_pchan->vqs + VHOST_HAB_PCHAN_TX_VQ);
		vhost_hab_stop_vq(vh_dev,
				vh_pchan->vqs + VHOST_HAB_PCHAN_RX_VQ);
	}
	vh_dev->started = 0;
}

static void vhost_hab_flush_vq(struct vhost_hab_dev *vh_dev,
				struct vhost_virtqueue *vq)
{
	vhost_poll_flush(&vq->poll);
}

static void vhost_hab_flush(struct vhost_hab_dev *vh_dev)
{
	struct vhost_hab_pchannel *vh_pchan;

	list_for_each_entry(vh_pchan, &vh_dev->vh_pchan_list, node) {
		vhost_hab_flush_vq(vh_dev,
				vh_pchan->vqs + VHOST_HAB_PCHAN_TX_VQ);
		vhost_hab_flush_vq(vh_dev,
				vh_pchan->vqs + VHOST_HAB_PCHAN_RX_VQ);
		vhost_work_flush(&vh_dev->dev, &vh_pchan->rx_send_work);
		vhost_work_flush(&vh_dev->dev, &vh_pchan->tx_recv_work);
	}
}

static int vhost_hab_release(struct inode *inode, struct file *f)
{
	struct vhost_hab_dev *vh_dev = f->private_data;
	struct vhost_hab_pchannel *vh_pchan, *vh_pchan_t;

	vhost_hab_stop(vh_dev);
	vhost_hab_flush(vh_dev);
	vhost_dev_stop(&vh_dev->dev);
	vhost_dev_cleanup(&vh_dev->dev);

	/* We do an extra flush before freeing memory,
	 * since jobs can re-queue themselves.
	 */
	vhost_hab_flush(vh_dev);
	kfree(vh_dev->dev.vqs);

	/* return pchannel back to the system */
	mutex_lock(&g_vh.pchan_mutex);
	list_for_each_entry_safe(vh_pchan, vh_pchan_t,
				&vh_dev->vh_pchan_list, node) {
		if (vh_pchan->pchan) {
			vh_pchan->pchan->hyp_data = NULL;
			hab_pchan_put(vh_pchan->pchan);
			vh_pchan->pchan = NULL;
		}
		list_move_tail(&vh_pchan->node, &g_vh.vh_pchan_list);
	}
	mutex_unlock(&g_vh.pchan_mutex);

	kfree(vh_dev);

	return 0;
}

static long vhost_hab_ready_check(struct vhost_hab_dev *vh_dev)
{
	struct vhost_virtqueue *vq;
	int r, index;

	mutex_lock(&vh_dev->dev.mutex);
	r = vhost_dev_check_owner(&vh_dev->dev);
	if (r)
		goto err;

	for (index = 0; index < vh_dev->dev.nvqs; ++index) {
		vq = vh_dev->dev.vqs[index];
		/* Verify that ring has been setup correctly. */
		if (!vhost_vq_access_ok(vq)) {
			r = -EFAULT;
			goto err;
		}

		if (vq->kick == NULL) {
			r = -EFAULT;
			goto err;
		}

		if (vq->call_ctx == NULL) {
			r = -EFAULT;
			goto err;
		}
	}

	mutex_unlock(&vh_dev->dev.mutex);
	return 0;

err:
	mutex_unlock(&vh_dev->dev.mutex);
	return r;
}

static long vhost_hab_run(struct vhost_hab_dev *vh_dev, int start)
{
	struct vhost_virtqueue *vq;
	struct vhost_hab_pchannel *vh_pchan;
	int r = 0, i, ret = 0, not_started = 0;

	pr_info("vh_dev start %d\n", start);
	if (start < 0 || start > 1)
		return -EINVAL;

	mutex_lock(&vh_dev->dev.mutex);

	if (vh_dev->started == start) {
		pr_info("already started\n");
		goto exit;
	}

	r = vhost_dev_check_owner(&vh_dev->dev);
	if (r)
		goto exit;

	for (i = 0; i < vh_dev->dev.nvqs; ++i) {
		vq = vh_dev->dev.vqs[i];

		/* Verify that ring has been setup correctly. */
		if (!vhost_vq_access_ok(vq)) {
			r = -EFAULT;
			goto exit;
		}
	}

	/* try to start all the pchan and its vq */
	list_for_each_entry(vh_pchan, &vh_dev->vh_pchan_list, node) {
		for (i = 0; i < VHOST_HAB_PCHAN_VQ_MAX; i++) {
			vq = vh_pchan->vqs + i;

			if (vq->private_data)
				continue; /* already started */

			mutex_lock(&vq->mutex);
			vq->private_data = vh_pchan;
			r = vhost_vq_init_access(vq); /* poll need retry */
			if (r) {
				vq->private_data = NULL;
				not_started += 1;
				pr_warn("%s vq %d not ready %d total %d\n",
					vh_pchan->pchan->name, i, ret,
					not_started);
				mutex_unlock(&vq->mutex);
				continue; /* still not ready, try next vq */
			}
			mutex_unlock(&vq->mutex);
		} /* vq */
	} /* pchan */

	if (not_started == 0) {
		vh_dev->started = 1; /* ready when all vq is ready */
		mutex_unlock(&vh_dev->dev.mutex);
		/* Once vhost device starts successfully, trigger a kick */
		for (i = 0; i < vh_dev->dev.nvqs; ++i) {
			vq = vh_dev->dev.vqs[i];
			vhost_poll_queue(&vq->poll);
		}

		pr_info("%s exit start %d r %d ret %d not_started %d\n",
			__func__, start, r, ret, not_started);
		return 0;
	}
exit:
	mutex_unlock(&vh_dev->dev.mutex);
	pr_info("%s exit start %d failure r %d ret %d not_started %d\n",
		__func__, start, r, ret, not_started);
	return r;
}

static long vhost_hab_reset_owner(struct vhost_hab_dev *vh_dev)
{
	long err;
	struct vhost_umem *umem;

	mutex_lock(&vh_dev->dev.mutex);
	err = vhost_dev_check_owner(&vh_dev->dev);
	if (err)
		goto done;
	umem = vhost_dev_reset_owner_prepare();
	if (!umem) {
		err = -ENOMEM;
		goto done;
	}
	vhost_hab_stop(vh_dev);
	vhost_hab_flush(vh_dev);
	vhost_dev_stop(&vh_dev->dev);
	vhost_dev_reset_owner(&vh_dev->dev, umem);
done:
	mutex_unlock(&vh_dev->dev.mutex);
	return err;
}

static int vhost_hab_set_features(struct vhost_hab_dev *vh_dev, u64 features)
{
	struct vhost_virtqueue *vq;
	int i;

	mutex_lock(&vh_dev->dev.mutex);
	if ((features & (1 << VHOST_F_LOG_ALL)) &&
	    !vhost_log_access_ok(&vh_dev->dev)) {
		mutex_unlock(&vh_dev->dev.mutex);
		return -EFAULT;
	}

	for (i = 0; i < vh_dev->dev.nvqs; ++i) {
		vq = vh_dev->dev.vqs[i];
		mutex_lock(&vq->mutex);
		vq->acked_features = features;
		mutex_unlock(&vq->mutex);
	}

	mutex_unlock(&vh_dev->dev.mutex);
	return 0;
}

static int vhost_hab_set_pchannels(struct vhost_hab_dev *vh_dev, int vmid)
{
	struct vhost_hab_pchannel *vh_pchan;
	struct physical_channel *pchan;
	int ret = 0;

	mutex_lock(&g_vh.pchan_mutex);
	list_for_each_entry(vh_pchan, &vh_dev->vh_pchan_list, node) {
		pchan = hab_pchan_find_domid(vh_pchan->habdev, vmid);
		if (!pchan || pchan->hyp_data) {
			pr_err("failed to find pchan for mmid %d, vmid %d\n",
				vh_pchan->habdev->id, vmid);
			goto err;
		}

		vh_pchan->pchan = pchan;
		pchan->hyp_data = vh_pchan;
	}

	mutex_unlock(&g_vh.pchan_mutex);
	return ret;

err:
	list_for_each_entry_continue_reverse(vh_pchan, &vh_dev->vh_pchan_list,
						node) {
		vh_pchan->pchan->hyp_data = NULL;
		hab_pchan_put(vh_pchan->pchan);
		vh_pchan->pchan = NULL;
	}
	mutex_unlock(&g_vh.pchan_mutex);
	return -ENODEV;
}

static int vhost_hab_set_config(struct vhost_hab_dev *vh_dev,
				struct vhost_config *cfg)
{
	struct vhost_hab_config hab_cfg;
	size_t vm_name_size = sizeof(hab_cfg.vm_name);
	size_t cfg_size = min_t(size_t, (size_t)cfg->size, sizeof(hab_cfg));
	char *s, *t;
	int vmid;
	int ret;

	if (copy_from_user(&hab_cfg, cfg->data + cfg->offset, cfg_size))
		return -EFAULT;

	hab_cfg.vm_name[vm_name_size - 1] = '\0';

	pr_info("%s: vm_name %s\n", __func__, hab_cfg.vm_name);
	s = strnstr(hab_cfg.vm_name, "vm", vm_name_size);
	if (!s) {
		pr_err("vmid is not found in vm_name\n");
		return -EINVAL;
	}

	s += 2; /* skip 'vm' */
	if (s >= (hab_cfg.vm_name + vm_name_size)) {
		pr_err("id is not found after 'vm' in vm_name\n");
		return -EINVAL;
	}

	/* terminate string at '-' after 'vm' */
	t = strchrnul(s, '-');
	*t = '\0';

	ret = kstrtoint(s, 10, &vmid);
	if (ret < 0) {
		pr_err("failed to parse vmid from %s, %d\n", s, ret);
		return ret;
	}

	pr_debug("vmid=%d\n", vmid);
	return vhost_hab_set_pchannels(vh_dev, vmid);
}

static long vhost_hab_ioctl(struct file *f, unsigned int ioctl,
				unsigned long arg)
{
	struct vhost_hab_dev *vh_dev = f->private_data;
	void __user *argp = (void __user *)arg;
	u64 features;
	struct vhost_config config;
	int r = 0;

	switch (ioctl) {
	case VHOST_RESET_OWNER:
		r = vhost_hab_reset_owner(vh_dev);
		break;
	case VHOST_SET_FEATURES:
		if (copy_from_user(&features, argp, sizeof(features))) {
			r = -EFAULT;
			break;
		}
		if (features & ~VHOST_FEATURES) {
			r = -EOPNOTSUPP;
			break;
		}
		r = vhost_hab_set_features(vh_dev, features);
		break;
	case VHOST_SET_CONFIG:
		if (copy_from_user(&config, argp, sizeof(config))) {
			r = -EFAULT;
			break;
		}
		r = vhost_hab_set_config(vh_dev, &config);
		break;
	case VHOST_GET_FEATURES:
		features = VHOST_FEATURES;
		if (copy_to_user(argp, &features, sizeof(features))) {
			r = -EFAULT;
			break;
		}
		r = 0;
		break;
	default:
	{
		mutex_lock(&vh_dev->dev.mutex);
		r = vhost_dev_ioctl(&vh_dev->dev, ioctl, argp);
		if (r == -ENOIOCTLCMD)
			r = vhost_vring_ioctl(&vh_dev->dev, ioctl, argp);

		vhost_hab_flush(vh_dev);
		mutex_unlock(&vh_dev->dev.mutex);
		if (vhost_hab_ready_check(vh_dev) == 0)
			vhost_hab_run(vh_dev, 1);

		break;
	}
	}

	return r;
}

#ifdef CONFIG_COMPAT
static long vhost_hab_compat_ioctl(struct file *f, unsigned int ioctl,
				unsigned long arg)
{
	return vhost_hab_ioctl(f, ioctl, (unsigned long)compat_ptr(arg));
}
#endif

int hab_hypervisor_register(void)
{
	uint32_t max_devices = HABCFG_MMID_AREA_MAX + 1;
	dev_t dev_no;
	int ret;

	ret = alloc_chrdev_region(&dev_no, 0, max_devices, "vhost-msm");
	if (ret < 0) {
		pr_err("alloc_chrdev_region failed: %d\n", ret);
		return ret;
	}
	g_vh.major = MAJOR(dev_no);

	pr_info("g_vh.major %d\n", g_vh.major);
	g_vh.class = class_create(THIS_MODULE, "vhost-msm");
	if (IS_ERR_OR_NULL(g_vh.class)) {
		pr_err("class_create failed\n");
		unregister_chrdev_region(g_vh.major, max_devices);
		return g_vh.class ? PTR_ERR(g_vh.class) : -ENOMEM;
	}

	g_vh.wq = create_singlethread_workqueue("hab_vhost_wq");
	if (!g_vh.wq) {
		pr_err("create workqueue failed\n");
		class_destroy(g_vh.class);
		unregister_chrdev_region(g_vh.major, max_devices);
		return -EINVAL;
	}

	mutex_init(&g_vh.pchan_mutex);
	INIT_LIST_HEAD(&g_vh.vh_pchan_list);
	return 0;
}

void hab_hypervisor_unregister(void)
{
	uint32_t max_devices = HABCFG_MMID_AREA_MAX + 1;
	struct vhost_hab_pchannel *n, *vh_pchan;

	list_for_each_entry_safe(vh_pchan, n, &g_vh.vh_pchan_list, node) {
		/*
		 * workaround: force hyp_date to NULL to prevent vh_pchan from
		 * being freed again during hab_pchan_free.
		 * ideally hab_pchan_free should not free hyp_data because it
		 * is not allocated by hab_pchan_alloc.
		 */
		if (vh_pchan->pchan)
			vh_pchan->pchan->hyp_data = NULL;

		list_del(&vh_pchan->node);
		mutex_destroy(&vh_pchan->send_list_mutex);
		kfree(vh_pchan);
	}

	hab_hypervisor_unregister_common();

	mutex_destroy(&g_vh.pchan_mutex);

	flush_workqueue(g_vh.wq);
	destroy_workqueue(g_vh.wq);

	class_destroy(g_vh.class);
	unregister_chrdev_region(g_vh.major, max_devices);
}

int hab_hypervisor_register_post(void) { return 0; }

void hab_pipe_read_dump(struct physical_channel *pchan) {};

void dump_hab_wq(struct physical_channel *pchan) {};

/* caller must hold vq->mutex */
static int get_rx_buf_locked(struct vhost_dev *dev,
			struct vhost_hab_pchannel *vh_pchan,
			struct iov_iter *in_iter, size_t *in_len,
			size_t *out_len, int *head)
{
	struct vhost_virtqueue *vq = &vh_pchan->vqs[VHOST_HAB_PCHAN_RX_VQ];
	unsigned int out_num = 0, in_num = 0;
	int ret = 0;

	while (1) {
		*head = vhost_get_vq_desc(vq, vq->iov, ARRAY_SIZE(vq->iov),
					 &out_num, &in_num, NULL, NULL);

		if (*head < 0) {
			pr_err("failed to get correct head %d\n", *head);
			ret = -EIO;
			break;
		}

		if (*head == vq->num) {
			pr_debug("rx buf %s underrun, vq avail %d\n",
				vh_pchan->pchan->name,
				vhost_vq_avail_empty(dev, vq));

			if (unlikely(vhost_enable_notify(dev, vq))) {
				pr_debug("%s enable notify return true, %d\n",
					vh_pchan->pchan->name,
					vhost_vq_avail_empty(dev, vq));
				vhost_disable_notify(dev, vq);
				continue; /* retry */
			}
			ret = -EAGAIN;
			break; /* no more buff wait for next round */
		}

		if (unlikely(out_num)) {
			*out_len = iov_length(vq->iov, out_num);
			pr_warn("unexpected outbuf in rxvq, num %d, len %lu\n",
					out_num, *out_len);
		}

		if (in_num) {
			*in_len = iov_length(&vq->iov[out_num], in_num);
			if (*in_len > 0)
				iov_iter_init(in_iter, READ, &vq->iov[out_num],
						in_num, *in_len);
			else {
				pr_err("out of in-len in iov %d in-num %d out-num %d\n",
					*in_len, in_num, out_num);
				ret = -EIO;
			}
		} else {
			pr_err("no in buf in this slot\n");
			ret = -EBUSY;
		}
		break;
	}

	return ret;
}

static ssize_t fill_rx_buf(void **pbuf, size_t *remain_size,
			struct iov_iter *in_iter, size_t in_len,
			size_t *size_filled)
{
	ssize_t copy_size, copy_size_ret;

	if (unlikely(in_len < *remain_size))
		copy_size = in_len;
	else
		copy_size = *remain_size;

	copy_size_ret = copy_to_iter(*pbuf, copy_size, in_iter);
	if (unlikely(copy_size_ret != copy_size)) {
		pr_err("fault on copy_to_iter, copy_size %lu, ret %lu\n",
			copy_size, copy_size_ret);
		return -EFAULT;
	}

	*remain_size -= copy_size;
	*size_filled += copy_size;
	*pbuf += copy_size;

	return 0;
}

/* caller must hold vq->mutex */
static int rx_send_one_node_locked(struct vhost_dev *dev,
				   struct vhost_hab_pchannel *vh_pchan,
				   struct vhost_virtqueue *vq,
				   struct vhost_hab_send_node *send_node,
				   int *added)
{
	int head;
	struct hab_header *header = &send_node->header;
	void *data = header;
	size_t remain_size = sizeof(*header) + HAB_HEADER_GET_SIZE(*header);
	size_t out_len, in_len, total_len = 0;

	size_t size_filled;
	struct iov_iter in_iter;
	int ret = 0;

	while (remain_size > 0) {
		out_len = 0;
		in_len = 0;

		ret = get_rx_buf_locked(dev, vh_pchan, &in_iter, &in_len,
					&out_len, &head);
		if (ret) {
			if (ret != -EAGAIN)
				pr_info("%s failed to get one rx-buf ret %d\n",
					vh_pchan->pchan->name, ret);
			break;
		}

		total_len += in_len + out_len;
		size_filled = 0;

		if (in_len) {
			if (HAB_HEADER_GET_TYPE(send_node->header) ==
				HAB_PAYLOAD_TYPE_PROFILE) {
				struct habmm_xing_vm_stat *pstat =
					(struct habmm_xing_vm_stat *)
					(send_node->payload);
				struct timespec64 ts = {0};

				ktime_get_ts64(&ts);
				pstat->tx_sec = ts.tv_sec;
				pstat->tx_usec = ts.tv_nsec/NSEC_PER_USEC;
			}

			header->sequence = ++vh_pchan->pchan->sequence_tx;
			header->signature = HAB_HEAD_SIGNATURE;

			ret = fill_rx_buf((void **)(&data),
					&remain_size,
					&in_iter, in_len, &size_filled);
			if (ret)
				break;

			ret = vhost_add_used(vq, head, size_filled);
			if (ret) {
				pr_err("%s failed to add used ret %d head %d size %d\n",
					vh_pchan->pchan->name, ret, head, size_filled);
				break;
			}
			*added += 1; /* continue for the remaining */
		} else {
			pr_err("%s rx-buf empty ret %d inlen %d outlen %d head %d\n",
				vh_pchan->pchan->name, ret, in_len, out_len, head);
			ret = -EPIPE;
			break;
		}

		if (unlikely(vhost_exceeds_weight(vq, 0, total_len))) {
			pr_err("total_len %lu > hab vq weight %d\n",
					total_len, VHOST_HAB_WEIGHT);
			ret = -EINVAL;
			break;
		}
	}

	return ret;
}

static int rx_worker(struct vhost_hab_pchannel *vh_pchan)
{
	struct vhost_hab_send_node *send_node;
	struct vhost_virtqueue *vq = &vh_pchan->vqs[VHOST_HAB_PCHAN_RX_VQ];
	struct vhost_dev *dev = vq->dev;
	int ret = 0, has_send = 1, added = 0;

	mutex_lock(&vq->mutex);

	vh_pchan = vq->private_data;
	if (!vh_pchan) {
		pr_err("rx vq is not ready yet\n");
		goto err_unlock;
	}

	vhost_disable_notify(dev, vq); /* no notify by default */

	while (has_send) {
		mutex_lock(&vh_pchan->send_list_mutex);
		send_node = list_first_entry_or_null(&vh_pchan->send_list,
					struct vhost_hab_send_node, node);
		mutex_unlock(&vh_pchan->send_list_mutex);

		if (!send_node) {
			has_send = 0; /* completed send list wait for more */
		} else {
			ret = rx_send_one_node_locked(dev, vh_pchan, vq,
							send_node, &added);
			if (ret)
				break; /* no more rx buf wait for next round */

			mutex_lock(&vh_pchan->send_list_mutex);
			list_del(&send_node->node);
			mutex_unlock(&vh_pchan->send_list_mutex);
			kfree(send_node); /* send OK process more */
		}
	}

	if (added)
		vhost_signal(dev, vq);

err_unlock:
	mutex_unlock(&vq->mutex);
	return 0;
}

int physical_channel_send(struct physical_channel *pchan,
			struct hab_header *header,
			void *payload,
			unsigned int flags)
{
	struct vhost_hab_pchannel *vh_pchan = pchan->hyp_data;
	struct vhost_virtqueue *vq;
	struct vhost_hab_dev *vh_dev;
	struct vhost_hab_send_node *send_node;
	size_t sizebytes = HAB_HEADER_GET_SIZE(*header);

	/* Only used in virtio arch */
	(void)flags;

	if (!vh_pchan) {
		pr_err("pchan is not ready yet\n");
		return -ENODEV;
	}

	vq = &vh_pchan->vqs[VHOST_HAB_PCHAN_RX_VQ];
	vh_dev = container_of(vq->dev, struct vhost_hab_dev, dev);

	send_node = kmalloc(sizebytes + sizeof(struct vhost_hab_send_node),
				GFP_KERNEL);
	if (!send_node)
		return -ENOMEM;

	send_node->header = *header;
	memcpy(send_node->payload, payload, sizebytes);

	mutex_lock(&vh_pchan->send_list_mutex);
	list_add_tail(&send_node->node, &vh_pchan->send_list);
	mutex_unlock(&vh_pchan->send_list_mutex);

	vhost_work_queue(&vh_dev->dev, &vh_pchan->rx_send_work);

	return 0;
}

int physical_channel_read(struct physical_channel *pchan,
						void *payload,
						size_t read_size)
{
	struct vhost_hab_pchannel *vh_pchan = pchan->hyp_data;
	ssize_t copy_size;

	if (!vh_pchan) {
		pr_err("pchan is not ready yet\n");
		return -ENODEV;
	}

	copy_size = copy_from_iter(payload, read_size, &vh_pchan->out_iter);
	if (unlikely(copy_size != read_size))
		pr_err("fault on copy_from_iter, read_size %lu, ret %lu\n",
			read_size, copy_size);

	return copy_size;
}

void physical_channel_rx_dispatch(unsigned long physical_channel)
{
	struct physical_channel *pchan =
			(struct physical_channel *)physical_channel;
	struct vhost_hab_pchannel *vh_pchan = pchan->hyp_data;
	struct vhost_virtqueue *vq = vh_pchan->vqs + VHOST_HAB_PCHAN_TX_VQ;
	struct vhost_hab_dev *vh_dev = container_of(vq->dev,
						struct vhost_hab_dev, dev);

	if (!vh_pchan) {
		pr_err("pchan is not ready yet\n");
		return;
	}

	vq = vh_pchan->vqs + VHOST_HAB_PCHAN_TX_VQ;
	vh_dev = container_of(vq->dev, struct vhost_hab_dev, dev);

	vhost_work_queue(&vh_dev->dev, &vh_pchan->tx_recv_work);
}

static const struct file_operations vhost_hab_fops = {
	.owner = THIS_MODULE,
	.open = vhost_hab_open,
	.release = vhost_hab_release,
	.unlocked_ioctl = vhost_hab_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = vhost_hab_compat_ioctl,
#endif
};

static struct vhost_hab_cdev *get_cdev(uint32_t domain_id)
{
	struct vhost_hab *vh = &g_vh;
	struct vhost_hab_cdev *vh_cdev = vh->vh_cdevs[domain_id];
	int ret;

	if (vh_cdev != NULL)
		return vh_cdev;

	vh_cdev = kzalloc(sizeof(*vh_cdev), GFP_KERNEL);
	if (vh_cdev == NULL)
		return NULL;

	cdev_init(&vh_cdev->cdev, &vhost_hab_fops);
	vh_cdev->cdev.owner = THIS_MODULE;
	vh_cdev->dev_no = MKDEV(vh->major, domain_id);
	ret = cdev_add(&vh_cdev->cdev, vh_cdev->dev_no, 1);
	if (ret) {
		pr_err("cdev_add failed for dev_no %d, domain_id %d\n",
			vh_cdev->dev_no, domain_id);
		goto err_free_cdev;
	}

	vh_cdev->dev = device_create(vh->class, NULL, vh_cdev->dev_no, NULL,
					"vhost-%s", hab_area_names[domain_id]);
	if (IS_ERR_OR_NULL(vh_cdev->dev)) {
		pr_err("device_create failed for, domain_id %d\n", domain_id);
		goto err_cdev_del;
	}

	vh_cdev->domain_id = domain_id;

	vh->vh_cdevs[domain_id] = vh_cdev;

	return vh_cdev;

err_cdev_del:
	cdev_del(&vh_cdev->cdev);
err_free_cdev:
	kfree(vh_cdev);

	return NULL;
}

static void del_hab_device_from_cdev(uint32_t mmid, struct hab_device *habdev)
{
	struct vhost_hab *vh = &g_vh;
	struct vhost_hab_cdev *vh_cdev;
	uint32_t domain_id = mmid / 100;
	bool destroy = true;
	int i;

	vh_cdev = vh->vh_cdevs[domain_id];
	if (vh_cdev == NULL) {
		pr_err("cdev not created for domain %d\n", domain_id);
		return;
	}

	for (i = 0; i < HABCFG_MMID_NUM; i++) {
		if (vh_cdev->habdevs[i] == habdev)
			vh_cdev->habdevs[i] = NULL;
		else if (vh_cdev->habdevs[i] != NULL)
			destroy = false;
	}

	/* if no habdev is on this cdev, destroy it */
	if (!destroy)
		return;

	pr_info("delete cdev, mmid %d\n", mmid);
	device_destroy(vh->class, vh_cdev->dev_no);
	cdev_del(&vh_cdev->cdev);
	kfree(vh_cdev);
	vh->vh_cdevs[domain_id] = NULL;
}

static void vhost_hab_cdev_del_hab_device(struct hab_device *habdev)
{
	del_hab_device_from_cdev(habdev->id, habdev);
	del_hab_device_from_cdev(HAB_MMID_ALL_AREA, habdev);
}

static int add_hab_device_to_cdev(uint32_t mmid, struct hab_device *habdev)
{
	struct vhost_hab_cdev *vh_cdev;
	uint32_t domain_id = mmid / 100;
	int i;

	vh_cdev = get_cdev(domain_id);
	if (vh_cdev == NULL)
		return -ENODEV;

	/* add hab device to the new slot */
	for (i = 0; i < HABCFG_MMID_NUM; i++)
		if (vh_cdev->habdevs[i] == NULL) {
			vh_cdev->habdevs[i] = habdev;
			break;
		}

	if (i >= HABCFG_MMID_NUM) {
		pr_err("too many hab devices created\n");
		return -EINVAL;
	}

	return 0;
}

static int vhost_hab_cdev_add_hab_device(struct hab_device *habdev)
{
	int ret;

	ret = add_hab_device_to_cdev(HAB_MMID_ALL_AREA, habdev);
	if (ret)
		return ret;

	ret = add_hab_device_to_cdev(habdev->id, habdev);
	if (ret)
		del_hab_device_from_cdev(HAB_MMID_ALL_AREA, habdev);

	return ret;
}

int habhyp_commdev_alloc(void **commdev, int is_be, char *name,
			int vmid_remote, struct hab_device *habdev)
{
	struct physical_channel *pchan;
	struct vhost_hab_pchannel *vh_pchan;
	int ret;

	pchan = hab_pchan_alloc(habdev, vmid_remote);
	if (!pchan) {
		pr_err("failed to create %s pchan in mmid device %s, pchan cnt %d\n",
			name, habdev->name, habdev->pchan_cnt);
		*commdev = NULL;
		return -ENOMEM;
	}

	pchan->closed = 0;
	pchan->is_be = 1; /* vhost is always backend */
	strscpy(pchan->name, name, sizeof(pchan->name));

	pr_info("pchan on %s, loopback %d, total pchan %d, vmid %d\n",
		name, hab_driver.b_loopback, habdev->pchan_cnt, vmid_remote);

	vh_pchan = kzalloc(sizeof(*vh_pchan), GFP_KERNEL);
	if (!vh_pchan) {
		hab_pchan_put(pchan);
		ret = -ENOMEM;
		*commdev = NULL;
		goto err_free_pchan;
	}

	vh_pchan->vqs[VHOST_HAB_PCHAN_TX_VQ].handle_kick = handle_tx_vq_kick;
	vh_pchan->vqs[VHOST_HAB_PCHAN_RX_VQ].handle_kick = handle_rx_vq_kick;

	mutex_init(&vh_pchan->send_list_mutex);
	INIT_LIST_HEAD(&vh_pchan->send_list);

	/* only add hab device when the first pchannel is added to it */
	if (habdev->pchan_cnt == 1) {
		ret = vhost_hab_cdev_add_hab_device(habdev);
		if (ret) {
			pr_err("vhost_hab_cdev_add_hab_device failed, vmid %d, mmid %d\n",
				vmid_remote, habdev->id);
			goto err_free_vh_pchan;
		}
	}

	vh_pchan->habdev = habdev;
	list_add_tail(&vh_pchan->node, &g_vh.vh_pchan_list);
	*commdev = pchan;

	pr_debug("pchan %s vchans %d refcnt %d\n",
		pchan->name, pchan->vcnt, get_refcnt(pchan->refcount));

	return 0;

err_free_vh_pchan:
	kfree(vh_pchan);
err_free_pchan:
	hab_pchan_put(pchan);

	return ret;
}

int habhyp_commdev_dealloc(void *commdev)
{
	struct physical_channel *pchan = commdev;
	struct hab_device *habdev = pchan->habdev;

	/* only remove hab device when removing the last pchannel */
	if (habdev->pchan_cnt == 1)
		vhost_hab_cdev_del_hab_device(habdev);

	hab_pchan_put(pchan);

	return 0;
}

int hab_stat_log(struct physical_channel **pchans, int pchan_cnt, char *dest,
			int dest_size)
{
	struct vhost_hab_stat_work stat_work;
	struct vhost_hab_pchannel *vh_pchan;
	int i, ret = 0;

	stat_work.pchans = pchans;
	stat_work.pchan_count = pchan_cnt;

	INIT_WORK_ONSTACK(&stat_work.work, stat_worker);
	queue_work(g_vh.wq, &stat_work.work);
	flush_workqueue(g_vh.wq);

	destroy_work_on_stack(&stat_work.work);

	mutex_lock(&g_vh.pchan_mutex);
	for (i = 0; i < pchan_cnt; i++) {
		vh_pchan = pchans[i]->hyp_data;
		if (!vh_pchan) {
			pr_err("%s: pchan %s is not ready\n", __func__,
				pchans[i]->name);
			continue;
		}
		ret = hab_stat_buffer_print(dest, dest_size,
				"mmid %d: vq empty tx %d rx %d\n",
				vh_pchan->habdev->id, vh_pchan->tx_empty,
				vh_pchan->rx_empty);
		if (ret)
			break;
	}
	mutex_unlock(&g_vh.pchan_mutex);

	return ret;
}

static void stat_worker(struct work_struct *work)
{
	struct vhost_hab_stat_work *stat_work = container_of(work,
					struct vhost_hab_stat_work, work);
	struct vhost_hab_pchannel *vh_pchan;
	struct vhost_virtqueue *vq_tx;
	struct vhost_virtqueue *vq_rx;
	int i;
	mm_segment_t oldfs;

	mutex_lock(&g_vh.pchan_mutex);
	for (i = 0; i < stat_work->pchan_count; i++) {
		vh_pchan = stat_work->pchans[i]->hyp_data;
		if (!vh_pchan) {
			pr_err("%s: pchan %s is not ready\n", __func__,
				stat_work->pchans[i]->name);
			continue;
		}
		vq_tx = vh_pchan->vqs + VHOST_HAB_PCHAN_TX_VQ;
		vq_rx = vh_pchan->vqs + VHOST_HAB_PCHAN_RX_VQ;

		oldfs = get_fs();

		set_fs(USER_DS);
		use_mm(vq_tx->dev->mm);

		vh_pchan->tx_empty = vhost_vq_avail_empty(vq_tx->dev, vq_tx);
		vh_pchan->rx_empty = vhost_vq_avail_empty(vq_rx->dev, vq_rx);

		unuse_mm(vq_tx->dev->mm);
		set_fs(oldfs);

		pr_info("%s mmid %d vq tx num %d empty %d vq rx num %d empty %d\n",
			vh_pchan->pchan->name, vh_pchan->pchan->habdev->id, vq_tx->num,
			vh_pchan->tx_empty, vq_rx->num, vh_pchan->rx_empty);
	}
	mutex_unlock(&g_vh.pchan_mutex);
}
