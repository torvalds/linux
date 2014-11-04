obj-$(CONFIG_STMMAC_ETH) += stmmac.o
stmmac-$(CONFIG_STMMAC_PCI) += stmmac_pci.o
stmmac-$(CONFIG_STMMAC_PLATFORM) +=	stmmac_platform.o dwmac-meson.o \
					dwmac-sunxi.o dwmac-sti.o \
					dwmac-socfpga.o
stmmac-objs:= stmmac_main.o stmmac_ethtool.o stmmac_mdio.o ring_mode.o	\
	      chain_mode.o dwmac_lib.o dwmac1000_core.o  dwmac1000_dma.o \
	      dwmac100_core.o dwmac100_dma.o enh_desc.o  norm_desc.o \
	      mmc_core.o stmmac_hwtstamp.o stmmac_ptp.o $(stmmac-y)
