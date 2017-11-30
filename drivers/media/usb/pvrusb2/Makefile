# SPDX-License-Identifier: GPL-2.0
obj-pvrusb2-sysfs-$(CONFIG_VIDEO_PVRUSB2_SYSFS) := pvrusb2-sysfs.o
obj-pvrusb2-debugifc-$(CONFIG_VIDEO_PVRUSB2_DEBUGIFC) := pvrusb2-debugifc.o
obj-pvrusb2-dvb-$(CONFIG_VIDEO_PVRUSB2_DVB) := pvrusb2-dvb.o

pvrusb2-objs	:= pvrusb2-i2c-core.o \
		   pvrusb2-audio.o \
		   pvrusb2-encoder.o pvrusb2-video-v4l.o \
		   pvrusb2-eeprom.o \
		   pvrusb2-main.o pvrusb2-hdw.o pvrusb2-v4l2.o \
		   pvrusb2-ctrl.o pvrusb2-std.o pvrusb2-devattr.o \
		   pvrusb2-context.o pvrusb2-io.o pvrusb2-ioread.o \
		   pvrusb2-cx2584x-v4l.o pvrusb2-wm8775.o \
		   pvrusb2-cs53l32a.o \
		   $(obj-pvrusb2-dvb-y) \
		   $(obj-pvrusb2-sysfs-y) $(obj-pvrusb2-debugifc-y)

obj-$(CONFIG_VIDEO_PVRUSB2) += pvrusb2.o

ccflags-y += -Idrivers/media/i2c
ccflags-y += -Idrivers/media/tuners
ccflags-y += -Idrivers/media/dvb-core
ccflags-y += -Idrivers/media/dvb-frontends
