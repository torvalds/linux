/*
 * AMLOGIC DVB frontend driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/fcntl.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
//#include <linux/videodev2.h>
//s#include <linux/pinctrl/consumer.h>
//#include <linux/amlogic/aml_gpio_consumer.h>

#include "aml_fe.h"

#define pr_dbg(fmt, args...)\
	do{\
		if(debug_fe)\
			printk("FE: " fmt, ## args);\
	}while(0)
#define pr_error(fmt, args...) printk("FE: " fmt, ## args)

MODULE_PARM_DESC(debug_fe, "\n\t\t Enable frontend debug information");
static int debug_fe = 0;
module_param(debug_fe, int, 0644);

#define AFC_BEST_LOCK      50
#define ATV_AFC_1_0MHZ   1000000
#define ATV_AFC_2_0MHZ	 2000000

#define AML_FE_MAX_RES		50

static int slow_mode=0;
module_param(slow_mode,int,0644);
MODULE_DESCRIPTION("search the channel by slow_mode,by add +1MHz\n");

static int tuner_status_cnt=4;
module_param(tuner_status_cnt,int,0644);
MODULE_DESCRIPTION("after write a freq, max cnt value of read tuner status\n");

static int delay_cnt=10;//ms
module_param(delay_cnt,int,0644);
MODULE_DESCRIPTION("delay_cnt value of read cvd format\n");

static struct aml_fe_drv *tuner_drv_list = NULL;
static struct aml_fe_drv *atv_demod_drv_list = NULL;
static struct aml_fe_drv *dtv_demod_drv_list = NULL;
static struct aml_fe_man  fe_man;

static u32 aml_fe_suspended = 0;

static DEFINE_SPINLOCK(lock);
static int aml_fe_afc_closer(struct dvb_frontend *fe,int minafcfreq,int maxafcfqreq);

typedef int (*hook_func_t)(void);
hook_func_t aml_fe_hook_atv_status = NULL;
hook_func_t aml_fe_hook_hv_lock = NULL;
void aml_fe_hook_cvd(hook_func_t atv_mode,hook_func_t cvd_hv_lock)
{
	aml_fe_hook_atv_status = atv_mode;
	aml_fe_hook_hv_lock = cvd_hv_lock;
	printk("[aml_fe]%s \n",__func__);
}
EXPORT_SYMBOL(aml_fe_hook_cvd);


static struct aml_fe_drv** aml_get_fe_drv_list(aml_fe_dev_type_t type)
{
	switch(type){
		case AM_DEV_TUNER:
			return &tuner_drv_list;
		case AM_DEV_ATV_DEMOD:
			return &atv_demod_drv_list;
		case AM_DEV_DTV_DEMOD:
			return &dtv_demod_drv_list;
		default:
			return NULL;
	}
}

int aml_register_fe_drv(aml_fe_dev_type_t type, struct aml_fe_drv *drv)
{
	if(drv){
		struct aml_fe_drv **list = aml_get_fe_drv_list(type);
		unsigned long flags;

		spin_lock_irqsave(&lock, flags);

		drv->next = *list;
		*list = drv;

		drv->ref = 0;

		spin_unlock_irqrestore(&lock, flags);
	}

	return 0;
}
EXPORT_SYMBOL(aml_register_fe_drv);

int aml_unregister_fe_drv(aml_fe_dev_type_t type, struct aml_fe_drv *drv)
{
	int ret = 0;

	if(drv){
		struct aml_fe_drv *pdrv, *pprev;
		struct aml_fe_drv **list = aml_get_fe_drv_list(type);
		unsigned long flags;

		spin_lock_irqsave(&lock, flags);

		if(!drv->ref){
			for(pprev = NULL, pdrv = *list;
				pdrv;
				pprev = pdrv, pdrv = pdrv->next){
				if(pdrv == drv){
					if(pprev)
						pprev->next = pdrv->next;
					else
						*list = pdrv->next;
					break;
				}
			}
		}else{
			pr_error("fe driver %d is inused\n", drv->id);
			ret = -1;
		}

		spin_unlock_irqrestore(&lock, flags);
	}

	return ret;
}
EXPORT_SYMBOL(aml_unregister_fe_drv);


int aml_fe_analog_set_frontend(struct dvb_frontend* fe)
{
	struct aml_fe *afe = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct analog_parameters p;
	int ret = -1;

	p.frequency  = c->frequency;
	p.soundsys   = c->analog.soundsys;
	p.audmode    = c->analog.audmode;
	p.std        = c->analog.std;
	p.reserved   = c->analog.reserved;

	/*set tuner&ademod such as philipse tuner*/
	if(fe->ops.analog_ops.set_params){
		fe->ops.analog_ops.set_params(fe, &p);
		ret = 0;
	}
	if(fe->ops.tuner_ops.set_params){
		ret = fe->ops.tuner_ops.set_params(fe);
	}

	if(ret == 0){
		afe->params.frequency = c->frequency;
		afe->params.inversion = c->inversion;
		afe->params.analog  = c->analog;
	}

	return ret;
}
EXPORT_SYMBOL(aml_fe_analog_set_frontend);

static int aml_fe_analog_get_frontend(struct dvb_frontend* fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct aml_fe *afe = fe->demodulator_priv;

	p->frequency = afe->params.frequency;
	pr_dbg("[%s] params.frequency:%d\n",__func__,p->frequency);

	return 0;
}

static int aml_fe_analog_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
    int ret = 0;
    if(!status)
        return -1;
    /*atv only demod locked is vaild*/
    if(fe->ops.analog_ops.get_status)
	    fe->ops.analog_ops.get_status(fe, status);
    else if(fe->ops.tuner_ops.get_status)
        ret = fe->ops.tuner_ops.get_status(fe, status);

    return ret;
}

static int aml_fe_analog_read_signal_strength(struct dvb_frontend* fe, u16 *strength)
{
	int ret = -1;
	u16 s;
	s=0;
	if(fe->ops.analog_ops.has_signal){

		fe->ops.analog_ops.has_signal(fe, &s);
		*strength = s;
		ret = 0;
	}else if(fe->ops.tuner_ops.get_rf_strength){
		ret = fe->ops.tuner_ops.get_rf_strength(fe, strength);
	}

	return ret;
}
static int aml_fe_analog_read_signal_snr(struct dvb_frontend* fe, u16 *snr)
{
    if(!snr)
    {
        pr_error("[aml_fe..]%s null pointer error.\n",__func__);
        return -1;
    }
    if(fe->ops.analog_ops.get_snr)
        *snr = (unsigned short)fe->ops.analog_ops.get_snr(fe);
    return 0;
}
static enum dvbfe_algo aml_fe_get_analog_algo(struct dvb_frontend *dev)
{
        return DVBFE_ALGO_CUSTOM;
}
//this func set two ways to search the channel
//1.if the afc_range>1Mhz,set the freq  more than once
//2. if the afc_range<=1MHz,set the freq only once ,on the mid freq
static enum dvbfe_search aml_fe_analog_search(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	fe_status_t tuner_state = FE_TIMEDOUT;
	fe_status_t ade_state = FE_TIMEDOUT;
	__u32  set_freq=0;
	__u32 minafcfreq, maxafcfreq;
	__u32 frist_step;
	__u32 afc_step;
	int tuner_status_cnt_local = tuner_status_cnt;
	int atv_cvd_format,hv_lock_status,snr_vale;
	v4l2_std_id std_bk = 0;
	struct aml_fe *fee;
	fee = fe->demodulator_priv;
	pr_dbg("[%s] is working,afc_range=%d,flag=0x%x[1->auto,11->mannul],the received freq=[%d]\n",\
			__func__,p->analog.afc_range,p->analog.flag,p->frequency);
	pr_dbg("the tuner type is [%d]\n",fee->tuner->drv->id);
	//backup the freq by api
	set_freq=p->frequency;
	if(p->analog.flag == ANALOG_FLAG_MANUL_SCAN){/*manul search force to ntsc_m*/
		std_bk = p->analog.std;
		p->analog.std = (p->analog.std&(~(V4L2_STD_B|V4L2_STD_GH)))|V4L2_STD_NTSC_M;
		if( fe->ops.set_frontend(fe)){
			printk("[%s]the func of set_param err.\n",__func__);
			p->analog.std = std_bk;
			fe->ops.set_frontend(fe);
			std_bk = 0;
			return DVBFE_ALGO_SEARCH_FAILED;
		}
		fe->ops.tuner_ops.get_status(fe, &tuner_state);
		fe->ops.analog_ops.get_status(fe, &ade_state);
		if(FE_HAS_LOCK==ade_state && FE_HAS_LOCK==tuner_state){
			if(aml_fe_afc_closer(fe,p->frequency - ATV_AFC_1_0MHZ ,p->frequency + ATV_AFC_1_0MHZ)==0){
				printk("[%s] manul scan mode:p->frequency=[%d] has lock,search success.\n",__func__,p->frequency);
				p->analog.std = std_bk;
				fe->ops.set_frontend(fe);
				std_bk = 0;
				return DVBFE_ALGO_SEARCH_SUCCESS;
			}
			else{
				p->analog.std = std_bk;
				fe->ops.set_frontend(fe);
				std_bk = 0;
				return DVBFE_ALGO_SEARCH_FAILED;
			}
		}
		else
		    return DVBFE_ALGO_SEARCH_FAILED;
	}
	if(p->analog.afc_range==0)
	{
		pr_dbg("[%s]:afc_range==0,skip the search\n",__func__);
		return DVBFE_ALGO_SEARCH_FAILED;
	}

//set the frist_step
	if(p->analog.afc_range>ATV_AFC_1_0MHZ)
		frist_step=ATV_AFC_1_0MHZ;
	else
		frist_step=p->analog.afc_range;
//set the afc_range and start freq
	minafcfreq  = p->frequency  - p->analog.afc_range;
	maxafcfreq = p->frequency + p->analog.afc_range;
//from the min freq start,and set the afc_step
	if(slow_mode || fee->tuner->drv->id ==	AM_TUNER_FQ1216 || AM_TUNER_HTM == fee->tuner->drv->id ){
		pr_dbg("[%s]this is slow mode to search the channel\n",__func__);
		p->frequency = minafcfreq;
		afc_step=ATV_AFC_1_0MHZ;
	}
	else if(!slow_mode && (fee->tuner->drv->id== AM_TUNER_SI2176)){
		p->frequency = minafcfreq+frist_step;
		afc_step=ATV_AFC_2_0MHZ;
	}
	else{
		pr_dbg("[%s]this is ukown tuner type and on slow_mode to search the channel\n",__func__);
		p->frequency = minafcfreq;
		afc_step=ATV_AFC_1_0MHZ;
	}
	if( fe->ops.set_frontend(fe)){
		printk("[%s]the func of set_param err.\n",__func__);
		return DVBFE_ALGO_SEARCH_FAILED;
	}
//atuo bettween afc range
	if(likely(fe->ops.tuner_ops.get_status && fe->ops.analog_ops.get_status && fe->ops.set_frontend))
	{
		 while( p->frequency<=maxafcfreq)
		{
			pr_dbg("[%s] p->frequency=[%d] is processing\n",__func__,p->frequency);
			do{
				if((fe->ops.tuner_ops.get_pll_status == NULL)||(fe->ops.tuner_ops.get_pll_status == NULL)){
					printk("[%s]error:the func of get_pll_status is NULL.\n",__func__);
					return DVBFE_ALGO_SEARCH_FAILED;
				}
				fe->ops.tuner_ops.get_pll_status(fe, &tuner_state);
				fe->ops.analog_ops.get_pll_status(fe, &ade_state);
				tuner_status_cnt_local--;
				if(FE_HAS_LOCK==ade_state || FE_HAS_LOCK==tuner_state || tuner_status_cnt_local == 0)
					break;
			}while(1);
			tuner_status_cnt_local = tuner_status_cnt;
			if(FE_HAS_LOCK==ade_state || FE_HAS_LOCK==tuner_state){
				pr_dbg("[%s] pll lock success \n",__func__);
				do{
					tuner_status_cnt_local--;
					atv_cvd_format = aml_fe_hook_atv_status();//tvafe_cvd2_get_atv_format();
					hv_lock_status = aml_fe_hook_hv_lock();//tvafe_cvd2_get_hv_lock();
					snr_vale = fe->ops.analog_ops.get_snr(fe);
					pr_dbg("[%s] atv_cvd_format:0x%x;hv_lock_status:0x%x;snr_vale:%d \n",
						__func__,atv_cvd_format,hv_lock_status,snr_vale);
					if(((atv_cvd_format & 0x4)==0)||((hv_lock_status==0x6)&&(snr_vale < 10))){
						std_bk = p->analog.std;
						p->analog.std = (p->analog.std&(~(V4L2_STD_B|V4L2_STD_GH)))|V4L2_STD_NTSC_M;
						p->frequency +=1;/*avoid std unenable*/
						pr_dbg("[%s] maybe ntsc m \n",__func__);
						break;
					}
					if(tuner_status_cnt_local == 0)
						break;
					mdelay(delay_cnt);
				}while(1);
			}
			tuner_status_cnt_local = tuner_status_cnt;
			if( fe->ops.set_frontend(fe)){
				printk("[%s] the func of set_frontend err.\n",__func__);
				return	DVBFE_ALGO_SEARCH_FAILED;
			}
			do{
			fe->ops.tuner_ops.get_status(fe, &tuner_state);
			fe->ops.analog_ops.get_status(fe, &ade_state);
			tuner_status_cnt_local--;
			if(FE_HAS_LOCK==ade_state || FE_HAS_LOCK==tuner_state || tuner_status_cnt_local == 0)
				break;
			}while(1);
			tuner_status_cnt_local = tuner_status_cnt;
			if(FE_HAS_LOCK==ade_state || FE_HAS_LOCK==tuner_state){
				if(aml_fe_afc_closer(fe,minafcfreq,maxafcfreq)==0){
					printk("[%s] afc end  :p->frequency=[%d] has lock,search success.\n",__func__,p->frequency);
					if(std_bk != 0){/*avoid sound format is not match after search over*/
						p->analog.std = std_bk;
						p->frequency -=1;/*avoid std unenable*/
						fe->ops.set_frontend(fe);
						std_bk = 0;
					}
					return DVBFE_ALGO_SEARCH_SUCCESS;
				}
			}
			if(std_bk != 0){/*avoid sound format is not match after search over*/
				p->analog.std = std_bk;
				fe->ops.set_frontend(fe);
				std_bk = 0;
			}
			pr_dbg("[%s] freq is[%d] unlock\n",__func__,p->frequency);
			p->frequency +=  afc_step;
			if(p->frequency >maxafcfreq)
			{
				pr_dbg("[%s] p->frequency=[%d] over maxafcfreq=[%d].search failed.\n",__func__,p->frequency,maxafcfreq);
				//back	original freq  to api
				p->frequency =	set_freq;
				fe->ops.set_frontend(fe);
				return DVBFE_ALGO_SEARCH_FAILED;
			}
		}
	}

	   return DVBFE_ALGO_SEARCH_FAILED;
}
static int aml_fe_afc_closer(struct dvb_frontend *fe,int minafcfreq,int maxafcfqreq)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int afc = 100;
	__u32 set_freq;
	int count=10;

	//do the auto afc make sure the afc<50k or the range from api
	if(fe->ops.analog_ops.get_afc &&fe->ops.set_frontend){
		set_freq=c->frequency;

		while(afc > AFC_BEST_LOCK){

			fe->ops.analog_ops.get_afc(fe, &afc);
			c->frequency += afc*1000;

			if(unlikely(c->frequency>maxafcfqreq) ){
				 pr_dbg("[%s]:[%d] is exceed maxafcfqreq[%d]\n",__func__,c->frequency,maxafcfqreq);
				 c->frequency=set_freq;
				 return -1;
			}
			if( unlikely(c->frequency<minafcfreq)){
				 pr_dbg("[%s]:[%d ] is exceed minafcfreq[%d]\n",__func__,c->frequency,minafcfreq);
				 c->frequency=set_freq;
				 return -1;
			}
			if(likely(!(count--))){
				 pr_dbg("[%s]:exceed the afc count\n",__func__);
				 c->frequency=set_freq;
				 return -1;
			}

			fe->ops.set_frontend(fe);

			pr_dbg("[aml_fe..]%s get afc %d khz, freq %u.\n",__func__,afc, c->frequency);
		}

		pr_dbg("[aml_fe..]%s get afc %d khz done, freq %u.\n",__func__,afc, c->frequency);
	}

    return 0;
}



static int aml_fe_set_mode(struct dvb_frontend *dev, fe_type_t type)
{
	struct aml_fe *fe;
	aml_fe_mode_t mode;
	unsigned long flags;
	fe = dev->demodulator_priv;
	//type=FE_ATSC;
	switch(type){
		case FE_QPSK:
			mode = AM_FE_QPSK;
			pr_dbg("set mode -> QPSK\n");
			break;
		case FE_QAM:
			pr_dbg("set mode -> QAM\n");
			mode = AM_FE_QAM;
			break;
		case FE_OFDM:
			pr_dbg("set mode -> OFDM\n");
			mode = AM_FE_OFDM;
			break;
		case FE_ATSC:
			pr_dbg("set mode -> ATSC\n");
			mode = AM_FE_ATSC;
			break;
		case FE_ISDBT:
			pr_dbg("set mode -> ISDBT\n");
			mode = AM_FE_ISDBT;
			break;
		case FE_DTMB:
			pr_dbg("set mode -> DTMB\n");
			mode = AM_FE_DTMB;
			break;
		case FE_ANALOG:
			pr_dbg("set mode -> ANALOG\n");
			mode = AM_FE_ANALOG;
			break;
		default:
			pr_error("illegal fe type %d\n", type);
			return -1;
	}

	if(fe->mode == mode)
	{
		pr_dbg("[%s]:the mode is not change!!!!\n",__func__);
		return 0;
	}

	if(fe->mode != AM_FE_UNKNOWN){
		pr_dbg("leave mode %d\n", fe->mode);

		if(fe->dtv_demod && (fe->dtv_demod->drv->capability & fe->mode) && fe->dtv_demod->drv->leave_mode)
				fe->dtv_demod->drv->leave_mode(fe, fe->mode);
		if(fe->atv_demod && (fe->atv_demod->drv->capability & fe->mode) && fe->atv_demod->drv->leave_mode)
				fe->atv_demod->drv->leave_mode(fe, fe->mode);
		if(fe->tuner && (fe->tuner->drv->capability & fe->mode) && fe->tuner->drv->leave_mode)
				fe->tuner->drv->leave_mode(fe, fe->mode);

		if(fe->mode & AM_FE_DTV_MASK)
			aml_dmx_register_frontend(fe->ts, NULL);

		fe->mode = AM_FE_UNKNOWN;
	}

	if(!(mode & fe->capability)){
		int i;

		spin_lock_irqsave(&lock, flags);
		for(i = 0; i < FE_DEV_COUNT; i++){
			if((mode & fe_man.fe[i].capability) && (fe_man.fe[i].dev_id == fe->dev_id))
				break;
		}
		spin_unlock_irqrestore(&lock, flags);

		if(i >= FE_DEV_COUNT){
			pr_error("frontend %p do not support mode %x, capability %x\n", fe, mode, fe->capability);
			return -1;
		}

		fe = &fe_man.fe[i];
		dev->demodulator_priv = fe;
	}

	if(fe->mode & AM_FE_DTV_MASK){
		aml_dmx_register_frontend(fe->ts, NULL);
		fe->mode = 0;
	}

	spin_lock_irqsave(&fe->slock, flags);

	memset(&fe->fe->ops.tuner_ops, 0, sizeof(fe->fe->ops.tuner_ops));
	memset(&fe->fe->ops.analog_ops, 0, sizeof(fe->fe->ops.analog_ops));
	memset(&fe->fe->ops.info,0,sizeof(fe->fe->ops.info));
	fe->fe->ops.release = NULL;
	fe->fe->ops.release_sec = NULL;
	fe->fe->ops.init = NULL;
	fe->fe->ops.sleep = NULL;
	fe->fe->ops.write = NULL;
	fe->fe->ops.tune = NULL;
	fe->fe->ops.get_frontend_algo = NULL;
	fe->fe->ops.set_frontend = NULL;
	fe->fe->ops.get_tune_settings = NULL;
	fe->fe->ops.get_frontend = NULL;
	fe->fe->ops.read_status = NULL;
	fe->fe->ops.read_ber = NULL;
	fe->fe->ops.read_signal_strength = NULL;
	fe->fe->ops.read_snr = NULL;
	fe->fe->ops.read_ucblocks = NULL;
	fe->fe->ops.set_qam_mode = NULL;
	fe->fe->ops.diseqc_reset_overload = NULL;
	fe->fe->ops.diseqc_send_master_cmd = NULL;
	fe->fe->ops.diseqc_recv_slave_reply = NULL;
	fe->fe->ops.diseqc_send_burst = NULL;
	fe->fe->ops.set_tone = NULL;
	fe->fe->ops.set_voltage = NULL;
	fe->fe->ops.enable_high_lnb_voltage = NULL;
	fe->fe->ops.dishnetwork_send_legacy_command = NULL;
	fe->fe->ops.i2c_gate_ctrl = NULL;
	fe->fe->ops.ts_bus_ctrl = NULL;
	fe->fe->ops.search = NULL;
	fe->fe->ops.track = NULL;
	fe->fe->ops.set_property = NULL;
	fe->fe->ops.get_property = NULL;
	memset(&fe->fe->ops.blindscan_ops, 0, sizeof(fe->fe->ops.blindscan_ops));
	fe->fe->ops.asyncinfo.set_frontend_asyncenable = 0;
	if(fe->tuner && fe->tuner->drv && (mode & fe->tuner->drv->capability) && fe->tuner->drv->get_ops){
		fe->tuner->drv->get_ops(fe->tuner, mode, &fe->fe->ops.tuner_ops);
	}

	if(fe->atv_demod && fe->atv_demod->drv && (mode & fe->atv_demod->drv->capability) && fe->atv_demod->drv->get_ops){
		fe->atv_demod->drv->get_ops(fe->atv_demod, mode, &fe->fe->ops.analog_ops);
		fe->fe->ops.set_frontend = aml_fe_analog_set_frontend;
		fe->fe->ops.get_frontend = aml_fe_analog_get_frontend;
		fe->fe->ops.read_status  = aml_fe_analog_read_status;
		fe->fe->ops.read_signal_strength = aml_fe_analog_read_signal_strength;
		fe->fe->ops.read_snr     = aml_fe_analog_read_signal_snr;
		fe->fe->ops.get_frontend_algo = aml_fe_get_analog_algo;
		fe->fe->ops.search       =  aml_fe_analog_search;
	}

	if(fe->dtv_demod && fe->dtv_demod->drv && (mode & fe->dtv_demod->drv->capability) && fe->dtv_demod->drv->get_ops){
		fe->dtv_demod->drv->get_ops(fe->dtv_demod, mode, &fe->fe->ops);
	}

	spin_unlock_irqrestore(&fe->slock, flags);

	pr_dbg("enter mode %d\n", mode);

	if(fe->dtv_demod && (fe->dtv_demod->drv->capability & mode) && fe->dtv_demod->drv->enter_mode)
		fe->dtv_demod->drv->enter_mode(fe, mode);
	if(fe->atv_demod && (fe->atv_demod->drv->capability & mode) && fe->atv_demod->drv->enter_mode)
		fe->atv_demod->drv->enter_mode(fe, mode);
	if(fe->tuner && (fe->tuner->drv->capability & mode) && fe->tuner->drv->enter_mode)
		fe->tuner->drv->enter_mode(fe, mode);

	pr_dbg("register demux frontend\n");
	if(mode & AM_FE_DTV_MASK){
		aml_dmx_register_frontend(fe->ts, fe->fe);
	}
	strcpy(fe->fe->ops.info.name, "amlogic dvb frontend");

	fe->fe->ops.info.type = type;
	fe->mode = mode;

	pr_dbg("set mode ok\n");

	return 0;
}

static int aml_fe_read_ts(struct dvb_frontend *dev, int *ts)
{
	struct aml_fe *fe;

	fe = dev->demodulator_priv;

	*ts = fe->ts;
	return 0;
}

#ifndef CONFIG_OF
struct resource *aml_fe_platform_get_resource_byname(const char *name)
{
	int i;

	for (i = 0; i < aml_fe_num_resources; i++) {
		struct resource *r = &aml_fe_resource[i];

		if (!strcmp(r->name, name))
			return r;
	}
	return NULL;
}
#endif /*CONFIG_OF*/

static int aml_fe_dev_init(struct aml_dvb *dvb, struct platform_device *pdev, aml_fe_dev_type_t type, struct aml_fe_dev *dev, int id)
{
#ifndef CONFIG_OF
	struct resource *res;
#endif
	char *name = NULL;
	char buf[32];
	int ret;
	u32 value;
	const char *str;

	switch(type){
		case AM_DEV_TUNER:
			name = "tuner";
			break;
		case AM_DEV_ATV_DEMOD:
			name = "atv_demod";
			break;
		case AM_DEV_DTV_DEMOD:
			name = "dtv_demod";
			break;
		default:
			break;
	}

	pr_dbg("init %s %d pdev: %p\n", name, id, pdev);

	snprintf(buf, sizeof(buf), "%s%d", name, id);
#ifdef CONFIG_OF
	ret = of_property_read_string(pdev->dev.of_node, buf, &str);
	if(ret){
		pr_dbg("cannot find resource \"%s\"\n", buf);
		return 0;
	}else{
		struct aml_fe_drv **list = aml_get_fe_drv_list(type);
		struct aml_fe_drv *drv;
		unsigned long flags;

		spin_lock_irqsave(&lock, flags);

		for(drv = *list; drv; drv = drv->next){
			if(!strcmp(drv->name, str)){
				break;
			}
		}

		if(dev->drv != drv){
			if(dev->drv){
				dev->drv->ref--;
				if(dev->drv->owner)
					module_put(dev->drv->owner);
			}
			if(drv){
				drv->ref++;
				if(drv->owner)
					try_module_get(drv->owner);
			}
			dev->drv = drv;
		}

		spin_unlock_irqrestore(&lock, flags);

		if(drv){
			pr_dbg("found %s%d driver: %s\n", name, id, str);
		}else{
			pr_err("cannot find %s%d driver: %s\n", name, id, str);
			return -1;
		}
	}

#else /*CONFIG_OF*/
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, buf);
	if(res){
		struct aml_fe_drv **list = aml_get_fe_drv_list(type);
		struct aml_fe_drv *drv;
		int type = res->start;
		unsigned long flags;

		spin_lock_irqsave(&lock, flags);

		for(drv = *list; drv; drv = drv->next){
			if(drv->id == type){
				drv->ref++;
				if(drv->owner)
					try_module_get(drv->owner);
				break;
			}
		}

		spin_unlock_irqrestore(&lock, flags);

		if(drv){
			dev->drv = drv;
		}else{
			pr_error("cannot find %s%d driver: %d\n", name, id, type);
			return -1;
		}
	}else{
		pr_dbg("cannot find resource \"%s\"\n", buf);
		return 0;
	}
#endif /*CONFIG_OF*/

	snprintf(buf, sizeof(buf), "%s%d_i2c_adap_id", name, id);
#ifdef CONFIG_OF
	ret = of_property_read_u32(pdev->dev.of_node, buf, &value);
	if(!ret){
		dev->i2c_adap_id = value;
		dev->i2c_adap = i2c_get_adapter(value);
		pr_dbg("%s: %d\n", buf, dev->i2c_adap_id);
	}else{
		dev->i2c_adap_id = -1;
		pr_dbg("cannot find resource \"%s\"\n", buf);
	}
#else /*CONFIG_OF*/
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, buf);
	if(res){
		int adap = res->start;

		dev->i2c_adap_id = adap;
		dev->i2c_adap = i2c_get_adapter(adap);
	}else{
		dev->i2c_adap_id = -1;
		pr_dbg("cannot find resource \"%s\"\n", buf);
	}
#endif /*CONFIG_OF*/

	snprintf(buf, sizeof(buf), "%s%d_i2c_addr", name, id);
#ifdef CONFIG_OF
	ret = of_property_read_u32(pdev->dev.of_node, buf, &value);
	if(!ret){
		dev->i2c_addr = value;
		pr_dbg("%s: %d\n", buf, dev->i2c_addr);
	}else{
		dev->i2c_addr = -1;
		pr_dbg("cannot find resource \"%s\"\n", buf);
	}
#else /*CONFIG_OF*/
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, buf);
	if(res){
		int addr = res->start;

		dev->i2c_addr = addr;
		pr_dbg("%s: %d\n", buf, dev->i2c_addr);
	}else{
		dev->i2c_addr = -1;
		pr_dbg("cannot find resource \"%s\"\n", buf);
	}
#endif

	snprintf(buf, sizeof(buf), "%s%d_reset_gpio", name, id);
#ifdef CONFIG_OF
	ret = of_property_read_string(pdev->dev.of_node, buf, &str);
	if(!ret){
		dev->reset_gpio = amlogic_gpio_name_map_num(str);
		pr_dbg("%s: %s\n", buf, str);
	}else{
		dev->reset_gpio = -1;
		pr_dbg("cannot find resource \"%s\"\n", buf);
	}
#else /*CONFIG_OF*/
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, buf);
	if(res){
		int gpio = res->start;

		dev->reset_gpio = gpio;
		pr_dbg("%s: %x\n", buf, gpio);
	}else{
		dev->reset_gpio = -1;
		pr_dbg("cannot find resource \"%s\"\n", buf);
	}
#endif /*CONFIG_OF*/

	snprintf(buf, sizeof(buf), "%s%d_reset_value", name, id);
#ifdef CONFIG_OF
	ret = of_property_read_u32(pdev->dev.of_node, buf, &value);
	if(!ret){
		dev->reset_value = value;
		pr_dbg("%s: %d\n", buf, dev->reset_value);
	}else{
		dev->reset_value = -1;
	}
#else /*CONFIG_OF*/
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, buf);
	if(res){
		int v = res->start;

		dev->reset_value = v;
		pr_dbg("%s: %d\n", buf, dev->reset_value);
	}else{
		dev->reset_value = 0;
		pr_dbg("cannot find resource \"%s\"\n", buf);
	}
#endif /*CONFIG_OF*/

	snprintf(buf, sizeof(buf), "%s%d_tunerpower", name, id);
#ifdef CONFIG_OF
	ret = of_property_read_string(pdev->dev.of_node, buf, &str);
	if(!ret){
		dev->tuner_power_gpio = amlogic_gpio_name_map_num(str);
		pr_dbg("%s: %s\n", buf, str);
	}else{
		dev->tuner_power_gpio = -1;
	}
#else /*CONFIG_OF*/
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, buf);
	if(res){
		int gpio = res->start;

		dev->tuner_power_gpio = gpio;
	}else{
		dev->tuner_power_gpio = -1;
	}
#endif /*CONFIG_OF*/

	snprintf(buf, sizeof(buf), "%s%d_lnbpower", name, id);
#ifdef CONFIG_OF
	ret = of_property_read_string(pdev->dev.of_node, buf, &str);
	if(!ret){
		dev->lnb_power_gpio = amlogic_gpio_name_map_num(str);
		pr_dbg("%s: %s\n", buf, str);
	}else{
		dev->lnb_power_gpio = -1;
	}
#else /*CONFIG_OF*/
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, buf);
	if(res){
		int gpio = res->start;

		dev->lnb_power_gpio = gpio;
	}else{
		dev->lnb_power_gpio = -1;
	}
#endif

	snprintf(buf, sizeof(buf), "%s%d_antoverload", name, id);
#ifdef CONFIG_OF
	ret = of_property_read_string(pdev->dev.of_node, buf, &str);
	if(!ret){
		dev->antoverload_gpio = amlogic_gpio_name_map_num(str);
		pr_dbg("%s: %s\n", buf, str);
	}else{
		dev->antoverload_gpio = -1;
	}
#else /*CONFIG_OF*/
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, buf);
	if(res){
		int gpio = res->start;

		dev->antoverload_gpio = gpio;
	}else{
		dev->antoverload_gpio = -1;
	}
#endif /*CONFIG_OF*/

#ifdef CONFIG_OF
	{
		long *mem_buf;
		int memstart;
		int memend;
		int memsize;
		snprintf(buf, sizeof(buf), "%s%d_mem", name, id);
		ret = of_property_read_u32(pdev->dev.of_node, buf, &value);
		if(!ret && value){
			ret = find_reserve_block(pdev->dev.of_node->name,0);
			if(ret < 0){
				pr_error("%s%d memory resource undefined.\n", name, id);
				dev->mem_start = 0;
				dev->mem_end = 0;
			}else{
				memstart = (phys_addr_t)get_reserve_block_addr(ret);
				memsize = (phys_addr_t)get_reserve_block_size(ret);
				memend = memstart+memsize-1;
				mem_buf=(long*)phys_to_virt(memstart);
				printk("memend is %x,memstart is %x,memsize is %x\n",memend,memstart,memsize);
				memset(mem_buf,0,memsize-1);
				dev->mem_start = memstart;
				dev->mem_end = memend;
			}
		}else{
			pr_error("%s%d memory resource undefined.\n", name, id);
			dev->mem_start = 0;
			dev->mem_end = 0;
		}
	}
#endif

	if(dev->drv->init){
		ret = dev->drv->init(dev);
		if(ret != 0){
			dev->drv = NULL;
            pr_error("[aml_fe..]%s error.\n",__func__);
			return ret;
		}
	}

	return 0;
}

static int aml_fe_dev_release(struct aml_dvb *dvb, aml_fe_dev_type_t type, struct aml_fe_dev *dev)
{
	if(dev->drv){
		if(dev->drv->owner)
			module_put(dev->drv->owner);
		dev->drv->ref--;
		if(dev->drv->release)
			dev->drv->release(dev);
	}

	dev->drv = NULL;
	return 0;
}

static void aml_fe_man_run(struct aml_dvb *dvb, struct aml_fe *fe)
{
	int tuner_cap = 0xFFFFFFFF;
	int demod_cap = 0;

	if(fe->init)
		return;

	if(fe->tuner && fe->tuner->drv){
		tuner_cap = fe->tuner->drv->capability;
		fe->init = 1;
	}

	if(fe->atv_demod && fe->atv_demod->drv){
		demod_cap |= fe->atv_demod->drv->capability;
		fe->init = 1;
	}

	if(fe->dtv_demod && fe->dtv_demod->drv){
		demod_cap |= fe->dtv_demod->drv->capability;
		fe->init = 1;
	}

	if(fe->init){
		int reg = 1;
		int ret;
		int id;

		spin_lock_init(&fe->slock);
		fe->mode = AM_FE_UNKNOWN;
		fe->capability = (tuner_cap & demod_cap);
		pr_dbg("fe: %p cap: %x tuner: %x demod: %x\n", fe, fe->capability, tuner_cap, demod_cap);

		for(id = 0; id < FE_DEV_COUNT; id++){
			struct aml_fe *prev_fe = &fe_man.fe[id];

			if(prev_fe == fe)
				continue;
			if(prev_fe->init && (prev_fe->dev_id == fe->dev_id)){
				reg = 0;
				break;
			}
		}
		fe->fe = &fe_man.dev[fe->dev_id];
		if(reg){
			fe->fe->demodulator_priv = fe;
			fe->fe->ops.set_mode = aml_fe_set_mode;
			fe->fe->ops.read_ts  = aml_fe_read_ts;

			ret = dvb_register_frontend(&dvb->dvb_adapter, fe->fe);
			if(ret){
				pr_error("register fe%d failed\n", fe->dev_id);
				return;
			}
		}

		if(fe->tuner)
			fe->tuner->fe = fe;
		if(fe->atv_demod)
			fe->atv_demod->fe = fe;
		if(fe->dtv_demod)
			fe->dtv_demod->fe = fe;
	}
}

static int aml_fe_man_init(struct aml_dvb *dvb, struct platform_device *pdev, struct aml_fe *fe, int id)
{
#ifndef CONFIG_OF
	struct resource *res;
#endif
	char buf[32];
	u32 value;
	int ret;


	snprintf(buf, sizeof(buf), "fe%d_tuner", id);

#ifdef CONFIG_OF
	ret = of_property_read_u32(pdev->dev.of_node, buf, &value);
	if(!ret){
		int id = value;
		if((id < 0) || (id >= FE_DEV_COUNT) || !fe_man.tuner[id].drv){
			pr_error("invalid tuner device id %d\n", id);
			return -1;
		}

		fe->tuner = &fe_man.tuner[id];

		pr_dbg("%s: %d\n", buf, id);
	}
#else /*CONFIG_OF*/
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, buf);
	if(res){
		int id = res->start;
		if((id < 0) || (id >= FE_DEV_COUNT) || !fe_man.tuner[id].drv){
			pr_error("invalid tuner device id %d\n", id);
			return -1;
		}

		fe->tuner = &fe_man.tuner[id];
	}
#endif /*CONFIG_OF*/

	snprintf(buf, sizeof(buf), "fe%d_atv_demod", id);
#ifdef CONFIG_OF
	ret = of_property_read_u32(pdev->dev.of_node, buf, &value);
	if(!ret){
		int id = value;
		if((id < 0) || (id >= FE_DEV_COUNT) || !fe_man.atv_demod[id].drv){
			pr_error("invalid ATV demod device id %d\n", id);
			return -1;
		}

		fe->atv_demod = &fe_man.atv_demod[id];
		pr_dbg("%s: %d\n", buf, id);
	}
#else /*CONFIG_OF*/
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, buf);
	if(res){
		int id = res->start;
		if((id < 0) || (id >= FE_DEV_COUNT) || !fe_man.atv_demod[id].drv){
			pr_error("invalid ATV demod device id %d\n", id);
			return -1;
		}

		fe->atv_demod = &fe_man.atv_demod[id];
	}
#endif /*CONFIG_OF*/

	snprintf(buf, sizeof(buf), "fe%d_dtv_demod", id);
#ifdef CONFIG_OF
	ret = of_property_read_u32(pdev->dev.of_node, buf, &value);
	if(!ret){
		int id = value;
		if((id < 0) || (id >= FE_DEV_COUNT) || !fe_man.dtv_demod[id].drv){
			pr_error("invalid DTV demod device id %d\n", id);
			return -1;
		}

		fe->dtv_demod = &fe_man.dtv_demod[id];
		pr_dbg("%s: %d\n", buf, id);
	}
#else /*CONFIG_OF*/
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, buf);
	if(res){
		int id = res->start;

		pr_dbg("[dvb] res->start is %d\n",res->start);
		if((id < 0) || (id >= FE_DEV_COUNT) || !fe_man.dtv_demod[id].drv){
			pr_error("invalid DTV demod device id %d\n", id);
			return -1;
		}

		fe->dtv_demod = &fe_man.dtv_demod[id];
	}
#endif /*CONFIG_OF*/

	snprintf(buf, sizeof(buf), "fe%d_ts", id);
#ifdef CONFIG_OF
	ret = of_property_read_u32(pdev->dev.of_node, buf, &value);
	if(!ret){
		int id = value;
		aml_ts_source_t ts = AM_TS_SRC_TS0;

		switch(id){
			case 0:
				ts = AM_TS_SRC_TS0;
				break;
			case 1:
				ts = AM_TS_SRC_TS1;
				break;
			case 2:
				ts = AM_TS_SRC_TS2;
				break;
			default:
				break;
		}

		fe->ts = ts;
		pr_dbg("%s: %d\n", buf, id);
	}
#else /*CONFIG_OF*/
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, buf);
	if(res){
		int id = res->start;
		aml_ts_source_t ts = AM_TS_SRC_TS0;

		switch(id){
			case 0:
				ts = AM_TS_SRC_TS0;
				break;
			case 1:
				ts = AM_TS_SRC_TS1;
				break;
			case 2:
				ts = AM_TS_SRC_TS2;
				break;
			default:
				break;
		}

		fe->ts = ts;
	}
#endif /*CONFIG_OF*/

	snprintf(buf, sizeof(buf), "fe%d_dev", id);
#ifdef CONFIG_OF
	ret = of_property_read_u32(pdev->dev.of_node, buf, &value);
	if(!ret){
		int id = value;

		if((id >= 0) && (id < FE_DEV_COUNT))
			fe->dev_id = id;
		else
			fe->dev_id = 0;
		pr_dbg("%s: %d\n", buf, fe->dev_id);
	}else{
		fe->dev_id = 0;
		pr_dbg("cannot get resource \"%s\"\n", buf);
	}
#else /*CONFIG_OF*/
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, buf);
	if(res){
		int id = res->start;

		if((id >= 0) && (id < FE_DEV_COUNT))
			fe->dev_id = id;
		else
			fe->dev_id = 0;
		pr_dbg("%s: %d\n", buf, fe->dev_id);
	}else{
		fe->dev_id = 0;
		pr_dbg("cannot get resource \"%s\"\n", buf);
	}
#endif /*CONFIG_OF*/

	aml_fe_man_run(dvb, fe);

	return 0;
}

static int aml_fe_man_release(struct aml_dvb *dvb, struct aml_fe *fe)
{
	if(fe->init){
		aml_dmx_register_frontend(fe->ts, NULL);
		dvb_unregister_frontend(fe->fe);
		dvb_frontend_detach(fe->fe);

		fe->tuner = NULL;
		fe->atv_demod = NULL;
		fe->dtv_demod = NULL;
		fe->init = 0;
	}

	return 0;
}
static ssize_t tuner_name_show(struct class *cls,struct class_attribute *attr,char *buf)
{
        size_t len = 0;
        struct aml_fe_drv *drv;
        unsigned long flags;

        struct aml_fe_drv **list = aml_get_fe_drv_list(AM_DEV_TUNER);
        spin_lock_irqsave(&lock, flags);
        for(drv = *list; drv; drv = drv->next){
	        len += sprintf(buf+len,"%s\n", drv->name);
        }
        spin_unlock_irqrestore(&lock, flags);
        return len;
}

static ssize_t atv_demod_name_show(struct class *cls,struct class_attribute *attr,char *buf)
{
	size_t len = 0;
	struct aml_fe_drv *drv;
	unsigned long flags;

	struct aml_fe_drv **list = aml_get_fe_drv_list(AM_DEV_ATV_DEMOD);
	spin_lock_irqsave(&lock, flags);
	for(drv = *list; drv; drv = drv->next){
		len += sprintf(buf+len,"%s\n", drv->name);
	}
	spin_unlock_irqrestore(&lock, flags);
	return len;
}

static ssize_t dtv_demod_name_show(struct class *cls,struct class_attribute *attr,char *buf)
{
	size_t len = 0;
	struct aml_fe_drv *drv;
	unsigned long flags;

	struct aml_fe_drv **list = aml_get_fe_drv_list(AM_DEV_DTV_DEMOD);
	spin_lock_irqsave(&lock, flags);
	for(drv = *list; drv; drv = drv->next){
		len += sprintf(buf+len,"%s\n", drv->name);
	}
	spin_unlock_irqrestore(&lock, flags);
	return len;
}

static ssize_t setting_show(struct class *cls,struct class_attribute *attr,char *buf)
{
	int r, total = 0;
	int i;
	struct aml_fe_man *fm = &fe_man;

	r = sprintf(buf, "tuner:\n");
	buf += r;
	total += r;
	for(i=0; i<FE_DEV_COUNT; i++){
		struct aml_fe_dev *dev = &fm->tuner[i];
		if(dev->drv){
			r = sprintf(buf, "\t%d: %s i2s_id: %d i2c_addr: 0x%x reset_gpio: 0x%x reset_level: %d\n",
					i,
					dev->drv->name,
					dev->i2c_adap_id,
					dev->i2c_addr,
					dev->reset_gpio,
					dev->reset_value);
			buf += r;
			total += r;
		}
	}

	r = sprintf(buf, "atv_demod:\n");
	buf += r;
	total += r;
	for(i=0; i<FE_DEV_COUNT; i++){
		struct aml_fe_dev *dev = &fm->atv_demod[i];
		if(dev->drv){
			r = sprintf(buf, "\t%d: %s i2s_id: %d i2c_addr: 0x%x reset_gpio: 0x%x reset_level: %d\n",
					i,
					dev->drv->name,
					dev->i2c_adap_id,
					dev->i2c_addr,
					dev->reset_gpio,
					dev->reset_value);
			buf += r;
			total += r;
		}
	}

	r = sprintf(buf, "dtv_demod:\n");
	buf += r;
	total += r;
	for(i=0; i<FE_DEV_COUNT; i++){
		struct aml_fe_dev *dev = &fm->dtv_demod[i];
		if(dev->drv){
			r = sprintf(buf, "\t%d: %s i2s_id: %d i2c_addr: 0x%x reset_gpio: 0x%x reset_level: %d\n",
					i,
					dev->drv->name,
					dev->i2c_adap_id,
					dev->i2c_addr,
					dev->reset_gpio,
					dev->reset_value);
			buf += r;
			total += r;
		}
	}

	r = sprintf(buf, "frontend:\n");
	buf += r;
	total += r;
	for(i=0; i<FE_DEV_COUNT; i++){
		struct aml_fe *fe = &fm->fe[i];

		r = sprintf(buf, "\t%d: %s device: %d ts: %d tuner: %s atv_demod: %s dtv_demod: %s\n",
				i,
				fe->init ? "enabled":"disabled",
				fe->dev_id,
				fe->ts,
				fe->tuner ? fe->tuner->drv->name : "none",
				fe->atv_demod ? fe->atv_demod->drv->name : "none",
				fe->dtv_demod ? fe->dtv_demod->drv->name : "none");
		buf += r;
		total += r;
	}

	return total;
}

static void reset_drv(int id, aml_fe_dev_type_t type, const char *name)
{
	struct aml_fe_man *fm = &fe_man;
	struct aml_fe_drv **list;
	struct aml_fe_drv **pdrv;
	struct aml_fe_drv *drv;
	struct aml_fe_drv *old;

	if((id < 0) || (id >= FE_DEV_COUNT))
		return;

	if(fm->fe[id].init){
		pr_error("cannot reset driver when the device is inused\n");
		return;
	}

	list = aml_get_fe_drv_list(type);
	for(drv = *list; drv; drv = drv->next){
		if(!strcmp(drv->name, name))
			break;
	}

	switch(type){
		case AM_DEV_TUNER:
			pdrv = &fm->tuner[id].drv;
			break;
		case AM_DEV_ATV_DEMOD:
			pdrv = &fm->atv_demod[id].drv;
			break;
		case AM_DEV_DTV_DEMOD:
			pdrv = &fm->dtv_demod[id].drv;
			break;
		default:
			return;
	}

	old = *pdrv;
	if(old == drv)
		return;

	if(old){
		old->ref--;
		if(old->owner)
			module_put(old->owner);
	}

	if(drv){
		drv->ref++;
		if(drv->owner)
			try_module_get(drv->owner);
	}

	*pdrv = drv;
}

static ssize_t setting_store(struct class *class, struct class_attribute *attr, const char *buf, size_t size)
{
	struct aml_dvb *dvb = aml_get_dvb_device();
	struct aml_fe_man *fm = &fe_man;
	int id, val;
	char dev_name[32];
	char gpio_name[32];
	unsigned long flags;

	spin_lock_irqsave(&lock, flags);

	if(sscanf(buf, "tuner %i driver %s", &id, dev_name) == 2){
		reset_drv(id, AM_DEV_TUNER, dev_name);
	}else if(sscanf(buf, "tuner %i i2c_id %i", &id, &val) == 2){
		if((id >= 0) && (id < FE_DEV_COUNT)){
			fm->tuner[id].i2c_adap_id = val;
			fm->tuner[id].i2c_adap = i2c_get_adapter(val);
		}
	}else if(sscanf(buf, "tuner %i i2c_addr %i", &id, &val) == 2){
		if((id >= 0) && (id < FE_DEV_COUNT))
			fm->tuner[id].i2c_addr = val;
#ifdef CONFIG_OF
	}else if(sscanf(buf, "tuner %i reset_gpio %s", &id, gpio_name) == 2){
		val = amlogic_gpio_name_map_num(gpio_name);
#else
	}else if(sscanf(buf, "tuner %i reset_gpio %i", &id, &val) == 2){
#endif
		if((id >= 0) && (id < FE_DEV_COUNT))
			fm->tuner[id].reset_gpio = val;
	}else if(sscanf(buf, "tuner %i reset_level %i", &id, &val) == 2){
		if((id >= 0) && (id < FE_DEV_COUNT))
			fm->tuner[id].reset_value = val;
	}else if(sscanf(buf, "atv_demod %i driver %s", &id, dev_name) == 2){
		reset_drv(id, AM_DEV_ATV_DEMOD, dev_name);
	}else if(sscanf(buf, "atv_demod %i i2c_id %i", &id, &val) == 2){
		if((id >= 0) && (id < FE_DEV_COUNT)){
			fm->atv_demod[id].i2c_adap_id = val;
			fm->dtv_demod[id].i2c_adap = i2c_get_adapter(val);
		}
	}else if(sscanf(buf, "atv_demod %i i2c_addr %i", &id, &val) == 2){
		if((id >= 0) && (id < FE_DEV_COUNT))
			fm->atv_demod[id].i2c_addr = val;
#ifdef CONFIG_OF
	}else if(sscanf(buf, "atv_demod %i reset_gpio %s", &id, gpio_name) == 2){
		val = amlogic_gpio_name_map_num(gpio_name);
#else
	}else if(sscanf(buf, "atv_demod %i reset_gpio %i", &id, &val) == 2){
#endif
		if((id >= 0) && (id < FE_DEV_COUNT))
			fm->atv_demod[id].reset_gpio = val;
	}else if(sscanf(buf, "atv_demod %i reset_level %i", &id, &val) == 2){
		if((id >= 0) && (id < FE_DEV_COUNT))
			fm->atv_demod[id].reset_value = val;
	}else if(sscanf(buf, "dtv_demod %i driver %s", &id, dev_name) == 2){
		reset_drv(id, AM_DEV_DTV_DEMOD, dev_name);
	}else if(sscanf(buf, "dtv_demod %i i2c_id %i", &id, &val) == 2){
		if((id >= 0) && (id < FE_DEV_COUNT)){
			fm->dtv_demod[id].i2c_adap_id = val;
			fm->dtv_demod[id].i2c_adap = i2c_get_adapter(val);
		}
	}else if(sscanf(buf, "dtv_demod %i i2c_addr %i", &id, &val) == 2){
		if((id >= 0) && (id < FE_DEV_COUNT))
			fm->dtv_demod[id].i2c_addr = val;
#ifdef CONFIG_OF
	}else if(sscanf(buf, "dtv_demod %i reset_gpio %s", &id, gpio_name) == 2){
		val = amlogic_gpio_name_map_num(gpio_name);
#else
	}else if(sscanf(buf, "dtv_demod %i reset_gpio %i", &id, &val) == 2){
#endif
		if((id >= 0) && (id < FE_DEV_COUNT))
			fm->dtv_demod[id].reset_gpio = val;
	}else if(sscanf(buf, "dtv_demod %i reset_level %i", &id, &val) == 2){
		if((id >= 0) && (id < FE_DEV_COUNT))
			fm->dtv_demod[id].reset_value = val;
	}else if(sscanf(buf, "frontend %i device %i", &id, &val) == 2){
		if((id >= 0) && (id < FE_DEV_COUNT))
			fm->fe[id].dev_id = val;
	}else if(sscanf(buf, "frontend %i ts %i", &id, &val) == 2){
		if((id >= 0) && (id < FE_DEV_COUNT))
			fm->fe[id].ts = val;
	}else if(sscanf(buf, "frontend %i tuner %i", &id, &val) == 2){
		if((id >= 0) && (id < FE_DEV_COUNT) && (val >= 0) && (val < FE_DEV_COUNT) && fm->tuner[val].drv){
			fm->fe[id].tuner = &fm->tuner[val];
		}
	}else if(sscanf(buf, "frontend %i atv_demod %i", &id, &val) == 2){
		if((id >= 0) && (id < FE_DEV_COUNT) && (val >= 0) && (val < FE_DEV_COUNT) && fm->atv_demod[val].drv){
			fm->fe[id].atv_demod = &fm->atv_demod[val];
		}
	}else if(sscanf(buf, "frontend %i dtv_demod %i", &id, &val) == 2){
		if((id >= 0) && (id < FE_DEV_COUNT) && (val >= 0) && (val < FE_DEV_COUNT) && fm->dtv_demod[val].drv){
			fm->fe[id].dtv_demod = &fm->dtv_demod[val];
		}
	}

	spin_unlock_irqrestore(&lock, flags);

	if(sscanf(buf, "enable %i", &id) == 1){
		if((id >= 0) && (id < FE_DEV_COUNT)){
			aml_fe_man_run(dvb, &fm->fe[id]);
		}
	}else if(sscanf(buf, "disable %i", &id) == 1){
		if((id >= 0) && (id < FE_DEV_COUNT)){
			aml_fe_man_release(dvb, &fm->fe[id]);
		}
	}else if(strstr(buf, "autoload")){
		for(id = 0; id < FE_DEV_COUNT; id++){
			aml_fe_dev_init(dvb, fm->pdev, AM_DEV_TUNER, &fm->tuner[id], id);
			aml_fe_dev_init(dvb, fm->pdev, AM_DEV_ATV_DEMOD, &fm->atv_demod[id], id);
			aml_fe_dev_init(dvb, fm->pdev, AM_DEV_DTV_DEMOD, &fm->dtv_demod[id], id);
		}

		for(id = 0; id < FE_DEV_COUNT; id++){
			aml_fe_man_init(dvb, fm->pdev, &fm->fe[id], id);
		}

	}else if(strstr(buf, "disableall")){
		for(id = 0; id < FE_DEV_COUNT; id++){
			aml_fe_man_release(dvb, &fm->fe[id]);
		}

		for(id = 0; id < FE_DEV_COUNT; id++){
			aml_fe_dev_release(dvb, AM_DEV_DTV_DEMOD, &fm->dtv_demod[id]);
			aml_fe_dev_release(dvb, AM_DEV_ATV_DEMOD, &fm->atv_demod[id]);
			aml_fe_dev_release(dvb, AM_DEV_TUNER, &fm->tuner[id]);
		}
	}

	return size;
}

static ssize_t aml_fe_show_suspended_flag(struct class *class, struct class_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	ret = sprintf(buf, "%d\n", aml_fe_suspended);

	return ret;
}

static ssize_t aml_fe_store_suspended_flag(struct class *class, struct class_attribute *attr, const char *buf, size_t size)
{
	aml_fe_suspended = simple_strtol(buf, 0, 0);

	return size;
}

static struct class_attribute aml_fe_cls_attrs[] = {
	__ATTR(tuner_name,  S_IRUGO | S_IWUSR, tuner_name_show, NULL),
	__ATTR(atv_demod_name,  S_IRUGO | S_IWUSR, atv_demod_name_show, NULL),
	__ATTR(dtv_demod_name,  S_IRUGO | S_IWUSR, dtv_demod_name_show, NULL),
	__ATTR(setting,  S_IRUGO | S_IWUSR, setting_show, setting_store),
	__ATTR(aml_fe_suspended_flag,  S_IRUGO | S_IWUSR, aml_fe_show_suspended_flag, aml_fe_store_suspended_flag),
	__ATTR_NULL
};

static struct class aml_fe_class = {
	.name = "amlfe",
	.class_attrs = aml_fe_cls_attrs,
};

static int aml_fe_probe(struct platform_device *pdev)
{
	struct aml_dvb *dvb = aml_get_dvb_device();
	int i;

	for(i = 0; i < FE_DEV_COUNT; i++){
		if(aml_fe_dev_init(dvb, pdev, AM_DEV_TUNER, &fe_man.tuner[i], i)<0)
			goto probe_end;
		if(aml_fe_dev_init(dvb, pdev, AM_DEV_ATV_DEMOD, &fe_man.atv_demod[i], i)<0)
			goto probe_end;
		if(aml_fe_dev_init(dvb, pdev, AM_DEV_DTV_DEMOD, &fe_man.dtv_demod[i], i)<0)
			goto probe_end;
	}

	for(i = 0; i < FE_DEV_COUNT; i++){
		if(aml_fe_man_init(dvb, pdev, &fe_man.fe[i], i)<0)
			goto probe_end;
	}

probe_end:

#ifdef CONFIG_OF
	fe_man.pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
#endif

	platform_set_drvdata(pdev, &fe_man);

	if(class_register(&aml_fe_class) < 0) {
		pr_error("[aml_fe..] register class error\n");
	}

	fe_man.pdev = pdev;

	pr_dbg("[aml_fe..] probe ok.\n");

	return 0;
}

static int aml_fe_remove(struct platform_device *pdev)
{
	struct aml_fe_man *fe_man = platform_get_drvdata(pdev);
	struct aml_dvb *dvb = aml_get_dvb_device();
	int i;

	if(fe_man){
		platform_set_drvdata(pdev, NULL);

		for(i = 0; i < FE_DEV_COUNT; i++){
			aml_fe_man_release(dvb, &fe_man->fe[i]);
		}

		for(i = 0; i < FE_DEV_COUNT; i++){
			aml_fe_dev_release(dvb, AM_DEV_DTV_DEMOD, &fe_man->dtv_demod[i]);
			aml_fe_dev_release(dvb, AM_DEV_ATV_DEMOD, &fe_man->atv_demod[i]);
			aml_fe_dev_release(dvb, AM_DEV_TUNER, &fe_man->tuner[i]);
		}

		if(fe_man->pinctrl)
			devm_pinctrl_put(fe_man->pinctrl);
	}

	class_unregister(&aml_fe_class);

	return 0;
}

static int aml_fe_suspend(struct platform_device *dev, pm_message_t state)
{
	int i;

	for(i = 0; i < FE_DEV_COUNT; i++){
		struct aml_fe *fe = &fe_man.fe[i];

		if(fe->tuner && fe->tuner->drv && fe->tuner->drv->suspend){
			fe->tuner->drv->suspend(fe->tuner);
		}

		if(fe->atv_demod && fe->atv_demod->drv && fe->atv_demod->drv->suspend){
			fe->atv_demod->drv->suspend(fe->atv_demod);
		}

		if(fe->dtv_demod && fe->dtv_demod->drv && fe->dtv_demod->drv->suspend){
			fe->dtv_demod->drv->suspend(fe->dtv_demod);
		}
	}

	aml_fe_suspended = 1;

	return 0;
}

static int aml_fe_resume(struct platform_device *dev)
{
	int i;

	for(i = 0; i < FE_DEV_COUNT; i++){
		struct aml_fe *fe = &fe_man.fe[i];

		if(fe->tuner && fe->tuner->drv && fe->tuner->drv->resume){
			fe->tuner->drv->resume(fe->tuner);
		}

		if(fe->atv_demod && fe->atv_demod->drv && fe->atv_demod->drv->resume){
			fe->atv_demod->drv->resume(fe->atv_demod);
		}

		if(fe->dtv_demod && fe->dtv_demod->drv && fe->dtv_demod->drv->resume){
			fe->dtv_demod->drv->resume(fe->dtv_demod);
		}
	}

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id aml_fe_dt_match[]={
	{
		.compatible = "amlogic,dvbfe",
	},
	{},
};
#endif /*CONFIG_OF*/

static struct platform_driver aml_fe_driver = {
	.probe		= aml_fe_probe,
	.remove		= aml_fe_remove,
	.suspend        = aml_fe_suspend,
	.resume         = aml_fe_resume,
	.driver		= {
		.name	= "amlogic-dvb-fe",
		.owner	= THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = aml_fe_dt_match,
#endif
	}
};
const char *audmode_to_str(unsigned short audmode)
{
  /*  switch(audmode)
    {
        case V4L2_TUNER_AUDMODE_NULL:
            return "V4L2_TUNER_AUDMODE_NULL";
            break;
        case V4L2_TUNER_MODE_MONO:
            return "V4L2_TUNER_MODE_MONO";
            break;
        case V4L2_TUNER_MODE_STEREO:
            return "V4L2_TUNER_MODE_STEREO";
            break;
        case V4L2_TUNER_MODE_LANG2:
            return "V4L2_TUNER_MODE_LANG2";
            break;
        case V4L2_TUNER_MODE_SAP:
            return "V4L2_TUNER_MODE_SAP";
            break;
        case V4L2_TUNER_SUB_LANG1:
            return "V4L2_TUNER_SUB_LANG1";
            break;
        case V4L2_TUNER_MODE_LANG1_LANG2:
            return "V4L2_TUNER_MODE_LANG1_LANG2";
            break;
        default:
            return "NO AUDMODE";
            break;
    }*/
	return 0;
}
EXPORT_SYMBOL(audmode_to_str);
const char *soundsys_to_str(unsigned short sys)
{
/*    switch(sys)
    {
        case V4L2_TUNER_SYS_NULL:
            return "V4L2_TUNER_SYS_NULL";
            break;
         case V4L2_TUNER_SYS_A2_BG:
            return "V4L2_TUNER_SYS_A2_BG";
            break;
         case V4L2_TUNER_SYS_A2_DK1:
            return "V4L2_TUNER_SYS_A2_DK1";
            break;
         case V4L2_TUNER_SYS_A2_DK2:
            return "V4L2_TUNER_SYS_A2_DK2";
            break;
         case V4L2_TUNER_SYS_A2_DK3:
            return "V4L2_TUNER_SYS_A2_DK3";
            break;
         case V4L2_TUNER_SYS_A2_M:
            return "V4L2_TUNER_SYS_A2_M";
            break;
         case V4L2_TUNER_SYS_NICAM_BG:
            return "V4L2_TUNER_SYS_NICAM_BG";
            break;
        case V4L2_TUNER_SYS_NICAM_I:
            return "V4L2_TUNER_SYS_NICAM_I";
            break;
        case V4L2_TUNER_SYS_NICAM_DK:
            return "V4L2_TUNER_SYS_NICAM_DK";
            break;
        case V4L2_TUNER_SYS_NICAM_L:
            return "V4L2_TUNER_SYS_NICAM_L";
            break;
        case V4L2_TUNER_SYS_EIAJ:
            return "V4L2_TUNER_SYS_EIAJ";
            break;
        case V4L2_TUNER_SYS_BTSC:
            return "V4L2_TUNER_SYS_BTSC";
            break;
        case V4L2_TUNER_SYS_FM_RADIO:
            return "V4L2_TUNER_SYS_FM_RADIO";
            break;
        default:
            return "NO SOUND SYS";
            break;
    }*/
    return 0;
}
EXPORT_SYMBOL(soundsys_to_str);

const char *v4l2_std_to_str(v4l2_std_id std)
{
    switch(std){
        case V4L2_STD_PAL_B:
            return "V4L2_STD_PAL_B";
            break;
        case V4L2_STD_PAL_B1:
            return "V4L2_STD_PAL_B1";
            break;
        case V4L2_STD_PAL_G:
            return "V4L2_STD_PAL_G";
            break;
        case V4L2_STD_PAL_H:
            return "V4L2_STD_PAL_H";
            break;
        case V4L2_STD_PAL_I:
            return "V4L2_STD_PAL_I";
            break;
        case V4L2_STD_PAL_D:
            return "V4L2_STD_PAL_D";
            break;
        case V4L2_STD_PAL_D1:
            return "V4L2_STD_PAL_D1";
            break;
        case V4L2_STD_PAL_K:
            return "V4L2_STD_PAL_K";
            break;
        case V4L2_STD_PAL_M:
            return "V4L2_STD_PAL_M";
            break;
        case V4L2_STD_PAL_N:
            return "V4L2_STD_PAL_N";
            break;
        case V4L2_STD_PAL_Nc:
            return "V4L2_STD_PAL_Nc";
            break;
        case V4L2_STD_PAL_60:
            return "V4L2_STD_PAL_60";
            break;
        case V4L2_STD_NTSC_M:
            return "V4L2_STD_NTSC_M";
            break;
        case V4L2_STD_NTSC_M_JP:
            return "V4L2_STD_NTSC_M_JP";
            break;
        case V4L2_STD_NTSC_443:
            return "V4L2_STD_NTSC_443";
            break;
        case V4L2_STD_NTSC_M_KR:
            return "V4L2_STD_NTSC_M_KR";
            break;
        case V4L2_STD_SECAM_B:
            return "V4L2_STD_SECAM_B";
            break;
         case V4L2_STD_SECAM_D:
            return "V4L2_STD_SECAM_D";
            break;
        case V4L2_STD_SECAM_G:
            return "V4L2_STD_SECAM_G";
            break;
        case V4L2_STD_SECAM_H:
            return "V4L2_STD_SECAM_H";
            break;
        case V4L2_STD_SECAM_K:
            return "V4L2_STD_SECAM_K";
            break;
        case V4L2_STD_SECAM_K1:
            return "V4L2_STD_SECAM_K1";
            break;
        case V4L2_STD_SECAM_L:
            return "V4L2_STD_SECAM_L";
            break;
        case V4L2_STD_SECAM_LC:
            return "V4L2_STD_SECAM_LC";
            break;
        case V4L2_STD_ATSC_8_VSB:
            return "V4L2_STD_ATSC_8_VSB";
            break;
         case V4L2_STD_ATSC_16_VSB:
            return "V4L2_STD_ATSC_16_VSB";
            break;
         case V4L2_COLOR_STD_PAL:
            return "V4L2_COLOR_STD_PAL";
            break;
         case V4L2_COLOR_STD_NTSC:
            return "V4L2_COLOR_STD_NTSC";
            break;
        case V4L2_COLOR_STD_SECAM:
            return "V4L2_COLOR_STD_SECAM";
            break;
        case V4L2_STD_MN:
            return "V4L2_STD_MN";
            break;
        case V4L2_STD_B:
            return "V4L2_STD_B";
            break;
         case V4L2_STD_GH:
            return "V4L2_STD_GH";
            break;
        case V4L2_STD_DK:
            return "V4L2_STD_DK";
            break;
        case V4L2_STD_PAL_BG:
            return "V4L2_STD_PAL_BG";
            break;
        case V4L2_STD_PAL_DK:
            return "V4L2_STD_PAL_DK";
            break;
        case V4L2_STD_PAL:
            return "V4L2_STD_PAL";
            break;
        case V4L2_STD_NTSC:
            return "V4L2_STD_NTSC";
            break;
        case V4L2_STD_SECAM_DK:
            return "V4L2_STD_SECAM_DK";
            break;
        case V4L2_STD_SECAM:
            return "V4L2_STD_SECAM";
            break;
        case V4L2_STD_525_60:
            return "V4L2_STD_525_60";
            break;
        case V4L2_STD_625_50:
            return "V4L2_STD_625_50";
            break;
        case V4L2_STD_ATSC:
            return "V4L2_STD_ATSC";
            break;
         case V4L2_STD_ALL:
            return "V4L2_STD_ALL";
            break;
         default:
            return "V4L2_STD_UNKNOWN";
            break;
    }
}
EXPORT_SYMBOL(v4l2_std_to_str);

const char* fe_type_to_str(fe_type_t type)
{
    switch(type)
    {
        case FE_QPSK:
            return "FE_QPSK";
            break;
        case FE_QAM:
            return "FE_QAM";
            break;
        case FE_OFDM:
            return "FE_OFDM";
            break;
        case FE_ATSC:
            return "FE_ATSC";
            break;
        case FE_ANALOG:
            return "FE_ANALOG";
            break;
		case FE_ISDBT:
			return "FE_ISDBT";
			break;
		case FE_DTMB:
            return "FE_DTMB";
            break;
       default:
            return "UNKONW TYPE";
            break;
    }
}
EXPORT_SYMBOL(fe_type_to_str);


static int __init aml_fe_init(void)
{
	return platform_driver_register(&aml_fe_driver);
}


static void __exit aml_fe_exit(void)
{
	platform_driver_unregister(&aml_fe_driver);
}

module_init(aml_fe_init);
module_exit(aml_fe_exit);


MODULE_DESCRIPTION("amlogic frontend driver");
MODULE_AUTHOR("L+#= +0=1");
