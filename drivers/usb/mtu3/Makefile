# SPDX-License-Identifier: GPL-2.0

ccflags-$(CONFIG_USB_MTU3_DEBUG)	+= -DDEBUG

# define_trace.h needs to know how to find our header
CFLAGS_mtu3_trace.o	:= -I$(src)

obj-$(CONFIG_USB_MTU3)	+= mtu3.o

mtu3-y	:= mtu3_plat.o

ifneq ($(CONFIG_TRACING),)
	mtu3-y	+= mtu3_trace.o
endif

ifneq ($(filter y,$(CONFIG_USB_MTU3_HOST) $(CONFIG_USB_MTU3_DUAL_ROLE)),)
	mtu3-y	+= mtu3_host.o
endif

ifneq ($(filter y,$(CONFIG_USB_MTU3_GADGET) $(CONFIG_USB_MTU3_DUAL_ROLE)),)
	mtu3-y	+= mtu3_core.o mtu3_gadget_ep0.o mtu3_gadget.o mtu3_qmu.o
endif

ifneq ($(CONFIG_USB_MTU3_DUAL_ROLE),)
	mtu3-y	+= mtu3_dr.o
endif

ifneq ($(CONFIG_DEBUG_FS),)
	mtu3-y	+= mtu3_debugfs.o
endif
