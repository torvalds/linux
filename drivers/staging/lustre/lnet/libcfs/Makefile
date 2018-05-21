# SPDX-License-Identifier: GPL-2.0
subdir-ccflags-y += -I$(srctree)/drivers/staging/lustre/include
subdir-ccflags-y += -I$(srctree)/drivers/staging/lustre/lustre/include

obj-$(CONFIG_LNET) += libcfs.o

libcfs-obj-y += linux-tracefile.o linux-debug.o
libcfs-obj-y += linux-crypto.o
libcfs-obj-y += linux-crypto-adler.o

libcfs-obj-y += debug.o fail.o module.o tracefile.o
libcfs-obj-y += libcfs_string.o hash.o
libcfs-obj-$(CONFIG_SMP) += libcfs_cpu.o
libcfs-obj-y += libcfs_mem.o libcfs_lock.o

libcfs-objs := $(libcfs-obj-y)
