# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

ifdef CONFIG_DYNAMIC_FTRACE
CFLAGS_REMOVE_ftrace.o = -pg
endif

extra-y	:= head.o init_task.o vmlinux.lds

obj-y := entry.o efi.o efi_stub.o gate-data.o fsys.o ia64_ksyms.o irq.o irq_ia64.o	\
	 irq_lsapic.o ivt.o machvec.o pal.o patch.o process.o perfmon.o ptrace.o sal.o		\
	 salinfo.o setup.o signal.o sys_ia64.o time.o traps.o unaligned.o \
	 unwind.o mca.o mca_asm.o topology.o dma-mapping.o

obj-$(CONFIG_ACPI)		+= acpi.o acpi-ext.o
obj-$(CONFIG_IA64_BRL_EMU)	+= brl_emu.o

obj-$(CONFIG_IA64_PALINFO)	+= palinfo.o
obj-$(CONFIG_IOSAPIC)		+= iosapic.o
obj-$(CONFIG_MODULES)		+= module.o
obj-$(CONFIG_SMP)		+= smp.o smpboot.o
obj-$(CONFIG_NUMA)		+= numa.o
obj-$(CONFIG_PERFMON)		+= perfmon_default_smpl.o
obj-$(CONFIG_IA64_CYCLONE)	+= cyclone.o
obj-$(CONFIG_IA64_MCA_RECOVERY)	+= mca_recovery.o
obj-$(CONFIG_KPROBES)		+= kprobes.o jprobes.o
obj-$(CONFIG_DYNAMIC_FTRACE)	+= ftrace.o
obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel.o crash.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump.o
obj-$(CONFIG_IA64_UNCACHED_ALLOCATOR)	+= uncached.o
obj-$(CONFIG_AUDIT)		+= audit.o
obj-$(CONFIG_PCI_MSI)		+= msi_ia64.o
mca_recovery-y			+= mca_drv.o mca_drv_asm.o
obj-$(CONFIG_IA64_MC_ERR_INJECT)+= err_inject.o
obj-$(CONFIG_STACKTRACE)	+= stacktrace.o

obj-$(CONFIG_IA64_ESI)		+= esi.o
ifneq ($(CONFIG_IA64_ESI),)
obj-y				+= esi_stub.o	# must be in kernel proper
endif
obj-$(CONFIG_INTEL_IOMMU)	+= pci-dma.o
obj-$(CONFIG_SWIOTLB)		+= pci-swiotlb.o

obj-$(CONFIG_BINFMT_ELF)	+= elfcore.o

# fp_emulate() expects f2-f5,f16-f31 to contain the user-level state.
CFLAGS_traps.o  += -mfixed-range=f2-f5,f16-f31

# The gate DSO image is built using a special linker script.
include $(src)/Makefile.gate

# We use internal kbuild rules to avoid the "is up to date" message from make
arch/$(SRCARCH)/kernel/nr-irqs.s: arch/$(SRCARCH)/kernel/nr-irqs.c
	$(Q)mkdir -p $(dir $@)
	$(call if_changed_dep,cc_s_c)

include/generated/nr-irqs.h: arch/$(SRCARCH)/kernel/nr-irqs.s FORCE
	$(call filechk,offsets,__ASM_NR_IRQS_H__)
