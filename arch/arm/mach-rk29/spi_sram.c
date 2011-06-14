
#include <mach/rk29_iomap.h>
#include <mach/board.h>
#include <mach/sram.h>
#include <asm/io.h>


#define SPI_KHZ (1000)
#define SPI_MHZ (1000*1000) 
#define GPLL_SPEED (24*SPI_MHZ)
#define SPI_SR_SPEED (2*SPI_MHZ)

#if defined(CONFIG_MACH_RK29_A22)||defined(CONFIG_MACH_RK29_PHONESDK)
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


#define CRU_CLKSEL6_CON	0x2C
#define CRU_CLKGATE2_CON	0x64
#define CRU_CLKGATE1_CON	0x60



#define spi_readl(offset) readl(SRAM_SPI_ADDRBASE + offset)
#define spi_writel(v, offset) writel(v, SRAM_SPI_ADDRBASE + offset)

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

/*unsigned int __sramdata spibase;
unsigned int __sramdata sram_spi_cs;
u32 __sramdata spi_base[2]={RK29_SPI0_BASE,RK29_SPI1_BASE};*/
u32 __sramdata spi_data[DATE_END]={};
#define sram_spi_dis()  spi_writel(spi_readl(SPIM_ENR)&~(0x1<<0),SPIM_ENR)
#define sram_spi_en()  spi_writel(spi_readl(SPIM_ENR)|(0x1<<0),SPIM_ENR)
#define sram_spi_cs_dis()  spi_writel(spi_readl(SPIM_SER)&~0x3,SPIM_SER)
#define sram_spi_cs_en()  spi_writel((spi_readl(SPIM_SER)&~0x3)|(0x1<<SRAM_SPI_CS),SPIM_SER);
#define sram_spi_busy() (spi_readl(SPIM_SR)&SR_BUSY)

#define wm831x_RD_MSK (0x1<<15)
#define wm831x_RD_VOID (0x7FFF)
#define spi_ctr0_mask 0x1fffc3



#if 0
void __sramfunc sram_printch(char byte);
void __sramfunc sram_printHX(unsigned int hex);
#else
#define sram_printch(a)
#define sram_printHX(a)
#endif

#define LOOPS_PER_USEC	13
#define LOOP(loops) do { int i = loops; barrier(); while (i--) barrier(); } while (0)

#define sram_udelay(usecs,a) LOOP((usecs)*LOOPS_PER_USEC)
/*
#define SRAM_ASM_LOOP_INSTRUCTION_NUM    8
static void __sramfunc sram_udelay(unsigned long usecs, unsigned long arm_freq_mhz)
{
	unsigned int cycle;

	cycle = usecs * arm_freq_mhz / SRAM_ASM_LOOP_INSTRUCTION_NUM;

	while (cycle--) {
		nop();
		nop();
		nop();
		barrier();
	}
}*/
	
/*void __sramfunc delay_test(int delay_time)
{
    int n = 100 * delay_time;
    while(n--)
    {
        __asm__ __volatile__("");
    }
}*/
	
#define SPI_GATE1_MASK 0xCF

void interface_ctr_reg_pread(void)
{
	unsigned int temp,temp2; 
	
	temp=readl(RK29_CRU_BASE + CRU_CLKGATE1_CON);
    temp2=readl(RK29_CRU_BASE + CRU_CLKGATE2_CON);

    writel(temp&~(SPI_GATE1_MASK),RK29_CRU_BASE + CRU_CLKGATE1_CON);
    writel(temp2&~(1<<(15+SRAM_SPI_CH)),RK29_CRU_BASE + CRU_CLKGATE2_CON);

    readl(SRAM_SPI_ADDRBASE);

    writel(temp2,RK29_CRU_BASE + CRU_CLKGATE2_CON);
    writel(temp,RK29_CRU_BASE + CRU_CLKGATE1_CON);

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
	
	sram_printch('V');
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
	spi_writel((spi_data[SPI_CTRLR0]&~0x1fffc3)|0x1<<11|(SRAM_SPI_DATA_BYTE),SPIM_CTRLR0);//spi setting
	spi_writel((spi_data[SPIM_BAUDR]&(~0xffff))|SRAM_SPI_DIV,SPIM_BAUDR);//setting spi speed
	spi_data[SPI_SER]=spi_readl(SPIM_SER);//spi cs
	
}

static void __sramfunc sram_spi_write(unsigned short add,unsigned short data){
  	unsigned int ret=0;

	sram_spi_cs_en();
	sram_udelay(10,24);
	sram_spi_en();
		
	spi_writel(add,SPIM_TXDR);
	spi_writel(data,SPIM_TXDR);
	//delay_test(100);

	sram_udelay(100,24);

	while(sram_spi_busy())
	{
		sram_udelay(1,24);
		sram_printch('B');
	}

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
	while(sram_spi_busy())
	{
		sram_udelay(1,24);
		sram_printch('B');
	}
	sram_udelay(100,24);

	ret1=spi_readl(SPIM_RXDR);
	ret=spi_readl(SPIM_RXDR); 

	while(sram_spi_busy())
	{
		sram_udelay(1,24);
		sram_printch('B');
	}
	
	sram_spi_dis();
	sram_udelay(10,24);
	sram_spi_cs_dis();
	return ret;
}


#if defined(CONFIG_MACH_RK29_A22)||defined(CONFIG_MACH_RK29_PHONESDK)

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
		sram_printHX(data_4003);//sleep ctr
	
		sram_printch('G');
		data_4003|=(0x1<<14);
		sram_spi_write(addr_4003,data_4003);// sleep
	
		data_4003=sram_spi_read(addr_4003|wm831x_RD_MSK,wm831x_RD_VOID);
		sram_printHX(data_4003);//sleep ctr
		sram_spi_deinit();
	#endif
} 
void __sramfunc rk29_suspend_voltage_resume(unsigned int vol)
{

	unsigned short addr_4003=0x4003;
	unsigned short data_4003;
	sram_spi_init();    //iomux  clk

	data_4003=sram_spi_read(addr_4003|wm831x_RD_MSK,wm831x_RD_VOID);
	sram_printHX(data_4003);//sleep ctr

	sram_printch('G');
	data_4003&=~(0x1<<14);
	sram_spi_write(addr_4003,data_4003);// sleep

	data_4003=sram_spi_read(addr_4003|wm831x_RD_MSK,wm831x_RD_VOID);
	sram_printHX(data_4003);//sleep ctr

	
	sram_spi_deinit();
	
}


#else
void interface_ctr_reg_pread(void)
{
}
unsigned int __sramfunc rk29_suspend_voltage_set(unsigned int vol)
{
}
void __sramfunc rk29_suspend_voltage_resume(unsigned int vol)
{
}
#endif

