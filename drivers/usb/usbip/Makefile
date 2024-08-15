# SPDX-License-Identifier: GPL-2.0
ccflags-$(CONFIG_USBIP_DEBUG) := -DDEBUG

obj-$(CONFIG_USBIP_CORE) += usbip-core.o
usbip-core-y := usbip_common.o usbip_event.o

obj-$(CONFIG_USBIP_VHCI_HCD) += vhci-hcd.o
vhci-hcd-y := vhci_sysfs.o vhci_tx.o vhci_rx.o vhci_hcd.o

obj-$(CONFIG_USBIP_HOST) += usbip-host.o
usbip-host-y := stub_dev.o stub_main.o stub_rx.o stub_tx.o

obj-$(CONFIG_USBIP_VUDC) += usbip-vudc.o
usbip-vudc-y := vudc_dev.o vudc_sysfs.o vudc_tx.o vudc_rx.o vudc_transfer.o vudc_main.o
