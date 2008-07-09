obj-$(CONFIG_USB_IP_COMMON) += usbip_common_mod.o
usbip_common_mod-objs := usbip_common.o usbip_event.o

obj-$(CONFIG_USB_IP_VHCI_HCD) += vhci-hcd.o
vhci-hcd-objs := vhci_sysfs.o vhci_tx.o vhci_rx.o vhci_hcd.o

ifeq ($(CONFIG_USB_DEBUG),y)
	EXTRA_CFLAGS += -DDEBUG
endif
