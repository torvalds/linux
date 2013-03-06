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

#include <linux/delay.h>

#include "cil.h"

void dwc_otg_enable_global_interrupts(struct core_if *core_if)
{
	u32 ahbcfg = 0;

	ahbcfg |= DWC_AHBCFG_GLBL_INT_MASK;
	dwc_reg_modify(core_if->core_global_regs, DWC_GAHBCFG, 0,
		     ahbcfg);
}

void dwc_otg_disable_global_interrupts(struct core_if *core_if)
{
	u32 ahbcfg = 0;

	ahbcfg |= DWC_AHBCFG_GLBL_INT_MASK;
	dwc_reg_modify(core_if->core_global_regs, DWC_GAHBCFG,
		     ahbcfg, 0);
}

/**
 * Tests if the current hardware is using a full speed phy.
 */
static inline int full_speed_phy(struct core_if *core_if)
{
	if (DWC_HWCFG2_FS_PHY_TYPE_RD(core_if->hwcfg2) == 2 &&
	     DWC_HWCFG2_FS_PHY_TYPE_RD(core_if->hwcfg2) == 1)
		return 1;
	return 0;
}

/**
 * Initializes the FSLSPClkSel field of the HCFG register depending on the PHY
 * type.
 */
void init_fslspclksel(struct core_if *core_if)
{
	u32 val;
	u32 hcfg = 0;

	if (full_speed_phy(core_if))
		val = DWC_HCFG_48_MHZ;
	else
		/* High speed PHY running at full speed or high speed */
		val = DWC_HCFG_30_60_MHZ;

	hcfg = dwc_reg_read(core_if->host_if->host_global_regs, DWC_HCFG);
	hcfg = DWC_HCFG_FSLSP_CLK_RW(hcfg, val);
	dwc_reg_write(core_if->host_if->host_global_regs, DWC_HCFG, hcfg);
}

/**
 * Initializes the DevSpd field of the DCFG register depending on the PHY type
 * and the enumeration speed of the device.
 */
static void init_devspd(struct core_if *core_if)
{
	u32 val;
	u32 dcfg;

	if (full_speed_phy(core_if))
		val = 0x3;
	else
		/* High speed PHY running at high speed */
		val = 0x0;

	dcfg = dwc_reg_read(core_if->dev_if->dev_global_regs, DWC_DCFG);
	dcfg = DWC_DCFG_DEV_SPEED_WR(dcfg, val);
	dwc_reg_write(core_if->dev_if->dev_global_regs, DWC_DCFG, dcfg);
}

/**
 * This function calculates the number of IN EPS using GHWCFG1 and GHWCFG2
 * registers values
 */
static u32 calc_num_in_eps(struct core_if *core_if)
{
	u32 num_in_eps = 0;
	u32 num_eps = DWC_HWCFG2_NO_DEV_EP_RD(core_if->hwcfg2);
	u32 hwcfg1 = core_if->hwcfg1 >> 2;
	u32 num_tx_fifos = DWC_HWCFG4_NUM_IN_EPS_RD(core_if->hwcfg4);
	u32 i;

	for (i = 0; i < num_eps; ++i) {
		if (!(hwcfg1 & 0x1))
			num_in_eps++;
		hwcfg1 >>= 2;
	}

	if (DWC_HWCFG4_DED_FIFO_ENA_RD(core_if->hwcfg4))
		num_in_eps = num_in_eps > num_tx_fifos ?
		    num_tx_fifos : num_in_eps;

	return num_in_eps;
}

/**
 * This function calculates the number of OUT EPS using GHWCFG1 and GHWCFG2
 * registers values
 */
static u32 calc_num_out_eps(struct core_if *core_if)
{
	u32 num_out_eps = 0;
	u32 num_eps = DWC_HWCFG2_NO_DEV_EP_RD(core_if->hwcfg2);
	u32 hwcfg1 = core_if->hwcfg1 >> 2;
	u32 i;

	for (i = 0; i < num_eps; ++i) {
		if (!(hwcfg1 & 0x2))
			num_out_eps++;
		hwcfg1 >>= 2;
	}
	return num_out_eps;
}

/**
 * Do core a soft reset of the core.  Be careful with this because it
 * resets all the internal state machines of the core.
 */
static void dwc_otg_core_reset(struct core_if *core_if)
{
	ulong global_regs = core_if->core_global_regs;
	u32 greset = 0;
	int count = 0;

	/* Wait for AHB master IDLE state. */
	do {
		udelay(10);
		greset = dwc_reg_read(global_regs, DWC_GRSTCTL);
		if (++count > 100000) {
			pr_warning("%s() HANG! AHB Idle GRSTCTL=%0x\n",
				   __func__, greset);
			return;
		}
	} while (!(greset & DWC_RSTCTL_AHB_IDLE));

	/* Core Soft Reset */
	count = 0;
	greset |= DWC_RSTCTL_SFT_RST;
	dwc_reg_write(global_regs, DWC_GRSTCTL, greset);

	do {
		greset = dwc_reg_read(global_regs, DWC_GRSTCTL);
		if (++count > 10000) {
			pr_warning("%s() HANG! Soft Reset "
				   "GRSTCTL=%0x\n", __func__, greset);
			break;
		}
		udelay(1);
	} while (greset & DWC_RSTCTL_SFT_RST);

	/* Wait for 3 PHY Clocks */
	msleep(100);
}

/**
 * This function initializes the commmon interrupts, used in both
 * device and host modes.
 */
void dwc_otg_enable_common_interrupts(struct core_if *core_if)
{
	ulong global_regs = core_if->core_global_regs;
	u32 intr_mask = 0;

	/* Clear any pending OTG Interrupts */
	dwc_reg_write(global_regs, DWC_GOTGINT, 0xFFFFFFFF);

	/* Clear any pending interrupts */
	dwc_reg_write(global_regs, DWC_GINTSTS, 0xFFFFFFFF);

	/* Enable the interrupts in the GINTMSK. */
	intr_mask |= DWC_INTMSK_MODE_MISMTC;
	intr_mask |= DWC_INTMSK_OTG;
	intr_mask |= DWC_INTMSK_CON_ID_STS_CHG;
	intr_mask |= DWC_INTMSK_WKP;
	intr_mask |= DWC_INTMSK_SES_DISCON_DET;
	intr_mask |= DWC_INTMSK_USB_SUSP;
	intr_mask |= DWC_INTMSK_NEW_SES_DET;
	if (!core_if->dma_enable)
		intr_mask |= DWC_INTMSK_RXFIFO_NOT_EMPT;
	dwc_reg_write(global_regs, DWC_GINTMSK, intr_mask);
}

/**
 * This function initializes the DWC_otg controller registers and prepares the
 * core for device mode or host mode operation.
 */
void dwc_otg_core_init(struct core_if *core_if)
{
	u32 i;
	u32 global_reg = core_if->core_global_regs;
	struct device_if *dev_if = core_if->dev_if;
	u32 ahbcfg = 0;
	u32 gusbcfg;

	/* Common Initialization */
	gusbcfg = dwc_reg_read(global_reg, DWC_GUSBCFG);

	/* Program the ULPI External VBUS bit if needed */
	gusbcfg |= DWC_USBCFG_ULPI_EXT_VBUS_DRV;

	/* Set external TS Dline pulsing */
	gusbcfg = gusbcfg & (~((u32) DWC_USBCFG_TERM_SEL_DL_PULSE));

	dwc_reg_write(global_reg, DWC_GUSBCFG, gusbcfg);

	/* Reset the Controller */
	dwc_otg_core_reset(core_if);

	/* Initialize parameters from Hardware configuration registers. */
	dev_if->num_in_eps = calc_num_in_eps(core_if);
	dev_if->num_out_eps = calc_num_out_eps(core_if);

	for (i = 0; i < DWC_HWCFG4_NUM_DEV_PERIO_IN_EP_RD(core_if->hwcfg4);
			i++) {
		dev_if->perio_tx_fifo_size[i] =
		    dwc_reg_read(global_reg, DWC_DPTX_FSIZ_DIPTXF(i)) >> 16;
	}
	for (i = 0; i < DWC_HWCFG4_NUM_IN_EPS_RD(core_if->hwcfg4); i++) {
		dev_if->tx_fifo_size[i] =
		    dwc_reg_read(global_reg, DWC_DPTX_FSIZ_DIPTXF(i)) >> 16;
	}

	core_if->total_fifo_size = DWC_HWCFG3_DFIFO_DEPTH_RD(core_if->hwcfg3);
	core_if->rx_fifo_size = dwc_reg_read(global_reg, DWC_GRXFSIZ);
	core_if->nperio_tx_fifo_size =
	    dwc_reg_read(global_reg, DWC_GRXFSIZ) >> 16;

	if (!core_if->phy_init_done) {
		gusbcfg = dwc_reg_read(global_reg, DWC_GUSBCFG);
		core_if->phy_init_done = 1;
		/*
		 * If the PHY is selectable, select ULPI
		 */
		if(DWC_HWCFG2_HS_PHY_TYPE_RD(core_if->hwcfg2) ==
		   DWC_HWCFG2_HS_PHY_TYPE_UTMI_ULPI) {
			gusbcfg |= DWC_USBCFG_ULPI_UTMI_SEL;
		}

		if (gusbcfg & DWC_USBCFG_ULPI_UTMI_SEL) {
			/* ULPI interface */
			gusbcfg |= DWC_USBCFG_PHYIF;
			if (core_if->core_params->phy_ulpi_ddr)
				gusbcfg |= DWC_USBCFG_DDRSEL;
			else
				gusbcfg &= ~((u32) DWC_USBCFG_DDRSEL);
		} else {
			/* UTMI+ interface */
			if (core_if->core_params->phy_utmi_width == 16)
				gusbcfg |= DWC_USBCFG_PHYIF;
			else
				gusbcfg &= ~((u32) DWC_USBCFG_PHYIF);
		}
		dwc_reg_write(global_reg, DWC_GUSBCFG, gusbcfg);

		/* Reset after setting the PHY parameters */
		dwc_otg_core_reset(core_if);
	}

	if (DWC_HWCFG2_HS_PHY_TYPE_RD(core_if->hwcfg2) == 2 &&
	    DWC_HWCFG2_FS_PHY_TYPE_RD(core_if->hwcfg2) == 1) {
		gusbcfg = dwc_reg_read(global_reg, DWC_GUSBCFG);
		gusbcfg |= DWC_USBCFG_ULPI_FSLS;
		gusbcfg |= DWC_USBCFG_ULPI_CLK_SUS_M;
		dwc_reg_write(global_reg, DWC_GUSBCFG, gusbcfg);
	} else {
		gusbcfg = dwc_reg_read(global_reg, DWC_GUSBCFG);
		gusbcfg &= ~((u32) DWC_USBCFG_ULPI_FSLS);
		gusbcfg &= ~((u32) DWC_USBCFG_ULPI_CLK_SUS_M);
		dwc_reg_write(global_reg, DWC_GUSBCFG, gusbcfg);
	}

	/* Program the GAHBCFG Register. */
	switch (DWC_HWCFG2_ARCH_RD(core_if->hwcfg2)) {
	case DWC_SLAVE_ONLY_ARCH:
		ahbcfg &= ~DWC_AHBCFG_NPFIFO_EMPTY;	/* HALF empty */
		ahbcfg &= ~DWC_AHBCFG_FIFO_EMPTY;	/* HALF empty */
		core_if->dma_enable = 0;
		break;
	case DWC_EXT_DMA_ARCH:
		ahbcfg = (ahbcfg & ~DWC_AHBCFG_BURST_LEN(0xf)) |
		    DWC_AHBCFG_BURST_LEN(core_if->core_params->dma_burst_size);
		core_if->dma_enable = (core_if->core_params->dma_enable != 0);
		break;
	case DWC_INT_DMA_ARCH:
		ahbcfg = (ahbcfg & ~DWC_AHBCFG_BURST_LEN(0xf)) |
		    DWC_AHBCFG_BURST_LEN(DWC_GAHBCFG_INT_DMA_BURST_INCR);
		core_if->dma_enable = (core_if->core_params->dma_enable != 0);
		break;
	}

	if (core_if->dma_enable)
		ahbcfg |= DWC_AHBCFG_DMA_ENA;
	else
		ahbcfg &= ~DWC_AHBCFG_DMA_ENA;
	dwc_reg_write(global_reg, DWC_GAHBCFG, ahbcfg);
	core_if->en_multiple_tx_fifo =
	    DWC_HWCFG4_DED_FIFO_ENA_RD(core_if->hwcfg4);

	/* Program the GUSBCFG register. */
	gusbcfg = dwc_reg_read(global_reg, DWC_GUSBCFG);
	switch (DWC_HWCFG2_OP_MODE_RD(core_if->hwcfg2)) {
	case DWC_MODE_HNP_SRP_CAPABLE:
		gusbcfg |= DWC_USBCFG_HNP_CAP;
		gusbcfg |= DWC_USBCFG_SRP_CAP;
		break;
	case DWC_MODE_SRP_ONLY_CAPABLE:
		gusbcfg &= ~((u32) DWC_USBCFG_HNP_CAP);
		gusbcfg |= DWC_USBCFG_SRP_CAP;
		break;
	case DWC_MODE_NO_HNP_SRP_CAPABLE:
		gusbcfg &= ~((u32) DWC_USBCFG_HNP_CAP);
		gusbcfg &= ~((u32) DWC_USBCFG_SRP_CAP);
		break;
	case DWC_MODE_SRP_CAPABLE_DEVICE:
		gusbcfg &= ~((u32) DWC_USBCFG_HNP_CAP);
		gusbcfg |= DWC_USBCFG_SRP_CAP;
		break;
	case DWC_MODE_NO_SRP_CAPABLE_DEVICE:
		gusbcfg &= ~((u32) DWC_USBCFG_HNP_CAP);
		gusbcfg &= ~((u32) DWC_USBCFG_SRP_CAP);
		break;
	case DWC_MODE_SRP_CAPABLE_HOST:
		gusbcfg &= ~((u32) DWC_USBCFG_HNP_CAP);
		gusbcfg |= DWC_USBCFG_SRP_CAP;
		break;
	case DWC_MODE_NO_SRP_CAPABLE_HOST:
		gusbcfg &= ~((u32) DWC_USBCFG_HNP_CAP);
		gusbcfg &= ~((u32) DWC_USBCFG_SRP_CAP);
		break;
	}
	dwc_reg_write(global_reg, DWC_GUSBCFG, gusbcfg);

	/* Enable common interrupts */
	dwc_otg_enable_common_interrupts(core_if);

	/*
	 * Do device or host intialization based on mode during PCD
	 * and HCD initialization
	 */
	if (dwc_otg_is_host_mode(core_if)) {
		core_if->xceiv->state = OTG_STATE_A_HOST;
	} else {
		core_if->xceiv->state = OTG_STATE_B_PERIPHERAL;
		if (dwc_has_feature(core_if, DWC_DEVICE_ONLY))
			dwc_otg_core_dev_init(core_if);
	}
}

/**
 * This function enables the Device mode interrupts.
 *
 * Note that the bits in the Device IN endpoint mask register are laid out
 * exactly the same as the Device IN endpoint interrupt register.
 */
static void dwc_otg_enable_device_interrupts(struct core_if *core_if)
{
	u32 intr_mask = 0;
	u32 msk = 0;
	ulong global_regs = core_if->core_global_regs;

	/* Disable all interrupts. */
	dwc_reg_write(global_regs, DWC_GINTMSK, 0);

	/* Clear any pending interrupts */
	dwc_reg_write(global_regs, DWC_GINTSTS, 0xFFFFFFFF);

	/* Enable the common interrupts */
	dwc_otg_enable_common_interrupts(core_if);

	/* Enable interrupts */
	intr_mask |= DWC_INTMSK_USB_RST;
	intr_mask |= DWC_INTMSK_ENUM_DONE;
	intr_mask |= DWC_INTMSK_IN_ENDP;
	intr_mask |= DWC_INTMSK_OUT_ENDP;
	intr_mask |= DWC_INTMSK_EARLY_SUSP;
	if (!core_if->en_multiple_tx_fifo)
		intr_mask |= DWC_INTMSK_ENDP_MIS_MTCH;

	/* Periodic EP */
	intr_mask |= DWC_INTMSK_ISYNC_OUTPKT_DRP;
	intr_mask |= DWC_INTMSK_END_OF_PFRM;
	intr_mask |= DWC_INTMSK_INCMP_IN_ATX;
	intr_mask |= DWC_INTMSK_INCMP_OUT_PTX;

	dwc_reg_modify(global_regs, DWC_GINTMSK, intr_mask, intr_mask);

	msk = DWC_DIEPMSK_TXFIFO_UNDERN_RW(msk, 1);
	dwc_reg_modify(core_if->dev_if->dev_global_regs, DWC_DIEPMSK,
		     msk, msk);
}

/**
 *  Configures the device data fifo sizes when dynamic sizing is enabled.
 */
static void config_dev_dynamic_fifos(struct core_if *core_if)
{
	u32 i;
	ulong regs = core_if->core_global_regs;
	struct core_params *params = core_if->core_params;
	u32 txsize = 0;
	u32 nptxsize = 0;
	u32 ptxsize = 0;

	/* Rx FIFO */
	dwc_reg_write(regs, DWC_GRXFSIZ, params->dev_rx_fifo_size);

	/* Set Periodic and Non-periodic Tx FIFO Mask bits to all 0 */
	core_if->p_tx_msk = 0;
	core_if->tx_msk = 0;

	if (core_if->en_multiple_tx_fifo == 0) {
		/* Non-periodic Tx FIFO */
		nptxsize = DWC_RX_FIFO_DEPTH_WR(nptxsize,
						params->
						dev_nperio_tx_fifo_size);
		nptxsize =
		    DWC_RX_FIFO_START_ADDR_WR(nptxsize,
					      params->dev_rx_fifo_size);
		dwc_reg_write(regs, DWC_GNPTXFSIZ, nptxsize);

		ptxsize = DWC_RX_FIFO_START_ADDR_WR(ptxsize,
						    (DWC_RX_FIFO_START_ADDR_RD
						     (nptxsize) +
						     DWC_RX_FIFO_DEPTH_RD
						     (nptxsize)));
		for (i = 0;
		     i < DWC_HWCFG4_NUM_DEV_PERIO_IN_EP_RD(core_if->hwcfg4);
		     i++) {
			ptxsize =
			    DWC_RX_FIFO_DEPTH_WR(ptxsize,
						 params->
						 dev_perio_tx_fifo_size[i]);
			dwc_reg_write(regs, DWC_DPTX_FSIZ_DIPTXF(i), ptxsize);
			ptxsize = DWC_RX_FIFO_START_ADDR_WR(ptxsize,
						   (DWC_RX_FIFO_START_ADDR_RD
						    (ptxsize) +
						    DWC_RX_FIFO_DEPTH_RD
						    (ptxsize)));
		}
	} else {
		nptxsize = DWC_RX_FIFO_DEPTH_WR(nptxsize,
						params->
						dev_nperio_tx_fifo_size);
		nptxsize =
		    DWC_RX_FIFO_START_ADDR_WR(nptxsize,
					      params->dev_rx_fifo_size);
		dwc_reg_write(regs, DWC_GNPTXFSIZ, nptxsize);

		txsize = DWC_RX_FIFO_START_ADDR_WR(txsize,
						   (DWC_RX_FIFO_START_ADDR_RD
						    (nptxsize) +
						    DWC_RX_FIFO_DEPTH_RD
						    (nptxsize)));
		for (i = 1;
		     i < DWC_HWCFG4_NUM_IN_EPS_RD(core_if->hwcfg4);
		     i++) {
			txsize =
			    DWC_RX_FIFO_DEPTH_WR(txsize,
						 params->dev_tx_fifo_size[i]);
			dwc_reg_write(regs, DWC_DPTX_FSIZ_DIPTXF(i - 1),
					txsize);
			txsize = DWC_RX_FIFO_START_ADDR_WR(txsize,
						   (DWC_RX_FIFO_START_ADDR_RD
						    (txsize) +
						    DWC_RX_FIFO_DEPTH_RD
						    (txsize)));
		}
	}
}

/**
 * This function initializes the DWC_otg controller registers for
 * device mode.
 */
void dwc_otg_core_dev_init(struct core_if *c_if)
{
	u32 i;
	struct device_if *d_if = c_if->dev_if;
	struct core_params *params = c_if->core_params;
	u32 dcfg = 0;
	u32 resetctl = 0;

	/* Restart the Phy Clock */
	dwc_reg_write(c_if->pcgcctl, 0, 0);

	/* Device configuration register */
	init_devspd(c_if);
	dcfg = dwc_reg_read(d_if->dev_global_regs, DWC_DCFG);
	dcfg = DWC_DCFG_P_FRM_INTRVL_WR(dcfg, DWC_DCFG_FRAME_INTERVAL_80);
	dwc_reg_write(d_if->dev_global_regs, DWC_DCFG, dcfg);

	/* If needed configure data FIFO sizes */
	if (DWC_HWCFG2_DYN_FIFO_RD(c_if->hwcfg2) && params->enable_dynamic_fifo)
		config_dev_dynamic_fifos(c_if);

	/* Flush the FIFOs */
	dwc_otg_flush_tx_fifo(c_if, DWC_GRSTCTL_TXFNUM_ALL);
	dwc_otg_flush_rx_fifo(c_if);

	/* Flush the Learning Queue. */
	resetctl |= DWC_RSTCTL_TKN_QUE_FLUSH;
	dwc_reg_write(c_if->core_global_regs, DWC_GRSTCTL, resetctl);

	/* Clear all pending Device Interrupts */
	dwc_reg_write(d_if->dev_global_regs, DWC_DIEPMSK, 0);
	dwc_reg_write(d_if->dev_global_regs, DWC_DOEPMSK, 0);
	dwc_reg_write(d_if->dev_global_regs, DWC_DAINT, 0xFFFFFFFF);
	dwc_reg_write(d_if->dev_global_regs, DWC_DAINTMSK, 0);

	for (i = 0; i <= d_if->num_in_eps; i++) {
		u32 depctl = 0;

		depctl = dwc_reg_read(d_if->in_ep_regs[i], DWC_DIEPCTL);
		if (DWC_DEPCTL_EPENA_RD(depctl)) {
			depctl = 0;
			depctl = DWC_DEPCTL_EPDIS_RW(depctl, 1);
			depctl = DWC_DEPCTL_SET_NAK_RW(depctl, 1);
		} else {
			depctl = 0;
		}

		dwc_reg_write(d_if->in_ep_regs[i], DWC_DIEPCTL, depctl);
		dwc_reg_write(d_if->in_ep_regs[i], DWC_DIEPTSIZ, 0);
		dwc_reg_write(d_if->in_ep_regs[i], DWC_DIEPDMA, 0);
		dwc_reg_write(d_if->in_ep_regs[i], DWC_DIEPINT, 0xFF);
	}

	for (i = 0; i <= d_if->num_out_eps; i++) {
		u32 depctl = 0;
		depctl = dwc_reg_read(d_if->out_ep_regs[i], DWC_DOEPCTL);
		if (DWC_DEPCTL_EPENA_RD(depctl)) {
			depctl = 0;
			depctl = DWC_DEPCTL_EPDIS_RW(depctl, 1);
			depctl = DWC_DEPCTL_SET_NAK_RW(depctl, 1);
		} else {
			depctl = 0;
		}
		dwc_reg_write(d_if->out_ep_regs[i], DWC_DOEPCTL, depctl);
		dwc_reg_write(d_if->out_ep_regs[i], DWC_DOEPTSIZ, 0);
		dwc_reg_write(d_if->out_ep_regs[i], DWC_DOEPDMA, 0);
		dwc_reg_write(d_if->out_ep_regs[i], DWC_DOEPINT, 0xFF);
	}

	dwc_otg_enable_device_interrupts(c_if);
}

/**
 * This function reads a packet from the Rx FIFO into the destination buffer.
 * To read SETUP data use dwc_otg_read_setup_packet.
 */
void dwc_otg_read_packet(struct core_if *core_if, u8 * dest, u16 _bytes)
{
	u32 i;
	int word_count = (_bytes + 3) / 4;
	u32 fifo = core_if->data_fifo[0];
	u32 *data_buff = (u32 *) dest;

	/*
	 * This requires reading data from the FIFO into a u32 temp buffer,
	 * then moving it into the data buffer.
	 */
	for (i = 0; i < word_count; i++, data_buff++)
		*data_buff = dwc_read_fifo32(fifo);
}

/**
 * Flush a Tx FIFO.
 */
void dwc_otg_flush_tx_fifo(struct core_if *core_if, const int num)
{
	ulong global_regs = core_if->core_global_regs;
	u32 greset = 0;
	int count = 0;

	greset |= DWC_RSTCTL_TX_FIFO_FLUSH;
	greset = DWC_RSTCTL_TX_FIFO_NUM(greset, num);
	dwc_reg_write(global_regs, DWC_GRSTCTL, greset);

	do {
		greset = dwc_reg_read(global_regs, DWC_GRSTCTL);
		if (++count > 10000) {
			pr_warning("%s() HANG! GRSTCTL=%0x "
				   "GNPTXSTS=0x%08x\n", __func__, greset,
				   dwc_reg_read(global_regs, DWC_GNPTXSTS));
			break;
		}
		udelay(1);
	} while (greset & DWC_RSTCTL_TX_FIFO_FLUSH);

	/* Wait for 3 PHY Clocks */
	udelay(1);
}

/**
 * Flush Rx FIFO.
 */
void dwc_otg_flush_rx_fifo(struct core_if *core_if)
{
	ulong global_regs = core_if->core_global_regs;
	u32 greset = 0;
	int count = 0;

	greset |= DWC_RSTCTL_RX_FIFO_FLUSH;
	dwc_reg_write(global_regs, DWC_GRSTCTL, greset);

	do {
		greset = dwc_reg_read(global_regs, DWC_GRSTCTL);
		if (++count > 10000) {
			pr_warning("%s() HANG! GRSTCTL=%0x\n",
				   __func__, greset);
			break;
		}
		udelay(1);
	} while (greset & DWC_RSTCTL_RX_FIFO_FLUSH);

	/* Wait for 3 PHY Clocks */
	udelay(1);
}

/**
 * Register HCD callbacks.
 * The callbacks are used to start and stop the HCD for interrupt processing.
 */
void dwc_otg_cil_register_hcd_callbacks(struct core_if *c_if,
						  struct cil_callbacks *cb,
						  void *p)
{
	c_if->hcd_cb = cb;
	cb->p = p;
}

/**
 * Register PCD callbacks.
 * The callbacks are used to start and stop the PCD for interrupt processing.
 */
void dwc_otg_cil_register_pcd_callbacks(struct core_if *c_if,
						  struct cil_callbacks *cb,
						  void *p)
{
	c_if->pcd_cb = cb;
	cb->p = p;
}

/**
 * This function is called to initialize the DWC_otg CSR data structures.
 *
 * The register addresses in the device and host structures are initialized from
 * the base address supplied by the caller. The calling function must make the
 * OS calls to get the base address of the DWC_otg controller registers.
 *
 * The params argument holds the parameters that specify how the core should be
 * configured.
 */
struct core_if *dwc_otg_cil_init(const __iomem u32 *base,
					   struct core_params *params)
{
	struct core_if *core_if;
	struct device_if *dev_if;
	struct dwc_host_if *host_if;
	u8 *reg_base = (__force u8 *)base;
	u32 offset;
	u32 i;

	core_if = kzalloc(sizeof(*core_if), GFP_KERNEL);
	if (!core_if)
		return NULL;

	core_if->core_params = params;
	core_if->core_global_regs = (ulong)reg_base;

	/* Allocate the Device Mode structures. */
	dev_if = kmalloc(sizeof(*dev_if), GFP_KERNEL);
	if (!dev_if) {
		kfree(core_if);
		return NULL;
	}

	dev_if->dev_global_regs = (ulong)(reg_base + DWC_DEV_GLOBAL_REG_OFFSET);

	for (i = 0; i < MAX_EPS_CHANNELS; i++) {
		offset = i * DWC_EP_REG_OFFSET;

		dev_if->in_ep_regs[i] = (ulong)(reg_base +
					       DWC_DEV_IN_EP_REG_OFFSET +
					       offset);

		dev_if->out_ep_regs[i] = (ulong)(reg_base +
						DWC_DEV_OUT_EP_REG_OFFSET +
						offset);
	}

	dev_if->speed = 0;	/* unknown */
	core_if->dev_if = dev_if;

	/* Allocate the Host Mode structures. */
	host_if = kmalloc(sizeof(*host_if), GFP_KERNEL);
	if (!host_if) {
		kfree(dev_if);
		kfree(core_if);
		return NULL;
	}

	host_if->host_global_regs = (ulong)(reg_base +
					   DWC_OTG_HOST_GLOBAL_REG_OFFSET);

	host_if->hprt0 = (ulong)(reg_base + DWC_OTG_HOST_PORT_REGS_OFFSET);

	for (i = 0; i < MAX_EPS_CHANNELS; i++) {
		offset = i * DWC_OTG_CHAN_REGS_OFFSET;

		host_if->hc_regs[i] = (ulong)(reg_base +
					     DWC_OTG_HOST_CHAN_REGS_OFFSET +
					     offset);
	}

	host_if->num_host_channels = MAX_EPS_CHANNELS;
	core_if->host_if = host_if;
	for (i = 0; i < MAX_EPS_CHANNELS; i++) {
		core_if->data_fifo[i] =
		    (ulong)(reg_base + DWC_OTG_DATA_FIFO_OFFSET +
			     (i * DWC_OTG_DATA_FIFO_SIZE));
	}
	core_if->pcgcctl = (ulong)(reg_base + DWC_OTG_PCGCCTL_OFFSET);

	/*
	 * Store the contents of the hardware configuration registers here for
	 * easy access later.
	 */
	core_if->hwcfg1 =
	    dwc_reg_read(core_if->core_global_regs, DWC_GHWCFG1);
	core_if->hwcfg2 =
	    dwc_reg_read(core_if->core_global_regs, DWC_GHWCFG2);
	core_if->hwcfg3 =
	    dwc_reg_read(core_if->core_global_regs, DWC_GHWCFG3);
	core_if->hwcfg4 =
	    dwc_reg_read(core_if->core_global_regs, DWC_GHWCFG4);

	/* Set the SRP sucess bit for FS-I2c */
	core_if->srp_success = 0;
	core_if->srp_timer_started = 0;
	return core_if;
}

/**
 * This function frees the structures allocated by dwc_otg_cil_init().
 */
void dwc_otg_cil_remove(struct core_if *core_if)
{
	/* Disable all interrupts */
	dwc_reg_modify(core_if->core_global_regs, DWC_GAHBCFG, 1, 0);
	dwc_reg_write(core_if->core_global_regs, DWC_GINTMSK, 0);

	if (core_if) {
		kfree(core_if->dev_if);
		kfree(core_if->host_if);
	}
	kfree(core_if);
}
