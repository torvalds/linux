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
obj-$(CONFIG_CPU_S5PV310)	+= setup-i2c0.o time.o

obj-$(CONFIG_SMP)		+= platsmp.o headsmp.o
obj-$(CONFIG_LOCAL_TIMERS)	+= localtimer.o
obj-$(CONFIG_HOTPLUG_CPU)	+= hotplug.o

# machine support

obj-$(CONFIG_MACH_SMDKV310)	+= mach-smdkv310.o
obj-$(CONFIG_MACH_UNIVERSAL_C210)	+= mach-universal_c210.o

# device support

obj-$(CONFIG_S5PV310_SETUP_I2C1)	+= setup-i2c1.o
obj-$(CONFIG_S5PV310_SETUP_I2C2)	+= setup-i2c2.o
