/*
 * Copyright (C) 2005-2013 Junjiro R. Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * judging filesystem type
 */

#ifndef __AUFS_FSTYPE_H__
#define __AUFS_FSTYPE_H__

#ifdef __KERNEL__

#include <linux/fs.h>
#include <linux/magic.h>
#include <linux/romfs_fs.h>

static inline int au_test_aufs(struct super_block *sb)
{
	return sb->s_magic == AUFS_SUPER_MAGIC;
}

static inline const char *au_sbtype(struct super_block *sb)
{
	return sb->s_type->name;
}

static inline int au_test_iso9660(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_ROMFS_FS) || defined(CONFIG_ROMFS_FS_MODULE)
	return sb->s_magic == ROMFS_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_romfs(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_ISO9660_FS) || defined(CONFIG_ISO9660_FS_MODULE)
	return sb->s_magic == ISOFS_SUPER_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_cramfs(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_CRAMFS) || defined(CONFIG_CRAMFS_MODULE)
	return sb->s_magic == CRAMFS_MAGIC;
#endif
	return 0;
}

static inline int au_test_nfs(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_NFS_FS) || defined(CONFIG_NFS_FS_MODULE)
	return sb->s_magic == NFS_SUPER_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_fuse(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_FUSE_FS) || defined(CONFIG_FUSE_FS_MODULE)
	return sb->s_magic == FUSE_SUPER_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_xfs(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_XFS_FS) || defined(CONFIG_XFS_FS_MODULE)
	return sb->s_magic == XFS_SB_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_tmpfs(struct super_block *sb __maybe_unused)
{
#ifdef CONFIG_TMPFS
	return sb->s_magic == TMPFS_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_ecryptfs(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_ECRYPT_FS) || defined(CONFIG_ECRYPT_FS_MODULE)
	return !strcmp(au_sbtype(sb), "ecryptfs");
#else
	return 0;
#endif
}

static inline int au_test_ocfs2(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_OCFS2_FS) || defined(CONFIG_OCFS2_FS_MODULE)
	return sb->s_magic == OCFS2_SUPER_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_ocfs2_dlmfs(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_OCFS2_FS_O2CB) || defined(CONFIG_OCFS2_FS_O2CB_MODULE)
	return sb->s_magic == DLMFS_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_coda(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_CODA_FS) || defined(CONFIG_CODA_FS_MODULE)
	return sb->s_magic == CODA_SUPER_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_v9fs(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_9P_FS) || defined(CONFIG_9P_FS_MODULE)
	return sb->s_magic == V9FS_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_ext4(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_EXT4_FS) || defined(CONFIG_EXT4_FS_MODULE)
	return sb->s_magic == EXT4_SUPER_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_sysv(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_SYSV_FS) || defined(CONFIG_SYSV_FS_MODULE)
	return !strcmp(au_sbtype(sb), "sysv");
#else
	return 0;
#endif
}

static inline int au_test_ramfs(struct super_block *sb)
{
	return sb->s_magic == RAMFS_MAGIC;
}

static inline int au_test_ubifs(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_UBIFS_FS) || defined(CONFIG_UBIFS_FS_MODULE)
	return sb->s_magic == UBIFS_SUPER_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_procfs(struct super_block *sb __maybe_unused)
{
#ifdef CONFIG_PROC_FS
	return sb->s_magic == PROC_SUPER_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_sysfs(struct super_block *sb __maybe_unused)
{
#ifdef CONFIG_SYSFS
	return sb->s_magic == SYSFS_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_configfs(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_CONFIGFS_FS) || defined(CONFIG_CONFIGFS_FS_MODULE)
	return sb->s_magic == CONFIGFS_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_minix(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_MINIX_FS) || defined(CONFIG_MINIX_FS_MODULE)
	return sb->s_magic == MINIX3_SUPER_MAGIC
		|| sb->s_magic == MINIX2_SUPER_MAGIC
		|| sb->s_magic == MINIX2_SUPER_MAGIC2
		|| sb->s_magic == MINIX_SUPER_MAGIC
		|| sb->s_magic == MINIX_SUPER_MAGIC2;
#else
	return 0;
#endif
}

static inline int au_test_cifs(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_CIFS_FS) || defined(CONFIGCIFS_FS_MODULE)
	return sb->s_magic == CIFS_MAGIC_NUMBER;
#else
	return 0;
#endif
}

static inline int au_test_fat(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_FAT_FS) || defined(CONFIG_FAT_FS_MODULE)
	return sb->s_magic == MSDOS_SUPER_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_msdos(struct super_block *sb)
{
	return au_test_fat(sb);
}

static inline int au_test_vfat(struct super_block *sb)
{
	return au_test_fat(sb);
}

static inline int au_test_securityfs(struct super_block *sb __maybe_unused)
{
#ifdef CONFIG_SECURITYFS
	return sb->s_magic == SECURITYFS_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_squashfs(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_SQUASHFS) || defined(CONFIG_SQUASHFS_MODULE)
	return sb->s_magic == SQUASHFS_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_btrfs(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_BTRFS_FS) || defined(CONFIG_BTRFS_FS_MODULE)
	return sb->s_magic == BTRFS_SUPER_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_xenfs(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_XENFS) || defined(CONFIG_XENFS_MODULE)
	return sb->s_magic == XENFS_SUPER_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_debugfs(struct super_block *sb __maybe_unused)
{
#ifdef CONFIG_DEBUG_FS
	return sb->s_magic == DEBUGFS_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_nilfs(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_NILFS) || defined(CONFIG_NILFS_MODULE)
	return sb->s_magic == NILFS_SUPER_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_hfsplus(struct super_block *sb __maybe_unused)
{
#if defined(CONFIG_HFSPLUS_FS) || defined(CONFIG_HFSPLUS_FS_MODULE)
	return sb->s_magic == HFSPLUS_SUPER_MAGIC;
#else
	return 0;
#endif
}

/* ---------------------------------------------------------------------- */
/*
 * they can't be an aufs branch.
 */
static inline int au_test_fs_unsuppoted(struct super_block *sb)
{
	return
#ifndef CONFIG_AUFS_BR_RAMFS
		au_test_ramfs(sb) ||
#endif
		au_test_procfs(sb)
		|| au_test_sysfs(sb)
		|| au_test_configfs(sb)
		|| au_test_debugfs(sb)
		|| au_test_securityfs(sb)
		|| au_test_xenfs(sb)
		|| au_test_ecryptfs(sb)
		/* || !strcmp(au_sbtype(sb), "unionfs") */
		|| au_test_aufs(sb); /* will be supported in next version */
}

static inline int au_test_fs_remote(struct super_block *sb)
{
	return !au_test_tmpfs(sb)
#ifdef CONFIG_AUFS_BR_RAMFS
		&& !au_test_ramfs(sb)
#endif
		&& !(sb->s_type->fs_flags & FS_REQUIRES_DEV);
}

/* ---------------------------------------------------------------------- */

/*
 * Note: these functions (below) are created after reading ->getattr() in all
 * filesystems under linux/fs. it means we have to do so in every update...
 */

/*
 * some filesystems require getattr to refresh the inode attributes before
 * referencing.
 * in most cases, we can rely on the inode attribute in NFS (or every remote fs)
 * and leave the work for d_revalidate()
 */
static inline int au_test_fs_refresh_iattr(struct super_block *sb)
{
	return au_test_nfs(sb)
		|| au_test_fuse(sb)
		/* || au_test_ocfs2(sb) */	/* untested */
		/* || au_test_btrfs(sb) */	/* untested */
		/* || au_test_coda(sb) */	/* untested */
		/* || au_test_v9fs(sb) */	/* untested */
		;
}

/*
 * filesystems which don't maintain i_size or i_blocks.
 */
static inline int au_test_fs_bad_iattr_size(struct super_block *sb)
{
	return au_test_xfs(sb)
		|| au_test_btrfs(sb)
		|| au_test_ubifs(sb)
		|| au_test_hfsplus(sb)	/* maintained, but incorrect */
		/* || au_test_ext4(sb) */	/* untested */
		/* || au_test_ocfs2(sb) */	/* untested */
		/* || au_test_ocfs2_dlmfs(sb) */ /* untested */
		/* || au_test_sysv(sb) */	/* untested */
		/* || au_test_minix(sb) */	/* untested */
		;
}

/*
 * filesystems which don't store the correct value in some of their inode
 * attributes.
 */
static inline int au_test_fs_bad_iattr(struct super_block *sb)
{
	return au_test_fs_bad_iattr_size(sb)
		/* || au_test_cifs(sb) */	/* untested */
		|| au_test_fat(sb)
		|| au_test_msdos(sb)
		|| au_test_vfat(sb);
}

/* they don't check i_nlink in link(2) */
static inline int au_test_fs_no_limit_nlink(struct super_block *sb)
{
	return au_test_tmpfs(sb)
#ifdef CONFIG_AUFS_BR_RAMFS
		|| au_test_ramfs(sb)
#endif
		|| au_test_ubifs(sb)
		|| au_test_hfsplus(sb);
}

/*
 * filesystems which sets S_NOATIME and S_NOCMTIME.
 */
static inline int au_test_fs_notime(struct super_block *sb)
{
	return au_test_nfs(sb)
		|| au_test_fuse(sb)
		|| au_test_ubifs(sb)
		/* || au_test_cifs(sb) */	/* untested */
		;
}

/*
 * filesystems which requires replacing i_mapping.
 */
static inline int au_test_fs_bad_mapping(struct super_block *sb)
{
	return au_test_fuse(sb)
		|| au_test_ubifs(sb);
}

/* temporary support for i#1 in cramfs */
static inline int au_test_fs_unique_ino(struct inode *inode)
{
	if (au_test_cramfs(inode->i_sb))
		return inode->i_ino != 1;
	return 1;
}

/* ---------------------------------------------------------------------- */

/*
 * the filesystem where the xino files placed must support i/o after unlink and
 * maintain i_size and i_blocks.
 */
static inline int au_test_fs_bad_xino(struct super_block *sb)
{
	return au_test_fs_remote(sb)
		|| au_test_fs_bad_iattr_size(sb)
		/* don't want unnecessary work for xino */
		|| au_test_aufs(sb)
		|| au_test_ecryptfs(sb)
		|| au_test_nilfs(sb);
}

static inline int au_test_fs_trunc_xino(struct super_block *sb)
{
	return au_test_tmpfs(sb)
		|| au_test_ramfs(sb);
}

/*
 * test if the @sb is real-readonly.
 */
static inline int au_test_fs_rr(struct super_block *sb)
{
	return au_test_squashfs(sb)
		|| au_test_iso9660(sb)
		|| au_test_cramfs(sb)
		|| au_test_romfs(sb);
}

#endif /* __KERNEL__ */
#endif /* __AUFS_FSTYPE_H__ */
