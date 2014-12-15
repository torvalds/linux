
#include <mach/am_regs.h>
#include "sii9233_drv.h"
#include "../../../../hardware/tvin/tvin_frontend.h"
#include "sii9233_interface.h"

extern int start_tvin_service(int no ,vdin_parm_t *para);
extern int stop_tvin_service(int no);
extern void set_invert_top_bot(bool invert_flag);

static void enable_vdin_pinmux(void)
{

	// disable LCD pinmux
	WRITE_CBUS_REG(PERIPHS_PIN_MUX_0, READ_CBUS_REG(PERIPHS_PIN_MUX_0) & 
	 ( ~(1       | // LCD_R 2/3/4/5/6/7
		(1 << 1)  | // LCD_R 0/1
		(1 << 2)  | // LCD_G 2/3/4/5/6/7
		(1 << 3)  | // LCD_G 0/1
		(1 << 4)  | // LCD_B 2/3/4/5/6/7
		(1 << 5)  | // LCD_B 0/1
		(1 << 18) | // LCD_HS
		(1 << 19)) )    // LCD_VS
		);

	// disable TCON pinmux
	WRITE_CBUS_REG(PERIPHS_PIN_MUX_8, READ_CBUS_REG(PERIPHS_PIN_MUX_8) & 
	 ( ~((1 << 19) | // TCON_VCOM
		(1 << 20) | // TCON_CPH3
		(1 << 21) | // TCON_CPH2
		(1 << 22) | // TCON_CPH1
		(1 << 23) | // TCON_STH1
		(1 << 24) | // TCON_STV1
		(1 << 25) | // TCON_CPV
		(1 << 26) | // TCON_VCOM
		(1 << 27) | // TCON_OEV
		(1 << 28)) )   // TCON_OEH
		);

	// disable ENC pinmux
	WRITE_CBUS_REG(PERIPHS_PIN_MUX_7, READ_CBUS_REG(PERIPHS_PIN_MUX_7) & 
	 ( ~(1     | // ENC_0
		(1 << 1)  | // ENC_1
		(1 << 2)  | // ENC_2
		(1 << 3)  | // ENC_3
		(1 << 4)  | // ENC_4
		(1 << 5)  | // ENC_5
		(1 << 6)  | // ENC_6
		(1 << 7)  | // ENC_7
		(1 << 8)  | // ENC_8
		(1 << 9)  | // ENC_9
		(1 << 10) | // ENC_10
		(1 << 11) | // ENC_11
		(1 << 12) | // ENC_12
		(1 << 13) | // ENC_13
		(1 << 14) | // ENC_14
		(1 << 15) | // ENC_15
		(1 << 16) | // ENC_16(eNet_0)
		(1 << 17)) )    // ENC_17(eNet_1)
		);

	// disable PWM pinmux
	WRITE_CBUS_REG(PERIPHS_PIN_MUX_3, READ_CBUS_REG(PERIPHS_PIN_MUX_3) & 
	 ( ~((1 << 24) | // PWM_C
		(1 << 25) | // PWM_C
		(1 << 26)))    // PWM_D
		);
	WRITE_CBUS_REG(PERIPHS_PIN_MUX_7, READ_CBUS_REG(PERIPHS_PIN_MUX_7) & 
	 ( ~((1 << 26) | // PWM_VS
		(1 << 27) | // PWM_VS
		(1 << 28)))    // PWM_VS
		);

	// disable VGA pinmux
	WRITE_CBUS_REG(PERIPHS_PIN_MUX_0, READ_CBUS_REG(PERIPHS_PIN_MUX_0) & 
	 ( ~((1 << 20) | // VGA_HS
		(1 << 21)))    // VGA_VS
		);

	// enable DVIN pinmux
	WRITE_CBUS_REG(PERIPHS_PIN_MUX_0, READ_CBUS_REG(PERIPHS_PIN_MUX_0) | 
	 ( (1 << 6)  | // DVIN R/G/B 0/1/2/3/4/5/6/7
		(1 << 7)  | // DVIN_CLK
		(1 << 8)  | // DVIN_HS
		(1 << 9)  | // DVIN_VS
		(1 << 10))    // DVIN_DE
		);
}

static int sii9233a_tvin_support(struct tvin_frontend_s *fe, enum tvin_port_e port)
{
	if(port == TVIN_PORT_DVIN0)
		return 0;
	else
		return -1;
}
static int sii9233a_tvin_open(tvin_frontend_t *fe, enum tvin_port_e port)
{
	sii9233a_info_t *devp = container_of(fe,sii9233a_info_t,tvin_frontend);

	/*copy the vdin_parm_s to local device parameter*/
	if( !memcpy(&devp->vdin_parm,fe->private_data,sizeof(vdin_parm_t)) )
	{
		printk("[%s] copy vdin parm error.\n",__func__);
	}

	enable_vdin_pinmux();

	WRITE_MPEG_REG_BITS(VDIN_ASFIFO_CTRL2, 0x39, 2, 6); 

	return 0;
}

static void sii9233a_tvin_close(struct tvin_frontend_s *fe)
{
	return ;          
}
static void sii9233a_tvin_start(struct tvin_frontend_s *fe, enum tvin_sig_fmt_e fmt)
{
	return ;
}
static void sii9233a_tvin_stop(struct tvin_frontend_s *fe, enum tvin_port_e port)
{
	return ;      
}
static int sii9233a_tvin_isr(struct tvin_frontend_s *fe, unsigned int hcnt64)
{
	return 0;
}

static struct tvin_decoder_ops_s sii9233a_tvin_dec_ops = {
				.support 		= sii9233a_tvin_support,
				.open 			= sii9233a_tvin_open,
				.close 			= sii9233a_tvin_close,
				.start  		= sii9233a_tvin_start,
				.stop  			= sii9233a_tvin_stop,
				.decode_isr 	= sii9233a_tvin_isr, 
};

static void sii9233a_tvin_get_sig_propery(struct tvin_frontend_s *fe, struct tvin_sig_property_s *prop)
{
	sii9233a_info_t *devp = container_of(fe,sii9233a_info_t,tvin_frontend);

	prop->color_format = TVIN_RGB444;
	prop->dest_cfmt = TVIN_YUV422;
	if( (devp->vdin_parm.h_active==1440) && 
	  ((devp->vdin_parm.v_active==576)||(devp->vdin_parm.v_active==480)) )
	  prop->decimation_ratio = 1;
	else 
	  prop->decimation_ratio = 0;

	return ;
}

static struct tvin_state_machine_ops_s sii9233a_tvin_sm_ops = {
		.get_sig_propery = sii9233a_tvin_get_sig_propery,
};

int sii9233a_register_tvin_frontend(struct tvin_frontend_s *frontend)
{
	int ret = 0;

	ret = tvin_frontend_init(frontend, &sii9233a_tvin_dec_ops, &sii9233a_tvin_sm_ops, 0);
	if( ret != 0 )
	{
		printk("[%s] init tvin frontend failed = %d\n", __FUNCTION__, ret);
		return -1;
	}

	ret = tvin_reg_frontend(frontend);
	if( ret != 0 )
	{
		printk("[%s] register tv in frontend failed = %d\n", __FUNCTION__, ret);
		return -2;
	}

	return 0;
}

void sii9233a_config_dvin (unsigned long hs_pol_inv,             // Invert HS polarity, for HW regards HS active high.
						unsigned long vs_pol_inv,             // Invert VS polarity, for HW regards VS active high.
						unsigned long de_pol_inv,             // Invert DE polarity, for HW regards DE active high.
						unsigned long field_pol_inv,          // Invert FIELD polarity, for HW regards odd field when high.
						unsigned long ext_field_sel,          // FIELD source select:
																		  // 1=Use external FIELD signal, ignore internal FIELD detection result;
																		  // 0=Use internal FIELD detection result, ignore external input FIELD signal.
						unsigned long de_mode,                // DE mode control:
																		  // 0=Ignore input DE signal, use internal detection to to determine active pixel;
																		  // 1=Rsrv;
																		  // 2=During internal detected active region, if input DE goes low, replace input data with the last good data;
																		  // 3=Active region is determined by input DE, no internal detection.
						unsigned long data_comp_map,          // Map input data to form YCbCr.
																		  // Use 0 if input is YCbCr;
																		  // Use 1 if input is YCrCb;
																		  // Use 2 if input is CbCrY;
																		  // Use 3 if input is CbYCr;
																		  // Use 4 if input is CrYCb;
																		  // Use 5 if input is CrCbY;
																		  // 6,7=Rsrv.
						unsigned long mode_422to444,          // 422 to 444 conversion control:
																		  // 0=No convertion; 1=Rsrv;
																		  // 2=Convert 422 to 444, use previous C value;
																		  // 3=Convert 422 to 444, use average C value.
						unsigned long dvin_clk_inv,           // Invert dvin_clk_in for ease of data capture.
						unsigned long vs_hs_tim_ctrl,         // Controls which edge of HS/VS (post polarity control) the active pixel/line is related:
																		  // Bit 0: HS and active pixel relation.
																		  //  0=Start of active pixel is counted from the rising edge of HS;
																		  //  1=Start of active pixel is counted from the falling edge of HS;
																		  // Bit 1: VS and active line relation.
																		  //  0=Start of active line is counted from the rising edge of VS;
																		  //  1=Start of active line is counted from the falling edge of VS.
						unsigned long hs_lead_vs_odd_min,     // For internal FIELD detection:
																		  // Minimum clock cycles allowed for HS active edge to lead before VS active edge in odd field. Failing it the field is even.
						unsigned long hs_lead_vs_odd_max,     // For internal FIELD detection:
																		  // Maximum clock cycles allowed for HS active edge to lead before VS active edge in odd field. Failing it the field is even.
						unsigned long active_start_pix_fe,    // Number of clock cycles between HS active edge to first active pixel, in even field.
						unsigned long active_start_pix_fo,    // Number of clock cycles between HS active edge to first active pixel, in odd field.
						unsigned long active_start_line_fe,   // Number of clock cycles between VS active edge to first active line, in even field.
						unsigned long active_start_line_fo,   // Number of clock cycles between VS active edge to first active line, in odd field.
						unsigned long line_width,             // Number_of_pixels_per_line
						unsigned long field_height)           // Number_of_lines_per_field
{
	unsigned long data32;

//	printk("[%s] config pol_inv: hs = %d, vs = %d, de = %d, field = %d, clk = %d\n",__FUNCTION__, hs_pol_inv,vs_pol_inv,de_pol_inv,field_pol_inv,dvin_clk_inv);
//	printk("[%s]: %lu %lu %lu %lu.\n",  __FUNCTION__, active_start_pix_fe, active_start_line_fe,  line_width, field_height);  
	// Program reg DVIN_CTRL_STAT: disable DVIN
	WRITE_MPEG_REG(DVIN_CTRL_STAT, 0);

	// Program reg DVIN_FRONT_END_CTRL
	data32 = 0;
	data32 |= (hs_pol_inv       & 0x1)  << 0;
	data32 |= (vs_pol_inv       & 0x1)  << 1;
	data32 |= (de_pol_inv       & 0x1)  << 2;
	data32 |= (field_pol_inv    & 0x1)  << 3;
	data32 |= (ext_field_sel    & 0x1)  << 4;
	data32 |= (de_mode          & 0x3)  << 5;
	data32 |= (mode_422to444    & 0x3)  << 7;
	data32 |= (dvin_clk_inv     & 0x1)  << 9;
	data32 |= (vs_hs_tim_ctrl   & 0x3)  << 10;
	WRITE_MPEG_REG(DVIN_FRONT_END_CTRL, data32);

	// Program reg DVIN_HS_LEAD_VS_ODD
	data32 = 0;
	data32 |= (hs_lead_vs_odd_min & 0xfff) << 0;
	data32 |= (hs_lead_vs_odd_max & 0xfff) << 16;
	WRITE_MPEG_REG(DVIN_HS_LEAD_VS_ODD, data32);

	// Program reg DVIN_ACTIVE_START_PIX
	data32 = 0;
	data32 |= (active_start_pix_fe & 0xfff) << 0;
	data32 |= (active_start_pix_fo & 0xfff) << 16;
	WRITE_MPEG_REG(DVIN_ACTIVE_START_PIX, data32);

	// Program reg DVIN_ACTIVE_START_LINE
	data32 = 0;
	data32 |= (active_start_line_fe & 0xfff) << 0;
	data32 |= (active_start_line_fo & 0xfff) << 16;
	WRITE_MPEG_REG(DVIN_ACTIVE_START_LINE, data32);

	// Program reg DVIN_DISPLAY_SIZE
	data32 = 0;
	data32 |= ((line_width-1)   & 0xfff) << 0;
	data32 |= ((field_height-1) & 0xfff) << 16;
	WRITE_MPEG_REG(DVIN_DISPLAY_SIZE, data32);

	// Program reg DVIN_CTRL_STAT, and enable DVIN
	data32 = 0;
	data32 |= 1                     << 0;
	data32 |= (data_comp_map & 0x7) << 1;
	WRITE_MPEG_REG(DVIN_CTRL_STAT, data32);
//    printk("[%s] end !\n", __FUNCTION__);
} /* config_dvin */

void sii9233a_stop_vdin(sii9233a_info_t *info)
{
	if( info->vdin_started == 0 )
	  return ;

	stop_tvin_service(0);
	set_invert_top_bot(false);
	info->vdin_started = 0;
	printk("%s: stop vdin\n", __FUNCTION__);
	return ;
}

void sii9233a_start_vdin(sii9233a_info_t *info, int width, int height, int frame_rate, int field_flag)
{
	vdin_parm_t para;

	printk("[%s]-%.3d, width = %d, height = %d, frame_rate = %d, field_flag = %d\n",
							__FUNCTION__, __LINE__, width,height,frame_rate,field_flag);

	//    printk("[%s]-%.3d, info = 0x%x\n",__FUNCTION__, __LINE__, info);
	if(info->vdin_started)
	{
		//printk("[%s]-%.3d, info->vdin_info = 0x%x\n",__FUNCTION__, __LINE__, &(info->vdin_info) );
		if( (info->vdin_info.cur_width != width) || (info->vdin_info.cur_height != height) ||
											(info->vdin_info.cur_frame_rate != frame_rate) )
		{
			stop_tvin_service(0);
			info->vdin_started=0;
			printk("%s: stop vdin\n", __func__);
		}
	}

	if( (info->vdin_started==0) && (width>0) && (height>0) && (frame_rate>0) )
	{
		int start_pix=0, start_line_o=0, start_line_e=0, h_total=0, v_total=0;

		info->vdin_info.cur_width = width;
		info->vdin_info.cur_height = height;
		info->vdin_info.cur_frame_rate = frame_rate;

		if(field_flag && height <= 576 )
		{
			// for rgb 576i signal from 9233, it's 720/864, not 1440/1728
			if( (width==1440)&&(height==576) )
			{
				start_pix = 138;
				start_line_o = 22;
				start_line_e = 23;
				h_total = 1728;
				v_total = 625;
			}
			// for rgb 480i signal from 9233, it's 720/858, not 1440/1716
			else if( (width==1440)&&(height==480) )
			{
				start_pix = 114;
				start_line_o = 18;
				start_line_e = 19;
				h_total = 1716;
				v_total = 525;
			}
			sii9233a_config_dvin(1, //hs_pol_inv,          
						1, //vs_pol_inv,          
						0, //de_pol_inv,          
						0, //field_pol_inv,       
						0, //ext_field_sel,       
						3, //de_mode,             
						0, //data_comp_map,       
						0, //mode_422to444,       
						0, //dvin_clk_inv,        
						0, //vs_hs_tim_ctrl,      
						400, //hs_lead_vs_odd_min,  
						1200, //hs_lead_vs_odd_max,  
						start_pix,//sii_get_hs_backporch()*2,//0xdc, //active_start_pix_fe, 
						start_pix,//sii_get_hs_backporch()*2,//0xdc, //active_start_pix_fo, 
						start_line_e,//sii_get_vs_backporch(), //0x19, //active_start_line_fe,
						start_line_o,//sii_get_vs_backporch(),//0x19, //active_start_line_fo,
						h_total,//sii_get_h_total(), //0x672, //line_width,          
						v_total//sii_get_v_total()*2 //0x2ee //field_height
						);
		}
		else
		{
			sii9233a_config_dvin(height>576?0:1, //hs_pol_inv,          
						height>576?0:1, //vs_pol_inv,          
						0, //de_pol_inv,          
						(field_flag && height>=540)?1:0, //field_pol_inv, set to 1 for 1080i
						0, //ext_field_sel,       
						3, //de_mode,             
						0, //data_comp_map,       
						0, //mode_422to444,       
						0, //dvin_clk_inv,        
						0, //vs_hs_tim_ctrl,      
						0, //hs_lead_vs_odd_min,  
						0, //hs_lead_vs_odd_max,  
						sii_get_hs_backporch(),//0xdc, //active_start_pix_fe, 
						sii_get_hs_backporch(),//0xdc, //active_start_pix_fo, 
						sii_get_vs_backporch(), //0x19, //active_start_line_fe,
						sii_get_vs_backporch(),//0x19, //active_start_line_fo,
						sii_get_h_total(), //0x672, //line_width,          
						sii_get_v_total() //0x2ee //field_height
						);       
		}        

		memset( &para, 0, sizeof(para));
		para.port  = TVIN_PORT_DVIN0;
		para.frame_rate = frame_rate;
		para.h_active = info->vdin_info.cur_width;
		para.v_active = info->vdin_info.cur_height;
		if(field_flag){
			if(info->vdin_info.cur_width == 1920 &&  
			  (info->vdin_info.cur_height == 1080 || info->vdin_info.cur_height == 540)){
				if( frame_rate == 60 )
					para.fmt = TVIN_SIG_FMT_HDMI_1920X1080I_60HZ;
				else if( frame_rate == 50 )
					para.fmt = TVIN_SIG_FMT_HDMI_1920X1080I_50HZ_A;
				para.v_active = 1080;
			}
		/*
			else if( info->vdin_info.cur_width == 720 &&  (info->vdin_info.cur_height == 576 || info->vdin_info.cur_height == 288)){
				 para.fmt = TVIN_SIG_FMT_HDMI_720X576I_50HZ;
				 para.v_active = 576;
				 set_invert_top_bot(true);
			}
		*/
			else if(info->vdin_info.cur_width == 1440 &&  
			  (info->vdin_info.cur_height == 576 || info->vdin_info.cur_height == 288)){
				para.fmt = TVIN_SIG_FMT_HDMI_1440X576I_50HZ;
				para.v_active = 576;
				set_invert_top_bot(true);
			}
		/*
			else if( info->vdin_info.cur_width == 720 &&  (info->vdin_info.cur_height == 480 || info->vdin_info.cur_height == 240)){
				 para.fmt = TVIN_SIG_FMT_HDMI_720X480I_60HZ;
				 para.v_active = 480;
				 set_invert_top_bot(true);
			}
		*/
			else if(info->vdin_info.cur_width == 1440  &&  
			  (info->vdin_info.cur_height == 480 || info->vdin_info.cur_height == 240)){
				para.fmt = TVIN_SIG_FMT_HDMI_1440X480I_60HZ;
				para.v_active = 480;
				set_invert_top_bot(true);
			}
			else{
				para.fmt = TVIN_SIG_FMT_MAX+1;
				set_invert_top_bot(true);
			}
			para.scan_mode = TVIN_SCAN_MODE_INTERLACED;	
		}
		else{
			if(info->vdin_info.cur_width == 1920 &&  info->vdin_info.cur_height == 1080){
				para.fmt = TVIN_SIG_FMT_HDMI_1920X1080P_60HZ;
			}
			else if(info->vdin_info.cur_width == 1280 &&  info->vdin_info.cur_height == 720){
				para.fmt = TVIN_SIG_FMT_HDMI_1280X720P_60HZ;
			}
			else if((info->vdin_info.cur_width == 1440 || info->vdin_info.cur_width == 720) &&  info->vdin_info.cur_height == 576){
				para.fmt = TVIN_SIG_FMT_HDMI_720X576P_50HZ;
			}
			else if((info->vdin_info.cur_width == 1440 || info->vdin_info.cur_width == 720) &&  info->vdin_info.cur_height == 480){
				para.fmt = TVIN_SIG_FMT_HDMI_720X480P_60HZ;
			}
			else{
				para.fmt = TVIN_SIG_FMT_MAX+1;
			}
			para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;	
		}
		para.hsync_phase = 1;
		para.vsync_phase = 0;
		//para.hs_bp = 0;
		//para.vs_bp = 2;
		para.cfmt = TVIN_RGB444;
		para.dfmt = TVIN_YUV422;
		para.reserved = 0; //skip_num

		printk("[%s] begin start_tvin_service() !\n",__FUNCTION__);
		start_tvin_service(0,&para);
		info->vdin_started = 1;

		//printk("%s: %dx%d %d %d/s\n", __func__, width, height, frame_rate, field_flag);
	}

	return ;
}
