#
# Makefile for the Linux/SuperH kernel.
#

extra-y	:= head.o init_task.o vmlinux.lds

obj-y	:= debugtraps.o io.o io_generic.o irq.o machvec.o process.o ptrace.o \
	   semaphore.o setup.o signal.o sys_sh.o syscalls.o \
	   time.o topology.o traps.o

obj-y				+= cpu/ timers/
obj-$(CONFIG_VSYSCALL)		+= vsyscall/

obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_CF_ENABLER)	+= cf-enabler.o
obj-$(CONFIG_SH_STANDARD_BIOS)	+= sh_bios.o
obj-$(CONFIG_SH_KGDB)		+= kgdb_stub.o kgdb_jmp.o
obj-$(CONFIG_SH_CPU_FREQ)	+= cpufreq.o
obj-$(CONFIG_MODULES)		+= sh_ksyms.o module.o
obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o
obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump.o
obj-$(CONFIG_PM)		+= pm.o
obj-$(CONFIG_STACKTRACE)	+= stacktrace.o
