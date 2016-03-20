#
# Makefile for the linux kernel.
#

ifdef CONFIG_FUNCTION_TRACER
# Don't trace early setup code and tracing code
CFLAGS_REMOVE_early.o = $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_ftrace.o = $(CC_FLAGS_FTRACE)
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

CFLAGS_sysinfo.o += -w

#
# Use -march=z900 for sclp.c to be able to print an error message if
# the kernel is started on a machine which is too old
#
CFLAGS_REMOVE_sclp.o = $(CC_FLAGS_FTRACE)
ifneq ($(CC_FLAGS_MARCH),-march=z900)
CFLAGS_REMOVE_sclp.o	+= $(CC_FLAGS_MARCH)
CFLAGS_sclp.o		+= -march=z900
AFLAGS_REMOVE_head.o	+= $(CC_FLAGS_MARCH)
AFLAGS_head.o		+= -march=z900
endif
GCOV_PROFILE_sclp.o := n

obj-y	:= traps.o time.o process.o base.o early.o setup.o idle.o vtime.o
obj-y	+= processor.o sys_s390.o ptrace.o signal.o cpcmd.o ebcdic.o nmi.o
obj-y	+= debug.o irq.o ipl.o dis.o diag.o sclp.o vdso.o
obj-y	+= sysinfo.o jump_label.o lgr.o os_info.o machine_kexec.o pgm_check.o
obj-y	+= runtime_instr.o cache.o dumpstack.o
obj-y	+= entry.o reipl.o relocate_kernel.o

extra-y				+= head.o head64.o vmlinux.lds

obj-$(CONFIG_MODULES)		+= s390_ksyms.o module.o
obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_SCHED_TOPOLOGY)	+= topology.o
obj-$(CONFIG_HIBERNATION)	+= suspend.o swsusp.o
obj-$(CONFIG_AUDIT)		+= audit.o
compat-obj-$(CONFIG_AUDIT)	+= compat_audit.o
obj-$(CONFIG_COMPAT)		+= compat_linux.o compat_signal.o
obj-$(CONFIG_COMPAT)		+= compat_wrapper.o $(compat-obj-y)

obj-$(CONFIG_STACKTRACE)	+= stacktrace.o
obj-$(CONFIG_KPROBES)		+= kprobes.o
obj-$(CONFIG_FUNCTION_TRACER)	+= mcount.o ftrace.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump.o
obj-$(CONFIG_UPROBES)		+= uprobes.o

obj-$(CONFIG_PERF_EVENTS)	+= perf_event.o perf_cpum_cf.o perf_cpum_sf.o
obj-$(CONFIG_PERF_EVENTS)	+= perf_cpum_cf_events.o

obj-$(CONFIG_TRACEPOINTS)	+= trace.o

# vdso
obj-y				+= vdso64/
obj-$(CONFIG_COMPAT)		+= vdso32/
