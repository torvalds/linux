#
# Makefile for the Solaris binary emulation.
#

EXTRA_AFLAGS := -ansi

solaris-objs := entry64.o fs.o misc.o signal.o systbl.o socket.o \
		ioctl.o ipc.o socksys.o timod.o

obj-$(CONFIG_SOLARIS_EMUL) += solaris.o
