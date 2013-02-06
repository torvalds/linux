# arch/arm/plat-s5p/Makefile
#
# Copyright (c) 2009 Samsung Electronics Co., Ltd.
# 		http://www.samsung.com/
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:= dummy.o
obj-				:=

# Core files

obj-y				+= dev-pmu.o
obj-y				+= dev-uart.o
obj-y				+= cpu.o
obj-y				+= clock.o
obj-y				+= irq.o
obj-y				+= reset.o
obj-y				+= reserve_mem.o
obj-$(CONFIG_S5P_EXT_INT)	+= irq-eint.o
obj-$(CONFIG_S5P_GPIO_INT)	+= irq-gpioint.o
obj-$(CONFIG_S5P_SYSTEM_MMU)	+= s5p-sysmmu.o
obj-$(CONFIG_PM)		+= pm.o
obj-$(CONFIG_PM)		+= irq-pm.o
obj-$(CONFIG_S5P_HRT) 		+= s5p-time.o
obj-$(CONFIG_S5P_BTS)		+= bts.o

# devices

obj-$(CONFIG_S3C_DEV_FIMC)	+= dev-fimc-s5p.o
obj-$(CONFIG_S5P_DEV_FIMC0)	+= dev-fimc0.o
obj-$(CONFIG_S5P_DEV_FIMC1)	+= dev-fimc1.o
obj-$(CONFIG_S5P_DEV_FIMC2)	+= dev-fimc2.o
obj-$(CONFIG_S5P_DEV_FIMC3)	+= dev-fimc3.o
obj-$(CONFIG_S5P_DEV_MFC)	+= dev-mfc.o
obj-$(CONFIG_S5P_DEV_FIMD0)	+= dev-fimd0.o
obj-$(CONFIG_S5P_DEV_FIMD1)	+= dev-fimd1.o
obj-$(CONFIG_S5P_DEV_I2C_HDMIPHY)      += dev-i2c-hdmiphy.o
obj-$(CONFIG_S5P_DEV_DP)	+= dev-dp.o
obj-$(CONFIG_S5P_DEV_FIMD_S5P)	+= dev-fimd-s5p.o
obj-$(CONFIG_S5P_DEV_TVOUT)	+= dev-tvout.o
obj-$(CONFIG_S5P_DEV_TV)	+= dev-tv.o
obj-$(CONFIG_S5P_DEV_FIMG2D)	+= dev-fimg2d.o
obj-$(CONFIG_S5P_DEV_ROTATOR)	+= dev-rotator.o
obj-$(CONFIG_S5P_DEV_ONENAND)	+= dev-onenand.o
obj-$(CONFIG_S5P_DEV_CSIS0)	+= dev-csis0.o
obj-$(CONFIG_S5P_DEV_CSIS1)	+= dev-csis1.o
obj-$(CONFIG_S5P_DEV_CSIS)	+= dev-csis-s5p.o
obj-$(CONFIG_S5P_DEV_JPEG)	+= dev-jpeg.o
obj-$(CONFIG_S5P_DEV_USB_EHCI)	+= dev-ehci.o
obj-$(CONFIG_S5P_DEV_USB_SWITCH)	+= dev-usb-switch.o
obj-$(CONFIG_S5P_DEV_USBGADGET)	+= dev-usbgadget.o
obj-$(CONFIG_S5P_DEV_MIPI_DSI)	+= dev-dsim.o
obj-$(CONFIG_S5P_DEV_DSIM02)	+= dev-dsim02.o
obj-$(CONFIG_S5P_DEV_DSIM12)	+= dev-dsim12.o
obj-$(CONFIG_S5P_DEV_MIPI_DSIM)	+= dev-mipidsim.o
obj-$(CONFIG_S5P_SETUP_MIPIPHY)	+= setup-mipiphy.o
obj-$(CONFIG_EXYNOS4_SETUP_THERMAL)	+= dev-tmu.o
obj-$(CONFIG_S5P_DEV_ACE)	+= dev-ace.o
ifeq ($(CONFIG_ARCH_EXYNOS4), y)
obj-$(CONFIG_IOMMU_EXYNOS4_API)		+= s5p_iommu.o
obj-$(CONFIG_IOVMM)		+= s5p_iovmm.o
endif
obj-$(CONFIG_S5P_DEV_I2C_HDMIPHY)	+= dev-i2c-hdmiphy.o
obj-$(CONFIG_S3C_DEV_TSI)	+= dev-tsi.o
