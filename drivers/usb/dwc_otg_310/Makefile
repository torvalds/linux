# SPDX-License-Identifier: GPL-2.0
#
# Makefile for DWC_otg Highspeed USB controller driver
#

BUS_INTERFACE = -DLM_INTERFACE
#EXTRA_CFLAGS    += -DDEBUG
EXTRA_CFLAGS	+= -Dlinux -DDWC_HS_ELECT_TST
#EXTRA_CFLAGS	+= -DDWC_EN_ISOC
#EXTRA_CFLAGS   	+= -I$(PWD)/dwc_common_port
#EXTRA_CFLAGS   	+= -I$(PORTLIB)
EXTRA_CFLAGS   	+= -DDWC_LINUX
EXTRA_CFLAGS   	+= $(CFI)
EXTRA_CFLAGS	+= $(BUS_INTERFACE)
#EXTRA_CFLAGS	+= -DDWC_DEV_SRPCAP


dwc_otg-objs	:= dwc_otg_driver.o dwc_otg_attr.o
dwc_otg-objs	+= dwc_otg_cil.o dwc_otg_cil_intr.o
dwc_otg-objs	+= dwc_otg_pcd_linux.o dwc_otg_pcd.o dwc_otg_pcd_intr.o
dwc_otg-objs	+= dwc_otg_hcd.o dwc_otg_hcd_linux.o dwc_otg_hcd_intr.o dwc_otg_hcd_queue.o dwc_otg_hcd_ddma.o
dwc_otg-objs	+= dwc_otg_adp.o
ifneq ($(CFI),)
dwc_otg-objs	+= dwc_otg_cfi.o
endif
dwc_otg-objs	+= common_port/dwc_common_linux.o

#objs relative to RK platform
dwc_otg-objs 	+= usbdev_rk30.o usbdev_rk32.o usbdev_rk3036.o usbdev_rk3126.o usbdev_rk3368.o usbdev_bc.o
obj-$(CONFIG_DWC_OTG_310) := dwc_otg.o
