#include <linux/fb.h>
#include <linux/delay.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>

#include <linux/hdmi.h>
#include "rk610_lcd.h"
#include <linux/mfd/rk610_core.h>
#include "../../rk29_fb.h"
static struct i2c_client *rk610_g_lcd_client=NULL;
//static int rk610_scaler_read_p0_reg(struct i2c_client *client, char reg, char *val)
//{
	//return i2c_master_reg8_recv(client, reg, val, 1, 100*1000) > 0? 0: -EINVAL;
//}

static int rk610_scaler_write_p0_reg(struct i2c_client *client, char reg, char *val)
{
	return i2c_master_reg8_send(client, reg, val, 1, 100*1000) > 0? 0: -EINVAL;
}
static void rk610_scaler_pll_enable(struct i2c_client *client)
{
    char c;
    RK610_DBG(&client->dev,"%s \n",__FUNCTION__);
    c = S_PLL_PWR(0)|S_PLL_RESET(0)|S_PLL_BYPASS(0);
	rk610_scaler_write_p0_reg(client, S_PLL_CON2, &c);
}
static void rk610_scaler_pll_disable(struct i2c_client *client)
{
    char c;
    RK610_DBG(&client->dev,"%s \n",__FUNCTION__);
    c = S_PLL_PWR(1) |S_PLL_RESET(0) |S_PLL_BYPASS(1);
	rk610_scaler_write_p0_reg(client, S_PLL_CON2, &c);
}
static void rk610_scaler_enable(struct i2c_client *client)
{
    char c;
    RK610_DBG(&client->dev,"%s \n",__FUNCTION__);
    
    c= SCL_BYPASS(0) |SCL_DEN_INV(0) |SCL_H_V_SYNC_INV(0) |SCL_OUT_CLK_INV(0) |SCL_ENABLE(ENABLE);  
	rk610_scaler_write_p0_reg(client, SCL_CON0, &c);
}
static void rk610_scaler_disable(struct i2c_client *client)
{
    char c;
    RK610_DBG(&client->dev,"%s \n",__FUNCTION__);
    
    c= SCL_BYPASS(1) |SCL_DEN_INV(0) |SCL_H_V_SYNC_INV(0) |SCL_OUT_CLK_INV(0) |SCL_ENABLE(DISABLE); 
    rk610_scaler_write_p0_reg(client, SCL_CON0, &c);
}
static int rk610_output_config(struct i2c_client *client,struct rk29fb_screen *screen,bool enable)
{
    char c=0;
    RK610_DBG(&client->dev,"%s \n",__FUNCTION__);
     if(SCREEN_LVDS == screen->type){
        c = LVDS_OUT_CLK_PIN(0) |LVDS_OUT_CLK_PWR_PIN(1) |LVDS_PLL_PWR_PIN(0) \
            |LVDS_LANE_IN_FORMAT(DATA_D0_MSB) |LVDS_INPUT_SOURCE(FROM_LCD0_OR_SCL) \
            |LVDS_OUTPUT_FORMAT(screen->hw_format) ; 
	    rk610_scaler_write_p0_reg(client, LVDS_CON0, &c);
        c = LVDS_OUT_ENABLE(0x0) |LVDS_TX_PWR_ENABLE(0x0); 
	    rk610_scaler_write_p0_reg(client, LVDS_CON1, &c);
	}else if(SCREEN_RGB == screen->type){
        c = LCD1_OUT_ENABLE(LCD1_AS_OUT) | LCD1_OUT_SRC(enable?LCD1_FROM_SCL : LCD1_FROM_LCD0);
	    rk610_scaler_write_p0_reg(client, LCD1_CON, &c);
	}
	return 0;
}
#ifdef CONFIG_HDMI_DUAL_DISP
static int rk610_scaler_pll_set(struct i2c_client *client,struct rk29fb_screen *screen,u32 clkin )
{
    char c=0;
    char M=0,N=0,OD=0;
    RK610_DBG(&client->dev,"%s \n",__FUNCTION__);
	/***************SET SCALER PLL FROM CLKIN ,DIV 0*/
    if(screen->s_pixclock != 0){
        OD = (screen->s_pixclock)&0x3;
        N = (screen->s_pixclock >>4)&0xf;
        M = (screen->s_pixclock >>8)&0xff;
    }else {
        RK610_ERR(&client->dev,"RK610 Scaler pll not support rate \n");
    }
    c = S_PLL_FROM_DIV<<3 | S_PLL_DIV(0);
	rk610_scaler_write_p0_reg(client, CLOCK_CON0, &c);
    
    c = S_DIV_N(N)| S_DIV_OD(OD);
	rk610_scaler_write_p0_reg(client, S_PLL_CON0, &c);
    c = S_DIV_M(M);
    rk610_scaler_write_p0_reg(client, S_PLL_CON1, &c);
    rk610_scaler_pll_enable(client);
	return 0;
}


static int  scale_hv_factor(struct i2c_client *client ,u32 Hin_act, u32 Hout_act, u32 Vin_act, u32 Vout_act)
   {
    char c;
  	u32 hfactor_f,vfactor_f,scl_factor_f;
	int  hfactor;
	int  vfactor;
	struct scl_hv_info  HV2;
	hfactor_f = ((Hin_act-1)*4096)/(Hout_act-1);
    if(hfactor_f==4096)
	    {hfactor = 0x1000;}
 	else if(hfactor_f>(int)hfactor_f)
	  	{hfactor = (int)hfactor_f+1;}
	else
	  	{hfactor = (int)hfactor_f;}
	  
	scl_factor_f = Vin_act/Vout_act;
	if(scl_factor_f<2)
	    {vfactor_f = ((Vin_act-1)*4096)/(Vout_act-1);}
	else
	  	{vfactor_f = ((Vin_act-2)*4096)/(Vout_act-1);} 
	if(vfactor_f==4096)
	    {vfactor = 0x1000;}
	else if(vfactor_f>(int)vfactor_f)
	  	{vfactor = (int)vfactor_f+1;}
	else
	  	{vfactor = (int)vfactor_f;}
	  
    HV2.scl_h= hfactor;
    HV2.scl_v= vfactor; 
           /*       SCL FACTOR          */
    c = SCL_H_FACTOR_LSB(HV2.scl_h);
	rk610_scaler_write_p0_reg(client, SCL_CON1, &c);
    c = SCL_H_FACTOR_MSB(HV2.scl_h);
	rk610_scaler_write_p0_reg(client, SCL_CON2, &c);

    c = SCL_V_FACTOR_LSB(HV2.scl_v);
	rk610_scaler_write_p0_reg(client, SCL_CON3, &c);
    c = SCL_V_FACTOR_MSB(HV2.scl_v);
	rk610_scaler_write_p0_reg(client, SCL_CON4, &c);
  	return 0;
   }

static int rk610_scaler_fator_config(struct i2c_client *client ,struct rk29fb_screen *screen)
{
    switch(screen->hdmi_resolution){
        case HDMI_1920x1080p_60Hz:
        case HDMI_1920x1080p_50Hz:
            rk610_scaler_pll_set(client,screen,148500000);
            /***************set scaler factor********************/
            scale_hv_factor(client,1920,screen->x_res,1080,screen->y_res);
            break;
        case HDMI_1280x720p_60Hz:
        case HDMI_1280x720p_50Hz:
            rk610_scaler_pll_set(client,screen,74250000);
            /***************set scaler factor********************/
            scale_hv_factor(client,1280,screen->x_res,720,screen->y_res);
        break;
        case HDMI_720x576p_50Hz_16x9:
        case HDMI_720x576p_50Hz_4x3:
            rk610_scaler_pll_set(client,screen,27000000);
            /***************set scaler factor********************/
            scale_hv_factor(client,720,screen->x_res,576,screen->y_res);
            break;
        case HDMI_720x480p_60Hz_16x9:
        case HDMI_720x480p_60Hz_4x3:
            rk610_scaler_pll_set(client,screen,27000000);
            /***************set scaler factor********************/
            scale_hv_factor(client,720,screen->x_res,480,screen->y_res);
        break;
    default :
        RK610_ERR(&client->dev,"RK610 not support dual display at hdmi resolution=%d \n",screen->hdmi_resolution); 
        return -1;
        break;
    }
}
static int rk610_scaler_output_timing_config(struct i2c_client *client,struct rk29fb_screen *screen)
{
    char c;
    int h_st = screen->s_hsync_st;
    int hs_end = screen->s_hsync_len;
    int h_act_st = hs_end + screen->s_left_margin;
    int xres = screen->x_res;
    int h_act_end = h_act_st + xres;
    int h_total = h_act_end + screen->s_right_margin;
    int v_st = screen->s_vsync_st;
    int vs_end = screen->s_vsync_len;
    int v_act_st = vs_end + screen->s_upper_margin;
    int yres = screen->y_res;    
    int v_act_end = v_act_st + yres;
    int v_total = v_act_end + screen->s_lower_margin;

    /*      SCL display Frame start point   */
    c = SCL_DSP_HST_LSB(h_st);
	rk610_scaler_write_p0_reg(client, SCL_CON5, &c);
    c = SCL_DSP_HST_MSB(h_st);
	rk610_scaler_write_p0_reg(client, SCL_CON6, &c);

    c = SCL_DSP_VST_LSB(v_st);
	rk610_scaler_write_p0_reg(client, SCL_CON7, &c);
    c = SCL_DSP_VST_MSB(v_st);
	rk610_scaler_write_p0_reg(client, SCL_CON8, &c);
    /*      SCL output timing       */

    c = SCL_DSP_HTOTAL_LSB(h_total);
	rk610_scaler_write_p0_reg(client, SCL_CON9, &c);
    c = SCL_DSP_HTOTAL_MSB(h_total);
	rk610_scaler_write_p0_reg(client, SCL_CON10, &c);

    c = SCL_DSP_HS_END(hs_end);
	rk610_scaler_write_p0_reg(client, SCL_CON11, &c);

    c = SCL_DSP_HACT_ST_LSB(h_act_st);
	rk610_scaler_write_p0_reg(client, SCL_CON12, &c);
    c = SCL_DSP_HACT_ST_MSB(h_act_st);
	rk610_scaler_write_p0_reg(client, SCL_CON13, &c);

    c = SCL_DSP_HACT_END_LSB(h_act_end);
	rk610_scaler_write_p0_reg(client, SCL_CON14, &c);
    c = SCL_DSP_HACT_END_MSB(h_act_end);
	rk610_scaler_write_p0_reg(client, SCL_CON15, &c);

    c = SCL_DSP_VTOTAL_LSB(v_total);
	rk610_scaler_write_p0_reg(client, SCL_CON16, &c);
    c = SCL_DSP_VTOTAL_MSB(v_total);
	rk610_scaler_write_p0_reg(client, SCL_CON17, &c);

    c = SCL_DSP_VS_END(vs_end);
	rk610_scaler_write_p0_reg(client, SCL_CON18, &c);

    c = SCL_DSP_VACT_ST(v_act_st);
	rk610_scaler_write_p0_reg(client, SCL_CON19, &c);

    c = SCL_DSP_VACT_END_LSB(v_act_end);
	rk610_scaler_write_p0_reg(client, SCL_CON20, &c);
    c = SCL_DSP_VACT_END_MSB(v_act_end); 
	rk610_scaler_write_p0_reg(client, SCL_CON21, &c);
 
    c = SCL_H_BORD_ST_LSB(h_act_st);
	rk610_scaler_write_p0_reg(client, SCL_CON22, &c);
    c = SCL_H_BORD_ST_MSB(h_act_st);
	rk610_scaler_write_p0_reg(client, SCL_CON23, &c);

    c = SCL_H_BORD_END_LSB(h_act_end);
	rk610_scaler_write_p0_reg(client, SCL_CON24, &c);
    c = SCL_H_BORD_END_MSB(h_act_end);
	rk610_scaler_write_p0_reg(client, SCL_CON25, &c);

    c = SCL_V_BORD_ST(v_act_st);
	rk610_scaler_write_p0_reg(client, SCL_CON26, &c);

    c = SCL_V_BORD_END_LSB(v_act_end);
	rk610_scaler_write_p0_reg(client, SCL_CON27, &c);
    c = SCL_V_BORD_END_MSB(v_act_end);
	rk610_scaler_write_p0_reg(client, SCL_CON28, &c);
	
	return 0;
}
static int rk610_scaler_chg(struct i2c_client *client ,struct rk29fb_screen *screen)
{

    RK610_DBG(&client->dev,"%s screen->hdmi_resolution=%d\n",__FUNCTION__,screen->hdmi_resolution);
    rk610_scaler_fator_config(client,screen);
    rk610_scaler_enable(client);
    rk610_scaler_output_timing_config(client,screen); 
    
    return 0;

}
#endif
static int rk610_lcd_scaler_bypass(struct i2c_client *client,bool enable)//enable:0 bypass 1: scale
{
    RK610_DBG(&client->dev,"%s \n",__FUNCTION__);
    
    rk610_scaler_pll_disable(client);
    rk610_scaler_disable(client);
   
    return 0;
}
int rk610_lcd_scaler_set_param(struct rk29fb_screen *screen,bool enable )//enable:0 bypass 1: scale
{
    int ret=0;
    struct i2c_client *client = rk610_g_lcd_client;
    RK610_DBG(&client->dev,"%s \n",__FUNCTION__);
    if(client == NULL){
    RK610_ERR(&client->dev,"%s client == NULL FAIL\n",__FUNCTION__);
    return -1;
    }
    
#ifdef CONFIG_HDMI_DUAL_DISP
    if(enable == 1){
        rk610_output_config(client,screen,1);
        ret = rk610_scaler_chg(client,screen);
	}
	else 
#endif
	{
	    rk610_output_config(client,screen,0);
	    ret = rk610_lcd_scaler_bypass(client,enable);
	}
	return ret;
}
int rk610_lcd_init(struct i2c_client *client)
{
    RK610_DBG(&client->dev,"%s \n",__FUNCTION__);
    rk610_g_lcd_client = client;
    return 0;
}
