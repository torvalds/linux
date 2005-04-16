#
# Makefile for the Linux/MIPS kernel.
#

extra-y		:= head.o init_task.o vmlinux.lds

obj-y		+= cpu-probe.o branch.o entry.o genex.o irq.o process.o \
		   ptrace.o reset.o semaphore.o setup.o signal.o syscall.o \
		   time.o traps.o unaligned.o

binfmt_irix-objs	:= irixelf.o irixinv.o irixioctl.o irixsig.o	\
			   irix5sys.o sysirix.o

ifdef CONFIG_MODULES
obj-y				+= mips_ksyms.o module.o
obj-$(CONFIG_MIPS32)		+= module-elf32.o
obj-$(CONFIG_MIPS64)		+= module-elf64.o
endif

obj-$(CONFIG_CPU_R3000)		+= r2300_fpu.o r2300_switch.o
obj-$(CONFIG_CPU_TX39XX)	+= r2300_fpu.o r2300_switch.o
obj-$(CONFIG_CPU_TX49XX)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R4000)		+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_VR41XX)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R4300)		+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R4X00)		+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R5000)		+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R5432)		+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R8000)		+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_RM7000)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_RM9000)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_NEVADA)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R10000)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_SB1)		+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_MIPS32)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_MIPS64)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R6000)		+= r6000_fpu.o r4k_switch.o

obj-$(CONFIG_SMP)		+= smp.o

obj-$(CONFIG_NO_ISA)		+= dma-no-isa.o
obj-$(CONFIG_I8259)		+= i8259.o
obj-$(CONFIG_IRQ_CPU)		+= irq_cpu.o
obj-$(CONFIG_IRQ_CPU_RM7K)	+= irq-rm7000.o
obj-$(CONFIG_IRQ_CPU_RM9K)	+= irq-rm9000.o
obj-$(CONFIG_IRQ_MV64340)	+= irq-mv6434x.o

obj-$(CONFIG_MIPS32)		+= scall32-o32.o
obj-$(CONFIG_MIPS64)		+= scall64-64.o
obj-$(CONFIG_BINFMT_IRIX)	+= binfmt_irix.o
obj-$(CONFIG_MIPS32_COMPAT)	+= ioctl32.o linux32.o signal32.o
obj-$(CONFIG_MIPS32_N32)	+= binfmt_elfn32.o scall64-n32.o signal_n32.o
obj-$(CONFIG_MIPS32_O32)	+= binfmt_elfo32.o scall64-o32.o ptrace32.o

obj-$(CONFIG_KGDB)		+= gdb-low.o gdb-stub.o
obj-$(CONFIG_PROC_FS)		+= proc.o

obj-$(CONFIG_MIPS64)		+= cpu-bugs64.o

obj-$(CONFIG_GEN_RTC)		+= genrtc.o

CFLAGS_cpu-bugs64.o	= $(shell if $(CC) $(CFLAGS) -Wa,-mdaddi -c -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-DHAVE_AS_SET_DADDI"; fi)
CFLAGS_ioctl32.o	+= -Ifs/

EXTRA_AFLAGS := $(CFLAGS)
