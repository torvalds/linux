# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

ifdef CONFIG_DYNAMIC_FTRACE
CFLAGS_REMOVE_ftrace.o = -pg
endif

extra-y	:= vmlinux.lds

obj-y := head.o entry.o efi.o efi_stub.o gate-data.o fsys.o irq.o irq_ia64.o	\
	 irq_lsapic.o ivt.o pal.o patch.o process.o ptrace.o sal.o		\
	 salinfo.o setup.o signal.o sys_ia64.o time.o traps.o unaligned.o \
	 unwind.o mca.o mca_asm.o topology.o dma-mapping.o iosapic.o acpi.o \
	 acpi-ext.o

obj-$(CONFIG_IA64_BRL_EMU)	+= brl_emu.o

obj-$(CONFIG_IA64_PALINFO)	+= palinfo.o
obj-$(CONFIG_MODULES)		+= module.o
obj-$(CONFIG_SMP)		+= smp.o smpboot.o
obj-$(CONFIG_NUMA)		+= numa.o
obj-$(CONFIG_IA64_CYCLONE)	+= cyclone.o
obj-$(CONFIG_IA64_MCA_RECOVERY)	+= mca_recovery.o
obj-$(CONFIG_KPROBES)		+= kprobes.o
obj-$(CONFIG_DYNAMIC_FTRACE)	+= ftrace.o
obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel.o crash.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump.o
obj-$(CONFIG_IA64_UNCACHED_ALLOCATOR)	+= uncached.o
obj-$(CONFIG_AUDIT)		+= audit.o
obj-y				+= msi_ia64.o
mca_recovery-y			+= mca_drv.o mca_drv_asm.o
obj-$(CONFIG_IA64_MC_ERR_INJECT)+= err_inject.o
obj-$(CONFIG_STACKTRACE)	+= stacktrace.o

obj-$(CONFIG_IA64_ESI)		+= esi.o esi_stub.o # must be in kernel proper
obj-$(CONFIG_INTEL_IOMMU)	+= pci-dma.o

obj-$(CONFIG_ELF_CORE)		+= elfcore.o

# fp_emulate() expects f2-f5,f16-f31 to contain the user-level state.
CFLAGS_traps.o  += -mfixed-range=f2-f5,f16-f31

# The gate DSO image is built using a special linker script.
include $(src)/Makefile.gate
