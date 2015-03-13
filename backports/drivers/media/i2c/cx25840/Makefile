cx25840-objs    := cx25840-core.o cx25840-audio.o cx25840-firmware.o \
		   cx25840-vbi.o cx25840-ir.o

obj-$(CONFIG_BACKPORT_VIDEO_CX25840) += cx25840.o

ccflags-y += -I$(backport_srctree)/drivers/media/i2c
