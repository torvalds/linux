# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux ncp filesystem routines.
#

obj-$(CONFIG_NCP_FS) += ncpfs.o

ncpfs-y      := dir.o file.o inode.o ioctl.o mmap.o ncplib_kernel.o sock.o \
		ncpsign_kernel.o getopt.o

ncpfs-$(CONFIG_NCPFS_EXTRAS)   += symlink.o
ncpfs-$(CONFIG_NCPFS_NFS_NS)   += symlink.o

# If you want debugging output, please uncomment the following line
# ccflags-y := -DDEBUG_NCP=1

CFLAGS_ncplib_kernel.o := -finline-functions
