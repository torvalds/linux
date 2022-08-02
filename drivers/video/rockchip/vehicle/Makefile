# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_VIDEO_REVERSE_IMAGE) += video_rkvehicle.o
video_rkvehicle-objs += vehicle_flinger.o \
			vehicle_dev.o \
			vehicle_main.o \
			vehicle_cif.o \
			vehicle_generic_sensor.o \
			vehicle_gpio.o \

video_rkvehicle-$(CONFIG_VIDEO_REVERSE_NVP6324) += \
			vehicle_ad_nvp6324.o

video_rkvehicle-$(CONFIG_VIDEO_REVERSE_NVP6188) += \
			vehicle_ad_nvp6188.o

video_rkvehicle-$(CONFIG_VIDEO_REVERSE_MAX96714) += \
			vehicle_ad_max96714.o

video_rkvehicle-$(CONFIG_VIDEO_REVERSE_GC2145) += \
			vehicle_ad_gc2145.o

video_rkvehicle-$(CONFIG_VIDEO_REVERSE_TP2825) += \
			vehicle_ad_tp2825.o

video_rkvehicle-$(CONFIG_VIDEO_REVERSE_AD7181) += \
			vehicle_ad_7181.o
