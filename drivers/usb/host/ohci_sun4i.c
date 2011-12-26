/*
*************************************************************************************
*                         			      Linux
*					           USB Host Controller Driver
*
*				        (c) Copyright 2006-2010, All winners Co,Ld.
*							       All Rights Reserved
*
* File Name 	: ohci_sun4i.c
*
* Author 		: javen
*
* Description 	: OHCI Driver
*
* Notes         :
*
* History 		:
*      <author>    		<time>       	<version >    		<desc>
*    yangnaitian      2011-5-24            1.0          create this file
*    javen            2011-6-26            1.1          add suspend and resume
*    javen            2011-7-18            1.2          时钟开关和供电开关从驱动移出来
*
*************************************************************************************
*/

#include <linux/platform_device.h>
#include <linux/signal.h>

#include <linux/time.h>
#include <linux/timer.h>

#include <mach/sys_config.h>
#include <linux/clk.h>

#include  <mach/clock.h>
#include "sw_hci_sun4i.h"

/*.......................................................................................*/
//                               全局信息定义
/*.......................................................................................*/

//#define  SW_USB_OHCI_DEBUG

#define   SW_OHCI_NAME    "sw-ohci"
static const char ohci_name[]       = SW_OHCI_NAME;

static struct sw_hci_hcd *g_sw_ohci[3];
static u32 ohci_first_probe[3] = {1, 1, 1};

/*.......................................................................................*/
//                                      函数区
/*.......................................................................................*/

extern int usb_disabled(void);
int sw_usb_disable_ohci(__u32 usbc_no);
int sw_usb_enable_ohci(__u32 usbc_no);

/*
*******************************************************************************
*                     open_ohci_clock
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int open_ohci_clock(struct sw_hci_hcd *sw_ohci)
{
	return sw_ohci->open_clock(sw_ohci, 1);
}

/*
*******************************************************************************
*                     close_ohci_clock
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int close_ohci_clock(struct sw_hci_hcd *sw_ohci)
{
	return sw_ohci->close_clock(sw_ohci, 1);
}

/*
*******************************************************************************
*                     sw_get_io_resource
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_get_io_resource(struct platform_device *pdev, struct sw_hci_hcd *sw_ohci)
{
	return 0;
}

/*
*******************************************************************************
*                     sw_release_io_resource
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_release_io_resource(struct platform_device *pdev, struct sw_hci_hcd *sw_ohci)
{
	return 0;
}


/*
*******************************************************************************
*                     sw_start_ohc
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static void sw_start_ohc(struct sw_hci_hcd *sw_ohci)
{
  	open_ohci_clock(sw_ohci);

    sw_ohci->port_configure(sw_ohci, 1);
    sw_ohci->usb_passby(sw_ohci, 1);
    sw_ohci->set_power(sw_ohci, 1);

	return;
}

/*
*******************************************************************************
*                     sw_stop_ohc
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static void sw_stop_ohc(struct sw_hci_hcd *sw_ohci)
{
    sw_ohci->set_power(sw_ohci, 0);
    sw_ohci->usb_passby(sw_ohci, 0);
    sw_ohci->port_configure(sw_ohci, 0);

	close_ohci_clock(sw_ohci);

    return;
}


/*
*******************************************************************************
*                     sw_ohci_start
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int __devinit sw_ohci_start(struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci(hcd);
	int ret;

	if ((ret = ohci_init(ohci)) < 0)
		return ret;

	if ((ret = ohci_run(ohci)) < 0) {
		DMSG_PANIC("can't start %s", hcd->self.bus_name);
		ohci_stop(hcd);
		return ret;
	}

	return 0;
}

static const struct hc_driver sw_ohci_hc_driver ={
	.description        = hcd_name,
	.product_desc       = "SW USB2.0 'Open' Host Controller (OHCI) Driver",
	.hcd_priv_size      = sizeof(struct ohci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq                = ohci_irq,
	.flags              = HCD_USB11 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.start              = sw_ohci_start,
	.stop               = ohci_stop,
	.shutdown           = ohci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue        = ohci_urb_enqueue,
	.urb_dequeue        = ohci_urb_dequeue,
	.endpoint_disable   = ohci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number   = ohci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data    = ohci_hub_status_data,
	.hub_control        = ohci_hub_control,

#ifdef	CONFIG_PM
	.bus_suspend        = ohci_bus_suspend,
	.bus_resume         = ohci_bus_resume,
#endif
	.start_port_reset   = ohci_start_port_reset,
};

/*
*******************************************************************************
*                     sw_ohci_hcd_probe
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/

static int sw_ohci_hcd_probe(struct platform_device *pdev)
{
	int ret;
	struct usb_hcd *hcd = NULL;
	struct sw_hci_hcd *sw_ohci = NULL;

	if(pdev == NULL){
	    DMSG_PANIC("ERR: Argment is invaild\n");
	    return -1;
    }

    /* if usb is disabled, can not probe */
    if (usb_disabled()){
        DMSG_PANIC("ERR: usb hcd is disabled\n");
        return -ENODEV;
    }

	sw_ohci = pdev->dev.platform_data;
	if(!sw_ohci){
		DMSG_PANIC("ERR: sw_ohci is null\n");
		ret = -ENOMEM;
		goto ERR1;
	}

	sw_ohci->pdev = pdev;
	g_sw_ohci[sw_ohci->usbc_no] = sw_ohci;

	DMSG_INFO("[%s%d]: probe, pdev->name: %s, pdev->id: %d, sw_ohci: 0x%p\n",
		      ohci_name, sw_ohci->usbc_no, pdev->name, pdev->id, sw_ohci);

	/* get io resource */
	sw_get_io_resource(pdev, sw_ohci);
	sw_ohci->ohci_base 			= sw_ohci->usb_vbase + SW_USB_OHCI_BASE_OFFSET;
	sw_ohci->ohci_reg_length 	= SW_USB_OHCI_LEN;

    /*creat a usb_hcd for the ohci controller*/
	hcd = usb_create_hcd(&sw_ohci_hc_driver, &pdev->dev, ohci_name);
	if(!hcd){
        DMSG_PANIC("ERR: usb_ohci_create_hcd failed\n");
        ret = -ENOMEM;
		goto ERR2;
	}

  	hcd->rsrc_start = (u32)sw_ohci->ohci_base;
	hcd->rsrc_len 	= sw_ohci->ohci_reg_length;
	hcd->regs 		= sw_ohci->ohci_base;
	sw_ohci->hcd    = hcd;

	/* ochi start to work */
	sw_start_ohc(sw_ohci);

    ohci_hcd_init(hcd_to_ohci(hcd));

    ret = usb_add_hcd(hcd, sw_ohci->irq_no, IRQF_DISABLED | IRQF_SHARED);
    if(ret != 0){
        DMSG_PANIC("ERR: usb_add_hcd failed\n");
        ret = -ENOMEM;
        goto ERR3;
    }

    platform_set_drvdata(pdev, hcd);

#ifdef  SW_USB_OHCI_DEBUG
    DMSG_INFO("[%s]: probe, clock: 0x60(0x%x), 0xcc(0x%x); usb: 0x800(0x%x), dram:(0x%x, 0x%x)\n",
              sw_ohci->hci_name,
              (u32)USBC_Readl(sw_ohci->clock_vbase + 0x60),
              (u32)USBC_Readl(sw_ohci->clock_vbase + 0xcc),
              (u32)USBC_Readl(sw_ohci->usb_vbase + 0x800),
              (u32)USBC_Readl(sw_ohci->sdram_vbase + SW_SDRAM_REG_HPCR_USB1),
              (u32)USBC_Readl(sw_ohci->sdram_vbase + SW_SDRAM_REG_HPCR_USB2));
#endif

	sw_ohci->probe = 1;

    /* Disable ohci, when driver probe */
    if(sw_ohci->host_init_state == 0){
        if(ohci_first_probe[sw_ohci->usbc_no]){
            sw_usb_disable_ohci(sw_ohci->usbc_no);
            ohci_first_probe[sw_ohci->usbc_no]--;
        }
    }

    return 0;

ERR3:
	usb_put_hcd(hcd);

ERR2:
	sw_ohci->hcd = NULL;
	g_sw_ohci[sw_ohci->usbc_no] = NULL;

ERR1:

    return ret;
}

/*
*******************************************************************************
*                     sw_ohci_hcd_remove
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/

static int sw_ohci_hcd_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = NULL;
	struct sw_hci_hcd *sw_ohci = NULL;

	if(pdev == NULL){
		DMSG_PANIC("ERR: Argment is invalid\n");
		return -1;
	}

	hcd = platform_get_drvdata(pdev);
	if(hcd == NULL){
		DMSG_PANIC("ERR: hcd is null\n");
		return -1;
	}

	sw_ohci = pdev->dev.platform_data;
	if(sw_ohci == NULL){
		DMSG_PANIC("ERR: sw_ohci is null\n");
		return -1;
	}

	DMSG_INFO("[%s%d]: remove, pdev->name: %s, pdev->id: %d, sw_ohci: 0x%p\n",
		      ohci_name, sw_ohci->usbc_no, pdev->name, pdev->id, sw_ohci);

	usb_remove_hcd(hcd);

	sw_stop_ohc(sw_ohci);
	sw_ohci->probe = 0;

	usb_put_hcd(hcd);

	sw_release_io_resource(pdev, sw_ohci);

	sw_ohci->hcd = NULL;

    if(sw_ohci->host_init_state){
	    g_sw_ohci[sw_ohci->usbc_no] = NULL;
	}

	platform_set_drvdata(pdev, NULL);

	return 0;
}

/*
*******************************************************************************
*                     sw_ohci_hcd_shutdown
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void sw_ohci_hcd_shutdown(struct platform_device* pdev)
{
	struct sw_hci_hcd *sw_ohci = NULL;

	sw_ohci = pdev->dev.platform_data;
	if(sw_ohci == NULL){
		DMSG_PANIC("ERR: sw_ohci is null\n");
		return ;
	}

	if(sw_ohci->probe == 0){
    	DMSG_PANIC("ERR: sw_ohci is disable, need not shutdown\n");
    	return ;
	}

 	DMSG_INFO("[%s]: ohci shutdown start\n", sw_ohci->hci_name);

    usb_hcd_platform_shutdown(pdev);

    sw_stop_ohc(sw_ohci);

 	DMSG_INFO("[%s]: ohci shutdown end\n", sw_ohci->hci_name);

    return;
}

#ifdef CONFIG_PM

/*
*******************************************************************************
*                     sw_ohci_hcd_suspend
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static int sw_ohci_hcd_suspend(struct device *dev)
{
	struct sw_hci_hcd *sw_ohci  = NULL;
	struct usb_hcd *hcd         = NULL;
	struct ohci_hcd	*ohci       = NULL;
	unsigned long flags         = 0;
	int rc                      = 0;

	if(dev == NULL){
		DMSG_PANIC("ERR: Argment is invalid\n");
		return 0;
	}

	hcd = dev_get_drvdata(dev);
	if(hcd == NULL){
		DMSG_PANIC("ERR: hcd is null\n");
		return 0;
	}

	sw_ohci = dev->platform_data;
	if(sw_ohci == NULL){
		DMSG_PANIC("ERR: sw_ohci is null\n");
		return 0;
	}

	if(sw_ohci->probe == 0){
		DMSG_PANIC("ERR: sw_ohci is disable, can not suspend\n");
		return 0;
	}

	ohci = hcd_to_ohci(hcd);
	if(ohci == NULL){
		DMSG_PANIC("ERR: ohci is null\n");
		return 0;
	}

 	DMSG_INFO("[%s]: sw_ohci_hcd_suspend\n", sw_ohci->hci_name);

	/* Root hub was already suspended. Disable irq emission and
	 * mark HW unaccessible, bail out if RH has been resumed. Use
	 * the spinlock to properly synchronize with possible pending
	 * RH suspend or resume activity.
	 *
	 * This is still racy as hcd->state is manipulated outside of
	 * any locks =P But that will be a different fix.
	 */
	spin_lock_irqsave(&ohci->lock, flags);

    ohci_writel(ohci, OHCI_INTR_MIE, &ohci->regs->intrdisable);
    (void)ohci_readl(ohci, &ohci->regs->intrdisable);

    clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

    spin_unlock_irqrestore(&ohci->lock, flags);

    sw_stop_ohc(sw_ohci);

    return rc;
}

/*
*******************************************************************************
*                     sw_ohci_hcd_resume
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/

static int sw_ohci_hcd_resume(struct device *dev)
{
	struct sw_hci_hcd *sw_ohci = NULL;
	struct usb_hcd *hcd = NULL;

	if(dev == NULL){
		DMSG_PANIC("ERR: Argment is invalid\n");
		return 0;
	}

	hcd = dev_get_drvdata(dev);
	if(hcd == NULL){
		DMSG_PANIC("ERR: hcd is null\n");
		return 0;
	}

	sw_ohci = dev->platform_data;
	if(sw_ohci == NULL){
		DMSG_PANIC("ERR: sw_ohci is null\n");
		return 0;
	}

	if(sw_ohci->probe == 0){
		DMSG_PANIC("ERR: sw_ohci is disable, can not resume\n");
		return 0;
	}

 	DMSG_INFO("[%s]: sw_ohci_hcd_resume\n", sw_ohci->hci_name);

	sw_start_ohc(sw_ohci);

	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	ohci_finish_controller_resume(hcd);

    return 0;
}

static const struct dev_pm_ops sw_ohci_pmops = {
	.suspend	= sw_ohci_hcd_suspend,
	.resume		= sw_ohci_hcd_resume,
};

#define SW_OHCI_PMOPS  &sw_ohci_pmops

#else

#define SW_OHCI_PMOPS NULL

#endif

static struct platform_driver sw_ohci_hcd_driver = {
	.probe		= sw_ohci_hcd_probe,
	.remove		= sw_ohci_hcd_remove,
	.shutdown	= sw_ohci_hcd_shutdown,
	.driver		= {
		.name	= ohci_name,
		.owner	= THIS_MODULE,
		.pm	    = SW_OHCI_PMOPS,
	},
};

/*
*******************************************************************************
*                     sw_usb_disable_ohci
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
int sw_usb_disable_ohci(__u32 usbc_no)
{
	struct sw_hci_hcd *sw_ohci = NULL;

	if(usbc_no != 1 && usbc_no != 2){
		DMSG_PANIC("ERR:Argmen invalid. usbc_no(%d)\n", usbc_no);
		return -1;
	}

	sw_ohci = g_sw_ohci[usbc_no];
	if(sw_ohci == NULL){
		DMSG_PANIC("ERR: sw_ohci is null\n");
		return -1;
	}

	if(sw_ohci->host_init_state){
		DMSG_PANIC("ERR: not support sw_usb_disable_ohci\n");
		return -1;
	}

	if(sw_ohci->probe == 0){
		DMSG_PANIC("ERR: sw_ohci is disable, can not disable again\n");
		return -1;
	}

	sw_ohci->probe = 0;

	DMSG_INFO("[%s]: sw_usb_disable_ohci\n", sw_ohci->hci_name);

	sw_ohci_hcd_remove(sw_ohci->pdev);

	return 0;
}
EXPORT_SYMBOL(sw_usb_disable_ohci);

/*
*******************************************************************************
*                     sw_usb_enable_ohci
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
int sw_usb_enable_ohci(__u32 usbc_no)
{
	struct sw_hci_hcd *sw_ohci = NULL;

	if(usbc_no != 1 && usbc_no != 2){
		DMSG_PANIC("ERR:Argmen invalid. usbc_no(%d)\n", usbc_no);
		return -1;
	}

	sw_ohci = g_sw_ohci[usbc_no];
	if(sw_ohci == NULL){
		DMSG_PANIC("ERR: sw_ohci is null\n");
		return -1;
	}

	if(sw_ohci->host_init_state){
		DMSG_PANIC("ERR: not support sw_usb_enable_ohci\n");
		return -1;
	}

	if(sw_ohci->probe == 1){
		DMSG_PANIC("ERR: sw_ohci is already enable, can not enable again\n");
		return -1;
	}

	sw_ohci->probe = 1;

	DMSG_INFO("[%s]: sw_usb_enable_ohci\n", sw_ohci->hci_name);

	sw_ohci_hcd_probe(sw_ohci->pdev);

	return 0;
}
EXPORT_SYMBOL(sw_usb_enable_ohci);


