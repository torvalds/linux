obj-$(CONFIG_USB_IP_COMMON) += usbip_common_mod.o
usbip_common_mod-y := usbip_common.o usbip_event.o

obj-$(CONFIG_USB_IP_VHCI_HCD) += vhci-hcd.o
vhci-hcd-y := vhci_sysfs.o vhci_tx.o vhci_rx.o vhci_hcd.o

obj-$(CONFIG_USB_IP_HOST) += usbip.o
usbip-y := stub_dev.o stub_main.o stub_rx.o stub_tx.o

ccflags-$(CONFIG_USB_IP_DEBUG_ENABLE) := -DDEBUG

