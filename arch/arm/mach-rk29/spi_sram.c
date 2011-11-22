/***************************************spi **************************************************/
#include <mach/rk29_iomap.h>
#include <mach/board.h>
#include <mach/sram.h>
#include <mach/iomux.h>
#include <mach/cru.h>
#include <asm/io.h>
#include <mach/gpio.h>

#include <asm/vfp.h>

#if 1
void __sramfunc sram_printch(char byte);
void __sramfunc printhex(unsigned int hex);
#define sram_printHX(a)
#else
#define sram_printch(a)
#define sram_printHX(a)
#endif

#define grf_readl(offset) readl(RK29_GRF_BASE + offset)
#define grf_writel(v, offset) do { writel(v, RK29_GRF_BASE + offset); readl(RK29_GRF_BASE + offset); } while (0)

#define sram_udelay(usecs,a) LOOP((usecs)*LOOPS_PER_USEC)

#if defined(CONFIG_RK29_SPI_INSRAM)

#define SPI_KHZ (1000)
#define SPI_MHZ (1000*1000) 
#define GPLL_SPEED (24*SPI_MHZ)
#define SPI_SR_SPEED (2*SPI_MHZ)


#if defined(CONFIG_MACH_RK29_A22)||defined(CONFIG_MACH_RK29_PHONESDK)||defined(CONFIG_MACH_RK29_TD8801_V2)

#define SRAM_SPI_CH 1
#define SRAM_SPI_CS 1
#define SRAM_SPI_DATA_BYTE 2
#define SRAM_SPI_ADDRBASE RK29_SPI1_BASE//RK29_SPI0_BASE
#define SPI_SPEED (500*SPI_KHZ)
//#elif defined()
#else
#define SRAM_SPI_CH 1
#define SRAM_SPI_CS 1
#define SRAM_SPI_DATA_BYTE 2
#define SRAM_SPI_ADDRBASE RK29_SPI1_BASE//RK29_SPI0_BASE
#define SPI_SPEED (500*SPI_KHZ)
#endif

#define SRAM_SPI_SR_DIV (GPLL_SPEED/SPI_SR_SPEED-1)  //
#define SRAM_SPI_DIV (SPI_SR_SPEED/SPI_SPEED)
//#include <mach/spi_sram.h>

#define SPIM_ENR	0x0008
#define SPIM_SER	0x000C
#define SPIM_CTRLR0	0x0000
#define SPIM_BAUDR	0x0010
#define SPIM_TXFTLR	0x0014
#define SPIM_RXFLR	0x0020
#define cs1 1
#define cs0 0
#define spi1 1
#define spi0 0
#define SPIM_SR		0x0024

#define SPIM_IMR	0x002c
#define SPIM_TXDR	0x400
#define SPIM_RXDR	0x800
/* Bit fields in rxflr, */
#define RXFLR_MASK	(0x3f)
/* Bit fields in SR, 7 bits */
#define SR_MASK				0x7f		/* cover 7 bits */
#define SR_BUSY				(1 << 0)
#define SR_TF_FULL		    (1 << 1)
#define SR_TF_EMPT			(1 << 2)
#define SR_RF_EMPT		    (1 << 3)
#define SR_RF_FULL			(1 << 4)

#define PM_GETGPIO_BASE(N) RK29_GPIO##N##_BASE
#define PM_GPIO_DR 0
#define PM_GPIO_DDR 0x4
#define PM_GPIO_INTEN 0x30

#define wm831x_RD_MSK (0x1<<15)
#define wm831x_RD_VOID (0x7FFF)
#define spi_ctr0_mask 0x1fffc3



enum
{
GRF_IOM50=0,
GRF_IOM5c,
CLKGATE1,
CLKGATE2,	
CLKSEL6,
SPI_CTRLR0,
SPI_BAUDR,
SPI_SER,
DATE_END,
};
 static u32 __sramdata spi_data[DATE_END]={};

#define sram_spi_dis()  spi_writel(spi_readl(SPIM_ENR)&~(0x1<<0),SPIM_ENR)
#define sram_spi_en()  spi_writel(spi_readl(SPIM_ENR)|(0x1<<0),SPIM_ENR)
#define sram_spi_cs_dis()  spi_writel(spi_readl(SPIM_SER)&~0x3,SPIM_SER)
#define sram_spi_cs_en()  spi_writel((spi_readl(SPIM_SER)&~0x3)|(0x1<<SRAM_SPI_CS),SPIM_SER)
#define sram_spi_busy() (spi_readl(SPIM_SR)&SR_BUSY)

#define spi_readl(offset) readl(SRAM_SPI_ADDRBASE + offset)
#define spi_writel(v, offset) writel(v, SRAM_SPI_ADDRBASE+ offset)


#define SPI_GATE1_MASK 0xCF

void  interface_ctr_reg_pread()
{	
	unsigned int temp,temp2; 
	
	temp=readl(RK29_CRU_BASE + CRU_CLKGATE1_CON);
    temp2=readl(RK29_CRU_BASE + CRU_CLKGATE2_CON);

    writel(temp&~(SPI_GATE1_MASK),RK29_CRU_BASE + CRU_CLKGATE1_CON);
    writel(temp2&~(1<<(15+SRAM_SPI_CH)),RK29_CRU_BASE + CRU_CLKGATE2_CON);             //spi clock enable

    readl(SRAM_SPI_ADDRBASE);

    writel(temp2,RK29_CRU_BASE + CRU_CLKGATE2_CON);
    writel(temp,RK29_CRU_BASE + CRU_CLKGATE1_CON);
	readl(RK29_GPIO0_BASE);
	readl(RK29_GPIO1_BASE);
	readl(RK29_GPIO2_BASE);
	readl(RK29_GPIO3_BASE);
	readl(RK29_GPIO4_BASE);
	readl(RK29_GPIO5_BASE);
	readl(RK29_GPIO6_BASE);


}


static void  __sramfunc sram_spi_deinit(void)
{
	 sram_spi_dis();

	 spi_writel(spi_data[SPI_CTRLR0],SPIM_CTRLR0);	 
	 spi_writel(spi_data[SPI_BAUDR],SPIM_BAUDR);	  
	 spi_writel(spi_data[SPI_SER],SPIM_SER);	

	 
	 writel(spi_data[GRF_IOM5c],RK29_GRF_BASE + 0x5c);	 
	 writel(spi_data[GRF_IOM50],RK29_GRF_BASE +0x50);

	 writel(spi_data[CLKSEL6], RK29_CRU_BASE + CRU_CLKSEL6_CON);
	 writel(spi_data[CLKGATE2],RK29_CRU_BASE + CRU_CLKGATE2_CON); 
	 writel(spi_data[CLKGATE1],RK29_CRU_BASE + CRU_CLKGATE1_CON);
}

static void __sramfunc sram_spi_init(void)
{
	
	//sram_printch('V');
	/***************prihp clk*******************/
	spi_data[CLKGATE1]=readl(RK29_CRU_BASE + CRU_CLKGATE1_CON);
	writel(spi_data[CLKGATE1]&~(SPI_GATE1_MASK),RK29_CRU_BASE + CRU_CLKGATE1_CON);

	/***************spi sr clk speed*******************/
	spi_data[CLKSEL6]=readl(RK29_CRU_BASE + CRU_CLKSEL6_CON);
	writel((spi_data[CLKSEL6]&~(0x7f<<(2+SRAM_SPI_CH*9)))|(SRAM_SPI_SR_DIV<<(2+SRAM_SPI_CH*9)),
		RK29_CRU_BASE + CRU_CLKSEL6_CON);//spi sr clk speed

	
	/***************spi clk enable*******************/
	spi_data[CLKGATE2]=readl(RK29_CRU_BASE + CRU_CLKGATE2_CON);
	writel(spi_data[CLKGATE2]&~(1<<(15+SRAM_SPI_CH)),RK29_CRU_BASE + CRU_CLKGATE2_CON);//spi clk enable
	
	/***************spi iomox*******************/
	spi_data[GRF_IOM50]=readl(RK29_GRF_BASE +0x50);
	spi_data[GRF_IOM5c]=readl(RK29_GRF_BASE +0x5c);
	if(SRAM_SPI_CS)
		writel((spi_data[GRF_IOM50]&~(0x3<<(8-2*SRAM_SPI_CH)))|(0x2<<(8-2*SRAM_SPI_CH)),RK29_GRF_BASE +0x50);    //spi cs1  iomux
	else
		writel((spi_data[GRF_IOM5c]&~(0x3<<(2+8*SRAM_SPI_CH)))|(0x1<<(2+8*SRAM_SPI_CH)),RK29_GRF_BASE +0x5c);   //spi cs0 iomux

	writel((spi_data[GRF_IOM5c]&~(0xf3<<(SRAM_SPI_CH*8)))
		|(0x51<<(SRAM_SPI_CH*8)),RK29_GRF_BASE +0x5c);	//spi clk\txd\rxd iomux 


	/***************spi ctr*******************/
	 
	//spibase=spi_base[ch];
	//sram_spi_cs=cs;

	sram_spi_dis();// disable spi
	
	spi_data[SPI_CTRLR0] = spi_readl(SPIM_CTRLR0); 
	spi_data[SPI_BAUDR] = spi_readl(SPIM_BAUDR);
	spi_writel((spi_data[SPI_CTRLR0]&~0x1fffc3)|0x1<<11|(SRAM_SPI_DATA_BYTE),SPIM_CTRLR0);//spi setting
	spi_writel((spi_data[SPI_BAUDR]&(~0xffff))|SRAM_SPI_DIV,SPIM_BAUDR);//setting spi speed
	spi_data[SPI_SER]=spi_readl(SPIM_SER);//spi cs
	
}

static void __sramfunc sram_spi_write(unsigned short add,unsigned short data)
{
	sram_spi_cs_en();
	sram_udelay(10,24);
	sram_spi_en();
		
	spi_writel(add,SPIM_TXDR);
	spi_writel(data,SPIM_TXDR);
	//delay_test(100);

	sram_udelay(100,24);
 
	//while(sram_spi_busy())
	//{
		sram_udelay(1,24);
		//sram_printch('B');
	//}
 
	sram_spi_dis();
	sram_udelay(10,24);
	sram_spi_cs_dis();
}

static unsigned short __sramfunc sram_spi_read(unsigned short add,unsigned short data){
  	unsigned short ret=-1,ret1;
	
	sram_spi_cs_en();
	sram_udelay(10,24);
	sram_spi_en();
	
	spi_writel(add,SPIM_TXDR);
	//delay_test(100);
	spi_writel(data,SPIM_TXDR);
 
	//while(sram_spi_busy())
	//{
		sram_udelay(1,24);
		//sram_printch('B');
	//}
 
	sram_udelay(100,24);

	ret1=spi_readl(SPIM_RXDR);
	ret=spi_readl(SPIM_RXDR); 
 
	//while(sram_spi_busy())
	//{
		sram_udelay(1,24);
		//sram_printch('B');
	//}
 
	sram_spi_dis();
	sram_udelay(10,24);
	sram_spi_cs_dis();
	return ret;
}


unsigned int __sramfunc rk29_suspend_voltage_set(unsigned int vol)
{
#if 0 //test
	unsigned short addr_4003=0x4003,addr_405d=0x405d,
		addr_4059=0x4059,addr_405e=0x405e,addr_4063=0x4063;
	unsigned short data_4003,data_405d,
		data_405e,data_4059,data_4063;

	sram_printch('s');
	sram_spi_init();
	sram_printch('\n');
	sram_printch('M');
	data_4059=sram_spi_read(addr_4059|wm831x_RD_MSK,wm831x_RD_VOID);
	sram_printHX(data_4059);//dc1 sleep

	data_405e=sram_spi_read(addr_405e|wm831x_RD_MSK,wm831x_RD_VOID);
	sram_printHX(data_405e);//dc2 sleep

	data_4063=sram_spi_read(addr_4063|wm831x_RD_MSK,wm831x_RD_VOID);
	sram_printHX(data_405e);//dc3 sleep

	
	
	data_4003=sram_spi_read(addr_4003|wm831x_RD_MSK,wm831x_RD_VOID);
	sram_printHX(data_4003);//sleep ctr
	
	



	sram_printch('N');

	data_4059=(data_4059&~(0x7f))|0x68;
	sram_spi_write(addr_4059,data_4059);//dc1 sleep 3.0v

	data_405e=(data_405e&~(0x7f))|0x28;//1.2 0x38 / 1.0 0x28,1.3 0x40
	sram_spi_write(addr_405e,data_405e);//dc2 sleep

	data_4063=(data_4063&~(0x7f))|0x56;
	sram_spi_write(addr_4063,data_4063);//dc3 sleep 1.8V



	sram_printch('J');
	data_4003|=(0x1<<14);
	sram_spi_write(addr_4003,data_4003);// sleep
	
	sram_printch('L');
	data_4059=sram_spi_read(addr_4059|wm831x_RD_MSK,wm831x_RD_VOID);
	sram_printHX(data_4059);
			
	data_405e=sram_spi_read(addr_405e|wm831x_RD_MSK,wm831x_RD_VOID);
	sram_printHX(data_405e);

	
	data_4063=sram_spi_read(addr_4063|wm831x_RD_MSK,wm831x_RD_VOID);
	sram_printHX(data_4063);
	
	data_4003=sram_spi_read(addr_4003|wm831x_RD_MSK,wm831x_RD_VOID);
	sram_printHX(data_4003);

		
		
	sram_spi_deinit();
	#else
		unsigned short addr_4003=0x4003;
		unsigned short data_4003;
		sram_spi_init();	//iomux  clk
		data_4003=sram_spi_read(addr_4003|wm831x_RD_MSK,wm831x_RD_VOID);
		//sram_printHX(data_4003);//sleep ctr
		//sram_printch('G'); 
		sram_spi_write(addr_4003,data_4003|(0x1<<14));// sleep 
		data_4003=sram_spi_read(addr_4003|wm831x_RD_MSK,wm831x_RD_VOID);
		//sram_printHX(data_4003);//sleep ctr
		sram_spi_deinit();
	#endif
	return 0;
}
void __sramfunc rk29_suspend_voltage_resume(unsigned int vol)
{

	unsigned short addr_4003=0x4003;
	unsigned short data_4003;
	sram_spi_init();    //iomux  clk

	data_4003=sram_spi_read(addr_4003|wm831x_RD_MSK,wm831x_RD_VOID);
	//sram_printHX(data_4003);//sleep ctr

	//sram_printch('G'); 
	sram_spi_write(addr_4003,data_4003&~(0x1<<14));
 
	data_4003=sram_spi_read(addr_4003|wm831x_RD_MSK,wm831x_RD_VOID);
 
	//sram_printHX(data_4003);//sleep ctr

 	sram_spi_deinit();
 
	sram_udelay(100000, 24);    
 

}
#endif
/*******************************************gpio*********************************************/
#ifdef CONFIG_RK29_CLK_SWITCH_TO_32K
#define PM_GETGPIO_BASE(N) RK29_GPIO##N##_BASE
#define PM_GPIO_DR 0
#define PM_GPIO_DDR 0x4
#define PM_GPIO_INTEN 0x30
__sramdata u32  pm_gpio_base[7]=
{
RK29_GPIO0_BASE,
RK29_GPIO1_BASE,
RK29_GPIO2_BASE,
RK29_GPIO3_BASE,
RK29_GPIO4_BASE,
RK29_GPIO5_BASE,
RK29_GPIO6_BASE
};

#define pm_gpio_out_low(gpio) pm_gpio_set((gpio),GPIO_OUT,GPIO_LOW)
#define pm_gpio_out_high(gpio) pm_gpio_set((gpio),GPIO_OUT,GPIO_HIGH)

void __sramfunc pm_gpio_set(unsigned gpio,eGPIOPinDirection_t direction,eGPIOPinLevel_t level)
{
	unsigned group,pin,value;
	group=gpio/32;
	pin=gpio%32;
	if(group>6||pin>31)
		return;
	
	if(direction==GPIO_OUT)
	{
		value=readl(pm_gpio_base[group]+PM_GPIO_DDR);
		value|=0x1<<pin;
		writel(value,pm_gpio_base[group]+PM_GPIO_DDR);

		value=readl(pm_gpio_base[group]+PM_GPIO_DR);
		
		if(level==GPIO_HIGH)
			value|=0x1<<pin;
		else
			value&=~(0x1<<pin);
		
		writel(value,pm_gpio_base[group]+PM_GPIO_DR);

		
	}
	else
	{
		value=readl(pm_gpio_base[group]+PM_GPIO_DDR);
		value&=~(0x1<<pin);
		writel(value,pm_gpio_base[group]+PM_GPIO_DDR);

	}
}
#endif
/*****************************************gpio ctr*********************************************/
#if defined(CONFIG_RK29_GPIO_SUSPEND)
#define GRF_GPIO0_DIR     0x000
#define GRF_GPIO1_DIR     0x004
#define GRF_GPIO2_DIR     0x008
#define GRF_GPIO3_DIR     0x00c
#define GRF_GPIO4_DIR     0x010
#define GRF_GPIO5_DIR     0x014


#define GRF_GPIO0_DO      0x018
#define GRF_GPIO1_DO      0x01c
#define GRF_GPIO2_DO      0x020
#define GRF_GPIO3_DO      0x024
#define GRF_GPIO4_DO      0x028
#define GRF_GPIO5_DO      0x02c

#define GRF_GPIO0_EN      0x030
#define GRF_GPIO1_EN      0x034
#define GRF_GPIO2_EN      0x038
#define GRF_GPIO3_EN      0x03c
#define GRF_GPIO4_EN      0x040
#define GRF_GPIO5_EN      0x044


#define GRF_GPIO0L_IOMUX  0x048
#define GRF_GPIO0H_IOMUX  0x04c
#define GRF_GPIO1L_IOMUX  0x050
#define GRF_GPIO1H_IOMUX  0x054
#define GRF_GPIO2L_IOMUX  0x058
#define GRF_GPIO2H_IOMUX  0x05c
#define GRF_GPIO3L_IOMUX  0x060
#define GRF_GPIO3H_IOMUX  0x064
#define GRF_GPIO4L_IOMUX  0x068
#define GRF_GPIO4H_IOMUX  0x06c
#define GRF_GPIO5L_IOMUX  0x070
#define GRF_GPIO5H_IOMUX  0x074

typedef struct GPIO_IOMUX
{
    unsigned int GPIOL_IOMUX;
    unsigned int GPIOH_IOMUX;
}GPIO_IOMUX_PM;

//GRF Registers
typedef  struct REG_FILE_GRF
{
   unsigned int GRF_GPIO_DIR[6];
   unsigned int GRF_GPIO_DO[6];
   unsigned int GRF_GPIO_EN[6];
   GPIO_IOMUX_PM GRF_GPIO_IOMUX[6];
   unsigned int GRF_GPIO_PULL[7];
} GRF_REG_SAVE;


static GRF_REG_SAVE  pm_grf;
int __sramdata crumode;
 u32 __sramdata gpio2_pull,gpio6_pull;
//static GRF_REG_SAVE __sramdata pm_grf;
static void  pm_keygpio_prepare(void)
{
	gpio6_pull = grf_readl(GRF_GPIO6_PULL);
	gpio2_pull = grf_readl(GRF_GPIO2_PULL);
}
 void  pm_keygpio_sdk_suspend(void)
{
    pm_keygpio_prepare();
	grf_writel(gpio6_pull|0x7f,GRF_GPIO6_PULL);//key pullup/pulldown disable
	grf_writel(gpio2_pull|0x00000f30,GRF_GPIO2_PULL);
}
 void  pm_keygpio_sdk_resume(void)
{
	grf_writel(gpio6_pull,GRF_GPIO6_PULL);//key pullup/pulldown enable
	grf_writel(gpio2_pull,GRF_GPIO2_PULL);
}
 void  pm_keygpio_a22_suspend(void)
{
    pm_keygpio_prepare();
	grf_writel(gpio6_pull|0x7f,GRF_GPIO6_PULL);//key pullup/pulldown disable
	grf_writel(gpio2_pull|0x00000900,GRF_GPIO2_PULL);
}
 void  pm_keygpio_a22_resume(void)
{
	grf_writel(gpio6_pull,GRF_GPIO6_PULL);//key pullup/pulldown enable
	grf_writel(gpio2_pull,GRF_GPIO2_PULL);
}


static void  pm_spi_gpio_prepare(void)
{
	pm_grf.GRF_GPIO_IOMUX[1].GPIOL_IOMUX = grf_readl(GRF_GPIO1L_IOMUX);
	pm_grf.GRF_GPIO_IOMUX[2].GPIOH_IOMUX = grf_readl(GRF_GPIO2H_IOMUX);

	pm_grf.GRF_GPIO_PULL[1] = grf_readl(GRF_GPIO1_PULL);
	pm_grf.GRF_GPIO_PULL[2] = grf_readl(GRF_GPIO2_PULL);

	pm_grf.GRF_GPIO_EN[1] = grf_readl(GRF_GPIO1_EN);
	pm_grf.GRF_GPIO_EN[2] = grf_readl(GRF_GPIO2_EN);
}

 void  pm_spi_gpio_suspend(void)
{
	int io1L_iomux;
	int io2H_iomux;
	int io1_pull,io2_pull;
	int io1_en,io2_en;

	pm_spi_gpio_prepare();

	io1L_iomux = grf_readl(GRF_GPIO1L_IOMUX);
	io2H_iomux = grf_readl(GRF_GPIO2H_IOMUX);

	grf_writel(io1L_iomux&(~((0x03<<6)|(0x03 <<8))), GRF_GPIO1L_IOMUX);
	grf_writel(io2H_iomux&0xffff0000, GRF_GPIO2H_IOMUX);

	io1_pull = grf_readl(GRF_GPIO1_PULL);
	io2_pull = grf_readl(GRF_GPIO2_PULL);

	grf_writel(io1_pull|0x18,GRF_GPIO1_PULL);
	grf_writel(io2_pull|0x00ff0000,GRF_GPIO2_PULL);

	io1_en = grf_readl(GRF_GPIO1_EN);
	io2_en = grf_readl(GRF_GPIO2_EN);

	grf_writel(io1_en|0x18,GRF_GPIO1_EN);
	grf_writel(io2_en|0x00ff0000,GRF_GPIO2_EN);
}

 void  pm_spi_gpio_resume(void)
{
	grf_writel(pm_grf.GRF_GPIO_EN[1],GRF_GPIO1_EN);
	grf_writel(pm_grf.GRF_GPIO_EN[2],GRF_GPIO2_EN);
	grf_writel(pm_grf.GRF_GPIO_PULL[1],GRF_GPIO1_PULL);
	grf_writel(pm_grf.GRF_GPIO_PULL[2],GRF_GPIO2_PULL);

	grf_writel(pm_grf.GRF_GPIO_IOMUX[1].GPIOL_IOMUX, GRF_GPIO1L_IOMUX);
	grf_writel(pm_grf.GRF_GPIO_IOMUX[2].GPIOH_IOMUX, GRF_GPIO2H_IOMUX);
}

void pm_gpio_suspend(void)
{
	pm_spi_gpio_suspend(); // spi  pullup/pulldown  disable....
	#if defined(CONFIG_MACH_RK29_PHONESDK)
	{	pm_keygpio_sdk_suspend();// key  pullup/pulldown  disable.....
	}
	#endif
	#if defined(CONFIG_MACH_RK29_A22)
	{	pm_keygpio_a22_suspend();// key  pullup/pulldown  disable.....
	}
	#endif
}
void pm_gpio_resume(void)
{
	pm_spi_gpio_resume(); // spi  pullup/pulldown  enable.....
	#if defined(CONFIG_MACH_RK29_PHONESDK)
	{	pm_keygpio_sdk_resume();// key  pullup/pulldown  enable.....
	}
	#endif
	#if defined(CONFIG_MACH_RK29_A22)
	{	pm_keygpio_a22_resume();// key  pullup/pulldown  enable.....
	}
	#endif
}
#else
void pm_gpio_suspend(void)
{}
void pm_gpio_resume(void)
{}
#endif
/*************************************neon powerdomain******************************/
#define vfpreg(_vfp_) #_vfp_

#define fmrx(_vfp_) ({			\
	u32 __v;			\
	asm("mrc p10, 7, %0, " vfpreg(_vfp_) ", cr0, 0 @ fmrx	%0, " #_vfp_	\
	    : "=r" (__v) : : "cc");	\
	__v;				\
 })

#define fmxr(_vfp_,_var_)		\
	asm("mcr p10, 7, %0, " vfpreg(_vfp_) ", cr0, 0 @ fmxr	" #_vfp_ ", %0"	\
	   : : "r" (_var_) : "cc")

#define pmu_read(offset)		readl(RK29_PMU_BASE + (offset))
#define pmu_write(offset, value)	writel((value), RK29_PMU_BASE + (offset))
#define PMU_PG_CON 0x10
extern void vfp_save_state(void *location, u32 fpexc);
extern void vfp_load_state(void *location, u32 fpexc);
 static u64 __sramdata saveptr[33]={};
void  neon_powerdomain_off(void)
{
	int ret,i=0;
	int *p;
	p=&saveptr;
	 unsigned int fpexc = fmrx(FPEXC);  //get neon Logic gate

	fmxr(FPEXC, fpexc | FPEXC_EN);  //open neon Logic gate
	for(i=0;i<34;i++){
	vfp_save_state(p,fpexc);                        //save neon reg,32 D reg,2 control reg
	p++;
	}
	fmxr(FPEXC, fpexc & ~FPEXC_EN);    //close neon Logic gate

	 ret=pmu_read(PMU_PG_CON);                   //get power domain state
	pmu_write(PMU_PG_CON,ret|(0x1<<1));          //powerdomain off neon

}
void   neon_powerdomain_on(void)
{
	int ret,i=0;
	int *p;
	p=&saveptr;

	ret=pmu_read(PMU_PG_CON);                   //get power domain state
	pmu_write(PMU_PG_CON,ret&~(0x1<<1));                //powerdomain on neon
	sram_udelay(5000,24);

	unsigned int fpexc = fmrx(FPEXC);              //get neon Logic gate
	fmxr(FPEXC, fpexc | FPEXC_EN);                   //open neon Logic gate
	for(i=0;i<34;i++){
	vfp_load_state(p,fpexc);   //recovery neon reg, 32 D reg,2 control reg
	p++;
	}
	fmxr(FPEXC, fpexc | FPEXC_EN);	    //open neon Logic gate

}




/*************************************************32k**************************************/

#ifdef CONFIG_RK29_CLK_SWITCH_TO_32K
//static int __sramdata crumode;
void __sramfunc pm_clk_switch_32k(void)
{
	int vol;
	sram_printch('7');

	#ifndef CONFIG_MACH_RK29_A22
	pm_gpio_out_high(RK29_PIN4_PC0);
	#endif
	//sram_udelay(10,30);

	crumode=cru_readl(CRU_MODE_CON); //24M to 27M
	cru_writel((crumode&(~0x7fff))|0x2baa, CRU_MODE_CON);
	//sram_udelay(10,30);

//	pm_gpio_iomux(RK29_PIN4_PC5,0x0);// disable 24
	pm_gpio_out_high(RK29_PIN4_PC5);

	//sram_udelay(10,30);
	dsb();
	asm("wfi");
	
	pm_gpio_out_low(RK29_PIN4_PC5);//enable 24M 
	sram_udelay(20,24);             //the system clock is 32.768K 
	cru_writel(crumode, CRU_MODE_CON); //externel clk 24M

	#ifndef CONFIG_MACH_RK29_A22
	pm_gpio_out_low(RK29_PIN4_PC0); //enable 27M
	#endif
	//sram_udelay(1000,27);
	sram_printch('7');


}

#else
void __sramfunc pm_clk_switch(void)
{

}
#endif
