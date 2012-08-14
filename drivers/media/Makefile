#
# Makefile for the kernel multimedia device drivers.
#

media-objs	:= media-device.o media-devnode.o media-entity.o

ifeq ($(CONFIG_MEDIA_CONTROLLER),y)
  obj-$(CONFIG_MEDIA_SUPPORT) += media.o
endif

obj-y += tuners/ common/ rc/ video/
obj-y += i2c/ pci/ usb/ mmc/ firewire/ parport/

obj-$(CONFIG_VIDEO_DEV) += radio/ v4l2-core/
obj-$(CONFIG_DVB_CORE)  += dvb-core/ dvb-frontends/
