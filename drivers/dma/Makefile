ifeq ($(CONFIG_DMADEVICES_DEBUG),y)
	EXTRA_CFLAGS	+= -DDEBUG
endif
ifeq ($(CONFIG_DMADEVICES_VDEBUG),y)
	EXTRA_CFLAGS	+= -DVERBOSE_DEBUG
endif

obj-$(CONFIG_DMA_ENGINE) += dmaengine.o
obj-$(CONFIG_NET_DMA) += iovlock.o
obj-$(CONFIG_DMATEST) += dmatest.o
obj-$(CONFIG_INTEL_IOATDMA) += ioat/
obj-$(CONFIG_INTEL_IOP_ADMA) += iop-adma.o
obj-$(CONFIG_FSL_DMA) += fsldma.o
obj-$(CONFIG_MPC512X_DMA) += mpc512x_dma.o
obj-$(CONFIG_MV_XOR) += mv_xor.o
obj-$(CONFIG_DW_DMAC) += dw_dmac.o
obj-$(CONFIG_AT_HDMAC) += at_hdmac.o
obj-$(CONFIG_MX3_IPU) += ipu/
obj-$(CONFIG_TXX9_DMAC) += txx9dmac.o
obj-$(CONFIG_SH_DMAE) += shdma.o
obj-$(CONFIG_COH901318) += coh901318.o coh901318_lli.o
obj-$(CONFIG_AMCC_PPC440SPE_ADMA) += ppc4xx/
obj-$(CONFIG_TIMB_DMA) += timb_dma.o
obj-$(CONFIG_STE_DMA40) += ste_dma40.o ste_dma40_ll.o
obj-$(CONFIG_PL330_DMA) += pl330.o
