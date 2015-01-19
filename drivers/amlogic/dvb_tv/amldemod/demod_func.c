//#include "register.h"
//#include "c_arc_pointer_reg.h"
//#include "a9_func.h"
//#include "clk_util.h"
//#include "c_stimulus.h"
//#include "a9_l2_func.h"

#include "demod_func.h"
#include "aml_demod.h"
#include <linux/string.h>
#include <linux/kernel.h>
#include "acf_filter_coefficient.h"
#define M6D


#define pr_dbg(fmt, args...)\
	do{\
		if(debug_demod)\
			printk("FE: " fmt, ## args);\
	}while(0)
#define pr_error(fmt, args...) printk("FE: " fmt, ## args)

MODULE_PARM_DESC(debug_demod, "\n\t\t Enable frontend debug information");
static int debug_demod = 0;
module_param(debug_demod, int, 0644);

//8vsb
static atsc_cfg_t list_8vsb[22] = {
	{0x0733, 0x00, 0},
	{0x0734, 0xff, 0},
	{0x0716, 0x02, 0},		//F06[7] invert spectrum  0x02 0x06
	{0x05e7, 0x00, 0},
	{0x05e8, 0x00, 0},
	{0x0f06, 0x80, 0},
	{0x0f09, 0x04, 0},
	{0x070c, 0x18, 0},
	{0x070d, 0x9d, 0},
	{0x070e, 0x89, 0},
	{0x070f, 0x6a, 0},
	{0x0710, 0x75, 0},
	{0x0711, 0x6f, 0},
	{0x072a, 0x02, 0},
	{0x072c, 0x02, 0},
	{0x090d, 0x03, 0},
	{0x090e, 0x02, 0},
	{0x090f, 0x00, 0},
	{0x0900, 0x01, 0},
	{0x0900, 0x00, 0},
	{0x0f00, 0x01, 0},
	{0x0000, 0x00, 1}};


//64qam
static atsc_cfg_t list_qam64[111] = {
	{0x0900, 0x01, 0},
	{0x0f04, 0x08, 0},
	{0x0f06, 0x80, 0},
	{0x0f07, 0x00, 0},
	{0x0f00, 0xe0, 0},
	{0x0f00, 0xec, 0},
	{0x0001, 0x05, 0},
	{0x0002, 0x61, 0},///0x61 invert spectrum
	{0x0003, 0x3e, 0},
	{0x0004, 0xed, 0},//0x9d
	{0x0005, 0x10, 0},
	{0x0006, 0xc0, 0},
	{0x0007, 0x5c, 0},
	{0x0008, 0x0f, 0},
	{0x0009, 0x4f, 0},
	{0x000a, 0xfc, 0},
	{0x000b, 0x0c, 0},
	{0x000c, 0x6c, 0},
	{0x000d, 0x3a, 0},
	{0x000e, 0x10, 0},
	{0x000f, 0x02, 0},
	{0x0011, 0x00, 0},
	{0x0012, 0xf5, 0},
	{0x0013, 0x74, 0},
	{0x0014, 0xb9, 0},
	{0x0015, 0x1f, 0},
	{0x0016, 0x80, 0},
	{0x0017, 0x1f, 0},
	{0x0018, 0x0f, 0},
	{0x001e, 0x00, 0},
	{0x001f, 0x00, 0},
	{0x0023, 0x03, 0},
	{0x0025, 0x20, 0},
	{0x0026, 0xff, 0},
	{0x0027, 0xff, 0},
	{0x0028, 0xf8, 0},
	{0x0200, 0x20, 0},
	{0x0201, 0x62, 0},
	{0x0202, 0x23, 0},
	{0x0204, 0x19, 0},
	{0x0205, 0x74, 0},
	{0x0206, 0xab, 0},
	{0x0207, 0xff, 0},
	{0x0208, 0xc0, 0},
	{0x0209, 0xff, 0},
	{0x0211, 0xc0, 0},
	{0x0212, 0xb0, 0},
	{0x0213, 0x05, 0},
	{0x0215, 0x08, 0},
	{0x0222, 0xe0, 0},
	{0x0223, 0xf0, 0},
	{0x0226, 0x40, 0},
	{0x0229, 0x23, 0},
	{0x022a, 0x02, 0},
	{0x022c, 0x01, 0},
	{0x022e, 0x01, 0},
	{0x022f, 0x25, 0},
	{0x0230, 0x40, 0},
	{0x0231, 0x01, 0},
	{0x0734, 0xff, 0},
	{0x073a, 0xff, 0},
	{0x073b, 0x04, 0},
	{0x073c, 0x08, 0},
	{0x073d, 0x08, 0},
	{0x073e, 0x01, 0},
	{0x073f, 0xf8, 0},
	{0x0740, 0xf1, 0},
	{0x0741, 0xf3, 0},
	{0x0742, 0xff, 0},
	{0x0743, 0x0f, 0},
	{0x0744, 0x1a, 0},
	{0x0745, 0x16, 0},
	{0x0746, 0x00, 0},
	{0x0747, 0xe3, 0},
	{0x0748, 0xce, 0},
	{0x0749, 0xd4, 0},
	{0x074a, 0x00, 0},
	{0x074b, 0x4b, 0},
	{0x074c, 0x00, 0},
	{0x074d, 0xa2, 0},
	{0x074e, 0x00, 0},
	{0x074f, 0xe6, 0},
	{0x0750, 0x00, 0},
	{0x0751, 0x00, 0},
	{0x0752, 0x01, 0},
	{0x0753, 0x03, 0},
	{0x0400, 0x00, 0},
	{0x0408, 0x04, 0},
	{0x040e, 0xe0, 0},
	{0x0500, 0x02, 0},
	{0x05e7, 0x00, 0},
	{0x05e8, 0x00, 0},
	{0x0f09, 0x18, 0},
	{0x070c, 0x20, 0},
	{0x070d, 0x41, 0},//0x49
	{0x070e, 0x04, 0},//0x37
	{0x070f, 0x00, 0},
	{0x0710, 0x00, 0},
	{0x0711, 0x00, 0},
	{0x0716, 0xf0, 0},
	{0x090f, 0x00, 0},
	{0x0900, 0x01, 1},
	{0x0900, 0x00, 0},
	{0x0001, 0xf5, 0},
	{0x0001, 0xf5, 1},
	{0x0001, 0xf5, 1},
	{0x0001, 0xf5, 1},
	{0x0001, 0xf5, 1},
	{0x0001, 0x05, 0},
	{0x0001, 0x05, 1},
	{0x0000, 0x00, 1}};


// 256qam
static atsc_cfg_t list_qam256[113] = {
	{0x0900, 0x01, 0},
	{0x0f04, 0x08, 0},
	{0x0f06, 0x80, 0},
	{0x0f00, 0xe0, 0},
	{0x0f00, 0xec, 0},
	{0x0001, 0x05, 0},
	{0x0002, 0x01, 0},  //0x09
	{0x0003, 0x2c, 0},
	{0x0004, 0x91, 0},
	{0x0005, 0x10, 0},
	{0x0006, 0xc0, 0},
	{0x0007, 0x5c, 0},
	{0x0008, 0x0f, 0},
	{0x0009, 0x4f, 0},
	{0x000a, 0xfc, 0},
	{0x000b, 0x0c, 0},
	{0x000c, 0x6c, 0},
	{0x000d, 0x3a, 0},
	{0x000e, 0x10, 0},
	{0x000f, 0x02, 0},
	{0x0011, 0x80, 0},
	{0x0012, 0xf5, 0},   //a5
	{0x0013, 0x74, 0},
	{0x0014, 0xb9, 0},
	{0x0015, 0x1f, 0},
	{0x0016, 0x80, 0},
	{0x0017, 0x1f, 0},
	{0x0018, 0x0f, 0},
	{0x001e, 0x00, 0},
	{0x001f, 0x00, 0},
	{0x0023, 0x03, 0},
	{0x0025, 0x20, 0},
	{0x0026, 0xff, 0},
	{0x0027, 0xff, 0},
	{0x0028, 0xf8, 0},
	{0x0200, 0x20, 0},
	{0x0201, 0x62, 0},
	{0x0202, 0x23, 0},
	{0x0204, 0x19, 0},
	{0x0205, 0x76, 0},
	{0x0206, 0xd2, 0},
	{0x0207, 0xff, 0},
	{0x0208, 0xc0, 0},
	{0x0209, 0xff, 0},
	{0x0211, 0xc0, 0},
	{0x0212, 0xb0, 0},
	{0x0213, 0x05, 0},
	{0x0215, 0x08, 0},
	{0x0222, 0xf0, 0},
	{0x0223, 0xff, 0},
	{0x0226, 0x40, 0},
	{0x0229, 0x23, 0},
	{0x022a, 0x02, 0},
	{0x022c, 0x01, 0},
	{0x022e, 0x01, 0},
	{0x022f, 0x05, 0},
	{0x0230, 0x40, 0},
	{0x0231, 0x01, 0},
	{0x0400, 0x02, 0},
	{0x0401, 0x30, 0},
	{0x0402, 0x13, 0},
	{0x0406, 0x06, 0},
	{0x0408, 0x04, 0},
	{0x040e, 0xe0, 0},
	{0x0411, 0x02, 0},
	{0x073a, 0x02, 0},
	{0x073b, 0x09, 0},
	{0x073c, 0x0c, 0},
	{0x073d, 0x08, 0},
	{0x073e, 0xfd, 0},
	{0x073f, 0xf2, 0},
	{0x0740, 0xed, 0},
	{0x0741, 0xf4, 0},
	{0x0742, 0x03, 0},
	{0x0743, 0x15, 0},
	{0x0744, 0x1d, 0},
	{0x0745, 0x15, 0},
	{0x0746, 0xfc, 0},
	{0x0747, 0xde, 0},
	{0x0748, 0xcc, 0},
	{0x0749, 0xd6, 0},
	{0x074a, 0x04, 0},
	{0x074b, 0x4f, 0},
	{0x074c, 0x00, 0},
	{0x074d, 0xa2, 0},
	{0x074e, 0x00, 0},
	{0x074f, 0xe3, 0},
	{0x0750, 0x00, 0},
	{0x0751, 0xfc, 0},
	{0x0752, 0x00, 0},
	{0x0753, 0x03, 0},
	{0x0500, 0x02, 0},
	{0x05e7, 0x00, 0},
	{0x05e8, 0x00, 0},
	{0x0f09, 0x18, 0},
	{0x070c, 0x20, 0},
	{0x070d, 0x49, 0},
	{0x070e, 0x37, 0},
	{0x070f, 0x00, 0},
	{0x0710, 0x00, 0},
	{0x0711, 0x00, 0},
	{0x0716, 0xf0, 0},
	{0x090f, 0x00, 0},
	{0x0900, 0x01, 1},
	{0x0900, 0x00, 0},
	{0x0001, 0xf5, 0},
	{0x0001, 0xf5, 1},
	{0x0001, 0xf5, 1},
	{0x0001, 0xf5, 1},
	{0x0001, 0xf5, 1},
	{0x0001, 0x05, 0},
	{0x0001, 0x05, 1},
	{0x0000, 0x00, 1}};


#if (defined CONFIG_AM_SI2176)
#include "si2176_func.h"
extern struct si2176_device_s *si2176_devp;
extern void si2176_set_frequency(unsigned int freq);
#elif (defined CONFIG_AM_SI2177)
//#include "si2177_func.h"
#include "../../../../hardware/dvb/silabs/drivers/si2177/si2177_func.h"

extern struct si2177_device_s *si2177_devp;
extern void si2177_set_frequency(unsigned int freq);
/*extern int si2177_init(struct i2c_client *si2177, si2177_cmdreplyobj_t *rsp, si2177_common_reply_struct *common_reply);
extern int si2177_configure(struct i2c_client *si2177, si2177_propobj_t *prop, si2177_cmdreplyobj_t *rsp, si2177_common_reply_struct *common_reply);
extern unsigned char si2177_set_property(struct i2c_client *si2177,
                unsigned char   reserved,
                unsigned int    prop,
                unsigned int    data,
                si2177_cmdreplyobj_t *rsp);*/


#endif

#if (defined CONFIG_AM_SI2176)
extern	int si2176_get_strength(void);

int demod_set_tuner(struct aml_demod_sta *demod_sta,
		  struct aml_demod_i2c *demod_i2c,
		  struct aml_tuner_sys *tuner_sys)
{
	unsigned int freq;
	int err_code;
	freq=tuner_sys->ch_freq;
	pr_dbg("freq is %d\n",freq);
	err_code = si2176_init(&si2176_devp->tuner_client,&si2176_devp->si_cmd_reply,&si2176_devp->si_common_reply);
        err_code = si2176_configure(&si2176_devp->tuner_client,&si2176_devp->si_prop,&si2176_devp->si_cmd_reply,&si2176_devp->si_common_reply);
        if(err_code)
        {
                pr_dbg("[si2176..]%s init si2176 error.\n",__func__);
                return err_code;
        }
		si2176_devp->si_prop.dtv_lif_out.amp = tuner_sys->amp;
		si2176_devp->si_prop.dtv_lif_freq.offset = tuner_sys->if_freq;
		si2176_devp->si_prop.dtv_agc_speed.if_agc_speed = tuner_sys->if_agc_speed;
		si2176_devp->si_prop.dtv_mode.bw = tuner_sys->bandwith&0xf;
		si2176_devp->si_prop.dtv_mode.modulation = tuner_sys->mode&0xf;
		si2176_devp->si_prop.dtv_mode.invert_spectrum=SI2176_DTV_MODE_PROP_INVERT_SPECTRUM_NORMAL&0x1;
		pr_dbg("amp is %d,offset is %d,amp+offset is %x,if_agc_speed is %x,mode is %x\n",si2176_devp->si_prop.dtv_lif_out.amp,si2176_devp->si_prop.dtv_lif_out.offset,((si2176_devp->si_prop.dtv_lif_out.offset)+((si2176_devp->si_prop.dtv_lif_out.amp)<<8)),si2176_devp->si_prop.dtv_agc_speed.if_agc_speed,
						((si2176_devp->si_prop.dtv_mode.bw)+((si2176_devp->si_prop.dtv_mode.modulation)<<4)+((si2176_devp->si_prop.dtv_mode.invert_spectrum)<<8)));
		if(si2176_set_property(&si2176_devp->tuner_client,0,0x703,((si2176_devp->si_prop.dtv_mode.bw)+((si2176_devp->si_prop.dtv_mode.modulation)<<4)+((si2176_devp->si_prop.dtv_mode.invert_spectrum)<<8)),&si2176_devp->si_cmd_reply))
					pr_error("[si2176..]%s set dtv_mode error.\n",__func__);
		if(si2176_set_property(&si2176_devp->tuner_client,0,0x707,(si2176_devp->si_prop.dtv_lif_out.offset)+((si2176_devp->si_prop.dtv_lif_out.amp)<<8),&si2176_devp->si_cmd_reply))
					pr_error("[si2176..]%s set dtv lif out amp error.\n",__func__);
		pr_dbg("if is %d\n",si2176_devp->si_prop.dtv_lif_freq.offset);
		if(si2176_set_property(&si2176_devp->tuner_client,0,0x706,si2176_devp->si_prop.dtv_lif_freq.offset,&si2176_devp->si_cmd_reply))
					pr_error("[si2176..]%s set dtv lif out if error.\n",__func__);



		si2176_set_frequency(freq);
		demod_sta->ch_if=tuner_sys->if_freq;
		msleep(tuner_sys->delay);
		tuner_sys->rssi=si2176_get_strength();
		return 0;
}
#elif (defined CONFIG_AM_SI2177)
int demod_set_tuner(struct aml_demod_sta *demod_sta,
		  struct aml_demod_i2c *demod_i2c,
		  struct aml_tuner_sys *tuner_sys)
{
	unsigned int freq;
	int err_code;
	freq=tuner_sys->ch_freq;
	pr_dbg("freq is %d\n",freq);
	err_code = si2177_init(&si2177_devp->tuner_client,&si2177_devp->si_cmd_reply,&si2177_devp->si_common_reply);
        err_code = si2177_configure(&si2177_devp->tuner_client,&si2177_devp->si_prop,&si2177_devp->si_cmd_reply,&si2177_devp->si_common_reply);
        if(err_code)
        {
                pr_error("[si2176..]%s init si2176 error.\n",__func__);
                return err_code;
        }
		si2177_devp->si_prop.dtv_lif_out.amp = tuner_sys->amp;
		si2177_devp->si_prop.dtv_lif_freq.offset = tuner_sys->if_freq;
		pr_dbg("amp is %d,offset is %d,amp+offset is %x\n",si2177_devp->si_prop.dtv_lif_out.amp,si2177_devp->si_prop.dtv_lif_out.offset,((si2177_devp->si_prop.dtv_lif_out.offset)+((si2177_devp->si_prop.dtv_lif_out.amp)<<8)));
		if(si2177_set_property(&si2177_devp->tuner_client,0,0x707,(si2177_devp->si_prop.dtv_lif_out.offset)+((si2177_devp->si_prop.dtv_lif_out.amp)<<8),&si2177_devp->si_cmd_reply))
					pr_error("[si2177..]%s set dtv lif out amp error.\n",__func__);
		pr_dbg("if is %d\n",si2177_devp->si_prop.dtv_lif_freq.offset);
		if(si2177_set_property(&si2177_devp->tuner_client,0,0x706,si2177_devp->si_prop.dtv_lif_freq.offset,&si2177_devp->si_cmd_reply))
					pr_error("[si2177..]%s set dtv lif out if error.\n",__func__);

		si2177_set_frequency(freq);
		demod_sta->ch_if=tuner_sys->if_freq;
		return 0;
}
#else

int demod_set_tuner(struct aml_demod_sta *demod_sta,
		  struct aml_demod_i2c *demod_i2c,
		  struct aml_tuner_sys *tuner_sys){
		return 0;

}


#endif


void demod_set_adc_core_clk(int clk_adc, int clk_dem,int dvb_mode)//dvbt dvbc 28571, 66666
{
    int unit, error;
    demod_dig_clk_t dig_clk_cfg;
    demod_adc_clk_t adc_clk_cfg;
    int pll_m, pll_n, pll_od, div_dem, div_adc;
    int freq_osc, freq_vco, freq_out, freq_dem, freq_adc;
    int freq_dem_act, freq_adc_act, err_tmp, best_err;
    unit = 10000; // 10000 as 1 MHz, 0.1 kHz resolution.
    freq_osc = 24*unit;
    adc_clk_cfg.d32 = 0;
    dig_clk_cfg.d32 = 0;
	div_dem=freq_dem_act=freq_adc=err_tmp=freq_dem=0;
    if (clk_adc > 0) {
        adc_clk_cfg.b.reset = 0;
        adc_clk_cfg.b.pll_pd = 0;    // 1  shutdown
        if (clk_adc < 1000)
            freq_adc = clk_adc*unit;
        else
            freq_adc = clk_adc*unit/1000;
    }
    else {
        adc_clk_cfg.b.pll_pd = 1;
    }

    if (clk_dem > 0) {
        dig_clk_cfg.b.demod_clk_en = 1;
        dig_clk_cfg.b.demod_clk_sel = 3;    //  1  1G
        if(dvb_mode == M6_Atsc){
	//		dig_clk_cfg.b.demod_clk_sel = 1;
		}
        if (clk_dem < 1000)
            freq_dem = clk_dem*unit;
        else
            freq_dem = clk_dem*unit/1000;
    }
    else {
        dig_clk_cfg.b.demod_clk_en = 0;
    }

    error = 1;
    best_err = 100*unit;
    for (pll_m=1; pll_m<512; pll_m++) {
        for (pll_n=1; pll_n<=5; pll_n++) {
            freq_vco = freq_osc * pll_m / pll_n;
            if (freq_vco<750*unit || freq_vco>1500*unit) continue;

            for (pll_od=0; pll_od<3; pll_od++) {
                freq_out = freq_vco / (1<<pll_od);
			//	printk("pll_od is %d\n",pll_od);
                if (freq_out > 800*unit) continue;

				#ifdef M6D
                div_dem = freq_vco / freq_dem;
				#else
                div_dem = freq_out / freq_dem;
				#endif
                if (div_dem==0 || div_dem>127) continue;

                freq_dem_act = freq_out / div_dem;
                err_tmp = freq_dem_act - freq_dem;

                div_adc = freq_out / freq_adc / 2;
                div_adc *= 2;
                if (div_adc==0 || div_adc>31) continue;

                freq_adc_act = freq_out / div_adc;
                if (freq_adc_act-freq_adc > unit/5) continue;

                if (err_tmp >= best_err) continue;

                adc_clk_cfg.b.pll_m = pll_m;
                adc_clk_cfg.b.pll_n = pll_n;
                adc_clk_cfg.b.pll_od = pll_od;
				#ifdef M6D
                adc_clk_cfg.b.pll_xd = div_adc;
                #else
				adc_clk_cfg.b.pll_xd = div_adc-1;//div_adc-1(m6l);
				#endif
                dig_clk_cfg.b.demod_clk_div = div_dem-1;  // 1/75-1
                #ifdef extadc
				if(dvb_mode == M6_Atsc){
					dig_clk_cfg.b.demod_clk_div = 1000000/clk_dem - 1;
					pr_dbg("[ATSC]use ext dmd clk,dig_clk_cfg.b.demod_clk_div is %x\n ",dig_clk_cfg.b.demod_clk_div);
				}
				#endif
                error = 0;
                best_err = err_tmp;
            }
        }
    }

    pll_m = adc_clk_cfg.b.pll_m;
    pll_n = adc_clk_cfg.b.pll_n;
    pll_od = adc_clk_cfg.b.pll_od;
	#ifdef M6D
    div_adc = adc_clk_cfg.b.pll_xd;
	#else
	div_adc = adc_clk_cfg.b.pll_xd + 1;
	#endif
    div_dem = dig_clk_cfg.b.demod_clk_div + 1;

    if (error) {
        pr_error(" ERROR DIG %7d kHz  ADC %7d kHz\n",freq_dem/(unit/1000), freq_adc/(unit/1000));
    }
    else {
      /*  printf("dig_clk_cfg %x \n",dig_clk_cfg.d32);
        printf("adc_clk_cfg %x \n",adc_clk_cfg.d32);
        printf("Whether write configure to IC?");
        scanf("%d", &tmp);
        if (tmp == 1)
        {
        reg.addr = 0xf11041d0; reg.val = dig_clk_cfg.d32;ioctl(fd, AML_DEMOD_SET_REG, &reg);
        reg.addr = 0xf11042a8; reg.val = adc_clk_cfg.d32;ioctl(fd, AML_DEMOD_SET_REG, &reg);
        }*/

	  /*adc_extclk_sel
	  0: crystal
	  1: fclk_div2 = 1GHz
	  2: fclk_div3 = 666M
	  3: fclk_div5 = 400M
	  4: fclk_div7 = 285M
	  5:mp2_clk_out    freq?
	  6: no----------
	  7: no------------

	  */
	  #ifdef extadc
	 //shutdown demodclk to save power
	//	dig_clk_cfg.b.demod_clk_sel = 1;  // 1GHZ
	//	adc_clk_cfg.b.pll_pd = 1;		  //shutdown adc_pll
	//	dig_clk_cfg.b.demod_clk_div = (1000000/clk_dem)-1;// 1GHZ/70M -1
	//	printk("demod_clk_sel is %d,pll_pd is %d, demod_clk_div is %d\n",dig_clk_cfg.b.demod_clk_sel,adc_clk_cfg.b.pll_pd,dig_clk_cfg.b.demod_clk_div);
	  //enable extclk
		dig_clk_cfg.b.adc_extclk_div=(1000000/clk_adc)-1;//27;     1GHZ/35M-1
	    dig_clk_cfg.b.adc_extclk_en=1;					//enable
	    dig_clk_cfg.b.adc_extclk_sel=1;
        dig_clk_cfg.b.use_adc_extclk=1;
		pr_dbg("adc_extclk_en is %d,adc_extclk_div is %d,clk_adc is %d\n",dig_clk_cfg.b.adc_extclk_en,dig_clk_cfg.b.adc_extclk_div,clk_adc);

	#endif
		demod_set_cbus_reg(dig_clk_cfg.d32, ADC_REG6);
		demod_set_cbus_reg(adc_clk_cfg.d32, ADC_REG1);
        pr_dbg("dig_clk_cfg.d32 is %x,adc_clk_cfg.d32 is %x\n",dig_clk_cfg.d32,adc_clk_cfg.d32);
        freq_vco = freq_osc * pll_m / pll_n;
        freq_out = freq_vco / (1<<pll_od);
        freq_dem_act = freq_vco / div_dem;
        freq_adc_act = freq_out / div_adc;

        pr_dbg(" ADC PLL  M %3d   N %3d\n", pll_m, pll_n);
        pr_dbg(" ADC PLL OD %3d  XD %3d\n", pll_od, div_adc);
        pr_dbg(" DIG SRC SEL %2d  DIV %2d\n", 3, div_dem);
        pr_dbg(" DIG %7d kHz %7d kHz\n", freq_dem/(unit/1000), freq_dem_act/(unit/1000));
        pr_dbg(" ADC %7d kHz %7d kHz\n", freq_adc/(unit/1000), freq_adc_act/(unit/1000));
    }
}

void demod_set_cbus_reg(int data, int addr)
{
//	__raw_writel(data,CBUS_REG_ADDR(addr));
	addr=CBUS_REG_ADDR(addr);
	*(volatile unsigned long*)addr = data;

}

unsigned long demod_read_cbus_reg(int addr)
{
//	return __raw_readl(CBUS_REG_ADDR(addr));
	unsigned long tmp;
	addr = CBUS_REG_ADDR(addr);
    tmp = *(volatile unsigned long*)addr;
    return tmp;

}


void demod_set_demod_reg(unsigned long data, unsigned long addr)
{
//	__raw_writel(data,(DEMOD_BASE+addr));
	addr=DEMOD_BASE+addr;
	*(volatile unsigned long*)addr = data;
}

unsigned long demod_read_demod_reg(unsigned long addr)
{
//	return __raw_readl(DEMOD_BASE+addr);
	unsigned long tmp;
	addr = DEMOD_BASE+addr;
    tmp = *(volatile unsigned long*)addr;
    return tmp;
}



void clocks_set_sys_defaults(unsigned char dvb_mode)
{
	demod_cfg0_t cfg0;
	demod_set_cbus_reg(ADC_REG1_VALUE,ADC_REG1);
	demod_set_cbus_reg(ADC_REG2_VALUE,ADC_REG2);
	demod_set_cbus_reg(ADC_REG3_VALUE,ADC_REG3);
	demod_set_cbus_reg(ADC_REG4_VALUE,ADC_REG4);
	demod_set_cbus_reg(ADC_REG5_VALUE,ADC_REG5);
	demod_set_cbus_reg(ADC_REG6_VALUE,ADC_REG6);

	demod_set_demod_reg(DEMOD_REG1_VALUE,DEMOD_REG1);
	demod_set_demod_reg(DEMOD_REG2_VALUE,DEMOD_REG2);
	demod_set_demod_reg(DEMOD_REG3_VALUE,DEMOD_REG3);
	cfg0.b.mode=7;
	cfg0.b.adc_format=1;
	if(dvb_mode==M6_Dvbc)    //// 0 -DVBC, 1-DVBT, ISDBT, 2-ATSC
	{
		cfg0.b.ts_sel=2;
	}else if((dvb_mode==M6_Dvbt_Isdbt)||(dvb_mode==M6_Dtmb)){
		cfg0.b.ts_sel=1;
		cfg0.b.adc_regout=1;
	}else if(dvb_mode==M6_Atsc){
		cfg0.b.ts_sel=4;
	}
	demod_set_demod_reg(cfg0.d32,DEMOD_REG1);
	pr_dbg("0xc8020c00 is %lx,dvb_mode is %d\n",demod_read_demod_reg(0xc00),dvb_mode);

}

void dtmb_write_reg(int reg_addr, int reg_data)
{
	apb_write_reg(DTMB_BASE+(reg_addr<<2),reg_data);
}

unsigned int dtmb_read_reg(int reg_addr)
{
    return apb_read_reg(DTMB_BASE+(reg_addr<<2));
}




void atsc_write_reg(int reg_addr, int reg_data)
{
    apb_write_reg(ATSC_BASE, (reg_addr&0xffff)<<8 | (reg_data&0xff));
}

unsigned long atsc_read_reg(int reg_addr)
{
    unsigned long tmp;

    apb_write_reg(ATSC_BASE+4, (reg_addr&0xffff)<<8);
    tmp = apb_read_reg(ATSC_BASE);

    return tmp&0xff;
}

unsigned long atsc_read_iqr_reg(void)
{
    unsigned long tmp;

    tmp = apb_read_reg(ATSC_BASE+8);
	pr_dbg("[atsc irq] is %lx\n",tmp);
    return tmp&0xffffffff;
}


int atsc_qam_set(fe_modulation_t mode){
	int i,j;
	if(mode==VSB_8)		//5-8vsb, 2-64qam, 4-256qam
	{

	    for (i=0; list_8vsb[i].adr != 0; i++) {
	        if (list_8vsb[i].rw){
	            atsc_read_reg(list_8vsb[i].adr);
			//	msleep(2);
	        }
	        else{
	            atsc_write_reg(list_8vsb[i].adr, list_8vsb[i].dat);
			//	msleep(2);
	        }
	    }
		j=15589;
		pr_dbg("8-vsb mode\n");
	}else if(mode==QAM_64){

	    for (i=0; list_qam64[i].adr != 0; i++) {
	        if (list_qam64[i].rw){
	            atsc_read_reg(list_qam64[i].adr);
				msleep(2);
	        }else{
	            atsc_write_reg(list_qam64[i].adr, list_qam64[i].dat);
				msleep(2);
	        }
	    }
		j=16588;//33177;
		pr_dbg("64qam mode\n");
	}else if(mode==QAM_256){

	    for (i=0; list_qam256[i].adr != 0; i++) {
	        if (list_qam256[i].rw){
	            atsc_read_reg(list_qam256[i].adr);
				msleep(2);
	        }else{
	            atsc_write_reg(list_qam256[i].adr, list_qam256[i].dat);
				msleep(2);
	        }
	    }
		j=15649;//31298;
		pr_dbg("256qam mode\n");
	}else{
		for (i=0; list_qam256[i].adr != 0; i++) {
	        if (list_qam256[i].rw){
	            atsc_read_reg(list_qam256[i].adr);
				msleep(2);
	        }else{
	            atsc_write_reg(list_qam256[i].adr, list_qam256[i].dat);
				msleep(2);
	        }
	    }
		j=15649;//31298;
		pr_dbg("256qam mode\n");

	}
	return j;

}

void atsc_initial(struct aml_demod_sta *demod_sta)
{
    int fc,fs,cr,ck, j;
	fe_modulation_t mode;
	mode=demod_sta->ch_mode;

	j=atsc_qam_set(mode);//set mode

	fs = demod_sta->adc_freq;//KHZ 25200
	fc = demod_sta->ch_if;//KHZ 6350

	cr = (fc*(1<<17)/fs)*(1<<6) ;
	ck = fs*j/10-(1<<25);  //  ck_rate = (f_samp / f_vsb /2 -1)*(1<<25);  	  double f_vsb = 10.76238;// double f_64q = 5.056941;// double f_256q = 5.360537;


	atsc_write_reg(0x070e,cr&0xff);
	atsc_write_reg(0x070d,(cr>>8)&0xff);
	atsc_write_reg(0x070c,(cr>>16)&0xff);

	if(demod_sta->ch_mode==VSB_8){
		atsc_write_reg(0x0711,ck&0xff);
		atsc_write_reg(0x0710,(ck>>8)&0xff);
		atsc_write_reg(0x070f,(ck>>16)&0xff);
	}
	pr_dbg("0x70e is %x, 0x70d is %x, 0x70c is %x\n",cr&0xff,(cr>>8)&0xff,(cr>>16)&0xff);
	pr_dbg("fs is %d(SR),fc is %d(IF),cr is %x,ck is %x\n",fs,fc,cr,ck);
}



int atsc_set_ch(struct aml_demod_sta *demod_sta,
			struct aml_demod_i2c *demod_i2c,
			struct aml_demod_atsc *demod_atsc)
{
	int ret = 0;
	u8 demod_mode;
	u8 bw, sr, ifreq, agc_mode;
	u32 ch_freq;
	bw		 = demod_atsc->bw;
	sr		 = demod_atsc->sr;
	ifreq	 = demod_atsc->ifreq;
	agc_mode = demod_atsc->agc_mode;
	ch_freq  = demod_atsc->ch_freq;
	demod_mode = demod_atsc->dat0;
	demod_sta->ch_mode	 = demod_atsc->mode; // TODO
	demod_sta->agc_mode  = agc_mode;
	demod_sta->ch_freq	 = ch_freq;
	demod_sta->dvb_mode  = demod_mode;
	demod_sta->ch_bw	 = (8-bw)*1000;
  	atsc_initial(demod_sta);
	pr_dbg("ATSC mode\n");
	return ret;
}
#if 0
static dtmb_cfg_t list_dtmb_v1[99] = {
	{0x00000000, 0x01, 0},
	{0x00001000, 0x02, 0},
	{0x00000000, 0x03, 0},
	{0x00000000, 0x04, 0},
	{0x00000000, 0x05, 0},
	{0x00000000, 0x06, 0},
	{0x007fffff, 0x07, 0},
	{0x0000000f, 0x08, 0},
	{0x00003000, 0x09, 0},
	{0x00000001, 0x0a, 0},
	{0x0c403006, 0x0b, 0},
	{0x44444400, 0x0c, 0},
	{0x1412c320, 0x0d, 0},
	{0x00000152, 0x10, 0},
	{0x47080137, 0x11, 0},
	{0x02200a16, 0x12, 0},
	{0x42190190, 0x13, 0},
	{0x7f807f80, 0x14, 0},
	{0x0000199a, 0x15, 0},
	{0x000a1466, 0x18, 0},
	{0x00274217, 0x1a, 0},
	{0x00131036, 0x1b, 1},
	{0x00000396, 0x1c, 0},
	{0x0037f3cc, 0x1d, 0},
	{0x00000029, 0x1e, 0},
	{0x0004f031, 0x1f, 0},
	{0x00f3cbd4, 0x20, 0},
	{0x0000007e, 0x21, 0},
	{0x23270b6a, 0x22, 0},
	{0x5f700c1b, 0x23, 0},
	{0x00133c2b, 0x24, 0},
	{0x2d3e0f12, 0x25, 0},
	{0x06363038, 0x26, 0},
	{0x060e0a3e, 0x27, 0},
	{0x0015161f, 0x28, 0},
	{0x0809031b, 0x29, 0},
	{0x181c0307, 0x2a, 0},
	{0x051f1a1b, 0x2b, 0},
	{0x00451dce, 0x2c, 0},
	{0x242fde12, 0x2d, 0},
	{0x0034e8fa, 0x2e, 0},
	{0x00000007, 0x30, 0},
	{0x16000d0c, 0x31, 0},
	{0x0000011f, 0x32, 0},
	{0x01000200, 0x33, 0},
	{0x10bbf376, 0x34, 0},
	{0x00000044, 0x35, 0},
	{0x00000000, 0x36, 0},
	{0x00000000, 0x37, 0},
	{0x00000000, 0x38, 0},
	{0x00000000, 0x39, 0},
	{0x00000031, 0x3a, 0},
	{0x4d6b0a58, 0x3b, 0},
	{0x00000c04, 0x3c, 0},
	{0x0d3b0a50, 0x3d, 0},
	{0x03140480, 0x3e, 0},
	{0x05e60452, 0x3f, 0},
	{0x05780400, 0x40, 0},
	{0x0063c025, 0x41, 0},
	{0x05050202, 0x42, 0},
	{0x5e4a0a14, 0x43, 0},
	{0x00003b42, 0x44, 0},
	{0xa53080ff, 0x45, 0},
	{0x00000000, 0x46, 0},
	{0x00133202, 0x47, 0},
	{0x01f00000, 0x48, 0},
	{0x00000000, 0x49, 0},
	{0x00000000, 0x4a, 0},
	{0x00000000, 0x4b, 0},
	{0x00000000, 0x4c, 0},
	{0x20405dc8, 0x4d, 0},
	{0x00000000, 0x4e, 0},
	{0x1f0205df, 0x4f, 0},
	{0x00001120, 0x50, 0},
	{0x4f190803, 0x51, 0},
	{0x00000000, 0x52, 0},
	{0x00000040, 0x53, 0},
	{0x00100050, 0x54, 0},
	{0x00cd1000, 0x55, 0},
	{0x00010fab, 0x56, 0},
	{0x03f0fc3f, 0x58, 0},
	{0x02005014, 0x59, 0},
	{0x01405014, 0x5a, 0},
	{0x00014284, 0x5b, 0},
	{0x00000320, 0x5c, 0},
	{0x14130e05, 0x5d, 0},
	{0x4321c963, 0x5f, 0},
	{0x624668f8, 0x60, 0},
	{0xccc08888, 0x61, 0},
	{0x13212111, 0x62, 0},
	{0x21100000, 0x63, 0},
	{0x624668f8, 0x64, 0},
	{0xccc08888, 0x65, 0},
	{0x13212111, 0x66, 0},
	{0x21100000, 0x67, 0},
	{0x624668f8, 0x68, 0},
	{0xccc08888, 0x69, 0},
	{0x0, 0x0, 0}
};
#endif

extern int memstart;

void dtmb_initial(struct aml_demod_sta *demod_sta)
{

	pr_dbg("[dtmb]mem_buf is 0x%x\n",memstart);
	dtmb_write_reg(0x049, memstart);		//only for init
	dtmb_write_reg(0x010, 0x52);
	dtmb_write_reg(0x047, 0x33202);  //20 bits, 1 - fpga. 0 - m6tvd
	dtmb_write_reg(0xd, 0x141a0320); // increase interleaver0 waiting time.
    dtmb_write_reg(0xc, 0x41444400); // shorten che waiting time.
    dtmb_write_reg(0x18,0x000a1316); // shorten mobile detect time.
    dtmb_write_reg(0x15,0x0199999A); // shift -5M.
	dtmb_write_reg(0x2f,0x13064263); // speed up src
#if 0
	int i;
	  for (i=0; list_dtmb_v1[i].adr != 0; i++) {
	        if (list_dtmb_v1[i].rw){
	            apb_read_reg(DTMB_BASE+((list_dtmb_v1[i].adr)<<2));
			//	msleep(2);
	        }
	        else{
	            apb_write_reg(DTMB_BASE+((list_dtmb_v1[i].adr)<<2), list_dtmb_v1[i].dat);
			//	msleep(2);
	        }
	    }
#endif


}

int dtmb_information(void)
{
	int tps;
	tps = dtmb_read_reg(0xe5);
	pr_dbg("¡¾FSM ¡¿: %x %x %x %x  \n",dtmb_read_reg(0xd7),dtmb_read_reg(0xd6),dtmb_read_reg(0xd5),dtmb_read_reg(0xd4));
	pr_dbg("¡¾AGC ¡¿: agc_power %d, dagc_power %3d, dagc_gain %3d mobi_det_power %d \n",
		(-(((dtmb_read_reg(0xd9) >> 22) & 0x1ff) / 16)),
		((dtmb_read_reg(0xda) >> 0) & 0x3f),
		((dtmb_read_reg(0xda) >> 8) & 0x3ff),
		(dtmb_read_reg(0xf1)>>8) & 0x7ffff);
	pr_dbg("¡¾TPS ¡¿ SC or MC %2d,f_r %2d qam_nr %2d intlv %2d cr %2d constl %2d,\n",
		(dtmb_read_reg(0xe6) >> 24) & 0x1,
		(tps >> 22) &0x1,
		(tps >> 21) &0x1,
		(tps >> 20) &0x1,
		(tps >> 18) &0x3,
		(tps >> 16) &0x3);

	return 0;

}


int dtmb_read_snr(void){
	int tmp,che_snr,snr,snr_avg,fec_lock,reg_46;
	int fsm_state,fec_ldpc_it_avg,local_state,fbe_in_num,SC_mode,time_eq;
	int time_cnt=0,fec_bch_add;
	int ddc_phase,icfo_phase,fcfo_phase,ddc_phase_new,reg_6b;
	int mobi_det_power;
    int mobile_times;
    int ctrl_che_working_state,pm_change,constell;
	mobile_times = 0;
	pm_change = 0;

	tmp=dtmb_read_reg(0x0e3);
	fec_ldpc_it_avg = dtmb_read_reg(0xdd) & 0xffff;
	fec_bch_add = dtmb_read_reg(0xdf);
	fec_lock = (tmp >> 12) & 0x1;
	che_snr = tmp & 0xfff;
    if(che_snr >= 2048)
        che_snr = che_snr - 4096;
	snr = che_snr/32;
    snr_avg = (tmp >> 13) & 0xfff;
    if(snr_avg >= 2048)
        snr_avg = snr_avg - 4096;
    snr_avg = snr_avg / 32;
    dtmb_information();
	pr_dbg("[dtmb] snr is %d,snr_avg is %d,fec_lock is %d,fec_bch_add is %d,fec_ldpc_it_avg is %d\n",snr,snr_avg,fec_lock,fec_bch_add,fec_ldpc_it_avg/256);
	ctrl_che_working_state = (dtmb_read_reg(0xf1) >> 28) & 0x3;
    if(((snr_avg < 1  && ctrl_che_working_state == 0) || (snr < 1  && ctrl_che_working_state == 1)) && fec_lock) { // false lock, reset
//	if((snr_avg<=0)&&(fec_lock==1)){
		pr_dbg("reset dtmb\n");
		dtmb_reset();
		return 0;
	}else{
		 if(fec_lock && fec_ldpc_it_avg < (3 * 256)){
			 pr_dbg("-----------  lock ! ------------ \n");
			 return 1;
		 }else{
			fsm_state = dtmb_read_reg(0xd7) & 0xf;
            local_state = 2;
			time_cnt=0;
			 while(fsm_state < 8 && time_cnt < 10) {// state change to pm
                msleep(50);
                fsm_state = dtmb_read_reg(0xd7) & 0xf;
                time_cnt++;
                local_state = 3;
                pr_dbg("*************** local_state = %d ************ \n", local_state);
            }

			 if(fsm_state >= 8) { // check whether SC and two path mode
		     	  fbe_in_num = (dtmb_read_reg(0xe4) >> 16) & 0x3ff;  // two path distance
		     	  SC_mode = (dtmb_read_reg(0xe6) >> 24) & 0x1;
		     	  time_eq = (dtmb_read_reg(0x46) >> 16) & 0x1;
		     	  fec_ldpc_it_avg = dtmb_read_reg(0xdd) & 0xffff;

			  	  mobi_det_power = (dtmb_read_reg(0xf1)>>8) & 0x7ffff;

             	  if(mobi_det_power > 10) {
             	      mobile_times = 8;
             	      dtmb_write_reg(0x46, (dtmb_read_reg(0x46) & 0xfffffff9) + (1 << 1)); // set mobile mode
             	   //   dtmb_write_reg(0x50, 0x1241);
             	  }
             	  else {
             	      mobile_times -= 1;
             	      if(mobile_times <= 0) {
             	          dtmb_write_reg(0x46, (dtmb_read_reg(0x46) & 0xfffffff9)); // set static mode
             	       //   dtmb_write_reg(0x50, 0x1120);
             	          mobile_times = 0;
             	      }
             	  }

				  if((fbe_in_num > 360) && (pm_change == 0)) {// two path mode, test distance.
             	      dtmb_write_reg(0x42, 0x03030202);
             	      dtmb_write_reg(0x5c, 0x00000000);
             	      pm_change = 1;
             	  }
             	  else if((fbe_in_num <= 360) && (pm_change == 1)){
             	      dtmb_write_reg(0x42, 0x05050202);
             	      dtmb_write_reg(0x5c, 0x00000320);
             	      pm_change = 0;
             	  }


		     	  local_state = 4;
		     	  pr_dbg("*************** local_state = %d ************ \n", local_state);
		     	  if(mobile_times > 0)
                         pr_dbg("***************  mobile state ************ \n");
		     	  if(time_eq){  // in time_eq mode
		     	      local_state = 5;
		     	      pr_dbg("*************** local_state = %d ************ \n", local_state);

		     	      if(SC_mode == 1 || fbe_in_num < 30){ // MC mode or not two path mode, restore normal mode
		     	          dtmb_write_reg(0x2e, 0x131a747d);  // cancel timing-loop
		     	          dtmb_write_reg(0xd, 0x141a0320); // increase interleaver0 waiting time.
		     	          reg_46 = dtmb_read_reg(0x46);
		                reg_46 = reg_46 &~ ((1<<20) + (1<<16));           // bypass fe and set time_eq
		                dtmb_write_reg(0x46, reg_46);
		                pr_dbg(" ------------ normal mode -------------\n");
		                local_state = 6;
		                pr_dbg("*************** local_state = %d ************ \n", local_state);

		     	      }
		        }
		        else {
		            local_state = 7;
		            pr_dbg("*************** local_state = %d ************ \n", local_state);
		            constell = (dtmb_read_reg(0xe5) >> 16) & 0x3;
		            if((SC_mode == 0) && (fbe_in_num > 30) && (fec_ldpc_it_avg > 640)&& (constell > 1)/*2.5*256*/) { // switch to time_eq mode
		                ddc_phase  = dtmb_read_reg(0x15) & 0x1ffffff;
		                icfo_phase = dtmb_read_reg(0xe0) & 0xfffff;
		                fcfo_phase = dtmb_read_reg(0xe1) & 0xfffff;
		                if(icfo_phase > (1<<19))
		                    icfo_phase -= (1<<20);
		                if(fcfo_phase > (1<<19))
		                    fcfo_phase -= (1<<20);
		                ddc_phase_new = ddc_phase + icfo_phase*4 + fcfo_phase;
		                //printf("before is DDC_phase %x, now is %x \n", ddc_phase, ddc_phase_new);
		                dtmb_write_reg(0x15, ddc_phase_new);

		                //printf("switch to time_eq configure : 0 -- No, 1 -- yes");
		                //scanf("%d", &tmp);
		                tmp = 1;
		                if(tmp == 1) {
		                    dtmb_write_reg(0x2e, 0x31a747d);  // start timing-loop
		                    dtmb_write_reg(0x0d, 0x14400640);  // delay fec sync time
		                    reg_46 = dtmb_read_reg(0x46);
		                    reg_46 = reg_46 | ((1<<20) + (1<<16));           // bypass fe and set time_eq
		                    dtmb_write_reg(0x46, reg_46);

		                    reg_6b = dtmb_read_reg(0x6b);
		                    reg_6b = (reg_6b &~(0x3<<16)) | (1<<17);           // set tune auto
		                    dtmb_write_reg(0x6b, reg_6b);
		                }
		                pr_dbg(" ------------ time_eq mode -------------\n");
		                local_state = 8;
		                pr_dbg("*************** local_state = %d ************ \n", local_state);
		                dtmb_reset();
						msleep(300);
	                        }
	                    }
	                }else if(time_cnt >=10) // don't sync, all reset
                        {
                            local_state = 9;
                            pr_dbg("*************** local_state = %d ************ \n", local_state);

                            dtmb_register_reset();
                        //    dtmb_write_reg(0x49,memstart); //set memory
                            dtmb_write_reg(0x10,0x52); //set memory
                            dtmb_write_reg(0xd, 0x141a0320); // increase interleaver0 waiting time.
                            dtmb_write_reg(0xc, 0x41444400); // shorten che waiting time.
                            dtmb_write_reg(0x47,0x33202);
							dtmb_write_reg(0x18,0x000a1316); // shorten mobile detect time.
				            dtmb_write_reg(0x15,0x0199999A); // shift -5M.
				            dtmb_write_reg(0x2f,0x13064263); // speed up src
				            pm_change = 0;
                        }

		 }

	}

	return 0;

}



void dtmb_reset(void)
{
	dtmb_write_reg(0x01, dtmb_read_reg(0x01) | (0x3 << 0));
	dtmb_write_reg(0x01, dtmb_read_reg(0x01) &~ (0x3 << 0));
}

void dtmb_register_reset()
{
    dtmb_write_reg(0x01, dtmb_read_reg(0x01) | (1 << 0));
    dtmb_write_reg(0x01, dtmb_read_reg(0x01) &~ (1 << 0));
}




int dtmb_set_ch(struct aml_demod_sta *demod_sta,
			struct aml_demod_i2c *demod_i2c,
			struct aml_demod_dtmb *demod_dtmb)
{
	int ret = 0;
	u8 demod_mode;
	u8 bw, sr, ifreq, agc_mode;
	u32 ch_freq;
	bw		 = demod_dtmb->bw;
	sr		 = demod_dtmb->sr;
	ifreq	 = demod_dtmb->ifreq;
	agc_mode = demod_dtmb->agc_mode;
	ch_freq  = demod_dtmb->ch_freq;
	demod_mode = demod_dtmb->dat0;
	demod_sta->ch_mode	 = demod_dtmb->mode; // TODO
	demod_sta->agc_mode  = agc_mode;
	demod_sta->ch_freq	 = ch_freq;
	demod_sta->dvb_mode  = demod_mode;
	demod_sta->ch_bw	 = (8-bw)*1000;
  	dtmb_initial(demod_sta);
	pr_dbg("DTMB mode\n");
	return ret;
}



/*int dvbc_set_ch(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c,
		struct aml_demod_dvbc *demod_dvbc)
{
    int ret = 0;
    u16 symb_rate;
    u8  mode;
    u32 ch_freq;

   printk("f=%d, s=%d, q=%d\n", demod_dvbc->ch_freq, demod_dvbc->symb_rate, demod_dvbc->mode);

    mode      = demod_dvbc->mode;
    symb_rate = demod_dvbc->symb_rate;
    ch_freq   = demod_dvbc->ch_freq;
	demod_i2c->tuner=7;
    if (mode > 4) {
	printk("Error: Invalid QAM mode option %d\n", mode);
	mode = 2;
       	ret = -1;
    }

    if (symb_rate<1000 || symb_rate>7000) {
	printk("Error: Invalid Symbol Rate option %d\n", symb_rate);
	symb_rate = 6875;
	ret = -1;
    }

    if (ch_freq<1000 || ch_freq>900000) {
	printk("Error: Invalid Channel Freq option %d\n", ch_freq);
	ch_freq = 474000;
	ret = -1;
    }

    // if (ret != 0) return ret;

    // Set DVB-C
  //  (*DEMOD_REG0) &= ~1;

    demod_sta->dvb_mode  = 0;    // 0:dvb-c, 1:dvb-t
    demod_sta->ch_mode   = mode; // 0:16, 1:32, 2:64, 3:128, 4:256
    demod_sta->agc_mode  = 1;    // 0:NULL, 1:IF, 2:RF, 3:both
    demod_sta->ch_freq   = ch_freq;
    demod_sta->tuner     = demod_i2c->tuner;

    if(demod_i2c->tuner == 1)
        demod_sta->ch_if     = 36130; // TODO  DCT tuner
    else if (demod_i2c->tuner == 2)
        demod_sta->ch_if     = 4570; // TODO  Maxlinear tuner
    else if (demod_i2c->tuner == 7)
        demod_sta->ch_if     = 5000; // TODO  Si2176 tuner

    demod_sta->ch_bw     = 8000;  // TODO
    demod_sta->symb_rate = symb_rate;

    // Set Tuner
    tuner_set_ch(demod_sta, demod_i2c);

 //   mdelay((demod_sta->ch_freq % 10) * 1000);
	qam_initial(mode);
  //  dvbc_reg_initial(demod_sta);

    return ret;
}  */



int dvbt_set_ch(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c,
		struct aml_demod_dvbt *demod_dvbt)
{
    int ret = 0;
    u8_t demod_mode = 1;
    u8_t bw, sr, ifreq, agc_mode;
    u32_t ch_freq;

    bw       = demod_dvbt->bw;
    sr       = demod_dvbt->sr;
    ifreq    = demod_dvbt->ifreq;
    agc_mode = demod_dvbt->agc_mode;
    ch_freq  = demod_dvbt->ch_freq;
    demod_mode = demod_dvbt->dat0;
    if (ch_freq<1000 || ch_freq>900000000) {
	//printk("Error: Invalid Channel Freq option %d\n", ch_freq);
	ch_freq = 474000;
	ret = -1;
    }

    if (demod_mode<0||demod_mode>4) {
    	//printk("Error: Invalid demod mode option %d\n", demod_mode);
    	//printk("Note: 0 is QAM, 1 is DVBT , 2 is ISDBT, 3 is DTMB, 4 is ATSC\n");
    	demod_mode = 1;
    	ret = -1;
    }

    //demod_sta->dvb_mode  = 1;
    demod_sta->ch_mode   = 0; // TODO
    demod_sta->agc_mode  = agc_mode;
    demod_sta->ch_freq   = ch_freq;
    demod_sta->dvb_mode  = demod_mode;
 /*   if (demod_i2c->tuner == 1)
	demod_sta->ch_if = 36130;
    else if (demod_i2c->tuner == 2)
	demod_sta->ch_if = 4570;
    else if (demod_i2c->tuner == 3)
	demod_sta->ch_if = 4000;// It is nouse.(alan)
    else if (demod_i2c->tuner == 7)
	demod_sta->ch_if = 5000;//silab 5000kHz IF*/


    demod_sta->ch_bw     = (8-bw)*1000;
    demod_sta->symb_rate = 0; // TODO

//	bw=0;
    demod_mode=1;
    //for si2176 IF:5M   sr 28.57
    sr=4;ifreq=4;
	if(bw==BANDWIDTH_AUTO){
		demod_mode=2;
	}
    ofdm_initial(
  		  bw            ,// 00:8M 01:7M 10:6M 11:5M
     		sr            ,// 00:45M 01:20.8333M 10:20.7M 11:28.57  100:24m
     		ifreq         ,// 000:36.13M 001:-5.5M 010:4.57M 011:4M 100:5M
     		demod_mode-1    ,// 00:DVBT,01:ISDBT
     		1        // 0: Unsigned, 1:TC
    	) ;
    pr_dbg("DVBT/ISDBT mode\n");


    return ret;
}

extern int clk_measure(char index);
int demod_set_sys(struct aml_demod_sta *demod_sta,
		  struct aml_demod_i2c *demod_i2c,
		  struct aml_demod_sys *demod_sys)
{
	int adc_clk;
//	demod_sta->tmp=Adc_mode;
    unsigned char dvb_mode;
    int clk_adc,clk_dem;
    dvb_mode=demod_sta->dvb_mode;
    clk_adc=demod_sys->adc_clk;
	clk_dem=demod_sys->demod_clk;
	pr_dbg("demod_set_sys,clk_adc is %d,clk_demod is %d,demod_sta.tmp is %d\n",clk_adc,clk_dem,demod_sta->tmp);
    clocks_set_sys_defaults(dvb_mode);
	if(demod_sta->tmp==Adc_mode){
		demod_set_cbus_reg(ADC_REG4_VALUE,ADC_REG4);
		pr_dbg("Adc_mode\n");
	}
	else{
		demod_set_cbus_reg(ADC_REG4_CRY_VALUE,ADC_REG4);
		pr_dbg("Cry_mode\n");
	}
    demod_set_adc_core_clk(clk_adc, clk_dem,dvb_mode);
	demod_sta->adc_freq=clk_adc;
	demod_sta->clk_freq=clk_dem;
	adc_clk=clk_measure(17);
	adc_clk=clk_measure(56);

    return 0;
}

void demod_set_reg(struct aml_demod_reg *demod_reg)
{
	 if(demod_reg->mode==0){
		demod_reg->addr=demod_reg->addr+QAM_BASE;
	}else if((demod_reg->mode==1)||((demod_reg->mode==2))){
		demod_reg->addr=demod_reg->addr*4+DVBT_BASE;
	}else if(demod_reg->mode==3){
	//	demod_reg->addr=ATSC_BASE;
	}
	else if(demod_reg->mode==4){
		demod_reg->addr=demod_reg->addr*4+DEMOD_CFG_BASE;
	}else if(demod_reg->mode==5){
		demod_reg->addr=demod_reg->addr+DEMOD_BASE;
	//	printk("DEMOD_BASE    	0xf1100000+addr*4 \n ISDBT_BASE 	  	0xf1100000+addr*4\n QAM_BASE	  	0xf1100400+addr\n DEMOD_CFG_BASE	0xf1100c00+addr\n");
	}else if(demod_reg->mode==7){


	}
	if(demod_reg->mode==3)
		atsc_write_reg(demod_reg->addr, demod_reg->val);
	//	apb_write_reg(demod_reg->addr, (demod_reg->val&0xffff)<<8 | (demod_reg->val&0xff));
	else
   		apb_write_reg(demod_reg->addr, demod_reg->val);
}


void demod_get_reg(struct aml_demod_reg *demod_reg)
{
    if(demod_reg->mode==0){
		demod_reg->addr=demod_reg->addr+QAM_BASE;
	}else if((demod_reg->mode==1)||(demod_reg->mode==2)){
		demod_reg->addr=demod_reg->addr*4+DVBT_BASE;
	}else if(demod_reg->mode==3){
	//	demod_reg->addr=demod_reg->addr+ATSC_BASE;
	}else if(demod_reg->mode==4){
		demod_reg->addr=demod_reg->addr*4+DEMOD_CFG_BASE;
	}else if(demod_reg->mode==5){
		demod_reg->addr=demod_reg->addr+DEMOD_BASE;
	//	printk("DEMOD_BASE    	0xf1100000+addr*4 \n ISDBT_BASE 	  	0xf1100000+addr*4\n QAM_BASE	  	0xf1100400+addr\n DEMOD_CFG_BASE	0xf1100c00+addr\n");
	}else if(demod_reg->mode==7){


	}

	if(demod_reg->mode==3){
		demod_reg->val=atsc_read_reg(demod_reg->addr);
	//	apb_write_reg(ATSC_BASE+4, (demod_reg->addr&0xffff)<<8);
	//	demod_reg->val = apb_read_reg(ATSC_BASE)&0xff;
	}else if(demod_reg->mode==6){
		demod_reg->val=atsc_read_iqr_reg();
	//	apb_write_reg(ATSC_BASE+4, (demod_reg->addr&0xffff)<<8);
	//	demod_reg->val = apb_read_reg(ATSC_BASE)&0xff;
	}
	else
   		demod_reg->val = apb_read_reg(demod_reg->addr);
}


void demod_reset()
{
    Wr(RESET0_REGISTER, (1<<8));
}

void demod_set_irq_mask()
{
 (*(volatile unsigned long *)(P_SYS_CPU_0_IRQ_IN4_INTR_MASK)) |= (1 << 8);
}

void demod_clr_irq_stat()
{
   (*(volatile unsigned long *)(P_SYS_CPU_0_IRQ_IN4_INTR_STAT)) |= (1 << 8);
}

void demod_set_adc_core_clk_quick(int clk_adc_cfg, int clk_dem_cfg)
{
 /*   volatile unsigned long *demod_dig_clk = P_HHI_DEMOD_CLK_CNTL;
    volatile unsigned long *demod_adc_clk = P_HHI_ADC_PLL_CNTL;
    int unit;
    demod_dig_clk_t dig_clk_cfg;
    demod_adc_clk_t adc_clk_cfg;
    int pll_m, pll_n, pll_od, div_dem, div_adc;
    int freq_osc, freq_vco, freq_out, freq_dem_act, freq_adc_act;

    unit = 10000; // 10000 as 1 MHz, 0.1 kHz resolution.
    freq_osc = 25*unit;

    adc_clk_cfg.d32 = clk_adc_cfg;
    dig_clk_cfg.d32 = clk_dem_cfg;

    pll_m = adc_clk_cfg.b.pll_m;
    pll_n = adc_clk_cfg.b.pll_n;
    pll_od = adc_clk_cfg.b.pll_od;
    div_adc = adc_clk_cfg.b.pll_xd;
    div_dem = dig_clk_cfg.b.demod_clk_div + 1;

    *demod_dig_clk = dig_clk_cfg.d32;
    *demod_adc_clk = adc_clk_cfg.d32;

    freq_vco = freq_osc * pll_m / pll_n;
    freq_out = freq_vco / (1<<pll_od);
    freq_dem_act = freq_out / div_dem;
    freq_adc_act = freq_out / div_adc;*/

    //printk(" ADC PLL  M %3d   N %3d\n", pll_m, pll_n);
    //printk(" ADC PLL OD %3d  XD %3d\n", pll_od, div_adc);
    //printk(" DIG SRC SEL %2d  DIV %2d\n", 3, div_dem);
    //printk(" DIG %7d kHz ADC %7d kHz\n",
               //       freq_dem_act/(unit/1000), freq_adc_act/(unit/1000));
}

/*void demod_set_adc_core_clk(int clk_adc, int clk_dem)
{
    volatile unsigned long *demod_dig_clk = P_HHI_DEMOD_CLK_CNTL;
    volatile unsigned long *demod_adc_clk = P_HHI_ADC_PLL_CNTL;
    int unit, error;
    demod_dig_clk_t dig_clk_cfg;
    demod_adc_clk_t adc_clk_cfg;
    int pll_m, pll_n, pll_od, div_dem, div_adc;
    int freq_osc, freq_vco, freq_out, freq_dem, freq_adc;
    int freq_dem_act, freq_adc_act, err_tmp, best_err;

    unit = 10000; // 10000 as 1 MHz, 0.1 kHz resolution.
    freq_osc = 25*unit;
    adc_clk_cfg.d32 = 0;
    dig_clk_cfg.d32 = 0;

    if (clk_adc > 0) {
        adc_clk_cfg.b.reset = 0;
        adc_clk_cfg.b.pll_pd = 0;
        if (clk_adc < 1000)
            freq_adc = clk_adc*unit;
        else
            freq_adc = clk_adc*unit/1000;
    }
    else {
        adc_clk_cfg.b.pll_pd = 1;
    }

    if (clk_dem > 0) {
        dig_clk_cfg.b.demod_clk_en = 1;
        dig_clk_cfg.b.demod_clk_sel = 3;
        if (clk_dem < 1000)
            freq_dem = clk_dem*unit;
        else
            freq_dem = clk_dem*unit/1000;
    }
    else {
        dig_clk_cfg.b.demod_clk_en = 0;
    }

    error = 1;
    best_err = 100*unit;
    for (pll_m=1; pll_m<512; pll_m++) {
        for (pll_n=1; pll_n<=5; pll_n++) {
            freq_vco = freq_osc * pll_m / pll_n;
            if (freq_vco<750*unit || freq_vco>1500*unit) continue;

            for (pll_od=0; pll_od<3; pll_od++) {
                freq_out = freq_vco / (1<<pll_od);
                if (freq_out > 800*unit) continue;

                div_dem = freq_out / freq_dem;
                if (div_dem==0 || div_dem>127) continue;

                freq_dem_act = freq_out / div_dem;
                err_tmp = freq_dem_act - freq_dem;

                div_adc = freq_out / freq_adc / 2;
                div_adc *= 2;
                if (div_adc==0 || div_adc>31) continue;

                freq_adc_act = freq_out / div_adc;
                if (freq_adc_act-freq_adc > unit/5) continue;

                if (err_tmp >= best_err) continue;

                adc_clk_cfg.b.pll_m = pll_m;
                adc_clk_cfg.b.pll_n = pll_n;
                adc_clk_cfg.b.pll_od = pll_od;
                adc_clk_cfg.b.pll_xd = div_adc;
                dig_clk_cfg.b.demod_clk_div = div_dem - 1;

                error = 0;
                best_err = err_tmp;
            }
        }
    }

    pll_m = adc_clk_cfg.b.pll_m;
    pll_n = adc_clk_cfg.b.pll_n;
    pll_od = adc_clk_cfg.b.pll_od;
    div_adc = adc_clk_cfg.b.pll_xd;
    div_dem = dig_clk_cfg.b.demod_clk_div + 1;

    if (error) {
        //printk(" ERROR DIG %7d kHz  ADC %7d kHz\n",
                    //      freq_dem/(unit/1000), freq_adc/(unit/1000));
    }
    else {
        *demod_dig_clk = dig_clk_cfg.d32;
        *demod_adc_clk = adc_clk_cfg.d32;

        freq_vco = freq_osc * pll_m / pll_n;
        freq_out = freq_vco / (1<<pll_od);
        freq_dem_act = freq_out / div_dem;
        freq_adc_act = freq_out / div_adc;

        //printk(" ADC PLL  M %3d   N %3d\n", pll_m, pll_n);
        //printk(" ADC PLL OD %3d  XD %3d\n", pll_od, div_adc);
        //printk(" DIG SRC SEL %2d  DIV %2d\n", 3, div_dem);
        //printk(" DIG %7d kHz %7d kHz\n",
//                          freq_dem/(unit/1000), freq_dem_act/(unit/1000));
        //printk(" ADC %7d kHz %7d kHz\n",
                  //        freq_adc/(unit/1000), freq_adc_act/(unit/1000));
    }
}*/

void apb_write_reg(int addr, int data)
{
    *(volatile unsigned long*)addr = data;
//	printk("[write]addr is %x,data is %x\n",addr,data);
}

unsigned long apb_read_reg(int addr)
{
    unsigned long tmp;

    tmp = *(volatile unsigned long*)addr;
//	printk("[read]addr is %x,data is %x\n",addr,tmp);
    return tmp;
}

void apb_write_regb(int addr, int index, int data)
{
    unsigned long tmp;

    tmp = *(volatile unsigned *)addr;
    tmp &= ~(1<<index);
    tmp |= (data<<index);
    *(volatile unsigned *)addr = tmp;
}

void enable_qam_int(int idx)
{
    unsigned long mask;

    mask = apb_read_reg(QAM_BASE+0xd0);
    mask |= (1<<idx);
    apb_write_reg(QAM_BASE+0xd0, mask);
}

void disable_qam_int(int idx)
{
    unsigned long mask;

    mask = apb_read_reg(QAM_BASE+0xd0);
    mask &= ~(1<<idx);
    apb_write_reg(QAM_BASE+0xd0, mask);
}

char *qam_int_name[] = {"      ADC",
			"   Symbol",
			"       RS",
			" In_Sync0",
			" In_Sync1",
			" In_Sync2",
			" In_Sync3",
			" In_Sync4",
			"Out_Sync0",
			"Out_Sync1",
			"Out_Sync2",
			"Out_Sync3",
			"Out_Sync4",
			"In_SyncCo",
			"OutSyncCo",
			"  In_Dagc",
			" Out_Dagc",
			"  Eq_Mode",
			"RS_Uncorr"};

#define OFDM_INT_STS         (volatile unsigned long *)(DVBT_BASE+4*0x0d)
#define OFDM_INT_EN          (volatile unsigned long *)(DVBT_BASE+4*0x0e)

void enable_ofdm_int(int ofdm_irq)
{
    // clear ofdm/xxx status
    (*OFDM_INT_STS) &= ~(1<<ofdm_irq);
    // enable ofdm/xxx irq
    (*OFDM_INT_EN) |= (1<<ofdm_irq);
}

void disable_ofdm_int(int ofdm_irq)
{
    unsigned long tmp;

    // disable ofdm/xxx irq
    tmp = (*OFDM_INT_EN);
    tmp &= ~(1<<ofdm_irq);
    (*OFDM_INT_EN) = tmp;
    // clear ofdm/xxx status
    (*OFDM_INT_STS) &= ~(1<<ofdm_irq);
}

char *ofdm_int_name[] = {"PFS_FCFO",
			 "PFS_ICFO",
			 " CS_FCFO",
			 " PFS_SFO",
			 " PFS_TPS",
			 "      SP",
			 "     CCI",
			 "  Symbol",
			 " In_Sync",
			 "Out_Sync",
			 "FSM Stat"};

unsigned long read_ofdm_int(void)
{
    unsigned long stat, mask, tmp;
    int idx;
    char buf[80];

    // read ofdm/xxx status
    tmp = (*OFDM_INT_STS);
    mask = (*OFDM_INT_EN);
    stat = tmp & mask;

    for (idx=0; idx<11; idx++) {
	if (stat>>idx&1) {
	    strcpy(buf, "OFDM ");
	    strcat(buf, ofdm_int_name[idx]);
	    strcat(buf, " INT %d    STATUS   %x");
	    //printk(buf, idx, stat);
	}
    }
    // clear ofdm/xxx status
    (*OFDM_INT_STS) = 0;

    return stat;
}

#define PHS_LOOP_OPEN

void qam_read_all_regs(void)
{
    int i, addr;
    unsigned long tmp;

    for (i=0; i<0xf0; i+=4) {
        addr = QAM_BASE + i;
	tmp = *(volatile unsigned *)addr;
//	pr_dbg("QAM addr 0x%02x  value 0x%08x\n", i, tmp);
    }
}

void  ini_icfo_pn_index (int mode) // 00:DVBT,01:ISDBT
{
    if (mode == 0)
    {
       apb_write_reg(DVBT_BASE+0x3f8, 0x00000031);apb_write_reg(DVBT_BASE+0x3fc, 0x00030000);
       apb_write_reg(DVBT_BASE+0x3f8, 0x00000032);apb_write_reg(DVBT_BASE+0x3fc, 0x00057036);
       apb_write_reg(DVBT_BASE+0x3f8, 0x00000033);apb_write_reg(DVBT_BASE+0x3fc, 0x0009c08d);
       apb_write_reg(DVBT_BASE+0x3f8, 0x00000034);apb_write_reg(DVBT_BASE+0x3fc, 0x000c90c0);
       apb_write_reg(DVBT_BASE+0x3f8, 0x00000035);apb_write_reg(DVBT_BASE+0x3fc, 0x001170ff);
       apb_write_reg(DVBT_BASE+0x3f8, 0x00000036);apb_write_reg(DVBT_BASE+0x3fc, 0x0014d11a);
    }
    else if (mode == 1)
    {
       apb_write_reg(DVBT_BASE+0x3f8, 0x00000031);apb_write_reg(DVBT_BASE+0x3fc, 0x00085046);
       apb_write_reg(DVBT_BASE+0x3f8, 0x00000032);apb_write_reg(DVBT_BASE+0x3fc, 0x0019a0e9);
       apb_write_reg(DVBT_BASE+0x3f8, 0x00000033);apb_write_reg(DVBT_BASE+0x3fc, 0x0024b1dc);
       apb_write_reg(DVBT_BASE+0x3f8, 0x00000034);apb_write_reg(DVBT_BASE+0x3fc, 0x003b3313);
       apb_write_reg(DVBT_BASE+0x3f8, 0x00000035);apb_write_reg(DVBT_BASE+0x3fc, 0x0048d409);
       apb_write_reg(DVBT_BASE+0x3f8, 0x00000036);apb_write_reg(DVBT_BASE+0x3fc, 0x00527509);
    }
}

void tfd_filter_coff_ini(void)
{
    int i;
    int coef[] = {
        0xf900, 0xfe00, 0x0000, 0x0000, 0x0100, 0x0100, 0x0000, 0x0000,
        0xfd00, 0xf700, 0x0000, 0x0000, 0x4c00, 0x0000, 0x0000, 0x0000,
        0x2200, 0x0c00, 0x0000, 0x0000, 0xf700, 0xf700, 0x0000, 0x0000,
        0x0300, 0x0900, 0x0000, 0x0000, 0x0600, 0x0600, 0x0000, 0x0000,
        0xfc00, 0xf300, 0x0000, 0x0000, 0x2e00, 0x0000, 0x0000, 0x0000,
        0x3900, 0x1300, 0x0000, 0x0000, 0xfa00, 0xfa00, 0x0000, 0x0000,
        0x0100, 0x0200, 0x0000, 0x0000, 0xf600, 0x0000, 0x0000, 0x0000,
        0x0700, 0x0700, 0x0000, 0x0000, 0xfe00, 0xfb00, 0x0000, 0x0000,
        0x0900, 0x0000, 0x0000, 0x0000, 0x3200, 0x1100, 0x0000, 0x0000,
        0x0400, 0x0400, 0x0000, 0x0000, 0xfe00, 0xfb00, 0x0000, 0x0000,
        0x0e00, 0x0000, 0x0000, 0x0000, 0xfb00, 0xfb00, 0x0000, 0x0000,
        0x0100, 0x0200, 0x0000, 0x0000, 0xf400, 0x0000, 0x0000, 0x0000,
        0x3900, 0x1300, 0x0000, 0x0000, 0x1700, 0x1700, 0x0000, 0x0000,
        0xfc00, 0xf300, 0x0000, 0x0000, 0x0c00, 0x0000, 0x0000, 0x0000,
        0x0300, 0x0900, 0x0000, 0x0000, 0xee00, 0x0000, 0x0000, 0x0000,
        0x2200, 0x0c00, 0x0000, 0x0000, 0x2600, 0x2600, 0x0000, 0x0000,
        0xfd00, 0xf700, 0x0000, 0x0000, 0x0200, 0x0000, 0x0000, 0x0000,
        0xf900, 0xfe00, 0x0000, 0x0000, 0x0400, 0x0b00, 0x0000, 0x0000,
        0xf900, 0x0000, 0x0000, 0x0000, 0x0700, 0x0200, 0x0000, 0x0000,
        0x2100, 0x2100, 0x0000, 0x0000, 0x0200, 0x0700, 0x0000, 0x0000,
        0xf900, 0x0000, 0x0000, 0x0000, 0x0b00, 0x0400, 0x0000, 0x0000,
        0xfe00, 0xf900, 0x0000, 0x0000, 0x0200, 0x0000, 0x0000, 0x0000,
        0xf700, 0xfd00, 0x0000, 0x0000, 0x2600, 0x2600, 0x0000, 0x0000,
        0x0c00, 0x2200, 0x0000, 0x0000, 0xee00, 0x0000, 0x0000, 0x0000,
        0x0900, 0x0300, 0x0000, 0x0000, 0x0c00, 0x0000, 0x0000, 0x0000,
        0xf300, 0xfc00, 0x0000, 0x0000, 0x1700, 0x1700, 0x0000, 0x0000,
        0x1300, 0x3900, 0x0000, 0x0000, 0xf400, 0x0000, 0x0000, 0x0000,
        0x0200, 0x0100, 0x0000, 0x0000, 0xfb00, 0xfb00, 0x0000, 0x0000,
        0x0e00, 0x0000, 0x0000, 0x0000, 0xfb00, 0xfe00, 0x0000, 0x0000,
        0x0400, 0x0400, 0x0000, 0x0000, 0x1100, 0x3200, 0x0000, 0x0000,
        0x0900, 0x0000, 0x0000, 0x0000, 0xfb00, 0xfe00, 0x0000, 0x0000,
        0x0700, 0x0700, 0x0000, 0x0000, 0xf600, 0x0000, 0x0000, 0x0000,
        0x0200, 0x0100, 0x0000, 0x0000, 0xfa00, 0xfa00, 0x0000, 0x0000,
        0x1300, 0x3900, 0x0000, 0x0000, 0x2e00, 0x0000, 0x0000, 0x0000,
        0xf300, 0xfc00, 0x0000, 0x0000, 0x0600, 0x0600, 0x0000, 0x0000,
        0x0900, 0x0300, 0x0000, 0x0000, 0xf700, 0xf700, 0x0000, 0x0000,
        0x0c00, 0x2200, 0x0000, 0x0000, 0x4c00, 0x0000, 0x0000, 0x0000,
        0xf700, 0xfd00, 0x0000, 0x0000, 0x0100, 0x0100, 0x0000, 0x0000,
        0xfe00, 0xf900, 0x0000, 0x0000, 0x0b00, 0x0400, 0x0000, 0x0000,
        0xfc00, 0xfc00, 0x0000, 0x0000, 0x0200, 0x0700, 0x0000, 0x0000,
        0x4200, 0x0000, 0x0000, 0x0000, 0x0700, 0x0200, 0x0000, 0x0000,
        0xfc00, 0xfc00, 0x0000, 0x0000, 0x0400, 0x0b00, 0x0000, 0x0000};

    for (i=0; i<336; i++) {
        apb_write_reg(DVBT_BASE+0x99*4, (i<<16) | coef[i]);
        apb_write_reg(DVBT_BASE+0x03*4, (1<<12));
    }
}

/*int dvbt_check_status(int)
{
		int i;
		unsigned int status;
		int signal=0;
		char status_num[8];
		status=apb_read_reg(DVBT_BASE+(0x2a<<2);
		for(i=0;i<8;i++){
			status_num[i]=status<<i&0xf;
			if(status_num[i]>3){
				signal=1;
				printk("dvbt have signal \n");
			}else{
				signal=0;
				printk("dvbt have no signal \n");
			}
		}



}*/


extern int memstart;
void ofdm_initial(
    int bandwidth, // 00:8M 01:7M 10:6M 11:5M
    int samplerate,// 00:45M 01:20.8333M 10:20.7M 11:28.57 100: 24.00
    int IF,        // 000:36.13M 001:-5.5M 010:4.57M 011:4M 100:5M
    int mode,       // 00:DVBT,01:ISDBT
    int tc_mode     // 0: Unsigned, 1:TC
)
{
    int tmp;
    int ch_if;
    int adc_freq;
	printk("[ofdm_initial]bandwidth is %d,samplerate is %d,IF is %d, mode is %d,tc_mode is %d\n",bandwidth,samplerate,IF,mode,tc_mode);
    switch(IF)
    {
        case 0: ch_if = 36130;break;
        case 1: ch_if = -5500;break;
        case 2: ch_if = 4570;break;
        case 3: ch_if = 4000;break;
        case 4: ch_if = 5000;break;
        default: ch_if = 4000;break;
    }
    switch(samplerate)
    {
        case 0: adc_freq = 45000;break;
        case 1: adc_freq = 20833;break;
        case 2: adc_freq = 20700;break;
        case 3: adc_freq = 28571;break;
		case 4: adc_freq = 24000;break;
        default: adc_freq = 28571;break;
    }

    apb_write_reg(DVBT_BASE+(0x02<<2), 0x00800000);  // SW reset bit[23] ; write anything to zero
    apb_write_reg(DVBT_BASE+(0x00<<2), 0x00000000);

    apb_write_reg(DVBT_BASE+(0xe<<2), 0xffff); // enable interrupt

    if (mode == 0) { // DVBT
       switch (samplerate)
       {
         case 0 : apb_write_reg(DVBT_BASE+(0x08<<2), 0x00005a00);break;  // 45MHz
         case 1 : apb_write_reg(DVBT_BASE+(0x08<<2), 0x000029aa);break;  // 20.833
         case 2 : apb_write_reg(DVBT_BASE+(0x08<<2), 0x00002966);break;  // 20.7   SAMPLERATE*512
         case 3 : apb_write_reg(DVBT_BASE+(0x08<<2), 0x00003924);break;  // 28.571
         case 4 : apb_write_reg(DVBT_BASE+(0x08<<2), 0x00003000);break;  // 24
         default: apb_write_reg(DVBT_BASE+(0x08<<2), 0x00003924);break;  // 28.571
       }
    }
    else { //ISDBT
       switch (samplerate)
       {
         case 0 : apb_write_reg(DVBT_BASE+(0x08<<2), 0x0000580d);break;  // 45MHz
         case 1 : apb_write_reg(DVBT_BASE+(0x08<<2), 0x0000290d);break;  // 20.833 = 56/7 * 20.8333 / (512/63)*512
         case 2 : apb_write_reg(DVBT_BASE+(0x08<<2), 0x000028da);break;  // 20.7
         case 3 : apb_write_reg(DVBT_BASE+(0x08<<2), 0x0000383F);break;  // 28.571  3863
         case 4 : apb_write_reg(DVBT_BASE+(0x08<<2), 0x00002F40);break;  // 24
         default: apb_write_reg(DVBT_BASE+(0x08<<2), 0x00003863);break;  // 28.571
       }
    }
//	memstart=0x93900000;
	printk("memstart is %x \n",memstart);
    apb_write_reg(DVBT_BASE+(0x10<<2), memstart);//0x8f300000

    apb_write_reg(DVBT_BASE+(0x14<<2), 0xe81c4ff6);   // AGC_TARGET 0xf0121385

    switch(samplerate)
    {
       case 0 : apb_write_reg(DVBT_BASE+(0x15<<2), 0x018c2df2); break;
       case 1 : apb_write_reg(DVBT_BASE+(0x15<<2), 0x0185bdf2); break;
       case 2 : apb_write_reg(DVBT_BASE+(0x15<<2), 0x0185bdf2); break;
       case 3 : apb_write_reg(DVBT_BASE+(0x15<<2), 0x0187bdf2); break;
       case 4 : apb_write_reg(DVBT_BASE+(0x15<<2), 0x0187bdf2); break;
       default: apb_write_reg(DVBT_BASE+(0x15<<2), 0x0187bdf2); break;
    }
    if (tc_mode == 1) apb_write_regb(DVBT_BASE+(0x15<<2),11,0);// For TC mode. Notice, For ADC input is Unsigned, For Capture Data, It is TC.
    apb_write_regb(DVBT_BASE+(0x15<<2),26,1); // [19:0] = [I , Q], I is high, Q is low. This bit is swap I/Q.

    apb_write_reg(DVBT_BASE+(0x16<<2), 0x00047f80);  // AGC_IFGAIN_CTRL
    apb_write_reg(DVBT_BASE+(0x17<<2), 0x00027f80);  // AGC_RFGAIN_CTRL
    apb_write_reg(DVBT_BASE+(0x18<<2), 0x00000190);  // AGC_IFGAIN_ACCUM
    apb_write_reg(DVBT_BASE+(0x19<<2), 0x00000190);  // AGC_RFGAIN_ACCUM
    if (ch_if <0) ch_if += adc_freq;
    if (ch_if > adc_freq) ch_if -= adc_freq;

    tmp = ch_if * (1<<15) / adc_freq;
    apb_write_reg(DVBT_BASE + (0x20<<2), tmp);

    apb_write_reg(DVBT_BASE+(0x21<<2), 0x001ff000);  // DDC CS_FCFO_ADJ_CTRL
    apb_write_reg(DVBT_BASE+(0x22<<2), 0x00000000);  // DDC ICFO_ADJ_CTRL
    apb_write_reg(DVBT_BASE+(0x23<<2), 0x00004000);  // DDC TRACK_FCFO_ADJ_CTRL

    apb_write_reg(DVBT_BASE+(0x27<<2), (1<<23)|(3<<19)|(3<<15)|(1000<<4)|9); // {8'd0,1'd1,4'd3,4'd3,11'd50,4'd9});//FSM_1
    apb_write_reg(DVBT_BASE+(0x28<<2), (100<<13)|1000);//{8'd0,11'd40,13'd50});//FSM_2
	apb_write_reg(DVBT_BASE+(0x29<<2), (31<<20)|(1<<16)|(24<<9)|(3<<6)|20);//{5'd0,7'd127,1'd0,3'd0,7'd24,3'd5,6'd20});

    if (mode == 0) { // DVBT

        if (bandwidth==0) { // 8M
            switch(samplerate)
            {
               case 0: ini_acf_iireq_src_45m_8m();  apb_write_reg(DVBT_BASE+(0x44<<2), 0x004ebf2e); break;// 45M
               case 1: ini_acf_iireq_src_207m_8m(); apb_write_reg(DVBT_BASE+(0x44<<2), 0x00247551); break;//20.833M
               case 2: ini_acf_iireq_src_207m_8m(); apb_write_reg(DVBT_BASE+(0x44<<2), 0x00243999); break;//20.7M
               case 3: ini_acf_iireq_src_2857m_8m();apb_write_reg(DVBT_BASE+(0x44<<2), 0x0031ffcd); break;//28.57M
               case 4: ini_acf_iireq_src_24m_8m(); apb_write_reg(DVBT_BASE+(0x44<<2), 0x002A0000); break;//24M
               default: ini_acf_iireq_src_2857m_8m();apb_write_reg(DVBT_BASE+(0x44<<2), 0x0031ffcd); break;//28.57M
            }
        }
        else if (bandwidth==1) { // 7M
            switch(samplerate)
            {
               case 0: ini_acf_iireq_src_45m_7m();  apb_write_reg(DVBT_BASE+(0x44<<2), 0x0059ff10); break;// 45M
               case 1: ini_acf_iireq_src_207m_7m(); apb_write_reg(DVBT_BASE+(0x44<<2), 0x0029aaa6); break;//20.833M
               case 2: ini_acf_iireq_src_207m_7m(); apb_write_reg(DVBT_BASE+(0x44<<2), 0x00296665); break;//20.7M
               case 3: ini_acf_iireq_src_2857m_7m();apb_write_reg(DVBT_BASE+(0x44<<2), 0x00392491); break;//28.57M
               case 4: ini_acf_iireq_src_24m_7m(); apb_write_reg(DVBT_BASE+(0x44<<2), 0x00300000); break;//24M
               default: ini_acf_iireq_src_2857m_7m();apb_write_reg(DVBT_BASE+(0x44<<2), 0x00392491); break;//28.57M
            }
        }
        else if (bandwidth==2) { // 6M
            switch(samplerate)
            {
               case 0: ini_acf_iireq_src_45m_6m();  apb_write_reg(DVBT_BASE+(0x44<<2), 0x00690000); break;// 45M
               case 1: ini_acf_iireq_src_207m_6m(); apb_write_reg(DVBT_BASE+(0x44<<2), 0x00309c3e); break;//20.833M
               case 2: ini_acf_iireq_src_207m_6m(); apb_write_reg(DVBT_BASE+(0x44<<2), 0x002eaaaa); break;//20.7M
               case 3: ini_acf_iireq_src_2857m_6m();apb_write_reg(DVBT_BASE+(0x44<<2), 0x0042AA69); break;//28.57M
               case 4: ini_acf_iireq_src_24m_6m();apb_write_reg(DVBT_BASE+(0x44<<2), 0x00380000); break;//24M
               default: ini_acf_iireq_src_2857m_6m();apb_write_reg(DVBT_BASE+(0x44<<2), 0x0042AA69); break;//28.57M
            }
        }
        else {// 5M
            switch(samplerate)
            {
               case 0: ini_acf_iireq_src_45m_5m();  apb_write_reg(DVBT_BASE+(0x44<<2), 0x007dfbe0); break;// 45M
               case 1: ini_acf_iireq_src_207m_5m(); apb_write_reg(DVBT_BASE+(0x44<<2), 0x003a554f); break;//20.833M
               case 2: ini_acf_iireq_src_207m_5m(); apb_write_reg(DVBT_BASE+(0x44<<2), 0x0039f5c0); break;//20.7M
               case 3: ini_acf_iireq_src_2857m_5m();apb_write_reg(DVBT_BASE+(0x44<<2), 0x004FFFFE); break;//28.57M
               case 4: ini_acf_iireq_src_24m_5m(); apb_write_reg(DVBT_BASE+(0x44<<2), 0x00433333); break;//24M
               default: ini_acf_iireq_src_2857m_5m();apb_write_reg(DVBT_BASE+(0x44<<2), 0x004FFFFE); break;//28.57M
            }
        }
    }
    else // ISDBT
    {
        switch(samplerate)
        {
           case 0: ini_acf_iireq_src_45m_6m();  apb_write_reg(DVBT_BASE+(0x44<<2), 0x00589800);break;// 45M     SampleRate/(symbolRate)*2^20, symbolRate = 512/63 for isdbt
           case 1: ini_acf_iireq_src_207m_6m(); apb_write_reg(DVBT_BASE+(0x44<<2), 0x002903d4);break;// 20.833M
           case 2: ini_acf_iireq_src_207m_6m(); apb_write_reg(DVBT_BASE+(0x44<<2), 0x00280ccc);break;// 20.7M
           case 3: ini_acf_iireq_src_2857m_6m();apb_write_reg(DVBT_BASE+(0x44<<2), 0x00383fc8);break;// 28.57M
           case 4: ini_acf_iireq_src_24m_6m();apb_write_reg(DVBT_BASE+(0x44<<2), 0x002F4000); break;//24M
           default: ini_acf_iireq_src_2857m_6m();apb_write_reg(DVBT_BASE+(0x44<<2), 0x00383fc8);break;//28.57M
        }
    }

    if (mode == 0)// DVBT
        apb_write_reg(DVBT_BASE+(0x02<<2), (bandwidth<<20)|0x10002);
    else          // ISDBT
        apb_write_reg(DVBT_BASE+(0x02<<2), (1<<20)|0x1001a);//{0x000,2'h1,20'h1_001a});    // For ISDBT , bandwith should be 1,

    apb_write_reg(DVBT_BASE+(0x45<<2), 0x00000000);  // SRC SFO_ADJ_CTRL
 	  apb_write_reg(DVBT_BASE+(0x46<<2), 0x02004000);  // SRC SFO_ADJ_CTRL
    apb_write_reg(DVBT_BASE+(0x48<<2), 0x000c0287);  // DAGC_CTRL1
    apb_write_reg(DVBT_BASE+(0x49<<2), 0x00000005);  // DAGC_CTRL2
    apb_write_reg(DVBT_BASE+(0x4c<<2), 0x00000bbf);  // CCI_RP
    apb_write_reg(DVBT_BASE+(0x4d<<2), 0x00000376);  // CCI_RPSQ
    apb_write_reg(DVBT_BASE+(0x4e<<2), 0x0f0f1d09);  // CCI_CTRL
    apb_write_reg(DVBT_BASE+(0x4f<<2), 0x00000000);  // CCI DET_INDX1
    apb_write_reg(DVBT_BASE+(0x50<<2), 0x00000000);  // CCI DET_INDX2
    apb_write_reg(DVBT_BASE+(0x51<<2), 0x00000000);  // CCI_NOTCH1_A1
    apb_write_reg(DVBT_BASE+(0x52<<2), 0x00000000);  // CCI_NOTCH1_A2
    apb_write_reg(DVBT_BASE+(0x53<<2), 0x00000000);  // CCI_NOTCH1_B1
    apb_write_reg(DVBT_BASE+(0x54<<2), 0x00000000);  // CCI_NOTCH2_A1
    apb_write_reg(DVBT_BASE+(0x55<<2), 0x00000000);  // CCI_NOTCH2_A2
    apb_write_reg(DVBT_BASE+(0x56<<2), 0x00000000);  // CCI_NOTCH2_B1
    apb_write_reg(DVBT_BASE+(0x58<<2), 0x00000885);  // MODE_DETECT_CTRL // 582
    if (mode == 0) //DVBT
    	apb_write_reg(DVBT_BASE+(0x5c<<2), 0x00001011);  //
    else
    	apb_write_reg(DVBT_BASE+(0x5c<<2), 0x00000753);  // ICFO_EST_CTRL ISDBT ICFO thres = 2

    apb_write_reg(DVBT_BASE+(0x5f<<2), 0x0ffffe10);  // TPS_FCFO_CTRL
    apb_write_reg(DVBT_BASE+(0x61<<2), 0x0000006c);  // FWDT ctrl
 	apb_write_reg(DVBT_BASE+(0x68<<2), 0x128c3929);
    apb_write_reg(DVBT_BASE+(0x69<<2), 0x91017f2d);  // 0x1a8
    apb_write_reg(DVBT_BASE+(0x6b<<2), 0x00442211);  // 0x1a8
    apb_write_reg(DVBT_BASE+(0x6c<<2), 0x01fc400a);  // 0x
    apb_write_reg(DVBT_BASE+(0x6d<<2), 0x0030303f);  // 0x
    apb_write_reg(DVBT_BASE+(0x73<<2), 0xffffffff);  // CCI0_PILOT_UPDATE_CTRL
    apb_write_reg(DVBT_BASE+(0x74<<2), 0xffffffff);  // CCI0_DATA_UPDATE_CTRL
    apb_write_reg(DVBT_BASE+(0x75<<2), 0xffffffff);  // CCI1_PILOT_UPDATE_CTRL
    apb_write_reg(DVBT_BASE+(0x76<<2), 0xffffffff);  // CCI1_DATA_UPDATE_CTRL

    tmp = mode==0 ? 0x000001a2 : 0x00000da2;
    apb_write_reg(DVBT_BASE+(0x78<<2), tmp);  // FEC_CTR

    apb_write_reg(DVBT_BASE+(0x7d<<2), 0x0000009d);
    apb_write_reg(DVBT_BASE+(0x7e<<2), 0x00004000);
    apb_write_reg(DVBT_BASE+(0x7f<<2), 0x00008000);


    apb_write_reg(DVBT_BASE+((0x8b+0)<<2), 0x20002000);
    apb_write_reg(DVBT_BASE+((0x8b+1)<<2), 0x20002000);
    apb_write_reg(DVBT_BASE+((0x8b+2)<<2), 0x20002000);
    apb_write_reg(DVBT_BASE+((0x8b+3)<<2), 0x20002000);
    apb_write_reg(DVBT_BASE+((0x8b+4)<<2), 0x20002000);
    apb_write_reg(DVBT_BASE+((0x8b+5)<<2), 0x20002000);
    apb_write_reg(DVBT_BASE+((0x8b+6)<<2), 0x20002000);
    apb_write_reg(DVBT_BASE+((0x8b+7)<<2), 0x20002000);

    apb_write_reg(DVBT_BASE+(0x93<<2), 0x31);
    apb_write_reg(DVBT_BASE+(0x94<<2), 0x00);
    apb_write_reg(DVBT_BASE+(0x95<<2), 0x7f1);
    apb_write_reg(DVBT_BASE+(0x96<<2), 0x20);

    apb_write_reg(DVBT_BASE+(0x98<<2), 0x03f9115a);
    apb_write_reg(DVBT_BASE+(0x9b<<2), 0x000005df);

    apb_write_reg(DVBT_BASE+(0x9c<<2), 0x00100000);// TestBus write valid, 0 is system clk valid
    apb_write_reg(DVBT_BASE+(0x9d<<2), 0x01000000);// DDR Start address
    apb_write_reg(DVBT_BASE+(0x9e<<2), 0x02000000);// DDR End   address

    apb_write_regb(DVBT_BASE+(0x9b<<2),7,0);// Enable Testbus dump to DDR
    apb_write_regb(DVBT_BASE+(0x9b<<2),8,0);// Run Testbus dump to DDR

    apb_write_reg(DVBT_BASE+(0xd6<<2), 0x00000003);
    //apb_write_reg(DVBT_BASE+(0xd7<<2), 0x00000008);
    apb_write_reg(DVBT_BASE+(0xd8<<2), 0x00000120);
    apb_write_reg(DVBT_BASE+(0xd9<<2), 0x01010101);

    ini_icfo_pn_index(mode);
    tfd_filter_coff_ini();

    calculate_cordic_para();
	msleep(1);
 //   delay_us(1);

    apb_write_reg(DVBT_BASE+(0x02<<2), apb_read_reg(DVBT_BASE+(0x02<<2)) | (1 << 0));
    apb_write_reg(DVBT_BASE+(0x02<<2), apb_read_reg(DVBT_BASE+(0x02<<2)) | (1 << 24));

//	dvbt_check_status();

}

void calculate_cordic_para()
{
    apb_write_reg(DVBT_BASE+0x0c, 0x00000040);
}

char *ofdm_fsm_name[] = {"    IDLE",
			 "     AGC",
			 "     CCI",
			 "     ACQ",
			 "    SYNC",
			 "TRACKING",
			 "  TIMING",
			 " SP_SYNC",
			 " TPS_DEC",
			 "FEC_LOCK",
			 "FEC_LOST"};

void check_fsm_state(void)
{
    unsigned long tmp;

    tmp = apb_read_reg(DVBT_BASE+0xa8);
    //printk(">>>>>>>>>>>>>>>>>>>>>>>>> OFDM FSM From %d        to %d\n", tmp>>4&0xf, tmp&0xf);

    if ((tmp&0xf)==3) {
        apb_write_regb(DVBT_BASE+(0x9b<<2),8,1);//Stop dump testbus;
        apb_write_regb(DVBT_BASE+(0x0f<<2),0,1);
        tmp = apb_read_reg(DVBT_BASE+(0x9f<<2));
        //printk(">>>>>>>>>>>>>>>>>>>>>>>>> STOP DUMP DATA To DDR : End Addr %d,Is it overflow?%d\n", tmp>>1, tmp&0x1);
    }

}

void ofdm_read_all_regs()
{
    int i;
    unsigned long tmp;

    for (i=0; i<0xff; i++) {
	tmp = apb_read_reg(DVBT_BASE+0x00+i*4);
	//printk("OFDM Reg (0x%x) is 0x%x\n", i, tmp);
    }

}

static int dvbt_get_status(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c)
{
	return apb_read_reg(DVBT_BASE+0x0)>>12&1;
}


static int dvbt_get_ber(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c)
{
	pr_dbg("[RSJ]per is %lu\n",apb_read_reg(DVBT_BASE+(0xbf<<2)));
	return apb_read_reg(DVBT_BASE+(0xbf<<2));
//	return dvbt_ber();/*unit: 1e-7*/
}

static int dvbt_get_snr(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c)
{
	pr_dbg("2snr is %lu\n",((apb_read_reg(DVBT_BASE+(0x0a<<2)))>>20)&0x3ff);
	return ((apb_read_reg(DVBT_BASE+(0x0a<<2)))>>20)&0x3ff;/*dBm: bit0~bit2=decimal*/
}

static int dvbt_get_strength(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c)
{
//	int dbm = dvbt_get_ch_power(demod_sta, demod_i2c);
//	return dbm;
	return 0;
}

static int dvbt_get_ucblocks(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c)
{
	return 0;
//	return dvbt_get_per();
}

struct demod_status_ops* dvbt_get_status_ops(void)
{
	static struct demod_status_ops ops = {
		.get_status = dvbt_get_status,
		.get_ber = dvbt_get_ber,
		.get_snr = dvbt_get_snr,
		.get_strength = dvbt_get_strength,
		.get_ucblocks = dvbt_get_ucblocks,
	};

	return &ops;
}

int app_apb_read_reg(int addr)
{
	return (int)(apb_read_reg(DVBT_BASE+(addr<<2)));

}

int app_apb_write_reg(int addr,int data)
{
	apb_write_reg((DVBT_BASE+(addr<<2)),data);
	return 0;
}


void monitor_isdbt(void)
{
     int SNR;
     int SNR_SP          = 500;
     int SNR_TPS         = 0;
     int SNR_CP          = 0;
     int timeStamp       = 0;
     int SFO_residual    = 0;
     int SFO_esti        = 0;
     int FCFO_esti       = 0;
     int FCFO_residual   = 0;
     int AGC_Gain        = 0;
     int RF_AGC          = 0;
     int Signal_power    = 0;
     int FECFlag         = 0;
     int EQ_seg_ratio    = 0;
     int tps_0           = 0;
     int tps_1           = 0;
     int tps_2           = 0;

     int time_stamp;
     int SFO;
     int FCFO;
     int timing_adj;
     int RS_CorrectNum;

     int cnt;
     int tmpAGCGain;
	 tmpAGCGain      = 0;
	 cnt = 0;

//     app_apb_write_reg(0x8, app_apb_read_reg(0x8) & ~(1 << 17));  // TPS symbol index update : active high
     time_stamp      =  app_apb_read_reg(0x07)&0xffff       ;
     SNR             =  app_apb_read_reg(0x0a)              ;
     FECFlag         = (app_apb_read_reg(0x00)>>11)&0x3     ;
     SFO             =  app_apb_read_reg(0x47)&0xfff        ;
     SFO_esti        =  app_apb_read_reg(0x60)&0xfff        ;
     FCFO_esti       = (app_apb_read_reg(0x60)>>11)&0xfff   ;
     FCFO            = (app_apb_read_reg(0x26))&0xffffff    ;
     RF_AGC          =  app_apb_read_reg(0x0c)&0x1fff       ;
     timing_adj      =  app_apb_read_reg(0x6f)&0x1fff       ;
     RS_CorrectNum   =  app_apb_read_reg(0xc1)&0xfffff      ;
     Signal_power    = (app_apb_read_reg(0x1b))&0x1ff       ;
     EQ_seg_ratio    =  app_apb_read_reg(0x6e)&0x3ffff      ;
     tps_0           =  app_apb_read_reg(0x64);
     tps_1           =  app_apb_read_reg(0x65);
     tps_2           =  app_apb_read_reg(0x66)&0xf;

     timeStamp       = (time_stamp>>8) * 68 + (time_stamp & 0x7f);
     SFO_residual    = (SFO>0x7ff)? (SFO - 0x1000): SFO;
     FCFO_residual   = (FCFO>0x7fffff)? (FCFO - 0x1000000): FCFO;
     //RF_AGC          = (RF_AGC>0x3ff)? (RF_AGC - 0x800): RF_AGC;
     FCFO_esti       = (FCFO_esti>0x7ff)?(FCFO_esti - 0x1000):FCFO_esti;
     SNR_CP          = (SNR)&0x3ff;
     SNR_TPS         = (SNR>>10)&0x3ff;
     SNR_SP          = (SNR>>20)&0x3ff;
     SNR_SP          = (SNR_SP  > 0x1ff)? SNR_SP - 0x400: SNR_SP;
     SNR_TPS         = (SNR_TPS > 0x1ff)? SNR_TPS- 0x400: SNR_TPS;
     SNR_CP          = (SNR_CP  > 0x1ff)? SNR_CP - 0x400: SNR_CP;
     AGC_Gain        = tmpAGCGain >> 4;
     tmpAGCGain      = (AGC_Gain>0x3ff)?AGC_Gain-0x800:AGC_Gain;
     timing_adj      = (timing_adj > 0xfff)? timing_adj - 0x2000: timing_adj;
     EQ_seg_ratio    = (EQ_seg_ratio > 0x1ffff)? EQ_seg_ratio - 0x40000 : EQ_seg_ratio;

     pr_dbg("T %4x SP %3d TPS %3d CP %3d EQS %8x RSC %4d SFO %4d FCFO %4d Vit %4x Timing %3d SigP %3x FEC %x RSErr %8x ReSyn %x tps %03x%08x"
         ,app_apb_read_reg(0xbf)
         ,SNR_SP
         ,SNR_TPS
         ,SNR_CP
//         ,EQ_seg_ratio
         ,app_apb_read_reg(0x62)
         ,RS_CorrectNum
         ,SFO_residual
         ,FCFO_residual
         ,RF_AGC
         ,timing_adj
         ,Signal_power
         ,FECFlag
         ,app_apb_read_reg(0x0b)
         ,(app_apb_read_reg(0xc0)>>20)&0xff
         ,app_apb_read_reg(0x05)&0xfff
         ,app_apb_read_reg(0x04)
					);
      pr_dbg("\n");


}




int find_2(int data, int *table, int len)
{
	  int end;
	  int index;
	  int start;
	  int cnt=0;
	  start = 0;
	  end   = len;
	  //printf("data is %d\n",data);
	  while ((len > 1)&&(cnt < 10))
	  {
	  	 cnt ++ ;
	  	 index = (len/2);
	  	 if (data > table[start + index])
	  	 	{
	  	 	  start = start + index;
	  	 	  len   = len - index - 1;
	  	 	}
	  	 if (data < table[start + index])
	  	 	{
	  	 	  len   = index + 1;
	  	 	}
	  	 else if (data == table[start+index])
	  	 	{
	  	 		start = start + index;
	  	 	  break;
	  	 	}
	  }
	  return start;
}


int read_atsc_all_reg(void)
{
	return 0;
#if 0
	int i,j,k;
	j=4;
	unsigned long data;
	pr_dbg("system agc is:");		//system agc
	for(i=0xc00;i<=0xc0c;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	j=4;
	for(i=0xc80;i<=0xc87;i++){
		data=atsc_read_reg(i);
		if(j==4){
			printk("\n[addr:0x%x]",i);
		    j=0;
		}
		printk("%02x   ",data);
		j++;
	}
	pr_dbg("\n vsb control is:");		//vsb control
	j=4;
	for(i=0x900;i<=0x905;i++){
		data=atsc_read_reg(i);
		if(j==4){
			printk("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	j=4;
	for(i=0x908;i<=0x912;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		printk("%02x   ",data);
		j++;
	}
	j=4;
	for(i=0x917;i<=0x91b;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	j=4;
	for(i=0x980;i<=0x992;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	pr_dbg("\n vsb demod is:");		//vsb demod
	j=4;
	for(i=0x700;i<=0x711;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	j=4;
	for(i=0x716;i<=0x720;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	j=4;
	for(i=0x722;i<=0x724;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	j=4;
	for(i=0x726;i<=0x72c;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	j=4;
	for(i=0x730;i<=0x732;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	j=4;
	for(i=0x735;i<=0x751;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	j=4;
	for(i=0x780;i<=0x795;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	j=4;
	for(i=0x752;i<=0x755;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	printk("\n vsb equalizer is:");		//vsb equalizer
	j=4;
	for(i=0x501;i<=0x5ff;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	printk("\n vsb fec is:");		//vsb fec
	j=4;
	for(i=0x601;i<=0x601;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	j=4;
	for(i=0x682;i<=0x685;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	printk("\n qam demod is:");		//qam demod
	j=4;
	for(i=0x1;i<=0x1a;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	j=4;
	for(i=0x25;i<=0x28;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	j=4;
	for(i=0x101;i<=0x10b;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	j=4;
	for(i=0x206;i<=0x207;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	printk("\n qam equalize is:");		//qam equalize
	j=4;
	for(i=0x200;i<=0x23d;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	j=4;
	for(i=0x260;i<=0x275;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	printk("\n qam fec is:");		//qam fec
	j=4;
	for(i=0x400;i<=0x418;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	pr_dbg("\n system mpeg formatter is:");		//system mpeg formatter
	j=4;
	for(i=0xf00;i<=0xf09;i++){
		data=atsc_read_reg(i);
		if(j==4){
			pr_dbg("\n[addr:0x%x]",i);
		    j=0;
		}
		pr_dbg("%02x   ",data);
		j++;
	}
	pr_dbg("\n\n");
	return 0;
#endif

}


int check_atsc_fsm_status()
{
	int SNR;
	int atsc_snr=0;
     int SNR_dB;
     int SNR_table[56] = {0 ,7   ,9   ,11  ,14  ,17  ,22  ,27  ,34  ,43  ,54  ,
     	                  68  ,86  ,108 ,136 ,171 ,215 ,271 ,341 ,429 ,540 ,
     	                  566 ,592 ,620 ,649 ,680 ,712 ,746 ,781 ,818 ,856 ,
     	                  896 ,939 ,983 ,1029,1078,1182,1237,1237,1296,1357,
     	                  1708,2150,2707,3408,4291,5402,6800,8561,10778,13568,
     	                  16312,17081,18081,19081,65536};
     int SNR_dB_table[56] = {360,350,340,330,320,310,300,290,280,270,260,
     	                     250,240,230,220,210,200,190,180,170,160,
     	                     158,156,154,152,150,148,146,144,142,140,
     	                     138,136,134,132,130,128,126,124,122,120,
     	                     110,100,90 ,80 ,70 ,60 ,50 ,40 ,30 ,20 ,
     	                     12 ,10 ,4  ,2  ,0  };

     int tmp[3];
     int cr;
     int ck;
     int SM;
     int tni;
	 int ber;
	 int per;

     int cnt;
     cnt = 0;
	 ber=0;
	 per=0;

//     g_demod_mode    = 2;
     tni             =  atsc_read_reg((0x08)>>16);
//	 g_demod_mode    = 4;
     tmp[0]          =  atsc_read_reg(0x0511);
     tmp[1]          =  atsc_read_reg(0x0512);
     SNR             =  (tmp[0]<<8) + tmp[1];
     SNR_dB          = SNR_dB_table[find_2(SNR,SNR_table,56)];

     tmp[0]          =  atsc_read_reg(0x0780);
     tmp[1]          =  atsc_read_reg(0x0781);
     tmp[2]          =  atsc_read_reg(0x0782);
     cr              =  tmp[0]+ (tmp[1]<<8) + (tmp[2]<<16);
     tmp[0]          =  atsc_read_reg(0x0786);
     tmp[1]          =  atsc_read_reg(0x0787);
     tmp[2]          =  atsc_read_reg(0x0788);
     ck              =  (tmp[0]<<16) + (tmp[1]<<8) + tmp[2];
     ck              =  (ck > 8388608)? ck - 16777216:ck;
     SM              =  atsc_read_reg(0x0980);
//ber per
	atsc_write_reg(0x0601,atsc_read_reg(0x0601)&(~(1<<3)));
	atsc_write_reg(0x0601,atsc_read_reg(0x0601)|(1<<3));
	ber=atsc_read_reg(0x0683)+(atsc_read_reg(0x0682)<<8);
	per=atsc_read_reg(0x0685)+(atsc_read_reg(0x0684)<<8);

//	read_atsc_all_reg();

     pr_dbg("INT %x SNR %x SNRdB %d.%d FSM %x cr %d ck %d,ber is %d, per is %d\n"
         ,tni
         ,SNR
         ,(SNR_dB/10)
         ,(SNR_dB-(SNR_dB/10)*10)
         ,SM
         ,cr
         ,ck
         ,ber
         ,per
					);
     atsc_snr=(SNR_dB/10);
     return atsc_snr;



 /*   unsigned long sm,snr1,snr2,snr;
    static int fec_lock_cnt = 0;

    delay_us(10000);
    sm = atsc_read_reg(0x0980);
    snr1 = atsc_read_reg(0x0511)&0xff;
    snr2 = atsc_read_reg(0x0512)&0xff;
    snr  = (snr1 << 8) + snr2;

    printk(">>>>>>>>>>>>>>>>>>>>>>>>> OFDM FSM %x    SNR %x\n", sm&0xff, snr);

    if (sm == 0x79) stimulus_finish_pass();*/
}
