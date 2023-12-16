# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Realtek network device drivers.
#

obj-$(CONFIG_8139CP) += 8139cp.o
obj-$(CONFIG_8139TOO) += 8139too.o
obj-$(CONFIG_ATP) += atp.o
r8169-objs += r8169_main.o r8169_firmware.o r8169_phy_config.o
ifdef CONFIG_LEDS_TRIGGER_NETDEV
r8169-objs += r8169_leds.o
endif
obj-$(CONFIG_R8169) += r8169.o
