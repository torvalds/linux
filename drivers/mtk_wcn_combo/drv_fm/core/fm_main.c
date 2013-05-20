/* fm_main.c
 *
 * (C) Copyright 2011
 * MediaTek <www.MediaTek.com>
 * Hongcheng <hongcheng.xia@MediaTek.com>
 *
 * FM Radio Driver -- main functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/kernel.h>
#include <asm/uaccess.h>
#include <linux/slab.h>

#include "fm_main.h"
#include "fm_config.h"
//#include "fm_cust_cfg.h"
#include "wmt_exp.h"
//fm main data structure
static struct fm *g_fm_struct = NULL;
//we must get low level interface first, when add a new chip, the main effort is this interface
static struct fm_lowlevel_ops fm_low_ops;
#ifdef MT6620_FM
static struct fm_lowlevel_ops MT6620fm_low_ops;
#endif
#ifdef MT6628_FM
static struct fm_lowlevel_ops MT6628fm_low_ops;
#endif
//MTK FM Radio private advanced features
#if 0//(!defined(MT6620_FM)&&!defined(MT6628_FM))
static struct fm_priv priv_adv;
#endif
//mutex for char device ops
static struct fm_lock *fm_ops_lock;
//mutex for RDS parsing and read result
static struct fm_lock *fm_read_lock;
//for get rds block counter
static struct fm_lock *fm_rds_cnt;
//mutex for fm timer, RDS reset
static struct fm_lock *fm_timer_lock;
static struct fm_lock *fm_rxtx_lock;//protect FM RX TX mode switch 
static struct fm_lock *fm_rtc_mutex;//protect FM GPS RTC drift info  

static struct fm_timer *fm_timer_sys;

static struct fm_i2s_info fm_i2s_inf = {
    .status = 0,    //i2s off
    .mode = 0,      //slave mode
    .rate = 48000,  //48000 sample rate
};

static fm_bool scan_stop_flag = fm_false;
static struct fm_gps_rtc_info gps_rtc_info;

//RDS reset related functions
static fm_u16 fm_cur_freq_get(void);
static fm_s32 fm_cur_freq_set(fm_u16 new_freq);
static enum fm_op_state fm_op_state_get(struct fm *fmp);
static enum fm_op_state fm_op_state_set(struct fm *fmp, enum fm_op_state sta);
static void fm_timer_func(unsigned long data);
static void fmtx_timer_func(unsigned long data);

static void fm_enable_rds_BlerCheck(struct fm *fm);
static void fm_disable_rds_BlerCheck(void);
static void fm_rds_reset_work_func(unsigned long data);
//when interrupt be triggered by FM chip, fm_eint_handler will first be executed
//then fm_eint_handler will schedule fm_eint_work_func to run
static void fm_eint_handler(void);
static void fm_eint_work_func(unsigned long data);
static fm_s32 fm_rds_parser(struct rds_rx_t *rds_raw, fm_s32 rds_size);
static fm_s32 fm_callback_register(struct fm_lowlevel_ops *ops);
static fm_s32 fm_callback_unregister(struct fm_lowlevel_ops *ops);

static fm_s32 pwrdown_flow(struct fm *fm);

static 	fm_u16 fm_cur_freq_get(void)
{
    return g_fm_struct ? g_fm_struct->cur_freq : 0;
}

static	fm_s32 fm_cur_freq_set(fm_u16 new_freq)
{
    if (g_fm_struct)
        g_fm_struct->cur_freq = new_freq;

    return 0;
}

static enum fm_op_state fm_op_state_get(struct fm *fmp)
{
    if (fmp) {
        WCN_DBG(FM_DBG | MAIN, "op state get %d\n", fmp->op_sta);
        return fmp->op_sta;
    } else {
        WCN_DBG(FM_ERR | MAIN, "op state get para error\n");
        return FM_STA_UNKOWN;
    }
}

static enum fm_op_state fm_op_state_set(struct fm *fmp, enum fm_op_state sta)
{
    if (fmp && (sta < FM_STA_MAX)) {
        fmp->op_sta = sta;
        WCN_DBG(FM_DBG | MAIN, "op state set to %d\n", sta);
        return fmp->op_sta;
    } else {
        WCN_DBG(FM_ERR | MAIN, "op state set para error, %d\n", sta);
        return FM_STA_UNKOWN;
    }
}

enum fm_pwr_state fm_pwr_state_get(struct fm *fmp)
{
    if (fmp) {
        WCN_DBG(FM_DBG | MAIN, "pwr state get %d\n", fmp->pwr_sta);
        return fmp->pwr_sta;
    } else {
        WCN_DBG(FM_ERR | MAIN, "pwr state get para error\n");
        return FM_PWR_MAX;
    }
}

enum fm_pwr_state fm_pwr_state_set(struct fm *fmp, enum fm_pwr_state sta)
{
    if (fmp && (sta < FM_PWR_MAX)) {
        fmp->pwr_sta = sta;
        WCN_DBG(FM_NTC | MAIN, "pwr state set to %d\n", sta);
        return fmp->pwr_sta;
    } else {
        WCN_DBG(FM_ERR | MAIN, "pwr state set para error, %d\n", sta);
        return FM_PWR_MAX;
    }
}

static volatile fm_s32 subsys_rst_state = FM_SUBSYS_RST_OFF;

fm_s32 fm_sys_state_get(struct fm *fmp)
{
    return subsys_rst_state;
}

fm_s32 fm_sys_state_set(struct fm *fmp, fm_s32 sta)
{
    if ((sta >= FM_SUBSYS_RST_OFF) && (sta < FM_SUBSYS_RST_MAX)) {
        WCN_DBG(FM_NTC | MAIN, "sys state set from %d to %d\n", subsys_rst_state, sta);
        subsys_rst_state = sta;
    } else {
        WCN_DBG(FM_ERR | MAIN, "sys state set para error, %d\n", sta);
    }

    return subsys_rst_state;
}


fm_s32 fm_subsys_reset(struct fm *fm)
{
    //check if we are resetting
    if (fm_sys_state_get(fm) != FM_SUBSYS_RST_OFF) {
        WCN_DBG(FM_NTC | MAIN, "subsys reset is ongoing\n");
        goto out;
    }

    FMR_ASSERT(fm);
    fm->timer_wkthd->add_work(fm->timer_wkthd, fm->rst_wk);

out:
    return 0;
}


fm_s32 fm_wholechip_rst_cb(fm_s32 sta)
{
    struct fm *fm = g_fm_struct;
    
    if (!fm) return 0;

    if (sta == 1) { 
        if (fm_sys_state_get(fm) == FM_SUBSYS_RST_OFF) {
            fm_sys_state_set(fm, FM_SUBSYS_RST_START);
        }
    } else {
        fm->timer_wkthd->add_work(fm->timer_wkthd, fm->rst_wk);
    }
    return 0;
}


fm_s32 fm_open(struct fm *fmp)
{
    fm_s32 ret = 0;
#if (defined(MT6620_FM)||defined(MT6628_FM))
    fm_s32 chipid;
#endif
    FMR_ASSERT(fmp);
	if (current->pid == 1)
		return 0;
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    //makesure fmp->ref >= 0
    fmp->ref = (fmp->ref < 0) ? 0 : fmp->ref;
    fmp->ref++;

    if ((fmp->ref > 0) && (fmp->chipon == fm_false)) 
	{
#if (defined(MT6620_FM)||defined(MT6628_FM))
		chipid = mtk_wcn_wmt_chipid_query();
		WCN_DBG(FM_NTC | MAIN, "wmt chip id=0x%x\n",chipid);
		if(chipid == 0x6628)//get WCN chip ID
		{
#ifdef MT6628_FM
			fm_low_ops = MT6628fm_low_ops;
			fmp->chip_id = 0x6628;
			WCN_DBG(FM_NTC | MAIN, "get 6628 low ops\n");
#endif
		}
		else if(chipid == 0x6620)
		{
#ifdef MT6620_FM
			fm_low_ops = MT6620fm_low_ops;
			fmp->chip_id = 0x6620;
			WCN_DBG(FM_NTC | MAIN, "get 6620 low ops\n");
#endif
		}
#endif
		if(fm_low_ops.bi.pwron == NULL)
		{
			WCN_DBG(FM_NTC | MAIN, "get fm_low_ops fail\n");
            fmp->ref--;
            ret = -ENODEV;
            goto out;
        }

        ret = fm_low_ops.bi.pwron(0);
        if (ret) {
            fmp->ref--;
            ret = -ENODEV;
            goto out;
        }

        fm_eint_pin_cfg(FM_EINT_PIN_EINT_MODE);
        fm_request_eint(fm_eint_handler);
        fmp->chipon = fm_true;
    }

out:
    WCN_DBG(FM_NTC | MAIN, "fm->ref:%d\n", fmp->ref);
    FM_UNLOCK(fm_ops_lock);
    return ret;
}

fm_s32 fm_close(struct fm *fmp)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fmp);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    fmp->ref--;

    if (fmp->ref == 0) {
        pwrdown_flow(fmp);
        if (fmp->chipon == fm_true) {
            fm_eint_pin_cfg(FM_EINT_PIN_GPIO_MODE);
            fm_low_ops.bi.pwroff(0);
            fmp->chipon = fm_false;
        }
    }

    //makesure fm->ref >= 0
    fmp->ref = (fmp->ref < 0) ? 0 : fmp->ref;
    WCN_DBG(FM_NTC | MAIN, "fmp->ref:%d\n", fmp->ref);
    FM_UNLOCK(fm_ops_lock);

    return ret;
}
/*
fm_s32 fm_flush(struct fm *fmp)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fmp);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    if (FM_PWR_OFF == fm_pwr_state_get(fmp)) 
    {
        WCN_DBG(FM_NTC | MAIN, "should power off combo!\n");
        if (fmp->chipon == fm_true) 
        {
            fm_low_ops.bi.pwroff(0);
            fmp->chipon = fm_false;
        }
    }
    WCN_DBG(FM_NTC | MAIN, "fm_flush done\n");
    FM_UNLOCK(fm_ops_lock);

    return ret;
}
*/
fm_s32 fm_rds_read(struct fm *fmp, fm_s8 *dst, fm_s32 len)
{
    fm_s32 copy_len = 0, left = 0;
    copy_len = sizeof(rds_t);

RESTART:

    if (FM_EVENT_GET(fmp->rds_event) == FM_RDS_DATA_READY) {
        if (FM_LOCK(fm_read_lock)) return (-FM_ELOCK);

        if ((left = copy_to_user((void *)dst, fmp->pstRDSData, (unsigned long)copy_len))) {
            WCN_DBG(FM_ALT | MAIN, "fm_read copy failed\n");
        } else {
            fmp->pstRDSData->event_status = 0x0000;
        }

        WCN_DBG(FM_DBG | MAIN, "fm_read copy len:%d\n", (copy_len - left));

        FM_EVENT_RESET(fmp->rds_event);
        FM_UNLOCK(fm_read_lock);
    } else {
        if (FM_EVENT_WAIT(fmp->rds_event, FM_RDS_DATA_READY) == 0) {
            WCN_DBG(FM_DBG | MAIN, "fm_read wait ok\n");
            goto RESTART;
        } else {
            WCN_DBG(FM_ALT | MAIN, "fm_read wait err\n");
            return 0;
        }
    }

    return (copy_len - left);
}

fm_s32 fm_powerup(struct fm *fm, struct fm_tune_parm *parm)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.bi.pwron);
    FMR_ASSERT(fm_low_ops.bi.pwrupseq);
    FMR_ASSERT(fm_low_ops.bi.low_pwr_wa);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    //for normal case
    if (fm->chipon == fm_false) {
        fm_low_ops.bi.pwron(0);
        fm->chipon = fm_true;
    }

    if (FM_PWR_RX_ON == fm_pwr_state_get(fm)) {
        WCN_DBG(FM_NTC | MAIN, "already pwron!\n");
        goto out;
    }
	else if(FM_PWR_TX_ON == fm_pwr_state_get(fm))
	{
        //if Tx is on, we need pwr down TX first
        ret = fm_powerdowntx(fm);
        if(ret)
		{
           WCN_DBG(FM_ERR | MAIN,"FM pwr down Tx fail!\n"); 
           return ret;
        }
    }

    fm_pwr_state_set(fm, FM_PWR_RX_ON);

    //execute power on sequence
    ret = fm_low_ops.bi.pwrupseq(&fm->chip_id, &fm->device_id);

    if (ret) {
        goto out;
    }

    fm_enable_eint();

    WCN_DBG(FM_DBG | MAIN, "pwron ok\n");
    fm_cur_freq_set(parm->freq);

    parm->err = FM_SUCCESS;
    fm_low_ops.bi.low_pwr_wa(1);

    fm->vol = 15;
	if(fm_low_ops.ri.rds_bci_get)
	{
		fm_timer_sys->init(fm_timer_sys, fm_timer_func, (unsigned long)g_fm_struct, fm_low_ops.ri.rds_bci_get(), 0);
		fm_timer_sys->start(fm_timer_sys);
		WCN_DBG(FM_NTC | MAIN, "start timer ok\n");
	}
	else
	{
		WCN_DBG(FM_NTC | MAIN, "start timer fail!!!\n");
	}

out:
    FM_UNLOCK(fm_ops_lock);
    return ret;
}

/*
 *  fm_powerup_tx
 */
fm_s32 fm_powerup_tx(struct fm *fm, struct fm_tune_parm *parm)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.bi.pwron);
    FMR_ASSERT(fm_low_ops.bi.pwrupseq_tx);

    if (FM_PWR_TX_ON == fm_pwr_state_get(fm)) {
        WCN_DBG(FM_NTC | MAIN, "already pwron!\n");
        parm->err = FM_BADSTATUS;
        goto out;
    }
	else if(FM_PWR_RX_ON == fm_pwr_state_get(fm))
	{
        //if Rx is on, we need pwr down  first
        ret = fm_powerdown(fm);
        if(ret)
		{
           WCN_DBG(FM_ERR | MAIN,"FM pwr down Rx fail!\n"); 
		   goto out;
        }
    }

    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    //for normal case
    if (fm->chipon == fm_false) {
        fm_low_ops.bi.pwron(0);
        fm->chipon = fm_true;
    }
    
    fm_pwr_state_set(fm, FM_PWR_TX_ON);
	ret = fm_low_ops.bi.pwrupseq_tx();

	if(ret)
	{
		parm->err = FM_FAILED; 
	}
	else
	{
		parm->err = FM_SUCCESS; 
	}
    fm_cur_freq_set(parm->freq);
	//if(fm_low_ops.ri.rds_bci_get)
	{
		fm_timer_sys->count = 0;
		fm_timer_sys->tx_pwr_ctrl_en = FM_TX_PWR_CTRL_ENABLE;
		fm_timer_sys->tx_rtc_ctrl_en = FM_TX_RTC_CTRL_ENABLE;
		fm_timer_sys->tx_desense_en = FM_TX_DESENSE_ENABLE;
	
		fm_timer_sys->init(fm_timer_sys, fmtx_timer_func, (unsigned long)g_fm_struct,FM_TIMER_TIMEOUT_MIN, 0);
		fm_timer_sys->start(fm_timer_sys);
		WCN_DBG(FM_NTC | MAIN, "start timer ok\n");
	}

out:
    FM_UNLOCK(fm_ops_lock);
    return ret;
}

static fm_s32 pwrdown_flow(struct fm *fm)
{
    fm_s32 ret = 0;
    FMR_ASSERT(fm_low_ops.ri.rds_onoff);
    FMR_ASSERT(fm_low_ops.bi.pwrdownseq);
    FMR_ASSERT(fm_low_ops.bi.low_pwr_wa);
    if (FM_PWR_OFF == fm_pwr_state_get(fm)) {
        WCN_DBG(FM_NTC | MAIN, "already pwroff!\n");
        goto out;
    }
	/*if(fm_low_ops.ri.rds_bci_get)
	{
		fm_timer_sys->stop(fm_timer_sys);
		WCN_DBG(FM_NTC | MAIN, "stop timer ok\n");
	}
	else
	{
		WCN_DBG(FM_NTC | MAIN, "stop timer fail!!!\n");
	}*/
    
    //Disable all interrupt
    fm_disable_rds_BlerCheck();
    fm_low_ops.ri.rds_onoff(fm->pstRDSData, fm_false);
    fm_disable_eint();

    fm_pwr_state_set(fm, FM_PWR_OFF);

    //execute power down sequence
    ret = fm_low_ops.bi.pwrdownseq();

    fm_low_ops.bi.low_pwr_wa(0);
    WCN_DBG(FM_ALT | MAIN, "pwrdown_flow exit\n");

out:
    return ret;
}

fm_s32 fm_powerdown(struct fm *fm)
{
    fm_s32 ret = 0;
	if (FM_PWR_TX_ON == fm_pwr_state_get(fm))
	{
	    ret = fm_powerdowntx(fm);
	}
	else
	{
	    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);
	    if (FM_LOCK(fm_rxtx_lock)) return(-FM_ELOCK);

	    ret = pwrdown_flow(fm);

	    FM_UNLOCK(fm_rxtx_lock);
	    FM_UNLOCK(fm_ops_lock);
    }
    return ret;
}

fm_s32 fm_powerdowntx(struct fm *fm)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.bi.pwrdownseq_tx);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);
    if (FM_LOCK(fm_rxtx_lock)) return (-FM_ELOCK);

    if (FM_PWR_TX_ON == fm_pwr_state_get(fm)) 
    {
		if (FM_LOCK(fm_timer_lock)) return (-FM_ELOCK);
		fm_timer_sys->stop(fm_timer_sys);
		FM_UNLOCK(fm_timer_lock);
		fm_timer_sys->count = 0;
		fm_timer_sys->tx_pwr_ctrl_en = FM_TX_PWR_CTRL_DISABLE;
		fm_timer_sys->tx_rtc_ctrl_en = FM_TX_RTC_CTRL_DISABLE;
		fm_timer_sys->tx_desense_en = FM_TX_DESENSE_DISABLE;
		
		fm_low_ops.ri.rds_onoff(fm->pstRDSData, fm_false);
		//execute power down sequence
		ret = fm_low_ops.bi.pwrdownseq();
		if(ret)
		{
			WCN_DBG(FM_ERR | MAIN, "pwrdown tx 1 fail\n");
		}
		else
		{
			ret = fm_low_ops.bi.pwrdownseq_tx();
			if(ret)
			{
				WCN_DBG(FM_ERR | MAIN, "pwrdown tx 2 fail\n");
			}
		}
		fm_pwr_state_set(fm, FM_PWR_OFF);
		WCN_DBG(FM_NTC | MAIN, "pwrdown tx ok\n");
    }

    FM_UNLOCK(fm_rxtx_lock);
    FM_UNLOCK(fm_ops_lock);
    return ret;
}

fm_s32 fm_seek(struct fm *fm, struct fm_seek_parm *parm)
{
    fm_s32 ret = 0;
    fm_u16 seekdir, space;

    FMR_ASSERT(fm_low_ops.bi.seek);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    if (fm_pwr_state_get(fm) != FM_PWR_RX_ON) {
        parm->err = FM_BADSTATUS;
        ret = -EPERM;
        goto out;
    }

    if (parm->space == FM_SPACE_100K) {
        space = 0x0002;
    } else if (parm->space == FM_SPACE_50K) {
        space = 0x0001;
    } else if (parm->space == FM_SPACE_200K) {
        space = 0x0004;
    } else {
        //default
        space = 0x0002;
    }

    if (parm->band == FM_BAND_UE) {
        fm->min_freq = FM_UE_FREQ_MIN;
        fm->max_freq = FM_UE_FREQ_MAX;
    } else if (parm->band == FM_BAND_JAPANW) {
        fm->min_freq = FM_JP_FREQ_MIN;
        fm->max_freq = FM_JP_FREQ_MAX;
    } else if (parm->band == FM_BAND_SPECIAL) {
        fm->min_freq = FM_RX_BAND_FREQ_L;
        fm->max_freq = FM_RX_BAND_FREQ_H;
    } else {
        WCN_DBG(FM_ALT | MAIN, "band:%d out of range\n", parm->band);
        parm->err = FM_EPARM;
        ret = -EPERM;
        goto out;
    }

    if (parm->freq < fm->min_freq || parm->freq > fm->max_freq) {
        WCN_DBG(FM_ALT | MAIN, "freq:%d out of range\n", parm->freq);
        parm->err = FM_EPARM;
        ret = -EPERM;
        goto out;
    }

    if (parm->seekdir == FM_SEEK_UP) {
        seekdir = FM_SEEK_UP;
    } else {
        seekdir = FM_SEEK_DOWN;
    }

    fm_op_state_set(fm, FM_STA_SEEK);

    // seek successfully
    if (fm_true == fm_low_ops.bi.seek(fm->min_freq, fm->max_freq, &(parm->freq), seekdir, space)) {
        parm->err = FM_SUCCESS;
    } else {
        parm->err = FM_SEEK_FAILED;
        ret = -EPERM;
    }

    if ((parm->space != FM_SPACE_50K) && (1 == fm_get_channel_space(parm->freq))) 
    {
        parm->freq /= 10; //(8750 / 10) = 875
    }

    fm_op_state_set(fm, FM_STA_PLAY);
out:
    FM_UNLOCK(fm_ops_lock);
    return ret;
}

/***********************************************************
Function: 	fm_tx_scan()

Description: 	get the valid channels for fm tx function

Para: 		fm--->fm driver global info
			parm--->input/output paramater
			
Return: 		0, if success; error code, if failed
***********************************************************/
fm_s32 fm_tx_scan(struct fm *fm, struct fm_tx_scan_parm *parm)
{
    fm_s32 ret = 0;
	fm_u16 scandir = 0;
	fm_u16 space = FM_SPACE_100K;
 
	FMR_ASSERT(fm_low_ops.bi.tx_scan);

	if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

	if (fm->chipon != fm_true) 
	{
		parm->err = FM_BADSTATUS;
		ret=-EPERM;
		WCN_DBG(FM_ERR | MAIN, "tx scan chip not on\n");
		goto out;
	}
	switch(parm->scandir){
		case FM_TX_SCAN_UP:
			scandir = 0;
			break;
		case FM_TX_SCAN_DOWN:
			scandir = 1;
			break;
		default:
			scandir = 0;
			break;
	}
	
	/*if (parm->space == FM_SPACE_100K) {
        space = 2;
    } else if (parm->space == FM_SPACE_50K) {
        space = 1;
    } else if (parm->space == FM_SPACE_200K) {
        space = 4;
    } else {
        //default
        space = 2;
	}*/
	
    if (parm->band == FM_BAND_UE) {
        fm->min_freq = FM_UE_FREQ_MIN;
        fm->max_freq = FM_UE_FREQ_MAX;
    } else if (parm->band == FM_BAND_JAPANW) {
        fm->min_freq = FM_JP_FREQ_MIN;
        fm->max_freq = FM_JP_FREQ_MAX;
    } else if (parm->band == FM_BAND_SPECIAL) {
        fm->min_freq = FM_FREQ_MIN;
        fm->max_freq = FM_FREQ_MAX;
    } else 
    {
        WCN_DBG(FM_ERR| MAIN,"band:%d out of range\n", parm->band);
		parm->err = FM_EPARM;
		ret=-EPERM;
		goto out;
	}

    if (unlikely((parm->freq < fm->min_freq) || (parm->freq > fm->max_freq))){
        parm->err = FM_EPARM;
        ret=-EPERM;
        goto out;
    }

	if (unlikely(parm->ScanTBLSize < TX_SCAN_MIN || parm->ScanTBLSize > TX_SCAN_MAX)){
        parm->err = FM_EPARM;
        ret=-EPERM;
        goto out;
    }

    ret = fm_low_ops.bi.anaswitch(FM_ANA_SHORT);
    if(ret){
        WCN_DBG(FM_ERR | MAIN,"switch to short ana failed\n");
        goto out;
    }
    
	//do tx scan
	if(!(ret = fm_low_ops.bi.tx_scan(fm->min_freq, fm->max_freq, &(parm->freq), 
								parm->ScanTBL, &(parm->ScanTBLSize), scandir, space))){
        parm->err = FM_SUCCESS;
    }else{
        WCN_DBG(FM_ERR | MAIN,"fm_tx_scan failed\n");
        parm->err = FM_SCAN_FAILED;
    }
out:    
	FM_UNLOCK(fm_ops_lock);
    return ret;
}

fm_s32  fm_scan(struct fm *fm, struct fm_scan_parm *parm)
{
    fm_s32 ret = 0;
    fm_u16 scandir = FM_SEEK_UP, space;

    FMR_ASSERT(fm_low_ops.bi.scan);
	WCN_DBG(FM_NTC | MAIN, "fm_scan:start\n");
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    if (fm_pwr_state_get(fm) != FM_PWR_RX_ON) {
        parm->err = FM_BADSTATUS;
        ret = -EPERM;
        goto out;
    }

    if (parm->space == FM_SPACE_100K) {
        space = 0x0002;
    } else if (parm->space == FM_SPACE_50K) {
        space = 0x0001;
    } else if (parm->space == FM_SPACE_200K) {
        space = 0x0004;
    } else {
        //default
        space = 0x0002;
    }

    if (parm->band == FM_BAND_UE) {
        fm->min_freq = FM_UE_FREQ_MIN;
        fm->max_freq = FM_UE_FREQ_MAX;
    } else if (parm->band == FM_BAND_JAPANW) {
        fm->min_freq = FM_JP_FREQ_MIN;
        fm->max_freq = FM_JP_FREQ_MAX;
    } else if (parm->band == FM_BAND_SPECIAL) {
        fm->min_freq = FM_RX_BAND_FREQ_L;
        fm->max_freq = FM_RX_BAND_FREQ_H;
    } else {
        WCN_DBG(FM_ALT | MAIN, "band:%d out of range\n", parm->band);
        parm->err = FM_EPARM;
        ret = -EPERM;
        goto out;
    }

    fm_op_state_set(fm, FM_STA_SCAN);
    scan_stop_flag = fm_false;

    if (fm_true == fm_low_ops.bi.scan(fm->min_freq, fm->max_freq, &(parm->freq), parm->ScanTBL, &(parm->ScanTBLSize), scandir, space)) {
        parm->err = FM_SUCCESS;
    } else {
        WCN_DBG(FM_ALT | MAIN, "fm_scan failed\n");
        parm->err = FM_SEEK_FAILED;
        ret = -EPERM;
    }

    fm_op_state_set(fm, FM_STA_STOP);

out:
    FM_UNLOCK(fm_ops_lock);
	WCN_DBG(FM_NTC | MAIN, "fm_scan:done\n");
    return ret;
}


#define SCAN_SEG_LEN 250
static struct fm_cqi cqi_buf[SCAN_SEG_LEN];

fm_s32 fm_scan_new(struct fm *fm, struct fm_scan_t *parm)
{
    fm_s32 ret = 0;
    fm_s32 tmp;
    fm_s32 cnt, seg;
    fm_s32 i, j;
    fm_s32 start_freq, end_freq;
    fm_u16 scan_tbl[FM_SCANTBL_SIZE]; //need no less than the chip
    fm_u16 tbl_size = FM_SCANTBL_SIZE;
    fm_u16 tmp_freq = 0;
    fm_s32 chl_cnt;
    fm_s32 ch_offset, step, tmp_val;
    fm_u16 space_idx = 0x0002;
    
    fm_s32 cqi_cnt, cqi_idx;
    fm_s8 *buf = (fm_s8*)cqi_buf;
    
    FMR_ASSERT(fm_low_ops.bi.scan);
    FMR_ASSERT(fm_low_ops.bi.cqi_get);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    // caculate channel number, get segment count
    cnt = (parm->upper - parm->lower) / parm->space + 1; //Eg, (10800 - 8750) / 5 = 411
    seg = (cnt / SCAN_SEG_LEN) + ((cnt % SCAN_SEG_LEN) ? 1 : 0); //Eg, (411 / 200) + ((411 % 200) ? 1 : 0) = 2 + 1 = 3

    WCN_DBG(FM_NTC | MAIN, "total ch %d, seg %d\n", cnt, seg);

    // alloc memory
    tmp = cnt * sizeof(struct fm_ch_rssi*);
    if (parm->sr_size < tmp) {
        if (parm->sr.ch_rssi_buf) {
            fm_free(parm->sr.ch_rssi_buf);
            parm->sr.ch_rssi_buf = NULL;
        }
        parm->sr_size = tmp;                
    }
    
    if (!parm->sr.ch_rssi_buf) {
        parm->sr.ch_rssi_buf = (struct fm_ch_rssi*)fm_zalloc(parm->sr_size);
        if (!parm->sr.ch_rssi_buf) {
            WCN_DBG(FM_ERR | MAIN, "scan alloc mem failed\n");
            parm->sr_size = 0;
            return -2;
        }
    }
    
    if (parm->space == 5) {
        space_idx = 0x0001; // 50Khz
    } else if (parm->space == 10) {
        space_idx = 0x0002; // 100Khz
    } else if (parm->space == 20) {
        space_idx = 0x0004; // 200Khz
    } 

    
    fm_op_state_set(fm, FM_STA_SCAN);
    
    // do scan
    chl_cnt = 0;
    for (i = 0; (i < seg) && (fm_false == scan_stop_flag); i++) {
        cqi_cnt = 0;
        cqi_idx = 0;
        
        start_freq = parm->lower + SCAN_SEG_LEN * parm->space * i;
        end_freq = parm->lower + SCAN_SEG_LEN * parm->space * (i + 1) - parm->space;
        end_freq = (end_freq > parm->upper) ? parm->upper : end_freq;

        WCN_DBG(FM_NTC | MAIN, "seg %d, start %d, end %d\n", i, start_freq, end_freq);
        if(fm_false == fm_low_ops.bi.scan(start_freq, end_freq, &tmp_freq, scan_tbl, &tbl_size, FM_SEEK_UP, space_idx)) {
            ret = -1;
            goto out;
        }

        // get channel count
        for (ch_offset = 0; ch_offset < FM_SCANTBL_SIZE; ch_offset++) {
		    if (scan_tbl[ch_offset] == 0)
			    continue;
		    for (step = 0; step < 16; step++) {
			    if (scan_tbl[ch_offset] & (1 << step)) {
                    tmp_val =  start_freq + (ch_offset * 16 + step) * parm->space;
                    if (tmp_val <= end_freq) {
                        // record valid  result channel
                        WCN_DBG(FM_NTC | MAIN, "freq %d\n", tmp_val);
                        parm->sr.ch_rssi_buf[chl_cnt].freq = tmp_val;
                        chl_cnt++; 
                        cqi_cnt++;
                    }
			    }
		    }
	    }  

        // get cqi
        if(fm_low_ops.bi.cqi_get)
        {
	        tmp = cqi_cnt;
	        while ((cqi_cnt > 0) && (fm_false == scan_stop_flag)) {
	            ret = fm_low_ops.bi.cqi_get(buf + (16 * sizeof(struct fm_cqi) * cqi_idx), 
	                sizeof(cqi_buf) - (16 * sizeof(struct fm_cqi) * cqi_idx));
	            if (ret) {
	                goto out;
	            }

	            cqi_cnt -= 16;
	            cqi_idx++;
	        }
	        cqi_cnt = tmp;

	        // fill cqi to result buffer
	        for (j = 0; j < cqi_cnt; j++) {
	            tmp = chl_cnt - cqi_cnt + j; // target pos
	            parm->sr.ch_rssi_buf[tmp].freq = (fm_u16)cqi_buf[j].ch;
	            parm->sr.ch_rssi_buf[tmp].rssi= cqi_buf[j].rssi;
	            WCN_DBG(FM_NTC | MAIN, "idx %d, freq %d, rssi %d \n", tmp, parm->sr.ch_rssi_buf[tmp].freq, parm->sr.ch_rssi_buf[tmp].rssi);
	        }
        }
        //6620 won't get rssi in scan new
        //else if(fm_low_ops.bi.rssiget)
    }
     
    fm_op_state_set(fm, FM_STA_STOP);
    
out:
    scan_stop_flag = fm_false;
    FM_UNLOCK(fm_ops_lock);
    parm->num = chl_cnt;
    return ret;
}


fm_s32 fm_seek_new(struct fm *fm, struct fm_seek_t *parm)
{
    fm_s32 ret = 0;
    fm_s32 space_idx = 0x0002;

    FMR_ASSERT(fm_low_ops.bi.setfreq);
    FMR_ASSERT(fm_low_ops.bi.rssiget);
    FMR_ASSERT(fm_low_ops.bi.rampdown);

    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);
     
    if (parm->freq < parm->lower || parm->freq > parm->upper) {
        WCN_DBG(FM_ERR | MAIN, "seek start freq:%d out of range\n", parm->freq);
        ret = -EPERM;
        goto out;
    }

    // tune to start freq
    fm_low_ops.bi.rampdown();
    fm_low_ops.bi.setfreq(parm->freq);

    if (parm->space == 5) {
        space_idx = 0x0001;
    } else if (parm->space == 10) {
        space_idx = 0x0002;
    } else if (parm->space == 20) {
        space_idx = 0x0004;
    }
    
    fm_op_state_set(fm, FM_STA_SEEK);
    if (fm_false == fm_low_ops.bi.seek(parm->lower, parm->upper, &(parm->freq), parm->dir, space_idx)) {
        ret = -1;
        goto out;
    } 

    // tune to new channel
    fm_low_ops.bi.setfreq(parm->freq);
    fm_low_ops.bi.rssiget(&parm->th);
    
out:
    FM_UNLOCK(fm_ops_lock);
    return ret;    
}


fm_s32 fm_tune_new(struct fm *fm, struct fm_tune_t *parm)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.bi.mute);
    FMR_ASSERT(fm_low_ops.bi.rampdown);
    FMR_ASSERT(fm_low_ops.bi.setfreq);

    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    WCN_DBG(FM_DBG | MAIN, "%s\n", __func__);

    if (fm_pwr_state_get(fm) != FM_PWR_RX_ON) {
        ret = -EPERM;
        goto out;
    }

    if (parm->freq < parm->lower || parm->freq > parm->upper) {
        WCN_DBG(FM_ERR | MAIN, "tune freq:%d out of range\n", parm->freq);
        ret = -EPERM;
        goto out;
    }
    
//    fm_low_ops.bi.mute(fm_true);
    fm_low_ops.bi.rampdown();

    if (fm_cur_freq_get() != parm->freq) {
        fm_memset(fm->pstRDSData, 0, sizeof(rds_t));
    }

#if 0//(!defined(MT6620_FM)&&!defined(MT6628_FM))
    //HILO side adjust if need
    if (priv_adv.priv_tbl.hl_dese) {
        if ((ret = priv_adv.priv_tbl.hl_dese(parm->freq, NULL)) < 0) {
            goto out;
        }

        WCN_DBG(FM_INF | MAIN, "HILO side %d\n", ret);
    }

    //Frequency avoid adjust if need
    if (priv_adv.priv_tbl.fa_dese) {
        if ((ret = priv_adv.priv_tbl.fa_dese(parm->freq, NULL)) < 0) {
            goto out;
        }

        WCN_DBG(FM_INF | MAIN, "FA %d\n", ret);
    }

    //MCU clock adjust if need
    if (priv_adv.priv_tbl.mcu_dese) {
        if ((ret = priv_adv.priv_tbl.mcu_dese(parm->freq, NULL)) < 0) {
            goto out;
        }

        WCN_DBG(FM_INF | MAIN, "MCU %d\n", ret);
    }

    //GPS clock adjust if need
    if (priv_adv.priv_tbl.gps_dese) {
        if ((ret = priv_adv.priv_tbl.gps_dese(parm->freq, NULL)) < 0) {
            goto out;
        }

        WCN_DBG(FM_INF | MAIN, "GPS %d\n", ret);
    }
#endif
    fm_op_state_set(fm, FM_STA_TUNE);
    WCN_DBG(FM_ALT | MAIN, "tuning to %d\n", parm->freq);

    if (fm_false == fm_low_ops.bi.setfreq(parm->freq)) {
        WCN_DBG(FM_ALT | MAIN, "FM tune failed\n");
        ret = -EPERM;
        goto out;
    }

//    fm_low_ops.bi.mute(fm_false);
    fm_op_state_set(fm, FM_STA_PLAY);
out:
    FM_UNLOCK(fm_ops_lock);
    return ret;
}


fm_s32  fm_cqi_get(struct fm *fm, fm_s32 ch_num, fm_s8 *buf, fm_s32 buf_size)
{
    fm_s32 ret = 0;
    fm_s32 idx = 0;

    FMR_ASSERT(fm_low_ops.bi.cqi_get);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    if (fm_true == scan_stop_flag) {
        WCN_DBG(FM_NTC | MAIN, "scan flow aborted, do not get CQI\n");
        ret = -1;
        goto out;
    }
    
    if (fm_pwr_state_get(fm) != FM_PWR_RX_ON) {
        ret = -EPERM;
        goto out;
    }

    if (ch_num*sizeof(struct fm_cqi) > buf_size) {
        ret = -EPERM;
        goto out;
    }

    fm_op_state_set(fm, FM_STA_SCAN);

    idx = 0;
    WCN_DBG(FM_NTC | MAIN, "cqi num %d\n", ch_num);

    while (ch_num > 0) {
        ret = fm_low_ops.bi.cqi_get(buf + 16 * sizeof(struct fm_cqi) * idx, buf_size - 16 * sizeof(struct fm_cqi) * idx);

        if (ret) {
            goto out;
        }

        ch_num -= 16;
        idx++;
    }

    fm_op_state_set(fm, FM_STA_STOP);

out:
    FM_UNLOCK(fm_ops_lock);
    return ret;
}


/*  fm_is_dese_chan -- check if gived channel is a de-sense channel or not
  *  @pfm - fm driver global DS
  *  @freq - gived channel
  *  return value: 0, not a dese chan; 1, a dese chan; else error NO.
  */
fm_s32 fm_is_dese_chan(struct fm *pfm, fm_u16 freq)
{
    fm_s32 ret = 0;
    FMR_ASSERT(pfm);
	if (fm_low_ops.bi.is_dese_chan) 
	{
        if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);
		ret = fm_low_ops.bi.is_dese_chan(freq);
        FM_UNLOCK(fm_ops_lock);
	}

	return ret;
}


/*  fm_is_dese_chan -- check if gived channel is a de-sense channel or not
  *  @pfm - fm driver global DS
  *  @freq - gived channel
  *  return value: 0, not a dese chan; 1, a dese chan; else error NO.
  */
fm_s32 fm_desense_check(struct fm *pfm, fm_u16 freq,fm_s32 rssi)
{
    fm_s32 ret = 0;
    FMR_ASSERT(pfm);
    if (fm_low_ops.bi.desense_check) 
	{
        if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);
        ret = fm_low_ops.bi.desense_check(freq,rssi);
        FM_UNLOCK(fm_ops_lock);
    }

    return ret;
}
fm_s32 fm_dump_reg(void)
{
    fm_s32 ret = 0;
    if (fm_low_ops.bi.dumpreg) 
    {
	    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);
        ret = fm_low_ops.bi.dumpreg();
	    FM_UNLOCK(fm_ops_lock);
    }
    return ret;
}

/*  fm_get_hw_info -- hw info: chip id, ECO version, DSP ROM version, Patch version
  *  @pfm - fm driver global DS
  *  @freq - target buffer
  *  return value: 0, success; else error NO.
  */
fm_s32 fm_get_hw_info(struct fm *pfm, struct fm_hw_info *req)
{
    fm_s32 ret = 0;

    FMR_ASSERT(req);

    //default value for all chips
    req->chip_id = 0x000066FF;
    req->eco_ver = 0x00000000;
    req->rom_ver = 0x00000001;
    req->patch_ver = 0x00000100;
    req->reserve = 0x00000000;

    //get actual chip hw info
    if (fm_low_ops.bi.hwinfo_get) {
        if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);
        ret = fm_low_ops.bi.hwinfo_get(req);
        FM_UNLOCK(fm_ops_lock);
    }

    return ret;
}


/*  fm_get_i2s_info -- i2s info: on/off, master/slave, sample rate
  *  @pfm - fm driver global DS
  *  @freq - target buffer
  *  return value: 0, success; else error NO.
  */
fm_s32 fm_get_i2s_info(struct fm *pfm, struct fm_i2s_info *req)
{
    FMR_ASSERT(req);

    if (fm_low_ops.bi.i2s_get) {
        return fm_low_ops.bi.i2s_get(&req->status, &req->mode, &req->rate);
    } else {
        req->status = fm_i2s_inf.status;
        req->mode = fm_i2s_inf.mode;
        req->rate = fm_i2s_inf.rate;

        return 0;
    }
}


fm_s32  fm_hwscan_stop(struct fm *fm)
{
    fm_s32 ret = 0;

    if ((FM_STA_SCAN != fm_op_state_get(fm))&&(FM_STA_SEEK!=fm_op_state_get(fm))) 
	{
        WCN_DBG(FM_WAR | MAIN, "fm isn't on scan, no need stop\n");
        return ret;
    }

    FMR_ASSERT(fm_low_ops.bi.scanstop);

    fm_low_ops.bi.scanstop();
    fm_low_ops.bi.seekstop();
    scan_stop_flag = fm_true;
    WCN_DBG(FM_DBG | MAIN, "fm will stop scan\n");

    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);
    
    fm_low_ops.bi.rampdown();
    fm_low_ops.bi.setfreq(fm_cur_freq_get());

    FM_UNLOCK(fm_ops_lock);
    
    return ret;
}

/* fm_ana_switch -- switch antenna to long/short
 * @fm - fm driver main data structure
 * @antenna - 0, long; 1, short
 * If success, return 0; else error code
 */
fm_s32 fm_ana_switch(struct fm *fm, fm_s32 antenna)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.bi.anaswitch);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    WCN_DBG(FM_DBG | MAIN, "Switching ana to %s\n", antenna ? "short" : "long");
    fm->ana_type = antenna;
    ret = fm_low_ops.bi.anaswitch(antenna);

    if (ret) {
        WCN_DBG(FM_ALT | MAIN, "Switch ana Failed\n");
    } else {
        WCN_DBG(FM_DBG | MAIN, "Switch ana OK!\n");
    }

    FM_UNLOCK(fm_ops_lock);
    return ret;
}

//volume?[0~15]
fm_s32 fm_setvol(struct fm *fm, fm_u32 vol)
{
    fm_u8 tmp_vol;

    if (fm_pwr_state_get(fm) != FM_PWR_RX_ON) {
		return -EPERM;
    }
    FMR_ASSERT(fm_low_ops.bi.volset);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    tmp_vol = (vol > 15) ? 15 : vol;
    fm_low_ops.bi.volset(tmp_vol);
    fm->vol = (fm_s32)tmp_vol;

    FM_UNLOCK(fm_ops_lock);
    return 0;
}

fm_s32 fm_getvol(struct fm *fm, fm_u32 *vol)
{
    fm_u8 tmp_vol;

    if (fm_pwr_state_get(fm) != FM_PWR_RX_ON) {
		return -EPERM;
    }
    FMR_ASSERT(fm_low_ops.bi.volget);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    fm_low_ops.bi.volget(&tmp_vol);
    *vol = (fm_u32)tmp_vol;

    FM_UNLOCK(fm_ops_lock);
    return 0;
}

fm_s32 fm_mute(struct fm *fm, fm_u32 bmute)
{
    fm_s32 ret = 0;

    if (fm_pwr_state_get(fm) != FM_PWR_RX_ON) {
        ret = -EPERM;
		return ret;
    }
    FMR_ASSERT(fm_low_ops.bi.mute);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    if (bmute) {
        ret = fm_low_ops.bi.mute(fm_true);
        fm->mute = fm_true;
    } else {
        ret = fm_low_ops.bi.mute(fm_false);
        fm->mute = fm_false;
    }

    FM_UNLOCK(fm_ops_lock);
    return ret;
}

fm_s32 fm_getrssi(struct fm *fm, fm_s32 *rssi)
{
    fm_s32 ret = 0;

    if (fm_pwr_state_get(fm) != FM_PWR_RX_ON) {
        ret = -EPERM;
		return ret;
    }
    FMR_ASSERT(fm_low_ops.bi.rssiget);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    ret = fm_low_ops.bi.rssiget(rssi);

    FM_UNLOCK(fm_ops_lock);
    return ret;
}

fm_s32 fm_reg_read(struct fm *fm, fm_u8 addr, fm_u16 *val)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.bi.read);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    ret = fm_low_ops.bi.read(addr, val);

    FM_UNLOCK(fm_ops_lock);
    return ret;
}

fm_s32 fm_reg_write(struct fm *fm, fm_u8 addr, fm_u16 val)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.bi.write);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    ret = fm_low_ops.bi.write(addr, val);

    FM_UNLOCK(fm_ops_lock);
    return ret;
}

fm_s32 fm_chipid_get(struct fm *fm, fm_u16 *chipid)
{
    FMR_ASSERT(chipid);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    *chipid = fm->chip_id;

    FM_UNLOCK(fm_ops_lock);
    return 0;
}

fm_s32 fm_monostereo_get(struct fm *fm, fm_u16 *ms)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.bi.msget);
    FMR_ASSERT(ms);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    if (fm_low_ops.bi.msget(ms) == fm_false) {
        ret = -FM_EPARA;
    }

    FM_UNLOCK(fm_ops_lock);
    return ret;
}

/*
 * Force set to stero/mono mode
 * @MonoStereo -- 0, auto; 1, mono
 * If success, return 0; else error code
 */
fm_s32 fm_monostereo_set(struct fm *fm, fm_s32 ms)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.bi.msset);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    ret = fm_low_ops.bi.msset(ms);

    FM_UNLOCK(fm_ops_lock);
    return ret;
}

fm_s32 fm_pamd_get(struct fm *fm, fm_u16 *pamd)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.bi.pamdget);
    FMR_ASSERT(pamd);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    if (fm_low_ops.bi.pamdget(pamd) == fm_false) {
        ret = -FM_EPARA;
    }

    FM_UNLOCK(fm_ops_lock);
    return ret;
}

fm_s32 fm_caparray_get(struct fm *fm, fm_s32 *ca)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.bi.caparray_get);
    FMR_ASSERT(ca);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    ret = fm_low_ops.bi.caparray_get(ca);

    FM_UNLOCK(fm_ops_lock);
    return ret;
}

fm_s32 fm_em_test(struct fm *fm, fm_u16 group, fm_u16 item, fm_u32 val)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.bi.em);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    if (fm_false == fm_low_ops.bi.em(group, item, val)) {
        ret = -FM_EPARA;
    }

    FM_UNLOCK(fm_ops_lock);
    return ret;
}

fm_s32 fm_rds_tx(struct fm *fm, struct fm_rds_tx_parm *parm)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.ri.rds_tx);
    FMR_ASSERT(fm_low_ops.ri.rds_tx_enable);

    if (fm_pwr_state_get(fm) != FM_PWR_TX_ON) {
        parm->err = FM_BADSTATUS;
        ret = -FM_EPARA;
        goto out;
    }
    if(parm->other_rds_cnt > 29)
    {
        parm->err = FM_EPARM;
        WCN_DBG(FM_ERR | MAIN,"other_rds_cnt=%d\n",parm->other_rds_cnt);
        ret = -FM_EPARA;
        goto out;
    }
    
    ret = fm_low_ops.ri.rds_tx_enable();
    if(ret){
        WCN_DBG(FM_ERR | MAIN,"Rds_Tx_Enable failed!\n");
        goto out;
    }   
    fm->rdstx_on = fm_true;
    ret = fm_low_ops.ri.rds_tx(parm->pi, parm->ps, parm->other_rds, parm->other_rds_cnt);
    if(ret){
        WCN_DBG(FM_ERR | MAIN,"Rds_Tx failed!\n");
        goto out;
    }   
//    fm_cxt->txcxt.rdsTxOn = true;
//    fm_cxt->txcxt.pi = parm->pi;
//    memcpy(fm_cxt->txcxt.ps, parm->ps,sizeof(parm->ps));
//    memcpy(fm_cxt->txcxt.other_rds, parm->other_rds,sizeof(parm->other_rds));
//    fm_cxt->txcxt.other_rds_cnt = parm->other_rds_cnt; 
out:
    return ret;
}

fm_s32 fm_rds_onoff(struct fm *fm, fm_u16 rdson_off)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.ri.rds_onoff);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    if (rdson_off) {
        fm->rds_on = fm_true;
        if (fm_low_ops.ri.rds_onoff(fm->pstRDSData, fm_true) == fm_false) {
            WCN_DBG(FM_ALT | MAIN, "FM_IOCTL_RDS_ONOFF faield\n");
            ret = -EPERM;
            goto out;
        }

        fm_enable_rds_BlerCheck(fm);
    } else {
        fm->rds_on = fm_false;
        fm_disable_rds_BlerCheck();
        fm_low_ops.ri.rds_onoff(fm->pstRDSData, fm_false);
    }

out:
    FM_UNLOCK(fm_ops_lock);
    return ret;
}

fm_s32 fm_rds_good_bc_get(struct fm *fm, fm_u16 *gbc)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.ri.rds_gbc_get);
    FMR_ASSERT(gbc);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    *gbc = fm_low_ops.ri.rds_gbc_get();

    FM_UNLOCK(fm_ops_lock);
    return ret;
}

fm_s32 fm_rds_bad_bc_get(struct fm *fm, fm_u16 *bbc)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.ri.rds_bbc_get);
    FMR_ASSERT(bbc);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    *bbc = fm_low_ops.ri.rds_bbc_get();

    FM_UNLOCK(fm_ops_lock);
    return ret;
}

fm_s32 fm_rds_bler_ratio_get(struct fm *fm, fm_u16 *bbr)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.ri.rds_bbr_get);
    FMR_ASSERT(bbr);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    *bbr = (fm_u16)fm_low_ops.ri.rds_bbr_get();

    FM_UNLOCK(fm_ops_lock);
    return ret;
}

fm_s32 fm_rds_group_cnt_get(struct fm *fm, struct rds_group_cnt_t *dst)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.ri.rds_gc_get);
    FMR_ASSERT(dst);
    if (FM_LOCK(fm_rds_cnt)) return (-FM_ELOCK);

    ret = fm_low_ops.ri.rds_gc_get(dst, fm->pstRDSData);

    FM_UNLOCK(fm_rds_cnt);
    return ret;
}

fm_s32 fm_rds_group_cnt_reset(struct fm *fm)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.ri.rds_gc_reset);
    if (FM_LOCK(fm_rds_cnt)) return (-FM_ELOCK);

    ret = fm_low_ops.ri.rds_gc_reset(fm->pstRDSData);

    FM_UNLOCK(fm_rds_cnt);
    return ret;
}

fm_s32 fm_rds_log_get(struct fm *fm, struct rds_rx_t *dst, fm_s32 *dst_len)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.ri.rds_log_get);
    FMR_ASSERT(dst);
    FMR_ASSERT(dst_len);
    if (FM_LOCK(fm_read_lock)) return (-FM_ELOCK);

    ret = fm_low_ops.ri.rds_log_get(dst, dst_len);

    FM_UNLOCK(fm_read_lock);
    return ret;
}

fm_s32 fm_rds_block_cnt_reset(struct fm *fm)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.ri.rds_bc_reset);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    ret = fm_low_ops.ri.rds_bc_reset();

    FM_UNLOCK(fm_ops_lock);
    return ret;
}

fm_s32 fm_i2s_set(struct fm *fm, fm_s32 onoff, fm_s32 mode, fm_s32 sample)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.bi.i2s_set);
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    ret = fm_low_ops.bi.i2s_set(onoff, mode, sample);

    FM_UNLOCK(fm_ops_lock);
    return ret;
}

/*
 *  fm_tune_tx
 */
fm_s32 fm_tune_tx(struct fm *fm, struct fm_tune_parm *parm)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.bi.tune_tx);
    
    if (fm_pwr_state_get(fm) != FM_PWR_TX_ON) {
        parm->err = FM_BADSTATUS;
        return -EPERM;
    }
    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);
    
    WCN_DBG(FM_DBG | MAIN, "%s\n", __func__);

	fm_op_state_set(fm, FM_STA_TUNE);
	WCN_DBG(FM_NTC | MAIN, "Tx tune to %d\n", parm->freq);
#if 0//ramp down tx will do in tx tune  flow
    while(0){
        if((ret = MT6620_RampDown_Tx()))
            return ret;
    }
#endif
    //tune to desired channel
    if (fm_true != fm_low_ops.bi.tune_tx(parm->freq)) 
    {
        parm->err = FM_TUNE_FAILED;
        WCN_DBG(FM_ALT | MAIN, "Tx tune failed\n");
        ret = -EPERM;
    }
	fm_op_state_set(fm, FM_STA_PLAY);
	FM_UNLOCK(fm_ops_lock);
    
    return ret;
}

/*
 *  fm_tune
 */
fm_s32 fm_tune(struct fm *fm, struct fm_tune_parm *parm)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm_low_ops.bi.mute);
    FMR_ASSERT(fm_low_ops.bi.rampdown);
    FMR_ASSERT(fm_low_ops.bi.setfreq);

    if (FM_LOCK(fm_ops_lock)) return (-FM_ELOCK);

    WCN_DBG(FM_DBG | MAIN, "%s\n", __func__);

    if (fm_pwr_state_get(fm) != FM_PWR_RX_ON) {
        parm->err = FM_BADSTATUS;
        ret = -EPERM;
        goto out;
    }

//    fm_low_ops.bi.mute(fm_true);
    fm_low_ops.bi.rampdown();

    if (fm_cur_freq_get() != parm->freq) {
        fm_memset(fm->pstRDSData, 0, sizeof(rds_t));
    }

#if 0//(!defined(MT6620_FM)&&!defined(MT6628_FM))
    //HILO side adjust if need
    if (priv_adv.priv_tbl.hl_dese) {
        if ((ret = priv_adv.priv_tbl.hl_dese(parm->freq, NULL)) < 0) {
            goto out;
        }

        WCN_DBG(FM_INF | MAIN, "HILO side %d\n", ret);
    }

    //Frequency avoid adjust if need
    if (priv_adv.priv_tbl.fa_dese) {
        if ((ret = priv_adv.priv_tbl.fa_dese(parm->freq, NULL)) < 0) {
            goto out;
        }

        WCN_DBG(FM_INF | MAIN, "FA %d\n", ret);
    }

    //MCU clock adjust if need
    if (priv_adv.priv_tbl.mcu_dese) {
        if ((ret = priv_adv.priv_tbl.mcu_dese(parm->freq, NULL)) < 0) {
            goto out;
        }

        WCN_DBG(FM_INF | MAIN, "MCU %d\n", ret);
    }

    //GPS clock adjust if need
    if (priv_adv.priv_tbl.gps_dese) {
        if ((ret = priv_adv.priv_tbl.gps_dese(parm->freq, NULL)) < 0) {
            goto out;
        }

        WCN_DBG(FM_INF | MAIN, "GPS %d\n", ret);
    }
#endif
    fm_op_state_set(fm, FM_STA_TUNE);
    WCN_DBG(FM_ALT | MAIN, "tuning to %d\n", parm->freq);

    if (fm_false == fm_low_ops.bi.setfreq(parm->freq)) {
        parm->err = FM_TUNE_FAILED;
        WCN_DBG(FM_ALT | MAIN, "FM tune failed\n");
        ret = -EPERM;
    }

//    fm_low_ops.bi.mute(fm_false);
    fm_op_state_set(fm, FM_STA_PLAY);
out:
    FM_UNLOCK(fm_ops_lock);
    return ret;
}
//cqi log tool entry
fm_s32 fm_cqi_log(void)
{
    fm_s32 ret = 0;
    fm_u16 freq;
	FMR_ASSERT(fm_low_ops.bi.cqi_log);
    freq = fm_cur_freq_get();
    if (0 == fm_get_channel_space(freq)) {
        freq *= 10;
    }
    if((freq != 10000) && (0xffffffff != g_dbg_level))
    {
    	return -FM_EPARA;
    }
	if (FM_LOCK(fm_ops_lock)) 
		return (-FM_ELOCK);
	ret = fm_low_ops.bi.cqi_log(8750,10800,2,5);
    FM_UNLOCK(fm_ops_lock);
	return ret;
}
/*fm soft mute tune function*/
fm_s32 fm_soft_mute_tune(struct fm *fm, struct fm_softmute_tune_t *parm)
{
    fm_s32 ret = 0;

	FMR_ASSERT(fm_low_ops.bi.softmute_tune);
	
	if (FM_LOCK(fm_ops_lock)) 
		return (-FM_ELOCK);
	
	
	if (fm_pwr_state_get(fm) != FM_PWR_RX_ON) 
	{
		parm->valid = fm_false;
		ret = -EPERM;
		goto out;
	}
	
	//	  fm_low_ops.bi.mute(fm_true);
	WCN_DBG(FM_NTC | MAIN, "+%s():[freq=%d]\n", __func__, parm->freq);
	//fm_op_state_set(fm, FM_STA_TUNE);
	
	if (fm_false == fm_low_ops.bi.softmute_tune(parm->freq,&parm->rssi,&parm->valid)) 
	{
		parm->valid = fm_false;
		WCN_DBG(FM_ALT | MAIN, "sm tune failed\n");
		ret = -EPERM;
	}
	
//	  fm_low_ops.bi.mute(fm_false);
out:
	WCN_DBG(FM_NTC | MAIN, "-%s()\n", __func__);
	FM_UNLOCK(fm_ops_lock);
		
	return ret;
}
fm_s32 fm_over_bt(struct fm *fm, fm_s32 flag)
{
    fm_s32 ret = 0;
	FMR_ASSERT(fm_low_ops.bi.fm_via_bt);
	
    if (fm_pwr_state_get(fm) != FM_PWR_RX_ON) 
    {
        return -EPERM;
    }
	if (FM_LOCK(fm_ops_lock)) 
		return (-FM_ELOCK);
   
    ret = fm_low_ops.bi.fm_via_bt(flag);
    if(ret)
    {
        WCN_DBG(FM_ALT | MAIN,"%s(),failed!\n", __func__);
    }
    else
    {
    	fm->via_bt = flag;
    }
    WCN_DBG(FM_NTC | MAIN,"%s(),[ret=%d]!\n", __func__, ret);
	FM_UNLOCK(fm_ops_lock);
    return ret;
}
fm_s32 fm_tx_support(struct fm *fm, fm_s32 *support)
{
	if (FM_LOCK(fm_ops_lock)) 
		return (-FM_ELOCK);
	if(fm_low_ops.bi.tx_support)
	{
		fm_low_ops.bi.tx_support(support);
	}
	else
	{
		*support=0;
	}
    WCN_DBG(FM_NTC | MAIN,"%s(),[%d]!\n", __func__, *support);
	FM_UNLOCK(fm_ops_lock);
	return 0;
}
fm_s32 fm_rdstx_support(struct fm *fm, fm_s32 *support)
{
	if (FM_LOCK(fm_ops_lock)) 
		return (-FM_ELOCK);
	if(fm_low_ops.ri.rdstx_support)
	{
		fm_low_ops.ri.rdstx_support(support);
	}
	else
	{
		*support=0;
	}
    WCN_DBG(FM_NTC | MAIN,"support=[%d]!\n", *support);
	FM_UNLOCK(fm_ops_lock);
	return 0;
}
fm_s32 fm_rdstx_enable(struct fm *fm, fm_s32 *support)
{
	if (FM_LOCK(fm_ops_lock)) 
		return (-FM_ELOCK);
	if(fm->rdstx_on)
	{
		*support=1;
	}
	else
	{
		*support=0;
	}
    WCN_DBG(FM_NTC | MAIN,"rds tx enable=[%d]!\n", *support);
	FM_UNLOCK(fm_ops_lock);
	return 0;
}

static void fm_timer_func(unsigned long data)
{
    struct fm *fm = g_fm_struct;

    if (FM_LOCK(fm_timer_lock)) return;

    if (fm_timer_sys->update(fm_timer_sys)) {
		WCN_DBG(FM_NTC | MAIN, "timer skip\n");
        goto out; //fm timer is stoped before timeout
    }

    if (fm != NULL) {
		WCN_DBG(FM_NTC | MAIN, "timer:rds_wk\n");
        fm->timer_wkthd->add_work(fm->timer_wkthd, fm->rds_wk);
    }

out:
    FM_UNLOCK(fm_timer_lock);
}


static void fmtx_timer_func(unsigned long data)
{
    struct fm *fm = g_fm_struct;
    fm_s32 vco_cycle = 1;

    if (FM_LOCK(fm_timer_lock)) return;

    fm_timer_sys->count++; 
    if (fm != NULL) 
    {
		//schedule tx pwr ctrl work if need
		if(fm->txpwrctl < 1)
		{
			WCN_DBG(FM_WAR | MAIN,"tx power ctl time err\n");
			fm->txpwrctl = FM_TX_PWR_CTRL_INVAL_MIN;
		}
		if((fm_timer_sys->tx_pwr_ctrl_en == FM_TX_PWR_CTRL_ENABLE) && (fm_timer_sys->count%fm->txpwrctl == 0))
		{
			WCN_DBG(FM_NTC | MAIN, "Tx timer:fm_tx_power_ctrl_work\n");
			fm->timer_wkthd->add_work(fm->timer_wkthd, fm->fm_tx_power_ctrl_work); 
		}
		/*
		//schedule tx RTC ctrl work if need
		if((timer->tx_rtc_ctrl_en == FM_TX_RTC_CTRL_ENABLE)&& (timer->count%FM_TX_RTC_CTRL_INTERVAL == 0)){
			FM_LOG_DBG(D_TIMER,"fm_tx_rtc_ctrl_work, ticks:%d\n", jiffies_to_msecs(jiffies));
			queue_work(fm->fm_timer_workqueue, &fm->fm_tx_rtc_ctrl_work); 
		}*/
		//schedule tx desense with wifi/bt work if need
		if(fm->vcooff < 1)
		{
			WCN_DBG(FM_WAR | MAIN,"tx vco tracking time err\n");
			fm->vcooff = FM_TX_VCO_OFF_MIN;
		}
		vco_cycle = fm->vcooff + fm->vcoon/1000;
		if((fm_timer_sys->tx_desense_en == FM_TX_DESENSE_ENABLE) && (fm_timer_sys->count%vco_cycle == 0))
		{
			WCN_DBG(FM_NTC | MAIN, "Tx timer:fm_tx_desense_wifi_work\n");
			fm->timer_wkthd->add_work(fm->timer_wkthd, fm->fm_tx_desense_wifi_work); 
		}
    }
    if (fm_timer_sys->update(fm_timer_sys)) {
		WCN_DBG(FM_NTC | MAIN, "timer skip\n");
        goto out; //fm timer is stoped before timeout
    }

out:
    FM_UNLOCK(fm_timer_lock);
}

static void fm_tx_power_ctrl_worker_func(unsigned long data)
{
    fm_s32 ctrl = 0,ret=0;
    struct fm *fm = g_fm_struct;

    WCN_DBG(FM_NTC | MAIN,"+%s():\n", __func__);
    
	if(fm_low_ops.bi.tx_pwr_ctrl == NULL)
		return;
    if (FM_LOCK(fm_rxtx_lock)) return;
    
    if(fm_pwr_state_get(fm) != FM_PWR_TX_ON){
        WCN_DBG(FM_ERR | MAIN,"FM is not on TX mode\n");
        goto out;
    }
    
    ctrl = fm->tx_pwr;
    WCN_DBG(FM_NTC | MAIN,"tx pwr %ddb\n", ctrl);
    ret = fm_low_ops.bi.tx_pwr_ctrl(fm_cur_freq_get(), &ctrl);
    if(ret)
    {
        WCN_DBG(FM_ERR | MAIN,"tx_pwr_ctrl fail\n");
    }
    
out:
    FM_UNLOCK(fm_rxtx_lock);
    WCN_DBG(FM_NTC | MAIN,"-%s()\n", __func__);
    return;
}
static void fm_tx_rtc_ctrl_worker_func(unsigned long data)
{
    fm_s32 ret = 0;
    fm_s32 ctrl = 0;
    struct fm_gps_rtc_info rtcInfo;
    //struct timeval curTime;
    //struct fm *fm = (struct fm*)fm_cb;
    unsigned long curTime = 0;

    WCN_DBG(FM_NTC | MAIN,"+%s():\n", __func__);

    if(FM_LOCK(fm_rtc_mutex)) return;
    
    if(gps_rtc_info.flag == FM_GPS_RTC_INFO_NEW){
        memcpy(&rtcInfo, &gps_rtc_info, sizeof(struct fm_gps_rtc_info));
        gps_rtc_info.flag = FM_GPS_RTC_INFO_OLD;
        FM_UNLOCK(fm_rtc_mutex);
    }else{
        WCN_DBG(FM_NTC | MAIN,"there's no new rtc drift info\n");
        FM_UNLOCK(fm_rtc_mutex);
        goto out;
    }

    if(rtcInfo.age > rtcInfo.ageThd){
       WCN_DBG(FM_WAR | MAIN,"age over it's threshlod\n");
       goto out;
    }
    if((rtcInfo.drift <= rtcInfo.driftThd) && (rtcInfo.drift >= -rtcInfo.driftThd)){
       WCN_DBG(FM_WAR | MAIN,"drift over it's MIN threshlod\n");
       goto out;
    }

    if(rtcInfo.drift > FM_GPS_RTC_DRIFT_MAX){
        WCN_DBG(FM_WAR | MAIN,"drift over it's +MAX threshlod\n");
        rtcInfo.drift = FM_GPS_RTC_DRIFT_MAX;
        goto out;
    }else if(rtcInfo.drift < -FM_GPS_RTC_DRIFT_MAX){
        WCN_DBG(FM_WAR | MAIN,"drift over it's -MAX threshlod\n");
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
        WCN_DBG(FM_WAR | MAIN,"time diff over it's threshlod\n");
        goto out;
    }
    if(fm_low_ops.bi.rtc_drift_ctrl != NULL)
    {
        ctrl = rtcInfo.drift;
		WCN_DBG(FM_NTC | MAIN,"RTC_drift_ctrl[0x%08x]\n", ctrl);
        if((ret = fm_low_ops.bi.rtc_drift_ctrl(fm_cur_freq_get(), &ctrl)))
            goto out;
    }
out:
    WCN_DBG(FM_NTC | MAIN,"-%s()\n", __func__);
    return;
} 

static void fm_tx_desense_wifi_worker_func(unsigned long data)
{
    fm_s32 ret = 0;
    fm_s32 ctrl = 0;
    struct fm *fm = g_fm_struct;

    WCN_DBG(FM_NTC | MAIN,"+%s():\n", __func__);

    if (FM_LOCK(fm_rxtx_lock)) return;
    
    if(fm_pwr_state_get(fm) != FM_PWR_TX_ON){
        WCN_DBG(FM_ERR | MAIN,"FM is not on TX mode\n");
        goto out;
    }
    
    fm_tx_rtc_ctrl_worker_func(0);

    ctrl = fm->vcoon;
    if(fm_low_ops.bi.tx_desense_wifi)
    {
	    WCN_DBG(FM_NTC | MAIN,"tx_desense_wifi[%d]\n", ctrl);
		ret = fm_low_ops.bi.tx_desense_wifi(fm_cur_freq_get(), &ctrl);
	    if(ret)
	    {
	        WCN_DBG(FM_ERR | MAIN,"tx_desense_wifi fail\n");
	    }
	}
out:
    FM_UNLOCK(fm_rxtx_lock);
    WCN_DBG(FM_NTC | MAIN,"-%s()\n", __func__);
    return;
} 

/*
************************************************************************************
Function:         fm_get_gps_rtc_info()

Description:     get GPS RTC drift info, and this function should not block

Date:              2011/04/10

Return Value:   success:0, failed: error coe
************************************************************************************
*/
fm_s32 fm_get_gps_rtc_info(struct fm_gps_rtc_info *src)
{
    fm_s32 ret = 0;
//    fm_s32 retry_cnt = 0;
	struct fm_gps_rtc_info *dst=&gps_rtc_info; 
    
    FMR_ASSERT(src);
    FMR_ASSERT(dst);

    if(src->retryCnt > 0){
        dst->retryCnt = src->retryCnt;
        WCN_DBG(FM_NTC | MAIN,"%s, new [retryCnt=%d]\n", __func__, dst->retryCnt);
    }
    if(src->ageThd > 0){
        dst->ageThd = src->ageThd;
        WCN_DBG(FM_NTC | MAIN,"%s, new [ageThd=%d]\n", __func__, dst->ageThd);
    }
    if(src->driftThd > 0){
        dst->driftThd = src->driftThd;
        WCN_DBG(FM_NTC | MAIN,"%s, new [driftThd=%d]\n", __func__, dst->driftThd);
    }
    if(src->tvThd.tv_sec > 0){
        dst->tvThd.tv_sec = src->tvThd.tv_sec;
        WCN_DBG(FM_NTC | MAIN,"%s, new [tvThd=%d]\n", __func__, (fm_s32)dst->tvThd.tv_sec);
    }
    ret = fm_rtc_mutex->trylock(fm_rtc_mutex,dst->retryCnt);
    if(ret)
    {
		goto out;
    }
    dst->age = src->age;
    dst->drift = src->drift;
    dst->stamp = jiffies; //get curren time stamp
    dst->flag = FM_GPS_RTC_INFO_NEW;
    
    FM_UNLOCK(fm_rtc_mutex);

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

static void fm_enable_rds_BlerCheck(struct fm *fm)
{
    if (FM_LOCK(fm_timer_lock)) return;
    fm_timer_sys->start(fm_timer_sys);
    FM_UNLOCK(fm_timer_lock);
}

static void fm_disable_rds_BlerCheck(void)
{
    if (FM_LOCK(fm_timer_lock)) return;
    fm_timer_sys->stop(fm_timer_sys);
    FM_UNLOCK(fm_timer_lock);
}

void fm_rds_reset_work_func(unsigned long data)
{
    fm_s32 ret = 0;

    if (!fm_low_ops.ri.rds_blercheck) {
        return;
    }
    if (FM_LOCK(fm_rxtx_lock)) return;

    if (FM_LOCK(fm_rds_cnt)) return;
    ret = fm_low_ops.ri.rds_blercheck(g_fm_struct->pstRDSData);

	WCN_DBG(FM_NTC | MAIN, "Addr_Cnt=%x\n",g_fm_struct->pstRDSData->AF_Data.Addr_Cnt);
	if(g_fm_struct->pstRDSData->AF_Data.Addr_Cnt == 0xFF)//check af list get,can't use event==af_list because event will clear after read rds every time
	{
		g_fm_struct->pstRDSData->event_status |= RDS_EVENT_AF;
	}
    if (!ret && g_fm_struct->pstRDSData->event_status) 
    {
        FM_EVENT_SEND(g_fm_struct->rds_event, FM_RDS_DATA_READY);
    }
	WCN_DBG(FM_NTC | MAIN, "rds event check=%x\n",g_fm_struct->pstRDSData->event_status);
    FM_UNLOCK(fm_rds_cnt);
    FM_UNLOCK(fm_rxtx_lock);
}


void fm_subsys_reset_work_func(unsigned long data)
{
    g_dbg_level = 0xffffffff;
    if (FM_LOCK(fm_ops_lock)) return;

    fm_sys_state_set(g_fm_struct, FM_SUBSYS_RST_START);
    
    if (g_fm_struct->chipon == fm_false)
	{
        WCN_DBG(FM_ALT | MAIN, "no need do recover\n");
        goto out;
    }
    // subsystem power off
    fm_low_ops.bi.pwroff(0);
    
    // prepare to reset
    
    // wait 3s
    fm_low_ops.bi.msdelay(2000);
    
    // subsystem power on
    fm_low_ops.bi.pwron(0);
    
    // recover context
    if (g_fm_struct->chipon == fm_false) {
        fm_low_ops.bi.pwroff(0);
        WCN_DBG(FM_ALT | MAIN, "no need do recover\n");
        goto out;
    }

    if (FM_PWR_RX_ON == fm_pwr_state_get(g_fm_struct)) {
        fm_low_ops.bi.pwrupseq(&g_fm_struct->chip_id, &g_fm_struct->device_id);
    } else {
        WCN_DBG(FM_ALT | MAIN, "no need do re-powerup\n");
        goto out;
    }

    fm_low_ops.bi.anaswitch(g_fm_struct->ana_type);
    
    fm_low_ops.bi.setfreq(fm_cur_freq_get()); 

    fm_low_ops.bi.volset((fm_u8)g_fm_struct->vol);

    fm_low_ops.bi.mute(g_fm_struct->mute);

    fm_low_ops.ri.rds_onoff(g_fm_struct->pstRDSData, g_fm_struct->rds_on);

    WCN_DBG(FM_ALT | MAIN, "recover done\n");

out:
    fm_sys_state_set(g_fm_struct, FM_SUBSYS_RST_END);
    fm_sys_state_set(g_fm_struct, FM_SUBSYS_RST_OFF);

    FM_UNLOCK(fm_ops_lock);
    g_dbg_level = 0xfffffff5;
}

static void fm_eint_handler(void)
{
    struct fm *fm = g_fm_struct;
    WCN_DBG(FM_DBG | MAIN, "intr occur, ticks:%d\n", jiffies_to_msecs(jiffies));

    if (fm != NULL) {
        fm->eint_wkthd->add_work(fm->eint_wkthd, fm->eint_wk);
    }
}

static fm_s32 fm_rds_parser(struct rds_rx_t *rds_raw, fm_s32 rds_size)
{
    struct fm *fm = g_fm_struct;//(struct fm *)work->data;
    rds_t *pstRDSData = fm->pstRDSData;

    if (FM_LOCK(fm_read_lock)) return (-FM_ELOCK);
    //parsing RDS data
    fm_low_ops.ri.rds_parser(pstRDSData, rds_raw, rds_size, fm_cur_freq_get);
    FM_UNLOCK(fm_read_lock);

    if ((pstRDSData->event_status != 0x0000) && (pstRDSData->event_status != RDS_EVENT_AF_LIST)) {
        WCN_DBG(FM_NTC | MAIN, "Notify user to read, [event:%04x]\n", pstRDSData->event_status);
        FM_EVENT_SEND(fm->rds_event, FM_RDS_DATA_READY);
    }

    return 0;
}

static void fm_eint_work_func(unsigned long data)
{
    fm_event_parser(fm_rds_parser);
    //re-enable eint if need
    fm_enable_eint();
}

static fm_s32 fm_callback_register(struct fm_lowlevel_ops *ops)
{
    FMR_ASSERT(ops);

    ops->cb.cur_freq_get = fm_cur_freq_get;
    ops->cb.cur_freq_set = fm_cur_freq_set;
    return 0;
}

static fm_s32 fm_callback_unregister(struct fm_lowlevel_ops *ops)
{
    FMR_ASSERT(ops);

    fm_memset(&ops->cb, 0, sizeof(struct fm_callback));
    return 0;
}


static fm_s32 fm_para_init(struct fm *fmp)
{
    FMR_ASSERT(fmp);

    fmp->band = FM_BAND_SPECIAL;
    fmp->min_freq = FM_RX_BAND_FREQ_L;
    fmp->max_freq = FM_RX_BAND_FREQ_H;
    fmp->cur_freq = 0;
    
    return 0;
}

fm_s32 fm_cust_config_setup(fm_s8 * filename)
{
	fm_s32 ret;
#if (defined(MT6620_FM)||defined(MT6628_FM))
#ifdef MT6628_FM	
	ret = MT6628fm_cust_config_setup(filename);
	if(ret < 0)
	{
        WCN_DBG(FM_ERR | MAIN, "MT6628fm_cust_config_setup failed\n");
	}
#endif
#ifdef MT6620_FM
	ret = MT6620fm_cust_config_setup(filename);
	if(ret < 0)
	{
        WCN_DBG(FM_ERR | MAIN, "MT6620fm_cust_config_setup failed\n");
	}
#endif
#else
	fm_cust_config(filename);
	if(ret < 0)
	{
        WCN_DBG(FM_ERR | MAIN, "fm_cust_config failed\n");
	}
#endif
	return ret;
}

struct fm* fm_dev_init(fm_u32 arg) 
{
    fm_s32 ret = 0;
    struct fm *fm = NULL;

//    if (!fm_low_ops.ri.rds_bci_get)
//        return NULL;

//    if (!fm_low_ops.bi.chipid_get)
//        return NULL;

    //alloc fm main data structure
    if (!(fm = fm_zalloc(sizeof(struct fm)))) {
        WCN_DBG(FM_ALT | MAIN, "-ENOMEM\n");
        ret = -ENOMEM;
        goto ERR_EXIT;
    }

    fm->ref = 0;
    fm->chipon = fm_false;
    fm_pwr_state_set(fm, FM_PWR_OFF);
//    fm->chip_id = fm_low_ops.bi.chipid_get();
	//FM Tx
	fm->vcoon = FM_TX_VCO_ON_DEFAULT;
	fm->vcooff = FM_TX_VCO_OFF_DEFAULT;
	fm->txpwrctl = FM_TX_PWR_CTRL_INVAL_DEFAULT;
    fm->tx_pwr = FM_TX_PWR_LEVEL_MAX;
    gps_rtc_info.err = 0;
    gps_rtc_info.age = 0;
    gps_rtc_info.drift = 0;
    gps_rtc_info.tv.tv_sec = 0;
    gps_rtc_info.tv.tv_usec = 0;
    gps_rtc_info.ageThd = FM_GPS_RTC_AGE_TH;
    gps_rtc_info.driftThd = FM_GPS_RTC_DRIFT_TH;
    gps_rtc_info.tvThd.tv_sec = FM_GPS_RTC_TIME_DIFF_TH;
    gps_rtc_info.retryCnt = FM_GPS_RTC_RETRY_CNT;
    gps_rtc_info.flag = FM_GPS_RTC_INFO_OLD;
	
    if (!(fm->rds_event = fm_flag_event_create("fm_rds_event"))) {
        WCN_DBG(FM_ALT | MAIN, "-ENOMEM for RDS event\n");
        ret = -ENOMEM;
        goto ERR_EXIT;
    }

    fm_flag_event_get(fm->rds_event);

    //alloc fm rds data structure
    if (!(fm->pstRDSData = fm_zalloc(sizeof(rds_t)))) {
        WCN_DBG(FM_ALT | MAIN, "-ENOMEM for RDS\n");
        ret = -ENOMEM;
        goto ERR_EXIT;
    }

    g_fm_struct = fm;

    fm->timer_wkthd = fm_workthread_create("fm_timer_wq");

    if (!fm->timer_wkthd) {
        WCN_DBG(FM_ALT | MAIN, "-ENOMEM for fm_timer_wq\n");
        ret = -ENOMEM;
        goto ERR_EXIT;
    }

    fm_workthread_get(fm->timer_wkthd);

    fm->eint_wkthd = fm_workthread_create("fm_eint_wq");

    if (!fm->eint_wkthd) {
        WCN_DBG(FM_ALT | MAIN, "-ENOMEM for fm_eint_wq\n");
        ret = -ENOMEM;
        goto ERR_EXIT;
    }

    fm_workthread_get(fm->eint_wkthd);

    fm->eint_wk = fm_work_create("fm_eint_work");

    if (!fm->eint_wk) {
        WCN_DBG(FM_ALT | MAIN, "-ENOMEM for eint_wk\n");
        ret = -ENOMEM;
        goto ERR_EXIT;
    } else {
        fm_work_get(fm->eint_wk);
        fm->eint_wk->init(fm->eint_wk, fm_eint_work_func, (unsigned long)fm);
    }

    // create reset work
    fm->rst_wk = fm_work_create("fm_rst_work");

    if (!fm->rst_wk) {
        WCN_DBG(FM_ALT | MAIN, "-ENOMEM for rst_wk\n");
        ret = -ENOMEM;
        goto ERR_EXIT;
    } else {
        fm_work_get(fm->rst_wk);
        fm->rst_wk->init(fm->rst_wk, fm_subsys_reset_work_func, (unsigned long)fm);
    }

    fm->rds_wk = fm_work_create("fm_rds_work");
    if (!fm->rds_wk) {
        WCN_DBG(FM_ALT | MAIN, "-ENOMEM for rds_wk\n");
        ret = -ENOMEM;
        goto ERR_EXIT;
    } else {
        fm_work_get(fm->rds_wk);
        fm->rds_wk->init(fm->rds_wk, fm_rds_reset_work_func, (unsigned long)fm);
    }
    
    fm->fm_tx_power_ctrl_work = fm_work_create("tx_pwr_ctl_work");
    if (!fm->fm_tx_power_ctrl_work) {
        WCN_DBG(FM_ALT | MAIN, "-ENOMEM for tx_pwr_ctl_work\n");
        ret = -ENOMEM;
        goto ERR_EXIT;
    } else {
        fm_work_get(fm->fm_tx_power_ctrl_work);
        fm->fm_tx_power_ctrl_work->init(fm->fm_tx_power_ctrl_work, fm_tx_power_ctrl_worker_func, (unsigned long)fm);
    }
    
    fm->fm_tx_desense_wifi_work = fm_work_create("tx_desen_wifi_work");
    if (!fm->fm_tx_desense_wifi_work) {
        WCN_DBG(FM_ALT | MAIN, "-ENOMEM for tx_desen_wifi_work\n");
        ret = -ENOMEM;
        goto ERR_EXIT;
    } else {
        fm_work_get(fm->fm_tx_desense_wifi_work);
        fm->fm_tx_desense_wifi_work->init(fm->fm_tx_desense_wifi_work, fm_tx_desense_wifi_worker_func, (unsigned long)fm);
    }

    //fm timer was created in fm_env_setp()
//    fm_timer_sys->init(fm_timer_sys, fm_timer_func, (unsigned long)g_fm_struct, fm_low_ops.ri.rds_bci_get(), 0);
//    fm_timer_sys->start(fm_timer_sys);

    //init customer config parameter
    fm_cust_config_setup(NULL);

    fm_para_init(fm);
    
    return g_fm_struct;

ERR_EXIT:

    if (fm->eint_wkthd) {
        ret = fm_workthread_put(fm->eint_wkthd);

        if (!ret)
            fm->eint_wkthd = NULL;
    }

    if (fm->timer_wkthd) {
        ret = fm_workthread_put(fm->timer_wkthd);

        if (!ret)
            fm->timer_wkthd = NULL;
    }

    if (fm->eint_wk) {
        ret = fm_work_put(fm->eint_wk);

        if (!ret)
            fm->eint_wk = NULL;
    }

    if (fm->rds_wk) {
        ret = fm_work_put(fm->rds_wk);

        if (!ret)
            fm->rds_wk = NULL;
    }

    if (fm->rst_wk) {
        ret = fm_work_put(fm->rst_wk);

        if (!ret)
            fm->rst_wk = NULL;
    }
    
    if (fm->fm_tx_desense_wifi_work) {
        ret = fm_work_put(fm->fm_tx_desense_wifi_work);

        if (!ret)
            fm->fm_tx_desense_wifi_work = NULL;
    }
    
    if (fm->fm_tx_power_ctrl_work) {
        ret = fm_work_put(fm->fm_tx_power_ctrl_work);

        if (!ret)
            fm->fm_tx_power_ctrl_work = NULL;
    }
    
    if (fm->pstRDSData) {
        fm_free(fm->pstRDSData);
        fm->pstRDSData = NULL;
    }

    fm_free(fm);
    g_fm_struct = NULL;
    return NULL;
}

fm_s32 fm_dev_destroy(struct fm *fm)
{
    fm_s32 ret = 0;

    WCN_DBG(FM_DBG | MAIN, "%s\n", __func__);

    fm_timer_sys->stop(fm_timer_sys);

    if (fm->eint_wkthd) {
        ret = fm_workthread_put(fm->eint_wkthd);

        if (!ret)
            fm->eint_wkthd = NULL;
    }

    if (fm->timer_wkthd) {
        ret = fm_workthread_put(fm->timer_wkthd);

        if (!ret)
            fm->timer_wkthd = NULL;
    }

    if (fm->eint_wk) {
        ret = fm_work_put(fm->eint_wk);

        if (!ret)
            fm->eint_wk = NULL;
    }

    if (fm->rds_wk) {
        ret = fm_work_put(fm->rds_wk);

        if (!ret)
            fm->rds_wk = NULL;
    }

    if (fm->rst_wk) {
        ret = fm_work_put(fm->rst_wk);

        if (!ret)
            fm->rst_wk = NULL;
    }
    
    if (fm->pstRDSData) {
        fm_free(fm->pstRDSData);
        fm->pstRDSData = NULL;
    }

    if (fm->pstRDSData) {
        fm_free(fm->pstRDSData);
        fm->pstRDSData = NULL;
    }

    fm_flag_event_put(fm->rds_event);

    // free all memory
    if (fm) {
        fm_free(fm);
        fm = NULL;
        g_fm_struct = NULL;
    }

    return ret;
}

fm_s32 fm_env_setup(void)
{
    fm_s32 ret = 0;

    WCN_DBG(FM_NTC | MAIN, "%s\n", __func__);
#if (defined(MT6620_FM)||defined(MT6628_FM))
#ifdef MT6620_FM	
    //register call back functions
    ret = fm_callback_register(&MT6620fm_low_ops);
    if (ret) {
        return ret;
    }
    WCN_DBG(FM_NTC | MAIN, "1. fm callback registered\n");
    //get low level functions
    ret = MT6620fm_low_ops_register(&MT6620fm_low_ops);
    if (ret) {
        return ret;
    }
    WCN_DBG(FM_NTC | MAIN, "2. fm low ops registered\n");
    //get rds level functions
    ret = MT6620fm_rds_ops_register(&MT6620fm_low_ops);
    if (ret) {
        return ret;
    }
    WCN_DBG(FM_NTC | MAIN, "3. fm rds ops registered\n");
#endif	
#ifdef MT6628_FM	
    //register call back functions
    ret = fm_callback_register(&MT6628fm_low_ops);
    if (ret) {
        return ret;
    }
    WCN_DBG(FM_NTC | MAIN, "1. fm callback registered\n");
    //get low level functions
    ret = MT6628fm_low_ops_register(&MT6628fm_low_ops);
    if (ret) {
        return ret;
    }
    WCN_DBG(FM_NTC | MAIN, "2. fm low ops registered\n");
    //get rds level functions
    ret = MT6628fm_rds_ops_register(&MT6628fm_low_ops);
    if (ret) {
        return ret;
    }
    WCN_DBG(FM_NTC | MAIN, "3. fm rds ops registered\n");
#endif	
#else 
    //register call back functions
    ret = fm_callback_register(&fm_low_ops);
    if (ret) {
        return ret;
    }
    WCN_DBG(FM_NTC | MAIN, "1. fm callback registered\n");
    //get low level functions
    ret = fm_low_ops_register(&fm_low_ops);

    if (ret) {
        return ret;
    }

    WCN_DBG(FM_NTC | MAIN, "2. fm low ops registered\n");
    //get rds level functions
    ret = fm_rds_ops_register(&fm_low_ops);

    if (ret) {
        return ret;
    }
    WCN_DBG(FM_NTC | MAIN, "3. fm rds ops registered\n");
#endif

    fm_ops_lock = fm_lock_create("ops_lock");

    if (!fm_ops_lock) {
        return -1;
    }

    fm_read_lock = fm_lock_create("rds_read");

    if (!fm_read_lock) {
        return -1;
    }
    
    fm_rds_cnt = fm_lock_create("rds_cnt");

    if (!fm_rds_cnt) {
        return -1;
    }

    fm_timer_lock = fm_spin_lock_create("timer_lock");

    if (!fm_timer_lock) {
        return -1;
    }
	fm_rxtx_lock = fm_lock_create("rxtx_lock");
    if (!fm_rxtx_lock) {
        return -1;
    }
	fm_rtc_mutex = fm_lock_create("rxtx_lock");
    if (!fm_rxtx_lock) {
        return -1;
    }

    fm_lock_get(fm_ops_lock);
    fm_lock_get(fm_read_lock);
    fm_lock_get(fm_rds_cnt);
    fm_spin_lock_get(fm_timer_lock);
    fm_lock_get(fm_rxtx_lock);
    WCN_DBG(FM_NTC | MAIN, "4. fm locks created\n");

    fm_timer_sys = fm_timer_create("fm_sys_timer");

    if (!fm_timer_sys) {
        return -1;
    }

    fm_timer_get(fm_timer_sys);
    WCN_DBG(FM_NTC | MAIN, "5. fm timer created\n");

    ret = fm_link_setup((void*)fm_wholechip_rst_cb);

    if (ret) {
        WCN_DBG(FM_ERR | MAIN, "fm link setup Failed\n");
        return -1;
    }

    return ret;
}

fm_s32 fm_env_destroy(void)
{
    fm_s32 ret = 0;

    WCN_DBG(FM_NTC | MAIN, "%s\n", __func__);

    fm_link_release();

#if (defined(MT6620_FM)||defined(MT6628_FM))
#if defined(MT6620_FM)
    //register call back functions
		ret = fm_callback_unregister(&MT6620fm_low_ops);

    if (ret) {
        return -1;
    }
		//put low level functions
		ret = MT6620fm_low_ops_unregister(&MT6620fm_low_ops);
	
		if (ret) {
			return -1;
		}
	
		//put rds func
		ret = MT6620fm_rds_ops_unregister(&MT6620fm_low_ops);
	
		if (ret) {
			return -1;
		}
#endif
#if defined(MT6628_FM)
		//register call back functions
		ret = fm_callback_unregister(&MT6628fm_low_ops);

		if (ret) {
			return -1;
		}
		//put low level functions
		ret = MT6628fm_low_ops_unregister(&MT6628fm_low_ops);

		if (ret) {
			return -1;
		}
	
		//put rds func
		ret = MT6628fm_rds_ops_unregister(&MT6628fm_low_ops);
	
		if (ret) {
			return -1;
		}
#endif
#else
    //register call back functions
    ret = fm_callback_unregister(&fm_low_ops);

    if (ret) {
        return -1;
    }
    //put low level functions
    ret = fm_low_ops_unregister(&fm_low_ops);

    if (ret) {
        return -1;
    }

    //put rds func
    ret = fm_rds_ops_unregister(&fm_low_ops);

    if (ret) {
        return -1;
    }
#endif
    ret = fm_lock_put(fm_ops_lock);

    if (!ret)
        fm_ops_lock = NULL;

    ret = fm_lock_put(fm_read_lock);

    if (!ret)
        fm_read_lock = NULL;

    ret = fm_lock_put(fm_rds_cnt);

    if (!ret)
        fm_rds_cnt = NULL;
    ret = fm_spin_lock_put(fm_timer_lock);

    if (!ret)
        fm_timer_lock = NULL;

    ret = fm_timer_put(fm_timer_sys);

    if (!ret)
        fm_timer_sys = NULL;

    return ret;
}

#if 0//(!defined(MT6620_FM)&&!defined(MT6628_FM))
fm_s32 fm_priv_register(struct fm_priv *pri, struct fm_pub *pub)
{
    fm_s32 ret = 0;
    //Basic functions.

    WCN_DBG(FM_NTC | MAIN, "%s\n", __func__);
    FMR_ASSERT(pri);
    FMR_ASSERT(pub);

    // functions provided by private module
    priv_adv.priv_tbl.hl_dese = pri->priv_tbl.hl_dese;
    priv_adv.priv_tbl.fa_dese = pri->priv_tbl.fa_dese;
    priv_adv.priv_tbl.mcu_dese = pri->priv_tbl.mcu_dese;
    priv_adv.priv_tbl.gps_dese = pri->priv_tbl.gps_dese;
    priv_adv.priv_tbl.chan_para_get = pri->priv_tbl.chan_para_get;
    priv_adv.priv_tbl.is_dese_chan = pri->priv_tbl.is_dese_chan;
    priv_adv.state = INITED;
    priv_adv.data = NULL;

    // for special chip(chip with DSP) use
    fm_low_ops.cb.chan_para_get = priv_adv.priv_tbl.chan_para_get;

    // private module will use these functions
    pub->pub_tbl.read = fm_low_ops.bi.read;
    pub->pub_tbl.write = fm_low_ops.bi.write;
    pub->pub_tbl.setbits = fm_low_ops.bi.setbits;
    pub->pub_tbl.rampdown = fm_low_ops.bi.rampdown;
    pub->pub_tbl.msdelay = fm_low_ops.bi.msdelay;
    pub->pub_tbl.usdelay = fm_low_ops.bi.usdelay;
    pub->pub_tbl.log = (fm_s32 (*)(const fm_s8 *arg1, ...))printk;
    pub->state = INITED;
    pub->data = NULL;

    return ret;
}

fm_s32 fm_priv_unregister(struct fm_priv *pri, struct fm_pub *pub)
{
    WCN_DBG(FM_NTC | MAIN, "%s\n", __func__);

    //FMR_ASSERT(pri);
    FMR_ASSERT(pub);

    fm_memset(&priv_adv, 0, sizeof(struct fm_priv));
    fm_low_ops.cb.chan_para_get = NULL;
    fm_memset(pub, 0, sizeof(struct fm_pub));

    return 0;
}
#endif

/*
 * GetChannelSpace - get the spcace of gived channel
 * @freq - value in 760~1080 or 7600~10800
 *
 * Return 0, if 760~1080; return 1, if 7600 ~ 10800, else err code < 0
 */
fm_s32 fm_get_channel_space(fm_s32 freq)
{
    if ((freq >= 760) && (freq <= 1080)) {
        return 0;
    } else if ((freq >= 7600) && (freq <= 10800)) {
        return 1;
    } else {
        return -1;
    }
}

