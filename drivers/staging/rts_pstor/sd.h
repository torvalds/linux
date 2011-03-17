/* Driver for Realtek PCI-Express card reader
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
 */

#ifndef __REALTEK_RTSX_SD_H
#define __REALTEK_RTSX_SD_H

#include "rtsx_chip.h"

#define SUPPORT_VOLTAGE	0x003C0000

/* Error Code */
#define	SD_NO_ERROR		0x0
#define	SD_CRC_ERR		0x80
#define	SD_TO_ERR		0x40
#define	SD_NO_CARD		0x20
#define SD_BUSY			0x10
#define	SD_STS_ERR		0x08
#define SD_RSP_TIMEOUT		0x04
#define SD_IO_ERR		0x02

/* MMC/SD Command Index */
/* Basic command (class 0) */
#define GO_IDLE_STATE		0
#define	SEND_OP_COND		1
#define	ALL_SEND_CID		2
#define	SET_RELATIVE_ADDR	3
#define	SEND_RELATIVE_ADDR	3
#define	SET_DSR			4
#define IO_SEND_OP_COND		5
#define	SWITCH			6
#define	SELECT_CARD		7
#define	DESELECT_CARD		7
/* CMD8 is "SEND_EXT_CSD" for MMC4.x Spec
 * while is "SEND_IF_COND" for SD 2.0
 */
#define	SEND_EXT_CSD		8
#define	SEND_IF_COND		8

#define	SEND_CSD		9
#define	SEND_CID		10
#define	VOLTAGE_SWITCH	    	11
#define	READ_DAT_UTIL_STOP	11
#define	STOP_TRANSMISSION	12
#define	SEND_STATUS		13
#define	GO_INACTIVE_STATE	15

#define	SET_BLOCKLEN		16
#define	READ_SINGLE_BLOCK	17
#define	READ_MULTIPLE_BLOCK	18
#define	SEND_TUNING_PATTERN	19

#define	BUSTEST_R		14
#define	BUSTEST_W		19

#define	WRITE_BLOCK		24
#define	WRITE_MULTIPLE_BLOCK	25
#define	PROGRAM_CSD		27

#define	ERASE_WR_BLK_START	32
#define	ERASE_WR_BLK_END	33
#define	ERASE_CMD		38

#define LOCK_UNLOCK 		42
#define	IO_RW_DIRECT		52

#define	APP_CMD			55
#define	GEN_CMD			56

#define	SET_BUS_WIDTH		6
#define	SD_STATUS		13
#define	SEND_NUM_WR_BLOCKS	22
#define	SET_WR_BLK_ERASE_COUNT	23
#define	SD_APP_OP_COND		41
#define	SET_CLR_CARD_DETECT	42
#define	SEND_SCR		51

#define	SD_READ_COMPLETE	0x00
#define	SD_READ_TO		0x01
#define	SD_READ_ADVENCE		0x02

#define	SD_CHECK_MODE		0x00
#define	SD_SWITCH_MODE		0x80
#define	SD_FUNC_GROUP_1	    	0x01
#define	SD_FUNC_GROUP_2	    	0x02
#define	SD_FUNC_GROUP_3	    	0x03
#define	SD_FUNC_GROUP_4	    	0x04
#define	SD_CHECK_SPEC_V1_1	0xFF

#define	NO_ARGUMENT	                        0x00
#define	CHECK_PATTERN	                    	0x000000AA
#define	VOLTAGE_SUPPLY_RANGE	            	0x00000100
#define	SUPPORT_HIGH_AND_EXTENDED_CAPACITY	0x40000000
#define	SUPPORT_MAX_POWER_PERMANCE	        0x10000000
#define	SUPPORT_1V8	                        0x01000000

#define	SWTICH_NO_ERR	  	0x00
#define	CARD_NOT_EXIST	  	0x01
#define	SPEC_NOT_SUPPORT  	0x02
#define	CHECK_MODE_ERR	  	0x03
#define	CHECK_NOT_READY	  	0x04
#define	SWITCH_CRC_ERR	  	0x05
#define	SWITCH_MODE_ERR	  	0x06
#define	SWITCH_PASS		0x07

#ifdef SUPPORT_SD_LOCK
#define SD_ERASE		0x08
#define SD_LOCK			0x04
#define SD_UNLOCK		0x00
#define SD_CLR_PWD		0x02
#define SD_SET_PWD		0x01

#define SD_PWD_LEN		0x10

#define SD_LOCKED		0x80
#define SD_LOCK_1BIT_MODE	0x40
#define SD_PWD_EXIST		0x20
#define SD_UNLOCK_POW_ON	0x01
#define SD_SDR_RST		0x02

#define SD_NOT_ERASE		0x00
#define SD_UNDER_ERASING	0x01
#define SD_COMPLETE_ERASE	0x02

#define SD_RW_FORBIDDEN		0x0F

#endif

#define	HS_SUPPORT			0x01
#define	SDR50_SUPPORT			0x02
#define	SDR104_SUPPORT	        	0x03
#define	DDR50_SUPPORT		    	0x04

#define	HS_SUPPORT_MASK	        	0x02
#define	SDR50_SUPPORT_MASK	    	0x04
#define	SDR104_SUPPORT_MASK	    	0x08
#define	DDR50_SUPPORT_MASK	    	0x10

#define	HS_QUERY_SWITCH_OK	    	0x01
#define	SDR50_QUERY_SWITCH_OK		0x02
#define	SDR104_QUERY_SWITCH_OK  	0x03
#define	DDR50_QUERY_SWITCH_OK   	0x04

#define	HS_SWITCH_BUSY	        	0x02
#define	SDR50_SWITCH_BUSY	    	0x04
#define	SDR104_SWITCH_BUSY      	0x08
#define	DDR50_SWITCH_BUSY       	0x10

#define	FUNCTION_GROUP1_SUPPORT_OFFSET       0x0D
#define FUNCTION_GROUP1_QUERY_SWITCH_OFFSET  0x10
#define FUNCTION_GROUP1_CHECK_BUSY_OFFSET    0x1D

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

#define	DATA_STRUCTURE_VER_OFFSET	0x11

#define MAX_PHASE			31

#define MMC_8BIT_BUS			0x0010
#define MMC_4BIT_BUS			0x0020

#define MMC_SWITCH_ERR			0x80

#define SD_IO_3V3		0
#define SD_IO_1V8		1

#define TUNE_TX    0x00
#define TUNE_RX	   0x01

#define CHANGE_TX  0x00
#define CHANGE_RX  0x01

#define DCM_HIGH_FREQUENCY_MODE  0x00
#define DCM_LOW_FREQUENCY_MODE   0x01

#define DCM_HIGH_FREQUENCY_MODE_SET  0x0C
#define DCM_Low_FREQUENCY_MODE_SET   0x00

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

struct timing_phase_path {
	int start;
	int end;
	int mid;
	int len;
};

int sd_select_card(struct rtsx_chip *chip, int select);
int sd_pull_ctl_enable(struct rtsx_chip *chip);
int reset_sd_card(struct rtsx_chip *chip);
int sd_switch_clock(struct rtsx_chip *chip);
void sd_stop_seq_mode(struct rtsx_chip *chip);
int sd_rw(struct scsi_cmnd *srb, struct rtsx_chip *chip, u32 start_sector, u16 sector_cnt);
void sd_cleanup_work(struct rtsx_chip *chip);
int sd_power_off_card3v3(struct rtsx_chip *chip);
int release_sd_card(struct rtsx_chip *chip);
#ifdef SUPPORT_CPRM
int soft_reset_sd_card(struct rtsx_chip *chip);
int ext_sd_send_cmd_get_rsp(struct rtsx_chip *chip, u8 cmd_idx,
		u32 arg, u8 rsp_type, u8 *rsp, int rsp_len, int special_check);
int ext_sd_get_rsp(struct rtsx_chip *chip, int len, u8 *rsp, u8 rsp_type);

int sd_pass_thru_mode(struct scsi_cmnd *srb, struct rtsx_chip *chip);
int sd_execute_no_data(struct scsi_cmnd *srb, struct rtsx_chip *chip);
int sd_execute_read_data(struct scsi_cmnd *srb, struct rtsx_chip *chip);
int sd_execute_write_data(struct scsi_cmnd *srb, struct rtsx_chip *chip);
int sd_get_cmd_rsp(struct scsi_cmnd *srb, struct rtsx_chip *chip);
int sd_hw_rst(struct scsi_cmnd *srb, struct rtsx_chip *chip);
#endif

#endif  /* __REALTEK_RTSX_SD_H */
