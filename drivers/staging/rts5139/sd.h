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

#ifndef __RTS51X_SD_H
#define __RTS51X_SD_H

#include "rts51x_chip.h"

#define SD_MAX_RETRY_COUNT	3

#define SUPPORT_VOLTAGE	0x003C0000

#define SD_RESET_FAIL	0x01
#define MMC_RESET_FAIL  0x02

/* Error Code */
#define	SD_NO_ERROR		0x0
#define	SD_CRC_ERR		0x80
#define	SD_TO_ERR		0x40
#define	SD_NO_CARD		0x20
#define SD_BUSY			0x10
#define	SD_STS_ERR		0x08
#define SD_RSP_TIMEOUT		0x04

/* MMC/SD Command Index */
/* Basic command (class 0) */
#define GO_IDLE_STATE		0
#define	SEND_OP_COND		1 /* reserved for SD */
#define	ALL_SEND_CID		2
#define	SET_RELATIVE_ADDR	3
#define	SEND_RELATIVE_ADDR	3
#define	SET_DSR			4
#define IO_SEND_OP_COND		5
#define	SWITCH			6
#define	SELECT_CARD		7
#define	DESELECT_CARD		7
/* CMD8 is "SEND_EXT_CSD" for MMC4.x Spec
 * while is "SEND_IF_COND" for SD 2.0 */
#define	SEND_EXT_CSD		8
#define	SEND_IF_COND		8
/* end  */
#define	SEND_CSD		9
#define	SEND_CID		10
#define	VOLTAGE_SWITCH		11
#define	READ_DAT_UTIL_STOP	11 /* reserved for SD */
#define	STOP_TRANSMISSION	12
#define	SEND_STATUS		13
#define	GO_INACTIVE_STATE	15

/* Block oriented read commands (class 2) */
#define	SET_BLOCKLEN		16
#define	READ_SINGLE_BLOCK	17
#define	READ_MULTIPLE_BLOCK	18
#define	SEND_TUNING_PATTERN	19

/* Bus Width Test */
#define	BUSTEST_R		14
#define	BUSTEST_W		19
/* end */

/* Block oriented write commands (class 4) */
#define	WRITE_BLOCK		24
#define	WRITE_MULTIPLE_BLOCK	25
#define	PROGRAM_CSD		27

/* Erase commands */
#define	ERASE_WR_BLK_START	32
#define	ERASE_WR_BLK_END	33
#define	ERASE_CMD		38

/* Block Oriented Write Protection Commands */
#define LOCK_UNLOCK		42

#define	IO_RW_DIRECT		52

/* Application specific commands (class 8) */
#define	APP_CMD			55
#define	GEN_CMD			56

/* SD Application command Index */
#define	SET_BUS_WIDTH			6
#define	SD_STATUS			13
#define	SEND_NUM_WR_BLOCKS		22
#define	SET_WR_BLK_ERASE_COUNT		23
#define	SD_APP_OP_COND			41
#define	SET_CLR_CARD_DETECT		42
#define	SEND_SCR			51

/* SD TIMEOUT function return error */
#define	SD_READ_COMPLETE	0x00
#define	SD_READ_TO		0x01
#define	SD_READ_ADVENCE		0x02

/* SD v1.1 CMD6 SWITCH function */
#define	SD_CHECK_MODE		0x00
#define	SD_SWITCH_MODE		0x80
#define	SD_FUNC_GROUP_1		0x01
#define	SD_FUNC_GROUP_2		0x02
#define	SD_FUNC_GROUP_3		0x03
#define	SD_FUNC_GROUP_4		0x04
#define	SD_CHECK_SPEC_V1_1	0xFF

/* SD Command Argument */
#define	NO_ARGUMENT	                        0x00
#define	CHECK_PATTERN				0x000000AA
#define	VOLTAGE_SUPPLY_RANGE			0x00000100 /* 2.7~3.6V */
#define	SUPPORT_HIGH_AND_EXTENDED_CAPACITY	0x40000000
#define	SUPPORT_MAX_POWER_PERMANCE	        0x10000000
#define	SUPPORT_1V8	                        0x01000000

/* Switch Command Error Code */
#define	SWTICH_NO_ERR	  0x00
#define	CARD_NOT_EXIST	  0x01
#define	SPEC_NOT_SUPPORT  0x02
#define	CHECK_MODE_ERR	  0x03
#define	CHECK_NOT_READY	  0x04
#define	SWITCH_CRC_ERR	  0x05
#define	SWITCH_MODE_ERR	  0x06
#define	SWITCH_PASS	  0x07

/* Function Group Definition */
/* Function Group 1 */
#define	HS_SUPPORT			0x01
#define	SDR50_SUPPORT			0x02
#define	SDR104_SUPPORT			0x03
#define	DDR50_SUPPORT			0x04
#define	HS_SUPPORT_MASK			0x02
#define	SDR50_SUPPORT_MASK		0x04
#define	SDR104_SUPPORT_MASK		0x08
#define	DDR50_SUPPORT_MASK		0x10
#define	HS_QUERY_SWITCH_OK		0x01
#define	SDR50_QUERY_SWITCH_OK		0x02
#define	SDR104_QUERY_SWITCH_OK		0x03
#define	DDR50_QUERY_SWITCH_OK		0x04
#define	HS_SWITCH_BUSY			0x02
#define	SDR50_SWITCH_BUSY		0x04
#define	SDR104_SWITCH_BUSY		0x08
#define	DDR50_SWITCH_BUSY		0x10
#define	FUNCTION_GROUP1_SUPPORT_OFFSET       0x0D
#define FUNCTION_GROUP1_QUERY_SWITCH_OFFSET  0x10
#define FUNCTION_GROUP1_CHECK_BUSY_OFFSET    0x1D
/* Function Group 3 */
#define	DRIVING_TYPE_A	        0x01
#define	DRIVING_TYPE_B		    0x00
#define	DRIVING_TYPE_C		    0x02
#define	DRIVING_TYPE_D	        0x03
#define	DRIVING_TYPE_A_MASK	    0x02
#define	DRIVING_TYPE_B_MASK	    0x01
#define	DRIVING_TYPE_C_MASK	    0x04
#define	DRIVING_TYPE_D_MASK	    0x08
#define	TYPE_A_QUERY_SWITCH_OK	0x01
#define	TYPE_B_QUERY_SWITCH_OK	0x00
#define	TYPE_C_QUERY_SWITCH_OK  0x02
#define	TYPE_D_QUERY_SWITCH_OK  0x03
#define	TYPE_A_SWITCH_BUSY	    0x02
#define	TYPE_B_SWITCH_BUSY	    0x01
#define	TYPE_C_SWITCH_BUSY      0x04
#define	TYPE_D_SWITCH_BUSY      0x08
#define	FUNCTION_GROUP3_SUPPORT_OFFSET       0x09
#define FUNCTION_GROUP3_QUERY_SWITCH_OFFSET  0x0F
#define FUNCTION_GROUP3_CHECK_BUSY_OFFSET    0x19
/* Function Group 4 */
#define	CURRENT_LIMIT_200	    0x00
#define	CURRENT_LIMIT_400	    0x01
#define	CURRENT_LIMIT_600	    0x02
#define	CURRENT_LIMIT_800	    0x03
#define	CURRENT_LIMIT_200_MASK	0x01
#define	CURRENT_LIMIT_400_MASK	0x02
#define	CURRENT_LIMIT_600_MASK	0x04
#define	CURRENT_LIMIT_800_MASK	0x08
#define	CURRENT_LIMIT_200_QUERY_SWITCH_OK    0x00
#define	CURRENT_LIMIT_400_QUERY_SWITCH_OK    0x01
#define	CURRENT_LIMIT_600_QUERY_SWITCH_OK    0x02
#define	CURRENT_LIMIT_800_QUERY_SWITCH_OK    0x03
#define	CURRENT_LIMIT_200_SWITCH_BUSY        0x01
#define	CURRENT_LIMIT_400_SWITCH_BUSY	     0x02
#define	CURRENT_LIMIT_600_SWITCH_BUSY        0x04
#define	CURRENT_LIMIT_800_SWITCH_BUSY        0x08
#define	FUNCTION_GROUP4_SUPPORT_OFFSET       0x07
#define FUNCTION_GROUP4_QUERY_SWITCH_OFFSET  0x0F
#define FUNCTION_GROUP4_CHECK_BUSY_OFFSET    0x17
/* Switch Function Status Offset */
#define	DATA_STRUCTURE_VER_OFFSET   0x11 /* The high offset */
#define MAX_PHASE		15
/* #define      TOTAL_READ_PHASE    0x20 */
/* #define      TOTAL_WRITE_PHASE    0x20 */
/* MMC v4.0 */
/* #define MMC_52MHZ_SPEED                       0x0001 */
/* #define MMC_26MHZ_SPEED                       0x0002 */
#define MMC_8BIT_BUS			0x0010
#define MMC_4BIT_BUS			0x0020
/* #define MMC_SECTOR_MODE                       0x0100 */
#define MMC_SWITCH_ERR			0x80
/* Tuning direction RX or TX */
#define TUNE_TX    0x00
#define TUNE_RX	   0x01
/* For Change_DCM_FreqMode Function */
#define CHANGE_TX  0x00
#define CHANGE_RX  0x01
#define DCM_HIGH_FREQUENCY_MODE  0x00
#define DCM_LOW_FREQUENCY_MODE   0x01
#define DCM_HIGH_FREQUENCY_MODE_SET  0x0C
#define DCM_Low_FREQUENCY_MODE_SET   0x00
/* For Change_FPGA_SSCClock Function */
#define MULTIPLY_BY_1    0x00
#define MULTIPLY_BY_2    0x01
#define MULTIPLY_BY_3    0x02
#define MULTIPLY_BY_4    0x03
#define MULTIPLY_BY_5    0x04
#define MULTIPLY_BY_6    0x05
#define MULTIPLY_BY_7    0x06
#define MULTIPLY_BY_8    0x07
#define MULTIPLY_BY_9    0x08
#define MULTIPLY_BY_10   0x09
#define DIVIDE_BY_2      0x01
#define DIVIDE_BY_3      0x02
#define DIVIDE_BY_4      0x03
#define DIVIDE_BY_5      0x04
#define DIVIDE_BY_6      0x05
#define DIVIDE_BY_7      0x06
#define DIVIDE_BY_8      0x07
#define DIVIDE_BY_9      0x08
#define DIVIDE_BY_10     0x09
#define CHECK_SD_TRANS_FAIL(chip, retval)	\
	(((retval) != STATUS_SUCCESS) || \
			(chip->rsp_buf[0] & SD_TRANSFER_ERR))
/* SD Tuning Data Structure */
/* Record continuous timing phase path */
struct timing_phase_path {
	int start;
	int end;
	int mid;
	int len;
};

int sd_select_card(struct rts51x_chip *chip, int select);
int reset_sd_card(struct rts51x_chip *chip);
int sd_switch_clock(struct rts51x_chip *chip);
int sd_rw(struct scsi_cmnd *srb, struct rts51x_chip *chip, u32 start_sector,
	  u16 sector_cnt);
void sd_cleanup_work(struct rts51x_chip *chip);
int release_sd_card(struct rts51x_chip *chip);

#ifdef SUPPORT_CPRM
extern int reset_sd(struct rts51x_chip *chip);
extern int sd_check_data0_status(struct rts51x_chip *chip);
extern int sd_read_data(struct rts51x_chip *chip, u8 trans_mode, u8 *cmd,
		int cmd_len, u16 byte_cnt, u16 blk_cnt, u8 bus_width,
		u8 *buf, int buf_len, int timeout);
#endif

#endif /* __RTS51X_SD_H */
