// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#include <linux/types.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "qed.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_init_ops.h"
#include "qed_reg_addr.h"
#include "qed_sriov.h"

#define QED_INIT_MAX_POLL_COUNT 100
#define QED_INIT_POLL_PERIOD_US 500

static u32 pxp_global_win[] = {
	0,
	0,
	0x1c02, /* win 2: addr=0x1c02000, size=4096 bytes */
	0x1c80, /* win 3: addr=0x1c80000, size=4096 bytes */
	0x1d00, /* win 4: addr=0x1d00000, size=4096 bytes */
	0x1d01, /* win 5: addr=0x1d01000, size=4096 bytes */
	0x1d02, /* win 6: addr=0x1d02000, size=4096 bytes */
	0x1d80, /* win 7: addr=0x1d80000, size=4096 bytes */
	0x1d81, /* win 8: addr=0x1d81000, size=4096 bytes */
	0x1d82, /* win 9: addr=0x1d82000, size=4096 bytes */
	0x1e00, /* win 10: addr=0x1e00000, size=4096 bytes */
	0x1e01, /* win 11: addr=0x1e01000, size=4096 bytes */
	0x1e80, /* win 12: addr=0x1e80000, size=4096 bytes */
	0x1f00, /* win 13: addr=0x1f00000, size=4096 bytes */
	0x1c08, /* win 14: addr=0x1c08000, size=4096 bytes */
	0,
	0,
	0,
	0,
};

/* IRO Array */
static const u32 iro_arr[] = {
	0x00000000, 0x00000000, 0x00080000,
	0x00003288, 0x00000088, 0x00880000,
	0x000058e8, 0x00000020, 0x00200000,
	0x00000b00, 0x00000008, 0x00040000,
	0x00000a80, 0x00000008, 0x00040000,
	0x00000000, 0x00000008, 0x00020000,
	0x00000080, 0x00000008, 0x00040000,
	0x00000084, 0x00000008, 0x00020000,
	0x00005718, 0x00000004, 0x00040000,
	0x00004dd0, 0x00000000, 0x00780000,
	0x00003e40, 0x00000000, 0x00780000,
	0x00004480, 0x00000000, 0x00780000,
	0x00003210, 0x00000000, 0x00780000,
	0x00003b50, 0x00000000, 0x00780000,
	0x00007f58, 0x00000000, 0x00780000,
	0x00005f58, 0x00000000, 0x00080000,
	0x00007100, 0x00000000, 0x00080000,
	0x0000aea0, 0x00000000, 0x00080000,
	0x00004398, 0x00000000, 0x00080000,
	0x0000a5a0, 0x00000000, 0x00080000,
	0x0000bde8, 0x00000000, 0x00080000,
	0x00000020, 0x00000004, 0x00040000,
	0x000056c8, 0x00000010, 0x00100000,
	0x0000c210, 0x00000030, 0x00300000,
	0x0000b088, 0x00000038, 0x00380000,
	0x00003d20, 0x00000080, 0x00400000,
	0x0000bf60, 0x00000000, 0x00040000,
	0x00004560, 0x00040080, 0x00040000,
	0x000001f8, 0x00000004, 0x00040000,
	0x00003d60, 0x00000080, 0x00200000,
	0x00008960, 0x00000040, 0x00300000,
	0x0000e840, 0x00000060, 0x00600000,
	0x00004618, 0x00000080, 0x00380000,
	0x00010738, 0x000000c0, 0x00c00000,
	0x000001f8, 0x00000002, 0x00020000,
	0x0000a2a0, 0x00000000, 0x01080000,
	0x0000a3a8, 0x00000008, 0x00080000,
	0x000001c0, 0x00000008, 0x00080000,
	0x000001f8, 0x00000008, 0x00080000,
	0x00000ac0, 0x00000008, 0x00080000,
	0x00002578, 0x00000008, 0x00080000,
	0x000024f8, 0x00000008, 0x00080000,
	0x00000280, 0x00000008, 0x00080000,
	0x00000680, 0x00080018, 0x00080000,
	0x00000b78, 0x00080018, 0x00020000,
	0x0000c640, 0x00000050, 0x003c0000,
	0x00012038, 0x00000018, 0x00100000,
	0x00011b00, 0x00000040, 0x00180000,
	0x000095d0, 0x00000050, 0x00200000,
	0x00008b10, 0x00000040, 0x00280000,
	0x00011640, 0x00000018, 0x00100000,
	0x0000c828, 0x00000048, 0x00380000,
	0x00011710, 0x00000020, 0x00200000,
	0x00004650, 0x00000080, 0x00100000,
	0x00003618, 0x00000010, 0x00100000,
	0x0000a968, 0x00000008, 0x00010000,
	0x000097a0, 0x00000008, 0x00010000,
	0x00011990, 0x00000008, 0x00010000,
	0x0000f018, 0x00000008, 0x00010000,
	0x00012628, 0x00000008, 0x00010000,
	0x00011da8, 0x00000008, 0x00010000,
	0x0000aa78, 0x00000030, 0x00100000,
	0x0000d768, 0x00000028, 0x00280000,
	0x00009a58, 0x00000018, 0x00180000,
	0x00009bd8, 0x00000008, 0x00080000,
	0x00013a18, 0x00000008, 0x00080000,
	0x000126e8, 0x00000018, 0x00180000,
	0x0000e608, 0x00500288, 0x00100000,
	0x00012970, 0x00000138, 0x00280000,
};

void qed_init_iro_array(struct qed_dev *cdev)
{
	cdev->iro_arr = iro_arr;
}

void qed_init_store_rt_reg(struct qed_hwfn *p_hwfn, u32 rt_offset, u32 val)
{
	p_hwfn->rt_data.init_val[rt_offset] = val;
	p_hwfn->rt_data.b_valid[rt_offset] = true;
}

void qed_init_store_rt_agg(struct qed_hwfn *p_hwfn,
			   u32 rt_offset, u32 *p_val, size_t size)
{
	size_t i;

	for (i = 0; i < size / sizeof(u32); i++) {
		p_hwfn->rt_data.init_val[rt_offset + i] = p_val[i];
		p_hwfn->rt_data.b_valid[rt_offset + i]	= true;
	}
}

static int qed_init_rt(struct qed_hwfn	*p_hwfn,
		       struct qed_ptt *p_ptt,
		       u32 addr, u16 rt_offset, u16 size, bool b_must_dmae)
{
	u32 *p_init_val = &p_hwfn->rt_data.init_val[rt_offset];
	bool *p_valid = &p_hwfn->rt_data.b_valid[rt_offset];
	u16 i, j, segment;
	int rc = 0;

	/* Since not all RT entries are initialized, go over the RT and
	 * for each segment of initialized values use DMA.
	 */
	for (i = 0; i < size; i++) {
		if (!p_valid[i])
			continue;

		/* In case there isn't any wide-bus configuration here,
		 * simply write the data instead of using dmae.
		 */
		if (!b_must_dmae) {
			qed_wr(p_hwfn, p_ptt, addr + (i << 2), p_init_val[i]);
			p_valid[i] = false;
			continue;
		}

		/* Start of a new segment */
		for (segment = 1; i + segment < size; segment++)
			if (!p_valid[i + segment])
				break;

		rc = qed_dmae_host2grc(p_hwfn, p_ptt,
				       (uintptr_t)(p_init_val + i),
				       addr + (i << 2), segment, NULL);
		if (rc)
			return rc;

		/* invalidate after writing */
		for (j = i; j < i + segment; j++)
			p_valid[j] = false;

		/* Jump over the entire segment, including invalid entry */
		i += segment;
	}

	return rc;
}

int qed_init_alloc(struct qed_hwfn *p_hwfn)
{
	struct qed_rt_data *rt_data = &p_hwfn->rt_data;

	if (IS_VF(p_hwfn->cdev))
		return 0;

	rt_data->b_valid = kcalloc(RUNTIME_ARRAY_SIZE, sizeof(bool),
				   GFP_KERNEL);
	if (!rt_data->b_valid)
		return -ENOMEM;

	rt_data->init_val = kcalloc(RUNTIME_ARRAY_SIZE, sizeof(u32),
				    GFP_KERNEL);
	if (!rt_data->init_val) {
		kfree(rt_data->b_valid);
		rt_data->b_valid = NULL;
		return -ENOMEM;
	}

	return 0;
}

void qed_init_free(struct qed_hwfn *p_hwfn)
{
	kfree(p_hwfn->rt_data.init_val);
	p_hwfn->rt_data.init_val = NULL;
	kfree(p_hwfn->rt_data.b_valid);
	p_hwfn->rt_data.b_valid = NULL;
}

static int qed_init_array_dmae(struct qed_hwfn *p_hwfn,
			       struct qed_ptt *p_ptt,
			       u32 addr,
			       u32 dmae_data_offset,
			       u32 size,
			       const u32 *buf,
			       bool b_must_dmae,
			       bool b_can_dmae)
{
	int rc = 0;

	/* Perform DMAE only for lengthy enough sections or for wide-bus */
	if (!b_can_dmae || (!b_must_dmae && (size < 16))) {
		const u32 *data = buf + dmae_data_offset;
		u32 i;

		for (i = 0; i < size; i++)
			qed_wr(p_hwfn, p_ptt, addr + (i << 2), data[i]);
	} else {
		rc = qed_dmae_host2grc(p_hwfn, p_ptt,
				       (uintptr_t)(buf + dmae_data_offset),
				       addr, size, NULL);
	}

	return rc;
}

static int qed_init_fill_dmae(struct qed_hwfn *p_hwfn,
			      struct qed_ptt *p_ptt,
			      u32 addr, u32 fill, u32 fill_count)
{
	static u32 zero_buffer[DMAE_MAX_RW_SIZE];
	struct qed_dmae_params params = {};

	memset(zero_buffer, 0, sizeof(u32) * DMAE_MAX_RW_SIZE);

	/* invoke the DMAE virtual/physical buffer API with
	 * 1. DMAE init channel
	 * 2. addr,
	 * 3. p_hwfb->temp_data,
	 * 4. fill_count
	 */
	SET_FIELD(params.flags, QED_DMAE_PARAMS_RW_REPL_SRC, 0x1);
	return qed_dmae_host2grc(p_hwfn, p_ptt,
				 (uintptr_t)(&zero_buffer[0]),
				 addr, fill_count, &params);
}

static void qed_init_fill(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt,
			  u32 addr, u32 fill, u32 fill_count)
{
	u32 i;

	for (i = 0; i < fill_count; i++, addr += sizeof(u32))
		qed_wr(p_hwfn, p_ptt, addr, fill);
}

static int qed_init_cmd_array(struct qed_hwfn *p_hwfn,
			      struct qed_ptt *p_ptt,
			      struct init_write_op *cmd,
			      bool b_must_dmae, bool b_can_dmae)
{
	u32 dmae_array_offset = le32_to_cpu(cmd->args.array_offset);
	u32 data = le32_to_cpu(cmd->data);
	u32 addr = GET_FIELD(data, INIT_WRITE_OP_ADDRESS) << 2;

	u32 offset, output_len, input_len, max_size;
	struct qed_dev *cdev = p_hwfn->cdev;
	union init_array_hdr *hdr;
	const u32 *array_data;
	int rc = 0;
	u32 size;

	array_data = cdev->fw_data->arr_data;

	hdr = (union init_array_hdr *)(array_data + dmae_array_offset);
	data = le32_to_cpu(hdr->raw.data);
	switch (GET_FIELD(data, INIT_ARRAY_RAW_HDR_TYPE)) {
	case INIT_ARR_ZIPPED:
		offset = dmae_array_offset + 1;
		input_len = GET_FIELD(data,
				      INIT_ARRAY_ZIPPED_HDR_ZIPPED_SIZE);
		max_size = MAX_ZIPPED_SIZE * 4;
		memset(p_hwfn->unzip_buf, 0, max_size);

		output_len = qed_unzip_data(p_hwfn, input_len,
					    (u8 *)&array_data[offset],
					    max_size, (u8 *)p_hwfn->unzip_buf);
		if (output_len) {
			rc = qed_init_array_dmae(p_hwfn, p_ptt, addr, 0,
						 output_len,
						 p_hwfn->unzip_buf,
						 b_must_dmae, b_can_dmae);
		} else {
			DP_NOTICE(p_hwfn, "Failed to unzip dmae data\n");
			rc = -EINVAL;
		}
		break;
	case INIT_ARR_PATTERN:
	{
		u32 repeats = GET_FIELD(data,
					INIT_ARRAY_PATTERN_HDR_REPETITIONS);
		u32 i;

		size = GET_FIELD(data, INIT_ARRAY_PATTERN_HDR_PATTERN_SIZE);

		for (i = 0; i < repeats; i++, addr += size << 2) {
			rc = qed_init_array_dmae(p_hwfn, p_ptt, addr,
						 dmae_array_offset + 1,
						 size, array_data,
						 b_must_dmae, b_can_dmae);
			if (rc)
				break;
		}
		break;
	}
	case INIT_ARR_STANDARD:
		size = GET_FIELD(data, INIT_ARRAY_STANDARD_HDR_SIZE);
		rc = qed_init_array_dmae(p_hwfn, p_ptt, addr,
					 dmae_array_offset + 1,
					 size, array_data,
					 b_must_dmae, b_can_dmae);
		break;
	}

	return rc;
}

/* init_ops write command */
static int qed_init_cmd_wr(struct qed_hwfn *p_hwfn,
			   struct qed_ptt *p_ptt,
			   struct init_write_op *p_cmd, bool b_can_dmae)
{
	u32 data = le32_to_cpu(p_cmd->data);
	bool b_must_dmae = GET_FIELD(data, INIT_WRITE_OP_WIDE_BUS);
	u32 addr = GET_FIELD(data, INIT_WRITE_OP_ADDRESS) << 2;
	union init_write_args *arg = &p_cmd->args;
	int rc = 0;

	/* Sanitize */
	if (b_must_dmae && !b_can_dmae) {
		DP_NOTICE(p_hwfn,
			  "Need to write to %08x for Wide-bus but DMAE isn't allowed\n",
			  addr);
		return -EINVAL;
	}

	switch (GET_FIELD(data, INIT_WRITE_OP_SOURCE)) {
	case INIT_SRC_INLINE:
		data = le32_to_cpu(p_cmd->args.inline_val);
		qed_wr(p_hwfn, p_ptt, addr, data);
		break;
	case INIT_SRC_ZEROS:
		data = le32_to_cpu(p_cmd->args.zeros_count);
		if (b_must_dmae || (b_can_dmae && (data >= 64)))
			rc = qed_init_fill_dmae(p_hwfn, p_ptt, addr, 0, data);
		else
			qed_init_fill(p_hwfn, p_ptt, addr, 0, data);
		break;
	case INIT_SRC_ARRAY:
		rc = qed_init_cmd_array(p_hwfn, p_ptt, p_cmd,
					b_must_dmae, b_can_dmae);
		break;
	case INIT_SRC_RUNTIME:
		qed_init_rt(p_hwfn, p_ptt, addr,
			    le16_to_cpu(arg->runtime.offset),
			    le16_to_cpu(arg->runtime.size),
			    b_must_dmae);
		break;
	}

	return rc;
}

static inline bool comp_eq(u32 val, u32 expected_val)
{
	return val == expected_val;
}

static inline bool comp_and(u32 val, u32 expected_val)
{
	return (val & expected_val) == expected_val;
}

static inline bool comp_or(u32 val, u32 expected_val)
{
	return (val | expected_val) > 0;
}

/* init_ops read/poll commands */
static void qed_init_cmd_rd(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt, struct init_read_op *cmd)
{
	bool (*comp_check)(u32 val, u32 expected_val);
	u32 delay = QED_INIT_POLL_PERIOD_US, val;
	u32 data, addr, poll;
	int i;

	data = le32_to_cpu(cmd->op_data);
	addr = GET_FIELD(data, INIT_READ_OP_ADDRESS) << 2;
	poll = GET_FIELD(data, INIT_READ_OP_POLL_TYPE);


	val = qed_rd(p_hwfn, p_ptt, addr);

	if (poll == INIT_POLL_NONE)
		return;

	switch (poll) {
	case INIT_POLL_EQ:
		comp_check = comp_eq;
		break;
	case INIT_POLL_OR:
		comp_check = comp_or;
		break;
	case INIT_POLL_AND:
		comp_check = comp_and;
		break;
	default:
		DP_ERR(p_hwfn, "Invalid poll comparison type %08x\n",
		       cmd->op_data);
		return;
	}

	data = le32_to_cpu(cmd->expected_val);
	for (i = 0;
	     i < QED_INIT_MAX_POLL_COUNT && !comp_check(val, data);
	     i++) {
		udelay(delay);
		val = qed_rd(p_hwfn, p_ptt, addr);
	}

	if (i == QED_INIT_MAX_POLL_COUNT) {
		DP_ERR(p_hwfn,
		       "Timeout when polling reg: 0x%08x [ Waiting-for: %08x Got: %08x (comparison %08x)]\n",
		       addr, le32_to_cpu(cmd->expected_val),
		       val, le32_to_cpu(cmd->op_data));
	}
}

/* init_ops callbacks entry point */
static int qed_init_cmd_cb(struct qed_hwfn *p_hwfn,
			   struct qed_ptt *p_ptt,
			   struct init_callback_op *p_cmd)
{
	int rc;

	switch (p_cmd->callback_id) {
	case DMAE_READY_CB:
		rc = qed_dmae_sanity(p_hwfn, p_ptt, "engine_phase");
		break;
	default:
		DP_NOTICE(p_hwfn, "Unexpected init op callback ID %d\n",
			  p_cmd->callback_id);
		return -EINVAL;
	}

	return rc;
}

static u8 qed_init_cmd_mode_match(struct qed_hwfn *p_hwfn,
				  u16 *p_offset, int modes)
{
	struct qed_dev *cdev = p_hwfn->cdev;
	const u8 *modes_tree_buf;
	u8 arg1, arg2, tree_val;

	modes_tree_buf = cdev->fw_data->modes_tree_buf;
	tree_val = modes_tree_buf[(*p_offset)++];
	switch (tree_val) {
	case INIT_MODE_OP_NOT:
		return qed_init_cmd_mode_match(p_hwfn, p_offset, modes) ^ 1;
	case INIT_MODE_OP_OR:
		arg1 = qed_init_cmd_mode_match(p_hwfn, p_offset, modes);
		arg2 = qed_init_cmd_mode_match(p_hwfn, p_offset, modes);
		return arg1 | arg2;
	case INIT_MODE_OP_AND:
		arg1 = qed_init_cmd_mode_match(p_hwfn, p_offset, modes);
		arg2 = qed_init_cmd_mode_match(p_hwfn, p_offset, modes);
		return arg1 & arg2;
	default:
		tree_val -= MAX_INIT_MODE_OPS;
		return (modes & BIT(tree_val)) ? 1 : 0;
	}
}

static u32 qed_init_cmd_mode(struct qed_hwfn *p_hwfn,
			     struct init_if_mode_op *p_cmd, int modes)
{
	u16 offset = le16_to_cpu(p_cmd->modes_buf_offset);

	if (qed_init_cmd_mode_match(p_hwfn, &offset, modes))
		return 0;
	else
		return GET_FIELD(le32_to_cpu(p_cmd->op_data),
				 INIT_IF_MODE_OP_CMD_OFFSET);
}

static u32 qed_init_cmd_phase(struct qed_hwfn *p_hwfn,
			      struct init_if_phase_op *p_cmd,
			      u32 phase, u32 phase_id)
{
	u32 data = le32_to_cpu(p_cmd->phase_data);
	u32 op_data = le32_to_cpu(p_cmd->op_data);

	if (!(GET_FIELD(data, INIT_IF_PHASE_OP_PHASE) == phase &&
	      (GET_FIELD(data, INIT_IF_PHASE_OP_PHASE_ID) == ANY_PHASE_ID ||
	       GET_FIELD(data, INIT_IF_PHASE_OP_PHASE_ID) == phase_id)))
		return GET_FIELD(op_data, INIT_IF_PHASE_OP_CMD_OFFSET);
	else
		return 0;
}

int qed_init_run(struct qed_hwfn *p_hwfn,
		 struct qed_ptt *p_ptt, int phase, int phase_id, int modes)
{
	bool b_dmae = (phase != PHASE_ENGINE);
	struct qed_dev *cdev = p_hwfn->cdev;
	u32 cmd_num, num_init_ops;
	union init_op *init_ops;
	int rc = 0;

	num_init_ops = cdev->fw_data->init_ops_size;
	init_ops = cdev->fw_data->init_ops;

	p_hwfn->unzip_buf = kzalloc(MAX_ZIPPED_SIZE * 4, GFP_ATOMIC);
	if (!p_hwfn->unzip_buf)
		return -ENOMEM;

	for (cmd_num = 0; cmd_num < num_init_ops; cmd_num++) {
		union init_op *cmd = &init_ops[cmd_num];
		u32 data = le32_to_cpu(cmd->raw.op_data);

		switch (GET_FIELD(data, INIT_CALLBACK_OP_OP)) {
		case INIT_OP_WRITE:
			rc = qed_init_cmd_wr(p_hwfn, p_ptt, &cmd->write,
					     b_dmae);
			break;
		case INIT_OP_READ:
			qed_init_cmd_rd(p_hwfn, p_ptt, &cmd->read);
			break;
		case INIT_OP_IF_MODE:
			cmd_num += qed_init_cmd_mode(p_hwfn, &cmd->if_mode,
						     modes);
			break;
		case INIT_OP_IF_PHASE:
			cmd_num += qed_init_cmd_phase(p_hwfn, &cmd->if_phase,
						      phase, phase_id);
			break;
		case INIT_OP_DELAY:
			/* qed_init_run is always invoked from
			 * sleep-able context
			 */
			udelay(le32_to_cpu(cmd->delay.delay));
			break;

		case INIT_OP_CALLBACK:
			rc = qed_init_cmd_cb(p_hwfn, p_ptt, &cmd->callback);
			if (phase == PHASE_ENGINE &&
			    cmd->callback.callback_id == DMAE_READY_CB)
				b_dmae = true;
			break;
		}

		if (rc)
			break;
	}

	kfree(p_hwfn->unzip_buf);
	p_hwfn->unzip_buf = NULL;
	return rc;
}

void qed_gtt_init(struct qed_hwfn *p_hwfn)
{
	u32 gtt_base;
	u32 i;

	/* Set the global windows */
	gtt_base = PXP_PF_WINDOW_ADMIN_START + PXP_PF_WINDOW_ADMIN_GLOBAL_START;

	for (i = 0; i < ARRAY_SIZE(pxp_global_win); i++)
		if (pxp_global_win[i])
			REG_WR(p_hwfn, gtt_base + i * PXP_GLOBAL_ENTRY_SIZE,
			       pxp_global_win[i]);
}

int qed_init_fw_data(struct qed_dev *cdev, const u8 *data)
{
	struct qed_fw_data *fw = cdev->fw_data;
	struct bin_buffer_hdr *buf_hdr;
	u32 offset, len;

	if (!data) {
		DP_NOTICE(cdev, "Invalid fw data\n");
		return -EINVAL;
	}

	/* First Dword contains metadata and should be skipped */
	buf_hdr = (struct bin_buffer_hdr *)data;

	offset = buf_hdr[BIN_BUF_INIT_FW_VER_INFO].offset;
	fw->fw_ver_info = (struct fw_ver_info *)(data + offset);

	offset = buf_hdr[BIN_BUF_INIT_CMD].offset;
	fw->init_ops = (union init_op *)(data + offset);

	offset = buf_hdr[BIN_BUF_INIT_VAL].offset;
	fw->arr_data = (u32 *)(data + offset);

	offset = buf_hdr[BIN_BUF_INIT_MODE_TREE].offset;
	fw->modes_tree_buf = (u8 *)(data + offset);
	len = buf_hdr[BIN_BUF_INIT_CMD].length;
	fw->init_ops_size = len / sizeof(struct init_raw_op);

	offset = buf_hdr[BIN_BUF_INIT_OVERLAYS].offset;
	fw->fw_overlays = (u32 *)(data + offset);
	len = buf_hdr[BIN_BUF_INIT_OVERLAYS].length;
	fw->fw_overlays_len = len;

	return 0;
}
