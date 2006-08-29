#
# Makefile for the Freescale Ethernet controllers
#

obj-$(CONFIG_FS_ENET) += fs_enet.o

obj-$(CONFIG_8xx) += mac-fec.o mac-scc.o mii-fec.o
obj-$(CONFIG_CPM2) += mac-fcc.o mii-bitbang.o

fs_enet-objs := fs_enet-main.o
