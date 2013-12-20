#
# Makefile for the Xilink network device drivers.
#

ll_temac-objs := ll_temac_main.o ll_temac_mdio.o
obj-$(CONFIG_XILINX_LL_TEMAC) += ll_temac.o
obj-$(CONFIG_XILINX_EMACLITE) += xilinx_emaclite.o
obj-$(CONFIG_XILINX_PS_EMAC) += xilinx_emacps.o
xilinx_emac-objs := xilinx_axienet_main.o xilinx_axienet_mdio.o
obj-$(CONFIG_XILINX_AXI_EMAC) += xilinx_emac.o
