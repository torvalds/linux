/*
 * volume_id - reads filesystem label and uuid
 *
 * Copyright (C) 2005 Kay Sievers <kay.sievers@vrfy.org>
 *
 *	This library is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU Lesser General Public
 *	License as published by the Free Software Foundation; either
 *	version 2.1 of the License, or (at your option) any later version.
 *
 *	This library is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *	Lesser General Public License for more details.
 *
 *	You should have received a copy of the GNU Lesser General Public
 *	License along with this library; if not, write to the Free Software
 *	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

//kbuild:lib-$(CONFIG_VOLUMEID) += volume_id.o util.o

#include "volume_id_internal.h"


/* Some detection routines do not set label or uuid anyway,
 * so they are disabled. */

/* Looks for partitions, we don't use it: */
#define ENABLE_FEATURE_VOLUMEID_MAC           0
/* #define ENABLE_FEATURE_VOLUMEID_MSDOS      0 - NB: this one
 * was not properly added to probe table anyway - ??! */

/* None of RAIDs have label or uuid, except LinuxRAID: */
#define ENABLE_FEATURE_VOLUMEID_HIGHPOINTRAID 0
#define ENABLE_FEATURE_VOLUMEID_ISWRAID       0
#define ENABLE_FEATURE_VOLUMEID_LSIRAID       0
#define ENABLE_FEATURE_VOLUMEID_LVM           0
#define ENABLE_FEATURE_VOLUMEID_NVIDIARAID    0
#define ENABLE_FEATURE_VOLUMEID_PROMISERAID   0
#define ENABLE_FEATURE_VOLUMEID_SILICONRAID   0
#define ENABLE_FEATURE_VOLUMEID_VIARAID       0

/* These filesystems also have no label or uuid: */
#define ENABLE_FEATURE_VOLUMEID_HPFS          0
#define ENABLE_FEATURE_VOLUMEID_UFS           0


typedef int FAST_FUNC (*raid_probe_fptr)(struct volume_id *id, /*uint64_t off,*/ uint64_t size);
typedef int FAST_FUNC (*probe_fptr)(struct volume_id *id /*, uint64_t off*/);

static const raid_probe_fptr raid1[] = {
#if ENABLE_FEATURE_VOLUMEID_LINUXRAID
	volume_id_probe_linux_raid,
#endif
#if ENABLE_FEATURE_VOLUMEID_ISWRAID
	volume_id_probe_intel_software_raid,
#endif
#if ENABLE_FEATURE_VOLUMEID_LSIRAID
	volume_id_probe_lsi_mega_raid,
#endif
#if ENABLE_FEATURE_VOLUMEID_VIARAID
	volume_id_probe_via_raid,
#endif
#if ENABLE_FEATURE_VOLUMEID_SILICONRAID
	volume_id_probe_silicon_medley_raid,
#endif
#if ENABLE_FEATURE_VOLUMEID_NVIDIARAID
	volume_id_probe_nvidia_raid,
#endif
#if ENABLE_FEATURE_VOLUMEID_PROMISERAID
	volume_id_probe_promise_fasttrack_raid,
#endif
#if ENABLE_FEATURE_VOLUMEID_HIGHPOINTRAID
	volume_id_probe_highpoint_45x_raid,
#endif
};

static const probe_fptr raid2[] = {
#if ENABLE_FEATURE_VOLUMEID_LVM
	volume_id_probe_lvm1,
	volume_id_probe_lvm2,
#endif
#if ENABLE_FEATURE_VOLUMEID_HIGHPOINTRAID
	volume_id_probe_highpoint_37x_raid,
#endif
#if ENABLE_FEATURE_VOLUMEID_LUKS
	volume_id_probe_luks,
#endif
};

/* signature in the first block, only small buffer needed */
static const probe_fptr fs1[] = {
#if ENABLE_FEATURE_VOLUMEID_FAT
	volume_id_probe_vfat,
#endif
#if ENABLE_FEATURE_VOLUMEID_EXFAT
	volume_id_probe_exfat,
#endif
#if ENABLE_FEATURE_VOLUMEID_LFS
	volume_id_probe_lfs,
#endif
#if ENABLE_FEATURE_VOLUMEID_MAC
	volume_id_probe_mac_partition_map,
#endif
#if ENABLE_FEATURE_VOLUMEID_SQUASHFS
	volume_id_probe_squashfs,
#endif
#if ENABLE_FEATURE_VOLUMEID_XFS
	volume_id_probe_xfs,
#endif
#if ENABLE_FEATURE_VOLUMEID_BCACHE
	volume_id_probe_bcache,
#endif
};

/* fill buffer with maximum */
static const probe_fptr fs2[] = {
#if ENABLE_FEATURE_VOLUMEID_LINUXSWAP
	volume_id_probe_linux_swap,
#endif
#if ENABLE_FEATURE_VOLUMEID_EXT
	volume_id_probe_ext,
#endif
#if ENABLE_FEATURE_VOLUMEID_BTRFS
	volume_id_probe_btrfs,
#endif
#if ENABLE_FEATURE_VOLUMEID_REISERFS
	volume_id_probe_reiserfs,
#endif
#if ENABLE_FEATURE_VOLUMEID_JFS
	volume_id_probe_jfs,
#endif
#if ENABLE_FEATURE_VOLUMEID_UDF
	volume_id_probe_udf,
#endif
#if ENABLE_FEATURE_VOLUMEID_ISO9660
	volume_id_probe_iso9660,
#endif
#if ENABLE_FEATURE_VOLUMEID_HFS
	volume_id_probe_hfs_hfsplus,
#endif
#if ENABLE_FEATURE_VOLUMEID_UFS
	volume_id_probe_ufs,
#endif
#if ENABLE_FEATURE_VOLUMEID_F2FS
	volume_id_probe_f2fs,
#endif
#if ENABLE_FEATURE_VOLUMEID_NILFS
	volume_id_probe_nilfs,
#endif
#if ENABLE_FEATURE_VOLUMEID_NTFS
	volume_id_probe_ntfs,
#endif
#if ENABLE_FEATURE_VOLUMEID_CRAMFS
	volume_id_probe_cramfs,
#endif
#if ENABLE_FEATURE_VOLUMEID_ROMFS
	volume_id_probe_romfs,
#endif
#if ENABLE_FEATURE_VOLUMEID_HPFS
	volume_id_probe_hpfs,
#endif
#if ENABLE_FEATURE_VOLUMEID_SYSV
	volume_id_probe_sysv,
#endif
#if ENABLE_FEATURE_VOLUMEID_MINIX
	volume_id_probe_minix,
#endif
#if ENABLE_FEATURE_VOLUMEID_OCFS2
	volume_id_probe_ocfs2,
#endif
#if ENABLE_FEATURE_VOLUMEID_UBIFS
	volume_id_probe_ubifs,
#endif
};

int FAST_FUNC volume_id_probe_all(struct volume_id *id, /*uint64_t off,*/ uint64_t size)
{
	unsigned i;

	/* probe for raid first, cause fs probes may be successful on raid members */
	if (size) {
		for (i = 0; i < ARRAY_SIZE(raid1); i++) {
			if (raid1[i](id, /*off,*/ size) == 0)
				goto ret;
			if (id->error)
				goto ret;
		}
	}

	for (i = 0; i < ARRAY_SIZE(raid2); i++) {
		if (raid2[i](id /*,off*/) == 0)
			goto ret;
		if (id->error)
			goto ret;
	}

	/* signature in the first block, only small buffer needed */
	for (i = 0; i < ARRAY_SIZE(fs1); i++) {
		if (fs1[i](id /*,off*/) == 0)
			goto ret;
		if (id->error)
			goto ret;
	}

	/* fill buffer with maximum */
	volume_id_get_buffer(id, 0, SB_BUFFER_SIZE);

	for (i = 0; i < ARRAY_SIZE(fs2); i++) {
		if (fs2[i](id /*,off*/) == 0)
			goto ret;
		if (id->error)
			goto ret;
	}

 ret:
	volume_id_free_buffer(id);
	return (- id->error); /* 0 or -1 */
}

/* open volume by device node */
struct volume_id* FAST_FUNC volume_id_open_node(int fd)
{
	struct volume_id *id;

	id = xzalloc(sizeof(struct volume_id));
	id->fd = fd;
	///* close fd on device close */
	//id->fd_close = 1;
	return id;
}

#ifdef UNUSED
/* open volume by major/minor */
struct volume_id* FAST_FUNC volume_id_open_dev_t(dev_t devt)
{
	struct volume_id *id;
	char *tmp_node[VOLUME_ID_PATH_MAX];

	tmp_node = xasprintf("/dev/.volume_id-%u-%u-%u",
		(unsigned)getpid(), (unsigned)major(devt), (unsigned)minor(devt));

	/* create temporary node to open block device */
	unlink(tmp_node);
	if (mknod(tmp_node, (S_IFBLK | 0600), devt) != 0)
		bb_perror_msg_and_die("can't mknod(%s)", tmp_node);

	id = volume_id_open_node(tmp_node);
	unlink(tmp_node);
	free(tmp_node);
	return id;
}
#endif

void FAST_FUNC free_volume_id(struct volume_id *id)
{
	if (id == NULL)
		return;

	//if (id->fd_close != 0) - always true
		close(id->fd);
	volume_id_free_buffer(id);
#ifdef UNUSED_PARTITION_CODE
	free(id->partitions);
#endif
	free(id);
}
