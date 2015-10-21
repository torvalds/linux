/*
 * Copyright 2014 Cisco Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _VNIC_STATS_H_
#define _VNIC_STATS_H_

/* Tx statistics */
struct vnic_tx_stats {
	u64 tx_frames_ok;
	u64 tx_unicast_frames_ok;
	u64 tx_multicast_frames_ok;
	u64 tx_broadcast_frames_ok;
	u64 tx_bytes_ok;
	u64 tx_unicast_bytes_ok;
	u64 tx_multicast_bytes_ok;
	u64 tx_broadcast_bytes_ok;
	u64 tx_drops;
	u64 tx_errors;
	u64 tx_tso;
	u64 rsvd[16];
};

/* Rx statistics */
struct vnic_rx_stats {
	u64 rx_frames_ok;
	u64 rx_frames_total;
	u64 rx_unicast_frames_ok;
	u64 rx_multicast_frames_ok;
	u64 rx_broadcast_frames_ok;
	u64 rx_bytes_ok;
	u64 rx_unicast_bytes_ok;
	u64 rx_multicast_bytes_ok;
	u64 rx_broadcast_bytes_ok;
	u64 rx_drop;
	u64 rx_no_bufs;
	u64 rx_errors;
	u64 rx_rss;
	u64 rx_crc_errors;
	u64 rx_frames_64;
	u64 rx_frames_127;
	u64 rx_frames_255;
	u64 rx_frames_511;
	u64 rx_frames_1023;
	u64 rx_frames_1518;
	u64 rx_frames_to_max;
	u64 rsvd[16];
};

struct vnic_stats {
	struct vnic_tx_stats tx;
	struct vnic_rx_stats rx;
};

#endif /* _VNIC_STATS_H_ */
