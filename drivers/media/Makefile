#
# Makefile for the kernel multimedia device drivers.
#

obj-y := common/

obj-$(CONFIG_VIDEO_MEDIA) += common/

# Since hybrid devices are here, should be compiled if DVB and/or V4L
obj-$(CONFIG_VIDEO_MEDIA) += video/

obj-$(CONFIG_VIDEO_DEV) += radio/
obj-$(CONFIG_DVB_CORE)  += dvb/
