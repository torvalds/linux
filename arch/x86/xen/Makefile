ifdef CONFIG_FUNCTION_TRACER
# Do not profile debug and lowlevel utilities
CFLAGS_REMOVE_spinlock.o = -pg
CFLAGS_REMOVE_time.o = -pg
CFLAGS_REMOVE_irq.o = -pg
endif

obj-y		:= enlighten.o setup.o multicalls.o mmu.o irq.o \
			time.o xen-asm.o xen-asm_$(BITS).o \
			grant-table.o suspend.o

obj-$(CONFIG_SMP)		+= smp.o spinlock.o
obj-$(CONFIG_XEN_DEBUG_FS)	+= debugfs.o