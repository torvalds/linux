/*
 * HND generic pktq operation primitives
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: $
 */

#include <typedefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <hnd_pktq.h>

/*
 * osl multiple-precedence packet queue
 * hi_prec is always >= the number of the highest non-empty precedence
 */
void * BCMFASTPATH
pktq_penq(struct pktq *pq, int prec, void *p)
{
	struct pktq_prec *q;

	ASSERT(prec >= 0 && prec < pq->num_prec);
	/* queueing chains not allowed */
	ASSERT(!((PKTLINK(p) != NULL) && (PKTLINK(p) != p)));
	ASSERT(!pktq_full(pq));
	ASSERT(!pktq_pfull(pq, prec));
	PKTSETLINK(p, NULL);

	q = &pq->q[prec];

	if (q->head)
		PKTSETLINK(q->tail, p);
	else
		q->head = p;

	q->tail = p;
	q->len++;

	pq->len++;

	if (pq->hi_prec < prec)
		pq->hi_prec = (uint8)prec;

	return p;
}

void * BCMFASTPATH
pktq_penq_head(struct pktq *pq, int prec, void *p)
{
	struct pktq_prec *q;

	ASSERT(prec >= 0 && prec < pq->num_prec);
	/* queueing chains not allowed */
	ASSERT(!((PKTLINK(p) != NULL) && (PKTLINK(p) != p)));
	ASSERT(!pktq_full(pq));
	ASSERT(!pktq_pfull(pq, prec));
	PKTSETLINK(p, NULL);

	q = &pq->q[prec];

	if (q->head == NULL)
		q->tail = p;

	PKTSETLINK(p, q->head);
	q->head = p;
	q->len++;

	pq->len++;

	if (pq->hi_prec < prec)
		pq->hi_prec = (uint8)prec;

	return p;
}

/*
 * Append spktq 'list' to the tail of pktq 'pq'
 */
void BCMFASTPATH
pktq_append(struct pktq *pq, int prec, struct spktq *list)
{
	struct pktq_prec *q;
	struct pktq_prec *list_q;

	list_q = &list->q[0];

	/* empty list check */
	if (list_q->head == NULL)
		return;

	ASSERT(prec >= 0 && prec < pq->num_prec);
	ASSERT(PKTLINK(list_q->tail) == NULL);         /* terminated list */

	ASSERT(!pktq_full(pq));
	ASSERT(!pktq_pfull(pq, prec));

	q = &pq->q[prec];

	if (q->head)
		PKTSETLINK(q->tail, list_q->head);
	else
		q->head = list_q->head;

	q->tail = list_q->tail;
	q->len += list_q->len;
	pq->len += list_q->len;

	if (pq->hi_prec < prec)
		pq->hi_prec = (uint8)prec;

	list_q->head = NULL;
	list_q->tail = NULL;
	list_q->len = 0;
	list->len = 0;
}

/*
 * Prepend spktq 'list' to the head of pktq 'pq'
 */
void BCMFASTPATH
pktq_prepend(struct pktq *pq, int prec, struct spktq *list)
{
	struct pktq_prec *q;
	struct pktq_prec *list_q;

	list_q = &list->q[0];

	/* empty list check */
	if (list_q->head == NULL)
		return;

	ASSERT(prec >= 0 && prec < pq->num_prec);
	ASSERT(PKTLINK(list_q->tail) == NULL);         /* terminated list */

	ASSERT(!pktq_full(pq));
	ASSERT(!pktq_pfull(pq, prec));

	q = &pq->q[prec];

	/* set the tail packet of list to point at the former pq head */
	PKTSETLINK(list_q->tail, q->head);
	/* the new q head is the head of list */
	q->head = list_q->head;

	/* If the q tail was non-null, then it stays as is.
	 * If the q tail was null, it is now the tail of list
	 */
	if (q->tail == NULL) {
		q->tail = list_q->tail;
	}

	q->len += list_q->len;
	pq->len += list_q->len;

	if (pq->hi_prec < prec)
		pq->hi_prec = (uint8)prec;

	list_q->head = NULL;
	list_q->tail = NULL;
	list_q->len = 0;
	list->len = 0;
}

void * BCMFASTPATH
pktq_pdeq(struct pktq *pq, int prec)
{
	struct pktq_prec *q;
	void *p;

	ASSERT(prec >= 0 && prec < pq->num_prec);

	q = &pq->q[prec];

	if ((p = q->head) == NULL)
		return NULL;

	if ((q->head = PKTLINK(p)) == NULL)
		q->tail = NULL;

	q->len--;

	pq->len--;

	PKTSETLINK(p, NULL);

	return p;
}

void * BCMFASTPATH
pktq_pdeq_prev(struct pktq *pq, int prec, void *prev_p)
{
	struct pktq_prec *q;
	void *p;

	ASSERT(prec >= 0 && prec < pq->num_prec);

	q = &pq->q[prec];

	if (prev_p == NULL)
		return NULL;

	if ((p = PKTLINK(prev_p)) == NULL)
		return NULL;

	q->len--;

	pq->len--;

	PKTSETLINK(prev_p, PKTLINK(p));
	PKTSETLINK(p, NULL);

	return p;
}

void * BCMFASTPATH
pktq_pdeq_with_fn(struct pktq *pq, int prec, ifpkt_cb_t fn, int arg)
{
	struct pktq_prec *q;
	void *p, *prev = NULL;

	ASSERT(prec >= 0 && prec < pq->num_prec);

	q = &pq->q[prec];
	p = q->head;

	while (p) {
		if (fn == NULL || (*fn)(p, arg)) {
			break;
		} else {
			prev = p;
			p = PKTLINK(p);
		}
	}
	if (p == NULL)
		return NULL;

	if (prev == NULL) {
		if ((q->head = PKTLINK(p)) == NULL) {
			q->tail = NULL;
		}
	} else {
		PKTSETLINK(prev, PKTLINK(p));
		if (q->tail == p) {
			q->tail = prev;
		}
	}

	q->len--;

	pq->len--;

	PKTSETLINK(p, NULL);

	return p;
}

void * BCMFASTPATH
pktq_pdeq_tail(struct pktq *pq, int prec)
{
	struct pktq_prec *q;
	void *p, *prev;

	ASSERT(prec >= 0 && prec < pq->num_prec);

	q = &pq->q[prec];

	if ((p = q->head) == NULL)
		return NULL;

	for (prev = NULL; p != q->tail; p = PKTLINK(p))
		prev = p;

	if (prev)
		PKTSETLINK(prev, NULL);
	else
		q->head = NULL;

	q->tail = prev;
	q->len--;

	pq->len--;

	return p;
}

void
pktq_pflush(osl_t *osh, struct pktq *pq, int prec, bool dir, ifpkt_cb_t fn, int arg)
{
	struct pktq_prec *q;
	void *p, *prev = NULL;

	q = &pq->q[prec];
	p = q->head;
	while (p) {
		if (fn == NULL || (*fn)(p, arg)) {
			bool head = (p == q->head);
			if (head)
				q->head = PKTLINK(p);
			else
				PKTSETLINK(prev, PKTLINK(p));
			PKTSETLINK(p, NULL);
			PKTFREE(osh, p, dir);
			q->len--;
			pq->len--;
			p = (head ? q->head : PKTLINK(prev));
		} else {
			prev = p;
			p = PKTLINK(p);
		}
	}

	if (q->head == NULL) {
		ASSERT(q->len == 0);
		q->tail = NULL;
	}
}

bool BCMFASTPATH
pktq_pdel(struct pktq *pq, void *pktbuf, int prec)
{
	struct pktq_prec *q;
	void *p;

	ASSERT(prec >= 0 && prec < pq->num_prec);

	/* Should this just assert pktbuf? */
	if (!pktbuf)
		return FALSE;

	q = &pq->q[prec];

	if (q->head == pktbuf) {
		if ((q->head = PKTLINK(pktbuf)) == NULL)
			q->tail = NULL;
	} else {
		for (p = q->head; p && PKTLINK(p) != pktbuf; p = PKTLINK(p))
			;
		if (p == NULL)
			return FALSE;

		PKTSETLINK(p, PKTLINK(pktbuf));
		if (q->tail == pktbuf)
			q->tail = p;
	}

	q->len--;
	pq->len--;
	PKTSETLINK(pktbuf, NULL);
	return TRUE;
}

void
pktq_init(struct pktq *pq, int num_prec, int max_len)
{
	int prec;

	ASSERT(num_prec > 0 && num_prec <= PKTQ_MAX_PREC);

	/* pq is variable size; only zero out what's requested */
	bzero(pq, OFFSETOF(struct pktq, q) + (sizeof(struct pktq_prec) * num_prec));

	pq->num_prec = (uint16)num_prec;

	pq->max = (uint16)max_len;

	for (prec = 0; prec < num_prec; prec++)
		pq->q[prec].max = pq->max;
}

void
pktq_set_max_plen(struct pktq *pq, int prec, int max_len)
{
	ASSERT(prec >= 0 && prec < pq->num_prec);

	if (prec < pq->num_prec)
		pq->q[prec].max = (uint16)max_len;
}

void * BCMFASTPATH
pktq_deq(struct pktq *pq, int *prec_out)
{
	struct pktq_prec *q;
	void *p;
	int prec;

	if (pq->len == 0)
		return NULL;

	while ((prec = pq->hi_prec) > 0 && pq->q[prec].head == NULL)
		pq->hi_prec--;

	q = &pq->q[prec];

	if ((p = q->head) == NULL)
		return NULL;

	if ((q->head = PKTLINK(p)) == NULL)
		q->tail = NULL;

	q->len--;

	pq->len--;

	if (prec_out)
		*prec_out = prec;

	PKTSETLINK(p, NULL);

	return p;
}

void * BCMFASTPATH
pktq_deq_tail(struct pktq *pq, int *prec_out)
{
	struct pktq_prec *q;
	void *p, *prev;
	int prec;

	if (pq->len == 0)
		return NULL;

	for (prec = 0; prec < pq->hi_prec; prec++)
		if (pq->q[prec].head)
			break;

	q = &pq->q[prec];

	if ((p = q->head) == NULL)
		return NULL;

	for (prev = NULL; p != q->tail; p = PKTLINK(p))
		prev = p;

	if (prev)
		PKTSETLINK(prev, NULL);
	else
		q->head = NULL;

	q->tail = prev;
	q->len--;

	pq->len--;

	if (prec_out)
		*prec_out = prec;

	PKTSETLINK(p, NULL);

	return p;
}

void *
pktq_peek(struct pktq *pq, int *prec_out)
{
	int prec;

	if (pq->len == 0)
		return NULL;

	while ((prec = pq->hi_prec) > 0 && pq->q[prec].head == NULL)
		pq->hi_prec--;

	if (prec_out)
		*prec_out = prec;

	return (pq->q[prec].head);
}

void *
pktq_peek_tail(struct pktq *pq, int *prec_out)
{
	int prec;

	if (pq->len == 0)
		return NULL;

	for (prec = 0; prec < pq->hi_prec; prec++)
		if (pq->q[prec].head)
			break;

	if (prec_out)
		*prec_out = prec;

	return (pq->q[prec].tail);
}

void
pktq_flush(osl_t *osh, struct pktq *pq, bool dir, ifpkt_cb_t fn, int arg)
{
	int prec;

	/* Optimize flush, if pktq len = 0, just return.
	 * pktq len of 0 means pktq's prec q's are all empty.
	 */
	if (pq->len == 0) {
		return;
	}

	for (prec = 0; prec < pq->num_prec; prec++)
		pktq_pflush(osh, pq, prec, dir, fn, arg);
	if (fn == NULL)
		ASSERT(pq->len == 0);
}

/* Return sum of lengths of a specific set of precedences */
int
pktq_mlen(struct pktq *pq, uint prec_bmp)
{
	int prec, len;

	len = 0;

	for (prec = 0; prec <= pq->hi_prec; prec++)
		if (prec_bmp & (1 << prec))
			len += pq->q[prec].len;

	return len;
}

/* Priority peek from a specific set of precedences */
void * BCMFASTPATH
pktq_mpeek(struct pktq *pq, uint prec_bmp, int *prec_out)
{
	struct pktq_prec *q;
	void *p;
	int prec;

	if (pq->len == 0)
	{
		return NULL;
	}
	while ((prec = pq->hi_prec) > 0 && pq->q[prec].head == NULL)
		pq->hi_prec--;

	while ((prec_bmp & (1 << prec)) == 0 || pq->q[prec].head == NULL)
		if (prec-- == 0)
			return NULL;

	q = &pq->q[prec];

	if ((p = q->head) == NULL)
		return NULL;

	if (prec_out)
		*prec_out = prec;

	return p;
}
/* Priority dequeue from a specific set of precedences */
void * BCMFASTPATH
pktq_mdeq(struct pktq *pq, uint prec_bmp, int *prec_out)
{
	struct pktq_prec *q;
	void *p;
	int prec;

	if (pq->len == 0)
		return NULL;

	while ((prec = pq->hi_prec) > 0 && pq->q[prec].head == NULL)
		pq->hi_prec--;

	while ((pq->q[prec].head == NULL) || ((prec_bmp & (1 << prec)) == 0))
		if (prec-- == 0)
			return NULL;

	q = &pq->q[prec];

	if ((p = q->head) == NULL)
		return NULL;

	if ((q->head = PKTLINK(p)) == NULL)
		q->tail = NULL;

	q->len--;

	if (prec_out)
		*prec_out = prec;

	pq->len--;

	PKTSETLINK(p, NULL);

	return p;
}
