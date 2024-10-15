# SPDX-License-Identifier: GPL-2.0
#
# USB peripheral controller drivers
#
subdir-ccflags-$(CONFIG_USB_GADGET_DEBUG)	:= -DDEBUG
subdir-ccflags-$(CONFIG_USB_GADGET_VERBOSE)	+= -DVERBOSE_DEBUG

obj-$(CONFIG_USB_LIBCOMPOSITE)	+= libcomposite.o
libcomposite-y			:= usbstring.o config.o epautoconf.o
libcomposite-y			+= composite.o functions.o configfs.o u_f.o

obj-$(CONFIG_USB_GADGET)	+= udc/ function/ legacy/
obj-$(CONFIG_USB_CI13XXX_MSM) 	+= ci13xxx.o
ci13xxx-y := ci13xxx_udc.o ci13xxx_msm.o
