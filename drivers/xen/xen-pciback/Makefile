obj-$(CONFIG_XEN_PCIDEV_BACKEND) += xen-pciback.o

xen-pciback-y := pci_stub.o pciback_ops.o xenbus.o
xen-pciback-y += conf_space.o conf_space_header.o \
		 conf_space_capability.o \
		 conf_space_quirks.o vpci.o \
		 passthrough.o
