/** @file mlan_sdio.h
  *
  * @brief This file contains definitions for SDIO interface.
  * driver.
  *
  * Copyright (C) 2008-2017, Marvell International Ltd.
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
  *
  */
/****************************************************
Change log:
****************************************************/

#ifndef	_MLAN_SDIO_H
#define	_MLAN_SDIO_H

/** Block mode */
#ifndef BLOCK_MODE
#define BLOCK_MODE	1
#endif

/** Fixed address mode */
#ifndef FIXED_ADDRESS
#define FIXED_ADDRESS	0
#endif

/* Host Control Registers */
/** Host Control Registers : Host to Card Event */
#define HOST_TO_CARD_EVENT_REG		0x00
/** Host Control Registers : Host terminates Command 53 */
#define HOST_TERM_CMD53			(0x1U << 2)
/** Host Control Registers : Host without Command 53 finish host */
#define HOST_WO_CMD53_FINISH_HOST	(0x1U << 2)
/** Host Control Registers : Host power up */
#define HOST_POWER_UP			(0x1U << 1)
/** Host Control Registers : Host power down */
#define HOST_POWER_DOWN			(0x1U << 0)

/** Host Control Registers : Host interrupt RSR */
#define HOST_INT_RSR_REG		0x04

/** Host Control Registers : Upload host interrupt RSR */
#define UP_LD_HOST_INT_RSR		(0x1U)
#define HOST_INT_RSR_MASK		0xFF

/** Host Control Registers : Host interrupt mask */
#define HOST_INT_MASK_REG		0x08

/** Host Control Registers : Upload host interrupt mask */
#define UP_LD_HOST_INT_MASK		(0x1U)
/** Host Control Registers : Download host interrupt mask */
#define DN_LD_HOST_INT_MASK		(0x2U)
/** Host Control Registers : Cmd port upload interrupt mask */
#define CMD_PORT_UPLD_INT_MASK		(0x1U << 6)
/** Host Control Registers : Cmd port download interrupt mask */
#define CMD_PORT_DNLD_INT_MASK		(0x1U << 7)
/** Enable Host interrupt mask */
#define HIM_ENABLE			(UP_LD_HOST_INT_MASK   | \
					DN_LD_HOST_INT_MASK    | \
					CMD_PORT_UPLD_INT_MASK | \
					CMD_PORT_DNLD_INT_MASK)
/** Disable Host interrupt mask */
#define	HIM_DISABLE			0xff

/** Host Control Registers : Host interrupt status */
#define HOST_INT_STATUS_REG		0x0C

/** Host Control Registers : Upload command port host interrupt status */
#define UP_LD_CMD_PORT_HOST_INT_STATUS		(0x40U)
/** Host Control Registers : Download command port host interrupt status */
#define DN_LD_CMD_PORT_HOST_INT_STATUS		(0x80U)

/** Host Control Registers : Upload host interrupt status */
#define UP_LD_HOST_INT_STATUS		(0x1U)
/** Host Control Registers : Download host interrupt status */
#define DN_LD_HOST_INT_STATUS		(0x2U)

/** Port for registers */
#define REG_PORT			0
/** Port for memory */
#define MEM_PORT			0x10000
/** LSB of read bitmap */
#define RD_BITMAP_L			0x10
/** MSB of read bitmap */
#define RD_BITMAP_U			0x11
/** LSB of read bitmap second word */
#define RD_BITMAP_1L		0x12
/** MSB of read bitmap second word */
#define RD_BITMAP_1U		0x13
/** LSB of write bitmap */
#define WR_BITMAP_L			0x14
/** MSB of write bitmap */
#define WR_BITMAP_U			0x15
/** LSB of write bitmap second word */
#define WR_BITMAP_1L		0x16
/** MSB of write bitmap second word */
#define WR_BITMAP_1U		0x17
/** LSB of read length for port 0 */
#define RD_LEN_P0_L			0x18
/** MSB of read length for port 0 */
#define RD_LEN_P0_U			0x19

/* Card Control Registers : Command port read length 0 */
#define CMD_RD_LEN_0   0xC0
/* Card Control Registers : Command port read length 1 */
#define CMD_RD_LEN_1   0xC1
/* Card Control Registers : Command port read length 2 (reserved)  */
#define CMD_RD_LEN_2   0xC2
/* Card Control Registers : Command port read length 3  */
#define CMD_RD_LEN_3   0xC3
/* Card Control Registers : Command port configuration 0 */
#define CMD_CONFIG_0   0xC4
#define CMD_PORT_RD_LEN_EN            (0x1U << 2)
/* Card Control Registers : Command port configuration 1 */
#define CMD_CONFIG_1   0xC5
/* Card Control Registers : cmd port auto enable */
#define CMD_PORT_AUTO_EN              (0x1U << 0)
/* Card Control Registers : Command port configuration 2 (reserved) */
#define CMD_CONFIG_2   0xC6
/* Card Control Registers : Command port configuration 3 (reserved) */
#define CMD_CONFIG_3   0xC7

/* Command port */
#define CMD_PORT_SLCT			0x8000
/** Data port mask */
#define DATA_PORT_MASK			0xffffffff

/** Misc. Config Register : Auto Re-enable interrupts */
#define AUTO_RE_ENABLE_INT		MBIT(4)

/** Host Control Registers : Host transfer status */
#define HOST_RESTART_REG		0x58
/** Host Control Registers : Download CRC error */
#define DN_LD_CRC_ERR			(0x1U << 2)
/** Host Control Registers : Upload restart */
#define UP_LD_RESTART			(0x1U << 1)
/** Host Control Registers : Download restart */
#define DN_LD_RESTART			(0x1U << 0)

/* Card Control Registers */
/** Card Control Registers : Card to host event */
#define CARD_TO_HOST_EVENT_REG		0x5C
/** Card Control Registers : Command port upload ready */
#define UP_LD_CP_RDY			(0x1U << 6)
/** Card Control Registers : Command port download ready */
#define DN_LD_CP_RDY			(0x1U << 7)
/** Card Control Registers : Card I/O ready */
#define CARD_IO_READY			(0x1U << 3)
/** Card Control Registers : CIS card ready */
#define CIS_CARD_RDY			(0x1U << 2)
/** Card Control Registers : Upload card ready */
#define UP_LD_CARD_RDY			(0x1U << 1)
/** Card Control Registers : Download card ready */
#define DN_LD_CARD_RDY			(0x1U << 0)

/** Card Control Registers : Host interrupt mask register */
#define HOST_INTERRUPT_MASK_REG		0x60
/** Card Control Registers : Host power interrupt mask */
#define HOST_POWER_INT_MASK		(0x1U << 3)
/** Card Control Registers : Abort card interrupt mask */
#define ABORT_CARD_INT_MASK		(0x1U << 2)
/** Card Control Registers : Upload card interrupt mask */
#define UP_LD_CARD_INT_MASK		(0x1U << 1)
/** Card Control Registers : Download card interrupt mask */
#define DN_LD_CARD_INT_MASK		(0x1U << 0)

/** Card Control Registers : Card interrupt status register */
#define CARD_INTERRUPT_STATUS_REG	0x64
/** Card Control Registers : Power up interrupt */
#define POWER_UP_INT			(0x1U << 4)
/** Card Control Registers : Power down interrupt */
#define POWER_DOWN_INT			(0x1U << 3)

/** Card Control Registers : Card interrupt RSR register */
#define CARD_INTERRUPT_RSR_REG		0x68
/** Card Control Registers : Power up RSR */
#define POWER_UP_RSR			(0x1U << 4)
/** Card Control Registers : Power down RSR */
#define POWER_DOWN_RSR			(0x1U << 3)

/** Card Control Registers : SQ Read base address 0 register */
#define READ_BASE_0_REG			0xf8
/** Card Control Registers : SQ Read base address 1 register */
#define READ_BASE_1_REG			0xf9
/** Enable GPIO-1 as a duplicated signal of interrupt as appear of SDIO_DAT1*/
#define ENABLE_GPIO_1_INT_MODE  0x88
/** Scratch reg 3 2  :     Configure GPIO-1 INT*/
#define SCRATCH_REG_32          0xEE

/** Card Control Registers : Card revision register */
#define CARD_REVISION_REG		0xC8

/** Firmware status 0 register (SCRATCH0_0) */
#define CARD_FW_STATUS0_REG		0xe8
/** Firmware status 1 register (SCRATCH0_1) */
#define CARD_FW_STATUS1_REG		0xe9
/** Rx length register (SCRATCH0_2) */
#define CARD_RX_LEN_REG			0xea
/** Rx unit register (SCRATCH0_3) */
#define CARD_RX_UNIT_REG		0xeb

/** Card Control Registers : Card OCR 0 register */
#define CARD_OCR_0_REG			0xD4
/** Card Control Registers : Card OCR 1 register */
#define CARD_OCR_1_REG			0xD5
/** Card Control Registers : Card OCR 3 register */
#define CARD_OCR_3_REG			0xD6
/** Card Control Registers : Card config register */
#define CARD_CONFIG_REG			0xD7
/** Card Control Registers : Miscellaneous Configuration Register */
#define CARD_MISC_CFG_REG		0xD8

/** Card Control Registers : sdio new mode register 1 */
#define CARD_CONFIG_2_1_REG             0xD9
/** Card Control Registers : cmd53 new mode */
#define CMD53_NEW_MODE			(0x1U << 0)
/** Card Control Registers : cmd53 tx len format 1 (0x10) */
#define CMD53_TX_LEN_FORMAT_1           (0x1U << 4)
/** Card Control Registers : cmd53 tx len format 2 (0x20)*/
#define CMD53_TX_LEN_FORMAT_2           (0x1U << 5)
/** Card Control Registers : cmd53 rx len format 1 (0x40) */
#define CMD53_RX_LEN_FORMAT_1           (0x1U << 6)
/** Card Control Registers : cmd53 rx len format 2 (0x80)*/
#define CMD53_RX_LEN_FORMAT_2           (0x1U << 7)

/** Card Control Registers : sdio new mode register 2 */
#define CARD_CONFIG_2_2_REG             0xDA
/** Card Control Registers : test data out (0x01) */
#define TEST_DATA_OUT_1           (0x1U << 0)
/** Card Control Registers : test data out (0x02) */
#define TEST_DATA_OUT_2           (0x1U << 1)
/** Card Control Registers : test data out (0x04) */
#define TEST_DATA_OUT_3           (0x1U << 2)
/** Card Control Registers : test data out (0x08) */
#define TEST_DATA_OUT_4           (0x1U << 3)
/** Card Control Registers : test cmd out (0x10) */
#define TEST_CMD_OUT			(0x1U << 4)

/** Card Control Registers : sdio new mode register 3 */
#define CARD_CONFIG_2_3_REG             0xDB
/** Card Control Registers : test data enable (0x01) */
#define TEST_DATA_EN_1           (0x1U << 0)
/** Card Control Registers : test data enable (0x02) */
#define TEST_DATA_EN_2           (0x1U << 1)
/** Card Control Registers : test data enable (0x04) */
#define TEST_DATA_EN_3           (0x1U << 2)
/** Card Control Registers : test data enable (0x08) */
#define TEST_DATA_EN_4           (0x1U << 3)
/** Card Control Registers : test cmd enable (0x10) */
#define TEST_CMD_EN		(0x1U << 4)
/** Card Control Registers : test mode (0x20) */
#define TEST_MODE			(0x1U << 5)

/** Card Control Registers : Debug 0 register */
#define DEBUG_0_REG			0xDC
/** Card Control Registers : SD test BUS 0 */
#define SD_TESTBUS0			(0x1U)
/** Card Control Registers : Debug 1 register */
#define DEBUG_1_REG			0xDD
/** Card Control Registers : SD test BUS 1 */
#define SD_TESTBUS1			(0x1U)
/** Card Control Registers : Debug 2 register */
#define DEBUG_2_REG			0xDE
/** Card Control Registers : SD test BUS 2 */
#define SD_TESTBUS2			(0x1U)
/** Card Control Registers : Debug 3 register */
#define DEBUG_3_REG			0xDF
/** Card Control Registers : SD test BUS 3 */
#define SD_TESTBUS3			(0x1U)

/** Host Control Registers : I/O port 0 */
#define IO_PORT_0_REG			0xE4
/** Host Control Registers : I/O port 1 */
#define IO_PORT_1_REG			0xE5
/** Host Control Registers : I/O port 2 */
#define IO_PORT_2_REG			0xE6

#define FW_RESET_REG  0x0EE
#define FW_RESET_VAL  0x99

/** Event header Len*/
#define MLAN_EVENT_HEADER_LEN           8

/** SDIO byte mode size */
#define MAX_BYTE_MODE_SIZE             512

#if defined(SDIO_MULTI_PORT_TX_AGGR) || defined(SDIO_MULTI_PORT_RX_AGGR)
/** The base address for packet with multiple ports aggregation */
#define SDIO_MPA_ADDR_BASE             0x1000
#endif

#ifdef SDIO_MULTI_PORT_TX_AGGR

/** SDIO Tx aggregation in progress ? */
#define MP_TX_AGGR_IN_PROGRESS(a) (a->mpa_tx.pkt_cnt > 0)

/** SDIO Tx aggregation buffer room for next packet ? */
#define MP_TX_AGGR_BUF_HAS_ROOM(a, mbuf, len) \
			(((a->mpa_tx.buf_len) + len) <= (a->mpa_tx.buf_size))

/** Copy current packet (SDIO Tx aggregation buffer) to SDIO buffer */
#define MP_TX_AGGR_BUF_PUT(a, mbuf, port) do {          \
	pmadapter->callbacks.moal_memmove(a->pmoal_handle, \
		&a->mpa_tx.buf[a->mpa_tx.buf_len], \
		mbuf->pbuf+mbuf->data_offset, mbuf->data_len);\
	a->mpa_tx.buf_len += mbuf->data_len;                \
	a->mpa_tx.mp_wr_info[a->mpa_tx.pkt_cnt] = \
		*(t_u16 *)(mbuf->pbuf+mbuf->data_offset); \
	if (!a->mpa_tx.pkt_cnt) {                           \
		a->mpa_tx.start_port = port;                    \
	}                                                   \
	a->mpa_tx.ports |= (1 << port);                     \
	a->mpa_tx.pkt_cnt++;                                \
} while (0)

#define MP_TX_AGGR_BUF_PUT_SG(a, mbuf, port) do {       \
	a->mpa_tx.buf_len += mbuf->data_len;                \
	a->mpa_tx.mp_wr_info[a->mpa_tx.pkt_cnt] = \
		*(t_u16 *)(mbuf->pbuf+mbuf->data_offset); \
	a->mpa_tx.mbuf_arr[a->mpa_tx.pkt_cnt] = mbuf;       \
	if (!a->mpa_tx.pkt_cnt) {                           \
		a->mpa_tx.start_port = port;                    \
	}                                                   \
	a->mpa_tx.ports |= (1 << port);                     \
	a->mpa_tx.pkt_cnt++;                                \
} while (0)
/** SDIO Tx aggregation limit ? */
#define MP_TX_AGGR_PKT_LIMIT_REACHED(a) ((a->mpa_tx.pkt_cnt) \
			== (a->mpa_tx.pkt_aggr_limit))

/** SDIO Tx aggregation port limit ? */
#define MP_TX_AGGR_PORT_LIMIT_REACHED(a)    (MFALSE)

/** Reset SDIO Tx aggregation buffer parameters */
#define MP_TX_AGGR_BUF_RESET(a) do {         \
	memset(a, a->mpa_tx.mp_wr_info, 0, sizeof(a->mpa_tx.mp_wr_info)); \
	a->mpa_tx.pkt_cnt = 0;                   \
	a->mpa_tx.buf_len = 0;                   \
	a->mpa_tx.ports = 0;                     \
	a->mpa_tx.start_port = 0;                \
} while (0)

#endif /* SDIO_MULTI_PORT_TX_AGGR */

#ifdef SDIO_MULTI_PORT_RX_AGGR

/** SDIO Rx aggregation limit ? */
#define MP_RX_AGGR_PKT_LIMIT_REACHED(a) (a->mpa_rx.pkt_cnt \
		== a->mpa_rx.pkt_aggr_limit)

/** SDIO Rx aggregation port limit ? */
/** this is for test only, because port 0 is reserved for control port */
/* #define MP_RX_AGGR_PORT_LIMIT_REACHED(a) (a->curr_rd_port == 1) */

/* receive packets aggregated up to a half of mp_end_port */
/* note: hw rx wraps round only after port (MAX_PORT-1) */
#define MP_RX_AGGR_PORT_LIMIT_REACHED(a) \
		(((a->curr_rd_port < a->mpa_rx.start_port) && \
		(((MAX_PORT - a->mpa_rx.start_port) + a->curr_rd_port) \
		>= (a->mp_end_port >> 1))) || \
		((a->curr_rd_port - a->mpa_rx.start_port) >= \
		(a->mp_end_port >> 1)))

/** SDIO Rx aggregation in progress ? */
#define MP_RX_AGGR_IN_PROGRESS(a) (a->mpa_rx.pkt_cnt > 0)

/** SDIO Rx aggregation buffer room for next packet ? */
#define MP_RX_AGGR_BUF_HAS_ROOM(a, rx_len)   \
	((a->mpa_rx.buf_len + rx_len) <= a->mpa_rx.buf_size)

/** Prepare to copy current packet from card to SDIO Rx aggregation buffer */
#define MP_RX_AGGR_SETUP(a, mbuf, port, rx_len) do {   \
	a->mpa_rx.buf_len += rx_len;                       \
	if (!a->mpa_rx.pkt_cnt) {                          \
		a->mpa_rx.start_port = port;                   \
	}                                                  \
	a->mpa_rx.ports |= (1 << port);                    \
	a->mpa_rx.mbuf_arr[a->mpa_rx.pkt_cnt] = mbuf;      \
	a->mpa_rx.len_arr[a->mpa_rx.pkt_cnt] = rx_len;     \
	a->mpa_rx.pkt_cnt++;                              \
} while (0)

/** Reset SDIO Rx aggregation buffer parameters */
#define MP_RX_AGGR_BUF_RESET(a) do {         \
	a->mpa_rx.pkt_cnt = 0;                   \
	a->mpa_rx.buf_len = 0;                   \
	a->mpa_rx.ports = 0;                     \
	a->mpa_rx.start_port = 0;                \
} while (0)

#endif /* SDIO_MULTI_PORT_RX_AGGR */

/** Enable host interrupt */
mlan_status wlan_enable_host_int(pmlan_adapter pmadapter);
/** Probe and initialization function */
mlan_status wlan_sdio_probe(pmlan_adapter pmadapter);
/** multi interface download check */
mlan_status wlan_check_winner_status(mlan_adapter *pmadapter, t_u32 *val);

#ifdef SDIO_MULTI_PORT_TX_AGGR
mlan_status wlan_send_mp_aggr_buf(mlan_adapter *pmadapter);
#endif

#if defined(SDIO_MULTI_PORT_RX_AGGR)
mlan_status wlan_re_alloc_sdio_rx_mpa_buffer(IN mlan_adapter *pmadapter);
#endif

void wlan_decode_spa_buffer(mlan_adapter *pmadapter, t_u8 *buf, t_u32 len);
t_void wlan_sdio_deaggr_rx_pkt(IN pmlan_adapter pmadapter, mlan_buffer *pmbuf);
/** Firmware status check */
mlan_status wlan_check_fw_status(mlan_adapter *pmadapter, t_u32 pollnum);
/** Read interrupt status */
mlan_status wlan_interrupt(pmlan_adapter pmadapter);
/** Process Interrupt Status */
mlan_status wlan_process_int_status(mlan_adapter *pmadapter);
/** Transfer data to card */
mlan_status wlan_sdio_host_to_card(mlan_adapter *pmadapter, t_u8 type,
				   mlan_buffer *mbuf, mlan_tx_param *tx_param);
mlan_status wlan_set_sdio_gpio_int(IN pmlan_private priv);
mlan_status wlan_cmd_sdio_gpio_int(pmlan_private pmpriv,
				   IN HostCmd_DS_COMMAND *cmd,
				   IN t_u16 cmd_action, IN t_void *pdata_buf);
mlan_status wlan_reset_fw(pmlan_adapter pmadapter);
#endif /* _MLAN_SDIO_H */
