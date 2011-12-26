/*
*************************************************************************************
*                         			      Linux
*					           USB Host Controller Driver
*
*				        (c) Copyright 2006-2010, All winners Co,Ld.
*							       All Rights Reserved
*
* File Name 	: ehci_sun4i.c
*
* Author 		: javen
*
* Description 	: SoftWinner EHCI Driver
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
#include <linux/time.h>
#include <linux/timer.h>

#include <mach/sys_config.h>
#include <linux/clk.h>

#include  <mach/clock.h>
#include "sw_hci_sun4i.h"

/*.......................................................................................*/
//                               全局信息定义
/*.......................................................................................*/

//#define  SW_USB_EHCI_DEBUG

#define  SW_EHCI_NAME				"sw-ehci"
static const char ehci_name[] 		= SW_EHCI_NAME;

static struct sw_hci_hcd *g_sw_ehci[3];
static u32 ehci_first_probe[3] = {1, 1, 1};

/*.......................................................................................*/
//                                      函数区
/*.......................................................................................*/

extern int usb_disabled(void);
int sw_usb_disable_ehci(__u32 usbc_no);
int sw_usb_enable_ehci(__u32 usbc_no);

void print_ehci_info(struct sw_hci_hcd *sw_ehci)
{
    DMSG_INFO("----------print_ehci_info---------\n");
	DMSG_INFO("hci_name             = %s\n", sw_ehci->hci_name);
	DMSG_INFO("irq_no               = %d\n", sw_ehci->irq_no);
	DMSG_INFO("usbc_no              = %d\n", sw_ehci->usbc_no);

	DMSG_INFO("usb_vbase            = 0x%p\n", sw_ehci->usb_vbase);
	DMSG_INFO("sram_vbase           = 0x%p\n", sw_ehci->sram_vbase);
	DMSG_INFO("clock_vbase          = 0x%p\n", sw_ehci->clock_vbase);
	DMSG_INFO("sdram_vbase          = 0x%p\n", sw_ehci->sdram_vbase);

	DMSG_INFO("clock: AHB(0x%x), USB(0x%x)\n",
	          (u32)USBC_Readl(sw_ehci->clock_vbase + 0x60),
              (u32)USBC_Readl(sw_ehci->clock_vbase + 0xcc));

	DMSG_INFO("USB: 0x%x\n",(u32)USBC_Readl(sw_ehci->usb_vbase + SW_USB_PMU_IRQ_ENABLE));
	DMSG_INFO("DRAM: USB1(0x%x), USB2(0x%x)\n",
	          (u32)USBC_Readl(sw_ehci->sdram_vbase + SW_SDRAM_REG_HPCR_USB1),
	          (u32)USBC_Readl(sw_ehci->sdram_vbase + SW_SDRAM_REG_HPCR_USB2));

	DMSG_INFO("----------------------------------\n");
}

/*
*******************************************************************************
*                     sw_hcd_board_set_vbus
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
static void sw_hcd_board_set_vbus(struct sw_hci_hcd *sw_ehci, int is_on)
{
	sw_ehci->set_power(sw_ehci, is_on);

	return;
}

/*
*******************************************************************************
*                     open_ehci_clock
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
static int open_ehci_clock(struct sw_hci_hcd *sw_ehci)
{
	return sw_ehci->open_clock(sw_ehci, 0);
}

/*
*******************************************************************************
*                     close_ehci_clock
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
static int close_ehci_clock(struct sw_hci_hcd *sw_ehci)
{
	return sw_ehci->close_clock(sw_ehci, 0);
}

/*
*******************************************************************************
*                     sw_ehci_port_configure
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
static void sw_ehci_port_configure(struct sw_hci_hcd *sw_ehci, u32 enable)
{
	sw_ehci->port_configure(sw_ehci, enable);

	return;
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
static int sw_get_io_resource(struct platform_device *pdev, struct sw_hci_hcd *sw_ehci)
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
static int sw_release_io_resource(struct platform_device *pdev, struct sw_hci_hcd *sw_ehci)
{
	return 0;
}

/*
*******************************************************************************
*                     sw_start_ehci
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
static void sw_start_ehci(struct sw_hci_hcd *sw_ehci)
{
  	open_ehci_clock(sw_ehci);
	sw_ehci->usb_passby(sw_ehci, 1);
	sw_ehci_port_configure(sw_ehci, 1);
	sw_hcd_board_set_vbus(sw_ehci, 1);

	return;
}

/*
*******************************************************************************
*                     sw_stop_ehci
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
static void sw_stop_ehci(struct sw_hci_hcd *sw_ehci)
{
	sw_hcd_board_set_vbus(sw_ehci, 0);
	sw_ehci_port_configure(sw_ehci, 0);
	sw_ehci->usb_passby(sw_ehci, 0);
	close_ehci_clock(sw_ehci);

	return;
}

/*
*******************************************************************************
*                     sw_ehci_setup
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
static int sw_ehci_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int ret = ehci_init(hcd);

	ehci->need_io_watchdog = 0;

	return ret;
}

static const struct hc_driver sw_ehci_hc_driver = {
	.description			= hcd_name,
	.product_desc			= "SW USB2.0 'Enhanced' Host Controller (EHCI) Driver",
	.hcd_priv_size			= sizeof(struct ehci_hcd),

	 /*
	 * generic hardware linkage
	 */
	 .irq					=  ehci_irq,
	 .flags					=  HCD_MEMORY | HCD_USB2,

	/*
	 * basic lifecycle operations
	 *
	 * FIXME -- ehci_init() doesn't do enough here.
	 * See ehci-ppc-soc for a complete implementation.
	 */
	.reset					= sw_ehci_setup,
	.start					= ehci_run,
	.stop					= ehci_stop,
	.shutdown				= ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue			= ehci_urb_enqueue,
	.urb_dequeue			= ehci_urb_dequeue,
	.endpoint_disable		= ehci_endpoint_disable,
	.endpoint_reset			= ehci_endpoint_reset,

	/*
	 * scheduling support
	 */
	.get_frame_number		= ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data		= ehci_hub_status_data,
	.hub_control			= ehci_hub_control,
	.bus_suspend			= ehci_bus_suspend,
	.bus_resume				= ehci_bus_resume,
	.relinquish_port		= ehci_relinquish_port,
	.port_handed_over		= ehci_port_handed_over,

	.clear_tt_buffer_complete	= ehci_clear_tt_buffer_complete,
};

/*
*******************************************************************************
*                     sw_ehci_hcd_probe
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
static int sw_ehci_hcd_probe(struct platform_device *pdev)
{
	struct usb_hcd 	*hcd 	= NULL;
	struct ehci_hcd *ehci	= NULL;
	struct sw_hci_hcd *sw_ehci = NULL;
	int ret = 0;

	if(pdev == NULL){
		DMSG_PANIC("ERR: Argment is invaild\n");
		return -1;
	}

	/* if usb is disabled, can not probe */
	if (usb_disabled()) {
		DMSG_PANIC("ERR: usb hcd is disabled\n");
		return -ENODEV;
	}

	sw_ehci = pdev->dev.platform_data;
	if(!sw_ehci){
		DMSG_PANIC("ERR: sw_ehci is null\n");
		ret = -ENOMEM;
		goto ERR1;
	}

	sw_ehci->pdev = pdev;
	g_sw_ehci[sw_ehci->usbc_no] = sw_ehci;

	DMSG_INFO("[%s%d]: probe, pdev->name: %s, pdev->id: %d, sw_ehci: 0x%p\n",
		      ehci_name, sw_ehci->usbc_no, pdev->name, pdev->id, sw_ehci);

	/* get io resource */
	sw_get_io_resource(pdev, sw_ehci);
	sw_ehci->ehci_base 			= sw_ehci->usb_vbase + SW_USB_EHCI_BASE_OFFSET;
	sw_ehci->ehci_reg_length 	= SW_USB_EHCI_LEN;

	/* creat a usb_hcd for the ehci controller */
	hcd = usb_create_hcd(&sw_ehci_hc_driver, &pdev->dev, ehci_name);
	if (!hcd){
		DMSG_PANIC("ERR: usb_create_hcd failed\n");
		ret = -ENOMEM;
		goto ERR2;
	}

  	hcd->rsrc_start = (u32)sw_ehci->ehci_base;
	hcd->rsrc_len 	= sw_ehci->ehci_reg_length;
	hcd->regs 		= sw_ehci->ehci_base;
	sw_ehci->hcd    = hcd;

	/* echi start to work */
	sw_start_ehci(sw_ehci);

	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs;
	ehci->regs = hcd->regs + HC_LENGTH(ehci, readl(&ehci->caps->hc_capbase));

	/* cache this readonly data, minimize chip reads */
	ehci->hcs_params = readl(&ehci->caps->hcs_params);

	ret = usb_add_hcd(hcd, sw_ehci->irq_no, IRQF_DISABLED | IRQF_SHARED);
	if (ret != 0) {
		DMSG_PANIC("ERR: usb_add_hcd failed\n");
		ret = -ENOMEM;
		goto ERR3;
	}

	platform_set_drvdata(pdev, hcd);

#ifdef  SW_USB_EHCI_DEBUG
	DMSG_INFO("[%s]: probe, clock: 0x60(0x%x), 0xcc(0x%x); usb: 0x800(0x%x), dram:(0x%x, 0x%x)\n",
		      sw_ehci->hci_name,
		      (u32)USBC_Readl(sw_ehci->clock_vbase + 0x60),
		      (u32)USBC_Readl(sw_ehci->clock_vbase + 0xcc),
		      (u32)USBC_Readl(sw_ehci->usb_vbase + 0x800),
		      (u32)USBC_Readl(sw_ehci->sdram_vbase + SW_SDRAM_REG_HPCR_USB1),
		      (u32)USBC_Readl(sw_ehci->sdram_vbase + SW_SDRAM_REG_HPCR_USB2));
#endif

    sw_ehci->probe = 1;

    /* Disable ehci, when driver probe */
    if(sw_ehci->host_init_state == 0){
        if(ehci_first_probe[sw_ehci->usbc_no]){
            sw_usb_disable_ehci(sw_ehci->usbc_no);
            ehci_first_probe[sw_ehci->usbc_no]--;
        }
    }

	return 0;

ERR3:
    usb_put_hcd(hcd);

ERR2:
	sw_ehci->hcd = NULL;
	g_sw_ehci[sw_ehci->usbc_no] = NULL;

ERR1:

	return ret;
}

/*
*******************************************************************************
*                     sw_ehci_hcd_remove
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
static int sw_ehci_hcd_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = NULL;
	struct sw_hci_hcd *sw_ehci = NULL;

	if(pdev == NULL){
		DMSG_PANIC("ERR: Argment is invalid\n");
		return -1;
	}

	hcd = platform_get_drvdata(pdev);
	if(hcd == NULL){
		DMSG_PANIC("ERR: hcd is null\n");
		return -1;
	}

	sw_ehci = pdev->dev.platform_data;
	if(sw_ehci == NULL){
		DMSG_PANIC("ERR: sw_ehci is null\n");
		return -1;
	}

	DMSG_INFO("[%s%d]: remove, pdev->name: %s, pdev->id: %d, sw_ehci: 0x%p\n",
		      ehci_name, sw_ehci->usbc_no, pdev->name, pdev->id, sw_ehci);

	usb_remove_hcd(hcd);

	sw_release_io_resource(pdev, sw_ehci);

	usb_put_hcd(hcd);

	sw_stop_ehci(sw_ehci);
    sw_ehci->probe = 0;

	sw_ehci->hcd = NULL;

    if(sw_ehci->host_init_state){
    	g_sw_ehci[sw_ehci->usbc_no] = NULL;
    }

	platform_set_drvdata(pdev, NULL);

	return 0;
}

/*
*******************************************************************************
*                     sw_ehci_hcd_shutdown
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
void sw_ehci_hcd_shutdown(struct platform_device* pdev)
{
	struct sw_hci_hcd *sw_ehci = NULL;

	if(pdev == NULL){
		DMSG_PANIC("ERR: Argment is invalid\n");
		return;
	}

	sw_ehci = pdev->dev.platform_data;
	if(sw_ehci == NULL){
		DMSG_PANIC("ERR: sw_ehci is null\n");
		return;
	}

	if(sw_ehci->probe == 0){
		DMSG_PANIC("ERR: sw_ehci is disable, need not shutdown\n");
		return;
	}

 	DMSG_INFO("[%s]: ehci shutdown start\n", sw_ehci->hci_name);

    usb_hcd_platform_shutdown(pdev);

    sw_stop_ehci(sw_ehci);

 	DMSG_INFO("[%s]: ehci shutdown end\n", sw_ehci->hci_name);

    return ;
}

#ifdef CONFIG_PM

/*
*******************************************************************************
*                     sw_ehci_hcd_suspend
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
static int sw_ehci_hcd_suspend(struct device *dev)
{
	struct sw_hci_hcd *sw_ehci = NULL;
	struct usb_hcd *hcd = NULL;
	struct ehci_hcd *ehci = NULL;
	unsigned long flags = 0;

	if(dev == NULL){
		DMSG_PANIC("ERR: Argment is invalid\n");
		return 0;
	}

	hcd = dev_get_drvdata(dev);
	if(hcd == NULL){
		DMSG_PANIC("ERR: hcd is null\n");
		return 0;
	}

	sw_ehci = dev->platform_data;
	if(sw_ehci == NULL){
		DMSG_PANIC("ERR: sw_ehci is null\n");
		return 0;
	}

	if(sw_ehci->probe == 0){
		DMSG_PANIC("ERR: sw_ehci is disable, can not suspend\n");
		return 0;
	}

	ehci = hcd_to_ehci(hcd);
	if(ehci == NULL){
		DMSG_PANIC("ERR: ehci is null\n");
		return 0;
	}

 	DMSG_INFO("[%s]: sw_ehci_hcd_suspend\n", sw_ehci->hci_name);

	spin_lock_irqsave(&ehci->lock, flags);
	ehci_prepare_ports_for_controller_suspend(ehci, device_may_wakeup(dev));
	ehci_writel(ehci, 0, &ehci->regs->intr_enable);
	(void)ehci_readl(ehci, &ehci->regs->intr_enable);

	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	spin_unlock_irqrestore(&ehci->lock, flags);

	sw_stop_ehci(sw_ehci);

	return 0;
}

/*
*******************************************************************************
*                     sw_ehci_hcd_resume
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
static int sw_ehci_hcd_resume(struct device *dev)
{
	struct sw_hci_hcd *sw_ehci = NULL;
	struct usb_hcd *hcd = NULL;
	struct ehci_hcd *ehci = NULL;

	if(dev == NULL){
		DMSG_PANIC("ERR: Argment is invalid\n");
		return 0;
	}

	hcd = dev_get_drvdata(dev);
	if(hcd == NULL){
		DMSG_PANIC("ERR: hcd is null\n");
		return 0;
	}

	sw_ehci = dev->platform_data;
	if(sw_ehci == NULL){
		DMSG_PANIC("ERR: sw_ehci is null\n");
		return 0;
	}

	if(sw_ehci->probe == 0){
		DMSG_PANIC("ERR: sw_ehci is disable, can not resume\n");
		return 0;
	}

	ehci = hcd_to_ehci(hcd);
	if(ehci == NULL){
		DMSG_PANIC("ERR: ehci is null\n");
		return 0;
	}

 	DMSG_INFO("[%s]: sw_ehci_hcd_resume\n", sw_ehci->hci_name);

	sw_start_ehci(sw_ehci);

	/* Mark hardware accessible again as we are out of D3 state by now */
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	if (ehci_readl(ehci, &ehci->regs->configured_flag) == FLAG_CF) {
		int	mask = INTR_MASK;

		ehci_prepare_ports_for_controller_resume(ehci);

		if (!hcd->self.root_hub->do_remote_wakeup){
			mask &= ~STS_PCD;
		}

		ehci_writel(ehci, mask, &ehci->regs->intr_enable);
		ehci_readl(ehci, &ehci->regs->intr_enable);

		return 0;
	}

 	DMSG_INFO("[%s]: lost power, restarting\n", sw_ehci->hci_name);

	usb_root_hub_lost_power(hcd->self.root_hub);

	/* Else reset, to cope with power loss or flush-to-storage
	 * style "resume" having let BIOS kick in during reboot.
	 */
	(void) ehci_halt(ehci);
	(void) ehci_reset(ehci);

	/* emptying the schedule aborts any urbs */
	spin_lock_irq(&ehci->lock);
	if (ehci->reclaim)
		end_unlink_async(ehci);
	ehci_work(ehci);
	spin_unlock_irq(&ehci->lock);

	ehci_writel(ehci, ehci->command, &ehci->regs->command);
	ehci_writel(ehci, FLAG_CF, &ehci->regs->configured_flag);
	ehci_readl(ehci, &ehci->regs->command);	/* unblock posted writes */

	/* here we "know" root ports should always stay powered */
	ehci_port_power(ehci, 1);

	hcd->state = HC_STATE_SUSPENDED;

	return 0;

}

static const struct dev_pm_ops  aw_ehci_pmops = {
	.suspend	= sw_ehci_hcd_suspend,
	.resume		= sw_ehci_hcd_resume,
};

#define SW_EHCI_PMOPS 	&aw_ehci_pmops

#else

#define SW_EHCI_PMOPS 	NULL

#endif

static struct platform_driver sw_ehci_hcd_driver ={
  .probe  	= sw_ehci_hcd_probe,
  .remove	= sw_ehci_hcd_remove,
  .shutdown = sw_ehci_hcd_shutdown,
  .driver = {
		.name	= ehci_name,
		.owner	= THIS_MODULE,
		.pm		= SW_EHCI_PMOPS,
  	}
};

/*
*******************************************************************************
*                     sw_usb_disable_ehci
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
int sw_usb_disable_ehci(__u32 usbc_no)
{
	struct sw_hci_hcd *sw_ehci = NULL;

	if(usbc_no != 1 && usbc_no != 2){
		DMSG_PANIC("ERR:Argmen invalid. usbc_no(%d)\n", usbc_no);
		return -1;
	}

	sw_ehci = g_sw_ehci[usbc_no];
	if(sw_ehci == NULL){
		DMSG_PANIC("ERR: sw_ehci is null\n");
		return -1;
	}

	if(sw_ehci->host_init_state){
		DMSG_PANIC("ERR: not support sw_usb_disable_ehci\n");
		return -1;
	}

	if(sw_ehci->probe == 0){
		DMSG_PANIC("ERR: sw_ehci is disable, can not disable again\n");
		return -1;
	}

	sw_ehci->probe = 0;

	DMSG_INFO("[%s]: sw_usb_disable_ehci\n", sw_ehci->hci_name);

    sw_ehci_hcd_remove(sw_ehci->pdev);

	return 0;
}
EXPORT_SYMBOL(sw_usb_disable_ehci);

/*
*******************************************************************************
*                     sw_usb_enable_ehci
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
int sw_usb_enable_ehci(__u32 usbc_no)
{
	struct sw_hci_hcd *sw_ehci = NULL;

	if(usbc_no != 1 && usbc_no != 2){
		DMSG_PANIC("ERR:Argmen invalid. usbc_no(%d)\n", usbc_no);
		return -1;
	}

	sw_ehci = g_sw_ehci[usbc_no];
	if(sw_ehci == NULL){
		DMSG_PANIC("ERR: sw_ehci is null\n");
		return -1;
	}

	if(sw_ehci->host_init_state){
		DMSG_PANIC("ERR: not support sw_usb_enable_ehci\n");
		return -1;
	}

	if(sw_ehci->host_init_state){
		DMSG_PANIC("ERR: not support sw_usb_enable_ehci\n");
		return -1;
	}

	if(sw_ehci->probe == 1){
		DMSG_PANIC("ERR: sw_ehci is already enable, can not enable again\n");
		return -1;
	}

	sw_ehci->probe = 1;

	DMSG_INFO("[%s]: sw_usb_enable_ehci\n", sw_ehci->hci_name);

    sw_ehci_hcd_probe(sw_ehci->pdev);

	return 0;
}
EXPORT_SYMBOL(sw_usb_enable_ehci);


