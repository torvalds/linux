/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Texas Instruments TI-SCI Processor Controller Helper Functions
 *
 * Copyright (C) 2018-2020 Texas Instruments Incorporated - https://www.ti.com/
 *	Suman Anna <s-anna@ti.com>
 */

#ifndef REMOTEPROC_TI_SCI_PROC_H
#define REMOTEPROC_TI_SCI_PROC_H

#include <linux/soc/ti/ti_sci_protocol.h>

/**
 * struct ti_sci_proc - structure representing a processor control client
 * @sci: cached TI-SCI protocol handle
 * @ops: cached TI-SCI proc ops
 * @dev: cached client device pointer
 * @proc_id: processor id for the consumer remoteproc device
 * @host_id: host id to pass the control over for this consumer remoteproc
 *	     device
 */
struct ti_sci_proc {
	const struct ti_sci_handle *sci;
	const struct ti_sci_proc_ops *ops;
	struct device *dev;
	u8 proc_id;
	u8 host_id;
};

static inline
struct ti_sci_proc *ti_sci_proc_of_get_tsp(struct device *dev,
					   const struct ti_sci_handle *sci)
{
	struct ti_sci_proc *tsp;
	u32 temp[2];
	int ret;

	ret = of_property_read_u32_array(dev_of_node(dev), "ti,sci-proc-ids",
					 temp, 2);
	if (ret < 0)
		return ERR_PTR(ret);

	tsp = devm_kzalloc(dev, sizeof(*tsp), GFP_KERNEL);
	if (!tsp)
		return ERR_PTR(-ENOMEM);

	tsp->dev = dev;
	tsp->sci = sci;
	tsp->ops = &sci->ops.proc_ops;
	tsp->proc_id = temp[0];
	tsp->host_id = temp[1];

	return tsp;
}

static inline int ti_sci_proc_request(struct ti_sci_proc *tsp)
{
	int ret;

	ret = tsp->ops->request(tsp->sci, tsp->proc_id);
	if (ret)
		dev_err(tsp->dev, "ti-sci processor request failed: %d\n",
			ret);
	return ret;
}

static inline int ti_sci_proc_release(struct ti_sci_proc *tsp)
{
	int ret;

	ret = tsp->ops->release(tsp->sci, tsp->proc_id);
	if (ret)
		dev_err(tsp->dev, "ti-sci processor release failed: %d\n",
			ret);
	return ret;
}

static inline int ti_sci_proc_handover(struct ti_sci_proc *tsp)
{
	int ret;

	ret = tsp->ops->handover(tsp->sci, tsp->proc_id, tsp->host_id);
	if (ret)
		dev_err(tsp->dev, "ti-sci processor handover of %d to %d failed: %d\n",
			tsp->proc_id, tsp->host_id, ret);
	return ret;
}

static inline int ti_sci_proc_set_config(struct ti_sci_proc *tsp,
					 u64 boot_vector,
					 u32 cfg_set, u32 cfg_clr)
{
	int ret;

	ret = tsp->ops->set_config(tsp->sci, tsp->proc_id, boot_vector,
				   cfg_set, cfg_clr);
	if (ret)
		dev_err(tsp->dev, "ti-sci processor set_config failed: %d\n",
			ret);
	return ret;
}

static inline int ti_sci_proc_set_control(struct ti_sci_proc *tsp,
					  u32 ctrl_set, u32 ctrl_clr)
{
	int ret;

	ret = tsp->ops->set_control(tsp->sci, tsp->proc_id, ctrl_set, ctrl_clr);
	if (ret)
		dev_err(tsp->dev, "ti-sci processor set_control failed: %d\n",
			ret);
	return ret;
}

static inline int ti_sci_proc_get_status(struct ti_sci_proc *tsp,
					 u64 *boot_vector, u32 *cfg_flags,
					 u32 *ctrl_flags, u32 *status_flags)
{
	int ret;

	ret = tsp->ops->get_status(tsp->sci, tsp->proc_id, boot_vector,
				   cfg_flags, ctrl_flags, status_flags);
	if (ret)
		dev_err(tsp->dev, "ti-sci processor get_status failed: %d\n",
			ret);
	return ret;
}

#endif /* REMOTEPROC_TI_SCI_PROC_H */
