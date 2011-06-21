ifdef CONFIG_FUNCTION_TRACER
# Do not profile debug and lowlevel utilities
CFLAGS_REMOVE_spinlock.o = -pg
CFLAGS_REMOVE_time.o = -pg
CFLAGS_REMOVE_irq.o = -pg
endif

# Make sure early boot has no stackprotector
nostackp := $(call cc-option, -fno-stack-protector)
CFLAGS_enlighten.o		:= $(nostackp)
CFLAGS_mmu.o			:= $(nostackp)

obj-y		:= enlighten.o setup.o multicalls.o mmu.o irq.o \
			time.o xen-asm.o xen-asm_$(BITS).o \
			grant-table.o suspend.o platform-pci-unplug.o \
			p2m.o

obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_PARAVIRT_SPINLOCKS)+= spinlock.o
obj-$(CONFIG_XEN_DEBUG_FS)	+= debugfs.o
obj-$(CONFIG_XEN_DOM0)		+= vga.o
obj-$(CONFIG_SWIOTLB_XEN)	+= pci-swiotlb-xen.o
