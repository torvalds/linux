// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include "ras.h"
#include "ras_mp1.h"
#include "core_status.h"
#include "ras_mp1_v13_0.h"

#define regMP1_SMN_C2PMSG_40                              0x0068
#define regMP1_SMN_C2PMSG_40_BASE_IDX                     2
#define regMP1_SMN_C2PMSG_41                              0x0069
#define regMP1_SMN_C2PMSG_41_BASE_IDX                     2
#define regMP1_SMN_C2PMSG_42                              0x006a
#define regMP1_SMN_C2PMSG_42_BASE_IDX                     2
#define regMP1_SMN_C2PMSG_43                              0x006b
#define regMP1_SMN_C2PMSG_43_BASE_IDX                     2
#define regMP1_SMN_C2PMSG_44                              0x006c
#define regMP1_SMN_C2PMSG_44_BASE_IDX                     2
#define regMP1_SMN_C2PMSG_45                              0x006d
#define regMP1_SMN_C2PMSG_45_BASE_IDX                     2

#define MP1_RESP_OK  1

static u32 ras_mp1_msg_codes[RAS_MP1_MSG_MAX] = {
	[RAS_MP1_MSG_GetRasTableVersion] = 0x1F,
	[RAS_MP1_MSG_GetRmaStatus] =    0x20,
	[RAS_MP1_MSG_GetBadPageCount] = 0x21,
	[RAS_MP1_MSG_GetBadPageMcaAddr] = 0x22,
	[RAS_MP1_MSG_GetBadPagePaAddr] =  0x23,
	[RAS_MP1_MSG_SetTimestamp] = 0x24,
	[RAS_MP1_MSG_GetTimestamp] = 0x25,
	[RAS_MP1_MSG_GetRasPolicy] = 0x26,
	[RAS_MP1_MSG_GetBadPageIpId] = 0x27,
	[RAS_MP1_MSG_EraseRasTable] =  0x28,
};

static int __direct_send_mp1_msg(struct ras_core_context *ras_core,
		enum ras_mp1_msg_id msg_id, u32 *inputs, u32 num_inputs,
		u32 *outputs, u32 num_outputs)
{
	u32 msg_code = 0;
	int timeout = 100000;  //100 ms
	u32 reg = 0;

	if (num_inputs > 2 || num_outputs > 2)
		return -EINVAL;

	msg_code = ras_mp1_msg_codes[msg_id];
	if (!msg_code)
		return -EOPNOTSUPP;

	/* Send message and parameter to fw */
	RAS_DEV_WREG32_SOC15(ras_core->dev, MP1, 0, regMP1_SMN_C2PMSG_41, 0);
	if (num_inputs == 1) {
		/* Input u32 parameter */
		RAS_DEV_WREG32_SOC15(ras_core->dev,
			MP1, 0, regMP1_SMN_C2PMSG_42, inputs[0]);
	} else if (num_inputs == 2) {
		/* Input u64 parameter */
		RAS_DEV_WREG32_SOC15(ras_core->dev,
			MP1, 0, regMP1_SMN_C2PMSG_42, inputs[0]);
		RAS_DEV_WREG32_SOC15(ras_core->dev,
			MP1, 0, regMP1_SMN_C2PMSG_43, inputs[1]);
	}
	RAS_DEV_WREG32_SOC15(ras_core->dev, MP1, 0, regMP1_SMN_C2PMSG_40, msg_code);

	/* Poll MP1 response */
	while (timeout--) {
		reg = RAS_DEV_RREG32_SOC15(ras_core->dev, MP1, 0, regMP1_SMN_C2PMSG_41);
		if (reg)
			break;

		udelay(1);
	};

	if (reg != MP1_RESP_OK) {
		RAS_DEV_ERR(ras_core->dev, "MP1 fail to ack 0x%x for msg: 0x%x\n",
			reg, msg_code);
		return -EIO;
	}

	/* Read output data */
	if (outputs && num_outputs) {
		if (num_outputs == 1) {
			/* Output u32 parameter */
			outputs[0] = RAS_DEV_RREG32_SOC15(ras_core->dev,
						MP1, 0, regMP1_SMN_C2PMSG_42);
		} else if (num_outputs == 2) {
			/* Output u64 parameter */
			outputs[0] = RAS_DEV_RREG32_SOC15(ras_core->dev,
						MP1, 0, regMP1_SMN_C2PMSG_42);
			outputs[1] = RAS_DEV_RREG32_SOC15(ras_core->dev,
						MP1, 0, regMP1_SMN_C2PMSG_43);
		}
	}

	return 0;
}

static int __sys_send_mp1_msg(struct ras_core_context *ras_core,
		enum ras_mp1_msg_id msg_id, u32 *inputs, u32 num_inputs,
		u32 *outputs, u32 num_outputs)
{
	if (!ras_core->ras_mp1.sys_func ||
	    !ras_core->ras_mp1.sys_func->mp1_send_ras_msg)
		return -EOPNOTSUPP;

	return ras_core->ras_mp1.sys_func->mp1_send_ras_msg(ras_core,
				msg_id, inputs, num_inputs, outputs, num_outputs);
}

static int __send_mp1_msg(struct ras_core_context *ras_core,
		enum ras_mp1_msg_id msg_id, u32 *inputs, u32 num_inputs,
		u32 *outputs, u32 num_outputs)
{
	if (msg_id >= RAS_MP1_MSG_MAX)
		return -EINVAL;

	if (ras_core_in_early_init(ras_core))
		return __direct_send_mp1_msg(ras_core, msg_id,
				inputs, num_inputs, outputs, num_outputs);
	else
		return __sys_send_mp1_msg(ras_core, msg_id,
				inputs, num_inputs, outputs, num_outputs);
}

static int __send_mp1_msg32(struct ras_core_context *ras_core,
		enum ras_mp1_msg_id msg_id, u32 input, u32 *output)
{
	return __send_mp1_msg(ras_core, msg_id,
			&input, 1, output, output ? 1 : 0);
}

static int __send_mp1_msg64(struct ras_core_context *ras_core,
		enum ras_mp1_msg_id msg_id, u64 input, u64 *output)
{
	u32 in[2] = {lower_32_bits(input), upper_32_bits(input)};
	u32 out[2] = {0};
	int ret;

	ret = __send_mp1_msg(ras_core, msg_id,
			in, 2, output ? out : NULL, output ? 2 : 0);
	if (!ret && output)
		*output = ((u64)out[1] << 32) | out[0];

	return ret;
}

static int ras_mp1_v15_get_table_version(struct ras_core_context *ras_core,
				     u32 *table_ver)
{
	if (!table_ver)
		return -EINVAL;

	return __send_mp1_msg32(ras_core, RAS_MP1_MSG_GetRasTableVersion,
			0, table_ver);
}

static bool ras_mp1_v15_rma_detected(struct ras_core_context *ras_core)
{
	u32 rma = 0;

	if (__send_mp1_msg32(ras_core, RAS_MP1_MSG_GetRmaStatus, 0, &rma))
		return false;

	return rma;
}

static int ras_mp1_v15_set_timestamp(struct ras_core_context *ras_core,
			u64 timestamp)
{
	if (!timestamp)
		return -EINVAL;

	return __send_mp1_msg64(ras_core, RAS_MP1_MSG_SetTimestamp, timestamp, NULL);
}

static int ras_mp1_v15_reset_ras_table(struct ras_core_context *ras_core,
				   u32 *result)
{
	if (!result)
		return -EINVAL;

	return __send_mp1_msg32(ras_core, RAS_MP1_MSG_EraseRasTable, 0, result);
}

static int ras_mp1_v15_get_record_count(struct ras_core_context *ras_core,
				u32 *count)
{
	if (!count)
		return -EINVAL;

	*count = 0;

	return __send_mp1_msg32(ras_core, RAS_MP1_MSG_GetBadPageCount, 0, count);
}

static int ras_mp1_v15_get_record(struct ras_core_context *ras_core,
			u32 idx, struct eeprom_err_record *rec)
{
	int ret;

	if (!rec)
		return -EINVAL;

	ret = __send_mp1_msg64(ras_core, RAS_MP1_MSG_GetTimestamp,
			idx, &rec->timestamp);
	if (ret)
		return ret;

	ret = __send_mp1_msg64(ras_core, RAS_MP1_MSG_GetBadPageMcaAddr,
			idx, &rec->mca_addr);
	if (ret)
		return ret;

	ret = __send_mp1_msg64(ras_core, RAS_MP1_MSG_GetBadPageIpId,
			idx, &rec->ipid);

	return ret;
}

const struct ras_mp1_ip_func mp1_ras_func_v15_0 = {
	.get_table_version = ras_mp1_v15_get_table_version,
	.rma_detected = ras_mp1_v15_rma_detected,
	.set_timestamp = ras_mp1_v15_set_timestamp,
	.reset_ras_table = ras_mp1_v15_reset_ras_table,
	.get_record_count = ras_mp1_v15_get_record_count,
	.get_record = ras_mp1_v15_get_record,
};
