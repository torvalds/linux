#undef TRACE_SYSTEM
#define TRACE_SYSTEM writeback

#if !defined(_TRACE_WRITEBACK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_WRITEBACK_H

#include <linux/backing-dev.h>
#include <linux/writeback.h>
#include <linux/version.h>

#ifndef _TRACE_WRITEBACK_DEF_
#define _TRACE_WRITEBACK_DEF_
static inline struct backing_dev_info *inode_to_bdi(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;

	if (strcmp(sb->s_type->name, "bdev") == 0)
		return inode->i_mapping->backing_dev_info;

	return sb->s_bdi;
}
#endif

#define show_inode_state(state)					\
	__print_flags(state, "|",				\
		{I_DIRTY_SYNC,		"I_DIRTY_SYNC"},	\
		{I_DIRTY_DATASYNC,	"I_DIRTY_DATASYNC"},	\
		{I_DIRTY_PAGES,		"I_DIRTY_PAGES"},	\
		{I_NEW,			"I_NEW"},		\
		{I_WILL_FREE,		"I_WILL_FREE"},		\
		{I_FREEING,		"I_FREEING"},		\
		{I_CLEAR,		"I_CLEAR"},		\
		{I_SYNC,		"I_SYNC"},		\
		{I_REFERENCED,		"I_REFERENCED"}		\
	)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
#define WB_WORK_REASON							\
		{WB_REASON_BACKGROUND,		"background"},		\
		{WB_REASON_TRY_TO_FREE_PAGES,	"try_to_free_pages"},	\
		{WB_REASON_SYNC,		"sync"},		\
		{WB_REASON_PERIODIC,		"periodic"},		\
		{WB_REASON_LAPTOP_TIMER,	"laptop_timer"},	\
		{WB_REASON_FREE_MORE_MEM,	"free_more_memory"},	\
		{WB_REASON_FS_FREE_SPACE,	"fs_free_space"},	\
		{WB_REASON_FORKER_THREAD,	"forker_thread"}
#endif

DECLARE_EVENT_CLASS(writeback_work_class,
	TP_PROTO(struct backing_dev_info *bdi, struct wb_writeback_work *work),
	TP_ARGS(bdi, work),
	TP_STRUCT__entry(
		__array(char, name, 32)
	),
	TP_fast_assign(
		tp_memcpy(name, dev_name(bdi->dev ? bdi->dev :
				default_backing_dev_info.dev), 32)
	),
	TP_printk("bdi %s",
		  __entry->name
	)
)
#define DEFINE_WRITEBACK_WORK_EVENT(name) \
DEFINE_EVENT(writeback_work_class, name, \
	TP_PROTO(struct backing_dev_info *bdi, struct wb_writeback_work *work), \
	TP_ARGS(bdi, work))
DEFINE_WRITEBACK_WORK_EVENT(writeback_nothread)
DEFINE_WRITEBACK_WORK_EVENT(writeback_queue)
DEFINE_WRITEBACK_WORK_EVENT(writeback_exec)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0))
DEFINE_WRITEBACK_WORK_EVENT(writeback_start)
DEFINE_WRITEBACK_WORK_EVENT(writeback_written)
DEFINE_WRITEBACK_WORK_EVENT(writeback_wait)
#endif

TRACE_EVENT(writeback_pages_written,
	TP_PROTO(long pages_written),
	TP_ARGS(pages_written),
	TP_STRUCT__entry(
		__field(long,		pages)
	),
	TP_fast_assign(
		tp_assign(pages, pages_written)
	),
	TP_printk("%ld", __entry->pages)
)

DECLARE_EVENT_CLASS(writeback_class,
	TP_PROTO(struct backing_dev_info *bdi),
	TP_ARGS(bdi),
	TP_STRUCT__entry(
		__array(char, name, 32)
	),
	TP_fast_assign(
		tp_memcpy(name, dev_name(bdi->dev), 32)
	),
	TP_printk("bdi %s",
		  __entry->name
	)
)
#define DEFINE_WRITEBACK_EVENT(name) \
DEFINE_EVENT(writeback_class, name, \
	TP_PROTO(struct backing_dev_info *bdi), \
	TP_ARGS(bdi))

#define DEFINE_WRITEBACK_EVENT_MAP(name, map) \
DEFINE_EVENT_MAP(writeback_class, name, map, \
	TP_PROTO(struct backing_dev_info *bdi), \
	TP_ARGS(bdi))

DEFINE_WRITEBACK_EVENT(writeback_nowork)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38))
DEFINE_WRITEBACK_EVENT(writeback_wake_background)
#endif
DEFINE_WRITEBACK_EVENT(writeback_wake_thread)
DEFINE_WRITEBACK_EVENT(writeback_wake_forker_thread)
DEFINE_WRITEBACK_EVENT(writeback_bdi_register)
DEFINE_WRITEBACK_EVENT(writeback_bdi_unregister)
DEFINE_WRITEBACK_EVENT(writeback_thread_start)
DEFINE_WRITEBACK_EVENT(writeback_thread_stop)
#if (LTTNG_KERNEL_RANGE(3,1,0, 3,2,0))
DEFINE_WRITEBACK_EVENT_MAP(balance_dirty_start, writeback_balance_dirty_start)
DEFINE_WRITEBACK_EVENT_MAP(balance_dirty_wait, writeback_balance_dirty_wait)

TRACE_EVENT_MAP(balance_dirty_written,

	writeback_balance_dirty_written,

	TP_PROTO(struct backing_dev_info *bdi, int written),

	TP_ARGS(bdi, written),

	TP_STRUCT__entry(
		__array(char,	name, 32)
		__field(int,	written)
	),

	TP_fast_assign(
		tp_memcpy(name, dev_name(bdi->dev), 32)
		tp_assign(written, written)
	),

	TP_printk("bdi %s written %d",
		  __entry->name,
		  __entry->written
	)
)
#endif

DECLARE_EVENT_CLASS(writeback_wbc_class,
	TP_PROTO(struct writeback_control *wbc, struct backing_dev_info *bdi),
	TP_ARGS(wbc, bdi),
	TP_STRUCT__entry(
		__array(char, name, 32)
		__field(long, nr_to_write)
		__field(long, pages_skipped)
		__field(int, sync_mode)
		__field(int, for_kupdate)
		__field(int, for_background)
		__field(int, for_reclaim)
		__field(int, range_cyclic)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0))
		__field(int, more_io)
		__field(unsigned long, older_than_this)
#endif
		__field(long, range_start)
		__field(long, range_end)
	),

	TP_fast_assign(
		tp_memcpy(name, dev_name(bdi->dev), 32)
		tp_assign(nr_to_write, wbc->nr_to_write)
		tp_assign(pages_skipped, wbc->pages_skipped)
		tp_assign(sync_mode, wbc->sync_mode)
		tp_assign(for_kupdate, wbc->for_kupdate)
		tp_assign(for_background, wbc->for_background)
		tp_assign(for_reclaim, wbc->for_reclaim)
		tp_assign(range_cyclic, wbc->range_cyclic)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0))
		tp_assign(more_io, wbc->more_io)
		tp_assign(older_than_this, wbc->older_than_this ?
						*wbc->older_than_this : 0)
#endif
		tp_assign(range_start, (long)wbc->range_start)
		tp_assign(range_end, (long)wbc->range_end)
	),

	TP_printk("bdi %s: towrt=%ld skip=%ld mode=%d kupd=%d "
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0))
		"bgrd=%d reclm=%d cyclic=%d more=%d older=0x%lx "
#else
		"bgrd=%d reclm=%d cyclic=%d "
#endif
		"start=0x%lx end=0x%lx",
		__entry->name,
		__entry->nr_to_write,
		__entry->pages_skipped,
		__entry->sync_mode,
		__entry->for_kupdate,
		__entry->for_background,
		__entry->for_reclaim,
		__entry->range_cyclic,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0))
		__entry->more_io,
		__entry->older_than_this,
#endif
		__entry->range_start,
		__entry->range_end)
)

#undef DEFINE_WBC_EVENT
#define DEFINE_WBC_EVENT(name, map) \
DEFINE_EVENT_MAP(writeback_wbc_class, name, map, \
	TP_PROTO(struct writeback_control *wbc, struct backing_dev_info *bdi), \
	TP_ARGS(wbc, bdi))
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0))
DEFINE_WBC_EVENT(wbc_writeback_start, writeback_wbc_writeback_start)
DEFINE_WBC_EVENT(wbc_writeback_written, writeback_wbc_writeback_written)
DEFINE_WBC_EVENT(wbc_writeback_wait, writeback_wbc_writeback_wait)
DEFINE_WBC_EVENT(wbc_balance_dirty_start, writeback_wbc_balance_dirty_start)
DEFINE_WBC_EVENT(wbc_balance_dirty_written, writeback_wbc_balance_dirty_written)
DEFINE_WBC_EVENT(wbc_balance_dirty_wait, writeback_wbc_balance_dirty_wait)
#endif
DEFINE_WBC_EVENT(wbc_writepage, writeback_wbc_writepage)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0))
TRACE_EVENT(writeback_queue_io,
	TP_PROTO(struct bdi_writeback *wb,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
		 struct wb_writeback_work *work,
#else
		 unsigned long *older_than_this,
#endif
		 int moved),
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
	TP_ARGS(wb, work, moved),
#else
	TP_ARGS(wb, older_than_this, moved),
#endif
	TP_STRUCT__entry(
		__array(char,		name, 32)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
#else
		__field(unsigned long,	older)
		__field(long,		age)
#endif
		__field(int,		moved)
	),
	TP_fast_assign(
		tp_memcpy(name, dev_name(wb->bdi->dev), 32)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
#else
		tp_assign(older, older_than_this ?  *older_than_this : 0)
		tp_assign(age, older_than_this ?
			(jiffies - *older_than_this) * 1000 / HZ : -1)
#endif
		tp_assign(moved, moved)
	),
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
	TP_printk("bdi %s: enqueue=%d",
		__entry->name,
		__entry->moved,
	)
#else
	TP_printk("bdi %s: older=%lu age=%ld enqueue=%d",
		__entry->name,
		__entry->older,	/* older_than_this in jiffies */
		__entry->age,	/* older_than_this in relative milliseconds */
		__entry->moved
	)
#endif
)

TRACE_EVENT_MAP(global_dirty_state,

	writeback_global_dirty_state,

	TP_PROTO(unsigned long background_thresh,
		 unsigned long dirty_thresh
	),

	TP_ARGS(background_thresh,
		dirty_thresh
	),

	TP_STRUCT__entry(
		__field(unsigned long,	nr_dirty)
		__field(unsigned long,	nr_writeback)
		__field(unsigned long,	nr_unstable)
		__field(unsigned long,	background_thresh)
		__field(unsigned long,	dirty_thresh)
		__field(unsigned long,	dirty_limit)
		__field(unsigned long,	nr_dirtied)
		__field(unsigned long,	nr_written)
	),

	TP_fast_assign(
		tp_assign(nr_dirty, global_page_state(NR_FILE_DIRTY))
		tp_assign(nr_writeback, global_page_state(NR_WRITEBACK))
		tp_assign(nr_unstable, global_page_state(NR_UNSTABLE_NFS))
		tp_assign(nr_dirtied, global_page_state(NR_DIRTIED))
		tp_assign(nr_written, global_page_state(NR_WRITTEN))
		tp_assign(background_thresh, background_thresh)
		tp_assign(dirty_thresh, dirty_thresh)
		tp_assign(dirty_limit, global_dirty_limit)
	),

	TP_printk("dirty=%lu writeback=%lu unstable=%lu "
		  "bg_thresh=%lu thresh=%lu limit=%lu "
		  "dirtied=%lu written=%lu",
		  __entry->nr_dirty,
		  __entry->nr_writeback,
		  __entry->nr_unstable,
		  __entry->background_thresh,
		  __entry->dirty_thresh,
		  __entry->dirty_limit,
		  __entry->nr_dirtied,
		  __entry->nr_written
	)
)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))

#define KBps(x)			((x) << (PAGE_SHIFT - 10))

TRACE_EVENT_MAP(bdi_dirty_ratelimit,

	writeback_bdi_dirty_ratelimit,

	TP_PROTO(struct backing_dev_info *bdi,
		 unsigned long dirty_rate,
		 unsigned long task_ratelimit),

	TP_ARGS(bdi, dirty_rate, task_ratelimit),

	TP_STRUCT__entry(
		__array(char,		bdi, 32)
		__field(unsigned long,	write_bw)
		__field(unsigned long,	avg_write_bw)
		__field(unsigned long,	dirty_rate)
		__field(unsigned long,	dirty_ratelimit)
		__field(unsigned long,	task_ratelimit)
		__field(unsigned long,	balanced_dirty_ratelimit)
	),

	TP_fast_assign(
		tp_memcpy(bdi, dev_name(bdi->dev), 32)
		tp_assign(write_bw, KBps(bdi->write_bandwidth))
		tp_assign(avg_write_bw, KBps(bdi->avg_write_bandwidth))
		tp_assign(dirty_rate, KBps(dirty_rate))
		tp_assign(dirty_ratelimit, KBps(bdi->dirty_ratelimit))
		tp_assign(task_ratelimit, KBps(task_ratelimit))
		tp_assign(balanced_dirty_ratelimit,
					KBps(bdi->balanced_dirty_ratelimit))
	),

	TP_printk("bdi %s: "
		  "write_bw=%lu awrite_bw=%lu dirty_rate=%lu "
		  "dirty_ratelimit=%lu task_ratelimit=%lu "
		  "balanced_dirty_ratelimit=%lu",
		  __entry->bdi,
		  __entry->write_bw,		/* write bandwidth */
		  __entry->avg_write_bw,	/* avg write bandwidth */
		  __entry->dirty_rate,		/* bdi dirty rate */
		  __entry->dirty_ratelimit,	/* base ratelimit */
		  __entry->task_ratelimit, /* ratelimit with position control */
		  __entry->balanced_dirty_ratelimit /* the balanced ratelimit */
	)
)

TRACE_EVENT_MAP(balance_dirty_pages,

	writeback_balance_dirty_pages,

	TP_PROTO(struct backing_dev_info *bdi,
		 unsigned long thresh,
		 unsigned long bg_thresh,
		 unsigned long dirty,
		 unsigned long bdi_thresh,
		 unsigned long bdi_dirty,
		 unsigned long dirty_ratelimit,
		 unsigned long task_ratelimit,
		 unsigned long dirtied,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0))
		 unsigned long period,
#endif
		 long pause,
		 unsigned long start_time),

	TP_ARGS(bdi, thresh, bg_thresh, dirty, bdi_thresh, bdi_dirty,
		dirty_ratelimit, task_ratelimit,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0))
		dirtied, period, pause, start_time),
#else
		dirtied, pause, start_time),
#endif
	TP_STRUCT__entry(
		__array(	 char,	bdi, 32)
		__field(unsigned long,	limit)
		__field(unsigned long,	setpoint)
		__field(unsigned long,	dirty)
		__field(unsigned long,	bdi_setpoint)
		__field(unsigned long,	bdi_dirty)
		__field(unsigned long,	dirty_ratelimit)
		__field(unsigned long,	task_ratelimit)
		__field(unsigned int,	dirtied)
		__field(unsigned int,	dirtied_pause)
		__field(unsigned long,	paused)
		__field(	 long,	pause)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0))
		__field(unsigned long,	period)
		__field(	 long,	think)
#endif
	),

	TP_fast_assign(
		tp_memcpy(bdi, dev_name(bdi->dev), 32)
		tp_assign(limit, global_dirty_limit)
		tp_assign(setpoint,
			(global_dirty_limit + (thresh + bg_thresh) / 2) / 2)
		tp_assign(dirty, dirty)
		tp_assign(bdi_setpoint,
			((global_dirty_limit + (thresh + bg_thresh) / 2) / 2) *
			bdi_thresh / (thresh + 1))
		tp_assign(bdi_dirty, bdi_dirty)
		tp_assign(dirty_ratelimit, KBps(dirty_ratelimit))
		tp_assign(task_ratelimit, KBps(task_ratelimit))
		tp_assign(dirtied, dirtied)
		tp_assign(dirtied_pause, current->nr_dirtied_pause)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0))
		tp_assign(think, current->dirty_paused_when == 0 ? 0 :
			(long)(jiffies - current->dirty_paused_when) * 1000/HZ)
		tp_assign(period, period * 1000 / HZ)
#endif
		tp_assign(pause, pause * 1000 / HZ)
		tp_assign(paused, (jiffies - start_time) * 1000 / HZ)
	),


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0))
	TP_printk("bdi %s: "
		  "limit=%lu setpoint=%lu dirty=%lu "
		  "bdi_setpoint=%lu bdi_dirty=%lu "
		  "dirty_ratelimit=%lu task_ratelimit=%lu "
		  "dirtied=%u dirtied_pause=%u "
		  "paused=%lu pause=%ld period=%lu think=%ld",
		  __entry->bdi,
		  __entry->limit,
		  __entry->setpoint,
		  __entry->dirty,
		  __entry->bdi_setpoint,
		  __entry->bdi_dirty,
		  __entry->dirty_ratelimit,
		  __entry->task_ratelimit,
		  __entry->dirtied,
		  __entry->dirtied_pause,
		  __entry->paused,	/* ms */
		  __entry->pause,	/* ms */
		  __entry->period,	/* ms */
		  __entry->think	/* ms */
	  )
#else
	TP_printk("bdi %s: "
		  "limit=%lu setpoint=%lu dirty=%lu "
		  "bdi_setpoint=%lu bdi_dirty=%lu "
		  "dirty_ratelimit=%lu task_ratelimit=%lu "
		  "dirtied=%u dirtied_pause=%u "
		  "paused=%lu pause=%ld",
		  __entry->bdi,
		  __entry->limit,
		  __entry->setpoint,
		  __entry->dirty,
		  __entry->bdi_setpoint,
		  __entry->bdi_dirty,
		  __entry->dirty_ratelimit,
		  __entry->task_ratelimit,
		  __entry->dirtied,
		  __entry->dirtied_pause,
		  __entry->paused,	/* ms */
		  __entry->pause	/* ms */
	  )
#endif
)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0))
TRACE_EVENT(writeback_sb_inodes_requeue,

	TP_PROTO(struct inode *inode),
	TP_ARGS(inode),

	TP_STRUCT__entry(
		__array(char, name, 32)
		__field(unsigned long, ino)
		__field(unsigned long, state)
		__field(unsigned long, dirtied_when)
	),

	TP_fast_assign(
		tp_memcpy(name, dev_name(inode_to_bdi(inode)->dev), 32)
		tp_assign(ino, inode->i_ino)
		tp_assign(state, inode->i_state)
		tp_assign(dirtied_when, inode->dirtied_when)
	),

	TP_printk("bdi %s: ino=%lu state=%s dirtied_when=%lu age=%lu",
		  __entry->name,
		  __entry->ino,
		  show_inode_state(__entry->state),
		  __entry->dirtied_when,
		  (jiffies - __entry->dirtied_when) / HZ
	)
)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
DECLARE_EVENT_CLASS(writeback_congest_waited_template,

	TP_PROTO(unsigned int usec_timeout, unsigned int usec_delayed),

	TP_ARGS(usec_timeout, usec_delayed),

	TP_STRUCT__entry(
		__field(	unsigned int,	usec_timeout	)
		__field(	unsigned int,	usec_delayed	)
	),

	TP_fast_assign(
		tp_assign(usec_timeout, usec_timeout)
		tp_assign(usec_delayed, usec_delayed)
	),

	TP_printk("usec_timeout=%u usec_delayed=%u",
			__entry->usec_timeout,
			__entry->usec_delayed)
)

DEFINE_EVENT(writeback_congest_waited_template, writeback_congestion_wait,

	TP_PROTO(unsigned int usec_timeout, unsigned int usec_delayed),

	TP_ARGS(usec_timeout, usec_delayed)
)

DEFINE_EVENT(writeback_congest_waited_template, writeback_wait_iff_congested,

	TP_PROTO(unsigned int usec_timeout, unsigned int usec_delayed),

	TP_ARGS(usec_timeout, usec_delayed)
)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0))
DECLARE_EVENT_CLASS(writeback_single_inode_template,

	TP_PROTO(struct inode *inode,
		 struct writeback_control *wbc,
		 unsigned long nr_to_write
	),

	TP_ARGS(inode, wbc, nr_to_write),

	TP_STRUCT__entry(
		__array(char, name, 32)
		__field(unsigned long, ino)
		__field(unsigned long, state)
		__field(unsigned long, dirtied_when)
		__field(unsigned long, writeback_index)
		__field(long, nr_to_write)
		__field(unsigned long, wrote)
	),

	TP_fast_assign(
		tp_memcpy(name, dev_name(inode_to_bdi(inode)->dev), 32)
		tp_assign(ino, inode->i_ino)
		tp_assign(state, inode->i_state)
		tp_assign(dirtied_when, inode->dirtied_when)
		tp_assign(writeback_index, inode->i_mapping->writeback_index)
		tp_assign(nr_to_write, nr_to_write)
		tp_assign(wrote, nr_to_write - wbc->nr_to_write)
	),

	TP_printk("bdi %s: ino=%lu state=%s dirtied_when=%lu age=%lu "
		  "index=%lu to_write=%ld wrote=%lu",
		  __entry->name,
		  __entry->ino,
		  show_inode_state(__entry->state),
		  __entry->dirtied_when,
		  (jiffies - __entry->dirtied_when) / HZ,
		  __entry->writeback_index,
		  __entry->nr_to_write,
		  __entry->wrote
	)
)

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0))
DEFINE_EVENT(writeback_single_inode_template, writeback_single_inode_requeue,
	TP_PROTO(struct inode *inode,
		struct writeback_control *wbc,
		unsigned long nr_to_write),
	TP_ARGS(inode, wbc, nr_to_write)
)
#endif

DEFINE_EVENT(writeback_single_inode_template, writeback_single_inode,
	TP_PROTO(struct inode *inode,
		 struct writeback_control *wbc,
		 unsigned long nr_to_write),
	TP_ARGS(inode, wbc, nr_to_write)
)
#endif

#endif /* _TRACE_WRITEBACK_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"
