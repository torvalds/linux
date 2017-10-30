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
obj-$(CONFIG_AT_HDMAC) += at_hdmac.o
obj-$(CONFIG_AT_XDMAC) += at_xdmac.o
obj-$(CONFIG_AXI_DMAC) += dma-axi-dmac.o
obj-$(CONFIG_BCM_SBA_RAID) += bcm-sba-raid.o
obj-$(CONFIG_COH901318) += coh901318.o coh901318_lli.o
obj-$(CONFIG_DMA_BCM2835) += bcm2835-dma.o
obj-$(CONFIG_DMA_JZ4740) += dma-jz4740.o
obj-$(CONFIG_DMA_JZ4780) += dma-jz4780.o
obj-$(CONFIG_DMA_OMAP) += omap-dma.o
obj-$(CONFIG_DMA_SA11X0) += sa11x0-dma.o
obj-$(CONFIG_DMA_SUN4I) += sun4i-dma.o
obj-$(CONFIG_DMA_SUN6I) += sun6i-dma.o
obj-$(CONFIG_DW_DMAC_CORE) += dw/
obj-$(CONFIG_EP93XX_DMA) += ep93xx_dma.o
obj-$(CONFIG_FSL_DMA) += fsldma.o
obj-$(CONFIG_FSL_EDMA) += fsl-edma.o
obj-$(CONFIG_FSL_RAID) += fsl_raid.o
obj-$(CONFIG_HSU_DMA) += hsu/
obj-$(CONFIG_IMG_MDC_DMA) += img-mdc-dma.o
obj-$(CONFIG_IMX_DMA) += imx-dma.o
obj-$(CONFIG_IMX_SDMA) += imx-sdma.o
obj-$(CONFIG_INTEL_IDMA64) += idma64.o
obj-$(CONFIG_INTEL_IOATDMA) += ioat/
obj-$(CONFIG_INTEL_IOP_ADMA) += iop-adma.o
obj-$(CONFIG_INTEL_MIC_X100_DMA) += mic_x100_dma.o
obj-$(CONFIG_K3_DMA) += k3dma.o
obj-$(CONFIG_LPC18XX_DMAMUX) += lpc18xx-dmamux.o
obj-$(CONFIG_MMP_PDMA) += mmp_pdma.o
obj-$(CONFIG_MMP_TDMA) += mmp_tdma.o
obj-$(CONFIG_MOXART_DMA) += moxart-dma.o
obj-$(CONFIG_MPC512X_DMA) += mpc512x_dma.o
obj-$(CONFIG_MV_XOR) += mv_xor.o
obj-$(CONFIG_MV_XOR_V2) += mv_xor_v2.o
obj-$(CONFIG_MXS_DMA) += mxs-dma.o
obj-$(CONFIG_MX3_IPU) += ipu/
obj-$(CONFIG_NBPFAXI_DMA) += nbpfaxi.o
obj-$(CONFIG_PCH_DMA) += pch_dma.o
obj-$(CONFIG_PL330_DMA) += pl330.o
obj-$(CONFIG_PPC_BESTCOMM) += bestcomm/
obj-$(CONFIG_PXA_DMA) += pxa_dma.o
obj-$(CONFIG_RENESAS_DMA) += sh/
obj-$(CONFIG_SIRF_DMA) += sirf-dma.o
obj-$(CONFIG_STE_DMA40) += ste_dma40.o ste_dma40_ll.o
obj-$(CONFIG_STM32_DMA) += stm32-dma.o
obj-$(CONFIG_S3C24XX_DMAC) += s3c24xx-dma.o
obj-$(CONFIG_TXX9_DMAC) += txx9dmac.o
obj-$(CONFIG_TEGRA20_APB_DMA) += tegra20-apb-dma.o
obj-$(CONFIG_TEGRA210_ADMA) += tegra210-adma.o
obj-$(CONFIG_TIMB_DMA) += timb_dma.o
obj-$(CONFIG_TI_CPPI41) += cppi41.o
obj-$(CONFIG_TI_DMA_CROSSBAR) += ti-dma-crossbar.o
obj-$(CONFIG_TI_EDMA) += edma.o
obj-$(CONFIG_XGENE_DMA) += xgene-dma.o
obj-$(CONFIG_ZX_DMA) += zx_dma.o
obj-$(CONFIG_ST_FDMA) += st_fdma.o

obj-y += qcom/
obj-y += xilinx/
