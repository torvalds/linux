#
# Makefile for the linux kernel.
#

ifdef CONFIG_FUNCTION_TRACER
# Don't trace early setup code and tracing code
CFLAGS_REMOVE_early.o = -pg
CFLAGS_REMOVE_ftrace.o = -pg
endif

#
# Passing null pointers is ok for smp code, since we access the lowcore here.
#
CFLAGS_smp.o	:= -Wno-nonnull

#
# Disable tailcall optimizations for stack / callchain walking functions
# since this might generate broken code when accessing register 15 and
# passing its content to other functions.
#
CFLAGS_stacktrace.o	+= -fno-optimize-sibling-calls
CFLAGS_dumpstack.o	+= -fno-optimize-sibling-calls

#
# Pass UTS_MACHINE for user_regset definition
#
CFLAGS_ptrace.o		+= -DUTS_MACHINE='"$(UTS_MACHINE)"'

CFLAGS_sysinfo.o += -Iinclude/math-emu -Iarch/s390/math-emu -w

obj-y	:= traps.o time.o process.o base.o early.o setup.o vtime.o
obj-y	+= processor.o sys_s390.o ptrace.o signal.o cpcmd.o ebcdic.o nmi.o
obj-y	+= debug.o irq.o ipl.o dis.o diag.o sclp.o vdso.o
obj-y	+= sysinfo.o jump_label.o lgr.o os_info.o machine_kexec.o pgm_check.o
obj-y	+= dumpstack.o

obj-y	+= $(if $(CONFIG_64BIT),entry64.o,entry.o)
obj-y	+= $(if $(CONFIG_64BIT),reipl64.o,reipl.o)
obj-y	+= $(if $(CONFIG_64BIT),relocate_kernel64.o,relocate_kernel.o)

extra-y				+= head.o vmlinux.lds
extra-y				+= $(if $(CONFIG_64BIT),head64.o,head31.o)

obj-$(CONFIG_MODULES)		+= s390_ksyms.o module.o
obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_SCHED_BOOK)	+= topology.o
obj-$(CONFIG_HIBERNATION)	+= suspend.o swsusp_asm64.o
obj-$(CONFIG_AUDIT)		+= audit.o
compat-obj-$(CONFIG_AUDIT)	+= compat_audit.o
obj-$(CONFIG_COMPAT)		+= compat_linux.o compat_signal.o \
					compat_wrapper.o compat_exec_domain.o \
					$(compat-obj-y)

obj-$(CONFIG_STACKTRACE)	+= stacktrace.o
obj-$(CONFIG_KPROBES)		+= kprobes.o
obj-$(CONFIG_FUNCTION_TRACER)	+= $(if $(CONFIG_64BIT),mcount64.o,mcount.o)
obj-$(CONFIG_DYNAMIC_FTRACE)	+= ftrace.o
obj-$(CONFIG_FUNCTION_GRAPH_TRACER) += ftrace.o
obj-$(CONFIG_FTRACE_SYSCALLS)  += ftrace.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump.o

ifdef CONFIG_64BIT
obj-$(CONFIG_PERF_EVENTS)	+= perf_event.o perf_cpum_cf.o
obj-y				+= runtime_instr.o cache.o
endif

# vdso
obj-$(CONFIG_64BIT)		+= vdso64/
obj-$(CONFIG_32BIT)		+= vdso32/
obj-$(CONFIG_COMPAT)		+= vdso32/
