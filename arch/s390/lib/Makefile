#
# Makefile for s390-specific library files..
#

lib-y += delay.o string.o uaccess_std.o uaccess_pt.o
obj-y += usercopy.o
obj-$(CONFIG_32BIT) += div64.o qrnnd.o ucmpdi2.o mem32.o
obj-$(CONFIG_64BIT) += mem64.o
lib-$(CONFIG_64BIT) += uaccess_mvcos.o
lib-$(CONFIG_SMP) += spinlock.o
