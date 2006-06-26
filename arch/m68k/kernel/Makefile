#
# Makefile for the linux kernel.
#

ifndef CONFIG_SUN3
  extra-y := head.o
else
  extra-y := sun3-head.o
endif
extra-y	+= vmlinux.lds

obj-y	:= entry.o process.o traps.o ints.o dma.o signal.o ptrace.o \
	   sys_m68k.o time.o semaphore.o setup.o m68k_ksyms.o

obj-$(CONFIG_PCI)	+= bios32.o
obj-$(CONFIG_MODULES)	+= module.o

EXTRA_AFLAGS := -traditional
