
obj-$(CONFIG_EASYCAP)	+= easycap.o

easycap-y	:= easycap_main.o easycap_low.o easycap_sound.o
easycap-y	+= easycap_ioctl.o easycap_settings.o
easycap-y	+= easycap_testcard.o

ccflags-y := -Wall
# Impose all or none of the following:
ccflags-y += -DEASYCAP_IS_VIDEODEV_CLIENT
ccflags-y += -DEASYCAP_NEEDS_V4L2_DEVICE_H
ccflags-y += -DEASYCAP_NEEDS_V4L2_FOPS
ccflags-y += -DEASYCAP_NEEDS_UNLOCKED_IOCTL

