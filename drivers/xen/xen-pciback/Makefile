obj-$(CONFIG_XEN_PCIDEV_BACKEND) += xen-pciback.o

xen-pciback-y := pci_stub.o pciback_ops.o xenbus.o
xen-pciback-y += conf_space.o conf_space_header.o \
		 conf_space_capability.o \
		 conf_space_capability_vpd.o \
		 conf_space_capability_pm.o \
		 conf_space_quirks.o
xen-pciback-$(CONFIG_PCI_MSI) += conf_space_capability_msi.o
xen-pciback-$(CONFIG_XEN_PCIDEV_BACKEND_VPCI) += vpci.o
xen-pciback-$(CONFIG_XEN_PCIDEV_BACKEND_SLOT) += slot.o
xen-pciback-$(CONFIG_XEN_PCIDEV_BACKEND_PASS) += passthrough.o
xen-pciback-$(CONFIG_XEN_PCIDEV_BACKEND_CONTROLLER) += controller.o

ifeq ($(CONFIG_XEN_PCIDEV_BE_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif
