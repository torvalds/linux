/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/adfs_fs.h>

/* Internal data structures for ADFS */

#define ADFS_FREE_FRAG		 0
#define ADFS_BAD_FRAG		 1
#define ADFS_ROOT_FRAG		 2

#define ADFS_FILETYPE_NONE	((u16)~0)

/* RISC OS 12-bit filetype is stored in load_address[19:8] */
static inline u16 adfs_filetype(u32 loadaddr)
{
	return (loadaddr & 0xfff00000) == 0xfff00000 ?
	       (loadaddr >> 8) & 0xfff : ADFS_FILETYPE_NONE;
}

#define ADFS_NDA_OWNER_READ	(1 << 0)
#define ADFS_NDA_OWNER_WRITE	(1 << 1)
#define ADFS_NDA_LOCKED		(1 << 2)
#define ADFS_NDA_DIRECTORY	(1 << 3)
#define ADFS_NDA_EXECUTE	(1 << 4)
#define ADFS_NDA_PUBLIC_READ	(1 << 5)
#define ADFS_NDA_PUBLIC_WRITE	(1 << 6)

#include "dir_f.h"

/*
 * adfs file system inode data in memory
 */
struct adfs_inode_info {
	loff_t		mmu_private;
	__u32		parent_id;	/* parent indirect disc address	*/
	__u32		loadaddr;	/* RISC OS load address		*/
	__u32		execaddr;	/* RISC OS exec address		*/
	unsigned int	attr;		/* RISC OS permissions		*/
	struct inode vfs_inode;
};

static inline struct adfs_inode_info *ADFS_I(struct inode *inode)
{
	return container_of(inode, struct adfs_inode_info, vfs_inode);
}

static inline bool adfs_inode_is_stamped(struct inode *inode)
{
	return (ADFS_I(inode)->loadaddr & 0xfff00000) == 0xfff00000;
}

/*
 * Forward-declare this
 */
struct adfs_discmap;
struct adfs_dir_ops;

/*
 * ADFS file system superblock data in memory
 */
struct adfs_sb_info {
	union { struct {
		struct adfs_discmap *s_map;	/* bh list containing map */
		const struct adfs_dir_ops *s_dir; /* directory operations */
		};
		struct rcu_head rcu;	/* used only at shutdown time	 */
	};
	kuid_t		s_uid;		/* owner uid */
	kgid_t		s_gid;		/* owner gid */
	umode_t		s_owner_mask;	/* ADFS owner perm -> unix perm */
	umode_t		s_other_mask;	/* ADFS other perm -> unix perm	*/
	int		s_ftsuffix;	/* ,xyz hex filetype suffix option */

	__u32		s_ids_per_zone;	/* max. no ids in one zone */
	__u32		s_idlen;	/* length of ID in map */
	__u32		s_map_size;	/* sector size of a map	*/
	signed int	s_map2blk;	/* shift left by this for map->sector*/
	unsigned int	s_log2sharesize;/* log2 share size */
	unsigned int	s_namelen;	/* maximum number of characters in name	 */
};

static inline struct adfs_sb_info *ADFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

/*
 * Directory handling
 */
struct adfs_dir {
	struct super_block	*sb;

	int			nr_buffers;
	struct buffer_head	*bh[4];

	/* big directories need allocated buffers */
	struct buffer_head	**bh_fplus;

	unsigned int		pos;
	__u32			parent_id;

	struct adfs_dirheader	dirhead;
	union  adfs_dirtail	dirtail;
};

/*
 * This is the overall maximum name length
 */
#define ADFS_MAX_NAME_LEN	(256 + 4) /* +4 for ,xyz hex filetype suffix */
struct object_info {
	__u32		parent_id;		/* parent object id	*/
	__u32		indaddr;		/* indirect disc addr	*/
	__u32		loadaddr;		/* load address		*/
	__u32		execaddr;		/* execution address	*/
	__u32		size;			/* size			*/
	__u8		attr;			/* RISC OS attributes	*/
	unsigned int	name_len;		/* name length		*/
	char		name[ADFS_MAX_NAME_LEN];/* file name		*/
};

struct adfs_dir_ops {
	int	(*read)(struct super_block *sb, unsigned int indaddr,
			unsigned int size, struct adfs_dir *dir);
	int	(*setpos)(struct adfs_dir *dir, unsigned int fpos);
	int	(*getnext)(struct adfs_dir *dir, struct object_info *obj);
	int	(*update)(struct adfs_dir *dir, struct object_info *obj);
	int	(*create)(struct adfs_dir *dir, struct object_info *obj);
	int	(*remove)(struct adfs_dir *dir, struct object_info *obj);
	int	(*sync)(struct adfs_dir *dir);
	void	(*free)(struct adfs_dir *dir);
};

struct adfs_discmap {
	struct buffer_head	*dm_bh;
	__u32			dm_startblk;
	unsigned int		dm_startbit;
	unsigned int		dm_endbit;
};

/* Inode stuff */
struct inode *adfs_iget(struct super_block *sb, struct object_info *obj);
int adfs_write_inode(struct inode *inode, struct writeback_control *wbc);
int adfs_notify_change(struct dentry *dentry, struct iattr *attr);

/* map.c */
int adfs_map_lookup(struct super_block *sb, u32 frag_id, unsigned int offset);
extern unsigned int adfs_map_free(struct super_block *sb);

/* Misc */
__printf(3, 4)
void __adfs_error(struct super_block *sb, const char *function,
		  const char *fmt, ...);
#define adfs_error(sb, fmt...) __adfs_error(sb, __func__, fmt)
void adfs_msg(struct super_block *sb, const char *pfx, const char *fmt, ...);

/* super.c */

/*
 * Inodes and file operations
 */

/* dir_*.c */
extern const struct inode_operations adfs_dir_inode_operations;
extern const struct file_operations adfs_dir_operations;
extern const struct dentry_operations adfs_dentry_operations;
extern const struct adfs_dir_ops adfs_f_dir_ops;
extern const struct adfs_dir_ops adfs_fplus_dir_ops;

void adfs_object_fixup(struct adfs_dir *dir, struct object_info *obj);
extern int adfs_dir_update(struct super_block *sb, struct object_info *obj,
			   int wait);

/* file.c */
extern const struct inode_operations adfs_file_inode_operations;
extern const struct file_operations adfs_file_operations;

static inline __u32 signed_asl(__u32 val, signed int shift)
{
	if (shift >= 0)
		val <<= shift;
	else
		val >>= -shift;
	return val;
}

/*
 * Calculate the address of a block in an object given the block offset
 * and the object identity.
 *
 * The root directory ID should always be looked up in the map [3.4]
 */
static inline int __adfs_block_map(struct super_block *sb, u32 indaddr,
				   unsigned int block)
{
	if (indaddr & 255) {
		unsigned int off;

		off = (indaddr & 255) - 1;
		block += off << ADFS_SB(sb)->s_log2sharesize;
	}

	return adfs_map_lookup(sb, indaddr >> 8, block);
}

/* Return the disc record from the map */
static inline
struct adfs_discrecord *adfs_map_discrecord(struct adfs_discmap *dm)
{
	return (void *)(dm[0].dm_bh->b_data + 4);
}

static inline u64 adfs_disc_size(const struct adfs_discrecord *dr)
{
	return (u64)le32_to_cpu(dr->disc_size_high) << 32 |
		    le32_to_cpu(dr->disc_size);
}
