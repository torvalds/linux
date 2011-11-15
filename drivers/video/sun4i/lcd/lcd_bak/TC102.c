
#include "lcd_panel_cfg.h"

static void  LCD_io_init(__u32 sel);
static void  LCD_io_exit(__u32 sel);
static void  LCD_open_cmd(__u32 sel);
static void  LCD_close_cmd(__u32 sel);

//delete this line if you want to use the lcd para define in sys_config1.fex
#define LCD_PARA_USE_CONFIG

#ifdef LCD_PARA_USE_CONFIG

static __u32 g_gamma_tbl[18] =
{
0,      //0
16,     //15
40,     //30
55,     //45
66,     //60
82,     //75
96,     //90
112,    //105
131,    //120
145,    //135
160,    //150
173,    //165
187,    //180
199,    //195
213,    //210
224,    //225
237,    //240
255     //255
};

static void LCD_cfg_panel_info(__panel_para_t * info)
{
    __u32 i = 0, j=0;

    memset(info,0,sizeof(__panel_para_t));

    info->lcd_x             = 1280;
    info->lcd_y             = 768;
    info->lcd_dclk_freq     = 68;       //MHz

    info->lcd_pwm_not_used  = 0;
    info->lcd_pwm_ch        = 0;
    info->lcd_pwm_freq      = 10000;     //Hz
    info->lcd_pwm_pol       = 0;

    info->lcd_if            = 0;        //0:hv(sync+de); 1:8080; 2:ttl; 3:lvds

    info->lcd_hbp           = 3;      //hsync back porch
    info->lcd_ht            = 1440;     //hsync total cycle
    info->lcd_vbp           = 3;       //vsync back porch
    info->lcd_vt            = 1580;  //vysnc total cycle *2

    info->lcd_hv_if         = 0;        //0:hv parallel 1:hv serial
    info->lcd_hv_smode      = 0;        //0:RGB888 1:CCIR656
    info->lcd_hv_s888_if    = 0;        //serial RGB format
    info->lcd_hv_syuv_if    = 0;        //serial YUV format
    info->lcd_hv_hspw       = 0;        //hsync plus width
    info->lcd_hv_vspw       = 0;        //vysnc plus width

    info->lcd_cpu_if        = 0;        //0:18bit 4:16bit
    info->lcd_frm           = 1;        //0: disable; 1: enable rgb666 dither; 2:enable rgb656 dither

    info->lcd_lvds_ch       = 0;        //0:single channel; 1:dual channel
    info->lcd_lvds_mode     = 0;        //0:NS mode; 1:JEIDA mode
    info->lcd_lvds_bitwidth = 0;        //0:24bit; 1:18bit
    info->lcd_lvds_io_cross = 0;        //0:normal; 1:pn cross

    info->lcd_io_cfg0       = 0x00000000;

    info->lcd_gamma_correction_en = 1;
    for(i=0; i<17; i++)
    {
        for(j=0; j<15; j++)
        {
            __u32 value = 0;

            value = g_gamma_tbl[i] + ((g_gamma_tbl[i+1] - g_gamma_tbl[i]) * j)/15;
            info->lcd_gamma_tbl[i*15 + j] = (value<<16) + (value<<8) + value;
        }
    }
    info->lcd_gamma_tbl[255] = (g_gamma_tbl[17]<<16) + (g_gamma_tbl[17]<<8) + g_gamma_tbl[17];
}
#endif

static __s32 LCD_open_flow(__u32 sel)
{
	LCD_OPEN_FUNC(sel, LCD_power_on	, 50); 	//open lcd power, and delay 10ms
	LCD_OPEN_FUNC(sel, LCD_io_init	, 20); 	//request and init gpio, and delay 20ms
	LCD_OPEN_FUNC(sel, TCON_open	, 500);   //open lcd controller, and delay 200ms
	LCD_OPEN_FUNC(sel, LCD_open_cmd	, 10); 	//use gpio to config lcd module to the  work mode, and delay 10ms
	LCD_OPEN_FUNC(sel, LCD_bl_open	, 0); 	//open lcd backlight, and delay 0ms

	return 0;
}

static __s32 LCD_close_flow(__u32 sel)
{
	LCD_CLOSE_FUNC(sel, LCD_bl_close    , 0); 	 //close lcd backlight, and delay 0ms
	LCD_CLOSE_FUNC(sel, LCD_close_cmd   , 0); 	 //use gpio to config lcd module to the powerdown/sleep mode, and delay 0ms
	LCD_CLOSE_FUNC(sel, TCON_close	    , 0); 	 //close lcd controller, and delay 0ms
	LCD_CLOSE_FUNC(sel, LCD_io_exit	    , 0); 	 //release gpio, and delay 0ms
	LCD_CLOSE_FUNC(sel, LCD_power_off   , 1000); //close lcd power, and delay 1000ms

	return 0;
}

static void LCD_power_on(__u32 sel)
{
    LCD_POWER_EN(sel, 1);//config lcd_power pin to open lcd power
}

static void LCD_power_off(__u32 sel)
{
    LCD_POWER_EN(sel, 0);//config lcd_power pin to close lcd power
}

static void LCD_bl_open(__u32 sel)
{
    LCD_PWM_EN(sel, 1);//open pwm module
    LCD_BL_EN(sel, 1);//config lcd_bl_en pin to open lcd backlight
}

static void LCD_bl_close(__u32 sel)
{
    LCD_BL_EN(sel, 0);//config lcd_bl_en pin to close lcd backlight
    LCD_PWM_EN(sel, 0);//close pwm module
}

#define IIC_SCLB_LOW()	        LCD_GPIO_write(0, 0, 0)
#define IIC_SCLB_HIGH()	        LCD_GPIO_write(0, 0, 1)

#define IIC_SDAB_INPUT_SETUP()	LCD_GPIO_set_attr(0, 1, 0)
#define IIC_SDAB_OUTPUT_SETUP()	LCD_GPIO_set_attr(0, 1, 1)
#define CHECK_SDAB_HIGH()       LCD_GPIO_read(0, 1)
#define IIC_SDAB_LOW()          LCD_GPIO_write(0, 1, 0)
#define IIC_SDAB_HIGH()	        LCD_GPIO_write(0, 1, 1)

static __bool i2cB_clock( void )
{
	__bool sample = 0;

	IIC_SCLB_HIGH();
	LCD_delay_us(10) ;
	IIC_SCLB_LOW();
	LCD_delay_us(10) ;
	return ( sample ) ;
}

static __bool i2cB_ack(void)
{
	IIC_SCLB_HIGH();
	IIC_SDAB_INPUT_SETUP();
	LCD_delay_us(5);
	LCD_delay_us(5);
	if(CHECK_SDAB_HIGH())
	{
		LCD_delay_us(5) ;
		IIC_SDAB_OUTPUT_SETUP();
		LCD_delay_us(5) ;
		IIC_SCLB_LOW();
		LCD_delay_us(5) ;
		IIC_SDAB_HIGH();
		LCD_delay_us(5) ;
		return(1);
	}
	else
	{
		LCD_delay_us(5) ;
		IIC_SDAB_OUTPUT_SETUP();
		LCD_delay_us(5) ;
		IIC_SCLB_LOW();
		LCD_delay_us(5) ;
		IIC_SDAB_HIGH();
		LCD_delay_us(5) ;
		return(0);
	}
}

//---------------------------------------------------------
static void i2cBStartA( void )
{
	IIC_SCLB_HIGH();
	IIC_SDAB_HIGH();
	LCD_delay_us(10) ;
	IIC_SDAB_LOW();
	LCD_delay_us(10) ;
	IIC_SCLB_LOW();
}

static __bool i2cBStart( void )
{
	IIC_SDAB_HIGH();
	IIC_SCLB_HIGH();
	LCD_delay_us(10) ;
	IIC_SDAB_INPUT_SETUP();
	if(CHECK_SDAB_HIGH())
	{
		IIC_SDAB_OUTPUT_SETUP();
		{
			i2cBStartA();
			return(1);
		}
	}
	return(0);
}

static void i2cBStop(void)
{
   IIC_SDAB_OUTPUT_SETUP();
   IIC_SDAB_LOW();
   LCD_delay_us(5) ;
   IIC_SCLB_HIGH();
   LCD_delay_us(5) ;
   IIC_SDAB_HIGH();
   LCD_delay_us(5) ;
}

//---------------------------------------------------------
static __bool i2cBTransmit(__u8 value)
{
	register __u8 i ;
	IIC_SDAB_OUTPUT_SETUP();
	LCD_delay_us(5) ;
	for ( i=0 ; i<8 ; i++ )
	{
		if((value&0x80)==0x80)
		{
			IIC_SDAB_HIGH();
			//printk("//////// DATA-1  //////\n");
		}
		else
		{
			IIC_SDAB_LOW();
			//printk("//////// DATA-0  //////\n");
		}
		value = value << 1 ;
		LCD_delay_us(10) ;
		i2cB_clock() ;

	}
	return(!i2cB_ack());
}

static __bool i2cBTransmitSubAddr(__u8 value)
{
	register __u8 i ;

	for ( i=0 ; i<8 ; i++ )
	{
		if((value&0x80)==0x80)
		{
			IIC_SDAB_HIGH();
		}
		else
		{
			IIC_SDAB_LOW();
		}
		value = value << 1 ;
		LCD_delay_us(10) ;
		i2cB_clock() ;
	}
	return(!i2cB_ack());
}

static __bool i2cBLocateSubAddr(__u8 slave_addr, __u8 sub_addr)
{
	register __u8 i;
	__u32 j;

	for (i=0; i<3; i++)
	{
		//Start I2C

		if (i2cBStart())
		{         //printk("-------------Start I2C OK-----------\n");

			if (i2cBTransmit(slave_addr))
			{  //printk("-------------SLAVE ADDR SEND OK-----------\n");



					if (i2cBTransmitSubAddr(sub_addr))
						 {
						  //printk("-------------ADDR SEND OK-----------\n");
						  return(1);
				     }
			}
		}
		i2cBStop();
	}

	return(0);
}

static __bool IIC_Write_forT101(__u8 slave_addr, __u8 sub_addr, __u8 value)
{

	__u32 i;
	if (i2cBLocateSubAddr(slave_addr, sub_addr))
	{
		if (i2cBTransmit(value))
		{
			i2cBStop();
			printk(KERN_WARNING"-------------DATA SEND OK-----------\n");
			return(1);
		}
	}
	i2cBStop();

printk(KERN_WARNING"-------------DATA SEND FAIL-----------\n");
	return(0);
}




i2cREAD()
{
	register __u8 i ;
	__u8  value = 0;

	  IIC_SDAB_INPUT_SETUP();

	  printk(KERN_WARNING"-------------IIC_Read_data-----------------\n");
for ( i=0 ; i<8 ; i++ )
	{
		value = value << 1;

	  //i2cB_clock() ;
	    LCD_delay_us(15) ;
	  	IIC_SCLB_HIGH();
	    LCD_delay_us(10) ;
	    IIC_SCLB_LOW();
	    LCD_delay_us(10) ;

	    LCD_delay_us(10) ;

	 if(CHECK_SDAB_HIGH())
	   value = value + 1;


  }


//	printk("value:value = %d\n",value);
	printk(KERN_WARNING"-------------read ok----------\n");
	i2cB_ack();
	i2cBStop();

}

static __bool IIC_Read_forT101(__u8 slave_addr1, __u8 sub_addr1)
{


	if (i2cBLocateSubAddr(slave_addr1, sub_addr1))

	   i2cREAD() ;


//i2cBLocateSubAddr(slave_addr, sub_addr);
//i2cBStop();
	return(0);
}


static void  LCD_io_init(__u32 sel)
{
    printk(KERN_WARNING"------+++++++++++++lcd init*************\n");
    //request SCLB gpio, and output high as default
    LCD_GPIO_request(sel, 0);
    LCD_GPIO_set_attr(sel, 0, 1);
    LCD_GPIO_write(sel, 0, 1);

    //request SDAB gpio, and output high as default
    LCD_GPIO_request(sel, 1);
    LCD_GPIO_set_attr(sel, 1, 1);
    LCD_GPIO_write(sel, 1, 1);
}

static void  LCD_io_exit(__u32 sel)
{
    printk(KERN_WARNING"------+++++++++++++lcd exit*************\n");
    //release SCLB gpio
    LCD_GPIO_release(sel, 0);

    //release SDAB gpio
    LCD_GPIO_release(sel, 1);
}

static void  LCD_open_cmd(__u32 sel)
{
	__u32 i;

	 printk(KERN_WARNING"------+++++++++++++into  T201_Initialize*************\n");

                                       printk(KERN_WARNING"-------------SEND1*************\n");
				       /*
 IIC_Write_forT101(0x6c, 0x2a,0xa2);
 //IIC_Read_forT101(0x6d, 0x2a);  printk("-------------SEND2*************\n"); //  IIC_Write_forT101(0x36, 0x2a,0xa2);
 IIC_Write_forT101(0x6c, 0x2d,0xc2);   printk("-------------SEND3*************\n"); //  IIC_Write_forT101(0x36, 0x2d,0xc2);
 IIC_Write_forT101(0x6c, 0x36,0x30);   printk("-------------SEND4*************\n"); //  IIC_Write_forT101(0x36, 0x36,0x30);
 IIC_Write_forT101(0x6c, 0x46,0x49);   printk("-------------SEND5*************\n"); //  IIC_Write_forT101(0x36, 0x46,0x49);
 IIC_Write_forT101(0x6c, 0x47,0x92);   printk("-------------SEND6*************\n"); //  IIC_Write_forT101(0x36, 0x47,0x92);
                                        //
 IIC_Write_forT101(0x6c, 0x48,0x00);   printk("-------------SEND7*************\n");  //  IIC_Write_forT101(0x36, 0x48,0x00);
 IIC_Write_forT101(0x6c, 0x5f,0x00);   printk("-------------SEND8*************\n");  //  IIC_Write_forT101(0x36, 0x5f,0x00);
 IIC_Write_forT101(0x6c, 0x60,0xa5);   printk("-------------SEND9*************\n");  //  IIC_Write_forT101(0x36, 0x60,0xa5);
 IIC_Write_forT101(0x6c, 0x61,0x08);   printk("-------------SEND10*************\n");  // IIC_Write_forT101(0x36, 0x61,0x08);
 IIC_Write_forT101(0x6c, 0x62,0xff);   printk("-------------SEND11*************\n");  // IIC_Write_forT101(0x36, 0x62,0xff);
                                        //
 IIC_Write_forT101(0x6c, 0x64,0x00);   printk("-------------SEND12*************\n"); // IIC_Write_forT101(0x36, 0x64,0x00);
 IIC_Write_forT101(0x6c, 0x80,0x01);   printk("-------------SEND13*************\n"); // IIC_Write_forT101(0x36, 0x80,0x01);
 IIC_Write_forT101(0x6c, 0x81,0xe4);   printk("-------------SEND14*************\n"); // IIC_Write_forT101(0x36, 0x81,0xe4);
 IIC_Write_forT101(0x6c, 0x2c,0x08);   printk("-------------SEND15*************\n"); // IIC_Write_forT101(0x36, 0x2c,0x08);
 IIC_Write_forT101(0x6c, 0x33,0x29);   printk("-------------SEND16*************\n"); // IIC_Write_forT101(0x36, 0x33,0x29);
                                       //
 IIC_Write_forT101(0x6c, 0x34,0x09);   printk("-------------SEND17*************\n"); // IIC_Write_forT101(0x36, 0x34,0x09);
 IIC_Write_forT101(0x6c, 0x3d,0x00);   printk("-------------SEND18*************\n"); // IIC_Write_forT101(0x36, 0x3d,0x00);
 IIC_Write_forT101(0x6c, 0x42,0x88);   printk("-------------SEND19*************\n"); // IIC_Write_forT101(0x36, 0x42,0x88);
 IIC_Write_forT101(0x6c, 0x43,0x86);   printk("-------------SEND20*************\n"); // IIC_Write_forT101(0x36, 0x43,0x86);
 IIC_Write_forT101(0x6c, 0x44,0x68);   printk("-------------SEND21*************\n"); // IIC_Write_forT101(0x36, 0x44,0x68);
                                       //
 IIC_Write_forT101(0x6c, 0x45,0x88);   printk("-------------SEND22*************\n"); // IIC_Write_forT101(0x36, 0x45,0x88);
 IIC_Write_forT101(0x6c, 0x46,0x96);   printk("-------------SEND23*************\n"); // IIC_Write_forT101(0x36, 0x46,0x96);
 IIC_Write_forT101(0x6c, 0x47,0x48);   printk("-------------SEND24*************\n"); // IIC_Write_forT101(0x36, 0x47,0x48);
 IIC_Write_forT101(0x6c, 0x48,0x04);                                          // IIC_Write_forT101(0x36, 0x48,0x04);

 */
IIC_Write_forT101(0x6c, 0x2a,0xa2);
IIC_Write_forT101(0x6c, 0x2d,0xc2);
IIC_Write_forT101(0x6c, 0x33,0x03);
IIC_Write_forT101(0x6c, 0x36,0x30);
IIC_Write_forT101(0x6c, 0x46,0x49);
IIC_Write_forT101(0x6c, 0x47,0x92);
IIC_Write_forT101(0x6c, 0x48,0x00);
IIC_Write_forT101(0x6c, 0x5f,0x00);
IIC_Write_forT101(0x6c, 0x60,0xa5);
IIC_Write_forT101(0x6c, 0x61,0x08);
IIC_Write_forT101(0x6c, 0x62,0xff);
IIC_Write_forT101(0x6c, 0x64,0x00);
IIC_Write_forT101(0x6c, 0x80,0x01);
IIC_Write_forT101(0x6c, 0x81,0xe4);
IIC_Write_forT101(0x6c, 0x34,0x01);

   printk(KERN_WARNING"-------------out  T201_Initialize*************\n");


}

static void  LCD_close_cmd(__u32 sel)
{
}
//sel: 0:lcd0; 1:lcd1
//para1 0:inter open 1:inter close 2:lense open 3:lense close
static __s32 LCD_user_defined_func(__u32 sel, __u32 para1, __u32 para2, __u32 para3)
{

   switch(para1)
{
	case 0:
IIC_Write_forT101(0x6c, 0x48,0x00);
break;
	case 1:
IIC_Write_forT101(0x6c, 0x48,0x03);
break;
	case 2:
IIC_Write_forT101(0x6c, 0x36,0x30);
break;
	case 3:
IIC_Write_forT101(0x6c, 0x36,0x00);
break;
	default:
break;
}
    return 0;
}

void LCD_get_panel_funs_0(__lcd_panel_fun_t * fun)
{
#ifdef LCD_PARA_USE_CONFIG
    fun->cfg_panel_info = LCD_cfg_panel_info;//delete this line if you want to use the lcd para define in sys_config1.fex
#endif
    fun->cfg_open_flow = LCD_open_flow;
    fun->cfg_close_flow = LCD_close_flow;
    fun->lcd_user_defined_func = LCD_user_defined_func;
}

