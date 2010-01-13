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

obj-y				+= dev-uart.o
obj-y				+= cpu.o
obj-y				+= clock.o
obj-y				+= irq.o
obj-y				+= setup-i2c0.o

# CPU support

obj-$(CONFIG_CPU_S5P6440_INIT)	+= s5p6440-init.o
obj-$(CONFIG_CPU_S5P6440_CLOCK)	+= s5p6440-clock.o
