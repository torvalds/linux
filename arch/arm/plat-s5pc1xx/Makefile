# arch/arm/plat-s5pc1xx/Makefile
#
# Copyright 2009 Samsung Electronics Co.
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:= dummy.o
obj-				:=

# Core files

obj-y				+= dev-uart.o
obj-y				+= cpu.o
obj-y				+= irq.o

# CPU support

obj-$(CONFIG_CPU_S5PC100_INIT)	+= s5pc100-init.o
obj-$(CONFIG_CPU_S5PC100_CLOCK)	+= s5pc100-clock.o

# Device setup

obj-$(CONFIG_S5PC100_SETUP_I2C0) += setup-i2c0.o
obj-$(CONFIG_S5PC100_SETUP_I2C1) += setup-i2c1.o
