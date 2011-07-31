
obj-$(CONFIG_EASYCAP)	+= easycap.o

easycap-objs	:= easycap_main.o easycap_low.o easycap_sound.o
easycap-objs	+= easycap_ioctl.o easycap_settings.o
easycap-objs	+= easycap_testcard.o

EXTRA_CFLAGS += -Wall
# Impose all or none of the following:
EXTRA_CFLAGS += -DEASYCAP_IS_VIDEODEV_CLIENT
EXTRA_CFLAGS += -DEASYCAP_NEEDS_V4L2_DEVICE_H
EXTRA_CFLAGS += -DEASYCAP_NEEDS_V4L2_FOPS

