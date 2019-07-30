# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_MOST) += most_core.o
most_core-y := core.o
most_core-y += configfs.o
ccflags-y += -I $(srctree)/drivers/staging/

obj-$(CONFIG_MOST_CDEV)	+= cdev/
obj-$(CONFIG_MOST_NET)	+= net/
obj-$(CONFIG_MOST_SOUND)	+= sound/
obj-$(CONFIG_MOST_VIDEO)	+= video/
obj-$(CONFIG_MOST_DIM2)	+= dim2/
obj-$(CONFIG_MOST_I2C)	+= i2c/
obj-$(CONFIG_MOST_USB)	+= usb/
