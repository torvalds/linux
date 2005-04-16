/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.6.x
 * Copyright (C) 2003-2004 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/

/* Management functions for various lists */

/* __add_to_done_queue()
 * 
 * Place SRB command on done queue.
 *
 * Input:
 *      ha           = host pointer
 *      sp           = srb pointer.
 * Locking:
 * 	this function assumes the ha->list_lock is already taken
 */
static inline void 
__add_to_done_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	/*
        if (sp->state != SRB_NO_QUEUE_STATE && 
        	sp->state != SRB_ACTIVE_STATE)
		BUG();
	*/

        /* Place block on done queue */
        sp->cmd->host_scribble = (unsigned char *) NULL;
        sp->state = SRB_DONE_STATE;
        list_add_tail(&sp->list,&ha->done_queue);
        ha->done_q_cnt++;
	sp->ha = ha;
}

static inline void 
__add_to_retry_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	/*
        if( sp->state != SRB_NO_QUEUE_STATE && 
        	sp->state != SRB_ACTIVE_STATE)
		BUG();
	*/

        /* Place block on retry queue */
        list_add_tail(&sp->list,&ha->retry_queue);
        ha->retry_q_cnt++;
        sp->flags |= SRB_WATCHDOG;
        sp->state = SRB_RETRY_STATE;
	sp->ha = ha;
}

static inline void 
__add_to_scsi_retry_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	/*
        if( sp->state != SRB_NO_QUEUE_STATE && 
        	sp->state != SRB_ACTIVE_STATE)
		BUG();
	*/

        /* Place block on retry queue */
        list_add_tail(&sp->list,&ha->scsi_retry_queue);
        ha->scsi_retry_q_cnt++;
        sp->state = SRB_SCSI_RETRY_STATE;
	sp->ha = ha;
}

static inline void 
add_to_done_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        unsigned long flags;

        spin_lock_irqsave(&ha->list_lock, flags);
        __add_to_done_queue(ha,sp);
        spin_unlock_irqrestore(&ha->list_lock, flags);
}

static inline void 
add_to_free_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	mempool_free(sp, ha->srb_mempool);
}

static inline void 
add_to_retry_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        unsigned long flags;

        spin_lock_irqsave(&ha->list_lock, flags);
        __add_to_retry_queue(ha,sp);
        spin_unlock_irqrestore(&ha->list_lock, flags);
}

static inline void 
add_to_scsi_retry_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        unsigned long flags;

        spin_lock_irqsave(&ha->list_lock, flags);
        __add_to_scsi_retry_queue(ha,sp);
        spin_unlock_irqrestore(&ha->list_lock, flags);
}

/*
 * __del_from_retry_queue
 *      Function used to remove a command block from the
 *      watchdog timer queue.
 *
 *      Note: Must insure that command is on watchdog
 *            list before calling del_from_retry_queue
 *            if (sp->flags & SRB_WATCHDOG)
 *
 * Input: 
 *      ha = adapter block pointer.
 *      sp = srb pointer.
 * Locking:
 *	this function assumes the list_lock is already taken
 */
static inline void 
__del_from_retry_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        list_del_init(&sp->list);

        sp->flags &= ~(SRB_WATCHDOG | SRB_BUSY);
        sp->state = SRB_NO_QUEUE_STATE;
        ha->retry_q_cnt--;
}

/*
 * __del_from_scsi_retry_queue
 *      Function used to remove a command block from the
 *      scsi retry queue.
 *
 * Input: 
 *      ha = adapter block pointer.
 *      sp = srb pointer.
 * Locking:
 *	this function assumes the list_lock is already taken
 */
static inline void 
__del_from_scsi_retry_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        list_del_init(&sp->list);

        ha->scsi_retry_q_cnt--;
        sp->state = SRB_NO_QUEUE_STATE;
}

/*
 * del_from_retry_queue
 *      Function used to remove a command block from the
 *      watchdog timer queue.
 *
 *      Note: Must insure that command is on watchdog
 *            list before calling del_from_retry_queue
 *            if (sp->flags & SRB_WATCHDOG)
 *
 * Input: 
 *      ha = adapter block pointer.
 *      sp = srb pointer.
 * Locking:
 *	this function takes and releases the list_lock
 */
static inline void 
del_from_retry_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        unsigned long flags;

        /*	if (unlikely(!(sp->flags & SRB_WATCHDOG)))
        		BUG();*/
        spin_lock_irqsave(&ha->list_lock, flags);

        /*	if (unlikely(list_empty(&ha->retry_queue)))
        		BUG();*/

        __del_from_retry_queue(ha,sp);

        spin_unlock_irqrestore(&ha->list_lock, flags);
}
/*
 * del_from_scsi_retry_queue
 *      Function used to remove a command block from the
 *      scsi retry queue.
 *
 * Input: 
 *      ha = adapter block pointer.
 *      sp = srb pointer.
 * Locking:
 *	this function takes and releases the list_lock
 */
static inline void 
del_from_scsi_retry_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        unsigned long flags;

        spin_lock_irqsave(&ha->list_lock, flags);

        /*	if (unlikely(list_empty(&ha->scsi_retry_queue)))
        		BUG();*/

        __del_from_scsi_retry_queue(ha,sp);

        spin_unlock_irqrestore(&ha->list_lock, flags);
}

/*
 * __add_to_pending_queue
 *      Add the standard SCB job to the bottom of standard SCB commands.
 *
 * Input:
 * COMPLETE!!!
 *      q  = SCSI LU pointer.
 *      sp = srb pointer.
 *      SCSI_LU_Q lock must be already obtained.
 */
static inline int 
__add_to_pending_queue(struct scsi_qla_host *ha, srb_t * sp)
{
	int	empty;
	/*
        if( sp->state != SRB_NO_QUEUE_STATE &&
        	sp->state != SRB_FREE_STATE &&
        	sp->state != SRB_ACTIVE_STATE)
		BUG();
	*/

	empty = list_empty(&ha->pending_queue);
	list_add_tail(&sp->list, &ha->pending_queue);
	ha->qthreads++;
	sp->state = SRB_PENDING_STATE;

	return (empty);
}

static inline void 
__add_to_pending_queue_head(struct scsi_qla_host *ha, srb_t * sp)
{
	/*
        if( sp->state != SRB_NO_QUEUE_STATE && 
        	sp->state != SRB_FREE_STATE &&
        	sp->state != SRB_ACTIVE_STATE)
		BUG();
	*/

	list_add(&sp->list, &ha->pending_queue);
	ha->qthreads++;
	sp->state = SRB_PENDING_STATE;
}

static inline int
add_to_pending_queue(struct scsi_qla_host *ha, srb_t *sp)
{
	int	empty;
	unsigned long flags;

	spin_lock_irqsave(&ha->list_lock, flags);
	empty = __add_to_pending_queue(ha, sp);
	spin_unlock_irqrestore(&ha->list_lock, flags);

	return (empty);
}
static inline void
add_to_pending_queue_head(struct scsi_qla_host *ha, srb_t *sp)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->list_lock, flags);
	__add_to_pending_queue_head(ha, sp);
	spin_unlock_irqrestore(&ha->list_lock, flags);
}

static inline void
__del_from_pending_queue(struct scsi_qla_host *ha, srb_t *sp)
{
	list_del_init(&sp->list);
	ha->qthreads--;
	sp->state = SRB_NO_QUEUE_STATE;
}

/*
 * Failover Stuff.
 */
static inline void
__add_to_failover_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	/*
        if( sp->state != SRB_NO_QUEUE_STATE && 
        	sp->state != SRB_ACTIVE_STATE)
		BUG();
	*/

        list_add_tail(&sp->list,&ha->failover_queue);
        ha->failover_cnt++;
        sp->state = SRB_FAILOVER_STATE;
	sp->ha = ha;
}

static inline void add_to_failover_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        unsigned long flags;

        spin_lock_irqsave(&ha->list_lock, flags);

        __add_to_failover_queue(ha,sp);

        spin_unlock_irqrestore(&ha->list_lock, flags);
}
static inline void __del_from_failover_queue(struct scsi_qla_host * ha, srb_t *
                sp)
{
        ha->failover_cnt--;
        list_del_init(&sp->list);
        sp->state = SRB_NO_QUEUE_STATE;
}

static inline void del_from_failover_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        unsigned long flags;

        spin_lock_irqsave(&ha->list_lock, flags);

        __del_from_failover_queue(ha,sp);

        spin_unlock_irqrestore(&ha->list_lock, flags);
}

static inline void 
del_from_pending_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        unsigned long flags;

        spin_lock_irqsave(&ha->list_lock, flags);

        __del_from_pending_queue(ha,sp);

        spin_unlock_irqrestore(&ha->list_lock, flags);
}
