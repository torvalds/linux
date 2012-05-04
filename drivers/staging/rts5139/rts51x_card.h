/* Driver for Realtek RTS51xx USB card reader
 * Header file
 *
 * Copyright(c) 2009 Realtek Semiconductor Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   wwang (wei_wang@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 * Maintainer:
 *   Edwin Rong (edwin_rong@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 */

#ifndef __RTS51X_CARD_H
#define __RTS51X_CARD_H

#include "rts51x_chip.h"

/* Register bit definition */

/* Card Power Control Register */
#define POWER_OFF			0x03
#define PARTIAL_POWER_ON		0x02
#define POWER_ON			0x00
#define POWER_MASK			0x03
#define LDO3318_PWR_MASK		0x0C
#define LDO_ON				0x00
#define LDO_SUSPEND			0x08
#define LDO_OFF				0x0C
#define DV3318_AUTO_PWR_OFF		0x10
#define FORCE_LDO_POWERB	0x60

/* Card Output Enable Register */
#define XD_OUTPUT_EN			0x02
#define SD_OUTPUT_EN			0x04
#define MS_OUTPUT_EN			0x08

/* System Clock Control Register */

/* System Clock Divider Register */
#define CLK_CHANGE			0x80
#define CLK_DIV_1			0x00
#define CLK_DIV_2			0x01
#define CLK_DIV_4			0x02
#define CLK_DIV_8			0x03

/* System Clock Select Register */
#define SSC_60				0
#define SSC_80				1
#define SSC_100				2
#define SSC_120				3
#define SSC_150				4

/* Card Clock Enable Register */
#define XD_CLK_EN			0x02
#define SD_CLK_EN			0x04
#define MS_CLK_EN			0x08

/* Card Select Register */
#define XD_MOD_SEL			1
#define SD_MOD_SEL			2
#define MS_MOD_SEL			3

/* Card Transfer Reset Register */
#define XD_STOP				0x02
#define SD_STOP				0x04
#define MS_STOP				0x08
#define XD_CLR_ERR			0x20
#define SD_CLR_ERR			0x40
#define MS_CLR_ERR			0x80

/* SD30_drive_sel */
#define SD30_DRIVE_MASK	0x07

/* CARD_DRIVE_SEL */
#define SD20_DRIVE_MASK	0x03
#define DRIVE_4mA			0x00
#define DRIVE_8mA			0x01
#define DRIVE_12mA			0x02

/* FPGA_PULL_CTL */
#define FPGA_MS_PULL_CTL_EN		0xEF
#define FPGA_SD_PULL_CTL_EN		0xF7
#define FPGA_XD_PULL_CTL_EN1		0xFE
#define FPGA_XD_PULL_CTL_EN2		0xFD
#define FPGA_XD_PULL_CTL_EN3		0xFB

#define FPGA_MS_PULL_CTL_BIT		0x10
#define FPGA_SD_PULL_CTL_BIT		0x08

/* Card Data Source Register */
#define PINGPONG_BUFFER			0x01
#define RING_BUFFER			0x00

/* SFSM_ED */
#define HW_CMD_STOP			0x80
#define CLR_STAGE_STALL			0x08
#define CARD_ERR				0x10

/* CARD_SHARE_MODE */
#define	CARD_SHARE_LQFP48		0x04
#define	CARD_SHARE_QFN24		0x00
#define CARD_SHARE_LQFP_SEL		0x04
#define	CARD_SHARE_XD			0x00
#define	CARD_SHARE_SD			0x01
#define	CARD_SHARE_MS			0x02
#define CARD_SHARE_MASK			0x03

/* CARD_AUTO_BLINK */
#define BLINK_ENABLE			0x08
#define BLINK_SPEED_MASK		0x07

/* CARD_GPIO */
#define GPIO_OE				0x02
#define GPIO_OUTPUT			0x01

/* CARD_CLK_SOURCE */
#define CRC_FIX_CLK			(0x00 << 0)
#define CRC_VAR_CLK0			(0x01 << 0)
#define CRC_VAR_CLK1			(0x02 << 0)
#define SD30_FIX_CLK			(0x00 << 2)
#define SD30_VAR_CLK0			(0x01 << 2)
#define SD30_VAR_CLK1			(0x02 << 2)
#define SAMPLE_FIX_CLK			(0x00 << 4)
#define SAMPLE_VAR_CLK0			(0x01 << 4)
#define SAMPLE_VAR_CLK1			(0x02 << 4)

/* DCM_DRP_CTL */
#define DCM_RESET			0x08
#define DCM_LOCKED			0x04
#define DCM_208M			0x00
#define DCM_TX			        0x01
#define DCM_RX			        0x02

/* DCM_DRP_TRIG */
#define DRP_START			0x80
#define DRP_DONE			0x40

/* DCM_DRP_CFG */
#define DRP_WRITE			0x80
#define DRP_READ			0x00
#define DCM_WRITE_ADDRESS_50		0x50
#define DCM_WRITE_ADDRESS_51		0x51
#define DCM_READ_ADDRESS_00		0x00
#define DCM_READ_ADDRESS_51		0x51

/* HW_VERSION */
#define FPGA_VER			0x80
#define HW_VER_MASK			0x0F

/* CD_DEGLITCH_EN */
#define DISABLE_SD_CD			0x08
#define DISABLE_MS_CD			0x10
#define DISABLE_XD_CD			0x20
#define SD_CD_DEGLITCH_EN		0x01
#define MS_CD_DEGLITCH_EN		0x02
#define XD_CD_DEGLITCH_EN		0x04

/* OCPCTL */
#define CARD_OC_DETECT_EN		0x08
#define CARD_OC_CLR			0x01

/* CARD_DMA1_CTL */
#define EXTEND_DMA1_ASYNC_SIGNAL	0x02

/* HS_USB_STAT */
#define USB_HI_SPEED			0x01

/* CFG_MODE_1 */
#define RTS5179				0x02

/* SYS_DUMMY0 */
#define NYET_EN				0x01
#define NYET_MSAK			0x01

/* SSC_CTL1 */
#define SSC_RSTB			0x80
#define SSC_8X_EN			0x40
#define SSC_FIX_FRAC			0x20
#define SSC_SEL_1M			0x00
#define SSC_SEL_2M			0x08
#define SSC_SEL_4M			0x10
#define SSC_SEL_8M			0x18

/* SSC_CTL2 */
#define SSC_DEPTH_MASK			0x03
#define SSC_DEPTH_DISALBE		0x00
#define SSC_DEPTH_2M			0x01
#define SSC_DEPTH_1M			0x02
#define SSC_DEPTH_512K			0x03

/* LDO_POWER_CFG */
#define TUNE_SD18_MASK			0x1C
#define TUNE_SD18_1V7			0x00
#define TUNE_SD18_1V8			(0x01 << 2)
#define TUNE_SD18_1V9			(0x02 << 2)
#define TUNE_SD18_2V0			(0x03 << 2)
#define TUNE_SD18_2V7			(0x04 << 2)
#define TUNE_SD18_2V8			(0x05 << 2)
#define TUNE_SD18_2V9			(0x06 << 2)
#define TUNE_SD18_3V3			(0x07 << 2)

/* XD_CP_WAITTIME */
#define WAIT_1F				0x00
#define WAIT_3F				0x01
#define WAIT_7F				0x02
#define WAIT_FF				0x03

/* XD_INIT */
#define	XD_PWR_OFF_DELAY0		0x00
#define	XD_PWR_OFF_DELAY1		0x02
#define	XD_PWR_OFF_DELAY2		0x04
#define	XD_PWR_OFF_DELAY3		0x06
#define	XD_AUTO_PWR_OFF_EN		0xF7
#define	XD_NO_AUTO_PWR_OFF		0x08

/* XD_DTCTL */
/* XD_CATCTL */
#define	XD_TIME_RWN_1			0x00
#define	XD_TIME_RWN_STEP		0x20
#define	XD_TIME_RW_1			0x00
#define	XD_TIME_RW_STEP			0x04
#define	XD_TIME_SETUP_1			0x00
#define	XD_TIME_SETUP_STEP		0x01

/* XD_CTL */
#define	XD_ECC2_UNCORRECTABLE		0x80
#define	XD_ECC2_ERROR			0x40
#define	XD_ECC1_UNCORRECTABLE		0x20
#define	XD_ECC1_ERROR			0x10
#define	XD_RDY				0x04
#define	XD_CE_EN			0xFD
#define	XD_CE_DISEN			0x02
#define	XD_WP_EN			0xFE
#define	XD_WP_DISEN			0x01

/* XD_TRANSFER */
#define	XD_TRANSFER_START		0x80
#define	XD_TRANSFER_END			0x40
#define	XD_PPB_EMPTY			0x20
#define	XD_ERR				0x10
#define	XD_RESET			0x00
#define	XD_ERASE			0x01
#define	XD_READ_STATUS			0x02
#define	XD_READ_ID			0x03
#define	XD_READ_REDUNDANT		0x04
#define	XD_READ_PAGES			0x05
#define	XD_SET_CMD			0x06
#define	XD_NORMAL_READ			0x07
#define	XD_WRITE_PAGES			0x08
#define	XD_NORMAL_WRITE			0x09
#define	XD_WRITE_REDUNDANT		0x0A
#define	XD_SET_ADDR			0x0B
#define XD_COPY_PAGES			0x0C

/* XD_CFG */
#define	XD_PPB_TO_SIE			0x80
#define	XD_TO_PPB_ONLY			0x00
#define	XD_BA_TRANSFORM			0x40
#define	XD_BA_NO_TRANSFORM		0x00
#define	XD_NO_CALC_ECC			0x20
#define	XD_CALC_ECC			0x00
#define	XD_IGNORE_ECC			0x10
#define	XD_CHECK_ECC			0x00
#define	XD_DIRECT_TO_RB			0x08
#define XD_ADDR_MASK			0x07
#define	XD_ADDR_LENGTH_0		0x00
#define	XD_ADDR_LENGTH_1		0x01
#define	XD_ADDR_LENGTH_2		0x02
#define	XD_ADDR_LENGTH_3		0x03
#define	XD_ADDR_LENGTH_4		0x04

/* XD_PAGE_STATUS */
#define	XD_GPG				0xFF
#define	XD_BPG				0x00

/* XD_BLOCK_STATUS */
#define	XD_GBLK				0xFF
#define	XD_LATER_BBLK			0xF0

/* XD_PARITY */
#define	XD_ECC2_ALL1			0x80
#define	XD_ECC1_ALL1			0x40
#define	XD_BA2_ALL0			0x20
#define	XD_BA1_ALL0			0x10
#define	XD_BA1_BA2_EQL			0x04
#define	XD_BA2_VALID			0x02
#define	XD_BA1_VALID			0x01

/* XD_CHK_DATA_STATUS */
#define	XD_PGSTS_ZEROBIT_OVER4		0x00
#define	XD_PGSTS_NOT_FF			0x02
#define	XD_AUTO_CHK_DATA_STATUS		0x01

/* SD_CFG1 */
#define SD_CLK_DIVIDE_0			0x00
#define	SD_CLK_DIVIDE_256		0xC0
#define	SD_CLK_DIVIDE_128		0x80
#define SD_CLK_DIVIDE_MASK		0xC0
#define	SD_BUS_WIDTH_1			0x00
#define	SD_BUS_WIDTH_4			0x01
#define	SD_BUS_WIDTH_8			0x02
#define	SD_ASYNC_FIFO_RST		0x10
#define	SD_20_MODE			0x00
#define	SD_DDR_MODE			0x04
#define	SD_30_MODE			0x08

/* SD_CFG2 */
#define	SD_CALCULATE_CRC7		0x00
#define	SD_NO_CALCULATE_CRC7		0x80
#define	SD_CHECK_CRC16			0x00
#define	SD_NO_CHECK_CRC16		0x40
#define SD_WAIT_CRC_TO_EN		0x20
#define	SD_WAIT_BUSY_END		0x08
#define	SD_NO_WAIT_BUSY_END		0x00
#define	SD_CHECK_CRC7			0x00
#define	SD_NO_CHECK_CRC7		0x04
#define	SD_RSP_LEN_0			0x00
#define	SD_RSP_LEN_6			0x01
#define	SD_RSP_LEN_17			0x02
/* SD/MMC Response Type Definition */
/* SD_CALCULATE_CRC7, SD_CHECK_CRC16,
 * SD_NO_WAIT_BUSY_END, SD_NO_CHECK_CRC7,
 * SD_RSP_LEN_0 */
#define	SD_RSP_TYPE_R0			0x04
/* SD_CALCULATE_CRC7, SD_CHECK_CRC16,
 * SD_NO_WAIT_BUSY_END, SD_CHECK_CRC7,
 * SD_RSP_LEN_6 */
#define	SD_RSP_TYPE_R1			0x01
/* SD_CALCULATE_CRC7, SD_CHECK_CRC16,
 * SD_WAIT_BUSY_END, SD_CHECK_CRC7,
 * SD_RSP_LEN_6 */
#define	SD_RSP_TYPE_R1b			0x09
/* SD_CALCULATE_CRC7, SD_CHECK_CRC16,
 * SD_NO_WAIT_BUSY_END, SD_CHECK_CRC7,
 * SD_RSP_LEN_17 */
#define	SD_RSP_TYPE_R2			0x02
/* SD_CALCULATE_CRC7, SD_CHECK_CRC16,
 * SD_NO_WAIT_BUSY_END, SD_NO_CHECK_CRC7,
 * SD_RSP_LEN_6 */
#define	SD_RSP_TYPE_R3			0x05
/* SD_CALCULATE_CRC7, SD_CHECK_CRC16,
 * SD_NO_WAIT_BUSY_END, SD_NO_CHECK_CRC7,
 * SD_RSP_LEN_6 */
#define	SD_RSP_TYPE_R4			0x05
/* SD_CALCULATE_CRC7, SD_CHECK_CRC16,
 * SD_NO_WAIT_BUSY_END, SD_CHECK_CRC7,
 * SD_RSP_LEN_6 */
#define	SD_RSP_TYPE_R5			0x01
/* SD_CALCULATE_CRC7, SD_CHECK_CRC16,
 * SD_NO_WAIT_BUSY_END, SD_CHECK_CRC7,
 * SD_RSP_LEN_6 */
#define	SD_RSP_TYPE_R6			0x01
/* SD_CALCULATE_CRC7, SD_CHECK_CRC16,
 * SD_NO_WAIT_BUSY_END, SD_CHECK_CRC7,
 * SD_RSP_LEN_6  */
#define	SD_RSP_TYPE_R7			0x01

/* SD_CFG3 */
#define	SD_RSP_80CLK_TIMEOUT_EN		0x01

/* SD_STAT1 */
#define	SD_CRC7_ERR			0x80
#define	SD_CRC16_ERR			0x40
#define	SD_CRC_WRITE_ERR		0x20
#define	SD_CRC_WRITE_ERR_MASK		0x1C
#define	GET_CRC_TIME_OUT		0x02
#define	SD_TUNING_COMPARE_ERR		0x01

/* SD_STAT2 */
#define	SD_RSP_80CLK_TIMEOUT		0x01

/* SD_BUS_STAT */
#define	SD_CLK_TOGGLE_EN		0x80
#define	SD_CLK_FORCE_STOP	        0x40
#define	SD_DAT3_STATUS		        0x10
#define	SD_DAT2_STATUS		        0x08
#define	SD_DAT1_STATUS		        0x04
#define	SD_DAT0_STATUS		        0x02
#define	SD_CMD_STATUS			0x01

/* SD_PAD_CTL */
#define	SD_IO_USING_1V8		        0x80
#define	SD_IO_USING_3V3		        0x7F
#define	TYPE_A_DRIVING		        0x00
#define	TYPE_B_DRIVING			0x01
#define	TYPE_C_DRIVING			0x02
#define	TYPE_D_DRIVING		        0x03

/* SD_SAMPLE_POINT_CTL */
#define	DDR_FIX_RX_DAT			0x00
#define	DDR_VAR_RX_DAT			0x80
#define	DDR_FIX_RX_DAT_EDGE		0x00
#define	DDR_FIX_RX_DAT_14_DELAY		0x40
#define	DDR_FIX_RX_CMD			0x00
#define	DDR_VAR_RX_CMD			0x20
#define	DDR_FIX_RX_CMD_POS_EDGE		0x00
#define	DDR_FIX_RX_CMD_14_DELAY		0x10
#define	SD20_RX_POS_EDGE		0x00
#define	SD20_RX_14_DELAY		0x08
#define SD20_RX_SEL_MASK		0x08

/* SD_PUSH_POINT_CTL */
#define	DDR_FIX_TX_CMD_DAT		0x00
#define	DDR_VAR_TX_CMD_DAT		0x80
#define	DDR_FIX_TX_DAT_14_TSU		0x00
#define	DDR_FIX_TX_DAT_12_TSU		0x40
#define	DDR_FIX_TX_CMD_NEG_EDGE		0x00
#define	DDR_FIX_TX_CMD_14_AHEAD		0x20
#define	SD20_TX_NEG_EDGE		0x00
#define	SD20_TX_14_AHEAD		0x10
#define SD20_TX_SEL_MASK		0x10
#define	DDR_VAR_SDCLK_POL_SWAP		0x01

/* SD_TRANSFER */
#define	SD_TRANSFER_START		0x80
#define	SD_TRANSFER_END			0x40
#define SD_STAT_IDLE			0x20
#define	SD_TRANSFER_ERR			0x10
/* SD Transfer Mode definition */
#define	SD_TM_NORMAL_WRITE		0x00
#define	SD_TM_AUTO_WRITE_3		0x01
#define	SD_TM_AUTO_WRITE_4		0x02
#define	SD_TM_AUTO_READ_3		0x05
#define	SD_TM_AUTO_READ_4		0x06
#define	SD_TM_CMD_RSP			0x08
#define	SD_TM_AUTO_WRITE_1		0x09
#define	SD_TM_AUTO_WRITE_2		0x0A
#define	SD_TM_NORMAL_READ		0x0C
#define	SD_TM_AUTO_READ_1		0x0D
#define	SD_TM_AUTO_READ_2		0x0E
#define	SD_TM_AUTO_TUNING		0x0F

/* SD_VPTX_CTL / SD_VPRX_CTL */
#define PHASE_CHANGE			0x80
#define PHASE_NOT_RESET			0x40

/* SD_DCMPS_TX_CTL / SD_DCMPS_RX_CTL */
#define DCMPS_CHANGE			0x80
#define DCMPS_CHANGE_DONE		0x40
#define DCMPS_ERROR			0x20
#define DCMPS_CURRENT_PHASE		0x1F

/* SD_CMD_STATE */
#define SD_CMD_IDLE			0x80

/* SD_DATA_STATE */
#define SD_DATA_IDLE			0x80

/* MS_BLKEND */
#define SET_BLKEND			0x01

/* MS_CFG */
#define	SAMPLE_TIME_RISING		0x00
#define	SAMPLE_TIME_FALLING		0x80
#define	PUSH_TIME_DEFAULT		0x00
#define	PUSH_TIME_ODD			0x40
#define	NO_EXTEND_TOGGLE		0x00
#define	EXTEND_TOGGLE_CHK		0x20
#define	MS_BUS_WIDTH_1			0x00
#define	MS_BUS_WIDTH_4			0x10
#define	MS_BUS_WIDTH_8			0x18
#define	MS_2K_SECTOR_MODE		0x04
#define	MS_512_SECTOR_MODE		0x00
#define	MS_TOGGLE_TIMEOUT_EN		0x00
#define	MS_TOGGLE_TIMEOUT_DISEN		0x01
#define MS_NO_CHECK_INT			0x02

/* MS_TRANS_CFG */
#define	WAIT_INT			0x80
#define	NO_WAIT_INT			0x00
#define	NO_AUTO_READ_INT_REG		0x00
#define	AUTO_READ_INT_REG		0x40
#define	MS_CRC16_ERR			0x20
#define	MS_RDY_TIMEOUT			0x10
#define	MS_INT_CMDNK			0x08
#define	MS_INT_BREQ			0x04
#define	MS_INT_ERR			0x02
#define	MS_INT_CED			0x01

/* MS_TRANSFER */
#define	MS_TRANSFER_START		0x80
#define	MS_TRANSFER_END			0x40
#define	MS_TRANSFER_ERR			0x20
#define	MS_BS_STATE			0x10
#define	MS_TM_READ_BYTES		0x00
#define	MS_TM_NORMAL_READ		0x01
#define	MS_TM_WRITE_BYTES		0x04
#define	MS_TM_NORMAL_WRITE		0x05
#define	MS_TM_AUTO_READ			0x08
#define	MS_TM_AUTO_WRITE		0x0C
#define MS_TM_SET_CMD			0x06
#define MS_TM_COPY_PAGE			0x07
#define MS_TM_MULTI_READ		0x02
#define MS_TM_MULTI_WRITE		0x03

/* MC_DMA_CTL */
#define DMA_TC_EQ_0			0x80
#define DMA_DIR_TO_CARD			0x00
#define DMA_DIR_FROM_CARD		0x02
#define DMA_EN				0x01
#define DMA_128				(0 << 2)
#define DMA_256				(1 << 2)
#define DMA_512				(2 << 2)
#define DMA_1024			(3 << 2)
#define DMA_PACK_SIZE_MASK		0x0C

/* CARD_INT_PEND */
#define XD_INT				0x10
#define MS_INT				0x08
#define SD_INT				0x04

/* MC_FIFO_CTL */
#define FIFO_FLUSH			0x01

/* AUTO_DELINK_EN */
#define AUTO_DELINK			0x02
#define FORCE_DELINK			0x01

/* MC_DMA_RST */
#define DMA_RESET  0x01

#define SSC_POWER_MASK			0x01
#define SSC_POWER_DOWN			0x01
#define SSC_POWER_ON			0x00

/* OCPCTL */
#define MS_OCP_DETECT_EN		0x08
#define	MS_OCP_INT_EN			0x04
#define	MS_OCP_INT_CLR			0x02
#define	MS_OCP_CLEAR			0x01

/* OCPSTAT */
#define MS_OCP_DETECT			0x80
#define MS_OCP_NOW			0x02
#define MS_OCP_EVER			0x01

/* MC_FIFO_STAT */
#define FIFO_FULL		0x01
#define FIFO_EMPTY		0x02

/* RCCTL */
#define U_HW_CMD_EN_MASK		0x02
#define U_HW_CMD_EN			0x02
#define U_HW_CMD_DIS			0x00

/* Register address */
#define FPDCTL				0xFC00
#define SSC_DIV_N_0			0xFC07
#define SSC_CTL1			0xFC09
#define SSC_CTL2			0xFC0A
#define CFG_MODE_1		0xFC0F
#define RCCTL			0xFC14
#define SYS_DUMMY0			0xFC30
#define XD_CP_WAITTIME			0xFD00
#define XD_CP_PAGELEN			0xFD01
#define XD_CP_READADDR0			0xFD02
#define XD_CP_READADDR1			0xFD03
#define XD_CP_READADDR2			0xFD04
#define XD_CP_READADDR3			0xFD05
#define XD_CP_READADDR4			0xFD06
#define XD_CP_WRITEADDR0		0xFD07
#define XD_CP_WRITEADDR1		0xFD08
#define XD_CP_WRITEADDR2		0xFD09
#define XD_CP_WRITEADDR3		0xFD0A
#define XD_CP_WRITEADDR4		0xFD0B
#define XD_INIT				0xFD10
#define XD_DTCTL			0xFD11
#define XD_CTL				0xFD12
#define XD_TRANSFER			0xFD13
#define XD_CFG				0xFD14
#define XD_ADDRESS0			0xFD15
#define XD_ADDRESS1			0xFD16
#define XD_ADDRESS2			0xFD17
#define XD_ADDRESS3			0xFD18
#define XD_ADDRESS4			0xFD19
#define XD_DAT				0xFD1A
#define XD_PAGE_CNT			0xFD1B
#define XD_PAGE_STATUS			0xFD1C
#define XD_BLOCK_STATUS			0xFD1D
#define XD_BLOCK_ADDR1_L		0xFD1E
#define XD_BLOCK_ADDR1_H		0xFD1F
#define XD_BLOCK_ADDR2_L		0xFD20
#define XD_BLOCK_ADDR2_H		0xFD21
#define XD_BYTE_CNT_L			0xFD22
#define XD_BYTE_CNT_H			0xFD23
#define	XD_PARITY			0xFD24
#define XD_ECC_BIT1			0xFD25
#define XD_ECC_BYTE1			0xFD26
#define XD_ECC_BIT2			0xFD27
#define XD_ECC_BYTE2			0xFD28
#define XD_RESERVED0			0xFD29
#define XD_RESERVED1			0xFD2A
#define XD_RESERVED2			0xFD2B
#define XD_RESERVED3			0xFD2C
#define XD_CHK_DATA_STATUS		0xFD2D
#define XD_CATCTL			0xFD2E

#define MS_BLKEND			0xFD30
#define MS_READ_START			0xFD31
#define MS_READ_COUNT			0xFD32
#define MS_WRITE_START			0xFD33
#define MS_WRITE_COUNT			0xFD34
#define MS_COMMAND			0xFD35
#define MS_OLD_BLOCK_0			0xFD36
#define MS_OLD_BLOCK_1			0xFD37
#define MS_NEW_BLOCK_0			0xFD38
#define MS_NEW_BLOCK_1			0xFD39
#define MS_LOG_BLOCK_0			0xFD3A
#define MS_LOG_BLOCK_1			0xFD3B
#define MS_BUS_WIDTH			0xFD3C
#define MS_PAGE_START			0xFD3D
#define MS_PAGE_LENGTH			0xFD3E
#define MS_CFG				0xFD40
#define MS_TPC				0xFD41
#define MS_TRANS_CFG			0xFD42
#define MS_TRANSFER			0xFD43
#define MS_INT_REG			0xFD44
#define MS_BYTE_CNT			0xFD45
#define MS_SECTOR_CNT_L			0xFD46
#define MS_SECTOR_CNT_H			0xFD47
#define MS_DBUS_H			0xFD48

#define CARD_DMA1_CTL			0xFD5C
#define CARD_PULL_CTL1			0xFD60
#define CARD_PULL_CTL2			0xFD61
#define CARD_PULL_CTL3			0xFD62
#define CARD_PULL_CTL4			0xFD63
#define CARD_PULL_CTL5			0xFD64
#define CARD_PULL_CTL6			0xFD65
#define CARD_EXIST				0xFD6F
#define CARD_INT_PEND			0xFD71

#define LDO_POWER_CFG			0xFD7B

#define SD_CFG1				0xFDA0
#define SD_CFG2				0xFDA1
#define SD_CFG3				0xFDA2
#define SD_STAT1			0xFDA3
#define SD_STAT2			0xFDA4
#define SD_BUS_STAT			0xFDA5
#define SD_PAD_CTL			0xFDA6
#define SD_SAMPLE_POINT_CTL		0xFDA7
#define SD_PUSH_POINT_CTL		0xFDA8
#define SD_CMD0				0xFDA9
#define SD_CMD1				0xFDAA
#define SD_CMD2				0xFDAB
#define SD_CMD3				0xFDAC
#define SD_CMD4				0xFDAD
#define SD_CMD5				0xFDAE
#define SD_BYTE_CNT_L			0xFDAF
#define SD_BYTE_CNT_H			0xFDB0
#define SD_BLOCK_CNT_L			0xFDB1
#define SD_BLOCK_CNT_H			0xFDB2
#define SD_TRANSFER			0xFDB3
#define SD_CMD_STATE			0xFDB5
#define SD_DATA_STATE			0xFDB6
#define SD_VPCLK0_CTL			0xFC2A
#define SD_VPCLK1_CTL			0xFC2B
#define SD_DCMPS0_CTL			0xFC2C
#define SD_DCMPS1_CTL			0xFC2D

#define CARD_DMA1_CTL			0xFD5C

#define HW_VERSION			0xFC01

#define SSC_CLK_FPGA_SEL		0xFC02
#define CLK_DIV				0xFC03
#define SFSM_ED				0xFC04

#define CD_DEGLITCH_WIDTH		0xFC20
#define CD_DEGLITCH_EN			0xFC21
#define AUTO_DELINK_EN			0xFC23

#define FPGA_PULL_CTL			0xFC1D
#define CARD_CLK_SOURCE			0xFC2E

#define CARD_SHARE_MODE			0xFD51
#define CARD_DRIVE_SEL			0xFD52
#define CARD_STOP			0xFD53
#define CARD_OE				0xFD54
#define CARD_AUTO_BLINK			0xFD55
#define CARD_GPIO			0xFD56
#define SD30_DRIVE_SEL		0xFD57

#define CARD_DATA_SOURCE		0xFD5D
#define CARD_SELECT			0xFD5E

#define CARD_CLK_EN			0xFD79
#define CARD_PWR_CTL			0xFD7A

#define OCPCTL				0xFD80
#define OCPPARA1			0xFD81
#define OCPPARA2			0xFD82
#define OCPSTAT				0xFD83

#define HS_USB_STAT			0xFE01
#define HS_VCONTROL			0xFE26
#define HS_VSTAIN			0xFE27
#define HS_VLOADM			0xFE28
#define HS_VSTAOUT			0xFE29

#define MC_IRQ				0xFF00
#define MC_IRQEN			0xFF01
#define MC_FIFO_CTL			0xFF02
#define MC_FIFO_BC0			0xFF03
#define MC_FIFO_BC1			0xFF04
#define MC_FIFO_STAT			0xFF05
#define MC_FIFO_MODE			0xFF06
#define MC_FIFO_RD_PTR0		0xFF07
#define MC_FIFO_RD_PTR1		0xFF08
#define MC_DMA_CTL			0xFF10
#define MC_DMA_TC0			0xFF11
#define MC_DMA_TC1			0xFF12
#define MC_DMA_TC2			0xFF13
#define MC_DMA_TC3			0xFF14
#define MC_DMA_RST			0xFF15

/* Memory mapping */
#define RBUF_SIZE_MASK		0xFBFF
#define RBUF_BASE			0xF000
#define PPBUF_BASE1			0xF800
#define PPBUF_BASE2			0xFA00

/* int monitor_card_cd */
#define CD_EXIST			0
#define CD_NOT_EXIST			1

#define DEBOUNCE_CNT			5

int monitor_card_cd(struct rts51x_chip *chip, u8 card);

void do_remaining_work(struct rts51x_chip *chip);
void do_reset_sd_card(struct rts51x_chip *chip);
void rts51x_init_cards(struct rts51x_chip *chip);
void rts51x_release_cards(struct rts51x_chip *chip);
int switch_ssc_clock(struct rts51x_chip *chip, int clk);
int switch_normal_clock(struct rts51x_chip *chip, int clk);
int card_rw(struct scsi_cmnd *srb, struct rts51x_chip *chip, u32 sec_addr,
	    u16 sec_cnt);
u8 get_lun_card(struct rts51x_chip *chip, unsigned int lun);
int rts51x_select_card(struct rts51x_chip *chip, int card);
void eject_card(struct rts51x_chip *chip, unsigned int lun);
void trans_dma_enable(enum dma_data_direction dir, struct rts51x_chip *chip,
		      u32 byte_cnt, u8 pack_size);
int enable_card_clock(struct rts51x_chip *chip, u8 card);
int card_power_on(struct rts51x_chip *chip, u8 card);
int card_power_off(struct rts51x_chip *chip, u8 card);
int toggle_gpio(struct rts51x_chip *chip, u8 gpio);
int turn_on_led(struct rts51x_chip *chip, u8 gpio);
int turn_off_led(struct rts51x_chip *chip, u8 gpio);

static inline int check_card_ready(struct rts51x_chip *chip, unsigned int lun)
{
	if (chip->card_ready & chip->lun2card[lun])
		return 1;

	return 0;
}

static inline int check_card_exist(struct rts51x_chip *chip, unsigned int lun)
{
	if (chip->card_exist & chip->lun2card[lun])
		return 1;

	return 0;
}

static inline int check_card_wp(struct rts51x_chip *chip, unsigned int lun)
{
	if (chip->card_wp & chip->lun2card[lun])
		return 1;

	return 0;
}

static inline int check_card_fail(struct rts51x_chip *chip, unsigned int lun)
{
	if (chip->card_fail & chip->lun2card[lun])
		return 1;

	return 0;
}

static inline int check_card_ejected(struct rts51x_chip *chip, unsigned int lun)
{
	if (chip->card_ejected & chip->lun2card[lun])
		return 1;

	return 0;
}

static inline int check_fake_card_ready(struct rts51x_chip *chip,
					unsigned int lun)
{
	if (chip->fake_card_ready & chip->lun2card[lun])
		return 1;

	return 0;
}

static inline u8 get_lun2card(struct rts51x_chip *chip, unsigned int lun)
{
	return chip->lun2card[lun];
}

static inline int check_lun_mc(struct rts51x_chip *chip, unsigned int lun)
{
	return CHK_BIT(chip->lun_mc, lun);
}

static inline void set_lun_mc(struct rts51x_chip *chip, unsigned int lun)
{
	SET_BIT(chip->lun_mc, lun);
}

static inline void clear_lun_mc(struct rts51x_chip *chip, unsigned int lun)
{
	CLR_BIT(chip->lun_mc, lun);
}

static inline int switch_clock(struct rts51x_chip *chip, int clk)
{
	int retval = 0;

	if (chip->asic_code)
		retval = switch_ssc_clock(chip, clk);
	else
		retval = switch_normal_clock(chip, clk);

	return retval;
}

static inline void rts51x_clear_xd_error(struct rts51x_chip *chip)
{
	rts51x_ep0_write_register(chip, CARD_STOP,
				  XD_STOP | XD_CLR_ERR, XD_STOP | XD_CLR_ERR);

	rts51x_ep0_write_register(chip, MC_FIFO_CTL, FIFO_FLUSH, FIFO_FLUSH);
	rts51x_ep0_write_register(chip, MC_DMA_RST, DMA_RESET, DMA_RESET);
	rts51x_ep0_write_register(chip, SFSM_ED, 0xf8, 0xf8);
}

static inline void rts51x_clear_sd_error(struct rts51x_chip *chip)
{
	rts51x_ep0_write_register(chip, CARD_STOP,
				  SD_STOP | SD_CLR_ERR, SD_STOP | SD_CLR_ERR);

	rts51x_ep0_write_register(chip, MC_FIFO_CTL, FIFO_FLUSH, FIFO_FLUSH);
	rts51x_ep0_write_register(chip, MC_DMA_RST, DMA_RESET, DMA_RESET);
	rts51x_ep0_write_register(chip, SFSM_ED, 0xf8, 0xf8);
}

static inline void rts51x_clear_ms_error(struct rts51x_chip *chip)
{
	rts51x_ep0_write_register(chip, CARD_STOP,
				  MS_STOP | MS_CLR_ERR, MS_STOP | MS_CLR_ERR);

	rts51x_ep0_write_register(chip, MC_FIFO_CTL, FIFO_FLUSH, FIFO_FLUSH);
	rts51x_ep0_write_register(chip, MC_DMA_RST, DMA_RESET, DMA_RESET);
	rts51x_ep0_write_register(chip, SFSM_ED, 0xf8, 0xf8);
}

#endif /* __RTS51X_CARD_H */
