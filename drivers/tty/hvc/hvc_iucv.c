/*
 * hvc_iucv.c - z/VM IUCV hypervisor console (HVC) device driver
 *
 * This HVC device driver provides terminal access using
 * z/VM IUCV communication paths.
 *
 * Copyright IBM Corp. 2008, 2009
 *
 * Author(s):	Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */
#define KMSG_COMPONENT		"hvc_iucv"
#define pr_fmt(fmt)		KMSG_COMPONENT ": " fmt

#include <linux/types.h>
#include <linux/slab.h>
#include <asm/ebcdic.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/mempool.h>
#include <linux/moduleparam.h>
#include <linux/tty.h>
#include <linux/wait.h>
#include <net/iucv/iucv.h>

#include "hvc_console.h"


/* General device driver settings */
#define HVC_IUCV_MAGIC		0xc9e4c3e5
#define MAX_HVC_IUCV_LINES	HVC_ALLOC_TTY_ADAPTERS
#define MEMPOOL_MIN_NR		(PAGE_SIZE / sizeof(struct iucv_tty_buffer)/4)

/* IUCV TTY message  */
#define MSG_VERSION		0x02	/* Message version */
#define MSG_TYPE_ERROR		0x01	/* Error message */
#define MSG_TYPE_TERMENV	0x02	/* Terminal environment variable */
#define MSG_TYPE_TERMIOS	0x04	/* Terminal IO struct update */
#define MSG_TYPE_WINSIZE	0x08	/* Terminal window size update */
#define MSG_TYPE_DATA		0x10	/* Terminal data */

struct iucv_tty_msg {
	u8	version;		/* Message version */
	u8	type;			/* Message type */
#define MSG_MAX_DATALEN		((u16)(~0))
	u16	datalen;		/* Payload length */
	u8	data[];			/* Payload buffer */
} __attribute__((packed));
#define MSG_SIZE(s)		((s) + offsetof(struct iucv_tty_msg, data))

enum iucv_state_t {
	IUCV_DISCONN	= 0,
	IUCV_CONNECTED	= 1,
	IUCV_SEVERED	= 2,
};

enum tty_state_t {
	TTY_CLOSED	= 0,
	TTY_OPENED	= 1,
};

struct hvc_iucv_private {
	struct hvc_struct	*hvc;		/* HVC struct reference */
	u8			srv_name[8];	/* IUCV service name (ebcdic) */
	unsigned char		is_console;	/* Linux console usage flag */
	enum iucv_state_t	iucv_state;	/* IUCV connection status */
	enum tty_state_t	tty_state;	/* TTY status */
	struct iucv_path	*path;		/* IUCV path pointer */
	spinlock_t		lock;		/* hvc_iucv_private lock */
#define SNDBUF_SIZE		(PAGE_SIZE)	/* must be < MSG_MAX_DATALEN */
	void			*sndbuf;	/* send buffer		  */
	size_t			sndbuf_len;	/* length of send buffer  */
#define QUEUE_SNDBUF_DELAY	(HZ / 25)
	struct delayed_work	sndbuf_work;	/* work: send iucv msg(s) */
	wait_queue_head_t	sndbuf_waitq;	/* wait for send completion */
	struct list_head	tty_outqueue;	/* outgoing IUCV messages */
	struct list_head	tty_inqueue;	/* incoming IUCV messages */
	struct device		*dev;		/* device structure */
};

struct iucv_tty_buffer {
	struct list_head	list;	/* list pointer */
	struct iucv_message	msg;	/* store an IUCV message */
	size_t			offset;	/* data buffer offset */
	struct iucv_tty_msg	*mbuf;	/* buffer to store input/output data */
};

/* IUCV callback handler */
static	int hvc_iucv_path_pending(struct iucv_path *, u8[8], u8[16]);
static void hvc_iucv_path_severed(struct iucv_path *, u8[16]);
static void hvc_iucv_msg_pending(struct iucv_path *, struct iucv_message *);
static void hvc_iucv_msg_complete(struct iucv_path *, struct iucv_message *);


/* Kernel module parameter: use one terminal device as default */
static unsigned long hvc_iucv_devices = 1;

/* Array of allocated hvc iucv tty lines... */
static struct hvc_iucv_private *hvc_iucv_table[MAX_HVC_IUCV_LINES];
#define IUCV_HVC_CON_IDX	(0)
/* List of z/VM user ID filter entries (struct iucv_vmid_filter) */
#define MAX_VMID_FILTER		(500)
static size_t hvc_iucv_filter_size;
static void *hvc_iucv_filter;
static const char *hvc_iucv_filter_string;
static DEFINE_RWLOCK(hvc_iucv_filter_lock);

/* Kmem cache and mempool for iucv_tty_buffer elements */
static struct kmem_cache *hvc_iucv_buffer_cache;
static mempool_t *hvc_iucv_mempool;

/* IUCV handler callback functions */
static struct iucv_handler hvc_iucv_handler = {
	.path_pending  = hvc_iucv_path_pending,
	.path_severed  = hvc_iucv_path_severed,
	.message_complete = hvc_iucv_msg_complete,
	.message_pending  = hvc_iucv_msg_pending,
};


/**
 * hvc_iucv_get_private() - Return a struct hvc_iucv_private instance.
 * @num:	The HVC virtual terminal number (vtermno)
 *
 * This function returns the struct hvc_iucv_private instance that corresponds
 * to the HVC virtual terminal number specified as parameter @num.
 */
struct hvc_iucv_private *hvc_iucv_get_private(uint32_t num)
{
	if ((num < HVC_IUCV_MAGIC) || (num - HVC_IUCV_MAGIC > hvc_iucv_devices))
		return NULL;
	return hvc_iucv_table[num - HVC_IUCV_MAGIC];
}

/**
 * alloc_tty_buffer() - Return a new struct iucv_tty_buffer element.
 * @size:	Size of the internal buffer used to store data.
 * @flags:	Memory allocation flags passed to mempool.
 *
 * This function allocates a new struct iucv_tty_buffer element and, optionally,
 * allocates an internal data buffer with the specified size @size.
 * The internal data buffer is always allocated with GFP_DMA which is
 * required for receiving and sending data with IUCV.
 * Note: The total message size arises from the internal buffer size and the
 *	 members of the iucv_tty_msg structure.
 * The function returns NULL if memory allocation has failed.
 */
static struct iucv_tty_buffer *alloc_tty_buffer(size_t size, gfp_t flags)
{
	struct iucv_tty_buffer *bufp;

	bufp = mempool_alloc(hvc_iucv_mempool, flags);
	if (!bufp)
		return NULL;
	memset(bufp, 0, sizeof(*bufp));

	if (size > 0) {
		bufp->msg.length = MSG_SIZE(size);
		bufp->mbuf = kmalloc(bufp->msg.length, flags | GFP_DMA);
		if (!bufp->mbuf) {
			mempool_free(bufp, hvc_iucv_mempool);
			return NULL;
		}
		bufp->mbuf->version = MSG_VERSION;
		bufp->mbuf->type    = MSG_TYPE_DATA;
		bufp->mbuf->datalen = (u16) size;
	}
	return bufp;
}

/**
 * destroy_tty_buffer() - destroy struct iucv_tty_buffer element.
 * @bufp:	Pointer to a struct iucv_tty_buffer element, SHALL NOT be NULL.
 */
static void destroy_tty_buffer(struct iucv_tty_buffer *bufp)
{
	kfree(bufp->mbuf);
	mempool_free(bufp, hvc_iucv_mempool);
}

/**
 * destroy_tty_buffer_list() - call destroy_tty_buffer() for each list element.
 * @list:	List containing struct iucv_tty_buffer elements.
 */
static void destroy_tty_buffer_list(struct list_head *list)
{
	struct iucv_tty_buffer *ent, *next;

	list_for_each_entry_safe(ent, next, list, list) {
		list_del(&ent->list);
		destroy_tty_buffer(ent);
	}
}

/**
 * hvc_iucv_write() - Receive IUCV message & write data to HVC buffer.
 * @priv:		Pointer to struct hvc_iucv_private
 * @buf:		HVC buffer for writing received terminal data.
 * @count:		HVC buffer size.
 * @has_more_data:	Pointer to an int variable.
 *
 * The function picks up pending messages from the input queue and receives
 * the message data that is then written to the specified buffer @buf.
 * If the buffer size @count is less than the data message size, the
 * message is kept on the input queue and @has_more_data is set to 1.
 * If all message data has been written, the message is removed from
 * the input queue.
 *
 * The function returns the number of bytes written to the terminal, zero if
 * there are no pending data messages available or if there is no established
 * IUCV path.
 * If the IUCV path has been severed, then -EPIPE is returned to cause a
 * hang up (that is issued by the HVC layer).
 */
static int hvc_iucv_write(struct hvc_iucv_private *priv,
			  char *buf, int count, int *has_more_data)
{
	struct iucv_tty_buffer *rb;
	int written;
	int rc;

	/* immediately return if there is no IUCV connection */
	if (priv->iucv_state == IUCV_DISCONN)
		return 0;

	/* if the IUCV path has been severed, return -EPIPE to inform the
	 * HVC layer to hang up the tty device. */
	if (priv->iucv_state == IUCV_SEVERED)
		return -EPIPE;

	/* check if there are pending messages */
	if (list_empty(&priv->tty_inqueue))
		return 0;

	/* receive an iucv message and flip data to the tty (ldisc) */
	rb = list_first_entry(&priv->tty_inqueue, struct iucv_tty_buffer, list);

	written = 0;
	if (!rb->mbuf) { /* message not yet received ... */
		/* allocate mem to store msg data; if no memory is available
		 * then leave the buffer on the list and re-try later */
		rb->mbuf = kmalloc(rb->msg.length, GFP_ATOMIC | GFP_DMA);
		if (!rb->mbuf)
			return -ENOMEM;

		rc = __iucv_message_receive(priv->path, &rb->msg, 0,
					    rb->mbuf, rb->msg.length, NULL);
		switch (rc) {
		case 0: /* Successful	    */
			break;
		case 2:	/* No message found */
		case 9: /* Message purged   */
			break;
		default:
			written = -EIO;
		}
		/* remove buffer if an error has occurred or received data
		 * is not correct */
		if (rc || (rb->mbuf->version != MSG_VERSION) ||
			  (rb->msg.length    != MSG_SIZE(rb->mbuf->datalen)))
			goto out_remove_buffer;
	}

	switch (rb->mbuf->type) {
	case MSG_TYPE_DATA:
		written = min_t(int, rb->mbuf->datalen - rb->offset, count);
		memcpy(buf, rb->mbuf->data + rb->offset, written);
		if (written < (rb->mbuf->datalen - rb->offset)) {
			rb->offset += written;
			*has_more_data = 1;
			goto out_written;
		}
		break;

	case MSG_TYPE_WINSIZE:
		if (rb->mbuf->datalen != sizeof(struct winsize))
			break;
		/* The caller must ensure that the hvc is locked, which
		 * is the case when called from hvc_iucv_get_chars() */
		__hvc_resize(priv->hvc, *((struct winsize *) rb->mbuf->data));
		break;

	case MSG_TYPE_ERROR:	/* ignored ... */
	case MSG_TYPE_TERMENV:	/* ignored ... */
	case MSG_TYPE_TERMIOS:	/* ignored ... */
		break;
	}

out_remove_buffer:
	list_del(&rb->list);
	destroy_tty_buffer(rb);
	*has_more_data = !list_empty(&priv->tty_inqueue);

out_written:
	return written;
}

/**
 * hvc_iucv_get_chars() - HVC get_chars operation.
 * @vtermno:	HVC virtual terminal number.
 * @buf:	Pointer to a buffer to store data
 * @count:	Size of buffer available for writing
 *
 * The HVC thread calls this method to read characters from the back-end.
 * If an IUCV communication path has been established, pending IUCV messages
 * are received and data is copied into buffer @buf up to @count bytes.
 *
 * Locking:	The routine gets called under an irqsave() spinlock; and
 *		the routine locks the struct hvc_iucv_private->lock to call
 *		helper functions.
 */
static int hvc_iucv_get_chars(uint32_t vtermno, char *buf, int count)
{
	struct hvc_iucv_private *priv = hvc_iucv_get_private(vtermno);
	int written;
	int has_more_data;

	if (count <= 0)
		return 0;

	if (!priv)
		return -ENODEV;

	spin_lock(&priv->lock);
	has_more_data = 0;
	written = hvc_iucv_write(priv, buf, count, &has_more_data);
	spin_unlock(&priv->lock);

	/* if there are still messages on the queue... schedule another run */
	if (has_more_data)
		hvc_kick();

	return written;
}

/**
 * hvc_iucv_queue() - Buffer terminal data for sending.
 * @priv:	Pointer to struct hvc_iucv_private instance.
 * @buf:	Buffer containing data to send.
 * @count:	Size of buffer and amount of data to send.
 *
 * The function queues data for sending. To actually send the buffered data,
 * a work queue function is scheduled (with QUEUE_SNDBUF_DELAY).
 * The function returns the number of data bytes that has been buffered.
 *
 * If the device is not connected, data is ignored and the function returns
 * @count.
 * If the buffer is full, the function returns 0.
 * If an existing IUCV communicaton path has been severed, -EPIPE is returned
 * (that can be passed to HVC layer to cause a tty hangup).
 */
static int hvc_iucv_queue(struct hvc_iucv_private *priv, const char *buf,
			  int count)
{
	size_t len;

	if (priv->iucv_state == IUCV_DISCONN)
		return count;			/* ignore data */

	if (priv->iucv_state == IUCV_SEVERED)
		return -EPIPE;

	len = min_t(size_t, count, SNDBUF_SIZE - priv->sndbuf_len);
	if (!len)
		return 0;

	memcpy(priv->sndbuf + priv->sndbuf_len, buf, len);
	priv->sndbuf_len += len;

	if (priv->iucv_state == IUCV_CONNECTED)
		schedule_delayed_work(&priv->sndbuf_work, QUEUE_SNDBUF_DELAY);

	return len;
}

/**
 * hvc_iucv_send() - Send an IUCV message containing terminal data.
 * @priv:	Pointer to struct hvc_iucv_private instance.
 *
 * If an IUCV communication path has been established, the buffered output data
 * is sent via an IUCV message and the number of bytes sent is returned.
 * Returns 0 if there is no established IUCV communication path or
 * -EPIPE if an existing IUCV communicaton path has been severed.
 */
static int hvc_iucv_send(struct hvc_iucv_private *priv)
{
	struct iucv_tty_buffer *sb;
	int rc, len;

	if (priv->iucv_state == IUCV_SEVERED)
		return -EPIPE;

	if (priv->iucv_state == IUCV_DISCONN)
		return -EIO;

	if (!priv->sndbuf_len)
		return 0;

	/* allocate internal buffer to store msg data and also compute total
	 * message length */
	sb = alloc_tty_buffer(priv->sndbuf_len, GFP_ATOMIC);
	if (!sb)
		return -ENOMEM;

	memcpy(sb->mbuf->data, priv->sndbuf, priv->sndbuf_len);
	sb->mbuf->datalen = (u16) priv->sndbuf_len;
	sb->msg.length = MSG_SIZE(sb->mbuf->datalen);

	list_add_tail(&sb->list, &priv->tty_outqueue);

	rc = __iucv_message_send(priv->path, &sb->msg, 0, 0,
				 (void *) sb->mbuf, sb->msg.length);
	if (rc) {
		/* drop the message here; however we might want to handle
		 * 0x03 (msg limit reached) by trying again... */
		list_del(&sb->list);
		destroy_tty_buffer(sb);
	}
	len = priv->sndbuf_len;
	priv->sndbuf_len = 0;

	return len;
}

/**
 * hvc_iucv_sndbuf_work() - Send buffered data over IUCV
 * @work:	Work structure.
 *
 * This work queue function sends buffered output data over IUCV and,
 * if not all buffered data could be sent, reschedules itself.
 */
static void hvc_iucv_sndbuf_work(struct work_struct *work)
{
	struct hvc_iucv_private *priv;

	priv = container_of(work, struct hvc_iucv_private, sndbuf_work.work);
	if (!priv)
		return;

	spin_lock_bh(&priv->lock);
	hvc_iucv_send(priv);
	spin_unlock_bh(&priv->lock);
}

/**
 * hvc_iucv_put_chars() - HVC put_chars operation.
 * @vtermno:	HVC virtual terminal number.
 * @buf:	Pointer to an buffer to read data from
 * @count:	Size of buffer available for reading
 *
 * The HVC thread calls this method to write characters to the back-end.
 * The function calls hvc_iucv_queue() to queue terminal data for sending.
 *
 * Locking:	The method gets called under an irqsave() spinlock; and
 *		locks struct hvc_iucv_private->lock.
 */
static int hvc_iucv_put_chars(uint32_t vtermno, const char *buf, int count)
{
	struct hvc_iucv_private *priv = hvc_iucv_get_private(vtermno);
	int queued;

	if (count <= 0)
		return 0;

	if (!priv)
		return -ENODEV;

	spin_lock(&priv->lock);
	queued = hvc_iucv_queue(priv, buf, count);
	spin_unlock(&priv->lock);

	return queued;
}

/**
 * hvc_iucv_notifier_add() - HVC notifier for opening a TTY for the first time.
 * @hp:	Pointer to the HVC device (struct hvc_struct)
 * @id:	Additional data (originally passed to hvc_alloc): the index of an struct
 *	hvc_iucv_private instance.
 *
 * The function sets the tty state to TTY_OPENED for the struct hvc_iucv_private
 * instance that is derived from @id. Always returns 0.
 *
 * Locking:	struct hvc_iucv_private->lock, spin_lock_bh
 */
static int hvc_iucv_notifier_add(struct hvc_struct *hp, int id)
{
	struct hvc_iucv_private *priv;

	priv = hvc_iucv_get_private(id);
	if (!priv)
		return 0;

	spin_lock_bh(&priv->lock);
	priv->tty_state = TTY_OPENED;
	spin_unlock_bh(&priv->lock);

	return 0;
}

/**
 * hvc_iucv_cleanup() - Clean up and reset a z/VM IUCV HVC instance.
 * @priv:	Pointer to the struct hvc_iucv_private instance.
 */
static void hvc_iucv_cleanup(struct hvc_iucv_private *priv)
{
	destroy_tty_buffer_list(&priv->tty_outqueue);
	destroy_tty_buffer_list(&priv->tty_inqueue);

	priv->tty_state = TTY_CLOSED;
	priv->iucv_state = IUCV_DISCONN;

	priv->sndbuf_len = 0;
}

/**
 * tty_outqueue_empty() - Test if the tty outq is empty
 * @priv:	Pointer to struct hvc_iucv_private instance.
 */
static inline int tty_outqueue_empty(struct hvc_iucv_private *priv)
{
	int rc;

	spin_lock_bh(&priv->lock);
	rc = list_empty(&priv->tty_outqueue);
	spin_unlock_bh(&priv->lock);

	return rc;
}

/**
 * flush_sndbuf_sync() - Flush send buffer and wait for completion
 * @priv:	Pointer to struct hvc_iucv_private instance.
 *
 * The routine cancels a pending sndbuf work, calls hvc_iucv_send()
 * to flush any buffered terminal output data and waits for completion.
 */
static void flush_sndbuf_sync(struct hvc_iucv_private *priv)
{
	int sync_wait;

	cancel_delayed_work_sync(&priv->sndbuf_work);

	spin_lock_bh(&priv->lock);
	hvc_iucv_send(priv);		/* force sending buffered data */
	sync_wait = !list_empty(&priv->tty_outqueue); /* anything queued ? */
	spin_unlock_bh(&priv->lock);

	if (sync_wait)
		wait_event_timeout(priv->sndbuf_waitq,
				   tty_outqueue_empty(priv), HZ/10);
}

/**
 * hvc_iucv_hangup() - Sever IUCV path and schedule hvc tty hang up
 * @priv:	Pointer to hvc_iucv_private structure
 *
 * This routine severs an existing IUCV communication path and hangs
 * up the underlying HVC terminal device.
 * The hang-up occurs only if an IUCV communication path is established;
 * otherwise there is no need to hang up the terminal device.
 *
 * The IUCV HVC hang-up is separated into two steps:
 * 1. After the IUCV path has been severed, the iucv_state is set to
 *    IUCV_SEVERED.
 * 2. Later, when the HVC thread calls hvc_iucv_get_chars(), the
 *    IUCV_SEVERED state causes the tty hang-up in the HVC layer.
 *
 * If the tty has not yet been opened, clean up the hvc_iucv_private
 * structure to allow re-connects.
 * If the tty has been opened, let get_chars() return -EPIPE to signal
 * the HVC layer to hang up the tty and, if so, wake up the HVC thread
 * to call get_chars()...
 *
 * Special notes on hanging up a HVC terminal instantiated as console:
 * Hang-up:	1. do_tty_hangup() replaces file ops (= hung_up_tty_fops)
 *		2. do_tty_hangup() calls tty->ops->close() for console_filp
 *			=> no hangup notifier is called by HVC (default)
 *		2. hvc_close() returns because of tty_hung_up_p(filp)
 *			=> no delete notifier is called!
 * Finally, the back-end is not being notified, thus, the tty session is
 * kept active (TTY_OPEN) to be ready for re-connects.
 *
 * Locking:	spin_lock(&priv->lock) w/o disabling bh
 */
static void hvc_iucv_hangup(struct hvc_iucv_private *priv)
{
	struct iucv_path *path;

	path = NULL;
	spin_lock(&priv->lock);
	if (priv->iucv_state == IUCV_CONNECTED) {
		path = priv->path;
		priv->path = NULL;
		priv->iucv_state = IUCV_SEVERED;
		if (priv->tty_state == TTY_CLOSED)
			hvc_iucv_cleanup(priv);
		else
			/* console is special (see above) */
			if (priv->is_console) {
				hvc_iucv_cleanup(priv);
				priv->tty_state = TTY_OPENED;
			} else
				hvc_kick();
	}
	spin_unlock(&priv->lock);

	/* finally sever path (outside of priv->lock due to lock ordering) */
	if (path) {
		iucv_path_sever(path, NULL);
		iucv_path_free(path);
	}
}

/**
 * hvc_iucv_notifier_hangup() - HVC notifier for TTY hangups.
 * @hp:		Pointer to the HVC device (struct hvc_struct)
 * @id:		Additional data (originally passed to hvc_alloc):
 *		the index of an struct hvc_iucv_private instance.
 *
 * This routine notifies the HVC back-end that a tty hangup (carrier loss,
 * virtual or otherwise) has occurred.
 * The z/VM IUCV HVC device driver ignores virtual hangups (vhangup())
 * to keep an existing IUCV communication path established.
 * (Background: vhangup() is called from user space (by getty or login) to
 *		disable writing to the tty by other applications).
 * If the tty has been opened and an established IUCV path has been severed
 * (we caused the tty hangup), the function calls hvc_iucv_cleanup().
 *
 * Locking:	struct hvc_iucv_private->lock
 */
static void hvc_iucv_notifier_hangup(struct hvc_struct *hp, int id)
{
	struct hvc_iucv_private *priv;

	priv = hvc_iucv_get_private(id);
	if (!priv)
		return;

	flush_sndbuf_sync(priv);

	spin_lock_bh(&priv->lock);
	/* NOTE: If the hangup was scheduled by ourself (from the iucv
	 *	 path_servered callback [IUCV_SEVERED]), we have to clean up
	 *	 our structure and to set state to TTY_CLOSED.
	 *	 If the tty was hung up otherwise (e.g. vhangup()), then we
	 *	 ignore this hangup and keep an established IUCV path open...
	 *	 (...the reason is that we are not able to connect back to the
	 *	 client if we disconnect on hang up) */
	priv->tty_state = TTY_CLOSED;

	if (priv->iucv_state == IUCV_SEVERED)
		hvc_iucv_cleanup(priv);
	spin_unlock_bh(&priv->lock);
}

/**
 * hvc_iucv_dtr_rts() - HVC notifier for handling DTR/RTS
 * @hp:		Pointer the HVC device (struct hvc_struct)
 * @raise:	Non-zero to raise or zero to lower DTR/RTS lines
 *
 * This routine notifies the HVC back-end to raise or lower DTR/RTS
 * lines.  Raising DTR/RTS is ignored.  Lowering DTR/RTS indicates to
 * drop the IUCV connection (similar to hang up the modem).
 */
static void hvc_iucv_dtr_rts(struct hvc_struct *hp, int raise)
{
	struct hvc_iucv_private *priv;
	struct iucv_path        *path;

	/* Raising the DTR/RTS is ignored as IUCV connections can be
	 * established at any times.
	 */
	if (raise)
		return;

	priv = hvc_iucv_get_private(hp->vtermno);
	if (!priv)
		return;

	/* Lowering the DTR/RTS lines disconnects an established IUCV
	 * connection.
	 */
	flush_sndbuf_sync(priv);

	spin_lock_bh(&priv->lock);
	path = priv->path;		/* save reference to IUCV path */
	priv->path = NULL;
	priv->iucv_state = IUCV_DISCONN;
	spin_unlock_bh(&priv->lock);

	/* Sever IUCV path outside of priv->lock due to lock ordering of:
	 * priv->lock <--> iucv_table_lock */
	if (path) {
		iucv_path_sever(path, NULL);
		iucv_path_free(path);
	}
}

/**
 * hvc_iucv_notifier_del() - HVC notifier for closing a TTY for the last time.
 * @hp:		Pointer to the HVC device (struct hvc_struct)
 * @id:		Additional data (originally passed to hvc_alloc):
 *		the index of an struct hvc_iucv_private instance.
 *
 * This routine notifies the HVC back-end that the last tty device fd has been
 * closed.  The function cleans up tty resources.  The clean-up of the IUCV
 * connection is done in hvc_iucv_dtr_rts() and depends on the HUPCL termios
 * control setting.
 *
 * Locking:	struct hvc_iucv_private->lock
 */
static void hvc_iucv_notifier_del(struct hvc_struct *hp, int id)
{
	struct hvc_iucv_private *priv;

	priv = hvc_iucv_get_private(id);
	if (!priv)
		return;

	flush_sndbuf_sync(priv);

	spin_lock_bh(&priv->lock);
	destroy_tty_buffer_list(&priv->tty_outqueue);
	destroy_tty_buffer_list(&priv->tty_inqueue);
	priv->tty_state = TTY_CLOSED;
	priv->sndbuf_len = 0;
	spin_unlock_bh(&priv->lock);
}

/**
 * hvc_iucv_filter_connreq() - Filter connection request based on z/VM user ID
 * @ipvmid:	Originating z/VM user ID (right padded with blanks)
 *
 * Returns 0 if the z/VM user ID @ipvmid is allowed to connection, otherwise
 * non-zero.
 */
static int hvc_iucv_filter_connreq(u8 ipvmid[8])
{
	size_t i;

	/* Note: default policy is ACCEPT if no filter is set */
	if (!hvc_iucv_filter_size)
		return 0;

	for (i = 0; i < hvc_iucv_filter_size; i++)
		if (0 == memcmp(ipvmid, hvc_iucv_filter + (8 * i), 8))
			return 0;
	return 1;
}

/**
 * hvc_iucv_path_pending() - IUCV handler to process a connection request.
 * @path:	Pending path (struct iucv_path)
 * @ipvmid:	z/VM system identifier of originator
 * @ipuser:	User specified data for this path
 *		(AF_IUCV: port/service name and originator port)
 *
 * The function uses the @ipuser data to determine if the pending path belongs
 * to a terminal managed by this device driver.
 * If the path belongs to this driver, ensure that the terminal is not accessed
 * multiple times (only one connection to a terminal is allowed).
 * If the terminal is not yet connected, the pending path is accepted and is
 * associated to the appropriate struct hvc_iucv_private instance.
 *
 * Returns 0 if @path belongs to a terminal managed by the this device driver;
 * otherwise returns -ENODEV in order to dispatch this path to other handlers.
 *
 * Locking:	struct hvc_iucv_private->lock
 */
static	int hvc_iucv_path_pending(struct iucv_path *path,
				  u8 ipvmid[8], u8 ipuser[16])
{
	struct hvc_iucv_private *priv;
	u8 nuser_data[16];
	u8 vm_user_id[9];
	int i, rc;

	priv = NULL;
	for (i = 0; i < hvc_iucv_devices; i++)
		if (hvc_iucv_table[i] &&
		    (0 == memcmp(hvc_iucv_table[i]->srv_name, ipuser, 8))) {
			priv = hvc_iucv_table[i];
			break;
		}
	if (!priv)
		return -ENODEV;

	/* Enforce that ipvmid is allowed to connect to us */
	read_lock(&hvc_iucv_filter_lock);
	rc = hvc_iucv_filter_connreq(ipvmid);
	read_unlock(&hvc_iucv_filter_lock);
	if (rc) {
		iucv_path_sever(path, ipuser);
		iucv_path_free(path);
		memcpy(vm_user_id, ipvmid, 8);
		vm_user_id[8] = 0;
		pr_info("A connection request from z/VM user ID %s "
			"was refused\n", vm_user_id);
		return 0;
	}

	spin_lock(&priv->lock);

	/* If the terminal is already connected or being severed, then sever
	 * this path to enforce that there is only ONE established communication
	 * path per terminal. */
	if (priv->iucv_state != IUCV_DISCONN) {
		iucv_path_sever(path, ipuser);
		iucv_path_free(path);
		goto out_path_handled;
	}

	/* accept path */
	memcpy(nuser_data, ipuser + 8, 8);  /* remote service (for af_iucv) */
	memcpy(nuser_data + 8, ipuser, 8);  /* local service  (for af_iucv) */
	path->msglim = 0xffff;		    /* IUCV MSGLIMIT */
	path->flags &= ~IUCV_IPRMDATA;	    /* TODO: use IUCV_IPRMDATA */
	rc = iucv_path_accept(path, &hvc_iucv_handler, nuser_data, priv);
	if (rc) {
		iucv_path_sever(path, ipuser);
		iucv_path_free(path);
		goto out_path_handled;
	}
	priv->path = path;
	priv->iucv_state = IUCV_CONNECTED;

	/* flush buffered output data... */
	schedule_delayed_work(&priv->sndbuf_work, 5);

out_path_handled:
	spin_unlock(&priv->lock);
	return 0;
}

/**
 * hvc_iucv_path_severed() - IUCV handler to process a path sever.
 * @path:	Pending path (struct iucv_path)
 * @ipuser:	User specified data for this path
 *		(AF_IUCV: port/service name and originator port)
 *
 * This function calls the hvc_iucv_hangup() function for the
 * respective IUCV HVC terminal.
 *
 * Locking:	struct hvc_iucv_private->lock
 */
static void hvc_iucv_path_severed(struct iucv_path *path, u8 ipuser[16])
{
	struct hvc_iucv_private *priv = path->private;

	hvc_iucv_hangup(priv);
}

/**
 * hvc_iucv_msg_pending() - IUCV handler to process an incoming IUCV message.
 * @path:	Pending path (struct iucv_path)
 * @msg:	Pointer to the IUCV message
 *
 * The function puts an incoming message on the input queue for later
 * processing (by hvc_iucv_get_chars() / hvc_iucv_write()).
 * If the tty has not yet been opened, the message is rejected.
 *
 * Locking:	struct hvc_iucv_private->lock
 */
static void hvc_iucv_msg_pending(struct iucv_path *path,
				 struct iucv_message *msg)
{
	struct hvc_iucv_private *priv = path->private;
	struct iucv_tty_buffer *rb;

	/* reject messages that exceed max size of iucv_tty_msg->datalen */
	if (msg->length > MSG_SIZE(MSG_MAX_DATALEN)) {
		iucv_message_reject(path, msg);
		return;
	}

	spin_lock(&priv->lock);

	/* reject messages if tty has not yet been opened */
	if (priv->tty_state == TTY_CLOSED) {
		iucv_message_reject(path, msg);
		goto unlock_return;
	}

	/* allocate tty buffer to save iucv msg only */
	rb = alloc_tty_buffer(0, GFP_ATOMIC);
	if (!rb) {
		iucv_message_reject(path, msg);
		goto unlock_return;	/* -ENOMEM */
	}
	rb->msg = *msg;

	list_add_tail(&rb->list, &priv->tty_inqueue);

	hvc_kick();	/* wake up hvc thread */

unlock_return:
	spin_unlock(&priv->lock);
}

/**
 * hvc_iucv_msg_complete() - IUCV handler to process message completion
 * @path:	Pending path (struct iucv_path)
 * @msg:	Pointer to the IUCV message
 *
 * The function is called upon completion of message delivery to remove the
 * message from the outqueue. Additional delivery information can be found
 * msg->audit: rejected messages (0x040000 (IPADRJCT)), and
 *	       purged messages	 (0x010000 (IPADPGNR)).
 *
 * Locking:	struct hvc_iucv_private->lock
 */
static void hvc_iucv_msg_complete(struct iucv_path *path,
				  struct iucv_message *msg)
{
	struct hvc_iucv_private *priv = path->private;
	struct iucv_tty_buffer	*ent, *next;
	LIST_HEAD(list_remove);

	spin_lock(&priv->lock);
	list_for_each_entry_safe(ent, next, &priv->tty_outqueue, list)
		if (ent->msg.id == msg->id) {
			list_move(&ent->list, &list_remove);
			break;
		}
	wake_up(&priv->sndbuf_waitq);
	spin_unlock(&priv->lock);
	destroy_tty_buffer_list(&list_remove);
}

/**
 * hvc_iucv_pm_freeze() - Freeze PM callback
 * @dev:	IUVC HVC terminal device
 *
 * Sever an established IUCV communication path and
 * trigger a hang-up of the underlying HVC terminal.
 */
static int hvc_iucv_pm_freeze(struct device *dev)
{
	struct hvc_iucv_private *priv = dev_get_drvdata(dev);

	local_bh_disable();
	hvc_iucv_hangup(priv);
	local_bh_enable();

	return 0;
}

/**
 * hvc_iucv_pm_restore_thaw() - Thaw and restore PM callback
 * @dev:	IUVC HVC terminal device
 *
 * Wake up the HVC thread to trigger hang-up and respective
 * HVC back-end notifier invocations.
 */
static int hvc_iucv_pm_restore_thaw(struct device *dev)
{
	hvc_kick();
	return 0;
}


/* HVC operations */
static const struct hv_ops hvc_iucv_ops = {
	.get_chars = hvc_iucv_get_chars,
	.put_chars = hvc_iucv_put_chars,
	.notifier_add = hvc_iucv_notifier_add,
	.notifier_del = hvc_iucv_notifier_del,
	.notifier_hangup = hvc_iucv_notifier_hangup,
	.dtr_rts = hvc_iucv_dtr_rts,
};

/* Suspend / resume device operations */
static const struct dev_pm_ops hvc_iucv_pm_ops = {
	.freeze	  = hvc_iucv_pm_freeze,
	.thaw	  = hvc_iucv_pm_restore_thaw,
	.restore  = hvc_iucv_pm_restore_thaw,
};

/* IUCV HVC device driver */
static struct device_driver hvc_iucv_driver = {
	.name = KMSG_COMPONENT,
	.bus  = &iucv_bus,
	.pm   = &hvc_iucv_pm_ops,
};

/**
 * hvc_iucv_alloc() - Allocates a new struct hvc_iucv_private instance
 * @id:			hvc_iucv_table index
 * @is_console:		Flag if the instance is used as Linux console
 *
 * This function allocates a new hvc_iucv_private structure and stores
 * the instance in hvc_iucv_table at index @id.
 * Returns 0 on success; otherwise non-zero.
 */
static int __init hvc_iucv_alloc(int id, unsigned int is_console)
{
	struct hvc_iucv_private *priv;
	char name[9];
	int rc;

	priv = kzalloc(sizeof(struct hvc_iucv_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);
	INIT_LIST_HEAD(&priv->tty_outqueue);
	INIT_LIST_HEAD(&priv->tty_inqueue);
	INIT_DELAYED_WORK(&priv->sndbuf_work, hvc_iucv_sndbuf_work);
	init_waitqueue_head(&priv->sndbuf_waitq);

	priv->sndbuf = (void *) get_zeroed_page(GFP_KERNEL);
	if (!priv->sndbuf) {
		kfree(priv);
		return -ENOMEM;
	}

	/* set console flag */
	priv->is_console = is_console;

	/* allocate hvc device */
	priv->hvc = hvc_alloc(HVC_IUCV_MAGIC + id, /*		  PAGE_SIZE */
			      HVC_IUCV_MAGIC + id, &hvc_iucv_ops, 256);
	if (IS_ERR(priv->hvc)) {
		rc = PTR_ERR(priv->hvc);
		goto out_error_hvc;
	}

	/* notify HVC thread instead of using polling */
	priv->hvc->irq_requested = 1;

	/* setup iucv related information */
	snprintf(name, 9, "lnxhvc%-2d", id);
	memcpy(priv->srv_name, name, 8);
	ASCEBC(priv->srv_name, 8);

	/* create and setup device */
	priv->dev = kzalloc(sizeof(*priv->dev), GFP_KERNEL);
	if (!priv->dev) {
		rc = -ENOMEM;
		goto out_error_dev;
	}
	dev_set_name(priv->dev, "hvc_iucv%d", id);
	dev_set_drvdata(priv->dev, priv);
	priv->dev->bus = &iucv_bus;
	priv->dev->parent = iucv_root;
	priv->dev->driver = &hvc_iucv_driver;
	priv->dev->release = (void (*)(struct device *)) kfree;
	rc = device_register(priv->dev);
	if (rc) {
		put_device(priv->dev);
		goto out_error_dev;
	}

	hvc_iucv_table[id] = priv;
	return 0;

out_error_dev:
	hvc_remove(priv->hvc);
out_error_hvc:
	free_page((unsigned long) priv->sndbuf);
	kfree(priv);

	return rc;
}

/**
 * hvc_iucv_destroy() - Destroy and free hvc_iucv_private instances
 */
static void __init hvc_iucv_destroy(struct hvc_iucv_private *priv)
{
	hvc_remove(priv->hvc);
	device_unregister(priv->dev);
	free_page((unsigned long) priv->sndbuf);
	kfree(priv);
}

/**
 * hvc_iucv_parse_filter() - Parse filter for a single z/VM user ID
 * @filter:	String containing a comma-separated list of z/VM user IDs
 */
static const char *hvc_iucv_parse_filter(const char *filter, char *dest)
{
	const char *nextdelim, *residual;
	size_t len;

	nextdelim = strchr(filter, ',');
	if (nextdelim) {
		len = nextdelim - filter;
		residual = nextdelim + 1;
	} else {
		len = strlen(filter);
		residual = filter + len;
	}

	if (len == 0)
		return ERR_PTR(-EINVAL);

	/* check for '\n' (if called from sysfs) */
	if (filter[len - 1] == '\n')
		len--;

	if (len > 8)
		return ERR_PTR(-EINVAL);

	/* pad with blanks and save upper case version of user ID */
	memset(dest, ' ', 8);
	while (len--)
		dest[len] = toupper(filter[len]);
	return residual;
}

/**
 * hvc_iucv_setup_filter() - Set up z/VM user ID filter
 * @filter:	String consisting of a comma-separated list of z/VM user IDs
 *
 * The function parses the @filter string and creates an array containing
 * the list of z/VM user ID filter entries.
 * Return code 0 means success, -EINVAL if the filter is syntactically
 * incorrect, -ENOMEM if there was not enough memory to allocate the
 * filter list array, or -ENOSPC if too many z/VM user IDs have been specified.
 */
static int hvc_iucv_setup_filter(const char *val)
{
	const char *residual;
	int err;
	size_t size, count;
	void *array, *old_filter;

	count = strlen(val);
	if (count == 0 || (count == 1 && val[0] == '\n')) {
		size  = 0;
		array = NULL;
		goto out_replace_filter;	/* clear filter */
	}

	/* count user IDs in order to allocate sufficient memory */
	size = 1;
	residual = val;
	while ((residual = strchr(residual, ',')) != NULL) {
		residual++;
		size++;
	}

	/* check if the specified list exceeds the filter limit */
	if (size > MAX_VMID_FILTER)
		return -ENOSPC;

	array = kzalloc(size * 8, GFP_KERNEL);
	if (!array)
		return -ENOMEM;

	count = size;
	residual = val;
	while (*residual && count) {
		residual = hvc_iucv_parse_filter(residual,
						 array + ((size - count) * 8));
		if (IS_ERR(residual)) {
			err = PTR_ERR(residual);
			kfree(array);
			goto out_err;
		}
		count--;
	}

out_replace_filter:
	write_lock_bh(&hvc_iucv_filter_lock);
	old_filter = hvc_iucv_filter;
	hvc_iucv_filter_size = size;
	hvc_iucv_filter = array;
	write_unlock_bh(&hvc_iucv_filter_lock);
	kfree(old_filter);

	err = 0;
out_err:
	return err;
}

/**
 * param_set_vmidfilter() - Set z/VM user ID filter parameter
 * @val:	String consisting of a comma-separated list of z/VM user IDs
 * @kp:		Kernel parameter pointing to hvc_iucv_filter array
 *
 * The function sets up the z/VM user ID filter specified as comma-separated
 * list of user IDs in @val.
 * Note: If it is called early in the boot process, @val is stored and
 *	 parsed later in hvc_iucv_init().
 */
static int param_set_vmidfilter(const char *val, const struct kernel_param *kp)
{
	int rc;

	if (!MACHINE_IS_VM || !hvc_iucv_devices)
		return -ENODEV;

	if (!val)
		return -EINVAL;

	rc = 0;
	if (slab_is_available())
		rc = hvc_iucv_setup_filter(val);
	else
		hvc_iucv_filter_string = val;	/* defer... */
	return rc;
}

/**
 * param_get_vmidfilter() - Get z/VM user ID filter
 * @buffer:	Buffer to store z/VM user ID filter,
 *		(buffer size assumption PAGE_SIZE)
 * @kp:		Kernel parameter pointing to the hvc_iucv_filter array
 *
 * The function stores the filter as a comma-separated list of z/VM user IDs
 * in @buffer. Typically, sysfs routines call this function for attr show.
 */
static int param_get_vmidfilter(char *buffer, const struct kernel_param *kp)
{
	int rc;
	size_t index, len;
	void *start, *end;

	if (!MACHINE_IS_VM || !hvc_iucv_devices)
		return -ENODEV;

	rc = 0;
	read_lock_bh(&hvc_iucv_filter_lock);
	for (index = 0; index < hvc_iucv_filter_size; index++) {
		start = hvc_iucv_filter + (8 * index);
		end   = memchr(start, ' ', 8);
		len   = (end) ? end - start : 8;
		memcpy(buffer + rc, start, len);
		rc += len;
		buffer[rc++] = ',';
	}
	read_unlock_bh(&hvc_iucv_filter_lock);
	if (rc)
		buffer[--rc] = '\0';	/* replace last comma and update rc */
	return rc;
}

#define param_check_vmidfilter(name, p) __param_check(name, p, void)

static struct kernel_param_ops param_ops_vmidfilter = {
	.set = param_set_vmidfilter,
	.get = param_get_vmidfilter,
};

/**
 * hvc_iucv_init() - z/VM IUCV HVC device driver initialization
 */
static int __init hvc_iucv_init(void)
{
	int rc;
	unsigned int i;

	if (!hvc_iucv_devices)
		return -ENODEV;

	if (!MACHINE_IS_VM) {
		pr_notice("The z/VM IUCV HVC device driver cannot "
			   "be used without z/VM\n");
		rc = -ENODEV;
		goto out_error;
	}

	if (hvc_iucv_devices > MAX_HVC_IUCV_LINES) {
		pr_err("%lu is not a valid value for the hvc_iucv= "
			"kernel parameter\n", hvc_iucv_devices);
		rc = -EINVAL;
		goto out_error;
	}

	/* register IUCV HVC device driver */
	rc = driver_register(&hvc_iucv_driver);
	if (rc)
		goto out_error;

	/* parse hvc_iucv_allow string and create z/VM user ID filter list */
	if (hvc_iucv_filter_string) {
		rc = hvc_iucv_setup_filter(hvc_iucv_filter_string);
		switch (rc) {
		case 0:
			break;
		case -ENOMEM:
			pr_err("Allocating memory failed with "
				"reason code=%d\n", 3);
			goto out_error;
		case -EINVAL:
			pr_err("hvc_iucv_allow= does not specify a valid "
				"z/VM user ID list\n");
			goto out_error;
		case -ENOSPC:
			pr_err("hvc_iucv_allow= specifies too many "
				"z/VM user IDs\n");
			goto out_error;
		default:
			goto out_error;
		}
	}

	hvc_iucv_buffer_cache = kmem_cache_create(KMSG_COMPONENT,
					   sizeof(struct iucv_tty_buffer),
					   0, 0, NULL);
	if (!hvc_iucv_buffer_cache) {
		pr_err("Allocating memory failed with reason code=%d\n", 1);
		rc = -ENOMEM;
		goto out_error;
	}

	hvc_iucv_mempool = mempool_create_slab_pool(MEMPOOL_MIN_NR,
						    hvc_iucv_buffer_cache);
	if (!hvc_iucv_mempool) {
		pr_err("Allocating memory failed with reason code=%d\n", 2);
		kmem_cache_destroy(hvc_iucv_buffer_cache);
		rc = -ENOMEM;
		goto out_error;
	}

	/* register the first terminal device as console
	 * (must be done before allocating hvc terminal devices) */
	rc = hvc_instantiate(HVC_IUCV_MAGIC, IUCV_HVC_CON_IDX, &hvc_iucv_ops);
	if (rc) {
		pr_err("Registering HVC terminal device as "
		       "Linux console failed\n");
		goto out_error_memory;
	}

	/* allocate hvc_iucv_private structs */
	for (i = 0; i < hvc_iucv_devices; i++) {
		rc = hvc_iucv_alloc(i, (i == IUCV_HVC_CON_IDX) ? 1 : 0);
		if (rc) {
			pr_err("Creating a new HVC terminal device "
				"failed with error code=%d\n", rc);
			goto out_error_hvc;
		}
	}

	/* register IUCV callback handler */
	rc = iucv_register(&hvc_iucv_handler, 0);
	if (rc) {
		pr_err("Registering IUCV handlers failed with error code=%d\n",
			rc);
		goto out_error_hvc;
	}

	return 0;

out_error_hvc:
	for (i = 0; i < hvc_iucv_devices; i++)
		if (hvc_iucv_table[i])
			hvc_iucv_destroy(hvc_iucv_table[i]);
out_error_memory:
	mempool_destroy(hvc_iucv_mempool);
	kmem_cache_destroy(hvc_iucv_buffer_cache);
out_error:
	if (hvc_iucv_filter)
		kfree(hvc_iucv_filter);
	hvc_iucv_devices = 0; /* ensure that we do not provide any device */
	return rc;
}

/**
 * hvc_iucv_config() - Parsing of hvc_iucv=  kernel command line parameter
 * @val:	Parameter value (numeric)
 */
static	int __init hvc_iucv_config(char *val)
{
	 return kstrtoul(val, 10, &hvc_iucv_devices);
}


device_initcall(hvc_iucv_init);
__setup("hvc_iucv=", hvc_iucv_config);
core_param(hvc_iucv_allow, hvc_iucv_filter, vmidfilter, 0640);
