#if 1
#define CONFIG_ERR(v, name)     do { printk("%s: Invalid parameter: %s(%d)\n", __func__, (name), (v)); } while(0)
#else
#define CONFIG_ERR(v, name)
#endif
#include <plat/config.h>


/* keyboard */
uint key_adc = DEF_KEY_ADC;
module_param(key_adc, uint, 0644);
uint key_val_size = 6;
uint key_val[] = {DEF_PLAY_KEY, DEF_VOLDN_KEY, DEF_VOLUP_KEY, DEF_MENU_KEY, DEF_ESC_KEY, DEF_HOME_KEY};
module_param_array(key_val, uint, &key_val_size, 0644);
static inline int check_key_param(void)
{
	return 0;
}

/* backlight */
static int bl_en = DEF_BL_EN;
module_param(bl_en, int, 0644);
static uint bl_pwmid = DEF_BL_PWMID;
module_param(bl_pwmid, uint, 0644);

static uint bl_pwm_mode =DEF_BL_PWM_MOD;

static uint bl_mode = DEF_BL_MOD;
module_param(bl_mode, uint, 0644);
static uint bl_div = DEF_BL_DIV;
module_param(bl_div, uint, 0644);
static uint bl_ref = DEF_BL_REF; 
module_param(bl_ref, uint, 0644);
static uint bl_min = DEF_BL_MIN;
module_param(bl_min, uint, 0644);
static uint bl_max = DEF_BL_MAX;
module_param(bl_max, uint, 0644);

static inline int check_bl_param(void){        
	if(bl_pwmid < 0 || bl_pwmid > 3){ 
		CONFIG_ERR(bl_pwmid, "bl_pwm");                
		return -EINVAL;        
	}        
	if(bl_ref != 0 && bl_ref != 1){ 
		CONFIG_ERR(bl_ref, "bl_ref");               
		return -EINVAL;        
	}        
	if(bl_min < 0||bl_min > 255){               
		CONFIG_ERR(bl_min, "bl_min");               
		return -EINVAL;        
	} 
	if(bl_max < 0||bl_max > 255){               
		CONFIG_ERR(bl_max, "bl_max");               
		return -EINVAL;        
	} 

	return 0;
}

/* lcd */
static int lcd_cs = DEF_LCD_CS;
module_param(lcd_cs, int, 0644);
static int lcd_en = DEF_LCD_EN;
module_param(lcd_en, int, 0644);
static int lcd_std = DEF_LCD_STD;
module_param(lcd_std, int, 0644);

static inline int check_lcd_param(void)
{       
	return 0;
}
uint lcd_param[LCD_PARAM_MAX] = DEF_LCD_PARAM;
module_param_array(lcd_param, uint, NULL, 0644);


/*codec*/
int codec_type = DEF_CODEC_TYPE;
module_param(codec_type, int, 0644);
int codec_power = DEF_CODEC_POWER;
module_param(codec_power, int, 0644);
int codec_rst = DEF_CODEC_RST;
module_param(codec_rst, int, 0644);
int codec_hdmi_irq = DEF_CODEC_HDMI_IRQ;
module_param(codec_hdmi_irq, int, 0644);
static int spk_ctl = DEF_SPK_CTL;
module_param(spk_ctl, int, 0644);
static int hp_det = DEF_HP_DET;
module_param(hp_det, int, 0644);
static int codec_i2c = DEF_CODEC_I2C;            // i2c channel
module_param(codec_i2c, int, 0644);
static int codec_addr = DEF_CODEC_ADDR;           // i2c addr
module_param(codec_addr, int, 0644);
static inline int check_codec_param(void)
{
	return 0;
}

/*tp*/
static int tp_type = DEF_TP_TYPE;
module_param(tp_type, int, 0644);
static int tp_irq = DEF_TP_IRQ;
module_param(tp_irq, int, 0644);
static int tp_rst =DEF_TP_RST;
module_param(tp_rst, int, 0644);
static int tp_i2c = DEF_TP_I2C;            // i2c channel
module_param(tp_i2c, int, 0644);
static int tp_addr = DEF_TP_ADDR;           // i2c addr
module_param(tp_addr, int, 0644);
static int tp_xmax = DEF_X_MAX;
module_param(tp_xmax, int, 0644);
static int tp_ymax = DEF_Y_MAX;
module_param(tp_ymax, int, 0644);
static int tp_firmVer= DEF_FIRMVER;
module_param(tp_firmVer, int, 0644);
static inline int check_tp_param(void)
{
    if(tp_type == TP_TYPE_NONE)
            return 0;
    if(tp_type < TP_TYPE_NONE || tp_type > TP_TYPE_MAX){
            CONFIG_ERR(tp_type, "tp_type");
            return -EINVAL;
    }
    if(tp_i2c < 0 || tp_i2c > 3){
            CONFIG_ERR(tp_i2c, "tp_i2c");
            return -EINVAL;
    }
    if(tp_addr < 0 || tp_addr > 0x7f){
            CONFIG_ERR(tp_addr, "tp_addr");
            return -EINVAL;
    }
    
     if(tp_xmax < 0 || tp_xmax >1920){
            CONFIG_ERR(tp_xmax, "tp_xmax");
            return -EINVAL;
    }
    
     if(tp_ymax < 0 || tp_ymax >1920){
            CONFIG_ERR(tp_ymax, "tp_ymax");
            return -EINVAL;
    }
   
    return 0;
}

/* gsensor */
static int gs_type = DEF_GS_TYPE;
module_param(gs_type, int, 0644);
static int gs_irq = DEF_GS_IRQ;
module_param(gs_irq, int, 0644);
static int gs_i2c = DEF_GS_I2C;
module_param(gs_i2c, int, 0644);
static int gs_addr = DEF_GS_ADDR;
module_param(gs_addr, int, 0644);
static int gs_orig[9] = DEF_GS_ORIG;
module_param_array(gs_orig, int, NULL, 0644);
static inline int check_gs_param(void)
{        
	int i;        
	if(gs_type == GS_TYPE_NONE)                
		return 0;        
	if(gs_type < GS_TYPE_NONE || gs_type > GS_TYPE_MAX){ 
		CONFIG_ERR(gs_type, "gs_type");                
		return -EINVAL;        
	}        
	if(gs_i2c < 0 || gs_i2c > 3){
		CONFIG_ERR(gs_i2c, "gs_i2c");                
		return -EINVAL;        
	}        
	if(gs_addr < 0 || gs_addr > 0x7f){
		CONFIG_ERR(gs_i2c, "gs_addr");                
		return -EINVAL;        
	}        
	for(i = 0; i < 9; i++){                
		if(gs_orig[i] != 1 && gs_orig[i] != 0 && gs_orig[i] != -1)
		{                        
			CONFIG_ERR(gs_orig[i], "gs_orig[x]");                        
			return -EINVAL;                
		}        
	}        
	return 0;
}
 
/* charge */
static int dc_det = DEF_DC_DET;
module_param(dc_det, int, 0644);
static int bat_low = DEF_BAT_LOW;
module_param(bat_low, int, 0644);
static int chg_ok = DEF_CHG_OK;
module_param(chg_ok, int, 0644);
static int chg_set = DEF_CHG_SET;
module_param(chg_set, int, 0644);
static int usb_det = DEF_USB_DET;
module_param(usb_det, int, 0644);
static int ref_vol = DEF_REF_VOL;
module_param(ref_vol, int, 0644);
static int up_res = DEF_UP_RES;
module_param(up_res, int, 0644);
static int down_res = DEF_DOWN_RES;
module_param(down_res, int, 0644);
static int root_chg = DEF_ROOT_CHG;
module_param(root_chg, int, 0644);
static int save_cap = DEF_SAVE_CAP;
module_param(save_cap, int, 0644);
static int low_vol = DEF_LOW_VOL;
module_param(low_vol, int, 0644);
int bat_charge[11] = DEF_BAT_CHARGE;
module_param_array(bat_charge, int, NULL, 0644);
int bat_discharge[11] = DEF_BAT_DISCHARGE;
module_param_array(bat_discharge, int, NULL, 0644);
static inline int check_chg_param(void)
{        
	return 0;
}

/*wifi*/
int wifi_type = DEF_WIFI_TYPE;
module_param(wifi_type, int, 0644);
int wifi_pwr = DEF_WIFI_POWER;
module_param(wifi_pwr, int, 0644);
static inline int check_wifi_param(void)
{        
	return 0;
}

/* global */
static int pwr_on = DEF_PWR_ON;
module_param(pwr_on, int, 0644);
static inline int rk_power_on(void)
{  
	int ret;
	ret=port_output_init(pwr_on, 1, "pwr_on");
	if(ret<0)
		CONFIG_ERR(pwr_on, "pwr_on"); 
	
	port_output_on(pwr_on);
	
	return 0;
}

