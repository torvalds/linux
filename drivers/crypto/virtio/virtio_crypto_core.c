// SPDX-License-Identifier: GPL-2.0-or-later
 /* Driver for Virtio crypto device.
  *
  * Copyright 2016 HUAWEI TECHNOLOGIES CO., LTD.
  */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/virtio_config.h>
#include <linux/cpu.h>

#include <uapi/linux/virtio_crypto.h>
#include "virtio_crypto_common.h"


void
virtcrypto_clear_request(struct virtio_crypto_request *vc_req)
{
	if (vc_req) {
		kfree_sensitive(vc_req->req_data);
		kfree(vc_req->sgs);
	}
}

static void virtio_crypto_ctrlq_callback(struct virtio_crypto_ctrl_request *vc_ctrl_req)
{
	complete(&vc_ctrl_req->compl);
}

static void virtcrypto_ctrlq_callback(struct virtqueue *vq)
{
	struct virtio_crypto *vcrypto = vq->vdev->priv;
	struct virtio_crypto_ctrl_request *vc_ctrl_req;
	unsigned long flags;
	unsigned int len;

	spin_lock_irqsave(&vcrypto->ctrl_lock, flags);
	do {
		virtqueue_disable_cb(vq);
		while ((vc_ctrl_req = virtqueue_get_buf(vq, &len)) != NULL) {
			spin_unlock_irqrestore(&vcrypto->ctrl_lock, flags);
			virtio_crypto_ctrlq_callback(vc_ctrl_req);
			spin_lock_irqsave(&vcrypto->ctrl_lock, flags);
		}
		if (unlikely(virtqueue_is_broken(vq)))
			break;
	} while (!virtqueue_enable_cb(vq));
	spin_unlock_irqrestore(&vcrypto->ctrl_lock, flags);
}

int virtio_crypto_ctrl_vq_request(struct virtio_crypto *vcrypto, struct scatterlist *sgs[],
		unsigned int out_sgs, unsigned int in_sgs,
		struct virtio_crypto_ctrl_request *vc_ctrl_req)
{
	int err;
	unsigned long flags;

	init_completion(&vc_ctrl_req->compl);

	spin_lock_irqsave(&vcrypto->ctrl_lock, flags);
	err = virtqueue_add_sgs(vcrypto->ctrl_vq, sgs, out_sgs, in_sgs, vc_ctrl_req, GFP_ATOMIC);
	if (err < 0) {
		spin_unlock_irqrestore(&vcrypto->ctrl_lock, flags);
		return err;
	}

	virtqueue_kick(vcrypto->ctrl_vq);
	spin_unlock_irqrestore(&vcrypto->ctrl_lock, flags);

	wait_for_completion(&vc_ctrl_req->compl);

	return 0;
}

static void virtcrypto_done_task(unsigned long data)
{
	struct data_queue *data_vq = (struct data_queue *)data;
	struct virtqueue *vq = data_vq->vq;
	struct virtio_crypto_request *vc_req;
	unsigned int len;

	do {
		virtqueue_disable_cb(vq);
		while ((vc_req = virtqueue_get_buf(vq, &len)) != NULL) {
			if (vc_req->alg_cb)
				vc_req->alg_cb(vc_req, len);
		}
	} while (!virtqueue_enable_cb(vq));
}

static void virtcrypto_dataq_callback(struct virtqueue *vq)
{
	struct virtio_crypto *vcrypto = vq->vdev->priv;
	struct data_queue *dq = &vcrypto->data_vq[vq->index];

	tasklet_schedule(&dq->done_task);
}

static int virtcrypto_find_vqs(struct virtio_crypto *vi)
{
	vq_callback_t **callbacks;
	struct virtqueue **vqs;
	int ret = -ENOMEM;
	int i, total_vqs;
	const char **names;
	struct device *dev = &vi->vdev->dev;

	/*
	 * We expect 1 data virtqueue, followed by
	 * possible N-1 data queues used in multiqueue mode,
	 * followed by control vq.
	 */
	total_vqs = vi->max_data_queues + 1;

	/* Allocate space for find_vqs parameters */
	vqs = kcalloc(total_vqs, sizeof(*vqs), GFP_KERNEL);
	if (!vqs)
		goto err_vq;
	callbacks = kcalloc(total_vqs, sizeof(*callbacks), GFP_KERNEL);
	if (!callbacks)
		goto err_callback;
	names = kcalloc(total_vqs, sizeof(*names), GFP_KERNEL);
	if (!names)
		goto err_names;

	/* Parameters for control virtqueue */
	callbacks[total_vqs - 1] = virtcrypto_ctrlq_callback;
	names[total_vqs - 1] = "controlq";

	/* Allocate/initialize parameters for data virtqueues */
	for (i = 0; i < vi->max_data_queues; i++) {
		callbacks[i] = virtcrypto_dataq_callback;
		snprintf(vi->data_vq[i].name, sizeof(vi->data_vq[i].name),
				"dataq.%d", i);
		names[i] = vi->data_vq[i].name;
	}

	ret = virtio_find_vqs(vi->vdev, total_vqs, vqs, callbacks, names, NULL);
	if (ret)
		goto err_find;

	vi->ctrl_vq = vqs[total_vqs - 1];

	for (i = 0; i < vi->max_data_queues; i++) {
		spin_lock_init(&vi->data_vq[i].lock);
		vi->data_vq[i].vq = vqs[i];
		/* Initialize crypto engine */
		vi->data_vq[i].engine = crypto_engine_alloc_init_and_set(dev, true, NULL, true,
						virtqueue_get_vring_size(vqs[i]));
		if (!vi->data_vq[i].engine) {
			ret = -ENOMEM;
			goto err_engine;
		}
		tasklet_init(&vi->data_vq[i].done_task, virtcrypto_done_task,
				(unsigned long)&vi->data_vq[i]);
	}

	kfree(names);
	kfree(callbacks);
	kfree(vqs);

	return 0;

err_engine:
err_find:
	kfree(names);
err_names:
	kfree(callbacks);
err_callback:
	kfree(vqs);
err_vq:
	return ret;
}

static int virtcrypto_alloc_queues(struct virtio_crypto *vi)
{
	vi->data_vq = kcalloc(vi->max_data_queues, sizeof(*vi->data_vq),
				GFP_KERNEL);
	if (!vi->data_vq)
		return -ENOMEM;

	return 0;
}

static void virtcrypto_clean_affinity(struct virtio_crypto *vi, long hcpu)
{
	int i;

	if (vi->affinity_hint_set) {
		for (i = 0; i < vi->max_data_queues; i++)
			virtqueue_set_affinity(vi->data_vq[i].vq, NULL);

		vi->affinity_hint_set = false;
	}
}

static void virtcrypto_set_affinity(struct virtio_crypto *vcrypto)
{
	int i = 0;
	int cpu;

	/*
	 * In single queue mode, we don't set the cpu affinity.
	 */
	if (vcrypto->curr_queue == 1 || vcrypto->max_data_queues == 1) {
		virtcrypto_clean_affinity(vcrypto, -1);
		return;
	}

	/*
	 * In multiqueue mode, we let the queue to be private to one cpu
	 * by setting the affinity hint to eliminate the contention.
	 *
	 * TODO: adds cpu hotplug support by register cpu notifier.
	 *
	 */
	for_each_online_cpu(cpu) {
		virtqueue_set_affinity(vcrypto->data_vq[i].vq, cpumask_of(cpu));
		if (++i >= vcrypto->max_data_queues)
			break;
	}

	vcrypto->affinity_hint_set = true;
}

static void virtcrypto_free_queues(struct virtio_crypto *vi)
{
	kfree(vi->data_vq);
}

static int virtcrypto_init_vqs(struct virtio_crypto *vi)
{
	int ret;

	/* Allocate send & receive queues */
	ret = virtcrypto_alloc_queues(vi);
	if (ret)
		goto err;

	ret = virtcrypto_find_vqs(vi);
	if (ret)
		goto err_free;

	cpus_read_lock();
	virtcrypto_set_affinity(vi);
	cpus_read_unlock();

	return 0;

err_free:
	virtcrypto_free_queues(vi);
err:
	return ret;
}

static int virtcrypto_update_status(struct virtio_crypto *vcrypto)
{
	u32 status;
	int err;

	virtio_cread_le(vcrypto->vdev,
			struct virtio_crypto_config, status, &status);

	/*
	 * Unknown status bits would be a host error and the driver
	 * should consider the device to be broken.
	 */
	if (status & (~VIRTIO_CRYPTO_S_HW_READY)) {
		dev_warn(&vcrypto->vdev->dev,
				"Unknown status bits: 0x%x\n", status);

		virtio_break_device(vcrypto->vdev);
		return -EPERM;
	}

	if (vcrypto->status == status)
		return 0;

	vcrypto->status = status;

	if (vcrypto->status & VIRTIO_CRYPTO_S_HW_READY) {
		err = virtcrypto_dev_start(vcrypto);
		if (err) {
			dev_err(&vcrypto->vdev->dev,
				"Failed to start virtio crypto device.\n");

			return -EPERM;
		}
		dev_info(&vcrypto->vdev->dev, "Accelerator device is ready\n");
	} else {
		virtcrypto_dev_stop(vcrypto);
		dev_info(&vcrypto->vdev->dev, "Accelerator is not ready\n");
	}

	return 0;
}

static int virtcrypto_start_crypto_engines(struct virtio_crypto *vcrypto)
{
	int32_t i;
	int ret;

	for (i = 0; i < vcrypto->max_data_queues; i++) {
		if (vcrypto->data_vq[i].engine) {
			ret = crypto_engine_start(vcrypto->data_vq[i].engine);
			if (ret)
				goto err;
		}
	}

	return 0;

err:
	while (--i >= 0)
		if (vcrypto->data_vq[i].engine)
			crypto_engine_exit(vcrypto->data_vq[i].engine);

	return ret;
}

static void virtcrypto_clear_crypto_engines(struct virtio_crypto *vcrypto)
{
	u32 i;

	for (i = 0; i < vcrypto->max_data_queues; i++)
		if (vcrypto->data_vq[i].engine)
			crypto_engine_exit(vcrypto->data_vq[i].engine);
}

static void virtcrypto_del_vqs(struct virtio_crypto *vcrypto)
{
	struct virtio_device *vdev = vcrypto->vdev;

	virtcrypto_clean_affinity(vcrypto, -1);

	vdev->config->del_vqs(vdev);

	virtcrypto_free_queues(vcrypto);
}

static void vcrypto_config_changed_work(struct work_struct *work)
{
	struct virtio_crypto *vcrypto =
		container_of(work, struct virtio_crypto, config_work);

	virtcrypto_update_status(vcrypto);
}

static int virtcrypto_probe(struct virtio_device *vdev)
{
	int err = -EFAULT;
	struct virtio_crypto *vcrypto;
	u32 max_data_queues = 0, max_cipher_key_len = 0;
	u32 max_auth_key_len = 0;
	u64 max_size = 0;
	u32 cipher_algo_l = 0;
	u32 cipher_algo_h = 0;
	u32 hash_algo = 0;
	u32 mac_algo_l = 0;
	u32 mac_algo_h = 0;
	u32 aead_algo = 0;
	u32 akcipher_algo = 0;
	u32 crypto_services = 0;

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1))
		return -ENODEV;

	if (!vdev->config->get) {
		dev_err(&vdev->dev, "%s failure: config access disabled\n",
			__func__);
		return -EINVAL;
	}

	if (num_possible_nodes() > 1 && dev_to_node(&vdev->dev) < 0) {
		/*
		 * If the accelerator is connected to a node with no memory
		 * there is no point in using the accelerator since the remote
		 * memory transaction will be very slow.
		 */
		dev_err(&vdev->dev, "Invalid NUMA configuration.\n");
		return -EINVAL;
	}

	vcrypto = kzalloc_node(sizeof(*vcrypto), GFP_KERNEL,
					dev_to_node(&vdev->dev));
	if (!vcrypto)
		return -ENOMEM;

	virtio_cread_le(vdev, struct virtio_crypto_config,
			max_dataqueues, &max_data_queues);
	if (max_data_queues < 1)
		max_data_queues = 1;

	virtio_cread_le(vdev, struct virtio_crypto_config,
			max_cipher_key_len, &max_cipher_key_len);
	virtio_cread_le(vdev, struct virtio_crypto_config,
			max_auth_key_len, &max_auth_key_len);
	virtio_cread_le(vdev, struct virtio_crypto_config,
			max_size, &max_size);
	virtio_cread_le(vdev, struct virtio_crypto_config,
			crypto_services, &crypto_services);
	virtio_cread_le(vdev, struct virtio_crypto_config,
			cipher_algo_l, &cipher_algo_l);
	virtio_cread_le(vdev, struct virtio_crypto_config,
			cipher_algo_h, &cipher_algo_h);
	virtio_cread_le(vdev, struct virtio_crypto_config,
			hash_algo, &hash_algo);
	virtio_cread_le(vdev, struct virtio_crypto_config,
			mac_algo_l, &mac_algo_l);
	virtio_cread_le(vdev, struct virtio_crypto_config,
			mac_algo_h, &mac_algo_h);
	virtio_cread_le(vdev, struct virtio_crypto_config,
			aead_algo, &aead_algo);
	if (crypto_services & (1 << VIRTIO_CRYPTO_SERVICE_AKCIPHER))
		virtio_cread_le(vdev, struct virtio_crypto_config,
				akcipher_algo, &akcipher_algo);

	/* Add virtio crypto device to global table */
	err = virtcrypto_devmgr_add_dev(vcrypto);
	if (err) {
		dev_err(&vdev->dev, "Failed to add new virtio crypto device.\n");
		goto free;
	}
	vcrypto->owner = THIS_MODULE;
	vcrypto = vdev->priv = vcrypto;
	vcrypto->vdev = vdev;

	spin_lock_init(&vcrypto->ctrl_lock);

	/* Use single data queue as default */
	vcrypto->curr_queue = 1;
	vcrypto->max_data_queues = max_data_queues;
	vcrypto->max_cipher_key_len = max_cipher_key_len;
	vcrypto->max_auth_key_len = max_auth_key_len;
	vcrypto->max_size = max_size;
	vcrypto->crypto_services = crypto_services;
	vcrypto->cipher_algo_l = cipher_algo_l;
	vcrypto->cipher_algo_h = cipher_algo_h;
	vcrypto->mac_algo_l = mac_algo_l;
	vcrypto->mac_algo_h = mac_algo_h;
	vcrypto->hash_algo = hash_algo;
	vcrypto->aead_algo = aead_algo;
	vcrypto->akcipher_algo = akcipher_algo;

	dev_info(&vdev->dev,
		"max_queues: %u, max_cipher_key_len: %u, max_auth_key_len: %u, max_size 0x%llx\n",
		vcrypto->max_data_queues,
		vcrypto->max_cipher_key_len,
		vcrypto->max_auth_key_len,
		vcrypto->max_size);

	err = virtcrypto_init_vqs(vcrypto);
	if (err) {
		dev_err(&vdev->dev, "Failed to initialize vqs.\n");
		goto free_dev;
	}

	err = virtcrypto_start_crypto_engines(vcrypto);
	if (err)
		goto free_vqs;

	virtio_device_ready(vdev);

	err = virtcrypto_update_status(vcrypto);
	if (err)
		goto free_engines;

	INIT_WORK(&vcrypto->config_work, vcrypto_config_changed_work);

	return 0;

free_engines:
	virtcrypto_clear_crypto_engines(vcrypto);
free_vqs:
	virtio_reset_device(vdev);
	virtcrypto_del_vqs(vcrypto);
free_dev:
	virtcrypto_devmgr_rm_dev(vcrypto);
free:
	kfree(vcrypto);
	return err;
}

static void virtcrypto_free_unused_reqs(struct virtio_crypto *vcrypto)
{
	struct virtio_crypto_request *vc_req;
	int i;
	struct virtqueue *vq;

	for (i = 0; i < vcrypto->max_data_queues; i++) {
		vq = vcrypto->data_vq[i].vq;
		while ((vc_req = virtqueue_detach_unused_buf(vq)) != NULL) {
			kfree(vc_req->req_data);
			kfree(vc_req->sgs);
		}
	}
}

static void virtcrypto_remove(struct virtio_device *vdev)
{
	struct virtio_crypto *vcrypto = vdev->priv;

	dev_info(&vdev->dev, "Start virtcrypto_remove.\n");

	flush_work(&vcrypto->config_work);
	if (virtcrypto_dev_started(vcrypto))
		virtcrypto_dev_stop(vcrypto);
	virtio_reset_device(vdev);
	virtcrypto_free_unused_reqs(vcrypto);
	virtcrypto_clear_crypto_engines(vcrypto);
	virtcrypto_del_vqs(vcrypto);
	virtcrypto_devmgr_rm_dev(vcrypto);
	kfree(vcrypto);
}

static void virtcrypto_config_changed(struct virtio_device *vdev)
{
	struct virtio_crypto *vcrypto = vdev->priv;

	schedule_work(&vcrypto->config_work);
}

#ifdef CONFIG_PM_SLEEP
static int virtcrypto_freeze(struct virtio_device *vdev)
{
	struct virtio_crypto *vcrypto = vdev->priv;

	flush_work(&vcrypto->config_work);
	virtio_reset_device(vdev);
	virtcrypto_free_unused_reqs(vcrypto);
	if (virtcrypto_dev_started(vcrypto))
		virtcrypto_dev_stop(vcrypto);

	virtcrypto_clear_crypto_engines(vcrypto);
	virtcrypto_del_vqs(vcrypto);
	return 0;
}

static int virtcrypto_restore(struct virtio_device *vdev)
{
	struct virtio_crypto *vcrypto = vdev->priv;
	int err;

	err = virtcrypto_init_vqs(vcrypto);
	if (err)
		return err;

	err = virtcrypto_start_crypto_engines(vcrypto);
	if (err)
		goto free_vqs;

	virtio_device_ready(vdev);

	err = virtcrypto_dev_start(vcrypto);
	if (err) {
		dev_err(&vdev->dev, "Failed to start virtio crypto device.\n");
		goto free_engines;
	}

	return 0;

free_engines:
	virtcrypto_clear_crypto_engines(vcrypto);
free_vqs:
	virtio_reset_device(vdev);
	virtcrypto_del_vqs(vcrypto);
	return err;
}
#endif

static const unsigned int features[] = {
	/* none */
};

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_CRYPTO, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static struct virtio_driver virtio_crypto_driver = {
	.driver.name         = KBUILD_MODNAME,
	.driver.owner        = THIS_MODULE,
	.feature_table       = features,
	.feature_table_size  = ARRAY_SIZE(features),
	.id_table            = id_table,
	.probe               = virtcrypto_probe,
	.remove              = virtcrypto_remove,
	.config_changed = virtcrypto_config_changed,
#ifdef CONFIG_PM_SLEEP
	.freeze = virtcrypto_freeze,
	.restore = virtcrypto_restore,
#endif
};

module_virtio_driver(virtio_crypto_driver);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("virtio crypto device driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gonglei <arei.gonglei@huawei.com>");
