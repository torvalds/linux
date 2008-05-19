#
# Makefile for 52xx based boards
#
ifeq ($(CONFIG_PPC_MERGE),y)
obj-y				+= mpc52xx_pic.o mpc52xx_common.o
obj-$(CONFIG_PCI)		+= mpc52xx_pci.o
endif

obj-$(CONFIG_PPC_MPC5200_SIMPLE) += mpc5200_simple.o
obj-$(CONFIG_PPC_EFIKA)		+= efika.o
obj-$(CONFIG_PPC_LITE5200)	+= lite5200.o

obj-$(CONFIG_PM)		+= mpc52xx_sleep.o mpc52xx_pm.o
ifeq ($(CONFIG_PPC_LITE5200),y)
	obj-$(CONFIG_PM)	+= lite5200_sleep.o lite5200_pm.o
endif

obj-$(CONFIG_PPC_MPC5200_GPIO)	+= mpc52xx_gpio.o