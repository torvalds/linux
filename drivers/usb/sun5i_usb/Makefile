#
# Makefile for SUN5I USB2.0 Dual Role Controller Driver.
#

sw_usbc-objs := usbc/usbc.o \
                usbc/usbc_dev.o \
                usbc/usbc_host.o \
                usbc/usbc_phy.o \
                misc/sw_usb_debug.o \
                manager/usb_manager.o \
                manager/usbc0_platform.o \
                manager/usb_hcd_servers.o \
                hcd/core/sw_hcd_core.o \
                hcd/core/sw_hcd_host.o \
                hcd/core/sw_hcd_virt_hub.o \
                hcd/core/sw_hcd_debug.o \
                hcd/core/sw_hcd_dma.o

ifeq ($(CONFIG_USB_SW_SUN5I_USB0_OTG),y)
	sw_usbc-objs += manager/usb_hw_scan.o manager/usb_msg_center.o
endif

obj-$(CONFIG_USB_SW_SUN5I_USB_MANAGER) += sw_usbc.o

sw_usb_hcd0-objs := hcd/hcd0/sw_hcd0.o
obj-$(CONFIG_USB_SW_SUN5I_HCD0) += sw_usb_hcd0.o

sw_usb_udc-objs :=  udc/sw_udc.o \
                    udc/sw_udc_board.o \
                    udc/sw_udc_debug.o \
                    udc/sw_udc_dma.o
obj-$(CONFIG_USB_SW_SUN5I_UDC0_BUILTIN) += sw_usb_udc.o

