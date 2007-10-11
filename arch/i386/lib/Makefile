#
# Makefile for i386-specific library files..
#


lib-y = checksum_32.o delay.o usercopy_32.o getuser_32.o putuser_32.o memcpy_32.o strstr_32.o \
	bitops_32.o semaphore_32.o string_32.o

lib-$(CONFIG_X86_USE_3DNOW) += mmx_32.o

obj-$(CONFIG_SMP)	+= msr-on-cpu.o
