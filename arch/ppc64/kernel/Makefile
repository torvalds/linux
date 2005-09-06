#
# Makefile for the linux ppc64 kernel.
#

EXTRA_CFLAGS	+= -mno-minimal-toc
extra-y		:= head.o vmlinux.lds

obj-y               :=	setup.o entry.o traps.o irq.o idle.o dma.o \
			time.o process.o signal.o syscalls.o misc.o ptrace.o \
			align.o semaphore.o bitops.o pacaData.o \
			udbg.o binfmt_elf32.o sys_ppc32.o ioctl32.o \
			ptrace32.o signal32.o rtc.o init_task.o \
			lmb.o cputable.o cpu_setup_power4.o idle_power4.o \
			iommu.o sysfs.o vdso.o pmc.o firmware.o
obj-y += vdso32/ vdso64/

obj-$(CONFIG_PPC_OF) +=	of_device.o

pci-obj-$(CONFIG_PPC_ISERIES)	+= iSeries_pci.o iSeries_irq.o \
				iSeries_VpdInfo.o
pci-obj-$(CONFIG_PPC_MULTIPLATFORM)	+= pci_dn.o pci_direct_iommu.o

obj-$(CONFIG_PCI)	+= pci.o pci_iommu.o iomap.o $(pci-obj-y)

obj-$(CONFIG_PPC_ISERIES) += HvCall.o HvLpConfig.o LparData.o \
			     iSeries_setup.o ItLpQueue.o hvCall.o \
			     mf.o HvLpEvent.o iSeries_proc.o iSeries_htab.o \
			     iSeries_iommu.o

obj-$(CONFIG_PPC_MULTIPLATFORM) += nvram.o i8259.o prom_init.o prom.o

obj-$(CONFIG_PPC_PSERIES) += pSeries_pci.o pSeries_lpar.o pSeries_hvCall.o \
			     pSeries_nvram.o rtasd.o ras.o pSeries_reconfig.o \
			     pSeries_setup.o pSeries_iommu.o udbg_16550.o

obj-$(CONFIG_PPC_BPA) += bpa_setup.o bpa_iommu.o bpa_nvram.o \
			 bpa_iic.o spider-pic.o

obj-$(CONFIG_KEXEC)		+= machine_kexec.o
obj-$(CONFIG_EEH)		+= eeh.o
obj-$(CONFIG_PROC_FS)		+= proc_ppc64.o
obj-$(CONFIG_RTAS_FLASH)	+= rtas_flash.o
obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_MODULES)		+= module.o ppc_ksyms.o
obj-$(CONFIG_PPC_RTAS)		+= rtas.o rtas_pci.o
obj-$(CONFIG_RTAS_PROC)		+= rtas-proc.o
obj-$(CONFIG_SCANLOG)		+= scanlog.o
obj-$(CONFIG_VIOPATH)		+= viopath.o
obj-$(CONFIG_LPARCFG)		+= lparcfg.o
obj-$(CONFIG_HVC_CONSOLE)	+= hvconsole.o
obj-$(CONFIG_BOOTX_TEXT)	+= btext.o
obj-$(CONFIG_HVCS)		+= hvcserver.o

vio-obj-$(CONFIG_PPC_PSERIES)	+= pSeries_vio.o
vio-obj-$(CONFIG_PPC_ISERIES)	+= iSeries_vio.o
obj-$(CONFIG_IBMVIO)		+= vio.o $(vio-obj-y)
obj-$(CONFIG_XICS)		+= xics.o
obj-$(CONFIG_MPIC)		+= mpic.o

obj-$(CONFIG_PPC_PMAC)		+= pmac_setup.o pmac_feature.o pmac_pci.o \
				   pmac_time.o pmac_nvram.o pmac_low_i2c.o \
				   udbg_scc.o

obj-$(CONFIG_PPC_MAPLE)		+= maple_setup.o maple_pci.o maple_time.o \
				   udbg_16550.o

obj-$(CONFIG_U3_DART)		+= u3_iommu.o

ifdef CONFIG_SMP
obj-$(CONFIG_PPC_PMAC)		+= pmac_smp.o smp-tbsync.o
obj-$(CONFIG_PPC_ISERIES)	+= iSeries_smp.o
obj-$(CONFIG_PPC_PSERIES)	+= pSeries_smp.o
obj-$(CONFIG_PPC_BPA)		+= pSeries_smp.o
obj-$(CONFIG_PPC_MAPLE)		+= smp-tbsync.o
endif

obj-$(CONFIG_ALTIVEC)		+= vecemu.o vector.o
obj-$(CONFIG_KPROBES)		+= kprobes.o

CFLAGS_ioctl32.o += -Ifs/

ifeq ($(CONFIG_PPC_ISERIES),y)
arch/ppc64/kernel/head.o: arch/ppc64/kernel/lparmap.s
AFLAGS_head.o += -Iarch/ppc64/kernel
endif
