/*****************************************************************
**
**  Copyright (C) 2009 Amlogic,Inc.
**  All rights reserved
**        Filename : amlfrontend.c
**
**  comment:
**        Driver for m6_demod demodulator
**  author :
**	    Shijie.Rong@amlogic
**  version :
**	    v1.0	 12/3/13
*****************************************************************/

/*
    Driver for m6_demod demodulator
*/

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#ifdef ARC_700
#include <asm/arch/am_regs.h>
#else
#include <mach/am_regs.h>
#endif
#include <linux/i2c.h>
#include <linux/gpio.h>
#include "../aml_fe.h"

#include "aml_demod.h"
#include "demod_func.h"
#include "../aml_dvb.h"
#include "amlfrontend.h"

#define pr_dbg(fmt, args...)\
	do{\
		if(debug_aml)\
			printk("FE: " fmt, ## args);\
	}while(0)

MODULE_PARM_DESC(debug_aml, "\n\t\t Enable frontend debug information");
static int debug_aml = 0;
module_param(debug_aml, int, 0644);

#define pr_error(fmt, args...) printk("M6_DEMOD: "fmt, ## args)

static int last_lock=-1;
#define DEMOD_DEVICE_NAME  "m6_demod"
static int cci_thread=0;


static int freq_dvbc=0;
static struct aml_demod_sta demod_status;
static fe_modulation_t atsc_mode=VSB_8;

long *mem_buf;
int memstart;

MODULE_PARM_DESC(frontend_mode, "\n\t\t Frontend mode 0-DVBC, 1-DVBT");
static int frontend_mode = -1;
module_param(frontend_mode, int, S_IRUGO);

MODULE_PARM_DESC(frontend_i2c, "\n\t\t IIc adapter id of frontend");
static int frontend_i2c = -1;
module_param(frontend_i2c, int, S_IRUGO);

MODULE_PARM_DESC(frontend_tuner, "\n\t\t Frontend tuner type 0-NULL, 1-DCT7070, 2-Maxliner, 3-FJ2207, 4-TD1316");
static int frontend_tuner = -1;
module_param(frontend_tuner, int, S_IRUGO);

MODULE_PARM_DESC(frontend_tuner_addr, "\n\t\t Tuner IIC address of frontend");
static int frontend_tuner_addr = -1;
module_param(frontend_tuner_addr, int, S_IRUGO);
static int autoflags=0,autoFlagsTrig=0;
static struct mutex aml_lock;

static int M6_Demod_Dvbc_Init(struct aml_fe_dev *dev,int mode);


static ssize_t dvbc_auto_sym_show(struct class *cls,struct class_attribute *attr,char *buf)
{
        return sprintf(buf, "dvbc_autoflags: %s\n", autoflags?"on":"off");
}
static ssize_t dvbc_auto_sym_store(struct class *cls, struct class_attribute *attr, const char *buf, size_t count)
{
		int mode = simple_strtol(buf,0,16);
		printk("autoflags is %d\n",mode);
		autoFlagsTrig = 1;
		autoflags = mode;
		return count;

}

#ifdef CONFIG_AM_SI2176
extern	int si2176_get_strength(void);
#endif
static ssize_t dvbc_para_show(struct class *cls,struct class_attribute *attr,char *buf)
{
	struct aml_demod_sts demod_sts;
	struct aml_demod_i2c demod_i2c;
	char *pbuf=buf;
	int strength=0;

	mutex_lock(&aml_lock);

	dvbc_status(&demod_status,&demod_i2c, &demod_sts);
	#ifdef CONFIG_AM_SI2176
			 strength=si2176_get_strength();
	#endif
	pbuf+=sprintf(pbuf, "dvbc_para: ch_sts is %d", demod_sts.ch_sts);
	pbuf+=sprintf(pbuf, "snr %d dB \n", demod_sts.ch_snr/100);
	pbuf+=sprintf(pbuf, "ber %d", demod_sts.ch_ber);
	pbuf+=sprintf(pbuf, "per %d \n", demod_sts.ch_per);
	pbuf+=sprintf(pbuf, "srate %d", demod_sts.symb_rate);
	pbuf+=sprintf(pbuf, "freqoff %d kHz \n", demod_sts.freq_off);
	pbuf+=sprintf(pbuf, "power is %d db", demod_sts.ch_pow);
	pbuf+=sprintf(pbuf, "tuner strength -%d db", (256-strength));

	mutex_unlock(&aml_lock);

	return pbuf-buf;
}
static ssize_t dvbc_para_store(struct class *cls, struct class_attribute *attr, const char *buf, size_t count)
{
	int mode = simple_strtol(buf,0,16);
	printk("autoflags is %d\n",mode);
	return count;
}

static int readregdata=0;

static ssize_t dvbc_reg_show(struct class *cls,struct class_attribute *attr,char *buf)
{
//	int readregaddr=0;
	char *pbuf=buf;
	pbuf+=sprintf(pbuf, "%x", readregdata);

	printk("read dvbc_reg\n");
	return pbuf-buf;

}

static ssize_t dvbc_reg_store(struct class *cls, struct class_attribute *attr, const char *buf, size_t count)
{
	int setregaddr=0;
	int setregdata=0;
	int settunerfreq=0;
	int n=0;
    char *buf_orig, *ps, *token;
    char *parm[8];
	struct aml_cap_data cap;
    buf_orig = kstrdup(buf, GFP_KERNEL);
    ps = buf_orig;
	printk("write dvbc_reg\n");
	mutex_lock(&aml_lock);
 	while (1) {
            token = strsep(&ps, "\b");
            if (token == NULL)
                    break;
            if (*token == '\0')
                    continue;
            parm[n++] = token;
			printk("parm[%p]\n",parm[n]);
    }
	if (!strncmp(parm[0],"w",strlen("w")))  //write reg
    {
     /*   if (n != 2)
        {
                pr_err("read: invalid parameter\n");
                kfree(buf_orig);
                return count;
        }*/
        printk("write\n");
        setregaddr = simple_strtol(parm[1], NULL, 16);
		setregdata = simple_strtol(parm[2], NULL, 16);
		printk("[debug][W]reg[%x]now set to[%x]\n",setregaddr,setregdata);
		apb_write_reg(QAM_BASE+setregaddr,setregdata);
		printk("[debug][W]reg[%x]now set to[%x]\n",setregaddr,setregdata);
	}else if(!strncmp(parm[0],"r",strlen("r")))   //read reg
    {
        setregaddr = simple_strtol(parm[1], NULL, 16);
		readregdata=apb_read_reg(QAM_BASE+setregaddr);
		printk("[debug][R]reg[%x]now is[%x]\n",setregaddr,setregdata);
	}else if(!strncmp(parm[0],"c",strlen("c")))   //capture data
    {
      /*  if (n != 2)
        {
                pr_err("read: invalid parameter\n");
                kfree(buf_orig);
                return count;
        }*/
      //  setregaddr = simple_strtol(parm[1], NULL, 16);
	    cap.cap_addr=0x94400000;
		cap.cap_size=4*1024*1024;
		cap.cap_afifo=0x60;
	  	cap_adc_data(&cap);
		printk("[capture]now begin to capture data[%x][%x]\n",cap.cap_addr,cap.cap_size);
	}else if(!strncmp(parm[0],"t",strlen("t")))   //set tuner
    {
        settunerfreq = simple_strtol(parm[1], NULL, 16);

		printk("[debug][R]reg[%x]now is[%x]\n",setregaddr,setregdata);
	}else
                printk("invalid command\n");
        kfree(buf_orig);

		mutex_unlock(&aml_lock);
        return count;


}



static CLASS_ATTR(auto_sym,0644,dvbc_auto_sym_show,dvbc_auto_sym_store);
static CLASS_ATTR(dvbc_para,0644,dvbc_para_show,dvbc_para_store);
static CLASS_ATTR(dvbc_reg,0666,dvbc_reg_show,dvbc_reg_store);


static irqreturn_t amdemod_isr(int irq, void *data)
{
/*	struct aml_fe_dev *state = data;

	#define dvb_isr_islock()	(((frontend_mode==0)&&dvbc_isr_islock()) \
								||((frontend_mode==1)&&dvbt_isr_islock()))
	#define dvb_isr_monitor()       do { if(frontend_mode==1) dvbt_isr_monitor(); }while(0)
	#define dvb_isr_cancel()	do { if(frontend_mode==1) dvbt_isr_cancel(); \
		else if(frontend_mode==0) dvbc_isr_cancel();}while(0)


	dvb_isr_islock();
	{
		if(waitqueue_active(&state->lock_wq))
			wake_up_interruptible(&state->lock_wq);
	}

	dvb_isr_monitor();

	dvb_isr_cancel();*/

	return IRQ_HANDLED;
}

static int install_isr(struct aml_fe_dev *state)
{
	int r = 0;

	/* hook demod isr */
	pr_dbg("amdemod irq register[IRQ(%d)].\n", INT_DEMOD);
	r = request_irq(INT_DEMOD, &amdemod_isr,
				IRQF_SHARED, "amldemod",
				(void *)state);
	if (r) {
		pr_error("amdemod irq register error.\n");
	}
	return r;
}

static void uninstall_isr(struct aml_fe_dev *state)
{
	pr_dbg("amdemod irq unregister[IRQ(%d)].\n", INT_DEMOD);

	free_irq(INT_DEMOD, (void*)state);
}


static int amdemod_qam(fe_modulation_t qam)
{
	switch(qam)
	{
		case QAM_16:  return 0;
		case QAM_32:  return 1;
		case QAM_64:  return 2;
		case QAM_128: return 3;
		case QAM_256: return 4;
		case VSB_8:	  return 5;
		case QAM_AUTO:	  return 6;
		default:          return 2;
	}
	return 2;
}


static int amdemod_stat_islock(struct aml_fe_dev *dev, int mode)
{
	struct aml_demod_sts demod_sts;
	int lock_status;
	int dvbt_status1;
	dvbt_status1=((apb_read_reg(DVBT_BASE+(0x0a<<2))>>20)&0x3ff);

	if(mode==0){
		/*DVBC*/
		//dvbc_status(state->sta, state->i2c, &demod_sts);
		demod_sts.ch_sts = apb_read_reg(QAM_BASE+0x18);
		return (demod_sts.ch_sts&0x1);
	} else if (mode==1){
		/*DVBT*/
		lock_status=(apb_read_reg(DVBT_BASE+(0x2a<<2)))&0xf;
		if((((lock_status)==9)||((lock_status)==10))&&((dvbt_status1)!=0)){
			return 1;
		}else{
			return 0;
		}
	 //((apb_read_reg(DVBT_BASE+0x0)>>12)&0x1);//dvbt_get_status_ops()->get_status(&demod_sts, &demod_sta);
	}else if (mode==2){
		/*ISDBT*/
	//	return dvbt_get_status_ops()->get_status(demod_sts, demod_sta);
	}else if (mode==3){
		/*ATSC*/
			if((atsc_mode==QAM_64)||(atsc_mode==QAM_256)){
				return ((atsc_read_iqr_reg()>>16)==0x1f);//
			}else if(atsc_mode==VSB_8){
				return (atsc_read_reg(0x0980)==0x79);
			}else{
				return ((atsc_read_iqr_reg()>>16)==0x1f);
			}
	}else if (mode==4){
		/*DTMB*/
			pr_dbg("DTMB lock status is %lu\n",((apb_read_reg(DTMB_BASE+(0x0e3<<2))>>12)&0x1));
			return (apb_read_reg(DTMB_BASE+(0x0e3<<2))>>12)&0x1;
	}
	return 0;
}
#define amdemod_dvbc_stat_islock(dev)  amdemod_stat_islock((dev), 0)
#define amdemod_dvbt_stat_islock(dev)  amdemod_stat_islock((dev), 1)
#define amdemod_isdbt_stat_islock(dev)  amdemod_stat_islock((dev), 2)
#define amdemod_atsc_stat_islock(dev)  amdemod_stat_islock((dev), 3)
#define amdemod_dtmb_stat_islock(dev)  amdemod_stat_islock((dev), 4)


static int m6_demod_dvbc_set_qam_mode(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct aml_demod_dvbc param;//mode 0:16, 1:32, 2:64, 3:128, 4:256
	memset(&param, 0, sizeof(param));
	param.mode = amdemod_qam(c->modulation);
	dvbc_set_qam_mode(param.mode);
	return 0;
}


static void m6_demod_dvbc_release(struct dvb_frontend *fe)
{
	struct aml_fe_dev *state = fe->demodulator_priv;

	uninstall_isr(state);

	kfree(state);
}


static int m6_demod_dvbc_read_status(struct dvb_frontend *fe, fe_status_t * status)
{
//	struct aml_fe_dev *dev = afe->dtv_demod;
	struct aml_demod_sts demod_sts;
//	struct aml_demod_sta demod_sta;
//	struct aml_demod_i2c demod_i2c;
	int ilock;
	demod_sts.ch_sts = apb_read_reg(QAM_BASE+0x18);
//	dvbc_status(&demod_sta, &demod_i2c, &demod_sts);
	if(demod_sts.ch_sts&0x1)
	{
		ilock=1;
		*status = FE_HAS_LOCK|FE_HAS_SIGNAL|FE_HAS_CARRIER|FE_HAS_VITERBI|FE_HAS_SYNC;
	}
	else
	{
		ilock=0;
		*status = FE_TIMEDOUT;
	}
	if(last_lock != ilock){
		pr_error("%s.\n", ilock? "!!  >> LOCK << !!" : "!! >> UNLOCK << !!");
		last_lock = ilock;
	}

	return  0;
}

static int m6_demod_dvbc_read_ber(struct dvb_frontend *fe, u32 * ber)
{
	//struct aml_fe_dev *dev = afe->dtv_demod;
	struct aml_demod_sts demod_sts;
	struct aml_demod_i2c demod_i2c;
	struct aml_demod_sta demod_sta;


	dvbc_status(&demod_sta, &demod_i2c, &demod_sts);
	*ber = demod_sts.ch_ber;
	return 0;
}

static int m6_demod_dvbc_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct aml_fe *afe = fe->demodulator_priv;
	struct aml_fe_dev *dev = afe->dtv_demod;

	*strength=256-tuner_get_ch_power(dev);

	return 0;
}

static int m6_demod_dvbc_read_snr(struct dvb_frontend *fe, u16 * snr)
{
	struct aml_demod_sts demod_sts;
	struct aml_demod_i2c demod_i2c;
	struct aml_demod_sta demod_sta;
	dvbc_status(&demod_sta, &demod_i2c, &demod_sts);
	*snr = demod_sts.ch_snr/100;
	return 0;
}

static int m6_demod_dvbc_read_ucblocks(struct dvb_frontend *fe, u32 * ucblocks)
{
	*ucblocks=0;
	return 0;
}

extern int aml_fe_analog_set_frontend(struct dvb_frontend* fe);
static int m6_demod_dvbc_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct aml_demod_dvbc param;//mode 0:16, 1:32, 2:64, 3:128, 4:256
	struct aml_demod_sts demod_sts;
	struct aml_demod_i2c demod_i2c;
	struct aml_fe *afe = fe->demodulator_priv;
	struct aml_fe_dev *dev = afe->dtv_demod;
	int error,times;
	demod_i2c.tuner=dev->drv->id;
	demod_i2c.addr=dev->i2c_addr;
	times = 2;
	memset(&param, 0, sizeof(param));
	param.ch_freq = c->frequency/1000;
	param.mode = amdemod_qam(c->modulation);
	param.symb_rate = c->symbol_rate/1000;
	if((param.mode==3)&&(demod_status.tmp!=Adc_mode)){
		M6_Demod_Dvbc_Init(dev,Adc_mode);
		printk("M6_Demod_Dvbc_Init,Adc_mode\n");
	}else{
	//	M6_Demod_Dvbc_Init(dev,Cry_mode);
	}
	if(autoflags==0){
	//	printk("QAM_TUNING mode \n");
		//	flag=0;
	}
	if((autoflags==1)&&(autoFlagsTrig == 0)&&(freq_dvbc==param.ch_freq)){
		printk("now is auto symbrating\n");
		return 0;
	}
	autoFlagsTrig = 0;
	last_lock = -1;
	pr_dbg("[m6_demod_dvbc_set_frontend]PARA demod_i2c.tuner is %d||||demod_i2c.addr is %d||||param.ch_freq is %d||||param.symb_rate is %d,param.mode is %d\n",
		demod_i2c.tuner,demod_i2c.addr,param.ch_freq,param.symb_rate,param.mode);
retry:
	aml_dmx_before_retune(afe->ts, fe);
	aml_fe_analog_set_frontend(fe);
	dvbc_set_ch(&demod_status, &demod_i2c, &param);
	if(autoflags==1){
		printk("QAM_PLAYING mode,start auto sym\n");
		dvbc_set_auto_symtrack();
	//	flag=1;
	}
//rsj_debug

    dvbc_status(&demod_status,&demod_i2c, &demod_sts);
	freq_dvbc=param.ch_freq;

	times--;
	if(amdemod_dvbc_stat_islock(dev) && times){
		int lock;

		aml_dmx_start_error_check(afe->ts, fe);
		msleep(20);
		error = aml_dmx_stop_error_check(afe->ts, fe);
		lock  = amdemod_dvbc_stat_islock(dev);
		if((error > 200) || !lock){
			pr_error("amlfe too many error, error count:%d lock statuc:%d, retry\n", error, lock);
			goto retry;
		}
	}

	aml_dmx_after_retune(afe->ts, fe);

	afe->params = *c;
/*	afe->params.frequency = c->frequency;
	afe->params.u.qam.symbol_rate = c->symbol_rate;
	afe->params.u.qam.modulation = c->modulation;*/


	pr_dbg("AML amldemod => frequency=%d,symbol_rate=%d\r\n",c->frequency,c->symbol_rate);
	return  0;

}

static int m6_demod_dvbc_get_frontend(struct dvb_frontend *fe)
{//these content will be writed into eeprom .

	struct aml_fe *afe = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int qam_mode;
	qam_mode=apb_read_reg(QAM_BASE+0x008);
	afe->params.modulation=(qam_mode&7)+1;
	printk("[mode] is %d\n",afe->params.modulation);

	*c=afe->params;
/*	c->modulation= afe->params.u.qam.modulation;
	c->frequency= afe->params.frequency;
	c->symbol_rate= afe->params.u.qam.symbol_rate;*/
	return 0;
}


static int M6_Demod_Dvbc_Init(struct aml_fe_dev *dev,int mode)
{
	struct aml_demod_sys sys;
	struct aml_demod_i2c i2c;
	pr_dbg("AML Demod DVB-C init\r\n");
	memset(&sys, 0, sizeof(sys));
	memset(&i2c, 0, sizeof(i2c));
	i2c.tuner = dev->drv->id;
	i2c.addr = dev->i2c_addr;
	// 0 -DVBC, 1-DVBT, ISDBT, 2-ATSC
	demod_status.dvb_mode = M6_Dvbc;

	if(mode==Adc_mode){
    	sys.adc_clk=35000;
    	sys.demod_clk=100000;
		demod_status.tmp=Adc_mode;
	}else{
		sys.adc_clk=Adc_Clk_24M;
    	sys.demod_clk=Demod_Clk_72M;
		demod_status.tmp=Cry_mode;
	}
	demod_status.ch_if=Si2176_6M_If*1000;
	pr_dbg("[%s]adc_clk is %d,demod_clk is %d\n",__func__,sys.adc_clk,sys.demod_clk);
	autoFlagsTrig = 1;
	demod_set_sys(&demod_status, &i2c, &sys);
	return 0;
}


static void m6_demod_dvbt_release(struct dvb_frontend *fe)
{
	struct aml_fe_dev *state = fe->demodulator_priv;

	uninstall_isr(state);

	kfree(state);
}


static int m6_demod_dvbt_read_status(struct dvb_frontend *fe, fe_status_t * status)
{
//	struct aml_fe *afe = fe->demodulator_priv;
	struct aml_demod_i2c demod_i2c;
	struct aml_demod_sta demod_sta;
	int ilock;
	unsigned char s=0;
	s = dvbt_get_status_ops()->get_status(&demod_sta, &demod_i2c);
	if(s==1)
	{
		ilock=1;
		*status = FE_HAS_LOCK|FE_HAS_SIGNAL|FE_HAS_CARRIER|FE_HAS_VITERBI|FE_HAS_SYNC;
	}
	else
	{
		ilock=0;
		*status = FE_TIMEDOUT;
	}
	if(last_lock != ilock){
		pr_error("%s.\n", ilock? "!!  >> LOCK << !!" : "!! >> UNLOCK << !!");
		last_lock = ilock;
	}

	return  0;
}

static int m6_demod_dvbt_read_ber(struct dvb_frontend *fe, u32 * ber)
{
//	struct aml_fe *afe = fe->demodulator_priv;
	struct aml_demod_i2c demod_i2c;
	struct aml_demod_sta demod_sta;


	*ber = dvbt_get_status_ops()->get_ber(&demod_sta, &demod_i2c)&0xffff;
	return 0;
}

static int m6_demod_dvbt_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct aml_fe *afe = fe->demodulator_priv;
	struct aml_fe_dev *dev = afe->dtv_demod;

	*strength=256-tuner_get_ch_power(dev);
	printk("[RSJ]tuner strength is %d dbm\n",*strength);
	return 0;
}

static int m6_demod_dvbt_read_snr(struct dvb_frontend *fe, u16 * snr)
{
//	struct aml_fe *afe = fe->demodulator_priv;
//	struct aml_demod_sts demod_sts;
	struct aml_demod_i2c demod_i2c;
	struct aml_demod_sta demod_sta;


	*snr = dvbt_get_status_ops()->get_snr(&demod_sta, &demod_i2c);
	*snr/=8;
	printk("[RSJ]snr is %d dbm\n",*snr);
	return 0;
}

static int m6_demod_dvbt_read_ucblocks(struct dvb_frontend *fe, u32 * ucblocks)
{
	*ucblocks=0;
	return 0;
}



static int m6_demod_dvbt_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	//struct aml_demod_sts demod_sts;
	struct aml_demod_i2c demod_i2c;
	int error,times;
	struct aml_demod_dvbt param;
	struct aml_fe *afe = fe->demodulator_priv;
	struct aml_fe_dev *dev = afe->dtv_demod;

	demod_i2c.tuner=dev->drv->id;
	demod_i2c.addr=dev->i2c_addr;

	times = 2;

    //////////////////////////////////////
    // bw == 0 : 8M
    //       1 : 7M
    //       2 : 6M
    //       3 : 5M
    // agc_mode == 0: single AGC
    //             1: dual AGC
    //////////////////////////////////////
    memset(&param, 0, sizeof(param));
	param.ch_freq = c->frequency/1000;
	param.bw = c->bandwidth_hz;
	param.agc_mode = 1;
	/*ISDBT or DVBT : 0 is QAM, 1 is DVBT, 2 is ISDBT, 3 is DTMB, 4 is ATSC */
	param.dat0 = 1;
	last_lock = -1;

retry:
	aml_dmx_before_retune(AM_TS_SRC_TS2, fe);
	aml_fe_analog_set_frontend(fe);
	dvbt_set_ch(&demod_status, &demod_i2c, &param);

	/*	for(count=0;count<10;count++){
			if(amdemod_dvbt_stat_islock(dev)){
				printk("first lock success\n");
				break;
			}

			msleep(200);
		}	*/
//rsj_debug

//

	times--;
	if(amdemod_dvbt_stat_islock(dev) && times){
		int lock;

		aml_dmx_start_error_check(AM_TS_SRC_TS2, fe);
		msleep(20);
		error = aml_dmx_stop_error_check(AM_TS_SRC_TS2, fe);
		lock  = amdemod_dvbt_stat_islock(dev);
		if((error > 200) || !lock){
			pr_error("amlfe too many error, error count:%d lock statuc:%d, retry\n", error, lock);
			goto retry;
		}
	}

	aml_dmx_after_retune(AM_TS_SRC_TS2, fe);


	afe->params = *c;


//	pr_dbg("AML amldemod => frequency=%d,symbol_rate=%d\r\n",p->frequency,p->u.qam.symbol_rate);
	return  0;

}

static int m6_demod_dvbt_get_frontend(struct dvb_frontend *fe)
{//these content will be writed into eeprom .

	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct aml_fe *afe = fe->demodulator_priv;

	*c = afe->params;
	return 0;
}



int M6_Demod_Dvbt_Init(struct aml_fe_dev *dev)
{
	struct aml_demod_sys sys;
	struct aml_demod_i2c i2c;

	pr_dbg("AML Demod DVB-T init\r\n");

	memset(&sys, 0, sizeof(sys));
	memset(&i2c, 0, sizeof(i2c));
	memset(&demod_status, 0, sizeof(demod_status));
	i2c.tuner = dev->drv->id;
	i2c.addr = dev->i2c_addr;
	// 0 -DVBC, 1-DVBT, ISDBT, 2-ATSC
	demod_status.dvb_mode = M6_Dvbt_Isdbt;
	sys.adc_clk=Adc_Clk_24M;
	sys.demod_clk=Demod_Clk_60M;
	demod_status.ch_if=Si2176_5M_If*1000;
	demod_set_sys(&demod_status, &i2c, &sys);
	return 0;
}


static void m6_demod_atsc_release(struct dvb_frontend *fe)
{
	struct aml_fe_dev *state = fe->demodulator_priv;

	uninstall_isr(state);

	kfree(state);
}

static int m6_demod_atsc_set_qam_mode(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct aml_demod_atsc param;//mode  3:64,  5:256, 7:vsb
	fe_modulation_t mode;
	memset(&param, 0, sizeof(param));
	mode = c->modulation;
	printk("mode is %d\n",mode);
	atsc_qam_set(mode);
	return 0;
}



static int m6_demod_atsc_read_status(struct dvb_frontend *fe, fe_status_t * status)
{
	struct aml_fe *afe = fe->demodulator_priv;
	struct aml_fe_dev *dev = afe->dtv_demod;
//	struct aml_demod_i2c demod_i2c;
//	struct aml_demod_sta demod_sta;
	int ilock;
	unsigned char s=0;
	s = amdemod_atsc_stat_islock(dev);
	if(s==1)
	{
		ilock=1;
		*status = FE_HAS_LOCK|FE_HAS_SIGNAL|FE_HAS_CARRIER|FE_HAS_VITERBI|FE_HAS_SYNC;
	}
	else
	{
		ilock=0;
		*status = FE_TIMEDOUT;
	}
	if(last_lock != ilock){
		pr_error("%s.\n", ilock? "!!  >> LOCK << !!" : "!! >> UNLOCK << !!");
		last_lock = ilock;
	}

	return  0;
}

static int m6_demod_atsc_read_ber(struct dvb_frontend *fe, u32 * ber)
{
//	struct aml_fe *afe = fe->demodulator_priv;
//	struct aml_fe_dev *dev = afe->dtv_demod;
//	struct aml_demod_sts demod_sts;
//	struct aml_demod_i2c demod_i2c;
//	struct aml_demod_sta demod_sta;

// check_atsc_fsm_status();
	return 0;
}

static int m6_demod_atsc_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct aml_fe *afe = fe->demodulator_priv;
	struct aml_fe_dev *dev = afe->dtv_demod;

	*strength=tuner_get_ch_power(dev);
	return 0;
}

static int m6_demod_atsc_read_snr(struct dvb_frontend *fe, u16 * snr)
{
//	struct aml_fe *afe = fe->demodulator_priv;
//	struct aml_fe_dev *dev = afe->dtv_demod;

//	struct aml_demod_sts demod_sts;
//	struct aml_demod_i2c demod_i2c;
//	struct aml_demod_sta demod_sta;

//	* snr=check_atsc_fsm_status();
	return 0;
}

static int m6_demod_atsc_read_ucblocks(struct dvb_frontend *fe, u32 * ucblocks)
{
	*ucblocks=0;
	return 0;
}

static int m6_demod_atsc_set_frontend(struct dvb_frontend *fe)
{
//	struct amlfe_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct aml_demod_atsc param;
//	struct aml_demod_sta demod_sta;
//	struct aml_demod_sts demod_sts;
	struct aml_demod_i2c demod_i2c;
	int error,times;
	struct aml_fe *afe = fe->demodulator_priv;
	struct aml_fe_dev *dev = afe->dtv_demod;

	demod_i2c.tuner=dev->drv->id;
	demod_i2c.addr=dev->i2c_addr;
	times = 2;

	memset(&param, 0, sizeof(param));
	param.ch_freq = c->frequency/1000;

	last_lock = -1;
	//p->u.vsb.modulation=QAM_64;
	atsc_mode=c->modulation;
   // param.mode = amdemod_qam(p->u.vsb.modulation);
   	param.mode=c->modulation;

retry:
	aml_dmx_before_retune(AM_TS_SRC_TS2, fe);
	aml_fe_analog_set_frontend(fe);
	atsc_set_ch(&demod_status, &demod_i2c, &param);

	/*{
		int ret;
		ret = wait_event_interruptible_timeout(dev->lock_wq, amdemod_atsc_stat_islock(dev), 4*HZ);
		if(!ret)	pr_error("amlfe wait lock timeout.\n");
	}*/
//rsj_debug
	/*	int count;
		for(count=0;count<10;count++){
			if(amdemod_atsc_stat_islock(dev)){
				printk("first lock success\n");
				break;
			}

			msleep(200);
		}	*/

	times--;
	if(amdemod_atsc_stat_islock(dev) && times){
		int lock;

		aml_dmx_start_error_check(AM_TS_SRC_TS2, fe);
		msleep(20);
		error = aml_dmx_stop_error_check(AM_TS_SRC_TS2, fe);
		lock  = amdemod_atsc_stat_islock(dev);
		if((error > 200) || !lock){
			pr_error("amlfe too many error, error count:%d lock statuc:%d, retry\n", error, lock);
			goto retry;
		}
	}

	aml_dmx_after_retune(AM_TS_SRC_TS2, fe);

	afe->params = *c;
//	pr_dbg("AML amldemod => frequency=%d,symbol_rate=%d\r\n",p->frequency,p->u.qam.symbol_rate);
	return  0;

}

static int m6_demod_atsc_get_frontend(struct dvb_frontend *fe)
{//these content will be writed into eeprom .
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct aml_fe *afe = fe->demodulator_priv;
	printk("c->frequency is %d\n",c->frequency);
	*c = afe->params;
	return 0;
}



int M6_Demod_Atsc_Init(struct aml_fe_dev *dev)
{
	struct aml_demod_sys sys;
	struct aml_demod_i2c i2c;

	pr_dbg("AML Demod ATSC init\r\n");

	memset(&sys, 0, sizeof(sys));
	memset(&i2c, 0, sizeof(i2c));
	memset(&demod_status, 0, sizeof(demod_status));
	// 0 -DVBC, 1-DVBT, ISDBT, 2-ATSC
	demod_status.dvb_mode = M6_Atsc;
	sys.adc_clk=Adc_Clk_25_2M;//Adc_Clk_26M;
	sys.demod_clk=Demod_Clk_75M;//Demod_Clk_71M;//Demod_Clk_78M;
	demod_status.ch_if=6350;
	demod_status.tmp=Adc_mode;
	demod_set_sys(&demod_status, &i2c, &sys);;
	return 0;
}


//dtmb 20140106 for m6d

static void m6_demod_dtmb_release(struct dvb_frontend *fe)
{
	struct aml_fe_dev *state = fe->demodulator_priv;

	uninstall_isr(state);

	kfree(state);
}

static int m6_demod_dtmb_read_status(struct dvb_frontend *fe, fe_status_t * status)
{
	struct aml_fe *afe = fe->demodulator_priv;
	struct aml_fe_dev *dev = afe->dtv_demod;
//	struct aml_demod_i2c demod_i2c;
//	struct aml_demod_sta demod_sta;
	int ilock;
	unsigned char s=0;
//	s = amdemod_dtmb_stat_islock(dev);
//	if(s==1)
	s = dtmb_read_snr();
	s = amdemod_dtmb_stat_islock(dev);
//	s=1;
	if(s==1)
	{
		ilock=1;
		*status = FE_HAS_LOCK|FE_HAS_SIGNAL|FE_HAS_CARRIER|FE_HAS_VITERBI|FE_HAS_SYNC;
	}
	else
	{
		ilock=0;
		*status = FE_TIMEDOUT;
	}
	if(last_lock != ilock){
		pr_error("%s.\n", ilock? "!!  >> LOCK << !!" : "!! >> UNLOCK << !!");
		last_lock = ilock;
	}

	return  0;
}

static int m6_demod_dtmb_read_ber(struct dvb_frontend *fe, u32 * ber)
{
//	struct aml_fe *afe = fe->demodulator_priv;
//	struct aml_fe_dev *dev = afe->dtv_demod;
//	struct aml_demod_sts demod_sts;
//	struct aml_demod_i2c demod_i2c;
//	struct aml_demod_sta demod_sta;

// check_atsc_fsm_status();
	int fec_bch_add;
	fec_bch_add = dtmb_read_reg(0xdf);
	*ber=fec_bch_add;
	return 0;
}

static int m6_demod_dtmb_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct aml_fe *afe = fe->demodulator_priv;
	struct aml_fe_dev *dev = afe->dtv_demod;

	*strength=tuner_get_ch_power(dev);
	return 0;
}

static int m6_demod_dtmb_read_snr(struct dvb_frontend *fe, u16 * snr)
{
//	struct aml_fe *afe = fe->demodulator_priv;
//	struct aml_fe_dev *dev = afe->dtv_demod;

	int tmp,snr_avg;
	tmp=snr_avg=0;
	tmp=dtmb_read_reg(0x0e3);
	snr_avg = (tmp >> 13) & 0xfff;
		if(snr_avg >= 2048)
			snr_avg = snr_avg - 4096;
		snr_avg = snr_avg / 32;
	*snr = snr_avg;
	return 0;
}

static int m6_demod_dtmb_read_ucblocks(struct dvb_frontend *fe, u32 * ucblocks)
{
	*ucblocks=0;
	return 0;
}

static int m6_demod_dtmb_set_frontend(struct dvb_frontend *fe)
{

	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct aml_demod_dtmb param;
//	struct aml_demod_sta demod_sta;
//	struct aml_demod_sts demod_sts;
	struct aml_demod_i2c demod_i2c;
	int times;
	struct aml_fe *afe = fe->demodulator_priv;
	struct aml_fe_dev *dev = afe->dtv_demod;

	demod_i2c.tuner=dev->drv->id;
	demod_i2c.addr=dev->i2c_addr;
	times = 2;
	pr_dbg("m6_demod_dtmb_set_frontend,freq is %d\n",c->frequency);
	memset(&param, 0, sizeof(param));
	param.ch_freq = c->frequency/1000;

	last_lock = -1;

//	aml_dmx_before_retune(AM_TS_SRC_TS2, fe);
	aml_fe_analog_set_frontend(fe);
	dtmb_set_ch(&demod_status, &demod_i2c, &param);

	/*{
		int ret;
		ret = wait_event_interruptible_timeout(dev->lock_wq, amdemod_atsc_stat_islock(dev), 4*HZ);
		if(!ret)	pr_error("amlfe wait lock timeout.\n");
	}*/
//rsj_debug
	/*	int count;
		for(count=0;count<10;count++){
			if(amdemod_atsc_stat_islock(dev)){
				printk("first lock success\n");
				break;
			}

			msleep(200);
		}	*/

/*	times--;
	if(amdemod_dtmb_stat_islock(dev) && times){
		int lock;

		aml_dmx_start_error_check(AM_TS_SRC_TS2, fe);
		msleep(20);
		error = aml_dmx_stop_error_check(AM_TS_SRC_TS2, fe);
		lock  = amdemod_dtmb_stat_islock(dev);
		if((error > 200) || !lock){
			pr_error("amlfe too many error, error count:%d lock statuc:%d, retry\n", error, lock);
			goto retry;
		}
	}

	aml_dmx_after_retune(AM_TS_SRC_TS2, fe);*/

	afe->params = *c;
//	pr_dbg("AML amldemod => frequency=%d,symbol_rate=%d\r\n",p->frequency,p->u.qam.symbol_rate);
	return  0;

}

static int m6_demod_dtmb_get_frontend(struct dvb_frontend *fe)
{//these content will be writed into eeprom .
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct aml_fe *afe = fe->demodulator_priv;
	*c = afe->params;
//	pr_dbg("[get frontend]c->frequency is %d\n",c->frequency);
	return 0;
}



int M6_Demod_Dtmb_Init(struct aml_fe_dev *dev)
{
	struct aml_demod_sys sys;
	struct aml_demod_i2c i2c;

	pr_dbg("AML Demod DTMB init\r\n");

	memset(&sys, 0, sizeof(sys));
	memset(&i2c, 0, sizeof(i2c));
	memset(&demod_status, 0, sizeof(demod_status));
	// 0 -DVBC, 1-DVBT, ISDBT, 2-ATSC
	demod_status.dvb_mode = M6_Dtmb;
	sys.adc_clk=Adc_Clk_25M;//Adc_Clk_26M;
	sys.demod_clk=Demod_Clk_100M;
	demod_status.ch_if=Si2176_5M_If;
	demod_status.tmp=Adc_mode;
	demod_set_sys(&demod_status, &i2c, &sys);;
	return 0;
}


static int m6_demod_fe_get_ops(struct aml_fe_dev *dev, int mode, void *ops)
{
	struct dvb_frontend_ops *fe_ops = (struct dvb_frontend_ops*)ops;
	if(mode == AM_FE_OFDM){

	fe_ops->info.frequency_min = 51000000;
	fe_ops->info.frequency_max = 858000000;
	fe_ops->info.frequency_stepsize = 0;
	fe_ops->info.frequency_tolerance = 0;
	fe_ops->info.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK | FE_CAN_QAM_16 |
			FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO |
			FE_CAN_RECOVER |
			FE_CAN_MUTE_TS;
	fe_ops->release = m6_demod_dvbt_release;
	fe_ops->set_frontend = m6_demod_dvbt_set_frontend;
	fe_ops->get_frontend = m6_demod_dvbt_get_frontend;
	fe_ops->read_status = m6_demod_dvbt_read_status;
	fe_ops->read_ber = m6_demod_dvbt_read_ber;
	fe_ops->read_signal_strength = m6_demod_dvbt_read_signal_strength;
	fe_ops->read_snr = m6_demod_dvbt_read_snr;
	fe_ops->read_ucblocks = m6_demod_dvbt_read_ucblocks;

	pr_dbg("=========================dvbt demod init\r\n");
	M6_Demod_Dvbt_Init(dev);
	}
	else if(mode == AM_FE_QAM){
	fe_ops->info.frequency_min = 51000000;
	fe_ops->info.frequency_max = 858000000;
	fe_ops->info.frequency_stepsize = 0;
	fe_ops->info.frequency_tolerance = 0;
	fe_ops->info.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK | FE_CAN_QAM_16 |FE_CAN_QAM_32|FE_CAN_QAM_128|FE_CAN_QAM_256|
			FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO |
			FE_CAN_RECOVER |
			FE_CAN_MUTE_TS;

	fe_ops->release = m6_demod_dvbc_release;
	fe_ops->set_frontend = m6_demod_dvbc_set_frontend;
	fe_ops->get_frontend = m6_demod_dvbc_get_frontend;
	fe_ops->read_status = m6_demod_dvbc_read_status;
	fe_ops->read_ber = m6_demod_dvbc_read_ber;
	fe_ops->read_signal_strength = m6_demod_dvbc_read_signal_strength;
	fe_ops->read_snr = m6_demod_dvbc_read_snr;
	fe_ops->read_ucblocks = m6_demod_dvbc_read_ucblocks;
	fe_ops->set_qam_mode = m6_demod_dvbc_set_qam_mode;

//	init_waitqueue_head(&dev->lock_wq);
	install_isr(dev);
	pr_dbg("=========================dvbc demod init\r\n");
	M6_Demod_Dvbc_Init(dev,Adc_mode);
	}else if(mode == AM_FE_ATSC){

	fe_ops->info.frequency_min = 51000000;
	fe_ops->info.frequency_max = 858000000;
	fe_ops->info.frequency_stepsize = 0;
	fe_ops->info.frequency_tolerance = 0;
	fe_ops->info.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK | FE_CAN_QAM_16 |
			FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO |
			FE_CAN_RECOVER |
			FE_CAN_MUTE_TS;

	fe_ops->release = m6_demod_atsc_release;
	fe_ops->set_frontend = m6_demod_atsc_set_frontend;
	fe_ops->get_frontend = m6_demod_atsc_get_frontend;
	fe_ops->read_status = m6_demod_atsc_read_status;
	fe_ops->read_ber = m6_demod_atsc_read_ber;
	fe_ops->read_signal_strength = m6_demod_atsc_read_signal_strength;
	fe_ops->read_snr = m6_demod_atsc_read_snr;
	fe_ops->read_ucblocks = m6_demod_atsc_read_ucblocks;
	fe_ops->set_qam_mode = m6_demod_atsc_set_qam_mode;
	M6_Demod_Atsc_Init(dev);
	}else if(mode == AM_FE_DTMB){

	fe_ops->info.frequency_min = 51000000;
	fe_ops->info.frequency_max = 900000000;
	fe_ops->info.frequency_stepsize = 0;
	fe_ops->info.frequency_tolerance = 0;
	fe_ops->info.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK | FE_CAN_QAM_16 |
			FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO |
			FE_CAN_RECOVER |
			FE_CAN_MUTE_TS;

	fe_ops->release = m6_demod_dtmb_release;
	fe_ops->set_frontend = m6_demod_dtmb_set_frontend;
	fe_ops->get_frontend = m6_demod_dtmb_get_frontend;
	fe_ops->read_status = m6_demod_dtmb_read_status;
	fe_ops->read_ber = m6_demod_dtmb_read_ber;
	fe_ops->read_signal_strength = m6_demod_dtmb_read_signal_strength;
	fe_ops->read_snr = m6_demod_dtmb_read_snr;
	fe_ops->read_ucblocks = m6_demod_dtmb_read_ucblocks;
	M6_Demod_Dtmb_Init(dev);
	}
	return 0;
}

static int m6_demod_fe_resume(struct aml_fe_dev *dev)
{
	pr_dbg("m6_demod_fe_resume\n");
//	M6_Demod_Dvbc_Init(dev);
	return 0;

}

static int m6_demod_fe_suspend(struct aml_fe_dev *dev)
{
	return 0;
}

static int m6_demod_fe_enter_mode(struct aml_fe *fe, int mode)
{
	autoFlagsTrig = 1;
	/*struct aml_fe_dev *dev=fe->dtv_demod;
	printk("fe->mode is %d",fe->mode);
	if(fe->mode==AM_FE_OFDM){
		M1_Demod_Dvbt_Init(dev);
	}else if(fe->mode==AM_FE_QAM){
		M1_Demod_Dvbc_Init(dev);
	}else if (fe->mode==AM_FE_ATSC){
		M6_Demod_Atsc_Init(dev);
	}*/
	//dvbc_timer_init();
	if(cci_thread){
		if(dvbc_get_cci_task()==1)
			dvbc_create_cci_task();
	}

	memstart = fe->dtv_demod->mem_start;
	mem_buf=(long*)phys_to_virt(memstart);
	return 0;
}

static int m6_demod_fe_leave_mode(struct aml_fe *fe, int mode)
{
//	dvbc_timer_exit();
	if(cci_thread){
		dvbc_kill_cci_task();
	}
	return 0;
}




static struct aml_fe_drv m6_demod_dtv_demod_drv = {
.id         = AM_DTV_DEMOD_M1,
.name       = "AMLDEMOD",
.capability = AM_FE_QPSK|AM_FE_QAM|AM_FE_ATSC|AM_FE_OFDM|AM_FE_DTMB,
.get_ops    = m6_demod_fe_get_ops,
.suspend    = m6_demod_fe_suspend,
.resume     = m6_demod_fe_resume,
.enter_mode = m6_demod_fe_enter_mode,
.leave_mode = m6_demod_fe_leave_mode
};
struct class *m6_clsp;
struct class *m6_para_clsp;

static int __init m6demodfrontend_init(void)
{
	int ret;
	pr_dbg("register m6_demod demod driver\n");
	ret=0;

	mutex_init(&aml_lock);

	m6_clsp = class_create(THIS_MODULE,DEMOD_DEVICE_NAME);
    if(!m6_clsp)
    {
            pr_error("[m6 demod]%s:create class error.\n",__func__);
            return PTR_ERR(m6_clsp);
    }
    ret = class_create_file(m6_clsp, &class_attr_auto_sym);
    if(ret)
            pr_error("[m6 demod]%s create  class file error.\n",__func__);

    ret = class_create_file(m6_clsp, &class_attr_dvbc_para);
    if(ret)
            pr_error("[m6 demod]%s create  class file error.\n",__func__);

	 ret = class_create_file(m6_clsp, &class_attr_dvbc_reg);
    if(ret)
            pr_error("[m6 demod]%s create  class file error.\n",__func__);


	return aml_register_fe_drv(AM_DEV_DTV_DEMOD, &m6_demod_dtv_demod_drv);
}


static void __exit m6demodfrontend_exit(void)
{
	pr_dbg("unregister m6_demod demod driver\n");

	mutex_destroy(&aml_lock);

	class_remove_file(m6_clsp, &class_attr_auto_sym);
	class_remove_file(m6_clsp, &class_attr_dvbc_para);
	class_remove_file(m6_clsp, &class_attr_dvbc_reg);
	class_destroy(m6_clsp);
	aml_unregister_fe_drv(AM_DEV_DTV_DEMOD, &m6_demod_dtv_demod_drv);
}

fs_initcall(m6demodfrontend_init);
module_exit(m6demodfrontend_exit);


MODULE_DESCRIPTION("m6_demod DVB-T/DVB-C Demodulator driver");
MODULE_AUTHOR("RSJ");
MODULE_LICENSE("GPL");


