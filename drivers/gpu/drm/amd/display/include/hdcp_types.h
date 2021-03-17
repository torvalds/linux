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

#ifndef __DC_HDCP_TYPES_H__
#define __DC_HDCP_TYPES_H__

enum hdcp_message_id {
	HDCP_MESSAGE_ID_INVALID = -1,

	/* HDCP 1.4 */

	HDCP_MESSAGE_ID_READ_BKSV = 0,
	/* HDMI is called Ri', DP is called R0' */
	HDCP_MESSAGE_ID_READ_RI_R0,
	HDCP_MESSAGE_ID_READ_PJ,
	HDCP_MESSAGE_ID_WRITE_AKSV,
	HDCP_MESSAGE_ID_WRITE_AINFO,
	HDCP_MESSAGE_ID_WRITE_AN,
	HDCP_MESSAGE_ID_READ_VH_X,
	HDCP_MESSAGE_ID_READ_VH_0,
	HDCP_MESSAGE_ID_READ_VH_1,
	HDCP_MESSAGE_ID_READ_VH_2,
	HDCP_MESSAGE_ID_READ_VH_3,
	HDCP_MESSAGE_ID_READ_VH_4,
	HDCP_MESSAGE_ID_READ_BCAPS,
	HDCP_MESSAGE_ID_READ_BSTATUS,
	HDCP_MESSAGE_ID_READ_KSV_FIFO,
	HDCP_MESSAGE_ID_READ_BINFO,

	/* HDCP 2.2 */

	HDCP_MESSAGE_ID_HDCP2VERSION,
	HDCP_MESSAGE_ID_RX_CAPS,
	HDCP_MESSAGE_ID_WRITE_AKE_INIT,
	HDCP_MESSAGE_ID_READ_AKE_SEND_CERT,
	HDCP_MESSAGE_ID_WRITE_AKE_NO_STORED_KM,
	HDCP_MESSAGE_ID_WRITE_AKE_STORED_KM,
	HDCP_MESSAGE_ID_READ_AKE_SEND_H_PRIME,
	HDCP_MESSAGE_ID_READ_AKE_SEND_PAIRING_INFO,
	HDCP_MESSAGE_ID_WRITE_LC_INIT,
	HDCP_MESSAGE_ID_READ_LC_SEND_L_PRIME,
	HDCP_MESSAGE_ID_WRITE_SKE_SEND_EKS,
	HDCP_MESSAGE_ID_READ_REPEATER_AUTH_SEND_RECEIVERID_LIST,
	HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_SEND_ACK,
	HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_STREAM_MANAGE,
	HDCP_MESSAGE_ID_READ_REPEATER_AUTH_STREAM_READY,
	HDCP_MESSAGE_ID_READ_RXSTATUS,
	HDCP_MESSAGE_ID_WRITE_CONTENT_STREAM_TYPE,

	HDCP_MESSAGE_ID_MAX
};

enum hdcp_version {
	HDCP_Unknown = 0,
	HDCP_VERSION_14,
	HDCP_VERSION_22,
};

enum hdcp_link {
	HDCP_LINK_PRIMARY,
	HDCP_LINK_SECONDARY
};

enum hdcp_message_status {
	HDCP_MESSAGE_SUCCESS,
	HDCP_MESSAGE_FAILURE,
	HDCP_MESSAGE_UNSUPPORTED
};

struct hdcp_protection_message {
	enum hdcp_version version;
	/* relevant only for DVI */
	enum hdcp_link link;
	enum hdcp_message_id msg_id;
	uint32_t length;
	uint8_t max_retries;
	uint8_t *data;
	enum hdcp_message_status status;
};

#endif
