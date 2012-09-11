#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>

#include <mach/irqs.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/cru.h>

#include "usbdev_rk.h"
#include "dwc_otg_regs.h"
#ifdef CONFIG_ARCH_RK2928

#define GRF_REG_BASE RK2928_GRF_BASE
#define USBOTG_SIZE    RK2928_USBOTG20_SIZE
#define USBGRF_SOC_STATUS0	(GRF_REG_BASE+0x14c)
#define USBGRF_UOC0_CON5	(GRF_REG_BASE+0x17c)
#define USBGRF_UOC1_CON4    (GRF_REG_BASE+0X190)
#define USBGRF_UOC1_CON5	(GRF_REG_BASE+0x194)

int dwc_otg_check_dpdm(void)
{
	static uint8_t * reg_base = 0;
    volatile unsigned int * otg_dctl;
    volatile unsigned int * otg_gotgctl;
    volatile unsigned int * otg_hprt0;
    int bus_status = 0;
    unsigned int * otg_phy_con1 = (unsigned int*)(USBGRF_UOC0_CON5) ;
    
    *(unsigned int*)(RK2928_CRU_BASE+0x120) = ((7<<5)<<16)|(7<<5);    // otg0 phy clkgate
    udelay(3);
    *(unsigned int*)(RK2928_CRU_BASE+0x120) = ((7<<5)<<16)|(0<<5);    // otg0 phy clkgate
    dsb();
    *(unsigned int*)(RK2928_CRU_BASE+0xd4) = ((1<<5)<<16);    // otg0 phy clkgate
    *(unsigned int*)(RK2928_CRU_BASE+0xe4) = ((1<<13)<<16);   // otg0 hclk clkgate
    *(unsigned int*)(RK2928_CRU_BASE+0xf4) = ((3<<10)<<16);    // hclk usb clkgat
   
    // exit phy suspend 
        *otg_phy_con1 = ((0x01<<0)<<16);  
    
    // soft connect
    if(reg_base == 0){
        reg_base = ioremap(RK2928_USBOTG20_PHYS,USBOTG_SIZE);
        if(!reg_base){
            bus_status = -1;
            goto out;
        }
    }
    mdelay(105);
    printk("regbase %p 0x%x, otg_phy_con%p, 0x%x\n",
        reg_base, *(reg_base), otg_phy_con1, *otg_phy_con1);
    otg_dctl = (unsigned int * )(reg_base+0x804);
    otg_gotgctl = (unsigned int * )(reg_base);
    otg_hprt0 = (unsigned int * )(reg_base + DWC_OTG_HOST_PORT_REGS_OFFSET);
    if(*otg_gotgctl &(1<<19)){
        bus_status = 1;
        *otg_dctl &= ~(0x01<<1);//@lyz exit soft-disconnect mode
        mdelay(50);    // delay about 10ms
    // check dp,dm
        if((*otg_hprt0 & 0xc00)==0xc00)//@lyz check hprt[11:10] 
            bus_status = 2;
    }
out:
    return bus_status;
}

EXPORT_SYMBOL(dwc_otg_check_dpdm);


#ifdef CONFIG_USB20_OTG
/*DWC_OTG*/
static struct resource usb20_otg_resource[] = {
	{
		.start = IRQ_USB_OTG,
		.end   = IRQ_USB_OTG,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = RK2928_USBOTG20_PHYS,
		.end   = RK2928_USBOTG20_PHYS + RK2928_USBOTG20_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},

};

void usb20otg_hw_init(void)
{
#ifndef CONFIG_USB20_HOST
    // close USB 2.0 HOST phy and clock
    unsigned int * otg_phy_con1 = (unsigned int*)(USBGRF_UOC1_CON5);
    *otg_phy_con1 = 0x1D5 |(0x1ff<<16);   // enter suspend.
#endif
    // usb phy config init

    // other hardware init
    rk30_mux_api_set(GPIO3C1_OTG_DRVVBUS_NAME, GPIO3C_OTG_DRVVBUS);    
}
void usb20otg_phy_suspend(void* pdata, int suspend)
{
    struct dwc_otg_platform_data *usbpdata=pdata;
    unsigned int * otg_phy_con1 = (unsigned int*)(USBGRF_UOC0_CON5);
    if(suspend){
        *otg_phy_con1 = 0x55 |(0x7f<<16);   // enter suspend.
        usbpdata->phy_status = 1;
    }
    else{
        *otg_phy_con1 = (0x01<<16);    // exit suspend.
        usbpdata->phy_status = 0;
    }
}
void usb20otg_soft_reset(void)
{
#if 1
    cru_set_soft_reset(SOFT_RST_USBOTG0, true);
    cru_set_soft_reset(SOFT_RST_USBPHY0, true);
    cru_set_soft_reset(SOFT_RST_OTGC0, true);
    udelay(1);

    cru_set_soft_reset(SOFT_RST_USBOTG0, false);
    cru_set_soft_reset(SOFT_RST_USBPHY0, false);
    cru_set_soft_reset(SOFT_RST_OTGC0, false);
    mdelay(1);
#endif
}
void usb20otg_clock_init(void* pdata)
{
    struct dwc_otg_platform_data *usbpdata=pdata;
    struct clk* ahbclk,*phyclk;
    ahbclk = clk_get(NULL, "hclk_otg0");
    phyclk = clk_get(NULL, "otgphy0");
	usbpdata->phyclk = phyclk;
	usbpdata->ahbclk = ahbclk;
}
void usb20otg_clock_enable(void* pdata, int enable)
{
    struct dwc_otg_platform_data *usbpdata=pdata;
    #if 1
    if(enable){
        clk_enable(usbpdata->ahbclk);
        clk_enable(usbpdata->phyclk);
    }
    else{
   // clk_disable(usbpdata->phyclk);   /* otg/host20 use the same phyclk, so can't disable phyclk in case host20 is used.*/ 
        clk_disable(usbpdata->ahbclk);    
    }
    #endif
}
int usb20otg_get_status(int id)
{
    int ret = -1;
    unsigned int usbgrf_status = *(unsigned int*)(USBGRF_SOC_STATUS0);
    unsigned int uoc1_con4 = *(unsigned int*)(USBGRF_UOC1_CON4);
    switch(id)
    {
        case USB_STATUS_BVABLID:
            // bvalid in grf
            ret = (usbgrf_status &(1<<7));
            break;
        case USB_STATUS_DPDM:
            // dpdm in grf
            ret = (usbgrf_status &(3<<8));
            break;
        case USB_STATUS_ID:
            // id in grf
            ret = (usbgrf_status &(1<<10));
            break;
        case USB_STATUS_UARTMODE:
            // usb_uart_mode in grf
            ret = (uoc1_con4 &(1<<13));
        default:
            break;
    }
    return ret;
}
void dwc_otg_uart_mode(void* pdata, int enter_usb_uart_mode)
{
#ifdef CONFIG_RK_USB_UART
    //struct dwc_otg_platform_data *usbpdata=pdata;//1:uart 0:usb
    unsigned int * otg_phy_con1 = (unsigned int*)(USBGRF_UOC1_CON4);
    //printk("usb_uart_mode = %d,enter_usb_uart_mode = %d\n",otg_phy_con1,enter_usb_uart_mode);
    if(1 == enter_usb_uart_mode)   //uart mode
    {
        *otg_phy_con1 = (0x03 << 12 | (0x03<<(16+12)));//bypass dm
        //printk("phy enter uart mode USBGRF_UOC1_CON4 = %08x\n",*otg_phy_con1);
        
    }
    if(0 == enter_usb_uart_mode)   //usb mode
    {   
        *otg_phy_con1 = (0x03<<(12+16)); //bypass dm disable 
        //printk("phy enter usb mode USBGRF_UOC1_CON4 = %8x\n",*otg_phy_con1);
    }
#endif
}

void usb20otg_power_enable(int enable)
{
}
struct dwc_otg_platform_data usb20otg_pdata = {
    .phyclk = NULL,
    .ahbclk = NULL,
    .busclk = NULL,
    .phy_status = 0,
    .hw_init=usb20otg_hw_init,
    .phy_suspend=usb20otg_phy_suspend,
    .soft_reset=usb20otg_soft_reset,
    .clock_init=usb20otg_clock_init,
    .clock_enable=usb20otg_clock_enable,
    .get_status=usb20otg_get_status,
    .dwc_otg_uart_mode=dwc_otg_uart_mode,
};

struct platform_device device_usb20_otg = {
	.name		  = "usb20_otg",
	.id		  = -1,
	.num_resources	  = ARRAY_SIZE(usb20_otg_resource),
	.resource	  = usb20_otg_resource,
	.dev		= {
		.platform_data	= &usb20otg_pdata,
	},
};
#endif
#ifdef CONFIG_USB20_HOST
static struct resource usb20_host_resource[] = {
    {
        .start = IRQ_USB_HOST,
        .end   = IRQ_USB_HOST,
        .flags = IORESOURCE_IRQ,
    },
    {
        .start = RK2928_USBHOST20_PHYS,
        .end   = RK2928_USBHOST20_PHYS + RK2928_USBHOST20_SIZE - 1,
        .flags = IORESOURCE_MEM,
    },

};

void usb20host_hw_init(void)
{
    // usb phy config init
    *(unsigned int *)(USBGRF_UOC0_CON5+4) = 0x07e00350;
    // other haredware init
    
}
void usb20host_phy_suspend(void* pdata, int suspend)
{
    struct dwc_otg_platform_data *usbpdata=pdata;
    unsigned int * otg_phy_con1 = (unsigned int*)(USBGRF_UOC1_CON5);
    if(suspend){
        *otg_phy_con1 = 0x1D5 |(0x1ff<<16);   // enter suspend.
        usbpdata->phy_status = 1;
    }
    else{
        *otg_phy_con1 = (0x01<<16);    // exit suspend.
        usbpdata->phy_status = 0;
    }
}
void usb20host_soft_reset(void)
{
#if 1
    cru_set_soft_reset(SOFT_RST_USBOTG1, true);
    cru_set_soft_reset(SOFT_RST_USBPHY1, true);
    cru_set_soft_reset(SOFT_RST_OTGC1, true);
    udelay(1);

    cru_set_soft_reset(SOFT_RST_USBOTG1, false);
    cru_set_soft_reset(SOFT_RST_USBPHY1, false);
    cru_set_soft_reset(SOFT_RST_OTGC1, false);
    mdelay(1);
#endif
}
void usb20host_clock_init(void* pdata)
{
    struct dwc_otg_platform_data *usbpdata=pdata;
    struct clk* ahbclk,*phyclk;
    ahbclk = clk_get(NULL, "hclk_otg1");
    phyclk = clk_get(NULL, "otgphy1");
	usbpdata->phyclk = phyclk;
	usbpdata->ahbclk = ahbclk;
}
void usb20host_clock_enable(void* pdata, int enable)
{
    struct dwc_otg_platform_data *usbpdata=pdata;
    #if 1
    if(enable){
        clk_enable(usbpdata->ahbclk);
        clk_enable(usbpdata->phyclk);
    }
    else{
        clk_disable(usbpdata->phyclk);
        clk_disable(usbpdata->ahbclk);
    }
    #endif
}
int usb20host_get_status(int id)
{
    int ret = -1;
    unsigned int usbgrf_status = *(unsigned int*)(USBGRF_SOC_STATUS0);
    switch(id)
    {
        case USB_STATUS_BVABLID:
            // bvalid in grf
            ret = (usbgrf_status &(1<<12));
            break;
        case USB_STATUS_DPDM:
            // dpdm in grf
            ret = (usbgrf_status &(3<<13));
            break;
        case USB_STATUS_ID:
            // id in grf
            ret = 0;
            break;
        default:
            break;
    }
    return ret;
}
void usb20host_power_enable(int enable)
{
}
struct dwc_otg_platform_data usb20host_pdata = {
    .phyclk = NULL,
    .ahbclk = NULL,
    .busclk = NULL,
    .phy_status = 0,
    .hw_init=usb20host_hw_init,
    .phy_suspend=usb20host_phy_suspend,
    .soft_reset=usb20host_soft_reset,
    .clock_init=usb20host_clock_init,
    .clock_enable=usb20host_clock_enable,
    .get_status=usb20host_get_status,
};

struct platform_device device_usb20_host = {
    .name             = "usb20_host",
    .id               = -1,
    .num_resources    = ARRAY_SIZE(usb20_host_resource),
    .resource         = usb20_host_resource,
    .dev		= {
		.platform_data	= &usb20host_pdata,
	},
};
#endif
static int __init usbdev_init_devices(void)
{
#ifdef CONFIG_USB20_OTG
	platform_device_register(&device_usb20_otg);
#endif
#ifdef CONFIG_USB20_HOST
	platform_device_register(&device_usb20_host);
#endif
}
arch_initcall(usbdev_init_devices);
#endif

