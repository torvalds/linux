# SPDX-License-Identifier: GPL-2.0
#
# Makefile for MMC/SD host controller drivers
#

obj-$(CONFIG_MMC_ARMMMCI) += armmmci.o
armmmci-y := mmci.o
armmmci-$(CONFIG_MMC_QCOM_DML) += mmci_qcom_dml.o
armmmci-$(CONFIG_MMC_STM32_SDMMC) += mmci_stm32_sdmmc.o
obj-$(CONFIG_MMC_PXA)		+= pxamci.o
obj-$(CONFIG_MMC_MXC)		+= mxcmmc.o
obj-$(CONFIG_MMC_MXS)		+= mxs-mmc.o
obj-$(CONFIG_MMC_SDHCI)		+= sdhci.o
obj-$(CONFIG_MMC_SDHCI_PCI)	+= sdhci-pci.o
sdhci-pci-y			+= sdhci-pci-core.o sdhci-pci-o2micro.o sdhci-pci-arasan.o \
				   sdhci-pci-dwc-mshc.o sdhci-pci-gli.o
obj-$(subst m,y,$(CONFIG_MMC_SDHCI_PCI))	+= sdhci-pci-data.o
obj-$(CONFIG_MMC_SDHCI_ACPI)	+= sdhci-acpi.o
obj-$(CONFIG_MMC_SDHCI_PXAV3)	+= sdhci-pxav3.o
obj-$(CONFIG_MMC_SDHCI_PXAV2)	+= sdhci-pxav2.o
obj-$(CONFIG_MMC_SDHCI_S3C)	+= sdhci-s3c.o
obj-$(CONFIG_MMC_SDHCI_SIRF)   	+= sdhci-sirf.o
obj-$(CONFIG_MMC_SDHCI_F_SDH30)	+= sdhci_f_sdh30.o
obj-$(CONFIG_MMC_SDHCI_MILBEAUT)	+= sdhci-milbeaut.o
obj-$(CONFIG_MMC_SDHCI_SPEAR)	+= sdhci-spear.o
obj-$(CONFIG_MMC_SDHCI_AM654)	+= sdhci_am654.o
obj-$(CONFIG_MMC_WBSD)		+= wbsd.o
obj-$(CONFIG_MMC_AU1X)		+= au1xmmc.o
obj-$(CONFIG_MMC_ALCOR)	+= alcor.o
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
obj-$(CONFIG_MMC_SDHI)		+= renesas_sdhi_core.o
obj-$(CONFIG_MMC_SDHI_SYS_DMAC)		+= renesas_sdhi_sys_dmac.o
obj-$(CONFIG_MMC_SDHI_INTERNAL_DMAC)	+= renesas_sdhi_internal_dmac.o
obj-$(CONFIG_MMC_UNIPHIER)	+= uniphier-sd.o
obj-$(CONFIG_MMC_CB710)		+= cb710-mmc.o
obj-$(CONFIG_MMC_VIA_SDMMC)	+= via-sdmmc.o
octeon-mmc-objs := cavium.o cavium-octeon.o
obj-$(CONFIG_MMC_CAVIUM_OCTEON) += octeon-mmc.o
thunderx-mmc-objs := cavium.o cavium-thunderx.o
obj-$(CONFIG_MMC_CAVIUM_THUNDERX) += thunderx-mmc.o
obj-$(CONFIG_MMC_DW)		+= dw_mmc.o
obj-$(CONFIG_MMC_DW_PLTFM)	+= dw_mmc-pltfm.o
obj-$(CONFIG_MMC_DW_BLUEFIELD)	+= dw_mmc-bluefield.o
obj-$(CONFIG_MMC_DW_EXYNOS)	+= dw_mmc-exynos.o
obj-$(CONFIG_MMC_DW_HI3798CV200) += dw_mmc-hi3798cv200.o
obj-$(CONFIG_MMC_DW_K3)		+= dw_mmc-k3.o
obj-$(CONFIG_MMC_DW_PCI)	+= dw_mmc-pci.o
obj-$(CONFIG_MMC_DW_ROCKCHIP)	+= dw_mmc-rockchip.o
obj-$(CONFIG_MMC_DW_ZX)		+= dw_mmc-zx.o
obj-$(CONFIG_MMC_SH_MMCIF)	+= sh_mmcif.o
obj-$(CONFIG_MMC_JZ4740)	+= jz4740_mmc.o
obj-$(CONFIG_MMC_VUB300)	+= vub300.o
obj-$(CONFIG_MMC_USHC)		+= ushc.o
obj-$(CONFIG_MMC_WMT)		+= wmt-sdmmc.o
obj-$(CONFIG_MMC_MESON_GX)	+= meson-gx-mmc.o
meson-mx-sdhc-objs 		:= meson-mx-sdhc-clkc.o meson-mx-sdhc-mmc.o
obj-$(CONFIG_MMC_MESON_MX_SDHC)	+= meson-mx-sdhc.o
obj-$(CONFIG_MMC_MESON_MX_SDIO)	+= meson-mx-sdio.o
obj-$(CONFIG_MMC_MOXART)	+= moxart-mmc.o
obj-$(CONFIG_MMC_SUNXI)		+= sunxi-mmc.o
obj-$(CONFIG_MMC_USDHI6ROL0)	+= usdhi6rol0.o
obj-$(CONFIG_MMC_TOSHIBA_PCI)	+= toshsd.o
obj-$(CONFIG_MMC_BCM2835)	+= bcm2835.o
obj-$(CONFIG_MMC_OWL)		+= owl-mmc.o

obj-$(CONFIG_MMC_REALTEK_PCI)	+= rtsx_pci_sdmmc.o
obj-$(CONFIG_MMC_REALTEK_USB)	+= rtsx_usb_sdmmc.o

obj-$(CONFIG_MMC_SDHCI_PLTFM)		+= sdhci-pltfm.o
obj-$(CONFIG_MMC_SDHCI_CADENCE)		+= sdhci-cadence.o
obj-$(CONFIG_MMC_SDHCI_CNS3XXX)		+= sdhci-cns3xxx.o
obj-$(CONFIG_MMC_SDHCI_ESDHC_MCF)       += sdhci-esdhc-mcf.o
obj-$(CONFIG_MMC_SDHCI_ESDHC_IMX)	+= sdhci-esdhc-imx.o
obj-$(CONFIG_MMC_SDHCI_DOVE)		+= sdhci-dove.o
obj-$(CONFIG_MMC_SDHCI_TEGRA)		+= sdhci-tegra.o
obj-$(CONFIG_MMC_SDHCI_OF_ARASAN)	+= sdhci-of-arasan.o
obj-$(CONFIG_MMC_SDHCI_OF_ASPEED)	+= sdhci-of-aspeed.o
obj-$(CONFIG_MMC_SDHCI_OF_AT91)		+= sdhci-of-at91.o
obj-$(CONFIG_MMC_SDHCI_OF_ESDHC)	+= sdhci-of-esdhc.o
obj-$(CONFIG_MMC_SDHCI_OF_HLWD)		+= sdhci-of-hlwd.o
obj-$(CONFIG_MMC_SDHCI_OF_DWCMSHC)	+= sdhci-of-dwcmshc.o
obj-$(CONFIG_MMC_SDHCI_OF_SPARX5)	+= sdhci-of-sparx5.o
obj-$(CONFIG_MMC_SDHCI_BCM_KONA)	+= sdhci-bcm-kona.o
obj-$(CONFIG_MMC_SDHCI_IPROC)		+= sdhci-iproc.o
obj-$(CONFIG_MMC_SDHCI_MSM)		+= sdhci-msm.o
obj-$(CONFIG_MMC_SDHCI_ST)		+= sdhci-st.o
obj-$(CONFIG_MMC_SDHCI_MICROCHIP_PIC32)	+= sdhci-pic32.o
obj-$(CONFIG_MMC_SDHCI_BRCMSTB)		+= sdhci-brcmstb.o
obj-$(CONFIG_MMC_SDHCI_OMAP)		+= sdhci-omap.o
obj-$(CONFIG_MMC_SDHCI_SPRD)		+= sdhci-sprd.o
obj-$(CONFIG_MMC_CQHCI)			+= cqhci.o
obj-$(CONFIG_MMC_HSQ)			+= mmc_hsq.o

ifeq ($(CONFIG_CB710_DEBUG),y)
	CFLAGS-cb710-mmc	+= -DDEBUG
endif

obj-$(CONFIG_MMC_SDHCI_XENON)	+= sdhci-xenon-driver.o
sdhci-xenon-driver-y		+= sdhci-xenon.o sdhci-xenon-phy.o
