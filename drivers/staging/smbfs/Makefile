#
# Makefile for the linux smb-filesystem routines.
#

obj-$(CONFIG_SMB_FS) += smbfs.o

smbfs-y := proc.o dir.o cache.o sock.o inode.o file.o ioctl.o getopt.o \
		symlink.o smbiod.o request.o

# If you want debugging output, you may add these flags to the EXTRA_CFLAGS
# SMBFS_PARANOIA should normally be enabled.

ccflags-y := -DSMBFS_PARANOIA
#ccflags-y += -DSMBFS_DEBUG
#ccflags-y += -DSMBFS_DEBUG_VERBOSE
#ccflags-y += -DDEBUG_SMB_TIMESTAMP
#ccflags-y += -Werror

