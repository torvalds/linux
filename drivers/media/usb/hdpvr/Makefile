hdpvr-objs	:= hdpvr-control.o hdpvr-core.o hdpvr-video.o hdpvr-i2c.o

obj-$(CONFIG_VIDEO_HDPVR) += hdpvr.o

ccflags-y += -Idrivers/media/i2c

ccflags-y += $(extra-cflags-y) $(extra-cflags-m)
