#
# Makefile for the linux kernel.
#

CPPFLAGS_vmlinux.lds	:= -DTEXT_OFFSET=$(TEXT_OFFSET)
AFLAGS_head.o		:= -DTEXT_OFFSET=$(TEXT_OFFSET)
CFLAGS_armv8_deprecated.o := -I$(src)

CFLAGS_REMOVE_ftrace.o = -pg
CFLAGS_REMOVE_insn.o = -pg
CFLAGS_REMOVE_return_address.o = -pg

CFLAGS_setup.o = -DUTS_MACHINE='"$(UTS_MACHINE)"'

# Object file lists.
arm64-obj-y		:= debug-monitors.o entry.o irq.o fpsimd.o		\
			   entry-fpsimd.o process.o ptrace.o setup.o signal.o	\
			   sys.o stacktrace.o time.o traps.o io.o vdso.o	\
			   hyp-stub.o psci.o cpu_ops.o insn.o	\
			   return_address.o cpuinfo.o cpu_errata.o		\
			   cpufeature.o alternative.o cacheinfo.o		\
			   smp.o smp_spin_table.o topology.o smccc-call.o

extra-$(CONFIG_EFI)			:= efi-entry.o

OBJCOPYFLAGS := --prefix-symbols=__efistub_
$(obj)/%.stub.o: $(obj)/%.o FORCE
	$(call if_changed,objcopy)

arm64-obj-$(CONFIG_COMPAT)		+= sys32.o kuser32.o signal32.o 	\
					   sys_compat.o entry32.o
arm64-obj-$(CONFIG_FUNCTION_TRACER)	+= ftrace.o entry-ftrace.o
arm64-obj-$(CONFIG_MODULES)		+= arm64ksyms.o module.o
arm64-obj-$(CONFIG_ARM64_MODULE_PLTS)	+= module-plts.o
arm64-obj-$(CONFIG_PERF_EVENTS)		+= perf_regs.o perf_callchain.o
arm64-obj-$(CONFIG_HW_PERF_EVENTS)	+= perf_event.o
arm64-obj-$(CONFIG_HAVE_HW_BREAKPOINT)	+= hw_breakpoint.o
arm64-obj-$(CONFIG_CPU_PM)		+= sleep.o suspend.o
arm64-obj-$(CONFIG_CPU_IDLE)		+= cpuidle.o
arm64-obj-$(CONFIG_JUMP_LABEL)		+= jump_label.o
arm64-obj-$(CONFIG_KGDB)		+= kgdb.o
arm64-obj-$(CONFIG_EFI)			+= efi.o efi-entry.stub.o
arm64-obj-$(CONFIG_PCI)			+= pci.o
arm64-obj-$(CONFIG_ARMV8_DEPRECATED)	+= armv8_deprecated.o
arm64-obj-$(CONFIG_ACPI)		+= acpi.o
arm64-obj-$(CONFIG_ACPI_NUMA)		+= acpi_numa.o
arm64-obj-$(CONFIG_ARM64_ACPI_PARKING_PROTOCOL)	+= acpi_parking_protocol.o
arm64-obj-$(CONFIG_PARAVIRT)		+= paravirt.o
arm64-obj-$(CONFIG_RANDOMIZE_BASE)	+= kaslr.o
arm64-obj-$(CONFIG_HIBERNATION)		+= hibernate.o hibernate-asm.o
arm64-obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel.o	\
					   cpu-reset.o
arm64-obj-$(CONFIG_ARM64_RELOC_TEST)	+= arm64-reloc-test.o
arm64-reloc-test-y := reloc_test_core.o reloc_test_syms.o
arm64-obj-$(CONFIG_CRASH_DUMP)		+= crash_dump.o

obj-y					+= $(arm64-obj-y) vdso/ probes/
obj-m					+= $(arm64-obj-m)
head-y					:= head.o
extra-y					+= $(head-y) vmlinux.lds

ifeq ($(CONFIG_DEBUG_EFI),y)
AFLAGS_head.o += -DVMLINUX_PATH="\"$(realpath $(objtree)/vmlinux)\""
endif
