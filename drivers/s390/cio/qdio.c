/*
 *
 * linux/drivers/s390/cio/qdio.c
 *
 * Linux for S/390 QDIO base support, Hipersocket base support
 * version 2
 *
 * Copyright 2000,2002 IBM Corporation
 * Author(s):             Utz Bacher <utz.bacher@de.ibm.com>
 * 2.6 cio integration by Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 * Restriction: only 63 iqdio subchannels would have its own indicator,
 * after that, subsequent subchannels share one indicator
 *
 *
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/timer.h>
#include <linux/mempool.h>

#include <asm/ccwdev.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <asm/semaphore.h>
#include <asm/timex.h>

#include <asm/debug.h>
#include <asm/s390_rdev.h>
#include <asm/qdio.h>

#include "cio.h"
#include "css.h"
#include "device.h"
#include "airq.h"
#include "qdio.h"
#include "ioasm.h"
#include "chsc.h"

/****************** MODULE PARAMETER VARIABLES ********************/
MODULE_AUTHOR("Utz Bacher <utz.bacher@de.ibm.com>");
MODULE_DESCRIPTION("QDIO base support version 2, " \
		   "Copyright 2000 IBM Corporation");
MODULE_LICENSE("GPL");

/******************** HERE WE GO ***********************************/

static const char version[] = "QDIO base support version 2";

static int qdio_performance_stats = 0;
static int proc_perf_file_registration;
static struct qdio_perf_stats perf_stats;

static int hydra_thinints;
static int is_passthrough = 0;
static int omit_svs;

static int indicator_used[INDICATORS_PER_CACHELINE];
static __u32 * volatile indicators;
static __u32 volatile spare_indicator;
static atomic_t spare_indicator_usecount;
#define QDIO_MEMPOOL_SCSSC_ELEMENTS 2
static mempool_t *qdio_mempool_scssc;

static debug_info_t *qdio_dbf_setup;
static debug_info_t *qdio_dbf_sbal;
static debug_info_t *qdio_dbf_trace;
static debug_info_t *qdio_dbf_sense;
#ifdef CONFIG_QDIO_DEBUG
static debug_info_t *qdio_dbf_slsb_out;
static debug_info_t *qdio_dbf_slsb_in;
#endif /* CONFIG_QDIO_DEBUG */

/* iQDIO stuff: */
static volatile struct qdio_q *tiq_list=NULL; /* volatile as it could change
						 during a while loop */
static DEFINE_SPINLOCK(ttiq_list_lock);
static int register_thinint_result;
static void tiqdio_tl(unsigned long);
static DECLARE_TASKLET(tiqdio_tasklet,tiqdio_tl,0);

/* not a macro, as one of the arguments is atomic_read */
static inline int 
qdio_min(int a,int b)
{
	if (a<b)
		return a;
	else
		return b;
}

/***************** SCRUBBER HELPER ROUTINES **********************/
#ifdef CONFIG_64BIT
static inline void qdio_perf_stat_inc(atomic64_t *count)
{
	if (qdio_performance_stats)
		atomic64_inc(count);
}

static inline void qdio_perf_stat_dec(atomic64_t *count)
{
	if (qdio_performance_stats)
		atomic64_dec(count);
}
#else /* CONFIG_64BIT */
static inline void qdio_perf_stat_inc(atomic_t *count)
{
	if (qdio_performance_stats)
		atomic_inc(count);
}

static inline void qdio_perf_stat_dec(atomic_t *count)
{
	if (qdio_performance_stats)
		atomic_dec(count);
}
#endif /* CONFIG_64BIT */

static inline __u64 
qdio_get_micros(void)
{
	return (get_clock() >> 12); /* time>>12 is microseconds */
}

/* 
 * unfortunately, we can't just xchg the values; in do_QDIO we want to reserve
 * the q in any case, so that we'll not be interrupted when we are in
 * qdio_mark_tiq... shouldn't have a really bad impact, as reserving almost
 * ever works (last famous words) 
 */
static inline int 
qdio_reserve_q(struct qdio_q *q)
{
	return atomic_add_return(1,&q->use_count) - 1;
}

static inline void 
qdio_release_q(struct qdio_q *q)
{
	atomic_dec(&q->use_count);
}

/*check ccq  */
static int
qdio_check_ccq(struct qdio_q *q, unsigned int ccq)
{
	char dbf_text[15];

	if (ccq == 0 || ccq == 32)
		return 0;
	if (ccq == 96 || ccq == 97)
		return 1;
	/*notify devices immediately*/
	sprintf(dbf_text,"%d", ccq);
	QDIO_DBF_TEXT2(1,trace,dbf_text);
	return -EIO;
}
/* EQBS: extract buffer states */
static int
qdio_do_eqbs(struct qdio_q *q, unsigned char *state,
	     unsigned int *start, unsigned int *cnt)
{
	struct qdio_irq *irq;
	unsigned int tmp_cnt, q_no, ccq;
	int rc ;
	char dbf_text[15];

	ccq = 0;
	tmp_cnt = *cnt;
	irq = (struct qdio_irq*)q->irq_ptr;
	q_no = q->q_no;
	if(!q->is_input_q)
		q_no += irq->no_input_qs;
again:
	ccq = do_eqbs(irq->sch_token, state, q_no, start, cnt);
	rc = qdio_check_ccq(q, ccq);
	if (rc == 1) {
		QDIO_DBF_TEXT5(1,trace,"eqAGAIN");
		goto again;
	}
	if (rc < 0) {
                QDIO_DBF_TEXT2(1,trace,"eqberr");
                sprintf(dbf_text,"%2x,%2x,%d,%d",tmp_cnt, *cnt, ccq, q_no);
                QDIO_DBF_TEXT2(1,trace,dbf_text);
		q->handler(q->cdev,QDIO_STATUS_ACTIVATE_CHECK_CONDITION|
				QDIO_STATUS_LOOK_FOR_ERROR,
				0, 0, 0, -1, -1, q->int_parm);
		return 0;
	}
	return (tmp_cnt - *cnt);
}

/* SQBS: set buffer states */
static int
qdio_do_sqbs(struct qdio_q *q, unsigned char state,
	     unsigned int *start, unsigned int *cnt)
{
	struct qdio_irq *irq;
	unsigned int tmp_cnt, q_no, ccq;
	int rc;
	char dbf_text[15];

	ccq = 0;
	tmp_cnt = *cnt;
	irq = (struct qdio_irq*)q->irq_ptr;
	q_no = q->q_no;
	if(!q->is_input_q)
		q_no += irq->no_input_qs;
again:
	ccq = do_sqbs(irq->sch_token, state, q_no, start, cnt);
	rc = qdio_check_ccq(q, ccq);
	if (rc == 1) {
		QDIO_DBF_TEXT5(1,trace,"sqAGAIN");
		goto again;
	}
	if (rc < 0) {
		QDIO_DBF_TEXT3(1,trace,"sqberr");
		sprintf(dbf_text,"%2x,%2x",tmp_cnt,*cnt);
		QDIO_DBF_TEXT3(1,trace,dbf_text);
		sprintf(dbf_text,"%d,%d",ccq,q_no);
		QDIO_DBF_TEXT3(1,trace,dbf_text);
		q->handler(q->cdev,QDIO_STATUS_ACTIVATE_CHECK_CONDITION|
				QDIO_STATUS_LOOK_FOR_ERROR,
				0, 0, 0, -1, -1, q->int_parm);
		return 0;
	}
	return (tmp_cnt - *cnt);
}

static inline int
qdio_set_slsb(struct qdio_q *q, unsigned int *bufno,
	      unsigned char state, unsigned int *count)
{
	volatile char *slsb;
	struct qdio_irq *irq;

	irq = (struct qdio_irq*)q->irq_ptr;
	if (!irq->is_qebsm) {
		slsb = (char *)&q->slsb.acc.val[(*bufno)];
		xchg(slsb, state);
		return 1;
	}
	return qdio_do_sqbs(q, state, bufno, count);
}

#ifdef CONFIG_QDIO_DEBUG
static inline void
qdio_trace_slsb(struct qdio_q *q)
{
	if (q->queue_type==QDIO_TRACE_QTYPE) {
		if (q->is_input_q)
			QDIO_DBF_HEX2(0,slsb_in,&q->slsb,
				      QDIO_MAX_BUFFERS_PER_Q);
		else
			QDIO_DBF_HEX2(0,slsb_out,&q->slsb,
				      QDIO_MAX_BUFFERS_PER_Q);
	}
}
#endif

static inline int
set_slsb(struct qdio_q *q, unsigned int *bufno,
	 unsigned char state, unsigned int *count)
{
	int rc;
#ifdef CONFIG_QDIO_DEBUG
	qdio_trace_slsb(q);
#endif
	rc = qdio_set_slsb(q, bufno, state, count);
#ifdef CONFIG_QDIO_DEBUG
	qdio_trace_slsb(q);
#endif
	return rc;
}
static inline int 
qdio_siga_sync(struct qdio_q *q, unsigned int gpr2,
	       unsigned int gpr3)
{
	int cc;

	QDIO_DBF_TEXT4(0,trace,"sigasync");
	QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));

	qdio_perf_stat_inc(&perf_stats.siga_syncs);

	cc = do_siga_sync(q->schid, gpr2, gpr3);
	if (cc)
		QDIO_DBF_HEX3(0,trace,&cc,sizeof(int*));

	return cc;
}

static inline int
qdio_siga_sync_q(struct qdio_q *q)
{
	if (q->is_input_q)
		return qdio_siga_sync(q, 0, q->mask);
	return qdio_siga_sync(q, q->mask, 0);
}

static int
__do_siga_output(struct qdio_q *q, unsigned int *busy_bit)
{
       struct qdio_irq *irq;
       unsigned int fc = 0;
       unsigned long schid;

       irq = (struct qdio_irq *) q->irq_ptr;
       if (!irq->is_qebsm)
	       schid = *((u32 *)&q->schid);
       else {
	       schid = irq->sch_token;
	       fc |= 0x80;
       }
       return do_siga_output(schid, q->mask, busy_bit, fc);
}

/* 
 * returns QDIO_SIGA_ERROR_ACCESS_EXCEPTION as cc, when SIGA returns
 * an access exception 
 */
static int
qdio_siga_output(struct qdio_q *q)
{
	int cc;
	__u32 busy_bit;
	__u64 start_time=0;

	qdio_perf_stat_inc(&perf_stats.siga_outs);

	QDIO_DBF_TEXT4(0,trace,"sigaout");
	QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));

	for (;;) {
		cc = __do_siga_output(q, &busy_bit);
//QDIO_PRINT_ERR("cc=%x, busy=%x\n",cc,busy_bit);
		if ((cc==2) && (busy_bit) && (q->is_iqdio_q)) {
			if (!start_time) 
				start_time=NOW;
			if ((NOW-start_time)>QDIO_BUSY_BIT_PATIENCE)
				break;
		} else
			break;
	}
	
	if ((cc==2) && (busy_bit)) 
		cc |= QDIO_SIGA_ERROR_B_BIT_SET;

	if (cc)
		QDIO_DBF_HEX3(0,trace,&cc,sizeof(int*));

	return cc;
}

static int
qdio_siga_input(struct qdio_q *q)
{
	int cc;

	QDIO_DBF_TEXT4(0,trace,"sigain");
	QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));

	qdio_perf_stat_inc(&perf_stats.siga_ins);

	cc = do_siga_input(q->schid, q->mask);
	
	if (cc)
		QDIO_DBF_HEX3(0,trace,&cc,sizeof(int*));

	return cc;
}

/* locked by the locks in qdio_activate and qdio_cleanup */
static __u32 *
qdio_get_indicator(void)
{
	int i;

	for (i=1;i<INDICATORS_PER_CACHELINE;i++)
		if (!indicator_used[i]) {
			indicator_used[i]=1;
			return indicators+i;
		}
	atomic_inc(&spare_indicator_usecount);
	return (__u32 * volatile) &spare_indicator;
}

/* locked by the locks in qdio_activate and qdio_cleanup */
static void 
qdio_put_indicator(__u32 *addr)
{
	int i;

	if ( (addr) && (addr!=&spare_indicator) ) {
		i=addr-indicators;
		indicator_used[i]=0;
	}
	if (addr == &spare_indicator)
		atomic_dec(&spare_indicator_usecount);
}

static inline void
tiqdio_clear_summary_bit(__u32 *location)
{
	QDIO_DBF_TEXT5(0,trace,"clrsummb");
	QDIO_DBF_HEX5(0,trace,&location,sizeof(void*));

	xchg(location,0);
}

static inline  void
tiqdio_set_summary_bit(__u32 *location)
{
	QDIO_DBF_TEXT5(0,trace,"setsummb");
	QDIO_DBF_HEX5(0,trace,&location,sizeof(void*));

	xchg(location,-1);
}

static inline void 
tiqdio_sched_tl(void)
{
	tasklet_hi_schedule(&tiqdio_tasklet);
}

static void
qdio_mark_tiq(struct qdio_q *q)
{
	unsigned long flags;

	QDIO_DBF_TEXT4(0,trace,"mark iq");
	QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));

	spin_lock_irqsave(&ttiq_list_lock,flags);
	if (unlikely(atomic_read(&q->is_in_shutdown)))
		goto out_unlock;

	if (!q->is_input_q)
		goto out_unlock;

	if ((q->list_prev) || (q->list_next)) 
		goto out_unlock;

	if (!tiq_list) {
		tiq_list=q;
		q->list_prev=q;
		q->list_next=q;
	} else {
		q->list_next=tiq_list;
		q->list_prev=tiq_list->list_prev;
		tiq_list->list_prev->list_next=q;
		tiq_list->list_prev=q;
	}
	spin_unlock_irqrestore(&ttiq_list_lock,flags);

	tiqdio_set_summary_bit((__u32*)q->dev_st_chg_ind);
	tiqdio_sched_tl();
	return;
out_unlock:
	spin_unlock_irqrestore(&ttiq_list_lock,flags);
	return;
}

static inline void
qdio_mark_q(struct qdio_q *q)
{
	QDIO_DBF_TEXT4(0,trace,"mark q");
	QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));

	if (unlikely(atomic_read(&q->is_in_shutdown)))
		return;

	tasklet_schedule(&q->tasklet);
}

static int
qdio_stop_polling(struct qdio_q *q)
{
#ifdef QDIO_USE_PROCESSING_STATE
       unsigned int tmp, gsf, count = 1;
       unsigned char state = 0;
       struct qdio_irq *irq = (struct qdio_irq *) q->irq_ptr;

	if (!atomic_xchg(&q->polling,0))
		return 1;

	QDIO_DBF_TEXT4(0,trace,"stoppoll");
	QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));

	/* show the card that we are not polling anymore */
	if (!q->is_input_q)
		return 1;

       tmp = gsf = GET_SAVED_FRONTIER(q);
       tmp = ((tmp + QDIO_MAX_BUFFERS_PER_Q-1) & (QDIO_MAX_BUFFERS_PER_Q-1) );
       set_slsb(q, &tmp, SLSB_P_INPUT_NOT_INIT, &count);

	/* 
	 * we don't issue this SYNC_MEMORY, as we trust Rick T and
	 * moreover will not use the PROCESSING state under VM, so
	 * q->polling was 0 anyway
	 */
	/*SYNC_MEMORY;*/
       if (irq->is_qebsm) {
               count = 1;
               qdio_do_eqbs(q, &state, &gsf, &count);
       } else
               state = q->slsb.acc.val[gsf];
       if (state != SLSB_P_INPUT_PRIMED)
		return 1;
	/* 
	 * set our summary bit again, as otherwise there is a
	 * small window we can miss between resetting it and
	 * checking for PRIMED state 
	 */
	if (q->is_thinint_q)
		tiqdio_set_summary_bit((__u32*)q->dev_st_chg_ind);
	return 0;

#else /* QDIO_USE_PROCESSING_STATE */
	return 1;
#endif /* QDIO_USE_PROCESSING_STATE */
}

/* 
 * see the comment in do_QDIO and before qdio_reserve_q about the
 * sophisticated locking outside of unmark_q, so that we don't need to
 * disable the interrupts :-) 
*/
static void
qdio_unmark_q(struct qdio_q *q)
{
	unsigned long flags;

	QDIO_DBF_TEXT4(0,trace,"unmark q");
	QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));

	if ((!q->list_prev)||(!q->list_next))
		return;

	if ((q->is_thinint_q)&&(q->is_input_q)) {
		/* iQDIO */
		spin_lock_irqsave(&ttiq_list_lock,flags);
		/* in case cleanup has done this already and simultanously
		 * qdio_unmark_q is called from the interrupt handler, we've
		 * got to check this in this specific case again */
		if ((!q->list_prev)||(!q->list_next))
			goto out;
		if (q->list_next==q) {
			/* q was the only interesting q */
			tiq_list=NULL;
			q->list_next=NULL;
			q->list_prev=NULL;
		} else {
			q->list_next->list_prev=q->list_prev;
			q->list_prev->list_next=q->list_next;
			tiq_list=q->list_next;
			q->list_next=NULL;
			q->list_prev=NULL;
		}
out:
		spin_unlock_irqrestore(&ttiq_list_lock,flags);
	}
}

static inline unsigned long 
tiqdio_clear_global_summary(void)
{
	unsigned long time;

	QDIO_DBF_TEXT5(0,trace,"clrglobl");
	
	time = do_clear_global_summary();

	QDIO_DBF_HEX5(0,trace,&time,sizeof(unsigned long));

	return time;
}


/************************* OUTBOUND ROUTINES *******************************/
static int
qdio_qebsm_get_outbound_buffer_frontier(struct qdio_q *q)
{
        struct qdio_irq *irq;
        unsigned char state;
        unsigned int cnt, count, ftc;

        irq = (struct qdio_irq *) q->irq_ptr;
        if ((!q->is_iqdio_q) && (!q->hydra_gives_outbound_pcis))
                SYNC_MEMORY;

        ftc = q->first_to_check;
        count = qdio_min(atomic_read(&q->number_of_buffers_used),
                        (QDIO_MAX_BUFFERS_PER_Q-1));
        if (count == 0)
                return q->first_to_check;
        cnt = qdio_do_eqbs(q, &state, &ftc, &count);
        if (cnt == 0)
                return q->first_to_check;
        switch (state) {
        case SLSB_P_OUTPUT_ERROR:
                QDIO_DBF_TEXT3(0,trace,"outperr");
                atomic_sub(cnt , &q->number_of_buffers_used);
                if (q->qdio_error)
                        q->error_status_flags |=
                                QDIO_STATUS_MORE_THAN_ONE_QDIO_ERROR;
                q->qdio_error = SLSB_P_OUTPUT_ERROR;
                q->error_status_flags |= QDIO_STATUS_LOOK_FOR_ERROR;
                q->first_to_check = ftc;
                break;
        case SLSB_P_OUTPUT_EMPTY:
                QDIO_DBF_TEXT5(0,trace,"outpempt");
                atomic_sub(cnt, &q->number_of_buffers_used);
                q->first_to_check = ftc;
                break;
        case SLSB_CU_OUTPUT_PRIMED:
                /* all buffers primed */
                QDIO_DBF_TEXT5(0,trace,"outpprim");
                break;
        default:
                break;
        }
        QDIO_DBF_HEX4(0,trace,&q->first_to_check,sizeof(int));
        return q->first_to_check;
}

static int
qdio_qebsm_get_inbound_buffer_frontier(struct qdio_q *q)
{
        struct qdio_irq *irq;
        unsigned char state;
        int tmp, ftc, count, cnt;
        char dbf_text[15];


        irq = (struct qdio_irq *) q->irq_ptr;
        ftc = q->first_to_check;
        count = qdio_min(atomic_read(&q->number_of_buffers_used),
                        (QDIO_MAX_BUFFERS_PER_Q-1));
        if (count == 0)
                 return q->first_to_check;
        cnt = qdio_do_eqbs(q, &state, &ftc, &count);
        if (cnt == 0)
                 return q->first_to_check;
        switch (state) {
        case SLSB_P_INPUT_ERROR :
#ifdef CONFIG_QDIO_DEBUG
                QDIO_DBF_TEXT3(1,trace,"inperr");
                sprintf(dbf_text,"%2x,%2x",ftc,count);
                QDIO_DBF_TEXT3(1,trace,dbf_text);
#endif /* CONFIG_QDIO_DEBUG */
                if (q->qdio_error)
                        q->error_status_flags |=
                                QDIO_STATUS_MORE_THAN_ONE_QDIO_ERROR;
                q->qdio_error = SLSB_P_INPUT_ERROR;
                q->error_status_flags |= QDIO_STATUS_LOOK_FOR_ERROR;
                atomic_sub(cnt, &q->number_of_buffers_used);
                q->first_to_check = ftc;
                break;
        case SLSB_P_INPUT_PRIMED :
                QDIO_DBF_TEXT3(0,trace,"inptprim");
                sprintf(dbf_text,"%2x,%2x",ftc,count);
                QDIO_DBF_TEXT3(1,trace,dbf_text);
                tmp = 0;
                ftc = q->first_to_check;
#ifdef QDIO_USE_PROCESSING_STATE
		if (cnt > 1) {
			cnt -= 1;
			tmp = set_slsb(q, &ftc, SLSB_P_INPUT_NOT_INIT, &cnt);
			if (!tmp)
				break;
		}
		cnt = 1;
		tmp += set_slsb(q, &ftc,
			       SLSB_P_INPUT_PROCESSING, &cnt);
		atomic_set(&q->polling, 1);
#else
                tmp = set_slsb(q, &ftc, SLSB_P_INPUT_NOT_INIT, &cnt);
#endif
                atomic_sub(tmp, &q->number_of_buffers_used);
                q->first_to_check = ftc;
                break;
        case SLSB_CU_INPUT_EMPTY:
        case SLSB_P_INPUT_NOT_INIT:
        case SLSB_P_INPUT_PROCESSING:
                QDIO_DBF_TEXT5(0,trace,"inpnipro");
                break;
        default:
                break;
        }
        QDIO_DBF_HEX4(0,trace,&q->first_to_check,sizeof(int));
        return q->first_to_check;
}

static int
qdio_get_outbound_buffer_frontier(struct qdio_q *q)
{
	struct qdio_irq *irq;
        volatile char *slsb;
        unsigned int count = 1;
        int first_not_to_check, f, f_mod_no;
	char dbf_text[15];

	QDIO_DBF_TEXT4(0,trace,"getobfro");
	QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));

	irq = (struct qdio_irq *) q->irq_ptr;
	if (irq->is_qebsm)
		return qdio_qebsm_get_outbound_buffer_frontier(q);

	slsb=&q->slsb.acc.val[0];
	f_mod_no=f=q->first_to_check;
	/* 
	 * f points to already processed elements, so f+no_used is correct...
	 * ... but: we don't check 128 buffers, as otherwise
	 * qdio_has_outbound_q_moved would return 0 
	 */
	first_not_to_check=f+qdio_min(atomic_read(&q->number_of_buffers_used),
				      (QDIO_MAX_BUFFERS_PER_Q-1));

	if ((!q->is_iqdio_q)&&(!q->hydra_gives_outbound_pcis))
		SYNC_MEMORY;

check_next:
	if (f==first_not_to_check) 
		goto out;

	switch(slsb[f_mod_no]) {

        /* the adapter has not fetched the output yet */
	case SLSB_CU_OUTPUT_PRIMED:
		QDIO_DBF_TEXT5(0,trace,"outpprim");
		break;

	/* the adapter got it */
	case SLSB_P_OUTPUT_EMPTY:
		atomic_dec(&q->number_of_buffers_used);
		f++;
		f_mod_no=f&(QDIO_MAX_BUFFERS_PER_Q-1);
		QDIO_DBF_TEXT5(0,trace,"outpempt");
		goto check_next;

	case SLSB_P_OUTPUT_ERROR:
		QDIO_DBF_TEXT3(0,trace,"outperr");
		sprintf(dbf_text,"%x-%x-%x",f_mod_no,
			q->sbal[f_mod_no]->element[14].sbalf.value,
			q->sbal[f_mod_no]->element[15].sbalf.value);
		QDIO_DBF_TEXT3(1,trace,dbf_text);
		QDIO_DBF_HEX2(1,sbal,q->sbal[f_mod_no],256);

		/* kind of process the buffer */
		set_slsb(q, &f_mod_no, SLSB_P_OUTPUT_NOT_INIT, &count);

		/* 
		 * we increment the frontier, as this buffer
		 * was processed obviously 
		 */
		atomic_dec(&q->number_of_buffers_used);
		f_mod_no=(f_mod_no+1)&(QDIO_MAX_BUFFERS_PER_Q-1);

		if (q->qdio_error)
			q->error_status_flags|=
				QDIO_STATUS_MORE_THAN_ONE_QDIO_ERROR;
		q->qdio_error=SLSB_P_OUTPUT_ERROR;
		q->error_status_flags|=QDIO_STATUS_LOOK_FOR_ERROR;

		break;

	/* no new buffers */
	default:
		QDIO_DBF_TEXT5(0,trace,"outpni");
	}
out:
	return (q->first_to_check=f_mod_no);
}

/* all buffers are processed */
static int
qdio_is_outbound_q_done(struct qdio_q *q)
{
	int no_used;
#ifdef CONFIG_QDIO_DEBUG
	char dbf_text[15];
#endif

	no_used=atomic_read(&q->number_of_buffers_used);

#ifdef CONFIG_QDIO_DEBUG
	if (no_used) {
		sprintf(dbf_text,"oqisnt%02x",no_used);
		QDIO_DBF_TEXT4(0,trace,dbf_text);
	} else {
		QDIO_DBF_TEXT4(0,trace,"oqisdone");
	}
	QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));
#endif /* CONFIG_QDIO_DEBUG */
	return (no_used==0);
}

static int
qdio_has_outbound_q_moved(struct qdio_q *q)
{
	int i;

	i=qdio_get_outbound_buffer_frontier(q);

	if ( (i!=GET_SAVED_FRONTIER(q)) ||
	     (q->error_status_flags&QDIO_STATUS_LOOK_FOR_ERROR) ) {
		SAVE_FRONTIER(q,i);
		QDIO_DBF_TEXT4(0,trace,"oqhasmvd");
		QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));
		return 1;
	} else {
		QDIO_DBF_TEXT4(0,trace,"oqhsntmv");
		QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));
		return 0;
	}
}

static void
qdio_kick_outbound_q(struct qdio_q *q)
{
	int result;
#ifdef CONFIG_QDIO_DEBUG
	char dbf_text[15];

	QDIO_DBF_TEXT4(0,trace,"kickoutq");
	QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));
#endif /* CONFIG_QDIO_DEBUG */

	if (!q->siga_out)
		return;

	/* here's the story with cc=2 and busy bit set (thanks, Rick):
	 * VM's CP could present us cc=2 and busy bit set on SIGA-write
	 * during reconfiguration of their Guest LAN (only in HIPERS mode,
	 * QDIO mode is asynchronous -- cc=2 and busy bit there will take
	 * the queues down immediately; and not being under VM we have a
	 * problem on cc=2 and busy bit set right away).
	 *
	 * Therefore qdio_siga_output will try for a short time constantly,
	 * if such a condition occurs. If it doesn't change, it will
	 * increase the busy_siga_counter and save the timestamp, and
	 * schedule the queue for later processing (via mark_q, using the
	 * queue tasklet). __qdio_outbound_processing will check out the
	 * counter. If non-zero, it will call qdio_kick_outbound_q as often
	 * as the value of the counter. This will attempt further SIGA
	 * instructions. For each successful SIGA, the counter is
	 * decreased, for failing SIGAs the counter remains the same, after
	 * all.
	 * After some time of no movement, qdio_kick_outbound_q will
	 * finally fail and reflect corresponding error codes to call
	 * the upper layer module and have it take the queues down.
	 *
	 * Note that this is a change from the original HiperSockets design
	 * (saying cc=2 and busy bit means take the queues down), but in
	 * these days Guest LAN didn't exist... excessive cc=2 with busy bit
	 * conditions will still take the queues down, but the threshold is
	 * higher due to the Guest LAN environment.
	 */


	result=qdio_siga_output(q);

	switch (result) {
	case 0:
		/* went smooth this time, reset timestamp */
#ifdef CONFIG_QDIO_DEBUG
		QDIO_DBF_TEXT3(0,trace,"cc2reslv");
		sprintf(dbf_text,"%4x%2x%2x",q->schid.sch_no,q->q_no,
			atomic_read(&q->busy_siga_counter));
		QDIO_DBF_TEXT3(0,trace,dbf_text);
#endif /* CONFIG_QDIO_DEBUG */
		q->timing.busy_start=0;
		break;
	case (2|QDIO_SIGA_ERROR_B_BIT_SET):
		/* cc=2 and busy bit: */
		atomic_inc(&q->busy_siga_counter);

		/* if the last siga was successful, save
		 * timestamp here */
		if (!q->timing.busy_start)
			q->timing.busy_start=NOW;

		/* if we're in time, don't touch error_status_flags
		 * and siga_error */
		if (NOW-q->timing.busy_start<QDIO_BUSY_BIT_GIVE_UP) {
			qdio_mark_q(q);
			break;
		}
		QDIO_DBF_TEXT2(0,trace,"cc2REPRT");
#ifdef CONFIG_QDIO_DEBUG
		sprintf(dbf_text,"%4x%2x%2x",q->schid.sch_no,q->q_no,
			atomic_read(&q->busy_siga_counter));
		QDIO_DBF_TEXT3(0,trace,dbf_text);
#endif /* CONFIG_QDIO_DEBUG */
		/* else fallthrough and report error */
	default:
		/* for plain cc=1, 2 or 3: */
		if (q->siga_error)
			q->error_status_flags|=
				QDIO_STATUS_MORE_THAN_ONE_SIGA_ERROR;
		q->error_status_flags|=
			QDIO_STATUS_LOOK_FOR_ERROR;
		q->siga_error=result;
	}
}

static void
qdio_kick_outbound_handler(struct qdio_q *q)
{
	int start, end, real_end, count;
#ifdef CONFIG_QDIO_DEBUG
	char dbf_text[15];
#endif

	start = q->first_element_to_kick;
	/* last_move_ftc was just updated */
	real_end = GET_SAVED_FRONTIER(q);
	end = (real_end+QDIO_MAX_BUFFERS_PER_Q-1)&
		(QDIO_MAX_BUFFERS_PER_Q-1);
	count = (end+QDIO_MAX_BUFFERS_PER_Q+1-start)&
		(QDIO_MAX_BUFFERS_PER_Q-1);

#ifdef CONFIG_QDIO_DEBUG
	QDIO_DBF_TEXT4(0,trace,"kickouth");
	QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));

	sprintf(dbf_text,"s=%2xc=%2x",start,count);
	QDIO_DBF_TEXT4(0,trace,dbf_text);
#endif /* CONFIG_QDIO_DEBUG */

	if (q->state==QDIO_IRQ_STATE_ACTIVE)
		q->handler(q->cdev,QDIO_STATUS_OUTBOUND_INT|
			   q->error_status_flags,
			   q->qdio_error,q->siga_error,q->q_no,start,count,
			   q->int_parm);

	/* for the next time: */
	q->first_element_to_kick=real_end;
	q->qdio_error=0;
	q->siga_error=0;
	q->error_status_flags=0;
}

static void
__qdio_outbound_processing(struct qdio_q *q)
{
	int siga_attempts;

	QDIO_DBF_TEXT4(0,trace,"qoutproc");
	QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));

	if (unlikely(qdio_reserve_q(q))) {
		qdio_release_q(q);
		qdio_perf_stat_inc(&perf_stats.outbound_tl_runs_resched);
		/* as we're sissies, we'll check next time */
		if (likely(!atomic_read(&q->is_in_shutdown))) {
			qdio_mark_q(q);
			QDIO_DBF_TEXT4(0,trace,"busy,agn");
		}
		return;
	}
	qdio_perf_stat_inc(&perf_stats.outbound_tl_runs);
	qdio_perf_stat_inc(&perf_stats.tl_runs);

	/* see comment in qdio_kick_outbound_q */
	siga_attempts=atomic_read(&q->busy_siga_counter);
	while (siga_attempts) {
		atomic_dec(&q->busy_siga_counter);
		qdio_kick_outbound_q(q);
		siga_attempts--;
	}

	if (qdio_has_outbound_q_moved(q))
		qdio_kick_outbound_handler(q);

	if (q->queue_type == QDIO_ZFCP_QFMT) {
		if ((!q->hydra_gives_outbound_pcis) &&
		    (!qdio_is_outbound_q_done(q)))
			qdio_mark_q(q);
	}
	else if (((!q->is_iqdio_q) && (!q->is_pci_out)) ||
		 (q->queue_type == QDIO_IQDIO_QFMT_ASYNCH)) {
		/* 
		 * make sure buffer switch from PRIMED to EMPTY is noticed
		 * and outbound_handler is called
		 */
		if (qdio_is_outbound_q_done(q)) {
			del_timer(&q->timer);
		} else {
			if (!timer_pending(&q->timer))
				mod_timer(&q->timer, jiffies +
					  QDIO_FORCE_CHECK_TIMEOUT);
		}
	}

	qdio_release_q(q);
}

static void
qdio_outbound_processing(struct qdio_q *q)
{
	__qdio_outbound_processing(q);
}

/************************* INBOUND ROUTINES *******************************/


static int
qdio_get_inbound_buffer_frontier(struct qdio_q *q)
{
	struct qdio_irq *irq;
	int f,f_mod_no;
	volatile char *slsb;
	unsigned int count = 1;
	int first_not_to_check;
#ifdef CONFIG_QDIO_DEBUG
	char dbf_text[15];
#endif /* CONFIG_QDIO_DEBUG */
#ifdef QDIO_USE_PROCESSING_STATE
	int last_position=-1;
#endif /* QDIO_USE_PROCESSING_STATE */

	QDIO_DBF_TEXT4(0,trace,"getibfro");
	QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));

	irq = (struct qdio_irq *) q->irq_ptr;
	if (irq->is_qebsm)
		return qdio_qebsm_get_inbound_buffer_frontier(q);

	slsb=&q->slsb.acc.val[0];
	f_mod_no=f=q->first_to_check;
	/* 
	 * we don't check 128 buffers, as otherwise qdio_has_inbound_q_moved
	 * would return 0 
	 */
	first_not_to_check=f+qdio_min(atomic_read(&q->number_of_buffers_used),
				      (QDIO_MAX_BUFFERS_PER_Q-1));

	/* 
	 * we don't use this one, as a PCI or we after a thin interrupt
	 * will sync the queues
	 */
	/* SYNC_MEMORY;*/

check_next:
	f_mod_no=f&(QDIO_MAX_BUFFERS_PER_Q-1);
	if (f==first_not_to_check) 
		goto out;
	switch (slsb[f_mod_no]) {

	/* CU_EMPTY means frontier is reached */
	case SLSB_CU_INPUT_EMPTY:
		QDIO_DBF_TEXT5(0,trace,"inptempt");
		break;

	/* P_PRIMED means set slsb to P_PROCESSING and move on */
	case SLSB_P_INPUT_PRIMED:
		QDIO_DBF_TEXT5(0,trace,"inptprim");

#ifdef QDIO_USE_PROCESSING_STATE
		/* 
		 * as soon as running under VM, polling the input queues will
		 * kill VM in terms of CP overhead 
		 */
		if (q->siga_sync) {
			set_slsb(q, &f_mod_no, SLSB_P_INPUT_NOT_INIT, &count);
		} else {
			/* set the previous buffer to NOT_INIT. The current
			 * buffer will be set to PROCESSING at the end of
			 * this function to avoid further interrupts. */
			if (last_position>=0)
				set_slsb(q, &last_position,
					 SLSB_P_INPUT_NOT_INIT, &count);
			atomic_set(&q->polling,1);
			last_position=f_mod_no;
		}
#else /* QDIO_USE_PROCESSING_STATE */
		set_slsb(q, &f_mod_no, SLSB_P_INPUT_NOT_INIT, &count);
#endif /* QDIO_USE_PROCESSING_STATE */
		/* 
		 * not needed, as the inbound queue will be synced on the next
		 * siga-r, resp. tiqdio_is_inbound_q_done will do the siga-s
		 */
		/*SYNC_MEMORY;*/
		f++;
		atomic_dec(&q->number_of_buffers_used);
		goto check_next;

	case SLSB_P_INPUT_NOT_INIT:
	case SLSB_P_INPUT_PROCESSING:
		QDIO_DBF_TEXT5(0,trace,"inpnipro");
		break;

	/* P_ERROR means frontier is reached, break and report error */
	case SLSB_P_INPUT_ERROR:
#ifdef CONFIG_QDIO_DEBUG
		sprintf(dbf_text,"inperr%2x",f_mod_no);
		QDIO_DBF_TEXT3(1,trace,dbf_text);
#endif /* CONFIG_QDIO_DEBUG */
		QDIO_DBF_HEX2(1,sbal,q->sbal[f_mod_no],256);

		/* kind of process the buffer */
		set_slsb(q, &f_mod_no, SLSB_P_INPUT_NOT_INIT, &count);

		if (q->qdio_error)
			q->error_status_flags|=
				QDIO_STATUS_MORE_THAN_ONE_QDIO_ERROR;
		q->qdio_error=SLSB_P_INPUT_ERROR;
		q->error_status_flags|=QDIO_STATUS_LOOK_FOR_ERROR;

		/* we increment the frontier, as this buffer
		 * was processed obviously */
		f_mod_no=(f_mod_no+1)&(QDIO_MAX_BUFFERS_PER_Q-1);
		atomic_dec(&q->number_of_buffers_used);

#ifdef QDIO_USE_PROCESSING_STATE
		last_position=-1;
#endif /* QDIO_USE_PROCESSING_STATE */

		break;

	/* everything else means frontier not changed (HALTED or so) */
	default: 
		break;
	}
out:
	q->first_to_check=f_mod_no;

#ifdef QDIO_USE_PROCESSING_STATE
	if (last_position>=0)
		set_slsb(q, &last_position, SLSB_P_INPUT_PROCESSING, &count);
#endif /* QDIO_USE_PROCESSING_STATE */

	QDIO_DBF_HEX4(0,trace,&q->first_to_check,sizeof(int));

	return q->first_to_check;
}

static int
qdio_has_inbound_q_moved(struct qdio_q *q)
{
	int i;

	i=qdio_get_inbound_buffer_frontier(q);
	if ( (i!=GET_SAVED_FRONTIER(q)) ||
	     (q->error_status_flags&QDIO_STATUS_LOOK_FOR_ERROR) ) {
		SAVE_FRONTIER(q,i);
		if ((!q->siga_sync)&&(!q->hydra_gives_outbound_pcis))
			SAVE_TIMESTAMP(q);

		QDIO_DBF_TEXT4(0,trace,"inhasmvd");
		QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));
		return 1;
	} else {
		QDIO_DBF_TEXT4(0,trace,"inhsntmv");
		QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));
		return 0;
	}
}

/* means, no more buffers to be filled */
static int
tiqdio_is_inbound_q_done(struct qdio_q *q)
{
	int no_used;
	unsigned int start_buf, count;
	unsigned char state = 0;
	struct qdio_irq *irq = (struct qdio_irq *) q->irq_ptr;

#ifdef CONFIG_QDIO_DEBUG
	char dbf_text[15];
#endif

	no_used=atomic_read(&q->number_of_buffers_used);

	/* propagate the change from 82 to 80 through VM */
	SYNC_MEMORY;

#ifdef CONFIG_QDIO_DEBUG
	if (no_used) {
		sprintf(dbf_text,"iqisnt%02x",no_used);
		QDIO_DBF_TEXT4(0,trace,dbf_text);
	} else {
		QDIO_DBF_TEXT4(0,trace,"iniqisdo");
	}
	QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));
#endif /* CONFIG_QDIO_DEBUG */

	if (!no_used)
		return 1;
	if (!q->siga_sync && !irq->is_qebsm)
		/* we'll check for more primed buffers in qeth_stop_polling */
		return 0;
	if (irq->is_qebsm) {
		count = 1;
		start_buf = q->first_to_check;
		qdio_do_eqbs(q, &state, &start_buf, &count);
	} else
		state = q->slsb.acc.val[q->first_to_check];
	if (state != SLSB_P_INPUT_PRIMED)
		/* 
		 * nothing more to do, if next buffer is not PRIMED.
		 * note that we did a SYNC_MEMORY before, that there
		 * has been a sychnronization.
		 * we will return 0 below, as there is nothing to do
		 * (stop_polling not necessary, as we have not been
		 * using the PROCESSING state 
		 */
		return 0;

	/* 
	 * ok, the next input buffer is primed. that means, that device state 
	 * change indicator and adapter local summary are set, so we will find
	 * it next time.
	 * we will return 0 below, as there is nothing to do, except scheduling
	 * ourselves for the next time. 
	 */
	tiqdio_set_summary_bit((__u32*)q->dev_st_chg_ind);
	tiqdio_sched_tl();
	return 0;
}

static int
qdio_is_inbound_q_done(struct qdio_q *q)
{
	int no_used;
	unsigned int start_buf, count;
	unsigned char state = 0;
	struct qdio_irq *irq = (struct qdio_irq *) q->irq_ptr;

#ifdef CONFIG_QDIO_DEBUG
	char dbf_text[15];
#endif

	no_used=atomic_read(&q->number_of_buffers_used);

	/* 
	 * we need that one for synchronization with the adapter, as it
	 * does a kind of PCI avoidance 
	 */
	SYNC_MEMORY;

	if (!no_used) {
		QDIO_DBF_TEXT4(0,trace,"inqisdnA");
		QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));
		return 1;
	}
	if (irq->is_qebsm) {
		count = 1;
		start_buf = q->first_to_check;
		qdio_do_eqbs(q, &state, &start_buf, &count);
	} else
		state = q->slsb.acc.val[q->first_to_check];
	if (state == SLSB_P_INPUT_PRIMED) {
		/* we got something to do */
		QDIO_DBF_TEXT4(0,trace,"inqisntA");
		QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));
		return 0;
	}

	/* on VM, we don't poll, so the q is always done here */
	if (q->siga_sync)
		return 1;
	if (q->hydra_gives_outbound_pcis)
		return 1;

	/* 
	 * at this point we know, that inbound first_to_check
	 * has (probably) not moved (see qdio_inbound_processing) 
	 */
	if (NOW>GET_SAVED_TIMESTAMP(q)+q->timing.threshold) {
#ifdef CONFIG_QDIO_DEBUG
		QDIO_DBF_TEXT4(0,trace,"inqisdon");
		QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));
		sprintf(dbf_text,"pf%02xcn%02x",q->first_to_check,no_used);
		QDIO_DBF_TEXT4(0,trace,dbf_text);
#endif /* CONFIG_QDIO_DEBUG */
		return 1;
	} else {
#ifdef CONFIG_QDIO_DEBUG
		QDIO_DBF_TEXT4(0,trace,"inqisntd");
		QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));
		sprintf(dbf_text,"pf%02xcn%02x",q->first_to_check,no_used);
		QDIO_DBF_TEXT4(0,trace,dbf_text);
#endif /* CONFIG_QDIO_DEBUG */
		return 0;
	}
}

static void
qdio_kick_inbound_handler(struct qdio_q *q)
{
	int count, start, end, real_end, i;
#ifdef CONFIG_QDIO_DEBUG
	char dbf_text[15];
#endif

	QDIO_DBF_TEXT4(0,trace,"kickinh");
	QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));

  	start=q->first_element_to_kick;
 	real_end=q->first_to_check;
 	end=(real_end+QDIO_MAX_BUFFERS_PER_Q-1)&(QDIO_MAX_BUFFERS_PER_Q-1);
 
 	i=start;
	count=0;
 	while (1) {
 		count++;
 		if (i==end)
			break;
 		i=(i+1)&(QDIO_MAX_BUFFERS_PER_Q-1);
 	}

#ifdef CONFIG_QDIO_DEBUG
	sprintf(dbf_text,"s=%2xc=%2x",start,count);
	QDIO_DBF_TEXT4(0,trace,dbf_text);
#endif /* CONFIG_QDIO_DEBUG */

	if (likely(q->state==QDIO_IRQ_STATE_ACTIVE))
		q->handler(q->cdev,
			   QDIO_STATUS_INBOUND_INT|q->error_status_flags,
			   q->qdio_error,q->siga_error,q->q_no,start,count,
			   q->int_parm);

	/* for the next time: */
	q->first_element_to_kick=real_end;
	q->qdio_error=0;
	q->siga_error=0;
	q->error_status_flags=0;

	qdio_perf_stat_inc(&perf_stats.inbound_cnt);
}

static void
__tiqdio_inbound_processing(struct qdio_q *q, int spare_ind_was_set)
{
	struct qdio_irq *irq_ptr;
	struct qdio_q *oq;
	int i;

	QDIO_DBF_TEXT4(0,trace,"iqinproc");
	QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));

	/* 
	 * we first want to reserve the q, so that we know, that we don't
	 * interrupt ourselves and call qdio_unmark_q, as is_in_shutdown might
	 * be set 
	 */
	if (unlikely(qdio_reserve_q(q))) {
		qdio_release_q(q);
		qdio_perf_stat_inc(&perf_stats.inbound_thin_tl_runs_resched);
		/* 
		 * as we might just be about to stop polling, we make
		 * sure that we check again at least once more 
		 */
		tiqdio_sched_tl();
		return;
	}
	qdio_perf_stat_inc(&perf_stats.inbound_thin_tl_runs);
	if (unlikely(atomic_read(&q->is_in_shutdown))) {
		qdio_unmark_q(q);
		goto out;
	}

	/* 
	 * we reset spare_ind_was_set, when the queue does not use the
	 * spare indicator
	 */
	if (spare_ind_was_set)
		spare_ind_was_set = (q->dev_st_chg_ind == &spare_indicator);

	if (!(*(q->dev_st_chg_ind)) && !spare_ind_was_set)
		goto out;
	/*
	 * q->dev_st_chg_ind is the indicator, be it shared or not.
	 * only clear it, if indicator is non-shared
	 */
	if (!spare_ind_was_set)
		tiqdio_clear_summary_bit((__u32*)q->dev_st_chg_ind);

	if (q->hydra_gives_outbound_pcis) {
		if (!q->siga_sync_done_on_thinints) {
			SYNC_MEMORY_ALL;
		} else if ((!q->siga_sync_done_on_outb_tis)&&
			 (q->hydra_gives_outbound_pcis)) {
			SYNC_MEMORY_ALL_OUTB;
		}
	} else {
		SYNC_MEMORY;
	}
	/*
	 * maybe we have to do work on our outbound queues... at least
	 * we have to check the outbound-int-capable thinint-capable
	 * queues
	 */
	if (q->hydra_gives_outbound_pcis) {
		irq_ptr = (struct qdio_irq*)q->irq_ptr;
		for (i=0;i<irq_ptr->no_output_qs;i++) {
			oq = irq_ptr->output_qs[i];
			if (!qdio_is_outbound_q_done(oq)) {
				qdio_perf_stat_dec(&perf_stats.tl_runs);
				__qdio_outbound_processing(oq);
			}
		}
	}

	if (!qdio_has_inbound_q_moved(q))
		goto out;

	qdio_kick_inbound_handler(q);
	if (tiqdio_is_inbound_q_done(q))
		if (!qdio_stop_polling(q)) {
			/* 
			 * we set the flags to get into the stuff next time,
			 * see also comment in qdio_stop_polling 
			 */
			tiqdio_set_summary_bit((__u32*)q->dev_st_chg_ind);
			tiqdio_sched_tl();
		}
out:
	qdio_release_q(q);
}

static void
tiqdio_inbound_processing(struct qdio_q *q)
{
	__tiqdio_inbound_processing(q, atomic_read(&spare_indicator_usecount));
}

static void
__qdio_inbound_processing(struct qdio_q *q)
{
	int q_laps=0;

	QDIO_DBF_TEXT4(0,trace,"qinproc");
	QDIO_DBF_HEX4(0,trace,&q,sizeof(void*));

	if (unlikely(qdio_reserve_q(q))) {
		qdio_release_q(q);
		qdio_perf_stat_inc(&perf_stats.inbound_tl_runs_resched);
		/* as we're sissies, we'll check next time */
		if (likely(!atomic_read(&q->is_in_shutdown))) {
			qdio_mark_q(q);
			QDIO_DBF_TEXT4(0,trace,"busy,agn");
		}
		return;
	}
	qdio_perf_stat_inc(&perf_stats.inbound_tl_runs);
	qdio_perf_stat_inc(&perf_stats.tl_runs);

again:
	if (qdio_has_inbound_q_moved(q)) {
		qdio_kick_inbound_handler(q);
		if (!qdio_stop_polling(q)) {
			q_laps++;
			if (q_laps<QDIO_Q_LAPS) 
				goto again;
		}
		qdio_mark_q(q);
	} else {
		if (!qdio_is_inbound_q_done(q)) 
                        /* means poll time is not yet over */
			qdio_mark_q(q);
	}

	qdio_release_q(q);
}

static void
qdio_inbound_processing(struct qdio_q *q)
{
	__qdio_inbound_processing(q);
}

/************************* MAIN ROUTINES *******************************/

#ifdef QDIO_USE_PROCESSING_STATE
static int
tiqdio_reset_processing_state(struct qdio_q *q, int q_laps)
{
	if (!q) {
		tiqdio_sched_tl();
		return 0;
	}

	/* 
	 * under VM, we have not used the PROCESSING state, so no
	 * need to stop polling 
	 */
	if (q->siga_sync)
		return 2;

	if (unlikely(qdio_reserve_q(q))) {
		qdio_release_q(q);
		qdio_perf_stat_inc(&perf_stats.inbound_thin_tl_runs_resched);
		/* 
		 * as we might just be about to stop polling, we make
		 * sure that we check again at least once more 
		 */
		
		/* 
		 * sanity -- we'd get here without setting the
		 * dev st chg ind 
		 */
		tiqdio_set_summary_bit((__u32*)q->dev_st_chg_ind);
		tiqdio_sched_tl();
		return 0;
	}
	if (qdio_stop_polling(q)) {
		qdio_release_q(q);
		return 2;
	}		
	if (q_laps<QDIO_Q_LAPS-1) {
		qdio_release_q(q);
		return 3;
	}
	/* 
	 * we set the flags to get into the stuff
	 * next time, see also comment in qdio_stop_polling 
	 */
	tiqdio_set_summary_bit((__u32*)q->dev_st_chg_ind);
	tiqdio_sched_tl();
	qdio_release_q(q);
	return 1;
	
}
#endif /* QDIO_USE_PROCESSING_STATE */

static void
tiqdio_inbound_checks(void)
{
	struct qdio_q *q;
	int spare_ind_was_set=0;
#ifdef QDIO_USE_PROCESSING_STATE
	int q_laps=0;
#endif /* QDIO_USE_PROCESSING_STATE */

	QDIO_DBF_TEXT4(0,trace,"iqdinbck");
	QDIO_DBF_TEXT5(0,trace,"iqlocsum");

#ifdef QDIO_USE_PROCESSING_STATE
again:
#endif /* QDIO_USE_PROCESSING_STATE */

	/* when the spare indicator is used and set, save that and clear it */
	if ((atomic_read(&spare_indicator_usecount)) && spare_indicator) {
		spare_ind_was_set = 1;
		tiqdio_clear_summary_bit((__u32*)&spare_indicator);
	}

	q=(struct qdio_q*)tiq_list;
	do {
		if (!q)
			break;
		__tiqdio_inbound_processing(q, spare_ind_was_set);
		q=(struct qdio_q*)q->list_next;
	} while (q!=(struct qdio_q*)tiq_list);

#ifdef QDIO_USE_PROCESSING_STATE
	q=(struct qdio_q*)tiq_list;
	do {
		int ret;

		ret = tiqdio_reset_processing_state(q, q_laps);
		switch (ret) {
		case 0:
			return;
		case 1:
			q_laps++;
		case 2:
			q = (struct qdio_q*)q->list_next;
			break;
		default:
			q_laps++;
			goto again;
		}
	} while (q!=(struct qdio_q*)tiq_list);
#endif /* QDIO_USE_PROCESSING_STATE */
}

static void
tiqdio_tl(unsigned long data)
{
	QDIO_DBF_TEXT4(0,trace,"iqdio_tl");

	qdio_perf_stat_inc(&perf_stats.tl_runs);

	tiqdio_inbound_checks();
}

/********************* GENERAL HELPER_ROUTINES ***********************/

static void
qdio_release_irq_memory(struct qdio_irq *irq_ptr)
{
	int i;

	for (i=0;i<QDIO_MAX_QUEUES_PER_IRQ;i++) {
		if (!irq_ptr->input_qs[i])
			goto next;

		kfree(irq_ptr->input_qs[i]->slib);
		kfree(irq_ptr->input_qs[i]);

next:
		if (!irq_ptr->output_qs[i])
			continue;

		kfree(irq_ptr->output_qs[i]->slib);
		kfree(irq_ptr->output_qs[i]);

	}
	kfree(irq_ptr->qdr);
	free_page((unsigned long) irq_ptr);
}

static void
qdio_set_impl_params(struct qdio_irq *irq_ptr,
		     unsigned int qib_param_field_format,
		     /* pointer to 128 bytes or NULL, if no param field */
		     unsigned char *qib_param_field,
		     /* pointer to no_queues*128 words of data or NULL */
		     unsigned int no_input_qs,
		     unsigned int no_output_qs,
		     unsigned long *input_slib_elements,
		     unsigned long *output_slib_elements)
{
	int i,j;

	if (!irq_ptr)
		return;

	irq_ptr->qib.pfmt=qib_param_field_format;
	if (qib_param_field)
		memcpy(irq_ptr->qib.parm,qib_param_field,
		       QDIO_MAX_BUFFERS_PER_Q);

	if (input_slib_elements)
		for (i=0;i<no_input_qs;i++) {
			for (j=0;j<QDIO_MAX_BUFFERS_PER_Q;j++)
				irq_ptr->input_qs[i]->slib->slibe[j].parms=
					input_slib_elements[
						i*QDIO_MAX_BUFFERS_PER_Q+j];
		}
	if (output_slib_elements)
		for (i=0;i<no_output_qs;i++) {
			for (j=0;j<QDIO_MAX_BUFFERS_PER_Q;j++)
				irq_ptr->output_qs[i]->slib->slibe[j].parms=
					output_slib_elements[
						i*QDIO_MAX_BUFFERS_PER_Q+j];
		}
}

static int
qdio_alloc_qs(struct qdio_irq *irq_ptr,
	      int no_input_qs, int no_output_qs)
{
	int i;
	struct qdio_q *q;
	int result=-ENOMEM;

	for (i=0;i<no_input_qs;i++) {
		q = kzalloc(sizeof(struct qdio_q), GFP_KERNEL);

		if (!q) {
			QDIO_PRINT_ERR("kmalloc of q failed!\n");
			goto out;
		}

		q->slib = kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (!q->slib) {
			QDIO_PRINT_ERR("kmalloc of slib failed!\n");
			goto out;
		}

		irq_ptr->input_qs[i]=q;
	}

	for (i=0;i<no_output_qs;i++) {
		q = kzalloc(sizeof(struct qdio_q), GFP_KERNEL);

		if (!q) {
			goto out;
		}

		q->slib=kmalloc(PAGE_SIZE,GFP_KERNEL);
		if (!q->slib) {
			QDIO_PRINT_ERR("kmalloc of slib failed!\n");
			goto out;
		}

		irq_ptr->output_qs[i]=q;
	}

	result=0;
out:
	return result;
}

static void
qdio_fill_qs(struct qdio_irq *irq_ptr, struct ccw_device *cdev,
       	     int no_input_qs, int no_output_qs,
	     qdio_handler_t *input_handler,
	     qdio_handler_t *output_handler,
	     unsigned long int_parm,int q_format,
	     unsigned long flags,
	     void **inbound_sbals_array,
	     void **outbound_sbals_array)
{
	struct qdio_q *q;
	int i,j;
	char dbf_text[20]; /* see qdio_initialize */
	void *ptr;
	int available;

	sprintf(dbf_text,"qfqs%4x",cdev->private->schid.sch_no);
	QDIO_DBF_TEXT0(0,setup,dbf_text);
	for (i=0;i<no_input_qs;i++) {
		q=irq_ptr->input_qs[i];

		memset(q,0,((char*)&q->slib)-((char*)q));
		sprintf(dbf_text,"in-q%4x",i);
		QDIO_DBF_TEXT0(0,setup,dbf_text);
		QDIO_DBF_HEX0(0,setup,&q,sizeof(void*));

		memset(q->slib,0,PAGE_SIZE);
		q->sl=(struct sl*)(((char*)q->slib)+PAGE_SIZE/2);

		available=0;

		for (j=0;j<QDIO_MAX_BUFFERS_PER_Q;j++)
			q->sbal[j]=*(inbound_sbals_array++);

                q->queue_type=q_format;
		q->int_parm=int_parm;
		q->schid = irq_ptr->schid;
		q->irq_ptr = irq_ptr;
		q->cdev = cdev;
		q->mask=1<<(31-i);
		q->q_no=i;
		q->is_input_q=1;
		q->first_to_check=0;
		q->last_move_ftc=0;
		q->handler=input_handler;
		q->dev_st_chg_ind=irq_ptr->dev_st_chg_ind;

		q->tasklet.data=(unsigned long)q;
		/* q->is_thinint_q isn't valid at this time, but
		 * irq_ptr->is_thinint_irq is */
		q->tasklet.func=(void(*)(unsigned long))
			((irq_ptr->is_thinint_irq)?&tiqdio_inbound_processing:
			 &qdio_inbound_processing);

		/* actually this is not used for inbound queues. yet. */
		atomic_set(&q->busy_siga_counter,0);
		q->timing.busy_start=0;

/*		for (j=0;j<QDIO_STATS_NUMBER;j++)
			q->timing.last_transfer_times[j]=(qdio_get_micros()/
							  QDIO_STATS_NUMBER)*j;
		q->timing.last_transfer_index=QDIO_STATS_NUMBER-1;
*/

		/* fill in slib */
		if (i>0) irq_ptr->input_qs[i-1]->slib->nsliba=
				 (unsigned long)(q->slib);
		q->slib->sla=(unsigned long)(q->sl);
		q->slib->slsba=(unsigned long)(&q->slsb.acc.val[0]);

		/* fill in sl */
		for (j=0;j<QDIO_MAX_BUFFERS_PER_Q;j++)
			q->sl->element[j].sbal=(unsigned long)(q->sbal[j]);

		QDIO_DBF_TEXT2(0,setup,"sl-sb-b0");
		ptr=(void*)q->sl;
		QDIO_DBF_HEX2(0,setup,&ptr,sizeof(void*));
		ptr=(void*)&q->slsb;
		QDIO_DBF_HEX2(0,setup,&ptr,sizeof(void*));
		ptr=(void*)q->sbal[0];
		QDIO_DBF_HEX2(0,setup,&ptr,sizeof(void*));

		/* fill in slsb */
		if (!irq_ptr->is_qebsm) {
                        unsigned int count = 1;
                        for (j = 0; j < QDIO_MAX_BUFFERS_PER_Q; j++)
                                set_slsb(q, &j, SLSB_P_INPUT_NOT_INIT, &count);
                }
	}

	for (i=0;i<no_output_qs;i++) {
		q=irq_ptr->output_qs[i];
		memset(q,0,((char*)&q->slib)-((char*)q));

		sprintf(dbf_text,"outq%4x",i);
		QDIO_DBF_TEXT0(0,setup,dbf_text);
		QDIO_DBF_HEX0(0,setup,&q,sizeof(void*));

		memset(q->slib,0,PAGE_SIZE);
		q->sl=(struct sl*)(((char*)q->slib)+PAGE_SIZE/2);

		available=0;
		
		for (j=0;j<QDIO_MAX_BUFFERS_PER_Q;j++)
			q->sbal[j]=*(outbound_sbals_array++);

                q->queue_type=q_format;
		if ((q->queue_type == QDIO_IQDIO_QFMT) &&
		    (no_output_qs > 1) &&
		    (i == no_output_qs-1))
			q->queue_type = QDIO_IQDIO_QFMT_ASYNCH;
		q->int_parm=int_parm;
		q->is_input_q=0;
		q->is_pci_out = 0;
		q->schid = irq_ptr->schid;
		q->cdev = cdev;
		q->irq_ptr = irq_ptr;
		q->mask=1<<(31-i);
		q->q_no=i;
		q->first_to_check=0;
		q->last_move_ftc=0;
		q->handler=output_handler;

		q->tasklet.data=(unsigned long)q;
		q->tasklet.func=(void(*)(unsigned long))
			&qdio_outbound_processing;
		q->timer.function=(void(*)(unsigned long))
			&qdio_outbound_processing;
		q->timer.data = (long)q;
		init_timer(&q->timer);

		atomic_set(&q->busy_siga_counter,0);
		q->timing.busy_start=0;

		/* fill in slib */
		if (i>0) irq_ptr->output_qs[i-1]->slib->nsliba=
				 (unsigned long)(q->slib);
		q->slib->sla=(unsigned long)(q->sl);
		q->slib->slsba=(unsigned long)(&q->slsb.acc.val[0]);

		/* fill in sl */
		for (j=0;j<QDIO_MAX_BUFFERS_PER_Q;j++)
			q->sl->element[j].sbal=(unsigned long)(q->sbal[j]);

		QDIO_DBF_TEXT2(0,setup,"sl-sb-b0");
		ptr=(void*)q->sl;
		QDIO_DBF_HEX2(0,setup,&ptr,sizeof(void*));
		ptr=(void*)&q->slsb;
		QDIO_DBF_HEX2(0,setup,&ptr,sizeof(void*));
		ptr=(void*)q->sbal[0];
		QDIO_DBF_HEX2(0,setup,&ptr,sizeof(void*));

		/* fill in slsb */
                if (!irq_ptr->is_qebsm) {
                        unsigned int count = 1;
                        for (j = 0; j < QDIO_MAX_BUFFERS_PER_Q; j++)
                                set_slsb(q, &j, SLSB_P_OUTPUT_NOT_INIT, &count);
                }
	}
}

static void
qdio_fill_thresholds(struct qdio_irq *irq_ptr,
		     unsigned int no_input_qs,
		     unsigned int no_output_qs,
		     unsigned int min_input_threshold,
		     unsigned int max_input_threshold,
		     unsigned int min_output_threshold,
		     unsigned int max_output_threshold)
{
	int i;
	struct qdio_q *q;

	for (i=0;i<no_input_qs;i++) {
		q=irq_ptr->input_qs[i];
		q->timing.threshold=max_input_threshold;
/*		for (j=0;j<QDIO_STATS_CLASSES;j++) {
			q->threshold_classes[j].threshold=
				min_input_threshold+
				(max_input_threshold-min_input_threshold)/
				QDIO_STATS_CLASSES;
		}
		qdio_use_thresholds(q,QDIO_STATS_CLASSES/2);*/
	}
	for (i=0;i<no_output_qs;i++) {
		q=irq_ptr->output_qs[i];
		q->timing.threshold=max_output_threshold;
/*		for (j=0;j<QDIO_STATS_CLASSES;j++) {
			q->threshold_classes[j].threshold=
				min_output_threshold+
				(max_output_threshold-min_output_threshold)/
				QDIO_STATS_CLASSES;
		}
		qdio_use_thresholds(q,QDIO_STATS_CLASSES/2);*/
	}
}

static int
tiqdio_thinint_handler(void)
{
	QDIO_DBF_TEXT4(0,trace,"thin_int");

	qdio_perf_stat_inc(&perf_stats.thinints);

	/* SVS only when needed:
	 * issue SVS to benefit from iqdio interrupt avoidance
	 * (SVS clears AISOI)*/
	if (!omit_svs)
		tiqdio_clear_global_summary();

	tiqdio_inbound_checks();
	return 0;
}

static void
qdio_set_state(struct qdio_irq *irq_ptr, enum qdio_irq_states state)
{
	int i;
#ifdef CONFIG_QDIO_DEBUG
	char dbf_text[15];

	QDIO_DBF_TEXT5(0,trace,"newstate");
	sprintf(dbf_text,"%4x%4x",irq_ptr->schid.sch_no,state);
	QDIO_DBF_TEXT5(0,trace,dbf_text);
#endif /* CONFIG_QDIO_DEBUG */

	irq_ptr->state=state;
	for (i=0;i<irq_ptr->no_input_qs;i++)
		irq_ptr->input_qs[i]->state=state;
	for (i=0;i<irq_ptr->no_output_qs;i++)
		irq_ptr->output_qs[i]->state=state;
	mb();
}

static void
qdio_irq_check_sense(struct subchannel_id schid, struct irb *irb)
{
	char dbf_text[15];

	if (irb->esw.esw0.erw.cons) {
		sprintf(dbf_text,"sens%4x",schid.sch_no);
		QDIO_DBF_TEXT2(1,trace,dbf_text);
		QDIO_DBF_HEX0(0,sense,irb,QDIO_DBF_SENSE_LEN);

		QDIO_PRINT_WARN("sense data available on qdio channel.\n");
		QDIO_HEXDUMP16(WARN,"irb: ",irb);
		QDIO_HEXDUMP16(WARN,"sense data: ",irb->ecw);
	}
		
}

static void
qdio_handle_pci(struct qdio_irq *irq_ptr)
{
	int i;
	struct qdio_q *q;

	qdio_perf_stat_inc(&perf_stats.pcis);
	for (i=0;i<irq_ptr->no_input_qs;i++) {
		q=irq_ptr->input_qs[i];
		if (q->is_input_q&QDIO_FLAG_NO_INPUT_INTERRUPT_CONTEXT)
			qdio_mark_q(q);
		else {
			qdio_perf_stat_dec(&perf_stats.tl_runs);
			__qdio_inbound_processing(q);
		}
	}
	if (!irq_ptr->hydra_gives_outbound_pcis)
		return;
	for (i=0;i<irq_ptr->no_output_qs;i++) {
		q=irq_ptr->output_qs[i];
		if (qdio_is_outbound_q_done(q))
			continue;
		qdio_perf_stat_dec(&perf_stats.tl_runs);
		if (!irq_ptr->sync_done_on_outb_pcis)
			SYNC_MEMORY;
		__qdio_outbound_processing(q);
	}
}

static void qdio_establish_handle_irq(struct ccw_device*, int, int);

static void
qdio_handle_activate_check(struct ccw_device *cdev, unsigned long intparm,
			   int cstat, int dstat)
{
	struct qdio_irq *irq_ptr;
	struct qdio_q *q;
	char dbf_text[15];

	irq_ptr = cdev->private->qdio_data;

	QDIO_DBF_TEXT2(1, trace, "ick2");
	sprintf(dbf_text,"%s", cdev->dev.bus_id);
	QDIO_DBF_TEXT2(1,trace,dbf_text);
	QDIO_DBF_HEX2(0,trace,&intparm,sizeof(int));
	QDIO_DBF_HEX2(0,trace,&dstat,sizeof(int));
	QDIO_DBF_HEX2(0,trace,&cstat,sizeof(int));
	QDIO_PRINT_ERR("received check condition on activate " \
		       "queues on device %s (cs=x%x, ds=x%x).\n",
		       cdev->dev.bus_id, cstat, dstat);
	if (irq_ptr->no_input_qs) {
		q=irq_ptr->input_qs[0];
	} else if (irq_ptr->no_output_qs) {
		q=irq_ptr->output_qs[0];
	} else {
		QDIO_PRINT_ERR("oops... no queue registered for device %s!?\n",
			       cdev->dev.bus_id);
		goto omit_handler_call;
	}
	q->handler(q->cdev,QDIO_STATUS_ACTIVATE_CHECK_CONDITION|
		   QDIO_STATUS_LOOK_FOR_ERROR,
		   0,0,0,-1,-1,q->int_parm);
omit_handler_call:
	qdio_set_state(irq_ptr,QDIO_IRQ_STATE_STOPPED);

}

static void
qdio_call_shutdown(struct work_struct *work)
{
	struct ccw_device_private *priv;
	struct ccw_device *cdev;

	priv = container_of(work, struct ccw_device_private, kick_work);
	cdev = priv->cdev;
	qdio_shutdown(cdev, QDIO_FLAG_CLEANUP_USING_CLEAR);
	put_device(&cdev->dev);
}

static void
qdio_timeout_handler(struct ccw_device *cdev)
{
	struct qdio_irq *irq_ptr;
	char dbf_text[15];

	QDIO_DBF_TEXT2(0, trace, "qtoh");
	sprintf(dbf_text, "%s", cdev->dev.bus_id);
	QDIO_DBF_TEXT2(0, trace, dbf_text);

	irq_ptr = cdev->private->qdio_data;
	sprintf(dbf_text, "state:%d", irq_ptr->state);
	QDIO_DBF_TEXT2(0, trace, dbf_text);

	switch (irq_ptr->state) {
	case QDIO_IRQ_STATE_INACTIVE:
		QDIO_PRINT_ERR("establish queues on irq 0.%x.%04x: timed out\n",
			       irq_ptr->schid.ssid, irq_ptr->schid.sch_no);
		QDIO_DBF_TEXT2(1,setup,"eq:timeo");
		qdio_set_state(irq_ptr, QDIO_IRQ_STATE_ERR);
		break;
	case QDIO_IRQ_STATE_CLEANUP:
		QDIO_PRINT_INFO("Did not get interrupt on cleanup, "
				"irq=0.%x.%x.\n",
				irq_ptr->schid.ssid, irq_ptr->schid.sch_no);
		qdio_set_state(irq_ptr, QDIO_IRQ_STATE_ERR);
		break;
	case QDIO_IRQ_STATE_ESTABLISHED:
	case QDIO_IRQ_STATE_ACTIVE:
		/* I/O has been terminated by common I/O layer. */
		QDIO_PRINT_INFO("Queues on irq 0.%x.%04x killed by cio.\n",
				irq_ptr->schid.ssid, irq_ptr->schid.sch_no);
		QDIO_DBF_TEXT2(1, trace, "cio:term");
		qdio_set_state(irq_ptr, QDIO_IRQ_STATE_STOPPED);
		if (get_device(&cdev->dev)) {
			/* Can't call shutdown from interrupt context. */
			PREPARE_WORK(&cdev->private->kick_work,
				     qdio_call_shutdown);
			queue_work(ccw_device_work, &cdev->private->kick_work);
		}
		break;
	default:
		BUG();
	}
	ccw_device_set_timeout(cdev, 0);
	wake_up(&cdev->private->wait_q);
}

static void
qdio_handler(struct ccw_device *cdev, unsigned long intparm, struct irb *irb)
{
	struct qdio_irq *irq_ptr;
	int cstat,dstat;
	char dbf_text[15];

#ifdef CONFIG_QDIO_DEBUG
	QDIO_DBF_TEXT4(0, trace, "qint");
	sprintf(dbf_text, "%s", cdev->dev.bus_id);
	QDIO_DBF_TEXT4(0, trace, dbf_text);
#endif /* CONFIG_QDIO_DEBUG */
	
	if (!intparm) {
		QDIO_PRINT_ERR("got unsolicited interrupt in qdio " \
				  "handler, device %s\n", cdev->dev.bus_id);
		return;
	}

	irq_ptr = cdev->private->qdio_data;
	if (!irq_ptr) {
		QDIO_DBF_TEXT2(1, trace, "uint");
		sprintf(dbf_text,"%s", cdev->dev.bus_id);
		QDIO_DBF_TEXT2(1,trace,dbf_text);
		QDIO_PRINT_ERR("received interrupt on unused device %s!\n",
			       cdev->dev.bus_id);
		return;
	}

	if (IS_ERR(irb)) {
		/* Currently running i/o is in error. */
		switch (PTR_ERR(irb)) {
		case -EIO:
			QDIO_PRINT_ERR("i/o error on device %s\n",
				       cdev->dev.bus_id);
			return;
		case -ETIMEDOUT:
			qdio_timeout_handler(cdev);
			return;
		default:
			QDIO_PRINT_ERR("unknown error state %ld on device %s\n",
				       PTR_ERR(irb), cdev->dev.bus_id);
			return;
		}
	}

	qdio_irq_check_sense(irq_ptr->schid, irb);

#ifdef CONFIG_QDIO_DEBUG
	sprintf(dbf_text, "state:%d", irq_ptr->state);
	QDIO_DBF_TEXT4(0, trace, dbf_text);
#endif /* CONFIG_QDIO_DEBUG */

        cstat = irb->scsw.cstat;
        dstat = irb->scsw.dstat;

	switch (irq_ptr->state) {
	case QDIO_IRQ_STATE_INACTIVE:
		qdio_establish_handle_irq(cdev, cstat, dstat);
		break;

	case QDIO_IRQ_STATE_CLEANUP:
		qdio_set_state(irq_ptr, QDIO_IRQ_STATE_INACTIVE);
		break;

	case QDIO_IRQ_STATE_ESTABLISHED:
	case QDIO_IRQ_STATE_ACTIVE:
		if (cstat & SCHN_STAT_PCI) {
			qdio_handle_pci(irq_ptr);
			break;
		}

		if ((cstat&~SCHN_STAT_PCI)||dstat) {
			qdio_handle_activate_check(cdev, intparm, cstat, dstat);
			break;
		}
	default:
		QDIO_PRINT_ERR("got interrupt for queues in state %d on " \
			       "device %s?!\n",
			       irq_ptr->state, cdev->dev.bus_id);
	}
	wake_up(&cdev->private->wait_q);

}

int
qdio_synchronize(struct ccw_device *cdev, unsigned int flags,
		 unsigned int queue_number)
{
	int cc = 0;
	struct qdio_q *q;
	struct qdio_irq *irq_ptr;
	void *ptr;
#ifdef CONFIG_QDIO_DEBUG
	char dbf_text[15]="SyncXXXX";
#endif

	irq_ptr = cdev->private->qdio_data;
	if (!irq_ptr)
		return -ENODEV;

#ifdef CONFIG_QDIO_DEBUG
	*((int*)(&dbf_text[4])) = irq_ptr->schid.sch_no;
	QDIO_DBF_HEX4(0,trace,dbf_text,QDIO_DBF_TRACE_LEN);
	*((int*)(&dbf_text[0]))=flags;
	*((int*)(&dbf_text[4]))=queue_number;
	QDIO_DBF_HEX4(0,trace,dbf_text,QDIO_DBF_TRACE_LEN);
#endif /* CONFIG_QDIO_DEBUG */

	if (flags&QDIO_FLAG_SYNC_INPUT) {
		q=irq_ptr->input_qs[queue_number];
		if (!q)
			return -EINVAL;
		if (!(irq_ptr->is_qebsm))
			cc = do_siga_sync(q->schid, 0, q->mask);
	} else if (flags&QDIO_FLAG_SYNC_OUTPUT) {
		q=irq_ptr->output_qs[queue_number];
		if (!q)
			return -EINVAL;
		if (!(irq_ptr->is_qebsm))
			cc = do_siga_sync(q->schid, q->mask, 0);
	} else 
		return -EINVAL;

	ptr=&cc;
	if (cc)
		QDIO_DBF_HEX3(0,trace,&ptr,sizeof(int));

	return cc;
}

static void
qdio_check_subchannel_qebsm(struct qdio_irq *irq_ptr, unsigned char qdioac,
			    unsigned long token)
{
	struct qdio_q *q;
	int i;
	unsigned int count, start_buf;
	char dbf_text[15];

	/*check if QEBSM is disabled */
	if (!(irq_ptr->is_qebsm) || !(qdioac & 0x01)) {
		irq_ptr->is_qebsm  = 0;
		irq_ptr->sch_token = 0;
		irq_ptr->qib.rflags &= ~QIB_RFLAGS_ENABLE_QEBSM;
		QDIO_DBF_TEXT0(0,setup,"noV=V");
		return;
	}
	irq_ptr->sch_token = token;
	/*input queue*/
	for (i = 0; i < irq_ptr->no_input_qs;i++) {
		q = irq_ptr->input_qs[i];
		count = QDIO_MAX_BUFFERS_PER_Q;
		start_buf = 0;
		set_slsb(q, &start_buf, SLSB_P_INPUT_NOT_INIT, &count);
	}
	sprintf(dbf_text,"V=V:%2x",irq_ptr->is_qebsm);
	QDIO_DBF_TEXT0(0,setup,dbf_text);
	sprintf(dbf_text,"%8lx",irq_ptr->sch_token);
	QDIO_DBF_TEXT0(0,setup,dbf_text);
	/*output queue*/
	for (i = 0; i < irq_ptr->no_output_qs; i++) {
		q = irq_ptr->output_qs[i];
		count = QDIO_MAX_BUFFERS_PER_Q;
		start_buf = 0;
		set_slsb(q, &start_buf, SLSB_P_OUTPUT_NOT_INIT, &count);
	}
}

static void
qdio_get_ssqd_information(struct qdio_irq *irq_ptr)
{
	int result;
	unsigned char qdioac;
	struct {
		struct chsc_header request;
		u16 reserved1:10;
		u16 ssid:2;
		u16 fmt:4;
		u16 first_sch;
		u16 reserved2;
		u16 last_sch;
		u32 reserved3;
		struct chsc_header response;
		u32 reserved4;
		u8  flags;
		u8  reserved5;
		u16 sch;
		u8  qfmt;
		u8  parm;
		u8  qdioac1;
		u8  sch_class;
		u8  reserved7;
		u8  icnt;
		u8  reserved8;
		u8  ocnt;
		u8 reserved9;
		u8 mbccnt;
		u16 qdioac2;
		u64 sch_token;
	} *ssqd_area;

	QDIO_DBF_TEXT0(0,setup,"getssqd");
	qdioac = 0;
	ssqd_area = mempool_alloc(qdio_mempool_scssc, GFP_ATOMIC);
	if (!ssqd_area) {
	        QDIO_PRINT_WARN("Could not get memory for chsc. Using all " \
				"SIGAs for sch x%x.\n", irq_ptr->schid.sch_no);
		irq_ptr->qdioac = CHSC_FLAG_SIGA_INPUT_NECESSARY |
				  CHSC_FLAG_SIGA_OUTPUT_NECESSARY |
				  CHSC_FLAG_SIGA_SYNC_NECESSARY; /* all flags set */
		irq_ptr->is_qebsm = 0;
		irq_ptr->sch_token = 0;
		irq_ptr->qib.rflags &= ~QIB_RFLAGS_ENABLE_QEBSM;
		return;
	}

	ssqd_area->request = (struct chsc_header) {
		.length = 0x0010,
		.code   = 0x0024,
	};
	ssqd_area->first_sch = irq_ptr->schid.sch_no;
	ssqd_area->last_sch = irq_ptr->schid.sch_no;
	ssqd_area->ssid = irq_ptr->schid.ssid;
	result = chsc(ssqd_area);

	if (result) {
		QDIO_PRINT_WARN("CHSC returned cc %i. Using all " \
				"SIGAs for sch 0.%x.%x.\n", result,
				irq_ptr->schid.ssid, irq_ptr->schid.sch_no);
		qdioac = CHSC_FLAG_SIGA_INPUT_NECESSARY |
			CHSC_FLAG_SIGA_OUTPUT_NECESSARY |
			CHSC_FLAG_SIGA_SYNC_NECESSARY; /* all flags set */
		irq_ptr->is_qebsm  = 0;
		goto out;
	}

	if (ssqd_area->response.code != QDIO_CHSC_RESPONSE_CODE_OK) {
		QDIO_PRINT_WARN("response upon checking SIGA needs " \
				"is 0x%x. Using all SIGAs for sch 0.%x.%x.\n",
				ssqd_area->response.code,
				irq_ptr->schid.ssid, irq_ptr->schid.sch_no);
		qdioac = CHSC_FLAG_SIGA_INPUT_NECESSARY |
			CHSC_FLAG_SIGA_OUTPUT_NECESSARY |
			CHSC_FLAG_SIGA_SYNC_NECESSARY; /* all flags set */
		irq_ptr->is_qebsm  = 0;
		goto out;
	}
	if (!(ssqd_area->flags & CHSC_FLAG_QDIO_CAPABILITY) ||
	    !(ssqd_area->flags & CHSC_FLAG_VALIDITY) ||
	    (ssqd_area->sch != irq_ptr->schid.sch_no)) {
		QDIO_PRINT_WARN("huh? problems checking out sch 0.%x.%x... " \
				"using all SIGAs.\n",
				irq_ptr->schid.ssid, irq_ptr->schid.sch_no);
		qdioac = CHSC_FLAG_SIGA_INPUT_NECESSARY |
			CHSC_FLAG_SIGA_OUTPUT_NECESSARY |
			CHSC_FLAG_SIGA_SYNC_NECESSARY; /* worst case */
		irq_ptr->is_qebsm  = 0;
		goto out;
	}
	qdioac = ssqd_area->qdioac1;
out:
	qdio_check_subchannel_qebsm(irq_ptr, qdioac,
				    ssqd_area->sch_token);
	mempool_free(ssqd_area, qdio_mempool_scssc);
	irq_ptr->qdioac = qdioac;
}

static unsigned int
tiqdio_check_chsc_availability(void)
{
	char dbf_text[15];

	if (!css_characteristics_avail)
		return -EIO;

	/* Check for bit 41. */
	if (!css_general_characteristics.aif) {
		QDIO_PRINT_WARN("Adapter interruption facility not " \
				"installed.\n");
		return -ENOENT;
	}

	/* Check for bits 107 and 108. */
	if (!css_chsc_characteristics.scssc ||
	    !css_chsc_characteristics.scsscf) {
		QDIO_PRINT_WARN("Set Chan Subsys. Char. & Fast-CHSCs " \
				"not available.\n");
		return -ENOENT;
	}

	/* Check for OSA/FCP thin interrupts (bit 67). */
	hydra_thinints = css_general_characteristics.aif_osa;
	sprintf(dbf_text,"hydrati%1x", hydra_thinints);
	QDIO_DBF_TEXT0(0,setup,dbf_text);

#ifdef CONFIG_64BIT
	/* Check for QEBSM support in general (bit 58). */
	is_passthrough = css_general_characteristics.qebsm;
#endif
	sprintf(dbf_text,"cssQBS:%1x", is_passthrough);
	QDIO_DBF_TEXT0(0,setup,dbf_text);

	/* Check for aif time delay disablement fac (bit 56). If installed,
	 * omit svs even under lpar (good point by rick again) */
	omit_svs = css_general_characteristics.aif_tdd;
	sprintf(dbf_text,"omitsvs%1x", omit_svs);
	QDIO_DBF_TEXT0(0,setup,dbf_text);
	return 0;
}


static unsigned int
tiqdio_set_subchannel_ind(struct qdio_irq *irq_ptr, int reset_to_zero)
{
	unsigned long real_addr_local_summary_bit;
	unsigned long real_addr_dev_st_chg_ind;
	void *ptr;
	char dbf_text[15];

	unsigned int resp_code;
	int result;

	struct {
		struct chsc_header request;
		u16 operation_code;
		u16 reserved1;
		u32 reserved2;
		u32 reserved3;
		u64 summary_indicator_addr;
		u64 subchannel_indicator_addr;
		u32 ks:4;
		u32 kc:4;
		u32 reserved4:21;
		u32 isc:3;
		u32 word_with_d_bit;
		/* set to 0x10000000 to enable
		 * time delay disablement facility */
		u32 reserved5;
		struct subchannel_id schid;
		u32 reserved6[1004];
		struct chsc_header response;
		u32 reserved7;
	} *scssc_area;

	if (!irq_ptr->is_thinint_irq)
		return -ENODEV;

	if (reset_to_zero) {
		real_addr_local_summary_bit=0;
		real_addr_dev_st_chg_ind=0;
	} else {
		real_addr_local_summary_bit=
			virt_to_phys((volatile void *)indicators);
		real_addr_dev_st_chg_ind=
			virt_to_phys((volatile void *)irq_ptr->dev_st_chg_ind);
	}

	scssc_area = mempool_alloc(qdio_mempool_scssc, GFP_ATOMIC);
	if (!scssc_area) {
		QDIO_PRINT_WARN("No memory for setting indicators on " \
				"subchannel 0.%x.%x.\n",
				irq_ptr->schid.ssid, irq_ptr->schid.sch_no);
		return -ENOMEM;
	}
	scssc_area->request = (struct chsc_header) {
		.length = 0x0fe0,
		.code   = 0x0021,
	};
	scssc_area->operation_code = 0;

        scssc_area->summary_indicator_addr = real_addr_local_summary_bit;
	scssc_area->subchannel_indicator_addr = real_addr_dev_st_chg_ind;
	scssc_area->ks = QDIO_STORAGE_KEY;
	scssc_area->kc = QDIO_STORAGE_KEY;
	scssc_area->isc = TIQDIO_THININT_ISC;
	scssc_area->schid = irq_ptr->schid;
	/* enables the time delay disablement facility. Don't care
	 * whether it is really there (i.e. we haven't checked for
	 * it) */
	if (css_general_characteristics.aif_tdd)
		scssc_area->word_with_d_bit = 0x10000000;
	else
		QDIO_PRINT_WARN("Time delay disablement facility " \
				"not available\n");

	result = chsc(scssc_area);
	if (result) {
		QDIO_PRINT_WARN("could not set indicators on irq 0.%x.%x, " \
				"cc=%i.\n",
				irq_ptr->schid.ssid, irq_ptr->schid.sch_no,result);
		result = -EIO;
		goto out;
	}

	resp_code = scssc_area->response.code;
	if (resp_code!=QDIO_CHSC_RESPONSE_CODE_OK) {
		QDIO_PRINT_WARN("response upon setting indicators " \
				"is 0x%x.\n",resp_code);
		sprintf(dbf_text,"sidR%4x",resp_code);
		QDIO_DBF_TEXT1(0,trace,dbf_text);
		QDIO_DBF_TEXT1(0,setup,dbf_text);
		ptr=&scssc_area->response;
		QDIO_DBF_HEX2(1,setup,&ptr,QDIO_DBF_SETUP_LEN);
		result = -EIO;
		goto out;
	}

	QDIO_DBF_TEXT2(0,setup,"setscind");
	QDIO_DBF_HEX2(0,setup,&real_addr_local_summary_bit,
		      sizeof(unsigned long));
	QDIO_DBF_HEX2(0,setup,&real_addr_dev_st_chg_ind,sizeof(unsigned long));
	result = 0;
out:
	mempool_free(scssc_area, qdio_mempool_scssc);
	return result;

}

static unsigned int
tiqdio_set_delay_target(struct qdio_irq *irq_ptr, unsigned long delay_target)
{
	unsigned int resp_code;
	int result;
	void *ptr;
	char dbf_text[15];

	struct {
		struct chsc_header request;
		u16 operation_code;
		u16 reserved1;
		u32 reserved2;
		u32 reserved3;
		u32 reserved4[2];
		u32 delay_target;
		u32 reserved5[1009];
		struct chsc_header response;
		u32 reserved6;
	} *scsscf_area;

	if (!irq_ptr->is_thinint_irq)
		return -ENODEV;

	scsscf_area = mempool_alloc(qdio_mempool_scssc, GFP_ATOMIC);
	if (!scsscf_area) {
		QDIO_PRINT_WARN("No memory for setting delay target on " \
				"subchannel 0.%x.%x.\n",
				irq_ptr->schid.ssid, irq_ptr->schid.sch_no);
		return -ENOMEM;
	}
	scsscf_area->request = (struct chsc_header) {
		.length = 0x0fe0,
		.code   = 0x1027,
	};

	scsscf_area->delay_target = delay_target<<16;

	result=chsc(scsscf_area);
	if (result) {
		QDIO_PRINT_WARN("could not set delay target on irq 0.%x.%x, " \
				"cc=%i. Continuing.\n",
				irq_ptr->schid.ssid, irq_ptr->schid.sch_no,
				result);
		result = -EIO;
		goto out;
	}

	resp_code = scsscf_area->response.code;
	if (resp_code!=QDIO_CHSC_RESPONSE_CODE_OK) {
		QDIO_PRINT_WARN("response upon setting delay target " \
				"is 0x%x. Continuing.\n",resp_code);
		sprintf(dbf_text,"sdtR%4x",resp_code);
		QDIO_DBF_TEXT1(0,trace,dbf_text);
		QDIO_DBF_TEXT1(0,setup,dbf_text);
		ptr=&scsscf_area->response;
		QDIO_DBF_HEX2(1,trace,&ptr,QDIO_DBF_TRACE_LEN);
	}
	QDIO_DBF_TEXT2(0,trace,"delytrgt");
	QDIO_DBF_HEX2(0,trace,&delay_target,sizeof(unsigned long));
	result = 0; /* not critical */
out:
	mempool_free(scsscf_area, qdio_mempool_scssc);
	return result;
}

int
qdio_cleanup(struct ccw_device *cdev, int how)
{
	struct qdio_irq *irq_ptr;
	char dbf_text[15];
	int rc;

	irq_ptr = cdev->private->qdio_data;
	if (!irq_ptr)
		return -ENODEV;

	sprintf(dbf_text,"qcln%4x",irq_ptr->schid.sch_no);
	QDIO_DBF_TEXT1(0,trace,dbf_text);
	QDIO_DBF_TEXT0(0,setup,dbf_text);

	rc = qdio_shutdown(cdev, how);
	if ((rc == 0) || (rc == -EINPROGRESS))
		rc = qdio_free(cdev);
	return rc;
}

int
qdio_shutdown(struct ccw_device *cdev, int how)
{
	struct qdio_irq *irq_ptr;
	int i;
	int result = 0;
	int rc;
	unsigned long flags;
	int timeout;
	char dbf_text[15];

	irq_ptr = cdev->private->qdio_data;
	if (!irq_ptr)
		return -ENODEV;

	down(&irq_ptr->setting_up_sema);

	sprintf(dbf_text,"qsqs%4x",irq_ptr->schid.sch_no);
	QDIO_DBF_TEXT1(0,trace,dbf_text);
	QDIO_DBF_TEXT0(0,setup,dbf_text);

	/* mark all qs as uninteresting */
	for (i=0;i<irq_ptr->no_input_qs;i++)
		atomic_set(&irq_ptr->input_qs[i]->is_in_shutdown,1);

	for (i=0;i<irq_ptr->no_output_qs;i++)
		atomic_set(&irq_ptr->output_qs[i]->is_in_shutdown,1);

	tasklet_kill(&tiqdio_tasklet);

	for (i=0;i<irq_ptr->no_input_qs;i++) {
		qdio_unmark_q(irq_ptr->input_qs[i]);
		tasklet_kill(&irq_ptr->input_qs[i]->tasklet);
		wait_event_interruptible_timeout(cdev->private->wait_q,
						 !atomic_read(&irq_ptr->
							      input_qs[i]->
							      use_count),
						 QDIO_NO_USE_COUNT_TIMEOUT);
		if (atomic_read(&irq_ptr->input_qs[i]->use_count))
			result=-EINPROGRESS;
	}

	for (i=0;i<irq_ptr->no_output_qs;i++) {
		tasklet_kill(&irq_ptr->output_qs[i]->tasklet);
		del_timer(&irq_ptr->output_qs[i]->timer);
		wait_event_interruptible_timeout(cdev->private->wait_q,
						 !atomic_read(&irq_ptr->
							      output_qs[i]->
							      use_count),
						 QDIO_NO_USE_COUNT_TIMEOUT);
		if (atomic_read(&irq_ptr->output_qs[i]->use_count))
			result=-EINPROGRESS;
	}

	/* cleanup subchannel */
	spin_lock_irqsave(get_ccwdev_lock(cdev),flags);
	if (how&QDIO_FLAG_CLEANUP_USING_CLEAR) {
		rc = ccw_device_clear(cdev, QDIO_DOING_CLEANUP);
		timeout=QDIO_CLEANUP_CLEAR_TIMEOUT;
	} else if (how&QDIO_FLAG_CLEANUP_USING_HALT) {
		rc = ccw_device_halt(cdev, QDIO_DOING_CLEANUP);
		timeout=QDIO_CLEANUP_HALT_TIMEOUT;
	} else { /* default behaviour */
		rc = ccw_device_halt(cdev, QDIO_DOING_CLEANUP);
		timeout=QDIO_CLEANUP_HALT_TIMEOUT;
	}
	if (rc == -ENODEV) {
		/* No need to wait for device no longer present. */
		qdio_set_state(irq_ptr, QDIO_IRQ_STATE_INACTIVE);
		spin_unlock_irqrestore(get_ccwdev_lock(cdev), flags);
	} else if (((void *)cdev->handler != (void *)qdio_handler) && rc == 0) {
		/*
		 * Whoever put another handler there, has to cope with the
		 * interrupt theirself. Might happen if qdio_shutdown was
		 * called on already shutdown queues, but this shouldn't have
		 * bad side effects.
		 */
		qdio_set_state(irq_ptr, QDIO_IRQ_STATE_INACTIVE);
		spin_unlock_irqrestore(get_ccwdev_lock(cdev), flags);
	} else if (rc == 0) {
		qdio_set_state(irq_ptr, QDIO_IRQ_STATE_CLEANUP);
		ccw_device_set_timeout(cdev, timeout);
		spin_unlock_irqrestore(get_ccwdev_lock(cdev),flags);

		wait_event(cdev->private->wait_q,
			   irq_ptr->state == QDIO_IRQ_STATE_INACTIVE ||
			   irq_ptr->state == QDIO_IRQ_STATE_ERR);
	} else {
		QDIO_PRINT_INFO("ccw_device_{halt,clear} returned %d for "
				"device %s\n", result, cdev->dev.bus_id);
		spin_unlock_irqrestore(get_ccwdev_lock(cdev), flags);
		result = rc;
		goto out;
	}
	if (irq_ptr->is_thinint_irq) {
		qdio_put_indicator((__u32*)irq_ptr->dev_st_chg_ind);
		tiqdio_set_subchannel_ind(irq_ptr,1); 
                /* reset adapter interrupt indicators */
	}

 	/* exchange int handlers, if necessary */
 	if ((void*)cdev->handler == (void*)qdio_handler)
 		cdev->handler=irq_ptr->original_int_handler;

	/* Ignore errors. */
	qdio_set_state(irq_ptr, QDIO_IRQ_STATE_INACTIVE);
	ccw_device_set_timeout(cdev, 0);
out:
	up(&irq_ptr->setting_up_sema);
	return result;
}

int
qdio_free(struct ccw_device *cdev)
{
	struct qdio_irq *irq_ptr;
	char dbf_text[15];

	irq_ptr = cdev->private->qdio_data;
	if (!irq_ptr)
		return -ENODEV;

	down(&irq_ptr->setting_up_sema);

	sprintf(dbf_text,"qfqs%4x",irq_ptr->schid.sch_no);
	QDIO_DBF_TEXT1(0,trace,dbf_text);
	QDIO_DBF_TEXT0(0,setup,dbf_text);

	cdev->private->qdio_data = NULL;

	up(&irq_ptr->setting_up_sema);

	qdio_release_irq_memory(irq_ptr);
	module_put(THIS_MODULE);
	return 0;
}

static void
qdio_allocate_do_dbf(struct qdio_initialize *init_data)
{
	char dbf_text[20]; /* if a printf printed out more than 8 chars */

	sprintf(dbf_text,"qfmt:%x",init_data->q_format);
	QDIO_DBF_TEXT0(0,setup,dbf_text);
	QDIO_DBF_HEX0(0,setup,init_data->adapter_name,8);
	sprintf(dbf_text,"qpff%4x",init_data->qib_param_field_format);
	QDIO_DBF_TEXT0(0,setup,dbf_text);
	QDIO_DBF_HEX0(0,setup,&init_data->qib_param_field,sizeof(char*));
	QDIO_DBF_HEX0(0,setup,&init_data->input_slib_elements,sizeof(long*));
	QDIO_DBF_HEX0(0,setup,&init_data->output_slib_elements,sizeof(long*));
	sprintf(dbf_text,"miit%4x",init_data->min_input_threshold);
	QDIO_DBF_TEXT0(0,setup,dbf_text);
	sprintf(dbf_text,"mait%4x",init_data->max_input_threshold);
	QDIO_DBF_TEXT0(0,setup,dbf_text);
	sprintf(dbf_text,"miot%4x",init_data->min_output_threshold);
	QDIO_DBF_TEXT0(0,setup,dbf_text);
	sprintf(dbf_text,"maot%4x",init_data->max_output_threshold);
	QDIO_DBF_TEXT0(0,setup,dbf_text);
	sprintf(dbf_text,"niq:%4x",init_data->no_input_qs);
	QDIO_DBF_TEXT0(0,setup,dbf_text);
	sprintf(dbf_text,"noq:%4x",init_data->no_output_qs);
	QDIO_DBF_TEXT0(0,setup,dbf_text);
	QDIO_DBF_HEX0(0,setup,&init_data->input_handler,sizeof(void*));
	QDIO_DBF_HEX0(0,setup,&init_data->output_handler,sizeof(void*));
	QDIO_DBF_HEX0(0,setup,&init_data->int_parm,sizeof(long));
	QDIO_DBF_HEX0(0,setup,&init_data->flags,sizeof(long));
	QDIO_DBF_HEX0(0,setup,&init_data->input_sbal_addr_array,sizeof(void*));
	QDIO_DBF_HEX0(0,setup,&init_data->output_sbal_addr_array,sizeof(void*));
}

static void
qdio_allocate_fill_input_desc(struct qdio_irq *irq_ptr, int i, int iqfmt)
{
	irq_ptr->input_qs[i]->is_iqdio_q = iqfmt;
	irq_ptr->input_qs[i]->is_thinint_q = irq_ptr->is_thinint_irq;

	irq_ptr->qdr->qdf0[i].sliba=(unsigned long)(irq_ptr->input_qs[i]->slib);

	irq_ptr->qdr->qdf0[i].sla=(unsigned long)(irq_ptr->input_qs[i]->sl);

	irq_ptr->qdr->qdf0[i].slsba=
		(unsigned long)(&irq_ptr->input_qs[i]->slsb.acc.val[0]);

	irq_ptr->qdr->qdf0[i].akey=QDIO_STORAGE_KEY;
	irq_ptr->qdr->qdf0[i].bkey=QDIO_STORAGE_KEY;
	irq_ptr->qdr->qdf0[i].ckey=QDIO_STORAGE_KEY;
	irq_ptr->qdr->qdf0[i].dkey=QDIO_STORAGE_KEY;
}

static void
qdio_allocate_fill_output_desc(struct qdio_irq *irq_ptr, int i,
			       int j, int iqfmt)
{
	irq_ptr->output_qs[i]->is_iqdio_q = iqfmt;
	irq_ptr->output_qs[i]->is_thinint_q = irq_ptr->is_thinint_irq;

	irq_ptr->qdr->qdf0[i+j].sliba=(unsigned long)(irq_ptr->output_qs[i]->slib);

	irq_ptr->qdr->qdf0[i+j].sla=(unsigned long)(irq_ptr->output_qs[i]->sl);

	irq_ptr->qdr->qdf0[i+j].slsba=
		(unsigned long)(&irq_ptr->output_qs[i]->slsb.acc.val[0]);

	irq_ptr->qdr->qdf0[i+j].akey=QDIO_STORAGE_KEY;
	irq_ptr->qdr->qdf0[i+j].bkey=QDIO_STORAGE_KEY;
	irq_ptr->qdr->qdf0[i+j].ckey=QDIO_STORAGE_KEY;
	irq_ptr->qdr->qdf0[i+j].dkey=QDIO_STORAGE_KEY;
}


static void
qdio_initialize_set_siga_flags_input(struct qdio_irq *irq_ptr)
{
	int i;

	for (i=0;i<irq_ptr->no_input_qs;i++) {
		irq_ptr->input_qs[i]->siga_sync=
			irq_ptr->qdioac&CHSC_FLAG_SIGA_SYNC_NECESSARY;
		irq_ptr->input_qs[i]->siga_in=
			irq_ptr->qdioac&CHSC_FLAG_SIGA_INPUT_NECESSARY;
		irq_ptr->input_qs[i]->siga_out=
			irq_ptr->qdioac&CHSC_FLAG_SIGA_OUTPUT_NECESSARY;
		irq_ptr->input_qs[i]->siga_sync_done_on_thinints=
			irq_ptr->qdioac&CHSC_FLAG_SIGA_SYNC_DONE_ON_THININTS;
		irq_ptr->input_qs[i]->hydra_gives_outbound_pcis=
			irq_ptr->hydra_gives_outbound_pcis;
		irq_ptr->input_qs[i]->siga_sync_done_on_outb_tis=
			((irq_ptr->qdioac&
			  (CHSC_FLAG_SIGA_SYNC_DONE_ON_OUTB_PCIS|
			   CHSC_FLAG_SIGA_SYNC_DONE_ON_THININTS))==
			 (CHSC_FLAG_SIGA_SYNC_DONE_ON_OUTB_PCIS|
			  CHSC_FLAG_SIGA_SYNC_DONE_ON_THININTS));

	}
}

static void
qdio_initialize_set_siga_flags_output(struct qdio_irq *irq_ptr)
{
	int i;

	for (i=0;i<irq_ptr->no_output_qs;i++) {
		irq_ptr->output_qs[i]->siga_sync=
			irq_ptr->qdioac&CHSC_FLAG_SIGA_SYNC_NECESSARY;
		irq_ptr->output_qs[i]->siga_in=
			irq_ptr->qdioac&CHSC_FLAG_SIGA_INPUT_NECESSARY;
		irq_ptr->output_qs[i]->siga_out=
			irq_ptr->qdioac&CHSC_FLAG_SIGA_OUTPUT_NECESSARY;
		irq_ptr->output_qs[i]->siga_sync_done_on_thinints=
			irq_ptr->qdioac&CHSC_FLAG_SIGA_SYNC_DONE_ON_THININTS;
		irq_ptr->output_qs[i]->hydra_gives_outbound_pcis=
			irq_ptr->hydra_gives_outbound_pcis;
		irq_ptr->output_qs[i]->siga_sync_done_on_outb_tis=
			((irq_ptr->qdioac&
			  (CHSC_FLAG_SIGA_SYNC_DONE_ON_OUTB_PCIS|
			   CHSC_FLAG_SIGA_SYNC_DONE_ON_THININTS))==
			 (CHSC_FLAG_SIGA_SYNC_DONE_ON_OUTB_PCIS|
			  CHSC_FLAG_SIGA_SYNC_DONE_ON_THININTS));

	}
}

static int
qdio_establish_irq_check_for_errors(struct ccw_device *cdev, int cstat,
				    int dstat)
{
	char dbf_text[15];
	struct qdio_irq *irq_ptr;

	irq_ptr = cdev->private->qdio_data;

	if (cstat || (dstat & ~(DEV_STAT_CHN_END|DEV_STAT_DEV_END))) {
		sprintf(dbf_text,"ick1%4x",irq_ptr->schid.sch_no);
		QDIO_DBF_TEXT2(1,trace,dbf_text);
		QDIO_DBF_HEX2(0,trace,&dstat,sizeof(int));
		QDIO_DBF_HEX2(0,trace,&cstat,sizeof(int));
		QDIO_PRINT_ERR("received check condition on establish " \
			       "queues on irq 0.%x.%x (cs=x%x, ds=x%x).\n",
			       irq_ptr->schid.ssid, irq_ptr->schid.sch_no,
			       cstat,dstat);
		qdio_set_state(irq_ptr,QDIO_IRQ_STATE_ERR);
	}
	
	if (!(dstat & DEV_STAT_DEV_END)) {
		QDIO_DBF_TEXT2(1,setup,"eq:no de");
		QDIO_DBF_HEX2(0,setup,&dstat, sizeof(dstat));
		QDIO_DBF_HEX2(0,setup,&cstat, sizeof(cstat));
		QDIO_PRINT_ERR("establish queues on irq 0.%x.%04x: didn't get "
			       "device end: dstat=%02x, cstat=%02x\n",
			       irq_ptr->schid.ssid, irq_ptr->schid.sch_no,
			       dstat, cstat);
		qdio_set_state(irq_ptr, QDIO_IRQ_STATE_ERR);
		return 1;
	}

	if (dstat & ~(DEV_STAT_CHN_END|DEV_STAT_DEV_END)) {
		QDIO_DBF_TEXT2(1,setup,"eq:badio");
		QDIO_DBF_HEX2(0,setup,&dstat, sizeof(dstat));
		QDIO_DBF_HEX2(0,setup,&cstat, sizeof(cstat));
		QDIO_PRINT_ERR("establish queues on irq 0.%x.%04x: got "
			       "the following devstat: dstat=%02x, "
			       "cstat=%02x\n", irq_ptr->schid.ssid,
			       irq_ptr->schid.sch_no, dstat, cstat);
		qdio_set_state(irq_ptr, QDIO_IRQ_STATE_ERR);
		return 1;
	}
	return 0;
}

static void
qdio_establish_handle_irq(struct ccw_device *cdev, int cstat, int dstat)
{
	struct qdio_irq *irq_ptr;
	char dbf_text[15];

	irq_ptr = cdev->private->qdio_data;

	sprintf(dbf_text,"qehi%4x",cdev->private->schid.sch_no);
	QDIO_DBF_TEXT0(0,setup,dbf_text);
	QDIO_DBF_TEXT0(0,trace,dbf_text);

	if (qdio_establish_irq_check_for_errors(cdev, cstat, dstat)) {
		ccw_device_set_timeout(cdev, 0);
		return;
	}

	qdio_set_state(irq_ptr,QDIO_IRQ_STATE_ESTABLISHED);
	ccw_device_set_timeout(cdev, 0);
}

int
qdio_initialize(struct qdio_initialize *init_data)
{
	int rc;
	char dbf_text[15];

	sprintf(dbf_text,"qini%4x",init_data->cdev->private->schid.sch_no);
	QDIO_DBF_TEXT0(0,setup,dbf_text);
	QDIO_DBF_TEXT0(0,trace,dbf_text);

	rc = qdio_allocate(init_data);
	if (rc == 0) {
		rc = qdio_establish(init_data);
		if (rc != 0)
			qdio_free(init_data->cdev);
	}

	return rc;
}


int
qdio_allocate(struct qdio_initialize *init_data)
{
	struct qdio_irq *irq_ptr;
	char dbf_text[15];

	sprintf(dbf_text,"qalc%4x",init_data->cdev->private->schid.sch_no);
	QDIO_DBF_TEXT0(0,setup,dbf_text);
	QDIO_DBF_TEXT0(0,trace,dbf_text);
	if ( (init_data->no_input_qs>QDIO_MAX_QUEUES_PER_IRQ) ||
	     (init_data->no_output_qs>QDIO_MAX_QUEUES_PER_IRQ) ||
	     ((init_data->no_input_qs) && (!init_data->input_handler)) ||
	     ((init_data->no_output_qs) && (!init_data->output_handler)) )
		return -EINVAL;

	if (!init_data->input_sbal_addr_array)
		return -EINVAL;

	if (!init_data->output_sbal_addr_array)
		return -EINVAL;

	qdio_allocate_do_dbf(init_data);

	/* create irq */
	irq_ptr = (void *) get_zeroed_page(GFP_KERNEL | GFP_DMA);

	QDIO_DBF_TEXT0(0,setup,"irq_ptr:");
	QDIO_DBF_HEX0(0,setup,&irq_ptr,sizeof(void*));

	if (!irq_ptr) {
		QDIO_PRINT_ERR("kmalloc of irq_ptr failed!\n");
		return -ENOMEM;
	}

	init_MUTEX(&irq_ptr->setting_up_sema);

	/* QDR must be in DMA area since CCW data address is only 32 bit */
	irq_ptr->qdr=kmalloc(sizeof(struct qdr), GFP_KERNEL | GFP_DMA);
  	if (!(irq_ptr->qdr)) {
   		free_page((unsigned long) irq_ptr);
    		QDIO_PRINT_ERR("kmalloc of irq_ptr->qdr failed!\n");
		return -ENOMEM;
       	}
	QDIO_DBF_TEXT0(0,setup,"qdr:");
	QDIO_DBF_HEX0(0,setup,&irq_ptr->qdr,sizeof(void*));

	if (qdio_alloc_qs(irq_ptr,
       			  init_data->no_input_qs,
			  init_data->no_output_qs)) {
		qdio_release_irq_memory(irq_ptr);
		return -ENOMEM;
	}

	init_data->cdev->private->qdio_data = irq_ptr;

	qdio_set_state(irq_ptr,QDIO_IRQ_STATE_INACTIVE);

	return 0;
}

static int qdio_fill_irq(struct qdio_initialize *init_data)
{
	int i;
	char dbf_text[15];
	struct ciw *ciw;
	int is_iqdio;
	struct qdio_irq *irq_ptr;

	irq_ptr = init_data->cdev->private->qdio_data;

	memset(irq_ptr,0,((char*)&irq_ptr->qdr)-((char*)irq_ptr));

        /* wipes qib.ac, required by ar7063 */
	memset(irq_ptr->qdr,0,sizeof(struct qdr));

	irq_ptr->int_parm=init_data->int_parm;

	irq_ptr->schid = ccw_device_get_subchannel_id(init_data->cdev);
	irq_ptr->no_input_qs=init_data->no_input_qs;
	irq_ptr->no_output_qs=init_data->no_output_qs;

	if (init_data->q_format==QDIO_IQDIO_QFMT) {
		irq_ptr->is_iqdio_irq=1;
		irq_ptr->is_thinint_irq=1;
	} else {
		irq_ptr->is_iqdio_irq=0;
		irq_ptr->is_thinint_irq=hydra_thinints;
	}
	sprintf(dbf_text,"is_i_t%1x%1x",
		irq_ptr->is_iqdio_irq,irq_ptr->is_thinint_irq);
	QDIO_DBF_TEXT2(0,setup,dbf_text);

	if (irq_ptr->is_thinint_irq) {
		irq_ptr->dev_st_chg_ind = qdio_get_indicator();
		QDIO_DBF_HEX1(0,setup,&irq_ptr->dev_st_chg_ind,sizeof(void*));
		if (!irq_ptr->dev_st_chg_ind) {
			QDIO_PRINT_WARN("no indicator location available " \
					"for irq 0.%x.%x\n",
					irq_ptr->schid.ssid, irq_ptr->schid.sch_no);
			qdio_release_irq_memory(irq_ptr);
			return -ENOBUFS;
		}
	}

	/* defaults */
	irq_ptr->equeue.cmd=DEFAULT_ESTABLISH_QS_CMD;
	irq_ptr->equeue.count=DEFAULT_ESTABLISH_QS_COUNT;
	irq_ptr->aqueue.cmd=DEFAULT_ACTIVATE_QS_CMD;
	irq_ptr->aqueue.count=DEFAULT_ACTIVATE_QS_COUNT;

	qdio_fill_qs(irq_ptr, init_data->cdev,
		     init_data->no_input_qs,
		     init_data->no_output_qs,
		     init_data->input_handler,
		     init_data->output_handler,init_data->int_parm,
		     init_data->q_format,init_data->flags,
	   	     init_data->input_sbal_addr_array,
   		     init_data->output_sbal_addr_array);

	if (!try_module_get(THIS_MODULE)) {
		QDIO_PRINT_CRIT("try_module_get() failed!\n");
		qdio_release_irq_memory(irq_ptr);
		return -EINVAL;
	}

	qdio_fill_thresholds(irq_ptr,init_data->no_input_qs,
			     init_data->no_output_qs,
			     init_data->min_input_threshold,
			     init_data->max_input_threshold,
			     init_data->min_output_threshold,
			     init_data->max_output_threshold);

	/* fill in qdr */
	irq_ptr->qdr->qfmt=init_data->q_format;
	irq_ptr->qdr->iqdcnt=init_data->no_input_qs;
	irq_ptr->qdr->oqdcnt=init_data->no_output_qs;
	irq_ptr->qdr->iqdsz=sizeof(struct qdesfmt0)/4; /* size in words */
	irq_ptr->qdr->oqdsz=sizeof(struct qdesfmt0)/4;

	irq_ptr->qdr->qiba=(unsigned long)&irq_ptr->qib;
	irq_ptr->qdr->qkey=QDIO_STORAGE_KEY;

	/* fill in qib */
	irq_ptr->is_qebsm = is_passthrough;
	if (irq_ptr->is_qebsm)
		irq_ptr->qib.rflags |= QIB_RFLAGS_ENABLE_QEBSM;

	irq_ptr->qib.qfmt=init_data->q_format;
	if (init_data->no_input_qs)
		irq_ptr->qib.isliba=(unsigned long)(irq_ptr->input_qs[0]->slib);
	if (init_data->no_output_qs)
		irq_ptr->qib.osliba=(unsigned long)(irq_ptr->output_qs[0]->slib);
	memcpy(irq_ptr->qib.ebcnam,init_data->adapter_name,8);

	qdio_set_impl_params(irq_ptr,init_data->qib_param_field_format,
			     init_data->qib_param_field,
			     init_data->no_input_qs,
			     init_data->no_output_qs,
			     init_data->input_slib_elements,
			     init_data->output_slib_elements);

	/* first input descriptors, then output descriptors */
	is_iqdio = (init_data->q_format == QDIO_IQDIO_QFMT) ? 1 : 0;
	for (i=0;i<init_data->no_input_qs;i++)
		qdio_allocate_fill_input_desc(irq_ptr, i, is_iqdio);

	for (i=0;i<init_data->no_output_qs;i++)
		qdio_allocate_fill_output_desc(irq_ptr, i,
					       init_data->no_input_qs,
					       is_iqdio);

	/* qdr, qib, sls, slsbs, slibs, sbales filled. */

	/* get qdio commands */
	ciw = ccw_device_get_ciw(init_data->cdev, CIW_TYPE_EQUEUE);
	if (!ciw) {
		QDIO_DBF_TEXT2(1,setup,"no eq");
		QDIO_PRINT_INFO("No equeue CIW found for QDIO commands. "
				"Trying to use default.\n");
	} else
		irq_ptr->equeue = *ciw;
	ciw = ccw_device_get_ciw(init_data->cdev, CIW_TYPE_AQUEUE);
	if (!ciw) {
		QDIO_DBF_TEXT2(1,setup,"no aq");
		QDIO_PRINT_INFO("No aqueue CIW found for QDIO commands. "
				"Trying to use default.\n");
	} else
		irq_ptr->aqueue = *ciw;

	/* Set new interrupt handler. */
	irq_ptr->original_int_handler = init_data->cdev->handler;
	init_data->cdev->handler = qdio_handler;

	return 0;
}

int
qdio_establish(struct qdio_initialize *init_data)
{
	struct qdio_irq *irq_ptr;
	unsigned long saveflags;
	int result, result2;
	struct ccw_device *cdev;
	char dbf_text[20];

	cdev=init_data->cdev;
	irq_ptr = cdev->private->qdio_data;
	if (!irq_ptr)
		return -EINVAL;

	if (cdev->private->state != DEV_STATE_ONLINE)
		return -EINVAL;
	
	down(&irq_ptr->setting_up_sema);

	qdio_fill_irq(init_data);

	/* the thinint CHSC stuff */
	if (irq_ptr->is_thinint_irq) {

		result = tiqdio_set_subchannel_ind(irq_ptr,0);
		if (result) {
			up(&irq_ptr->setting_up_sema);
			qdio_shutdown(cdev, QDIO_FLAG_CLEANUP_USING_CLEAR);
			return result;
		}
		tiqdio_set_delay_target(irq_ptr,TIQDIO_DELAY_TARGET);
	}

	sprintf(dbf_text,"qest%4x",cdev->private->schid.sch_no);
	QDIO_DBF_TEXT0(0,setup,dbf_text);
	QDIO_DBF_TEXT0(0,trace,dbf_text);

	/* establish q */
	irq_ptr->ccw.cmd_code=irq_ptr->equeue.cmd;
	irq_ptr->ccw.flags=CCW_FLAG_SLI;
	irq_ptr->ccw.count=irq_ptr->equeue.count;
	irq_ptr->ccw.cda=QDIO_GET_ADDR(irq_ptr->qdr);

	spin_lock_irqsave(get_ccwdev_lock(cdev),saveflags);

	ccw_device_set_options_mask(cdev, 0);
	result=ccw_device_start_timeout(cdev,&irq_ptr->ccw,
					QDIO_DOING_ESTABLISH,0, 0,
					QDIO_ESTABLISH_TIMEOUT);
	if (result) {
		result2=ccw_device_start_timeout(cdev,&irq_ptr->ccw,
						 QDIO_DOING_ESTABLISH,0,0,
						 QDIO_ESTABLISH_TIMEOUT);
		sprintf(dbf_text,"eq:io%4x",result);
		QDIO_DBF_TEXT2(1,setup,dbf_text);
		if (result2) {
			sprintf(dbf_text,"eq:io%4x",result);
			QDIO_DBF_TEXT2(1,setup,dbf_text);
		}
		QDIO_PRINT_WARN("establish queues on irq 0.%x.%04x: do_IO " \
				"returned %i, next try returned %i\n",
				irq_ptr->schid.ssid, irq_ptr->schid.sch_no,
				result, result2);
		result=result2;
		if (result)
			ccw_device_set_timeout(cdev, 0);
	}

	spin_unlock_irqrestore(get_ccwdev_lock(cdev),saveflags);

	if (result) {
		up(&irq_ptr->setting_up_sema);
		qdio_shutdown(cdev,QDIO_FLAG_CLEANUP_USING_CLEAR);
		return result;
	}
	
	/* Timeout is cared for already by using ccw_device_start_timeout(). */
	wait_event_interruptible(cdev->private->wait_q,
		 irq_ptr->state == QDIO_IRQ_STATE_ESTABLISHED ||
		 irq_ptr->state == QDIO_IRQ_STATE_ERR);

	if (irq_ptr->state == QDIO_IRQ_STATE_ESTABLISHED)
		result = 0;
	else {
		up(&irq_ptr->setting_up_sema);
		qdio_shutdown(cdev, QDIO_FLAG_CLEANUP_USING_CLEAR);
		return -EIO;
	}

	qdio_get_ssqd_information(irq_ptr);
	/* if this gets set once, we're running under VM and can omit SVSes */
	if (irq_ptr->qdioac&CHSC_FLAG_SIGA_SYNC_NECESSARY)
		omit_svs=1;

	sprintf(dbf_text,"qdioac%2x",irq_ptr->qdioac);
	QDIO_DBF_TEXT2(0,setup,dbf_text);

	sprintf(dbf_text,"qib ac%2x",irq_ptr->qib.ac);
	QDIO_DBF_TEXT2(0,setup,dbf_text);

	irq_ptr->hydra_gives_outbound_pcis=
		irq_ptr->qib.ac&QIB_AC_OUTBOUND_PCI_SUPPORTED;
	irq_ptr->sync_done_on_outb_pcis=
		irq_ptr->qdioac&CHSC_FLAG_SIGA_SYNC_DONE_ON_OUTB_PCIS;

	qdio_initialize_set_siga_flags_input(irq_ptr);
	qdio_initialize_set_siga_flags_output(irq_ptr);

	up(&irq_ptr->setting_up_sema);

	return result;
	
}

int
qdio_activate(struct ccw_device *cdev, int flags)
{
	struct qdio_irq *irq_ptr;
	int i,result=0,result2;
	unsigned long saveflags;
	char dbf_text[20]; /* see qdio_initialize */

	irq_ptr = cdev->private->qdio_data;
	if (!irq_ptr)
		return -ENODEV;

	if (cdev->private->state != DEV_STATE_ONLINE)
		return -EINVAL;

	down(&irq_ptr->setting_up_sema);
	if (irq_ptr->state==QDIO_IRQ_STATE_INACTIVE) {
		result=-EBUSY;
		goto out;
	}

	sprintf(dbf_text,"qact%4x", irq_ptr->schid.sch_no);
	QDIO_DBF_TEXT2(0,setup,dbf_text);
	QDIO_DBF_TEXT2(0,trace,dbf_text);

	/* activate q */
	irq_ptr->ccw.cmd_code=irq_ptr->aqueue.cmd;
	irq_ptr->ccw.flags=CCW_FLAG_SLI;
	irq_ptr->ccw.count=irq_ptr->aqueue.count;
	irq_ptr->ccw.cda=QDIO_GET_ADDR(0);

	spin_lock_irqsave(get_ccwdev_lock(cdev),saveflags);

	ccw_device_set_timeout(cdev, 0);
	ccw_device_set_options(cdev, CCWDEV_REPORT_ALL);
	result=ccw_device_start(cdev,&irq_ptr->ccw,QDIO_DOING_ACTIVATE,
				0, DOIO_DENY_PREFETCH);
	if (result) {
		result2=ccw_device_start(cdev,&irq_ptr->ccw,
					 QDIO_DOING_ACTIVATE,0,0);
		sprintf(dbf_text,"aq:io%4x",result);
		QDIO_DBF_TEXT2(1,setup,dbf_text);
		if (result2) {
			sprintf(dbf_text,"aq:io%4x",result);
			QDIO_DBF_TEXT2(1,setup,dbf_text);
		}
		QDIO_PRINT_WARN("activate queues on irq 0.%x.%04x: do_IO " \
				"returned %i, next try returned %i\n",
				irq_ptr->schid.ssid, irq_ptr->schid.sch_no,
				result, result2);
		result=result2;
	}

	spin_unlock_irqrestore(get_ccwdev_lock(cdev),saveflags);
	if (result)
		goto out;

	for (i=0;i<irq_ptr->no_input_qs;i++) {
		if (irq_ptr->is_thinint_irq) {
			/* 
			 * that way we know, that, if we will get interrupted
			 * by tiqdio_inbound_processing, qdio_unmark_q will
			 * not be called 
			 */
			qdio_reserve_q(irq_ptr->input_qs[i]);
			qdio_mark_tiq(irq_ptr->input_qs[i]);
			qdio_release_q(irq_ptr->input_qs[i]);
		}
	}

	if (flags&QDIO_FLAG_NO_INPUT_INTERRUPT_CONTEXT) {
		for (i=0;i<irq_ptr->no_input_qs;i++) {
			irq_ptr->input_qs[i]->is_input_q|=
				QDIO_FLAG_NO_INPUT_INTERRUPT_CONTEXT;
		}
	}

	wait_event_interruptible_timeout(cdev->private->wait_q,
					 ((irq_ptr->state ==
					  QDIO_IRQ_STATE_STOPPED) ||
					  (irq_ptr->state ==
					   QDIO_IRQ_STATE_ERR)),
					 QDIO_ACTIVATE_TIMEOUT);

	switch (irq_ptr->state) {
	case QDIO_IRQ_STATE_STOPPED:
	case QDIO_IRQ_STATE_ERR:
		up(&irq_ptr->setting_up_sema);
		qdio_shutdown(cdev, QDIO_FLAG_CLEANUP_USING_CLEAR);
		down(&irq_ptr->setting_up_sema);
		result = -EIO;
		break;
	default:
		qdio_set_state(irq_ptr, QDIO_IRQ_STATE_ACTIVE);
		result = 0;
	}
 out:
	up(&irq_ptr->setting_up_sema);

	return result;
}

/* buffers filled forwards again to make Rick happy */
static void
qdio_do_qdio_fill_input(struct qdio_q *q, unsigned int qidx,
			unsigned int count, struct qdio_buffer *buffers)
{
	struct qdio_irq *irq = (struct qdio_irq *) q->irq_ptr;
	int tmp = 0;

	qidx &= (QDIO_MAX_BUFFERS_PER_Q - 1);
	if (irq->is_qebsm) {
		while (count) {
			tmp = set_slsb(q, &qidx, SLSB_CU_INPUT_EMPTY, &count);
			if (!tmp)
				return;
		}
		return;
	}
	for (;;) {
		set_slsb(q, &qidx, SLSB_CU_INPUT_EMPTY, &count);
		count--;
		if (!count) break;
		qidx = (qidx + 1) & (QDIO_MAX_BUFFERS_PER_Q - 1);
	}
}

static void
qdio_do_qdio_fill_output(struct qdio_q *q, unsigned int qidx,
			 unsigned int count, struct qdio_buffer *buffers)
{
	struct qdio_irq *irq = (struct qdio_irq *) q->irq_ptr;
	int tmp = 0;

	qidx &= (QDIO_MAX_BUFFERS_PER_Q - 1);
	if (irq->is_qebsm) {
		while (count) {
			tmp = set_slsb(q, &qidx, SLSB_CU_OUTPUT_PRIMED, &count);
			if (!tmp)
				return;
		}
		return;
	}

	for (;;) {
		set_slsb(q, &qidx, SLSB_CU_OUTPUT_PRIMED, &count);
		count--;
		if (!count) break;
		qidx = (qidx + 1) & (QDIO_MAX_BUFFERS_PER_Q - 1);
	}
}

static void
do_qdio_handle_inbound(struct qdio_q *q, unsigned int callflags,
		       unsigned int qidx, unsigned int count,
		       struct qdio_buffer *buffers)
{
	int used_elements;

        /* This is the inbound handling of queues */
	used_elements=atomic_add_return(count, &q->number_of_buffers_used) - count;
	
	qdio_do_qdio_fill_input(q,qidx,count,buffers);
	
	if ((used_elements+count==QDIO_MAX_BUFFERS_PER_Q)&&
	    (callflags&QDIO_FLAG_UNDER_INTERRUPT))
		atomic_xchg(&q->polling,0);
	
	if (used_elements) 
		return;
	if (callflags&QDIO_FLAG_DONT_SIGA)
		return;
	if (q->siga_in) {
		int result;
		
		result=qdio_siga_input(q);
		if (result) {
			if (q->siga_error)
				q->error_status_flags|=
					QDIO_STATUS_MORE_THAN_ONE_SIGA_ERROR;
			q->error_status_flags|=QDIO_STATUS_LOOK_FOR_ERROR;
			q->siga_error=result;
		}
	}
		
	qdio_mark_q(q);
}

static void
do_qdio_handle_outbound(struct qdio_q *q, unsigned int callflags,
			unsigned int qidx, unsigned int count,
			struct qdio_buffer *buffers)
{
	int used_elements;
	unsigned int cnt, start_buf;
	unsigned char state = 0;
	struct qdio_irq *irq = (struct qdio_irq *) q->irq_ptr;

	/* This is the outbound handling of queues */
	qdio_do_qdio_fill_output(q,qidx,count,buffers);

	used_elements=atomic_add_return(count, &q->number_of_buffers_used) - count;

	if (callflags&QDIO_FLAG_DONT_SIGA) {
		qdio_perf_stat_inc(&perf_stats.outbound_cnt);
		return;
	}
	if (callflags & QDIO_FLAG_PCI_OUT)
		q->is_pci_out = 1;
	else
		q->is_pci_out = 0;
	if (q->is_iqdio_q) {
		/* one siga for every sbal */
		while (count--)
			qdio_kick_outbound_q(q);
			
		__qdio_outbound_processing(q);
	} else {
		/* under VM, we do a SIGA sync unconditionally */
		SYNC_MEMORY;
		else {
			/* 
			 * w/o shadow queues (else branch of
			 * SYNC_MEMORY :-/ ), we try to
			 * fast-requeue buffers 
			 */
			if (irq->is_qebsm) {
				cnt = 1;
				start_buf = ((qidx+QDIO_MAX_BUFFERS_PER_Q-1) &
					     (QDIO_MAX_BUFFERS_PER_Q-1));
				qdio_do_eqbs(q, &state, &start_buf, &cnt);
			} else
				state = q->slsb.acc.val[(qidx+QDIO_MAX_BUFFERS_PER_Q-1)
					&(QDIO_MAX_BUFFERS_PER_Q-1) ];
			 if (state != SLSB_CU_OUTPUT_PRIMED) {
				qdio_kick_outbound_q(q);
			} else {
				QDIO_DBF_TEXT3(0,trace, "fast-req");
				qdio_perf_stat_inc(&perf_stats.fast_reqs);
			}
		}
		/* 
		 * only marking the q could take too long,
		 * the upper layer module could do a lot of
		 * traffic in that time 
		 */
		__qdio_outbound_processing(q);
	}

	qdio_perf_stat_inc(&perf_stats.outbound_cnt);
}

/* count must be 1 in iqdio */
int
do_QDIO(struct ccw_device *cdev,unsigned int callflags,
	unsigned int queue_number, unsigned int qidx,
	unsigned int count,struct qdio_buffer *buffers)
{
	struct qdio_irq *irq_ptr;
#ifdef CONFIG_QDIO_DEBUG
	char dbf_text[20];

	sprintf(dbf_text,"doQD%04x",cdev->private->schid.sch_no);
 	QDIO_DBF_TEXT3(0,trace,dbf_text);
#endif /* CONFIG_QDIO_DEBUG */

	if ( (qidx>QDIO_MAX_BUFFERS_PER_Q) ||
	     (count>QDIO_MAX_BUFFERS_PER_Q) ||
	     (queue_number>QDIO_MAX_QUEUES_PER_IRQ) )
		return -EINVAL;

	if (count==0)
		return 0;

	irq_ptr = cdev->private->qdio_data;
	if (!irq_ptr)
		return -ENODEV;

#ifdef CONFIG_QDIO_DEBUG
	if (callflags&QDIO_FLAG_SYNC_INPUT)
		QDIO_DBF_HEX3(0,trace,&irq_ptr->input_qs[queue_number],
			      sizeof(void*));
	else
		QDIO_DBF_HEX3(0,trace,&irq_ptr->output_qs[queue_number],
			      sizeof(void*));
	sprintf(dbf_text,"flag%04x",callflags);
	QDIO_DBF_TEXT3(0,trace,dbf_text);
	sprintf(dbf_text,"qi%02xct%02x",qidx,count);
	QDIO_DBF_TEXT3(0,trace,dbf_text);
#endif /* CONFIG_QDIO_DEBUG */

	if (irq_ptr->state!=QDIO_IRQ_STATE_ACTIVE)
		return -EBUSY;

	if (callflags&QDIO_FLAG_SYNC_INPUT)
		do_qdio_handle_inbound(irq_ptr->input_qs[queue_number],
				       callflags, qidx, count, buffers);
	else if (callflags&QDIO_FLAG_SYNC_OUTPUT)
		do_qdio_handle_outbound(irq_ptr->output_qs[queue_number],
					callflags, qidx, count, buffers);
	else {
		QDIO_DBF_TEXT3(1,trace,"doQD:inv");
		return -EINVAL;
	}
	return 0;
}

static int
qdio_perf_procfile_read(char *buffer, char **buffer_location, off_t offset,
			int buffer_length, int *eof, void *data)
{
        int c=0;

        /* we are always called with buffer_length=4k, so we all
           deliver on the first read */
        if (offset>0)
		return 0;

#define _OUTP_IT(x...) c+=sprintf(buffer+c,x)
#ifdef CONFIG_64BIT
	_OUTP_IT("Number of tasklet runs (total)                  : %li\n",
		 (long)atomic64_read(&perf_stats.tl_runs));
	_OUTP_IT("Inbound tasklet runs      tried/retried         : %li/%li\n",
		 (long)atomic64_read(&perf_stats.inbound_tl_runs),
		 (long)atomic64_read(&perf_stats.inbound_tl_runs_resched));
	_OUTP_IT("Inbound-thin tasklet runs tried/retried         : %li/%li\n",
		 (long)atomic64_read(&perf_stats.inbound_thin_tl_runs),
		 (long)atomic64_read(&perf_stats.inbound_thin_tl_runs_resched));
	_OUTP_IT("Outbound tasklet runs     tried/retried         : %li/%li\n",
		 (long)atomic64_read(&perf_stats.outbound_tl_runs),
		 (long)atomic64_read(&perf_stats.outbound_tl_runs_resched));
	_OUTP_IT("\n");
	_OUTP_IT("Number of SIGA sync's issued                    : %li\n",
		 (long)atomic64_read(&perf_stats.siga_syncs));
	_OUTP_IT("Number of SIGA in's issued                      : %li\n",
		 (long)atomic64_read(&perf_stats.siga_ins));
	_OUTP_IT("Number of SIGA out's issued                     : %li\n",
		 (long)atomic64_read(&perf_stats.siga_outs));
	_OUTP_IT("Number of PCIs caught                           : %li\n",
		 (long)atomic64_read(&perf_stats.pcis));
	_OUTP_IT("Number of adapter interrupts caught             : %li\n",
		 (long)atomic64_read(&perf_stats.thinints));
	_OUTP_IT("Number of fast requeues (outg. SBALs w/o SIGA)  : %li\n",
		 (long)atomic64_read(&perf_stats.fast_reqs));
	_OUTP_IT("\n");
	_OUTP_IT("Number of inbound transfers                     : %li\n",
		 (long)atomic64_read(&perf_stats.inbound_cnt));
	_OUTP_IT("Number of do_QDIOs outbound                     : %li\n",
		 (long)atomic64_read(&perf_stats.outbound_cnt));
#else /* CONFIG_64BIT */
	_OUTP_IT("Number of tasklet runs (total)                  : %i\n",
		 atomic_read(&perf_stats.tl_runs));
	_OUTP_IT("Inbound tasklet runs      tried/retried         : %i/%i\n",
		 atomic_read(&perf_stats.inbound_tl_runs),
		 atomic_read(&perf_stats.inbound_tl_runs_resched));
	_OUTP_IT("Inbound-thin tasklet runs tried/retried         : %i/%i\n",
		 atomic_read(&perf_stats.inbound_thin_tl_runs),
		 atomic_read(&perf_stats.inbound_thin_tl_runs_resched));
	_OUTP_IT("Outbound tasklet runs     tried/retried         : %i/%i\n",
		 atomic_read(&perf_stats.outbound_tl_runs),
		 atomic_read(&perf_stats.outbound_tl_runs_resched));
	_OUTP_IT("\n");
	_OUTP_IT("Number of SIGA sync's issued                    : %i\n",
		 atomic_read(&perf_stats.siga_syncs));
	_OUTP_IT("Number of SIGA in's issued                      : %i\n",
		 atomic_read(&perf_stats.siga_ins));
	_OUTP_IT("Number of SIGA out's issued                     : %i\n",
		 atomic_read(&perf_stats.siga_outs));
	_OUTP_IT("Number of PCIs caught                           : %i\n",
		 atomic_read(&perf_stats.pcis));
	_OUTP_IT("Number of adapter interrupts caught             : %i\n",
		 atomic_read(&perf_stats.thinints));
	_OUTP_IT("Number of fast requeues (outg. SBALs w/o SIGA)  : %i\n",
		 atomic_read(&perf_stats.fast_reqs));
	_OUTP_IT("\n");
	_OUTP_IT("Number of inbound transfers                     : %i\n",
		 atomic_read(&perf_stats.inbound_cnt));
	_OUTP_IT("Number of do_QDIOs outbound                     : %i\n",
		 atomic_read(&perf_stats.outbound_cnt));
#endif /* CONFIG_64BIT */
	_OUTP_IT("\n");

        return c;
}

static struct proc_dir_entry *qdio_perf_proc_file;

static void
qdio_add_procfs_entry(void)
{
        proc_perf_file_registration=0;
	qdio_perf_proc_file=create_proc_entry(QDIO_PERF,
					      S_IFREG|0444,&proc_root);
	if (qdio_perf_proc_file) {
		qdio_perf_proc_file->read_proc=&qdio_perf_procfile_read;
	} else proc_perf_file_registration=-1;

        if (proc_perf_file_registration)
                QDIO_PRINT_WARN("was not able to register perf. " \
				"proc-file (%i).\n",
				proc_perf_file_registration);
}

static void
qdio_remove_procfs_entry(void)
{
        if (!proc_perf_file_registration) /* means if it went ok earlier */
		remove_proc_entry(QDIO_PERF,&proc_root);
}

/**
 * attributes in sysfs
 *****************************************************************************/

static ssize_t
qdio_performance_stats_show(struct bus_type *bus, char *buf)
{
	return sprintf(buf, "%i\n", qdio_performance_stats ? 1 : 0);
}

static ssize_t
qdio_performance_stats_store(struct bus_type *bus, const char *buf, size_t count)
{
	char *tmp;
	int i;

	i = simple_strtoul(buf, &tmp, 16);
	if ((i == 0) || (i == 1)) {
		if (i == qdio_performance_stats)
			return count;
		qdio_performance_stats = i;
		if (i==0) {
			/* reset perf. stat. info */
#ifdef CONFIG_64BIT
			atomic64_set(&perf_stats.tl_runs, 0);
			atomic64_set(&perf_stats.outbound_tl_runs, 0);
			atomic64_set(&perf_stats.inbound_tl_runs, 0);
			atomic64_set(&perf_stats.inbound_tl_runs_resched, 0);
			atomic64_set(&perf_stats.inbound_thin_tl_runs, 0);
			atomic64_set(&perf_stats.inbound_thin_tl_runs_resched,
				     0);
			atomic64_set(&perf_stats.siga_outs, 0);
			atomic64_set(&perf_stats.siga_ins, 0);
			atomic64_set(&perf_stats.siga_syncs, 0);
			atomic64_set(&perf_stats.pcis, 0);
			atomic64_set(&perf_stats.thinints, 0);
			atomic64_set(&perf_stats.fast_reqs, 0);
			atomic64_set(&perf_stats.outbound_cnt, 0);
			atomic64_set(&perf_stats.inbound_cnt, 0);
#else /* CONFIG_64BIT */
			atomic_set(&perf_stats.tl_runs, 0);
			atomic_set(&perf_stats.outbound_tl_runs, 0);
			atomic_set(&perf_stats.inbound_tl_runs, 0);
			atomic_set(&perf_stats.inbound_tl_runs_resched, 0);
			atomic_set(&perf_stats.inbound_thin_tl_runs, 0);
			atomic_set(&perf_stats.inbound_thin_tl_runs_resched, 0);
			atomic_set(&perf_stats.siga_outs, 0);
			atomic_set(&perf_stats.siga_ins, 0);
			atomic_set(&perf_stats.siga_syncs, 0);
			atomic_set(&perf_stats.pcis, 0);
			atomic_set(&perf_stats.thinints, 0);
			atomic_set(&perf_stats.fast_reqs, 0);
			atomic_set(&perf_stats.outbound_cnt, 0);
			atomic_set(&perf_stats.inbound_cnt, 0);
#endif /* CONFIG_64BIT */
		}
	} else {
		QDIO_PRINT_WARN("QDIO performance_stats: write 0 or 1 to this file!\n");
		return -EINVAL;
	}
	return count;
}

static BUS_ATTR(qdio_performance_stats, 0644, qdio_performance_stats_show,
			qdio_performance_stats_store);

static void
tiqdio_register_thinints(void)
{
	char dbf_text[20];
	register_thinint_result=
		s390_register_adapter_interrupt(&tiqdio_thinint_handler);
	if (register_thinint_result) {
		sprintf(dbf_text,"regthn%x",(register_thinint_result&0xff));
		QDIO_DBF_TEXT0(0,setup,dbf_text);
		QDIO_PRINT_ERR("failed to register adapter handler " \
			       "(rc=%i).\nAdapter interrupts might " \
			       "not work. Continuing.\n",
			       register_thinint_result);
	}
}

static void
tiqdio_unregister_thinints(void)
{
	if (!register_thinint_result)
		s390_unregister_adapter_interrupt(&tiqdio_thinint_handler);
}

static int
qdio_get_qdio_memory(void)
{
	int i;
	indicator_used[0]=1;

	for (i=1;i<INDICATORS_PER_CACHELINE;i++)
		indicator_used[i]=0;
	indicators = kzalloc(sizeof(__u32)*(INDICATORS_PER_CACHELINE),
				   GFP_KERNEL);
       	if (!indicators)
		return -ENOMEM;
	return 0;
}

static void
qdio_release_qdio_memory(void)
{
	kfree(indicators);
}


static void
qdio_unregister_dbf_views(void)
{
	if (qdio_dbf_setup)
		debug_unregister(qdio_dbf_setup);
	if (qdio_dbf_sbal)
		debug_unregister(qdio_dbf_sbal);
	if (qdio_dbf_sense)
		debug_unregister(qdio_dbf_sense);
	if (qdio_dbf_trace)
		debug_unregister(qdio_dbf_trace);
#ifdef CONFIG_QDIO_DEBUG
        if (qdio_dbf_slsb_out)
                debug_unregister(qdio_dbf_slsb_out);
        if (qdio_dbf_slsb_in)
                debug_unregister(qdio_dbf_slsb_in);
#endif /* CONFIG_QDIO_DEBUG */
}

static int
qdio_register_dbf_views(void)
{
	qdio_dbf_setup=debug_register(QDIO_DBF_SETUP_NAME,
				      QDIO_DBF_SETUP_PAGES,
				      QDIO_DBF_SETUP_NR_AREAS,
				      QDIO_DBF_SETUP_LEN);
	if (!qdio_dbf_setup)
		goto oom;
	debug_register_view(qdio_dbf_setup,&debug_hex_ascii_view);
	debug_set_level(qdio_dbf_setup,QDIO_DBF_SETUP_LEVEL);

	qdio_dbf_sbal=debug_register(QDIO_DBF_SBAL_NAME,
				     QDIO_DBF_SBAL_PAGES,
				     QDIO_DBF_SBAL_NR_AREAS,
				     QDIO_DBF_SBAL_LEN);
	if (!qdio_dbf_sbal)
		goto oom;

	debug_register_view(qdio_dbf_sbal,&debug_hex_ascii_view);
	debug_set_level(qdio_dbf_sbal,QDIO_DBF_SBAL_LEVEL);

	qdio_dbf_sense=debug_register(QDIO_DBF_SENSE_NAME,
				      QDIO_DBF_SENSE_PAGES,
				      QDIO_DBF_SENSE_NR_AREAS,
				      QDIO_DBF_SENSE_LEN);
	if (!qdio_dbf_sense)
		goto oom;

	debug_register_view(qdio_dbf_sense,&debug_hex_ascii_view);
	debug_set_level(qdio_dbf_sense,QDIO_DBF_SENSE_LEVEL);

	qdio_dbf_trace=debug_register(QDIO_DBF_TRACE_NAME,
				      QDIO_DBF_TRACE_PAGES,
				      QDIO_DBF_TRACE_NR_AREAS,
				      QDIO_DBF_TRACE_LEN);
	if (!qdio_dbf_trace)
		goto oom;

	debug_register_view(qdio_dbf_trace,&debug_hex_ascii_view);
	debug_set_level(qdio_dbf_trace,QDIO_DBF_TRACE_LEVEL);

#ifdef CONFIG_QDIO_DEBUG
        qdio_dbf_slsb_out=debug_register(QDIO_DBF_SLSB_OUT_NAME,
                                         QDIO_DBF_SLSB_OUT_PAGES,
                                         QDIO_DBF_SLSB_OUT_NR_AREAS,
                                         QDIO_DBF_SLSB_OUT_LEN);
        if (!qdio_dbf_slsb_out)
		goto oom;
        debug_register_view(qdio_dbf_slsb_out,&debug_hex_ascii_view);
        debug_set_level(qdio_dbf_slsb_out,QDIO_DBF_SLSB_OUT_LEVEL);

        qdio_dbf_slsb_in=debug_register(QDIO_DBF_SLSB_IN_NAME,
                                        QDIO_DBF_SLSB_IN_PAGES,
                                        QDIO_DBF_SLSB_IN_NR_AREAS,
                                        QDIO_DBF_SLSB_IN_LEN);
        if (!qdio_dbf_slsb_in)
		goto oom;
        debug_register_view(qdio_dbf_slsb_in,&debug_hex_ascii_view);
        debug_set_level(qdio_dbf_slsb_in,QDIO_DBF_SLSB_IN_LEVEL);
#endif /* CONFIG_QDIO_DEBUG */
	return 0;
oom:
	QDIO_PRINT_ERR("not enough memory for dbf.\n");
	qdio_unregister_dbf_views();
	return -ENOMEM;
}

static void *qdio_mempool_alloc(gfp_t gfp_mask, void *size)
{
	return (void *) get_zeroed_page(gfp_mask|GFP_DMA);
}

static void qdio_mempool_free(void *element, void *size)
{
	free_page((unsigned long) element);
}

static int __init
init_QDIO(void)
{
	int res;
	void *ptr;

	printk("qdio: loading %s\n",version);

	res=qdio_get_qdio_memory();
	if (res)
		return res;

	res = qdio_register_dbf_views();
	if (res)
		return res;

	QDIO_DBF_TEXT0(0,setup,"initQDIO");
	res = bus_create_file(&ccw_bus_type, &bus_attr_qdio_performance_stats);

	memset((void*)&perf_stats,0,sizeof(perf_stats));
	QDIO_DBF_TEXT0(0,setup,"perfstat");
	ptr=&perf_stats;
	QDIO_DBF_HEX0(0,setup,&ptr,sizeof(void*));

	qdio_add_procfs_entry();

	qdio_mempool_scssc = mempool_create(QDIO_MEMPOOL_SCSSC_ELEMENTS,
					    qdio_mempool_alloc,
					    qdio_mempool_free, NULL);

	if (tiqdio_check_chsc_availability())
		QDIO_PRINT_ERR("Not all CHSCs supported. Continuing.\n");

	tiqdio_register_thinints();

	return 0;
 }

static void __exit
cleanup_QDIO(void)
{
	tiqdio_unregister_thinints();
	qdio_remove_procfs_entry();
	qdio_release_qdio_memory();
	qdio_unregister_dbf_views();
	mempool_destroy(qdio_mempool_scssc);
	bus_remove_file(&ccw_bus_type, &bus_attr_qdio_performance_stats);
  	printk("qdio: %s: module removed\n",version);
}

module_init(init_QDIO);
module_exit(cleanup_QDIO);

EXPORT_SYMBOL(qdio_allocate);
EXPORT_SYMBOL(qdio_establish);
EXPORT_SYMBOL(qdio_initialize);
EXPORT_SYMBOL(qdio_activate);
EXPORT_SYMBOL(do_QDIO);
EXPORT_SYMBOL(qdio_shutdown);
EXPORT_SYMBOL(qdio_free);
EXPORT_SYMBOL(qdio_cleanup);
EXPORT_SYMBOL(qdio_synchronize);
