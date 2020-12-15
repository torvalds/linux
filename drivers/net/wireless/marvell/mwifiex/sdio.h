/*
 * NXP Wireless LAN device driver: SDIO specific definitions
 *
 * Copyright 2011-2020 NXP
 *
 * This software file (the "File") is distributed by NXP
 * under the terms of the GNU General Public License Version 2, June 1991
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
#define SD8987_DEFAULT_FW_NAME "mrvl/sd8987_uapsta.bin"
#define SD8997_DEFAULT_FW_NAME "mrvl/sdsd8997_combo_v4.bin"

#define BLOCK_MODE	1
#define BYTE_MODE	0

#define REG_PORT			0

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
	u8 host_int_rsr_reg;
	u8 host_int_status_reg;
	u8 host_int_mask_reg;
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

	struct mwifiex_sdio_mpa_tx mpa_tx;
	struct mwifiex_sdio_mpa_rx mpa_rx;

	struct work_struct work;
	unsigned long work_flags;
};

struct mwifiex_sdio_device {
	const char *firmware;
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
};

static const struct mwifiex_sdio_card_reg mwifiex_reg_sd87xx = {
	.start_rd_port = 1,
	.start_wr_port = 1,
	.base_0_reg = 0x0040,
	.base_1_reg = 0x0041,
	.poll_reg = 0x30,
	.host_int_enable = UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK,
	.host_int_rsr_reg = 0x1,
	.host_int_mask_reg = 0x02,
	.host_int_status_reg = 0x03,
	.status_reg_0 = 0x60,
	.status_reg_1 = 0x61,
	.sdio_int_mask = 0x3f,
	.data_port_mask = 0x0000fffe,
	.io_port_0_reg = 0x78,
	.io_port_1_reg = 0x79,
	.io_port_2_reg = 0x7A,
	.max_mp_regs = 64,
	.rd_bitmap_l = 0x04,
	.rd_bitmap_u = 0x05,
	.wr_bitmap_l = 0x06,
	.wr_bitmap_u = 0x07,
	.rd_len_p0_l = 0x08,
	.rd_len_p0_u = 0x09,
	.card_misc_cfg_reg = 0x6c,
	.func1_dump_reg_start = 0x0,
	.func1_dump_reg_end = 0x9,
	.func1_scratch_reg = 0x60,
	.func1_spec_reg_num = 5,
	.func1_spec_reg_table = {0x28, 0x30, 0x34, 0x38, 0x3c},
};

static const struct mwifiex_sdio_card_reg mwifiex_reg_sd8897 = {
	.start_rd_port = 0,
	.start_wr_port = 0,
	.base_0_reg = 0x60,
	.base_1_reg = 0x61,
	.poll_reg = 0x50,
	.host_int_enable = UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK |
			CMD_PORT_UPLD_INT_MASK | CMD_PORT_DNLD_INT_MASK,
	.host_int_rsr_reg = 0x1,
	.host_int_status_reg = 0x03,
	.host_int_mask_reg = 0x02,
	.status_reg_0 = 0xc0,
	.status_reg_1 = 0xc1,
	.sdio_int_mask = 0xff,
	.data_port_mask = 0xffffffff,
	.io_port_0_reg = 0xD8,
	.io_port_1_reg = 0xD9,
	.io_port_2_reg = 0xDA,
	.max_mp_regs = 184,
	.rd_bitmap_l = 0x04,
	.rd_bitmap_u = 0x05,
	.rd_bitmap_1l = 0x06,
	.rd_bitmap_1u = 0x07,
	.wr_bitmap_l = 0x08,
	.wr_bitmap_u = 0x09,
	.wr_bitmap_1l = 0x0a,
	.wr_bitmap_1u = 0x0b,
	.rd_len_p0_l = 0x0c,
	.rd_len_p0_u = 0x0d,
	.card_misc_cfg_reg = 0xcc,
	.card_cfg_2_1_reg = 0xcd,
	.cmd_rd_len_0 = 0xb4,
	.cmd_rd_len_1 = 0xb5,
	.cmd_rd_len_2 = 0xb6,
	.cmd_rd_len_3 = 0xb7,
	.cmd_cfg_0 = 0xb8,
	.cmd_cfg_1 = 0xb9,
	.cmd_cfg_2 = 0xba,
	.cmd_cfg_3 = 0xbb,
	.fw_dump_host_ready = 0xee,
	.fw_dump_ctrl = 0xe2,
	.fw_dump_start = 0xe3,
	.fw_dump_end = 0xea,
	.func1_dump_reg_start = 0x0,
	.func1_dump_reg_end = 0xb,
	.func1_scratch_reg = 0xc0,
	.func1_spec_reg_num = 8,
	.func1_spec_reg_table = {0x4C, 0x50, 0x54, 0x55, 0x58,
				 0x59, 0x5c, 0x5d},
};

static const struct mwifiex_sdio_card_reg mwifiex_reg_sd8977 = {
	.start_rd_port = 0,
	.start_wr_port = 0,
	.base_0_reg = 0xF8,
	.base_1_reg = 0xF9,
	.poll_reg = 0x5C,
	.host_int_enable = UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK |
		CMD_PORT_UPLD_INT_MASK | CMD_PORT_DNLD_INT_MASK,
	.host_int_rsr_reg = 0x4,
	.host_int_status_reg = 0x0C,
	.host_int_mask_reg = 0x08,
	.status_reg_0 = 0xE8,
	.status_reg_1 = 0xE9,
	.sdio_int_mask = 0xff,
	.data_port_mask = 0xffffffff,
	.io_port_0_reg = 0xE4,
	.io_port_1_reg = 0xE5,
	.io_port_2_reg = 0xE6,
	.max_mp_regs = 196,
	.rd_bitmap_l = 0x10,
	.rd_bitmap_u = 0x11,
	.rd_bitmap_1l = 0x12,
	.rd_bitmap_1u = 0x13,
	.wr_bitmap_l = 0x14,
	.wr_bitmap_u = 0x15,
	.wr_bitmap_1l = 0x16,
	.wr_bitmap_1u = 0x17,
	.rd_len_p0_l = 0x18,
	.rd_len_p0_u = 0x19,
	.card_misc_cfg_reg = 0xd8,
	.card_cfg_2_1_reg = 0xd9,
	.cmd_rd_len_0 = 0xc0,
	.cmd_rd_len_1 = 0xc1,
	.cmd_rd_len_2 = 0xc2,
	.cmd_rd_len_3 = 0xc3,
	.cmd_cfg_0 = 0xc4,
	.cmd_cfg_1 = 0xc5,
	.cmd_cfg_2 = 0xc6,
	.cmd_cfg_3 = 0xc7,
	.fw_dump_host_ready = 0xcc,
	.fw_dump_ctrl = 0xf0,
	.fw_dump_start = 0xf1,
	.fw_dump_end = 0xf8,
	.func1_dump_reg_start = 0x10,
	.func1_dump_reg_end = 0x17,
	.func1_scratch_reg = 0xe8,
	.func1_spec_reg_num = 13,
	.func1_spec_reg_table = {0x08, 0x58, 0x5C, 0x5D,
				 0x60, 0x61, 0x62, 0x64,
				 0x65, 0x66, 0x68, 0x69,
				 0x6a},
};

static const struct mwifiex_sdio_card_reg mwifiex_reg_sd8997 = {
	.start_rd_port = 0,
	.start_wr_port = 0,
	.base_0_reg = 0xF8,
	.base_1_reg = 0xF9,
	.poll_reg = 0x5C,
	.host_int_enable = UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK |
			CMD_PORT_UPLD_INT_MASK | CMD_PORT_DNLD_INT_MASK,
	.host_int_rsr_reg = 0x4,
	.host_int_status_reg = 0x0C,
	.host_int_mask_reg = 0x08,
	.status_reg_0 = 0xE8,
	.status_reg_1 = 0xE9,
	.sdio_int_mask = 0xff,
	.data_port_mask = 0xffffffff,
	.io_port_0_reg = 0xE4,
	.io_port_1_reg = 0xE5,
	.io_port_2_reg = 0xE6,
	.max_mp_regs = 196,
	.rd_bitmap_l = 0x10,
	.rd_bitmap_u = 0x11,
	.rd_bitmap_1l = 0x12,
	.rd_bitmap_1u = 0x13,
	.wr_bitmap_l = 0x14,
	.wr_bitmap_u = 0x15,
	.wr_bitmap_1l = 0x16,
	.wr_bitmap_1u = 0x17,
	.rd_len_p0_l = 0x18,
	.rd_len_p0_u = 0x19,
	.card_misc_cfg_reg = 0xd8,
	.card_cfg_2_1_reg = 0xd9,
	.cmd_rd_len_0 = 0xc0,
	.cmd_rd_len_1 = 0xc1,
	.cmd_rd_len_2 = 0xc2,
	.cmd_rd_len_3 = 0xc3,
	.cmd_cfg_0 = 0xc4,
	.cmd_cfg_1 = 0xc5,
	.cmd_cfg_2 = 0xc6,
	.cmd_cfg_3 = 0xc7,
	.fw_dump_host_ready = 0xcc,
	.fw_dump_ctrl = 0xf0,
	.fw_dump_start = 0xf1,
	.fw_dump_end = 0xf8,
	.func1_dump_reg_start = 0x10,
	.func1_dump_reg_end = 0x17,
	.func1_scratch_reg = 0xe8,
	.func1_spec_reg_num = 13,
	.func1_spec_reg_table = {0x08, 0x58, 0x5C, 0x5D,
				 0x60, 0x61, 0x62, 0x64,
				 0x65, 0x66, 0x68, 0x69,
				 0x6a},
};

static const struct mwifiex_sdio_card_reg mwifiex_reg_sd8887 = {
	.start_rd_port = 0,
	.start_wr_port = 0,
	.base_0_reg = 0x6C,
	.base_1_reg = 0x6D,
	.poll_reg = 0x5C,
	.host_int_enable = UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK |
			CMD_PORT_UPLD_INT_MASK | CMD_PORT_DNLD_INT_MASK,
	.host_int_rsr_reg = 0x4,
	.host_int_status_reg = 0x0C,
	.host_int_mask_reg = 0x08,
	.status_reg_0 = 0x90,
	.status_reg_1 = 0x91,
	.sdio_int_mask = 0xff,
	.data_port_mask = 0xffffffff,
	.io_port_0_reg = 0xE4,
	.io_port_1_reg = 0xE5,
	.io_port_2_reg = 0xE6,
	.max_mp_regs = 196,
	.rd_bitmap_l = 0x10,
	.rd_bitmap_u = 0x11,
	.rd_bitmap_1l = 0x12,
	.rd_bitmap_1u = 0x13,
	.wr_bitmap_l = 0x14,
	.wr_bitmap_u = 0x15,
	.wr_bitmap_1l = 0x16,
	.wr_bitmap_1u = 0x17,
	.rd_len_p0_l = 0x18,
	.rd_len_p0_u = 0x19,
	.card_misc_cfg_reg = 0xd8,
	.card_cfg_2_1_reg = 0xd9,
	.cmd_rd_len_0 = 0xc0,
	.cmd_rd_len_1 = 0xc1,
	.cmd_rd_len_2 = 0xc2,
	.cmd_rd_len_3 = 0xc3,
	.cmd_cfg_0 = 0xc4,
	.cmd_cfg_1 = 0xc5,
	.cmd_cfg_2 = 0xc6,
	.cmd_cfg_3 = 0xc7,
	.func1_dump_reg_start = 0x10,
	.func1_dump_reg_end = 0x17,
	.func1_scratch_reg = 0x90,
	.func1_spec_reg_num = 13,
	.func1_spec_reg_table = {0x08, 0x58, 0x5C, 0x5D, 0x60,
				 0x61, 0x62, 0x64, 0x65, 0x66,
				 0x68, 0x69, 0x6a},
};

static const struct mwifiex_sdio_card_reg mwifiex_reg_sd8987 = {
	.start_rd_port = 0,
	.start_wr_port = 0,
	.base_0_reg = 0xF8,
	.base_1_reg = 0xF9,
	.poll_reg = 0x5C,
	.host_int_enable = UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK |
			CMD_PORT_UPLD_INT_MASK | CMD_PORT_DNLD_INT_MASK,
	.host_int_rsr_reg = 0x4,
	.host_int_status_reg = 0x0C,
	.host_int_mask_reg = 0x08,
	.status_reg_0 = 0xE8,
	.status_reg_1 = 0xE9,
	.sdio_int_mask = 0xff,
	.data_port_mask = 0xffffffff,
	.io_port_0_reg = 0xE4,
	.io_port_1_reg = 0xE5,
	.io_port_2_reg = 0xE6,
	.max_mp_regs = 196,
	.rd_bitmap_l = 0x10,
	.rd_bitmap_u = 0x11,
	.rd_bitmap_1l = 0x12,
	.rd_bitmap_1u = 0x13,
	.wr_bitmap_l = 0x14,
	.wr_bitmap_u = 0x15,
	.wr_bitmap_1l = 0x16,
	.wr_bitmap_1u = 0x17,
	.rd_len_p0_l = 0x18,
	.rd_len_p0_u = 0x19,
	.card_misc_cfg_reg = 0xd8,
	.card_cfg_2_1_reg = 0xd9,
	.cmd_rd_len_0 = 0xc0,
	.cmd_rd_len_1 = 0xc1,
	.cmd_rd_len_2 = 0xc2,
	.cmd_rd_len_3 = 0xc3,
	.cmd_cfg_0 = 0xc4,
	.cmd_cfg_1 = 0xc5,
	.cmd_cfg_2 = 0xc6,
	.cmd_cfg_3 = 0xc7,
	.fw_dump_host_ready = 0xcc,
	.fw_dump_ctrl = 0xf9,
	.fw_dump_start = 0xf1,
	.fw_dump_end = 0xf8,
	.func1_dump_reg_start = 0x10,
	.func1_dump_reg_end = 0x17,
	.func1_scratch_reg = 0xE8,
	.func1_spec_reg_num = 13,
	.func1_spec_reg_table = {0x08, 0x58, 0x5C, 0x5D, 0x60,
				 0x61, 0x62, 0x64, 0x65, 0x66,
				 0x68, 0x69, 0x6a},
};

static const struct mwifiex_sdio_device mwifiex_sdio_sd8786 = {
	.firmware = SD8786_DEFAULT_FW_NAME,
	.reg = &mwifiex_reg_sd87xx,
	.max_ports = 16,
	.mp_agg_pkt_limit = 8,
	.tx_buf_size = MWIFIEX_TX_DATA_BUF_SIZE_2K,
	.mp_tx_agg_buf_size = MWIFIEX_MP_AGGR_BUF_SIZE_16K,
	.mp_rx_agg_buf_size = MWIFIEX_MP_AGGR_BUF_SIZE_16K,
	.supports_sdio_new_mode = false,
	.has_control_mask = true,
	.can_dump_fw = false,
	.can_auto_tdls = false,
	.can_ext_scan = false,
};

static const struct mwifiex_sdio_device mwifiex_sdio_sd8787 = {
	.firmware = SD8787_DEFAULT_FW_NAME,
	.reg = &mwifiex_reg_sd87xx,
	.max_ports = 16,
	.mp_agg_pkt_limit = 8,
	.tx_buf_size = MWIFIEX_TX_DATA_BUF_SIZE_2K,
	.mp_tx_agg_buf_size = MWIFIEX_MP_AGGR_BUF_SIZE_16K,
	.mp_rx_agg_buf_size = MWIFIEX_MP_AGGR_BUF_SIZE_16K,
	.supports_sdio_new_mode = false,
	.has_control_mask = true,
	.can_dump_fw = false,
	.can_auto_tdls = false,
	.can_ext_scan = true,
};

static const struct mwifiex_sdio_device mwifiex_sdio_sd8797 = {
	.firmware = SD8797_DEFAULT_FW_NAME,
	.reg = &mwifiex_reg_sd87xx,
	.max_ports = 16,
	.mp_agg_pkt_limit = 8,
	.tx_buf_size = MWIFIEX_TX_DATA_BUF_SIZE_2K,
	.mp_tx_agg_buf_size = MWIFIEX_MP_AGGR_BUF_SIZE_16K,
	.mp_rx_agg_buf_size = MWIFIEX_MP_AGGR_BUF_SIZE_16K,
	.supports_sdio_new_mode = false,
	.has_control_mask = true,
	.can_dump_fw = false,
	.can_auto_tdls = false,
	.can_ext_scan = true,
};

static const struct mwifiex_sdio_device mwifiex_sdio_sd8897 = {
	.firmware = SD8897_DEFAULT_FW_NAME,
	.reg = &mwifiex_reg_sd8897,
	.max_ports = 32,
	.mp_agg_pkt_limit = 16,
	.tx_buf_size = MWIFIEX_TX_DATA_BUF_SIZE_4K,
	.mp_tx_agg_buf_size = MWIFIEX_MP_AGGR_BUF_SIZE_MAX,
	.mp_rx_agg_buf_size = MWIFIEX_MP_AGGR_BUF_SIZE_MAX,
	.supports_sdio_new_mode = true,
	.has_control_mask = false,
	.can_dump_fw = true,
	.can_auto_tdls = false,
	.can_ext_scan = true,
};

static const struct mwifiex_sdio_device mwifiex_sdio_sd8977 = {
	.firmware = SD8977_DEFAULT_FW_NAME,
	.reg = &mwifiex_reg_sd8977,
	.max_ports = 32,
	.mp_agg_pkt_limit = 16,
	.tx_buf_size = MWIFIEX_TX_DATA_BUF_SIZE_4K,
	.mp_tx_agg_buf_size = MWIFIEX_MP_AGGR_BUF_SIZE_MAX,
	.mp_rx_agg_buf_size = MWIFIEX_MP_AGGR_BUF_SIZE_MAX,
	.supports_sdio_new_mode = true,
	.has_control_mask = false,
	.can_dump_fw = true,
	.fw_dump_enh = true,
	.can_auto_tdls = false,
	.can_ext_scan = true,
};

static const struct mwifiex_sdio_device mwifiex_sdio_sd8997 = {
	.firmware = SD8997_DEFAULT_FW_NAME,
	.reg = &mwifiex_reg_sd8997,
	.max_ports = 32,
	.mp_agg_pkt_limit = 16,
	.tx_buf_size = MWIFIEX_TX_DATA_BUF_SIZE_4K,
	.mp_tx_agg_buf_size = MWIFIEX_MP_AGGR_BUF_SIZE_MAX,
	.mp_rx_agg_buf_size = MWIFIEX_MP_AGGR_BUF_SIZE_MAX,
	.supports_sdio_new_mode = true,
	.has_control_mask = false,
	.can_dump_fw = true,
	.fw_dump_enh = true,
	.can_auto_tdls = false,
	.can_ext_scan = true,
};

static const struct mwifiex_sdio_device mwifiex_sdio_sd8887 = {
	.firmware = SD8887_DEFAULT_FW_NAME,
	.reg = &mwifiex_reg_sd8887,
	.max_ports = 32,
	.mp_agg_pkt_limit = 16,
	.tx_buf_size = MWIFIEX_TX_DATA_BUF_SIZE_2K,
	.mp_tx_agg_buf_size = MWIFIEX_MP_AGGR_BUF_SIZE_32K,
	.mp_rx_agg_buf_size = MWIFIEX_MP_AGGR_BUF_SIZE_32K,
	.supports_sdio_new_mode = true,
	.has_control_mask = false,
	.can_dump_fw = false,
	.can_auto_tdls = true,
	.can_ext_scan = true,
};

static const struct mwifiex_sdio_device mwifiex_sdio_sd8987 = {
	.firmware = SD8987_DEFAULT_FW_NAME,
	.reg = &mwifiex_reg_sd8987,
	.max_ports = 32,
	.mp_agg_pkt_limit = 16,
	.tx_buf_size = MWIFIEX_TX_DATA_BUF_SIZE_2K,
	.mp_tx_agg_buf_size = MWIFIEX_MP_AGGR_BUF_SIZE_MAX,
	.mp_rx_agg_buf_size = MWIFIEX_MP_AGGR_BUF_SIZE_MAX,
	.supports_sdio_new_mode = true,
	.has_control_mask = false,
	.can_dump_fw = true,
	.fw_dump_enh = true,
	.can_auto_tdls = true,
	.can_ext_scan = true,
};

static const struct mwifiex_sdio_device mwifiex_sdio_sd8801 = {
	.firmware = SD8801_DEFAULT_FW_NAME,
	.reg = &mwifiex_reg_sd87xx,
	.max_ports = 16,
	.mp_agg_pkt_limit = 8,
	.supports_sdio_new_mode = false,
	.has_control_mask = true,
	.tx_buf_size = MWIFIEX_TX_DATA_BUF_SIZE_2K,
	.mp_tx_agg_buf_size = MWIFIEX_MP_AGGR_BUF_SIZE_16K,
	.mp_rx_agg_buf_size = MWIFIEX_MP_AGGR_BUF_SIZE_16K,
	.can_dump_fw = false,
	.can_auto_tdls = false,
	.can_ext_scan = true,
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
	card->mpa_rx.skb_arr[card->mpa_rx.pkt_cnt] = NULL;
	card->mpa_rx.len_arr[card->mpa_rx.pkt_cnt] = rx_len;
	card->mpa_rx.pkt_cnt++;
}
#endif /* _MWIFIEX_SDIO_H */
