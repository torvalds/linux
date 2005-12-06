#
# Makefile for the Freescale Ethernet controllers
#

obj-$(CONFIG_FS_ENET) += fs_enet.o

obj-$(CONFIG_8xx) += mac-fec.o mac-scc.o
obj-$(CONFIG_8260) += mac-fcc.o

fs_enet-objs := fs_enet-main.o fs_enet-mii.o mii-bitbang.o mii-fixed.o
