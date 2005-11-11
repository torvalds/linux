#
# Makefile for the linux kernel.
#

ifeq ($(CONFIG_PPC64),y)
EXTRA_CFLAGS	+= -mno-minimal-toc
CFLAGS_ioctl32.o += -Ifs/
endif
ifeq ($(CONFIG_PPC32),y)
CFLAGS_prom_init.o      += -fPIC
CFLAGS_btext.o		+= -fPIC
endif

obj-y				:= semaphore.o cputable.o ptrace.o syscalls.o \
				   irq.o signal_32.o pmc.o vdso.o
obj-y				+= vdso32/
obj-$(CONFIG_PPC64)		+= setup_64.o binfmt_elf32.o sys_ppc32.o \
				   signal_64.o ptrace32.o systbl.o \
				   paca.o ioctl32.o cpu_setup_power4.o \
				   firmware.o sysfs.o udbg.o
obj-$(CONFIG_PPC64)		+= vdso64/
obj-$(CONFIG_ALTIVEC)		+= vecemu.o vector.o
obj-$(CONFIG_POWER4)		+= idle_power4.o
obj-$(CONFIG_PPC_OF)		+= of_device.o
procfs-$(CONFIG_PPC64)		:= proc_ppc64.o
obj-$(CONFIG_PROC_FS)		+= $(procfs-y)
rtaspci-$(CONFIG_PPC64)		:= rtas_pci.o
obj-$(CONFIG_PPC_RTAS)		+= rtas.o $(rtaspci-y)
obj-$(CONFIG_RTAS_FLASH)	+= rtas_flash.o
obj-$(CONFIG_RTAS_PROC)		+= rtas-proc.o
obj-$(CONFIG_LPARCFG)		+= lparcfg.o
obj-$(CONFIG_IBMVIO)		+= vio.o
obj-$(CONFIG_GENERIC_TBSYNC)	+= smp-tbsync.o
obj-$(CONFIG_PPC_PSERIES)	+= udbg_16550.o
obj-$(CONFIG_PPC_MAPLE)		+= udbg_16550.o
udbgscc-$(CONFIG_PPC64)		:= udbg_scc.o
obj-$(CONFIG_PPC_PMAC)		+= $(udbgscc-y)

ifeq ($(CONFIG_PPC_MERGE),y)

extra-$(CONFIG_PPC_STD_MMU)	:= head_32.o
extra-$(CONFIG_PPC64)		:= head_64.o
extra-$(CONFIG_40x)		:= head_4xx.o
extra-$(CONFIG_44x)		:= head_44x.o
extra-$(CONFIG_FSL_BOOKE)	:= head_fsl_booke.o
extra-$(CONFIG_8xx)		:= head_8xx.o
extra-y				+= vmlinux.lds

obj-y				+= process.o init_task.o time.o \
				   prom.o traps.o setup-common.o
obj-$(CONFIG_PPC32)		+= entry_32.o setup_32.o misc_32.o systbl.o
obj-$(CONFIG_PPC64)		+= misc_64.o
obj-$(CONFIG_PPC_OF)		+= prom_init.o
obj-$(CONFIG_MODULES)		+= ppc_ksyms.o
obj-$(CONFIG_BOOTX_TEXT)	+= btext.o
obj-$(CONFIG_6xx)		+= idle_6xx.o
obj-$(CONFIG_SMP)		+= smp.o

ifeq ($(CONFIG_PPC_ISERIES),y)
$(obj)/head_64.o: $(obj)/lparmap.s
AFLAGS_head_64.o += -I$(obj)
endif

else
# stuff used from here for ARCH=ppc or ARCH=ppc64
smpobj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_PPC64)		+= traps.o process.o init_task.o time.o \
				   setup-common.o $(smpobj-y)


endif

extra-$(CONFIG_PPC_FPU)		+= fpu.o
extra-$(CONFIG_PPC64)		+= entry_64.o
