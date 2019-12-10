/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#include "hdcp.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define HDCP_I2C_ADDR 0x3a	/* 0x74 >> 1*/
#define KSV_READ_SIZE 0xf	/* 0x6803b - 0x6802c */
#define HDCP_MAX_AUX_TRANSACTION_SIZE 16

enum mod_hdcp_ddc_message_id {
	MOD_HDCP_MESSAGE_ID_INVALID = -1,

	/* HDCP 1.4 */

	MOD_HDCP_MESSAGE_ID_READ_BKSV = 0,
	MOD_HDCP_MESSAGE_ID_READ_RI_R0,
	MOD_HDCP_MESSAGE_ID_WRITE_AKSV,
	MOD_HDCP_MESSAGE_ID_WRITE_AINFO,
	MOD_HDCP_MESSAGE_ID_WRITE_AN,
	MOD_HDCP_MESSAGE_ID_READ_VH_X,
	MOD_HDCP_MESSAGE_ID_READ_VH_0,
	MOD_HDCP_MESSAGE_ID_READ_VH_1,
	MOD_HDCP_MESSAGE_ID_READ_VH_2,
	MOD_HDCP_MESSAGE_ID_READ_VH_3,
	MOD_HDCP_MESSAGE_ID_READ_VH_4,
	MOD_HDCP_MESSAGE_ID_READ_BCAPS,
	MOD_HDCP_MESSAGE_ID_READ_BSTATUS,
	MOD_HDCP_MESSAGE_ID_READ_KSV_FIFO,
	MOD_HDCP_MESSAGE_ID_READ_BINFO,

	MOD_HDCP_MESSAGE_ID_MAX
};

static const uint8_t hdcp_i2c_offsets[] = {
	[MOD_HDCP_MESSAGE_ID_READ_BKSV] = 0x0,
	[MOD_HDCP_MESSAGE_ID_READ_RI_R0] = 0x8,
	[MOD_HDCP_MESSAGE_ID_WRITE_AKSV] = 0x10,
	[MOD_HDCP_MESSAGE_ID_WRITE_AINFO] = 0x15,
	[MOD_HDCP_MESSAGE_ID_WRITE_AN] = 0x18,
	[MOD_HDCP_MESSAGE_ID_READ_VH_X] = 0x20,
	[MOD_HDCP_MESSAGE_ID_READ_VH_0] = 0x20,
	[MOD_HDCP_MESSAGE_ID_READ_VH_1] = 0x24,
	[MOD_HDCP_MESSAGE_ID_READ_VH_2] = 0x28,
	[MOD_HDCP_MESSAGE_ID_READ_VH_3] = 0x2C,
	[MOD_HDCP_MESSAGE_ID_READ_VH_4] = 0x30,
	[MOD_HDCP_MESSAGE_ID_READ_BCAPS] = 0x40,
	[MOD_HDCP_MESSAGE_ID_READ_BSTATUS] = 0x41,
	[MOD_HDCP_MESSAGE_ID_READ_KSV_FIFO] = 0x43,
	[MOD_HDCP_MESSAGE_ID_READ_BINFO] = 0xFF,
};

static const uint32_t hdcp_dpcd_addrs[] = {
	[MOD_HDCP_MESSAGE_ID_READ_BKSV] = 0x68000,
	[MOD_HDCP_MESSAGE_ID_READ_RI_R0] = 0x68005,
	[MOD_HDCP_MESSAGE_ID_WRITE_AKSV] = 0x68007,
	[MOD_HDCP_MESSAGE_ID_WRITE_AINFO] = 0x6803B,
	[MOD_HDCP_MESSAGE_ID_WRITE_AN] = 0x6800c,
	[MOD_HDCP_MESSAGE_ID_READ_VH_X] = 0x68014,
	[MOD_HDCP_MESSAGE_ID_READ_VH_0] = 0x68014,
	[MOD_HDCP_MESSAGE_ID_READ_VH_1] = 0x68018,
	[MOD_HDCP_MESSAGE_ID_READ_VH_2] = 0x6801c,
	[MOD_HDCP_MESSAGE_ID_READ_VH_3] = 0x68020,
	[MOD_HDCP_MESSAGE_ID_READ_VH_4] = 0x68024,
	[MOD_HDCP_MESSAGE_ID_READ_BCAPS] = 0x68028,
	[MOD_HDCP_MESSAGE_ID_READ_BSTATUS] = 0x68029,
	[MOD_HDCP_MESSAGE_ID_READ_KSV_FIFO] = 0x6802c,
	[MOD_HDCP_MESSAGE_ID_READ_BINFO] = 0x6802a,
};

static enum mod_hdcp_status read(struct mod_hdcp *hdcp,
		enum mod_hdcp_ddc_message_id msg_id,
		uint8_t *buf,
		uint32_t buf_len)
{
	bool success = true;
	uint32_t cur_size = 0;
	uint32_t data_offset = 0;

	if (is_dp_hdcp(hdcp)) {
		while (buf_len > 0) {
			cur_size = MIN(buf_len, HDCP_MAX_AUX_TRANSACTION_SIZE);
			success = hdcp->config.ddc.funcs.read_dpcd(hdcp->config.ddc.handle,
					hdcp_dpcd_addrs[msg_id] + data_offset,
					buf + data_offset,
					cur_size);

			if (!success)
				break;

			buf_len -= cur_size;
			data_offset += cur_size;
		}
	} else {
		success = hdcp->config.ddc.funcs.read_i2c(
				hdcp->config.ddc.handle,
				HDCP_I2C_ADDR,
				hdcp_i2c_offsets[msg_id],
				buf,
				(uint32_t)buf_len);
	}

	return success ? MOD_HDCP_STATUS_SUCCESS : MOD_HDCP_STATUS_DDC_FAILURE;
}

static enum mod_hdcp_status read_repeatedly(struct mod_hdcp *hdcp,
		enum mod_hdcp_ddc_message_id msg_id,
		uint8_t *buf,
		uint32_t buf_len,
		uint8_t read_size)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_DDC_FAILURE;
	uint32_t cur_size = 0;
	uint32_t data_offset = 0;

	while (buf_len > 0) {
		cur_size = MIN(buf_len, read_size);
		status = read(hdcp, msg_id, buf + data_offset, cur_size);

		if (status != MOD_HDCP_STATUS_SUCCESS)
			break;

		buf_len -= cur_size;
		data_offset += cur_size;
	}

	return status;
}

static enum mod_hdcp_status write(struct mod_hdcp *hdcp,
		enum mod_hdcp_ddc_message_id msg_id,
		uint8_t *buf,
		uint32_t buf_len)
{
	bool success = true;
	uint32_t cur_size = 0;
	uint32_t data_offset = 0;

	if (is_dp_hdcp(hdcp)) {
		while (buf_len > 0) {
			cur_size = MIN(buf_len, HDCP_MAX_AUX_TRANSACTION_SIZE);
			success = hdcp->config.ddc.funcs.write_dpcd(
					hdcp->config.ddc.handle,
					hdcp_dpcd_addrs[msg_id] + data_offset,
					buf + data_offset,
					cur_size);

			if (!success)
				break;

			buf_len -= cur_size;
			data_offset += cur_size;
		}
	} else {
		hdcp->buf[0] = hdcp_i2c_offsets[msg_id];
		memmove(&hdcp->buf[1], buf, buf_len);
		success = hdcp->config.ddc.funcs.write_i2c(
				hdcp->config.ddc.handle,
				HDCP_I2C_ADDR,
				hdcp->buf,
				(uint32_t)(buf_len+1));
	}

	return success ? MOD_HDCP_STATUS_SUCCESS : MOD_HDCP_STATUS_DDC_FAILURE;
}

enum mod_hdcp_status mod_hdcp_read_bksv(struct mod_hdcp *hdcp)
{
	return read(hdcp, MOD_HDCP_MESSAGE_ID_READ_BKSV,
			hdcp->auth.msg.hdcp1.bksv,
			sizeof(hdcp->auth.msg.hdcp1.bksv));
}

enum mod_hdcp_status mod_hdcp_read_bcaps(struct mod_hdcp *hdcp)
{
	return read(hdcp, MOD_HDCP_MESSAGE_ID_READ_BCAPS,
			&hdcp->auth.msg.hdcp1.bcaps,
			sizeof(hdcp->auth.msg.hdcp1.bcaps));
}

enum mod_hdcp_status mod_hdcp_read_bstatus(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp))
		status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_BSTATUS,
					(uint8_t *)&hdcp->auth.msg.hdcp1.bstatus,
					1);
	else
		status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_BSTATUS,
				(uint8_t *)&hdcp->auth.msg.hdcp1.bstatus,
				sizeof(hdcp->auth.msg.hdcp1.bstatus));
	return status;
}

enum mod_hdcp_status mod_hdcp_read_r0p(struct mod_hdcp *hdcp)
{
	return read(hdcp, MOD_HDCP_MESSAGE_ID_READ_RI_R0,
			(uint8_t *)&hdcp->auth.msg.hdcp1.r0p,
			sizeof(hdcp->auth.msg.hdcp1.r0p));
}

/* special case, reading repeatedly at the same address, don't use read() */
enum mod_hdcp_status mod_hdcp_read_ksvlist(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp))
		status = read_repeatedly(hdcp, MOD_HDCP_MESSAGE_ID_READ_KSV_FIFO,
				hdcp->auth.msg.hdcp1.ksvlist,
				hdcp->auth.msg.hdcp1.ksvlist_size,
				KSV_READ_SIZE);
	else
		status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_KSV_FIFO,
				(uint8_t *)&hdcp->auth.msg.hdcp1.ksvlist,
				hdcp->auth.msg.hdcp1.ksvlist_size);
	return status;
}

enum mod_hdcp_status mod_hdcp_read_vp(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_VH_0,
			&hdcp->auth.msg.hdcp1.vp[0], 4);
	if (status != MOD_HDCP_STATUS_SUCCESS)
		goto out;

	status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_VH_1,
			&hdcp->auth.msg.hdcp1.vp[4], 4);
	if (status != MOD_HDCP_STATUS_SUCCESS)
		goto out;

	status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_VH_2,
			&hdcp->auth.msg.hdcp1.vp[8], 4);
	if (status != MOD_HDCP_STATUS_SUCCESS)
		goto out;

	status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_VH_3,
			&hdcp->auth.msg.hdcp1.vp[12], 4);
	if (status != MOD_HDCP_STATUS_SUCCESS)
		goto out;

	status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_VH_4,
			&hdcp->auth.msg.hdcp1.vp[16], 4);
out:
	return status;
}

enum mod_hdcp_status mod_hdcp_read_binfo(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp))
		status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_BINFO,
				(uint8_t *)&hdcp->auth.msg.hdcp1.binfo_dp,
				sizeof(hdcp->auth.msg.hdcp1.binfo_dp));
	else
		status = MOD_HDCP_STATUS_INVALID_OPERATION;

	return status;
}

enum mod_hdcp_status mod_hdcp_write_aksv(struct mod_hdcp *hdcp)
{
	return write(hdcp, MOD_HDCP_MESSAGE_ID_WRITE_AKSV,
			hdcp->auth.msg.hdcp1.aksv,
			sizeof(hdcp->auth.msg.hdcp1.aksv));
}

enum mod_hdcp_status mod_hdcp_write_ainfo(struct mod_hdcp *hdcp)
{
	return write(hdcp, MOD_HDCP_MESSAGE_ID_WRITE_AINFO,
			&hdcp->auth.msg.hdcp1.ainfo,
			sizeof(hdcp->auth.msg.hdcp1.ainfo));
}

enum mod_hdcp_status mod_hdcp_write_an(struct mod_hdcp *hdcp)
{
	return write(hdcp, MOD_HDCP_MESSAGE_ID_WRITE_AN,
			hdcp->auth.msg.hdcp1.an,
			sizeof(hdcp->auth.msg.hdcp1.an));
}
