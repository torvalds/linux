#
# Makefile for MMC/SD host controller drivers
#

ifeq ($(CONFIG_MMC_DEBUG),y)
	EXTRA_CFLAGS		+= -DDEBUG
endif

obj-$(CONFIG_MMC_ARMMMCI)	+= mmci.o
obj-$(CONFIG_MMC_PXA)		+= pxamci.o
obj-$(CONFIG_MMC_IMX)		+= imxmmc.o
obj-$(CONFIG_MMC_SDHCI)		+= sdhci.o
obj-$(CONFIG_MMC_SDHCI_PCI)	+= sdhci-pci.o
obj-$(CONFIG_MMC_RICOH_MMC)	+= ricoh_mmc.o
obj-$(CONFIG_MMC_WBSD)		+= wbsd.o
obj-$(CONFIG_MMC_AU1X)		+= au1xmmc.o
obj-$(CONFIG_MMC_OMAP)		+= omap.o
obj-$(CONFIG_MMC_AT91)		+= at91_mci.o
obj-$(CONFIG_MMC_ATMELMCI)	+= atmel-mci.o
obj-$(CONFIG_MMC_TIFM_SD)	+= tifm_sd.o
obj-$(CONFIG_MMC_SPI)		+= mmc_spi.o
ifeq ($(CONFIG_OF),y)
obj-$(CONFIG_MMC_SPI)		+= of_mmc_spi.o
endif
obj-$(CONFIG_MMC_S3C)   	+= s3cmci.o
obj-$(CONFIG_MMC_SDRICOH_CS)	+= sdricoh_cs.o
obj-$(CONFIG_MMC_TMIO)		+= tmio_mmc.o

