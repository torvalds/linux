#
# Makefile for the x86 low level entry code
#
obj-y				:= entry_$(BITS).o thunk_$(BITS).o syscall_$(BITS).o

obj-y				+= vdso/
obj-y				+= vsyscall/

obj-$(CONFIG_IA32_EMULATION)	+= entry_64_compat.o syscall_32.o

