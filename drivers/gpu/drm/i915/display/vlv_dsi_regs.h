/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __VLV_DSI_REGS_H__
#define __VLV_DSI_REGS_H__

#include "i915_reg_defs.h"

#define VLV_MIPI_BASE			VLV_DISPLAY_BASE
#define BXT_MIPI_BASE			0x60000

#define _MIPI_PORT(port, a, c)	(((port) == PORT_A) ? a : c)	/* ports A and C only */
#define _MMIO_MIPI(port, a, c)	_MMIO(_MIPI_PORT(port, a, c))

/* BXT MIPI mode configure */
#define  _BXT_MIPIA_TRANS_HACTIVE			0x6B0F8
#define  _BXT_MIPIC_TRANS_HACTIVE			0x6B8F8
#define  BXT_MIPI_TRANS_HACTIVE(tc)	_MMIO_MIPI(tc, \
		_BXT_MIPIA_TRANS_HACTIVE, _BXT_MIPIC_TRANS_HACTIVE)

#define  _BXT_MIPIA_TRANS_VACTIVE			0x6B0FC
#define  _BXT_MIPIC_TRANS_VACTIVE			0x6B8FC
#define  BXT_MIPI_TRANS_VACTIVE(tc)	_MMIO_MIPI(tc, \
		_BXT_MIPIA_TRANS_VACTIVE, _BXT_MIPIC_TRANS_VACTIVE)

#define  _BXT_MIPIA_TRANS_VTOTAL			0x6B100
#define  _BXT_MIPIC_TRANS_VTOTAL			0x6B900
#define  BXT_MIPI_TRANS_VTOTAL(tc)	_MMIO_MIPI(tc, \
		_BXT_MIPIA_TRANS_VTOTAL, _BXT_MIPIC_TRANS_VTOTAL)

#define BXT_P_DSI_REGULATOR_CFG			_MMIO(0x160020)
#define  STAP_SELECT					(1 << 0)

#define BXT_P_DSI_REGULATOR_TX_CTRL		_MMIO(0x160054)
#define  HS_IO_CTRL_SELECT				(1 << 0)

#define _MIPIA_PORT_CTRL			(VLV_DISPLAY_BASE + 0x61190)
#define _MIPIC_PORT_CTRL			(VLV_DISPLAY_BASE + 0x61700)
#define MIPI_PORT_CTRL(port)	_MMIO_MIPI(port, _MIPIA_PORT_CTRL, _MIPIC_PORT_CTRL)

 /* BXT port control */
#define _BXT_MIPIA_PORT_CTRL				0x6B0C0
#define _BXT_MIPIC_PORT_CTRL				0x6B8C0
#define BXT_MIPI_PORT_CTRL(tc)	_MMIO_MIPI(tc, _BXT_MIPIA_PORT_CTRL, _BXT_MIPIC_PORT_CTRL)

#define  DPI_ENABLE					(1 << 31) /* A + C */
#define  MIPIA_MIPI4DPHY_DELAY_COUNT_SHIFT		27
#define  MIPIA_MIPI4DPHY_DELAY_COUNT_MASK		(0xf << 27)
#define  DUAL_LINK_MODE_SHIFT				26
#define  DUAL_LINK_MODE_MASK				(1 << 26)
#define  DUAL_LINK_MODE_FRONT_BACK			(0 << 26)
#define  DUAL_LINK_MODE_PIXEL_ALTERNATIVE		(1 << 26)
#define  DITHERING_ENABLE				(1 << 25) /* A + C */
#define  FLOPPED_HSTX					(1 << 23)
#define  DE_INVERT					(1 << 19) /* XXX */
#define  MIPIA_FLISDSI_DELAY_COUNT_SHIFT		18
#define  MIPIA_FLISDSI_DELAY_COUNT_MASK			(0xf << 18)
#define  AFE_LATCHOUT					(1 << 17)
#define  LP_OUTPUT_HOLD					(1 << 16)
#define  MIPIC_FLISDSI_DELAY_COUNT_HIGH_SHIFT		15
#define  MIPIC_FLISDSI_DELAY_COUNT_HIGH_MASK		(1 << 15)
#define  MIPIC_MIPI4DPHY_DELAY_COUNT_SHIFT		11
#define  MIPIC_MIPI4DPHY_DELAY_COUNT_MASK		(0xf << 11)
#define  CSB_SHIFT					9
#define  CSB_MASK					(3 << 9)
#define  CSB_20MHZ					(0 << 9)
#define  CSB_10MHZ					(1 << 9)
#define  CSB_40MHZ					(2 << 9)
#define  BANDGAP_MASK					(1 << 8)
#define  BANDGAP_PNW_CIRCUIT				(0 << 8)
#define  BANDGAP_LNC_CIRCUIT				(1 << 8)
#define  MIPIC_FLISDSI_DELAY_COUNT_LOW_SHIFT		5
#define  MIPIC_FLISDSI_DELAY_COUNT_LOW_MASK		(7 << 5)
#define  TEARING_EFFECT_DELAY				(1 << 4) /* A + C */
#define  TEARING_EFFECT_SHIFT				2 /* A + C */
#define  TEARING_EFFECT_MASK				(3 << 2)
#define  TEARING_EFFECT_OFF				(0 << 2)
#define  TEARING_EFFECT_DSI				(1 << 2)
#define  TEARING_EFFECT_GPIO				(2 << 2)
#define  LANE_CONFIGURATION_SHIFT			0
#define  LANE_CONFIGURATION_MASK			(3 << 0)
#define  LANE_CONFIGURATION_4LANE			(0 << 0)
#define  LANE_CONFIGURATION_DUAL_LINK_A			(1 << 0)
#define  LANE_CONFIGURATION_DUAL_LINK_B			(2 << 0)

#define _MIPIA_TEARING_CTRL			(VLV_DISPLAY_BASE + 0x61194)
#define _MIPIC_TEARING_CTRL			(VLV_DISPLAY_BASE + 0x61704)
#define MIPI_TEARING_CTRL(port)			_MMIO_MIPI(port, _MIPIA_TEARING_CTRL, _MIPIC_TEARING_CTRL)
#define  TEARING_EFFECT_DELAY_SHIFT			0
#define  TEARING_EFFECT_DELAY_MASK			(0xffff << 0)

/* XXX: all bits reserved */
#define _MIPIA_AUTOPWG			(VLV_DISPLAY_BASE + 0x611a0)

/* MIPI DSI Controller and D-PHY registers */

#define _MIPIA_DEVICE_READY		(dev_priv->mipi_mmio_base + 0xb000)
#define _MIPIC_DEVICE_READY		(dev_priv->mipi_mmio_base + 0xb800)
#define MIPI_DEVICE_READY(port)		_MMIO_MIPI(port, _MIPIA_DEVICE_READY, _MIPIC_DEVICE_READY)
#define  BUS_POSSESSION					(1 << 3) /* set to give bus to receiver */
#define  ULPS_STATE_MASK				(3 << 1)
#define  ULPS_STATE_ENTER				(2 << 1)
#define  ULPS_STATE_EXIT				(1 << 1)
#define  ULPS_STATE_NORMAL_OPERATION			(0 << 1)
#define  DEVICE_READY					(1 << 0)

#define _MIPIA_INTR_STAT		(dev_priv->mipi_mmio_base + 0xb004)
#define _MIPIC_INTR_STAT		(dev_priv->mipi_mmio_base + 0xb804)
#define MIPI_INTR_STAT(port)		_MMIO_MIPI(port, _MIPIA_INTR_STAT, _MIPIC_INTR_STAT)
#define _MIPIA_INTR_EN			(dev_priv->mipi_mmio_base + 0xb008)
#define _MIPIC_INTR_EN			(dev_priv->mipi_mmio_base + 0xb808)
#define MIPI_INTR_EN(port)		_MMIO_MIPI(port, _MIPIA_INTR_EN, _MIPIC_INTR_EN)
#define  TEARING_EFFECT					(1 << 31)
#define  SPL_PKT_SENT_INTERRUPT				(1 << 30)
#define  GEN_READ_DATA_AVAIL				(1 << 29)
#define  LP_GENERIC_WR_FIFO_FULL			(1 << 28)
#define  HS_GENERIC_WR_FIFO_FULL			(1 << 27)
#define  RX_PROT_VIOLATION				(1 << 26)
#define  RX_INVALID_TX_LENGTH				(1 << 25)
#define  ACK_WITH_NO_ERROR				(1 << 24)
#define  TURN_AROUND_ACK_TIMEOUT			(1 << 23)
#define  LP_RX_TIMEOUT					(1 << 22)
#define  HS_TX_TIMEOUT					(1 << 21)
#define  DPI_FIFO_UNDERRUN				(1 << 20)
#define  LOW_CONTENTION					(1 << 19)
#define  HIGH_CONTENTION				(1 << 18)
#define  TXDSI_VC_ID_INVALID				(1 << 17)
#define  TXDSI_DATA_TYPE_NOT_RECOGNISED			(1 << 16)
#define  TXCHECKSUM_ERROR				(1 << 15)
#define  TXECC_MULTIBIT_ERROR				(1 << 14)
#define  TXECC_SINGLE_BIT_ERROR				(1 << 13)
#define  TXFALSE_CONTROL_ERROR				(1 << 12)
#define  RXDSI_VC_ID_INVALID				(1 << 11)
#define  RXDSI_DATA_TYPE_NOT_REGOGNISED			(1 << 10)
#define  RXCHECKSUM_ERROR				(1 << 9)
#define  RXECC_MULTIBIT_ERROR				(1 << 8)
#define  RXECC_SINGLE_BIT_ERROR				(1 << 7)
#define  RXFALSE_CONTROL_ERROR				(1 << 6)
#define  RXHS_RECEIVE_TIMEOUT_ERROR			(1 << 5)
#define  RX_LP_TX_SYNC_ERROR				(1 << 4)
#define  RXEXCAPE_MODE_ENTRY_ERROR			(1 << 3)
#define  RXEOT_SYNC_ERROR				(1 << 2)
#define  RXSOT_SYNC_ERROR				(1 << 1)
#define  RXSOT_ERROR					(1 << 0)

#define _MIPIA_DSI_FUNC_PRG		(dev_priv->mipi_mmio_base + 0xb00c)
#define _MIPIC_DSI_FUNC_PRG		(dev_priv->mipi_mmio_base + 0xb80c)
#define MIPI_DSI_FUNC_PRG(port)		_MMIO_MIPI(port, _MIPIA_DSI_FUNC_PRG, _MIPIC_DSI_FUNC_PRG)
#define  CMD_MODE_DATA_WIDTH_MASK			(7 << 13)
#define  CMD_MODE_NOT_SUPPORTED				(0 << 13)
#define  CMD_MODE_DATA_WIDTH_16_BIT			(1 << 13)
#define  CMD_MODE_DATA_WIDTH_9_BIT			(2 << 13)
#define  CMD_MODE_DATA_WIDTH_8_BIT			(3 << 13)
#define  CMD_MODE_DATA_WIDTH_OPTION1			(4 << 13)
#define  CMD_MODE_DATA_WIDTH_OPTION2			(5 << 13)
#define  VID_MODE_FORMAT_MASK				(0xf << 7)
#define  VID_MODE_NOT_SUPPORTED				(0 << 7)
#define  VID_MODE_FORMAT_RGB565				(1 << 7)
#define  VID_MODE_FORMAT_RGB666_PACKED			(2 << 7)
#define  VID_MODE_FORMAT_RGB666				(3 << 7)
#define  VID_MODE_FORMAT_RGB888				(4 << 7)
#define  CMD_MODE_CHANNEL_NUMBER_SHIFT			5
#define  CMD_MODE_CHANNEL_NUMBER_MASK			(3 << 5)
#define  VID_MODE_CHANNEL_NUMBER_SHIFT			3
#define  VID_MODE_CHANNEL_NUMBER_MASK			(3 << 3)
#define  DATA_LANES_PRG_REG_SHIFT			0
#define  DATA_LANES_PRG_REG_MASK			(7 << 0)

#define _MIPIA_HS_TX_TIMEOUT		(dev_priv->mipi_mmio_base + 0xb010)
#define _MIPIC_HS_TX_TIMEOUT		(dev_priv->mipi_mmio_base + 0xb810)
#define MIPI_HS_TX_TIMEOUT(port)	_MMIO_MIPI(port, _MIPIA_HS_TX_TIMEOUT, _MIPIC_HS_TX_TIMEOUT)
#define  HIGH_SPEED_TX_TIMEOUT_COUNTER_MASK		0xffffff

#define _MIPIA_LP_RX_TIMEOUT		(dev_priv->mipi_mmio_base + 0xb014)
#define _MIPIC_LP_RX_TIMEOUT		(dev_priv->mipi_mmio_base + 0xb814)
#define MIPI_LP_RX_TIMEOUT(port)	_MMIO_MIPI(port, _MIPIA_LP_RX_TIMEOUT, _MIPIC_LP_RX_TIMEOUT)
#define  LOW_POWER_RX_TIMEOUT_COUNTER_MASK		0xffffff

#define _MIPIA_TURN_AROUND_TIMEOUT	(dev_priv->mipi_mmio_base + 0xb018)
#define _MIPIC_TURN_AROUND_TIMEOUT	(dev_priv->mipi_mmio_base + 0xb818)
#define MIPI_TURN_AROUND_TIMEOUT(port)	_MMIO_MIPI(port, _MIPIA_TURN_AROUND_TIMEOUT, _MIPIC_TURN_AROUND_TIMEOUT)
#define  TURN_AROUND_TIMEOUT_MASK			0x3f

#define _MIPIA_DEVICE_RESET_TIMER	(dev_priv->mipi_mmio_base + 0xb01c)
#define _MIPIC_DEVICE_RESET_TIMER	(dev_priv->mipi_mmio_base + 0xb81c)
#define MIPI_DEVICE_RESET_TIMER(port)	_MMIO_MIPI(port, _MIPIA_DEVICE_RESET_TIMER, _MIPIC_DEVICE_RESET_TIMER)
#define  DEVICE_RESET_TIMER_MASK			0xffff

#define _MIPIA_DPI_RESOLUTION		(dev_priv->mipi_mmio_base + 0xb020)
#define _MIPIC_DPI_RESOLUTION		(dev_priv->mipi_mmio_base + 0xb820)
#define MIPI_DPI_RESOLUTION(port)	_MMIO_MIPI(port, _MIPIA_DPI_RESOLUTION, _MIPIC_DPI_RESOLUTION)
#define  VERTICAL_ADDRESS_SHIFT				16
#define  VERTICAL_ADDRESS_MASK				(0xffff << 16)
#define  HORIZONTAL_ADDRESS_SHIFT			0
#define  HORIZONTAL_ADDRESS_MASK			0xffff

#define _MIPIA_DBI_FIFO_THROTTLE	(dev_priv->mipi_mmio_base + 0xb024)
#define _MIPIC_DBI_FIFO_THROTTLE	(dev_priv->mipi_mmio_base + 0xb824)
#define MIPI_DBI_FIFO_THROTTLE(port)	_MMIO_MIPI(port, _MIPIA_DBI_FIFO_THROTTLE, _MIPIC_DBI_FIFO_THROTTLE)
#define  DBI_FIFO_EMPTY_HALF				(0 << 0)
#define  DBI_FIFO_EMPTY_QUARTER				(1 << 0)
#define  DBI_FIFO_EMPTY_7_LOCATIONS			(2 << 0)

/* regs below are bits 15:0 */
#define _MIPIA_HSYNC_PADDING_COUNT	(dev_priv->mipi_mmio_base + 0xb028)
#define _MIPIC_HSYNC_PADDING_COUNT	(dev_priv->mipi_mmio_base + 0xb828)
#define MIPI_HSYNC_PADDING_COUNT(port)	_MMIO_MIPI(port, _MIPIA_HSYNC_PADDING_COUNT, _MIPIC_HSYNC_PADDING_COUNT)

#define _MIPIA_HBP_COUNT		(dev_priv->mipi_mmio_base + 0xb02c)
#define _MIPIC_HBP_COUNT		(dev_priv->mipi_mmio_base + 0xb82c)
#define MIPI_HBP_COUNT(port)		_MMIO_MIPI(port, _MIPIA_HBP_COUNT, _MIPIC_HBP_COUNT)

#define _MIPIA_HFP_COUNT		(dev_priv->mipi_mmio_base + 0xb030)
#define _MIPIC_HFP_COUNT		(dev_priv->mipi_mmio_base + 0xb830)
#define MIPI_HFP_COUNT(port)		_MMIO_MIPI(port, _MIPIA_HFP_COUNT, _MIPIC_HFP_COUNT)

#define _MIPIA_HACTIVE_AREA_COUNT	(dev_priv->mipi_mmio_base + 0xb034)
#define _MIPIC_HACTIVE_AREA_COUNT	(dev_priv->mipi_mmio_base + 0xb834)
#define MIPI_HACTIVE_AREA_COUNT(port)	_MMIO_MIPI(port, _MIPIA_HACTIVE_AREA_COUNT, _MIPIC_HACTIVE_AREA_COUNT)

#define _MIPIA_VSYNC_PADDING_COUNT	(dev_priv->mipi_mmio_base + 0xb038)
#define _MIPIC_VSYNC_PADDING_COUNT	(dev_priv->mipi_mmio_base + 0xb838)
#define MIPI_VSYNC_PADDING_COUNT(port)	_MMIO_MIPI(port, _MIPIA_VSYNC_PADDING_COUNT, _MIPIC_VSYNC_PADDING_COUNT)

#define _MIPIA_VBP_COUNT		(dev_priv->mipi_mmio_base + 0xb03c)
#define _MIPIC_VBP_COUNT		(dev_priv->mipi_mmio_base + 0xb83c)
#define MIPI_VBP_COUNT(port)		_MMIO_MIPI(port, _MIPIA_VBP_COUNT, _MIPIC_VBP_COUNT)

#define _MIPIA_VFP_COUNT		(dev_priv->mipi_mmio_base + 0xb040)
#define _MIPIC_VFP_COUNT		(dev_priv->mipi_mmio_base + 0xb840)
#define MIPI_VFP_COUNT(port)		_MMIO_MIPI(port, _MIPIA_VFP_COUNT, _MIPIC_VFP_COUNT)

#define _MIPIA_HIGH_LOW_SWITCH_COUNT	(dev_priv->mipi_mmio_base + 0xb044)
#define _MIPIC_HIGH_LOW_SWITCH_COUNT	(dev_priv->mipi_mmio_base + 0xb844)
#define MIPI_HIGH_LOW_SWITCH_COUNT(port)	_MMIO_MIPI(port,	_MIPIA_HIGH_LOW_SWITCH_COUNT, _MIPIC_HIGH_LOW_SWITCH_COUNT)

#define _MIPIA_DPI_CONTROL		(dev_priv->mipi_mmio_base + 0xb048)
#define _MIPIC_DPI_CONTROL		(dev_priv->mipi_mmio_base + 0xb848)
#define MIPI_DPI_CONTROL(port)		_MMIO_MIPI(port, _MIPIA_DPI_CONTROL, _MIPIC_DPI_CONTROL)
#define  DPI_LP_MODE					(1 << 6)
#define  BACKLIGHT_OFF					(1 << 5)
#define  BACKLIGHT_ON					(1 << 4)
#define  COLOR_MODE_OFF					(1 << 3)
#define  COLOR_MODE_ON					(1 << 2)
#define  TURN_ON					(1 << 1)
#define  SHUTDOWN					(1 << 0)

#define _MIPIA_DPI_DATA			(dev_priv->mipi_mmio_base + 0xb04c)
#define _MIPIC_DPI_DATA			(dev_priv->mipi_mmio_base + 0xb84c)
#define MIPI_DPI_DATA(port)		_MMIO_MIPI(port, _MIPIA_DPI_DATA, _MIPIC_DPI_DATA)
#define  COMMAND_BYTE_SHIFT				0
#define  COMMAND_BYTE_MASK				(0x3f << 0)

#define _MIPIA_INIT_COUNT		(dev_priv->mipi_mmio_base + 0xb050)
#define _MIPIC_INIT_COUNT		(dev_priv->mipi_mmio_base + 0xb850)
#define MIPI_INIT_COUNT(port)		_MMIO_MIPI(port, _MIPIA_INIT_COUNT, _MIPIC_INIT_COUNT)
#define  MASTER_INIT_TIMER_SHIFT			0
#define  MASTER_INIT_TIMER_MASK				(0xffff << 0)

#define _MIPIA_MAX_RETURN_PKT_SIZE	(dev_priv->mipi_mmio_base + 0xb054)
#define _MIPIC_MAX_RETURN_PKT_SIZE	(dev_priv->mipi_mmio_base + 0xb854)
#define MIPI_MAX_RETURN_PKT_SIZE(port)	_MMIO_MIPI(port, \
			_MIPIA_MAX_RETURN_PKT_SIZE, _MIPIC_MAX_RETURN_PKT_SIZE)
#define  MAX_RETURN_PKT_SIZE_SHIFT			0
#define  MAX_RETURN_PKT_SIZE_MASK			(0x3ff << 0)

#define _MIPIA_VIDEO_MODE_FORMAT	(dev_priv->mipi_mmio_base + 0xb058)
#define _MIPIC_VIDEO_MODE_FORMAT	(dev_priv->mipi_mmio_base + 0xb858)
#define MIPI_VIDEO_MODE_FORMAT(port)	_MMIO_MIPI(port, _MIPIA_VIDEO_MODE_FORMAT, _MIPIC_VIDEO_MODE_FORMAT)
#define  RANDOM_DPI_DISPLAY_RESOLUTION			(1 << 4)
#define  DISABLE_VIDEO_BTA				(1 << 3)
#define  IP_TG_CONFIG					(1 << 2)
#define  VIDEO_MODE_NON_BURST_WITH_SYNC_PULSE		(1 << 0)
#define  VIDEO_MODE_NON_BURST_WITH_SYNC_EVENTS		(2 << 0)
#define  VIDEO_MODE_BURST				(3 << 0)

#define _MIPIA_EOT_DISABLE		(dev_priv->mipi_mmio_base + 0xb05c)
#define _MIPIC_EOT_DISABLE		(dev_priv->mipi_mmio_base + 0xb85c)
#define MIPI_EOT_DISABLE(port)		_MMIO_MIPI(port, _MIPIA_EOT_DISABLE, _MIPIC_EOT_DISABLE)
#define  BXT_DEFEATURE_DPI_FIFO_CTR			(1 << 9)
#define  BXT_DPHY_DEFEATURE_EN				(1 << 8)
#define  LP_RX_TIMEOUT_ERROR_RECOVERY_DISABLE		(1 << 7)
#define  HS_RX_TIMEOUT_ERROR_RECOVERY_DISABLE		(1 << 6)
#define  LOW_CONTENTION_RECOVERY_DISABLE		(1 << 5)
#define  HIGH_CONTENTION_RECOVERY_DISABLE		(1 << 4)
#define  TXDSI_TYPE_NOT_RECOGNISED_ERROR_RECOVERY_DISABLE (1 << 3)
#define  TXECC_MULTIBIT_ERROR_RECOVERY_DISABLE		(1 << 2)
#define  CLOCKSTOP					(1 << 1)
#define  EOT_DISABLE					(1 << 0)

#define _MIPIA_LP_BYTECLK		(dev_priv->mipi_mmio_base + 0xb060)
#define _MIPIC_LP_BYTECLK		(dev_priv->mipi_mmio_base + 0xb860)
#define MIPI_LP_BYTECLK(port)		_MMIO_MIPI(port, _MIPIA_LP_BYTECLK, _MIPIC_LP_BYTECLK)
#define  LP_BYTECLK_SHIFT				0
#define  LP_BYTECLK_MASK				(0xffff << 0)

#define _MIPIA_TLPX_TIME_COUNT		(dev_priv->mipi_mmio_base + 0xb0a4)
#define _MIPIC_TLPX_TIME_COUNT		(dev_priv->mipi_mmio_base + 0xb8a4)
#define MIPI_TLPX_TIME_COUNT(port)	 _MMIO_MIPI(port, _MIPIA_TLPX_TIME_COUNT, _MIPIC_TLPX_TIME_COUNT)

#define _MIPIA_CLK_LANE_TIMING		(dev_priv->mipi_mmio_base + 0xb098)
#define _MIPIC_CLK_LANE_TIMING		(dev_priv->mipi_mmio_base + 0xb898)
#define MIPI_CLK_LANE_TIMING(port)	 _MMIO_MIPI(port, _MIPIA_CLK_LANE_TIMING, _MIPIC_CLK_LANE_TIMING)

/* bits 31:0 */
#define _MIPIA_LP_GEN_DATA		(dev_priv->mipi_mmio_base + 0xb064)
#define _MIPIC_LP_GEN_DATA		(dev_priv->mipi_mmio_base + 0xb864)
#define MIPI_LP_GEN_DATA(port)		_MMIO_MIPI(port, _MIPIA_LP_GEN_DATA, _MIPIC_LP_GEN_DATA)

/* bits 31:0 */
#define _MIPIA_HS_GEN_DATA		(dev_priv->mipi_mmio_base + 0xb068)
#define _MIPIC_HS_GEN_DATA		(dev_priv->mipi_mmio_base + 0xb868)
#define MIPI_HS_GEN_DATA(port)		_MMIO_MIPI(port, _MIPIA_HS_GEN_DATA, _MIPIC_HS_GEN_DATA)

#define _MIPIA_LP_GEN_CTRL		(dev_priv->mipi_mmio_base + 0xb06c)
#define _MIPIC_LP_GEN_CTRL		(dev_priv->mipi_mmio_base + 0xb86c)
#define MIPI_LP_GEN_CTRL(port)		_MMIO_MIPI(port, _MIPIA_LP_GEN_CTRL, _MIPIC_LP_GEN_CTRL)
#define _MIPIA_HS_GEN_CTRL		(dev_priv->mipi_mmio_base + 0xb070)
#define _MIPIC_HS_GEN_CTRL		(dev_priv->mipi_mmio_base + 0xb870)
#define MIPI_HS_GEN_CTRL(port)		_MMIO_MIPI(port, _MIPIA_HS_GEN_CTRL, _MIPIC_HS_GEN_CTRL)
#define  LONG_PACKET_WORD_COUNT_SHIFT			8
#define  LONG_PACKET_WORD_COUNT_MASK			(0xffff << 8)
#define  SHORT_PACKET_PARAM_SHIFT			8
#define  SHORT_PACKET_PARAM_MASK			(0xffff << 8)
#define  VIRTUAL_CHANNEL_SHIFT				6
#define  VIRTUAL_CHANNEL_MASK				(3 << 6)
#define  DATA_TYPE_SHIFT				0
#define  DATA_TYPE_MASK					(0x3f << 0)
/* data type values, see include/video/mipi_display.h */

#define _MIPIA_GEN_FIFO_STAT		(dev_priv->mipi_mmio_base + 0xb074)
#define _MIPIC_GEN_FIFO_STAT		(dev_priv->mipi_mmio_base + 0xb874)
#define MIPI_GEN_FIFO_STAT(port)	_MMIO_MIPI(port, _MIPIA_GEN_FIFO_STAT, _MIPIC_GEN_FIFO_STAT)
#define  DPI_FIFO_EMPTY					(1 << 28)
#define  DBI_FIFO_EMPTY					(1 << 27)
#define  LP_CTRL_FIFO_EMPTY				(1 << 26)
#define  LP_CTRL_FIFO_HALF_EMPTY			(1 << 25)
#define  LP_CTRL_FIFO_FULL				(1 << 24)
#define  HS_CTRL_FIFO_EMPTY				(1 << 18)
#define  HS_CTRL_FIFO_HALF_EMPTY			(1 << 17)
#define  HS_CTRL_FIFO_FULL				(1 << 16)
#define  LP_DATA_FIFO_EMPTY				(1 << 10)
#define  LP_DATA_FIFO_HALF_EMPTY			(1 << 9)
#define  LP_DATA_FIFO_FULL				(1 << 8)
#define  HS_DATA_FIFO_EMPTY				(1 << 2)
#define  HS_DATA_FIFO_HALF_EMPTY			(1 << 1)
#define  HS_DATA_FIFO_FULL				(1 << 0)

#define _MIPIA_HS_LS_DBI_ENABLE		(dev_priv->mipi_mmio_base + 0xb078)
#define _MIPIC_HS_LS_DBI_ENABLE		(dev_priv->mipi_mmio_base + 0xb878)
#define MIPI_HS_LP_DBI_ENABLE(port)	_MMIO_MIPI(port, _MIPIA_HS_LS_DBI_ENABLE, _MIPIC_HS_LS_DBI_ENABLE)
#define  DBI_HS_LP_MODE_MASK				(1 << 0)
#define  DBI_LP_MODE					(1 << 0)
#define  DBI_HS_MODE					(0 << 0)

#define _MIPIA_DPHY_PARAM		(dev_priv->mipi_mmio_base + 0xb080)
#define _MIPIC_DPHY_PARAM		(dev_priv->mipi_mmio_base + 0xb880)
#define MIPI_DPHY_PARAM(port)		_MMIO_MIPI(port, _MIPIA_DPHY_PARAM, _MIPIC_DPHY_PARAM)
#define  EXIT_ZERO_COUNT_SHIFT				24
#define  EXIT_ZERO_COUNT_MASK				(0x3f << 24)
#define  TRAIL_COUNT_SHIFT				16
#define  TRAIL_COUNT_MASK				(0x1f << 16)
#define  CLK_ZERO_COUNT_SHIFT				8
#define  CLK_ZERO_COUNT_MASK				(0xff << 8)
#define  PREPARE_COUNT_SHIFT				0
#define  PREPARE_COUNT_MASK				(0x3f << 0)

#define _MIPIA_DBI_BW_CTRL		(dev_priv->mipi_mmio_base + 0xb084)
#define _MIPIC_DBI_BW_CTRL		(dev_priv->mipi_mmio_base + 0xb884)
#define MIPI_DBI_BW_CTRL(port)		_MMIO_MIPI(port, _MIPIA_DBI_BW_CTRL, _MIPIC_DBI_BW_CTRL)

#define _MIPIA_CLK_LANE_SWITCH_TIME_CNT		(dev_priv->mipi_mmio_base + 0xb088)
#define _MIPIC_CLK_LANE_SWITCH_TIME_CNT		(dev_priv->mipi_mmio_base + 0xb888)
#define MIPI_CLK_LANE_SWITCH_TIME_CNT(port)	_MMIO_MIPI(port, _MIPIA_CLK_LANE_SWITCH_TIME_CNT, _MIPIC_CLK_LANE_SWITCH_TIME_CNT)
#define  LP_HS_SSW_CNT_SHIFT				16
#define  LP_HS_SSW_CNT_MASK				(0xffff << 16)
#define  HS_LP_PWR_SW_CNT_SHIFT				0
#define  HS_LP_PWR_SW_CNT_MASK				(0xffff << 0)

#define _MIPIA_STOP_STATE_STALL		(dev_priv->mipi_mmio_base + 0xb08c)
#define _MIPIC_STOP_STATE_STALL		(dev_priv->mipi_mmio_base + 0xb88c)
#define MIPI_STOP_STATE_STALL(port)	_MMIO_MIPI(port, _MIPIA_STOP_STATE_STALL, _MIPIC_STOP_STATE_STALL)
#define  STOP_STATE_STALL_COUNTER_SHIFT			0
#define  STOP_STATE_STALL_COUNTER_MASK			(0xff << 0)

#define _MIPIA_INTR_STAT_REG_1		(dev_priv->mipi_mmio_base + 0xb090)
#define _MIPIC_INTR_STAT_REG_1		(dev_priv->mipi_mmio_base + 0xb890)
#define MIPI_INTR_STAT_REG_1(port)	_MMIO_MIPI(port, _MIPIA_INTR_STAT_REG_1, _MIPIC_INTR_STAT_REG_1)
#define _MIPIA_INTR_EN_REG_1		(dev_priv->mipi_mmio_base + 0xb094)
#define _MIPIC_INTR_EN_REG_1		(dev_priv->mipi_mmio_base + 0xb894)
#define MIPI_INTR_EN_REG_1(port)	_MMIO_MIPI(port, _MIPIA_INTR_EN_REG_1, _MIPIC_INTR_EN_REG_1)
#define  RX_CONTENTION_DETECTED				(1 << 0)

/* XXX: only pipe A ?!? */
#define MIPIA_DBI_TYPEC_CTRL		(dev_priv->mipi_mmio_base + 0xb100)
#define  DBI_TYPEC_ENABLE				(1 << 31)
#define  DBI_TYPEC_WIP					(1 << 30)
#define  DBI_TYPEC_OPTION_SHIFT				28
#define  DBI_TYPEC_OPTION_MASK				(3 << 28)
#define  DBI_TYPEC_FREQ_SHIFT				24
#define  DBI_TYPEC_FREQ_MASK				(0xf << 24)
#define  DBI_TYPEC_OVERRIDE				(1 << 8)
#define  DBI_TYPEC_OVERRIDE_COUNTER_SHIFT		0
#define  DBI_TYPEC_OVERRIDE_COUNTER_MASK		(0xff << 0)

/* MIPI adapter registers */

#define _MIPIA_CTRL			(dev_priv->mipi_mmio_base + 0xb104)
#define _MIPIC_CTRL			(dev_priv->mipi_mmio_base + 0xb904)
#define MIPI_CTRL(port)			_MMIO_MIPI(port, _MIPIA_CTRL, _MIPIC_CTRL)
#define  ESCAPE_CLOCK_DIVIDER_SHIFT			5 /* A only */
#define  ESCAPE_CLOCK_DIVIDER_MASK			(3 << 5)
#define  ESCAPE_CLOCK_DIVIDER_1				(0 << 5)
#define  ESCAPE_CLOCK_DIVIDER_2				(1 << 5)
#define  ESCAPE_CLOCK_DIVIDER_4				(2 << 5)
#define  READ_REQUEST_PRIORITY_SHIFT			3
#define  READ_REQUEST_PRIORITY_MASK			(3 << 3)
#define  READ_REQUEST_PRIORITY_LOW			(0 << 3)
#define  READ_REQUEST_PRIORITY_HIGH			(3 << 3)
#define  RGB_FLIP_TO_BGR				(1 << 2)

#define  BXT_PIPE_SELECT_SHIFT				7
#define  BXT_PIPE_SELECT_MASK				(7 << 7)
#define  BXT_PIPE_SELECT(pipe)				((pipe) << 7)
#define  GLK_PHY_STATUS_PORT_READY			(1 << 31) /* RO */
#define  GLK_ULPS_NOT_ACTIVE				(1 << 30) /* RO */
#define  GLK_MIPIIO_RESET_RELEASED			(1 << 28)
#define  GLK_CLOCK_LANE_STOP_STATE			(1 << 27) /* RO */
#define  GLK_DATA_LANE_STOP_STATE			(1 << 26) /* RO */
#define  GLK_LP_WAKE					(1 << 22)
#define  GLK_LP11_LOW_PWR_MODE				(1 << 21)
#define  GLK_LP00_LOW_PWR_MODE				(1 << 20)
#define  GLK_FIREWALL_ENABLE				(1 << 16)
#define  BXT_PIXEL_OVERLAP_CNT_MASK			(0xf << 10)
#define  BXT_PIXEL_OVERLAP_CNT_SHIFT			10
#define  BXT_DSC_ENABLE					(1 << 3)
#define  BXT_RGB_FLIP					(1 << 2)
#define  GLK_MIPIIO_PORT_POWERED			(1 << 1) /* RO */
#define  GLK_MIPIIO_ENABLE				(1 << 0)

#define _MIPIA_DATA_ADDRESS		(dev_priv->mipi_mmio_base + 0xb108)
#define _MIPIC_DATA_ADDRESS		(dev_priv->mipi_mmio_base + 0xb908)
#define MIPI_DATA_ADDRESS(port)		_MMIO_MIPI(port, _MIPIA_DATA_ADDRESS, _MIPIC_DATA_ADDRESS)
#define  DATA_MEM_ADDRESS_SHIFT				5
#define  DATA_MEM_ADDRESS_MASK				(0x7ffffff << 5)
#define  DATA_VALID					(1 << 0)

#define _MIPIA_DATA_LENGTH		(dev_priv->mipi_mmio_base + 0xb10c)
#define _MIPIC_DATA_LENGTH		(dev_priv->mipi_mmio_base + 0xb90c)
#define MIPI_DATA_LENGTH(port)		_MMIO_MIPI(port, _MIPIA_DATA_LENGTH, _MIPIC_DATA_LENGTH)
#define  DATA_LENGTH_SHIFT				0
#define  DATA_LENGTH_MASK				(0xfffff << 0)

#define _MIPIA_COMMAND_ADDRESS		(dev_priv->mipi_mmio_base + 0xb110)
#define _MIPIC_COMMAND_ADDRESS		(dev_priv->mipi_mmio_base + 0xb910)
#define MIPI_COMMAND_ADDRESS(port)	_MMIO_MIPI(port, _MIPIA_COMMAND_ADDRESS, _MIPIC_COMMAND_ADDRESS)
#define  COMMAND_MEM_ADDRESS_SHIFT			5
#define  COMMAND_MEM_ADDRESS_MASK			(0x7ffffff << 5)
#define  AUTO_PWG_ENABLE				(1 << 2)
#define  MEMORY_WRITE_DATA_FROM_PIPE_RENDERING		(1 << 1)
#define  COMMAND_VALID					(1 << 0)

#define _MIPIA_COMMAND_LENGTH		(dev_priv->mipi_mmio_base + 0xb114)
#define _MIPIC_COMMAND_LENGTH		(dev_priv->mipi_mmio_base + 0xb914)
#define MIPI_COMMAND_LENGTH(port)	_MMIO_MIPI(port, _MIPIA_COMMAND_LENGTH, _MIPIC_COMMAND_LENGTH)
#define  COMMAND_LENGTH_SHIFT(n)			(8 * (n)) /* n: 0...3 */
#define  COMMAND_LENGTH_MASK(n)				(0xff << (8 * (n)))

#define _MIPIA_READ_DATA_RETURN0	(dev_priv->mipi_mmio_base + 0xb118)
#define _MIPIC_READ_DATA_RETURN0	(dev_priv->mipi_mmio_base + 0xb918)
#define MIPI_READ_DATA_RETURN(port, n) _MMIO(_MIPI(port, _MIPIA_READ_DATA_RETURN0, _MIPIC_READ_DATA_RETURN0) + 4 * (n)) /* n: 0...7 */

#define _MIPIA_READ_DATA_VALID		(dev_priv->mipi_mmio_base + 0xb138)
#define _MIPIC_READ_DATA_VALID		(dev_priv->mipi_mmio_base + 0xb938)
#define MIPI_READ_DATA_VALID(port)	_MMIO_MIPI(port, _MIPIA_READ_DATA_VALID, _MIPIC_READ_DATA_VALID)
#define  READ_DATA_VALID(n)				(1 << (n))

#endif /* __VLV_DSI_REGS_H__ */
