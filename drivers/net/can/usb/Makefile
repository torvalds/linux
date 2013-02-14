#
#  Makefile for the Linux Controller Area Network USB drivers.
#

obj-$(CONFIG_CAN_EMS_USB) += ems_usb.o
obj-$(CONFIG_CAN_ESD_USB2) += esd_usb2.o
obj-$(CONFIG_CAN_KVASER_USB) += kvaser_usb.o
obj-$(CONFIG_CAN_PEAK_USB) += peak_usb/

ccflags-$(CONFIG_CAN_DEBUG_DEVICES) := -DDEBUG
