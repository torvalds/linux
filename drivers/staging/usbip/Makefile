obj-$(CONFIG_USB_IP_COMMON) += usbip_common_mod.o
usbip_common_mod-objs := usbip_common.o usbip_event.o

obj-$(CONFIG_USB_IP_VHCI_HCD) += vhci-hcd.o
vhci-hcd-objs := vhci_sysfs.o vhci_tx.o vhci_rx.o vhci_hcd.o

obj-$(CONFIG_USB_IP_HOST) += usbip.o
usbip-objs := stub_dev.o stub_main.o stub_rx.o stub_tx.o

ifeq ($(CONFIG_USB_IP_DEBUG_ENABLE),y)
	EXTRA_CFLAGS += -DDEBUG
endif
