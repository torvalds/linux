#ifndef ISCSI_THREAD_QUEUE_H
#define ISCSI_THREAD_QUEUE_H

/*
 * Defines for thread sets.
 */
extern int iscsi_thread_set_force_reinstatement(struct iscsi_conn *);
extern void iscsi_add_ts_to_inactive_list(struct iscsi_thread_set *);
extern int iscsi_allocate_thread_sets(u32);
extern void iscsi_deallocate_thread_sets(void);
extern void iscsi_activate_thread_set(struct iscsi_conn *, struct iscsi_thread_set *);
extern struct iscsi_thread_set *iscsi_get_thread_set(void);
extern void iscsi_set_thread_clear(struct iscsi_conn *, u8);
extern void iscsi_set_thread_set_signal(struct iscsi_conn *, u8);
extern int iscsi_release_thread_set(struct iscsi_conn *);
extern struct iscsi_conn *iscsi_rx_thread_pre_handler(struct iscsi_thread_set *);
extern struct iscsi_conn *iscsi_tx_thread_pre_handler(struct iscsi_thread_set *);
extern int iscsi_thread_set_init(void);
extern void iscsi_thread_set_free(void);

extern int iscsi_target_tx_thread(void *);
extern int iscsi_target_rx_thread(void *);

#define TARGET_THREAD_SET_COUNT			4

#define ISCSI_RX_THREAD                         1
#define ISCSI_TX_THREAD                         2
#define ISCSI_RX_THREAD_NAME			"iscsi_trx"
#define ISCSI_TX_THREAD_NAME			"iscsi_ttx"
#define ISCSI_BLOCK_RX_THREAD			0x1
#define ISCSI_BLOCK_TX_THREAD			0x2
#define ISCSI_CLEAR_RX_THREAD			0x1
#define ISCSI_CLEAR_TX_THREAD			0x2
#define ISCSI_SIGNAL_RX_THREAD			0x1
#define ISCSI_SIGNAL_TX_THREAD			0x2

/* struct iscsi_thread_set->status */
#define ISCSI_THREAD_SET_FREE			1
#define ISCSI_THREAD_SET_ACTIVE			2
#define ISCSI_THREAD_SET_DIE			3
#define ISCSI_THREAD_SET_RESET			4
#define ISCSI_THREAD_SET_DEALLOCATE_THREADS	5

/* By default allow a maximum of 32K iSCSI connections */
#define ISCSI_TS_BITMAP_BITS			32768

struct iscsi_thread_set {
	/* flags used for blocking and restarting sets */
	int	blocked_threads;
	/* flag for creating threads */
	int	create_threads;
	/* flag for delaying readding to inactive list */
	int	delay_inactive;
	/* status for thread set */
	int	status;
	/* which threads have had signals sent */
	int	signal_sent;
	/* flag for which threads exited first */
	int	thread_clear;
	/* Active threads in the thread set */
	int	thread_count;
	/* Unique thread ID */
	u32	thread_id;
	/* pointer to connection if set is active */
	struct iscsi_conn	*conn;
	/* used for controlling ts state accesses */
	spinlock_t	ts_state_lock;
	/* Used for rx side post startup */
	struct completion	rx_post_start_comp;
	/* Used for tx side post startup */
	struct completion	tx_post_start_comp;
	/* used for restarting thread queue */
	struct completion	rx_restart_comp;
	/* used for restarting thread queue */
	struct completion	tx_restart_comp;
	/* used for normal unused blocking */
	struct completion	rx_start_comp;
	/* used for normal unused blocking */
	struct completion	tx_start_comp;
	/* OS descriptor for rx thread */
	struct task_struct	*rx_thread;
	/* OS descriptor for tx thread */
	struct task_struct	*tx_thread;
	/* struct iscsi_thread_set in list list head*/
	struct list_head	ts_list;
};

#endif   /*** ISCSI_THREAD_QUEUE_H ***/
