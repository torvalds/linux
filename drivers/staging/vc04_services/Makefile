# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_BCM2835_VCHIQ)	+= vchiq.o

vchiq-objs := \
   interface/vchiq_arm/vchiq_core.o  \
   interface/vchiq_arm/vchiq_arm.o \
   interface/vchiq_arm/vchiq_debugfs.o \
   interface/vchiq_arm/vchiq_connected.o \

ifdef CONFIG_VCHIQ_CDEV
vchiq-objs += interface/vchiq_arm/vchiq_dev.o
endif

obj-$(CONFIG_SND_BCM2835)		+= bcm2835-audio/
obj-$(CONFIG_VIDEO_BCM2835)		+= bcm2835-camera/
obj-$(CONFIG_BCM2835_VCHIQ_MMAL)	+= vchiq-mmal/

ccflags-y += -I $(srctree)/$(src)/include

