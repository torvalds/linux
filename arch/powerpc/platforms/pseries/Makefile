ifeq ($(CONFIG_PPC64),y)
EXTRA_CFLAGS		+= -mno-minimal-toc
endif

ifeq ($(CONFIG_PPC_PSERIES_DEBUG),y)
EXTRA_CFLAGS		+= -DDEBUG
endif

obj-y			:= lpar.o hvCall.o nvram.o reconfig.o \
			   setup.o iommu.o ras.o \
			   firmware.o power.o dlpar.o
obj-$(CONFIG_SMP)	+= smp.o
obj-$(CONFIG_XICS)	+= xics.o
obj-$(CONFIG_SCANLOG)	+= scanlog.o
obj-$(CONFIG_EEH)	+= eeh.o eeh_cache.o eeh_driver.o eeh_event.o eeh_sysfs.o
obj-$(CONFIG_KEXEC)	+= kexec.o
obj-$(CONFIG_PCI)	+= pci.o pci_dlpar.o
obj-$(CONFIG_PSERIES_MSI)	+= msi.o

obj-$(CONFIG_HOTPLUG_CPU)	+= hotplug-cpu.o
obj-$(CONFIG_MEMORY_HOTPLUG)	+= hotplug-memory.o

obj-$(CONFIG_HVC_CONSOLE)	+= hvconsole.o
obj-$(CONFIG_HVCS)		+= hvcserver.o
obj-$(CONFIG_HCALL_STATS)	+= hvCall_inst.o
obj-$(CONFIG_PHYP_DUMP)	+= phyp_dump.o
obj-$(CONFIG_CMM)		+= cmm.o
obj-$(CONFIG_DTL)		+= dtl.o
