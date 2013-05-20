/* fm_module.c
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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <linux/sched.h>
#include <linux/delay.h> // udelay()

#include "fm_config.h"
#include "fm_main.h"
#include "fm_ioctl.h"

#define FM_PROC_FILE 		"fm"

fm_u32 g_dbg_level = 0xfffffff5;  // Debug level of FM

//fm main data structure
static struct fm *g_fm = NULL;
//proc file entry
static struct proc_dir_entry *g_fm_proc = NULL;

//char device interface
static fm_s32  fm_cdev_setup(struct fm *fm);
static fm_s32  fm_cdev_destroy(struct fm *fm);

static long  fm_ops_ioctl(struct file *filp, fm_u32 cmd, unsigned long arg);
static loff_t fm_ops_lseek(struct file *filp, loff_t off, fm_s32 whence);
static ssize_t fm_ops_read(struct file *filp, char *buf, size_t len, loff_t *off);
static fm_s32  fm_ops_open(struct inode *inode, struct file *filp);
static fm_s32  fm_ops_release(struct inode *inode, struct file *filp);
static fm_s32  fm_ops_flush(struct file *filp,fl_owner_t Id);
static struct file_operations fm_ops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = fm_ops_ioctl,
    .llseek = fm_ops_lseek,
    .read = fm_ops_read,
    .open = fm_ops_open,
    .release = fm_ops_release,
    .flush = fm_ops_flush,
};

static fm_s32 fm_proc_read(char *page, char **start, off_t off, fm_s32 count, fm_s32 *eof, void *data);
static fm_s32 fm_proc_write(struct file *file, const char *buffer, unsigned long count, void *data);


static struct fm_scan_t parm = {
    .sr_size = 0,
    .sr.ch_rssi_buf = NULL,
};

static long fm_ops_ioctl(struct file *filp, fm_u32 cmd, unsigned long arg)
{
    fm_s32 ret = 0;
    struct fm_platform *plat = container_of(filp->f_dentry->d_inode->i_cdev, struct fm_platform, cdev);
    struct fm *fm = container_of(plat, struct fm, platform);

    WCN_DBG(FM_NTC | MAIN, "%s---pid(%d)---cmd(0x%08x)---arg(0x%08x)\n", current->comm, current->pid, cmd, (fm_u32)arg);

    if (fm_sys_state_get(fm) != FM_SUBSYS_RST_OFF) {
        WCN_DBG(FM_ALT | MAIN, "FM subsys is resetting, retry later\n");
        ret = -FM_ESRST;
        return ret;
    }
    
    switch (cmd) {
    case FM_IOCTL_POWERUP: {
        struct fm_tune_parm parm;
        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_POWERUP:0\n");

        if (copy_from_user(&parm, (void*)arg, sizeof(struct fm_tune_parm))) {
            ret = -EFAULT;
            goto out;
        }

        ret = fm_powerup(fm, &parm);
        if (ret < 0) goto out;
        ret = fm_tune(fm, &parm);
        if (ret < 0) goto out;

        if (copy_to_user((void*)arg, &parm, sizeof(struct fm_tune_parm))) {
            ret = -EFAULT;
            goto out;
        }
		WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_POWERUP:1\n");

        break;
    }

    case FM_IOCTL_POWERDOWN: {
        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_POWERDOWN:0\n");
        ret = fm_powerdown(fm);
		WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_POWERDOWN:1\n");
        break;
    }

    case FM_IOCTL_TUNE: {
        struct fm_tune_parm parm;
        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_TUNE:0\n");

        if (copy_from_user(&parm, (void*)arg, sizeof(struct fm_tune_parm))) {
            ret = -EFAULT;
            goto out;
        }

        ret = fm_tune(fm, &parm);
        if (ret < 0) {
            goto out;
        }

        if (copy_to_user((void*)arg, &parm, sizeof(struct fm_tune_parm))) {
            ret = -EFAULT;
            goto out;
        }

		WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_TUNE:1\n");
        break;
    }

	case FM_IOCTL_SOFT_MUTE_TUNE:
	{
        struct fm_softmute_tune_t parm;
        fm_cqi_log();//cqi log tool
        WCN_DBG(FM_NTC | MAIN,"......FM_IOCTL_SOFT_MUTE_TUNE......\n");
        if(copy_from_user(&parm, (void*)arg, sizeof(struct fm_softmute_tune_t)))
        {
            ret = -EFAULT;
            goto out;
        }

        ret = fm_soft_mute_tune(fm, &parm);
        if (ret < 0) {
            goto out;
        }

        if(copy_to_user((void*)arg, &parm, sizeof(struct fm_softmute_tune_t)))
        {
            ret = -EFAULT;
            goto out;
        }
        break;
	}    
	case FM_IOCTL_SEEK: {
        struct fm_seek_parm parm;
        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_SEEK:0\n");

        if (copy_from_user(&parm, (void*)arg, sizeof(struct fm_seek_parm))) {
            ret = -EFAULT;
            goto out;
        }

        ret = fm_seek(fm, &parm);
        if (ret < 0) {
            goto out;
        }

        if (copy_to_user((void*)arg, &parm, sizeof(struct fm_seek_parm))) {
            ret = -EFAULT;
            goto out;
        }
		WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_SEEK:1\n");
        break;
    }

    case FM_IOCTL_SCAN: {
        struct fm_scan_parm parm;
        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_SCAN start\n");

        if (copy_from_user(&parm, (void*)arg, sizeof(struct fm_scan_parm))) {
            WCN_DBG(FM_ALT | MAIN, "copy_from_user failed\n");
            ret = -EFAULT;
            goto out;
        }

        ret = fm_scan(fm, &parm);
        if (ret < 0) goto out;

        if (copy_to_user((void*)arg, &parm, sizeof(struct fm_scan_parm))) {
            ret = -EFAULT;
            goto out;
        }

		WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_SCAN end\n");
        break;
    }

    case FM_IOCTL_TUNE_NEW: {
        struct fm_tune_t tune_tmp;
    
        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_TUNE_NEW\n");
    
        if (copy_from_user(&tune_tmp, (void*)arg, sizeof(struct fm_tune_t))) {
            WCN_DBG(FM_ERR | MAIN, "tune new copy_from_user error\n");
            ret = -EFAULT;
            goto out;
        }
    
        ret = fm_tune_new(fm, &tune_tmp);
        if (ret < 0) {
            goto out;
        }
            
        if (copy_to_user((void*)arg, &tune_tmp, sizeof(struct fm_tune_t))) {
            WCN_DBG(FM_ERR | MAIN, "tune new copy_to_user error\n");
            ret = -EFAULT;
            goto out;
        }
            
        break;        
    }

    case FM_IOCTL_SEEK_NEW: {
        struct fm_seek_t seek_tmp;

        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_SEEK_NEW\n");
        
        if (copy_from_user(&seek_tmp, (void*)arg, sizeof(struct fm_seek_t))) {
            WCN_DBG(FM_ERR | MAIN, "seek new copy_from_user error\n");
            ret = -EFAULT;
            goto out;
        }
        
        ret = fm_seek_new(fm, &seek_tmp);
        if (ret < 0) {
            goto out;
        }

        if (copy_to_user((void*)arg, &seek_tmp, sizeof(struct fm_seek_t))) {
            WCN_DBG(FM_ERR | MAIN, "seek new copy_to_user error\n");
            ret = -EFAULT;
            goto out;
        }
        
        break;
    }
    
    case FM_IOCTL_SCAN_NEW: {
        struct fm_scan_t tmp;
        
        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_SCAN_NEW\n");

        if (copy_from_user(&tmp, (void*)arg, sizeof(struct fm_scan_t))) {
            WCN_DBG(FM_ERR | MAIN, "copy_from_user error\n");
            ret =  -EFAULT;
            goto out;
        }

        switch (tmp.cmd) {
            case FM_SCAN_CMD_START: 
                if ((tmp.upper > 10800) || (tmp.lower < 7600) || (tmp.space < 5) || (tmp.space > 20)) {
                    WCN_DBG(FM_ERR | MAIN, "scan para error\n");
                    ret = -EFAULT;
                    goto out;
                }
                parm.cmd = tmp.cmd;
                parm.lower = tmp.lower;
                parm.upper = tmp.upper;
                parm.space = tmp.space;

                ret = fm_scan_new(fm, &parm);
                if (ret < 0) {
                    goto out;
                }
                break;
                
            case FM_SCAN_CMD_GET_CH_RSSI:
                if (parm.sr_size && parm.sr.ch_rssi_buf) {
                    if (copy_to_user(tmp.sr.ch_rssi_buf, parm.sr.ch_rssi_buf, parm.num * sizeof(struct fm_ch_rssi))) {
                        WCN_DBG(FM_ERR | MAIN, "scan copy_to_user err\n");
                        ret = -EFAULT;
                        goto out;
                    }
                }
                break;
                
            default:
                break;
        }

        tmp.num = parm.num;
        if (copy_to_user((void*)arg, &tmp, sizeof(struct fm_scan_t))) {
            WCN_DBG(FM_ERR | MAIN, "copy_to_user error\n");
            ret = -EFAULT;
            goto out;
        }
        break;
    }   
    
    case FM_IOCTL_CQI_GET: {
        struct fm_cqi_req cqi_req;
        fm_s8 *buf = NULL;
        fm_s32 tmp;

        WCN_DBG(FM_DBG | MAIN, "FM_IOCTL_CQI_GET\n");

        if (copy_from_user(&cqi_req, (void*)arg, sizeof(struct fm_cqi_req))) {
            WCN_DBG(FM_ALT | MAIN, "copy_from_user failed\n");
            ret = -EFAULT;
            goto out;
        }

        if ((cqi_req.ch_num*sizeof(struct fm_cqi) > cqi_req.buf_size) || !cqi_req.cqi_buf) {
            ret = -FM_EPARA;
            goto out;
        }

        tmp = cqi_req.ch_num / 16 + ((cqi_req.ch_num % 16) ? 1 : 0);
        tmp = tmp * 16 * sizeof(struct fm_cqi);
        buf = fm_zalloc(tmp);

        if (!buf) {
            ret = -FM_ENOMEM;
            goto out;
        }

        ret = fm_cqi_get(fm, cqi_req.ch_num, buf, tmp);

        if (ret) {
            fm_free(buf);
            WCN_DBG(FM_ALT | MAIN, "get cqi failed\n");
            goto out;
        }

        if (copy_to_user((void*)cqi_req.cqi_buf, buf, cqi_req.ch_num*sizeof(struct fm_cqi))) {
            fm_free(buf);
            ret = -EFAULT;
            goto out;
        }

        fm_free(buf);
        break;
    }

    case FM_IOCTL_GET_HW_INFO: {
        struct fm_hw_info info;

        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_GET_HW_INFO\n");

        ret = fm_get_hw_info(fm, &info);

        if (ret) {
            WCN_DBG(FM_ALT | MAIN, "get hw info failed\n");
            goto out;
        }

        if (copy_to_user((void*)arg, &info, sizeof(struct fm_hw_info))) {
            ret = -EFAULT;
            goto out;
        }

        break;
    }

    case FM_IOCTL_GET_I2S_INFO: {
        struct fm_i2s_info i2sinfo;

        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_GET_I2S_INFO\n");

        ret = fm_get_i2s_info(fm, &i2sinfo);

        if (ret) {
            WCN_DBG(FM_ALT | MAIN, "get i2s info failed\n");
            goto out;
        }

        if (copy_to_user((void*)arg, &i2sinfo, sizeof(struct fm_i2s_info))) {
            ret = -EFAULT;
            goto out;
        }

        break;
    }

    case FM_IOCTL_SETVOL: {
        fm_u32 vol;

        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_SETVOL start\n");
        if (copy_from_user(&vol, (void*)arg, sizeof(fm_u32))) {
            WCN_DBG(FM_ALT | MAIN, "copy_from_user failed\n");
            ret = -EFAULT;
            goto out;
        }

        ret = fm_setvol(fm, vol);
        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_SETVOL end:%d\n", vol);
        break;
    }
    case FM_IOCTL_GETVOL: {
        fm_u32 vol;
        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_GETVOL start\n");
        ret = fm_getvol(fm, &vol);
        if (ret < 0) goto out;

        if (copy_to_user((void*)arg, &vol, sizeof(fm_u32))) {
            ret = -EFAULT;
            goto out;
        }

        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_GETVOL end=%d\n",vol);
        break;
    }

    case FM_IOCTL_MUTE: {
        fm_u32 bmute;

        WCN_DBG(FM_NTC| MAIN, "FM_IOCTL_MUTE start\n");
        if (copy_from_user(&bmute, (void*)arg, sizeof(fm_u32))) {
            ret = -EFAULT;
            goto out;
        }

        ret = fm_mute(fm, bmute);
        WCN_DBG(FM_NTC| MAIN, "FM_IOCTL_MUTE end-%d\n", bmute);
        break;
    }

    case FM_IOCTL_GETRSSI: {
        fm_s32 rssi = 0;

        ret = fm_getrssi(fm, &rssi);
        if (ret < 0) goto out;

        if (copy_to_user((void*)arg, &rssi, sizeof(fm_s32))) {
            ret = -EFAULT;
            goto out;
        }

        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_GETRSSI:%d\n",rssi);
        break;
    }

    case FM_IOCTL_RW_REG: {
        struct fm_ctl_parm parm_ctl;
        WCN_DBG(FM_DBG | MAIN, "FM_IOCTL_RW_REG\n");

        if (copy_from_user(&parm_ctl, (void*)arg, sizeof(struct fm_ctl_parm))) {
            ret = -EFAULT;
            goto out;
        }

        if (parm_ctl.rw_flag == 0) {
            ret = fm_reg_write(fm, parm_ctl.addr, parm_ctl.val);
        } else {
            ret = fm_reg_read(fm, parm_ctl.addr, &parm_ctl.val);
        }
        if (ret < 0) goto out;

        if ((parm_ctl.rw_flag == 0x01) && (!ret)) {
            if (copy_to_user((void*)arg, &parm_ctl, sizeof(struct fm_ctl_parm))) {
                ret = -EFAULT;
                goto out;
            }
        }

        break;
    }

    case FM_IOCTL_GETCHIPID: {
        fm_u16 chipid;

        ret = fm_chipid_get(fm, &chipid);
        WCN_DBG(FM_DBG | MAIN, "FM_IOCTL_GETCHIPID:%04x\n", chipid);
        if (ret < 0) goto out;

        if (copy_to_user((void*)arg, &chipid, sizeof(fm_u16))) {
            ret = -EFAULT;
            goto out;
        }
        break;
    }

    case FM_IOCTL_GETMONOSTERO: {
        fm_u16 usStereoMono;

        ret = fm_monostereo_get(fm, &usStereoMono);
        WCN_DBG(FM_DBG | MAIN, "FM_IOCTL_GETMONOSTERO:%04x\n", usStereoMono);
        if (ret < 0) goto out;

        if (copy_to_user((void*)arg, &usStereoMono, sizeof(fm_u16))) {
            ret = -EFAULT;
            goto out;
        }
        break;
    }

    case FM_IOCTL_SETMONOSTERO: {
        WCN_DBG(FM_DBG | MAIN, "FM_IOCTL_SETMONOSTERO, %d\n", (fm_s32)arg);
        ret = fm_monostereo_set(fm, (fm_s32)arg);
        break;
    }

    case FM_IOCTL_GETCURPAMD: {
        fm_u16 PamdLevl;

        ret = fm_pamd_get(fm, &PamdLevl);
        WCN_DBG(FM_DBG | MAIN, "FM_IOCTL_GETCURPAMD:%d\n", PamdLevl);
        if (ret < 0) goto out;

        if (copy_to_user((void*)arg, &PamdLevl, sizeof(fm_u16)))
            ret = -EFAULT;
            goto out;

        break;
    }

    case FM_IOCTL_GETCAPARRAY: {
        fm_s32 ca;

        ret = fm_caparray_get(fm, &ca);
        WCN_DBG(FM_DBG | MAIN, "FM_IOCTL_GETCAPARRAY:%d\n", ca);
        if (ret < 0) goto out;

        if (copy_to_user((void*)arg, &ca, sizeof(fm_s32))) {
            ret = -EFAULT;
            goto out;
        }
        break;
    }

    case FM_IOCTL_EM_TEST: {
        struct fm_em_parm parm_em;

        WCN_DBG(FM_DBG | MAIN, "FM_IOCTL_EM_TEST\n");

        if (copy_from_user(&parm_em, (void*)arg, sizeof(struct fm_em_parm))) {
            ret = -EFAULT;
            goto out;
        }
        ret = fm_em_test(fm, parm_em.group_idx, parm_em.item_idx, parm_em.item_value);
        break;
    }

    case FM_IOCTL_RDS_SUPPORT: {
        fm_s32 support = FM_RDS_ENABLE;
        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_RDS_SUPPORT\n");

        if (copy_to_user((void*)arg, &support, sizeof(fm_s32))) {
            ret = -EFAULT;
            goto out;
        }
        break;
    }

    case FM_IOCTL_IS_FM_POWERED_UP: {
        fm_u32 powerup;
        WCN_DBG(FM_DBG | MAIN, "FM_IOCTL_IS_FM_POWERED_UP");

        if (fm->chipon && fm_pwr_state_get(fm)) {
            powerup = 1;
        } else {
            powerup = 0;
        }

        if (copy_to_user((void*)arg, &powerup, sizeof(fm_u32))) {
            ret = -EFAULT;
            goto out;
        }
        break;
    }

    case FM_IOCTL_RDS_ONOFF: {
        fm_u16 rdson_off = 0;
        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_RDS_ONOFF start\n");

        if (copy_from_user(&rdson_off, (void*)arg, sizeof(fm_u16))) {
            ret = -EFAULT;
            goto out;
        }
        ret = fm_rds_onoff(fm, rdson_off);
        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_RDS_ONOFF end:%d\n",rdson_off);
        break;
    }

    case FM_IOCTL_GETGOODBCNT: {
        fm_u16 uGBLCNT = 0;

        ret = fm_rds_good_bc_get(fm, &uGBLCNT);
        WCN_DBG(FM_DBG | MAIN, "FM_IOCTL_GETGOODBCNT:%d\n", uGBLCNT);
        if (ret < 0) goto out;

        if (copy_to_user((void*)arg, &uGBLCNT, sizeof(fm_u16))) {
            ret = -EFAULT;
            goto out;
        }
        break;
    }

    case FM_IOCTL_GETBADBNT: {
        fm_u16 uBadBLCNT = 0;

        ret = fm_rds_bad_bc_get(fm, &uBadBLCNT);
        WCN_DBG(FM_DBG | MAIN, "FM_IOCTL_GETBADBNT:%d\n", uBadBLCNT);
        if (ret < 0) goto out;

        if (copy_to_user((void*)arg, &uBadBLCNT, sizeof(fm_u16))) {
            ret = -EFAULT;
            goto out;
        }
        break;
    }

    case FM_IOCTL_GETBLERRATIO: {
        fm_u16 uBlerRatio = 0;

        ret = fm_rds_bler_ratio_get(fm, &uBlerRatio);
        WCN_DBG(FM_DBG | MAIN, "FM_IOCTL_GETBLERRATIO:%d\n", uBlerRatio);
        if (ret < 0) goto out;

        if (copy_to_user((void*)arg, &uBlerRatio, sizeof(fm_u16))) {
            ret = -EFAULT;
            goto out;
        }
        break;
    }

    case FM_IOCTL_ANA_SWITCH: {
        fm_s32 antenna = -1;
        WCN_DBG(FM_DBG | MAIN, "FM_IOCTL_ANA_SWITCH\n");

        if (copy_from_user(&antenna, (void*)arg, sizeof(fm_s32))) {
            WCN_DBG(FM_ALT | MAIN, "copy from user error\n");
            ret = -EFAULT;
            goto out;
        }

        ret = fm_ana_switch(fm, antenna);
        break;
    }

    case FM_IOCTL_RDS_GROUPCNT: {
        struct rds_group_cnt_req_t gc_req;
        WCN_DBG(FM_DBG | MAIN, "......FM_IOCTL_RDS_GROUPCNT......\n");

        if (copy_from_user(&gc_req, (void*)arg, sizeof(struct rds_group_cnt_req_t))) {
            WCN_DBG(FM_ALT | MAIN, "copy_from_user error\n");
            ret = -EFAULT;
            goto out;
        }

        //handle group counter request
        switch (gc_req.op) {
        case RDS_GROUP_CNT_READ:
            ret = fm_rds_group_cnt_get(fm, &gc_req.gc);
            break;
        case RDS_GROUP_CNT_WRITE:
            break;
        case RDS_GROUP_CNT_RESET:
            ret = fm_rds_group_cnt_reset(fm);
            break;
        default:
            break;
        }

        if (copy_to_user((void*)arg, &gc_req, sizeof(struct rds_group_cnt_req_t))) {
            WCN_DBG(FM_ALT | MAIN, "copy_to_user error\n");
            ret = -EFAULT;
            goto out;
        }

        break;
    }

    case FM_IOCTL_RDS_GET_LOG: {
        struct rds_raw_t rds_log;
        fm_s32 len;
        WCN_DBG(FM_DBG | MAIN, "......FM_IOCTL_RDS_GET_LOG......\n");
        //fetch a record form RDS log buffer
        ret = fm_rds_log_get(fm, (struct rds_rx_t*) & (rds_log.data), &len);
        rds_log.dirty = TRUE;
        rds_log.len = (len < sizeof(rds_log.data)) ? len : sizeof(rds_log.data);
        if (ret < 0) goto out;

        if (copy_to_user((void*)arg, &rds_log, rds_log.len + 2*sizeof(fm_s32))) {
            WCN_DBG(FM_ALT | MAIN, "copy_to_user error\n");
            ret = -EFAULT;
            goto out;
        }

        break;
    }

    case FM_IOCTL_RDS_BC_RST: {
        WCN_DBG(FM_DBG | MAIN, "FM_IOCTL_RDS_BC_RST\n");
        ret = fm_rds_block_cnt_reset(fm);
        break;
    }

    case FM_IOCTL_I2S_SETTING: {
        struct fm_i2s_setting i2s_cfg;
        WCN_DBG(FM_DBG | MAIN, "FM_IOCTL_I2S_SETTING\n");

        if (copy_from_user(&i2s_cfg, (void*)arg, sizeof(struct fm_i2s_setting))) {
            WCN_DBG(FM_ALT | MAIN, "i2s set, copy_from_user err\n");
            ret = -EFAULT;
            goto out;
        }

        ret = fm_i2s_set(fm, i2s_cfg.onoff, i2s_cfg.mode, i2s_cfg.sample);

        if (ret) {
            WCN_DBG(FM_ALT | MAIN, "Set i2s err\n");
            goto out;
        }

        break;
    }

    case FM_IOCTL_IS_DESE_CHAN: {
        fm_s32 tmp;
        WCN_DBG(FM_DBG | MAIN, "FM_IOCTL_IS_DESE_CHAN\n");

        if (copy_from_user(&tmp, (void*)arg, sizeof(fm_s32))) {
            WCN_DBG(FM_ALT | MAIN, "is dese chan, copy_from_user err\n");
            ret = -EFAULT;
            goto out;
        }

        tmp = fm_is_dese_chan(fm, (fm_u16)tmp);

        if (copy_to_user((void*)arg, &tmp, sizeof(fm_s32))) {
            WCN_DBG(FM_ALT | MAIN, "is dese chan, copy_to_user err\n");
            ret = -EFAULT;
            goto out;
        }

        break;
    }
    case FM_IOCTL_DESENSE_CHECK: 
    {
        fm_desense_check_t tmp;
        WCN_DBG(FM_DBG | MAIN, "FM_IOCTL_IS_DESE_CHAN\n");

        if (copy_from_user(&tmp, (void*)arg, sizeof(fm_desense_check_t))) 
        {
            WCN_DBG(FM_ALT | MAIN, "desene check, copy_from_user err\n");
            ret = -EFAULT;
            goto out;
        }
        ret = fm_desense_check(fm, (fm_u16)tmp.freq,tmp.rssi);

        /*if (copy_to_user((void*)arg, &tmp, sizeof(fm_desense_check_t))) {
            WCN_DBG(FM_ALT | MAIN, "desene check, copy_to_user err\n");
            ret = -EFAULT;
            goto out;
        }*/

        break;
    }
	case FM_IOCTL_SCAN_GETRSSI:
	{
		/*struct fm_rssi_req *req;
        WCN_DBG(FM_DBG | MAIN, "FM_IOCTL_SCAN_GETRSSI\n");
		if(!(req = fm_vmalloc(sizeof(struct fm_rssi_req))))
		{
            WCN_DBG(FM_ALT | MAIN, "fm_vmalloc err\n");
            ret = -EFAULT;
            goto out;
		}
		if(copy_from_user(req, (void*)arg, sizeof(struct fm_rssi_req)))
		{
            WCN_DBG(FM_ALT | MAIN, "copy_from_user err\n");
            ret = -EFAULT;
			fm_vfree(req);
            goto out;
		}
		ret = fm_get_rssi_after_scan(fm, req);
		if(-ERR_FW_NORES == ret){
            WCN_DBG(FM_ALT | MAIN, "fm_get_rssi_after_scan err\n");
		}
		if(copy_to_user((void*)arg, req, sizeof(struct fm_rssi_req)))
		{
            WCN_DBG(FM_ALT | MAIN, "copy_to_user err\n");
            ret = -EFAULT;
			fm_vfree(req);
            goto out;
		}
		*/
        WCN_DBG(FM_ALT | MAIN, "FM_IOCTL_SCAN_GETRSSI:not support\n");
		break;
	}

	case FM_IOCTL_DUMP_REG:
	{
		WCN_DBG(FM_NTC | MAIN,"......FM_IOCTL_DUMP_REG......\n");

		ret = fm_dump_reg();
		if(ret)
		{
			WCN_DBG(FM_ALT | MAIN, "fm_dump_reg err\n");
		}
		break;
	}
	case FM_IOCTL_GPS_RTC_DRIFT:{
		struct fm_gps_rtc_info rtc_info;
		WCN_DBG(FM_NTC | MAIN,"......FM_IOCTL_GPS_RTC_DRIFT......\n");
	
		if (fm_false == fm->chipon){
			WCN_DBG(FM_ERR | MAIN,"ERROR, FM chip is OFF\n");
            ret = -EFAULT;
            goto out;
		}
		if(copy_from_user(&rtc_info, (void*)arg, sizeof(struct fm_gps_rtc_info))){
			WCN_DBG(FM_ERR | MAIN,"copy_from_user error\n");
            ret = -EFAULT;
            goto out;
		}
		
		ret = fm_get_gps_rtc_info(&rtc_info);
		if(ret){
			WCN_DBG(FM_ERR | MAIN,"fm_get_gps_rtc_info error\n");
            goto out;
		}
		break;
	}
	case FM_IOCTL_OVER_BT_ENABLE:
	{
		fm_s32 fm_via_bt = -1;
		WCN_DBG(FM_NTC | MAIN,"......FM_IOCTL_OVER_BT_ENABLE......\n");
		
		if(copy_from_user(&fm_via_bt, (void*)arg, sizeof(int32_t))){
			WCN_DBG(FM_ERR | MAIN,"copy_from_user error\n");
			ret = -EFAULT;
			goto out;
		}
	
		ret = fm_over_bt(fm, fm_via_bt);
		if(ret)
		{
			WCN_DBG(FM_ERR | MAIN, "fm_over_bt err\n");
		}
		break;
	}
	
    /***************************FM Tx function************************************/
	case FM_IOCTL_TX_SUPPORT:
	{
		fm_s32 tx_support = -1;
		WCN_DBG(FM_NTC | MAIN,"......FM_IOCTL_TX_SUPPORT......\n");

		ret = fm_tx_support(fm,&tx_support);
		if(ret)
		{
			WCN_DBG(FM_ERR | MAIN, "fm_tx_support err\n");
		}
		if (copy_to_user((void*)arg, &tx_support, sizeof(fm_s32)))
		{
			WCN_DBG(FM_ERR | MAIN,"copy_to_user error\n");
			ret = -EFAULT;
			goto out;
		}
		break;
	}
	case FM_IOCTL_POWERUP_TX:
	{
		struct fm_tune_parm parm;
        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_POWERUP_TX:0\n");
        if (copy_from_user(&parm, (void*)arg, sizeof(struct fm_tune_parm))) {
            ret = -EFAULT;
            goto out;
        }

		ret = fm_powerup_tx(fm, &parm);
        if (ret < 0) {
            goto out;
        }
        ret = fm_tune_tx(fm, &parm);
        if (ret < 0) goto out;

        if (copy_to_user((void*)arg, &parm, sizeof(struct fm_tune_parm))) {
            ret = -EFAULT;
            goto out;
        }
        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_POWERUP_TX:1\n");
		break;
	}
	
	case FM_IOCTL_TUNE_TX:
	{
		struct fm_tune_parm parm;
        WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_TUNE_TX:0\n");

        if (copy_from_user(&parm, (void*)arg, sizeof(struct fm_tune_parm))) {
            ret = -EFAULT;
            goto out;
        }

        ret = fm_tune_tx(fm, &parm);
        if (ret < 0) {
            goto out;
        }

        if (copy_to_user((void*)arg, &parm, sizeof(struct fm_tune_parm))) {
            ret = -EFAULT;
            goto out;
        }

		WCN_DBG(FM_NTC | MAIN, "FM_IOCTL_TUNE_TX:1\n");
		break;
	}
	case FM_IOCTL_RDSTX_SUPPORT:
	{
		fm_s32 rds_tx_support = -1;
		WCN_DBG(FM_NTC | MAIN,"......FM_IOCTL_RDSTX_SUPPORT......\n");

		ret = fm_rdstx_support(fm,&rds_tx_support);
		if(ret)
		{
			WCN_DBG(FM_ERR | MAIN, "fm_rdstx_support err\n");
		}
		if (copy_to_user((void*)arg, &rds_tx_support, sizeof(fm_s32)))
		{
			WCN_DBG(FM_ERR | MAIN,"copy_to_user error\n");
			ret = -EFAULT;
			goto out;
		}
		break;
	}	
	
	case FM_IOCTL_RDSTX_ENABLE:
	{
		fm_s32 rds_tx_enable = -1;
		WCN_DBG(FM_NTC | MAIN,"......FM_IOCTL_RDSTX_ENABLE......\n");

		ret = fm_rdstx_enable(fm,&rds_tx_enable);
		if(ret)
		{
			WCN_DBG(FM_ERR | MAIN, "fm_rdstx_enable err\n");
		}
		if (copy_to_user((void*)arg, &rds_tx_enable, sizeof(fm_s32)))
		{
			WCN_DBG(FM_ERR | MAIN,"copy_to_user error\n");
			ret = -EFAULT;
			goto out;
		}
		break;
	}	
	
	case FM_IOCTL_RDS_TX:
	{
		struct fm_rds_tx_parm parm;
		WCN_DBG(FM_NTC | MAIN, "......FM_IOCTL_RDS_TX......\n");

		if (copy_from_user(&parm, (void*)arg, sizeof(struct fm_rds_tx_parm)))
		{
            WCN_DBG(FM_ALT | MAIN, "RDS Tx, copy_from_user err\n");
			ret = -EFAULT;
			goto out;
		}

		ret = fm_rds_tx(fm, &parm);
		if(ret)
		{
            WCN_DBG(FM_ALT | MAIN, "fm_rds_tx err\n");
		}

		if (copy_to_user((void*)arg, &parm, sizeof(struct fm_rds_tx_parm))){
            WCN_DBG(FM_ALT | MAIN, "RDS Tx, copy_to_user err\n");
			ret = -EFAULT;
			goto out;
		}
		break;
	}
	
	case FM_IOCTL_TX_SCAN:
	{
		struct fm_tx_scan_parm parm;
		WCN_DBG(FM_NTC | MAIN, "......FM_IOCTL_TX_SCAN......\n");

		if (copy_from_user(&parm, (void*)arg, sizeof(struct fm_tx_scan_parm))){
			WCN_DBG(FM_ALT | MAIN,"copy_from_user error\n");
			ret = -EFAULT;
			goto out;
		}
		ret = fm_tx_scan(fm, &parm);
		if(ret < 0){
			WCN_DBG(FM_ERR | MAIN,"FM_IOCTL_TX_SCAN failed\n");
		}
		if (copy_to_user((void*)arg, &parm, sizeof(struct fm_tx_scan_parm))){
			WCN_DBG(FM_ALT | MAIN,"copy_to_user error\n");
			ret = -EFAULT;
			goto out;
		}
		break;
	}

    default:
        ret = -EPERM;
    }

out:
    if (ret == -FM_EFW) {
        // subsystem reset
        fm_subsys_reset(fm);
    }

    return ret;
}

static loff_t fm_ops_lseek(struct file *filp, loff_t off, fm_s32 whence)
{
    struct fm *fm = filp->private_data;

    if (whence == SEEK_END) {
        fm_hwscan_stop(fm);
    } else if (whence == SEEK_SET) {
        FM_EVENT_SEND(fm->rds_event, FM_RDS_DATA_READY);
    }

    return off;
}

static ssize_t fm_ops_read(struct file *filp, char *buf, size_t len, loff_t *off)
{
    struct fm *fm = filp->private_data;
    fm_s32 copy_len = 0;

    if (!fm) {
        WCN_DBG(FM_ALT | MAIN, "fm_read invalid fm pointer\n");
        return 0;
    }

    if (!buf || len < sizeof(rds_t)) {
        WCN_DBG(FM_DBG | MAIN, "fm_read invliad buf\n");
        return 0;
    }

    //block when FM is resetting
    while (fm_sys_state_get(fm) != FM_SUBSYS_RST_OFF) {
        msleep_interruptible(100);
    }

    copy_len = sizeof(rds_t);

    return fm_rds_read(fm, buf, copy_len);
}

static fm_s32 fm_ops_open(struct inode *inode, struct file *filp)
{
    fm_s32 ret = 0;
    struct fm_platform *plat = container_of(inode->i_cdev, struct fm_platform, cdev);
    struct fm *fm = container_of(plat, struct fm, platform);

    WCN_DBG(FM_NTC | MAIN, "fm_ops_open:0\n");
    if (fm_sys_state_get(fm) != FM_SUBSYS_RST_OFF) {
        WCN_DBG(FM_ALT| MAIN, "FM subsys is resetting, retry later\n");
        ret = -FM_ESRST;
        return ret;
    }
    
    ret = fm_open(fm);
    filp->private_data = fm;

    WCN_DBG(FM_NTC | MAIN, "fm_ops_open:1\n");
    return ret;
}

static fm_s32 fm_ops_release(struct inode *inode, struct file *filp)
{
//    fm_s32 ret = 0;
//    struct fm_platform *plat = container_of(inode->i_cdev, struct fm_platform, cdev);
//    struct fm *fm = container_of(plat, struct fm, platform);

//    WCN_DBG(FM_NTC | MAIN, "fm_ops_release:0\n");
    //fm_close(fm);
    filp->private_data = NULL;

    WCN_DBG(FM_NTC | MAIN, "fm_ops_release\n");
    return 0;
}

static fm_s32 fm_ops_flush(struct file *filp,fl_owner_t Id)
{
    fm_s32 ret = 0;
    struct fm *fm = filp->private_data;

    WCN_DBG(FM_NTC | MAIN, "fm_ops_flush:0\n");
    fm_close(fm);
    filp->private_data = fm;

    WCN_DBG(FM_NTC | MAIN, "fm_ops_flush:1\n");
    return ret;
}

static fm_s32 fm_proc_read(char *page, char **start, off_t off, fm_s32 count, fm_s32 *eof, void *data)
{
    fm_s32 cnt = 0;
    struct fm *fm  = g_fm;

    WCN_DBG(FM_NTC | MAIN, "Enter fm_proc_read.\n");

    if (off != 0)
        return 0;

    if (!fm) {
        WCN_DBG(FM_ALT | MAIN, "para err\n");
        return 0;
    }

    if (fm->chipon && (fm_pwr_state_get(fm) == FM_PWR_RX_ON)) 
	{
        cnt = sprintf(page, "1\n");
		WCN_DBG(FM_NTC | MAIN, " FM_PWR_RX_ON\n");
    } 
	else if (fm->chipon && (fm_pwr_state_get(fm) == FM_PWR_TX_ON)) 
	{
		WCN_DBG(FM_NTC | MAIN, " FM_PWR_TX_ON\n");
        cnt = sprintf(page, "2\n");
    } 
	else 
	{
        cnt = sprintf(page, "0\n");
    }

    *eof = 1;
    WCN_DBG(FM_NTC | MAIN, "Leave fm_proc_read. cnt = %d\n", cnt);
    return cnt;
}

static fm_s32 fm_proc_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
    fm_s8 tmp_buf[50] = {0};
    fm_u32 copysize;

    copysize = (count < (sizeof(tmp_buf) - 1)) ? count : (sizeof(tmp_buf) - 1);

    WCN_DBG(FM_NTC | MAIN, "fm_proc_write:0\n");
    if (copy_from_user(tmp_buf, buffer, copysize)) {
        WCN_DBG(FM_ERR | MAIN, "failed copy_from_user\n");
        return -EFAULT;
    }

    if (strncmp(tmp_buf, "subsys reset", strlen("subsys reset")) == 0) {
        fm_subsys_reset(g_fm);
        return count;
    }
    
    if (!fm_cust_config_setup(tmp_buf)) {
        WCN_DBG(FM_NTC | MAIN, "get config form %s ok\n", tmp_buf);
        return count;
    }

    if (sscanf(tmp_buf, "%x", &g_dbg_level) != 1) {
        WCN_DBG(FM_ERR | MAIN, "failed g_dbg_level = 0x%x\n", g_dbg_level);
        return -EFAULT;
    }

    WCN_DBG(FM_NTC | MAIN, "fm_proc_write:1 g_dbg_level = 0x%x\n", g_dbg_level);
    return count;
}

#define FM_DEV_STATIC_ALLOC
#define FM_DEV_MAJOR    193
static int FM_major = FM_DEV_MAJOR;      /* dynamic allocation */

static fm_s32 fm_cdev_setup(struct fm *fm)
{
    fm_s32 ret = 0;
    struct fm_platform *plat = &fm->platform;

#ifdef FM_DEV_STATIC_ALLOC
    /*static allocate chrdev*/
    plat->dev_t = MKDEV(FM_major, 0);
    ret = register_chrdev_region(plat->dev_t, 1, FM_NAME);

    if (ret) {
        WCN_DBG(FM_ERR | MAIN, "%s():fail to register chrdev\n", __func__);
        return ret;
    }

#endif

#ifndef FM_DEV_STATIC_ALLOC
    ret = alloc_chrdev_region(&plat->dev_t, 0, 1, FM_NAME);

    if (ret) {
        WCN_DBG(FM_ALT | MAIN, "alloc dev_t failed\n");
        return ret;
    }

#endif

    WCN_DBG(FM_NTC | MAIN, "alloc %s:%d:%d\n", FM_NAME, MAJOR(plat->dev_t), MINOR(plat->dev_t));

    cdev_init(&plat->cdev, &fm_ops);

    plat->cdev.owner = THIS_MODULE;
    plat->cdev.ops = &fm_ops;

    ret = cdev_add(&plat->cdev, plat->dev_t, 1);

    if (ret) {
        WCN_DBG(FM_ALT | MAIN, "add dev_t failed\n");
        return ret;
    }

#ifndef FM_DEV_STATIC_ALLOC
    plat->cls = class_create(THIS_MODULE, FM_NAME);

    if (IS_ERR(plat->cls)) {
        ret = PTR_ERR(plat->cls);
        WCN_DBG(FM_ALT | MAIN, "class_create err:%d\n", ret);
        return ret;
    }

    plat->dev = device_create(plat->cls, NULL, plat->dev_t, NULL, FM_NAME);
#endif

    return ret;
}

static fm_s32  fm_cdev_destroy(struct fm *fm)
{
    FMR_ASSERT(fm);

    device_destroy(fm->platform.cls, fm->platform.dev_t);
    class_destroy(fm->platform.cls);
    cdev_del(&fm->platform.cdev);
    unregister_chrdev_region(fm->platform.dev_t, 1);

    return 0;
}

static fm_s32 fm_mod_init(fm_u32 arg)
{
    fm_s32 ret = 0;
    struct fm *fm = NULL;

    fm = fm_dev_init(0);

    if (!fm) {
        ret = -ENOMEM;
        goto ERR_EXIT;
    }

    if ((ret = fm_cdev_setup(fm))) {
        goto ERR_EXIT;
    }

    //fm proc file create "/proc/fm"
    g_fm_proc = create_proc_entry(FM_PROC_FILE, 0444, NULL);

    if (g_fm_proc == NULL) {
        WCN_DBG(FM_ALT | MAIN, "create_proc_entry failed\n");
        ret = -ENOMEM;
        goto ERR_EXIT;
    } else {
        g_fm_proc->read_proc = fm_proc_read;
        g_fm_proc->write_proc = fm_proc_write;
        WCN_DBG(FM_NTC | MAIN, "create_proc_entry success\n");
    }

    g_fm = fm;
    return 0;

ERR_EXIT:

    if (fm) {
        fm_cdev_destroy(fm);
        fm_dev_destroy(fm);
    }

    remove_proc_entry(FM_PROC_FILE, NULL);
    return ret;
}

static fm_s32 fm_mod_destroy(struct fm *fm)
{
    fm_s32 ret = 0;

    FMR_ASSERT(fm);

    WCN_DBG(FM_NTC | MAIN, "%s\n", __func__);
    remove_proc_entry(FM_PROC_FILE, NULL);
    fm_cdev_destroy(fm);
    fm_dev_destroy(fm);

    return ret;
}

static fm_s32 mt_fm_probe(struct platform_device *pdev)
{
    fm_s32 ret = 0;

    WCN_DBG(FM_NTC | MAIN, "%s\n", __func__);

    ret = fm_mod_init(0);

    if (ret) {
        WCN_DBG(FM_ERR | MAIN, "fm mod init err\n");
        return -ENOMEM;
    }

    return ret;
}

static fm_s32 mt_fm_remove(struct platform_device *pdev)
{
    WCN_DBG(FM_NTC | MAIN, "%s\n", __func__);

    fm_mod_destroy(g_fm);
    g_fm = NULL;
    return 0;
}

static struct platform_device mt_fm_device = {
    .name	= FM_NAME,
    .id	= -1,
};

//platform driver entry
static struct platform_driver mt_fm_dev_drv = {
    .probe   = mt_fm_probe,
    .remove  = mt_fm_remove,
    .driver = {
        .name   = FM_NAME,
        .owner  = THIS_MODULE,
    }
};

static fm_s32 __init mt_fm_init(void)
{
    fm_s32 ret = 0;

    ret = fm_env_setup();

    if (ret) {
        fm_env_destroy();
        return ret;
    }

    //register fm device to platform bus
    ret = platform_device_register(&mt_fm_device);

    if (ret) {
        return ret;
    }

    //register fm driver to platform bus
    ret = platform_driver_register(&mt_fm_dev_drv);

    if (ret) {
        return ret;
    }

    WCN_DBG(FM_NTC | MAIN, "6. fm platform driver registered\n");
    return ret;
}

static void __exit mt_fm_exit(void)
{
    platform_driver_unregister(&mt_fm_dev_drv);
    fm_env_destroy();
}


EXPORT_SYMBOL(g_dbg_level);
module_init(mt_fm_init);
module_exit(mt_fm_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek FM Driver");
MODULE_AUTHOR("Hongcheng <hongcheng.xia@MediaTek.com>");
