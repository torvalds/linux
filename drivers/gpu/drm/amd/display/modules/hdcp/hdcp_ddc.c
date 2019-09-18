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

	/* HDCP 2.2 */

	MOD_HDCP_MESSAGE_ID_HDCP2VERSION,
	MOD_HDCP_MESSAGE_ID_RX_CAPS,
	MOD_HDCP_MESSAGE_ID_WRITE_AKE_INIT,
	MOD_HDCP_MESSAGE_ID_READ_AKE_SEND_CERT,
	MOD_HDCP_MESSAGE_ID_WRITE_AKE_NO_STORED_KM,
	MOD_HDCP_MESSAGE_ID_WRITE_AKE_STORED_KM,
	MOD_HDCP_MESSAGE_ID_READ_AKE_SEND_H_PRIME,
	MOD_HDCP_MESSAGE_ID_READ_AKE_SEND_PAIRING_INFO,
	MOD_HDCP_MESSAGE_ID_WRITE_LC_INIT,
	MOD_HDCP_MESSAGE_ID_READ_LC_SEND_L_PRIME,
	MOD_HDCP_MESSAGE_ID_WRITE_SKE_SEND_EKS,
	MOD_HDCP_MESSAGE_ID_READ_REPEATER_AUTH_SEND_RECEIVERID_LIST,
	MOD_HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_SEND_ACK,
	MOD_HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_STREAM_MANAGE,
	MOD_HDCP_MESSAGE_ID_READ_REPEATER_AUTH_STREAM_READY,
	MOD_HDCP_MESSAGE_ID_READ_RXSTATUS,
	MOD_HDCP_MESSAGE_ID_WRITE_CONTENT_STREAM_TYPE,

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
	[MOD_HDCP_MESSAGE_ID_HDCP2VERSION] = 0x50,
	[MOD_HDCP_MESSAGE_ID_WRITE_AKE_INIT] = 0x60,
	[MOD_HDCP_MESSAGE_ID_READ_AKE_SEND_CERT] = 0x80,
	[MOD_HDCP_MESSAGE_ID_WRITE_AKE_NO_STORED_KM] = 0x60,
	[MOD_HDCP_MESSAGE_ID_WRITE_AKE_STORED_KM] = 0x60,
	[MOD_HDCP_MESSAGE_ID_READ_AKE_SEND_H_PRIME] = 0x80,
	[MOD_HDCP_MESSAGE_ID_READ_AKE_SEND_PAIRING_INFO] = 0x80,
	[MOD_HDCP_MESSAGE_ID_WRITE_LC_INIT] = 0x60,
	[MOD_HDCP_MESSAGE_ID_READ_LC_SEND_L_PRIME] = 0x80,
	[MOD_HDCP_MESSAGE_ID_WRITE_SKE_SEND_EKS] = 0x60,
	[MOD_HDCP_MESSAGE_ID_READ_REPEATER_AUTH_SEND_RECEIVERID_LIST] = 0x80,
	[MOD_HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_SEND_ACK] = 0x60,
	[MOD_HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_STREAM_MANAGE] = 0x60,
	[MOD_HDCP_MESSAGE_ID_READ_REPEATER_AUTH_STREAM_READY] = 0x80,
	[MOD_HDCP_MESSAGE_ID_READ_RXSTATUS] = 0x70,
	[MOD_HDCP_MESSAGE_ID_WRITE_CONTENT_STREAM_TYPE] = 0x0
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
	[MOD_HDCP_MESSAGE_ID_RX_CAPS] = 0x6921d,
	[MOD_HDCP_MESSAGE_ID_WRITE_AKE_INIT] = 0x69000,
	[MOD_HDCP_MESSAGE_ID_READ_AKE_SEND_CERT] = 0x6900b,
	[MOD_HDCP_MESSAGE_ID_WRITE_AKE_NO_STORED_KM] = 0x69220,
	[MOD_HDCP_MESSAGE_ID_WRITE_AKE_STORED_KM] = 0x692a0,
	[MOD_HDCP_MESSAGE_ID_READ_AKE_SEND_H_PRIME] = 0x692c0,
	[MOD_HDCP_MESSAGE_ID_READ_AKE_SEND_PAIRING_INFO] = 0x692e0,
	[MOD_HDCP_MESSAGE_ID_WRITE_LC_INIT] = 0x692f0,
	[MOD_HDCP_MESSAGE_ID_READ_LC_SEND_L_PRIME] = 0x692f8,
	[MOD_HDCP_MESSAGE_ID_WRITE_SKE_SEND_EKS] = 0x69318,
	[MOD_HDCP_MESSAGE_ID_READ_REPEATER_AUTH_SEND_RECEIVERID_LIST] = 0x69330,
	[MOD_HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_SEND_ACK] = 0x693e0,
	[MOD_HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_STREAM_MANAGE] = 0x693f0,
	[MOD_HDCP_MESSAGE_ID_READ_REPEATER_AUTH_STREAM_READY] = 0x69473,
	[MOD_HDCP_MESSAGE_ID_READ_RXSTATUS] = 0x69493,
	[MOD_HDCP_MESSAGE_ID_WRITE_CONTENT_STREAM_TYPE] = 0x69494
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

enum mod_hdcp_status mod_hdcp_read_hdcp2version(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp))
		status = MOD_HDCP_STATUS_INVALID_OPERATION;
	else
		status = read(hdcp, MOD_HDCP_MESSAGE_ID_HDCP2VERSION,
				&hdcp->auth.msg.hdcp2.hdcp2version_hdmi,
				sizeof(hdcp->auth.msg.hdcp2.hdcp2version_hdmi));

	return status;
}

enum mod_hdcp_status mod_hdcp_read_rxcaps(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (!is_dp_hdcp(hdcp))
		status = MOD_HDCP_STATUS_INVALID_OPERATION;
	else
		status = read(hdcp, MOD_HDCP_MESSAGE_ID_RX_CAPS,
				hdcp->auth.msg.hdcp2.rxcaps_dp,
				sizeof(hdcp->auth.msg.hdcp2.rxcaps_dp));

	return status;
}

enum mod_hdcp_status mod_hdcp_read_rxstatus(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp)) {
		status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_RXSTATUS,
				(uint8_t *)&hdcp->auth.msg.hdcp2.rxstatus,
				1);
	} else {
		status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_RXSTATUS,
					(uint8_t *)&hdcp->auth.msg.hdcp2.rxstatus,
					sizeof(hdcp->auth.msg.hdcp2.rxstatus));
	}
	return status;
}

enum mod_hdcp_status mod_hdcp_read_ake_cert(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp)) {
		hdcp->auth.msg.hdcp2.ake_cert[0] = 3;
		status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_AKE_SEND_CERT,
				hdcp->auth.msg.hdcp2.ake_cert+1,
				sizeof(hdcp->auth.msg.hdcp2.ake_cert)-1);

	} else {
		status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_AKE_SEND_CERT,
					hdcp->auth.msg.hdcp2.ake_cert,
					sizeof(hdcp->auth.msg.hdcp2.ake_cert));
	}
	return status;
}

enum mod_hdcp_status mod_hdcp_read_h_prime(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp)) {
		hdcp->auth.msg.hdcp2.ake_h_prime[0] = 7;
		status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_AKE_SEND_H_PRIME,
				hdcp->auth.msg.hdcp2.ake_h_prime+1,
				sizeof(hdcp->auth.msg.hdcp2.ake_h_prime)-1);

	} else {
		status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_AKE_SEND_H_PRIME,
				hdcp->auth.msg.hdcp2.ake_h_prime,
				sizeof(hdcp->auth.msg.hdcp2.ake_h_prime));
	}
	return status;
}

enum mod_hdcp_status mod_hdcp_read_pairing_info(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp)) {
		hdcp->auth.msg.hdcp2.ake_pairing_info[0] = 8;
		status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_AKE_SEND_PAIRING_INFO,
				hdcp->auth.msg.hdcp2.ake_pairing_info+1,
				sizeof(hdcp->auth.msg.hdcp2.ake_pairing_info)-1);

	} else {
		status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_AKE_SEND_PAIRING_INFO,
				hdcp->auth.msg.hdcp2.ake_pairing_info,
				sizeof(hdcp->auth.msg.hdcp2.ake_pairing_info));
	}
	return status;
}

enum mod_hdcp_status mod_hdcp_read_l_prime(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp)) {
		hdcp->auth.msg.hdcp2.lc_l_prime[0] = 10;
		status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_LC_SEND_L_PRIME,
				hdcp->auth.msg.hdcp2.lc_l_prime+1,
				sizeof(hdcp->auth.msg.hdcp2.lc_l_prime)-1);

	} else {
		status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_LC_SEND_L_PRIME,
				hdcp->auth.msg.hdcp2.lc_l_prime,
				sizeof(hdcp->auth.msg.hdcp2.lc_l_prime));
	}
	return status;
}

enum mod_hdcp_status mod_hdcp_read_rx_id_list(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp)) {
		hdcp->auth.msg.hdcp2.rx_id_list[0] = 12;
		status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_REPEATER_AUTH_SEND_RECEIVERID_LIST,
				hdcp->auth.msg.hdcp2.rx_id_list+1,
				sizeof(hdcp->auth.msg.hdcp2.rx_id_list)-1);

	} else {
		status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_REPEATER_AUTH_SEND_RECEIVERID_LIST,
				hdcp->auth.msg.hdcp2.rx_id_list,
				hdcp->auth.msg.hdcp2.rx_id_list_size);
	}
	return status;
}

enum mod_hdcp_status mod_hdcp_read_stream_ready(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp)) {
		hdcp->auth.msg.hdcp2.repeater_auth_stream_ready[0] = 17;
		status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_REPEATER_AUTH_STREAM_READY,
				hdcp->auth.msg.hdcp2.repeater_auth_stream_ready+1,
				sizeof(hdcp->auth.msg.hdcp2.repeater_auth_stream_ready)-1);

	} else {
		status = read(hdcp, MOD_HDCP_MESSAGE_ID_READ_REPEATER_AUTH_STREAM_READY,
				hdcp->auth.msg.hdcp2.repeater_auth_stream_ready,
				sizeof(hdcp->auth.msg.hdcp2.repeater_auth_stream_ready));
	}
	return status;
}

enum mod_hdcp_status mod_hdcp_write_ake_init(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp))
		status = write(hdcp, MOD_HDCP_MESSAGE_ID_WRITE_AKE_INIT,
				hdcp->auth.msg.hdcp2.ake_init+1,
				sizeof(hdcp->auth.msg.hdcp2.ake_init)-1);
	else
		status = write(hdcp, MOD_HDCP_MESSAGE_ID_WRITE_AKE_INIT,
					hdcp->auth.msg.hdcp2.ake_init,
					sizeof(hdcp->auth.msg.hdcp2.ake_init));
	return status;
}

enum mod_hdcp_status mod_hdcp_write_no_stored_km(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp))
		status = write(hdcp, MOD_HDCP_MESSAGE_ID_WRITE_AKE_NO_STORED_KM,
				hdcp->auth.msg.hdcp2.ake_no_stored_km+1,
				sizeof(hdcp->auth.msg.hdcp2.ake_no_stored_km)-1);
	else
		status = write(hdcp, MOD_HDCP_MESSAGE_ID_WRITE_AKE_NO_STORED_KM,
			hdcp->auth.msg.hdcp2.ake_no_stored_km,
			sizeof(hdcp->auth.msg.hdcp2.ake_no_stored_km));
	return status;
}

enum mod_hdcp_status mod_hdcp_write_stored_km(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp))
		status = write(hdcp, MOD_HDCP_MESSAGE_ID_WRITE_AKE_STORED_KM,
				hdcp->auth.msg.hdcp2.ake_stored_km+1,
				sizeof(hdcp->auth.msg.hdcp2.ake_stored_km)-1);
	else
		status = write(hdcp, MOD_HDCP_MESSAGE_ID_WRITE_AKE_STORED_KM,
				hdcp->auth.msg.hdcp2.ake_stored_km,
				sizeof(hdcp->auth.msg.hdcp2.ake_stored_km));
	return status;
}

enum mod_hdcp_status mod_hdcp_write_lc_init(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp))
		status = write(hdcp, MOD_HDCP_MESSAGE_ID_WRITE_LC_INIT,
				hdcp->auth.msg.hdcp2.lc_init+1,
				sizeof(hdcp->auth.msg.hdcp2.lc_init)-1);
	else
		status = write(hdcp, MOD_HDCP_MESSAGE_ID_WRITE_LC_INIT,
				hdcp->auth.msg.hdcp2.lc_init,
				sizeof(hdcp->auth.msg.hdcp2.lc_init));
	return status;
}

enum mod_hdcp_status mod_hdcp_write_eks(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp))
		status = write(hdcp,
				MOD_HDCP_MESSAGE_ID_WRITE_SKE_SEND_EKS,
				hdcp->auth.msg.hdcp2.ske_eks+1,
				sizeof(hdcp->auth.msg.hdcp2.ske_eks)-1);
	else
		status = write(hdcp,
			MOD_HDCP_MESSAGE_ID_WRITE_SKE_SEND_EKS,
			hdcp->auth.msg.hdcp2.ske_eks,
			sizeof(hdcp->auth.msg.hdcp2.ske_eks));
	return status;
}

enum mod_hdcp_status mod_hdcp_write_repeater_auth_ack(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp))
		status = write(hdcp, MOD_HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_SEND_ACK,
				hdcp->auth.msg.hdcp2.repeater_auth_ack+1,
				sizeof(hdcp->auth.msg.hdcp2.repeater_auth_ack)-1);
	else
		status = write(hdcp, MOD_HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_SEND_ACK,
				hdcp->auth.msg.hdcp2.repeater_auth_ack,
				sizeof(hdcp->auth.msg.hdcp2.repeater_auth_ack));
	return status;
}

enum mod_hdcp_status mod_hdcp_write_stream_manage(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp))
		status = write(hdcp,
				MOD_HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_STREAM_MANAGE,
				hdcp->auth.msg.hdcp2.repeater_auth_stream_manage+1,
				hdcp->auth.msg.hdcp2.stream_manage_size-1);
	else
		status = write(hdcp,
				MOD_HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_STREAM_MANAGE,
				hdcp->auth.msg.hdcp2.repeater_auth_stream_manage,
				hdcp->auth.msg.hdcp2.stream_manage_size);
	return status;
}

enum mod_hdcp_status mod_hdcp_write_content_type(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp))
		status = write(hdcp, MOD_HDCP_MESSAGE_ID_WRITE_CONTENT_STREAM_TYPE,
				hdcp->auth.msg.hdcp2.content_stream_type_dp+1,
				sizeof(hdcp->auth.msg.hdcp2.content_stream_type_dp)-1);
	else
		status = MOD_HDCP_STATUS_INVALID_OPERATION;
	return status;
}
