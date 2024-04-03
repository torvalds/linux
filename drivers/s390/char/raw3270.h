/* SPDX-License-Identifier: GPL-2.0 */
/*
 * IBM/3270 Driver
 *
 * Author(s):
 *   Original 3270 Code for 2.4 written by Richard Hitt (UTS Global)
 *   Rewritten for 2.5 by Martin Schwidefsky <schwidefsky@de.ibm.com>
 *     Copyright IBM Corp. 2003, 2009
 */

#include <uapi/asm/raw3270.h>
#include <asm/idals.h>
#include <asm/ioctl.h>

struct raw3270;
struct raw3270_view;
extern const struct class class3270;

/* 3270 CCW request */
struct raw3270_request {
	struct list_head list;		/* list head for request queueing. */
	struct raw3270_view *view;	/* view of this request */
	struct ccw1 ccw;		/* single ccw. */
	void *buffer;			/* output buffer. */
	size_t size;			/* size of output buffer. */
	int rescnt;			/* residual count from devstat. */
	int rc;				/* return code for this request. */

	/* Callback for delivering final status. */
	void (*callback)(struct raw3270_request *rq, void *data);
	void *callback_data;
};

struct raw3270_request *raw3270_request_alloc(size_t size);
void raw3270_request_free(struct raw3270_request *rq);
int raw3270_request_reset(struct raw3270_request *rq);
void raw3270_request_set_cmd(struct raw3270_request *rq, u8 cmd);
int  raw3270_request_add_data(struct raw3270_request *rq, void *data, size_t size);
void raw3270_request_set_data(struct raw3270_request *rq, void *data, size_t size);
void raw3270_request_set_idal(struct raw3270_request *rq, struct idal_buffer *ib);

static inline int
raw3270_request_final(struct raw3270_request *rq)
{
	return list_empty(&rq->list);
}

void raw3270_buffer_address(struct raw3270 *, char *, int, int);

/*
 * Functions of a 3270 view.
 */
struct raw3270_fn {
	int  (*activate)(struct raw3270_view *rq);
	void (*deactivate)(struct raw3270_view *rq);
	void (*intv)(struct raw3270_view *view,
		     struct raw3270_request *rq, struct irb *ib);
	void (*release)(struct raw3270_view *view);
	void (*free)(struct raw3270_view *view);
	void (*resize)(struct raw3270_view *view,
		       int new_model, int new_cols, int new_rows,
		       int old_model, int old_cols, int old_rows);
};

/*
 * View structure chaining. The raw3270_view structure is meant to
 * be embedded at the start of the real view data structure, e.g.:
 *   struct example {
 *     struct raw3270_view view;
 *     ...
 *   };
 */
struct raw3270_view {
	struct list_head list;
	spinlock_t lock; /* protects members of view */
#define RAW3270_VIEW_LOCK_IRQ	0
#define RAW3270_VIEW_LOCK_BH	1
	atomic_t ref_count;
	struct raw3270 *dev;
	struct raw3270_fn *fn;
	unsigned int model;
	unsigned int rows, cols;	/* # of rows & colums of the view */
	unsigned char *ascebc;		/* ascii -> ebcdic table */
};

int raw3270_add_view(struct raw3270_view *view, struct raw3270_fn *fn, int minor, int subclass);
int raw3270_view_lock_unavailable(struct raw3270_view *view);
int raw3270_activate_view(struct raw3270_view *view);
void raw3270_del_view(struct raw3270_view *view);
void raw3270_deactivate_view(struct raw3270_view *view);
struct raw3270_view *raw3270_find_view(struct raw3270_fn *fn, int minor);
int raw3270_start(struct raw3270_view *view, struct raw3270_request *rq);
int raw3270_start_locked(struct raw3270_view *view, struct raw3270_request *rq);
int raw3270_start_irq(struct raw3270_view *view, struct raw3270_request *rq);
int raw3270_reset(struct raw3270_view *view);
struct raw3270_view *raw3270_view(struct raw3270_view *view);
int raw3270_view_active(struct raw3270_view *view);
int raw3270_start_request(struct raw3270_view *view, struct raw3270_request *rq,
			  int cmd, void *data, size_t len);
void raw3270_read_modified_cb(struct raw3270_request *rq, void *data);

/* Reference count inliner for view structures. */
static inline void
raw3270_get_view(struct raw3270_view *view)
{
	atomic_inc(&view->ref_count);
}

extern wait_queue_head_t raw3270_wait_queue;

static inline void
raw3270_put_view(struct raw3270_view *view)
{
	if (atomic_dec_return(&view->ref_count) == 0)
		wake_up(&raw3270_wait_queue);
}

struct raw3270 *raw3270_setup_console(void);
void raw3270_wait_cons_dev(struct raw3270 *rp);

/* Notifier for device addition/removal */
struct raw3270_notifier {
	struct list_head list;
	void (*create)(int minor);
	void (*destroy)(int minor);
};

int raw3270_register_notifier(struct raw3270_notifier *notifier);
void raw3270_unregister_notifier(struct raw3270_notifier *notifier);
