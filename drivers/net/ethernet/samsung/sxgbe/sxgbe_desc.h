/* SPDX-License-Identifier: GPL-2.0-only */
/* 10G controller driver for Samsung SoCs
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Siva Reddy Kallam <siva.kallam@samsung.com>
 */
#ifndef __SXGBE_DESC_H__
#define __SXGBE_DESC_H__

#define SXGBE_DESC_SIZE_BYTES	16

/* forward declaration */
struct sxgbe_extra_stats;

/* Transmit checksum insertion control */
enum tdes_csum_insertion {
	cic_disabled		= 0,	/* Checksum Insertion Control */
	cic_only_ip		= 1,	/* Only IP header */
	/* IP header but pseudoheader is not calculated */
	cic_no_pseudoheader	= 2,
	cic_full		= 3,	/* IP header and pseudoheader */
};

struct sxgbe_tx_norm_desc {
	u64 tdes01; /* buf1 address */
	union {
		/* TX Read-Format Desc 2,3 */
		struct {
			/* TDES2 */
			u32 buf1_size:14;
			u32 vlan_tag_ctl:2;
			u32 buf2_size:14;
			u32 timestmp_enable:1;
			u32 int_on_com:1;
			/* TDES3 */
			union {
				u16 tcp_payload_len;
				struct {
					u32 total_pkt_len:15;
					u32 reserved1:1;
				} pkt_len;
			} tx_pkt_len;

			u16 cksum_ctl:2;
			u16 tse_bit:1;
			u16 tcp_hdr_len:4;
			u16 sa_insert_ctl:3;
			u16 crc_pad_ctl:2;
			u16 last_desc:1;
			u16 first_desc:1;
			u16 ctxt_bit:1;
			u16 own_bit:1;
		} tx_rd_des23;

		/* tx write back Desc 2,3 */
		struct {
			/* WB TES2 */
			u32 reserved1;
			/* WB TES3 */
			u32 reserved2:31;
			u32 own_bit:1;
		} tx_wb_des23;
	} tdes23;
};

struct sxgbe_rx_norm_desc {
	union {
		u64 rdes01; /* buf1 address */
		union {
			u32 out_vlan_tag:16;
			u32 in_vlan_tag:16;
			u32 rss_hash;
		} rx_wb_des01;
	} rdes01;

	union {
		/* RX Read format Desc 2,3 */
		struct{
			/* RDES2 */
			u64 buf2_addr:62;
			/* RDES3 */
			u32 int_on_com:1;
			u32 own_bit:1;
		} rx_rd_des23;

		/* RX write back */
		struct{
			/* WB RDES2 */
			u32 hdr_len:10;
			u32 rdes2_reserved:2;
			u32 elrd_val:1;
			u32 iovt_sel:1;
			u32 res_pkt:1;
			u32 vlan_filter_match:1;
			u32 sa_filter_fail:1;
			u32 da_filter_fail:1;
			u32 hash_filter_pass:1;
			u32 macaddr_filter_match:8;
			u32 l3_filter_match:1;
			u32 l4_filter_match:1;
			u32 l34_filter_num:3;

			/* WB RDES3 */
			u32 pkt_len:14;
			u32 rdes3_reserved:1;
			u32 err_summary:1;
			u32 err_l2_type:4;
			u32 layer34_pkt_type:4;
			u32 no_coagulation_pkt:1;
			u32 in_seq_pkt:1;
			u32 rss_valid:1;
			u32 context_des_avail:1;
			u32 last_desc:1;
			u32 first_desc:1;
			u32 recv_context_desc:1;
			u32 own_bit:1;
		} rx_wb_des23;
	} rdes23;
};

/* Context descriptor structure */
struct sxgbe_tx_ctxt_desc {
	u32 tstamp_lo;
	u32 tstamp_hi;
	u32 maxseg_size:15;
	u32 reserved1:1;
	u32 ivlan_tag:16;
	u32 vlan_tag:16;
	u32 vltag_valid:1;
	u32 ivlan_tag_valid:1;
	u32 ivlan_tag_ctl:2;
	u32 reserved2:3;
	u32 ctxt_desc_err:1;
	u32 reserved3:2;
	u32 ostc:1;
	u32 tcmssv:1;
	u32 reserved4:2;
	u32 ctxt_bit:1;
	u32 own_bit:1;
};

struct sxgbe_rx_ctxt_desc {
	u32 tstamp_lo;
	u32 tstamp_hi;
	u32 reserved1;
	u32 ptp_msgtype:4;
	u32 tstamp_available:1;
	u32 ptp_rsp_err:1;
	u32 tstamp_dropped:1;
	u32 reserved2:23;
	u32 rx_ctxt_desc:1;
	u32 own_bit:1;
};

struct sxgbe_desc_ops {
	/* DMA TX descriptor ring initialization */
	void (*init_tx_desc)(struct sxgbe_tx_norm_desc *p);

	/* Invoked by the xmit function to prepare the tx descriptor */
	void (*tx_desc_enable_tse)(struct sxgbe_tx_norm_desc *p, u8 is_tse,
				   u32 total_hdr_len, u32 tcp_hdr_len,
				   u32 tcp_payload_len);

	/* Assign buffer lengths for descriptor */
	void (*prepare_tx_desc)(struct sxgbe_tx_norm_desc *p, u8 is_fd,
				int buf1_len, int pkt_len, int cksum);

	/* Set VLAN control information */
	void (*tx_vlanctl_desc)(struct sxgbe_tx_norm_desc *p, int vlan_ctl);

	/* Set the owner of the descriptor */
	void (*set_tx_owner)(struct sxgbe_tx_norm_desc *p);

	/* Get the owner of the descriptor */
	int (*get_tx_owner)(struct sxgbe_tx_norm_desc *p);

	/* Invoked by the xmit function to close the tx descriptor */
	void (*close_tx_desc)(struct sxgbe_tx_norm_desc *p);

	/* Clean the tx descriptor as soon as the tx irq is received */
	void (*release_tx_desc)(struct sxgbe_tx_norm_desc *p);

	/* Clear interrupt on tx frame completion. When this bit is
	 * set an interrupt happens as soon as the frame is transmitted
	 */
	void (*clear_tx_ic)(struct sxgbe_tx_norm_desc *p);

	/* Last tx segment reports the transmit status */
	int (*get_tx_ls)(struct sxgbe_tx_norm_desc *p);

	/* Get the buffer size from the descriptor */
	int (*get_tx_len)(struct sxgbe_tx_norm_desc *p);

	/* Set tx timestamp enable bit */
	void (*tx_enable_tstamp)(struct sxgbe_tx_norm_desc *p);

	/* get tx timestamp status */
	int (*get_tx_timestamp_status)(struct sxgbe_tx_norm_desc *p);

	/* TX Context Descripto Specific */
	void (*tx_ctxt_desc_set_ctxt)(struct sxgbe_tx_ctxt_desc *p);

	/* Set the owner of the TX context descriptor */
	void (*tx_ctxt_desc_set_owner)(struct sxgbe_tx_ctxt_desc *p);

	/* Get the owner of the TX context descriptor */
	int (*get_tx_ctxt_owner)(struct sxgbe_tx_ctxt_desc *p);

	/* Set TX mss */
	void (*tx_ctxt_desc_set_mss)(struct sxgbe_tx_ctxt_desc *p, u16 mss);

	/* Set TX mss */
	int (*tx_ctxt_desc_get_mss)(struct sxgbe_tx_ctxt_desc *p);

	/* Set TX tcmssv */
	void (*tx_ctxt_desc_set_tcmssv)(struct sxgbe_tx_ctxt_desc *p);

	/* Reset TX ostc */
	void (*tx_ctxt_desc_reset_ostc)(struct sxgbe_tx_ctxt_desc *p);

	/* Set IVLAN information */
	void (*tx_ctxt_desc_set_ivlantag)(struct sxgbe_tx_ctxt_desc *p,
					  int is_ivlanvalid, int ivlan_tag,
					  int ivlan_ctl);

	/* Return IVLAN Tag */
	int (*tx_ctxt_desc_get_ivlantag)(struct sxgbe_tx_ctxt_desc *p);

	/* Set VLAN Tag */
	void (*tx_ctxt_desc_set_vlantag)(struct sxgbe_tx_ctxt_desc *p,
					 int is_vlanvalid, int vlan_tag);

	/* Return VLAN Tag */
	int (*tx_ctxt_desc_get_vlantag)(struct sxgbe_tx_ctxt_desc *p);

	/* Set Time stamp */
	void (*tx_ctxt_set_tstamp)(struct sxgbe_tx_ctxt_desc *p,
				   u8 ostc_enable, u64 tstamp);

	/* Close TX context descriptor */
	void (*close_tx_ctxt_desc)(struct sxgbe_tx_ctxt_desc *p);

	/* WB status of context descriptor */
	int (*get_tx_ctxt_cde)(struct sxgbe_tx_ctxt_desc *p);

	/* DMA RX descriptor ring initialization */
	void (*init_rx_desc)(struct sxgbe_rx_norm_desc *p, int disable_rx_ic,
			     int mode, int end);

	/* Get own bit */
	int (*get_rx_owner)(struct sxgbe_rx_norm_desc *p);

	/* Set own bit */
	void (*set_rx_owner)(struct sxgbe_rx_norm_desc *p);

	/* Set Interrupt on completion bit */
	void (*set_rx_int_on_com)(struct sxgbe_rx_norm_desc *p);

	/* Get the receive frame size */
	int (*get_rx_frame_len)(struct sxgbe_rx_norm_desc *p);

	/* Return first Descriptor status */
	int (*get_rx_fd_status)(struct sxgbe_rx_norm_desc *p);

	/* Return first Descriptor status */
	int (*get_rx_ld_status)(struct sxgbe_rx_norm_desc *p);

	/* Return the reception status looking at the RDES1 */
	int (*rx_wbstatus)(struct sxgbe_rx_norm_desc *p,
			   struct sxgbe_extra_stats *x, int *checksum);

	/* Get own bit */
	int (*get_rx_ctxt_owner)(struct sxgbe_rx_ctxt_desc *p);

	/* Set own bit */
	void (*set_rx_ctxt_owner)(struct sxgbe_rx_ctxt_desc *p);

	/* Return the reception status looking at Context control information */
	void (*rx_ctxt_wbstatus)(struct sxgbe_rx_ctxt_desc *p,
				 struct sxgbe_extra_stats *x);

	/* Get rx timestamp status */
	int (*get_rx_ctxt_tstamp_status)(struct sxgbe_rx_ctxt_desc *p);

	/* Get timestamp value for rx, need to check this */
	u64 (*get_timestamp)(struct sxgbe_rx_ctxt_desc *p);
};

const struct sxgbe_desc_ops *sxgbe_get_desc_ops(void);

#endif /* __SXGBE_DESC_H__ */
