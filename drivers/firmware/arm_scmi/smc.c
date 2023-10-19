// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Message SMC/HVC
 * Transport driver
 *
 * Copyright 2020 NXP
 */

#include <linux/arm-smccc.h>
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/processor.h>
#include <linux/slab.h>

#include "common.h"

/**
 * struct scmi_smc - Structure representing a SCMI smc transport
 *
 * @cinfo: SCMI channel info
 * @shmem: Transmit/Receive shared memory area
 * @shmem_lock: Lock to protect access to Tx/Rx shared memory area.
 *		Used when NOT operating in atomic mode.
 * @inflight: Atomic flag to protect access to Tx/Rx shared memory area.
 *	      Used when operating in atomic mode.
 * @func_id: smc/hvc call function id
 */

struct scmi_smc {
	struct scmi_chan_info *cinfo;
	struct scmi_shared_mem __iomem *shmem;
	/* Protect access to shmem area */
	struct mutex shmem_lock;
#define INFLIGHT_NONE	MSG_TOKEN_MAX
	atomic_t inflight;
	u32 func_id;
};

static irqreturn_t smc_msg_done_isr(int irq, void *data)
{
	struct scmi_smc *scmi_info = data;

	scmi_rx_callback(scmi_info->cinfo,
			 shmem_read_header(scmi_info->shmem), NULL);

	return IRQ_HANDLED;
}

static bool smc_chan_available(struct device *dev, int idx)
{
	struct device_node *np = of_parse_phandle(dev->of_node, "shmem", 0);
	if (!np)
		return false;

	of_node_put(np);
	return true;
}

static inline void smc_channel_lock_init(struct scmi_smc *scmi_info)
{
	if (IS_ENABLED(CONFIG_ARM_SCMI_TRANSPORT_SMC_ATOMIC_ENABLE))
		atomic_set(&scmi_info->inflight, INFLIGHT_NONE);
	else
		mutex_init(&scmi_info->shmem_lock);
}

static bool smc_xfer_inflight(struct scmi_xfer *xfer, atomic_t *inflight)
{
	int ret;

	ret = atomic_cmpxchg(inflight, INFLIGHT_NONE, xfer->hdr.seq);

	return ret == INFLIGHT_NONE;
}

static inline void
smc_channel_lock_acquire(struct scmi_smc *scmi_info,
			 struct scmi_xfer *xfer __maybe_unused)
{
	if (IS_ENABLED(CONFIG_ARM_SCMI_TRANSPORT_SMC_ATOMIC_ENABLE))
		spin_until_cond(smc_xfer_inflight(xfer, &scmi_info->inflight));
	else
		mutex_lock(&scmi_info->shmem_lock);
}

static inline void smc_channel_lock_release(struct scmi_smc *scmi_info)
{
	if (IS_ENABLED(CONFIG_ARM_SCMI_TRANSPORT_SMC_ATOMIC_ENABLE))
		atomic_set(&scmi_info->inflight, INFLIGHT_NONE);
	else
		mutex_unlock(&scmi_info->shmem_lock);
}

static int smc_chan_setup(struct scmi_chan_info *cinfo, struct device *dev,
			  bool tx)
{
	struct device *cdev = cinfo->dev;
	struct scmi_smc *scmi_info;
	resource_size_t size;
	struct resource res;
	struct device_node *np;
	u32 func_id;
	int ret, irq;

	if (!tx)
		return -ENODEV;

	scmi_info = devm_kzalloc(dev, sizeof(*scmi_info), GFP_KERNEL);
	if (!scmi_info)
		return -ENOMEM;

	np = of_parse_phandle(cdev->of_node, "shmem", 0);
	if (!of_device_is_compatible(np, "arm,scmi-shmem"))
		return -ENXIO;

	ret = of_address_to_resource(np, 0, &res);
	of_node_put(np);
	if (ret) {
		dev_err(cdev, "failed to get SCMI Tx shared memory\n");
		return ret;
	}

	size = resource_size(&res);
	scmi_info->shmem = devm_ioremap(dev, res.start, size);
	if (!scmi_info->shmem) {
		dev_err(dev, "failed to ioremap SCMI Tx shared memory\n");
		return -EADDRNOTAVAIL;
	}

	ret = of_property_read_u32(dev->of_node, "arm,smc-id", &func_id);
	if (ret < 0)
		return ret;

	/*
	 * If there is an interrupt named "a2p", then the service and
	 * completion of a message is signaled by an interrupt rather than by
	 * the return of the SMC call.
	 */
	irq = of_irq_get_byname(cdev->of_node, "a2p");
	if (irq > 0) {
		ret = devm_request_irq(dev, irq, smc_msg_done_isr,
				       IRQF_NO_SUSPEND,
				       dev_name(dev), scmi_info);
		if (ret) {
			dev_err(dev, "failed to setup SCMI smc irq\n");
			return ret;
		}
	} else {
		cinfo->no_completion_irq = true;
	}

	scmi_info->func_id = func_id;
	scmi_info->cinfo = cinfo;
	smc_channel_lock_init(scmi_info);
	cinfo->transport_info = scmi_info;

	return 0;
}

static int smc_chan_free(int id, void *p, void *data)
{
	struct scmi_chan_info *cinfo = p;
	struct scmi_smc *scmi_info = cinfo->transport_info;

	cinfo->transport_info = NULL;
	scmi_info->cinfo = NULL;

	scmi_free_channel(cinfo, data, id);

	return 0;
}

static int smc_send_message(struct scmi_chan_info *cinfo,
			    struct scmi_xfer *xfer)
{
	struct scmi_smc *scmi_info = cinfo->transport_info;
	struct arm_smccc_res res;

	/*
	 * Channel will be released only once response has been
	 * surely fully retrieved, so after .mark_txdone()
	 */
	smc_channel_lock_acquire(scmi_info, xfer);

	shmem_tx_prepare(scmi_info->shmem, xfer, cinfo);

	arm_smccc_1_1_invoke(scmi_info->func_id, 0, 0, 0, 0, 0, 0, 0, &res);

	/* Only SMCCC_RET_NOT_SUPPORTED is valid error code */
	if (res.a0) {
		smc_channel_lock_release(scmi_info);
		return -EOPNOTSUPP;
	}

	return 0;
}

static void smc_fetch_response(struct scmi_chan_info *cinfo,
			       struct scmi_xfer *xfer)
{
	struct scmi_smc *scmi_info = cinfo->transport_info;

	shmem_fetch_response(scmi_info->shmem, xfer);
}

static void smc_mark_txdone(struct scmi_chan_info *cinfo, int ret,
			    struct scmi_xfer *__unused)
{
	struct scmi_smc *scmi_info = cinfo->transport_info;

	smc_channel_lock_release(scmi_info);
}

static const struct scmi_transport_ops scmi_smc_ops = {
	.chan_available = smc_chan_available,
	.chan_setup = smc_chan_setup,
	.chan_free = smc_chan_free,
	.send_message = smc_send_message,
	.mark_txdone = smc_mark_txdone,
	.fetch_response = smc_fetch_response,
};

const struct scmi_desc scmi_smc_desc = {
	.ops = &scmi_smc_ops,
	.max_rx_timeout_ms = 30,
	.max_msg = 20,
	.max_msg_size = 128,
	/*
	 * Setting .sync_cmds_atomic_replies to true for SMC assumes that,
	 * once the SMC instruction has completed successfully, the issued
	 * SCMI command would have been already fully processed by the SCMI
	 * platform firmware and so any possible response value expected
	 * for the issued command will be immmediately ready to be fetched
	 * from the shared memory area.
	 */
	.sync_cmds_completed_on_ret = true,
	.atomic_enabled = IS_ENABLED(CONFIG_ARM_SCMI_TRANSPORT_SMC_ATOMIC_ENABLE),
};
