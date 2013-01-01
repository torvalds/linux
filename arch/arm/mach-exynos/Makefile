# arch/arm/mach-exynos/Makefile
#
# Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
#		http://www.samsung.com/
#
# Licensed under GPLv2

ifneq ($(CONFIG_CMA),y)
obj-y				:= reserve_mem-exynos4.o
endif
obj-m				:=
obj-n				:=
obj-				:=

# Core support for EXYNOS system

FLAGS_cpu-exynos4.o += $(call as-instr,.arch_extension sec)
CFLAGS_pm-exynos4.o += $(call as-instr,.arch_extension sec)
CFLAGS_tmu-exynos.o += $(call as-instr,.arch_extension sec)
CFLAGS_platsmp.o += $(call as-instr,.arch_extension sec)
plus_sec := $(call as-instr,.arch_extension sec,+sec)
AFLAGS_sleep-exynos4.o	:=-Wa,-march=armv7-a$(plus_sec)
AFLAGS_idle-exynos4.o	:=-Wa,-march=armv7-a$(plus_sec)

obj-y				+= init.o irq-combiner.o dma.o irq-eint.o ppmu.o
obj-$(CONFIG_ARM_TRUSTZONE)	+= irq-sgi.o
obj-$(CONFIG_ARCH_EXYNOS4)	+= cpu-exynos4.o clock-exynos4.o pmu-exynos4.o ppc.o
obj-$(CONFIG_ARCH_EXYNOS5)	+= cpu-exynos5.o clock-exynos5.o pmu-exynos5.o

obj-y				+= clock-domain.o
obj-$(CONFIG_CPU_EXYNOS4210)	+= clock-exynos4210.o
obj-$(CONFIG_CPU_EXYNOS4212)	+= clock-exynos4212.o
obj-$(CONFIG_EXYNOS4_PM)	+= pm-exynos4.o sleep-exynos4.o
obj-$(CONFIG_EXYNOS5_PM)	+= pm-exynos5.o sleep-exynos5.o
obj-$(CONFIG_CPU_FREQ)		+= cpufreq.o
obj-$(CONFIG_EXYNOS4_CPUFREQ)	+= asv.o asv-4210.o asv-4x12.o
obj-$(CONFIG_EXYNOS4_CPUFREQ)	+= cpufreq-4210.o cpufreq-4x12.o
obj-$(CONFIG_EXYNOS5_CPUFREQ)	+= cpufreq-5250.o
obj-$(CONFIG_EXYNOS4_CPUIDLE)	+= cpuidle-exynos4.o idle-exynos4.o
obj-$(CONFIG_EXYNOS5_CPUIDLE)	+= cpuidle-exynos5.o idle-exynos5.o

obj-$(CONFIG_BUSFREQ)		+= busfreq.o
obj-$(CONFIG_BUSFREQ_OPP)	+= dev.o
ifeq ($(CONFIG_BUSFREQ_OPP),y)
obj-$(CONFIG_ARCH_EXYNOS4)	+= busfreq_opp_exynos4.o busfreq_opp_4x12.o
obj-$(CONFIG_ARCH_EXYNOS5)	+= busfreq_opp_exynos5.o busfreq_opp_5250.o
endif
obj-$(CONFIG_SMP)		+= platsmp.o headsmp.o

obj-$(CONFIG_EXYNOS_MCT)	+= mct.o

obj-$(CONFIG_HOTPLUG_CPU)	+= hotplug.o

obj-$(CONFIG_STAND_ALONE_POLICY)	+= stand-hotplug.o
obj-$(CONFIG_WITH_DVFS_POLICY)		+= dvfs-hotplug.o
obj-$(CONFIG_DVFS_NR_RUNNING_POLICY)	+= dynamic-dvfs-nr_running-hotplug.o
obj-$(CONFIG_NR_RUNNING_POLICY)		+= dynamic-nr_running-hotplug.o

# machine support

obj-$(CONFIG_MACH_SMDKC210)		+= mach-smdkv310.o
obj-$(CONFIG_MACH_SMDKV310)		+= mach-smdkv310.o
obj-$(CONFIG_MACH_ARMLEX4210)		+= mach-armlex4210.o
obj-$(CONFIG_MACH_UNIVERSAL_C210)	+= mach-universal_c210.o
obj-$(CONFIG_MACH_NURI)			+= mach-nuri.o
obj-$(CONFIG_MACH_SMDK4X12)		+= mach-smdk4x12.o
obj-$(CONFIG_MACH_SMDK5250)		+= mach-smdk5250.o

# For Odroid
ifeq ($(CONFIG_MACH_ODROID_4X12),y)
	obj-$(CONFIG_BOARD_ODROID_Q)	+= mach-odroid-q.o odroid-sysfs.o
	obj-$(CONFIG_BOARD_ODROID_Q2)	+= mach-odroid-q.o odroid-sysfs.o
	obj-$(CONFIG_BOARD_ODROID_X)	+= mach-odroid-x.o odroid-sysfs.o
	obj-$(CONFIG_BOARD_ODROID_X2)	+= mach-odroid-x.o odroid-sysfs.o
	obj-$(CONFIG_BOARD_ODROID_U)	+= mach-odroid-u.o odroid-sysfs.o
	obj-$(CONFIG_BOARD_ODROID_U2)	+= mach-odroid-u.o odroid-sysfs.o 
	obj-$(CONFIG_BOARD_SMD_A2)		+= mach-smd-a2.o odroid-sysfs.o
endif

# device support

obj-$(CONFIG_ODROID_FAN)		+= odroid_fan.o
obj-$(CONFIG_ARCH_EXYNOS)		+= dev-audio.o
obj-$(CONFIG_EXYNOS5_DEV_AHCI)		+= dev-ahci-exynos5.o
obj-$(CONFIG_EXYNOS4_DEV_AHCI)		+= dev-ahci.o
obj-$(CONFIG_EXYNOS_DEV_PD)		+= dev-pd.o
ifeq ($(CONFIG_EXYNOS_DEV_PD),y)
obj-$(CONFIG_ARCH_EXYNOS4)		+= dev-pd-exynos4.o
obj-$(CONFIG_ARCH_EXYNOS5)		+= dev-pd-exynos5.o
endif
obj-$(CONFIG_S5P_SYSTEM_MMU)		+= dev-sysmmu.o
obj-$(CONFIG_EXYNOS4_DEV_DWMCI)		+= dev-dwmci.o
obj-$(CONFIG_EXYNOS4_DEV_FIMC_IS)	+= dev-fimc-is.o
obj-$(CONFIG_EXYNOS4_DEV_FIMC_LITE)	+= dev-fimc-lite.o
obj-$(CONFIG_EXYNOS_DEV_C2C)		+= dev-c2c.o
obj-$(CONFIG_EXYNOS_DEV_TMU)		+= dev-tmu.o
obj-$(CONFIG_EXYNOS5_DEV_GSC)		+= dev-gsc.o

obj-$(CONFIG_EXYNOS4_SETUP_FIMC)	+= setup-fimc.o
obj-$(CONFIG_EXYNOS4_SETUP_FIMC0)	+= setup-fimc0.o
obj-$(CONFIG_EXYNOS4_SETUP_FIMC1)	+= setup-fimc1.o
obj-$(CONFIG_EXYNOS4_SETUP_FIMC2)	+= setup-fimc2.o
obj-$(CONFIG_EXYNOS4_SETUP_FIMC3)	+= setup-fimc3.o
obj-$(CONFIG_EXYNOS4_SETUP_CSIS)	+= setup-csis.o
obj-$(CONFIG_EXYNOS4_SETUP_FIMD0)	+= setup-fimd0.o
obj-$(CONFIG_EXYNOS4_SETUP_FIMD)	+= setup-fimd.o
obj-$(CONFIG_EXYNOS4_SETUP_DP)		+= setup-dp.o
obj-$(CONFIG_EXYNOS4_SETUP_FIMC_IS)	+= setup-fimc-is.o
obj-$(CONFIG_EXYNOS5_SETUP_GSC)		+= setup-gsc.o
obj-y					+= setup-i2c0.o
obj-$(CONFIG_EXYNOS4_SETUP_I2C1)	+= setup-i2c1.o
obj-$(CONFIG_EXYNOS4_SETUP_I2C2)	+= setup-i2c2.o
obj-$(CONFIG_EXYNOS4_SETUP_I2C3)	+= setup-i2c3.o
obj-$(CONFIG_EXYNOS4_SETUP_I2C4)	+= setup-i2c4.o
obj-$(CONFIG_EXYNOS4_SETUP_I2C5)	+= setup-i2c5.o
obj-$(CONFIG_EXYNOS4_SETUP_I2C6)	+= setup-i2c6.o
obj-$(CONFIG_EXYNOS4_SETUP_I2C7)	+= setup-i2c7.o
obj-$(CONFIG_EXYNOS4_SETUP_KEYPAD)	+= setup-keypad.o
obj-$(CONFIG_EXYNOS4_SETUP_MFC)		+= setup-mfc.o
obj-$(CONFIG_EXYNOS4_SETUP_SDHCI)	+= setup-sdhci.o
obj-$(CONFIG_EXYNOS4_SETUP_SDHCI_GPIO)	+= setup-sdhci-gpio.o
obj-$(CONFIG_EXYNOS4_SETUP_MSHCI)	+= setup-mshci.o
obj-$(CONFIG_EXYNOS4_SETUP_MSHCI_GPIO)	+= setup-mshci-gpio.o

obj-$(CONFIG_S3C64XX_DEV_SPI)		+= dev-spi.o

obj-$(CONFIG_EXYNOS4_SETUP_USB_PHY)	+= setup-usb-phy.o
#obj-$(CONFIG_EXYNOS4_SETUP_FB_S5P)	+= setup-fb-s5p.o
obj-$(CONFIG_EXYNOS4_SETUP_FB_S5P)	+= setup-fb-odroid.o
obj-$(CONFIG_EXYNOS4_SETUP_TVOUT)	+= setup-tvout.o
obj-$(CONFIG_EXYNOS4_SETUP_JPEG)	+= setup-jpeg.o
obj-$(CONFIG_EXYNOS4_SETUP_MIPI_DSI)	+= setup-dsim.o
obj-$(CONFIG_EXYNOS4_SETUP_MIPI_DSIM)	+= setup-mipidsim.o
obj-$(CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION) += secmem-allocdev.o

obj-$(CONFIG_WAKEUP_ASSIST)		+= wakeup_assist.o

obj-$(CONFIG_ION_EXYNOS)		+= dev-ion.o
obj-$(CONFIG_EXYNOS_C2C)		+= setup-c2c.o
obj-$(CONFIG_EXYNOS_THERMAL)		+= tmu-exynos.o

obj-$(CONFIG_USE_GPIO_AS_I2C)		+= pix_i2c.o
