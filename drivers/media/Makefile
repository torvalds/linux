#
# Makefile for the kernel multimedia device drivers.
#

obj-y := common/
obj-y += video/
obj-$(CONFIG_VIDEO_DEV) += radio/
obj-$(CONFIG_DVB_CORE)  += dvb/
ifeq ($(CONFIG_DVB_CORE),)
  obj-$(CONFIG_VIDEO_TUNER)  += dvb/frontends/
endif
