
#ifndef __LCD_NT35510__
#define __LCD_NT35510__

#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>

/* Base */
#define SCREEN_TYPE		SCREEN_RGB
#define OUT_FACE		OUT_P888
#define LVDS_FORMAT       	LVDS_8BIT_1
#define DCLK			26*1000*1000	//***27
#define LCDC_ACLK       	300000000     //29 lcdc axi DMA 频锟斤拷           //rk29

/* Timing */
#define H_PW			4 //8前消影
#define H_BP			8//6
#define H_VD			480//320	//***800 
#define H_FP			8//60

#define V_PW			4//12
#define V_BP			8// 4
#define V_VD			800//480	//***480
#define V_FP			8//40

#define LCD_WIDTH       57    //lcd size *mm
#define LCD_HEIGHT      94

/* Other */
#define DCLK_POL		1//0 
#define DEN_POL			0
#define VSYNC_POL		0
#define HSYNC_POL		0

#define SWAP_RB			0
#define SWAP_RG			0
#define SWAP_GB			0


static struct rk29lcd_info *gLcd_info = NULL;

int rk_lcd_init(void);
int rk_lcd_standby(u8 enable);

#define TXD_PORT        gLcd_info->txd_pin
#define CLK_PORT        gLcd_info->clk_pin
#define CS_PORT         gLcd_info->cs_pin

#define CS_OUT()        gpio_direction_output(CS_PORT, 1)
#define CS_SET()        gpio_set_value(CS_PORT, GPIO_HIGH)
#define CS_CLR()        gpio_set_value(CS_PORT, GPIO_LOW)
#define CLK_OUT()       gpio_direction_output(CLK_PORT, 0) 
#define CLK_SET()       gpio_set_value(CLK_PORT, GPIO_HIGH)
#define CLK_CLR()       gpio_set_value(CLK_PORT, GPIO_LOW)
#define TXD_OUT()       gpio_direction_output(TXD_PORT, 1) 
#define TXD_SET()       gpio_set_value(TXD_PORT, GPIO_HIGH)
#define TXD_CLR()       gpio_set_value(TXD_PORT, GPIO_LOW)


#define DRVDelayUs(i)   udelay(i*4)
#if 0
void spi_screenreg_cmd(u8 Addr)
{
 u32 i;
    u32 control_bit;

    TXD_OUT();
    CLK_OUT();
    CS_OUT();
    DRVDelayUs(2);
    DRVDelayUs(2);

    CS_SET();
    TXD_SET();
    CLK_CLR();
    DRVDelayUs(30);

        CS_CLR();
        control_bit = 0x0000;
        Addr = (control_bit | Addr);//spi_screenreg_set(0x36, 0x0000, 0xffff);	
        //printk("addr is 0x%x \n", Addr); 
        for(i = 0; i < 9; i++)  //reg
        {
                if(Addr &(1<<(8-i)))
                        TXD_SET();
                else
                        TXD_CLR();

                // \u6a21\u62dfCLK
                CLK_SET();
                DRVDelayUs(2);
                CLK_CLR();
                DRVDelayUs(2);
        }

        CS_SET();
        TXD_SET();
        CLK_CLR();		
        DRVDelayUs(10);
}


void spi_screenreg_param(u8 Param)
{

	u32 i;
    u32 control_bit;

   CS_CLR();
 
        control_bit = 0x0100;
        Param = (control_bit | Param);
        //printk("data0 is 0x%x \n", Data); 
        for(i = 0; i < 9; i++)  //data
        {
                if(Param &(1<<(8-i)))
                        TXD_SET();
                else
                        TXD_CLR();

                // \u6a21\u62dfCLK
                CLK_SET();
                DRVDelayUs(2);
                CLK_CLR();
                DRVDelayUs(2);
        }

        CS_SET();
        CLK_CLR();
        TXD_CLR();
        DRVDelayUs(10);
}
#endif



void WriteCommand( int  Command)
{
	unsigned char i,count1, count2,count3,count4;
	count1= Command>>8;
	count2= Command;
	count3=0x20;//00100000   //写命令高位
	count4=0x00;//00000000   //写命令低位======具体请看IC的Datasheet
	CS_CLR();
	for(i=0;i<8;i++)
	{
		CLK_CLR();
		if (count3 & 0x80) TXD_SET();
		else             TXD_CLR();
		CLK_SET();
		count3<<=1;
	}

	for(i=0;i<8;i++)
	{
		CLK_CLR();
		if (count1 & 0x80) TXD_SET();
		else             TXD_CLR();
		CLK_SET();
		count1<<=1;
	}

	for(i=0;i<8;i++)
	{
		CLK_CLR();
		if (count4 & 0x80) TXD_SET();
		else             TXD_CLR();
		CLK_SET();
		count4<<=1;
	}
	
	for(i=0;i<8;i++)
	{
		CLK_CLR();
		if (count2 & 0x80) TXD_SET();
		else             TXD_CLR();
		CLK_SET();
		count2<<=1;
	}

	CS_SET();

}



void WriteParameter(char DH)
{
	unsigned char i, count1, count2,count3,count4;
	count1=DH>>8;
	count2=DH;
	count3=0x60;//写数据高位
	count4=0x40;//写数据低位

	CS_CLR();
	/*
	TXD_CLR();  CLK_CLR(); CLK_SET();  //WRITE
	TXD_SET(); CLK_CLR(); CLK_SET();  //DATA
	TXD_SET(); CLK_CLR(); CLK_SET(); //HIGH BYTE
	TXD_CLR(); CLK_CLR(); CLK_SET();
	TXD_CLR(); CLK_CLR(); CLK_SET();
	TXD_CLR(); CLK_CLR(); CLK_SET();
	TXD_CLR(); CLK_CLR(); CLK_SET();
	TXD_CLR(); CLK_CLR(); CLK_SET();
	*/
	/*
	//因为数据的高位基本是不用的，可以不传高位，直接传低位
	for(i=0;i<8;i++)
	{
	CLK_CLR();
	if (count3 & 0x80) TXD_SET();
	else             TXD_CLR();
	CLK_SET();
	count3<<=1;
	}

	for(i=0;i<8;i++)
	{
	CLK_CLR();
	if (count1 & 0x80) TXD_SET();
	else             TXD_CLR();
	CLK_SET();
	count1<<=1;
	}
	*/


	for(i=0;i<8;i++)
	{
		CLK_CLR();
		if (count4 & 0x80) TXD_SET();
		else             TXD_CLR();
		CLK_SET();
		count4<<=1;
	}

	for(i=0;i<8;i++)
	{
		CLK_CLR();
		if (count2 & 0x80) TXD_SET();
		else             TXD_CLR();
		CLK_SET();
		count2<<=1;
	}

	CS_SET();

}


void init_nt35510(void)
{
	WriteCommand(0X1100); 
	usleep_range(10*1000, 10*1000);

	WriteCommand(0X1300); 

	WriteCommand(0XF000); 
	WriteParameter(0x55);

	WriteCommand(0XF001); 
	WriteParameter(0xAA);

	WriteCommand(0XF002); 
	WriteParameter(0x52);

	WriteCommand(0XF003); 
	WriteParameter(0x08);

	WriteCommand(0XF004); 
	WriteParameter(0x01);

	//Gamma setting Red
	WriteCommand(0XD100);
	WriteParameter(0x00);

	WriteCommand(0XD101);
	WriteParameter(0x20);

	WriteCommand(0XD102);
	WriteParameter(0x00);

	WriteCommand(0XD103);
	WriteParameter(0x2B);

	WriteCommand(0XD104);
	WriteParameter(0x00);

	WriteCommand(0XD105);
	WriteParameter(0x3C);

	WriteCommand(0XD106);
	WriteParameter(0x00);

	WriteCommand(0XD107);
	WriteParameter(0x56);

	WriteCommand(0XD108);
	WriteParameter(0x00);

	WriteCommand(0XD109);
	WriteParameter(0x68);

	WriteCommand(0XD10a);
	WriteParameter(0x00);

	WriteCommand(0XD10b);
	WriteParameter(0x87);

	WriteCommand(0XD10c);
	WriteParameter(0x00);

	WriteCommand(0XD10d);
	WriteParameter(0x9E);

	WriteCommand(0XD10e);
	WriteParameter(0x00);

	WriteCommand(0XD10f);
	WriteParameter(0xC6);

	WriteCommand(0XD110);
	WriteParameter(0x00);

	WriteCommand(0XD111);
	WriteParameter(0xE4);

	WriteCommand(0XD112);
	WriteParameter(0x01);

	WriteCommand(0XD113);
	WriteParameter(0x12);

	WriteCommand(0XD114);
	WriteParameter(0x01);

	WriteCommand(0XD115);
	WriteParameter(0x37);

	WriteCommand(0XD116);
	WriteParameter(0x01);

	WriteCommand(0XD117);
	WriteParameter(0x75);

	WriteCommand(0XD118);
	WriteParameter(0x01);

	WriteCommand(0XD119);
	WriteParameter(0xA5);

	WriteCommand(0XD11a);
	WriteParameter(0x01);

	WriteCommand(0XD11b);
	WriteParameter(0xA6);

	WriteCommand(0XD11c);
	WriteParameter(0x01);

	WriteCommand(0XD11d);
	WriteParameter(0xD0);

	WriteCommand(0XD11e);
	WriteParameter(0x01);

	WriteCommand(0XD11f);
	WriteParameter(0xF5);

	WriteCommand(0XD120);
	WriteParameter(0x02);

	WriteCommand(0XD121);
	WriteParameter(0x0A);

	WriteCommand(0XD122);
	WriteParameter(0x02);

	WriteCommand(0XD123);
	WriteParameter(0x26);

	WriteCommand(0XD124);
	WriteParameter(0x02);

	WriteCommand(0XD125);
	WriteParameter(0x3B);

	WriteCommand(0XD126);
	WriteParameter(0x02);

	WriteCommand(0XD127);
	WriteParameter(0x6B);

	WriteCommand(0XD128);
	WriteParameter(0x02);

	WriteCommand(0XD129);
	WriteParameter(0x99);

	WriteCommand(0XD12a);
	WriteParameter(0x02);

	WriteCommand(0XD12b);
	WriteParameter(0xDD);

	WriteCommand(0XD12C);
	WriteParameter(0x03);

	WriteCommand(0XD12D);
	WriteParameter(0x10);

	WriteCommand(0XD12E);
	WriteParameter(0x03);

	WriteCommand(0XD12F);
	WriteParameter(0x26);

	WriteCommand(0XD130);
	WriteParameter(0x03);

	WriteCommand(0XD131);
	WriteParameter(0x32);

	WriteCommand(0XD132);
	WriteParameter(0x03);

	WriteCommand(0XD133);
	WriteParameter(0x9A);

	//Gamma setting Green
	WriteCommand(0XD200);
	WriteParameter(0x00);

	WriteCommand(0XD201);
	WriteParameter(0xa0);

	WriteCommand(0XD202);
	WriteParameter(0x00);

	WriteCommand(0XD203);
	WriteParameter(0xa9);

	WriteCommand(0XD204);
	WriteParameter(0x00);

	WriteCommand(0XD205);
	WriteParameter(0xb5);

	WriteCommand(0XD206);
	WriteParameter(0x00);

	WriteCommand(0XD207);
	WriteParameter(0xbf);

	WriteCommand(0XD208);
	WriteParameter(0x00);

	WriteCommand(0XD209);
	WriteParameter(0xc9);

	WriteCommand(0XD20a);
	WriteParameter(0x00);

	WriteCommand(0XD20b);
	WriteParameter(0xdc);

	WriteCommand(0XD20c);
	WriteParameter(0x00);

	WriteCommand(0XD20d);
	WriteParameter(0xEE);

	WriteCommand(0XD20e);
	WriteParameter(0x01);

	WriteCommand(0XD20f);
	WriteParameter(0x0A);

	WriteCommand(0XD210);
	WriteParameter(0x01);

	WriteCommand(0XD211);
	WriteParameter(0x21);

	WriteCommand(0XD212);
	WriteParameter(0x01);

	WriteCommand(0XD213);
	WriteParameter(0x48);

	WriteCommand(0XD214);
	WriteParameter(0x01);

	WriteCommand(0XD215);
	WriteParameter(0x67);

	WriteCommand(0XD216);
	WriteParameter(0x01);

	WriteCommand(0XD217);
	WriteParameter(0x97);

	WriteCommand(0XD218);
	WriteParameter(0x01);

	WriteCommand(0XD219);
	WriteParameter(0xBE);

	WriteCommand(0XD21a);
	WriteParameter(0x01);

	WriteCommand(0XD21b);
	WriteParameter(0xC0);

	WriteCommand(0XD21c);
	WriteParameter(0x01);

	WriteCommand(0XD21d);
	WriteParameter(0xE1);

	WriteCommand(0XD21e);
	WriteParameter(0x02);

	WriteCommand(0XD21f);
	WriteParameter(0x04);

	WriteCommand(0XD220);
	WriteParameter(0x02);

	WriteCommand(0XD221);
	WriteParameter(0x17);

	WriteCommand(0XD222);
	WriteParameter(0x02);

	WriteCommand(0XD223);
	WriteParameter(0x36);

	WriteCommand(0XD224);
	WriteParameter(0x02);

	WriteCommand(0XD225);
	WriteParameter(0x50);

	WriteCommand(0XD226);
	WriteParameter(0x02);

	WriteCommand(0XD227);
	WriteParameter(0x7E);

	WriteCommand(0XD228);
	WriteParameter(0x02);

	WriteCommand(0XD229);
	WriteParameter(0xAC);

	WriteCommand(0XD22a);
	WriteParameter(0x02);

	WriteCommand(0XD22b);
	WriteParameter(0xF1);

	WriteCommand(0XD22C);
	WriteParameter(0x03);

	WriteCommand(0XD22D);
	WriteParameter(0x20);

	WriteCommand(0XD22E);
	WriteParameter(0x03);

	WriteCommand(0XD22F);
	WriteParameter(0x38);

	WriteCommand(0XD230);
	WriteParameter(0x03);

	WriteCommand(0XD231);
	WriteParameter(0x43);

	WriteCommand(0XD232);
	WriteParameter(0x03);

	WriteCommand(0XD233);
	WriteParameter(0x9A);


	//Gamma setting Blue
	WriteCommand(0XD300);
	WriteParameter(0x00);

	WriteCommand(0XD301);
	WriteParameter(0x50);

	WriteCommand(0XD302);
	WriteParameter(0x00);

	WriteCommand(0XD303);
	WriteParameter(0x53);

	WriteCommand(0XD304);
	WriteParameter(0x00);

	WriteCommand(0XD305);
	WriteParameter(0x73);

	WriteCommand(0XD306);
	WriteParameter(0x00);

	WriteCommand(0XD307);
	WriteParameter(0x89);

	WriteCommand(0XD308);
	WriteParameter(0x00);

	WriteCommand(0XD309);
	WriteParameter(0x9f);

	WriteCommand(0XD30a);
	WriteParameter(0x00);

	WriteCommand(0XD30b);
	WriteParameter(0xc1);

	WriteCommand(0XD30c);
	WriteParameter(0x00);

	WriteCommand(0XD30d);
	WriteParameter(0xda);

	WriteCommand(0XD30e);
	WriteParameter(0x01);

	WriteCommand(0XD30f);
	WriteParameter(0x02);

	WriteCommand(0XD310);
	WriteParameter(0x01);

	WriteCommand(0XD311);
	WriteParameter(0x23);

	WriteCommand(0XD312);
	WriteParameter(0x01);

	WriteCommand(0XD313);
	WriteParameter(0x50);

	WriteCommand(0XD314);
	WriteParameter(0x01);

	WriteCommand(0XD315);
	WriteParameter(0x6f);

	WriteCommand(0XD316);
	WriteParameter(0x01);

	WriteCommand(0XD317);
	WriteParameter(0x9f);

	WriteCommand(0XD318);
	WriteParameter(0x01);

	WriteCommand(0XD319);
	WriteParameter(0xc5);

	WriteCommand(0XD31a);
	WriteParameter(0x01);

	WriteCommand(0XD31b);
	WriteParameter(0xC6);

	WriteCommand(0XD31c);
	WriteParameter(0x01);

	WriteCommand(0XD31d);
	WriteParameter(0xE3);

	WriteCommand(0XD31e);
	WriteParameter(0x02);

	WriteCommand(0XD31f);
	WriteParameter(0x08);

	WriteCommand(0XD320);
	WriteParameter(0x02);

	WriteCommand(0XD321);
	WriteParameter(0x16);

	WriteCommand(0XD322);
	WriteParameter(0x02);

	WriteCommand(0XD323);
	WriteParameter(0x2b);

	WriteCommand(0XD324);
	WriteParameter(0x02);

	WriteCommand(0XD325);
	WriteParameter(0x4d);

	WriteCommand(0XD326);
	WriteParameter(0x02);

	WriteCommand(0XD327);
	WriteParameter(0x6f);

	WriteCommand(0XD328);
	WriteParameter(0x02);

	WriteCommand(0XD329);
	WriteParameter(0x8C);

	WriteCommand(0XD32a);
	WriteParameter(0x02);

	WriteCommand(0XD32b);
	WriteParameter(0xd6);

	WriteCommand(0XD32C);
	WriteParameter(0x03);

	WriteCommand(0XD32D);
	WriteParameter(0x12);

	WriteCommand(0XD32E);
	WriteParameter(0x03);

	WriteCommand(0XD32F);
	WriteParameter(0x28);

	WriteCommand(0XD330);
	WriteParameter(0x03);

	WriteCommand(0XD331);
	WriteParameter(0x3e);

	WriteCommand(0XD332);
	WriteParameter(0x03);

	WriteCommand(0XD333);
	WriteParameter(0x9A);

	//Gamma setting Red
	WriteCommand(0XD400);
	WriteParameter(0x00);

	WriteCommand(0XD401);
	WriteParameter(0x20);

	WriteCommand(0XD402);
	WriteParameter(0x00);

	WriteCommand(0XD403);
	WriteParameter(0x2b);

	WriteCommand(0XD404);
	WriteParameter(0x00);

	WriteCommand(0XD405);
	WriteParameter(0x3c);

	WriteCommand(0XD406);
	WriteParameter(0x00);

	WriteCommand(0XD407);
	WriteParameter(0x56);

	WriteCommand(0XD408);
	WriteParameter(0x00);

	WriteCommand(0XD409);
	WriteParameter(0x68);

	WriteCommand(0XD40a);
	WriteParameter(0x00);

	WriteCommand(0XD40b);
	WriteParameter(0x87);

	WriteCommand(0XD40c);
	WriteParameter(0x00);

	WriteCommand(0XD40d);
	WriteParameter(0x9e);

	WriteCommand(0XD40e);
	WriteParameter(0x00);

	WriteCommand(0XD40f);
	WriteParameter(0xc6);

	WriteCommand(0XD410);
	WriteParameter(0x00);

	WriteCommand(0XD411);
	WriteParameter(0xe4);

	WriteCommand(0XD412);
	WriteParameter(0x01);

	WriteCommand(0XD413);
	WriteParameter(0x12);

	WriteCommand(0XD414);
	WriteParameter(0x01);

	WriteCommand(0XD415);
	WriteParameter(0x37);

	WriteCommand(0XD416);
	WriteParameter(0x01);

	WriteCommand(0XD417);
	WriteParameter(0x75);

	WriteCommand(0XD418);
	WriteParameter(0x01);

	WriteCommand(0XD419);
	WriteParameter(0xa5);

	WriteCommand(0XD41a);
	WriteParameter(0x01);

	WriteCommand(0XD41b);
	WriteParameter(0xa6);

	WriteCommand(0XD41c);
	WriteParameter(0x01);

	WriteCommand(0XD41d);
	WriteParameter(0xd0);

	WriteCommand(0XD41e);
	WriteParameter(0x01);

	WriteCommand(0XD41f);
	WriteParameter(0xf5);

	WriteCommand(0XD420);
	WriteParameter(0x02);

	WriteCommand(0XD421);
	WriteParameter(0x0a);

	WriteCommand(0XD422);
	WriteParameter(0x02);

	WriteCommand(0XD423);
	WriteParameter(0x26);

	WriteCommand(0XD424);
	WriteParameter(0x02);

	WriteCommand(0XD425);
	WriteParameter(0x3b);

	WriteCommand(0XD426);
	WriteParameter(0x02);

	WriteCommand(0XD427);
	WriteParameter(0x6b);

	WriteCommand(0XD428);
	WriteParameter(0x02);

	WriteCommand(0XD429);
	WriteParameter(0x99);

	WriteCommand(0XD42a);
	WriteParameter(0x02);

	WriteCommand(0XD42b);
	WriteParameter(0xdd);

	WriteCommand(0XD42C);
	WriteParameter(0x03);

	WriteCommand(0XD42D);
	WriteParameter(0x10);

	WriteCommand(0XD42E);
	WriteParameter(0x03);

	WriteCommand(0XD42F);
	WriteParameter(0x26);

	WriteCommand(0XD430);
	WriteParameter(0x03);

	WriteCommand(0XD431);
	WriteParameter(0x32);

	WriteCommand(0XD432);
	WriteParameter(0x03);

	WriteCommand(0XD433);
	WriteParameter(0x9A);

	//Gamma setting Green
	WriteCommand(0XD500);
	WriteParameter(0x00);

	WriteCommand(0XD501);
	WriteParameter(0xa0);

	WriteCommand(0XD502);
	WriteParameter(0x00);

	WriteCommand(0XD503);
	WriteParameter(0xa9);

	WriteCommand(0XD504);
	WriteParameter(0x00);

	WriteCommand(0XD505);
	WriteParameter(0xb5);

	WriteCommand(0XD506);
	WriteParameter(0x00);

	WriteCommand(0XD507);
	WriteParameter(0xbf);

	WriteCommand(0XD508);
	WriteParameter(0x00);

	WriteCommand(0XD509);
	WriteParameter(0xc9);

	WriteCommand(0XD50a);
	WriteParameter(0x00);

	WriteCommand(0XD50b);
	WriteParameter(0xdc);

	WriteCommand(0XD50c);
	WriteParameter(0x00);

	WriteCommand(0XD50d);
	WriteParameter(0xee);

	WriteCommand(0XD50e);
	WriteParameter(0x01);

	WriteCommand(0XD50f);
	WriteParameter(0x0a);

	WriteCommand(0XD510);
	WriteParameter(0x01);

	WriteCommand(0XD511);
	WriteParameter(0x21);

	WriteCommand(0XD512);
	WriteParameter(0x01);

	WriteCommand(0XD513);
	WriteParameter(0x48);

	WriteCommand(0XD514);
	WriteParameter(0x01);

	WriteCommand(0XD515);
	WriteParameter(0x67);

	WriteCommand(0XD516);
	WriteParameter(0x01);

	WriteCommand(0XD517);
	WriteParameter(0x97);

	WriteCommand(0XD518);
	WriteParameter(0x01);

	WriteCommand(0XD519);
	WriteParameter(0xbe);

	WriteCommand(0XD51a);
	WriteParameter(0x01);

	WriteCommand(0XD51b);
	WriteParameter(0xc0);

	WriteCommand(0XD51c);
	WriteParameter(0x01);

	WriteCommand(0XD51d);
	WriteParameter(0xe1);

	WriteCommand(0XD51e);
	WriteParameter(0x02);

	WriteCommand(0XD51f);
	WriteParameter(0x04);

	WriteCommand(0XD520);
	WriteParameter(0x02);

	WriteCommand(0XD521);
	WriteParameter(0x17);

	WriteCommand(0XD522);
	WriteParameter(0x02);

	WriteCommand(0XD523);
	WriteParameter(0x36);

	WriteCommand(0XD524);
	WriteParameter(0x02);

	WriteCommand(0XD525);
	WriteParameter(0x50);

	WriteCommand(0XD526);
	WriteParameter(0x02);

	WriteCommand(0XD527);
	WriteParameter(0x7e);

	WriteCommand(0XD528);
	WriteParameter(0x02);

	WriteCommand(0XD529);
	WriteParameter(0xac);

	WriteCommand(0XD52a);
	WriteParameter(0x02);

	WriteCommand(0XD52b);
	WriteParameter(0xf1);

	WriteCommand(0XD52C);
	WriteParameter(0x03);

	WriteCommand(0XD52D);
	WriteParameter(0x20);

	WriteCommand(0XD52E);
	WriteParameter(0x03);

	WriteCommand(0XD52F);
	WriteParameter(0x38);

	WriteCommand(0XD530);
	WriteParameter(0x03);

	WriteCommand(0XD531);
	WriteParameter(0x43);

	WriteCommand(0XD532);
	WriteParameter(0x03);

	WriteCommand(0XD533);
	WriteParameter(0x9A);

	//Gamma setting Blue
	WriteCommand(0XD600);
	WriteParameter(0x00);

	WriteCommand(0XD601);
	WriteParameter(0x50);

	WriteCommand(0XD602);
	WriteParameter(0x00);

	WriteCommand(0XD603);
	WriteParameter(0x53);

	WriteCommand(0XD604);
	WriteParameter(0x00);

	WriteCommand(0XD605);
	WriteParameter(0x73);

	WriteCommand(0XD606);
	WriteParameter(0x00);

	WriteCommand(0XD607);
	WriteParameter(0x89);

	WriteCommand(0XD608);
	WriteParameter(0x00);

	WriteCommand(0XD609);
	WriteParameter(0x9f);

	WriteCommand(0XD60a);
	WriteParameter(0x00);

	WriteCommand(0XD60b);
	WriteParameter(0xc1);

	WriteCommand(0XD60c);
	WriteParameter(0x00);

	WriteCommand(0XD60d);
	WriteParameter(0xda);

	WriteCommand(0XD60e);
	WriteParameter(0x01);

	WriteCommand(0XD60f);
	WriteParameter(0x02);

	WriteCommand(0XD610);
	WriteParameter(0x01);

	WriteCommand(0XD611);
	WriteParameter(0x23);

	WriteCommand(0XD612);
	WriteParameter(0x01);

	WriteCommand(0XD613);
	WriteParameter(0x50);

	WriteCommand(0XD614);
	WriteParameter(0x01);

	WriteCommand(0XD615);
	WriteParameter(0x6f);

	WriteCommand(0XD616);
	WriteParameter(0x01);

	WriteCommand(0XD617);
	WriteParameter(0x9f);

	WriteCommand(0XD618);
	WriteParameter(0x01);

	WriteCommand(0XD619);
	WriteParameter(0xc5);

	WriteCommand(0XD61a);
	WriteParameter(0x01);

	WriteCommand(0XD61b);
	WriteParameter(0xc6);

	WriteCommand(0XD61c);
	WriteParameter(0x01);

	WriteCommand(0XD61d);
	WriteParameter(0xe3);

	WriteCommand(0XD61e);
	WriteParameter(0x02);

	WriteCommand(0XD61f);
	WriteParameter(0x08);

	WriteCommand(0XD620);
	WriteParameter(0x02);

	WriteCommand(0XD621);
	WriteParameter(0x16);

	WriteCommand(0XD622);
	WriteParameter(0x02);

	WriteCommand(0XD623);
	WriteParameter(0x2b);

	WriteCommand(0XD624);
	WriteParameter(0x02);

	WriteCommand(0XD625);
	WriteParameter(0x4d);

	WriteCommand(0XD626);
	WriteParameter(0x02);

	WriteCommand(0XD627);
	WriteParameter(0x6f);

	WriteCommand(0XD628);
	WriteParameter(0x02);

	WriteCommand(0XD629);
	WriteParameter(0x8c);

	WriteCommand(0XD62a);
	WriteParameter(0x02);

	WriteCommand(0XD62b);
	WriteParameter(0xd6);

	WriteCommand(0XD62C);
	WriteParameter(0x03);

	WriteCommand(0XD62D);
	WriteParameter(0x12);

	WriteCommand(0XD62E);
	WriteParameter(0x03);

	WriteCommand(0XD62F);
	WriteParameter(0x28);

	WriteCommand(0XD630);
	WriteParameter(0x03);

	WriteCommand(0XD631);
	WriteParameter(0x3e);

	WriteCommand(0XD632);
	WriteParameter(0x03);

	WriteCommand(0XD633);
	WriteParameter(0x9A);

	WriteCommand(0XBA00); 
	WriteParameter(0x14);

	WriteCommand(0XBA01); 
	WriteParameter(0x14);

	WriteCommand(0XBA02); 
	WriteParameter(0x14);

	WriteCommand(0XBF00); 
	WriteParameter(0x01);

	WriteCommand(0XB300); 
	WriteParameter(0x07);

	WriteCommand(0XB301); 
	WriteParameter(0x07);

	WriteCommand(0XB302); 
	WriteParameter(0x07);

	WriteCommand(0XB900); 
	WriteParameter(0x25);

	WriteCommand(0XB901); 
	WriteParameter(0x25);

	WriteCommand(0XB902); 
	WriteParameter(0x25);



	WriteCommand(0XBC01); 
	WriteParameter(0xA0);

	WriteCommand(0XBC02); 
	WriteParameter(0x00);

	WriteCommand(0XBD01); 
	WriteParameter(0xA0);

	WriteCommand(0XBD02); 
	WriteParameter(0x00);


	WriteCommand(0XF000); 
	WriteParameter(0x55);

	WriteCommand(0XF001); 
	WriteParameter(0xAA);

	WriteCommand(0XF002); 
	WriteParameter(0x52);

	WriteCommand(0XF003); 
	WriteParameter(0x08);

	WriteCommand(0XF004); 
	WriteParameter(0x00);

	WriteCommand(0XB100); 
	WriteParameter(0xCC);

	WriteCommand(0XBC00); 
	WriteParameter(0x05);

	WriteCommand(0XBC01); 
	WriteParameter(0x05);

	WriteCommand(0XBC02); 
	WriteParameter(0x05);


	WriteCommand(0XBD02); 
	WriteParameter(0x07);
	WriteCommand(0XBD03); 
	WriteParameter(0x31);

	WriteCommand(0XBE02); 
	WriteParameter(0x07);
	WriteCommand(0XBE03); 
	WriteParameter(0x31);

	WriteCommand(0XBF02); 
	WriteParameter(0x07);
	WriteCommand(0XBF03); 
	WriteParameter(0x31);
/*
	WriteCommand(0XFF00); 
	WriteParameter(0xAA);
	WriteCommand(0XFF01); 
	WriteParameter(0x55);
	WriteCommand(0XFF02); 
	WriteParameter(0x25);
	WriteCommand(0XFF03); 
	WriteParameter(0x01);
*/
/*****************************************************************/
	WriteCommand(0XF000);WriteParameter(0x55);//ENABLE  High Mode
	WriteCommand(0XF001);WriteParameter(0xAA);
	WriteCommand(0XF002);WriteParameter(0x52);
	WriteCommand(0XF003);WriteParameter(0x08);
	WriteCommand(0XF004);WriteParameter(0x00);
	
	WriteCommand(0XB400);WriteParameter(0x10);
	
	WriteCommand(0XFF00);WriteParameter(0xAA);//ENABLE LV3 
	WriteCommand(0XFF01);WriteParameter(0x55);
	WriteCommand(0XFF02);WriteParameter(0x25);
	WriteCommand(0XFF03);WriteParameter(0x01);

  WriteCommand(0XF900);WriteParameter(0x14);//中等增艳显示效果
	WriteCommand(0XF901);WriteParameter(0x00);
	WriteCommand(0XF902);WriteParameter(0x0A);
	WriteCommand(0XF903);WriteParameter(0x11);
	WriteCommand(0XF904);WriteParameter(0x17);
	WriteCommand(0XF905);WriteParameter(0x1D);
	WriteCommand(0XF906);WriteParameter(0x24);
	WriteCommand(0XF907);WriteParameter(0x2A);
	WriteCommand(0XF908);WriteParameter(0x31);
	WriteCommand(0XF909);WriteParameter(0x37);
	WriteCommand(0XF90A);WriteParameter(0x3D);
/*
	WriteCommand(0XF900);WriteParameter(0x14);//高等增艳显示效果
	WriteCommand(0XF901);WriteParameter(0x00);
	WriteCommand(0XF902);WriteParameter(0x0D);
	WriteCommand(0XF903);WriteParameter(0x1A);
	WriteCommand(0XF904);WriteParameter(0x26);
	WriteCommand(0XF905);WriteParameter(0x33);
	WriteCommand(0XF906);WriteParameter(0x40);
	WriteCommand(0XF907);WriteParameter(0x4D);
	WriteCommand(0XF908);WriteParameter(0x5A);
	WriteCommand(0XF909);WriteParameter(0x66);
	WriteCommand(0XF90A);WriteParameter(0x73);
*/
/******************************************************************/
	WriteCommand(0X3500); 
	WriteParameter(0x00);

	WriteCommand(0X3a00); 
	
if(OUT_FACE == OUT_P888)
	WriteParameter(0x70);	//24bit
else if(OUT_FACE == OUT_P666)
	WriteParameter(0x60);//18bit

	WriteCommand(0X3600); 
	WriteParameter(0x00);//R<->B

	WriteCommand(0X2000); //

	WriteCommand(0X1100); 
	usleep_range(120*1000, 120*1000);

	WriteCommand(0X2900); 

	usleep_range(100*1000, 100*1000);
	WriteCommand(0X2C00); 
}


void resume_nt35510(void)
{
	WriteCommand(0X1100); 
	msleep(120);

	WriteCommand(0X1300); 

	WriteCommand(0XF000); 
	WriteParameter(0x55);

	WriteCommand(0XF001); 
	WriteParameter(0xAA);

	WriteCommand(0XF002); 
	WriteParameter(0x52);

	WriteCommand(0XF003); 
	WriteParameter(0x08);

	WriteCommand(0XF004); 
	WriteParameter(0x01);


	/**************/
	WriteCommand(0XBA00); 
	WriteParameter(0x14);

	WriteCommand(0XBA01); 
	WriteParameter(0x14);

	WriteCommand(0XBA02); 
	WriteParameter(0x14);

	WriteCommand(0XBF00); 
	WriteParameter(0x01);

	WriteCommand(0XB300); 
	WriteParameter(0x07);

	WriteCommand(0XB301); 
	WriteParameter(0x07);

	WriteCommand(0XB302); 
	WriteParameter(0x07);

	WriteCommand(0XB900); 
	WriteParameter(0x25);

	WriteCommand(0XB901); 
	WriteParameter(0x25);

	WriteCommand(0XB902); 
	WriteParameter(0x25);



	WriteCommand(0XBC01); 
	WriteParameter(0xA0);

	WriteCommand(0XBC02); 
	WriteParameter(0x00);

	WriteCommand(0XBD01); 
	WriteParameter(0xA0);

	WriteCommand(0XBD02); 
	WriteParameter(0x00);


	WriteCommand(0XF000); 
	WriteParameter(0x55);

	WriteCommand(0XF001); 
	WriteParameter(0xAA);

	WriteCommand(0XF002); 
	WriteParameter(0x52);

	WriteCommand(0XF003); 
	WriteParameter(0x08);

	WriteCommand(0XF004); 
	WriteParameter(0x00);

	WriteCommand(0XB100); 
	WriteParameter(0xCC);

	WriteCommand(0XBC00); 
	WriteParameter(0x05);

	WriteCommand(0XBC01); 
	WriteParameter(0x05);

	WriteCommand(0XBC02); 
	WriteParameter(0x05);


	WriteCommand(0XBD02); 
	WriteParameter(0x07);
	WriteCommand(0XBD03); 
	WriteParameter(0x31);

	WriteCommand(0XBE02); 
	WriteParameter(0x07);
	WriteCommand(0XBE03); 
	WriteParameter(0x31);

	WriteCommand(0XBF02); 
	WriteParameter(0x07);
	WriteCommand(0XBF03); 
	WriteParameter(0x31);

	WriteCommand(0XFF00); 
	WriteParameter(0xAA);
	WriteCommand(0XFF01); 
	WriteParameter(0x55);
	WriteCommand(0XFF02); 
	WriteParameter(0x25);
	WriteCommand(0XFF03); 
	WriteParameter(0x01);


	WriteCommand(0X3500); 
	WriteParameter(0x00);

	WriteCommand(0X3a00); 
	
if(OUT_FACE == OUT_P888)
	WriteParameter(0x70);	//24bit
else if(OUT_FACE == OUT_P666)
	WriteParameter(0x60);//18bit

	WriteCommand(0X3600); 
	WriteParameter(0x00);//R<->B

	WriteCommand(0X2000); //

	WriteCommand(0X1100); 
	msleep(120);

	WriteCommand(0X2900); 

	msleep(100);
	WriteCommand(0X2C00); 
}

static DEFINE_MUTEX(lcd_mutex);
extern void rk29_lcd_spim_spin_lock(void);
extern void rk29_lcd_spim_spin_unlock(void);

static void lcd_resume(struct work_struct *work)
{
	mutex_lock(&lcd_mutex);
	rk29_lcd_spim_spin_lock();
	if(gLcd_info)
		gLcd_info->io_init();
	init_nt35510();
	//resume_nt35510();//may be fail to wake up LCD some time,so change to init lcd again
	printk(KERN_DEBUG "%s\n",__FUNCTION__);

	if(gLcd_info)
		gLcd_info->io_deinit();

	rk29_lcd_spim_spin_unlock();
	mutex_unlock(&lcd_mutex);
}

static DECLARE_WORK(lcd_resume_work, lcd_resume);
static struct workqueue_struct *lcd_resume_wq;

static void lcd_late_resume(struct early_suspend *h)
{
	queue_work(lcd_resume_wq, &lcd_resume_work);
}

static struct early_suspend lcd_early_suspend_desc = {
	.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 1, // before fb resume
	.resume = lcd_late_resume,
};

int rk_lcd_init(void)
{ 
	volatile u32 data;
	printk("lcd init...\n");
	if(gLcd_info)
	gLcd_info->io_init();
	init_nt35510();

	if(gLcd_info)
	gLcd_info->io_deinit();

	lcd_resume_wq = create_singlethread_workqueue("lcd");
	register_early_suspend(&lcd_early_suspend_desc);
    return 0;
}

int rk_lcd_standby(u8 enable)	//***enable =1 means suspend, 0 means resume 
{
	if (enable) {
		mutex_lock(&lcd_mutex);
		rk29_lcd_spim_spin_lock();
		if(gLcd_info)
			gLcd_info->io_init();

		WriteCommand(0X2800); 
		WriteCommand(0X1100); 
		msleep(5);
		WriteCommand(0X4f00); 
		WriteParameter(0x01);
		if(gLcd_info)
			gLcd_info->io_deinit();

		rk29_lcd_spim_spin_unlock();
		mutex_unlock(&lcd_mutex);
	} else {
		flush_workqueue(lcd_resume_wq);
	}
	return 0;
}

#endif
