
# defined in ${srctree}/fs/fuse/inode.c
# tristate
ifdef CONFIG_FUSE_FS
ccflags-y += -DFUSE_SUPER_MAGIC=0x65735546
endif

# defined in ${srctree}/fs/ocfs2/ocfs2_fs.h
# tristate
ifdef CONFIG_OCFS2_FS
ccflags-y += -DOCFS2_SUPER_MAGIC=0x7461636f
endif

# defined in ${srctree}/fs/ocfs2/dlm/userdlm.h
# tristate
ifdef CONFIG_OCFS2_FS_O2CB
ccflags-y += -DDLMFS_MAGIC=0x76a9f425
endif

# defined in ${srctree}/fs/cifs/cifsfs.c
# tristate
ifdef CONFIG_CIFS_FS
ccflags-y += -DCIFS_MAGIC_NUMBER=0xFF534D42
endif

# defined in ${srctree}/fs/xfs/xfs_sb.h
# tristate
ifdef CONFIG_XFS_FS
ccflags-y += -DXFS_SB_MAGIC=0x58465342
endif

# defined in ${srctree}/fs/configfs/mount.c
# tristate
ifdef CONFIG_CONFIGFS_FS
ccflags-y += -DCONFIGFS_MAGIC=0x62656570
endif

# defined in ${srctree}/fs/9p/v9fs.h
# tristate
ifdef CONFIG_9P_FS
ccflags-y += -DV9FS_MAGIC=0x01021997
endif

# defined in ${srctree}/fs/ubifs/ubifs.h
# tristate
ifdef CONFIG_UBIFS_FS
ccflags-y += -DUBIFS_SUPER_MAGIC=0x24051905
endif

# defined in ${srctree}/fs/hfsplus/hfsplus_raw.h
# tristate
ifdef CONFIG_HFSPLUS_FS
ccflags-y += -DHFSPLUS_SUPER_MAGIC=0x482b
endif
