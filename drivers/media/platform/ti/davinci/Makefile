# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the davinci video device drivers.
#

#VPIF Display driver
obj-$(CONFIG_VIDEO_DAVINCI_VPIF_DISPLAY) += vpif.o vpif_display.o
#VPIF Capture driver
obj-$(CONFIG_VIDEO_DAVINCI_VPIF_CAPTURE) += vpif.o vpif_capture.o

obj-$(CONFIG_VIDEO_DAVINCI_VPBE_DISPLAY) += vpss.o vpbe.o vpbe_osd.o \
	vpbe_venc.o vpbe_display.o
