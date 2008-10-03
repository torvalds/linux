#
# Makefile for the Freescale Ethernet controllers
#

obj-$(CONFIG_FS_ENET) += fs_enet.o

fs_enet-$(CONFIG_FS_ENET_HAS_SCC) += mac-scc.o
fs_enet-$(CONFIG_FS_ENET_HAS_FEC) += mac-fec.o
fs_enet-$(CONFIG_FS_ENET_HAS_FCC) += mac-fcc.o

obj-$(CONFIG_FS_ENET_MDIO_FEC) += mii-fec.o
obj-$(CONFIG_FS_ENET_MDIO_FCC) += mii-bitbang.o

fs_enet-objs := fs_enet-main.o $(fs_enet-m)
