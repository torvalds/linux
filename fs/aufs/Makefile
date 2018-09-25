# SPDX-License-Identifier: GPL-2.0

include ${src}/magic.mk
ifeq (${CONFIG_AUFS_FS},m)
include ${src}/conf.mk
endif
-include ${src}/priv_def.mk

# cf. include/linux/kernel.h
# enable pr_debug
ccflags-y += -DDEBUG
# sparse requires the full pathname
ifdef M
ccflags-y += -include ${M}/../../include/uapi/linux/aufs_type.h
else
ccflags-y += -include ${srctree}/include/uapi/linux/aufs_type.h
endif

obj-$(CONFIG_AUFS_FS) += aufs.o
aufs-y := module.o sbinfo.o super.o branch.o xino.o sysaufs.o opts.o \
	wkq.o vfsub.o dcsub.o \
	cpup.o whout.o wbr_policy.o \
	dinfo.o dentry.o \
	dynop.o \
	finfo.o file.o f_op.o \
	dir.o vdir.o \
	iinfo.o inode.o i_op.o i_op_add.o i_op_del.o i_op_ren.o \
	mvdown.o ioctl.o

# all are boolean
aufs-$(CONFIG_PROC_FS) += procfs.o plink.o
aufs-$(CONFIG_SYSFS) += sysfs.o
aufs-$(CONFIG_DEBUG_FS) += dbgaufs.o
aufs-$(CONFIG_AUFS_BDEV_LOOP) += loop.o
aufs-$(CONFIG_AUFS_HNOTIFY) += hnotify.o
aufs-$(CONFIG_AUFS_HFSNOTIFY) += hfsnotify.o
aufs-$(CONFIG_AUFS_EXPORT) += export.o
aufs-$(CONFIG_AUFS_XATTR) += xattr.o
aufs-$(CONFIG_FS_POSIX_ACL) += posix_acl.o
aufs-$(CONFIG_AUFS_DIRREN) += dirren.o
aufs-$(CONFIG_AUFS_FHSM) += fhsm.o
aufs-$(CONFIG_AUFS_POLL) += poll.o
aufs-$(CONFIG_AUFS_RDU) += rdu.o
aufs-$(CONFIG_AUFS_BR_HFSPLUS) += hfsplus.o
aufs-$(CONFIG_AUFS_DEBUG) += debug.o
aufs-$(CONFIG_AUFS_MAGIC_SYSRQ) += sysrq.o
