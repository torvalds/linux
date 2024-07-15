/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NXP Wireless LAN device driver: SDIO specific definitions
 *
 * Copyright 2011-2020 NXP
 */

#ifndef	_MWIFIEX_SDIO_H
#define	_MWIFIEX_SDIO_H


#include <linux/completion.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>

#include "main.h"

#define SD8786_DEFAULT_FW_NAME "mrvl/sd8786_uapsta.bin"
#define SD8787_DEFAULT_FW_NAME "mrvl/sd8787_uapsta.bin"
#define SD8797_DEFAULT_FW_NAME "mrvl/sd8797_uapsta.bin"
#define SD8897_DEFAULT_FW_NAME "mrvl/sd8897_uapsta.bin"
#define SD8887_DEFAULT_FW_NAME "mrvl/sd8887_uapsta.bin"
#define SD8801_DEFAULT_FW_NAME "mrvl/sd8801_uapsta.bin"
#define SD8977_DEFAULT_FW_NAME "mrvl/sdsd8977_combo_v2.bin"
#define SD8978_SDIOUART_FW_NAME "mrvl/sdiouartiw416_combo_v0.bin"
#define SD8987_DEFAULT_FW_NAME "mrvl/sd8987_uapsta.bin"
#define SD8997_DEFAULT_FW_NAME "mrvl/sdsd8997_combo_v4.bin"
#define SD8997_SDIOUART_FW_NAME "mrvl/sdiouart8997_combo_v4.bin"

#define BLOCK_MODE	1
#define BYTE_MODE	0

#define MWIFIEX_SDIO_IO_PORT_MASK		0xfffff

#define MWIFIEX_SDIO_BYTE_MODE_MASK	0x80000000

#define MWIFIEX_MAX_FUNC2_REG_NUM	13
#define MWIFIEX_SDIO_SCRATCH_SIZE	10

#define SDIO_MPA_ADDR_BASE		0x1000
#define CTRL_PORT			0
#define CTRL_PORT_MASK			0x0001

#define CMD_PORT_UPLD_INT_MASK		(0x1U<<6)
#define CMD_PORT_DNLD_INT_MASK		(0x1U<<7)
#define HOST_TERM_CMD53			(0x1U << 2)
#define REG_PORT			0
#define MEM_PORT			0x10000

#define CMD53_NEW_MODE			(0x1U << 0)
#define CMD_PORT_RD_LEN_EN		(0x1U << 2)
#define CMD_PORT_AUTO_EN		(0x1U << 0)
#define CMD_PORT_SLCT			0x8000
#define UP_LD_CMD_PORT_HOST_INT_STATUS	(0x40U)
#define DN_LD_CMD_PORT_HOST_INT_STATUS	(0x80U)

#define MWIFIEX_MP_AGGR_BUF_SIZE_16K	(16384)
#define MWIFIEX_MP_AGGR_BUF_SIZE_32K	(32768)
/* we leave one block of 256 bytes for DMA alignment*/
#define MWIFIEX_MP_AGGR_BUF_SIZE_MAX    (65280)

/* Misc. Config Register : Auto Re-enable interrupts */
#define AUTO_RE_ENABLE_INT              BIT(4)

/* Host Control Registers : Configuration */
#define CONFIGURATION_REG		0x00
/* Host Control Registers : Host power up */
#define HOST_POWER_UP			(0x1U << 1)

/* Host Control Registers : Upload host interrupt mask */
#define UP_LD_HOST_INT_MASK		(0x1U)
/* Host Control Registers : Download host interrupt mask */
#define DN_LD_HOST_INT_MASK		(0x2U)

/* Host Control Registers : Upload host interrupt status */
#define UP_LD_HOST_INT_STATUS		(0x1U)
/* Host Control Registers : Download host interrupt status */
#define DN_LD_HOST_INT_STATUS		(0x2U)

/* Host Control Registers : Host interrupt status */
#define CARD_INT_STATUS_REG		0x28

/* Card Control Registers : Card I/O ready */
#define CARD_IO_READY                   (0x1U << 3)
/* Card Control Registers : Download card ready */
#define DN_LD_CARD_RDY                  (0x1U << 0)

/* Max retry number of CMD53 write */
#define MAX_WRITE_IOMEM_RETRY		2

/* SDIO Tx aggregation in progress ? */
#define MP_TX_AGGR_IN_PROGRESS(a) (a->mpa_tx.pkt_cnt > 0)

/* SDIO Tx aggregation buffer room for next packet ? */
#define MP_TX_AGGR_BUF_HAS_ROOM(a, len) ((a->mpa_tx.buf_len+len)	\
						<= a->mpa_tx.buf_size)

/* Copy current packet (SDIO Tx aggregation buffer) to SDIO buffer */
#define MP_TX_AGGR_BUF_PUT(a, payload, pkt_len, port) do {		\
	memmove(&a->mpa_tx.buf[a->mpa_tx.buf_len],			\
			payload, pkt_len);				\
	a->mpa_tx.buf_len += pkt_len;					\
	if (!a->mpa_tx.pkt_cnt)						\
		a->mpa_tx.start_port = port;				\
	if (a->mpa_tx.start_port <= port)				\
		a->mpa_tx.ports |= (1<<(a->mpa_tx.pkt_cnt));		\
	else								\
		a->mpa_tx.ports |= (1<<(a->mpa_tx.pkt_cnt+1+		\
						(a->max_ports -	\
						a->mp_end_port)));	\
	a->mpa_tx.pkt_cnt++;						\
} while (0)

/* SDIO Tx aggregation limit ? */
#define MP_TX_AGGR_PKT_LIMIT_REACHED(a)					\
			(a->mpa_tx.pkt_cnt == a->mpa_tx.pkt_aggr_limit)

/* Reset SDIO Tx aggregation buffer parameters */
#define MP_TX_AGGR_BUF_RESET(a) do {					\
	a->mpa_tx.pkt_cnt = 0;						\
	a->mpa_tx.buf_len = 0;						\
	a->mpa_tx.ports = 0;						\
	a->mpa_tx.start_port = 0;					\
} while (0)

/* SDIO Rx aggregation limit ? */
#define MP_RX_AGGR_PKT_LIMIT_REACHED(a)					\
			(a->mpa_rx.pkt_cnt == a->mpa_rx.pkt_aggr_limit)

/* SDIO Rx aggregation in progress ? */
#define MP_RX_AGGR_IN_PROGRESS(a) (a->mpa_rx.pkt_cnt > 0)

/* SDIO Rx aggregation buffer room for next packet ? */
#define MP_RX_AGGR_BUF_HAS_ROOM(a, rx_len)				\
			((a->mpa_rx.buf_len+rx_len) <= a->mpa_rx.buf_size)

/* Reset SDIO Rx aggregation buffer parameters */
#define MP_RX_AGGR_BUF_RESET(a) do {					\
	a->mpa_rx.pkt_cnt = 0;						\
	a->mpa_rx.buf_len = 0;						\
	a->mpa_rx.ports = 0;						\
	a->mpa_rx.start_port = 0;					\
} while (0)

/* data structure for SDIO MPA TX */
struct mwifiex_sdio_mpa_tx {
	/* multiport tx aggregation buffer pointer */
	u8 *buf;
	u32 buf_len;
	u32 pkt_cnt;
	u32 ports;
	u16 start_port;
	u8 enabled;
	u32 buf_size;
	u32 pkt_aggr_limit;
};

struct mwifiex_sdio_mpa_rx {
	u8 *buf;
	u32 buf_len;
	u32 pkt_cnt;
	u32 ports;
	u16 start_port;
	u32 *len_arr;
	u8 enabled;
	u32 buf_size;
	u32 pkt_aggr_limit;
};

int mwifiex_bus_register(void);
void mwifiex_bus_unregister(void);

struct mwifiex_sdio_card_reg {
	u8 start_rd_port;
	u8 start_wr_port;
	u8 base_0_reg;
	u8 base_1_reg;
	u8 poll_reg;
	u8 host_int_enable;
	u8 host_int_rsr_reg;
	u8 host_int_status_reg;
	u8 host_int_mask_reg;
	u8 host_strap_reg;
	u8 host_strap_mask;
	u8 host_strap_value;
	u8 status_reg_0;
	u8 status_reg_1;
	u8 sdio_int_mask;
	u32 data_port_mask;
	u8 io_port_0_reg;
	u8 io_port_1_reg;
	u8 io_port_2_reg;
	u8 max_mp_regs;
	u8 rd_bitmap_l;
	u8 rd_bitmap_u;
	u8 rd_bitmap_1l;
	u8 rd_bitmap_1u;
	u8 wr_bitmap_l;
	u8 wr_bitmap_u;
	u8 wr_bitmap_1l;
	u8 wr_bitmap_1u;
	u8 rd_len_p0_l;
	u8 rd_len_p0_u;
	u8 card_misc_cfg_reg;
	u8 card_cfg_2_1_reg;
	u8 cmd_rd_len_0;
	u8 cmd_rd_len_1;
	u8 cmd_rd_len_2;
	u8 cmd_rd_len_3;
	u8 cmd_cfg_0;
	u8 cmd_cfg_1;
	u8 cmd_cfg_2;
	u8 cmd_cfg_3;
	u8 fw_dump_host_ready;
	u8 fw_dump_ctrl;
	u8 fw_dump_start;
	u8 fw_dump_end;
	u8 func1_dump_reg_start;
	u8 func1_dump_reg_end;
	u8 func1_scratch_reg;
	u8 func1_spec_reg_num;
	u8 func1_spec_reg_table[MWIFIEX_MAX_FUNC2_REG_NUM];
};

struct sdio_mmc_card {
	struct sdio_func *func;
	struct mwifiex_adapter *adapter;

	struct completion fw_done;
	const char *firmware;
	const char *firmware_sdiouart;
	const struct mwifiex_sdio_card_reg *reg;
	u8 max_ports;
	u8 mp_agg_pkt_limit;
	u16 tx_buf_size;
	u32 mp_tx_agg_buf_size;
	u32 mp_rx_agg_buf_size;

	u32 mp_rd_bitmap;
	u32 mp_wr_bitmap;

	u16 mp_end_port;
	u32 mp_data_port_mask;

	u8 curr_rd_port;
	u8 curr_wr_port;

	u8 *mp_regs;
	bool supports_sdio_new_mode;
	bool has_control_mask;
	bool can_dump_fw;
	bool fw_dump_enh;
	bool can_auto_tdls;
	bool can_ext_scan;
	bool fw_ready_extra_delay;

	struct mwifiex_sdio_mpa_tx mpa_tx;
	struct mwifiex_sdio_mpa_rx mpa_rx;

	struct work_struct work;
	unsigned long work_flags;
};

struct mwifiex_sdio_device {
	const char *firmware;
	const char *firmware_sdiouart;
	const struct mwifiex_sdio_card_reg *reg;
	u8 max_ports;
	u8 mp_agg_pkt_limit;
	u16 tx_buf_size;
	u32 mp_tx_agg_buf_size;
	u32 mp_rx_agg_buf_size;
	bool supports_sdio_new_mode;
	bool has_control_mask;
	bool can_dump_fw;
	bool fw_dump_enh;
	bool can_auto_tdls;
	bool can_ext_scan;
	bool fw_ready_extra_delay;
};

/*
 * .cmdrsp_complete handler
 */
static inline int mwifiex_sdio_cmdrsp_complete(struct mwifiex_adapter *adapter,
					       struct sk_buff *skb)
{
	dev_kfree_skb_any(skb);
	return 0;
}

/*
 * .event_complete handler
 */
static inline int mwifiex_sdio_event_complete(struct mwifiex_adapter *adapter,
					      struct sk_buff *skb)
{
	dev_kfree_skb_any(skb);
	return 0;
}

static inline bool
mp_rx_aggr_port_limit_reached(struct sdio_mmc_card *card)
{
	u8 tmp;

	if (card->curr_rd_port < card->mpa_rx.start_port) {
		if (card->supports_sdio_new_mode)
			tmp = card->mp_end_port >> 1;
		else
			tmp = card->mp_agg_pkt_limit;

		if (((card->max_ports - card->mpa_rx.start_port) +
		    card->curr_rd_port) >= tmp)
			return true;
	}

	if (!card->supports_sdio_new_mode)
		return false;

	if ((card->curr_rd_port - card->mpa_rx.start_port) >=
	    (card->mp_end_port >> 1))
		return true;

	return false;
}

static inline bool
mp_tx_aggr_port_limit_reached(struct sdio_mmc_card *card)
{
	u16 tmp;

	if (card->curr_wr_port < card->mpa_tx.start_port) {
		if (card->supports_sdio_new_mode)
			tmp = card->mp_end_port >> 1;
		else
			tmp = card->mp_agg_pkt_limit;

		if (((card->max_ports - card->mpa_tx.start_port) +
		    card->curr_wr_port) >= tmp)
			return true;
	}

	if (!card->supports_sdio_new_mode)
		return false;

	if ((card->curr_wr_port - card->mpa_tx.start_port) >=
	    (card->mp_end_port >> 1))
		return true;

	return false;
}

/* Prepare to copy current packet from card to SDIO Rx aggregation buffer */
static inline void mp_rx_aggr_setup(struct sdio_mmc_card *card,
				    u16 rx_len, u8 port)
{
	card->mpa_rx.buf_len += rx_len;

	if (!card->mpa_rx.pkt_cnt)
		card->mpa_rx.start_port = port;

	if (card->supports_sdio_new_mode) {
		card->mpa_rx.ports |= (1 << port);
	} else {
		if (card->mpa_rx.start_port <= port)
			card->mpa_rx.ports |= 1 << (card->mpa_rx.pkt_cnt);
		else
			card->mpa_rx.ports |= 1 << (card->mpa_rx.pkt_cnt + 1);
	}
	card->mpa_rx.len_arr[card->mpa_rx.pkt_cnt] = rx_len;
	card->mpa_rx.pkt_cnt++;
}
#endif /* _MWIFIEX_SDIO_H */
