/*
  This file is provided under a dual BSD/GPLv2 license.  When using or
  redistributing this file, you may do so under either license.

  GPL LICENSE SUMMARY
  Copyright(c) 2014 Intel Corporation.
  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  Contact Information:
  qat-linux@intel.com

  BSD LICENSE
  Copyright(c) 2014 Intel Corporation.
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <linux/module.h>
#include <linux/slab.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_transport.h"
#include "adf_cfg.h"
#include "adf_cfg_strings.h"
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
	struct qat_crypto_instance *inst;
	struct list_head *list_ptr, *tmp;
	int i;

	list_for_each_safe(list_ptr, tmp, &accel_dev->crypto_list) {
		inst = list_entry(list_ptr, struct qat_crypto_instance, list);

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

		list_del(list_ptr);
		kfree(inst);
	}
	return 0;
}

struct qat_crypto_instance *qat_crypto_get_instance_node(int node)
{
	struct adf_accel_dev *accel_dev = NULL;
	struct qat_crypto_instance *inst = NULL;
	struct list_head *itr;
	unsigned long best = ~0;

	list_for_each(itr, adf_devmgr_get_head()) {
		struct adf_accel_dev *tmp_dev;
		unsigned long ctr;

		tmp_dev = list_entry(itr, struct adf_accel_dev, list);

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
	if (!accel_dev)
		pr_info("QAT: Could not find a device on node %d\n", node);

	/* Get any started device */
	list_for_each(itr, adf_devmgr_get_head()) {
		struct adf_accel_dev *tmp_dev;

		tmp_dev = list_entry(itr, struct adf_accel_dev, list);

		if (adf_dev_started(tmp_dev) &&
		    !list_empty(&tmp_dev->crypto_list)) {
			accel_dev = tmp_dev;
			break;
		}
	}

	if (!accel_dev)
		return NULL;

	best = ~0;
	list_for_each(itr, &accel_dev->crypto_list) {
		struct qat_crypto_instance *tmp_inst;
		unsigned long ctr;

		tmp_inst = list_entry(itr, struct qat_crypto_instance, list);
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

static int qat_crypto_create_instances(struct adf_accel_dev *accel_dev)
{
	int i;
	unsigned long bank;
	unsigned long num_inst, num_msg_sym, num_msg_asym;
	int msg_size;
	struct qat_crypto_instance *inst;
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];

	INIT_LIST_HEAD(&accel_dev->crypto_list);
	strlcpy(key, ADF_NUM_CY, sizeof(key));
	if (adf_cfg_get_param_value(accel_dev, SEC, key, val))
		return -EFAULT;

	if (kstrtoul(val, 0, &num_inst))
		return -EFAULT;

	for (i = 0; i < num_inst; i++) {
		inst = kzalloc_node(sizeof(*inst), GFP_KERNEL,
				    dev_to_node(&GET_DEV(accel_dev)));
		if (!inst)
			goto err;

		list_add_tail(&inst->list, &accel_dev->crypto_list);
		inst->id = i;
		atomic_set(&inst->refctr, 0);
		inst->accel_dev = accel_dev;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_BANK_NUM, i);
		if (adf_cfg_get_param_value(accel_dev, SEC, key, val))
			goto err;

		if (kstrtoul(val, 10, &bank))
			goto err;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_SIZE, i);
		if (adf_cfg_get_param_value(accel_dev, SEC, key, val))
			goto err;

		if (kstrtoul(val, 10, &num_msg_sym))
			goto err;

		num_msg_sym = num_msg_sym >> 1;

		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_SIZE, i);
		if (adf_cfg_get_param_value(accel_dev, SEC, key, val))
			goto err;

		if (kstrtoul(val, 10, &num_msg_asym))
			goto err;
		num_msg_asym = num_msg_asym >> 1;

		msg_size = ICP_QAT_FW_REQ_DEFAULT_SZ;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_TX, i);
		if (adf_create_ring(accel_dev, SEC, bank, num_msg_sym,
				    msg_size, key, NULL, 0, &inst->sym_tx))
			goto err;

		msg_size = msg_size >> 1;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_TX, i);
		if (adf_create_ring(accel_dev, SEC, bank, num_msg_asym,
				    msg_size, key, NULL, 0, &inst->pke_tx))
			goto err;

		msg_size = ICP_QAT_FW_RESP_DEFAULT_SZ;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_RX, i);
		if (adf_create_ring(accel_dev, SEC, bank, num_msg_sym,
				    msg_size, key, qat_alg_callback, 0,
				    &inst->sym_rx))
			goto err;

		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_RX, i);
		if (adf_create_ring(accel_dev, SEC, bank, num_msg_asym,
				    msg_size, key, qat_alg_asym_callback, 0,
				    &inst->pke_rx))
			goto err;
	}
	return 0;
err:
	qat_crypto_free_instances(accel_dev);
	return -ENOMEM;
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
