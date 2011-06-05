
#include <mach/spi_sram.h>

unsigned __sramdata  int spibase;
u32 __sramdata spi_base[2]={RK29_SPI0_BASE,RK29_SPI1_BASE};
u32 __sramdata spi_data[6]={};

//chcs : &0xf0->ch(spi1spi0), &ox0f->cs(cs1cs0)

void __sramfunc delay_test(int delay_time)
{
    int n = 100 * delay_time;
    while(n--)
    {
        __asm__ __volatile__("");
    }
}

void  __sramfunc spi_reg_init(unsigned char size,unsigned char ch,unsigned char cs){
	unsigned int ret=0;
	
	spibase=spi_base[ch];
	ret=readl(spibase + SPIM_ENR);
	 writel(ret&~(0x1<<0), spibase + SPIM_ENR);  //disable SPI reg

	 ret = readl(spibase + SPIM_SER);
	writel(ret&~(0xffffffff)|(0x1<<cs), spibase + SPIM_SER);    //cs0\1  
	
	ret = readl(spibase + SPIM_CTRLR0);	
	spi_data[5]=ret&~(1<<18)&~(1<<13)&~(1<<8)&~(0x3<<1)|(1<<11);
	writel(spi_data[5]|size,spibase + SPIM_CTRLR0);             //  16\8\4 wei
	                                          
	ret=readl(spibase + SPIM_BAUDR);
	writel(ret&(~0xffff)|0x2,spibase + SPIM_BAUDR);          //sout clk

	ret=readl(spibase + SPIM_ENR);
	writel(ret|0x1, spibase + SPIM_ENR);                         //enable spi

}
void  __sramfunc spi_deinit(void){
	
	writel(spi_data[0],RK29_GRF_BASE + 0x5c);	
	writel(spi_data[1],RK29_GRF_BASE +0x50);
	writel(0, spibase + SPIM_ENR);  
	writel(spi_data[2], RK29_CRU_BASE + CRU_CLKSEL6_CON);
	writel(spi_data[3],RK29_CRU_BASE + CRU_CLKGATE2_CON); 
	writel(spi_data[4],RK29_CRU_BASE + CRU_CLKGATE1_CON);
	
}
 void __sramfunc spi_init( unsigned char ch,unsigned char cs){
	unsigned int ret=0;
	ret=readl(RK29_CRU_BASE + CRU_CLKGATE1_CON);
	spi_data[4]=ret;
	writel(ret&~(1<<3)&~(1<<2)&~(1<<1)&~(1<<0),RK29_CRU_BASE + CRU_CLKGATE1_CON);
	spi_data[3]=readl(RK29_CRU_BASE + CRU_CLKGATE2_CON);
	spi_data[0]=readl(RK29_GRF_BASE +0x5c);
	spi_data[2]=readl(RK29_CRU_BASE + CRU_CLKSEL6_CON);
	spi_data[1]=readl(RK29_GRF_BASE +0x50);
	writel(spi_data[3]&~(1<<(15+ch)),RK29_CRU_BASE + CRU_CLKGATE2_CON);   //spi0\1  clk enable
	writel(spi_data[1]&~(1<<(8-2*ch))|(1<<(9-2*ch)),RK29_GRF_BASE +0x50);    //sp0\1 cs1  iomux
	writel(spi_data[0]&~(1<<(3+8*ch))|(1<<(2+8*ch)),RK29_GRF_BASE +0x5c);   //spi0\1 cs0 iomux
	if(ch!=0){
		writel(spi_data[2]&~(0x7f<<11)|(0x0b<<11),RK29_CRU_BASE + CRU_CLKSEL6_CON);     //spi1 clk speed
		writel(spi_data[0]&~(1<<15)&~(1<<13)&~(1<<9)|(1<<14)|(1<<12)|(1<<8),RK29_GRF_BASE +0x5c);   //spi1 clk\txd\rxd iomux 
	}
	else{
		writel(spi_data[2]&~(0x7f<<2)|(0x0b<<2),RK29_CRU_BASE + CRU_CLKSEL6_CON);     //spi0 clk speed
		writel(spi_data[0]&~(1<<7)&~(1<<5)&~(1<<1)|(1<<6)|(1<<4)|(1<<0),RK29_GRF_BASE +0x5c);   //spi0 clk\txd\rxd iomux
	}
}

static void __sramfunc spi_write(unsigned int add,unsigned int data){
  	unsigned int ret=0;
	spi_reg_init(2,1,1);
	writel(add, spibase + SPIM_TXDR);
	delay_test(100);
	writel(data, spibase + SPIM_TXDR);
	delay_test(100);
	ret = readl(spibase + SPIM_SER);
	writel(ret&~(0xffffffff), spibase + SPIM_SER);    

}
 static unsigned int __sramfunc spi_read(unsigned int add,unsigned int data){
  	unsigned int ret=0,ret1=0;
	spi_reg_init(2,1,1);
	writel(add, spibase + SPIM_TXDR);
	delay_test(100);
	writel(data, spibase + SPIM_TXDR);
	delay_test(100);
	ret=readl(spibase + SPIM_RXDR); 
	//printhex(ret);
	//printch('\n');
	ret1=readl(spibase + SPIM_RXDR); 
	//printhex(ret1);
	//printch('\n');
	return ret1;
	ret = readl(spibase + SPIM_SER);
	writel(ret&~(0xffffffff), spibase + SPIM_SER);  
	
}
void __sramfunc rk29_wm831x_sleep_test(void){
     
	unsigned int data2 = 0xc003, data1 = 0x4128,ret = 0; 
	unsigned int addw2 = 0x4003,addw1 = 0x405d;
	unsigned int addr2= 0xc003,addr1= 0xc05d;
	unsigned int size=2,ch=1,cs=1;    //DATA SIZE IS 2^(size+2)  (16wei)   ch0\1 is spi0\1     
	//printch('\n');
	spi_init(ch,cs);    //iomux  clk
	spi_reg_init(2,1,1);  //spi reg     size, ch,cs
	spi_write(addw1,data1);
	ret=spi_read(addr1,data1);
	spi_write(addw2,data2);	
	spi_deinit();
		
} 
void __sramfunc rk29_wm831x_resume_test(void){
     
	unsigned int data4 = 0x8003, data3 = 0x4140,ret = 0; 
	unsigned int addw4 = 0x4003,addw3 = 0x405d;
	unsigned int addr4= 0xc003,addr3= 0xc05d;
	unsigned int size=2,ch=1,cs=1;
	//printch('\n');
	spi_init(ch,cs);          // clk iomux
	spi_reg_init(2,1,1);         //spi reg       size,ch,cs
	spi_write(addw3,data3);  
	ret=spi_read(addr3,data3);
	spi_write(addw4,data4);	
	spi_deinit();   
}


