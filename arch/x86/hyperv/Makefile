obj-y			:= hv_init.o mmu.o nested.o
obj-$(CONFIG_X86_64)	+= hv_apic.o

ifdef CONFIG_X86_64
obj-$(CONFIG_PARAVIRT_SPINLOCKS)	+= hv_spinlock.o
endif
