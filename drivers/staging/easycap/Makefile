
obj-$(CONFIG_EASYCAP)	+= easycap.o

easycap-objs	:= easycap_main.o easycap_low.o easycap_sound.o
easycap-objs	+= easycap_ioctl.o easycap_settings.o
easycap-objs	+= easycap_testcard.o

ccflags-y := -Wall
# Impose all or none of the following:
ccflags-y += -DEASYCAP_IS_VIDEODEV_CLIENT
ccflags-y += -DEASYCAP_NEEDS_V4L2_DEVICE_H
ccflags-y += -DEASYCAP_NEEDS_V4L2_FOPS

