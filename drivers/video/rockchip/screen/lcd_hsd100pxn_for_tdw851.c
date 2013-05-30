/* This Lcd Driver is HSD070IDW1 write by cst 2009.10.27 */
#include <linux/delay.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>

/* Base */
#define SCREEN_TYPE		SCREEN_RGB
#define LVDS_FORMAT      	LVDS_8BIT_2
#define OUT_FACE		OUT_P888//OUT_D888_P666        //OUT_D888_P565
#define DCLK			24000000
#define LCDC_ACLK       	456000000       //29 lcdc axi DMA ÆµÂÊ

/* Timing */
#define H_PW			10
#define H_BP			10
#define H_VD			480
#define H_FP			12

#define V_PW			4
#define V_BP			4
#define V_VD			800
#define V_FP			8

/* Other */
#define DCLK_POL		1
#define DEN_POL			0
#define VSYNC_POL		0
#define HSYNC_POL		0

#define SWAP_RB			0
#define SWAP_RG			0
#define SWAP_GB			0


#define LCD_WIDTH       68//800    //need modify
#define LCD_HEIGHT      112//480

static struct rk29lcd_info *gLcd_info = NULL;

#define RK_SCREEN_INIT             //this screen need to init

#define TXD_PORT        gLcd_info->txd_pin
#define CLK_PORT        gLcd_info->clk_pin
#define CS_PORT         gLcd_info->cs_pin
#define RST_PORT        gLcd_info->reset_pin


#define CS_OUT()        gpio_direction_output(CS_PORT, 1)
#define CS_SET()        gpio_set_value(CS_PORT, GPIO_HIGH)
#define CS_CLR()        gpio_set_value(CS_PORT, GPIO_LOW)

#define CLK_OUT()       gpio_direction_output(CLK_PORT, 0) 
#define CLK_SET()       gpio_set_value(CLK_PORT, GPIO_HIGH)
#define CLK_CLR()       gpio_set_value(CLK_PORT, GPIO_LOW)

#define TXD_OUT()       gpio_direction_output(TXD_PORT, 1)   
#define TXD_SET()       gpio_set_value(TXD_PORT, GPIO_HIGH)
#define TXD_CLR()       gpio_set_value(TXD_PORT, GPIO_LOW)

#define RST_OUT()       gpio_direction_output(RST_PORT, 1)   
#define RST_SET()       gpio_set_value(RST_PORT, GPIO_HIGH)
#define RST_CLR()       gpio_set_value(RST_PORT, GPIO_LOW)

#define UDELAY_TIME     1
#define MDELAY_TIME     120
void Spi_Write_index(unsigned char index)
{
	int  j;
	CS_CLR();
	TXD_CLR();	//0
	udelay(UDELAY_TIME);
	  	
	CLK_CLR();
	udelay(3);//
        
	CLK_SET();        
	udelay(UDELAY_TIME);

	TXD_CLR();
	CLK_CLR();
		
	for(j=0;j<8;j++)
	{
		if(index&0x80)
		{
			TXD_SET();	
		}
		else
		{
			TXD_CLR();
		}
		index<<=1;	
			
		CLK_CLR();     
		udelay(UDELAY_TIME);
		CLK_SET();
		udelay(UDELAY_TIME);	
	}
	CS_SET();	
}

void Spi_Write_data(unsigned char data)
{
	int j;
	CS_CLR();
	TXD_SET();	
	udelay(UDELAY_TIME);
	  	
	CLK_CLR();
	udelay(3);
        
	CLK_SET();
	udelay(UDELAY_TIME);
		
	TXD_CLR();
	CLK_CLR();
		
	for(j=0;j<8;j++)
	{
		if(data&0x80)
		{
			TXD_SET();	
		}
		else
		{
			TXD_CLR();
		}
		data<<=1;	
			
		CLK_CLR();     
		udelay(UDELAY_TIME);
		CLK_SET();
		udelay(UDELAY_TIME);	
	}
	CS_SET();
}

void  Lcd_WriteSpi_initial3(void)	//HX8363A+IVO 20111128 canshu
{
	//FOR IVO5.2 + HX8363-A
	//Set_EXTC
	printk("Lcd_WriteSpi_initial3-------------\n");
	Spi_Write_index(0xB9);
	Spi_Write_data(0xFF);
	Spi_Write_data(0x83);
	Spi_Write_data(0x63);

	//Set_VCOM
	Spi_Write_index(0xB6);
	Spi_Write_data(0x27);//09


	//Set_POWER
	Spi_Write_index(0xB1);
	Spi_Write_data(0x81);
	Spi_Write_data(0x30);
	Spi_Write_data(0x07);//04
	Spi_Write_data(0x33);
	Spi_Write_data(0x02);
	Spi_Write_data(0x13);
	Spi_Write_data(0x11);
	Spi_Write_data(0x00);
	Spi_Write_data(0x24);
	Spi_Write_data(0x2B);
	Spi_Write_data(0x3F);
	Spi_Write_data(0x3F);

	Spi_Write_index(0xBf);   //
	Spi_Write_data(0x00); 
	Spi_Write_data(0x10); 

	//Sleep Out
	Spi_Write_index(0x11);
	mdelay(MDELAY_TIME);


	//Set COLMOD
	Spi_Write_index(0x3A);
	Spi_Write_data(0x70);


	//Set_RGBIF
	Spi_Write_index(0xB3);
	Spi_Write_data(0x01);


	//Set_CYC
	Spi_Write_index(0xB4);
	Spi_Write_data(0x08);
	Spi_Write_data(0x16);
	Spi_Write_data(0x5C);
	Spi_Write_data(0x0B);
	Spi_Write_data(0x01);
	Spi_Write_data(0x1E);
	Spi_Write_data(0x7B);
	Spi_Write_data(0x01);
	Spi_Write_data(0x4D);

	//Set_PANEL
	Spi_Write_index(0xCC);
	//Spi_Write_data(0x01);
	Spi_Write_data(0x09);
	mdelay(5);


	//Set Gamma 2.2
	Spi_Write_index(0xE0);
	Spi_Write_data(0x00);
	Spi_Write_data(0x1E);
	Spi_Write_data(0x63);
	Spi_Write_data(0x15);
	Spi_Write_data(0x11);
	Spi_Write_data(0x30);
	Spi_Write_data(0x0C);
	Spi_Write_data(0x8F);
	Spi_Write_data(0x8F);
	Spi_Write_data(0x15);
	Spi_Write_data(0x17);
	Spi_Write_data(0xD5);
	Spi_Write_data(0x56);
	Spi_Write_data(0x0e);
	Spi_Write_data(0x15);
	Spi_Write_data(0x00);
	Spi_Write_data(0x1E);
	Spi_Write_data(0x63);
	Spi_Write_data(0x15);
	Spi_Write_data(0x11);
	Spi_Write_data(0x30);
	Spi_Write_data(0x0C);
	Spi_Write_data(0x8F);
	Spi_Write_data(0x8F);
	Spi_Write_data(0x15);
	Spi_Write_data(0x17);
	Spi_Write_data(0xD5);
	Spi_Write_data(0x56);
	Spi_Write_data(0x0e);
	Spi_Write_data(0x15);
	mdelay(5);
	
	//Display On
	Spi_Write_index(0x29);
	Spi_Write_index(0x2c);
}


static int rk_lcd_init(void)
{
	if(gLcd_info)
		gLcd_info->io_init();

	TXD_OUT();
	CLK_OUT();
	CS_OUT();

	RST_CLR();
	CS_SET();
	CLK_SET();

	mdelay(5);
	RST_SET();
	mdelay(2);

	Lcd_WriteSpi_initial3();

	return 0;
}
static int deinit(void)
{
	Spi_Write_index(0x10);
	if(gLcd_info)
		gLcd_info->io_deinit();
        return 0;

}
static int rk_lcd_standby(u8 enable)
{
        if(!enable)
                rk_lcd_init();
        else
                deinit();
        return 0;
}

