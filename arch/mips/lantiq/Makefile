# Copyright (C) 2010 John Crispin <blogic@openwrt.org>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published
# by the Free Software Foundation.

obj-y := irq.o clk.o prom.o

obj-y += dts/

obj-$(CONFIG_EARLY_PRINTK) += early_printk.o

obj-$(CONFIG_SOC_TYPE_XWAY) += xway/
obj-$(CONFIG_SOC_FALCON) += falcon/
