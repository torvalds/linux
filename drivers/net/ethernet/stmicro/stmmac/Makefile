# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_STMMAC_ETH) += stmmac.o
stmmac-objs:= stmmac_main.o stmmac_ethtool.o stmmac_mdio.o ring_mode.o	\
	      chain_mode.o dwmac_lib.o dwmac1000_core.o dwmac1000_dma.o	\
	      dwmac100_core.o dwmac100_dma.o enh_desc.o norm_desc.o	\
	      mmc_core.o stmmac_hwtstamp.o stmmac_ptp.o dwmac4_descs.o	\
	      dwmac4_dma.o dwmac4_lib.o dwmac4_core.o dwmac5.o hwif.o \
	      stmmac_tc.o dwxgmac2_core.o dwxgmac2_dma.o dwxgmac2_descs.o \
	      stmmac_xdp.o stmmac_est.o \
	      $(stmmac-y)

stmmac-$(CONFIG_STMMAC_SELFTESTS) += stmmac_selftests.o

# Ordering matters. Generic driver must be last.
obj-$(CONFIG_STMMAC_PLATFORM)	+= stmmac-platform.o
obj-$(CONFIG_DWMAC_ANARION)	+= dwmac-anarion.o
obj-$(CONFIG_DWMAC_INGENIC)	+= dwmac-ingenic.o
obj-$(CONFIG_DWMAC_IPQ806X)	+= dwmac-ipq806x.o
obj-$(CONFIG_DWMAC_LPC18XX)	+= dwmac-lpc18xx.o
obj-$(CONFIG_DWMAC_MEDIATEK)	+= dwmac-mediatek.o
obj-$(CONFIG_DWMAC_MESON)	+= dwmac-meson.o dwmac-meson8b.o
obj-$(CONFIG_DWMAC_QCOM_ETHQOS)	+= dwmac-qcom-ethqos.o
obj-$(CONFIG_DWMAC_ROCKCHIP)	+= dwmac-rk.o
obj-$(CONFIG_DWMAC_SOCFPGA)	+= dwmac-altr-socfpga.o
obj-$(CONFIG_DWMAC_STARFIVE)	+= dwmac-starfive.o
obj-$(CONFIG_DWMAC_STI)		+= dwmac-sti.o
obj-$(CONFIG_DWMAC_STM32)	+= dwmac-stm32.o
obj-$(CONFIG_DWMAC_SUNXI)	+= dwmac-sunxi.o
obj-$(CONFIG_DWMAC_SUN8I)	+= dwmac-sun8i.o
obj-$(CONFIG_DWMAC_DWC_QOS_ETH)	+= dwmac-dwc-qos-eth.o
obj-$(CONFIG_DWMAC_INTEL_PLAT)	+= dwmac-intel-plat.o
obj-$(CONFIG_DWMAC_LOONGSON1)	+= dwmac-loongson1.o
obj-$(CONFIG_DWMAC_GENERIC)	+= dwmac-generic.o
obj-$(CONFIG_DWMAC_IMX8)	+= dwmac-imx.o
obj-$(CONFIG_DWMAC_TEGRA)	+= dwmac-tegra.o
obj-$(CONFIG_DWMAC_VISCONTI)	+= dwmac-visconti.o
stmmac-platform-objs:= stmmac_platform.o
dwmac-altr-socfpga-objs := dwmac-socfpga.o

obj-$(CONFIG_STMMAC_PCI)	+= stmmac-pci.o
obj-$(CONFIG_DWMAC_INTEL)	+= dwmac-intel.o
obj-$(CONFIG_DWMAC_LOONGSON)	+= dwmac-loongson.o
stmmac-pci-objs:= stmmac_pci.o
