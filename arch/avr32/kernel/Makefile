#
# Makefile for the Linux/AVR32 kernel.
#

extra-y				:= head.o vmlinux.lds

obj-$(CONFIG_SUBARCH_AVR32B)	+= entry-avr32b.o
obj-y				+= syscall_table.o syscall-stubs.o irq.o
obj-y				+= setup.o traps.o semaphore.o ptrace.o
obj-y				+= signal.o sys_avr32.o process.o time.o
obj-y				+= init_task.o switch_to.o cpu.o
obj-$(CONFIG_MODULES)		+= module.o avr32_ksyms.o
obj-$(CONFIG_KPROBES)		+= kprobes.o

USE_STANDARD_AS_RULE		:= true

%.lds: %.lds.c FORCE
	$(call if_changed_dep,cpp_lds_S)
