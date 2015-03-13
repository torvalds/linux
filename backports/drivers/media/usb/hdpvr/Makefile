hdpvr-objs	:= hdpvr-control.o hdpvr-core.o hdpvr-video.o hdpvr-i2c.o

obj-$(CONFIG_BACKPORT_VIDEO_HDPVR) += hdpvr.o

ccflags-y += -I$(backport_srctree)/drivers/media/i2c

ccflags-y += $(extra-cflags-y) $(extra-cflags-m)
