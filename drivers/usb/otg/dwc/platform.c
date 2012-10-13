/*
 * DesignWare HS OTG controller driver
 * Copyright (C) 2006 Synopsys, Inc.
 * Portions Copyright (C) 2010 Applied Micro Circuits Corporation.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/licenses
 * or write to the Free Software Foundation, Inc., 51 Franklin Street,
 * Suite 500, Boston, MA 02110-1335 USA.
 *
 * Based on Synopsys driver version 2.60a
 * Modified by Mark Miesfeld <mmiesfeld@apm.com>
 * Modified by Stefan Roese <sr@denx.de>, DENX Software Engineering
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING BUT NOT LIMITED TO THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL SYNOPSYS, INC. BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES
 * (INCLUDING BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * The dwc_otg module provides the initialization and cleanup entry
 * points for the dwcotg driver. This module will be dynamically installed
 * after Linux is booted using the insmod command. When the module is
 * installed, the dwc_otg_driver_init function is called. When the module is
 * removed (using rmmod), the dwc_otg_driver_cleanup function is called.
 *
 * This module also defines a data structure for the dwc_otg driver, which is
 * used in conjunction with the standard device structure. These
 * structures allow the OTG driver to comply with the standard Linux driver
 * model in which devices and drivers are registered with a bus driver. This
 * has the benefit that Linux can expose attributes of the driver and device
 * in its special sysfs file system. Users can then read or write files in
 * this file system to perform diagnostics on the driver components or the
 * device.
 */

#include <linux/usb/dwc_otg.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>

#include "driver.h"

#define DWC_DRIVER_VERSION		"1.05"
#define DWC_DRIVER_DESC			"HS OTG USB Controller driver"
static const char dwc_driver_name[] = "dwc_otg";
u64 dwc_otg_dmamask = 0xffffffffUL;

static irqreturn_t dwc_otg_common_irq(int _irq, void *dev)
{
	struct dwc_otg_device *dwc_dev = dev;
	int retval;

	spin_lock(&dwc_dev->lock);
	retval = dwc_otg_handle_common_intr(dwc_dev->core_if);
	spin_unlock(&dwc_dev->lock);
	return IRQ_RETVAL(retval);
}

static irqreturn_t dwc_otg_externalchgpump_irq(int _irq, void *dev)
{
	struct dwc_otg_device *dwc_dev = dev;

	if (dwc_otg_is_host_mode(dwc_dev->core_if)) {
		struct dwc_hcd *dwc_hcd;
		u32 hprt0 = 0;

		dwc_hcd = dwc_dev->hcd;
		spin_lock(&dwc_hcd->lock);
		dwc_hcd->flags.b.port_over_current_change = 1;

		hprt0 = DWC_HPRT0_PRT_PWR_RW(hprt0, 0);
		dwc_reg_write(dwc_dev->core_if->host_if->hprt0, 0, hprt0);
		spin_unlock(&dwc_hcd->lock);
	} else {
		/* Device mode - This int is n/a for device mode */
		dev_dbg(dev, "DeviceMode: OTG OverCurrent Detected\n");
	}

	return IRQ_HANDLED;
}

static int __devexit dwc_otg_driver_remove(struct platform_device *ofdev)
{
	struct device *dev = &ofdev->dev;
	struct dwc_otg_device *dwc_dev = dev_get_drvdata(dev);

	/* Memory allocation for dwc_otg_device may have failed. */
	if (!dwc_dev)
		return 0;

	/* Free the IRQ */
	free_irq(dwc_dev->irq, dwc_dev);
	/* Free external charge pump irq */
	free_irq(dwc_dev->hcd->cp_irq, dwc_dev);

	if (dwc_dev->hcd)
		dwc_otg_hcd_remove(dev);

	if (dwc_dev->pcd)
		dwc_otg_pcd_remove(dev);

	if (dwc_dev->core_if)
		dwc_otg_cil_remove(dwc_dev->core_if);

	if(dwc_dev->core_if->core_params)
		kfree(dwc_dev->core_if->core_params);

	/* Return the memory. */
	if (dwc_dev->base)
		iounmap(dwc_dev->base);

	if (dwc_dev->phys_addr)
		release_mem_region(dwc_dev->phys_addr, dwc_dev->base_len);

	usb_put_phy(dwc_dev->core_if->xceiv);
	dwc_dev->core_if->xceiv = NULL;

	kfree(dwc_dev);

	/* Clear the drvdata pointer. */
	dev_set_drvdata(dev, NULL);
	return 0;
}

static u64 dwc_otg_dma_mask;

static int __devinit dwc_otg_driver_probe(struct platform_device *ofdev)
{
	int retval;
	struct dwc_otg_device *dwc_dev;
	struct device *dev = &ofdev->dev;
	struct resource res;
	ulong gusbcfg_addr;
	u32 usbcfg = 0;
	struct resource *nres = 0;
#ifdef CONFIG_OF
	u32 prop;
	u32 prop_array[15];
#endif

	dwc_dev = kzalloc(sizeof(*dwc_dev), GFP_KERNEL);
	if (!dwc_dev) {
		dev_err(dev, "kmalloc of dwc_otg_device failed\n");
		retval = -ENOMEM;
		goto fail_dwc_dev;
	}

	/* Retrieve the memory and IRQ resources. */
	dwc_dev->irq = platform_get_irq(ofdev, 0);
	if (dwc_dev->irq == NO_IRQ) {
		dev_err(dev, "no device irq\n");
		retval = -ENODEV;
		goto fail_of_irq;
	}

	nres = platform_get_resource(ofdev, IORESOURCE_MEM, 0);
	res = *nres;
	if(nres == 0) {
		dev_err(dev, "%s: Can't get USB-OTG register address\n",
			__func__);
		retval = -ENOMEM;
		goto fail_of_irq;
	}

	dwc_dev->phys_addr = res.start;
	dwc_dev->base_len = res.end - res.start + 1;
	if (!request_mem_region(dwc_dev->phys_addr,
				dwc_dev->base_len, dwc_driver_name)) {
		dev_err(dev, "request_mem_region failed\n");
		retval = -EBUSY;
		goto fail_of_irq;
	}

	/* Map the DWC_otg Core memory into virtual address space. */
	dwc_dev->base = ioremap(platform_get_resource(ofdev, IORESOURCE_MEM, 0)->start, SZ_256K);
	if (!dwc_dev->base) {
		dev_err(dev, "ioremap() failed\n");
		retval = -ENOMEM;
		goto fail_ioremap;
	}
	dev_dbg(dev, "mapped base=0x%08x\n", (__force u32)dwc_dev->base);

	/*
	 * Initialize driver data to point to the global DWC_otg
	 * Device structure.
	 */
	dev_set_drvdata(dev, dwc_dev);

	dwc_dev->core_if =
	    dwc_otg_cil_init(dwc_dev->base, &dwc_otg_module_params);
	if (!dwc_dev->core_if) {
		dev_err(dev, "CIL initialization failed!\n");
		retval = -ENOMEM;
		goto fail_cil_init;
	}

	/*
	* Set the wqfunc of this core_if as "not set"
	*/
	dwc_dev->core_if->wqfunc_setup_done = 0;

	/*
	 * Validate parameter values after dwc_otg_cil_init.
	 */
	if (check_parameters(dwc_dev->core_if)) {
		retval = -EINVAL;
		goto fail_check_param;
	}

#ifdef CONFIG_OF
	if(!of_property_read_u32(ofdev->dev.of_node,
		"dma-mask", (u32*)&dwc_otg_dma_mask)) {
		dev->dma_mask = &dwc_otg_dma_mask;
	}
	else {
		dev->dma_mask = NULL;
	}

	if(!of_property_read_u32(ofdev->dev.of_node,
		"ulpi-ddr", &prop)) {
		dwc_otg_module_params.phy_ulpi_ddr = prop;
	}

	if(!of_property_read_u32(ofdev->dev.of_node,
		"host-rx-fifo-size", &prop)) {
		dwc_otg_module_params.host_rx_fifo_size = prop;
	}
	if(!of_property_read_u32(ofdev->dev.of_node,
		"dev-rx-fifo-size", &prop)) {
		dwc_otg_module_params.dev_rx_fifo_size = prop;
	}
	if(!of_property_read_u32(ofdev->dev.of_node,
		"host-nperio-tx-fifo-size", &prop)) {
		dwc_otg_module_params.host_nperio_tx_fifo_size = prop;
	}
	if(!of_property_read_u32(ofdev->dev.of_node,
		"dev-nperio-tx-fifo-size", &prop)) {
		dwc_otg_module_params.dev_nperio_tx_fifo_size = prop;
	}
	if(!of_property_read_u32(ofdev->dev.of_node,
		"host-perio-tx-fifo-size", &prop)) {
		dwc_otg_module_params.host_perio_tx_fifo_size = prop;
	}
	if(!of_property_read_u32_array(ofdev->dev.of_node,
		"dev-perio-tx-fifo-size", prop_array, MAX_PERIO_FIFOS)) {
		int i;
		for(i=0; i<MAX_PERIO_FIFOS; i++)
			dwc_otg_module_params.dev_tx_fifo_size[i]
				= prop_array[i];
	}
	if(!of_property_read_u32_array(ofdev->dev.of_node,
		"dev-tx-fifo-size", prop_array, MAX_TX_FIFOS)) {
		int i;
		for(i=0; i<MAX_TX_FIFOS; i++)
			dwc_otg_module_params.dev_perio_tx_fifo_size[i]
				= prop_array[i];
	}
#endif

	usb_nop_xceiv_register();
	dwc_dev->core_if->xceiv = usb_get_phy(USB_PHY_TYPE_USB2);
	if (!dwc_dev->core_if->xceiv) {
		retval = -ENODEV;
		goto fail_xceiv;
	}
	dwc_set_feature(dwc_dev->core_if);

	/* Initialize the DWC_otg core. */
	dwc_otg_core_init(dwc_dev->core_if);

	/*
	 * Disable the global interrupt until all the interrupt
	 * handlers are installed.
	 */
	spin_lock(&dwc_dev->lock);
	dwc_otg_disable_global_interrupts(dwc_dev->core_if);
	spin_unlock(&dwc_dev->lock);

	/*
	 * Install the interrupt handler for the common interrupts before
	 * enabling common interrupts in core_init below.
	 */
	retval = request_irq(dwc_dev->irq, dwc_otg_common_irq,
			     IRQF_SHARED, "dwc_otg", dwc_dev);
	if (retval) {
		dev_err(dev, "request of irq%d failed retval: %d\n",
			dwc_dev->irq, retval);
		retval = -EBUSY;
		goto fail_req_irq;
	} else {
		dwc_dev->common_irq_installed = 1;
	}

	if (!dwc_has_feature(dwc_dev->core_if, DWC_HOST_ONLY)) {
		/* Initialize the PCD */
		retval = dwc_otg_pcd_init(dev);
		if (retval) {
			dev_err(dev, "dwc_otg_pcd_init failed\n");
			dwc_dev->pcd = NULL;
			goto fail_req_irq;
		}
	}

	gusbcfg_addr = (ulong) (dwc_dev->core_if->core_global_regs)
		+ DWC_GUSBCFG;
	if (!dwc_has_feature(dwc_dev->core_if, DWC_DEVICE_ONLY)) {
		/* Initialize the HCD and force_host_mode */
		usbcfg = dwc_reg_read(gusbcfg_addr, 0);
		usbcfg |= DWC_USBCFG_FRC_HST_MODE;
		dwc_reg_write(gusbcfg_addr, 0, usbcfg);

		retval = dwc_otg_hcd_init(dev, dwc_dev);
		if (retval) {
			dev_err(dev, "dwc_otg_hcd_init failed\n");
			dwc_dev->hcd = NULL;
			goto fail_hcd;
		}
		/* configure chargepump interrupt */
		dwc_dev->hcd->cp_irq = platform_get_irq_byname(ofdev, "chargepumpirq");
		if(dwc_dev->hcd->cp_irq != -ENXIO) {
			retval = request_irq(dwc_dev->hcd->cp_irq,
					     dwc_otg_externalchgpump_irq,
					     IRQF_SHARED,
					     "dwc_otg_ext_chg_pump", dwc_dev);
			if (retval) {
				dev_err(dev,
					"request of irq failed retval: %d\n",
					retval);
				retval = -EBUSY;
				goto fail_hcd;
			} else {
				dev_dbg(dev, "%s: ExtChgPump Detection "
					"IRQ registered\n", dwc_driver_name);
			}
		}
	}
	/*
	 * Enable the global interrupt after all the interrupt
	 * handlers are installed.
	 */
	dwc_otg_enable_global_interrupts(dwc_dev->core_if);

	usbcfg = dwc_reg_read(gusbcfg_addr, 0);
	usbcfg &= ~DWC_USBCFG_FRC_HST_MODE;
	dwc_reg_write(gusbcfg_addr, 0, usbcfg);

	return 0;
fail_hcd:
	free_irq(dwc_dev->irq, dwc_dev);
	if (!dwc_has_feature(dwc_dev->core_if, DWC_HOST_ONLY)) {
		if (dwc_dev->pcd)
			dwc_otg_pcd_remove(dev);
	}
fail_req_irq:
	usb_put_phy(dwc_dev->core_if->xceiv);
fail_xceiv:
	usb_nop_xceiv_unregister();
fail_check_param:
	dwc_otg_cil_remove(dwc_dev->core_if);
fail_cil_init:
	dev_set_drvdata(dev, NULL);
	iounmap(dwc_dev->base);
fail_ioremap:
	release_mem_region(dwc_dev->phys_addr, dwc_dev->base_len);
fail_of_irq:
	kfree(dwc_dev);
fail_dwc_dev:
	return retval;
}

static const struct of_device_id dwc_otg_match[] = {
	{.compatible = DWC_OTG_OF_COMPATIBLE,},
	{}
};

MODULE_DEVICE_TABLE(of, dwc_otg_match);

static struct platform_driver dwc_otg_driver = {
	.probe = dwc_otg_driver_probe,
	.remove = __devexit_p(dwc_otg_driver_remove),
	.driver = {
			.name = "dwc_otg",
			.owner = THIS_MODULE,
			.of_match_table = dwc_otg_match,
		},
};

static int __init dwc_otg_driver_init(void)
{
	return platform_driver_probe(&dwc_otg_driver, dwc_otg_driver_probe);
}

module_init(dwc_otg_driver_init);

static void __exit dwc_otg_driver_cleanup(void)
{
	platform_driver_unregister(&dwc_otg_driver);
}

module_exit(dwc_otg_driver_cleanup);

MODULE_DESCRIPTION(DWC_DRIVER_DESC);
MODULE_AUTHOR("Mark Miesfeld <mmiesfeld@apm.com");
MODULE_LICENSE("GPL");
