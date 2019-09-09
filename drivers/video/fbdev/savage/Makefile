# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the S3 Savage framebuffer driver
#

obj-$(CONFIG_FB_SAVAGE)			+= savagefb.o

savagefb-y				+= savagefb_driver.o
savagefb-$(CONFIG_FB_SAVAGE_I2C)	+= savagefb-i2c.o
savagefb-$(CONFIG_FB_SAVAGE_ACCEL)	+= savagefb_accel.o
