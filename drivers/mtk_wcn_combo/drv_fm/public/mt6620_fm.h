/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 *
 * MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek Software")
 * have been modified by MediaTek Inc. All revisions are subject to any receiver's
 * applicable license agreements with MediaTek Inc.
 */

#ifndef __MT6620_FM_H__
#define __MT6620_FM_H__

#include <linux/cdev.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include "fm.h"

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define RDS_RX_BLOCK_PER_GROUP (4)
#define RDS_RX_GROUP_SIZE (2*RDS_RX_BLOCK_PER_GROUP)
#define MAX_RDS_RX_GROUP_CNT (12)
#define FM_RDS_ENABLE        0x01 // 1: enable RDS, 0:disable RDS


/******************DBG level added by DDB************************/
#define D_BASE  4
#define D_IOCTL (1 << (D_BASE+0))
#define D_RX    (1 << (D_BASE+1))
#define D_TIMER (1 << (D_BASE+2))
#define D_BLKC	(1 << (D_BASE+3))
#define D_G0	(1 << (D_BASE+4))
#define D_G1	(1 << (D_BASE+5))
#define D_G2	(1 << (D_BASE+6))
#define D_G3	(1 << (D_BASE+7))
#define D_G4	(1 << (D_BASE+8))
#define D_G14	(1 << (D_BASE+9))
#define D_RAW	(1 << (D_BASE+10))
#define D_RDS	(1 << (D_BASE+11))
#define D_INIT	(1 << (D_BASE+12))
#define D_MAIN	(1 << (D_BASE+13))
#define D_CMD	(1 << (D_BASE+14))
#define D_ALL	0xfffffff0

#define L0 0x00000000 // EMERG, system will crush 
#define L1 0x00000001 // ALERT, need action in time
#define L2 0x00000002 // CRIT, important HW or SW operation failed 
#define L3 0x00000003 // ERR, normal HW or SW ERR 
#define L4 0x00000004 // WARNING, importan path or somewhere may occurs err
#define L5 0x00000005 // NOTICE, normal case 
#define L6 0x00000006 // INFO, print info if need 
#define L7 0x00000007 // DEBUG, for debug info

#define FM_EMERG    L0
#define FM_ALERT    L1
#define FM_CRIT     L2
#define FM_ERR      L3
#define FM_WARNING  L4
#define FM_NOTICE   L5
#define FM_INFO     L6
#define FM_DEBUG    L7

extern uint32_t g_dbg_level;

#if 0
#define WCN_DBG(flag, fmt, args...) \
	do { \
		if ((((flag)&0x0000000f)<=(g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
			printk(KERN_ALERT "[" #flag "]" fmt, ## args); \
		} \
	} while(0)
#endif

//#define FM_USE_XLOG

#ifdef FM_USE_XLOG
#include <linux/xlog.h>
#define FM_DRV_LOG_TAG "FM_DRV"

#define FM_LOG_DBG(flag, fmt, args...) \
	do{ \
		if((FM_DEBUG <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
            xlog_printk(ANDROID_LOG_INFO, FM_DRV_LOG_TAG, "[" #flag "]" fmt, ## args); \
		} \
	} while(0)

#define FM_LOG_INF(flag, fmt, args...) \
    do{ \
        if((FM_INFO <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
            xlog_printk(ANDROID_LOG_INFO, FM_DRV_LOG_TAG, "[" #flag "]" fmt, ## args); \
        } \
    } while(0)

#define FM_LOG_NTC(flag, fmt, args...) \
    do{ \
        if((FM_NOTICE <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
            xlog_printk(ANDROID_LOG_WARN, FM_DRV_LOG_TAG, "[" #flag "]" fmt, ## args); \
        } \
    } while(0)

#define FM_LOG_WAR(flag, fmt, args...) \
    do{ \
        if((FM_WARNING <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
            xlog_printk(ANDROID_LOG_WARN, FM_DRV_LOG_TAG, "[" #flag "]" fmt, ## args); \
        } \
    } while(0)

#define FM_LOG_ERR(flag, fmt, args...) \
    do{ \
        if((FM_ERR <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
            xlog_printk(ANDROID_LOG_ERROR, FM_DRV_LOG_TAG, "[" #flag "]" fmt, ## args); \
        } \
    } while(0)

#define FM_LOG_CRT(flag, fmt, args...) \
    do{ \
        if((FM_CRIT <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
            xlog_printk(ANDROID_LOG_FATAL, FM_DRV_LOG_TAG, "[" #flag "]" fmt, ## args); \
        } \
    } while(0)

#define FM_LOG_ALT(flag, fmt, args...) \
    do{ \
        if((FM_ALERT <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
            xlog_printk(ANDROID_LOG_FATAL, FM_DRV_LOG_TAG, "[" #flag "]" fmt, ## args); \
        } \
    } while(0)

#define FM_LOG_EMG(flag, fmt, args...) \
    do{ \
        if((FM_EMERG <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
            xlog_printk(ANDROID_LOG_FATAL, FM_DRV_LOG_TAG, "[" #flag "]" fmt, ## args); \
        } \
    } while(0)

#else

#define FM_LOG_DBG(flag, fmt, args...) \
            do{ \
                if((FM_DEBUG <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
                    printk(KERN_DEBUG "[" #flag "]" fmt, ## args); \
                } \
            } while(0)
        
#define FM_LOG_INF(flag, fmt, args...) \
            do{ \
                if((FM_INFO <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
                    printk(KERN_INFO "[" #flag "]" fmt, ## args); \
                } \
            } while(0)
        
#define FM_LOG_NTC(flag, fmt, args...) \
            do{ \
                if((FM_NOTICE <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
                    printk(KERN_NOTICE "[" #flag "]" fmt, ## args); \
                } \
            } while(0)
        
#define FM_LOG_WAR(flag, fmt, args...) \
            do{ \
                if((FM_WARNING <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
                    printk(KERN_WARNING "[" #flag "]" fmt, ## args); \
                } \
            } while(0)
        
#define FM_LOG_ERR(flag, fmt, args...) \
            do{ \
                if((FM_ERR <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
                    printk(KERN_ERR "[" #flag "]" fmt, ## args); \
                } \
            } while(0)
        
#define FM_LOG_CRT(flag, fmt, args...) \
            do{ \
                if((FM_CRIT <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
                    printk(KERN_CRIT "[" #flag "]" fmt, ## args); \
                } \
            } while(0)
        
#define FM_LOG_ALT(flag, fmt, args...) \
            do{ \
                if((FM_ALERT <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
                    printk(KERN_ALERT "[" #flag "]" fmt, ## args); \
                } \
            } while(0)
        
#define FM_LOG_EMG(flag, fmt, args...) \
            do{ \
                if((FM_EMERG <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
                    printk(KERN_EMERG "[" #flag "]" fmt, ## args); \
                } \
            } while(0)
#endif

#define BITn(n) (uint16_t)(1<<(n))
#define MASK(n) (uint16_t)(~(1<<(n)))

#define FM_COM_ASSERT(n) {\
    if(!(n)){\
            FM_LOG_ERR(D_ALL, "%s(), %d\n", __func__, __LINE__);\
            return -EFAULT;\
        }\
    }


typedef enum
{
	FM_TX_NOT_SUPPORT = 0,
	FM_TX_SUPPORT
}fm_tx_support_flag;

typedef enum
{
	FM_RDS_TX_NOT_SUPPORT = 0,
	FM_RDS_TX_SUPPORT
}fm_rds_tx_support_flag;

typedef enum
{
	FM_RDS_TX_DISENABLE = 0,
	FM_RDS_TX_ENABLE
}fm_rds_tx_enable_state;

typedef enum
{
	FM_OVER_BT_DISABLE = 0,
	FM_OVER_BT_ENABLE
}fm_over_bt_enable_state;

typedef enum
{
	FM_LONG_ANA = 0,
	FM_SHORT_ANA
}fm_antenna_type;

typedef struct
{
    uint16_t blkA;
    uint16_t blkB;
    uint16_t blkC;
    uint16_t blkD;
    uint16_t cbc; //correct bit cnt
    uint16_t crc; //crc checksum
}rds_packet_struct;

struct rds_rx
{
    uint16_t sin;
    uint16_t cos;
    rds_packet_struct data[MAX_RDS_RX_GROUP_CNT];
};

typedef enum fm_pwr_status{
    FM_PWR_OFF = 0,
    FM_PWR_RX_ON = 1,
    FM_PWR_TX_ON = 2,
    FM_PWR_MAX
}fm_pwr_status;

struct fm {
    volatile uint ref;
    volatile bool chipon;  //MT6620 chip power state
    volatile fm_pwr_status powerup;      //FM module power state
    atomic_t tx_support; //FM Tx support flag, 1: support, 0: not support
    int tx_pwr;
    atomic_t rds_tx_support; //FM RDS Tx support flag, 1: support, 0: not support
    atomic_t rds_tx_enable; //FM RDS Tx enable state, 1:enable RDS Tx function,  0:disable 
    atomic_t over_bt_enable; //FM over BT enable state, 1:enable,  0:disable
    uint16_t chip_id;
    uint16_t device_id;
    dev_t dev_t;
    uint16_t min_freq; // KHz
    uint16_t max_freq; // KHz
    uint8_t band;      // TODO
    struct class *cls;
    struct device *dev;
    struct cdev cdev;
    wait_queue_head_t read_wait;
    volatile bool RDS_Data_ready;
    RDSData_Struct *pstRDSData; 
    struct rds_group_cnt rds_gc;
    struct workqueue_struct *fm_workqueue;    /* fm rx  handling */
    struct workqueue_struct *fm_timer_workqueue;    /* fm rds reset handling */
    struct work_struct fm_rds_reset_work;
    struct work_struct fm_tx_power_ctrl_work;
    struct work_struct fm_tx_rtc_ctrl_work;
    struct work_struct fm_tx_desense_wifi_work;
    struct work_struct fm_rst_recover_work;
    struct work_struct fm_subsys_recover_work;
    struct work_struct fm_rx_work;
};

struct fm_rx_cxt{
    int audioPath; //I2S or Analog
    int vol;
    int mute;
    int rdsOn;
    int ana; //long/short antenna
    int ViaBt; //FM over bt controller
};

struct fm_tx_cxt{
    int audioPath; //I2S or Analog
    int rdsTxOn;
    uint16_t pi;
    uint16_t ps[12]; // 4 ps
    uint16_t other_rds[87];  // 0~29 other groups
    uint8_t other_rds_cnt; // # of other group
};

struct fm_context{
    volatile int ref;
    volatile bool chipon;
    fm_pwr_status powerup;
    volatile int16_t freq;
    struct fm_rx_cxt rxcxt;
    struct fm_tx_cxt txcxt;
};

struct fm_config{
    struct proc_dir_entry *proc;
    uint32_t vcoon;
    uint32_t vcooff;
    uint32_t timer;
    uint32_t txpwrctl;
    uint32_t rdsrst;
};

typedef struct fm_timer_sys{
    volatile uint32_t    count;
    volatile uint8_t     onoff; 
    volatile uint8_t     rds_reset_en;
    volatile uint8_t     tx_pwr_ctrl_en;
    volatile uint8_t     tx_rtc_ctrl_en;
    volatile uint8_t     tx_desense_en;
}fm_timer_sys;

enum{
    FM_TIMER_SYS_OFF,
    FM_TIMER_SYS_ON,
    FM_TIMER_SYS_MAX
};

enum{
    FM_RDS_RST_DISABLE,
    FM_RDS_RST_ENABLE,
    FM_RDS_RST_MAX
};

enum{
    FM_TX_PWR_CTRL_DISABLE,
    FM_TX_PWR_CTRL_ENABLE,
    FM_TX_PWR_CTRL_MAX
};

enum{
    FM_TX_RTC_CTRL_DISABLE,
    FM_TX_RTC_CTRL_ENABLE,
    FM_TX_RTC_CTRL_MAX
};

enum{
    FM_TX_DESENSE_DISABLE,
    FM_TX_DESENSE_ENABLE,
    FM_TX_DESENSE_MAX
};

enum{
    FM_WHOLECHIP_RST_OFF,
    FM_WHOLECHIP_RST_START,
    FM_WHOLECHIP_RST_END,
    FM_WHOLECHIP_RST_MAX
};

enum{
    FM_SUBSYS_RST_OFF,
    FM_SUBSYS_RST_START,
    FM_SUBSYS_RST_END,
    FM_SUBSYS_RST_MAX
};

#define FM_TIMER_TIMEOUT_DEFAULT 1000
#define FM_TIMER_TIMEOUT_MIN 1000
#define FM_TIMER_TIMEOUT_MAX 1000000

#define FM_RDS_RST_INVAL_DEFAULT 5
#define FM_RDS_RST_INVAL_MIN 5
#define FM_RDS_RST_INVAL_MAX 10000

#define FM_TX_PWR_CTRL_INVAL_DEFAULT 10
#define FM_TX_PWR_CTRL_INVAL_MIN 5
#define FM_TX_PWR_CTRL_INVAL_MAX 10000

#define FM_TX_VCO_OFF_DEFAULT 5
#define FM_TX_VCO_OFF_MIN 1
#define FM_TX_VCO_OFF_MAX 10000

#define FM_TX_VCO_ON_DEFAULT 100
#define FM_TX_VCO_ON_MIN 10
#define FM_TX_VCO_ON_MAX 10000

#define FM_TX_RTC_CTRL_INTERVAL 10

#define FM_GPS_RTC_AGE_TH       2
#define FM_GPS_RTC_DRIFT_TH     0
#define FM_GPS_RTC_TIME_DIFF_TH 10
#define FM_GPS_RTC_RETRY_CNT    1
#define FM_GPS_RTC_DRIFT_MAX 5000
enum{
    FM_GPS_RTC_INFO_OLD = 0,
    FM_GPS_RTC_INFO_NEW = 1,
    FM_GPS_RTC_INFO_MAX
};
// MUST sync with FM f/w definitions (eg. sys_fm_6620.h)
/* FM opcode */
#define FM_STP_TEST_OPCODE      (0x00)
#define FSPI_ENABLE_OPCODE      (0x01)
#define FSPI_MUX_SEL_OPCODE     (0x02)
#define FSPI_READ_OPCODE        (0x03)
#define FSPI_WRITE_OPCODE       (0x04)
#define FI2C_READ_OPCODE        (0x05)
#define FI2C_WRITE_OPCODE       (0x06)
#define FM_ENABLE_OPCODE        (0x07)
#define FM_RESET_OPCODE         (0x08)
#define FM_TUNE_OPCODE          (0x09)
#define FM_SEEK_OPCODE          (0x0a)
#define FM_SCAN_OPCODE          (0x0b)
#define RDS_RX_ENABLE_OPCODE    (0x0c)
#define RDS_RX_DATA_OPCODE      (0x0d)
#define FM_RAMPDOWN_OPCODE      (0x0e)
#define FM_MCUCLK_SEL_OPCODE    (0x0f)
#define FM_MODEMCLK_SEL_OPCODE  (0x10)
#define RDS_TX_OPCODE           (0x11)
#define FM_MAX_OPCODE           (0x12)

#define FM_COM_CMD_BASE 0
typedef enum{
    FM_COM_CMD_TEST = FM_COM_CMD_BASE + 0,
    FM_COM_CMD_SEEK,
    FM_COM_CMD_SCAN,
    FM_COM_CMD_MAX
}fm_com_cmd_t;

#define SW_RETRY_CNT            (2)
#define SW_RETRY_CNT_MAX        (5)
#define SW_WAIT_TIMEOUT_MAX     (100)
typedef enum 
{
    ERR_SUCCESS = 1000,
    ERR_INVALID_BUF,
    ERR_INVALID_PARA,
    ERR_STP,
    ERR_GET_MUTEX,
    ERR_FW_NORES,
    ERR_RDS_CRC,
    ERR_WHOLECHIP_RST,
    ERR_SUBSYS_RST,
    ERR_MAX    
}fm_drv_error_t;

// FM operation timeout define for error handle
#define TEST_TIMEOUT            (3)
#define FSPI_EN_TIMEOUT         (3)
#define FSPI_MUXSEL_TIMEOUT     (3)
#define FSPI_RD_TIMEOUT         (3)
#define FSPI_WR_TIMEOUT         (3)
#define I2C_RD_TIMEOUT          (3)
#define I2C_WR_TIMEOUT          (3)
#define EN_TIMEOUT              (5)
#define RST_TIMEOUT             (3)
#define TUNE_TIMEOUT            (3)
#define SEEK_TIMEOUT            (10)
#define SCAN_TIMEOUT            (15) //usualy scan will cost 10 seconds 
#define RDS_RX_EN_TIMEOUT       (3)
#define RDS_DATA_TIMEOUT        (100)
#define RAMPDOWN_TIMEOUT        (3)
#define MCUCLK_TIMEOUT          (3)
#define MODEMCLK_TIMEOUT        (3)
#define RDS_TX_TIMEOUT          (3)
#define POWERON_TIMEOUT         (3)
#define POWEROFF_TIMEOUT        (3)
#define SEEK_DONE_TIMEOUT       (3)
#define SCAN_DONE_TIMEOUT       (3)


/* FM basic-operation's opcode */
#define FM_BOP_BASE             (0x80)
#define FM_WRITE_BASIC_OP       (FM_BOP_BASE + 0x00)
#define FM_UDELAY_BASIC_OP      (FM_BOP_BASE + 0x01)
#define FM_RD_UNTIL_BASIC_OP    (FM_BOP_BASE + 0x02)
#define FM_MODIFY_BASIC_OP      (FM_BOP_BASE + 0x03)
#define FM_MSLEEP_BASIC_OP      (FM_BOP_BASE + 0x04)
#define FM_MAX_BASIC_OP         (FM_BOP_BASE + 0x05)

/* FM BOP's size */
#define FM_WRITE_BASIC_OP_SIZE      (3)
#define FM_UDELAY_BASIC_OP_SIZE     (4)
#define FM_RD_UNTIL_BASIC_OP_SIZE   (5)
#define FM_MODIFY_BASIC_OP_SIZE     (5)
#define FM_MSLEEP_BASIC_OP_SIZE     (4)

#define RX_BUF_SIZE 128
#define TX_BUF_SIZE 1024

typedef enum
{
    FM_TASK_RX_PARSER_PKT_TYPE = 0,
    FM_TASK_RX_PARSER_OPCODE,
    FM_TASK_RX_PARSER_PKT_LEN_1,
    FM_TASK_RX_PARSER_PKT_LEN_2,
    FM_TASK_RX_PARSER_PKT_PAYLOAD,
    FM_TASK_RX_PARSER_BUFFER_CONGESTION
} fm_task_parser_state;

#define FM_ADDR_RSSI    (0xE8)	//D9~D0 10bits
#define FM_ADDR_PAMD    (0xE9)	//D7~D0 8bits
#define FM_ADDR_MR    	(0xF2)	//D8~D0 9bits

#define FM_TXSCAN_RSSI_TH	(-250)
#define FM_TXSCAN_PAMD_TH	(-20)
#define FM_TXSCAN_MR_TH		(-38)

#define SCAN_UP 	(0)
#define SCAN_DOWN 	(1)

#define MT6620_SCANTBL_SIZE  16 //16*uinit16_t

#define FM_TASK_COMMAND_PKT_TYPE    0x01
#define FM_TASK_EVENT_PKT_TYPE      0x04

#define MT6620_FM_SEEK_UP       0x0
#define MT6620_FM_SEEK_DOWN     0x01
#define MT6620_FM_SCAN_UP       0x0
#define MT6620_FM_SCAN_DOWN     0x01
#define MT6620_FM_SPACE_INVALID 0x0
#define MT6620_FM_SPACE_50K     0x01
#define MT6620_FM_SPACE_100K    0x02
#define MT6620_FM_SPACE_200K    0x04 

#define FLAG_TEST           (1 << 0)
#define FLAG_FSPI_EN        (1 << 1)
#define FLAG_FSPI_MUXSEL    (1 << 2)
#define FLAG_FSPI_RD        (1 << 3)
#define FLAG_FSPI_WR        (1 << 4)
#define FLAG_I2C_RD         (1 << 5)
#define FLAG_I2C_WR         (1 << 6)
#define FLAG_EN             (1 << 7)
#define FLAG_RST            (1 << 8)
#define FLAG_TUNE           (1 << 9)
#define FLAG_SEEK           (1 << 10)
#define FLAG_SCAN           (1 << 11)
#define FLAG_RDS_RX_EN      (1 << 12)
#define FLAG_RDS_DATA       (1 << 13)
#define FLAG_RAMPDOWN       (1 << 14)
#define FLAG_MCUCLK         (1 << 15)
#define FLAG_MODEMCLK       (1 << 16)
#define FLAG_RDS_TX         (1 << 17)
#define FLAG_OP_MAX         (1 << 18)
#define FLAG_POWERON        (1 << 19)
#define FLAG_POWEROFF       (1 << 20)
#define FLAG_SEEK_DONE      (1 << 21)
#define FLAG_SCAN_DONE      (1 << 22)
#define FLAG_TERMINATE      (1 << 31)

#endif //__MT6620_FM_H__
