
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/hardware/cache-l2x0.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <asm/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <linux/rockchip/cpu.h>
#include <linux/rockchip/cru.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/iomap.h>
#include "pm.h"

#define CPU 3188
//#include "sram.h"
#include "pm-pie.c"

#define RK3188_CLK_GATING_OPS(ID) cru_writel((0x1<<((ID%16)+16))|(0x1<<(ID%16)),RK3188_CRU_GATEID_CONS(ID))
#define RK3188_CLK_UNGATING_OPS(ID) cru_writel(0x1<<((ID%16)+16),RK3188_CRU_GATEID_CONS(ID))

/*************************cru define********************************************/
/*******************CRU BITS*******************************/
#define CRU_W_MSK(bits_shift, msk)	((msk) << ((bits_shift) + 16))
#define CRU_SET_BITS(val, bits_shift, msk)	(((val)&(msk)) << (bits_shift))
#define CRU_W_MSK_SETBITS(val, bits_shift,msk) \
	(CRU_W_MSK(bits_shift, msk) | CRU_SET_BITS(val, bits_shift, msk))
	
#define RK3188_CRU_GET_REG_BITS_VAL(reg,bits_shift, msk)  (((reg) >> (bits_shift))&(msk))
#define RK3188_CRU_W_MSK(bits_shift, msk)	((msk) << ((bits_shift) + 16))
#define RK3188_CRU_SET_BITS(val,bits_shift, msk)	(((val)&(msk)) << (bits_shift))
    
#define RK3188_CRU_W_MSK_SETBITS(val,bits_shift,msk) \
        (RK3188_CRU_W_MSK(bits_shift, msk)|RK3188_CRU_SET_BITS(val,bits_shift, msk))
    
    
/*******************RK3188_PLL CON3 BITS***************************/

#define RK3188_PLL_PWR_DN_MSK		(1 << 1)
#define RK3188_PLL_PWR_DN_W_MSK	(RK3188_PLL_PWR_DN_MSK << 16)
#define RK3188_PLL_PWR_DN		(1 << 1)
#define RK3188_PLL_PWR_ON		(0 << 1)
    


/*******************CLKSEL0 BITS***************************/
//RK3188_CORE_preiph div
#define RK3188_CORE_PERIPH_W_MSK	(3 << 22)
#define RK3188_CORE_PERIPH_MSK		(3 << 6)
#define RK3188_CORE_PERIPH_2		(0 << 6)
#define RK3188_CORE_PERIPH_4		(1 << 6)
#define RK3188_CORE_PERIPH_8		(2 << 6)
#define RK3188_CORE_PERIPH_16		(3 << 6)

//clk_RK3188_CORE
#define RK3188_CORE_SEL_PLL_MSK	(1 << 8)
#define RK3188_CORE_SEL_PLL_W_MSK	(1 << 24)
#define RK3188_CORE_SEL_APLL		(0 << 8)
#define RK3188_CORE_SEL_GPLL		(1 << 8)

#define RK3188_CORE_CLK_DIV_W_MSK	(0x1F << 25)
#define RK3188_CORE_CLK_DIV_MSK	(0x1F << 9)
#define RK3188_CORE_CLK_DIV(i)		((((i) - 1) & 0x1F) << 9)
#define RK3188_CORE_CLK_MAX_DIV	32

#define RK3188_CPU_SEL_PLL_MSK		(1 << 5)
#define RK3188_CPU_SEL_PLL_W_MSK	(1 << 21)
#define RK3188_CPU_SEL_APLL		(0 << 5)
#define RK3188_CPU_SEL_GPLL		(1 << 5)

#define RK3188_CPU_CLK_DIV_W_MSK	(0x1F << 16)
#define RK3188_CPU_CLK_DIV_MSK		(0x1F)
#define RK3188_CPU_CLK_DIV(i)		(((i) - 1) & 0x1F)

/*******************CLKSEL1 BITS***************************/
//aclk div
#define RK3188_GET_CORE_ACLK_VAL(reg) ((reg)>=4 ?8:((reg)+1))

#define RK3188_CORE_ACLK_W_MSK		(7 << 19)
#define RK3188_CORE_ACLK_MSK		(7 << 3)
#define RK3188_CORE_ACLK_11		(0 << 3)
#define RK3188_CORE_ACLK_21		(1 << 3)
#define RK3188_CORE_ACLK_31		(2 << 3)
#define RK3188_CORE_ACLK_41		(3 << 3)
#define RK3188_CORE_ACLK_81		(4 << 3)
//hclk div
#define RK3188_ACLK_HCLK_W_MSK		(3 << 24)
#define RK3188_ACLK_HCLK_MSK		(3 << 8)
#define RK3188_ACLK_HCLK_11		(0 << 8)
#define RK3188_ACLK_HCLK_21		(1 << 8)
#define RK3188_ACLK_HCLK_41		(2 << 8)
// pclk div
#define RK3188_ACLK_PCLK_W_MSK		(3 << 28)
#define RK3188_ACLK_PCLK_MSK		(3 << 12)
#define RK3188_ACLK_PCLK_11		(0 << 12)
#define RK3188_ACLK_PCLK_21		(1 << 12)
#define RK3188_ACLK_PCLK_41		(2 << 12)
#define RK3188_ACLK_PCLK_81		(3 << 12)
// ahb2apb div
#define RK3188_AHB2APB_W_MSK		(3 << 30)
#define RK3188_AHB2APB_MSK		(3 << 14)
#define RK3188_AHB2APB_11		(0 << 14)
#define RK3188_AHB2APB_21		(1 << 14)
#define RK3188_AHB2APB_41		(2 << 14)

/*******************clksel10***************************/

#define RK3188_PERI_ACLK_DIV_MASK 0x1f
#define RK3188_PERI_ACLK_DIV_W_MSK	(RK3188_PERI_ACLK_DIV_MASK << 16)
#define RK3188_PERI_ACLK_DIV(i)	(((i) - 1) & RK3188_PERI_ACLK_DIV_MASK)
#define RK3188_PERI_ACLK_DIV_OFF 0

#define RK3188_PERI_HCLK_DIV_MASK 0x3
#define RK3188_PERI_HCLK_DIV_OFF 8

#define RK3188_PERI_PCLK_DIV_MASK 0x3
#define RK3188_PERI_PCLK_DIV_OFF 12



/*************************gate id**************************************/
#define RK3188_CLK_GATEID(i)	(16 * (i))


enum cru_clk_gate {
	/* SCU CLK GATE 0 CON */
	RK3188_CLKGATE_CORE_PERIPH = RK3188_CLK_GATEID(0),

	RK3188_CLKGATE_TIMER0 = RK3188_CLK_GATEID(1),
	RK3188_CLKGATE_UART0_SRC=RK3188_CLK_GATEID(1)+8,
	RK3188_CLKGATE_UART0_FRAC_SRC,

	RK3188_CLKGATE_PCLK_UART0 = RK3188_CLK_GATEID(8),
	

	RK3188_CLKGATE_CLK_CORE_DBG = RK3188_CLK_GATEID(9),

	RK3188_CLKGATE_MAX= RK3188_CLK_GATEID(10),
};
/*******************************gpio define **********************************************/
#define GPIO_INTEN			0x30
#define GPIO_INTMASK		0x34
#define GPIO_INTTYPE_LEVEL	0x38
#define GPIO_INT_POLARITY	0x3c
#define GPIO_INT_STATUS		0x40

/*******************************common code  for rkxxx*********************************/

static void  inline uart_printch(char byte)
{
        u32 reg_save[2];
        u32 u_clk_id=(RK3188_CLKGATE_UART0_SRC+CONFIG_RK_DEBUG_UART);
        u32 u_pclk_id=(RK3188_CLKGATE_PCLK_UART0+CONFIG_RK_DEBUG_UART);
        
        reg_save[0]=cru_readl(RK3188_CRU_GATEID_CONS(u_clk_id));
        reg_save[1]=cru_readl(RK3188_CRU_GATEID_CONS(u_pclk_id));
        RK3188_CLK_UNGATING_OPS(u_clk_id);
        RK3188_CLK_UNGATING_OPS(u_pclk_id);
        
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

         cru_writel(reg_save[0]|0x1<<((u_pclk_id%16)+16),RK3188_CRU_GATEID_CONS(u_clk_id));         
         cru_writel(reg_save[1]|0x1<<((u_pclk_id%16)+16),RK3188_CRU_GATEID_CONS(u_pclk_id));
}

void PIE_FUNC(sram_printch)(char byte)
{
	uart_printch(byte);
}

static void  ddr_printch(char byte)
{
	uart_printch(byte);
}
/*******************************clk gating config*******************************************/
#define CLK_MSK_GATING(msk, con) cru_writel((msk << 16) | 0xffff, con)
#define CLK_MSK_UNGATING(msk, con) cru_writel(((~msk) << 16) | 0xffff, con)


static u32 clk_ungt_msk[RK3188_CRU_CLKGATES_CON_CNT];// first clk gating setting
static u32 clk_ungt_save[RK3188_CRU_CLKGATES_CON_CNT]; //first clk gating value saveing


u32 DEFINE_PIE_DATA(rkpm_clkgt_last_set[RK3188_CRU_CLKGATES_CON_CNT]);
static u32 *p_rkpm_clkgt_last_set;

u32 DEFINE_PIE_DATA(rkpm_clkgt_last_save[RK3188_CRU_CLKGATES_CON_CNT]);
static u32 *p_rkpm_clkgt_last_save;

void PIE_FUNC(gtclks_sram_suspend)(void)
{
    int i;
   // u32 u_clk_id=(RK3188_CLKGATE_UART0_SRC+CONFIG_RK_DEBUG_UART);
   // u32 u_pclk_id=(RK3188_CLKGATE_PCLK_UART0+CONFIG_RK_DEBUG_UART);

    for(i=0;i<RK3188_CRU_CLKGATES_CON_CNT;i++)
    {
        DATA(rkpm_clkgt_last_save[i])=cru_readl(RK3188_CRU_CLKGATES_CON(i));     
        CLK_MSK_UNGATING( DATA(rkpm_clkgt_last_set[i]), RK3188_CRU_CLKGATES_CON(i));      
        #if 0
        rkpm_sram_printch('\n');   
        rkpm_sram_printhex(DATA(rkpm_clkgt_last_save[i]));
        rkpm_sram_printch('-');   
        rkpm_sram_printhex(DATA(rkpm_clkgt_last_set[i]));
        rkpm_sram_printch('-');   
        rkpm_sram_printhex(cru_readl(RK3188_CRU_CLKGATES_CON(i)));
        if(i==(RK3188_CRU_CLKGATES_CON_CNT-1))         
        rkpm_sram_printch('\n');   
        #endif
    }
    
        //RK3188_CLK_UNGATING_OPS(u_clk_id);
        //RK3188_CLK_UNGATING_OPS(u_pclk_id);
 
}

void PIE_FUNC(gtclks_sram_resume)(void)
{
    int i;
    for(i=0;i<RK3188_CRU_CLKGATES_CON_CNT;i++)
    {
        cru_writel(DATA(rkpm_clkgt_last_save[i])|0xffff0000, RK3188_CRU_CLKGATES_CON(i));
    }
}

static void gtclks_suspend(void)
{
    int i;
    
    for(i=0;i<RK3188_CRU_CLKGATES_CON_CNT;i++)
    {
    
        clk_ungt_save[i]=cru_readl(RK3188_CRU_CLKGATES_CON(i));    
        //if(i!=4||i!=0)
        CLK_MSK_UNGATING(clk_ungt_msk[i],RK3188_CRU_CLKGATES_CON(i));
       #if 0
        rkpm_ddr_printch('\n');   
        rkpm_ddr_printhex(clk_ungt_save[i]);
        rkpm_ddr_printch('-');   
        rkpm_ddr_printhex(clk_ungt_msk[i]);
        rkpm_ddr_printch('-');   
        rkpm_ddr_printhex(cru_readl(RK3188_CRU_CLKGATES_CON(i))) ;  
        if(i==(RK3188_CRU_CLKGATES_CON_CNT-1))            
            rkpm_ddr_printch('\n');   
        #endif
    }

}

static void gtclks_resume(void)
{
    int i;
     for(i=0;i<RK3188_CRU_CLKGATES_CON_CNT;i++)
    {
       cru_writel(clk_ungt_save[i]|0xffff0000,RK3188_CRU_CLKGATES_CON(i));
    }
    
}

/********************************pll power down***************************************/

#define power_off_pll(id) \
	cru_writel(RK3188_PLL_PWR_DN_W_MSK | RK3188_PLL_PWR_DN, RK3188_PLL_CONS((id), 3))
#if 0

static void pm_pll_wait_lock(u32 pll_idx)
{
	u32 pll_state[4] = { 1, 0, 2, 3 };
	u32 bit = 0x20u << pll_state[pll_idx];
	u32 delay = pll_idx == RK3188_APLL_ID ? 600000U : 30000000U;
	dsb();
	dsb();
	dsb();
	dsb();
	dsb();
	dsb();
	while (delay > 0) {
		if (grf_readl(RK3188_GRF_SOC_STATUS0) & bit)
			break;
		delay--;
	}
	if (delay == 0) {
		//CRU_PRINTK_ERR("wait pll bit 0x%x time out!\n", bit); 
		rkpm_ddr_printch('p');
		rkpm_ddr_printch('l');
		rkpm_ddr_printch('l');
		rkpm_ddr_printhex(pll_idx);
		rkpm_ddr_printch('\n');
	}
}	
static void power_on_pll(u32 pll_id)
{
        cru_writel(RK3188_PLL_PWR_DN_W_MSK | RK3188_PLL_PWR_ON, RK3188_PLL_CONS((pll_id), 3));
        pm_pll_wait_lock((pll_id));
}
#endif
static u32 clk_sel0, clk_sel1, clk_sel10;
static u32 cpll_con3;
static u32 cru_mode_con;

static void plls_suspend(void)
{
    cru_mode_con = cru_readl(RK3188_CRU_MODE_CON);
    cru_writel(RK3188_PLL_MODE_SLOW(RK3188_CPLL_ID), RK3188_CRU_MODE_CON);

    cpll_con3 = cru_readl(RK3188_PLL_CONS(RK3188_CPLL_ID, 3));
   //power_off_pll(RK3188_CPLL_ID);
       

       //apll
       clk_sel0 = cru_readl(RK3188_CRU_CLKSELS_CON(0));
       clk_sel1 = cru_readl(RK3188_CRU_CLKSELS_CON(1));

       cru_writel(RK3188_PLL_MODE_SLOW(RK3188_APLL_ID), RK3188_CRU_MODE_CON);
       
       /* To make sure aclk_cpu select apll before div effect */
       cru_writel(RK3188_CPU_SEL_PLL_W_MSK | RK3188_CPU_SEL_APLL
                          | RK3188_CORE_SEL_PLL_W_MSK | RK3188_CORE_SEL_APLL
                          , RK3188_CRU_CLKSELS_CON(0));
       cru_writel(RK3188_CORE_PERIPH_W_MSK | RK3188_CORE_PERIPH_2
              | RK3188_CORE_CLK_DIV_W_MSK | RK3188_CORE_CLK_DIV(1)
              | RK3188_CPU_CLK_DIV_W_MSK | RK3188_CPU_CLK_DIV(1)
              , RK3188_CRU_CLKSELS_CON(0));
       cru_writel(RK3188_CORE_ACLK_W_MSK | RK3188_CORE_ACLK_11
              | RK3188_ACLK_HCLK_W_MSK | RK3188_ACLK_HCLK_11
              | RK3188_ACLK_PCLK_W_MSK | RK3188_ACLK_PCLK_11
              | RK3188_AHB2APB_W_MSK | RK3188_AHB2APB_11
              , RK3188_CRU_CLKSELS_CON(1));
       //power_off_pll(RK3188_APLL_ID);
    cru_writel(RK3188_PLL_MODE_SLOW(RK3188_GPLL_ID), RK3188_CRU_MODE_CON);

       
    clk_sel10 = cru_readl(RK3188_CRU_CLKSELS_CON(10));
    cru_writel(RK3188_CRU_W_MSK_SETBITS(0, RK3188_PERI_ACLK_DIV_OFF, RK3188_PERI_ACLK_DIV_MASK)
    | RK3188_CRU_W_MSK_SETBITS(0,RK3188_PERI_HCLK_DIV_OFF, RK3188_PERI_HCLK_DIV_MASK)
    | RK3188_CRU_W_MSK_SETBITS(0, RK3188_PERI_PCLK_DIV_OFF, RK3188_PERI_PCLK_DIV_MASK)
    , RK3188_CRU_CLKSELS_CON(10));
    
  //power_off_pll(RK3188_GPLL_ID);

}

static void plls_resume(void)
{
    //gpll
       
        cru_writel(0xffff0000 | clk_sel10, RK3188_CRU_CLKSELS_CON(10));
    
      // power_on_pll(RK3188_GPLL_ID);
        cru_writel((RK3188_PLL_MODE_MSK(RK3188_GPLL_ID) << 16) 
                        | (RK3188_PLL_MODE_MSK(RK3188_GPLL_ID) & cru_mode_con)
                        ,  RK3188_CRU_MODE_CON);

        //apll
        cru_writel(0xffff0000 | clk_sel1, RK3188_CRU_CLKSELS_CON(1));
        /* To make sure aclk_cpu select gpll after div effect */
        cru_writel((0xffff0000 & ~RK3188_CPU_SEL_PLL_W_MSK & ~RK3188_CORE_SEL_PLL_W_MSK) 
                         | clk_sel0
                         , RK3188_CRU_CLKSELS_CON(0));
        
        cru_writel(RK3188_CPU_SEL_PLL_W_MSK 
                        | RK3188_CORE_SEL_PLL_W_MSK 
                        | clk_sel0
                        , RK3188_CRU_CLKSELS_CON(0));
        
     //   power_on_pll(RK3188_APLL_ID);
        cru_writel((RK3188_PLL_MODE_MSK(RK3188_APLL_ID) << 16)
                        | (RK3188_PLL_MODE_MSK(RK3188_APLL_ID) & cru_mode_con)
                        , RK3188_CRU_MODE_CON);

    
        // it was power off ,don't need to power up
        if (((cpll_con3 & RK3188_PLL_PWR_DN_MSK) == RK3188_PLL_PWR_ON) 
            &&((RK3188_PLL_MODE_NORM(RK3188_CPLL_ID) & RK3188_PLL_MODE_MSK(RK3188_CPLL_ID)) 
            == (cru_mode_con & RK3188_PLL_MODE_MSK(RK3188_CPLL_ID)))) {
       //     power_on_pll(RK3188_CPLL_ID);
        }
        cru_writel((RK3188_PLL_MODE_MSK(RK3188_CPLL_ID) << 16) 
                        | (RK3188_PLL_MODE_MSK(RK3188_CPLL_ID) & cru_mode_con)
                        , RK3188_CRU_MODE_CON);
}

u32  DEFINE_PIE_DATA(sysclk_cru_clksel0_con);
u32  DEFINE_PIE_DATA(sysclk_cru_clksel10_con);
u32  DEFINE_PIE_DATA(sysclk_cru_mode_con);

void PIE_FUNC(sysclk_suspend)(u32 sel_clk)
{
      DATA(sysclk_cru_clksel0_con) = cru_readl(RK3188_CRU_CLKSELS_CON(0));
      if(sel_clk&(RKPM_CTR_SYSCLK_32K))
        {
            DATA(sysclk_cru_mode_con) = cru_readl(RK3188_CRU_MODE_CON);
            DATA(sysclk_cru_clksel10_con) = cru_readl(RK3188_CRU_CLKSELS_CON(10));
            
            cru_writel(RK3188_PERI_ACLK_DIV_W_MSK | RK3188_PERI_ACLK_DIV(4), RK3188_CRU_CLKSELS_CON(10));
            cru_writel(RK3188_CORE_CLK_DIV_W_MSK | RK3188_CORE_CLK_DIV(4) 
                            | RK3188_CPU_CLK_DIV_W_MSK | RK3188_CPU_CLK_DIV(4)
                            , RK3188_CRU_CLKSELS_CON(0));
            
            cru_writel(0
                            | RK3188_PLL_MODE_DEEP(RK3188_APLL_ID)
                            //| RK3188_PLL_MODE_DEEP(RK3188_DPLL_ID)
                            | RK3188_PLL_MODE_DEEP(RK3188_CPLL_ID)
                            | RK3188_PLL_MODE_DEEP(RK3188_GPLL_ID)
                            , RK3188_CRU_MODE_CON);
            rkpm_sram_printch('8');
        }
        else if(sel_clk&(RKPM_CTR_SYSCLK_DIV))
        {
            //set core_clk_div and cpu_clk_div to the largest
            cru_writel(RK3188_CORE_CLK_DIV_W_MSK | RK3188_CORE_CLK_DIV_MSK
            		| RK3188_CPU_CLK_DIV_W_MSK | RK3188_CPU_CLK_DIV_MSK, RK3188_CRU_CLKSELS_CON(0));
        }
}

void PIE_FUNC(sysclk_resume)(u32 sel_clk)
{

    if(sel_clk&(RKPM_CTR_SYSCLK_32K))
    {
        cru_writel((0xffff<<16) | DATA(sysclk_cru_mode_con), RK3188_CRU_MODE_CON);
        cru_writel(RK3188_CORE_CLK_DIV_W_MSK | RK3188_CPU_CLK_DIV_W_MSK
        		| DATA(sysclk_cru_clksel0_con), RK3188_CRU_CLKSELS_CON(0));
        cru_writel(RK3188_PERI_ACLK_DIV_W_MSK | DATA(sysclk_cru_clksel10_con),
        		RK3188_CRU_CLKSELS_CON(10));
        
        rkpm_sram_printch('8');
    }
    else if(sel_clk&(RKPM_CTR_SYSCLK_DIV))
    {
        cru_writel(RK3188_CORE_CLK_DIV_W_MSK | RK3188_CPU_CLK_DIV_W_MSK
		        | DATA(sysclk_cru_clksel0_con), RK3188_CRU_CLKSELS_CON(0));
    }

}

static void clks_gating_suspend_init(void)
{
    // get clk gating info
    p_rkpm_clkgt_last_set= kern_to_pie(rockchip_pie_chunk, &DATA(rkpm_clkgt_last_set[0]));
    p_rkpm_clkgt_last_save= kern_to_pie(rockchip_pie_chunk, &DATA(rkpm_clkgt_last_save[0]));
    
    if(clk_suspend_clkgt_info_get(clk_ungt_msk,p_rkpm_clkgt_last_set, RK3188_CRU_CLKGATES_CON_CNT) 
        ==RK3188_CRU_CLKGATES_CON(0))
    {
        rkpm_set_ops_gtclks(gtclks_suspend,gtclks_resume);
        rkpm_set_sram_ops_gtclks(fn_to_pie(rockchip_pie_chunk, &FUNC(gtclks_sram_suspend)), 
                        fn_to_pie(rockchip_pie_chunk, &FUNC(gtclks_sram_resume)));
        
        PM_LOG("%s:clkgt info ok\n",__FUNCTION__);

    }
    rkpm_set_sram_ops_sysclk(fn_to_pie(rockchip_pie_chunk, &FUNC(sysclk_suspend))
                                                ,fn_to_pie(rockchip_pie_chunk, &FUNC(sysclk_resume))); 
}

/***************************prepare and finish reg_pread***********************************/

static noinline void rk30_pm_dump_irq(void)
{
#if 0
	u32 irq_gpio = (readl_relaxed(RK30_GICD_BASE + GIC_DIST_PENDING_SET + 8) >> 22) & 0x7F;
	printk("wakeup irq: %08x %08x %08x %08x\n",
		readl_relaxed(RK30_GICD_BASE + GIC_DIST_PENDING_SET + 4),
		readl_relaxed(RK30_GICD_BASE + GIC_DIST_PENDING_SET + 8),
		readl_relaxed(RK30_GICD_BASE + GIC_DIST_PENDING_SET + 12),
		readl_relaxed(RK30_GICD_BASE + GIC_DIST_PENDING_SET + 16));
	DUMP_GPIO_INT_STATUS(0);
	DUMP_GPIO_INT_STATUS(1);
	DUMP_GPIO_INT_STATUS(2);
	DUMP_GPIO_INT_STATUS(3);
    #if GPIO_BANKS > 4
	DUMP_GPIO_INT_STATUS(4);
    #endif
    #if GPIO_BANKS > 5
	DUMP_GPIO_INT_STATUS(6);
    #endif
    #endif
}


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
static noinline void rk30_pm_dump_inten(void)
{
	DUMP_GPIO_INTEN(0);
	DUMP_GPIO_INTEN(1);
	DUMP_GPIO_INTEN(2);
	DUMP_GPIO_INTEN(3);
}


static  void rkpm_prepare(void)
{   
    #if 1
        u32 temp =reg_readl(RK_GPIO_VIRT(0)+0x30);

       // rkpm_ddr_printhex(temp);
        reg_writel(temp|0x1<<4,RK_GPIO_VIRT(0)+0x30);
        temp =reg_readl(RK_GPIO_VIRT(0)+0x30);
       // rkpm_ddr_printhex(temp);
#endif
        
	// dump GPIO INTEN for debug
	rk30_pm_dump_inten();
        #ifdef CONFIG_DDR_TEST
        // memory tester
        //ddr_testmode();
        #endif
}

static void rkpm_finish(void)
{
	rk30_pm_dump_irq();
}


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

static u32 gpios_data[2];

static void __init  rk3188_suspend_init(void)
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

    if(of_property_read_u32_array(parent,"rockchip,pmic-gpios",gpios_data,ARRAY_SIZE(gpios_data)))
    {
            PM_ERR("%s:get pm ctr error\n",__FUNCTION__);
            return ;
    }
    rkpm_set_ctrbits(pm_ctrbits);
    clks_gating_suspend_init();
    rkpm_set_ops_plls(plls_suspend,plls_resume);
    rkpm_set_ops_prepare_finish(rkpm_prepare,rkpm_finish);
    rkpm_set_ops_regs_pread(interface_ctr_reg_pread); 
    
    //rkpm_set_sram_ops_ddr(fn_to_pie(rockchip_pie_chunk, &FUNC(ddr_suspend))
                                   //     ,fn_to_pie(rockchip_pie_chunk, &FUNC(ddr_resume)));
                                   
    rkpm_set_sram_ops_printch(fn_to_pie(rockchip_pie_chunk, &FUNC(sram_printch)));
    rkpm_set_ops_printch(ddr_printch); 	
    
}

