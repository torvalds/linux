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
obj-y				+= clock.o

# CPU support

obj-$(CONFIG_CPU_S5PC100_INIT)	+= s5pc100-init.o
obj-$(CONFIG_CPU_S5PC100_CLOCK)	+= s5pc100-clock.o

# Device setup

obj-$(CONFIG_S5PC1XX_SETUP_SDHCI_GPIO)	+= setup-sdhci-gpio.o
