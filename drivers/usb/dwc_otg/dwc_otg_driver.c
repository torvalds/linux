/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/otg_ipmate/linux/drivers/dwc_otg_driver.c $
 * $Revision: #12 $
 * $Date: 2007/02/07 $
 * $Change: 791271 $
 *
 * Synopsys HS OTG Linux Software Driver and documentation (hereinafter,
 * "Software") is an Unsupported proprietary work of Synopsys, Inc. unless
 * otherwise expressly agreed to in writing between Synopsys and you.
 * 
 * The Software IS NOT an item of Licensed Software or Licensed Product under
 * any End User Software License Agreement or Agreement for Licensed Product
 * with Synopsys or any supplement thereto. You are permitted to use and
 * redistribute this Software in source and binary forms, with or without
 * modification, provided that redistributions of source code must retain this
 * notice. You may not view, use, disclose, copy or distribute this file or
 * any information contained herein except pursuant to this license grant from
 * Synopsys. If you do not agree with this notice, including the disclaimer
 * below, then you are not authorized to use the Software.
 * 
 * THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS" BASIS
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * ========================================================================== */

/** @file
 * The dwc_otg_driver module provides the initialization and cleanup entry
 * points for the DWC_otg driver. This module will be dynamically installed
 * after Linux is booted using the insmod command. When the module is
 * installed, the dwc_otg_driver_init function is called. When the module is
 * removed (using rmmod), the dwc_otg_driver_cleanup function is called.
 * 
 * This module also defines a data structure for the dwc_otg_driver, which is
 * used in conjunction with the standard ARM lm_device structure. These
 * structures allow the OTG driver to comply with the standard Linux driver
 * model in which devices and drivers are registered with a bus driver. This
 * has the benefit that Linux can expose attributes of the driver and device
 * in its special sysfs file system. Users can then read or write files in
 * this file system to perform diagnostics on the driver components or the
 * device.
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/stat.h>	 /* permission constants */

#include <asm/io.h>
#include <asm/sizes.h>

#include "linux/dwc_otg_plat.h"
#include <linux/platform_device.h>
#include "dwc_otg_attr.h"
#include "dwc_otg_driver.h"
#include "dwc_otg_cil.h"
#include "dwc_otg_pcd.h"
#include "dwc_otg_hcd.h"
#ifdef CONFIG_ARCH_RK29
#include <mach/cru.h>
#endif
//#define DWC_DRIVER_VERSION	"2.60a 22-NOV-2006"
//#define DWC_DRIVER_VERSION	"2.70 2009-12-31"
#define DWC_DRIVER_VERSION	"3.00 2010-12-12 rockchip"

#define DWC_DRIVER_DESC		"HS OTG USB Controller driver"

static const char dwc_driver_name[] = "usb20_otg";

dwc_otg_device_t* g_otgdev = NULL;

/*-------------------------------------------------------------------------*/
/* Encapsulate the module parameter settings */

static dwc_otg_core_params_t dwc_otg_module_params = {
	.opt = -1,
	.otg_cap = -1,
	.dma_enable = -1,
	.dma_burst_size = -1,
	.speed = -1,
	.host_support_fs_ls_low_power = -1,
	.host_ls_low_power_phy_clk = -1,
	.enable_dynamic_fifo = -1,
	.data_fifo_size = -1,
	.dev_rx_fifo_size = -1,
	.dev_nperio_tx_fifo_size = -1,
	.dev_perio_tx_fifo_size = 
	{	/* dev_perio_tx_fifo_size_1 */
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1
	},	/* 15 */
	.host_rx_fifo_size = -1,
	.host_nperio_tx_fifo_size = -1,
	//.host_perio_tx_fifo_size = 512,
	.host_perio_tx_fifo_size = -1,
	.max_transfer_size = -1,
	.max_packet_count = -1,
	.host_channels = -1,
	.dev_endpoints = -1,
	.phy_type = -1,
	.phy_utmi_width = -1,
	.phy_ulpi_ddr = -1,
	.phy_ulpi_ext_vbus = -1,
	.i2c_enable = -1,
	.ulpi_fs_ls = -1,
	.ts_dline = -1,
	.en_multiple_tx_fifo = -1,
	.dev_tx_fifo_size = 
	{	/* dev_tx_fifo_size */
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1
	},	/* 15 */
	.thr_ctl = -1,
	.tx_thr_length = -1,
	.rx_thr_length = -1,
};

#ifdef CONFIG_USB11_HOST

dwc_otg_device_t* g_host11 = NULL;

static dwc_otg_core_params_t host11_module_params = {
	.opt = -1,
	.otg_cap = -1,
	.dma_enable = -1,
	.dma_burst_size = -1,
	.speed = -1,
	.host_support_fs_ls_low_power = 1,
	.host_ls_low_power_phy_clk = -1,
	.enable_dynamic_fifo = -1,
	.data_fifo_size = -1,
	.dev_rx_fifo_size = -1,
	.dev_nperio_tx_fifo_size = -1,
	.dev_perio_tx_fifo_size = 
	{	/* dev_perio_tx_fifo_size_1 */
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1
	},	/* 15 */
	.host_rx_fifo_size = -1,
	.host_nperio_tx_fifo_size = -1,
	//.host_perio_tx_fifo_size = 512,
	.host_perio_tx_fifo_size = -1,
	.max_transfer_size = -1,
	.max_packet_count = -1,
	.host_channels = -1,
	.dev_endpoints = -1,
	.phy_type = -1,
	.phy_utmi_width = -1,
	.phy_ulpi_ddr = -1,
	.phy_ulpi_ext_vbus = -1,
	.i2c_enable = -1,
	.ulpi_fs_ls = -1,
	.ts_dline = -1,
	.en_multiple_tx_fifo = -1,
	.dev_tx_fifo_size = 
	{	/* dev_tx_fifo_size */
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1
	},	/* 15 */
	.thr_ctl = -1,
	.tx_thr_length = -1,
	.rx_thr_length = -1,
};
#endif

#ifdef CONFIG_USB20_HOST
dwc_otg_device_t* g_host20 = NULL;

static dwc_otg_core_params_t host20_module_params = {
	.opt = -1,
	.otg_cap = -1,
	.dma_enable = -1,
	.dma_burst_size = -1,
	.speed = -1,
	.host_support_fs_ls_low_power = -1,
	.host_ls_low_power_phy_clk = -1,
	.enable_dynamic_fifo = -1,
	.data_fifo_size = -1,
	.dev_rx_fifo_size = -1,
	.dev_nperio_tx_fifo_size = -1,
	.dev_perio_tx_fifo_size = 
	{	/* dev_perio_tx_fifo_size_1 */
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1
	},	/* 15 */
	.host_rx_fifo_size = -1,
	.host_nperio_tx_fifo_size = -1,
	//.host_perio_tx_fifo_size = 512,
	.host_perio_tx_fifo_size = -1,
	.max_transfer_size = -1,
	.max_packet_count = -1,
	.host_channels = -1,
	.dev_endpoints = -1,
	.phy_type = -1,
	.phy_utmi_width = -1,
	.phy_ulpi_ddr = -1,
	.phy_ulpi_ext_vbus = -1,
	.i2c_enable = -1,
	.ulpi_fs_ls = -1,
	.ts_dline = -1,
	.en_multiple_tx_fifo = -1,
	.dev_tx_fifo_size = 
	{	/* dev_tx_fifo_size */
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1,
			-1
	},	/* 15 */
	.thr_ctl = -1,
	.tx_thr_length = -1,
	.rx_thr_length = -1,
};
#endif

/**
 * This function shows the Driver Version.
 */
static ssize_t version_show(struct device_driver *dev, char *buf)
{
	return snprintf(buf, sizeof(DWC_DRIVER_VERSION)+2,"%s\n", 
		DWC_DRIVER_VERSION);
}
static DRIVER_ATTR(version, S_IRUGO, version_show, NULL);

/**
 * Global Debug Level Mask.
 */
uint32_t g_dbg_lvl = DBG_OFF;//0xFFFF;//DBG_CIL|DBG_CILV|DBG_PCDV|DBG_HCDV|DBG_HCD|DBG_HCD_URB; /* OFF */

/**
 * This function shows the driver Debug Level.
 */
static ssize_t dbg_level_show(struct device_driver *_drv, char *_buf)
{
	return sprintf(_buf, "0x%0x\n", g_dbg_lvl);
}
/**
 * This function stores the driver Debug Level.
 */
static ssize_t dbg_level_store(struct device_driver *_drv, const char *_buf, 
							   size_t _count)
{
	g_dbg_lvl = simple_strtoul(_buf, NULL, 16);
		return _count;
}
static DRIVER_ATTR(debuglevel, S_IRUGO|S_IWUSR, dbg_level_show, dbg_level_store);
#ifdef CONFIG_USB

extern struct usb_hub *g_root_hub20;
#ifdef DWC_BOTH_HOST_SLAVE
extern void hcd_start( dwc_otg_core_if_t *_core_if );

extern int rk28_usb_suspend( int exitsuspend );
extern void hub_disconnect_device(struct usb_hub *hub);

static ssize_t force_usb_mode_show(struct device_driver *_drv, char *_buf)
{
    dwc_otg_device_t *otg_dev = g_otgdev;
    dwc_otg_core_if_t *core_if = otg_dev->core_if;
#if 1
    return sprintf (_buf, "%d\n", core_if->usb_mode);
#else
    dwc_otg_device_t *otg_dev = lm_get_drvdata(g_lmdev);
    dwc_otg_core_if_t *core_if = otg_dev->core_if;
    gotgctl_data_t    gctrl;
    gctrl.d32 = dwc_read_reg32( &core_if->core_global_regs->gotgctl );
    printk("OTGCTL=0x%08X\n", gctrl.d32);

    if(g_usb_mode == USB_NORMAL_MODE)
        return sprintf (_buf, "Current usb mode: Normal Mode\n");
    else if(g_usb_mode == FORCE_HOST_MODE)
        return sprintf (_buf, "Current usb mode: Force Host\n");
    else if(g_usb_mode == FORCE_DEVICE_MODE)
        return sprintf (_buf, "Current usb mode: Force Device\n");
    else
        return sprintf (_buf, "Current usb mode: Unknown\n");
#endif        
}

void dwc_otg_force_host(dwc_otg_core_if_t *core_if)
{
    dwc_otg_device_t *otg_dev = g_otgdev;
    dctl_data_t dctl = {.d32=0};
    if(core_if->op_state == A_HOST)
    {
    	printk("dwc_otg_force_host,already in A_HOST mode,everest\n");
    	return;
    }
	if((otg_dev->pcd)&&(otg_dev->pcd->phy_suspend == 1))
	{
		rk28_usb_suspend( 1 );
	}
    del_timer(&otg_dev->pcd->check_vbus_timer);
    // force disconnect 
    /* soft disconnect */
    dctl.d32 = dwc_read_reg32( &core_if->dev_if->dev_global_regs->dctl );
    dctl.b.sftdiscon = 1;
    dwc_write_reg32( &core_if->dev_if->dev_global_regs->dctl, dctl.d32 );
    
    if (core_if->pcd_cb && core_if->pcd_cb->stop ) {
            core_if->pcd_cb->stop( core_if->pcd_cb->p );
    }
    
    //core_if->op_state = A_HOST;
    /*
     * Initialize the Core for Host mode.
     */
	dwc_otg_core_init(core_if);
	dwc_otg_enable_global_interrupts(core_if);
    hcd_start( core_if );
    
}
void dwc_otg_force_device(dwc_otg_core_if_t *core_if)
{
    dwc_otg_device_t *otg_dev = g_otgdev;
    dwc_otg_disable_global_interrupts( core_if );
    if (core_if->hcd_cb && core_if->hcd_cb->stop) {
    	core_if->hcd_cb->stop( core_if->hcd_cb->p );
    }
    if(core_if->op_state == B_PERIPHERAL)
    {
    	printk("dwc_otg_force_device,already in B_PERIPHERAL,everest\n");
    	return;
    }
	hub_disconnect_device(g_root_hub20);
    otg_dev->core_if->op_state = B_PERIPHERAL;
	/* Reset the Controller */
	dwc_otg_core_reset( core_if );
    //otg_dev->pcd->phy_suspend = 1;
    otg_dev->pcd->vbus_status = 0;
    dwc_otg_pcd_start_vbus_timer( otg_dev->pcd );
	
}
static void dwc_otg_set_gusbcfg(dwc_otg_core_if_t *core_if, int mode)
{
	gusbcfg_data_t usbcfg = { .d32 = 0 };
	
    usbcfg.d32 = dwc_read_reg32( &core_if->core_global_regs->gusbcfg);
    switch(mode)
    {
    case USB_MODE_FORCE_HOST:
        usbcfg.b.force_hst_mode = 1;
        usbcfg.b.force_dev_mode = 0;
        break;
    case USB_MODE_FORCE_DEVICE:
        usbcfg.b.force_hst_mode = 0;
        usbcfg.b.force_dev_mode = 1;
        break;
    case USB_MODE_NORMAL:
        usbcfg.b.force_hst_mode = 0;
        usbcfg.b.force_dev_mode = 0;
        break;
    }
    dwc_write_reg32( &core_if->core_global_regs->gusbcfg, usbcfg.d32 );
}

static ssize_t force_usb_mode_store(struct device_driver *_drv, const char *_buf, 
			  size_t _count ) 
{
    int new_mode = simple_strtoul(_buf, NULL, 16);
    dwc_otg_device_t *otg_dev = g_otgdev;
    dwc_otg_core_if_t *core_if = otg_dev->core_if;
    DWC_PRINT("%s %d->%d\n",__func__, core_if->usb_mode, new_mode);
    if(core_if->usb_mode == new_mode)
    {
    	return _count;
    }

	switch(new_mode)
	{
		case USB_MODE_FORCE_HOST:
			if(USB_MODE_FORCE_DEVICE == core_if->usb_mode)
			{/* device-->host */
				core_if->usb_mode = new_mode;
				dwc_otg_force_host(core_if);
			}
			else if(USB_MODE_NORMAL == core_if->usb_mode)
			{
				core_if->usb_mode = new_mode;
				if(dwc_otg_is_host_mode(core_if))
				{
					dwc_otg_set_gusbcfg(core_if, new_mode);
				}
				else
				{
					dwc_otg_force_host(core_if);
				}
			}
			else
			    core_if->usb_mode = new_mode;
			break;
		case USB_MODE_FORCE_DEVICE:
			if(USB_MODE_FORCE_HOST == core_if->usb_mode)
			{
				core_if->usb_mode = new_mode;
				dwc_otg_force_device(core_if);
			}
			else if(USB_MODE_NORMAL == core_if->usb_mode)
			{
				core_if->usb_mode = new_mode;
				if(dwc_otg_is_device_mode(core_if))
				{
					dwc_otg_set_gusbcfg(core_if, new_mode);
				}
				else
				{
					dwc_otg_force_device(core_if);
				}
			}
			break;
		case USB_MODE_NORMAL:
			#if 1
			if(USB_MODE_FORCE_DEVICE == core_if->usb_mode)
			{
				core_if->usb_mode = new_mode;
				if((otg_dev->pcd)&&(otg_dev->pcd->phy_suspend == 1))
				{
					rk28_usb_suspend( 1 );
				}
				del_timer(&otg_dev->pcd->check_vbus_timer);
				dwc_otg_set_gusbcfg(core_if, new_mode);
				msleep(50);
				if(dwc_otg_is_host_mode(core_if))
				{
					dwc_otg_force_host(core_if);
				}
				else
				{
					dwc_otg_pcd_start_vbus_timer( otg_dev->pcd );
				}
				//mdelay(10);
				//core_if->usb_mode = new_mode;
				//if(!dwc_otg_connid(core_if))
				//	dwc_otg_force_host(core_if);
			}
			else if(USB_MODE_FORCE_HOST == core_if->usb_mode)
			{
				if((otg_dev->pcd)&&(otg_dev->pcd->phy_suspend == 1))
				{
					rk28_usb_suspend( 1 );
				}
				core_if->usb_mode = new_mode;
				dwc_otg_set_gusbcfg(core_if, new_mode);
				msleep(100);
				if(dwc_otg_is_device_mode(core_if))
				{
					dwc_otg_force_device(core_if);
				}
				//if(dwc_otg_connid(core_if))
				//	hub_disconnect_device();
				//core_if->usb_mode = new_mode;
				//	dwc_otg_force_device(core_if);
			}
			#endif
			break;
		default:
			break;
	}	
	return _count;	
}
static DRIVER_ATTR(force_usb_mode, 0666/*S_IRUGO|S_IWUSR*/, force_usb_mode_show, force_usb_mode_store);
#endif
static ssize_t dwc_otg_enable_show( struct device *_dev, 
								struct device_attribute *attr, char *buf)
{
    dwc_otg_device_t *otg_dev = _dev->platform_data;
    return sprintf (buf, "%d\n", otg_dev->hcd->host_enabled);
}

static ssize_t dwc_otg_enable_store( struct device *_dev,
								struct device_attribute *attr, 
								const char *buf, size_t count )
{
    dwc_otg_device_t *otg_dev = _dev->platform_data;
    dwc_otg_core_if_t *_core_if = otg_dev->core_if;
    struct platform_device *pdev = to_platform_device(_dev);
	uint32_t val = simple_strtoul(buf, NULL, 16);
	if(otg_dev->hcd->host_enabled == val)
	    return count;
	    
	otg_dev->hcd->host_enabled = val;
	if(val == 0)    // enable -> disable
	{
	    DWC_PRINT("disable host controller:%s,id:%d\n",pdev->name,pdev->id);
	    #if 1
        if (_core_if->hcd_cb && _core_if->hcd_cb->disconnect) {
                _core_if->hcd_cb->disconnect( _core_if->hcd_cb->p );
        }
        #endif
        if (_core_if->hcd_cb && _core_if->hcd_cb->stop) {
                _core_if->hcd_cb->stop( _core_if->hcd_cb->p );
        }
        if (_core_if->hcd_cb && _core_if->hcd_cb->suspend) {
                _core_if->hcd_cb->suspend( _core_if->hcd_cb->p, val);
        }
        udelay(3);
        clk_disable(otg_dev->phyclk);
        clk_disable(otg_dev->ahbclk);
	}
	else if(val == 1)
	{
	    DWC_PRINT("enable host controller:%s\n",pdev->name);
        clk_enable(otg_dev->phyclk);
        clk_enable(otg_dev->ahbclk);
        if (_core_if->hcd_cb && _core_if->hcd_cb->suspend) {
                _core_if->hcd_cb->suspend( _core_if->hcd_cb->p, val);
        }
        mdelay(5);
        if (_core_if->hcd_cb && _core_if->hcd_cb->start) {
                _core_if->hcd_cb->start( _core_if->hcd_cb->p );
        }
	}

    return count;
}
static DEVICE_ATTR(enable, S_IRUGO|S_IWUSR, dwc_otg_enable_show, dwc_otg_enable_store);
#endif
static ssize_t dwc_otg_conn_en_show(struct device_driver *_drv, char *_buf)
{
#ifndef CONFIG_DWC_OTG_HOST_ONLY
    dwc_otg_device_t *otg_dev = g_otgdev;
    dwc_otg_pcd_t *_pcd = otg_dev->pcd;
    return sprintf (_buf, "%d\n", _pcd->conn_en);
#else
    return sprintf(_buf, "0\n");
#endif
}

static ssize_t dwc_otg_conn_en_store(struct device_driver *_drv, const char *_buf,
				     size_t _count)
{
#ifndef CONFIG_DWC_OTG_HOST_ONLY
    int enable = simple_strtoul(_buf, NULL, 10);
    dwc_otg_device_t *otg_dev = g_otgdev;
    dwc_otg_pcd_t *_pcd = otg_dev->pcd;
    DWC_PRINT("%s %d->%d\n",__func__, _pcd->conn_en, enable);
    
    _pcd->conn_en = enable;
#endif
    return _count;
}
static DRIVER_ATTR(dwc_otg_conn_en, S_IRUGO|S_IWUSR, dwc_otg_conn_en_show, dwc_otg_conn_en_store);
#ifndef CONFIG_DWC_OTG_HOST_ONLY
static ssize_t vbus_status_show(struct device_driver *_drv, char *_buf)
{
    dwc_otg_device_t *otg_dev = g_otgdev;
    dwc_otg_pcd_t *_pcd = otg_dev->pcd;
    return sprintf (_buf, "%d\n", _pcd->vbus_status);
}
static DRIVER_ATTR(vbus_status, S_IRUGO|S_IWUSR, vbus_status_show, NULL);
#endif
volatile depctl_data_t depctl_ep0 = {.d32 = 0};
volatile depctl_data_t depctl_ep2 = {.d32 = 0};
volatile depctl_data_t depctl_ep4 = {.d32 = 0};
void dwc_otg_epout_save(void)
{
    dwc_otg_device_t *otg_dev = g_otgdev;
	dwc_otg_dev_if_t *dev_if = otg_dev->core_if->dev_if;
    volatile depctl_data_t depctl = {.d32 = 0};
    volatile grstctl_t grstctl = {.d32 = 0};
    grstctl.d32 = dwc_read_reg32(&otg_dev->core_if->core_global_regs->grstctl);

    while(grstctl.b.ahbidle != 1)
    {
        grstctl.d32 = dwc_read_reg32(&otg_dev->core_if->core_global_regs->grstctl);
    }
    depctl_ep0.d32 = dwc_read_reg32(&dev_if->out_ep_regs[0]->doepctl);
    depctl.d32 = depctl_ep0.d32;
    if(depctl.b.epena)
    {
        depctl.b.epena = 0;
        dwc_write_reg32(&dev_if->out_ep_regs[0]->doepctl, depctl.d32);
    }
    depctl_ep2.d32 = dwc_read_reg32(&dev_if->out_ep_regs[2]->doepctl);
    depctl.d32 = depctl_ep2.d32;
    if(depctl.b.epena)
    {
        depctl.b.epena = 0;
        dwc_write_reg32(&dev_if->out_ep_regs[2]->doepctl, depctl.d32);
    }
    depctl_ep4.d32 = dwc_read_reg32(&dev_if->out_ep_regs[4]->doepctl);
    depctl.d32 = depctl_ep4.d32;
    if(depctl.b.epena)
    {
        depctl.b.epena = 0;
        dwc_write_reg32(&dev_if->out_ep_regs[4]->doepctl, depctl.d32);
    }
}
void dwc_otg_epout_restore(void)
{
    dwc_otg_device_t *otg_dev = g_otgdev;
	dwc_otg_dev_if_t *dev_if = otg_dev->core_if->dev_if;
    dwc_write_reg32(&dev_if->out_ep_regs[0]->doepctl, depctl_ep0.d32);
    dwc_write_reg32(&dev_if->out_ep_regs[2]->doepctl, depctl_ep2.d32);
    dwc_write_reg32(&dev_if->out_ep_regs[4]->doepctl, depctl_ep4.d32);
}

/**
 * This function is called during module intialization to verify that
 * the module parameters are in a valid state.
 */
static int check_parameters(dwc_otg_core_if_t *core_if)
{
	int i;
	int retval = 0;
	dwc_otg_core_params_t	   *core_params;
	core_params = core_if->core_params;
/* Checks if the parameter is outside of its valid range of values */
#define DWC_OTG_PARAM_TEST(_param_,_low_,_high_) \
		((core_params->_param_ < (_low_)) || \
		(core_params->_param_ > (_high_)))

/* If the parameter has been set by the user, check that the parameter value is
 * within the value range of values.  If not, report a module error. */
#define DWC_OTG_PARAM_ERR(_param_,_low_,_high_,_string_) \
		do { \
			if (core_params->_param_ != -1) { \
				if (DWC_OTG_PARAM_TEST(_param_,(_low_),(_high_))) { \
					DWC_ERROR("`%d' invalid for parameter `%s'\n", \
						core_params->_param_, _string_); \
					core_params->_param_ = dwc_param_##_param_##_default; \
					retval ++; \
				} \
			} \
		} while (0)

	DWC_OTG_PARAM_ERR(opt,0,1,"opt");
	DWC_OTG_PARAM_ERR(otg_cap,0,2,"otg_cap");
	DWC_OTG_PARAM_ERR(dma_enable,0,1,"dma_enable");
	DWC_OTG_PARAM_ERR(speed,0,1,"speed");
	DWC_OTG_PARAM_ERR(host_support_fs_ls_low_power,0,1,"host_support_fs_ls_low_power");
	DWC_OTG_PARAM_ERR(host_ls_low_power_phy_clk,0,1,"host_ls_low_power_phy_clk");
	DWC_OTG_PARAM_ERR(enable_dynamic_fifo,0,1,"enable_dynamic_fifo");
	DWC_OTG_PARAM_ERR(data_fifo_size,32,32768,"data_fifo_size");
	DWC_OTG_PARAM_ERR(dev_rx_fifo_size,16,32768,"dev_rx_fifo_size");
	DWC_OTG_PARAM_ERR(dev_nperio_tx_fifo_size,16,32768,"dev_nperio_tx_fifo_size");
	DWC_OTG_PARAM_ERR(host_rx_fifo_size,16,32768,"host_rx_fifo_size");
	DWC_OTG_PARAM_ERR(host_nperio_tx_fifo_size,16,32768,"host_nperio_tx_fifo_size");
	DWC_OTG_PARAM_ERR(host_perio_tx_fifo_size,16,32768,"host_perio_tx_fifo_size");
	DWC_OTG_PARAM_ERR(max_transfer_size,2047,524288,"max_transfer_size");
	DWC_OTG_PARAM_ERR(max_packet_count,15,511,"max_packet_count");
	DWC_OTG_PARAM_ERR(host_channels,1,16,"host_channels");
	DWC_OTG_PARAM_ERR(dev_endpoints,1,15,"dev_endpoints");
	DWC_OTG_PARAM_ERR(phy_type,0,2,"phy_type");
	DWC_OTG_PARAM_ERR(phy_ulpi_ddr,0,1,"phy_ulpi_ddr");
	DWC_OTG_PARAM_ERR(phy_ulpi_ext_vbus,0,1,"phy_ulpi_ext_vbus");
	DWC_OTG_PARAM_ERR(i2c_enable,0,1,"i2c_enable");
	DWC_OTG_PARAM_ERR(ulpi_fs_ls,0,1,"ulpi_fs_ls");
	DWC_OTG_PARAM_ERR(ts_dline,0,1,"ts_dline");

	if (core_params->dma_burst_size != -1) 
	{
		if (DWC_OTG_PARAM_TEST(dma_burst_size,1,1) &&
			DWC_OTG_PARAM_TEST(dma_burst_size,4,4) &&
			DWC_OTG_PARAM_TEST(dma_burst_size,8,8) &&
			DWC_OTG_PARAM_TEST(dma_burst_size,16,16) &&
			DWC_OTG_PARAM_TEST(dma_burst_size,32,32) &&
			DWC_OTG_PARAM_TEST(dma_burst_size,64,64) &&
			DWC_OTG_PARAM_TEST(dma_burst_size,128,128) &&
			DWC_OTG_PARAM_TEST(dma_burst_size,256,256))
		{
			DWC_ERROR("`%d' invalid for parameter `dma_burst_size'\n", 
				  core_params->dma_burst_size);
			core_params->dma_burst_size = 32;
			retval ++;
		}
	}

	if (core_params->phy_utmi_width != -1) 
	{
		if (DWC_OTG_PARAM_TEST(phy_utmi_width,8,8) &&
			DWC_OTG_PARAM_TEST(phy_utmi_width,16,16)) 
		{
			DWC_ERROR("`%d' invalid for parameter `phy_utmi_width'\n", 
				  core_params->phy_utmi_width);
			core_params->phy_utmi_width = 16;
			retval ++;
		}
	}

	for (i=0; i<15; i++) 
	{
		/** @todo should be like above */
		//DWC_OTG_PARAM_ERR(dev_perio_tx_fifo_size[i],4,768,"dev_perio_tx_fifo_size");
		if (core_params->dev_perio_tx_fifo_size[i] != -1) 
		{
			if (DWC_OTG_PARAM_TEST(dev_perio_tx_fifo_size[i],4,768)) 
			{
				DWC_ERROR("`%d' invalid for parameter `%s_%d'\n",
					  core_params->dev_perio_tx_fifo_size[i], "dev_perio_tx_fifo_size", i);
				core_params->dev_perio_tx_fifo_size[i] = dwc_param_dev_perio_tx_fifo_size_default;
				retval ++;
			}
		}
	}

	DWC_OTG_PARAM_ERR(en_multiple_tx_fifo,0,1,"en_multiple_tx_fifo");

	for (i=0; i<15; i++) 
	{
		/** @todo should be like above */
		//DWC_OTG_PARAM_ERR(dev_tx_fifo_size[i],4,768,"dev_tx_fifo_size");
		if (core_params->dev_tx_fifo_size[i] != -1) 
		{
			if (DWC_OTG_PARAM_TEST(dev_tx_fifo_size[i],4,768)) 
			{
				DWC_ERROR("`%d' invalid for parameter `%s_%d'\n",
					  core_params->dev_tx_fifo_size[i], "dev_tx_fifo_size", i);
				core_params->dev_tx_fifo_size[i] = dwc_param_dev_tx_fifo_size_default;
				retval ++;
			}
		}
	}

	DWC_OTG_PARAM_ERR(thr_ctl, 0, 7, "thr_ctl");
	DWC_OTG_PARAM_ERR(tx_thr_length, 8, 128, "tx_thr_length");
	DWC_OTG_PARAM_ERR(rx_thr_length, 8, 128, "rx_thr_length");
	
	
	/* At this point, all module parameters that have been set by the user
	 * are valid, and those that have not are left unset.  Now set their
	 * default values and/or check the parameters against the hardware
	 * configurations of the OTG core. */



/* This sets the parameter to the default value if it has not been set by the
 * user */
#define DWC_OTG_PARAM_SET_DEFAULT(_param_) \
	({ \
		int changed = 1; \
		if (core_params->_param_ == -1) { \
			changed = 0; \
			core_params->_param_ = dwc_param_##_param_##_default; \
		} \
		changed; \
	})

/* This checks the macro agains the hardware configuration to see if it is
 * valid.  It is possible that the default value could be invalid.	In this
 * case, it will report a module error if the user touched the parameter.
 * Otherwise it will adjust the value without any error. */
#define DWC_OTG_PARAM_CHECK_VALID(_param_,_str_,_is_valid_,_set_valid_) \
	({ \
			int changed = DWC_OTG_PARAM_SET_DEFAULT(_param_); \
		int error = 0; \
		if (!(_is_valid_)) { \
			if (changed) { \
				DWC_ERROR("`%d' invalid for parameter `%s'.	 Check HW configuration.\n", core_params->_param_,_str_); \
				error = 1; \
			} \
			core_params->_param_ = (_set_valid_); \
		} \
		error; \
	})

	/* OTG Cap */
	retval += DWC_OTG_PARAM_CHECK_VALID(otg_cap,"otg_cap",
				  ({
					  int valid;
					  valid = 1;
					  switch (core_params->otg_cap) {
					  case DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE:
						  if (core_if->hwcfg2.b.op_mode != DWC_HWCFG2_OP_MODE_HNP_SRP_CAPABLE_OTG) valid = 0;
						  break;
					  case DWC_OTG_CAP_PARAM_SRP_ONLY_CAPABLE:
						  if ((core_if->hwcfg2.b.op_mode != DWC_HWCFG2_OP_MODE_HNP_SRP_CAPABLE_OTG) &&
							  (core_if->hwcfg2.b.op_mode != DWC_HWCFG2_OP_MODE_SRP_ONLY_CAPABLE_OTG) &&
							  (core_if->hwcfg2.b.op_mode != DWC_HWCFG2_OP_MODE_SRP_CAPABLE_DEVICE) &&
							  (core_if->hwcfg2.b.op_mode != DWC_HWCFG2_OP_MODE_SRP_CAPABLE_HOST))
						  {
							  valid = 0;
						  }
						  break;
					  case DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE:
						  /* always valid */
						  break;
					  } 
					  valid;
			  }),
					(((core_if->hwcfg2.b.op_mode == DWC_HWCFG2_OP_MODE_HNP_SRP_CAPABLE_OTG) ||
					(core_if->hwcfg2.b.op_mode == DWC_HWCFG2_OP_MODE_SRP_ONLY_CAPABLE_OTG) ||
					(core_if->hwcfg2.b.op_mode == DWC_HWCFG2_OP_MODE_SRP_CAPABLE_DEVICE) ||
					(core_if->hwcfg2.b.op_mode == DWC_HWCFG2_OP_MODE_SRP_CAPABLE_HOST)) ?
					DWC_OTG_CAP_PARAM_SRP_ONLY_CAPABLE :
					DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE));
	
	retval += DWC_OTG_PARAM_CHECK_VALID(dma_enable,"dma_enable",
				((core_params->dma_enable == 1) && (core_if->hwcfg2.b.architecture == 0)) ? 0 : 1, 
				0);

	retval += DWC_OTG_PARAM_CHECK_VALID(opt,"opt",
				1,
				0);

	DWC_OTG_PARAM_SET_DEFAULT(dma_burst_size);

	retval += DWC_OTG_PARAM_CHECK_VALID(host_support_fs_ls_low_power,
				"host_support_fs_ls_low_power",
				1, 0);

	retval += DWC_OTG_PARAM_CHECK_VALID(enable_dynamic_fifo,
					"enable_dynamic_fifo",
					((core_params->enable_dynamic_fifo == 0) ||
					(core_if->hwcfg2.b.dynamic_fifo == 1)), 0);
	

	retval += DWC_OTG_PARAM_CHECK_VALID(data_fifo_size,
					"data_fifo_size",
					(core_params->data_fifo_size <= core_if->hwcfg3.b.dfifo_depth),
					core_if->hwcfg3.b.dfifo_depth);

	retval += DWC_OTG_PARAM_CHECK_VALID(dev_rx_fifo_size,
					"dev_rx_fifo_size",
					(core_params->dev_rx_fifo_size <= dwc_read_reg32(&core_if->core_global_regs->grxfsiz)),
					dwc_read_reg32(&core_if->core_global_regs->grxfsiz));

	retval += DWC_OTG_PARAM_CHECK_VALID(dev_nperio_tx_fifo_size,
					"dev_nperio_tx_fifo_size",
					(core_params->dev_nperio_tx_fifo_size <= (dwc_read_reg32(&core_if->core_global_regs->gnptxfsiz) >> 16)),
					(dwc_read_reg32(&core_if->core_global_regs->gnptxfsiz) >> 16));

	retval += DWC_OTG_PARAM_CHECK_VALID(host_rx_fifo_size,
					"host_rx_fifo_size",
					(core_params->host_rx_fifo_size <= dwc_read_reg32(&core_if->core_global_regs->grxfsiz)),
					dwc_read_reg32(&core_if->core_global_regs->grxfsiz));


	retval += DWC_OTG_PARAM_CHECK_VALID(host_nperio_tx_fifo_size,
					"host_nperio_tx_fifo_size",
					(core_params->host_nperio_tx_fifo_size <= (dwc_read_reg32(&core_if->core_global_regs->gnptxfsiz) >> 16)),
					(dwc_read_reg32(&core_if->core_global_regs->gnptxfsiz) >> 16));
	retval += DWC_OTG_PARAM_CHECK_VALID(host_perio_tx_fifo_size,
					"host_perio_tx_fifo_size",
					(core_params->host_perio_tx_fifo_size <= ((dwc_read_reg32(&core_if->core_global_regs->hptxfsiz) >> 16))),
					((dwc_read_reg32(&core_if->core_global_regs->hptxfsiz) >> 16)));

	retval += DWC_OTG_PARAM_CHECK_VALID(max_transfer_size,
					"max_transfer_size",
					(core_params->max_transfer_size < (1 << (core_if->hwcfg3.b.xfer_size_cntr_width + 11))),
					((1 << (core_if->hwcfg3.b.xfer_size_cntr_width + 11)) - 1));

	retval += DWC_OTG_PARAM_CHECK_VALID(max_packet_count,
					"max_packet_count",
					(core_params->max_packet_count < (1 << (core_if->hwcfg3.b.packet_size_cntr_width + 4))),
					((1 << (core_if->hwcfg3.b.packet_size_cntr_width + 4)) - 1));

	retval += DWC_OTG_PARAM_CHECK_VALID(host_channels,
					"host_channels",
					(core_params->host_channels <= (core_if->hwcfg2.b.num_host_chan + 1)),
					(core_if->hwcfg2.b.num_host_chan + 1));

	retval += DWC_OTG_PARAM_CHECK_VALID(dev_endpoints,
					"dev_endpoints",
					(core_params->dev_endpoints <= (core_if->hwcfg2.b.num_dev_ep)),
					core_if->hwcfg2.b.num_dev_ep);

/*
 * Define the following to disable the FS PHY Hardware checking.  This is for
 * internal testing only.
 *
 * #define NO_FS_PHY_HW_CHECKS 
 */

#ifdef NO_FS_PHY_HW_CHECKS
	retval += DWC_OTG_PARAM_CHECK_VALID(phy_type,
				"phy_type", 1, 0);
#else
	retval += DWC_OTG_PARAM_CHECK_VALID(phy_type,
				"phy_type",
				({
					int valid = 0;
					if ((core_params->phy_type == DWC_PHY_TYPE_PARAM_UTMI) &&
					((core_if->hwcfg2.b.hs_phy_type == 1) || 
					 (core_if->hwcfg2.b.hs_phy_type == 3)))
					{
						valid = 1;
					}
					else if ((core_params->phy_type == DWC_PHY_TYPE_PARAM_ULPI) &&
						 ((core_if->hwcfg2.b.hs_phy_type == 2) || 
						  (core_if->hwcfg2.b.hs_phy_type == 3)))
					{
						valid = 1;
					}
					else if ((core_params->phy_type == DWC_PHY_TYPE_PARAM_FS) &&
						 (core_if->hwcfg2.b.fs_phy_type == 1))
					{
						valid = 1;
					}
					valid;
				}),
				({
					int set = DWC_PHY_TYPE_PARAM_FS;
					if (core_if->hwcfg2.b.hs_phy_type) { 
						if ((core_if->hwcfg2.b.hs_phy_type == 3) || 
						(core_if->hwcfg2.b.hs_phy_type == 1)) {
							set = DWC_PHY_TYPE_PARAM_UTMI;
						}
						else {
							set = DWC_PHY_TYPE_PARAM_ULPI;
						}
					}
					set;
				}));
#endif

	retval += DWC_OTG_PARAM_CHECK_VALID(speed,"speed",
				(core_params->speed == 0) && (core_params->phy_type == DWC_PHY_TYPE_PARAM_FS) ? 0 : 1,
				core_params->phy_type == DWC_PHY_TYPE_PARAM_FS ? 1 : 0);

	retval += DWC_OTG_PARAM_CHECK_VALID(host_ls_low_power_phy_clk,
				"host_ls_low_power_phy_clk",
				((core_params->host_ls_low_power_phy_clk == DWC_HOST_LS_LOW_POWER_PHY_CLK_PARAM_48MHZ) && (core_params->phy_type == DWC_PHY_TYPE_PARAM_FS) ? 0 : 1),
				((core_params->phy_type == DWC_PHY_TYPE_PARAM_FS) ? DWC_HOST_LS_LOW_POWER_PHY_CLK_PARAM_6MHZ : DWC_HOST_LS_LOW_POWER_PHY_CLK_PARAM_48MHZ));

	DWC_OTG_PARAM_SET_DEFAULT(phy_ulpi_ddr);
	DWC_OTG_PARAM_SET_DEFAULT(phy_ulpi_ext_vbus);
	DWC_OTG_PARAM_SET_DEFAULT(phy_utmi_width);
	DWC_OTG_PARAM_SET_DEFAULT(ulpi_fs_ls);
	DWC_OTG_PARAM_SET_DEFAULT(ts_dline);

#ifdef NO_FS_PHY_HW_CHECKS
	retval += DWC_OTG_PARAM_CHECK_VALID(i2c_enable,
				"i2c_enable", 1, 0);
#else
	retval += DWC_OTG_PARAM_CHECK_VALID(i2c_enable,
				"i2c_enable",
				(core_params->i2c_enable == 1) && (core_if->hwcfg3.b.i2c == 0) ? 0 : 1,
				0);
#endif

	for (i=0; i<15; i++) 
	{
		int changed = 1;
		int error = 0;

		if (core_params->dev_perio_tx_fifo_size[i] == -1) 
		{
			changed = 0;
			core_params->dev_perio_tx_fifo_size[i] = dwc_param_dev_perio_tx_fifo_size_default;
		}
		if (!(core_params->dev_perio_tx_fifo_size[i] <= (dwc_read_reg32(&core_if->core_global_regs->dptxfsiz_dieptxf[i])))) 
		{
			if (changed) 
			{
				DWC_ERROR("`%d' invalid for parameter `dev_perio_fifo_size_%d'.	 Check HW configuration.\n", core_params->dev_perio_tx_fifo_size[i],i);
				error = 1;
			}
			core_params->dev_perio_tx_fifo_size[i] = dwc_read_reg32(&core_if->core_global_regs->dptxfsiz_dieptxf[i]);
		}
		retval += error;
	}


	retval += DWC_OTG_PARAM_CHECK_VALID(en_multiple_tx_fifo,"en_multiple_tx_fifo",
						((core_params->en_multiple_tx_fifo == 1) && (core_if->hwcfg4.b.ded_fifo_en == 0)) ? 0 : 1, 
						0);

	
	for (i=0; i<15; i++) 
	{

		int changed = 1;
		int error = 0;

		if (core_params->dev_tx_fifo_size[i] == -1) 
		{
			changed = 0;
			core_params->dev_tx_fifo_size[i] = dwc_param_dev_tx_fifo_size_default;
		}
		if (!(core_params->dev_tx_fifo_size[i] <= (dwc_read_reg32(&core_if->core_global_regs->dptxfsiz_dieptxf[i])))) 
		{
			if (changed) 
			{
				DWC_ERROR("%d' invalid for parameter `dev_perio_fifo_size_%d'.	Check HW configuration.\n", core_params->dev_tx_fifo_size[i],i);
				error = 1;
			}
			core_params->dev_tx_fifo_size[i] = dwc_read_reg32(&core_if->core_global_regs->dptxfsiz_dieptxf[i]);
		}
		retval += error;
		
		
	}
	
	DWC_OTG_PARAM_SET_DEFAULT(thr_ctl);
	DWC_OTG_PARAM_SET_DEFAULT(tx_thr_length);
	DWC_OTG_PARAM_SET_DEFAULT(rx_thr_length);
	
	return retval;
}
/** 
 * This function is the top level interrupt handler for the Common
 * (Device and host modes) interrupts.
 */
static irqreturn_t dwc_otg_common_irq(int _irq, void *_dev)
{
	dwc_otg_device_t *otg_dev = _dev;
	int32_t retval = IRQ_NONE;

	retval = dwc_otg_handle_common_intr( otg_dev->core_if );
	return IRQ_RETVAL(retval);
}

/**
 * This function is called when a lm_device is unregistered with the
 * dwc_otg_driver. This happens, for example, when the rmmod command is
 * executed. The device may or may not be electrically present. If it is
 * present, the driver stops device processing. Any resources used on behalf
 * of this device are freed.
 *
 * @param[in] pdev
 */
static int dwc_otg_driver_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	dwc_otg_device_t *otg_dev = dev->platform_data;
	DWC_DEBUGPL(DBG_ANY, "%s(%p)\n", __func__, pdev);
	
	if (otg_dev == NULL) 
	{
		/* Memory allocation for the dwc_otg_device failed. */
		return 0;
	}

	/*
	 * Free the IRQ 
	 */
	if (otg_dev->common_irq_installed) 
	{
		free_irq( platform_get_irq(to_platform_device(dev),0), otg_dev );
	}

#ifndef CONFIG_DWC_OTG_DEVICE_ONLY
	if (otg_dev->hcd != NULL) 
	{
		dwc_otg_hcd_remove(dev);
	}
#endif

#ifndef CONFIG_DWC_OTG_HOST_ONLY
	if (otg_dev->pcd != NULL) 
	{
		dwc_otg_pcd_remove(dev);
	}
#endif
	if (otg_dev->core_if != NULL) 
	{
		dwc_otg_cil_remove( otg_dev->core_if );
	}

	/*
	 * Remove the device attributes
	 */
	dwc_otg_attr_remove(dev);

	/*
	 * Return the memory.
	 */
	if (otg_dev->base != NULL) 
	{
		iounmap(otg_dev->base);
	}
	clk_put(otg_dev->phyclk);
	clk_disable(otg_dev->phyclk);
	clk_put(otg_dev->ahbclk);
	clk_disable(otg_dev->ahbclk);
	clk_put(otg_dev->busclk);
	clk_disable(otg_dev->busclk);
	kfree(otg_dev);

	/*
	 * Clear the drvdata pointer.
	 */
	dev->platform_data = 0;

#ifdef DWC_BOTH_HOST_SLAVE
	dwc_otg_module_params.host_rx_fifo_size = -1;
	dwc_otg_module_params.dev_nperio_tx_fifo_size = -1;
	dwc_otg_module_params.host_nperio_tx_fifo_size = -1;
	dwc_otg_module_params.dev_rx_fifo_size = -1;
#endif	
	return 0;
}

/**
 * This function is called when an lm_device is bound to a
 * dwc_otg_driver. It creates the driver components required to
 * control the device (CIL, HCD, and PCD) and it initializes the
 * device. The driver components are stored in a dwc_otg_device
 * structure. A reference to the dwc_otg_device is saved in the
 * lm_device. This allows the driver to access the dwc_otg_device
 * structure on subsequent calls to driver methods for this device.
 *
 * @param[in] pdev  platform_device definition
 */
static __devinit int dwc_otg_driver_probe(struct platform_device *pdev)
{
	int retval = 0;
	struct resource *res_base;
	struct device *dev = &pdev->dev;
	dwc_otg_device_t *dwc_otg_device;
	int32_t snpsid;
	int irq;
	int32_t regval;
    struct clk *ahbclk,*phyclk,*busclk;
#ifdef CONFIG_ARCH_RK29    
    unsigned int * otg_phy_con1 = (unsigned int*)(USB_GRF_CON);
#endif
#ifdef CONFIG_ARCH_RK30
    unsigned int * otg_phy_con1 = (unsigned int*)(USBGRF_UOC0_CON2);
#endif
    
    regval = * otg_phy_con1;
#ifdef CONFIG_ARCH_RK29    
#ifndef CONFIG_USB11_HOST
	/*
	 * disable usb host 1.1 controller if not support
	 */
    phyclk = clk_get(NULL, "uhost");
    if (IS_ERR(phyclk)) {
            retval = PTR_ERR(phyclk);
            DWC_ERROR("can't get UHOST clock\n");
           goto fail;
    }
    clk_enable(phyclk);
    
    ahbclk = clk_get(NULL, "hclk_uhost");
    if (IS_ERR(ahbclk)) {
            retval = PTR_ERR(ahbclk);
            DWC_ERROR("can't get UHOST ahb bus clock\n");
           goto fail;
    }
    clk_enable(ahbclk);
    
	regval |= (0x01<<28);
	
    *otg_phy_con1 = regval;
    
    udelay(3);
    clk_disable(phyclk);
    clk_disable(ahbclk);
#endif
#ifndef CONFIG_USB20_HOST
	/*
	 * disable usb host 2.0 phy if not support
	 */
    phyclk = clk_get(NULL, "otgphy1");
    if (IS_ERR(phyclk)) {
            retval = PTR_ERR(phyclk);
            DWC_ERROR("can't get USBPHY1 clock\n");
           goto fail;
    }
    clk_enable(phyclk);
    
    ahbclk = clk_get(NULL, "usbotg1");
    if (IS_ERR(ahbclk)) {
            retval = PTR_ERR(ahbclk);
            DWC_ERROR("can't get USBOTG1 ahb bus clock\n");
           goto fail;
    }
    clk_enable(ahbclk);
    
    regval &= ~(0x01<<14);    // exit suspend.
    regval |= (0x01<<13);    // software control

    *otg_phy_con1 = regval;
    udelay(3);
    clk_disable(phyclk);
    clk_disable(ahbclk);
#endif
#endif

	dwc_otg_device = kmalloc(sizeof(dwc_otg_device_t), GFP_KERNEL);
	
	if (dwc_otg_device == 0) 
	{
		dev_err(dev, "kmalloc of dwc_otg_device failed\n");
		retval = -ENOMEM;
		goto fail;
	}
	
	memset(dwc_otg_device, 0, sizeof(*dwc_otg_device));
	dwc_otg_device->reg_offset = 0xFFFFFFFF;
	
#ifdef CONFIG_ARCH_RK29
	cru_set_soft_reset(SOFT_RST_USB_OTG_2_0_AHB_BUS, true);
	cru_set_soft_reset(SOFT_RST_USB_OTG_2_0_PHY, true);
	cru_set_soft_reset(SOFT_RST_USB_OTG_2_0_CONTROLLER, true);
    udelay(1);
	
	cru_set_soft_reset(SOFT_RST_USB_OTG_2_0_AHB_BUS, false);
	cru_set_soft_reset(SOFT_RST_USB_OTG_2_0_PHY, false);
	cru_set_soft_reset(SOFT_RST_USB_OTG_2_0_CONTROLLER, false);
    busclk = clk_get(NULL, "hclk_usb_peri");
    if (IS_ERR(busclk)) {
            retval = PTR_ERR(busclk);
            DWC_ERROR("can't get USB PERIPH AHB bus clock\n");
           goto fail;
    }
    clk_enable(busclk);
     
    phyclk = clk_get(NULL, "otgphy0");
    if (IS_ERR(phyclk)) {
            retval = PTR_ERR(phyclk);
            DWC_ERROR("can't get USBPHY0 clock\n");
           goto fail;
    }
    clk_enable(phyclk);
    
    ahbclk = clk_get(NULL, "usbotg0");
    if (IS_ERR(ahbclk)) {
            retval = PTR_ERR(ahbclk);
            DWC_ERROR("can't get USB otg0 ahb bus clock\n");
           goto fail;
    }
    clk_enable(ahbclk);
    
	/*
	 * Enable usb phy 0
	 */
    regval = * otg_phy_con1;
    regval |= (0x01<<2);
    regval |= (0x01<<3);    // exit suspend.
    regval &= ~(0x01<<2);
    *otg_phy_con1 = regval;
    
	dwc_otg_device->phyclk = phyclk;
	dwc_otg_device->ahbclk = ahbclk;
	dwc_otg_device->busclk = busclk;
#endif	
	/*
	 * Map the DWC_otg Core memory into virtual address space.
	 */
	 
	res_base = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_base)
		goto fail;

	dwc_otg_device->base =
		ioremap(res_base->start,USBOTG_SIZE);
	if (dwc_otg_device->base == NULL)
	{
		dev_err(dev, "ioremap() failed\n");
		retval = -ENOMEM;
		goto fail;
	}
#if 0
	dwc_otg_device->base = (void*)(USB_OTG_BASE_ADDR_VA);
	
	if (dwc_otg_device->base == NULL)
	{
		dev_err(dev, "ioremap() failed\n");
		retval = -ENOMEM;
		goto fail;
	}
#endif	
	dev_dbg(dev, "base=0x%08x\n", (unsigned)dwc_otg_device->base);
	/*
	 * Attempt to ensure this device is really a DWC_otg Controller.
	 * Read and verify the SNPSID register contents. The value should be
	 * 0x45F42XXX, which corresponds to "OT2", as in "OTG version 2.XX".
	 */
	snpsid = dwc_read_reg32((uint32_t *)((uint8_t *)dwc_otg_device->base + 0x40));
	if ((snpsid & 0xFFFFF000) != 0x4F542000) 
	{
		dev_err(dev, "Bad value for SNPSID: 0x%08x\n", snpsid);
		retval = -EINVAL;
		goto fail;
	}

	/*
	 * Initialize driver data to point to the global DWC_otg
	 * Device structure.
	 */
	dev->platform_data = dwc_otg_device;
	dev_dbg(dev, "dwc_otg_device=0x%p\n", dwc_otg_device);
	g_otgdev = dwc_otg_device;
	
	dwc_otg_device->core_if = dwc_otg_cil_init( dwc_otg_device->base, 
							&dwc_otg_module_params);
	if (dwc_otg_device->core_if == 0) 
	{
		dev_err(dev, "CIL initialization failed!\n");
		retval = -ENOMEM;
		goto fail;
	}
	dwc_otg_device->core_if->otg_dev = dwc_otg_device;
	/*
	 * Validate parameter values.
	 */
	if (check_parameters(dwc_otg_device->core_if) != 0) 
	{
		retval = -EINVAL;
		goto fail;
	}

	/*
	 * Create Device Attributes in sysfs
	 */	 
	dwc_otg_attr_create(dev);
#ifndef CONFIG_DWC_OTG_DEVICE_ONLY
	retval |= device_create_file(dev, &dev_attr_enable);
#endif

	/*
	 * Disable the global interrupt until all the interrupt
	 * handlers are installed.
	 */
	dwc_otg_disable_global_interrupts( dwc_otg_device->core_if );
	/*
	 * Install the interrupt handler for the common interrupts before
	 * enabling common interrupts in core_init below.
	 */
	irq = platform_get_irq(to_platform_device(dev),0);
	DWC_DEBUGPL( DBG_CIL, "registering (common) handler for irq%d\n", 
			 irq);
	retval = request_irq(irq, dwc_otg_common_irq,
				 IRQF_SHARED, "dwc_otg", dwc_otg_device );
	if (retval != 0) 
	{
		DWC_ERROR("request of irq%d failed\n", irq);
		retval = -EBUSY;
		goto fail;
	} 
	else 
	{
		dwc_otg_device->common_irq_installed = 1;
	}

#ifdef CONFIG_MACH_IPMATE
	set_irq_type(irq, IRQT_LOW);
#endif

#ifdef CONFIG_DWC_OTG_DEVICE_ONLY
        dwc_otg_device->core_if->usb_mode = USB_MODE_FORCE_DEVICE;
#else 
#ifdef CONFIG_DWC_OTG_HOST_ONLY
        dwc_otg_device->core_if->usb_mode = USB_MODE_FORCE_HOST;
#else
        
#ifdef CONFIG_DWC_OTG_DEFAULT_HOST
        dwc_otg_device->core_if->usb_mode = USB_MODE_FORCE_HOST;
#else 
        dwc_otg_device->core_if->usb_mode = USB_MODE_NORMAL;
#endif

#endif
#endif

	/*
	 * Initialize the DWC_otg core.
	 */
	dwc_otg_core_init( dwc_otg_device->core_if );

/* Initialize the bus state.  If the core is in Device Mode
 * HALT the USB bus and return. */
#ifndef CONFIG_DWC_OTG_DEVICE_ONLY
#ifdef CONFIG_ARCH_RK29
    USB_IOMUX_INIT(GPIO4A5_OTG0DRVVBUS_NAME, GPIO4L_OTG0_DRV_VBUS);
#endif
#ifdef CONFIG_ARCH_RK30
    USB_IOMUX_INIT(GPIO0A5_OTGDRVVBUS_NAME, GPIO0A_OTG_DRV_VBUS);    
#endif
	/*
	 * Initialize the HCD
	 */
	retval = dwc_otg_hcd_init(dev);
	if (retval != 0) 
	{
		DWC_ERROR("dwc_otg_hcd_init failed\n");
		dwc_otg_device->hcd = NULL;
		goto fail;
	}
#endif
#ifndef CONFIG_DWC_OTG_HOST_ONLY
	/*
	 * Initialize the PCD
	 */
	retval = dwc_otg_pcd_init(dev);
	if (retval != 0) 
	{
		DWC_ERROR("dwc_otg_pcd_init failed\n");
		dwc_otg_device->pcd = NULL;
		goto fail;
	}
#endif

	
	/*
	 * Enable the global interrupt after all the interrupt
	 * handlers are installed.
	 */
	dwc_otg_enable_global_interrupts( dwc_otg_device->core_if );
#ifdef CONFIG_ARCH_RK29
#ifndef CONFIG_DWC_OTG_DEVICE_ONLY
    if(dwc_otg_device->hcd->host_enabled == 0)
    {
        clk_disable(dwc_otg_device->phyclk);
        clk_disable(dwc_otg_device->ahbclk);
        *otg_phy_con1 |= (0x01<<2);
        *otg_phy_con1 &= ~(0x01<<3);    // enter suspend.
    }
#endif
#endif
	return 0;
 fail:
	devm_kfree(&pdev->dev, dwc_otg_device);
	DWC_PRINT("dwc_otg_driver_probe fail,everest\n");
	return retval;
}

#ifndef CONFIG_DWC_OTG_HOST_ONLY
extern int rk28_usb_suspend( int exitsuspend );
static int dwc_otg_driver_suspend(struct platform_device *_dev , pm_message_t state )
{
	struct device *dev = &_dev->dev;
	dwc_otg_device_t *otg_dev = dev->platform_data;
    dwc_otg_core_if_t *core_if = otg_dev->core_if;
    if(core_if->op_state == A_HOST)
    {
    	DWC_PRINT("%s,A_HOST mode\n", __func__);
    	return 0;
    }
    /* Clear any pending interrupts */
    dwc_write_reg32( &core_if->core_global_regs->gintsts, 0xFFFFFFFF);
    dwc_otg_disable_global_interrupts(core_if);
    rk28_usb_suspend(0);
    del_timer(&otg_dev->pcd->check_vbus_timer); 
	
    return 0;
}
#else
static int dwc_otg_driver_suspend(struct platform_device *_dev , pm_message_t state )
{
    return 0;
}
#endif

static int dwc_otg_driver_resume(struct platform_device *_dev )
{
	struct device *dev = &_dev->dev;
	dwc_otg_device_t *otg_dev = dev->platform_data;
    dwc_otg_core_if_t *core_if = otg_dev->core_if;
    dctl_data_t dctl = {.d32=0};

    dwc_otg_core_global_regs_t *global_regs = 
	core_if->core_global_regs;
    if(core_if->op_state == A_HOST)
    {
    	DWC_PRINT("%s,A_HOST mode\n", __func__);
    	return 0;
    }
#ifndef CONFIG_DWC_OTG_HOST_ONLY

    rk28_usb_suspend(1);

    /* soft disconnect */
    /* 20100226,HSL@RK,if not disconnect,when usb cable in,will auto reconnect 
     *  besause now USB PHY is enable,and get USB RESET irq.
    */
    /* soft disconnect */
    dctl.d32 = dwc_read_reg32( &core_if->dev_if->dev_global_regs->dctl );
    dctl.b.sftdiscon = 1;
    dwc_write_reg32( &core_if->dev_if->dev_global_regs->dctl, dctl.d32 );
    
    /* Clear any pending interrupts */
    dwc_write_reg32( &global_regs->gintsts, 0xeFFFFFFF); 
    
    dwc_otg_enable_global_interrupts(core_if);
    mod_timer(&otg_dev->pcd->check_vbus_timer , jiffies + HZ);

//sendwakeup:        
    if(core_if->usb_wakeup)
    {
        core_if->usb_wakeup = 0;
    }
    DWC_PRINT("%s gahbcfg:0x%x\n", __func__, global_regs->gahbcfg);
#endif    
    return 0;
}

static void dwc_otg_driver_shutdown(struct platform_device *_dev )
{
	struct device *dev = &_dev->dev;
	dwc_otg_device_t *otg_dev = dev->platform_data;
    dwc_otg_core_if_t *core_if = otg_dev->core_if;
    dctl_data_t dctl = {.d32=0};

    DWC_PRINT("%s:: disconnect USB\n" , __func__ );
    /* soft disconnect */
    dctl.d32 = dwc_read_reg32( &core_if->dev_if->dev_global_regs->dctl );
    dctl.b.sftdiscon = 1;
    dwc_write_reg32( &core_if->dev_if->dev_global_regs->dctl, dctl.d32 );

    /* Clear any pending interrupts */
    dwc_write_reg32( &core_if->core_global_regs->gintsts, 0xFFFFFFFF); 

}

/** 
 * This structure defines the methods to be called by a bus driver
 * during the lifecycle of a device on that bus. Both drivers and
 * devices are registered with a bus driver. The bus driver matches
 * devices to drivers based on information in the device and driver
 * structures.
 *
 * The probe function is called when the bus driver matches a device
 * to this driver. The remove function is called when a device is
 * unregistered with the bus driver.
 */
static struct platform_driver dwc_otg_driver = {
	.probe = dwc_otg_driver_probe,
	.remove = dwc_otg_driver_remove,
	.suspend = dwc_otg_driver_suspend,
	.resume = dwc_otg_driver_resume,
	.shutdown = dwc_otg_driver_shutdown,
	.driver = {
		   .name = dwc_driver_name,
		   .owner = THIS_MODULE},
};

#ifdef CONFIG_USB11_HOST
extern void dwc_otg_hcd_remove(struct device *dev);
extern int __devinit host11_hcd_init(struct device *dev);

static int host11_driver_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	dwc_otg_device_t *otg_dev = dev->platform_data;
	DWC_DEBUGPL(DBG_ANY, "%s(%p)\n", __func__, pdev);
	
	if (otg_dev == NULL) 
	{
		/* Memory allocation for the dwc_otg_device failed. */
		return 0;
	}

	/*
	 * Free the IRQ 
	 */
	if (otg_dev->common_irq_installed) 
	{
		free_irq( platform_get_irq(to_platform_device(dev),0), otg_dev );
	}

	if (otg_dev->hcd != NULL) 
	{
		dwc_otg_hcd_remove(dev);
	}

	if (otg_dev->core_if != NULL) 
	{
		dwc_otg_cil_remove( otg_dev->core_if );
	}

	/*
	 * Remove the device attributes
	 */
	//dwc_otg_attr_remove(dev);

	/*
	 * Return the memory.
	 */
	if (otg_dev->base != NULL) 
	{
		iounmap(otg_dev->base);
	}
	clk_put(otg_dev->phyclk);
	clk_disable(otg_dev->phyclk);
	clk_put(otg_dev->ahbclk);
	clk_disable(otg_dev->ahbclk);
	kfree(otg_dev);

	/*
	 * Clear the drvdata pointer.
	 */
	dev->platform_data = 0;

	return 0;
}

/**
 * This function is called when an lm_device is bound to a
 * dwc_otg_driver. It creates the driver components required to
 * control the device (CIL, HCD, and PCD) and it initializes the
 * device. The driver components are stored in a dwc_otg_device
 * structure. A reference to the dwc_otg_device is saved in the
 * lm_device. This allows the driver to access the dwc_otg_device
 * structure on subsequent calls to driver methods for this device.
 *
 * @param[in] pdev  platform_device definition
 */
static __devinit int host11_driver_probe(struct platform_device *pdev)
{
	struct resource *res_base;
	int retval = 0;
	struct device *dev = &pdev->dev;
	dwc_otg_device_t *dwc_otg_device;
	int32_t snpsid;
	int irq;
    struct clk* ahbclk,*phyclk;
	/*
	 *Enable usb phy
	 */
    unsigned int * otg_phy_con1 = (unsigned int*)(USB_GRF_CON);
        
    *otg_phy_con1 &= ~(0x01<<28);    // exit suspend.
    #if 0
    *otg_phy_con1 |= (0x01<<2);
    *otg_phy_con1 |= (0x01<<3);    // exit suspend.
    *otg_phy_con1 &= ~(0x01<<2);
    otgreg = ioremap(RK2818_USBOTG_PHYS,RK2818_USBOTG_SIZE);
    DWC_PRINT("%s otg2.0 reg addr: 0x%x",__func__,otgreg);
    dwc_modify_reg32((uint32_t *)(otgreg+0xc),0x20000000,0x20000000);
	dwc_write_reg32((uint32_t *)(otgreg+0x440), 0x1000);
    #endif

	dwc_otg_device = kmalloc(sizeof(dwc_otg_device_t), GFP_KERNEL);
	
	if (dwc_otg_device == 0) 
	{
		dev_err(dev, "kmalloc of dwc_otg_device failed\n");
		retval = -ENOMEM;
		goto fail;
	}
	
	memset(dwc_otg_device, 0, sizeof(*dwc_otg_device));
	dwc_otg_device->reg_offset = 0xFFFFFFFF;
	
	cru_set_soft_reset(SOFT_RST_UHOST, true);
    udelay(1);
	
	cru_set_soft_reset(SOFT_RST_UHOST, false);
	
    phyclk = clk_get(NULL, "uhost");
    if (IS_ERR(phyclk)) {
            retval = PTR_ERR(phyclk);
            DWC_ERROR("can't get UHOST clock\n");
           goto fail;
    }
    clk_enable(phyclk);
    
    ahbclk = clk_get(NULL, "hclk_uhost");
    if (IS_ERR(ahbclk)) {
            retval = PTR_ERR(ahbclk);
            DWC_ERROR("can't get UHOST ahb bus clock\n");
           goto fail1;
    }
    clk_enable(ahbclk);
    
    if (clk_get_rate(phyclk) != 48000000) {
        DWC_PRINT("Bad USB clock (%d Hz), changing to 48000000 Hz\n",
                 (int)clk_get_rate(phyclk));
        if (clk_set_rate(phyclk, 48000000)) {
            DWC_ERROR("Unable to set correct USB clock (48MHz)\n");
            retval = -EIO;
            goto fail2;
        }
    }
	dwc_otg_device->ahbclk = ahbclk;
	dwc_otg_device->phyclk = phyclk;
	
	/*
	 * Map the DWC_otg Core memory into virtual address space.
	 */
	 
	res_base = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_base)
		goto fail;

	dwc_otg_device->base =
		ioremap(res_base->start,USBOTG_SIZE);
    DWC_PRINT("%s host1.1 reg addr: 0x%x remap:0x%x\n",__func__,
    		(unsigned)res_base->start, (unsigned)dwc_otg_device->base);
	if (dwc_otg_device->base == NULL)
	{
		DWC_ERROR("ioremap() failed\n");
		retval = -ENOMEM;
		goto fail;
	}
	DWC_DEBUGPL( DBG_CIL, "base addr for rk29 host11:0x%x\n", (unsigned)dwc_otg_device->base);
	/*
	 * Attempt to ensure this device is really a DWC_otg Controller.
	 * Read and verify the SNPSID register contents. The value should be
	 * 0x45F42XXX, which corresponds to "OT2", as in "OTG version 2.XX".
	 */
	snpsid = dwc_read_reg32((uint32_t *)((uint8_t *)dwc_otg_device->base + 0x40));
	if ((snpsid & 0xFFFFF000) != 0x4F542000) 
	{
	                DWC_PRINT("%s::snpsid=0x%x,want 0x%x" , __func__ , snpsid , 0x4F542000 );
		dev_err(dev, "Bad value for SNPSID: 0x%08x\n", snpsid);
		retval = -EINVAL;
		goto fail;
	}

	/*
	 * Initialize driver data to point to the global DWC_otg
	 * Device structure.
	 */
	dev->platform_data = dwc_otg_device;
	DWC_DEBUGPL(DBG_CIL, "dwc_otg_device=0x%p\n", dwc_otg_device);
	g_host11 = dwc_otg_device;
	
	dwc_otg_device->core_if = dwc_otg_cil_init( dwc_otg_device->base, 
							&host11_module_params);
	if (dwc_otg_device->core_if == 0) 
	{
		dev_err(dev, "CIL initialization failed!\n");
		retval = -ENOMEM;
		goto fail;
	}

	dwc_otg_device->core_if->otg_dev = dwc_otg_device;
	/*
	 * Validate parameter values.
	 */
	if (check_parameters(dwc_otg_device->core_if) != 0) 
	{
		retval = -EINVAL;
		goto fail;
	}

	/*
	 * Create Device Attributes in sysfs
	 */	 
	dwc_otg_attr_create(dev);
	retval |= device_create_file(dev, &dev_attr_enable);

	/*
	 * Disable the global interrupt until all the interrupt
	 * handlers are installed.
	 */
	dwc_otg_disable_global_interrupts( dwc_otg_device->core_if );
	/*
	 * Install the interrupt handler for the common interrupts before
	 * enabling common interrupts in core_init below.
	 */
	irq = platform_get_irq(to_platform_device(dev),0);
	DWC_DEBUGPL( DBG_CIL, "registering (common) handler for irq%d\n", 
			 irq);
	retval = request_irq(irq, dwc_otg_common_irq,
				 IRQF_SHARED, "dwc_otg", dwc_otg_device );
	if (retval != 0) 
	{
		DWC_ERROR("request of irq%d failed\n", irq);
		retval = -EBUSY;
		goto fail;
	} 
	else 
	{
		dwc_otg_device->common_irq_installed = 1;
	}

	/*
	 * Initialize the DWC_otg core.
	 */
	dwc_otg_core_init( dwc_otg_device->core_if );

	/*
	 * Initialize the HCD
	 */
	retval = host11_hcd_init(dev);
	if (retval != 0) 
	{
		DWC_ERROR("host11_hcd_init failed\n");
		dwc_otg_device->hcd = NULL;
		goto fail;
	}
	/*
	 * Enable the global interrupt after all the interrupt
	 * handlers are installed.
	 */
	dwc_otg_enable_global_interrupts( dwc_otg_device->core_if );
#ifndef CONFIG_USB11_HOST_EN
    *otg_phy_con1 |= (0x01<<28);    // enter suspend.
    clk_disable(phyclk);
    clk_disable(ahbclk);
#endif
	return 0;
    
fail2:
    clk_put(ahbclk);
    clk_disable(ahbclk);
fail1:
    clk_put(phyclk);
    clk_disable(phyclk);

 fail:
	devm_kfree(&pdev->dev, dwc_otg_device);
	DWC_PRINT("host11_driver_probe fail,everest\n");
	return retval;
}

static struct platform_driver host11_driver = {
	.probe = host11_driver_probe,
	.remove = host11_driver_remove,
	.driver = {
		   .name = "usb11_host",
		   .owner = THIS_MODULE},
};
#endif

#ifdef CONFIG_USB20_HOST
extern void dwc_otg_hcd_remove(struct device *dev);
extern int __devinit host20_hcd_init(struct device *_dev);


static int host20_driver_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	dwc_otg_device_t *otg_dev = dev->platform_data;
	DWC_DEBUGPL(DBG_ANY, "%s(%p)\n", __func__, pdev);
	
	if (otg_dev == NULL) 
	{
		/* Memory allocation for the dwc_otg_device failed. */
		return 0;
	}

	/*
	 * Free the IRQ 
	 */
	if (otg_dev->common_irq_installed) 
	{
		free_irq( platform_get_irq(to_platform_device(dev),0), otg_dev );
	}

	if (otg_dev->hcd != NULL) 
	{
		dwc_otg_hcd_remove(dev);
	}

	if (otg_dev->core_if != NULL) 
	{
		dwc_otg_cil_remove( otg_dev->core_if );
	}

	/*
	 * Remove the device attributes
	 */
	//dwc_otg_attr_remove(dev);

	/*
	 * Return the memory.
	 */
	if (otg_dev->base != NULL) 
	{
		iounmap(otg_dev->base);
	}
	clk_put(otg_dev->phyclk);
	clk_disable(otg_dev->phyclk);
	clk_put(otg_dev->ahbclk);
	clk_disable(otg_dev->ahbclk);
	kfree(otg_dev);

	/*
	 * Clear the drvdata pointer.
	 */
	dev->platform_data = 0;
	

	return 0;
}

/**
 * This function is called when an lm_device is bound to a
 * dwc_otg_driver. It creates the driver components required to
 * control the device (CIL, HCD, and PCD) and it initializes the
 * device. The driver components are stored in a dwc_otg_device
 * structure. A reference to the dwc_otg_device is saved in the
 * lm_device. This allows the driver to access the dwc_otg_device
 * structure on subsequent calls to driver methods for this device.
 *
 * @param[in] pdev  platform_device definition
 */
static __devinit int host20_driver_probe(struct platform_device *pdev)
{
	struct resource *res_base;
	int retval = 0;
	struct device *dev = &pdev->dev;
	dwc_otg_device_t *dwc_otg_device;
	int32_t snpsid;
	int irq;
	uint32_t otgreg;
    struct clk* ahbclk,*phyclk;
	/*
	 *Enable usb phy
	 */
#ifdef CONFIG_ARCH_RK29    
    unsigned int * otg_phy_con1 = (unsigned int*)(USB_GRF_CON);
#endif
#ifdef CONFIG_ARCH_RK30
    unsigned int * otg_phy_con1 = (unsigned int*)(USBGRF_UOC1_CON2);
#endif
        
    otgreg = * otg_phy_con1;
    otgreg |= (0x01<<13);    // software control
    otgreg |= (0x01<<14);    // exit suspend.
    otgreg &= ~(0x01<<13);    // software control
    *otg_phy_con1 = otgreg;
    #if 0
    *otg_phy_con1 |= (0x01<<2);
    *otg_phy_con1 |= (0x01<<3);    // exit suspend.
    *otg_phy_con1 &= ~(0x01<<2);
    otgreg = ioremap(RK2818_USBOTG_PHYS,RK2818_USBOTG_SIZE);
    DWC_PRINT("%s otg2.0 reg addr: 0x%x",__func__,otgreg);
    dwc_modify_reg32((uint32_t *)(otgreg+0xc),0x20000000,0x20000000);
	dwc_write_reg32((uint32_t *)(otgreg+0x440), 0x1000);
    #endif

	dwc_otg_device = kmalloc(sizeof(dwc_otg_device_t), GFP_KERNEL);
	
#ifdef CONFIG_ARCH_RK29  
	cru_set_soft_reset(SOFT_RST_USB_HOST_2_0_AHB_BUS, true);
	cru_set_soft_reset(SOFT_RST_USB_HOST_2_0_PHY, true);
	cru_set_soft_reset(SOFT_RST_USB_HOST_2_0_CONTROLLER, true);
	
    udelay(1);
	
	cru_set_soft_reset(SOFT_RST_USB_HOST_2_0_AHB_BUS, false);
	cru_set_soft_reset(SOFT_RST_USB_HOST_2_0_PHY, false);
	cru_set_soft_reset(SOFT_RST_USB_HOST_2_0_CONTROLLER, false);
#endif
#ifdef CONFIG_ARCH_RK30  
    *(unsigned int*)(USBGRF_UOC1_CON2+4) = ((1<<5)|((1<<5)<<16));
#endif    
	if (dwc_otg_device == 0) 
	{
		dev_err(dev, "kmalloc of dwc_otg_device failed\n");
		retval = -ENOMEM;
		goto fail;
	}
	
	memset(dwc_otg_device, 0, sizeof(*dwc_otg_device));
	dwc_otg_device->reg_offset = 0xFFFFFFFF;
	
    phyclk = clk_get(NULL, "otgphy1");
    if (IS_ERR(phyclk)) {
            retval = PTR_ERR(phyclk);
            DWC_ERROR("can't get USBPHY1 clock\n");
           goto fail;
    }
    clk_enable(phyclk);
    
#ifdef CONFIG_ARCH_RK29  
    ahbclk = clk_get(NULL, "usbotg1");
#endif
#ifdef CONFIG_ARCH_RK30  
    ahbclk = clk_get(NULL, "hclk_otg1");
#endif    
    if (IS_ERR(ahbclk)) {
            retval = PTR_ERR(ahbclk);
            DWC_ERROR("can't get USBOTG1 ahb bus clock\n");
           goto fail;
    }
    clk_enable(ahbclk);
	dwc_otg_device->phyclk = phyclk;
	dwc_otg_device->ahbclk = ahbclk;
	
	/*
	 * Map the DWC_otg Core memory into virtual address space.
	 */
	 
	res_base = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_base)
		goto fail;

	dwc_otg_device->base =
		ioremap(res_base->start,USBOTG_SIZE);
    DWC_PRINT("%s host2.0 reg addr: 0x%x remap:0x%x\n",__func__,
    		(unsigned)res_base->start, (unsigned)dwc_otg_device->base);
	if (dwc_otg_device->base == NULL)
	{
		DWC_ERROR("ioremap() failed\n");
		retval = -ENOMEM;
		goto fail;
	}
	DWC_DEBUGPL( DBG_CIL, "base addr for rk29 host20:0x%x\n", (unsigned)dwc_otg_device->base);
	/*
	 * Attempt to ensure this device is really a DWC_otg Controller.
	 * Read and verify the SNPSID register contents. The value should be
	 * 0x45F42XXX, which corresponds to "OT2", as in "OTG version 2.XX".
	 */
	snpsid = dwc_read_reg32((uint32_t *)((uint8_t *)dwc_otg_device->base + 0x40));
	if ((snpsid & 0xFFFFF000) != 0x4F542000) 
	{
	                DWC_PRINT("%s::snpsid=0x%x,want 0x%x" , __func__ , snpsid , 0x4F542000 );
		dev_err(dev, "Bad value for SNPSID: 0x%08x\n", snpsid);
		retval = -EINVAL;
		goto fail;
	}

	/*
	 * Initialize driver data to point to the global DWC_otg
	 * Device structure.
	 */
	dev->platform_data = dwc_otg_device;
	DWC_DEBUGPL(DBG_CIL, "dwc_otg_device=0x%p\n", dwc_otg_device);
	g_host20 = dwc_otg_device;
	
	dwc_otg_device->core_if = dwc_otg_cil_init( dwc_otg_device->base, 
							&host20_module_params);
	if (dwc_otg_device->core_if == 0) 
	{
		dev_err(dev, "CIL initialization failed!\n");
		retval = -ENOMEM;
		goto fail;
	}

	dwc_otg_device->core_if->otg_dev = dwc_otg_device;
	/*
	 * Validate parameter values.
	 */
	if (check_parameters(dwc_otg_device->core_if) != 0) 
	{
		retval = -EINVAL;
		goto fail;
	}

	/*
	 * Create Device Attributes in sysfs
	 */	 
	dwc_otg_attr_create(dev);
	retval |= device_create_file(dev, &dev_attr_enable);

	/*
	 * Disable the global interrupt until all the interrupt
	 * handlers are installed.
	 */
	dwc_otg_disable_global_interrupts( dwc_otg_device->core_if );
	/*
	 * Install the interrupt handler for the common interrupts before
	 * enabling common interrupts in core_init below.
	 */
	irq = platform_get_irq(to_platform_device(dev),0);
	DWC_DEBUGPL( DBG_CIL, "registering (common) handler for irq%d\n", 
			 irq);
	retval = request_irq(irq, dwc_otg_common_irq,
				 IRQF_SHARED, "dwc_otg", dwc_otg_device );
	if (retval != 0) 
	{
		DWC_ERROR("request of irq%d failed\n", irq);
		retval = -EBUSY;
		goto fail;
	} 
	else 
	{
		dwc_otg_device->common_irq_installed = 1;
	}
    
#ifdef CONFIG_ARCH_RK29
    USB_IOMUX_INIT(GPIO4A6_OTG1DRVVBUS_NAME, GPIO4L_OTG1_DRV_VBUS);
#endif    
#ifdef CONFIG_ARCH_RK30
    USB_IOMUX_INIT(GPIO0A6_HOSTDRVVBUS_NAME, GPIO0A_HOST_DRV_VBUS);    
#endif
	/*
	 * Initialize the DWC_otg core.
	 */
	dwc_otg_core_init( dwc_otg_device->core_if );

	/*
	 * Initialize the HCD
	 */
	retval = host20_hcd_init(dev);
	if (retval != 0) 
	{
		DWC_ERROR("host20_hcd_init failed\n");
		dwc_otg_device->hcd = NULL;
		goto fail;
	}
	/*
	 * Enable the global interrupt after all the interrupt
	 * handlers are installed.
	 */
	dwc_otg_enable_global_interrupts( dwc_otg_device->core_if );
#ifndef CONFIG_USB20_HOST_EN
    clk_disable(phyclk);
    clk_disable(ahbclk);
    otgreg &= ~(0x01<<14);    // suspend.
    otgreg |= (0x01<<13);     // software control
    *otg_phy_con1 = otgreg;
#endif
	return 0;

 fail:
	devm_kfree(&pdev->dev, dwc_otg_device);
	DWC_PRINT("host20_driver_probe fail,everest\n");
	return retval;
}

static struct platform_driver host20_driver = {
	.probe = host20_driver_probe,
	.remove = host20_driver_remove,
	.driver = {
		   .name = "usb20_host",
		   .owner = THIS_MODULE},
};
#endif

/**
 * This function is called when the dwc_otg_driver is installed with the
 * insmod command. It registers the dwc_otg_driver structure with the
 * appropriate bus driver. This will cause the dwc_otg_driver_probe function
 * to be called. In addition, the bus driver will automatically expose
 * attributes defined for the device and driver in the special sysfs file
 * system.
 *
 * @return
 */
static int __init dwc_otg_driver_init(void) 
{
	int retval = 0;
    /*
     *  USB2.0 OTG controller
     */
	retval = platform_driver_register(&dwc_otg_driver);
	if (retval < 0) 
	{
		DWC_ERROR("%s retval=%d\n", __func__, retval);
		return retval;
	}
	if (driver_create_file(&dwc_otg_driver.driver, &driver_attr_version))
		pr_warning("DWC_OTG: Failed to create driver version file\n");
	if (driver_create_file(&dwc_otg_driver.driver, &driver_attr_debuglevel))
		pr_warning("DWC_OTG: Failed to create driver debug level file\n");
#ifndef CONFIG_DWC_OTG_HOST_ONLY
	if(driver_create_file(&dwc_otg_driver.driver, &driver_attr_dwc_otg_conn_en))
		pr_warning("DWC_OTG: Failed to create driver dwc_otg_conn_en file");
#endif
#ifndef CONFIG_DWC_OTG_HOST_ONLY
	if(driver_create_file(&dwc_otg_driver.driver, &driver_attr_vbus_status))
		pr_warning("DWC_OTG: Failed to create driver vbus status file");
#endif
#ifdef DWC_BOTH_HOST_SLAVE
    if(driver_create_file(&dwc_otg_driver.driver, &driver_attr_force_usb_mode))
		pr_warning("DWC_OTG: Failed to create driver force usb mode file\n");
#endif
    
    /*
     *  USB2.0 host controller
     */
#ifdef CONFIG_USB20_HOST
    retval = platform_driver_register(&host20_driver);
    if (retval < 0) 
    {
        DWC_ERROR("%s retval=%d\n", __func__, retval);
        return retval;
    }
#endif

    /*
     *  USB1.1 host controller
     */

#ifdef CONFIG_USB11_HOST
	retval = platform_driver_register(&host11_driver);
	if (retval < 0) 
	{
		DWC_ERROR("%s retval=%d\n", __func__, retval);
		return retval;
	}
//	retval = driver_create_file(&host11_driver.driver, &driver_attr_enable_usb11);
#endif

	return retval;
}
module_init(dwc_otg_driver_init);

/** 
 * This function is called when the driver is removed from the kernel
 * with the rmmod command. The driver unregisters itself with its bus
 * driver.
 *
 */
static void __exit dwc_otg_driver_cleanup(void)
{
	DWC_PRINT("dwc_otg_driver_cleanup()\n");

	driver_remove_file(&dwc_otg_driver.driver, &driver_attr_version);
	driver_remove_file(&dwc_otg_driver.driver, &driver_attr_debuglevel);
    
#ifdef DWC_BOTH_HOST_SLAVE	
        driver_remove_file(&dwc_otg_driver.driver, &driver_attr_force_usb_mode);
#endif
#ifndef CONFIG_DWC_OTG_HOST_ONLY
    driver_remove_file(&dwc_otg_driver.driver, &driver_attr_dwc_otg_conn_en);
#endif
#ifndef CONFIG_DWC_OTG_HOST_ONLY
    driver_remove_file(&dwc_otg_driver.driver, &driver_attr_vbus_status);
#endif

	platform_driver_unregister(&dwc_otg_driver);
	
#ifdef CONFIG_USB11_HOST
	platform_driver_unregister(&host11_driver);
#endif

#ifdef CONFIG_USB20_HOST
	platform_driver_unregister(&host20_driver);
#endif
	DWC_PRINT("%s module removed\n", dwc_driver_name);
}
module_exit(dwc_otg_driver_cleanup);

MODULE_DESCRIPTION(DWC_DRIVER_DESC);
MODULE_AUTHOR("Synopsys Inc.");
MODULE_LICENSE("GPL");

module_param_named(otg_cap, dwc_otg_module_params.otg_cap, int, 0444);
MODULE_PARM_DESC(otg_cap, "OTG Capabilities 0=HNP&SRP 1=SRP Only 2=None");
module_param_named(opt, dwc_otg_module_params.opt, int, 0444);
MODULE_PARM_DESC(opt, "OPT Mode");
module_param_named(dma_enable, dwc_otg_module_params.dma_enable, int, 0444);
MODULE_PARM_DESC(dma_enable, "DMA Mode 0=Slave 1=DMA enabled");
module_param_named(dma_burst_size, dwc_otg_module_params.dma_burst_size, int, 0444);
MODULE_PARM_DESC(dma_burst_size, "DMA Burst Size 1, 4, 8, 16, 32, 64, 128, 256");
module_param_named(speed, dwc_otg_module_params.speed, int, 0444);
MODULE_PARM_DESC(speed, "Speed 0=High Speed 1=Full Speed");
module_param_named(host_support_fs_ls_low_power, dwc_otg_module_params.host_support_fs_ls_low_power, int, 0444);
MODULE_PARM_DESC(host_support_fs_ls_low_power, "Support Low Power w/FS or LS 0=Support 1=Don't Support");
module_param_named(host_ls_low_power_phy_clk, dwc_otg_module_params.host_ls_low_power_phy_clk, int, 0444);
MODULE_PARM_DESC(host_ls_low_power_phy_clk, "Low Speed Low Power Clock 0=48Mhz 1=6Mhz");
module_param_named(enable_dynamic_fifo, dwc_otg_module_params.enable_dynamic_fifo, int, 0444);
MODULE_PARM_DESC(enable_dynamic_fifo, "0=cC Setting 1=Allow Dynamic Sizing");
module_param_named(data_fifo_size, dwc_otg_module_params.data_fifo_size, int, 0444);
MODULE_PARM_DESC(data_fifo_size, "Total number of words in the data FIFO memory 32-32768");
module_param_named(dev_rx_fifo_size, dwc_otg_module_params.dev_rx_fifo_size, int, 0444);
MODULE_PARM_DESC(dev_rx_fifo_size, "Number of words in the Rx FIFO 16-32768");
module_param_named(dev_nperio_tx_fifo_size, dwc_otg_module_params.dev_nperio_tx_fifo_size, int, 0444);
MODULE_PARM_DESC(dev_nperio_tx_fifo_size, "Number of words in the non-periodic Tx FIFO 16-32768");
module_param_named(dev_perio_tx_fifo_size_1, dwc_otg_module_params.dev_perio_tx_fifo_size[0], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_1, "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_2, dwc_otg_module_params.dev_perio_tx_fifo_size[1], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_2, "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_3, dwc_otg_module_params.dev_perio_tx_fifo_size[2], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_3, "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_4, dwc_otg_module_params.dev_perio_tx_fifo_size[3], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_4, "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_5, dwc_otg_module_params.dev_perio_tx_fifo_size[4], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_5, "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_6, dwc_otg_module_params.dev_perio_tx_fifo_size[5], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_6, "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_7, dwc_otg_module_params.dev_perio_tx_fifo_size[6], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_7, "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_8, dwc_otg_module_params.dev_perio_tx_fifo_size[7], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_8, "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_9, dwc_otg_module_params.dev_perio_tx_fifo_size[8], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_9, "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_10, dwc_otg_module_params.dev_perio_tx_fifo_size[9], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_10, "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_11, dwc_otg_module_params.dev_perio_tx_fifo_size[10], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_11, "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_12, dwc_otg_module_params.dev_perio_tx_fifo_size[11], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_12, "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_13, dwc_otg_module_params.dev_perio_tx_fifo_size[12], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_13, "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_14, dwc_otg_module_params.dev_perio_tx_fifo_size[13], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_14, "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_15, dwc_otg_module_params.dev_perio_tx_fifo_size[14], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_15, "Number of words in the periodic Tx FIFO 4-768");
module_param_named(host_rx_fifo_size, dwc_otg_module_params.host_rx_fifo_size, int, 0444);
MODULE_PARM_DESC(host_rx_fifo_size, "Number of words in the Rx FIFO 16-32768");
module_param_named(host_nperio_tx_fifo_size, dwc_otg_module_params.host_nperio_tx_fifo_size, int, 0444);
MODULE_PARM_DESC(host_nperio_tx_fifo_size, "Number of words in the non-periodic Tx FIFO 16-32768");
module_param_named(host_perio_tx_fifo_size, dwc_otg_module_params.host_perio_tx_fifo_size, int, 0444);
MODULE_PARM_DESC(host_perio_tx_fifo_size, "Number of words in the host periodic Tx FIFO 16-32768");
module_param_named(max_transfer_size, dwc_otg_module_params.max_transfer_size, int, 0444);
/** @todo Set the max to 512K, modify checks */
MODULE_PARM_DESC(max_transfer_size, "The maximum transfer size supported in bytes 2047-65535");
module_param_named(max_packet_count, dwc_otg_module_params.max_packet_count, int, 0444);
MODULE_PARM_DESC(max_packet_count, "The maximum number of packets in a transfer 15-511");
module_param_named(host_channels, dwc_otg_module_params.host_channels, int, 0444);
MODULE_PARM_DESC(host_channels, "The number of host channel registers to use 1-16");
module_param_named(dev_endpoints, dwc_otg_module_params.dev_endpoints, int, 0444);
MODULE_PARM_DESC(dev_endpoints, "The number of endpoints in addition to EP0 available for device mode 1-15");
module_param_named(phy_type, dwc_otg_module_params.phy_type, int, 0444);
MODULE_PARM_DESC(phy_type, "0=Reserved 1=UTMI+ 2=ULPI");
module_param_named(phy_utmi_width, dwc_otg_module_params.phy_utmi_width, int, 0444);
MODULE_PARM_DESC(phy_utmi_width, "Specifies the UTMI+ Data Width 8 or 16 bits");
module_param_named(phy_ulpi_ddr, dwc_otg_module_params.phy_ulpi_ddr, int, 0444);
MODULE_PARM_DESC(phy_ulpi_ddr, "ULPI at double or single data rate 0=Single 1=Double");
module_param_named(phy_ulpi_ext_vbus, dwc_otg_module_params.phy_ulpi_ext_vbus, int, 0444);
MODULE_PARM_DESC(phy_ulpi_ext_vbus, "ULPI PHY using internal or external vbus 0=Internal");
module_param_named(i2c_enable, dwc_otg_module_params.i2c_enable, int, 0444);
MODULE_PARM_DESC(i2c_enable, "FS PHY Interface");
module_param_named(ulpi_fs_ls, dwc_otg_module_params.ulpi_fs_ls, int, 0444);
MODULE_PARM_DESC(ulpi_fs_ls, "ULPI PHY FS/LS mode only");
module_param_named(ts_dline, dwc_otg_module_params.ts_dline, int, 0444);
MODULE_PARM_DESC(ts_dline, "Term select Dline pulsing for all PHYs");
module_param_named(debug, g_dbg_lvl, int, 0444);
MODULE_PARM_DESC(debug, "");

module_param_named(en_multiple_tx_fifo, dwc_otg_module_params.en_multiple_tx_fifo, int, 0444);
MODULE_PARM_DESC(en_multiple_tx_fifo, "Dedicated Non Periodic Tx FIFOs 0=disabled 1=enabled");
module_param_named(dev_tx_fifo_size_1, dwc_otg_module_params.dev_tx_fifo_size[0], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_1, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_2, dwc_otg_module_params.dev_tx_fifo_size[1], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_2, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_3, dwc_otg_module_params.dev_tx_fifo_size[2], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_3, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_4, dwc_otg_module_params.dev_tx_fifo_size[3], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_4, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_5, dwc_otg_module_params.dev_tx_fifo_size[4], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_5, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_6, dwc_otg_module_params.dev_tx_fifo_size[5], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_6, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_7, dwc_otg_module_params.dev_tx_fifo_size[6], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_7, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_8, dwc_otg_module_params.dev_tx_fifo_size[7], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_8, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_9, dwc_otg_module_params.dev_tx_fifo_size[8], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_9, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_10, dwc_otg_module_params.dev_tx_fifo_size[9], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_10, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_11, dwc_otg_module_params.dev_tx_fifo_size[10], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_11, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_12, dwc_otg_module_params.dev_tx_fifo_size[11], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_12, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_13, dwc_otg_module_params.dev_tx_fifo_size[12], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_13, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_14, dwc_otg_module_params.dev_tx_fifo_size[13], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_14, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_15, dwc_otg_module_params.dev_tx_fifo_size[14], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_15, "Number of words in the Tx FIFO 4-768");

module_param_named(thr_ctl, dwc_otg_module_params.thr_ctl, int, 0444);
MODULE_PARM_DESC(thr_ctl, "Thresholding enable flag bit 0 - non ISO Tx thr., 1 - ISO Tx thr., 2 - Rx thr.- bit 0=disabled 1=enabled");
module_param_named(tx_thr_length, dwc_otg_module_params.tx_thr_length, int, 0444);
MODULE_PARM_DESC(tx_thr_length, "Tx Threshold length in 32 bit DWORDs");
module_param_named(rx_thr_length, dwc_otg_module_params.rx_thr_length, int, 0444);
MODULE_PARM_DESC(rx_thr_length, "Rx Threshold length in 32 bit DWORDs");
/** @page "Module Parameters"
 *
 * The following parameters may be specified when starting the module.
 * These parameters define how the DWC_otg controller should be
 * configured.	Parameter values are passed to the CIL initialization
 * function dwc_otg_cil_init
 *
 * Example: <code>modprobe dwc_otg speed=1 otg_cap=1</code>
 *
 
 <table>
 <tr><td>Parameter Name</td><td>Meaning</td></tr> 
 
 <tr>
 <td>otg_cap</td>
 <td>Specifies the OTG capabilities. The driver will automatically detect the
 value for this parameter if none is specified.
 - 0: HNP and SRP capable (default, if available)
 - 1: SRP Only capable
 - 2: No HNP/SRP capable
 </td></tr>
 
 <tr>
 <td>dma_enable</td>
 <td>Specifies whether to use slave or DMA mode for accessing the data FIFOs.
 The driver will automatically detect the value for this parameter if none is
 specified.
 - 0: Slave
 - 1: DMA (default, if available)
 </td></tr>
 
 <tr>
 <td>dma_burst_size</td>
 <td>The DMA Burst size (applicable only for External DMA Mode).
 - Values: 1, 4, 8 16, 32, 64, 128, 256 (default 32)
 </td></tr>
 
 <tr>
 <td>speed</td>
 <td>Specifies the maximum speed of operation in host and device mode. The
 actual speed depends on the speed of the attached device and the value of
 phy_type.
 - 0: High Speed (default)
 - 1: Full Speed
 </td></tr>
 
 <tr>
 <td>host_support_fs_ls_low_power</td>
 <td>Specifies whether low power mode is supported when attached to a Full
 Speed or Low Speed device in host mode.
 - 0: Don't support low power mode (default)
 - 1: Support low power mode
 </td></tr>
 
 <tr>
 <td>host_ls_low_power_phy_clk</td>
 <td>Specifies the PHY clock rate in low power mode when connected to a Low
 Speed device in host mode. This parameter is applicable only if
 HOST_SUPPORT_FS_LS_LOW_POWER is enabled.
 - 0: 48 MHz (default)
 - 1: 6 MHz
 </td></tr>
 
 <tr>
 <td>enable_dynamic_fifo</td>
 <td> Specifies whether FIFOs may be resized by the driver software.
 - 0: Use cC FIFO size parameters
 - 1: Allow dynamic FIFO sizing (default)
 </td></tr>
 
 <tr>
 <td>data_fifo_size</td>
 <td>Total number of 4-byte words in the data FIFO memory. This memory
 includes the Rx FIFO, non-periodic Tx FIFO, and periodic Tx FIFOs.
 - Values: 32 to 32768 (default 8192)

 Note: The total FIFO memory depth in the FPGA configuration is 8192.
 </td></tr>
 
 <tr>
 <td>dev_rx_fifo_size</td>
 <td>Number of 4-byte words in the Rx FIFO in device mode when dynamic
 FIFO sizing is enabled.
 - Values: 16 to 32768 (default 1064)
 </td></tr>
 
 <tr>
 <td>dev_nperio_tx_fifo_size</td>
 <td>Number of 4-byte words in the non-periodic Tx FIFO in device mode when
 dynamic FIFO sizing is enabled.
 - Values: 16 to 32768 (default 1024)
 </td></tr>
 
 <tr>
 <td>dev_perio_tx_fifo_size_n (n = 1 to 15)</td>
 <td>Number of 4-byte words in each of the periodic Tx FIFOs in device mode
 when dynamic FIFO sizing is enabled.
 - Values: 4 to 768 (default 256)
 </td></tr>
 
 <tr>
 <td>host_rx_fifo_size</td>
 <td>Number of 4-byte words in the Rx FIFO in host mode when dynamic FIFO
 sizing is enabled.
 - Values: 16 to 32768 (default 1024)
 </td></tr>
 
 <tr>
 <td>host_nperio_tx_fifo_size</td>
 <td>Number of 4-byte words in the non-periodic Tx FIFO in host mode when
 dynamic FIFO sizing is enabled in the core.
 - Values: 16 to 32768 (default 1024)
 </td></tr>
 
 <tr>
 <td>host_perio_tx_fifo_size</td>
 <td>Number of 4-byte words in the host periodic Tx FIFO when dynamic FIFO
 sizing is enabled.
 - Values: 16 to 32768 (default 1024)
 </td></tr>
 
 <tr>
 <td>max_transfer_size</td>
 <td>The maximum transfer size supported in bytes.
 - Values: 2047 to 65,535 (default 65,535)
 </td></tr>
 
 <tr>
 <td>max_packet_count</td>
 <td>The maximum number of packets in a transfer.
 - Values: 15 to 511 (default 511)
 </td></tr>
 
 <tr>
 <td>host_channels</td>
 <td>The number of host channel registers to use.
 - Values: 1 to 16 (default 12)

 Note: The FPGA configuration supports a maximum of 12 host channels.
 </td></tr>
 
 <tr>
 <td>dev_endpoints</td>
 <td>The number of endpoints in addition to EP0 available for device mode
 operations.
 - Values: 1 to 15 (default 6 IN and OUT)

 Note: The FPGA configuration supports a maximum of 6 IN and OUT endpoints in
 addition to EP0.
 </td></tr>
 
 <tr>
 <td>phy_type</td>
 <td>Specifies the type of PHY interface to use. By default, the driver will
 automatically detect the phy_type.
 - 0: Full Speed
 - 1: UTMI+ (default, if available)
 - 2: ULPI
 </td></tr>
 
 <tr>
 <td>phy_utmi_width</td>
 <td>Specifies the UTMI+ Data Width. This parameter is applicable for a
 phy_type of UTMI+. Also, this parameter is applicable only if the
 OTG_HSPHY_WIDTH cC parameter was set to "8 and 16 bits", meaning that the
 core has been configured to work at either data path width.
 - Values: 8 or 16 bits (default 16)
 </td></tr>
 
 <tr>
 <td>phy_ulpi_ddr</td>
 <td>Specifies whether the ULPI operates at double or single data rate. This
 parameter is only applicable if phy_type is ULPI.
 - 0: single data rate ULPI interface with 8 bit wide data bus (default)
 - 1: double data rate ULPI interface with 4 bit wide data bus
 </td></tr>

 <tr>
 <td>i2c_enable</td>
 <td>Specifies whether to use the I2C interface for full speed PHY. This
 parameter is only applicable if PHY_TYPE is FS.
 - 0: Disabled (default)
 - 1: Enabled
 </td></tr>

 <tr>
 <td>otg_en_multiple_tx_fifo</td>
 <td>Specifies whether dedicatedto tx fifos are enabled for non periodic IN EPs.
 The driver will automatically detect the value for this parameter if none is
 specified.
 - 0: Disabled
 - 1: Enabled (default, if available)
 </td></tr>

 <tr>
 <td>dev_tx_fifo_size_n (n = 1 to 15)</td>
 <td>Number of 4-byte words in each of the Tx FIFOs in device mode
 when dynamic FIFO sizing is enabled.
 - Values: 4 to 768 (default 256)
 </td></tr>

*/
