/*
 * hvc_iucv.c - z/VM IUCV back-end for the Hypervisor Console (HVC)
 *
 * This back-end for HVC provides terminal access via
 * z/VM IUCV communication paths.
 *
 * Copyright IBM Corp. 2008.
 *
 * Author(s):	Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */
#define KMSG_COMPONENT		"hvc_iucv"

#include <linux/types.h>
#include <asm/ebcdic.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <net/iucv/iucv.h>

#include "hvc_console.h"


/* HVC backend for z/VM IUCV */
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

#define MSG_SIZE(s)		((s) + offsetof(struct iucv_tty_msg, data))
struct iucv_tty_msg {
	u8	version;		/* Message version */
	u8	type;			/* Message type */
#define MSG_MAX_DATALEN		(~(u16)0)
	u16	datalen;		/* Payload length */
	u8	data[];			/* Payload buffer */
} __attribute__((packed));

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
	struct hvc_struct	*hvc; /* HVC console struct reference */
	u8			srv_name[8];	/* IUCV service name (ebcdic) */
	enum iucv_state_t	iucv_state;	/* IUCV connection status */
	enum tty_state_t	tty_state;	/* TTY status */
	struct iucv_path	*path;		/* IUCV path pointer */
	spinlock_t		lock;		/* hvc_iucv_private lock */
	struct list_head	tty_outqueue;	/* outgoing IUCV messages */
	struct list_head	tty_inqueue;	/* incoming IUCV messages */
};

struct iucv_tty_buffer {
	struct list_head	list;	/* list pointer */
	struct iucv_message	msg;	/* store an incoming IUCV message */
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
 * alloc_tty_buffer() - Returns a new struct iucv_tty_buffer element.
 * @size:	Size of the internal buffer used to store data.
 * @flags:	Memory allocation flags passed to mempool.
 *
 * This function allocates a new struct iucv_tty_buffer element and, optionally,
 * allocates an internal data buffer with the specified size @size.
 * Note: The total message size arises from the internal buffer size and the
 *	 members of the iucv_tty_msg structure.
 *
 * The function returns NULL if memory allocation has failed.
 */
static struct iucv_tty_buffer *alloc_tty_buffer(size_t size, gfp_t flags)
{
	struct iucv_tty_buffer *bufp;

	bufp = mempool_alloc(hvc_iucv_mempool, flags);
	if (!bufp)
		return NULL;
	memset(bufp, 0, sizeof(struct iucv_tty_buffer));

	if (size > 0) {
		bufp->msg.length = MSG_SIZE(size);
		bufp->mbuf = kmalloc(bufp->msg.length, flags);
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
 *
 * The destroy_tty_buffer() function frees the internal data buffer and returns
 * the struct iucv_tty_buffer element back to the mempool for freeing.
 */
static void destroy_tty_buffer(struct iucv_tty_buffer *bufp)
{
	kfree(bufp->mbuf);
	mempool_free(bufp, hvc_iucv_mempool);
}

/**
 * destroy_tty_buffer_list() - call destroy_tty_buffer() for each list element.
 * @list:	List head pointer to a list containing struct iucv_tty_buffer
 *		elements.
 *
 * Calls destroy_tty_buffer() for each struct iucv_tty_buffer element in the
 * list @list.
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
 * hvc_iucv_write() - Receive IUCV message write data to HVC console buffer.
 * @priv:		Pointer to hvc_iucv_private structure.
 * @buf:		HVC console buffer for writing received terminal data.
 * @count:		HVC console buffer size.
 * @has_more_data:	Pointer to an int variable.
 *
 * The function picks up pending messages from the input queue and receives
 * the message data that is then written to the specified buffer @buf.
 * If the buffer size @count is less than the data message size, then the
 * message is kept on the input queue and @has_more_data is set to 1.
 * If the message data has been entirely written, the message is removed from
 * the input queue.
 *
 * The function returns the number of bytes written to the terminal, zero if
 * there are no pending data messages available or if there is no established
 * IUCV path.
 * If the IUCV path has been severed, then -EPIPE is returned to cause a
 * hang up (that is issued by the HVC console layer).
 */
static int hvc_iucv_write(struct hvc_iucv_private *priv,
			  char *buf, int count, int *has_more_data)
{
	struct iucv_tty_buffer *rb;
	int written;
	int rc;

	/* Immediately return if there is no IUCV connection */
	if (priv->iucv_state == IUCV_DISCONN)
		return 0;

	/* If the IUCV path has been severed, return -EPIPE to inform the
	 * hvc console layer to hang up the tty device. */
	if (priv->iucv_state == IUCV_SEVERED)
		return -EPIPE;

	/* check if there are pending messages */
	if (list_empty(&priv->tty_inqueue))
		return 0;

	/* receive a iucv message and flip data to the tty (ldisc) */
	rb = list_first_entry(&priv->tty_inqueue, struct iucv_tty_buffer, list);

	written = 0;
	if (!rb->mbuf) { /* message not yet received ... */
		/* allocate mem to store msg data; if no memory is available
		 * then leave the buffer on the list and re-try later */
		rb->mbuf = kmalloc(rb->msg.length, GFP_ATOMIC);
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
		/* remove buffer if an error has occured or received data
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
		hvc_resize(priv->hvc, *((struct winsize *)rb->mbuf->data));
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
 * The hvc_console thread calls this method to read characters from
 * the terminal backend. If an IUCV communication path has been established,
 * pending IUCV messages are received and data is copied into buffer @buf
 * up to @count bytes.
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
 * hvc_iucv_send() - Send an IUCV message containing terminal data.
 * @priv:	Pointer to struct hvc_iucv_private instance.
 * @buf:	Buffer containing data to send.
 * @size:	Size of buffer and amount of data to send.
 *
 * If an IUCV communication path is established, the function copies the buffer
 * data to a newly allocated struct iucv_tty_buffer element, sends the data and
 * puts the element to the outqueue.
 *
 * If there is no IUCV communication path established, the function returns 0.
 * If an existing IUCV communicaton path has been severed, the function returns
 * -EPIPE (can be passed to HVC layer to cause a tty hangup).
 */
static int hvc_iucv_send(struct hvc_iucv_private *priv, const char *buf,
			 int count)
{
	struct iucv_tty_buffer *sb;
	int rc;
	u16 len;

	if (priv->iucv_state == IUCV_SEVERED)
		return -EPIPE;

	if (priv->iucv_state == IUCV_DISCONN)
		return 0;

	len = min_t(u16, MSG_MAX_DATALEN, count);

	/* allocate internal buffer to store msg data and also compute total
	 * message length */
	sb = alloc_tty_buffer(len, GFP_ATOMIC);
	if (!sb)
		return -ENOMEM;

	sb->mbuf->datalen = len;
	memcpy(sb->mbuf->data, buf, len);

	list_add_tail(&sb->list, &priv->tty_outqueue);

	rc = __iucv_message_send(priv->path, &sb->msg, 0, 0,
				 (void *) sb->mbuf, sb->msg.length);
	if (rc) {
		list_del(&sb->list);
		destroy_tty_buffer(sb);
		len = 0;
	}

	return len;
}

/**
 * hvc_iucv_put_chars() - HVC put_chars operation.
 * @vtermno:	HVC virtual terminal number.
 * @buf:	Pointer to an buffer to read data from
 * @count:	Size of buffer available for reading
 *
 * The hvc_console thread calls this method to write characters from
 * to the terminal backend.
 * The function calls hvc_iucv_send() under the lock of the
 * struct hvc_iucv_private instance that corresponds to the tty @vtermno.
 *
 * Locking:	The method gets called under an irqsave() spinlock; and
 *		locks struct hvc_iucv_private->lock.
 */
static int hvc_iucv_put_chars(uint32_t vtermno, const char *buf, int count)
{
	struct hvc_iucv_private *priv = hvc_iucv_get_private(vtermno);
	int sent;

	if (count <= 0)
		return 0;

	if (!priv)
		return -ENODEV;

	spin_lock(&priv->lock);
	sent = hvc_iucv_send(priv, buf, count);
	spin_unlock(&priv->lock);

	return sent;
}

/**
 * hvc_iucv_notifier_add() - HVC notifier for opening a TTY for the first time.
 * @hp:	Pointer to the HVC device (struct hvc_struct)
 * @id:	Additional data (originally passed to hvc_alloc): the index of an struct
 *	hvc_iucv_private instance.
 *
 * The function sets the tty state to TTY_OPEN for the struct hvc_iucv_private
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
 * hvc_iucv_cleanup() - Clean up function if the tty portion is finally closed.
 * @priv:	Pointer to the struct hvc_iucv_private instance.
 *
 * The functions severs the established IUCV communication path (if any), and
 * destroy struct iucv_tty_buffer elements from the in- and outqueue. Finally,
 * the functions resets the states to TTY_CLOSED and IUCV_DISCONN.
 */
static void hvc_iucv_cleanup(struct hvc_iucv_private *priv)
{
	destroy_tty_buffer_list(&priv->tty_outqueue);
	destroy_tty_buffer_list(&priv->tty_inqueue);

	priv->tty_state = TTY_CLOSED;
	priv->iucv_state = IUCV_DISCONN;
}

/**
 * hvc_iucv_notifier_hangup() - HVC notifier for tty hangups.
 * @hp: Pointer to the HVC device (struct hvc_struct)
 * @id: Additional data (originally passed to hvc_alloc): the index of an struct
 *	hvc_iucv_private instance.
 *
 * This routine notifies the HVC backend that a tty hangup (carrier loss,
 * virtual or otherwise) has occured.
 *
 * The HVC backend for z/VM IUCV ignores virtual hangups (vhangup()), to keep
 * an existing IUCV communication path established.
 * (Background: vhangup() is called from user space (by getty or login) to
 *		disable writing to the tty by other applications).
 *
 * If the tty has been opened (e.g. getty) and an established IUCV path has been
 * severed (we caused the tty hangup in that case), then the functions invokes
 * hvc_iucv_cleanup() to clean up.
 *
 * Locking:	struct hvc_iucv_private->lock
 */
static void hvc_iucv_notifier_hangup(struct hvc_struct *hp, int id)
{
	struct hvc_iucv_private *priv;

	priv = hvc_iucv_get_private(id);
	if (!priv)
		return;

	spin_lock_bh(&priv->lock);
	/* NOTE: If the hangup was scheduled by ourself (from the iucv
	 *	 path_servered callback [IUCV_SEVERED]), then we have to
	 *	 finally clean up the tty backend structure and set state to
	 *	 TTY_CLOSED.
	 *
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
 * hvc_iucv_notifier_del() - HVC notifier for closing a TTY for the last time.
 * @hp:		Pointer to the HVC device (struct hvc_struct)
 * @id:		Additional data (originally passed to hvc_alloc):
 *		the index of an struct hvc_iucv_private instance.
 *
 * This routine notifies the HVC backend that the last tty device file
 * descriptor has been closed.
 * The function calls hvc_iucv_cleanup() to clean up the struct hvc_iucv_private
 * instance.
 *
 * Locking:	struct hvc_iucv_private->lock
 */
static void hvc_iucv_notifier_del(struct hvc_struct *hp, int id)
{
	struct hvc_iucv_private *priv;
	struct iucv_path	*path;

	priv = hvc_iucv_get_private(id);
	if (!priv)
		return;

	spin_lock_bh(&priv->lock);
	path = priv->path;		/* save reference to IUCV path */
	priv->path = NULL;
	hvc_iucv_cleanup(priv);
	spin_unlock_bh(&priv->lock);

	/* sever IUCV path outside of priv->lock due to lock ordering of:
	 * priv->lock <--> iucv_table_lock */
	if (path) {
		iucv_path_sever(path, NULL);
		iucv_path_free(path);
	}
}

/**
 * hvc_iucv_path_pending() - IUCV handler to process a connection request.
 * @path:	Pending path (struct iucv_path)
 * @ipvmid:	Originator z/VM system identifier
 * @ipuser:	User specified data for this path
 *		(AF_IUCV: port/service name and originator port)
 *
 * The function uses the @ipuser data to check to determine if the pending
 * path belongs to a terminal managed by this HVC backend.
 * If the check is successful, then an additional check is done to ensure
 * that a terminal cannot be accessed multiple times (only one connection
 * to a terminal is allowed). In that particular case, the pending path is
 * severed. If it is the first connection, the pending path is accepted and
 * associated to the struct hvc_iucv_private. The iucv state is updated to
 * reflect that a communication path has been established.
 *
 * Returns 0 if the path belongs to a terminal managed by the this HVC backend;
 * otherwise returns -ENODEV in order to dispatch this path to other handlers.
 *
 * Locking:	struct hvc_iucv_private->lock
 */
static	int hvc_iucv_path_pending(struct iucv_path *path,
				  u8 ipvmid[8], u8 ipuser[16])
{
	struct hvc_iucv_private *priv;
	u8 nuser_data[16];
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
 * The function also severs the path (as required by the IUCV protocol) and
 * sets the iucv state to IUCV_SEVERED for the associated struct
 * hvc_iucv_private instance. Later, the IUCV_SEVERED state triggers a tty
 * hangup (hvc_iucv_get_chars() / hvc_iucv_write()).
 *
 * If tty portion of the HVC is closed then clean up the outqueue in addition.
 *
 * Locking:	struct hvc_iucv_private->lock
 */
static void hvc_iucv_path_severed(struct iucv_path *path, u8 ipuser[16])
{
	struct hvc_iucv_private *priv = path->private;

	spin_lock(&priv->lock);
	priv->iucv_state = IUCV_SEVERED;

	/* NOTE: If the tty has not yet been opened by a getty program
	 *	 (e.g. to see console messages), then cleanup the
	 *	 hvc_iucv_private structure to allow re-connects.
	 *
	 *	 If the tty has been opened, the get_chars() callback returns
	 *	 -EPIPE to signal the hvc console layer to hang up the tty. */
	priv->path = NULL;
	if (priv->tty_state == TTY_CLOSED)
		hvc_iucv_cleanup(priv);
	spin_unlock(&priv->lock);

	/* finally sever path (outside of priv->lock due to lock ordering) */
	iucv_path_sever(path, ipuser);
	iucv_path_free(path);
}

/**
 * hvc_iucv_msg_pending() - IUCV handler to process an incoming IUCV message.
 * @path:	Pending path (struct iucv_path)
 * @msg:	Pointer to the IUCV message
 *
 * The function stores an incoming message on the input queue for later
 * processing (by hvc_iucv_get_chars() / hvc_iucv_write()).
 * However, if the tty has not yet been opened, the message is rejected.
 *
 * Locking:	struct hvc_iucv_private->lock
 */
static void hvc_iucv_msg_pending(struct iucv_path *path,
				 struct iucv_message *msg)
{
	struct hvc_iucv_private *priv = path->private;
	struct iucv_tty_buffer *rb;

	spin_lock(&priv->lock);

	/* reject messages if tty has not yet been opened */
	if (priv->tty_state == TTY_CLOSED) {
		iucv_message_reject(path, msg);
		goto unlock_return;
	}

	/* allocate buffer an empty buffer element */
	rb = alloc_tty_buffer(0, GFP_ATOMIC);
	if (!rb) {
		iucv_message_reject(path, msg);
		goto unlock_return;	/* -ENOMEM */
	}
	rb->msg = *msg;

	list_add_tail(&rb->list, &priv->tty_inqueue);

	hvc_kick();	/* wakup hvc console thread */

unlock_return:
	spin_unlock(&priv->lock);
}

/**
 * hvc_iucv_msg_complete() - IUCV handler to process message completion
 * @path:	Pending path (struct iucv_path)
 * @msg:	Pointer to the IUCV message
 *
 * The function is called upon completion of message delivery and the
 * message is removed from the outqueue. Additional delivery information
 * can be found in msg->audit: rejected messages (0x040000 (IPADRJCT)) and
 * purged messages (0x010000 (IPADPGNR)).
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
	spin_unlock(&priv->lock);
	destroy_tty_buffer_list(&list_remove);
}


/* HVC operations */
static struct hv_ops hvc_iucv_ops = {
	.get_chars = hvc_iucv_get_chars,
	.put_chars = hvc_iucv_put_chars,
	.notifier_add = hvc_iucv_notifier_add,
	.notifier_del = hvc_iucv_notifier_del,
	.notifier_hangup = hvc_iucv_notifier_hangup,
};

/**
 * hvc_iucv_alloc() - Allocates a new struct hvc_iucv_private instance
 * @id:	hvc_iucv_table index
 *
 * This function allocates a new hvc_iucv_private struct and put the
 * instance into hvc_iucv_table at index @id.
 * Returns 0 on success; otherwise non-zero.
 */
static int __init hvc_iucv_alloc(int id)
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

	/* Finally allocate hvc */
	priv->hvc = hvc_alloc(HVC_IUCV_MAGIC + id,
			      HVC_IUCV_MAGIC + id, &hvc_iucv_ops, PAGE_SIZE);
	if (IS_ERR(priv->hvc)) {
		rc = PTR_ERR(priv->hvc);
		kfree(priv);
		return rc;
	}

	/* setup iucv related information */
	snprintf(name, 9, "lnxhvc%-2d", id);
	memcpy(priv->srv_name, name, 8);
	ASCEBC(priv->srv_name, 8);

	hvc_iucv_table[id] = priv;
	return 0;
}

/**
 * hvc_iucv_init() - Initialization of HVC backend for z/VM IUCV
 */
static int __init hvc_iucv_init(void)
{
	int rc, i;

	if (!MACHINE_IS_VM) {
		pr_warning("The z/VM IUCV Hypervisor console cannot be "
			   "used without z/VM.\n");
		return -ENODEV;
	}

	if (!hvc_iucv_devices)
		return -ENODEV;

	if (hvc_iucv_devices > MAX_HVC_IUCV_LINES)
		return -EINVAL;

	hvc_iucv_buffer_cache = kmem_cache_create(KMSG_COMPONENT,
					   sizeof(struct iucv_tty_buffer),
					   0, 0, NULL);
	if (!hvc_iucv_buffer_cache) {
		pr_err("Not enough memory for driver initialization "
			"(rs=%d).\n", 1);
		return -ENOMEM;
	}

	hvc_iucv_mempool = mempool_create_slab_pool(MEMPOOL_MIN_NR,
						    hvc_iucv_buffer_cache);
	if (!hvc_iucv_mempool) {
		pr_err("Not enough memory for driver initialization "
			"(rs=%d).\n", 2);
		kmem_cache_destroy(hvc_iucv_buffer_cache);
		return -ENOMEM;
	}

	/* allocate hvc_iucv_private structs */
	for (i = 0; i < hvc_iucv_devices; i++) {
		rc = hvc_iucv_alloc(i);
		if (rc) {
			pr_err("Could not create new z/VM IUCV HVC backend "
				"rc=%d.\n", rc);
			goto out_error_hvc;
		}
	}

	/* register IUCV callback handler */
	rc = iucv_register(&hvc_iucv_handler, 0);
	if (rc) {
		pr_err("Could not register iucv handler (rc=%d).\n", rc);
		goto out_error_iucv;
	}

	return 0;

out_error_iucv:
	iucv_unregister(&hvc_iucv_handler, 0);
out_error_hvc:
	for (i = 0; i < hvc_iucv_devices; i++)
		if (hvc_iucv_table[i]) {
			if (hvc_iucv_table[i]->hvc)
				hvc_remove(hvc_iucv_table[i]->hvc);
			kfree(hvc_iucv_table[i]);
		}
	mempool_destroy(hvc_iucv_mempool);
	kmem_cache_destroy(hvc_iucv_buffer_cache);
	return rc;
}

/**
 * hvc_iucv_console_init() - Early console initialization
 */
static	int __init hvc_iucv_console_init(void)
{
	if (!MACHINE_IS_VM || !hvc_iucv_devices)
		return -ENODEV;
	return hvc_instantiate(HVC_IUCV_MAGIC, 0, &hvc_iucv_ops);
}

/**
 * hvc_iucv_config() - Parsing of hvc_iucv=  kernel command line parameter
 * @val:	Parameter value (numeric)
 */
static	int __init hvc_iucv_config(char *val)
{
	 return strict_strtoul(val, 10, &hvc_iucv_devices);
}


module_init(hvc_iucv_init);
console_initcall(hvc_iucv_console_init);
__setup("hvc_iucv=", hvc_iucv_config);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HVC back-end for z/VM IUCV.");
MODULE_AUTHOR("Hendrik Brueckner <brueckner@linux.vnet.ibm.com>");
