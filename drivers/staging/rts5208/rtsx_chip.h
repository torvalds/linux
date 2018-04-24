/* Driver for Realtek PCI-Express card reader
 * Header file
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
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
 *   Wei WANG (wei_wang@realsil.com.cn)
 *   Micky Ching (micky_ching@realsil.com.cn)
 */

#ifndef __REALTEK_RTSX_CHIP_H
#define __REALTEK_RTSX_CHIP_H

#include "rtsx.h"

#define SUPPORT_CPRM
#define SUPPORT_OCP
#define SUPPORT_SDIO_ASPM
#define SUPPORT_MAGIC_GATE
#define SUPPORT_MSXC
#define SUPPORT_SD_LOCK
/* Hardware switch bus_ctl and cd_ctl automatically */
#define HW_AUTO_SWITCH_SD_BUS
/* Enable hardware interrupt write clear */
#define HW_INT_WRITE_CLR
/* #define LED_AUTO_BLINK */
/* #define DISABLE_CARD_INT */

#ifdef SUPPORT_MAGIC_GATE
	/* Using NORMAL_WRITE instead of AUTO_WRITE to set ICV */
	#define MG_SET_ICV_SLOW
	/* HW may miss ERR/CMDNK signal when sampling INT status. */
	#define MS_SAMPLE_INT_ERR
	/*
	 * HW DO NOT support Wait_INT function
	 * during READ_BYTES transfer mode
	 */
	#define READ_BYTES_WAIT_INT
#endif

#ifdef SUPPORT_MSXC
#define XC_POWERCLASS
#define SUPPORT_PCGL_1P18
#endif

#ifndef LED_AUTO_BLINK
#define REGULAR_BLINK
#endif

#define LED_BLINK_SPEED		5
#define LED_TOGGLE_INTERVAL	6
#define	GPIO_TOGGLE_THRESHOLD   1024
#define LED_GPIO		0

#define POLLING_INTERVAL	30

#define TRACE_ITEM_CNT		64

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS		0
#endif
#ifndef STATUS_FAIL
#define STATUS_FAIL		1
#endif
#ifndef STATUS_TIMEDOUT
#define STATUS_TIMEDOUT		2
#endif
#ifndef STATUS_NOMEM
#define STATUS_NOMEM		3
#endif
#ifndef STATUS_READ_FAIL
#define STATUS_READ_FAIL	4
#endif
#ifndef STATUS_WRITE_FAIL
#define STATUS_WRITE_FAIL	5
#endif
#ifndef STATUS_ERROR
#define STATUS_ERROR		10
#endif

#define PM_S1			1
#define PM_S3			3

/*
 * Transport return codes
 */

#define TRANSPORT_GOOD		0   /* Transport good, command good	   */
#define TRANSPORT_FAILED	1   /* Transport good, command failed   */
#define TRANSPORT_NO_SENSE	2  /* Command failed, no auto-sense    */
#define TRANSPORT_ERROR		3   /* Transport bad (i.e. device dead) */

/*
 * Start-Stop-Unit
 */
#define STOP_MEDIUM			0x00    /* access disable         */
#define MAKE_MEDIUM_READY		0x01    /* access enable          */
#define UNLOAD_MEDIUM			0x02    /* unload                 */
#define LOAD_MEDIUM			0x03    /* load                   */

/*
 * STANDARD_INQUIRY
 */
#define QULIFIRE                0x00
#define AENC_FNC                0x00
#define TRML_IOP                0x00
#define REL_ADR                 0x00
#define WBUS_32                 0x00
#define WBUS_16                 0x00
#define SYNC                    0x00
#define LINKED                  0x00
#define CMD_QUE                 0x00
#define SFT_RE                  0x00

#define VEN_ID_LEN              8               /* Vendor ID Length         */
#define PRDCT_ID_LEN            16              /* Product ID Length        */
#define PRDCT_REV_LEN           4               /* Product LOT Length       */

/* Dynamic flag definitions: used in set_bit() etc. */
/* 0x00040000 transfer is active */
#define RTSX_FLIDX_TRANS_ACTIVE		18
/* 0x00100000 abort is in progress */
#define RTSX_FLIDX_ABORTING		20
/* 0x00200000 disconnect in progress */
#define RTSX_FLIDX_DISCONNECTING	21

#define ABORTING_OR_DISCONNECTING	((1UL << US_FLIDX_ABORTING) | \
					 (1UL << US_FLIDX_DISCONNECTING))

/* 0x00400000 device reset in progress */
#define RTSX_FLIDX_RESETTING		22
/* 0x00800000 SCSI midlayer timed out  */
#define RTSX_FLIDX_TIMED_OUT		23
#define DRCT_ACCESS_DEV         0x00    /* Direct Access Device      */
#define RMB_DISC                0x80    /* The Device is Removable   */
#define ANSI_SCSI2              0x02    /* Based on ANSI-SCSI2       */

#define SCSI                    0x00    /* Interface ID              */

#define	WRITE_PROTECTED_MEDIA 0x07

/*---- sense key ----*/
#define ILI                     0x20    /* ILI bit is on                    */

#define NO_SENSE                0x00    /* not exist sense key              */
#define RECOVER_ERR             0x01    /* Target/Logical unit is recoverd  */
#define NOT_READY               0x02    /* Logical unit is not ready        */
#define MEDIA_ERR               0x03    /* medium/data error                */
#define HARDWARE_ERR            0x04    /* hardware error                   */
#define ILGAL_REQ               0x05    /* CDB/parameter/identify msg error */
#define UNIT_ATTENTION          0x06    /* unit attention condition occur   */
#define DAT_PRTCT               0x07    /* read/write is desable            */
#define BLNC_CHK                0x08    /* find blank/DOF in read           */
					/* write to unblank area            */
#define CPY_ABRT                0x0a    /* Copy/Compare/Copy&Verify illgal  */
#define ABRT_CMD                0x0b    /* Target make the command in error */
#define EQUAL                   0x0c    /* Search Data end with Equal       */
#define VLM_OVRFLW              0x0d    /* Some data are left in buffer     */
#define MISCMP                  0x0e    /* find inequality                  */

#define READ_ERR                -1
#define WRITE_ERR               -2

#define	FIRST_RESET		0x01
#define	USED_EXIST		0x02

/*
 * SENSE_DATA
 */
/*---- valid ----*/
#define SENSE_VALID             0x80    /* Sense data is valid as SCSI2     */
#define SENSE_INVALID           0x00    /* Sense data is invalid as SCSI2   */

/*---- error code ----*/
#define CUR_ERR                 0x70    /* current error                    */
#define DEF_ERR                 0x71    /* specific command error           */

/*---- sense key Information ----*/
#define SNSKEYINFO_LEN          3       /* length of sense key information   */

#define SKSV                    0x80
#define CDB_ILLEGAL             0x40
#define DAT_ILLEGAL             0x00
#define BPV                     0x08
#define BIT_ILLEGAL0            0       /* bit0 is illegal                  */
#define BIT_ILLEGAL1            1       /* bit1 is illegal                  */
#define BIT_ILLEGAL2            2       /* bit2 is illegal                  */
#define BIT_ILLEGAL3            3       /* bit3 is illegal                  */
#define BIT_ILLEGAL4            4       /* bit4 is illegal                  */
#define BIT_ILLEGAL5            5       /* bit5 is illegal                  */
#define BIT_ILLEGAL6            6       /* bit6 is illegal                  */
#define BIT_ILLEGAL7            7       /* bit7 is illegal                  */

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
	unsigned char   err_code;	/* error code */
	/* bit7 : valid */
	/*   (1 : SCSI2) */
	/*   (0 : Vendor * specific) */
	/* bit6-0 : error * code */
	/*  (0x70 : current * error) */
	/*  (0x71 : specific command error) */
	unsigned char   seg_no;		/* segment No.                      */
	unsigned char   sense_key;	/* byte5 : ILI                      */
	/* bit3-0 : sense key              */
	unsigned char   info[4];	/* information                       */
	unsigned char   ad_sense_len;	/* additional sense data length     */
	unsigned char   cmd_info[4];	/* command specific information      */
	unsigned char   asc;		/* ASC                              */
	unsigned char   ascq;		/* ASCQ                             */
	unsigned char   rfu;		/* FRU                              */
	unsigned char   sns_key_info[3];/* sense key specific information    */
};

/* PCI Operation Register Address */
#define RTSX_HCBAR		0x00
#define RTSX_HCBCTLR		0x04
#define RTSX_HDBAR		0x08
#define RTSX_HDBCTLR		0x0C
#define RTSX_HAIMR		0x10
#define RTSX_BIPR		0x14
#define RTSX_BIER		0x18

/* Host command buffer control register */
#define STOP_CMD		(0x01 << 28)

/* Host data buffer control register */
#define SDMA_MODE		0x00
#define ADMA_MODE		(0x02 << 26)
#define STOP_DMA		(0x01 << 28)
#define TRIG_DMA		(0x01 << 31)

/* Bus interrupt pending register */
#define CMD_DONE_INT		BIT(31)
#define DATA_DONE_INT		BIT(30)
#define TRANS_OK_INT		BIT(29)
#define TRANS_FAIL_INT		BIT(28)
#define XD_INT			BIT(27)
#define MS_INT			BIT(26)
#define SD_INT			BIT(25)
#define GPIO0_INT		BIT(24)
#define OC_INT			BIT(23)
#define SD_WRITE_PROTECT	BIT(19)
#define XD_EXIST		BIT(18)
#define MS_EXIST		BIT(17)
#define SD_EXIST		BIT(16)
#define DELINK_INT		GPIO0_INT
#define MS_OC_INT		BIT(23)
#define SD_OC_INT		BIT(22)

#define CARD_INT		(XD_INT | MS_INT | SD_INT)
#define NEED_COMPLETE_INT	(DATA_DONE_INT | TRANS_OK_INT | TRANS_FAIL_INT)
#define RTSX_INT		(CMD_DONE_INT | NEED_COMPLETE_INT | CARD_INT | \
				 GPIO0_INT | OC_INT)

#define CARD_EXIST		(XD_EXIST | MS_EXIST | SD_EXIST)

/* Bus interrupt enable register */
#define CMD_DONE_INT_EN		BIT(31)
#define DATA_DONE_INT_EN	BIT(30)
#define TRANS_OK_INT_EN		BIT(29)
#define TRANS_FAIL_INT_EN	BIT(28)
#define XD_INT_EN		BIT(27)
#define MS_INT_EN		BIT(26)
#define SD_INT_EN		BIT(25)
#define GPIO0_INT_EN		BIT(24)
#define OC_INT_EN		BIT(23)
#define DELINK_INT_EN		GPIO0_INT_EN
#define MS_OC_INT_EN		BIT(23)
#define SD_OC_INT_EN		BIT(22)

#define READ_REG_CMD		0
#define WRITE_REG_CMD		1
#define CHECK_REG_CMD		2

#define HOST_TO_DEVICE		0
#define DEVICE_TO_HOST		1

#define RTSX_RESV_BUF_LEN	4096
#define HOST_CMDS_BUF_LEN	1024
#define HOST_SG_TBL_BUF_LEN	(RTSX_RESV_BUF_LEN - HOST_CMDS_BUF_LEN)

#define SD_NR		2
#define MS_NR		3
#define XD_NR		4
#define SPI_NR		7
#define SD_CARD		BIT(SD_NR)
#define MS_CARD		BIT(MS_NR)
#define XD_CARD		BIT(XD_NR)
#define SPI_CARD	BIT(SPI_NR)

#define MAX_ALLOWED_LUN_CNT	8

#define XD_FREE_TABLE_CNT	1200
#define MS_FREE_TABLE_CNT	512

/* Bit Operation */
#define SET_BIT(data, idx)	((data) |= 1 << (idx))
#define CLR_BIT(data, idx)	((data) &= ~(1 << (idx)))
#define CHK_BIT(data, idx)	((data) & (1 << (idx)))

/* SG descriptor */
#define RTSX_SG_INT		0x04
#define RTSX_SG_END		0x02
#define RTSX_SG_VALID		0x01

#define RTSX_SG_NO_OP		0x00
#define RTSX_SG_TRANS_DATA	(0x02 << 4)
#define RTSX_SG_LINK_DESC	(0x03 << 4)

struct rtsx_chip;

typedef int (*card_rw_func)(struct scsi_cmnd *srb, struct rtsx_chip *chip,
			u32 sec_addr, u16 sec_cnt);

/* Supported Clock */
enum card_clock	{CLK_20 = 1, CLK_30, CLK_40, CLK_50, CLK_60,
		 CLK_80, CLK_100, CLK_120, CLK_150, CLK_200};

enum RTSX_STAT	{RTSX_STAT_INIT, RTSX_STAT_IDLE, RTSX_STAT_RUN, RTSX_STAT_SS,
		 RTSX_STAT_DELINK, RTSX_STAT_SUSPEND,
		 RTSX_STAT_ABORT, RTSX_STAT_DISCONNECT};
enum IC_VER	{IC_VER_AB, IC_VER_C = 2, IC_VER_D = 3};

#define MAX_RESET_CNT		3

/* For MS Card */
#define MAX_DEFECTIVE_BLOCK     10

struct zone_entry {
	u16 *l2p_table;
	u16 *free_table;
	u16 defect_list[MAX_DEFECTIVE_BLOCK];  /* For MS card only */
	int set_index;
	int get_index;
	int unused_blk_cnt;
	int disable_count;
	/* To indicate whether the L2P table of this zone has been built. */
	int build_flag;
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
#define CHK_SD_HS(sd_card)		(CHK_SD(sd_card) && \
					 ((sd_card)->sd_type & SD_HS))
#define CHK_SD_SDR50(sd_card)		(CHK_SD(sd_card) && \
					 ((sd_card)->sd_type & SD_SDR50))
#define CHK_SD_DDR50(sd_card)		(CHK_SD(sd_card) && \
					 ((sd_card)->sd_type & SD_DDR50))
#define CHK_SD_SDR104(sd_card)		(CHK_SD(sd_card) && \
					 ((sd_card)->sd_type & SD_SDR104))
#define CHK_SD_HCXC(sd_card)		(CHK_SD(sd_card) && \
					 ((sd_card)->sd_type & SD_HCXC))
#define CHK_SD_HC(sd_card)		(CHK_SD_HCXC(sd_card) && \
					 ((sd_card)->capacity <= 0x4000000))
#define CHK_SD_XC(sd_card)		(CHK_SD_HCXC(sd_card) && \
					 ((sd_card)->capacity > 0x4000000))
#define CHK_SD30_SPEED(sd_card)		(CHK_SD_SDR50(sd_card) || \
					 CHK_SD_DDR50(sd_card) || \
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

/* MMC card */
#define CHK_MMC(sd_card)		(((sd_card)->sd_type & 0xFF) == \
					 TYPE_MMC)
#define CHK_MMC_26M(sd_card)		(CHK_MMC(sd_card) && \
					 ((sd_card)->sd_type & MMC_26M))
#define CHK_MMC_52M(sd_card)		(CHK_MMC(sd_card) && \
					 ((sd_card)->sd_type & MMC_52M))
#define CHK_MMC_4BIT(sd_card)		(CHK_MMC(sd_card) && \
					 ((sd_card)->sd_type & MMC_4BIT))
#define CHK_MMC_8BIT(sd_card)		(CHK_MMC(sd_card) && \
					 ((sd_card)->sd_type & MMC_8BIT))
#define CHK_MMC_SECTOR_MODE(sd_card)	(CHK_MMC(sd_card) && \
					 ((sd_card)->sd_type & MMC_SECTOR_MODE))
#define CHK_MMC_DDR52(sd_card)		(CHK_MMC(sd_card) && \
					 ((sd_card)->sd_type & MMC_DDR52))

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

#define CHK_MMC_HS(sd_card)		(CHK_MMC_52M(sd_card) && \
					 CHK_MMC_26M(sd_card))
#define CLR_MMC_HS(sd_card)			\
do {						\
	CLR_MMC_DDR52(sd_card);			\
	CLR_MMC_52M(sd_card);			\
	CLR_MMC_26M(sd_card);			\
} while (0)

#define SD_SUPPORT_CLASS_TEN		0x01
#define SD_SUPPORT_1V8			0x02

#define SD_SET_CLASS_TEN(sd_card)	((sd_card)->sd_setting |= \
					 SD_SUPPORT_CLASS_TEN)
#define SD_CHK_CLASS_TEN(sd_card)	((sd_card)->sd_setting & \
					 SD_SUPPORT_CLASS_TEN)
#define SD_CLR_CLASS_TEN(sd_card)	((sd_card)->sd_setting &= \
					 ~SD_SUPPORT_CLASS_TEN)
#define SD_SET_1V8(sd_card)		((sd_card)->sd_setting |= \
					 SD_SUPPORT_1V8)
#define SD_CHK_1V8(sd_card)		((sd_card)->sd_setting & \
					 SD_SUPPORT_1V8)
#define SD_CLR_1V8(sd_card)		((sd_card)->sd_setting &= \
					 ~SD_SUPPORT_1V8)

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

	int cleanup_counter;

	int sd_clock;

	int mmc_dont_switch_bus;

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

#ifdef SUPPORT_SD_LOCK
	u8 sd_lock_status;
	u8 sd_erase_status;
	u8 sd_lock_notify;
#endif
	int need_retune;
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
	int cleanup_counter;

	int xd_clock;
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

#define CHK_MSPRO(ms_card)	(((ms_card)->ms_type & 0xFF) == TYPE_MSPRO)
#define CHK_HG8BIT(ms_card)	(CHK_MSPRO(ms_card) && \
				 (((ms_card)->ms_type & HG8BIT) == HG8BIT))
#define CHK_MSXC(ms_card)	(CHK_MSPRO(ms_card) && \
				 ((ms_card)->ms_type & MS_XC))
#define CHK_MSHG(ms_card)	(CHK_MSPRO(ms_card) && \
				 ((ms_card)->ms_type & MS_HG))

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

	struct ms_delay_write_tag delay_write;

	int cleanup_counter;

	int ms_clock;

#ifdef SUPPORT_MAGIC_GATE
	u8 magic_gate_id[16];
	u8 mg_entry_num;
	int mg_auth;    /* flag to indicate authentication process */
#endif
};

struct spi_info {
	u8 use_clk;
	u8 write_en;
	u16 clk_div;
	u8 err_code;

	int spi_clock;
};

#ifdef _MSG_TRACE
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
#endif

/************/
/* LUN mode */
/************/
/* Single LUN, support xD/SD/MS */
#define DEFAULT_SINGLE		0
/* 2 LUN mode, support SD/MS */
#define SD_MS_2LUN		1
/* Single LUN, but only support SD/MS, for Barossa LQFP */
#define SD_MS_1LUN		2

#define LAST_LUN_MODE		2

/* Barossa package */
#define QFN		0
#define LQFP		1

/******************/
/* sd_ctl bit map */
/******************/
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
#define SUPPORT_MMC_DDR_MODE		0x40
/* Reset MMC at first */
#define RESET_MMC_FIRST			0x80

#define SEQ_START_CRITERIA		0x20

/* MS Power Class En */
#define POWER_CLASS_2_EN		0x02
#define POWER_CLASS_1_EN		0x01

#define MAX_SHOW_CNT			10
#define MAX_RESET_CNT			3

#define SDIO_EXIST			0x01
#define SDIO_IGNORED			0x02

#define CHK_SDIO_EXIST(chip)		((chip)->sdio_func_exist & SDIO_EXIST)
#define SET_SDIO_EXIST(chip)		((chip)->sdio_func_exist |= SDIO_EXIST)
#define CLR_SDIO_EXIST(chip)		((chip)->sdio_func_exist &= ~SDIO_EXIST)

#define CHK_SDIO_IGNORED(chip)		((chip)->sdio_func_exist & SDIO_IGNORED)
#define SET_SDIO_IGNORED(chip)		((chip)->sdio_func_exist |= \
					 SDIO_IGNORED)
#define CLR_SDIO_IGNORED(chip)		((chip)->sdio_func_exist &= \
					 ~SDIO_IGNORED)

struct rtsx_chip {
	struct rtsx_dev	*rtsx;

	u32		int_reg; /* Bus interrupt pending register */
	char		max_lun;
	void		*context;

	void		*host_cmds_ptr;	/* host commands buffer pointer */
	dma_addr_t	host_cmds_addr;
	int		ci;			/* Command Index */

	void		*host_sg_tbl_ptr;	/* SG descriptor table */
	dma_addr_t	host_sg_tbl_addr;
	int		sgi;			/* SG entry index */

	struct scsi_cmnd	*srb;			/* current srb */
	struct sense_data_t	sense_buffer[MAX_ALLOWED_LUN_CNT];

	int			cur_clk;		/* current card clock */

	/* Current accessed card */
	int			cur_card;

	unsigned long	need_release;		/* need release bit map */
	unsigned long	need_reset;		/* need reset bit map */
	/*
	 * Flag to indicate that this card is just resumed from SS state,
	 * and need released before being resetted
	 */
	unsigned long		need_reinit;

	int			rw_need_retry;

#ifdef SUPPORT_OCP
	u32			ocp_int;
	u8			ocp_stat;
#endif

	u8	card_exist;	/* card exist bit map (physical exist) */
	u8	card_ready;	/* card ready bit map (reset successfully) */
	u8	card_fail;	/* card reset fail bit map */
	u8	card_ejected;	/* card ejected bit map */
	u8	card_wp;	/* card write protected bit map */

	u8	lun_mc;		/*
				 * flag to indicate whether to answer
				 * MediaChange
				 */

#ifndef LED_AUTO_BLINK
	int			led_toggle_counter;
#endif

	int			sd_reset_counter;
	int			xd_reset_counter;
	int			ms_reset_counter;

	/* card bus width */
	u8			card_bus_width[MAX_ALLOWED_LUN_CNT];
	/* card capacity */
	u32			capacity[MAX_ALLOWED_LUN_CNT];
	/* read/write card function pointer */
	card_rw_func		rw_card[MAX_ALLOWED_LUN_CNT];
	/* read/write capacity, used for GPIO Toggle */
	u32			rw_cap[MAX_ALLOWED_LUN_CNT];
	/* card to lun mapping table */
	u8			card2lun[32];
	/* lun to card mapping table */
	u8			lun2card[MAX_ALLOWED_LUN_CNT];

	int			rw_fail_cnt[MAX_ALLOWED_LUN_CNT];

	int			sd_show_cnt;
	int			xd_show_cnt;
	int			ms_show_cnt;

	/* card information */
	struct sd_info		sd_card;
	struct xd_info		xd_card;
	struct ms_info		ms_card;

	struct spi_info		spi;

#ifdef _MSG_TRACE
	struct trace_msg_t	trace_msg[TRACE_ITEM_CNT];
	int			msg_idx;
#endif

	int			auto_delink_cnt;
	int			auto_delink_allowed;

	int			aspm_enabled;

	int			sdio_aspm;
	int			sdio_idle;
	int			sdio_counter;
	u8			sdio_raw_data[12];

	u8			sd_io;
	u8			sd_int;

	u8			rtsx_flag;

	int			ss_counter;
	int			idle_counter;
	enum RTSX_STAT		rtsx_stat;

	u16			vendor_id;
	u16			product_id;
	u8			ic_version;

	int			driver_first_load;

#ifdef HW_AUTO_SWITCH_SD_BUS
	int			sdio_in_charge;
#endif

	u8			aspm_level[2];

	int			chip_insert_with_sdio;

	/* Options */

	int adma_mode;

	int auto_delink_en;
	int ss_en;
	u8 lun_mode;
	u8 aspm_l0s_l1_en;

	int power_down_in_ss;

	int sdr104_en;
	int ddr50_en;
	int sdr50_en;

	int baro_pkg;

	int asic_code;
	int phy_debug_mode;
	int hw_bypass_sd;
	int sdio_func_exist;
	int aux_pwr_exist;
	u8 ms_power_class_en;

	int mspro_formatter_enable;

	int remote_wakeup_en;

	int ignore_sd;
	int use_hw_setting;

	int ss_idle_period;

	int dynamic_aspm;

	int fpga_sd_sdr104_clk;
	int fpga_sd_ddr50_clk;
	int fpga_sd_sdr50_clk;
	int fpga_sd_hs_clk;
	int fpga_mmc_52m_clk;
	int fpga_ms_hg_clk;
	int fpga_ms_4bit_clk;
	int fpga_ms_1bit_clk;

	int asic_sd_sdr104_clk;
	int asic_sd_ddr50_clk;
	int asic_sd_sdr50_clk;
	int asic_sd_hs_clk;
	int asic_mmc_52m_clk;
	int asic_ms_hg_clk;
	int asic_ms_4bit_clk;
	int asic_ms_1bit_clk;

	u8 ssc_depth_sd_sdr104;
	u8 ssc_depth_sd_ddr50;
	u8 ssc_depth_sd_sdr50;
	u8 ssc_depth_sd_hs;
	u8 ssc_depth_mmc_52m;
	u8 ssc_depth_ms_hg;
	u8 ssc_depth_ms_4bit;
	u8 ssc_depth_low_speed;

	u8 card_drive_sel;
	u8 sd30_drive_sel_1v8;
	u8 sd30_drive_sel_3v3;

	u8 sd_400mA_ocp_thd;
	u8 sd_800mA_ocp_thd;
	u8 ms_ocp_thd;

	int ssc_en;
	int msi_en;

	int xd_timeout;
	int sd_timeout;
	int ms_timeout;
	int mspro_timeout;

	int auto_power_down;

	int sd_ddr_tx_phase;
	int mmc_ddr_tx_phase;
	int sd_default_tx_phase;
	int sd_default_rx_phase;

	int pmos_pwr_on_interval;
	int sd_voltage_switch_delay;
	int s3_pwr_off_delay;

	int force_clkreq_0;
	int ft2_fast_mode;

	int do_delink_before_power_down;
	int polling_config;
	int sdio_retry_cnt;

	int delink_stage1_step;
	int delink_stage2_step;
	int delink_stage3_step;

	int auto_delink_in_L1;
	int hp_watch_bios_hotplug;
	int support_ms_8bit;

	u8 blink_led;
	u8 phy_voltage;
	u8 max_payload;

	u32 sd_speed_prior;
	u32 sd_current_prior;
	u32 sd_ctl;
};

static inline struct device *rtsx_dev(const struct rtsx_chip *chip)
{
	return &chip->rtsx->pci->dev;
}

#define rtsx_set_stat(chip, stat)				\
do {								\
	if ((stat) != RTSX_STAT_IDLE) {				\
		(chip)->idle_counter = 0;			\
	}							\
	(chip)->rtsx_stat = (enum RTSX_STAT)(stat);		\
} while (0)
#define rtsx_get_stat(chip)		((chip)->rtsx_stat)
#define rtsx_chk_stat(chip, stat)	((chip)->rtsx_stat == (stat))

#define RTSX_SET_DELINK(chip)	((chip)->rtsx_flag |= 0x01)
#define RTSX_CLR_DELINK(chip)	((chip)->rtsx_flag &= 0xFE)
#define RTSX_TST_DELINK(chip)	((chip)->rtsx_flag & 0x01)

#define CHECK_PID(chip, pid)		((chip)->product_id == (pid))
#define CHECK_BARO_PKG(chip, pkg)	((chip)->baro_pkg == (pkg))
#define CHECK_LUN_MODE(chip, mode)	((chip)->lun_mode == (mode))

/* Power down control */
#define SSC_PDCTL		0x01
#define OC_PDCTL		0x02

int rtsx_force_power_on(struct rtsx_chip *chip, u8 ctl);
int rtsx_force_power_down(struct rtsx_chip *chip, u8 ctl);

void rtsx_enable_card_int(struct rtsx_chip *chip);
void rtsx_enable_bus_int(struct rtsx_chip *chip);
void rtsx_disable_bus_int(struct rtsx_chip *chip);
int rtsx_reset_chip(struct rtsx_chip *chip);
int rtsx_init_chip(struct rtsx_chip *chip);
void rtsx_release_chip(struct rtsx_chip *chip);
void rtsx_polling_func(struct rtsx_chip *chip);
void rtsx_stop_cmd(struct rtsx_chip *chip, int card);
int rtsx_write_register(struct rtsx_chip *chip, u16 addr, u8 mask, u8 data);
int rtsx_read_register(struct rtsx_chip *chip, u16 addr, u8 *data);
int rtsx_write_cfg_dw(struct rtsx_chip *chip,
		      u8 func_no, u16 addr, u32 mask, u32 val);
int rtsx_read_cfg_dw(struct rtsx_chip *chip, u8 func_no, u16 addr, u32 *val);
int rtsx_write_cfg_seq(struct rtsx_chip *chip,
		       u8 func, u16 addr, u8 *buf, int len);
int rtsx_read_cfg_seq(struct rtsx_chip *chip,
		      u8 func, u16 addr, u8 *buf, int len);
int rtsx_write_phy_register(struct rtsx_chip *chip, u8 addr, u16 val);
int rtsx_read_phy_register(struct rtsx_chip *chip, u8 addr, u16 *val);
int rtsx_read_efuse(struct rtsx_chip *chip, u8 addr, u8 *val);
int rtsx_write_efuse(struct rtsx_chip *chip, u8 addr, u8 val);
int rtsx_clr_phy_reg_bit(struct rtsx_chip *chip, u8 reg, u8 bit);
int rtsx_set_phy_reg_bit(struct rtsx_chip *chip, u8 reg, u8 bit);
void rtsx_enter_ss(struct rtsx_chip *chip);
void rtsx_exit_ss(struct rtsx_chip *chip);
int rtsx_pre_handle_interrupt(struct rtsx_chip *chip);
void rtsx_enter_L1(struct rtsx_chip *chip);
void rtsx_exit_L1(struct rtsx_chip *chip);
void rtsx_do_before_power_down(struct rtsx_chip *chip, int pm_stat);
void rtsx_enable_aspm(struct rtsx_chip *chip);
void rtsx_disable_aspm(struct rtsx_chip *chip);
int rtsx_read_ppbuf(struct rtsx_chip *chip, u8 *buf, int buf_len);
int rtsx_write_ppbuf(struct rtsx_chip *chip, u8 *buf, int buf_len);
int rtsx_check_chip_exist(struct rtsx_chip *chip);

#endif  /* __REALTEK_RTSX_CHIP_H */
