# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
#

obj-y	:= head.o arcksyms.o setup.o irq.o reset.o ptrace.o process.o devtree.o
obj-y	+= signal.o traps.o sys.o troubleshoot.o stacktrace.o disasm.o
obj-y	+= ctx_sw_asm.o

obj-$(CONFIG_ISA_ARCOMPACT)		+= entry-compact.o intc-compact.o
obj-$(CONFIG_ISA_ARCV2)			+= entry-arcv2.o intc-arcv2.o

obj-$(CONFIG_MODULES)			+= arcksyms.o module.o
obj-$(CONFIG_SMP) 			+= smp.o
obj-$(CONFIG_ARC_MCIP)			+= mcip.o
obj-$(CONFIG_ARC_DW2_UNWIND)		+= unwind.o
obj-$(CONFIG_KPROBES)      		+= kprobes.o
obj-$(CONFIG_ARC_EMUL_UNALIGNED) 	+= unaligned.o
obj-$(CONFIG_KGDB)			+= kgdb.o
obj-$(CONFIG_ARC_METAWARE_HLINK)	+= arc_hostlink.o
obj-$(CONFIG_PERF_EVENTS)		+= perf_event.o
obj-$(CONFIG_JUMP_LABEL)		+= jump_label.o

obj-$(CONFIG_ARC_FPU_SAVE_RESTORE)	+= fpu.o
ifdef CONFIG_ISA_ARCOMPACT
CFLAGS_fpu.o   += -mdpfp
endif

extra-y := vmlinux.lds
