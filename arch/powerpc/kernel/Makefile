# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

ifdef CONFIG_PPC64
CFLAGS_prom_init.o	+= $(NO_MINIMAL_TOC)
endif
ifdef CONFIG_PPC32
CFLAGS_prom_init.o      += -fPIC
CFLAGS_btext.o		+= -fPIC
endif

CFLAGS_early_32.o += $(DISABLE_LATENT_ENTROPY_PLUGIN)
CFLAGS_cputable.o += $(DISABLE_LATENT_ENTROPY_PLUGIN)
CFLAGS_prom_init.o += $(DISABLE_LATENT_ENTROPY_PLUGIN)
CFLAGS_btext.o += $(DISABLE_LATENT_ENTROPY_PLUGIN)
CFLAGS_prom.o += $(DISABLE_LATENT_ENTROPY_PLUGIN)

CFLAGS_prom_init.o += -fno-stack-protector
CFLAGS_prom_init.o += -DDISABLE_BRANCH_PROFILING
CFLAGS_prom_init.o += -ffreestanding
CFLAGS_prom_init.o += $(call cc-option, -ftrivial-auto-var-init=uninitialized)

ifdef CONFIG_FUNCTION_TRACER
# Do not trace early boot code
CFLAGS_REMOVE_cputable.o = $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_prom_init.o = $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_btext.o = $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_prom.o = $(CC_FLAGS_FTRACE)
endif

KASAN_SANITIZE_early_32.o := n
KASAN_SANITIZE_cputable.o := n
KASAN_SANITIZE_prom_init.o := n
KASAN_SANITIZE_btext.o := n
KASAN_SANITIZE_paca.o := n
KASAN_SANITIZE_setup_64.o := n
KASAN_SANITIZE_mce.o := n
KASAN_SANITIZE_mce_power.o := n
KASAN_SANITIZE_udbg.o := n
KASAN_SANITIZE_udbg_16550.o := n

# we have to be particularly careful in ppc64 to exclude code that
# runs with translations off, as we cannot access the shadow with
# translations off. However, ppc32 can sanitize this.
ifdef CONFIG_PPC64
KASAN_SANITIZE_traps.o := n
endif

ifdef CONFIG_KASAN
CFLAGS_early_32.o += -DDISABLE_BRANCH_PROFILING
CFLAGS_cputable.o += -DDISABLE_BRANCH_PROFILING
CFLAGS_btext.o += -DDISABLE_BRANCH_PROFILING
endif

KCSAN_SANITIZE_early_32.o := n
KCSAN_SANITIZE_early_64.o := n
KCSAN_SANITIZE_cputable.o := n
KCSAN_SANITIZE_btext.o := n
KCSAN_SANITIZE_paca.o := n
KCSAN_SANITIZE_setup_64.o := n

#ifdef CONFIG_RANDOMIZE_KSTACK_OFFSET
# Remove stack protector to avoid triggering unneeded stack canary
# checks due to randomize_kstack_offset.
CFLAGS_REMOVE_syscall.o = -fstack-protector -fstack-protector-strong
CFLAGS_syscall.o += -fno-stack-protector
#endif

obj-y				:= cputable.o syscalls.o \
				   irq.o align.o signal_$(BITS).o pmc.o vdso.o \
				   process.o systbl.o idle.o \
				   signal.o sysfs.o cacheinfo.o time.o \
				   prom.o traps.o setup-common.o \
				   udbg.o misc.o io.o misc_$(BITS).o \
				   of_platform.o prom_parse.o firmware.o \
				   hw_breakpoint_constraints.o interrupt.o \
				   kdebugfs.o stacktrace.o syscall.o
obj-y				+= ptrace/
obj-$(CONFIG_PPC64)		+= setup_64.o irq_64.o\
				   paca.o nvram_64.o note.o
obj-$(CONFIG_PPC32)		+= sys_ppc32.o
obj-$(CONFIG_COMPAT)		+= sys_ppc32.o signal_32.o
obj-$(CONFIG_VDSO32)		+= vdso32_wrapper.o
obj-$(CONFIG_PPC_WATCHDOG)	+= watchdog.o
obj-$(CONFIG_HAVE_HW_BREAKPOINT)	+= hw_breakpoint.o
obj-$(CONFIG_PPC_DAWR)		+= dawr.o
obj-$(CONFIG_PPC_BOOK3S_64)	+= cpu_setup_ppc970.o cpu_setup_pa6t.o
obj-$(CONFIG_PPC_BOOK3S_64)	+= cpu_setup_power.o
obj-$(CONFIG_PPC_BOOK3S_64)	+= mce.o mce_power.o
obj-$(CONFIG_PPC_BOOK3E_64)	+= exceptions-64e.o idle_64e.o
obj-$(CONFIG_PPC_BARRIER_NOSPEC) += security.o
obj-$(CONFIG_PPC64)		+= vdso64_wrapper.o
obj-$(CONFIG_ALTIVEC)		+= vecemu.o
obj-$(CONFIG_PPC_BOOK3S_IDLE)	+= idle_book3s.o
procfs-y			:= proc_powerpc.o
obj-$(CONFIG_PROC_FS)		+= $(procfs-y)
rtaspci-$(CONFIG_PPC64)-$(CONFIG_PCI)	:= rtas_pci.o
obj-$(CONFIG_PPC_RTAS)		+= rtas_entry.o rtas.o rtas-rtc.o $(rtaspci-y-y)
obj-$(CONFIG_PPC_RTAS_DAEMON)	+= rtasd.o
obj-$(CONFIG_RTAS_FLASH)	+= rtas_flash.o
obj-$(CONFIG_RTAS_PROC)		+= rtas-proc.o
obj-$(CONFIG_PPC_DT_CPU_FTRS)	+= dt_cpu_ftrs.o
obj-$(CONFIG_EEH)              += eeh.o eeh_pe.o eeh_cache.o \
				  eeh_driver.o eeh_event.o eeh_sysfs.o
obj-$(CONFIG_GENERIC_TBSYNC)	+= smp-tbsync.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump.o
obj-$(CONFIG_FA_DUMP)		+= fadump.o
obj-$(CONFIG_PRESERVE_FA_DUMP)	+= fadump.o
obj-$(CONFIG_PPC_85xx)		+= idle_85xx.o
obj-$(CONFIG_PPC_BOOK3S_32)	+= idle_6xx.o l2cr_6xx.o cpu_setup_6xx.o
obj-$(CONFIG_TAU)		+= tau_6xx.o
obj-$(CONFIG_HIBERNATION)	+= swsusp.o suspend.o
ifdef CONFIG_PPC_85xx
obj-$(CONFIG_HIBERNATION)	+= swsusp_85xx.o
else
obj-$(CONFIG_HIBERNATION)	+= swsusp_$(BITS).o
endif
obj64-$(CONFIG_HIBERNATION)	+= swsusp_asm64.o
obj-$(CONFIG_MODULES)		+= module.o module_$(BITS).o
obj-$(CONFIG_44x)		+= cpu_setup_44x.o
obj-$(CONFIG_PPC_E500)		+= cpu_setup_e500.o
obj-$(CONFIG_PPC_DOORBELL)	+= dbell.o
obj-$(CONFIG_JUMP_LABEL)	+= jump_label.o

obj-$(CONFIG_PPC64)		+= head_64.o
obj-$(CONFIG_PPC_BOOK3S_32)	+= head_book3s_32.o
obj-$(CONFIG_40x)		+= head_40x.o
obj-$(CONFIG_44x)		+= head_44x.o
obj-$(CONFIG_PPC_8xx)		+= head_8xx.o
obj-$(CONFIG_PPC_85xx)		+= head_85xx.o
extra-y				+= vmlinux.lds

obj-$(CONFIG_RELOCATABLE)	+= reloc_$(BITS).o

obj-$(CONFIG_PPC32)		+= entry_32.o setup_32.o early_32.o static_call.o
obj-$(CONFIG_PPC64)		+= dma-iommu.o iommu.o
obj-$(CONFIG_KGDB)		+= kgdb.o
obj-$(CONFIG_BOOTX_TEXT)	+= btext.o
obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_KPROBES)		+= kprobes.o
obj-$(CONFIG_OPTPROBES)		+= optprobes.o optprobes_head.o
obj-$(CONFIG_KPROBES_ON_FTRACE)	+= kprobes-ftrace.o
obj-$(CONFIG_UPROBES)		+= uprobes.o
obj-$(CONFIG_PPC_UDBG_16550)	+= legacy_serial.o udbg_16550.o
obj-$(CONFIG_SWIOTLB)		+= dma-swiotlb.o
obj-$(CONFIG_ARCH_HAS_DMA_SET_MASK) += dma-mask.o

pci64-$(CONFIG_PPC64)		+= pci_dn.o pci-hotplug.o isa-bridge.o
obj-$(CONFIG_PCI)		+= pci_$(BITS).o $(pci64-y) \
				   pci-common.o pci_of_scan.o
obj-$(CONFIG_PCI_MSI)		+= msi.o

obj-$(CONFIG_AUDIT)		+= audit.o
obj64-$(CONFIG_AUDIT)		+= compat_audit.o

obj-$(CONFIG_PPC_IO_WORKAROUNDS)	+= io-workarounds.o

obj-y				+= trace/

ifneq ($(CONFIG_PPC_INDIRECT_PIO),y)
obj-y				+= iomap.o
endif

obj64-$(CONFIG_PPC_TRANSACTIONAL_MEM)	+= tm.o

obj-$(CONFIG_PPC64)		+= $(obj64-y)
obj-$(CONFIG_PPC32)		+= $(obj32-y)

ifneq ($(CONFIG_XMON)$(CONFIG_KEXEC_CORE)(CONFIG_PPC_BOOK3S),)
obj-y				+= ppc_save_regs.o
endif

obj-$(CONFIG_EPAPR_PARAVIRT)	+= epapr_paravirt.o epapr_hcalls.o
obj-$(CONFIG_KVM_GUEST)		+= kvm.o kvm_emul.o
ifneq ($(CONFIG_PPC_POWERNV)$(CONFIG_PPC_SVM),)
obj-y				+= ucall.o
endif

obj-$(CONFIG_PPC_SECURE_BOOT)	+= secure_boot.o ima_arch.o secvar-ops.o
obj-$(CONFIG_PPC_SECVAR_SYSFS)	+= secvar-sysfs.o

# Disable GCOV, KCOV & sanitizers in odd or sensitive code
GCOV_PROFILE_prom_init.o := n
KCOV_INSTRUMENT_prom_init.o := n
KCSAN_SANITIZE_prom_init.o := n
UBSAN_SANITIZE_prom_init.o := n
GCOV_PROFILE_kprobes.o := n
KCOV_INSTRUMENT_kprobes.o := n
KCSAN_SANITIZE_kprobes.o := n
UBSAN_SANITIZE_kprobes.o := n
GCOV_PROFILE_kprobes-ftrace.o := n
KCOV_INSTRUMENT_kprobes-ftrace.o := n
KCSAN_SANITIZE_kprobes-ftrace.o := n
UBSAN_SANITIZE_kprobes-ftrace.o := n
GCOV_PROFILE_syscall_64.o := n
KCOV_INSTRUMENT_syscall_64.o := n
UBSAN_SANITIZE_syscall_64.o := n
UBSAN_SANITIZE_vdso.o := n

# Necessary for booting with kcov enabled on book3e machines
KCOV_INSTRUMENT_cputable.o := n
KCOV_INSTRUMENT_setup_64.o := n
KCOV_INSTRUMENT_paca.o := n

CFLAGS_setup_64.o		+= -fno-stack-protector
CFLAGS_paca.o			+= -fno-stack-protector

obj-$(CONFIG_PPC_FPU)		+= fpu.o
obj-$(CONFIG_ALTIVEC)		+= vector.o
obj-$(CONFIG_PPC64)		+= entry_64.o
obj-$(CONFIG_PPC_OF_BOOT_TRAMPOLINE)	+= prom_init.o

extra-$(CONFIG_PPC_OF_BOOT_TRAMPOLINE)	+= prom_init_check

quiet_cmd_prom_init_check = PROMCHK $@
      cmd_prom_init_check = $(CONFIG_SHELL) $< "$(NM)" $(obj)/prom_init.o; touch $@

$(obj)/prom_init_check: $(src)/prom_init_check.sh $(obj)/prom_init.o FORCE
	$(call if_changed,prom_init_check)
targets += prom_init_check

clean-files := vmlinux.lds

# Force dependency (incbin is bad)
$(obj)/vdso32_wrapper.o : $(obj)/vdso/vdso32.so.dbg
$(obj)/vdso64_wrapper.o : $(obj)/vdso/vdso64.so.dbg

# for cleaning
subdir- += vdso
