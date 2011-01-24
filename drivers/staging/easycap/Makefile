easycap-objs := easycap_main.o
easycap-objs += easycap_low.o
easycap-objs += easycap_ioctl.o
easycap-objs += easycap_settings.o
easycap-objs += easycap_testcard.o
easycap-objs += easycap_sound.o
easycap-$(CONFIG_EASYCAP_OSS) += easycap_sound_oss.o

obj-$(CONFIG_EASYCAP)       += easycap.o

ccflags-y := -Wall
ccflags-y += -DEASYCAP_IS_VIDEODEV_CLIENT
ccflags-y += -DEASYCAP_NEEDS_V4L2_DEVICE_H
ccflags-y += -DEASYCAP_NEEDS_V4L2_FOPS
ccflags-y += -DEASYCAP_NEEDS_UNLOCKED_IOCTL

