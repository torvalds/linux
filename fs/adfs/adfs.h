/* Internal data structures for ADFS */

#define ADFS_FREE_FRAG		 0
#define ADFS_BAD_FRAG		 1
#define ADFS_ROOT_FRAG		 2

#define ADFS_NDA_OWNER_READ	(1 << 0)
#define ADFS_NDA_OWNER_WRITE	(1 << 1)
#define ADFS_NDA_LOCKED		(1 << 2)
#define ADFS_NDA_DIRECTORY	(1 << 3)
#define ADFS_NDA_EXECUTE	(1 << 4)
#define ADFS_NDA_PUBLIC_READ	(1 << 5)
#define ADFS_NDA_PUBLIC_WRITE	(1 << 6)

#include "dir_f.h"

struct buffer_head;

/*
 * Directory handling
 */
struct adfs_dir {
	struct super_block	*sb;

	int			nr_buffers;
	struct buffer_head	*bh[4];
	unsigned int		pos;
	unsigned int		parent_id;

	struct adfs_dirheader	dirhead;
	union  adfs_dirtail	dirtail;
};

/*
 * This is the overall maximum name length
 */
#define ADFS_MAX_NAME_LEN	256
struct object_info {
	__u32		parent_id;		/* parent object id	*/
	__u32		file_id;		/* object id		*/
	__u32		loadaddr;		/* load address		*/
	__u32		execaddr;		/* execution address	*/
	__u32		size;			/* size			*/
	__u8		attr;			/* RISC OS attributes	*/
	unsigned char	name_len;		/* name length		*/
	char		name[ADFS_MAX_NAME_LEN];/* file name		*/
};

struct adfs_dir_ops {
	int	(*read)(struct super_block *sb, unsigned int id, unsigned int sz, struct adfs_dir *dir);
	int	(*setpos)(struct adfs_dir *dir, unsigned int fpos);
	int	(*getnext)(struct adfs_dir *dir, struct object_info *obj);
	int	(*update)(struct adfs_dir *dir, struct object_info *obj);
	int	(*create)(struct adfs_dir *dir, struct object_info *obj);
	int	(*remove)(struct adfs_dir *dir, struct object_info *obj);
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
int adfs_write_inode(struct inode *inode,int unused);
int adfs_notify_change(struct dentry *dentry, struct iattr *attr);

/* map.c */
extern int adfs_map_lookup(struct super_block *sb, unsigned int frag_id, unsigned int offset);
extern unsigned int adfs_map_free(struct super_block *sb);

/* Misc */
void __adfs_error(struct super_block *sb, const char *function,
		  const char *fmt, ...);
#define adfs_error(sb, fmt...) __adfs_error(sb, __FUNCTION__, fmt)

/* super.c */

/*
 * Inodes and file operations
 */

/* dir_*.c */
extern struct inode_operations adfs_dir_inode_operations;
extern struct file_operations adfs_dir_operations;
extern struct dentry_operations adfs_dentry_operations;
extern struct adfs_dir_ops adfs_f_dir_ops;
extern struct adfs_dir_ops adfs_fplus_dir_ops;

extern int adfs_dir_update(struct super_block *sb, struct object_info *obj);

/* file.c */
extern struct inode_operations adfs_file_inode_operations;
extern struct file_operations adfs_file_operations;

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
static inline int
__adfs_block_map(struct super_block *sb, unsigned int object_id,
		 unsigned int block)
{
	if (object_id & 255) {
		unsigned int off;

		off = (object_id & 255) - 1;
		block += off << ADFS_SB(sb)->s_log2sharesize;
	}

	return adfs_map_lookup(sb, object_id >> 8, block);
}
