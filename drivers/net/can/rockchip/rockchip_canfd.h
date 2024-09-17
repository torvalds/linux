/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2023, 2024 Pengutronix,
 *               Marc Kleine-Budde <kernel@pengutronix.de>
 */

#ifndef _ROCKCHIP_CANFD_H
#define _ROCKCHIP_CANFD_H

#include <linux/bitfield.h>
#include <linux/can/dev.h>
#include <linux/can/rx-offload.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/reset.h>
#include <linux/skbuff.h>
#include <linux/timecounter.h>
#include <linux/types.h>
#include <linux/u64_stats_sync.h>
#include <linux/units.h>

#define RKCANFD_REG_MODE 0x000
#define RKCANFD_REG_MODE_CAN_FD_MODE_ENABLE BIT(15)
#define RKCANFD_REG_MODE_DPEE BIT(14)
#define RKCANFD_REG_MODE_BRSD BIT(13)
#define RKCANFD_REG_MODE_SPACE_RX_MODE BIT(12)
#define RKCANFD_REG_MODE_AUTO_BUS_ON BIT(11)
#define RKCANFD_REG_MODE_AUTO_RETX_MODE BIT(10)
#define RKCANFD_REG_MODE_OVLD_MODE BIT(9)
#define RKCANFD_REG_MODE_COVER_MODE BIT(8)
#define RKCANFD_REG_MODE_RXSORT_MODE BIT(7)
#define RKCANFD_REG_MODE_TXORDER_MODE BIT(6)
#define RKCANFD_REG_MODE_RXSTX_MODE BIT(5)
#define RKCANFD_REG_MODE_LBACK_MODE BIT(4)
#define RKCANFD_REG_MODE_SILENT_MODE BIT(3)
#define RKCANFD_REG_MODE_SELF_TEST BIT(2)
#define RKCANFD_REG_MODE_SLEEP_MODE BIT(1)
#define RKCANFD_REG_MODE_WORK_MODE BIT(0)

#define RKCANFD_REG_CMD 0x004
#define RKCANFD_REG_CMD_TX1_REQ BIT(1)
#define RKCANFD_REG_CMD_TX0_REQ BIT(0)
#define RKCANFD_REG_CMD_TX_REQ(i) (RKCANFD_REG_CMD_TX0_REQ << (i))

#define RKCANFD_REG_STATE 0x008
#define RKCANFD_REG_STATE_SLEEP_STATE BIT(6)
#define RKCANFD_REG_STATE_BUS_OFF_STATE BIT(5)
#define RKCANFD_REG_STATE_ERROR_WARNING_STATE BIT(4)
#define RKCANFD_REG_STATE_TX_PERIOD BIT(3)
#define RKCANFD_REG_STATE_RX_PERIOD BIT(2)
#define RKCANFD_REG_STATE_TX_BUFFER_FULL BIT(1)
#define RKCANFD_REG_STATE_RX_BUFFER_FULL BIT(0)

#define RKCANFD_REG_INT 0x00c
#define RKCANFD_REG_INT_WAKEUP_INT BIT(14)
#define RKCANFD_REG_INT_TXE_FIFO_FULL_INT BIT(13)
#define RKCANFD_REG_INT_TXE_FIFO_OV_INT BIT(12)
#define RKCANFD_REG_INT_TIMESTAMP_COUNTER_OVERFLOW_INT BIT(11)
#define RKCANFD_REG_INT_BUS_OFF_RECOVERY_INT BIT(10)
#define RKCANFD_REG_INT_BUS_OFF_INT BIT(9)
#define RKCANFD_REG_INT_RX_FIFO_OVERFLOW_INT BIT(8)
#define RKCANFD_REG_INT_RX_FIFO_FULL_INT BIT(7)
#define RKCANFD_REG_INT_ERROR_INT BIT(6)
#define RKCANFD_REG_INT_TX_ARBIT_FAIL_INT BIT(5)
#define RKCANFD_REG_INT_PASSIVE_ERROR_INT BIT(4)
#define RKCANFD_REG_INT_OVERLOAD_INT BIT(3)
#define RKCANFD_REG_INT_ERROR_WARNING_INT BIT(2)
#define RKCANFD_REG_INT_TX_FINISH_INT BIT(1)
#define RKCANFD_REG_INT_RX_FINISH_INT BIT(0)

#define RKCANFD_REG_INT_ALL \
	(RKCANFD_REG_INT_WAKEUP_INT | \
	 RKCANFD_REG_INT_TXE_FIFO_FULL_INT | \
	 RKCANFD_REG_INT_TXE_FIFO_OV_INT | \
	 RKCANFD_REG_INT_TIMESTAMP_COUNTER_OVERFLOW_INT | \
	 RKCANFD_REG_INT_BUS_OFF_RECOVERY_INT | \
	 RKCANFD_REG_INT_BUS_OFF_INT | \
	 RKCANFD_REG_INT_RX_FIFO_OVERFLOW_INT | \
	 RKCANFD_REG_INT_RX_FIFO_FULL_INT | \
	 RKCANFD_REG_INT_ERROR_INT | \
	 RKCANFD_REG_INT_TX_ARBIT_FAIL_INT | \
	 RKCANFD_REG_INT_PASSIVE_ERROR_INT | \
	 RKCANFD_REG_INT_OVERLOAD_INT | \
	 RKCANFD_REG_INT_ERROR_WARNING_INT | \
	 RKCANFD_REG_INT_TX_FINISH_INT | \
	 RKCANFD_REG_INT_RX_FINISH_INT)

#define RKCANFD_REG_INT_ALL_ERROR \
	(RKCANFD_REG_INT_BUS_OFF_INT | \
	 RKCANFD_REG_INT_ERROR_INT | \
	 RKCANFD_REG_INT_PASSIVE_ERROR_INT | \
	 RKCANFD_REG_INT_ERROR_WARNING_INT)

#define RKCANFD_REG_INT_MASK 0x010

#define RKCANFD_REG_DMA_CTL 0x014
#define RKCANFD_REG_DMA_CTL_DMA_RX_MODE BIT(1)
#define RKCANFD_REG_DMA_CTL_DMA_TX_MODE BIT(9)

#define RKCANFD_REG_BITTIMING 0x018
#define RKCANFD_REG_BITTIMING_SAMPLE_MODE BIT(16)
#define RKCANFD_REG_BITTIMING_SJW GENMASK(15, 14)
#define RKCANFD_REG_BITTIMING_BRP GENMASK(13, 8)
#define RKCANFD_REG_BITTIMING_TSEG2 GENMASK(6, 4)
#define RKCANFD_REG_BITTIMING_TSEG1 GENMASK(3, 0)

#define RKCANFD_REG_ARBITFAIL 0x028
#define RKCANFD_REG_ARBITFAIL_ARBIT_FAIL_CODE GENMASK(6, 0)

/* Register seems to be clear or read */
#define RKCANFD_REG_ERROR_CODE 0x02c
#define RKCANFD_REG_ERROR_CODE_PHASE BIT(29)
#define RKCANFD_REG_ERROR_CODE_TYPE GENMASK(28, 26)
#define RKCANFD_REG_ERROR_CODE_TYPE_BIT 0x0
#define RKCANFD_REG_ERROR_CODE_TYPE_STUFF 0x1
#define RKCANFD_REG_ERROR_CODE_TYPE_FORM 0x2
#define RKCANFD_REG_ERROR_CODE_TYPE_ACK 0x3
#define RKCANFD_REG_ERROR_CODE_TYPE_CRC 0x4
#define RKCANFD_REG_ERROR_CODE_DIRECTION_RX BIT(25)
#define RKCANFD_REG_ERROR_CODE_TX GENMASK(24, 16)
#define RKCANFD_REG_ERROR_CODE_TX_OVERLOAD BIT(24)
#define RKCANFD_REG_ERROR_CODE_TX_ERROR BIT(23)
#define RKCANFD_REG_ERROR_CODE_TX_ACK BIT(22)
#define RKCANFD_REG_ERROR_CODE_TX_ACK_EOF BIT(21)
#define RKCANFD_REG_ERROR_CODE_TX_CRC BIT(20)
#define RKCANFD_REG_ERROR_CODE_TX_STUFF_COUNT BIT(19)
#define RKCANFD_REG_ERROR_CODE_TX_DATA BIT(18)
#define RKCANFD_REG_ERROR_CODE_TX_SOF_DLC BIT(17)
#define RKCANFD_REG_ERROR_CODE_TX_IDLE BIT(16)
#define RKCANFD_REG_ERROR_CODE_RX GENMASK(15, 0)
#define RKCANFD_REG_ERROR_CODE_RX_BUF_INT BIT(15)
#define RKCANFD_REG_ERROR_CODE_RX_SPACE BIT(14)
#define RKCANFD_REG_ERROR_CODE_RX_EOF BIT(13)
#define RKCANFD_REG_ERROR_CODE_RX_ACK_LIM BIT(12)
#define RKCANFD_REG_ERROR_CODE_RX_ACK BIT(11)
#define RKCANFD_REG_ERROR_CODE_RX_CRC_LIM BIT(10)
#define RKCANFD_REG_ERROR_CODE_RX_CRC BIT(9)
#define RKCANFD_REG_ERROR_CODE_RX_STUFF_COUNT BIT(8)
#define RKCANFD_REG_ERROR_CODE_RX_DATA BIT(7)
#define RKCANFD_REG_ERROR_CODE_RX_DLC BIT(6)
#define RKCANFD_REG_ERROR_CODE_RX_BRS_ESI BIT(5)
#define RKCANFD_REG_ERROR_CODE_RX_RES BIT(4)
#define RKCANFD_REG_ERROR_CODE_RX_FDF BIT(3)
#define RKCANFD_REG_ERROR_CODE_RX_ID2_RTR BIT(2)
#define RKCANFD_REG_ERROR_CODE_RX_SOF_IDE BIT(1)
#define RKCANFD_REG_ERROR_CODE_RX_IDLE BIT(0)

#define RKCANFD_REG_ERROR_CODE_NOACK \
	(FIELD_PREP(RKCANFD_REG_ERROR_CODE_TYPE, \
		    RKCANFD_REG_ERROR_CODE_TYPE_ACK) | \
	 RKCANFD_REG_ERROR_CODE_TX_ACK_EOF | \
	 RKCANFD_REG_ERROR_CODE_RX_ACK)

#define RKCANFD_REG_RXERRORCNT 0x034
#define RKCANFD_REG_RXERRORCNT_RX_ERR_CNT GENMASK(7, 0)

#define RKCANFD_REG_TXERRORCNT 0x038
#define RKCANFD_REG_TXERRORCNT_TX_ERR_CNT GENMASK(8, 0)

#define RKCANFD_REG_IDCODE 0x03c
#define RKCANFD_REG_IDCODE_STANDARD_FRAME_ID GENMASK(10, 0)
#define RKCANFD_REG_IDCODE_EXTENDED_FRAME_ID GENMASK(28, 0)

#define RKCANFD_REG_IDMASK 0x040

#define RKCANFD_REG_TXFRAMEINFO 0x050
#define RKCANFD_REG_FRAMEINFO_FRAME_FORMAT BIT(7)
#define RKCANFD_REG_FRAMEINFO_RTR BIT(6)
#define RKCANFD_REG_FRAMEINFO_DATA_LENGTH GENMASK(3, 0)

#define RKCANFD_REG_TXID 0x054
#define RKCANFD_REG_TXID_TX_ID GENMASK(28, 0)

#define RKCANFD_REG_TXDATA0 0x058
#define RKCANFD_REG_TXDATA1 0x05C
#define RKCANFD_REG_RXFRAMEINFO 0x060
#define RKCANFD_REG_RXID 0x064
#define RKCANFD_REG_RXDATA0 0x068
#define RKCANFD_REG_RXDATA1 0x06c

#define RKCANFD_REG_RTL_VERSION 0x070
#define RKCANFD_REG_RTL_VERSION_MAJOR GENMASK(7, 4)
#define RKCANFD_REG_RTL_VERSION_MINOR GENMASK(3, 0)

#define RKCANFD_REG_FD_NOMINAL_BITTIMING 0x100
#define RKCANFD_REG_FD_NOMINAL_BITTIMING_SAMPLE_MODE BIT(31)
#define RKCANFD_REG_FD_NOMINAL_BITTIMING_SJW GENMASK(30, 24)
#define RKCANFD_REG_FD_NOMINAL_BITTIMING_BRP GENMASK(23, 16)
#define RKCANFD_REG_FD_NOMINAL_BITTIMING_TSEG2 GENMASK(14, 8)
#define RKCANFD_REG_FD_NOMINAL_BITTIMING_TSEG1 GENMASK(7, 0)

#define RKCANFD_REG_FD_DATA_BITTIMING 0x104
#define RKCANFD_REG_FD_DATA_BITTIMING_SAMPLE_MODE BIT(21)
#define RKCANFD_REG_FD_DATA_BITTIMING_SJW GENMASK(20, 17)
#define RKCANFD_REG_FD_DATA_BITTIMING_BRP GENMASK(16, 9)
#define RKCANFD_REG_FD_DATA_BITTIMING_TSEG2 GENMASK(8, 5)
#define RKCANFD_REG_FD_DATA_BITTIMING_TSEG1 GENMASK(4, 0)

#define RKCANFD_REG_TRANSMIT_DELAY_COMPENSATION 0x108
#define RKCANFD_REG_TRANSMIT_DELAY_COMPENSATION_TDC_OFFSET GENMASK(6, 1)
#define RKCANFD_REG_TRANSMIT_DELAY_COMPENSATION_TDC_ENABLE BIT(0)

#define RKCANFD_REG_TIMESTAMP_CTRL 0x10c
/* datasheet says 6:1, which is wrong */
#define RKCANFD_REG_TIMESTAMP_CTRL_TIME_BASE_COUNTER_PRESCALE GENMASK(5, 1)
#define RKCANFD_REG_TIMESTAMP_CTRL_TIME_BASE_COUNTER_ENABLE BIT(0)

#define RKCANFD_REG_TIMESTAMP 0x110

#define RKCANFD_REG_TXEVENT_FIFO_CTRL 0x114
#define RKCANFD_REG_TXEVENT_FIFO_CTRL_TXE_FIFO_CNT GENMASK(8, 5)
#define RKCANFD_REG_TXEVENT_FIFO_CTRL_TXE_FIFO_WATERMARK GENMASK(4, 1)
#define RKCANFD_REG_TXEVENT_FIFO_CTRL_TXE_FIFO_ENABLE BIT(0)

#define RKCANFD_REG_RX_FIFO_CTRL 0x118
#define RKCANFD_REG_RX_FIFO_CTRL_RX_FIFO_CNT GENMASK(6, 4)
#define RKCANFD_REG_RX_FIFO_CTRL_RX_FIFO_FULL_WATERMARK GENMASK(3, 1)
#define RKCANFD_REG_RX_FIFO_CTRL_RX_FIFO_ENABLE BIT(0)

#define RKCANFD_REG_AFC_CTRL 0x11c
#define RKCANFD_REG_AFC_CTRL_UAF5 BIT(4)
#define RKCANFD_REG_AFC_CTRL_UAF4 BIT(3)
#define RKCANFD_REG_AFC_CTRL_UAF3 BIT(2)
#define RKCANFD_REG_AFC_CTRL_UAF2 BIT(1)
#define RKCANFD_REG_AFC_CTRL_UAF1 BIT(0)

#define RKCANFD_REG_IDCODE0 0x120
#define RKCANFD_REG_IDMASK0 0x124
#define RKCANFD_REG_IDCODE1 0x128
#define RKCANFD_REG_IDMASK1 0x12c
#define RKCANFD_REG_IDCODE2 0x130
#define RKCANFD_REG_IDMASK2 0x134
#define RKCANFD_REG_IDCODE3 0x138
#define RKCANFD_REG_IDMASK3 0x13c
#define RKCANFD_REG_IDCODE4 0x140
#define RKCANFD_REG_IDMASK4 0x144

#define RKCANFD_REG_FD_TXFRAMEINFO 0x200
#define RKCANFD_REG_FD_FRAMEINFO_FRAME_FORMAT BIT(7)
#define RKCANFD_REG_FD_FRAMEINFO_RTR BIT(6)
#define RKCANFD_REG_FD_FRAMEINFO_FDF BIT(5)
#define RKCANFD_REG_FD_FRAMEINFO_BRS BIT(4)
#define RKCANFD_REG_FD_FRAMEINFO_DATA_LENGTH GENMASK(3, 0)

#define RKCANFD_REG_FD_TXID 0x204
#define RKCANFD_REG_FD_ID_EFF GENMASK(28, 0)
#define RKCANFD_REG_FD_ID_SFF GENMASK(11, 0)

#define RKCANFD_REG_FD_TXDATA0 0x208
#define RKCANFD_REG_FD_TXDATA1 0x20c
#define RKCANFD_REG_FD_TXDATA2 0x210
#define RKCANFD_REG_FD_TXDATA3 0x214
#define RKCANFD_REG_FD_TXDATA4 0x218
#define RKCANFD_REG_FD_TXDATA5 0x21c
#define RKCANFD_REG_FD_TXDATA6 0x220
#define RKCANFD_REG_FD_TXDATA7 0x224
#define RKCANFD_REG_FD_TXDATA8 0x228
#define RKCANFD_REG_FD_TXDATA9 0x22c
#define RKCANFD_REG_FD_TXDATA10 0x230
#define RKCANFD_REG_FD_TXDATA11 0x234
#define RKCANFD_REG_FD_TXDATA12 0x238
#define RKCANFD_REG_FD_TXDATA13 0x23c
#define RKCANFD_REG_FD_TXDATA14 0x240
#define RKCANFD_REG_FD_TXDATA15 0x244

#define RKCANFD_REG_FD_RXFRAMEINFO 0x300
#define RKCANFD_REG_FD_RXID 0x304
#define RKCANFD_REG_FD_RXTIMESTAMP 0x308
#define RKCANFD_REG_FD_RXDATA0 0x30c
#define RKCANFD_REG_FD_RXDATA1 0x310
#define RKCANFD_REG_FD_RXDATA2 0x314
#define RKCANFD_REG_FD_RXDATA3 0x318
#define RKCANFD_REG_FD_RXDATA4 0x31c
#define RKCANFD_REG_FD_RXDATA5 0x320
#define RKCANFD_REG_FD_RXDATA6 0x320
#define RKCANFD_REG_FD_RXDATA7 0x328
#define RKCANFD_REG_FD_RXDATA8 0x32c
#define RKCANFD_REG_FD_RXDATA9 0x330
#define RKCANFD_REG_FD_RXDATA10 0x334
#define RKCANFD_REG_FD_RXDATA11 0x338
#define RKCANFD_REG_FD_RXDATA12 0x33c
#define RKCANFD_REG_FD_RXDATA13 0x340
#define RKCANFD_REG_FD_RXDATA14 0x344
#define RKCANFD_REG_FD_RXDATA15 0x348

#define RKCANFD_REG_RX_FIFO_RDATA 0x400
#define RKCANFD_REG_TXE_FIFO_RDATA 0x500

#define DEVICE_NAME "rockchip_canfd"
#define RKCANFD_NAPI_WEIGHT 32
#define RKCANFD_TXFIFO_DEPTH 2
#define RKCANFD_TX_STOP_THRESHOLD 1
#define RKCANFD_TX_START_THRESHOLD 1

#define RKCANFD_TIMESTAMP_WORK_MAX_DELAY_SEC 60
#define RKCANFD_ERRATUM_5_SYSCLOCK_HZ_MIN (300 * MEGA)

/* rk3568 CAN-FD Errata, as of Tue 07 Nov 2023 11:25:31 +08:00 */

/* Erratum 1: The error frame sent by the CAN controller has an
 * abnormal format.
 */
#define RKCANFD_QUIRK_RK3568_ERRATUM_1 BIT(0)

/* Erratum 2: The error frame sent after detecting a CRC error has an
 * abnormal position.
 */
#define RKCANFD_QUIRK_RK3568_ERRATUM_2 BIT(1)

/* Erratum 3: Intermittent CRC calculation errors. */
#define RKCANFD_QUIRK_RK3568_ERRATUM_3 BIT(2)

/* Erratum 4: Intermittent occurrence of stuffing errors. */
#define RKCANFD_QUIRK_RK3568_ERRATUM_4 BIT(3)

/* Erratum 5: Counters related to the TXFIFO and RXFIFO exhibit
 * abnormal counting behavior.
 *
 * The rk3568 CAN-FD errata sheet as of Tue 07 Nov 2023 11:25:31 +08:00
 * states that only the rk3568v2 is affected by this erratum, but
 * tests with the rk3568v2 and rk3568v3 show that the RX_FIFO_CNT is
 * sometimes too high. This leads to CAN frames being read from the
 * FIFO, which is then already empty.
 *
 * Further tests on the rk3568v2 and rk3568v3 show that in this
 * situation (i.e. empty FIFO) all elements of the FIFO header
 * (frameinfo, id, ts) contain the same data.
 *
 * On the rk3568v2 and rk3568v3, this problem only occurs extremely
 * rarely with the standard clock of 300 MHz, but almost immediately
 * at 80 MHz.
 *
 * To workaround this problem, check for empty FIFO with
 * rkcanfd_fifo_header_empty() in rkcanfd_handle_rx_int_one() and exit
 * early.
 *
 * To reproduce:
 * assigned-clocks = <&cru CLK_CANx>;
 * assigned-clock-rates = <80000000>;
 */
#define RKCANFD_QUIRK_RK3568_ERRATUM_5 BIT(4)

/* Erratum 6: The CAN controller's transmission of extended frames may
 * intermittently change into standard frames
 *
 * Work around this issue by activating self reception (RXSTX). If we
 * have pending TX CAN frames, check all RX'ed CAN frames in
 * rkcanfd_rxstx_filter().
 *
 * If it's a frame we've send and it's OK, call the TX complete
 * handler: rkcanfd_handle_tx_done_one(). Mask the TX complete IRQ.
 *
 * If it's a frame we've send, but the CAN-ID is mangled, resend the
 * original extended frame.
 *
 * To reproduce:
 * host:
 *   canfdtest -evx -g can0
 *   candump any,0:80000000 -cexdtA
 * dut:
 *   canfdtest -evx can0
 *   ethtool -S can0
 */
#define RKCANFD_QUIRK_RK3568_ERRATUM_6 BIT(5)

/* Erratum 7: In the passive error state, the CAN controller's
 * interframe space segment counting is inaccurate.
 */
#define RKCANFD_QUIRK_RK3568_ERRATUM_7 BIT(6)

/* Erratum 8: The Format-Error error flag is transmitted one bit
 * later.
 */
#define RKCANFD_QUIRK_RK3568_ERRATUM_8 BIT(7)

/* Erratum 9: In the arbitration segment, the CAN controller will
 * identify stuffing errors as arbitration failures.
 */
#define RKCANFD_QUIRK_RK3568_ERRATUM_9 BIT(8)

/* Erratum 10: Does not support the BUSOFF slow recovery mechanism. */
#define RKCANFD_QUIRK_RK3568_ERRATUM_10 BIT(9)

/* Erratum 11: Arbitration error. */
#define RKCANFD_QUIRK_RK3568_ERRATUM_11 BIT(10)

/* Erratum 12: A dominant bit at the third bit of the intermission may
 * cause a transmission error.
 */
#define RKCANFD_QUIRK_RK3568_ERRATUM_12 BIT(11)

/* Tests on the rk3568v2 and rk3568v3 show that receiving certain
 * CAN-FD frames trigger an Error Interrupt.
 *
 * - Form Error in RX Arbitration Phase: TX_IDLE RX_STUFF_COUNT (0x0a010100) CMD=0 RX=0 TX=0
 *   Error-Warning=1 Bus-Off=0
 *   To reproduce:
 *   host:
 *     cansend can0 002##01f
 *   DUT:
 *     candump any,0:0,#FFFFFFFF -cexdHtA
 *
 * - Form Error in RX Arbitration Phase: TX_IDLE RX_CRC (0x0a010200) CMD=0 RX=0 TX=0
 *   Error-Warning=1 Bus-Off=0
 *   To reproduce:
 *   host:
 *     cansend can0 002##07217010000000000
 *   DUT:
 *     candump any,0:0,#FFFFFFFF -cexdHtA
 */
#define RKCANFD_QUIRK_CANFD_BROKEN BIT(12)

/* known issues with rk3568v3:
 *
 * - Overload situation during high bus load
 *   To reproduce:
 *   host:
 *     # add a 2nd CAN adapter to the CAN bus
 *     cangen can0 -I 1 -Li -Di -p10 -g 0.3
 *     cansequence -rve
 *   DUT:
 *     cangen can0 -I2 -L1 -Di -p10 -c10 -g 1 -e
 *     cansequence -rv -i 1
 *
 * - TX starvation after repeated Bus-Off
 *   To reproduce:
 *   host:
 *     sleep 3 && cangen can0 -I2 -Li -Di -p10 -g 0.0
 *   DUT:
 *     cangen can0 -I2 -Li -Di -p10 -g 0.05
 */

enum rkcanfd_model {
	RKCANFD_MODEL_RK3568V2 = 0x35682,
	RKCANFD_MODEL_RK3568V3 = 0x35683,
};

struct rkcanfd_devtype_data {
	enum rkcanfd_model model;
	u32 quirks;
};

struct rkcanfd_fifo_header {
	u32 frameinfo;
	u32 id;
	u32 ts;
};

struct rkcanfd_stats {
	struct u64_stats_sync syncp;

	/* Erratum 5 */
	u64_stats_t rx_fifo_empty_errors;

	/* Erratum 6 */
	u64_stats_t tx_extended_as_standard_errors;
};

struct rkcanfd_priv {
	struct can_priv can;
	struct can_rx_offload offload;
	struct net_device *ndev;

	void __iomem *regs;
	unsigned int tx_head;
	unsigned int tx_tail;

	u32 reg_mode_default;
	u32 reg_int_mask_default;
	struct rkcanfd_devtype_data devtype_data;

	struct cyclecounter cc;
	struct timecounter tc;
	struct delayed_work timestamp;
	unsigned long work_delay_jiffies;

	struct can_berr_counter bec;

	struct rkcanfd_stats stats;

	struct reset_control *reset;
	struct clk_bulk_data *clks;
	int clks_num;
};

static inline u32
rkcanfd_read(const struct rkcanfd_priv *priv, u32 reg)
{
	return readl(priv->regs + reg);
}

static inline void
rkcanfd_read_rep(const struct rkcanfd_priv *priv, u32 reg,
		 void *buf, unsigned int len)
{
	readsl(priv->regs + reg, buf, len / sizeof(u32));
}

static inline void
rkcanfd_write(const struct rkcanfd_priv *priv, u32 reg, u32 val)
{
	writel(val, priv->regs + reg);
}

static inline u32
rkcanfd_get_timestamp(const struct rkcanfd_priv *priv)
{
	return rkcanfd_read(priv, RKCANFD_REG_TIMESTAMP);
}

static inline unsigned int
rkcanfd_get_tx_head(const struct rkcanfd_priv *priv)
{
	return READ_ONCE(priv->tx_head) & (RKCANFD_TXFIFO_DEPTH - 1);
}

static inline unsigned int
rkcanfd_get_tx_tail(const struct rkcanfd_priv *priv)
{
	return READ_ONCE(priv->tx_tail) & (RKCANFD_TXFIFO_DEPTH - 1);
}

static inline unsigned int
rkcanfd_get_tx_pending(const struct rkcanfd_priv *priv)
{
	return READ_ONCE(priv->tx_head) - READ_ONCE(priv->tx_tail);
}

static inline unsigned int
rkcanfd_get_tx_free(const struct rkcanfd_priv *priv)
{
	return RKCANFD_TXFIFO_DEPTH - rkcanfd_get_tx_pending(priv);
}

void rkcanfd_ethtool_init(struct rkcanfd_priv *priv);

int rkcanfd_handle_rx_int(struct rkcanfd_priv *priv);

void rkcanfd_skb_set_timestamp(const struct rkcanfd_priv *priv,
			       struct sk_buff *skb, const u32 timestamp);
void rkcanfd_timestamp_init(struct rkcanfd_priv *priv);
void rkcanfd_timestamp_start(struct rkcanfd_priv *priv);
void rkcanfd_timestamp_stop(struct rkcanfd_priv *priv);
void rkcanfd_timestamp_stop_sync(struct rkcanfd_priv *priv);

unsigned int rkcanfd_get_effective_tx_free(const struct rkcanfd_priv *priv);
void rkcanfd_xmit_retry(struct rkcanfd_priv *priv);
netdev_tx_t rkcanfd_start_xmit(struct sk_buff *skb, struct net_device *ndev);
void rkcanfd_handle_tx_done_one(struct rkcanfd_priv *priv, const u32 ts,
				unsigned int *frame_len_p);

#endif
