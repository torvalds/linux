ifeq ($(CONFIG_PPC64),y)
EXTRA_CFLAGS		+= -mno-minimal-toc
endif

obj-y			:= pci.o lpar.o hvCall.o nvram.o reconfig.o \
			   setup.o iommu.o ras.o rtasd.o pci_dlpar.o \
			   firmware.o
obj-$(CONFIG_SMP)	+= smp.o
obj-$(CONFIG_XICS)	+= xics.o
obj-$(CONFIG_SCANLOG)	+= scanlog.o
obj-$(CONFIG_EEH)	+= eeh.o eeh_cache.o eeh_driver.o eeh_event.o

obj-$(CONFIG_HOTPLUG_CPU)	+= hotplug-cpu.o

obj-$(CONFIG_HVC_CONSOLE)	+= hvconsole.o
obj-$(CONFIG_HVCS)		+= hvcserver.o
obj-$(CONFIG_HCALL_STATS)	+= hvCall_inst.o
