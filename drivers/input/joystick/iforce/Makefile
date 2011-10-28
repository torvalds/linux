#
# Makefile for the I-Force driver
#
# By Johann Deneux <johann.deneux@gmail.com>
#

obj-$(CONFIG_JOYSTICK_IFORCE)	+= iforce.o

iforce-y := iforce-ff.o iforce-main.o iforce-packets.o
iforce-$(CONFIG_JOYSTICK_IFORCE_232)	+= iforce-serio.o
iforce-$(CONFIG_JOYSTICK_IFORCE_USB)	+= iforce-usb.o
