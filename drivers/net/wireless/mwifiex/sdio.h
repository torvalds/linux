/*
 * Marvell Wireless LAN device driver: SDIO specific definitions
 *
 * Copyright (C) 2011, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#ifndef	_MWIFIEX_SDIO_H
#define	_MWIFIEX_SDIO_H


#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>

#include "main.h"

#define SD8786_DEFAULT_FW_NAME "mrvl/sd8786_uapsta.bin"
#define SD8787_DEFAULT_FW_NAME "mrvl/sd8787_uapsta.bin"
#define SD8797_DEFAULT_FW_NAME "mrvl/sd8797_uapsta.bin"

#define BLOCK_MODE	1
#define BYTE_MODE	0

#define REG_PORT			0

#define MWIFIEX_SDIO_IO_PORT_MASK		0xfffff

#define MWIFIEX_SDIO_BYTE_MODE_MASK	0x80000000

#define SDIO_MPA_ADDR_BASE		0x1000
#define CTRL_PORT			0
#define CTRL_PORT_MASK			0x0001

#define SDIO_MP_TX_AGGR_DEF_BUF_SIZE        (8192)	/* 8K */

/* Multi port RX aggregation buffer size */
#define SDIO_MP_RX_AGGR_DEF_BUF_SIZE        (16384)	/* 16K */

/* Misc. Config Register : Auto Re-enable interrupts */
#define AUTO_RE_ENABLE_INT              BIT(4)

/* Host Control Registers */
/* Host Control Registers : I/O port 0 */
#define IO_PORT_0_REG			0x78
/* Host Control Registers : I/O port 1 */
#define IO_PORT_1_REG			0x79
/* Host Control Registers : I/O port 2 */
#define IO_PORT_2_REG			0x7A

/* Host Control Registers : Configuration */
#define CONFIGURATION_REG		0x00
/* Host Control Registers : Host power up */
#define HOST_POWER_UP			(0x1U << 1)

/* Host Control Registers : Host interrupt mask */
#define HOST_INT_MASK_REG		0x02
/* Host Control Registers : Upload host interrupt mask */
#define UP_LD_HOST_INT_MASK		(0x1U)
/* Host Control Registers : Download host interrupt mask */
#define DN_LD_HOST_INT_MASK		(0x2U)
/* Disable Host interrupt mask */
#define	HOST_INT_DISABLE		0xff

/* Host Control Registers : Host interrupt status */
#define HOST_INTSTATUS_REG		0x03
/* Host Control Registers : Upload host interrupt status */
#define UP_LD_HOST_INT_STATUS		(0x1U)
/* Host Control Registers : Download host interrupt status */
#define DN_LD_HOST_INT_STATUS		(0x2U)

/* Host Control Registers : Host interrupt RSR */
#define HOST_INT_RSR_REG		0x01

/* Host Control Registers : Host interrupt status */
#define HOST_INT_STATUS_REG		0x28

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

	struct sk_buff **skb_arr;
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
	u8 status_reg_0;
	u8 status_reg_1;
	u8 sdio_int_mask;
	u32 data_port_mask;
	u8 max_mp_regs;
	u8 rd_bitmap_l;
	u8 rd_bitmap_u;
	u8 wr_bitmap_l;
	u8 wr_bitmap_u;
	u8 rd_len_p0_l;
	u8 rd_len_p0_u;
	u8 card_misc_cfg_reg;
};

struct sdio_mmc_card {
	struct sdio_func *func;
	struct mwifiex_adapter *adapter;

	const char *firmware;
	const struct mwifiex_sdio_card_reg *reg;
	u8 max_ports;
	u8 mp_agg_pkt_limit;

	u32 mp_rd_bitmap;
	u32 mp_wr_bitmap;

	u16 mp_end_port;
	u32 mp_data_port_mask;

	u8 curr_rd_port;
	u8 curr_wr_port;

	u8 *mp_regs;

	struct mwifiex_sdio_mpa_tx mpa_tx;
	struct mwifiex_sdio_mpa_rx mpa_rx;
};

struct mwifiex_sdio_device {
	const char *firmware;
	const struct mwifiex_sdio_card_reg *reg;
	u8 max_ports;
	u8 mp_agg_pkt_limit;
};

static const struct mwifiex_sdio_card_reg mwifiex_reg_sd87xx = {
	.start_rd_port = 1,
	.start_wr_port = 1,
	.base_0_reg = 0x0040,
	.base_1_reg = 0x0041,
	.poll_reg = 0x30,
	.host_int_enable = UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK,
	.status_reg_0 = 0x60,
	.status_reg_1 = 0x61,
	.sdio_int_mask = 0x3f,
	.data_port_mask = 0x0000fffe,
	.max_mp_regs = 64,
	.rd_bitmap_l = 0x04,
	.rd_bitmap_u = 0x05,
	.wr_bitmap_l = 0x06,
	.wr_bitmap_u = 0x07,
	.rd_len_p0_l = 0x08,
	.rd_len_p0_u = 0x09,
	.card_misc_cfg_reg = 0x6c,
};

static const struct mwifiex_sdio_device mwifiex_sdio_sd8786 = {
	.firmware = SD8786_DEFAULT_FW_NAME,
	.reg = &mwifiex_reg_sd87xx,
	.max_ports = 16,
	.mp_agg_pkt_limit = 8,
};

static const struct mwifiex_sdio_device mwifiex_sdio_sd8787 = {
	.firmware = SD8787_DEFAULT_FW_NAME,
	.reg = &mwifiex_reg_sd87xx,
	.max_ports = 16,
	.mp_agg_pkt_limit = 8,
};

static const struct mwifiex_sdio_device mwifiex_sdio_sd8797 = {
	.firmware = SD8797_DEFAULT_FW_NAME,
	.reg = &mwifiex_reg_sd87xx,
	.max_ports = 16,
	.mp_agg_pkt_limit = 8,
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
		tmp = card->mp_agg_pkt_limit;

		if (((card->max_ports - card->mpa_rx.start_port) +
		    card->curr_rd_port) >= tmp)
			return true;
	}

	return false;
}

static inline bool
mp_tx_aggr_port_limit_reached(struct sdio_mmc_card *card)
{
	u16 tmp;

	if (card->curr_wr_port < card->mpa_tx.start_port) {
		tmp = card->mp_agg_pkt_limit;

		if (((card->max_ports - card->mpa_tx.start_port) +
		    card->curr_wr_port) >= tmp)
			return true;
	}

	return false;
}

/* Prepare to copy current packet from card to SDIO Rx aggregation buffer */
static inline void mp_rx_aggr_setup(struct sdio_mmc_card *card,
				    struct sk_buff *skb, u8 port)
{
	card->mpa_rx.buf_len += skb->len;

	if (!card->mpa_rx.pkt_cnt)
		card->mpa_rx.start_port = port;

	if (card->mpa_rx.start_port <= port)
		card->mpa_rx.ports |= 1 << (card->mpa_rx.pkt_cnt);
	else
		card->mpa_rx.ports |= 1 << (card->mpa_rx.pkt_cnt + 1);

	card->mpa_rx.skb_arr[card->mpa_rx.pkt_cnt] = skb;
	card->mpa_rx.len_arr[card->mpa_rx.pkt_cnt] = skb->len;
	card->mpa_rx.pkt_cnt++;
}
#endif /* _MWIFIEX_SDIO_H */
