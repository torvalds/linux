# SPDX-License-Identifier: GPL-2.0
gigaset-y := common.o interface.o proc.o ev-layer.o asyncdata.o

ifdef CONFIG_GIGASET_CAPI
gigaset-y += capi.o
else
gigaset-y += dummyll.o
endif

usb_gigaset-y := usb-gigaset.o
ser_gigaset-y := ser-gigaset.o
bas_gigaset-y := bas-gigaset.o isocdata.o

obj-$(CONFIG_ISDN_DRV_GIGASET) += gigaset.o
obj-$(CONFIG_GIGASET_M105) += usb_gigaset.o
obj-$(CONFIG_GIGASET_BASE) += bas_gigaset.o
obj-$(CONFIG_GIGASET_M101) += ser_gigaset.o
