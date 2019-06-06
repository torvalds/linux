# SPDX-License-Identifier: GPL-2.0-only
smiapp-objs			+= smiapp-core.o smiapp-regs.o \
				   smiapp-quirk.o smiapp-limits.o
obj-$(CONFIG_VIDEO_SMIAPP)	+= smiapp.o

ccflags-y += -I $(srctree)/drivers/media/i2c
