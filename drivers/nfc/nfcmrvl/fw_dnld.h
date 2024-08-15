/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Marvell NFC driver: Firmware downloader
 *
 * Copyright (C) 2015, Marvell International Ltd.
 */

#ifndef __NFCMRVL_FW_DNLD_H__
#define __NFCMRVL_FW_DNLD_H__

#include <linux/workqueue.h>

#define NFCMRVL_FW_MAGIC		0x88888888

#define NCI_OP_PROP_BOOT_CMD		0x3A

#define NCI_CORE_LC_PROP_FW_DL		0xFD
#define NCI_CORE_LC_CONNID_PROP_FW_DL	0x02

#define HELPER_CMD_ENTRY_POINT		0x04
#define HELPER_CMD_PACKET_FORMAT	0xA5
#define HELPER_ACK_PACKET_FORMAT	0x5A
#define HELPER_RETRY_REQUESTED		(1 << 15)

struct nfcmrvl_private;

struct nfcmrvl_fw_uart_config {
	uint8_t flow_control;
	uint32_t baudrate;
} __packed;

struct nfcmrvl_fw_i2c_config {
	uint32_t clk;
} __packed;

struct nfcmrvl_fw_spi_config {
	uint32_t clk;
} __packed;

struct nfcmrvl_fw_binary_config {
	uint32_t offset;
	union {
		void *config;
		struct nfcmrvl_fw_uart_config uart;
		struct nfcmrvl_fw_i2c_config i2c;
		struct nfcmrvl_fw_spi_config spi;
		uint8_t reserved[64];
	};
} __packed;

struct nfcmrvl_fw {
	uint32_t magic;
	uint32_t ref_clock;
	uint32_t phy;
	struct nfcmrvl_fw_binary_config bootrom;
	struct nfcmrvl_fw_binary_config helper;
	struct nfcmrvl_fw_binary_config firmware;
	uint8_t reserved[64];
} __packed;

struct nfcmrvl_fw_dnld {
	char name[NFC_FIRMWARE_NAME_MAXSIZE + 1];
	const struct firmware *fw;

	const struct nfcmrvl_fw *header;
	const struct nfcmrvl_fw_binary_config *binary_config;

	int state;
	int substate;
	int offset;
	int chunk_len;

	struct workqueue_struct	*rx_wq;
	struct work_struct rx_work;
	struct sk_buff_head rx_q;

	struct timer_list timer;
};

int nfcmrvl_fw_dnld_init(struct nfcmrvl_private *priv);
void nfcmrvl_fw_dnld_deinit(struct nfcmrvl_private *priv);
void nfcmrvl_fw_dnld_abort(struct nfcmrvl_private *priv);
int nfcmrvl_fw_dnld_start(struct nci_dev *ndev, const char *firmware_name);
void nfcmrvl_fw_dnld_recv_frame(struct nfcmrvl_private *priv,
				struct sk_buff *skb);

#endif
