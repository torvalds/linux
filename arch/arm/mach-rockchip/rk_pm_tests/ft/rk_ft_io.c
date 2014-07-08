
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/iomap.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/rockchip/pmu.h>

#include "../../pm.h"

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


static void rk3288_pin_set_fun(u8 port,u8 bank,u8 b_gpio,u8 fun)
{ 
        u8 off_set;
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
static void pin_set_fun(u32 pins)
{
    rk3288_pin_set_fun(RKPM_PINBITS_PORT(pins),RKPM_PINBITS_BANK(pins),
                                                        RKPM_PINBITS_BGPIO(pins),RKPM_PINBITS_FUN(pins));
}

static void rk3288_gpio_set_in_output(u8 port,u8 bank,u8 b_gpio,u8 type)
{
    u32 val;    
    
    bank-=0xa;
    b_gpio=bank*8+b_gpio;//

    val=reg_readl(RK_GPIO_VIRT(port)+GPIO_SWPORT_DDR);

    if(type==RKPM_GPIO_OUTPUT)
        val|=(0x1<<b_gpio);
    else
        val&=~(0x1<<b_gpio);
    
    reg_writel(val,RK_GPIO_VIRT(port)+GPIO_SWPORT_DDR);
}
static void gpio_set_in_output(u32 pins,u8 type)
{
    rk3288_gpio_set_in_output(RKPM_PINBITS_PORT(pins),RKPM_PINBITS_BANK(pins),
                                                                        RKPM_PINBITS_BGPIO(pins),type);
}

static  void rk3288_gpio_set_output_level(u8 port,u8 bank,u8 b_gpio,u8 level)
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

static void gpio_set_output_level(u32 pins,u8 level)
{

        rk3288_gpio_set_output_level(RKPM_PINBITS_PORT(pins),RKPM_PINBITS_BANK(pins),
                                                                RKPM_PINBITS_BGPIO(pins),level);

}

static u8 rk3288_gpio_get_input_level(u8 port,u8 bank,u8 b_gpio)
{

    bank-=0xa;
    b_gpio=bank*8+b_gpio;

    return (reg_readl(RK_GPIO_VIRT(port)+GPIO_EXT_PORT)>>b_gpio)&0x1;
}
static u8 gpio_get_input_level(u32 pins)
{

        return rk3288_gpio_get_input_level(RKPM_PINBITS_PORT(pins),RKPM_PINBITS_BANK(pins),
                                                                RKPM_PINBITS_BGPIO(pins));
}


