#
# Makefile for the MN10300-specific core kernel code
#
extra-y := head.o init_task.o vmlinux.lds

fpu-obj-y := fpu-nofpu.o fpu-nofpu-low.o
fpu-obj-$(CONFIG_FPU) := fpu.o fpu-low.o

obj-y   := process.o signal.o entry.o traps.o irq.o \
	   ptrace.o setup.o time.o sys_mn10300.o io.o kthread.o \
	   switch_to.o mn10300_ksyms.o kernel_execve.o $(fpu-obj-y)

obj-$(CONFIG_SMP) += smp.o smp-low.o

obj-$(CONFIG_MN10300_WD_TIMER) += mn10300-watchdog.o mn10300-watchdog-low.o

obj-$(CONFIG_MN10300_TTYSM) += mn10300-serial.o mn10300-serial-low.o \
			       mn10300-debug.o
obj-$(CONFIG_GDBSTUB) += gdb-stub.o gdb-low.o
obj-$(CONFIG_GDBSTUB_ON_TTYSx) += gdb-io-serial.o gdb-io-serial-low.o
obj-$(CONFIG_GDBSTUB_ON_TTYSMx) += gdb-io-ttysm.o gdb-io-ttysm-low.o

ifeq ($(CONFIG_MN10300_CACHE_ENABLED),y)
obj-$(CONFIG_GDBSTUB) += gdb-cache.o
endif

obj-$(CONFIG_MN10300_RTC) += rtc.o
obj-$(CONFIG_PROFILE) += profile.o profile-low.o
obj-$(CONFIG_MODULES) += module.o
obj-$(CONFIG_KPROBES) += kprobes.o
obj-$(CONFIG_CSRC_MN10300) += csrc-mn10300.o
obj-$(CONFIG_CEVT_MN10300) += cevt-mn10300.o
