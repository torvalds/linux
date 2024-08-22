# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

ifdef CONFIG_FUNCTION_TRACER

# Do not trace tracer code
CFLAGS_REMOVE_ftrace.o		= $(CC_FLAGS_FTRACE)

# Do not trace early setup code
CFLAGS_REMOVE_early.o		= $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_rethook.o		= $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_stacktrace.o	= $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_unwind_bc.o	= $(CC_FLAGS_FTRACE)

endif

GCOV_PROFILE_early.o		:= n
KCOV_INSTRUMENT_early.o		:= n
UBSAN_SANITIZE_early.o		:= n
KASAN_SANITIZE_ipl.o		:= n
KASAN_SANITIZE_machine_kexec.o	:= n

#
# Passing null pointers is ok for smp code, since we access the lowcore here.
#
CFLAGS_smp.o		:= -Wno-nonnull

#
# Disable tailcall optimizations for stack / callchain walking functions
# since this might generate broken code when accessing register 15 and
# passing its content to other functions.
#
CFLAGS_stacktrace.o	+= -fno-optimize-sibling-calls
CFLAGS_dumpstack.o	+= -fno-optimize-sibling-calls
CFLAGS_unwind_bc.o	+= -fno-optimize-sibling-calls

obj-y	:= head64.o traps.o time.o process.o earlypgm.o early.o setup.o idle.o vtime.o
obj-y	+= processor.o syscall.o ptrace.o signal.o cpcmd.o ebcdic.o nmi.o
obj-y	+= debug.o irq.o ipl.o dis.o diag.o vdso.o cpufeature.o
obj-y	+= sysinfo.o lgr.o os_info.o ctlreg.o
obj-y	+= runtime_instr.o cache.o fpu.o dumpstack.o guarded_storage.o sthyi.o
obj-y	+= entry.o reipl.o kdebugfs.o alternative.o
obj-y	+= nospec-branch.o ipl_vmparm.o machine_kexec_reloc.o unwind_bc.o
obj-y	+= smp.o text_amode31.o stacktrace.o abs_lowcore.o facility.o uv.o

extra-y				+= vmlinux.lds

obj-$(CONFIG_SYSFS)		+= nospec-sysfs.o
CFLAGS_REMOVE_nospec-branch.o	+= $(CC_FLAGS_EXPOLINE)

obj-$(CONFIG_MODULES)		+= module.o
obj-$(CONFIG_SCHED_TOPOLOGY)	+= topology.o
obj-$(CONFIG_NUMA)		+= numa.o
obj-$(CONFIG_AUDIT)		+= audit.o
compat-obj-$(CONFIG_AUDIT)	+= compat_audit.o
obj-$(CONFIG_COMPAT)		+= compat_linux.o compat_signal.o
obj-$(CONFIG_COMPAT)		+= $(compat-obj-y)
obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o
obj-$(CONFIG_KPROBES)		+= kprobes.o
obj-$(CONFIG_KPROBES)		+= mcount.o
obj-$(CONFIG_RETHOOK)		+= rethook.o
obj-$(CONFIG_FUNCTION_TRACER)	+= ftrace.o
obj-$(CONFIG_FUNCTION_TRACER)	+= mcount.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump.o
obj-$(CONFIG_KEXEC_CORE)	+= machine_kexec.o relocate_kernel.o
obj-$(CONFIG_VMCORE_INFO)	+= vmcore_info.o
obj-$(CONFIG_UPROBES)		+= uprobes.o
obj-$(CONFIG_JUMP_LABEL)	+= jump_label.o

obj-$(CONFIG_KEXEC_FILE)	+= machine_kexec_file.o kexec_image.o
obj-$(CONFIG_KEXEC_FILE)	+= kexec_elf.o
obj-$(CONFIG_CERT_STORE)	+= cert_store.o
obj-$(CONFIG_IMA_SECURE_AND_OR_TRUSTED_BOOT)	+= ima_arch.o

obj-$(CONFIG_PERF_EVENTS)	+= perf_event.o
obj-$(CONFIG_PERF_EVENTS)	+= perf_cpum_cf.o perf_cpum_sf.o
obj-$(CONFIG_PERF_EVENTS)	+= perf_cpum_cf_events.o perf_regs.o
obj-$(CONFIG_PERF_EVENTS)	+= perf_pai_crypto.o perf_pai_ext.o

obj-$(CONFIG_TRACEPOINTS)	+= trace.o

# vdso
obj-y				+= vdso64/
obj-$(CONFIG_COMPAT)		+= vdso32/
