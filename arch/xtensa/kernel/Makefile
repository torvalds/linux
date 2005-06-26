#
# Makefile for the Linux/Xtensa kernel.
#

extra-y := head.o vmlinux.lds


obj-y := align.o entry.o irq.o coprocessor.o process.o ptrace.o semaphore.o  \
	 setup.o signal.o syscalls.o time.o traps.o vectors.o platform.o  \
	 pci-dma.o

## windowspill.o

obj-$(CONFIG_KGDB) += xtensa-stub.o
obj-$(CONFIG_PCI) += pci.o
obj-$(CONFIG_MODULES) += xtensa_ksyms.o module.o


