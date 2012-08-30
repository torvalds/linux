#
# Makefile for DWC_otg Highspeed USB controller driver
#

ifeq ($(CONFIG_DWC_OTG_DEBUG),y)
EXTRA_CFLAGS   += -DDEBUG
endif

# Use one of the following flags to compile the software in host-only or
# device-only mode.

ifeq ($(CONFIG_DWC_OTG_HOST_ONLY),y)
EXTRA_CFLAGS   += -DDWC_HOST_ONLY
endif

ifneq ($(CONFIG_USB),y)
EXTRA_CFLAGS   += -DDWC_DEVICE_ONLY
endif
#ifeq ($(CONFIG_DWC_OTG_DEVICE_ONLY),y)
#EXTRA_CFLAGS   += -DDWC_DEVICE_ONLY
#endif

ifeq ($(CONFIG_DWC_OTG_BOTH_HOST_SLAVE),y)
EXTRA_CFLAGS   += -DDWC_BOTH_HOST_SLAVE
endif
#EXTRA_CFLAGS += -Dlinux -DDWC_HS_ELECT_TST

dwc_otg-objs    := dwc_otg_driver.o dwc_otg_attr.o usbdev_rk30.o
dwc_otg-objs    += dwc_otg_cil.o dwc_otg_cil_intr.o
dwc_otg-objs    += dwc_otg_pcd.o dwc_otg_pcd_intr.o
dwc_otg-objs    += dwc_otg_hcd.o dwc_otg_hcd_intr.o dwc_otg_hcd_queue.o

obj-$(CONFIG_DWC_OTG) := dwc_otg.o
