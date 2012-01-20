#
# Makefile for arch/c6x/kernel/
#

extra-y := head.o vmlinux.lds

obj-y := process.o traps.o irq.o signal.o ptrace.o
obj-y += setup.o sys_c6x.o time.o devicetree.o
obj-y += switch_to.o entry.o vectors.o c6x_ksyms.o
obj-y += soc.o dma.o

obj-$(CONFIG_MODULES)           += module.o
