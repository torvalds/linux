EXTRA_CFLAGS	+= -mno-minimal-toc

obj-y += hvlog.o hvlpconfig.o lpardata.o setup.o mf.o lpevents.o \
	hvcall.o proc.o htab.o iommu.o misc.o
obj-$(CONFIG_PCI) += pci.o irq.o vpdinfo.o
obj-$(CONFIG_IBMVIO) += vio.o
obj-$(CONFIG_SMP) += smp.o
obj-$(CONFIG_VIOPATH) += viopath.o
obj-$(CONFIG_MODULES) += ksyms.o
