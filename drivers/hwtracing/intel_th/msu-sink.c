// SPDX-License-Identifier: GPL-2.0
/*
 * An example software sink buffer for Intel TH MSU.
 *
 * Copyright (C) 2019 Intel Corporation.
 */

#include <linux/intel_th.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>

#define MAX_SGTS 16

struct msu_sink_private {
	struct device	*dev;
	struct sg_table **sgts;
	unsigned int	nr_sgts;
};

static void *msu_sink_assign(struct device *dev, int *mode)
{
	struct msu_sink_private *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return NULL;

	priv->sgts = kcalloc(MAX_SGTS, sizeof(void *), GFP_KERNEL);
	if (!priv->sgts) {
		kfree(priv);
		return NULL;
	}

	priv->dev = dev;
	*mode = MSC_MODE_MULTI;

	return priv;
}

static void msu_sink_unassign(void *data)
{
	struct msu_sink_private *priv = data;

	kfree(priv->sgts);
	kfree(priv);
}

/* See also: msc.c: __msc_buffer_win_alloc() */
static int msu_sink_alloc_window(void *data, struct sg_table **sgt, size_t size)
{
	struct msu_sink_private *priv = data;
	unsigned int nents;
	struct scatterlist *sg_ptr;
	void *block;
	int ret, i;

	if (priv->nr_sgts == MAX_SGTS)
		return -ENOMEM;

	nents = DIV_ROUND_UP(size, PAGE_SIZE);

	ret = sg_alloc_table(*sgt, nents, GFP_KERNEL);
	if (ret)
		return -ENOMEM;

	priv->sgts[priv->nr_sgts++] = *sgt;

	for_each_sg((*sgt)->sgl, sg_ptr, nents, i) {
		block = dma_alloc_coherent(priv->dev->parent->parent,
					   PAGE_SIZE, &sg_dma_address(sg_ptr),
					   GFP_KERNEL);
		if (!block)
			return -ENOMEM;

		sg_set_buf(sg_ptr, block, PAGE_SIZE);
	}

	return nents;
}

/* See also: msc.c: __msc_buffer_win_free() */
static void msu_sink_free_window(void *data, struct sg_table *sgt)
{
	struct msu_sink_private *priv = data;
	struct scatterlist *sg_ptr;
	int i;

	for_each_sg(sgt->sgl, sg_ptr, sgt->nents, i) {
		dma_free_coherent(priv->dev->parent->parent, PAGE_SIZE,
				  sg_virt(sg_ptr), sg_dma_address(sg_ptr));
	}

	sg_free_table(sgt);
	priv->nr_sgts--;
}

static int msu_sink_ready(void *data, struct sg_table *sgt, size_t bytes)
{
	struct msu_sink_private *priv = data;

	intel_th_msc_window_unlock(priv->dev, sgt);

	return 0;
}

static const struct msu_buffer sink_mbuf = {
	.name		= "sink",
	.assign		= msu_sink_assign,
	.unassign	= msu_sink_unassign,
	.alloc_window	= msu_sink_alloc_window,
	.free_window	= msu_sink_free_window,
	.ready		= msu_sink_ready,
};

module_intel_th_msu_buffer(sink_mbuf);

MODULE_DESCRIPTION("example software sink buffer for Intel TH MSU");
MODULE_LICENSE("GPL v2");
