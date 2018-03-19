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
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>

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

static void drm_dp_send_link_address(struct drm_dp_mst_topology_mgr *mgr,
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

		/*
		 * ignore out-of-order messages or messages that are part of a
		 * failed transaction
		 */
		if (!recv_hdr.somt && !msg->have_somt)
			return false;

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
				  u8 vcpi, uint16_t pbn,
				  u8 number_sdp_streams,
				  u8 *sdp_stream_sink)
{
	struct drm_dp_sideband_msg_req_body req;
	memset(&req, 0, sizeof(req));
	req.req_type = DP_ALLOCATE_PAYLOAD;
	req.u.allocate_payload.port_number = port_num;
	req.u.allocate_payload.vcpi = vcpi;
	req.u.allocate_payload.pbn = pbn;
	req.u.allocate_payload.number_sdp_streams = number_sdp_streams;
	memcpy(req.u.allocate_payload.sdp_stream_sink, sdp_stream_sink,
		   number_sdp_streams);
	drm_dp_encode_sideband_req(&req, msg);
	msg->path_msg = true;
	return 0;
}

static int drm_dp_mst_assign_payload_id(struct drm_dp_mst_topology_mgr *mgr,
					struct drm_dp_vcpi *vcpi)
{
	int ret, vcpi_ret;

	mutex_lock(&mgr->payload_lock);
	ret = find_first_zero_bit(&mgr->payload_mask, mgr->max_payloads + 1);
	if (ret > mgr->max_payloads) {
		ret = -EINVAL;
		DRM_DEBUG_KMS("out of payload ids %d\n", ret);
		goto out_unlock;
	}

	vcpi_ret = find_first_zero_bit(&mgr->vcpi_mask, mgr->max_payloads + 1);
	if (vcpi_ret > mgr->max_payloads) {
		ret = -EINVAL;
		DRM_DEBUG_KMS("out of vcpi ids %d\n", ret);
		goto out_unlock;
	}

	set_bit(ret, &mgr->payload_mask);
	set_bit(vcpi_ret, &mgr->vcpi_mask);
	vcpi->vcpi = vcpi_ret + 1;
	mgr->proposed_vcpis[ret - 1] = vcpi;
out_unlock:
	mutex_unlock(&mgr->payload_lock);
	return ret;
}

static void drm_dp_mst_put_payload_id(struct drm_dp_mst_topology_mgr *mgr,
				      int vcpi)
{
	int i;
	if (vcpi == 0)
		return;

	mutex_lock(&mgr->payload_lock);
	DRM_DEBUG_KMS("putting payload %d\n", vcpi);
	clear_bit(vcpi - 1, &mgr->vcpi_mask);

	for (i = 0; i < mgr->max_payloads; i++) {
		if (mgr->proposed_vcpis[i])
			if (mgr->proposed_vcpis[i]->vcpi == vcpi) {
				mgr->proposed_vcpis[i] = NULL;
				clear_bit(i + 1, &mgr->payload_mask);
			}
	}
	mutex_unlock(&mgr->payload_lock);
}

static bool check_txmsg_state(struct drm_dp_mst_topology_mgr *mgr,
			      struct drm_dp_sideband_msg_tx *txmsg)
{
	unsigned int state;

	/*
	 * All updates to txmsg->state are protected by mgr->qlock, and the two
	 * cases we check here are terminal states. For those the barriers
	 * provided by the wake_up/wait_event pair are enough.
	 */
	state = READ_ONCE(txmsg->state);
	return (state == DRM_DP_SIDEBAND_TX_RX ||
		state == DRM_DP_SIDEBAND_TX_TIMEOUT);
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

static void drm_dp_free_mst_port(struct kref *kref);

static void drm_dp_free_mst_branch_device(struct kref *kref)
{
	struct drm_dp_mst_branch *mstb = container_of(kref, struct drm_dp_mst_branch, kref);
	if (mstb->port_parent) {
		if (list_empty(&mstb->port_parent->next))
			kref_put(&mstb->port_parent->kref, drm_dp_free_mst_port);
	}
	kfree(mstb);
}

static void drm_dp_destroy_mst_branch_device(struct kref *kref)
{
	struct drm_dp_mst_branch *mstb = container_of(kref, struct drm_dp_mst_branch, kref);
	struct drm_dp_mst_port *port, *tmp;
	bool wake_tx = false;

	/*
	 * init kref again to be used by ports to remove mst branch when it is
	 * not needed anymore
	 */
	kref_init(kref);

	if (mstb->port_parent && list_empty(&mstb->port_parent->next))
		kref_get(&mstb->port_parent->kref);

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
		wake_up_all(&mstb->mgr->tx_waitq);

	kref_put(kref, drm_dp_free_mst_branch_device);
}

static void drm_dp_put_mst_branch_device(struct drm_dp_mst_branch *mstb)
{
	kref_put(&mstb->kref, drm_dp_destroy_mst_branch_device);
}


static void drm_dp_port_teardown_pdt(struct drm_dp_mst_port *port, int old_pdt)
{
	struct drm_dp_mst_branch *mstb;

	switch (old_pdt) {
	case DP_PEER_DEVICE_DP_LEGACY_CONV:
	case DP_PEER_DEVICE_SST_SINK:
		/* remove i2c over sideband */
		drm_dp_mst_unregister_i2c_bus(&port->aux);
		break;
	case DP_PEER_DEVICE_MST_BRANCHING:
		mstb = port->mstb;
		port->mstb = NULL;
		drm_dp_put_mst_branch_device(mstb);
		break;
	}
}

static void drm_dp_destroy_port(struct kref *kref)
{
	struct drm_dp_mst_port *port = container_of(kref, struct drm_dp_mst_port, kref);
	struct drm_dp_mst_topology_mgr *mgr = port->mgr;

	if (!port->input) {
		port->vcpi.num_slots = 0;

		kfree(port->cached_edid);

		/*
		 * The only time we don't have a connector
		 * on an output port is if the connector init
		 * fails.
		 */
		if (port->connector) {
			/* we can't destroy the connector here, as
			 * we might be holding the mode_config.mutex
			 * from an EDID retrieval */

			mutex_lock(&mgr->destroy_connector_lock);
			kref_get(&port->parent->kref);
			list_add(&port->next, &mgr->destroy_connector_list);
			mutex_unlock(&mgr->destroy_connector_lock);
			schedule_work(&mgr->destroy_connector_work);
			return;
		}
		/* no need to clean up vcpi
		 * as if we have no connector we never setup a vcpi */
		drm_dp_port_teardown_pdt(port, port->pdt);
		port->pdt = DP_PEER_DEVICE_NONE;
	}
	kfree(port);
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
	int parent_lct = port->parent->lct;
	int shift = 4;
	int idx = (parent_lct - 1) / 2;
	if (parent_lct > 1) {
		memcpy(rad, port->parent->rad, idx + 1);
		shift = (parent_lct % 2) ? 4 : 0;
	} else
		rad[0] = 0;

	rad[idx] |= port->port_num << shift;
	return parent_lct + 1;
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

static void drm_dp_check_mstb_guid(struct drm_dp_mst_branch *mstb, u8 *guid)
{
	int ret;

	memcpy(mstb->guid, guid, 16);

	if (!drm_dp_validate_guid(mstb->mgr, mstb->guid)) {
		if (mstb->port_parent) {
			ret = drm_dp_send_dpcd_write(
					mstb->mgr,
					mstb->port_parent,
					DP_GUID,
					16,
					mstb->guid);
		} else {

			ret = drm_dp_dpcd_write(
					mstb->mgr->aux,
					DP_GUID,
					mstb->guid,
					16);
		}
	}
}

static void build_mst_prop_path(const struct drm_dp_mst_branch *mstb,
				int pnum,
				char *proppath,
				size_t proppath_size)
{
	int i;
	char temp[8];
	snprintf(proppath, proppath_size, "mst:%d", mstb->mgr->conn_base_id);
	for (i = 0; i < (mstb->lct - 1); i++) {
		int shift = (i % 2) ? 0 : 4;
		int port_num = (mstb->rad[i / 2] >> shift) & 0xf;
		snprintf(temp, sizeof(temp), "-%d", port_num);
		strlcat(proppath, temp, proppath_size);
	}
	snprintf(temp, sizeof(temp), "-%d", pnum);
	strlcat(proppath, temp, proppath_size);
}

static void drm_dp_add_port(struct drm_dp_mst_branch *mstb,
			    struct drm_device *dev,
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
		port->aux.dev = dev->dev;
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
			if (!port->input)
				drm_dp_send_enum_path_resources(mstb->mgr, mstb, port);
		} else {
			port->available_pbn = 0;
			}
	}

	if (old_pdt != port->pdt && !port->input) {
		drm_dp_port_teardown_pdt(port, old_pdt);

		ret = drm_dp_port_setup_pdt(port);
		if (ret == true)
			drm_dp_send_link_address(mstb->mgr, port->mstb);
	}

	if (created && !port->input) {
		char proppath[255];

		build_mst_prop_path(mstb, port->port_num, proppath, sizeof(proppath));
		port->connector = (*mstb->mgr->cbs->add_connector)(mstb->mgr, port, proppath);
		if (!port->connector) {
			/* remove it from the port list */
			mutex_lock(&mstb->mgr->lock);
			list_del(&port->next);
			mutex_unlock(&mstb->mgr->lock);
			/* drop port list reference */
			drm_dp_put_port(port);
			goto out;
		}
		if ((port->pdt == DP_PEER_DEVICE_DP_LEGACY_CONV ||
		     port->pdt == DP_PEER_DEVICE_SST_SINK) &&
		    port->port_num >= DP_MST_LOGICAL_PORT_0) {
			port->cached_edid = drm_get_edid(port->connector, &port->aux.ddc);
			drm_mode_connector_set_tile_property(port->connector);
		}
		(*mstb->mgr->cbs->register_connector)(port->connector);
	}

out:
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
			dowork = true;
		} else {
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

	mutex_lock(&mgr->lock);
	mstb = mgr->mst_primary;

	for (i = 0; i < lct - 1; i++) {
		int shift = (i % 2) ? 0 : 4;
		int port_num = (rad[i / 2] >> shift) & 0xf;

		list_for_each_entry(port, &mstb->ports, next) {
			if (port->port_num == port_num) {
				mstb = port->mstb;
				if (!mstb) {
					DRM_ERROR("failed to lookup MSTB with lct %d, rad %02x\n", lct, rad[0]);
					goto out;
				}

				break;
			}
		}
	}
	kref_get(&mstb->kref);
out:
	mutex_unlock(&mgr->lock);
	return mstb;
}

static struct drm_dp_mst_branch *get_mst_branch_device_by_guid_helper(
	struct drm_dp_mst_branch *mstb,
	uint8_t *guid)
{
	struct drm_dp_mst_branch *found_mstb;
	struct drm_dp_mst_port *port;

	if (memcmp(mstb->guid, guid, 16) == 0)
		return mstb;


	list_for_each_entry(port, &mstb->ports, next) {
		if (!port->mstb)
			continue;

		found_mstb = get_mst_branch_device_by_guid_helper(port->mstb, guid);

		if (found_mstb)
			return found_mstb;
	}

	return NULL;
}

static struct drm_dp_mst_branch *drm_dp_get_mst_branch_device_by_guid(
	struct drm_dp_mst_topology_mgr *mgr,
	uint8_t *guid)
{
	struct drm_dp_mst_branch *mstb;

	/* find the port by iterating down */
	mutex_lock(&mgr->lock);

	mstb = get_mst_branch_device_by_guid_helper(mgr->mst_primary, guid);

	if (mstb)
		kref_get(&mstb->kref);

	mutex_unlock(&mgr->lock);
	return mstb;
}

static void drm_dp_check_and_send_link_address(struct drm_dp_mst_topology_mgr *mgr,
					       struct drm_dp_mst_branch *mstb)
{
	struct drm_dp_mst_port *port;
	struct drm_dp_mst_branch *mstb_child;
	if (!mstb->link_address_sent)
		drm_dp_send_link_address(mgr, mstb);

	list_for_each_entry(port, &mstb->ports, next) {
		if (port->input)
			continue;

		if (!port->ddps)
			continue;

		if (!port->available_pbn)
			drm_dp_send_enum_path_resources(mgr, mstb, port);

		if (port->mstb) {
			mstb_child = drm_dp_get_validated_mstb_ref(mgr, port->mstb);
			if (mstb_child) {
				drm_dp_check_and_send_link_address(mgr, mstb_child);
				drm_dp_put_mst_branch_device(mstb_child);
			}
		}
	}
}

static void drm_dp_mst_link_probe_work(struct work_struct *work)
{
	struct drm_dp_mst_topology_mgr *mgr = container_of(work, struct drm_dp_mst_topology_mgr, work);
	struct drm_dp_mst_branch *mstb;

	mutex_lock(&mgr->lock);
	mstb = mgr->mst_primary;
	if (mstb) {
		kref_get(&mstb->kref);
	}
	mutex_unlock(&mgr->lock);
	if (mstb) {
		drm_dp_check_and_send_link_address(mgr, mstb);
		drm_dp_put_mst_branch_device(mstb);
	}
}

static bool drm_dp_validate_guid(struct drm_dp_mst_topology_mgr *mgr,
				 u8 *guid)
{
	u64 salt;

	if (memchr_inv(guid, 0, 16))
		return true;

	salt = get_jiffies_64();

	memcpy(&guid[0], &salt, sizeof(u64));
	memcpy(&guid[8], &salt, sizeof(u64));

	return false;
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
	u8 req_type;

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

	req_type = txmsg->msg[0] & 0x7f;
	if (req_type == DP_CONNECTION_STATUS_NOTIFY ||
		req_type == DP_RESOURCE_STATUS_NOTIFY)
		hdr->broadcast = 1;
	else
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

	memset(&hdr, 0, sizeof(struct drm_dp_sideband_msg_hdr));

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

static void process_single_down_tx_qlock(struct drm_dp_mst_topology_mgr *mgr)
{
	struct drm_dp_sideband_msg_tx *txmsg;
	int ret;

	WARN_ON(!mutex_is_locked(&mgr->qlock));

	/* construct a chunk from the first msg in the tx_msg queue */
	if (list_empty(&mgr->tx_msg_downq))
		return;

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
		wake_up_all(&mgr->tx_waitq);
	}
}

/* called holding qlock */
static void process_single_up_tx_qlock(struct drm_dp_mst_topology_mgr *mgr,
				       struct drm_dp_sideband_msg_tx *txmsg)
{
	int ret;

	/* construct a chunk from the first msg in the tx_msg queue */
	ret = process_single_tx_qlock(mgr, txmsg, true);

	if (ret != 1)
		DRM_DEBUG_KMS("failed to send msg in q %d\n", ret);

	txmsg->dst->tx_slots[txmsg->seqno] = NULL;
}

static void drm_dp_queue_down_tx(struct drm_dp_mst_topology_mgr *mgr,
				 struct drm_dp_sideband_msg_tx *txmsg)
{
	mutex_lock(&mgr->qlock);
	list_add_tail(&txmsg->next, &mgr->tx_msg_downq);
	if (list_is_singular(&mgr->tx_msg_downq))
		process_single_down_tx_qlock(mgr);
	mutex_unlock(&mgr->qlock);
}

static void drm_dp_send_link_address(struct drm_dp_mst_topology_mgr *mgr,
				     struct drm_dp_mst_branch *mstb)
{
	int len;
	struct drm_dp_sideband_msg_tx *txmsg;
	int ret;

	txmsg = kzalloc(sizeof(*txmsg), GFP_KERNEL);
	if (!txmsg)
		return;

	txmsg->dst = mstb;
	len = build_link_address(txmsg);

	mstb->link_address_sent = true;
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

			drm_dp_check_mstb_guid(mstb, txmsg->reply.u.link_addr.guid);

			for (i = 0; i < txmsg->reply.u.link_addr.nports; i++) {
				drm_dp_add_port(mstb, mgr->dev, &txmsg->reply.u.link_addr.ports[i]);
			}
			(*mgr->cbs->hotplug)(mgr);
		}
	} else {
		mstb->link_address_sent = false;
		DRM_DEBUG_KMS("link address failed %d\n", ret);
	}

	kfree(txmsg);
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

static struct drm_dp_mst_port *drm_dp_get_last_connected_port_to_mstb(struct drm_dp_mst_branch *mstb)
{
	if (!mstb->port_parent)
		return NULL;

	if (mstb->port_parent->mstb != mstb)
		return mstb->port_parent;

	return drm_dp_get_last_connected_port_to_mstb(mstb->port_parent->parent);
}

static struct drm_dp_mst_branch *drm_dp_get_last_connected_port_and_mstb(struct drm_dp_mst_topology_mgr *mgr,
									 struct drm_dp_mst_branch *mstb,
									 int *port_num)
{
	struct drm_dp_mst_branch *rmstb = NULL;
	struct drm_dp_mst_port *found_port;
	mutex_lock(&mgr->lock);
	if (mgr->mst_primary) {
		found_port = drm_dp_get_last_connected_port_to_mstb(mstb);

		if (found_port) {
			rmstb = found_port->parent;
			kref_get(&rmstb->kref);
			*port_num = found_port->port_num;
		}
	}
	mutex_unlock(&mgr->lock);
	return rmstb;
}

static int drm_dp_payload_send_msg(struct drm_dp_mst_topology_mgr *mgr,
				   struct drm_dp_mst_port *port,
				   int id,
				   int pbn)
{
	struct drm_dp_sideband_msg_tx *txmsg;
	struct drm_dp_mst_branch *mstb;
	int len, ret, port_num;
	u8 sinks[DRM_DP_MAX_SDP_STREAMS];
	int i;

	port = drm_dp_get_validated_port_ref(mgr, port);
	if (!port)
		return -EINVAL;

	port_num = port->port_num;
	mstb = drm_dp_get_validated_mstb_ref(mgr, port->parent);
	if (!mstb) {
		mstb = drm_dp_get_last_connected_port_and_mstb(mgr, port->parent, &port_num);

		if (!mstb) {
			drm_dp_put_port(port);
			return -EINVAL;
		}
	}

	txmsg = kzalloc(sizeof(*txmsg), GFP_KERNEL);
	if (!txmsg) {
		ret = -ENOMEM;
		goto fail_put;
	}

	for (i = 0; i < port->num_sdp_streams; i++)
		sinks[i] = i;

	txmsg->dst = mstb;
	len = build_allocate_payload(txmsg, port_num,
				     id,
				     pbn, port->num_sdp_streams, sinks);

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
	drm_dp_put_port(port);
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

static int drm_dp_create_payload_step2(struct drm_dp_mst_topology_mgr *mgr,
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

static int drm_dp_destroy_payload_step1(struct drm_dp_mst_topology_mgr *mgr,
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
	payload->payload_state = DP_PAYLOAD_DELETE_LOCAL;
	return 0;
}

static int drm_dp_destroy_payload_step2(struct drm_dp_mst_topology_mgr *mgr,
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
	int i, j;
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
			port = drm_dp_get_validated_port_ref(mgr, port);
			if (!port) {
				mutex_unlock(&mgr->payload_lock);
				return -EINVAL;
			}
			req_payload.num_slots = mgr->proposed_vcpis[i]->num_slots;
			req_payload.vcpi = mgr->proposed_vcpis[i]->vcpi;
		} else {
			port = NULL;
			req_payload.num_slots = 0;
		}

		if (mgr->payloads[i].start_slot != req_payload.start_slot) {
			mgr->payloads[i].start_slot = req_payload.start_slot;
		}
		/* work out what is required to happen with this payload */
		if (mgr->payloads[i].num_slots != req_payload.num_slots) {

			/* need to push an update for this payload */
			if (req_payload.num_slots) {
				drm_dp_create_payload_step1(mgr, mgr->proposed_vcpis[i]->vcpi, &req_payload);
				mgr->payloads[i].num_slots = req_payload.num_slots;
				mgr->payloads[i].vcpi = req_payload.vcpi;
			} else if (mgr->payloads[i].num_slots) {
				mgr->payloads[i].num_slots = 0;
				drm_dp_destroy_payload_step1(mgr, port, mgr->payloads[i].vcpi, &mgr->payloads[i]);
				req_payload.payload_state = mgr->payloads[i].payload_state;
				mgr->payloads[i].start_slot = 0;
			}
			mgr->payloads[i].payload_state = req_payload.payload_state;
		}
		cur_slots += req_payload.num_slots;

		if (port)
			drm_dp_put_port(port);
	}

	for (i = 0; i < mgr->max_payloads; i++) {
		if (mgr->payloads[i].payload_state == DP_PAYLOAD_DELETE_LOCAL) {
			DRM_DEBUG_KMS("removing payload %d\n", i);
			for (j = i; j < mgr->max_payloads - 1; j++) {
				memcpy(&mgr->payloads[j], &mgr->payloads[j + 1], sizeof(struct drm_dp_payload));
				mgr->proposed_vcpis[j] = mgr->proposed_vcpis[j + 1];
				if (mgr->proposed_vcpis[j] && mgr->proposed_vcpis[j]->num_slots) {
					set_bit(j + 1, &mgr->payload_mask);
				} else {
					clear_bit(j + 1, &mgr->payload_mask);
				}
			}
			memset(&mgr->payloads[mgr->max_payloads - 1], 0, sizeof(struct drm_dp_payload));
			mgr->proposed_vcpis[mgr->max_payloads - 1] = NULL;
			clear_bit(mgr->max_payloads, &mgr->payload_mask);

		}
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
	int ret = 0;
	mutex_lock(&mgr->payload_lock);
	for (i = 0; i < mgr->max_payloads; i++) {

		if (!mgr->proposed_vcpis[i])
			continue;

		port = container_of(mgr->proposed_vcpis[i], struct drm_dp_mst_port, vcpi);

		DRM_DEBUG_KMS("payload %d %d\n", i, mgr->payloads[i].payload_state);
		if (mgr->payloads[i].payload_state == DP_PAYLOAD_LOCAL) {
			ret = drm_dp_create_payload_step2(mgr, port, mgr->proposed_vcpis[i]->vcpi, &mgr->payloads[i]);
		} else if (mgr->payloads[i].payload_state == DP_PAYLOAD_DELETE_LOCAL) {
			ret = drm_dp_destroy_payload_step2(mgr, mgr->proposed_vcpis[i]->vcpi, &mgr->payloads[i]);
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

	reply.reply_type = 0;
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

	process_single_up_tx_qlock(mgr, txmsg);

	mutex_unlock(&mgr->qlock);

	kfree(txmsg);
	return 0;
}

static bool drm_dp_get_vc_payload_bw(int dp_link_bw,
				     int dp_link_count,
				     int *out)
{
	switch (dp_link_bw) {
	default:
		DRM_DEBUG_KMS("invalid link bandwidth in DPCD: %x (link count: %d)\n",
			      dp_link_bw, dp_link_count);
		return false;

	case DP_LINK_BW_1_62:
		*out = 3 * dp_link_count;
		break;
	case DP_LINK_BW_2_7:
		*out = 5 * dp_link_count;
		break;
	case DP_LINK_BW_5_4:
		*out = 10 * dp_link_count;
		break;
	}
	return true;
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

		if (!drm_dp_get_vc_payload_bw(mgr->dpcd[1],
					      mgr->dpcd[2] & DP_MAX_LANE_COUNT_MASK,
					      &mgr->pbn_div)) {
			ret = -EINVAL;
			goto out_unlock;
		}

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

		ret = drm_dp_dpcd_writeb(mgr->aux, DP_MSTM_CTRL,
							 DP_MST_EN | DP_UP_REQ_EN | DP_UPSTREAM_IS_SRC);
		if (ret < 0) {
			goto out_unlock;
		}

		{
			struct drm_dp_payload reset_pay;
			reset_pay.start_slot = 0;
			reset_pay.num_slots = 0x3f;
			drm_dp_dpcd_write_payload(mgr, 0, &reset_pay);
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
		mgr->vcpi_mask = 0;
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
	flush_work(&mgr->work);
	flush_work(&mgr->destroy_connector_work);
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
		u8 guid[16];

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

		/* Some hubs forget their guids after they resume */
		sret = drm_dp_dpcd_read(mgr->aux, DP_GUID, guid, 16);
		if (sret != 16) {
			DRM_DEBUG_KMS("dpcd read failed - undocked during suspend?\n");
			ret = -1;
			goto out_unlock;
		}
		drm_dp_check_mstb_guid(mgr->mst_primary, guid);

		ret = 0;
	} else
		ret = -1;

out_unlock:
	mutex_unlock(&mgr->lock);
	return ret;
}
EXPORT_SYMBOL(drm_dp_mst_topology_mgr_resume);

static bool drm_dp_get_one_sb_msg(struct drm_dp_mst_topology_mgr *mgr, bool up)
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
		return false;
	}
	ret = drm_dp_sideband_msg_build(msg, replyblock, len, true);
	if (!ret) {
		DRM_DEBUG_KMS("sideband msg build failed %d\n", replyblock[0]);
		return false;
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
			DRM_DEBUG_KMS("failed to read a chunk (len %d, ret %d)\n",
				      len, ret);
			return false;
		}

		ret = drm_dp_sideband_msg_build(msg, replyblock, len, false);
		if (!ret) {
			DRM_DEBUG_KMS("failed to build sideband msg\n");
			return false;
		}

		curreply += len;
		replylen -= len;
	}
	return true;
}

static int drm_dp_mst_handle_down_rep(struct drm_dp_mst_topology_mgr *mgr)
{
	int ret = 0;

	if (!drm_dp_get_one_sb_msg(mgr, false)) {
		memset(&mgr->down_rep_recv, 0,
		       sizeof(struct drm_dp_sideband_msg_rx));
		return 0;
	}

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

		wake_up_all(&mgr->tx_waitq);
	}
	return ret;
}

static int drm_dp_mst_handle_up_req(struct drm_dp_mst_topology_mgr *mgr)
{
	int ret = 0;

	if (!drm_dp_get_one_sb_msg(mgr, true)) {
		memset(&mgr->up_req_recv, 0,
		       sizeof(struct drm_dp_sideband_msg_rx));
		return 0;
	}

	if (mgr->up_req_recv.have_eomt) {
		struct drm_dp_sideband_msg_req_body msg;
		struct drm_dp_mst_branch *mstb = NULL;
		bool seqno;

		if (!mgr->up_req_recv.initial_hdr.broadcast) {
			mstb = drm_dp_get_mst_branch_device(mgr,
							    mgr->up_req_recv.initial_hdr.lct,
							    mgr->up_req_recv.initial_hdr.rad);
			if (!mstb) {
				DRM_DEBUG_KMS("Got MST reply from unknown device %d\n", mgr->up_req_recv.initial_hdr.lct);
				memset(&mgr->up_req_recv, 0, sizeof(struct drm_dp_sideband_msg_rx));
				return 0;
			}
		}

		seqno = mgr->up_req_recv.initial_hdr.seqno;
		drm_dp_sideband_parse_req(&mgr->up_req_recv, &msg);

		if (msg.req_type == DP_CONNECTION_STATUS_NOTIFY) {
			drm_dp_send_up_ack_reply(mgr, mgr->mst_primary, msg.req_type, seqno, false);

			if (!mstb)
				mstb = drm_dp_get_mst_branch_device_by_guid(mgr, msg.u.conn_stat.guid);

			if (!mstb) {
				DRM_DEBUG_KMS("Got MST reply from unknown device %d\n", mgr->up_req_recv.initial_hdr.lct);
				memset(&mgr->up_req_recv, 0, sizeof(struct drm_dp_sideband_msg_rx));
				return 0;
			}

			drm_dp_update_port(mstb, &msg.u.conn_stat);

			DRM_DEBUG_KMS("Got CSN: pn: %d ldps:%d ddps: %d mcs: %d ip: %d pdt: %d\n", msg.u.conn_stat.port_number, msg.u.conn_stat.legacy_device_plug_status, msg.u.conn_stat.displayport_device_plug_status, msg.u.conn_stat.message_capability_status, msg.u.conn_stat.input_port, msg.u.conn_stat.peer_device_type);
			(*mgr->cbs->hotplug)(mgr);

		} else if (msg.req_type == DP_RESOURCE_STATUS_NOTIFY) {
			drm_dp_send_up_ack_reply(mgr, mgr->mst_primary, msg.req_type, seqno, false);
			if (!mstb)
				mstb = drm_dp_get_mst_branch_device_by_guid(mgr, msg.u.resource_stat.guid);

			if (!mstb) {
				DRM_DEBUG_KMS("Got MST reply from unknown device %d\n", mgr->up_req_recv.initial_hdr.lct);
				memset(&mgr->up_req_recv, 0, sizeof(struct drm_dp_sideband_msg_rx));
				return 0;
			}

			DRM_DEBUG_KMS("Got RSN: pn: %d avail_pbn %d\n", msg.u.resource_stat.port_number, msg.u.resource_stat.available_pbn);
		}

		if (mstb)
			drm_dp_put_mst_branch_device(mstb);

		memset(&mgr->up_req_recv, 0, sizeof(struct drm_dp_sideband_msg_rx));
	}
	return ret;
}

/**
 * drm_dp_mst_hpd_irq() - MST hotplug IRQ notify
 * @mgr: manager to notify irq for.
 * @esi: 4 bytes from SINK_COUNT_ESI
 * @handled: whether the hpd interrupt was consumed or not
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
 * @connector: DRM connector for this port
 * @mgr: manager for this port
 * @port: unverified pointer to a port
 *
 * This returns the current connection state for a port. It validates the
 * port pointer still exists so the caller doesn't require a reference
 */
enum drm_connector_status drm_dp_mst_detect_port(struct drm_connector *connector,
						 struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port)
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
		/* for logical ports - cache the EDID */
		if (port->port_num >= 8 && !port->cached_edid) {
			port->cached_edid = drm_get_edid(connector, &port->aux.ddc);
		}
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
 * drm_dp_mst_port_has_audio() - Check whether port has audio capability or not
 * @mgr: manager for this port
 * @port: unverified pointer to a port.
 *
 * This returns whether the port supports audio or not.
 */
bool drm_dp_mst_port_has_audio(struct drm_dp_mst_topology_mgr *mgr,
					struct drm_dp_mst_port *port)
{
	bool ret = false;

	port = drm_dp_get_validated_port_ref(mgr, port);
	if (!port)
		return ret;
	ret = port->has_audio;
	drm_dp_put_port(port);
	return ret;
}
EXPORT_SYMBOL(drm_dp_mst_port_has_audio);

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

	if (port->cached_edid)
		edid = drm_edid_duplicate(port->cached_edid);
	else {
		edid = drm_get_edid(connector, &port->aux.ddc);
		drm_mode_connector_set_tile_property(connector);
	}
	port->has_audio = drm_detect_monitor_audio(edid);
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

	/* max. time slots - one slot for MTP header */
	if (num_slots > 63)
		return -ENOSPC;
	return num_slots;
}
EXPORT_SYMBOL(drm_dp_find_vcpi_slots);

static int drm_dp_init_vcpi(struct drm_dp_mst_topology_mgr *mgr,
			    struct drm_dp_vcpi *vcpi, int pbn, int slots)
{
	int ret;

	/* max. time slots - one slot for MTP header */
	if (slots > 63)
		return -ENOSPC;

	vcpi->pbn = pbn;
	vcpi->aligned_pbn = slots * mgr->pbn_div;
	vcpi->num_slots = slots;

	ret = drm_dp_mst_assign_payload_id(mgr, vcpi);
	if (ret < 0)
		return ret;
	return 0;
}

/**
 * drm_dp_atomic_find_vcpi_slots() - Find and add vcpi slots to the state
 * @state: global atomic state
 * @mgr: MST topology manager for the port
 * @port: port to find vcpi slots for
 * @pbn: bandwidth required for the mode in PBN
 *
 * RETURNS:
 * Total slots in the atomic state assigned for this port or error
 */
int drm_dp_atomic_find_vcpi_slots(struct drm_atomic_state *state,
				  struct drm_dp_mst_topology_mgr *mgr,
				  struct drm_dp_mst_port *port, int pbn)
{
	struct drm_dp_mst_topology_state *topology_state;
	int req_slots;

	topology_state = drm_atomic_get_mst_topology_state(state, mgr);
	if (IS_ERR(topology_state))
		return PTR_ERR(topology_state);

	port = drm_dp_get_validated_port_ref(mgr, port);
	if (port == NULL)
		return -EINVAL;
	req_slots = DIV_ROUND_UP(pbn, mgr->pbn_div);
	DRM_DEBUG_KMS("vcpi slots req=%d, avail=%d\n",
			req_slots, topology_state->avail_slots);

	if (req_slots > topology_state->avail_slots) {
		drm_dp_put_port(port);
		return -ENOSPC;
	}

	topology_state->avail_slots -= req_slots;
	DRM_DEBUG_KMS("vcpi slots avail=%d", topology_state->avail_slots);

	drm_dp_put_port(port);
	return req_slots;
}
EXPORT_SYMBOL(drm_dp_atomic_find_vcpi_slots);

/**
 * drm_dp_atomic_release_vcpi_slots() - Release allocated vcpi slots
 * @state: global atomic state
 * @mgr: MST topology manager for the port
 * @slots: number of vcpi slots to release
 *
 * RETURNS:
 * 0 if @slots were added back to &drm_dp_mst_topology_state->avail_slots or
 * negative error code
 */
int drm_dp_atomic_release_vcpi_slots(struct drm_atomic_state *state,
				     struct drm_dp_mst_topology_mgr *mgr,
				     int slots)
{
	struct drm_dp_mst_topology_state *topology_state;

	topology_state = drm_atomic_get_mst_topology_state(state, mgr);
	if (IS_ERR(topology_state))
		return PTR_ERR(topology_state);

	/* We cannot rely on port->vcpi.num_slots to update
	 * topology_state->avail_slots as the port may not exist if the parent
	 * branch device was unplugged. This should be fixed by tracking
	 * per-port slot allocation in drm_dp_mst_topology_state instead of
	 * depending on the caller to tell us how many slots to release.
	 */
	topology_state->avail_slots += slots;
	DRM_DEBUG_KMS("vcpi slots released=%d, avail=%d\n",
			slots, topology_state->avail_slots);

	return 0;
}
EXPORT_SYMBOL(drm_dp_atomic_release_vcpi_slots);

/**
 * drm_dp_mst_allocate_vcpi() - Allocate a virtual channel
 * @mgr: manager for this port
 * @port: port to allocate a virtual channel for.
 * @pbn: payload bandwidth number to request
 * @slots: returned number of slots for this PBN.
 */
bool drm_dp_mst_allocate_vcpi(struct drm_dp_mst_topology_mgr *mgr,
			      struct drm_dp_mst_port *port, int pbn, int slots)
{
	int ret;

	port = drm_dp_get_validated_port_ref(mgr, port);
	if (!port)
		return false;

	if (slots < 0)
		return false;

	if (port->vcpi.vcpi > 0) {
		DRM_DEBUG_KMS("payload: vcpi %d already allocated for pbn %d - requested pbn %d\n", port->vcpi.vcpi, port->vcpi.pbn, pbn);
		if (pbn == port->vcpi.pbn) {
			drm_dp_put_port(port);
			return true;
		}
	}

	ret = drm_dp_init_vcpi(mgr, &port->vcpi, pbn, slots);
	if (ret) {
		DRM_DEBUG_KMS("failed to init vcpi slots=%d max=63 ret=%d\n",
				DIV_ROUND_UP(pbn, mgr->pbn_div), ret);
		goto out;
	}
	DRM_DEBUG_KMS("initing vcpi for pbn=%d slots=%d\n",
			pbn, port->vcpi.num_slots);

	drm_dp_put_port(port);
	return true;
out:
	return false;
}
EXPORT_SYMBOL(drm_dp_mst_allocate_vcpi);

int drm_dp_mst_get_vcpi_slots(struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port)
{
	int slots = 0;
	port = drm_dp_get_validated_port_ref(mgr, port);
	if (!port)
		return slots;

	slots = port->vcpi.num_slots;
	drm_dp_put_port(port);
	return slots;
}
EXPORT_SYMBOL(drm_dp_mst_get_vcpi_slots);

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
	u64 kbps;
	s64 peak_kbps;
	u32 numerator;
	u32 denominator;

	kbps = clock * bpp;

	/*
	 * margin 5300ppm + 300ppm ~ 0.6% as per spec, factor is 1.006
	 * The unit of 54/64Mbytes/sec is an arbitrary unit chosen based on
	 * common multiplier to render an integer PBN for all link rate/lane
	 * counts combinations
	 * calculate
	 * peak_kbps *= (1006/1000)
	 * peak_kbps *= (64/54)
	 * peak_kbps *= 8    convert to bytes
	 */

	numerator = 64 * 1006;
	denominator = 54 * 8 * 1000 * 1000;

	kbps *= numerator;
	peak_kbps = drm_fixp_from_fraction(kbps, denominator);

	return drm_fixp2int_ceil(peak_kbps);
}
EXPORT_SYMBOL(drm_dp_calc_pbn_mode);

static int test_calc_pbn_mode(void)
{
	int ret;
	ret = drm_dp_calc_pbn_mode(154000, 30);
	if (ret != 689) {
		DRM_ERROR("PBN calculation test failed - clock %d, bpp %d, expected PBN %d, actual PBN %d.\n",
				154000, 30, 689, ret);
		return -EINVAL;
	}
	ret = drm_dp_calc_pbn_mode(234000, 30);
	if (ret != 1047) {
		DRM_ERROR("PBN calculation test failed - clock %d, bpp %d, expected PBN %d, actual PBN %d.\n",
				234000, 30, 1047, ret);
		return -EINVAL;
	}
	ret = drm_dp_calc_pbn_mode(297000, 24);
	if (ret != 1063) {
		DRM_ERROR("PBN calculation test failed - clock %d, bpp %d, expected PBN %d, actual PBN %d.\n",
				297000, 24, 1063, ret);
		return -EINVAL;
	}
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
		seq_printf(m, "%sport: %d: input: %d: pdt: %d, ddps: %d ldps: %d, sdp: %d/%d, %p, conn: %p\n", prefix, port->port_num, port->input, port->pdt, port->ddps, port->ldps, port->num_sdp_streams, port->num_sdp_stream_sinks, port, port->connector);
		if (port->mstb)
			drm_dp_mst_dump_mstb(m, port->mstb);
	}
}

#define DP_PAYLOAD_TABLE_SIZE		64

static bool dump_dp_payload_table(struct drm_dp_mst_topology_mgr *mgr,
				  char *buf)
{
	int i;

	for (i = 0; i < DP_PAYLOAD_TABLE_SIZE; i += 16) {
		if (drm_dp_dpcd_read(mgr->aux,
				     DP_PAYLOAD_TABLE_UPDATE_STATUS + i,
				     &buf[i], 16) != 16)
			return false;
	}
	return true;
}

static void fetch_monitor_name(struct drm_dp_mst_topology_mgr *mgr,
			       struct drm_dp_mst_port *port, char *name,
			       int namelen)
{
	struct edid *mst_edid;

	mst_edid = drm_dp_mst_get_edid(port->connector, mgr, port);
	drm_edid_get_monitor_name(mst_edid, name, namelen);
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
	seq_printf(m, "vcpi: %lx %lx %d\n", mgr->payload_mask, mgr->vcpi_mask,
		mgr->max_payloads);

	for (i = 0; i < mgr->max_payloads; i++) {
		if (mgr->proposed_vcpis[i]) {
			char name[14];

			port = container_of(mgr->proposed_vcpis[i], struct drm_dp_mst_port, vcpi);
			fetch_monitor_name(mgr, port, name, sizeof(name));
			seq_printf(m, "vcpi %d: %d %d %d sink name: %s\n", i,
				   port->port_num, port->vcpi.vcpi,
				   port->vcpi.num_slots,
				   (*name != 0) ? name :  "Unknown");
		} else
			seq_printf(m, "vcpi %d:unused\n", i);
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
		u8 buf[DP_PAYLOAD_TABLE_SIZE];
		int ret;

		ret = drm_dp_dpcd_read(mgr->aux, DP_DPCD_REV, buf, DP_RECEIVER_CAP_SIZE);
		seq_printf(m, "dpcd: %*ph\n", DP_RECEIVER_CAP_SIZE, buf);
		ret = drm_dp_dpcd_read(mgr->aux, DP_FAUX_CAP, buf, 2);
		seq_printf(m, "faux/mst: %*ph\n", 2, buf);
		ret = drm_dp_dpcd_read(mgr->aux, DP_MSTM_CTRL, buf, 1);
		seq_printf(m, "mst ctrl: %*ph\n", 1, buf);

		/* dump the standard OUI branch header */
		ret = drm_dp_dpcd_read(mgr->aux, DP_BRANCH_OUI, buf, DP_BRANCH_OUI_HEADER_SIZE);
		seq_printf(m, "branch oui: %*phN devid: ", 3, buf);
		for (i = 0x3; i < 0x8 && buf[i]; i++)
			seq_printf(m, "%c", buf[i]);
		seq_printf(m, " revision: hw: %x.%x sw: %x.%x\n",
			   buf[0x9] >> 4, buf[0x9] & 0xf, buf[0xa], buf[0xb]);
		if (dump_dp_payload_table(mgr, buf))
			seq_printf(m, "payload table: %*ph\n", DP_PAYLOAD_TABLE_SIZE, buf);
	}

	mutex_unlock(&mgr->lock);

}
EXPORT_SYMBOL(drm_dp_mst_dump_topology);

static void drm_dp_tx_work(struct work_struct *work)
{
	struct drm_dp_mst_topology_mgr *mgr = container_of(work, struct drm_dp_mst_topology_mgr, tx_work);

	mutex_lock(&mgr->qlock);
	if (!list_empty(&mgr->tx_msg_downq))
		process_single_down_tx_qlock(mgr);
	mutex_unlock(&mgr->qlock);
}

static void drm_dp_free_mst_port(struct kref *kref)
{
	struct drm_dp_mst_port *port = container_of(kref, struct drm_dp_mst_port, kref);
	kref_put(&port->parent->kref, drm_dp_free_mst_branch_device);
	kfree(port);
}

static void drm_dp_destroy_connector_work(struct work_struct *work)
{
	struct drm_dp_mst_topology_mgr *mgr = container_of(work, struct drm_dp_mst_topology_mgr, destroy_connector_work);
	struct drm_dp_mst_port *port;
	bool send_hotplug = false;
	/*
	 * Not a regular list traverse as we have to drop the destroy
	 * connector lock before destroying the connector, to avoid AB->BA
	 * ordering between this lock and the config mutex.
	 */
	for (;;) {
		mutex_lock(&mgr->destroy_connector_lock);
		port = list_first_entry_or_null(&mgr->destroy_connector_list, struct drm_dp_mst_port, next);
		if (!port) {
			mutex_unlock(&mgr->destroy_connector_lock);
			break;
		}
		list_del(&port->next);
		mutex_unlock(&mgr->destroy_connector_lock);

		kref_init(&port->kref);
		INIT_LIST_HEAD(&port->next);

		mgr->cbs->destroy_connector(mgr, port->connector);

		drm_dp_port_teardown_pdt(port, port->pdt);
		port->pdt = DP_PEER_DEVICE_NONE;

		if (!port->input && port->vcpi.vcpi > 0) {
			drm_dp_mst_reset_vcpi_slots(mgr, port);
			drm_dp_update_payload_part1(mgr);
			drm_dp_mst_put_payload_id(mgr, port->vcpi.vcpi);
		}

		kref_put(&port->kref, drm_dp_free_mst_port);
		send_hotplug = true;
	}
	if (send_hotplug)
		(*mgr->cbs->hotplug)(mgr);
}

static struct drm_private_state *
drm_dp_mst_duplicate_state(struct drm_private_obj *obj)
{
	struct drm_dp_mst_topology_state *state;

	state = kmemdup(obj->state, sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_private_obj_duplicate_state(obj, &state->base);

	return &state->base;
}

static void drm_dp_mst_destroy_state(struct drm_private_obj *obj,
				     struct drm_private_state *state)
{
	struct drm_dp_mst_topology_state *mst_state =
		to_dp_mst_topology_state(state);

	kfree(mst_state);
}

static const struct drm_private_state_funcs mst_state_funcs = {
	.atomic_duplicate_state = drm_dp_mst_duplicate_state,
	.atomic_destroy_state = drm_dp_mst_destroy_state,
};

/**
 * drm_atomic_get_mst_topology_state: get MST topology state
 *
 * @state: global atomic state
 * @mgr: MST topology manager, also the private object in this case
 *
 * This function wraps drm_atomic_get_priv_obj_state() passing in the MST atomic
 * state vtable so that the private object state returned is that of a MST
 * topology object. Also, drm_atomic_get_private_obj_state() expects the caller
 * to care of the locking, so warn if don't hold the connection_mutex.
 *
 * RETURNS:
 *
 * The MST topology state or error pointer.
 */
struct drm_dp_mst_topology_state *drm_atomic_get_mst_topology_state(struct drm_atomic_state *state,
								    struct drm_dp_mst_topology_mgr *mgr)
{
	struct drm_device *dev = mgr->dev;

	WARN_ON(!drm_modeset_is_locked(&dev->mode_config.connection_mutex));
	return to_dp_mst_topology_state(drm_atomic_get_private_obj_state(state, &mgr->base));
}
EXPORT_SYMBOL(drm_atomic_get_mst_topology_state);

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
				 struct drm_device *dev, struct drm_dp_aux *aux,
				 int max_dpcd_transaction_bytes,
				 int max_payloads, int conn_base_id)
{
	struct drm_dp_mst_topology_state *mst_state;

	mutex_init(&mgr->lock);
	mutex_init(&mgr->qlock);
	mutex_init(&mgr->payload_lock);
	mutex_init(&mgr->destroy_connector_lock);
	INIT_LIST_HEAD(&mgr->tx_msg_downq);
	INIT_LIST_HEAD(&mgr->destroy_connector_list);
	INIT_WORK(&mgr->work, drm_dp_mst_link_probe_work);
	INIT_WORK(&mgr->tx_work, drm_dp_tx_work);
	INIT_WORK(&mgr->destroy_connector_work, drm_dp_destroy_connector_work);
	init_waitqueue_head(&mgr->tx_waitq);
	mgr->dev = dev;
	mgr->aux = aux;
	mgr->max_dpcd_transaction_bytes = max_dpcd_transaction_bytes;
	mgr->max_payloads = max_payloads;
	mgr->conn_base_id = conn_base_id;
	if (max_payloads + 1 > sizeof(mgr->payload_mask) * 8 ||
	    max_payloads + 1 > sizeof(mgr->vcpi_mask) * 8)
		return -EINVAL;
	mgr->payloads = kcalloc(max_payloads, sizeof(struct drm_dp_payload), GFP_KERNEL);
	if (!mgr->payloads)
		return -ENOMEM;
	mgr->proposed_vcpis = kcalloc(max_payloads, sizeof(struct drm_dp_vcpi *), GFP_KERNEL);
	if (!mgr->proposed_vcpis)
		return -ENOMEM;
	set_bit(0, &mgr->payload_mask);
	if (test_calc_pbn_mode() < 0)
		DRM_ERROR("MST PBN self-test failed\n");

	mst_state = kzalloc(sizeof(*mst_state), GFP_KERNEL);
	if (mst_state == NULL)
		return -ENOMEM;

	mst_state->mgr = mgr;

	/* max. time slots - one slot for MTP header */
	mst_state->avail_slots = 63;

	drm_atomic_private_obj_init(&mgr->base,
				    &mst_state->base,
				    &mst_state_funcs);

	return 0;
}
EXPORT_SYMBOL(drm_dp_mst_topology_mgr_init);

/**
 * drm_dp_mst_topology_mgr_destroy() - destroy topology manager.
 * @mgr: manager to destroy
 */
void drm_dp_mst_topology_mgr_destroy(struct drm_dp_mst_topology_mgr *mgr)
{
	flush_work(&mgr->work);
	flush_work(&mgr->destroy_connector_work);
	mutex_lock(&mgr->payload_lock);
	kfree(mgr->payloads);
	mgr->payloads = NULL;
	kfree(mgr->proposed_vcpis);
	mgr->proposed_vcpis = NULL;
	mutex_unlock(&mgr->payload_lock);
	mgr->dev = NULL;
	mgr->aux = NULL;
	drm_atomic_private_obj_fini(&mgr->base);
	mgr->funcs = NULL;
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

	if (!reading || (num - 1 > DP_REMOTE_I2C_READ_MAX_TRANSACTIONS)) {
		DRM_DEBUG_KMS("Unsupported I2C transaction for MST device\n");
		ret = -EIO;
		goto out;
	}

	memset(&msg, 0, sizeof(msg));
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
