#
# Makefile for s390-specific library files..
#

EXTRA_AFLAGS := -traditional

lib-y += delay.o string.o spinlock.o
lib-y += $(if $(CONFIG_64BIT),uaccess64.o,uaccess.o)
