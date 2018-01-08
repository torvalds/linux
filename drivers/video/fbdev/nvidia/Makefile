# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the nVidia framebuffer driver
#

obj-$(CONFIG_FB_NVIDIA)          += nvidiafb.o

nvidiafb-y                       := nvidia.o nv_hw.o nv_setup.o \
			            nv_accel.o nv_of.o
nvidiafb-$(CONFIG_FB_NVIDIA_I2C) += nv_i2c.o
nvidiafb-$(CONFIG_FB_NVIDIA_BACKLIGHT)  += nv_backlight.o

nvidiafb-objs                    := $(nvidiafb-y)
