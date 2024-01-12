// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include "hab.h"

struct loopback_msg {
	struct list_head node;
	int payload_size;
	struct hab_header header;
	char payload[];
};

struct lb_thread_struct {
	int stop; /* set by creator */
	int bexited; /* set by thread */
	void *data; /* thread private data */
};

struct loopback_dev {
	spinlock_t io_lock;
	struct list_head msg_list;
	int msg_cnt;
	struct task_struct *kthread; /* creator's thread handle */
	struct lb_thread_struct thread_data; /* thread private data */
	wait_queue_head_t thread_queue;
	struct loopback_msg *current_msg;
};

static int lb_thread_queue_empty(struct loopback_dev *dev)
{
	int ret;

	spin_lock_bh(&dev->io_lock);
	ret = list_empty(&dev->msg_list);
	spin_unlock_bh(&dev->io_lock);
	return ret;
}


int lb_kthread(void *d)
{
	struct lb_thread_struct *p = (struct lb_thread_struct *)d;
	struct physical_channel *pchan = (struct physical_channel *)p->data;
	struct loopback_dev *dev = pchan->hyp_data;
	int ret = 0;

	while (!p->stop) {
		schedule();
		ret = wait_event_interruptible(dev->thread_queue,
				   !lb_thread_queue_empty(dev) ||
				   p->stop);

		spin_lock_bh(&dev->io_lock);

		while (!list_empty(&dev->msg_list)) {
			struct loopback_msg *msg = NULL;

			msg = list_first_entry(&dev->msg_list,
					struct loopback_msg, node);
			dev->current_msg = msg;
			list_del(&msg->node);
			dev->msg_cnt--;

			ret = hab_msg_recv(pchan, &msg->header);
			if (ret) {
				pr_err("failed %d msg handling sz %d header %d %d %d, %d %X %d, total %d\n",
					ret, msg->payload_size,
					HAB_HEADER_GET_ID(msg->header),
					HAB_HEADER_GET_TYPE(msg->header),
					HAB_HEADER_GET_SIZE(msg->header),
					msg->header.session_id,
					msg->header.signature,
					msg->header.sequence, dev->msg_cnt);
			}

			kfree(msg);
			dev->current_msg = NULL;
		}

		spin_unlock_bh(&dev->io_lock);
	}
	p->bexited = 1;
	pr_debug("exit kthread\n");
	return 0;
}

int physical_channel_send(struct physical_channel *pchan,
			struct hab_header *header,
			void *payload, unsigned int flags)
{
	int size = HAB_HEADER_GET_SIZE(*header); /* payload size */
	struct timespec64 ts = {0};
	struct loopback_msg *msg = NULL;
	struct loopback_dev *dev = pchan->hyp_data;

	/* Only used in virtio arch */
	(void)flags;

	msg = kmalloc(size + sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	memcpy(&msg->header, header, sizeof(*header));
	msg->payload_size = size; /* payload size could be zero */

	if (size && payload) {
		if (HAB_HEADER_GET_TYPE(*header) == HAB_PAYLOAD_TYPE_PROFILE) {
			struct habmm_xing_vm_stat *pstat =
				(struct habmm_xing_vm_stat *)payload;

			ktime_get_ts64(&ts);
			pstat->tx_sec = ts.tv_sec;
			pstat->tx_usec = ts.tv_nsec/NSEC_PER_USEC;
		}

		memcpy(msg->payload, payload, size);
	}

	spin_lock_bh(&dev->io_lock);
	list_add_tail(&msg->node, &dev->msg_list);
	dev->msg_cnt++;
	spin_unlock_bh(&dev->io_lock);

	wake_up_interruptible(&dev->thread_queue);
	return 0;
}

/* loopback read is only used during open */
int physical_channel_read(struct physical_channel *pchan,
				void *payload,
				size_t read_size)
{
	struct loopback_dev *dev = pchan->hyp_data;
	struct loopback_msg *msg = dev->current_msg;

	if (read_size) {
		if (read_size != msg->payload_size) {
			pr_err("read size mismatch requested %zd, received %d\n",
					read_size, msg->payload_size);
			memcpy(payload, msg->payload, min(((int)read_size),
				msg->payload_size));
		} else {
			memcpy(payload, msg->payload, read_size);
		}
	} else {
		read_size = 0;
	}
	return read_size;
}

/* pchan is directly added into the hab_device */
int loopback_pchan_create(struct hab_device *dev, char *pchan_name)
{
	int result;
	struct physical_channel *pchan = NULL;
	struct loopback_dev *lb_dev = NULL;

	pchan = hab_pchan_alloc(dev, LOOPBACK_DOM);
	if (!pchan) {
		result = -ENOMEM;
		goto err;
	}

	pchan->closed = 0;
	strscpy(pchan->name, pchan_name, sizeof(pchan->name));

	lb_dev = kzalloc(sizeof(*lb_dev), GFP_KERNEL);
	if (!lb_dev) {
		result = -ENOMEM;
		goto err;
	}

	spin_lock_init(&lb_dev->io_lock);
	INIT_LIST_HEAD(&lb_dev->msg_list);

	init_waitqueue_head(&lb_dev->thread_queue);

	lb_dev->thread_data.data = pchan;
	lb_dev->kthread = kthread_run(lb_kthread, &lb_dev->thread_data,
				pchan->name);
	if (IS_ERR(lb_dev->kthread)) {
		result = PTR_ERR(lb_dev->kthread);
		pr_err("failed to create kthread for %s, ret %d\n",
			   pchan->name, result);
		goto err;
	}

	pchan->hyp_data = lb_dev;

	return 0;
err:
	kfree(lb_dev);
	kfree(pchan);

	return result;
}

void physical_channel_rx_dispatch(unsigned long data)
{
}

int habhyp_commdev_alloc(void **commdev, int is_be, char *name,
			int vmid_remote, struct hab_device *mmid_device)
{
	struct physical_channel *pchan;

	int ret = loopback_pchan_create(mmid_device, name);

	if (ret) {
		pr_err("failed to create %s pchan in mmid device %s, ret %d, pchan cnt %d\n",
			name, mmid_device->name, ret, mmid_device->pchan_cnt);
		*commdev = NULL;
	} else {
		pr_debug("loopback physical channel on %s return %d, loopback mode(%d), total pchan %d\n",
			name, ret, hab_driver.b_loopback,
			mmid_device->pchan_cnt);
		pchan = hab_pchan_find_domid(mmid_device,
				HABCFG_VMID_DONT_CARE);
		*commdev = pchan;
		hab_pchan_put(pchan);

		pr_debug("pchan %s vchans %d refcnt %d\n",
			pchan->name, pchan->vcnt, get_refcnt(pchan->refcount));
	}

	return ret;
}

int habhyp_commdev_dealloc(void *commdev)
{
	struct physical_channel *pchan = commdev;
	struct loopback_dev *dev = pchan->hyp_data;
	struct loopback_msg *msg, *tmp;
	int ret;

	spin_lock_bh(&dev->io_lock);
	if (!list_empty(&dev->msg_list) || dev->msg_cnt) {
		pr_err("pchan %s msg leak cnt %d\n", pchan->name, dev->msg_cnt);

		list_for_each_entry_safe(msg, tmp, &dev->msg_list, node) {
			list_del(&msg->node);
			dev->msg_cnt--;
			kfree(msg);
		}
		pr_debug("pchan %s msg cnt %d now\n",
				pchan->name, dev->msg_cnt);
	}
	spin_unlock_bh(&dev->io_lock);

	dev->thread_data.stop = 1;
	ret = kthread_stop(dev->kthread);
	while (!dev->thread_data.bexited)
		schedule();
	dev->kthread = NULL;

	/* hyp_data is freed in pchan  */
	if (get_refcnt(pchan->refcount) > 1) {
		pr_warn("potential leak pchan %s vchans %d refcnt %d\n",
			pchan->name, pchan->vcnt, get_refcnt(pchan->refcount));
	}
	hab_pchan_put((struct physical_channel *)commdev);

	return 0;
}
