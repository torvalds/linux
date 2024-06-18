# SPDX-License-Identifier: GPL-2.0

# N.B. The below cannot be expressed with a single line using
# CONFIG_XEN_PCI_STUB as it always remains in "y" state,
# thus preventing the driver to be built as a module.
# Please note, that CONFIG_XEN_PCIDEV_BACKEND and
# CONFIG_XEN_PCIDEV_STUB are mutually exclusive.
obj-$(CONFIG_XEN_PCIDEV_BACKEND) += xen-pciback.o
obj-$(CONFIG_XEN_PCIDEV_STUB) += xen-pciback.o

xen-pciback-y := pci_stub.o pciback_ops.o xenbus.o
xen-pciback-y += conf_space.o conf_space_header.o \
		 conf_space_capability.o \
		 conf_space_quirks.o vpci.o \
		 passthrough.o
