# SPDX-License-Identifier: GPL-2.0
#
# Makefile for s390-specific library files..
#

lib-y += delay.o string.o uaccess.o find.o
obj-y += mem.o xor.o
lib-$(CONFIG_SMP) += spinlock.o
lib-$(CONFIG_KPROBES) += probes.o
lib-$(CONFIG_UPROBES) += probes.o

# Instrumenting memory accesses to __user data (in different address space)
# produce false positives
KASAN_SANITIZE_uaccess.o := n

chkbss := mem.o
include $(srctree)/arch/s390/scripts/Makefile.chkbss
