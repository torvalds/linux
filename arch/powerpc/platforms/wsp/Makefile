ccflags-y			+= -mno-minimal-toc

obj-y				+= setup.o ics.o
obj-$(CONFIG_PPC_PSR2)		+= psr2.o opb_pic.o
obj-$(CONFIG_PPC_WSP)		+= scom_wsp.o
obj-$(CONFIG_SMP)		+= smp.o scom_smp.o
obj-$(CONFIG_PCI)		+= wsp_pci.o
obj-$(CONFIG_PCI_MSI)		+= msi.o