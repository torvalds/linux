// SPDX-License-Identifier: GPL-2.0
/* ICSSG Buffer queue helpers
 *
 * Copyright (C) 2021 Texas Instruments Incorporated - https://www.ti.com
 */

#include <linux/regmap.h>
#include "icssg_prueth.h"

#define ICSSG_QUEUES_MAX		64
#define ICSSG_QUEUE_OFFSET		0xd00
#define ICSSG_QUEUE_PEEK_OFFSET		0xe00
#define ICSSG_QUEUE_CNT_OFFSET		0xe40
#define	ICSSG_QUEUE_RESET_OFFSET	0xf40

int icssg_queue_pop(struct prueth *prueth, u8 queue)
{
	u32 val, cnt;

	if (queue >= ICSSG_QUEUES_MAX)
		return -EINVAL;

	regmap_read(prueth->miig_rt, ICSSG_QUEUE_CNT_OFFSET + 4 * queue, &cnt);
	if (!cnt)
		return -EINVAL;

	regmap_read(prueth->miig_rt, ICSSG_QUEUE_OFFSET + 4 * queue, &val);

	return val;
}
EXPORT_SYMBOL_GPL(icssg_queue_pop);

void icssg_queue_push(struct prueth *prueth, int queue, u16 addr)
{
	if (queue >= ICSSG_QUEUES_MAX)
		return;

	regmap_write(prueth->miig_rt, ICSSG_QUEUE_OFFSET + 4 * queue, addr);
}
EXPORT_SYMBOL_GPL(icssg_queue_push);

u32 icssg_queue_level(struct prueth *prueth, int queue)
{
	u32 reg;

	if (queue >= ICSSG_QUEUES_MAX)
		return 0;

	regmap_read(prueth->miig_rt, ICSSG_QUEUE_CNT_OFFSET + 4 * queue, &reg);

	return reg;
}
