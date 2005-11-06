#
# Makefile for the Linux filesystems.
#
# 14 Sep 2000, Christoph Hellwig <hch@infradead.org>
# Rewritten to use lists instead of if-statements.
# 

obj-y :=	open.o read_write.o file_table.o buffer.o  bio.o super.o \
		block_dev.o char_dev.o stat.o exec.o pipe.o namei.o fcntl.o \
		ioctl.o readdir.o select.o fifo.o locks.o dcache.o inode.o \
		attr.o bad_inode.o file.o filesystems.o namespace.o aio.o \
		seq_file.o xattr.o libfs.o fs-writeback.o mpage.o direct-io.o \
		ioprio.o

obj-$(CONFIG_INOTIFY)		+= inotify.o
obj-$(CONFIG_EPOLL)		+= eventpoll.o
obj-$(CONFIG_COMPAT)		+= compat.o

nfsd-$(CONFIG_NFSD)		:= nfsctl.o
obj-y				+= $(nfsd-y) $(nfsd-m)

obj-$(CONFIG_BINFMT_AOUT)	+= binfmt_aout.o
obj-$(CONFIG_BINFMT_EM86)	+= binfmt_em86.o
obj-$(CONFIG_BINFMT_MISC)	+= binfmt_misc.o

# binfmt_script is always there
obj-y				+= binfmt_script.o

obj-$(CONFIG_BINFMT_ELF)	+= binfmt_elf.o
obj-$(CONFIG_BINFMT_ELF_FDPIC)	+= binfmt_elf_fdpic.o
obj-$(CONFIG_BINFMT_SOM)	+= binfmt_som.o
obj-$(CONFIG_BINFMT_FLAT)	+= binfmt_flat.o

obj-$(CONFIG_FS_MBCACHE)	+= mbcache.o
obj-$(CONFIG_FS_POSIX_ACL)	+= posix_acl.o xattr_acl.o
obj-$(CONFIG_NFS_COMMON)	+= nfs_common/

obj-$(CONFIG_QUOTA)		+= dquot.o
obj-$(CONFIG_QFMT_V1)		+= quota_v1.o
obj-$(CONFIG_QFMT_V2)		+= quota_v2.o
obj-$(CONFIG_QUOTACTL)		+= quota.o

obj-$(CONFIG_DNOTIFY)		+= dnotify.o

obj-$(CONFIG_PROC_FS)		+= proc/
obj-y				+= partitions/
obj-$(CONFIG_SYSFS)		+= sysfs/
obj-y				+= devpts/

obj-$(CONFIG_PROFILING)		+= dcookies.o
 
# Do not add any filesystems before this line
obj-$(CONFIG_REISERFS_FS)	+= reiserfs/
obj-$(CONFIG_EXT3_FS)		+= ext3/ # Before ext2 so root fs can be ext3
obj-$(CONFIG_JBD)		+= jbd/
obj-$(CONFIG_EXT2_FS)		+= ext2/
obj-$(CONFIG_CRAMFS)		+= cramfs/
obj-$(CONFIG_RAMFS)		+= ramfs/
obj-$(CONFIG_HUGETLBFS)		+= hugetlbfs/
obj-$(CONFIG_CODA_FS)		+= coda/
obj-$(CONFIG_MINIX_FS)		+= minix/
obj-$(CONFIG_FAT_FS)		+= fat/
obj-$(CONFIG_MSDOS_FS)		+= msdos/
obj-$(CONFIG_VFAT_FS)		+= vfat/
obj-$(CONFIG_BFS_FS)		+= bfs/
obj-$(CONFIG_ISO9660_FS)	+= isofs/
obj-$(CONFIG_DEVFS_FS)		+= devfs/
obj-$(CONFIG_HFSPLUS_FS)	+= hfsplus/ # Before hfs to find wrapped HFS+
obj-$(CONFIG_HFS_FS)		+= hfs/
obj-$(CONFIG_VXFS_FS)		+= freevxfs/
obj-$(CONFIG_NFS_FS)		+= nfs/
obj-$(CONFIG_EXPORTFS)		+= exportfs/
obj-$(CONFIG_NFSD)		+= nfsd/
obj-$(CONFIG_LOCKD)		+= lockd/
obj-$(CONFIG_NLS)		+= nls/
obj-$(CONFIG_SYSV_FS)		+= sysv/
obj-$(CONFIG_SMB_FS)		+= smbfs/
obj-$(CONFIG_CIFS)		+= cifs/
obj-$(CONFIG_NCP_FS)		+= ncpfs/
obj-$(CONFIG_HPFS_FS)		+= hpfs/
obj-$(CONFIG_NTFS_FS)		+= ntfs/
obj-$(CONFIG_UFS_FS)		+= ufs/
obj-$(CONFIG_EFS_FS)		+= efs/
obj-$(CONFIG_JFFS_FS)		+= jffs/
obj-$(CONFIG_JFFS2_FS)		+= jffs2/
obj-$(CONFIG_AFFS_FS)		+= affs/
obj-$(CONFIG_ROMFS_FS)		+= romfs/
obj-$(CONFIG_QNX4FS_FS)		+= qnx4/
obj-$(CONFIG_AUTOFS_FS)		+= autofs/
obj-$(CONFIG_AUTOFS4_FS)	+= autofs4/
obj-$(CONFIG_ADFS_FS)		+= adfs/
obj-$(CONFIG_FUSE_FS)		+= fuse/
obj-$(CONFIG_UDF_FS)		+= udf/
obj-$(CONFIG_RELAYFS_FS)	+= relayfs/
obj-$(CONFIG_SUN_OPENPROMFS)	+= openpromfs/
obj-$(CONFIG_JFS_FS)		+= jfs/
obj-$(CONFIG_XFS_FS)		+= xfs/
obj-$(CONFIG_9P_FS)		+= 9p/
obj-$(CONFIG_AFS_FS)		+= afs/
obj-$(CONFIG_BEFS_FS)		+= befs/
obj-$(CONFIG_HOSTFS)		+= hostfs/
obj-$(CONFIG_HPPFS)		+= hppfs/
obj-$(CONFIG_DEBUG_FS)		+= debugfs/
