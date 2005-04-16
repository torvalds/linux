#
# Makefile for the I-Force driver
#
# By Johann Deneux <deneux@ifrance.com>
#

# Goal definition
iforce-objs	:= iforce-ff.o iforce-main.o iforce-packets.o

obj-$(CONFIG_JOYSTICK_IFORCE)	+= iforce.o

ifeq ($(CONFIG_JOYSTICK_IFORCE_232),y)
	iforce-objs += iforce-serio.o
endif

ifeq ($(CONFIG_JOYSTICK_IFORCE_USB),y)
	iforce-objs += iforce-usb.o
endif

EXTRA_CFLAGS = -Werror-implicit-function-declaration
