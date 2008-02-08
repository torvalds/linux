#
# Makefile for the linux kernel.
#

extra-y	:= head.o


obj-y   := entry.o traps.o irq.o debugport.o \
	   process.o ptrace.o setup.o signal.o traps.o time.o \
	   cache.o cacheflush.o

obj-$(CONFIG_ETRAXFS_SIM) += vcs_hook.o

obj-$(CONFIG_SMP) += smp.o
obj-$(CONFIG_ETRAX_KGDB) += kgdb.o kgdb_asm.o
obj-$(CONFIG_ETRAX_FAST_TIMER) += fasttimer.o
obj-$(CONFIG_MODULES)    += crisksyms.o

clean:

