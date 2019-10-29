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

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/seq_file.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_dp_mst_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "drm_crtc_helper_internal.h"
#include "drm_dp_mst_topology_internal.h"

/**
 * DOC: dp mst helper
 *
 * These functions contain parts of the DisplayPort 1.2a MultiStream Transport
 * protocol. The helpers contain a topology manager and bandwidth manager.
 * The helpers encapsulate the sending and received of sideband msgs.
 */
static bool dump_dp_payload_table(struct drm_dp_mst_topology_mgr *mgr,
				  char *buf);

static void drm_dp_mst_topology_put_port(struct drm_dp_mst_port *port);

static int drm_dp_dpcd_write_payload(struct drm_dp_mst_topology_mgr *mgr,
				     int id,
				     struct drm_dp_payload *payload);

static int drm_dp_send_dpcd_read(struct drm_dp_mst_topology_mgr *mgr,
				 struct drm_dp_mst_port *port,
				 int offset, int size, u8 *bytes);
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

#define DBG_PREFIX "[dp_mst]"

#define DP_STR(x) [DP_ ## x] = #x

static const char *drm_dp_mst_req_type_str(u8 req_type)
{
	static const char * const req_type_str[] = {
		DP_STR(GET_MSG_TRANSACTION_VERSION),
		DP_STR(LINK_ADDRESS),
		DP_STR(CONNECTION_STATUS_NOTIFY),
		DP_STR(ENUM_PATH_RESOURCES),
		DP_STR(ALLOCATE_PAYLOAD),
		DP_STR(QUERY_PAYLOAD),
		DP_STR(RESOURCE_STATUS_NOTIFY),
		DP_STR(CLEAR_PAYLOAD_ID_TABLE),
		DP_STR(REMOTE_DPCD_READ),
		DP_STR(REMOTE_DPCD_WRITE),
		DP_STR(REMOTE_I2C_READ),
		DP_STR(REMOTE_I2C_WRITE),
		DP_STR(POWER_UP_PHY),
		DP_STR(POWER_DOWN_PHY),
		DP_STR(SINK_EVENT_NOTIFY),
		DP_STR(QUERY_STREAM_ENC_STATUS),
	};

	if (req_type >= ARRAY_SIZE(req_type_str) ||
	    !req_type_str[req_type])
		return "unknown";

	return req_type_str[req_type];
}

#undef DP_STR
#define DP_STR(x) [DP_NAK_ ## x] = #x

static const char *drm_dp_mst_nak_reason_str(u8 nak_reason)
{
	static const char * const nak_reason_str[] = {
		DP_STR(WRITE_FAILURE),
		DP_STR(INVALID_READ),
		DP_STR(CRC_FAILURE),
		DP_STR(BAD_PARAM),
		DP_STR(DEFER),
		DP_STR(LINK_FAILURE),
		DP_STR(NO_RESOURCES),
		DP_STR(DPCD_FAIL),
		DP_STR(I2C_NAK),
		DP_STR(ALLOCATE_FAIL),
	};

	if (nak_reason >= ARRAY_SIZE(nak_reason_str) ||
	    !nak_reason_str[nak_reason])
		return "unknown";

	return nak_reason_str[nak_reason];
}

#undef DP_STR
#define DP_STR(x) [DRM_DP_SIDEBAND_TX_ ## x] = #x

static const char *drm_dp_mst_sideband_tx_state_str(int state)
{
	static const char * const sideband_reason_str[] = {
		DP_STR(QUEUED),
		DP_STR(START_SEND),
		DP_STR(SENT),
		DP_STR(RX),
		DP_STR(TIMEOUT),
	};

	if (state >= ARRAY_SIZE(sideband_reason_str) ||
	    !sideband_reason_str[state])
		return "unknown";

	return sideband_reason_str[state];
}

static int
drm_dp_mst_rad_to_str(const u8 rad[8], u8 lct, char *out, size_t len)
{
	int i;
	u8 unpacked_rad[16];

	for (i = 0; i < lct; i++) {
		if (i % 2)
			unpacked_rad[i] = rad[i / 2] >> 4;
		else
			unpacked_rad[i] = rad[i / 2] & BIT_MASK(4);
	}

	/* TODO: Eventually add something to printk so we can format the rad
	 * like this: 1.2.3
	 */
	return snprintf(out, len, "%*phC", lct, unpacked_rad);
}

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

void
drm_dp_encode_sideband_req(const struct drm_dp_sideband_msg_req_body *req,
			   struct drm_dp_sideband_msg_tx *raw)
{
	int idx = 0;
	int i;
	u8 *buf = raw->msg;
	buf[idx++] = req->req_type & 0x7f;

	switch (req->req_type) {
	case DP_ENUM_PATH_RESOURCES:
	case DP_POWER_DOWN_PHY:
	case DP_POWER_UP_PHY:
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
EXPORT_SYMBOL_FOR_TESTS_ONLY(drm_dp_encode_sideband_req);

/* Decode a sideband request we've encoded, mainly used for debugging */
int
drm_dp_decode_sideband_req(const struct drm_dp_sideband_msg_tx *raw,
			   struct drm_dp_sideband_msg_req_body *req)
{
	const u8 *buf = raw->msg;
	int i, idx = 0;

	req->req_type = buf[idx++] & 0x7f;
	switch (req->req_type) {
	case DP_ENUM_PATH_RESOURCES:
	case DP_POWER_DOWN_PHY:
	case DP_POWER_UP_PHY:
		req->u.port_num.port_number = (buf[idx] >> 4) & 0xf;
		break;
	case DP_ALLOCATE_PAYLOAD:
		{
			struct drm_dp_allocate_payload *a =
				&req->u.allocate_payload;

			a->number_sdp_streams = buf[idx] & 0xf;
			a->port_number = (buf[idx] >> 4) & 0xf;

			WARN_ON(buf[++idx] & 0x80);
			a->vcpi = buf[idx] & 0x7f;

			a->pbn = buf[++idx] << 8;
			a->pbn |= buf[++idx];

			idx++;
			for (i = 0; i < a->number_sdp_streams; i++) {
				a->sdp_stream_sink[i] =
					(buf[idx + (i / 2)] >> ((i % 2) ? 0 : 4)) & 0xf;
			}
		}
		break;
	case DP_QUERY_PAYLOAD:
		req->u.query_payload.port_number = (buf[idx] >> 4) & 0xf;
		WARN_ON(buf[++idx] & 0x80);
		req->u.query_payload.vcpi = buf[idx] & 0x7f;
		break;
	case DP_REMOTE_DPCD_READ:
		{
			struct drm_dp_remote_dpcd_read *r = &req->u.dpcd_read;

			r->port_number = (buf[idx] >> 4) & 0xf;

			r->dpcd_address = (buf[idx] << 16) & 0xf0000;
			r->dpcd_address |= (buf[++idx] << 8) & 0xff00;
			r->dpcd_address |= buf[++idx] & 0xff;

			r->num_bytes = buf[++idx];
		}
		break;
	case DP_REMOTE_DPCD_WRITE:
		{
			struct drm_dp_remote_dpcd_write *w =
				&req->u.dpcd_write;

			w->port_number = (buf[idx] >> 4) & 0xf;

			w->dpcd_address = (buf[idx] << 16) & 0xf0000;
			w->dpcd_address |= (buf[++idx] << 8) & 0xff00;
			w->dpcd_address |= buf[++idx] & 0xff;

			w->num_bytes = buf[++idx];

			w->bytes = kmemdup(&buf[++idx], w->num_bytes,
					   GFP_KERNEL);
			if (!w->bytes)
				return -ENOMEM;
		}
		break;
	case DP_REMOTE_I2C_READ:
		{
			struct drm_dp_remote_i2c_read *r = &req->u.i2c_read;
			struct drm_dp_remote_i2c_read_tx *tx;
			bool failed = false;

			r->num_transactions = buf[idx] & 0x3;
			r->port_number = (buf[idx] >> 4) & 0xf;
			for (i = 0; i < r->num_transactions; i++) {
				tx = &r->transactions[i];

				tx->i2c_dev_id = buf[++idx] & 0x7f;
				tx->num_bytes = buf[++idx];
				tx->bytes = kmemdup(&buf[++idx],
						    tx->num_bytes,
						    GFP_KERNEL);
				if (!tx->bytes) {
					failed = true;
					break;
				}
				idx += tx->num_bytes;
				tx->no_stop_bit = (buf[idx] >> 5) & 0x1;
				tx->i2c_transaction_delay = buf[idx] & 0xf;
			}

			if (failed) {
				for (i = 0; i < r->num_transactions; i++)
					kfree(tx->bytes);
				return -ENOMEM;
			}

			r->read_i2c_device_id = buf[++idx] & 0x7f;
			r->num_bytes_read = buf[++idx];
		}
		break;
	case DP_REMOTE_I2C_WRITE:
		{
			struct drm_dp_remote_i2c_write *w = &req->u.i2c_write;

			w->port_number = (buf[idx] >> 4) & 0xf;
			w->write_i2c_device_id = buf[++idx] & 0x7f;
			w->num_bytes = buf[++idx];
			w->bytes = kmemdup(&buf[++idx], w->num_bytes,
					   GFP_KERNEL);
			if (!w->bytes)
				return -ENOMEM;
		}
		break;
	}

	return 0;
}
EXPORT_SYMBOL_FOR_TESTS_ONLY(drm_dp_decode_sideband_req);

void
drm_dp_dump_sideband_msg_req_body(const struct drm_dp_sideband_msg_req_body *req,
				  int indent, struct drm_printer *printer)
{
	int i;

#define P(f, ...) drm_printf_indent(printer, indent, f, ##__VA_ARGS__)
	if (req->req_type == DP_LINK_ADDRESS) {
		/* No contents to print */
		P("type=%s\n", drm_dp_mst_req_type_str(req->req_type));
		return;
	}

	P("type=%s contents:\n", drm_dp_mst_req_type_str(req->req_type));
	indent++;

	switch (req->req_type) {
	case DP_ENUM_PATH_RESOURCES:
	case DP_POWER_DOWN_PHY:
	case DP_POWER_UP_PHY:
		P("port=%d\n", req->u.port_num.port_number);
		break;
	case DP_ALLOCATE_PAYLOAD:
		P("port=%d vcpi=%d pbn=%d sdp_streams=%d %*ph\n",
		  req->u.allocate_payload.port_number,
		  req->u.allocate_payload.vcpi, req->u.allocate_payload.pbn,
		  req->u.allocate_payload.number_sdp_streams,
		  req->u.allocate_payload.number_sdp_streams,
		  req->u.allocate_payload.sdp_stream_sink);
		break;
	case DP_QUERY_PAYLOAD:
		P("port=%d vcpi=%d\n",
		  req->u.query_payload.port_number,
		  req->u.query_payload.vcpi);
		break;
	case DP_REMOTE_DPCD_READ:
		P("port=%d dpcd_addr=%05x len=%d\n",
		  req->u.dpcd_read.port_number, req->u.dpcd_read.dpcd_address,
		  req->u.dpcd_read.num_bytes);
		break;
	case DP_REMOTE_DPCD_WRITE:
		P("port=%d addr=%05x len=%d: %*ph\n",
		  req->u.dpcd_write.port_number,
		  req->u.dpcd_write.dpcd_address,
		  req->u.dpcd_write.num_bytes, req->u.dpcd_write.num_bytes,
		  req->u.dpcd_write.bytes);
		break;
	case DP_REMOTE_I2C_READ:
		P("port=%d num_tx=%d id=%d size=%d:\n",
		  req->u.i2c_read.port_number,
		  req->u.i2c_read.num_transactions,
		  req->u.i2c_read.read_i2c_device_id,
		  req->u.i2c_read.num_bytes_read);

		indent++;
		for (i = 0; i < req->u.i2c_read.num_transactions; i++) {
			const struct drm_dp_remote_i2c_read_tx *rtx =
				&req->u.i2c_read.transactions[i];

			P("%d: id=%03d size=%03d no_stop_bit=%d tx_delay=%03d: %*ph\n",
			  i, rtx->i2c_dev_id, rtx->num_bytes,
			  rtx->no_stop_bit, rtx->i2c_transaction_delay,
			  rtx->num_bytes, rtx->bytes);
		}
		break;
	case DP_REMOTE_I2C_WRITE:
		P("port=%d id=%d size=%d: %*ph\n",
		  req->u.i2c_write.port_number,
		  req->u.i2c_write.write_i2c_device_id,
		  req->u.i2c_write.num_bytes, req->u.i2c_write.num_bytes,
		  req->u.i2c_write.bytes);
		break;
	default:
		P("???\n");
		break;
	}
#undef P
}
EXPORT_SYMBOL_FOR_TESTS_ONLY(drm_dp_dump_sideband_msg_req_body);

static inline void
drm_dp_mst_dump_sideband_msg_tx(struct drm_printer *p,
				const struct drm_dp_sideband_msg_tx *txmsg)
{
	struct drm_dp_sideband_msg_req_body req;
	char buf[64];
	int ret;
	int i;

	drm_dp_mst_rad_to_str(txmsg->dst->rad, txmsg->dst->lct, buf,
			      sizeof(buf));
	drm_printf(p, "txmsg cur_offset=%x cur_len=%x seqno=%x state=%s path_msg=%d dst=%s\n",
		   txmsg->cur_offset, txmsg->cur_len, txmsg->seqno,
		   drm_dp_mst_sideband_tx_state_str(txmsg->state),
		   txmsg->path_msg, buf);

	ret = drm_dp_decode_sideband_req(txmsg, &req);
	if (ret) {
		drm_printf(p, "<failed to decode sideband req: %d>\n", ret);
		return;
	}
	drm_dp_dump_sideband_msg_req_body(&req, 1, p);

	switch (req.req_type) {
	case DP_REMOTE_DPCD_WRITE:
		kfree(req.u.dpcd_write.bytes);
		break;
	case DP_REMOTE_I2C_READ:
		for (i = 0; i < req.u.i2c_read.num_transactions; i++)
			kfree(req.u.i2c_read.transactions[i].bytes);
		break;
	case DP_REMOTE_I2C_WRITE:
		kfree(req.u.i2c_write.bytes);
		break;
	}
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
	idx++;
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

static bool drm_dp_sideband_parse_power_updown_phy_ack(struct drm_dp_sideband_msg_rx *raw,
						       struct drm_dp_sideband_msg_reply_body *repmsg)
{
	int idx = 1;

	repmsg->u.port_number.port_number = (raw->msg[idx] >> 4) & 0xf;
	idx++;
	if (idx > raw->curlen) {
		DRM_DEBUG_KMS("power up/down phy parse length fail %d %d\n",
			      idx, raw->curlen);
		return false;
	}
	return true;
}

static bool drm_dp_sideband_parse_reply(struct drm_dp_sideband_msg_rx *raw,
					struct drm_dp_sideband_msg_reply_body *msg)
{
	memset(msg, 0, sizeof(*msg));
	msg->reply_type = (raw->msg[0] & 0x80) >> 7;
	msg->req_type = (raw->msg[0] & 0x7f);

	if (msg->reply_type == DP_SIDEBAND_REPLY_NAK) {
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
	case DP_POWER_DOWN_PHY:
	case DP_POWER_UP_PHY:
		return drm_dp_sideband_parse_power_updown_phy_ack(raw, msg);
	default:
		DRM_ERROR("Got unknown reply 0x%02x (%s)\n", msg->req_type,
			  drm_dp_mst_req_type_str(msg->req_type));
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
		DRM_ERROR("Got unknown request 0x%02x (%s)\n", msg->req_type,
			  drm_dp_mst_req_type_str(msg->req_type));
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

static int build_power_updown_phy(struct drm_dp_sideband_msg_tx *msg,
				  int port_num, bool power_up)
{
	struct drm_dp_sideband_msg_req_body req;

	if (power_up)
		req.req_type = DP_POWER_UP_PHY;
	else
		req.req_type = DP_POWER_DOWN_PHY;

	req.u.port_num.port_number = port_num;
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
		if (mgr->proposed_vcpis[i] &&
		    mgr->proposed_vcpis[i]->vcpi == vcpi) {
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
	if (unlikely(ret == -EIO) && drm_debug_enabled(DRM_UT_DP)) {
		struct drm_printer p = drm_debug_printer(DBG_PREFIX);

		drm_dp_mst_dump_sideband_msg_tx(&p, txmsg);
	}
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
	kref_init(&mstb->topology_kref);
	kref_init(&mstb->malloc_kref);
	return mstb;
}

static void drm_dp_free_mst_branch_device(struct kref *kref)
{
	struct drm_dp_mst_branch *mstb =
		container_of(kref, struct drm_dp_mst_branch, malloc_kref);

	if (mstb->port_parent)
		drm_dp_mst_put_port_malloc(mstb->port_parent);

	kfree(mstb);
}

/**
 * DOC: Branch device and port refcounting
 *
 * Topology refcount overview
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * The refcounting schemes for &struct drm_dp_mst_branch and &struct
 * drm_dp_mst_port are somewhat unusual. Both ports and branch devices have
 * two different kinds of refcounts: topology refcounts, and malloc refcounts.
 *
 * Topology refcounts are not exposed to drivers, and are handled internally
 * by the DP MST helpers. The helpers use them in order to prevent the
 * in-memory topology state from being changed in the middle of critical
 * operations like changing the internal state of payload allocations. This
 * means each branch and port will be considered to be connected to the rest
 * of the topology until its topology refcount reaches zero. Additionally,
 * for ports this means that their associated &struct drm_connector will stay
 * registered with userspace until the port's refcount reaches 0.
 *
 * Malloc refcount overview
 * ~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Malloc references are used to keep a &struct drm_dp_mst_port or &struct
 * drm_dp_mst_branch allocated even after all of its topology references have
 * been dropped, so that the driver or MST helpers can safely access each
 * branch's last known state before it was disconnected from the topology.
 * When the malloc refcount of a port or branch reaches 0, the memory
 * allocation containing the &struct drm_dp_mst_branch or &struct
 * drm_dp_mst_port respectively will be freed.
 *
 * For &struct drm_dp_mst_branch, malloc refcounts are not currently exposed
 * to drivers. As of writing this documentation, there are no drivers that
 * have a usecase for accessing &struct drm_dp_mst_branch outside of the MST
 * helpers. Exposing this API to drivers in a race-free manner would take more
 * tweaking of the refcounting scheme, however patches are welcome provided
 * there is a legitimate driver usecase for this.
 *
 * Refcount relationships in a topology
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Let's take a look at why the relationship between topology and malloc
 * refcounts is designed the way it is.
 *
 * .. kernel-figure:: dp-mst/topology-figure-1.dot
 *
 *    An example of topology and malloc refs in a DP MST topology with two
 *    active payloads. Topology refcount increments are indicated by solid
 *    lines, and malloc refcount increments are indicated by dashed lines.
 *    Each starts from the branch which incremented the refcount, and ends at
 *    the branch to which the refcount belongs to, i.e. the arrow points the
 *    same way as the C pointers used to reference a structure.
 *
 * As you can see in the above figure, every branch increments the topology
 * refcount of its children, and increments the malloc refcount of its
 * parent. Additionally, every payload increments the malloc refcount of its
 * assigned port by 1.
 *
 * So, what would happen if MSTB #3 from the above figure was unplugged from
 * the system, but the driver hadn't yet removed payload #2 from port #3? The
 * topology would start to look like the figure below.
 *
 * .. kernel-figure:: dp-mst/topology-figure-2.dot
 *
 *    Ports and branch devices which have been released from memory are
 *    colored grey, and references which have been removed are colored red.
 *
 * Whenever a port or branch device's topology refcount reaches zero, it will
 * decrement the topology refcounts of all its children, the malloc refcount
 * of its parent, and finally its own malloc refcount. For MSTB #4 and port
 * #4, this means they both have been disconnected from the topology and freed
 * from memory. But, because payload #2 is still holding a reference to port
 * #3, port #3 is removed from the topology but its &struct drm_dp_mst_port
 * is still accessible from memory. This also means port #3 has not yet
 * decremented the malloc refcount of MSTB #3, so its &struct
 * drm_dp_mst_branch will also stay allocated in memory until port #3's
 * malloc refcount reaches 0.
 *
 * This relationship is necessary because in order to release payload #2, we
 * need to be able to figure out the last relative of port #3 that's still
 * connected to the topology. In this case, we would travel up the topology as
 * shown below.
 *
 * .. kernel-figure:: dp-mst/topology-figure-3.dot
 *
 * And finally, remove payload #2 by communicating with port #2 through
 * sideband transactions.
 */

/**
 * drm_dp_mst_get_mstb_malloc() - Increment the malloc refcount of a branch
 * device
 * @mstb: The &struct drm_dp_mst_branch to increment the malloc refcount of
 *
 * Increments &drm_dp_mst_branch.malloc_kref. When
 * &drm_dp_mst_branch.malloc_kref reaches 0, the memory allocation for @mstb
 * will be released and @mstb may no longer be used.
 *
 * See also: drm_dp_mst_put_mstb_malloc()
 */
static void
drm_dp_mst_get_mstb_malloc(struct drm_dp_mst_branch *mstb)
{
	kref_get(&mstb->malloc_kref);
	DRM_DEBUG("mstb %p (%d)\n", mstb, kref_read(&mstb->malloc_kref));
}

/**
 * drm_dp_mst_put_mstb_malloc() - Decrement the malloc refcount of a branch
 * device
 * @mstb: The &struct drm_dp_mst_branch to decrement the malloc refcount of
 *
 * Decrements &drm_dp_mst_branch.malloc_kref. When
 * &drm_dp_mst_branch.malloc_kref reaches 0, the memory allocation for @mstb
 * will be released and @mstb may no longer be used.
 *
 * See also: drm_dp_mst_get_mstb_malloc()
 */
static void
drm_dp_mst_put_mstb_malloc(struct drm_dp_mst_branch *mstb)
{
	DRM_DEBUG("mstb %p (%d)\n", mstb, kref_read(&mstb->malloc_kref) - 1);
	kref_put(&mstb->malloc_kref, drm_dp_free_mst_branch_device);
}

static void drm_dp_free_mst_port(struct kref *kref)
{
	struct drm_dp_mst_port *port =
		container_of(kref, struct drm_dp_mst_port, malloc_kref);

	drm_dp_mst_put_mstb_malloc(port->parent);
	kfree(port);
}

/**
 * drm_dp_mst_get_port_malloc() - Increment the malloc refcount of an MST port
 * @port: The &struct drm_dp_mst_port to increment the malloc refcount of
 *
 * Increments &drm_dp_mst_port.malloc_kref. When &drm_dp_mst_port.malloc_kref
 * reaches 0, the memory allocation for @port will be released and @port may
 * no longer be used.
 *
 * Because @port could potentially be freed at any time by the DP MST helpers
 * if &drm_dp_mst_port.malloc_kref reaches 0, including during a call to this
 * function, drivers that which to make use of &struct drm_dp_mst_port should
 * ensure that they grab at least one main malloc reference to their MST ports
 * in &drm_dp_mst_topology_cbs.add_connector. This callback is called before
 * there is any chance for &drm_dp_mst_port.malloc_kref to reach 0.
 *
 * See also: drm_dp_mst_put_port_malloc()
 */
void
drm_dp_mst_get_port_malloc(struct drm_dp_mst_port *port)
{
	kref_get(&port->malloc_kref);
	DRM_DEBUG("port %p (%d)\n", port, kref_read(&port->malloc_kref));
}
EXPORT_SYMBOL(drm_dp_mst_get_port_malloc);

/**
 * drm_dp_mst_put_port_malloc() - Decrement the malloc refcount of an MST port
 * @port: The &struct drm_dp_mst_port to decrement the malloc refcount of
 *
 * Decrements &drm_dp_mst_port.malloc_kref. When &drm_dp_mst_port.malloc_kref
 * reaches 0, the memory allocation for @port will be released and @port may
 * no longer be used.
 *
 * See also: drm_dp_mst_get_port_malloc()
 */
void
drm_dp_mst_put_port_malloc(struct drm_dp_mst_port *port)
{
	DRM_DEBUG("port %p (%d)\n", port, kref_read(&port->malloc_kref) - 1);
	kref_put(&port->malloc_kref, drm_dp_free_mst_port);
}
EXPORT_SYMBOL(drm_dp_mst_put_port_malloc);

static void drm_dp_destroy_mst_branch_device(struct kref *kref)
{
	struct drm_dp_mst_branch *mstb =
		container_of(kref, struct drm_dp_mst_branch, topology_kref);
	struct drm_dp_mst_topology_mgr *mgr = mstb->mgr;
	struct drm_dp_mst_port *port, *tmp;
	bool wake_tx = false;

	mutex_lock(&mgr->lock);
	list_for_each_entry_safe(port, tmp, &mstb->ports, next) {
		list_del(&port->next);
		drm_dp_mst_topology_put_port(port);
	}
	mutex_unlock(&mgr->lock);

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

	drm_dp_mst_put_mstb_malloc(mstb);
}

/**
 * drm_dp_mst_topology_try_get_mstb() - Increment the topology refcount of a
 * branch device unless it's zero
 * @mstb: &struct drm_dp_mst_branch to increment the topology refcount of
 *
 * Attempts to grab a topology reference to @mstb, if it hasn't yet been
 * removed from the topology (e.g. &drm_dp_mst_branch.topology_kref has
 * reached 0). Holding a topology reference implies that a malloc reference
 * will be held to @mstb as long as the user holds the topology reference.
 *
 * Care should be taken to ensure that the user has at least one malloc
 * reference to @mstb. If you already have a topology reference to @mstb, you
 * should use drm_dp_mst_topology_get_mstb() instead.
 *
 * See also:
 * drm_dp_mst_topology_get_mstb()
 * drm_dp_mst_topology_put_mstb()
 *
 * Returns:
 * * 1: A topology reference was grabbed successfully
 * * 0: @port is no longer in the topology, no reference was grabbed
 */
static int __must_check
drm_dp_mst_topology_try_get_mstb(struct drm_dp_mst_branch *mstb)
{
	int ret = kref_get_unless_zero(&mstb->topology_kref);

	if (ret)
		DRM_DEBUG("mstb %p (%d)\n", mstb,
			  kref_read(&mstb->topology_kref));

	return ret;
}

/**
 * drm_dp_mst_topology_get_mstb() - Increment the topology refcount of a
 * branch device
 * @mstb: The &struct drm_dp_mst_branch to increment the topology refcount of
 *
 * Increments &drm_dp_mst_branch.topology_refcount without checking whether or
 * not it's already reached 0. This is only valid to use in scenarios where
 * you are already guaranteed to have at least one active topology reference
 * to @mstb. Otherwise, drm_dp_mst_topology_try_get_mstb() must be used.
 *
 * See also:
 * drm_dp_mst_topology_try_get_mstb()
 * drm_dp_mst_topology_put_mstb()
 */
static void drm_dp_mst_topology_get_mstb(struct drm_dp_mst_branch *mstb)
{
	WARN_ON(kref_read(&mstb->topology_kref) == 0);
	kref_get(&mstb->topology_kref);
	DRM_DEBUG("mstb %p (%d)\n", mstb, kref_read(&mstb->topology_kref));
}

/**
 * drm_dp_mst_topology_put_mstb() - release a topology reference to a branch
 * device
 * @mstb: The &struct drm_dp_mst_branch to release the topology reference from
 *
 * Releases a topology reference from @mstb by decrementing
 * &drm_dp_mst_branch.topology_kref.
 *
 * See also:
 * drm_dp_mst_topology_try_get_mstb()
 * drm_dp_mst_topology_get_mstb()
 */
static void
drm_dp_mst_topology_put_mstb(struct drm_dp_mst_branch *mstb)
{
	DRM_DEBUG("mstb %p (%d)\n",
		  mstb, kref_read(&mstb->topology_kref) - 1);
	kref_put(&mstb->topology_kref, drm_dp_destroy_mst_branch_device);
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
		drm_dp_mst_topology_put_mstb(mstb);
		break;
	}
}

static void drm_dp_destroy_port(struct kref *kref)
{
	struct drm_dp_mst_port *port =
		container_of(kref, struct drm_dp_mst_port, topology_kref);
	struct drm_dp_mst_topology_mgr *mgr = port->mgr;

	if (!port->input) {
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
	drm_dp_mst_put_port_malloc(port);
}

/**
 * drm_dp_mst_topology_try_get_port() - Increment the topology refcount of a
 * port unless it's zero
 * @port: &struct drm_dp_mst_port to increment the topology refcount of
 *
 * Attempts to grab a topology reference to @port, if it hasn't yet been
 * removed from the topology (e.g. &drm_dp_mst_port.topology_kref has reached
 * 0). Holding a topology reference implies that a malloc reference will be
 * held to @port as long as the user holds the topology reference.
 *
 * Care should be taken to ensure that the user has at least one malloc
 * reference to @port. If you already have a topology reference to @port, you
 * should use drm_dp_mst_topology_get_port() instead.
 *
 * See also:
 * drm_dp_mst_topology_get_port()
 * drm_dp_mst_topology_put_port()
 *
 * Returns:
 * * 1: A topology reference was grabbed successfully
 * * 0: @port is no longer in the topology, no reference was grabbed
 */
static int __must_check
drm_dp_mst_topology_try_get_port(struct drm_dp_mst_port *port)
{
	int ret = kref_get_unless_zero(&port->topology_kref);

	if (ret)
		DRM_DEBUG("port %p (%d)\n", port,
			  kref_read(&port->topology_kref));

	return ret;
}

/**
 * drm_dp_mst_topology_get_port() - Increment the topology refcount of a port
 * @port: The &struct drm_dp_mst_port to increment the topology refcount of
 *
 * Increments &drm_dp_mst_port.topology_refcount without checking whether or
 * not it's already reached 0. This is only valid to use in scenarios where
 * you are already guaranteed to have at least one active topology reference
 * to @port. Otherwise, drm_dp_mst_topology_try_get_port() must be used.
 *
 * See also:
 * drm_dp_mst_topology_try_get_port()
 * drm_dp_mst_topology_put_port()
 */
static void drm_dp_mst_topology_get_port(struct drm_dp_mst_port *port)
{
	WARN_ON(kref_read(&port->topology_kref) == 0);
	kref_get(&port->topology_kref);
	DRM_DEBUG("port %p (%d)\n", port, kref_read(&port->topology_kref));
}

/**
 * drm_dp_mst_topology_put_port() - release a topology reference to a port
 * @port: The &struct drm_dp_mst_port to release the topology reference from
 *
 * Releases a topology reference from @port by decrementing
 * &drm_dp_mst_port.topology_kref.
 *
 * See also:
 * drm_dp_mst_topology_try_get_port()
 * drm_dp_mst_topology_get_port()
 */
static void drm_dp_mst_topology_put_port(struct drm_dp_mst_port *port)
{
	DRM_DEBUG("port %p (%d)\n",
		  port, kref_read(&port->topology_kref) - 1);
	kref_put(&port->topology_kref, drm_dp_destroy_port);
}

static struct drm_dp_mst_branch *
drm_dp_mst_topology_get_mstb_validated_locked(struct drm_dp_mst_branch *mstb,
					      struct drm_dp_mst_branch *to_find)
{
	struct drm_dp_mst_port *port;
	struct drm_dp_mst_branch *rmstb;

	if (to_find == mstb)
		return mstb;

	list_for_each_entry(port, &mstb->ports, next) {
		if (port->mstb) {
			rmstb = drm_dp_mst_topology_get_mstb_validated_locked(
			    port->mstb, to_find);
			if (rmstb)
				return rmstb;
		}
	}
	return NULL;
}

static struct drm_dp_mst_branch *
drm_dp_mst_topology_get_mstb_validated(struct drm_dp_mst_topology_mgr *mgr,
				       struct drm_dp_mst_branch *mstb)
{
	struct drm_dp_mst_branch *rmstb = NULL;

	mutex_lock(&mgr->lock);
	if (mgr->mst_primary) {
		rmstb = drm_dp_mst_topology_get_mstb_validated_locked(
		    mgr->mst_primary, mstb);

		if (rmstb && !drm_dp_mst_topology_try_get_mstb(rmstb))
			rmstb = NULL;
	}
	mutex_unlock(&mgr->lock);
	return rmstb;
}

static struct drm_dp_mst_port *
drm_dp_mst_topology_get_port_validated_locked(struct drm_dp_mst_branch *mstb,
					      struct drm_dp_mst_port *to_find)
{
	struct drm_dp_mst_port *port, *mport;

	list_for_each_entry(port, &mstb->ports, next) {
		if (port == to_find)
			return port;

		if (port->mstb) {
			mport = drm_dp_mst_topology_get_port_validated_locked(
			    port->mstb, to_find);
			if (mport)
				return mport;
		}
	}
	return NULL;
}

static struct drm_dp_mst_port *
drm_dp_mst_topology_get_port_validated(struct drm_dp_mst_topology_mgr *mgr,
				       struct drm_dp_mst_port *port)
{
	struct drm_dp_mst_port *rport = NULL;

	mutex_lock(&mgr->lock);
	if (mgr->mst_primary) {
		rport = drm_dp_mst_topology_get_port_validated_locked(
		    mgr->mst_primary, port);

		if (rport && !drm_dp_mst_topology_try_get_port(rport))
			rport = NULL;
	}
	mutex_unlock(&mgr->lock);
	return rport;
}

static struct drm_dp_mst_port *drm_dp_get_port(struct drm_dp_mst_branch *mstb, u8 port_num)
{
	struct drm_dp_mst_port *port;
	int ret;

	list_for_each_entry(port, &mstb->ports, next) {
		if (port->port_num == port_num) {
			ret = drm_dp_mst_topology_try_get_port(port);
			return ret ? port : NULL;
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
		if (port->mstb) {
			port->mstb->mgr = port->mgr;
			port->mstb->port_parent = port;
			/*
			 * Make sure this port's memory allocation stays
			 * around until its child MSTB releases it
			 */
			drm_dp_mst_get_port_malloc(port);

			send_link = true;
		}
		break;
	}
	return send_link;
}

/**
 * drm_dp_mst_dpcd_read() - read a series of bytes from the DPCD via sideband
 * @aux: Fake sideband AUX CH
 * @offset: address of the (first) register to read
 * @buffer: buffer to store the register values
 * @size: number of bytes in @buffer
 *
 * Performs the same functionality for remote devices via
 * sideband messaging as drm_dp_dpcd_read() does for local
 * devices via actual AUX CH.
 *
 * Return: Number of bytes read, or negative error code on failure.
 */
ssize_t drm_dp_mst_dpcd_read(struct drm_dp_aux *aux,
			     unsigned int offset, void *buffer, size_t size)
{
	struct drm_dp_mst_port *port = container_of(aux, struct drm_dp_mst_port,
						    aux);

	return drm_dp_send_dpcd_read(port->mgr, port,
				     offset, size, buffer);
}

/**
 * drm_dp_mst_dpcd_write() - write a series of bytes to the DPCD via sideband
 * @aux: Fake sideband AUX CH
 * @offset: address of the (first) register to write
 * @buffer: buffer containing the values to write
 * @size: number of bytes in @buffer
 *
 * Performs the same functionality for remote devices via
 * sideband messaging as drm_dp_dpcd_write() does for local
 * devices via actual AUX CH.
 *
 * Return: 0 on success, negative error code on failure.
 */
ssize_t drm_dp_mst_dpcd_write(struct drm_dp_aux *aux,
			      unsigned int offset, void *buffer, size_t size)
{
	struct drm_dp_mst_port *port = container_of(aux, struct drm_dp_mst_port,
						    aux);

	return drm_dp_send_dpcd_write(port->mgr, port,
				      offset, size, buffer);
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

/**
 * drm_dp_mst_connector_late_register() - Late MST connector registration
 * @connector: The MST connector
 * @port: The MST port for this connector
 *
 * Helper to register the remote aux device for this MST port. Drivers should
 * call this from their mst connector's late_register hook to enable MST aux
 * devices.
 *
 * Return: 0 on success, negative error code on failure.
 */
int drm_dp_mst_connector_late_register(struct drm_connector *connector,
				       struct drm_dp_mst_port *port)
{
	DRM_DEBUG_KMS("registering %s remote bus for %s\n",
		      port->aux.name, connector->kdev->kobj.name);

	port->aux.dev = connector->kdev;
	return drm_dp_aux_register_devnode(&port->aux);
}
EXPORT_SYMBOL(drm_dp_mst_connector_late_register);

/**
 * drm_dp_mst_connector_early_unregister() - Early MST connector unregistration
 * @connector: The MST connector
 * @port: The MST port for this connector
 *
 * Helper to unregister the remote aux device for this MST port, registered by
 * drm_dp_mst_connector_late_register(). Drivers should call this from their mst
 * connector's early_unregister hook.
 */
void drm_dp_mst_connector_early_unregister(struct drm_connector *connector,
					   struct drm_dp_mst_port *port)
{
	DRM_DEBUG_KMS("unregistering %s remote bus for %s\n",
		      port->aux.name, connector->kdev->kobj.name);
	drm_dp_aux_unregister_devnode(&port->aux);
}
EXPORT_SYMBOL(drm_dp_mst_connector_early_unregister);

static void
drm_dp_mst_handle_link_address_port(struct drm_dp_mst_branch *mstb,
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
		kref_init(&port->topology_kref);
		kref_init(&port->malloc_kref);
		port->parent = mstb;
		port->port_num = port_msg->port_number;
		port->mgr = mstb->mgr;
		port->aux.name = "DPMST";
		port->aux.dev = dev->dev;
		port->aux.is_remote = true;

		/*
		 * Make sure the memory allocation for our parent branch stays
		 * around until our own memory allocation is released
		 */
		drm_dp_mst_get_mstb_malloc(mstb);

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
		drm_dp_mst_topology_get_port(port);
		list_add(&port->next, &mstb->ports);
		mutex_unlock(&mstb->mgr->lock);
	}

	if (old_ddps != port->ddps) {
		if (port->ddps) {
			if (!port->input) {
				drm_dp_send_enum_path_resources(mstb->mgr,
								mstb, port);
			}
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

		build_mst_prop_path(mstb, port->port_num, proppath,
				    sizeof(proppath));
		port->connector = (*mstb->mgr->cbs->add_connector)(mstb->mgr,
								   port,
								   proppath);
		if (!port->connector) {
			/* remove it from the port list */
			mutex_lock(&mstb->mgr->lock);
			list_del(&port->next);
			mutex_unlock(&mstb->mgr->lock);
			/* drop port list reference */
			drm_dp_mst_topology_put_port(port);
			goto out;
		}
		if ((port->pdt == DP_PEER_DEVICE_DP_LEGACY_CONV ||
		     port->pdt == DP_PEER_DEVICE_SST_SINK) &&
		    port->port_num >= DP_MST_LOGICAL_PORT_0) {
			port->cached_edid = drm_get_edid(port->connector,
							 &port->aux.ddc);
			drm_connector_set_tile_property(port->connector);
		}
		(*mstb->mgr->cbs->register_connector)(port->connector);
	}

out:
	/* put reference to this port */
	drm_dp_mst_topology_put_port(port);
}

static void
drm_dp_mst_handle_conn_stat(struct drm_dp_mst_branch *mstb,
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

	drm_dp_mst_topology_put_port(port);
	if (dowork)
		queue_work(system_long_wq, &mstb->mgr->work);

}

static struct drm_dp_mst_branch *drm_dp_get_mst_branch_device(struct drm_dp_mst_topology_mgr *mgr,
							       u8 lct, u8 *rad)
{
	struct drm_dp_mst_branch *mstb;
	struct drm_dp_mst_port *port;
	int i, ret;
	/* find the port by iterating down */

	mutex_lock(&mgr->lock);
	mstb = mgr->mst_primary;

	if (!mstb)
		goto out;

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
	ret = drm_dp_mst_topology_try_get_mstb(mstb);
	if (!ret)
		mstb = NULL;
out:
	mutex_unlock(&mgr->lock);
	return mstb;
}

static struct drm_dp_mst_branch *get_mst_branch_device_by_guid_helper(
	struct drm_dp_mst_branch *mstb,
	const uint8_t *guid)
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

static struct drm_dp_mst_branch *
drm_dp_get_mst_branch_device_by_guid(struct drm_dp_mst_topology_mgr *mgr,
				     const uint8_t *guid)
{
	struct drm_dp_mst_branch *mstb;
	int ret;

	/* find the port by iterating down */
	mutex_lock(&mgr->lock);

	mstb = get_mst_branch_device_by_guid_helper(mgr->mst_primary, guid);
	if (mstb) {
		ret = drm_dp_mst_topology_try_get_mstb(mstb);
		if (!ret)
			mstb = NULL;
	}

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
			mstb_child = drm_dp_mst_topology_get_mstb_validated(
			    mgr, port->mstb);
			if (mstb_child) {
				drm_dp_check_and_send_link_address(mgr, mstb_child);
				drm_dp_mst_topology_put_mstb(mstb_child);
			}
		}
	}
}

static void drm_dp_mst_link_probe_work(struct work_struct *work)
{
	struct drm_dp_mst_topology_mgr *mgr = container_of(work, struct drm_dp_mst_topology_mgr, work);
	struct drm_dp_mst_branch *mstb;
	int ret;

	mutex_lock(&mgr->lock);
	mstb = mgr->mst_primary;
	if (mstb) {
		ret = drm_dp_mst_topology_try_get_mstb(mstb);
		if (!ret)
			mstb = NULL;
	}
	mutex_unlock(&mgr->lock);
	if (mstb) {
		drm_dp_check_and_send_link_address(mgr, mstb);
		drm_dp_mst_topology_put_mstb(mstb);
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
	if (unlikely(ret) && drm_debug_enabled(DRM_UT_DP)) {
		struct drm_printer p = drm_debug_printer(DBG_PREFIX);

		drm_printf(&p, "sideband msg failed to send\n");
		drm_dp_mst_dump_sideband_msg_tx(&p, txmsg);
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

	if (txmsg->seqno != -1) {
		WARN_ON((unsigned int)txmsg->seqno >
			ARRAY_SIZE(txmsg->dst->tx_slots));
		txmsg->dst->tx_slots[txmsg->seqno] = NULL;
	}
}

static void drm_dp_queue_down_tx(struct drm_dp_mst_topology_mgr *mgr,
				 struct drm_dp_sideband_msg_tx *txmsg)
{
	mutex_lock(&mgr->qlock);
	list_add_tail(&txmsg->next, &mgr->tx_msg_downq);

	if (drm_debug_enabled(DRM_UT_DP)) {
		struct drm_printer p = drm_debug_printer(DBG_PREFIX);

		drm_dp_mst_dump_sideband_msg_tx(&p, txmsg);
	}

	if (list_is_singular(&mgr->tx_msg_downq))
		process_single_down_tx_qlock(mgr);
	mutex_unlock(&mgr->qlock);
}

static void
drm_dp_dump_link_address(struct drm_dp_link_address_ack_reply *reply)
{
	struct drm_dp_link_addr_reply_port *port_reply;
	int i;

	for (i = 0; i < reply->nports; i++) {
		port_reply = &reply->ports[i];
		DRM_DEBUG_KMS("port %d: input %d, pdt: %d, pn: %d, dpcd_rev: %02x, mcs: %d, ddps: %d, ldps %d, sdp %d/%d\n",
			      i,
			      port_reply->input_port,
			      port_reply->peer_device_type,
			      port_reply->port_number,
			      port_reply->dpcd_revision,
			      port_reply->mcs,
			      port_reply->ddps,
			      port_reply->legacy_device_plug_status,
			      port_reply->num_sdp_streams,
			      port_reply->num_sdp_stream_sinks);
	}
}

static void drm_dp_send_link_address(struct drm_dp_mst_topology_mgr *mgr,
				     struct drm_dp_mst_branch *mstb)
{
	struct drm_dp_sideband_msg_tx *txmsg;
	struct drm_dp_link_address_ack_reply *reply;
	int i, len, ret;

	txmsg = kzalloc(sizeof(*txmsg), GFP_KERNEL);
	if (!txmsg)
		return;

	txmsg->dst = mstb;
	len = build_link_address(txmsg);

	mstb->link_address_sent = true;
	drm_dp_queue_down_tx(mgr, txmsg);

	/* FIXME: Actually do some real error handling here */
	ret = drm_dp_mst_wait_tx_reply(mstb, txmsg);
	if (ret <= 0) {
		DRM_ERROR("Sending link address failed with %d\n", ret);
		goto out;
	}
	if (txmsg->reply.reply_type == DP_SIDEBAND_REPLY_NAK) {
		DRM_ERROR("link address NAK received\n");
		ret = -EIO;
		goto out;
	}

	reply = &txmsg->reply.u.link_addr;
	DRM_DEBUG_KMS("link address reply: %d\n", reply->nports);
	drm_dp_dump_link_address(reply);

	drm_dp_check_mstb_guid(mstb, reply->guid);

	for (i = 0; i < reply->nports; i++)
		drm_dp_mst_handle_link_address_port(mstb, mgr->dev,
						    &reply->ports[i]);

	drm_kms_helper_hotplug_event(mgr->dev);

out:
	if (ret <= 0)
		mstb->link_address_sent = false;
	kfree(txmsg);
}

static int
drm_dp_send_enum_path_resources(struct drm_dp_mst_topology_mgr *mgr,
				struct drm_dp_mst_branch *mstb,
				struct drm_dp_mst_port *port)
{
	struct drm_dp_enum_path_resources_ack_reply *path_res;
	struct drm_dp_sideband_msg_tx *txmsg;
	int len;
	int ret;

	txmsg = kzalloc(sizeof(*txmsg), GFP_KERNEL);
	if (!txmsg)
		return -ENOMEM;

	txmsg->dst = mstb;
	len = build_enum_path_resources(txmsg, port->port_num);

	drm_dp_queue_down_tx(mgr, txmsg);

	ret = drm_dp_mst_wait_tx_reply(mstb, txmsg);
	if (ret > 0) {
		path_res = &txmsg->reply.u.path_resources;

		if (txmsg->reply.reply_type == DP_SIDEBAND_REPLY_NAK) {
			DRM_DEBUG_KMS("enum path resources nak received\n");
		} else {
			if (port->port_num != path_res->port_number)
				DRM_ERROR("got incorrect port in response\n");

			DRM_DEBUG_KMS("enum path resources %d: %d %d\n",
				      path_res->port_number,
				      path_res->full_payload_bw_number,
				      path_res->avail_payload_bw_number);
			port->available_pbn =
				path_res->avail_payload_bw_number;
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

/*
 * Searches upwards in the topology starting from mstb to try to find the
 * closest available parent of mstb that's still connected to the rest of the
 * topology. This can be used in order to perform operations like releasing
 * payloads, where the branch device which owned the payload may no longer be
 * around and thus would require that the payload on the last living relative
 * be freed instead.
 */
static struct drm_dp_mst_branch *
drm_dp_get_last_connected_port_and_mstb(struct drm_dp_mst_topology_mgr *mgr,
					struct drm_dp_mst_branch *mstb,
					int *port_num)
{
	struct drm_dp_mst_branch *rmstb = NULL;
	struct drm_dp_mst_port *found_port;

	mutex_lock(&mgr->lock);
	if (!mgr->mst_primary)
		goto out;

	do {
		found_port = drm_dp_get_last_connected_port_to_mstb(mstb);
		if (!found_port)
			break;

		if (drm_dp_mst_topology_try_get_mstb(found_port->parent)) {
			rmstb = found_port->parent;
			*port_num = found_port->port_num;
		} else {
			/* Search again, starting from this parent */
			mstb = found_port->parent;
		}
	} while (!rmstb);
out:
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

	port_num = port->port_num;
	mstb = drm_dp_mst_topology_get_mstb_validated(mgr, port->parent);
	if (!mstb) {
		mstb = drm_dp_get_last_connected_port_and_mstb(mgr,
							       port->parent,
							       &port_num);

		if (!mstb)
			return -EINVAL;
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

	/*
	 * FIXME: there is a small chance that between getting the last
	 * connected mstb and sending the payload message, the last connected
	 * mstb could also be removed from the topology. In the future, this
	 * needs to be fixed by restarting the
	 * drm_dp_get_last_connected_port_and_mstb() search in the event of a
	 * timeout if the topology is still connected to the system.
	 */
	ret = drm_dp_mst_wait_tx_reply(mstb, txmsg);
	if (ret > 0) {
		if (txmsg->reply.reply_type == DP_SIDEBAND_REPLY_NAK)
			ret = -EINVAL;
		else
			ret = 0;
	}
	kfree(txmsg);
fail_put:
	drm_dp_mst_topology_put_mstb(mstb);
	return ret;
}

int drm_dp_send_power_updown_phy(struct drm_dp_mst_topology_mgr *mgr,
				 struct drm_dp_mst_port *port, bool power_up)
{
	struct drm_dp_sideband_msg_tx *txmsg;
	int len, ret;

	port = drm_dp_mst_topology_get_port_validated(mgr, port);
	if (!port)
		return -EINVAL;

	txmsg = kzalloc(sizeof(*txmsg), GFP_KERNEL);
	if (!txmsg) {
		drm_dp_mst_topology_put_port(port);
		return -ENOMEM;
	}

	txmsg->dst = port->parent;
	len = build_power_updown_phy(txmsg, port->port_num, power_up);
	drm_dp_queue_down_tx(mgr, txmsg);

	ret = drm_dp_mst_wait_tx_reply(port->parent, txmsg);
	if (ret > 0) {
		if (txmsg->reply.reply_type == DP_SIDEBAND_REPLY_NAK)
			ret = -EINVAL;
		else
			ret = 0;
	}
	kfree(txmsg);
	drm_dp_mst_topology_put_port(port);

	return ret;
}
EXPORT_SYMBOL(drm_dp_send_power_updown_phy);

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
	/* it's okay for these to fail */
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
	struct drm_dp_payload req_payload;
	struct drm_dp_mst_port *port;
	int i, j;
	int cur_slots = 1;

	mutex_lock(&mgr->payload_lock);
	for (i = 0; i < mgr->max_payloads; i++) {
		struct drm_dp_vcpi *vcpi = mgr->proposed_vcpis[i];
		struct drm_dp_payload *payload = &mgr->payloads[i];
		bool put_port = false;

		/* solve the current payloads - compare to the hw ones
		   - update the hw view */
		req_payload.start_slot = cur_slots;
		if (vcpi) {
			port = container_of(vcpi, struct drm_dp_mst_port,
					    vcpi);

			/* Validated ports don't matter if we're releasing
			 * VCPI
			 */
			if (vcpi->num_slots) {
				port = drm_dp_mst_topology_get_port_validated(
				    mgr, port);
				if (!port) {
					mutex_unlock(&mgr->payload_lock);
					return -EINVAL;
				}
				put_port = true;
			}

			req_payload.num_slots = vcpi->num_slots;
			req_payload.vcpi = vcpi->vcpi;
		} else {
			port = NULL;
			req_payload.num_slots = 0;
		}

		payload->start_slot = req_payload.start_slot;
		/* work out what is required to happen with this payload */
		if (payload->num_slots != req_payload.num_slots) {

			/* need to push an update for this payload */
			if (req_payload.num_slots) {
				drm_dp_create_payload_step1(mgr, vcpi->vcpi,
							    &req_payload);
				payload->num_slots = req_payload.num_slots;
				payload->vcpi = req_payload.vcpi;

			} else if (payload->num_slots) {
				payload->num_slots = 0;
				drm_dp_destroy_payload_step1(mgr, port,
							     payload->vcpi,
							     payload);
				req_payload.payload_state =
					payload->payload_state;
				payload->start_slot = 0;
			}
			payload->payload_state = req_payload.payload_state;
		}
		cur_slots += req_payload.num_slots;

		if (put_port)
			drm_dp_mst_topology_put_port(port);
	}

	for (i = 0; i < mgr->max_payloads; i++) {
		if (mgr->payloads[i].payload_state != DP_PAYLOAD_DELETE_LOCAL)
			continue;

		DRM_DEBUG_KMS("removing payload %d\n", i);
		for (j = i; j < mgr->max_payloads - 1; j++) {
			mgr->payloads[j] = mgr->payloads[j + 1];
			mgr->proposed_vcpis[j] = mgr->proposed_vcpis[j + 1];

			if (mgr->proposed_vcpis[j] &&
			    mgr->proposed_vcpis[j]->num_slots) {
				set_bit(j + 1, &mgr->payload_mask);
			} else {
				clear_bit(j + 1, &mgr->payload_mask);
			}
		}

		memset(&mgr->payloads[mgr->max_payloads - 1], 0,
		       sizeof(struct drm_dp_payload));
		mgr->proposed_vcpis[mgr->max_payloads - 1] = NULL;
		clear_bit(mgr->max_payloads, &mgr->payload_mask);
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

static int drm_dp_send_dpcd_read(struct drm_dp_mst_topology_mgr *mgr,
				 struct drm_dp_mst_port *port,
				 int offset, int size, u8 *bytes)
{
	int len;
	int ret = 0;
	struct drm_dp_sideband_msg_tx *txmsg;
	struct drm_dp_mst_branch *mstb;

	mstb = drm_dp_mst_topology_get_mstb_validated(mgr, port->parent);
	if (!mstb)
		return -EINVAL;

	txmsg = kzalloc(sizeof(*txmsg), GFP_KERNEL);
	if (!txmsg) {
		ret = -ENOMEM;
		goto fail_put;
	}

	len = build_dpcd_read(txmsg, port->port_num, offset, size);
	txmsg->dst = port->parent;

	drm_dp_queue_down_tx(mgr, txmsg);

	ret = drm_dp_mst_wait_tx_reply(mstb, txmsg);
	if (ret < 0)
		goto fail_free;

	/* DPCD read should never be NACKed */
	if (txmsg->reply.reply_type == 1) {
		DRM_ERROR("mstb %p port %d: DPCD read on addr 0x%x for %d bytes NAKed\n",
			  mstb, port->port_num, offset, size);
		ret = -EIO;
		goto fail_free;
	}

	if (txmsg->reply.u.remote_dpcd_read_ack.num_bytes != size) {
		ret = -EPROTO;
		goto fail_free;
	}

	ret = min_t(size_t, txmsg->reply.u.remote_dpcd_read_ack.num_bytes,
		    size);
	memcpy(bytes, txmsg->reply.u.remote_dpcd_read_ack.bytes, ret);

fail_free:
	kfree(txmsg);
fail_put:
	drm_dp_mst_topology_put_mstb(mstb);

	return ret;
}

static int drm_dp_send_dpcd_write(struct drm_dp_mst_topology_mgr *mgr,
				  struct drm_dp_mst_port *port,
				  int offset, int size, u8 *bytes)
{
	int len;
	int ret;
	struct drm_dp_sideband_msg_tx *txmsg;
	struct drm_dp_mst_branch *mstb;

	mstb = drm_dp_mst_topology_get_mstb_validated(mgr, port->parent);
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
		if (txmsg->reply.reply_type == DP_SIDEBAND_REPLY_NAK)
			ret = -EIO;
		else
			ret = 0;
	}
	kfree(txmsg);
fail_put:
	drm_dp_mst_topology_put_mstb(mstb);
	return ret;
}

static int drm_dp_encode_up_ack_reply(struct drm_dp_sideband_msg_tx *msg, u8 req_type)
{
	struct drm_dp_sideband_msg_reply_body reply;

	reply.reply_type = DP_SIDEBAND_REPLY_ACK;
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

static int drm_dp_get_vc_payload_bw(u8 dp_link_bw, u8  dp_link_count)
{
	if (dp_link_bw == 0 || dp_link_count == 0)
		DRM_DEBUG_KMS("invalid link bandwidth in DPCD: %x (link count: %d)\n",
			      dp_link_bw, dp_link_count);

	return dp_link_bw * dp_link_count / 2;
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

		mgr->pbn_div = drm_dp_get_vc_payload_bw(mgr->dpcd[1],
							mgr->dpcd[2] & DP_MAX_LANE_COUNT_MASK);
		if (mgr->pbn_div == 0) {
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
		drm_dp_mst_topology_get_mstb(mgr->mst_primary);

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
		drm_dp_mst_topology_put_mstb(mstb);
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
	struct drm_dp_sideband_msg_tx *txmsg;
	struct drm_dp_mst_branch *mstb;
	struct drm_dp_sideband_msg_hdr *hdr = &mgr->down_rep_recv.initial_hdr;
	int slot = -1;

	if (!drm_dp_get_one_sb_msg(mgr, false))
		goto clear_down_rep_recv;

	if (!mgr->down_rep_recv.have_eomt)
		return 0;

	mstb = drm_dp_get_mst_branch_device(mgr, hdr->lct, hdr->rad);
	if (!mstb) {
		DRM_DEBUG_KMS("Got MST reply from unknown device %d\n",
			      hdr->lct);
		goto clear_down_rep_recv;
	}

	/* find the message */
	slot = hdr->seqno;
	mutex_lock(&mgr->qlock);
	txmsg = mstb->tx_slots[slot];
	/* remove from slots */
	mutex_unlock(&mgr->qlock);

	if (!txmsg) {
		DRM_DEBUG_KMS("Got MST reply with no msg %p %d %d %02x %02x\n",
			      mstb, hdr->seqno, hdr->lct, hdr->rad[0],
			      mgr->down_rep_recv.msg[0]);
		goto no_msg;
	}

	drm_dp_sideband_parse_reply(&mgr->down_rep_recv, &txmsg->reply);

	if (txmsg->reply.reply_type == DP_SIDEBAND_REPLY_NAK)
		DRM_DEBUG_KMS("Got NAK reply: req 0x%02x (%s), reason 0x%02x (%s), nak data 0x%02x\n",
			      txmsg->reply.req_type,
			      drm_dp_mst_req_type_str(txmsg->reply.req_type),
			      txmsg->reply.u.nak.reason,
			      drm_dp_mst_nak_reason_str(txmsg->reply.u.nak.reason),
			      txmsg->reply.u.nak.nak_data);

	memset(&mgr->down_rep_recv, 0, sizeof(struct drm_dp_sideband_msg_rx));
	drm_dp_mst_topology_put_mstb(mstb);

	mutex_lock(&mgr->qlock);
	txmsg->state = DRM_DP_SIDEBAND_TX_RX;
	mstb->tx_slots[slot] = NULL;
	mutex_unlock(&mgr->qlock);

	wake_up_all(&mgr->tx_waitq);

	return 0;

no_msg:
	drm_dp_mst_topology_put_mstb(mstb);
clear_down_rep_recv:
	memset(&mgr->down_rep_recv, 0, sizeof(struct drm_dp_sideband_msg_rx));

	return 0;
}

static int drm_dp_mst_handle_up_req(struct drm_dp_mst_topology_mgr *mgr)
{
	struct drm_dp_sideband_msg_req_body msg;
	struct drm_dp_sideband_msg_hdr *hdr = &mgr->up_req_recv.initial_hdr;
	struct drm_dp_mst_branch *mstb = NULL;
	const u8 *guid;
	bool seqno;

	if (!drm_dp_get_one_sb_msg(mgr, true))
		goto out;

	if (!mgr->up_req_recv.have_eomt)
		return 0;

	if (!hdr->broadcast) {
		mstb = drm_dp_get_mst_branch_device(mgr, hdr->lct, hdr->rad);
		if (!mstb) {
			DRM_DEBUG_KMS("Got MST reply from unknown device %d\n",
				      hdr->lct);
			goto out;
		}
	}

	seqno = hdr->seqno;
	drm_dp_sideband_parse_req(&mgr->up_req_recv, &msg);

	if (msg.req_type == DP_CONNECTION_STATUS_NOTIFY)
		guid = msg.u.conn_stat.guid;
	else if (msg.req_type == DP_RESOURCE_STATUS_NOTIFY)
		guid = msg.u.resource_stat.guid;
	else
		goto out;

	drm_dp_send_up_ack_reply(mgr, mgr->mst_primary, msg.req_type, seqno,
				 false);

	if (!mstb) {
		mstb = drm_dp_get_mst_branch_device_by_guid(mgr, guid);
		if (!mstb) {
			DRM_DEBUG_KMS("Got MST reply from unknown device %d\n",
				      hdr->lct);
			goto out;
		}
	}

	if (msg.req_type == DP_CONNECTION_STATUS_NOTIFY) {
		drm_dp_mst_handle_conn_stat(mstb, &msg.u.conn_stat);

		DRM_DEBUG_KMS("Got CSN: pn: %d ldps:%d ddps: %d mcs: %d ip: %d pdt: %d\n",
			      msg.u.conn_stat.port_number,
			      msg.u.conn_stat.legacy_device_plug_status,
			      msg.u.conn_stat.displayport_device_plug_status,
			      msg.u.conn_stat.message_capability_status,
			      msg.u.conn_stat.input_port,
			      msg.u.conn_stat.peer_device_type);

		drm_kms_helper_hotplug_event(mgr->dev);
	} else if (msg.req_type == DP_RESOURCE_STATUS_NOTIFY) {
		DRM_DEBUG_KMS("Got RSN: pn: %d avail_pbn %d\n",
			      msg.u.resource_stat.port_number,
			      msg.u.resource_stat.available_pbn);
	}

	drm_dp_mst_topology_put_mstb(mstb);
out:
	memset(&mgr->up_req_recv, 0, sizeof(struct drm_dp_sideband_msg_rx));
	return 0;
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

	/* we need to search for the port in the mgr in case it's gone */
	port = drm_dp_mst_topology_get_port_validated(mgr, port);
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
	drm_dp_mst_topology_put_port(port);
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

	port = drm_dp_mst_topology_get_port_validated(mgr, port);
	if (!port)
		return ret;
	ret = port->has_audio;
	drm_dp_mst_topology_put_port(port);
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

	/* we need to search for the port in the mgr in case it's gone */
	port = drm_dp_mst_topology_get_port_validated(mgr, port);
	if (!port)
		return NULL;

	if (port->cached_edid)
		edid = drm_edid_duplicate(port->cached_edid);
	else {
		edid = drm_get_edid(connector, &port->aux.ddc);
	}
	port->has_audio = drm_detect_monitor_audio(edid);
	drm_dp_mst_topology_put_port(port);
	return edid;
}
EXPORT_SYMBOL(drm_dp_mst_get_edid);

/**
 * drm_dp_find_vcpi_slots() - Find VCPI slots for this PBN value
 * @mgr: manager to use
 * @pbn: payload bandwidth to convert into slots.
 *
 * Calculate the number of VCPI slots that will be required for the given PBN
 * value. This function is deprecated, and should not be used in atomic
 * drivers.
 *
 * RETURNS:
 * The total slots required for this port, or error.
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
 * drm_dp_atomic_find_vcpi_slots() - Find and add VCPI slots to the state
 * @state: global atomic state
 * @mgr: MST topology manager for the port
 * @port: port to find vcpi slots for
 * @pbn: bandwidth required for the mode in PBN
 *
 * Allocates VCPI slots to @port, replacing any previous VCPI allocations it
 * may have had. Any atomic drivers which support MST must call this function
 * in their &drm_encoder_helper_funcs.atomic_check() callback to change the
 * current VCPI allocation for the new state, but only when
 * &drm_crtc_state.mode_changed or &drm_crtc_state.connectors_changed is set
 * to ensure compatibility with userspace applications that still use the
 * legacy modesetting UAPI.
 *
 * Allocations set by this function are not checked against the bandwidth
 * restraints of @mgr until the driver calls drm_dp_mst_atomic_check().
 *
 * Additionally, it is OK to call this function multiple times on the same
 * @port as needed. It is not OK however, to call this function and
 * drm_dp_atomic_release_vcpi_slots() in the same atomic check phase.
 *
 * See also:
 * drm_dp_atomic_release_vcpi_slots()
 * drm_dp_mst_atomic_check()
 *
 * Returns:
 * Total slots in the atomic state assigned for this port, or a negative error
 * code if the port no longer exists
 */
int drm_dp_atomic_find_vcpi_slots(struct drm_atomic_state *state,
				  struct drm_dp_mst_topology_mgr *mgr,
				  struct drm_dp_mst_port *port, int pbn)
{
	struct drm_dp_mst_topology_state *topology_state;
	struct drm_dp_vcpi_allocation *pos, *vcpi = NULL;
	int prev_slots, req_slots;

	topology_state = drm_atomic_get_mst_topology_state(state, mgr);
	if (IS_ERR(topology_state))
		return PTR_ERR(topology_state);

	/* Find the current allocation for this port, if any */
	list_for_each_entry(pos, &topology_state->vcpis, next) {
		if (pos->port == port) {
			vcpi = pos;
			prev_slots = vcpi->vcpi;

			/*
			 * This should never happen, unless the driver tries
			 * releasing and allocating the same VCPI allocation,
			 * which is an error
			 */
			if (WARN_ON(!prev_slots)) {
				DRM_ERROR("cannot allocate and release VCPI on [MST PORT:%p] in the same state\n",
					  port);
				return -EINVAL;
			}

			break;
		}
	}
	if (!vcpi)
		prev_slots = 0;

	req_slots = DIV_ROUND_UP(pbn, mgr->pbn_div);

	DRM_DEBUG_ATOMIC("[CONNECTOR:%d:%s] [MST PORT:%p] VCPI %d -> %d\n",
			 port->connector->base.id, port->connector->name,
			 port, prev_slots, req_slots);

	/* Add the new allocation to the state */
	if (!vcpi) {
		vcpi = kzalloc(sizeof(*vcpi), GFP_KERNEL);
		if (!vcpi)
			return -ENOMEM;

		drm_dp_mst_get_port_malloc(port);
		vcpi->port = port;
		list_add(&vcpi->next, &topology_state->vcpis);
	}
	vcpi->vcpi = req_slots;

	return req_slots;
}
EXPORT_SYMBOL(drm_dp_atomic_find_vcpi_slots);

/**
 * drm_dp_atomic_release_vcpi_slots() - Release allocated vcpi slots
 * @state: global atomic state
 * @mgr: MST topology manager for the port
 * @port: The port to release the VCPI slots from
 *
 * Releases any VCPI slots that have been allocated to a port in the atomic
 * state. Any atomic drivers which support MST must call this function in
 * their &drm_connector_helper_funcs.atomic_check() callback when the
 * connector will no longer have VCPI allocated (e.g. because its CRTC was
 * removed) when it had VCPI allocated in the previous atomic state.
 *
 * It is OK to call this even if @port has been removed from the system.
 * Additionally, it is OK to call this function multiple times on the same
 * @port as needed. It is not OK however, to call this function and
 * drm_dp_atomic_find_vcpi_slots() on the same @port in a single atomic check
 * phase.
 *
 * See also:
 * drm_dp_atomic_find_vcpi_slots()
 * drm_dp_mst_atomic_check()
 *
 * Returns:
 * 0 if all slots for this port were added back to
 * &drm_dp_mst_topology_state.avail_slots or negative error code
 */
int drm_dp_atomic_release_vcpi_slots(struct drm_atomic_state *state,
				     struct drm_dp_mst_topology_mgr *mgr,
				     struct drm_dp_mst_port *port)
{
	struct drm_dp_mst_topology_state *topology_state;
	struct drm_dp_vcpi_allocation *pos;
	bool found = false;

	topology_state = drm_atomic_get_mst_topology_state(state, mgr);
	if (IS_ERR(topology_state))
		return PTR_ERR(topology_state);

	list_for_each_entry(pos, &topology_state->vcpis, next) {
		if (pos->port == port) {
			found = true;
			break;
		}
	}
	if (WARN_ON(!found)) {
		DRM_ERROR("no VCPI for [MST PORT:%p] found in mst state %p\n",
			  port, &topology_state->base);
		return -EINVAL;
	}

	DRM_DEBUG_ATOMIC("[MST PORT:%p] VCPI %d -> 0\n", port, pos->vcpi);
	if (pos->vcpi) {
		drm_dp_mst_put_port_malloc(port);
		pos->vcpi = 0;
	}

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

	port = drm_dp_mst_topology_get_port_validated(mgr, port);
	if (!port)
		return false;

	if (slots < 0)
		return false;

	if (port->vcpi.vcpi > 0) {
		DRM_DEBUG_KMS("payload: vcpi %d already allocated for pbn %d - requested pbn %d\n",
			      port->vcpi.vcpi, port->vcpi.pbn, pbn);
		if (pbn == port->vcpi.pbn) {
			drm_dp_mst_topology_put_port(port);
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

	/* Keep port allocated until its payload has been removed */
	drm_dp_mst_get_port_malloc(port);
	drm_dp_mst_topology_put_port(port);
	return true;
out:
	return false;
}
EXPORT_SYMBOL(drm_dp_mst_allocate_vcpi);

int drm_dp_mst_get_vcpi_slots(struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port)
{
	int slots = 0;
	port = drm_dp_mst_topology_get_port_validated(mgr, port);
	if (!port)
		return slots;

	slots = port->vcpi.num_slots;
	drm_dp_mst_topology_put_port(port);
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
	/*
	 * A port with VCPI will remain allocated until its VCPI is
	 * released, no verified ref needed
	 */

	port->vcpi.num_slots = 0;
}
EXPORT_SYMBOL(drm_dp_mst_reset_vcpi_slots);

/**
 * drm_dp_mst_deallocate_vcpi() - deallocate a VCPI
 * @mgr: manager for this port
 * @port: port to deallocate vcpi for
 *
 * This can be called unconditionally, regardless of whether
 * drm_dp_mst_allocate_vcpi() succeeded or not.
 */
void drm_dp_mst_deallocate_vcpi(struct drm_dp_mst_topology_mgr *mgr,
				struct drm_dp_mst_port *port)
{
	if (!port->vcpi.vcpi)
		return;

	drm_dp_mst_put_payload_id(mgr, port->vcpi.vcpi);
	port->vcpi.num_slots = 0;
	port->vcpi.pbn = 0;
	port->vcpi.aligned_pbn = 0;
	port->vcpi.vcpi = 0;
	drm_dp_mst_put_port_malloc(port);
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
	return DIV_ROUND_UP_ULL(mul_u32_u32(clock * bpp, 64 * 1006),
				8 * 54 * 1000 * 1000);
}
EXPORT_SYMBOL(drm_dp_calc_pbn_mode);

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

		mgr->cbs->destroy_connector(mgr, port->connector);

		drm_dp_port_teardown_pdt(port, port->pdt);
		port->pdt = DP_PEER_DEVICE_NONE;

		drm_dp_mst_put_port_malloc(port);
		send_hotplug = true;
	}
	if (send_hotplug)
		drm_kms_helper_hotplug_event(mgr->dev);
}

static struct drm_private_state *
drm_dp_mst_duplicate_state(struct drm_private_obj *obj)
{
	struct drm_dp_mst_topology_state *state, *old_state =
		to_dp_mst_topology_state(obj->state);
	struct drm_dp_vcpi_allocation *pos, *vcpi;

	state = kmemdup(old_state, sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_private_obj_duplicate_state(obj, &state->base);

	INIT_LIST_HEAD(&state->vcpis);

	list_for_each_entry(pos, &old_state->vcpis, next) {
		/* Prune leftover freed VCPI allocations */
		if (!pos->vcpi)
			continue;

		vcpi = kmemdup(pos, sizeof(*vcpi), GFP_KERNEL);
		if (!vcpi)
			goto fail;

		drm_dp_mst_get_port_malloc(vcpi->port);
		list_add(&vcpi->next, &state->vcpis);
	}

	return &state->base;

fail:
	list_for_each_entry_safe(pos, vcpi, &state->vcpis, next) {
		drm_dp_mst_put_port_malloc(pos->port);
		kfree(pos);
	}
	kfree(state);

	return NULL;
}

static void drm_dp_mst_destroy_state(struct drm_private_obj *obj,
				     struct drm_private_state *state)
{
	struct drm_dp_mst_topology_state *mst_state =
		to_dp_mst_topology_state(state);
	struct drm_dp_vcpi_allocation *pos, *tmp;

	list_for_each_entry_safe(pos, tmp, &mst_state->vcpis, next) {
		/* We only keep references to ports with non-zero VCPIs */
		if (pos->vcpi)
			drm_dp_mst_put_port_malloc(pos->port);
		kfree(pos);
	}

	kfree(mst_state);
}

static inline int
drm_dp_mst_atomic_check_topology_state(struct drm_dp_mst_topology_mgr *mgr,
				       struct drm_dp_mst_topology_state *mst_state)
{
	struct drm_dp_vcpi_allocation *vcpi;
	int avail_slots = 63, payload_count = 0;

	list_for_each_entry(vcpi, &mst_state->vcpis, next) {
		/* Releasing VCPI is always OK-even if the port is gone */
		if (!vcpi->vcpi) {
			DRM_DEBUG_ATOMIC("[MST PORT:%p] releases all VCPI slots\n",
					 vcpi->port);
			continue;
		}

		DRM_DEBUG_ATOMIC("[MST PORT:%p] requires %d vcpi slots\n",
				 vcpi->port, vcpi->vcpi);

		avail_slots -= vcpi->vcpi;
		if (avail_slots < 0) {
			DRM_DEBUG_ATOMIC("[MST PORT:%p] not enough VCPI slots in mst state %p (avail=%d)\n",
					 vcpi->port, mst_state,
					 avail_slots + vcpi->vcpi);
			return -ENOSPC;
		}

		if (++payload_count > mgr->max_payloads) {
			DRM_DEBUG_ATOMIC("[MST MGR:%p] state %p has too many payloads (max=%d)\n",
					 mgr, mst_state, mgr->max_payloads);
			return -EINVAL;
		}
	}
	DRM_DEBUG_ATOMIC("[MST MGR:%p] mst state %p VCPI avail=%d used=%d\n",
			 mgr, mst_state, avail_slots,
			 63 - avail_slots);

	return 0;
}

/**
 * drm_dp_mst_atomic_check - Check that the new state of an MST topology in an
 * atomic update is valid
 * @state: Pointer to the new &struct drm_dp_mst_topology_state
 *
 * Checks the given topology state for an atomic update to ensure that it's
 * valid. This includes checking whether there's enough bandwidth to support
 * the new VCPI allocations in the atomic update.
 *
 * Any atomic drivers supporting DP MST must make sure to call this after
 * checking the rest of their state in their
 * &drm_mode_config_funcs.atomic_check() callback.
 *
 * See also:
 * drm_dp_atomic_find_vcpi_slots()
 * drm_dp_atomic_release_vcpi_slots()
 *
 * Returns:
 *
 * 0 if the new state is valid, negative error code otherwise.
 */
int drm_dp_mst_atomic_check(struct drm_atomic_state *state)
{
	struct drm_dp_mst_topology_mgr *mgr;
	struct drm_dp_mst_topology_state *mst_state;
	int i, ret = 0;

	for_each_new_mst_mgr_in_state(state, mgr, mst_state, i) {
		ret = drm_dp_mst_atomic_check_topology_state(mgr, mst_state);
		if (ret)
			break;
	}

	return ret;
}
EXPORT_SYMBOL(drm_dp_mst_atomic_check);

const struct drm_private_state_funcs drm_dp_mst_topology_state_funcs = {
	.atomic_duplicate_state = drm_dp_mst_duplicate_state,
	.atomic_destroy_state = drm_dp_mst_destroy_state,
};
EXPORT_SYMBOL(drm_dp_mst_topology_state_funcs);

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

	mst_state = kzalloc(sizeof(*mst_state), GFP_KERNEL);
	if (mst_state == NULL)
		return -ENOMEM;

	mst_state->mgr = mgr;
	INIT_LIST_HEAD(&mst_state->vcpis);

	drm_atomic_private_obj_init(dev, &mgr->base,
				    &mst_state->base,
				    &drm_dp_mst_topology_state_funcs);

	return 0;
}
EXPORT_SYMBOL(drm_dp_mst_topology_mgr_init);

/**
 * drm_dp_mst_topology_mgr_destroy() - destroy topology manager.
 * @mgr: manager to destroy
 */
void drm_dp_mst_topology_mgr_destroy(struct drm_dp_mst_topology_mgr *mgr)
{
	drm_dp_mst_topology_mgr_set_mst(mgr, false);
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

	mutex_destroy(&mgr->destroy_connector_lock);
	mutex_destroy(&mgr->payload_lock);
	mutex_destroy(&mgr->qlock);
	mutex_destroy(&mgr->lock);
}
EXPORT_SYMBOL(drm_dp_mst_topology_mgr_destroy);

static bool remote_i2c_read_ok(const struct i2c_msg msgs[], int num)
{
	int i;

	if (num - 1 > DP_REMOTE_I2C_READ_MAX_TRANSACTIONS)
		return false;

	for (i = 0; i < num - 1; i++) {
		if (msgs[i].flags & I2C_M_RD ||
		    msgs[i].len > 0xff)
			return false;
	}

	return msgs[num - 1].flags & I2C_M_RD &&
		msgs[num - 1].len <= 0xff;
}

/* I2C device */
static int drm_dp_mst_i2c_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs,
			       int num)
{
	struct drm_dp_aux *aux = adapter->algo_data;
	struct drm_dp_mst_port *port = container_of(aux, struct drm_dp_mst_port, aux);
	struct drm_dp_mst_branch *mstb;
	struct drm_dp_mst_topology_mgr *mgr = port->mgr;
	unsigned int i;
	struct drm_dp_sideband_msg_req_body msg;
	struct drm_dp_sideband_msg_tx *txmsg = NULL;
	int ret;

	mstb = drm_dp_mst_topology_get_mstb_validated(mgr, port->parent);
	if (!mstb)
		return -EREMOTEIO;

	if (!remote_i2c_read_ok(msgs, num)) {
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
		msg.u.i2c_read.transactions[i].no_stop_bit = !(msgs[i].flags & I2C_M_STOP);
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

		if (txmsg->reply.reply_type == DP_SIDEBAND_REPLY_NAK) {
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
	drm_dp_mst_topology_put_mstb(mstb);
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
