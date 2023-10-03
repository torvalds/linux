// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Texas Instruments DS90UB960-Q1 video deserializer
 *
 * Copyright (c) 2019 Luca Ceresoli <luca@lucaceresoli.net>
 * Copyright (c) 2023 Tomi Valkeinen <tomi.valkeinen@ideasonboard.com>
 */

/*
 * (Possible) TODOs:
 *
 * - PM for serializer and remote peripherals. We need to manage:
 *   - VPOC
 *     - Power domain? Regulator? Somehow any remote device should be able to
 *       cause the VPOC to be turned on.
 *   - Link between the deserializer and the serializer
 *     - Related to VPOC management. We probably always want to turn on the VPOC
 *       and then enable the link.
 *   - Serializer's services: i2c, gpios, power
 *     - The serializer needs to resume before the remote peripherals can
 *       e.g. use the i2c.
 *     - How to handle gpios? Reserving a gpio essentially keeps the provider
 *       (serializer) always powered on.
 * - Do we need a new bus for the FPD-Link? At the moment the serializers
 *   are children of the same i2c-adapter where the deserializer resides.
 * - i2c-atr could be made embeddable instead of allocatable.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/fwnode.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c-atr.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include <media/i2c/ds90ub9xx.h>
#include <media/mipi-csi2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define MHZ(v) ((u32)((v) * 1000000U))

#define UB960_POLL_TIME_MS	500

#define UB960_MAX_RX_NPORTS	4
#define UB960_MAX_TX_NPORTS	2
#define UB960_MAX_NPORTS	(UB960_MAX_RX_NPORTS + UB960_MAX_TX_NPORTS)

#define UB960_MAX_PORT_ALIASES	8

#define UB960_NUM_BC_GPIOS		4

/*
 * Register map
 *
 * 0x00-0x32   Shared (UB960_SR)
 * 0x33-0x3a   CSI-2 TX (per-port paged on DS90UB960, shared on 954) (UB960_TR)
 * 0x4c        Shared (UB960_SR)
 * 0x4d-0x7f   FPD-Link RX, per-port paged (UB960_RR)
 * 0xb0-0xbf   Shared (UB960_SR)
 * 0xd0-0xdf   FPD-Link RX, per-port paged (UB960_RR)
 * 0xf0-0xf5   Shared (UB960_SR)
 * 0xf8-0xfb   Shared (UB960_SR)
 * All others  Reserved
 *
 * Register prefixes:
 * UB960_SR_* = Shared register
 * UB960_RR_* = FPD-Link RX, per-port paged register
 * UB960_TR_* = CSI-2 TX, per-port paged register
 * UB960_XR_* = Reserved register
 * UB960_IR_* = Indirect register
 */

#define UB960_SR_I2C_DEV_ID			0x00
#define UB960_SR_RESET				0x01
#define UB960_SR_RESET_DIGITAL_RESET1		BIT(1)
#define UB960_SR_RESET_DIGITAL_RESET0		BIT(0)
#define UB960_SR_RESET_GPIO_LOCK_RELEASE	BIT(5)

#define UB960_SR_GEN_CONFIG			0x02
#define UB960_SR_REV_MASK			0x03
#define UB960_SR_DEVICE_STS			0x04
#define UB960_SR_PAR_ERR_THOLD_HI		0x05
#define UB960_SR_PAR_ERR_THOLD_LO		0x06
#define UB960_SR_BCC_WDOG_CTL			0x07
#define UB960_SR_I2C_CTL1			0x08
#define UB960_SR_I2C_CTL2			0x09
#define UB960_SR_SCL_HIGH_TIME			0x0a
#define UB960_SR_SCL_LOW_TIME			0x0b
#define UB960_SR_RX_PORT_CTL			0x0c
#define UB960_SR_IO_CTL				0x0d
#define UB960_SR_GPIO_PIN_STS			0x0e
#define UB960_SR_GPIO_INPUT_CTL			0x0f
#define UB960_SR_GPIO_PIN_CTL(n)		(0x10 + (n)) /* n < UB960_NUM_GPIOS */
#define UB960_SR_GPIO_PIN_CTL_GPIO_OUT_SEL		5
#define UB960_SR_GPIO_PIN_CTL_GPIO_OUT_SRC_SHIFT	2
#define UB960_SR_GPIO_PIN_CTL_GPIO_OUT_EN		BIT(0)

#define UB960_SR_FS_CTL				0x18
#define UB960_SR_FS_HIGH_TIME_1			0x19
#define UB960_SR_FS_HIGH_TIME_0			0x1a
#define UB960_SR_FS_LOW_TIME_1			0x1b
#define UB960_SR_FS_LOW_TIME_0			0x1c
#define UB960_SR_MAX_FRM_HI			0x1d
#define UB960_SR_MAX_FRM_LO			0x1e
#define UB960_SR_CSI_PLL_CTL			0x1f

#define UB960_SR_FWD_CTL1			0x20
#define UB960_SR_FWD_CTL1_PORT_DIS(n)		BIT((n) + 4)

#define UB960_SR_FWD_CTL2			0x21
#define UB960_SR_FWD_STS			0x22

#define UB960_SR_INTERRUPT_CTL			0x23
#define UB960_SR_INTERRUPT_CTL_INT_EN		BIT(7)
#define UB960_SR_INTERRUPT_CTL_IE_CSI_TX0	BIT(4)
#define UB960_SR_INTERRUPT_CTL_IE_RX(n)		BIT((n)) /* rxport[n] IRQ */

#define UB960_SR_INTERRUPT_STS			0x24
#define UB960_SR_INTERRUPT_STS_INT		BIT(7)
#define UB960_SR_INTERRUPT_STS_IS_CSI_TX(n)	BIT(4 + (n)) /* txport[n] IRQ */
#define UB960_SR_INTERRUPT_STS_IS_RX(n)		BIT((n)) /* rxport[n] IRQ */

#define UB960_SR_TS_CONFIG			0x25
#define UB960_SR_TS_CONTROL			0x26
#define UB960_SR_TS_LINE_HI			0x27
#define UB960_SR_TS_LINE_LO			0x28
#define UB960_SR_TS_STATUS			0x29
#define UB960_SR_TIMESTAMP_P0_HI		0x2a
#define UB960_SR_TIMESTAMP_P0_LO		0x2b
#define UB960_SR_TIMESTAMP_P1_HI		0x2c
#define UB960_SR_TIMESTAMP_P1_LO		0x2d

#define UB960_SR_CSI_PORT_SEL			0x32

#define UB960_TR_CSI_CTL			0x33
#define UB960_TR_CSI_CTL_CSI_CAL_EN		BIT(6)
#define UB960_TR_CSI_CTL_CSI_CONTS_CLOCK	BIT(1)
#define UB960_TR_CSI_CTL_CSI_ENABLE		BIT(0)

#define UB960_TR_CSI_CTL2			0x34
#define UB960_TR_CSI_STS			0x35
#define UB960_TR_CSI_TX_ICR			0x36

#define UB960_TR_CSI_TX_ISR			0x37
#define UB960_TR_CSI_TX_ISR_IS_CSI_SYNC_ERROR	BIT(3)
#define UB960_TR_CSI_TX_ISR_IS_CSI_PASS_ERROR	BIT(1)

#define UB960_TR_CSI_TEST_CTL			0x38
#define UB960_TR_CSI_TEST_PATT_HI		0x39
#define UB960_TR_CSI_TEST_PATT_LO		0x3a

#define UB960_XR_SFILTER_CFG			0x41
#define UB960_XR_SFILTER_CFG_SFILTER_MAX_SHIFT	4
#define UB960_XR_SFILTER_CFG_SFILTER_MIN_SHIFT	0

#define UB960_XR_AEQ_CTL1			0x42
#define UB960_XR_AEQ_CTL1_AEQ_ERR_CTL_FPD_CLK	BIT(6)
#define UB960_XR_AEQ_CTL1_AEQ_ERR_CTL_ENCODING	BIT(5)
#define UB960_XR_AEQ_CTL1_AEQ_ERR_CTL_PARITY	BIT(4)
#define UB960_XR_AEQ_CTL1_AEQ_ERR_CTL_MASK        \
	(UB960_XR_AEQ_CTL1_AEQ_ERR_CTL_FPD_CLK |  \
	 UB960_XR_AEQ_CTL1_AEQ_ERR_CTL_ENCODING | \
	 UB960_XR_AEQ_CTL1_AEQ_ERR_CTL_PARITY)
#define UB960_XR_AEQ_CTL1_AEQ_SFILTER_EN	BIT(0)

#define UB960_XR_AEQ_ERR_THOLD			0x43

#define UB960_RR_BCC_ERR_CTL			0x46
#define UB960_RR_BCC_STATUS			0x47
#define UB960_RR_BCC_STATUS_SEQ_ERROR		BIT(5)
#define UB960_RR_BCC_STATUS_MASTER_ERR		BIT(4)
#define UB960_RR_BCC_STATUS_MASTER_TO		BIT(3)
#define UB960_RR_BCC_STATUS_SLAVE_ERR		BIT(2)
#define UB960_RR_BCC_STATUS_SLAVE_TO		BIT(1)
#define UB960_RR_BCC_STATUS_RESP_ERR		BIT(0)
#define UB960_RR_BCC_STATUS_ERROR_MASK                                    \
	(UB960_RR_BCC_STATUS_SEQ_ERROR | UB960_RR_BCC_STATUS_MASTER_ERR | \
	 UB960_RR_BCC_STATUS_MASTER_TO | UB960_RR_BCC_STATUS_SLAVE_ERR |  \
	 UB960_RR_BCC_STATUS_SLAVE_TO | UB960_RR_BCC_STATUS_RESP_ERR)

#define UB960_RR_FPD3_CAP			0x4a
#define UB960_RR_RAW_EMBED_DTYPE		0x4b
#define UB960_RR_RAW_EMBED_DTYPE_LINES_SHIFT	6

#define UB960_SR_FPD3_PORT_SEL			0x4c

#define UB960_RR_RX_PORT_STS1			0x4d
#define UB960_RR_RX_PORT_STS1_BCC_CRC_ERROR	BIT(5)
#define UB960_RR_RX_PORT_STS1_LOCK_STS_CHG	BIT(4)
#define UB960_RR_RX_PORT_STS1_BCC_SEQ_ERROR	BIT(3)
#define UB960_RR_RX_PORT_STS1_PARITY_ERROR	BIT(2)
#define UB960_RR_RX_PORT_STS1_PORT_PASS		BIT(1)
#define UB960_RR_RX_PORT_STS1_LOCK_STS		BIT(0)
#define UB960_RR_RX_PORT_STS1_ERROR_MASK       \
	(UB960_RR_RX_PORT_STS1_BCC_CRC_ERROR | \
	 UB960_RR_RX_PORT_STS1_BCC_SEQ_ERROR | \
	 UB960_RR_RX_PORT_STS1_PARITY_ERROR)

#define UB960_RR_RX_PORT_STS2			0x4e
#define UB960_RR_RX_PORT_STS2_LINE_LEN_UNSTABLE	BIT(7)
#define UB960_RR_RX_PORT_STS2_LINE_LEN_CHG	BIT(6)
#define UB960_RR_RX_PORT_STS2_FPD3_ENCODE_ERROR	BIT(5)
#define UB960_RR_RX_PORT_STS2_BUFFER_ERROR	BIT(4)
#define UB960_RR_RX_PORT_STS2_CSI_ERROR		BIT(3)
#define UB960_RR_RX_PORT_STS2_FREQ_STABLE	BIT(2)
#define UB960_RR_RX_PORT_STS2_CABLE_FAULT	BIT(1)
#define UB960_RR_RX_PORT_STS2_LINE_CNT_CHG	BIT(0)
#define UB960_RR_RX_PORT_STS2_ERROR_MASK       \
	UB960_RR_RX_PORT_STS2_BUFFER_ERROR

#define UB960_RR_RX_FREQ_HIGH			0x4f
#define UB960_RR_RX_FREQ_LOW			0x50
#define UB960_RR_SENSOR_STS_0			0x51
#define UB960_RR_SENSOR_STS_1			0x52
#define UB960_RR_SENSOR_STS_2			0x53
#define UB960_RR_SENSOR_STS_3			0x54
#define UB960_RR_RX_PAR_ERR_HI			0x55
#define UB960_RR_RX_PAR_ERR_LO			0x56
#define UB960_RR_BIST_ERR_COUNT			0x57

#define UB960_RR_BCC_CONFIG			0x58
#define UB960_RR_BCC_CONFIG_I2C_PASS_THROUGH	BIT(6)
#define UB960_RR_BCC_CONFIG_BC_FREQ_SEL_MASK	GENMASK(2, 0)

#define UB960_RR_DATAPATH_CTL1			0x59
#define UB960_RR_DATAPATH_CTL2			0x5a
#define UB960_RR_SER_ID				0x5b
#define UB960_RR_SER_ALIAS_ID			0x5c

/* For these two register sets: n < UB960_MAX_PORT_ALIASES */
#define UB960_RR_SLAVE_ID(n)			(0x5d + (n))
#define UB960_RR_SLAVE_ALIAS(n)			(0x65 + (n))

#define UB960_RR_PORT_CONFIG			0x6d
#define UB960_RR_PORT_CONFIG_FPD3_MODE_MASK	GENMASK(1, 0)

#define UB960_RR_BC_GPIO_CTL(n)			(0x6e + (n)) /* n < 2 */
#define UB960_RR_RAW10_ID			0x70
#define UB960_RR_RAW10_ID_VC_SHIFT		6
#define UB960_RR_RAW10_ID_DT_SHIFT		0

#define UB960_RR_RAW12_ID			0x71
#define UB960_RR_CSI_VC_MAP			0x72
#define UB960_RR_CSI_VC_MAP_SHIFT(x)		((x) * 2)

#define UB960_RR_LINE_COUNT_HI			0x73
#define UB960_RR_LINE_COUNT_LO			0x74
#define UB960_RR_LINE_LEN_1			0x75
#define UB960_RR_LINE_LEN_0			0x76
#define UB960_RR_FREQ_DET_CTL			0x77
#define UB960_RR_MAILBOX_1			0x78
#define UB960_RR_MAILBOX_2			0x79

#define UB960_RR_CSI_RX_STS			0x7a
#define UB960_RR_CSI_RX_STS_LENGTH_ERR		BIT(3)
#define UB960_RR_CSI_RX_STS_CKSUM_ERR		BIT(2)
#define UB960_RR_CSI_RX_STS_ECC2_ERR		BIT(1)
#define UB960_RR_CSI_RX_STS_ECC1_ERR		BIT(0)
#define UB960_RR_CSI_RX_STS_ERROR_MASK                                    \
	(UB960_RR_CSI_RX_STS_LENGTH_ERR | UB960_RR_CSI_RX_STS_CKSUM_ERR | \
	 UB960_RR_CSI_RX_STS_ECC2_ERR | UB960_RR_CSI_RX_STS_ECC1_ERR)

#define UB960_RR_CSI_ERR_COUNTER		0x7b
#define UB960_RR_PORT_CONFIG2			0x7c
#define UB960_RR_PORT_CONFIG2_RAW10_8BIT_CTL_MASK GENMASK(7, 6)
#define UB960_RR_PORT_CONFIG2_RAW10_8BIT_CTL_SHIFT 6

#define UB960_RR_PORT_CONFIG2_LV_POL_LOW	BIT(1)
#define UB960_RR_PORT_CONFIG2_FV_POL_LOW	BIT(0)

#define UB960_RR_PORT_PASS_CTL			0x7d
#define UB960_RR_SEN_INT_RISE_CTL		0x7e
#define UB960_RR_SEN_INT_FALL_CTL		0x7f

#define UB960_SR_CSI_FRAME_COUNT_HI(n)		(0x90 + 8 * (n))
#define UB960_SR_CSI_FRAME_COUNT_LO(n)		(0x91 + 8 * (n))
#define UB960_SR_CSI_FRAME_ERR_COUNT_HI(n)	(0x92 + 8 * (n))
#define UB960_SR_CSI_FRAME_ERR_COUNT_LO(n)	(0x93 + 8 * (n))
#define UB960_SR_CSI_LINE_COUNT_HI(n)		(0x94 + 8 * (n))
#define UB960_SR_CSI_LINE_COUNT_LO(n)		(0x95 + 8 * (n))
#define UB960_SR_CSI_LINE_ERR_COUNT_HI(n)	(0x96 + 8 * (n))
#define UB960_SR_CSI_LINE_ERR_COUNT_LO(n)	(0x97 + 8 * (n))

#define UB960_XR_REFCLK_FREQ			0xa5	/* UB960 */

#define UB960_RR_VC_ID_MAP(x)			(0xa0 + (x)) /* UB9702 */

#define UB960_SR_IND_ACC_CTL			0xb0
#define UB960_SR_IND_ACC_CTL_IA_AUTO_INC	BIT(1)

#define UB960_SR_IND_ACC_ADDR			0xb1
#define UB960_SR_IND_ACC_DATA			0xb2
#define UB960_SR_BIST_CONTROL			0xb3
#define UB960_SR_MODE_IDX_STS			0xb8
#define UB960_SR_LINK_ERROR_COUNT		0xb9
#define UB960_SR_FPD3_ENC_CTL			0xba
#define UB960_SR_FV_MIN_TIME			0xbc
#define UB960_SR_GPIO_PD_CTL			0xbe

#define UB960_SR_FPD_RATE_CFG			0xc2	/* UB9702 */
#define UB960_SR_CSI_PLL_DIV			0xc9	/* UB9702 */

#define UB960_RR_PORT_DEBUG			0xd0
#define UB960_RR_AEQ_CTL2			0xd2
#define UB960_RR_AEQ_CTL2_SET_AEQ_FLOOR		BIT(2)

#define UB960_RR_AEQ_STATUS			0xd3
#define UB960_RR_AEQ_STATUS_STATUS_2		GENMASK(5, 3)
#define UB960_RR_AEQ_STATUS_STATUS_1		GENMASK(2, 0)

#define UB960_RR_AEQ_BYPASS			0xd4
#define UB960_RR_AEQ_BYPASS_EQ_STAGE1_VALUE_SHIFT	5
#define UB960_RR_AEQ_BYPASS_EQ_STAGE1_VALUE_MASK	GENMASK(7, 5)
#define UB960_RR_AEQ_BYPASS_EQ_STAGE2_VALUE_SHIFT	1
#define UB960_RR_AEQ_BYPASS_EQ_STAGE2_VALUE_MASK	GENMASK(3, 1)
#define UB960_RR_AEQ_BYPASS_ENABLE			BIT(0)

#define UB960_RR_AEQ_MIN_MAX			0xd5
#define UB960_RR_AEQ_MIN_MAX_AEQ_MAX_SHIFT	4
#define UB960_RR_AEQ_MIN_MAX_AEQ_FLOOR_SHIFT	0

#define UB960_RR_SFILTER_STS_0			0xd6
#define UB960_RR_SFILTER_STS_1			0xd7
#define UB960_RR_PORT_ICR_HI			0xd8
#define UB960_RR_PORT_ICR_LO			0xd9
#define UB960_RR_PORT_ISR_HI			0xda
#define UB960_RR_PORT_ISR_LO			0xdb
#define UB960_RR_FC_GPIO_STS			0xdc
#define UB960_RR_FC_GPIO_ICR			0xdd
#define UB960_RR_SEN_INT_RISE_STS		0xde
#define UB960_RR_SEN_INT_FALL_STS		0xdf

#define UB960_RR_CHANNEL_MODE			0xe4	/* UB9702 */

#define UB960_SR_FPD3_RX_ID(n)			(0xf0 + (n))
#define UB960_SR_FPD3_RX_ID_LEN			6

#define UB960_SR_I2C_RX_ID(n)			(0xf8 + (n)) /* < UB960_FPD_RX_NPORTS */

/* Indirect register blocks */
#define UB960_IND_TARGET_PAT_GEN		0x00
#define UB960_IND_TARGET_RX_ANA(n)		(0x01 + (n))
#define UB960_IND_TARGET_CSI_CSIPLL_REG_1	0x92	/* UB9702 */
#define UB960_IND_TARGET_CSI_ANA		0x07

/* UB960_IR_PGEN_*: Indirect Registers for Test Pattern Generator */

#define UB960_IR_PGEN_CTL			0x01
#define UB960_IR_PGEN_CTL_PGEN_ENABLE		BIT(0)

#define UB960_IR_PGEN_CFG			0x02
#define UB960_IR_PGEN_CSI_DI			0x03
#define UB960_IR_PGEN_LINE_SIZE1		0x04
#define UB960_IR_PGEN_LINE_SIZE0		0x05
#define UB960_IR_PGEN_BAR_SIZE1			0x06
#define UB960_IR_PGEN_BAR_SIZE0			0x07
#define UB960_IR_PGEN_ACT_LPF1			0x08
#define UB960_IR_PGEN_ACT_LPF0			0x09
#define UB960_IR_PGEN_TOT_LPF1			0x0a
#define UB960_IR_PGEN_TOT_LPF0			0x0b
#define UB960_IR_PGEN_LINE_PD1			0x0c
#define UB960_IR_PGEN_LINE_PD0			0x0d
#define UB960_IR_PGEN_VBP			0x0e
#define UB960_IR_PGEN_VFP			0x0f
#define UB960_IR_PGEN_COLOR(n)			(0x10 + (n)) /* n < 15 */

#define UB960_IR_RX_ANA_STROBE_SET_CLK		0x08
#define UB960_IR_RX_ANA_STROBE_SET_CLK_NO_EXTRA_DELAY	BIT(3)
#define UB960_IR_RX_ANA_STROBE_SET_CLK_DELAY_MASK	GENMASK(2, 0)

#define UB960_IR_RX_ANA_STROBE_SET_DATA		0x09
#define UB960_IR_RX_ANA_STROBE_SET_DATA_NO_EXTRA_DELAY	BIT(3)
#define UB960_IR_RX_ANA_STROBE_SET_DATA_DELAY_MASK	GENMASK(2, 0)

/* EQ related */

#define UB960_MIN_AEQ_STROBE_POS -7
#define UB960_MAX_AEQ_STROBE_POS  7

#define UB960_MANUAL_STROBE_EXTRA_DELAY 6

#define UB960_MIN_MANUAL_STROBE_POS -(7 + UB960_MANUAL_STROBE_EXTRA_DELAY)
#define UB960_MAX_MANUAL_STROBE_POS  (7 + UB960_MANUAL_STROBE_EXTRA_DELAY)
#define UB960_NUM_MANUAL_STROBE_POS  (UB960_MAX_MANUAL_STROBE_POS - UB960_MIN_MANUAL_STROBE_POS + 1)

#define UB960_MIN_EQ_LEVEL  0
#define UB960_MAX_EQ_LEVEL  14
#define UB960_NUM_EQ_LEVELS (UB960_MAX_EQ_LEVEL - UB960_MIN_EQ_LEVEL + 1)

struct ub960_hw_data {
	const char *model;
	u8 num_rxports;
	u8 num_txports;
	bool is_ub9702;
	bool is_fpdlink4;
};

enum ub960_rxport_mode {
	RXPORT_MODE_RAW10 = 0,
	RXPORT_MODE_RAW12_HF = 1,
	RXPORT_MODE_RAW12_LF = 2,
	RXPORT_MODE_CSI2_SYNC = 3,
	RXPORT_MODE_CSI2_NONSYNC = 4,
	RXPORT_MODE_LAST = RXPORT_MODE_CSI2_NONSYNC,
};

enum ub960_rxport_cdr {
	RXPORT_CDR_FPD3 = 0,
	RXPORT_CDR_FPD4 = 1,
	RXPORT_CDR_LAST = RXPORT_CDR_FPD4,
};

struct ub960_rxport {
	struct ub960_data      *priv;
	u8                      nport;	/* RX port number, and index in priv->rxport[] */

	struct {
		struct v4l2_subdev *sd;
		u16 pad;
		struct fwnode_handle *ep_fwnode;
	} source;

	/* Serializer */
	struct {
		struct fwnode_handle *fwnode;
		struct i2c_client *client;
		unsigned short alias; /* I2C alias (lower 7 bits) */
		struct ds90ub9xx_platform_data pdata;
	} ser;

	enum ub960_rxport_mode  rx_mode;
	enum ub960_rxport_cdr	cdr_mode;

	u8			lv_fv_pol;	/* LV and FV polarities */

	struct regulator	*vpoc;

	/* EQ settings */
	struct {
		bool manual_eq;

		s8 strobe_pos;

		union {
			struct {
				u8 eq_level_min;
				u8 eq_level_max;
			} aeq;

			struct {
				u8 eq_level;
			} manual;
		};
	} eq;

	const struct i2c_client *aliased_clients[UB960_MAX_PORT_ALIASES];
};

struct ub960_asd {
	struct v4l2_async_connection base;
	struct ub960_rxport *rxport;
};

static inline struct ub960_asd *to_ub960_asd(struct v4l2_async_connection *asd)
{
	return container_of(asd, struct ub960_asd, base);
}

struct ub960_txport {
	struct ub960_data      *priv;
	u8                      nport;	/* TX port number, and index in priv->txport[] */

	u32 num_data_lanes;
	bool non_continous_clk;
};

struct ub960_data {
	const struct ub960_hw_data	*hw_data;
	struct i2c_client	*client; /* for shared local registers */
	struct regmap		*regmap;

	/* lock for register access */
	struct mutex		reg_lock;

	struct clk		*refclk;

	struct regulator	*vddio;

	struct gpio_desc	*pd_gpio;
	struct delayed_work	poll_work;
	struct ub960_rxport	*rxports[UB960_MAX_RX_NPORTS];
	struct ub960_txport	*txports[UB960_MAX_TX_NPORTS];

	struct v4l2_subdev	sd;
	struct media_pad	pads[UB960_MAX_NPORTS];

	struct v4l2_ctrl_handler   ctrl_handler;
	struct v4l2_async_notifier notifier;

	u32 tx_data_rate;		/* Nominal data rate (Gb/s) */
	s64 tx_link_freq[1];

	struct i2c_atr *atr;

	struct {
		u8 rxport;
		u8 txport;
		u8 indirect_target;
	} reg_current;

	bool streaming;

	u8 stored_fwd_ctl;

	u64 stream_enable_mask[UB960_MAX_NPORTS];

	/* These are common to all ports */
	struct {
		bool manual;

		s8 min;
		s8 max;
	} strobe;
};

static inline struct ub960_data *sd_to_ub960(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ub960_data, sd);
}

static inline bool ub960_pad_is_sink(struct ub960_data *priv, u32 pad)
{
	return pad < priv->hw_data->num_rxports;
}

static inline bool ub960_pad_is_source(struct ub960_data *priv, u32 pad)
{
	return pad >= priv->hw_data->num_rxports;
}

static inline unsigned int ub960_pad_to_port(struct ub960_data *priv, u32 pad)
{
	if (ub960_pad_is_sink(priv, pad))
		return pad;
	else
		return pad - priv->hw_data->num_rxports;
}

struct ub960_format_info {
	u32 code;
	u32 bpp;
	u8 datatype;
	bool meta;
};

static const struct ub960_format_info ub960_formats[] = {
	{ .code = MEDIA_BUS_FMT_YUYV8_1X16, .bpp = 16, .datatype = MIPI_CSI2_DT_YUV422_8B, },
	{ .code = MEDIA_BUS_FMT_UYVY8_1X16, .bpp = 16, .datatype = MIPI_CSI2_DT_YUV422_8B, },
	{ .code = MEDIA_BUS_FMT_VYUY8_1X16, .bpp = 16, .datatype = MIPI_CSI2_DT_YUV422_8B, },
	{ .code = MEDIA_BUS_FMT_YVYU8_1X16, .bpp = 16, .datatype = MIPI_CSI2_DT_YUV422_8B, },

	{ .code = MEDIA_BUS_FMT_SBGGR12_1X12, .bpp = 12, .datatype = MIPI_CSI2_DT_RAW12, },
	{ .code = MEDIA_BUS_FMT_SGBRG12_1X12, .bpp = 12, .datatype = MIPI_CSI2_DT_RAW12, },
	{ .code = MEDIA_BUS_FMT_SGRBG12_1X12, .bpp = 12, .datatype = MIPI_CSI2_DT_RAW12, },
	{ .code = MEDIA_BUS_FMT_SRGGB12_1X12, .bpp = 12, .datatype = MIPI_CSI2_DT_RAW12, },
};

static const struct ub960_format_info *ub960_find_format(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ub960_formats); i++) {
		if (ub960_formats[i].code == code)
			return &ub960_formats[i];
	}

	return NULL;
}

/* -----------------------------------------------------------------------------
 * Basic device access
 */

static int ub960_read(struct ub960_data *priv, u8 reg, u8 *val)
{
	struct device *dev = &priv->client->dev;
	unsigned int v;
	int ret;

	mutex_lock(&priv->reg_lock);

	ret = regmap_read(priv->regmap, reg, &v);
	if (ret) {
		dev_err(dev, "%s: cannot read register 0x%02x (%d)!\n",
			__func__, reg, ret);
		goto out_unlock;
	}

	*val = v;

out_unlock:
	mutex_unlock(&priv->reg_lock);

	return ret;
}

static int ub960_write(struct ub960_data *priv, u8 reg, u8 val)
{
	struct device *dev = &priv->client->dev;
	int ret;

	mutex_lock(&priv->reg_lock);

	ret = regmap_write(priv->regmap, reg, val);
	if (ret)
		dev_err(dev, "%s: cannot write register 0x%02x (%d)!\n",
			__func__, reg, ret);

	mutex_unlock(&priv->reg_lock);

	return ret;
}

static int ub960_update_bits(struct ub960_data *priv, u8 reg, u8 mask, u8 val)
{
	struct device *dev = &priv->client->dev;
	int ret;

	mutex_lock(&priv->reg_lock);

	ret = regmap_update_bits(priv->regmap, reg, mask, val);
	if (ret)
		dev_err(dev, "%s: cannot update register 0x%02x (%d)!\n",
			__func__, reg, ret);

	mutex_unlock(&priv->reg_lock);

	return ret;
}

static int ub960_read16(struct ub960_data *priv, u8 reg, u16 *val)
{
	struct device *dev = &priv->client->dev;
	__be16 __v;
	int ret;

	mutex_lock(&priv->reg_lock);

	ret = regmap_bulk_read(priv->regmap, reg, &__v, sizeof(__v));
	if (ret) {
		dev_err(dev, "%s: cannot read register 0x%02x (%d)!\n",
			__func__, reg, ret);
		goto out_unlock;
	}

	*val = be16_to_cpu(__v);

out_unlock:
	mutex_unlock(&priv->reg_lock);

	return ret;
}

static int ub960_rxport_select(struct ub960_data *priv, u8 nport)
{
	struct device *dev = &priv->client->dev;
	int ret;

	lockdep_assert_held(&priv->reg_lock);

	if (priv->reg_current.rxport == nport)
		return 0;

	ret = regmap_write(priv->regmap, UB960_SR_FPD3_PORT_SEL,
			   (nport << 4) | BIT(nport));
	if (ret) {
		dev_err(dev, "%s: cannot select rxport %d (%d)!\n", __func__,
			nport, ret);
		return ret;
	}

	priv->reg_current.rxport = nport;

	return 0;
}

static int ub960_rxport_read(struct ub960_data *priv, u8 nport, u8 reg, u8 *val)
{
	struct device *dev = &priv->client->dev;
	unsigned int v;
	int ret;

	mutex_lock(&priv->reg_lock);

	ret = ub960_rxport_select(priv, nport);
	if (ret)
		goto out_unlock;

	ret = regmap_read(priv->regmap, reg, &v);
	if (ret) {
		dev_err(dev, "%s: cannot read register 0x%02x (%d)!\n",
			__func__, reg, ret);
		goto out_unlock;
	}

	*val = v;

out_unlock:
	mutex_unlock(&priv->reg_lock);

	return ret;
}

static int ub960_rxport_write(struct ub960_data *priv, u8 nport, u8 reg, u8 val)
{
	struct device *dev = &priv->client->dev;
	int ret;

	mutex_lock(&priv->reg_lock);

	ret = ub960_rxport_select(priv, nport);
	if (ret)
		goto out_unlock;

	ret = regmap_write(priv->regmap, reg, val);
	if (ret)
		dev_err(dev, "%s: cannot write register 0x%02x (%d)!\n",
			__func__, reg, ret);

out_unlock:
	mutex_unlock(&priv->reg_lock);

	return ret;
}

static int ub960_rxport_update_bits(struct ub960_data *priv, u8 nport, u8 reg,
				    u8 mask, u8 val)
{
	struct device *dev = &priv->client->dev;
	int ret;

	mutex_lock(&priv->reg_lock);

	ret = ub960_rxport_select(priv, nport);
	if (ret)
		goto out_unlock;

	ret = regmap_update_bits(priv->regmap, reg, mask, val);
	if (ret)
		dev_err(dev, "%s: cannot update register 0x%02x (%d)!\n",
			__func__, reg, ret);

out_unlock:
	mutex_unlock(&priv->reg_lock);

	return ret;
}

static int ub960_rxport_read16(struct ub960_data *priv, u8 nport, u8 reg,
			       u16 *val)
{
	struct device *dev = &priv->client->dev;
	__be16 __v;
	int ret;

	mutex_lock(&priv->reg_lock);

	ret = ub960_rxport_select(priv, nport);
	if (ret)
		goto out_unlock;

	ret = regmap_bulk_read(priv->regmap, reg, &__v, sizeof(__v));
	if (ret) {
		dev_err(dev, "%s: cannot read register 0x%02x (%d)!\n",
			__func__, reg, ret);
		goto out_unlock;
	}

	*val = be16_to_cpu(__v);

out_unlock:
	mutex_unlock(&priv->reg_lock);

	return ret;
}

static int ub960_txport_select(struct ub960_data *priv, u8 nport)
{
	struct device *dev = &priv->client->dev;
	int ret;

	lockdep_assert_held(&priv->reg_lock);

	if (priv->reg_current.txport == nport)
		return 0;

	ret = regmap_write(priv->regmap, UB960_SR_CSI_PORT_SEL,
			   (nport << 4) | BIT(nport));
	if (ret) {
		dev_err(dev, "%s: cannot select tx port %d (%d)!\n", __func__,
			nport, ret);
		return ret;
	}

	priv->reg_current.txport = nport;

	return 0;
}

static int ub960_txport_read(struct ub960_data *priv, u8 nport, u8 reg, u8 *val)
{
	struct device *dev = &priv->client->dev;
	unsigned int v;
	int ret;

	mutex_lock(&priv->reg_lock);

	ret = ub960_txport_select(priv, nport);
	if (ret)
		goto out_unlock;

	ret = regmap_read(priv->regmap, reg, &v);
	if (ret) {
		dev_err(dev, "%s: cannot read register 0x%02x (%d)!\n",
			__func__, reg, ret);
		goto out_unlock;
	}

	*val = v;

out_unlock:
	mutex_unlock(&priv->reg_lock);

	return ret;
}

static int ub960_txport_write(struct ub960_data *priv, u8 nport, u8 reg, u8 val)
{
	struct device *dev = &priv->client->dev;
	int ret;

	mutex_lock(&priv->reg_lock);

	ret = ub960_txport_select(priv, nport);
	if (ret)
		goto out_unlock;

	ret = regmap_write(priv->regmap, reg, val);
	if (ret)
		dev_err(dev, "%s: cannot write register 0x%02x (%d)!\n",
			__func__, reg, ret);

out_unlock:
	mutex_unlock(&priv->reg_lock);

	return ret;
}

static int ub960_txport_update_bits(struct ub960_data *priv, u8 nport, u8 reg,
				    u8 mask, u8 val)
{
	struct device *dev = &priv->client->dev;
	int ret;

	mutex_lock(&priv->reg_lock);

	ret = ub960_txport_select(priv, nport);
	if (ret)
		goto out_unlock;

	ret = regmap_update_bits(priv->regmap, reg, mask, val);
	if (ret)
		dev_err(dev, "%s: cannot update register 0x%02x (%d)!\n",
			__func__, reg, ret);

out_unlock:
	mutex_unlock(&priv->reg_lock);

	return ret;
}

static int ub960_select_ind_reg_block(struct ub960_data *priv, u8 block)
{
	struct device *dev = &priv->client->dev;
	int ret;

	lockdep_assert_held(&priv->reg_lock);

	if (priv->reg_current.indirect_target == block)
		return 0;

	ret = regmap_write(priv->regmap, UB960_SR_IND_ACC_CTL, block << 2);
	if (ret) {
		dev_err(dev, "%s: cannot select indirect target %u (%d)!\n",
			__func__, block, ret);
		return ret;
	}

	priv->reg_current.indirect_target = block;

	return 0;
}

static int ub960_read_ind(struct ub960_data *priv, u8 block, u8 reg, u8 *val)
{
	struct device *dev = &priv->client->dev;
	unsigned int v;
	int ret;

	mutex_lock(&priv->reg_lock);

	ret = ub960_select_ind_reg_block(priv, block);
	if (ret)
		goto out_unlock;

	ret = regmap_write(priv->regmap, UB960_SR_IND_ACC_ADDR, reg);
	if (ret) {
		dev_err(dev,
			"Write to IND_ACC_ADDR failed when reading %u:%x02x: %d\n",
			block, reg, ret);
		goto out_unlock;
	}

	ret = regmap_read(priv->regmap, UB960_SR_IND_ACC_DATA, &v);
	if (ret) {
		dev_err(dev,
			"Write to IND_ACC_DATA failed when reading %u:%x02x: %d\n",
			block, reg, ret);
		goto out_unlock;
	}

	*val = v;

out_unlock:
	mutex_unlock(&priv->reg_lock);

	return ret;
}

static int ub960_write_ind(struct ub960_data *priv, u8 block, u8 reg, u8 val)
{
	struct device *dev = &priv->client->dev;
	int ret;

	mutex_lock(&priv->reg_lock);

	ret = ub960_select_ind_reg_block(priv, block);
	if (ret)
		goto out_unlock;

	ret = regmap_write(priv->regmap, UB960_SR_IND_ACC_ADDR, reg);
	if (ret) {
		dev_err(dev,
			"Write to IND_ACC_ADDR failed when writing %u:%x02x: %d\n",
			block, reg, ret);
		goto out_unlock;
	}

	ret = regmap_write(priv->regmap, UB960_SR_IND_ACC_DATA, val);
	if (ret) {
		dev_err(dev,
			"Write to IND_ACC_DATA failed when writing %u:%x02x: %d\n",
			block, reg, ret);
		goto out_unlock;
	}

out_unlock:
	mutex_unlock(&priv->reg_lock);

	return ret;
}

static int ub960_ind_update_bits(struct ub960_data *priv, u8 block, u8 reg,
				 u8 mask, u8 val)
{
	struct device *dev = &priv->client->dev;
	int ret;

	mutex_lock(&priv->reg_lock);

	ret = ub960_select_ind_reg_block(priv, block);
	if (ret)
		goto out_unlock;

	ret = regmap_write(priv->regmap, UB960_SR_IND_ACC_ADDR, reg);
	if (ret) {
		dev_err(dev,
			"Write to IND_ACC_ADDR failed when updating %u:%x02x: %d\n",
			block, reg, ret);
		goto out_unlock;
	}

	ret = regmap_update_bits(priv->regmap, UB960_SR_IND_ACC_DATA, mask,
				 val);
	if (ret) {
		dev_err(dev,
			"Write to IND_ACC_DATA failed when updating %u:%x02x: %d\n",
			block, reg, ret);
		goto out_unlock;
	}

out_unlock:
	mutex_unlock(&priv->reg_lock);

	return ret;
}

/* -----------------------------------------------------------------------------
 * I2C-ATR (address translator)
 */

static int ub960_atr_attach_client(struct i2c_atr *atr, u32 chan_id,
				   const struct i2c_client *client, u16 alias)
{
	struct ub960_data *priv = i2c_atr_get_driver_data(atr);
	struct ub960_rxport *rxport = priv->rxports[chan_id];
	struct device *dev = &priv->client->dev;
	unsigned int reg_idx;

	for (reg_idx = 0; reg_idx < ARRAY_SIZE(rxport->aliased_clients); reg_idx++) {
		if (!rxport->aliased_clients[reg_idx])
			break;
	}

	if (reg_idx == ARRAY_SIZE(rxport->aliased_clients)) {
		dev_err(dev, "rx%u: alias pool exhausted\n", rxport->nport);
		return -EADDRNOTAVAIL;
	}

	rxport->aliased_clients[reg_idx] = client;

	ub960_rxport_write(priv, chan_id, UB960_RR_SLAVE_ID(reg_idx),
			   client->addr << 1);
	ub960_rxport_write(priv, chan_id, UB960_RR_SLAVE_ALIAS(reg_idx),
			   alias << 1);

	dev_dbg(dev, "rx%u: client 0x%02x assigned alias 0x%02x at slot %u\n",
		rxport->nport, client->addr, alias, reg_idx);

	return 0;
}

static void ub960_atr_detach_client(struct i2c_atr *atr, u32 chan_id,
				    const struct i2c_client *client)
{
	struct ub960_data *priv = i2c_atr_get_driver_data(atr);
	struct ub960_rxport *rxport = priv->rxports[chan_id];
	struct device *dev = &priv->client->dev;
	unsigned int reg_idx;

	for (reg_idx = 0; reg_idx < ARRAY_SIZE(rxport->aliased_clients); reg_idx++) {
		if (rxport->aliased_clients[reg_idx] == client)
			break;
	}

	if (reg_idx == ARRAY_SIZE(rxport->aliased_clients)) {
		dev_err(dev, "rx%u: client 0x%02x is not mapped!\n",
			rxport->nport, client->addr);
		return;
	}

	rxport->aliased_clients[reg_idx] = NULL;

	ub960_rxport_write(priv, chan_id, UB960_RR_SLAVE_ALIAS(reg_idx), 0);

	dev_dbg(dev, "rx%u: client 0x%02x released at slot %u\n", rxport->nport,
		client->addr, reg_idx);
}

static const struct i2c_atr_ops ub960_atr_ops = {
	.attach_client = ub960_atr_attach_client,
	.detach_client = ub960_atr_detach_client,
};

static int ub960_init_atr(struct ub960_data *priv)
{
	struct device *dev = &priv->client->dev;
	struct i2c_adapter *parent_adap = priv->client->adapter;

	priv->atr = i2c_atr_new(parent_adap, dev, &ub960_atr_ops,
				priv->hw_data->num_rxports);
	if (IS_ERR(priv->atr))
		return PTR_ERR(priv->atr);

	i2c_atr_set_driver_data(priv->atr, priv);

	return 0;
}

static void ub960_uninit_atr(struct ub960_data *priv)
{
	i2c_atr_delete(priv->atr);
	priv->atr = NULL;
}

/* -----------------------------------------------------------------------------
 * TX ports
 */

static int ub960_parse_dt_txport(struct ub960_data *priv,
				 struct fwnode_handle *ep_fwnode,
				 u8 nport)
{
	struct device *dev = &priv->client->dev;
	struct v4l2_fwnode_endpoint vep = {};
	struct ub960_txport *txport;
	int ret;

	txport = kzalloc(sizeof(*txport), GFP_KERNEL);
	if (!txport)
		return -ENOMEM;

	txport->priv = priv;
	txport->nport = nport;

	vep.bus_type = V4L2_MBUS_CSI2_DPHY;
	ret = v4l2_fwnode_endpoint_alloc_parse(ep_fwnode, &vep);
	if (ret) {
		dev_err(dev, "tx%u: failed to parse endpoint data\n", nport);
		goto err_free_txport;
	}

	txport->non_continous_clk = vep.bus.mipi_csi2.flags &
				    V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK;

	txport->num_data_lanes = vep.bus.mipi_csi2.num_data_lanes;

	if (vep.nr_of_link_frequencies != 1) {
		ret = -EINVAL;
		goto err_free_vep;
	}

	priv->tx_link_freq[0] = vep.link_frequencies[0];
	priv->tx_data_rate = priv->tx_link_freq[0] * 2;

	if (priv->tx_data_rate != MHZ(1600) &&
	    priv->tx_data_rate != MHZ(1200) &&
	    priv->tx_data_rate != MHZ(800) &&
	    priv->tx_data_rate != MHZ(400)) {
		dev_err(dev, "tx%u: invalid 'link-frequencies' value\n", nport);
		ret = -EINVAL;
		goto err_free_vep;
	}

	v4l2_fwnode_endpoint_free(&vep);

	priv->txports[nport] = txport;

	return 0;

err_free_vep:
	v4l2_fwnode_endpoint_free(&vep);
err_free_txport:
	kfree(txport);

	return ret;
}

static void ub960_csi_handle_events(struct ub960_data *priv, u8 nport)
{
	struct device *dev = &priv->client->dev;
	u8 csi_tx_isr;
	int ret;

	ret = ub960_txport_read(priv, nport, UB960_TR_CSI_TX_ISR, &csi_tx_isr);
	if (ret)
		return;

	if (csi_tx_isr & UB960_TR_CSI_TX_ISR_IS_CSI_SYNC_ERROR)
		dev_warn(dev, "TX%u: CSI_SYNC_ERROR\n", nport);

	if (csi_tx_isr & UB960_TR_CSI_TX_ISR_IS_CSI_PASS_ERROR)
		dev_warn(dev, "TX%u: CSI_PASS_ERROR\n", nport);
}

/* -----------------------------------------------------------------------------
 * RX ports
 */

static int ub960_rxport_enable_vpocs(struct ub960_data *priv)
{
	unsigned int nport;
	int ret;

	for (nport = 0; nport < priv->hw_data->num_rxports; nport++) {
		struct ub960_rxport *rxport = priv->rxports[nport];

		if (!rxport || !rxport->vpoc)
			continue;

		ret = regulator_enable(rxport->vpoc);
		if (ret)
			goto err_disable_vpocs;
	}

	return 0;

err_disable_vpocs:
	while (nport--) {
		struct ub960_rxport *rxport = priv->rxports[nport];

		if (!rxport || !rxport->vpoc)
			continue;

		regulator_disable(rxport->vpoc);
	}

	return ret;
}

static void ub960_rxport_disable_vpocs(struct ub960_data *priv)
{
	unsigned int nport;

	for (nport = 0; nport < priv->hw_data->num_rxports; nport++) {
		struct ub960_rxport *rxport = priv->rxports[nport];

		if (!rxport || !rxport->vpoc)
			continue;

		regulator_disable(rxport->vpoc);
	}
}

static void ub960_rxport_clear_errors(struct ub960_data *priv,
				      unsigned int nport)
{
	u8 v;

	ub960_rxport_read(priv, nport, UB960_RR_RX_PORT_STS1, &v);
	ub960_rxport_read(priv, nport, UB960_RR_RX_PORT_STS2, &v);
	ub960_rxport_read(priv, nport, UB960_RR_CSI_RX_STS, &v);
	ub960_rxport_read(priv, nport, UB960_RR_BCC_STATUS, &v);

	ub960_rxport_read(priv, nport, UB960_RR_RX_PAR_ERR_HI, &v);
	ub960_rxport_read(priv, nport, UB960_RR_RX_PAR_ERR_LO, &v);

	ub960_rxport_read(priv, nport, UB960_RR_CSI_ERR_COUNTER, &v);
}

static void ub960_clear_rx_errors(struct ub960_data *priv)
{
	unsigned int nport;

	for (nport = 0; nport < priv->hw_data->num_rxports; nport++)
		ub960_rxport_clear_errors(priv, nport);
}

static int ub960_rxport_get_strobe_pos(struct ub960_data *priv,
				       unsigned int nport, s8 *strobe_pos)
{
	u8 v;
	u8 clk_delay, data_delay;
	int ret;

	ub960_read_ind(priv, UB960_IND_TARGET_RX_ANA(nport),
		       UB960_IR_RX_ANA_STROBE_SET_CLK, &v);

	clk_delay = (v & UB960_IR_RX_ANA_STROBE_SET_CLK_NO_EXTRA_DELAY) ?
			    0 : UB960_MANUAL_STROBE_EXTRA_DELAY;

	ub960_read_ind(priv, UB960_IND_TARGET_RX_ANA(nport),
		       UB960_IR_RX_ANA_STROBE_SET_DATA, &v);

	data_delay = (v & UB960_IR_RX_ANA_STROBE_SET_DATA_NO_EXTRA_DELAY) ?
			     0 : UB960_MANUAL_STROBE_EXTRA_DELAY;

	ret = ub960_rxport_read(priv, nport, UB960_RR_SFILTER_STS_0, &v);
	if (ret)
		return ret;

	clk_delay += v & UB960_IR_RX_ANA_STROBE_SET_CLK_DELAY_MASK;

	ub960_rxport_read(priv, nport, UB960_RR_SFILTER_STS_1, &v);
	if (ret)
		return ret;

	data_delay += v & UB960_IR_RX_ANA_STROBE_SET_DATA_DELAY_MASK;

	*strobe_pos = data_delay - clk_delay;

	return 0;
}

static void ub960_rxport_set_strobe_pos(struct ub960_data *priv,
					unsigned int nport, s8 strobe_pos)
{
	u8 clk_delay, data_delay;

	clk_delay = UB960_IR_RX_ANA_STROBE_SET_CLK_NO_EXTRA_DELAY;
	data_delay = UB960_IR_RX_ANA_STROBE_SET_DATA_NO_EXTRA_DELAY;

	if (strobe_pos < UB960_MIN_AEQ_STROBE_POS)
		clk_delay = abs(strobe_pos) - UB960_MANUAL_STROBE_EXTRA_DELAY;
	else if (strobe_pos > UB960_MAX_AEQ_STROBE_POS)
		data_delay = strobe_pos - UB960_MANUAL_STROBE_EXTRA_DELAY;
	else if (strobe_pos < 0)
		clk_delay = abs(strobe_pos) | UB960_IR_RX_ANA_STROBE_SET_CLK_NO_EXTRA_DELAY;
	else if (strobe_pos > 0)
		data_delay = strobe_pos | UB960_IR_RX_ANA_STROBE_SET_DATA_NO_EXTRA_DELAY;

	ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport),
			UB960_IR_RX_ANA_STROBE_SET_CLK, clk_delay);

	ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport),
			UB960_IR_RX_ANA_STROBE_SET_DATA, data_delay);
}

static void ub960_rxport_set_strobe_range(struct ub960_data *priv,
					  s8 strobe_min, s8 strobe_max)
{
	/* Convert the signed strobe pos to positive zero based value */
	strobe_min -= UB960_MIN_AEQ_STROBE_POS;
	strobe_max -= UB960_MIN_AEQ_STROBE_POS;

	ub960_write(priv, UB960_XR_SFILTER_CFG,
		    ((u8)strobe_min << UB960_XR_SFILTER_CFG_SFILTER_MIN_SHIFT) |
		    ((u8)strobe_max << UB960_XR_SFILTER_CFG_SFILTER_MAX_SHIFT));
}

static int ub960_rxport_get_eq_level(struct ub960_data *priv,
				     unsigned int nport, u8 *eq_level)
{
	int ret;
	u8 v;

	ret = ub960_rxport_read(priv, nport, UB960_RR_AEQ_STATUS, &v);
	if (ret)
		return ret;

	*eq_level = (v & UB960_RR_AEQ_STATUS_STATUS_1) +
		    (v & UB960_RR_AEQ_STATUS_STATUS_2);

	return 0;
}

static void ub960_rxport_set_eq_level(struct ub960_data *priv,
				      unsigned int nport, u8 eq_level)
{
	u8 eq_stage_1_select_value, eq_stage_2_select_value;
	const unsigned int eq_stage_max = 7;
	u8 v;

	if (eq_level <= eq_stage_max) {
		eq_stage_1_select_value = eq_level;
		eq_stage_2_select_value = 0;
	} else {
		eq_stage_1_select_value = eq_stage_max;
		eq_stage_2_select_value = eq_level - eq_stage_max;
	}

	ub960_rxport_read(priv, nport, UB960_RR_AEQ_BYPASS, &v);

	v &= ~(UB960_RR_AEQ_BYPASS_EQ_STAGE1_VALUE_MASK |
	       UB960_RR_AEQ_BYPASS_EQ_STAGE2_VALUE_MASK);
	v |= eq_stage_1_select_value << UB960_RR_AEQ_BYPASS_EQ_STAGE1_VALUE_SHIFT;
	v |= eq_stage_2_select_value << UB960_RR_AEQ_BYPASS_EQ_STAGE2_VALUE_SHIFT;
	v |= UB960_RR_AEQ_BYPASS_ENABLE;

	ub960_rxport_write(priv, nport, UB960_RR_AEQ_BYPASS, v);
}

static void ub960_rxport_set_eq_range(struct ub960_data *priv,
				      unsigned int nport, u8 eq_min, u8 eq_max)
{
	ub960_rxport_write(priv, nport, UB960_RR_AEQ_MIN_MAX,
			   (eq_min << UB960_RR_AEQ_MIN_MAX_AEQ_FLOOR_SHIFT) |
			   (eq_max << UB960_RR_AEQ_MIN_MAX_AEQ_MAX_SHIFT));

	/* Enable AEQ min setting */
	ub960_rxport_update_bits(priv, nport, UB960_RR_AEQ_CTL2,
				 UB960_RR_AEQ_CTL2_SET_AEQ_FLOOR,
				 UB960_RR_AEQ_CTL2_SET_AEQ_FLOOR);
}

static void ub960_rxport_config_eq(struct ub960_data *priv, unsigned int nport)
{
	struct ub960_rxport *rxport = priv->rxports[nport];

	/* We also set common settings here. Should be moved elsewhere. */

	if (priv->strobe.manual) {
		/* Disable AEQ_SFILTER_EN */
		ub960_update_bits(priv, UB960_XR_AEQ_CTL1,
				  UB960_XR_AEQ_CTL1_AEQ_SFILTER_EN, 0);
	} else {
		/* Enable SFILTER and error control */
		ub960_write(priv, UB960_XR_AEQ_CTL1,
			    UB960_XR_AEQ_CTL1_AEQ_ERR_CTL_MASK |
				    UB960_XR_AEQ_CTL1_AEQ_SFILTER_EN);

		/* Set AEQ strobe range */
		ub960_rxport_set_strobe_range(priv, priv->strobe.min,
					      priv->strobe.max);
	}

	/* The rest are port specific */

	if (priv->strobe.manual)
		ub960_rxport_set_strobe_pos(priv, nport, rxport->eq.strobe_pos);
	else
		ub960_rxport_set_strobe_pos(priv, nport, 0);

	if (rxport->eq.manual_eq) {
		ub960_rxport_set_eq_level(priv, nport,
					  rxport->eq.manual.eq_level);

		/* Enable AEQ Bypass */
		ub960_rxport_update_bits(priv, nport, UB960_RR_AEQ_BYPASS,
					 UB960_RR_AEQ_BYPASS_ENABLE,
					 UB960_RR_AEQ_BYPASS_ENABLE);
	} else {
		ub960_rxport_set_eq_range(priv, nport,
					  rxport->eq.aeq.eq_level_min,
					  rxport->eq.aeq.eq_level_max);

		/* Disable AEQ Bypass */
		ub960_rxport_update_bits(priv, nport, UB960_RR_AEQ_BYPASS,
					 UB960_RR_AEQ_BYPASS_ENABLE, 0);
	}
}

static int ub960_rxport_link_ok(struct ub960_data *priv, unsigned int nport,
				bool *ok)
{
	u8 rx_port_sts1, rx_port_sts2;
	u16 parity_errors;
	u8 csi_rx_sts;
	u8 csi_err_cnt;
	u8 bcc_sts;
	int ret;
	bool errors;

	ret = ub960_rxport_read(priv, nport, UB960_RR_RX_PORT_STS1,
				&rx_port_sts1);
	if (ret)
		return ret;

	if (!(rx_port_sts1 & UB960_RR_RX_PORT_STS1_LOCK_STS)) {
		*ok = false;
		return 0;
	}

	ret = ub960_rxport_read(priv, nport, UB960_RR_RX_PORT_STS2,
				&rx_port_sts2);
	if (ret)
		return ret;

	ret = ub960_rxport_read(priv, nport, UB960_RR_CSI_RX_STS, &csi_rx_sts);
	if (ret)
		return ret;

	ret = ub960_rxport_read(priv, nport, UB960_RR_CSI_ERR_COUNTER,
				&csi_err_cnt);
	if (ret)
		return ret;

	ret = ub960_rxport_read(priv, nport, UB960_RR_BCC_STATUS, &bcc_sts);
	if (ret)
		return ret;

	ret = ub960_rxport_read16(priv, nport, UB960_RR_RX_PAR_ERR_HI,
				  &parity_errors);
	if (ret)
		return ret;

	errors = (rx_port_sts1 & UB960_RR_RX_PORT_STS1_ERROR_MASK) ||
		 (rx_port_sts2 & UB960_RR_RX_PORT_STS2_ERROR_MASK) ||
		 (bcc_sts & UB960_RR_BCC_STATUS_ERROR_MASK) ||
		 (csi_rx_sts & UB960_RR_CSI_RX_STS_ERROR_MASK) || csi_err_cnt ||
		 parity_errors;

	*ok = !errors;

	return 0;
}

/*
 * Wait for the RX ports to lock, have no errors and have stable strobe position
 * and EQ level.
 */
static int ub960_rxport_wait_locks(struct ub960_data *priv,
				   unsigned long port_mask,
				   unsigned int *lock_mask)
{
	struct device *dev = &priv->client->dev;
	unsigned long timeout;
	unsigned int link_ok_mask;
	unsigned int missing;
	unsigned int loops;
	u8 nport;
	int ret;

	if (port_mask == 0) {
		if (lock_mask)
			*lock_mask = 0;
		return 0;
	}

	if (port_mask >= BIT(priv->hw_data->num_rxports))
		return -EINVAL;

	timeout = jiffies + msecs_to_jiffies(1000);
	loops = 0;
	link_ok_mask = 0;

	while (time_before(jiffies, timeout)) {
		missing = 0;

		for_each_set_bit(nport, &port_mask,
				 priv->hw_data->num_rxports) {
			struct ub960_rxport *rxport = priv->rxports[nport];
			bool ok;

			if (!rxport)
				continue;

			ret = ub960_rxport_link_ok(priv, nport, &ok);
			if (ret)
				return ret;

			/*
			 * We want the link to be ok for two consecutive loops,
			 * as a link could get established just before our test
			 * and drop soon after.
			 */
			if (!ok || !(link_ok_mask & BIT(nport)))
				missing++;

			if (ok)
				link_ok_mask |= BIT(nport);
			else
				link_ok_mask &= ~BIT(nport);
		}

		loops++;

		if (missing == 0)
			break;

		msleep(50);
	}

	if (lock_mask)
		*lock_mask = link_ok_mask;

	dev_dbg(dev, "Wait locks done in %u loops\n", loops);
	for_each_set_bit(nport, &port_mask, priv->hw_data->num_rxports) {
		struct ub960_rxport *rxport = priv->rxports[nport];
		s8 strobe_pos, eq_level;
		u16 v;

		if (!rxport)
			continue;

		if (!(link_ok_mask & BIT(nport))) {
			dev_dbg(dev, "\trx%u: not locked\n", nport);
			continue;
		}

		ub960_rxport_read16(priv, nport, UB960_RR_RX_FREQ_HIGH, &v);

		ret = ub960_rxport_get_strobe_pos(priv, nport, &strobe_pos);
		if (ret)
			return ret;

		ret = ub960_rxport_get_eq_level(priv, nport, &eq_level);
		if (ret)
			return ret;

		dev_dbg(dev, "\trx%u: locked, SP: %d, EQ: %u, freq %llu Hz\n",
			nport, strobe_pos, eq_level, (v * 1000000ULL) >> 8);
	}

	return 0;
}

static unsigned long ub960_calc_bc_clk_rate_ub960(struct ub960_data *priv,
						  struct ub960_rxport *rxport)
{
	unsigned int mult;
	unsigned int div;

	switch (rxport->rx_mode) {
	case RXPORT_MODE_RAW10:
	case RXPORT_MODE_RAW12_HF:
	case RXPORT_MODE_RAW12_LF:
		mult = 1;
		div = 10;
		break;

	case RXPORT_MODE_CSI2_SYNC:
		mult = 2;
		div = 1;
		break;

	case RXPORT_MODE_CSI2_NONSYNC:
		mult = 2;
		div = 5;
		break;

	default:
		return 0;
	}

	return clk_get_rate(priv->refclk) * mult / div;
}

static unsigned long ub960_calc_bc_clk_rate_ub9702(struct ub960_data *priv,
						   struct ub960_rxport *rxport)
{
	switch (rxport->rx_mode) {
	case RXPORT_MODE_RAW10:
	case RXPORT_MODE_RAW12_HF:
	case RXPORT_MODE_RAW12_LF:
		return 2359400;

	case RXPORT_MODE_CSI2_SYNC:
		return 47187500;

	case RXPORT_MODE_CSI2_NONSYNC:
		return 9437500;

	default:
		return 0;
	}
}

static int ub960_rxport_add_serializer(struct ub960_data *priv, u8 nport)
{
	struct ub960_rxport *rxport = priv->rxports[nport];
	struct device *dev = &priv->client->dev;
	struct ds90ub9xx_platform_data *ser_pdata = &rxport->ser.pdata;
	struct i2c_board_info ser_info = {
		.of_node = to_of_node(rxport->ser.fwnode),
		.fwnode = rxport->ser.fwnode,
		.platform_data = ser_pdata,
	};

	ser_pdata->port = nport;
	ser_pdata->atr = priv->atr;
	if (priv->hw_data->is_ub9702)
		ser_pdata->bc_rate = ub960_calc_bc_clk_rate_ub9702(priv, rxport);
	else
		ser_pdata->bc_rate = ub960_calc_bc_clk_rate_ub960(priv, rxport);

	/*
	 * The serializer is added under the same i2c adapter as the
	 * deserializer. This is not quite right, as the serializer is behind
	 * the FPD-Link.
	 */
	ser_info.addr = rxport->ser.alias;
	rxport->ser.client =
		i2c_new_client_device(priv->client->adapter, &ser_info);
	if (IS_ERR(rxport->ser.client)) {
		dev_err(dev, "rx%u: cannot add %s i2c device", nport,
			ser_info.type);
		return PTR_ERR(rxport->ser.client);
	}

	dev_dbg(dev, "rx%u: remote serializer at alias 0x%02x (%u-%04x)\n",
		nport, rxport->ser.client->addr,
		rxport->ser.client->adapter->nr, rxport->ser.client->addr);

	return 0;
}

static void ub960_rxport_remove_serializer(struct ub960_data *priv, u8 nport)
{
	struct ub960_rxport *rxport = priv->rxports[nport];

	i2c_unregister_device(rxport->ser.client);
	rxport->ser.client = NULL;
}

/* Add serializer i2c devices for all initialized ports */
static int ub960_rxport_add_serializers(struct ub960_data *priv)
{
	unsigned int nport;
	int ret;

	for (nport = 0; nport < priv->hw_data->num_rxports; nport++) {
		struct ub960_rxport *rxport = priv->rxports[nport];

		if (!rxport)
			continue;

		ret = ub960_rxport_add_serializer(priv, nport);
		if (ret)
			goto err_remove_sers;
	}

	return 0;

err_remove_sers:
	while (nport--) {
		struct ub960_rxport *rxport = priv->rxports[nport];

		if (!rxport)
			continue;

		ub960_rxport_remove_serializer(priv, nport);
	}

	return ret;
}

static void ub960_rxport_remove_serializers(struct ub960_data *priv)
{
	unsigned int nport;

	for (nport = 0; nport < priv->hw_data->num_rxports; nport++) {
		struct ub960_rxport *rxport = priv->rxports[nport];

		if (!rxport)
			continue;

		ub960_rxport_remove_serializer(priv, nport);
	}
}

static void ub960_init_tx_port(struct ub960_data *priv,
			       struct ub960_txport *txport)
{
	unsigned int nport = txport->nport;
	u8 csi_ctl = 0;

	/*
	 * From the datasheet: "initial CSI Skew-Calibration
	 * sequence [...] should be set when operating at 1.6 Gbps"
	 */
	if (priv->tx_data_rate == MHZ(1600))
		csi_ctl |= UB960_TR_CSI_CTL_CSI_CAL_EN;

	csi_ctl |= (4 - txport->num_data_lanes) << 4;

	if (!txport->non_continous_clk)
		csi_ctl |= UB960_TR_CSI_CTL_CSI_CONTS_CLOCK;

	ub960_txport_write(priv, nport, UB960_TR_CSI_CTL, csi_ctl);
}

static int ub960_init_tx_ports(struct ub960_data *priv)
{
	unsigned int nport;
	u8 speed_select;
	u8 pll_div;

	/* TX ports */

	switch (priv->tx_data_rate) {
	case MHZ(1600):
	default:
		speed_select = 0;
		pll_div = 0x10;
		break;
	case MHZ(1200):
		speed_select = 1;
		pll_div = 0x18;
		break;
	case MHZ(800):
		speed_select = 2;
		pll_div = 0x10;
		break;
	case MHZ(400):
		speed_select = 3;
		pll_div = 0x10;
		break;
	}

	ub960_write(priv, UB960_SR_CSI_PLL_CTL, speed_select);

	if (priv->hw_data->is_ub9702) {
		ub960_write(priv, UB960_SR_CSI_PLL_DIV, pll_div);

		switch (priv->tx_data_rate) {
		case MHZ(1600):
		default:
			ub960_write_ind(priv, UB960_IND_TARGET_CSI_ANA, 0x92, 0x80);
			ub960_write_ind(priv, UB960_IND_TARGET_CSI_ANA, 0x4b, 0x2a);
			break;
		case MHZ(800):
			ub960_write_ind(priv, UB960_IND_TARGET_CSI_ANA, 0x92, 0x90);
			ub960_write_ind(priv, UB960_IND_TARGET_CSI_ANA, 0x4f, 0x2a);
			ub960_write_ind(priv, UB960_IND_TARGET_CSI_ANA, 0x4b, 0x2a);
			break;
		case MHZ(400):
			ub960_write_ind(priv, UB960_IND_TARGET_CSI_ANA, 0x92, 0xa0);
			break;
		}
	}

	for (nport = 0; nport < priv->hw_data->num_txports; nport++) {
		struct ub960_txport *txport = priv->txports[nport];

		if (!txport)
			continue;

		ub960_init_tx_port(priv, txport);
	}

	return 0;
}

static void ub960_init_rx_port_ub960(struct ub960_data *priv,
				     struct ub960_rxport *rxport)
{
	unsigned int nport = rxport->nport;
	u32 bc_freq_val;

	/*
	 * Back channel frequency select.
	 * Override FREQ_SELECT from the strap.
	 * 0 - 2.5 Mbps (DS90UB913A-Q1 / DS90UB933-Q1)
	 * 2 - 10 Mbps
	 * 6 - 50 Mbps (DS90UB953-Q1)
	 *
	 * Note that changing this setting will result in some errors on the back
	 * channel for a short period of time.
	 */

	switch (rxport->rx_mode) {
	case RXPORT_MODE_RAW10:
	case RXPORT_MODE_RAW12_HF:
	case RXPORT_MODE_RAW12_LF:
		bc_freq_val = 0;
		break;

	case RXPORT_MODE_CSI2_NONSYNC:
		bc_freq_val = 2;
		break;

	case RXPORT_MODE_CSI2_SYNC:
		bc_freq_val = 6;
		break;

	default:
		return;
	}

	ub960_rxport_update_bits(priv, nport, UB960_RR_BCC_CONFIG,
				 UB960_RR_BCC_CONFIG_BC_FREQ_SEL_MASK,
				 bc_freq_val);

	switch (rxport->rx_mode) {
	case RXPORT_MODE_RAW10:
		/* FPD3_MODE = RAW10 Mode (DS90UB913A-Q1 / DS90UB933-Q1 compatible) */
		ub960_rxport_update_bits(priv, nport, UB960_RR_PORT_CONFIG,
					 UB960_RR_PORT_CONFIG_FPD3_MODE_MASK,
					 0x3);

		/*
		 * RAW10_8BIT_CTL = 0b10 : 8-bit processing using upper 8 bits
		 */
		ub960_rxport_update_bits(priv, nport, UB960_RR_PORT_CONFIG2,
			UB960_RR_PORT_CONFIG2_RAW10_8BIT_CTL_MASK,
			0x2 << UB960_RR_PORT_CONFIG2_RAW10_8BIT_CTL_SHIFT);

		break;

	case RXPORT_MODE_RAW12_HF:
	case RXPORT_MODE_RAW12_LF:
		/* Not implemented */
		return;

	case RXPORT_MODE_CSI2_SYNC:
	case RXPORT_MODE_CSI2_NONSYNC:
		/* CSI-2 Mode (DS90UB953-Q1 compatible) */
		ub960_rxport_update_bits(priv, nport, UB960_RR_PORT_CONFIG, 0x3,
					 0x0);

		break;
	}

	/* LV_POLARITY & FV_POLARITY */
	ub960_rxport_update_bits(priv, nport, UB960_RR_PORT_CONFIG2, 0x3,
				 rxport->lv_fv_pol);

	/* Enable all interrupt sources from this port */
	ub960_rxport_write(priv, nport, UB960_RR_PORT_ICR_HI, 0x07);
	ub960_rxport_write(priv, nport, UB960_RR_PORT_ICR_LO, 0x7f);

	/* Enable I2C_PASS_THROUGH */
	ub960_rxport_update_bits(priv, nport, UB960_RR_BCC_CONFIG,
				 UB960_RR_BCC_CONFIG_I2C_PASS_THROUGH,
				 UB960_RR_BCC_CONFIG_I2C_PASS_THROUGH);

	/* Enable I2C communication to the serializer via the alias addr */
	ub960_rxport_write(priv, nport, UB960_RR_SER_ALIAS_ID,
			   rxport->ser.alias << 1);

	/* Configure EQ related settings */
	ub960_rxport_config_eq(priv, nport);

	/* Enable RX port */
	ub960_update_bits(priv, UB960_SR_RX_PORT_CTL, BIT(nport), BIT(nport));
}

static void ub960_init_rx_port_ub9702_fpd3(struct ub960_data *priv,
					   struct ub960_rxport *rxport)
{
	unsigned int nport = rxport->nport;
	u8 bc_freq_val;
	u8 fpd_func_mode;

	switch (rxport->rx_mode) {
	case RXPORT_MODE_RAW10:
		bc_freq_val = 0;
		fpd_func_mode = 5;
		break;

	case RXPORT_MODE_RAW12_HF:
		bc_freq_val = 0;
		fpd_func_mode = 4;
		break;

	case RXPORT_MODE_RAW12_LF:
		bc_freq_val = 0;
		fpd_func_mode = 6;
		break;

	case RXPORT_MODE_CSI2_SYNC:
		bc_freq_val = 6;
		fpd_func_mode = 2;
		break;

	case RXPORT_MODE_CSI2_NONSYNC:
		bc_freq_val = 2;
		fpd_func_mode = 2;
		break;

	default:
		return;
	}

	ub960_rxport_update_bits(priv, nport, UB960_RR_BCC_CONFIG, 0x7,
				 bc_freq_val);
	ub960_rxport_write(priv, nport, UB960_RR_CHANNEL_MODE, fpd_func_mode);

	/* set serdes_eq_mode = 1 */
	ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0xa8, 0x80);

	/* enable serdes driver */
	ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0x0d, 0x7f);

	/* set serdes_eq_offset=4 */
	ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0x2b, 0x04);

	/* init default serdes_eq_max in 0xa9 */
	ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0xa9, 0x23);

	/* init serdes_eq_min in 0xaa */
	ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0xaa, 0);

	/* serdes_driver_ctl2 control: DS90UB953-Q1/DS90UB933-Q1/DS90UB913A-Q1 */
	ub960_ind_update_bits(priv, UB960_IND_TARGET_RX_ANA(nport), 0x1b,
			      BIT(3), BIT(3));

	/* RX port to half-rate */
	ub960_update_bits(priv, UB960_SR_FPD_RATE_CFG, 0x3 << (nport * 2),
			  BIT(nport * 2));
}

static void ub960_init_rx_port_ub9702_fpd4_aeq(struct ub960_data *priv,
					       struct ub960_rxport *rxport)
{
	unsigned int nport = rxport->nport;
	bool first_time_power_up = true;

	if (first_time_power_up) {
		u8 v;

		/* AEQ init */
		ub960_read_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0x2c, &v);

		ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0x27, v);
		ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0x28, v + 1);

		ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0x2b, 0x00);
	}

	/* enable serdes_eq_ctl2 */
	ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0x9e, 0x00);

	/* enable serdes_eq_ctl1 */
	ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0x90, 0x40);

	/* enable serdes_eq_en */
	ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0x2e, 0x40);

	/* disable serdes_eq_override */
	ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0xf0, 0x00);

	/* disable serdes_gain_override */
	ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0x71, 0x00);
}

static void ub960_init_rx_port_ub9702_fpd4(struct ub960_data *priv,
					   struct ub960_rxport *rxport)
{
	unsigned int nport = rxport->nport;
	u8 bc_freq_val;

	switch (rxport->rx_mode) {
	case RXPORT_MODE_RAW10:
		bc_freq_val = 0;
		break;

	case RXPORT_MODE_RAW12_HF:
		bc_freq_val = 0;
		break;

	case RXPORT_MODE_RAW12_LF:
		bc_freq_val = 0;
		break;

	case RXPORT_MODE_CSI2_SYNC:
		bc_freq_val = 6;
		break;

	case RXPORT_MODE_CSI2_NONSYNC:
		bc_freq_val = 2;
		break;

	default:
		return;
	}

	ub960_rxport_update_bits(priv, nport, UB960_RR_BCC_CONFIG, 0x7,
				 bc_freq_val);

	/* FPD4 Sync Mode */
	ub960_rxport_write(priv, nport, UB960_RR_CHANNEL_MODE, 0);

	/* add serdes_eq_offset of 4 */
	ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0x2b, 0x04);

	/* FPD4 serdes_start_eq in 0x27: assign default */
	ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0x27, 0x0);
	/* FPD4 serdes_end_eq in 0x28: assign default */
	ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0x28, 0x23);

	/* set serdes_driver_mode into FPD IV mode */
	ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0x04, 0x00);
	/* set FPD PBC drv into FPD IV mode */
	ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0x1b, 0x00);

	/* set serdes_system_init to 0x2f */
	ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0x21, 0x2f);
	/* set serdes_system_rst in reset mode */
	ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0x25, 0xc1);

	/* RX port to 7.55G mode */
	ub960_update_bits(priv, UB960_SR_FPD_RATE_CFG, 0x3 << (nport * 2),
			  0 << (nport * 2));

	ub960_init_rx_port_ub9702_fpd4_aeq(priv, rxport);
}

static void ub960_init_rx_port_ub9702(struct ub960_data *priv,
				      struct ub960_rxport *rxport)
{
	unsigned int nport = rxport->nport;

	if (rxport->cdr_mode == RXPORT_CDR_FPD3)
		ub960_init_rx_port_ub9702_fpd3(priv, rxport);
	else /* RXPORT_CDR_FPD4 */
		ub960_init_rx_port_ub9702_fpd4(priv, rxport);

	switch (rxport->rx_mode) {
	case RXPORT_MODE_RAW10:
		/*
		 * RAW10_8BIT_CTL = 0b11 : 8-bit processing using lower 8 bits
		 * 0b10 : 8-bit processing using upper 8 bits
		 */
		ub960_rxport_update_bits(priv, nport, UB960_RR_PORT_CONFIG2,
					 0x3 << 6, 0x2 << 6);

		break;

	case RXPORT_MODE_RAW12_HF:
	case RXPORT_MODE_RAW12_LF:
		/* Not implemented */
		return;

	case RXPORT_MODE_CSI2_SYNC:
	case RXPORT_MODE_CSI2_NONSYNC:

		break;
	}

	/* LV_POLARITY & FV_POLARITY */
	ub960_rxport_update_bits(priv, nport, UB960_RR_PORT_CONFIG2, 0x3,
				 rxport->lv_fv_pol);

	/* Enable all interrupt sources from this port */
	ub960_rxport_write(priv, nport, UB960_RR_PORT_ICR_HI, 0x07);
	ub960_rxport_write(priv, nport, UB960_RR_PORT_ICR_LO, 0x7f);

	/* Enable I2C_PASS_THROUGH */
	ub960_rxport_update_bits(priv, nport, UB960_RR_BCC_CONFIG,
				 UB960_RR_BCC_CONFIG_I2C_PASS_THROUGH,
				 UB960_RR_BCC_CONFIG_I2C_PASS_THROUGH);

	/* Enable I2C communication to the serializer via the alias addr */
	ub960_rxport_write(priv, nport, UB960_RR_SER_ALIAS_ID,
			   rxport->ser.alias << 1);

	/* Enable RX port */
	ub960_update_bits(priv, UB960_SR_RX_PORT_CTL, BIT(nport), BIT(nport));

	if (rxport->cdr_mode == RXPORT_CDR_FPD4) {
		/* unreset 960 AEQ */
		ub960_write_ind(priv, UB960_IND_TARGET_RX_ANA(nport), 0x25, 0x41);
	}
}

static int ub960_init_rx_ports(struct ub960_data *priv)
{
	unsigned int nport;

	for (nport = 0; nport < priv->hw_data->num_rxports; nport++) {
		struct ub960_rxport *rxport = priv->rxports[nport];

		if (!rxport)
			continue;

		if (priv->hw_data->is_ub9702)
			ub960_init_rx_port_ub9702(priv, rxport);
		else
			ub960_init_rx_port_ub960(priv, rxport);
	}

	return 0;
}

static void ub960_rxport_handle_events(struct ub960_data *priv, u8 nport)
{
	struct device *dev = &priv->client->dev;
	u8 rx_port_sts1;
	u8 rx_port_sts2;
	u8 csi_rx_sts;
	u8 bcc_sts;
	int ret = 0;

	/* Read interrupts (also clears most of them) */
	if (!ret)
		ret = ub960_rxport_read(priv, nport, UB960_RR_RX_PORT_STS1,
					&rx_port_sts1);
	if (!ret)
		ret = ub960_rxport_read(priv, nport, UB960_RR_RX_PORT_STS2,
					&rx_port_sts2);
	if (!ret)
		ret = ub960_rxport_read(priv, nport, UB960_RR_CSI_RX_STS,
					&csi_rx_sts);
	if (!ret)
		ret = ub960_rxport_read(priv, nport, UB960_RR_BCC_STATUS,
					&bcc_sts);

	if (ret)
		return;

	if (rx_port_sts1 & UB960_RR_RX_PORT_STS1_PARITY_ERROR) {
		u16 v;

		ret = ub960_rxport_read16(priv, nport, UB960_RR_RX_PAR_ERR_HI,
					  &v);
		if (!ret)
			dev_err(dev, "rx%u parity errors: %u\n", nport, v);
	}

	if (rx_port_sts1 & UB960_RR_RX_PORT_STS1_BCC_CRC_ERROR)
		dev_err(dev, "rx%u BCC CRC error\n", nport);

	if (rx_port_sts1 & UB960_RR_RX_PORT_STS1_BCC_SEQ_ERROR)
		dev_err(dev, "rx%u BCC SEQ error\n", nport);

	if (rx_port_sts2 & UB960_RR_RX_PORT_STS2_LINE_LEN_UNSTABLE)
		dev_err(dev, "rx%u line length unstable\n", nport);

	if (rx_port_sts2 & UB960_RR_RX_PORT_STS2_FPD3_ENCODE_ERROR)
		dev_err(dev, "rx%u FPD3 encode error\n", nport);

	if (rx_port_sts2 & UB960_RR_RX_PORT_STS2_BUFFER_ERROR)
		dev_err(dev, "rx%u buffer error\n", nport);

	if (csi_rx_sts)
		dev_err(dev, "rx%u CSI error: %#02x\n", nport, csi_rx_sts);

	if (csi_rx_sts & UB960_RR_CSI_RX_STS_ECC1_ERR)
		dev_err(dev, "rx%u CSI ECC1 error\n", nport);

	if (csi_rx_sts & UB960_RR_CSI_RX_STS_ECC2_ERR)
		dev_err(dev, "rx%u CSI ECC2 error\n", nport);

	if (csi_rx_sts & UB960_RR_CSI_RX_STS_CKSUM_ERR)
		dev_err(dev, "rx%u CSI checksum error\n", nport);

	if (csi_rx_sts & UB960_RR_CSI_RX_STS_LENGTH_ERR)
		dev_err(dev, "rx%u CSI length error\n", nport);

	if (bcc_sts)
		dev_err(dev, "rx%u BCC error: %#02x\n", nport, bcc_sts);

	if (bcc_sts & UB960_RR_BCC_STATUS_RESP_ERR)
		dev_err(dev, "rx%u BCC response error", nport);

	if (bcc_sts & UB960_RR_BCC_STATUS_SLAVE_TO)
		dev_err(dev, "rx%u BCC slave timeout", nport);

	if (bcc_sts & UB960_RR_BCC_STATUS_SLAVE_ERR)
		dev_err(dev, "rx%u BCC slave error", nport);

	if (bcc_sts & UB960_RR_BCC_STATUS_MASTER_TO)
		dev_err(dev, "rx%u BCC master timeout", nport);

	if (bcc_sts & UB960_RR_BCC_STATUS_MASTER_ERR)
		dev_err(dev, "rx%u BCC master error", nport);

	if (bcc_sts & UB960_RR_BCC_STATUS_SEQ_ERROR)
		dev_err(dev, "rx%u BCC sequence error", nport);

	if (rx_port_sts2 & UB960_RR_RX_PORT_STS2_LINE_LEN_CHG) {
		u16 v;

		ret = ub960_rxport_read16(priv, nport, UB960_RR_LINE_LEN_1, &v);
		if (!ret)
			dev_dbg(dev, "rx%u line len changed: %u\n", nport, v);
	}

	if (rx_port_sts2 & UB960_RR_RX_PORT_STS2_LINE_CNT_CHG) {
		u16 v;

		ret = ub960_rxport_read16(priv, nport, UB960_RR_LINE_COUNT_HI,
					  &v);
		if (!ret)
			dev_dbg(dev, "rx%u line count changed: %u\n", nport, v);
	}

	if (rx_port_sts1 & UB960_RR_RX_PORT_STS1_LOCK_STS_CHG) {
		dev_dbg(dev, "rx%u: %s, %s, %s, %s\n", nport,
			(rx_port_sts1 & UB960_RR_RX_PORT_STS1_LOCK_STS) ?
				"locked" :
				"unlocked",
			(rx_port_sts1 & UB960_RR_RX_PORT_STS1_PORT_PASS) ?
				"passed" :
				"not passed",
			(rx_port_sts2 & UB960_RR_RX_PORT_STS2_CABLE_FAULT) ?
				"no clock" :
				"clock ok",
			(rx_port_sts2 & UB960_RR_RX_PORT_STS2_FREQ_STABLE) ?
				"stable freq" :
				"unstable freq");
	}
}

/* -----------------------------------------------------------------------------
 * V4L2
 */

/*
 * The current implementation only supports a simple VC mapping, where all VCs
 * from a one RX port will be mapped to the same VC. Also, the hardware
 * dictates that all streams from an RX port must go to a single TX port.
 *
 * This function decides the target VC numbers for each RX port with a simple
 * algorithm, so that for each TX port, we get VC numbers starting from 0,
 * and counting up.
 *
 * E.g. if all four RX ports are in use, of which the first two go to the
 * first TX port and the secont two go to the second TX port, we would get
 * the following VCs for the four RX ports: 0, 1, 0, 1.
 *
 * TODO: implement a more sophisticated VC mapping. As the driver cannot know
 * what VCs the sinks expect (say, an FPGA with hardcoded VC routing), this
 * probably needs to be somehow configurable. Device tree?
 */
static void ub960_get_vc_maps(struct ub960_data *priv,
			      struct v4l2_subdev_state *state, u8 *vc)
{
	u8 cur_vc[UB960_MAX_TX_NPORTS] = {};
	struct v4l2_subdev_route *route;
	u8 handled_mask = 0;

	for_each_active_route(&state->routing, route) {
		unsigned int rx, tx;

		rx = ub960_pad_to_port(priv, route->sink_pad);
		if (BIT(rx) & handled_mask)
			continue;

		tx = ub960_pad_to_port(priv, route->source_pad);

		vc[rx] = cur_vc[tx]++;
		handled_mask |= BIT(rx);
	}
}

static int ub960_enable_tx_port(struct ub960_data *priv, unsigned int nport)
{
	struct device *dev = &priv->client->dev;

	dev_dbg(dev, "enable TX port %u\n", nport);

	return ub960_txport_update_bits(priv, nport, UB960_TR_CSI_CTL,
					UB960_TR_CSI_CTL_CSI_ENABLE,
					UB960_TR_CSI_CTL_CSI_ENABLE);
}

static void ub960_disable_tx_port(struct ub960_data *priv, unsigned int nport)
{
	struct device *dev = &priv->client->dev;

	dev_dbg(dev, "disable TX port %u\n", nport);

	ub960_txport_update_bits(priv, nport, UB960_TR_CSI_CTL,
				 UB960_TR_CSI_CTL_CSI_ENABLE, 0);
}

static int ub960_enable_rx_port(struct ub960_data *priv, unsigned int nport)
{
	struct device *dev = &priv->client->dev;

	dev_dbg(dev, "enable RX port %u\n", nport);

	/* Enable forwarding */
	return ub960_update_bits(priv, UB960_SR_FWD_CTL1,
				 UB960_SR_FWD_CTL1_PORT_DIS(nport), 0);
}

static void ub960_disable_rx_port(struct ub960_data *priv, unsigned int nport)
{
	struct device *dev = &priv->client->dev;

	dev_dbg(dev, "disable RX port %u\n", nport);

	/* Disable forwarding */
	ub960_update_bits(priv, UB960_SR_FWD_CTL1,
			  UB960_SR_FWD_CTL1_PORT_DIS(nport),
			  UB960_SR_FWD_CTL1_PORT_DIS(nport));
}

/*
 * The driver only supports using a single VC for each source. This function
 * checks that each source only provides streams using a single VC.
 */
static int ub960_validate_stream_vcs(struct ub960_data *priv)
{
	unsigned int nport;
	unsigned int i;

	for (nport = 0; nport < priv->hw_data->num_rxports; nport++) {
		struct ub960_rxport *rxport = priv->rxports[nport];
		struct v4l2_mbus_frame_desc desc;
		int ret;
		u8 vc;

		if (!rxport)
			continue;

		ret = v4l2_subdev_call(rxport->source.sd, pad, get_frame_desc,
				       rxport->source.pad, &desc);
		if (ret)
			return ret;

		if (desc.type != V4L2_MBUS_FRAME_DESC_TYPE_CSI2)
			continue;

		if (desc.num_entries == 0)
			continue;

		vc = desc.entry[0].bus.csi2.vc;

		for (i = 1; i < desc.num_entries; i++) {
			if (vc == desc.entry[i].bus.csi2.vc)
				continue;

			dev_err(&priv->client->dev,
				"rx%u: source with multiple virtual-channels is not supported\n",
				nport);
			return -ENODEV;
		}
	}

	return 0;
}

static int ub960_configure_ports_for_streaming(struct ub960_data *priv,
					       struct v4l2_subdev_state *state)
{
	u8 fwd_ctl;
	struct {
		u32 num_streams;
		u8 pixel_dt;
		u8 meta_dt;
		u32 meta_lines;
		u32 tx_port;
	} rx_data[UB960_MAX_RX_NPORTS] = {};
	u8 vc_map[UB960_MAX_RX_NPORTS] = {};
	struct v4l2_subdev_route *route;
	unsigned int nport;
	int ret;

	ret = ub960_validate_stream_vcs(priv);
	if (ret)
		return ret;

	ub960_get_vc_maps(priv, state, vc_map);

	for_each_active_route(&state->routing, route) {
		struct ub960_rxport *rxport;
		struct ub960_txport *txport;
		struct v4l2_mbus_framefmt *fmt;
		const struct ub960_format_info *ub960_fmt;
		unsigned int nport;

		nport = ub960_pad_to_port(priv, route->sink_pad);

		rxport = priv->rxports[nport];
		if (!rxport)
			return -EINVAL;

		txport = priv->txports[ub960_pad_to_port(priv, route->source_pad)];
		if (!txport)
			return -EINVAL;

		rx_data[nport].tx_port = ub960_pad_to_port(priv, route->source_pad);

		rx_data[nport].num_streams++;

		/* For the rest, we are only interested in parallel busses */
		if (rxport->rx_mode == RXPORT_MODE_CSI2_SYNC ||
		    rxport->rx_mode == RXPORT_MODE_CSI2_NONSYNC)
			continue;

		if (rx_data[nport].num_streams > 2)
			return -EPIPE;

		fmt = v4l2_subdev_state_get_stream_format(state,
							  route->sink_pad,
							  route->sink_stream);
		if (!fmt)
			return -EPIPE;

		ub960_fmt = ub960_find_format(fmt->code);
		if (!ub960_fmt)
			return -EPIPE;

		if (ub960_fmt->meta) {
			if (fmt->height > 3) {
				dev_err(&priv->client->dev,
					"rx%u: unsupported metadata height %u\n",
					nport, fmt->height);
				return -EPIPE;
			}

			rx_data[nport].meta_dt = ub960_fmt->datatype;
			rx_data[nport].meta_lines = fmt->height;
		} else {
			rx_data[nport].pixel_dt = ub960_fmt->datatype;
		}
	}

	/* Configure RX ports */

	/*
	 * Keep all port forwardings disabled by default. Forwarding will be
	 * enabled in ub960_enable_rx_port.
	 */
	fwd_ctl = GENMASK(7, 4);

	for (nport = 0; nport < priv->hw_data->num_rxports; nport++) {
		struct ub960_rxport *rxport = priv->rxports[nport];
		u8 vc = vc_map[nport];

		if (rx_data[nport].num_streams == 0)
			continue;

		switch (rxport->rx_mode) {
		case RXPORT_MODE_RAW10:
			ub960_rxport_write(priv, nport, UB960_RR_RAW10_ID,
				rx_data[nport].pixel_dt | (vc << UB960_RR_RAW10_ID_VC_SHIFT));

			ub960_rxport_write(priv, rxport->nport,
				UB960_RR_RAW_EMBED_DTYPE,
				(rx_data[nport].meta_lines << UB960_RR_RAW_EMBED_DTYPE_LINES_SHIFT) |
					rx_data[nport].meta_dt);

			break;

		case RXPORT_MODE_RAW12_HF:
		case RXPORT_MODE_RAW12_LF:
			/* Not implemented */
			break;

		case RXPORT_MODE_CSI2_SYNC:
		case RXPORT_MODE_CSI2_NONSYNC:
			if (!priv->hw_data->is_ub9702) {
				/* Map all VCs from this port to the same VC */
				ub960_rxport_write(priv, nport, UB960_RR_CSI_VC_MAP,
						   (vc << UB960_RR_CSI_VC_MAP_SHIFT(3)) |
						   (vc << UB960_RR_CSI_VC_MAP_SHIFT(2)) |
						   (vc << UB960_RR_CSI_VC_MAP_SHIFT(1)) |
						   (vc << UB960_RR_CSI_VC_MAP_SHIFT(0)));
			} else {
				unsigned int i;

				/* Map all VCs from this port to VC(nport) */
				for (i = 0; i < 8; i++)
					ub960_rxport_write(priv, nport,
							   UB960_RR_VC_ID_MAP(i),
							   nport);
			}

			break;
		}

		if (rx_data[nport].tx_port == 1)
			fwd_ctl |= BIT(nport); /* forward to TX1 */
		else
			fwd_ctl &= ~BIT(nport); /* forward to TX0 */
	}

	ub960_write(priv, UB960_SR_FWD_CTL1, fwd_ctl);

	return 0;
}

static void ub960_update_streaming_status(struct ub960_data *priv)
{
	unsigned int i;

	for (i = 0; i < UB960_MAX_NPORTS; i++) {
		if (priv->stream_enable_mask[i])
			break;
	}

	priv->streaming = i < UB960_MAX_NPORTS;
}

static int ub960_enable_streams(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state, u32 source_pad,
				u64 source_streams_mask)
{
	struct ub960_data *priv = sd_to_ub960(sd);
	struct device *dev = &priv->client->dev;
	u64 sink_streams[UB960_MAX_RX_NPORTS] = {};
	struct v4l2_subdev_route *route;
	unsigned int failed_port;
	unsigned int nport;
	int ret;

	if (!priv->streaming) {
		dev_dbg(dev, "Prepare for streaming\n");
		ret = ub960_configure_ports_for_streaming(priv, state);
		if (ret)
			return ret;
	}

	/* Enable TX port if not yet enabled */
	if (!priv->stream_enable_mask[source_pad]) {
		ret = ub960_enable_tx_port(priv,
					   ub960_pad_to_port(priv, source_pad));
		if (ret)
			return ret;
	}

	priv->stream_enable_mask[source_pad] |= source_streams_mask;

	/* Collect sink streams per pad which we need to enable */
	for_each_active_route(&state->routing, route) {
		if (route->source_pad != source_pad)
			continue;

		if (!(source_streams_mask & BIT_ULL(route->source_stream)))
			continue;

		nport = ub960_pad_to_port(priv, route->sink_pad);

		sink_streams[nport] |= BIT_ULL(route->sink_stream);
	}

	for (nport = 0; nport < priv->hw_data->num_rxports; nport++) {
		if (!sink_streams[nport])
			continue;

		/* Enable the RX port if not yet enabled */
		if (!priv->stream_enable_mask[nport]) {
			ret = ub960_enable_rx_port(priv, nport);
			if (ret) {
				failed_port = nport;
				goto err;
			}
		}

		priv->stream_enable_mask[nport] |= sink_streams[nport];

		dev_dbg(dev, "enable RX port %u streams %#llx\n", nport,
			sink_streams[nport]);

		ret = v4l2_subdev_enable_streams(
			priv->rxports[nport]->source.sd,
			priv->rxports[nport]->source.pad,
			sink_streams[nport]);
		if (ret) {
			priv->stream_enable_mask[nport] &= ~sink_streams[nport];

			if (!priv->stream_enable_mask[nport])
				ub960_disable_rx_port(priv, nport);

			failed_port = nport;
			goto err;
		}
	}

	priv->streaming = true;

	return 0;

err:
	for (nport = 0; nport < failed_port; nport++) {
		if (!sink_streams[nport])
			continue;

		dev_dbg(dev, "disable RX port %u streams %#llx\n", nport,
			sink_streams[nport]);

		ret = v4l2_subdev_disable_streams(
			priv->rxports[nport]->source.sd,
			priv->rxports[nport]->source.pad,
			sink_streams[nport]);
		if (ret)
			dev_err(dev, "Failed to disable streams: %d\n", ret);

		priv->stream_enable_mask[nport] &= ~sink_streams[nport];

		/* Disable RX port if no active streams */
		if (!priv->stream_enable_mask[nport])
			ub960_disable_rx_port(priv, nport);
	}

	priv->stream_enable_mask[source_pad] &= ~source_streams_mask;

	if (!priv->stream_enable_mask[source_pad])
		ub960_disable_tx_port(priv,
				      ub960_pad_to_port(priv, source_pad));

	ub960_update_streaming_status(priv);

	return ret;
}

static int ub960_disable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 u32 source_pad, u64 source_streams_mask)
{
	struct ub960_data *priv = sd_to_ub960(sd);
	struct device *dev = &priv->client->dev;
	u64 sink_streams[UB960_MAX_RX_NPORTS] = {};
	struct v4l2_subdev_route *route;
	unsigned int nport;
	int ret;

	/* Collect sink streams per pad which we need to disable */
	for_each_active_route(&state->routing, route) {
		if (route->source_pad != source_pad)
			continue;

		if (!(source_streams_mask & BIT_ULL(route->source_stream)))
			continue;

		nport = ub960_pad_to_port(priv, route->sink_pad);

		sink_streams[nport] |= BIT_ULL(route->sink_stream);
	}

	for (nport = 0; nport < priv->hw_data->num_rxports; nport++) {
		if (!sink_streams[nport])
			continue;

		dev_dbg(dev, "disable RX port %u streams %#llx\n", nport,
			sink_streams[nport]);

		ret = v4l2_subdev_disable_streams(
			priv->rxports[nport]->source.sd,
			priv->rxports[nport]->source.pad,
			sink_streams[nport]);
		if (ret)
			dev_err(dev, "Failed to disable streams: %d\n", ret);

		priv->stream_enable_mask[nport] &= ~sink_streams[nport];

		/* Disable RX port if no active streams */
		if (!priv->stream_enable_mask[nport])
			ub960_disable_rx_port(priv, nport);
	}

	/* Disable TX port if no active streams */

	priv->stream_enable_mask[source_pad] &= ~source_streams_mask;

	if (!priv->stream_enable_mask[source_pad])
		ub960_disable_tx_port(priv,
				      ub960_pad_to_port(priv, source_pad));

	ub960_update_streaming_status(priv);

	return 0;
}

static int _ub960_set_routing(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state,
			      struct v4l2_subdev_krouting *routing)
{
	static const struct v4l2_mbus_framefmt format = {
		.width = 640,
		.height = 480,
		.code = MEDIA_BUS_FMT_UYVY8_1X16,
		.field = V4L2_FIELD_NONE,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.ycbcr_enc = V4L2_YCBCR_ENC_601,
		.quantization = V4L2_QUANTIZATION_LIM_RANGE,
		.xfer_func = V4L2_XFER_FUNC_SRGB,
	};
	int ret;

	/*
	 * Note: we can only support up to V4L2_FRAME_DESC_ENTRY_MAX, until
	 * frame desc is made dynamically allocated.
	 */

	if (routing->num_routes > V4L2_FRAME_DESC_ENTRY_MAX)
		return -E2BIG;

	ret = v4l2_subdev_routing_validate(sd, routing,
					   V4L2_SUBDEV_ROUTING_ONLY_1_TO_1 |
					   V4L2_SUBDEV_ROUTING_NO_SINK_STREAM_MIX);
	if (ret)
		return ret;

	ret = v4l2_subdev_set_routing_with_fmt(sd, state, routing, &format);
	if (ret)
		return ret;

	return 0;
}

static int ub960_set_routing(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     enum v4l2_subdev_format_whence which,
			     struct v4l2_subdev_krouting *routing)
{
	struct ub960_data *priv = sd_to_ub960(sd);

	if (which == V4L2_SUBDEV_FORMAT_ACTIVE && priv->streaming)
		return -EBUSY;

	return _ub960_set_routing(sd, state, routing);
}

static int ub960_get_frame_desc(struct v4l2_subdev *sd, unsigned int pad,
				struct v4l2_mbus_frame_desc *fd)
{
	struct ub960_data *priv = sd_to_ub960(sd);
	struct v4l2_subdev_route *route;
	struct v4l2_subdev_state *state;
	int ret = 0;
	struct device *dev = &priv->client->dev;
	u8 vc_map[UB960_MAX_RX_NPORTS] = {};

	if (!ub960_pad_is_source(priv, pad))
		return -EINVAL;

	memset(fd, 0, sizeof(*fd));

	fd->type = V4L2_MBUS_FRAME_DESC_TYPE_CSI2;

	state = v4l2_subdev_lock_and_get_active_state(&priv->sd);

	ub960_get_vc_maps(priv, state, vc_map);

	for_each_active_route(&state->routing, route) {
		struct v4l2_mbus_frame_desc_entry *source_entry = NULL;
		struct v4l2_mbus_frame_desc source_fd;
		unsigned int nport;
		unsigned int i;

		if (route->source_pad != pad)
			continue;

		nport = ub960_pad_to_port(priv, route->sink_pad);

		ret = v4l2_subdev_call(priv->rxports[nport]->source.sd, pad,
				       get_frame_desc,
				       priv->rxports[nport]->source.pad,
				       &source_fd);
		if (ret) {
			dev_err(dev,
				"Failed to get source frame desc for pad %u\n",
				route->sink_pad);
			goto out_unlock;
		}

		for (i = 0; i < source_fd.num_entries; i++) {
			if (source_fd.entry[i].stream == route->sink_stream) {
				source_entry = &source_fd.entry[i];
				break;
			}
		}

		if (!source_entry) {
			dev_err(dev,
				"Failed to find stream from source frame desc\n");
			ret = -EPIPE;
			goto out_unlock;
		}

		fd->entry[fd->num_entries].stream = route->source_stream;
		fd->entry[fd->num_entries].flags = source_entry->flags;
		fd->entry[fd->num_entries].length = source_entry->length;
		fd->entry[fd->num_entries].pixelcode = source_entry->pixelcode;

		fd->entry[fd->num_entries].bus.csi2.vc = vc_map[nport];

		if (source_fd.type == V4L2_MBUS_FRAME_DESC_TYPE_CSI2) {
			fd->entry[fd->num_entries].bus.csi2.dt =
				source_entry->bus.csi2.dt;
		} else {
			const struct ub960_format_info *ub960_fmt;
			struct v4l2_mbus_framefmt *fmt;

			fmt = v4l2_subdev_state_get_stream_format(state, pad,
								  route->source_stream);

			if (!fmt) {
				ret = -EINVAL;
				goto out_unlock;
			}

			ub960_fmt = ub960_find_format(fmt->code);
			if (!ub960_fmt) {
				dev_err(dev, "Unable to find format\n");
				ret = -EINVAL;
				goto out_unlock;
			}

			fd->entry[fd->num_entries].bus.csi2.dt =
				ub960_fmt->datatype;
		}

		fd->num_entries++;
	}

out_unlock:
	v4l2_subdev_unlock_state(state);

	return ret;
}

static int ub960_set_fmt(struct v4l2_subdev *sd,
			 struct v4l2_subdev_state *state,
			 struct v4l2_subdev_format *format)
{
	struct ub960_data *priv = sd_to_ub960(sd);
	struct v4l2_mbus_framefmt *fmt;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE && priv->streaming)
		return -EBUSY;

	/* No transcoding, source and sink formats must match. */
	if (ub960_pad_is_source(priv, format->pad))
		return v4l2_subdev_get_fmt(sd, state, format);

	/*
	 * Default to the first format if the requested media bus code isn't
	 * supported.
	 */
	if (!ub960_find_format(format->format.code))
		format->format.code = ub960_formats[0].code;

	fmt = v4l2_subdev_state_get_stream_format(state, format->pad,
						  format->stream);
	if (!fmt)
		return -EINVAL;

	*fmt = format->format;

	fmt = v4l2_subdev_state_get_opposite_stream_format(state, format->pad,
							   format->stream);
	if (!fmt)
		return -EINVAL;

	*fmt = format->format;

	return 0;
}

static int ub960_init_cfg(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state)
{
	struct ub960_data *priv = sd_to_ub960(sd);

	struct v4l2_subdev_route routes[] = {
		{
			.sink_pad = 0,
			.sink_stream = 0,
			.source_pad = priv->hw_data->num_rxports,
			.source_stream = 0,
			.flags = V4L2_SUBDEV_ROUTE_FL_ACTIVE,
		},
	};

	struct v4l2_subdev_krouting routing = {
		.num_routes = ARRAY_SIZE(routes),
		.routes = routes,
	};

	return _ub960_set_routing(sd, state, &routing);
}

static const struct v4l2_subdev_pad_ops ub960_pad_ops = {
	.enable_streams = ub960_enable_streams,
	.disable_streams = ub960_disable_streams,

	.set_routing = ub960_set_routing,
	.get_frame_desc = ub960_get_frame_desc,

	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = ub960_set_fmt,

	.init_cfg = ub960_init_cfg,
};

static int ub960_log_status(struct v4l2_subdev *sd)
{
	struct ub960_data *priv = sd_to_ub960(sd);
	struct device *dev = &priv->client->dev;
	struct v4l2_subdev_state *state;
	unsigned int nport;
	unsigned int i;
	u16 v16 = 0;
	u8 v = 0;
	u8 id[UB960_SR_FPD3_RX_ID_LEN];

	state = v4l2_subdev_lock_and_get_active_state(sd);

	for (i = 0; i < sizeof(id); i++)
		ub960_read(priv, UB960_SR_FPD3_RX_ID(i), &id[i]);

	dev_info(dev, "ID '%.*s'\n", (int)sizeof(id), id);

	for (nport = 0; nport < priv->hw_data->num_txports; nport++) {
		struct ub960_txport *txport = priv->txports[nport];

		dev_info(dev, "TX %u\n", nport);

		if (!txport) {
			dev_info(dev, "\tNot initialized\n");
			continue;
		}

		ub960_txport_read(priv, nport, UB960_TR_CSI_STS, &v);
		dev_info(dev, "\tsync %u, pass %u\n", v & (u8)BIT(1),
			 v & (u8)BIT(0));

		ub960_read16(priv, UB960_SR_CSI_FRAME_COUNT_HI(nport), &v16);
		dev_info(dev, "\tframe counter %u\n", v16);

		ub960_read16(priv, UB960_SR_CSI_FRAME_ERR_COUNT_HI(nport), &v16);
		dev_info(dev, "\tframe error counter %u\n", v16);

		ub960_read16(priv, UB960_SR_CSI_LINE_COUNT_HI(nport), &v16);
		dev_info(dev, "\tline counter %u\n", v16);

		ub960_read16(priv, UB960_SR_CSI_LINE_ERR_COUNT_HI(nport), &v16);
		dev_info(dev, "\tline error counter %u\n", v16);
	}

	for (nport = 0; nport < priv->hw_data->num_rxports; nport++) {
		struct ub960_rxport *rxport = priv->rxports[nport];
		u8 eq_level;
		s8 strobe_pos;
		unsigned int i;

		dev_info(dev, "RX %u\n", nport);

		if (!rxport) {
			dev_info(dev, "\tNot initialized\n");
			continue;
		}

		ub960_rxport_read(priv, nport, UB960_RR_RX_PORT_STS1, &v);

		if (v & UB960_RR_RX_PORT_STS1_LOCK_STS)
			dev_info(dev, "\tLocked\n");
		else
			dev_info(dev, "\tNot locked\n");

		dev_info(dev, "\trx_port_sts1 %#02x\n", v);
		ub960_rxport_read(priv, nport, UB960_RR_RX_PORT_STS2, &v);
		dev_info(dev, "\trx_port_sts2 %#02x\n", v);

		ub960_rxport_read16(priv, nport, UB960_RR_RX_FREQ_HIGH, &v16);
		dev_info(dev, "\tlink freq %llu Hz\n", (v16 * 1000000ULL) >> 8);

		ub960_rxport_read16(priv, nport, UB960_RR_RX_PAR_ERR_HI, &v16);
		dev_info(dev, "\tparity errors %u\n", v16);

		ub960_rxport_read16(priv, nport, UB960_RR_LINE_COUNT_HI, &v16);
		dev_info(dev, "\tlines per frame %u\n", v16);

		ub960_rxport_read16(priv, nport, UB960_RR_LINE_LEN_1, &v16);
		dev_info(dev, "\tbytes per line %u\n", v16);

		ub960_rxport_read(priv, nport, UB960_RR_CSI_ERR_COUNTER, &v);
		dev_info(dev, "\tcsi_err_counter %u\n", v);

		/* Strobe */

		ub960_read(priv, UB960_XR_AEQ_CTL1, &v);

		dev_info(dev, "\t%s strobe\n",
			 (v & UB960_XR_AEQ_CTL1_AEQ_SFILTER_EN) ? "Adaptive" :
								  "Manual");

		if (v & UB960_XR_AEQ_CTL1_AEQ_SFILTER_EN) {
			ub960_read(priv, UB960_XR_SFILTER_CFG, &v);

			dev_info(dev, "\tStrobe range [%d, %d]\n",
				 ((v >> UB960_XR_SFILTER_CFG_SFILTER_MIN_SHIFT) & 0xf) - 7,
				 ((v >> UB960_XR_SFILTER_CFG_SFILTER_MAX_SHIFT) & 0xf) - 7);
		}

		ub960_rxport_get_strobe_pos(priv, nport, &strobe_pos);

		dev_info(dev, "\tStrobe pos %d\n", strobe_pos);

		/* EQ */

		ub960_rxport_read(priv, nport, UB960_RR_AEQ_BYPASS, &v);

		dev_info(dev, "\t%s EQ\n",
			 (v & UB960_RR_AEQ_BYPASS_ENABLE) ? "Manual" :
							    "Adaptive");

		if (!(v & UB960_RR_AEQ_BYPASS_ENABLE)) {
			ub960_rxport_read(priv, nport, UB960_RR_AEQ_MIN_MAX, &v);

			dev_info(dev, "\tEQ range [%u, %u]\n",
				 (v >> UB960_RR_AEQ_MIN_MAX_AEQ_FLOOR_SHIFT) & 0xf,
				 (v >> UB960_RR_AEQ_MIN_MAX_AEQ_MAX_SHIFT) & 0xf);
		}

		if (ub960_rxport_get_eq_level(priv, nport, &eq_level) == 0)
			dev_info(dev, "\tEQ level %u\n", eq_level);

		/* GPIOs */
		for (i = 0; i < UB960_NUM_BC_GPIOS; i++) {
			u8 ctl_reg;
			u8 ctl_shift;

			ctl_reg = UB960_RR_BC_GPIO_CTL(i / 2);
			ctl_shift = (i % 2) * 4;

			ub960_rxport_read(priv, nport, ctl_reg, &v);

			dev_info(dev, "\tGPIO%u: mode %u\n", i,
				 (v >> ctl_shift) & 0xf);
		}
	}

	v4l2_subdev_unlock_state(state);

	return 0;
}

static const struct v4l2_subdev_core_ops ub960_subdev_core_ops = {
	.log_status = ub960_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_ops ub960_subdev_ops = {
	.core = &ub960_subdev_core_ops,
	.pad = &ub960_pad_ops,
};

static const struct media_entity_operations ub960_entity_ops = {
	.get_fwnode_pad = v4l2_subdev_get_fwnode_pad_1_to_1,
	.link_validate = v4l2_subdev_link_validate,
	.has_pad_interdep = v4l2_subdev_has_pad_interdep,
};

/* -----------------------------------------------------------------------------
 * Core
 */

static irqreturn_t ub960_handle_events(int irq, void *arg)
{
	struct ub960_data *priv = arg;
	unsigned int i;
	u8 int_sts;
	u8 fwd_sts;
	int ret;

	ret = ub960_read(priv, UB960_SR_INTERRUPT_STS, &int_sts);
	if (ret || !int_sts)
		return IRQ_NONE;

	dev_dbg(&priv->client->dev, "INTERRUPT_STS %x\n", int_sts);

	ret = ub960_read(priv, UB960_SR_FWD_STS, &fwd_sts);
	if (ret)
		return IRQ_NONE;

	dev_dbg(&priv->client->dev, "FWD_STS %#02x\n", fwd_sts);

	for (i = 0; i < priv->hw_data->num_txports; i++) {
		if (int_sts & UB960_SR_INTERRUPT_STS_IS_CSI_TX(i))
			ub960_csi_handle_events(priv, i);
	}

	for (i = 0; i < priv->hw_data->num_rxports; i++) {
		if (!priv->rxports[i])
			continue;

		if (int_sts & UB960_SR_INTERRUPT_STS_IS_RX(i))
			ub960_rxport_handle_events(priv, i);
	}

	return IRQ_HANDLED;
}

static void ub960_handler_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct ub960_data *priv =
		container_of(dwork, struct ub960_data, poll_work);

	ub960_handle_events(0, priv);

	schedule_delayed_work(&priv->poll_work,
			      msecs_to_jiffies(UB960_POLL_TIME_MS));
}

static void ub960_txport_free_ports(struct ub960_data *priv)
{
	unsigned int nport;

	for (nport = 0; nport < priv->hw_data->num_txports; nport++) {
		struct ub960_txport *txport = priv->txports[nport];

		if (!txport)
			continue;

		kfree(txport);
		priv->txports[nport] = NULL;
	}
}

static void ub960_rxport_free_ports(struct ub960_data *priv)
{
	unsigned int nport;

	for (nport = 0; nport < priv->hw_data->num_rxports; nport++) {
		struct ub960_rxport *rxport = priv->rxports[nport];

		if (!rxport)
			continue;

		fwnode_handle_put(rxport->source.ep_fwnode);
		fwnode_handle_put(rxport->ser.fwnode);

		kfree(rxport);
		priv->rxports[nport] = NULL;
	}
}

static int
ub960_parse_dt_rxport_link_properties(struct ub960_data *priv,
				      struct fwnode_handle *link_fwnode,
				      struct ub960_rxport *rxport)
{
	struct device *dev = &priv->client->dev;
	unsigned int nport = rxport->nport;
	u32 rx_mode;
	u32 cdr_mode;
	s32 strobe_pos;
	u32 eq_level;
	u32 ser_i2c_alias;
	int ret;

	cdr_mode = RXPORT_CDR_FPD3;

	ret = fwnode_property_read_u32(link_fwnode, "ti,cdr-mode", &cdr_mode);
	if (ret < 0 && ret != -EINVAL) {
		dev_err(dev, "rx%u: failed to read '%s': %d\n", nport,
			"ti,cdr-mode", ret);
		return ret;
	}

	if (cdr_mode > RXPORT_CDR_LAST) {
		dev_err(dev, "rx%u: bad 'ti,cdr-mode' %u\n", nport, cdr_mode);
		return -EINVAL;
	}

	if (!priv->hw_data->is_fpdlink4 && cdr_mode == RXPORT_CDR_FPD4) {
		dev_err(dev, "rx%u: FPD-Link 4 CDR not supported\n", nport);
		return -EINVAL;
	}

	rxport->cdr_mode = cdr_mode;

	ret = fwnode_property_read_u32(link_fwnode, "ti,rx-mode", &rx_mode);
	if (ret < 0) {
		dev_err(dev, "rx%u: failed to read '%s': %d\n", nport,
			"ti,rx-mode", ret);
		return ret;
	}

	if (rx_mode > RXPORT_MODE_LAST) {
		dev_err(dev, "rx%u: bad 'ti,rx-mode' %u\n", nport, rx_mode);
		return -EINVAL;
	}

	switch (rx_mode) {
	case RXPORT_MODE_RAW12_HF:
	case RXPORT_MODE_RAW12_LF:
		dev_err(dev, "rx%u: unsupported 'ti,rx-mode' %u\n", nport,
			rx_mode);
		return -EINVAL;
	default:
		break;
	}

	rxport->rx_mode = rx_mode;

	/* EQ & Strobe related */

	/* Defaults */
	rxport->eq.manual_eq = false;
	rxport->eq.aeq.eq_level_min = UB960_MIN_EQ_LEVEL;
	rxport->eq.aeq.eq_level_max = UB960_MAX_EQ_LEVEL;

	ret = fwnode_property_read_u32(link_fwnode, "ti,strobe-pos",
				       &strobe_pos);
	if (ret) {
		if (ret != -EINVAL) {
			dev_err(dev, "rx%u: failed to read '%s': %d\n", nport,
				"ti,strobe-pos", ret);
			return ret;
		}
	} else {
		if (strobe_pos < UB960_MIN_MANUAL_STROBE_POS ||
		    strobe_pos > UB960_MAX_MANUAL_STROBE_POS) {
			dev_err(dev, "rx%u: illegal 'strobe-pos' value: %d\n",
				nport, strobe_pos);
			return -EINVAL;
		}

		/* NOTE: ignored unless global manual strobe pos is also set */
		rxport->eq.strobe_pos = strobe_pos;
		if (!priv->strobe.manual)
			dev_warn(dev,
				 "rx%u: 'ti,strobe-pos' ignored as 'ti,manual-strobe' not set\n",
				 nport);
	}

	ret = fwnode_property_read_u32(link_fwnode, "ti,eq-level", &eq_level);
	if (ret) {
		if (ret != -EINVAL) {
			dev_err(dev, "rx%u: failed to read '%s': %d\n", nport,
				"ti,eq-level", ret);
			return ret;
		}
	} else {
		if (eq_level > UB960_MAX_EQ_LEVEL) {
			dev_err(dev, "rx%u: illegal 'ti,eq-level' value: %d\n",
				nport, eq_level);
			return -EINVAL;
		}

		rxport->eq.manual_eq = true;
		rxport->eq.manual.eq_level = eq_level;
	}

	ret = fwnode_property_read_u32(link_fwnode, "i2c-alias",
				       &ser_i2c_alias);
	if (ret) {
		dev_err(dev, "rx%u: failed to read '%s': %d\n", nport,
			"i2c-alias", ret);
		return ret;
	}
	rxport->ser.alias = ser_i2c_alias;

	rxport->ser.fwnode = fwnode_get_named_child_node(link_fwnode, "serializer");
	if (!rxport->ser.fwnode) {
		dev_err(dev, "rx%u: missing 'serializer' node\n", nport);
		return -EINVAL;
	}

	return 0;
}

static int ub960_parse_dt_rxport_ep_properties(struct ub960_data *priv,
					       struct fwnode_handle *ep_fwnode,
					       struct ub960_rxport *rxport)
{
	struct device *dev = &priv->client->dev;
	struct v4l2_fwnode_endpoint vep = {};
	unsigned int nport = rxport->nport;
	bool hsync_hi;
	bool vsync_hi;
	int ret;

	rxport->source.ep_fwnode = fwnode_graph_get_remote_endpoint(ep_fwnode);
	if (!rxport->source.ep_fwnode) {
		dev_err(dev, "rx%u: no remote endpoint\n", nport);
		return -ENODEV;
	}

	/* We currently have properties only for RAW modes */

	switch (rxport->rx_mode) {
	case RXPORT_MODE_RAW10:
	case RXPORT_MODE_RAW12_HF:
	case RXPORT_MODE_RAW12_LF:
		break;
	default:
		return 0;
	}

	vep.bus_type = V4L2_MBUS_PARALLEL;
	ret = v4l2_fwnode_endpoint_parse(ep_fwnode, &vep);
	if (ret) {
		dev_err(dev, "rx%u: failed to parse endpoint data\n", nport);
		goto err_put_source_ep_fwnode;
	}

	hsync_hi = !!(vep.bus.parallel.flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH);
	vsync_hi = !!(vep.bus.parallel.flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH);

	/* LineValid and FrameValid are inverse to the h/vsync active */
	rxport->lv_fv_pol = (hsync_hi ? UB960_RR_PORT_CONFIG2_LV_POL_LOW : 0) |
			    (vsync_hi ? UB960_RR_PORT_CONFIG2_FV_POL_LOW : 0);

	return 0;

err_put_source_ep_fwnode:
	fwnode_handle_put(rxport->source.ep_fwnode);
	return ret;
}

static int ub960_parse_dt_rxport(struct ub960_data *priv, unsigned int nport,
				 struct fwnode_handle *link_fwnode,
				 struct fwnode_handle *ep_fwnode)
{
	static const char *vpoc_names[UB960_MAX_RX_NPORTS] = {
		"vpoc0", "vpoc1", "vpoc2", "vpoc3"
	};
	struct device *dev = &priv->client->dev;
	struct ub960_rxport *rxport;
	int ret;

	rxport = kzalloc(sizeof(*rxport), GFP_KERNEL);
	if (!rxport)
		return -ENOMEM;

	priv->rxports[nport] = rxport;

	rxport->nport = nport;
	rxport->priv = priv;

	ret = ub960_parse_dt_rxport_link_properties(priv, link_fwnode, rxport);
	if (ret)
		goto err_free_rxport;

	rxport->vpoc = devm_regulator_get_optional(dev, vpoc_names[nport]);
	if (IS_ERR(rxport->vpoc)) {
		ret = PTR_ERR(rxport->vpoc);
		if (ret == -ENODEV) {
			rxport->vpoc = NULL;
		} else {
			dev_err(dev, "rx%u: failed to get VPOC supply: %d\n",
				nport, ret);
			goto err_put_remote_fwnode;
		}
	}

	ret = ub960_parse_dt_rxport_ep_properties(priv, ep_fwnode, rxport);
	if (ret)
		goto err_put_remote_fwnode;

	return 0;

err_put_remote_fwnode:
	fwnode_handle_put(rxport->ser.fwnode);
err_free_rxport:
	priv->rxports[nport] = NULL;
	kfree(rxport);
	return ret;
}

static struct fwnode_handle *
ub960_fwnode_get_link_by_regs(struct fwnode_handle *links_fwnode,
			      unsigned int nport)
{
	struct fwnode_handle *link_fwnode;
	int ret;

	fwnode_for_each_child_node(links_fwnode, link_fwnode) {
		u32 link_num;

		if (!str_has_prefix(fwnode_get_name(link_fwnode), "link@"))
			continue;

		ret = fwnode_property_read_u32(link_fwnode, "reg", &link_num);
		if (ret) {
			fwnode_handle_put(link_fwnode);
			return NULL;
		}

		if (nport == link_num)
			return link_fwnode;
	}

	return NULL;
}

static int ub960_parse_dt_rxports(struct ub960_data *priv)
{
	struct device *dev = &priv->client->dev;
	struct fwnode_handle *links_fwnode;
	unsigned int nport;
	int ret;

	links_fwnode = fwnode_get_named_child_node(dev_fwnode(dev), "links");
	if (!links_fwnode) {
		dev_err(dev, "'links' node missing\n");
		return -ENODEV;
	}

	/* Defaults, recommended by TI */
	priv->strobe.min = 2;
	priv->strobe.max = 3;

	priv->strobe.manual = fwnode_property_read_bool(links_fwnode, "ti,manual-strobe");

	for (nport = 0; nport < priv->hw_data->num_rxports; nport++) {
		struct fwnode_handle *link_fwnode;
		struct fwnode_handle *ep_fwnode;

		link_fwnode = ub960_fwnode_get_link_by_regs(links_fwnode, nport);
		if (!link_fwnode)
			continue;

		ep_fwnode = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev),
							    nport, 0, 0);
		if (!ep_fwnode) {
			fwnode_handle_put(link_fwnode);
			continue;
		}

		ret = ub960_parse_dt_rxport(priv, nport, link_fwnode,
					    ep_fwnode);

		fwnode_handle_put(link_fwnode);
		fwnode_handle_put(ep_fwnode);

		if (ret) {
			dev_err(dev, "rx%u: failed to parse RX port\n", nport);
			goto err_put_links;
		}
	}

	fwnode_handle_put(links_fwnode);

	return 0;

err_put_links:
	fwnode_handle_put(links_fwnode);

	return ret;
}

static int ub960_parse_dt_txports(struct ub960_data *priv)
{
	struct device *dev = &priv->client->dev;
	u32 nport;
	int ret;

	for (nport = 0; nport < priv->hw_data->num_txports; nport++) {
		unsigned int port = nport + priv->hw_data->num_rxports;
		struct fwnode_handle *ep_fwnode;

		ep_fwnode = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev),
							    port, 0, 0);
		if (!ep_fwnode)
			continue;

		ret = ub960_parse_dt_txport(priv, ep_fwnode, nport);

		fwnode_handle_put(ep_fwnode);

		if (ret)
			break;
	}

	return 0;
}

static int ub960_parse_dt(struct ub960_data *priv)
{
	int ret;

	ret = ub960_parse_dt_rxports(priv);
	if (ret)
		return ret;

	ret = ub960_parse_dt_txports(priv);
	if (ret)
		goto err_free_rxports;

	return 0;

err_free_rxports:
	ub960_rxport_free_ports(priv);

	return ret;
}

static int ub960_notify_bound(struct v4l2_async_notifier *notifier,
			      struct v4l2_subdev *subdev,
			      struct v4l2_async_connection *asd)
{
	struct ub960_data *priv = sd_to_ub960(notifier->sd);
	struct ub960_rxport *rxport = to_ub960_asd(asd)->rxport;
	struct device *dev = &priv->client->dev;
	u8 nport = rxport->nport;
	unsigned int i;
	int ret;

	ret = media_entity_get_fwnode_pad(&subdev->entity,
					  rxport->source.ep_fwnode,
					  MEDIA_PAD_FL_SOURCE);
	if (ret < 0) {
		dev_err(dev, "Failed to find pad for %s\n", subdev->name);
		return ret;
	}

	rxport->source.sd = subdev;
	rxport->source.pad = ret;

	ret = media_create_pad_link(&rxport->source.sd->entity,
				    rxport->source.pad, &priv->sd.entity, nport,
				    MEDIA_LNK_FL_ENABLED |
					    MEDIA_LNK_FL_IMMUTABLE);
	if (ret) {
		dev_err(dev, "Unable to link %s:%u -> %s:%u\n",
			rxport->source.sd->name, rxport->source.pad,
			priv->sd.name, nport);
		return ret;
	}

	for (i = 0; i < priv->hw_data->num_rxports; i++) {
		if (priv->rxports[i] && !priv->rxports[i]->source.sd) {
			dev_dbg(dev, "Waiting for more subdevs to be bound\n");
			return 0;
		}
	}

	return 0;
}

static void ub960_notify_unbind(struct v4l2_async_notifier *notifier,
				struct v4l2_subdev *subdev,
				struct v4l2_async_connection *asd)
{
	struct ub960_rxport *rxport = to_ub960_asd(asd)->rxport;

	rxport->source.sd = NULL;
}

static const struct v4l2_async_notifier_operations ub960_notify_ops = {
	.bound = ub960_notify_bound,
	.unbind = ub960_notify_unbind,
};

static int ub960_v4l2_notifier_register(struct ub960_data *priv)
{
	struct device *dev = &priv->client->dev;
	unsigned int i;
	int ret;

	v4l2_async_subdev_nf_init(&priv->notifier, &priv->sd);

	for (i = 0; i < priv->hw_data->num_rxports; i++) {
		struct ub960_rxport *rxport = priv->rxports[i];
		struct ub960_asd *asd;

		if (!rxport)
			continue;

		asd = v4l2_async_nf_add_fwnode(&priv->notifier,
					       rxport->source.ep_fwnode,
					       struct ub960_asd);
		if (IS_ERR(asd)) {
			dev_err(dev, "Failed to add subdev for source %u: %pe",
				i, asd);
			v4l2_async_nf_cleanup(&priv->notifier);
			return PTR_ERR(asd);
		}

		asd->rxport = rxport;
	}

	priv->notifier.ops = &ub960_notify_ops;

	ret = v4l2_async_nf_register(&priv->notifier);
	if (ret) {
		dev_err(dev, "Failed to register subdev_notifier");
		v4l2_async_nf_cleanup(&priv->notifier);
		return ret;
	}

	return 0;
}

static void ub960_v4l2_notifier_unregister(struct ub960_data *priv)
{
	v4l2_async_nf_unregister(&priv->notifier);
	v4l2_async_nf_cleanup(&priv->notifier);
}

static int ub960_create_subdev(struct ub960_data *priv)
{
	struct device *dev = &priv->client->dev;
	unsigned int i;
	int ret;

	v4l2_i2c_subdev_init(&priv->sd, priv->client, &ub960_subdev_ops);

	v4l2_ctrl_handler_init(&priv->ctrl_handler, 1);
	priv->sd.ctrl_handler = &priv->ctrl_handler;

	v4l2_ctrl_new_int_menu(&priv->ctrl_handler, NULL, V4L2_CID_LINK_FREQ,
			       ARRAY_SIZE(priv->tx_link_freq) - 1, 0,
			       priv->tx_link_freq);

	if (priv->ctrl_handler.error) {
		ret = priv->ctrl_handler.error;
		goto err_free_ctrl;
	}

	priv->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			  V4L2_SUBDEV_FL_HAS_EVENTS | V4L2_SUBDEV_FL_STREAMS;
	priv->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	priv->sd.entity.ops = &ub960_entity_ops;

	for (i = 0; i < priv->hw_data->num_rxports + priv->hw_data->num_txports; i++) {
		priv->pads[i].flags = ub960_pad_is_sink(priv, i) ?
					      MEDIA_PAD_FL_SINK :
					      MEDIA_PAD_FL_SOURCE;
	}

	ret = media_entity_pads_init(&priv->sd.entity,
				     priv->hw_data->num_rxports +
					     priv->hw_data->num_txports,
				     priv->pads);
	if (ret)
		goto err_free_ctrl;

	priv->sd.state_lock = priv->sd.ctrl_handler->lock;

	ret = v4l2_subdev_init_finalize(&priv->sd);
	if (ret)
		goto err_entity_cleanup;

	ret = ub960_v4l2_notifier_register(priv);
	if (ret) {
		dev_err(dev, "v4l2 subdev notifier register failed: %d\n", ret);
		goto err_subdev_cleanup;
	}

	ret = v4l2_async_register_subdev(&priv->sd);
	if (ret) {
		dev_err(dev, "v4l2_async_register_subdev error: %d\n", ret);
		goto err_unreg_notif;
	}

	return 0;

err_unreg_notif:
	ub960_v4l2_notifier_unregister(priv);
err_subdev_cleanup:
	v4l2_subdev_cleanup(&priv->sd);
err_entity_cleanup:
	media_entity_cleanup(&priv->sd.entity);
err_free_ctrl:
	v4l2_ctrl_handler_free(&priv->ctrl_handler);

	return ret;
}

static void ub960_destroy_subdev(struct ub960_data *priv)
{
	ub960_v4l2_notifier_unregister(priv);
	v4l2_async_unregister_subdev(&priv->sd);

	v4l2_subdev_cleanup(&priv->sd);

	media_entity_cleanup(&priv->sd.entity);
	v4l2_ctrl_handler_free(&priv->ctrl_handler);
}

static const struct regmap_config ub960_regmap_config = {
	.name = "ds90ub960",

	.reg_bits = 8,
	.val_bits = 8,

	.max_register = 0xff,

	/*
	 * We do locking in the driver to cover the TX/RX port selection and the
	 * indirect register access.
	 */
	.disable_locking = true,
};

static void ub960_reset(struct ub960_data *priv, bool reset_regs)
{
	struct device *dev = &priv->client->dev;
	unsigned int v;
	int ret;
	u8 bit;

	bit = reset_regs ? UB960_SR_RESET_DIGITAL_RESET1 :
			   UB960_SR_RESET_DIGITAL_RESET0;

	ub960_write(priv, UB960_SR_RESET, bit);

	mutex_lock(&priv->reg_lock);

	ret = regmap_read_poll_timeout(priv->regmap, UB960_SR_RESET, v,
				       (v & bit) == 0, 2000, 100000);

	mutex_unlock(&priv->reg_lock);

	if (ret)
		dev_err(dev, "reset failed: %d\n", ret);
}

static int ub960_get_hw_resources(struct ub960_data *priv)
{
	struct device *dev = &priv->client->dev;

	priv->regmap = devm_regmap_init_i2c(priv->client, &ub960_regmap_config);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	priv->vddio = devm_regulator_get(dev, "vddio");
	if (IS_ERR(priv->vddio))
		return dev_err_probe(dev, PTR_ERR(priv->vddio),
				     "cannot get VDDIO regulator\n");

	/* get power-down pin from DT */
	priv->pd_gpio =
		devm_gpiod_get_optional(dev, "powerdown", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->pd_gpio))
		return dev_err_probe(dev, PTR_ERR(priv->pd_gpio),
				     "Cannot get powerdown GPIO\n");

	priv->refclk = devm_clk_get(dev, "refclk");
	if (IS_ERR(priv->refclk))
		return dev_err_probe(dev, PTR_ERR(priv->refclk),
				     "Cannot get REFCLK\n");

	return 0;
}

static int ub960_enable_core_hw(struct ub960_data *priv)
{
	struct device *dev = &priv->client->dev;
	u8 rev_mask;
	int ret;
	u8 dev_sts;
	u8 refclk_freq;

	ret = regulator_enable(priv->vddio);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to enable VDDIO regulator\n");

	ret = clk_prepare_enable(priv->refclk);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to enable refclk\n");
		goto err_disable_vddio;
	}

	if (priv->pd_gpio) {
		gpiod_set_value_cansleep(priv->pd_gpio, 1);
		/* wait min 2 ms for reset to complete */
		fsleep(2000);
		gpiod_set_value_cansleep(priv->pd_gpio, 0);
		/* wait min 2 ms for power up to finish */
		fsleep(2000);
	}

	ub960_reset(priv, true);

	/* Runtime check register accessibility */
	ret = ub960_read(priv, UB960_SR_REV_MASK, &rev_mask);
	if (ret) {
		dev_err_probe(dev, ret, "Cannot read first register, abort\n");
		goto err_pd_gpio;
	}

	dev_dbg(dev, "Found %s (rev/mask %#04x)\n", priv->hw_data->model,
		rev_mask);

	ret = ub960_read(priv, UB960_SR_DEVICE_STS, &dev_sts);
	if (ret)
		goto err_pd_gpio;

	ret = ub960_read(priv, UB960_XR_REFCLK_FREQ, &refclk_freq);
	if (ret)
		goto err_pd_gpio;

	dev_dbg(dev, "refclk valid %u freq %u MHz (clk fw freq %lu MHz)\n",
		!!(dev_sts & BIT(4)), refclk_freq,
		clk_get_rate(priv->refclk) / 1000000);

	/* Disable all RX ports by default */
	ret = ub960_write(priv, UB960_SR_RX_PORT_CTL, 0);
	if (ret)
		goto err_pd_gpio;

	/* release GPIO lock */
	if (priv->hw_data->is_ub9702) {
		ret = ub960_update_bits(priv, UB960_SR_RESET,
					UB960_SR_RESET_GPIO_LOCK_RELEASE,
					UB960_SR_RESET_GPIO_LOCK_RELEASE);
		if (ret)
			goto err_pd_gpio;
	}

	return 0;

err_pd_gpio:
	gpiod_set_value_cansleep(priv->pd_gpio, 1);
	clk_disable_unprepare(priv->refclk);
err_disable_vddio:
	regulator_disable(priv->vddio);

	return ret;
}

static void ub960_disable_core_hw(struct ub960_data *priv)
{
	gpiod_set_value_cansleep(priv->pd_gpio, 1);
	clk_disable_unprepare(priv->refclk);
	regulator_disable(priv->vddio);
}

static int ub960_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ub960_data *priv;
	unsigned int port_lock_mask;
	unsigned int port_mask;
	unsigned int nport;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;

	priv->hw_data = device_get_match_data(dev);

	mutex_init(&priv->reg_lock);

	INIT_DELAYED_WORK(&priv->poll_work, ub960_handler_work);

	/*
	 * Initialize these to invalid values so that the first reg writes will
	 * configure the target.
	 */
	priv->reg_current.indirect_target = 0xff;
	priv->reg_current.rxport = 0xff;
	priv->reg_current.txport = 0xff;

	ret = ub960_get_hw_resources(priv);
	if (ret)
		goto err_mutex_destroy;

	ret = ub960_enable_core_hw(priv);
	if (ret)
		goto err_mutex_destroy;

	ret = ub960_parse_dt(priv);
	if (ret)
		goto err_disable_core_hw;

	ret = ub960_init_tx_ports(priv);
	if (ret)
		goto err_free_ports;

	ret = ub960_rxport_enable_vpocs(priv);
	if (ret)
		goto err_free_ports;

	ret = ub960_init_rx_ports(priv);
	if (ret)
		goto err_disable_vpocs;

	ub960_reset(priv, false);

	port_mask = 0;

	for (nport = 0; nport < priv->hw_data->num_rxports; nport++) {
		struct ub960_rxport *rxport = priv->rxports[nport];

		if (!rxport)
			continue;

		port_mask |= BIT(nport);
	}

	ret = ub960_rxport_wait_locks(priv, port_mask, &port_lock_mask);
	if (ret)
		goto err_disable_vpocs;

	if (port_mask != port_lock_mask) {
		ret = -EIO;
		dev_err_probe(dev, ret, "Failed to lock all RX ports\n");
		goto err_disable_vpocs;
	}

	/*
	 * Clear any errors caused by switching the RX port settings while
	 * probing.
	 */
	ub960_clear_rx_errors(priv);

	ret = ub960_init_atr(priv);
	if (ret)
		goto err_disable_vpocs;

	ret = ub960_rxport_add_serializers(priv);
	if (ret)
		goto err_uninit_atr;

	ret = ub960_create_subdev(priv);
	if (ret)
		goto err_free_sers;

	if (client->irq)
		dev_warn(dev, "irq support not implemented, using polling\n");

	schedule_delayed_work(&priv->poll_work,
			      msecs_to_jiffies(UB960_POLL_TIME_MS));

	return 0;

err_free_sers:
	ub960_rxport_remove_serializers(priv);
err_uninit_atr:
	ub960_uninit_atr(priv);
err_disable_vpocs:
	ub960_rxport_disable_vpocs(priv);
err_free_ports:
	ub960_rxport_free_ports(priv);
	ub960_txport_free_ports(priv);
err_disable_core_hw:
	ub960_disable_core_hw(priv);
err_mutex_destroy:
	mutex_destroy(&priv->reg_lock);
	return ret;
}

static void ub960_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ub960_data *priv = sd_to_ub960(sd);

	cancel_delayed_work_sync(&priv->poll_work);

	ub960_destroy_subdev(priv);
	ub960_rxport_remove_serializers(priv);
	ub960_uninit_atr(priv);
	ub960_rxport_disable_vpocs(priv);
	ub960_rxport_free_ports(priv);
	ub960_txport_free_ports(priv);
	ub960_disable_core_hw(priv);
	mutex_destroy(&priv->reg_lock);
}

static const struct ub960_hw_data ds90ub960_hw = {
	.model = "ub960",
	.num_rxports = 4,
	.num_txports = 2,
};

static const struct ub960_hw_data ds90ub9702_hw = {
	.model = "ub9702",
	.num_rxports = 4,
	.num_txports = 2,
	.is_ub9702 = true,
	.is_fpdlink4 = true,
};

static const struct i2c_device_id ub960_id[] = {
	{ "ds90ub960-q1", (kernel_ulong_t)&ds90ub960_hw },
	{ "ds90ub9702-q1", (kernel_ulong_t)&ds90ub9702_hw },
	{}
};
MODULE_DEVICE_TABLE(i2c, ub960_id);

static const struct of_device_id ub960_dt_ids[] = {
	{ .compatible = "ti,ds90ub960-q1", .data = &ds90ub960_hw },
	{ .compatible = "ti,ds90ub9702-q1", .data = &ds90ub9702_hw },
	{}
};
MODULE_DEVICE_TABLE(of, ub960_dt_ids);

static struct i2c_driver ds90ub960_driver = {
	.probe		= ub960_probe,
	.remove		= ub960_remove,
	.id_table	= ub960_id,
	.driver = {
		.name	= "ds90ub960",
		.of_match_table = ub960_dt_ids,
	},
};
module_i2c_driver(ds90ub960_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Texas Instruments FPD-Link III/IV Deserializers Driver");
MODULE_AUTHOR("Luca Ceresoli <luca@lucaceresoli.net>");
MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ideasonboard.com>");
MODULE_IMPORT_NS(I2C_ATR);
