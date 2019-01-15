# SPDX-License-Identifier: GPL-2.0

# defined in ${srctree}/fs/fuse/inode.c
# tristate
ifdef CONFIG_FUSE_FS
ccflags-y += -DFUSE_SUPER_MAGIC=0x65735546
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
