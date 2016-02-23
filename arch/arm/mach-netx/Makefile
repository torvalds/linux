#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y			+= time.o generic.o pfifo.o xc.o

# Specific board support
obj-$(CONFIG_MACH_NXDKN) += nxdkn.o
obj-$(CONFIG_MACH_NXDB500) += nxdb500.o fb.o
obj-$(CONFIG_MACH_NXEB500HMI) += nxeb500hmi.o fb.o
