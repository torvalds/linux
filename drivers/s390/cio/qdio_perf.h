/*
 *  drivers/s390/cio/qdio_perf.h
 *
 *  Copyright IBM Corp. 2008
 *
 *  Author: Jan Glauber (jang@linux.vnet.ibm.com)
 */
#ifndef QDIO_PERF_H
#define QDIO_PERF_H

#include <linux/types.h>
#include <linux/device.h>
#include <asm/atomic.h>

struct qdio_perf_stats {
	/* interrupt handler calls */
	atomic_long_t qdio_int;
	atomic_long_t pci_int;
	atomic_long_t thin_int;

	/* tasklet runs */
	atomic_long_t tasklet_inbound;
	atomic_long_t tasklet_outbound;
	atomic_long_t tasklet_thinint;
	atomic_long_t tasklet_thinint_loop;
	atomic_long_t thinint_inbound;
	atomic_long_t thinint_inbound_loop;
	atomic_long_t thinint_inbound_loop2;

	/* signal adapter calls */
	atomic_long_t siga_out;
	atomic_long_t siga_in;
	atomic_long_t siga_sync;

	/* misc */
	atomic_long_t inbound_handler;
	atomic_long_t outbound_handler;
	atomic_long_t fast_requeue;
	atomic_long_t outbound_target_full;

	/* for debugging */
	atomic_long_t debug_tl_out_timer;
	atomic_long_t debug_stop_polling;
	atomic_long_t debug_eqbs_all;
	atomic_long_t debug_eqbs_incomplete;
	atomic_long_t debug_sqbs_all;
	atomic_long_t debug_sqbs_incomplete;
};

extern struct qdio_perf_stats perf_stats;
extern int qdio_performance_stats;

int qdio_setup_perf_stats(void);
void qdio_remove_perf_stats(void);

extern void qdio_perf_stat_inc(atomic_long_t *count);
extern void qdio_perf_stat_dec(atomic_long_t *count);

#endif
