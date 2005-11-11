#
# Makefile for the linux ppc64 kernel.
#

ifneq ($(CONFIG_PPC_MERGE),y)

EXTRA_CFLAGS	+= -mno-minimal-toc
extra-y		:= head.o vmlinux.lds

obj-y               :=	misc.o prom.o

endif

obj-y               +=	idle.o dma.o \
			align.o \
			rtc.o \
			iommu.o

pci-obj-$(CONFIG_PPC_MULTIPLATFORM)	+= pci_dn.o pci_direct_iommu.o

obj-$(CONFIG_PCI)	+= pci.o pci_iommu.o iomap.o $(pci-obj-y)

obj-$(CONFIG_PPC_MULTIPLATFORM) += nvram.o
ifneq ($(CONFIG_PPC_MERGE),y)
obj-$(CONFIG_PPC_MULTIPLATFORM) += prom_init.o
endif

obj-$(CONFIG_KEXEC)		+= machine_kexec.o
obj-$(CONFIG_MODULES)		+= module.o
ifneq ($(CONFIG_PPC_MERGE),y)
obj-$(CONFIG_MODULES)		+= ppc_ksyms.o
endif
obj-$(CONFIG_HVC_CONSOLE)	+= hvconsole.o
ifneq ($(CONFIG_PPC_MERGE),y)
obj-$(CONFIG_BOOTX_TEXT)	+= btext.o
endif
obj-$(CONFIG_HVCS)		+= hvcserver.o

obj-$(CONFIG_KPROBES)		+= kprobes.o

ifneq ($(CONFIG_PPC_MERGE),y)
ifeq ($(CONFIG_PPC_ISERIES),y)
arch/ppc64/kernel/head.o: arch/powerpc/kernel/lparmap.s
AFLAGS_head.o += -Iarch/powerpc/kernel
endif
endif
