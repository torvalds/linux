/*
 * Definitions used in MIPS MT SMTC "Interprocessor Interrupt" code.
 */
#ifndef __ASM_SMTC_IPI_H
#define __ASM_SMTC_IPI_H

#include <linux/spinlock.h>

//#define SMTC_IPI_DEBUG

#ifdef SMTC_IPI_DEBUG
#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>
#endif /* SMTC_IPI_DEBUG */

/*
 * An IPI "message"
 */

struct smtc_ipi {
	struct smtc_ipi *flink;
	int type;
	void *arg;
	int dest;
#ifdef	SMTC_IPI_DEBUG
	int sender;
	long stamp;
#endif /* SMTC_IPI_DEBUG */
};

/*
 * Defined IPI Types
 */

#define LINUX_SMP_IPI 1
#define SMTC_CLOCK_TICK 2
#define IRQ_AFFINITY_IPI 3

/*
 * A queue of IPI messages
 */

struct smtc_ipi_q {
	struct smtc_ipi *head;
	spinlock_t lock;
	struct smtc_ipi *tail;
	int depth;
	int resched_flag;	/* reschedule already queued */
};

static inline void smtc_ipi_nq(struct smtc_ipi_q *q, struct smtc_ipi *p)
{
	unsigned long flags;

	spin_lock_irqsave(&q->lock, flags);
	if (q->head == NULL)
		q->head = q->tail = p;
	else
		q->tail->flink = p;
	p->flink = NULL;
	q->tail = p;
	q->depth++;
#ifdef	SMTC_IPI_DEBUG
	p->sender = read_c0_tcbind();
	p->stamp = read_c0_count();
#endif /* SMTC_IPI_DEBUG */
	spin_unlock_irqrestore(&q->lock, flags);
}

static inline struct smtc_ipi *__smtc_ipi_dq(struct smtc_ipi_q *q)
{
	struct smtc_ipi *p;

	if (q->head == NULL)
		p = NULL;
	else {
		p = q->head;
		q->head = q->head->flink;
		q->depth--;
		/* Arguably unnecessary, but leaves queue cleaner */
		if (q->head == NULL)
			q->tail = NULL;
	}

	return p;
}

static inline struct smtc_ipi *smtc_ipi_dq(struct smtc_ipi_q *q)
{
	unsigned long flags;
	struct smtc_ipi *p;

	spin_lock_irqsave(&q->lock, flags);
	p = __smtc_ipi_dq(q);
	spin_unlock_irqrestore(&q->lock, flags);

	return p;
}

static inline void smtc_ipi_req(struct smtc_ipi_q *q, struct smtc_ipi *p)
{
	unsigned long flags;

	spin_lock_irqsave(&q->lock, flags);
	if (q->head == NULL) {
		q->head = q->tail = p;
		p->flink = NULL;
	} else {
		p->flink = q->head;
		q->head = p;
	}
	q->depth++;
	spin_unlock_irqrestore(&q->lock, flags);
}

static inline int smtc_ipi_qdepth(struct smtc_ipi_q *q)
{
	unsigned long flags;
	int retval;

	spin_lock_irqsave(&q->lock, flags);
	retval = q->depth;
	spin_unlock_irqrestore(&q->lock, flags);
	return retval;
}

extern void smtc_send_ipi(int cpu, int type, unsigned int action);

#endif /* __ASM_SMTC_IPI_H */
