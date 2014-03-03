#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <mach/irqs.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/cru.h>
#include <mach/board.h>

#include "usbdev_rk.h"
#include "dwc_otg_regs.h"

#ifdef CONFIG_ARCH_RK319X
#include "usbdev_rk3190_grf_regs.h"

pGRF_UOC0_REG USBGRF_UOC0 = (pGRF_UOC0_REG)(GRF_UOC0_BASE);
pGRF_UOC1_REG USBGRF_UOC1 = (pGRF_UOC1_REG)(GRF_UOC1_BASE);
pGRF_HSIC_REG USBGRF_HSIC = (pGRF_HSIC_REG)(GRF_UOC2_BASE);
pGRF_USBPHY_REG GRF_USBPHY1 = (pGRF_USBPHY_REG)(GRF_USBPHY1_CON_BASE);
pGRF_SOC_STATUS USBGRF_STATUS = (pGRF_SOC_STATUS)(USBGRF_SOC_STATUS0);

int dwc_otg_check_dpdm(void)
{
/*
	static uint8_t * reg_base = 0;
    volatile unsigned int * otg_dctl;
    volatile unsigned int * otg_gotgctl;
    volatile unsigned int * otg_hprt0;
    int bus_status = 0;
    unsigned int * otg_phy_con0 = (unsigned int*)(USBGRF_UOC0_CON0) ;
    
    *(unsigned int*)(RK319X_CRU_BASE+0x120) = ((7<<5)<<16)|(7<<5);    // otg0 phy clkgate
    udelay(3);
    *(unsigned int*)(RK319X_CRU_BASE+0x120) = ((7<<5)<<16)|(0<<5);    // otg0 phy clkgate
    dsb();
    *(unsigned int*)(RK319X_CRU_BASE+0xd4) = ((1<<5)<<16);    // otg0 phy clkgate
    *(unsigned int*)(RK319X_CRU_BASE+0xe4) = ((1<<13)<<16);   // otg0 hclk clkgate
    *(unsigned int*)(RK319X_CRU_BASE+0xf4) = ((3<<10)<<16);    // hclk usb clkgat
   
    // exit phy suspend 
        *otg_phy_con0 = ((0x01<<0)<<16);  
    
    // soft connect
    if(reg_base == 0){
        reg_base = ioremap(RK319X_USBOTG20_PHYS, RK319X_USBOTG20_SIZE);
        if(!reg_base){
            bus_status = -1;
            goto out;
        }
    }
    mdelay(10);
    //printk("regbase %p 0x%x, otg_phy_con%p, 0x%x\n",
    //    reg_base, *(reg_base), otg_phy_con1, *otg_phy_con1);
    otg_dctl = (unsigned int * )(reg_base+0x804);
    otg_gotgctl = (unsigned int * )(reg_base);
    otg_hprt0 = (unsigned int * )(reg_base + DWC_OTG_HOST_PORT_REGS_OFFSET);
    if(*otg_gotgctl &(1<<19)){
        bus_status = 1;
        *otg_dctl &= ~(0x01<<1);//exit soft-disconnect mode
        mdelay(1);    // delay about 1ms
    // check dp,dm
        if((*otg_hprt0 & 0xc00)==0xc00)//check hprt[11:10] 
            bus_status = 2;
        //*(unsigned int*)(GRF_REG_BASE + GRF_UOC0_CON0) = 0x10001000;
    }
out:
    return bus_status;
    */
    return 0;
}

EXPORT_SYMBOL(dwc_otg_check_dpdm);

/***********************************
USB Port Type
0 : Disconnect
1 : SDP - pc
2 : DCP - charger
3 : CDP - pc with big currect charge
***********************************/
/* When do BC detect PCD pull-up register should be disabled  */

#define T_DCD_TIMEOUT	 (200)
#define T_BC_WAIT_CHGDET (40)
#define T_BC_SRC_OFF     (10)

int usb_battery_charger_detect(void)
{
	int port_type = 0;
	int timeout;
    
	//VBUS Valid detect
	if(USBGRF_STATUS->otg_vbusvalid){
	    //todo : add DCD !
		mdelay(T_DCD_TIMEOUT);//no DCD just wait for timeout
		/* Turn on VDPSRC */
        //Primary Detection
		USBGRF_UOC0->CON2 = 0x00e000e0;
		timeout = T_BC_WAIT_CHGDET;
		while(timeout--){
			if((USBGRF_STATUS->snps_chgdet))
				break;
			mdelay(1);
		}

		/* SDP and CDP/DCP distinguish */
        if(!(USBGRF_STATUS->snps_chgdet))
		{
		    port_type = 1;
		    /* Turn off VDPSRC */
		    USBGRF_UOC0->CON2 = 0x00e00000;
		    goto out;
		}

		/* Turn on VDMSRC */
		//Secondary Detection
        USBGRF_UOC0->CON2 = 0x00e000c0;
        timeout = T_BC_SRC_OFF;
		while(timeout--){
			if((USBGRF_UOC0->CON2 & 0x00e0) == 0x00c0)
				break;
			mdelay(1);
		}

        timeout = T_BC_WAIT_CHGDET;
		while(timeout--){
			if((USBGRF_STATUS->snps_chgdet))
				break;
			mdelay(1);
		}
		
		if((USBGRF_STATUS->snps_chgdet))
			port_type = 2;
		else
			port_type = 3;

        USBGRF_UOC0->CON2 = 0x00e00000;
        timeout = T_BC_SRC_OFF;
		while(timeout--){
			if((USBGRF_UOC0->CON2 & 0x00e0) == 0x00c0)
				break;
			mdelay(1);
		}
	}
out:
	printk("%s , battery_charger_detect %d\n", __func__, port_type);
	return port_type;
}
EXPORT_SYMBOL(usb_battery_charger_detect);


#ifdef CONFIG_USB20_OTG
/*DWC_OTG*/
static struct resource usb20_otg_resource[] = {
	{
		.start = IRQ_USB_OTG,
		.end   = IRQ_USB_OTG,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = RK319X_USBOTG20_PHYS,
		.end   = RK319X_USBOTG20_PHYS + RK319X_USBOTG20_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},

};

void usb20otg_hw_init(void)
{
#ifndef CONFIG_USB20_HOST
    // close USB 2.0 HOST phy and clock
    USBGRF_UOC1->CON1 = 0x007f0055;
#endif

    // usb phy config init

    // other hardware init
    iomux_set(OTG_DRV_VBUS);
}
void usb20otg_phy_suspend(void* pdata, int suspend)
{
    struct dwc_otg_platform_data *usbpdata=pdata;
    if(suspend){
        USBGRF_UOC0->CON3 = 0x003f002a;
        USBGRF_UOC0->CON2 = 0x00040004;
        
        usbpdata->phy_status = 1;
    }
    else{
        USBGRF_UOC0->CON2 = 0x00040000;
        usbpdata->phy_status = 0;
    }
}

void usb20otg_soft_reset(void)
{
    cru_set_soft_reset(SOFT_RST_USBOTG0, true);
    cru_set_soft_reset(SOFT_RST_USBPHY0, true);
    cru_set_soft_reset(SOFT_RST_OTGC0, true);
    udelay(5);

    cru_set_soft_reset(SOFT_RST_USBOTG0, false);
    cru_set_soft_reset(SOFT_RST_USBPHY0, false);
    cru_set_soft_reset(SOFT_RST_OTGC0, false);
    mdelay(2);
}

void usb20otg_clock_init(void* pdata)
{
    struct dwc_otg_platform_data *usbpdata=pdata;
    struct clk* ahbclk,*phyclk;
    ahbclk = clk_get(NULL, "hclk_otg0");
    phyclk = clk_get(NULL, "otgphy0_480m_vir");
	usbpdata->phyclk = phyclk;
	usbpdata->ahbclk = ahbclk;
}
void usb20otg_clock_enable(void* pdata, int enable)
{
    struct dwc_otg_platform_data *usbpdata=pdata;
    if(enable){
        clk_enable(usbpdata->ahbclk);
        clk_enable(usbpdata->phyclk);
    }
    else{
        clk_disable(usbpdata->phyclk);   
        clk_disable(usbpdata->ahbclk);
    }
}
int usb20otg_get_status(int id)
{
    int ret = -1;
    switch(id)
    {
        case USB_STATUS_BVABLID:
            // bvalid in grf
            ret = USBGRF_STATUS->otg_bvalid;
            break;
        case USB_STATUS_DPDM:
            // dpdm in grf
            ret = USBGRF_STATUS->otg_linestate;
            break;
        case USB_STATUS_ID:
            // id in grf
            ret = USBGRF_STATUS->otg_iddig;
            break;

        default:
            break;
    }
    return ret;
}
void dwc_otg_uart_mode(void* pdata, int enter_usb_uart_mode)
{
#ifdef CONFIG_RK_USB_UART
    //struct dwc_otg_platform_data *usbpdata=pdata;//1:uart 0:usb
    if(1 == enter_usb_uart_mode)   //uart mode
    {
        USBGRF_UOC0->CON0 = 0x03000300;
        printk("otg-phy enter uart mode USBGRF_UOC1_CON0 = 0x%08x\n",USBGRF_UOC0->CON0);
        
    }
    if(0 == enter_usb_uart_mode)   //usb mode
    {   
        USBGRF_UOC0->CON0 = 0x03000000;
        printk("otg-phy enter usb mode USBGRF_UOC1_CON0 = 0x%08x\n",USBGRF_UOC0->CON0);
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
        .start = RK319X_USBHOST20_PHYS,
        .end   = RK319X_USBHOST20_PHYS + RK319X_USBHOST20_SIZE - 1,
        .flags = IORESOURCE_MEM,
    },

};

void usb20host_hw_init(void)
{
    // usb phy config init

    //GRF_USBPHY1->CON7 = 0x78000000;//host_discon_con 575mv
    GRF_USBPHY1->CON0 = 0xffff8518;
    GRF_USBPHY1->CON1 = 0xffffe007;
    GRF_USBPHY1->CON2 = 0xffff82c9;
    GRF_USBPHY1->CON3 = 0xffff0202;
    GRF_USBPHY1->CON4 = 0xffff5556;
    GRF_USBPHY1->CON5 = 0xffff5555;
    GRF_USBPHY1->CON6 = 0xffff0005;
    GRF_USBPHY1->CON7 = 0xffff68d0;
    GRF_USBPHY1->CON8 = 0xffff0001;

    // other haredware init
    iomux_set(HOST_DRV_VBUS);
}
void usb20host_phy_suspend(void* pdata, int suspend)
{
    struct dwc_otg_platform_data *usbpdata=pdata;

    if(suspend){
        USBGRF_UOC1->CON1 = 0x007f0051;
        usbpdata->phy_status = 1;
    }
    else{
        USBGRF_UOC1->CON1 = 0x00010000;
        usbpdata->phy_status = 0;
    }
}
void usb20host_soft_reset(void)
{
    //phy reset
    USBGRF_UOC1->CON1 = 0x00030001;

    cru_set_soft_reset(SOFT_RST_USBPHY1_UTMI, true);

    udelay(15);

    USBGRF_UOC1->CON1 = 0x00030002;

    udelay(1500);
    cru_set_soft_reset(SOFT_RST_USBPHY1_UTMI, false);

    cru_set_soft_reset(SOFT_RST_USBOTG1, true);
    cru_set_soft_reset(SOFT_RST_USBPHY1, true);
    cru_set_soft_reset(SOFT_RST_OTGC1, true);
    udelay(5);

    cru_set_soft_reset(SOFT_RST_USBOTG1, false);
    cru_set_soft_reset(SOFT_RST_USBPHY1, false);
    cru_set_soft_reset(SOFT_RST_OTGC1, false);
    mdelay(2);

}

void usb20host_clock_init(void* pdata)
{
    struct dwc_otg_platform_data *usbpdata=pdata;
    struct clk* ahbclk,*phyclk;
    ahbclk = clk_get(NULL, "hclk_otg1");
    phyclk = clk_get(NULL, "otgphy1_480m_vir");
	usbpdata->phyclk = phyclk;
	usbpdata->ahbclk = ahbclk;
}

void usb20host_clock_enable(void* pdata, int enable)
{
    struct dwc_otg_platform_data *usbpdata=pdata;

    if(enable){
        clk_enable(usbpdata->ahbclk);
        clk_enable(usbpdata->phyclk);
    }
    else{
        clk_disable(usbpdata->ahbclk);
        //clk_disable(usbpdata->phyclk);
    }
}
int usb20host_get_status(int id)
{
    int ret = -1;
    pGRF_SOC_STATUS USBGRF_STATUS = (pGRF_SOC_STATUS)(USBGRF_SOC_STATUS0);
    switch(id)
    {
        case USB_STATUS_BVABLID:
            // bvalid in grf
            ret = USBGRF_STATUS->uhost_bvalid;
            break;
        case USB_STATUS_DPDM:
            // dpdm in grf
            ret = USBGRF_STATUS->uhost_linestate;
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
    ;
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
#ifdef CONFIG_USB_EHCI_RK
void rkehci_hw_init(void)
{
	// usb phy config init
	// hsic phy config init, set hsicphy_txsrtune
	USBGRF_HSIC->PHY_CON0 = 0x03c003c0;

	// other haredware init
	// set common_on, in suspend mode, otg/host PLL blocks remain powered
	USBGRF_UOC1->CON0 = 0x00400000;

	/* change INCR to INCR16 or INCR8(beats less than 16)
	 * or INCR4(beats less than 8) or SINGLE(beats less than 4)
	 */
	USBGRF_HSIC->CTRLER_CON0 = 0x00ff00bc;
}

void rkehci_clock_init(void* pdata)
{
	struct rkehci_platform_data *usbpdata=pdata;

	struct clk *clk_otg, *clk_hs;

	/* By default, hsicphy_480m's parent is otg phy 480MHz clk
	 * rk3188 must use host phy 480MHz clk
	 */
	clk_hs = clk_get(NULL, "hsicphy_480m");
	clk_otg = clk_get(NULL, "otgphy1_480m");
	clk_set_parent(clk_hs, clk_otg);

	usbpdata->hclk_hsic = clk_get(NULL, "hclk_hsic");
	usbpdata->hsic_phy_480m = clk_get(NULL, "hsicphy_480m");
	usbpdata->hsic_phy_12m = clk_get(NULL, "hsicphy_12m");
}

void rkehci_clock_enable(void* pdata, int enable)
{
	struct rkehci_platform_data *usbpdata=pdata;

	if(enable == usbpdata->clk_status)
		return;

	if(enable){
		clk_enable(usbpdata->hclk_hsic);
		clk_enable(usbpdata->hsic_phy_480m);
		clk_enable(usbpdata->hsic_phy_12m);
		usbpdata->clk_status = 1;
	}else{
		clk_disable(usbpdata->hsic_phy_12m);
		clk_disable(usbpdata->hsic_phy_480m);
		clk_disable(usbpdata->hclk_hsic);
		usbpdata->clk_status = 0;
	}
}

void rkehci_soft_reset(void)
{
    pGRF_HSIC_REG USBGRF_HSIC = (pGRF_HSIC_REG)(GRF_UOC2_BASE);

	cru_set_soft_reset(SOFT_RST_HSICPHY, true);
	udelay(12);
	cru_set_soft_reset(SOFT_RST_HSICPHY, false);
	mdelay(2);

    USBGRF_HSIC->PHY_CON0 = 0x04000400;
	udelay(2);
    USBGRF_HSIC->PHY_CON0 = 0x04000000;
	udelay(2);

	cru_set_soft_reset(SOFT_RST_HSIC_AHB, true);
	udelay(2);
	cru_set_soft_reset(SOFT_RST_HSIC_AHB, false);
	udelay(2);
}

struct rkehci_platform_data rkehci_pdata = {
	.hclk_hsic = NULL,
	.hsic_phy_12m = NULL,
	.hsic_phy_480m = NULL,
	.clk_status = -1,
	.hw_init = rkehci_hw_init,
	.clock_init = rkehci_clock_init,
	.clock_enable = rkehci_clock_enable,
	.soft_reset = rkehci_soft_reset,
};

static struct resource resources_hsusb_host[] = {
    {
        .start = IRQ_HSIC,
        .end   = IRQ_HSIC,
        .flags = IORESOURCE_IRQ,
    },
    {
        .start = RK30_HSIC_PHYS,
        .end   = RK30_HSIC_PHYS + RK30_HSIC_SIZE - 1,
        .flags = IORESOURCE_MEM,
    },
};

struct platform_device device_hsusb_host = {
    .name           = "rk_hsusb_host",
    .id             = -1,
    .num_resources  = ARRAY_SIZE(resources_hsusb_host),
    .resource       = resources_hsusb_host,
    .dev            = {
        .coherent_dma_mask      = 0xffffffff,
        .platform_data  = &rkehci_pdata,
    },
};
#endif
static int __init usbdev_init_devices(void)
{
    int ret = 0;
#ifdef CONFIG_USB20_OTG
	ret |= platform_device_register(&device_usb20_otg);
#endif
#ifdef CONFIG_USB20_HOST
	ret |= platform_device_register(&device_usb20_host);
#endif
#ifdef CONFIG_USB_EHCI_RK
    ret |= platform_device_register(&device_hsusb_host);
#endif
    if(ret < 0){
        printk("%s: platform_device_register(usb20_otg) failed\n", __func__);
    }
    return ret;
}
arch_initcall(usbdev_init_devices);

/*********************************************************************
                        rk3190 usb detect 
*********************************************************************/

#define WAKE_LOCK_TIMEOUT (HZ * 10)

static struct wake_lock usb_wakelock;
static struct delayed_work usb_det_wakeup_work;

inline void do_wakeup(void)
{
    wake_lock_timeout(&usb_wakelock, WAKE_LOCK_TIMEOUT);
	rk28_send_wakeup_key();
}


/*********** handler for bvalid irq ***********/

static irqreturn_t bvalid_irq_handler(int irq, void *dev_id)
{
    
#ifdef CONFIG_RK_USB_UART
    /* usb otg dp/dm switch to usb phy */
    dwc_otg_uart_mode(NULL, PHY_USB_MODE);
#endif
    
    schedule_delayed_work(&usb_det_wakeup_work, 0);

    /* clear irq pending */
    writel_relaxed(0x02000200, &USBGRF_UOC0->CON3);

    return IRQ_HANDLED;
}

/***** handler for otg id rise and fall edge *****/

static irqreturn_t id_irq_handler(int irq, void *dev_id)
{
    unsigned int uoc_con;
    
    /* clear irq */  
    uoc_con = USBGRF_UOC0->CON3;
    
    if(uoc_con & (1<<11))//id rise 
    {
        writel_relaxed(0x08000800, &USBGRF_UOC0->CON3);//clear id rise irq pandding
    }
    if(uoc_con & (1<<13))//id fall
    {
    
#ifdef CONFIG_RK_USB_UART
        /* usb otg dp/dm switch to usb phy */
        dwc_otg_uart_mode(NULL, PHY_USB_MODE);
#endif

        writel_relaxed(0x20002000, &USBGRF_UOC0->CON3);//clear id fall irq pandding
    }
    schedule_delayed_work(&usb_det_wakeup_work, 0);

    return IRQ_HANDLED;
}

/***** handler for otg line status change *****/

static int __init otg_irq_detect_init(void)
{
    int ret;
    int irq = IRQ_OTG_BVALID;

    wake_lock_init(&usb_wakelock, WAKE_LOCK_SUSPEND, "usb_detect");
    INIT_DELAYED_WORK(&usb_det_wakeup_work, do_wakeup);

    ret = request_irq(irq, bvalid_irq_handler, 0, "bvalid", NULL);
    if (ret < 0) {
        pr_err("%s: request_irq(%d) failed\n", __func__, irq);
        return ret;
    }
    
    /* clear & enable bvalid irq */
    writel_relaxed((3 << 8) | (3 << 24), &USBGRF_UOC0->CON3);

    irq = IRQ_OTG0_ID;

    ret = request_irq(irq, id_irq_handler, 0, "otg-id", NULL);
    if (ret < 0) {
        pr_err("%s: request_irq(%d) failed\n", __func__, irq);
        return ret;
    }

    /* clear & enable otg change irq */
    /* for rk3026 enable and clear id_fall_irq & id_rise_irq*/
    writel_relaxed((0xf << 26) | (0xf << 10), &USBGRF_UOC0->CON3);
    
    return 0;
}
late_initcall(otg_irq_detect_init);
#endif

