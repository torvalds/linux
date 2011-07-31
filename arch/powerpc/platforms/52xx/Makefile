#
# Makefile for 52xx based boards
#
obj-y				+= mpc52xx_pic.o mpc52xx_common.o mpc52xx_gpt.o
obj-$(CONFIG_PCI)		+= mpc52xx_pci.o

obj-$(CONFIG_PPC_MPC5200_SIMPLE) += mpc5200_simple.o
obj-$(CONFIG_PPC_EFIKA)		+= efika.o
obj-$(CONFIG_PPC_LITE5200)	+= lite5200.o
obj-$(CONFIG_PPC_MEDIA5200)	+= media5200.o

obj-$(CONFIG_PM)		+= mpc52xx_sleep.o mpc52xx_pm.o
ifeq ($(CONFIG_PPC_LITE5200),y)
	obj-$(CONFIG_PM)	+= lite5200_sleep.o lite5200_pm.o
endif

obj-$(CONFIG_PPC_MPC5200_GPIO)	+= mpc52xx_gpio.o
obj-$(CONFIG_PPC_MPC5200_LPBFIFO)	+= mpc52xx_lpbfifo.o
