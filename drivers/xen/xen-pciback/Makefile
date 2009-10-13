obj-$(CONFIG_XEN_PCIDEV_BACKEND) += pciback.o

pciback-y := pci_stub.o pciback_ops.o xenbus.o
pciback-y += conf_space.o conf_space_header.o \
	     conf_space_capability.o \
	     conf_space_capability_vpd.o \
	     conf_space_capability_pm.o \
             conf_space_quirks.o
pciback-$(CONFIG_PCI_MSI) += conf_space_capability_msi.o
pciback-$(CONFIG_XEN_PCIDEV_BACKEND_VPCI) += vpci.o
pciback-$(CONFIG_XEN_PCIDEV_BACKEND_SLOT) += slot.o
pciback-$(CONFIG_XEN_PCIDEV_BACKEND_PASS) += passthrough.o
pciback-$(CONFIG_XEN_PCIDEV_BACKEND_CONTROLLER) += controller.o

ifeq ($(CONFIG_XEN_PCIDEV_BE_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif
