/*
 * Copyright © 2014 Red Hat
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/i2c.h>
#include <drm/drm_dp_mst_helper.h>
#include <drm/drmP.h>

#include <drm/drm_fixed.h>

/**
 * DOC: dp mst helper
 *
 * These functions contain parts of the DisplayPort 1.2a MultiStream Transport
 * protocol. The helpers contain a topology manager and bandwidth manager.
 * The helpers encapsulate the sending and received of sideband msgs.
 */
static bool dump_dp_payload_table(struct drm_dp_mst_topology_mgr *mgr,
				  char *buf);
static int test_calc_pbn_mode(void);

static void drm_dp_put_port(struct drm_dp_mst_port *port);

static int drm_dp_dpcd_write_payload(struct drm_dp_mst_topology_mgr *mgr,
				     int id,
				     struct drm_dp_payload *payload);

static int drm_dp_send_dpcd_write(struct drm_dp_mst_topology_mgr *mgr,
				  struct drm_dp_mst_port *port,
				  int offset, int size, u8 *bytes);

static int drm_dp_send_link_address(struct drm_dp_mst_topology_mgr *mgr,
				    struct drm_dp_mst_branch *mstb);
static int drm_dp_send_enum_path_resources(struct drm_dp_mst_topology_mgr *mgr,
					   struct drm_dp_mst_branch *mstb,
					   struct drm_dp_mst_port *port);
static bool drm_dp_validate_guid(struct drm_dp_mst_topology_mgr *mgr,
				 u8 *guid);

static int drm_dp_mst_register_i2c_bus(struct drm_dp_aux *aux);
static void drm_dp_mst_unregister_i2c_bus(struct drm_dp_aux *aux);
static void drm_dp_mst_kick_tx(struct drm_dp_mst_topology_mgr *mgr);
/* sideband msg handling */
static u8 drm_dp_msg_header_crc4(const uint8_t *data, size_t num_nibbles)
{
	u8 bitmask = 0x80;
	u8 bitshift = 7;
	u8 array_index = 0;
	int number_of_bits = num_nibbles * 4;
	u8 remainder = 0;

	while (number_of_bits != 0) {
		number_of_bits--;
		remainder <<= 1;
		remainder |= (data[array_index] & bitmask) >> bitshift;
		bitmask >>= 1;
		bitshift--;
		if (bitmask == 0) {
			bitmask = 0x80;
			bitshift = 7;
			array_index++;
		}
		if ((remainder & 0x10) == 0x10)
			remainder ^= 0x13;
	}

	number_of_bits = 4;
	while (number_of_bits != 0) {
		number_of_bits--;
		remainder <<= 1;
		if ((remainder & 0x10) != 0)
			remainder ^= 0x13;
	}

	return remainder;
}

static u8 drm_dp_msg_data_crc4(const uint8_t *data, u8 number_of_bytes)
{
	u8 bitmask = 0x80;
	u8 bitshift = 7;
	u8 array_index = 0;
	int number_of_bits = number_of_bytes * 8;
	u16 remainder = 0;

	while (number_of_bits != 0) {
		number_of_bits--;
		remainder <<= 1;
		remainder |= (data[array_index] & bitmask) >> bitshift;
		bitmask >>= 1;
		bitshift--;
		if (bitmask == 0) {
			bitmask = 0x80;
			bitshift = 7;
			array_index++;
		}
		if ((remainder & 0x100) == 0x100)
			remainder ^= 0xd5;
	}

	number_of_bits = 8;
	while (number_of_bits != 0) {
		number_of_bits--;
		remainder <<= 1;
		if ((remainder & 0x100) != 0)
			remainder ^= 0xd5;
	}

	return remainder & 0xff;
}
static inline u8 drm_dp_calc_sb_hdr_size(struct drm_dp_sideband_msg_hdr *hdr)
{
	u8 size = 3;
	size += (hdr->lct / 2);
	return size;
}

static void drm_dp_encode_sideband_msg_hdr(struct drm_dp_sideband_msg_hdr *hdr,
					   u8 *buf, int *len)
{
	int idx = 0;
	int i;
	u8 crc4;
	buf[idx++] = ((hdr->lct & 0xf) << 4) | (hdr->lcr & 0xf);
	for (i = 0; i < (hdr->lct / 2); i++)
		buf[idx++] = hdr->rad[i];
	buf[idx++] = (hdr->broadcast << 7) | (hdr->path_msg << 6) |
		(hdr->msg_len & 0x3f);
	buf[idx++] = (hdr->somt << 7) | (hdr->eomt << 6) | (hdr->seqno << 4);

	crc4 = drm_dp_msg_header_crc4(buf, (idx * 2) - 1);
	buf[idx - 1] |= (crc4 & 0xf);

	*len = idx;
}

static bool drm_dp_decode_sideband_msg_hdr(struct drm_dp_sideband_msg_hdr *hdr,
					   u8 *buf, int buflen, u8 *hdrlen)
{
	u8 crc4;
	u8 len;
	int i;
	u8 idx;
	if (buf[0] == 0)
		return false;
	len = 3;
	len += ((buf[0] & 0xf0) >> 4) / 2;
	if (len > buflen)
		return false;
	crc4 = drm_dp_msg_header_crc4(buf, (len * 2) - 1);

	if ((crc4 & 0xf) != (buf[len - 1] & 0xf)) {
		DRM_DEBUG_KMS("crc4 mismatch 0x%x 0x%x\n", crc4, buf[len - 1]);
		return false;
	}

	hdr->lct = (buf[0] & 0xf0) >> 4;
	hdr->lcr = (buf[0] & 0xf);
	idx = 1;
	for (i = 0; i < (hdr->lct / 2); i++)
		hdr->rad[i] = buf[idx++];
	hdr->broadcast = (buf[idx] >> 7) & 0x1;
	hdr->path_msg = (buf[idx] >> 6) & 0x1;
	hdr->msg_len = buf[idx] & 0x3f;
	idx++;
	hdr->somt = (buf[idx] >> 7) & 0x1;
	hdr->eomt = (buf[idx] >> 6) & 0x1;
	hdr->seqno = (buf[idx] >> 4) & 0x1;
	idx++;
	*hdrlen = idx;
	return true;
}

static void drm_dp_encode_sideband_req(struct drm_dp_sideband_msg_req_body *req,
				       struct drm_dp_sideband_msg_tx *raw)
{
	int idx = 0;
	int i;
	u8 *buf = raw->msg;
	buf[idx++] = req->req_type & 0x7f;

	switch (req->req_type) {
	case DP_ENUM_PATH_RESOURCES:
		buf[idx] = (req->u.port_num.port_number & 0xf) << 4;
		idx++;
		break;
	case DP_ALLOCATE_PAYLOAD:
		buf[idx] = (req->u.allocate_payload.port_number & 0xf) << 4 |
			(req->u.allocate_payload.number_sdp_streams & 0xf);
		idx++;
		buf[idx] = (req->u.allocate_payload.vcpi & 0x7f);
		idx++;
		buf[idx] = (req->u.allocate_payload.pbn >> 8);
		idx++;
		buf[idx] = (req->u.allocate_payload.pbn & 0xff);
		idx++;
		for (i = 0; i < req->u.allocate_payload.number_sdp_streams / 2; i++) {
			buf[idx] = ((req->u.allocate_payload.sdp_stream_sink[i * 2] & 0xf) << 4) |
				(req->u.allocate_payload.sdp_stream_sink[i * 2 + 1] & 0xf);
			idx++;
		}
		if (req->u.allocate_payload.number_sdp_streams & 1) {
			i = req->u.allocate_payload.number_sdp_streams - 1;
			buf[idx] = (req->u.allocate_payload.sdp_stream_sink[i] & 0xf) << 4;
			idx++;
		}
		break;
	case DP_QUERY_PAYLOAD:
		buf[idx] = (req->u.query_payload.port_number & 0xf) << 4;
		idx++;
		buf[idx] = (req->u.query_payload.vcpi & 0x7f);
		idx++;
		break;
	case DP_REMOTE_DPCD_READ:
		buf[idx] = (req->u.dpcd_read.port_number & 0xf) << 4;
		buf[idx] |= ((req->u.dpcd_read.dpcd_address & 0xf0000) >> 16) & 0xf;
		idx++;
		buf[idx] = (req->u.dpcd_read.dpcd_address & 0xff00) >> 8;
		idx++;
		buf[idx] = (req->u.dpcd_read.dpcd_address & 0xff);
		idx++;
		buf[idx] = (req->u.dpcd_read.num_bytes);
		idx++;
		break;

	case DP_REMOTE_DPCD_WRITE:
		buf[idx] = (req->u.dpcd_write.port_number & 0xf) << 4;
		buf[idx] |= ((req->u.dpcd_write.dpcd_address & 0xf0000) >> 16) & 0xf;
		idx++;
		buf[idx] = (req->u.dpcd_write.dpcd_address & 0xff00) >> 8;
		idx++;
		buf[idx] = (req->u.dpcd_write.dpcd_address & 0xff);
		idx++;
		buf[idx] = (req->u.dpcd_write.num_bytes);
		idx++;
		memcpy(&buf[idx], req->u.dpcd_write.bytes, req->u.dpcd_write.num_bytes);
		idx += req->u.dpcd_write.num_bytes;
		break;
	case DP_REMOTE_I2C_READ:
		buf[idx] = (req->u.i2c_read.port_number & 0xf) << 4;
		buf[idx] |= (req->u.i2c_read.num_transactions & 0x3);
		idx++;
		for (i = 0; i < (req->u.i2c_read.num_transactions & 0x3); i++) {
			buf[idx] = req->u.i2c_read.transactions[i].i2c_dev_id & 0x7f;
			idx++;
			buf[idx] = req->u.i2c_read.transactions[i].num_bytes;
			idx++;
			memcpy(&buf[idx], req->u.i2c_read.transactions[i].bytes, req->u.i2c_read.transactions[i].num_bytes);
			idx += req->u.i2c_read.transactions[i].num_bytes;

			buf[idx] = (req->u.i2c_read.transactions[i].no_stop_bit & 0x1) << 5;
			buf[idx] |= (req->u.i2c_read.transactions[i].i2c_transaction_delay & 0xf);
			idx++;
		}
		buf[idx] = (req->u.i2c_read.read_i2c_device_id) & 0x7f;
		idx++;
		buf[idx] = (req->u.i2c_read.num_bytes_read);
		idx++;
		break;

	case DP_REMOTE_I2C_WRITE:
		buf[idx] = (req->u.i2c_write.port_number & 0xf) << 4;
		idx++;
		buf[idx] = (req->u.i2c_write.write_i2c_device_id) & 0x7f;
		idx++;
		buf[idx] = (req->u.i2c_write.num_bytes);
		idx++;
		memcpy(&buf[idx], req->u.i2c_write.bytes, req->u.i2c_write.num_bytes);
		idx += req->u.i2c_write.num_bytes;
		break;
	}
	raw->cur_len = idx;
}

static void drm_dp_crc_sideband_chunk_req(u8 *msg, u8 len)
{
	u8 crc4;
	crc4 = drm_dp_msg_data_crc4(msg, len);
	msg[len] = crc4;
}

static void drm_dp_encode_sideband_reply(struct drm_dp_sideband_msg_reply_body *rep,
					 struct drm_dp_sideband_msg_tx *raw)
{
	int idx = 0;
	u8 *buf = raw->msg;

	buf[idx++] = (rep->reply_type & 0x1) << 7 | (rep->req_type & 0x7f);

	raw->cur_len = idx;
}

/* this adds a chunk of msg to the builder to get the final msg */
static bool drm_dp_sideband_msg_build(struct drm_dp_sideband_msg_rx *msg,
				      u8 *replybuf, u8 replybuflen, bool hdr)
{
	int ret;
	u8 crc4;

	if (hdr) {
		u8 hdrlen;
		struct drm_dp_sideband_msg_hdr recv_hdr;
		ret = drm_dp_decode_sideband_msg_hdr(&recv_hdr, replybuf, replybuflen, &hdrlen);
		if (ret == false) {
			print_hex_dump(KERN_DEBUG, "failed hdr", DUMP_PREFIX_NONE, 16, 1, replybuf, replybuflen, false);
			return false;
		}

		/* get length contained in this portion */
		msg->curchunk_len = recv_hdr.msg_len;
		msg->curchunk_hdrlen = hdrlen;

		/* we have already gotten an somt - don't bother parsing */
		if (recv_hdr.somt && msg->have_somt)
			return false;

		if (recv_hdr.somt) {
			memcpy(&msg->initial_hdr, &recv_hdr, sizeof(struct drm_dp_sideband_msg_hdr));
			msg->have_somt = true;
		}
		if (recv_hdr.eomt)
			msg->have_eomt = true;

		/* copy the bytes for the remainder of this header chunk */
		msg->curchunk_idx = min(msg->curchunk_len, (u8)(replybuflen - hdrlen));
		memcpy(&msg->chunk[0], replybuf + hdrlen, msg->curchunk_idx);
	} else {
		memcpy(&msg->chunk[msg->curchunk_idx], replybuf, replybuflen);
		msg->curchunk_idx += replybuflen;
	}

	if (msg->curchunk_idx >= msg->curchunk_len) {
		/* do CRC */
		crc4 = drm_dp_msg_data_crc4(msg->chunk, msg->curchunk_len - 1);
		/* copy chunk into bigger msg */
		memcpy(&msg->msg[msg->curlen], msg->chunk, msg->curchunk_len - 1);
		msg->curlen += msg->curchunk_len - 1;
	}
	return true;
}

static bool drm_dp_sideband_parse_link_address(struct drm_dp_sideband_msg_rx *raw,
					       struct drm_dp_sideband_msg_reply_body *repmsg)
{
	int idx = 1;
	int i;
	memcpy(repmsg->u.link_addr.guid, &raw->msg[idx], 16);
	idx += 16;
	repmsg->u.link_addr.nports = raw->msg[idx] & 0xf;
	idx++;
	if (idx > raw->curlen)
		goto fail_len;
	for (i = 0; i < repmsg->u.link_addr.nports; i++) {
		if (raw->msg[idx] & 0x80)
			repmsg->u.link_addr.ports[i].input_port = 1;

		repmsg->u.link_addr.ports[i].peer_device_type = (raw->msg[idx] >> 4) & 0x7;
		repmsg->u.link_addr.ports[i].port_number = (raw->msg[idx] & 0xf);

		idx++;
		if (idx > raw->curlen)
			goto fail_len;
		repmsg->u.link_addr.ports[i].mcs = (raw->msg[idx] >> 7) & 0x1;
		repmsg->u.link_addr.ports[i].ddps = (raw->msg[idx] >> 6) & 0x1;
		if (repmsg->u.link_addr.ports[i].input_port == 0)
			repmsg->u.link_addr.ports[i].legacy_device_plug_status = (raw->msg[idx] >> 5) & 0x1;
		idx++;
		if (idx > raw->curlen)
			goto fail_len;
		if (repmsg->u.link_addr.ports[i].input_port == 0) {
			repmsg->u.link_addr.ports[i].dpcd_revision = (raw->msg[idx]);
			idx++;
			if (idx > raw->curlen)
				goto fail_len;
			memcpy(repmsg->u.link_addr.ports[i].peer_guid, &raw->msg[idx], 16);
			idx += 16;
			if (idx > raw->curlen)
				goto fail_len;
			repmsg->u.link_addr.ports[i].num_sdp_streams = (raw->msg[idx] >> 4) & 0xf;
			repmsg->u.link_addr.ports[i].num_sdp_stream_sinks = (raw->msg[idx] & 0xf);
			idx++;

		}
		if (idx > raw->curlen)
			goto fail_len;
	}

	return true;
fail_len:
	DRM_DEBUG_KMS("link address reply parse length fail %d %d\n", idx, raw->curlen);
	return false;
}

static bool drm_dp_sideband_parse_remote_dpcd_read(struct drm_dp_sideband_msg_rx *raw,
						   struct drm_dp_sideband_msg_reply_body *repmsg)
{
	int idx = 1;
	repmsg->u.remote_dpcd_read_ack.port_number = raw->msg[idx] & 0xf;
	idx++;
	if (idx > raw->curlen)
		goto fail_len;
	repmsg->u.remote_dpcd_read_ack.num_bytes = raw->msg[idx];
	if (idx > raw->curlen)
		goto fail_len;

	memcpy(repmsg->u.remote_dpcd_read_ack.bytes, &raw->msg[idx], repmsg->u.remote_dpcd_read_ack.num_bytes);
	return true;
fail_len:
	DRM_DEBUG_KMS("link address reply parse length fail %d %d\n", idx, raw->curlen);
	return false;
}

static bool drm_dp_sideband_parse_remote_dpcd_write(struct drm_dp_sideband_msg_rx *raw,
						      struct drm_dp_sideband_msg_reply_body *repmsg)
{
	int idx = 1;
	repmsg->u.remote_dpcd_write_ack.port_number = raw->msg[idx] & 0xf;
	idx++;
	if (idx > raw->curlen)
		goto fail_len;
	return true;
fail_len:
	DRM_DEBUG_KMS("parse length fail %d %d\n", idx, raw->curlen);
	return false;
}

static bool drm_dp_sideband_parse_remote_i2c_read_ack(struct drm_dp_sideband_msg_rx *raw,
						      struct drm_dp_sideband_msg_reply_body *repmsg)
{
	int idx = 1;

	repmsg->u.remote_i2c_read_ack.port_number = (raw->msg[idx] & 0xf);
	idx++;
	if (idx > raw->curlen)
		goto fail_len;
	repmsg->u.remote_i2c_read_ack.num_bytes = raw->msg[idx];
	idx++;
	/* TODO check */
	memcpy(repmsg->u.remote_i2c_read_ack.bytes, &raw->msg[idx], repmsg->u.remote_i2c_read_ack.num_bytes);
	return true;
fail_len:
	DRM_DEBUG_KMS("remote i2c reply parse length fail %d %d\n", idx, raw->curlen);
	return false;
}

static bool drm_dp_sideband_parse_enum_path_resources_ack(struct drm_dp_sideband_msg_rx *raw,
							  struct drm_dp_sideband_msg_reply_body *repmsg)
{
	int idx = 1;
	repmsg->u.path_resources.port_number = (raw->msg[idx] >> 4) & 0xf;
	idx++;
	if (idx > raw->curlen)
		goto fail_len;
	repmsg->u.path_resources.full_payload_bw_number = (raw->msg[idx] << 8) | (raw->msg[idx+1]);
	idx += 2;
	if (idx > raw->curlen)
		goto fail_len;
	repmsg->u.path_resources.avail_payload_bw_number = (raw->msg[idx] << 8) | (raw->msg[idx+1]);
	idx += 2;
	if (idx > raw->curlen)
		goto fail_len;
	return true;
fail_len:
	DRM_DEBUG_KMS("enum resource parse length fail %d %d\n", idx, raw->curlen);
	return false;
}

static bool drm_dp_sideband_parse_allocate_payload_ack(struct drm_dp_sideband_msg_rx *raw,
							  struct drm_dp_sideband_msg_reply_body *repmsg)
{
	int idx = 1;
	repmsg->u.allocate_payload.port_number = (raw->msg[idx] >> 4) & 0xf;
	idx++;
	if (idx > raw->curlen)
		goto fail_len;
	repmsg->u.allocate_payload.vcpi = raw->msg[idx];
	idx++;
	if (idx > raw->curlen)
		goto fail_len;
	repmsg->u.allocate_payload.allocated_pbn = (raw->msg[idx] << 8) | (raw->msg[idx+1]);
	idx += 2;
	if (idx > raw->curlen)
		goto fail_len;
	return true;
fail_len:
	DRM_DEBUG_KMS("allocate payload parse length fail %d %d\n", idx, raw->curlen);
	return false;
}

static bool drm_dp_sideband_parse_query_payload_ack(struct drm_dp_sideband_msg_rx *raw,
						    struct drm_dp_sideband_msg_reply_body *repmsg)
{
	int idx = 1;
	repmsg->u.query_payload.port_number = (raw->msg[idx] >> 4) & 0xf;
	idx++;
	if (idx > raw->curlen)
		goto fail_len;
	repmsg->u.query_payload.allocated_pbn = (raw->msg[idx] << 8) | (raw->msg[idx + 1]);
	idx += 2;
	if (idx > raw->curlen)
		goto fail_len;
	return true;
fail_len:
	DRM_DEBUG_KMS("query payload parse length fail %d %d\n", idx, raw->curlen);
	return false;
}

static bool drm_dp_sideband_parse_reply(struct drm_dp_sideband_msg_rx *raw,
					struct drm_dp_sideband_msg_reply_body *msg)
{
	memset(msg, 0, sizeof(*msg));
	msg->reply_type = (raw->msg[0] & 0x80) >> 7;
	msg->req_type = (raw->msg[0] & 0x7f);

	if (msg->reply_type) {
		memcpy(msg->u.nak.guid, &raw->msg[1], 16);
		msg->u.nak.reason = raw->msg[17];
		msg->u.nak.nak_data = raw->msg[18];
		return false;
	}

	switch (msg->req_type) {
	case DP_LINK_ADDRESS:
		return drm_dp_sideband_parse_link_address(raw, msg);
	case DP_QUERY_PAYLOAD:
		return drm_dp_sideband_parse_query_payload_ack(raw, msg);
	case DP_REMOTE_DPCD_READ:
		return drm_dp_sideband_parse_remote_dpcd_read(raw, msg);
	case DP_REMOTE_DPCD_WRITE:
		return drm_dp_sideband_parse_remote_dpcd_write(raw, msg);
	case DP_REMOTE_I2C_READ:
		return drm_dp_sideband_parse_remote_i2c_read_ack(raw, msg);
	case DP_ENUM_PATH_RESOURCES:
		return drm_dp_sideband_parse_enum_path_resources_ack(raw, msg);
	case DP_ALLOCATE_PAYLOAD:
		return drm_dp_sideband_parse_allocate_payload_ack(raw, msg);
	default:
		DRM_ERROR("Got unknown reply 0x%02x\n", msg->req_type);
		return false;
	}
}

static bool drm_dp_sideband_parse_connection_status_notify(struct drm_dp_sideband_msg_rx *raw,
							   struct drm_dp_sideband_msg_req_body *msg)
{
	int idx = 1;

	msg->u.conn_stat.port_number = (raw->msg[idx] & 0xf0) >> 4;
	idx++;
	if (idx > raw->curlen)
		goto fail_len;

	memcpy(msg->u.conn_stat.guid, &raw->msg[idx], 16);
	idx += 16;
	if (idx > raw->curlen)
		goto fail_len;

	msg->u.conn_stat.legacy_device_plug_status = (raw->msg[idx] >> 6) & 0x1;
	msg->u.conn_stat.displayport_device_plug_status = (raw->msg[idx] >> 5) & 0x1;
	msg->u.conn_stat.message_capability_status = (raw->msg[idx] >> 4) & 0x1;
	msg->u.conn_stat.input_port = (raw->msg[idx] >> 3) & 0x1;
	msg->u.conn_stat.peer_device_type = (raw->msg[idx] & 0x7);
	idx++;
	return true;
fail_len:
	DRM_DEBUG_KMS("connection status reply parse length fail %d %d\n", idx, raw->curlen);
	return false;
}

static bool drm_dp_sideband_parse_resource_status_notify(struct drm_dp_sideband_msg_rx *raw,
							   struct drm_dp_sideband_msg_req_body *msg)
{
	int idx = 1;

	msg->u.resource_stat.port_number = (raw->msg[idx] & 0xf0) >> 4;
	idx++;
	if (idx > raw->curlen)
		goto fail_len;

	memcpy(msg->u.resource_stat.guid, &raw->msg[idx], 16);
	idx += 16;
	if (idx > raw->curlen)
		goto fail_len;

	msg->u.resource_stat.available_pbn = (raw->msg[idx] << 8) | (raw->msg[idx + 1]);
	idx++;
	return true;
fail_len:
	DRM_DEBUG_KMS("resource status reply parse length fail %d %d\n", idx, raw->curlen);
	return false;
}

static bool drm_dp_sideband_parse_req(struct drm_dp_sideband_msg_rx *raw,
				      struct drm_dp_sideband_msg_req_body *msg)
{
	memset(msg, 0, sizeof(*msg));
	msg->req_type = (raw->msg[0] & 0x7f);

	switch (msg->req_type) {
	case DP_CONNECTION_STATUS_NOTIFY:
		return drm_dp_sideband_parse_connection_status_notify(raw, msg);
	case DP_RESOURCE_STATUS_NOTIFY:
		return drm_dp_sideband_parse_resource_status_notify(raw, msg);
	default:
		DRM_ERROR("Got unknown request 0x%02x\n", msg->req_type);
		return false;
	}
}

static int build_dpcd_write(struct drm_dp_sideband_msg_tx *msg, u8 port_num, u32 offset, u8 num_bytes, u8 *bytes)
{
	struct drm_dp_sideband_msg_req_body req;

	req.req_type = DP_REMOTE_DPCD_WRITE;
	req.u.dpcd_write.port_number = port_num;
	req.u.dpcd_write.dpcd_address = offset;
	req.u.dpcd_write.num_bytes = num_bytes;
	req.u.dpcd_write.bytes = bytes;
	drm_dp_encode_sideband_req(&req, msg);

	return 0;
}

static int build_link_address(struct drm_dp_sideband_msg_tx *msg)
{
	struct drm_dp_sideband_msg_req_body req;

	req.req_type = DP_LINK_ADDRESS;
	drm_dp_encode_sideband_req(&req, msg);
	return 0;
}

static int build_enum_path_resources(struct drm_dp_sideband_msg_tx *msg, int port_num)
{
	struct drm_dp_sideband_msg_req_body req;

	req.req_type = DP_ENUM_PATH_RESOURCES;
	req.u.port_num.port_number = port_num;
	drm_dp_encode_sideband_req(&req, msg);
	msg->path_msg = true;
	return 0;
}

static int build_allocate_payload(struct drm_dp_sideband_msg_tx *msg, int port_num,
				  u8 vcpi, uint16_t pbn)
{
	struct drm_dp_sideband_msg_req_body req;
	memset(&req, 0, sizeof(req));
	req.req_type = DP_ALLOCATE_PAYLOAD;
	req.u.allocate_payload.port_number = port_num;
	req.u.allocate_payload.vcpi = vcpi;
	req.u.allocate_payload.pbn = pbn;
	drm_dp_encode_sideband_req(&req, msg);
	msg->path_msg = true;
	return 0;
}

static int drm_dp_mst_assign_payload_id(struct drm_dp_mst_topology_mgr *mgr,
					struct drm_dp_vcpi *vcpi)
{
	int ret;

	mutex_lock(&mgr->payload_lock);
	ret = find_first_zero_bit(&mgr->payload_mask, mgr->max_payloads + 1);
	if (ret > mgr->max_payloads) {
		ret = -EINVAL;
		DRM_DEBUG_KMS("out of payload ids %d\n", ret);
		goto out_unlock;
	}

	set_bit(ret, &mgr->payload_mask);
	vcpi->vcpi = ret;
	mgr->proposed_vcpis[ret - 1] = vcpi;
out_unlock:
	mutex_unlock(&mgr->payload_lock);
	return ret;
}

static void drm_dp_mst_put_payload_id(struct drm_dp_mst_topology_mgr *mgr,
				      int id)
{
	if (id == 0)
		return;

	mutex_lock(&mgr->payload_lock);
	DRM_DEBUG_KMS("putting payload %d\n", id);
	clear_bit(id, &mgr->payload_mask);
	mgr->proposed_vcpis[id - 1] = NULL;
	mutex_unlock(&mgr->payload_lock);
}

static bool check_txmsg_state(struct drm_dp_mst_topology_mgr *mgr,
			      struct drm_dp_sideband_msg_tx *txmsg)
{
	bool ret;
	mutex_lock(&mgr->qlock);
	ret = (txmsg->state == DRM_DP_SIDEBAND_TX_RX ||
	       txmsg->state == DRM_DP_SIDEBAND_TX_TIMEOUT);
	mutex_unlock(&mgr->qlock);
	return ret;
}

static int drm_dp_mst_wait_tx_reply(struct drm_dp_mst_branch *mstb,
				    struct drm_dp_sideband_msg_tx *txmsg)
{
	struct drm_dp_mst_topology_mgr *mgr = mstb->mgr;
	int ret;

	ret = wait_event_timeout(mgr->tx_waitq,
				 check_txmsg_state(mgr, txmsg),
				 (4 * HZ));
	mutex_lock(&mstb->mgr->qlock);
	if (ret > 0) {
		if (txmsg->state == DRM_DP_SIDEBAND_TX_TIMEOUT) {
			ret = -EIO;
			goto out;
		}
	} else {
		DRM_DEBUG_KMS("timedout msg send %p %d %d\n", txmsg, txmsg->state, txmsg->seqno);

		/* dump some state */
		ret = -EIO;

		/* remove from q */
		if (txmsg->state == DRM_DP_SIDEBAND_TX_QUEUED ||
		    txmsg->state == DRM_DP_SIDEBAND_TX_START_SEND) {
			list_del(&txmsg->next);
		}

		if (txmsg->state == DRM_DP_SIDEBAND_TX_START_SEND ||
		    txmsg->state == DRM_DP_SIDEBAND_TX_SENT) {
			mstb->tx_slots[txmsg->seqno] = NULL;
		}
	}
out:
	mutex_unlock(&mgr->qlock);

	return ret;
}

static struct drm_dp_mst_branch *drm_dp_add_mst_branch_device(u8 lct, u8 *rad)
{
	struct drm_dp_mst_branch *mstb;

	mstb = kzalloc(sizeof(*mstb), GFP_KERNEL);
	if (!mstb)
		return NULL;

	mstb->lct = lct;
	if (lct > 1)
		memcpy(mstb->rad, rad, lct / 2);
	INIT_LIST_HEAD(&mstb->ports);
	kref_init(&mstb->kref);
	return mstb;
}

static void drm_dp_destroy_mst_branch_device(struct kref *kref)
{
	struct drm_dp_mst_branch *mstb = container_of(kref, struct drm_dp_mst_branch, kref);
	struct drm_dp_mst_port *port, *tmp;
	bool wake_tx = false;

	cancel_work_sync(&mstb->mgr->work);

	/*
	 * destroy all ports - don't need lock
	 * as there are no more references to the mst branch
	 * device at this point.
	 */
	list_for_each_entry_safe(port, tmp, &mstb->ports, next) {
		list_del(&port->next);
		drm_dp_put_port(port);
	}

	/* drop any tx slots msg */
	mutex_lock(&mstb->mgr->qlock);
	if (mstb->tx_slots[0]) {
		mstb->tx_slots[0]->state = DRM_DP_SIDEBAND_TX_TIMEOUT;
		mstb->tx_slots[0] = NULL;
		wake_tx = true;
	}
	if (mstb->tx_slots[1]) {
		mstb->tx_slots[1]->state = DRM_DP_SIDEBAND_TX_TIMEOUT;
		mstb->tx_slots[1] = NULL;
		wake_tx = true;
	}
	mutex_unlock(&mstb->mgr->qlock);

	if (wake_tx)
		wake_up(&mstb->mgr->tx_waitq);
	kfree(mstb);
}

static void drm_dp_put_mst_branch_device(struct drm_dp_mst_branch *mstb)
{
	kref_put(&mstb->kref, drm_dp_destroy_mst_branch_device);
}


static void drm_dp_port_teardown_pdt(struct drm_dp_mst_port *port, int old_pdt)
{
	switch (old_pdt) {
	case DP_PEER_DEVICE_DP_LEGACY_CONV:
	case DP_PEER_DEVICE_SST_SINK:
		/* remove i2c over sideband */
		drm_dp_mst_unregister_i2c_bus(&port->aux);
		break;
	case DP_PEER_DEVICE_MST_BRANCHING:
		drm_dp_put_mst_branch_device(port->mstb);
		port->mstb = NULL;
		break;
	}
}

static void drm_dp_destroy_port(struct kref *kref)
{
	struct drm_dp_mst_port *port = container_of(kref, struct drm_dp_mst_port, kref);
	struct drm_dp_mst_topology_mgr *mgr = port->mgr;
	if (!port->input) {
		port->vcpi.num_slots = 0;
		if (port->connector)
			(*port->mgr->cbs->destroy_connector)(mgr, port->connector);
		drm_dp_port_teardown_pdt(port, port->pdt);

		if (!port->input && port->vcpi.vcpi > 0)
			drm_dp_mst_put_payload_id(mgr, port->vcpi.vcpi);
	}
	kfree(port);

	(*mgr->cbs->hotplug)(mgr);
}

static void drm_dp_put_port(struct drm_dp_mst_port *port)
{
	kref_put(&port->kref, drm_dp_destroy_port);
}

static struct drm_dp_mst_branch *drm_dp_mst_get_validated_mstb_ref_locked(struct drm_dp_mst_branch *mstb, struct drm_dp_mst_branch *to_find)
{
	struct drm_dp_mst_port *port;
	struct drm_dp_mst_branch *rmstb;
	if (to_find == mstb) {
		kref_get(&mstb->kref);
		return mstb;
	}
	list_for_each_entry(port, &mstb->ports, next) {
		if (port->mstb) {
			rmstb = drm_dp_mst_get_validated_mstb_ref_locked(port->mstb, to_find);
			if (rmstb)
				return rmstb;
		}
	}
	return NULL;
}

static struct drm_dp_mst_branch *drm_dp_get_validated_mstb_ref(struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_branch *mstb)
{
	struct drm_dp_mst_branch *rmstb = NULL;
	mutex_lock(&mgr->lock);
	if (mgr->mst_primary)
		rmstb = drm_dp_mst_get_validated_mstb_ref_locked(mgr->mst_primary, mstb);
	mutex_unlock(&mgr->lock);
	return rmstb;
}

static struct drm_dp_mst_port *drm_dp_mst_get_port_ref_locked(struct drm_dp_mst_branch *mstb, struct drm_dp_mst_port *to_find)
{
	struct drm_dp_mst_port *port, *mport;

	list_for_each_entry(port, &mstb->ports, next) {
		if (port == to_find) {
			kref_get(&port->kref);
			return port;
		}
		if (port->mstb) {
			mport = drm_dp_mst_get_port_ref_locked(port->mstb, to_find);
			if (mport)
				return mport;
		}
	}
	return NULL;
}

static struct drm_dp_mst_port *drm_dp_get_validated_port_ref(struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port)
{
	struct drm_dp_mst_port *rport = NULL;
	mutex_lock(&mgr->lock);
	if (mgr->mst_primary)
		rport = drm_dp_mst_get_port_ref_locked(mgr->mst_primary, port);
	mutex_unlock(&mgr->lock);
	return rport;
}

static struct drm_dp_mst_port *drm_dp_get_port(struct drm_dp_mst_branch *mstb, u8 port_num)
{
	struct drm_dp_mst_port *port;

	list_for_each_entry(port, &mstb->ports, next) {
		if (port->port_num == port_num) {
			kref_get(&port->kref);
			return port;
		}
	}

	return NULL;
}

/*
 * calculate a new RAD for this MST branch device
 * if parent has an LCT of 2 then it has 1 nibble of RAD,
 * if parent has an LCT of 3 then it has 2 nibbles of RAD,
 */
static u8 drm_dp_calculate_rad(struct drm_dp_mst_port *port,
				 u8 *rad)
{
	int lct = port->parent->lct;
	int shift = 4;
	int idx = lct / 2;
	if (lct > 1) {
		memcpy(rad, port->parent->rad, idx);
		shift = (lct % 2) ? 4 : 0;
	} else
		rad[0] = 0;

	rad[idx] |= port->port_num << shift;
	return lct + 1;
}

/*
 * return sends link address for new mstb
 */
static bool drm_dp_port_setup_pdt(struct drm_dp_mst_port *port)
{
	int ret;
	u8 rad[6], lct;
	bool send_link = false;
	switch (port->pdt) {
	case DP_PEER_DEVICE_DP_LEGACY_CONV:
	case DP_PEER_DEVICE_SST_SINK:
		/* add i2c over sideband */
		ret = drm_dp_mst_register_i2c_bus(&port->aux);
		break;
	case DP_PEER_DEVICE_MST_BRANCHING:
		lct = drm_dp_calculate_rad(port, rad);

		port->mstb = drm_dp_add_mst_branch_device(lct, rad);
		port->mstb->mgr = port->mgr;
		port->mstb->port_parent = port;

		send_link = true;
		break;
	}
	return send_link;
}

static void drm_dp_check_port_guid(struct drm_dp_mst_branch *mstb,
				   struct drm_dp_mst_port *port)
{
	int ret;
	if (port->dpcd_rev >= 0x12) {
		port->guid_valid = drm_dp_validate_guid(mstb->mgr, port->guid);
		if (!port->guid_valid) {
			ret = drm_dp_send_dpcd_write(mstb->mgr,
						     port,
						     DP_GUID,
						     16, port->guid);
			port->guid_valid = true;
		}
	}
}

static void build_mst_prop_path(struct drm_dp_mst_port *port,
				struct drm_dp_mst_branch *mstb,
				char *proppath)
{
	int i;
	char temp[8];
	snprintf(proppath, 255, "mst:%d", mstb->mgr->conn_base_id);
	for (i = 0; i < (mstb->lct - 1); i++) {
		int shift = (i % 2) ? 0 : 4;
		int port_num = mstb->rad[i / 2] >> shift;
		snprintf(temp, 8, "-%d", port_num);
		strncat(proppath, temp, 255);
	}
	snprintf(temp, 8, "-%d", port->port_num);
	strncat(proppath, temp, 255);
}

static void drm_dp_add_port(struct drm_dp_mst_branch *mstb,
			    struct device *dev,
			    struct drm_dp_link_addr_reply_port *port_msg)
{
	struct drm_dp_mst_port *port;
	bool ret;
	bool created = false;
	int old_pdt = 0;
	int old_ddps = 0;
	port = drm_dp_get_port(mstb, port_msg->port_number);
	if (!port) {
		port = kzalloc(sizeof(*port), GFP_KERNEL);
		if (!port)
			return;
		kref_init(&port->kref);
		port->parent = mstb;
		port->port_num = port_msg->port_number;
		port->mgr = mstb->mgr;
		port->aux.name = "DPMST";
		port->aux.dev = dev;
		created = true;
	} else {
		old_pdt = port->pdt;
		old_ddps = port->ddps;
	}

	port->pdt = port_msg->peer_device_type;
	port->input = port_msg->input_port;
	port->mcs = port_msg->mcs;
	port->ddps = port_msg->ddps;
	port->ldps = port_msg->legacy_device_plug_status;
	port->dpcd_rev = port_msg->dpcd_revision;
	port->num_sdp_streams = port_msg->num_sdp_streams;
	port->num_sdp_stream_sinks = port_msg->num_sdp_stream_sinks;
	memcpy(port->guid, port_msg->peer_guid, 16);

	/* manage mstb port lists with mgr lock - take a reference
	   for this list */
	if (created) {
		mutex_lock(&mstb->mgr->lock);
		kref_get(&port->kref);
		list_add(&port->next, &mstb->ports);
		mutex_unlock(&mstb->mgr->lock);
	}

	if (old_ddps != port->ddps) {
		if (port->ddps) {
			drm_dp_check_port_guid(mstb, port);
			if (!port->input)
				drm_dp_send_enum_path_resources(mstb->mgr, mstb, port);
		} else {
			port->guid_valid = false;
			port->available_pbn = 0;
			}
	}

	if (old_pdt != port->pdt && !port->input) {
		drm_dp_port_teardown_pdt(port, old_pdt);

		ret = drm_dp_port_setup_pdt(port);
		if (ret == true) {
			drm_dp_send_link_address(mstb->mgr, port->mstb);
			port->mstb->link_address_sent = true;
		}
	}

	if (created && !port->input) {
		char proppath[255];
		build_mst_prop_path(port, mstb, proppath);
		port->connector = (*mstb->mgr->cbs->add_connector)(mstb->mgr, port, proppath);
	}

	/* put reference to this port */
	drm_dp_put_port(port);
}

static void drm_dp_update_port(struct drm_dp_mst_branch *mstb,
			       struct drm_dp_connection_status_notify *conn_stat)
{
	struct drm_dp_mst_port *port;
	int old_pdt;
	int old_ddps;
	bool dowork = false;
	port = drm_dp_get_port(mstb, conn_stat->port_number);
	if (!port)
		return;

	old_ddps = port->ddps;
	old_pdt = port->pdt;
	port->pdt = conn_stat->peer_device_type;
	port->mcs = conn_stat->message_capability_status;
	port->ldps = conn_stat->legacy_device_plug_status;
	port->ddps = conn_stat->displayport_device_plug_status;

	if (old_ddps != port->ddps) {
		if (port->ddps) {
			drm_dp_check_port_guid(mstb, port);
			dowork = true;
		} else {
			port->guid_valid = false;
			port->available_pbn = 0;
		}
	}
	if (old_pdt != port->pdt && !port->input) {
		drm_dp_port_teardown_pdt(port, old_pdt);

		if (drm_dp_port_setup_pdt(port))
			dowork = true;
	}

	drm_dp_put_port(port);
	if (dowork)
		queue_work(system_long_wq, &mstb->mgr->work);

}

static struct drm_dp_mst_branch *drm_dp_get_mst_branch_device(struct drm_dp_mst_topology_mgr *mgr,
							       u8 lct, u8 *rad)
{
	struct drm_dp_mst_branch *mstb;
	struct drm_dp_mst_port *port;
	int i;
	/* find the port by iterating down */
	mstb = mgr->mst_primary;

	for (i = 0; i < lct - 1; i++) {
		int shift = (i % 2) ? 0 : 4;
		int port_num = rad[i / 2] >> shift;

		list_for_each_entry(port, &mstb->ports, next) {
			if (port->port_num == port_num) {
				if (!port->mstb) {
					DRM_ERROR("failed to lookup MSTB with lct %d, rad %02x\n", lct, rad[0]);
					return NULL;
				}

				mstb = port->mstb;
				break;
			}
		}
	}
	kref_get(&mstb->kref);
	return mstb;
}

static void drm_dp_check_and_send_link_address(struct drm_dp_mst_topology_mgr *mgr,
					       struct drm_dp_mst_branch *mstb)
{
	struct drm_dp_mst_port *port;

	if (!mstb->link_address_sent) {
		drm_dp_send_link_address(mgr, mstb);
		mstb->link_address_sent = true;
	}
	list_for_each_entry(port, &mstb->ports, next) {
		if (port->input)
			continue;

		if (!port->ddps)
			continue;

		if (!port->available_pbn)
			drm_dp_send_enum_path_resources(mgr, mstb, port);

		if (port->mstb)
			drm_dp_check_and_send_link_address(mgr, port->mstb);
	}
}

static void drm_dp_mst_link_probe_work(struct work_struct *work)
{
	struct drm_dp_mst_topology_mgr *mgr = container_of(work, struct drm_dp_mst_topology_mgr, work);

	drm_dp_check_and_send_link_address(mgr, mgr->mst_primary);

}

static bool drm_dp_validate_guid(struct drm_dp_mst_topology_mgr *mgr,
				 u8 *guid)
{
	static u8 zero_guid[16];

	if (!memcmp(guid, zero_guid, 16)) {
		u64 salt = get_jiffies_64();
		memcpy(&guid[0], &salt, sizeof(u64));
		memcpy(&guid[8], &salt, sizeof(u64));
		return false;
	}
	return true;
}

#if 0
static int build_dpcd_read(struct drm_dp_sideband_msg_tx *msg, u8 port_num, u32 offset, u8 num_bytes)
{
	struct drm_dp_sideband_msg_req_body req;

	req.req_type = DP_REMOTE_DPCD_READ;
	req.u.dpcd_read.port_number = port_num;
	req.u.dpcd_read.dpcd_address = offset;
	req.u.dpcd_read.num_bytes = num_bytes;
	drm_dp_encode_sideband_req(&req, msg);

	return 0;
}
#endif

static int drm_dp_send_sideband_msg(struct drm_dp_mst_topology_mgr *mgr,
				    bool up, u8 *msg, int len)
{
	int ret;
	int regbase = up ? DP_SIDEBAND_MSG_UP_REP_BASE : DP_SIDEBAND_MSG_DOWN_REQ_BASE;
	int tosend, total, offset;
	int retries = 0;

retry:
	total = len;
	offset = 0;
	do {
		tosend = min3(mgr->max_dpcd_transaction_bytes, 16, total);

		ret = drm_dp_dpcd_write(mgr->aux, regbase + offset,
					&msg[offset],
					tosend);
		if (ret != tosend) {
			if (ret == -EIO && retries < 5) {
				retries++;
				goto retry;
			}
			DRM_DEBUG_KMS("failed to dpcd write %d %d\n", tosend, ret);
			WARN(1, "fail\n");

			return -EIO;
		}
		offset += tosend;
		total -= tosend;
	} while (total > 0);
	return 0;
}

static int set_hdr_from_dst_qlock(struct drm_dp_sideband_msg_hdr *hdr,
				  struct drm_dp_sideband_msg_tx *txmsg)
{
	struct drm_dp_mst_branch *mstb = txmsg->dst;

	/* both msg slots are full */
	if (txmsg->seqno == -1) {
		if (mstb->tx_slots[0] && mstb->tx_slots[1]) {
			DRM_DEBUG_KMS("%s: failed to find slot\n", __func__);
			return -EAGAIN;
		}
		if (mstb->tx_slots[0] == NULL && mstb->tx_slots[1] == NULL) {
			txmsg->seqno = mstb->last_seqno;
			mstb->last_seqno ^= 1;
		} else if (mstb->tx_slots[0] == NULL)
			txmsg->seqno = 0;
		else
			txmsg->seqno = 1;
		mstb->tx_slots[txmsg->seqno] = txmsg;
	}
	hdr->broadcast = 0;
	hdr->path_msg = txmsg->path_msg;
	hdr->lct = mstb->lct;
	hdr->lcr = mstb->lct - 1;
	if (mstb->lct > 1)
		memcpy(hdr->rad, mstb->rad, mstb->lct / 2);
	hdr->seqno = txmsg->seqno;
	return 0;
}
/*
 * process a single block of the next message in the sideband queue
 */
static int process_single_tx_qlock(struct drm_dp_mst_topology_mgr *mgr,
				   struct drm_dp_sideband_msg_tx *txmsg,
				   bool up)
{
	u8 chunk[48];
	struct drm_dp_sideband_msg_hdr hdr;
	int len, space, idx, tosend;
	int ret;

	if (txmsg->state == DRM_DP_SIDEBAND_TX_QUEUED) {
		txmsg->seqno = -1;
		txmsg->state = DRM_DP_SIDEBAND_TX_START_SEND;
	}

	/* make hdr from dst mst - for replies use seqno
	   otherwise assign one */
	ret = set_hdr_from_dst_qlock(&hdr, txmsg);
	if (ret < 0)
		return ret;

	/* amount left to send in this message */
	len = txmsg->cur_len - txmsg->cur_offset;

	/* 48 - sideband msg size - 1 byte for data CRC, x header bytes */
	space = 48 - 1 - drm_dp_calc_sb_hdr_size(&hdr);

	tosend = min(len, space);
	if (len == txmsg->cur_len)
		hdr.somt = 1;
	if (space >= len)
		hdr.eomt = 1;


	hdr.msg_len = tosend + 1;
	drm_dp_encode_sideband_msg_hdr(&hdr, chunk, &idx);
	memcpy(&chunk[idx], &txmsg->msg[txmsg->cur_offset], tosend);
	/* add crc at end */
	drm_dp_crc_sideband_chunk_req(&chunk[idx], tosend);
	idx += tosend + 1;

	ret = drm_dp_send_sideband_msg(mgr, up, chunk, idx);
	if (ret) {
		DRM_DEBUG_KMS("sideband msg failed to send\n");
		return ret;
	}

	txmsg->cur_offset += tosend;
	if (txmsg->cur_offset == txmsg->cur_len) {
		txmsg->state = DRM_DP_SIDEBAND_TX_SENT;
		return 1;
	}
	return 0;
}

/* must be called holding qlock */
static void process_single_down_tx_qlock(struct drm_dp_mst_topology_mgr *mgr)
{
	struct drm_dp_sideband_msg_tx *txmsg;
	int ret;

	/* construct a chunk from the first msg in the tx_msg queue */
	if (list_empty(&mgr->tx_msg_downq)) {
		mgr->tx_down_in_progress = false;
		return;
	}
	mgr->tx_down_in_progress = true;

	txmsg = list_first_entry(&mgr->tx_msg_downq, struct drm_dp_sideband_msg_tx, next);
	ret = process_single_tx_qlock(mgr, txmsg, false);
	if (ret == 1) {
		/* txmsg is sent it should be in the slots now */
		list_del(&txmsg->next);
	} else if (ret) {
		DRM_DEBUG_KMS("failed to send msg in q %d\n", ret);
		list_del(&txmsg->next);
		if (txmsg->seqno != -1)
			txmsg->dst->tx_slots[txmsg->seqno] = NULL;
		txmsg->state = DRM_DP_SIDEBAND_TX_TIMEOUT;
		wake_up(&mgr->tx_waitq);
	}
	if (list_empty(&mgr->tx_msg_downq)) {
		mgr->tx_down_in_progress = false;
		return;
	}
}

/* called holding qlock */
static void process_single_up_tx_qlock(struct drm_dp_mst_topology_mgr *mgr)
{
	struct drm_dp_sideband_msg_tx *txmsg;
	int ret;

	/* construct a chunk from the first msg in the tx_msg queue */
	if (list_empty(&mgr->tx_msg_upq)) {
		mgr->tx_up_in_progress = false;
		return;
	}

	txmsg = list_first_entry(&mgr->tx_msg_upq, struct drm_dp_sideband_msg_tx, next);
	ret = process_single_tx_qlock(mgr, txmsg, true);
	if (ret == 1) {
		/* up txmsgs aren't put in slots - so free after we send it */
		list_del(&txmsg->next);
		kfree(txmsg);
	} else if (ret)
		DRM_DEBUG_KMS("failed to send msg in q %d\n", ret);
	mgr->tx_up_in_progress = true;
}

static void drm_dp_queue_down_tx(struct drm_dp_mst_topology_mgr *mgr,
				 struct drm_dp_sideband_msg_tx *txmsg)
{
	mutex_lock(&mgr->qlock);
	list_add_tail(&txmsg->next, &mgr->tx_msg_downq);
	if (!mgr->tx_down_in_progress)
		process_single_down_tx_qlock(mgr);
	mutex_unlock(&mgr->qlock);
}

static int drm_dp_send_link_address(struct drm_dp_mst_topology_mgr *mgr,
				    struct drm_dp_mst_branch *mstb)
{
	int len;
	struct drm_dp_sideband_msg_tx *txmsg;
	int ret;

	txmsg = kzalloc(sizeof(*txmsg), GFP_KERNEL);
	if (!txmsg)
		return -ENOMEM;

	txmsg->dst = mstb;
	len = build_link_address(txmsg);

	drm_dp_queue_down_tx(mgr, txmsg);

	ret = drm_dp_mst_wait_tx_reply(mstb, txmsg);
	if (ret > 0) {
		int i;

		if (txmsg->reply.reply_type == 1)
			DRM_DEBUG_KMS("link address nak received\n");
		else {
			DRM_DEBUG_KMS("link address reply: %d\n", txmsg->reply.u.link_addr.nports);
			for (i = 0; i < txmsg->reply.u.link_addr.nports; i++) {
				DRM_DEBUG_KMS("port %d: input %d, pdt: %d, pn: %d, dpcd_rev: %02x, mcs: %d, ddps: %d, ldps %d, sdp %d/%d\n", i,
				       txmsg->reply.u.link_addr.ports[i].input_port,
				       txmsg->reply.u.link_addr.ports[i].peer_device_type,
				       txmsg->reply.u.link_addr.ports[i].port_number,
				       txmsg->reply.u.link_addr.ports[i].dpcd_revision,
				       txmsg->reply.u.link_addr.ports[i].mcs,
				       txmsg->reply.u.link_addr.ports[i].ddps,
				       txmsg->reply.u.link_addr.ports[i].legacy_device_plug_status,
				       txmsg->reply.u.link_addr.ports[i].num_sdp_streams,
				       txmsg->reply.u.link_addr.ports[i].num_sdp_stream_sinks);
			}
			for (i = 0; i < txmsg->reply.u.link_addr.nports; i++) {
				drm_dp_add_port(mstb, mgr->dev, &txmsg->reply.u.link_addr.ports[i]);
			}
			(*mgr->cbs->hotplug)(mgr);
		}
	} else
		DRM_DEBUG_KMS("link address failed %d\n", ret);

	kfree(txmsg);
	return 0;
}

static int drm_dp_send_enum_path_resources(struct drm_dp_mst_topology_mgr *mgr,
					   struct drm_dp_mst_branch *mstb,
					   struct drm_dp_mst_port *port)
{
	int len;
	struct drm_dp_sideband_msg_tx *txmsg;
	int ret;

	txmsg = kzalloc(sizeof(*txmsg), GFP_KERNEL);
	if (!txmsg)
		return -ENOMEM;

	txmsg->dst = mstb;
	len = build_enum_path_resources(txmsg, port->port_num);

	drm_dp_queue_down_tx(mgr, txmsg);

	ret = drm_dp_mst_wait_tx_reply(mstb, txmsg);
	if (ret > 0) {
		if (txmsg->reply.reply_type == 1)
			DRM_DEBUG_KMS("enum path resources nak received\n");
		else {
			if (port->port_num != txmsg->reply.u.path_resources.port_number)
				DRM_ERROR("got incorrect port in response\n");
			DRM_DEBUG_KMS("enum path resources %d: %d %d\n", txmsg->reply.u.path_resources.port_number, txmsg->reply.u.path_resources.full_payload_bw_number,
			       txmsg->reply.u.path_resources.avail_payload_bw_number);
			port->available_pbn = txmsg->reply.u.path_resources.avail_payload_bw_number;
		}
	}

	kfree(txmsg);
	return 0;
}

int drm_dp_payload_send_msg(struct drm_dp_mst_topology_mgr *mgr,
			    struct drm_dp_mst_port *port,
			    int id,
			    int pbn)
{
	struct drm_dp_sideband_msg_tx *txmsg;
	struct drm_dp_mst_branch *mstb;
	int len, ret;

	mstb = drm_dp_get_validated_mstb_ref(mgr, port->parent);
	if (!mstb)
		return -EINVAL;

	txmsg = kzalloc(sizeof(*txmsg), GFP_KERNEL);
	if (!txmsg) {
		ret = -ENOMEM;
		goto fail_put;
	}

	txmsg->dst = mstb;
	len = build_allocate_payload(txmsg, port->port_num,
				     id,
				     pbn);

	drm_dp_queue_down_tx(mgr, txmsg);

	ret = drm_dp_mst_wait_tx_reply(mstb, txmsg);
	if (ret > 0) {
		if (txmsg->reply.reply_type == 1) {
			ret = -EINVAL;
		} else
			ret = 0;
	}
	kfree(txmsg);
fail_put:
	drm_dp_put_mst_branch_device(mstb);
	return ret;
}

static int drm_dp_create_payload_step1(struct drm_dp_mst_topology_mgr *mgr,
				       int id,
				       struct drm_dp_payload *payload)
{
	int ret;

	ret = drm_dp_dpcd_write_payload(mgr, id, payload);
	if (ret < 0) {
		payload->payload_state = 0;
		return ret;
	}
	payload->payload_state = DP_PAYLOAD_LOCAL;
	return 0;
}

int drm_dp_create_payload_step2(struct drm_dp_mst_topology_mgr *mgr,
				struct drm_dp_mst_port *port,
				int id,
				struct drm_dp_payload *payload)
{
	int ret;
	ret = drm_dp_payload_send_msg(mgr, port, id, port->vcpi.pbn);
	if (ret < 0)
		return ret;
	payload->payload_state = DP_PAYLOAD_REMOTE;
	return ret;
}

int drm_dp_destroy_payload_step1(struct drm_dp_mst_topology_mgr *mgr,
				 struct drm_dp_mst_port *port,
				 int id,
				 struct drm_dp_payload *payload)
{
	DRM_DEBUG_KMS("\n");
	/* its okay for these to fail */
	if (port) {
		drm_dp_payload_send_msg(mgr, port, id, 0);
	}

	drm_dp_dpcd_write_payload(mgr, id, payload);
	payload->payload_state = 0;
	return 0;
}

int drm_dp_destroy_payload_step2(struct drm_dp_mst_topology_mgr *mgr,
				 int id,
				 struct drm_dp_payload *payload)
{
	payload->payload_state = 0;
	return 0;
}

/**
 * drm_dp_update_payload_part1() - Execute payload update part 1
 * @mgr: manager to use.
 *
 * This iterates over all proposed virtual channels, and tries to
 * allocate space in the link for them. For 0->slots transitions,
 * this step just writes the VCPI to the MST device. For slots->0
 * transitions, this writes the updated VCPIs and removes the
 * remote VC payloads.
 *
 * after calling this the driver should generate ACT and payload
 * packets.
 */
int drm_dp_update_payload_part1(struct drm_dp_mst_topology_mgr *mgr)
{
	int i;
	int cur_slots = 1;
	struct drm_dp_payload req_payload;
	struct drm_dp_mst_port *port;

	mutex_lock(&mgr->payload_lock);
	for (i = 0; i < mgr->max_payloads; i++) {
		/* solve the current payloads - compare to the hw ones
		   - update the hw view */
		req_payload.start_slot = cur_slots;
		if (mgr->proposed_vcpis[i]) {
			port = container_of(mgr->proposed_vcpis[i], struct drm_dp_mst_port, vcpi);
			req_payload.num_slots = mgr->proposed_vcpis[i]->num_slots;
		} else {
			port = NULL;
			req_payload.num_slots = 0;
		}
		/* work out what is required to happen with this payload */
		if (mgr->payloads[i].start_slot != req_payload.start_slot ||
		    mgr->payloads[i].num_slots != req_payload.num_slots) {

			/* need to push an update for this payload */
			if (req_payload.num_slots) {
				drm_dp_create_payload_step1(mgr, i + 1, &req_payload);
				mgr->payloads[i].num_slots = req_payload.num_slots;
			} else if (mgr->payloads[i].num_slots) {
				mgr->payloads[i].num_slots = 0;
				drm_dp_destroy_payload_step1(mgr, port, i + 1, &mgr->payloads[i]);
				req_payload.payload_state = mgr->payloads[i].payload_state;
			} else
				req_payload.payload_state = 0;

			mgr->payloads[i].start_slot = req_payload.start_slot;
			mgr->payloads[i].payload_state = req_payload.payload_state;
		}
		cur_slots += req_payload.num_slots;
	}
	mutex_unlock(&mgr->payload_lock);

	return 0;
}
EXPORT_SYMBOL(drm_dp_update_payload_part1);

/**
 * drm_dp_update_payload_part2() - Execute payload update part 2
 * @mgr: manager to use.
 *
 * This iterates over all proposed virtual channels, and tries to
 * allocate space in the link for them. For 0->slots transitions,
 * this step writes the remote VC payload commands. For slots->0
 * this just resets some internal state.
 */
int drm_dp_update_payload_part2(struct drm_dp_mst_topology_mgr *mgr)
{
	struct drm_dp_mst_port *port;
	int i;
	int ret;
	mutex_lock(&mgr->payload_lock);
	for (i = 0; i < mgr->max_payloads; i++) {

		if (!mgr->proposed_vcpis[i])
			continue;

		port = container_of(mgr->proposed_vcpis[i], struct drm_dp_mst_port, vcpi);

		DRM_DEBUG_KMS("payload %d %d\n", i, mgr->payloads[i].payload_state);
		if (mgr->payloads[i].payload_state == DP_PAYLOAD_LOCAL) {
			ret = drm_dp_create_payload_step2(mgr, port, i + 1, &mgr->payloads[i]);
		} else if (mgr->payloads[i].payload_state == DP_PAYLOAD_DELETE_LOCAL) {
			ret = drm_dp_destroy_payload_step2(mgr, i + 1, &mgr->payloads[i]);
		}
		if (ret) {
			mutex_unlock(&mgr->payload_lock);
			return ret;
		}
	}
	mutex_unlock(&mgr->payload_lock);
	return 0;
}
EXPORT_SYMBOL(drm_dp_update_payload_part2);

#if 0 /* unused as of yet */
static int drm_dp_send_dpcd_read(struct drm_dp_mst_topology_mgr *mgr,
				 struct drm_dp_mst_port *port,
				 int offset, int size)
{
	int len;
	struct drm_dp_sideband_msg_tx *txmsg;

	txmsg = kzalloc(sizeof(*txmsg), GFP_KERNEL);
	if (!txmsg)
		return -ENOMEM;

	len = build_dpcd_read(txmsg, port->port_num, 0, 8);
	txmsg->dst = port->parent;

	drm_dp_queue_down_tx(mgr, txmsg);

	return 0;
}
#endif

static int drm_dp_send_dpcd_write(struct drm_dp_mst_topology_mgr *mgr,
				  struct drm_dp_mst_port *port,
				  int offset, int size, u8 *bytes)
{
	int len;
	int ret;
	struct drm_dp_sideband_msg_tx *txmsg;
	struct drm_dp_mst_branch *mstb;

	mstb = drm_dp_get_validated_mstb_ref(mgr, port->parent);
	if (!mstb)
		return -EINVAL;

	txmsg = kzalloc(sizeof(*txmsg), GFP_KERNEL);
	if (!txmsg) {
		ret = -ENOMEM;
		goto fail_put;
	}

	len = build_dpcd_write(txmsg, port->port_num, offset, size, bytes);
	txmsg->dst = mstb;

	drm_dp_queue_down_tx(mgr, txmsg);

	ret = drm_dp_mst_wait_tx_reply(mstb, txmsg);
	if (ret > 0) {
		if (txmsg->reply.reply_type == 1) {
			ret = -EINVAL;
		} else
			ret = 0;
	}
	kfree(txmsg);
fail_put:
	drm_dp_put_mst_branch_device(mstb);
	return ret;
}

static int drm_dp_encode_up_ack_reply(struct drm_dp_sideband_msg_tx *msg, u8 req_type)
{
	struct drm_dp_sideband_msg_reply_body reply;

	reply.reply_type = 1;
	reply.req_type = req_type;
	drm_dp_encode_sideband_reply(&reply, msg);
	return 0;
}

static int drm_dp_send_up_ack_reply(struct drm_dp_mst_topology_mgr *mgr,
				    struct drm_dp_mst_branch *mstb,
				    int req_type, int seqno, bool broadcast)
{
	struct drm_dp_sideband_msg_tx *txmsg;

	txmsg = kzalloc(sizeof(*txmsg), GFP_KERNEL);
	if (!txmsg)
		return -ENOMEM;

	txmsg->dst = mstb;
	txmsg->seqno = seqno;
	drm_dp_encode_up_ack_reply(txmsg, req_type);

	mutex_lock(&mgr->qlock);
	list_add_tail(&txmsg->next, &mgr->tx_msg_upq);
	if (!mgr->tx_up_in_progress) {
		process_single_up_tx_qlock(mgr);
	}
	mutex_unlock(&mgr->qlock);
	return 0;
}

static int drm_dp_get_vc_payload_bw(int dp_link_bw, int dp_link_count)
{
	switch (dp_link_bw) {
	case DP_LINK_BW_1_62:
		return 3 * dp_link_count;
	case DP_LINK_BW_2_7:
		return 5 * dp_link_count;
	case DP_LINK_BW_5_4:
		return 10 * dp_link_count;
	}
	return 0;
}

/**
 * drm_dp_mst_topology_mgr_set_mst() - Set the MST state for a topology manager
 * @mgr: manager to set state for
 * @mst_state: true to enable MST on this connector - false to disable.
 *
 * This is called by the driver when it detects an MST capable device plugged
 * into a DP MST capable port, or when a DP MST capable device is unplugged.
 */
int drm_dp_mst_topology_mgr_set_mst(struct drm_dp_mst_topology_mgr *mgr, bool mst_state)
{
	int ret = 0;
	struct drm_dp_mst_branch *mstb = NULL;

	mutex_lock(&mgr->lock);
	if (mst_state == mgr->mst_state)
		goto out_unlock;

	mgr->mst_state = mst_state;
	/* set the device into MST mode */
	if (mst_state) {
		WARN_ON(mgr->mst_primary);

		/* get dpcd info */
		ret = drm_dp_dpcd_read(mgr->aux, DP_DPCD_REV, mgr->dpcd, DP_RECEIVER_CAP_SIZE);
		if (ret != DP_RECEIVER_CAP_SIZE) {
			DRM_DEBUG_KMS("failed to read DPCD\n");
			goto out_unlock;
		}

		mgr->pbn_div = drm_dp_get_vc_payload_bw(mgr->dpcd[1], mgr->dpcd[2] & DP_MAX_LANE_COUNT_MASK);
		mgr->total_pbn = 2560;
		mgr->total_slots = DIV_ROUND_UP(mgr->total_pbn, mgr->pbn_div);
		mgr->avail_slots = mgr->total_slots;

		/* add initial branch device at LCT 1 */
		mstb = drm_dp_add_mst_branch_device(1, NULL);
		if (mstb == NULL) {
			ret = -ENOMEM;
			goto out_unlock;
		}
		mstb->mgr = mgr;

		/* give this the main reference */
		mgr->mst_primary = mstb;
		kref_get(&mgr->mst_primary->kref);

		{
			struct drm_dp_payload reset_pay;
			reset_pay.start_slot = 0;
			reset_pay.num_slots = 0x3f;
			drm_dp_dpcd_write_payload(mgr, 0, &reset_pay);
		}

		ret = drm_dp_dpcd_writeb(mgr->aux, DP_MSTM_CTRL,
					 DP_MST_EN | DP_UP_REQ_EN | DP_UPSTREAM_IS_SRC);
		if (ret < 0) {
			goto out_unlock;
		}


		/* sort out guid */
		ret = drm_dp_dpcd_read(mgr->aux, DP_GUID, mgr->guid, 16);
		if (ret != 16) {
			DRM_DEBUG_KMS("failed to read DP GUID %d\n", ret);
			goto out_unlock;
		}

		mgr->guid_valid = drm_dp_validate_guid(mgr, mgr->guid);
		if (!mgr->guid_valid) {
			ret = drm_dp_dpcd_write(mgr->aux, DP_GUID, mgr->guid, 16);
			mgr->guid_valid = true;
		}

		queue_work(system_long_wq, &mgr->work);

		ret = 0;
	} else {
		/* disable MST on the device */
		mstb = mgr->mst_primary;
		mgr->mst_primary = NULL;
		/* this can fail if the device is gone */
		drm_dp_dpcd_writeb(mgr->aux, DP_MSTM_CTRL, 0);
		ret = 0;
		memset(mgr->payloads, 0, mgr->max_payloads * sizeof(struct drm_dp_payload));
		mgr->payload_mask = 0;
		set_bit(0, &mgr->payload_mask);
	}

out_unlock:
	mutex_unlock(&mgr->lock);
	if (mstb)
		drm_dp_put_mst_branch_device(mstb);
	return ret;

}
EXPORT_SYMBOL(drm_dp_mst_topology_mgr_set_mst);

/**
 * drm_dp_mst_topology_mgr_suspend() - suspend the MST manager
 * @mgr: manager to suspend
 *
 * This function tells the MST device that we can't handle UP messages
 * anymore. This should stop it from sending any since we are suspended.
 */
void drm_dp_mst_topology_mgr_suspend(struct drm_dp_mst_topology_mgr *mgr)
{
	mutex_lock(&mgr->lock);
	drm_dp_dpcd_writeb(mgr->aux, DP_MSTM_CTRL,
			   DP_MST_EN | DP_UPSTREAM_IS_SRC);
	mutex_unlock(&mgr->lock);
}
EXPORT_SYMBOL(drm_dp_mst_topology_mgr_suspend);

/**
 * drm_dp_mst_topology_mgr_resume() - resume the MST manager
 * @mgr: manager to resume
 *
 * This will fetch DPCD and see if the device is still there,
 * if it is, it will rewrite the MSTM control bits, and return.
 *
 * if the device fails this returns -1, and the driver should do
 * a full MST reprobe, in case we were undocked.
 */
int drm_dp_mst_topology_mgr_resume(struct drm_dp_mst_topology_mgr *mgr)
{
	int ret = 0;

	mutex_lock(&mgr->lock);

	if (mgr->mst_primary) {
		int sret;
		sret = drm_dp_dpcd_read(mgr->aux, DP_DPCD_REV, mgr->dpcd, DP_RECEIVER_CAP_SIZE);
		if (sret != DP_RECEIVER_CAP_SIZE) {
			DRM_DEBUG_KMS("dpcd read failed - undocked during suspend?\n");
			ret = -1;
			goto out_unlock;
		}

		ret = drm_dp_dpcd_writeb(mgr->aux, DP_MSTM_CTRL,
					 DP_MST_EN | DP_UP_REQ_EN | DP_UPSTREAM_IS_SRC);
		if (ret < 0) {
			DRM_DEBUG_KMS("mst write failed - undocked during suspend?\n");
			ret = -1;
			goto out_unlock;
		}
		ret = 0;
	} else
		ret = -1;

out_unlock:
	mutex_unlock(&mgr->lock);
	return ret;
}
EXPORT_SYMBOL(drm_dp_mst_topology_mgr_resume);

static void drm_dp_get_one_sb_msg(struct drm_dp_mst_topology_mgr *mgr, bool up)
{
	int len;
	u8 replyblock[32];
	int replylen, origlen, curreply;
	int ret;
	struct drm_dp_sideband_msg_rx *msg;
	int basereg = up ? DP_SIDEBAND_MSG_UP_REQ_BASE : DP_SIDEBAND_MSG_DOWN_REP_BASE;
	msg = up ? &mgr->up_req_recv : &mgr->down_rep_recv;

	len = min(mgr->max_dpcd_transaction_bytes, 16);
	ret = drm_dp_dpcd_read(mgr->aux, basereg,
			       replyblock, len);
	if (ret != len) {
		DRM_DEBUG_KMS("failed to read DPCD down rep %d %d\n", len, ret);
		return;
	}
	ret = drm_dp_sideband_msg_build(msg, replyblock, len, true);
	if (!ret) {
		DRM_DEBUG_KMS("sideband msg build failed %d\n", replyblock[0]);
		return;
	}
	replylen = msg->curchunk_len + msg->curchunk_hdrlen;

	origlen = replylen;
	replylen -= len;
	curreply = len;
	while (replylen > 0) {
		len = min3(replylen, mgr->max_dpcd_transaction_bytes, 16);
		ret = drm_dp_dpcd_read(mgr->aux, basereg + curreply,
				    replyblock, len);
		if (ret != len) {
			DRM_DEBUG_KMS("failed to read a chunk\n");
		}
		ret = drm_dp_sideband_msg_build(msg, replyblock, len, false);
		if (ret == false)
			DRM_DEBUG_KMS("failed to build sideband msg\n");
		curreply += len;
		replylen -= len;
	}
}

static int drm_dp_mst_handle_down_rep(struct drm_dp_mst_topology_mgr *mgr)
{
	int ret = 0;

	drm_dp_get_one_sb_msg(mgr, false);

	if (mgr->down_rep_recv.have_eomt) {
		struct drm_dp_sideband_msg_tx *txmsg;
		struct drm_dp_mst_branch *mstb;
		int slot = -1;
		mstb = drm_dp_get_mst_branch_device(mgr,
						    mgr->down_rep_recv.initial_hdr.lct,
						    mgr->down_rep_recv.initial_hdr.rad);

		if (!mstb) {
			DRM_DEBUG_KMS("Got MST reply from unknown device %d\n", mgr->down_rep_recv.initial_hdr.lct);
			memset(&mgr->down_rep_recv, 0, sizeof(struct drm_dp_sideband_msg_rx));
			return 0;
		}

		/* find the message */
		slot = mgr->down_rep_recv.initial_hdr.seqno;
		mutex_lock(&mgr->qlock);
		txmsg = mstb->tx_slots[slot];
		/* remove from slots */
		mutex_unlock(&mgr->qlock);

		if (!txmsg) {
			DRM_DEBUG_KMS("Got MST reply with no msg %p %d %d %02x %02x\n",
			       mstb,
			       mgr->down_rep_recv.initial_hdr.seqno,
			       mgr->down_rep_recv.initial_hdr.lct,
				      mgr->down_rep_recv.initial_hdr.rad[0],
				      mgr->down_rep_recv.msg[0]);
			drm_dp_put_mst_branch_device(mstb);
			memset(&mgr->down_rep_recv, 0, sizeof(struct drm_dp_sideband_msg_rx));
			return 0;
		}

		drm_dp_sideband_parse_reply(&mgr->down_rep_recv, &txmsg->reply);
		if (txmsg->reply.reply_type == 1) {
			DRM_DEBUG_KMS("Got NAK reply: req 0x%02x, reason 0x%02x, nak data 0x%02x\n", txmsg->reply.req_type, txmsg->reply.u.nak.reason, txmsg->reply.u.nak.nak_data);
		}

		memset(&mgr->down_rep_recv, 0, sizeof(struct drm_dp_sideband_msg_rx));
		drm_dp_put_mst_branch_device(mstb);

		mutex_lock(&mgr->qlock);
		txmsg->state = DRM_DP_SIDEBAND_TX_RX;
		mstb->tx_slots[slot] = NULL;
		mutex_unlock(&mgr->qlock);

		wake_up(&mgr->tx_waitq);
	}
	return ret;
}

static int drm_dp_mst_handle_up_req(struct drm_dp_mst_topology_mgr *mgr)
{
	int ret = 0;
	drm_dp_get_one_sb_msg(mgr, true);

	if (mgr->up_req_recv.have_eomt) {
		struct drm_dp_sideband_msg_req_body msg;
		struct drm_dp_mst_branch *mstb;
		bool seqno;
		mstb = drm_dp_get_mst_branch_device(mgr,
						    mgr->up_req_recv.initial_hdr.lct,
						    mgr->up_req_recv.initial_hdr.rad);
		if (!mstb) {
			DRM_DEBUG_KMS("Got MST reply from unknown device %d\n", mgr->up_req_recv.initial_hdr.lct);
			memset(&mgr->up_req_recv, 0, sizeof(struct drm_dp_sideband_msg_rx));
			return 0;
		}

		seqno = mgr->up_req_recv.initial_hdr.seqno;
		drm_dp_sideband_parse_req(&mgr->up_req_recv, &msg);

		if (msg.req_type == DP_CONNECTION_STATUS_NOTIFY) {
			drm_dp_send_up_ack_reply(mgr, mstb, msg.req_type, seqno, false);
			drm_dp_update_port(mstb, &msg.u.conn_stat);
			DRM_DEBUG_KMS("Got CSN: pn: %d ldps:%d ddps: %d mcs: %d ip: %d pdt: %d\n", msg.u.conn_stat.port_number, msg.u.conn_stat.legacy_device_plug_status, msg.u.conn_stat.displayport_device_plug_status, msg.u.conn_stat.message_capability_status, msg.u.conn_stat.input_port, msg.u.conn_stat.peer_device_type);
			(*mgr->cbs->hotplug)(mgr);

		} else if (msg.req_type == DP_RESOURCE_STATUS_NOTIFY) {
			drm_dp_send_up_ack_reply(mgr, mstb, msg.req_type, seqno, false);
			DRM_DEBUG_KMS("Got RSN: pn: %d avail_pbn %d\n", msg.u.resource_stat.port_number, msg.u.resource_stat.available_pbn);
		}

		drm_dp_put_mst_branch_device(mstb);
		memset(&mgr->up_req_recv, 0, sizeof(struct drm_dp_sideband_msg_rx));
	}
	return ret;
}

/**
 * drm_dp_mst_hpd_irq() - MST hotplug IRQ notify
 * @mgr: manager to notify irq for.
 * @esi: 4 bytes from SINK_COUNT_ESI
 *
 * This should be called from the driver when it detects a short IRQ,
 * along with the value of the DEVICE_SERVICE_IRQ_VECTOR_ESI0. The
 * topology manager will process the sideband messages received as a result
 * of this.
 */
int drm_dp_mst_hpd_irq(struct drm_dp_mst_topology_mgr *mgr, u8 *esi, bool *handled)
{
	int ret = 0;
	int sc;
	*handled = false;
	sc = esi[0] & 0x3f;

	if (sc != mgr->sink_count) {
		mgr->sink_count = sc;
		*handled = true;
	}

	if (esi[1] & DP_DOWN_REP_MSG_RDY) {
		ret = drm_dp_mst_handle_down_rep(mgr);
		*handled = true;
	}

	if (esi[1] & DP_UP_REQ_MSG_RDY) {
		ret |= drm_dp_mst_handle_up_req(mgr);
		*handled = true;
	}

	drm_dp_mst_kick_tx(mgr);
	return ret;
}
EXPORT_SYMBOL(drm_dp_mst_hpd_irq);

/**
 * drm_dp_mst_detect_port() - get connection status for an MST port
 * @mgr: manager for this port
 * @port: unverified pointer to a port
 *
 * This returns the current connection state for a port. It validates the
 * port pointer still exists so the caller doesn't require a reference
 */
enum drm_connector_status drm_dp_mst_detect_port(struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port)
{
	enum drm_connector_status status = connector_status_disconnected;

	/* we need to search for the port in the mgr in case its gone */
	port = drm_dp_get_validated_port_ref(mgr, port);
	if (!port)
		return connector_status_disconnected;

	if (!port->ddps)
		goto out;

	switch (port->pdt) {
	case DP_PEER_DEVICE_NONE:
	case DP_PEER_DEVICE_MST_BRANCHING:
		break;

	case DP_PEER_DEVICE_SST_SINK:
		status = connector_status_connected;
		break;
	case DP_PEER_DEVICE_DP_LEGACY_CONV:
		if (port->ldps)
			status = connector_status_connected;
		break;
	}
out:
	drm_dp_put_port(port);
	return status;
}
EXPORT_SYMBOL(drm_dp_mst_detect_port);

/**
 * drm_dp_mst_get_edid() - get EDID for an MST port
 * @connector: toplevel connector to get EDID for
 * @mgr: manager for this port
 * @port: unverified pointer to a port.
 *
 * This returns an EDID for the port connected to a connector,
 * It validates the pointer still exists so the caller doesn't require a
 * reference.
 */
struct edid *drm_dp_mst_get_edid(struct drm_connector *connector, struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port)
{
	struct edid *edid = NULL;

	/* we need to search for the port in the mgr in case its gone */
	port = drm_dp_get_validated_port_ref(mgr, port);
	if (!port)
		return NULL;

	edid = drm_get_edid(connector, &port->aux.ddc);
	drm_dp_put_port(port);
	return edid;
}
EXPORT_SYMBOL(drm_dp_mst_get_edid);

/**
 * drm_dp_find_vcpi_slots() - find slots for this PBN value
 * @mgr: manager to use
 * @pbn: payload bandwidth to convert into slots.
 */
int drm_dp_find_vcpi_slots(struct drm_dp_mst_topology_mgr *mgr,
			   int pbn)
{
	int num_slots;

	num_slots = DIV_ROUND_UP(pbn, mgr->pbn_div);

	if (num_slots > mgr->avail_slots)
		return -ENOSPC;
	return num_slots;
}
EXPORT_SYMBOL(drm_dp_find_vcpi_slots);

static int drm_dp_init_vcpi(struct drm_dp_mst_topology_mgr *mgr,
			    struct drm_dp_vcpi *vcpi, int pbn)
{
	int num_slots;
	int ret;

	num_slots = DIV_ROUND_UP(pbn, mgr->pbn_div);

	if (num_slots > mgr->avail_slots)
		return -ENOSPC;

	vcpi->pbn = pbn;
	vcpi->aligned_pbn = num_slots * mgr->pbn_div;
	vcpi->num_slots = num_slots;

	ret = drm_dp_mst_assign_payload_id(mgr, vcpi);
	if (ret < 0)
		return ret;
	return 0;
}

/**
 * drm_dp_mst_allocate_vcpi() - Allocate a virtual channel
 * @mgr: manager for this port
 * @port: port to allocate a virtual channel for.
 * @pbn: payload bandwidth number to request
 * @slots: returned number of slots for this PBN.
 */
bool drm_dp_mst_allocate_vcpi(struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port, int pbn, int *slots)
{
	int ret;

	port = drm_dp_get_validated_port_ref(mgr, port);
	if (!port)
		return false;

	if (port->vcpi.vcpi > 0) {
		DRM_DEBUG_KMS("payload: vcpi %d already allocated for pbn %d - requested pbn %d\n", port->vcpi.vcpi, port->vcpi.pbn, pbn);
		if (pbn == port->vcpi.pbn) {
			*slots = port->vcpi.num_slots;
			return true;
		}
	}

	ret = drm_dp_init_vcpi(mgr, &port->vcpi, pbn);
	if (ret) {
		DRM_DEBUG_KMS("failed to init vcpi %d %d %d\n", DIV_ROUND_UP(pbn, mgr->pbn_div), mgr->avail_slots, ret);
		goto out;
	}
	DRM_DEBUG_KMS("initing vcpi for %d %d\n", pbn, port->vcpi.num_slots);
	*slots = port->vcpi.num_slots;

	drm_dp_put_port(port);
	return true;
out:
	return false;
}
EXPORT_SYMBOL(drm_dp_mst_allocate_vcpi);

/**
 * drm_dp_mst_reset_vcpi_slots() - Reset number of slots to 0 for VCPI
 * @mgr: manager for this port
 * @port: unverified pointer to a port.
 *
 * This just resets the number of slots for the ports VCPI for later programming.
 */
void drm_dp_mst_reset_vcpi_slots(struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port)
{
	port = drm_dp_get_validated_port_ref(mgr, port);
	if (!port)
		return;
	port->vcpi.num_slots = 0;
	drm_dp_put_port(port);
}
EXPORT_SYMBOL(drm_dp_mst_reset_vcpi_slots);

/**
 * drm_dp_mst_deallocate_vcpi() - deallocate a VCPI
 * @mgr: manager for this port
 * @port: unverified port to deallocate vcpi for
 */
void drm_dp_mst_deallocate_vcpi(struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port)
{
	port = drm_dp_get_validated_port_ref(mgr, port);
	if (!port)
		return;

	drm_dp_mst_put_payload_id(mgr, port->vcpi.vcpi);
	port->vcpi.num_slots = 0;
	port->vcpi.pbn = 0;
	port->vcpi.aligned_pbn = 0;
	port->vcpi.vcpi = 0;
	drm_dp_put_port(port);
}
EXPORT_SYMBOL(drm_dp_mst_deallocate_vcpi);

static int drm_dp_dpcd_write_payload(struct drm_dp_mst_topology_mgr *mgr,
				     int id, struct drm_dp_payload *payload)
{
	u8 payload_alloc[3], status;
	int ret;
	int retries = 0;

	drm_dp_dpcd_writeb(mgr->aux, DP_PAYLOAD_TABLE_UPDATE_STATUS,
			   DP_PAYLOAD_TABLE_UPDATED);

	payload_alloc[0] = id;
	payload_alloc[1] = payload->start_slot;
	payload_alloc[2] = payload->num_slots;

	ret = drm_dp_dpcd_write(mgr->aux, DP_PAYLOAD_ALLOCATE_SET, payload_alloc, 3);
	if (ret != 3) {
		DRM_DEBUG_KMS("failed to write payload allocation %d\n", ret);
		goto fail;
	}

retry:
	ret = drm_dp_dpcd_readb(mgr->aux, DP_PAYLOAD_TABLE_UPDATE_STATUS, &status);
	if (ret < 0) {
		DRM_DEBUG_KMS("failed to read payload table status %d\n", ret);
		goto fail;
	}

	if (!(status & DP_PAYLOAD_TABLE_UPDATED)) {
		retries++;
		if (retries < 20) {
			usleep_range(10000, 20000);
			goto retry;
		}
		DRM_DEBUG_KMS("status not set after read payload table status %d\n", status);
		ret = -EINVAL;
		goto fail;
	}
	ret = 0;
fail:
	return ret;
}


/**
 * drm_dp_check_act_status() - Check ACT handled status.
 * @mgr: manager to use
 *
 * Check the payload status bits in the DPCD for ACT handled completion.
 */
int drm_dp_check_act_status(struct drm_dp_mst_topology_mgr *mgr)
{
	u8 status;
	int ret;
	int count = 0;

	do {
		ret = drm_dp_dpcd_readb(mgr->aux, DP_PAYLOAD_TABLE_UPDATE_STATUS, &status);

		if (ret < 0) {
			DRM_DEBUG_KMS("failed to read payload table status %d\n", ret);
			goto fail;
		}

		if (status & DP_PAYLOAD_ACT_HANDLED)
			break;
		count++;
		udelay(100);

	} while (count < 30);

	if (!(status & DP_PAYLOAD_ACT_HANDLED)) {
		DRM_DEBUG_KMS("failed to get ACT bit %d after %d retries\n", status, count);
		ret = -EINVAL;
		goto fail;
	}
	return 0;
fail:
	return ret;
}
EXPORT_SYMBOL(drm_dp_check_act_status);

/**
 * drm_dp_calc_pbn_mode() - Calculate the PBN for a mode.
 * @clock: dot clock for the mode
 * @bpp: bpp for the mode.
 *
 * This uses the formula in the spec to calculate the PBN value for a mode.
 */
int drm_dp_calc_pbn_mode(int clock, int bpp)
{
	fixed20_12 pix_bw;
	fixed20_12 fbpp;
	fixed20_12 result;
	fixed20_12 margin, tmp;
	u32 res;

	pix_bw.full = dfixed_const(clock);
	fbpp.full = dfixed_const(bpp);
	tmp.full = dfixed_const(8);
	fbpp.full = dfixed_div(fbpp, tmp);

	result.full = dfixed_mul(pix_bw, fbpp);
	margin.full = dfixed_const(54);
	tmp.full = dfixed_const(64);
	margin.full = dfixed_div(margin, tmp);
	result.full = dfixed_div(result, margin);

	margin.full = dfixed_const(1006);
	tmp.full = dfixed_const(1000);
	margin.full = dfixed_div(margin, tmp);
	result.full = dfixed_mul(result, margin);

	result.full = dfixed_div(result, tmp);
	result.full = dfixed_ceil(result);
	res = dfixed_trunc(result);
	return res;
}
EXPORT_SYMBOL(drm_dp_calc_pbn_mode);

static int test_calc_pbn_mode(void)
{
	int ret;
	ret = drm_dp_calc_pbn_mode(154000, 30);
	if (ret != 689)
		return -EINVAL;
	ret = drm_dp_calc_pbn_mode(234000, 30);
	if (ret != 1047)
		return -EINVAL;
	return 0;
}

/* we want to kick the TX after we've ack the up/down IRQs. */
static void drm_dp_mst_kick_tx(struct drm_dp_mst_topology_mgr *mgr)
{
	queue_work(system_long_wq, &mgr->tx_work);
}

static void drm_dp_mst_dump_mstb(struct seq_file *m,
				 struct drm_dp_mst_branch *mstb)
{
	struct drm_dp_mst_port *port;
	int tabs = mstb->lct;
	char prefix[10];
	int i;

	for (i = 0; i < tabs; i++)
		prefix[i] = '\t';
	prefix[i] = '\0';

	seq_printf(m, "%smst: %p, %d\n", prefix, mstb, mstb->num_ports);
	list_for_each_entry(port, &mstb->ports, next) {
		seq_printf(m, "%sport: %d: ddps: %d ldps: %d, %p, conn: %p\n", prefix, port->port_num, port->ddps, port->ldps, port, port->connector);
		if (port->mstb)
			drm_dp_mst_dump_mstb(m, port->mstb);
	}
}

static bool dump_dp_payload_table(struct drm_dp_mst_topology_mgr *mgr,
				  char *buf)
{
	int ret;
	int i;
	for (i = 0; i < 4; i++) {
		ret = drm_dp_dpcd_read(mgr->aux, DP_PAYLOAD_TABLE_UPDATE_STATUS + (i * 16), &buf[i * 16], 16);
		if (ret != 16)
			break;
	}
	if (i == 4)
		return true;
	return false;
}

/**
 * drm_dp_mst_dump_topology(): dump topology to seq file.
 * @m: seq_file to dump output to
 * @mgr: manager to dump current topology for.
 *
 * helper to dump MST topology to a seq file for debugfs.
 */
void drm_dp_mst_dump_topology(struct seq_file *m,
			      struct drm_dp_mst_topology_mgr *mgr)
{
	int i;
	struct drm_dp_mst_port *port;
	mutex_lock(&mgr->lock);
	if (mgr->mst_primary)
		drm_dp_mst_dump_mstb(m, mgr->mst_primary);

	/* dump VCPIs */
	mutex_unlock(&mgr->lock);

	mutex_lock(&mgr->payload_lock);
	seq_printf(m, "vcpi: %lx\n", mgr->payload_mask);

	for (i = 0; i < mgr->max_payloads; i++) {
		if (mgr->proposed_vcpis[i]) {
			port = container_of(mgr->proposed_vcpis[i], struct drm_dp_mst_port, vcpi);
			seq_printf(m, "vcpi %d: %d %d %d\n", i, port->port_num, port->vcpi.vcpi, port->vcpi.num_slots);
		} else
			seq_printf(m, "vcpi %d:unsed\n", i);
	}
	for (i = 0; i < mgr->max_payloads; i++) {
		seq_printf(m, "payload %d: %d, %d, %d\n",
			   i,
			   mgr->payloads[i].payload_state,
			   mgr->payloads[i].start_slot,
			   mgr->payloads[i].num_slots);


	}
	mutex_unlock(&mgr->payload_lock);

	mutex_lock(&mgr->lock);
	if (mgr->mst_primary) {
		u8 buf[64];
		bool bret;
		int ret;
		ret = drm_dp_dpcd_read(mgr->aux, DP_DPCD_REV, buf, DP_RECEIVER_CAP_SIZE);
		seq_printf(m, "dpcd: ");
		for (i = 0; i < DP_RECEIVER_CAP_SIZE; i++)
			seq_printf(m, "%02x ", buf[i]);
		seq_printf(m, "\n");
		ret = drm_dp_dpcd_read(mgr->aux, DP_FAUX_CAP, buf, 2);
		seq_printf(m, "faux/mst: ");
		for (i = 0; i < 2; i++)
			seq_printf(m, "%02x ", buf[i]);
		seq_printf(m, "\n");
		ret = drm_dp_dpcd_read(mgr->aux, DP_MSTM_CTRL, buf, 1);
		seq_printf(m, "mst ctrl: ");
		for (i = 0; i < 1; i++)
			seq_printf(m, "%02x ", buf[i]);
		seq_printf(m, "\n");

		bret = dump_dp_payload_table(mgr, buf);
		if (bret == true) {
			seq_printf(m, "payload table: ");
			for (i = 0; i < 63; i++)
				seq_printf(m, "%02x ", buf[i]);
			seq_printf(m, "\n");
		}

	}

	mutex_unlock(&mgr->lock);

}
EXPORT_SYMBOL(drm_dp_mst_dump_topology);

static void drm_dp_tx_work(struct work_struct *work)
{
	struct drm_dp_mst_topology_mgr *mgr = container_of(work, struct drm_dp_mst_topology_mgr, tx_work);

	mutex_lock(&mgr->qlock);
	if (mgr->tx_down_in_progress)
		process_single_down_tx_qlock(mgr);
	mutex_unlock(&mgr->qlock);
}

/**
 * drm_dp_mst_topology_mgr_init - initialise a topology manager
 * @mgr: manager struct to initialise
 * @dev: device providing this structure - for i2c addition.
 * @aux: DP helper aux channel to talk to this device
 * @max_dpcd_transaction_bytes: hw specific DPCD transaction limit
 * @max_payloads: maximum number of payloads this GPU can source
 * @conn_base_id: the connector object ID the MST device is connected to.
 *
 * Return 0 for success, or negative error code on failure
 */
int drm_dp_mst_topology_mgr_init(struct drm_dp_mst_topology_mgr *mgr,
				 struct device *dev, struct drm_dp_aux *aux,
				 int max_dpcd_transaction_bytes,
				 int max_payloads, int conn_base_id)
{
	mutex_init(&mgr->lock);
	mutex_init(&mgr->qlock);
	mutex_init(&mgr->payload_lock);
	INIT_LIST_HEAD(&mgr->tx_msg_upq);
	INIT_LIST_HEAD(&mgr->tx_msg_downq);
	INIT_WORK(&mgr->work, drm_dp_mst_link_probe_work);
	INIT_WORK(&mgr->tx_work, drm_dp_tx_work);
	init_waitqueue_head(&mgr->tx_waitq);
	mgr->dev = dev;
	mgr->aux = aux;
	mgr->max_dpcd_transaction_bytes = max_dpcd_transaction_bytes;
	mgr->max_payloads = max_payloads;
	mgr->conn_base_id = conn_base_id;
	mgr->payloads = kcalloc(max_payloads, sizeof(struct drm_dp_payload), GFP_KERNEL);
	if (!mgr->payloads)
		return -ENOMEM;
	mgr->proposed_vcpis = kcalloc(max_payloads, sizeof(struct drm_dp_vcpi *), GFP_KERNEL);
	if (!mgr->proposed_vcpis)
		return -ENOMEM;
	set_bit(0, &mgr->payload_mask);
	test_calc_pbn_mode();
	return 0;
}
EXPORT_SYMBOL(drm_dp_mst_topology_mgr_init);

/**
 * drm_dp_mst_topology_mgr_destroy() - destroy topology manager.
 * @mgr: manager to destroy
 */
void drm_dp_mst_topology_mgr_destroy(struct drm_dp_mst_topology_mgr *mgr)
{
	mutex_lock(&mgr->payload_lock);
	kfree(mgr->payloads);
	mgr->payloads = NULL;
	kfree(mgr->proposed_vcpis);
	mgr->proposed_vcpis = NULL;
	mutex_unlock(&mgr->payload_lock);
	mgr->dev = NULL;
	mgr->aux = NULL;
}
EXPORT_SYMBOL(drm_dp_mst_topology_mgr_destroy);

/* I2C device */
static int drm_dp_mst_i2c_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs,
			       int num)
{
	struct drm_dp_aux *aux = adapter->algo_data;
	struct drm_dp_mst_port *port = container_of(aux, struct drm_dp_mst_port, aux);
	struct drm_dp_mst_branch *mstb;
	struct drm_dp_mst_topology_mgr *mgr = port->mgr;
	unsigned int i;
	bool reading = false;
	struct drm_dp_sideband_msg_req_body msg;
	struct drm_dp_sideband_msg_tx *txmsg = NULL;
	int ret;

	mstb = drm_dp_get_validated_mstb_ref(mgr, port->parent);
	if (!mstb)
		return -EREMOTEIO;

	/* construct i2c msg */
	/* see if last msg is a read */
	if (msgs[num - 1].flags & I2C_M_RD)
		reading = true;

	if (!reading) {
		DRM_DEBUG_KMS("Unsupported I2C transaction for MST device\n");
		ret = -EIO;
		goto out;
	}

	msg.req_type = DP_REMOTE_I2C_READ;
	msg.u.i2c_read.num_transactions = num - 1;
	msg.u.i2c_read.port_number = port->port_num;
	for (i = 0; i < num - 1; i++) {
		msg.u.i2c_read.transactions[i].i2c_dev_id = msgs[i].addr;
		msg.u.i2c_read.transactions[i].num_bytes = msgs[i].len;
		msg.u.i2c_read.transactions[i].bytes = msgs[i].buf;
	}
	msg.u.i2c_read.read_i2c_device_id = msgs[num - 1].addr;
	msg.u.i2c_read.num_bytes_read = msgs[num - 1].len;

	txmsg = kzalloc(sizeof(*txmsg), GFP_KERNEL);
	if (!txmsg) {
		ret = -ENOMEM;
		goto out;
	}

	txmsg->dst = mstb;
	drm_dp_encode_sideband_req(&msg, txmsg);

	drm_dp_queue_down_tx(mgr, txmsg);

	ret = drm_dp_mst_wait_tx_reply(mstb, txmsg);
	if (ret > 0) {

		if (txmsg->reply.reply_type == 1) { /* got a NAK back */
			ret = -EREMOTEIO;
			goto out;
		}
		if (txmsg->reply.u.remote_i2c_read_ack.num_bytes != msgs[num - 1].len) {
			ret = -EIO;
			goto out;
		}
		memcpy(msgs[num - 1].buf, txmsg->reply.u.remote_i2c_read_ack.bytes, msgs[num - 1].len);
		ret = num;
	}
out:
	kfree(txmsg);
	drm_dp_put_mst_branch_device(mstb);
	return ret;
}

static u32 drm_dp_mst_i2c_functionality(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL |
	       I2C_FUNC_SMBUS_READ_BLOCK_DATA |
	       I2C_FUNC_SMBUS_BLOCK_PROC_CALL |
	       I2C_FUNC_10BIT_ADDR;
}

static const struct i2c_algorithm drm_dp_mst_i2c_algo = {
	.functionality = drm_dp_mst_i2c_functionality,
	.master_xfer = drm_dp_mst_i2c_xfer,
};

/**
 * drm_dp_mst_register_i2c_bus() - register an I2C adapter for I2C-over-AUX
 * @aux: DisplayPort AUX channel
 *
 * Returns 0 on success or a negative error code on failure.
 */
static int drm_dp_mst_register_i2c_bus(struct drm_dp_aux *aux)
{
	aux->ddc.algo = &drm_dp_mst_i2c_algo;
	aux->ddc.algo_data = aux;
	aux->ddc.retries = 3;

	aux->ddc.class = I2C_CLASS_DDC;
	aux->ddc.owner = THIS_MODULE;
	aux->ddc.dev.parent = aux->dev;
	aux->ddc.dev.of_node = aux->dev->of_node;

	strlcpy(aux->ddc.name, aux->name ? aux->name : dev_name(aux->dev),
		sizeof(aux->ddc.name));

	return i2c_add_adapter(&aux->ddc);
}

/**
 * drm_dp_mst_unregister_i2c_bus() - unregister an I2C-over-AUX adapter
 * @aux: DisplayPort AUX channel
 */
static void drm_dp_mst_unregister_i2c_bus(struct drm_dp_aux *aux)
{
	i2c_del_adapter(&aux->ddc);
}
