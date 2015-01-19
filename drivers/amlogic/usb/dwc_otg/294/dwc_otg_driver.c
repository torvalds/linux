/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/otg/linux/drivers/dwc_otg_driver.c $
 * $Revision: #91 $
 * $Date: 2011/10/24 $
 * $Change: 1871159 $
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
#include "dwc_os.h"
#include "dwc_otg_dbg.h"
#include "dwc_otg_driver.h"
#include "dwc_otg_attr.h"
#include "dwc_otg_core_if.h"
#include "dwc_otg_cil.h"
#include "dwc_otg_pcd_if.h"
#include "dwc_otg_hcd_if.h"

#include <linux/of_platform.h>
#include <linux/amlogic/of_lm.h>
#include <linux/amlogic/usb-aml.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/clk.h>
#include <asm/io.h>
#include <asm/sizes.h>
#include <plat/lm.h>
#include <mach/usbclock.h>
#include <mach/usb.h>


#define DWC_DRIVER_VERSION	"2.94a 22-MAR-2013"
#define DWC_DRIVER_DESC		"HS OTG USB Controller driver"

static const char dwc_driver_name[] = "dwc_otg";

static struct aml_usb_platform usb_platform_data = {
	.port_name 	= {MESON_USB_NAMES},
	.ctrl_regaddr 	= {MESON_USB_CTRL_ADDRS},
	.ctrl_size 	= {MESON_USB_CTRL_SIZES},
	.phy_regaddr 	= {MESON_USB_PHY_ADDRS},
	.phy_size 	= {MESON_USB_PHY_SIZES},
	.irq_no 		= {MESON_USB_IRQS},
	.fifo_size 	= {MESON_USB_FIFOS},
};

extern int pcd_init(
#ifdef LM_INTERFACE
			   struct lm_device *_dev
#elif  defined(PCI_INTERFACE)
			   struct pci_dev *_dev
#endif
    );
extern int hcd_init(
#ifdef LM_INTERFACE
			   struct lm_device *_dev
#elif  defined(PCI_INTERFACE)
			   struct pci_dev *_dev
#endif
    );

extern int pcd_remove(
#ifdef LM_INTERFACE
			     struct lm_device *_dev
#elif  defined(PCI_INTERFACE)
			     struct pci_dev *_dev
#endif
    );

extern void hcd_remove(
#ifdef LM_INTERFACE
			      struct lm_device *_dev
#elif  defined(PCI_INTERFACE)
			      struct pci_dev *_dev
#endif
    );

extern void dwc_otg_adp_start(dwc_otg_core_if_t * core_if, uint8_t is_host);

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
	.data_fifo_size = 1024,
	.dev_endpoints = -1,
	.en_multiple_tx_fifo = -1,
	.dev_rx_fifo_size = 256,
	.dev_nperio_tx_fifo_size = 256,
	.dev_tx_fifo_size = {
			     /* dev_tx_fifo_size */
			     256,
			     256,
			     128,
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
	.host_rx_fifo_size = 512,
	.host_nperio_tx_fifo_size = 500,
	.host_perio_tx_fifo_size = -1,
	.max_transfer_size = -1,
	.max_packet_count = -1,
	.host_channels = -1,
	.phy_type = -1,
	.phy_utmi_width = -1,
	.phy_ulpi_ddr = -1,
	.phy_ulpi_ext_vbus = -1,
	.i2c_enable = -1,
	.ulpi_fs_ls = -1,
	.ts_dline = -1,
	.thr_ctl = -1,
	.tx_thr_length = -1,
	.rx_thr_length = -1,
	.pti_enable = -1,
	.mpi_enable = -1,
	.lpm_enable = -1,
	.ic_usb_cap = -1,
	.ahb_thr_ratio = -1,
	.power_down = -1,
	.reload_ctl = 1,
	.dev_out_nak = -1,
	.cont_on_bna = -1,
	.ahb_single = 1,
	.otg_ver = -1,
	.adp_enable = -1,
};
/**
  *  Index-name refer to lm.h usb_dma_config_e
  */
static const char *dma_config_name[] = {
	"BURST_DEFAULT",
	"BURST_SINGLE",
	"BURST_INCR",
	"BURST_INCR4",
	"BURST_INCR8",
	"BURST_INCR16"
	"DISABLE",
};
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
uint32_t g_dbg_lvl = 0;		/* OFF */

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

/**
 * This function is called during module intialization
 * to pass module parameters to the DWC_OTG CORE.
 */
static int set_parameters(dwc_otg_core_if_t * core_if)
{
	int retval = 0;
	int i;

	if (dwc_otg_module_params.otg_cap != -1) {
		retval +=
		    dwc_otg_set_param_otg_cap(core_if,
					      dwc_otg_module_params.otg_cap);
	}
	if (dwc_otg_module_params.dma_enable != -1) {
		retval +=
		    dwc_otg_set_param_dma_enable(core_if,
						 dwc_otg_module_params.
						 dma_enable);
	}
	if (dwc_otg_module_params.dma_desc_enable != -1) {
		retval +=
		    dwc_otg_set_param_dma_desc_enable(core_if,
						      dwc_otg_module_params.
						      dma_desc_enable);
	}
	if (dwc_otg_module_params.opt != -1) {
		retval +=
		    dwc_otg_set_param_opt(core_if, dwc_otg_module_params.opt);
	}
	if (dwc_otg_module_params.dma_burst_size != -1) {
		retval +=
		    dwc_otg_set_param_dma_burst_size(core_if,
						     dwc_otg_module_params.
						     dma_burst_size);
	}
	if (dwc_otg_module_params.host_support_fs_ls_low_power != -1) {
		retval +=
		    dwc_otg_set_param_host_support_fs_ls_low_power(core_if,
								   dwc_otg_module_params.
								   host_support_fs_ls_low_power);
	}
	if (dwc_otg_module_params.enable_dynamic_fifo != -1) {
		retval +=
		    dwc_otg_set_param_enable_dynamic_fifo(core_if,
							  dwc_otg_module_params.
							  enable_dynamic_fifo);
	}
	if (dwc_otg_module_params.data_fifo_size != -1) {
		retval +=
		    dwc_otg_set_param_data_fifo_size(core_if,
						     dwc_otg_module_params.
						     data_fifo_size);
	}
	if (dwc_otg_module_params.dev_rx_fifo_size != -1) {
		retval +=
		    dwc_otg_set_param_dev_rx_fifo_size(core_if,
						       dwc_otg_module_params.
						       dev_rx_fifo_size);
	}
	if (dwc_otg_module_params.dev_nperio_tx_fifo_size != -1) {
		retval +=
		    dwc_otg_set_param_dev_nperio_tx_fifo_size(core_if,
							      dwc_otg_module_params.
							      dev_nperio_tx_fifo_size);
	}
	if (dwc_otg_module_params.host_rx_fifo_size != -1) {
		retval +=
		    dwc_otg_set_param_host_rx_fifo_size(core_if,
							dwc_otg_module_params.host_rx_fifo_size);
	}
	if (dwc_otg_module_params.host_nperio_tx_fifo_size != -1) {
		retval +=
		    dwc_otg_set_param_host_nperio_tx_fifo_size(core_if,
							       dwc_otg_module_params.
							       host_nperio_tx_fifo_size);
	}
	if (dwc_otg_module_params.host_perio_tx_fifo_size != -1) {
		retval +=
		    dwc_otg_set_param_host_perio_tx_fifo_size(core_if,
							      dwc_otg_module_params.
							      host_perio_tx_fifo_size);
	}
	if (dwc_otg_module_params.max_transfer_size != -1) {
		retval +=
		    dwc_otg_set_param_max_transfer_size(core_if,
							dwc_otg_module_params.
							max_transfer_size);
	}
	if (dwc_otg_module_params.max_packet_count != -1) {
		retval +=
		    dwc_otg_set_param_max_packet_count(core_if,
						       dwc_otg_module_params.
						       max_packet_count);
	}
	if (dwc_otg_module_params.host_channels != -1) {
		retval +=
		    dwc_otg_set_param_host_channels(core_if,
						    dwc_otg_module_params.
						    host_channels);
	}
	if (dwc_otg_module_params.dev_endpoints != -1) {
		retval +=
		    dwc_otg_set_param_dev_endpoints(core_if,
						    dwc_otg_module_params.
						    dev_endpoints);
	}
	if (dwc_otg_module_params.phy_type != -1) {
		retval +=
		    dwc_otg_set_param_phy_type(core_if,
					       dwc_otg_module_params.phy_type);
	}
	if (dwc_otg_module_params.speed != -1) {
		retval +=
		    dwc_otg_set_param_speed(core_if,
					    dwc_otg_module_params.speed);
	}
	if (dwc_otg_module_params.host_ls_low_power_phy_clk != -1) {
		retval +=
		    dwc_otg_set_param_host_ls_low_power_phy_clk(core_if,
								dwc_otg_module_params.
								host_ls_low_power_phy_clk);
	}
	if (dwc_otg_module_params.phy_ulpi_ddr != -1) {
		retval +=
		    dwc_otg_set_param_phy_ulpi_ddr(core_if,
						   dwc_otg_module_params.
						   phy_ulpi_ddr);
	}
	if (dwc_otg_module_params.phy_ulpi_ext_vbus != -1) {
		retval +=
		    dwc_otg_set_param_phy_ulpi_ext_vbus(core_if,
							dwc_otg_module_params.
							phy_ulpi_ext_vbus);
	}
	if (dwc_otg_module_params.phy_utmi_width != -1) {
		retval +=
		    dwc_otg_set_param_phy_utmi_width(core_if,
						     dwc_otg_module_params.
						     phy_utmi_width);
	}
	if (dwc_otg_module_params.ulpi_fs_ls != -1) {
		retval +=
		    dwc_otg_set_param_ulpi_fs_ls(core_if,
						 dwc_otg_module_params.ulpi_fs_ls);
	}
	if (dwc_otg_module_params.ts_dline != -1) {
		retval +=
		    dwc_otg_set_param_ts_dline(core_if,
					       dwc_otg_module_params.ts_dline);
	}
	if (dwc_otg_module_params.i2c_enable != -1) {
		retval +=
		    dwc_otg_set_param_i2c_enable(core_if,
						 dwc_otg_module_params.
						 i2c_enable);
	}
	if (dwc_otg_module_params.en_multiple_tx_fifo != -1) {
		retval +=
		    dwc_otg_set_param_en_multiple_tx_fifo(core_if,
							  dwc_otg_module_params.
							  en_multiple_tx_fifo);
	}
	for (i = 0; i < 15; i++) {
		if (dwc_otg_module_params.dev_perio_tx_fifo_size[i] != -1) {
			retval +=
			    dwc_otg_set_param_dev_perio_tx_fifo_size(core_if,
								     dwc_otg_module_params.
								     dev_perio_tx_fifo_size
								     [i], i);
		}
	}

	for (i = 0; i < 15; i++) {
		if (dwc_otg_module_params.dev_tx_fifo_size[i] != -1) {
			retval += dwc_otg_set_param_dev_tx_fifo_size(core_if,
								     dwc_otg_module_params.
								     dev_tx_fifo_size
								     [i], i);
		}
	}
	if (dwc_otg_module_params.thr_ctl != -1) {
		retval +=
		    dwc_otg_set_param_thr_ctl(core_if,
					      dwc_otg_module_params.thr_ctl);
	}
	if (dwc_otg_module_params.mpi_enable != -1) {
		retval +=
		    dwc_otg_set_param_mpi_enable(core_if,
						 dwc_otg_module_params.
						 mpi_enable);
	}
	if (dwc_otg_module_params.pti_enable != -1) {
		retval +=
		    dwc_otg_set_param_pti_enable(core_if,
						 dwc_otg_module_params.
						 pti_enable);
	}
	if (dwc_otg_module_params.lpm_enable != -1) {
		retval +=
		    dwc_otg_set_param_lpm_enable(core_if,
						 dwc_otg_module_params.
						 lpm_enable);
	}
	if (dwc_otg_module_params.ic_usb_cap != -1) {
		retval +=
		    dwc_otg_set_param_ic_usb_cap(core_if,
						 dwc_otg_module_params.
						 ic_usb_cap);
	}
	if (dwc_otg_module_params.tx_thr_length != -1) {
		retval +=
		    dwc_otg_set_param_tx_thr_length(core_if,
						    dwc_otg_module_params.tx_thr_length);
	}
	if (dwc_otg_module_params.rx_thr_length != -1) {
		retval +=
		    dwc_otg_set_param_rx_thr_length(core_if,
						    dwc_otg_module_params.
						    rx_thr_length);
	}
	if (dwc_otg_module_params.ahb_thr_ratio != -1) {
		retval +=
		    dwc_otg_set_param_ahb_thr_ratio(core_if,
						    dwc_otg_module_params.ahb_thr_ratio);
	}
	if (dwc_otg_module_params.power_down != -1) {
		retval +=
		    dwc_otg_set_param_power_down(core_if,
						 dwc_otg_module_params.power_down);
	}
	if (dwc_otg_module_params.reload_ctl != -1) {
		retval +=
		    dwc_otg_set_param_reload_ctl(core_if,
						 dwc_otg_module_params.reload_ctl);
	}

	if (dwc_otg_module_params.dev_out_nak != -1) {
		retval +=
			dwc_otg_set_param_dev_out_nak(core_if,
			dwc_otg_module_params.dev_out_nak);
	}

	if (dwc_otg_module_params.cont_on_bna != -1) {
		retval +=
			dwc_otg_set_param_cont_on_bna(core_if,
			dwc_otg_module_params.cont_on_bna);
	}

	if (dwc_otg_module_params.ahb_single != -1) {
		retval +=
			dwc_otg_set_param_ahb_single(core_if,
			dwc_otg_module_params.ahb_single);
	}

	if (dwc_otg_module_params.otg_ver != -1) {
		retval +=
		    dwc_otg_set_param_otg_ver(core_if,
					      dwc_otg_module_params.otg_ver);
	}
	if (dwc_otg_module_params.adp_enable != -1) {
		retval +=
		    dwc_otg_set_param_adp_enable(core_if,
						 dwc_otg_module_params.
						 adp_enable);
	}
	return retval;
}

#define FORCE_ID_CLEAR	-1
#define FORCE_ID_HOST	0
#define FORCE_ID_SLAVE	1
#define FORCE_ID_ERROR	2
static void dwc_otg_set_force_id(dwc_otg_core_if_t *core_if,int mode)
{
	gusbcfg_data_t gusbcfg_data;

	gusbcfg_data.d32 = DWC_READ_REG32(&core_if->core_global_regs->gusbcfg);
	switch(mode){
		case FORCE_ID_CLEAR:
			gusbcfg_data.b.force_host_mode = 0;
			gusbcfg_data.b.force_dev_mode = 0;
			break;
		case FORCE_ID_HOST:
			gusbcfg_data.b.force_host_mode = 1;
			gusbcfg_data.b.force_dev_mode = 0;
			break;
		case FORCE_ID_SLAVE:
			gusbcfg_data.b.force_host_mode = 0;
			gusbcfg_data.b.force_dev_mode = 1;
			break;
		default:
			DWC_ERROR("error id mode\n");
			return;
			break;
	}
	DWC_WRITE_REG32(&core_if->core_global_regs->gusbcfg,gusbcfg_data.d32);
	return;
}

#define VBUS_POWER_GPIO_OWNER  "DWC_OTG"
void set_usb_vbus_power(int pin,char is_power_on)
{
    if(is_power_on){
        printk( "set usb port power on (board gpio %d)!\n",pin);
	 
    }
    else    {
        printk("set usb port power off (board gpio %d)!\n",pin);
    }

    amlogic_gpio_direction_output(pin,is_power_on,VBUS_POWER_GPIO_OWNER);	
}

static void dwc_otg_id_change_timer_handler(void * parg)
{

	dwc_otg_device_t *otg_dev = (dwc_otg_device_t *)parg;
//	struct lm_device * lmdev = otg_dev->os_dep.lmdev;
	usb_peri_reg_t * phy_peri = (usb_peri_reg_t * )otg_dev->core_if->usb_peri_reg;
	usb_adp_bc_data_t adp_bc;
	unsigned long flags;

	//DWC_DEBUGPL(DBG_HCDV, "%s() %p\n", __func__, otg_dev);
	local_irq_save(flags);

	adp_bc.d32 = phy_peri->adp_bc;
	if(adp_bc.b.iddig){
		dwc_otg_set_force_id(otg_dev->core_if, FORCE_ID_SLAVE);
	}else{
		dwc_otg_set_force_id(otg_dev->core_if, FORCE_ID_HOST);
	}

	DWC_TIMER_SCHEDULE(otg_dev->id_change_timer, 100 /* 100 ms */);

	local_irq_restore(flags);
	return;
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
		S3C2410X_CLEAR_EINTPEND();
	}
	return IRQ_RETVAL(retval);
}

/**
 * This function is called when a lm_device is unregistered with the
 * dwc_otg_driver. This happens, for example, when the rmmod command is
 * executed. The device may or may not be electrically present. If it is
 * present, the driver stops device processing. Any resources used on behalf
 * of this device are freed.
 *
 * @param _dev
 */
static void dwc_otg_driver_remove(
#ifdef LM_INTERFACE
					 struct lm_device *_dev
#elif defined(PCI_INTERFACE)
					 struct pci_dev *_dev
#endif
    )
{
#ifdef LM_INTERFACE
	dwc_otg_device_t *otg_dev = lm_get_drvdata(_dev);
#elif defined(PCI_INTERFACE)
	dwc_otg_device_t *otg_dev = pci_get_drvdata(_dev);
#endif

	DWC_DEBUGPL(DBG_ANY, "%s(%p)\n", __func__, _dev);

	if (!otg_dev) {
		/* Memory allocation for the dwc_otg_device failed. */
		DWC_DEBUGPL(DBG_ANY, "%s: otg_dev NULL!\n", __func__);
		return;
	}
#ifndef DWC_DEVICE_ONLY
	if (otg_dev->hcd) {
		hcd_remove(_dev);
	} else {
		DWC_DEBUGPL(DBG_ANY, "%s: otg_dev->hcd NULL!\n", __func__);
		return;
	}
#endif

#ifndef DWC_HOST_ONLY
	if (otg_dev->pcd) {
		pcd_remove(_dev);
	} else {
		DWC_DEBUGPL(DBG_ANY, "%s: otg_dev->pcd NULL!\n", __func__);
		return;
	}
#endif
	/*
	 * Free the IRQ
	 */
	if (otg_dev->common_irq_installed) {
		free_irq(_dev->irq, otg_dev);
	} else {
		DWC_DEBUGPL(DBG_ANY, "%s: There is no installed irq!\n", __func__);
		return;
	}

	if (otg_dev->core_if) {
		amlogic_gpio_free(otg_dev->core_if->vbus_power_pin,VBUS_POWER_GPIO_OWNER);
		dwc_otg_cil_remove(otg_dev->core_if);
	} else {
		DWC_DEBUGPL(DBG_ANY, "%s: otg_dev->core_if NULL!\n", __func__);
		return;
	}

	if(otg_dev->id_change_timer)
		DWC_TIMER_FREE(otg_dev->id_change_timer);

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
#ifdef LM_INTERFACE
	lm_set_drvdata(_dev, 0);
#elif defined(PCI_INTERFACE)
    release_mem_region(otg_dev->os_dep.rsrc_start, otg_dev->os_dep.rsrc_len);
	pci_set_drvdata(_dev, 0);
#endif
}
#ifdef CONFIG_HAS_EARLYSUSPEND
extern int get_pcd_ums_state(dwc_otg_pcd_t *pcd);
static void usb_early_suspend(struct early_suspend *h)
{
	int is_mount = 0;
	dwc_otg_device_t *dwc_otg_device;
	dwc_otg_device = (dwc_otg_device_t *)h->param;
	is_mount = get_pcd_ums_state(dwc_otg_device->pcd);
	printk("DWC_OTG: going early suspend! is_mount=%d\n",is_mount);
	if (dwc_otg_is_device_mode(dwc_otg_device->core_if) && !is_mount) {
		DWC_MODIFY_REG32(&dwc_otg_device->core_if->dev_if->dev_global_regs->dctl, 0, 2);
	}
}
static void usb_early_resume(struct early_suspend *h)
{
	dwc_otg_device_t *dwc_otg_device;
	printk("DWC_OTG: going early resume\n");
	dwc_otg_device = (dwc_otg_device_t *)h->param;
	if (dwc_otg_is_device_mode(dwc_otg_device->core_if)) {
		DWC_MODIFY_REG32(&dwc_otg_device->core_if->dev_if->dev_global_regs->dctl, 2, 0);
	}
}
#endif
static const struct of_device_id dwc_otg_dt_match[]={
	{	.compatible 	= "amlogic,usb",
	},
	{},
};
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
static int dwc_otg_driver_probe(
#ifdef LM_INTERFACE
				       struct lm_device *_dev
#elif defined(PCI_INTERFACE)
				       struct pci_dev *_dev,
				       const struct pci_device_id *id
#endif
    )
{
	int retval = 0;
	int port_index = 0;
	int port_type = USB_PORT_TYPE_OTG;
	int id_mode = USB_PHY_ID_MODE_HW;
	int port_speed = USB_PORT_SPEED_DEFAULT;
	int port_config = 0;
	int dma_config = USB_DMA_BURST_DEFAULT;
	int gpio_work_mask =1;
	int gpio_vbus_power_pin = -1;
	unsigned int phy_reg_addr = 0;
	unsigned int ctrl_reg_addr = 0;
	const char *s_clock_name = NULL;
	const char *gpio_name = NULL;
	const void *prop;
	struct clk * clock;
	dwc_otg_device_t *dwc_otg_device;
	struct dwc_otg_driver_module_params *pcore_para;

	dev_dbg(&_dev->dev, "dwc_otg_driver_probe(%p)\n", _dev);

#ifdef LM_INTERFACE
//	dev_dbg(&_dev->dev, "start=0x%08x\n", (unsigned)_dev->resource.start);
#elif defined(PCI_INTERFACE)
	if (!id) {
		DWC_ERROR("Invalid pci_device_id %p", id);
		return -EINVAL;
	}

	if (!_dev || (pci_enable_device(_dev) < 0)) {
		DWC_ERROR("Invalid pci_device %p", _dev);
		return -ENODEV;
	}
	dev_dbg(&_dev->dev, "start=0x%08x\n", (unsigned)pci_resource_start(_dev,0));
	/* other stuff needed as well? */

#endif

#ifdef LM_INTERFACE
	if (_dev->dev.of_node) {
		const struct of_device_id *match;
		struct device_node	*of_node = _dev->dev.of_node;
		match = of_lm_match_node(dwc_otg_dt_match, of_node);
		if(match){
			s_clock_name = of_get_property(of_node, "clock-src", NULL);
			prop = of_get_property(of_node, "port-id", NULL);
			if(prop)
				port_index = of_read_ulong(prop,1);
			prop = of_get_property(of_node, "port-type", NULL);
			if(prop)
				port_type = of_read_ulong(prop,1);
			prop = of_get_property(of_node, "port-speed", NULL);
			if(prop)
				port_speed = of_read_ulong(prop,1);
			prop = of_get_property(of_node, "port-config", NULL);
			if(prop)
				port_config = of_read_ulong(prop,1);
			prop = of_get_property(of_node, "port-dma", NULL);
			if(prop)
				dma_config = of_read_ulong(prop,1);
			prop = of_get_property(of_node, "port-id-mode", NULL);
			if(prop)
				id_mode = of_read_ulong(prop,1);

			gpio_name = of_get_property(of_node, "gpio-vbus-power", NULL);
			if(gpio_name)
			{
				gpio_vbus_power_pin= amlogic_gpio_name_map_num(gpio_name);
				amlogic_gpio_request(gpio_vbus_power_pin,VBUS_POWER_GPIO_OWNER);
				
				prop = of_get_property(of_node, "gpio-work-mask", NULL);
				if(prop)
					gpio_work_mask = of_read_ulong(prop,1);	
			}
			ctrl_reg_addr = (unsigned long)usb_platform_data.ctrl_regaddr[port_index];
			phy_reg_addr = (unsigned long)usb_platform_data.phy_regaddr[port_index];
			_dev->irq = usb_platform_data.irq_no[port_index];
printk("%s: type: %d, speed: %d, config: %d, dma: %d, id: %d, phy: %x, ctrl: %x\n",
	s_clock_name,port_type,port_speed,port_config,dma_config,id_mode,phy_reg_addr,ctrl_reg_addr);

		}
	}
#endif

	dwc_otg_device = DWC_ALLOC(sizeof(dwc_otg_device_t));

	if (!dwc_otg_device) {
		dev_err(&_dev->dev, "kmalloc of dwc_otg_device failed\n");
		return -ENOMEM;
	}

	memset(dwc_otg_device, 0, sizeof(*dwc_otg_device));
	dwc_otg_device->os_dep.reg_offset = 0xFFFFFFFF;

	/*
	 * Map the DWC_otg Core memory into virtual address space.
	 */
#ifdef LM_INTERFACE

	clock = clk_get_sys(s_clock_name, NULL);
	if(clk_enable(clock)){
		dev_err(&_dev->dev, "Set dwc_otg PHY clock %s failed!\n",s_clock_name);
		return -ENODEV;
	}
	/*if(set_usb_phy_clk(_dev,1)){
		dev_err(&_dev->dev, "Set dwc_otg PHY clock failed!\n");
		return -ENODEV;
	}*/

	dwc_otg_device->os_dep.base = (void*)ctrl_reg_addr;
	//dwc_otg_device->os_dep.base = (void*)_dev->resource.start;
	//ioremap(_dev->resource.start, SZ_256K);

	if (!dwc_otg_device->os_dep.base) {
		dev_err(&_dev->dev, "ioremap() failed\n");
		DWC_FREE(dwc_otg_device);
		return -ENOMEM;
	}
	dev_dbg(&_dev->dev, "base=0x%08x\n",
		(unsigned)dwc_otg_device->os_dep.base);
#elif defined(PCI_INTERFACE)
	_dev->current_state = PCI_D0;
	_dev->dev.power.power_state = PMSG_ON;

	if (!_dev->irq) {
		DWC_ERROR("Found HC with no IRQ. Check BIOS/PCI %s setup!",
			  pci_name(_dev));
		iounmap(dwc_otg_device->os_dep.base);
		DWC_FREE(dwc_otg_device);
		return -ENODEV;
	}

	dwc_otg_device->os_dep.rsrc_start = pci_resource_start(_dev, 0);
	dwc_otg_device->os_dep.rsrc_len = pci_resource_len(_dev, 0);
	DWC_DEBUGPL(DBG_ANY, "PCI resource: start=%08x, len=%08x\n",
		    (unsigned)dwc_otg_device->os_dep.rsrc_start,
		    (unsigned)dwc_otg_device->os_dep.rsrc_len);
	if (!request_mem_region
	    (dwc_otg_device->os_dep.rsrc_start, dwc_otg_device->os_dep.rsrc_len,
	     "dwc_otg")) {
		dev_dbg(&_dev->dev, "error requesting memory\n");
		iounmap(dwc_otg_device->os_dep.base);
		DWC_FREE(dwc_otg_device);
		return -EFAULT;
	}

	dwc_otg_device->os_dep.base =
	    ioremap_nocache(dwc_otg_device->os_dep.rsrc_start,
			    dwc_otg_device->os_dep.rsrc_len);
	if (dwc_otg_device->os_dep.base == NULL) {
		dev_dbg(&_dev->dev, "error mapping memory\n");
		release_mem_region(dwc_otg_device->os_dep.rsrc_start,
				   dwc_otg_device->os_dep.rsrc_len);
		iounmap(dwc_otg_device->os_dep.base);
		DWC_FREE(dwc_otg_device);
		return -EFAULT;
	}
	dev_dbg(&_dev->dev, "base=0x%p (before adjust) \n",
		dwc_otg_device->os_dep.base);
	dwc_otg_device->os_dep.base = (char *)dwc_otg_device->os_dep.base;
	dev_dbg(&_dev->dev, "base=0x%p (after adjust) \n",
		dwc_otg_device->os_dep.base);
	dev_dbg(&_dev->dev, "%s: mapped PA 0x%x to VA 0x%p\n", __func__,
		(unsigned)dwc_otg_device->os_dep.rsrc_start,
		dwc_otg_device->os_dep.base);

	pci_set_master(_dev);
	pci_set_drvdata(_dev, dwc_otg_device);
#endif

	/*
	 * Initialize driver data to point to the global DWC_otg
	 * Device structure.
	 */
#ifdef LM_INTERFACE
	lm_set_drvdata(_dev, dwc_otg_device);
	dwc_otg_device->os_dep.lmdev = _dev;
	dwc_otg_device->gen_dev = &_dev->dev;
#elif defined(PCI_INTERFACE)
	pci_set_drvdata(_dev, dwc_otg_device);
	dwc_otg_device->os_dep.pcidev = _dev;
	dwc_otg_device->gen_dev = &_dev->dev;
#endif
	dwc_otg_device->dev_name = dev_name(dwc_otg_device->gen_dev);

	pcore_para = &dwc_otg_module_params;

	dev_dbg(&_dev->dev, "dwc_otg_device=0x%p\n", dwc_otg_device);

	dwc_otg_device->core_if = dwc_otg_cil_init(dwc_otg_device->os_dep.base);
	if (!dwc_otg_device->core_if) {
		dev_err(&_dev->dev, "CIL initialization failed!\n");
		retval = -ENOMEM;
		goto fail;
	}

	//dwc_otg_device->core_if->usb_peri_reg = (usb_peri_reg_t *)_dev->param.usb.phy_tune_reg;
	dwc_otg_device->core_if->usb_peri_reg = (usb_peri_reg_t *)phy_reg_addr;
	/*
	 * Attempt to ensure this device is really a DWC_otg Controller.
	 * Read and verify the SNPSID register contents. The value should be
	 * 0x45F42XXX, which corresponds to "OT2", as in "OTG version 2.XX".
	 */

	if ((dwc_otg_get_gsnpsid(dwc_otg_device->core_if) & 0xFFFFF000) !=
	    0x4F542000) {
		dev_err(&_dev->dev, "Bad value for SNPSID: 0x%08x\n",
			dwc_otg_get_gsnpsid(dwc_otg_device->core_if));
		retval = -EINVAL;
		goto fail;
	}

	dev_dbg(&_dev->dev,"DMA config: %s\n",dma_config_name[dma_config]);
	if (dma_config == USB_DMA_DISABLE) {
		pcore_para->dma_enable = 0;
		_dev->dev.coherent_dma_mask = 0;
		_dev->dev.dma_mask = 0;
	} else {
		_dev->dev.dma_mask = &_dev->dma_mask_room;
		_dev->dev.coherent_dma_mask = *_dev->dev.dma_mask;
		//printk("_lmdev->dev.dma_mask %p (%llX)\n",_lmdev->dev.dma_mask,*_lmdev->dev.dma_mask);
		switch (dma_config) {
		case USB_DMA_BURST_INCR:
			pcore_para->dma_burst_size =
			    DWC_GAHBCFG_INT_DMA_BURST_INCR;
			break;
		case USB_DMA_BURST_INCR4:
			pcore_para->dma_burst_size =
			    DWC_GAHBCFG_INT_DMA_BURST_INCR4;
			break;
		case USB_DMA_BURST_INCR8:
			pcore_para->dma_burst_size =
			    DWC_GAHBCFG_INT_DMA_BURST_INCR8;
			break;
		case USB_DMA_BURST_INCR16:
			pcore_para->dma_burst_size =
			    DWC_GAHBCFG_INT_DMA_BURST_INCR16;
			break;
		case USB_DMA_BURST_SINGLE:
			pcore_para->dma_burst_size =
			    DWC_GAHBCFG_INT_DMA_BURST_SINGLE;
			break;
		default:
			pcore_para->dma_burst_size =
			    DWC_GAHBCFG_INT_DMA_BURST_INCR4;
			break;
		}
	}

	/*
	 * Validate parameter values.
	 */
	if (set_parameters(dwc_otg_device->core_if)) {
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
	DWC_DEBUGPL(DBG_CIL, "registering (common) handler for irq%d\n",
		    _dev->irq);
	retval = request_irq(_dev->irq, dwc_otg_common_irq,
			     IRQF_SHARED | IRQF_DISABLED | IRQ_LEVEL, "dwc_otg",
			     dwc_otg_device);
	if (retval) {
		DWC_ERROR("request of irq%d failed\n", _dev->irq);
		retval = -EBUSY;
		goto fail;
	} else {
		dwc_otg_device->common_irq_installed = 1;
	}

#ifdef LM_INTERFACE
//	set_irq_type(_dev->irq, IRQT_LOW);
#endif

	switch(port_type){
	case USB_PORT_TYPE_OTG:
		id_mode = FORCE_ID_CLEAR;
		break;
	case USB_PORT_TYPE_HOST:
		id_mode = FORCE_ID_HOST;
		break;
	case USB_PORT_TYPE_SLAVE:
		id_mode = FORCE_ID_SLAVE;
		break;
	default:
		id_mode = FORCE_ID_ERROR;
		break;
	}
	dwc_otg_set_force_id(dwc_otg_device->core_if,id_mode);

	/*
	 * Initialize the DWC_otg core.
	 */
	dwc_otg_core_init(dwc_otg_device->core_if);

	/*
	 *   Set VBus Power CallBack
	 */
	dwc_otg_device->core_if->vbus_power_pin = gpio_vbus_power_pin;
	dwc_otg_device->core_if->vbus_power_pin_work_mask= gpio_work_mask;

	if (port_type == USB_PORT_TYPE_HOST) {
		/*
		 * Initialize the HCD
		 */
		printk("Working on port type = HOST\n");
		if (!dwc_otg_is_host_mode(dwc_otg_device->core_if)) {
			printk
			    ("Chip mode not match! -- Want HOST mode but not.  --\n");
			goto fail;
		}
		retval = hcd_init(_dev);
		if (retval != 0) {
			DWC_ERROR("hcd_init failed\n");
			dwc_otg_device->hcd = NULL;
			goto fail;
		}
	} else if (port_type == USB_PORT_TYPE_SLAVE) {
		/*
		 * Initialize the PCD
		 */
		printk("Working on port type = SLAVE\n");
		if (!dwc_otg_is_device_mode(dwc_otg_device->core_if)) {
			DWC_ERROR
			    ("Chip mode not match! -- Want Device mode but not.  --\n");
			goto fail;
		}
		//dwc_otg_device->core_if->charger_detect_cb = _dev->param.usb.charger_detect_cb;
		retval = pcd_init(_dev);
		if (retval != 0) {
			DWC_ERROR("pcd_init failed\n");
			dwc_otg_device->pcd = NULL;
			goto fail;
		}
	}

	else if (port_type == USB_PORT_TYPE_OTG) {
		printk("Working on port type = OTG\n");
		printk("Current port type: %s\n",
			dwc_otg_is_host_mode(dwc_otg_device->core_if)?"HOST":"SLAVE");

		retval = hcd_init(_dev);
		if (retval != 0) {
			DWC_ERROR("hcd_init failed(in otg mode)\n");
			dwc_otg_device->hcd = NULL;
			goto fail;
		}
		//dwc_otg_device->core_if->charger_detect_cb = _dev->param.usb.charger_detect_cb;
		retval = pcd_init(_dev);
		if (retval != 0) {
			DWC_ERROR("pcd_init failed(in otg mode)\n");
			dwc_otg_device->pcd = NULL;
			goto fail;
		}
		if(!dwc_otg_get_param_adp_enable(dwc_otg_device->core_if)){
			DWC_PRINTF("using timer detect id change, %p\n",dwc_otg_device->core_if);
			dwc_otg_device->id_change_timer = DWC_TIMER_ALLOC("ID change timer",
				dwc_otg_id_change_timer_handler,dwc_otg_device);
			DWC_TIMER_SCHEDULE(dwc_otg_device->id_change_timer, 0);
		}
	}

	else {
		DWC_ERROR("can't config as right mode\n");
		goto fail;
	}

	dwc_otg_save_global_regs(dwc_otg_device->core_if);

	/*
	 * Enable the global interrupt after all the interrupt
	 * handlers are installed if there is no ADP support else
	 * perform initial actions required for Internal ADP logic.
	 */

	if(port_type == USB_PORT_TYPE_OTG){
		if (!dwc_otg_get_param_adp_enable(dwc_otg_device->core_if))
			dwc_otg_enable_global_interrupts(dwc_otg_device->core_if);
		else
			dwc_otg_adp_start(dwc_otg_device->core_if,
								dwc_otg_is_host_mode(dwc_otg_device->core_if));
	}else{
		dwc_otg_enable_global_interrupts(dwc_otg_device->core_if);
	}
#ifdef CONFIG_HAS_EARLYSUSPEND
        dwc_otg_device->usb_early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB;
        dwc_otg_device->usb_early_suspend.suspend = usb_early_suspend;
        dwc_otg_device->usb_early_suspend.resume = usb_early_resume;
        dwc_otg_device->usb_early_suspend.param = dwc_otg_device;
        register_early_suspend(&dwc_otg_device->usb_early_suspend);
#endif
	return 0;

fail:
	dwc_otg_driver_remove(_dev);
	return retval;
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
#ifdef LM_INTERFACE
static struct lm_driver dwc_otg_driver = {
	.drv = {.name = (char *)dwc_driver_name,},
	.probe = dwc_otg_driver_probe,
	.remove = dwc_otg_driver_remove,
};
#elif defined(PCI_INTERFACE)
static const struct pci_device_id pci_ids[] = { {
						 PCI_DEVICE(0x16c3, 0xabcd),
						 .driver_data =
						 (unsigned long)0xdeadbeef,
						 }, { /* end: all zeroes */ }
};

MODULE_DEVICE_TABLE(pci, pci_ids);

/* pci driver glue; this is a "new style" PCI driver module */
static struct pci_driver dwc_otg_driver = {
	.name = "dwc_otg",
	.id_table = pci_ids,

	.probe = dwc_otg_driver_probe,
	.remove = dwc_otg_driver_remove,

	.driver = {
		   .name = (char *)dwc_driver_name,
		   },
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
	int error;
	printk(KERN_INFO "%s: version %s\n", dwc_driver_name,
	       DWC_DRIVER_VERSION);
#ifdef LM_INTERFACE
	retval = lm_driver_register(&dwc_otg_driver);
#elif defined(PCI_INTERFACE)
	retval = pci_register_driver(&dwc_otg_driver);
#endif
	if (retval < 0) {
		printk(KERN_ERR "%s retval=%d\n", __func__, retval);
		return retval;
	}
#ifdef LM_INTERFACE
	error = driver_create_file(&dwc_otg_driver.drv, &driver_attr_version);
	error = driver_create_file(&dwc_otg_driver.drv, &driver_attr_debuglevel);
#elif defined(PCI_INTERFACE)
	error = driver_create_file(&dwc_otg_driver.driver, &driver_attr_version);
	error = driver_create_file(&dwc_otg_driver.driver, &driver_attr_debuglevel);
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

#ifdef LM_INTERFACE
	driver_remove_file(&dwc_otg_driver.drv, &driver_attr_debuglevel);
	driver_remove_file(&dwc_otg_driver.drv, &driver_attr_version);
	lm_driver_unregister(&dwc_otg_driver);
#elif defined(PCI_INTERFACE)
	driver_remove_file(&dwc_otg_driver.driver, &driver_attr_debuglevel);
	driver_remove_file(&dwc_otg_driver.driver, &driver_attr_version);
	pci_unregister_driver(&dwc_otg_driver);
#endif

	printk(KERN_INFO "%s module removed\n", dwc_driver_name);
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
