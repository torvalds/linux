# SPDX-License-Identifier: GPL-2.0
#dmaengine debug flags
subdir-ccflags-$(CONFIG_DMADEVICES_DEBUG)  := -DDEBUG
subdir-ccflags-$(CONFIG_DMADEVICES_VDEBUG) += -DVERBOSE_DEBUG

#core
obj-$(CONFIG_DMA_ENGINE) += dmaengine.o
obj-$(CONFIG_DMA_VIRTUAL_CHANNELS) += virt-dma.o
obj-$(CONFIG_DMA_ACPI) += acpi-dma.o
obj-$(CONFIG_DMA_OF) += of-dma.o

#dmatest
obj-$(CONFIG_DMATEST) += dmatest.o

#devices
obj-$(CONFIG_ALTERA_MSGDMA) += altera-msgdma.o
obj-$(CONFIG_AMBA_PL08X) += amba-pl08x.o
obj-$(CONFIG_AMCC_PPC440SPE_ADMA) += ppc4xx/
obj-$(CONFIG_AMD_PTDMA) += ptdma/
obj-$(CONFIG_APPLE_ADMAC) += apple-admac.o
obj-$(CONFIG_AT_HDMAC) += at_hdmac.o
obj-$(CONFIG_AT_XDMAC) += at_xdmac.o
obj-$(CONFIG_AXI_DMAC) += dma-axi-dmac.o
obj-$(CONFIG_BCM_SBA_RAID) += bcm-sba-raid.o
obj-$(CONFIG_DMA_BCM2835) += bcm2835-dma.o
obj-$(CONFIG_DMA_JZ4780) += dma-jz4780.o
obj-$(CONFIG_DMA_SA11X0) += sa11x0-dma.o
obj-$(CONFIG_DMA_SUN4I) += sun4i-dma.o
obj-$(CONFIG_DMA_SUN6I) += sun6i-dma.o
obj-$(CONFIG_DW_AXI_DMAC) += dw-axi-dmac/
obj-$(CONFIG_DW_DMAC_CORE) += dw/
obj-$(CONFIG_DW_EDMA) += dw-edma/
obj-$(CONFIG_EP93XX_DMA) += ep93xx_dma.o
obj-$(CONFIG_FSL_DMA) += fsldma.o
obj-$(CONFIG_FSL_EDMA) += fsl-edma.o fsl-edma-common.o
obj-$(CONFIG_MCF_EDMA) += mcf-edma.o fsl-edma-common.o
obj-$(CONFIG_FSL_QDMA) += fsl-qdma.o
obj-$(CONFIG_FSL_RAID) += fsl_raid.o
obj-$(CONFIG_HISI_DMA) += hisi_dma.o
obj-$(CONFIG_HSU_DMA) += hsu/
obj-$(CONFIG_IMG_MDC_DMA) += img-mdc-dma.o
obj-$(CONFIG_IMX_DMA) += imx-dma.o
obj-$(CONFIG_IMX_SDMA) += imx-sdma.o
obj-$(CONFIG_INTEL_IDMA64) += idma64.o
obj-$(CONFIG_INTEL_IOATDMA) += ioat/
obj-y += idxd/
obj-$(CONFIG_K3_DMA) += k3dma.o
obj-$(CONFIG_LPC18XX_DMAMUX) += lpc18xx-dmamux.o
obj-$(CONFIG_MILBEAUT_HDMAC) += milbeaut-hdmac.o
obj-$(CONFIG_MILBEAUT_XDMAC) += milbeaut-xdmac.o
obj-$(CONFIG_MMP_PDMA) += mmp_pdma.o
obj-$(CONFIG_MMP_TDMA) += mmp_tdma.o
obj-$(CONFIG_MOXART_DMA) += moxart-dma.o
obj-$(CONFIG_MPC512X_DMA) += mpc512x_dma.o
obj-$(CONFIG_MV_XOR) += mv_xor.o
obj-$(CONFIG_MV_XOR_V2) += mv_xor_v2.o
obj-$(CONFIG_MXS_DMA) += mxs-dma.o
obj-$(CONFIG_NBPFAXI_DMA) += nbpfaxi.o
obj-$(CONFIG_OWL_DMA) += owl-dma.o
obj-$(CONFIG_PCH_DMA) += pch_dma.o
obj-$(CONFIG_PL330_DMA) += pl330.o
obj-$(CONFIG_PLX_DMA) += plx_dma.o
obj-$(CONFIG_PPC_BESTCOMM) += bestcomm/
obj-$(CONFIG_PXA_DMA) += pxa_dma.o
obj-$(CONFIG_RENESAS_DMA) += sh/
obj-$(CONFIG_SF_PDMA) += sf-pdma/
obj-$(CONFIG_STE_DMA40) += ste_dma40.o ste_dma40_ll.o
obj-$(CONFIG_STM32_DMA) += stm32-dma.o
obj-$(CONFIG_STM32_DMAMUX) += stm32-dmamux.o
obj-$(CONFIG_STM32_MDMA) += stm32-mdma.o
obj-$(CONFIG_SPRD_DMA) += sprd-dma.o
obj-$(CONFIG_TXX9_DMAC) += txx9dmac.o
obj-$(CONFIG_TEGRA186_GPC_DMA) += tegra186-gpc-dma.o
obj-$(CONFIG_TEGRA20_APB_DMA) += tegra20-apb-dma.o
obj-$(CONFIG_TEGRA210_ADMA) += tegra210-adma.o
obj-$(CONFIG_TIMB_DMA) += timb_dma.o
obj-$(CONFIG_UNIPHIER_MDMAC) += uniphier-mdmac.o
obj-$(CONFIG_UNIPHIER_XDMAC) += uniphier-xdmac.o
obj-$(CONFIG_XGENE_DMA) += xgene-dma.o
obj-$(CONFIG_ST_FDMA) += st_fdma.o
obj-$(CONFIG_FSL_DPAA2_QDMA) += fsl-dpaa2-qdma/
obj-$(CONFIG_INTEL_LDMA) += lgm/

obj-y += mediatek/
obj-y += qcom/
obj-y += ti/
obj-y += xilinx/
