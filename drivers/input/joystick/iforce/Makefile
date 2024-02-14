# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the I-Force driver
#
# By Johann Deneux <johann.deneux@gmail.com>
#

obj-$(CONFIG_JOYSTICK_IFORCE)		+= iforce.o
iforce-y := iforce-ff.o iforce-main.o iforce-packets.o
obj-$(CONFIG_JOYSTICK_IFORCE_232)	+= iforce-serio.o
obj-$(CONFIG_JOYSTICK_IFORCE_USB)	+= iforce-usb.o
