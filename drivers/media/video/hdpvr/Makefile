hdpvr-objs	:= hdpvr-control.o hdpvr-core.o hdpvr-video.o

hdpvr-$(CONFIG_I2C) += hdpvr-i2c.o

obj-$(CONFIG_VIDEO_HDPVR) += hdpvr.o

EXTRA_CFLAGS += -Idrivers/media/video

EXTRA_CFLAGS += $(extra-cflags-y) $(extra-cflags-m)
