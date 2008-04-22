#
# Makefile for the linux kernel.
#

extra-y	:= head.o init_task.o vmlinux.lds

obj-y := acpi.o entry.o efi.o efi_stub.o gate-data.o fsys.o ia64_ksyms.o irq.o irq_ia64.o	\
	 irq_lsapic.o ivt.o machvec.o pal.o patch.o process.o perfmon.o ptrace.o sal.o		\
	 salinfo.o setup.o signal.o sys_ia64.o time.o traps.o unaligned.o \
	 unwind.o mca.o mca_asm.o topology.o

obj-$(CONFIG_IA64_BRL_EMU)	+= brl_emu.o
obj-$(CONFIG_IA64_GENERIC)	+= acpi-ext.o
obj-$(CONFIG_IA64_HP_ZX1)	+= acpi-ext.o
obj-$(CONFIG_IA64_HP_ZX1_SWIOTLB) += acpi-ext.o

ifneq ($(CONFIG_ACPI_PROCESSOR),)
obj-y				+= acpi-processor.o
endif

obj-$(CONFIG_IA64_PALINFO)	+= palinfo.o
obj-$(CONFIG_IOSAPIC)		+= iosapic.o
obj-$(CONFIG_MODULES)		+= module.o
obj-$(CONFIG_SMP)		+= smp.o smpboot.o
obj-$(CONFIG_NUMA)		+= numa.o
obj-$(CONFIG_PERFMON)		+= perfmon_default_smpl.o
obj-$(CONFIG_IA64_CYCLONE)	+= cyclone.o
obj-$(CONFIG_CPU_FREQ)		+= cpufreq/
obj-$(CONFIG_IA64_MCA_RECOVERY)	+= mca_recovery.o
obj-$(CONFIG_KPROBES)		+= kprobes.o jprobes.o
obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel.o crash.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump.o
obj-$(CONFIG_IA64_UNCACHED_ALLOCATOR)	+= uncached.o
obj-$(CONFIG_AUDIT)		+= audit.o
obj-$(CONFIG_PCI_MSI)		+= msi_ia64.o
mca_recovery-y			+= mca_drv.o mca_drv_asm.o
obj-$(CONFIG_IA64_MC_ERR_INJECT)+= err_inject.o

obj-$(CONFIG_IA64_ESI)		+= esi.o
ifneq ($(CONFIG_IA64_ESI),)
obj-y				+= esi_stub.o	# must be in kernel proper
endif

# The gate DSO image is built using a special linker script.
targets += gate.so gate-syms.o

extra-y += gate.so gate-syms.o gate.lds gate.o

# fp_emulate() expects f2-f5,f16-f31 to contain the user-level state.
CFLAGS_traps.o  += -mfixed-range=f2-f5,f16-f31

CPPFLAGS_gate.lds := -P -C -U$(ARCH)

quiet_cmd_gate = GATE $@
      cmd_gate = $(CC) -nostdlib $(GATECFLAGS_$(@F)) -Wl,-T,$(filter-out FORCE,$^) -o $@

GATECFLAGS_gate.so = -shared -s -Wl,-soname=linux-gate.so.1 \
		     $(call ld-option, -Wl$(comma)--hash-style=sysv)
$(obj)/gate.so: $(obj)/gate.lds $(obj)/gate.o FORCE
	$(call if_changed,gate)

$(obj)/built-in.o: $(obj)/gate-syms.o
$(obj)/built-in.o: ld_flags += -R $(obj)/gate-syms.o

GATECFLAGS_gate-syms.o = -r
$(obj)/gate-syms.o: $(obj)/gate.lds $(obj)/gate.o FORCE
	$(call if_changed,gate)

# gate-data.o contains the gate DSO image as data in section .data.gate.
# We must build gate.so before we can assemble it.
# Note: kbuild does not track this dependency due to usage of .incbin
$(obj)/gate-data.o: $(obj)/gate.so
