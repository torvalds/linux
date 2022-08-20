# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

CFLAGS_armv8_deprecated.o := -I$(src)

CFLAGS_REMOVE_ftrace.o = $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_insn.o = $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_return_address.o = $(CC_FLAGS_FTRACE)

# Remove stack protector to avoid triggering unneeded stack canary
# checks due to randomize_kstack_offset.
CFLAGS_REMOVE_syscall.o	 = -fstack-protector -fstack-protector-strong
CFLAGS_syscall.o	+= -fno-stack-protector

# When KASAN is enabled, a stack trace is recorded for every alloc/free, which
# can significantly impact performance. Avoid instrumenting the stack trace
# collection code to minimize this impact.
KASAN_SANITIZE_stacktrace.o := n

# It's not safe to invoke KCOV when portions of the kernel environment aren't
# available or are out-of-sync with HW state. Since `noinstr` doesn't always
# inhibit KCOV instrumentation, disable it for the entire compilation unit.
KCOV_INSTRUMENT_entry-common.o := n
KCOV_INSTRUMENT_idle.o := n

# Object file lists.
obj-y			:= debug-monitors.o entry.o irq.o fpsimd.o		\
			   entry-common.o entry-fpsimd.o process.o ptrace.o	\
			   setup.o signal.o sys.o stacktrace.o time.o traps.o	\
			   io.o vdso.o hyp-stub.o psci.o cpu_ops.o		\
			   return_address.o cpuinfo.o cpu_errata.o		\
			   cpufeature.o alternative.o cacheinfo.o		\
			   smp.o smp_spin_table.o topology.o smccc-call.o	\
			   syscall.o proton-pack.o idreg-override.o idle.o	\
			   patching.o

targets			+= efi-entry.o

OBJCOPYFLAGS := --prefix-symbols=__efistub_
$(obj)/%.stub.o: $(obj)/%.o FORCE
	$(call if_changed,objcopy)

obj-$(CONFIG_COMPAT)			+= sys32.o signal32.o			\
					   sys_compat.o
obj-$(CONFIG_COMPAT)			+= sigreturn32.o
obj-$(CONFIG_KUSER_HELPERS)		+= kuser32.o
obj-$(CONFIG_FUNCTION_TRACER)		+= ftrace.o entry-ftrace.o
obj-$(CONFIG_MODULES)			+= module.o
obj-$(CONFIG_ARM64_MODULE_PLTS)		+= module-plts.o
obj-$(CONFIG_PERF_EVENTS)		+= perf_regs.o perf_callchain.o
obj-$(CONFIG_HW_PERF_EVENTS)		+= perf_event.o
obj-$(CONFIG_HAVE_HW_BREAKPOINT)	+= hw_breakpoint.o
obj-$(CONFIG_CPU_PM)			+= sleep.o suspend.o
obj-$(CONFIG_CPU_IDLE)			+= cpuidle.o
obj-$(CONFIG_JUMP_LABEL)		+= jump_label.o
obj-$(CONFIG_KGDB)			+= kgdb.o
obj-$(CONFIG_EFI)			+= efi.o efi-entry.stub.o		\
					   efi-rt-wrapper.o
obj-$(CONFIG_PCI)			+= pci.o
obj-$(CONFIG_ARMV8_DEPRECATED)		+= armv8_deprecated.o
obj-$(CONFIG_ACPI)			+= acpi.o
obj-$(CONFIG_ACPI_NUMA)			+= acpi_numa.o
obj-$(CONFIG_ARM64_ACPI_PARKING_PROTOCOL)	+= acpi_parking_protocol.o
obj-$(CONFIG_PARAVIRT)			+= paravirt.o
obj-$(CONFIG_RANDOMIZE_BASE)		+= kaslr.o pi/
obj-$(CONFIG_HIBERNATION)		+= hibernate.o hibernate-asm.o
obj-$(CONFIG_ELF_CORE)			+= elfcore.o
obj-$(CONFIG_KEXEC_CORE)		+= machine_kexec.o relocate_kernel.o	\
					   cpu-reset.o
obj-$(CONFIG_KEXEC_FILE)		+= machine_kexec_file.o kexec_image.o
obj-$(CONFIG_ARM64_RELOC_TEST)		+= arm64-reloc-test.o
arm64-reloc-test-y := reloc_test_core.o reloc_test_syms.o
obj-$(CONFIG_CRASH_DUMP)		+= crash_dump.o
obj-$(CONFIG_CRASH_CORE)		+= crash_core.o
obj-$(CONFIG_ARM_SDE_INTERFACE)		+= sdei.o
obj-$(CONFIG_ARM64_PTR_AUTH)		+= pointer_auth.o
obj-$(CONFIG_ARM64_MTE)			+= mte.o
obj-y					+= vdso-wrap.o
obj-$(CONFIG_COMPAT_VDSO)		+= vdso32-wrap.o

# Force dependency (vdso*-wrap.S includes vdso.so through incbin)
$(obj)/vdso-wrap.o: $(obj)/vdso/vdso.so
$(obj)/vdso32-wrap.o: $(obj)/vdso32/vdso.so

obj-y					+= probes/
head-y					:= head.o
extra-y					+= $(head-y) vmlinux.lds

ifeq ($(CONFIG_DEBUG_EFI),y)
AFLAGS_head.o += -DVMLINUX_PATH="\"$(realpath $(objtree)/vmlinux)\""
endif

# for cleaning
subdir- += vdso vdso32
