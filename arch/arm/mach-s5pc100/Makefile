# arch/arm/mach-s5pc100/Makefile
#
# Copyright 2009 Samsung Electronics Co.
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:=
obj-				:=

# Core support for S5PC100 system

obj-$(CONFIG_CPU_S5PC100)	+= cpu.o gpiolib.o
obj-$(CONFIG_CPU_S5PC100)	+= setup-i2c0.o

# Helper and device support

obj-$(CONFIG_S5PC100_SETUP_FB_24BPP)	+= setup-fb-24bpp.o
obj-$(CONFIG_S5PC100_SETUP_I2C1)	+= setup-i2c1.o
obj-$(CONFIG_S5PC100_SETUP_SDHCI)       += setup-sdhci.o

# machine support
obj-$(CONFIG_MACH_SMDKC100)	+= mach-smdkc100.o
