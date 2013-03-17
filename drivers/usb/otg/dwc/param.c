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
 * This file provides dwc_otg driver parameter and parameter checking.
 */

#include "cil.h"

/*
 * Encapsulate the module parameter settings
 */
struct core_params dwc_otg_module_params = {
	.dma_burst_size = -1,
	.enable_dynamic_fifo = -1,
	.dev_rx_fifo_size = -1,
	.dev_nperio_tx_fifo_size = -1,
	.dev_perio_tx_fifo_size = {-1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1},	/* 15 */
	.host_rx_fifo_size = -1,
	.host_nperio_tx_fifo_size = -1,
	.host_perio_tx_fifo_size = -1,
	.max_transfer_size = -1,
	.max_packet_count = -1,
	.host_channels = -1,
	.dev_endpoints = -1,
	.phy_utmi_width = -1,
	.phy_ulpi_ddr = -1,
	.en_multiple_tx_fifo = -1,
	.dev_tx_fifo_size = {-1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1},	/* 15 */
};


/**
 * Checks that parameter settings for the periodic Tx FIFO sizes are correct
 * according to the hardware configuration. Sets the size to the hardware
 * configuration if an incorrect size is detected.
 */
static int set_valid_perio_tx_fifo_sizes(struct core_if *core_if)
{
	ulong regs = (u32) core_if->core_global_regs;
	u32 *param_size = &core_if->core_params->dev_perio_tx_fifo_size[0];
	u32 i, size;

	for (i = 0; i < MAX_PERIO_FIFOS; i++, param_size++) {
		size = dwc_reg_read(regs, DWC_DPTX_FSIZ_DIPTXF(i));
		*param_size = size;
	}
	return 0;
}

/**
 * Checks that parameter settings for the Tx FIFO sizes are correct according to
 * the hardware configuration.  Sets the size to the hardware configuration if
 * an incorrect size is detected.
 */
static int set_valid_tx_fifo_sizes(struct core_if *core_if)
{
	ulong regs = (u32) core_if->core_global_regs;
	u32 *param_size = &core_if->core_params->dev_tx_fifo_size[0];
	u32 i, size;

	for (i = 0; i < MAX_TX_FIFOS; i++, param_size) {
		size = dwc_reg_read(regs,  DWC_DPTX_FSIZ_DIPTXF(i));
		*param_size = size;
	}
	return 0;
}

/**
 * This function is called during module intialization to verify that
 * the module parameters are in a valid state.
 */
int check_parameters(struct core_if *core_if)
{
	struct core_params *dwc_otg_module_params = core_if->core_params;
	/* Default values */
	dwc_otg_module_params->dma_enable = dwc_param_dma_enable_default;
	dwc_otg_module_params->phy_ulpi_ddr = dwc_param_phy_ulpi_ddr_default;
	dwc_otg_module_params->dma_burst_size = dwc_param_dma_burst_size_default;
	dwc_otg_module_params->phy_utmi_width = dwc_param_phy_utmi_width_default;

	/*
	 * Hardware configurations of the OTG core.
	 */
	dwc_otg_module_params->enable_dynamic_fifo =
	    DWC_HWCFG2_DYN_FIFO_RD(core_if->hwcfg2);
	dwc_otg_module_params->dev_rx_fifo_size =
	    dwc_reg_read(core_if->core_global_regs, DWC_GRXFSIZ);
	dwc_otg_module_params->dev_nperio_tx_fifo_size =
	    dwc_reg_read(core_if->core_global_regs, DWC_GNPTXFSIZ) >> 16;

	dwc_otg_module_params->host_rx_fifo_size =
	    dwc_reg_read(core_if->core_global_regs, DWC_GRXFSIZ);
	dwc_otg_module_params->host_nperio_tx_fifo_size =
	    dwc_reg_read(core_if->core_global_regs, DWC_GNPTXFSIZ) >> 16;
	dwc_otg_module_params->host_perio_tx_fifo_size =
	    dwc_reg_read(core_if->core_global_regs, DWC_HPTXFSIZ) >> 16;
	dwc_otg_module_params->max_transfer_size =
	    (1 << (DWC_HWCFG3_XFERSIZE_CTR_WIDTH_RD(core_if->hwcfg3) + 11))
	    - 1;
	dwc_otg_module_params->max_packet_count =
	    (1 << (DWC_HWCFG3_PKTSIZE_CTR_WIDTH_RD(core_if->hwcfg3) + 4))
	    - 1;

	dwc_otg_module_params->host_channels =
	    DWC_HWCFG2_NO_HST_CHAN_RD(core_if->hwcfg2) + 1;
	dwc_otg_module_params->dev_endpoints =
	    DWC_HWCFG2_NO_DEV_EP_RD(core_if->hwcfg2);
	dwc_otg_module_params->en_multiple_tx_fifo =
	    (DWC_HWCFG4_DED_FIFO_ENA_RD(core_if->hwcfg4) == 0)
	    ? 0 : 1, 0;
	set_valid_perio_tx_fifo_sizes(core_if);
	set_valid_tx_fifo_sizes(core_if);

	return 0;
}

module_param_named(dma_enable, dwc_otg_module_params.dma_enable, int, 0444);
MODULE_PARM_DESC(dma_enable, "DMA Mode 0=Slave 1=DMA enabled");
