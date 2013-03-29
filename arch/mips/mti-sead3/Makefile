#
# Carsten Langgaard, carstenl@mips.com
# Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
#
# Copyright (C) 2008 Wind River Systems, Inc.
#   written by Ralf Baechle <ralf@linux-mips.org>
#
# Copyright (C) 2012 MIPS Technoligies, Inc.  All rights reserved.
# Steven J. Hill <sjhill@mips.com>
#
obj-y				:= sead3-lcd.o sead3-cmdline.o \
				   sead3-display.o sead3-init.o sead3-int.o \
				   sead3-mtd.o sead3-net.o sead3-platform.o \
				   sead3-reset.o sead3-setup.o sead3-time.o

obj-y				+= sead3-i2c-dev.o sead3-i2c.o \
				   sead3-pic32-i2c-drv.o sead3-pic32-bus.o \
				   leds-sead3.o sead3-leds.o

obj-$(CONFIG_EARLY_PRINTK)	+= sead3-console.o
obj-$(CONFIG_USB_EHCI_HCD)	+= sead3-ehci.o
obj-$(CONFIG_OF)		+= sead3.dtb.o

$(obj)/%.dtb: $(obj)/%.dts
	$(call if_changed,dtc)
