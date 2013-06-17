
#ifndef __LCD_NT35580_H__
#define __LCD_NT35580_H__
#include <linux/delay.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>


/* Base */
#define SCREEN_TYPE		SCREEN_RGB
#define LVDS_FORMAT 		LVDS_8BIT_1
#define OUT_FACE		OUT_P888
#define DCLK			 24000000
#define LCDC_ACLK       	150000000     //29 lcdc axi DMA ÆµÂÊ

/* Timing */
#define H_PW			1
#define H_BP			1
#define H_VD			480
#define H_FP			2

#define V_PW			1
#define V_BP			4
#define V_VD			800
#define V_FP			2

#define LCD_WIDTH       480    //need modify
#define LCD_HEIGHT      800

/* Other */
#define DCLK_POL               0
#define DEN_POL			0
#define VSYNC_POL		0
#define HSYNC_POL		0

#define SWAP_RB			0
#define SWAP_RG			0
#define SWAP_GB			0


#define TXD_PORT        gLcd_info->txd_pin
#define CLK_PORT        gLcd_info->clk_pin
#define CS_PORT         gLcd_info->cs_pin

#define CS_OUT()        gpio_direction_output(CS_PORT, 0)
#define CS_SET()        gpio_set_value(CS_PORT, GPIO_HIGH)
#define CS_CLR()        gpio_set_value(CS_PORT, GPIO_LOW)
#define CLK_OUT()       gpio_direction_output(CLK_PORT, 0)
#define CLK_SET()       gpio_set_value(CLK_PORT, GPIO_HIGH)
#define CLK_CLR()       gpio_set_value(CLK_PORT, GPIO_LOW)
#define TXD_OUT()       gpio_direction_output(TXD_PORT, 0)
#define TXD_SET()       gpio_set_value(TXD_PORT, GPIO_HIGH)
#define TXD_CLR()       gpio_set_value(TXD_PORT, GPIO_LOW)
#define TXD_IN()        gpio_direction_input(TXD_PORT)
#define TXD_GET()       gpio_get_value(TXD_PORT)


#define delay_us(i)      udelay(i)
static struct rk29lcd_info *gLcd_info = NULL;

u32 spi_screenreg_get(u32 Addr)
{
    u32 i;
	u8 addr_h = (Addr>>8) & 0x000000ff;
	u8 addr_l = Addr & 0x000000ff;
	u8 cmd1 = 0x20;   //0010 0000
	u8 cmd2 = 0x00;   //0000 0000
	u8 cmd3 = 0x00;   //0000 0000

    u8 data_l = 0;
    u8 tmp;

    TXD_OUT();
    CLK_OUT();
    CS_OUT();
    delay_us(8);

    CS_SET();
    CLK_CLR();
    TXD_CLR();
    delay_us(4);

	// first transmit
	CS_CLR();
	delay_us(4);
	for(i = 0; i < 8; i++)
	{
		if(cmd1 &(1<<(7-i)))
			TXD_SET();
		else
			TXD_CLR();

		CLK_CLR();
		delay_us(4);
		CLK_SET();
		delay_us(4);
	}
	for(i = 0; i < 8; i++)
	{
		if(addr_h &(1<<(7-i)))
			TXD_SET();
		else
			TXD_CLR();

		CLK_CLR();
		delay_us(4);
		CLK_SET();
		delay_us(4);
	}
	CLK_CLR();
	TXD_CLR();
	delay_us(4);
	CS_SET();
	delay_us(8);

	// second transmit
	CS_CLR();
	delay_us(4);
	for(i = 0; i < 8; i++)
	{
		if(cmd2 &(1<<(7-i)))
			TXD_SET();
		else
			TXD_CLR();

		CLK_CLR();
		delay_us(4);
		CLK_SET();
		delay_us(4);
	}
	for(i = 0; i < 8; i++)
	{
		if(addr_l &(1<<(7-i)))
			TXD_SET();
		else
			TXD_CLR();

		CLK_CLR();
		delay_us(4);
		CLK_SET();
		delay_us(4);
	}
	CLK_CLR();
	TXD_CLR();
	delay_us(4);
	CS_SET();
	delay_us(8);

	// third transmit
	CS_CLR();
	delay_us(4);
	for(i = 0; i < 8; i++)
	{
		if(cmd3 &(1<<(7-i)))
			TXD_SET();
		else
			TXD_CLR();

		CLK_CLR();
		delay_us(4);
		CLK_SET();
		delay_us(4);
	}
	TXD_CLR();
	TXD_IN();
	for(i = 0; i < 8; i++)
	{
		CLK_CLR();
		delay_us(4);
		CLK_SET();

        tmp = TXD_GET();
        data_l += (tmp<<(7-i));

		delay_us(4);
	}
	CLK_CLR();
	TXD_CLR();
	delay_us(4);
	CS_SET();
	delay_us(8);

	return data_l;
}


void spi_screenreg_set(u32 Addr, u32 Data)
{
    u32 i;
	u8 addr_h = (Addr>>8) & 0x000000ff;
	u8 addr_l = Addr & 0x000000ff;
	u8 data_l = Data & 0x000000ff;
	u8 cmd1 = 0x20;   //0010 0000
	u8 cmd2 = 0x00;   //0000 0000
	u8 cmd3 = 0x40;   //0100 0000

    TXD_OUT();
    CLK_OUT();
    CS_OUT();
    delay_us(8);

    CS_SET();
    CLK_CLR();
    TXD_CLR();
    delay_us(4);

	// first transmit
	CS_CLR();
	delay_us(4);
	for(i = 0; i < 8; i++)
	{
		if(cmd1 &(1<<(7-i)))
			TXD_SET();
		else
			TXD_CLR();

		CLK_CLR();
		delay_us(4);
		CLK_SET();
		delay_us(4);
	}
	for(i = 0; i < 8; i++)
	{
		if(addr_h &(1<<(7-i)))
			TXD_SET();
		else
			TXD_CLR();

		CLK_CLR();
		delay_us(4);
		CLK_SET();
		delay_us(4);
	}
	CLK_CLR();
	TXD_CLR();
	delay_us(4);
	CS_SET();
	delay_us(8);

	// second transmit
	CS_CLR();
	delay_us(4);
	for(i = 0; i < 8; i++)
	{
		if(cmd2 &(1<<(7-i)))
			TXD_SET();
		else
			TXD_CLR();

		CLK_CLR();
		delay_us(4);
		CLK_SET();
		delay_us(4);
	}
	for(i = 0; i < 8; i++)
	{
		if(addr_l &(1<<(7-i)))
			TXD_SET();
		else
			TXD_CLR();

		CLK_CLR();
		delay_us(4);
		CLK_SET();
		delay_us(4);
	}
	CLK_CLR();
	TXD_CLR();
	delay_us(4);
	CS_SET();
	delay_us(8);

	// third transmit
	CS_CLR();
	delay_us(4);
	for(i = 0; i < 8; i++)
	{
		if(cmd3 &(1<<(7-i)))
			TXD_SET();
		else
			TXD_CLR();

		CLK_CLR();
		delay_us(4);
		CLK_SET();
		delay_us(4);
	}
	for(i = 0; i < 8; i++)
	{
		if(data_l &(1<<(7-i)))
			TXD_SET();
		else
			TXD_CLR();

		CLK_CLR();
		delay_us(4);
		CLK_SET();
		delay_us(4);
	}
	CLK_CLR();
	TXD_CLR();
	delay_us(4);
	CS_SET();
	delay_us(8);

    //printk("Addr=0x%04x, WData=0x%02x, RData=0x%02x \n", Addr, Data, spi_screenreg_get(Addr));

}





int rk_lcd_init(void)
{

#if 0
    GPIO_SetPinDirection(reset_pin, GPIO_OUT);
    GPIO_SetPinLevel(reset_pin,GPIO_HIGH);
    DelayMs_nops(100);
    GPIO_SetPinLevel(reset_pin,GPIO_LOW);
    DelayMs_nops(100);
    GPIO_SetPinLevel(reset_pin,GPIO_HIGH);
#endif

    if(gLcd_info)
        gLcd_info->io_init();

    spi_screenreg_set(0x2E80, 0x0001);
    spi_screenreg_set(0x0680, 0x002D);
    spi_screenreg_set(0xD380, 0x0004);
    spi_screenreg_set(0xD480, 0x0060);
    spi_screenreg_set(0xD580, 0x0007);
    spi_screenreg_set(0xD680, 0x005A);
    spi_screenreg_set(0xD080, 0x000F);
    spi_screenreg_set(0xD180, 0x0016);
    spi_screenreg_set(0xD280, 0x0004);
    spi_screenreg_set(0xDC80, 0x0004);
    spi_screenreg_set(0xD780, 0x0001);

    spi_screenreg_set(0x2280, 0x000F);
    spi_screenreg_set(0x2480, 0x0068);
    spi_screenreg_set(0x2580, 0x0000);
    spi_screenreg_set(0x2780, 0x00AF);

    spi_screenreg_set(0x3A00, 0x0060);
    spi_screenreg_set(0x3B00, 0x0003);
    spi_screenreg_set(0x3B02, 0x0005);
    spi_screenreg_set(0x3B03, 0x0002);
    spi_screenreg_set(0x3B04, 0x0002);
    spi_screenreg_set(0x3B05, 0x0002);

    spi_screenreg_set(0x0180, 0x0000);
    spi_screenreg_set(0x4080, 0x0051);
    spi_screenreg_set(0x4180, 0x0055);
    spi_screenreg_set(0x4280, 0x0058);
    spi_screenreg_set(0x4380, 0x0064);
    spi_screenreg_set(0x4480, 0x001A);
    spi_screenreg_set(0x4580, 0x002E);
    spi_screenreg_set(0x4680, 0x005F);
    spi_screenreg_set(0x4780, 0x0021);
    spi_screenreg_set(0x4880, 0x001C);
    spi_screenreg_set(0x4980, 0x0022);
    spi_screenreg_set(0x4A80, 0x005D);
    spi_screenreg_set(0x4B80, 0x0019);
    spi_screenreg_set(0x4C80, 0x0046);
    spi_screenreg_set(0x4D80, 0x0062);
    spi_screenreg_set(0x4E80, 0x0048);
    spi_screenreg_set(0x4F80, 0x005B);

    spi_screenreg_set(0x5080, 0x002F);
    spi_screenreg_set(0x5180, 0x005E);
    spi_screenreg_set(0x5880, 0x002E);
    spi_screenreg_set(0x5980, 0x003B);
    spi_screenreg_set(0x5A80, 0x008D);
    spi_screenreg_set(0x5B80, 0x00A7);
    spi_screenreg_set(0x5C80, 0x0027);
    spi_screenreg_set(0x5D80, 0x0039);
    spi_screenreg_set(0x5E80, 0x0065);
    spi_screenreg_set(0x5F80, 0x0055);

    spi_screenreg_set(0x6080, 0x001A);
    spi_screenreg_set(0x6180, 0x0021);
    spi_screenreg_set(0x6280, 0x008F);
    spi_screenreg_set(0x6380, 0x0022);
    spi_screenreg_set(0x6480, 0x0053);
    spi_screenreg_set(0x6580, 0x0066);
    spi_screenreg_set(0x6680, 0x008A);
    spi_screenreg_set(0x6780, 0x0097);
    spi_screenreg_set(0x6880, 0x001F);
    spi_screenreg_set(0x6980, 0x0026);

    spi_screenreg_set(0x1100, 0x0000);
    msleep(150);
    spi_screenreg_set(0x2900, 0x0000);

#if 0
    printk("spi_screenreg_set(0x5555, 0x0055)... \n");
    while(1) {
       spi_screenreg_set(0x5555, 0x0055);
       msleep(1);
    }
#endif

#if 0
    while(1) {
        int i = 0;
        for(i=0; i<400*480; i++)
            mcu_ioctl(MCU_WRDATA, 0xffffffff);
        for(i=0; i<400*480; i++)
            mcu_ioctl(MCU_WRDATA, 0x00000000);
        msleep(1000);
        printk(">>>>> MCU_WRDATA ...\n");

        for(i=0; i<400*480; i++)
            mcu_ioctl(MCU_WRDATA, 0x00000000);
        for(i=0; i<400*480; i++)
            mcu_ioctl(MCU_WRDATA, 0xffffffff);
        msleep(1000);
        printk(">>>>> MCU_WRDATA ...\n");
    }
#endif

    if(gLcd_info)
        gLcd_info->io_deinit();
    return 0;
}


int rk_lcd_standby(u8 enable)
{
    return 0;
}


#endif
