extra-y := head.o vmlinux.lds init_task.o

obj-$(CONFIG_SMP) += smp.o topology.o

obj-y += setup.o irq_cpu.o traps.o syscalltab.o signal.o time.o
obj-y += process.o syscall.o trampoline.o reset.o ptrace.o
obj-y += vdso.o

obj-$(CONFIG_KGDB)    += kgdb.o
obj-$(CONFIG_MODULES) += module.o hexagon_ksyms.o

# Modules required to work with the Hexagon Virtual Machine
obj-y += vm_entry.o vm_events.o vm_switch.o vm_ops.o vm_init_segtable.o
obj-y += vm_vectors.o

obj-$(CONFIG_HAS_DMA) += dma.o

obj-$(CONFIG_STACKTRACE) += stacktrace.o
