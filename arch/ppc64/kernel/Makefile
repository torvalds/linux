#
# Makefile for the linux ppc64 kernel.
#

ifneq ($(CONFIG_PPC_MERGE),y)

EXTRA_CFLAGS	+= -mno-minimal-toc
extra-y		:= head.o vmlinux.lds

obj-y               :=	misc.o prom.o

endif

obj-y               +=	irq.o idle.o dma.o \
			align.o pacaData.o \
			udbg.o ioctl32.o \
			rtc.o \
			cpu_setup_power4.o \
			iommu.o sysfs.o vdso.o firmware.o
obj-y += vdso32/ vdso64/

pci-obj-$(CONFIG_PPC_MULTIPLATFORM)	+= pci_dn.o pci_direct_iommu.o

obj-$(CONFIG_PCI)	+= pci.o pci_iommu.o iomap.o $(pci-obj-y)

obj-$(CONFIG_PPC_MULTIPLATFORM) += nvram.o
ifneq ($(CONFIG_PPC_MERGE),y)
obj-$(CONFIG_PPC_MULTIPLATFORM) += prom_init.o
endif

obj-$(CONFIG_PPC_PSERIES) += udbg_16550.o

obj-$(CONFIG_KEXEC)		+= machine_kexec.o
obj-$(CONFIG_EEH)		+= eeh.o
obj-$(CONFIG_PROC_FS)		+= proc_ppc64.o
obj-$(CONFIG_MODULES)		+= module.o
ifneq ($(CONFIG_PPC_MERGE),y)
obj-$(CONFIG_MODULES)		+= ppc_ksyms.o
endif
obj-$(CONFIG_PPC_RTAS)		+= rtas_pci.o
obj-$(CONFIG_SCANLOG)		+= scanlog.o
obj-$(CONFIG_LPARCFG)		+= lparcfg.o
obj-$(CONFIG_HVC_CONSOLE)	+= hvconsole.o
ifneq ($(CONFIG_PPC_MERGE),y)
obj-$(CONFIG_BOOTX_TEXT)	+= btext.o
endif
obj-$(CONFIG_HVCS)		+= hvcserver.o

obj-$(CONFIG_PPC_PMAC)		+= udbg_scc.o

obj-$(CONFIG_PPC_MAPLE)		+= udbg_16550.o

obj-$(CONFIG_KPROBES)		+= kprobes.o

CFLAGS_ioctl32.o += -Ifs/

ifneq ($(CONFIG_PPC_MERGE),y)
ifeq ($(CONFIG_PPC_ISERIES),y)
arch/ppc64/kernel/head.o: arch/powerpc/kernel/lparmap.s
AFLAGS_head.o += -Iarch/powerpc/kernel
endif
endif
