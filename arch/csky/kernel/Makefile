extra-y := head.o vmlinux.lds

obj-y += entry.o atomic.o signal.o traps.o irq.o time.o vdso.o
obj-y += power.o syscall.o syscall_table.o setup.o
obj-y += process.o cpu-probe.o ptrace.o dumpstack.o

obj-$(CONFIG_MODULES)			+= module.o
obj-$(CONFIG_SMP)			+= smp.o
