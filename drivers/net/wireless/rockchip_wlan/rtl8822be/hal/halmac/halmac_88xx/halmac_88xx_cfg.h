/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _HALMAC_88XX_CFG_H_
#define _HALMAC_88XX_CFG_H_

#include "../halmac_2_platform.h"
#include "../halmac_type.h"
#include "../halmac_hw_cfg.h"
#include "../halmac_api.h"
#include "../halmac_bit2.h"
#include "../halmac_reg2.h"
#include "../halmac_pwr_seq_cmd.h"
#include "halmac_func_88xx.h"
#include "halmac_api_88xx.h"
#include "halmac_api_88xx_usb.h"
#include "halmac_api_88xx_pcie.h"
#include "halmac_api_88xx_sdio.h"
#if HALMAC_PLATFORM_TESTPROGRAM
#include "halmisc_api_88xx.h"
#include "halmisc_api_88xx_usb.h"
#include "halmisc_api_88xx_pcie.h"
#include "halmisc_api_88xx_sdio.h"
#endif

#define HALMAC_SVN_VER_88XX "11974M"

/* major version, ver_1 for async_api */
#define HALMAC_MAJOR_VER_88XX        0x0001
/* For halmac_api num change or prototype change, increment prototype version */
#define HALMAC_PROTOTYPE_VER_88XX    0x0002
/* else increment minor version */
#define HALMAC_MINOR_VER_88XX        0x0000



#define HALMAC_C2H_DATA_OFFSET_88XX             10
#define HALMAC_RX_AGG_ALIGNMENT_SIZE_88XX       8
#define HALMAC_TX_AGG_ALIGNMENT_SIZE_88XX       8
#define HALMAC_TX_AGG_BUFF_SIZE_88XX            32768

#define HALMAC_EXTRA_INFO_BUFF_SIZE_88XX				4096 /*4K*/
#define HALMAC_EXTRA_INFO_BUFF_SIZE_FULL_FIFO_88XX		16384 /*16K*/
#define HALMAC_FW_OFFLOAD_CMD_SIZE_88XX					12 /*Fw config parameter cmd size, each 12 byte*/

#define HALMAC_H2C_CMD_ORIGINAL_SIZE_88XX       8
#define HALMAC_H2C_CMD_SIZE_UNIT_88XX           32 /* Only support 32 byte packet now */

#define HALMAC_NLO_INFO_SIZE_88XX	1024

/* Download FW */
#define HALMAC_FW_SIZE_MAX_88XX                 0x40000
#define HALMAC_FWHDR_SIZE_88XX                  64
#define HALMAC_FW_CHKSUM_DUMMY_SIZE_88XX        8
#define HALMAC_FW_MAX_DL_SIZE_88XX              0x2000 /* need power of 2 */
/* Max dlfw size can not over 31K, because SDIO HW restriction */
#define HALMAC_FW_CFG_MAX_DL_SIZE_MAX_88XX      0x7C00

#define DLFW_RESTORE_REG_NUM_88XX       9

/* FW header information */
#define HALMAC_FWHDR_OFFSET_VERSION_88XX                4
#define HALMAC_FWHDR_OFFSET_SUBVERSION_88XX             6
#define HALMAC_FWHDR_OFFSET_SUBINDEX_88XX               7
#define HALMAC_FWHDR_OFFSET_MEM_USAGE_88XX              24
#define HALMAC_FWHDR_OFFSET_H2C_FORMAT_VER_88XX			28
#define HALMAC_FWHDR_OFFSET_DMEM_ADDR_88XX              32
#define HALMAC_FWHDR_OFFSET_DMEM_SIZE_88XX              36
#define HALMAC_FWHDR_OFFSET_IRAM_SIZE_88XX              48
#define HALMAC_FWHDR_OFFSET_ERAM_SIZE_88XX              52
#define HALMAC_FWHDR_OFFSET_EMEM_ADDR_88XX              56
#define HALMAC_FWHDR_OFFSET_IRAM_ADDR_88XX              60

/* HW memory address */
#define HALMAC_OCPBASE_TXBUF_88XX				0x18780000
#define HALMAC_OCPBASE_DMEM_88XX                0x00200000
#define HALMAC_OCPBASE_IMEM_88XX                0x00000000

/* define the SDIO Bus CLK threshold, for avoiding CMD53 fails that result from SDIO CLK sync to ana_clk fail */
#define HALMAC_SD_CLK_THRESHOLD_88XX    150000000 /* 150MHz */

/* MAC clock */
#define HALMAC_MAC_CLOCK_88XX   80 /* 80M */

/* H2C/C2H*/
#define HALMAC_H2C_CMD_SIZE_88XX		32
#define HALMAC_H2C_CMD_HDR_SIZE_88XX    8

#define HALMAC_RESERVED_EFUSE_SIZE_88XX 0x30

#define HALMAC_PROTECTED_EFUSE_SIZE_88XX 0x60

/* Function enable */
#define HALMAC_FUNCTION_ENABLE_88XX     0xDC

/* FIFO size & packet size */
/* #define HALMAC_WOWLAN_PATTERN_SIZE	256 */

/* CFEND rate */
#define HALMAC_BASIC_CFEND_RATE_88XX    0x5
#define HALMAC_STBC_CFEND_RATE_88XX     0xF

/* Response rate */
#define HALMAC_RESPONSE_RATE_BITMAP_ALL_88XX    0xFFFFF
#define HALMAC_RESPONSE_RATE_88XX				HALMAC_RESPONSE_RATE_BITMAP_ALL_88XX

/* Spec SIFS */
#define HALMAC_SIFS_CCK_PTCL_88XX       16
#define HALMAC_SIFS_OFDM_PTCL_88XX      16

/* Retry limit */
#define HALMAC_LONG_RETRY_LIMIT_88XX    8
#define HALMAC_SHORT_RETRY_LIMIT_88XX   7

/* Slot, SIFS, PIFS time */
#define HALMAC_SLOT_TIME_88XX           0x05
#define HALMAC_PIFS_TIME_88XX           0x19
#define HALMAC_SIFS_CCK_CTX_88XX        0xA
#define HALMAC_SIFS_OFDM_CTX_88XX       0xA
#define HALMAC_SIFS_CCK_TRX_88XX        0x10
#define HALMAC_SIFS_OFDM_TRX_88XX       0x10

/* TXOP limit */
#define HALMAC_VO_TXOP_LIMIT_88XX       0x186
#define HALMAC_VI_TXOP_LIMIT_88XX       0x3BC

/* NAV */
#define HALMAC_RDG_NAV_88XX             0x05
#define HALMAC_TXOP_NAV_88XX            0x1B

/* TSF */
#define HALMAC_CCK_RX_TSF_88XX			0x30
#define HALMAC_OFDM_RX_TSF_88XX			0x30

/* Send beacon related */
#define HALMAC_TBTT_PROHIBIT_88XX       0x04
#define HALMAC_TBTT_HOLD_TIME_88XX      0x064
#define HALMAC_DRIVER_EARLY_INT_88XX    0x04
#define HALMAC_BEACON_DMA_TIM_88XX      0x02

/* RX filter */
#define HALMAC_RX_FILTER0_RECIVE_ALL_88XX       0xFFFFFFF
#define HALMAC_RX_FILTER0_88XX                  HALMAC_RX_FILTER0_RECIVE_ALL_88XX
#define HALMAC_RX_FILTER_RECIVE_ALL_88XX        0xFFFF
#define HALMAC_RX_FILTER_88XX                   HALMAC_RX_FILTER_RECIVE_ALL_88XX

/* RCR */
#define HALMAC_RCR_CONFIG_88XX  0xE400631E

/* Security config */
#define HALMAC_SECURITY_CONFIG_88XX     0x01CC

#endif
