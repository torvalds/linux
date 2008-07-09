obj-$(CONFIG_USB_IP_COMMON) += usbip_common_mod.o
usbip_common_mod-objs := usbip_common.o usbip_event.o

ifeq ($(CONFIG_USB_DEBUG),y)
	EXTRA_CFLAGS += -DDEBUG
endif
