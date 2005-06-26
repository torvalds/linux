#
# Makefile for the linux kernel.
#

AFLAGS_head.o := -DTEXTADDR=$(TEXTADDR) -DDATAADDR=$(DATAADDR)

# Object file lists.

obj-y		:= compat.o dma.o entry-armv.o entry-common.o irq.o \
		   process.o ptrace.o semaphore.o setup.o signal.o sys_arm.o \
		   time.o traps.o

obj-$(CONFIG_APM)		+= apm.o
obj-$(CONFIG_ARCH_ACORN)	+= ecard.o 
obj-$(CONFIG_FOOTBRIDGE)	+= isa.o
obj-$(CONFIG_FIQ)		+= fiq.o
obj-$(CONFIG_MODULES)		+= armksyms.o module.o
obj-$(CONFIG_ARTHUR)		+= arthur.o
obj-$(CONFIG_ISA_DMA)		+= dma-isa.o
obj-$(CONFIG_PCI)		+= bios32.o
obj-$(CONFIG_SMP)		+= smp.o

obj-$(CONFIG_IWMMXT)		+= iwmmxt.o
AFLAGS_iwmmxt.o			:= -Wa,-mcpu=iwmmxt

ifneq ($(CONFIG_ARCH_EBSA110),y)
  obj-y		+= io.o
endif

head-y			:= head.o
obj-$(CONFIG_DEBUG_LL)	+= debug.o

extra-y := $(head-y) init_task.o vmlinux.lds
