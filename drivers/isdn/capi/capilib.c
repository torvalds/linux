
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/isdn/capilli.h>

#define DBG(format, arg...) do { \
printk(KERN_DEBUG "%s: " format "\n" , __func__ , ## arg); \
} while (0)

struct capilib_msgidqueue {
	struct capilib_msgidqueue *next;
	u16 msgid;
};

struct capilib_ncci {
	struct list_head list;
	u16 applid;
	u32 ncci;
	u32 winsize;
	int   nmsg;
	struct capilib_msgidqueue *msgidqueue;
	struct capilib_msgidqueue *msgidlast;
	struct capilib_msgidqueue *msgidfree;
	struct capilib_msgidqueue msgidpool[CAPI_MAXDATAWINDOW];
};

// ---------------------------------------------------------------------------
// NCCI Handling

static inline void mq_init(struct capilib_ncci * np)
{
	u_int i;
	np->msgidqueue = NULL;
	np->msgidlast = NULL;
	np->nmsg = 0;
	memset(np->msgidpool, 0, sizeof(np->msgidpool));
	np->msgidfree = &np->msgidpool[0];
	for (i = 1; i < np->winsize; i++) {
		np->msgidpool[i].next = np->msgidfree;
		np->msgidfree = &np->msgidpool[i];
	}
}

static inline int mq_enqueue(struct capilib_ncci * np, u16 msgid)
{
	struct capilib_msgidqueue *mq;
	if ((mq = np->msgidfree) == NULL)
		return 0;
	np->msgidfree = mq->next;
	mq->msgid = msgid;
	mq->next = NULL;
	if (np->msgidlast)
		np->msgidlast->next = mq;
	np->msgidlast = mq;
	if (!np->msgidqueue)
		np->msgidqueue = mq;
	np->nmsg++;
	return 1;
}

static inline int mq_dequeue(struct capilib_ncci * np, u16 msgid)
{
	struct capilib_msgidqueue **pp;
	for (pp = &np->msgidqueue; *pp; pp = &(*pp)->next) {
		if ((*pp)->msgid == msgid) {
			struct capilib_msgidqueue *mq = *pp;
			*pp = mq->next;
			if (mq == np->msgidlast)
				np->msgidlast = NULL;
			mq->next = np->msgidfree;
			np->msgidfree = mq;
			np->nmsg--;
			return 1;
		}
	}
	return 0;
}

void capilib_new_ncci(struct list_head *head, u16 applid, u32 ncci, u32 winsize)
{
	struct capilib_ncci *np;

	np = kmalloc(sizeof(*np), GFP_ATOMIC);
	if (!np) {
		printk(KERN_WARNING "capilib_new_ncci: no memory.\n");
		return;
	}
	if (winsize > CAPI_MAXDATAWINDOW) {
		printk(KERN_ERR "capi_new_ncci: winsize %d too big\n",
		       winsize);
		winsize = CAPI_MAXDATAWINDOW;
	}
	np->applid = applid;
	np->ncci = ncci;
	np->winsize = winsize;
	mq_init(np);
	list_add_tail(&np->list, head);
	DBG("kcapi: appl %d ncci 0x%x up", applid, ncci);
}

EXPORT_SYMBOL(capilib_new_ncci);

void capilib_free_ncci(struct list_head *head, u16 applid, u32 ncci)
{
	struct list_head *l;
	struct capilib_ncci *np;

	list_for_each(l, head) {
		np = list_entry(l, struct capilib_ncci, list);
		if (np->applid != applid)
			continue;
		if (np->ncci != ncci)
			continue;
		printk(KERN_INFO "kcapi: appl %d ncci 0x%x down\n", applid, ncci);
		list_del(&np->list);
		kfree(np);
		return;
	}
	printk(KERN_ERR "capilib_free_ncci: ncci 0x%x not found\n", ncci);
}

EXPORT_SYMBOL(capilib_free_ncci);

void capilib_release_appl(struct list_head *head, u16 applid)
{
	struct list_head *l, *n;
	struct capilib_ncci *np;

	list_for_each_safe(l, n, head) {
		np = list_entry(l, struct capilib_ncci, list);
		if (np->applid != applid)
			continue;
		printk(KERN_INFO "kcapi: appl %d ncci 0x%x forced down\n", applid, np->ncci);
		list_del(&np->list);
		kfree(np);
	}
}

EXPORT_SYMBOL(capilib_release_appl);

void capilib_release(struct list_head *head)
{
	struct list_head *l, *n;
	struct capilib_ncci *np;

	list_for_each_safe(l, n, head) {
		np = list_entry(l, struct capilib_ncci, list);
		printk(KERN_INFO "kcapi: appl %d ncci 0x%x forced down\n", np->applid, np->ncci);
		list_del(&np->list);
		kfree(np);
	}
}

EXPORT_SYMBOL(capilib_release);

u16 capilib_data_b3_req(struct list_head *head, u16 applid, u32 ncci, u16 msgid)
{
	struct list_head *l;
	struct capilib_ncci *np;

	list_for_each(l, head) {
		np = list_entry(l, struct capilib_ncci, list);
		if (np->applid != applid)
			continue;
		if (np->ncci != ncci)
			continue;
		
		if (mq_enqueue(np, msgid) == 0)
			return CAPI_SENDQUEUEFULL;

		return CAPI_NOERROR;
	}
	printk(KERN_ERR "capilib_data_b3_req: ncci 0x%x not found\n", ncci);
	return CAPI_NOERROR;
}

EXPORT_SYMBOL(capilib_data_b3_req);

void capilib_data_b3_conf(struct list_head *head, u16 applid, u32 ncci, u16 msgid)
{
	struct list_head *l;
	struct capilib_ncci *np;

	list_for_each(l, head) {
		np = list_entry(l, struct capilib_ncci, list);
		if (np->applid != applid)
			continue;
		if (np->ncci != ncci)
			continue;
		
		if (mq_dequeue(np, msgid) == 0) {
			printk(KERN_ERR "kcapi: msgid %hu ncci 0x%x not on queue\n",
			       msgid, ncci);
		}
		return;
	}
	printk(KERN_ERR "capilib_data_b3_conf: ncci 0x%x not found\n", ncci);
}

EXPORT_SYMBOL(capilib_data_b3_conf);
