
vfio-pci-y := vfio_pci.o vfio_pci_intrs.o vfio_pci_rdwr.o vfio_pci_config.o
vfio-pci-$(CONFIG_VFIO_PCI_IGD) += vfio_pci_igd.o
vfio-pci-$(CONFIG_VFIO_PCI_NVLINK2) += vfio_pci_nvlink2.o

obj-$(CONFIG_VFIO_PCI) += vfio-pci.o
