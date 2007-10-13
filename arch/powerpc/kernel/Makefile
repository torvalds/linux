#
# Makefile for the linux kernel.
#

ifeq ($(CONFIG_PPC64),y)
EXTRA_CFLAGS	+= -mno-minimal-toc
endif
ifeq ($(CONFIG_PPC32),y)
CFLAGS_prom_init.o      += -fPIC
CFLAGS_btext.o		+= -fPIC
endif

obj-y				:= semaphore.o cputable.o ptrace.o syscalls.o \
				   irq.o align.o signal_32.o pmc.o vdso.o \
				   init_task.o process.o systbl.o idle.o \
				   signal.o
obj-y				+= vdso32/
obj-$(CONFIG_PPC64)		+= setup_64.o binfmt_elf32.o sys_ppc32.o \
				   signal_64.o ptrace32.o \
				   paca.o cpu_setup_ppc970.o \
				   cpu_setup_pa6t.o \
				   firmware.o sysfs.o nvram_64.o
obj-$(CONFIG_PPC64)		+= vdso64/
obj-$(CONFIG_ALTIVEC)		+= vecemu.o vector.o
obj-$(CONFIG_PPC_970_NAP)	+= idle_power4.o
obj-$(CONFIG_PPC_OF)		+= of_device.o of_platform.o prom_parse.o
obj-$(CONFIG_PPC_CLOCK)		+= clock.o
procfs-$(CONFIG_PPC64)		:= proc_ppc64.o
obj-$(CONFIG_PROC_FS)		+= $(procfs-y)
rtaspci-$(CONFIG_PPC64)-$(CONFIG_PCI)	:= rtas_pci.o
obj-$(CONFIG_PPC_RTAS)		+= rtas.o rtas-rtc.o $(rtaspci-y-y)
obj-$(CONFIG_RTAS_FLASH)	+= rtas_flash.o
obj-$(CONFIG_RTAS_PROC)		+= rtas-proc.o
obj-$(CONFIG_LPARCFG)		+= lparcfg.o
obj-$(CONFIG_IBMVIO)		+= vio.o
obj-$(CONFIG_IBMEBUS)           += ibmebus.o
obj-$(CONFIG_GENERIC_TBSYNC)	+= smp-tbsync.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump.o
obj-$(CONFIG_6xx)		+= idle_6xx.o l2cr_6xx.o cpu_setup_6xx.o
obj-$(CONFIG_TAU)		+= tau_6xx.o
obj-$(CONFIG_HIBERNATION)	+= swsusp.o suspend.o \
				   swsusp_$(CONFIG_WORD_SIZE).o
obj64-$(CONFIG_HIBERNATION)	+= swsusp_asm64.o
obj-$(CONFIG_MODULES)		+= module_$(CONFIG_WORD_SIZE).o
obj-$(CONFIG_44x)		+= cpu_setup_44x.o

ifeq ($(CONFIG_PPC_MERGE),y)

extra-$(CONFIG_PPC_STD_MMU)	:= head_32.o
extra-$(CONFIG_PPC64)		:= head_64.o
extra-$(CONFIG_40x)		:= head_40x.o
extra-$(CONFIG_44x)		:= head_44x.o
extra-$(CONFIG_FSL_BOOKE)	:= head_fsl_booke.o
extra-$(CONFIG_8xx)		:= head_8xx.o
extra-y				+= vmlinux.lds

obj-y				+= time.o prom.o traps.o setup-common.o \
				   udbg.o misc.o io.o \
				   misc_$(CONFIG_WORD_SIZE).o
obj-$(CONFIG_PPC32)		+= entry_32.o setup_32.o
obj-$(CONFIG_PPC64)		+= dma_64.o iommu.o
obj-$(CONFIG_PPC_MULTIPLATFORM)	+= prom_init.o
obj-$(CONFIG_MODULES)		+= ppc_ksyms.o
obj-$(CONFIG_BOOTX_TEXT)	+= btext.o
obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_KPROBES)		+= kprobes.o
obj-$(CONFIG_PPC_UDBG_16550)	+= legacy_serial.o udbg_16550.o

pci64-$(CONFIG_PPC64)		+= pci_dn.o isa-bridge.o
obj-$(CONFIG_PCI)		+= pci_$(CONFIG_WORD_SIZE).o $(pci64-y) \
				   pci-common.o
obj-$(CONFIG_PCI_MSI)		+= msi.o
obj-$(CONFIG_KEXEC)		+= machine_kexec.o crash.o \
				   machine_kexec_$(CONFIG_WORD_SIZE).o
obj-$(CONFIG_AUDIT)		+= audit.o
obj64-$(CONFIG_AUDIT)		+= compat_audit.o

obj-$(CONFIG_8XX_MINIMAL_FPEMU) += softemu8xx.o

ifneq ($(CONFIG_PPC_INDIRECT_IO),y)
obj-y				+= iomap.o
endif

else
# stuff used from here for ARCH=ppc
smpobj-$(CONFIG_SMP)		+= smp.o

endif

obj-$(CONFIG_PPC64)		+= $(obj64-y)

extra-$(CONFIG_PPC_FPU)		+= fpu.o
extra-$(CONFIG_PPC64)		+= entry_64.o
