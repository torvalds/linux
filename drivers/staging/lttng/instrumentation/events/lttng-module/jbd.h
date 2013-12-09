#undef TRACE_SYSTEM
#define TRACE_SYSTEM jbd

#if !defined(_TRACE_JBD_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_JBD_H

#include <linux/jbd.h>
#include <linux/tracepoint.h>
#include <linux/version.h>

TRACE_EVENT(jbd_checkpoint,

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
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->result)
)

DECLARE_EVENT_CLASS(jbd_commit,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0))
		__field(	char,	sync_commit		)
#endif
		__field(	int,	transaction		)
	),

	TP_fast_assign(
		tp_assign(dev, journal->j_fs_dev->bd_dev)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0))
		tp_assign(sync_commit, commit_transaction->t_synchronous_commit)
#endif
		tp_assign(transaction, commit_transaction->t_tid)
	),

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0))
	TP_printk("dev %d,%d transaction %d sync %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->transaction, __entry->sync_commit)
#else
	TP_printk("dev %d,%d transaction %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->transaction)
#endif
)

DEFINE_EVENT(jbd_commit, jbd_start_commit,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction)
)

DEFINE_EVENT(jbd_commit, jbd_commit_locking,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction)
)

DEFINE_EVENT(jbd_commit, jbd_commit_flushing,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction)
)

DEFINE_EVENT(jbd_commit, jbd_commit_logging,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction)
)

TRACE_EVENT(jbd_drop_transaction,

	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0))
		__field(	char,	sync_commit		)
#endif
		__field(	int,	transaction		)
	),

	TP_fast_assign(
		tp_assign(dev, journal->j_fs_dev->bd_dev)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0))
		tp_assign(sync_commit, commit_transaction->t_synchronous_commit)
#endif
		tp_assign(transaction, commit_transaction->t_tid)
	),

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0))
	TP_printk("dev %d,%d transaction %d sync %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->transaction, __entry->sync_commit)
#else
	TP_printk("dev %d,%d transaction %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->transaction)
#endif
)

TRACE_EVENT(jbd_end_commit,
	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0))
		__field(	char,	sync_commit		)
#endif
		__field(	int,	transaction		)
		__field(	int,	head			)
	),

	TP_fast_assign(
		tp_assign(dev, journal->j_fs_dev->bd_dev)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0))
		tp_assign(sync_commit, commit_transaction->t_synchronous_commit)
#endif
		tp_assign(transaction, commit_transaction->t_tid)
		tp_assign(head, journal->j_tail_sequence)
	),

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0))
	TP_printk("dev %d,%d transaction %d sync %d head %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->transaction, __entry->sync_commit, __entry->head)
#else
	TP_printk("dev %d,%d transaction %d head %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->transaction, __entry->head)
#endif
)

TRACE_EVENT(jbd_do_submit_data,
	TP_PROTO(journal_t *journal, transaction_t *commit_transaction),

	TP_ARGS(journal, commit_transaction),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0))
		__field(	char,	sync_commit		)
#endif
		__field(	int,	transaction		)
	),

	TP_fast_assign(
		tp_assign(dev, journal->j_fs_dev->bd_dev)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0))
		tp_assign(sync_commit, commit_transaction->t_synchronous_commit)
#endif
		tp_assign(transaction, commit_transaction->t_tid)
	),

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0))
	TP_printk("dev %d,%d transaction %d sync %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		   __entry->transaction, __entry->sync_commit)
#else
	TP_printk("dev %d,%d transaction %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		   __entry->transaction)
#endif
)

TRACE_EVENT(jbd_cleanup_journal_tail,

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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0))
TRACE_EVENT_MAP(journal_write_superblock,

	jbd_journal_write_superblock,

	TP_PROTO(journal_t *journal, int write_op),

	TP_ARGS(journal, write_op),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	int,	write_op		)
	),

	TP_fast_assign(
		tp_assign(dev, journal->j_fs_dev->bd_dev)
		tp_assign(write_op, write_op)
	),

	TP_printk("dev %d,%d write_op %x", MAJOR(__entry->dev),
		  MINOR(__entry->dev), __entry->write_op)
)
#else
TRACE_EVENT(jbd_update_superblock_end,
	TP_PROTO(journal_t *journal, int wait),

	TP_ARGS(journal, wait),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	int,	wait			)
	),

	TP_fast_assign(
		tp_assign(dev, journal->j_fs_dev->bd_dev)
		tp_assign(wait, wait)
	),

	TP_printk("dev %d,%d wait %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		   __entry->wait)
)
#endif

#endif /* _TRACE_JBD_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"
