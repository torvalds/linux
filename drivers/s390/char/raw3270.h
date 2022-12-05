/* SPDX-License-Identifier: GPL-2.0 */
/*
 * IBM/3270 Driver
 *
 * Author(s):
 *   Original 3270 Code for 2.4 written by Richard Hitt (UTS Global)
 *   Rewritten for 2.5 by Martin Schwidefsky <schwidefsky@de.ibm.com>
 *     Copyright IBM Corp. 2003, 2009
 */

#include <asm/idals.h>
#include <asm/ioctl.h>

/* ioctls for fullscreen 3270 */
#define TUBICMD		_IO('3', 3)	/* set ccw command for fs reads. */
#define TUBOCMD		_IO('3', 4)	/* set ccw command for fs writes. */
#define TUBGETI		_IO('3', 7)	/* get ccw command for fs reads. */
#define TUBGETO		_IO('3', 8)	/* get ccw command for fs writes. */
#define TUBSETMOD	_IO('3', 12)	/* FIXME: what does it do ?*/
#define TUBGETMOD	_IO('3', 13)	/* FIXME: what does it do ?*/

/* Local Channel Commands */
#define TC_WRITE	0x01		/* Write */
#define TC_RDBUF	0x02		/* Read Buffer */
#define TC_EWRITE	0x05		/* Erase write */
#define TC_READMOD	0x06		/* Read modified */
#define TC_EWRITEA	0x0d		/* Erase write alternate */
#define TC_WRITESF	0x11		/* Write structured field */

/* Buffer Control Orders */
#define TO_GE		0x08		/* Graphics Escape */
#define TO_SF		0x1d		/* Start field */
#define TO_SBA		0x11		/* Set buffer address */
#define TO_IC		0x13		/* Insert cursor */
#define TO_PT		0x05		/* Program tab */
#define TO_RA		0x3c		/* Repeat to address */
#define TO_SFE		0x29		/* Start field extended */
#define TO_EUA		0x12		/* Erase unprotected to address */
#define TO_MF		0x2c		/* Modify field */
#define TO_SA		0x28		/* Set attribute */

/* Field Attribute Bytes */
#define TF_INPUT	0x40		/* Visible input */
#define TF_INPUTN	0x4c		/* Invisible input */
#define TF_INMDT	0xc1		/* Visible, Set-MDT */
#define TF_LOG		0x60

/* Character Attribute Bytes */
#define TAT_RESET	0x00
#define TAT_FIELD	0xc0
#define TAT_EXTHI	0x41
#define TAT_FGCOLOR	0x42
#define TAT_CHARS	0x43
#define TAT_BGCOLOR	0x45
#define TAT_TRANS	0x46

/* Extended-Highlighting Bytes */
#define TAX_RESET	0x00
#define TAX_BLINK	0xf1
#define TAX_REVER	0xf2
#define TAX_UNDER	0xf4

/* Reset value */
#define TAR_RESET	0x00

/* Color values */
#define TAC_RESET	0x00
#define TAC_BLUE	0xf1
#define TAC_RED		0xf2
#define TAC_PINK	0xf3
#define TAC_GREEN	0xf4
#define TAC_TURQ	0xf5
#define TAC_YELLOW	0xf6
#define TAC_WHITE	0xf7
#define TAC_DEFAULT	0x00

/* Write Control Characters */
#define TW_NONE		0x40		/* No particular action */
#define TW_KR		0xc2		/* Keyboard restore */
#define TW_PLUSALARM	0x04		/* Add this bit for alarm */

#define RAW3270_FIRSTMINOR	1	/* First minor number */
#define RAW3270_MAXDEVS		255	/* Max number of 3270 devices */

#define AID_CLEAR		0x6d
#define AID_ENTER		0x7d
#define AID_PF3			0xf3
#define AID_PF7			0xf7
#define AID_PF8			0xf8
#define AID_READ_PARTITION	0x88

/* For TUBGETMOD and TUBSETMOD. Should include. */
struct raw3270_iocb {
	short model;
	short line_cnt;
	short col_cnt;
	short pf_cnt;
	short re_cnt;
	short map;
};

struct raw3270;
struct raw3270_view;
extern struct class *class3270;

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
void raw3270_request_reset(struct raw3270_request *rq);
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
	spinlock_t lock;
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
