# SPDX-License-Identifier: GPL-2.0
ccflags-$(CONFIG_PPC64)			:= $(NO_MINIMAL_TOC)
ccflags-$(CONFIG_PPC_PSERIES_DEBUG)	+= -DDEBUG

obj-y			:= lpar.o hvCall.o nvram.o reconfig.o \
			   of_helpers.o rtas-work-area.o papr-sysparm.o \
			   setup.o iommu.o event_sources.o ras.o \
			   firmware.o power.o dlpar.o mobility.o rng.o \
			   pci.o pci_dlpar.o eeh_pseries.o msi.o \
			   papr_platform_attributes.o dtl.o
obj-$(CONFIG_SMP)	+= smp.o
obj-$(CONFIG_KEXEC_CORE)	+= kexec.o
obj-$(CONFIG_PSERIES_ENERGY)	+= pseries_energy.o

obj-$(CONFIG_HOTPLUG_CPU)	+= hotplug-cpu.o
obj-$(CONFIG_MEMORY_HOTPLUG)	+= hotplug-memory.o pmem.o

obj-$(CONFIG_HVC_CONSOLE)	+= hvconsole.o
obj-$(CONFIG_HVCS)		+= hvcserver.o
obj-$(CONFIG_HCALL_STATS)	+= hvCall_inst.o
obj-$(CONFIG_CMM)		+= cmm.o
obj-$(CONFIG_IO_EVENT_IRQ)	+= io_event_irq.o
obj-$(CONFIG_LPARCFG)		+= lparcfg.o
obj-$(CONFIG_IBMVIO)		+= vio.o
obj-$(CONFIG_IBMEBUS)		+= ibmebus.o
obj-$(CONFIG_PAPR_SCM)		+= papr_scm.o
obj-$(CONFIG_PPC_SPLPAR)	+= vphn.o
obj-$(CONFIG_PPC_SVM)		+= svm.o
obj-$(CONFIG_FA_DUMP)		+= rtas-fadump.o
obj-$(CONFIG_PSERIES_PLPKS)	+= plpks.o
obj-$(CONFIG_PPC_SECURE_BOOT)	+= plpks-secvar.o
obj-$(CONFIG_SUSPEND)		+= suspend.o
obj-$(CONFIG_PPC_VAS)		+= vas.o vas-sysfs.o

obj-$(CONFIG_ARCH_HAS_CC_PLATFORM)	+= cc_platform.o

# nothing that operates in real mode is safe for KASAN
KASAN_SANITIZE_ras.o := n
KASAN_SANITIZE_kexec.o := n
