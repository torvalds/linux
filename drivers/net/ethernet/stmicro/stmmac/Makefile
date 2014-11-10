obj-$(CONFIG_STMMAC_ETH) += stmmac.o
stmmac-objs:= stmmac_main.o stmmac_ethtool.o stmmac_mdio.o ring_mode.o	\
	      chain_mode.o dwmac_lib.o dwmac1000_core.o dwmac1000_dma.o	\
	      dwmac100_core.o dwmac100_dma.o enh_desc.o norm_desc.o	\
	      mmc_core.o stmmac_hwtstamp.o stmmac_ptp.o $(stmmac-y)

obj-$(CONFIG_STMMAC_PLATFORM) += stmmac-platform.o
stmmac-platform-objs:= stmmac_platform.o dwmac-meson.o dwmac-sunxi.o	\
		       dwmac-sti.o dwmac-socfpga.o

obj-$(CONFIG_STMMAC_PCI) += stmmac-pci.o
stmmac-pci-objs:= stmmac_pci.o
