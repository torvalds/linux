/*
* ISP 3A State Machine
*
* Author: Kele Bai <kele.bai@amlogic.com>
*
* Copyright (C) 2010 Amlogic Inc.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*/
#include <linux/amlogic/tvin/tvin_v4l2.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "isp_drv.h"
#include "isp_hw.h"
#include "isp_sm.h"

#define DEVICE_NAME "isp"

static struct isp_sm_s sm_state;
static unsigned int capture_debug = 0;

#define AE_ORI_SET_DG			0x00000001
#define AE_SHUTTER_ADJUST_DG		0x00000002
#define AE_GAIN_ADJUST_SEL		0x00000004
#define AE_CALCULATE_LUMA_AVG_DG	0x00000008
#define AE_CALCULATE_LUMA_TARG_DG	0x00000010
#define AE_LUMA_AVG_CHECK_DG		0x00000020
#define AE_EXPOSURE_ADJUST_DG		0x00000040
#define AE_STATUS_DG			0x00000080
#define AE_DYNAMIC_ADJUST_DG    0x00000100

static unsigned int ae_sm_dg = 0;
module_param(ae_sm_dg,uint,0664);
MODULE_PARM_DESC(ae_sm_dg,"\n debug flag for ae.\n");

static unsigned int ae_step = 1;

module_param(ae_step,uint,0664);
MODULE_PARM_DESC(ae_step,"\n debug flag for ae.\n");

#define AWB_RGB_BLEND_DG		0x00000001
#define AWB_TEMP_CHECK_DG		0x00000002
#define AWB_TEMP_ADJUST_DG		0x00000004
#define AWB_GAIN_SET_DG			0x00000008
#define AWB_RGB_COUNT_CHECK_DG		0x00000010

static unsigned int awb_sm_dg = AWB_GAIN_SET_DG;
module_param(awb_sm_dg,uint,0664);
MODULE_PARM_DESC(awb_sm_dg,"\n debug flag for awb.\n");

static unsigned int exposure_extra = 1024;
module_param(exposure_extra,uint,0664);
MODULE_PARM_DESC(exposure_extra,"\n debug exposure for ae.\n");


#define P1DB 1149
#define N1DB  913
#define P2DB 1289
#define N2DB  813
#define P1DB5 1217
#define N1DB5 862

#define AF_DETECT			0x00000001
#define AF_FINE_TUNE			0x00000002
#define AF_BEST_STEP			0x00000004
static unsigned int af_sm_dg = 0;

volatile struct isp_ae_to_sensor_s ae_sens;


static inline int isp_ae_find_step(cam_function_t *func, unsigned int low, unsigned int hign, unsigned int gain)
{
	unsigned int mid = 0;
	unsigned int rate = 0;
	while(hign >= low){
		mid = (hign + low)/2;
		if(func&&func->get_aet_gain_by_step)
			rate = func->get_aet_gain_by_step(func->priv_data,mid);
		if(gain < rate)
			hign = mid - 1;
		else if(gain > rate)
			low = mid + 1;
		else {
			return mid;
		}
	}
	if((mid + 1) > hign){
		return hign;
	}
	return (mid + 1);
}

static unsigned int aet_gain_pre = 0, format_gain_pre = 0;

static unsigned int isp_ae_cal_new_para(isp_dev_t *devp)
{
    struct isp_ae_sm_s *aepa = &sm_state.isp_ae_parm;
    struct cam_function_s *func = &devp->cam_param->cam_function;
    unsigned int aet_gain_new = 0, format_gain_new = 0;

    aepa->max_step = func->get_aet_max_step(func->priv_data);
    format_gain_new = devp->isp_ae_parm->aet_fmt_gain;
    aet_gain_new = ((aet_gain_pre * format_gain_pre) / format_gain_new);
    if (aet_gain_new == 0) {
        pr_info("[isp] %s: cal ae error, aet_gain_pre:%d, format_gain_pre:%d ... ...\n", __func__, \
                aet_gain_pre, format_gain_pre);
        return 0;
    }
    ae_sens.new_step = isp_ae_find_step(func, 0, aepa->max_step, aet_gain_new);
    ae_sens.shutter = 1;
    ae_sens.gain = 1;
    pr_info("[isp] %s: format_gain_new:%d, aet_gain_new:%d new_step:%d... ...\n", __func__, \
            format_gain_new, aet_gain_new, ae_sens.new_step);

    if (func && func->set_aet_new_step) {
        func->set_aet_new_step(func->priv_data,ae_sens.new_step, ae_sens.shutter, ae_sens.gain);
        pr_info("[isp] %s: write new step to sensor... ...\n", __func__);
    }

    return 0;
}

static struct isp_awb_gain_s awb_gain, awb_gain_pre;

unsigned int isp_awb_load_pre_para(isp_dev_t *devp)
{
    if ((awb_gain_pre.r_val == 0) &&
        (awb_gain_pre.r_val == 0) &&
        (awb_gain_pre.r_val == 0)) {
        pr_info("[isp] %s cal awb error, r:%d g:%d b:%d ... ...\n",
                    __func__, awb_gain.r_val, awb_gain.g_val,awb_gain.b_val);
        return 0;
    }
    memcpy(&awb_gain, &awb_gain_pre, sizeof(struct isp_awb_gain_s));
    isp_awb_set_gain(awb_gain.r_val, awb_gain.g_val,awb_gain.b_val);
    pr_info("[isp] %s r:%d g:%d b:%d ... ...\n",
                __func__, awb_gain.r_val, awb_gain.g_val,awb_gain.b_val);

    return 0;
}
void isp_sm_init(isp_dev_t *devp)
{
	sm_state.isp_ae_parm.tf_ratio = devp->wave->torch_flash_ratio;
	sm_state.status = ISP_AE_STATUS_NULL;
	sm_state.flash = ISP_FLASH_STATUS_NULL;
	sm_state.isp_ae_parm.isp_ae_state = AE_INIT;
	sm_state.isp_awb_parm.isp_awb_state = AWB_INIT;
	sm_state.env = ENV_NULL;
	sm_state.ae_down = true;
	/*init for af*/
	sm_state.cap_sm.fr_time = devp->wave->flash_rising_time;
	sm_state.cap_sm.tr_time = devp->wave->torch_rising_time;
	/*init for wave*/
	sm_state.flash = ISP_FLASH_STATUS_NULL;
    isp_ae_cal_new_para(devp);     // cal and set new ae value
    isp_awb_load_pre_para(devp);  // cal and set new awb value
}

void isp_set_manual_exposure(isp_dev_t *devp)
{
	struct xml_algorithm_ae_s *aep = devp->isp_ae_parm;
	struct isp_ae_sm_s *aepa = &sm_state.isp_ae_parm;
	int i;
	int manual_target;
	if((aepa->targ > aep->targethigh)||(aepa->targ < aep->targetlow)){
		return;
	}
	i = devp->ae_info.manul_level;
	manual_target = aep->targetmid;
	if(i > 0){
		for(;i>0;i--){
			manual_target = (manual_target*P2DB+512) >> 10;
		}
	}
	else if(i < 0){
		for(;i<0;i++){
			manual_target = (manual_target*N2DB+512) >> 10;
		}
	}
	aepa->targ = manual_target;
    if(aepa->targ > aep->targethigh)
		aepa->targ = aep->targethigh;
    if(aepa->targ < aep->targetlow)
		aepa->targ = aep->targetlow;
	pr_info("devp->ae_info.manul_level=%d,targ=%d\n",devp->ae_info.manul_level,aepa->targ);
}

void af_sm_init(isp_dev_t *devp)
{
	struct isp_af_info_s *af_info = &devp->af_info;
	struct cam_function_s *func = &devp->cam_param->cam_function;
	/*init for af*/
	if(devp->flag & ISP_AF_SM_MASK){
    	        sm_state.af_state = AF_SCAN_INIT;
	}
	if(devp->flag & ISP_FLAG_CAPTURE){
		af_info->cur_step = af_info->capture_step;
		func->set_af_new_step(func->priv_data,devp->af_info.cur_step);
	}
}
int isp_ae_save_current_para(isp_dev_t *devp)
{
    struct cam_function_s *func = &devp->cam_param->cam_function;
    struct isp_ae_sm_s *aepa = &sm_state.isp_ae_parm;
    aepa->max_step = func->get_aet_max_step(func->priv_data);

    if (func && func->get_aet_gain_by_step)
        aet_gain_pre = func->get_aet_gain_by_step(func->priv_data,ae_sens.new_step);
    format_gain_pre = devp->isp_ae_parm->aet_fmt_gain;
    pr_info("[isp] %s format_gain_pre:%d aet_gain_pre:%d ... ...\n",
                __func__, format_gain_pre, aet_gain_pre);

    return 0;
}

unsigned int isp_tune_exposure(isp_dev_t *devp)
{
    struct isp_ae_sm_s *aepa = &sm_state.isp_ae_parm;
    struct cam_function_s *func = &devp->cam_param->cam_function;
    unsigned int new_step = 0;
	unsigned int gain_cur = 0;
	unsigned int gain_target = 0;
	if(func&&func->get_aet_current_gain)
	gain_cur = func->get_aet_current_gain(func->priv_data);
	gain_target = (gain_cur*exposure_extra + 512) >> 10;
	new_step = isp_ae_find_step(func, 0, aepa->max_step, gain_target);
	return new_step;
}
void isp_ae_sm(isp_dev_t *devp)
{
	struct isp_ae_stat_s *ae = &devp->isp_ae;
	struct xml_algorithm_ae_s *aep = devp->isp_ae_parm;
	struct isp_info_s *parm = &devp->info;
	struct isp_ae_sm_s *aepa = &sm_state.isp_ae_parm;
	struct cam_function_s *func = &devp->cam_param->cam_function;
	struct vframe_prop_s *ph = devp->frontend.private_data;
	struct isp_ae_info_s *ae_info = &devp->ae_info;
	unsigned int targrate,targstep,avg,avgo,i,radium_delta,lpfcoef,sub_avg[16] = {0};
	static unsigned int newstep,delay_vs,sum,targ_ag_adjust_dir;
	unsigned int avg_sum = 0;

    	switch(sm_state.isp_ae_parm.isp_ae_state){
		case AE_INIT:
			delay_vs = 0;
			aepa->win_l = (parm->h_active * aep->ratio_winl) >> 10;
			aepa->win_r = ((parm->h_active * aep->ratio_winr) >> 10) - 1;
			aepa->win_t = (parm->v_active * aep->ratio_wint) >> 10;
			aepa->win_b = ((parm->v_active * aep->ratio_winb) >> 10) - 1;
			isp_set_ae_win(aepa->win_l, aepa->win_r, aepa->win_t, aepa->win_b);
			aepa->pixel_sum = parm->h_active * parm->v_active;
			aepa->sub_pixel_sum = aepa->pixel_sum >> 4;
			aepa->max_lumasum1 = ((aepa->pixel_sum >> 4) * aep->ae_ratio_low) >> 7;
			aepa->max_lumasum2 = ((aepa->pixel_sum >> 4) * aep->ae_ratio_low2mid) >> 7;
			aepa->max_lumasum3 = ((aepa->pixel_sum >> 4) * aep->ae_ratio_mid2high) >> 7;
			aepa->max_lumasum4 = ((aepa->pixel_sum >> 4) * aep->ae_ratio_high) >> 7;
			if(devp->ae_info.manul_level==0)
				aepa->targ = aep->targetmid;
			targ_ag_adjust_dir = 1;/*0:up; 2:down; 1:stable*/
			if((func == NULL)||(func->get_aet_max_gain == NULL)||(func->get_aet_min_gain == NULL)||(func->get_aet_max_step == NULL)){
				pr_info("[tvin isp error][AE_INIT]%s:AE FUNC POINT TO NULL!\n",__func__);
				break;
			}
			else{
				aepa->max_gain = func->get_aet_max_gain(func->priv_data);
				aepa->min_gain = func->get_aet_min_gain(func->priv_data);
				aepa->max_step = func->get_aet_max_step(func->priv_data);
			}
			pr_info("ae,win_l=%d,win_r=%d,win_t=%d,win_b=%d\n",aepa->win_l,aepa->win_r,aepa->win_t,aepa->win_b);
			pr_info("aepa->max_lumasum1=%d,max_lumasum2=%d,=%d,=%d",aepa->max_lumasum1,aepa->max_lumasum2,aepa->max_lumasum3,aepa->max_lumasum4);
			pr_info("aepa->alert_r=%d,g=%d,b=%d\n",aepa->alert_r,aepa->alert_g,aepa->alert_b);
			pr_info("aepa->sub_pixel_sum=%d,aepa->max_gain=%d,aepa->min_gain=%d,aepa->max_step=%d\n",
				aepa->sub_pixel_sum,aepa->max_gain,aepa->min_gain,aepa->max_step);
			sm_state.isp_ae_parm.isp_ae_state = AE_SHUTTER_ADJUST;
			break;
		case AE_SHUTTER_ADJUST:
			if((func == NULL)||(func->get_aet_current_gain == NULL)||(func->get_aet_current_step == NULL)){
				pr_info("[tvin isp error][AE_SHUTTER_ADJUST]%s:AE FUNC POINT TO NULL!\n",__func__);
				break;
			}
			else{
				aepa->cur_gain = func->get_aet_current_gain(func->priv_data);
				aepa->pre_gain = aepa->cur_gain;
				aepa->cur_step = func->get_aet_current_step(func->priv_data);
			}
			if(ae_sm_dg&AE_SHUTTER_ADJUST_DG)
				pr_info("cur_gain = %d,cur_step = %d\n",aepa->cur_gain,aepa->cur_step);
			if(aepa->cur_gain == 0)
				break;
			/*calculate luma avg*/
			for(i=0;i<16;i++){
				sub_avg[i] = ae->luma_win[i]/aepa->sub_pixel_sum;
			    	avg_sum += sub_avg[i] * aep->coef_cur[i];
				if((i == ae_step)&&(ae_sm_dg&AE_CALCULATE_LUMA_AVG_DG))
					pr_info("sub_avg[%d]=%d,ae->luma_win=%d,aep->coef_cur=%d,aep->coef_env=%d,avg_sum=%d \n",i,sub_avg[i],ae->luma_win[i],aep->coef_cur[i],aep->coef_env[i],avg_sum);
			}
			avg = avg_sum >> 10;
			if(devp->flag & ISP_FLAG_CAPTURE){
				avgo = avg/aepa->cur_gain;
				if(avgo < aep->flash_thr)
					sm_state.flash = ISP_FLASH_STATUS_ON;
				else
					sm_state.flash = ISP_FLASH_STATUS_OFF;
			}
			/*calculate luma targ*/
			sum = (ph->hist.gamma[58]+ph->hist.gamma[57]) << ph->hist.hist_pow;
			if(ae_sm_dg&AE_CALCULATE_LUMA_TARG_DG){
				pr_info("avg=%d,aepa->cur_gain=%d,avg_sum=%d,luma_win[9]=%d,sub[9]=%d\n",avg,aepa->cur_gain,avg_sum,ae->luma_win[9],sub_avg[9]);
				pr_info("ph->hist.gamma[58]=%d,%d,%d\n",ph->hist.gamma[58],sum,ph->hist.hist_pow);
			}
			if((devp->ae_info.manul_level==0)&&((ae_sm_dg&AE_DYNAMIC_ADJUST_DG)==0)){
				if((sum > aepa->max_lumasum4)||((sum > aepa->max_lumasum3)&&(targ_ag_adjust_dir==2))){
					targ_ag_adjust_dir = 2;
					aepa->targ-=2;
					if(aepa->targ<(aep->targetmid-aep->ae_min_diff))
						aepa->targ = aep->targetmid-aep->ae_min_diff;
				}
				else if((sum < aepa->max_lumasum1)||((sum < aepa->max_lumasum2)&&(targ_ag_adjust_dir==0))){
					targ_ag_adjust_dir = 0;
					aepa->targ++;
					if(aepa->targ>(aep->targetmid+aep->ae_max_diff))
						aepa->targ = aep->targetmid+aep->ae_max_diff;
				}
				else{
					targ_ag_adjust_dir = 1;
				}
			}
			radium_delta = (avg > aepa->targ) ? (avg - aepa->targ):(aepa->targ - avg);
			sm_state.env = ENV_LOW;
			//isp_set_ae_thrlpf(aep->thr_r_low, aep->thr_g_low, aep->thr_b_low, aep->lpftype_low);
			if(ae_sm_dg&AE_CALCULATE_LUMA_TARG_DG)
				pr_info("avg=%d,targ=%d,targ_ag_adjust_dir=%d\n",avg,aepa->targ,targ_ag_adjust_dir);
			/*luma avg check*/
			if(((sm_state.status == ISP_AE_STATUS_UNSTABLE)&&(radium_delta > aep->radium_inner_l))
				||(radium_delta > aep->radium_outer_l)){
				sm_state.status = ISP_AE_STATUS_UNSTABLE;
			}
			else{
				if(ae_sm_dg&AE_STATUS_DG)
					pr_info("ISP_AE_STATUS_STABLE\n");
				sm_state.status = ISP_AE_STATUS_STABLE;
				sm_state.isp_ae_parm.isp_ae_state = AE_SHUTTER_ADJUST;
				break;
			}
			/*exposure adjust*/
			if(avg == 0)
				break;
			targrate = (aepa->targ * aepa->cur_gain)/avg;
			if(targrate > aepa->max_gain)
				targrate = aepa->max_gain;
			if(targrate < aepa->min_gain)
				targrate = aepa->min_gain;
			targstep = isp_ae_find_step(func,0,aepa->max_step,targrate);
			if(targstep > aepa->max_step)
				targstep = aepa->max_step;
			lpfcoef = (devp->flag & ISP_FLAG_CAPTURE)?aep->fast_lpfcoef:aep->slow_lpfcoef;
			newstep = (aepa->cur_step*lpfcoef + targstep*(256-lpfcoef))>>8;
			if(ae_sm_dg&AE_EXPOSURE_ADJUST_DG)
				pr_info("targstep = %d,targrate =%d,newstep =%d,lpf =%d,cur_step =%d\n",
				targstep,targrate,newstep,lpfcoef,aepa->cur_step);
			if((newstep >= aepa->max_step - 1)||(newstep == aepa->cur_step)||(newstep == 0)){
				sm_state.status = ISP_AE_STATUS_STABLE;
				if(ae_sm_dg&AE_STATUS_DG)
					pr_info("ISP_AE_STATUS_STABLE2\n");
				break;
			}
			/*set newstep*/
			if(aep->ae_skip[0] == 0x1){
				if(atomic_read(&ae_info->writeable) <= 0){
					ae_sens.new_step = newstep;
					ae_sens.shutter = 0;
					ae_sens.gain = 1;
					atomic_set(&ae_info->writeable,1);
					sm_state.status = ISP_AE_STATUS_UNSTABLE;
					if(ae_sm_dg&AE_STATUS_DG)
						pr_info("ISP_AE_STATUS_UNSTABLE1\n");
					sm_state.ae_down = false;
				}
				if(ae_sm_dg&AE_GAIN_ADJUST_SEL)
					sm_state.isp_ae_parm.isp_ae_state = AE_GAIN_ADJUST;
				else
					sm_state.isp_ae_parm.isp_ae_state = AE_SHUTTER_ADJUST;
				break;
			}
			else if(aep->ae_skip[1] == 0x1){
				ae_sens.new_step = newstep;
				ae_sens.shutter = 1;
				ae_sens.gain = 1;
				atomic_set(&ae_info->writeable,1);
				sm_state.status = ISP_AE_STATUS_UNSTABLE;
				if(ae_sm_dg&AE_STATUS_DG)
					pr_info("ISP_AE_STATUS_UNSTABLE\n");
				sm_state.ae_down = false;
				sm_state.isp_ae_parm.isp_ae_state = AE_REST;
				break;
			}
			else{
				ae_sens.new_step = newstep;
				ae_sens.shutter = 1;
				ae_sens.gain = 0;
				atomic_set(&ae_info->writeable,1);
				sm_state.status = ISP_AE_STATUS_UNSTABLE;
				if(ae_sm_dg&AE_STATUS_DG)
					pr_info("ISP_AE_STATUS_UNSTABLE\n");
				sm_state.ae_down = false;
				sm_state.isp_ae_parm.isp_ae_state = AE_GAIN_ADJUST;//AE_REST;
				break;
			}
			break;
		case AE_GAIN_ADJUST:
			if(atomic_read(&ae_info->writeable) <= 0){
				ae_sens.new_step = newstep;
				ae_sens.shutter = 0;
				ae_sens.gain = 1;
				atomic_set(&ae_info->writeable,1);
				sm_state.isp_ae_parm.isp_ae_state = AE_REST;
			}
			break;
		case AE_REST:
			sm_state.ae_down = true;
			if(atomic_read(&ae_info->writeable) <= 0)
				delay_vs++;
			if(delay_vs > ae_step){
				delay_vs = 0;
				sm_state.isp_ae_parm.isp_ae_state = AE_SHUTTER_ADJUST;
			}
			break;
		default:
			break;
    	}
}

// VDIN_MATRIX_YUV601_RGB
//	-16 	1.164  0	  1.596 	 0
// -128 	1.164 -0.391 -0.813 	 0
// -128 	1.164  2.018  0 		 0
static inline int matrix_yuv601_rgb_r(unsigned int y, unsigned int u, unsigned int v)
{
	return (((y-16)*1192+(u-128)*0+(v-128)*1634+0) >> 10);
}

static inline int matrix_yuv601_rgb_g(unsigned int y, unsigned int u, unsigned int v)
{
	return (((y-16)*1192+(u-128)*(-400)+(v-128)*(-833)+0) >> 10);
}

static inline int matrix_yuv601_rgb_b(unsigned int y, unsigned int u, unsigned int v)
{
	return (((y-16)*1192+(u-128)*2066+(v-128)*0+0) >> 10);
}

// VDIN_MATRIX_YUV709_RGB
//	-16 	1.164  0	  1.793 	 0
// -128 	1.164 -0.213 -0.534 	 0
// -128 	1.164  2.115  0 		 0
static inline int matrix_yuv709_rgb_r(unsigned int y, unsigned int u, unsigned int v)
{
	return (((y-16)*1192+(u-128)*0+(v-128)*1836+0) >> 10);
}

static inline int matrix_yuv709_rgb_g(unsigned int y, unsigned int u, unsigned int v)
{
	return (((y-16)*1192+(u-128)*(-218)+(v-128)*(-547)+0) >> 10);
}

static inline int matrix_yuv709_rgb_b(unsigned int y, unsigned int u, unsigned int v)
{
	return (((y-16)*1192+(u-128)*2166+(v-128)*0+0) >> 10);
}

//static unsigned int r_val = 256;
//static unsigned int g_val = 390;//256;
//static unsigned int b_val = 256;

int isp_awb_save_current_para(isp_dev_t *devp)
{
    isp_awb_get_gain(&awb_gain_pre);

    pr_info("[isp] %s save awb, r:%d g:%d b:%d ... ...\n",
                __func__, awb_gain_pre.r_val, awb_gain_pre.g_val,awb_gain_pre.b_val);

    return 0;
}
void isp_awb_sm(isp_dev_t *devp)
{
	struct isp_awb_stat_s *awb = &devp->isp_awb;
	struct xml_algorithm_awb_s *awbp = devp->isp_awb_parm;
	struct isp_info_s *parm = &devp->info;
	struct isp_awb_sm_s *awba = &sm_state.isp_awb_parm;
	unsigned int rg,bg,target_r,target_b,i,yy,countlimity,cnt,u,v;
	awb_yuv_stat_t yuv_ex[4];
	u16 r[5] = {0};      //0,rgb;1,ym;2,yh;3,yl;4,final.
	u16 g[5] = {0};
	u16 b[5] = {0};

	switch(sm_state.isp_awb_parm.isp_awb_state){
		case AWB_INIT:
			awba->win_l = (parm->h_active * awbp->ratio_winl) >> 10;
			awba->win_r = ((parm->h_active * awbp->ratio_winr) >> 10) - 1;
			awba->win_t = (parm->v_active * awbp->ratio_wint) >> 10;
			awba->win_b = ((parm->v_active * awbp->ratio_winb) >> 10) - 1;
			pr_info("awb,win_l=%d,win_r=%d,win_t=%d,win_b=%d\n",awba->win_l,awba->win_r,awba->win_t,awba->win_b);
			isp_set_awb_win(awba->win_l, awba->win_r, awba->win_t, awba->win_b);
			if(sm_state.env == ENV_HIGH){
				isp_set_awb_yuv_thr(awbp->thr_yh_h, awbp->thr_yl_h, awbp->thr_du_h, awbp->thr_dv_h);
				isp_set_awb_rgb_thr(awbp->thr_gb_h, awbp->thr_gr_h, awbp->thr_br_h);
			}
			else if(sm_state.env == ENV_MID){
				isp_set_awb_yuv_thr(awbp->thr_yh_m, awbp->thr_yl_m, awbp->thr_du_m, awbp->thr_dv_m);
				isp_set_awb_rgb_thr(awbp->thr_gb_m, awbp->thr_gr_m, awbp->thr_br_m);
			}
			else{/*sm_state.env == ENV_LOW or ENV_NULL*/
				isp_set_awb_yuv_thr(awbp->thr_yh_l, awbp->thr_yl_l, awbp->thr_du_l, awbp->thr_dv_l);
				isp_set_awb_rgb_thr(awbp->thr_gb_l, awbp->thr_gr_l, awbp->thr_br_l);
			}
			awba->pixel_sum = parm->h_active * parm->v_active;
			awba->countlimitrgb = ((awba->pixel_sum >> 2) * awbp->ratio_rgb) >> 6;
			awba->countlimityh = ((awba->pixel_sum >> 2) * awbp->ratio_yh) >> 6;
			awba->countlimitym = ((awba->pixel_sum >> 2) * awbp->ratio_ym) >> 6;
			awba->countlimityl = ((awba->pixel_sum >> 2) * awbp->ratio_yl) >> 6;
			awba->status = ISP_AWB_STATUS_STABLE;
			sm_state.isp_awb_parm.isp_awb_state = AWB_CHECK;
			break;
		case AWB_CHECK:
			if(awb_sm_dg&AWB_RGB_COUNT_CHECK_DG)
				pr_info("awb->rgb.rgb_count=%d\n",awb->rgb.rgb_count);
			/*AWB_RGB_COUNT_CHECK & CAL*/
			if(awb->rgb.rgb_count >= awba->countlimitrgb){
				r[0] = awb->rgb.rgb_sum[0]/awb->rgb.rgb_count;
				g[0] = awb->rgb.rgb_sum[1]/awb->rgb.rgb_count;
				b[0] = awb->rgb.rgb_sum[2]/awb->rgb.rgb_count;
			}
			else{
				r[0] = 0;
				g[0] = 0;
				b[0] = 0;
			}
			/*AWB_YUV_COUNT_CHECK & CAL*/
			for(i = 0;i < 3;i++){
				if(i == 0){
					yy = awbp->yym;
					countlimity = awba->countlimitym;
					memcpy(yuv_ex,awb->yuv_mid,4*sizeof(awb_yuv_stat_t));
				}
				else if(i == 1){
					yy = awbp->yyh;
					countlimity = awba->countlimityh;
					memcpy(yuv_ex,awb->yuv_high,4*sizeof(awb_yuv_stat_t));
				}
				else if(i == 2){
					yy = awbp->yyl;
					countlimity = awba->countlimityl;
					memcpy(yuv_ex,awb->yuv_low,4*sizeof(awb_yuv_stat_t));
				}
				cnt = yuv_ex[0].count + yuv_ex[1].count;
				if(cnt >= countlimity){
					u = (yuv_ex[1].sum - yuv_ex[0].sum)/cnt;
					v = (yuv_ex[3].sum - yuv_ex[2].sum)/cnt;
					if(parm->v_active >= 720){
						r[i+1] = matrix_yuv709_rgb_r(yy,u+128,v+128);
						g[i+1] = matrix_yuv709_rgb_g(yy,u+128,v+128);
						b[i+1] = matrix_yuv709_rgb_b(yy,u+128,v+128);
					}
					else{
						r[i+1] = matrix_yuv601_rgb_r(yy,u+128,v+128);
						g[i+1] = matrix_yuv601_rgb_g(yy,u+128,v+128);
						b[i+1] = matrix_yuv601_rgb_b(yy,u+128,v+128);
					}
				}
				else{
					r[i+1] = 0;
					g[i+1] = 0;
					b[i+1] = 0;
				}
			}
			/*AWB_RGB_BLEND*/
			r[4] = (r[0]*awbp->coef_r[0]+r[1]*awbp->coef_r[1]+r[2]*awbp->coef_r[2]+r[3]*awbp->coef_r[3])>>8;
			g[4] = (g[0]*awbp->coef_g[0]+g[1]*awbp->coef_g[1]+g[2]*awbp->coef_g[2]+g[3]*awbp->coef_g[3])>>8;
			b[4] = (b[0]*awbp->coef_b[0]+b[1]*awbp->coef_b[1]+b[2]*awbp->coef_b[2]+b[3]*awbp->coef_b[3])>>8;
			if(awb_sm_dg&AWB_RGB_BLEND_DG)
				pr_info("r=%d,%d,%d,%d,%d,g=%d,%d,%d,%d,%d,b=%d,%d,%d,%d,%d\n",r[0],r[1],r[2],r[3],r[4],g[0],g[1],g[2],g[3],g[4],b[0],b[1],b[2],b[3],b[4]);
			/*check & adjust*/
			if((g[4] == 0)||(r[4] == 0)||(b[4] == 0)){
				break;
			}
			rg = (r[4] << 10)/g[4];
			bg = (b[4] << 10)/g[4];
			if(awb_sm_dg&AWB_TEMP_CHECK_DG)
				pr_info("rg=%d,bg=%d\n",rg,bg);
			if(((awba->status == ISP_AWB_STATUS_UNSTABLE) && ((rg > 1024 + awbp->inner_rg)||(rg < 1024 - awbp->inner_rg)))
				||((rg > 1024 + awbp->outer_rg)||(rg < 1024 - awbp->outer_rg))
				||((awba->status == ISP_AWB_STATUS_UNSTABLE) && ((bg > 1024 + awbp->inner_bg)||(bg < 1024 - awbp->inner_bg)))
				||((bg > 1024 + awbp->outer_bg)||(bg < 1024 - awbp->outer_bg))
				){
				awba->status = ISP_AWB_STATUS_UNSTABLE;
				isp_awb_get_gain(&awb_gain);
				target_r = (awb_gain.r_val<<10)/rg;
				target_b = (awb_gain.b_val<<10)/bg;
				if(awb_sm_dg&AWB_TEMP_ADJUST_DG)
					pr_info("cur_r_val=%d,cur_b_val=%d,target_r=%d,target_b=%d\n",
					awb_gain.r_val,awb_gain.b_val,target_r,target_b);
				target_r = (target_r > awbp->r_max) ? awbp->r_max : target_r;
				target_r = (target_r < awbp->r_min) ? awbp->r_min : target_r;
				target_b = (target_b > awbp->b_max) ? awbp->b_max : target_b;
				target_b = (target_b < awbp->b_min) ? awbp->b_min : target_b;
				if(awb_sm_dg&AWB_GAIN_SET_DG)
					isp_awb_set_gain(target_r,awb_gain.g_val,target_b);
			}
			else{
				awba->status = ISP_AWB_STATUS_STABLE;
				break;
			}
			break;
		default:
			break;
	}
}
unsigned long long div64(unsigned long long n, unsigned long long d) // n for numerator, d for denominator
{
    unsigned int n_bits = 0, d_bits = 0, i = 0;
    unsigned long long q = 0, t = 0; // q for quotient, t for temporary
    // invalid
    if (!d) {
        q = 0xffffffffffffffff;
    }
    // (0.5, 0]
    else if (n + n < d) {
        q = 0;
    }
    // [1.0, 0.5]
    else if (n <= d) {
        q = 1;
    }
    // [max, 1.0)
    else
    {
        // get n_bits
        for (n_bits = 1; n_bits <= 64; n_bits++)
            if (!(n >> n_bits))
                break;
        if (n_bits > 64)
		n_bits = 64;
		// get d_bits
        for (d_bits = 1; d_bits <= 64; d_bits++)
            if (!(d >> d_bits))
                break;
        if (d_bits > 64)
            d_bits = 64;
        // check integer part
        for (i = n_bits; i >= d_bits; i--) {
            q <<= 1;
            t = d << (i - d_bits);
            if (n >= t)
            {
                n -= t;
                q += 1;
            }
        }
        // check fraction part
        if (n + n >= d)
            q += 1;
    }
    return q;
}
static unsigned long long isp_abs64(unsigned long long a,unsigned long long b)
{
	return (a>b?(a-b):(b-a));
}
static unsigned long long get_fv_base_blnr(isp_blnr_stat_t *blnr)
{
	unsigned long long sum_ac = 0, sum_dc = 0, mul_ac = 0;
	sum_ac  = (unsigned long long)blnr->ac[0];
	sum_ac += (unsigned long long)blnr->ac[1];
	sum_ac += (unsigned long long)blnr->ac[2];
	sum_ac += (unsigned long long)blnr->ac[3];

	sum_dc  = (unsigned long long)blnr->dc[0];
	sum_dc += (unsigned long long)blnr->dc[1];
	sum_dc += (unsigned long long)blnr->dc[2];
	sum_dc += (unsigned long long)blnr->dc[3];

	mul_ac = (sum_ac > 0x00000000ffffffff) ? 0xffffffffffffffff : sum_ac*sum_ac;
	sum_ac = (unsigned long long)blnr->af_ac[0];
	return sum_ac;
	//return div64(mul_ac,sum_dc);
}
static bool isp_af_is_lost_focus(isp_af_info_t *af_info,xml_algorithm_af_t *af_alg)
{
	unsigned long long *v_dc,sum_vdc=0,ave_vdc=0,delta_dc=0,tmp_vdc=0;
	unsigned int i=0,dc0,dc1,dc2,dc3,static_cnt;
	bool is_move=false,is_static=false;
	v_dc = (unsigned long long *)af_info->v_dc;
	/*calc v dc*/
	dc0 = af_info->last_blnr.dc[0];
	dc1 = af_info->last_blnr.dc[1];
	dc2 = af_info->last_blnr.dc[2];
	dc3 = af_info->last_blnr.dc[3];
	for(i=0;i<af_alg->detect_step_cnt;i++){
		delta_dc = isp_abs64(dc0,af_info->af_detect[i].dc[0]);
		v_dc[i]  = div64((delta_dc*1024),(unsigned long long)dc0);
		delta_dc = isp_abs64(dc1,af_info->af_detect[i].dc[1]);
		v_dc[i] += div64((delta_dc*1024),(unsigned long long)dc1);
		delta_dc = isp_abs64(dc2,af_info->af_detect[i].dc[2]);
		v_dc[i] += div64((delta_dc*1024),(unsigned long long)dc2);
		delta_dc = isp_abs64(dc3,af_info->af_detect[i].dc[3]);
		v_dc[i] += div64((delta_dc*1024),(unsigned long long)dc3);
		sum_vdc += v_dc[i];
	}
	ave_vdc = div64(sum_vdc,af_alg->detect_step_cnt);

	static_cnt = 0;
	for(i=0;i<af_alg->detect_step_cnt;i++){
		delta_dc = isp_abs64(v_dc[i],ave_vdc);
		tmp_vdc = div64(delta_dc*1024,af_alg->enter_static_ratio);
		if(!af_info->last_move){
			if((ave_vdc > af_alg->ave_vdc_thr)||(v_dc[i] > af_alg->ave_vdc_thr)){
			        is_move = true;
				break;
			}
		}else if(tmp_vdc < ave_vdc){
			if(++static_cnt >= af_alg->detect_step_cnt)
				is_static = true;
		}
	}
	/* enter move from static */
	if(is_move){
		if(af_sm_dg&AF_DETECT)
			pr_info("0->1\n");
		af_info->last_move = true;
		return false;
	/* during hysteresis ,still last state*/
	}else if((!is_static&&!is_move)||!af_info->last_move){
		if(af_sm_dg&AF_DETECT){
			pr_info("ave_vdc:%llu,af_info->last_move:%d keep last state.\n",ave_vdc,af_info->last_move);
		}
		return false;
	}
	if(af_sm_dg&AF_DETECT)
		pr_info("1->0\n");
	/*enter static from move,trigger full scan*/
	return true;
}
void isp_af_detect(isp_dev_t *devp)
{
	struct xml_algorithm_af_s *af_alg = devp->isp_af_parm;
	struct isp_af_info_s *af_info = &devp->af_info;

	switch(sm_state.af_state){
		case AF_DETECT_INIT:
			isp_set_blenr_stat(af_info->x0,af_info->y0,af_info->x1,af_info->y1);
			af_info->cur_index = 0;
			sm_state.af_state = AF_GET_STEPS_INFO;
			break;
		case AF_GET_STEPS_INFO:
			memcpy(&af_info->af_detect[af_info->cur_index],&af_info->isr_af_data,sizeof(isp_blnr_stat_t));
			if(++af_info->cur_index >= af_alg->detect_step_cnt){
				if(af_sm_dg&AF_DETECT)
					pr_info("%s get info end index=%u .\n",__func__,af_info->cur_index);
				af_info->cur_index = 0;
				sm_state.af_state = AF_GET_STATUS;
			}
			break;
		case AF_GET_STATUS:
			if(isp_af_is_lost_focus(af_info,af_alg)){
				sm_state.af_state = AF_SCAN_INIT;
				if(af_sm_dg&AF_DETECT)
					pr_info("[af_sm]:lost focus.\n");
			}else if(af_info->cur_index < af_alg->detect_step_cnt){
				memcpy(&af_info->af_detect[af_info->cur_index],&af_info->isr_af_data,sizeof(isp_blnr_stat_t));
				af_info->cur_index++;
			}else{
				af_info->cur_index = 0;
			}
			break;
		default:
			isp_af_fine_tune(devp);
			break;
	}
}
static unsigned int check_hillside(isp_af_info_t *af_info,xml_algorithm_af_t *af_alg)
{
	unsigned int cur_ac,last_ac,delta_ac,delta_ac_ratio,ret;
	if(af_info->valid_step_cnt < 2)
		return 0;//avoid first step
	cur_ac = af_info->af_fine_data[af_info->valid_step_cnt - 1].af_data.af_ac[0];
	last_ac = af_info->af_fine_data[af_info->valid_step_cnt - 2].af_data.af_ac[0];
	delta_ac = isp_abs64(cur_ac,last_ac);
	if((cur_ac == 0)||(last_ac == 0)){/*avoid wrong*/
		if(af_sm_dg&AF_FINE_TUNE)
			pr_info("[check_hillside]error:ac is 0\n");
		return 3;
	}
	delta_ac_ratio = delta_ac*100/((last_ac < cur_ac)?cur_ac:last_ac);
	if(af_sm_dg&AF_FINE_TUNE)
		pr_info("[check hillside]delta_ac_ratio:%d,cur_ac:%d,last_ac:%d .\n",delta_ac_ratio,cur_ac,last_ac);
	if((last_ac > cur_ac)&&(delta_ac_ratio > af_alg->hillside_fall)){
		ret = 1;//fall fillside
	}
	else if((last_ac < cur_ac)&&(delta_ac_ratio > af_alg->hillside_fall)){
		return 2;//up fillside
	}
	else
		ret = 0;//platform
	return ret;

}
static unsigned int isp_af_get_fine_step(isp_af_info_t *af_info,xml_algorithm_af_t *af_alg)
{
        unsigned int i = 0, j = 0,cur_grid = 0, max_grid = 0, best_step = 0;
        unsigned long long delta_fv,fv[FOCUS_GRIDS], max_fv = 0, min_fv = 0xffffffffffffffff, sum_fv = 0,moment = 0;
	isp_af_fine_tune_t af_fine_data_ex;
	for(i = 0; i < af_info->valid_step_cnt; i++){
		for(j = i+1; j < af_info->valid_step_cnt; j++ ){
			if(af_info->af_fine_data[j].cur_step < af_info->af_fine_data[i].cur_step){
				memcpy(&af_fine_data_ex,&af_info->af_fine_data[i],sizeof(isp_af_fine_tune_t));
				memcpy(&af_info->af_fine_data[i],&af_info->af_fine_data[j],sizeof(isp_af_fine_tune_t));
				memcpy(&af_info->af_fine_data[j],&af_fine_data_ex,sizeof(isp_af_fine_tune_t));
			}
		}
	}
	if(af_sm_dg&AF_BEST_STEP)
		pr_info("%s ac[0] ac[1] ac[2] ac[3] dc[0] dc[1] dc[2] dc[3] af0_ac af1_ac\n", __func__);
        for (i = 0; i < af_info->valid_step_cnt; i++){
                if (i && (af_info->af_fine_data[i].cur_step==0))
                        break;
                max_grid = i;
                fv[i] = get_fv_base_blnr(&af_info->af_fine_data[i].af_data);
	        if(af_sm_dg&AF_BEST_STEP)
                        pr_info("%s %u %u %u %u %u %u %u %u %u %u\n", __func__, af_info->af_fine_data[i].af_data.ac[0], af_info->af_fine_data[i].af_data.ac[1], af_info->af_fine_data[i].af_data.ac[2],
                        af_info->af_fine_data[i].af_data.ac[3], af_info->af_fine_data[i].af_data.dc[0], af_info->af_fine_data[i].af_data.dc[1], af_info->af_fine_data[i].af_data.dc[2], af_info->af_fine_data[i].af_data.dc[3],
                        af_info->af_fine_data[i].af_data.af_ac[0],af_info->af_fine_data[i].af_data.af_ac[1]);
                if (max_fv < fv[i]){
		        max_fv = fv[i];
		        cur_grid = i;
	        }
		if(min_fv > fv[i])
			min_fv = fv[i];
        }
	// too less stroke, for power saving
        if (!cur_grid) {
	        best_step = af_info->af_fine_data[0].cur_step;
        }
        // too much stroke
        else if (cur_grid == max_grid){
	        best_step = af_info->af_fine_data[max_grid].cur_step;
	}
	// work out best step with 3 grids
	else if ((cur_grid == 1) || (cur_grid == max_grid - 1)){
                moment += fv[cur_grid - 1]*(unsigned long long)af_info->af_fine_data[cur_grid - 1].cur_step;
                moment += fv[cur_grid    ]*(unsigned long long)af_info->af_fine_data[cur_grid    ].cur_step;
                moment += fv[cur_grid + 1]*(unsigned long long)af_info->af_fine_data[cur_grid + 1].cur_step;
                sum_fv += fv[cur_grid - 1];
                sum_fv += fv[cur_grid    ];
                sum_fv += fv[cur_grid + 1];
                best_step = (unsigned int)div64(moment,sum_fv);
	}
	// work out best step with 5 grids
        else {
                moment += (unsigned long long)fv[cur_grid - 2]*(unsigned long long)af_info->af_fine_data[cur_grid - 2].cur_step;
                moment += (unsigned long long)fv[cur_grid - 1]*(unsigned long long)af_info->af_fine_data[cur_grid - 1].cur_step;
                moment += (unsigned long long)fv[cur_grid    ]*(unsigned long long)af_info->af_fine_data[cur_grid    ].cur_step;
                moment += (unsigned long long)fv[cur_grid + 1]*(unsigned long long)af_info->af_fine_data[cur_grid + 1].cur_step;
                moment += (unsigned long long)fv[cur_grid + 2]*(unsigned long long)af_info->af_fine_data[cur_grid + 2].cur_step;
                sum_fv += fv[cur_grid - 2];
                sum_fv += fv[cur_grid - 1];
                sum_fv += fv[cur_grid    ];
                sum_fv += fv[cur_grid + 1];
                sum_fv += fv[cur_grid + 2];
                best_step = (unsigned int)div64(moment,sum_fv);
	}
	delta_fv = div64(100*(max_fv-min_fv),max_fv);
	if(af_sm_dg&AF_BEST_STEP)
		pr_info("%s:get best step %u,delta_fv:%lld.\n",__func__,best_step,delta_fv);
	return best_step;
}
void isp_af_fine_tune(isp_dev_t *devp)
{
	static unsigned int af_delay=0;
	struct xml_algorithm_af_s *af_alg = devp->isp_af_parm;
	struct isp_af_info_s *af_info = &devp->af_info;
	af_delay++;

	switch(sm_state.af_state){
		case AF_SCAN_INIT:
			devp->cmd_state = CAM_STATE_DOING;
			isp_set_blenr_stat(af_info->x0,af_info->y0,af_info->x1,af_info->y1);
			af_delay = 0;
			af_info->valid_step_cnt = 0;
			memset(af_info->af_fine_data,0,FOCUS_GRIDS*sizeof(isp_af_fine_tune_t));
			if(af_info->cur_step < (af_alg->step[af_alg->valid_step_cnt - 1] + af_alg->step[0])/2){
				af_info->cur_index = 0;
				sm_state.af_state = AF_GET_COARSE_INFO_L;
			}
			else{
				af_info->cur_index = af_alg->valid_step_cnt - 1;
				sm_state.af_state = AF_GET_COARSE_INFO_H;
			}
			break;
		case AF_GET_COARSE_INFO_H://from H step --> L step
			/*return to max step*/
			if((atomic_read(&af_info->writeable) <= 0)&&(af_info->cur_index == (af_alg->valid_step_cnt - 1))&&(af_info->cur_step != af_alg->step[af_alg->valid_step_cnt - 1])){
				if(af_info->cur_step < af_alg->step[af_alg->valid_step_cnt - 1]){
					if((af_alg->step[af_alg->valid_step_cnt - 1] - af_info->cur_step) > af_alg->jump_offset)
						af_info->cur_step = af_info->cur_step + af_alg->jump_offset;
					else
						af_info->cur_step = af_alg->step[af_info->cur_index];
				}
				else
					af_info->cur_step = af_alg->step[af_info->cur_index];
				atomic_set(&af_info->writeable,1);
				af_delay = 0;
				break;
			}
			/*get isp af info*/
			if((atomic_read(&af_info->writeable) <= 0)&&(af_delay >= af_alg->field_delay)){
				af_info->valid_step_cnt++;
				af_info->af_fine_data[af_info->valid_step_cnt - 1].cur_step = af_info->cur_step;
				memcpy(&af_info->af_fine_data[af_info->valid_step_cnt - 1].af_data,&af_info->isr_af_data,sizeof(isp_blnr_stat_t));
				if((af_info->cur_index == 0)||(check_hillside(af_info,af_alg) == 1)){
				        sm_state.af_state = AF_GET_FINE_INFO;
					af_info->great_step = isp_af_get_fine_step(af_info,af_alg);
				}else{
					af_info->cur_index--;
					af_info->cur_step = af_alg->step[af_info->cur_index];
				        atomic_set(&af_info->writeable,1);
				        af_delay = 0;
				}
			}
			break;
		case AF_GET_COARSE_INFO_L://from L step --> H step
			/*return to min step*/
			if((atomic_read(&af_info->writeable) <= 0)&&(af_info->cur_index == 0)&&(af_info->cur_step != af_alg->step[0])){
				if(af_info->cur_step > af_alg->step[0]){
					if((af_info->cur_step - af_alg->step[0]) > af_alg->jump_offset)
						af_info->cur_step = af_info->cur_step - af_alg->jump_offset;
					else
						af_info->cur_step = af_alg->step[af_info->cur_index];
				}
				else
					af_info->cur_step = af_alg->step[af_info->cur_index];
				atomic_set(&af_info->writeable,1);
				af_delay = 0;
				break;
			}
			/*get isp af info*/
			if((atomic_read(&af_info->writeable) <= 0)&&(af_delay >= af_alg->field_delay)){
				af_info->valid_step_cnt++;
				af_info->af_fine_data[af_info->valid_step_cnt - 1].cur_step = af_info->cur_step;
				memcpy(&af_info->af_fine_data[af_info->valid_step_cnt - 1].af_data,&af_info->isr_af_data,sizeof(isp_blnr_stat_t));
				if((++af_info->cur_index >= af_alg->valid_step_cnt)||(check_hillside(af_info,af_alg) == 1)){
				        sm_state.af_state = AF_GET_FINE_INFO;
					af_info->great_step = isp_af_get_fine_step(af_info,af_alg);
				}else{
					af_info->cur_step = af_alg->step[af_info->cur_index];
				        atomic_set(&af_info->writeable,1);
				        af_delay = 0;
				}
			}
			break;
		case AF_CALC_GREAT:
			if(atomic_read(&af_info->writeable) <= 0){
				af_info->great_step = isp_af_get_fine_step(af_info,af_alg);
				if((af_info->cur_step - af_alg->jump_offset) > af_info->great_step){
					af_info->cur_step = af_info->cur_step - af_alg->jump_offset;
					sm_state.af_state = AF_GET_FINE_INFO;
				}
				else{
					af_info->cur_step = af_info->great_step;
					sm_state.af_state = AF_SUCCESS;
				}
				atomic_set(&af_info->writeable,1);
				af_delay = 0;
			}
			break;
		case AF_GET_FINE_INFO:
			if(atomic_read(&af_info->writeable) <= 0){
				if(af_sm_dg&AF_FINE_TUNE)
					pr_info("[af_sm..]:af_info->cur_step:%d,af_alg->jump_offset:%d,af_info->great_step:%d.\n",
					af_info->cur_step,af_alg->jump_offset,af_info->great_step);
				if((af_info->cur_step - af_alg->jump_offset) > af_info->great_step){
					af_info->cur_step = af_info->cur_step - af_alg->jump_offset;
				}
				else{
					af_info->cur_step = af_info->great_step;
					sm_state.af_state = AF_SUCCESS;
				}
				atomic_set(&af_info->writeable,1);
				af_delay = 0;
			}
			break;
		case AF_SUCCESS:
			if((atomic_read(&af_info->writeable) <= 0)&&(af_delay >= af_alg->field_delay*2)){
				/*get last blnr*/
				memcpy(&af_info->last_blnr,&af_info->isr_af_data,sizeof(isp_blnr_stat_t));
			        if(af_sm_dg&AF_FINE_TUNE){
				        pr_info("[af] last blnr:ac0=%u ac1=%u ac2=%u ac3=%u dc0=%u dc1=%u dc2=%u dc3=%u af0_ac=%u af1_ac=%u.\n",
				                af_info->last_blnr.ac[0],af_info->last_blnr.ac[1],
				                af_info->last_blnr.ac[2],af_info->last_blnr.ac[3],
					        af_info->last_blnr.dc[0],af_info->last_blnr.dc[1],
					        af_info->last_blnr.dc[2],af_info->last_blnr.dc[3],
					        af_info->last_blnr.af_ac[0],af_info->last_blnr.af_ac[1]);
				}
				af_info->last_move = false;
				af_delay = 0;
				if(devp->flag & ISP_FLAG_TOUCH_AF)
					devp->flag &= (~ISP_FLAG_TOUCH_AF);
				if(devp->flag & ISP_FLAG_AF)
					sm_state.af_state = AF_DETECT_INIT;
				else
					sm_state.af_state = AF_NULL;
				devp->cmd_state = CAM_STATE_SUCCESS;
				isp_set_blenr_stat(af_info->x0,af_info->y0,af_info->x1,af_info->y1);
				isp_set_af_scan_stat(af_info->x0,af_info->y0,af_info->x1,af_info->y1);
			}
			break;
		default:
			break;
	}
}
void isp_af_save_current_para(isp_dev_t *devp)
{
	struct isp_af_info_s *af_info = &devp->af_info;
	struct xml_algorithm_af_s *af_alg = devp->isp_af_parm;
	af_info->last_move = false;
	sm_state.af_state = AF_NULL;
	if(sm_state.af_state == AF_SUCCESS)
		af_info->capture_step = af_info->cur_step;
	else
		af_info->capture_step = af_alg->step[0];
	pr_info("[isp]%s:save step:%d\n",__func__,af_info->capture_step);
}
#define FLASH_OFF         0
#define FLASH_ON	  1
#define FLASH_TORCH       2
static void isp_set_flash(isp_dev_t *devp,unsigned flash_mode,unsigned level)
{
	if(!flash_mode)
	torch_level(devp->flash.mode_pol_inv,devp->flash.led1_pol_inv,devp->flash.pin_mux_inv,devp->flash.torch_pol_inv,devp->wave,0);
	else if(flash_mode == FLASH_ON)
		flash_on(devp->flash.mode_pol_inv,devp->flash.led1_pol_inv,devp->flash.pin_mux_inv,devp->wave);
	else if(flash_mode == FLASH_TORCH)
		torch_level(devp->flash.mode_pol_inv,devp->flash.led1_pol_inv,devp->flash.pin_mux_inv,devp->flash.torch_pol_inv,devp->wave,level);
}

void isp_set_flash_mode(isp_dev_t *devp)
{
	if(!devp->flash.valid)
		/*no flash*/
		sm_state.cap_sm.flash_mode = FLASH_MODE_NULL;
	else
		sm_state.cap_sm.flash_mode = devp->cam_param->flash_mode;
}
void capture_sm_init(isp_dev_t *devp)
{
	struct isp_capture_sm_s *cap_sm = &sm_state.cap_sm;
	xml_capture_t *parm = devp->capture_parm;

	devp->capture_parm->ae_try_max_cnt = 3;
	devp->capture_parm->sigle_count = 0;
	devp->capture_parm->skip_step = 0;
	devp->capture_parm->multi_capture_num = 0;
	devp->capture_parm->af_mode = CAM_SCANMODE_FULL;
	devp->capture_parm->eyetime = 0;
	devp->capture_parm->pretime = 0;
	devp->capture_parm->postime = 0;
	cap_sm->adj_cnt = 0;
	cap_sm->flash_mode = FLASH_MODE_NULL;
	cap_sm->fr_time = 0;
	cap_sm->tr_time = 0;

	if(cap_sm->flash_mode) {
		cap_sm->capture_state = CAPTURE_INIT;
	} else {
		cap_sm->capture_state = CAPTURE_TUNE_3A;
		if(parm->af_mode){
			devp->flag |= ISP_FLAG_AF;
		}else{
			devp->flag &= (~ISP_FLAG_AF);
		}
		devp->flag &= (~ISP_FLAG_AWB);
		devp->flag &= (~ISP_FLAG_AE);
	}
	isp_ae_cal_new_para(devp);     // cal and set new ae value
	isp_awb_load_pre_para(devp);  // cal and set new awb value

	af_sm_init(devp);
}
int isp_capture_sm(isp_dev_t *devp)
{
	static unsigned int start_jf,multi_count=0;
	unsigned int cur_ac=0,j=0;
	xml_capture_t *parm = devp->capture_parm;
	struct isp_capture_sm_s *cap_sm = &sm_state.cap_sm;
	enum tvin_buffer_ctl_e ret = TVIN_BUF_SKIP;

	cap_sm->adj_cnt++;

	switch(cap_sm->capture_state){
		case CAPTURE_INIT:
			isp_set_blenr_stat(0,0,devp->info.h_active-1,devp->info.v_active-1);
			if(parm->pretime){
				cap_sm->capture_state = CAPTURE_PRE_WAIT;
				start_jf = jiffies;
				if(capture_debug)
					pr_info("[cap_sm]%u:init->pre_wait.\n",__LINE__);
			} else {
			        cap_sm->capture_state = CAPTURE_FLASH_ON;
				if(capture_debug)
					pr_info("[cap_sm]%u:init->flash_on.\n",__LINE__);
			}
			break;
		case CAPTURE_PRE_WAIT:
			if(time_after((unsigned long)jiffies,(unsigned long)(start_jf+parm->pretime))){
				cap_sm->capture_state = CAPTURE_FLASH_ON;
				start_jf = 0;
				if(capture_debug)
					pr_info("[cap_sm]%u:pre_wait->flash_on.\n",__LINE__);
			}
			break;
		case CAPTURE_FLASH_ON:
			if((sm_state.flash==ISP_FLASH_STATUS_NULL)&&(cap_sm->flash_mode==FLASH_MODE_AUTO)){
				return ret;
			}
			if(((cap_sm->flash_mode == FLASH_MODE_AUTO)&&(sm_state.flash == ISP_FLASH_STATUS_ON))||
				(cap_sm->flash_mode == FLASH_MODE_ON))
			{
				start_jf = jiffies;
				isp_set_flash(devp,FLASH_TORCH,100);
				cap_sm->adj_cnt = 0;
				cap_sm->flash_on = 1;
				cap_sm->capture_state = CAPTURE_TR_WAIT;
				if(capture_debug)
					pr_info("[cap_sm]%u:flash on->torch rising wait.\n",__LINE__);
			} else {
				/*without flash*/
				cap_sm->flash_on = 0;
				cap_sm->capture_state = CAPTURE_TUNE_3A;
			        if(capture_debug)
				        pr_info("[cap_sm]%u:flash on->tune 3a wait.\n",__LINE__);
			}
			break;
		case CAPTURE_TR_WAIT:
			if(time_after((unsigned long)jiffies,(unsigned long)(cap_sm->tr_time))){
				cap_sm->capture_state = CAPTURE_TUNE_3A;
				if(capture_debug)
				        pr_info("[cap_sm]%u:torch rising wait->tune 3a wait.\n",__LINE__);
			}
			break;
		case CAPTURE_TUNE_3A:
			if(cap_sm->adj_cnt >= parm->ae_try_max_cnt)
			{
				cap_sm->adj_cnt = 0;
				if(cap_sm->flash_on) {
					devp->flag |= ISP_FLAG_AE;
					cap_sm->capture_state = CAPTURE_LOW_GAIN;
					if(capture_debug)
						pr_info("[cap_sm]%u:3a->low gain.\n",__LINE__);
				} else {
					cap_sm->capture_state = CAPTURE_SINGLE;
					if(capture_debug)
						pr_info("[cap_sm]%u:3a(%s)->sigle.\n",__LINE__,
							cap_sm->adj_cnt>=parm->ae_try_max_cnt?"timeout":"stable");
				}
			}
			break;
		case CAPTURE_LOW_GAIN:
			if(sm_state.ae_down==true){
				devp->flag &=(~ISP_FLAG_AE);
				cap_sm->capture_state = CAPTURE_EYE_WAIT;
				if(capture_debug)
					pr_info("[cap_sm]%u:low gain->eye wait.\n",__LINE__);
			}
			break;
		case CAPTURE_EYE_WAIT:
			if(time_after((unsigned long)jiffies,(unsigned long)(start_jf+parm->eyetime))){
				isp_set_flash(devp,FLASH_TORCH,0);
				start_jf = 0;
				cap_sm->capture_state = CAPTURE_POS_WAIT;
				if(capture_debug)
				        pr_info("[cap_sm]%u:eye wait->post wait:%u.\n",__LINE__,start_jf);
			}
			break;
		case CAPTURE_POS_WAIT:
			if(time_after((unsigned long)jiffies,(unsigned long)(start_jf+parm->postime))){
				cap_sm->capture_state = CAPTURE_FLASHW;
				isp_set_flash(devp,FLASH_ON,0);
				start_jf = 0;
				if(capture_debug)
				        pr_info("[cap_sm]%u:changed post waite->flash wait.\n",__LINE__);
			}
			break;
		case CAPTURE_FLASHW:
			if(time_after((unsigned long)jiffies,(unsigned long)(start_jf+cap_sm->fr_time))){
				isp_set_flash(devp,FLASH_TORCH,0);
				ret = TVIN_BUF_NULL;
				if(capture_debug)
					pr_info("[cap_sm]%u:flash wait end,report buffer.\n",__LINE__);
			}
			break;
		case CAPTURE_SINGLE:
			if(parm->sigle_count <= 1){
				ret = TVIN_BUF_NULL;
				cap_sm->capture_state = CAPTURE_NULL;
				return ret;
			}
			if(cap_sm->adj_cnt <= parm->sigle_count){
				for(j=0;j<4;j++)
					cur_ac += devp->blnr_stat.ac[j];
				if(capture_debug)
					pr_info("[cap_sm]%u:field[%u] ac_sum %u.\n",__LINE__,cap_sm->adj_cnt,cur_ac);
				if(cur_ac > cap_sm->max_ac_sum){
					cap_sm->max_ac_sum = cur_ac;
					ret = TVIN_BUF_TMP;
				}
			}else{
				ret = TVIN_BUF_RECYCLE_TMP;
				if(parm->multi_capture_num > 0){
					cap_sm->capture_state = CAPTURE_MULTI;
					cap_sm->adj_cnt = 1;
					if(capture_debug)
						pr_info("[cap_sm]%u:single->multi.\n",__LINE__);
				}else{
					cap_sm->capture_state = CAPTURE_END;
					if(capture_debug)
						pr_info("[cap_sm]%u:single->capture end.\n",__LINE__);
				}
			}

			break;
		case CAPTURE_MULTI:
			if(cap_sm->adj_cnt % parm->skip_step == 0) {
				ret = TVIN_BUF_NULL;
				if(multi_count++ > parm->multi_capture_num){
					cap_sm->capture_state = CAPTURE_END;
                                        if(capture_debug)
                                                pr_info("[cap_sm]%u:muti capture end.\n",__LINE__);
				}
			} else {
				ret = TVIN_BUF_SKIP;
			}
			break;
		case CAPTURE_END:
			ret = TVIN_BUF_RECYCLE_TMP;
			devp->flag &= (~ISP_FLAG_CAPTURE);
			break;
		default:
			break;
		}

	return ret;
}

#define MAX_ABC(a,b,c)  (max(max(a,b),c))

void isp_sm_uninit(isp_dev_t *devp)
{
    isp_ae_save_current_para(devp);
    isp_awb_save_current_para(devp);
    isp_af_save_current_para(devp);
}

module_param(af_sm_dg,uint,0664);
MODULE_PARM_DESC(af_sm_dg,"\n debug flag for auto focus.\n");

module_param(capture_debug,uint,0664);
MODULE_PARM_DESC(capture_debug,"\n debug flag for isp capture function.\n");

