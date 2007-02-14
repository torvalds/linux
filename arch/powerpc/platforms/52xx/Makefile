#
# Makefile for 52xx based boards
#
ifeq ($(CONFIG_PPC_MERGE),y)
obj-y				+= mpc52xx_pic.o mpc52xx_common.o
obj-$(CONFIG_PCI)		+= mpc52xx_pci.o
endif

obj-$(CONFIG_PPC_EFIKA)		+= efika.o
obj-$(CONFIG_PPC_LITE5200)	+= lite5200.o
