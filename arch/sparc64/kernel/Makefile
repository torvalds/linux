# $Id: Makefile,v 1.70 2002/02/09 19:49:30 davem Exp $
# Makefile for the linux kernel.
#

EXTRA_AFLAGS := -ansi
EXTRA_CFLAGS := -Werror

extra-y		:= head.o init_task.o vmlinux.lds

obj-y		:= process.o setup.o cpu.o idprom.o \
		   traps.o devices.o auxio.o una_asm.o \
		   irq.o ptrace.o time.o sys_sparc.o signal.o \
		   unaligned.o central.o pci.o starfire.o semaphore.o \
		   power.o sbus.o iommu_common.o sparc64_ksyms.o chmc.o

obj-$(CONFIG_PCI)	 += ebus.o isa.o pci_common.o pci_iommu.o \
			    pci_psycho.o pci_sabre.o pci_schizo.o
obj-$(CONFIG_SMP)	 += smp.o trampoline.o
obj-$(CONFIG_SPARC32_COMPAT) += sys32.o sys_sparc32.o signal32.o ioctl32.o
obj-$(CONFIG_BINFMT_ELF32) += binfmt_elf32.o
obj-$(CONFIG_BINFMT_AOUT32) += binfmt_aout32.o
obj-$(CONFIG_MODULES) += module.o
obj-$(CONFIG_US3_FREQ) += us3_cpufreq.o
obj-$(CONFIG_US2E_FREQ) += us2e_cpufreq.o
obj-$(CONFIG_KPROBES) += kprobes.o

ifdef CONFIG_SUNOS_EMUL
  obj-y += sys_sunos32.o sunos_ioctl32.o
else
  ifdef CONFIG_SOLARIS_EMUL
    obj-y += sys_sunos32.o sunos_ioctl32.o
  endif
endif

ifneq ($(NEW_GCC),y)
  CMODEL_CFLAG := -mmedlow
else
  CMODEL_CFLAG := -m64 -mcmodel=medlow
endif

head.o: head.S ttable.S itlb_base.S dtlb_base.S dtlb_backend.S dtlb_prot.S \
	etrap.S rtrap.S winfixup.S entry.S

CFLAGS_ioctl32.o += -Ifs/
