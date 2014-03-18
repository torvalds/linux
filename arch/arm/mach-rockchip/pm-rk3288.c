
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
//#include <linux/rockchip/cru.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/iomap.h>
#include "pm.h"

//#define CPU 3288
//#include "sram.h"
#include "pm-pie.c"


/*************************cru define********************************************/
#define RK3288_CRU_UNGATING_OPS(id) cru_writel(CRU_W_MSK_SETBITS(0,id%16,0x1),RK3288_CRU_GATEID_CONS(id))
#define RK3288_CRU_GATING_OPS(id) cru_writel(CRU_W_MSK_SETBITS(1,id%16,0x1),RK3288_CRU_GATEID_CONS(id))



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
        u32 u_clk_id=(RK3288_CLKGATE_UART0_SRC+CONFIG_RK_DEBUG_UART);
        u32 u_pclk_id=(RK3288_CLKGATE_PCLK_UART0+CONFIG_RK_DEBUG_UART);
        
        reg_save[0]=cru_readl(RK3288_CRU_GATEID_CONS(u_clk_id));
        reg_save[1]=cru_readl(RK3288_CRU_GATEID_CONS(u_pclk_id));
        RK3288_CRU_UNGATING_OPS(u_clk_id);
        RK3288_CRU_UNGATING_OPS(u_pclk_id);
        
        rkpm_udelay(1);
        
	writel_relaxed(byte, RK_DEBUG_UART_VIRT);
	dsb();

	/* loop check LSR[6], Transmitter Empty bit */
	while (!(readl_relaxed(RK_DEBUG_UART_VIRT + 0x14) & 0x40))
		barrier();
    
         cru_writel(reg_save[0]|CRU_W_MSK(u_clk_id%16,0x1),RK3288_CRU_GATEID_CONS(u_clk_id));         
         cru_writel(reg_save[1]|CRU_W_MSK(u_pclk_id%16,0x1),RK3288_CRU_GATEID_CONS(u_pclk_id));
        
	if (byte == '\n')
		uart_printch('\r');
}

void PIE_FUNC(sram_printch)(char byte)
{
	uart_printch(byte);
}

static void  ddr_printch(char byte)
{
	uart_printch(byte);
}
/*******************************gpio func*******************************************/
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

//pin=0x0a21  gpio0a2,port=0,bank=a,b_gpio=2,fun=1
static inline void pin_set_fun(u8 port,u8 bank,u8 b_gpio,u8 fun)
{ 
    u8 off_set;
    bank-=0xa;
    off_set=port*(4*4)+bank*4;
    
    if(off_set<RK3288_GRF_GPIO1D_IOMUX)
        return;   
    reg_writel(RKPM_W_MSK_SETBITS(fun,b_gpio*2,0x3),RK_GRF_VIRT+0+off_set);
}

static inline u8 pin_get_funset(u8 port,u8 bank,u8 b_gpio)
{ 
    u8 off_set;
    bank-=0xa;
    off_set=port*(4*4)+bank*4;
    
    if(off_set<RK3288_GRF_GPIO1D_IOMUX)
        return 0;   
    return (reg_readl(RK_GRF_VIRT+0+off_set)>>(b_gpio*2))&0x3;
}

static inline void pin_set_pull(u8 port,u8 bank,u8 b_gpio,u8 pull)
{ 
    u8 off_set;
    
    bank-=0xa;

    if(port > 0)
    {
        //gpio1_d st
        if(port==1&&bank<3)
         return;   
        //gpio1_d==0x14c ,form gpio0_a to gpio1_d offset 1*16+3*4= 0x1c
        off_set=0x14c-0x1c+port*(4*4)+bank*4;    
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
    u8 off_set;
    
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
    u8 val;    
    
    bank-=0xa;
    b_gpio=bank*8+b_gpio;//

    val=reg_readl(RK_GPIO_VIRT(port)+GPIO_SWPORT_DDR);

    if(type==RKPM_GPIO_OUTPUT)
        val|=(0x1<<b_gpio);
    else
        val&=~(0x1<<b_gpio);
    
    reg_writel(val,RK_GPIO_VIRT(port)+GPIO_SWPORT_DDR);
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
    u8 val;    

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

static u8 __sramfunc sram_gpio_get_input_level(u8 port,u8 bank,u8 b_gpio)
{
    return gpio_get_input_level(port,bank,b_gpio);
}
//ddr
static void ddr_pin_set_fun(u8 port,u8 bank,u8 b_gpio,u8 fun)
{ 
        pin_set_fun(port,bank,b_gpio,fun); 
}
static u8 ddr_pin_get_funset(u8 port,u8 bank,u8 b_gpio)
{ 
    return pin_get_funset(port,bank,b_gpio); 
}

static void ddr_pin_set_pull(u8 port,u8 bank,u8 b_gpio,u8 fun)
{ 
        pin_set_pull(port,bank,b_gpio,fun); 
}
static u8 ddr_pin_get_pullset(u8 port,u8 bank,u8 b_gpio)
{ 
    return pin_get_pullset(port,bank,b_gpio); 
}

static void ddr_gpio_set_in_output(u8 port,u8 bank,u8 b_gpio,u8 type)
{
    gpio_set_in_output(port,bank,b_gpio,type);
}

static u8 ddr_gpio_get_in_outputset(u8 port,u8 bank,u8 b_gpio)
{
    return gpio_get_in_outputset(port,bank,b_gpio);
}

static void ddr_gpio_set_output_level(u8 port,u8 bank,u8 b_gpio,u8 level)
{   
    gpio_set_output_level(port,bank,b_gpio,level);
}

static u8 ddr_gpio_get_output_levelset(u8 port,u8 bank,u8 b_gpio)
{
    return gpio_get_output_levelset(port,bank,b_gpio);
}

static u8 ddr_gpio_get_input_level(u8 port,u8 bank,u8 b_gpio)
{
    return gpio_get_input_level(port,bank,b_gpio);
}



static  void __sramfunc rkpm_pin_gpio_config_sram(u32 pin_gpio_bits,u32 *save_bits)
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
    
    //save pins info
    if(save_bits)
    {
        pins=RKPM_PINBITS_SET_FUN(pins,sram_pin_get_funset(port,bank,b_gpio));
       *save_bits=RKPM_PINGPIO_BITS(pins,sram_pin_get_pullset(port,bank,b_gpio),sram_gpio_get_in_outputset(port,bank,b_gpio),
                                                                                        sram_gpio_get_output_levelset(port,bank,b_gpio));
    }
    if(!fun&&(in_out==RKPM_GPIO_OUTPUT))
   {
        if(level==RKPM_GPIO_OUT_L)
            pull=RKPM_GPIO_PULL_DN;
        else
            pull=RKPM_GPIO_PULL_UP;
        
        sram_gpio_set_output_level(port,bank,b_gpio,level);
    }
        
    sram_pin_set_pull(port,bank,b_gpio,pull);                
    sram_pin_set_fun(port,bank,b_gpio,fun);
    
    if(!fun)
    {
        sram_gpio_set_in_output(port,bank,b_gpio,in_out);
    }      
    
}

static inline void rkpm_pin_gpio_config_ddr(u32 pin_gpio_bits,u32 *save_bits)
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
    
    //save pins info
    if(save_bits)
    {
        pins=RKPM_PINBITS_SET_FUN(pins,ddr_pin_get_funset(port,bank,b_gpio));
       *save_bits=RKPM_PINGPIO_BITS(pins,ddr_pin_get_pullset(port,bank,b_gpio),ddr_gpio_get_in_outputset(port,bank,b_gpio),
                                                                                        ddr_gpio_get_output_levelset(port,bank,b_gpio));
    }
    if(!fun&&(in_out==RKPM_GPIO_OUTPUT))
   {
        if(level==RKPM_GPIO_OUT_L)
            pull=RKPM_GPIO_PULL_DN;
        else
            pull=RKPM_GPIO_PULL_UP;
        
        ddr_gpio_set_output_level(port,bank,b_gpio,level);
    }
        
    ddr_pin_set_pull(port,bank,b_gpio,pull);                
    ddr_pin_set_fun(port,bank,b_gpio,fun);
    
    if(!fun)
    {
        ddr_gpio_set_in_output(port,bank,b_gpio,in_out);
    }      
    
}


#define GPIO_DTS_NUM 10

static  u32 gpio_dts_save[GPIO_DTS_NUM];
static  u32 gpio_dts[GPIO_DTS_NUM];

#define PMICGPIO_DTS_NUM 3


u32 DEFINE_PIE_DATA(pmicgpio_dts[PMICGPIO_DTS_NUM]);
static u32 *p_pmicgpio_dts;
static __sramdata u32 pmicgpio_dts_save[PMICGPIO_DTS_NUM];

static void __sramfunc pmic_gpio_suspend(void)
{
       int i;   
       for(i=0;;i++)
       {
            if(DATA(pmicgpio_dts[i]))
                rkpm_pin_gpio_config_sram(DATA(pmicgpio_dts[i]),& pmicgpio_dts_save[i]);
            else
            {
                    pmicgpio_dts_save[i]=0; 
                    break;
             }
       }
    #if 0       
         for(i=0;i<6;i++)
        {
            rkpm_sram_reg_dump(RK_GPIO_VIRT(i),0,0x4); 
        }
        //
        rkpm_sram_reg_dump(RK_GRF_VIRT,0xc,0x84); 
        rkpm_sram_reg_dump(RK_GRF_VIRT,0x14c,0x1b4);     
        rkpm_sram_reg_dump(RK_PMU_VIRT,0x64,0x6c);   
        rkpm_sram_reg_dump(RK_PMU_VIRT,0x84,0x9c); 
    #endif

}

static void  __sramfunc pmic_gpio_resume(void)
{
       int i;   
       for(i=0;;i++)
       {
            if(pmicgpio_dts_save[i])
                rkpm_pin_gpio_config_sram(pmicgpio_dts_save[i],NULL);     
       }

}

void PIE_FUNC(pmic_suspend)(void)
{
    pmic_gpio_suspend();

}

void PIE_FUNC(pmic_resume)(void)
{
    pmic_gpio_resume();
}


static void  rkpm_gpio_suspend(void)
{
       int i;   
       for(i=0;;i++)
       {
            if(DATA(pmicgpio_dts[i]))
                rkpm_pin_gpio_config_ddr(DATA(pmicgpio_dts[i]),& pmicgpio_dts_save[i]);
            else
            {
                    pmicgpio_dts_save[i]=0; 
                    break;
             }
       }
    #if 0       
         for(i=0;i<6;i++)
        {
            rkpm_ddr_reg_dump(RK_GPIO_VIRT(i),0,0x4); 
        }
        //
        rkpm_ddr_reg_dump(RK_GRF_VIRT,0xc,0x84); 
        rkpm_ddr_reg_dump(RK_GRF_VIRT,0x14c,0x1b4);     
        rkpm_ddr_reg_dump(RK_PMU_VIRT,0x64,0x6c);   
        rkpm_ddr_reg_dump(RK_PMU_VIRT,0x84,0x9c); 
    #endif

}

static void  rkpm_gpio_resume(void)
{
       int i;   
       for(i=0;;i++)
       {
            if(pmicgpio_dts_save[i])
                rkpm_pin_gpio_config_ddr(pmicgpio_dts_save[i],NULL);     
       }

}





static void gpio_get_dts_info(struct device_node *parent)
{
        int i;

        for(i=0;i<PMICGPIO_DTS_NUM;i++)
            p_pmicgpio_dts[i]=0;

        for(i=0;i<GPIO_DTS_NUM;i++)
            gpio_dts[i]=0;

        
        p_pmicgpio_dts= kern_to_pie(rockchip_pie_chunk, &DATA(pmicgpio_dts[0]));
        
       if(of_property_read_u32_array(parent,"rockchip,pmic-gpios",p_pmicgpio_dts,PMICGPIO_DTS_NUM))
       {
                p_pmicgpio_dts[0]=0;
               PM_ERR("%s:get pm ctr error\n",__FUNCTION__);
       }
       
       for(i=0;i<PMICGPIO_DTS_NUM;i++)
            printk("%s:pmic gpio(%x)\n",__FUNCTION__,p_pmicgpio_dts[i]);

        if(of_property_read_u32_array(parent,"rockchip,pm-gpios",gpio_dts,GPIO_DTS_NUM))
        {
                 gpio_dts[0]=0;
                PM_ERR("%s:get pm ctr error\n",__FUNCTION__);
        }
        for(i=0;i<GPIO_DTS_NUM;i++)
         printk("%s:pmic gpio(%x)\n",__FUNCTION__,gpio_dts[i]);

    rkpm_set_ops_gpios(rkpm_gpio_suspend,rkpm_gpio_resume);
    rkpm_set_sram_ops_gtclks(fn_to_pie(rockchip_pie_chunk, &FUNC(pmic_suspend)), 
                  fn_to_pie(rockchip_pie_chunk, &FUNC(pmic_resume)));

}







/*******************************clk gating config*******************************************/
#define CLK_MSK_GATING(msk, con) cru_writel((msk << 16) | 0xffff, con)
#define CLK_MSK_UNGATING(msk, con) cru_writel(((~msk) << 16) | 0xffff, con)


static u32 clk_ungt_msk[RK3288_CRU_CLKGATES_CON_CNT];// first clk gating setting
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

static void gtclks_suspend(void)
{
    int i;
    
    for(i=0;i<RK3288_CRU_CLKGATES_CON_CNT;i++)
    {
    
        clk_ungt_save[i]=cru_readl(RK3288_CRU_CLKGATES_CON(i));    
        //if(i!=4||i!=0)
        CLK_MSK_UNGATING(clk_ungt_msk[i],RK3288_CRU_CLKGATES_CON(i));
       #if 0
        rkpm_ddr_printch('\n');   
        rkpm_ddr_printhex(clk_ungt_save[i]);
        rkpm_ddr_printch('-');   
        rkpm_ddr_printhex(clk_ungt_msk[i]);
        rkpm_ddr_printch('-');   
        rkpm_ddr_printhex(cru_readl(RK3188_CRU_CLKGATES_CON(i))) ;  
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
    
}
/********************************pll power down***************************************/

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

static void pm_pll_wait_lock(u32 pll_idx)
{
	u32 delay = 600000U;
        u32 mode;
       mode=cru_readl(RK3288_CRU_MODE_CON);
	dsb();
	dsb();
	dsb();
	dsb();
	dsb();
	dsb();
	while (delay > 0) {
		if (cru_readl(RK3288_PLL_CONS(pll_idx,1))&(0x1<<31))
			break;
		delay--;
	}
	if (delay == 0) {
		rkpm_ddr_printascii("unlock-pll:");
		rkpm_ddr_printhex(pll_idx);
		rkpm_ddr_printch('\n');
	}
    cru_writel(mode|(RK3288_PLL_MODE_MSK(pll_idx)<<16), RK3288_CRU_MODE_CON);
}	

void pll_udelay(u32 udelay)
{
    u32 mode;
    mode=cru_readl(RK3288_CRU_MODE_CON);
    // delay in 24m
    cru_writel(RK3288_PLL_MODE_SLOW(APLL_ID), RK3288_CRU_MODE_CON);
    
    rkpm_udelay(udelay*5);
    
    cru_writel(mode|(RK3288_PLL_MODE_MSK(APLL_ID)<<16), RK3288_CRU_MODE_CON);
}

static u32 plls_con3_save[END_PLL_ID];
static u32 cru_mode_con;

static inline void plls_suspend(u32 pll_id)
{
    plls_con3_save[pll_id]=cru_readl(RK3288_PLL_CONS((pll_id), 3));
    cru_writel(RK3288_PLL_PWR_DN, RK3288_PLL_CONS((pll_id), 3));
    
}
static inline void plls_resume(u32 pll_id)
{
        u32 pllcon0, pllcon1, pllcon2;

        if(plls_con3_save[pll_id]||RK3288_PLL_PWR_DN_MSK)
            return ;    
        //enter slowmode
        cru_writel(RK3288_PLL_MODE_SLOW(pll_id), RK3288_CRU_MODE_CON);      
        
        cru_writel(RK3288_PLL_PWR_ON, RK3288_PLL_CONS((pll_id),3));
        cru_writel(RK3288_PLL_NO_BYPASS, RK3288_PLL_CONS((pll_id),3));
        
        pllcon0 = cru_readl(RK3288_PLL_CONS((pll_id),0));
        pllcon1 = cru_readl(RK3288_PLL_CONS((pll_id),1));
        pllcon2 = cru_readl(RK3288_PLL_CONS((pll_id),2));

        //enter rest
        cru_writel(RK3288_PLL_RESET, RK3288_PLL_CONS(pll_id,3));
        cru_writel(pllcon0, RK3288_PLL_CONS(pll_id,0));
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

static u32 clk_sel0,clk_sel1, clk_sel10,clk_sel26,clk_sel36, clk_sel37;

static void pm_plls_suspend(void)
{
    clk_sel0=cru_readl(RK3288_CRU_CLKSELS_CON(0));
    clk_sel1=cru_readl(RK3288_CRU_CLKSELS_CON(1));    
    clk_sel10=cru_readl(RK3288_CRU_CLKSELS_CON(10));
    clk_sel26=cru_readl(RK3288_CRU_CLKSELS_CON(26));
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
    cru_writel(CRU_W_MSK_SETBITS(1,15,0x1), RK3288_CRU_CLKSELS_CON(1));

    // pd_bus clk ,aclk ,hclk ,pclk, pd bus pll sel
    cru_writel(CRU_W_MSK_SETBITS(1,0,0x7)|CRU_W_MSK_SETBITS(1,3,0x1f)|CRU_W_MSK_SETBITS(1,8,0x3)|CRU_W_MSK_SETBITS(1,12,0x3)
                     , RK3288_CRU_CLKSELS_CON(1));
    //crypto for pd_bus
    cru_writel(CRU_W_MSK_SETBITS(3,6,0x3), RK3288_CRU_CLKSELS_CON(26));

    // peri aclk hclk pclk
    cru_writel(CRU_W_MSK_SETBITS(1,0,0x1f)|CRU_W_MSK_SETBITS(1,8,0x3)
                          |CRU_W_MSK_SETBITS(2,12,0x7), RK3288_CRU_CLKSELS_CON(10));

  
    plls_suspend(CPLL_ID);
    plls_suspend(GPLL_ID);

//apll 
   cru_writel(RK3288_PLL_MODE_SLOW(APLL_ID), RK3288_CRU_MODE_CON);
     // core_m0 core_mp a12_core
    cru_writel(CRU_W_MSK_SETBITS(1,0,0xf)|CRU_W_MSK_SETBITS(3,4,0xf)
                      |CRU_W_MSK_SETBITS(0,8,0x1f), RK3288_CRU_CLKSELS_CON(0));
    // core0 core1 core2 core3
    cru_writel(CRU_W_MSK_SETBITS(0,0,0x7)|CRU_W_MSK_SETBITS(0,4,0x7)
                      |CRU_W_MSK_SETBITS(0,8,0x7)|CRU_W_MSK_SETBITS(0,12,0x7)
                      , RK3288_CRU_CLKSELS_CON(36));
    // l2ram atclk pclk
    cru_writel((CRU_W_MSK_SETBITS(3,0,0x7)|CRU_W_MSK_SETBITS(0xf,4,0x1f)
                                |CRU_W_MSK_SETBITS(0xf,9,0x1f)), RK3288_CRU_CLKSELS_CON(37));
    plls_suspend(APLL_ID);

}

static void pm_plls_resume(void)
{
        plls_resume(APLL_ID);     
        // core_m0 core_mp a12_core
        cru_writel(clk_sel0|(CRU_W_MSK(0,0xf)|CRU_W_MSK(4,0xf)|CRU_W_MSK(8,0xf)),RK3288_CRU_CLKSELS_CON(0));
        // core0 core1 core2 core3
        cru_writel(clk_sel36|(CRU_W_MSK(0,0x7)|CRU_W_MSK(4,0x7)|CRU_W_MSK(8,0x7)|CRU_W_MSK(12,0x7))
                        , RK3288_CRU_CLKSELS_CON(36));
        // l2ram atclk pclk
        cru_writel(clk_sel37|(CRU_W_MSK(0,0x7)|CRU_W_MSK(4,0x1f)|CRU_W_MSK(9,0x1f)) , RK3288_CRU_CLKSELS_CON(37));
        cru_writel(cru_mode_con|(RK3288_PLL_MODE_MSK(APLL_ID)<<16), RK3288_CRU_MODE_CON);

        plls_resume(GPLL_ID);       
        plls_resume(CPLL_ID);       
        // peri aclk hclk pclk
        cru_writel(clk_sel10|(CRU_W_MSK(0,0x1f)|CRU_W_MSK(8,0x3)|CRU_W_MSK(12,0x7))
                                                                            , RK3288_CRU_CLKSELS_CON(10));
        // pd_bus aclk hclk pclk
        cru_writel(clk_sel1|(CRU_W_MSK(0,0x7)|CRU_W_MSK(3,0x1f)|CRU_W_MSK(8,0x3)|CRU_W_MSK(12,0x3))
                    , RK3288_CRU_CLKSELS_CON(1));
        // crypto
        cru_writel(clk_sel26|CRU_W_MSK(6,0x3), RK3288_CRU_CLKSELS_CON(26));

        cru_writel(clk_sel1|CRU_W_MSK(15,0x1), RK3288_CRU_CLKSELS_CON(1));

        cru_writel(cru_mode_con|(RK3288_PLL_MODE_MSK(GPLL_ID)<<16), RK3288_CRU_MODE_CON);
        cru_writel(cru_mode_con|(RK3288_PLL_MODE_MSK(CPLL_ID)<<16), RK3288_CRU_MODE_CON);

        plls_resume(NPLL_ID);       
        cru_writel(cru_mode_con|(RK3288_PLL_MODE_MSK(NPLL_ID)<<16), RK3288_CRU_MODE_CON);

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
    p_rkpm_clkgt_last_set= kern_to_pie(rockchip_pie_chunk, &DATA(rkpm_clkgt_last_set[0]));
    
    if(clk_suspend_clkgt_info_get(clk_ungt_msk,p_rkpm_clkgt_last_set, RK3288_CRU_CLKGATES_CON_CNT) 
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



#define GIC_DIST_PENDING_SET		0x200
#define DUMP_GPIO_INT_STATUS(ID) \
do { \
	if (irq_gpio & (1 << ID)) \
		printk("wakeup gpio" #ID ": %08x\n", readl_relaxed(RK_GPIO_VIRT(ID) + GPIO_INT_STATUS)); \
} while (0)
static noinline void rk30_pm_dump_irq(void)
{
	u32 irq_gpio = (readl_relaxed(RK_GIC_VIRT + GIC_DIST_PENDING_SET + 8) >> 22) & 0x7F;
	printk("wakeup irq: %08x %08x %08x %08x\n",
		readl_relaxed(RK_GIC_VIRT + GIC_DIST_PENDING_SET + 4),
		readl_relaxed(RK_GIC_VIRT + GIC_DIST_PENDING_SET + 8),
		readl_relaxed(RK_GIC_VIRT + GIC_DIST_PENDING_SET + 12),
		readl_relaxed(RK_GIC_VIRT + GIC_DIST_PENDING_SET + 16));
        DUMP_GPIO_INT_STATUS(0);
        DUMP_GPIO_INT_STATUS(1);
        DUMP_GPIO_INT_STATUS(2);
        DUMP_GPIO_INT_STATUS(3);
        DUMP_GPIO_INT_STATUS(4);
        DUMP_GPIO_INT_STATUS(5);
        DUMP_GPIO_INT_STATUS(6);
        DUMP_GPIO_INT_STATUS(7);
        DUMP_GPIO_INT_STATUS(8);
        
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

//dump while irq is enable
static noinline void rk30_pm_dump_inten(void)
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
        #if 0
        u32 temp =reg_readl(RK_GPIO_VIRT(0)+0x30);

       // rkpm_ddr_printhex(temp);
        reg_writel(temp|0x1<<4,RK_GPIO_VIRT(0)+0x30);
        temp =reg_readl(RK_GPIO_VIRT(0)+0x30);
       // rkpm_ddr_printhex(temp);
        #endif             
	// dump GPIO INTEN for debug
	//rk30_pm_dump_inten();
}

static void rkpm_finish(void)
{
	//rk30_pm_dump_irq();
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
#if 0
    if(of_property_read_u32_array(parent,"rockchip,pmic-gpios",gpios_data,ARRAY_SIZE(gpios_data)))
    {
            PM_ERR("%s:get pm ctr error\n",__FUNCTION__);
            return ;
    }
#endif    
    rkpm_set_ctrbits(pm_ctrbits);
    
    clks_gating_suspend_init();
    
   rkpm_set_ops_plls(pm_plls_suspend,pm_plls_resume);

    //rkpm_set_ops_prepare_finish(rkpm_prepare,rkpm_finish);
   // rkpm_set_ops_regs_pread(interface_ctr_reg_pread);                                    
    rkpm_set_sram_ops_printch(fn_to_pie(rockchip_pie_chunk, &FUNC(sram_printch)));
    rkpm_set_ops_printch(ddr_printch); 	
}
