
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/hardware/cache-l2x0.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/wakeup_reason.h>
#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <asm/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <linux/rockchip/cpu.h>
//#include <linux/rockchip/cru.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/iomap.h>
#include "pm.h"
#include <linux/irqchip/arm-gic.h>

#define CPU 3288
//#include "sram.h"
#include "pm-pie.c"

__weak void rk_usb_power_down(void);
__weak void rk_usb_power_up(void);

//static void ddr_pin_set_pull(u8 port,u8 bank,u8 b_gpio,u8 fun);
//static void ddr_gpio_set_in_output(u8 port,u8 bank,u8 b_gpio,u8 type);
static void ddr_pin_set_fun(u8 port,u8 bank,u8 b_gpio,u8 fun);


/*************************cru define********************************************/


#define RK3288_CRU_UNGATING_OPS(id) cru_writel(CRU_W_MSK_SETBITS(0,(id)%16,0x1),RK3288_CRU_GATEID_CONS((id)))
#define RK3288_CRU_GATING_OPS(id) cru_writel(CRU_W_MSK_SETBITS(1,(id)%16,0x1),RK3288_CRU_GATEID_CONS((id)))

enum rk_plls_id {
	APLL_ID = 0,
	DPLL_ID,
	CPLL_ID,
	GPLL_ID,
	NPLL_ID,
	END_PLL_ID,
};

#define RK3288_PLL_PWR_DN_MSK (0x1<<1)
#define RK3288_PLL_PWR_DN CRU_W_MSK_SETBITS(1,1,0x1)
#define RK3288_PLL_PWR_ON CRU_W_MSK_SETBITS(0,1,0x1)


#define RK3288_PLL_RESET		CRU_W_MSK_SETBITS(1,5,0x1)
#define RK3288_PLL_RESET_RESUME CRU_W_MSK_SETBITS(0,5,0x1)

#define RK3288_PLL_BYPASS_MSK (0x1<<0)
#define RK3288_PLL_BYPASS CRU_W_MSK_SETBITS(1,0,0x1)
#define RK3288_PLL_NO_BYPASS CRU_W_MSK_SETBITS(0,0,0x1)


/*******************************gpio define **********************************************/

/* GPIO control registers */
#define GPIO_SWPORT_DR		0x00
#define GPIO_SWPORT_DDR		0x04
#define GPIO_INTEN			0x30
#define GPIO_INTMASK		0x34
#define GPIO_INTTYPE_LEVEL	0x38
#define GPIO_INT_POLARITY	0x3c
#define GPIO_INT_STATUS		0x40
#define GPIO_INT_RAWSTATUS	0x44
#define GPIO_DEBOUNCE		0x48
#define GPIO_PORTS_EOI		0x4c
#define GPIO_EXT_PORT		0x50
#define GPIO_LS_SYNC		0x60

/***********************************sleep func*********************************************/

#define RKPM_BOOTRAM_PHYS (RK3288_BOOTRAM_PHYS)
#define RKPM_BOOTRAM_BASE (RK_BOOTRAM_VIRT)
#define RKPM_BOOTRAM_SIZE (RK3288_BOOTRAM_SIZE)

// sys resume code in boot ram
#define  RKPM_BOOT_CODE_PHY  (RKPM_BOOTRAM_PHYS+RKPM_BOOT_CODE_OFFSET)
#define  RKPM_BOOT_CODE_BASE  (RKPM_BOOTRAM_BASE+RKPM_BOOT_CODE_OFFSET)


// sys resume data in boot ram
#define  RKPM_BOOT_DATA_PHY  (RKPM_BOOTRAM_PHYS+RKPM_BOOT_DATA_OFFSET)
#define  RKPM_BOOT_DATA_BASE  (RKPM_BOOTRAM_BASE+RKPM_BOOT_DATA_OFFSET)

// ddr resume data in boot ram
#define  RKPM_BOOT_DDRCODE_PHY   (RKPM_BOOTRAM_PHYS + RKPM_BOOT_DDRCODE_OFFSET)
#define  RKPM_BOOT_DDRCODE_BASE  (RKPM_BOOTRAM_BASE+RKPM_BOOT_DDRCODE_OFFSET)

#define RKPM_BOOT_CPUSP_PHY (RKPM_BOOTRAM_PHYS+((RKPM_BOOTRAM_SIZE-1)&~0x7))

// the value is used to control cpu resume flow
static u32 sleep_resume_data[RKPM_BOOTDATA_ARR_SIZE];
static char *resume_data_base=(char *)( RKPM_BOOT_DATA_BASE);
static char *resume_data_phy=  (char *)( RKPM_BOOT_DATA_PHY);



#define BOOT_RAM_SIZE	(4*1024)
#define INT_RAM_SIZE		(64*1024)

static char boot_ram_data[BOOT_RAM_SIZE+4*10];
static char int_ram_data[INT_RAM_SIZE];

char * ddr_get_resume_code_info(u32 *size);
char * ddr_get_resume_data_info(u32 *size);

/**
ddr code and data

*** code start
---data offset-- 
---code----
---data----
*/
static void sram_data_for_sleep(char *boot_save, char *int_save,u32 flag)
{	
 	
	char *addr_base,*addr_phy,*data_src,*data_dst;
	u32 sr_size,data_size;

	addr_base=(char *)RKPM_BOOTRAM_BASE;
	addr_phy=(char *)RKPM_BOOTRAM_PHYS;
	sr_size=RKPM_BOOTRAM_SIZE;

 	// save boot sram
	 if(boot_save)
		 memcpy(boot_save,addr_base, sr_size);

	// move resume code and date to boot sram
	// move sys code
	data_dst=(char *)RKPM_BOOT_CODE_BASE;
	data_src=(char *)rkpm_slp_cpu_resume;
	data_size=RKPM_BOOT_CODE_SIZE;
	memcpy((char *)data_dst,(char *)data_src, data_size);

	// move sys data
	data_dst=(char *)resume_data_base;
	data_src=(char *)sleep_resume_data;
	data_size=sizeof(sleep_resume_data);
	memcpy((char *)data_dst,(char *)data_src, data_size);
            
        if(flag)
        {
                /*************************ddr code cpy*************************************/
                // ddr code
                data_dst=(char *)(char *)RKPM_BOOT_DDRCODE_BASE;
                data_src=(char *)ddr_get_resume_code_info(&data_size);

                data_size=RKPM_ALIGN(data_size,4);

                memcpy((char *)data_dst,(char *)data_src, data_size);

                // ddr data
                data_dst=(char *)(data_dst+data_size);

                data_src=(char *)ddr_get_resume_data_info(&data_size);
                data_size=RKPM_ALIGN(data_size,4);
                memcpy((char *)data_dst,(char *)data_src, data_size);

                /*************************ddr code cpy  end*************************************/
                flush_icache_range((unsigned long)addr_base, (unsigned long)addr_base + sr_size);
                outer_clean_range((phys_addr_t) addr_phy, (phys_addr_t)(addr_phy)+sr_size);
                /*************************int mem bak*************************************/
                // int mem
                addr_base=(char *)rockchip_sram_virt;
                addr_phy=(char *)pie_to_phys(rockchip_pie_chunk,(unsigned long )rockchip_sram_virt);
                sr_size=rockchip_sram_size;
                //  rkpm_ddr_printascii("piephy\n");
                //rkpm_ddr_printhex(addr_phy);
                //mmap
                if(int_save)
                    memcpy(int_save,addr_base, sr_size);

                flush_icache_range((unsigned long)addr_base, (unsigned long)addr_base + sr_size);
                outer_clean_range((phys_addr_t) addr_phy, (phys_addr_t)(addr_phy)+sr_size);
        }    
     
 }

static void sram_data_resume(char *boot_save, char *int_save,u32 flag)
{  
 
    char *addr_base,*addr_phy;
    u32 sr_size;
    
    addr_base=(char *)RKPM_BOOTRAM_BASE;
    addr_phy=(char *)RKPM_BOOTRAM_PHYS;
    sr_size=RKPM_BOOTRAM_SIZE;
    // save boot sram
    if(boot_save)
        memcpy(addr_base,boot_save, sr_size);

    flush_icache_range((unsigned long)addr_base, (unsigned long)addr_base + sr_size);
    outer_clean_range((phys_addr_t) addr_phy, (phys_addr_t)addr_phy+sr_size);

    if(flag)
    {
        // int mem
        addr_base=(char *)rockchip_sram_virt;
        addr_phy=(char *)pie_to_phys(rockchip_pie_chunk,(unsigned long )rockchip_sram_virt);
        sr_size=rockchip_sram_size;

        if(int_save)
        memcpy(addr_base, int_save,sr_size);

        flush_icache_range((unsigned long)addr_base, (unsigned long)addr_base + sr_size);
        outer_clean_range((phys_addr_t) addr_phy,(unsigned long)addr_phy+sr_size);
     }
}

/**************************************gic save and resume**************************/
#define  RK_GICD_BASE (RK_GIC_VIRT)
#define RK_GICC_BASE (RK_GIC_VIRT+RK3288_GIC_DIST_SIZE)

#define PM_IRQN_START 32
#define PM_IRQN_END	107//107
#if 0 //funciton is ok ,but not used
static void pm_gic_enable(u32 irqs)
{

        int irqstart=0;
        u32 bit_off;
        void __iomem *reg_off;
        unsigned int gic_irqs;

        gic_irqs = PM_IRQN_END;
        irqstart=PM_IRQN_START;//PM_IRQN_START;

        reg_off=(irqs/32)*4+GIC_DIST_ENABLE_SET+RK_GICD_BASE;
        bit_off=irqs%32;
        writel_relaxed(readl_relaxed(reg_off)|(1<<bit_off),reg_off);

        dsb();
}
  
static void rkpm_gic_disable(u32 irqs)
{
        int irqstart=0;
        u32 bit_off;    
        void __iomem *reg_off;
        unsigned int gic_irqs;

        gic_irqs = PM_IRQN_END;
        irqstart=PM_IRQN_START;//PM_IRQN_START;

        reg_off=(irqs/32)*4+GIC_DIST_ENABLE_CLEAR+RK_GICD_BASE;
        bit_off=irqs%32;
        writel_relaxed(readl_relaxed(reg_off)&~(1<<bit_off),reg_off);
        dsb();
}
#endif
#define gic_reg_dump(a,b,c)  {}//reg_dump((a),(b),(c))
  
static u32 slp_gic_save[260+50];


static void rkpm_gic_dist_save(u32 *context)
{
     int i = 0,j,irqstart=0;
     unsigned int gic_irqs;
     
     gic_irqs = readl_relaxed(RK_GICD_BASE + GIC_DIST_CTR) & 0x1f;
     gic_irqs = (gic_irqs + 1) * 32;
     if (gic_irqs > 1020)
     gic_irqs = 1020;
     //printk("gic_irqs=%d\n",gic_irqs);
     //gic_irqs = PM_IRQN_END;
     irqstart=PM_IRQN_START;//PM_IRQN_START;
     
     i = 0;
     //level
     for (j = irqstart; j < gic_irqs; j += 16)
      context[i++]=readl_relaxed(RK_GICD_BASE + GIC_DIST_CONFIG + (j * 4) / 16);
     gic_reg_dump("gic level",j,RK_GICD_BASE + GIC_DIST_CONFIG);

     /*
     * Set all global interrupts to this CPU only.
     */
     for(j = 0; j < gic_irqs; j += 4)
    	 context[i++]=readl_relaxed(RK_GICD_BASE + GIC_DIST_TARGET +	(j * 4) / 4);    
     gic_reg_dump("gic trig",j,RK_GICD_BASE + GIC_DIST_TARGET);

     //pri
     for (j = 0; j < gic_irqs; j += 4)
    	 context[i++]=readl_relaxed(RK_GICD_BASE+ GIC_DIST_PRI + (j * 4) / 4);
     gic_reg_dump("gic pri",j,RK_GICD_BASE + GIC_DIST_PRI);	 

     //secure
     for (j = 0; j < gic_irqs; j += 32)
    	 context[i++]=readl_relaxed(RK_GICD_BASE + GIC_DIST_IGROUP + (j * 4) / 32);
     gic_reg_dump("gic secure",j,RK_GICD_BASE + 0x80); 
     	 
     for (j = irqstart; j < gic_irqs; j += 32)
    	 context[i++]=readl_relaxed(RK_GICD_BASE + GIC_DIST_PENDING_SET + (j * 4) / 32);    
     gic_reg_dump("gic PENDING",j,RK_GICD_BASE + GIC_DIST_PENDING_SET);	 
   
    #if 0
     //disable
     for (j = 0; j < gic_irqs; j += 32)
    	 context[i++]=readl_relaxed(RK_GICD_BASE + GIC_DIST_ENABLE_CLEAR + (j * 4) / 32);
     gic_reg_dump("gic dis",j,RK_GICD_BASE + GIC_DIST_ENABLE_CLEAR);
    #endif
    
     //enable
     for (j = 0; j < gic_irqs; j += 32)
    	 context[i++]=readl_relaxed(RK_GICD_BASE + GIC_DIST_ENABLE_SET + (j * 4) / 32);
     gic_reg_dump("gic en",j,RK_GICD_BASE + GIC_DIST_ENABLE_SET);  

     
     
     gic_reg_dump("gicc",0x1c,RK_GICC_BASE);	 
     gic_reg_dump("giccfc",0,RK_GICC_BASE+0xfc);

     context[i++]=readl_relaxed(RK_GICC_BASE + GIC_CPU_PRIMASK);  
     context[i++]=readl_relaxed(RK_GICD_BASE + GIC_DIST_CTRL);
     context[i++]=readl_relaxed(RK_GICC_BASE + GIC_CPU_CTRL);
   
    #if 0
     context[i++]=readl_relaxed(RK_GICC_BASE + GIC_CPU_BINPOINT);
     context[i++]=readl_relaxed(RK_GICC_BASE + GIC_CPU_PRIMASK);
     context[i++]=readl_relaxed(RK_GICC_BASE + GIC_DIST_SOFTINT);
     context[i++]=readl_relaxed(RK_GICC_BASE + GIC_CPU_CTRL);
     context[i++]=readl_relaxed(RK_GICD_BASE + GIC_DIST_CTRL);
    #endif	
    
    #if 1
    for (j = irqstart; j < gic_irqs; j += 32)
    {
        writel_relaxed(0xffffffff, RK_GICD_BASE + GIC_DIST_ENABLE_CLEAR + j * 4 / 32);
        dsb();
    }     
    writel_relaxed(0xffff0000, RK_GICD_BASE + GIC_DIST_ENABLE_CLEAR);
    writel_relaxed(0x0000ffff, RK_GICD_BASE + GIC_DIST_ENABLE_SET);

    writel_relaxed(0, RK_GICC_BASE + GIC_CPU_CTRL);
    writel_relaxed(0, RK_GICD_BASE + GIC_DIST_CTRL);  
    #endif 

}

static void rkpm_gic_dist_resume(u32 *context)
{

         int i = 0,j,irqstart=0;
         unsigned int gic_irqs;

         
         gic_irqs = readl_relaxed(RK_GICD_BASE + GIC_DIST_CTR) & 0x1f;
         gic_irqs = (gic_irqs + 1) * 32;
         if (gic_irqs > 1020)
        	 gic_irqs = 1020;
                 
         //gic_irqs = PM_IRQN_END;
         irqstart=PM_IRQN_START;//PM_IRQN_START;

         writel_relaxed(0,RK_GICC_BASE + GIC_CPU_CTRL);
         dsb();
         writel_relaxed(0,RK_GICD_BASE + GIC_DIST_CTRL);
         dsb();
         for (j = irqstart; j < gic_irqs; j += 32)
         {
        	 writel_relaxed(0xffffffff, RK_GICD_BASE + GIC_DIST_ENABLE_CLEAR + j * 4 / 32);
        	 dsb();
         }

         i = 0;

         //trig
         for (j = irqstart; j < gic_irqs; j += 16)
         {
        	 writel_relaxed(context[i++],RK_GICD_BASE + GIC_DIST_CONFIG + j * 4 / 16);
        	 dsb();
         }
         gic_reg_dump("gic level",j,RK_GICD_BASE + GIC_DIST_CONFIG);	 

         /*
         * Set all global interrupts to this CPU only.
         */
         for (j = 0; j < gic_irqs; j += 4)
         {
        	 writel_relaxed(context[i++],RK_GICD_BASE + GIC_DIST_TARGET +  (j * 4) / 4);
        	 dsb();
         }
         gic_reg_dump("gic target",j,RK_GICD_BASE + GIC_DIST_TARGET);  

         //pri
         for (j = 0; j < gic_irqs; j += 4)
         {
        	 writel_relaxed(context[i++],RK_GICD_BASE+ GIC_DIST_PRI + (j * 4) / 4);
        	 
        	 dsb();
         }
         gic_reg_dump("gic pri",j,RK_GICD_BASE + GIC_DIST_PRI);	 

         
         //secu
         for (j = 0; j < gic_irqs; j += 32)
         {
        	 writel_relaxed(context[i++],RK_GICD_BASE + GIC_DIST_IGROUP + (j * 4 )/ 32);        	 
        	 dsb();
         }
         gic_reg_dump("gic secu",j,RK_GICD_BASE + 0x80);	 

         //pending
         for (j = irqstart; j < gic_irqs; j += 32)
         {
        	 //writel_relaxed(context[i++],RK_GICD_BASE + GIC_DIST_PENDING_SET + j * 4 / 32);
        	 i++;
        	 dsb();
         }
         gic_reg_dump("gic pending",j,RK_GICD_BASE + GIC_DIST_PENDING_SET);	 

         //disable
#if 0
         for (j = 0; j < gic_irqs; j += 32)
         {
        	 writel_relaxed(context[i++],RK_GICD_BASE + GIC_DIST_ENABLE_CLEAR + j * 4 / 32);
        	 dsb();
         }
         gic_reg_dump("gic disable",j,RK_GICD_BASE + GIC_DIST_ENABLE_CLEAR);	 
         
#else
        for (j = irqstart; j < gic_irqs; j += 32)
            writel_relaxed(0xffffffff,RK_GICD_BASE + GIC_DIST_ENABLE_CLEAR + j * 4 / 32);        
        writel_relaxed(0xffff0000, RK_GICD_BASE + GIC_DIST_ENABLE_CLEAR);
        writel_relaxed(0x0000ffff, RK_GICD_BASE + GIC_DIST_ENABLE_SET);
#endif
             	 
         //enable
         for (j = 0; j < gic_irqs; j += 32)
         {
        	 writel_relaxed(context[i++],RK_GICD_BASE + GIC_DIST_ENABLE_SET + (j * 4) / 32);
        	 
        	 dsb();
         }
     
         gic_reg_dump("gic enable",j,RK_GICD_BASE + GIC_DIST_ENABLE_SET);  
      
         writel_relaxed(context[i++],RK_GICC_BASE + GIC_CPU_PRIMASK);
         writel_relaxed(context[i++],RK_GICD_BASE + GIC_DIST_CTRL);
         writel_relaxed(context[i++],RK_GICC_BASE + GIC_CPU_CTRL);

         gic_reg_dump("gicc",0x1c,RK_GICC_BASE);	 
         gic_reg_dump("giccfc",0,RK_GICC_BASE+0xfc);	 
 
}

/**************************************regs save and resume**************************/

void slp_regs_save(u32 *data,void __iomem * base,u32 st_offset,u32 end_offset)
{
     u32 i;
         u32 cnt=(end_offset-st_offset)/4+1;
     for(i=0;i<cnt;i++)
     {
    	 data[i]=readl_relaxed(base+st_offset+i*4);
     }	 
}

void slp_regs_resume(u32 *data,void __iomem * base,u32 st_offset,u32 end_offset,u32 w_msk)
{
     u32 i;
     u32 cnt=(end_offset-st_offset)/4+1;
     for(i=0;i<cnt;i++)
     {		 
    	 reg_writel(data[i]|w_msk,(base+st_offset+i*4));
     }	 
}

void slp_regs_w_msk_resume(u32 *data,void __iomem * base,u32 st_offset,u32 end_offset,u32 *w_msk)
{
        u32 i;
        u32 cnt=(end_offset-st_offset)/4+1;
         for(i=0;i<cnt;i++)
	 {		 
		 reg_writel(data[i]|w_msk[i],(base+st_offset+i*4));
	 }	 
}

/**************************************uarts save and resume**************************/

#define RK3288_UART_NUM (4)

static void __iomem *slp_uart_base[RK3288_UART_NUM]={NULL};
static u32 slp_uart_phy[RK3288_UART_NUM]={(0xff180000),(0xff190000),(0xff690000),(0xff1b0000)};
 
#define UART_DLL	0	/* Out: Divisor Latch Low */
#define UART_DLM	1	/* Out: Divisor Latch High */

#define UART_IER	1
#define UART_FCR	2
 
#define UART_LCR	3	/* Out: Line Control Register */
#define UART_MCR	4

#if 0 //
static u32 slp_uart_data[RK3288_UART_NUM][10];
static u32 slp_uart_data_flag[RK3288_UART_NUM];

 void slp_uart_save(int ch)
 {
	 int i=0;
	void __iomem *b_addr=slp_uart_base[ch];
	 int idx=RK3288_CLKGATE_PCLK_UART0+ch;
	 u32 gate_reg;
	 if(b_addr==NULL || ch>=RK3288_UART_NUM)
	 	return;	
     
        if(ch==2)
        {
            idx=RK3288_CLKGATE_PCLK_UART2;
            b_addr=RK_DEBUG_UART_VIRT;
        }

        
	gate_reg=cru_readl(RK3288_CRU_GATEID_CONS(idx));     
        RK3288_CRU_UNGATING_OPS(idx); 
         i=0;
	 slp_uart_data[ch][i++]=readl_relaxed(b_addr+UART_LCR*4); 
	 writel_relaxed(readl_relaxed(b_addr+UART_LCR*4)|0x80,b_addr+UART_LCR*4);
	 
	 slp_uart_data[ch][i++]=readl_relaxed(b_addr+UART_DLL*4);
	 slp_uart_data[ch][i++]=readl_relaxed(b_addr+UART_DLM*4);
	 
	 writel_relaxed(readl_relaxed(b_addr+UART_LCR*4)&(~0x80),b_addr+UART_LCR*4);
	 slp_uart_data[ch][i++]=readl_relaxed(b_addr+UART_IER*4);
	 slp_uart_data[ch][i++]=readl_relaxed(b_addr+UART_FCR*4);
	 slp_uart_data[ch][i++]=readl_relaxed(b_addr+UART_MCR*4);
	 
        cru_writel(gate_reg|CRU_W_MSK(idx%16,0x1),RK3288_CRU_GATEID_CONS(idx));         
 
 }
 
 void slp_uart_resume(int ch)
 {	 
        int i=0;

        u32 temp;
        void __iomem *b_addr=slp_uart_base[ch];
        int idx=RK3288_CLKGATE_PCLK_UART0+ch;
        u32 gate_reg;
        
        //rkpm_ddr_printascii("\nch");
     //   rkpm_ddr_printhex(b_addr);
        
        if(b_addr==NULL || ch>=RK3288_UART_NUM)
            return;	
        
        if(ch==2)
            idx=RK3288_CLKGATE_PCLK_UART2;

        //rkpm_ddr_printhex(ch);

        gate_reg=cru_readl(RK3288_CRU_GATEID_CONS(idx));     
        RK3288_CRU_UNGATING_OPS(idx); 
 
         i=0;
	 temp=slp_uart_data[ch][i++];
	 writel_relaxed(readl_relaxed(b_addr+UART_LCR*4)|0x80,b_addr+UART_LCR*4);
	 
	 writel_relaxed(slp_uart_data[ch][i++],b_addr+UART_DLL*4);
	 writel_relaxed(slp_uart_data[ch][i++],b_addr+UART_DLM*4);
	 
	 writel_relaxed(readl_relaxed(b_addr+UART_LCR*4)&(~0x80),b_addr+UART_LCR*4);
 
	 writel_relaxed(slp_uart_data[ch][i++],b_addr+UART_IER*4);
	 writel_relaxed(slp_uart_data[ch][i++],b_addr+UART_FCR*4);
	 writel_relaxed(slp_uart_data[ch][i++],b_addr+UART_MCR*4);
	 
	 writel_relaxed(temp,b_addr+UART_LCR*4);
	 
         cru_writel(gate_reg|CRU_W_MSK(idx%16,0x1),RK3288_CRU_GATEID_CONS(idx));         
 }
#endif
 void slp_uartdbg_resume(void)
{   
    void __iomem *b_addr=RK_DEBUG_UART_VIRT;
    u32 pclk_id=RK3288_CLKGATE_PCLK_UART2,clk_id=(RK3288_CLKGATE_UART0_SRC+2*2);
    u32 gate_reg[2];
    u32 rfl_reg,lsr_reg;

    gate_reg[0]=cru_readl(RK3288_CRU_GATEID_CONS(pclk_id));        
    gate_reg[1]=cru_readl(RK3288_CRU_GATEID_CONS(clk_id));     

    RK3288_CRU_UNGATING_OPS(pclk_id); 
    // 24M is no gating setting
    ddr_pin_set_fun(0x7,0xc,0x6,0x0);
    ddr_pin_set_fun(0x7,0xc,0x7,0x0);             

    do{
            // out clk sel 24M
            cru_writel(CRU_W_MSK_SETBITS(0x2,8,0x3), RK3288_CRU_CLKSELS_CON(15));
            
            //uart2 dbg reset
            cru_writel(0|CRU_W_MSK_SETBITS(1,5,0x1), RK3288_CRU_SOFTRSTS_CON(11));
            dsb();
            dsb();
            rkpm_udelay(10);
            cru_writel(0|CRU_W_MSK_SETBITS(0,5,0x1), RK3288_CRU_SOFTRSTS_CON(11));

        #if 0
            //out clk (form pll)  is gating 
            RK3288_CRU_GATING_OPS(clk_id);
            //out clk form pll gating to disable uart clk out
            // div 12
            cru_writel(CRU_W_MSK_SETBITS(11,0,0x7f), RK3288_CRU_CLKSELS_CON(15));
            dsb();
            dsb();   
            dsb();
            dsb();
            cru_writel(CRU_W_MSK_SETBITS(0,8,0x3) , RK3288_CRU_CLKSELS_CON(15));
         #endif


            reg_writel(0x83,b_addr+UART_LCR*4);  

            reg_writel(0xd,b_addr+UART_DLL*4);
            reg_writel(0x0,b_addr+UART_DLM*4);

            reg_writel(0x3,b_addr+UART_LCR*4);    

            reg_writel(0x5,b_addr+UART_IER*4);
            reg_writel(0xc1,b_addr+UART_FCR*4);

            rfl_reg=readl_relaxed(b_addr+0x84);
            lsr_reg=readl_relaxed(b_addr+0x14);
       
        }while((rfl_reg&0x1f)||(lsr_reg&0xf));
                 
        // out clk sel 24M
        cru_writel(CRU_W_MSK_SETBITS(0x2,8,0x3), RK3288_CRU_CLKSELS_CON(15));

        ddr_pin_set_fun(0x7,0xc,0x6,0x1);
        ddr_pin_set_fun(0x7,0xc,0x7,0x1);
        cru_writel(gate_reg[0]|CRU_W_MSK(pclk_id%16,0x1),RK3288_CRU_GATEID_CONS(pclk_id)); 
        cru_writel(gate_reg[1]|CRU_W_MSK(clk_id%16,0x1),RK3288_CRU_GATEID_CONS(clk_id)); 
}
 
/**************************************i2c save and resume**************************/

//#define RK3288_I2C_REG_DUMP
#define RK3288_I2C_NUM (6)
static u32 slp_i2c_phy[RK3288_I2C_NUM]={(0xff650000),(0xff140000),(0xff660000),(0xff150000),(0xff160000),(0xff170000)};
static void __iomem *slp_i2c_base[RK3288_I2C_NUM]={NULL};

static u32 slp_i2c_data[RK3288_I2C_NUM][10];

void slp_i2c_save(int ch)
{

	void __iomem *b_addr=slp_i2c_base[ch];
	int idx= (ch>1) ? (RK3288_CLKGATE_PCLK_I2C2+ch-2):(RK3288_CLKGATE_PCLK_I2C0+ch);
	u32 gate_reg;

	if(!b_addr)
		return;
    
        gate_reg=cru_readl(RK3288_CRU_GATEID_CONS(idx));     
        RK3288_CRU_UNGATING_OPS(idx); 
        
        #ifdef RK3288_I2C_REG_DUMP
        rkpm_ddr_printascii("i2c save");
        rkpm_ddr_printhex(ch);
        rkpm_ddr_printch('\n');        
        rkpm_ddr_regs_dump(b_addr,0x0,0xc);
        #endif
        
        slp_regs_save(&slp_i2c_data[ch][0],b_addr,0x0,0xc);  
        

        cru_writel(gate_reg|CRU_W_MSK(idx%16,0x1),RK3288_CRU_GATEID_CONS(idx));         

}
void slp_i2c_resume(int ch)
{
        void __iomem *b_addr=slp_i2c_base[ch];
        int idx= (ch>1) ? (RK3288_CLKGATE_PCLK_I2C2+ch-2):(RK3288_CLKGATE_PCLK_I2C0+ch);
	u32 gate_reg;
	
	if(!b_addr)
		return;
        gate_reg=cru_readl(RK3288_CRU_GATEID_CONS(idx));     
        RK3288_CRU_UNGATING_OPS(idx); 

        slp_regs_resume(&slp_i2c_data[ch][0],b_addr,0x0,0xc,0x0);  

        #ifdef RK3288_I2C_REG_DUMP
        rkpm_ddr_printascii("i2c resume");
        rkpm_ddr_printhex(ch);
        rkpm_ddr_printch('\n');        
        rkpm_ddr_regs_dump(b_addr,0x0,0xc);
        #endif
  
        cru_writel(gate_reg|CRU_W_MSK(idx%16,0x1),RK3288_CRU_GATEID_CONS(idx));         
}

/**************************************gpios save and resume**************************/
#define RK3288_GPIO_CH (9)
#if 0 //fun is ok ,not used

static u32 slp_gpio_data[RK3288_GPIO_CH][10]; 
static u32 slp_grf_iomux_data[RK3288_GPIO_CH*4];
static u32 slp_grf_io_pull_data[RK3288_GPIO_CH*4];
static void gpio_ddr_dump_reg(int ports)
{
    void __iomem *b_addr=RK_GPIO_VIRT(ports);
    
    rkpm_ddr_printascii("gpio-");
    rkpm_ddr_printhex(ports);
    rkpm_ddr_printhex('\n');      
    
    rkpm_ddr_reg_offset_dump(b_addr,GPIO_SWPORT_DR);
    rkpm_ddr_reg_offset_dump(b_addr,GPIO_SWPORT_DDR);      
    rkpm_ddr_reg_offset_dump(b_addr,GPIO_INTEN);  
    rkpm_ddr_reg_offset_dump(b_addr,GPIO_INTMASK);     
    rkpm_ddr_reg_offset_dump(b_addr,GPIO_INTTYPE_LEVEL);  
    rkpm_ddr_reg_offset_dump(b_addr,GPIO_INT_POLARITY);   
    rkpm_ddr_reg_offset_dump(b_addr,GPIO_DEBOUNCE);   
    rkpm_ddr_reg_offset_dump(b_addr,GPIO_LS_SYNC);    
    rkpm_ddr_printhex('\n');      

    rkpm_ddr_printascii("iomux\n");
    rkpm_ddr_regs_dump(RK_GRF_VIRT,0x0+ports*4*4,0x0+ports*4*4+3*4);

    rkpm_ddr_printascii("iomux\n");
    rkpm_ddr_regs_dump(RK_GRF_VIRT,0x130+ports*4*4,ports*4*4+3*4);

}
 static void slp_pin_gpio_save(int ports)
 {
        int i;
        void __iomem *b_addr=RK_GPIO_VIRT(ports);
        int idx=RK3288_CLKGATE_PCLK_GPIO1+ports-1;
        u32 gate_reg;

	if(ports==0||ports>=RK3288_GPIO_CH)
		return;
	
         gate_reg=cru_readl(RK3288_CRU_GATEID_CONS(idx));     
         RK3288_CRU_UNGATING_OPS(idx); 
         
         //gpio_ddr_dump_reg(ports);          
	 i=0;
	 slp_gpio_data[ports][i++]=readl_relaxed(b_addr+GPIO_SWPORT_DR);
	 slp_gpio_data[ports][i++]=readl_relaxed(b_addr+GPIO_SWPORT_DDR);
	 slp_gpio_data[ports][i++]=readl_relaxed(b_addr+GPIO_INTEN);	 
	 slp_gpio_data[ports][i++]=readl_relaxed(b_addr+GPIO_INTMASK);  
	 slp_gpio_data[ports][i++]=readl_relaxed(b_addr+GPIO_INTTYPE_LEVEL);	 
	 slp_gpio_data[ports][i++]=readl_relaxed(b_addr+GPIO_INT_POLARITY);
	 slp_gpio_data[ports][i++]=readl_relaxed(b_addr+GPIO_DEBOUNCE);
	 slp_gpio_data[ports][i++]=readl_relaxed(b_addr+GPIO_LS_SYNC); 

        if(ports>0)
        {
            slp_regs_save(&slp_grf_iomux_data[ports*4],RK_GRF_VIRT,0x0+ports*4*4,0x0+ports*4*4+3*4);  
            slp_regs_save(&slp_grf_io_pull_data[ports*4],RK_GRF_VIRT,0x130+ports*4*4,ports*4*4+3*4);
         }

     
        cru_writel(gate_reg|CRU_W_MSK(idx%16,0x1),RK3288_CRU_GATEID_CONS(idx));         
 
 }

 static void slp_pin_gpio_resume (int ports)
 {
	 int i;
        void __iomem *b_addr=RK_GPIO_VIRT(ports);
        int idx=RK3288_CLKGATE_PCLK_GPIO1+ports-1;
	 u32 gate_reg;
	 
	 if(ports==0||ports>=RK3288_GPIO_CH)
		return;
	  gate_reg=cru_readl(RK3288_CRU_GATEID_CONS(idx));     
         RK3288_CRU_UNGATING_OPS(idx); 


        if(ports>0)
        {
            slp_regs_resume(&slp_grf_iomux_data[ports*4],RK_GRF_VIRT,0x0+ports*4*4,0x0+ports*4*4+3*4,0xffff0000);  
            slp_regs_resume(&slp_grf_io_pull_data[ports*4],RK_GRF_VIRT,0x130+ports*4*4,ports*4*4+3*4,0xffff0000);
        }
 
        i=0;
        writel_relaxed(slp_gpio_data[ports][i++],b_addr+GPIO_SWPORT_DR);
        writel_relaxed(slp_gpio_data[ports][i++],b_addr+GPIO_SWPORT_DDR);
        writel_relaxed(slp_gpio_data[ports][i++],b_addr+GPIO_INTEN);	 
        writel_relaxed(slp_gpio_data[ports][i++],b_addr+GPIO_INTMASK); 
        writel_relaxed(slp_gpio_data[ports][i++],b_addr+GPIO_INTTYPE_LEVEL);	 
        writel_relaxed(slp_gpio_data[ports][i++],b_addr+GPIO_INT_POLARITY);
        writel_relaxed(slp_gpio_data[ports][i++],b_addr+GPIO_DEBOUNCE);
        writel_relaxed(slp_gpio_data[ports][i++],b_addr+GPIO_LS_SYNC);	    
        
        //gpio_ddr_dump_reg(ports);	
        cru_writel(gate_reg|CRU_W_MSK(idx%16,0x1),RK3288_CRU_GATEID_CONS(idx));         
 
 }
 
#endif
 static inline u32 rkpm_l2_config(void)
 {
     u32 l2ctlr;
     asm("mrc p15, 1, %0, c9, c0, 2" : "=r" (l2ctlr));
      return l2ctlr;
 }

static inline u32 rkpm_armerrata_818325(void)
{
    u32 armerrata;
    asm("mrc p15, 0, %0, c15, c0, 1" : "=r" (armerrata));
    return armerrata;
}



/**************************************sleep func**************************/

void ddr_reg_save(uint32_t *pArg);
void fiq_glue_resume(void);
void rk30_cpu_resume(void);
void rk30_l2_cache_init_pm(void);
//static void rk319x_pm_set_power_domain(enum pmu_power_domain pd, bool state);
void ddr_cfg_to_lp_mode(void);
void l2x0_inv_all_pm(void);
void rk30_cpu_while_tst(void);

#if 0
static u32 slp_grf_soc_con_data[5];
static u32 slp_grf_soc_con_w_msk[5]={0x70000,0x40ff0000,0xffff0000,0xffff0000,0xffff0000};

static u32 slp_grf_cpu_con_data[5];
static u32 slp_grf_cpu_con_w_msk[5]={0xefff0000,0xffff0000,0xcfff0000,0xffff0000,0x7fff0000};

static u32 slp_grf_uoc0_con_data[4];
static u32 slp_grf_uoc0_con_w_msk[4]={0xffff0000,0xffff0000,0x7dff0000,0x7fff0000};// uoc0_con4 bit 15?? 

static u32 slp_grf_uoc1_con_data[2];
static u32 slp_grf_uoc1_con_w_msk[2]={0x1fdc0000,0x047f0000};

static u32 slp_grf_uoc2_con_data[2];
static u32 slp_grf_uoc2_con_w_msk[2]={0x7fff0000,0x1f0000};

static u32 slp_grf_uoc3_con_data[2];
static u32 slp_grf_uoc3_con_w_msk[2]={0x3ff0000,0x0fff0000};

#endif
//static u32 slp_pmu_pwrmode_con_data[1];


//static u32 slp_nandc_data[8];
//static void __iomem *rk30_nandc_base=NULL;

#define MS_37K (37)
#define US_24M (24)

void inline pm_io_base_map(void)
{
        int i;
        for(i=0;i<RK3288_I2C_NUM;i++)
            slp_i2c_base[i]  = ioremap(slp_i2c_phy[i], 0x1000);

        for(i=0;i<RK3288_UART_NUM;i++)
            {
                if(i!=CONFIG_RK_DEBUG_UART)
                    slp_uart_base[i]  = ioremap(slp_uart_phy[i], 0x1000);
                else
                    slp_uart_base[i] = RK_DEBUG_UART_VIRT;
            }
	
}	
enum rk3288_pwr_mode_con {

        pmu_pwr_mode_en=0,
        pmu_clk_core_src_gate_en,
        pmu_global_int_disable,
        pmu_l2flush_en,
        
        pmu_bus_pd_en,
        pmu_a12_0_pd_en,
        pmu_scu_en,
        pmu_pll_pd_en,
        
        pmu_chip_pd_en, // power off pin enable
        pmu_pwroff_comb,
        pmu_alive_use_lf,
        pmu_pmu_use_lf,
        
        pmu_osc_24m_dis,
        pmu_input_clamp_en,
        pmu_wakeup_reset_en,
        pmu_sref0_enter_en,
        
        pmu_sref1_enter_en,       
        pmu_ddr0io_ret_en,
        pmu_ddr1io_ret_en,
        pmu_ddr0_gating_en,
        
        pmu_ddr1_gating_en,
        pmu_ddr0io_ret_de_req,
        pmu_ddr1io_ret_de_req

};
 enum rk3288_pwr_mode_con1 {

        pmu_clr_bus=0,
        pmu_clr_core,
        pmu_clr_cpup,
        pmu_clr_alive,
        
        pmu_clr_dma,
        pmu_clr_peri,
        pmu_clr_gpu,
        pmu_clr_video,
        pmu_clr_hevc,
        pmu_clr_vio
  
};
 static u32 rk3288_powermode=0;
static void ddr_pin_set_fun(u8 port,u8 bank,u8 b_gpio,u8 fun);

static u32 sgrf_soc_con0,pmu_wakeup_cfg0,pmu_wakeup_cfg1,pmu_pwr_mode_con0,pmu_pwr_mode_con1;

static u32  rkpm_slp_mode_set(u32 ctrbits)
{
    u32 mode_set,mode_set1;
    
    // setting gpio0_a0 arm off pin

    sgrf_soc_con0=reg_readl(RK_SGRF_VIRT+RK3288_SGRF_SOC_CON0);
    
    pmu_wakeup_cfg0=pmu_readl(RK3288_PMU_WAKEUP_CFG0);  
    pmu_wakeup_cfg1=pmu_readl(RK3288_PMU_WAKEUP_CFG1);
    
    pmu_pwr_mode_con0=pmu_readl(RK3288_PMU_PWRMODE_CON);  
    pmu_pwr_mode_con1=pmu_readl(RK3288_PMU_PWRMODE_CON1);
    
    ddr_pin_set_fun(0x0,0xa,0x0,0x1);


    
    //mode_set1=pmu_pwr_mode_con1;
    //mode_set=pmu_pwr_mode_con0;
  
   //pmu_writel(0x1<<3,RK3188_PMU_WAKEUP_CFG1);  
   pmu_writel(0x1<<0,RK3188_PMU_WAKEUP_CFG1);  

    // enable boot ram    
    reg_writel((0x1<<8)|(0x1<<(8+16)),RK_SGRF_VIRT+RK3288_SGRF_SOC_CON0);
    dsb();
    
    reg_writel(RKPM_BOOTRAM_PHYS,RK_SGRF_VIRT+RK3288_SGRF_FAST_BOOT_ADDR);
    dsb();

    mode_set=  BIT(pmu_pwr_mode_en) |BIT(pmu_global_int_disable) | BIT(pmu_l2flush_en);
     mode_set1=0;

    if(rkpm_chk_val_ctrbits(ctrbits,RKPM_CTR_IDLEAUTO_MD))
    {
        rkpm_ddr_printascii("-autoidle-");
        mode_set|=BIT(pmu_clk_core_src_gate_en);        
    }
    else if(rkpm_chk_val_ctrbits(ctrbits,RKPM_CTR_ARMDP_LPMD))
    {
        rkpm_ddr_printascii("-armdp-");            
        mode_set|=BIT(pmu_a12_0_pd_en);
    }
    else if(rkpm_chk_val_ctrbits(ctrbits,RKPM_CTR_ARMOFF_LPMD))
    {   
        rkpm_ddr_printascii("-armoff-");                         
        mode_set|=BIT(pmu_scu_en)
                            //|BIT(pmu_a12_0_pd_en) 
                            |BIT(pmu_clk_core_src_gate_en) // »½ÐÑºóÒì³£
                            |BIT(pmu_sref0_enter_en)|BIT(pmu_sref1_enter_en) 
                            |BIT(pmu_ddr0_gating_en)|BIT(pmu_ddr1_gating_en)              
                            //|BIT(pmu_ddr1io_ret_en)|BIT(pmu_ddr0io_ret_en)   
                            |BIT(pmu_chip_pd_en);
        mode_set1=BIT(pmu_clr_core)|BIT(pmu_clr_cpup)
                                |BIT(pmu_clr_alive)
                                |BIT(pmu_clr_peri)
                                |BIT(pmu_clr_bus)
                                |BIT(pmu_clr_dma)
                                ;
    } 
    else if(rkpm_chk_val_ctrbits(ctrbits,RKPM_CTR_ARMOFF_LOGDP_LPMD))
    {
    
        rkpm_ddr_printascii("-armoff-logdp-");        
        
        mode_set|=BIT(pmu_scu_en)|BIT(pmu_bus_pd_en)
                            |BIT(pmu_chip_pd_en)
                            |BIT(pmu_sref0_enter_en)|BIT(pmu_sref1_enter_en) 
                            |BIT(pmu_ddr0_gating_en)|BIT(pmu_ddr1_gating_en)              
                            |BIT(pmu_ddr1io_ret_en)|BIT(pmu_ddr0io_ret_en)   
                            |BIT(pmu_osc_24m_dis)|BIT(pmu_pmu_use_lf)|BIT(pmu_alive_use_lf)|BIT(pmu_pll_pd_en)
                            ;
        mode_set1=BIT(pmu_clr_core)|BIT(pmu_clr_cpup)
                           |BIT(pmu_clr_alive)
                           |BIT(pmu_clr_peri)
                           |BIT(pmu_clr_bus) 
                           |BIT(pmu_clr_dma)                                       
                          ;
     
    } 
    else
    {
        mode_set=0;
        mode_set1=0;
    }

    
    if(mode_set&BIT(pmu_osc_24m_dis))
    {
        rkpm_ddr_printascii("osc_off");        
        pmu_writel(32*30,RK3288_PMU_OSC_CNT);  
        pmu_writel(32*30,RK3288_PMU_STABL_CNT);  
    }
    else
    {
        pmu_writel(24*1000*10,RK3288_PMU_STABL_CNT);  
        
       // pmu_writel(24*1000*20,RK3288_PMU_CORE_PWRDWN_CNT);  
    }

    if(mode_set&BIT(pmu_ddr0io_ret_en))
    {
        rkpm_ddr_printascii("ddrc_off");  
        ddr_pin_set_fun(0x0,0xa,0x1,0x1);
        ddr_pin_set_fun(0x0,0xa,0x2,0x1);
        ddr_pin_set_fun(0x0,0xa,0x3,0x1);
    }

    pmu_writel(mode_set,RK3288_PMU_PWRMODE_CON);  
    pmu_writel(mode_set1,RK3288_PMU_PWRMODE_CON1);  
    
  //  rkpm_ddr_printhex(mode_set);
  //  rkpm_ddr_printhex(pmu_readl(RK3288_PMU_PWRMODE_CON));
  
    return (pmu_readl(RK3288_PMU_PWRMODE_CON));  
}

static inline void  rkpm_slp_mode_set_resume(void)
{

    pmu_writel(pmu_wakeup_cfg0,RK3288_PMU_WAKEUP_CFG0);  
    pmu_writel(pmu_wakeup_cfg1,RK3288_PMU_WAKEUP_CFG1);  
    
    pmu_writel(pmu_pwr_mode_con0,RK3288_PMU_PWRMODE_CON);  
    pmu_writel(pmu_pwr_mode_con1,RK3288_PMU_PWRMODE_CON1);  
    reg_writel(sgrf_soc_con0|(0x1<<(8+16)),RK_SGRF_VIRT+RK3288_SGRF_SOC_CON0);
    
}

static void sram_code_data_save(u32 pwrmode)
{
	char *code_src,*data_src;
	u32 code_size,data_size;
      
     
	//u32 *p;
        if(pwrmode&(BIT(pmu_scu_en)|BIT(pmu_a12_0_pd_en)))
        {   
            sleep_resume_data[RKPM_BOOTDATA_L2LTY_F]=1;
            sleep_resume_data[RKPM_BOOTDATA_L2LTY]=rkpm_l2_config();// in sys resume ,ddr is need resume	
            
            sleep_resume_data[RKPM_BOOTDATA_ARM_ERRATA_818325_F]=1;
            sleep_resume_data[RKPM_BOOTDATA_ARM_ERRATA_818325]=rkpm_armerrata_818325();//
        
            sleep_resume_data[RKPM_BOOTDATA_CPUSP]=RKPM_BOOT_CPUSP_PHY;// in sys resume ,ddr is need resume	            
            sleep_resume_data[RKPM_BOOTDATA_CPUCODE]=virt_to_phys(cpu_resume);// in sys resume ,ddr is need resume  
            #if 0
            rkpm_ddr_printascii("l2&arm_errata--");   
            rkpm_ddr_printhex(rkpm_l2_config());             
            rkpm_ddr_printhex(rkpm_armerrata_818325());
            rkpm_ddr_printascii("\n");  
            #endif
        }
        else
        {
            sleep_resume_data[RKPM_BOOTDATA_L2LTY_F]=0;
            sleep_resume_data[RKPM_BOOTDATA_ARM_ERRATA_818325_F]=0;       
            sleep_resume_data[RKPM_BOOTDATA_CPUCODE]=0;
            return ;
        }
	
        if(pwrmode&BIT(pmu_bus_pd_en))                
        {   
        	sleep_resume_data[RKPM_BOOTDATA_DDR_F]=1;// in sys resume ,ddr is need resume
        	sleep_resume_data[RKPM_BOOTDATA_DPLL_F]=1;// in ddr resume ,dpll is need resume
                code_src=(char *)ddr_get_resume_code_info(&code_size);
                sleep_resume_data[RKPM_BOOTDATA_DDRCODE]=RKPM_BOOT_DDRCODE_PHY;
                sleep_resume_data[RKPM_BOOTDATA_DDRDATA]=RKPM_BOOT_DDRCODE_PHY+RKPM_ALIGN(code_size,4);
                data_src=(char *)ddr_get_resume_data_info(&data_size);
                ddr_reg_save((u32 *)(resume_data_phy+RKPM_BOOTDATA_DPLL_F*4));
       }
        else
        {
            sleep_resume_data[RKPM_BOOTDATA_DDR_F]=0;
        }
        
	sram_data_for_sleep(boot_ram_data,int_ram_data,sleep_resume_data[RKPM_BOOTDATA_DDR_F]);
    
        flush_cache_all();
        outer_flush_all();
        local_flush_tlb_all();

}

static inline void sram_code_data_resume(u32 pwrmode)
{
         if(pwrmode&(BIT(pmu_scu_en)|BIT(pmu_a12_0_pd_en)))
        {
             sram_data_resume(boot_ram_data,int_ram_data,sleep_resume_data[RKPM_BOOTDATA_DDR_F]);
        }
             
}

static void  rkpm_peri_save(u32 power_mode)
{
//    u32 gpio_gate[2];

    if(power_mode&BIT(pmu_scu_en))
    {
        rkpm_gic_dist_save(&slp_gic_save[0]);   
    }
#if 0
    gpio_gate[0]=cru_readl(RK3288_CRU_GATEID_CONS(RK3288_CLKGATE_PCLK_GPIO0));
    gpio_gate[1]=cru_readl(RK3288_CRU_GATEID_CONS(RK3288_CLKGATE_PCLK_GPIO1));
    RK3288_CRU_UNGATING_OPS(RK3288_CLKGATE_PCLK_GPIO0);
    cru_writel(0xff<<(RK3288_CLKGATE_PCLK_GPIO1%16+16),
                         RK3288_CRU_GATEID_CONS(RK3288_CLKGATE_PCLK_GPIO1));
#endif
    
    if(power_mode&BIT(pmu_bus_pd_en))
   {  
       #if 0
        //gpio7_c6
        //gpio7_c7
        ddr_pin_set_pull(7,0xc,0x6,RKPM_GPIO_PULL_UP);
        ddr_gpio_set_in_output(7,0xc,0x6,RKPM_GPIO_INPUT);
        ddr_pin_set_fun(7,0xc,0x6,0);
        
        ddr_pin_set_pull(7,0xc,0x7,RKPM_GPIO_PULL_UP);
        ddr_gpio_set_in_output(7,0xc,0x7,RKPM_GPIO_INPUT);
        ddr_pin_set_fun(7,0xc,0x7,0);
        #endif
        //slp_uart_save(2);
        #if 0
        ddr_pin_set_pull(0,0xb,0x7,RKPM_GPIO_PULL_UP);
        ddr_gpio_set_in_output(0,0xb,0x7,RKPM_GPIO_INPUT);
        ddr_pin_set_fun(0,0xb,0x7,0);
        
        ddr_pin_set_pull(0,0xc,0x0,RKPM_GPIO_PULL_UP);
        ddr_gpio_set_in_output(0,0xc,0x0,RKPM_GPIO_INPUT);
        ddr_pin_set_fun(0,0xc,0x0,0);
        #endif      
        slp_i2c_save(0);// i2c pmu gpio0b7 gpio0_c0
        slp_i2c_save(1);//i2c audio
    }

#if 0
        cru_writel((0xff<<(RK3288_CLKGATE_PCLK_GPIO1%16+16))|gpio_gate[0],
                                      RK3288_CRU_GATEID_CONS(RK3288_CLKGATE_PCLK_GPIO1));
        cru_writel(gpio_gate[0]|CRU_W_MSK(RK3288_CLKGATE_PCLK_GPIO0%16,0x1),RK3288_CRU_GATEID_CONS(RK3288_CLKGATE_PCLK_GPIO0));         
#endif

}

static inline void  rkpm_peri_resume(u32 power_mode)
{
    if(power_mode&BIT(pmu_scu_en))
    {       
        //fiq_glue_resume();
        rkpm_gic_dist_resume(&slp_gic_save[0]);          
        fiq_glue_resume();
        //rkpm_ddr_printascii("gic res");       
    }  
    if(power_mode&BIT(pmu_bus_pd_en))
   {
        slp_i2c_resume(0);// i2c pmu
        slp_i2c_resume(1);//i2c audio
    }

}

static u32 pdbus_gate_reg[5];
static inline void  rkpm_peri_resume_first(u32 power_mode)
{
    
    if(power_mode&BIT(pmu_bus_pd_en))
    {
        cru_writel(0xffff0000|pdbus_gate_reg[0],RK3288_CRU_CLKGATES_CON(0));      
        cru_writel(0xffff0000|pdbus_gate_reg[1],RK3288_CRU_CLKGATES_CON(4));       
        cru_writel(0xffff0000|pdbus_gate_reg[2],RK3288_CRU_CLKGATES_CON(5));      
        cru_writel(0xffff0000|pdbus_gate_reg[3],RK3288_CRU_CLKGATES_CON(10));     
        cru_writel(0xffff0000|pdbus_gate_reg[4],RK3288_CRU_CLKGATES_CON(11));     
    }


      if(power_mode&BIT(pmu_bus_pd_en))
        slp_uartdbg_resume();
}

static void rkpm_slp_setting(void)
{
    rk_usb_power_down();

    if(rk3288_powermode&BIT(pmu_bus_pd_en))
    {   
        // pd bus will be power down ,but if it reup,ungating clk for its reset
        // ungating pdbus clk
        pdbus_gate_reg[0]=cru_readl(RK3288_CRU_CLKGATES_CON(0));
        pdbus_gate_reg[1]=cru_readl(RK3288_CRU_CLKGATES_CON(4));
        pdbus_gate_reg[2]=cru_readl(RK3288_CRU_CLKGATES_CON(5));
        pdbus_gate_reg[3]=cru_readl(RK3288_CRU_CLKGATES_CON(10));
        pdbus_gate_reg[4]=cru_readl(RK3288_CRU_CLKGATES_CON(11));
        
        cru_writel(0xffff0000,RK3288_CRU_CLKGATES_CON(0));      
        cru_writel(0xffff0000,RK3288_CRU_CLKGATES_CON(4));       
        cru_writel(0xffff0000,RK3288_CRU_CLKGATES_CON(5));      
        cru_writel(0xffff0000,RK3288_CRU_CLKGATES_CON(10));     
        cru_writel(0xffff0000,RK3288_CRU_CLKGATES_CON(11));     

        RK3288_CRU_UNGATING_OPS(RK3288_CLKGATE_PCLK_UART2); 
       // RK3288_CRU_UNGATING_OPS((RK3288_CLKGATE_UART0_SRC+2*2)); 
       //c2c host
       RK3288_CRU_UNGATING_OPS(RK3288_CRU_CONS_GATEID(13)+8); 
           
    }

}


static void rkpm_save_setting_resume_first(void)
{
	rk_usb_power_up();
        rkpm_peri_resume_first(rk3288_powermode);     
        
        // rkpm_ddr_printhex(cru_readl(RK3288_CRU_MODE_CON));
        #if 0
	//rk319x_pm_set_power_domain(PD_PERI,true);
	//slp_regs_resume(slp_grf_io_pull_data,(u32)RK_GRF_VIRT+0x144,16,0xffff0000);
	slp_pin_gpio_resume(1);
	slp_pin_gpio_resume(2);
	slp_pin_gpio_resume(3);
	slp_pin_gpio_resume(4);

	#if 0
	slp_regs_w_msk_resume(slp_grf_soc_con_data,(u32)RK_GRF_VIRT+0x60,5,slp_grf_soc_con_w_msk);
	slp_regs_w_msk_resume(slp_grf_cpu_con_data,(u32)RK_GRF_VIRT+0x9c,5,slp_grf_cpu_con_w_msk);

	slp_regs_w_msk_resume(slp_grf_uoc0_con_data,(u32)RK_GRF_VIRT+0xc4,4,slp_grf_uoc0_con_w_msk);
	slp_regs_w_msk_resume(slp_grf_uoc1_con_data,(u32)RK_GRF_VIRT+0xd4,2,slp_grf_uoc1_con_w_msk);
	slp_regs_w_msk_resume(slp_grf_uoc2_con_data,(u32)RK_GRF_VIRT+0xe4,2,slp_grf_uoc2_con_w_msk);
	slp_regs_w_msk_resume(slp_grf_uoc3_con_data,(u32)RK_GRF_VIRT+0xec,2,slp_grf_uoc3_con_w_msk);
	#endif
	//sram_printch_uart_enable();
	slp_uart_resume(2);
       #endif
}

static void rkpm_save_setting(u32 ctrbits)
{

#if 0
    rkpm_ddr_regs_dump(RK_DDR_VIRT,0,0x3fc);
    rkpm_ddr_regs_dump(RK_DDR_VIRT+RK3288_DDR_PCTL_SIZE,0,0x294);

    rkpm_ddr_regs_dump(RK_DDR_VIRT+RK3288_DDR_PCTL_SIZE+RK3288_DDR_PUBL_SIZE,0,0x3fc);
    rkpm_ddr_regs_dump(RK_DDR_VIRT+RK3288_DDR_PCTL_SIZE*2+RK3288_DDR_PUBL_SIZE,0,0x294);
#endif
    rk3288_powermode=rkpm_slp_mode_set(ctrbits);
    if(rk3288_powermode&BIT(pmu_pwr_mode_en))
    {
            sram_code_data_save(rk3288_powermode);   
            rkpm_peri_save(rk3288_powermode);                
    }
    else
         return ;

}
static void rkpm_save_setting_resume(void)
{

        #if 0
        rkpm_ddr_printascii("l2&arm_errata--");   
        rkpm_ddr_printhex(rkpm_l2_config());             
        rkpm_ddr_printhex(rkpm_armerrata_818325());
        rkpm_ddr_printascii("\n");            
        #endif
                   
         if(rk3288_powermode&BIT(pmu_pwr_mode_en))
        {
            sram_code_data_resume(rk3288_powermode); 
            rkpm_peri_resume(rk3288_powermode);
        }         
         rkpm_slp_mode_set_resume();       

}

/*******************************common code  for rkxxx*********************************/
static void  inline uart_printch(char byte)
{
        u32 reg_save[2];
        u32 u_clk_id=(RK3288_CLKGATE_UART0_SRC+CONFIG_RK_DEBUG_UART*2);
        u32 u_pclk_id=(RK3288_CLKGATE_PCLK_UART0+CONFIG_RK_DEBUG_UART);
        
        if(CONFIG_RK_DEBUG_UART==4)
            u_clk_id=RK3288_CLKGATE_UART4_SRC;
        if(CONFIG_RK_DEBUG_UART==2)
            u_pclk_id=RK3288_CLKGATE_PCLK_UART2;
            
        reg_save[0]=cru_readl(RK3288_CRU_GATEID_CONS(u_clk_id));
        reg_save[1]=cru_readl(RK3288_CRU_GATEID_CONS(u_pclk_id));
        RK3288_CRU_UNGATING_OPS(u_clk_id);
        RK3288_CRU_UNGATING_OPS(u_pclk_id);
        
        rkpm_udelay(1);
        
write_uart:
	writel_relaxed(byte, RK_DEBUG_UART_VIRT);
	dsb();

	/* loop check LSR[6], Transmitter Empty bit */
	while (!(readl_relaxed(RK_DEBUG_UART_VIRT + 0x14) & 0x40))
		barrier();
    
	if (byte == '\n') {
		byte = '\r';
		goto write_uart;
	}

         cru_writel(reg_save[0]|CRU_W_MSK(u_clk_id%16,0x1),RK3288_CRU_GATEID_CONS(u_clk_id));         
         cru_writel(reg_save[1]|CRU_W_MSK(u_pclk_id%16,0x1),RK3288_CRU_GATEID_CONS(u_pclk_id));
}

void PIE_FUNC(sram_printch)(char byte)
{
	uart_printch(byte);
}

static void pll_udelay(u32 udelay);

#ifdef CONFIG_RK_LAST_LOG
extern void rk_last_log_text(char *text, size_t size);
#endif

static void  ddr_printch(char byte)
{
	uart_printch(byte);  
    
#ifdef CONFIG_RK_LAST_LOG
	rk_last_log_text(&byte, 1);

	if (byte == '\n') {
		byte = '\r';
		rk_last_log_text(&byte, 1);
	}
#endif
        pll_udelay(2);
}
/*******************************gpio func*******************************************/
//#define RK3288_PMU_GPIO0_A_IOMUX	0x0084
//#define RK3288_PMU_GPIO0_B_IOMUX	0x0088
//#define RK3288_PMU_GPIO0_C_IOMUX	0x008c
//#define RK3288_PMU_GPIO0_D_IOMUX	0x0090
//pin=0x0a21  gpio0a2,port=0,bank=a,b_gpio=2,fun=1
static inline void pin_set_fun(u8 port,u8 bank,u8 b_gpio,u8 fun)
{ 
        u32 off_set;
        bank-=0xa;
    
        if(port==0)
        { 
            if(bank>2)
                return;
            off_set=RK3288_PMU_GPIO0_A_IOMUX+bank*4;
            pmu_writel(RKPM_VAL_SETBITS(pmu_readl(off_set),fun,b_gpio*2,0x3),off_set);
        }
        else if(port==1||port==2)
        {
            off_set=port*(4*4)+bank*4;
            reg_writel(RKPM_W_MSK_SETBITS(fun,b_gpio*2,0x3),RK_GRF_VIRT+0+off_set);
        }
        else if(port==3)
        {
            if(bank<=2)
            {
                off_set=0x20+bank*4;
                reg_writel(RKPM_W_MSK_SETBITS(fun,b_gpio*2,0x3),RK_GRF_VIRT+0+off_set);

            }
            else
            {
                off_set=0x2c+(b_gpio/4)*4;
                reg_writel(RKPM_W_MSK_SETBITS(fun,(b_gpio%4)*4,0x3),RK_GRF_VIRT+0+off_set);
            }

        }
        else if(port==4)
        {
            if(bank<=1)
            {
                off_set=0x34+bank*8+(b_gpio/4)*4;
                reg_writel(RKPM_W_MSK_SETBITS(fun,(b_gpio%4)*4,0x3),RK_GRF_VIRT+0+off_set);
            }
            else
            {
                off_set=0x44+(bank-2)*4;
                reg_writel(RKPM_W_MSK_SETBITS(fun,b_gpio*2,0x3),RK_GRF_VIRT+0+off_set);
            }

        }
        else if(port==5||port==6)
        {
                off_set=0x4c+(port-5)*4*4+bank*4;
                reg_writel(RKPM_W_MSK_SETBITS(fun,b_gpio*2,0x3),RK_GRF_VIRT+0+off_set);
        }
        else if(port==7)
        {
            if(bank<=1)
            {
                off_set=0x6c+bank*4;
                reg_writel(RKPM_W_MSK_SETBITS(fun,b_gpio*2,0x3),RK_GRF_VIRT+0+off_set);
            }
            else
            {
                off_set=0x74+(bank-2)*8+(b_gpio/4)*4;
                //rkpm_ddr_printascii("gpio");
                //rkpm_ddr_printhex(off_set);                   
                //rkpm_ddr_printascii("-");
                //rkpm_ddr_printhex((b_gpio%4)*4);

                reg_writel(RKPM_W_MSK_SETBITS(fun,(b_gpio%4)*4,0x3),RK_GRF_VIRT+0+off_set);

                //rkpm_ddr_printhex(reg_readl(RK_GRF_VIRT+0+off_set));    
                //rkpm_ddr_printascii("\n");        
            }

        }
        else if(port==8)
        {
            if(bank<=1)
            {
                off_set=0x80+bank*4;
                reg_writel(RKPM_W_MSK_SETBITS(fun,b_gpio*2,0x3),RK_GRF_VIRT+0+off_set);
            }
        }
               
}

#if 0
static inline u8 pin_get_funset(u8 port,u8 bank,u8 b_gpio)
{ 
           
}
#endif
static inline void pin_set_pull(u8 port,u8 bank,u8 b_gpio,u8 pull)
{ 
    u32 off_set;
    
    bank-=0xa;

    if(port > 0)
    {
        //gpio1_d st
        //if(port==1&&bank<3)
       //  return;   
        //gpio1_d==0x14c ,form gpio0_a to gpio1_d offset 1*16+3*4= 0x1c
        off_set=(0x14c-0x1c)+port*(4*4)+bank*4;    

        #if 0
        rkpm_ddr_printascii("gpio pull\n");
        rkpm_ddr_printhex((u32)RK_GPIO_VIRT(port));
        rkpm_ddr_printhex(b_gpio);
        rkpm_ddr_printhex(pull);
        rkpm_ddr_printhex(off_set);
        rkpm_ddr_printhex(RKPM_W_MSK_SETBITS(pull,b_gpio*2,0x3));
        #endif
        
        reg_writel(RKPM_W_MSK_SETBITS(pull,b_gpio*2,0x3),RK_GRF_VIRT+off_set);

    }
    else
    {
        if(bank>2)// gpio0_d is not support
            return; 
        pmu_writel(RKPM_VAL_SETBITS(pmu_readl(0x64+bank*4),pull,b_gpio*2,0x3),0x64+bank*4);
    }
        
}

static inline u8 pin_get_pullset(u8 port,u8 bank,u8 b_gpio)
{ 
    u32 off_set;
    
    bank-=0xa;

    if(port > 0)
    {
        //gpio1_d st
        if(port==1&&bank<3)
            return 0;   
        //gpio1_d==0x14c ,form gpio0_a to gpio1_d offset 1*16+3*4= 0x1c
        off_set=0x14c-0x1c+port*(4*4)+bank*4;    
        return RKPM_GETBITS(reg_readl(RK_GRF_VIRT+off_set),b_gpio*2,0x3);

    }
    else
    {
        if(bank>2)// gpio0_d is not support
            return 0;         
        return RKPM_GETBITS(pmu_readl(0x64+bank*4),b_gpio*2,0x3);
    }
        
}


//RKPM_GPIOS_INPUT
static inline void gpio_set_in_output(u8 port,u8 bank,u8 b_gpio,u8 type)
{
    u32 val;    
    
    bank-=0xa;
    b_gpio=bank*8+b_gpio;//

    val=reg_readl(RK_GPIO_VIRT(port)+GPIO_SWPORT_DDR);

    if(type==RKPM_GPIO_OUTPUT)
        val|=(0x1<<b_gpio);
    else
        val&=~(0x1<<b_gpio);
    #if 0
    rkpm_ddr_printascii("gpio out\n");
    rkpm_ddr_printhex((u32)RK_GPIO_VIRT(port));
    rkpm_ddr_printhex(b_gpio);

    rkpm_ddr_printhex(type);
    rkpm_ddr_printhex(val);
    #endif
    reg_writel(val,RK_GPIO_VIRT(port)+GPIO_SWPORT_DDR);

    //rkpm_ddr_printhex(reg_readl(RK_GPIO_VIRT(port)+GPIO_SWPORT_DDR));

    
}

static inline u8 gpio_get_in_outputset(u8 port,u8 bank,u8 b_gpio)
{
    bank-=0xa;
    b_gpio=bank*8+b_gpio;
    return reg_readl(RK_GPIO_VIRT(port)+GPIO_SWPORT_DDR)&(0x1<<b_gpio);
}

//RKPM_GPIOS_OUT_L   RKPM_GPIOS_OUT_H
static inline void gpio_set_output_level(u8 port,u8 bank,u8 b_gpio,u8 level)
{
    u32 val;    

    bank-=0xa;
    b_gpio=bank*8+b_gpio;
        
    val=reg_readl(RK_GPIO_VIRT(port)+GPIO_SWPORT_DR);

    if(level==RKPM_GPIO_OUT_H)
        val|=(0x1<<b_gpio);
    else //
        val&=~(0x1<<b_gpio);

     reg_writel(val,RK_GPIO_VIRT(port)+GPIO_SWPORT_DR);
}

static inline u8 gpio_get_output_levelset(u8 port,u8 bank,u8 b_gpio)
{     
    bank-=0xa;
    b_gpio=bank*8+b_gpio;
    return reg_readl(RK_GPIO_VIRT(port)+GPIO_SWPORT_DR)&(0x1<<b_gpio);
}

static inline u8 gpio_get_input_level(u8 port,u8 bank,u8 b_gpio)
{

    bank-=0xa;
    b_gpio=bank*8+b_gpio;

    return (reg_readl(RK_GPIO_VIRT(port)+GPIO_EXT_PORT)>>b_gpio)&0x1;
}
static inline void gpio_set_inten(u8 port,u8 bank,u8 b_gpio,u8 en)
{
    u32 val;    

    bank-=0xa;
    b_gpio=bank*8+b_gpio;
        
    val=reg_readl(RK_GPIO_VIRT(port)+GPIO_INTEN);
    rkpm_ddr_printascii("\n inten:");
    rkpm_ddr_printhex(val);
    
    rkpm_ddr_printascii("-");
    if(en==1)
        val|=(0x1<<b_gpio);
    else //
        val&=~(0x1<<b_gpio);

    reg_writel(val,RK_GPIO_VIRT(port)+GPIO_INTEN);
    dsb();
     
     rkpm_ddr_printhex(val);
     rkpm_ddr_printascii("-");
     
     rkpm_ddr_printhex(reg_readl(RK_GPIO_VIRT(port)+GPIO_INTEN));
    
    rkpm_ddr_printascii("\n");

     
}
#if 0
static void __sramfunc sram_pin_set_fun(u8 port,u8 bank,u8 b_gpio,u8 fun)
{ 
        pin_set_fun(port,bank,b_gpio,fun); 
}
static u8 __sramfunc sram_pin_get_funset(u8 port,u8 bank,u8 b_gpio)
{ 
    return pin_get_funset(port,bank,b_gpio); 
}

static void __sramfunc sram_pin_set_pull(u8 port,u8 bank,u8 b_gpio,u8 fun)
{ 
        pin_set_pull(port,bank,b_gpio,fun); 
}
static u8 __sramfunc sram_pin_get_pullset(u8 port,u8 bank,u8 b_gpio)
{ 
    return pin_get_pullset(port,bank,b_gpio); 
}

static void __sramfunc sram_gpio_set_in_output(u8 port,u8 bank,u8 b_gpio,u8 type)
{
    gpio_set_in_output(port,bank,b_gpio,type);
}

static u8 __sramfunc sram_gpio_get_in_outputset(u8 port,u8 bank,u8 b_gpio)
{
    return gpio_get_in_outputset(port,bank,b_gpio);
}

static void __sramfunc sram_gpio_set_output_level(u8 port,u8 bank,u8 b_gpio,u8 level)
{
    
    gpio_set_output_level(port,bank,b_gpio,level);

}

static u8 __sramfunc sram_gpio_get_output_levelset(u8 port,u8 bank,u8 b_gpio)
{
    return gpio_get_output_levelset(port,bank,b_gpio);
}
#endif
#if 0
static u8 __sramfunc sram_gpio_get_input_level(u8 port,u8 bank,u8 b_gpio)
{
    return gpio_get_input_level(port,bank,b_gpio);
}
#endif
//ddr
static void ddr_pin_set_fun(u8 port,u8 bank,u8 b_gpio,u8 fun)
{ 
        pin_set_fun(port,bank,b_gpio,fun); 
}
#if 0
static u8 ddr_pin_get_funset(u8 port,u8 bank,u8 b_gpio)
{ 
    return pin_get_funset(port,bank,b_gpio); 
}
static u8 ddr_pin_get_pullset(u8 port,u8 bank,u8 b_gpio)
{ 
    return pin_get_pullset(port,bank,b_gpio); 
}
static u8 ddr_gpio_get_in_outputset(u8 port,u8 bank,u8 b_gpio)
{
    return gpio_get_in_outputset(port,bank,b_gpio);
}

static u8 ddr_gpio_get_output_levelset(u8 port,u8 bank,u8 b_gpio)
{
    return gpio_get_output_levelset(port,bank,b_gpio);
}
static u8 ddr_gpio_get_input_level(u8 port,u8 bank,u8 b_gpio)
{
    return gpio_get_input_level(port,bank,b_gpio);
}


#endif


static void ddr_pin_set_pull(u8 port,u8 bank,u8 b_gpio,u8 fun)
{ 
        pin_set_pull(port,bank,b_gpio,fun); 
}

static void ddr_gpio_set_in_output(u8 port,u8 bank,u8 b_gpio,u8 type)
{
    gpio_set_in_output(port,bank,b_gpio,type);
}
static void ddr_gpio_set_output_level(u8 port,u8 bank,u8 b_gpio,u8 level)
{   
    gpio_set_output_level(port,bank,b_gpio,level);
}



#define GPIO_DTS_NUM (20)
static  u32 suspend_gpios[GPIO_DTS_NUM];
static  u32 resume_gpios[GPIO_DTS_NUM];

static int of_find_property_value_getsize(const struct device_node *np,const char *propname)
{
	struct property *prop = of_find_property(np, propname, NULL);

	if (!prop)
		return 0;
	if (!prop->value)
		return 0;
	return prop->length;
}

static  void rkpm_pin_gpio_config(u32 pin_gpio_bits)
{
    
    u32 pins;
    u8 port,bank,b_gpio,fun,in_out, level, pull;
   
    pins=RKPM_PINGPIO_BITS_PIN(pin_gpio_bits);      
    in_out=RKPM_PINGPIO_BITS_INOUT(pin_gpio_bits);       
    pull=RKPM_PINGPIO_BITS_PULL(pin_gpio_bits);          
    level=RKPM_PINGPIO_BITS_LEVEL(pin_gpio_bits);     

    port=RKPM_PINBITS_PORT(pins);
    bank=RKPM_PINBITS_BANK(pins);
    b_gpio=RKPM_PINBITS_BGPIO(pins);
    fun=RKPM_PINBITS_FUN(pins);
    
  
    if(!fun)
   {
        if(in_out==RKPM_GPIO_OUTPUT)
        {
            if(level==RKPM_GPIO_OUT_L)
                pull=RKPM_GPIO_PULL_DN;
            else
                pull=RKPM_GPIO_PULL_UP;
            
            ddr_gpio_set_output_level(port,bank,b_gpio,level);       
        }            
        //rkpm_ddr_printhex(pins);

        ddr_gpio_set_in_output(port,bank,b_gpio,in_out);
    }

    ddr_pin_set_pull(port,bank,b_gpio,pull);                
    ddr_pin_set_fun(port,bank,b_gpio,fun);
    
   
    
}

#define RKPM_PINGPIO_BITS_PINTOPORT(pin_gpio_bits) RKPM_PINBITS_PORT(RKPM_PINGPIO_BITS_PIN((pin_gpio_bits)))
#define  rkpm_gpio_pclk_idx(port) ((port)==0) ? RK3288_CLKGATE_PCLK_GPIO0 : (RK3288_CLKGATE_PCLK_GPIO1+(port)-1)

//rk3288_powermode
static void rkpm_pins_setting(u32 *gpios,u32 cnt)
{
       u32 i,clk_id; 
       u32 gpio_clk_reg[9];
       u8 port;
       
      // rkpm_ddr_printascii("\ngpios");
       
        for(i=0;i<9;i++)
        {
            gpio_clk_reg[i]=0xffff0000;
        }
       
       for(i=0;i<cnt;i++)
       {
            if(gpios[i]!=0)
           {
                port=RKPM_PINGPIO_BITS_PINTOPORT(gpios[i]);
                if(gpio_clk_reg[port]==0xffff0000)
                {
                    clk_id=rkpm_gpio_pclk_idx(port);
                    gpio_clk_reg[port]=cru_readl(RK3288_CRU_GATEID_CONS(clk_id))&0xffff;
                    RK3288_CRU_UNGATING_OPS(clk_id);
                }
               // rkpm_ddr_printhex(gpios[i]);
                rkpm_pin_gpio_config(gpios[i]);
           }           
       }
      // rkpm_ddr_printascii("\n");
       
 #if 0       
        for(i=0;i<9;i++)
       {
           rkpm_ddr_regs_dump(RK_GPIO_VIRT(i),0,0x4); 
       }
       //
       rkpm_ddr_regs_dump(RK_GRF_VIRT,0xc,0x84); 
       rkpm_ddr_regs_dump(RK_GRF_VIRT,0x14c,0x1b4);     
     //  rkpm_ddr_regs_dump(RK_PMU_VIRT,0x64,0x6c);   
       //rkpm_ddr_regs_dump(RK_PMU_VIRT,0x84,0x9c); 
   #endif
   
        for(i=0;i<9;i++)
       {
            if(gpio_clk_reg[i]!=0xffff0000)
            {          
                clk_id=rkpm_gpio_pclk_idx(i);           
                cru_writel(gpio_clk_reg[i]|CRU_W_MSK(clk_id%16,0x1),RK3288_CRU_GATEID_CONS(clk_id));    
            }
       }
       
}

static void  rkpm_gpio_suspend(void)
{
    rkpm_pins_setting(&suspend_gpios[0],GPIO_DTS_NUM);
}



static void  rkpm_gpio_resume(void)
{     
     rkpm_pins_setting(&resume_gpios[0],GPIO_DTS_NUM);
}

#if 1
static void gpio_get_dts_info(struct device_node *parent)
{
        int i;
        size_t temp_len;
    //return;

        for(i=0;i<GPIO_DTS_NUM;i++)
        {
            suspend_gpios[i]=0;
            resume_gpios[i]=0;
        }
 
     #if 1   
        temp_len=of_find_property_value_getsize(parent,"rockchip,pmic-suspend_gpios");
        if(temp_len)
        {
            printk("%s suspend:%d\n",__FUNCTION__,temp_len);
            if(temp_len)
            {
                if(of_property_read_u32_array(parent,"rockchip,pmic-suspend_gpios",&suspend_gpios[0],temp_len/4))
                {
                        suspend_gpios[0]=0;
                       printk("%s:get pm ctr error\n",__FUNCTION__);
                }
            }
        }

       temp_len=of_find_property_value_getsize(parent,"rockchip,pmic-resume_gpios");
       if(temp_len)
       {
           printk("%s resume:%d\n",__FUNCTION__,temp_len);
           if(of_property_read_u32_array(parent,"rockchip,pmic-resume_gpios",&resume_gpios[0],temp_len/4))
           {
                    resume_gpios[0]=0;
                   printk("%s:get pm ctr error\n",__FUNCTION__);
           }
        }  
     #endif
     
     printk("rockchip,pmic-suspend_gpios:");
     for(i=0;i<GPIO_DTS_NUM;i++)
     {
         printk("%x ",suspend_gpios[i]);
         if(i==(GPIO_DTS_NUM-1))
             printk("\n");
     }
 
     printk("rockchip,pmic-resume_gpios:");
     for(i=0;i<GPIO_DTS_NUM;i++)
     {
          printk("%x ",resume_gpios[i]);
          if(i==(GPIO_DTS_NUM-1))
              printk("\n");
     }
     
   rkpm_set_ops_gpios(rkpm_gpio_suspend,rkpm_gpio_resume);

}
#endif

/*******************************clk gating config*******************************************/
#define CLK_MSK_GATING(msk, con) cru_writel((msk << 16) | 0xffff, con)
#define CLK_MSK_UNGATING(msk, con) cru_writel(((~msk) << 16) | 0xffff, con)


static u32 clk_ungt_msk[RK3288_CRU_CLKGATES_CON_CNT];// first clk gating setting
static u32 clk_ungt_msk_1[RK3288_CRU_CLKGATES_CON_CNT];// first clk gating setting
static u32 clk_ungt_save[RK3288_CRU_CLKGATES_CON_CNT]; //first clk gating value saveing


u32 DEFINE_PIE_DATA(rkpm_clkgt_last_set[RK3288_CRU_CLKGATES_CON_CNT]);
static u32 *p_rkpm_clkgt_last_set;

static __sramdata u32 rkpm_clkgt_last_save[RK3288_CRU_CLKGATES_CON_CNT];

void PIE_FUNC(gtclks_sram_suspend)(void)
{
    int i;
   // u32 u_clk_id=(RK3188_CLKGATE_UART0_SRC+CONFIG_RK_DEBUG_UART);
   // u32 u_pclk_id=(RK3188_CLKGATE_PCLK_UART0+CONFIG_RK_DEBUG_UART);

    for(i=0;i<RK3288_CRU_CLKGATES_CON_CNT;i++)
    {
        rkpm_clkgt_last_save[i]=cru_readl(RK3288_CRU_CLKGATES_CON(i));     
        CLK_MSK_UNGATING( DATA(rkpm_clkgt_last_set[i]), RK3288_CRU_CLKGATES_CON(i));      
        #if 0
        rkpm_sram_printch('\n');   
        rkpm_sram_printhex(DATA(rkpm_clkgt_last_save[i]));
        rkpm_sram_printch('-');   
        rkpm_sram_printhex(DATA(rkpm_clkgt_last_set[i]));
        rkpm_sram_printch('-');   
        rkpm_sram_printhex(cru_readl(RK3188_CRU_CLKGATES_CON(i)));
        if(i==(RK3288_CRU_CLKGATES_CON_CNT-1))         
        rkpm_sram_printch('\n');   
        #endif
    }
    
        //RK3288_CRU_UNGATING_OPS(u_clk_id);
        //RK3288_CRU_UNGATING_OPS(u_pclk_id);
 
}

void PIE_FUNC(gtclks_sram_resume)(void)
{
    int i;
    for(i=0;i<RK3288_CRU_CLKGATES_CON_CNT;i++)
    {
        cru_writel(rkpm_clkgt_last_save[i]|0xffff0000, RK3288_CRU_CLKGATES_CON(i));
    }
}
#define grf_readl(offset)	readl_relaxed(RK_GRF_VIRT + offset)
#define grf_writel(v, offset)	do { writel_relaxed(v, RK_GRF_VIRT + offset); dsb(); } while (0)

#define gpio7_readl(offset)	readl_relaxed(RK_GPIO_VIRT(7)+ offset)
#define gpio7_writel(v, offset)	do { writel_relaxed(v, RK_GPIO_VIRT(7) + offset); dsb(); } while (0)

int gpio7_pin_data1, gpio7_pin_dir1;
int gpio7_pin_iomux1;

static void gtclks_suspend(void)
{
    int i;
	gpio7_pin_data1= gpio7_readl(0);
	gpio7_pin_dir1 = gpio7_readl(0x04);
	gpio7_pin_iomux1 =  gpio7_readl(0x6c);
	grf_writel(0x00040000, 0x6c);
	gpio7_writel(gpio7_pin_dir1|0x2, 0x04);
	gpio7_writel((gpio7_pin_data1|2), 0x00);

  // rkpm_ddr_regs_dump(RK_CRU_VIRT,RK3288_CRU_CLKGATES_CON(0)
                                          //          ,RK3288_CRU_CLKGATES_CON(RK3288_CRU_CLKGATES_CON_CNT-1));
    for(i=0;i<RK3288_CRU_CLKGATES_CON_CNT;i++)
    {
            clk_ungt_save[i]=cru_readl(RK3288_CRU_CLKGATES_CON(i));   
           // 160 1a8
           #if 0
           if(
               // RK3288_CRU_CLKGATES_CON(i)==0x160 ||
                //RK3288_CRU_CLKGATES_CON(i)==0x164 ||
                //RK3288_CRU_CLKGATES_CON(i)==0x168 ||
              //  RK3288_CRU_CLKGATES_CON(i)==0x16c ||
                //RK3288_CRU_CLKGATES_CON(i)==0x170 ||
               // RK3288_CRU_CLKGATES_CON(i)==0x174 ||
               // RK3288_CRU_CLKGATES_CON(i)==0x178 ||

           
                //RK3288_CRU_CLKGATES_CON(i)==0x17c ||
               // RK3288_CRU_CLKGATES_CON(i)==0x180 ||
               // RK3288_CRU_CLKGATES_CON(i)==0x184 ||
               // RK3288_CRU_CLKGATES_CON(i)==0x188 ||
                //RK3288_CRU_CLKGATES_CON(i)==0x18c ||
                //RK3288_CRU_CLKGATES_CON(i)==0x190 ||
                //RK3288_CRU_CLKGATES_CON(i)==0x194 ||
                //RK3288_CRU_CLKGATES_CON(i)==0x198 ||
                //RK3288_CRU_CLKGATES_CON(i)==0x19c ||
                //RK3288_CRU_CLKGATES_CON(i)==0x1a0 ||
                //RK3288_CRU_CLKGATES_CON(i)==0x1a4 ||      
               // RK3288_CRU_CLKGATES_CON(i)==0x1a8
               RK3288_CRU_CLKGATES_CON(i)==0xfff
            )
            {
            
                 cru_writel(0xffff0000, RK3288_CRU_CLKGATES_CON(i));
               // CLK_MSK_UNGATING(clk_ungt_msk[i],RK3288_CRU_CLKGATES_CON(i));
            
            }
           else
            #endif
            {
               // if(RK3288_CRU_CLKGATES_CON(i)!=0x188 )
               CLK_MSK_UNGATING(clk_ungt_msk[i],RK3288_CRU_CLKGATES_CON(i));
           }
           #if 0
            rkpm_ddr_printch('\n');   
            rkpm_ddr_printhex(RK3288_CRU_CLKGATES_CON(i));
            rkpm_ddr_printch('-');   
            rkpm_ddr_printhex(clk_ungt_msk[i]);
            rkpm_ddr_printch('-');   
            rkpm_ddr_printhex(cru_readl(RK3288_CRU_CLKGATES_CON(i))) ;  
            if(i==(RK3288_CRU_CLKGATES_CON_CNT-1))            
            rkpm_ddr_printch('\n');   
            #endif
    }

}

static void gtclks_resume(void)
{
    int i;
     for(i=0;i<RK3288_CRU_CLKGATES_CON_CNT;i++)
    {
       cru_writel(clk_ungt_save[i]|0xffff0000,RK3288_CRU_CLKGATES_CON(i));       
     }
     //rkpm_ddr_regs_dump(RK_CRU_VIRT,RK3288_CRU_CLKGATES_CON(0)
                                                 //   ,RK3288_CRU_CLKGATES_CON(RK3288_CRU_CLKGATES_CON_CNT-1));
	grf_writel(0x00040004, 0x6c);

}
/********************************pll power down***************************************/

static void pm_pll_wait_lock(u32 pll_idx)
{
	u32 delay = 600000U;
       // u32 mode;
     //  mode=cru_readl(RK3288_CRU_MODE_CON);
	dsb();
	dsb();
	dsb();
	dsb();
	dsb();
	dsb();
	while (delay > 0) {
		if ((cru_readl(RK3288_PLL_CONS(pll_idx,1))&(0x1<<31)))
			break;
		delay--;
	}
	if (delay == 0) {
		rkpm_ddr_printascii("unlock-pll:");
		rkpm_ddr_printhex(pll_idx);
		rkpm_ddr_printch('\n');
	}
    //cru_writel(mode|(RK3288_PLL_MODE_MSK(pll_idx)<<16), RK3288_CRU_MODE_CON);
}	

static void pll_udelay(u32 udelay)
{
    u32 mode;
    mode=cru_readl(RK3288_CRU_MODE_CON);
    // delay in 24m
    cru_writel(RK3288_PLL_MODE_SLOW(APLL_ID), RK3288_CRU_MODE_CON);
    
    rkpm_udelay(udelay*5);
    
    cru_writel(mode|(RK3288_PLL_MODE_MSK(APLL_ID)<<16), RK3288_CRU_MODE_CON);
}

static u32 plls_con0_save[END_PLL_ID];
static u32 plls_con1_save[END_PLL_ID];
static u32 plls_con2_save[END_PLL_ID];
static u32 plls_con3_save[END_PLL_ID];

static u32 cru_mode_con;

static inline void plls_suspend(u32 pll_id)
{
    plls_con0_save[pll_id]=cru_readl(RK3288_PLL_CONS((pll_id), 0));
    plls_con1_save[pll_id]=cru_readl(RK3288_PLL_CONS((pll_id), 1));
    plls_con2_save[pll_id]=cru_readl(RK3288_PLL_CONS((pll_id), 2));
    plls_con3_save[pll_id]=cru_readl(RK3288_PLL_CONS((pll_id), 3));
 
    cru_writel(RK3288_PLL_PWR_DN, RK3288_PLL_CONS((pll_id), 3));
    
}
static inline void plls_resume(u32 pll_id)
{
        u32 pllcon0, pllcon1, pllcon2;

        if((plls_con3_save[pll_id]&RK3288_PLL_PWR_DN_MSK))
            return ;
         
        //enter slowmode
        cru_writel(RK3288_PLL_MODE_SLOW(pll_id), RK3288_CRU_MODE_CON);      
        
        cru_writel(RK3288_PLL_PWR_ON, RK3288_PLL_CONS((pll_id),3));
        cru_writel(RK3288_PLL_NO_BYPASS, RK3288_PLL_CONS((pll_id),3));
        
        pllcon0 =plls_con0_save[pll_id];// cru_readl(RK3288_PLL_CONS((pll_id),0));
        pllcon1 = plls_con1_save[pll_id];//cru_readl(RK3288_PLL_CONS((pll_id),1));
        pllcon2 = plls_con2_save[pll_id];//cru_readl(RK3288_PLL_CONS((pll_id),2));

        //enter rest
        cru_writel(RK3288_PLL_RESET, RK3288_PLL_CONS(pll_id,3));
        cru_writel(pllcon0|CRU_W_MSK(0,0xf)|CRU_W_MSK(8,0x3f), RK3288_PLL_CONS(pll_id,0));
        cru_writel(pllcon1, RK3288_PLL_CONS(pll_id,1));
        cru_writel(pllcon2, RK3288_PLL_CONS(pll_id,2));
        
        pll_udelay(5);
        //udelay(5); //timer7 delay

        //return form rest
        cru_writel(RK3288_PLL_RESET_RESUME, RK3288_PLL_CONS(pll_id,3));

        //wating lock state
        pll_udelay(168);
        pm_pll_wait_lock(pll_id);
        
        cru_writel(plls_con3_save[pll_id]|(RK3288_PLL_BYPASS_MSK<<16),RK3288_PLL_CONS(pll_id,3));

}

static u32 clk_sel0,clk_sel1, clk_sel10,clk_sel26,clk_sel33,clk_sel36, clk_sel37;

static void pm_plls_suspend(void)
{

   // rkpm_ddr_regs_dump(RK_CRU_VIRT,RK3288_PLL_CONS((0), 0),RK3288_PLL_CONS((4), 3)); 
   // rkpm_ddr_regs_dump(RK_CRU_VIRT,RK3288_CRU_MODE_CON,RK3288_CRU_MODE_CON);   
   // rkpm_ddr_regs_dump(RK_CRU_VIRT,RK3288_CRU_CLKSELS_CON(0),RK3288_CRU_CLKSELS_CON(42));
    
    clk_sel0=cru_readl(RK3288_CRU_CLKSELS_CON(0));
    clk_sel1=cru_readl(RK3288_CRU_CLKSELS_CON(1));
    clk_sel10=cru_readl(RK3288_CRU_CLKSELS_CON(10));
    clk_sel26=cru_readl(RK3288_CRU_CLKSELS_CON(26));    
    clk_sel33=cru_readl(RK3288_CRU_CLKSELS_CON(33));
    clk_sel36=cru_readl(RK3288_CRU_CLKSELS_CON(36));
    clk_sel37=cru_readl(RK3288_CRU_CLKSELS_CON(37));
    
    cru_mode_con = cru_readl(RK3288_CRU_MODE_CON);


    cru_writel(RK3288_PLL_MODE_SLOW(NPLL_ID), RK3288_CRU_MODE_CON);  
    plls_suspend(NPLL_ID);
    
// cpll
    cru_writel(RK3288_PLL_MODE_SLOW(CPLL_ID), RK3288_CRU_MODE_CON);
  
// gpll 
    cru_writel(RK3288_PLL_MODE_SLOW(GPLL_ID), RK3288_CRU_MODE_CON); 

    // set 1,pdbus pll is gpll
    cru_writel(CRU_W_MSK_SETBITS(1,15,0x1), RK3288_CRU_CLKSELS_CON(1)); // 0 cpll 1gpll

    // pd_bus clk 
    cru_writel(0
                        |CRU_W_MSK_SETBITS(0,0,0x7)  //  1  aclk
                        |CRU_W_MSK_SETBITS(0,3,0x1f) //  1   aclk src
                        |CRU_W_MSK_SETBITS(0,8,0x3) // 1   hclk 0~1 1 2 4
                        |CRU_W_MSK_SETBITS(0,12,0x7) //  3   pclk
                     , RK3288_CRU_CLKSELS_CON(1));
    
    //crypto for pd_bus
    cru_writel(CRU_W_MSK_SETBITS(3,6,0x3), RK3288_CRU_CLKSELS_CON(26));

    // peri aclk hclk pclk
    cru_writel(0
                        |CRU_W_MSK_SETBITS(0,0,0x1f) // 1 aclk
                        |CRU_W_MSK_SETBITS(0,8,0x3) // 2   hclk 0 1:1,1 2:1 ,2 4:1
                        |CRU_W_MSK_SETBITS(0,12,0x3)// 2     0~3  1 2 4 8 div
                        , RK3288_CRU_CLKSELS_CON(10));
    // pmu alive 
    cru_writel(CRU_W_MSK_SETBITS(0,0,0x1f)|CRU_W_MSK_SETBITS(0,8,0x1f), RK3288_CRU_CLKSELS_CON(33));

    plls_suspend(CPLL_ID);
    plls_suspend(GPLL_ID);

//apll 
   cru_writel(RK3288_PLL_MODE_SLOW(APLL_ID), RK3288_CRU_MODE_CON);
     // core_m0 core_mp a12_core
    cru_writel(0
                        |CRU_W_MSK_SETBITS(0,0,0xf) // 1   axi_mo
                        |CRU_W_MSK_SETBITS(0,4,0xf) // 3  axi mp
                        |CRU_W_MSK_SETBITS(0,8,0x1f) // 0 a12 core div
                      , RK3288_CRU_CLKSELS_CON(0));
    // core0 core1 core2 core3
    cru_writel(0
                        |CRU_W_MSK_SETBITS(0,0,0x7) //core 0 div
                        |CRU_W_MSK_SETBITS(0,4,0x7) // core 1
                        |CRU_W_MSK_SETBITS(0,8,0x7) // core2
                        |CRU_W_MSK_SETBITS(0,12,0x7)//core3
                      , RK3288_CRU_CLKSELS_CON(36));
    // l2ram atclk pclk
    #if 1
    cru_writel(0
                    |CRU_W_MSK_SETBITS(3,0,0x7) // l2ram
                    |CRU_W_MSK_SETBITS(0xf,4,0x1f) // atclk
                     |CRU_W_MSK_SETBITS(0xf,9,0x1f) // pclk dbg
                     , RK3288_CRU_CLKSELS_CON(37));
    #else
    cru_writel(0
                      |CRU_W_MSK_SETBITS(0,0,0x7) // l2ram
                      |CRU_W_MSK_SETBITS(0x2,4,0x1f) // atclk
                       |CRU_W_MSK_SETBITS(0x2,9,0x1f) // pclk dbg
                       , RK3288_CRU_CLKSELS_CON(37));
    #endif

    
    plls_suspend(APLL_ID);

}

static void pm_plls_resume(void)
{


        // core_m0 core_mp a12_core
        cru_writel(clk_sel0|(CRU_W_MSK(0,0xf)|CRU_W_MSK(4,0xf)|CRU_W_MSK(8,0xf)),RK3288_CRU_CLKSELS_CON(0));
        // core0 core1 core2 core3
        cru_writel(clk_sel36|(CRU_W_MSK(0,0x7)|CRU_W_MSK(4,0x7)|CRU_W_MSK(8,0x7)|CRU_W_MSK(12,0x7))
                        , RK3288_CRU_CLKSELS_CON(36));
        // l2ram atclk pclk
        cru_writel(clk_sel37|(CRU_W_MSK(0,0x7)|CRU_W_MSK(4,0x1f)|CRU_W_MSK(9,0x1f)) , RK3288_CRU_CLKSELS_CON(37));
        
        plls_resume(APLL_ID);    
        cru_writel(cru_mode_con|(RK3288_PLL_MODE_MSK(APLL_ID)<<16), RK3288_CRU_MODE_CON);
        
        // peri aclk hclk pclk
        cru_writel(clk_sel10|(CRU_W_MSK(0,0x1f)|CRU_W_MSK(8,0x3)|CRU_W_MSK(12,0x3))
                                                                            , RK3288_CRU_CLKSELS_CON(10));
        //pd bus gpll sel
        cru_writel(clk_sel1|CRU_W_MSK(15,0x1), RK3288_CRU_CLKSELS_CON(1));
        // pd_bus clk 
        cru_writel(clk_sel1|(CRU_W_MSK(0,0x7)|CRU_W_MSK(3,0x1f)|CRU_W_MSK(8,0x3)|CRU_W_MSK(12,0x7))
                    , RK3288_CRU_CLKSELS_CON(1));
                
        // crypto
        cru_writel(clk_sel26|CRU_W_MSK(6,0x3), RK3288_CRU_CLKSELS_CON(26));
    
        
          // pmu alive 
        cru_writel(clk_sel33|CRU_W_MSK(0,0x1f)|CRU_W_MSK(8,0x1f), RK3288_CRU_CLKSELS_CON(33));

        plls_resume(GPLL_ID);   
        cru_writel(cru_mode_con|(RK3288_PLL_MODE_MSK(GPLL_ID)<<16), RK3288_CRU_MODE_CON);       
        
        plls_resume(CPLL_ID);    
        cru_writel(cru_mode_con|(RK3288_PLL_MODE_MSK(CPLL_ID)<<16), RK3288_CRU_MODE_CON);
        
        plls_resume(NPLL_ID);       
        cru_writel(cru_mode_con|(RK3288_PLL_MODE_MSK(NPLL_ID)<<16), RK3288_CRU_MODE_CON);

       // rkpm_ddr_regs_dump(RK_CRU_VIRT,RK3288_PLL_CONS((0), 0),RK3288_PLL_CONS((4), 3)); 
       // rkpm_ddr_regs_dump(RK_CRU_VIRT,RK3288_CRU_MODE_CON,RK3288_CRU_MODE_CON);   
       // rkpm_ddr_regs_dump(RK_CRU_VIRT,RK3288_CRU_CLKSELS_CON(0),RK3288_CRU_CLKSELS_CON(42));
        
}

static __sramdata u32  sysclk_clksel0_con,sysclk_clksel1_con,sysclk_clksel10_con,sysclk_mode_con;

void PIE_FUNC(sysclk_suspend)(u32 sel_clk)
{

    int div;  
    sysclk_clksel0_con = cru_readl(RK3288_CRU_CLKSELS_CON(0));
    sysclk_clksel1_con = cru_readl(RK3288_CRU_CLKSELS_CON(1));
    sysclk_clksel10_con= cru_readl(RK3288_CRU_CLKSELS_CON(10));


    if(sel_clk&(RKPM_CTR_SYSCLK_32K))
    {
        div=3;
        sysclk_mode_con= cru_readl(RK3288_CRU_MODE_CON);
        cru_writel(0
                |RK3288_PLL_MODE_DEEP(APLL_ID)| RK3288_PLL_MODE_DEEP(CPLL_ID)
                | RK3288_PLL_MODE_DEEP(GPLL_ID)|RK3288_PLL_MODE_DEEP(NPLL_ID)
                            , RK3288_CRU_MODE_CON);
    }
    else if(sel_clk&(RKPM_CTR_SYSCLK_DIV))
    {      
        div=31;
    }

    cru_writel(CRU_W_MSK_SETBITS(div,8,0x1f), RK3188_CRU_CLKSELS_CON(0)); //pd core
    cru_writel(CRU_W_MSK_SETBITS(div,3,0x1f), RK3188_CRU_CLKSELS_CON(1));//pd bus
    cru_writel(CRU_W_MSK_SETBITS(div,0,0x1f), RK3188_CRU_CLKSELS_CON(10));//pd peri
    
}

void PIE_FUNC(sysclk_resume)(u32 sel_clk)
{
    
    cru_writel(sysclk_clksel0_con|CRU_W_MSK(8,0x1f), RK3188_CRU_CLKSELS_CON(0)); //pd core
    cru_writel(sysclk_clksel1_con|CRU_W_MSK(3,0x1f), RK3188_CRU_CLKSELS_CON(1));//pd bus
    cru_writel(sysclk_clksel10_con|CRU_W_MSK(0,0x1f), RK3188_CRU_CLKSELS_CON(10));//pd peri
    cru_writel(sysclk_mode_con|(RK3288_PLL_MODE_MSK(APLL_ID)<<16)
                            |(RK3288_PLL_MODE_MSK(CPLL_ID)<<16)
                            |(RK3288_PLL_MODE_MSK(GPLL_ID)<<16)
                            |(RK3288_PLL_MODE_MSK(NPLL_ID)<<16), RK3288_CRU_MODE_CON);

}


static void clks_gating_suspend_init(void)
{
    // get clk gating info
    if(rockchip_pie_chunk)
        p_rkpm_clkgt_last_set= kern_to_pie(rockchip_pie_chunk, &DATA(rkpm_clkgt_last_set[0]));
    else
        p_rkpm_clkgt_last_set=&clk_ungt_msk_1[0];
    if(clk_suspend_clkgt_info_get(clk_ungt_msk,p_rkpm_clkgt_last_set, RK3288_CRU_CLKGATES_CON_CNT) 
        ==RK3288_CRU_CLKGATES_CON(0))
    {
        rkpm_set_ops_gtclks(gtclks_suspend,gtclks_resume);
        if(rockchip_pie_chunk)
            rkpm_set_sram_ops_gtclks(fn_to_pie(rockchip_pie_chunk, &FUNC(gtclks_sram_suspend)), 
                                fn_to_pie(rockchip_pie_chunk, &FUNC(gtclks_sram_resume)));
        
        PM_LOG("%s:clkgt info ok\n",__FUNCTION__);

    }
    if(rockchip_pie_chunk)
        rkpm_set_sram_ops_sysclk(fn_to_pie(rockchip_pie_chunk, &FUNC(sysclk_suspend))
                                                ,fn_to_pie(rockchip_pie_chunk, &FUNC(sysclk_resume))); 
}

/***************************prepare and finish reg_pread***********************************/



#define GIC_DIST_PENDING_SET		0x200
static noinline void rk3288_pm_dump_irq(void)
{
	u32 irq_gpio = (readl_relaxed(RK_GIC_VIRT + GIC_DIST_PENDING_SET + 12) >> 17) & 0x1FF;
	u32 irq[4];
	int i;

	for (i = 0; i < ARRAY_SIZE(irq); i++)
		irq[i] = readl_relaxed(RK_GIC_VIRT + GIC_DIST_PENDING_SET + (1 + i) * 4);
	for (i = 0; i < ARRAY_SIZE(irq); i++) {
		if (irq[i])
			log_wakeup_reason(32 * (i + 1) + fls(irq[i]) - 1);
	}
	printk("wakeup irq: %08x %08x %08x %08x\n", irq[0], irq[1], irq[2], irq[3]);
	for (i = 0; i <= 8; i++) {
		if (irq_gpio & (1 << i))
			printk("wakeup gpio%d: %08x\n", i, readl_relaxed(RK_GPIO_VIRT(i) + GPIO_INT_STATUS));
	}
}

#if 0
#define DUMP_GPIO_INTEN(ID) \
do { \
	u32 en = readl_relaxed(RK_GPIO_VIRT(ID) + GPIO_INTEN); \
	if (en) { \
		rkpm_ddr_printascii("GPIO" #ID "_INTEN: "); \
		rkpm_ddr_printhex(en); \
		rkpm_ddr_printch('\n'); \
		printk(KERN_DEBUG "GPIO%d_INTEN: %08x\n", ID, en); \
	} \
} while (0)
#else

#define DUMP_GPIO_INTEN(ID) \
    do { \
    	u32 en = readl_relaxed(RK_GPIO_VIRT(ID) + GPIO_INTEN); \
    	if (en) { \
    		printk("GPIO%d_INTEN: %08x\n", ID, en); \
    	} \
    } while (0)

#endif


//dump while irq is enable
static noinline void rk3288_pm_dump_inten(void)
{
	DUMP_GPIO_INTEN(0);
	DUMP_GPIO_INTEN(1);
	DUMP_GPIO_INTEN(2);
	DUMP_GPIO_INTEN(3);
    	DUMP_GPIO_INTEN(4);
	DUMP_GPIO_INTEN(5);
	DUMP_GPIO_INTEN(6);
	DUMP_GPIO_INTEN(7);    
	DUMP_GPIO_INTEN(8);
}

static  void rkpm_prepare(void)
{   

        int i;
         for(i=0;i<RK3288_CRU_CLKGATES_CON_CNT;i++)
        {
           //cru_writel(0xffff0000,RK3288_CRU_CLKGATES_CON(i));       
         }

        #if 0
        u32 temp =reg_readl(RK_GPIO_VIRT(0)+0x30);

       // rkpm_ddr_printhex(temp);
        reg_writel(temp|0x1<<4,RK_GPIO_VIRT(0)+0x30);
        temp =reg_readl(RK_GPIO_VIRT(0)+0x30);
       // rkpm_ddr_printhex(temp);
        #endif             
	// dump GPIO INTEN for debug
	rk3288_pm_dump_inten();
}

static void rkpm_finish(void)
{
	rk3288_pm_dump_irq();
}

#if 0
static  void interface_ctr_reg_pread(void)
{
	//u32 addr;
	flush_cache_all();
	outer_flush_all();
	local_flush_tlb_all();
        #if 0  // do it in ddr suspend 
	for (addr = (u32)SRAM_CODE_OFFSET; addr < (u32)(SRAM_CODE_OFFSET+rockchip_sram_size); addr += PAGE_SIZE)
		readl_relaxed(addr);
        #endif
        readl_relaxed(RK_PMU_VIRT);
        readl_relaxed(RK_GRF_VIRT);
        readl_relaxed(RK_DDR_VIRT);
        readl_relaxed(RK_GPIO_VIRT(0));     
        //readl_relaxed(RK30_I2C1_BASE+SZ_4K);
        //readl_relaxed(RK_GPIO_VIRT(3));
}
#endif
void PIE_FUNC(ddr_leakage_tst)(void)
{
    cru_writel(RK3288_PLL_MODE_SLOW(DPLL_ID), RK3288_CRU_MODE_CON);    
    rkpm_sram_printch('\n');   
    rkpm_sram_printch('t');   
    rkpm_sram_printch('e');   
    rkpm_sram_printch('s');
    rkpm_sram_printch('t');   
    while(1);               
}

static void __init  rk3288_suspend_init(void)
{
    struct device_node *parent;
    u32 pm_ctrbits;

    PM_LOG("%s enter\n",__FUNCTION__);

    parent = of_find_node_by_name(NULL, "rockchip_suspend");    

    if (IS_ERR_OR_NULL(parent)) {
		PM_ERR("%s dev node err\n", __func__);
		return;
	}


    if(of_property_read_u32_array(parent,"rockchip,ctrbits",&pm_ctrbits,1))
    {
            PM_ERR("%s:get pm ctr error\n",__FUNCTION__);
            return ;
    }
    PM_LOG("%s: pm_ctrbits =%x\n",__FUNCTION__,pm_ctrbits);
    pm_io_base_map();
    memset(&sleep_resume_data[0],0,sizeof(sleep_resume_data));
    rkpm_set_ctrbits(pm_ctrbits);
    
    gpio_get_dts_info(parent);
    clks_gating_suspend_init();

    rkpm_set_ops_plls(pm_plls_suspend,pm_plls_resume);
    
    //rkpm_set_sram_ops_ddr(fn_to_pie(rockchip_pie_chunk, &FUNC(ddr_leakage_tst)),NULL);
    
    rkpm_set_ops_prepare_finish(rkpm_prepare,rkpm_finish);
    
    //rkpm_set_ops_regs_pread(interface_ctr_reg_pread);  
    
     rkpm_set_ops_save_setting(rkpm_save_setting,rkpm_save_setting_resume);
     rkpm_set_ops_regs_sleep(rkpm_slp_setting,rkpm_save_setting_resume_first);//rkpm_slp_setting

    if(rockchip_pie_chunk)
        rkpm_set_sram_ops_printch(fn_to_pie(rockchip_pie_chunk, &FUNC(sram_printch)));
    
    rkpm_set_ops_printch(ddr_printch); 	
}
