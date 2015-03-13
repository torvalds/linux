usbvision-objs  := usbvision-core.o usbvision-video.o usbvision-i2c.o usbvision-cards.o

obj-$(CONFIG_BACKPORT_VIDEO_USBVISION) += usbvision.o

ccflags-y += -I$(backport_srctree)/drivers/media/i2c
ccflags-y += -I$(backport_srctree)/drivers/media/tuners
