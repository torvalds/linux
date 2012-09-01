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
#ifdef CONFIG_ARCH_RK30

#define GRF_REG_BASE	RK30_GRF_BASE	
#define USBOTG_SIZE	RK30_USBOTG20_SIZE
#ifdef CONFIG_ARCH_RK3066B
#define USBGRF_SOC_STATUS0	(GRF_REG_BASE+0xac)
#define USBGRF_UOC0_CON2	(GRF_REG_BASE+0x118) // USBGRF_UOC0_CON3
#define USBGRF_UOC1_CON2	(GRF_REG_BASE+0x128) // USBGRF_UOC1_CON3
#else
#define USBGRF_SOC_STATUS0	(GRF_REG_BASE+0x15c)
#define USBGRF_UOC0_CON2	(GRF_REG_BASE+0x184)
#define USBGRF_UOC1_CON2	(GRF_REG_BASE+0x190)
#endif
//#define USB_IOMUX_INIT(a,b) rk30_mux_api_set(a,b)

#ifdef CONFIG_USB20_OTG
/*DWC_OTG*/
static struct resource usb20_otg_resource[] = {
	{
		.start = IRQ_USB_OTG,
		.end   = IRQ_USB_OTG,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = RK30_USBOTG20_PHYS,
		.end   = RK30_USBOTG20_PHYS + RK30_USBOTG20_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},

};

void usb20otg_hw_init(void)
{
#ifndef CONFIG_USB20_HOST
    // close USB 2.0 HOST phy and clock
    unsigned int * otg_phy_con1 = (unsigned int*)(USBGRF_UOC1_CON2);
    *otg_phy_con1 = 0x554|(0xfff<<16);   // enter suspend.
#endif
    // usb phy config init

    // other haredware init
#ifdef CONFIG_ARCH_RK3066B
//  conflict with pwm2
//    rk30_mux_api_set(GPIO3D5_PWM2_JTAGTCK_OTGDRVVBUS_NAME, GPIO3D_OTGDRVVBUS);
#else
    rk30_mux_api_set(GPIO0A5_OTGDRVVBUS_NAME, GPIO0A_OTG_DRV_VBUS);
#endif
}
void usb20otg_phy_suspend(void* pdata, int suspend)
{
    struct dwc_otg_platform_data *usbpdata=pdata;
    unsigned int * otg_phy_con1 = (unsigned int*)(USBGRF_UOC0_CON2);
    if(suspend){
        *otg_phy_con1 = 0x554|(0xfff<<16);   // enter suspend.
        usbpdata->phy_status = 1;
    }
    else{
        *otg_phy_con1 = ((0x01<<2)<<16);    // exit suspend.
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
        clk_disable(usbpdata->phyclk);
        clk_disable(usbpdata->ahbclk);
    }
    #endif
}
int usb20otg_get_status(int id)
{
    int ret = -1;
    unsigned int usbgrf_status = *(unsigned int*)(USBGRF_SOC_STATUS0);
    switch(id)
    {
        case 0x01:
            // bvalid in grf
            ret = (usbgrf_status &0x20000);
            break;
        case 0x02:
            // dpdm in grf
            ret = (usbgrf_status &(3<<18));
            break;
        case 0x03:
            // id in grf
            ret = (usbgrf_status &(1<<20));
            break;
        default:
            break;
    }
    return ret;
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
        .start = RK30_USBHOST20_PHYS,
        .end   = RK30_USBHOST20_PHYS + RK30_USBHOST20_SIZE - 1,
        .flags = IORESOURCE_MEM,
    },

};
void usb20host_hw_init(void)
{
    // usb phy config init

    // other haredware init
#ifdef CONFIG_ARCH_RK3066B
    rk30_mux_api_set(GPIO3D6_PWM3_JTAGTMS_HOSTDRVVBUS_NAME, GPIO3D_HOSTDRVVBUS);
#else
    rk30_mux_api_set(GPIO0A6_HOSTDRVVBUS_NAME, GPIO0A_HOST_DRV_VBUS);
#endif
}
void usb20host_phy_suspend(void* pdata, int suspend)
{
    struct dwc_otg_platform_data *usbpdata=pdata;
    unsigned int * otg_phy_con1 = (unsigned int*)(USBGRF_UOC1_CON2);
    if(suspend){
        *otg_phy_con1 = 0x554|(0xfff<<16);   // enter suspend.
        usbpdata->phy_status = 0;
    }
    else{
        *otg_phy_con1 = ((0x01<<2)<<16);    // exit suspend.
        usbpdata->phy_status = 1;
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
            ret = (usbgrf_status &(1<<22));
            break;
        case USB_STATUS_DPDM:
            // dpdm in grf
            ret = (usbgrf_status &(3<<23));
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