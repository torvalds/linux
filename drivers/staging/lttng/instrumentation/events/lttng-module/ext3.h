#undef TRACE_SYSTEM
#define TRACE_SYSTEM ext3

#if !defined(_TRACE_EXT3_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_EXT3_H

#include <linux/tracepoint.h>
#include <linux/version.h>

TRACE_EVENT(ext3_free_inode,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	umode_t, mode			)
		__field(	uid_t,	uid			)
		__field(	gid_t,	gid			)
		__field(	blkcnt_t, blocks		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(mode, inode->i_mode)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0))
		tp_assign(uid, i_uid_read(inode))
		tp_assign(gid, i_gid_read(inode))
#else
		tp_assign(uid, inode->i_uid)
		tp_assign(gid, inode->i_gid)
#endif
		tp_assign(blocks, inode->i_blocks)
	),

	TP_printk("dev %d,%d ino %lu mode 0%o uid %u gid %u blocks %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->mode, __entry->uid, __entry->gid,
		  (unsigned long) __entry->blocks)
)

TRACE_EVENT(ext3_request_inode,
	TP_PROTO(struct inode *dir, int mode),

	TP_ARGS(dir, mode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	dir			)
		__field(	umode_t, mode			)
	),

	TP_fast_assign(
		tp_assign(dev, dir->i_sb->s_dev)
		tp_assign(dir, dir->i_ino)
		tp_assign(mode, mode)
	),

	TP_printk("dev %d,%d dir %lu mode 0%o",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->dir, __entry->mode)
)

TRACE_EVENT(ext3_allocate_inode,
	TP_PROTO(struct inode *inode, struct inode *dir, int mode),

	TP_ARGS(inode, dir, mode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	ino_t,	dir			)
		__field(	umode_t, mode			)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(dir, dir->i_ino)
		tp_assign(mode, mode)
	),

	TP_printk("dev %d,%d ino %lu dir %lu mode 0%o",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned long) __entry->dir, __entry->mode)
)

TRACE_EVENT(ext3_evict_inode,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	int,	nlink			)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(nlink, inode->i_nlink)
	),

	TP_printk("dev %d,%d ino %lu nlink %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->nlink)
)

TRACE_EVENT(ext3_drop_inode,
	TP_PROTO(struct inode *inode, int drop),

	TP_ARGS(inode, drop),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	int,	drop			)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(drop, drop)
	),

	TP_printk("dev %d,%d ino %lu drop %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->drop)
)

TRACE_EVENT(ext3_mark_inode_dirty,
	TP_PROTO(struct inode *inode, unsigned long IP),

	TP_ARGS(inode, IP),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(unsigned long,	ip			)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(ip, IP)
	),

	TP_printk("dev %d,%d ino %lu caller %pF",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, (void *)__entry->ip)
)

TRACE_EVENT(ext3_write_begin,
	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int flags),

	TP_ARGS(inode, pos, len, flags),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	loff_t,	pos			)
		__field(	unsigned int, len		)
		__field(	unsigned int, flags		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(pos, pos)
		tp_assign(len, len)
		tp_assign(flags, flags)
	),

	TP_printk("dev %d,%d ino %lu pos %llu len %u flags %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned long long) __entry->pos, __entry->len,
		  __entry->flags)
)

DECLARE_EVENT_CLASS(ext3__write_end,
	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
			unsigned int copied),

	TP_ARGS(inode, pos, len, copied),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	loff_t,	pos			)
		__field(	unsigned int, len		)
		__field(	unsigned int, copied		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(pos, pos)
		tp_assign(len, len)
		tp_assign(copied, copied)
	),

	TP_printk("dev %d,%d ino %lu pos %llu len %u copied %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned long long) __entry->pos, __entry->len,
		  __entry->copied)
)

DEFINE_EVENT(ext3__write_end, ext3_ordered_write_end,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int copied),

	TP_ARGS(inode, pos, len, copied)
)

DEFINE_EVENT(ext3__write_end, ext3_writeback_write_end,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int copied),

	TP_ARGS(inode, pos, len, copied)
)

DEFINE_EVENT(ext3__write_end, ext3_journalled_write_end,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int copied),

	TP_ARGS(inode, pos, len, copied)
)

DECLARE_EVENT_CLASS(ext3__page_op,
	TP_PROTO(struct page *page),

	TP_ARGS(page),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	pgoff_t, index			)

	),

	TP_fast_assign(
		tp_assign(index, page->index)
		tp_assign(ino, page->mapping->host->i_ino)
		tp_assign(dev, page->mapping->host->i_sb->s_dev)
	),

	TP_printk("dev %d,%d ino %lu page_index %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->index)
)

DEFINE_EVENT(ext3__page_op, ext3_ordered_writepage,

	TP_PROTO(struct page *page),

	TP_ARGS(page)
)

DEFINE_EVENT(ext3__page_op, ext3_writeback_writepage,

	TP_PROTO(struct page *page),

	TP_ARGS(page)
)

DEFINE_EVENT(ext3__page_op, ext3_journalled_writepage,

	TP_PROTO(struct page *page),

	TP_ARGS(page)
)

DEFINE_EVENT(ext3__page_op, ext3_readpage,

	TP_PROTO(struct page *page),

	TP_ARGS(page)
)

DEFINE_EVENT(ext3__page_op, ext3_releasepage,

	TP_PROTO(struct page *page),

	TP_ARGS(page)
)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0))

TRACE_EVENT(ext3_invalidatepage,
	TP_PROTO(struct page *page, unsigned int offset, unsigned int length),

	TP_ARGS(page, offset, length),

	TP_STRUCT__entry(
		__field(	pgoff_t, index			)
		__field(	unsigned int, offset		)
		__field(	unsigned int, length		)
		__field(	ino_t,	ino			)
		__field(	dev_t,	dev			)

	),

	TP_fast_assign(
		tp_assign(index, page->index)
		tp_assign(offset, offset)
		tp_assign(length, length)
		tp_assign(ino, page->mapping->host->i_ino)
		tp_assign(dev, page->mapping->host->i_sb->s_dev)
	),

	TP_printk("dev %d,%d ino %lu page_index %lu offset %u length %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->index, __entry->offset, __entry->length)
)

#else

TRACE_EVENT(ext3_invalidatepage,
	TP_PROTO(struct page *page, unsigned long offset),

	TP_ARGS(page, offset),

	TP_STRUCT__entry(
		__field(	pgoff_t, index			)
		__field(	unsigned long, offset		)
		__field(	ino_t,	ino			)
		__field(	dev_t,	dev			)

	),

	TP_fast_assign(
		tp_assign(index, page->index)
		tp_assign(offset, offset)
		tp_assign(ino, page->mapping->host->i_ino)
		tp_assign(dev, page->mapping->host->i_sb->s_dev)
	),

	TP_printk("dev %d,%d ino %lu page_index %lu offset %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->index, __entry->offset)
)

#endif

TRACE_EVENT(ext3_discard_blocks,
	TP_PROTO(struct super_block *sb, unsigned long blk,
			unsigned long count),

	TP_ARGS(sb, blk, count),

	TP_STRUCT__entry(
		__field(	dev_t,		dev		)
		__field(	unsigned long,	blk		)
		__field(	unsigned long,	count		)

	),

	TP_fast_assign(
		tp_assign(dev, sb->s_dev)
		tp_assign(blk, blk)
		tp_assign(count, count)
	),

	TP_printk("dev %d,%d blk %lu count %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->blk, __entry->count)
)

TRACE_EVENT(ext3_request_blocks,
	TP_PROTO(struct inode *inode, unsigned long goal,
		 unsigned long count),

	TP_ARGS(inode, goal, count),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	unsigned long, count		)
		__field(	unsigned long,	goal		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(count, count)
		tp_assign(goal, goal)
	),

	TP_printk("dev %d,%d ino %lu count %lu goal %lu ",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->count, __entry->goal)
)

TRACE_EVENT(ext3_allocate_blocks,
	TP_PROTO(struct inode *inode, unsigned long goal,
		 unsigned long count, unsigned long block),

	TP_ARGS(inode, goal, count, block),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	unsigned long,	block		)
		__field(	unsigned long, count		)
		__field(	unsigned long,	goal		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(block, block)
		tp_assign(count, count)
		tp_assign(goal, goal)
	),

	TP_printk("dev %d,%d ino %lu count %lu block %lu goal %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		   __entry->count, __entry->block,
		  __entry->goal)
)

TRACE_EVENT(ext3_free_blocks,
	TP_PROTO(struct inode *inode, unsigned long block,
		 unsigned long count),

	TP_ARGS(inode, block, count),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	umode_t, mode			)
		__field(	unsigned long,	block		)
		__field(	unsigned long,	count		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(mode, inode->i_mode)
		tp_assign(block, block)
		tp_assign(count, count)
	),

	TP_printk("dev %d,%d ino %lu mode 0%o block %lu count %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->mode, __entry->block, __entry->count)
)

TRACE_EVENT(ext3_sync_file_enter,
	TP_PROTO(struct file *file, int datasync),

	TP_ARGS(file, datasync),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	ino_t,	parent			)
		__field(	int,	datasync		)
	),

	TP_fast_assign(
		tp_assign(dev, file->f_path.dentry->d_inode->i_sb->s_dev)
		tp_assign(ino, file->f_path.dentry->d_inode->i_ino)
		tp_assign(datasync, datasync)
		tp_assign(parent, file->f_path.dentry->d_parent->d_inode->i_ino)
	),

	TP_printk("dev %d,%d ino %lu parent %ld datasync %d ",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned long) __entry->parent, __entry->datasync)
)

TRACE_EVENT(ext3_sync_file_exit,
	TP_PROTO(struct inode *inode, int ret),

	TP_ARGS(inode, ret),

	TP_STRUCT__entry(
		__field(	int,	ret			)
		__field(	ino_t,	ino			)
		__field(	dev_t,	dev			)
	),

	TP_fast_assign(
		tp_assign(ret, ret)
		tp_assign(ino, inode->i_ino)
		tp_assign(dev, inode->i_sb->s_dev)
	),

	TP_printk("dev %d,%d ino %lu ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->ret)
)

TRACE_EVENT(ext3_sync_fs,
	TP_PROTO(struct super_block *sb, int wait),

	TP_ARGS(sb, wait),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	int,	wait			)

	),

	TP_fast_assign(
		tp_assign(dev, sb->s_dev)
		tp_assign(wait, wait)
	),

	TP_printk("dev %d,%d wait %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->wait)
)

TRACE_EVENT(ext3_rsv_window_add,
	TP_PROTO(struct super_block *sb,
		 struct ext3_reserve_window_node *rsv_node),

	TP_ARGS(sb, rsv_node),

	TP_STRUCT__entry(
		__field(	unsigned long,	start		)
		__field(	unsigned long,	end		)
		__field(	dev_t,	dev			)
	),

	TP_fast_assign(
		tp_assign(dev, sb->s_dev)
		tp_assign(start, rsv_node->rsv_window._rsv_start)
		tp_assign(end, rsv_node->rsv_window._rsv_end)
	),

	TP_printk("dev %d,%d start %lu end %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->start, __entry->end)
)

TRACE_EVENT(ext3_discard_reservation,
	TP_PROTO(struct inode *inode,
		 struct ext3_reserve_window_node *rsv_node),

	TP_ARGS(inode, rsv_node),

	TP_STRUCT__entry(
		__field(	unsigned long,	start		)
		__field(	unsigned long,	end		)
		__field(	ino_t,	ino			)
		__field(	dev_t,	dev			)
	),

	TP_fast_assign(
		tp_assign(start, rsv_node->rsv_window._rsv_start)
		tp_assign(end, rsv_node->rsv_window._rsv_end)
		tp_assign(ino, inode->i_ino)
		tp_assign(dev, inode->i_sb->s_dev)
	),

	TP_printk("dev %d,%d ino %lu start %lu end %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long)__entry->ino, __entry->start,
		  __entry->end)
)

TRACE_EVENT(ext3_alloc_new_reservation,
	TP_PROTO(struct super_block *sb, unsigned long goal),

	TP_ARGS(sb, goal),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	unsigned long,	goal		)
	),

	TP_fast_assign(
		tp_assign(dev, sb->s_dev)
		tp_assign(goal, goal)
	),

	TP_printk("dev %d,%d goal %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->goal)
)

TRACE_EVENT(ext3_reserved,
	TP_PROTO(struct super_block *sb, unsigned long block,
		 struct ext3_reserve_window_node *rsv_node),

	TP_ARGS(sb, block, rsv_node),

	TP_STRUCT__entry(
		__field(	unsigned long,	block		)
		__field(	unsigned long,	start		)
		__field(	unsigned long,	end		)
		__field(	dev_t,	dev			)
	),

	TP_fast_assign(
		tp_assign(block, block)
		tp_assign(start, rsv_node->rsv_window._rsv_start)
		tp_assign(end, rsv_node->rsv_window._rsv_end)
		tp_assign(dev, sb->s_dev)
	),

	TP_printk("dev %d,%d block %lu, start %lu end %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->block, __entry->start, __entry->end)
)

TRACE_EVENT(ext3_forget,
	TP_PROTO(struct inode *inode, int is_metadata, unsigned long block),

	TP_ARGS(inode, is_metadata, block),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	umode_t, mode			)
		__field(	int,	is_metadata		)
		__field(	unsigned long,	block		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(mode, inode->i_mode)
		tp_assign(is_metadata, is_metadata)
		tp_assign(block, block)
	),

	TP_printk("dev %d,%d ino %lu mode 0%o is_metadata %d block %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->mode, __entry->is_metadata, __entry->block)
)

TRACE_EVENT(ext3_read_block_bitmap,
	TP_PROTO(struct super_block *sb, unsigned int group),

	TP_ARGS(sb, group),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	__u32,	group			)

	),

	TP_fast_assign(
		tp_assign(dev, sb->s_dev)
		tp_assign(group, group)
	),

	TP_printk("dev %d,%d group %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->group)
)

TRACE_EVENT(ext3_direct_IO_enter,
	TP_PROTO(struct inode *inode, loff_t offset, unsigned long len, int rw),

	TP_ARGS(inode, offset, len, rw),

	TP_STRUCT__entry(
		__field(	ino_t,	ino			)
		__field(	dev_t,	dev			)
		__field(	loff_t,	pos			)
		__field(	unsigned long,	len		)
		__field(	int,	rw			)
	),

	TP_fast_assign(
		tp_assign(ino, inode->i_ino)
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(pos, offset)
		tp_assign(len, len)
		tp_assign(rw, rw)
	),

	TP_printk("dev %d,%d ino %lu pos %llu len %lu rw %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned long long) __entry->pos, __entry->len,
		  __entry->rw)
)

TRACE_EVENT(ext3_direct_IO_exit,
	TP_PROTO(struct inode *inode, loff_t offset, unsigned long len,
		 int rw, int ret),

	TP_ARGS(inode, offset, len, rw, ret),

	TP_STRUCT__entry(
		__field(	ino_t,	ino			)
		__field(	dev_t,	dev			)
		__field(	loff_t,	pos			)
		__field(	unsigned long,	len		)
		__field(	int,	rw			)
		__field(	int,	ret			)
	),

	TP_fast_assign(
		tp_assign(ino, inode->i_ino)
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(pos, offset)
		tp_assign(len, len)
		tp_assign(rw, rw)
		tp_assign(ret, ret)
	),

	TP_printk("dev %d,%d ino %lu pos %llu len %lu rw %d ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned long long) __entry->pos, __entry->len,
		  __entry->rw, __entry->ret)
)

TRACE_EVENT(ext3_unlink_enter,
	TP_PROTO(struct inode *parent, struct dentry *dentry),

	TP_ARGS(parent, dentry),

	TP_STRUCT__entry(
		__field(	ino_t,	parent			)
		__field(	ino_t,	ino			)
		__field(	loff_t,	size			)
		__field(	dev_t,	dev			)
	),

	TP_fast_assign(
		tp_assign(parent, parent->i_ino)
		tp_assign(ino, dentry->d_inode->i_ino)
		tp_assign(size, dentry->d_inode->i_size)
		tp_assign(dev, dentry->d_inode->i_sb->s_dev)
	),

	TP_printk("dev %d,%d ino %lu size %lld parent %ld",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned long long)__entry->size,
		  (unsigned long) __entry->parent)
)

TRACE_EVENT(ext3_unlink_exit,
	TP_PROTO(struct dentry *dentry, int ret),

	TP_ARGS(dentry, ret),

	TP_STRUCT__entry(
		__field(	ino_t,	ino			)
		__field(	dev_t,	dev			)
		__field(	int,	ret			)
	),

	TP_fast_assign(
		tp_assign(ino, dentry->d_inode->i_ino)
		tp_assign(dev, dentry->d_inode->i_sb->s_dev)
		tp_assign(ret, ret)
	),

	TP_printk("dev %d,%d ino %lu ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->ret)
)

DECLARE_EVENT_CLASS(ext3__truncate,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	ino_t,		ino		)
		__field(	dev_t,		dev		)
		__field(	blkcnt_t,	blocks		)
	),

	TP_fast_assign(
		tp_assign(ino, inode->i_ino)
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(blocks, inode->i_blocks)
	),

	TP_printk("dev %d,%d ino %lu blocks %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, (unsigned long) __entry->blocks)
)

DEFINE_EVENT(ext3__truncate, ext3_truncate_enter,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
)

DEFINE_EVENT(ext3__truncate, ext3_truncate_exit,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
)

TRACE_EVENT(ext3_get_blocks_enter,
	TP_PROTO(struct inode *inode, unsigned long lblk,
		 unsigned long len, int create),

	TP_ARGS(inode, lblk, len, create),

	TP_STRUCT__entry(
		__field(	ino_t,		ino		)
		__field(	dev_t,		dev		)
		__field(	unsigned long,	lblk		)
		__field(	unsigned long,	len		)
		__field(	int,		create		)
	),

	TP_fast_assign(
		tp_assign(ino, inode->i_ino)
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(lblk, lblk)
		tp_assign(len, len)
		tp_assign(create, create)
	),

	TP_printk("dev %d,%d ino %lu lblk %lu len %lu create %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->lblk, __entry->len, __entry->create)
)

TRACE_EVENT(ext3_get_blocks_exit,
	TP_PROTO(struct inode *inode, unsigned long lblk,
		 unsigned long pblk, unsigned long len, int ret),

	TP_ARGS(inode, lblk, pblk, len, ret),

	TP_STRUCT__entry(
		__field(	ino_t,		ino		)
		__field(	dev_t,		dev		)
		__field(	unsigned long,	lblk		)
		__field(	unsigned long,	pblk		)
		__field(	unsigned long,	len		)
		__field(	int,		ret		)
	),

	TP_fast_assign(
		tp_assign(ino, inode->i_ino)
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(lblk, lblk)
		tp_assign(pblk, pblk)
		tp_assign(len, len)
		tp_assign(ret, ret)
	),

	TP_printk("dev %d,%d ino %lu lblk %lu pblk %lu len %lu ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		   __entry->lblk, __entry->pblk,
		  __entry->len, __entry->ret)
)

TRACE_EVENT(ext3_load_inode,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	ino_t,	ino		)
		__field(	dev_t,	dev		)
	),

	TP_fast_assign(
		tp_assign(ino, inode->i_ino)
		tp_assign(dev, inode->i_sb->s_dev)
	),

	TP_printk("dev %d,%d ino %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino)
)

#endif /* _TRACE_EXT3_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"
