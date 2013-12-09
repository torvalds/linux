#undef TRACE_SYSTEM
#define TRACE_SYSTEM jbd2

#if !defined(_TRACE_JBD2_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_JBD2_H

#include <linux/jbd2.h>
#include <linux/tracepoint.h>
#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
#ifndef _TRACE_JBD2_DEF
#define _TRACE_JBD2_DEF
struct transaction_chp_stats_s;
struct transaction_run_stats_s;
#endif
#endif

TRACE_EVENT(jbd2_checkpoint,

	TP_PROTO(journal_t *journal, int result),

	TP_ARGS(journal, result),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	int,	result			)
	),

	TP_fast_assign(
		tp_assign(dev, journal->j_fs_dev->bd_dev)
		tp_assign(result, result)
	),

	TP_printk("dev %d,%d result %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->result)
)

DECLARE_EVENT_CLASS(jbd2_commit,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	char,	sync_commit		  )
		__field(	int,	transaction		  )
	),

	TP_fast_assign(
		tp_assign(dev, journal->j_fs_dev->bd_dev)
		tp_assign(sync_commit, commit_transaction->t_synchronous_commit)
		tp_assign(transaction, commit_transaction->t_tid)
	),

	TP_printk("dev %d,%d transaction %d sync %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->transaction, __entry->sync_commit)
)

DEFINE_EVENT(jbd2_commit, jbd2_start_commit,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction)
)

DEFINE_EVENT(jbd2_commit, jbd2_commit_locking,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction)
)

DEFINE_EVENT(jbd2_commit, jbd2_commit_flushing,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction)
)

DEFINE_EVENT(jbd2_commit, jbd2_commit_logging,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction)
)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0))
DEFINE_EVENT(jbd2_commit, jbd2_drop_transaction,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction)
)
#endif

TRACE_EVENT(jbd2_end_commit,
	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	char,	sync_commit		  )
		__field(	int,	transaction		  )
		__field(	int,	head		  	  )
	),

	TP_fast_assign(
		tp_assign(dev, journal->j_fs_dev->bd_dev)
		tp_assign(sync_commit, commit_transaction->t_synchronous_commit)
		tp_assign(transaction, commit_transaction->t_tid)
		tp_assign(head, journal->j_tail_sequence)
	),

	TP_printk("dev %d,%d transaction %d sync %d head %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->transaction, __entry->sync_commit, __entry->head)
)

TRACE_EVENT(jbd2_submit_inode_data,
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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
TRACE_EVENT(jbd2_run_stats,
	TP_PROTO(dev_t dev, unsigned long tid,
		 struct transaction_run_stats_s *stats),

	TP_ARGS(dev, tid, stats),

	TP_STRUCT__entry(
		__field(		dev_t,	dev		)
		__field(	unsigned long,	tid		)
		__field(	unsigned long,	wait		)
		__field(	unsigned long,	running		)
		__field(	unsigned long,	locked		)
		__field(	unsigned long,	flushing	)
		__field(	unsigned long,	logging		)
		__field(		__u32,	handle_count	)
		__field(		__u32,	blocks		)
		__field(		__u32,	blocks_logged	)
	),

	TP_fast_assign(
		tp_assign(dev, dev)
		tp_assign(tid, tid)
		tp_assign(wait, stats->rs_wait)
		tp_assign(running, stats->rs_running)
		tp_assign(locked, stats->rs_locked)
		tp_assign(flushing, stats->rs_flushing)
		tp_assign(logging, stats->rs_logging)
		tp_assign(handle_count, stats->rs_handle_count)
		tp_assign(blocks, stats->rs_blocks)
		tp_assign(blocks_logged, stats->rs_blocks_logged)
	),

	TP_printk("dev %d,%d tid %lu wait %u running %u locked %u flushing %u "
		  "logging %u handle_count %u blocks %u blocks_logged %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->tid,
		  jiffies_to_msecs(__entry->wait),
		  jiffies_to_msecs(__entry->running),
		  jiffies_to_msecs(__entry->locked),
		  jiffies_to_msecs(__entry->flushing),
		  jiffies_to_msecs(__entry->logging),
		  __entry->handle_count, __entry->blocks,
		  __entry->blocks_logged)
)

TRACE_EVENT(jbd2_checkpoint_stats,
	TP_PROTO(dev_t dev, unsigned long tid,
		 struct transaction_chp_stats_s *stats),

	TP_ARGS(dev, tid, stats),

	TP_STRUCT__entry(
		__field(		dev_t,	dev		)
		__field(	unsigned long,	tid		)
		__field(	unsigned long,	chp_time	)
		__field(		__u32,	forced_to_close	)
		__field(		__u32,	written		)
		__field(		__u32,	dropped		)
	),

	TP_fast_assign(
		tp_assign(dev, dev)
		tp_assign(tid, tid)
		tp_assign(chp_time, stats->cs_chp_time)
		tp_assign(forced_to_close, stats->cs_forced_to_close)
		tp_assign(written, stats->cs_written)
		tp_assign(dropped, stats->cs_dropped)
	),

	TP_printk("dev %d,%d tid %lu chp_time %u forced_to_close %u "
		  "written %u dropped %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->tid,
		  jiffies_to_msecs(__entry->chp_time),
		  __entry->forced_to_close, __entry->written, __entry->dropped)
)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,34))
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0))
TRACE_EVENT(jbd2_update_log_tail,
#else
TRACE_EVENT(jbd2_cleanup_journal_tail,
#endif

	TP_PROTO(journal_t *journal, tid_t first_tid,
		 unsigned long block_nr, unsigned long freed),

	TP_ARGS(journal, first_tid, block_nr, freed),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	tid_t,	tail_sequence		)
		__field(	tid_t,	first_tid		)
		__field(unsigned long,	block_nr		)
		__field(unsigned long,	freed			)
	),

	TP_fast_assign(
		tp_assign(dev, journal->j_fs_dev->bd_dev)
		tp_assign(tail_sequence, journal->j_tail_sequence)
		tp_assign(first_tid, first_tid)
		tp_assign(block_nr, block_nr)
		tp_assign(freed, freed)
	),

	TP_printk("dev %d,%d from %u to %u offset %lu freed %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->tail_sequence, __entry->first_tid,
		  __entry->block_nr, __entry->freed)
)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0))
TRACE_EVENT(jbd2_write_superblock,

	TP_PROTO(journal_t *journal, int write_op),

	TP_ARGS(journal, write_op),

	TP_STRUCT__entry(
		__field(	dev_t,  dev			)
		__field(	  int,  write_op		)
	),

	TP_fast_assign(
		tp_assign(dev, journal->j_fs_dev->bd_dev)
		tp_assign(write_op, write_op)
	),

	TP_printk("dev %d,%d write_op %x", MAJOR(__entry->dev),
		  MINOR(__entry->dev), __entry->write_op)
)
#endif

#endif /* _TRACE_JBD2_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"
