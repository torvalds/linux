#undef TRACE_SYSTEM
#define TRACE_SYSTEM ext4

#if !defined(_TRACE_EXT4_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_EXT4_H

#include <linux/writeback.h>
#include <linux/tracepoint.h>
#include <linux/version.h>

#ifndef _TRACE_EXT4_DEF_
#define _TRACE_EXT4_DEF_
struct ext4_allocation_context;
struct ext4_allocation_request;
struct ext4_prealloc_space;
struct ext4_inode_info;
struct mpage_da_data;
struct ext4_map_blocks;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
struct ext4_extent;
#endif
#endif

#define EXT4_I(inode) (container_of(inode, struct ext4_inode_info, vfs_inode))
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0))
#define TP_MODE_T	__u16
#else
#define TP_MODE_T	umode_t
#endif

TRACE_EVENT(ext4_free_inode,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	uid_t,	uid			)
		__field(	gid_t,	gid			)
		__field(	__u64, blocks			)
		__field(	TP_MODE_T, mode			)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0))
		tp_assign(uid, i_uid_read(inode))
		tp_assign(gid, i_gid_read(inode))
#else
		tp_assign(uid, inode->i_uid)
		tp_assign(gid, inode->i_gid)
#endif
		tp_assign(blocks, inode->i_blocks)
		tp_assign(mode, inode->i_mode)
	),

	TP_printk("dev %d,%d ino %lu mode 0%o uid %u gid %u blocks %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->mode,
		  __entry->uid, __entry->gid, __entry->blocks)
)

TRACE_EVENT(ext4_request_inode,
	TP_PROTO(struct inode *dir, int mode),

	TP_ARGS(dir, mode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	dir			)
		__field(	TP_MODE_T, mode			)
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

TRACE_EVENT(ext4_allocate_inode,
	TP_PROTO(struct inode *inode, struct inode *dir, int mode),

	TP_ARGS(inode, dir, mode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	ino_t,	dir			)
		__field(	TP_MODE_T, mode			)
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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
TRACE_EVENT(ext4_evict_inode,
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

TRACE_EVENT(ext4_drop_inode,
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

TRACE_EVENT(ext4_mark_inode_dirty,
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

TRACE_EVENT(ext4_begin_ordered_truncate,
	TP_PROTO(struct inode *inode, loff_t new_size),

	TP_ARGS(inode, new_size),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	loff_t,	new_size		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(new_size, new_size)
	),

	TP_printk("dev %d,%d ino %lu new_size %lld",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->new_size)
)
#endif

DECLARE_EVENT_CLASS(ext4__write_begin,

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

	TP_printk("dev %d,%d ino %lu pos %lld len %u flags %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->pos, __entry->len, __entry->flags)
)

DEFINE_EVENT(ext4__write_begin, ext4_write_begin,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int flags),

	TP_ARGS(inode, pos, len, flags)
)

DEFINE_EVENT(ext4__write_begin, ext4_da_write_begin,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int flags),

	TP_ARGS(inode, pos, len, flags)
)

DECLARE_EVENT_CLASS(ext4__write_end,
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

	TP_printk("dev %d,%d ino %lu pos %lld len %u copied %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->pos, __entry->len, __entry->copied)
)

DEFINE_EVENT(ext4__write_end, ext4_ordered_write_end,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int copied),

	TP_ARGS(inode, pos, len, copied)
)

DEFINE_EVENT(ext4__write_end, ext4_writeback_write_end,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int copied),

	TP_ARGS(inode, pos, len, copied)
)

DEFINE_EVENT(ext4__write_end, ext4_journalled_write_end,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int copied),

	TP_ARGS(inode, pos, len, copied)
)

DEFINE_EVENT(ext4__write_end, ext4_da_write_end,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int copied),

	TP_ARGS(inode, pos, len, copied)
)

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,40))
TRACE_EVENT(ext4_writepage,
	TP_PROTO(struct inode *inode, struct page *page),

	TP_ARGS(inode, page),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	pgoff_t, index			)

	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(index, page->index)
	),

	TP_printk("dev %d,%d ino %lu page_index %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->index)
)
#endif

TRACE_EVENT(ext4_da_writepages,
	TP_PROTO(struct inode *inode, struct writeback_control *wbc),

	TP_ARGS(inode, wbc),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	long,	nr_to_write		)
		__field(	long,	pages_skipped		)
		__field(	loff_t,	range_start		)
		__field(	loff_t,	range_end		)
		__field(       pgoff_t,	writeback_index		)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39))
		__field(	int,	sync_mode		)
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37))
		__field(	char,	nonblocking		)
#endif
		__field(	char,	for_kupdate		)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39))
		__field(	char,	for_reclaim		)
#endif
		__field(	char,	range_cyclic		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(nr_to_write, wbc->nr_to_write)
		tp_assign(pages_skipped, wbc->pages_skipped)
		tp_assign(range_start, wbc->range_start)
		tp_assign(range_end, wbc->range_end)
		tp_assign(writeback_index, inode->i_mapping->writeback_index)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39))
		tp_assign(sync_mode, wbc->sync_mode)
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37))
		tp_assign(nonblocking, wbc->nonblocking)
#endif
		tp_assign(for_kupdate, wbc->for_kupdate)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39))
		tp_assign(for_reclaim, wbc->for_reclaim)
#endif
		tp_assign(range_cyclic, wbc->range_cyclic)
	),

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39))
	TP_printk("dev %d,%d ino %lu nr_to_write %ld pages_skipped %ld "
		  "range_start %lld range_end %lld sync_mode %d "
		  "for_kupdate %d range_cyclic %d writeback_index %lu",
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	TP_printk("dev %d,%d ino %lu nr_to_write %ld pages_skipped %ld "
		  "range_start %llu range_end %llu "
		  "for_kupdate %d for_reclaim %d "
		  "range_cyclic %d writeback_index %lu",
#else
	TP_printk("dev %d,%d ino %lu nr_to_write %ld pages_skipped %ld "
		  "range_start %llu range_end %llu "
		  "nonblocking %d for_kupdate %d for_reclaim %d "
		  "range_cyclic %d writeback_index %lu",
#endif
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->nr_to_write,
		  __entry->pages_skipped, __entry->range_start,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39))
		  __entry->range_end, __entry->sync_mode,
		  __entry->for_kupdate, __entry->range_cyclic,
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
		  __entry->range_end,
		  __entry->for_kupdate, __entry->for_reclaim,
		  __entry->range_cyclic,
#else
		  __entry->range_end, __entry->nonblocking,
		  __entry->for_kupdate, __entry->for_reclaim,
		  __entry->range_cyclic,
#endif
		  (unsigned long) __entry->writeback_index)
)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0))

TRACE_EVENT(ext4_da_write_pages,
	TP_PROTO(struct inode *inode, pgoff_t first_page,
		 struct writeback_control *wbc),

	TP_ARGS(inode, first_page, wbc),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(      pgoff_t,	first_page		)
		__field(	 long,	nr_to_write		)
		__field(	  int,	sync_mode		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(first_page, first_page)
		tp_assign(nr_to_write, wbc->nr_to_write)
		tp_assign(sync_mode, wbc->sync_mode)
	),

	TP_printk("dev %d,%d ino %lu first_page %lu nr_to_write %ld "
		  "sync_mode %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->first_page,
		  __entry->nr_to_write, __entry->sync_mode)
)

#else

TRACE_EVENT(ext4_da_write_pages,
	TP_PROTO(struct inode *inode, struct mpage_da_data *mpd),

	TP_ARGS(inode, mpd),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u64,	b_blocknr		)
		__field(	__u32,	b_size			)
		__field(	__u32,	b_state			)
		__field(	unsigned long,	first_page	)
		__field(	int,	io_done			)
		__field(	int,	pages_written		)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39))
		__field(	int,	sync_mode		)
#endif
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(b_blocknr, mpd->b_blocknr)
		tp_assign(b_size, mpd->b_size)
		tp_assign(b_state, mpd->b_state)
		tp_assign(first_page, mpd->first_page)
		tp_assign(io_done, mpd->io_done)
		tp_assign(pages_written, mpd->pages_written)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39))
		tp_assign(sync_mode, mpd->wbc->sync_mode)
#endif
	),

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39))
	TP_printk("dev %d,%d ino %lu b_blocknr %llu b_size %u b_state 0x%04x "
		  "first_page %lu io_done %d pages_written %d sync_mode %d",
#else
	TP_printk("dev %d,%d ino %lu b_blocknr %llu b_size %u b_state 0x%04x "
		  "first_page %lu io_done %d pages_written %d",
#endif
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->b_blocknr, __entry->b_size,
		  __entry->b_state, __entry->first_page,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39))
		  __entry->io_done, __entry->pages_written,
		  __entry->sync_mode
#else
		  __entry->io_done, __entry->pages_written
#endif
                  )
)

#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0))

TRACE_EVENT(ext4_da_write_pages_extent,
	TP_PROTO(struct inode *inode, struct ext4_map_blocks *map),

	TP_ARGS(inode, map),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u64,	lblk			)
		__field(	__u32,	len			)
		__field(	__u32,	flags			)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(lblk, map->m_lblk)
		tp_assign(len, map->m_len)
		tp_assign(flags, map->m_flags)
	),

	TP_printk("dev %d,%d ino %lu lblk %llu len %u flags %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->lblk, __entry->len,
		  show_mflags(__entry->flags))
)

#endif

TRACE_EVENT(ext4_da_writepages_result,
	TP_PROTO(struct inode *inode, struct writeback_control *wbc,
			int ret, int pages_written),

	TP_ARGS(inode, wbc, ret, pages_written),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	int,	ret			)
		__field(	int,	pages_written		)
		__field(	long,	pages_skipped		)
		__field(       pgoff_t,	writeback_index		)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39))
		__field(	int,	sync_mode		)
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33))
		__field(	char,	encountered_congestion	)
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0))
		__field(	char,	more_io			)
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
		__field(	char,	no_nrwrite_index_update	)
#endif
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(ret, ret)
		tp_assign(pages_written, pages_written)
		tp_assign(pages_skipped, wbc->pages_skipped)
		tp_assign(writeback_index, inode->i_mapping->writeback_index)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39))
		tp_assign(sync_mode, wbc->sync_mode)
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33))
		tp_assign(encountered_congestion, wbc->encountered_congestion)
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0))
		tp_assign(more_io, wbc->more_io)
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
		tp_assign(no_nrwrite_index_update, wbc->no_nrwrite_index_update)
#endif
	),

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0))
	TP_printk("dev %d,%d ino %lu ret %d pages_written %d pages_skipped %ld "
		  "sync_mode %d writeback_index %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->ret,
		  __entry->pages_written, __entry->pages_skipped,
		  __entry->sync_mode,
		  (unsigned long) __entry->writeback_index)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39))
	TP_printk("dev %d,%d ino %lu ret %d pages_written %d pages_skipped %ld "
		  " more_io %d sync_mode %d writeback_index %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->ret,
		  __entry->pages_written, __entry->pages_skipped,
		  __entry->more_io, __entry->sync_mode,
		  (unsigned long) __entry->writeback_index)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
	TP_printk("dev %d,%d ino %lu ret %d pages_written %d pages_skipped %ld "
		  " more_io %d writeback_index %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->ret,
		  __entry->pages_written, __entry->pages_skipped,
		  __entry->more_io,
		  (unsigned long) __entry->writeback_index)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33))
	TP_printk("dev %d,%d ino %lu ret %d pages_written %d pages_skipped %ld "
		  " more_io %d no_nrwrite_index_update %d writeback_index %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->ret,
		  __entry->pages_written, __entry->pages_skipped,
		  __entry->more_io, __entry->no_nrwrite_index_update,
		  (unsigned long) __entry->writeback_index)
#else
	TP_printk("dev %d,%d ino %lu ret %d pages_written %d pages_skipped %ld "
		  " congestion %d"
		  " more_io %d no_nrwrite_index_update %d writeback_index %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->ret,
		  __entry->pages_written, __entry->pages_skipped,
		  __entry->encountered_congestion,
		  __entry->more_io, __entry->no_nrwrite_index_update,
		  (unsigned long) __entry->writeback_index)
#endif
)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39))
DECLARE_EVENT_CLASS(ext4__page_op,
	TP_PROTO(struct page *page),

	TP_ARGS(page),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	pgoff_t, index			)

	),

	TP_fast_assign(
		tp_assign(dev, page->mapping->host->i_sb->s_dev)
		tp_assign(ino, page->mapping->host->i_ino)
		tp_assign(index, page->index)
	),

	TP_printk("dev %d,%d ino %lu page_index %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned long) __entry->index)
)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,40))
DEFINE_EVENT(ext4__page_op, ext4_writepage,

	TP_PROTO(struct page *page),

	TP_ARGS(page)
)
#endif

DEFINE_EVENT(ext4__page_op, ext4_readpage,

	TP_PROTO(struct page *page),

	TP_ARGS(page)
)

DEFINE_EVENT(ext4__page_op, ext4_releasepage,

	TP_PROTO(struct page *page),

	TP_ARGS(page)
)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0))

DECLARE_EVENT_CLASS(ext4_invalidatepage_op,
	TP_PROTO(struct page *page, unsigned int offset, unsigned int length),

	TP_ARGS(page, offset, length),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	pgoff_t, index			)
		__field(	unsigned int, offset		)
		__field(	unsigned int, length		)
	),

	TP_fast_assign(
		tp_assign(dev, page->mapping->host->i_sb->s_dev)
		tp_assign(ino, page->mapping->host->i_ino)
		tp_assign(index, page->index)
		tp_assign(offset, offset)
		tp_assign(length, length)
	),

	TP_printk("dev %d,%d ino %lu page_index %lu offset %u length %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned long) __entry->index,
		  __entry->offset, __entry->length)
)

DEFINE_EVENT(ext4_invalidatepage_op, ext4_invalidatepage,
	TP_PROTO(struct page *page, unsigned int offset, unsigned int length),

	TP_ARGS(page, offset, length)
)

DEFINE_EVENT(ext4_invalidatepage_op, ext4_journalled_invalidatepage,
	TP_PROTO(struct page *page, unsigned int offset, unsigned int length),

	TP_ARGS(page, offset, length)
)

#else

TRACE_EVENT(ext4_invalidatepage,
	TP_PROTO(struct page *page, unsigned long offset),

	TP_ARGS(page, offset),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	pgoff_t, index			)
		__field(	unsigned long, offset		)

	),

	TP_fast_assign(
		tp_assign(dev, page->mapping->host->i_sb->s_dev)
		tp_assign(ino, page->mapping->host->i_ino)
		tp_assign(index, page->index)
		tp_assign(offset, offset)
	),

	TP_printk("dev %d,%d ino %lu page_index %lu offset %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned long) __entry->index, __entry->offset)
)

#endif

#endif

TRACE_EVENT(ext4_discard_blocks,
	TP_PROTO(struct super_block *sb, unsigned long long blk,
			unsigned long long count),

	TP_ARGS(sb, blk, count),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	__u64,	blk			)
		__field(	__u64,	count			)

	),

	TP_fast_assign(
		tp_assign(dev, sb->s_dev)
		tp_assign(blk, blk)
		tp_assign(count, count)
	),

	TP_printk("dev %d,%d blk %llu count %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->blk, __entry->count)
)

DECLARE_EVENT_CLASS(ext4__mb_new_pa,
	TP_PROTO(struct ext4_allocation_context *ac,
		 struct ext4_prealloc_space *pa),

	TP_ARGS(ac, pa),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u64,	pa_pstart		)
		__field(	__u64,	pa_lstart		)
		__field(	__u32,	pa_len			)

	),

	TP_fast_assign(
		tp_assign(dev, ac->ac_sb->s_dev)
		tp_assign(ino, ac->ac_inode->i_ino)
		tp_assign(pa_pstart, pa->pa_pstart)
		tp_assign(pa_lstart, pa->pa_lstart)
		tp_assign(pa_len, pa->pa_len)
	),

	TP_printk("dev %d,%d ino %lu pstart %llu len %u lstart %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->pa_pstart, __entry->pa_len, __entry->pa_lstart)
)

DEFINE_EVENT(ext4__mb_new_pa, ext4_mb_new_inode_pa,

	TP_PROTO(struct ext4_allocation_context *ac,
		 struct ext4_prealloc_space *pa),

	TP_ARGS(ac, pa)
)

DEFINE_EVENT(ext4__mb_new_pa, ext4_mb_new_group_pa,

	TP_PROTO(struct ext4_allocation_context *ac,
		 struct ext4_prealloc_space *pa),

	TP_ARGS(ac, pa)
)

TRACE_EVENT(ext4_mb_release_inode_pa,
	TP_PROTO(
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,40))
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
		 struct super_block *sb,
		 struct inode *inode,
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36))
		 struct super_block *sb,
		 struct ext4_allocation_context *ac,
#else
		 struct ext4_allocation_context *ac,
#endif
#endif
		 struct ext4_prealloc_space *pa,
		 unsigned long long block, unsigned int count),

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,40))
	TP_ARGS(pa, block, count),
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	TP_ARGS(sb, inode, pa, block, count),
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36))
	TP_ARGS(sb, ac, pa, block, count),
#else
	TP_ARGS(ac, pa, block, count),
#endif

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u64,	block			)
		__field(	__u32,	count			)

	),

	TP_fast_assign(
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,40))
		tp_assign(dev, pa->pa_inode->i_sb->s_dev)
		tp_assign(ino, pa->pa_inode->i_ino)
#else
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36))
		tp_assign(dev, sb->s_dev)
#else
		tp_assign(dev, ac->ac_sb->s_dev)
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
		tp_assign(ino, inode->i_ino)
#else
		tp_assign(ino, (ac && ac->ac_inode) ? ac->ac_inode->i_ino : 0)
#endif
#endif
		tp_assign(block, block)
		tp_assign(count, count)
	),

	TP_printk("dev %d,%d ino %lu block %llu count %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->block, __entry->count)
)

TRACE_EVENT(ext4_mb_release_group_pa,

#if (LTTNG_KERNEL_RANGE(2,6,40, 3,3,0))
	TP_PROTO(struct ext4_prealloc_space *pa),

	TP_ARGS(pa),
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	TP_PROTO(struct super_block *sb, struct ext4_prealloc_space *pa),

	TP_ARGS(sb, pa),
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36))
	TP_PROTO(struct super_block *sb,
		 struct ext4_allocation_context *ac,
		 struct ext4_prealloc_space *pa),

	TP_ARGS(sb, ac, pa),
#else
	TP_PROTO(struct ext4_allocation_context *ac,
		 struct ext4_prealloc_space *pa),

	TP_ARGS(ac, pa),
#endif

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37))
		__field(	ino_t,	ino			)
#endif
		__field(	__u64,	pa_pstart		)
		__field(	__u32,	pa_len			)

	),

	TP_fast_assign(
#if (LTTNG_KERNEL_RANGE(2,6,40, 3,3,0))
		tp_assign(dev, pa->pa_inode->i_sb->s_dev)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36))
		tp_assign(dev, sb->s_dev)
#else
		tp_assign(dev, ac->ac_sb->s_dev)
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37))
		tp_assign(ino, (ac && ac->ac_inode) ? ac->ac_inode->i_ino : 0)
#endif
		tp_assign(pa_pstart, pa->pa_pstart)
		tp_assign(pa_len, pa->pa_len)
	),

	TP_printk("dev %d,%d pstart %llu len %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->pa_pstart, __entry->pa_len)
)

TRACE_EVENT(ext4_discard_preallocations,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)

	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
	),

	TP_printk("dev %d,%d ino %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino)
)

TRACE_EVENT(ext4_mb_discard_preallocations,
	TP_PROTO(struct super_block *sb, int needed),

	TP_ARGS(sb, needed),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	int,	needed			)

	),

	TP_fast_assign(
		tp_assign(dev, sb->s_dev)
		tp_assign(needed, needed)
	),

	TP_printk("dev %d,%d needed %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->needed)
)

TRACE_EVENT(ext4_request_blocks,
	TP_PROTO(struct ext4_allocation_request *ar),

	TP_ARGS(ar),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	unsigned int, len		)
		__field(	__u32,  logical			)
		__field(	__u32,	lleft			)
		__field(	__u32,	lright			)
		__field(	__u64,	goal			)
		__field(	__u64,	pleft			)
		__field(	__u64,	pright			)
		__field(	unsigned int, flags		)
	),

	TP_fast_assign(
		tp_assign(dev, ar->inode->i_sb->s_dev)
		tp_assign(ino, ar->inode->i_ino)
		tp_assign(len, ar->len)
		tp_assign(logical, ar->logical)
		tp_assign(goal, ar->goal)
		tp_assign(lleft, ar->lleft)
		tp_assign(lright, ar->lright)
		tp_assign(pleft, ar->pleft)
		tp_assign(pright, ar->pright)
		tp_assign(flags, ar->flags)
	),

	TP_printk("dev %d,%d ino %lu flags %u len %u lblk %u goal %llu "
		  "lleft %u lright %u pleft %llu pright %llu ",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->flags,
		  __entry->len, __entry->logical, __entry->goal,
		  __entry->lleft, __entry->lright, __entry->pleft,
		  __entry->pright)
)

TRACE_EVENT(ext4_allocate_blocks,
	TP_PROTO(struct ext4_allocation_request *ar, unsigned long long block),

	TP_ARGS(ar, block),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u64,	block			)
		__field(	unsigned int, len		)
		__field(	__u32,  logical			)
		__field(	__u32,	lleft			)
		__field(	__u32,	lright			)
		__field(	__u64,	goal			)
		__field(	__u64,	pleft			)
		__field(	__u64,	pright			)
		__field(	unsigned int, flags		)
	),

	TP_fast_assign(
		tp_assign(dev, ar->inode->i_sb->s_dev)
		tp_assign(ino, ar->inode->i_ino)
		tp_assign(block, block)
		tp_assign(len, ar->len)
		tp_assign(logical, ar->logical)
		tp_assign(goal, ar->goal)
		tp_assign(lleft, ar->lleft)
		tp_assign(lright, ar->lright)
		tp_assign(pleft, ar->pleft)
		tp_assign(pright, ar->pright)
		tp_assign(flags, ar->flags)
	),

	TP_printk("dev %d,%d ino %lu flags %u len %u block %llu lblk %u "
		  "goal %llu lleft %u lright %u pleft %llu pright %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->flags,
		  __entry->len, __entry->block, __entry->logical,
		  __entry->goal,  __entry->lleft, __entry->lright,
		  __entry->pleft, __entry->pright)
)

TRACE_EVENT(ext4_free_blocks,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33))
	TP_PROTO(struct inode *inode, __u64 block, unsigned long count,
		 int flags),

	TP_ARGS(inode, block, count, flags),
#else
	TP_PROTO(struct inode *inode, __u64 block, unsigned long count,
		 int metadata),

	TP_ARGS(inode, block, count, metadata),
#endif

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u64,	block			)
		__field(	unsigned long,	count		)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33))
		__field(	int,	flags			)
		__field(	TP_MODE_T, mode			)
#else
		__field(	int,	metadata		)
#endif
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(block, block)
		tp_assign(count, count)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33))
		tp_assign(flags, flags)
		tp_assign(mode, inode->i_mode)
#else
		tp_assign(metadata, metadata)
#endif
	),

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33))
	TP_printk("dev %d,%d ino %lu mode 0%o block %llu count %lu flags %d",
#else
	TP_printk("dev %d,%d ino %lu block %llu count %lu metadata %d",
#endif
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33))
		  __entry->mode, __entry->block, __entry->count,
		  __entry->flags)
#else
		  __entry->block, __entry->count, __entry->metadata)
#endif
)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39))
TRACE_EVENT(ext4_sync_file_enter,
#else
TRACE_EVENT(ext4_sync_file,
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
	TP_PROTO(struct file *file, int datasync),

	TP_ARGS(file, datasync),
#else
	TP_PROTO(struct file *file, struct dentry *dentry, int datasync),

	TP_ARGS(file, dentry, datasync),
#endif

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	ino_t,	parent			)
		__field(	int,	datasync		)
	),

	TP_fast_assign(
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
		tp_assign(dev, file->f_path.dentry->d_inode->i_sb->s_dev)
		tp_assign(ino, file->f_path.dentry->d_inode->i_ino)
		tp_assign(datasync, datasync)
		tp_assign(parent, file->f_path.dentry->d_parent->d_inode->i_ino)
#else
		tp_assign(dev, dentry->d_inode->i_sb->s_dev)
		tp_assign(ino, dentry->d_inode->i_ino)
		tp_assign(datasync, datasync)
		tp_assign(parent, dentry->d_parent->d_inode->i_ino)
#endif
	),

	TP_printk("dev %d,%d ino %lu parent %lu datasync %d ",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned long) __entry->parent, __entry->datasync)
)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39))
TRACE_EVENT(ext4_sync_file_exit,
	TP_PROTO(struct inode *inode, int ret),

	TP_ARGS(inode, ret),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	int,	ret			)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(ret, ret)
	),

	TP_printk("dev %d,%d ino %lu ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->ret)
)
#endif

TRACE_EVENT(ext4_sync_fs,
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

TRACE_EVENT(ext4_alloc_da_blocks,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field( unsigned int,	data_blocks	)
		__field( unsigned int,	meta_blocks	)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(data_blocks, EXT4_I(inode)->i_reserved_data_blocks)
		tp_assign(meta_blocks, EXT4_I(inode)->i_reserved_meta_blocks)
	),

	TP_printk("dev %d,%d ino %lu data_blocks %u meta_blocks %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->data_blocks, __entry->meta_blocks)
)

TRACE_EVENT(ext4_mballoc_alloc,
	TP_PROTO(struct ext4_allocation_context *ac),

	TP_ARGS(ac),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u32, 	orig_logical		)
		__field(	  int,	orig_start		)
		__field(	__u32, 	orig_group		)
		__field(	  int,	orig_len		)
		__field(	__u32, 	goal_logical		)
		__field(	  int,	goal_start		)
		__field(	__u32, 	goal_group		)
		__field(	  int,	goal_len		)
		__field(	__u32, 	result_logical		)
		__field(	  int,	result_start		)
		__field(	__u32, 	result_group		)
		__field(	  int,	result_len		)
		__field(	__u16,	found			)
		__field(	__u16,	groups			)
		__field(	__u16,	buddy			)
		__field(	__u16,	flags			)
		__field(	__u16,	tail			)
		__field(	__u8,	cr			)
	),

	TP_fast_assign(
		tp_assign(dev, ac->ac_inode->i_sb->s_dev)
		tp_assign(ino, ac->ac_inode->i_ino)
		tp_assign(orig_logical, ac->ac_o_ex.fe_logical)
		tp_assign(orig_start, ac->ac_o_ex.fe_start)
		tp_assign(orig_group, ac->ac_o_ex.fe_group)
		tp_assign(orig_len, ac->ac_o_ex.fe_len)
		tp_assign(goal_logical, ac->ac_g_ex.fe_logical)
		tp_assign(goal_start, ac->ac_g_ex.fe_start)
		tp_assign(goal_group, ac->ac_g_ex.fe_group)
		tp_assign(goal_len, ac->ac_g_ex.fe_len)
		tp_assign(result_logical, ac->ac_f_ex.fe_logical)
		tp_assign(result_start, ac->ac_f_ex.fe_start)
		tp_assign(result_group, ac->ac_f_ex.fe_group)
		tp_assign(result_len, ac->ac_f_ex.fe_len)
		tp_assign(found, ac->ac_found)
		tp_assign(flags, ac->ac_flags)
		tp_assign(groups, ac->ac_groups_scanned)
		tp_assign(buddy, ac->ac_buddy)
		tp_assign(tail, ac->ac_tail)
		tp_assign(cr, ac->ac_criteria)
	),

	TP_printk("dev %d,%d inode %lu orig %u/%d/%u@%u goal %u/%d/%u@%u "
		  "result %u/%d/%u@%u blks %u grps %u cr %u flags 0x%04x "
		  "tail %u broken %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->orig_group, __entry->orig_start,
		  __entry->orig_len, __entry->orig_logical,
		  __entry->goal_group, __entry->goal_start,
		  __entry->goal_len, __entry->goal_logical,
		  __entry->result_group, __entry->result_start,
		  __entry->result_len, __entry->result_logical,
		  __entry->found, __entry->groups, __entry->cr,
		  __entry->flags, __entry->tail,
		  __entry->buddy ? 1 << __entry->buddy : 0)
)

TRACE_EVENT(ext4_mballoc_prealloc,
	TP_PROTO(struct ext4_allocation_context *ac),

	TP_ARGS(ac),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u32, 	orig_logical		)
		__field(	  int,	orig_start		)
		__field(	__u32, 	orig_group		)
		__field(	  int,	orig_len		)
		__field(	__u32, 	result_logical		)
		__field(	  int,	result_start		)
		__field(	__u32, 	result_group		)
		__field(	  int,	result_len		)
	),

	TP_fast_assign(
		tp_assign(dev, ac->ac_inode->i_sb->s_dev)
		tp_assign(ino, ac->ac_inode->i_ino)
		tp_assign(orig_logical, ac->ac_o_ex.fe_logical)
		tp_assign(orig_start, ac->ac_o_ex.fe_start)
		tp_assign(orig_group, ac->ac_o_ex.fe_group)
		tp_assign(orig_len, ac->ac_o_ex.fe_len)
		tp_assign(result_logical, ac->ac_b_ex.fe_logical)
		tp_assign(result_start, ac->ac_b_ex.fe_start)
		tp_assign(result_group, ac->ac_b_ex.fe_group)
		tp_assign(result_len, ac->ac_b_ex.fe_len)
	),

	TP_printk("dev %d,%d inode %lu orig %u/%d/%u@%u result %u/%d/%u@%u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->orig_group, __entry->orig_start,
		  __entry->orig_len, __entry->orig_logical,
		  __entry->result_group, __entry->result_start,
		  __entry->result_len, __entry->result_logical)
)

DECLARE_EVENT_CLASS(ext4__mballoc,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	TP_PROTO(struct super_block *sb,
		 struct inode *inode,
		 ext4_group_t group,
		 ext4_grpblk_t start,
		 ext4_grpblk_t len),

	TP_ARGS(sb, inode, group, start, len),
#else
	TP_PROTO(struct ext4_allocation_context *ac),

	TP_ARGS(ac),
#endif

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37))
		__field(	__u32, 	result_logical		)
#endif
		__field(	  int,	result_start		)
		__field(	__u32, 	result_group		)
		__field(	  int,	result_len		)
	),

	TP_fast_assign(
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
		tp_assign(dev, sb->s_dev)
		tp_assign(ino, inode ? inode->i_ino : 0)
		tp_assign(result_start, start)
		tp_assign(result_group, group)
		tp_assign(result_len, len)
#else
		tp_assign(dev, ac->ac_sb->s_dev)
		tp_assign(ino, ac->ac_inode ? ac->ac_inode->i_ino : 0)
		tp_assign(result_logical, ac->ac_b_ex.fe_logical)
		tp_assign(result_start, ac->ac_b_ex.fe_start)
		tp_assign(result_group, ac->ac_b_ex.fe_group)
		tp_assign(result_len, ac->ac_b_ex.fe_len)
#endif
	),

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	TP_printk("dev %d,%d inode %lu extent %u/%d/%d ",
#else
	TP_printk("dev %d,%d inode %lu extent %u/%d/%u@%u ",
#endif
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->result_group, __entry->result_start,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
		  __entry->result_len
#else
		  __entry->result_len, __entry->result_logical
#endif
	)
)

DEFINE_EVENT(ext4__mballoc, ext4_mballoc_discard,

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	TP_PROTO(struct super_block *sb,
		 struct inode *inode,
		 ext4_group_t group,
		 ext4_grpblk_t start,
		 ext4_grpblk_t len),

	TP_ARGS(sb, inode, group, start, len)
#else
	TP_PROTO(struct ext4_allocation_context *ac),

	TP_ARGS(ac)
#endif
)

DEFINE_EVENT(ext4__mballoc, ext4_mballoc_free,

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	TP_PROTO(struct super_block *sb,
		 struct inode *inode,
		 ext4_group_t group,
		 ext4_grpblk_t start,
		 ext4_grpblk_t len),

	TP_ARGS(sb, inode, group, start, len)
#else
	TP_PROTO(struct ext4_allocation_context *ac),

	TP_ARGS(ac)
#endif
)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33))
TRACE_EVENT(ext4_forget,
	TP_PROTO(struct inode *inode, int is_metadata, __u64 block),

	TP_ARGS(inode, is_metadata, block),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u64,	block			)
		__field(	int,	is_metadata		)
		__field(	TP_MODE_T, mode			)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(block, block)
		tp_assign(is_metadata, is_metadata)
		tp_assign(mode, inode->i_mode)
	),

	TP_printk("dev %d,%d ino %lu mode 0%o is_metadata %d block %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->mode, __entry->is_metadata, __entry->block)
)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,34))
TRACE_EVENT(ext4_da_update_reserve_space,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
	TP_PROTO(struct inode *inode, int used_blocks, int quota_claim),

	TP_ARGS(inode, used_blocks, quota_claim),
#else
	TP_PROTO(struct inode *inode, int used_blocks),

	TP_ARGS(inode, used_blocks),
#endif

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u64,	i_blocks		)
		__field(	int,	used_blocks		)
		__field(	int,	reserved_data_blocks	)
		__field(	int,	reserved_meta_blocks	)
		__field(	int,	allocated_meta_blocks	)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
		__field(	int,	quota_claim		)
#endif
		__field(	TP_MODE_T, mode			)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(i_blocks, inode->i_blocks)
		tp_assign(used_blocks, used_blocks)
		tp_assign(reserved_data_blocks,
				EXT4_I(inode)->i_reserved_data_blocks)
		tp_assign(reserved_meta_blocks,
				EXT4_I(inode)->i_reserved_meta_blocks)
		tp_assign(allocated_meta_blocks,
				EXT4_I(inode)->i_allocated_meta_blocks)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
		tp_assign(quota_claim, quota_claim)
#endif
		tp_assign(mode, inode->i_mode)
	),

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
	TP_printk("dev %d,%d ino %lu mode 0%o i_blocks %llu used_blocks %d "
		  "reserved_data_blocks %d reserved_meta_blocks %d "
		  "allocated_meta_blocks %d quota_claim %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->mode, __entry->i_blocks,
		  __entry->used_blocks, __entry->reserved_data_blocks,
		  __entry->reserved_meta_blocks, __entry->allocated_meta_blocks,
		  __entry->quota_claim)
#else
	TP_printk("dev %d,%d ino %lu mode 0%o i_blocks %llu used_blocks %d "
		  "reserved_data_blocks %d reserved_meta_blocks %d "
		  "allocated_meta_blocks %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->mode, __entry->i_blocks,
		  __entry->used_blocks, __entry->reserved_data_blocks,
		  __entry->reserved_meta_blocks, __entry->allocated_meta_blocks)
#endif
)

TRACE_EVENT(ext4_da_reserve_space,
	TP_PROTO(struct inode *inode, int md_needed),

	TP_ARGS(inode, md_needed),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u64,	i_blocks		)
		__field(	int,	md_needed		)
		__field(	int,	reserved_data_blocks	)
		__field(	int,	reserved_meta_blocks	)
		__field(	TP_MODE_T, mode			)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(i_blocks, inode->i_blocks)
		tp_assign(md_needed, md_needed)
		tp_assign(reserved_data_blocks,
				EXT4_I(inode)->i_reserved_data_blocks)
		tp_assign(reserved_meta_blocks,
				EXT4_I(inode)->i_reserved_meta_blocks)
		tp_assign(mode, inode->i_mode)
	),

	TP_printk("dev %d,%d ino %lu mode 0%o i_blocks %llu md_needed %d "
		  "reserved_data_blocks %d reserved_meta_blocks %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->mode, __entry->i_blocks,
		  __entry->md_needed, __entry->reserved_data_blocks,
		  __entry->reserved_meta_blocks)
)

TRACE_EVENT(ext4_da_release_space,
	TP_PROTO(struct inode *inode, int freed_blocks),

	TP_ARGS(inode, freed_blocks),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u64,	i_blocks		)
		__field(	int,	freed_blocks		)
		__field(	int,	reserved_data_blocks	)
		__field(	int,	reserved_meta_blocks	)
		__field(	int,	allocated_meta_blocks	)
		__field(	TP_MODE_T, mode			)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(i_blocks, inode->i_blocks)
		tp_assign(freed_blocks, freed_blocks)
		tp_assign(reserved_data_blocks,
				EXT4_I(inode)->i_reserved_data_blocks)
		tp_assign(reserved_meta_blocks,
				EXT4_I(inode)->i_reserved_meta_blocks)
		tp_assign(allocated_meta_blocks,
				EXT4_I(inode)->i_allocated_meta_blocks)
		tp_assign(mode, inode->i_mode)
	),

	TP_printk("dev %d,%d ino %lu mode 0%o i_blocks %llu freed_blocks %d "
		  "reserved_data_blocks %d reserved_meta_blocks %d "
		  "allocated_meta_blocks %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->mode, __entry->i_blocks,
		  __entry->freed_blocks, __entry->reserved_data_blocks,
		  __entry->reserved_meta_blocks, __entry->allocated_meta_blocks)
)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
DECLARE_EVENT_CLASS(ext4__bitmap_load,
	TP_PROTO(struct super_block *sb, unsigned long group),

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

DEFINE_EVENT(ext4__bitmap_load, ext4_mb_bitmap_load,

	TP_PROTO(struct super_block *sb, unsigned long group),

	TP_ARGS(sb, group)
)

DEFINE_EVENT(ext4__bitmap_load, ext4_mb_buddy_bitmap_load,

	TP_PROTO(struct super_block *sb, unsigned long group),

	TP_ARGS(sb, group)
)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39))
DEFINE_EVENT(ext4__bitmap_load, ext4_read_block_bitmap_load,

	TP_PROTO(struct super_block *sb, unsigned long group),

	TP_ARGS(sb, group)
)

DEFINE_EVENT(ext4__bitmap_load, ext4_load_inode_bitmap,

	TP_PROTO(struct super_block *sb, unsigned long group),

	TP_ARGS(sb, group)
)

TRACE_EVENT(ext4_direct_IO_enter,
	TP_PROTO(struct inode *inode, loff_t offset, unsigned long len, int rw),

	TP_ARGS(inode, offset, len, rw),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	loff_t,	pos			)
		__field(	unsigned long,	len		)
		__field(	int,	rw			)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(pos, offset)
		tp_assign(len, len)
		tp_assign(rw, rw)
	),

	TP_printk("dev %d,%d ino %lu pos %lld len %lu rw %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->pos, __entry->len, __entry->rw)
)

TRACE_EVENT(ext4_direct_IO_exit,
	TP_PROTO(struct inode *inode, loff_t offset, unsigned long len,
		 int rw, int ret),

	TP_ARGS(inode, offset, len, rw, ret),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	loff_t,	pos			)
		__field(	unsigned long,	len		)
		__field(	int,	rw			)
		__field(	int,	ret			)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(pos, offset)
		tp_assign(len, len)
		tp_assign(rw, rw)
		tp_assign(ret, ret)
	),

	TP_printk("dev %d,%d ino %lu pos %lld len %lu rw %d ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->pos, __entry->len,
		  __entry->rw, __entry->ret)
)

TRACE_EVENT(ext4_fallocate_enter,
	TP_PROTO(struct inode *inode, loff_t offset, loff_t len, int mode),

	TP_ARGS(inode, offset, len, mode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	loff_t,	pos			)
		__field(	loff_t,	len			)
		__field(	int,	mode			)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(pos, offset)
		tp_assign(len, len)
		tp_assign(mode, mode)
	),

	TP_printk("dev %d,%d ino %lu pos %lld len %lld mode %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->pos,
		  __entry->len, __entry->mode)
)

TRACE_EVENT(ext4_fallocate_exit,
	TP_PROTO(struct inode *inode, loff_t offset,
		 unsigned int max_blocks, int ret),

	TP_ARGS(inode, offset, max_blocks, ret),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	loff_t,	pos			)
		__field(	unsigned int,	blocks		)
		__field(	int, 	ret			)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(pos, offset)
		tp_assign(blocks, max_blocks)
		tp_assign(ret, ret)
	),

	TP_printk("dev %d,%d ino %lu pos %lld blocks %u ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->pos, __entry->blocks,
		  __entry->ret)
)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0))

TRACE_EVENT(ext4_punch_hole,
	TP_PROTO(struct inode *inode, loff_t offset, loff_t len),

	TP_ARGS(inode, offset, len),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	loff_t,	offset			)
		__field(	loff_t, len			)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(offset, offset)
		tp_assign(len, len)
	),

	TP_printk("dev %d,%d ino %lu offset %lld len %lld",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->offset, __entry->len)
)

#endif

TRACE_EVENT(ext4_unlink_enter,
	TP_PROTO(struct inode *parent, struct dentry *dentry),

	TP_ARGS(parent, dentry),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	ino_t,	parent			)
		__field(	loff_t,	size			)
	),

	TP_fast_assign(
		tp_assign(dev, dentry->d_inode->i_sb->s_dev)
		tp_assign(ino, dentry->d_inode->i_ino)
		tp_assign(parent, parent->i_ino)
		tp_assign(size, dentry->d_inode->i_size)
	),

	TP_printk("dev %d,%d ino %lu size %lld parent %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->size,
		  (unsigned long) __entry->parent)
)

TRACE_EVENT(ext4_unlink_exit,
	TP_PROTO(struct dentry *dentry, int ret),

	TP_ARGS(dentry, ret),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	int,	ret			)
	),

	TP_fast_assign(
		tp_assign(dev, dentry->d_inode->i_sb->s_dev)
		tp_assign(ino, dentry->d_inode->i_ino)
		tp_assign(ret, ret)
	),

	TP_printk("dev %d,%d ino %lu ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->ret)
)

DECLARE_EVENT_CLASS(ext4__truncate,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	dev_t,		dev		)
		__field(	ino_t,		ino		)
		__field(	__u64,		blocks		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(blocks, inode->i_blocks)
	),

	TP_printk("dev %d,%d ino %lu blocks %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->blocks)
)

DEFINE_EVENT(ext4__truncate, ext4_truncate_enter,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
)

DEFINE_EVENT(ext4__truncate, ext4_truncate_exit,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
/* 'ux' is the uninitialized extent. */
TRACE_EVENT(ext4_ext_convert_to_initialized_enter,
	TP_PROTO(struct inode *inode, struct ext4_map_blocks *map,
		 struct ext4_extent *ux),

	TP_ARGS(inode, map, ux),

	TP_STRUCT__entry(
		__field(	dev_t,		dev	)
		__field(	ino_t,		ino	)
		__field(	ext4_lblk_t,	m_lblk	)
		__field(	unsigned,	m_len	)
		__field(	ext4_lblk_t,	u_lblk	)
		__field(	unsigned,	u_len	)
		__field(	ext4_fsblk_t,	u_pblk	)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(m_lblk, map->m_lblk)
		tp_assign(m_len, map->m_len)
		tp_assign(u_lblk, le32_to_cpu(ux->ee_block))
		tp_assign(u_len, ext4_ext_get_actual_len(ux))
		tp_assign(u_pblk, ext4_ext_pblock(ux))
	),

	TP_printk("dev %d,%d ino %lu m_lblk %u m_len %u u_lblk %u u_len %u "
		  "u_pblk %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->m_lblk, __entry->m_len,
		  __entry->u_lblk, __entry->u_len, __entry->u_pblk)
)

/*
 * 'ux' is the uninitialized extent.
 * 'ix' is the initialized extent to which blocks are transferred.
 */
TRACE_EVENT(ext4_ext_convert_to_initialized_fastpath,
	TP_PROTO(struct inode *inode, struct ext4_map_blocks *map,
		 struct ext4_extent *ux, struct ext4_extent *ix),

	TP_ARGS(inode, map, ux, ix),

	TP_STRUCT__entry(
		__field(	dev_t,		dev	)
		__field(	ino_t,		ino	)
		__field(	ext4_lblk_t,	m_lblk	)
		__field(	unsigned,	m_len	)
		__field(	ext4_lblk_t,	u_lblk	)
		__field(	unsigned,	u_len	)
		__field(	ext4_fsblk_t,	u_pblk	)
		__field(	ext4_lblk_t,	i_lblk	)
		__field(	unsigned,	i_len	)
		__field(	ext4_fsblk_t,	i_pblk	)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(m_lblk, map->m_lblk)
		tp_assign(m_len, map->m_len)
		tp_assign(u_lblk, le32_to_cpu(ux->ee_block))
		tp_assign(u_len, ext4_ext_get_actual_len(ux))
		tp_assign(u_pblk, ext4_ext_pblock(ux))
		tp_assign(i_lblk, le32_to_cpu(ix->ee_block))
		tp_assign(i_len, ext4_ext_get_actual_len(ix))
		tp_assign(i_pblk, ext4_ext_pblock(ix))
	),

	TP_printk("dev %d,%d ino %lu m_lblk %u m_len %u "
		  "u_lblk %u u_len %u u_pblk %llu "
		  "i_lblk %u i_len %u i_pblk %llu ",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->m_lblk, __entry->m_len,
		  __entry->u_lblk, __entry->u_len, __entry->u_pblk,
		  __entry->i_lblk, __entry->i_len, __entry->i_pblk)
)
#endif

DECLARE_EVENT_CLASS(ext4__map_blocks_enter,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk,
		 unsigned int len, unsigned int flags),

	TP_ARGS(inode, lblk, len, flags),

	TP_STRUCT__entry(
		__field(	dev_t,		dev		)
		__field(	ino_t,		ino		)
		__field(	ext4_lblk_t,	lblk		)
		__field(	unsigned int,	len		)
		__field(	unsigned int,	flags		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(lblk, lblk)
		tp_assign(len, len)
		tp_assign(flags, flags)
	),

	TP_printk("dev %d,%d ino %lu lblk %u len %u flags %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->lblk, __entry->len, __entry->flags)
)

DEFINE_EVENT(ext4__map_blocks_enter, ext4_ext_map_blocks_enter,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk,
		 unsigned len, unsigned flags),

	TP_ARGS(inode, lblk, len, flags)
)

DEFINE_EVENT(ext4__map_blocks_enter, ext4_ind_map_blocks_enter,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk,
		 unsigned len, unsigned flags),

	TP_ARGS(inode, lblk, len, flags)
)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0))

DECLARE_EVENT_CLASS(ext4__map_blocks_exit,
	TP_PROTO(struct inode *inode, unsigned flags, struct ext4_map_blocks *map,
		 int ret),

	TP_ARGS(inode, flags, map, ret),

	TP_STRUCT__entry(
		__field(	dev_t,		dev		)
		__field(	ino_t,		ino		)
		__field(	unsigned int,	flags		)
		__field(	ext4_fsblk_t,	pblk		)
		__field(	ext4_lblk_t,	lblk		)
		__field(	unsigned int,	len		)
		__field(	unsigned int,	mflags		)
		__field(	int,		ret		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(flags, flags)
		tp_assign(pblk, map->m_pblk)
		tp_assign(lblk, map->m_lblk)
		tp_assign(len, map->m_len)
		tp_assign(mflags, map->m_flags)
		tp_assign(ret, ret)
	),

	TP_printk("dev %d,%d ino %lu flags %s lblk %u pblk %llu len %u "
		  "mflags %s ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  show_map_flags(__entry->flags), __entry->lblk, __entry->pblk,
		  __entry->len, show_mflags(__entry->mflags), __entry->ret)
)

DEFINE_EVENT(ext4__map_blocks_exit, ext4_ext_map_blocks_exit,
	TP_PROTO(struct inode *inode, unsigned flags,
		 struct ext4_map_blocks *map, int ret),

	TP_ARGS(inode, flags, map, ret)
)

DEFINE_EVENT(ext4__map_blocks_exit, ext4_ind_map_blocks_exit,
	TP_PROTO(struct inode *inode, unsigned flags,
		 struct ext4_map_blocks *map, int ret),

	TP_ARGS(inode, flags, map, ret)
)

#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0))

DECLARE_EVENT_CLASS(ext4__map_blocks_exit,
	TP_PROTO(struct inode *inode, struct ext4_map_blocks *map, int ret),

	TP_ARGS(inode, map, ret),

	TP_STRUCT__entry(
		__field(	dev_t,		dev		)
		__field(	ino_t,		ino		)
		__field(	ext4_fsblk_t,	pblk		)
		__field(	ext4_lblk_t,	lblk		)
		__field(	unsigned int,	len		)
		__field(	unsigned int,	flags		)
		__field(	int,		ret		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(pblk, map->m_pblk)
		tp_assign(lblk, map->m_lblk)
		tp_assign(len, map->m_len)
		tp_assign(flags, map->m_flags)
		tp_assign(ret, ret)
	),

	TP_printk("dev %d,%d ino %lu lblk %u pblk %llu len %u flags %x ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->lblk, __entry->pblk,
		  __entry->len, __entry->flags, __entry->ret)
)

DEFINE_EVENT(ext4__map_blocks_exit, ext4_ext_map_blocks_exit,
	TP_PROTO(struct inode *inode, struct ext4_map_blocks *map, int ret),

	TP_ARGS(inode, map, ret)
)

DEFINE_EVENT(ext4__map_blocks_exit, ext4_ind_map_blocks_exit,
	TP_PROTO(struct inode *inode, struct ext4_map_blocks *map, int ret),

	TP_ARGS(inode, map, ret)
)

#else	/* #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0)) */

DECLARE_EVENT_CLASS(ext4__map_blocks_exit,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk,
		 ext4_fsblk_t pblk, unsigned int len, int ret),

	TP_ARGS(inode, lblk, pblk, len, ret),

	TP_STRUCT__entry(
		__field(	dev_t,		dev		)
		__field(	ino_t,		ino		)
		__field(	ext4_fsblk_t,	pblk		)
		__field(	ext4_lblk_t,	lblk		)
		__field(	unsigned int,	len		)
		__field(	int,		ret		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(pblk, pblk)
		tp_assign(lblk, lblk)
		tp_assign(len, len)
		tp_assign(ret, ret)
	),

	TP_printk("dev %d,%d ino %lu lblk %u pblk %llu len %u ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->lblk, __entry->pblk,
		  __entry->len, __entry->ret)
)

DEFINE_EVENT(ext4__map_blocks_exit, ext4_ext_map_blocks_exit,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk,
		 ext4_fsblk_t pblk, unsigned len, int ret),

	TP_ARGS(inode, lblk, pblk, len, ret)
)

DEFINE_EVENT(ext4__map_blocks_exit, ext4_ind_map_blocks_exit,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk,
		 ext4_fsblk_t pblk, unsigned len, int ret),

	TP_ARGS(inode, lblk, pblk, len, ret)
)

#endif	/* #else #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0)) */

TRACE_EVENT(ext4_ext_load_extent,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk, ext4_fsblk_t pblk),

	TP_ARGS(inode, lblk, pblk),

	TP_STRUCT__entry(
		__field(	dev_t,		dev		)
		__field(	ino_t,		ino		)
		__field(	ext4_fsblk_t,	pblk		)
		__field(	ext4_lblk_t,	lblk		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(pblk, pblk)
		tp_assign(lblk, lblk)
	),

	TP_printk("dev %d,%d ino %lu lblk %u pblk %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->lblk, __entry->pblk)
)

TRACE_EVENT(ext4_load_inode,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev		)
		__field(	ino_t,	ino		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
	),

	TP_printk("dev %d,%d ino %ld",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino)
)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0))

TRACE_EVENT(ext4_journal_start,
	TP_PROTO(struct super_block *sb, int blocks, int rsv_blocks,
		 unsigned long IP),

	TP_ARGS(sb, blocks, rsv_blocks, IP),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(unsigned long,	ip			)
		__field(	  int,	blocks			)
		__field(	  int,	rsv_blocks		)
	),

	TP_fast_assign(
		tp_assign(dev, sb->s_dev)
		tp_assign(ip, IP)
		tp_assign(blocks, blocks)
		tp_assign(rsv_blocks, rsv_blocks)
	),

	TP_printk("dev %d,%d blocks, %d rsv_blocks, %d caller %pF",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->blocks, __entry->rsv_blocks, (void *)__entry->ip)
)

TRACE_EVENT(ext4_journal_start_reserved,
	TP_PROTO(struct super_block *sb, int blocks, unsigned long IP),

	TP_ARGS(sb, blocks, IP),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(unsigned long,	ip			)
		__field(	  int,	blocks			)
	),

	TP_fast_assign(
		tp_assign(dev, sb->s_dev)
		tp_assign(ip, IP)
		tp_assign(blocks, blocks)
	),

	TP_printk("dev %d,%d blocks, %d caller %pF",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->blocks, (void *)__entry->ip)
)

#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0))

TRACE_EVENT(ext4_journal_start,
	TP_PROTO(struct super_block *sb, int nblocks, unsigned long IP),

	TP_ARGS(sb, nblocks, IP),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(unsigned long,	ip			)
		__field(	  int, 	nblocks			)
	),

	TP_fast_assign(
		tp_assign(dev, sb->s_dev)
		tp_assign(ip, IP)
		tp_assign(nblocks, nblocks)
	),

	TP_printk("dev %d,%d nblocks %d caller %pF",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->nblocks, (void *)__entry->ip)
)

DECLARE_EVENT_CLASS(ext4__trim,
	TP_PROTO(struct super_block *sb,
		 ext4_group_t group,
		 ext4_grpblk_t start,
		 ext4_grpblk_t len),

	TP_ARGS(sb, group, start, len),

	TP_STRUCT__entry(
		__field(	int,	dev_major		)
		__field(	int,	dev_minor		)
		__field(	__u32, 	group			)
		__field(	int,	start			)
		__field(	int,	len			)
	),

	TP_fast_assign(
		tp_assign(dev_major, MAJOR(sb->s_dev))
		tp_assign(dev_minor, MINOR(sb->s_dev))
		tp_assign(group, group)
		tp_assign(start, start)
		tp_assign(len, len)
	),

	TP_printk("dev %d,%d group %u, start %d, len %d",
		  __entry->dev_major, __entry->dev_minor,
		  __entry->group, __entry->start, __entry->len)
)

DEFINE_EVENT(ext4__trim, ext4_trim_extent,

	TP_PROTO(struct super_block *sb,
		 ext4_group_t group,
		 ext4_grpblk_t start,
		 ext4_grpblk_t len),

	TP_ARGS(sb, group, start, len)
)

DEFINE_EVENT(ext4__trim, ext4_trim_all_free,

	TP_PROTO(struct super_block *sb,
		 ext4_group_t group,
		 ext4_grpblk_t start,
		 ext4_grpblk_t len),

	TP_ARGS(sb, group, start, len)
)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))

TRACE_EVENT(ext4_ext_handle_uninitialized_extents,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0))
	TP_PROTO(struct inode *inode, struct ext4_map_blocks *map, int flags,
		 unsigned int allocated, ext4_fsblk_t newblock),

	TP_ARGS(inode, map, flags, allocated, newblock),
#else /* #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0)) */
	TP_PROTO(struct inode *inode, struct ext4_map_blocks *map,
		 unsigned int allocated, ext4_fsblk_t newblock),

	TP_ARGS(inode, map, allocated, newblock),
#endif /* #else #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0)) */

	TP_STRUCT__entry(
		__field(	dev_t,		dev		)
		__field(	ino_t,		ino		)
		__field(	int,		flags		)
		__field(	ext4_lblk_t,	lblk		)
		__field(	ext4_fsblk_t,	pblk		)
		__field(	unsigned int,	len		)
		__field(	unsigned int,	allocated	)
		__field(	ext4_fsblk_t,	newblk		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0))
		tp_assign(flags, flags)
#else /* #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0)) */
		tp_assign(flags, map->m_flags)
#endif /* #else #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0)) */
		tp_assign(lblk, map->m_lblk)
		tp_assign(pblk, map->m_pblk)
		tp_assign(len, map->m_len)
		tp_assign(allocated, allocated)
		tp_assign(newblk, newblock)
	),

	TP_printk("dev %d,%d ino %lu m_lblk %u m_pblk %llu m_len %u flags %d"
		  "allocated %d newblock %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->lblk, (unsigned long long) __entry->pblk,
		  __entry->len, __entry->flags,
		  (unsigned int) __entry->allocated,
		  (unsigned long long) __entry->newblk)
)

TRACE_EVENT(ext4_get_implied_cluster_alloc_exit,
	TP_PROTO(struct super_block *sb, struct ext4_map_blocks *map, int ret),

	TP_ARGS(sb, map, ret),

	TP_STRUCT__entry(
		__field(	dev_t,		dev	)
		__field(	unsigned int,	flags	)
		__field(	ext4_lblk_t,	lblk	)
		__field(	ext4_fsblk_t,	pblk	)
		__field(	unsigned int,	len	)
		__field(	int,		ret	)
	),

	TP_fast_assign(
		tp_assign(dev, sb->s_dev)
		tp_assign(flags, map->m_flags)
		tp_assign(lblk, map->m_lblk)
		tp_assign(pblk, map->m_pblk)
		tp_assign(len, map->m_len)
		tp_assign(ret, ret)
	),

	TP_printk("dev %d,%d m_lblk %u m_pblk %llu m_len %u m_flags %u ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->lblk, (unsigned long long) __entry->pblk,
		  __entry->len, __entry->flags, __entry->ret)
)

TRACE_EVENT(ext4_ext_put_in_cache,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk, unsigned int len,
		 ext4_fsblk_t start),

	TP_ARGS(inode, lblk, len, start),

	TP_STRUCT__entry(
		__field(	dev_t,		dev	)
		__field(	ino_t,		ino	)
		__field(	ext4_lblk_t,	lblk	)
		__field(	unsigned int,	len	)
		__field(	ext4_fsblk_t,	start	)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(lblk, lblk)
		tp_assign(len, len)
		tp_assign(start, start)
	),

	TP_printk("dev %d,%d ino %lu lblk %u len %u start %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->lblk,
		  __entry->len,
		  (unsigned long long) __entry->start)
)

TRACE_EVENT(ext4_ext_in_cache,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk, int ret),

	TP_ARGS(inode, lblk, ret),

	TP_STRUCT__entry(
		__field(	dev_t,		dev	)
		__field(	ino_t,		ino	)
		__field(	ext4_lblk_t,	lblk	)
		__field(	int,		ret	)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(lblk, lblk)
		tp_assign(ret, ret)
	),

	TP_printk("dev %d,%d ino %lu lblk %u ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->lblk,
		  __entry->ret)

)

TRACE_EVENT(ext4_find_delalloc_range,
	TP_PROTO(struct inode *inode, ext4_lblk_t from, ext4_lblk_t to,
		int reverse, int found, ext4_lblk_t found_blk),

	TP_ARGS(inode, from, to, reverse, found, found_blk),

	TP_STRUCT__entry(
		__field(	dev_t,		dev		)
		__field(	ino_t,		ino		)
		__field(	ext4_lblk_t,	from		)
		__field(	ext4_lblk_t,	to		)
		__field(	int,		reverse		)
		__field(	int,		found		)
		__field(	ext4_lblk_t,	found_blk	)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(from, from)
		tp_assign(to, to)
		tp_assign(reverse, reverse)
		tp_assign(found, found)
		tp_assign(found_blk, found_blk)
	),

	TP_printk("dev %d,%d ino %lu from %u to %u reverse %d found %d "
		  "(blk = %u)",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->from, (unsigned) __entry->to,
		  __entry->reverse, __entry->found,
		  (unsigned) __entry->found_blk)
)

TRACE_EVENT(ext4_get_reserved_cluster_alloc,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk, unsigned int len),

	TP_ARGS(inode, lblk, len),

	TP_STRUCT__entry(
		__field(	dev_t,		dev	)
		__field(	ino_t,		ino	)
		__field(	ext4_lblk_t,	lblk	)
		__field(	unsigned int,	len	)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(lblk, lblk)
		tp_assign(len, len)
	),

	TP_printk("dev %d,%d ino %lu lblk %u len %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->lblk,
		  __entry->len)
)

TRACE_EVENT(ext4_ext_show_extent,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk, ext4_fsblk_t pblk,
		 unsigned short len),

	TP_ARGS(inode, lblk, pblk, len),

	TP_STRUCT__entry(
		__field(	dev_t,		dev	)
		__field(	ino_t,		ino	)
		__field(	ext4_fsblk_t,	pblk	)
		__field(	ext4_lblk_t,	lblk	)
		__field(	unsigned short,	len	)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(pblk, pblk)
		tp_assign(lblk, lblk)
		tp_assign(len, len)
	),

	TP_printk("dev %d,%d ino %lu lblk %u pblk %llu len %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->lblk,
		  (unsigned long long) __entry->pblk,
		  (unsigned short) __entry->len)
)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0))

TRACE_EVENT(ext4_remove_blocks,
	    TP_PROTO(struct inode *inode, struct ext4_extent *ex,
		ext4_lblk_t from, ext4_fsblk_t to,
		long long partial_cluster),

	TP_ARGS(inode, ex, from, to, partial_cluster),

	TP_STRUCT__entry(
		__field(	dev_t,		dev	)
		__field(	ino_t,		ino	)
		__field(	ext4_lblk_t,	from	)
		__field(	ext4_lblk_t,	to	)
		__field(	long long,	partial	)
		__field(	ext4_fsblk_t,	ee_pblk	)
		__field(	ext4_lblk_t,	ee_lblk	)
		__field(	unsigned short,	ee_len	)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(from, from)
		tp_assign(to, to)
		tp_assign(partial, partial_cluster)
		tp_assign(ee_pblk, ext4_ext_pblock(ex))
		tp_assign(ee_lblk, le32_to_cpu(ex->ee_block))
		tp_assign(ee_len, ext4_ext_get_actual_len(ex))
	),

	TP_printk("dev %d,%d ino %lu extent [%u(%llu), %u]"
		  "from %u to %u partial_cluster %lld",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->ee_lblk,
		  (unsigned long long) __entry->ee_pblk,
		  (unsigned short) __entry->ee_len,
		  (unsigned) __entry->from,
		  (unsigned) __entry->to,
		  (long long) __entry->partial)
)

#else

TRACE_EVENT(ext4_remove_blocks,
	    TP_PROTO(struct inode *inode, struct ext4_extent *ex,
		ext4_lblk_t from, ext4_fsblk_t to,
		ext4_fsblk_t partial_cluster),

	TP_ARGS(inode, ex, from, to, partial_cluster),

	TP_STRUCT__entry(
		__field(	dev_t,		dev	)
		__field(	ino_t,		ino	)
		__field(	ext4_lblk_t,	from	)
		__field(	ext4_lblk_t,	to	)
		__field(	ext4_fsblk_t,	partial	)
		__field(	ext4_fsblk_t,	ee_pblk	)
		__field(	ext4_lblk_t,	ee_lblk	)
		__field(	unsigned short,	ee_len	)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(from, from)
		tp_assign(to, to)
		tp_assign(partial, partial_cluster)
		tp_assign(ee_pblk, ext4_ext_pblock(ex))
		tp_assign(ee_lblk, cpu_to_le32(ex->ee_block))
		tp_assign(ee_len, ext4_ext_get_actual_len(ex))
	),

	TP_printk("dev %d,%d ino %lu extent [%u(%llu), %u]"
		  "from %u to %u partial_cluster %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->ee_lblk,
		  (unsigned long long) __entry->ee_pblk,
		  (unsigned short) __entry->ee_len,
		  (unsigned) __entry->from,
		  (unsigned) __entry->to,
		  (unsigned) __entry->partial)
)

#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0))

TRACE_EVENT(ext4_ext_rm_leaf,
	TP_PROTO(struct inode *inode, ext4_lblk_t start,
		 struct ext4_extent *ex,
		 long long partial_cluster),

	TP_ARGS(inode, start, ex, partial_cluster),

	TP_STRUCT__entry(
		__field(	dev_t,		dev	)
		__field(	ino_t,		ino	)
		__field(	long long,	partial	)
		__field(	ext4_lblk_t,	start	)
		__field(	ext4_lblk_t,	ee_lblk	)
		__field(	ext4_fsblk_t,	ee_pblk	)
		__field(	short,		ee_len	)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(partial, partial_cluster)
		tp_assign(start, start)
		tp_assign(ee_lblk, le32_to_cpu(ex->ee_block))
		tp_assign(ee_pblk, ext4_ext_pblock(ex))
		tp_assign(ee_len, ext4_ext_get_actual_len(ex))
	),

	TP_printk("dev %d,%d ino %lu start_lblk %u last_extent [%u(%llu), %u]"
		  "partial_cluster %lld",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->start,
		  (unsigned) __entry->ee_lblk,
		  (unsigned long long) __entry->ee_pblk,
		  (unsigned short) __entry->ee_len,
		  (long long) __entry->partial)
)

#else

TRACE_EVENT(ext4_ext_rm_leaf,
	TP_PROTO(struct inode *inode, ext4_lblk_t start,
		 struct ext4_extent *ex, ext4_fsblk_t partial_cluster),

	TP_ARGS(inode, start, ex, partial_cluster),

	TP_STRUCT__entry(
		__field(	dev_t,		dev	)
		__field(	ino_t,		ino	)
		__field(	ext4_fsblk_t,	partial	)
		__field(	ext4_lblk_t,	start	)
		__field(	ext4_lblk_t,	ee_lblk	)
		__field(	ext4_fsblk_t,	ee_pblk	)
		__field(	short,		ee_len	)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(partial, partial_cluster)
		tp_assign(start, start)
		tp_assign(ee_lblk, le32_to_cpu(ex->ee_block))
		tp_assign(ee_pblk, ext4_ext_pblock(ex))
		tp_assign(ee_len, ext4_ext_get_actual_len(ex))
	),

	TP_printk("dev %d,%d ino %lu start_lblk %u last_extent [%u(%llu), %u]"
		  "partial_cluster %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->start,
		  (unsigned) __entry->ee_lblk,
		  (unsigned long long) __entry->ee_pblk,
		  (unsigned short) __entry->ee_len,
		  (unsigned) __entry->partial)
)

#endif

TRACE_EVENT(ext4_ext_rm_idx,
	TP_PROTO(struct inode *inode, ext4_fsblk_t pblk),

	TP_ARGS(inode, pblk),

	TP_STRUCT__entry(
		__field(	dev_t,		dev	)
		__field(	ino_t,		ino	)
		__field(	ext4_fsblk_t,	pblk	)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(pblk, pblk)
	),

	TP_printk("dev %d,%d ino %lu index_pblk %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned long long) __entry->pblk)
)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0))

TRACE_EVENT(ext4_ext_remove_space,
	TP_PROTO(struct inode *inode, ext4_lblk_t start,
		 ext4_lblk_t end, int depth),

	TP_ARGS(inode, start, end, depth),

	TP_STRUCT__entry(
		__field(	dev_t,		dev	)
		__field(	ino_t,		ino	)
		__field(	ext4_lblk_t,	start	)
		__field(	ext4_lblk_t,	end	)
		__field(	int,		depth	)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(start, start)
		tp_assign(end, end)
		tp_assign(depth, depth)
	),

	TP_printk("dev %d,%d ino %lu since %u end %u depth %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->start,
		  (unsigned) __entry->end,
		  __entry->depth)
)

#else

TRACE_EVENT(ext4_ext_remove_space,
	TP_PROTO(struct inode *inode, ext4_lblk_t start, int depth),

	TP_ARGS(inode, start, depth),

	TP_STRUCT__entry(
		__field(	dev_t,		dev	)
		__field(	ino_t,		ino	)
		__field(	ext4_lblk_t,	start	)
		__field(	int,		depth	)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(start, start)
		tp_assign(depth, depth)
	),

	TP_printk("dev %d,%d ino %lu since %u depth %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->start,
		  __entry->depth)
)

#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0))

TRACE_EVENT(ext4_ext_remove_space_done,
	TP_PROTO(struct inode *inode, ext4_lblk_t start, ext4_lblk_t end,
		 int depth, long long partial, __le16 eh_entries),

	TP_ARGS(inode, start, end, depth, partial, eh_entries),

	TP_STRUCT__entry(
		__field(	dev_t,		dev		)
		__field(	ino_t,		ino		)
		__field(	ext4_lblk_t,	start		)
		__field(	ext4_lblk_t,	end		)
		__field(	int,		depth		)
		__field(	long long,	partial		)
		__field(	unsigned short,	eh_entries	)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(start, start)
		tp_assign(end, end)
		tp_assign(depth, depth)
		tp_assign(partial, partial)
		tp_assign(eh_entries, le16_to_cpu(eh_entries))
	),

	TP_printk("dev %d,%d ino %lu since %u end %u depth %d partial %lld "
		  "remaining_entries %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->start,
		  (unsigned) __entry->end,
		  __entry->depth,
		  (long long) __entry->partial,
		  (unsigned short) __entry->eh_entries)
)

#else

TRACE_EVENT(ext4_ext_remove_space_done,
	TP_PROTO(struct inode *inode, ext4_lblk_t start, int depth,
		ext4_lblk_t partial, unsigned short eh_entries),

	TP_ARGS(inode, start, depth, partial, eh_entries),

	TP_STRUCT__entry(
		__field(	dev_t,		dev		)
		__field(	ino_t,		ino		)
		__field(	ext4_lblk_t,	start		)
		__field(	int,		depth		)
		__field(	ext4_lblk_t,	partial		)
		__field(	unsigned short,	eh_entries	)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(start, start)
		tp_assign(depth, depth)
		tp_assign(partial, partial)
		tp_assign(eh_entries, eh_entries)
	),

	TP_printk("dev %d,%d ino %lu since %u depth %d partial %u "
		  "remaining_entries %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->start,
		  __entry->depth,
		  (unsigned) __entry->partial,
		  (unsigned short) __entry->eh_entries)
)

#endif

#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,12,0))

DECLARE_EVENT_CLASS(ext4__es_extent,
	TP_PROTO(struct inode *inode, struct extent_status *es),

	TP_ARGS(inode, es),

	TP_STRUCT__entry(
		__field(	dev_t,		dev		)
		__field(	ino_t,		ino		)
		__field(	ext4_lblk_t,	lblk		)
		__field(	ext4_lblk_t,	len		)
		__field(	ext4_fsblk_t,	pblk		)
		__field(	char, status	)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(lblk, es->es_lblk)
		tp_assign(len, es->es_len)
		tp_assign(pblk, ext4_es_pblock(es))
		tp_assign(status, ext4_es_status(es))
	),

	TP_printk("dev %d,%d ino %lu es [%u/%u) mapped %llu status %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->lblk, __entry->len,
		  __entry->pblk, show_extent_status(__entry->status))
)

DEFINE_EVENT(ext4__es_extent, ext4_es_insert_extent,
	TP_PROTO(struct inode *inode, struct extent_status *es),

	TP_ARGS(inode, es)
)

DEFINE_EVENT(ext4__es_extent, ext4_es_cache_extent,
	TP_PROTO(struct inode *inode, struct extent_status *es),

	TP_ARGS(inode, es)
)

#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0))

TRACE_EVENT(ext4_es_insert_extent,
	TP_PROTO(struct inode *inode, struct extent_status *es),

	TP_ARGS(inode, es),

	TP_STRUCT__entry(
		__field(	dev_t,		dev		)
		__field(	ino_t,		ino		)
		__field(	ext4_lblk_t,	lblk		)
		__field(	ext4_lblk_t,	len		)
		__field(	ext4_fsblk_t,	pblk		)
		__field(	char, status	)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(lblk, es->es_lblk)
		tp_assign(len, es->es_len)
		tp_assign(pblk, ext4_es_pblock(es))
		tp_assign(status, ext4_es_status(es) >> 60)
	),

	TP_printk("dev %d,%d ino %lu es [%u/%u) mapped %llu status %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->lblk, __entry->len,
		  __entry->pblk, show_extent_status(__entry->status))
)

TRACE_EVENT(ext4_es_remove_extent,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk, ext4_lblk_t len),

	TP_ARGS(inode, lblk, len),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	loff_t,	lblk			)
		__field(	loff_t,	len			)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(lblk, lblk)
		tp_assign(len, len)
	),

	TP_printk("dev %d,%d ino %lu es [%lld/%lld)",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->lblk, __entry->len)
)

TRACE_EVENT(ext4_es_find_delayed_extent_range_enter,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk),

	TP_ARGS(inode, lblk),

	TP_STRUCT__entry(
		__field(	dev_t,		dev		)
		__field(	ino_t,		ino		)
		__field(	ext4_lblk_t,	lblk		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(lblk, lblk)
	),

	TP_printk("dev %d,%d ino %lu lblk %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->lblk)
)

TRACE_EVENT(ext4_es_find_delayed_extent_range_exit,
	TP_PROTO(struct inode *inode, struct extent_status *es),

	TP_ARGS(inode, es),

	TP_STRUCT__entry(
		__field(	dev_t,		dev		)
		__field(	ino_t,		ino		)
		__field(	ext4_lblk_t,	lblk		)
		__field(	ext4_lblk_t,	len		)
		__field(	ext4_fsblk_t,	pblk		)
		__field(	char, status	)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(lblk, es->es_lblk)
		tp_assign(len, es->es_len)
		tp_assign(pblk, ext4_es_pblock(es))
		tp_assign(status, ext4_es_status(es) >> 60)
	),

	TP_printk("dev %d,%d ino %lu es [%u/%u) mapped %llu status %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->lblk, __entry->len,
		  __entry->pblk, show_extent_status(__entry->status))
)

TRACE_EVENT(ext4_es_lookup_extent_enter,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk),

	TP_ARGS(inode, lblk),

	TP_STRUCT__entry(
		__field(	dev_t,		dev		)
		__field(	ino_t,		ino		)
		__field(	ext4_lblk_t,	lblk		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(lblk, lblk)
	),

	TP_printk("dev %d,%d ino %lu lblk %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->lblk)
)

TRACE_EVENT(ext4_es_lookup_extent_exit,
	TP_PROTO(struct inode *inode, struct extent_status *es,
		 int found),

	TP_ARGS(inode, es, found),

	TP_STRUCT__entry(
		__field(	dev_t,		dev		)
		__field(	ino_t,		ino		)
		__field(	ext4_lblk_t,	lblk		)
		__field(	ext4_lblk_t,	len		)
		__field(	ext4_fsblk_t,	pblk		)
		__field(	char,		status		)
		__field(	int,		found		)
	),

	TP_fast_assign(
		tp_assign(dev, inode->i_sb->s_dev)
		tp_assign(ino, inode->i_ino)
		tp_assign(lblk, es->es_lblk)
		tp_assign(len, es->es_len)
		tp_assign(pblk, ext4_es_pblock(es))
		tp_assign(status, ext4_es_status(es) >> 60)
		tp_assign(found, found)
	),

	TP_printk("dev %d,%d ino %lu found %d [%u/%u) %llu %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->found,
		  __entry->lblk, __entry->len,
		  __entry->found ? __entry->pblk : 0,
		  show_extent_status(__entry->found ? __entry->status : 0))
)

TRACE_EVENT(ext4_es_shrink_enter,
	TP_PROTO(struct super_block *sb, int nr_to_scan, int cache_cnt),

	TP_ARGS(sb, nr_to_scan, cache_cnt),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	int,	nr_to_scan		)
		__field(	int,	cache_cnt		)
	),

	TP_fast_assign(
		tp_assign(dev, sb->s_dev)
		tp_assign(nr_to_scan, nr_to_scan)
		tp_assign(cache_cnt, cache_cnt)
	),

	TP_printk("dev %d,%d nr_to_scan %d cache_cnt %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->nr_to_scan, __entry->cache_cnt)
)

TRACE_EVENT(ext4_es_shrink_exit,
	TP_PROTO(struct super_block *sb, int shrunk_nr, int cache_cnt),

	TP_ARGS(sb, shrunk_nr, cache_cnt),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	int,	shrunk_nr		)
		__field(	int,	cache_cnt		)
	),

	TP_fast_assign(
		tp_assign(dev, sb->s_dev)
		tp_assign(shrunk_nr, shrunk_nr)
		tp_assign(cache_cnt, cache_cnt)
	),

	TP_printk("dev %d,%d shrunk_nr %d cache_cnt %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->shrunk_nr, __entry->cache_cnt)
)

#endif

#endif /* _TRACE_EXT4_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"
