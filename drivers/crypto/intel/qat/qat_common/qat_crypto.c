// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2020 Intel Corporation */
#include <linux/module.h>
#include <linux/slab.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_transport.h"
#include "adf_cfg.h"
#include "adf_cfg_strings.h"
#include "adf_gen2_hw_data.h"
#include "qat_crypto.h"
#include "icp_qat_fw.h"

#define SEC ADF_KERNEL_SEC

static struct service_hndl qat_crypto;

void qat_crypto_put_instance(struct qat_crypto_instance *inst)
{
	atomic_dec(&inst->refctr);
	adf_dev_put(inst->accel_dev);
}

static int qat_crypto_free_instances(struct adf_accel_dev *accel_dev)
{
	struct qat_crypto_instance *inst, *tmp;
	int i;

	list_for_each_entry_safe(inst, tmp, &accel_dev->crypto_list, list) {
		for (i = 0; i < atomic_read(&inst->refctr); i++)
			qat_crypto_put_instance(inst);

		if (inst->sym_tx)
			adf_remove_ring(inst->sym_tx);

		if (inst->sym_rx)
			adf_remove_ring(inst->sym_rx);

		if (inst->pke_tx)
			adf_remove_ring(inst->pke_tx);

		if (inst->pke_rx)
			adf_remove_ring(inst->pke_rx);

		list_del(&inst->list);
		kfree(inst);
	}
	return 0;
}

struct qat_crypto_instance *qat_crypto_get_instance_node(int node)
{
	struct adf_accel_dev *accel_dev = NULL, *tmp_dev;
	struct qat_crypto_instance *inst = NULL, *tmp_inst;
	unsigned long best = ~0;

	list_for_each_entry(tmp_dev, adf_devmgr_get_head(), list) {
		unsigned long ctr;

		if ((node == dev_to_node(&GET_DEV(tmp_dev)) ||
		     dev_to_node(&GET_DEV(tmp_dev)) < 0) &&
		    adf_dev_started(tmp_dev) &&
		    !list_empty(&tmp_dev->crypto_list)) {
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
		list_for_each_entry(tmp_dev, adf_devmgr_get_head(), list) {
			if (adf_dev_started(tmp_dev) &&
			    !list_empty(&tmp_dev->crypto_list)) {
				accel_dev = tmp_dev;
				break;
			}
		}
	}

	if (!accel_dev)
		return NULL;

	best = ~0;
	list_for_each_entry(tmp_inst, &accel_dev->crypto_list, list) {
		unsigned long ctr;

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

/**
 * qat_crypto_vf_dev_config()
 *     create dev config required to create crypto inst.
 *
 * @accel_dev: Pointer to acceleration device.
 *
 * Function creates device configuration required to create
 * asym, sym or, crypto instances
 *
 * Return: 0 on success, error code otherwise.
 */
int qat_crypto_vf_dev_config(struct adf_accel_dev *accel_dev)
{
	u16 ring_to_svc_map = GET_HW_DATA(accel_dev)->ring_to_svc_map;

	if (ring_to_svc_map != ADF_GEN2_DEFAULT_RING_TO_SRV_MAP) {
		dev_err(&GET_DEV(accel_dev),
			"Unsupported ring/service mapping present on PF");
		return -EFAULT;
	}

	return GET_HW_DATA(accel_dev)->dev_config(accel_dev);
}

static int qat_crypto_create_instances(struct adf_accel_dev *accel_dev)
{
	unsigned long num_inst, num_msg_sym, num_msg_asym;
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];
	unsigned long sym_bank, asym_bank;
	struct qat_crypto_instance *inst;
	int msg_size;
	int ret;
	int i;

	INIT_LIST_HEAD(&accel_dev->crypto_list);
	ret = adf_cfg_get_param_value(accel_dev, SEC, ADF_NUM_CY, val);
	if (ret)
		return ret;

	ret = kstrtoul(val, 0, &num_inst);
	if (ret)
		return ret;

	for (i = 0; i < num_inst; i++) {
		inst = kzalloc_node(sizeof(*inst), GFP_KERNEL,
				    dev_to_node(&GET_DEV(accel_dev)));
		if (!inst) {
			ret = -ENOMEM;
			goto err;
		}

		list_add_tail(&inst->list, &accel_dev->crypto_list);
		inst->id = i;
		atomic_set(&inst->refctr, 0);
		inst->accel_dev = accel_dev;

		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_BANK_NUM, i);
		ret = adf_cfg_get_param_value(accel_dev, SEC, key, val);
		if (ret)
			goto err;

		ret = kstrtoul(val, 10, &sym_bank);
		if (ret)
			goto err;

		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_BANK_NUM, i);
		ret = adf_cfg_get_param_value(accel_dev, SEC, key, val);
		if (ret)
			goto err;

		ret = kstrtoul(val, 10, &asym_bank);
		if (ret)
			goto err;

		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_SIZE, i);
		ret = adf_cfg_get_param_value(accel_dev, SEC, key, val);
		if (ret)
			goto err;

		ret = kstrtoul(val, 10, &num_msg_sym);
		if (ret)
			goto err;

		num_msg_sym = num_msg_sym >> 1;

		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_SIZE, i);
		ret = adf_cfg_get_param_value(accel_dev, SEC, key, val);
		if (ret)
			goto err;

		ret = kstrtoul(val, 10, &num_msg_asym);
		if (ret)
			goto err;
		num_msg_asym = num_msg_asym >> 1;

		msg_size = ICP_QAT_FW_REQ_DEFAULT_SZ;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_TX, i);
		ret = adf_create_ring(accel_dev, SEC, sym_bank, num_msg_sym,
				      msg_size, key, NULL, 0, &inst->sym_tx);
		if (ret)
			goto err;

		msg_size = msg_size >> 1;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_TX, i);
		ret = adf_create_ring(accel_dev, SEC, asym_bank, num_msg_asym,
				      msg_size, key, NULL, 0, &inst->pke_tx);
		if (ret)
			goto err;

		msg_size = ICP_QAT_FW_RESP_DEFAULT_SZ;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_RX, i);
		ret = adf_create_ring(accel_dev, SEC, sym_bank, num_msg_sym,
				      msg_size, key, qat_alg_callback, 0,
				      &inst->sym_rx);
		if (ret)
			goto err;

		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_RX, i);
		ret = adf_create_ring(accel_dev, SEC, asym_bank, num_msg_asym,
				      msg_size, key, qat_alg_asym_callback, 0,
				      &inst->pke_rx);
		if (ret)
			goto err;

		INIT_LIST_HEAD(&inst->backlog.list);
		spin_lock_init(&inst->backlog.lock);
	}
	return 0;
err:
	qat_crypto_free_instances(accel_dev);
	return ret;
}

static int qat_crypto_init(struct adf_accel_dev *accel_dev)
{
	if (qat_crypto_create_instances(accel_dev))
		return -EFAULT;

	return 0;
}

static int qat_crypto_shutdown(struct adf_accel_dev *accel_dev)
{
	return qat_crypto_free_instances(accel_dev);
}

static int qat_crypto_event_handler(struct adf_accel_dev *accel_dev,
				    enum adf_event event)
{
	int ret;

	switch (event) {
	case ADF_EVENT_INIT:
		ret = qat_crypto_init(accel_dev);
		break;
	case ADF_EVENT_SHUTDOWN:
		ret = qat_crypto_shutdown(accel_dev);
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

int qat_crypto_register(void)
{
	memset(&qat_crypto, 0, sizeof(qat_crypto));
	qat_crypto.event_hld = qat_crypto_event_handler;
	qat_crypto.name = "qat_crypto";
	return adf_service_register(&qat_crypto);
}

int qat_crypto_unregister(void)
{
	return adf_service_unregister(&qat_crypto);
}
