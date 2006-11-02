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
				   init_task.o process.o systbl.o idle.o
obj-y				+= vdso32/
obj-$(CONFIG_PPC64)		+= setup_64.o binfmt_elf32.o sys_ppc32.o \
				   signal_64.o ptrace32.o \
				   paca.o cpu_setup_ppc970.o \
				   firmware.o sysfs.o nvram_64.o
obj-$(CONFIG_PPC64)		+= vdso64/
obj-$(CONFIG_ALTIVEC)		+= vecemu.o vector.o
obj-$(CONFIG_PPC_970_NAP)	+= idle_power4.o
obj-$(CONFIG_PPC_OF)		+= of_device.o prom_parse.o
procfs-$(CONFIG_PPC64)		:= proc_ppc64.o
obj-$(CONFIG_PROC_FS)		+= $(procfs-y)
rtaspci-$(CONFIG_PPC64)		:= rtas_pci.o
obj-$(CONFIG_PPC_RTAS)		+= rtas.o rtas-rtc.o $(rtaspci-y)
obj-$(CONFIG_RTAS_FLASH)	+= rtas_flash.o
obj-$(CONFIG_RTAS_PROC)		+= rtas-proc.o
obj-$(CONFIG_LPARCFG)		+= lparcfg.o
obj-$(CONFIG_IBMVIO)		+= vio.o
obj-$(CONFIG_IBMEBUS)           += ibmebus.o
obj-$(CONFIG_GENERIC_TBSYNC)	+= smp-tbsync.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump.o
obj-$(CONFIG_6xx)		+= idle_6xx.o l2cr_6xx.o cpu_setup_6xx.o
obj-$(CONFIG_TAU)		+= tau_6xx.o
obj32-$(CONFIG_SOFTWARE_SUSPEND) += swsusp_32.o
obj32-$(CONFIG_MODULES)		+= module_32.o
obj-$(CONFIG_E500)		+= perfmon_fsl_booke.o

ifeq ($(CONFIG_PPC_MERGE),y)

extra-$(CONFIG_PPC_STD_MMU)	:= head_32.o
extra-$(CONFIG_PPC64)		:= head_64.o
extra-$(CONFIG_40x)		:= head_4xx.o
extra-$(CONFIG_44x)		:= head_44x.o
extra-$(CONFIG_FSL_BOOKE)	:= head_fsl_booke.o
extra-$(CONFIG_8xx)		:= head_8xx.o
extra-y				+= vmlinux.lds

obj-y				+= time.o prom.o traps.o setup-common.o \
				   udbg.o misc.o io.o
obj-$(CONFIG_PPC32)		+= entry_32.o setup_32.o misc_32.o
obj-$(CONFIG_PPC64)		+= misc_64.o dma_64.o iommu.o
obj-$(CONFIG_PPC_MULTIPLATFORM)	+= prom_init.o
obj-$(CONFIG_MODULES)		+= ppc_ksyms.o
obj-$(CONFIG_BOOTX_TEXT)	+= btext.o
obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_KPROBES)		+= kprobes.o
obj-$(CONFIG_PPC_UDBG_16550)	+= legacy_serial.o udbg_16550.o
module-$(CONFIG_PPC64)		+= module_64.o
obj-$(CONFIG_MODULES)		+= $(module-y)

pci64-$(CONFIG_PPC64)		+= pci_64.o pci_dn.o pci_iommu.o \
				   pci_direct_iommu.o iomap.o
pci32-$(CONFIG_PPC32)		:= pci_32.o
obj-$(CONFIG_PCI)		+= $(pci64-y) $(pci32-y)
kexec-$(CONFIG_PPC64)		:= machine_kexec_64.o
kexec-$(CONFIG_PPC32)		:= machine_kexec_32.o
obj-$(CONFIG_KEXEC)		+= machine_kexec.o crash.o $(kexec-y)
obj-$(CONFIG_AUDIT)		+= audit.o
obj64-$(CONFIG_AUDIT)		+= compat_audit.o

ifeq ($(CONFIG_PPC_ISERIES),y)
$(obj)/head_64.o: $(obj)/lparmap.s
AFLAGS_head_64.o += -I$(obj)
endif

else
# stuff used from here for ARCH=ppc
smpobj-$(CONFIG_SMP)		+= smp.o

endif

obj-$(CONFIG_PPC32)		+= $(obj32-y)
obj-$(CONFIG_PPC64)		+= $(obj64-y)

extra-$(CONFIG_PPC_FPU)		+= fpu.o
extra-$(CONFIG_PPC64)		+= entry_64.o
