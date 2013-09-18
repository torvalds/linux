obj-$(CONFIG_9P_FS) := 9p.o

9p-objs := \
	vfs_super.o \
	vfs_inode.o \
	vfs_inode_dotl.o \
	vfs_addr.o \
	vfs_file.o \
	vfs_dir.o \
	vfs_dentry.o \
	v9fs.o \
	fid.o  \
	xattr.o \
	xattr_user.o \
	xattr_trusted.o

9p-$(CONFIG_9P_FSCACHE) += cache.o
9p-$(CONFIG_9P_FS_POSIX_ACL) += acl.o
9p-$(CONFIG_9P_FS_SECURITY) += xattr_security.o
