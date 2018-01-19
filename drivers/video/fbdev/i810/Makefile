# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Intel 810/815 framebuffer driver
#

obj-$(CONFIG_FB_I810)		+= i810fb.o

i810fb-objs                     := i810_main.o i810_accel.o

ifdef CONFIG_FB_I810_GTF
i810fb-objs                     += i810_gtf.o
else
i810fb-objs                     += i810_dvt.o
endif

ifdef CONFIG_FB_I810_I2C
i810fb-objs += i810-i2c.o
endif
