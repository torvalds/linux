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

#include <linux/sched.h>

#ifdef FM_TASK_INFO_DEBUG
#include <asm-generic/current.h>
#endif

#include <linux/dcache.h>
#include <linux/string.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h> // udelay()
#include <linux/device.h> // device_create()
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>

#include <asm/uaccess.h> // get_user()

#include "fm.h"
//#include <mach/mt_bt.h>
#ifdef MT6573
#include <mach/mt6573_boot.h>
#endif
//#include <mach/mt_combo.h>

#include "stp_exp.h"
#include "wmt_exp.h"

#include "mt6620_fm.h"
#include "mt6620_fm_lib.h"
#include "../private/mtk_fm.h"
#include "mt6620_fm_reg.h"

#ifdef CTP_CHECK_FM
extern void tpd_get_fm_frequency(int16_t freq);
#endif

#define FM_VOL_MAX             0x2B // 43 volume(0-15)

#define FM_DEV_MAJOR    193
static int FM_major = FM_DEV_MAJOR;       /* dynamic allocation */

#define FM_PROC_FILE "fm"
static struct proc_dir_entry *g_fm_proc = NULL;

#define FM_PROC_CONFIG "fmconfig"
static struct fm_config g_fm_config;

uint32_t g_dbg_level = 0xfffffff5;  // Debug level of FM

static struct fm *fm_cb = NULL;    //fm main data structure
struct fm_priv priv;    //fm priv data, and will be linked to fm main data structure

volatile int16_t _current_frequency = 0xFFFF;
static unsigned char *tx_buf = NULL;

volatile uint16_t seek_result = 0;
uint16_t scan_result[MT6620_SCANTBL_SIZE] = {0};
struct rds_rx rds_rx_result;
volatile uint16_t rds_rx_size = 0;
static volatile uint32_t flag = 0;
static spinlock_t flag_lock;

static volatile uint16_t fspi_rd = 0;

static struct fm_gps_rtc_info gps_rtc_info;

extern uint32_t gBLER_CHK_INTERVAL;
extern uint16_t GOOD_BLK_CNT;
extern uint16_t BAD_BLK_CNT;
extern uint8_t  BAD_BLK_RATIO;

static struct timer_list fm_timer;
static struct fm_timer_sys timer_sys;

static volatile int chip_rst_state = FM_WHOLECHIP_RST_OFF;
static volatile int subsys_rst_state = FM_SUBSYS_RST_OFF;
static struct fm_context *fm_cxt;

#ifndef DECLARE_MUTEX
#define DECLARE_MUTEX(name) DEFINE_SEMAPHORE(name)
#endif
static DECLARE_WAIT_QUEUE_HEAD(fm_wq);    
static DECLARE_MUTEX(fm_ops_mutex);  //protect FM ops: open, close, read, ioctl
static DECLARE_MUTEX(fm_read_mutex); //protect RDS good/bad block global val
static DECLARE_MUTEX(fm_rds_mutex); //protec RDS ps/rt ...
static DECLARE_MUTEX(fm_timer_mutex); //protec FM  timer 
static DECLARE_MUTEX(fm_cmd_mutex); //protect FM HW access 
static DECLARE_MUTEX(fm_rtc_mutex); //protect FM GPS RTC drift info  
static DECLARE_MUTEX(fm_rxtx_mutex); //protect FM RX TX mode switch 

/******************************************************************************
 * FUNCTION PROTOTYPES
 *****************************************************************************/
//schedule func 
void stp_rx_event_cb(void);  //called by stp driver
static void fm_timer_func(unsigned long data);  //called by timer timeout

//worker func
static void fm_rx_worker_func(struct work_struct *work); //scheduled by stp_rx_event_cb
static void fm_rds_reset_worker_func(struct work_struct *work); //scheduled by fm_timer_func
static void fm_tx_rtc_ctrl_worker_func(struct work_struct *work); //
static void fm_tx_power_ctrl_worker_func(struct work_struct *work);
static void fm_tx_desense_wifi_worker_func(struct work_struct *work);
static void fm_rst_recover_worker_func(struct work_struct *work);
static void fm_subsys_recover_worker_func(struct work_struct *work);

//normal func
static int fm_enable_rds_BlerCheck(struct fm *fm);
static int fm_disable_rds_BlerCheck(void);

static int fm_hw_reset(void);
static int fm_recover_func(struct fm *fm, struct fm_context *cxt);

static int fm_tx_scan(struct fm *fm, struct fm_tx_scan_parm *parm);
int fm_send_wait_timeout(unsigned char* tx_buf, 
                                uint16_t pkt_size, 
                                int flag_mask,
                                int retry_cnt,
                                int timeout);

static int MT6620_TxScan_SetFreq(uint16_t freq);
static int MT6620_TxScan_GetCQI(int16_t *pRSSI, int16_t *pPAMD, int16_t *pMR);
static int MT6620_TxScan_IsEmptyChannel(int16_t RSSI, int16_t PAMD, int16_t MR, int *empty);
static int MT6620_TxScan(uint16_t min_freq, uint16_t max_freq, uint16_t *pFreq, uint16_t *pScanTBL, uint16_t *ScanTBLsize, uint16_t scandir, uint16_t space);
extern int MT6620_RDS_BlerCheck(struct fm *fm);
extern int MT6620_RDS_Eint_Handler(struct fm *fm, struct rds_rx *rds_raw, int rds_size);
extern int MT6620_RDS_OnOff(struct fm *fm, bool bFlag);
extern int rds_group_counter_get(struct rds_group_cnt *dst, struct rds_group_cnt *src);
extern int rds_group_counter_reset(struct rds_group_cnt *gc);

int  Delayms(uint32_t data);
int  Delayus(uint32_t data);
int   MT6620_read(uint8_t addr, uint16_t *val);
int   MT6620_write(uint8_t addr, uint16_t val);
int   MT6620_set_bits(uint8_t addr, uint16_t bits, uint16_t mask);
static int MT6620_Mute(bool mute);
static int MT6620_RampDown(void);
static int MT6620_RampDown_Tx(void);
static int MT6620_SetFreq(uint16_t freq); // functionality tune
static int MT6620_Fast_SetFreq(uint16_t freq);
static int MT6620_SetFreq_Tx(uint16_t freq); // functionality tune
static int MT6620_Seek(
                        uint16_t min_freq, uint16_t max_freq,
                        uint16_t *pFreq,
                        uint16_t seekdir,
                        uint16_t space);
static int MT6620_Scan(
                        uint16_t min_freq, uint16_t max_freq,
                        uint16_t *pFreq, //get the valid freq after scan
                        uint16_t *pScanTBL,
                        uint16_t *ScanTBLsize,
                        uint16_t scandir,
                        uint16_t space);
static int MT6620_ScanForceStop(void);
static int MT6620_GetCurRSSI(int *pRSSI);
static int MT6620_SetVol(uint8_t vol);
static int MT6620_GetVol(uint8_t *pVol);
#ifdef FMDEBUG
static int MT6620_dump_reg(void);
#endif
static int MT6620_GetMonoStereo(uint16_t *pMonoStereo);
static int MT6620_SetMonoStereo(int MonoStereo);
static int MT6620_GetCapArray(int *caparray);
static int MT6620_GetCurPamd(uint16_t *pPamdLevl);
static int MT6620_em_test(uint16_t group_idx, uint16_t item_idx, uint32_t item_value);
static int MT6620_Rds_Tx_Enable(uint8_t enable);
static int MT6620_Rds_Tx(uint16_t pi, uint16_t *ps, uint16_t *other_rds, uint8_t other_rds_cnt);
static int MT6620_I2S_Setting(int onoff, int mode, int sample);
static int MT6620_FMOverBT(int enable);
static int MT6620_ANA_SWITCH(int antenna);

static int  fm_setup_cdev(struct fm *fm);
static long  fm_ops_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static loff_t fm_ops_lseek(struct file *filp, loff_t off, int whence);
static ssize_t fm_ops_read(struct file *filp, char *buf, size_t len, loff_t *off);
static int  fm_ops_open(struct inode *inode, struct file *filp);
static int  fm_ops_release(struct inode *inode, struct file *filp);

static int fm_get_process_path(struct fm *fm);
static int fm_get_gps_rtc_info(struct fm_gps_rtc_info *dst, struct fm_gps_rtc_info *src);
static int fm_rtc_compensation_init(struct fm *fm, struct fm_gps_rtc_info *rtc);
static int fm_timer_init(struct fm *fm, struct fm_timer_sys *fmtimer, struct timer_list *timer);

static int  fm_init(void);
static int  fm_destroy(struct fm *fm);
static int  fm_powerup(struct fm *fm, struct fm_tune_parm *parm);
static int  fm_antenna_switch(struct fm *fm, int antenna);
static int fm_powerup_tx(struct fm *fm, struct fm_tune_parm *parm);
static int  fm_powerdown(struct fm *fm, int type);
static int  fm_tune(struct fm *fm, struct fm_tune_parm *parm);
static int  fm_tune_tx(struct fm *fm, struct fm_tune_parm *parm);
static int  fm_seek(struct fm *fm, struct fm_seek_parm *parm);
static int  fm_scan(struct fm *fm, struct fm_scan_parm *parm);
static int  fm_get_rssi_after_scan(struct fm *fm, struct fm_rssi_req *req);
static int  fm_get_hw_info(struct fm *pfm, struct fm_hw_info *req);
static int  fm_setvol(struct fm *fm, uint32_t vol);
static int  fm_getvol(struct fm *fm, uint32_t *vol);
static int  fm_getrssi(struct fm *fm, int *rssi);
static int fm_rds_tx(struct fm *fm, struct fm_rds_tx_parm *parm);
static int fm_over_bt(struct fm *fm, int enable);
static int fm_proc_read(char *page, char **start, off_t off, 
    int count, int *eof, void *data);
static int fm_proc_write(struct file *file, const char *buffer, 
    unsigned long count, void *data);
static int fmconfig_proc_read(char *page, char **start, off_t off, 
    int count, int *eof, void *data);
static int fmconfig_proc_write(struct file *file, const char *buffer, 
    unsigned long count, void *data);

static struct file_operations fm_ops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = fm_ops_ioctl,
    .llseek = fm_ops_lseek,
    .read = fm_ops_read,
    .open = fm_ops_open,
    .release = fm_ops_release,
};

/******************************************************************************
 *****************************************************************************/

/*
 *  delay ms
 */
int Delayms(uint32_t data)
{
    msleep(data);
    FM_LOG_DBG(D_CMD,"delay %dms\n", data);
    return 0;
}

/*
 *  delay us
 */
int Delayus(uint32_t data)
{
    udelay(data);
    FM_LOG_DBG(D_CMD,"delay %dus\n", data);
    return 0;
}

/***********************************************************
Function: 	fm_send_wait_timeout()

Description: 	send cmd to FM firmware and wait event 

Para: 		tx_buf--->send buffer 
			pkt_size--->the length of cmd
			flag_mask--->the event flag mask
			retry_cnt--->the retry conter
			timeout--->timeout per cmd
			
Return: 		0, if success; error code, if failed
***********************************************************/
int fm_send_wait_timeout(unsigned char* tx_buf, 
                                uint16_t pkt_size, 
                                int flag_mask,
                                int retry_cnt,
                                int timeout)
{
    int ret_val = 0;
    int ret_time = 0;

    if((NULL == tx_buf) || (pkt_size < 0) || (0 == flag_mask) 
        || (retry_cnt > SW_RETRY_CNT_MAX) || (timeout > SW_WAIT_TIMEOUT_MAX)){
        FM_LOG_ERR(D_ALL,"%s():invalid para\n", __func__);
        ret_val = -ERR_INVALID_PARA;
        goto out;
    }
     FM_LOG_DBG(D_MAIN,"0,flag_mask=0x%08x\n", flag_mask);
sw_retry:
     //FM_LOG_DBG(D_MAIN,"0,flag_mask=0x%08x\n", flag_mask);
    if(mtk_wcn_stp_send_data(tx_buf, pkt_size, FM_TASK_INDX) == 0){
        FM_LOG_ERR(D_MAIN,"%s():send data over stp failed\n", __func__);
        ret_val = -ERR_STP; //stp error is the most serious error, wo can do nothing but info APP
        goto out;
    }else{
        //FM_LOG_DBG(D_MAIN,"1,flag_mask=0x%08x\n", flag_mask);
        ret_time = wait_event_timeout(fm_wq, ((flag & flag_mask) == flag_mask), timeout*HZ);
        FM_LOG_DBG(D_MAIN,"[flag=0x%08x]\n", flag);
        if(!ret_time){
           if(0 < retry_cnt--){
              FM_LOG_WAR(D_MAIN,"%s():wait even timeout, [retry_cnt=%d]\n", __func__, retry_cnt);
              goto sw_retry;
           }else{
              FM_LOG_ERR(D_MAIN,"%s():fatal error, SW retry failed, reset HW\n", __func__);
              ret_val = -ERR_FW_NORES; //return fatal error
              goto out;
           }
        }
        FM_LOG_DBG(D_MAIN,"2,flag_mask=0x%08x\n", flag_mask);
        spin_lock(&flag_lock);
        flag &= ~flag_mask;
        spin_unlock(&flag_lock);
    }
out:
    return ret_val;
}

/*
 *  MT6620_read
 */
int MT6620_read(uint8_t addr, uint16_t *val)
{
    int ret_val = 0;
    uint16_t pkt_size;

    FM_LOG_DBG(D_CMD,"+%s(): [addr=0x%02x]\n", __func__, addr);
    FM_LOG_INF(D_CMD,"-fm_cmd_mutex\n");
    if (down_interruptible(&fm_cmd_mutex)){
        FM_LOG_ERR(D_CMD|D_ALL, "%s(): get mutex failed\n", __func__);
        ret_val = -ERR_GET_MUTEX;
        goto out1;
    }
    
	if(tx_buf == NULL){
        FM_LOG_ERR(D_CMD|D_ALL, "%s(): invalid tx_buf\n", __func__);
        ret_val = -ERR_INVALID_BUF;
		goto out;
	}
	memset(tx_buf, 0, TX_BUF_SIZE);

    pkt_size = mt6620_get_reg(tx_buf, TX_BUF_SIZE, addr);
    ret_val = fm_send_wait_timeout(tx_buf, pkt_size, 
                                    FLAG_FSPI_RD, SW_RETRY_CNT, FSPI_RD_TIMEOUT);
    if(!ret_val){
         *val = fspi_rd;
    }
out:
    up(&fm_cmd_mutex);
    FM_LOG_INF(D_CMD,"+fm_cmd_mutex\n");
    
out1:
    FM_LOG_DBG(D_CMD,"-%s(): [val=0x%02x]\n", __func__, fspi_rd);
    return ret_val;
}

/*
 *  MT6620_write
 */
int MT6620_write(uint8_t addr, uint16_t val)
{
    int ret_val = 0;
    uint16_t pkt_size;

    FM_LOG_DBG(D_CMD,"+%s(): [addr=0x%02x]\n", __func__, addr);
    FM_LOG_INF(D_CMD,"-fm_cmd_mutex\n");
    if (down_interruptible(&fm_cmd_mutex)){
        FM_LOG_ERR(D_CMD|D_ALL,"%s(): get mutex failed\n", __func__);
        ret_val = -ERR_GET_MUTEX;
        goto out1;
    }
    
	if(tx_buf == NULL){
        FM_LOG_ERR(D_CMD|D_ALL,"%s(): invalid tx_buf\n", __func__);
        ret_val = -ERR_INVALID_BUF;
		goto out;
	}
	memset(tx_buf, 0, TX_BUF_SIZE); 
    pkt_size = mt6620_set_reg(tx_buf, TX_BUF_SIZE, addr, val);
    ret_val = fm_send_wait_timeout(tx_buf, pkt_size, 
                                    FLAG_FSPI_WR, SW_RETRY_CNT, FSPI_WR_TIMEOUT);
out:
    up(&fm_cmd_mutex); 
    FM_LOG_INF(D_CMD,"+fm_cmd_mutex\n");
    
out1:
    FM_LOG_DBG(D_CMD,"-%s(): [addr=0x%02x]\n", __func__, addr);
    return ret_val;
}

int MT6620_set_bits(uint8_t addr, uint16_t bits, uint16_t mask)
{
    int ret = 0;
    uint16_t val;

    FM_LOG_DBG(D_CMD,"+%s(): [addr=0x%02x],[bits=0x%04x],[mask=0x%04x]\n", __func__, addr, bits, mask);
    ret = MT6620_read(addr, &val);
    if (ret)
        goto out;

    val = ((val & (mask)) | bits);

    ret = MT6620_write(addr, val);
    
out:
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

/*
 * MT6620_Mute          
 * 
 * 1.Mute/Unmute audio, enable/disable rgf mute
 * 2.Record mute status for FM recover context 
 */
static int MT6620_Mute(bool mute)
{
    int ret = 0;
    
    FM_LOG_DBG(D_CMD,"+%s():[mute=%d]\n", __func__, mute);
    fm_cxt->rxcxt.mute = mute;

    if(mute){
        ret = MT6620_set_bits(0x9C, 0x0008, 0xFFF7); //1:9C D3 = 1
    }else{
        ret = MT6620_set_bits(0x9c, 0x0000, 0xFFF7); //1:9C D3 = 0
    }

    FM_LOG_NTC(D_CMD,"%s\n", mute ? "mute" : "unmute");
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

/*
 * MT6620_RampDown          
 * 
 * 1.Mute audio, enable rgf mute
 * 2.Set softmute to fast mode
 * 3.Clear all hw control requests
 *
 * Before return, we need check STC_DONE status flag (not the interrupt flag!) 
 * to ensure the sounds has now fully muted.
 */
static int MT6620_RampDown()
{
    int ret = 0;
    uint16_t pkt_size = 0;

    FM_LOG_DBG(D_CMD,"+%s():\n", __func__);
    FM_LOG_INF(D_CMD,"-fm_cmd_mutex\n");
    if (down_interruptible(&fm_cmd_mutex)){
        FM_LOG_ERR(D_CMD|D_ALL,"%s(): get mutex failed\n", __func__);
        ret = -ERR_GET_MUTEX;
        goto out1;
    }
    
	if(tx_buf == NULL){
        FM_LOG_ERR(D_CMD|D_ALL,"%s(): invalid tx_buf\n", __func__);
        ret = -ERR_INVALID_BUF;
		goto out;
	}
	memset(tx_buf, 0, TX_BUF_SIZE);

    pkt_size = mt6620_rampdown(tx_buf, TX_BUF_SIZE);
    ret = fm_send_wait_timeout(tx_buf, pkt_size, 
                                    FLAG_RAMPDOWN, SW_RETRY_CNT, RAMPDOWN_TIMEOUT);
out:
    up(&fm_cmd_mutex);
    FM_LOG_INF(D_CMD,"+fm_cmd_mutex\n");
        
out1: 
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

static int MT6620_RampDown_Tx()
{
    int ret = 0;
    uint16_t pkt_size = 0;

    FM_LOG_DBG(D_CMD,"+%s():\n", __func__);
    FM_LOG_INF(D_CMD,"-fm_cmd_mutex\n");
    if (down_interruptible(&fm_cmd_mutex)){
        FM_LOG_ERR(D_CMD|D_ALL,"%s(): get mutex failed\n", __func__);
        ret = -ERR_GET_MUTEX;
        goto out1;
    }
    
	if(tx_buf == NULL){
        FM_LOG_ERR(D_CMD|D_ALL,"%s(): invalid tx_buf\n", __func__);
        ret = -ERR_INVALID_BUF;
		goto out;
	}
	memset(tx_buf, 0, TX_BUF_SIZE);    
    pkt_size = mt6620_rampdown_tx(tx_buf, TX_BUF_SIZE);
    ret = fm_send_wait_timeout(tx_buf, pkt_size, 
                                    FLAG_RAMPDOWN, SW_RETRY_CNT, RAMPDOWN_TIMEOUT);
    
out:
    up(&fm_cmd_mutex);
    FM_LOG_INF(D_CMD,"+fm_cmd_mutex\n");
            
out1:
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

static int MT6620_SetFreq(uint16_t freq)
{
    int ret = 0;
    uint16_t pkt_size = 0;

    FM_LOG_DBG(D_CMD,"+%s():[freq=%d]\n", __func__, freq);
    FM_LOG_INF(D_CMD,"-fm_cmd_mutex\n");
    if (down_interruptible(&fm_cmd_mutex)){
        FM_LOG_ERR(D_CMD|D_ALL,"%s(): get mutex failed\n", __func__);
        ret = -ERR_GET_MUTEX;
        goto out1;
    }
    
	if(tx_buf == NULL){
        FM_LOG_ERR(D_CMD|D_ALL,"%s(): invalid tx_buf\n", __func__);
        ret = -ERR_INVALID_BUF;
		goto out;
	}
	memset(tx_buf, 0, TX_BUF_SIZE);

    pkt_size = mt6620_tune_1(tx_buf, TX_BUF_SIZE, freq);
    ret = fm_send_wait_timeout(tx_buf, pkt_size, 
                                    FLAG_TUNE, SW_RETRY_CNT, TUNE_TIMEOUT);
    Delayms(200);
    pkt_size = mt6620_tune_2(tx_buf, TX_BUF_SIZE, freq);
    ret = fm_send_wait_timeout(tx_buf, pkt_size, 
                                    FLAG_TUNE, SW_RETRY_CNT, TUNE_TIMEOUT);
    Delayms(35);
    pkt_size = mt6620_tune_3(tx_buf, TX_BUF_SIZE, freq);
    ret = fm_send_wait_timeout(tx_buf, pkt_size, 
                                    FLAG_TUNE, SW_RETRY_CNT, TUNE_TIMEOUT);
    _current_frequency = freq;
    FM_LOG_NTC(D_CMD,"tune[freq=%d]\n", freq);
#ifdef CTP_CHECK_FM
    tpd_get_fm_frequency(_current_frequency);
#endif
    
out:
    up(&fm_cmd_mutex);
    FM_LOG_INF(D_CMD,"+fm_cmd_mutex\n");
        
out1:
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

static int MT6620_Fast_SetFreq(uint16_t freq)
{
    int ret = 0;
    uint16_t pkt_size = 0;

    if (down_interruptible(&fm_cmd_mutex)){
        FM_LOG_ERR(D_CMD, "%s(): get mutex failed\n", __func__);
        return -ERR_GET_MUTEX;
    }
    
	if(!tx_buf){
        FM_LOG_ERR(D_CMD, "%s():invalid tx_buf\n", __func__);
        ret = -ERR_INVALID_BUF;
		goto release_mutex;
	}
	memset(tx_buf, 0, TX_BUF_SIZE);

    pkt_size = mt6620_fast_tune(tx_buf, TX_BUF_SIZE, freq);
    ret = fm_send_wait_timeout(tx_buf, pkt_size, FLAG_TUNE, SW_RETRY_CNT, TUNE_TIMEOUT); 
    
release_mutex:
    up(&fm_cmd_mutex);
    return ret;
}

static int MT6620_SetFreq_Tx(uint16_t freq)
{
    int ret = 0;
    uint16_t pkt_size = 0;

    FM_LOG_DBG(D_CMD,"+%s():[freq=%d]\n", __func__, freq);
    FM_LOG_INF(D_CMD,"-fm_cmd_mutex\n");
    if (down_interruptible(&fm_cmd_mutex)){
        FM_LOG_ERR(D_CMD|D_ALL,"%s(): get mutex failed\n", __func__);
        ret = -ERR_GET_MUTEX;
        goto out1;
    }
    
	if(tx_buf == NULL){
        FM_LOG_ERR(D_CMD|D_ALL,"%s(): invalid tx_buf\n", __func__);
        ret = -ERR_INVALID_BUF;
		goto out;
	}
	memset(tx_buf, 0, TX_BUF_SIZE);

    pkt_size = mt6620_tune_tx(tx_buf, TX_BUF_SIZE, freq);
    ret = fm_send_wait_timeout(tx_buf, pkt_size, 
                                    FLAG_TUNE, SW_RETRY_CNT, TUNE_TIMEOUT);
       
  	Delayms(125);
    _current_frequency = freq;
    
out:
    up(&fm_cmd_mutex);
    FM_LOG_INF(D_CMD,"+fm_cmd_mutex\n");
        
out1:
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

/*
* MT6620_Seek
* pFreq: IN/OUT parm, IN start freq/OUT seek valid freq
* return true:seek success; false:seek failed
*/
static int MT6620_Seek(uint16_t min_freq, uint16_t max_freq, uint16_t *pFreq, uint16_t seekdir, uint16_t space)
{
    int ret = 0;
    uint16_t pkt_size = 0;
    uint16_t startfreq = *pFreq;

    FM_LOG_DBG(D_CMD,"+%s():[startfreq=%d]\n", __func__, startfreq);

    if((ret = MT6620_RampDown())){
        FM_LOG_ERR(D_CMD|D_ALL,"%s():RampDown failed\n", __func__);
        goto out1;
    }

    FM_LOG_INF(D_CMD,"-fm_cmd_mutex\n");
    if (down_interruptible(&fm_cmd_mutex)){
        FM_LOG_ERR(D_CMD|D_ALL,"%s(): get mutex failed\n", __func__);
        ret = -ERR_GET_MUTEX;
        goto out1;
    }
    
	if(tx_buf == NULL){
        FM_LOG_ERR(D_CMD|D_ALL,"%s(): invalid tx_buf\n", __func__);
        ret = -ERR_INVALID_BUF;
		goto out;
	}
	memset(tx_buf, 0, TX_BUF_SIZE);
    
    pkt_size = mt6620_seek_1(tx_buf, TX_BUF_SIZE, seekdir, space, max_freq, min_freq);
    ret = fm_send_wait_timeout(tx_buf, pkt_size, 
                                    FLAG_SEEK | FLAG_SEEK_DONE, SW_RETRY_CNT, SEEK_TIMEOUT);
    if(!ret){
        *pFreq = seek_result;
    _current_frequency = seek_result;
    FM_LOG_NTC(D_CMD,"seek[freq=%d]\n", seek_result);
        Delayms(35);
#ifdef CTP_CHECK_FM
        tpd_get_fm_frequency(_current_frequency);
#endif
    }else{
        FM_LOG_WAR(D_CMD,"Rx Seek Failed\n");
    }
    pkt_size = mt6620_seek_2(tx_buf, TX_BUF_SIZE, seekdir, space, max_freq, min_freq);
    ret = fm_send_wait_timeout(tx_buf, pkt_size, 
                                    FLAG_SEEK, SW_RETRY_CNT, SEEK_TIMEOUT);
out:
    up(&fm_cmd_mutex);
    FM_LOG_INF(D_CMD,"+fm_cmd_mutex\n");
        
out1:
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

static int MT6620_TxScan_SetFreq(uint16_t freq)
{
    int ret = 0;
    uint16_t pkt_size = 0;

    FM_LOG_DBG(D_CMD,"+%s():[freq=%d]\n", __func__, freq);
    FM_LOG_INF(D_CMD,"-fm_cmd_mutex\n");
    if (down_interruptible(&fm_cmd_mutex)){
        FM_LOG_ERR(D_CMD|D_ALL,"%s(): get mutex failed\n", __func__);
        ret = -ERR_GET_MUTEX;
        goto out1;
    }
    
	if(tx_buf == NULL){
        FM_LOG_ERR(D_CMD|D_ALL,"%s(): invalid tx_buf\n", __func__);
        ret = -ERR_INVALID_BUF;
		goto out;
	}
	memset(tx_buf, 0, TX_BUF_SIZE);

    pkt_size = mt6620_tune_txscan(tx_buf, TX_BUF_SIZE, freq);
    ret = fm_send_wait_timeout(tx_buf, pkt_size, 
                                        FLAG_TUNE, SW_RETRY_CNT, TUNE_TIMEOUT);
    
  	//Delayms(125);
    _current_frequency = freq;
out:
    up(&fm_cmd_mutex);
    FM_LOG_INF(D_CMD,"+fm_cmd_mutex\n");
        
out1:    
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

static int MT6620_TxScan_GetCQI(int16_t *pRSSI, int16_t *pPAMD, int16_t *pMR)
{
    int cnt = 0;
    int ret = 0;
	int16_t tmp_reg = 0;
	int16_t aRSSI = 0;
	int16_t aPAMD = 0;
	int16_t aMR = 0;
	
    FM_LOG_DBG(D_CMD,"+%s():\n", __func__);

	if((pRSSI == NULL) || (pPAMD == NULL) || (pMR == NULL)){
        FM_LOG_ERR(D_CMD|D_ALL,"%s():para error, [pRSSI=%p],[aPAMD=%p],[pMR=%p]\n", 
            __func__, pRSSI, pPAMD, pMR);
        ret = -ERR_INVALID_PARA;
		goto out;
	}
	
	for(cnt = 0; cnt < 8; cnt++){
		Delayms(3);
		if((ret = MT6620_read(FM_ADDR_RSSI, &tmp_reg)))
            goto out;
		tmp_reg = tmp_reg&0x03ff;
		tmp_reg = (tmp_reg > 511) ? ((int16_t)(tmp_reg-1024)) : tmp_reg;
		aRSSI += tmp_reg;
		
		if((ret = MT6620_read(FM_ADDR_PAMD, &tmp_reg)))
            goto out;
		tmp_reg = tmp_reg&0x00ff;
		tmp_reg = (tmp_reg > 127) ? ((int16_t)(tmp_reg-256)) : tmp_reg;
		aPAMD += tmp_reg;
		
		if((ret = MT6620_read(FM_ADDR_MR, &tmp_reg)))
            goto out;
		tmp_reg = tmp_reg&0x01ff;
		tmp_reg = (tmp_reg > 255) ? ((int16_t)(tmp_reg-512)) : tmp_reg;
		aMR += tmp_reg;
	}

	*pRSSI = aRSSI>>3;
	*pPAMD = aPAMD>>3;
	*pMR = aMR>>3;

    FM_LOG_DBG(D_CMD,"%s():[RSSI=%d],[PAMD=%d],[MR=%d]\n", 
            __func__, *pRSSI, *pPAMD, *pMR);
    
out:
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

static int MT6620_TxScan_IsEmptyChannel(int16_t RSSI, int16_t PAMD, int16_t MR, int *empty)
{
    int ret = 0;
    
    FM_LOG_DBG(D_CMD,"+%s():[RSSI=%d],[PAMD=%d],[MR=%d]\n", __func__, RSSI, PAMD, MR);

    if(empty == NULL){
        FM_LOG_DBG(D_CMD,"invalid pointer [empty=0x%x]!\n", (unsigned int)empty);
        ret = -ERR_INVALID_PARA;
        goto out;
    }

    *empty = TRUE; 
	if(RSSI > FM_TXSCAN_RSSI_TH){
	    *empty = FALSE;
        goto out;
	}

	if(PAMD < FM_TXSCAN_PAMD_TH){
		*empty = FALSE;
        goto out;
	}

	if(MR < FM_TXSCAN_MR_TH){
		*empty = FALSE;
        goto out;
	}

out:
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

static int MT6620_TxScan(
                        uint16_t min_freq, uint16_t max_freq,
                        uint16_t *pFreq,
                        uint16_t *pScanTBL,
                        uint16_t *ScanTBLsize,
                        uint16_t scandir,
                        uint16_t space)
{
	int ret = 0;
 	uint16_t freq = *pFreq;
	uint16_t scan_cnt = *ScanTBLsize;
	uint16_t cnt = 0; 
	int16_t rssi = 0;
	int16_t pamd = 0;
	int16_t mr = 0;
	int counter = 0;
    int empty = -1;

    FM_LOG_DBG(D_CMD,"+%s():\n", __func__);
    
    if((!pScanTBL) || (*ScanTBLsize < TX_SCAN_MIN) || (*ScanTBLsize >  TX_SCAN_MAX)){
        FM_LOG_ERR(D_CMD|D_ALL,"+%s():invalid scan table\n", __func__);
        ret = -ERR_INVALID_PARA;
        goto out;
    }

    FM_LOG_DBG(D_CMD, "[freq=%d], [max_freq=%d],[min_freq=%d],[scan BTL size=%d],[scandir=%d],[space=%d]\n", 
			*pFreq, max_freq, min_freq, *ScanTBLsize, scandir, space);

	cnt = 0;
	while(!(!(cnt < scan_cnt) || (freq > max_freq) || (freq < min_freq))){
		//MT6620_RampDown();
		//Set desired channel, tune to the channel, and perform fast AGC
		counter++; //just for debug
		
		ret = MT6620_TxScan_SetFreq(freq);
		if(ret){
            FM_LOG_ERR(D_CMD|D_ALL,"%s():set freq failed\n", __func__);
			goto out;
		}
		
		//wait 8~10ms for RF setting
		Delayms(10);
		//wait 4 AAGC period for AAGC setting, AAGC period = 1024/480k = 2.13ms
		Delayms(9);
		
		ret = MT6620_TxScan_GetCQI(&rssi, &pamd, &mr);
		if(ret){
            FM_LOG_ERR(D_CMD|D_ALL,"%s():get CQI failed\n", __func__);
			goto out;
		}

		ret = MT6620_TxScan_IsEmptyChannel(rssi, pamd, mr, &empty);
		if(!ret){
            if((empty == TRUE) && ((freq < FMTX_SCAN_HOLE_LOW) || (freq > FMTX_SCAN_HOLE_HIGH))){
                *(pScanTBL + cnt) = freq; //strore the valid empty channel 
			    cnt++;
                FM_LOG_NTC(D_CMD|D_ALL,"empty channel:[freq=%d] [cnt=%d]\n", freq, cnt);
            }			
		}else{ 
		    FM_LOG_ERR(D_CMD|D_ALL,"%s():IsEmptyChannel failed\n", __func__);
			goto out;
        }

		if(scandir == SCAN_UP){
            if(freq == FMTX_SCAN_HOLE_LOW){
                freq += (FMTX_SCAN_HOLE_HIGH - FMTX_SCAN_HOLE_LOW + 1);
            }else{
			freq += space;
            }
		}else if(scandir == SCAN_DOWN){
		    if(freq == FMTX_SCAN_HOLE_HIGH){
                freq -= (FMTX_SCAN_HOLE_HIGH - FMTX_SCAN_HOLE_LOW + 1);
            }else{
			freq -= space;
            }
		}else{
            FM_LOG_ERR(D_CMD|D_ALL,"%s():scandir para error\n", __func__);
			ret = -ERR_INVALID_PARA;
            goto out;
		}
	}

	*ScanTBLsize = cnt;
	*pFreq = *(pScanTBL + cnt);
	FM_LOG_DBG(D_CMD, "completed, [cnt=%d],[freq=%d],[counter=%d]\n", cnt, freq, counter);
    FM_LOG_INF(D_CMD, "--time end[%d]\n", jiffies_to_msecs(jiffies));
    
out:
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

/*
 * MT6620_Scan          
 * 
 * Steps:
 * 1.We should mute FM, and clear all hw control requests, so do Ramp_Down
 * 2.Set scan direction, channel spacing
 *   (If need, set scan lower and upper Freq before step 3)
 * 3.Enable hardware controlled scanning sequence
 * 4.Check STC_DONE interrupt status flag, then clear the STC_DONE interrupt status flag
 * 5.Read back channel scan bit_map information
  *  (IF need sorting by RSSI jump to Get RSSI after scan page)
 * 6.Disable mute, Set softmute to normal mode.
 */
static int MT6620_Scan(
                        uint16_t min_freq, uint16_t max_freq,
                        uint16_t *pFreq,
                        uint16_t *pScanTBL,
                        uint16_t *ScanTBLsize,
                        uint16_t scandir,
                        uint16_t space)
{
    int ret = 0;
    uint16_t pkt_size = 0;
    uint16_t offset = 0;
    uint16_t tmp_scanTBLsize = *ScanTBLsize;
    
    FM_LOG_DBG(D_CMD,"+%s():\n", __func__);

    if((!pScanTBL) || (tmp_scanTBLsize < MT6620_SCANTBL_SIZE)){
        FM_LOG_ERR(D_ALL,"+%s():invalid scan table\n", __func__);
        ret = -ERR_INVALID_PARA;
        goto out1;
    }

    FM_LOG_INF(D_CMD, "[freq=%d], [max_freq=%d],[min_freq=%d],[scan BTL size=%d],[scandir=%d],[space=%d]\n", 
                *pFreq, max_freq, min_freq, *ScanTBLsize, scandir, space);
    if(tmp_scanTBLsize > MT6620_SCANTBL_SIZE){
        tmp_scanTBLsize = MT6620_SCANTBL_SIZE;
    }

    MT6620_RampDown();

    //Clear "FLAG_SCAN" & "FLAG_SCAN_DONE" to make sure we do make a new scan
    spin_lock(&flag_lock);
    flag &= ~(FLAG_SCAN | FLAG_SCAN_DONE);
    spin_unlock(&flag_lock);
    FM_LOG_INF(D_CMD,"-fm_cmd_mutex\n");
    if (down_interruptible(&fm_cmd_mutex)){
        FM_LOG_ERR(D_CMD|D_ALL,"%s(): get mutex failed\n", __func__);
        ret = -ERR_GET_MUTEX;
        goto out1;
    }
    
	if(tx_buf == NULL){
        FM_LOG_ERR(D_CMD|D_ALL,"%s(): invalid tx_buf\n", __func__);
        ret = -ERR_INVALID_BUF;
		goto out;
	}
	memset(tx_buf, 0, TX_BUF_SIZE);
    
    pkt_size = mt6620_scan_1(tx_buf, TX_BUF_SIZE, scandir, space, max_freq, min_freq);
    ret = fm_send_wait_timeout(tx_buf, pkt_size, 
                                    FLAG_SCAN | FLAG_SCAN_DONE, SW_RETRY_CNT, SCAN_TIMEOUT);
    if(!ret){
        memcpy(pScanTBL, scan_result, sizeof(uint16_t)*MT6620_SCANTBL_SIZE);
        FM_LOG_INF(D_CMD,"Rx Scan Result:\n");
        for(offset = 0; offset < tmp_scanTBLsize; offset++){
            FM_LOG_INF(D_CMD,"%d: %04x\n", (int)offset, *(pScanTBL+offset));
        }
        *ScanTBLsize = tmp_scanTBLsize;
        Delayms(35);
    }else{
        FM_LOG_WAR(D_CMD,"Rx Scan Failed\n");
    }
    pkt_size = mt6620_scan_2(tx_buf, TX_BUF_SIZE, scandir, space, max_freq, min_freq);
    ret = fm_send_wait_timeout(tx_buf, pkt_size, 
                                    FLAG_SCAN, SW_RETRY_CNT, SCAN_TIMEOUT);
out:
    up(&fm_cmd_mutex);
    FM_LOG_INF(D_CMD,"+fm_cmd_mutex\n");
        
out1:  
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

static int MT6620_ScanForceStop()
{
    int ret = 0;
    
    FM_LOG_DBG(D_CMD,"+%s():\n", __func__);
    
    spin_lock(&flag_lock);
    flag |= FLAG_SCAN_DONE;
    spin_unlock(&flag_lock);
    memset(scan_result, 0, sizeof(uint16_t) * MT6620_SCANTBL_SIZE);
    FM_LOG_DBG(D_CMD,"[flag=0x%08x]\n", flag);
    wake_up(&fm_wq);

    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

static int MT6620_GetCurRSSI(int *pRSSI)
{
    uint16_t tmp_reg;
    int ret = 0;
    
    FM_LOG_DBG(D_CMD,"+%s():\n", __func__);

    if((ret = MT6620_read(FM_ADDR_RSSI, &tmp_reg)))
        goto out;
    FM_LOG_DBG(D_CMD,"[addr=0x%02x],[val=0x%04x]\n",\
			FM_ADDR_RSSI, tmp_reg);

    tmp_reg = tmp_reg&0x03ff;
    //RS=RSSI
    //If RS>511, then RSSI(dBm)= (RS-1024)/16*6
    //                 else RSSI(dBm)= RS/16*6
    // dBm
    if (pRSSI){
        *pRSSI = (tmp_reg>511) ? (((tmp_reg-1024)*6)>>4):((tmp_reg*6)>>4);
    }
    FM_LOG_DBG(D_CMD,"[RSSI=%d]\n", *pRSSI);
out:
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

static int MT6620_SetVol(uint8_t vol)
{
    int ret = 0;
    uint8_t tmp_vol = vol&0x3f;

    FM_LOG_DBG(D_CMD,"+%s():\n", __func__);
    
    if(tmp_vol > FM_VOL_MAX)
        tmp_vol = FM_VOL_MAX;

    if((ret = MT6620_set_bits(0x9C, (tmp_vol<<8), 0xC0FF)))
        goto out;
    FM_LOG_DBG(D_CMD,"[vol=%d]\n", tmp_vol);

out:
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

static int MT6620_GetVol(uint8_t *pVol)
{
    uint16_t tmp_reg;
    int ret = 0;

    FM_LOG_DBG(D_CMD,"+%s():\n", __func__);
    
    if((ret = MT6620_read(0x9C, &tmp_reg)))
        goto out;

    *pVol = (tmp_reg>>8) & 0x3f;
    FM_LOG_DBG(D_CMD,"[vol=%d]\n", *pVol);

out:
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}


/***********************************************************
Function: 	MT6620_I2S_Setting()

Description: 	set the I2S state on MT6620

Para: 		onoff--->I2S on/off
			mode--->I2S mode: Master or Slave
			
Return: 		0, if success; error code, if failed
***********************************************************/
static int MT6620_I2S_Setting(int onoff, int mode, int sample)
{
    uint16_t tmp_state = 0;
    uint16_t tmp_mode = 0;
    uint16_t tmp_sample = 0;
    int ret = 0;

    FM_LOG_DBG(D_CMD,"+%s():\n", __func__);
    
    if(onoff == FM_I2S_ON){
        tmp_state = 0x01; //I2S Frequency tracking on
    }else if(onoff == FM_I2S_OFF){
        tmp_state = 0x00; //I2S Frequency tracking off
    }else{
        FM_LOG_ERR(D_CMD|D_ALL,"%s():[onoff=%d]\n", __func__, onoff);
        ret = -ERR_INVALID_PARA;
        goto out;
    }

    if(mode == FM_I2S_MASTER){
        tmp_mode = 0x03; //6620 as I2S master
    }else if(mode == FM_I2S_SLAVE){
        tmp_mode = 0x0B; //6620 as I2S slave
    }else{
        FM_LOG_ERR(D_CMD|D_ALL,"%s():[mode=%d]\n", __func__, mode);
        ret = -ERR_INVALID_PARA;
        goto out;
    }

    if(sample == FM_I2S_32K){
        tmp_sample = 0x0000; //6620 I2S 32KHz sample rate
    }else if(sample == FM_I2S_44K){
        tmp_sample = 0x0800; //6620 I2S 44.1KHz sample rate
    }else if(sample == FM_I2S_48K){
        tmp_sample = 0x1000; //6620 I2S 48KHz sample rate
    }else{
        FM_LOG_ERR(D_CMD|D_ALL,"%s():[sample=%d]\n", __func__, sample);
        ret = -ERR_INVALID_PARA;
        goto out;
    }
    if((ret = MT6620_set_bits(0x5F, tmp_sample, 0xE7FF)))
        goto out;
    if((ret = MT6620_write(0x9B, tmp_mode)))
        goto out;
    if((ret = MT6620_write(0x56, tmp_state)))
        goto out;
    
    FM_LOG_NTC(D_CMD,"[onoff=%s][mode=%s][sample=%d](0)33KHz,(1)44.1KHz,(2)48KHz\n", 
        (onoff == FM_I2S_ON) ? "On" : "Off", 
        (mode == FM_I2S_MASTER) ? "Master" : "Slave", 
        sample);

out:
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

static int MT6620_FMOverBT(int enable)
{
    int ret = 0;
    static uint16_t state = 0;
    static uint16_t mode = 0;
    static uint16_t sample = 0;
    static uint16_t inited = false;

    FM_LOG_DBG(D_CMD,"+%s():\n", __func__);
    if(inited == false){
        // record priv val
        if((ret = MT6620_read(0x56, &state)))
            goto out;
        if((ret = MT6620_read(0x9B, &mode)))
            goto out;
        if((ret = MT6620_read(0x5F, &sample)))
            goto out;
        inited = true;
        FM_LOG_NTC(D_MAIN,"init, record priv seetings\n");
    }
    
    if(enable == TRUE){
        //disable analog output when FM over BT
        if((ret = MT6620_set_bits(0x3A, 0, MASK(2))))
            goto out;
        //set FM over BT
        if((ret = MT6620_write(0x56, 0x0001)))
            goto out;
        if((ret = MT6620_write(0x9B, 0x000b)))
            goto out;
        if((ret = MT6620_write(0x5F, 0x1175)))
            goto out;
        FM_LOG_NTC(D_MAIN,"set FM via BT controller\n");
    }else if(enable == FALSE){
        //enable analog output when FM normal mode
        if((ret = MT6620_set_bits(0x3A, BITn(2), MASK(2))))
            goto out;
        //recover to priv val
        if((ret = MT6620_write(0x56, state)))
            goto out;
        if((ret = MT6620_write(0x9B, mode)))
            goto out;
        if((ret = MT6620_write(0x5F, sample)))
            goto out;
        FM_LOG_NTC(D_MAIN,"set FM via Host\n");
    }else{
        FM_LOG_ERR(D_CMD|D_ALL,"%s()\n", __func__);
        ret = -ERR_INVALID_PARA;
        goto out;
    }
out:
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret; 
}

static int MT6620_ANA_SWITCH(int antenna)
{
	int ret = 0;

	FM_LOG_DBG(D_CMD,"+%s():\n", __func__);
    FM_LOG_NTC(D_CMD,"Switch to %s atenna\n", (antenna == FM_LONG_ANA) ? "Long" : "Short");
	if(antenna == FM_LONG_ANA){
        //Long Antenna RSSI threshold, 0xE0 D0~D9
        if((ret = MT6620_write(0xE0, ((0xA301 & 0xFC00) | (FMR_RSSI_TH_LONG & 0x03FF)))))
            goto out;
        //Turn on Short Antenna LNA and Off TR Switch
        if((ret = MT6620_write(0x04, 0x0142)))
            goto out;
		//Turn off the Short Antenna Capbank biasing
        if((ret = MT6620_write(0x05, 0x00E7)))
            goto out;
		//Turn off the Short Antenna Capbank biasing
        if((ret = MT6620_write(0x26, 0x0004)))
            goto out;
		//Disable concurrent calibration for VCO and SCAL
		if((ret = MT6620_write(0x2E, 0x0008)))
            goto out;
    }else if(antenna == FM_SHORT_ANA){
        //Short Antenna RSSI threshold, 0xE0 D0~D9
        if((ret = MT6620_write(0xE0, ((0xA2E0 & 0xFC00) | (FMR_RSSI_TH_SHORT & 0x03FF)))))
            goto out;
        //Turn on Short Antenna LNA and TR Switch
        if((ret = MT6620_write(0x04, 0x0145)))
            goto out;
		//Turn on the Short Antenna Capbank biasing
        if((ret = MT6620_write(0x05, 0x00FF)))
            goto out;
		//Turn on the Short Antenna Capbank biasing
        if((ret = MT6620_write(0x26, 0x0024)))
            goto out;
		//Enable concurrent calibration for VCO and SCAL
		if((ret = MT6620_write(0x2E, 0x0000)))
            goto out;
    }else{
        FM_LOG_ERR(D_CMD|D_ALL,"%s()\n", __func__);
        ret = -ERR_INVALID_PARA;
        goto out;
    }
out:
	FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
	return ret;
}

#ifdef FMDEBUG
static int MT6620_dump_reg(void)
{
    int i;
    int ret = 0;
    uint16_t val;

    FM_LOG_DBG(D_CMD,"+%s():\n", __func__);

    if((ret = MT6620_write(FM_MAIN_PGSEL, 0x0001)))
        goto out;
    for (i = 0; i < MT6620_SCANTBL_SIZE; i++){
        val = 0;
        ret = MT6620_read(FM_MAIN_BITMAP0+i, &val);
        if (ret == 0){
            FM_LOG_DBG(D_CMD,"%2d\t%04x\n", i, val);
        }else{
            FM_LOG_DBG(D_CMD|D_ALL,"%2d\tXXXX\n", i);
            goto out;
        }
    }
    ret = MT6620_write(FM_MAIN_PGSEL, 0x0);

out:
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}
#endif // FMDEBUG

static int MT6620_GetMonoStereo(uint16_t *pMonoStereo)
{
    uint16_t tmp_reg;
    int ret = 0;
    
    FM_LOG_DBG(D_CMD,"+%s():\n", __func__);

    if((ret = MT6620_write(FM_MAIN_PGSEL, 0x01)))
        goto out;

    if((ret = MT6620_read(0xF8, &tmp_reg)))
        goto out;
    
    if((ret = MT6620_write(FM_MAIN_PGSEL, 0x0)))
        goto out;
    
    tmp_reg = (tmp_reg&0x400)>>10;
    if(!tmp_reg){
        tmp_reg = 1;
    }else{
        tmp_reg = 0;
    }
    *pMonoStereo = tmp_reg;//0:stereo, 1:mono
    FM_LOG_DBG(D_CMD,"[MonoStereo=%d]\n", *pMonoStereo);
    
out:
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

/*
 * MT6620_SetMonoStereo
 * Force set to stero/mono mode
 * @MonoStereo -- 0, auto; 1, force mono
 * If success, return 0; else error code
 */
static int MT6620_SetMonoStereo(int MonoStereo)
{
    uint16_t tmp_reg;
    int ret = 0;
    
    if((ret = MT6620_write(FM_MAIN_PGSEL, FM_PG1)))
        goto out;

    tmp_reg = MonoStereo ? BITn(1) : 0; //MonoStereo, 1: force mono; 0:auto
    if((ret = MT6620_set_bits(FM_STEROMONO_CTR, tmp_reg, MASK(1))))//set E0 D1=0
                goto out;
    
    if((ret = MT6620_write(FM_MAIN_PGSEL, FM_PG0)))
        goto out;
    
    FM_LOG_DBG(D_CMD,"set to %s\n", MonoStereo ? "auto" : "force mono");
    
out:
    return ret;
}

static int MT6620_GetCapArray(int *caparray)
{
    uint16_t tmp_reg;
    int ret = 0;
    
    FM_LOG_DBG(D_CMD,"+%s():\n", __func__);

    if((ret = MT6620_read(0x26, &tmp_reg)))
        goto out;
	
    *caparray = (int)tmp_reg;
    FM_LOG_DBG(D_CMD,"[caparray=%d]\n", *caparray);
    
out:
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

static int MT6620_GetCurPamd(uint16_t *pPamdLevl)
{
    uint16_t tmp_reg;
    int ret = 0;
    
    FM_LOG_DBG(D_CMD,"+%s():\n", __func__);
    if((ret = MT6620_read(0xE9, &tmp_reg)))
        goto out;
    tmp_reg &= 0x00FF;
    /*PA=PAMD
    *If PA>511 then PAMD(dB)=  (PA-1024)/16*6,
    *               else PAMD(dB)=PA/16*6
    */
    *pPamdLevl = (tmp_reg>127) ? ((256-tmp_reg)*6/16):0;
    
out:
    FM_LOG_DBG(D_CMD,"[PamdLevl=%d]\n", (int)*pPamdLevl);
    return ret;
}

static int MT6620_em_test(uint16_t group_idx, uint16_t item_idx, uint32_t item_value)
{
    int ret = 0;
    
    FM_LOG_DBG(D_CMD,"+%s():[group_idx=%d],[item_idx=%d],[item_value=%d]\n", 
        __func__, group_idx, item_idx, item_value);
    
	switch (group_idx){
		case mono:
            if((ret = MT6620_write(FM_MAIN_PGSEL, 0x01)))
                goto out;

			if(item_value == 1){
			    if((ret = MT6620_set_bits(0xE0, 0x02, 0xFFFF)))
                    goto out;
			}else{
			    if((ret = MT6620_set_bits(0xE0, 0x0, 0xFFFD)))
                    goto out;
			}
            if((ret = MT6620_write(FM_MAIN_PGSEL, 0x0)))
                goto out;
			break;
		case stereo:
            if((ret = MT6620_write(FM_MAIN_PGSEL, 0x01)))
                goto out;
			if(item_value == 0){
			    if((ret = MT6620_set_bits(0xE0, 0x0, 0xFFFD)))
                    goto out;
			}else{
				switch (item_idx){
					case Sblend_ON:
					    if((ret = MT6620_set_bits(0xD8, item_idx<<15, 0x7FFF)))
                            goto out;
					    break;
					case Sblend_OFF:
					    if((ret = MT6620_set_bits(0xD8, 0, 0x7FFF)))
                            goto out;
					    break;
				}
			}
            if((ret = MT6620_write(FM_MAIN_PGSEL, 0x0)))
                goto out;
			break;
		case RSSI_threshold:
		    if((ret = MT6620_set_bits(0xe0, item_value, 0xFC00)))
                goto out;
		    break;
		case HCC_Enable:
            if((ret = MT6620_write(FM_MAIN_PGSEL, 0x01)))
                goto out;
			if(item_idx){
				if((ret = MT6620_set_bits(0xCF, 0x10, 0xFFFF)))
                    goto out;
			}else{
			    if((ret = MT6620_set_bits(0xCF, 0x0, 0xFFEF)))
                    goto out;
			}
            if((ret = MT6620_write(FM_MAIN_PGSEL, 0x0)))
                goto out;
		    break;
		case PAMD_threshold:
		    if((ret = MT6620_set_bits(0xE1, item_value, 0xFF00)))
                goto out;
		    break;
		case Softmute_Enable:
            if((ret = MT6620_write(FM_MAIN_PGSEL, 0x01)))
                goto out;
			if(item_idx){
				if((ret = MT6620_set_bits(0xCF, 0x0020, 0xFFFF))) //1:CF[5] = 1
                    goto out; 
			}else{
			    if((ret = MT6620_set_bits(0xCF, 0x0000, 0xFFDF))) //1:CF[5] = 0
			        goto out; 
			}
            if((ret = MT6620_write(FM_MAIN_PGSEL, 0x0)))
                goto out;
		    break;
		case De_emphasis:
            if((ret = MT6620_write(FM_MAIN_PGSEL, 0x01)))
                goto out;
			if(item_idx == 2){
			    if((ret = MT6620_set_bits(0xd4, 0x2000, 0xCFFF)))
                    goto out;
			}else if(item_idx == 1){
			    if((ret = MT6620_set_bits(0xd4, 0x1000, 0xCFFF)))
                    goto out;
			}else if(item_idx == 0){
			    if((ret = MT6620_set_bits(0xd4, 0x0000, 0xCFFF)))
                    goto out;
			}
            if((ret = MT6620_write(FM_MAIN_PGSEL, 0x0)))
                goto out;
		    break;
		case HL_Side:
			if(item_idx == 2){
			     //H-Side
			    if((ret = MT6620_set_bits(0xCB, 0x11, 0xFFFE)))
                    goto out;
			    if((ret = MT6620_set_bits(0xF, 0x0400,  0xFBFF)))
                    goto out;
			}else if(item_idx == 1){
			     //L-Side
			    if((ret = MT6620_set_bits(0xCB, 0x10, 0xFFFE)))
                    goto out;
			    if((ret = MT6620_set_bits(0xF, 0x0,  0xFBFF)))
                    goto out;
			}
		    break;

		case Dynamic_Limiter:
		    if((ret = MT6620_write(FM_MAIN_PGSEL, 0x01)))
                goto out;
			if(item_idx){
			    if((ret = MT6620_set_bits(0xFA, 0x0, 0xFFF7)))
                    goto out;
			}else{
			    if((ret = MT6620_set_bits(0xFA, 0x08, 0xFFF7)))
                    goto out;
			}
			if((ret = MT6620_write(FM_MAIN_PGSEL, 0x0)))
                goto out;
		    break;

		case Softmute_Rate:
		    if((ret = MT6620_write(FM_MAIN_PGSEL, 0x01)))
                goto out;
		    if((ret = MT6620_set_bits(0xc8, item_value<<8, 0x80FF)))
                goto out;
			if((ret = MT6620_write(FM_MAIN_PGSEL, 0x0)))
                goto out;
		    break;

		case AFC_Enable:
		    if (item_idx){
		        if((ret = MT6620_set_bits(0x63, 0x0400, 0xFBFF)))
                    goto out;
		    }else{
		        if((ret = MT6620_set_bits(0x63, 0x0,    0xFBFF)))
                    goto out;
		    }
		    break;

#if 0
		case Softmute_Level:
		    MT6620_write(FM_MAIN_PGSEL, 0x01);
		    if(item_value > 0x24)
				item_value = 0x24;
		    MT6620_set_bits(0xD1, item_value, 0xFFC0);
		    MT6620_write(FM_MAIN_PGSEL, 0x0);
		    break;
#endif
		case Analog_Volume:
		    if((ret = MT6620_set_bits(0x9C, item_value<<8, 0xC0FF)))
                goto out;
		    break;

		default:
		    break;
	}

out:
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

static int MT6620_Rds_Tx_Enable(uint8_t enable)
{
    int ret = 0;
    
    FM_LOG_NTC(D_CMD,"+%s(): %s\n", __func__, enable ? "Enable" : "Disable");
    
    if(enable == 1){
        if((ret = MT6620_write(0x9F, 0x0000)))
            goto out;
        if((ret = MT6620_write(0xAB, 0x3872)))
            goto out;
        if((ret = MT6620_write(0xAC, 0x3B3A)))
            goto out;
        if((ret = MT6620_write(0xAD, 0x0113)))
            goto out;
        if((ret = MT6620_write(0xAE, 0x03B2)))
            goto out;
        if((ret = MT6620_write(0xAF, 0x0001)))
            goto out;
        if((ret = MT6620_write(0xB1, 0x63EB)))
            goto out;
        if((ret = MT6620_write(0xF4, 0x0020)))
            goto out;
        if((ret = MT6620_write(0xF5, 0x3222)))
            goto out;
    }else{
        if((ret = MT6620_write(0x9F, 0x0000)))
            goto out;
        if((ret = MT6620_write(0xAB, 0x39B6)))
            goto out;
        if((ret = MT6620_write(0xAC, 0x3C3E)))
            goto out;
        if((ret = MT6620_write(0xAD, 0x0000)))
            goto out;
        if((ret = MT6620_write(0xAE, 0x03C2)))
            goto out;
        if((ret = MT6620_write(0xAF, 0x0001)))
            goto out;
        if((ret = MT6620_write(0xF4, 0x0020)))
            goto out;
        if((ret = MT6620_write(0xF5, 0xBF16)))
            goto out;
        ret = MT6620_write(0xB1, 0x623D);
    }

out:
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

static int MT6620_Rds_Tx(uint16_t pi, uint16_t *ps, uint16_t *other_rds, uint8_t other_rds_cnt)
{
    int ret = 0;
    uint16_t pkt_size = 0;

    FM_LOG_DBG(D_CMD,"+%s():PI=0x%04x, PS=0x%04x/0x%04x/0x%04x, other_rds_cnt=%d \n", 
                                    __func__, pi, ps[0], ps[1], ps[2], other_rds_cnt);
    FM_LOG_INF(D_CMD,"-fm_cmd_mutex\n");
    if (down_interruptible(&fm_cmd_mutex)){
        FM_LOG_ERR(D_CMD|D_ALL,"%s():get mutex failed\n", __func__);
        ret = -ERR_GET_MUTEX;
        goto out1;
    }
    
	if(tx_buf == NULL){
        FM_LOG_ERR(D_CMD|D_ALL,"%s():invalid tx_buf\n", __func__);
        ret = -ERR_INVALID_BUF;
		goto out;
	}
	memset(tx_buf, 0, TX_BUF_SIZE);
    pkt_size = mt6620_rds_tx(tx_buf, TX_BUF_SIZE, pi, ps, other_rds, other_rds_cnt);
    ret = fm_send_wait_timeout(tx_buf, pkt_size, 
                                    FLAG_RDS_TX, SW_RETRY_CNT, RDS_TX_TIMEOUT);
    
out:
    up(&fm_cmd_mutex);
    FM_LOG_INF(D_CMD,"+fm_cmd_mutex\n");
        
out1: 
    FM_LOG_DBG(D_CMD,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

static int fm_enable_rds_BlerCheck(struct fm *fm)
{
    int ret = 0;
    ret = down_interruptible(&fm_timer_mutex);
    if(ret){
        FM_LOG_ERR(D_MAIN,"down fm_timer_mutex failed[ret=%d]\n", ret);
        goto out;
    }
    timer_sys.rds_reset_en = FM_RDS_RST_ENABLE;
    FM_LOG_DBG(D_TIMER,"enable RDS reset func\n");
    up(&fm_timer_mutex);
    
out:
    return ret;
}

static int fm_disable_rds_BlerCheck()
{
    int ret = 0;
    ret = down_interruptible(&fm_timer_mutex);
    if(ret){
        FM_LOG_ERR(D_MAIN,"down fm_timer_mutex failed[ret=%d]\n", ret);
        goto out;
    }
    timer_sys.rds_reset_en = FM_RDS_RST_DISABLE;
    FM_LOG_DBG(D_TIMER,"disable RDS reset func\n");
	up(&fm_timer_mutex);
    
out:
	return ret;
}

/*
************************************************************************************
Function:         fm_get_gps_rtc_info()

Description:     get GPS RTC drift info, and this function should not block

Date:              2011/04/10

Return Value:   success:0, failed: error coe
************************************************************************************
*/
static int fm_get_gps_rtc_info(struct fm_gps_rtc_info *dst, struct fm_gps_rtc_info *src)
{
    int ret = 0;
    int retry_cnt = 0;
    //struct fm *fm = (struct fm*)fm_cb;
    
    FMR_ASSERT(src);
    FMR_ASSERT(dst);

    if(src->retryCnt > 0){
        dst->retryCnt = src->retryCnt;
        FM_LOG_DBG(D_MAIN,"%s, new [retryCnt=%d]\n", __func__, dst->retryCnt);
    }
    if(src->ageThd > 0){
        dst->ageThd = src->ageThd;
        FM_LOG_DBG(D_MAIN,"%s, new [ageThd=%d]\n", __func__, dst->ageThd);
    }
    if(src->driftThd > 0){
        dst->driftThd = src->driftThd;
        FM_LOG_DBG(D_MAIN,"%s, new [driftThd=%d]\n", __func__, dst->driftThd);
    }
    if(src->tvThd.tv_sec > 0){
        dst->tvThd.tv_sec = src->tvThd.tv_sec;
        FM_LOG_DBG(D_MAIN,"%s, new [tvThd=%d]\n", __func__, (int)dst->tvThd.tv_sec);
    }
    
    while(down_trylock(&fm_rtc_mutex)){
        FM_LOG_WAR(D_MAIN,"down_trylock failed\n");
        if(++retry_cnt < dst->retryCnt){
            FM_LOG_WAR(D_MAIN,"[retryCnt=%d]\n", retry_cnt);
            msleep_interruptible(50); 
            continue;
        }else{
            FM_LOG_WAR(D_MAIN,"down_trylock retry failed\n");
            ret = -EFAULT;
            goto out;
        }    
    }

    dst->age = src->age;
    dst->drift = src->drift;
    dst->stamp = jiffies; //get curren time stamp
    dst->flag = FM_GPS_RTC_INFO_NEW;
    
    up(&fm_rtc_mutex);

   /*
    //send event to info fm_tx_rtc_ctrl_work
    if(timer_sys.tx_rtc_ctrl_en == FM_TX_RTC_CTRL_ENABLE){
        FM_LOG_DBG(D_TIMER,"fm_tx_rtc_ctrl_work, ticks:%d\n", jiffies_to_msecs(jiffies));
        queue_work(fm->fm_timer_workqueue, &fm->fm_tx_rtc_ctrl_work); 
    }
    */
    
out:
    return ret;
}
static int fm_hw_reset(void)
{
    int ret = 0;
    FM_LOG_DBG(D_MAIN,"+%s():\n", __func__);

    //check if we are resetting
    if(subsys_rst_state != FM_SUBSYS_RST_OFF){
        FM_LOG_NTC(D_MAIN, "FM subsys resetting is ongoing!!!\n");
        goto out;
    }
                
    //set rst state start
    subsys_rst_state = FM_SUBSYS_RST_START;

    //disable FM timer
    timer_sys.onoff = FM_TIMER_SYS_OFF;
    
    //wake up waiting wq, all flag set true, force all operations exit
    spin_lock(&flag_lock);
    flag = 0xFFFFFFFF;
    spin_unlock(&flag_lock);
    wake_up(&fm_wq);

    //FM subsys OFF
    if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_off(WMTDRV_TYPE_FM)){
        FM_LOG_ERR(D_MAIN,"WMT turn off FM fail!\n");
        ret = -ERR_STP;
    }else{
        FM_LOG_NTC(D_MAIN,"WMT turn off FM OK!\n");
    }
    
    //change FM pwr state
    fm_cb->powerup = FM_PWR_OFF;
    fm_cb->chipon = false;
    mtk_wcn_stp_register_event_cb(FM_TASK_INDX, NULL);
    
    //wake up recover work if need
    queue_work(fm_cb->fm_timer_workqueue, &fm_cb->fm_subsys_recover_work);
    
out:
    FM_LOG_DBG(D_MAIN,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}


//fm timer will timeout every  sencod
static void fm_timer_func(unsigned long data)
{
    int ret = 0;
    struct fm *fm = (struct fm*)fm_cb;
    struct fm_timer_sys *timer = &timer_sys;
    int vco_cycle = 1;

    if(!fm)
        goto out;
    if(!timer)
        goto out;
    //FM_LOG_DBG(D_TIMER,"+%s():\n", __func__);
    ret = down_interruptible(&fm_timer_mutex);
    if(ret){
        FM_LOG_ERR(D_MAIN,"down fm_timer_mutex failed[ret=%d]\n", ret);
        goto out;
    }
    if (timer->onoff == FM_TIMER_SYS_OFF){
        up(&fm_timer_mutex);
	    goto out;   
    }
    timer->count++; 

    //FM_LOG_DBG(D_TIMER,"+T:%d\n", jiffies_to_msecs(jiffies));
    //schedule RDS reset work if need
    if(g_fm_config.rdsrst < 1){
        FM_LOG_WAR(D_TIMER,"rds rst time err\n");
        g_fm_config.rdsrst = FM_RDS_RST_INVAL_MIN;
    }
    if((timer->rds_reset_en == FM_RDS_RST_ENABLE) && (timer->count%g_fm_config.rdsrst == 0)){
        FM_LOG_DBG(D_TIMER,"fm_rds_reset_work, ticks:%d\n", jiffies_to_msecs(jiffies));
        queue_work(fm->fm_timer_workqueue, &fm->fm_rds_reset_work); 
    }
    //schedule tx pwr ctrl work if need
    if(g_fm_config.txpwrctl < 1){
        FM_LOG_WAR(D_TIMER,"tx power ctl time err\n");
        g_fm_config.txpwrctl = FM_TX_PWR_CTRL_INVAL_MIN;
    }
    if((timer->tx_pwr_ctrl_en == FM_TX_PWR_CTRL_ENABLE) && (timer->count%g_fm_config.txpwrctl == 0)){
        FM_LOG_DBG(D_TIMER,"fm_tx_power_ctrl_work, ticks:%d\n", jiffies_to_msecs(jiffies));
        queue_work(fm->fm_timer_workqueue, &fm->fm_tx_power_ctrl_work); 
    }
    /*
    //schedule tx RTC ctrl work if need
    if((timer->tx_rtc_ctrl_en == FM_TX_RTC_CTRL_ENABLE)&& (timer->count%FM_TX_RTC_CTRL_INTERVAL == 0)){
        FM_LOG_DBG(D_TIMER,"fm_tx_rtc_ctrl_work, ticks:%d\n", jiffies_to_msecs(jiffies));
        queue_work(fm->fm_timer_workqueue, &fm->fm_tx_rtc_ctrl_work); 
    }*/
    //schedule tx desense with wifi/bt work if need
    if(g_fm_config.vcooff < 1){
        FM_LOG_WAR(D_TIMER,"vco tracking time err\n");
        g_fm_config.vcooff = FM_TX_VCO_OFF_MIN;
    }
    vco_cycle = g_fm_config.vcooff + g_fm_config.vcoon/1000;
    if((timer->tx_desense_en == FM_TX_DESENSE_ENABLE) && (timer->count%vco_cycle == 0)){
        FM_LOG_DBG(D_TIMER,"fm_tx_desense_wifi_work, ticks:%d\n", jiffies_to_msecs(jiffies));
        queue_work(fm->fm_timer_workqueue, &fm->fm_tx_desense_wifi_work); 
    }
    up(&fm_timer_mutex);
    
    //update timer    
    if(g_fm_config.timer < 1000){
        FM_LOG_WAR(D_TIMER,"timersys time err\n");
        g_fm_config.timer = FM_TIMER_TIMEOUT_MIN;
    }
	mod_timer(&fm_timer, jiffies + g_fm_config.timer/(1000/HZ)); 
    FM_LOG_DBG(D_TIMER,"-T:%d,mod timer\n", jiffies_to_msecs(jiffies));

out:
    //FM_LOG_DBG(D_TIMER,"-%s():[onoff=%d]\n", __func__, timer->onoff);
    return;
}
static void fm_tx_rtc_ctrl_worker_func(struct work_struct *work)
{
    int ret = 0;
    int ctrl = 0;
    struct fm_gps_rtc_info rtcInfo;
    //struct timeval curTime;
    //struct fm *fm = (struct fm*)fm_cb;
    unsigned long curTime = 0;

    FM_LOG_DBG(D_TIMER,"+%s():\n", __func__);

    ret = down_interruptible(&fm_rtc_mutex);
    if(ret){
        FM_LOG_ERR(D_MAIN,"down fm_rtc_mutex failed[ret=%d]\n", ret);
        goto out;
    }
    
    if(gps_rtc_info.flag == FM_GPS_RTC_INFO_NEW){
        memcpy(&rtcInfo, &gps_rtc_info, sizeof(struct fm_gps_rtc_info));
        gps_rtc_info.flag = FM_GPS_RTC_INFO_OLD;
        up(&fm_rtc_mutex);
    }else{
        FM_LOG_DBG(D_MAIN,"there's no new rtc drift info\n");
        up(&fm_rtc_mutex);
        goto out;
    }

    if(rtcInfo.age > rtcInfo.ageThd){
       FM_LOG_WAR(D_MAIN,"age over it's threshlod\n");
       goto out;
    }
    if((rtcInfo.drift <= rtcInfo.driftThd) && (rtcInfo.drift >= -rtcInfo.driftThd)){
       FM_LOG_WAR(D_MAIN,"drift over it's MIN threshlod\n");
       goto out;
    }

    if(rtcInfo.drift > FM_GPS_RTC_DRIFT_MAX){
        FM_LOG_WAR(D_MAIN,"drift over it's +MAX threshlod\n");
        rtcInfo.drift = FM_GPS_RTC_DRIFT_MAX;
        goto out;
    }else if(rtcInfo.drift < -FM_GPS_RTC_DRIFT_MAX){
        FM_LOG_WAR(D_MAIN,"drift over it's -MAX threshlod\n");
        rtcInfo.drift = -FM_GPS_RTC_DRIFT_MAX;
        goto out;
    }
    /*
    //get current time
    do_gettimeofday(&curTime);
    if((curTime.tv_sec - rtcInfo.tv.tv_sec) > rtcInfo.tvThd.tv_sec){
        FM_LOG_WAR(D_MAIN,"time diff over it's threshlod\n");
        goto out;
    }*/
    curTime = jiffies;
    if(((long)curTime - (long)rtcInfo.stamp)/HZ > rtcInfo.tvThd.tv_sec){
        FM_LOG_WAR(D_MAIN,"time diff over it's threshlod\n");
        goto out;
    }
    if(priv.state == INITED){
        FM_LOG_INF(D_TIMER,"%s, RTC_drift_ctrl[0x%08x]\n", __func__, (unsigned int)priv.priv_tbl.rtc_drift_ctrl);
        if(priv.priv_tbl.rtc_drift_ctrl != NULL){
            ctrl = rtcInfo.drift;
            if((ret = priv.priv_tbl.rtc_drift_ctrl(_current_frequency, &ctrl)))
                goto out;
        }
    }
out:
    FM_LOG_DBG(D_TIMER,"-%s():[T=%d]\n", __func__, jiffies_to_msecs(jiffies));
    return;
} 

static void fm_tx_power_ctrl_worker_func(struct work_struct *work)
{
    int ret = 0;
    int ctrl = 0;
    struct fm *fm = (struct fm*)fm_cb;

    FM_LOG_DBG(D_TIMER,"+%s():\n", __func__);
    
    if(down_interruptible(&fm_rxtx_mutex)){
        FM_LOG_ERR(D_TIMER, "%s(): get rx/tx mutex failed\n", __func__);
        return;
    }
    
    if(!fm){
        FM_LOG_ERR(D_TIMER,"err, [fm=0x%08x]\n", (int)fm);
        goto out;
    }

    if(fm->powerup != FM_PWR_TX_ON){
        FM_LOG_NTC(D_TIMER,"FM is not on TX mode\n");
        goto out;
    }
    
    if(priv.state == INITED){
        if(priv.priv_tbl.tx_pwr_ctrl != NULL){
            ctrl = fm->tx_pwr;
            FM_LOG_INF(D_TIMER,"tx pwr %ddb\n", ctrl);
            if((ret = priv.priv_tbl.tx_pwr_ctrl(_current_frequency, &ctrl)))
                goto out;
        }
    }
    
out:
    up(&fm_rxtx_mutex);
    FM_LOG_DBG(D_TIMER,"-%s():[T=%d]\n", __func__, jiffies_to_msecs(jiffies));
    return;
}

static void fm_tx_desense_wifi_worker_func(struct work_struct *work)
{
    int ret = 0;
    int ctrl = 0;
    struct fm *fm = (struct fm*)fm_cb;

    FM_LOG_DBG(D_TIMER,"+%s():\n", __func__);

    if(down_interruptible(&fm_rxtx_mutex)){
        FM_LOG_ERR(D_TIMER, "%s(): get rx/tx mutex failed\n", __func__);
        return;
    }
    
    if(!fm){
        FM_LOG_ERR(D_TIMER,"err, [fm=0x%08x]\n", (int)fm);
        goto out;
    }

    if(fm->powerup != FM_PWR_TX_ON){
        FM_LOG_NTC(D_TIMER,"FM is not on TX mode\n");
        goto out;
    }
    
    fm_tx_rtc_ctrl_worker_func(work);

    ctrl = g_fm_config.vcoon;
    if(priv.state == INITED){
        FM_LOG_INF(D_TIMER,"%s, tx_desense_wifi[0x%08x]\n", __func__, (unsigned int)priv.priv_tbl.tx_desense_wifi);
        if(priv.priv_tbl.tx_desense_wifi != NULL){
            if((ret = priv.priv_tbl.tx_desense_wifi(_current_frequency, &ctrl)))
                goto out;
        }
    }

out:
    up(&fm_rxtx_mutex);
    FM_LOG_DBG(D_TIMER,"-%s():[T=%d]\n", __func__, jiffies_to_msecs(jiffies));
    return;
} 

static void fm_rds_reset_worker_func(struct work_struct *work)
{
    struct fm *fm = fm_cb;
    int ret = 0;
    
    FM_LOG_DBG(D_TIMER,"+%s():[T=%d]\n", __func__, jiffies_to_msecs(jiffies));
    
    if(down_interruptible(&fm_rxtx_mutex)){
        FM_LOG_ERR(D_TIMER, "%s(): get rx/tx mutex failed\n", __func__);
        return;
    }
    
    if(!fm){
        FM_LOG_ERR(D_TIMER,"err, [fm=0x%08x]\n", (int)fm);
        goto out;
    }

    if(fm->powerup != FM_PWR_RX_ON){
        FM_LOG_NTC(D_TIMER,"FM is not on RX mode\n");
        goto out;
    }
    
    FM_LOG_INF(D_TIMER,"%s():-fm_read_mutex\n", __func__);
    if (down_interruptible(&fm_read_mutex)){
        FM_LOG_ERR(D_ALL,"fm_rds_reset_worker_func can't get mutex");
        goto out;      
    }
    
    ret = MT6620_RDS_BlerCheck(fm);
    if(-ERR_FW_NORES == ret){
        FM_LOG_ERR(D_TIMER|D_ALL,"fw no response, do hw reset\n");
        fm_hw_reset();
    }
    up(&fm_read_mutex); 
    FM_LOG_INF(D_TIMER,"%s():+fm_read_mutex\n", __func__);
    
out:
    up(&fm_rxtx_mutex);
    FM_LOG_DBG(D_TIMER,"-%s():[T=%d]\n", __func__, jiffies_to_msecs(jiffies));
    return;
}

void stp_rx_event_cb(void)
{
	struct fm *fm = fm_cb;
    static int cnt1 = 0;
	queue_work(fm->fm_workqueue, &fm->fm_rx_work); 
    FM_LOG_DBG(D_MAIN,"cnt1=%d\n", ++cnt1);
    FM_LOG_DBG(D_MAIN,"%s()\n", __func__);  
}

static void fm_wholechip_rst_cb(ENUM_WMTDRV_TYPE_T src, 
                                    ENUM_WMTDRV_TYPE_T dst, 
                                    ENUM_WMTMSG_TYPE_T type, 
                                    void *buf, 
                                    unsigned int sz)
{
    int ret = 0;
    //To handle reset procedure please
    ENUM_WMTRSTMSG_TYPE_T rst_msg;
    
    FM_LOG_DBG(D_MAIN,"+%s()\n", __func__);
    if(sz <= sizeof(ENUM_WMTRSTMSG_TYPE_T)){ 
        memcpy((char *)&rst_msg, (char *)buf, sz);
        FM_LOG_DBG(D_MAIN, "[src=%d], [dst=%d], [type=%d], [buf=0x%x], [sz=%d], [max=%d]\n", src, dst, type, rst_msg, sz, WMTRSTMSG_RESET_MAX);
        if((src==WMTDRV_TYPE_WMT) && (dst == WMTDRV_TYPE_FM) && (type == WMTMSG_TYPE_RESET)){                
            if(rst_msg == WMTRSTMSG_RESET_START){
                FM_LOG_NTC(D_MAIN, "FM restart start!\n");
                /*reset_start message handling*/
                //check if we are resetting
                if(chip_rst_state != FM_WHOLECHIP_RST_OFF){
                    FM_LOG_NTC(D_MAIN, "FM resetting is ongoing!!!\n");
                    return;
                }
                
                //set rst state start
                chip_rst_state = FM_WHOLECHIP_RST_START;

                //Record current context
                
                //disable FM timer
                ret = down_interruptible(&fm_timer_mutex);
                if(ret){
                    FM_LOG_WAR(D_MAIN, "down fm_timer_mutex failed!\n");
                    //FIX ME, how to handle this case?
                    return;
                }else{
                    timer_sys.onoff = FM_TIMER_SYS_OFF;
                    up(&fm_timer_mutex);
                }

                //wake up waiting wq, all flag set true, force all operations exit
                spin_lock(&flag_lock);
                flag = 0xFFFFFFFF;
                spin_unlock(&flag_lock);
                wake_up(&fm_wq);
                
                //change FM pwr state
                ret = down_interruptible(&fm_ops_mutex);
                if(ret){
                    FM_LOG_WAR(D_MAIN, "down fm_ops_mutex failed!\n");
                    //FIX ME, how to handle this case?
                    return;
                }else{
                    fm_cb->powerup = FM_PWR_OFF;
                    fm_cb->chipon = false;
                    up(&fm_ops_mutex);
                }  

                //Unregister FM main Callback
                mtk_wcn_stp_register_event_cb(FM_TASK_INDX, NULL);
            } else if(rst_msg == WMTRSTMSG_RESET_END){
                FM_LOG_NTC(D_MAIN, "FM restart end!\n");
                /*reset_end message handling*/
                //check if we have get start event
                if(chip_rst_state != FM_WHOLECHIP_RST_START){
                    FM_LOG_NTC(D_MAIN, "Wrong event, there's no start!!!\n");
                    return;
                }

                //set all event flag false
                spin_lock(&flag_lock);
                flag = 0x0;
                spin_unlock(&flag_lock);

                //wake up recover work if need
                queue_work(fm_cb->fm_timer_workqueue, &fm_cb->fm_rst_recover_work);
                
                //set rst state end
                chip_rst_state = FM_WHOLECHIP_RST_END;
            }
        }
    } else {
        /*message format invalid*/
        FM_LOG_WAR(D_MAIN, "message format invalid!\n");
    }
}

static int fm_recover_func(struct fm *fm, struct fm_context *cxt)
{
    int ret = 0;
    struct fm_tune_parm parm;

    FM_LOG_NTC(D_MAIN,"+%s():\n", __func__);
    if(cxt->ref < 1){
        FM_LOG_NTC(D_MAIN,"FM not in use, no need recover\n");
        goto out;
    }else{
        //FM func on
        if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_on(WMTDRV_TYPE_FM)){
            FM_LOG_WAR(D_MAIN, "FM mtk_wcn_wmt_func_on failed!\n");
            //FIX ME, how to handle this case?
            goto out;
        }

        //change FM Subsys state
        ret = down_interruptible(&fm_ops_mutex);
        if(ret){
            FM_LOG_WAR(D_MAIN, "down fm_ops_mutex failed!\n");
            //FIX ME, how to handle this case?
            goto out;
        }else{
            fm->chipon = true;
            up(&fm_ops_mutex);
        }  
        
        //Register FM main Callback
        mtk_wcn_stp_register_event_cb(FM_TASK_INDX, stp_rx_event_cb);

        //Check stp ready after turn on function
        if(FALSE == mtk_wcn_stp_is_ready()){
            FM_LOG_ERR(D_ALL,"6620 stp is not ready, please retry later\n");
            goto out;
        }

        //power on FM, recover the context  
        switch(cxt->powerup){
            case FM_PWR_RX_ON:
                fm_powerdown(fm, FM_RX);
                //power on FM RX
                parm.band = fm->band;
                parm.freq = cxt->freq;
                parm.space = FM_SPACE_100K;
                ret = fm_powerup(fm, &parm);
                FM_LOG_NTC(D_MAIN, "[band=%d][freq=%d]\n", parm.band, parm.freq);
                if(ret){
                    FM_LOG_WAR(D_MAIN, "fm_powerup failed!\n");
                    //FIX ME, how to handle this case?
                    goto out;
                }

                /*//set FM RX vol
                ret = fm_setvol(fm, cxt->rxcxt.vol);
                if(ret){
                    FM_LOG_WAR(D_MAIN, "fm_setvol failed!\n");
                    //FIX ME, how to handle this case?
                    goto out;
                }*/
                //set mute if need
                FM_LOG_NTC(D_MAIN, "[rxcxt.mute=%d]\n", cxt->rxcxt.mute);
                ret = MT6620_Mute(cxt->rxcxt.mute);
                if(ret){
                    FM_LOG_WAR(D_MAIN, "MT6620_Mute failed!\n");
                    //FIX ME, how to handle this case?
                    goto out;
                }

                //set FM antenna type
                FM_LOG_NTC(D_MAIN, "[rxcxt.ana=%d]\n", cxt->rxcxt.ana);
                ret = fm_antenna_switch(fm, cxt->rxcxt.ana);
                if(ret){
                    FM_LOG_WAR(D_MAIN, "fm_antenna_switch failed!\n");
                    //FIX ME, how to handle this case?
                    goto out;
                }

                //set FM over bt
                FM_LOG_NTC(D_MAIN, "[rxcxt.ViaBt=%d]\n", cxt->rxcxt.ViaBt);
                ret = fm_over_bt(fm, cxt->rxcxt.ViaBt);
                if(ret){
                    FM_LOG_WAR(D_MAIN, "fm_over_bt failed!\n");
                    //FIX ME, how to handle this case?
                    goto out;
                }

                //set FM RDS on/off
                FM_LOG_NTC(D_MAIN, "[rxcxt.rdsOn=%d]\n", cxt->rxcxt.rdsOn);
                cxt->rxcxt.rdsOn = true; //XHC. just for test
                if(fm_cxt->rxcxt.rdsOn == true){
                    timer_sys.rds_reset_en = FM_RDS_RST_ENABLE;
                }
                ret = MT6620_RDS_OnOff(fm, cxt->rxcxt.rdsOn);
                if(ret){
                    FM_LOG_WAR(D_MAIN, "MT6620_RDS_OnOff failed!\n");
                    //FIX ME, how to handle this case?
                    goto out;
                }

                //set audio path to I2S
#ifdef FM_DIGITAL_INPUT
                mt_combo_audio_ctrl(COMBO_AUDIO_STATE_2);
#endif
                break;
            case FM_PWR_TX_ON:
                fm_powerdown(fm, FM_TX);
                parm.band = fm->band;
                parm.freq = cxt->freq;
                parm.space = FM_SPACE_100K;
                ret = fm_powerup_tx(fm, &parm);
                if(ret){
                    FM_LOG_WAR(D_MAIN, "fm_powerup_tx failed!\n");
                    //FIX ME, how to handle this case?
                    goto out;
                }
                 //set audio path to I2S
#ifdef FM_DIGITAL_OUTPUT
                mt_combo_audio_ctrl(COMBO_AUDIO_STATE_2);
#endif
                //RDS TX 
                ret = MT6620_Rds_Tx_Enable(1);
                if(ret){
                    FM_LOG_WAR(D_MAIN, "MT6620_Rds_Tx_Enable failed!\n");
                    //FIX ME, how to handle this case?
                    goto out;
                }   
                ret = MT6620_Rds_Tx(cxt->txcxt.pi, cxt->txcxt.ps, cxt->txcxt.other_rds, cxt->txcxt.other_rds_cnt);
                if(ret){
                    FM_LOG_WAR(D_MAIN, "MT6620_Rds_Tx failed!\n");
                    //FIX ME, how to handle this case?
                    goto out;
                }
                break;
            case FM_PWR_OFF:
                FM_LOG_WAR(D_MAIN, "fm was pwr down, no need powerup!\n");
                break;
            default:
                FM_LOG_ERR(D_MAIN, "error para!\n");
                break;
        }
    }  
out:
    FM_LOG_NTC(D_MAIN,"-%s():[T=%d]\n", __func__, jiffies_to_msecs(jiffies));
    return ret;
}

static void fm_rst_recover_worker_func(struct work_struct *work)
{
    int ret = 0;

    FM_LOG_NTC(D_MAIN,"+%s():\n", __func__);
    
    fm_cxt->ref = fm_cb->ref;
    fm_cxt->freq = _current_frequency;
    
    //set all event flag false
    spin_lock(&flag_lock);
    flag = 0x0;
    spin_unlock(&flag_lock);
    ret = fm_recover_func(fm_cb, fm_cxt);
    if(ret){
        FM_LOG_ERR(D_MAIN,"fm_recover_func failed\n");
    }
    
    chip_rst_state = FM_WHOLECHIP_RST_OFF;
    FM_LOG_NTC(D_MAIN,"-%s():[T=%d]\n", __func__, jiffies_to_msecs(jiffies));
    return;
}

static void fm_subsys_recover_worker_func(struct work_struct *work)
{
    int ret = 0;

    FM_LOG_NTC(D_MAIN,"+%s():\n", __func__);
    if(chip_rst_state != FM_WHOLECHIP_RST_OFF){
        FM_LOG_WAR(D_MAIN,"FM whole chip resetting\n");
        goto out;
    }
    fm_cxt->ref = fm_cb->ref;
    fm_cxt->freq = _current_frequency;
    //set all event flag false
    spin_lock(&flag_lock);
    flag = 0x0;
    spin_unlock(&flag_lock);
    ret = fm_recover_func(fm_cb, fm_cxt);
    if(ret){
        FM_LOG_ERR(D_MAIN,"fm_recover_func failed\n");
    }

out:
    subsys_rst_state = FM_SUBSYS_RST_OFF;
    FM_LOG_NTC(D_MAIN,"-%s():[T=%d]\n", __func__, jiffies_to_msecs(jiffies));
    return;
}

static void fm_rx_worker_func(struct work_struct *work)
{
    int len;
    int i = 0;
    static volatile fm_task_parser_state state = FM_TASK_RX_PARSER_PKT_TYPE;
    uint8_t opcode = 0;
    uint16_t length = 0;
    unsigned char ch;
	unsigned char rx_buf[RX_BUF_SIZE + 10] = {0}; //the 10 bytes are protect gaps
    struct fm *fm = fm_cb;
    RDSData_Struct *pstRDSData = fm->pstRDSData;
    static int cnt2 = 0;

    FM_LOG_DBG(D_MAIN,"cnt2=%d\n", ++cnt2);
    len = mtk_wcn_stp_receive_data(rx_buf, RX_BUF_SIZE, FM_TASK_INDX);
    FM_LOG_DBG(D_RX,"+%s():[len=%d],[CMD=0x%02x 0x%02x 0x%02x 0x%02x]\n", __func__, len, rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3]);

	while (i < len){
        ch = rx_buf[i];

        switch (state){
            case FM_TASK_RX_PARSER_PKT_TYPE:
                if(ch == FM_TASK_EVENT_PKT_TYPE){
                    if((i+5) < RX_BUF_SIZE){
                        FM_LOG_DBG(D_RX,"0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x \n", rx_buf[i], rx_buf[i+1], rx_buf[i+2], rx_buf[i+3], rx_buf[i+4], rx_buf[i+5]);
                    }else{
                        FM_LOG_DBG(D_RX,"0x%02x 0x%02x\n", rx_buf[i], rx_buf[i+1]);
                    }
                    state = FM_TASK_RX_PARSER_OPCODE;
                }else{
                    FM_LOG_ERR(D_RX,"%s(): event pkt type error (rx_buf[%d] = 0x%02x)\n", __func__, i, ch);
                }
                i++;
                break;

            case FM_TASK_RX_PARSER_OPCODE:
                i++;
                opcode = ch;
                state = FM_TASK_RX_PARSER_PKT_LEN_1;
                break;

            case FM_TASK_RX_PARSER_PKT_LEN_1:
                i++;
                length = ch;
                state = FM_TASK_RX_PARSER_PKT_LEN_2;
                break;

            case FM_TASK_RX_PARSER_PKT_LEN_2:
                i++;
                length |= (uint16_t)(ch << 0x8);
                if(length > 0){
                    state = FM_TASK_RX_PARSER_PKT_PAYLOAD;
                }else{
                    spin_lock(&flag_lock);
                    flag |= (1 << opcode);
                    spin_unlock(&flag_lock);
                    state = FM_TASK_RX_PARSER_PKT_TYPE;
                    FM_LOG_DBG(D_RX,"[flag=0x%08x]\n", flag);
                    wake_up(&fm_wq);
                }
                break;

            case FM_TASK_RX_PARSER_PKT_PAYLOAD:
                switch(opcode){
                    case FM_SEEK_OPCODE:
                        spin_lock(&flag_lock);
                        flag |= FLAG_SEEK_DONE;
                        spin_unlock(&flag_lock);
                        if((i+1) < RX_BUF_SIZE){
                            seek_result = (rx_buf[i] + (rx_buf[i+1] << 8)) / 10;
                        }
                        FM_LOG_DBG(D_RX,"[flag=0x%08x]\n", flag);
                        wake_up(&fm_wq);
                        break;

                    case FM_SCAN_OPCODE:
                        spin_lock(&flag_lock);
                        flag |= FLAG_SCAN_DONE;
                        spin_unlock(&flag_lock);
                        //check if the result data is long enough
                        if((RX_BUF_SIZE - i) < (sizeof(uint16_t) * MT6620_SCANTBL_SIZE)){
                            FM_LOG_ERR(D_RX,"FM_SCAN_OPCODE err, [tblsize=%d],[bufsize=%d]\n", (sizeof(uint16_t) * MT6620_SCANTBL_SIZE), (RX_BUF_SIZE - i));
                            wake_up(&fm_wq);
                            return;
                        }
                        memcpy(scan_result, &rx_buf[i], sizeof(uint16_t) * MT6620_SCANTBL_SIZE);
                        FM_LOG_DBG(D_RX,"[flag=0x%08x]\n", flag);
                        wake_up(&fm_wq);
                        break;

                    case FSPI_READ_OPCODE:
                        spin_lock(&flag_lock);
                        flag |= (1 << opcode);
                        spin_unlock(&flag_lock);
                        if((i+1) < RX_BUF_SIZE){
                            fspi_rd = (rx_buf[i] + (rx_buf[i+1] << 8));
                        }
                        FM_LOG_DBG(D_RX,"[flag=0x%08x]\n", flag);
                        wake_up(&fm_wq);
                        break;

                    case RDS_RX_DATA_OPCODE:
                        spin_lock(&flag_lock);
                        flag |= (1 << opcode);
                        spin_unlock(&flag_lock);
                        FM_LOG_INF(D_RX,"-fm_rds_mutex\n");
                        if (down_interruptible(&fm_rds_mutex)){
                            FM_LOG_ERR(D_RX,"Handle RDS: down rds mutex failed\n");
                            return;
                        }
                        rds_rx_size = length;
                        //check if the rds data is long enough
                        if((RX_BUF_SIZE - i) < rds_rx_size){
                            FM_LOG_ERR(D_RX,"RDS_RX_DATA_OPCODE err, [rdsrxsize=%d],[bufsize=%d]\n", (int)rds_rx_size, (RX_BUF_SIZE - i));
                            goto rds_out;
                        }
                        memcpy(&rds_rx_result, &rx_buf[i], rds_rx_size);
                        FM_LOG_DBG(D_RX,"[rds_rx_size=%d]\n", rds_rx_size);
                        #if 0   //RDS Rx verification
                        {
                            int grp;
                            int j = 4;
                            unsigned char *ptr = &rx_buf[i];

                            printk("%lu\t", jiffies * 1000 / HZ);
                            FM_LOG_NTC(D_RX,"%lu\t", jiffies * 1000 / HZ);
                            for(grp = 0; grp < 6; grp++, j+=12){
                                FM_LOG_NTC(D_RX,"%02x%02x%02x%02x%02x%02x%02x%02x\t%02x%02x%02x%02x\t", ptr[j], ptr[j+1], ptr[j+2], ptr[j+3], ptr[j+4], ptr[j+5], ptr[j+6], ptr[j+7], ptr[j+8], ptr[j+9], ptr[j+10], ptr[j+11]);
                            }
                            FM_LOG_NTC(D_RX,"\n");
                        }
                        #endif

                        /*Handle the RDS data that we get*/
                        MT6620_RDS_Eint_Handler(fm, &rds_rx_result, rds_rx_size);
rds_out:                        
                        up(&fm_rds_mutex);
                        FM_LOG_INF(D_RX,"+fm_rds_mutex\n");
                        
                        //loop pstRDSData->event_status then act 
                        //if(pstRDSData->event_status != 0)
                        if((pstRDSData->event_status != 0) && (pstRDSData->event_status 
                        != RDS_EVENT_AF_LIST)){
                            FM_LOG_DBG(D_RX,"Notify user to read, [event:%04x]\n", pstRDSData->event_status);
                            fm->RDS_Data_ready = true;
                            wake_up_interruptible(&fm->read_wait);
                        }

                        FM_LOG_DBG(D_RX,"%s():[flag=0x%08x]\n", __func__, flag);
                        wake_up(&fm_wq);
                        break;

                    default:
                        spin_lock(&flag_lock);
                        flag |= (1 << opcode);
                        spin_unlock(&flag_lock);
                        FM_LOG_DBG(D_RX,"[flag=0x%08x]\n", flag);
                        wake_up(&fm_wq);
                        break;
                }
                state = FM_TASK_RX_PARSER_PKT_TYPE;
                i += length;
                break;

            default:
                break;
        }
    }
    FM_LOG_DBG(D_RX,"-%s():\n", __func__);
}

static int fm_setup_cdev(struct fm *fm)
{
    int err;
    int alloc_ret = 0;

	/*static allocate chrdev*/
    fm->dev_t = MKDEV(FM_major, 0);
	alloc_ret = register_chrdev_region(fm->dev_t, 1, FM_NAME);
    if (alloc_ret){
        FM_LOG_ERR(D_ALL,"%s():fail to register chrdev\n", __func__);
        return alloc_ret;
    }
#if 0
    //dynamic allocate chrdev
    err = alloc_chrdev_region(&fm->dev_t, 0, 1, FM_NAME);
    if (err) {
        FM_LOG_ERR(D_MAIN,"alloc dev_t failed\n");
        return -1;
    }
#endif
    FM_LOG_NTC(D_MAIN,"alloc %s:%d:%d\n", FM_NAME, MAJOR(fm->dev_t), MINOR(fm->dev_t));
    cdev_init(&fm->cdev, &fm_ops);

    fm->cdev.owner = THIS_MODULE;
    fm->cdev.ops = &fm_ops;

    err = cdev_add(&fm->cdev, fm->dev_t, 1);
    if (err){
        FM_LOG_ERR(D_ALL,"%s():alloc dev_t failed\n", __func__);
        return -1;
    }

    return 0;
}

/* 
 *  fm_ops_ioctl 
 *
 *  ioctl system call interface changed, in order to enhance system performance, 
 *  Linux kernel 3.0 will not support ioctl(), so we shoud use unlocked_ioctl().
 *  We should be care of race condition, and lock is a valid way to protect our data.
 *
 *  @filp - current strcut file's pointer, we can get dentry and inode info by it
 *  @cmd - ioctl command that was send by ioctl caller
 *  @arg - usually it's the pointer of a data structure defined in user space
 *  
 *  If success, return 0; else error code
 */
static long fm_ops_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int ret = 0;
	struct fm *fm = container_of(filp->f_dentry->d_inode->i_cdev, struct fm, cdev);

    FM_LOG_INF(D_MAIN,"%s():\n", __func__);

    //check if we are resetting
    if(chip_rst_state != FM_WHOLECHIP_RST_OFF){
        FM_LOG_WAR(D_MAIN, "whole chip is resetting, retry later\n");
        ret = -ERR_WHOLECHIP_RST;
        //ret = 0;
        return ret;
    }

    if(subsys_rst_state != FM_SUBSYS_RST_OFF){
        FM_LOG_WAR(D_MAIN, "FM subsys is resetting, retry later\n");
        ret = -ERR_SUBSYS_RST;
        //ret = 0;
        return ret;
    }
    
    switch(cmd){
        case FM_IOCTL_POWERUP:{
            struct fm_tune_parm parm;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_POWERUP......\n");
// FIXME!!
//            if (!capable(CAP_SYS_ADMIN))
//                return -EPERM;
            if (copy_from_user(&parm, (void*)arg, sizeof(struct fm_tune_parm))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_from_user error\n");
                return -EFAULT;
            }
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }
            ret = fm_powerup(fm, &parm);
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }         
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");
            if (copy_to_user((void*)arg, &parm, sizeof(struct fm_tune_parm))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }

            break;
        }

        case FM_IOCTL_POWERDOWN:{
            int32_t type = -1;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_POWERDOWN......\n");
            FM_LOG_DBG(D_IOCTL,"[chipon=%d],[powerup=%d],[ref=%d]\n", 
                fm->chipon, 
				fm->powerup, 
				fm->ref);
// FIXME!!
//            if (!capable(CAP_SYS_ADMIN))
//                return -EPERM;
            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip already is off\n");
                return -EFAULT;
            }
            if (copy_from_user(&type, (void*)arg, sizeof(int32_t))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_from_user error\n");
                return -EFAULT;
            }  
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }
            ret = fm_powerdown(fm, type);
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");
            break;
	    }

        // tune (frequency, auto Hi/Lo ON/OFF )
        case FM_IOCTL_TUNE:{
            struct fm_tune_parm parm;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_TUNE......\n");
// FIXME!
//            if (!capable(CAP_SYS_ADMIN))
//                return -EPERM;
            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }
            if (copy_from_user(&parm, (void*)arg, sizeof(struct fm_tune_parm))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_from_user error\n");
                return -EFAULT;
            }          
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }
            ret = fm_tune(fm, &parm);
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");

            if (copy_to_user((void*)arg, &parm, sizeof(struct fm_tune_parm))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }
            break;
        }

        case FM_IOCTL_SEEK:{
            struct fm_seek_parm parm;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_SEEK......\n");
// FIXME!!
//            if (!capable(CAP_SYS_ADMIN))
//              return -EPERM;
            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }

            if (copy_from_user(&parm, (void*)arg, sizeof(struct fm_seek_parm))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_from_user error\n");
                return -EFAULT;
            }
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }
            ret = fm_seek(fm, &parm);
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");

            if (copy_to_user((void*)arg, &parm, sizeof(struct fm_seek_parm))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }
            break;
        }

        case FM_IOCTL_SCAN:{
            struct fm_scan_parm parm;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_SCAN......\n");

            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }
            if(copy_from_user(&parm, (void*)arg, sizeof(struct fm_scan_parm))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_from_user error\n");
                return -EFAULT;
            }
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }
            ret = fm_scan(fm, &parm);
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");

            if(copy_to_user((void*)arg, &parm, sizeof(struct fm_scan_parm))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }
            //XHC
            //ret = 0;
            break;
        }

        case FM_IOCTL_SCAN_GETRSSI:{
            struct fm_rssi_req *req;
            if(!(req = kzalloc(sizeof(struct fm_rssi_req), GFP_KERNEL))){
                FM_LOG_ERR(D_INIT,"kzalloc(fm) -ENOMEM\n");
                return -EFAULT;
            }
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_SCAN_GETRSSI......\n");
            FM_COM_ASSERT(TRUE == fm->chipon);
            if(copy_from_user(req, (void*)arg, sizeof(struct fm_rssi_req))){
                FM_LOG_ERR(D_IOCTL, "copy_from_user error\n");
                kfree(req);
                return -EFAULT;
            }
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL, "get fm_ops_mutex error\n");
                kfree(req);
                return -EFAULT;
            }
            ret = fm_get_rssi_after_scan(fm, req);
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL, "fw no response, do hw reset\n");
                fm_hw_reset();
            }
            up(&fm_ops_mutex);
            if(copy_to_user((void*)arg, req, sizeof(struct fm_rssi_req))){
                FM_LOG_ERR(D_IOCTL, "copy_to_user error\n");
                kfree(req);
                return -EFAULT;
            }
            kfree(req);
            break;
        }
        
        case FM_IOCTL_SETVOL:{
            uint32_t vol;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_SETVOL......\n");
// FIXME!!
//            if (!capable(CAP_SYS_ADMIN))
//              return -EPERM;
            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }

	        if(copy_from_user(&vol, (void*)arg, sizeof(uint32_t))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_from_user error\n");
                return -EFAULT;
            }
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }
            FM_LOG_DBG(D_IOCTL,"FM_IOCTL_SETVOL, [vol=%d]\n", vol);
            ret = fm_setvol(fm, vol);
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");
            break;
        }
        case FM_IOCTL_GETVOL:{
            uint32_t vol;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_GETVOL......\n");
// FIXME!!
//            if (!capable(CAP_SYS_ADMIN))
//              return -EPERM;

            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }
            ret = fm_getvol(fm, &vol);
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");
            if (copy_to_user((void*)arg, &vol, sizeof(uint32_t))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }
            break;
        }

		case FM_IOCTL_IS_FM_POWERED_UP:{
			uint32_t pwredup = 0;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_IS_FM_POWERED_UP......\n");
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }
            pwredup = fm->powerup;
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");
			FM_LOG_DBG(D_IOCTL,"FM_IOCTL_IS_FM_POWERED_UP,[powerup=%d]\n", pwredup);
            if (copy_to_user((void*)arg, &pwredup, sizeof(uint32_t))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }
            break;
		}
		
        case FM_IOCTL_MUTE:{
            uint32_t bmute;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_MUTE......\n");
// FIXME!!
//            if (!capable(CAP_SYS_ADMIN))
//              return -EPERM;
            if (copy_from_user(&bmute, (void*)arg, sizeof(uint32_t))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_from_user error\n");
                return -EFAULT;
            }
            FM_LOG_DBG(D_IOCTL,"FM_IOCTL_MUTE:[mute=%d]\n", bmute);
            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }
            if (bmute){
                ret = MT6620_Mute(true);
            }else{
                ret = MT6620_Mute(false);
            }
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");
            break;
        }

        case FM_IOCTL_GETRSSI:{
            int rssi = 0;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_GETRSSI......\n");
// FIXME!!
//            if (!capable(CAP_SYS_ADMIN))
//              return -EPERM;
            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }
            ret = fm_getrssi(fm, &rssi);
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");
            if (copy_to_user((void*)arg, &rssi, sizeof(int))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }
            break;
        }

        case FM_IOCTL_RW_REG:{
            struct fm_ctl_parm parm_ctl;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_RW_REG......\n");
// FIXME!!
//            if (!capable(CAP_SYS_ADMIN))
//              return -EPERM;
            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }

            if (copy_from_user(&parm_ctl, (void*)arg, sizeof(struct fm_ctl_parm))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_from_user error\n");
                return -EFAULT;
            }
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }

            //write
            if(parm_ctl.rw_flag == 0){
                FM_LOG_DBG(D_IOCTL,"Write Reg %02x:%04x\n", parm_ctl.addr, parm_ctl.val);
                ret = MT6620_write(parm_ctl.addr, parm_ctl.val);
            }else{
                ret = MT6620_read(parm_ctl.addr, &parm_ctl.val);
                FM_LOG_DBG(D_IOCTL,"Read Reg %02x:%04x, [ret=%d]\n", parm_ctl.addr, parm_ctl.val, ret);
            }
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }

            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");

            // Read success.
            if ((parm_ctl.rw_flag == 0x01) && (!ret)){
                if (copy_to_user((void*)arg, &parm_ctl, sizeof(struct fm_ctl_parm))){
                    FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                    return -EFAULT;
                }
            }
            break;
        }

        case FM_IOCTL_GETCHIPID:{
            uint16_t chipid;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_GETCHIPID......\n");

            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }

            //ret = MT6620_read(0x62, &chipid);
            chipid = fm->chip_id;
            FM_LOG_DBG(D_IOCTL,"FM_IOCTL_GETCHIPID:%04x\n", chipid);

            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");

            if (copy_to_user((void*)arg, &chipid, sizeof(uint16_t))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }

            break;
        }

        case FM_IOCTL_GETMONOSTERO:{
            uint16_t usStereoMono;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_GETMONOSTERO......\n");

            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }

            ret = MT6620_GetMonoStereo(&usStereoMono);
            FM_LOG_DBG(D_IOCTL,"FM_IOCTL_GETMONOSTERO:%04x\n", usStereoMono);
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }

            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");

            if (copy_to_user((void*)arg, &usStereoMono, sizeof(uint16_t))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }

            break;
        }

        case FM_IOCTL_SETMONOSTERO:{
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_SETMONOSTERO......\n");
            FM_COM_ASSERT(TRUE == fm->chipon);
            ret = MT6620_SetMonoStereo((int)arg);
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }
            break;
        }
        
        case FM_IOCTL_GETCURPAMD:{
            uint16_t PamdLevl;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_GETCURPAMD......\n");

            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }

            ret = MT6620_GetCurPamd(&PamdLevl);
            FM_LOG_DBG(D_MAIN,"FM_IOCTL_GETCURPAMD:%d\n", PamdLevl);
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }

            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");

            if (copy_to_user((void*)arg, &PamdLevl, sizeof(uint16_t))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }

            break;
        }

        case FM_IOCTL_EM_TEST:{
            struct fm_em_parm parm_em;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_EM_TEST......\n");
// FIXME!!
//            if (!capable(CAP_SYS_ADMIN))
//              return -EPERM;
            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }

            if (copy_from_user(&parm_em, (void*)arg, sizeof(struct fm_em_parm))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_from_user error\n");
                return -EFAULT;
            }
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }

            ret = MT6620_em_test(parm_em.group_idx, parm_em.item_idx, parm_em.item_value);

            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }

            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");

            break;
        }
        
		case FM_IOCTL_RDS_SUPPORT:{
			int support;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_RDS_SUPPORT......\n");
            
			support = FM_RDS_ENABLE;
			if (copy_to_user((void*)arg, &support, sizeof(int32_t))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }
			break;
		}

        case FM_IOCTL_RDS_ONOFF:{
            uint16_t rds_onoff = 0;;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_RDS_ONOFF......\n");
            FM_LOG_DBG(D_IOCTL,"[chipon=%d] [powerup=%d] [ref=%d]\n", 
				fm->chipon, 
				fm->powerup, 
				fm->ref);

            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }

            if (copy_from_user(&rds_onoff, (void*)arg, sizeof(uint16_t))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_from_user error\n");
                return -EFAULT;
            }
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");            
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }
                    
            if(rds_onoff){                
                if((ret = MT6620_RDS_OnOff(fm, true))){
                    FM_LOG_ERR(D_IOCTL,"FM_IOCTL_RDS_ONOFF faield\n");
                    if(-ERR_FW_NORES == ret){
                        FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                        fm_hw_reset();
                    }
                    up(&fm_ops_mutex);
                    FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");
                    return -EPERM;                   
                }                
                //fm_enable_eint();
                fm_enable_rds_BlerCheck(fm);
                fm_cxt->rxcxt.rdsOn = true;
                FM_LOG_DBG(D_MAIN,"FM_IOCTL_RDS_ON:%d\n", jiffies_to_msecs(jiffies));
            }else{
                //fm_disable_eint(); 
                fm->RDS_Data_ready = true;
                memset(fm->pstRDSData, 0, sizeof(RDSData_Struct));
                wake_up_interruptible(&fm->read_wait);
                fm_disable_rds_BlerCheck();               
                ret = MT6620_RDS_OnOff(fm, false);
                fm_cxt->rxcxt.rdsOn = false;
                FM_LOG_DBG(D_MAIN,"FM_IOCTL_RDS_OFF:%d\n", jiffies_to_msecs(jiffies));        
            }
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");
            break;      
        }  

	    case FM_IOCTL_GETGOODBCNT:{
		    uint16_t uGBLCNT = 0;	
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_GETGOODBCNT......\n");

            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }

            FM_LOG_INF(D_IOCTL,"-fm_read_mutex\n");
	        if (down_interruptible(&fm_read_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }
		    uGBLCNT = GOOD_BLK_CNT;
		    up(&fm_read_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_read_mutex\n");
            FM_LOG_DBG(D_IOCTL,"FM_IOCTL_GETGOODBCNT:%d\n", uGBLCNT);
		    if (copy_to_user((void*)arg, &uGBLCNT, sizeof(uint16_t))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }       
		    break;
	    }
        case FM_IOCTL_GETBADBNT:{
		    uint16_t uBadBLCNT = 0;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_GETBADBNT......\n");

            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }

            FM_LOG_INF(D_IOCTL,"-fm_read_mutex\n");
	        if (down_interruptible(&fm_read_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }
		    uBadBLCNT = BAD_BLK_CNT;
		    up(&fm_read_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_read_mutex\n");
			
			FM_LOG_DBG(D_IOCTL,"FM_IOCTL_GETBADBNT:%d\n", uBadBLCNT);
		    if (copy_to_user((void*)arg, &uBadBLCNT, sizeof(uint16_t))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }
		    break;
	    }

        case FM_IOCTL_GETBLERRATIO:{
		    uint16_t uBlerRatio = 0;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_GETBLERRATIO......\n");

            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }

            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
	        if (down_interruptible(&fm_read_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }
		    uBlerRatio = (uint16_t)BAD_BLK_RATIO;
		    up(&fm_read_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");

			FM_LOG_DBG(D_IOCTL,"FM_IOCTL_GETBLERRATIO:%d\n", uBlerRatio);
		    if (copy_to_user((void*)arg, &uBlerRatio, sizeof(uint16_t))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }
		    break;
	    }		
		
        #ifdef FMDEBUG
        case FM_IOCTL_DUMP_REG:{
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_DUMP_REG......\n");

            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }
            ret = MT6620_dump_reg();
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");
            break;
        }
        #endif

        case FM_IOCTL_POWERUP_TX:{
            struct fm_tune_parm parm;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_POWERUP_TX......\n");
// FIXME!!
//            if (!capable(CAP_SYS_ADMIN))
//                return -EPERM;
            if (copy_from_user(&parm, (void*)arg, sizeof(struct fm_tune_parm))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_from_user error\n");
                return -EFAULT;
            }
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }
            ret = fm_powerup_tx(fm, &parm);
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");
            if (copy_to_user((void*)arg, &parm, sizeof(struct fm_tune_parm))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }
            break;
        }

        // tune (frequency, auto Hi/Lo ON/OFF )
        case FM_IOCTL_TUNE_TX:{
            struct fm_tune_parm parm;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_TUNE_TX......\n");
// FIXME!
//            if (!capable(CAP_SYS_ADMIN))
//                return -EPERM;
            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }

            if (copy_from_user(&parm, (void*)arg, sizeof(struct fm_tune_parm))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_from_user error\n");
                return -EFAULT;
            }
            
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }
            ret = fm_tune_tx(fm, &parm);
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");

            if (copy_to_user((void*)arg, &parm, sizeof(struct fm_tune_parm))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }
            break;
        }

        case FM_IOCTL_TX_SUPPORT:{
			int32_t tx_support = -1;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_TX_SUPPORT......\n");
// FIXME!!
//            if (!capable(CAP_SYS_ADMIN))
//                return -EPERM;
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
           	if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }

			if(FM_TX_SUPPORT == atomic_read(&fm->tx_support)){
           		tx_support = 1;
            }else if(FM_TX_NOT_SUPPORT == atomic_read(&fm->tx_support)){
		   		tx_support = 0; 
		    }else{
		   		tx_support = -1;
				FM_LOG_ERR(D_IOCTL,"FM_IOCTL_TX_SUPPORT:invalid flag[fm->tx_support=%d]\n", 
					atomic_read(&fm->tx_support));
				ret = -1;
		    }		
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");
            if (copy_to_user((void*)arg, &tx_support, sizeof(int32_t))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }
            break;
        }

		case FM_IOCTL_RDSTX_ENABLE:{
			int32_t rds_tx_enable = -1;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_RDSTX_ENABLE......\n");
// FIXME!!
//            if (!capable(CAP_SYS_ADMIN))
//                return -EPERM;
			if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }
			if(copy_from_user(&rds_tx_enable, (void*)arg, sizeof(int32_t))){
				FM_LOG_ERR(D_IOCTL|D_ALL,"copy_from_user error\n");
				return -EFAULT;
			}
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
           	if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }

			if(FM_RDS_TX_ENABLE == rds_tx_enable){
           		atomic_set(&fm->rds_tx_enable, FM_RDS_TX_ENABLE);
            }else if(FM_RDS_TX_DISENABLE == rds_tx_enable){
		   		atomic_set(&fm->rds_tx_enable, FM_RDS_TX_DISENABLE); 
		    }else{
				FM_LOG_ERR(D_IOCTL,"FM_IOCTL_RDSTX_ENABLE:invalid para[rds_tx_enable=%d]\n", 
					rds_tx_enable);
				ret = -1;
		    }		
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");
            break;
        }

		case FM_IOCTL_RDSTX_SUPPORT:{
			int32_t rds_tx_support = -1;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_RDSTX_SUPPORT......\n");
// FIXME!!
//            if (!capable(CAP_SYS_ADMIN))
//                return -EPERM;
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
           	if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }

			if(FM_RDS_TX_SUPPORT == atomic_read(&fm->rds_tx_support)){
           		rds_tx_support = 1;
            }else if(FM_RDS_TX_NOT_SUPPORT == atomic_read(&fm->rds_tx_support)){
		   		rds_tx_support = 0; 
		    }else{
		   		rds_tx_support = -1;
				FM_LOG_ERR(D_IOCTL,"FM_IOCTL_RDSTX_SUPPORT:invalid flag[fm->rds_tx_support=%d]\n", 
					atomic_read(&fm->rds_tx_support));
				ret = -1;
		    }		
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");
            if (copy_to_user((void*)arg, &rds_tx_support, sizeof(int32_t))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }
            break;
		}

		case FM_IOCTL_TX_SCAN:{
            struct fm_tx_scan_parm parm;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_TX_SCAN......\n");
// FIXME!!
//            if (!capable(CAP_SYS_ADMIN))
//                return -EPERM;
            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }

            if (copy_from_user(&parm, (void*)arg, sizeof(struct fm_tx_scan_parm))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_from_user error\n");
                return -EFAULT;
            }
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }

            ret = fm_tx_scan(fm, &parm);
			if(ret < 0){
				FM_LOG_ERR(D_MAIN,"FM_IOCTL_TX_SCAN failed\n");
			}
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }
            
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");
            if (copy_to_user((void*)arg, &parm, sizeof(struct fm_tx_scan_parm))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }
            break;
        }
        case FM_IOCTL_RDS_TX:{
            struct fm_rds_tx_parm parm;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_RDS_TX......\n");
// FIXME!!
//            if (!capable(CAP_SYS_ADMIN))
//                return -EPERM;
            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }

            if (copy_from_user(&parm, (void*)arg, sizeof(struct fm_rds_tx_parm))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_from_user error\n");
                return -EFAULT;
            }
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }
            ret = fm_rds_tx(fm, &parm);
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");
            if (copy_to_user((void*)arg, &parm, sizeof(struct fm_rds_tx_parm))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }
            break;
        }

        case FM_IOCTL_OVER_BT_ENABLE:{
			int32_t fm_via_bt = -1;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_OVER_BT_ENABLE......\n");
            
			if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }
			if(copy_from_user(&fm_via_bt, (void*)arg, sizeof(int32_t))){
				FM_LOG_ERR(D_IOCTL|D_ALL,"copy_from_user error\n");
				return -EFAULT;
			}
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
           	if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }

            ret = fm_over_bt(fm, fm_via_bt);
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }
			if(FM_OVER_BT_ENABLE == fm_via_bt){
           		atomic_set(&fm->over_bt_enable, FM_OVER_BT_ENABLE);
            }else if(FM_OVER_BT_DISABLE == fm_via_bt){
		   		atomic_set(&fm->over_bt_enable, FM_OVER_BT_DISABLE); 
		    }else{
				FM_LOG_ERR(D_IOCTL,"FM_IOCTL_OVER_BT_ENABLE:invalid para[fm_over_bt=%d]\n", 
					fm_via_bt);
				ret = -1;
		    }		
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");		
            break;
        }

        case FM_IOCTL_ANA_SWITCH:{
            int32_t antenna = -1;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_ANA_SWITCH......\n");
            
            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }
            if(copy_from_user(&antenna, (void*)arg, sizeof(int32_t))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_from_user error\n");
                return -EFAULT;
            }
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
           	if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }

            ret = fm_antenna_switch(fm, antenna);
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }
			
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");		
            break;
        }

        case FM_IOCTL_GETCAPARRAY:{
            int caparray;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_GETCAPARRAY......\n");

            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }

            ret = MT6620_GetCapArray(&caparray);
            FM_LOG_DBG(D_IOCTL,"FM_IOCTL_GETMONOSTERO:%x\n", caparray);
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fw no response, do hw reset\n");
                fm_hw_reset();
            }

            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");

            if (copy_to_user((void*)arg, &caparray, sizeof(int32_t))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }
            break;
        }

        case FM_IOCTL_GPS_RTC_DRIFT:{
            struct fm_gps_rtc_info rtc_info;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_GPS_RTC_DRIFT......\n");

            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }
            if(copy_from_user(&rtc_info, (void*)arg, sizeof(struct fm_gps_rtc_info))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_from_user error\n");
                return -EFAULT;
            }
            
            ret = fm_get_gps_rtc_info(&gps_rtc_info, &rtc_info);
            if(ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"fm_get_gps_rtc_info error\n");
                return ret;
            }
            break;
        }

        case FM_IOCTL_I2S_SETTING:{
            struct fm_i2s_setting i2s_cfg;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_I2S_SETTING......\n");

            if (FALSE == fm->chipon){
                FM_LOG_ERR(D_IOCTL,"ERROR, FM chip is OFF\n");
                return -EFAULT;
            }
            if(copy_from_user(&i2s_cfg, (void*)arg, sizeof(struct fm_i2s_setting))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_from_user error\n");
                return -EFAULT;
            }
            
            ret = MT6620_I2S_Setting(i2s_cfg.onoff, i2s_cfg.mode, i2s_cfg.sample);
            if(ret){
                FM_LOG_ERR(D_IOCTL|D_ALL,"MT6620_I2S_Setting error\n");
                return ret;
            }
            break;
        }
        case FM_IOCTL_RDS_GROUPCNT:{
            struct rds_group_cnt_req gc_req;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_RDS_GROUPCNT......\n");
            if (copy_from_user(&gc_req, (void*)arg, sizeof(struct rds_group_cnt_req))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_from_user error\n");
                return -EFAULT;
            }
            FM_LOG_INF(D_IOCTL,"-fm_ops_mutex\n");
            if (down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
                return -EFAULT;
            }
            //handle group counter request
            switch(gc_req.op){
                case RDS_GROUP_CNT_READ:
                    rds_group_counter_get(&gc_req.gc, &fm->rds_gc);
                    break;
                case RDS_GROUP_CNT_WRITE:
                    break;
                case RDS_GROUP_CNT_RESET:
                    rds_group_counter_reset(&fm->rds_gc);
                    break;
                default:
                    break;
            }
            up(&fm_ops_mutex);
            FM_LOG_INF(D_IOCTL,"+fm_ops_mutex\n");
            if (copy_to_user((void*)arg, &gc_req, sizeof(struct rds_group_cnt_req))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }
            break;
        }
        case FM_IOCTL_RDS_GET_LOG:{
            struct rds_raw_data rds_log;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_RDS_GET_LOG......\n");
            rds_log.dirty = TRUE;
            rds_log.len = rds_rx_size;
            memcpy(&rds_log.data[0], (const void*)&rds_rx_result, sizeof(struct rds_rx));
            if (copy_to_user((void*)arg, &rds_log, sizeof(struct rds_raw_data))){
                FM_LOG_ERR(D_IOCTL|D_ALL,"copy_to_user error\n");
                return -EFAULT;
            }
            break;
        }

        case FM_IOCTL_GET_HW_INFO:{
            struct fm_hw_info info;
            FM_LOG_DBG(D_IOCTL,"......FM_IOCTL_GET_HW_INFO......\n");
            FM_COM_ASSERT(TRUE == fm->chipon);

            if(down_interruptible(&fm_ops_mutex)){
                FM_LOG_ERR(D_IOCTL, "get hw info, get fm_ops_mutex error\n");
                return -EFAULT;
            }
            ret = fm_get_hw_info(fm, &info);
            if(-ERR_FW_NORES == ret){
                FM_LOG_ERR(D_IOCTL, "get hw info, fw no response, do hw reset\n");
                fm_hw_reset();
            }
            up(&fm_ops_mutex);
            if(copy_to_user((void*)arg, &info, sizeof(struct fm_hw_info))){
                FM_LOG_ERR(D_IOCTL, "get hw info, copy_to_user error\n");
                return -EFAULT;
            }
            break;
        }
        
        default:
            FM_LOG_ERR(D_IOCTL,"Invalid IOCTL[cmd=%d], please check!\n", cmd);
            return -EPERM;
    } 
    return ret;
}

static loff_t fm_ops_lseek(struct file *filp, loff_t off, int whence)
{
    if(whence == SEEK_END){
        MT6620_ScanForceStop();
    }
    return off;
}

static ssize_t fm_ops_read(struct file *filp, char *buf, size_t len, loff_t *off)
{
    struct fm *fm = filp->private_data;
    int copy_len = 0, left = 0;
    uint8_t indx;
    
	indx = 0;
    FM_LOG_DBG(D_MAIN,"+%s():\n", __func__);
    if (!fm){
        FM_LOG_ERR(D_MAIN,"%s():invalid fm pointer\n", __func__);
        return 0;
    }
    
    if(!buf || len < sizeof(RDSData_Struct)){
        FM_LOG_ERR(D_ALL,"%s():invalid buf\n", __func__);
        return 0;
    }    
    copy_len = sizeof(RDSData_Struct);   
    
RESTART:    
    if((fm->RDS_Data_ready == true) || (fm->powerup == FM_PWR_OFF)){

        //block when FM is resetting
        while((chip_rst_state != FM_WHOLECHIP_RST_OFF) || (subsys_rst_state != FM_SUBSYS_RST_OFF)){
            msleep_interruptible(100);
        }
        
        FM_LOG_INF(D_MAIN,"-fm_rds_mutex\n");
        if (down_interruptible(&fm_rds_mutex)){
            FM_LOG_ERR(D_MAIN,"get fm_rds_mutex error\n");
            return 0;       
        }

        if(fm->powerup == FM_PWR_OFF){
            memset(fm->pstRDSData, 0, sizeof(RDSData_Struct));
        }
        
        if((left = copy_to_user((void *)buf, fm->pstRDSData, (unsigned long)copy_len))){
            FM_LOG_ERR(D_MAIN,"%s():copy_to_user failed\n", __func__);
        }
        FM_LOG_DBG(D_MAIN,"copy len=%d\n", (copy_len-left));
         //Clear
        if(left == 0){
            fm->pstRDSData->event_status = 0x0;			
        }
        fm->RDS_Data_ready = false;			
        up(&fm_rds_mutex);
        FM_LOG_INF(D_MAIN,"+fm_rds_mutex\n");
    }else{
        FM_LOG_DBG(D_MAIN,"wait event\n");
        if (wait_event_interruptible(fm->read_wait, (fm->RDS_Data_ready == true)) == 0){
            FM_LOG_DBG(D_MAIN,"wait event success[%d]\n", fm->RDS_Data_ready);
            goto RESTART;             
        }else {
            FM_LOG_ERR(D_MAIN,"%s():wait event err[%d]\n", __func__, fm->RDS_Data_ready);
            fm->RDS_Data_ready = false;
            return 0;            
        }
    }

    FM_LOG_DBG(D_MAIN,"-%s():\n", __func__);
    return (copy_len-left);
}

static int fm_ops_open(struct inode *inode, struct file *filp)
{
    int ret = 0;
    struct fm *fm = container_of(inode->i_cdev, struct fm, cdev);

    fm_get_process_path(fm);
   
    FM_LOG_DBG(D_MAIN,"+%s():[T=%d]\n", __func__, jiffies_to_msecs(jiffies));

    //check if we are resetting
    if(chip_rst_state != FM_WHOLECHIP_RST_OFF){
        FM_LOG_WAR(D_MAIN, "FM is resetting, please open later\n");
        ret = -ERR_WHOLECHIP_RST;
        return ret;
    }

    if(subsys_rst_state != FM_SUBSYS_RST_OFF){
        FM_LOG_WAR(D_MAIN, "FM subsys is resetting, retry later\n");
        ret = -ERR_SUBSYS_RST;
        return ret;
    }
    
    FM_LOG_INF(D_MAIN,"-fm_ops_mutex\n");
    if (down_interruptible(&fm_ops_mutex)){
        FM_LOG_ERR(D_IOCTL|D_ALL,"get fm_ops_mutex error\n");
        return -EFAULT;
    }
    
    fm->ref++;
    FM_LOG_NTC(D_MAIN,"%s [fm->ref=%d]\n", __func__, fm->ref);
    //Audio driver will open first handle, for save power.
    if(fm->ref > 0){
        if(FALSE == fm->chipon){
            //Turn on FM on 6620 chip by WMT driver 
            if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_on(WMTDRV_TYPE_FM)) {
                FM_LOG_ERR(D_MAIN,"%s WMT turn on FM fail!\n", __func__);
                ret = -ENODEV;
                goto out; 
            }
            fm->chipon = true;
            fm_cxt->chipon = true;
            spin_lock(&flag_lock);
            flag = 0; //FM info flag
            spin_unlock(&flag_lock);
             /* GeorgeKuo: turn on function before check stp ready */
            if(FALSE == mtk_wcn_stp_is_ready()){
                fm->chipon = false;
                fm_cxt->chipon = false;
                fm->ref--;
                FM_LOG_ERR(D_MAIN,"6620 stp is not ready, please retry later\n");
                ret = -ENODEV;
                goto out; 
            }
            //Register the main Callback func that will get data form 6620 FM firware 
            mtk_wcn_stp_register_event_cb(FM_TASK_INDX, stp_rx_event_cb);
            mtk_wcn_wmt_msgcb_reg(WMTDRV_TYPE_FM, fm_wholechip_rst_cb);
            FM_LOG_NTC(D_MAIN,"%s WMT turn on FM OK, [fm->chipon=%d]!\n", __func__, fm->chipon);
        }
    }
    
out:
    up(&fm_ops_mutex);
    FM_LOG_INF(D_MAIN,"+fm_ops_mutex\n");
    filp->private_data = fm;
    FM_LOG_DBG(D_MAIN,"-%s():[T=%d]\n", __func__, jiffies_to_msecs(jiffies));
    return ret;
}

static int fm_ops_release(struct inode *inode, struct file *filp)
{
    int ret = 0;
    struct fm *fm = container_of(inode->i_cdev, struct fm, cdev);

    FM_LOG_DBG(D_MAIN,"+%s()\n", __func__);
    FM_LOG_INF(D_MAIN,"-fm_ops_mutex\n");
    if (down_interruptible(&fm_ops_mutex)){
        FM_LOG_ERR(D_MAIN,"get fm_ops_mutex error\n");
        return -EFAULT;
    }

    if(fm->ref > 0){
        fm->ref--;
    }else{
        FM_LOG_ERR(D_MAIN,"%s():open and close FM unmatch\n", __func__);
        ret = -EFAULT;
    }
    FM_LOG_NTC(D_MAIN,"%s [fm->ref=%d]\n", __func__, fm->ref);

    //check if we are resetting
    if(chip_rst_state != FM_WHOLECHIP_RST_OFF){
        FM_LOG_WAR(D_MAIN, "FM is resetting, only ref--\n");
        ret = -ERR_WHOLECHIP_RST;
        up(&fm_ops_mutex);
        FM_LOG_INF(D_MAIN,"+fm_ops_mutex\n"); 
        return ret;
    }

    if(subsys_rst_state != FM_SUBSYS_RST_OFF){
        FM_LOG_WAR(D_MAIN, "FM subsys is resetting, only ref--\n");
        ret = -ERR_SUBSYS_RST;
        up(&fm_ops_mutex);
        FM_LOG_INF(D_MAIN,"+fm_ops_mutex\n");
        return ret;
    }
    if(fm->ref < 1){

        fm_disable_rds_BlerCheck();               
        ret = MT6620_RDS_OnOff(fm, FALSE);
        
        if(FM_PWR_RX_ON == fm->powerup){
            ret = fm_powerdown(fm, FM_RX);
        }else if(FM_PWR_TX_ON == fm->powerup){
            ret = fm_powerdown(fm, FM_TX);
        }
        
        if (TRUE == fm->chipon){
            if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_off(WMTDRV_TYPE_FM)){
                FM_LOG_ERR(D_MAIN,"WMT turn off FM fail!\n");
                ret = -EFAULT;
            }else{
                FM_LOG_NTC(D_MAIN,"WMT turn off FM OK!\n");
            }
            fm->chipon = FALSE;
            fm_cxt->chipon = false;
            //Unregister FM main Callback
			mtk_wcn_stp_register_event_cb(FM_TASK_INDX, NULL);
            mtk_wcn_wmt_msgcb_unreg(WMTDRV_TYPE_FM);
        }
    }

    up(&fm_ops_mutex);
    FM_LOG_INF(D_MAIN,"+fm_ops_mutex\n");   
    FM_LOG_DBG(D_MAIN,"-%s()[ret=%d]\n", __func__, ret);
    return ret;
}

static int fm_get_process_path(struct fm *fm)
{
    int ret = 0;
    
#ifdef FM_TASK_INFO_DEBUG
    struct dentry *dnty = NULL;
    FM_LOG_NTC(D_MAIN,"[TaskName=%s],[TaskId=%d]\n", current->comm, current->pid);
    dnty = current->mm->mmap->vm_file->f_path.dentry;
    printk("[%s]", dnty->d_name.name);
    while(strcmp(dnty->d_name.name, "/")){
        dnty = dnty->d_parent;
        printk("[%s]", dnty->d_name.name);   
    }  
    printk("\n");
#endif   

    return ret;
}

static int fm_timer_init(struct fm *fm, struct fm_timer_sys *fmtimer, struct timer_list *timer)
{
    int ret = 0;
    
    fmtimer->onoff = FM_TIMER_SYS_OFF;
    fmtimer->count = 0;
    fmtimer->rds_reset_en = FM_RDS_RST_DISABLE;
    fmtimer->tx_pwr_ctrl_en = FM_TX_PWR_CTRL_DISABLE;
    fmtimer->tx_rtc_ctrl_en = FM_TX_RTC_CTRL_DISABLE;
    fmtimer->tx_desense_en = FM_TX_DESENSE_DISABLE;
    
    init_timer(timer);
    timer->expires  = jiffies + FM_TIMER_TIMEOUT_MIN/(1000/HZ);
    timer->function = fm_timer_func;
    timer->data     = (unsigned long)fm;
    add_timer(timer);
    
    return ret;
}
static int fm_rtc_compensation_init(struct fm *fm, struct fm_gps_rtc_info *rtc)
{
    int ret = 0;
    
    rtc->err = 0;
    rtc->age = 0;
    rtc->drift = 0;
    rtc->tv.tv_sec = 0;
    rtc->tv.tv_usec = 0;
    rtc->ageThd = FM_GPS_RTC_AGE_TH;
    rtc->driftThd = FM_GPS_RTC_DRIFT_TH;
    rtc->tvThd.tv_sec = FM_GPS_RTC_TIME_DIFF_TH;
    rtc->retryCnt = FM_GPS_RTC_RETRY_CNT;
    rtc->flag = FM_GPS_RTC_INFO_OLD;
    
    return ret;
}

static int fm_print_cfg_info(struct fm *fm, void* data)
{
    int ret = 0;
    
    FM_LOG_NTC(D_INIT,"******fm config info******\n");
    FM_LOG_NTC(D_INIT,"***chip:\tMT6620\t\n");
    FM_LOG_NTC(D_INIT,"***band:\t%d\t\n", FMR_BAND);
    FM_LOG_NTC(D_INIT,"***freq_min:\t%d\t\n", FMR_BAND_FREQ_L);
    FM_LOG_NTC(D_INIT,"***freq_max:\t%d\t\n", FMR_BAND_FREQ_H);
    FM_LOG_NTC(D_INIT,"***scan_tbl:\t%d\t\n", FMR_SCAN_CH_SIZE);
    FM_LOG_NTC(D_INIT,"***space:\t%d\t\n", FMR_SEEK_SPACE);
    FM_LOG_NTC(D_INIT,"***rssi_long:\t0x%04x\t\n", FMR_RSSI_TH_LONG);
    FM_LOG_NTC(D_INIT,"***rssi_short:\t0x%04x\t\n", FMR_RSSI_TH_SHORT);
    FM_LOG_NTC(D_INIT,"***CQI:\t0x%04x\t\n", FMR_CQI_TH);
    FM_LOG_NTC(D_INIT,"******fm config end******\n");
    
    return ret;
}

static int fm_init()
{
    int err;
    struct fm *fm = NULL;

    if (!(fm = kzalloc(sizeof(struct fm), GFP_KERNEL))){
        FM_LOG_ERR(D_INIT,"kzalloc(fm) -ENOMEM\n");
        err = -ENOMEM;
        goto fm_alloc_err;
    }

    if (!(tx_buf = kzalloc(TX_BUF_SIZE + 1, GFP_KERNEL))){
        FM_LOG_ERR(D_INIT,"kzalloc(tx_buf) -ENOMEM\n");
        err = -ENOMEM;
        goto tx_alloc_err;
    }

    if (!(fm_cxt = kzalloc(sizeof(struct fm_context), GFP_KERNEL))){
        FM_LOG_ERR(D_INIT,"kzalloc(fm_cxt) -ENOMEM\n");
        err = -ENOMEM;
        goto tx_alloc_err;
    }
    
    fm->ref = 0;
	atomic_set(&fm->tx_support, FM_TX_SUPPORT);
	atomic_set(&fm->rds_tx_support, FM_RDS_TX_SUPPORT);
	atomic_set(&fm->rds_tx_enable, FM_RDS_TX_ENABLE);
    atomic_set(&fm->over_bt_enable, FM_OVER_BT_DISABLE);
    fm->chipon = false;
    fm->powerup = FM_PWR_OFF;
    fm->chip_id = 0x6620;
    fm->tx_pwr = FMTX_PWR_LEVEL_MAX;

    if ((err = fm_setup_cdev(fm))){
        FM_LOG_ERR(D_INIT,"fm_setup_cdev() failed\n");
        goto setup_cdev_err;
    }

    init_waitqueue_head(&fm->read_wait);
    if (!(fm->pstRDSData = kmalloc(sizeof(RDSData_Struct), GFP_KERNEL))){
        FM_LOG_ERR(D_INIT,"-ENOMEM for RDS\n");
        err = -ENOMEM;
        goto alloc_rds_err;
    }
	
    fm->fm_workqueue = create_singlethread_workqueue("fm_workqueue");
    if(!fm->fm_workqueue){
        FM_LOG_ERR(D_INIT,"-ENOMEM for fm_workqueue\n");
        err = -ENOMEM;
        goto rx_wq_err;
    }
    
    
    fm->fm_timer_workqueue = create_singlethread_workqueue("fm_timer_workqueue");
    if(!fm->fm_timer_workqueue){
        FM_LOG_ERR(D_INIT,"-ENOMEM for fm_timer_workqueue\n");
        err = -ENOMEM;
        goto rds_reset_wq_err;
    }
    
    
    INIT_WORK(&fm->fm_rx_work, fm_rx_worker_func);
    INIT_WORK(&fm->fm_rds_reset_work, fm_rds_reset_worker_func);
    INIT_WORK(&fm->fm_tx_power_ctrl_work, fm_tx_power_ctrl_worker_func);
    INIT_WORK(&fm->fm_tx_rtc_ctrl_work, fm_tx_rtc_ctrl_worker_func); 
    INIT_WORK(&fm->fm_tx_desense_wifi_work, fm_tx_desense_wifi_worker_func);
    INIT_WORK(&fm->fm_rst_recover_work, fm_rst_recover_worker_func);
    INIT_WORK(&fm->fm_subsys_recover_work, fm_subsys_recover_worker_func);
    
    fm_cb = fm;

    fm_rtc_compensation_init(fm_cb, &gps_rtc_info);
    fm_timer_init(fm_cb, &timer_sys, &fm_timer);

    /*Add porc file system*/
	g_fm_proc = create_proc_entry(FM_PROC_FILE, 0666, NULL);
	if (g_fm_proc == NULL){
		FM_LOG_ERR(D_INIT,"create_proc_entry failed\n");
		err = -ENOMEM;
		goto rds_reset_wq_err;
	}else{
		g_fm_proc->read_proc = fm_proc_read;
		g_fm_proc->write_proc = fm_proc_write;
		FM_LOG_NTC(D_INIT,"create_proc_entry success\n");
	}

    /*Add fm config porc file*/
	g_fm_config.proc = create_proc_entry(FM_PROC_CONFIG, 0666, NULL);
	if (g_fm_config.proc == NULL){
		FM_LOG_ERR(D_INIT,"create_config_entry failed\n");
		err = -ENOMEM;
		goto rds_reset_wq_err;
	}else{
		g_fm_config.proc->read_proc = fmconfig_proc_read;
		g_fm_config.proc->write_proc = fmconfig_proc_write;
		FM_LOG_NTC(D_INIT,"create_config_entry success\n");
	}

    g_fm_config.vcooff = FM_TX_VCO_OFF_DEFAULT;  //TX RTC VCO tracking interval(s)
    g_fm_config.vcoon = FM_TX_VCO_ON_DEFAULT; //TX VCO tracking ON duiration(ms)
    g_fm_config.rdsrst = FM_RDS_RST_INVAL_DEFAULT; //RDS RX Good/Bad block reset interval(s)
    g_fm_config.txpwrctl = FM_TX_PWR_CTRL_INVAL_DEFAULT; //TX power contrl interval(s)
    g_fm_config.timer = FM_TIMER_TIMEOUT_DEFAULT; //FM timer system time out interval(ms) 

    err = fm_print_cfg_info(fm, NULL);
    if(err){
        FM_LOG_ERR(D_INIT,"print fm cfg info failed\n");    
    }
    return 0;
    
rds_reset_wq_err:
    destroy_workqueue(fm->fm_workqueue);

rx_wq_err:
	kfree(fm->pstRDSData);
	
alloc_rds_err:
	cdev_del(&fm->cdev);
	
setup_cdev_err:
    kfree(tx_buf);

tx_alloc_err:
    kfree(fm);

fm_alloc_err:
    return err;
}

static int fm_destroy(struct fm *fm)
{
    int err = 0;

    FM_LOG_NTC(D_INIT, "%s\n", __func__);
    remove_proc_entry(FM_PROC_CONFIG, NULL);
	remove_proc_entry(FM_PROC_FILE, NULL);
    
    if(NULL == fm){
        FM_LOG_ERR(D_INIT,"%s Invalid pointer fm\n", __func__);
        err = -1;
    }
    del_timer(&fm_timer);
    cdev_del(&fm->cdev);
    unregister_chrdev_region(fm->dev_t, 1);

	flush_scheduled_work();
    
    // FIXME: any other hardware configuration ?
    
    if(fm->fm_timer_workqueue){
        destroy_workqueue(fm->fm_timer_workqueue);
    }
    
	if(fm->fm_workqueue){
        destroy_workqueue(fm->fm_workqueue);
    }
	
    if (fm->pstRDSData){
        kfree(fm->pstRDSData);
        fm->pstRDSData = NULL;
    }

    // free all memory
    if(fm_cxt){
        kfree(fm_cxt);
        fm_cxt = NULL;
    }
    
    if(tx_buf){
        kfree(tx_buf);
        tx_buf = NULL;
    }

    if(fm){
        kfree(fm);
    }
    fm_cb = NULL;   
    return err;
}

static int do_cmd_send(struct fm *fm, unsigned char *buf, int len, int opcode, void* data){
    int ret = 0;
    int pkt_size = 0;
    
    if (down_interruptible(&fm_cmd_mutex)){
        FM_LOG_ERR(D_MAIN,"%s(): get mutex failed\n", __func__);
        ret = -ERR_GET_MUTEX;
        return ret;
    }

    //special defined cmd
    if(buf && (len > 4) && (len < TX_BUF_SIZE)){
        FM_LOG_NTC(D_MAIN,"sending special cmd[len=%d]\n", len);
        ret = fm_send_wait_timeout(buf, (uint16_t)len, 
                                    FLAG_EN, SW_RETRY_CNT, EN_TIMEOUT);
        if(ret){
            FM_LOG_ERR(D_MAIN,"send special cmd failed\n");
            goto out;
        }
    }

    //additional cmd for normal case
    if(tx_buf == NULL){
        FM_LOG_ERR(D_MAIN,"%s(): invalid tx_buf\n", __func__);
        ret = -ERR_INVALID_BUF;
		goto out;
	}
	memset(tx_buf, 0, TX_BUF_SIZE);
    pkt_size = mt6620_com(tx_buf, TX_BUF_SIZE, opcode, NULL);
    if(pkt_size < 0){
       FM_LOG_ERR(D_MAIN,"unsupported cmd\n"); 
       goto out;
    }
    FM_LOG_NTC(D_MAIN,"sending additional cmd[len=%d]\n", pkt_size);
    ret = fm_send_wait_timeout(tx_buf, (uint16_t)pkt_size, 
                                    FLAG_EN, SW_RETRY_CNT, EN_TIMEOUT);
    if(ret){
        FM_LOG_ERR(D_MAIN,"send additional cmd failed\n");
    }
    
out:  
    up(&fm_cmd_mutex);
    return ret;
}

/*
 *  fm_powerup
 */
static int fm_powerup(struct fm *fm, struct fm_tune_parm *parm)
{
    int ret = 0;
    uint16_t pkt_size = 0;

    if(FM_PWR_RX_ON == fm->powerup){
        //FM RX already on
        FM_LOG_NTC(D_MAIN,"%s, FM Rx already powered up\n", __func__);
        parm->err = FM_BADSTATUS;
        return 0;
    }else if(FM_PWR_TX_ON == fm->powerup){
        //if Tx is on, we need pwr down TX first
        ret = fm_powerdown(fm, FM_TX);
        if(ret){
           FM_LOG_ERR(D_MAIN,"FM pwr down Tx fail!\n"); 
           return ret;
        }
    }

    //We should make chip on first before do Power on  
    if (FALSE == fm->chipon){
        if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_on(WMTDRV_TYPE_FM)){
            FM_LOG_ERR(D_MAIN,"WMT turn on FM fail!\n");
            return -ENODEV;
        }
        FM_LOG_NTC(D_MAIN,"WMT turn on FM OK!\n");
        fm->chipon = TRUE;
    }

    FM_LOG_INF(D_MAIN,"+MT6620 power on procedure\n");
    if (down_interruptible(&fm_cmd_mutex)){
        FM_LOG_ERR(D_MAIN,"%s(): get mutex failed\n", __func__);
        ret = -ERR_GET_MUTEX;
        return ret;
    }
    
	if(tx_buf == NULL){
        FM_LOG_ERR(D_MAIN,"%s(): invalid tx_buf\n", __func__);
        ret = -ERR_INVALID_BUF;
		goto release_mutex;
	}
	memset(tx_buf, 0, TX_BUF_SIZE);
    
    pkt_size = mt6620_off_2_longANA(tx_buf, TX_BUF_SIZE);
    ret = fm_send_wait_timeout(tx_buf, pkt_size, 
                                    FLAG_EN, SW_RETRY_CNT, EN_TIMEOUT);
    if(ret){
        FM_LOG_ERR(D_MAIN,"%s(): mt6620_off_2_longANA failed\n", __func__);
        goto release_mutex;
    }

    pkt_size = mt6620_rx_digital_init(tx_buf, TX_BUF_SIZE);
    ret = fm_send_wait_timeout(tx_buf, pkt_size, 
                                    FLAG_EN, SW_RETRY_CNT, EN_TIMEOUT);
    if(ret){
        FM_LOG_ERR(D_MAIN,"%s(): mt6620_rx_digital_init failed\n", __func__);
        goto release_mutex;
    }
    
release_mutex:
    up(&fm_cmd_mutex);

    if(ret)
        return ret;
    
#ifdef FM_DIGITAL_INPUT
  #ifdef MT6573 //for MT6573
    if(get_chip_eco_ver() == CHIP_E1){
        ret = MT6620_I2S_Setting(FM_I2S_ON, FM_I2S_MASTER, FM_I2S_48K);
    }else{
        ret = MT6620_I2S_Setting(FM_I2S_ON, FM_I2S_SLAVE, FM_I2S_48K);
    }
  #else //for MT6575
    ret = MT6620_I2S_Setting(FM_I2S_ON, FM_I2S_SLAVE, FM_I2S_48K);
  #endif
    if(ret){
        FM_LOG_ERR(D_MAIN,"pwron set I2S on error\n");
        return ret;
    }
    //we will disable 6620 fm chip analog output when use I2S path, set 0x3A bit2 = 0
    //MT6620_set_bits(0x3A, 0, MASK(2));
    FM_LOG_DBG(D_MAIN,"pwron set I2S on ok\n");
#endif

	FM_LOG_INF(D_MAIN,"-MT6620 power on procedure\n");
    FM_LOG_NTC(D_MAIN,"pwron RX ok\n");
    
    fm->powerup = FM_PWR_RX_ON;
    fm_cxt->powerup = FM_PWR_RX_ON;
    ret = down_interruptible(&fm_timer_mutex);
    if(ret){
        FM_LOG_ERR(D_MAIN,"down fm_timer_mutex failed[ret=%d]\n", ret);
        goto out;
    }
    timer_sys.onoff = FM_TIMER_SYS_ON;
    //update timer    
    if(g_fm_config.timer < 1000){
        FM_LOG_WAR(D_TIMER,"timersys time err\n");
        g_fm_config.timer = FM_TIMER_TIMEOUT_DEFAULT;
    }
	mod_timer(&fm_timer, jiffies + g_fm_config.timer/(1000/HZ)); 
    up(&fm_timer_mutex);
    //Tune to desired channel
    if ((ret = fm_tune(fm, parm))){
        FM_LOG_ERR(D_MAIN,"Power on RX tune failed\n");
        return ret;
    }
    parm->err = FM_SUCCESS;  
    
out:
    return ret;
}

static int fm_antenna_switch(struct fm *fm, int antenna)
{
	int ret = 0;
	
	ret = MT6620_ANA_SWITCH(antenna);
	if(ret){
		FM_LOG_ERR(D_MAIN,"Switch antenna failed\n");
	}
    fm_cxt->rxcxt.ana = antenna;
	FM_LOG_DBG(D_MAIN,"%s(),[ret=%d]!\n", __func__, ret);
	return ret;
}

/*
 *  fm_powerup_tx
 */
static int fm_powerup_tx(struct fm *fm, struct fm_tune_parm *parm)
{
    int ret = 0;
    uint16_t pkt_size = 0;

    if(FM_PWR_TX_ON == fm->powerup){
        //FM TX already on
        FM_LOG_NTC(D_MAIN,"%s, FM Tx already powered up\n", __func__);
        parm->err = FM_BADSTATUS;
        return 0;
    }else if(FM_PWR_RX_ON == fm->powerup){
        //if Rx is on, we need pwr down RX first
        ret = fm_powerdown(fm, FM_RX);
        if(ret){
           FM_LOG_ERR(D_MAIN,"FM pwr down Rx fail!\n"); 
           return ret;
        }
    }
    //for normal case
    if (FALSE == fm->chipon){
        if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_on(WMTDRV_TYPE_FM)) {
            FM_LOG_ERR(D_MAIN,"WMT turn on FM fail!\n");
            return -ENODEV;
        }
        FM_LOG_DBG(D_MAIN,"WMT turn on FM OK!\n");
        fm->chipon = TRUE;
    }

    FM_LOG_INF(D_MAIN,"+MT6620 power on tx procedure\n");
    if (down_interruptible(&fm_cmd_mutex)){
        FM_LOG_ERR(D_MAIN,"%s(): get mutex failed\n", __func__);
        ret = -ERR_GET_MUTEX;
        return ret;
    }
    
	if(tx_buf == NULL){
        FM_LOG_ERR(D_MAIN,"%s(): invalid tx_buf\n", __func__);
        ret = -ERR_INVALID_BUF;
		goto release_mutex;
	}
	memset(tx_buf, 0, TX_BUF_SIZE);
    
    pkt_size = mt6620_off_2_tx_shortANA(tx_buf, TX_BUF_SIZE);
    ret = fm_send_wait_timeout(tx_buf, pkt_size, 
                                    FLAG_EN, SW_RETRY_CNT, EN_TIMEOUT);
    if(ret){
        FM_LOG_ERR(D_MAIN,"%s(): mt6620_off_2_tx_shortANA failed\n", __func__);
        goto release_mutex;
    }

    pkt_size = mt6620_dig_init(tx_buf, TX_BUF_SIZE);
    ret = fm_send_wait_timeout(tx_buf, pkt_size, 
                                    FLAG_EN, SW_RETRY_CNT, EN_TIMEOUT);
    if(ret){
        FM_LOG_ERR(D_MAIN,"%s(): mt6620_dig_init failed\n", __func__);
        goto release_mutex;
    }
release_mutex:
    up(&fm_cmd_mutex);

    if(ret)
        return ret;
    
	FM_LOG_INF(D_MAIN,"-MT6620 power on tx procedure\n");
    FM_LOG_NTC(D_MAIN,"pwron tx ok\n");
    
    fm->powerup = FM_PWR_TX_ON;
    fm_cxt->powerup = FM_PWR_TX_ON;
    ret = down_interruptible(&fm_timer_mutex);
    if(ret){
        FM_LOG_ERR(D_MAIN,"down fm_timer_mutex failed[ret=%d]\n", ret);
        goto out;
    }
    //update timer    
    if(g_fm_config.timer < 1000){
        FM_LOG_WAR(D_TIMER,"timersys time err\n");
        g_fm_config.timer = FM_TIMER_TIMEOUT_DEFAULT;
    }
	mod_timer(&fm_timer, jiffies + g_fm_config.timer/(1000/HZ)); 
    timer_sys.onoff = FM_TIMER_SYS_ON;
    timer_sys.tx_pwr_ctrl_en = FM_TX_PWR_CTRL_ENABLE;
    timer_sys.tx_rtc_ctrl_en = FM_TX_RTC_CTRL_ENABLE;
    timer_sys.tx_desense_en = FM_TX_DESENSE_ENABLE;
    up(&fm_timer_mutex);
    //get temprature
    if(mtk_wcn_wmt_therm_ctrl(WMTTHERM_ENABLE) != TRUE){
       FM_LOG_ERR(D_MAIN,"wmt_therm_ctrl, WMTTHERM_ENABLE failed\n"); 
       ret = -ERR_STP;
       return ret;
    }
    if ((ret = fm_tune_tx(fm, parm))){
        FM_LOG_ERR(D_MAIN,"PowerUp Tx(), tune failed\n");
        return ret;
    }

#ifdef FM_DIGITAL_OUTPUT
  #ifdef MT6573 //for MT6573
    if(get_chip_eco_ver() == CHIP_E1){
        ret = MT6620_I2S_Setting(FM_I2S_ON, FM_I2S_SLAVE, FM_I2S_48K);
    }else{
        ret = MT6620_I2S_Setting(FM_I2S_ON, FM_I2S_SLAVE, FM_I2S_48K);
    }
  #else // for MT6575
    ret = MT6620_I2S_Setting(FM_I2S_ON, FM_I2S_SLAVE, FM_I2S_48K);
  #endif
    if(ret){
        FM_LOG_ERR(D_MAIN,"pwron Tx set I2S on error\n");
        return ret;
    }
    FM_LOG_DBG(D_MAIN,"pwron Tx set I2S on ok\n");
#endif

    parm->err = FM_SUCCESS; 

out:
    return ret;
}
/*
 *  fm_powerdown
 */
static int fm_powerdown(struct fm *fm, int type)
{
    int ret = 0;
    uint16_t pkt_size = 0;

    FM_LOG_DBG(D_MAIN,"%s \n", __func__);
    
    if(down_interruptible(&fm_rxtx_mutex)){
        FM_LOG_ERR(D_TIMER, "%s(): get rx/tx mutex failed\n", __func__);
        return -ERR_GET_MUTEX;
    }
    
    if(((fm->powerup == FM_PWR_TX_ON) && (type == FM_RX)) || ((fm->powerup == FM_PWR_RX_ON) && (type == FM_TX))){
        FM_LOG_NTC(D_MAIN,"no need do pwr down\n");
        ret = 0;
        goto out;
    }
    
#if (defined(FM_DIGITAL_INPUT) || defined(FM_DIGITAL_OUTPUT))
    if(MT6620_I2S_Setting(FM_I2S_OFF, FM_I2S_MASTER, FM_I2S_48K)){
        FM_LOG_ERR(D_MAIN,"pwrdown set I2S off error\n");
        goto out;
    }
    //MT6620_set_bits(0x3A, BITn(2), MASK(2));
    FM_LOG_DBG(D_MAIN,"pwrdown set I2S off ok\n");
#endif

    if (down_interruptible(&fm_cmd_mutex)){
        FM_LOG_ERR(D_CMD|D_ALL, "%s(): get mutex failed\n", __func__);
        ret = -ERR_GET_MUTEX;
        goto out;
    }
	if(tx_buf == NULL){
		FM_LOG_ERR(D_MAIN,"invalid tx_buf\n");
        up(&fm_cmd_mutex);
		goto out;
	}
	memset(tx_buf, 0, TX_BUF_SIZE);
    pkt_size = mt6620_powerdown(tx_buf, TX_BUF_SIZE);
    ret = fm_send_wait_timeout(tx_buf, pkt_size, 
                                    FLAG_EN, SW_RETRY_CNT, EN_TIMEOUT);
    up(&fm_cmd_mutex);
    FM_LOG_INF(D_CMD,"+fm_cmd_mutex\n");
    if(ret){
        FM_LOG_ERR(D_MAIN,"%s(): pwrdown failed\n", __func__);
        goto out;
    }

    ret = down_interruptible(&fm_timer_mutex);
    if(ret){
        FM_LOG_ERR(D_MAIN,"down fm_timer_mutex failed[ret=%d]\n", ret);
        goto out;
    }
    timer_sys.onoff = FM_TIMER_SYS_OFF;
    timer_sys.rds_reset_en = FM_RDS_RST_DISABLE;
    timer_sys.tx_pwr_ctrl_en = FM_TX_PWR_CTRL_DISABLE;
    timer_sys.tx_rtc_ctrl_en = FM_TX_RTC_CTRL_DISABLE;
    timer_sys.tx_desense_en = FM_TX_DESENSE_DISABLE;
    up(&fm_timer_mutex);
    if(fm->powerup == FM_PWR_TX_ON){
        if(mtk_wcn_wmt_therm_ctrl(WMTTHERM_DISABLE) != TRUE){
            FM_LOG_ERR(D_MAIN,"wmt_therm_ctrl, WMTTHERM_DISABLE failed\n"); 
            ret = -ERR_STP;
        }
    }
    fm->RDS_Data_ready = true; //info FM APP to stop read RDS
    memset(fm->pstRDSData, 0, sizeof(RDSData_Struct));
    wake_up_interruptible(&fm->read_wait);
    fm->powerup = FM_PWR_OFF;
    fm_cxt->powerup = FM_PWR_OFF;
    FM_LOG_NTC(D_MAIN,"pwrdown %s ok\n", (type==FM_TX)? "TX" : "RX");
out:
    up(&fm_rxtx_mutex);
    return ret;
}

/*
 *  fm_seek
 */
static int fm_seek(struct fm *fm, struct fm_seek_parm *parm)
{
    int ret = 0;
    uint16_t seekdir, space;

    if (!fm->powerup){
        parm->err = FM_BADSTATUS;
        return -EPERM;
    }

    if (parm->space == FM_SPACE_100K){
        space = MT6620_FM_SPACE_100K;
    }else if (parm->space == FM_SPACE_200K) {
        space = MT6620_FM_SPACE_200K;
    }else{
        //default
        space = MT6620_FM_SPACE_100K;
    }

    if (parm->band == FM_BAND_UE){
        fm->min_freq = 875;
        fm->max_freq = 1080;
    }else if (parm->band == FM_BAND_JAPAN){
        fm->min_freq = 760;
        fm->max_freq = 900;
    }else if (parm->band == FM_BAND_JAPANW){
        fm->min_freq = 760;
        fm->max_freq = 1080;
    }else if(parm->band == FM_BAND_SPECIAL){
        fm->min_freq = FMR_BAND_FREQ_L;
        fm->max_freq = FMR_BAND_FREQ_H;
    }else{
        FM_LOG_ERR(D_MAIN,"band:%d out of range\n", parm->band);
        parm->err = FM_EPARM;
        return -EPERM;
    }

    if (parm->freq < fm->min_freq || parm->freq > fm->max_freq){
        FM_LOG_ERR(D_MAIN,"freq:%d out of range\n", parm->freq);
        parm->err = FM_EPARM;
        return -EPERM;
    }

    if (parm->seekdir == FM_SEEK_UP){
        seekdir = MT6620_FM_SEEK_UP;
    }else{
        seekdir = MT6620_FM_SEEK_DOWN;
    }

#ifdef FMDEBUG
    if (parm->seekdir == FM_SEEK_UP)
        FM_LOG_DBG(D_MAIN,"seek %d up\n", parm->freq);
    else
        FM_LOG_DBG(D_MAIN,"seek %d down\n", parm->freq);
#endif

    // seek successfully
    if(!(ret = MT6620_Seek(fm->min_freq, fm->max_freq, &(parm->freq), seekdir, space))){
        parm->err = FM_SUCCESS; 
    }else{
        parm->err = FM_SEEK_FAILED;
    }
    
    //ret = do_cmd_send(fm, NULL, 0, FM_COM_CMD_SEEK, NULL);
    
    return ret;
}

/*
 *  fm_scan
 */
static int  fm_scan(struct fm *fm, struct fm_scan_parm *parm)
{
    int ret = 0;
    uint16_t scandir = MT6620_FM_SCAN_UP, space;

    if (!fm->powerup){
        parm->err = FM_BADSTATUS;
        return -EPERM;
    }

    if (parm->space == FM_SPACE_100K){
        space = MT6620_FM_SPACE_100K;
    }else if (parm->space == FM_SPACE_200K) {
        space = MT6620_FM_SPACE_200K;
    }else{
        //default
        space = MT6620_FM_SPACE_100K;
    }

    if(parm->band == FM_BAND_UE){
        fm->min_freq = 875;
        fm->max_freq = 1080;
    }else if(parm->band == FM_BAND_JAPAN){
        fm->min_freq = 760;
        fm->max_freq = 900;
    }else if(parm->band == FM_BAND_JAPANW){
        fm->min_freq = 760;
        fm->max_freq = 1080;
    }else if(parm->band == FM_BAND_SPECIAL){
        fm->min_freq = FMR_BAND_FREQ_L;
        fm->max_freq = FMR_BAND_FREQ_H;
    }else{
        FM_LOG_ERR(D_ALL,"fm_scan band:%d out of range\n", parm->band);
        parm->err = FM_EPARM;
        return -EPERM;
    }

    if(!(ret = MT6620_Scan(fm->min_freq, fm->max_freq, &(parm->freq), 
                            parm->ScanTBL, &(parm->ScanTBLSize), scandir, space))){
        parm->err = FM_SUCCESS;
    }else{
        FM_LOG_ERR(D_MAIN,"fm_scan failed\n");
        parm->err = FM_SEEK_FAILED;
    }

    return ret;
}

/*
 * fm_get_rssi_after_scan
 * 
 * get the rssi per channel, we need only use fast tune instead of normal tune to save time
 * @fm -- main data struct of fm driver
 * @req -- input freq,  and output rssi 
 * 
 */
static int  fm_get_rssi_after_scan(struct fm *fm, struct fm_rssi_req *req)
{
    int ret = 0;
    int i, j;
    int tmp = 0;
    int total = 0;

    req->num = (req->num > sizeof(req->cr)) ? sizeof(req->cr) : req->num; 
    for(i = 0; i < req->num; i++){
        ret = MT6620_Fast_SetFreq(req->cr[i].freq);
        if(ret) 
            return ret;
        //get rssi, if need multi read, do it and get the average val
        req->read_cnt = (req->read_cnt < 1) ? 1 : req->read_cnt;
        req->read_cnt = (req->read_cnt > 10) ? 10 : req->read_cnt;
        tmp = 0;
        total = 0;
        for(j = 0; j < req->read_cnt; j++){
            ret = MT6620_GetCurRSSI(&tmp);
            if(ret) 
                return ret;
            Delayms(2);
            total += tmp;
        }
        req->cr[i].rssi = total/req->read_cnt;
    }

    return ret;
}

static int fm_get_hw_info(struct fm *pfm, struct fm_hw_info *req)
{
    int ret = 0;
    ENUM_WMTHWVER_TYPE_T eco_ver;
    
    eco_ver = mtk_wcn_wmt_hwver_get();
    req->chip_id = 0x00006620;
    req->eco_ver = (int)eco_ver;
    req->rom_ver = 0x00000002;
    req->patch_ver = 0x00000111;
    req->reserve = 0x00000000;
    
    return ret;
}

//volume?[0~15]
static int fm_setvol(struct fm *fm, uint32_t vol)
{
    uint8_t tmp_vol;
    int ret = 0;

    tmp_vol = vol*3;
    ret = MT6620_SetVol(tmp_vol);
    fm_cxt->rxcxt.vol = vol;

    return ret;
}

static int fm_getvol(struct fm *fm, uint32_t *vol)
{
    uint8_t tmp_vol;
    int ret = 0;

    ret = MT6620_GetVol(&tmp_vol);
    if(tmp_vol == FM_VOL_MAX)
        *vol = 15;
    else
        *vol = (tmp_vol/3);

    return ret;
}

static int fm_getrssi(struct fm *fm, int *rssi)
{
    int ret = 0;
    
    ret = MT6620_GetCurRSSI(rssi);

    return ret;
}

/*
 * fm_print_curCQI -- print cur freq's CQI
 * @cur_freq, current frequency
 * If OK, return 0, else error code
 */
static int fm_print_curCQI(uint16_t cur_freq)
{
    int ret = 0;
    uint16_t rssi = 0;
    uint16_t pamd = 0;
    uint16_t mr = 0;

    if((ret = MT6620_write(FM_PGSEL, FM_PG0)))
        return ret;
    if((ret = MT6620_read(FM_RSSI_IND, &rssi)))
        return ret;
    if((ret = MT6620_read(FM_PAMD_IND, &pamd)))
        return ret;
    if((ret = MT6620_read(FM_MR_IND, &mr)))
        return ret;
    
    FM_LOG_NTC(D_MAIN,"FREQ=%d, RSSI=0x%04x, PAMD=0x%04x, MR=0x%04x\n", (int)cur_freq, rssi, pamd, mr);
    return ret;    
}

/*
 *  fm_tune
 */
static int fm_tune(struct fm *fm, struct fm_tune_parm *parm)
{
    int ret = 0;
    int hl_side = -1;
    int freq_avoid = -1;
    ENUM_WMTHWVER_TYPE_T hw_ver;
    
    FM_LOG_DBG(D_MAIN,"%s\n", __func__);

    if (!fm->powerup){
        parm->err = FM_BADSTATUS;
        return -ERR_INVALID_PARA;
    }

    if (parm->band == FM_BAND_UE){
        fm->min_freq = 875;
        fm->max_freq = 1080;
    }else if (parm->band == FM_BAND_JAPAN){
        fm->min_freq = 760;
        fm->max_freq = 900;
    }else if (parm->band == FM_BAND_JAPANW){
        fm->min_freq = 760;
        fm->max_freq = 1080;
    }else{
        parm->err = FM_EPARM;
        return -ERR_INVALID_PARA;
    }
    fm->band = parm->band;
    
    if (unlikely(parm->freq < fm->min_freq || parm->freq > fm->max_freq)){
        parm->err = FM_EPARM;
        return -ERR_INVALID_PARA;
    }

    if((ret = MT6620_RampDown()))
        return ret;
	
    //high low side switch
    //adpll freq avoid
    //mcu freq avoid
    if(priv.state == INITED){
        FM_LOG_INF(D_MAIN,"%s, hl_side switch[0x%08x]\n", __func__, (unsigned int)priv.priv_tbl.hl_side);
        if(priv.priv_tbl.hl_side != NULL){
            if((ret = priv.priv_tbl.hl_side(parm->freq, &hl_side)))
                return ret;
        }
        FM_LOG_NTC(D_MAIN,"%s, [hl_side=%d]\n", __func__, hl_side);
		
        FM_LOG_INF(D_MAIN,"%s, adpll freq avoid[0x%08x]\n", __func__, (unsigned int)priv.priv_tbl.adpll_freq_avoid);
        if(priv.priv_tbl.adpll_freq_avoid != NULL){
            if((ret = priv.priv_tbl.adpll_freq_avoid(parm->freq, &freq_avoid)))
                return ret;
        }
		FM_LOG_NTC(D_MAIN,"%s, adpll [freq_avoid=%d]\n", __func__, freq_avoid);

        hw_ver = mtk_wcn_wmt_hwver_get();
        if(hw_ver >= WMTHWVER_MT6620_E3){
		FM_LOG_INF(D_MAIN,"%s, mcu freq avoid[0x%08x]\n", __func__, (unsigned int)priv.priv_tbl.mcu_freq_avoid);
		if(priv.priv_tbl.mcu_freq_avoid != NULL){
            if((ret = priv.priv_tbl.mcu_freq_avoid(parm->freq, &freq_avoid)))
                return ret;
        }
        FM_LOG_NTC(D_MAIN,"%s, mcu [freq_avoid=%d]\n", __func__, freq_avoid);
        }else{
            FM_LOG_NTC(D_MAIN,"%s, no need do mcu freq avoid[hw_ver=%d]\n", __func__, hw_ver);
        }
    }
    
    ret = MT6620_SetFreq(parm->freq);

    ret = fm_print_curCQI(_current_frequency);
    
    return ret;
}

/*
 *  fm_tune_tx
 */
static int fm_tune_tx(struct fm *fm, struct fm_tune_parm *parm)
{
    int ret = 0;
    FM_LOG_DBG(D_MAIN,"%s\n", __func__);

    if (!fm->powerup){
        parm->err = FM_BADSTATUS;
        return -EPERM;
    }

    if (parm->band == FM_BAND_UE){
        fm->min_freq = 875;
        fm->max_freq = 1080;
    }else if (parm->band == FM_BAND_JAPAN){
        fm->min_freq = 760;
        fm->max_freq = 900;
    }else if (parm->band == FM_BAND_JAPANW){
        fm->min_freq = 760;
        fm->max_freq = 1080;
    }else{
        parm->err = FM_EPARM;
        return -ERR_INVALID_PARA;
    }

    if (unlikely(parm->freq < fm->min_freq || parm->freq > fm->max_freq)){
        parm->err = FM_EPARM;
        return -ERR_INVALID_PARA;
    }

    //we will do rampdown in tune sequence, it's do nothing here
    while(0){
        if((ret = MT6620_RampDown_Tx()))
            return ret;
    }

    //tune to desired channel
    ret = MT6620_SetFreq_Tx(parm->freq);
    return ret;
}

/***********************************************************
Function: 	fm_tx_scan()

Description: 	get the valid channels for fm tx function

Para: 		fm--->fm driver global info
			parm--->input/output paramater
			
Return: 		0, if success; error code, if failed
***********************************************************/
static int fm_tx_scan(struct fm *fm, struct fm_tx_scan_parm *parm)
{
    int ret = 0;
	uint16_t scandir = 0;
	uint16_t space = FM_SPACE_100K;
 
    if (!fm->powerup){
        parm->err = FM_BADSTATUS;
		FM_LOG_ERR(D_ALL,"fm_tx_scan failed, [fm->powerup=%d]\n", fm->powerup);
        return -EPERM;
    }
	
	if(FM_TX_SUPPORT != atomic_read(&fm->tx_support)){
		parm->err = FM_BADSTATUS;
		FM_LOG_ERR(D_ALL,"fm_tx_scan failed, [fm->tx_support=%d]\n", atomic_read(&fm->tx_support));
		return -EPERM;
	}

	switch(parm->scandir){
		case MT6620_FM_SCAN_UP:
			scandir = 0;
			break;
		case MT6620_FM_SCAN_DOWN:
			scandir = 1;
			break;
		default:
			scandir = 0;
			break;
	}
	
	switch(parm->space){
		case FM_SPACE_100K:
			space = 1;
			break;
		case FM_SPACE_200K:
			space = 2;
			break;
		default:
			space = 1;
			break;
	}
	
	switch(parm->band){
		case FM_BAND_UE:
			fm->min_freq = 875;
        	fm->max_freq = 1080;
			break;
		case FM_BAND_JAPAN:
			fm->min_freq = 760;
        	fm->max_freq = 900;
			break;
		case FM_BAND_JAPANW:
			fm->min_freq = 760;
        	fm->max_freq = 1080;
			break;
		default:
			parm->err = FM_EPARM;
			FM_LOG_ERR(D_ALL,"fm_tx_scan: bad band para\n");
        	return -EPERM;
			break;
	}

    if (unlikely((parm->freq < fm->min_freq) || (parm->freq > fm->max_freq))){
        parm->err = FM_EPARM;
        return -EPERM;
    }

	if (unlikely(parm->ScanTBLSize < TX_SCAN_MIN || parm->ScanTBLSize > TX_SCAN_MAX)){
        parm->err = FM_EPARM;
        return -EPERM;
    }

    //make sure use short antenna, and you can't use "fm_antenna_switch()" because it will record ana context for RX
    ret = MT6620_ANA_SWITCH(FM_SHORT_ANA); 
    if(ret){
        FM_LOG_ERR(D_MAIN,"switch to short ana failed\n");
        return ret;
    }
    
	//do tx scan
	if(!(ret = MT6620_TxScan(fm->min_freq, fm->max_freq, &(parm->freq), 
								parm->ScanTBL, &(parm->ScanTBLSize), scandir, space))){
        parm->err = FM_SUCCESS;
    }else{
        FM_LOG_ERR(D_MAIN,"fm_tx_scan failed\n");
        parm->err = FM_SCAN_FAILED;
    }
    return ret;
}

static int fm_rds_tx(struct fm *fm, struct fm_rds_tx_parm *parm)
{
    int ret = 0;
    FM_LOG_DBG(D_RDS,"+%s()\n", __func__);
    if(parm->other_rds_cnt > 29){
        parm->err = FM_EPARM;
        FM_LOG_WAR(D_RDS,"%s(), [other_rds_cnt=%d]\n", __func__, parm->other_rds_cnt);
        ret = -ERR_INVALID_PARA;
        goto out;
    }
    
    ret = MT6620_Rds_Tx_Enable(1);
    if(ret){
        FM_LOG_WAR(D_RDS,"%s(), MT6620_Rds_Tx_Enable failed!\n", __func__);
        goto out;
    }   
    ret = MT6620_Rds_Tx(parm->pi, parm->ps, parm->other_rds, parm->other_rds_cnt);
    fm_cxt->txcxt.rdsTxOn = true;
    fm_cxt->txcxt.pi = parm->pi;
    memcpy(fm_cxt->txcxt.ps, parm->ps,sizeof(parm->ps));
    memcpy(fm_cxt->txcxt.other_rds, parm->other_rds,sizeof(parm->other_rds));
    fm_cxt->txcxt.other_rds_cnt = parm->other_rds_cnt; 
out:
    FM_LOG_DBG(D_RDS,"-%s()\n", __func__);
    return ret;
}

static int fm_over_bt(struct fm *fm, int enable)
{
    int ret = 0;
   
    ret = MT6620_FMOverBT(enable);
    if(ret){
        FM_LOG_WAR(D_MAIN,"%s(),failed!\n", __func__);
    }
    fm_cxt->rxcxt.ViaBt = enable;
    FM_LOG_DBG(D_MAIN,"%s(),[ret=%d]!\n", __func__, ret);
    return ret;
}
 
static int fm_proc_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int cnt = 0;
    struct fm *fm  = fm_cb;
    
	FM_LOG_DBG(D_MAIN, "Enter fm_proc_read.\n");
	if(off != 0)
		return 0;
	if (fm != NULL && fm->chipon){
        if(fm->powerup == FM_PWR_TX_ON){
		    cnt = sprintf(page, "2\n");
	    }else if(fm->powerup == FM_PWR_RX_ON){
		cnt = sprintf(page, "1\n");
        }
	} else {
		cnt = sprintf(page, "0\n");
	}
	*eof = 1;
	FM_LOG_DBG(D_MAIN, "Leave fm_proc_read. FM_on = %s\n", page);
	return cnt;
}

static int fm_proc_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	char tmp_buf[11] = {0};
	uint32_t copysize;
	
	copysize = (count < (sizeof(tmp_buf) - 1)) ? count : (sizeof(tmp_buf) - 1);
	if (copy_from_user(tmp_buf, buffer, copysize)){
		FM_LOG_ERR(D_ALL, "failed copy_from_user\n");
        return -EFAULT;
    }

	if (sscanf(tmp_buf, "%x", &g_dbg_level) != 1){
		FM_LOG_ERR(D_ALL, "failed g_dbg_level = 0x%x\n", g_dbg_level);
		return -EFAULT;
	}

	FM_LOG_WAR(D_MAIN, "success g_dbg_level = 0x%x\n", g_dbg_level);
	return count;
}

static int fmconfig_proc_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int cnt = 0;
    //struct fm *fm  = fm_cb;
    
	FM_LOG_DBG(D_MAIN, "Enter fm_config_proc_read.\n");
	if(off != 0)
		return 0;
    cnt = sprintf(page, "[t]timerSys: %d ms\n", g_fm_config.timer);
    cnt += sprintf(page+cnt, "[v]vcoOff: %d s\n", g_fm_config.vcooff);
    cnt += sprintf(page+cnt, "[v]vcoOn: %d ms\n", g_fm_config.vcoon);
    cnt += sprintf(page+cnt, "[p]pwrctlTx: %d s\n", g_fm_config.txpwrctl);
    cnt += sprintf(page+cnt, "[r]rdsRst: %d s\n", g_fm_config.rdsrst);
	*eof = 1;
	FM_LOG_DBG(D_MAIN, "Leave fm_config_proc_read. Config = %s\n", page);
	return cnt;
}

static int fmconfig_proc_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	char tmp_buf[100] = {0};
	uint32_t copysize;
	
	copysize = (count < (sizeof(tmp_buf) - 1)) ? count : (sizeof(tmp_buf) - 1);
	if (copy_from_user(tmp_buf, buffer, copysize)){
		FM_LOG_ERR(D_ALL, "failed copy_from_user\n");
        return -EFAULT;
    }

    switch(tmp_buf[0]){
        case 'v':
            switch(tmp_buf[1]){
                case 'u':
                    //FM TX vco tracking ON duiration setting 
                    if (sscanf(&tmp_buf[2], "%d", &g_fm_config.vcoon) != 1){
		                FM_LOG_ERR(D_ALL, "failed vcoon = %d\n", g_fm_config.vcoon);
		                return -EFAULT;
	                }
                    g_fm_config.vcoon = (g_fm_config.vcoon < FM_TX_VCO_ON_MIN) ? FM_TX_VCO_ON_MIN : g_fm_config.vcoon;
                    g_fm_config.vcoon = (g_fm_config.vcoon > FM_TX_VCO_ON_MAX) ? FM_TX_VCO_ON_MAX : g_fm_config.vcoon;
	                FM_LOG_WAR(D_MAIN, "success vcoon = %d\n", g_fm_config.vcoon);
                    break;
                case 'd':
                    //FM TX vco tracking OFF duiration setting 
                    if (sscanf(&tmp_buf[2], "%d", &g_fm_config.vcooff) != 1){
		                FM_LOG_ERR(D_ALL, "failed vcooff = %d\n", g_fm_config.vcooff);
		                return -EFAULT;
	                }
                    g_fm_config.vcooff = (g_fm_config.vcooff < FM_TX_VCO_OFF_MIN) ? FM_TX_VCO_OFF_MIN : g_fm_config.vcooff;
                    g_fm_config.vcooff = (g_fm_config.vcooff > FM_TX_VCO_OFF_MAX) ? FM_TX_VCO_OFF_MAX : g_fm_config.vcooff;
	                FM_LOG_WAR(D_MAIN, "success vcooff = %d\n", g_fm_config.vcooff);
                    break;
                default:
                    FM_LOG_ERR(D_MAIN, "Para 2 error\n");
                    break;
            }
            break;
        case 't':
            if (sscanf(&tmp_buf[1], "%d", &g_fm_config.timer) != 1){
		        FM_LOG_ERR(D_ALL, "failed timer = %d\n", g_fm_config.timer);
		        return -EFAULT;
	        }
            g_fm_config.timer = (g_fm_config.timer < FM_TIMER_TIMEOUT_MIN) ? FM_TIMER_TIMEOUT_MIN : g_fm_config.timer;
            g_fm_config.timer = (g_fm_config.timer > FM_TIMER_TIMEOUT_MAX) ? FM_TIMER_TIMEOUT_MAX : g_fm_config.timer;
	        FM_LOG_WAR(D_MAIN, "success timer = %d\n", g_fm_config.timer);
            break;
        case 'r':
            if (sscanf(&tmp_buf[1], "%d", &g_fm_config.rdsrst) != 1){
		        FM_LOG_ERR(D_ALL, "failed rdsrst = %d\n", g_fm_config.rdsrst);
		        return -EFAULT;
	        }
            g_fm_config.rdsrst = (g_fm_config.rdsrst < FM_RDS_RST_INVAL_MIN) ? FM_RDS_RST_INVAL_MIN : g_fm_config.rdsrst;
            g_fm_config.rdsrst = (g_fm_config.rdsrst > FM_RDS_RST_INVAL_MAX) ? FM_RDS_RST_INVAL_MAX : g_fm_config.rdsrst;
	        FM_LOG_WAR(D_MAIN, "success rdsrst = %d\n", g_fm_config.rdsrst);
            break;
        case 'p':
            if (sscanf(&tmp_buf[1], "%d", &g_fm_config.txpwrctl) != 1){
		        FM_LOG_ERR(D_ALL, "failed txpwrctl = %d\n", g_fm_config.txpwrctl);
		        return -EFAULT;
	        }
            g_fm_config.txpwrctl = (g_fm_config.txpwrctl < FM_RDS_RST_INVAL_MIN) ? FM_RDS_RST_INVAL_MIN : g_fm_config.txpwrctl;
            g_fm_config.txpwrctl = (g_fm_config.txpwrctl > FM_RDS_RST_INVAL_MAX) ? FM_RDS_RST_INVAL_MAX : g_fm_config.txpwrctl;
	        FM_LOG_WAR(D_MAIN, "success txpwrctl = %d\n", g_fm_config.txpwrctl);
            break;
        case 's':
            fm_hw_reset();
	        FM_LOG_WAR(D_MAIN, "do fm_hw_reset\n");
            break;
        case 'm':
            do_cmd_send(NULL, NULL, 0, FM_COM_CMD_TEST, NULL);
            break;
        default:
            FM_LOG_ERR(D_MAIN, "Para 1 error\n");
            break;
    }
	return count;
}

int fm_priv_register(struct fm_priv *pri, struct fm_op *op)
{
    int ret = 0;
	//Basic functions.

    FM_LOG_NTC(D_INIT,"%s(), [pri=0x%08x][op=0x%08x]\n", __func__, (unsigned int)pri, (unsigned int)op);
	FMR_ASSERT(pri);
	FMR_ASSERT(op);

    priv.priv_tbl.hl_side = pri->priv_tbl.hl_side;
    priv.priv_tbl.adpll_freq_avoid = pri->priv_tbl.adpll_freq_avoid;
	priv.priv_tbl.mcu_freq_avoid = pri->priv_tbl.mcu_freq_avoid;
    priv.priv_tbl.tx_pwr_ctrl = pri->priv_tbl.tx_pwr_ctrl;
    priv.priv_tbl.rtc_drift_ctrl = pri->priv_tbl.rtc_drift_ctrl;
    priv.priv_tbl.tx_desense_wifi = pri->priv_tbl.tx_desense_wifi;
    priv.state = INITED;
    priv.data = NULL;

    op->op_tbl.read = MT6620_read;
    op->op_tbl.write = MT6620_write;
    op->op_tbl.setbits = MT6620_set_bits;
    op->op_tbl.rampdown = MT6620_RampDown;
    op->state = INITED;
    op->data = NULL;
    
	return ret;
}

int fm_priv_unregister(struct fm_priv *pri, struct fm_op *op)
{
	int ret = 0;
	//Basic functions.
	
	FM_LOG_NTC(D_INIT,"%s(), [pri=0x%08x][op=0x%08x]\n", __func__, (unsigned int)pri, (unsigned int)op);
    
	FMR_ASSERT(pri);
	FMR_ASSERT(op);

    priv.priv_tbl.hl_side = NULL;
    priv.priv_tbl.adpll_freq_avoid = NULL;
	priv.priv_tbl.mcu_freq_avoid = NULL;
    priv.priv_tbl.tx_pwr_ctrl = NULL;
    priv.priv_tbl.rtc_drift_ctrl = NULL;
    priv.priv_tbl.tx_desense_wifi = NULL;
    priv.state = UNINITED;
    priv.data = NULL;

    op->op_tbl.read = NULL;
    op->op_tbl.write = NULL;
    op->op_tbl.setbits = NULL;
    op->op_tbl.rampdown = NULL;
    op->state = UNINITED;
    op->data = NULL;
    
	return ret;
}

static int __init mt_fm_probe(void)
{
    int err = -1;
    FM_LOG_NTC(D_INIT,"%s()\n", __func__);

    spin_lock_init(&flag_lock);
    
    if ((err = fm_init())){
        FM_LOG_ALT(D_ALL, "fm_init ERR:%d\n", err);
    }

    return err; 
}

static void __exit mt_fm_remove(void)
{
    FM_LOG_NTC(D_INIT,"%s()\n", __func__);
    fm_destroy(fm_cb);

    return;
}

EXPORT_SYMBOL(fm_priv_register);
EXPORT_SYMBOL(fm_priv_unregister);
EXPORT_SYMBOL(g_dbg_level);

module_init(mt_fm_probe);
module_exit(mt_fm_remove);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek MT6620 FM Driver");
MODULE_AUTHOR("Mike <yunchang.chang@MediaTek.com>");

