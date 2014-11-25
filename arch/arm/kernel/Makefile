#
# Makefile for the linux kernel.
#

CPPFLAGS_vmlinux.lds := -DTEXT_OFFSET=$(TEXT_OFFSET)
AFLAGS_head.o        := -DTEXT_OFFSET=$(TEXT_OFFSET)

ifdef CONFIG_FUNCTION_TRACER
CFLAGS_REMOVE_ftrace.o = -pg
CFLAGS_REMOVE_insn.o = -pg
CFLAGS_REMOVE_patch.o = -pg
endif

CFLAGS_REMOVE_return_address.o = -pg

# Object file lists.

obj-y		:= elf.o entry-common.o irq.o opcodes.o \
		   process.o ptrace.o return_address.o \
		   setup.o signal.o sigreturn_codes.o \
		   stacktrace.o sys_arm.o time.o traps.o

obj-$(CONFIG_ATAGS)		+= atags_parse.o
obj-$(CONFIG_ATAGS_PROC)	+= atags_proc.o
obj-$(CONFIG_DEPRECATED_PARAM_STRUCT) += atags_compat.o

ifeq ($(CONFIG_CPU_V7M),y)
obj-y		+= entry-v7m.o v7m.o
else
obj-y		+= entry-armv.o
endif

obj-$(CONFIG_OC_ETM)		+= etm.o
obj-$(CONFIG_CPU_IDLE)		+= cpuidle.o
obj-$(CONFIG_ISA_DMA_API)	+= dma.o
obj-$(CONFIG_FIQ)		+= fiq.o fiqasm.o
obj-$(CONFIG_MODULES)		+= armksyms.o module.o
obj-$(CONFIG_ARTHUR)		+= arthur.o
obj-$(CONFIG_ISA_DMA)		+= dma-isa.o
obj-$(CONFIG_PCI)		+= bios32.o isa.o
obj-$(CONFIG_ARM_CPU_SUSPEND)	+= sleep.o suspend.o
obj-$(CONFIG_HIBERNATION)	+= hibernate.o
obj-$(CONFIG_SMP)		+= smp.o
ifdef CONFIG_MMU
obj-$(CONFIG_SMP)		+= smp_tlb.o
endif
obj-$(CONFIG_HAVE_ARM_SCU)	+= smp_scu.o
obj-$(CONFIG_HAVE_ARM_TWD)	+= smp_twd.o
obj-$(CONFIG_ARM_ARCH_TIMER)	+= arch_timer.o
obj-$(CONFIG_FUNCTION_TRACER)	+= entry-ftrace.o
obj-$(CONFIG_DYNAMIC_FTRACE)	+= ftrace.o insn.o
obj-$(CONFIG_FUNCTION_GRAPH_TRACER)	+= ftrace.o insn.o
obj-$(CONFIG_JUMP_LABEL)	+= jump_label.o insn.o patch.o
obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel.o
obj-$(CONFIG_UPROBES)		+= probes.o probes-arm.o uprobes.o uprobes-arm.o
obj-$(CONFIG_KPROBES)		+= probes.o kprobes.o kprobes-common.o patch.o
ifdef CONFIG_THUMB2_KERNEL
obj-$(CONFIG_KPROBES)		+= kprobes-thumb.o probes-thumb.o
else
obj-$(CONFIG_KPROBES)		+= kprobes-arm.o probes-arm.o
endif
obj-$(CONFIG_ARM_KPROBES_TEST)	+= test-kprobes.o
test-kprobes-objs		:= kprobes-test.o
ifdef CONFIG_THUMB2_KERNEL
test-kprobes-objs		+= kprobes-test-thumb.o
else
test-kprobes-objs		+= kprobes-test-arm.o
endif
obj-$(CONFIG_OABI_COMPAT)	+= sys_oabi-compat.o
obj-$(CONFIG_ARM_THUMBEE)	+= thumbee.o
obj-$(CONFIG_KGDB)		+= kgdb.o
obj-$(CONFIG_ARM_UNWIND)	+= unwind.o
obj-$(CONFIG_HAVE_TCM)		+= tcm.o
obj-$(CONFIG_OF)		+= devtree.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump.o
obj-$(CONFIG_SWP_EMULATE)	+= swp_emulate.o
CFLAGS_swp_emulate.o		:= -Wa,-march=armv7-a
obj-$(CONFIG_HAVE_HW_BREAKPOINT)	+= hw_breakpoint.o

obj-$(CONFIG_CPU_XSCALE)	+= xscale-cp0.o
obj-$(CONFIG_CPU_XSC3)		+= xscale-cp0.o
obj-$(CONFIG_CPU_MOHAWK)	+= xscale-cp0.o
obj-$(CONFIG_CPU_PJ4)		+= pj4-cp0.o
obj-$(CONFIG_CPU_PJ4B)		+= pj4-cp0.o
obj-$(CONFIG_IWMMXT)		+= iwmmxt.o
obj-$(CONFIG_PERF_EVENTS)	+= perf_regs.o
obj-$(CONFIG_HW_PERF_EVENTS)	+= perf_event.o perf_event_cpu.o
CFLAGS_pj4-cp0.o		:= -marm
AFLAGS_iwmmxt.o			:= -Wa,-mcpu=iwmmxt
obj-$(CONFIG_ARM_CPU_TOPOLOGY)  += topology.o

ifneq ($(CONFIG_ARCH_EBSA110),y)
  obj-y		+= io.o
endif

head-y			:= head$(MMUEXT).o
obj-$(CONFIG_DEBUG_LL)	+= debug.o
obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o

obj-$(CONFIG_ARM_VIRT_EXT)	+= hyp-stub.o
ifeq ($(CONFIG_ARM_PSCI),y)
obj-y				+= psci.o
obj-$(CONFIG_SMP)		+= psci_smp.o
endif

extra-y := $(head-y) vmlinux.lds
