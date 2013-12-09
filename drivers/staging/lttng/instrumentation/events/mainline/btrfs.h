#undef TRACE_SYSTEM
#define TRACE_SYSTEM btrfs

#if !defined(_TRACE_BTRFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_BTRFS_H

#include <linux/writeback.h>
#include <linux/tracepoint.h>
#include <trace/events/gfpflags.h>

struct btrfs_root;
struct btrfs_fs_info;
struct btrfs_inode;
struct extent_map;
struct btrfs_ordered_extent;
struct btrfs_delayed_ref_node;
struct btrfs_delayed_tree_ref;
struct btrfs_delayed_data_ref;
struct btrfs_delayed_ref_head;
struct btrfs_block_group_cache;
struct btrfs_free_cluster;
struct map_lookup;
struct extent_buffer;

#define show_ref_type(type)						\
	__print_symbolic(type,						\
		{ BTRFS_TREE_BLOCK_REF_KEY, 	"TREE_BLOCK_REF" },	\
		{ BTRFS_EXTENT_DATA_REF_KEY, 	"EXTENT_DATA_REF" },	\
		{ BTRFS_EXTENT_REF_V0_KEY, 	"EXTENT_REF_V0" },	\
		{ BTRFS_SHARED_BLOCK_REF_KEY, 	"SHARED_BLOCK_REF" },	\
		{ BTRFS_SHARED_DATA_REF_KEY, 	"SHARED_DATA_REF" })

#define __show_root_type(obj)						\
	__print_symbolic_u64(obj,					\
		{ BTRFS_ROOT_TREE_OBJECTID, 	"ROOT_TREE"	},	\
		{ BTRFS_EXTENT_TREE_OBJECTID, 	"EXTENT_TREE"	},	\
		{ BTRFS_CHUNK_TREE_OBJECTID, 	"CHUNK_TREE"	},	\
		{ BTRFS_DEV_TREE_OBJECTID, 	"DEV_TREE"	},	\
		{ BTRFS_FS_TREE_OBJECTID, 	"FS_TREE"	},	\
		{ BTRFS_ROOT_TREE_DIR_OBJECTID, "ROOT_TREE_DIR"	},	\
		{ BTRFS_CSUM_TREE_OBJECTID, 	"CSUM_TREE"	},	\
		{ BTRFS_TREE_LOG_OBJECTID,	"TREE_LOG"	},	\
		{ BTRFS_TREE_RELOC_OBJECTID,	"TREE_RELOC"	},	\
		{ BTRFS_DATA_RELOC_TREE_OBJECTID, "DATA_RELOC_TREE" })

#define show_root_type(obj)						\
	obj, ((obj >= BTRFS_DATA_RELOC_TREE_OBJECTID) ||		\
	      (obj <= BTRFS_CSUM_TREE_OBJECTID )) ? __show_root_type(obj) : "-"

#define BTRFS_GROUP_FLAGS	\
	{ BTRFS_BLOCK_GROUP_DATA,	"DATA"}, \
	{ BTRFS_BLOCK_GROUP_SYSTEM,	"SYSTEM"}, \
	{ BTRFS_BLOCK_GROUP_METADATA,	"METADATA"}, \
	{ BTRFS_BLOCK_GROUP_RAID0,	"RAID0"}, \
	{ BTRFS_BLOCK_GROUP_RAID1,	"RAID1"}, \
	{ BTRFS_BLOCK_GROUP_DUP,	"DUP"}, \
	{ BTRFS_BLOCK_GROUP_RAID10,	"RAID10"}

#define BTRFS_UUID_SIZE 16

TRACE_EVENT(btrfs_transaction_commit,

	TP_PROTO(struct btrfs_root *root),

	TP_ARGS(root),

	TP_STRUCT__entry(
		__field(	u64,  generation		)
		__field(	u64,  root_objectid		)
	),

	TP_fast_assign(
		__entry->generation	= root->fs_info->generation;
		__entry->root_objectid	= root->root_key.objectid;
	),

	TP_printk("root = %llu(%s), gen = %llu",
		  show_root_type(__entry->root_objectid),
		  (unsigned long long)__entry->generation)
);

DECLARE_EVENT_CLASS(btrfs__inode,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	ino_t,  ino			)
		__field(	blkcnt_t,  blocks		)
		__field(	u64,  disk_i_size		)
		__field(	u64,  generation		)
		__field(	u64,  last_trans		)
		__field(	u64,  logged_trans		)
		__field(	u64,  root_objectid		)
	),

	TP_fast_assign(
		__entry->ino	= inode->i_ino;
		__entry->blocks	= inode->i_blocks;
		__entry->disk_i_size  = BTRFS_I(inode)->disk_i_size;
		__entry->generation = BTRFS_I(inode)->generation;
		__entry->last_trans = BTRFS_I(inode)->last_trans;
		__entry->logged_trans = BTRFS_I(inode)->logged_trans;
		__entry->root_objectid =
				BTRFS_I(inode)->root->root_key.objectid;
	),

	TP_printk("root = %llu(%s), gen = %llu, ino = %lu, blocks = %llu, "
		  "disk_i_size = %llu, last_trans = %llu, logged_trans = %llu",
		  show_root_type(__entry->root_objectid),
		  (unsigned long long)__entry->generation,
		  (unsigned long)__entry->ino,
		  (unsigned long long)__entry->blocks,
		  (unsigned long long)__entry->disk_i_size,
		  (unsigned long long)__entry->last_trans,
		  (unsigned long long)__entry->logged_trans)
);

DEFINE_EVENT(btrfs__inode, btrfs_inode_new,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
);

DEFINE_EVENT(btrfs__inode, btrfs_inode_request,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
);

DEFINE_EVENT(btrfs__inode, btrfs_inode_evict,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
);

#define __show_map_type(type)						\
	__print_symbolic_u64(type,					\
		{ EXTENT_MAP_LAST_BYTE, "LAST_BYTE" 	},		\
		{ EXTENT_MAP_HOLE, 	"HOLE" 		},		\
		{ EXTENT_MAP_INLINE, 	"INLINE" 	},		\
		{ EXTENT_MAP_DELALLOC,	"DELALLOC" 	})

#define show_map_type(type)			\
	type, (type >= EXTENT_MAP_LAST_BYTE) ? "-" :  __show_map_type(type)

#define show_map_flags(flag)						\
	__print_flags(flag, "|",					\
		{ EXTENT_FLAG_PINNED, 		"PINNED" 	},	\
		{ EXTENT_FLAG_COMPRESSED, 	"COMPRESSED" 	},	\
		{ EXTENT_FLAG_VACANCY, 		"VACANCY" 	},	\
		{ EXTENT_FLAG_PREALLOC, 	"PREALLOC" 	})

TRACE_EVENT(btrfs_get_extent,

	TP_PROTO(struct btrfs_root *root, struct extent_map *map),

	TP_ARGS(root, map),

	TP_STRUCT__entry(
		__field(	u64,  root_objectid	)
		__field(	u64,  start		)
		__field(	u64,  len		)
		__field(	u64,  orig_start	)
		__field(	u64,  block_start	)
		__field(	u64,  block_len		)
		__field(	unsigned long,  flags	)
		__field(	int,  refs		)
		__field(	unsigned int,  compress_type	)
	),

	TP_fast_assign(
		__entry->root_objectid	= root->root_key.objectid;
		__entry->start 		= map->start;
		__entry->len		= map->len;
		__entry->orig_start	= map->orig_start;
		__entry->block_start	= map->block_start;
		__entry->block_len	= map->block_len;
		__entry->flags		= map->flags;
		__entry->refs		= atomic_read(&map->refs);
		__entry->compress_type	= map->compress_type;
	),

	TP_printk("root = %llu(%s), start = %llu, len = %llu, "
		  "orig_start = %llu, block_start = %llu(%s), "
		  "block_len = %llu, flags = %s, refs = %u, "
		  "compress_type = %u",
		  show_root_type(__entry->root_objectid),
		  (unsigned long long)__entry->start,
		  (unsigned long long)__entry->len,
		  (unsigned long long)__entry->orig_start,
		  show_map_type(__entry->block_start),
		  (unsigned long long)__entry->block_len,
		  show_map_flags(__entry->flags),
		  __entry->refs, __entry->compress_type)
);

#define show_ordered_flags(flags)					\
	__print_symbolic(flags,					\
		{ BTRFS_ORDERED_IO_DONE, 	"IO_DONE" 	},	\
		{ BTRFS_ORDERED_COMPLETE, 	"COMPLETE" 	},	\
		{ BTRFS_ORDERED_NOCOW, 		"NOCOW" 	},	\
		{ BTRFS_ORDERED_COMPRESSED, 	"COMPRESSED" 	},	\
		{ BTRFS_ORDERED_PREALLOC, 	"PREALLOC" 	},	\
		{ BTRFS_ORDERED_DIRECT, 	"DIRECT" 	})

DECLARE_EVENT_CLASS(btrfs__ordered_extent,

	TP_PROTO(struct inode *inode, struct btrfs_ordered_extent *ordered),

	TP_ARGS(inode, ordered),

	TP_STRUCT__entry(
		__field(	ino_t,  ino		)
		__field(	u64,  file_offset	)
		__field(	u64,  start		)
		__field(	u64,  len		)
		__field(	u64,  disk_len		)
		__field(	u64,  bytes_left	)
		__field(	unsigned long,  flags	)
		__field(	int,  compress_type	)
		__field(	int,  refs		)
		__field(	u64,  root_objectid	)
	),

	TP_fast_assign(
		__entry->ino 		= inode->i_ino;
		__entry->file_offset	= ordered->file_offset;
		__entry->start		= ordered->start;
		__entry->len		= ordered->len;
		__entry->disk_len	= ordered->disk_len;
		__entry->bytes_left	= ordered->bytes_left;
		__entry->flags		= ordered->flags;
		__entry->compress_type	= ordered->compress_type;
		__entry->refs		= atomic_read(&ordered->refs);
		__entry->root_objectid	=
				BTRFS_I(inode)->root->root_key.objectid;
	),

	TP_printk("root = %llu(%s), ino = %llu, file_offset = %llu, "
		  "start = %llu, len = %llu, disk_len = %llu, "
		  "bytes_left = %llu, flags = %s, compress_type = %d, "
		  "refs = %d",
		  show_root_type(__entry->root_objectid),
		  (unsigned long long)__entry->ino,
		  (unsigned long long)__entry->file_offset,
		  (unsigned long long)__entry->start,
		  (unsigned long long)__entry->len,
		  (unsigned long long)__entry->disk_len,
		  (unsigned long long)__entry->bytes_left,
		  show_ordered_flags(__entry->flags),
		  __entry->compress_type, __entry->refs)
);

DEFINE_EVENT(btrfs__ordered_extent, btrfs_ordered_extent_add,

	TP_PROTO(struct inode *inode, struct btrfs_ordered_extent *ordered),

	TP_ARGS(inode, ordered)
);

DEFINE_EVENT(btrfs__ordered_extent, btrfs_ordered_extent_remove,

	TP_PROTO(struct inode *inode, struct btrfs_ordered_extent *ordered),

	TP_ARGS(inode, ordered)
);

DEFINE_EVENT(btrfs__ordered_extent, btrfs_ordered_extent_start,

	TP_PROTO(struct inode *inode, struct btrfs_ordered_extent *ordered),

	TP_ARGS(inode, ordered)
);

DEFINE_EVENT(btrfs__ordered_extent, btrfs_ordered_extent_put,

	TP_PROTO(struct inode *inode, struct btrfs_ordered_extent *ordered),

	TP_ARGS(inode, ordered)
);

DECLARE_EVENT_CLASS(btrfs__writepage,

	TP_PROTO(struct page *page, struct inode *inode,
		 struct writeback_control *wbc),

	TP_ARGS(page, inode, wbc),

	TP_STRUCT__entry(
		__field(	ino_t,  ino			)
		__field(	pgoff_t,  index			)
		__field(	long,   nr_to_write		)
		__field(	long,   pages_skipped		)
		__field(	loff_t, range_start		)
		__field(	loff_t, range_end		)
		__field(	char,   for_kupdate		)
		__field(	char,   for_reclaim		)
		__field(	char,   range_cyclic		)
		__field(	pgoff_t,  writeback_index	)
		__field(	u64,    root_objectid		)
	),

	TP_fast_assign(
		__entry->ino		= inode->i_ino;
		__entry->index		= page->index;
		__entry->nr_to_write	= wbc->nr_to_write;
		__entry->pages_skipped	= wbc->pages_skipped;
		__entry->range_start	= wbc->range_start;
		__entry->range_end	= wbc->range_end;
		__entry->for_kupdate	= wbc->for_kupdate;
		__entry->for_reclaim	= wbc->for_reclaim;
		__entry->range_cyclic	= wbc->range_cyclic;
		__entry->writeback_index = inode->i_mapping->writeback_index;
		__entry->root_objectid	=
				 BTRFS_I(inode)->root->root_key.objectid;
	),

	TP_printk("root = %llu(%s), ino = %lu, page_index = %lu, "
		  "nr_to_write = %ld, pages_skipped = %ld, range_start = %llu, "
		  "range_end = %llu, for_kupdate = %d, "
		  "for_reclaim = %d, range_cyclic = %d, writeback_index = %lu",
		  show_root_type(__entry->root_objectid),
		  (unsigned long)__entry->ino, __entry->index,
		  __entry->nr_to_write, __entry->pages_skipped,
		  __entry->range_start, __entry->range_end,
		  __entry->for_kupdate,
		  __entry->for_reclaim, __entry->range_cyclic,
		  (unsigned long)__entry->writeback_index)
);

DEFINE_EVENT(btrfs__writepage, __extent_writepage,

	TP_PROTO(struct page *page, struct inode *inode,
		 struct writeback_control *wbc),

	TP_ARGS(page, inode, wbc)
);

TRACE_EVENT(btrfs_writepage_end_io_hook,

	TP_PROTO(struct page *page, u64 start, u64 end, int uptodate),

	TP_ARGS(page, start, end, uptodate),

	TP_STRUCT__entry(
		__field(	ino_t,	 ino		)
		__field(	pgoff_t, index		)
		__field(	u64,	 start		)
		__field(	u64,	 end		)
		__field(	int,	 uptodate	)
		__field(	u64,    root_objectid	)
	),

	TP_fast_assign(
		__entry->ino	= page->mapping->host->i_ino;
		__entry->index	= page->index;
		__entry->start	= start;
		__entry->end	= end;
		__entry->uptodate = uptodate;
		__entry->root_objectid	=
			 BTRFS_I(page->mapping->host)->root->root_key.objectid;
	),

	TP_printk("root = %llu(%s), ino = %lu, page_index = %lu, start = %llu, "
		  "end = %llu, uptodate = %d",
		  show_root_type(__entry->root_objectid),
		  (unsigned long)__entry->ino, (unsigned long)__entry->index,
		  (unsigned long long)__entry->start,
		  (unsigned long long)__entry->end, __entry->uptodate)
);

TRACE_EVENT(btrfs_sync_file,

	TP_PROTO(struct file *file, int datasync),

	TP_ARGS(file, datasync),

	TP_STRUCT__entry(
		__field(	ino_t,  ino		)
		__field(	ino_t,  parent		)
		__field(	int,    datasync	)
		__field(	u64,    root_objectid	)
	),

	TP_fast_assign(
		struct dentry *dentry = file->f_path.dentry;
		struct inode *inode = dentry->d_inode;

		__entry->ino		= inode->i_ino;
		__entry->parent		= dentry->d_parent->d_inode->i_ino;
		__entry->datasync	= datasync;
		__entry->root_objectid	=
				 BTRFS_I(inode)->root->root_key.objectid;
	),

	TP_printk("root = %llu(%s), ino = %ld, parent = %ld, datasync = %d",
		  show_root_type(__entry->root_objectid),
		  (unsigned long)__entry->ino, (unsigned long)__entry->parent,
		  __entry->datasync)
);

TRACE_EVENT(btrfs_sync_fs,

	TP_PROTO(int wait),

	TP_ARGS(wait),

	TP_STRUCT__entry(
		__field(	int,  wait		)
	),

	TP_fast_assign(
		__entry->wait	= wait;
	),

	TP_printk("wait = %d", __entry->wait)
);

#define show_ref_action(action)						\
	__print_symbolic(action,					\
		{ BTRFS_ADD_DELAYED_REF,    "ADD_DELAYED_REF" },	\
		{ BTRFS_DROP_DELAYED_REF,   "DROP_DELAYED_REF" },	\
		{ BTRFS_ADD_DELAYED_EXTENT, "ADD_DELAYED_EXTENT" }, 	\
		{ BTRFS_UPDATE_DELAYED_HEAD, "UPDATE_DELAYED_HEAD" })


TRACE_EVENT(btrfs_delayed_tree_ref,

	TP_PROTO(struct btrfs_delayed_ref_node *ref,
		 struct btrfs_delayed_tree_ref *full_ref,
		 int action),

	TP_ARGS(ref, full_ref, action),

	TP_STRUCT__entry(
		__field(	u64,  bytenr		)
		__field(	u64,  num_bytes		)
		__field(	int,  action		)
		__field(	u64,  parent		)
		__field(	u64,  ref_root		)
		__field(	int,  level		)
		__field(	int,  type		)
		__field(	u64,  seq		)
	),

	TP_fast_assign(
		__entry->bytenr		= ref->bytenr;
		__entry->num_bytes	= ref->num_bytes;
		__entry->action		= action;
		__entry->parent		= full_ref->parent;
		__entry->ref_root	= full_ref->root;
		__entry->level		= full_ref->level;
		__entry->type		= ref->type;
		__entry->seq		= ref->seq;
	),

	TP_printk("bytenr = %llu, num_bytes = %llu, action = %s, "
		  "parent = %llu(%s), ref_root = %llu(%s), level = %d, "
		  "type = %s, seq = %llu",
		  (unsigned long long)__entry->bytenr,
		  (unsigned long long)__entry->num_bytes,
		  show_ref_action(__entry->action),
		  show_root_type(__entry->parent),
		  show_root_type(__entry->ref_root),
		  __entry->level, show_ref_type(__entry->type),
		  (unsigned long long)__entry->seq)
);

TRACE_EVENT(btrfs_delayed_data_ref,

	TP_PROTO(struct btrfs_delayed_ref_node *ref,
		 struct btrfs_delayed_data_ref *full_ref,
		 int action),

	TP_ARGS(ref, full_ref, action),

	TP_STRUCT__entry(
		__field(	u64,  bytenr		)
		__field(	u64,  num_bytes		)
		__field(	int,  action		)
		__field(	u64,  parent		)
		__field(	u64,  ref_root		)
		__field(	u64,  owner		)
		__field(	u64,  offset		)
		__field(	int,  type		)
		__field(	u64,  seq		)
	),

	TP_fast_assign(
		__entry->bytenr		= ref->bytenr;
		__entry->num_bytes	= ref->num_bytes;
		__entry->action		= action;
		__entry->parent		= full_ref->parent;
		__entry->ref_root	= full_ref->root;
		__entry->owner		= full_ref->objectid;
		__entry->offset		= full_ref->offset;
		__entry->type		= ref->type;
		__entry->seq		= ref->seq;
	),

	TP_printk("bytenr = %llu, num_bytes = %llu, action = %s, "
		  "parent = %llu(%s), ref_root = %llu(%s), owner = %llu, "
		  "offset = %llu, type = %s, seq = %llu",
		  (unsigned long long)__entry->bytenr,
		  (unsigned long long)__entry->num_bytes,
		  show_ref_action(__entry->action),
		  show_root_type(__entry->parent),
		  show_root_type(__entry->ref_root),
		  (unsigned long long)__entry->owner,
		  (unsigned long long)__entry->offset,
		  show_ref_type(__entry->type),
		  (unsigned long long)__entry->seq)
);

TRACE_EVENT(btrfs_delayed_ref_head,

	TP_PROTO(struct btrfs_delayed_ref_node *ref,
		 struct btrfs_delayed_ref_head *head_ref,
		 int action),

	TP_ARGS(ref, head_ref, action),

	TP_STRUCT__entry(
		__field(	u64,  bytenr		)
		__field(	u64,  num_bytes		)
		__field(	int,  action		)
		__field(	int,  is_data		)
	),

	TP_fast_assign(
		__entry->bytenr		= ref->bytenr;
		__entry->num_bytes	= ref->num_bytes;
		__entry->action		= action;
		__entry->is_data	= head_ref->is_data;
	),

	TP_printk("bytenr = %llu, num_bytes = %llu, action = %s, is_data = %d",
		  (unsigned long long)__entry->bytenr,
		  (unsigned long long)__entry->num_bytes,
		  show_ref_action(__entry->action),
		  __entry->is_data)
);

#define show_chunk_type(type)					\
	__print_flags(type, "|",				\
		{ BTRFS_BLOCK_GROUP_DATA, 	"DATA"	},	\
		{ BTRFS_BLOCK_GROUP_SYSTEM, 	"SYSTEM"},	\
		{ BTRFS_BLOCK_GROUP_METADATA, 	"METADATA"},	\
		{ BTRFS_BLOCK_GROUP_RAID0, 	"RAID0" },	\
		{ BTRFS_BLOCK_GROUP_RAID1, 	"RAID1" },	\
		{ BTRFS_BLOCK_GROUP_DUP, 	"DUP"	},	\
		{ BTRFS_BLOCK_GROUP_RAID10, 	"RAID10"})

DECLARE_EVENT_CLASS(btrfs__chunk,

	TP_PROTO(struct btrfs_root *root, struct map_lookup *map,
		 u64 offset, u64 size),

	TP_ARGS(root, map, offset, size),

	TP_STRUCT__entry(
		__field(	int,  num_stripes		)
		__field(	u64,  type			)
		__field(	int,  sub_stripes		)
		__field(	u64,  offset			)
		__field(	u64,  size			)
		__field(	u64,  root_objectid		)
	),

	TP_fast_assign(
		__entry->num_stripes	= map->num_stripes;
		__entry->type		= map->type;
		__entry->sub_stripes	= map->sub_stripes;
		__entry->offset		= offset;
		__entry->size		= size;
		__entry->root_objectid	= root->root_key.objectid;
	),

	TP_printk("root = %llu(%s), offset = %llu, size = %llu, "
		  "num_stripes = %d, sub_stripes = %d, type = %s",
		  show_root_type(__entry->root_objectid),
		  (unsigned long long)__entry->offset,
		  (unsigned long long)__entry->size,
		  __entry->num_stripes, __entry->sub_stripes,
		  show_chunk_type(__entry->type))
);

DEFINE_EVENT(btrfs__chunk,  btrfs_chunk_alloc,

	TP_PROTO(struct btrfs_root *root, struct map_lookup *map,
		 u64 offset, u64 size),

	TP_ARGS(root, map, offset, size)
);

DEFINE_EVENT(btrfs__chunk,  btrfs_chunk_free,

	TP_PROTO(struct btrfs_root *root, struct map_lookup *map,
		 u64 offset, u64 size),

	TP_ARGS(root, map, offset, size)
);

TRACE_EVENT(btrfs_cow_block,

	TP_PROTO(struct btrfs_root *root, struct extent_buffer *buf,
		 struct extent_buffer *cow),

	TP_ARGS(root, buf, cow),

	TP_STRUCT__entry(
		__field(	u64,  root_objectid		)
		__field(	u64,  buf_start			)
		__field(	int,  refs			)
		__field(	u64,  cow_start			)
		__field(	int,  buf_level			)
		__field(	int,  cow_level			)
	),

	TP_fast_assign(
		__entry->root_objectid	= root->root_key.objectid;
		__entry->buf_start	= buf->start;
		__entry->refs		= atomic_read(&buf->refs);
		__entry->cow_start	= cow->start;
		__entry->buf_level	= btrfs_header_level(buf);
		__entry->cow_level	= btrfs_header_level(cow);
	),

	TP_printk("root = %llu(%s), refs = %d, orig_buf = %llu "
		  "(orig_level = %d), cow_buf = %llu (cow_level = %d)",
		  show_root_type(__entry->root_objectid),
		  __entry->refs,
		  (unsigned long long)__entry->buf_start,
		  __entry->buf_level,
		  (unsigned long long)__entry->cow_start,
		  __entry->cow_level)
);

TRACE_EVENT(btrfs_space_reservation,

	TP_PROTO(struct btrfs_fs_info *fs_info, char *type, u64 val,
		 u64 bytes, int reserve),

	TP_ARGS(fs_info, type, val, bytes, reserve),

	TP_STRUCT__entry(
		__array(	u8,	fsid,	BTRFS_UUID_SIZE	)
		__string(	type,	type			)
		__field(	u64,	val			)
		__field(	u64,	bytes			)
		__field(	int,	reserve			)
	),

	TP_fast_assign(
		memcpy(__entry->fsid, fs_info->fsid, BTRFS_UUID_SIZE);
		__assign_str(type, type);
		__entry->val		= val;
		__entry->bytes		= bytes;
		__entry->reserve	= reserve;
	),

	TP_printk("%pU: %s: %Lu %s %Lu", __entry->fsid, __get_str(type),
		  __entry->val, __entry->reserve ? "reserve" : "release",
		  __entry->bytes)
);

DECLARE_EVENT_CLASS(btrfs__reserved_extent,

	TP_PROTO(struct btrfs_root *root, u64 start, u64 len),

	TP_ARGS(root, start, len),

	TP_STRUCT__entry(
		__field(	u64,  root_objectid		)
		__field(	u64,  start			)
		__field(	u64,  len			)
	),

	TP_fast_assign(
		__entry->root_objectid	= root->root_key.objectid;
		__entry->start		= start;
		__entry->len		= len;
	),

	TP_printk("root = %llu(%s), start = %llu, len = %llu",
		  show_root_type(__entry->root_objectid),
		  (unsigned long long)__entry->start,
		  (unsigned long long)__entry->len)
);

DEFINE_EVENT(btrfs__reserved_extent,  btrfs_reserved_extent_alloc,

	TP_PROTO(struct btrfs_root *root, u64 start, u64 len),

	TP_ARGS(root, start, len)
);

DEFINE_EVENT(btrfs__reserved_extent,  btrfs_reserved_extent_free,

	TP_PROTO(struct btrfs_root *root, u64 start, u64 len),

	TP_ARGS(root, start, len)
);

TRACE_EVENT(find_free_extent,

	TP_PROTO(struct btrfs_root *root, u64 num_bytes, u64 empty_size,
		 u64 data),

	TP_ARGS(root, num_bytes, empty_size, data),

	TP_STRUCT__entry(
		__field(	u64,	root_objectid		)
		__field(	u64,	num_bytes		)
		__field(	u64,	empty_size		)
		__field(	u64,	data			)
	),

	TP_fast_assign(
		__entry->root_objectid	= root->root_key.objectid;
		__entry->num_bytes	= num_bytes;
		__entry->empty_size	= empty_size;
		__entry->data		= data;
	),

	TP_printk("root = %Lu(%s), len = %Lu, empty_size = %Lu, "
		  "flags = %Lu(%s)", show_root_type(__entry->root_objectid),
		  __entry->num_bytes, __entry->empty_size, __entry->data,
		  __print_flags((unsigned long)__entry->data, "|",
				 BTRFS_GROUP_FLAGS))
);

DECLARE_EVENT_CLASS(btrfs__reserve_extent,

	TP_PROTO(struct btrfs_root *root,
		 struct btrfs_block_group_cache *block_group, u64 start,
		 u64 len),

	TP_ARGS(root, block_group, start, len),

	TP_STRUCT__entry(
		__field(	u64,	root_objectid		)
		__field(	u64,	bg_objectid		)
		__field(	u64,	flags			)
		__field(	u64,	start			)
		__field(	u64,	len			)
	),

	TP_fast_assign(
		__entry->root_objectid	= root->root_key.objectid;
		__entry->bg_objectid	= block_group->key.objectid;
		__entry->flags		= block_group->flags;
		__entry->start		= start;
		__entry->len		= len;
	),

	TP_printk("root = %Lu(%s), block_group = %Lu, flags = %Lu(%s), "
		  "start = %Lu, len = %Lu",
		  show_root_type(__entry->root_objectid), __entry->bg_objectid,
		  __entry->flags, __print_flags((unsigned long)__entry->flags,
						"|", BTRFS_GROUP_FLAGS),
		  __entry->start, __entry->len)
);

DEFINE_EVENT(btrfs__reserve_extent, btrfs_reserve_extent,

	TP_PROTO(struct btrfs_root *root,
		 struct btrfs_block_group_cache *block_group, u64 start,
		 u64 len),

	TP_ARGS(root, block_group, start, len)
);

DEFINE_EVENT(btrfs__reserve_extent, btrfs_reserve_extent_cluster,

	TP_PROTO(struct btrfs_root *root,
		 struct btrfs_block_group_cache *block_group, u64 start,
		 u64 len),

	TP_ARGS(root, block_group, start, len)
);

TRACE_EVENT(btrfs_find_cluster,

	TP_PROTO(struct btrfs_block_group_cache *block_group, u64 start,
		 u64 bytes, u64 empty_size, u64 min_bytes),

	TP_ARGS(block_group, start, bytes, empty_size, min_bytes),

	TP_STRUCT__entry(
		__field(	u64,	bg_objectid		)
		__field(	u64,	flags			)
		__field(	u64,	start			)
		__field(	u64,	bytes			)
		__field(	u64,	empty_size		)
		__field(	u64,	min_bytes		)
	),

	TP_fast_assign(
		__entry->bg_objectid	= block_group->key.objectid;
		__entry->flags		= block_group->flags;
		__entry->start		= start;
		__entry->bytes		= bytes;
		__entry->empty_size	= empty_size;
		__entry->min_bytes	= min_bytes;
	),

	TP_printk("block_group = %Lu, flags = %Lu(%s), start = %Lu, len = %Lu,"
		  " empty_size = %Lu, min_bytes = %Lu", __entry->bg_objectid,
		  __entry->flags,
		  __print_flags((unsigned long)__entry->flags, "|",
				BTRFS_GROUP_FLAGS), __entry->start,
		  __entry->bytes, __entry->empty_size,  __entry->min_bytes)
);

TRACE_EVENT(btrfs_failed_cluster_setup,

	TP_PROTO(struct btrfs_block_group_cache *block_group),

	TP_ARGS(block_group),

	TP_STRUCT__entry(
		__field(	u64,	bg_objectid		)
	),

	TP_fast_assign(
		__entry->bg_objectid	= block_group->key.objectid;
	),

	TP_printk("block_group = %Lu", __entry->bg_objectid)
);

TRACE_EVENT(btrfs_setup_cluster,

	TP_PROTO(struct btrfs_block_group_cache *block_group,
		 struct btrfs_free_cluster *cluster, u64 size, int bitmap),

	TP_ARGS(block_group, cluster, size, bitmap),

	TP_STRUCT__entry(
		__field(	u64,	bg_objectid		)
		__field(	u64,	flags			)
		__field(	u64,	start			)
		__field(	u64,	max_size		)
		__field(	u64,	size			)
		__field(	int,	bitmap			)
	),

	TP_fast_assign(
		__entry->bg_objectid	= block_group->key.objectid;
		__entry->flags		= block_group->flags;
		__entry->start		= cluster->window_start;
		__entry->max_size	= cluster->max_size;
		__entry->size		= size;
		__entry->bitmap		= bitmap;
	),

	TP_printk("block_group = %Lu, flags = %Lu(%s), window_start = %Lu, "
		  "size = %Lu, max_size = %Lu, bitmap = %d",
		  __entry->bg_objectid,
		  __entry->flags,
		  __print_flags((unsigned long)__entry->flags, "|",
				BTRFS_GROUP_FLAGS), __entry->start,
		  __entry->size, __entry->max_size, __entry->bitmap)
);

struct extent_state;
TRACE_EVENT(alloc_extent_state,

	TP_PROTO(struct extent_state *state, gfp_t mask, unsigned long IP),

	TP_ARGS(state, mask, IP),

	TP_STRUCT__entry(
		__field(struct extent_state *, state)
		__field(gfp_t, mask)
		__field(unsigned long, ip)
	),

	TP_fast_assign(
		__entry->state	= state,
		__entry->mask	= mask,
		__entry->ip	= IP
	),

	TP_printk("state=%p; mask = %s; caller = %pF", __entry->state,
		  show_gfp_flags(__entry->mask), (void *)__entry->ip)
);

TRACE_EVENT(free_extent_state,

	TP_PROTO(struct extent_state *state, unsigned long IP),

	TP_ARGS(state, IP),

	TP_STRUCT__entry(
		__field(struct extent_state *, state)
		__field(unsigned long, ip)
	),

	TP_fast_assign(
		__entry->state	= state,
		__entry->ip = IP
	),

	TP_printk(" state=%p; caller = %pF", __entry->state,
		  (void *)__entry->ip)
);

#endif /* _TRACE_BTRFS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
