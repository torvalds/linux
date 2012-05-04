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

#ifndef __RTS51X_CHIP_H
#define __RTS51X_CHIP_H

#include <linux/usb.h>
#include <linux/usb_usual.h>
#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <scsi/scsi_host.h>

#include "trace.h"

#define SUPPORT_CPRM
#define SUPPORT_MAGIC_GATE
#define SUPPORT_MSXC

#ifdef SUPPORT_MAGIC_GA
/* Using NORMAL_WRITE instead of AUTO_WRITE to set ICVTE */
#define MG_SET_ICV_SLOW
#endif

#ifdef SUPPORT_MSXC
#define XC_POWERCLASS
#define SUPPORT_PCGL_1P18
#endif

#define GET_CARD_STATUS_USING_EPC

#define CLOSE_SSC_POWER

#define SUPPORT_OCP

#define MS_SPEEDUP
/* #define XD_SPEEDUP */

#define SD_XD_IO_FOLLOW_PWR

#define SD_NR		2
#define MS_NR		3
#define XD_NR		4
#define SD_CARD		(1 << SD_NR)
#define MS_CARD		(1 << MS_NR)
#define XD_CARD		(1 << XD_NR)

#define SD_CD		0x01
#define MS_CD		0x02
#define XD_CD		0x04
#define SD_WP		0x08

#define MAX_ALLOWED_LUN_CNT	8
#define CMD_BUF_LEN		1024
#define RSP_BUF_LEN		1024
#define POLLING_INTERVAL	50	/* 50ms */

#define XD_FREE_TABLE_CNT	1200
#define MS_FREE_TABLE_CNT	512

/* Bit Operation */
#define SET_BIT(data, idx)	((data) |= 1 << (idx))
#define CLR_BIT(data, idx)	((data) &= ~(1 << (idx)))
#define CHK_BIT(data, idx)	((data) & (1 << (idx)))

/* Command type */
#define READ_REG_CMD		0
#define WRITE_REG_CMD		1
#define CHECK_REG_CMD		2

#define PACKET_TYPE		4
#define CNT_H			5
#define CNT_L			6
#define STAGE_FLAG		7
#define CMD_OFFSET		8

/* Packet type */
#define BATCH_CMD		0
#define SEQ_READ		1
#define SEQ_WRITE		2

/* Stage flag */
#define STAGE_R			0x01
#define STAGE_DI		0x02
#define STAGE_DO		0x04
/* Return MS_TRANS_CFG, GET_INT */
#define STAGE_MS_STATUS		0x08
/* Return XD_CFG, XD_CTL, XD_PAGE_STATUS */
#define STAGE_XD_STATUS		0x10
/* Command stage mode */
#define MODE_C			0x00
#define MODE_CR			(STAGE_R)
#define MODE_CDIR		(STAGE_R | STAGE_DI)
#define MODE_CDOR		(STAGE_R | STAGE_DO)

/* Function return code */
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS		0
#endif

#define STATUS_FAIL		1
#define STATUS_READ_FAIL	2
#define STATUS_WRITE_FAIL	3
#define STATUS_TIMEDOUT		4
#define STATUS_NOMEM		5
#define STATUS_TRANS_SHORT	6
#define STATUS_TRANS_LONG	7
#define STATUS_STALLED		8
#define STATUS_ERROR		10

#define IDLE_MAX_COUNT		10
#define POLLING_WAIT_CNT	1
#define DELINK_DELAY		100
#define LED_TOGGLE_INTERVAL	6
#define LED_GPIO		0

/* package */
#define QFN24			0
#define LQFP48			1

#define USB_11			0
#define USB_20			1

/*
 * Transport return codes
 */
/* Transport good, command good */
#define TRANSPORT_GOOD		0
/* Transport good, command failed */
#define TRANSPORT_FAILED	1
/* Command failed, no auto-sense */
#define TRANSPORT_NO_SENSE	2
/* Transport bad (i.e. device dead) */
#define TRANSPORT_ERROR		3

/* Supported Clock */
enum card_clock { CLK_20 = 1, CLK_30, CLK_40, CLK_50, CLK_60, CLK_80, CLK_100 };

#ifdef _MSG_TRACE

#define TRACE_ITEM_CNT		64

struct trace_msg_t {
	u16 line;
#define MSG_FUNC_LEN 64
	char func[MSG_FUNC_LEN];
#define MSG_FILE_LEN 32
	char file[MSG_FILE_LEN];
#define TIME_VAL_LEN 16
	u8 timeval_buf[TIME_VAL_LEN];
	u8 valid;
};

#endif /* _MSG_TRACE */

/* Size of the autosense data buffer */
#define SENSE_SIZE		18

/* Sense type */
#define	SENSE_TYPE_NO_SENSE				0
#define	SENSE_TYPE_MEDIA_CHANGE				1
#define	SENSE_TYPE_MEDIA_NOT_PRESENT			2
#define	SENSE_TYPE_MEDIA_LBA_OVER_RANGE			3
#define	SENSE_TYPE_MEDIA_LUN_NOT_SUPPORT		4
#define	SENSE_TYPE_MEDIA_WRITE_PROTECT			5
#define	SENSE_TYPE_MEDIA_INVALID_CMD_FIELD		6
#define	SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR		7
#define	SENSE_TYPE_MEDIA_WRITE_ERR			8
#define SENSE_TYPE_FORMAT_IN_PROGRESS			9
#define SENSE_TYPE_FORMAT_CMD_FAILED			10
#ifdef SUPPORT_MAGIC_GATE
/* COPY PROTECTION KEY EXCHANGE FAILURE - KEY NOT ESTABLISHED */
#define SENSE_TYPE_MG_KEY_FAIL_NOT_ESTAB		0x0b
/* COPY PROTECTION KEY EXCHANGE FAILURE - AUTHENTICATION FAILURE */
#define SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN		0x0c
/* INCOMPATIBLE MEDIUM INSTALLED */
#define SENSE_TYPE_MG_INCOMPATIBLE_MEDIUM		0x0d
/* WRITE ERROR */
#define SENSE_TYPE_MG_WRITE_ERR				0x0e
#endif

/*---- sense key ----*/
#define ILI                     0x20	/* ILI bit is on                    */

#define NO_SENSE                0x00	/* not exist sense key              */
#define RECOVER_ERR             0x01	/* Target/Logical unit is recoverd  */
#define NOT_READY               0x02	/* Logical unit is not ready        */
#define MEDIA_ERR               0x03	/* medium/data error                */
#define HARDWARE_ERR            0x04	/* hardware error                   */
#define ILGAL_REQ               0x05	/* CDB/parameter/identify msg error */
#define UNIT_ATTENTION          0x06	/* unit attention condition occur   */
#define DAT_PRTCT               0x07	/* read/write is desable            */
#define BLNC_CHK                0x08	/* find blank/DOF in read           */
					/* write to unblank area            */
#define CPY_ABRT                0x0a	/* Copy/Compare/Copy&Verify illgal  */
#define ABRT_CMD                0x0b	/* Target make the command in error */
#define EQUAL                   0x0c	/* Search Data end with Equal       */
#define VLM_OVRFLW              0x0d	/* Some data are left in buffer     */
#define MISCMP                  0x0e	/* find inequality                  */

/*-----------------------------------
    SENSE_DATA
-----------------------------------*/
/*---- valid ----*/
#define SENSE_VALID             0x80	/* Sense data is valid as SCSI2     */
#define SENSE_INVALID           0x00	/* Sense data is invalid as SCSI2   */

/*---- error code ----*/
#define CUR_ERR                 0x70	/* current error                    */
#define DEF_ERR                 0x71	/* specific command error           */

/*---- sense key Infomation ----*/
#define SNSKEYINFO_LEN          3	/* length of sense key infomation   */

#define SKSV                    0x80
#define CDB_ILLEGAL             0x40
#define DAT_ILLEGAL             0x00
#define BPV                     0x08
#define BIT_ILLEGAL0            0	/* bit0 is illegal                  */
#define BIT_ILLEGAL1            1	/* bit1 is illegal                  */
#define BIT_ILLEGAL2            2	/* bit2 is illegal                  */
#define BIT_ILLEGAL3            3	/* bit3 is illegal                  */
#define BIT_ILLEGAL4            4	/* bit4 is illegal                  */
#define BIT_ILLEGAL5            5	/* bit5 is illegal                  */
#define BIT_ILLEGAL6            6	/* bit6 is illegal                  */
#define BIT_ILLEGAL7            7	/* bit7 is illegal                  */

/*---- ASC ----*/
#define ASC_NO_INFO             0x00
#define ASC_MISCMP              0x1d
#define ASC_INVLD_CDB           0x24
#define ASC_INVLD_PARA          0x26
#define ASC_LU_NOT_READY	0x04
#define ASC_WRITE_ERR           0x0c
#define ASC_READ_ERR            0x11
#define ASC_LOAD_EJCT_ERR       0x53
#define	ASC_MEDIA_NOT_PRESENT	0x3A
#define	ASC_MEDIA_CHANGED	0x28
#define	ASC_MEDIA_IN_PROCESS	0x04
#define	ASC_WRITE_PROTECT	0x27
#define ASC_LUN_NOT_SUPPORTED	0x25

/*---- ASQC ----*/
#define ASCQ_NO_INFO            0x00
#define	ASCQ_MEDIA_IN_PROCESS	0x01
#define ASCQ_MISCMP             0x00
#define ASCQ_INVLD_CDB          0x00
#define ASCQ_INVLD_PARA         0x02
#define ASCQ_LU_NOT_READY	0x02
#define ASCQ_WRITE_ERR          0x02
#define ASCQ_READ_ERR           0x00
#define ASCQ_LOAD_EJCT_ERR      0x00
#define	ASCQ_WRITE_PROTECT	0x00

struct sense_data_t {
	unsigned char err_code;	/* error code */
	/* bit7 : valid                    */
	/*   (1 : SCSI2)                    */
	/*   (0 : Vendor specific)          */
	/* bit6-0 : error code             */
	/*  (0x70 : current error)          */
	/*  (0x71 : specific command error) */
	unsigned char seg_no;	/* segment No.                      */
	unsigned char sense_key;	/* byte5 : ILI                      */
	/* bit3-0 : sense key              */
	unsigned char info[4];	/* infomation                       */
	unsigned char ad_sense_len;	/* additional sense data length     */
	unsigned char cmd_info[4];	/* command specific infomation      */
	unsigned char asc;	/* ASC                              */
	unsigned char ascq;	/* ASCQ                             */
	unsigned char rfu;	/* FRU                              */
	unsigned char sns_key_info[3];	/* sense key specific infomation    */
};

/* sd_ctl bit map */
/* SD push point control, bit 0, 1 */
#define SD_PUSH_POINT_CTL_MASK		0x03
#define SD_PUSH_POINT_DELAY		0x01
#define SD_PUSH_POINT_AUTO		0x02
/* SD sample point control, bit 2, 3 */
#define SD_SAMPLE_POINT_CTL_MASK	0x0C
#define SD_SAMPLE_POINT_DELAY		0x04
#define SD_SAMPLE_POINT_AUTO		0x08
/* SD DDR Tx phase set by user, bit 4 */
#define SD_DDR_TX_PHASE_SET_BY_USER	0x10
/* MMC DDR Tx phase set by user, bit 5 */
#define MMC_DDR_TX_PHASE_SET_BY_USER	0x20
/* Support MMC DDR mode, bit 6 */
/*#define SUPPORT_MMC_DDR_MODE          0x40 */
#define SUPPORT_UHS50_MMC44		0x40

struct rts51x_option {
	u8 led_blink_speed;

	int mspro_formatter_enable;

	/* card clock expected by user for fpga platform */
	int fpga_sd_sdr104_clk;
	int fpga_sd_ddr50_clk;
	int fpga_sd_sdr50_clk;
	int fpga_sd_hs_clk;
	int fpga_mmc_52m_clk;
	int fpga_ms_hg_clk;
	int fpga_ms_4bit_clk;

	/* card clock expected by user for asic platform */
	int asic_sd_sdr104_clk;
	int asic_sd_ddr50_clk;
	int asic_sd_sdr50_clk;
	int asic_sd_hs_clk;
	int asic_mmc_52m_clk;
	int asic_ms_hg_clk;
	int asic_ms_4bit_clk;

	u8 ssc_depth_sd_sdr104;	/* sw */
	u8 ssc_depth_sd_ddr50;	/* sw */
	u8 ssc_depth_sd_sdr50;	/* sw */
	u8 ssc_depth_sd_hs;	/* sw */
	u8 ssc_depth_mmc_52m;	/* sw */
	u8 ssc_depth_ms_hg;	/* sw */
	u8 ssc_depth_ms_4bit;	/* sw */
	u8 ssc_depth_low_speed;	/* sw */

	/* SD/MMC Tx phase */
	int sd_ddr_tx_phase;	/* Enabled by bit 4 of sd_ctl */
	int mmc_ddr_tx_phase;	/* Enabled by bit 5 of sd_ctl */

	/* priority of choosing sd speed funciton */
	u32 sd_speed_prior;

	/* sd card control */
	u32 sd_ctl;

	/* Enable Selective Suspend */
	int ss_en;
	/* Interval to enter SS from IDLE state (second) */
	int ss_delay;
	int needs_remote_wakeup;
	u8 ww_enable;	/* sangdy2010-08-03:add for remote wakeup */

	/* Enable SSC clock */
	int ssc_en;

	int auto_delink_en;

	/* sangdy2010-07-13:add FT2 fast mode */
	int FT2_fast_mode;
	/* sangdy2010-07-15:
	 * add for config delay between 1/4 PMOS and 3/4 PMOS */
	int pwr_delay;

	int xd_rw_step;		/* add to tune xd tRP */
	int D3318_off_delay;	/* add to tune D3318 off delay time */
	int delink_delay;	/* add to tune delink delay time */
	/* add for rts5129 to enable/disable D3318 off */
	u8 rts5129_D3318_off_enable;
	u8 sd20_pad_drive;	/* add to config SD20 PAD drive */
	u8 sd30_pad_drive;	/* add to config SD30 pad drive */
	/*if reset or rw fail,then set SD20 pad drive again */
	u8 reset_or_rw_fail_set_pad_drive;

	u8 rcc_fail_flag;	/* add to indicate whether rcc bug happen */
	u8 rcc_bug_fix_en;	/* if set,then support fixing rcc bug */
	u8 debounce_num;	/* debounce number */
	int polling_time;	/* polling delay time */
	u8 led_toggle_interval;	/* used to control led toggle speed */
	int xd_rwn_step;
	u8 sd_send_status_en;
	/* used to store default phase which is
	 * used when phase tune all pass. */
	u8 ddr50_tx_phase;
	u8 ddr50_rx_phase;
	u8 sdr50_tx_phase;
	u8 sdr50_rx_phase;
	/* used to enable select sdr50 tx phase according to  proportion. */
	u8 sdr50_phase_sel;
	u8 ms_errreg_fix;
	u8 reset_mmc_first;
	u8 speed_mmc;		/* when set, then try CMD55 only twice */
	u8 led_always_on;	/* if set, then led always on when card exist */
	u8 dv18_voltage;	/* add to tune dv18 voltage */
};

#define MS_FORMATTER_ENABLED(chip)	((chip)->option.mspro_formatter_enable)

struct rts51x_chip;

typedef int (*card_rw_func) (struct scsi_cmnd *srb, struct rts51x_chip *chip,
			     u32 sec_addr, u16 sec_cnt);

/* For MS Card */
#define    MAX_DEFECTIVE_BLOCK     10

struct zone_entry {
	u16 *l2p_table;
	u16 *free_table;
	u16 defect_list[MAX_DEFECTIVE_BLOCK];	/* For MS card only */
	int set_index;
	int get_index;
	int unused_blk_cnt;
	int disable_count;
	/* To indicate whether the L2P table of this zone has been built. */
	int build_flag;
};

struct xd_delay_write_tag {
	u32 old_phyblock;
	u32 new_phyblock;
	u32 logblock;
	u8 pageoff;
	u8 delay_write_flag;
};

struct xd_info {
	u8 maker_code;
	u8 device_code;
	u8 block_shift;
	u8 page_off;
	u8 addr_cycle;
	u16 cis_block;
	u8 multi_flag;
	u8 err_code;
	u32 capacity;

	struct zone_entry *zone;
	int zone_cnt;

	struct xd_delay_write_tag delay_write;

	int counter;

	int xd_clock;
};

#define TYPE_SD			0x0000
#define TYPE_MMC		0x0001

/* TYPE_SD */
#define SD_HS			0x0100
#define SD_SDR50		0x0200
#define SD_DDR50		0x0400
#define SD_SDR104		0x0800
#define SD_HCXC			0x1000

/* TYPE_MMC */
#define MMC_26M			0x0100
#define MMC_52M			0x0200
#define MMC_4BIT		0x0400
#define MMC_8BIT		0x0800
#define MMC_SECTOR_MODE		0x1000
#define MMC_DDR52		0x2000

/* SD card */
#define CHK_SD(sd_card)			(((sd_card)->sd_type & 0xFF) == TYPE_SD)
#define CHK_SD_HS(sd_card)	\
	(CHK_SD(sd_card) && ((sd_card)->sd_type & SD_HS))
#define CHK_SD_SDR50(sd_card)		\
	(CHK_SD(sd_card) && ((sd_card)->sd_type & SD_SDR50))
#define CHK_SD_DDR50(sd_card)	\
	(CHK_SD(sd_card) && ((sd_card)->sd_type & SD_DDR50))
#define CHK_SD_SDR104(sd_card)	\
	(CHK_SD(sd_card) && ((sd_card)->sd_type & SD_SDR104))
#define CHK_SD_HCXC(sd_card)	\
	(CHK_SD(sd_card) && ((sd_card)->sd_type & SD_HCXC))
#define CHK_SD30_SPEED(sd_card)	\
	(CHK_SD_SDR50(sd_card) || CHK_SD_DDR50(sd_card) ||\
	 CHK_SD_SDR104(sd_card))

#define SET_SD(sd_card)			((sd_card)->sd_type = TYPE_SD)
#define SET_SD_HS(sd_card)		((sd_card)->sd_type |= SD_HS)
#define SET_SD_SDR50(sd_card)		((sd_card)->sd_type |= SD_SDR50)
#define SET_SD_DDR50(sd_card)		((sd_card)->sd_type |= SD_DDR50)
#define SET_SD_SDR104(sd_card)		((sd_card)->sd_type |= SD_SDR104)
#define SET_SD_HCXC(sd_card)		((sd_card)->sd_type |= SD_HCXC)

#define CLR_SD_HS(sd_card)		((sd_card)->sd_type &= ~SD_HS)
#define CLR_SD_SDR50(sd_card)		((sd_card)->sd_type &= ~SD_SDR50)
#define CLR_SD_DDR50(sd_card)		((sd_card)->sd_type &= ~SD_DDR50)
#define CLR_SD_SDR104(sd_card)		((sd_card)->sd_type &= ~SD_SDR104)
#define CLR_SD_HCXC(sd_card)		((sd_card)->sd_type &= ~SD_HCXC)
#define CLR_SD30_SPEED(sd_card)	\
	((sd_card)->sd_type &= ~(SD_SDR50|SD_DDR50|SD_SDR104))

/* MMC card */
#define CHK_MMC(sd_card)	\
	(((sd_card)->sd_type & 0xFF) == TYPE_MMC)
#define CHK_MMC_26M(sd_card)	\
	(CHK_MMC(sd_card) && ((sd_card)->sd_type & MMC_26M))
#define CHK_MMC_52M(sd_card)	\
	(CHK_MMC(sd_card) && ((sd_card)->sd_type & MMC_52M))
#define CHK_MMC_4BIT(sd_card)	\
	(CHK_MMC(sd_card) && ((sd_card)->sd_type & MMC_4BIT))
#define CHK_MMC_8BIT(sd_card)	\
	(CHK_MMC(sd_card) && ((sd_card)->sd_type & MMC_8BIT))
#define CHK_MMC_SECTOR_MODE(sd_card)\
	(CHK_MMC(sd_card) && ((sd_card)->sd_type & MMC_SECTOR_MODE))
#define CHK_MMC_DDR52(sd_card)	\
	(CHK_MMC(sd_card) && ((sd_card)->sd_type & MMC_DDR52))

#define SET_MMC(sd_card)		((sd_card)->sd_type = TYPE_MMC)
#define SET_MMC_26M(sd_card)		((sd_card)->sd_type |= MMC_26M)
#define SET_MMC_52M(sd_card)		((sd_card)->sd_type |= MMC_52M)
#define SET_MMC_4BIT(sd_card)		((sd_card)->sd_type |= MMC_4BIT)
#define SET_MMC_8BIT(sd_card)		((sd_card)->sd_type |= MMC_8BIT)
#define SET_MMC_SECTOR_MODE(sd_card)	((sd_card)->sd_type |= MMC_SECTOR_MODE)
#define SET_MMC_DDR52(sd_card)		((sd_card)->sd_type |= MMC_DDR52)

#define CLR_MMC_26M(sd_card)		((sd_card)->sd_type &= ~MMC_26M)
#define CLR_MMC_52M(sd_card)		((sd_card)->sd_type &= ~MMC_52M)
#define CLR_MMC_4BIT(sd_card)		((sd_card)->sd_type &= ~MMC_4BIT)
#define CLR_MMC_8BIT(sd_card)		((sd_card)->sd_type &= ~MMC_8BIT)
#define CLR_MMC_SECTOR_MODE(sd_card)	((sd_card)->sd_type &= ~MMC_SECTOR_MODE)
#define CLR_MMC_DDR52(sd_card)		((sd_card)->sd_type &= ~MMC_DDR52)

#define CHK_MMC_HS(sd_card)	\
	(CHK_MMC_52M(sd_card) && CHK_MMC_26M(sd_card))
#define CLR_MMC_HS(sd_card)			\
do {						\
	CLR_MMC_DDR52(sd_card);			\
	CLR_MMC_52M(sd_card);			\
	CLR_MMC_26M(sd_card);			\
} while (0)

#define SD_SUPPORT_CLASS_TEN		0x01
#define SD_SUPPORT_1V8			0x02

#define SD_SET_CLASS_TEN(sd_card)	\
	((sd_card)->sd_setting |= SD_SUPPORT_CLASS_TEN)
#define SD_CHK_CLASS_TEN(sd_card)	\
	((sd_card)->sd_setting & SD_SUPPORT_CLASS_TEN)
#define SD_CLR_CLASS_TEN(sd_card)	\
	((sd_card)->sd_setting &= ~SD_SUPPORT_CLASS_TEN)
#define SD_SET_1V8(sd_card)		\
	((sd_card)->sd_setting |= SD_SUPPORT_1V8)
#define SD_CHK_1V8(sd_card)		\
	((sd_card)->sd_setting & SD_SUPPORT_1V8)
#define SD_CLR_1V8(sd_card)		\
	((sd_card)->sd_setting &= ~SD_SUPPORT_1V8)
#define CLR_RETRY_SD20_MODE(sd_card)		\
	((sd_card)->retry_SD20_mode = 0)
#define SET_RETRY_SD20_MODE(sd_card)		\
	((sd_card)->retry_SD20_mode = 1)
#define CHK_RETRY_SD20_MODE(sd_card)		\
	((sd_card)->retry_SD20_mode == 1)

struct sd_info {
	u16 sd_type;
	u8 err_code;
	u8 sd_data_buf_ready;
	u32 sd_addr;
	u32 capacity;

	u8 raw_csd[16];
	u8 raw_scr[8];

	/* Sequential RW */
	int seq_mode;
	enum dma_data_direction pre_dir;
	u32 pre_sec_addr;
	u16 pre_sec_cnt;

	int counter;

	int sd_clock;

#ifdef SUPPORT_CPRM
	int sd_pass_thru_en;
	int pre_cmd_err;
	u8 last_rsp_type;
	u8 rsp[17];
#endif

	u8 func_group1_mask;
	u8 func_group2_mask;
	u8 func_group3_mask;
	u8 func_group4_mask;

	u8 sd_switch_fail;
	u8 sd_read_phase;
	u8 retry_SD20_mode;	/* sangdy2010-06-10 */
	u8 sd_reset_fail;	/* sangdy2010-07-01 */
	u8 sd_send_status_en;

};

#define MODE_512_SEQ		0x01
#define MODE_2K_SEQ		0x02

#define TYPE_MS			0x0000
#define TYPE_MSPRO		0x0001

#define MS_4BIT			0x0100
#define MS_8BIT			0x0200
#define MS_HG			0x0400
#define MS_XC			0x0800

#define HG8BIT			(MS_HG | MS_8BIT)

#define CHK_MSPRO(ms_card)	\
	(((ms_card)->ms_type & 0xFF) == TYPE_MSPRO)
#define CHK_HG8BIT(ms_card)	\
	(CHK_MSPRO(ms_card) && (((ms_card)->ms_type & HG8BIT) == HG8BIT))
#define CHK_MSXC(ms_card)	\
	(CHK_MSPRO(ms_card) && ((ms_card)->ms_type & MS_XC))
#define CHK_MSHG(ms_card)	\
	(CHK_MSPRO(ms_card) && ((ms_card)->ms_type & MS_HG))

#define CHK_MS8BIT(ms_card)	(((ms_card)->ms_type & MS_8BIT))
#define CHK_MS4BIT(ms_card)	(((ms_card)->ms_type & MS_4BIT))

struct ms_delay_write_tag {
	u16 old_phyblock;
	u16 new_phyblock;
	u16 logblock;
	u8 pageoff;
	u8 delay_write_flag;
};

struct ms_info {
	u16 ms_type;
	u8 block_shift;
	u8 page_off;
	u16 total_block;
	u16 boot_block;
	u32 capacity;

	u8 check_ms_flow;
	u8 switch_8bit_fail;
	u8 err_code;

	struct zone_entry *segment;
	int segment_cnt;

	int pro_under_formatting;
	int format_status;
	u16 progress;
	u8 raw_sys_info[96];
#ifdef SUPPORT_PCGL_1P18
	u8 raw_model_name[48];
#endif

	u8 multi_flag;

	/* Sequential RW */
	u8 seq_mode;
	enum dma_data_direction pre_dir;
	u32 pre_sec_addr;
	u16 pre_sec_cnt;
	u32 total_sec_cnt;
	u8 last_rw_int;

	struct ms_delay_write_tag delay_write;

	int counter;

	int ms_clock;

#ifdef SUPPORT_MAGIC_GATE
	u8 magic_gate_id[16];
	u8 mg_entry_num;
	int mg_auth;		/* flag to indicate authentication process */
#endif
};

#define PRO_UNDER_FORMATTING(ms_card)		\
	((ms_card)->pro_under_formatting)
#define SET_FORMAT_STATUS(ms_card, status)	\
	((ms_card)->format_status = (status))
#define CHK_FORMAT_STATUS(ms_card, status)	\
	((ms_card)->format_status == (status))

struct scsi_cmnd;

enum CHIP_STAT { STAT_INIT, STAT_IDLE, STAT_RUN, STAT_SS_PRE, STAT_SS,
	    STAT_SUSPEND };

struct rts51x_chip {
	u16 vendor_id;
	u16 product_id;
	char max_lun;

	struct scsi_cmnd *srb;
	struct sense_data_t sense_buffer[MAX_ALLOWED_LUN_CNT];

	int led_toggle_counter;

	int ss_counter;
	int idle_counter;
	int auto_delink_counter;
	enum CHIP_STAT chip_stat;

	int resume_from_scsi;

	/* Card information */
	struct xd_info xd_card;
	struct sd_info sd_card;
	struct ms_info ms_card;

	int cur_clk;		/* current card clock */
	int cur_card;		/* Current card module */

	u8 card_exist;		/* card exist bit map (physical exist) */
	u8 card_ready;		/* card ready bit map (reset successfully) */
	u8 card_fail;		/* card reset fail bit map */
	u8 card_ejected;	/* card ejected bit map */
	u8 card_wp;		/* card write protected bit map */

	u8 fake_card_ready;
	/* flag to indicate whether to answer MediaChange */
	unsigned long lun_mc;

	/* card bus width */
	u8 card_bus_width[MAX_ALLOWED_LUN_CNT];
	/* card capacity */
	u32 capacity[MAX_ALLOWED_LUN_CNT];

	/* read/write card function pointer */
	card_rw_func rw_card[MAX_ALLOWED_LUN_CNT];
	/* read/write capacity, used for GPIO Toggle */
	u32 rw_cap[MAX_ALLOWED_LUN_CNT];
	/* card to lun mapping table */
	u8 card2lun[32];
	/* lun to card mapping table */
	u8 lun2card[MAX_ALLOWED_LUN_CNT];

#ifdef _MSG_TRACE
	struct trace_msg_t trace_msg[TRACE_ITEM_CNT];
	int msg_idx;
#endif

	int rw_need_retry;

	/* ASIC or FPGA */
	int asic_code;

	/* QFN24 or LQFP48 */
	int package;

	/* Full Speed or High Speed */
	int usb_speed;

	/*sangdy:enable or disable UHS50 and MMC4.4 */
	int uhs50_mmc44_en;

	u8 ic_version;

	/* Command buffer */
	u8 *cmd_buf;
	unsigned int cmd_idx;
	/* Response buffer */
	u8 *rsp_buf;

	u16 card_status;

#ifdef SUPPORT_OCP
	u16 ocp_stat;
#endif

	struct rts51x_option option;
	struct rts51x_usb *usb;

	u8 rcc_read_response;
	int reset_need_retry;
	u8 rts5179;
};

#define UHS50_EN 0x0001
#define UHS50_DIS 0x0000
#define SET_UHS50(chip)   ((chip)->uhs50_mmc44_en = UHS50_EN)
#define CLEAR_UHS50(chip)  ((chip)->uhs50_mmc44_en = UHS50_DIS)
#define CHECK_UHS50(chip)  (((chip)->uhs50_mmc44_en&0xff) == UHS50_EN)

#define RTS51X_GET_VID(chip)		((chip)->vendor_id)
#define RTS51X_GET_PID(chip)		((chip)->product_id)

#define RTS51X_SET_STAT(chip, stat)			\
do {							\
	if ((stat) != STAT_IDLE) {			\
		(chip)->idle_counter = 0;		\
	}						\
	(chip)->chip_stat = (enum CHIP_STAT)(stat);	\
} while (0)
#define RTS51X_CHK_STAT(chip, stat)	((chip)->chip_stat == (stat))
#define RTS51X_GET_STAT(chip)		((chip)->chip_stat)

#define CHECK_PID(chip, pid)		(RTS51X_GET_PID(chip) == (pid))
#define CHECK_PKG(chip, pkg)		((chip)->package == (pkg))
#define CHECK_USB(chip, speed)		((chip)->usb_speed == (speed))

int rts51x_reset_chip(struct rts51x_chip *chip);
int rts51x_init_chip(struct rts51x_chip *chip);
int rts51x_release_chip(struct rts51x_chip *chip);
void rts51x_polling_func(struct rts51x_chip *chip);

static inline void rts51x_init_cmd(struct rts51x_chip *chip)
{
	chip->cmd_idx = 0;
	chip->cmd_buf[0] = 'R';
	chip->cmd_buf[1] = 'T';
	chip->cmd_buf[2] = 'C';
	chip->cmd_buf[3] = 'R';
	chip->cmd_buf[PACKET_TYPE] = BATCH_CMD;
}

void rts51x_add_cmd(struct rts51x_chip *chip,
		    u8 cmd_type, u16 reg_addr, u8 mask, u8 data);
int rts51x_send_cmd(struct rts51x_chip *chip, u8 flag, int timeout);
int rts51x_get_rsp(struct rts51x_chip *chip, int rsp_len, int timeout);

static inline void rts51x_read_rsp_buf(struct rts51x_chip *chip, int offset,
				       u8 *buf, int buf_len)
{
	memcpy(buf, chip->rsp_buf + offset, buf_len);
}

static inline u8 *rts51x_get_rsp_data(struct rts51x_chip *chip)
{
	return chip->rsp_buf;
}

int rts51x_get_card_status(struct rts51x_chip *chip, u16 *status);
int rts51x_write_register(struct rts51x_chip *chip, u16 addr, u8 mask, u8 data);
int rts51x_read_register(struct rts51x_chip *chip, u16 addr, u8 *data);
int rts51x_ep0_write_register(struct rts51x_chip *chip, u16 addr, u8 mask,
			      u8 data);
int rts51x_ep0_read_register(struct rts51x_chip *chip, u16 addr, u8 *data);
int rts51x_seq_write_register(struct rts51x_chip *chip, u16 addr, u16 len,
			      u8 *data);
int rts51x_seq_read_register(struct rts51x_chip *chip, u16 addr, u16 len,
			     u8 *data);
int rts51x_read_ppbuf(struct rts51x_chip *chip, u8 *buf, int buf_len);
int rts51x_write_ppbuf(struct rts51x_chip *chip, u8 *buf, int buf_len);
int rts51x_write_phy_register(struct rts51x_chip *chip, u8 addr, u8 val);
int rts51x_read_phy_register(struct rts51x_chip *chip, u8 addr, u8 *val);
void rts51x_do_before_power_down(struct rts51x_chip *chip);
void rts51x_clear_hw_error(struct rts51x_chip *chip);
void rts51x_prepare_run(struct rts51x_chip *chip);
void rts51x_trace_msg(struct rts51x_chip *chip, unsigned char *buf, int clear);
void rts51x_pp_status(struct rts51x_chip *chip, unsigned int lun, u8 *status,
		      u8 status_len);
void rts51x_read_status(struct rts51x_chip *chip, unsigned int lun,
			u8 *rts51x_status, u8 status_len);
int rts51x_transfer_data_rcc(struct rts51x_chip *chip, unsigned int pipe,
			     void *buf, unsigned int len, int use_sg,
			     unsigned int *act_len, int timeout, u8 stage_flag);

#define RTS51X_WRITE_REG(chip, addr, mask, data)	\
do {							\
	int _retval = rts51x_write_register((chip),	\
			(addr), (mask), (data));	\
	if (_retval != STATUS_SUCCESS) {		\
		TRACE_RET((chip), _retval);		\
	}						\
} while (0)

#define RTS51X_READ_REG(chip, addr, data)		\
do {							\
	int _retval = rts51x_read_register((chip),	\
			(addr), (data));		\
	if (_retval != STATUS_SUCCESS) {		\
		TRACE_RET((chip), _retval);		\
	}						\
} while (0)

#endif /* __RTS51X_CHIP_H */
