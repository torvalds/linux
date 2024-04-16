# SPDX-License-Identifier: GPL-2.0
bcm2835-v4l2-$(CONFIG_VIDEO_BCM2835) := \
	bcm2835-camera.o \
	controls.o

obj-$(CONFIG_VIDEO_BCM2835) += bcm2835-v4l2.o

ccflags-y += \
	-I $(srctree)/$(src)/.. \
	-I $(srctree)/$(src)/../vchiq-mmal/ \
	-D__VCCOREVER__=0x04000000
