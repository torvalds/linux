#
# Makefile for the S/390 specific device drivers
#

CFLAGS_sysinfo.o += -Iinclude/math-emu -Iarch/s390/math-emu -w

obj-y += s390mach.o sysinfo.o s390_rdev.o
obj-y += cio/ block/ char/ crypto/ net/ scsi/ kvm/

drivers-y += drivers/s390/built-in.o

