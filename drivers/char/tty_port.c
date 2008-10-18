/*
 * Tty port functions
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/module.h>

void tty_port_init(struct tty_port *port)
{
	memset(port, 0, sizeof(*port));
	init_waitqueue_head(&port->open_wait);
	init_waitqueue_head(&port->close_wait);
	mutex_init(&port->mutex);
	spin_lock_init(&port->lock);
	port->close_delay = (50 * HZ) / 100;
	port->closing_wait = (3000 * HZ) / 100;
}
EXPORT_SYMBOL(tty_port_init);

int tty_port_alloc_xmit_buf(struct tty_port *port)
{
	/* We may sleep in get_zeroed_page() */
	mutex_lock(&port->mutex);
	if (port->xmit_buf == NULL)
		port->xmit_buf = (unsigned char *)get_zeroed_page(GFP_KERNEL);
	mutex_unlock(&port->mutex);
	if (port->xmit_buf == NULL)
		return -ENOMEM;
	return 0;
}
EXPORT_SYMBOL(tty_port_alloc_xmit_buf);

void tty_port_free_xmit_buf(struct tty_port *port)
{
	mutex_lock(&port->mutex);
	if (port->xmit_buf != NULL) {
		free_page((unsigned long)port->xmit_buf);
		port->xmit_buf = NULL;
	}
	mutex_unlock(&port->mutex);
}
EXPORT_SYMBOL(tty_port_free_xmit_buf);


/**
 *	tty_port_tty_get	-	get a tty reference
 *	@port: tty port
 *
 *	Return a refcount protected tty instance or NULL if the port is not
 *	associated with a tty (eg due to close or hangup)
 */

struct tty_struct *tty_port_tty_get(struct tty_port *port)
{
	unsigned long flags;
	struct tty_struct *tty;

	spin_lock_irqsave(&port->lock, flags);
	tty = tty_kref_get(port->tty);
	spin_unlock_irqrestore(&port->lock, flags);
	return tty;
}
EXPORT_SYMBOL(tty_port_tty_get);

/**
 *	tty_port_tty_set	-	set the tty of a port
 *	@port: tty port
 *	@tty: the tty
 *
 *	Associate the port and tty pair. Manages any internal refcounts.
 *	Pass NULL to deassociate a port
 */

void tty_port_tty_set(struct tty_port *port, struct tty_struct *tty)
{
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	if (port->tty)
		tty_kref_put(port->tty);
	port->tty = tty;
	spin_unlock_irqrestore(&port->lock, flags);
}
EXPORT_SYMBOL(tty_port_tty_set);
