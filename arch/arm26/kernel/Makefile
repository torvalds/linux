#
# Makefile for the linux kernel.
#

# Object file lists.

AFLAGS_head.o           := -DTEXTADDR=$(TEXTADDR)

obj-y		:= compat.o dma.o entry.o irq.o process.o ptrace.o       \
		   semaphore.o setup.o signal.o sys_arm.o time.o traps.o \
		   ecard.o dma.o ecard.o fiq.o time.o

extra-y		:= head.o init_task.o vmlinux.lds

obj-$(CONFIG_FIQ)		+= fiq.o
obj-$(CONFIG_MODULES)		+= armksyms.o

