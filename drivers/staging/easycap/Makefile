easycap-objs      := easycap_main.o easycap_low.o easycap_sound.o \
		     easycap_ioctl.o easycap_settings.o easycap_testcard.o

obj-$(CONFIG_EASYCAP)       += easycap.o

ccflags-y := -Wall
ccflags-y += -DEASYCAP_IS_VIDEODEV_CLIENT
ccflags-y += -DEASYCAP_NEEDS_V4L2_DEVICE_H
ccflags-y += -DEASYCAP_NEEDS_V4L2_FOPS
ccflags-y += -DEASYCAP_NEEDS_UNLOCKED_IOCTL
ccflags-y += -DEASYCAP_NEEDS_ALSA

