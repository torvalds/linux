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

#define PPE_QUEUE_SCH_PRI_NUM		8

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

/**
 * enum ppe_scheduler_direction - PPE scheduler direction for packet.
 * @PPE_SCH_INGRESS: Scheduler for the packet on ingress,
 * @PPE_SCH_EGRESS: Scheduler for the packet on egress,
 */
enum ppe_scheduler_direction {
	PPE_SCH_INGRESS = 0,
	PPE_SCH_EGRESS = 1,
};

/**
 * struct ppe_scheduler_bm_config - PPE arbitration for buffer config.
 * @valid: Arbitration entry valid or not.
 * @dir: Arbitration entry for egress or ingress.
 * @port: Port ID to use arbitration entry.
 * @backup_port_valid: Backup port valid or not.
 * @backup_port: Backup port ID to use.
 *
 * Configure the scheduler settings for accessing and releasing the PPE buffers.
 */
struct ppe_scheduler_bm_config {
	bool valid;
	enum ppe_scheduler_direction dir;
	unsigned int port;
	bool backup_port_valid;
	unsigned int backup_port;
};

/**
 * struct ppe_scheduler_qm_config - PPE arbitration for scheduler config.
 * @ensch_port_bmp: Port bit map for enqueue scheduler.
 * @ensch_port: Port ID to enqueue scheduler.
 * @desch_port: Port ID to dequeue scheduler.
 * @desch_backup_port_valid: Dequeue for the backup port valid or not.
 * @desch_backup_port: Backup port ID to dequeue scheduler.
 *
 * Configure the scheduler settings for enqueuing and dequeuing packets on
 * the PPE port.
 */
struct ppe_scheduler_qm_config {
	unsigned int ensch_port_bmp;
	unsigned int ensch_port;
	unsigned int desch_port;
	bool desch_backup_port_valid;
	unsigned int desch_backup_port;
};

/**
 * struct ppe_scheduler_port_config - PPE port scheduler config.
 * @port: Port ID to be scheduled.
 * @flow_level: Scheduler flow level or not.
 * @node_id: Node ID, for level 0, queue ID is used.
 * @loop_num: Loop number of scheduler config.
 * @pri_max: Max priority configured.
 * @flow_id: Strict priority ID.
 * @drr_node_id: Node ID for scheduler.
 *
 * PPE port scheduler configuration which decides the priority in the
 * packet scheduler for the egress port.
 */
struct ppe_scheduler_port_config {
	unsigned int port;
	bool flow_level;
	unsigned int node_id;
	unsigned int loop_num;
	unsigned int pri_max;
	unsigned int flow_id;
	unsigned int drr_node_id;
};

/**
 * struct ppe_port_schedule_resource - PPE port scheduler resource.
 * @ucastq_start: Unicast queue start ID.
 * @ucastq_end: Unicast queue end ID.
 * @mcastq_start: Multicast queue start ID.
 * @mcastq_end: Multicast queue end ID.
 * @flow_id_start: Flow start ID.
 * @flow_id_end: Flow end ID.
 * @l0node_start: Scheduler node start ID for queue level.
 * @l0node_end: Scheduler node end ID for queue level.
 * @l1node_start: Scheduler node start ID for flow level.
 * @l1node_end: Scheduler node end ID for flow level.
 *
 * PPE scheduler resource allocated among the PPE ports.
 */
struct ppe_port_schedule_resource {
	unsigned int ucastq_start;
	unsigned int ucastq_end;
	unsigned int mcastq_start;
	unsigned int mcastq_end;
	unsigned int flow_id_start;
	unsigned int flow_id_end;
	unsigned int l0node_start;
	unsigned int l0node_end;
	unsigned int l1node_start;
	unsigned int l1node_end;
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

/* PPE scheduler configuration for BM includes multiple entries. Each entry
 * indicates the primary port to be assigned the buffers for the ingress or
 * to release the buffers for the egress. Backup port ID will be used when
 * the primary port ID is down.
 */
static const struct ppe_scheduler_bm_config ipq9574_ppe_sch_bm_config[] = {
	{true, PPE_SCH_INGRESS, 0, false, 0},
	{true, PPE_SCH_EGRESS,  0, false, 0},
	{true, PPE_SCH_INGRESS, 5, false, 0},
	{true, PPE_SCH_EGRESS,  5, false, 0},
	{true, PPE_SCH_INGRESS, 6, false, 0},
	{true, PPE_SCH_EGRESS,  6, false, 0},
	{true, PPE_SCH_INGRESS, 1, false, 0},
	{true, PPE_SCH_EGRESS,  1, false, 0},
	{true, PPE_SCH_INGRESS, 0, false, 0},
	{true, PPE_SCH_EGRESS,  0, false, 0},
	{true, PPE_SCH_INGRESS, 5, false, 0},
	{true, PPE_SCH_EGRESS,  5, false, 0},
	{true, PPE_SCH_INGRESS, 6, false, 0},
	{true, PPE_SCH_EGRESS,  6, false, 0},
	{true, PPE_SCH_INGRESS, 7, false, 0},
	{true, PPE_SCH_EGRESS,  7, false, 0},
	{true, PPE_SCH_INGRESS, 0, false, 0},
	{true, PPE_SCH_EGRESS,  0, false, 0},
	{true, PPE_SCH_INGRESS, 1, false, 0},
	{true, PPE_SCH_EGRESS,  1, false, 0},
	{true, PPE_SCH_INGRESS, 5, false, 0},
	{true, PPE_SCH_EGRESS,  5, false, 0},
	{true, PPE_SCH_INGRESS, 6, false, 0},
	{true, PPE_SCH_EGRESS,  6, false, 0},
	{true, PPE_SCH_INGRESS, 2, false, 0},
	{true, PPE_SCH_EGRESS,  2, false, 0},
	{true, PPE_SCH_INGRESS, 0, false, 0},
	{true, PPE_SCH_EGRESS,  0, false, 0},
	{true, PPE_SCH_INGRESS, 5, false, 0},
	{true, PPE_SCH_EGRESS,  5, false, 0},
	{true, PPE_SCH_INGRESS, 6, false, 0},
	{true, PPE_SCH_EGRESS,  6, false, 0},
	{true, PPE_SCH_INGRESS, 1, false, 0},
	{true, PPE_SCH_EGRESS,  1, false, 0},
	{true, PPE_SCH_INGRESS, 3, false, 0},
	{true, PPE_SCH_EGRESS,  3, false, 0},
	{true, PPE_SCH_INGRESS, 0, false, 0},
	{true, PPE_SCH_EGRESS,  0, false, 0},
	{true, PPE_SCH_INGRESS, 5, false, 0},
	{true, PPE_SCH_EGRESS,  5, false, 0},
	{true, PPE_SCH_INGRESS, 6, false, 0},
	{true, PPE_SCH_EGRESS,  6, false, 0},
	{true, PPE_SCH_INGRESS, 7, false, 0},
	{true, PPE_SCH_EGRESS,  7, false, 0},
	{true, PPE_SCH_INGRESS, 0, false, 0},
	{true, PPE_SCH_EGRESS,  0, false, 0},
	{true, PPE_SCH_INGRESS, 1, false, 0},
	{true, PPE_SCH_EGRESS,  1, false, 0},
	{true, PPE_SCH_INGRESS, 5, false, 0},
	{true, PPE_SCH_EGRESS,  5, false, 0},
	{true, PPE_SCH_INGRESS, 6, false, 0},
	{true, PPE_SCH_EGRESS,  6, false, 0},
	{true, PPE_SCH_INGRESS, 4, false, 0},
	{true, PPE_SCH_EGRESS,  4, false, 0},
	{true, PPE_SCH_INGRESS, 0, false, 0},
	{true, PPE_SCH_EGRESS,  0, false, 0},
	{true, PPE_SCH_INGRESS, 5, false, 0},
	{true, PPE_SCH_EGRESS,  5, false, 0},
	{true, PPE_SCH_INGRESS, 6, false, 0},
	{true, PPE_SCH_EGRESS,  6, false, 0},
	{true, PPE_SCH_INGRESS, 1, false, 0},
	{true, PPE_SCH_EGRESS,  1, false, 0},
	{true, PPE_SCH_INGRESS, 0, false, 0},
	{true, PPE_SCH_EGRESS,  0, false, 0},
	{true, PPE_SCH_INGRESS, 5, false, 0},
	{true, PPE_SCH_EGRESS,  5, false, 0},
	{true, PPE_SCH_INGRESS, 6, false, 0},
	{true, PPE_SCH_EGRESS,  6, false, 0},
	{true, PPE_SCH_INGRESS, 2, false, 0},
	{true, PPE_SCH_EGRESS,  2, false, 0},
	{true, PPE_SCH_INGRESS, 0, false, 0},
	{true, PPE_SCH_EGRESS,  0, false, 0},
	{true, PPE_SCH_INGRESS, 7, false, 0},
	{true, PPE_SCH_EGRESS,  7, false, 0},
	{true, PPE_SCH_INGRESS, 5, false, 0},
	{true, PPE_SCH_EGRESS,  5, false, 0},
	{true, PPE_SCH_INGRESS, 6, false, 0},
	{true, PPE_SCH_EGRESS,  6, false, 0},
	{true, PPE_SCH_INGRESS, 1, false, 0},
	{true, PPE_SCH_EGRESS,  1, false, 0},
	{true, PPE_SCH_INGRESS, 0, false, 0},
	{true, PPE_SCH_EGRESS,  0, false, 0},
	{true, PPE_SCH_INGRESS, 5, false, 0},
	{true, PPE_SCH_EGRESS,  5, false, 0},
	{true, PPE_SCH_INGRESS, 6, false, 0},
	{true, PPE_SCH_EGRESS,  6, false, 0},
	{true, PPE_SCH_INGRESS, 3, false, 0},
	{true, PPE_SCH_EGRESS,  3, false, 0},
	{true, PPE_SCH_INGRESS, 1, false, 0},
	{true, PPE_SCH_EGRESS,  1, false, 0},
	{true, PPE_SCH_INGRESS, 0, false, 0},
	{true, PPE_SCH_EGRESS,  0, false, 0},
	{true, PPE_SCH_INGRESS, 5, false, 0},
	{true, PPE_SCH_EGRESS,  5, false, 0},
	{true, PPE_SCH_INGRESS, 6, false, 0},
	{true, PPE_SCH_EGRESS,  6, false, 0},
	{true, PPE_SCH_INGRESS, 4, false, 0},
	{true, PPE_SCH_EGRESS,  4, false, 0},
	{true, PPE_SCH_INGRESS, 7, false, 0},
	{true, PPE_SCH_EGRESS,  7, false, 0},
};

/* PPE scheduler configuration for QM includes multiple entries. Each entry
 * contains ports to be dispatched for enqueueing and dequeueing. The backup
 * port for dequeueing is supported to be used when the primary port for
 * dequeueing is down.
 */
static const struct ppe_scheduler_qm_config ipq9574_ppe_sch_qm_config[] = {
	{0x98, 6, 0, true, 1},
	{0x94, 5, 6, true, 3},
	{0x86, 0, 5, true, 4},
	{0x8C, 1, 6, true, 0},
	{0x1C, 7, 5, true, 1},
	{0x98, 2, 6, true, 0},
	{0x1C, 5, 7, true, 1},
	{0x34, 3, 6, true, 0},
	{0x8C, 4, 5, true, 1},
	{0x98, 2, 6, true, 0},
	{0x8C, 5, 4, true, 1},
	{0xA8, 0, 6, true, 2},
	{0x98, 5, 1, true, 0},
	{0x98, 6, 5, true, 2},
	{0x89, 1, 6, true, 4},
	{0xA4, 3, 0, true, 1},
	{0x8C, 5, 6, true, 4},
	{0xA8, 0, 2, true, 1},
	{0x98, 6, 5, true, 0},
	{0xC4, 4, 3, true, 1},
	{0x94, 6, 5, true, 0},
	{0x1C, 7, 6, true, 1},
	{0x98, 2, 5, true, 0},
	{0x1C, 6, 7, true, 1},
	{0x1C, 5, 6, true, 0},
	{0x94, 3, 5, true, 1},
	{0x8C, 4, 6, true, 0},
	{0x94, 1, 5, true, 3},
	{0x94, 6, 1, true, 0},
	{0xD0, 3, 5, true, 2},
	{0x98, 6, 0, true, 1},
	{0x94, 5, 6, true, 3},
	{0x94, 1, 5, true, 0},
	{0x98, 2, 6, true, 1},
	{0x8C, 4, 5, true, 0},
	{0x1C, 7, 6, true, 1},
	{0x8C, 0, 5, true, 4},
	{0x89, 1, 6, true, 2},
	{0x98, 5, 0, true, 1},
	{0x94, 6, 5, true, 3},
	{0x92, 0, 6, true, 2},
	{0x98, 1, 5, true, 0},
	{0x98, 6, 2, true, 1},
	{0xD0, 0, 5, true, 3},
	{0x94, 6, 0, true, 1},
	{0x8C, 5, 6, true, 4},
	{0x8C, 1, 5, true, 0},
	{0x1C, 6, 7, true, 1},
	{0x1C, 5, 6, true, 0},
	{0xB0, 2, 3, true, 1},
	{0xC4, 4, 5, true, 0},
	{0x8C, 6, 4, true, 1},
	{0xA4, 3, 6, true, 0},
	{0x1C, 5, 7, true, 1},
	{0x4C, 0, 5, true, 4},
	{0x8C, 6, 0, true, 1},
	{0x34, 7, 6, true, 3},
	{0x94, 5, 0, true, 1},
	{0x98, 6, 5, true, 2},
};

static const struct ppe_scheduler_port_config ppe_port_sch_config[] = {
	{
		.port		= 0,
		.flow_level	= true,
		.node_id	= 0,
		.loop_num	= 1,
		.pri_max	= 1,
		.flow_id	= 0,
		.drr_node_id	= 0,
	},
	{
		.port		= 0,
		.flow_level	= false,
		.node_id	= 0,
		.loop_num	= 8,
		.pri_max	= 8,
		.flow_id	= 0,
		.drr_node_id	= 0,
	},
	{
		.port		= 0,
		.flow_level	= false,
		.node_id	= 8,
		.loop_num	= 8,
		.pri_max	= 8,
		.flow_id	= 0,
		.drr_node_id	= 0,
	},
	{
		.port		= 0,
		.flow_level	= false,
		.node_id	= 16,
		.loop_num	= 8,
		.pri_max	= 8,
		.flow_id	= 0,
		.drr_node_id	= 0,
	},
	{
		.port		= 0,
		.flow_level	= false,
		.node_id	= 24,
		.loop_num	= 8,
		.pri_max	= 8,
		.flow_id	= 0,
		.drr_node_id	= 0,
	},
	{
		.port		= 0,
		.flow_level	= false,
		.node_id	= 32,
		.loop_num	= 8,
		.pri_max	= 8,
		.flow_id	= 0,
		.drr_node_id	= 0,
	},
	{
		.port		= 0,
		.flow_level	= false,
		.node_id	= 40,
		.loop_num	= 8,
		.pri_max	= 8,
		.flow_id	= 0,
		.drr_node_id	= 0,
	},
	{
		.port		= 0,
		.flow_level	= false,
		.node_id	= 48,
		.loop_num	= 8,
		.pri_max	= 8,
		.flow_id	= 0,
		.drr_node_id	= 0,
	},
	{
		.port		= 0,
		.flow_level	= false,
		.node_id	= 56,
		.loop_num	= 8,
		.pri_max	= 8,
		.flow_id	= 0,
		.drr_node_id	= 0,
	},
	{
		.port		= 0,
		.flow_level	= false,
		.node_id	= 256,
		.loop_num	= 8,
		.pri_max	= 8,
		.flow_id	= 0,
		.drr_node_id	= 0,
	},
	{
		.port		= 0,
		.flow_level	= false,
		.node_id	= 264,
		.loop_num	= 8,
		.pri_max	= 8,
		.flow_id	= 0,
		.drr_node_id	= 0,
	},
	{
		.port		= 1,
		.flow_level	= true,
		.node_id	= 36,
		.loop_num	= 2,
		.pri_max	= 0,
		.flow_id	= 1,
		.drr_node_id	= 8,
	},
	{
		.port		= 1,
		.flow_level	= false,
		.node_id	= 144,
		.loop_num	= 16,
		.pri_max	= 8,
		.flow_id	= 36,
		.drr_node_id	= 48,
	},
	{
		.port		= 1,
		.flow_level	= false,
		.node_id	= 272,
		.loop_num	= 4,
		.pri_max	= 4,
		.flow_id	= 36,
		.drr_node_id	= 48,
	},
	{
		.port		= 2,
		.flow_level	= true,
		.node_id	= 40,
		.loop_num	= 2,
		.pri_max	= 0,
		.flow_id	= 2,
		.drr_node_id	= 12,
	},
	{
		.port		= 2,
		.flow_level	= false,
		.node_id	= 160,
		.loop_num	= 16,
		.pri_max	= 8,
		.flow_id	= 40,
		.drr_node_id	= 64,
	},
	{
		.port		= 2,
		.flow_level	= false,
		.node_id	= 276,
		.loop_num	= 4,
		.pri_max	= 4,
		.flow_id	= 40,
		.drr_node_id	= 64,
	},
	{
		.port		= 3,
		.flow_level	= true,
		.node_id	= 44,
		.loop_num	= 2,
		.pri_max	= 0,
		.flow_id	= 3,
		.drr_node_id	= 16,
	},
	{
		.port		= 3,
		.flow_level	= false,
		.node_id	= 176,
		.loop_num	= 16,
		.pri_max	= 8,
		.flow_id	= 44,
		.drr_node_id	= 80,
	},
	{
		.port		= 3,
		.flow_level	= false,
		.node_id	= 280,
		.loop_num	= 4,
		.pri_max	= 4,
		.flow_id	= 44,
		.drr_node_id	= 80,
	},
	{
		.port		= 4,
		.flow_level	= true,
		.node_id	= 48,
		.loop_num	= 2,
		.pri_max	= 0,
		.flow_id	= 4,
		.drr_node_id	= 20,
	},
	{
		.port		= 4,
		.flow_level	= false,
		.node_id	= 192,
		.loop_num	= 16,
		.pri_max	= 8,
		.flow_id	= 48,
		.drr_node_id	= 96,
	},
	{
		.port		= 4,
		.flow_level	= false,
		.node_id	= 284,
		.loop_num	= 4,
		.pri_max	= 4,
		.flow_id	= 48,
		.drr_node_id	= 96,
	},
	{
		.port		= 5,
		.flow_level	= true,
		.node_id	= 52,
		.loop_num	= 2,
		.pri_max	= 0,
		.flow_id	= 5,
		.drr_node_id	= 24,
	},
	{
		.port		= 5,
		.flow_level	= false,
		.node_id	= 208,
		.loop_num	= 16,
		.pri_max	= 8,
		.flow_id	= 52,
		.drr_node_id	= 112,
	},
	{
		.port		= 5,
		.flow_level	= false,
		.node_id	= 288,
		.loop_num	= 4,
		.pri_max	= 4,
		.flow_id	= 52,
		.drr_node_id	= 112,
	},
	{
		.port		= 6,
		.flow_level	= true,
		.node_id	= 56,
		.loop_num	= 2,
		.pri_max	= 0,
		.flow_id	= 6,
		.drr_node_id	= 28,
	},
	{
		.port		= 6,
		.flow_level	= false,
		.node_id	= 224,
		.loop_num	= 16,
		.pri_max	= 8,
		.flow_id	= 56,
		.drr_node_id	= 128,
	},
	{
		.port		= 6,
		.flow_level	= false,
		.node_id	= 292,
		.loop_num	= 4,
		.pri_max	= 4,
		.flow_id	= 56,
		.drr_node_id	= 128,
	},
	{
		.port		= 7,
		.flow_level	= true,
		.node_id	= 60,
		.loop_num	= 2,
		.pri_max	= 0,
		.flow_id	= 7,
		.drr_node_id	= 32,
	},
	{
		.port		= 7,
		.flow_level	= false,
		.node_id	= 240,
		.loop_num	= 16,
		.pri_max	= 8,
		.flow_id	= 60,
		.drr_node_id	= 144,
	},
	{
		.port		= 7,
		.flow_level	= false,
		.node_id	= 296,
		.loop_num	= 4,
		.pri_max	= 4,
		.flow_id	= 60,
		.drr_node_id	= 144,
	},
};

/* The scheduler resource is applied to each PPE port, The resource
 * includes the unicast & multicast queues, flow nodes and DRR nodes.
 */
static const struct ppe_port_schedule_resource ppe_scheduler_res[] = {
	{	.ucastq_start	= 0,
		.ucastq_end	= 63,
		.mcastq_start	= 256,
		.mcastq_end	= 271,
		.flow_id_start	= 0,
		.flow_id_end	= 0,
		.l0node_start	= 0,
		.l0node_end	= 7,
		.l1node_start	= 0,
		.l1node_end	= 0,
	},
	{	.ucastq_start	= 144,
		.ucastq_end	= 159,
		.mcastq_start	= 272,
		.mcastq_end	= 275,
		.flow_id_start	= 36,
		.flow_id_end	= 39,
		.l0node_start	= 48,
		.l0node_end	= 63,
		.l1node_start	= 8,
		.l1node_end	= 11,
	},
	{	.ucastq_start	= 160,
		.ucastq_end	= 175,
		.mcastq_start	= 276,
		.mcastq_end	= 279,
		.flow_id_start	= 40,
		.flow_id_end	= 43,
		.l0node_start	= 64,
		.l0node_end	= 79,
		.l1node_start	= 12,
		.l1node_end	= 15,
	},
	{	.ucastq_start	= 176,
		.ucastq_end	= 191,
		.mcastq_start	= 280,
		.mcastq_end	= 283,
		.flow_id_start	= 44,
		.flow_id_end	= 47,
		.l0node_start	= 80,
		.l0node_end	= 95,
		.l1node_start	= 16,
		.l1node_end	= 19,
	},
	{	.ucastq_start	= 192,
		.ucastq_end	= 207,
		.mcastq_start	= 284,
		.mcastq_end	= 287,
		.flow_id_start	= 48,
		.flow_id_end	= 51,
		.l0node_start	= 96,
		.l0node_end	= 111,
		.l1node_start	= 20,
		.l1node_end	= 23,
	},
	{	.ucastq_start	= 208,
		.ucastq_end	= 223,
		.mcastq_start	= 288,
		.mcastq_end	= 291,
		.flow_id_start	= 52,
		.flow_id_end	= 55,
		.l0node_start	= 112,
		.l0node_end	= 127,
		.l1node_start	= 24,
		.l1node_end	= 27,
	},
	{	.ucastq_start	= 224,
		.ucastq_end	= 239,
		.mcastq_start	= 292,
		.mcastq_end	= 295,
		.flow_id_start	= 56,
		.flow_id_end	= 59,
		.l0node_start	= 128,
		.l0node_end	= 143,
		.l1node_start	= 28,
		.l1node_end	= 31,
	},
	{	.ucastq_start	= 240,
		.ucastq_end	= 255,
		.mcastq_start	= 296,
		.mcastq_end	= 299,
		.flow_id_start	= 60,
		.flow_id_end	= 63,
		.l0node_start	= 144,
		.l0node_end	= 159,
		.l1node_start	= 32,
		.l1node_end	= 35,
	},
	{	.ucastq_start	= 64,
		.ucastq_end	= 143,
		.mcastq_start	= 0,
		.mcastq_end	= 0,
		.flow_id_start	= 1,
		.flow_id_end	= 35,
		.l0node_start	= 8,
		.l0node_end	= 47,
		.l1node_start	= 1,
		.l1node_end	= 7,
	},
};

/* Set the PPE queue level scheduler configuration. */
static int ppe_scheduler_l0_queue_map_set(struct ppe_device *ppe_dev,
					  int node_id, int port,
					  struct ppe_scheduler_cfg scheduler_cfg)
{
	u32 val, reg;
	int ret;

	reg = PPE_L0_FLOW_MAP_TBL_ADDR + node_id * PPE_L0_FLOW_MAP_TBL_INC;
	val = FIELD_PREP(PPE_L0_FLOW_MAP_TBL_FLOW_ID, scheduler_cfg.flow_id);
	val |= FIELD_PREP(PPE_L0_FLOW_MAP_TBL_C_PRI, scheduler_cfg.pri);
	val |= FIELD_PREP(PPE_L0_FLOW_MAP_TBL_E_PRI, scheduler_cfg.pri);
	val |= FIELD_PREP(PPE_L0_FLOW_MAP_TBL_C_NODE_WT, scheduler_cfg.drr_node_wt);
	val |= FIELD_PREP(PPE_L0_FLOW_MAP_TBL_E_NODE_WT, scheduler_cfg.drr_node_wt);

	ret = regmap_write(ppe_dev->regmap, reg, val);
	if (ret)
		return ret;

	reg = PPE_L0_C_FLOW_CFG_TBL_ADDR +
	      (scheduler_cfg.flow_id * PPE_QUEUE_SCH_PRI_NUM + scheduler_cfg.pri) *
	      PPE_L0_C_FLOW_CFG_TBL_INC;
	val = FIELD_PREP(PPE_L0_C_FLOW_CFG_TBL_NODE_ID, scheduler_cfg.drr_node_id);
	val |= FIELD_PREP(PPE_L0_C_FLOW_CFG_TBL_NODE_CREDIT_UNIT, scheduler_cfg.unit_is_packet);

	ret = regmap_write(ppe_dev->regmap, reg, val);
	if (ret)
		return ret;

	reg = PPE_L0_E_FLOW_CFG_TBL_ADDR +
	      (scheduler_cfg.flow_id * PPE_QUEUE_SCH_PRI_NUM + scheduler_cfg.pri) *
	      PPE_L0_E_FLOW_CFG_TBL_INC;
	val = FIELD_PREP(PPE_L0_E_FLOW_CFG_TBL_NODE_ID, scheduler_cfg.drr_node_id);
	val |= FIELD_PREP(PPE_L0_E_FLOW_CFG_TBL_NODE_CREDIT_UNIT, scheduler_cfg.unit_is_packet);

	ret = regmap_write(ppe_dev->regmap, reg, val);
	if (ret)
		return ret;

	reg = PPE_L0_FLOW_PORT_MAP_TBL_ADDR + node_id * PPE_L0_FLOW_PORT_MAP_TBL_INC;
	val = FIELD_PREP(PPE_L0_FLOW_PORT_MAP_TBL_PORT_NUM, port);

	ret = regmap_write(ppe_dev->regmap, reg, val);
	if (ret)
		return ret;

	reg = PPE_L0_COMP_CFG_TBL_ADDR + node_id * PPE_L0_COMP_CFG_TBL_INC;
	val = FIELD_PREP(PPE_L0_COMP_CFG_TBL_NODE_METER_LEN, scheduler_cfg.frame_mode);

	return regmap_update_bits(ppe_dev->regmap, reg,
				  PPE_L0_COMP_CFG_TBL_NODE_METER_LEN,
				  val);
}

/* Set the PPE flow level scheduler configuration. */
static int ppe_scheduler_l1_queue_map_set(struct ppe_device *ppe_dev,
					  int node_id, int port,
					  struct ppe_scheduler_cfg scheduler_cfg)
{
	u32 val, reg;
	int ret;

	val = FIELD_PREP(PPE_L1_FLOW_MAP_TBL_FLOW_ID, scheduler_cfg.flow_id);
	val |= FIELD_PREP(PPE_L1_FLOW_MAP_TBL_C_PRI, scheduler_cfg.pri);
	val |= FIELD_PREP(PPE_L1_FLOW_MAP_TBL_E_PRI, scheduler_cfg.pri);
	val |= FIELD_PREP(PPE_L1_FLOW_MAP_TBL_C_NODE_WT, scheduler_cfg.drr_node_wt);
	val |= FIELD_PREP(PPE_L1_FLOW_MAP_TBL_E_NODE_WT, scheduler_cfg.drr_node_wt);
	reg = PPE_L1_FLOW_MAP_TBL_ADDR + node_id * PPE_L1_FLOW_MAP_TBL_INC;

	ret = regmap_write(ppe_dev->regmap, reg, val);
	if (ret)
		return ret;

	val = FIELD_PREP(PPE_L1_C_FLOW_CFG_TBL_NODE_ID, scheduler_cfg.drr_node_id);
	val |= FIELD_PREP(PPE_L1_C_FLOW_CFG_TBL_NODE_CREDIT_UNIT, scheduler_cfg.unit_is_packet);
	reg = PPE_L1_C_FLOW_CFG_TBL_ADDR +
	      (scheduler_cfg.flow_id * PPE_QUEUE_SCH_PRI_NUM + scheduler_cfg.pri) *
	      PPE_L1_C_FLOW_CFG_TBL_INC;

	ret = regmap_write(ppe_dev->regmap, reg, val);
	if (ret)
		return ret;

	val = FIELD_PREP(PPE_L1_E_FLOW_CFG_TBL_NODE_ID, scheduler_cfg.drr_node_id);
	val |= FIELD_PREP(PPE_L1_E_FLOW_CFG_TBL_NODE_CREDIT_UNIT, scheduler_cfg.unit_is_packet);
	reg = PPE_L1_E_FLOW_CFG_TBL_ADDR +
		(scheduler_cfg.flow_id * PPE_QUEUE_SCH_PRI_NUM + scheduler_cfg.pri) *
		PPE_L1_E_FLOW_CFG_TBL_INC;

	ret = regmap_write(ppe_dev->regmap, reg, val);
	if (ret)
		return ret;

	val = FIELD_PREP(PPE_L1_FLOW_PORT_MAP_TBL_PORT_NUM, port);
	reg = PPE_L1_FLOW_PORT_MAP_TBL_ADDR + node_id * PPE_L1_FLOW_PORT_MAP_TBL_INC;

	ret = regmap_write(ppe_dev->regmap, reg, val);
	if (ret)
		return ret;

	reg = PPE_L1_COMP_CFG_TBL_ADDR + node_id * PPE_L1_COMP_CFG_TBL_INC;
	val = FIELD_PREP(PPE_L1_COMP_CFG_TBL_NODE_METER_LEN, scheduler_cfg.frame_mode);

	return regmap_update_bits(ppe_dev->regmap, reg, PPE_L1_COMP_CFG_TBL_NODE_METER_LEN, val);
}

/**
 * ppe_queue_scheduler_set - Configure scheduler for PPE hardware queue
 * @ppe_dev: PPE device
 * @node_id: PPE queue ID or flow ID
 * @flow_level: Flow level scheduler or queue level scheduler
 * @port: PPE port ID set scheduler configuration
 * @scheduler_cfg: PPE scheduler configuration
 *
 * PPE scheduler configuration supports queue level and flow level on
 * the PPE egress port.
 *
 * Return: 0 on success, negative error code on failure.
 */
int ppe_queue_scheduler_set(struct ppe_device *ppe_dev,
			    int node_id, bool flow_level, int port,
			    struct ppe_scheduler_cfg scheduler_cfg)
{
	if (flow_level)
		return ppe_scheduler_l1_queue_map_set(ppe_dev, node_id,
						      port, scheduler_cfg);

	return ppe_scheduler_l0_queue_map_set(ppe_dev, node_id,
					      port, scheduler_cfg);
}

/**
 * ppe_queue_ucast_base_set - Set PPE unicast queue base ID and profile ID
 * @ppe_dev: PPE device
 * @queue_dst: PPE queue destination configuration
 * @queue_base: PPE queue base ID
 * @profile_id: Profile ID
 *
 * The PPE unicast queue base ID and profile ID are configured based on the
 * destination port information that can be service code or CPU code or the
 * destination port.
 *
 * Return: 0 on success, negative error code on failure.
 */
int ppe_queue_ucast_base_set(struct ppe_device *ppe_dev,
			     struct ppe_queue_ucast_dest queue_dst,
			     int queue_base, int profile_id)
{
	int index, profile_size;
	u32 val, reg;

	profile_size = queue_dst.src_profile << 8;
	if (queue_dst.service_code_en)
		index = PPE_QUEUE_BASE_SERVICE_CODE + profile_size +
			queue_dst.service_code;
	else if (queue_dst.cpu_code_en)
		index = PPE_QUEUE_BASE_CPU_CODE + profile_size +
			queue_dst.cpu_code;
	else
		index = profile_size + queue_dst.dest_port;

	val = FIELD_PREP(PPE_UCAST_QUEUE_MAP_TBL_PROFILE_ID, profile_id);
	val |= FIELD_PREP(PPE_UCAST_QUEUE_MAP_TBL_QUEUE_ID, queue_base);
	reg = PPE_UCAST_QUEUE_MAP_TBL_ADDR + index * PPE_UCAST_QUEUE_MAP_TBL_INC;

	return regmap_write(ppe_dev->regmap, reg, val);
}

/**
 * ppe_queue_ucast_offset_pri_set - Set PPE unicast queue offset based on priority
 * @ppe_dev: PPE device
 * @profile_id: Profile ID
 * @priority: PPE internal priority to be used to set queue offset
 * @queue_offset: Queue offset used for calculating the destination queue ID
 *
 * The PPE unicast queue offset is configured based on the PPE
 * internal priority.
 *
 * Return: 0 on success, negative error code on failure.
 */
int ppe_queue_ucast_offset_pri_set(struct ppe_device *ppe_dev,
				   int profile_id,
				   int priority,
				   int queue_offset)
{
	u32 val, reg;
	int index;

	index = (profile_id << 4) + priority;
	val = FIELD_PREP(PPE_UCAST_PRIORITY_MAP_TBL_CLASS, queue_offset);
	reg = PPE_UCAST_PRIORITY_MAP_TBL_ADDR + index * PPE_UCAST_PRIORITY_MAP_TBL_INC;

	return regmap_write(ppe_dev->regmap, reg, val);
}

/**
 * ppe_queue_ucast_offset_hash_set - Set PPE unicast queue offset based on hash
 * @ppe_dev: PPE device
 * @profile_id: Profile ID
 * @rss_hash: Packet hash value to be used to set queue offset
 * @queue_offset: Queue offset used for calculating the destination queue ID
 *
 * The PPE unicast queue offset is configured based on the RSS hash value.
 *
 * Return: 0 on success, negative error code on failure.
 */
int ppe_queue_ucast_offset_hash_set(struct ppe_device *ppe_dev,
				    int profile_id,
				    int rss_hash,
				    int queue_offset)
{
	u32 val, reg;
	int index;

	index = (profile_id << 8) + rss_hash;
	val = FIELD_PREP(PPE_UCAST_HASH_MAP_TBL_HASH, queue_offset);
	reg = PPE_UCAST_HASH_MAP_TBL_ADDR + index * PPE_UCAST_HASH_MAP_TBL_INC;

	return regmap_write(ppe_dev->regmap, reg, val);
}

/**
 * ppe_port_resource_get - Get PPE resource per port
 * @ppe_dev: PPE device
 * @port: PPE port
 * @type: Resource type
 * @res_start: Resource start ID returned
 * @res_end: Resource end ID returned
 *
 * PPE resource is assigned per PPE port, which is acquired for QoS scheduler.
 *
 * Return: 0 on success, negative error code on failure.
 */
int ppe_port_resource_get(struct ppe_device *ppe_dev, int port,
			  enum ppe_resource_type type,
			  int *res_start, int *res_end)
{
	struct ppe_port_schedule_resource res;

	/* The reserved resource with the maximum port ID of PPE is
	 * also allowed to be acquired.
	 */
	if (port > ppe_dev->num_ports)
		return -EINVAL;

	res = ppe_scheduler_res[port];
	switch (type) {
	case PPE_RES_UCAST:
		*res_start = res.ucastq_start;
		*res_end = res.ucastq_end;
		break;
	case PPE_RES_MCAST:
		*res_start = res.mcastq_start;
		*res_end = res.mcastq_end;
		break;
	case PPE_RES_FLOW_ID:
		*res_start = res.flow_id_start;
		*res_end = res.flow_id_end;
		break;
	case PPE_RES_L0_NODE:
		*res_start = res.l0node_start;
		*res_end = res.l0node_end;
		break;
	case PPE_RES_L1_NODE:
		*res_start = res.l1node_start;
		*res_end = res.l1node_end;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

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

static int ppe_node_scheduler_config(struct ppe_device *ppe_dev,
				     const struct ppe_scheduler_port_config config)
{
	struct ppe_scheduler_cfg sch_cfg;
	int ret, i;

	for (i = 0; i < config.loop_num; i++) {
		if (!config.pri_max) {
			/* Round robin scheduler without priority. */
			sch_cfg.flow_id = config.flow_id;
			sch_cfg.pri = 0;
			sch_cfg.drr_node_id = config.drr_node_id;
		} else {
			sch_cfg.flow_id = config.flow_id + (i / config.pri_max);
			sch_cfg.pri = i % config.pri_max;
			sch_cfg.drr_node_id = config.drr_node_id + i;
		}

		/* Scheduler weight, must be more than 0. */
		sch_cfg.drr_node_wt = 1;
		/* Byte based to be scheduled. */
		sch_cfg.unit_is_packet = false;
		/* Frame + CRC calculated. */
		sch_cfg.frame_mode = PPE_SCH_WITH_FRAME_CRC;

		ret = ppe_queue_scheduler_set(ppe_dev, config.node_id + i,
					      config.flow_level,
					      config.port,
					      sch_cfg);
		if (ret)
			return ret;
	}

	return 0;
}

/* Initialize scheduler settings for PPE buffer utilization and dispatching
 * packet on PPE queue.
 */
static int ppe_config_scheduler(struct ppe_device *ppe_dev)
{
	const struct ppe_scheduler_port_config *port_cfg;
	const struct ppe_scheduler_qm_config *qm_cfg;
	const struct ppe_scheduler_bm_config *bm_cfg;
	int ret, i, count;
	u32 val, reg;

	count = ARRAY_SIZE(ipq9574_ppe_sch_bm_config);
	bm_cfg = ipq9574_ppe_sch_bm_config;

	/* Configure the depth of BM scheduler entries. */
	val = FIELD_PREP(PPE_BM_SCH_CTRL_SCH_DEPTH, count);
	val |= FIELD_PREP(PPE_BM_SCH_CTRL_SCH_OFFSET, 0);
	val |= FIELD_PREP(PPE_BM_SCH_CTRL_SCH_EN, 1);

	ret = regmap_write(ppe_dev->regmap, PPE_BM_SCH_CTRL_ADDR, val);
	if (ret)
		goto sch_config_fail;

	/* Configure each BM scheduler entry with the valid ingress port and
	 * egress port, the second port takes effect when the specified port
	 * is in the inactive state.
	 */
	for (i = 0; i < count; i++) {
		val = FIELD_PREP(PPE_BM_SCH_CFG_TBL_VALID, bm_cfg[i].valid);
		val |= FIELD_PREP(PPE_BM_SCH_CFG_TBL_DIR, bm_cfg[i].dir);
		val |= FIELD_PREP(PPE_BM_SCH_CFG_TBL_PORT_NUM, bm_cfg[i].port);
		val |= FIELD_PREP(PPE_BM_SCH_CFG_TBL_SECOND_PORT_VALID,
				  bm_cfg[i].backup_port_valid);
		val |= FIELD_PREP(PPE_BM_SCH_CFG_TBL_SECOND_PORT,
				  bm_cfg[i].backup_port);

		reg = PPE_BM_SCH_CFG_TBL_ADDR + i * PPE_BM_SCH_CFG_TBL_INC;
		ret = regmap_write(ppe_dev->regmap, reg, val);
		if (ret)
			goto sch_config_fail;
	}

	count = ARRAY_SIZE(ipq9574_ppe_sch_qm_config);
	qm_cfg = ipq9574_ppe_sch_qm_config;

	/* Configure the depth of QM scheduler entries. */
	val = FIELD_PREP(PPE_PSCH_SCH_DEPTH_CFG_SCH_DEPTH, count);
	ret = regmap_write(ppe_dev->regmap, PPE_PSCH_SCH_DEPTH_CFG_ADDR, val);
	if (ret)
		goto sch_config_fail;

	/* Configure each QM scheduler entry with enqueue port and dequeue
	 * port, the second port takes effect when the specified dequeue
	 * port is in the inactive port.
	 */
	for (i = 0; i < count; i++) {
		val = FIELD_PREP(PPE_PSCH_SCH_CFG_TBL_ENS_PORT_BITMAP,
				 qm_cfg[i].ensch_port_bmp);
		val |= FIELD_PREP(PPE_PSCH_SCH_CFG_TBL_ENS_PORT,
				  qm_cfg[i].ensch_port);
		val |= FIELD_PREP(PPE_PSCH_SCH_CFG_TBL_DES_PORT,
				  qm_cfg[i].desch_port);
		val |= FIELD_PREP(PPE_PSCH_SCH_CFG_TBL_DES_SECOND_PORT_EN,
				  qm_cfg[i].desch_backup_port_valid);
		val |= FIELD_PREP(PPE_PSCH_SCH_CFG_TBL_DES_SECOND_PORT,
				  qm_cfg[i].desch_backup_port);

		reg = PPE_PSCH_SCH_CFG_TBL_ADDR + i * PPE_PSCH_SCH_CFG_TBL_INC;
		ret = regmap_write(ppe_dev->regmap, reg, val);
		if (ret)
			goto sch_config_fail;
	}

	count = ARRAY_SIZE(ppe_port_sch_config);
	port_cfg = ppe_port_sch_config;

	/* Configure scheduler per PPE queue or flow. */
	for (i = 0; i < count; i++) {
		if (port_cfg[i].port >= ppe_dev->num_ports)
			break;

		ret = ppe_node_scheduler_config(ppe_dev, port_cfg[i]);
		if (ret)
			goto sch_config_fail;
	}

	return 0;

sch_config_fail:
	dev_err(ppe_dev->dev, "PPE scheduler arbitration config error %d\n", ret);
	return ret;
};

/* Configure PPE queue destination of each PPE port. */
static int ppe_queue_dest_init(struct ppe_device *ppe_dev)
{
	int ret, port_id, index, q_base, q_offset, res_start, res_end, pri_max;
	struct ppe_queue_ucast_dest queue_dst;

	for (port_id = 0; port_id < ppe_dev->num_ports; port_id++) {
		memset(&queue_dst, 0, sizeof(queue_dst));

		ret = ppe_port_resource_get(ppe_dev, port_id, PPE_RES_UCAST,
					    &res_start, &res_end);
		if (ret)
			return ret;

		q_base = res_start;
		queue_dst.dest_port = port_id;

		/* Configure queue base ID and profile ID that is same as
		 * physical port ID.
		 */
		ret = ppe_queue_ucast_base_set(ppe_dev, queue_dst,
					       q_base, port_id);
		if (ret)
			return ret;

		/* Queue priority range supported by each PPE port */
		ret = ppe_port_resource_get(ppe_dev, port_id, PPE_RES_L0_NODE,
					    &res_start, &res_end);
		if (ret)
			return ret;

		pri_max = res_end - res_start;

		/* Redirect ARP reply packet with the max priority on CPU port,
		 * which keeps the ARP reply directed to CPU (CPU code is 101)
		 * with highest priority queue of EDMA.
		 */
		if (port_id == 0) {
			memset(&queue_dst, 0, sizeof(queue_dst));

			queue_dst.cpu_code_en = true;
			queue_dst.cpu_code = 101;
			ret = ppe_queue_ucast_base_set(ppe_dev, queue_dst,
						       q_base + pri_max,
						       0);
			if (ret)
				return ret;
		}

		/* Initialize the queue offset of internal priority. */
		for (index = 0; index < PPE_QUEUE_INTER_PRI_NUM; index++) {
			q_offset = index > pri_max ? pri_max : index;

			ret = ppe_queue_ucast_offset_pri_set(ppe_dev, port_id,
							     index, q_offset);
			if (ret)
				return ret;
		}

		/* Initialize the queue offset of RSS hash as 0 to avoid the
		 * random hardware value that will lead to the unexpected
		 * destination queue generated.
		 */
		for (index = 0; index < PPE_QUEUE_HASH_NUM; index++) {
			ret = ppe_queue_ucast_offset_hash_set(ppe_dev, port_id,
							      index, 0);
			if (ret)
				return ret;
		}
	}

	return 0;
}

int ppe_hw_config(struct ppe_device *ppe_dev)
{
	int ret;

	ret = ppe_config_bm(ppe_dev);
	if (ret)
		return ret;

	ret = ppe_config_qm(ppe_dev);
	if (ret)
		return ret;

	ret = ppe_config_scheduler(ppe_dev);
	if (ret)
		return ret;

	return ppe_queue_dest_init(ppe_dev);
}
