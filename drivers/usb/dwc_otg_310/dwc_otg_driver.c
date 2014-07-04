/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/otg/linux/drivers/dwc_otg_driver.c $
 * $Revision: #94 $
 * $Date: 2012/12/21 $
 * $Change: 2131568 $
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

#include "dwc_otg_os_dep.h"
#include "common_port/dwc_os.h"
#include "dwc_otg_dbg.h"
#include "dwc_otg_driver.h"
#include "dwc_otg_attr.h"
#include "dwc_otg_core_if.h"
#include "dwc_otg_pcd_if.h"
#include "dwc_otg_hcd_if.h"
#include "dwc_otg_cil.h"
#include "dwc_otg_pcd.h"

#include "usbdev_rk.h"

#define DWC_DRIVER_VERSION	"3.10a 21-DEC-2012"
#define DWC_DRIVER_DESC		"HS OTG USB Controller driver"

static const char dwc_host20_driver_name[] = "usb20_host";
static const char dwc_otg20_driver_name[] = "usb20_otg";

dwc_otg_device_t *g_otgdev;

extern int pcd_init(struct platform_device *_dev);
extern int otg20_hcd_init(struct platform_device *_dev);
extern int host20_hcd_init(struct platform_device *_dev);
extern int pcd_remove(struct platform_device *_dev);
extern void hcd_remove(struct platform_device *_dev);
extern void dwc_otg_adp_start(dwc_otg_core_if_t *core_if, uint8_t is_host);

#ifdef CONFIG_USB20_OTG
static struct usb20otg_pdata_id usb20otg_pdata[] = {
	{
	 .name = "rk3188-usb20otg",
	 .pdata = &usb20otg_pdata_rk3188,
	 },
	{
	 .name = "rk3288-usb20otg",
	 .pdata = &usb20otg_pdata_rk3288,
	 },
	{
	 .name = "rk3036-usb20otg",
	 .pdata = &usb20otg_pdata_rk3036,
	 },
	{},
};
#endif

#ifdef CONFIG_USB20_HOST
static struct usb20host_pdata_id usb20host_pdata[] = {
	{
	 .name = "rk3188-usb20host",
	 .pdata = &usb20host_pdata_rk3188,
	 },
	{
	 .name = "rk3288-usb20host",
	 .pdata = &usb20host_pdata_rk3288,
	 },
	{
	 .name = "rk3288-usb20host",
	 .pdata = &usb20host_pdata_rk3036,
	 },
	{},
};
#endif

#ifdef CONFIG_RK_USB_UART
static u32 usb_to_uart_status;
#endif
/*-------------------------------------------------------------------------*/
/* Encapsulate the module parameter settings */

struct dwc_otg_driver_module_params {
	int32_t opt;
	int32_t otg_cap;
	int32_t dma_enable;
	int32_t dma_desc_enable;
	int32_t dma_burst_size;
	int32_t speed;
	int32_t host_support_fs_ls_low_power;
	int32_t host_ls_low_power_phy_clk;
	int32_t enable_dynamic_fifo;
	int32_t data_fifo_size;
	int32_t dev_rx_fifo_size;
	int32_t dev_nperio_tx_fifo_size;
	uint32_t dev_perio_tx_fifo_size[MAX_PERIO_FIFOS];
	int32_t host_rx_fifo_size;
	int32_t host_nperio_tx_fifo_size;
	int32_t host_perio_tx_fifo_size;
	int32_t max_transfer_size;
	int32_t max_packet_count;
	int32_t host_channels;
	int32_t dev_endpoints;
	int32_t phy_type;
	int32_t phy_utmi_width;
	int32_t phy_ulpi_ddr;
	int32_t phy_ulpi_ext_vbus;
	int32_t i2c_enable;
	int32_t ulpi_fs_ls;
	int32_t ts_dline;
	int32_t en_multiple_tx_fifo;
	uint32_t dev_tx_fifo_size[MAX_TX_FIFOS];
	uint32_t thr_ctl;
	uint32_t tx_thr_length;
	uint32_t rx_thr_length;
	int32_t pti_enable;
	int32_t mpi_enable;
	int32_t lpm_enable;
	int32_t besl_enable;
	int32_t baseline_besl;
	int32_t deep_besl;
	int32_t ic_usb_cap;
	int32_t ahb_thr_ratio;
	int32_t power_down;
	int32_t reload_ctl;
	int32_t dev_out_nak;
	int32_t cont_on_bna;
	int32_t ahb_single;
	int32_t otg_ver;
	int32_t adp_enable;
};

static struct dwc_otg_driver_module_params dwc_otg_module_params = {
	.opt = -1,
	.otg_cap = DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE,
	.dma_enable = -1,
	.dma_desc_enable = 0,
	.dma_burst_size = -1,
	.speed = -1,
	.host_support_fs_ls_low_power = -1,
	.host_ls_low_power_phy_clk = -1,
	.enable_dynamic_fifo = 1,
	.data_fifo_size = -1,
	.dev_rx_fifo_size = 0x120,
	.dev_nperio_tx_fifo_size = 0x10,
	.dev_perio_tx_fifo_size = {
				   /* dev_perio_tx_fifo_size_1 */
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
				   /* 15 */
				   },
	.host_rx_fifo_size = -1,
	.host_nperio_tx_fifo_size = -1,
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
	.dev_tx_fifo_size = {
			     /* dev_tx_fifo_size */
			     0x100,
			     0x80,
			     0x80,
			     0x60,
			     0x10,
			     0x10,
			     -1,
			     -1,
			     -1,
			     -1,
			     -1,
			     -1,
			     -1,
			     -1,
			     -1
			     /* 15 */
			     },
	.thr_ctl = -1,
	.tx_thr_length = -1,
	.rx_thr_length = -1,
	.pti_enable = -1,
	.mpi_enable = -1,
	.lpm_enable = -1,
	.besl_enable = -1,
	.baseline_besl = -1,
	.deep_besl = -1,
	.ic_usb_cap = -1,
	.ahb_thr_ratio = -1,
	.power_down = -1,
	.reload_ctl = -1,
	.dev_out_nak = -1,
	.cont_on_bna = -1,
	.ahb_single = -1,
	.otg_ver = -1,
	.adp_enable = 0,
};

#ifdef CONFIG_USB20_HOST
static struct dwc_otg_driver_module_params dwc_host_module_params = {
	.opt = -1,
	.otg_cap = DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE,
	.dma_enable = -1,
	.dma_desc_enable = 0,
	.dma_burst_size = -1,
	.speed = -1,
	.host_support_fs_ls_low_power = -1,
	.host_ls_low_power_phy_clk = -1,
	.enable_dynamic_fifo = -1,
	.data_fifo_size = -1,
	.dev_rx_fifo_size = -1,
	.dev_nperio_tx_fifo_size = -1,
	.dev_perio_tx_fifo_size = {
				   /* dev_perio_tx_fifo_size_1 */
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
				   /* 15 */
				   },
	.host_rx_fifo_size = -1,
	.host_nperio_tx_fifo_size = -1,
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
	.dev_tx_fifo_size = {
			     /* dev_tx_fifo_size */
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
			     /* 15 */
			     },
	.thr_ctl = -1,
	.tx_thr_length = -1,
	.rx_thr_length = -1,
	.pti_enable = -1,
	.mpi_enable = -1,
	.lpm_enable = -1,
	.besl_enable = -1,
	.baseline_besl = -1,
	.deep_besl = -1,
	.ic_usb_cap = -1,
	.ahb_thr_ratio = -1,
	.power_down = -1,
	.reload_ctl = -1,
	.dev_out_nak = -1,
	.cont_on_bna = -1,
	.ahb_single = -1,
	.otg_ver = -1,
	.adp_enable = 0,
};
#endif

/**
 * This function shows the Driver Version.
 */
static ssize_t version_show(struct device_driver *dev, char *buf)
{
	return snprintf(buf, sizeof(DWC_DRIVER_VERSION) + 2, "%s\n",
			DWC_DRIVER_VERSION);
}

static DRIVER_ATTR(version, S_IRUGO, version_show, NULL);

/**
 * Global Debug Level Mask.
 */
uint32_t g_dbg_lvl = DBG_OFF;	/* OFF */

/**
 * This function shows the driver Debug Level.
 */
static ssize_t dbg_level_show(struct device_driver *drv, char *buf)
{
	return sprintf(buf, "0x%0x\n", g_dbg_lvl);
}

/**
 * This function stores the driver Debug Level.
 */
static ssize_t dbg_level_store(struct device_driver *drv, const char *buf,
			       size_t count)
{
	g_dbg_lvl = simple_strtoul(buf, NULL, 16);
	return count;
}

static DRIVER_ATTR(debuglevel, S_IRUGO | S_IWUSR, dbg_level_show,
		   dbg_level_store);

extern void hcd_start(dwc_otg_core_if_t *core_if);
extern struct usb_hub *g_dwc_otg_root_hub20;
extern void dwc_otg_hub_disconnect_device(struct usb_hub *hub);

void dwc_otg_force_host(dwc_otg_core_if_t *core_if)
{
	dwc_otg_device_t *otg_dev = core_if->otg_dev;
	dctl_data_t dctl = {.d32 = 0 };
	unsigned long flags;

	if (core_if->op_state == A_HOST) {
		printk("dwc_otg_force_host,already in A_HOST mode,everest\n");
		return;
	}
	core_if->op_state = A_HOST;

	cancel_delayed_work(&otg_dev->pcd->check_vbus_work);
	dctl.d32 = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dctl);
	dctl.b.sftdiscon = 1;
	DWC_WRITE_REG32(&core_if->dev_if->dev_global_regs->dctl, dctl.d32);

	local_irq_save(flags);
	cil_pcd_stop(core_if);
	/*
	 * Initialize the Core for Host mode.
	 */

	dwc_otg_core_init(core_if);
	dwc_otg_enable_global_interrupts(core_if);
	cil_hcd_start(core_if);
	local_irq_restore(flags);
}

void dwc_otg_force_device(dwc_otg_core_if_t *core_if)
{
	dwc_otg_device_t *otg_dev = core_if->otg_dev;
	unsigned long flags;

	local_irq_save(flags);

	if (core_if->op_state == B_PERIPHERAL) {
		printk
		    ("dwc_otg_force_device,already in B_PERIPHERAL,everest\n");
		return;
	}
	core_if->op_state = B_PERIPHERAL;
	cil_hcd_stop(core_if);
	/* dwc_otg_hub_disconnect_device(g_dwc_otg_root_hub20); */
	otg_dev->pcd->phy_suspend = 1;
	otg_dev->pcd->vbus_status = 0;
	dwc_otg_pcd_start_check_vbus_work(otg_dev->pcd);

	/* Reset the Controller */
	dwc_otg_core_reset(core_if);

	dwc_otg_core_init(core_if);
	dwc_otg_disable_global_interrupts(core_if);
	cil_pcd_start(core_if);

	local_irq_restore(flags);
}

static void dwc_otg_set_force_mode(dwc_otg_core_if_t *core_if, int mode)
{
	gusbcfg_data_t usbcfg = {.d32 = 0 };
	printk("!!!dwc_otg_set_force_mode\n");
	usbcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->gusbcfg);
	switch (mode) {
	case USB_MODE_FORCE_HOST:
		usbcfg.b.force_host_mode = 1;
		usbcfg.b.force_dev_mode = 0;
		break;
	case USB_MODE_FORCE_DEVICE:
		usbcfg.b.force_host_mode = 0;
		usbcfg.b.force_dev_mode = 1;
		break;
	case USB_MODE_NORMAL:
		usbcfg.b.force_host_mode = 0;
		usbcfg.b.force_dev_mode = 0;
		break;
	}
	DWC_WRITE_REG32(&core_if->core_global_regs->gusbcfg, usbcfg.d32);
}

static ssize_t force_usb_mode_show(struct device_driver *drv, char *buf)
{
	dwc_otg_device_t *otg_dev = g_otgdev;
	dwc_otg_core_if_t *core_if = otg_dev->core_if;

	return sprintf(buf, "%d\n", core_if->usb_mode);
}

static ssize_t force_usb_mode_store(struct device_driver *drv, const char *buf,
				    size_t count)
{
	int new_mode = simple_strtoul(buf, NULL, 16);
	dwc_otg_device_t *otg_dev = g_otgdev;
	dwc_otg_core_if_t *core_if;
	struct dwc_otg_platform_data *pldata;

	if (!otg_dev)
		return -EINVAL;

	core_if = otg_dev->core_if;
	pldata = otg_dev->pldata;

	DWC_PRINTF("%s %d->%d\n", __func__, core_if->usb_mode, new_mode);

	if (core_if->usb_mode == new_mode) {
		return count;
	}

	if (pldata->phy_status == USB_PHY_SUSPEND) {
		pldata->clock_enable(pldata, 1);
		pldata->phy_suspend(pldata, USB_PHY_ENABLED);
	}

	switch (new_mode) {
	case USB_MODE_FORCE_HOST:
		if (USB_MODE_FORCE_DEVICE == core_if->usb_mode) {
			/* device-->host */
			core_if->usb_mode = new_mode;
			dwc_otg_force_host(core_if);
		} else if (USB_MODE_NORMAL == core_if->usb_mode) {
			core_if->usb_mode = new_mode;
			if (dwc_otg_is_host_mode(core_if))
				dwc_otg_set_force_mode(core_if, new_mode);
			else
				dwc_otg_force_host(core_if);
		}
		break;

	case USB_MODE_FORCE_DEVICE:
		if (USB_MODE_FORCE_HOST == core_if->usb_mode) {
			core_if->usb_mode = new_mode;
			dwc_otg_force_device(core_if);
		} else if (USB_MODE_NORMAL == core_if->usb_mode) {
			core_if->usb_mode = new_mode;
			if (dwc_otg_is_device_mode(core_if))
				dwc_otg_set_force_mode(core_if, new_mode);
			else
				dwc_otg_force_device(core_if);
		}
		break;

	case USB_MODE_NORMAL:
		if (USB_MODE_FORCE_DEVICE == core_if->usb_mode) {
			core_if->usb_mode = new_mode;
			cancel_delayed_work(&otg_dev->pcd->check_vbus_work);
			dwc_otg_set_force_mode(core_if, new_mode);
			/* msleep(100); */
			if (dwc_otg_is_host_mode(core_if)) {
				dwc_otg_force_host(core_if);
			} else {
				dwc_otg_pcd_start_check_vbus_work(otg_dev->pcd);
			}
		} else if (USB_MODE_FORCE_HOST == core_if->usb_mode) {
			core_if->usb_mode = new_mode;
			dwc_otg_set_force_mode(core_if, new_mode);
			/* msleep(100); */
			if (dwc_otg_is_device_mode(core_if)) {
				dwc_otg_force_device(core_if);
			}
		}
		break;

	default:
		break;
	}
	return count;
}

static DRIVER_ATTR(force_usb_mode, S_IRUGO | S_IWUSR, force_usb_mode_show,
		   force_usb_mode_store);

static ssize_t dwc_otg_conn_en_show(struct device_driver *_drv, char *_buf)
{

	dwc_otg_device_t *otg_dev = g_otgdev;
	dwc_otg_pcd_t *_pcd = otg_dev->pcd;
	return sprintf(_buf, "%d\n", _pcd->conn_en);

}

static ssize_t dwc_otg_conn_en_store(struct device_driver *_drv,
				     const char *_buf, size_t _count)
{
	int enable = simple_strtoul(_buf, NULL, 10);
	dwc_otg_device_t *otg_dev = g_otgdev;
	dwc_otg_pcd_t *_pcd = otg_dev->pcd;
	DWC_PRINTF("%s %d->%d\n", __func__, _pcd->conn_en, enable);

	_pcd->conn_en = enable;
	return _count;
}

static DRIVER_ATTR(dwc_otg_conn_en, S_IRUGO | S_IWUSR, dwc_otg_conn_en_show,
		   dwc_otg_conn_en_store);

/* used for product vbus power control, SDK not need.
 * If dwc_otg is host mode, enable vbus power.
 * If dwc_otg is device mode, disable vbus power.
 * return 1 - host mode, 0 - device mode.
 */
int dwc_otg_usb_state(void)
{
	dwc_otg_device_t *otg_dev = g_otgdev;

	if (otg_dev) {
		/* op_state is A_HOST */
		if (1 == otg_dev->core_if->op_state)
			return 1;
		/* op_state is B_PERIPHERAL */
		else if (4 == otg_dev->core_if->op_state)
			return 0;
		else
			return 0;
	} else {
		DWC_WARN("g_otgdev is NULL, maybe otg probe is failed!\n");
		return 0;
	}
}
EXPORT_SYMBOL(dwc_otg_usb_state);

static ssize_t dwc_otg_op_state_show(struct device_driver *_drv, char *_buf)
{
	dwc_otg_device_t *otg_dev = g_otgdev;

	if (otg_dev) {
		return sprintf(_buf, "%d\n", otg_dev->core_if->op_state);
	} else {
		return sprintf(_buf, "%d\n", 0);
	}
}
static DRIVER_ATTR(op_state, S_IRUGO, dwc_otg_op_state_show, NULL);

static ssize_t vbus_status_show(struct device_driver *_drv, char *_buf)
{
	dwc_otg_device_t *otg_dev = g_otgdev;
	dwc_otg_pcd_t *_pcd = otg_dev->pcd;
	return sprintf(_buf, "%d\n", _pcd->vbus_status);
}

static DRIVER_ATTR(vbus_status, S_IRUGO, vbus_status_show, NULL);

/**
 * This function is called during module intialization
 * to pass module parameters to the DWC_OTG CORE.
 */
static int set_parameters(dwc_otg_core_if_t *core_if,
			  struct dwc_otg_driver_module_params module_params)
{
	int retval = 0;
	int i;

	if (module_params.otg_cap != -1) {
		retval +=
		    dwc_otg_set_param_otg_cap(core_if, module_params.otg_cap);
	}
	if (module_params.dma_enable != -1) {
		retval +=
		    dwc_otg_set_param_dma_enable(core_if,
						 module_params.dma_enable);
	}
	if (module_params.dma_desc_enable != -1) {
		retval +=
		    dwc_otg_set_param_dma_desc_enable(core_if,
						      module_params.dma_desc_enable);
	}
	if (module_params.opt != -1) {
		retval += dwc_otg_set_param_opt(core_if, module_params.opt);
	}
	if (module_params.dma_burst_size != -1) {
		retval +=
		    dwc_otg_set_param_dma_burst_size(core_if,
						     module_params.dma_burst_size);
	}
	if (module_params.host_support_fs_ls_low_power != -1) {
		retval +=
		    dwc_otg_set_param_host_support_fs_ls_low_power(core_if,
								   module_params.host_support_fs_ls_low_power);
	}
	if (module_params.enable_dynamic_fifo != -1) {
		retval +=
		    dwc_otg_set_param_enable_dynamic_fifo(core_if,
							  module_params.enable_dynamic_fifo);
	}
	if (module_params.data_fifo_size != -1) {
		retval +=
		    dwc_otg_set_param_data_fifo_size(core_if,
						     module_params.data_fifo_size);
	}
	if (module_params.dev_rx_fifo_size != -1) {
		retval +=
		    dwc_otg_set_param_dev_rx_fifo_size(core_if,
						       module_params.dev_rx_fifo_size);
	}
	if (module_params.dev_nperio_tx_fifo_size != -1) {
		retval +=
		    dwc_otg_set_param_dev_nperio_tx_fifo_size(core_if,
							      module_params.dev_nperio_tx_fifo_size);
	}
	if (module_params.host_rx_fifo_size != -1) {
		retval +=
		    dwc_otg_set_param_host_rx_fifo_size(core_if,
							module_params.
							host_rx_fifo_size);
	}
	if (module_params.host_nperio_tx_fifo_size != -1) {
		retval +=
		    dwc_otg_set_param_host_nperio_tx_fifo_size(core_if,
							       module_params.host_nperio_tx_fifo_size);
	}
	if (module_params.host_perio_tx_fifo_size != -1) {
		retval +=
		    dwc_otg_set_param_host_perio_tx_fifo_size(core_if,
							      module_params.host_perio_tx_fifo_size);
	}
	if (module_params.max_transfer_size != -1) {
		retval +=
		    dwc_otg_set_param_max_transfer_size(core_if,
							module_params.max_transfer_size);
	}
	if (module_params.max_packet_count != -1) {
		retval +=
		    dwc_otg_set_param_max_packet_count(core_if,
						       module_params.max_packet_count);
	}
	if (module_params.host_channels != -1) {
		retval +=
		    dwc_otg_set_param_host_channels(core_if,
						    module_params.host_channels);
	}
	if (module_params.dev_endpoints != -1) {
		retval +=
		    dwc_otg_set_param_dev_endpoints(core_if,
						    module_params.dev_endpoints);
	}
	if (module_params.phy_type != -1) {
		retval +=
		    dwc_otg_set_param_phy_type(core_if, module_params.phy_type);
	}
	if (module_params.speed != -1) {
		retval += dwc_otg_set_param_speed(core_if, module_params.speed);
	}
	if (module_params.host_ls_low_power_phy_clk != -1) {
		retval +=
		    dwc_otg_set_param_host_ls_low_power_phy_clk(core_if,
								module_params.host_ls_low_power_phy_clk);
	}
	if (module_params.phy_ulpi_ddr != -1) {
		retval +=
		    dwc_otg_set_param_phy_ulpi_ddr(core_if,
						   module_params.phy_ulpi_ddr);
	}
	if (module_params.phy_ulpi_ext_vbus != -1) {
		retval +=
		    dwc_otg_set_param_phy_ulpi_ext_vbus(core_if,
							module_params.phy_ulpi_ext_vbus);
	}
	if (module_params.phy_utmi_width != -1) {
		retval +=
		    dwc_otg_set_param_phy_utmi_width(core_if,
						     module_params.phy_utmi_width);
	}
	if (module_params.ulpi_fs_ls != -1) {
		retval +=
		    dwc_otg_set_param_ulpi_fs_ls(core_if,
						 module_params.ulpi_fs_ls);
	}
	if (module_params.ts_dline != -1) {
		retval +=
		    dwc_otg_set_param_ts_dline(core_if, module_params.ts_dline);
	}
	if (module_params.i2c_enable != -1) {
		retval +=
		    dwc_otg_set_param_i2c_enable(core_if,
						 module_params.i2c_enable);
	}
	if (module_params.en_multiple_tx_fifo != -1) {
		retval +=
		    dwc_otg_set_param_en_multiple_tx_fifo(core_if,
							  module_params.en_multiple_tx_fifo);
	}
	for (i = 0; i < 15; i++) {
		if (module_params.dev_perio_tx_fifo_size[i] != -1) {
			retval +=
			    dwc_otg_set_param_dev_perio_tx_fifo_size(core_if,
								     module_params.dev_perio_tx_fifo_size
								     [i], i);
		}
	}

	for (i = 0; i < 15; i++) {
		if (module_params.dev_tx_fifo_size[i] != -1) {
			retval += dwc_otg_set_param_dev_tx_fifo_size(core_if,
								     module_params.dev_tx_fifo_size
								     [i], i);
		}
	}
	if (module_params.thr_ctl != -1) {
		retval +=
		    dwc_otg_set_param_thr_ctl(core_if, module_params.thr_ctl);
	}
	if (module_params.mpi_enable != -1) {
		retval +=
		    dwc_otg_set_param_mpi_enable(core_if,
						 module_params.mpi_enable);
	}
	if (module_params.pti_enable != -1) {
		retval +=
		    dwc_otg_set_param_pti_enable(core_if,
						 module_params.pti_enable);
	}
	if (module_params.lpm_enable != -1) {
		retval +=
		    dwc_otg_set_param_lpm_enable(core_if,
						 module_params.lpm_enable);
	}
	if (module_params.besl_enable != -1) {
		retval +=
		    dwc_otg_set_param_besl_enable(core_if,
						  module_params.besl_enable);
	}
	if (module_params.baseline_besl != -1) {
		retval +=
		    dwc_otg_set_param_baseline_besl(core_if,
						    module_params.baseline_besl);
	}
	if (module_params.deep_besl != -1) {
		retval +=
		    dwc_otg_set_param_deep_besl(core_if,
						module_params.deep_besl);
	}
	if (module_params.ic_usb_cap != -1) {
		retval +=
		    dwc_otg_set_param_ic_usb_cap(core_if,
						 module_params.ic_usb_cap);
	}
	if (module_params.tx_thr_length != -1) {
		retval +=
		    dwc_otg_set_param_tx_thr_length(core_if,
						    module_params.
						    tx_thr_length);
	}
	if (module_params.rx_thr_length != -1) {
		retval +=
		    dwc_otg_set_param_rx_thr_length(core_if,
						    module_params.rx_thr_length);
	}
	if (module_params.ahb_thr_ratio != -1) {
		retval +=
		    dwc_otg_set_param_ahb_thr_ratio(core_if,
						    module_params.
						    ahb_thr_ratio);
	}
	if (module_params.power_down != -1) {
		retval +=
		    dwc_otg_set_param_power_down(core_if,
						 module_params.power_down);
	}
	if (module_params.reload_ctl != -1) {
		retval +=
		    dwc_otg_set_param_reload_ctl(core_if,
						 module_params.reload_ctl);
	}

	if (module_params.dev_out_nak != -1) {
		retval +=
		    dwc_otg_set_param_dev_out_nak(core_if,
						  module_params.dev_out_nak);
	}

	if (module_params.cont_on_bna != -1) {
		retval +=
		    dwc_otg_set_param_cont_on_bna(core_if,
						  module_params.cont_on_bna);
	}

	if (module_params.ahb_single != -1) {
		retval +=
		    dwc_otg_set_param_ahb_single(core_if,
						 module_params.ahb_single);
	}

	if (module_params.otg_ver != -1) {
		retval +=
		    dwc_otg_set_param_otg_ver(core_if, module_params.otg_ver);
	}
	if (module_params.adp_enable != -1) {
		retval +=
		    dwc_otg_set_param_adp_enable(core_if,
						 module_params.adp_enable);
	}
	return retval;
}

/**
 * This function is the top level interrupt handler for the Common
 * (Device and host modes) interrupts.
 */
static irqreturn_t dwc_otg_common_irq(int irq, void *dev)
{
	int32_t retval = IRQ_NONE;

	retval = dwc_otg_handle_common_intr(dev);
	if (retval != 0) {
		/* S3C2410X_CLEAR_EINTPEND(); */
	}
	return IRQ_RETVAL(retval);
}

#ifdef CONFIG_USB20_HOST
/**
 * This function is called when a lm_device is unregistered with the
 * dwc_otg_driver. This happens, for example, when the rmmod command is
 * executed. The device may or may not be electrically present. If it is
 * present, the driver stops device processing. Any resources used on behalf
 * of this device are freed.
 *
 * @param _dev
 */
static int host20_driver_remove(struct platform_device *_dev)
{

	dwc_otg_device_t *otg_dev = dwc_get_device_platform_data(_dev);
	DWC_DEBUGPL(DBG_ANY, "%s(%p)\n", __func__, _dev);

	if (!otg_dev) {
		/* Memory allocation for the dwc_otg_device failed. */
		DWC_DEBUGPL(DBG_ANY, "%s: otg_dev NULL!\n", __func__);
		return 0;
	}
#ifndef DWC_DEVICE_ONLY
	if (otg_dev->hcd) {
		hcd_remove(_dev);
	} else {
		DWC_DEBUGPL(DBG_ANY, "%s: otg_dev->hcd NULL!\n", __func__);
		return 0;
	}
#endif

#ifndef DWC_HOST_ONLY
	if (otg_dev->pcd) {
		pcd_remove(_dev);
	} else {
		DWC_DEBUGPL(DBG_ANY, "%s: otg_dev->pcd NULL!\n", __func__);
		return 0;
	}
#endif

	/*
	 * Free the IRQ
	 */
	if (otg_dev->common_irq_installed) {
		/* free_irq(_dev->irq, otg_dev); */
		free_irq(platform_get_irq(_dev, 0), otg_dev);
	} else {
		DWC_DEBUGPL(DBG_ANY, "%s: There is no installed irq!\n",
			    __func__);
		return 0;
	}

	if (otg_dev->core_if) {
		dwc_otg_cil_remove(otg_dev->core_if);
	} else {
		DWC_DEBUGPL(DBG_ANY, "%s: otg_dev->core_if NULL!\n", __func__);
		return 0;
	}

	/*
	 * Remove the device attributes
	 */
	dwc_otg_attr_remove(_dev);

	/*
	 * Return the memory.
	 */
	if (otg_dev->os_dep.base) {
		iounmap(otg_dev->os_dep.base);
	}
	DWC_FREE(otg_dev);

	/*
	 * Clear the drvdata pointer.
	 */

	dwc_set_device_platform_data(_dev, 0);

	return 0;
}

static const struct of_device_id usb20_host_of_match[] = {
	{
	 .compatible = "rockchip,rk3188_usb20_host",
	 .data = &usb20host_pdata[RK3188_USB_CTLR],
	 },
	{
	 .compatible = "rockchip,rk3288_usb20_host",
	 .data = &usb20host_pdata[RK3288_USB_CTLR],
	 },
	{
	 .compatible = "rockchip,rk3036_usb20_host",
	 .data = &usb20host_pdata[RK3036_USB_CTLR],
	 },
	{},
};

MODULE_DEVICE_TABLE(of, usb20_host_of_match);

/**
 * This function is called when an lm_device is bound to a
 * dwc_otg_driver. It creates the driver components required to
 * control the device (CIL, HCD, and PCD) and it initializes the
 * device. The driver components are stored in a dwc_otg_device
 * structure. A reference to the dwc_otg_device is saved in the
 * lm_device. This allows the driver to access the dwc_otg_device
 * structure on subsequent calls to driver methods for this device.
 *
 * @param _dev Bus device
 */
static int host20_driver_probe(struct platform_device *_dev)
{
	int retval = 0;
	int irq;
	struct resource *res_base;
	dwc_otg_device_t *dwc_otg_device;
	struct device *dev = &_dev->dev;
	struct device_node *node = _dev->dev.of_node;
	struct dwc_otg_platform_data *pldata;
	struct usb20host_pdata_id *p;
	const struct of_device_id *match =
	    of_match_device(of_match_ptr(usb20_host_of_match), &_dev->dev);

	if (match) {
		p = (struct usb20host_pdata_id *)match->data;
	} else {
		dev_err(dev, "usb20host match failed\n");
		return -EINVAL;
	}

	dev->platform_data = p->pdata;
	pldata = dev->platform_data;
	pldata->dev = dev;

	if (!node) {
		dev_err(dev, "device node not found\n");
		return -EINVAL;
	}

	if (pldata->hw_init)
		pldata->hw_init();

	if (pldata->clock_init) {
		pldata->clock_init(pldata);
		pldata->clock_enable(pldata, 1);
	}

	if (pldata->phy_suspend)
		pldata->phy_suspend(pldata, USB_PHY_ENABLED);

	if (pldata->soft_reset)
		pldata->soft_reset();

	res_base = platform_get_resource(_dev, IORESOURCE_MEM, 0);

	dwc_otg_device = DWC_ALLOC(sizeof(dwc_otg_device_t));

	if (!dwc_otg_device) {
		dev_err(&_dev->dev, "kmalloc of dwc_otg_device failed\n");
		retval = -ENOMEM;
		goto clk_disable;
	}

	memset(dwc_otg_device, 0, sizeof(*dwc_otg_device));
	dwc_otg_device->os_dep.reg_offset = 0xFFFFFFFF;

	/*
	 * Map the DWC_otg Core memory into virtual address space.
	 */

	dwc_otg_device->os_dep.base = devm_ioremap_resource(dev, res_base);

	if (!dwc_otg_device->os_dep.base) {
		dev_err(&_dev->dev, "ioremap() failed\n");
		DWC_FREE(dwc_otg_device);
		retval = -ENOMEM;
		goto clk_disable;
	}
	dev_dbg(&_dev->dev, "base=0x%08x\n",
		(unsigned)dwc_otg_device->os_dep.base);

	/*
	 * Initialize driver data to point to the global DWC_otg
	 * Device structure.
	 */

	dwc_set_device_platform_data(_dev, dwc_otg_device);
	pldata->privdata = dwc_otg_device;
	dwc_otg_device->pldata = (void *)pldata;

	dev_dbg(&_dev->dev, "dwc_otg_device=0x%p\n", dwc_otg_device);

	dwc_otg_device->core_if = dwc_otg_cil_init(dwc_otg_device->os_dep.base);

	if (!dwc_otg_device->core_if) {
		dev_err(&_dev->dev, "CIL initialization failed!\n");
		retval = -ENOMEM;
		goto fail;
	}

	dwc_otg_device->core_if->otg_dev = dwc_otg_device;

	/*
	 * Attempt to ensure this device is really a DWC_otg Controller.
	 * Read and verify the SNPSID register contents. The value should be
	 * 0x45F42XXX or 0x45F42XXX, which corresponds to either "OT2" or "OTG3",
	 * as in "OTG version 2.XX" or "OTG version 3.XX".
	 */

	if (((dwc_otg_get_gsnpsid(dwc_otg_device->core_if) & 0xFFFFF000) !=
	     0x4F542000)
	    && ((dwc_otg_get_gsnpsid(dwc_otg_device->core_if) & 0xFFFFF000) !=
		0x4F543000)) {
		dev_err(&_dev->dev, "Bad value for SNPSID: 0x%08x\n",
			dwc_otg_get_gsnpsid(dwc_otg_device->core_if));
		retval = -EINVAL;
		goto fail;
	}

	/*
	 * Validate parameter values.
	 */
	if (set_parameters(dwc_otg_device->core_if, dwc_host_module_params)) {
		retval = -EINVAL;
		goto fail;
	}

	/*
	 * Create Device Attributes in sysfs
	 */
	dwc_otg_attr_create(_dev);

	/*
	 * Disable the global interrupt until all the interrupt
	 * handlers are installed.
	 */
	dwc_otg_disable_global_interrupts(dwc_otg_device->core_if);

	/*
	 * Install the interrupt handler for the common interrupts before
	 * enabling common interrupts in core_init below.
	 */
	irq = platform_get_irq(_dev, 0);
	DWC_DEBUGPL(DBG_CIL, "registering (common) handler for irq%d\n", irq);
	retval = request_irq(irq, dwc_otg_common_irq,
			     IRQF_SHARED, "dwc_otg", dwc_otg_device);
	if (retval) {
		DWC_ERROR("request of irq%d failed\n", irq);
		retval = -EBUSY;
		goto fail;
	} else {
		dwc_otg_device->common_irq_installed = 1;
	}

	/*
	 * Initialize the DWC_otg core.
	 * In order to reduce the time of initialization,
	 * we do core soft reset after connection detected.
	 */
	dwc_otg_core_init_no_reset(dwc_otg_device->core_if);

	/*
	 * Initialize the HCD
	 */
	retval = host20_hcd_init(_dev);
	if (retval != 0) {
		DWC_ERROR("hcd_init failed\n");
		dwc_otg_device->hcd = NULL;
		goto fail;
	}

	clk_set_rate(pldata->phyclk_480m, 480000000);
	/*
	 * Enable the global interrupt after all the interrupt
	 * handlers are installed if there is no ADP support else
	 * perform initial actions required for Internal ADP logic.
	 */
	if (!dwc_otg_get_param_adp_enable(dwc_otg_device->core_if)) {
		if (pldata->phy_status == USB_PHY_ENABLED) {
			pldata->phy_suspend(pldata, USB_PHY_SUSPEND);
			udelay(3);
			pldata->clock_enable(pldata, 0);
		}
		/* dwc_otg_enable_global_interrupts(dwc_otg_device->core_if); */
	} else
		dwc_otg_adp_start(dwc_otg_device->core_if,
				  dwc_otg_is_host_mode(dwc_otg_device->
						       core_if));

	return 0;

fail:
	host20_driver_remove(_dev);
clk_disable:
	if (pldata->clock_enable)
		pldata->clock_enable(pldata, 0);

	return retval;
}
#endif

static int dwc_otg_driver_suspend(struct platform_device *_dev,
				  pm_message_t state)
{
	return 0;
}

static int dwc_otg_driver_resume(struct platform_device *_dev)
{
	return 0;
}

static void dwc_otg_driver_shutdown(struct platform_device *_dev)
{
	struct device *dev = &_dev->dev;
	dwc_otg_device_t *otg_dev = dev->platform_data;
	dwc_otg_core_if_t *core_if = otg_dev->core_if;
	dctl_data_t dctl = {.d32 = 0 };

	DWC_PRINTF("%s: disconnect USB %s mode\n", __func__,
		   dwc_otg_is_host_mode(core_if) ? "host" : "device");
	if (dwc_otg_is_host_mode(core_if)) {
		if (core_if->hcd_cb && core_if->hcd_cb->stop)
			core_if->hcd_cb->stop(core_if->hcd_cb_p);
	} else {
		/* soft disconnect */
		dctl.d32 =
		    DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dctl);
		dctl.b.sftdiscon = 1;
		DWC_WRITE_REG32(&core_if->dev_if->dev_global_regs->dctl,
				dctl.d32);
	}
	/* Clear any pending interrupts */
	DWC_WRITE_REG32(&core_if->core_global_regs->gintsts, 0xFFFFFFFF);

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
#ifdef CONFIG_USB20_HOST
static struct platform_driver dwc_host_driver = {
	.driver = {
		   .name = (char *)dwc_host20_driver_name,
		   .of_match_table = of_match_ptr(usb20_host_of_match),
		   },
	.probe = host20_driver_probe,
	.remove = host20_driver_remove,
	.suspend = dwc_otg_driver_suspend,
	.resume = dwc_otg_driver_resume,
};
#endif

#ifdef CONFIG_USB20_OTG
/**
 * This function is called when a lm_device is unregistered with the
 * dwc_otg_driver. This happens, for example, when the rmmod command is
 * executed. The device may or may not be electrically present. If it is
 * present, the driver stops device processing. Any resources used on behalf
 * of this device are freed.
 *
 * @param _dev
 */
static int otg20_driver_remove(struct platform_device *_dev)
{

	dwc_otg_device_t *otg_dev = dwc_get_device_platform_data(_dev);
	DWC_DEBUGPL(DBG_ANY, "%s(%p)\n", __func__, _dev);

	if (!otg_dev) {
		/* Memory allocation for the dwc_otg_device failed. */
		DWC_DEBUGPL(DBG_ANY, "%s: otg_dev NULL!\n", __func__);
		return 0;
	}
#ifndef DWC_DEVICE_ONLY
	if (otg_dev->hcd) {
		hcd_remove(_dev);
	} else {
		DWC_DEBUGPL(DBG_ANY, "%s: otg_dev->hcd NULL!\n", __func__);
		return 0;
	}
#endif

#ifndef DWC_HOST_ONLY
	if (otg_dev->pcd) {
		pcd_remove(_dev);
	} else {
		DWC_DEBUGPL(DBG_ANY, "%s: otg_dev->pcd NULL!\n", __func__);
		return 0;
	}
#endif
	/*
	 * Free the IRQ
	 */
	if (otg_dev->common_irq_installed) {
		/* free_irq(_dev->irq, otg_dev); */
		free_irq(platform_get_irq(_dev, 0), otg_dev);
	} else {
		DWC_DEBUGPL(DBG_ANY, "%s: There is no installed irq!\n",
			    __func__);
		return 0;
	}

	if (otg_dev->core_if) {
		dwc_otg_cil_remove(otg_dev->core_if);
	} else {
		DWC_DEBUGPL(DBG_ANY, "%s: otg_dev->core_if NULL!\n", __func__);
		return 0;
	}

	/*
	 * Remove the device attributes
	 */
	dwc_otg_attr_remove(_dev);

	/*
	 * Return the memory.
	 */
	if (otg_dev->os_dep.base)
		iounmap(otg_dev->os_dep.base);
	DWC_FREE(otg_dev);

	/*
	 * Clear the drvdata pointer.
	 */

	dwc_set_device_platform_data(_dev, 0);

	return 0;
}

static const struct of_device_id usb20_otg_of_match[] = {
	{
	 .compatible = "rockchip,rk3188_usb20_otg",
	 .data = &usb20otg_pdata[RK3188_USB_CTLR],
	 },
	{
	 .compatible = "rockchip,rk3288_usb20_otg",
	 .data = &usb20otg_pdata[RK3288_USB_CTLR],
	 },
	{
	 .compatible = "rockchip,rk3036_usb20_otg",
	 .data = &usb20otg_pdata[RK3036_USB_CTLR],
	 },
	{
	 },
};

MODULE_DEVICE_TABLE(of, usb20_otg_of_match);

/**
 * This function is called when an lm_device is bound to a
 * dwc_otg_driver. It creates the driver components required to
 * control the device (CIL, HCD, and PCD) and it initializes the
 * device. The driver components are stored in a dwc_otg_device
 * structure. A reference to the dwc_otg_device is saved in the
 * lm_device. This allows the driver to access the dwc_otg_device
 * structure on subsequent calls to driver methods for this device.
 *
 * @param _dev Bus device
 */
static int otg20_driver_probe(struct platform_device *_dev)
{
	int retval = 0;
	int irq;
	uint32_t val;
	struct resource *res_base;
	dwc_otg_device_t *dwc_otg_device;
	struct device *dev = &_dev->dev;
	struct device_node *node = _dev->dev.of_node;
	struct dwc_otg_platform_data *pldata;
	struct usb20otg_pdata_id *p;
	const struct of_device_id *match =
	    of_match_device(of_match_ptr(usb20_otg_of_match), &_dev->dev);

	if (match) {
		p = (struct usb20otg_pdata_id *)match->data;
	} else {
		dev_err(dev, "usb20otg match failed\n");
		return -EINVAL;
	}

	dev->platform_data = p->pdata;
	/* dev->platform_data = &usb20otg_pdata; */
	pldata = dev->platform_data;
	pldata->dev = dev;

	if (!node) {
		dev_err(dev, "device node not found\n");
		return -EINVAL;
	}
	/*todo : move to usbdev_rk-XX.c */
	if (pldata->hw_init)
		pldata->hw_init();

	if (pldata->clock_init) {
		pldata->clock_init(pldata);
		pldata->clock_enable(pldata, 1);
	}

	if (pldata->phy_suspend)
		pldata->phy_suspend(pldata, USB_PHY_ENABLED);

	if (pldata->dwc_otg_uart_mode)
		pldata->dwc_otg_uart_mode(pldata, PHY_USB_MODE);

	/* do reset later, because reset need about
	 * 100ms to ensure otg id state change.
	 */
	/*
	   if(pldata->soft_reset)
	   pldata->soft_reset();
	 */
	/*end todo */

	res_base = platform_get_resource(_dev, IORESOURCE_MEM, 0);

	dwc_otg_device = DWC_ALLOC(sizeof(dwc_otg_device_t));

	if (!dwc_otg_device) {
		dev_err(&_dev->dev, "kmalloc of dwc_otg_device failed\n");
		retval = -ENOMEM;
		goto clk_disable;
	}

	memset(dwc_otg_device, 0, sizeof(*dwc_otg_device));
	dwc_otg_device->os_dep.reg_offset = 0xFFFFFFFF;

	/*
	 * Map the DWC_otg Core memory into virtual address space.
	 */

	dwc_otg_device->os_dep.base = devm_ioremap_resource(dev, res_base);

	if (!dwc_otg_device->os_dep.base) {
		dev_err(&_dev->dev, "ioremap() failed\n");
		DWC_FREE(dwc_otg_device);
		retval = -ENOMEM;
		goto clk_disable;
	}
	dev_dbg(&_dev->dev, "base=0x%08x\n",
		(unsigned)dwc_otg_device->os_dep.base);

	/*
	 * Initialize driver data to point to the global DWC_otg
	 * Device structure.
	 */

	g_otgdev = dwc_otg_device;
	pldata->privdata = dwc_otg_device;
	dwc_otg_device->pldata = pldata;

	dwc_set_device_platform_data(_dev, dwc_otg_device);

	dev_dbg(&_dev->dev, "dwc_otg_device=0x%p\n", dwc_otg_device);

	dwc_otg_device->core_if = dwc_otg_cil_init(dwc_otg_device->os_dep.base);
	if (!dwc_otg_device->core_if) {
		dev_err(&_dev->dev, "CIL initialization failed!\n");
		retval = -ENOMEM;
		goto fail;
	}

	dwc_otg_device->core_if->otg_dev = dwc_otg_device;
	/*
	 * Attempt to ensure this device is really a DWC_otg Controller.
	 * Read and verify the SNPSID register contents. The value should be
	 * 0x45F42XXX or 0x45F42XXX, which corresponds to either "OT2" or "OTG3",
	 * as in "OTG version 2.XX" or "OTG version 3.XX".
	 */

	if (((dwc_otg_get_gsnpsid(dwc_otg_device->core_if) & 0xFFFFF000) !=
	     0x4F542000)
	    && ((dwc_otg_get_gsnpsid(dwc_otg_device->core_if) & 0xFFFFF000) !=
		0x4F543000)) {
		dev_err(&_dev->dev, "Bad value for SNPSID: 0x%08x\n",
			dwc_otg_get_gsnpsid(dwc_otg_device->core_if));
		retval = -EINVAL;
		goto fail;
	}

	/*
	 * Validate parameter values.
	 */
	if (set_parameters(dwc_otg_device->core_if, dwc_otg_module_params)) {
		retval = -EINVAL;
		goto fail;
	}

	/*
	 * Create Device Attributes in sysfs
	 */
	dwc_otg_attr_create(_dev);

	/*
	 * Disable the global interrupt until all the interrupt
	 * handlers are installed.
	 */
	dwc_otg_disable_global_interrupts(dwc_otg_device->core_if);

	/*
	 * Install the interrupt handler for the common interrupts before
	 * enabling common interrupts in core_init below.
	 */
	irq = platform_get_irq(_dev, 0);
	DWC_DEBUGPL(DBG_CIL, "registering (common) handler for irq%d\n", irq);
	retval = request_irq(irq, dwc_otg_common_irq,
			     IRQF_SHARED, "dwc_otg", dwc_otg_device);
	if (retval) {
		DWC_ERROR("request of irq%d failed\n", irq);
		retval = -EBUSY;
		goto fail;
	} else {
		dwc_otg_device->common_irq_installed = 1;
	}

	/*
	 * Initialize the DWC_otg core.
	 * In order to reduce the time of initialization,
	 * we do core soft reset after connection detected.
	 */
	dwc_otg_core_init_no_reset(dwc_otg_device->core_if);

	/* set otg mode
	 * 0 - USB_MODE_NORMAL
	 * 1 - USB_MODE_FORCE_HOST
	 * 2 - USB_MODE_FORCE_DEVICE
	 */
	of_property_read_u32(node, "rockchip,usb-mode", &val);
	dwc_otg_device->core_if->usb_mode = val;

#ifndef DWC_HOST_ONLY
	/*
	 * Initialize the PCD
	 */
	retval = pcd_init(_dev);
	if (retval != 0) {
		DWC_ERROR("pcd_init failed\n");
		dwc_otg_device->pcd = NULL;
		goto fail;
	}
#endif
#ifndef DWC_DEVICE_ONLY
	/*
	 * Initialize the HCD
	 */
	retval = otg20_hcd_init(_dev);
	if (retval != 0) {
		DWC_ERROR("hcd_init failed\n");
		dwc_otg_device->hcd = NULL;
		goto fail;
	}
#endif
	/*
	 * Enable the global interrupt after all the interrupt
	 * handlers are installed if there is no ADP support else
	 * perform initial actions required for Internal ADP logic.
	 */
	if (!dwc_otg_get_param_adp_enable(dwc_otg_device->core_if)) {
		if (pldata->phy_status == USB_PHY_ENABLED) {
			pldata->phy_suspend(pldata, USB_PHY_SUSPEND);
			udelay(3);
			pldata->clock_enable(pldata, 0);
		}
		/* dwc_otg_enable_global_interrupts(dwc_otg_device->core_if); */
	} else
		dwc_otg_adp_start(dwc_otg_device->core_if,
				  dwc_otg_is_host_mode(dwc_otg_device->
						       core_if));

	return 0;

fail:
	otg20_driver_remove(_dev);

clk_disable:
	if (pldata->clock_enable)
		pldata->clock_enable(pldata, 0);

	return retval;
}

static struct platform_driver dwc_otg_driver = {
	.driver = {
		   .name = (char *)dwc_otg20_driver_name,
		   .of_match_table = of_match_ptr(usb20_otg_of_match),
		   },
	.probe = otg20_driver_probe,
	.remove = otg20_driver_remove,
	.suspend = dwc_otg_driver_suspend,
	.resume = dwc_otg_driver_resume,
	.shutdown = dwc_otg_driver_shutdown,
};
#endif

void rk_usb_power_up(void)
{
	struct dwc_otg_platform_data *pldata_otg;
	struct dwc_otg_platform_data *pldata_host;
	struct rkehci_platform_data *pldata_ehci;

	if (cpu_is_rk3288()) {
#ifdef CONFIG_RK_USB_UART
		/* enable USB bypass UART function  */
		writel_relaxed(0x00c00000 | usb_to_uart_status,
			       RK_GRF_VIRT + RK3288_GRF_UOC0_CON3);

#endif
		/* unset siddq,the analog blocks are powered up */
#ifdef CONFIG_USB20_OTG
		pldata_otg = &usb20otg_pdata_rk3288;
		if (pldata_otg) {
			if (pldata_otg->phy_status == USB_PHY_SUSPEND)
				writel_relaxed((0x01 << 13) << 16,
					       RK_GRF_VIRT +
					       RK3288_GRF_UOC0_CON0);
		}
#endif
#ifdef CONFIG_USB20_HOST
		pldata_host = &usb20host_pdata_rk3288;
		if (pldata_host) {
			if (pldata_host->phy_status == USB_PHY_SUSPEND)
				writel_relaxed((0x01 << 13) << 16,
					       RK_GRF_VIRT +
					       RK3288_GRF_UOC2_CON0);
		}
#endif
#ifdef CONFIG_USB_EHCI_RK
		pldata_ehci = &rkehci_pdata_rk3288;
		if (pldata_ehci) {
			if (pldata_ehci->phy_status == USB_PHY_SUSPEND)
				writel_relaxed((0x01 << 13) << 16,
					       RK_GRF_VIRT +
					       RK3288_GRF_UOC1_CON0);
		}
#endif

	}
}

void rk_usb_power_down(void)
{
	struct dwc_otg_platform_data *pldata_otg;
	struct dwc_otg_platform_data *pldata_host;
	struct rkehci_platform_data *pldata_ehci;

	if (cpu_is_rk3288()) {
#ifdef CONFIG_RK_USB_UART
		/* disable USB bypass UART function */
		usb_to_uart_status =
		    readl_relaxed(RK_GRF_VIRT + RK3288_GRF_UOC0_CON3);
		writel_relaxed(0x00c00000, RK_GRF_VIRT + RK3288_GRF_UOC0_CON3);
#endif
		/* set siddq,the analog blocks are powered down
		 * note:
		 * 1. Before asserting SIDDQ, ensure that VDATSRCENB0,
		 * VDATDETENB0, DCDENB0, BYPASSSEL0, ADPPRBENB0,
		 * and TESTBURNIN are set to 1'b0.
		 * 2. Before asserting SIDDQ, ensure that phy enter suspend.*/
#ifdef CONFIG_USB20_OTG
		pldata_otg = &usb20otg_pdata_rk3288;
		if (pldata_otg) {
			if (pldata_otg->phy_status == USB_PHY_SUSPEND)
				writel_relaxed((0x01 << 13) |
					       ((0x01 << 13) << 16),
					       RK_GRF_VIRT +
					       RK3288_GRF_UOC0_CON0);
		}
#endif
#ifdef CONFIG_USB20_HOST
		pldata_host = &usb20host_pdata_rk3288;
		if (pldata_host) {
			if (pldata_host->phy_status == USB_PHY_SUSPEND)
				writel_relaxed((0x01 << 13) |
					       ((0x01 << 13) << 16),
					       RK_GRF_VIRT +
					       RK3288_GRF_UOC2_CON0);
		}
#endif
#ifdef CONFIG_USB_EHCI_RK
		pldata_ehci = &rkehci_pdata_rk3288;
		if (pldata_ehci) {
			if (pldata_ehci->phy_status == USB_PHY_SUSPEND)
				writel_relaxed((0x01 << 13) |
					       ((0x01 << 13) << 16),
					       RK_GRF_VIRT +
					       RK3288_GRF_UOC1_CON0);
		}
#endif
	}
}

EXPORT_SYMBOL(rk_usb_power_up);
EXPORT_SYMBOL(rk_usb_power_down);
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
	int error;

#ifdef CONFIG_USB20_OTG
	/* register otg20 */
	printk(KERN_INFO "%s: version %s\n", dwc_otg20_driver_name,
	       DWC_DRIVER_VERSION);

	retval = platform_driver_register(&dwc_otg_driver);
	if (retval < 0) {
		printk(KERN_ERR "%s retval=%d\n", __func__, retval);
		return retval;
	}

	error =
	    driver_create_file(&dwc_otg_driver.driver, &driver_attr_version);
	error =
	    driver_create_file(&dwc_otg_driver.driver, &driver_attr_debuglevel);
	error =
	    driver_create_file(&dwc_otg_driver.driver,
			       &driver_attr_dwc_otg_conn_en);
	error =
	    driver_create_file(&dwc_otg_driver.driver,
			       &driver_attr_vbus_status);
	error =
	    driver_create_file(&dwc_otg_driver.driver,
			       &driver_attr_force_usb_mode);
	error =
	    driver_create_file(&dwc_otg_driver.driver,
			       &driver_attr_op_state);

#endif

	/* register host20 */
#ifdef CONFIG_USB20_HOST
	printk(KERN_INFO "%s: version %s\n", dwc_host20_driver_name,
	       DWC_DRIVER_VERSION);

	retval = platform_driver_register(&dwc_host_driver);
	if (retval < 0) {
		printk(KERN_ERR "%s retval=%d\n", __func__, retval);
		return retval;
	}

	error =
	    driver_create_file(&dwc_host_driver.driver, &driver_attr_version);
	error =
	    driver_create_file(&dwc_host_driver.driver,
			       &driver_attr_debuglevel);
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
	printk(KERN_DEBUG "dwc_otg_driver_cleanup()\n");

#ifdef CONFIG_USB20_HOST
	/*for host20 */
	driver_remove_file(&dwc_host_driver.driver, &driver_attr_debuglevel);
	driver_remove_file(&dwc_host_driver.driver, &driver_attr_version);
	platform_driver_unregister(&dwc_host_driver);
	printk(KERN_INFO "%s module removed\n", dwc_host20_driver_name);
#endif

#ifdef CONFIG_USB20_OTG
	/*for otg */
	driver_remove_file(&dwc_otg_driver.driver,
			   &driver_attr_dwc_otg_conn_en);
	driver_remove_file(&dwc_otg_driver.driver, &driver_attr_debuglevel);
	driver_remove_file(&dwc_otg_driver.driver, &driver_attr_version);
	driver_remove_file(&dwc_otg_driver.driver, &driver_attr_vbus_status);
	driver_remove_file(&dwc_otg_driver.driver, &driver_attr_force_usb_mode);
	driver_remove_file(&dwc_otg_driver.driver, &driver_attr_op_state);
	platform_driver_unregister(&dwc_otg_driver);
	printk(KERN_INFO "%s module removed\n", dwc_otg20_driver_name);
#endif
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

module_param_named(dma_desc_enable, dwc_otg_module_params.dma_desc_enable, int,
		   0444);
MODULE_PARM_DESC(dma_desc_enable,
		 "DMA Desc Mode 0=Address DMA 1=DMA Descriptor enabled");

module_param_named(dma_burst_size, dwc_otg_module_params.dma_burst_size, int,
		   0444);
MODULE_PARM_DESC(dma_burst_size,
		 "DMA Burst Size 1, 4, 8, 16, 32, 64, 128, 256");
module_param_named(speed, dwc_otg_module_params.speed, int, 0444);
MODULE_PARM_DESC(speed, "Speed 0=High Speed 1=Full Speed");
module_param_named(host_support_fs_ls_low_power,
		   dwc_otg_module_params.host_support_fs_ls_low_power, int,
		   0444);
MODULE_PARM_DESC(host_support_fs_ls_low_power,
		 "Support Low Power w/FS or LS 0=Support 1=Don't Support");
module_param_named(host_ls_low_power_phy_clk,
		   dwc_otg_module_params.host_ls_low_power_phy_clk, int, 0444);
MODULE_PARM_DESC(host_ls_low_power_phy_clk,
		 "Low Speed Low Power Clock 0=48Mhz 1=6Mhz");
module_param_named(enable_dynamic_fifo,
		   dwc_otg_module_params.enable_dynamic_fifo, int, 0444);
MODULE_PARM_DESC(enable_dynamic_fifo, "0=cC Setting 1=Allow Dynamic Sizing");
module_param_named(data_fifo_size, dwc_otg_module_params.data_fifo_size, int,
		   0444);
MODULE_PARM_DESC(data_fifo_size,
		 "Total number of words in the data FIFO memory 32-32768");
module_param_named(dev_rx_fifo_size, dwc_otg_module_params.dev_rx_fifo_size,
		   int, 0444);
MODULE_PARM_DESC(dev_rx_fifo_size, "Number of words in the Rx FIFO 16-32768");
module_param_named(dev_nperio_tx_fifo_size,
		   dwc_otg_module_params.dev_nperio_tx_fifo_size, int, 0444);
MODULE_PARM_DESC(dev_nperio_tx_fifo_size,
		 "Number of words in the non-periodic Tx FIFO 16-32768");
module_param_named(dev_perio_tx_fifo_size_1,
		   dwc_otg_module_params.dev_perio_tx_fifo_size[0], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_1,
		 "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_2,
		   dwc_otg_module_params.dev_perio_tx_fifo_size[1], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_2,
		 "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_3,
		   dwc_otg_module_params.dev_perio_tx_fifo_size[2], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_3,
		 "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_4,
		   dwc_otg_module_params.dev_perio_tx_fifo_size[3], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_4,
		 "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_5,
		   dwc_otg_module_params.dev_perio_tx_fifo_size[4], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_5,
		 "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_6,
		   dwc_otg_module_params.dev_perio_tx_fifo_size[5], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_6,
		 "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_7,
		   dwc_otg_module_params.dev_perio_tx_fifo_size[6], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_7,
		 "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_8,
		   dwc_otg_module_params.dev_perio_tx_fifo_size[7], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_8,
		 "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_9,
		   dwc_otg_module_params.dev_perio_tx_fifo_size[8], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_9,
		 "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_10,
		   dwc_otg_module_params.dev_perio_tx_fifo_size[9], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_10,
		 "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_11,
		   dwc_otg_module_params.dev_perio_tx_fifo_size[10], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_11,
		 "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_12,
		   dwc_otg_module_params.dev_perio_tx_fifo_size[11], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_12,
		 "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_13,
		   dwc_otg_module_params.dev_perio_tx_fifo_size[12], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_13,
		 "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_14,
		   dwc_otg_module_params.dev_perio_tx_fifo_size[13], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_14,
		 "Number of words in the periodic Tx FIFO 4-768");
module_param_named(dev_perio_tx_fifo_size_15,
		   dwc_otg_module_params.dev_perio_tx_fifo_size[14], int, 0444);
MODULE_PARM_DESC(dev_perio_tx_fifo_size_15,
		 "Number of words in the periodic Tx FIFO 4-768");
module_param_named(host_rx_fifo_size, dwc_otg_module_params.host_rx_fifo_size,
		   int, 0444);
MODULE_PARM_DESC(host_rx_fifo_size, "Number of words in the Rx FIFO 16-32768");
module_param_named(host_nperio_tx_fifo_size,
		   dwc_otg_module_params.host_nperio_tx_fifo_size, int, 0444);
MODULE_PARM_DESC(host_nperio_tx_fifo_size,
		 "Number of words in the non-periodic Tx FIFO 16-32768");
module_param_named(host_perio_tx_fifo_size,
		   dwc_otg_module_params.host_perio_tx_fifo_size, int, 0444);
MODULE_PARM_DESC(host_perio_tx_fifo_size,
		 "Number of words in the host periodic Tx FIFO 16-32768");
module_param_named(max_transfer_size, dwc_otg_module_params.max_transfer_size,
		   int, 0444);
/** @todo Set the max to 512K, modify checks */
MODULE_PARM_DESC(max_transfer_size,
		 "The maximum transfer size supported in bytes 2047-65535");
module_param_named(max_packet_count, dwc_otg_module_params.max_packet_count,
		   int, 0444);
MODULE_PARM_DESC(max_packet_count,
		 "The maximum number of packets in a transfer 15-511");
module_param_named(host_channels, dwc_otg_module_params.host_channels, int,
		   0444);
MODULE_PARM_DESC(host_channels,
		 "The number of host channel registers to use 1-16");
module_param_named(dev_endpoints, dwc_otg_module_params.dev_endpoints, int,
		   0444);
MODULE_PARM_DESC(dev_endpoints,
		 "The number of endpoints in addition to EP0 available for device mode 1-15");
module_param_named(phy_type, dwc_otg_module_params.phy_type, int, 0444);
MODULE_PARM_DESC(phy_type, "0=Reserved 1=UTMI+ 2=ULPI");
module_param_named(phy_utmi_width, dwc_otg_module_params.phy_utmi_width, int,
		   0444);
MODULE_PARM_DESC(phy_utmi_width, "Specifies the UTMI+ Data Width 8 or 16 bits");
module_param_named(phy_ulpi_ddr, dwc_otg_module_params.phy_ulpi_ddr, int, 0444);
MODULE_PARM_DESC(phy_ulpi_ddr,
		 "ULPI at double or single data rate 0=Single 1=Double");
module_param_named(phy_ulpi_ext_vbus, dwc_otg_module_params.phy_ulpi_ext_vbus,
		   int, 0444);
MODULE_PARM_DESC(phy_ulpi_ext_vbus,
		 "ULPI PHY using internal or external vbus 0=Internal");
module_param_named(i2c_enable, dwc_otg_module_params.i2c_enable, int, 0444);
MODULE_PARM_DESC(i2c_enable, "FS PHY Interface");
module_param_named(ulpi_fs_ls, dwc_otg_module_params.ulpi_fs_ls, int, 0444);
MODULE_PARM_DESC(ulpi_fs_ls, "ULPI PHY FS/LS mode only");
module_param_named(ts_dline, dwc_otg_module_params.ts_dline, int, 0444);
MODULE_PARM_DESC(ts_dline, "Term select Dline pulsing for all PHYs");
module_param_named(debug, g_dbg_lvl, int, 0444);
MODULE_PARM_DESC(debug, "");

module_param_named(en_multiple_tx_fifo,
		   dwc_otg_module_params.en_multiple_tx_fifo, int, 0444);
MODULE_PARM_DESC(en_multiple_tx_fifo,
		 "Dedicated Non Periodic Tx FIFOs 0=disabled 1=enabled");
module_param_named(dev_tx_fifo_size_1,
		   dwc_otg_module_params.dev_tx_fifo_size[0], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_1, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_2,
		   dwc_otg_module_params.dev_tx_fifo_size[1], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_2, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_3,
		   dwc_otg_module_params.dev_tx_fifo_size[2], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_3, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_4,
		   dwc_otg_module_params.dev_tx_fifo_size[3], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_4, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_5,
		   dwc_otg_module_params.dev_tx_fifo_size[4], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_5, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_6,
		   dwc_otg_module_params.dev_tx_fifo_size[5], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_6, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_7,
		   dwc_otg_module_params.dev_tx_fifo_size[6], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_7, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_8,
		   dwc_otg_module_params.dev_tx_fifo_size[7], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_8, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_9,
		   dwc_otg_module_params.dev_tx_fifo_size[8], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_9, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_10,
		   dwc_otg_module_params.dev_tx_fifo_size[9], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_10, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_11,
		   dwc_otg_module_params.dev_tx_fifo_size[10], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_11, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_12,
		   dwc_otg_module_params.dev_tx_fifo_size[11], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_12, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_13,
		   dwc_otg_module_params.dev_tx_fifo_size[12], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_13, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_14,
		   dwc_otg_module_params.dev_tx_fifo_size[13], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_14, "Number of words in the Tx FIFO 4-768");
module_param_named(dev_tx_fifo_size_15,
		   dwc_otg_module_params.dev_tx_fifo_size[14], int, 0444);
MODULE_PARM_DESC(dev_tx_fifo_size_15, "Number of words in the Tx FIFO 4-768");

module_param_named(thr_ctl, dwc_otg_module_params.thr_ctl, int, 0444);
MODULE_PARM_DESC(thr_ctl,
		 "Thresholding enable flag bit 0 - non ISO Tx thr., 1 - ISO Tx thr., 2 - Rx thr.- bit 0=disabled 1=enabled");
module_param_named(tx_thr_length, dwc_otg_module_params.tx_thr_length, int,
		   0444);
MODULE_PARM_DESC(tx_thr_length, "Tx Threshold length in 32 bit DWORDs");
module_param_named(rx_thr_length, dwc_otg_module_params.rx_thr_length, int,
		   0444);
MODULE_PARM_DESC(rx_thr_length, "Rx Threshold length in 32 bit DWORDs");

module_param_named(pti_enable, dwc_otg_module_params.pti_enable, int, 0444);
module_param_named(mpi_enable, dwc_otg_module_params.mpi_enable, int, 0444);
module_param_named(lpm_enable, dwc_otg_module_params.lpm_enable, int, 0444);
MODULE_PARM_DESC(lpm_enable, "LPM Enable 0=LPM Disabled 1=LPM Enabled");

module_param_named(besl_enable, dwc_otg_module_params.besl_enable, int, 0444);
MODULE_PARM_DESC(besl_enable, "BESL Enable 0=BESL Disabled 1=BESL Enabled");
module_param_named(baseline_besl, dwc_otg_module_params.baseline_besl, int,
		   0444);
MODULE_PARM_DESC(baseline_besl, "Set the baseline besl value");
module_param_named(deep_besl, dwc_otg_module_params.deep_besl, int, 0444);
MODULE_PARM_DESC(deep_besl, "Set the deep besl value");

module_param_named(ic_usb_cap, dwc_otg_module_params.ic_usb_cap, int, 0444);
MODULE_PARM_DESC(ic_usb_cap,
		 "IC_USB Capability 0=IC_USB Disabled 1=IC_USB Enabled");
module_param_named(ahb_thr_ratio, dwc_otg_module_params.ahb_thr_ratio, int,
		   0444);
MODULE_PARM_DESC(ahb_thr_ratio, "AHB Threshold Ratio");
module_param_named(power_down, dwc_otg_module_params.power_down, int, 0444);
MODULE_PARM_DESC(power_down, "Power Down Mode");
module_param_named(reload_ctl, dwc_otg_module_params.reload_ctl, int, 0444);
MODULE_PARM_DESC(reload_ctl, "HFIR Reload Control");
module_param_named(dev_out_nak, dwc_otg_module_params.dev_out_nak, int, 0444);
MODULE_PARM_DESC(dev_out_nak, "Enable Device OUT NAK");
module_param_named(cont_on_bna, dwc_otg_module_params.cont_on_bna, int, 0444);
MODULE_PARM_DESC(cont_on_bna, "Enable Enable Continue on BNA");
module_param_named(ahb_single, dwc_otg_module_params.ahb_single, int, 0444);
MODULE_PARM_DESC(ahb_single, "Enable AHB Single Support");
module_param_named(adp_enable, dwc_otg_module_params.adp_enable, int, 0444);
MODULE_PARM_DESC(adp_enable, "ADP Enable 0=ADP Disabled 1=ADP Enabled");
module_param_named(otg_ver, dwc_otg_module_params.otg_ver, int, 0444);
MODULE_PARM_DESC(otg_ver, "OTG revision supported 0=OTG 1.3 1=OTG 2.0");

/** @page "Module Parameters"
 *
 * The following parameters may be specified when starting the module.
 * These parameters define how the DWC_otg controller should be
 * configured. Parameter values are passed to the CIL initialization
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
 <td>ulpi_fs_ls</td>
 <td>Specifies whether to use ULPI FS/LS mode only.
 - 0: Disabled (default)
 - 1: Enabled
 </td></tr>

 <tr>
 <td>ts_dline</td>
 <td>Specifies whether term select D-Line pulsing for all PHYs is enabled.
 - 0: Disabled (default)
 - 1: Enabled
 </td></tr>

 <tr>
 <td>en_multiple_tx_fifo</td>
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

 <tr>
 <td>tx_thr_length</td>
 <td>Transmit Threshold length in 32 bit double words
 - Values: 8 to 128 (default 64)
 </td></tr>

 <tr>
 <td>rx_thr_length</td>
 <td>Receive Threshold length in 32 bit double words
 - Values: 8 to 128 (default 64)
 </td></tr>

<tr>
 <td>thr_ctl</td>
 <td>Specifies whether to enable Thresholding for Device mode. Bits 0, 1, 2 of
 this parmater specifies if thresholding is enabled for non-Iso Tx, Iso Tx and
 Rx transfers accordingly.
 The driver will automatically detect the value for this parameter if none is
 specified.
 - Values: 0 to 7 (default 0)
 Bit values indicate:
 - 0: Thresholding disabled
 - 1: Thresholding enabled
 </td></tr>

<tr>
 <td>dma_desc_enable</td>
 <td>Specifies whether to enable Descriptor DMA mode.
 The driver will automatically detect the value for this parameter if none is
 specified.
 - 0: Descriptor DMA disabled
 - 1: Descriptor DMA (default, if available)
 </td></tr>

<tr>
 <td>mpi_enable</td>
 <td>Specifies whether to enable MPI enhancement mode.
 The driver will automatically detect the value for this parameter if none is
 specified.
 - 0: MPI disabled (default)
 - 1: MPI enable
 </td></tr>

<tr>
 <td>pti_enable</td>
 <td>Specifies whether to enable PTI enhancement support.
 The driver will automatically detect the value for this parameter if none is
 specified.
 - 0: PTI disabled (default)
 - 1: PTI enable
 </td></tr>

<tr>
 <td>lpm_enable</td>
 <td>Specifies whether to enable LPM support.
 The driver will automatically detect the value for this parameter if none is
 specified.
 - 0: LPM disabled
 - 1: LPM enable (default, if available)
 </td></tr>

 <tr>
 <td>besl_enable</td>
 <td>Specifies whether to enable LPM Errata support.
 The driver will automatically detect the value for this parameter if none is
 specified.
 - 0: LPM Errata disabled (default)
 - 1: LPM Errata enable
 </td></tr>

  <tr>
 <td>baseline_besl</td>
 <td>Specifies the baseline besl value.
 - Values: 0 to 15 (default 0)
 </td></tr>

  <tr>
 <td>deep_besl</td>
 <td>Specifies the deep besl value.
 - Values: 0 to 15 (default 15)
 </td></tr>

<tr>
 <td>ic_usb_cap</td>
 <td>Specifies whether to enable IC_USB capability.
 The driver will automatically detect the value for this parameter if none is
 specified.
 - 0: IC_USB disabled (default, if available)
 - 1: IC_USB enable
 </td></tr>

<tr>
 <td>ahb_thr_ratio</td>
 <td>Specifies AHB Threshold ratio.
 - Values: 0 to 3 (default 0)
 </td></tr>

<tr>
 <td>power_down</td>
 <td>Specifies Power Down(Hibernation) Mode.
 The driver will automatically detect the value for this parameter if none is
 specified.
 - 0: Power Down disabled (default)
 - 2: Power Down enabled
 </td></tr>

 <tr>
 <td>reload_ctl</td>
 <td>Specifies whether dynamic reloading of the HFIR register is allowed during
 run time. The driver will automatically detect the value for this parameter if
 none is specified. In case the HFIR value is reloaded when HFIR.RldCtrl == 1'b0
 the core might misbehave.
 - 0: Reload Control disabled (default)
 - 1: Reload Control enabled
 </td></tr>

 <tr>
 <td>dev_out_nak</td>
 <td>Specifies whether  Device OUT NAK enhancement enabled or no.
 The driver will automatically detect the value for this parameter if
 none is specified. This parameter is valid only when OTG_EN_DESC_DMA == 1’b1.
 - 0: The core does not set NAK after Bulk OUT transfer complete (default)
 - 1: The core sets NAK after Bulk OUT transfer complete
 </td></tr>

 <tr>
 <td>cont_on_bna</td>
 <td>Specifies whether Enable Continue on BNA enabled or no.
 After receiving BNA interrupt the core disables the endpoint,when the
 endpoint is re-enabled by the application the
 - 0: Core starts processing from the DOEPDMA descriptor (default)
 - 1: Core starts processing from the descriptor which received the BNA.
 This parameter is valid only when OTG_EN_DESC_DMA == 1’b1.
 </td></tr>

 <tr>
 <td>ahb_single</td>
 <td>This bit when programmed supports SINGLE transfers for remainder data
 in a transfer for DMA mode of operation.
 - 0: The remainder data will be sent using INCR burst size (default)
 - 1: The remainder data will be sent using SINGLE burst size.
 </td></tr>

<tr>
 <td>adp_enable</td>
 <td>Specifies whether ADP feature is enabled.
 The driver will automatically detect the value for this parameter if none is
 specified.
 - 0: ADP feature disabled (default)
 - 1: ADP feature enabled
 </td></tr>

  <tr>
 <td>otg_ver</td>
 <td>Specifies whether OTG is performing as USB OTG Revision 2.0 or Revision 1.3
 USB OTG device.
 - 0: OTG 2.0 support disabled (default)
 - 1: OTG 2.0 support enabled
 </td></tr>

*/
