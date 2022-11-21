/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2019-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GAUDI_FW_IF_H
#define GAUDI_FW_IF_H

#define GAUDI_EVENT_QUEUE_MSI_IDX	8
#define GAUDI_NIC_PORT1_MSI_IDX		10
#define GAUDI_NIC_PORT3_MSI_IDX		12
#define GAUDI_NIC_PORT5_MSI_IDX		14
#define GAUDI_NIC_PORT7_MSI_IDX		16
#define GAUDI_NIC_PORT9_MSI_IDX		18

#define UBOOT_FW_OFFSET			0x100000	/* 1MB in SRAM */
#define LINUX_FW_OFFSET			0x800000	/* 8MB in HBM */

/* HBM thermal delta in [Deg] added to composite (CTemp) */
#define HBM_TEMP_ADJUST_COEFF		6

enum gaudi_nic_axi_error {
	RXB,
	RXE,
	TXS,
	TXE,
	QPC_RESP,
	NON_AXI_ERR,
	TMR,
};

/*
 * struct eq_nic_sei_event - describes an AXI error cause.
 * @axi_error_cause: one of the events defined in enum gaudi_nic_axi_error.
 * @id: can be either 0 or 1, to further describe unit with interrupt cause
 *      (i.e. TXE0 or TXE1).
 * @pad[6]: padding structure to 64bit.
 */
struct eq_nic_sei_event {
	__u8 axi_error_cause;
	__u8 id;
	__u8 pad[6];
};

/*
 * struct gaudi_nic_status - describes the status of a NIC port.
 * @port: NIC port index.
 * @bad_format_cnt: e.g. CRC.
 * @responder_out_of_sequence_psn_cnt: e.g NAK.
 * @high_ber_reinit_cnt: link reinit due to high BER.
 * @correctable_err_cnt: e.g. bit-flip.
 * @uncorrectable_err_cnt: e.g. MAC errors.
 * @retraining_cnt: re-training counter.
 * @up: is port up.
 * @pcs_link: has PCS link.
 * @phy_ready: is PHY ready.
 * @auto_neg: is Autoneg enabled.
 * @timeout_retransmission_cnt: timeout retransmission events
 * @high_ber_cnt: high ber events
 */
struct gaudi_nic_status {
	__u32 port;
	__u32 bad_format_cnt;
	__u32 responder_out_of_sequence_psn_cnt;
	__u32 high_ber_reinit;
	__u32 correctable_err_cnt;
	__u32 uncorrectable_err_cnt;
	__u32 retraining_cnt;
	__u8 up;
	__u8 pcs_link;
	__u8 phy_ready;
	__u8 auto_neg;
	__u32 timeout_retransmission_cnt;
	__u32 high_ber_cnt;
};

struct gaudi_cold_rst_data {
	union {
		struct {
			u32 spsram_init_done : 1;
			u32 reserved : 31;
		};
		__le32 data;
	};
};

#define GAUDI_PLL_FREQ_LOW		200000000 /* 200 MHz */

#endif /* GAUDI_FW_IF_H */
