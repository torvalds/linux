/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#ifndef _EXFAT_H
#define _EXFAT_H

#include <linux/types.h>
#include <linux/buffer_head.h>

#ifdef CONFIG_STAGING_EXFAT_KERNEL_DEBUG
  /* For Debugging Purpose */
	/* IOCTL code 'f' used by
	 *   - file systems typically #0~0x1F
	 *   - embedded terminal devices #128~
	 *   - exts for debugging purpose #99
	 * number 100 and 101 is available now but has possible conflicts
	 */
#define EXFAT_IOC_GET_DEBUGFLAGS	_IOR('f', 100, long)
#define EXFAT_IOC_SET_DEBUGFLAGS	_IOW('f', 101, long)

#define EXFAT_DEBUGFLAGS_INVALID_UMOUNT	0x01
#define EXFAT_DEBUGFLAGS_ERROR_RW	0x02
#endif /* CONFIG_STAGING_EXFAT_KERNEL_DEBUG */

#ifdef CONFIG_STAGING_EXFAT_DEBUG_MSG
#define DEBUG	1
#else
#undef DEBUG
#endif

#define EFSCORRUPTED	EUCLEAN		/* Filesystem is corrupted */

#define DENTRY_SIZE		32	/* dir entry size */
#define DENTRY_SIZE_BITS	5

/* PBR entries */
#define PBR_SIGNATURE	0xAA55
#define EXT_SIGNATURE	0xAA550000
#define VOL_LABEL	"NO NAME    "	/* size should be 11 */
#define OEM_NAME	"MSWIN4.1"	/* size should be 8 */
#define STR_FAT12	"FAT12   "	/* size should be 8 */
#define STR_FAT16	"FAT16   "	/* size should be 8 */
#define STR_FAT32	"FAT32   "	/* size should be 8 */
#define STR_EXFAT	"EXFAT   "	/* size should be 8 */
#define VOL_CLEAN	0x0000
#define VOL_DIRTY	0x0002

/* max number of clusters */
#define FAT12_THRESHOLD		4087		/* 2^12 - 1 + 2 (clu 0 & 1) */
#define FAT16_THRESHOLD		65527		/* 2^16 - 1 + 2 */
#define FAT32_THRESHOLD		268435457	/* 2^28 - 1 + 2 */
#define EXFAT_THRESHOLD		268435457	/* 2^28 - 1 + 2 */

/* file types */
#define TYPE_UNUSED		0x0000
#define TYPE_DELETED		0x0001
#define TYPE_INVALID		0x0002
#define TYPE_CRITICAL_PRI	0x0100
#define TYPE_BITMAP		0x0101
#define TYPE_UPCASE		0x0102
#define TYPE_VOLUME		0x0103
#define TYPE_DIR		0x0104
#define TYPE_FILE		0x011F
#define TYPE_SYMLINK		0x015F
#define TYPE_CRITICAL_SEC	0x0200
#define TYPE_STREAM		0x0201
#define TYPE_EXTEND		0x0202
#define TYPE_ACL		0x0203
#define TYPE_BENIGN_PRI		0x0400
#define TYPE_GUID		0x0401
#define TYPE_PADDING		0x0402
#define TYPE_ACLTAB		0x0403
#define TYPE_BENIGN_SEC		0x0800
#define TYPE_ALL		0x0FFF

/* time modes */
#define TM_CREATE		0
#define TM_MODIFY		1
#define TM_ACCESS		2

/* checksum types */
#define CS_DIR_ENTRY		0
#define CS_PBR_SECTOR		1
#define CS_DEFAULT		2

#define CLUSTER_16(x)		((u16)(x))
#define CLUSTER_32(x)		((u32)(x))

#define START_SECTOR(x)							\
	((((sector_t)((x) - 2)) << p_fs->sectors_per_clu_bits) +	\
	 p_fs->data_start_sector)

#define IS_LAST_SECTOR_IN_CLUSTER(sec)				\
	((((sec) - p_fs->data_start_sector + 1) &		\
	  ((1 <<  p_fs->sectors_per_clu_bits) - 1)) == 0)

#define GET_CLUSTER_FROM_SECTOR(sec)				\
	((u32)((((sec) - p_fs->data_start_sector) >>		\
		p_fs->sectors_per_clu_bits) + 2))

#define GET16(p_src)						\
	(((u16)(p_src)[0]) | (((u16)(p_src)[1]) << 8))
#define GET32(p_src)						\
	(((u32)(p_src)[0]) | (((u32)(p_src)[1]) << 8) |		\
	(((u32)(p_src)[2]) << 16) | (((u32)(p_src)[3]) << 24))
#define GET64(p_src) \
	(((u64)(p_src)[0]) | (((u64)(p_src)[1]) << 8) |		\
	(((u64)(p_src)[2]) << 16) | (((u64)(p_src)[3]) << 24) |	\
	(((u64)(p_src)[4]) << 32) | (((u64)(p_src)[5]) << 40) |	\
	(((u64)(p_src)[6]) << 48) | (((u64)(p_src)[7]) << 56))

#define SET16(p_dst, src)					\
	do {							\
		(p_dst)[0] = (u8)(src);				\
		(p_dst)[1] = (u8)(((u16)(src)) >> 8);		\
	} while (0)
#define SET32(p_dst, src)					\
	do {							\
		(p_dst)[0] = (u8)(src);				\
		(p_dst)[1] = (u8)(((u32)(src)) >> 8);		\
		(p_dst)[2] = (u8)(((u32)(src)) >> 16);		\
		(p_dst)[3] = (u8)(((u32)(src)) >> 24);		\
	} while (0)
#define SET64(p_dst, src)					\
	do {							\
		(p_dst)[0] = (u8)(src);				\
		(p_dst)[1] = (u8)(((u64)(src)) >> 8);		\
		(p_dst)[2] = (u8)(((u64)(src)) >> 16);		\
		(p_dst)[3] = (u8)(((u64)(src)) >> 24);		\
		(p_dst)[4] = (u8)(((u64)(src)) >> 32);		\
		(p_dst)[5] = (u8)(((u64)(src)) >> 40);		\
		(p_dst)[6] = (u8)(((u64)(src)) >> 48);		\
		(p_dst)[7] = (u8)(((u64)(src)) >> 56);		\
	} while (0)

#ifdef __LITTLE_ENDIAN
#define GET16_A(p_src)		(*((u16 *)(p_src)))
#define GET32_A(p_src)		(*((u32 *)(p_src)))
#define GET64_A(p_src)		(*((u64 *)(p_src)))
#define SET16_A(p_dst, src)	(*((u16 *)(p_dst)) = (u16)(src))
#define SET32_A(p_dst, src)	(*((u32 *)(p_dst)) = (u32)(src))
#define SET64_A(p_dst, src)	(*((u64 *)(p_dst)) = (u64)(src))
#else /* BIG_ENDIAN */
#define GET16_A(p_src)		GET16(p_src)
#define GET32_A(p_src)		GET32(p_src)
#define GET64_A(p_src)		GET64(p_src)
#define SET16_A(p_dst, src)	SET16(p_dst, src)
#define SET32_A(p_dst, src)	SET32(p_dst, src)
#define SET64_A(p_dst, src)	SET64(p_dst, src)
#endif

/* cache size (in number of sectors) */
/* (should be an exponential value of 2) */
#define FAT_CACHE_SIZE		128
#define FAT_CACHE_HASH_SIZE	64
#define BUF_CACHE_SIZE		256
#define BUF_CACHE_HASH_SIZE	64

/* Upcase table macro */
#define HIGH_INDEX_BIT	(8)
#define HIGH_INDEX_MASK	(0xFF00)
#define LOW_INDEX_BIT	(16 - HIGH_INDEX_BIT)
#define UTBL_ROW_COUNT	BIT(LOW_INDEX_BIT)
#define UTBL_COL_COUNT	BIT(HIGH_INDEX_BIT)

static inline u16 get_col_index(u16 i)
{
	return i >> LOW_INDEX_BIT;
}

static inline u16 get_row_index(u16 i)
{
	return i & ~HIGH_INDEX_MASK;
}

#define EXFAT_SUPER_MAGIC       (0x2011BAB0L)
#define EXFAT_ROOT_INO          1

/* FAT types */
#define FAT12			0x01	/* FAT12 */
#define FAT16			0x0E	/* Win95 FAT16 (LBA) */
#define FAT32			0x0C	/* Win95 FAT32 (LBA) */
#define EXFAT			0x07	/* exFAT */

/* file name lengths */
#define MAX_CHARSET_SIZE	3	/* max size of multi-byte character */
#define MAX_PATH_DEPTH		15	/* max depth of path name */
#define MAX_NAME_LENGTH		256	/* max len of filename including NULL */
#define MAX_PATH_LENGTH		260	/* max len of pathname including NULL */

/* file attributes */
#define ATTR_NORMAL		0x0000
#define ATTR_READONLY		0x0001
#define ATTR_HIDDEN		0x0002
#define ATTR_SYSTEM		0x0004
#define ATTR_VOLUME		0x0008
#define ATTR_SUBDIR		0x0010
#define ATTR_ARCHIVE		0x0020
#define ATTR_SYMLINK		0x0040
#define ATTR_EXTEND		0x000F
#define ATTR_RWMASK		0x007E

/* file creation modes */
#define FM_REGULAR              0x00
#define FM_SYMLINK              0x40

#define NUM_UPCASE              2918

#ifdef __LITTLE_ENDIAN
#define UNI_CUR_DIR_NAME        ".\0"
#define UNI_PAR_DIR_NAME        ".\0.\0"
#else
#define UNI_CUR_DIR_NAME        "\0."
#define UNI_PAR_DIR_NAME        "\0.\0."
#endif

struct date_time_t {
	u16      year;
	u16      month;
	u16      day;
	u16      hour;
	u16      minute;
	u16      second;
	u16      millisecond;
};

struct vol_info_t {
	u32      FatType;
	u32      ClusterSize;
	u32      NumClusters;
	u32      FreeClusters;
	u32      UsedClusters;
};

/* directory structure */
struct chain_t {
	u32      dir;
	s32       size;
	u8       flags;
};

struct file_id_t {
	struct chain_t     dir;
	s32       entry;
	u32      type;
	u32      attr;
	u32      start_clu;
	u64      size;
	u8       flags;
	s64       rwoffset;
	s32       hint_last_off;
	u32      hint_last_clu;
};

struct dir_entry_t {
	char name[MAX_NAME_LENGTH * MAX_CHARSET_SIZE];
	u32 attr;
	u64 Size;
	u32 num_subdirs;
	struct date_time_t create_timestamp;
	struct date_time_t modify_timestamp;
	struct date_time_t access_timestamp;
};

struct timestamp_t {
	u16      sec;        /* 0 ~ 59               */
	u16      min;        /* 0 ~ 59               */
	u16      hour;       /* 0 ~ 23               */
	u16      day;        /* 1 ~ 31               */
	u16      mon;        /* 1 ~ 12               */
	u16      year;       /* 0 ~ 127 (since 1980) */
};

/* MS_DOS FAT partition boot record (512 bytes) */
struct pbr_sector_t {
	u8       jmp_boot[3];
	u8       oem_name[8];
	u8       bpb[109];
	u8       boot_code[390];
	u8       signature[2];
};

/* MS-DOS FAT12/16 BIOS parameter block (51 bytes) */
struct bpb16_t {
	u8       sector_size[2];
	u8       sectors_per_clu;
	u8       num_reserved[2];
	u8       num_fats;
	u8       num_root_entries[2];
	u8       num_sectors[2];
	u8       media_type;
	u8       num_fat_sectors[2];
	u8       sectors_in_track[2];
	u8       num_heads[2];
	u8       num_hid_sectors[4];
	u8       num_huge_sectors[4];

	u8       phy_drv_no;
	u8       reserved;
	u8       ext_signature;
	u8       vol_serial[4];
	u8       vol_label[11];
	u8       vol_type[8];
};

/* MS-DOS FAT32 BIOS parameter block (79 bytes) */
struct bpb32_t {
	u8       sector_size[2];
	u8       sectors_per_clu;
	u8       num_reserved[2];
	u8       num_fats;
	u8       num_root_entries[2];
	u8       num_sectors[2];
	u8       media_type;
	u8       num_fat_sectors[2];
	u8       sectors_in_track[2];
	u8       num_heads[2];
	u8       num_hid_sectors[4];
	u8       num_huge_sectors[4];
	u8       num_fat32_sectors[4];
	u8       ext_flags[2];
	u8       fs_version[2];
	u8       root_cluster[4];
	u8       fsinfo_sector[2];
	u8       backup_sector[2];
	u8       reserved[12];

	u8       phy_drv_no;
	u8       ext_reserved;
	u8       ext_signature;
	u8       vol_serial[4];
	u8       vol_label[11];
	u8       vol_type[8];
};

/* MS-DOS EXFAT BIOS parameter block (109 bytes) */
struct bpbex_t {
	u8       reserved1[53];
	u8       vol_offset[8];
	u8       vol_length[8];
	u8       fat_offset[4];
	u8       fat_length[4];
	u8       clu_offset[4];
	u8       clu_count[4];
	u8       root_cluster[4];
	u8       vol_serial[4];
	u8       fs_version[2];
	u8       vol_flags[2];
	u8       sector_size_bits;
	u8       sectors_per_clu_bits;
	u8       num_fats;
	u8       phy_drv_no;
	u8       perc_in_use;
	u8       reserved2[7];
};

/* MS-DOS FAT file system information sector (512 bytes) */
struct fsi_sector_t {
	u8       signature1[4];
	u8       reserved1[480];
	u8       signature2[4];
	u8       free_cluster[4];
	u8       next_cluster[4];
	u8       reserved2[14];
	u8       signature3[2];
};

/* MS-DOS FAT directory entry (32 bytes) */
struct dentry_t {
	u8       dummy[32];
};

/* MS-DOS EXFAT file directory entry (32 bytes) */
struct file_dentry_t {
	u8       type;
	u8       num_ext;
	u8       checksum[2];
	u8       attr[2];
	u8       reserved1[2];
	u8       create_time[2];
	u8       create_date[2];
	u8       modify_time[2];
	u8       modify_date[2];
	u8       access_time[2];
	u8       access_date[2];
	u8       create_time_ms;
	u8       modify_time_ms;
	u8       access_time_ms;
	u8       reserved2[9];
};

/* MS-DOS EXFAT stream extension directory entry (32 bytes) */
struct strm_dentry_t {
	u8       type;
	u8       flags;
	u8       reserved1;
	u8       name_len;
	u8       name_hash[2];
	u8       reserved2[2];
	u8       valid_size[8];
	u8       reserved3[4];
	u8       start_clu[4];
	u8       size[8];
};

/* MS-DOS EXFAT file name directory entry (32 bytes) */
struct name_dentry_t {
	u8       type;
	u8       flags;
	u8       unicode_0_14[30];
};

/* MS-DOS EXFAT allocation bitmap directory entry (32 bytes) */
struct bmap_dentry_t {
	u8       type;
	u8       flags;
	u8       reserved[18];
	u8       start_clu[4];
	u8       size[8];
};

/* MS-DOS EXFAT up-case table directory entry (32 bytes) */
struct case_dentry_t {
	u8       type;
	u8       reserved1[3];
	u8       checksum[4];
	u8       reserved2[12];
	u8       start_clu[4];
	u8       size[8];
};

/* MS-DOS EXFAT volume label directory entry (32 bytes) */
struct volm_dentry_t {
	u8       type;
	u8       label_len;
	u8       unicode_0_10[22];
	u8       reserved[8];
};

/* unused entry hint information */
struct uentry_t {
	u32      dir;
	s32       entry;
	struct chain_t     clu;
};

/* unicode name structure */
struct uni_name_t {
	u16      name[MAX_NAME_LENGTH];
	u16      name_hash;
	u8       name_len;
};

struct buf_cache_t {
	struct buf_cache_t *next;
	struct buf_cache_t *prev;
	struct buf_cache_t *hash_next;
	struct buf_cache_t *hash_prev;
	s32                drv;
	sector_t          sec;
	u32               flag;
	struct buffer_head   *buf_bh;
};

struct fs_info_t {
	u32      drv;                    /* drive ID */
	u32      vol_id;                 /* volume serial number */

	u64      num_sectors;            /* num of sectors in volume */
	u32      num_clusters;           /* num of clusters in volume */
	u32      cluster_size;           /* cluster size in bytes */
	u32      cluster_size_bits;
	u32      sectors_per_clu;        /* cluster size in sectors */
	u32      sectors_per_clu_bits;

	u32      PBR_sector;             /* PBR sector */
	u32      FAT1_start_sector;      /* FAT1 start sector */
	u32      FAT2_start_sector;      /* FAT2 start sector */
	u32      root_start_sector;      /* root dir start sector */
	u32      data_start_sector;      /* data area start sector */
	u32      num_FAT_sectors;        /* num of FAT sectors */

	u32      root_dir;               /* root dir cluster */
	u32      dentries_in_root;       /* num of dentries in root dir */
	u32      dentries_per_clu;       /* num of dentries per cluster */

	u32      vol_flag;               /* volume dirty flag */
	struct buffer_head *pbr_bh;         /* PBR sector */

	u32      map_clu;                /* allocation bitmap start cluster */
	u32      map_sectors;            /* num of allocation bitmap sectors */
	struct buffer_head **vol_amap;      /* allocation bitmap */

	u16 **vol_utbl;			/* upcase table */

	u32 clu_srch_ptr;		/* cluster search pointer */
	u32 used_clusters;		/* number of used clusters */
	struct uentry_t hint_uentry;	/* unused entry hint information */

	u32 dev_ejected;	/* block device operation error flag */

	struct mutex v_mutex;

	/* FAT cache */
	struct buf_cache_t FAT_cache_array[FAT_CACHE_SIZE];
	struct buf_cache_t FAT_cache_lru_list;
	struct buf_cache_t FAT_cache_hash_list[FAT_CACHE_HASH_SIZE];

	/* buf cache */
	struct buf_cache_t buf_cache_array[BUF_CACHE_SIZE];
	struct buf_cache_t buf_cache_lru_list;
	struct buf_cache_t buf_cache_hash_list[BUF_CACHE_HASH_SIZE];
};

#define ES_2_ENTRIES		2
#define ES_3_ENTRIES		3
#define ES_ALL_ENTRIES		0

struct entry_set_cache_t {
	/* sector number that contains file_entry */
	sector_t sector;

	/* byte offset in the sector */
	s32 offset;

	/*
	 * flag in stream entry.
	 * 01 for cluster chain,
	 * 03 for contig. clusteres.
	 */
	s32 alloc_flag;

	u32 num_entries;

	/* __buf should be the last member */
	void *__buf;
};

#define EXFAT_ERRORS_CONT	1	/* ignore error and continue */
#define EXFAT_ERRORS_PANIC	2	/* panic on error */
#define EXFAT_ERRORS_RO		3	/* remount r/o on error */

/* ioctl command */
#define EXFAT_IOCTL_GET_VOLUME_ID _IOR('r', 0x12, __u32)

struct exfat_mount_options {
	kuid_t fs_uid;
	kgid_t fs_gid;
	unsigned short fs_fmask;
	unsigned short fs_dmask;

	/* permission for setting the [am]time */
	unsigned short allow_utime;

	/* codepage for shortname conversions */
	unsigned short codepage;

	/* charset for filename input/display */
	char *iocharset;

	unsigned char casesensitive;

	/* on error: continue, panic, remount-ro */
	unsigned char errors;
#ifdef CONFIG_STAGING_EXFAT_DISCARD
	/* flag on if -o dicard specified and device support discard() */
	unsigned char discard;
#endif /* CONFIG_STAGING_EXFAT_DISCARD */
};

#define EXFAT_HASH_BITS		8
#define EXFAT_HASH_SIZE		BIT(EXFAT_HASH_BITS)

/*
 * EXFAT file system in-core superblock data
 */
struct bd_info_t {
	s32 sector_size;	/* in bytes */
	s32 sector_size_bits;
	s32 sector_size_mask;

	/* total number of sectors in this block device */
	s32 num_sectors;

	/* opened or not */
	bool opened;
};

struct exfat_sb_info {
	struct fs_info_t fs_info;
	struct bd_info_t bd_info;

	struct exfat_mount_options options;

	int s_dirt;
	struct mutex s_lock;
	struct nls_table *nls_disk; /* Codepage used on disk */
	struct nls_table *nls_io;   /* Charset used for input and display */

	struct inode *fat_inode;

	spinlock_t inode_hash_lock;
	struct hlist_head inode_hashtable[EXFAT_HASH_SIZE];
#ifdef CONFIG_STAGING_EXFAT_KERNEL_DEBUG
	long debug_flags;
#endif /* CONFIG_STAGING_EXFAT_KERNEL_DEBUG */
};

/*
 * EXFAT file system inode data in memory
 */
struct exfat_inode_info {
	struct file_id_t fid;
	char  *target;
	/* NOTE: mmu_private is 64bits, so must hold ->i_mutex to access */
	loff_t mmu_private;	/* physically allocated size */
	loff_t i_pos;		/* on-disk position of directory entry or 0 */
	struct hlist_node i_hash_fat;	/* hash by i_location */
	struct rw_semaphore truncate_lock;
	struct inode vfs_inode;
	struct rw_semaphore i_alloc_sem; /* protect bmap against truncate */
};

#define EXFAT_SB(sb)		((struct exfat_sb_info *)((sb)->s_fs_info))

static inline struct exfat_inode_info *EXFAT_I(struct inode *inode)
{
	return container_of(inode, struct exfat_inode_info, vfs_inode);
}

/* NLS management function */
u16 nls_upper(struct super_block *sb, u16 a);
int nls_uniname_cmp(struct super_block *sb, u16 *a, u16 *b);
void nls_uniname_to_cstring(struct super_block *sb, u8 *p_cstring,
			    struct uni_name_t *p_uniname);
void nls_cstring_to_uniname(struct super_block *sb,
			    struct uni_name_t *p_uniname, u8 *p_cstring,
			    bool *p_lossy);

/* buffer cache management */
void exfat_buf_init(struct super_block *sb);
void exfat_buf_shutdown(struct super_block *sb);
int exfat_fat_read(struct super_block *sb, u32 loc, u32 *content);
s32 exfat_fat_write(struct super_block *sb, u32 loc, u32 content);
u8 *exfat_fat_getblk(struct super_block *sb, sector_t sec);
void exfat_fat_modify(struct super_block *sb, sector_t sec);
void exfat_fat_release_all(struct super_block *sb);
u8 *exfat_buf_getblk(struct super_block *sb, sector_t sec);
void exfat_buf_modify(struct super_block *sb, sector_t sec);
void exfat_buf_lock(struct super_block *sb, sector_t sec);
void exfat_buf_unlock(struct super_block *sb, sector_t sec);
void exfat_buf_release(struct super_block *sb, sector_t sec);
void exfat_buf_release_all(struct super_block *sb);

/* fs management functions */
void fs_set_vol_flags(struct super_block *sb, u32 new_flag);
void fs_error(struct super_block *sb);

/* cluster management functions */
s32 count_num_clusters(struct super_block *sb, struct chain_t *dir);
void exfat_chain_cont_cluster(struct super_block *sb, u32 chain, s32 len);

/* allocation bitmap management functions */
s32 load_alloc_bitmap(struct super_block *sb);
void free_alloc_bitmap(struct super_block *sb);

/* upcase table management functions */
s32 load_upcase_table(struct super_block *sb);
void free_upcase_table(struct super_block *sb);

/* dir entry management functions */
struct timestamp_t *tm_current(struct timestamp_t *tm);

struct dentry_t *get_entry_in_dir(struct super_block *sb, struct chain_t *p_dir,
				  s32 entry, sector_t *sector);
struct entry_set_cache_t *get_entry_set_in_dir(struct super_block *sb,
					       struct chain_t *p_dir, s32 entry,
					       u32 type,
					       struct dentry_t **file_ep);
void release_entry_set(struct entry_set_cache_t *es);
s32 count_dir_entries(struct super_block *sb, struct chain_t *p_dir);
void update_dir_checksum(struct super_block *sb, struct chain_t *p_dir,
			 s32 entry);
void update_dir_checksum_with_entry_set(struct super_block *sb,
					struct entry_set_cache_t *es);
bool is_dir_empty(struct super_block *sb, struct chain_t *p_dir);

/* name conversion functions */
s32 get_num_entries(struct super_block *sb, struct chain_t *p_dir,
		    struct uni_name_t *p_uniname, s32 *entries);
u16 calc_checksum_2byte(void *data, s32 len, u16 chksum, s32 type);

/* name resolution functions */
s32 resolve_path(struct inode *inode, char *path, struct chain_t *p_dir,
		 struct uni_name_t *p_uniname);

/* file operation functions */
s32 exfat_mount(struct super_block *sb, struct pbr_sector_t *p_pbr);
s32 create_dir(struct inode *inode, struct chain_t *p_dir,
	       struct uni_name_t *p_uniname, struct file_id_t *fid);
s32 create_file(struct inode *inode, struct chain_t *p_dir,
		struct uni_name_t *p_uniname, u8 mode, struct file_id_t *fid);
void remove_file(struct inode *inode, struct chain_t *p_dir, s32 entry);
s32 exfat_rename_file(struct inode *inode, struct chain_t *p_dir, s32 old_entry,
		      struct uni_name_t *p_uniname, struct file_id_t *fid);
s32 move_file(struct inode *inode, struct chain_t *p_olddir, s32 oldentry,
	      struct chain_t *p_newdir, struct uni_name_t *p_uniname,
	      struct file_id_t *fid);

/* sector read/write functions */
int sector_read(struct super_block *sb, sector_t sec,
		struct buffer_head **bh, bool read);
int sector_write(struct super_block *sb, sector_t sec,
		 struct buffer_head *bh, bool sync);
int multi_sector_read(struct super_block *sb, sector_t sec,
		      struct buffer_head **bh, s32 num_secs, bool read);
int multi_sector_write(struct super_block *sb, sector_t sec,
		       struct buffer_head *bh, s32 num_secs, bool sync);

void exfat_bdev_open(struct super_block *sb);
void exfat_bdev_close(struct super_block *sb);
int exfat_bdev_read(struct super_block *sb, sector_t secno,
	      struct buffer_head **bh, u32 num_secs, bool read);
int exfat_bdev_write(struct super_block *sb, sector_t secno,
	       struct buffer_head *bh, u32 num_secs, bool sync);
int exfat_bdev_sync(struct super_block *sb);

/* cluster operation functions */
s32 exfat_alloc_cluster(struct super_block *sb, s32 num_alloc,
			struct chain_t *p_chain);
void exfat_free_cluster(struct super_block *sb, struct chain_t *p_chain,
			s32 do_relse);
s32 exfat_count_used_clusters(struct super_block *sb);

/* dir operation functions */
s32 exfat_find_dir_entry(struct super_block *sb, struct chain_t *p_dir,
			 struct uni_name_t *p_uniname, s32 num_entries,
			 u32 type);
void exfat_delete_dir_entry(struct super_block *sb, struct chain_t *p_dir,
			    s32 entry, s32 order, s32 num_entries);
void exfat_get_uni_name_from_ext_entry(struct super_block *sb,
				       struct chain_t *p_dir, s32 entry,
				       u16 *uniname);
s32 exfat_count_ext_entries(struct super_block *sb, struct chain_t *p_dir,
			    s32 entry, struct dentry_t *p_entry);
s32 exfat_calc_num_entries(struct uni_name_t *p_uniname);

/* dir entry getter/setter */
u32 exfat_get_entry_type(struct dentry_t *p_entry);
u32 exfat_get_entry_attr(struct dentry_t *p_entry);
void exfat_set_entry_attr(struct dentry_t *p_entry, u32 attr);
u8 exfat_get_entry_flag(struct dentry_t *p_entry);
void exfat_set_entry_flag(struct dentry_t *p_entry, u8 flags);
u32 exfat_get_entry_clu0(struct dentry_t *p_entry);
void exfat_set_entry_clu0(struct dentry_t *p_entry, u32 start_clu);
u64 exfat_get_entry_size(struct dentry_t *p_entry);
void exfat_set_entry_size(struct dentry_t *p_entry, u64 size);
void exfat_get_entry_time(struct dentry_t *p_entry, struct timestamp_t *tp,
			  u8 mode);
void exfat_set_entry_time(struct dentry_t *p_entry, struct timestamp_t *tp,
			  u8 mode);

extern const u8 uni_upcase[];
#endif /* _EXFAT_H */
