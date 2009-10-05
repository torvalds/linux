EXTRA_CFLAGS	+= -mno-minimal-toc

obj-y += exception.o
obj-y += hvlog.o hvlpconfig.o lpardata.o setup.o dt.o mf.o lpevents.o \
	hvcall.o proc.o htab.o iommu.o misc.o irq.o
obj-$(CONFIG_PCI) += pci.o
obj-$(CONFIG_SMP) += smp.o
obj-$(CONFIG_VIOPATH) += viopath.o vio.o
obj-$(CONFIG_MODULES) += ksyms.o
