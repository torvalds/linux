// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2024 Intel Corporation. All rights reserved. */
#include <linux/mm.h>
#include <linux/notifier.h>
#include <linux/set_memory.h>
#include <asm/mce.h>
#include <cxl.h>
#include "core.h"
#include "mce.h"

static int cxl_handle_mce(struct notifier_block *nb, unsigned long val,
			  void *data)
{
	struct cxl_region *cxlr = container_of(nb, struct cxl_region,
					       mce_notifier);
	struct cxl_region_params *p = &cxlr->params;
	struct mce *mce = data;
	u64 spa, spa_alias;
	unsigned long pfn;

	if (!mce || !mce_usable_address(mce))
		return NOTIFY_DONE;

	spa = mce->addr & MCI_ADDR_PHYSADDR;

	if (!cxl_resource_contains_addr(p->res, spa))
		return NOTIFY_DONE;

	if (spa >= p->res->start + p->cache_size)
		spa_alias = spa - p->cache_size;
	else
		spa_alias = spa + p->cache_size;

	pfn = spa_alias >> PAGE_SHIFT;
	if (!pfn_valid(pfn))
		return NOTIFY_DONE;

	/*
	 * Take down the aliased memory page. The original memory page flagged
	 * by the MCE will be taken cared of by the standard MCE handler.
	 */
	dev_emerg(&cxlr->dev, "Offlining aliased SPA address0: %#llx\n",
		  spa_alias);
	if (!memory_failure(pfn, 0))
		set_mce_nospec(pfn);

	return NOTIFY_OK;
}

static void cxl_unregister_mce_notifier(void *mce_notifier)
{
	mce_unregister_decode_chain(mce_notifier);
}

int devm_cxl_register_mce_notifier(struct device *dev,
				   struct notifier_block *mce_notifier)
{
	mce_notifier->notifier_call = cxl_handle_mce;
	mce_notifier->priority = MCE_PRIO_UC;
	mce_register_decode_chain(mce_notifier);

	return devm_add_action_or_reset(dev, cxl_unregister_mce_notifier,
					mce_notifier);
}
