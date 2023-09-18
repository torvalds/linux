// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation */
#include <linux/module.h>
#include <linux/slab.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_transport.h"
#include "adf_transport_access_macros.h"
#include "adf_cfg.h"
#include "adf_cfg_strings.h"
#include "qat_compression.h"
#include "icp_qat_fw.h"

#define SEC ADF_KERNEL_SEC

static struct service_hndl qat_compression;

void qat_compression_put_instance(struct qat_compression_instance *inst)
{
	atomic_dec(&inst->refctr);
	adf_dev_put(inst->accel_dev);
}

static int qat_compression_free_instances(struct adf_accel_dev *accel_dev)
{
	struct qat_compression_instance *inst;
	struct list_head *list_ptr, *tmp;
	int i;

	list_for_each_safe(list_ptr, tmp, &accel_dev->compression_list) {
		inst = list_entry(list_ptr,
				  struct qat_compression_instance, list);

		for (i = 0; i < atomic_read(&inst->refctr); i++)
			qat_compression_put_instance(inst);

		if (inst->dc_tx)
			adf_remove_ring(inst->dc_tx);

		if (inst->dc_rx)
			adf_remove_ring(inst->dc_rx);

		list_del(list_ptr);
		kfree(inst);
	}
	return 0;
}

struct qat_compression_instance *qat_compression_get_instance_node(int node)
{
	struct qat_compression_instance *inst = NULL;
	struct adf_accel_dev *accel_dev = NULL;
	unsigned long best = ~0;
	struct list_head *itr;

	list_for_each(itr, adf_devmgr_get_head()) {
		struct adf_accel_dev *tmp_dev;
		unsigned long ctr;
		int tmp_dev_node;

		tmp_dev = list_entry(itr, struct adf_accel_dev, list);
		tmp_dev_node = dev_to_node(&GET_DEV(tmp_dev));

		if ((node == tmp_dev_node || tmp_dev_node < 0) &&
		    adf_dev_started(tmp_dev) && !list_empty(&tmp_dev->compression_list)) {
			ctr = atomic_read(&tmp_dev->ref_count);
			if (best > ctr) {
				accel_dev = tmp_dev;
				best = ctr;
			}
		}
	}

	if (!accel_dev) {
		pr_debug_ratelimited("QAT: Could not find a device on node %d\n", node);
		/* Get any started device */
		list_for_each(itr, adf_devmgr_get_head()) {
			struct adf_accel_dev *tmp_dev;

			tmp_dev = list_entry(itr, struct adf_accel_dev, list);
			if (adf_dev_started(tmp_dev) &&
			    !list_empty(&tmp_dev->compression_list)) {
				accel_dev = tmp_dev;
				break;
			}
		}
	}

	if (!accel_dev)
		return NULL;

	best = ~0;
	list_for_each(itr, &accel_dev->compression_list) {
		struct qat_compression_instance *tmp_inst;
		unsigned long ctr;

		tmp_inst = list_entry(itr, struct qat_compression_instance, list);
		ctr = atomic_read(&tmp_inst->refctr);
		if (best > ctr) {
			inst = tmp_inst;
			best = ctr;
		}
	}
	if (inst) {
		if (adf_dev_get(accel_dev)) {
			dev_err(&GET_DEV(accel_dev), "Could not increment dev refctr\n");
			return NULL;
		}
		atomic_inc(&inst->refctr);
	}
	return inst;
}

static int qat_compression_create_instances(struct adf_accel_dev *accel_dev)
{
	struct qat_compression_instance *inst;
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];
	unsigned long num_inst, num_msg_dc;
	unsigned long bank;
	int msg_size;
	int ret;
	int i;

	INIT_LIST_HEAD(&accel_dev->compression_list);
	strscpy(key, ADF_NUM_DC, sizeof(key));
	ret = adf_cfg_get_param_value(accel_dev, SEC, key, val);
	if (ret)
		return ret;

	ret = kstrtoul(val, 10, &num_inst);
	if (ret)
		return ret;

	for (i = 0; i < num_inst; i++) {
		inst = kzalloc_node(sizeof(*inst), GFP_KERNEL,
				    dev_to_node(&GET_DEV(accel_dev)));
		if (!inst) {
			ret = -ENOMEM;
			goto err;
		}

		list_add_tail(&inst->list, &accel_dev->compression_list);
		inst->id = i;
		atomic_set(&inst->refctr, 0);
		inst->accel_dev = accel_dev;
		inst->build_deflate_ctx = GET_DC_OPS(accel_dev)->build_deflate_ctx;

		snprintf(key, sizeof(key), ADF_DC "%d" ADF_RING_DC_BANK_NUM, i);
		ret = adf_cfg_get_param_value(accel_dev, SEC, key, val);
		if (ret)
			return ret;

		ret = kstrtoul(val, 10, &bank);
		if (ret)
			return ret;

		snprintf(key, sizeof(key), ADF_DC "%d" ADF_RING_DC_SIZE, i);
		ret = adf_cfg_get_param_value(accel_dev, SEC, key, val);
		if (ret)
			return ret;

		ret = kstrtoul(val, 10, &num_msg_dc);
		if (ret)
			return ret;

		msg_size = ICP_QAT_FW_REQ_DEFAULT_SZ;
		snprintf(key, sizeof(key), ADF_DC "%d" ADF_RING_DC_TX, i);
		ret = adf_create_ring(accel_dev, SEC, bank, num_msg_dc,
				      msg_size, key, NULL, 0, &inst->dc_tx);
		if (ret)
			return ret;

		msg_size = ICP_QAT_FW_RESP_DEFAULT_SZ;
		snprintf(key, sizeof(key), ADF_DC "%d" ADF_RING_DC_RX, i);
		ret = adf_create_ring(accel_dev, SEC, bank, num_msg_dc,
				      msg_size, key, qat_comp_alg_callback, 0,
				      &inst->dc_rx);
		if (ret)
			return ret;

		inst->dc_data = accel_dev->dc_data;
		INIT_LIST_HEAD(&inst->backlog.list);
		spin_lock_init(&inst->backlog.lock);
	}
	return 0;
err:
	qat_compression_free_instances(accel_dev);
	return ret;
}

static int qat_compression_alloc_dc_data(struct adf_accel_dev *accel_dev)
{
	struct device *dev = &GET_DEV(accel_dev);
	dma_addr_t obuff_p = DMA_MAPPING_ERROR;
	size_t ovf_buff_sz = QAT_COMP_MAX_SKID;
	struct adf_dc_data *dc_data = NULL;
	u8 *obuff = NULL;

	dc_data = devm_kzalloc(dev, sizeof(*dc_data), GFP_KERNEL);
	if (!dc_data)
		goto err;

	obuff = kzalloc_node(ovf_buff_sz, GFP_KERNEL, dev_to_node(dev));
	if (!obuff)
		goto err;

	obuff_p = dma_map_single(dev, obuff, ovf_buff_sz, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(dev, obuff_p)))
		goto err;

	dc_data->ovf_buff = obuff;
	dc_data->ovf_buff_p = obuff_p;
	dc_data->ovf_buff_sz = ovf_buff_sz;

	accel_dev->dc_data = dc_data;

	return 0;

err:
	accel_dev->dc_data = NULL;
	kfree(obuff);
	devm_kfree(dev, dc_data);
	return -ENOMEM;
}

static void qat_free_dc_data(struct adf_accel_dev *accel_dev)
{
	struct adf_dc_data *dc_data = accel_dev->dc_data;
	struct device *dev = &GET_DEV(accel_dev);

	if (!dc_data)
		return;

	dma_unmap_single(dev, dc_data->ovf_buff_p, dc_data->ovf_buff_sz,
			 DMA_FROM_DEVICE);
	kfree_sensitive(dc_data->ovf_buff);
	devm_kfree(dev, dc_data);
	accel_dev->dc_data = NULL;
}

static int qat_compression_init(struct adf_accel_dev *accel_dev)
{
	int ret;

	ret = qat_compression_alloc_dc_data(accel_dev);
	if (ret)
		return ret;

	ret = qat_compression_create_instances(accel_dev);
	if (ret)
		qat_free_dc_data(accel_dev);

	return ret;
}

static int qat_compression_shutdown(struct adf_accel_dev *accel_dev)
{
	qat_free_dc_data(accel_dev);
	return qat_compression_free_instances(accel_dev);
}

static int qat_compression_event_handler(struct adf_accel_dev *accel_dev,
					 enum adf_event event)
{
	int ret;

	switch (event) {
	case ADF_EVENT_INIT:
		ret = qat_compression_init(accel_dev);
		break;
	case ADF_EVENT_SHUTDOWN:
		ret = qat_compression_shutdown(accel_dev);
		break;
	case ADF_EVENT_RESTARTING:
	case ADF_EVENT_RESTARTED:
	case ADF_EVENT_START:
	case ADF_EVENT_STOP:
	default:
		ret = 0;
	}
	return ret;
}

int qat_compression_register(void)
{
	memset(&qat_compression, 0, sizeof(qat_compression));
	qat_compression.event_hld = qat_compression_event_handler;
	qat_compression.name = "qat_compression";
	return adf_service_register(&qat_compression);
}

int qat_compression_unregister(void)
{
	return adf_service_unregister(&qat_compression);
}
