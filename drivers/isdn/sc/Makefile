# Makefile for the sc ISDN device driver

# Each configuration option enables a list of files.

obj-$(CONFIG_ISDN_DRV_SC)	+= sc.o

# Multipart objects.

sc-y				:= shmem.o init.o debug.o packet.o command.o event.o \
		   		   ioctl.o interrupt.o message.o timer.o	
