# SPDX-License-Identifier: GPL-2.0
subdir-ccflags-y += -I$(srctree)/drivers/staging/lustre/include
subdir-ccflags-y += -I$(srctree)/drivers/staging/lustre/lustre/include

obj-$(CONFIG_LNET) += libcfs.o

libcfs-linux-objs := linux-tracefile.o linux-debug.o
libcfs-linux-objs += linux-cpu.o
libcfs-linux-objs += linux-module.o
libcfs-linux-objs += linux-crypto.o
libcfs-linux-objs += linux-crypto-adler.o

libcfs-linux-objs := $(addprefix linux/,$(libcfs-linux-objs))

libcfs-all-objs := debug.o fail.o module.o tracefile.o \
		   libcfs_string.o hash.o \
		   libcfs_cpu.o libcfs_mem.o libcfs_lock.o

libcfs-objs := $(libcfs-linux-objs) $(libcfs-all-objs)
