obj-$(CONFIG_STMMAC_ETH) += stmmac.o
stmmac-$(CONFIG_STMMAC_RING) += ring_mode.o
stmmac-$(CONFIG_STMMAC_CHAINED) += chain_mode.o
stmmac-$(CONFIG_STMMAC_PLATFORM) += stmmac_platform.o
stmmac-$(CONFIG_STMMAC_PCI) += stmmac_pci.o
stmmac-objs:= stmmac_main.o stmmac_ethtool.o stmmac_mdio.o	\
	      dwmac_lib.o dwmac1000_core.o  dwmac1000_dma.o	\
	      dwmac100_core.o dwmac100_dma.o enh_desc.o  norm_desc.o \
	      mmc_core.o $(stmmac-y)
