#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).

# Object file lists.

obj-y			+= time.o generic.o pfifo.o xc.o

# Specific board support
obj-$(CONFIG_MACH_NXDKN) += nxdkn.o
obj-$(CONFIG_MACH_NXDB500) += nxdb500.o fb.o
obj-$(CONFIG_MACH_NXEB500HMI) += nxeb500hmi.o fb.o
