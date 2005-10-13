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

obj-y				:= semaphore.o cputable.o
obj-$(CONFIG_PPC64)		+= binfmt_elf32.o
obj-$(CONFIG_ALTIVEC)		+= vecemu.o vector.o
obj-$(CONFIG_POWER4)		+= idle_power4.o

ifeq ($(CONFIG_PPC_MERGE),y)

extra-$(CONFIG_PPC_STD_MMU)	:= head_32.o
extra-$(CONFIG_PPC64)		:= head_64.o
extra-$(CONFIG_40x)		:= head_4xx.o
extra-$(CONFIG_44x)		:= head_44x.o
extra-$(CONFIG_FSL_BOOKE)	:= head_fsl_booke.o
extra-$(CONFIG_8xx)		:= head_8xx.o
extra-$(CONFIG_6xx)		+= idle_6xx.o
extra-$(CONFIG_PPC64)		+= entry_64.o
extra-$(CONFIG_PPC_FPU)		+= fpu.o
extra-y				+= vmlinux.lds

obj-y				+= process.o init_task.o \
				   prom.o systbl.o traps.o
obj-$(CONFIG_PPC32)		+= entry_32.o setup_32.o misc_32.o
obj-$(CONFIG_PPC64)		+= setup_64.o misc_64.o
obj-$(CONFIG_PPC_OF)		+= prom_init.o of_device.o
obj-$(CONFIG_MODULES)		+= ppc_ksyms.o
obj-$(CONFIG_BOOTX_TEXT)	+= btext.o

ifeq ($(CONFIG_PPC_ISERIES),y)
$(obj)/head_64.o: $(obj)/lparmap.s
AFLAGS_head_64.o += -I$(obj)
endif

else
# stuff used from here for ARCH=ppc or ARCH=ppc64
obj-$(CONFIG_PPC64)		+= traps.o process.o init_task.o

fpux-$(CONFIG_PPC32)		+= fpu.o
extra-$(CONFIG_PPC_FPU)		+= $(fpux-y)

endif
