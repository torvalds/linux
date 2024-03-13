# SPDX-License-Identifier: GPL-2.0-only
ccs-objs			+= ccs-core.o ccs-reg-access.o \
				   ccs-quirk.o ccs-limits.o ccs-data.o
obj-$(CONFIG_VIDEO_CCS)		+= ccs.o

ccflags-y += -I $(srctree)/drivers/media/i2c
