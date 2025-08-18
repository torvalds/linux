// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

/* PPE HW initialization configs such as BM(buffer management),
 * QM(queue management) and scheduler configs.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/regmap.h>

#include "ppe.h"
#include "ppe_config.h"
#include "ppe_regs.h"

/**
 * struct ppe_bm_port_config - PPE BM port configuration.
 * @port_id_start: The fist BM port ID to configure.
 * @port_id_end: The last BM port ID to configure.
 * @pre_alloc: BM port dedicated buffer number.
 * @in_fly_buf: Buffer number for receiving the packet after pause frame sent.
 * @ceil: Ceil to generate the back pressure.
 * @weight: Weight value.
 * @resume_offset: Resume offset from the threshold value.
 * @resume_ceil: Ceil to resume from the back pressure state.
 * @dynamic: Dynamic threshold used or not.
 *
 * The is for configuring the threshold that impacts the port
 * flow control.
 */
struct ppe_bm_port_config {
	unsigned int port_id_start;
	unsigned int port_id_end;
	unsigned int pre_alloc;
	unsigned int in_fly_buf;
	unsigned int ceil;
	unsigned int weight;
	unsigned int resume_offset;
	unsigned int resume_ceil;
	bool dynamic;
};

/**
 * struct ppe_qm_queue_config - PPE queue config.
 * @queue_start: PPE start of queue ID.
 * @queue_end: PPE end of queue ID.
 * @prealloc_buf: Queue dedicated buffer number.
 * @ceil: Ceil to start drop packet from queue.
 * @weight: Weight value.
 * @resume_offset: Resume offset from the threshold.
 * @dynamic: Threshold value is decided dynamically or statically.
 *
 * Queue configuration decides the threshold to drop packet from PPE
 * hardware queue.
 */
struct ppe_qm_queue_config {
	unsigned int queue_start;
	unsigned int queue_end;
	unsigned int prealloc_buf;
	unsigned int ceil;
	unsigned int weight;
	unsigned int resume_offset;
	bool dynamic;
};

/* There are total 2048 buffers available in PPE, out of which some
 * buffers are reserved for some specific purposes per PPE port. The
 * rest of the pool of 1550 buffers are assigned to the general 'group0'
 * which is shared among all ports of the PPE.
 */
static const int ipq9574_ppe_bm_group_config = 1550;

/* The buffer configurations per PPE port. There are 15 BM ports and
 * 4 BM groups supported by PPE. BM port (0-7) is for EDMA port 0,
 * BM port (8-13) is for PPE physical port 1-6 and BM port 14 is for
 * EIP port.
 */
static const struct ppe_bm_port_config ipq9574_ppe_bm_port_config[] = {
	{
		/* Buffer configuration for the BM port ID 0 of EDMA. */
		.port_id_start	= 0,
		.port_id_end	= 0,
		.pre_alloc	= 0,
		.in_fly_buf	= 100,
		.ceil		= 1146,
		.weight		= 7,
		.resume_offset	= 8,
		.resume_ceil	= 0,
		.dynamic	= true,
	},
	{
		/* Buffer configuration for the BM port ID 1-7 of EDMA. */
		.port_id_start	= 1,
		.port_id_end	= 7,
		.pre_alloc	= 0,
		.in_fly_buf	= 100,
		.ceil		= 250,
		.weight		= 4,
		.resume_offset	= 36,
		.resume_ceil	= 0,
		.dynamic	= true,
	},
	{
		/* Buffer configuration for the BM port ID 8-13 of PPE ports. */
		.port_id_start	= 8,
		.port_id_end	= 13,
		.pre_alloc	= 0,
		.in_fly_buf	= 128,
		.ceil		= 250,
		.weight		= 4,
		.resume_offset	= 36,
		.resume_ceil	= 0,
		.dynamic	= true,
	},
	{
		/* Buffer configuration for the BM port ID 14 of EIP. */
		.port_id_start	= 14,
		.port_id_end	= 14,
		.pre_alloc	= 0,
		.in_fly_buf	= 40,
		.ceil		= 250,
		.weight		= 4,
		.resume_offset	= 36,
		.resume_ceil	= 0,
		.dynamic	= true,
	},
};

/* QM fetches the packet from PPE buffer management for transmitting the
 * packet out. The QM group configuration limits the total number of buffers
 * enqueued by all PPE hardware queues.
 * There are total 2048 buffers available, out of which some buffers are
 * dedicated to hardware exception handlers. The remaining buffers are
 * assigned to the general 'group0', which is the group assigned to all
 * queues by default.
 */
static const int ipq9574_ppe_qm_group_config = 2000;

/* Default QM settings for unicast and multicast queues for IPQ9754. */
static const struct ppe_qm_queue_config ipq9574_ppe_qm_queue_config[] = {
	{
		/* QM settings for unicast queues 0 to 255. */
		.queue_start	= 0,
		.queue_end	= 255,
		.prealloc_buf	= 0,
		.ceil		= 1200,
		.weight		= 7,
		.resume_offset	= 36,
		.dynamic	= true,
	},
	{
		/* QM settings for multicast queues 256 to 299. */
		.queue_start	= 256,
		.queue_end	= 299,
		.prealloc_buf	= 0,
		.ceil		= 250,
		.weight		= 0,
		.resume_offset	= 36,
		.dynamic	= false,
	},
};

static int ppe_config_bm_threshold(struct ppe_device *ppe_dev, int bm_port_id,
				   const struct ppe_bm_port_config port_cfg)
{
	u32 reg, val, bm_fc_val[2];
	int ret;

	reg = PPE_BM_PORT_FC_CFG_TBL_ADDR + PPE_BM_PORT_FC_CFG_TBL_INC * bm_port_id;
	ret = regmap_bulk_read(ppe_dev->regmap, reg,
			       bm_fc_val, ARRAY_SIZE(bm_fc_val));
	if (ret)
		return ret;

	/* Configure BM flow control related threshold. */
	PPE_BM_PORT_FC_SET_WEIGHT(bm_fc_val, port_cfg.weight);
	PPE_BM_PORT_FC_SET_RESUME_OFFSET(bm_fc_val, port_cfg.resume_offset);
	PPE_BM_PORT_FC_SET_RESUME_THRESHOLD(bm_fc_val, port_cfg.resume_ceil);
	PPE_BM_PORT_FC_SET_DYNAMIC(bm_fc_val, port_cfg.dynamic);
	PPE_BM_PORT_FC_SET_REACT_LIMIT(bm_fc_val, port_cfg.in_fly_buf);
	PPE_BM_PORT_FC_SET_PRE_ALLOC(bm_fc_val, port_cfg.pre_alloc);

	/* Configure low/high bits of the ceiling for the BM port. */
	val = FIELD_GET(GENMASK(2, 0), port_cfg.ceil);
	PPE_BM_PORT_FC_SET_CEILING_LOW(bm_fc_val, val);
	val = FIELD_GET(GENMASK(10, 3), port_cfg.ceil);
	PPE_BM_PORT_FC_SET_CEILING_HIGH(bm_fc_val, val);

	ret = regmap_bulk_write(ppe_dev->regmap, reg,
				bm_fc_val, ARRAY_SIZE(bm_fc_val));
	if (ret)
		return ret;

	/* Assign the default group ID 0 to the BM port. */
	val = FIELD_PREP(PPE_BM_PORT_GROUP_ID_SHARED_GROUP_ID, 0);
	reg = PPE_BM_PORT_GROUP_ID_ADDR + PPE_BM_PORT_GROUP_ID_INC * bm_port_id;
	ret = regmap_update_bits(ppe_dev->regmap, reg,
				 PPE_BM_PORT_GROUP_ID_SHARED_GROUP_ID,
				 val);
	if (ret)
		return ret;

	/* Enable BM port flow control. */
	reg = PPE_BM_PORT_FC_MODE_ADDR + PPE_BM_PORT_FC_MODE_INC * bm_port_id;

	return regmap_set_bits(ppe_dev->regmap, reg, PPE_BM_PORT_FC_MODE_EN);
}

/* Configure the buffer threshold for the port flow control function. */
static int ppe_config_bm(struct ppe_device *ppe_dev)
{
	const struct ppe_bm_port_config *port_cfg;
	unsigned int i, bm_port_id, port_cfg_cnt;
	u32 reg, val;
	int ret;

	/* Configure the allocated buffer number only for group 0.
	 * The buffer number of group 1-3 is already cleared to 0
	 * after PPE reset during the probe of PPE driver.
	 */
	reg = PPE_BM_SHARED_GROUP_CFG_ADDR;
	val = FIELD_PREP(PPE_BM_SHARED_GROUP_CFG_SHARED_LIMIT,
			 ipq9574_ppe_bm_group_config);
	ret = regmap_update_bits(ppe_dev->regmap, reg,
				 PPE_BM_SHARED_GROUP_CFG_SHARED_LIMIT,
				 val);
	if (ret)
		goto bm_config_fail;

	/* Configure buffer thresholds for the BM ports. */
	port_cfg = ipq9574_ppe_bm_port_config;
	port_cfg_cnt = ARRAY_SIZE(ipq9574_ppe_bm_port_config);
	for (i = 0; i < port_cfg_cnt; i++) {
		for (bm_port_id = port_cfg[i].port_id_start;
		     bm_port_id <= port_cfg[i].port_id_end; bm_port_id++) {
			ret = ppe_config_bm_threshold(ppe_dev, bm_port_id,
						      port_cfg[i]);
			if (ret)
				goto bm_config_fail;
		}
	}

	return 0;

bm_config_fail:
	dev_err(ppe_dev->dev, "PPE BM config error %d\n", ret);
	return ret;
}

/* Configure PPE hardware queue depth, which is decided by the threshold
 * of queue.
 */
static int ppe_config_qm(struct ppe_device *ppe_dev)
{
	const struct ppe_qm_queue_config *queue_cfg;
	int ret, i, queue_id, queue_cfg_count;
	u32 reg, multicast_queue_cfg[5];
	u32 unicast_queue_cfg[4];
	u32 group_cfg[3];

	/* Assign the buffer number to the group 0 by default. */
	reg = PPE_AC_GRP_CFG_TBL_ADDR;
	ret = regmap_bulk_read(ppe_dev->regmap, reg,
			       group_cfg, ARRAY_SIZE(group_cfg));
	if (ret)
		goto qm_config_fail;

	PPE_AC_GRP_SET_BUF_LIMIT(group_cfg, ipq9574_ppe_qm_group_config);

	ret = regmap_bulk_write(ppe_dev->regmap, reg,
				group_cfg, ARRAY_SIZE(group_cfg));
	if (ret)
		goto qm_config_fail;

	queue_cfg = ipq9574_ppe_qm_queue_config;
	queue_cfg_count = ARRAY_SIZE(ipq9574_ppe_qm_queue_config);
	for (i = 0; i < queue_cfg_count; i++) {
		queue_id = queue_cfg[i].queue_start;

		/* Configure threshold for dropping packets separately for
		 * unicast and multicast PPE queues.
		 */
		while (queue_id <= queue_cfg[i].queue_end) {
			if (queue_id < PPE_AC_UNICAST_QUEUE_CFG_TBL_ENTRIES) {
				reg = PPE_AC_UNICAST_QUEUE_CFG_TBL_ADDR +
				      PPE_AC_UNICAST_QUEUE_CFG_TBL_INC * queue_id;

				ret = regmap_bulk_read(ppe_dev->regmap, reg,
						       unicast_queue_cfg,
						       ARRAY_SIZE(unicast_queue_cfg));
				if (ret)
					goto qm_config_fail;

				PPE_AC_UNICAST_QUEUE_SET_EN(unicast_queue_cfg, true);
				PPE_AC_UNICAST_QUEUE_SET_GRP_ID(unicast_queue_cfg, 0);
				PPE_AC_UNICAST_QUEUE_SET_PRE_LIMIT(unicast_queue_cfg,
								   queue_cfg[i].prealloc_buf);
				PPE_AC_UNICAST_QUEUE_SET_DYNAMIC(unicast_queue_cfg,
								 queue_cfg[i].dynamic);
				PPE_AC_UNICAST_QUEUE_SET_WEIGHT(unicast_queue_cfg,
								queue_cfg[i].weight);
				PPE_AC_UNICAST_QUEUE_SET_THRESHOLD(unicast_queue_cfg,
								   queue_cfg[i].ceil);
				PPE_AC_UNICAST_QUEUE_SET_GRN_RESUME(unicast_queue_cfg,
								    queue_cfg[i].resume_offset);

				ret = regmap_bulk_write(ppe_dev->regmap, reg,
							unicast_queue_cfg,
							ARRAY_SIZE(unicast_queue_cfg));
				if (ret)
					goto qm_config_fail;
			} else {
				reg = PPE_AC_MULTICAST_QUEUE_CFG_TBL_ADDR +
				      PPE_AC_MULTICAST_QUEUE_CFG_TBL_INC * queue_id;

				ret = regmap_bulk_read(ppe_dev->regmap, reg,
						       multicast_queue_cfg,
						       ARRAY_SIZE(multicast_queue_cfg));
				if (ret)
					goto qm_config_fail;

				PPE_AC_MULTICAST_QUEUE_SET_EN(multicast_queue_cfg, true);
				PPE_AC_MULTICAST_QUEUE_SET_GRN_GRP_ID(multicast_queue_cfg, 0);
				PPE_AC_MULTICAST_QUEUE_SET_GRN_PRE_LIMIT(multicast_queue_cfg,
									 queue_cfg[i].prealloc_buf);
				PPE_AC_MULTICAST_QUEUE_SET_GRN_THRESHOLD(multicast_queue_cfg,
									 queue_cfg[i].ceil);
				PPE_AC_MULTICAST_QUEUE_SET_GRN_RESUME(multicast_queue_cfg,
								      queue_cfg[i].resume_offset);

				ret = regmap_bulk_write(ppe_dev->regmap, reg,
							multicast_queue_cfg,
							ARRAY_SIZE(multicast_queue_cfg));
				if (ret)
					goto qm_config_fail;
			}

			/* Enable enqueue. */
			reg = PPE_ENQ_OPR_TBL_ADDR + PPE_ENQ_OPR_TBL_INC * queue_id;
			ret = regmap_clear_bits(ppe_dev->regmap, reg,
						PPE_ENQ_OPR_TBL_ENQ_DISABLE);
			if (ret)
				goto qm_config_fail;

			/* Enable dequeue. */
			reg = PPE_DEQ_OPR_TBL_ADDR + PPE_DEQ_OPR_TBL_INC * queue_id;
			ret = regmap_clear_bits(ppe_dev->regmap, reg,
						PPE_DEQ_OPR_TBL_DEQ_DISABLE);
			if (ret)
				goto qm_config_fail;

			queue_id++;
		}
	}

	/* Enable queue counter for all PPE hardware queues. */
	ret = regmap_set_bits(ppe_dev->regmap, PPE_EG_BRIDGE_CONFIG_ADDR,
			      PPE_EG_BRIDGE_CONFIG_QUEUE_CNT_EN);
	if (ret)
		goto qm_config_fail;

	return 0;

qm_config_fail:
	dev_err(ppe_dev->dev, "PPE QM config error %d\n", ret);
	return ret;
}

int ppe_hw_config(struct ppe_device *ppe_dev)
{
	int ret;

	ret = ppe_config_bm(ppe_dev);
	if (ret)
		return ret;

	return ppe_config_qm(ppe_dev);
}
