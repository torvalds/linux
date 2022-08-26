// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019-2022 Marvell International Ltd. All rights reserved */

#include <linux/kernel.h>
#include <linux/list.h>

#include "prestera.h"
#include "prestera_hw.h"
#include "prestera_flow.h"
#include "prestera_flower.h"
#include "prestera_matchall.h"
#include "prestera_span.h"

static int prestera_mall_prio_check(struct prestera_flow_block *block,
				    struct tc_cls_matchall_offload *f)
{
	u32 flower_prio_min;
	u32 flower_prio_max;
	int err;

	err = prestera_flower_prio_get(block, f->common.chain_index,
				       &flower_prio_min, &flower_prio_max);
	if (err == -ENOENT)
		/* No flower filters installed on this chain. */
		return 0;

	if (err) {
		NL_SET_ERR_MSG(f->common.extack, "Failed to get flower priorities");
		return err;
	}

	if (f->common.prio <= flower_prio_max && !block->ingress) {
		NL_SET_ERR_MSG(f->common.extack, "Failed to add in front of existing flower rules");
		return -EOPNOTSUPP;
	}
	if (f->common.prio >= flower_prio_min && block->ingress) {
		NL_SET_ERR_MSG(f->common.extack, "Failed to add behind of existing flower rules");
		return -EOPNOTSUPP;
	}

	return 0;
}

int prestera_mall_prio_get(struct prestera_flow_block *block,
			   u32 *prio_min, u32 *prio_max)
{
	if (!block->mall.bound)
		return -ENOENT;

	*prio_min = block->mall.prio_min;
	*prio_max = block->mall.prio_max;
	return 0;
}

static void prestera_mall_prio_update(struct prestera_flow_block *block,
				      struct tc_cls_matchall_offload *f)
{
	block->mall.prio_min = min(block->mall.prio_min, f->common.prio);
	block->mall.prio_max = max(block->mall.prio_max, f->common.prio);
}

int prestera_mall_replace(struct prestera_flow_block *block,
			  struct tc_cls_matchall_offload *f)
{
	struct prestera_flow_block_binding *binding;
	__be16 protocol = f->common.protocol;
	struct flow_action_entry *act;
	struct prestera_port *port;
	int err;

	if (!flow_offload_has_one_action(&f->rule->action)) {
		NL_SET_ERR_MSG(f->common.extack,
			       "Only singular actions are supported");
		return -EOPNOTSUPP;
	}

	act = &f->rule->action.entries[0];

	if (!prestera_netdev_check(act->dev)) {
		NL_SET_ERR_MSG(f->common.extack,
			       "Only Marvell Prestera port is supported");
		return -EINVAL;
	}
	if (!tc_cls_can_offload_and_chain0(act->dev, &f->common))
		return -EOPNOTSUPP;
	if (act->id != FLOW_ACTION_MIRRED)
		return -EOPNOTSUPP;
	if (protocol != htons(ETH_P_ALL))
		return -EOPNOTSUPP;

	err = prestera_mall_prio_check(block, f);
	if (err)
		return err;

	port = netdev_priv(act->dev);

	list_for_each_entry(binding, &block->binding_list, list) {
		err = prestera_span_rule_add(binding, port, block->ingress);
		if (err)
			goto rollback;
	}

	prestera_mall_prio_update(block, f);

	block->mall.bound = true;
	return 0;

rollback:
	list_for_each_entry_continue_reverse(binding,
					     &block->binding_list, list)
		prestera_span_rule_del(binding, block->ingress);
	return err;
}

void prestera_mall_destroy(struct prestera_flow_block *block)
{
	struct prestera_flow_block_binding *binding;

	list_for_each_entry(binding, &block->binding_list, list)
		prestera_span_rule_del(binding, block->ingress);

	block->mall.prio_min = UINT_MAX;
	block->mall.prio_max = 0;
	block->mall.bound = false;
}
