#
# Makefile for i386-specific library files..
#


lib-y = checksum_32.o delay.o usercopy.o getuser.o putuser.o memcpy.o strstr_32.o \
	bitops.o semaphore.o string.o

lib-$(CONFIG_X86_USE_3DNOW) += mmx.o

obj-$(CONFIG_SMP)	+= msr-on-cpu.o
