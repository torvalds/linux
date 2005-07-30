#
# Makefile for s390-specific library files..
#

EXTRA_AFLAGS := -traditional

lib-y += delay.o string.o
lib-$(CONFIG_ARCH_S390_31) += uaccess.o spinlock.o
lib-$(CONFIG_ARCH_S390X) += uaccess64.o spinlock.o
