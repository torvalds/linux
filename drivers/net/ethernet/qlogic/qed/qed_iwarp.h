/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
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
#ifndef _QED_IWARP_H
#define _QED_IWARP_H

enum qed_iwarp_qp_state {
	QED_IWARP_QP_STATE_IDLE,
	QED_IWARP_QP_STATE_RTS,
	QED_IWARP_QP_STATE_TERMINATE,
	QED_IWARP_QP_STATE_CLOSING,
	QED_IWARP_QP_STATE_ERROR,
};

enum qed_iwarp_qp_state qed_roce2iwarp_state(enum qed_roce_qp_state state);

#define QED_IWARP_PREALLOC_CNT  (256)

#define QED_IWARP_LL2_SYN_TX_SIZE       (128)
#define QED_IWARP_LL2_SYN_RX_SIZE       (256)

#define QED_IWARP_LL2_OOO_DEF_TX_SIZE   (256)
#define QED_IWARP_MAX_OOO		(16)
#define QED_IWARP_LL2_OOO_MAX_RX_SIZE   (16384)

#define QED_IWARP_HANDLE_INVAL		(0xff)

struct qed_iwarp_ll2_buff {
	struct qed_iwarp_ll2_buff *piggy_buf;
	void *data;
	dma_addr_t data_phys_addr;
	u32 buff_size;
};

struct qed_iwarp_ll2_mpa_buf {
	struct list_head list_entry;
	struct qed_iwarp_ll2_buff *ll2_buf;
	struct unaligned_opaque_data data;
	u16 tcp_payload_len;
	u8 placement_offset;
};

/* In some cases a fpdu will arrive with only one byte of the header, in this
 * case the fpdu_length will be partial (contain only higher byte and
 * incomplete bytes will contain the invalid value
 */
#define QED_IWARP_INVALID_INCOMPLETE_BYTES 0xffff

struct qed_iwarp_fpdu {
	struct qed_iwarp_ll2_buff *mpa_buf;
	void *mpa_frag_virt;
	dma_addr_t mpa_frag;
	dma_addr_t pkt_hdr;
	u16 mpa_frag_len;
	u16 fpdu_length;
	u16 incomplete_bytes;
	u8 pkt_hdr_size;
};

struct qed_iwarp_info {
	struct list_head listen_list;	/* qed_iwarp_listener */
	struct list_head ep_list;	/* qed_iwarp_ep */
	struct list_head ep_free_list;	/* pre-allocated ep's */
	struct list_head mpa_buf_list;	/* list of mpa_bufs */
	struct list_head mpa_buf_pending_list;
	spinlock_t iw_lock;	/* for iwarp resources */
	spinlock_t qp_lock;	/* for teardown races */
	u32 rcv_wnd_scale;
	u16 rcv_wnd_size;
	u16 max_mtu;
	u8 mac_addr[ETH_ALEN];
	u8 crc_needed;
	u8 tcp_flags;
	u8 ll2_syn_handle;
	u8 ll2_ooo_handle;
	u8 ll2_mpa_handle;
	u8 peer2peer;
	enum mpa_negotiation_mode mpa_rev;
	enum mpa_rtr_type rtr_type;
	struct qed_iwarp_fpdu *partial_fpdus;
	struct qed_iwarp_ll2_mpa_buf *mpa_bufs;
	u8 *mpa_intermediate_buf;
	u16 max_num_partial_fpdus;
};

enum qed_iwarp_ep_state {
	QED_IWARP_EP_INIT,
	QED_IWARP_EP_MPA_REQ_RCVD,
	QED_IWARP_EP_MPA_OFFLOADED,
	QED_IWARP_EP_ESTABLISHED,
	QED_IWARP_EP_CLOSED
};

union async_output {
	struct iwarp_eqe_data_mpa_async_completion mpa_response;
	struct iwarp_eqe_data_tcp_async_completion mpa_request;
};

#define QED_MAX_PRIV_DATA_LEN (512)
struct qed_iwarp_ep_memory {
	u8 in_pdata[QED_MAX_PRIV_DATA_LEN];
	u8 out_pdata[QED_MAX_PRIV_DATA_LEN];
	union async_output async_output;
};

/* Endpoint structure represents a TCP connection. This connection can be
 * associated with a QP or not (in which case QP==NULL)
 */
struct qed_iwarp_ep {
	struct list_head list_entry;
	struct qed_rdma_qp *qp;
	struct qed_iwarp_ep_memory *ep_buffer_virt;
	dma_addr_t ep_buffer_phys;
	enum qed_iwarp_ep_state state;
	int sig;
	struct qed_iwarp_cm_info cm_info;
	enum tcp_connect_mode connect_mode;
	enum mpa_rtr_type rtr_type;
	enum mpa_negotiation_mode mpa_rev;
	u32 tcp_cid;
	u32 cid;
	u16 mss;
	u8 remote_mac_addr[6];
	u8 local_mac_addr[6];
	bool mpa_reply_processed;

	/* For Passive side - syn packet related data */
	u16 syn_ip_payload_length;
	struct qed_iwarp_ll2_buff *syn;
	dma_addr_t syn_phy_addr;

	/* The event_cb function is called for asynchrounous events associated
	 * with the ep. It is initialized at different entry points depending
	 * on whether the ep is the tcp connection active side or passive side
	 * The cb_context is passed to the event_cb function.
	 */
	iwarp_event_handler event_cb;
	void *cb_context;
};

struct qed_iwarp_listener {
	struct list_head list_entry;

	/* The event_cb function is called for connection requests.
	 * The cb_context is passed to the event_cb function.
	 */
	iwarp_event_handler event_cb;
	void *cb_context;
	u32 max_backlog;
	u32 ip_addr[4];
	u16 port;
	u16 vlan;
	u8 ip_version;
};

int qed_iwarp_alloc(struct qed_hwfn *p_hwfn);

int qed_iwarp_setup(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
		    struct qed_rdma_start_in_params *params);

void qed_iwarp_init_fw_ramrod(struct qed_hwfn *p_hwfn,
			      struct iwarp_init_func_ramrod_data *p_ramrod);

int qed_iwarp_stop(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

void qed_iwarp_resc_free(struct qed_hwfn *p_hwfn);

void qed_iwarp_init_devinfo(struct qed_hwfn *p_hwfn);

void qed_iwarp_init_hw(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

int qed_iwarp_create_qp(struct qed_hwfn *p_hwfn,
			struct qed_rdma_qp *qp,
			struct qed_rdma_create_qp_out_params *out_params);

int qed_iwarp_modify_qp(struct qed_hwfn *p_hwfn, struct qed_rdma_qp *qp,
			enum qed_iwarp_qp_state new_state, bool internal);

int qed_iwarp_destroy_qp(struct qed_hwfn *p_hwfn, struct qed_rdma_qp *qp);

int qed_iwarp_fw_destroy(struct qed_hwfn *p_hwfn, struct qed_rdma_qp *qp);

void qed_iwarp_query_qp(struct qed_rdma_qp *qp,
			struct qed_rdma_query_qp_out_params *out_params);

int
qed_iwarp_connect(void *rdma_cxt,
		  struct qed_iwarp_connect_in *iparams,
		  struct qed_iwarp_connect_out *oparams);

int
qed_iwarp_create_listen(void *rdma_cxt,
			struct qed_iwarp_listen_in *iparams,
			struct qed_iwarp_listen_out *oparams);

int qed_iwarp_accept(void *rdma_cxt, struct qed_iwarp_accept_in *iparams);

int qed_iwarp_reject(void *rdma_cxt, struct qed_iwarp_reject_in *iparams);
int qed_iwarp_destroy_listen(void *rdma_cxt, void *handle);

int qed_iwarp_send_rtr(void *rdma_cxt, struct qed_iwarp_send_rtr_in *iparams);

#endif
