# arch/arm/mach-s5pv310/Makefile
#
# Copyright (c) 2010 Samsung Electronics Co., Ltd.
#		http://www.samsung.com/
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:=
obj-				:=

# Core support for S5PV310 system

obj-$(CONFIG_CPU_S5PV310)	+= cpu.o init.o clock.o irq-combiner.o
obj-$(CONFIG_CPU_S5PV310)	+= setup-i2c0.o time.o gpiolib.o irq-eint.o dma.o
obj-$(CONFIG_CPU_FREQ)		+= cpufreq.o

obj-$(CONFIG_SMP)		+= platsmp.o headsmp.o
obj-$(CONFIG_LOCAL_TIMERS)	+= localtimer.o
obj-$(CONFIG_HOTPLUG_CPU)	+= hotplug.o

# machine support

obj-$(CONFIG_MACH_SMDKC210)	+= mach-smdkc210.o
obj-$(CONFIG_MACH_SMDKV310)	+= mach-smdkv310.o
obj-$(CONFIG_MACH_UNIVERSAL_C210)	+= mach-universal_c210.o

# device support

obj-y					+= dev-audio.o
obj-$(CONFIG_S5PV310_DEV_PD)		+= dev-pd.o
obj-$(CONFIG_S5PV310_DEV_SYSMMU)	+= dev-sysmmu.o

obj-$(CONFIG_S5PV310_SETUP_I2C1)	+= setup-i2c1.o
obj-$(CONFIG_S5PV310_SETUP_I2C2)	+= setup-i2c2.o
obj-$(CONFIG_S5PV310_SETUP_I2C3)	+= setup-i2c3.o
obj-$(CONFIG_S5PV310_SETUP_I2C4)	+= setup-i2c4.o
obj-$(CONFIG_S5PV310_SETUP_I2C5)	+= setup-i2c5.o
obj-$(CONFIG_S5PV310_SETUP_I2C6)	+= setup-i2c6.o
obj-$(CONFIG_S5PV310_SETUP_I2C7)	+= setup-i2c7.o
obj-$(CONFIG_S5PV310_SETUP_SDHCI)	+= setup-sdhci.o
obj-$(CONFIG_S5PV310_SETUP_SDHCI_GPIO)	+= setup-sdhci-gpio.o
