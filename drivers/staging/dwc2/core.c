/*
 * core.c - DesignWare HS OTG Controller common routines
 *
 * Copyright (C) 2004-2013 Synopsys, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The Core code provides basic services for accessing and managing the
 * DWC_otg hardware. These services are used by both the Host Controller
 * Driver and the Peripheral Controller Driver.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include <linux/usb/hcd.h>
#include <linux/usb/ch11.h>

#include "core.h"
#include "hcd.h"

/**
 * dwc2_enable_common_interrupts() - Initializes the commmon interrupts,
 * used in both device and host modes
 *
 * @hsotg: Programming view of the DWC_otg controller
 */
static void dwc2_enable_common_interrupts(struct dwc2_hsotg *hsotg)
{
	u32 intmsk;

	/* Clear any pending OTG Interrupts */
	writel(0xffffffff, hsotg->regs + GOTGINT);

	/* Clear any pending interrupts */
	writel(0xffffffff, hsotg->regs + GINTSTS);

	/* Enable the interrupts in the GINTMSK */
	intmsk = GINTSTS_MODEMIS | GINTSTS_OTGINT;

	if (hsotg->core_params->dma_enable <= 0)
		intmsk |= GINTSTS_RXFLVL;

	intmsk |= GINTSTS_CONIDSTSCHNG | GINTSTS_WKUPINT | GINTSTS_USBSUSP |
		  GINTSTS_SESSREQINT;

	writel(intmsk, hsotg->regs + GINTMSK);
}

/*
 * Initializes the FSLSPClkSel field of the HCFG register depending on the
 * PHY type
 */
static void dwc2_init_fs_ls_pclk_sel(struct dwc2_hsotg *hsotg)
{
	u32 hs_phy_type = hsotg->hwcfg2 & GHWCFG2_HS_PHY_TYPE_MASK;
	u32 fs_phy_type = hsotg->hwcfg2 & GHWCFG2_FS_PHY_TYPE_MASK;
	u32 hcfg, val;

	if ((hs_phy_type == GHWCFG2_HS_PHY_TYPE_ULPI &&
	     fs_phy_type == GHWCFG2_FS_PHY_TYPE_DEDICATED &&
	     hsotg->core_params->ulpi_fs_ls > 0) ||
	    hsotg->core_params->phy_type == DWC2_PHY_TYPE_PARAM_FS) {
		/* Full speed PHY */
		val = HCFG_FSLSPCLKSEL_48_MHZ;
	} else {
		/* High speed PHY running at full speed or high speed */
		val = HCFG_FSLSPCLKSEL_30_60_MHZ;
	}

	dev_dbg(hsotg->dev, "Initializing HCFG.FSLSPClkSel to %08x\n", val);
	hcfg = readl(hsotg->regs + HCFG);
	hcfg &= ~HCFG_FSLSPCLKSEL_MASK;
	hcfg |= val;
	writel(hcfg, hsotg->regs + HCFG);
}

/*
 * Do core a soft reset of the core.  Be careful with this because it
 * resets all the internal state machines of the core.
 */
static void dwc2_core_reset(struct dwc2_hsotg *hsotg)
{
	u32 greset;
	int count = 0;

	dev_vdbg(hsotg->dev, "%s()\n", __func__);

	/* Wait for AHB master IDLE state */
	do {
		usleep_range(20000, 40000);
		greset = readl(hsotg->regs + GRSTCTL);
		if (++count > 50) {
			dev_warn(hsotg->dev,
				 "%s() HANG! AHB Idle GRSTCTL=%0x\n",
				 __func__, greset);
			return;
		}
	} while (!(greset & GRSTCTL_AHBIDLE));

	/* Core Soft Reset */
	count = 0;
	greset |= GRSTCTL_CSFTRST;
	writel(greset, hsotg->regs + GRSTCTL);
	do {
		usleep_range(20000, 40000);
		greset = readl(hsotg->regs + GRSTCTL);
		if (++count > 50) {
			dev_warn(hsotg->dev,
				 "%s() HANG! Soft Reset GRSTCTL=%0x\n",
				 __func__, greset);
			break;
		}
	} while (greset & GRSTCTL_CSFTRST);

	/*
	 * NOTE: This long sleep is _very_ important, otherwise the core will
	 * not stay in host mode after a connector ID change!
	 */
	usleep_range(150000, 200000);
}

static void dwc2_fs_phy_init(struct dwc2_hsotg *hsotg, bool select_phy)
{
	u32 usbcfg, i2cctl;

	/*
	 * core_init() is now called on every switch so only call the
	 * following for the first time through
	 */
	if (select_phy) {
		dev_dbg(hsotg->dev, "FS PHY selected\n");
		usbcfg = readl(hsotg->regs + GUSBCFG);
		usbcfg |= GUSBCFG_PHYSEL;
		writel(usbcfg, hsotg->regs + GUSBCFG);

		/* Reset after a PHY select */
		dwc2_core_reset(hsotg);
	}

	/*
	 * Program DCFG.DevSpd or HCFG.FSLSPclkSel to 48Mhz in FS. Also
	 * do this on HNP Dev/Host mode switches (done in dev_init and
	 * host_init).
	 */
	if (dwc2_is_host_mode(hsotg))
		dwc2_init_fs_ls_pclk_sel(hsotg);

	if (hsotg->core_params->i2c_enable > 0) {
		dev_dbg(hsotg->dev, "FS PHY enabling I2C\n");

		/* Program GUSBCFG.OtgUtmiFsSel to I2C */
		usbcfg = readl(hsotg->regs + GUSBCFG);
		usbcfg |= GUSBCFG_OTG_UTMI_FS_SEL;
		writel(usbcfg, hsotg->regs + GUSBCFG);

		/* Program GI2CCTL.I2CEn */
		i2cctl = readl(hsotg->regs + GI2CCTL);
		i2cctl &= ~GI2CCTL_I2CDEVADDR_MASK;
		i2cctl |= 1 << GI2CCTL_I2CDEVADDR_SHIFT;
		i2cctl &= ~GI2CCTL_I2CEN;
		writel(i2cctl, hsotg->regs + GI2CCTL);
		i2cctl |= GI2CCTL_I2CEN;
		writel(i2cctl, hsotg->regs + GI2CCTL);
	}
}

static void dwc2_hs_phy_init(struct dwc2_hsotg *hsotg, bool select_phy)
{
	u32 usbcfg;

	if (!select_phy)
		return;

	usbcfg = readl(hsotg->regs + GUSBCFG);

	/*
	 * HS PHY parameters. These parameters are preserved during soft reset
	 * so only program the first time. Do a soft reset immediately after
	 * setting phyif.
	 */
	switch (hsotg->core_params->phy_type) {
	case DWC2_PHY_TYPE_PARAM_ULPI:
		/* ULPI interface */
		dev_dbg(hsotg->dev, "HS ULPI PHY selected\n");
		usbcfg |= GUSBCFG_ULPI_UTMI_SEL;
		usbcfg &= ~(GUSBCFG_PHYIF16 | GUSBCFG_DDRSEL);
		if (hsotg->core_params->phy_ulpi_ddr > 0)
			usbcfg |= GUSBCFG_DDRSEL;
		break;
	case DWC2_PHY_TYPE_PARAM_UTMI:
		/* UTMI+ interface */
		dev_dbg(hsotg->dev, "HS UTMI+ PHY selected\n");
		usbcfg &= ~(GUSBCFG_ULPI_UTMI_SEL | GUSBCFG_PHYIF16);
		if (hsotg->core_params->phy_utmi_width == 16)
			usbcfg |= GUSBCFG_PHYIF16;
		break;
	default:
		dev_err(hsotg->dev, "FS PHY selected at HS!\n");
		break;
	}

	writel(usbcfg, hsotg->regs + GUSBCFG);

	/* Reset after setting the PHY parameters */
	dwc2_core_reset(hsotg);
}

static void dwc2_phy_init(struct dwc2_hsotg *hsotg, bool select_phy)
{
	u32 usbcfg, hs_phy_type, fs_phy_type;

	if (hsotg->core_params->speed == DWC2_SPEED_PARAM_FULL &&
	    hsotg->core_params->phy_type == DWC2_PHY_TYPE_PARAM_FS) {
		/* If FS mode with FS PHY */
		dwc2_fs_phy_init(hsotg, select_phy);
	} else {
		/* High speed PHY */
		dwc2_hs_phy_init(hsotg, select_phy);
	}

	hs_phy_type = hsotg->hwcfg2 & GHWCFG2_HS_PHY_TYPE_MASK;
	fs_phy_type = hsotg->hwcfg2 & GHWCFG2_FS_PHY_TYPE_MASK;

	if (hs_phy_type == GHWCFG2_HS_PHY_TYPE_ULPI &&
	    fs_phy_type == GHWCFG2_FS_PHY_TYPE_DEDICATED &&
	    hsotg->core_params->ulpi_fs_ls > 0) {
		dev_dbg(hsotg->dev, "Setting ULPI FSLS\n");
		usbcfg = readl(hsotg->regs + GUSBCFG);
		usbcfg |= GUSBCFG_ULPI_FS_LS;
		usbcfg |= GUSBCFG_ULPI_CLK_SUSP_M;
		writel(usbcfg, hsotg->regs + GUSBCFG);
	} else {
		usbcfg = readl(hsotg->regs + GUSBCFG);
		usbcfg &= ~GUSBCFG_ULPI_FS_LS;
		usbcfg &= ~GUSBCFG_ULPI_CLK_SUSP_M;
		writel(usbcfg, hsotg->regs + GUSBCFG);
	}
}

static int dwc2_gahbcfg_init(struct dwc2_hsotg *hsotg)
{
	u32 ahbcfg = 0;

	switch (hsotg->hwcfg2 & GHWCFG2_ARCHITECTURE_MASK) {
	case GHWCFG2_EXT_DMA_ARCH:
		dev_err(hsotg->dev, "External DMA Mode not supported\n");
		return -EINVAL;

	case GHWCFG2_INT_DMA_ARCH:
		dev_dbg(hsotg->dev, "Internal DMA Mode\n");
		/*
		 * Old value was GAHBCFG_HBSTLEN_INCR - done for
		 * Host mode ISOC in issue fix - vahrama
		 */
		ahbcfg |= GAHBCFG_HBSTLEN_INCR4;
		break;

	case GHWCFG2_SLAVE_ONLY_ARCH:
	default:
		dev_dbg(hsotg->dev, "Slave Only Mode\n");
		break;
	}

	dev_dbg(hsotg->dev, "dma_enable:%d dma_desc_enable:%d\n",
		hsotg->core_params->dma_enable,
		hsotg->core_params->dma_desc_enable);

	if (hsotg->core_params->dma_enable > 0) {
		if (hsotg->core_params->dma_desc_enable > 0)
			dev_dbg(hsotg->dev, "Using Descriptor DMA mode\n");
		else
			dev_dbg(hsotg->dev, "Using Buffer DMA mode\n");
	} else {
		dev_dbg(hsotg->dev, "Using Slave mode\n");
		hsotg->core_params->dma_desc_enable = 0;
	}

	if (hsotg->core_params->ahb_single > 0)
		ahbcfg |= GAHBCFG_AHB_SINGLE;

	if (hsotg->core_params->dma_enable > 0)
		ahbcfg |= GAHBCFG_DMA_EN;

	writel(ahbcfg, hsotg->regs + GAHBCFG);

	return 0;
}

static void dwc2_gusbcfg_init(struct dwc2_hsotg *hsotg)
{
	u32 usbcfg;

	usbcfg = readl(hsotg->regs + GUSBCFG);
	usbcfg &= ~(GUSBCFG_HNPCAP | GUSBCFG_SRPCAP);

	switch (hsotg->hwcfg2 & GHWCFG2_OP_MODE_MASK) {
	case GHWCFG2_OP_MODE_HNP_SRP_CAPABLE:
		if (hsotg->core_params->otg_cap ==
				DWC2_CAP_PARAM_HNP_SRP_CAPABLE)
			usbcfg |= GUSBCFG_HNPCAP;
		if (hsotg->core_params->otg_cap !=
				DWC2_CAP_PARAM_NO_HNP_SRP_CAPABLE)
			usbcfg |= GUSBCFG_SRPCAP;
		break;

	case GHWCFG2_OP_MODE_SRP_ONLY_CAPABLE:
	case GHWCFG2_OP_MODE_SRP_CAPABLE_DEVICE:
	case GHWCFG2_OP_MODE_SRP_CAPABLE_HOST:
		if (hsotg->core_params->otg_cap !=
				DWC2_CAP_PARAM_NO_HNP_SRP_CAPABLE)
			usbcfg |= GUSBCFG_SRPCAP;
		break;

	case GHWCFG2_OP_MODE_NO_HNP_SRP_CAPABLE:
	case GHWCFG2_OP_MODE_NO_SRP_CAPABLE_DEVICE:
	case GHWCFG2_OP_MODE_NO_SRP_CAPABLE_HOST:
	default:
		break;
	}

	writel(usbcfg, hsotg->regs + GUSBCFG);
}

/**
 * dwc2_core_init() - Initializes the DWC_otg controller registers and
 * prepares the core for device mode or host mode operation
 *
 * @hsotg:      Programming view of the DWC_otg controller
 * @select_phy: If true then also set the Phy type
 * @irq:        If >= 0, the irq to register
 */
int dwc2_core_init(struct dwc2_hsotg *hsotg, bool select_phy, int irq)
{
	u32 usbcfg, otgctl;
	int retval;

	dev_dbg(hsotg->dev, "%s(%p)\n", __func__, hsotg);

	usbcfg = readl(hsotg->regs + GUSBCFG);

	/* Set ULPI External VBUS bit if needed */
	usbcfg &= ~GUSBCFG_ULPI_EXT_VBUS_DRV;
	if (hsotg->core_params->phy_ulpi_ext_vbus ==
				DWC2_PHY_ULPI_EXTERNAL_VBUS)
		usbcfg |= GUSBCFG_ULPI_EXT_VBUS_DRV;

	/* Set external TS Dline pulsing bit if needed */
	usbcfg &= ~GUSBCFG_TERMSELDLPULSE;
	if (hsotg->core_params->ts_dline > 0)
		usbcfg |= GUSBCFG_TERMSELDLPULSE;

	writel(usbcfg, hsotg->regs + GUSBCFG);

	/* Reset the Controller */
	dwc2_core_reset(hsotg);

	dev_dbg(hsotg->dev, "num_dev_perio_in_ep=%d\n",
		hsotg->hwcfg4 >> GHWCFG4_NUM_DEV_PERIO_IN_EP_SHIFT &
		GHWCFG4_NUM_DEV_PERIO_IN_EP_MASK >>
				GHWCFG4_NUM_DEV_PERIO_IN_EP_SHIFT);

	hsotg->total_fifo_size = hsotg->hwcfg3 >> GHWCFG3_DFIFO_DEPTH_SHIFT &
			GHWCFG3_DFIFO_DEPTH_MASK >> GHWCFG3_DFIFO_DEPTH_SHIFT;
	hsotg->rx_fifo_size = readl(hsotg->regs + GRXFSIZ);
	hsotg->nperio_tx_fifo_size =
			readl(hsotg->regs + GNPTXFSIZ) >> 16 & 0xffff;

	dev_dbg(hsotg->dev, "Total FIFO SZ=%d\n", hsotg->total_fifo_size);
	dev_dbg(hsotg->dev, "RxFIFO SZ=%d\n", hsotg->rx_fifo_size);
	dev_dbg(hsotg->dev, "NP TxFIFO SZ=%d\n", hsotg->nperio_tx_fifo_size);

	/*
	 * This needs to happen in FS mode before any other programming occurs
	 */
	dwc2_phy_init(hsotg, select_phy);

	/* Program the GAHBCFG Register */
	retval = dwc2_gahbcfg_init(hsotg);
	if (retval)
		return retval;

	/* Program the GUSBCFG register */
	dwc2_gusbcfg_init(hsotg);

	/* Program the GOTGCTL register */
	otgctl = readl(hsotg->regs + GOTGCTL);
	otgctl &= ~GOTGCTL_OTGVER;
	if (hsotg->core_params->otg_ver > 0)
		otgctl |= GOTGCTL_OTGVER;
	writel(otgctl, hsotg->regs + GOTGCTL);
	dev_dbg(hsotg->dev, "OTG VER PARAM: %d\n", hsotg->core_params->otg_ver);

	/* Clear the SRP success bit for FS-I2c */
	hsotg->srp_success = 0;

	if (irq >= 0) {
		dev_dbg(hsotg->dev, "registering common handler for irq%d\n",
			irq);
		retval = devm_request_irq(hsotg->dev, irq,
					  dwc2_handle_common_intr, IRQF_SHARED,
					  dev_name(hsotg->dev), hsotg);
		if (retval)
			return retval;
	}

	/* Enable common interrupts */
	dwc2_enable_common_interrupts(hsotg);

	/*
	 * Do device or host intialization based on mode during PCD and
	 * HCD initialization
	 */
	if (dwc2_is_host_mode(hsotg)) {
		dev_dbg(hsotg->dev, "Host Mode\n");
		hsotg->op_state = OTG_STATE_A_HOST;
	} else {
		dev_dbg(hsotg->dev, "Device Mode\n");
		hsotg->op_state = OTG_STATE_B_PERIPHERAL;
	}

	return 0;
}

/**
 * dwc2_enable_host_interrupts() - Enables the Host mode interrupts
 *
 * @hsotg: Programming view of DWC_otg controller
 */
void dwc2_enable_host_interrupts(struct dwc2_hsotg *hsotg)
{
	u32 intmsk;

	dev_dbg(hsotg->dev, "%s()\n", __func__);

	/* Disable all interrupts */
	writel(0, hsotg->regs + GINTMSK);
	writel(0, hsotg->regs + HAINTMSK);

	/* Clear any pending interrupts */
	writel(0xffffffff, hsotg->regs + GINTSTS);

	/* Enable the common interrupts */
	dwc2_enable_common_interrupts(hsotg);

	/* Enable host mode interrupts without disturbing common interrupts */
	intmsk = readl(hsotg->regs + GINTMSK);
	intmsk |= GINTSTS_DISCONNINT | GINTSTS_PRTINT | GINTSTS_HCHINT;
	writel(intmsk, hsotg->regs + GINTMSK);
}

/**
 * dwc2_disable_host_interrupts() - Disables the Host Mode interrupts
 *
 * @hsotg: Programming view of DWC_otg controller
 */
void dwc2_disable_host_interrupts(struct dwc2_hsotg *hsotg)
{
	u32 intmsk = readl(hsotg->regs + GINTMSK);

	/* Disable host mode interrupts without disturbing common interrupts */
	intmsk &= ~(GINTSTS_SOF | GINTSTS_PRTINT | GINTSTS_HCHINT |
		    GINTSTS_PTXFEMP | GINTSTS_NPTXFEMP);
	writel(intmsk, hsotg->regs + GINTMSK);
}

static void dwc2_config_fifos(struct dwc2_hsotg *hsotg)
{
	struct dwc2_core_params *params = hsotg->core_params;
	u32 rxfsiz, nptxfsiz, ptxfsiz, hptxfsiz, dfifocfg;

	if (!params->enable_dynamic_fifo)
		return;

	dev_dbg(hsotg->dev, "Total FIFO Size=%d\n", hsotg->total_fifo_size);
	dev_dbg(hsotg->dev, "Rx FIFO Size=%d\n", params->host_rx_fifo_size);
	dev_dbg(hsotg->dev, "NP Tx FIFO Size=%d\n",
		params->host_nperio_tx_fifo_size);
	dev_dbg(hsotg->dev, "P Tx FIFO Size=%d\n",
		params->host_perio_tx_fifo_size);

	/* Rx FIFO */
	dev_dbg(hsotg->dev, "initial grxfsiz=%08x\n",
		readl(hsotg->regs + GRXFSIZ));
	writel(params->host_rx_fifo_size, hsotg->regs + GRXFSIZ);
	dev_dbg(hsotg->dev, "new grxfsiz=%08x\n", readl(hsotg->regs + GRXFSIZ));

	/* Non-periodic Tx FIFO */
	dev_dbg(hsotg->dev, "initial gnptxfsiz=%08x\n",
		readl(hsotg->regs + GNPTXFSIZ));
	nptxfsiz = params->host_nperio_tx_fifo_size <<
		   FIFOSIZE_DEPTH_SHIFT & FIFOSIZE_DEPTH_MASK;
	nptxfsiz |= params->host_rx_fifo_size <<
		    FIFOSIZE_STARTADDR_SHIFT & FIFOSIZE_STARTADDR_MASK;
	writel(nptxfsiz, hsotg->regs + GNPTXFSIZ);
	dev_dbg(hsotg->dev, "new gnptxfsiz=%08x\n",
		readl(hsotg->regs + GNPTXFSIZ));

	/* Periodic Tx FIFO */
	dev_dbg(hsotg->dev, "initial hptxfsiz=%08x\n",
		readl(hsotg->regs + HPTXFSIZ));
	ptxfsiz = params->host_perio_tx_fifo_size <<
		  FIFOSIZE_DEPTH_SHIFT & FIFOSIZE_DEPTH_MASK;
	ptxfsiz |= (params->host_rx_fifo_size +
		    params->host_nperio_tx_fifo_size) <<
		   FIFOSIZE_STARTADDR_SHIFT & FIFOSIZE_STARTADDR_MASK;
	writel(ptxfsiz, hsotg->regs + HPTXFSIZ);
	dev_dbg(hsotg->dev, "new hptxfsiz=%08x\n",
		readl(hsotg->regs + HPTXFSIZ));

	if (hsotg->core_params->en_multiple_tx_fifo > 0 &&
	    hsotg->snpsid <= DWC2_CORE_REV_2_94a) {
		/*
		 * Global DFIFOCFG calculation for Host mode -
		 * include RxFIFO, NPTXFIFO and HPTXFIFO
		 */
		dfifocfg = readl(hsotg->regs + GDFIFOCFG);
		rxfsiz = readl(hsotg->regs + GRXFSIZ) & 0x0000ffff;
		nptxfsiz = readl(hsotg->regs + GNPTXFSIZ) >> 16 & 0xffff;
		hptxfsiz = readl(hsotg->regs + HPTXFSIZ) >> 16 & 0xffff;
		dfifocfg &= ~GDFIFOCFG_EPINFOBASE_MASK;
		dfifocfg |= (rxfsiz + nptxfsiz + hptxfsiz) <<
			    GDFIFOCFG_EPINFOBASE_SHIFT &
			    GDFIFOCFG_EPINFOBASE_MASK;
		writel(dfifocfg, hsotg->regs + GDFIFOCFG);
	}
}

/**
 * dwc2_core_host_init() - Initializes the DWC_otg controller registers for
 * Host mode
 *
 * @hsotg: Programming view of DWC_otg controller
 *
 * This function flushes the Tx and Rx FIFOs and flushes any entries in the
 * request queues. Host channels are reset to ensure that they are ready for
 * performing transfers.
 */
void dwc2_core_host_init(struct dwc2_hsotg *hsotg)
{
	u32 hcfg, hfir, otgctl;

	dev_dbg(hsotg->dev, "%s(%p)\n", __func__, hsotg);

	/* Restart the Phy Clock */
	writel(0, hsotg->regs + PCGCTL);

	/* Initialize Host Configuration Register */
	dwc2_init_fs_ls_pclk_sel(hsotg);
	if (hsotg->core_params->speed == DWC2_SPEED_PARAM_FULL) {
		hcfg = readl(hsotg->regs + HCFG);
		hcfg |= HCFG_FSLSSUPP;
		writel(hcfg, hsotg->regs + HCFG);
	}

	/*
	 * This bit allows dynamic reloading of the HFIR register during
	 * runtime. This bit needs to be programmed during inital configuration
	 * and its value must not be changed during runtime.
	 */
	if (hsotg->core_params->reload_ctl > 0) {
		hfir = readl(hsotg->regs + HFIR);
		hfir |= HFIR_RLDCTRL;
		writel(hfir, hsotg->regs + HFIR);
	}

	if (hsotg->core_params->dma_desc_enable > 0) {
		u32 op_mode = hsotg->hwcfg2 & GHWCFG2_OP_MODE_MASK;

		if (hsotg->snpsid < DWC2_CORE_REV_2_90a ||
		    !(hsotg->hwcfg4 & GHWCFG4_DESC_DMA) ||
		    op_mode == GHWCFG2_OP_MODE_SRP_CAPABLE_DEVICE ||
		    op_mode == GHWCFG2_OP_MODE_NO_SRP_CAPABLE_DEVICE ||
		    op_mode == GHWCFG2_OP_MODE_UNDEFINED) {
			dev_err(hsotg->dev,
				"Hardware does not support descriptor DMA mode -\n");
			dev_err(hsotg->dev,
				"falling back to buffer DMA mode.\n");
			hsotg->core_params->dma_desc_enable = 0;
		} else {
			hcfg = readl(hsotg->regs + HCFG);
			hcfg |= HCFG_DESCDMA;
			writel(hcfg, hsotg->regs + HCFG);
		}
	}

	/* Configure data FIFO sizes */
	dwc2_config_fifos(hsotg);

	/* TODO - check this */
	/* Clear Host Set HNP Enable in the OTG Control Register */
	otgctl = readl(hsotg->regs + GOTGCTL);
	otgctl &= ~GOTGCTL_HSTSETHNPEN;
	writel(otgctl, hsotg->regs + GOTGCTL);

	/* Make sure the FIFOs are flushed */
	dwc2_flush_tx_fifo(hsotg, 0x10 /* all TX FIFOs */);
	dwc2_flush_rx_fifo(hsotg);

	/* Clear Host Set HNP Enable in the OTG Control Register */
	otgctl = readl(hsotg->regs + GOTGCTL);
	otgctl &= ~GOTGCTL_HSTSETHNPEN;
	writel(otgctl, hsotg->regs + GOTGCTL);

	if (hsotg->core_params->dma_desc_enable <= 0) {
		int num_channels, i;
		u32 hcchar;

		/* Flush out any leftover queued requests */
		num_channels = hsotg->core_params->host_channels;
		for (i = 0; i < num_channels; i++) {
			hcchar = readl(hsotg->regs + HCCHAR(i));
			hcchar &= ~HCCHAR_CHENA;
			hcchar |= HCCHAR_CHDIS;
			hcchar &= ~HCCHAR_EPDIR;
			writel(hcchar, hsotg->regs + HCCHAR(i));
		}

		/* Halt all channels to put them into a known state */
		for (i = 0; i < num_channels; i++) {
			int count = 0;

			hcchar = readl(hsotg->regs + HCCHAR(i));
			hcchar |= HCCHAR_CHENA | HCCHAR_CHDIS;
			hcchar &= ~HCCHAR_EPDIR;
			writel(hcchar, hsotg->regs + HCCHAR(i));
			dev_dbg(hsotg->dev, "%s: Halt channel %d\n",
				__func__, i);
			do {
				hcchar = readl(hsotg->regs + HCCHAR(i));
				if (++count > 1000) {
					dev_err(hsotg->dev,
						"Unable to clear enable on channel %d\n",
						i);
					break;
				}
				udelay(1);
			} while (hcchar & HCCHAR_CHENA);
		}
	}

	/* Turn on the vbus power */
	dev_dbg(hsotg->dev, "Init: Port Power? op_state=%d\n", hsotg->op_state);
	if (hsotg->op_state == OTG_STATE_A_HOST) {
		u32 hprt0 = dwc2_read_hprt0(hsotg);

		dev_dbg(hsotg->dev, "Init: Power Port (%d)\n",
			!!(hprt0 & HPRT0_PWR));
		if (!(hprt0 & HPRT0_PWR)) {
			hprt0 |= HPRT0_PWR;
			writel(hprt0, hsotg->regs + HPRT0);
		}
	}

	dwc2_enable_host_interrupts(hsotg);
}

static void dwc2_hc_enable_slave_ints(struct dwc2_hsotg *hsotg,
				      struct dwc2_host_chan *chan)
{
	u32 hcintmsk = HCINTMSK_CHHLTD;

	switch (chan->ep_type) {
	case USB_ENDPOINT_XFER_CONTROL:
	case USB_ENDPOINT_XFER_BULK:
		dev_vdbg(hsotg->dev, "control/bulk\n");
		hcintmsk |= HCINTMSK_XFERCOMPL;
		hcintmsk |= HCINTMSK_STALL;
		hcintmsk |= HCINTMSK_XACTERR;
		hcintmsk |= HCINTMSK_DATATGLERR;
		if (chan->ep_is_in) {
			hcintmsk |= HCINTMSK_BBLERR;
		} else {
			hcintmsk |= HCINTMSK_NAK;
			hcintmsk |= HCINTMSK_NYET;
			if (chan->do_ping)
				hcintmsk |= HCINTMSK_ACK;
		}

		if (chan->do_split) {
			hcintmsk |= HCINTMSK_NAK;
			if (chan->complete_split)
				hcintmsk |= HCINTMSK_NYET;
			else
				hcintmsk |= HCINTMSK_ACK;
		}

		if (chan->error_state)
			hcintmsk |= HCINTMSK_ACK;
		break;

	case USB_ENDPOINT_XFER_INT:
		if (dbg_perio())
			dev_vdbg(hsotg->dev, "intr\n");
		hcintmsk |= HCINTMSK_XFERCOMPL;
		hcintmsk |= HCINTMSK_NAK;
		hcintmsk |= HCINTMSK_STALL;
		hcintmsk |= HCINTMSK_XACTERR;
		hcintmsk |= HCINTMSK_DATATGLERR;
		hcintmsk |= HCINTMSK_FRMOVRUN;

		if (chan->ep_is_in)
			hcintmsk |= HCINTMSK_BBLERR;
		if (chan->error_state)
			hcintmsk |= HCINTMSK_ACK;
		if (chan->do_split) {
			if (chan->complete_split)
				hcintmsk |= HCINTMSK_NYET;
			else
				hcintmsk |= HCINTMSK_ACK;
		}
		break;

	case USB_ENDPOINT_XFER_ISOC:
		if (dbg_perio())
			dev_vdbg(hsotg->dev, "isoc\n");
		hcintmsk |= HCINTMSK_XFERCOMPL;
		hcintmsk |= HCINTMSK_FRMOVRUN;
		hcintmsk |= HCINTMSK_ACK;

		if (chan->ep_is_in) {
			hcintmsk |= HCINTMSK_XACTERR;
			hcintmsk |= HCINTMSK_BBLERR;
		}
		break;
	default:
		dev_err(hsotg->dev, "## Unknown EP type ##\n");
		break;
	}

	writel(hcintmsk, hsotg->regs + HCINTMSK(chan->hc_num));
	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "set HCINTMSK to %08x\n", hcintmsk);
}

static void dwc2_hc_enable_dma_ints(struct dwc2_hsotg *hsotg,
				    struct dwc2_host_chan *chan)
{
	u32 hcintmsk = HCINTMSK_CHHLTD;

	/*
	 * For Descriptor DMA mode core halts the channel on AHB error.
	 * Interrupt is not required.
	 */
	if (hsotg->core_params->dma_desc_enable <= 0) {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "desc DMA disabled\n");
		hcintmsk |= HCINTMSK_AHBERR;
	} else {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "desc DMA enabled\n");
		if (chan->ep_type == USB_ENDPOINT_XFER_ISOC)
			hcintmsk |= HCINTMSK_XFERCOMPL;
	}

	if (chan->error_state && !chan->do_split &&
	    chan->ep_type != USB_ENDPOINT_XFER_ISOC) {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "setting ACK\n");
		hcintmsk |= HCINTMSK_ACK;
		if (chan->ep_is_in) {
			hcintmsk |= HCINTMSK_DATATGLERR;
			if (chan->ep_type != USB_ENDPOINT_XFER_INT)
				hcintmsk |= HCINTMSK_NAK;
		}
	}

	writel(hcintmsk, hsotg->regs + HCINTMSK(chan->hc_num));
	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "set HCINTMSK to %08x\n", hcintmsk);
}

static void dwc2_hc_enable_ints(struct dwc2_hsotg *hsotg,
				struct dwc2_host_chan *chan)
{
	u32 intmsk;

	if (hsotg->core_params->dma_enable > 0) {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "DMA enabled\n");
		dwc2_hc_enable_dma_ints(hsotg, chan);
	} else {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "DMA disabled\n");
		dwc2_hc_enable_slave_ints(hsotg, chan);
	}

	/* Enable the top level host channel interrupt */
	intmsk = readl(hsotg->regs + HAINTMSK);
	intmsk |= 1 << chan->hc_num;
	writel(intmsk, hsotg->regs + HAINTMSK);
	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "set HAINTMSK to %08x\n", intmsk);

	/* Make sure host channel interrupts are enabled */
	intmsk = readl(hsotg->regs + GINTMSK);
	intmsk |= GINTSTS_HCHINT;
	writel(intmsk, hsotg->regs + GINTMSK);
	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "set GINTMSK to %08x\n", intmsk);
}

/**
 * dwc2_hc_init() - Prepares a host channel for transferring packets to/from
 * a specific endpoint
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Information needed to initialize the host channel
 *
 * The HCCHARn register is set up with the characteristics specified in chan.
 * Host channel interrupts that may need to be serviced while this transfer is
 * in progress are enabled.
 */
void dwc2_hc_init(struct dwc2_hsotg *hsotg, struct dwc2_host_chan *chan)
{
	u8 hc_num = chan->hc_num;
	u32 hcintmsk;
	u32 hcchar;
	u32 hcsplt = 0;

	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "%s()\n", __func__);

	/* Clear old interrupt conditions for this host channel */
	hcintmsk = 0xffffffff;
	hcintmsk &= ~HCINTMSK_RESERVED14_31;
	writel(hcintmsk, hsotg->regs + HCINT(hc_num));

	/* Enable channel interrupts required for this transfer */
	dwc2_hc_enable_ints(hsotg, chan);

	/*
	 * Program the HCCHARn register with the endpoint characteristics for
	 * the current transfer
	 */
	hcchar = chan->dev_addr << HCCHAR_DEVADDR_SHIFT & HCCHAR_DEVADDR_MASK;
	hcchar |= chan->ep_num << HCCHAR_EPNUM_SHIFT & HCCHAR_EPNUM_MASK;
	if (chan->ep_is_in)
		hcchar |= HCCHAR_EPDIR;
	if (chan->speed == USB_SPEED_LOW)
		hcchar |= HCCHAR_LSPDDEV;
	hcchar |= chan->ep_type << HCCHAR_EPTYPE_SHIFT & HCCHAR_EPTYPE_MASK;
	hcchar |= chan->max_packet << HCCHAR_MPS_SHIFT & HCCHAR_MPS_MASK;
	writel(hcchar, hsotg->regs + HCCHAR(hc_num));
	if (dbg_hc(chan)) {
		dev_vdbg(hsotg->dev, "set HCCHAR(%d) to %08x\n",
			 hc_num, hcchar);

		dev_vdbg(hsotg->dev, "%s: Channel %d\n", __func__, hc_num);
		dev_vdbg(hsotg->dev, "	 Dev Addr: %d\n",
			 hcchar >> HCCHAR_DEVADDR_SHIFT &
			 HCCHAR_DEVADDR_MASK >> HCCHAR_DEVADDR_SHIFT);
		dev_vdbg(hsotg->dev, "	 Ep Num: %d\n",
			 hcchar >> HCCHAR_EPNUM_SHIFT &
			 HCCHAR_EPNUM_MASK >> HCCHAR_EPNUM_SHIFT);
		dev_vdbg(hsotg->dev, "	 Is In: %d\n",
			 !!(hcchar & HCCHAR_EPDIR));
		dev_vdbg(hsotg->dev, "	 Is Low Speed: %d\n",
			 !!(hcchar & HCCHAR_LSPDDEV));
		dev_vdbg(hsotg->dev, "	 Ep Type: %d\n",
			 hcchar >> HCCHAR_EPTYPE_SHIFT &
			 HCCHAR_EPTYPE_MASK >> HCCHAR_EPTYPE_SHIFT);
		dev_vdbg(hsotg->dev, "	 Max Pkt: %d\n",
			 hcchar >> HCCHAR_MPS_SHIFT &
			 HCCHAR_MPS_MASK >> HCCHAR_MPS_SHIFT);
		dev_vdbg(hsotg->dev, "	 Multi Cnt: %d\n",
			 hcchar >> HCCHAR_MULTICNT_SHIFT &
			 HCCHAR_MULTICNT_MASK >> HCCHAR_MULTICNT_SHIFT);
	}

	/* Program the HCSPLT register for SPLITs */
	if (chan->do_split) {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev,
				 "Programming HC %d with split --> %s\n",
				 hc_num,
				 chan->complete_split ? "CSPLIT" : "SSPLIT");
		if (chan->complete_split)
			hcsplt |= HCSPLT_COMPSPLT;
		hcsplt |= chan->xact_pos << HCSPLT_XACTPOS_SHIFT &
			  HCSPLT_XACTPOS_MASK;
		hcsplt |= chan->hub_addr << HCSPLT_HUBADDR_SHIFT &
			  HCSPLT_HUBADDR_MASK;
		hcsplt |= chan->hub_port << HCSPLT_PRTADDR_SHIFT &
			  HCSPLT_PRTADDR_MASK;
		if (dbg_hc(chan)) {
			dev_vdbg(hsotg->dev, "	  comp split %d\n",
				 chan->complete_split);
			dev_vdbg(hsotg->dev, "	  xact pos %d\n",
				 chan->xact_pos);
			dev_vdbg(hsotg->dev, "	  hub addr %d\n",
				 chan->hub_addr);
			dev_vdbg(hsotg->dev, "	  hub port %d\n",
				 chan->hub_port);
			dev_vdbg(hsotg->dev, "	  is_in %d\n",
				 chan->ep_is_in);
			dev_vdbg(hsotg->dev, "	  Max Pkt %d\n",
				 hcchar >> HCCHAR_MPS_SHIFT &
				 HCCHAR_MPS_MASK >> HCCHAR_MPS_SHIFT);
			dev_vdbg(hsotg->dev, "	  xferlen %d\n",
				 chan->xfer_len);
		}
	}

	writel(hcsplt, hsotg->regs + HCSPLT(hc_num));
}

/**
 * dwc2_hc_halt() - Attempts to halt a host channel
 *
 * @hsotg:       Controller register interface
 * @chan:        Host channel to halt
 * @halt_status: Reason for halting the channel
 *
 * This function should only be called in Slave mode or to abort a transfer in
 * either Slave mode or DMA mode. Under normal circumstances in DMA mode, the
 * controller halts the channel when the transfer is complete or a condition
 * occurs that requires application intervention.
 *
 * In slave mode, checks for a free request queue entry, then sets the Channel
 * Enable and Channel Disable bits of the Host Channel Characteristics
 * register of the specified channel to intiate the halt. If there is no free
 * request queue entry, sets only the Channel Disable bit of the HCCHARn
 * register to flush requests for this channel. In the latter case, sets a
 * flag to indicate that the host channel needs to be halted when a request
 * queue slot is open.
 *
 * In DMA mode, always sets the Channel Enable and Channel Disable bits of the
 * HCCHARn register. The controller ensures there is space in the request
 * queue before submitting the halt request.
 *
 * Some time may elapse before the core flushes any posted requests for this
 * host channel and halts. The Channel Halted interrupt handler completes the
 * deactivation of the host channel.
 */
void dwc2_hc_halt(struct dwc2_hsotg *hsotg, struct dwc2_host_chan *chan,
		  enum dwc2_halt_status halt_status)
{
	u32 nptxsts, hptxsts, hcchar;

	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "%s()\n", __func__);
	if (halt_status == DWC2_HC_XFER_NO_HALT_STATUS)
		dev_err(hsotg->dev, "!!! halt_status = %d !!!\n", halt_status);

	if (halt_status == DWC2_HC_XFER_URB_DEQUEUE ||
	    halt_status == DWC2_HC_XFER_AHB_ERR) {
		/*
		 * Disable all channel interrupts except Ch Halted. The QTD
		 * and QH state associated with this transfer has been cleared
		 * (in the case of URB_DEQUEUE), so the channel needs to be
		 * shut down carefully to prevent crashes.
		 */
		u32 hcintmsk = HCINTMSK_CHHLTD;

		dev_vdbg(hsotg->dev, "dequeue/error\n");
		writel(hcintmsk, hsotg->regs + HCINTMSK(chan->hc_num));

		/*
		 * Make sure no other interrupts besides halt are currently
		 * pending. Handling another interrupt could cause a crash due
		 * to the QTD and QH state.
		 */
		writel(~hcintmsk, hsotg->regs + HCINT(chan->hc_num));

		/*
		 * Make sure the halt status is set to URB_DEQUEUE or AHB_ERR
		 * even if the channel was already halted for some other
		 * reason
		 */
		chan->halt_status = halt_status;

		hcchar = readl(hsotg->regs + HCCHAR(chan->hc_num));
		if (!(hcchar & HCCHAR_CHENA)) {
			/*
			 * The channel is either already halted or it hasn't
			 * started yet. In DMA mode, the transfer may halt if
			 * it finishes normally or a condition occurs that
			 * requires driver intervention. Don't want to halt
			 * the channel again. In either Slave or DMA mode,
			 * it's possible that the transfer has been assigned
			 * to a channel, but not started yet when an URB is
			 * dequeued. Don't want to halt a channel that hasn't
			 * started yet.
			 */
			return;
		}
	}
	if (chan->halt_pending) {
		/*
		 * A halt has already been issued for this channel. This might
		 * happen when a transfer is aborted by a higher level in
		 * the stack.
		 */
		dev_vdbg(hsotg->dev,
			 "*** %s: Channel %d, chan->halt_pending already set ***\n",
			 __func__, chan->hc_num);
		return;
	}

	hcchar = readl(hsotg->regs + HCCHAR(chan->hc_num));

	/* No need to set the bit in DDMA for disabling the channel */
	/* TODO check it everywhere channel is disabled */
	if (hsotg->core_params->dma_desc_enable <= 0) {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "desc DMA disabled\n");
		hcchar |= HCCHAR_CHENA;
	} else {
		if (dbg_hc(chan))
			dev_dbg(hsotg->dev, "desc DMA enabled\n");
	}
	hcchar |= HCCHAR_CHDIS;

	if (hsotg->core_params->dma_enable <= 0) {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "DMA not enabled\n");
		hcchar |= HCCHAR_CHENA;

		/* Check for space in the request queue to issue the halt */
		if (chan->ep_type == USB_ENDPOINT_XFER_CONTROL ||
		    chan->ep_type == USB_ENDPOINT_XFER_BULK) {
			dev_vdbg(hsotg->dev, "control/bulk\n");
			nptxsts = readl(hsotg->regs + GNPTXSTS);
			if ((nptxsts & TXSTS_QSPCAVAIL_MASK) == 0) {
				dev_vdbg(hsotg->dev, "Disabling channel\n");
				hcchar &= ~HCCHAR_CHENA;
			}
		} else {
			if (dbg_perio())
				dev_vdbg(hsotg->dev, "isoc/intr\n");
			hptxsts = readl(hsotg->regs + HPTXSTS);
			if ((hptxsts & TXSTS_QSPCAVAIL_MASK) == 0 ||
			    hsotg->queuing_high_bandwidth) {
				if (dbg_perio())
					dev_vdbg(hsotg->dev, "Disabling channel\n");
				hcchar &= ~HCCHAR_CHENA;
			}
		}
	} else {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "DMA enabled\n");
	}

	writel(hcchar, hsotg->regs + HCCHAR(chan->hc_num));
	chan->halt_status = halt_status;

	if (hcchar & HCCHAR_CHENA) {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "Channel enabled\n");
		chan->halt_pending = 1;
		chan->halt_on_queue = 0;
	} else {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "Channel disabled\n");
		chan->halt_on_queue = 1;
	}

	if (dbg_hc(chan)) {
		dev_vdbg(hsotg->dev, "%s: Channel %d\n", __func__,
			 chan->hc_num);
		dev_vdbg(hsotg->dev, "	 hcchar: 0x%08x\n",
			 hcchar);
		dev_vdbg(hsotg->dev, "	 halt_pending: %d\n",
			 chan->halt_pending);
		dev_vdbg(hsotg->dev, "	 halt_on_queue: %d\n",
			 chan->halt_on_queue);
		dev_vdbg(hsotg->dev, "	 halt_status: %d\n",
			 chan->halt_status);
	}
}

/**
 * dwc2_hc_cleanup() - Clears the transfer state for a host channel
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Identifies the host channel to clean up
 *
 * This function is normally called after a transfer is done and the host
 * channel is being released
 */
void dwc2_hc_cleanup(struct dwc2_hsotg *hsotg, struct dwc2_host_chan *chan)
{
	u32 hcintmsk;

	chan->xfer_started = 0;

	/*
	 * Clear channel interrupt enables and any unhandled channel interrupt
	 * conditions
	 */
	writel(0, hsotg->regs + HCINTMSK(chan->hc_num));
	hcintmsk = 0xffffffff;
	hcintmsk &= ~HCINTMSK_RESERVED14_31;
	writel(hcintmsk, hsotg->regs + HCINT(chan->hc_num));
}

/**
 * dwc2_hc_set_even_odd_frame() - Sets the channel property that indicates in
 * which frame a periodic transfer should occur
 *
 * @hsotg:  Programming view of DWC_otg controller
 * @chan:   Identifies the host channel to set up and its properties
 * @hcchar: Current value of the HCCHAR register for the specified host channel
 *
 * This function has no effect on non-periodic transfers
 */
static void dwc2_hc_set_even_odd_frame(struct dwc2_hsotg *hsotg,
				       struct dwc2_host_chan *chan, u32 *hcchar)
{
	if (chan->ep_type == USB_ENDPOINT_XFER_INT ||
	    chan->ep_type == USB_ENDPOINT_XFER_ISOC) {
		/* 1 if _next_ frame is odd, 0 if it's even */
		if (!(dwc2_hcd_get_frame_number(hsotg) & 0x1))
			*hcchar |= HCCHAR_ODDFRM;
	}
}

static void dwc2_set_pid_isoc(struct dwc2_host_chan *chan)
{
	/* Set up the initial PID for the transfer */
	if (chan->speed == USB_SPEED_HIGH) {
		if (chan->ep_is_in) {
			if (chan->multi_count == 1)
				chan->data_pid_start = DWC2_HC_PID_DATA0;
			else if (chan->multi_count == 2)
				chan->data_pid_start = DWC2_HC_PID_DATA1;
			else
				chan->data_pid_start = DWC2_HC_PID_DATA2;
		} else {
			if (chan->multi_count == 1)
				chan->data_pid_start = DWC2_HC_PID_DATA0;
			else
				chan->data_pid_start = DWC2_HC_PID_MDATA;
		}
	} else {
		chan->data_pid_start = DWC2_HC_PID_DATA0;
	}
}

/**
 * dwc2_hc_write_packet() - Writes a packet into the Tx FIFO associated with
 * the Host Channel
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Information needed to initialize the host channel
 *
 * This function should only be called in Slave mode. For a channel associated
 * with a non-periodic EP, the non-periodic Tx FIFO is written. For a channel
 * associated with a periodic EP, the periodic Tx FIFO is written.
 *
 * Upon return the xfer_buf and xfer_count fields in chan are incremented by
 * the number of bytes written to the Tx FIFO.
 */
static void dwc2_hc_write_packet(struct dwc2_hsotg *hsotg,
				 struct dwc2_host_chan *chan)
{
	u32 i;
	u32 remaining_count;
	u32 byte_count;
	u32 dword_count;
	u32 __iomem *data_fifo;
	u32 *data_buf = (u32 *)chan->xfer_buf;

	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "%s()\n", __func__);

	data_fifo = (u32 __iomem *)(hsotg->regs + HCFIFO(chan->hc_num));

	remaining_count = chan->xfer_len - chan->xfer_count;
	if (remaining_count > chan->max_packet)
		byte_count = chan->max_packet;
	else
		byte_count = remaining_count;

	dword_count = (byte_count + 3) / 4;

	if (((unsigned long)data_buf & 0x3) == 0) {
		/* xfer_buf is DWORD aligned */
		for (i = 0; i < dword_count; i++, data_buf++)
			writel(*data_buf, data_fifo);
	} else {
		/* xfer_buf is not DWORD aligned */
		for (i = 0; i < dword_count; i++, data_buf++) {
			u32 data = data_buf[0] | data_buf[1] << 8 |
				   data_buf[2] << 16 | data_buf[3] << 24;
			writel(data, data_fifo);
		}
	}

	chan->xfer_count += byte_count;
	chan->xfer_buf += byte_count;
}

/**
 * dwc2_hc_start_transfer() - Does the setup for a data transfer for a host
 * channel and starts the transfer
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Information needed to initialize the host channel. The xfer_len value
 *         may be reduced to accommodate the max widths of the XferSize and
 *         PktCnt fields in the HCTSIZn register. The multi_count value may be
 *         changed to reflect the final xfer_len value.
 *
 * This function may be called in either Slave mode or DMA mode. In Slave mode,
 * the caller must ensure that there is sufficient space in the request queue
 * and Tx Data FIFO.
 *
 * For an OUT transfer in Slave mode, it loads a data packet into the
 * appropriate FIFO. If necessary, additional data packets are loaded in the
 * Host ISR.
 *
 * For an IN transfer in Slave mode, a data packet is requested. The data
 * packets are unloaded from the Rx FIFO in the Host ISR. If necessary,
 * additional data packets are requested in the Host ISR.
 *
 * For a PING transfer in Slave mode, the Do Ping bit is set in the HCTSIZ
 * register along with a packet count of 1 and the channel is enabled. This
 * causes a single PING transaction to occur. Other fields in HCTSIZ are
 * simply set to 0 since no data transfer occurs in this case.
 *
 * For a PING transfer in DMA mode, the HCTSIZ register is initialized with
 * all the information required to perform the subsequent data transfer. In
 * addition, the Do Ping bit is set in the HCTSIZ register. In this case, the
 * controller performs the entire PING protocol, then starts the data
 * transfer.
 */
void dwc2_hc_start_transfer(struct dwc2_hsotg *hsotg,
			    struct dwc2_host_chan *chan)
{
	u32 max_hc_xfer_size = hsotg->core_params->max_transfer_size;
	u16 max_hc_pkt_count = hsotg->core_params->max_packet_count;
	u32 hcchar;
	u32 hctsiz = 0;
	u16 num_packets;

	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "%s()\n", __func__);

	if (chan->do_ping) {
		if (hsotg->core_params->dma_enable <= 0) {
			if (dbg_hc(chan))
				dev_vdbg(hsotg->dev, "ping, no DMA\n");
			dwc2_hc_do_ping(hsotg, chan);
			chan->xfer_started = 1;
			return;
		} else {
			if (dbg_hc(chan))
				dev_vdbg(hsotg->dev, "ping, DMA\n");
			hctsiz |= TSIZ_DOPNG;
		}
	}

	if (chan->do_split) {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "split\n");
		num_packets = 1;

		if (chan->complete_split && !chan->ep_is_in)
			/*
			 * For CSPLIT OUT Transfer, set the size to 0 so the
			 * core doesn't expect any data written to the FIFO
			 */
			chan->xfer_len = 0;
		else if (chan->ep_is_in || chan->xfer_len > chan->max_packet)
			chan->xfer_len = chan->max_packet;
		else if (!chan->ep_is_in && chan->xfer_len > 188)
			chan->xfer_len = 188;

		hctsiz |= chan->xfer_len << TSIZ_XFERSIZE_SHIFT &
			  TSIZ_XFERSIZE_MASK;
	} else {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "no split\n");
		/*
		 * Ensure that the transfer length and packet count will fit
		 * in the widths allocated for them in the HCTSIZn register
		 */
		if (chan->ep_type == USB_ENDPOINT_XFER_INT ||
		    chan->ep_type == USB_ENDPOINT_XFER_ISOC) {
			/*
			 * Make sure the transfer size is no larger than one
			 * (micro)frame's worth of data. (A check was done
			 * when the periodic transfer was accepted to ensure
			 * that a (micro)frame's worth of data can be
			 * programmed into a channel.)
			 */
			u32 max_periodic_len =
				chan->multi_count * chan->max_packet;

			if (chan->xfer_len > max_periodic_len)
				chan->xfer_len = max_periodic_len;
		} else if (chan->xfer_len > max_hc_xfer_size) {
			/*
			 * Make sure that xfer_len is a multiple of max packet
			 * size
			 */
			chan->xfer_len =
				max_hc_xfer_size - chan->max_packet + 1;
		}

		if (chan->xfer_len > 0) {
			num_packets = (chan->xfer_len + chan->max_packet - 1) /
					chan->max_packet;
			if (num_packets > max_hc_pkt_count) {
				num_packets = max_hc_pkt_count;
				chan->xfer_len = num_packets * chan->max_packet;
			}
		} else {
			/* Need 1 packet for transfer length of 0 */
			num_packets = 1;
		}

		if (chan->ep_is_in)
			/*
			 * Always program an integral # of max packets for IN
			 * transfers
			 */
			chan->xfer_len = num_packets * chan->max_packet;

		if (chan->ep_type == USB_ENDPOINT_XFER_INT ||
		    chan->ep_type == USB_ENDPOINT_XFER_ISOC)
			/*
			 * Make sure that the multi_count field matches the
			 * actual transfer length
			 */
			chan->multi_count = num_packets;

		if (chan->ep_type == USB_ENDPOINT_XFER_ISOC)
			dwc2_set_pid_isoc(chan);

		hctsiz |= chan->xfer_len << TSIZ_XFERSIZE_SHIFT &
			  TSIZ_XFERSIZE_MASK;
	}

	chan->start_pkt_count = num_packets;
	hctsiz |= num_packets << TSIZ_PKTCNT_SHIFT & TSIZ_PKTCNT_MASK;
	hctsiz |= chan->data_pid_start << TSIZ_SC_MC_PID_SHIFT &
		  TSIZ_SC_MC_PID_MASK;
	writel(hctsiz, hsotg->regs + HCTSIZ(chan->hc_num));
	if (dbg_hc(chan)) {
		dev_vdbg(hsotg->dev, "Wrote %08x to HCTSIZ(%d)\n",
			 hctsiz, chan->hc_num);

		dev_vdbg(hsotg->dev, "%s: Channel %d\n", __func__,
			 chan->hc_num);
		dev_vdbg(hsotg->dev, "	 Xfer Size: %d\n",
			 hctsiz >> TSIZ_XFERSIZE_SHIFT &
			 TSIZ_XFERSIZE_MASK >> TSIZ_XFERSIZE_SHIFT);
		dev_vdbg(hsotg->dev, "	 Num Pkts: %d\n",
			 hctsiz >> TSIZ_PKTCNT_SHIFT &
			 TSIZ_PKTCNT_MASK >> TSIZ_PKTCNT_SHIFT);
		dev_vdbg(hsotg->dev, "	 Start PID: %d\n",
			 hctsiz >> TSIZ_SC_MC_PID_SHIFT &
			 TSIZ_SC_MC_PID_MASK >> TSIZ_SC_MC_PID_SHIFT);
	}

	if (hsotg->core_params->dma_enable > 0) {
		dma_addr_t dma_addr;

		if (chan->align_buf) {
			if (dbg_hc(chan))
				dev_vdbg(hsotg->dev, "align_buf\n");
			dma_addr = chan->align_buf;
		} else {
			dma_addr = chan->xfer_dma;
		}
		writel((u32)dma_addr, hsotg->regs + HCDMA(chan->hc_num));
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "Wrote %08lx to HCDMA(%d)\n",
				 (unsigned long)dma_addr, chan->hc_num);
	}

	/* Start the split */
	if (chan->do_split) {
		u32 hcsplt = readl(hsotg->regs + HCSPLT(chan->hc_num));

		hcsplt |= HCSPLT_SPLTENA;
		writel(hcsplt, hsotg->regs + HCSPLT(chan->hc_num));
	}

	hcchar = readl(hsotg->regs + HCCHAR(chan->hc_num));
	hcchar &= ~HCCHAR_MULTICNT_MASK;
	hcchar |= chan->multi_count << HCCHAR_MULTICNT_SHIFT &
		  HCCHAR_MULTICNT_MASK;
	dwc2_hc_set_even_odd_frame(hsotg, chan, &hcchar);

	if (hcchar & HCCHAR_CHDIS)
		dev_warn(hsotg->dev,
			 "%s: chdis set, channel %d, hcchar 0x%08x\n",
			 __func__, chan->hc_num, hcchar);

	/* Set host channel enable after all other setup is complete */
	hcchar |= HCCHAR_CHENA;
	hcchar &= ~HCCHAR_CHDIS;

	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "	 Multi Cnt: %d\n",
			 hcchar >> HCCHAR_MULTICNT_SHIFT &
			 HCCHAR_MULTICNT_MASK >> HCCHAR_MULTICNT_SHIFT);

	writel(hcchar, hsotg->regs + HCCHAR(chan->hc_num));
	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "Wrote %08x to HCCHAR(%d)\n", hcchar,
			 chan->hc_num);

	chan->xfer_started = 1;
	chan->requests++;

	if (hsotg->core_params->dma_enable <= 0 &&
	    !chan->ep_is_in && chan->xfer_len > 0)
		/* Load OUT packet into the appropriate Tx FIFO */
		dwc2_hc_write_packet(hsotg, chan);
}

/**
 * dwc2_hc_start_transfer_ddma() - Does the setup for a data transfer for a
 * host channel and starts the transfer in Descriptor DMA mode
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Information needed to initialize the host channel
 *
 * Initializes HCTSIZ register. For a PING transfer the Do Ping bit is set.
 * Sets PID and NTD values. For periodic transfers initializes SCHED_INFO field
 * with micro-frame bitmap.
 *
 * Initializes HCDMA register with descriptor list address and CTD value then
 * starts the transfer via enabling the channel.
 */
void dwc2_hc_start_transfer_ddma(struct dwc2_hsotg *hsotg,
				 struct dwc2_host_chan *chan)
{
	u32 hcchar;
	u32 hc_dma;
	u32 hctsiz = 0;

	if (chan->do_ping)
		hctsiz |= TSIZ_DOPNG;

	if (chan->ep_type == USB_ENDPOINT_XFER_ISOC)
		dwc2_set_pid_isoc(chan);

	/* Packet Count and Xfer Size are not used in Descriptor DMA mode */
	hctsiz |= chan->data_pid_start << TSIZ_SC_MC_PID_SHIFT &
		  TSIZ_SC_MC_PID_MASK;

	/* 0 - 1 descriptor, 1 - 2 descriptors, etc */
	hctsiz |= (chan->ntd - 1) << TSIZ_NTD_SHIFT & TSIZ_NTD_MASK;

	/* Non-zero only for high-speed interrupt endpoints */
	hctsiz |= chan->schinfo << TSIZ_SCHINFO_SHIFT & TSIZ_SCHINFO_MASK;

	if (dbg_hc(chan)) {
		dev_vdbg(hsotg->dev, "%s: Channel %d\n", __func__,
			 chan->hc_num);
		dev_vdbg(hsotg->dev, "	 Start PID: %d\n",
			 chan->data_pid_start);
		dev_vdbg(hsotg->dev, "	 NTD: %d\n", chan->ntd - 1);
	}

	writel(hctsiz, hsotg->regs + HCTSIZ(chan->hc_num));

	hc_dma = (u32)chan->desc_list_addr & HCDMA_DMA_ADDR_MASK;

	/* Always start from first descriptor */
	hc_dma &= ~HCDMA_CTD_MASK;
	writel(hc_dma, hsotg->regs + HCDMA(chan->hc_num));
	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "Wrote %08x to HCDMA(%d)\n",
			 hc_dma, chan->hc_num);

	hcchar = readl(hsotg->regs + HCCHAR(chan->hc_num));
	hcchar &= ~HCCHAR_MULTICNT_MASK;
	hcchar |= chan->multi_count << HCCHAR_MULTICNT_SHIFT &
		  HCCHAR_MULTICNT_MASK;

	if (hcchar & HCCHAR_CHDIS)
		dev_warn(hsotg->dev,
			 "%s: chdis set, channel %d, hcchar 0x%08x\n",
			 __func__, chan->hc_num, hcchar);

	/* Set host channel enable after all other setup is complete */
	hcchar |= HCCHAR_CHENA;
	hcchar &= ~HCCHAR_CHDIS;

	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "	 Multi Cnt: %d\n",
			 hcchar >> HCCHAR_MULTICNT_SHIFT &
			 HCCHAR_MULTICNT_MASK >> HCCHAR_MULTICNT_SHIFT);

	writel(hcchar, hsotg->regs + HCCHAR(chan->hc_num));
	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "Wrote %08x to HCCHAR(%d)\n", hcchar,
			 chan->hc_num);

	chan->xfer_started = 1;
	chan->requests++;
}

/**
 * dwc2_hc_continue_transfer() - Continues a data transfer that was started by
 * a previous call to dwc2_hc_start_transfer()
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Information needed to initialize the host channel
 *
 * The caller must ensure there is sufficient space in the request queue and Tx
 * Data FIFO. This function should only be called in Slave mode. In DMA mode,
 * the controller acts autonomously to complete transfers programmed to a host
 * channel.
 *
 * For an OUT transfer, a new data packet is loaded into the appropriate FIFO
 * if there is any data remaining to be queued. For an IN transfer, another
 * data packet is always requested. For the SETUP phase of a control transfer,
 * this function does nothing.
 *
 * Return: 1 if a new request is queued, 0 if no more requests are required
 * for this transfer
 */
int dwc2_hc_continue_transfer(struct dwc2_hsotg *hsotg,
			      struct dwc2_host_chan *chan)
{
	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "%s: Channel %d\n", __func__,
			 chan->hc_num);

	if (chan->do_split)
		/* SPLITs always queue just once per channel */
		return 0;

	if (chan->data_pid_start == DWC2_HC_PID_SETUP)
		/* SETUPs are queued only once since they can't be NAK'd */
		return 0;

	if (chan->ep_is_in) {
		/*
		 * Always queue another request for other IN transfers. If
		 * back-to-back INs are issued and NAKs are received for both,
		 * the driver may still be processing the first NAK when the
		 * second NAK is received. When the interrupt handler clears
		 * the NAK interrupt for the first NAK, the second NAK will
		 * not be seen. So we can't depend on the NAK interrupt
		 * handler to requeue a NAK'd request. Instead, IN requests
		 * are issued each time this function is called. When the
		 * transfer completes, the extra requests for the channel will
		 * be flushed.
		 */
		u32 hcchar = readl(hsotg->regs + HCCHAR(chan->hc_num));

		dwc2_hc_set_even_odd_frame(hsotg, chan, &hcchar);
		hcchar |= HCCHAR_CHENA;
		hcchar &= ~HCCHAR_CHDIS;
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "	 IN xfer: hcchar = 0x%08x\n",
				 hcchar);
		writel(hcchar, hsotg->regs + HCCHAR(chan->hc_num));
		chan->requests++;
		return 1;
	}

	/* OUT transfers */

	if (chan->xfer_count < chan->xfer_len) {
		if (chan->ep_type == USB_ENDPOINT_XFER_INT ||
		    chan->ep_type == USB_ENDPOINT_XFER_ISOC) {
			u32 hcchar = readl(hsotg->regs +
					   HCCHAR(chan->hc_num));

			dwc2_hc_set_even_odd_frame(hsotg, chan,
						   &hcchar);
		}

		/* Load OUT packet into the appropriate Tx FIFO */
		dwc2_hc_write_packet(hsotg, chan);
		chan->requests++;
		return 1;
	}

	return 0;
}

/**
 * dwc2_hc_do_ping() - Starts a PING transfer
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Information needed to initialize the host channel
 *
 * This function should only be called in Slave mode. The Do Ping bit is set in
 * the HCTSIZ register, then the channel is enabled.
 */
void dwc2_hc_do_ping(struct dwc2_hsotg *hsotg, struct dwc2_host_chan *chan)
{
	u32 hcchar;
	u32 hctsiz;

	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "%s: Channel %d\n", __func__,
			 chan->hc_num);


	hctsiz = TSIZ_DOPNG;
	hctsiz |= 1 << TSIZ_PKTCNT_SHIFT;
	writel(hctsiz, hsotg->regs + HCTSIZ(chan->hc_num));

	hcchar = readl(hsotg->regs + HCCHAR(chan->hc_num));
	hcchar |= HCCHAR_CHENA;
	hcchar &= ~HCCHAR_CHDIS;
	writel(hcchar, hsotg->regs + HCCHAR(chan->hc_num));
}

/**
 * dwc2_calc_frame_interval() - Calculates the correct frame Interval value for
 * the HFIR register according to PHY type and speed
 *
 * @hsotg: Programming view of DWC_otg controller
 *
 * NOTE: The caller can modify the value of the HFIR register only after the
 * Port Enable bit of the Host Port Control and Status register (HPRT.EnaPort)
 * has been set
 */
u32 dwc2_calc_frame_interval(struct dwc2_hsotg *hsotg)
{
	u32 usbcfg;
	u32 hwcfg2;
	u32 hprt0;
	int clock = 60;	/* default value */

	usbcfg = readl(hsotg->regs + GUSBCFG);
	hwcfg2 = readl(hsotg->regs + GHWCFG2);
	hprt0 = readl(hsotg->regs + HPRT0);

	if (!(usbcfg & GUSBCFG_PHYSEL) && (usbcfg & GUSBCFG_ULPI_UTMI_SEL) &&
	    !(usbcfg & GUSBCFG_PHYIF16))
		clock = 60;
	if ((usbcfg & GUSBCFG_PHYSEL) && (hwcfg2 & GHWCFG2_FS_PHY_TYPE_MASK) ==
	    GHWCFG2_FS_PHY_TYPE_SHARED_ULPI)
		clock = 48;
	if (!(usbcfg & GUSBCFG_PHY_LP_CLK_SEL) && !(usbcfg & GUSBCFG_PHYSEL) &&
	    !(usbcfg & GUSBCFG_ULPI_UTMI_SEL) && (usbcfg & GUSBCFG_PHYIF16))
		clock = 30;
	if (!(usbcfg & GUSBCFG_PHY_LP_CLK_SEL) && !(usbcfg & GUSBCFG_PHYSEL) &&
	    !(usbcfg & GUSBCFG_ULPI_UTMI_SEL) && !(usbcfg & GUSBCFG_PHYIF16))
		clock = 60;
	if ((usbcfg & GUSBCFG_PHY_LP_CLK_SEL) && !(usbcfg & GUSBCFG_PHYSEL) &&
	    !(usbcfg & GUSBCFG_ULPI_UTMI_SEL) && (usbcfg & GUSBCFG_PHYIF16))
		clock = 48;
	if ((usbcfg & GUSBCFG_PHYSEL) && !(usbcfg & GUSBCFG_PHYIF16) &&
	    (hwcfg2 & GHWCFG2_FS_PHY_TYPE_MASK) ==
	    GHWCFG2_FS_PHY_TYPE_SHARED_UTMI)
		clock = 48;
	if ((usbcfg & GUSBCFG_PHYSEL) && (hwcfg2 & GHWCFG2_FS_PHY_TYPE_MASK) ==
	    GHWCFG2_FS_PHY_TYPE_DEDICATED)
		clock = 48;

	if ((hprt0 & HPRT0_SPD_MASK) == HPRT0_SPD_HIGH_SPEED)
		/* High speed case */
		return 125 * clock;
	else
		/* FS/LS case */
		return 1000 * clock;
}

/**
 * dwc2_read_packet() - Reads a packet from the Rx FIFO into the destination
 * buffer
 *
 * @core_if: Programming view of DWC_otg controller
 * @dest:    Destination buffer for the packet
 * @bytes:   Number of bytes to copy to the destination
 */
void dwc2_read_packet(struct dwc2_hsotg *hsotg, u8 *dest, u16 bytes)
{
	u32 __iomem *fifo = hsotg->regs + HCFIFO(0);
	u32 *data_buf = (u32 *)dest;
	int word_count = (bytes + 3) / 4;
	int i;

	/*
	 * Todo: Account for the case where dest is not dword aligned. This
	 * requires reading data from the FIFO into a u32 temp buffer, then
	 * moving it into the data buffer.
	 */

	dev_vdbg(hsotg->dev, "%s(%p,%p,%d)\n", __func__, hsotg, dest, bytes);

	for (i = 0; i < word_count; i++, data_buf++)
		*data_buf = readl(fifo);
}

/**
 * dwc2_dump_host_registers() - Prints the host registers
 *
 * @hsotg: Programming view of DWC_otg controller
 *
 * NOTE: This function will be removed once the peripheral controller code
 * is integrated and the driver is stable
 */
void dwc2_dump_host_registers(struct dwc2_hsotg *hsotg)
{
#ifdef DEBUG
	u32 __iomem *addr;
	int i;

	dev_dbg(hsotg->dev, "Host Global Registers\n");
	addr = hsotg->regs + HCFG;
	dev_dbg(hsotg->dev, "HCFG	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + HFIR;
	dev_dbg(hsotg->dev, "HFIR	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + HFNUM;
	dev_dbg(hsotg->dev, "HFNUM	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + HPTXSTS;
	dev_dbg(hsotg->dev, "HPTXSTS	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + HAINT;
	dev_dbg(hsotg->dev, "HAINT	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + HAINTMSK;
	dev_dbg(hsotg->dev, "HAINTMSK	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	if (hsotg->core_params->dma_desc_enable > 0) {
		addr = hsotg->regs + HFLBADDR;
		dev_dbg(hsotg->dev, "HFLBADDR @0x%08lX : 0x%08X\n",
			(unsigned long)addr, readl(addr));
	}

	addr = hsotg->regs + HPRT0;
	dev_dbg(hsotg->dev, "HPRT0	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));

	for (i = 0; i < hsotg->core_params->host_channels; i++) {
		dev_dbg(hsotg->dev, "Host Channel %d Specific Registers\n", i);
		addr = hsotg->regs + HCCHAR(i);
		dev_dbg(hsotg->dev, "HCCHAR	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, readl(addr));
		addr = hsotg->regs + HCSPLT(i);
		dev_dbg(hsotg->dev, "HCSPLT	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, readl(addr));
		addr = hsotg->regs + HCINT(i);
		dev_dbg(hsotg->dev, "HCINT	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, readl(addr));
		addr = hsotg->regs + HCINTMSK(i);
		dev_dbg(hsotg->dev, "HCINTMSK	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, readl(addr));
		addr = hsotg->regs + HCTSIZ(i);
		dev_dbg(hsotg->dev, "HCTSIZ	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, readl(addr));
		addr = hsotg->regs + HCDMA(i);
		dev_dbg(hsotg->dev, "HCDMA	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, readl(addr));
		if (hsotg->core_params->dma_desc_enable > 0) {
			addr = hsotg->regs + HCDMAB(i);
			dev_dbg(hsotg->dev, "HCDMAB	 @0x%08lX : 0x%08X\n",
				(unsigned long)addr, readl(addr));
		}
	}
#endif
}

/**
 * dwc2_dump_global_registers() - Prints the core global registers
 *
 * @hsotg: Programming view of DWC_otg controller
 *
 * NOTE: This function will be removed once the peripheral controller code
 * is integrated and the driver is stable
 */
void dwc2_dump_global_registers(struct dwc2_hsotg *hsotg)
{
#ifdef DEBUG
	u32 __iomem *addr;

	dev_dbg(hsotg->dev, "Core Global Registers\n");
	addr = hsotg->regs + GOTGCTL;
	dev_dbg(hsotg->dev, "GOTGCTL	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GOTGINT;
	dev_dbg(hsotg->dev, "GOTGINT	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GAHBCFG;
	dev_dbg(hsotg->dev, "GAHBCFG	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GUSBCFG;
	dev_dbg(hsotg->dev, "GUSBCFG	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GRSTCTL;
	dev_dbg(hsotg->dev, "GRSTCTL	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GINTSTS;
	dev_dbg(hsotg->dev, "GINTSTS	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GINTMSK;
	dev_dbg(hsotg->dev, "GINTMSK	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GRXSTSR;
	dev_dbg(hsotg->dev, "GRXSTSR	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GRXFSIZ;
	dev_dbg(hsotg->dev, "GRXFSIZ	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GNPTXFSIZ;
	dev_dbg(hsotg->dev, "GNPTXFSIZ	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GNPTXSTS;
	dev_dbg(hsotg->dev, "GNPTXSTS	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GI2CCTL;
	dev_dbg(hsotg->dev, "GI2CCTL	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GPVNDCTL;
	dev_dbg(hsotg->dev, "GPVNDCTL	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GGPIO;
	dev_dbg(hsotg->dev, "GGPIO	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GUID;
	dev_dbg(hsotg->dev, "GUID	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GSNPSID;
	dev_dbg(hsotg->dev, "GSNPSID	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GHWCFG1;
	dev_dbg(hsotg->dev, "GHWCFG1	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GHWCFG2;
	dev_dbg(hsotg->dev, "GHWCFG2	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GHWCFG3;
	dev_dbg(hsotg->dev, "GHWCFG3	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GHWCFG4;
	dev_dbg(hsotg->dev, "GHWCFG4	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GLPMCFG;
	dev_dbg(hsotg->dev, "GLPMCFG	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GPWRDN;
	dev_dbg(hsotg->dev, "GPWRDN	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + GDFIFOCFG;
	dev_dbg(hsotg->dev, "GDFIFOCFG	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
	addr = hsotg->regs + HPTXFSIZ;
	dev_dbg(hsotg->dev, "HPTXFSIZ	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));

	addr = hsotg->regs + PCGCTL;
	dev_dbg(hsotg->dev, "PCGCTL	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, readl(addr));
#endif
}

/**
 * dwc2_flush_tx_fifo() - Flushes a Tx FIFO
 *
 * @hsotg: Programming view of DWC_otg controller
 * @num:   Tx FIFO to flush
 */
void dwc2_flush_tx_fifo(struct dwc2_hsotg *hsotg, const int num)
{
	u32 greset;
	int count = 0;

	dev_vdbg(hsotg->dev, "Flush Tx FIFO %d\n", num);

	greset = GRSTCTL_TXFFLSH;
	greset |= num << GRSTCTL_TXFNUM_SHIFT & GRSTCTL_TXFNUM_MASK;
	writel(greset, hsotg->regs + GRSTCTL);

	do {
		greset = readl(hsotg->regs + GRSTCTL);
		if (++count > 10000) {
			dev_warn(hsotg->dev,
				 "%s() HANG! GRSTCTL=%0x GNPTXSTS=0x%08x\n",
				 __func__, greset,
				 readl(hsotg->regs + GNPTXSTS));
			break;
		}
		udelay(1);
	} while (greset & GRSTCTL_TXFFLSH);

	/* Wait for at least 3 PHY Clocks */
	udelay(1);
}

/**
 * dwc2_flush_rx_fifo() - Flushes the Rx FIFO
 *
 * @hsotg: Programming view of DWC_otg controller
 */
void dwc2_flush_rx_fifo(struct dwc2_hsotg *hsotg)
{
	u32 greset;
	int count = 0;

	dev_vdbg(hsotg->dev, "%s()\n", __func__);

	greset = GRSTCTL_RXFFLSH;
	writel(greset, hsotg->regs + GRSTCTL);

	do {
		greset = readl(hsotg->regs + GRSTCTL);
		if (++count > 10000) {
			dev_warn(hsotg->dev, "%s() HANG! GRSTCTL=%0x\n",
				 __func__, greset);
			break;
		}
		udelay(1);
	} while (greset & GRSTCTL_RXFFLSH);

	/* Wait for at least 3 PHY Clocks */
	udelay(1);
}

#define DWC2_PARAM_TEST(a, b, c)	((a) < (b) || (a) > (c))

/* Parameter access functions */
int dwc2_set_param_otg_cap(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;
	int retval = 0;
	u32 op_mode;

	op_mode = hsotg->hwcfg2 & GHWCFG2_OP_MODE_MASK;

	switch (val) {
	case DWC2_CAP_PARAM_HNP_SRP_CAPABLE:
		if (op_mode != GHWCFG2_OP_MODE_HNP_SRP_CAPABLE)
			valid = 0;
		break;
	case DWC2_CAP_PARAM_SRP_ONLY_CAPABLE:
		switch (op_mode) {
		case GHWCFG2_OP_MODE_HNP_SRP_CAPABLE:
		case GHWCFG2_OP_MODE_SRP_ONLY_CAPABLE:
		case GHWCFG2_OP_MODE_SRP_CAPABLE_DEVICE:
		case GHWCFG2_OP_MODE_SRP_CAPABLE_HOST:
			break;
		default:
			valid = 0;
			break;
		}
		break;
	case DWC2_CAP_PARAM_NO_HNP_SRP_CAPABLE:
		/* always valid */
		break;
	default:
		valid = 0;
		break;
	}

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for otg_cap parameter. Check HW configuration.\n",
				val);
		switch (op_mode) {
		case GHWCFG2_OP_MODE_HNP_SRP_CAPABLE:
			val = DWC2_CAP_PARAM_HNP_SRP_CAPABLE;
			break;
		case GHWCFG2_OP_MODE_SRP_ONLY_CAPABLE:
		case GHWCFG2_OP_MODE_SRP_CAPABLE_DEVICE:
		case GHWCFG2_OP_MODE_SRP_CAPABLE_HOST:
			val = DWC2_CAP_PARAM_SRP_ONLY_CAPABLE;
			break;
		default:
			val = DWC2_CAP_PARAM_NO_HNP_SRP_CAPABLE;
			break;
		}
		dev_dbg(hsotg->dev, "Setting otg_cap to %d\n", val);
		retval = -EINVAL;
	}

	hsotg->core_params->otg_cap = val;
	return retval;
}

int dwc2_set_param_dma_enable(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;
	int retval = 0;

	if (val > 0 && (hsotg->hwcfg2 & GHWCFG2_ARCHITECTURE_MASK) ==
	    GHWCFG2_SLAVE_ONLY_ARCH)
		valid = 0;
	if (val < 0)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for dma_enable parameter. Check HW configuration.\n",
				val);
		val = (hsotg->hwcfg2 & GHWCFG2_ARCHITECTURE_MASK) !=
			GHWCFG2_SLAVE_ONLY_ARCH;
		dev_dbg(hsotg->dev, "Setting dma_enable to %d\n", val);
		retval = -EINVAL;
	}

	hsotg->core_params->dma_enable = val;
	return retval;
}

int dwc2_set_param_dma_desc_enable(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;
	int retval = 0;

	if (val > 0 && (hsotg->core_params->dma_enable <= 0 ||
			!(hsotg->hwcfg4 & GHWCFG4_DESC_DMA)))
		valid = 0;
	if (val < 0)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for dma_desc_enable parameter. Check HW configuration.\n",
				val);
		val = (hsotg->core_params->dma_enable > 0 &&
			(hsotg->hwcfg4 & GHWCFG4_DESC_DMA));
		dev_dbg(hsotg->dev, "Setting dma_desc_enable to %d\n", val);
		retval = -EINVAL;
	}

	hsotg->core_params->dma_desc_enable = val;
	return retval;
}

int dwc2_set_param_host_support_fs_ls_low_power(struct dwc2_hsotg *hsotg,
						int val)
{
	int retval = 0;

	if (DWC2_PARAM_TEST(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"Wrong value for host_support_fs_low_power\n");
			dev_err(hsotg->dev,
				"host_support_fs_low_power must be 0 or 1\n");
		}
		val = 0;
		dev_dbg(hsotg->dev,
			"Setting host_support_fs_low_power to %d\n", val);
		retval = -EINVAL;
	}

	hsotg->core_params->host_support_fs_ls_low_power = val;
	return retval;
}

int dwc2_set_param_enable_dynamic_fifo(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;
	int retval = 0;

	if (val > 0 && !(hsotg->hwcfg2 & GHWCFG2_DYNAMIC_FIFO))
		valid = 0;
	if (val < 0)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for enable_dynamic_fifo parameter. Check HW configuration.\n",
				val);
		val = !!(hsotg->hwcfg2 & GHWCFG2_DYNAMIC_FIFO);
		dev_dbg(hsotg->dev, "Setting enable_dynamic_fifo to %d\n", val);
		retval = -EINVAL;
	}

	hsotg->core_params->enable_dynamic_fifo = val;
	return retval;
}

int dwc2_set_param_host_rx_fifo_size(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;
	int retval = 0;

	if (val < 16 || val > readl(hsotg->regs + GRXFSIZ))
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for host_rx_fifo_size. Check HW configuration.\n",
				val);
		val = readl(hsotg->regs + GRXFSIZ);
		dev_dbg(hsotg->dev, "Setting host_rx_fifo_size to %d\n", val);
		retval = -EINVAL;
	}

	hsotg->core_params->host_rx_fifo_size = val;
	return retval;
}

int dwc2_set_param_host_nperio_tx_fifo_size(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;
	int retval = 0;

	if (val < 16 || val > (readl(hsotg->regs + GNPTXFSIZ) >> 16 & 0xffff))
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for host_nperio_tx_fifo_size. Check HW configuration.\n",
				val);
		val = readl(hsotg->regs + GNPTXFSIZ) >> 16 & 0xffff;
		dev_dbg(hsotg->dev, "Setting host_nperio_tx_fifo_size to %d\n",
			val);
		retval = -EINVAL;
	}

	hsotg->core_params->host_nperio_tx_fifo_size = val;
	return retval;
}

int dwc2_set_param_host_perio_tx_fifo_size(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;
	int retval = 0;

	if (val < 16 || val > (hsotg->hptxfsiz >> 16))
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for host_perio_tx_fifo_size. Check HW configuration.\n",
				val);
		val = hsotg->hptxfsiz >> 16;
		dev_dbg(hsotg->dev, "Setting host_perio_tx_fifo_size to %d\n",
			val);
		retval = -EINVAL;
	}

	hsotg->core_params->host_perio_tx_fifo_size = val;
	return retval;
}

int dwc2_set_param_max_transfer_size(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;
	int retval = 0;
	int width = hsotg->hwcfg3 >> GHWCFG3_XFER_SIZE_CNTR_WIDTH_SHIFT &
		    GHWCFG3_XFER_SIZE_CNTR_WIDTH_MASK >>
				GHWCFG3_XFER_SIZE_CNTR_WIDTH_SHIFT;

	if (val < 2047 || val >= (1 << (width + 11)))
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for max_transfer_size. Check HW configuration.\n",
				val);
		val = (1 << (width + 11)) - 1;
		dev_dbg(hsotg->dev, "Setting max_transfer_size to %d\n", val);
		retval = -EINVAL;
	}

	hsotg->core_params->max_transfer_size = val;
	return retval;
}

int dwc2_set_param_max_packet_count(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;
	int retval = 0;
	int width = hsotg->hwcfg3 >> GHWCFG3_PACKET_SIZE_CNTR_WIDTH_SHIFT &
		    GHWCFG3_PACKET_SIZE_CNTR_WIDTH_MASK >>
				GHWCFG3_PACKET_SIZE_CNTR_WIDTH_SHIFT;

	if (val < 15 || val > (1 << (width + 4)))
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for max_packet_count. Check HW configuration.\n",
				val);
		val = (1 << (width + 4)) - 1;
		dev_dbg(hsotg->dev, "Setting max_packet_count to %d\n", val);
		retval = -EINVAL;
	}

	hsotg->core_params->max_packet_count = val;
	return retval;
}

int dwc2_set_param_host_channels(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;
	int retval = 0;
	int num_chan = hsotg->hwcfg2 >> GHWCFG2_NUM_HOST_CHAN_SHIFT &
		GHWCFG2_NUM_HOST_CHAN_MASK >> GHWCFG2_NUM_HOST_CHAN_SHIFT;

	if (val < 1 || val > num_chan + 1)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for host_channels. Check HW configuration.\n",
				val);
		val = num_chan + 1;
		dev_dbg(hsotg->dev, "Setting host_channels to %d\n", val);
		retval = -EINVAL;
	}

	hsotg->core_params->host_channels = val;
	return retval;
}

int dwc2_set_param_phy_type(struct dwc2_hsotg *hsotg, int val)
{
#ifndef NO_FS_PHY_HW_CHECKS
	int valid = 0;
	u32 hs_phy_type;
	u32 fs_phy_type;
#endif
	int retval = 0;

	if (DWC2_PARAM_TEST(val, DWC2_PHY_TYPE_PARAM_FS,
			    DWC2_PHY_TYPE_PARAM_ULPI)) {
		if (val >= 0) {
			dev_err(hsotg->dev, "Wrong value for phy_type\n");
			dev_err(hsotg->dev, "phy_type must be 0, 1 or 2\n");
		}

#ifndef NO_FS_PHY_HW_CHECKS
		valid = 0;
#else
		val = DWC2_PHY_TYPE_PARAM_FS;
		dev_dbg(hsotg->dev, "Setting phy_type to %d\n", val);
		retval = -EINVAL;
#endif
	}

#ifndef NO_FS_PHY_HW_CHECKS
	hs_phy_type = hsotg->hwcfg2 & GHWCFG2_HS_PHY_TYPE_MASK;
	fs_phy_type = hsotg->hwcfg2 & GHWCFG2_FS_PHY_TYPE_MASK;

	if (val == DWC2_PHY_TYPE_PARAM_UTMI &&
	    (hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI ||
	     hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI_ULPI))
		valid = 1;
	else if (val == DWC2_PHY_TYPE_PARAM_ULPI &&
		 (hs_phy_type == GHWCFG2_HS_PHY_TYPE_ULPI ||
		  hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI_ULPI))
		valid = 1;
	else if (val == DWC2_PHY_TYPE_PARAM_FS &&
		 fs_phy_type == GHWCFG2_FS_PHY_TYPE_DEDICATED)
		valid = 1;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for phy_type. Check HW configuration.\n",
				val);
		val = DWC2_PHY_TYPE_PARAM_FS;
		if (hs_phy_type != GHWCFG2_HS_PHY_TYPE_NOT_SUPPORTED) {
			if (hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI ||
			    hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI_ULPI)
				val = DWC2_PHY_TYPE_PARAM_UTMI;
			else
				val = DWC2_PHY_TYPE_PARAM_ULPI;
		}
		dev_dbg(hsotg->dev, "Setting phy_type to %d\n", val);
		retval = -EINVAL;
	}
#endif

	hsotg->core_params->phy_type = val;
	return retval;
}

static int dwc2_get_param_phy_type(struct dwc2_hsotg *hsotg)
{
	return hsotg->core_params->phy_type;
}

int dwc2_set_param_speed(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;
	int retval = 0;

	if (DWC2_PARAM_TEST(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev, "Wrong value for speed parameter\n");
			dev_err(hsotg->dev, "max_speed parameter must be 0 or 1\n");
		}
		valid = 0;
	}

	if (val == DWC2_SPEED_PARAM_HIGH &&
	    dwc2_get_param_phy_type(hsotg) == DWC2_PHY_TYPE_PARAM_FS)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for speed parameter. Check HW configuration.\n",
				val);
		val = dwc2_get_param_phy_type(hsotg) == DWC2_PHY_TYPE_PARAM_FS ?
				DWC2_SPEED_PARAM_FULL : DWC2_SPEED_PARAM_HIGH;
		dev_dbg(hsotg->dev, "Setting speed to %d\n", val);
		retval = -EINVAL;
	}

	hsotg->core_params->speed = val;
	return retval;
}

int dwc2_set_param_host_ls_low_power_phy_clk(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;
	int retval = 0;

	if (DWC2_PARAM_TEST(val, DWC2_HOST_LS_LOW_POWER_PHY_CLK_PARAM_48MHZ,
			    DWC2_HOST_LS_LOW_POWER_PHY_CLK_PARAM_6MHZ)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"Wrong value for host_ls_low_power_phy_clk parameter\n");
			dev_err(hsotg->dev,
				"host_ls_low_power_phy_clk must be 0 or 1\n");
		}
		valid = 0;
	}

	if (val == DWC2_HOST_LS_LOW_POWER_PHY_CLK_PARAM_48MHZ &&
	    dwc2_get_param_phy_type(hsotg) == DWC2_PHY_TYPE_PARAM_FS)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for host_ls_low_power_phy_clk. Check HW configuration.\n",
				val);
		val = dwc2_get_param_phy_type(hsotg) == DWC2_PHY_TYPE_PARAM_FS
			? DWC2_HOST_LS_LOW_POWER_PHY_CLK_PARAM_6MHZ
			: DWC2_HOST_LS_LOW_POWER_PHY_CLK_PARAM_48MHZ;
		dev_dbg(hsotg->dev, "Setting host_ls_low_power_phy_clk to %d\n",
			val);
		retval = -EINVAL;
	}

	hsotg->core_params->host_ls_low_power_phy_clk = val;
	return retval;
}

int dwc2_set_param_phy_ulpi_ddr(struct dwc2_hsotg *hsotg, int val)
{
	int retval = 0;

	if (DWC2_PARAM_TEST(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev, "Wrong value for phy_ulpi_ddr\n");
			dev_err(hsotg->dev, "phy_upli_ddr must be 0 or 1\n");
		}
		val = 0;
		dev_dbg(hsotg->dev, "Setting phy_upli_ddr to %d\n", val);
		retval = -EINVAL;
	}

	hsotg->core_params->phy_ulpi_ddr = val;
	return retval;
}

int dwc2_set_param_phy_ulpi_ext_vbus(struct dwc2_hsotg *hsotg, int val)
{
	int retval = 0;

	if (DWC2_PARAM_TEST(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"Wrong value for phy_ulpi_ext_vbus\n");
			dev_err(hsotg->dev,
				"phy_ulpi_ext_vbus must be 0 or 1\n");
		}
		val = 0;
		dev_dbg(hsotg->dev, "Setting phy_ulpi_ext_vbus to %d\n", val);
		retval = -EINVAL;
	}

	hsotg->core_params->phy_ulpi_ext_vbus = val;
	return retval;
}

int dwc2_set_param_phy_utmi_width(struct dwc2_hsotg *hsotg, int val)
{
	int retval = 0;

	if (DWC2_PARAM_TEST(val, 8, 8) && DWC2_PARAM_TEST(val, 16, 16)) {
		if (val >= 0) {
			dev_err(hsotg->dev, "Wrong value for phy_utmi_width\n");
			dev_err(hsotg->dev, "phy_utmi_width must be 8 or 16\n");
		}
		val = 8;
		dev_dbg(hsotg->dev, "Setting phy_utmi_width to %d\n", val);
		retval = -EINVAL;
	}

	hsotg->core_params->phy_utmi_width = val;
	return retval;
}

int dwc2_set_param_ulpi_fs_ls(struct dwc2_hsotg *hsotg, int val)
{
	int retval = 0;

	if (DWC2_PARAM_TEST(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev, "Wrong value for ulpi_fs_ls\n");
			dev_err(hsotg->dev, "ulpi_fs_ls must be 0 or 1\n");
		}
		val = 0;
		dev_dbg(hsotg->dev, "Setting ulpi_fs_ls to %d\n", val);
		retval = -EINVAL;
	}

	hsotg->core_params->ulpi_fs_ls = val;
	return retval;
}

int dwc2_set_param_ts_dline(struct dwc2_hsotg *hsotg, int val)
{
	int retval = 0;

	if (DWC2_PARAM_TEST(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev, "Wrong value for ts_dline\n");
			dev_err(hsotg->dev, "ts_dline must be 0 or 1\n");
		}
		val = 0;
		dev_dbg(hsotg->dev, "Setting ts_dline to %d\n", val);
		retval = -EINVAL;
	}

	hsotg->core_params->ts_dline = val;
	return retval;
}

int dwc2_set_param_i2c_enable(struct dwc2_hsotg *hsotg, int val)
{
#ifndef NO_FS_PHY_HW_CHECKS
	int valid = 1;
#endif
	int retval = 0;

	if (DWC2_PARAM_TEST(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev, "Wrong value for i2c_enable\n");
			dev_err(hsotg->dev, "i2c_enable must be 0 or 1\n");
		}

#ifndef NO_FS_PHY_HW_CHECKS
		valid = 0;
#else
		val = 0;
		dev_dbg(hsotg->dev, "Setting i2c_enable to %d\n", val);
		retval = -EINVAL;
#endif
	}

#ifndef NO_FS_PHY_HW_CHECKS
	if (val == 1 && !(hsotg->hwcfg3 & GHWCFG3_I2C))
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for i2c_enable. Check HW configuration.\n",
				val);
		val = !!(hsotg->hwcfg3 & GHWCFG3_I2C);
		dev_dbg(hsotg->dev, "Setting i2c_enable to %d\n", val);
		retval = -EINVAL;
	}
#endif

	hsotg->core_params->i2c_enable = val;
	return retval;
}

int dwc2_set_param_en_multiple_tx_fifo(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;
	int retval = 0;

	if (DWC2_PARAM_TEST(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"Wrong value for en_multiple_tx_fifo,\n");
			dev_err(hsotg->dev,
				"en_multiple_tx_fifo must be 0 or 1\n");
		}
		valid = 0;
	}

	if (val == 1 && !(hsotg->hwcfg4 & GHWCFG4_DED_FIFO_EN))
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for parameter en_multiple_tx_fifo. Check HW configuration.\n",
				val);
		val = !!(hsotg->hwcfg4 & GHWCFG4_DED_FIFO_EN);
		dev_dbg(hsotg->dev, "Setting en_multiple_tx_fifo to %d\n", val);
		retval = -EINVAL;
	}

	hsotg->core_params->en_multiple_tx_fifo = val;
	return retval;
}

int dwc2_set_param_reload_ctl(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;
	int retval = 0;

	if (DWC2_PARAM_TEST(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"'%d' invalid for parameter reload_ctl\n", val);
			dev_err(hsotg->dev, "reload_ctl must be 0 or 1\n");
		}
		valid = 0;
	}

	if (val == 1 && hsotg->snpsid < DWC2_CORE_REV_2_92a)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for parameter reload_ctl. Check HW configuration.\n",
				val);
		val = hsotg->snpsid >= DWC2_CORE_REV_2_92a;
		dev_dbg(hsotg->dev, "Setting reload_ctl to %d\n", val);
		retval = -EINVAL;
	}

	hsotg->core_params->reload_ctl = val;
	return retval;
}

int dwc2_set_param_ahb_single(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;
	int retval = 0;

	if (DWC2_PARAM_TEST(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"'%d' invalid for parameter ahb_single\n", val);
			dev_err(hsotg->dev, "ahb_single must be 0 or 1\n");
		}
		valid = 0;
	}

	if (val > 0 && hsotg->snpsid < DWC2_CORE_REV_2_94a)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for parameter ahb_single. Check HW configuration.\n",
				val);
		val = 0;
		dev_dbg(hsotg->dev, "Setting ahb_single to %d\n", val);
		retval = -EINVAL;
	}

	hsotg->core_params->ahb_single = val;
	return retval;
}

int dwc2_set_param_otg_ver(struct dwc2_hsotg *hsotg, int val)
{
	int retval = 0;

	if (DWC2_PARAM_TEST(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"'%d' invalid for parameter otg_ver\n", val);
			dev_err(hsotg->dev,
				"otg_ver must be 0 (for OTG 1.3 support) or 1 (for OTG 2.0 support)\n");
		}
		val = 0;
		dev_dbg(hsotg->dev, "Setting otg_ver to %d\n", val);
		retval = -EINVAL;
	}

	hsotg->core_params->otg_ver = val;
	return retval;
}

/*
 * This function is called during module intialization to pass module parameters
 * for the DWC_otg core. It returns non-0 if any parameters are invalid.
 */
int dwc2_set_parameters(struct dwc2_hsotg *hsotg,
			const struct dwc2_core_params *params)
{
	int retval = 0;

	dev_dbg(hsotg->dev, "%s()\n", __func__);

	retval |= dwc2_set_param_otg_cap(hsotg, params->otg_cap);
	retval |= dwc2_set_param_dma_enable(hsotg, params->dma_enable);
	retval |= dwc2_set_param_dma_desc_enable(hsotg,
						 params->dma_desc_enable);
	retval |= dwc2_set_param_host_support_fs_ls_low_power(hsotg,
			params->host_support_fs_ls_low_power);
	retval |= dwc2_set_param_enable_dynamic_fifo(hsotg,
			params->enable_dynamic_fifo);
	retval |= dwc2_set_param_host_rx_fifo_size(hsotg,
			params->host_rx_fifo_size);
	retval |= dwc2_set_param_host_nperio_tx_fifo_size(hsotg,
			params->host_nperio_tx_fifo_size);
	retval |= dwc2_set_param_host_perio_tx_fifo_size(hsotg,
			params->host_perio_tx_fifo_size);
	retval |= dwc2_set_param_max_transfer_size(hsotg,
			params->max_transfer_size);
	retval |= dwc2_set_param_max_packet_count(hsotg,
			params->max_packet_count);
	retval |= dwc2_set_param_host_channels(hsotg, params->host_channels);
	retval |= dwc2_set_param_phy_type(hsotg, params->phy_type);
	retval |= dwc2_set_param_speed(hsotg, params->speed);
	retval |= dwc2_set_param_host_ls_low_power_phy_clk(hsotg,
			params->host_ls_low_power_phy_clk);
	retval |= dwc2_set_param_phy_ulpi_ddr(hsotg, params->phy_ulpi_ddr);
	retval |= dwc2_set_param_phy_ulpi_ext_vbus(hsotg,
			params->phy_ulpi_ext_vbus);
	retval |= dwc2_set_param_phy_utmi_width(hsotg, params->phy_utmi_width);
	retval |= dwc2_set_param_ulpi_fs_ls(hsotg, params->ulpi_fs_ls);
	retval |= dwc2_set_param_ts_dline(hsotg, params->ts_dline);
	retval |= dwc2_set_param_i2c_enable(hsotg, params->i2c_enable);
	retval |= dwc2_set_param_en_multiple_tx_fifo(hsotg,
			params->en_multiple_tx_fifo);
	retval |= dwc2_set_param_reload_ctl(hsotg, params->reload_ctl);
	retval |= dwc2_set_param_ahb_single(hsotg, params->ahb_single);
	retval |= dwc2_set_param_otg_ver(hsotg, params->otg_ver);

	return retval;
}

u16 dwc2_get_otg_version(struct dwc2_hsotg *hsotg)
{
	return (u16)(hsotg->core_params->otg_ver == 1 ? 0x0200 : 0x0103);
}

int dwc2_check_core_status(struct dwc2_hsotg *hsotg)
{
	if (readl(hsotg->regs + GSNPSID) == 0xffffffff)
		return -1;
	else
		return 0;
}

/**
 * dwc2_enable_global_interrupts() - Enables the controller's Global
 * Interrupt in the AHB Config register
 *
 * @hsotg: Programming view of DWC_otg controller
 */
void dwc2_enable_global_interrupts(struct dwc2_hsotg *hsotg)
{
	u32 ahbcfg = readl(hsotg->regs + GAHBCFG);

	ahbcfg |= GAHBCFG_GLBL_INTR_EN;
	writel(ahbcfg, hsotg->regs + GAHBCFG);
}

/**
 * dwc2_disable_global_interrupts() - Disables the controller's Global
 * Interrupt in the AHB Config register
 *
 * @hsotg: Programming view of DWC_otg controller
 */
void dwc2_disable_global_interrupts(struct dwc2_hsotg *hsotg)
{
	u32 ahbcfg = readl(hsotg->regs + GAHBCFG);

	ahbcfg &= ~GAHBCFG_GLBL_INTR_EN;
	writel(ahbcfg, hsotg->regs + GAHBCFG);
}

MODULE_DESCRIPTION("DESIGNWARE HS OTG Core");
MODULE_AUTHOR("Synopsys, Inc.");
MODULE_LICENSE("Dual BSD/GPL");
