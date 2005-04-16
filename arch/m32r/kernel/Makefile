#
# Makefile for the Linux/M32R kernel.
#

extra-y	:= head.o init_task.o vmlinux.lds

obj-y	:= process.o entry.o traps.o align.o irq.o setup.o time.o \
	m32r_ksyms.o sys_m32r.o semaphore.o signal.o ptrace.o

obj-$(CONFIG_SMP)		+= smp.o smpboot.o
obj-$(CONFIG_PLAT_MAPPI)	+= setup_mappi.o io_mappi.o
obj-$(CONFIG_PLAT_MAPPI2)	+= setup_mappi2.o io_mappi2.o
obj-$(CONFIG_PLAT_USRV)		+= setup_usrv.o io_usrv.o
obj-$(CONFIG_PLAT_M32700UT)	+= setup_m32700ut.o io_m32700ut.o
obj-$(CONFIG_PLAT_OPSPUT)	+= setup_opsput.o io_opsput.o
obj-$(CONFIG_MODULES)		+= module.o
obj-$(CONFIG_PLAT_OAKS32R)	+= setup_oaks32r.o io_oaks32r.o

EXTRA_AFLAGS	:= -traditional

