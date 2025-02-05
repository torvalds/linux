// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2017-2018 The Linux Foundation. All rights reserved. */

#include <linux/completion.h>
#include <linux/circ_buf.h>
#include <linux/list.h>

#include <soc/qcom/cmd-db.h>
#include <soc/qcom/tcs.h>

#include "a6xx_gmu.h"
#include "a6xx_gmu.xml.h"
#include "a6xx_gpu.h"

#define HFI_MSG_ID(val) [val] = #val

static const char * const a6xx_hfi_msg_id[] = {
	HFI_MSG_ID(HFI_H2F_MSG_INIT),
	HFI_MSG_ID(HFI_H2F_MSG_FW_VERSION),
	HFI_MSG_ID(HFI_H2F_MSG_BW_TABLE),
	HFI_MSG_ID(HFI_H2F_MSG_PERF_TABLE),
	HFI_MSG_ID(HFI_H2F_MSG_TEST),
	HFI_MSG_ID(HFI_H2F_MSG_START),
	HFI_MSG_ID(HFI_H2F_MSG_CORE_FW_START),
	HFI_MSG_ID(HFI_H2F_MSG_GX_BW_PERF_VOTE),
	HFI_MSG_ID(HFI_H2F_MSG_PREPARE_SLUMBER),
};

static int a6xx_hfi_queue_read(struct a6xx_gmu *gmu,
	struct a6xx_hfi_queue *queue, u32 *data, u32 dwords)
{
	struct a6xx_hfi_queue_header *header = queue->header;
	u32 i, hdr, index = header->read_index;

	if (header->read_index == header->write_index) {
		header->rx_request = 1;
		return 0;
	}

	hdr = queue->data[index];

	queue->history[(queue->history_idx++) % HFI_HISTORY_SZ] = index;

	/*
	 * If we are to assume that the GMU firmware is in fact a rational actor
	 * and is programmed to not send us a larger response than we expect
	 * then we can also assume that if the header size is unexpectedly large
	 * that it is due to memory corruption and/or hardware failure. In this
	 * case the only reasonable course of action is to BUG() to help harden
	 * the failure.
	 */

	BUG_ON(HFI_HEADER_SIZE(hdr) > dwords);

	for (i = 0; i < HFI_HEADER_SIZE(hdr); i++) {
		data[i] = queue->data[index];
		index = (index + 1) % header->size;
	}

	if (!gmu->legacy)
		index = ALIGN(index, 4) % header->size;

	header->read_index = index;
	return HFI_HEADER_SIZE(hdr);
}

static int a6xx_hfi_queue_write(struct a6xx_gmu *gmu,
	struct a6xx_hfi_queue *queue, u32 *data, u32 dwords)
{
	struct a6xx_hfi_queue_header *header = queue->header;
	u32 i, space, index = header->write_index;

	spin_lock(&queue->lock);

	space = CIRC_SPACE(header->write_index, header->read_index,
		header->size);
	if (space < dwords) {
		header->dropped++;
		spin_unlock(&queue->lock);
		return -ENOSPC;
	}

	queue->history[(queue->history_idx++) % HFI_HISTORY_SZ] = index;

	for (i = 0; i < dwords; i++) {
		queue->data[index] = data[i];
		index = (index + 1) % header->size;
	}

	/* Cookify any non used data at the end of the write buffer */
	if (!gmu->legacy) {
		for (; index % 4; index = (index + 1) % header->size)
			queue->data[index] = 0xfafafafa;
	}

	header->write_index = index;
	spin_unlock(&queue->lock);

	gmu_write(gmu, REG_A6XX_GMU_HOST2GMU_INTR_SET, 0x01);
	return 0;
}

static int a6xx_hfi_wait_for_ack(struct a6xx_gmu *gmu, u32 id, u32 seqnum,
		u32 *payload, u32 payload_size)
{
	struct a6xx_hfi_queue *queue = &gmu->queues[HFI_RESPONSE_QUEUE];
	u32 val;
	int ret;

	/* Wait for a response */
	ret = gmu_poll_timeout(gmu, REG_A6XX_GMU_GMU2HOST_INTR_INFO, val,
		val & A6XX_GMU_GMU2HOST_INTR_INFO_MSGQ, 100, 5000);

	if (ret) {
		DRM_DEV_ERROR(gmu->dev,
			"Message %s id %d timed out waiting for response\n",
			a6xx_hfi_msg_id[id], seqnum);
		return -ETIMEDOUT;
	}

	/* Clear the interrupt */
	gmu_write(gmu, REG_A6XX_GMU_GMU2HOST_INTR_CLR,
		A6XX_GMU_GMU2HOST_INTR_INFO_MSGQ);

	for (;;) {
		struct a6xx_hfi_msg_response resp;

		/* Get the next packet */
		ret = a6xx_hfi_queue_read(gmu, queue, (u32 *) &resp,
			sizeof(resp) >> 2);

		/* If the queue is empty our response never made it */
		if (!ret) {
			DRM_DEV_ERROR(gmu->dev,
				"The HFI response queue is unexpectedly empty\n");

			return -ENOENT;
		}

		if (HFI_HEADER_ID(resp.header) == HFI_F2H_MSG_ERROR) {
			struct a6xx_hfi_msg_error *error =
				(struct a6xx_hfi_msg_error *) &resp;

			DRM_DEV_ERROR(gmu->dev, "GMU firmware error %d\n",
				error->code);
			continue;
		}

		if (seqnum != HFI_HEADER_SEQNUM(resp.ret_header)) {
			DRM_DEV_ERROR(gmu->dev,
				"Unexpected message id %d on the response queue\n",
				HFI_HEADER_SEQNUM(resp.ret_header));
			continue;
		}

		if (resp.error) {
			DRM_DEV_ERROR(gmu->dev,
				"Message %s id %d returned error %d\n",
				a6xx_hfi_msg_id[id], seqnum, resp.error);
			return -EINVAL;
		}

		/* All is well, copy over the buffer */
		if (payload && payload_size)
			memcpy(payload, resp.payload,
				min_t(u32, payload_size, sizeof(resp.payload)));

		return 0;
	}
}

static int a6xx_hfi_send_msg(struct a6xx_gmu *gmu, int id,
		void *data, u32 size, u32 *payload, u32 payload_size)
{
	struct a6xx_hfi_queue *queue = &gmu->queues[HFI_COMMAND_QUEUE];
	int ret, dwords = size >> 2;
	u32 seqnum;

	seqnum = atomic_inc_return(&queue->seqnum) % 0xfff;

	/* First dword of the message is the message header - fill it in */
	*((u32 *) data) = (seqnum << 20) | (HFI_MSG_CMD << 16) |
		(dwords << 8) | id;

	ret = a6xx_hfi_queue_write(gmu, queue, data, dwords);
	if (ret) {
		DRM_DEV_ERROR(gmu->dev, "Unable to send message %s id %d\n",
			a6xx_hfi_msg_id[id], seqnum);
		return ret;
	}

	return a6xx_hfi_wait_for_ack(gmu, id, seqnum, payload, payload_size);
}

static int a6xx_hfi_send_gmu_init(struct a6xx_gmu *gmu, int boot_state)
{
	struct a6xx_hfi_msg_gmu_init_cmd msg = { 0 };

	msg.dbg_buffer_addr = (u32) gmu->debug.iova;
	msg.dbg_buffer_size = (u32) gmu->debug.size;
	msg.boot_state = boot_state;

	return a6xx_hfi_send_msg(gmu, HFI_H2F_MSG_INIT, &msg, sizeof(msg),
		NULL, 0);
}

static int a6xx_hfi_get_fw_version(struct a6xx_gmu *gmu, u32 *version)
{
	struct a6xx_hfi_msg_fw_version msg = { 0 };

	/* Currently supporting version 1.10 */
	msg.supported_version = (1 << 28) | (1 << 19) | (1 << 17);

	return a6xx_hfi_send_msg(gmu, HFI_H2F_MSG_FW_VERSION, &msg, sizeof(msg),
		version, sizeof(*version));
}

static int a6xx_hfi_send_perf_table_v1(struct a6xx_gmu *gmu)
{
	struct a6xx_hfi_msg_perf_table_v1 msg = { 0 };
	int i;

	msg.num_gpu_levels = gmu->nr_gpu_freqs;
	msg.num_gmu_levels = gmu->nr_gmu_freqs;

	for (i = 0; i < gmu->nr_gpu_freqs; i++) {
		msg.gx_votes[i].vote = gmu->gx_arc_votes[i];
		msg.gx_votes[i].freq = gmu->gpu_freqs[i] / 1000;
	}

	for (i = 0; i < gmu->nr_gmu_freqs; i++) {
		msg.cx_votes[i].vote = gmu->cx_arc_votes[i];
		msg.cx_votes[i].freq = gmu->gmu_freqs[i] / 1000;
	}

	return a6xx_hfi_send_msg(gmu, HFI_H2F_MSG_PERF_TABLE, &msg, sizeof(msg),
		NULL, 0);
}

static int a6xx_hfi_send_perf_table(struct a6xx_gmu *gmu)
{
	struct a6xx_hfi_msg_perf_table msg = { 0 };
	int i;

	msg.num_gpu_levels = gmu->nr_gpu_freqs;
	msg.num_gmu_levels = gmu->nr_gmu_freqs;

	for (i = 0; i < gmu->nr_gpu_freqs; i++) {
		msg.gx_votes[i].vote = gmu->gx_arc_votes[i];
		msg.gx_votes[i].acd = 0xffffffff;
		msg.gx_votes[i].freq = gmu->gpu_freqs[i] / 1000;
	}

	for (i = 0; i < gmu->nr_gmu_freqs; i++) {
		msg.cx_votes[i].vote = gmu->cx_arc_votes[i];
		msg.cx_votes[i].freq = gmu->gmu_freqs[i] / 1000;
	}

	return a6xx_hfi_send_msg(gmu, HFI_H2F_MSG_PERF_TABLE, &msg, sizeof(msg),
		NULL, 0);
}

static void a6xx_generate_bw_table(const struct a6xx_info *info, struct a6xx_gmu *gmu,
				   struct a6xx_hfi_msg_bw_table *msg)
{
	unsigned int i, j;

	for (i = 0; i < GMU_MAX_BCMS; i++) {
		if (!info->bcms[i].name)
			break;
		msg->ddr_cmds_addrs[i] = cmd_db_read_addr(info->bcms[i].name);
	}
	msg->ddr_cmds_num = i;

	for (i = 0; i < gmu->nr_gpu_bws; ++i)
		for (j = 0; j < msg->ddr_cmds_num; j++)
			msg->ddr_cmds_data[i][j] = gmu->gpu_ib_votes[i][j];
	msg->bw_level_num = gmu->nr_gpu_bws;

	/* Compute the wait bitmask with each BCM having the commit bit */
	msg->ddr_wait_bitmask = 0;
	for (j = 0; j < msg->ddr_cmds_num; j++)
		if (msg->ddr_cmds_data[0][j] & BCM_TCS_CMD_COMMIT_MASK)
			msg->ddr_wait_bitmask |= BIT(j);

	/*
	 * These are the CX (CNOC) votes - these are used by the GMU
	 * The 'CN0' BCM is used on all targets, and votes are basically
	 * 'off' and 'on' states with first bit to enable the path.
	 */

	msg->cnoc_cmds_addrs[0] = cmd_db_read_addr("CN0");
	msg->cnoc_cmds_num = 1;

	msg->cnoc_cmds_data[0][0] = BCM_TCS_CMD(true, false, 0, 0);
	msg->cnoc_cmds_data[1][0] = BCM_TCS_CMD(true, true, 0, BIT(0));

	/* Compute the wait bitmask with each BCM having the commit bit */
	msg->cnoc_wait_bitmask = 0;
	for (j = 0; j < msg->cnoc_cmds_num; j++)
		if (msg->cnoc_cmds_data[0][j] & BCM_TCS_CMD_COMMIT_MASK)
			msg->cnoc_wait_bitmask |= BIT(j);
}

static void a618_build_bw_table(struct a6xx_hfi_msg_bw_table *msg)
{
	/* Send a single "off" entry since the 618 GMU doesn't do bus scaling */
	msg->bw_level_num = 1;

	msg->ddr_cmds_num = 3;
	msg->ddr_wait_bitmask = 0x01;

	msg->ddr_cmds_addrs[0] = 0x50000;
	msg->ddr_cmds_addrs[1] = 0x5003c;
	msg->ddr_cmds_addrs[2] = 0x5000c;

	msg->ddr_cmds_data[0][0] =  0x40000000;
	msg->ddr_cmds_data[0][1] =  0x40000000;
	msg->ddr_cmds_data[0][2] =  0x40000000;

	/*
	 * These are the CX (CNOC) votes - these are used by the GMU but the
	 * votes are known and fixed for the target
	 */
	msg->cnoc_cmds_num = 1;
	msg->cnoc_wait_bitmask = 0x01;

	msg->cnoc_cmds_addrs[0] = 0x5007c;
	msg->cnoc_cmds_data[0][0] =  0x40000000;
	msg->cnoc_cmds_data[1][0] =  0x60000001;
}

static void a619_build_bw_table(struct a6xx_hfi_msg_bw_table *msg)
{
	msg->bw_level_num = 13;

	msg->ddr_cmds_num = 3;
	msg->ddr_wait_bitmask = 0x0;

	msg->ddr_cmds_addrs[0] = 0x50000;
	msg->ddr_cmds_addrs[1] = 0x50004;
	msg->ddr_cmds_addrs[2] = 0x50080;

	msg->ddr_cmds_data[0][0]  = 0x40000000;
	msg->ddr_cmds_data[0][1]  = 0x40000000;
	msg->ddr_cmds_data[0][2]  = 0x40000000;
	msg->ddr_cmds_data[1][0]  = 0x6000030c;
	msg->ddr_cmds_data[1][1]  = 0x600000db;
	msg->ddr_cmds_data[1][2]  = 0x60000008;
	msg->ddr_cmds_data[2][0]  = 0x60000618;
	msg->ddr_cmds_data[2][1]  = 0x600001b6;
	msg->ddr_cmds_data[2][2]  = 0x60000008;
	msg->ddr_cmds_data[3][0]  = 0x60000925;
	msg->ddr_cmds_data[3][1]  = 0x60000291;
	msg->ddr_cmds_data[3][2]  = 0x60000008;
	msg->ddr_cmds_data[4][0]  = 0x60000dc1;
	msg->ddr_cmds_data[4][1]  = 0x600003dc;
	msg->ddr_cmds_data[4][2]  = 0x60000008;
	msg->ddr_cmds_data[5][0]  = 0x600010ad;
	msg->ddr_cmds_data[5][1]  = 0x600004ae;
	msg->ddr_cmds_data[5][2]  = 0x60000008;
	msg->ddr_cmds_data[6][0]  = 0x600014c3;
	msg->ddr_cmds_data[6][1]  = 0x600005d4;
	msg->ddr_cmds_data[6][2]  = 0x60000008;
	msg->ddr_cmds_data[7][0]  = 0x6000176a;
	msg->ddr_cmds_data[7][1]  = 0x60000693;
	msg->ddr_cmds_data[7][2]  = 0x60000008;
	msg->ddr_cmds_data[8][0]  = 0x60001f01;
	msg->ddr_cmds_data[8][1]  = 0x600008b5;
	msg->ddr_cmds_data[8][2]  = 0x60000008;
	msg->ddr_cmds_data[9][0]  = 0x60002940;
	msg->ddr_cmds_data[9][1]  = 0x60000b95;
	msg->ddr_cmds_data[9][2]  = 0x60000008;
	msg->ddr_cmds_data[10][0] = 0x60002f68;
	msg->ddr_cmds_data[10][1] = 0x60000d50;
	msg->ddr_cmds_data[10][2] = 0x60000008;
	msg->ddr_cmds_data[11][0] = 0x60003700;
	msg->ddr_cmds_data[11][1] = 0x60000f71;
	msg->ddr_cmds_data[11][2] = 0x60000008;
	msg->ddr_cmds_data[12][0] = 0x60003fce;
	msg->ddr_cmds_data[12][1] = 0x600011ea;
	msg->ddr_cmds_data[12][2] = 0x60000008;

	msg->cnoc_cmds_num = 1;
	msg->cnoc_wait_bitmask = 0x0;

	msg->cnoc_cmds_addrs[0] = 0x50054;

	msg->cnoc_cmds_data[0][0] = 0x40000000;
}

static void a640_build_bw_table(struct a6xx_hfi_msg_bw_table *msg)
{
	/*
	 * Send a single "off" entry just to get things running
	 * TODO: bus scaling
	 */
	msg->bw_level_num = 1;

	msg->ddr_cmds_num = 3;
	msg->ddr_wait_bitmask = 0x01;

	msg->ddr_cmds_addrs[0] = 0x50000;
	msg->ddr_cmds_addrs[1] = 0x5003c;
	msg->ddr_cmds_addrs[2] = 0x5000c;

	msg->ddr_cmds_data[0][0] =  0x40000000;
	msg->ddr_cmds_data[0][1] =  0x40000000;
	msg->ddr_cmds_data[0][2] =  0x40000000;

	/*
	 * These are the CX (CNOC) votes - these are used by the GMU but the
	 * votes are known and fixed for the target
	 */
	msg->cnoc_cmds_num = 3;
	msg->cnoc_wait_bitmask = 0x01;

	msg->cnoc_cmds_addrs[0] = 0x50034;
	msg->cnoc_cmds_addrs[1] = 0x5007c;
	msg->cnoc_cmds_addrs[2] = 0x5004c;

	msg->cnoc_cmds_data[0][0] =  0x40000000;
	msg->cnoc_cmds_data[0][1] =  0x00000000;
	msg->cnoc_cmds_data[0][2] =  0x40000000;

	msg->cnoc_cmds_data[1][0] =  0x60000001;
	msg->cnoc_cmds_data[1][1] =  0x20000001;
	msg->cnoc_cmds_data[1][2] =  0x60000001;
}

static void a650_build_bw_table(struct a6xx_hfi_msg_bw_table *msg)
{
	/*
	 * Send a single "off" entry just to get things running
	 * TODO: bus scaling
	 */
	msg->bw_level_num = 1;

	msg->ddr_cmds_num = 3;
	msg->ddr_wait_bitmask = 0x01;

	msg->ddr_cmds_addrs[0] = 0x50000;
	msg->ddr_cmds_addrs[1] = 0x50004;
	msg->ddr_cmds_addrs[2] = 0x5007c;

	msg->ddr_cmds_data[0][0] =  0x40000000;
	msg->ddr_cmds_data[0][1] =  0x40000000;
	msg->ddr_cmds_data[0][2] =  0x40000000;

	/*
	 * These are the CX (CNOC) votes - these are used by the GMU but the
	 * votes are known and fixed for the target
	 */
	msg->cnoc_cmds_num = 1;
	msg->cnoc_wait_bitmask = 0x01;

	msg->cnoc_cmds_addrs[0] = 0x500a4;
	msg->cnoc_cmds_data[0][0] =  0x40000000;
	msg->cnoc_cmds_data[1][0] =  0x60000001;
}

static void a690_build_bw_table(struct a6xx_hfi_msg_bw_table *msg)
{
	/*
	 * Send a single "off" entry just to get things running
	 * TODO: bus scaling
	 */
	msg->bw_level_num = 1;

	msg->ddr_cmds_num = 3;
	msg->ddr_wait_bitmask = 0x01;

	msg->ddr_cmds_addrs[0] = 0x50004;
	msg->ddr_cmds_addrs[1] = 0x50000;
	msg->ddr_cmds_addrs[2] = 0x500ac;

	msg->ddr_cmds_data[0][0] =  0x40000000;
	msg->ddr_cmds_data[0][1] =  0x40000000;
	msg->ddr_cmds_data[0][2] =  0x40000000;

	/*
	 * These are the CX (CNOC) votes - these are used by the GMU but the
	 * votes are known and fixed for the target
	 */
	msg->cnoc_cmds_num = 1;
	msg->cnoc_wait_bitmask = 0x01;

	msg->cnoc_cmds_addrs[0] = 0x5003c;
	msg->cnoc_cmds_data[0][0] =  0x40000000;
	msg->cnoc_cmds_data[1][0] =  0x60000001;
}

static void a660_build_bw_table(struct a6xx_hfi_msg_bw_table *msg)
{
	/*
	 * Send a single "off" entry just to get things running
	 * TODO: bus scaling
	 */
	msg->bw_level_num = 1;

	msg->ddr_cmds_num = 3;
	msg->ddr_wait_bitmask = 0x01;

	msg->ddr_cmds_addrs[0] = 0x50004;
	msg->ddr_cmds_addrs[1] = 0x500a0;
	msg->ddr_cmds_addrs[2] = 0x50000;

	msg->ddr_cmds_data[0][0] =  0x40000000;
	msg->ddr_cmds_data[0][1] =  0x40000000;
	msg->ddr_cmds_data[0][2] =  0x40000000;

	/*
	 * These are the CX (CNOC) votes - these are used by the GMU but the
	 * votes are known and fixed for the target
	 */
	msg->cnoc_cmds_num = 1;
	msg->cnoc_wait_bitmask = 0x01;

	msg->cnoc_cmds_addrs[0] = 0x50070;
	msg->cnoc_cmds_data[0][0] =  0x40000000;
	msg->cnoc_cmds_data[1][0] =  0x60000001;
}

static void a663_build_bw_table(struct a6xx_hfi_msg_bw_table *msg)
{
	/*
	 * Send a single "off" entry just to get things running
	 * TODO: bus scaling
	 */
	msg->bw_level_num = 1;

	msg->ddr_cmds_num = 3;
	msg->ddr_wait_bitmask = 0x07;

	msg->ddr_cmds_addrs[0] = 0x50004;
	msg->ddr_cmds_addrs[1] = 0x50000;
	msg->ddr_cmds_addrs[2] = 0x500b4;

	msg->ddr_cmds_data[0][0] =  0x40000000;
	msg->ddr_cmds_data[0][1] =  0x40000000;
	msg->ddr_cmds_data[0][2] =  0x40000000;

	/*
	 * These are the CX (CNOC) votes - these are used by the GMU but the
	 * votes are known and fixed for the target
	 */
	msg->cnoc_cmds_num = 1;
	msg->cnoc_wait_bitmask = 0x01;

	msg->cnoc_cmds_addrs[0] = 0x50058;
	msg->cnoc_cmds_data[0][0] =  0x40000000;
	msg->cnoc_cmds_data[1][0] =  0x60000001;
}

static void adreno_7c3_build_bw_table(struct a6xx_hfi_msg_bw_table *msg)
{
	/*
	 * Send a single "off" entry just to get things running
	 * TODO: bus scaling
	 */
	msg->bw_level_num = 1;

	msg->ddr_cmds_num = 3;
	msg->ddr_wait_bitmask = 0x07;

	msg->ddr_cmds_addrs[0] = 0x50004;
	msg->ddr_cmds_addrs[1] = 0x50000;
	msg->ddr_cmds_addrs[2] = 0x50088;

	msg->ddr_cmds_data[0][0] =  0x40000000;
	msg->ddr_cmds_data[0][1] =  0x40000000;
	msg->ddr_cmds_data[0][2] =  0x40000000;

	/*
	 * These are the CX (CNOC) votes - these are used by the GMU but the
	 * votes are known and fixed for the target
	 */
	msg->cnoc_cmds_num = 1;
	msg->cnoc_wait_bitmask = 0x01;

	msg->cnoc_cmds_addrs[0] = 0x5006c;
	msg->cnoc_cmds_data[0][0] =  0x40000000;
	msg->cnoc_cmds_data[1][0] =  0x60000001;
}

static void a730_build_bw_table(struct a6xx_hfi_msg_bw_table *msg)
{
	msg->bw_level_num = 12;

	msg->ddr_cmds_num = 3;
	msg->ddr_wait_bitmask = 0x7;

	msg->ddr_cmds_addrs[0] = cmd_db_read_addr("SH0");
	msg->ddr_cmds_addrs[1] = cmd_db_read_addr("MC0");
	msg->ddr_cmds_addrs[2] = cmd_db_read_addr("ACV");

	msg->ddr_cmds_data[0][0] = 0x40000000;
	msg->ddr_cmds_data[0][1] = 0x40000000;
	msg->ddr_cmds_data[0][2] = 0x40000000;
	msg->ddr_cmds_data[1][0] = 0x600002e8;
	msg->ddr_cmds_data[1][1] = 0x600003d0;
	msg->ddr_cmds_data[1][2] = 0x60000008;
	msg->ddr_cmds_data[2][0] = 0x6000068d;
	msg->ddr_cmds_data[2][1] = 0x6000089a;
	msg->ddr_cmds_data[2][2] = 0x60000008;
	msg->ddr_cmds_data[3][0] = 0x600007f2;
	msg->ddr_cmds_data[3][1] = 0x60000a6e;
	msg->ddr_cmds_data[3][2] = 0x60000008;
	msg->ddr_cmds_data[4][0] = 0x600009e5;
	msg->ddr_cmds_data[4][1] = 0x60000cfd;
	msg->ddr_cmds_data[4][2] = 0x60000008;
	msg->ddr_cmds_data[5][0] = 0x60000b29;
	msg->ddr_cmds_data[5][1] = 0x60000ea6;
	msg->ddr_cmds_data[5][2] = 0x60000008;
	msg->ddr_cmds_data[6][0] = 0x60001698;
	msg->ddr_cmds_data[6][1] = 0x60001da8;
	msg->ddr_cmds_data[6][2] = 0x60000008;
	msg->ddr_cmds_data[7][0] = 0x600018d2;
	msg->ddr_cmds_data[7][1] = 0x60002093;
	msg->ddr_cmds_data[7][2] = 0x60000008;
	msg->ddr_cmds_data[8][0] = 0x60001e66;
	msg->ddr_cmds_data[8][1] = 0x600027e6;
	msg->ddr_cmds_data[8][2] = 0x60000008;
	msg->ddr_cmds_data[9][0] = 0x600027c2;
	msg->ddr_cmds_data[9][1] = 0x6000342f;
	msg->ddr_cmds_data[9][2] = 0x60000008;
	msg->ddr_cmds_data[10][0] = 0x60002e71;
	msg->ddr_cmds_data[10][1] = 0x60003cf5;
	msg->ddr_cmds_data[10][2] = 0x60000008;
	msg->ddr_cmds_data[11][0] = 0x600030ae;
	msg->ddr_cmds_data[11][1] = 0x60003fe5;
	msg->ddr_cmds_data[11][2] = 0x60000008;

	msg->cnoc_cmds_num = 1;
	msg->cnoc_wait_bitmask = 0x1;

	msg->cnoc_cmds_addrs[0] = cmd_db_read_addr("CN0");
	msg->cnoc_cmds_data[0][0] = 0x40000000;
	msg->cnoc_cmds_data[1][0] = 0x60000001;
}

static void a740_build_bw_table(struct a6xx_hfi_msg_bw_table *msg)
{
	msg->bw_level_num = 1;

	msg->ddr_cmds_num = 3;
	msg->ddr_wait_bitmask = 0x7;

	msg->ddr_cmds_addrs[0] = cmd_db_read_addr("SH0");
	msg->ddr_cmds_addrs[1] = cmd_db_read_addr("MC0");
	msg->ddr_cmds_addrs[2] = cmd_db_read_addr("ACV");

	msg->ddr_cmds_data[0][0] = 0x40000000;
	msg->ddr_cmds_data[0][1] = 0x40000000;
	msg->ddr_cmds_data[0][2] = 0x40000000;

	/* TODO: add a proper dvfs table */

	msg->cnoc_cmds_num = 1;
	msg->cnoc_wait_bitmask = 0x1;

	msg->cnoc_cmds_addrs[0] = cmd_db_read_addr("CN0");
	msg->cnoc_cmds_data[0][0] = 0x40000000;
	msg->cnoc_cmds_data[1][0] = 0x60000001;
}

static void a6xx_build_bw_table(struct a6xx_hfi_msg_bw_table *msg)
{
	/* Send a single "off" entry since the 630 GMU doesn't do bus scaling */
	msg->bw_level_num = 1;

	msg->ddr_cmds_num = 3;
	msg->ddr_wait_bitmask = 0x07;

	msg->ddr_cmds_addrs[0] = 0x50000;
	msg->ddr_cmds_addrs[1] = 0x5005c;
	msg->ddr_cmds_addrs[2] = 0x5000c;

	msg->ddr_cmds_data[0][0] =  0x40000000;
	msg->ddr_cmds_data[0][1] =  0x40000000;
	msg->ddr_cmds_data[0][2] =  0x40000000;

	/*
	 * These are the CX (CNOC) votes.  This is used but the values for the
	 * sdm845 GMU are known and fixed so we can hard code them.
	 */

	msg->cnoc_cmds_num = 3;
	msg->cnoc_wait_bitmask = 0x05;

	msg->cnoc_cmds_addrs[0] = 0x50034;
	msg->cnoc_cmds_addrs[1] = 0x5007c;
	msg->cnoc_cmds_addrs[2] = 0x5004c;

	msg->cnoc_cmds_data[0][0] =  0x40000000;
	msg->cnoc_cmds_data[0][1] =  0x00000000;
	msg->cnoc_cmds_data[0][2] =  0x40000000;

	msg->cnoc_cmds_data[1][0] =  0x60000001;
	msg->cnoc_cmds_data[1][1] =  0x20000001;
	msg->cnoc_cmds_data[1][2] =  0x60000001;
}


static int a6xx_hfi_send_bw_table(struct a6xx_gmu *gmu)
{
	struct a6xx_hfi_msg_bw_table *msg;
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	const struct a6xx_info *info = adreno_gpu->info->a6xx;

	if (gmu->bw_table)
		goto send;

	msg = devm_kzalloc(gmu->dev, sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	if (info->bcms && gmu->nr_gpu_bws > 1)
		a6xx_generate_bw_table(info, gmu, msg);
	else if (adreno_is_a618(adreno_gpu))
		a618_build_bw_table(msg);
	else if (adreno_is_a619(adreno_gpu))
		a619_build_bw_table(msg);
	else if (adreno_is_a640_family(adreno_gpu))
		a640_build_bw_table(msg);
	else if (adreno_is_a650(adreno_gpu))
		a650_build_bw_table(msg);
	else if (adreno_is_7c3(adreno_gpu))
		adreno_7c3_build_bw_table(msg);
	else if (adreno_is_a660(adreno_gpu))
		a660_build_bw_table(msg);
	else if (adreno_is_a663(adreno_gpu))
		a663_build_bw_table(msg);
	else if (adreno_is_a690(adreno_gpu))
		a690_build_bw_table(msg);
	else if (adreno_is_a730(adreno_gpu))
		a730_build_bw_table(msg);
	else if (adreno_is_a740_family(adreno_gpu))
		a740_build_bw_table(msg);
	else
		a6xx_build_bw_table(msg);

	gmu->bw_table = msg;

send:
	return a6xx_hfi_send_msg(gmu, HFI_H2F_MSG_BW_TABLE, gmu->bw_table, sizeof(*(gmu->bw_table)),
		NULL, 0);
}

static int a6xx_hfi_send_test(struct a6xx_gmu *gmu)
{
	struct a6xx_hfi_msg_test msg = { 0 };

	return a6xx_hfi_send_msg(gmu, HFI_H2F_MSG_TEST, &msg, sizeof(msg),
		NULL, 0);
}

static int a6xx_hfi_send_start(struct a6xx_gmu *gmu)
{
	struct a6xx_hfi_msg_start msg = { 0 };

	return a6xx_hfi_send_msg(gmu, HFI_H2F_MSG_START, &msg, sizeof(msg),
		NULL, 0);
}

static int a6xx_hfi_send_core_fw_start(struct a6xx_gmu *gmu)
{
	struct a6xx_hfi_msg_core_fw_start msg = { 0 };

	return a6xx_hfi_send_msg(gmu, HFI_H2F_MSG_CORE_FW_START, &msg,
		sizeof(msg), NULL, 0);
}

int a6xx_hfi_set_freq(struct a6xx_gmu *gmu, u32 freq_index, u32 bw_index)
{
	struct a6xx_hfi_gx_bw_perf_vote_cmd msg = { 0 };

	msg.ack_type = 1; /* blocking */
	msg.freq = freq_index;
	msg.bw = bw_index;

	return a6xx_hfi_send_msg(gmu, HFI_H2F_MSG_GX_BW_PERF_VOTE, &msg,
		sizeof(msg), NULL, 0);
}

int a6xx_hfi_send_prep_slumber(struct a6xx_gmu *gmu)
{
	struct a6xx_hfi_prep_slumber_cmd msg = { 0 };

	/* TODO: should freq and bw fields be non-zero ? */

	return a6xx_hfi_send_msg(gmu, HFI_H2F_MSG_PREPARE_SLUMBER, &msg,
		sizeof(msg), NULL, 0);
}

static int a6xx_hfi_start_v1(struct a6xx_gmu *gmu, int boot_state)
{
	int ret;

	ret = a6xx_hfi_send_gmu_init(gmu, boot_state);
	if (ret)
		return ret;

	ret = a6xx_hfi_get_fw_version(gmu, NULL);
	if (ret)
		return ret;

	/*
	 * We have to get exchange version numbers per the sequence but at this
	 * point th kernel driver doesn't need to know the exact version of
	 * the GMU firmware
	 */

	ret = a6xx_hfi_send_perf_table_v1(gmu);
	if (ret)
		return ret;

	ret = a6xx_hfi_send_bw_table(gmu);
	if (ret)
		return ret;

	/*
	 * Let the GMU know that there won't be any more HFI messages until next
	 * boot
	 */
	a6xx_hfi_send_test(gmu);

	return 0;
}

int a6xx_hfi_start(struct a6xx_gmu *gmu, int boot_state)
{
	int ret;

	if (gmu->legacy)
		return a6xx_hfi_start_v1(gmu, boot_state);


	ret = a6xx_hfi_send_perf_table(gmu);
	if (ret)
		return ret;

	ret = a6xx_hfi_send_bw_table(gmu);
	if (ret)
		return ret;

	ret = a6xx_hfi_send_core_fw_start(gmu);
	if (ret)
		return ret;

	/*
	 * Downstream driver sends this in its "a6xx_hw_init" equivalent,
	 * but seems to be no harm in sending it here
	 */
	ret = a6xx_hfi_send_start(gmu);
	if (ret)
		return ret;

	return 0;
}

void a6xx_hfi_stop(struct a6xx_gmu *gmu)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(gmu->queues); i++) {
		struct a6xx_hfi_queue *queue = &gmu->queues[i];

		if (!queue->header)
			continue;

		if (queue->header->read_index != queue->header->write_index)
			DRM_DEV_ERROR(gmu->dev, "HFI queue %d is not empty\n", i);

		queue->header->read_index = 0;
		queue->header->write_index = 0;

		memset(&queue->history, 0xff, sizeof(queue->history));
		queue->history_idx = 0;
	}
}

static void a6xx_hfi_queue_init(struct a6xx_hfi_queue *queue,
		struct a6xx_hfi_queue_header *header, void *virt, u64 iova,
		u32 id)
{
	spin_lock_init(&queue->lock);
	queue->header = header;
	queue->data = virt;
	atomic_set(&queue->seqnum, 0);

	memset(&queue->history, 0xff, sizeof(queue->history));
	queue->history_idx = 0;

	/* Set up the shared memory header */
	header->iova = iova;
	header->type =  10 << 8 | id;
	header->status = 1;
	header->size = SZ_4K >> 2;
	header->msg_size = 0;
	header->dropped = 0;
	header->rx_watermark = 1;
	header->tx_watermark = 1;
	header->rx_request = 1;
	header->tx_request = 0;
	header->read_index = 0;
	header->write_index = 0;
}

void a6xx_hfi_init(struct a6xx_gmu *gmu)
{
	struct a6xx_gmu_bo *hfi = &gmu->hfi;
	struct a6xx_hfi_queue_table_header *table = hfi->virt;
	struct a6xx_hfi_queue_header *headers = hfi->virt + sizeof(*table);
	u64 offset;
	int table_size;

	/*
	 * The table size is the size of the table header plus all of the queue
	 * headers
	 */
	table_size = sizeof(*table);
	table_size += (ARRAY_SIZE(gmu->queues) *
		sizeof(struct a6xx_hfi_queue_header));

	table->version = 0;
	table->size = table_size;
	/* First queue header is located immediately after the table header */
	table->qhdr0_offset = sizeof(*table) >> 2;
	table->qhdr_size = sizeof(struct a6xx_hfi_queue_header) >> 2;
	table->num_queues = ARRAY_SIZE(gmu->queues);
	table->active_queues = ARRAY_SIZE(gmu->queues);

	/* Command queue */
	offset = SZ_4K;
	a6xx_hfi_queue_init(&gmu->queues[0], &headers[0], hfi->virt + offset,
		hfi->iova + offset, 0);

	/* GMU response queue */
	offset += SZ_4K;
	a6xx_hfi_queue_init(&gmu->queues[1], &headers[1], hfi->virt + offset,
		hfi->iova + offset, gmu->legacy ? 4 : 1);
}
