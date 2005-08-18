/*
 * linux/drivers/char/ppdev.c
 *
 * This is the code behind /dev/parport* -- it allows a user-space
 * application to use the parport subsystem.
 *
 * Copyright (C) 1998-2000, 2002 Tim Waugh <tim@cyberelk.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * A /dev/parportx device node represents an arbitrary device
 * on port 'x'.  The following operations are possible:
 *
 * open		do nothing, set up default IEEE 1284 protocol to be COMPAT
 * close	release port and unregister device (if necessary)
 * ioctl
 *   EXCL	register device exclusively (may fail)
 *   CLAIM	(register device first time) parport_claim_or_block
 *   RELEASE	parport_release
 *   SETMODE	set the IEEE 1284 protocol to use for read/write
 *   SETPHASE	set the IEEE 1284 phase of a particular mode.  Not to be
 *              confused with ioctl(fd, SETPHASER, &stun). ;-)
 *   DATADIR	data_forward / data_reverse
 *   WDATA	write_data
 *   RDATA	read_data
 *   WCONTROL	write_control
 *   RCONTROL	read_control
 *   FCONTROL	frob_control
 *   RSTATUS	read_status
 *   NEGOT	parport_negotiate
 *   YIELD	parport_yield_blocking
 *   WCTLONIRQ	on interrupt, set control lines
 *   CLRIRQ	clear (and return) interrupt count
 *   SETTIME	sets device timeout (struct timeval)
 *   GETTIME	gets device timeout (struct timeval)
 *   GETMODES	gets hardware supported modes (unsigned int)
 *   GETMODE	gets the current IEEE1284 mode
 *   GETPHASE   gets the current IEEE1284 phase
 *   GETFLAGS   gets current (user-visible) flags
 *   SETFLAGS   sets current (user-visible) flags
 * read/write	read or write in current IEEE 1284 protocol
 * select	wait for interrupt (in readfds)
 *
 * Changes:
 * Added SETTIME/GETTIME ioctl, Fred Barnes, 1999.
 *
 * Arnaldo Carvalho de Melo <acme@conectiva.com.br> 2000/08/25
 * - On error, copy_from_user and copy_to_user do not return -EFAULT,
 *   They return the positive number of bytes *not* copied due to address
 *   space errors.
 *
 * Added GETMODES/GETMODE/GETPHASE ioctls, Fred Barnes <frmb2@ukc.ac.uk>, 03/01/2001.
 * Added GETFLAGS/SETFLAGS ioctls, Fred Barnes, 04/2001
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/ioctl.h>
#include <linux/parport.h>
#include <linux/ctype.h>
#include <linux/poll.h>
#include <asm/uaccess.h>
#include <linux/ppdev.h>
#include <linux/smp_lock.h>
#include <linux/device.h>

#define PP_VERSION "ppdev: user-space parallel port driver"
#define CHRDEV "ppdev"

struct pp_struct {
	struct pardevice * pdev;
	wait_queue_head_t irq_wait;
	atomic_t irqc;
	unsigned int flags;
	int irqresponse;
	unsigned char irqctl;
	struct ieee1284_info state;
	struct ieee1284_info saved_state;
	long default_inactivity;
};

/* pp_struct.flags bitfields */
#define PP_CLAIMED    (1<<0)
#define PP_EXCL       (1<<1)

/* Other constants */
#define PP_INTERRUPT_TIMEOUT (10 * HZ) /* 10s */
#define PP_BUFFER_SIZE 1024
#define PARDEVICE_MAX 8

/* ROUND_UP macro from fs/select.c */
#define ROUND_UP(x,y) (((x)+(y)-1)/(y))

static inline void pp_enable_irq (struct pp_struct *pp)
{
	struct parport *port = pp->pdev->port;
	port->ops->enable_irq (port);
}

static ssize_t pp_read (struct file * file, char __user * buf, size_t count,
			loff_t * ppos)
{
	unsigned int minor = iminor(file->f_dentry->d_inode);
	struct pp_struct *pp = file->private_data;
	char * kbuffer;
	ssize_t bytes_read = 0;
	struct parport *pport;
	int mode;

	if (!(pp->flags & PP_CLAIMED)) {
		/* Don't have the port claimed */
		printk (KERN_DEBUG CHRDEV "%x: claim the port first\n",
			minor);
		return -EINVAL;
	}

	/* Trivial case. */
	if (count == 0)
		return 0;

	kbuffer = kmalloc(min_t(size_t, count, PP_BUFFER_SIZE), GFP_KERNEL);
	if (!kbuffer) {
		return -ENOMEM;
	}
	pport = pp->pdev->port;
	mode = pport->ieee1284.mode & ~(IEEE1284_DEVICEID | IEEE1284_ADDR);

	parport_set_timeout (pp->pdev,
			     (file->f_flags & O_NONBLOCK) ?
			     PARPORT_INACTIVITY_O_NONBLOCK :
			     pp->default_inactivity);

	while (bytes_read == 0) {
		ssize_t need = min_t(unsigned long, count, PP_BUFFER_SIZE);

		if (mode == IEEE1284_MODE_EPP) {
			/* various specials for EPP mode */
			int flags = 0;
			size_t (*fn)(struct parport *, void *, size_t, int);

			if (pp->flags & PP_W91284PIC) {
				flags |= PARPORT_W91284PIC;
			}
			if (pp->flags & PP_FASTREAD) {
				flags |= PARPORT_EPP_FAST;
			}
			if (pport->ieee1284.mode & IEEE1284_ADDR) {
				fn = pport->ops->epp_read_addr;
			} else {
				fn = pport->ops->epp_read_data;
			}
			bytes_read = (*fn)(pport, kbuffer, need, flags);
		} else {
			bytes_read = parport_read (pport, kbuffer, need);
		}

		if (bytes_read != 0)
			break;

		if (file->f_flags & O_NONBLOCK) {
			bytes_read = -EAGAIN;
			break;
		}

		if (signal_pending (current)) {
			bytes_read = -ERESTARTSYS;
			break;
		}

		cond_resched();
	}

	parport_set_timeout (pp->pdev, pp->default_inactivity);

	if (bytes_read > 0 && copy_to_user (buf, kbuffer, bytes_read))
		bytes_read = -EFAULT;

	kfree (kbuffer);
	pp_enable_irq (pp);
	return bytes_read;
}

static ssize_t pp_write (struct file * file, const char __user * buf,
			 size_t count, loff_t * ppos)
{
	unsigned int minor = iminor(file->f_dentry->d_inode);
	struct pp_struct *pp = file->private_data;
	char * kbuffer;
	ssize_t bytes_written = 0;
	ssize_t wrote;
	int mode;
	struct parport *pport;

	if (!(pp->flags & PP_CLAIMED)) {
		/* Don't have the port claimed */
		printk (KERN_DEBUG CHRDEV "%x: claim the port first\n",
			minor);
		return -EINVAL;
	}

	kbuffer = kmalloc(min_t(size_t, count, PP_BUFFER_SIZE), GFP_KERNEL);
	if (!kbuffer) {
		return -ENOMEM;
	}
	pport = pp->pdev->port;
	mode = pport->ieee1284.mode & ~(IEEE1284_DEVICEID | IEEE1284_ADDR);

	parport_set_timeout (pp->pdev,
			     (file->f_flags & O_NONBLOCK) ?
			     PARPORT_INACTIVITY_O_NONBLOCK :
			     pp->default_inactivity);

	while (bytes_written < count) {
		ssize_t n = min_t(unsigned long, count - bytes_written, PP_BUFFER_SIZE);

		if (copy_from_user (kbuffer, buf + bytes_written, n)) {
			bytes_written = -EFAULT;
			break;
		}

		if ((pp->flags & PP_FASTWRITE) && (mode == IEEE1284_MODE_EPP)) {
			/* do a fast EPP write */
			if (pport->ieee1284.mode & IEEE1284_ADDR) {
				wrote = pport->ops->epp_write_addr (pport,
					kbuffer, n, PARPORT_EPP_FAST);
			} else {
				wrote = pport->ops->epp_write_data (pport,
					kbuffer, n, PARPORT_EPP_FAST);
			}
		} else {
			wrote = parport_write (pp->pdev->port, kbuffer, n);
		}

		if (wrote <= 0) {
			if (!bytes_written) {
				bytes_written = wrote;
			}
			break;
		}

		bytes_written += wrote;

		if (file->f_flags & O_NONBLOCK) {
			if (!bytes_written)
				bytes_written = -EAGAIN;
			break;
		}

		if (signal_pending (current)) {
			if (!bytes_written) {
				bytes_written = -EINTR;
			}
			break;
		}

		cond_resched();
	}

	parport_set_timeout (pp->pdev, pp->default_inactivity);

	kfree (kbuffer);
	pp_enable_irq (pp);
	return bytes_written;
}

static void pp_irq (int irq, void * private, struct pt_regs * unused)
{
	struct pp_struct * pp = (struct pp_struct *) private;

	if (pp->irqresponse) {
		parport_write_control (pp->pdev->port, pp->irqctl);
		pp->irqresponse = 0;
	}

	atomic_inc (&pp->irqc);
	wake_up_interruptible (&pp->irq_wait);
}

static int register_device (int minor, struct pp_struct *pp)
{
	struct parport *port;
	struct pardevice * pdev = NULL;
	char *name;
	int fl;

	name = kmalloc (strlen (CHRDEV) + 3, GFP_KERNEL);
	if (name == NULL)
		return -ENOMEM;

	sprintf (name, CHRDEV "%x", minor);

	port = parport_find_number (minor);
	if (!port) {
		printk (KERN_WARNING "%s: no associated port!\n", name);
		kfree (name);
		return -ENXIO;
	}

	fl = (pp->flags & PP_EXCL) ? PARPORT_FLAG_EXCL : 0;
	pdev = parport_register_device (port, name, NULL,
					NULL, pp_irq, fl, pp);
	parport_put_port (port);

	if (!pdev) {
		printk (KERN_WARNING "%s: failed to register device!\n", name);
		kfree (name);
		return -ENXIO;
	}

	pp->pdev = pdev;
	printk (KERN_DEBUG "%s: registered pardevice\n", name);
	return 0;
}

static enum ieee1284_phase init_phase (int mode)
{
	switch (mode & ~(IEEE1284_DEVICEID
			 | IEEE1284_ADDR)) {
	case IEEE1284_MODE_NIBBLE:
	case IEEE1284_MODE_BYTE:
		return IEEE1284_PH_REV_IDLE;
	}
	return IEEE1284_PH_FWD_IDLE;
}

static int pp_ioctl(struct inode *inode, struct file *file,
		    unsigned int cmd, unsigned long arg)
{
	unsigned int minor = iminor(inode);
	struct pp_struct *pp = file->private_data;
	struct parport * port;
	void __user *argp = (void __user *)arg;

	/* First handle the cases that don't take arguments. */
	switch (cmd) {
	case PPCLAIM:
	    {
		struct ieee1284_info *info;
		int ret;

		if (pp->flags & PP_CLAIMED) {
			printk (KERN_DEBUG CHRDEV
				"%x: you've already got it!\n", minor);
			return -EINVAL;
		}

		/* Deferred device registration. */
		if (!pp->pdev) {
			int err = register_device (minor, pp);
			if (err) {
				return err;
			}
		}

		ret = parport_claim_or_block (pp->pdev);
		if (ret < 0)
			return ret;

		pp->flags |= PP_CLAIMED;

		/* For interrupt-reporting to work, we need to be
		 * informed of each interrupt. */
		pp_enable_irq (pp);

		/* We may need to fix up the state machine. */
		info = &pp->pdev->port->ieee1284;
		pp->saved_state.mode = info->mode;
		pp->saved_state.phase = info->phase;
		info->mode = pp->state.mode;
		info->phase = pp->state.phase;
		pp->default_inactivity = parport_set_timeout (pp->pdev, 0);
		parport_set_timeout (pp->pdev, pp->default_inactivity);

		return 0;
	    }
	case PPEXCL:
		if (pp->pdev) {
			printk (KERN_DEBUG CHRDEV "%x: too late for PPEXCL; "
				"already registered\n", minor);
			if (pp->flags & PP_EXCL)
				/* But it's not really an error. */
				return 0;
			/* There's no chance of making the driver happy. */
			return -EINVAL;
		}

		/* Just remember to register the device exclusively
		 * when we finally do the registration. */
		pp->flags |= PP_EXCL;
		return 0;
	case PPSETMODE:
	    {
		int mode;
		if (copy_from_user (&mode, argp, sizeof (mode)))
			return -EFAULT;
		/* FIXME: validate mode */
		pp->state.mode = mode;
		pp->state.phase = init_phase (mode);

		if (pp->flags & PP_CLAIMED) {
			pp->pdev->port->ieee1284.mode = mode;
			pp->pdev->port->ieee1284.phase = pp->state.phase;
		}

		return 0;
	    }
	case PPGETMODE:
	    {
		int mode;

		if (pp->flags & PP_CLAIMED) {
			mode = pp->pdev->port->ieee1284.mode;
		} else {
			mode = pp->state.mode;
		}
		if (copy_to_user (argp, &mode, sizeof (mode))) {
			return -EFAULT;
		}
		return 0;
	    }
	case PPSETPHASE:
	    {
		int phase;
		if (copy_from_user (&phase, argp, sizeof (phase))) {
			return -EFAULT;
		}
		/* FIXME: validate phase */
		pp->state.phase = phase;

		if (pp->flags & PP_CLAIMED) {
			pp->pdev->port->ieee1284.phase = phase;
		}

		return 0;
	    }
	case PPGETPHASE:
	    {
		int phase;

		if (pp->flags & PP_CLAIMED) {
			phase = pp->pdev->port->ieee1284.phase;
		} else {
			phase = pp->state.phase;
		}
		if (copy_to_user (argp, &phase, sizeof (phase))) {
			return -EFAULT;
		}
		return 0;
	    }
	case PPGETMODES:
	    {
		unsigned int modes;

		port = parport_find_number (minor);
		if (!port)
			return -ENODEV;

		modes = port->modes;
		if (copy_to_user (argp, &modes, sizeof (modes))) {
			return -EFAULT;
		}
		return 0;
	    }
	case PPSETFLAGS:
	    {
		int uflags;

		if (copy_from_user (&uflags, argp, sizeof (uflags))) {
			return -EFAULT;
		}
		pp->flags &= ~PP_FLAGMASK;
		pp->flags |= (uflags & PP_FLAGMASK);
		return 0;
	    }
	case PPGETFLAGS:
	    {
		int uflags;

		uflags = pp->flags & PP_FLAGMASK;
		if (copy_to_user (argp, &uflags, sizeof (uflags))) {
			return -EFAULT;
		}
		return 0;
	    }
	}	/* end switch() */

	/* Everything else requires the port to be claimed, so check
	 * that now. */
	if ((pp->flags & PP_CLAIMED) == 0) {
		printk (KERN_DEBUG CHRDEV "%x: claim the port first\n",
			minor);
		return -EINVAL;
	}

	port = pp->pdev->port;
	switch (cmd) {
		struct ieee1284_info *info;
		unsigned char reg;
		unsigned char mask;
		int mode;
		int ret;
		struct timeval par_timeout;
		long to_jiffies;

	case PPRSTATUS:
		reg = parport_read_status (port);
		if (copy_to_user (argp, &reg, sizeof (reg)))
			return -EFAULT;
		return 0;
	case PPRDATA:
		reg = parport_read_data (port);
		if (copy_to_user (argp, &reg, sizeof (reg)))
			return -EFAULT;
		return 0;
	case PPRCONTROL:
		reg = parport_read_control (port);
		if (copy_to_user (argp, &reg, sizeof (reg)))
			return -EFAULT;
		return 0;
	case PPYIELD:
		parport_yield_blocking (pp->pdev);
		return 0;

	case PPRELEASE:
		/* Save the state machine's state. */
		info = &pp->pdev->port->ieee1284;
		pp->state.mode = info->mode;
		pp->state.phase = info->phase;
		info->mode = pp->saved_state.mode;
		info->phase = pp->saved_state.phase;
		parport_release (pp->pdev);
		pp->flags &= ~PP_CLAIMED;
		return 0;

	case PPWCONTROL:
		if (copy_from_user (&reg, argp, sizeof (reg)))
			return -EFAULT;
		parport_write_control (port, reg);
		return 0;

	case PPWDATA:
		if (copy_from_user (&reg, argp, sizeof (reg)))
			return -EFAULT;
		parport_write_data (port, reg);
		return 0;

	case PPFCONTROL:
		if (copy_from_user (&mask, argp,
				    sizeof (mask)))
			return -EFAULT;
		if (copy_from_user (&reg, 1 + (unsigned char __user *) arg,
				    sizeof (reg)))
			return -EFAULT;
		parport_frob_control (port, mask, reg);
		return 0;

	case PPDATADIR:
		if (copy_from_user (&mode, argp, sizeof (mode)))
			return -EFAULT;
		if (mode)
			port->ops->data_reverse (port);
		else
			port->ops->data_forward (port);
		return 0;

	case PPNEGOT:
		if (copy_from_user (&mode, argp, sizeof (mode)))
			return -EFAULT;
		switch ((ret = parport_negotiate (port, mode))) {
		case 0: break;
		case -1: /* handshake failed, peripheral not IEEE 1284 */
			ret = -EIO;
			break;
		case 1:  /* handshake succeeded, peripheral rejected mode */
			ret = -ENXIO;
			break;
		}
		pp_enable_irq (pp);
		return ret;

	case PPWCTLONIRQ:
		if (copy_from_user (&reg, argp, sizeof (reg)))
			return -EFAULT;

		/* Remember what to set the control lines to, for next
		 * time we get an interrupt. */
		pp->irqctl = reg;
		pp->irqresponse = 1;
		return 0;

	case PPCLRIRQ:
		ret = atomic_read (&pp->irqc);
		if (copy_to_user (argp, &ret, sizeof (ret)))
			return -EFAULT;
		atomic_sub (ret, &pp->irqc);
		return 0;

	case PPSETTIME:
		if (copy_from_user (&par_timeout, argp, sizeof(struct timeval))) {
			return -EFAULT;
		}
		/* Convert to jiffies, place in pp->pdev->timeout */
		if ((par_timeout.tv_sec < 0) || (par_timeout.tv_usec < 0)) {
			return -EINVAL;
		}
		to_jiffies = ROUND_UP(par_timeout.tv_usec, 1000000/HZ);
		to_jiffies += par_timeout.tv_sec * (long)HZ;
		if (to_jiffies <= 0) {
			return -EINVAL;
		}
		pp->pdev->timeout = to_jiffies;
		return 0;

	case PPGETTIME:
		to_jiffies = pp->pdev->timeout;
		par_timeout.tv_sec = to_jiffies / HZ;
		par_timeout.tv_usec = (to_jiffies % (long)HZ) * (1000000/HZ);
		if (copy_to_user (argp, &par_timeout, sizeof(struct timeval)))
			return -EFAULT;
		return 0;

	default:
		printk (KERN_DEBUG CHRDEV "%x: What? (cmd=0x%x)\n", minor,
			cmd);
		return -EINVAL;
	}

	/* Keep the compiler happy */
	return 0;
}

static int pp_open (struct inode * inode, struct file * file)
{
	unsigned int minor = iminor(inode);
	struct pp_struct *pp;

	if (minor >= PARPORT_MAX)
		return -ENXIO;

	pp = kmalloc (sizeof (struct pp_struct), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;

	pp->state.mode = IEEE1284_MODE_COMPAT;
	pp->state.phase = init_phase (pp->state.mode);
	pp->flags = 0;
	pp->irqresponse = 0;
	atomic_set (&pp->irqc, 0);
	init_waitqueue_head (&pp->irq_wait);

	/* Defer the actual device registration until the first claim.
	 * That way, we know whether or not the driver wants to have
	 * exclusive access to the port (PPEXCL).
	 */
	pp->pdev = NULL;
	file->private_data = pp;

	return 0;
}

static int pp_release (struct inode * inode, struct file * file)
{
	unsigned int minor = iminor(inode);
	struct pp_struct *pp = file->private_data;
	int compat_negot;

	compat_negot = 0;
	if (!(pp->flags & PP_CLAIMED) && pp->pdev &&
	    (pp->state.mode != IEEE1284_MODE_COMPAT)) {
	    	struct ieee1284_info *info;

		/* parport released, but not in compatibility mode */
		parport_claim_or_block (pp->pdev);
		pp->flags |= PP_CLAIMED;
		info = &pp->pdev->port->ieee1284;
		pp->saved_state.mode = info->mode;
		pp->saved_state.phase = info->phase;
		info->mode = pp->state.mode;
		info->phase = pp->state.phase;
		compat_negot = 1;
	} else if ((pp->flags & PP_CLAIMED) && pp->pdev &&
	    (pp->pdev->port->ieee1284.mode != IEEE1284_MODE_COMPAT)) {
		compat_negot = 2;
	}
	if (compat_negot) {
		parport_negotiate (pp->pdev->port, IEEE1284_MODE_COMPAT);
		printk (KERN_DEBUG CHRDEV
			"%x: negotiated back to compatibility mode because "
			"user-space forgot\n", minor);
	}

	if (pp->flags & PP_CLAIMED) {
		struct ieee1284_info *info;

		info = &pp->pdev->port->ieee1284;
		pp->state.mode = info->mode;
		pp->state.phase = info->phase;
		info->mode = pp->saved_state.mode;
		info->phase = pp->saved_state.phase;
		parport_release (pp->pdev);
		if (compat_negot != 1) {
			printk (KERN_DEBUG CHRDEV "%x: released pardevice "
				"because user-space forgot\n", minor);
		}
	}

	if (pp->pdev) {
		const char *name = pp->pdev->name;
		parport_unregister_device (pp->pdev);
		kfree (name);
		pp->pdev = NULL;
		printk (KERN_DEBUG CHRDEV "%x: unregistered pardevice\n",
			minor);
	}

	kfree (pp);

	return 0;
}

/* No kernel lock held - fine */
static unsigned int pp_poll (struct file * file, poll_table * wait)
{
	struct pp_struct *pp = file->private_data;
	unsigned int mask = 0;

	poll_wait (file, &pp->irq_wait, wait);
	if (atomic_read (&pp->irqc))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static struct class *ppdev_class;

static struct file_operations pp_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= pp_read,
	.write		= pp_write,
	.poll		= pp_poll,
	.ioctl		= pp_ioctl,
	.open		= pp_open,
	.release	= pp_release,
};

static void pp_attach(struct parport *port)
{
	class_device_create(ppdev_class, MKDEV(PP_MAJOR, port->number),
			NULL, "parport%d", port->number);
}

static void pp_detach(struct parport *port)
{
	class_device_destroy(ppdev_class, MKDEV(PP_MAJOR, port->number));
}

static struct parport_driver pp_driver = {
	.name		= CHRDEV,
	.attach		= pp_attach,
	.detach		= pp_detach,
};

static int __init ppdev_init (void)
{
	int i, err = 0;

	if (register_chrdev (PP_MAJOR, CHRDEV, &pp_fops)) {
		printk (KERN_WARNING CHRDEV ": unable to get major %d\n",
			PP_MAJOR);
		return -EIO;
	}
	ppdev_class = class_create(THIS_MODULE, CHRDEV);
	if (IS_ERR(ppdev_class)) {
		err = PTR_ERR(ppdev_class);
		goto out_chrdev;
	}
	devfs_mk_dir("parports");
	for (i = 0; i < PARPORT_MAX; i++) {
		devfs_mk_cdev(MKDEV(PP_MAJOR, i),
				S_IFCHR | S_IRUGO | S_IWUGO, "parports/%d", i);
	}
	if (parport_register_driver(&pp_driver)) {
		printk (KERN_WARNING CHRDEV ": unable to register with parport\n");
		goto out_class;
	}

	printk (KERN_INFO PP_VERSION "\n");
	goto out;

out_class:
	for (i = 0; i < PARPORT_MAX; i++)
		devfs_remove("parports/%d", i);
	devfs_remove("parports");
	class_destroy(ppdev_class);
out_chrdev:
	unregister_chrdev(PP_MAJOR, CHRDEV);
out:
	return err;
}

static void __exit ppdev_cleanup (void)
{
	int i;
	/* Clean up all parport stuff */
	for (i = 0; i < PARPORT_MAX; i++)
		devfs_remove("parports/%d", i);
	parport_unregister_driver(&pp_driver);
	devfs_remove("parports");
	class_destroy(ppdev_class);
	unregister_chrdev (PP_MAJOR, CHRDEV);
}

module_init(ppdev_init);
module_exit(ppdev_cleanup);

MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV_MAJOR(PP_MAJOR);
