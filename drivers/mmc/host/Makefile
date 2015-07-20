#
# Makefile for MMC/SD host controller drivers
#

obj-$(CONFIG_MMC_ARMMMCI)	+= mmci.o
obj-$(CONFIG_MMC_QCOM_DML)	+= mmci_qcom_dml.o
obj-$(CONFIG_MMC_PXA)		+= pxamci.o
obj-$(CONFIG_MMC_MXC)		+= mxcmmc.o
obj-$(CONFIG_MMC_MXS)		+= mxs-mmc.o
obj-$(CONFIG_MMC_SDHCI)		+= sdhci.o
obj-$(CONFIG_MMC_SDHCI_PCI)	+= sdhci-pci.o
obj-$(subst m,y,$(CONFIG_MMC_SDHCI_PCI))	+= sdhci-pci-data.o
obj-$(subst m,y,$(CONFIG_MMC_SDHCI_PCI))	+= sdhci-pci-o2micro.o
obj-$(CONFIG_MMC_SDHCI_ACPI)	+= sdhci-acpi.o
obj-$(CONFIG_MMC_SDHCI_PXAV3)	+= sdhci-pxav3.o
obj-$(CONFIG_MMC_SDHCI_PXAV2)	+= sdhci-pxav2.o
obj-$(CONFIG_MMC_SDHCI_S3C)	+= sdhci-s3c.o
obj-$(CONFIG_MMC_SDHCI_SIRF)   	+= sdhci-sirf.o
obj-$(CONFIG_MMC_SDHCI_F_SDH30)	+= sdhci_f_sdh30.o
obj-$(CONFIG_MMC_SDHCI_SPEAR)	+= sdhci-spear.o
obj-$(CONFIG_MMC_WBSD)		+= wbsd.o
obj-$(CONFIG_MMC_AU1X)		+= au1xmmc.o
obj-$(CONFIG_MMC_MTK)		+= mtk-sd.o
obj-$(CONFIG_MMC_OMAP)		+= omap.o
obj-$(CONFIG_MMC_OMAP_HS)	+= omap_hsmmc.o
obj-$(CONFIG_MMC_ATMELMCI)	+= atmel-mci.o
obj-$(CONFIG_MMC_TIFM_SD)	+= tifm_sd.o
obj-$(CONFIG_MMC_MVSDIO)	+= mvsdio.o
obj-$(CONFIG_MMC_DAVINCI)       += davinci_mmc.o
obj-$(CONFIG_MMC_GOLDFISH)	+= android-goldfish.o
obj-$(CONFIG_MMC_SPI)		+= mmc_spi.o
ifeq ($(CONFIG_OF),y)
obj-$(CONFIG_MMC_SPI)		+= of_mmc_spi.o
endif
obj-$(CONFIG_MMC_S3C)   	+= s3cmci.o
obj-$(CONFIG_MMC_SDRICOH_CS)	+= sdricoh_cs.o
obj-$(CONFIG_MMC_TMIO)		+= tmio_mmc.o
obj-$(CONFIG_MMC_TMIO_CORE)	+= tmio_mmc_core.o
tmio_mmc_core-y			:= tmio_mmc_pio.o
tmio_mmc_core-$(subst m,y,$(CONFIG_MMC_SDHI))	+= tmio_mmc_dma.o
obj-$(CONFIG_MMC_SDHI)		+= sh_mobile_sdhi.o
obj-$(CONFIG_MMC_CB710)		+= cb710-mmc.o
obj-$(CONFIG_MMC_VIA_SDMMC)	+= via-sdmmc.o
obj-$(CONFIG_SDH_BFIN)		+= bfin_sdh.o
obj-$(CONFIG_MMC_DW)		+= dw_mmc.o
obj-$(CONFIG_MMC_DW_PLTFM)	+= dw_mmc-pltfm.o
obj-$(CONFIG_MMC_DW_EXYNOS)	+= dw_mmc-exynos.o
obj-$(CONFIG_MMC_DW_K3)		+= dw_mmc-k3.o
obj-$(CONFIG_MMC_DW_PCI)	+= dw_mmc-pci.o
obj-$(CONFIG_MMC_DW_ROCKCHIP)	+= dw_mmc-rockchip.o
obj-$(CONFIG_MMC_SH_MMCIF)	+= sh_mmcif.o
obj-$(CONFIG_MMC_JZ4740)	+= jz4740_mmc.o
obj-$(CONFIG_MMC_VUB300)	+= vub300.o
obj-$(CONFIG_MMC_USHC)		+= ushc.o
obj-$(CONFIG_MMC_WMT)		+= wmt-sdmmc.o
obj-$(CONFIG_MMC_MOXART)	+= moxart-mmc.o
obj-$(CONFIG_MMC_SUNXI)		+= sunxi-mmc.o
obj-$(CONFIG_MMC_USDHI6ROL0)	+= usdhi6rol0.o
obj-$(CONFIG_MMC_TOSHIBA_PCI)	+= toshsd.o

obj-$(CONFIG_MMC_REALTEK_PCI)	+= rtsx_pci_sdmmc.o
obj-$(CONFIG_MMC_REALTEK_USB)	+= rtsx_usb_sdmmc.o

obj-$(CONFIG_MMC_SDHCI_PLTFM)		+= sdhci-pltfm.o
obj-$(CONFIG_MMC_SDHCI_CNS3XXX)		+= sdhci-cns3xxx.o
obj-$(CONFIG_MMC_SDHCI_ESDHC_IMX)	+= sdhci-esdhc-imx.o
obj-$(CONFIG_MMC_SDHCI_DOVE)		+= sdhci-dove.o
obj-$(CONFIG_MMC_SDHCI_TEGRA)		+= sdhci-tegra.o
obj-$(CONFIG_MMC_SDHCI_OF_ARASAN)	+= sdhci-of-arasan.o
obj-$(CONFIG_MMC_SDHCI_OF_ESDHC)	+= sdhci-of-esdhc.o
obj-$(CONFIG_MMC_SDHCI_OF_HLWD)		+= sdhci-of-hlwd.o
obj-$(CONFIG_MMC_SDHCI_BCM_KONA)	+= sdhci-bcm-kona.o
obj-$(CONFIG_MMC_SDHCI_BCM2835)		+= sdhci-bcm2835.o
obj-$(CONFIG_MMC_SDHCI_IPROC)		+= sdhci-iproc.o
obj-$(CONFIG_MMC_SDHCI_MSM)		+= sdhci-msm.o
obj-$(CONFIG_MMC_SDHCI_ST)		+= sdhci-st.o

ifeq ($(CONFIG_CB710_DEBUG),y)
	CFLAGS-cb710-mmc	+= -DDEBUG
endif
