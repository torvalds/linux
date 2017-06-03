ifdef CONFIG_FUNCTION_TRACER
# Do not profile debug and lowlevel utilities
CFLAGS_REMOVE_spinlock.o = -pg
CFLAGS_REMOVE_time.o = -pg
CFLAGS_REMOVE_irq.o = -pg
endif

# Make sure early boot has no stackprotector
nostackp := $(call cc-option, -fno-stack-protector)
CFLAGS_enlighten_pv.o		:= $(nostackp)
CFLAGS_mmu_pv.o		:= $(nostackp)

obj-y		:= enlighten.o multicalls.o mmu.o irq.o \
			time.o xen-asm.o xen-asm_$(BITS).o \
			grant-table.o suspend.o platform-pci-unplug.o

obj-$(CONFIG_XEN_PVHVM)		+= enlighten_hvm.o mmu_hvm.o suspend_hvm.o
obj-$(CONFIG_XEN_PV)			+= setup.o apic.o pmu.o suspend_pv.o \
						p2m.o enlighten_pv.o mmu_pv.o
obj-$(CONFIG_XEN_PVH)			+= enlighten_pvh.o

obj-$(CONFIG_EVENT_TRACING) += trace.o

obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_XEN_PV_SMP)  	+= smp_pv.o
obj-$(CONFIG_XEN_PVHVM_SMP)  	+= smp_hvm.o
obj-$(CONFIG_PARAVIRT_SPINLOCKS)+= spinlock.o
obj-$(CONFIG_XEN_DEBUG_FS)	+= debugfs.o
obj-$(CONFIG_XEN_DOM0)		+= vga.o
obj-$(CONFIG_SWIOTLB_XEN)	+= pci-swiotlb-xen.o
obj-$(CONFIG_XEN_EFI)		+= efi.o
obj-$(CONFIG_XEN_PVH)	 	+= xen-pvh.o
