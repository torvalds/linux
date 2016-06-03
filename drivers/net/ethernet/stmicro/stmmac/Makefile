obj-$(CONFIG_STMMAC_ETH) += stmmac.o
stmmac-objs:= stmmac_main.o stmmac_ethtool.o stmmac_mdio.o ring_mode.o	\
	      chain_mode.o dwmac_lib.o dwmac1000_core.o dwmac1000_dma.o	\
	      dwmac100_core.o dwmac100_dma.o enh_desc.o norm_desc.o	\
	      mmc_core.o stmmac_hwtstamp.o stmmac_ptp.o dwmac4_descs.o	\
	      dwmac4_dma.o dwmac4_lib.o dwmac4_core.o $(stmmac-y)

# Ordering matters. Generic driver must be last.
obj-$(CONFIG_STMMAC_PLATFORM)	+= stmmac-platform.o
obj-$(CONFIG_DWMAC_IPQ806X)	+= dwmac-ipq806x.o
obj-$(CONFIG_DWMAC_LPC18XX)	+= dwmac-lpc18xx.o
obj-$(CONFIG_DWMAC_MESON)	+= dwmac-meson.o
obj-$(CONFIG_DWMAC_ROCKCHIP)	+= dwmac-rk.o
obj-$(CONFIG_DWMAC_SOCFPGA)	+= dwmac-socfpga.o
obj-$(CONFIG_DWMAC_STI)		+= dwmac-sti.o
obj-$(CONFIG_DWMAC_SUNXI)	+= dwmac-sunxi.o
obj-$(CONFIG_DWMAC_GENERIC)	+= dwmac-generic.o
stmmac-platform-objs:= stmmac_platform.o

obj-$(CONFIG_STMMAC_PCI) += stmmac-pci.o
stmmac-pci-objs:= stmmac_pci.o
