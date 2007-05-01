#
# Makefile for i386-specific library files..
#


lib-y = checksum.o delay.o usercopy.o getuser.o putuser.o memcpy.o strstr.o \
	bitops.o semaphore.o

lib-$(CONFIG_X86_USE_3DNOW) += mmx.o

obj-$(CONFIG_SMP)	+= msr-on-cpu.o
