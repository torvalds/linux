#
# Makefile for USB Core files and filesystem
#

usbmon-objs	:= mon_main.o mon_stat.o mon_text.o mon_dma.o

# This does not use CONFIG_USB_MON because we want this to use a tristate.
obj-$(CONFIG_USB)	+= usbmon.o
