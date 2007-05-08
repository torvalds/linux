/*
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/sched.h"
#include "linux/slab.h"
#include "linux/list.h"
#include "linux/kd.h"
#include "linux/interrupt.h"
#include "asm/uaccess.h"
#include "chan_kern.h"
#include "irq_user.h"
#include "line.h"
#include "kern.h"
#include "kern_util.h"
#include "os.h"
#include "irq_kern.h"

#define LINE_BUFSIZE 4096

static irqreturn_t line_interrupt(int irq, void *data)
{
	struct chan *chan = data;
	struct line *line = chan->line;
	struct tty_struct *tty = line->tty;

	if (line)
		chan_interrupt(&line->chan_list, &line->task, tty, irq);
	return IRQ_HANDLED;
}

static void line_timer_cb(struct work_struct *work)
{
	struct line *line = container_of(work, struct line, task.work);

	if(!line->throttled)
		chan_interrupt(&line->chan_list, &line->task, line->tty,
			       line->driver->read_irq);
}

/* Returns the free space inside the ring buffer of this line.
 *
 * Should be called while holding line->lock (this does not modify datas).
 */
static int write_room(struct line *line)
{
	int n;

	if (line->buffer == NULL)
		return LINE_BUFSIZE - 1;

	/* This is for the case where the buffer is wrapped! */
	n = line->head - line->tail;

	if (n <= 0)
		n = LINE_BUFSIZE + n; /* The other case */
	return n - 1;
}

int line_write_room(struct tty_struct *tty)
{
	struct line *line = tty->driver_data;
	unsigned long flags;
	int room;

	if (tty->stopped)
		return 0;

	spin_lock_irqsave(&line->lock, flags);
	room = write_room(line);
	spin_unlock_irqrestore(&line->lock, flags);

	/*XXX: Warning to remove */
	if (0 == room)
		printk(KERN_DEBUG "%s: %s: no room left in buffer\n",
		       __FUNCTION__,tty->name);
	return room;
}

int line_chars_in_buffer(struct tty_struct *tty)
{
	struct line *line = tty->driver_data;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&line->lock, flags);

	/*write_room subtracts 1 for the needed NULL, so we readd it.*/
	ret = LINE_BUFSIZE - (write_room(line) + 1);
	spin_unlock_irqrestore(&line->lock, flags);

	return ret;
}

/*
 * This copies the content of buf into the circular buffer associated with
 * this line.
 * The return value is the number of characters actually copied, i.e. the ones
 * for which there was space: this function is not supposed to ever flush out
 * the circular buffer.
 *
 * Must be called while holding line->lock!
 */
static int buffer_data(struct line *line, const char *buf, int len)
{
	int end, room;

	if(line->buffer == NULL){
		line->buffer = kmalloc(LINE_BUFSIZE, GFP_ATOMIC);
		if (line->buffer == NULL) {
			printk("buffer_data - atomic allocation failed\n");
			return(0);
		}
		line->head = line->buffer;
		line->tail = line->buffer;
	}

	room = write_room(line);
	len = (len > room) ? room : len;

	end = line->buffer + LINE_BUFSIZE - line->tail;

	if (len < end){
		memcpy(line->tail, buf, len);
		line->tail += len;
	}
	else {
		/* The circular buffer is wrapping */
		memcpy(line->tail, buf, end);
		buf += end;
		memcpy(line->buffer, buf, len - end);
		line->tail = line->buffer + len - end;
	}

	return len;
}

/*
 * Flushes the ring buffer to the output channels. That is, write_chan is
 * called, passing it line->head as buffer, and an appropriate count.
 *
 * On exit, returns 1 when the buffer is empty,
 * 0 when the buffer is not empty on exit,
 * and -errno when an error occurred.
 *
 * Must be called while holding line->lock!*/
static int flush_buffer(struct line *line)
{
	int n, count;

	if ((line->buffer == NULL) || (line->head == line->tail))
		return 1;

	if (line->tail < line->head) {
		/* line->buffer + LINE_BUFSIZE is the end of the buffer! */
		count = line->buffer + LINE_BUFSIZE - line->head;

		n = write_chan(&line->chan_list, line->head, count,
			       line->driver->write_irq);
		if (n < 0)
			return n;
		if (n == count) {
			/* We have flushed from ->head to buffer end, now we
			 * must flush only from the beginning to ->tail.*/
			line->head = line->buffer;
		} else {
			line->head += n;
			return 0;
		}
	}

	count = line->tail - line->head;
	n = write_chan(&line->chan_list, line->head, count,
		       line->driver->write_irq);

	if(n < 0)
		return n;

	line->head += n;
	return line->head == line->tail;
}

void line_flush_buffer(struct tty_struct *tty)
{
	struct line *line = tty->driver_data;
	unsigned long flags;
	int err;

	/*XXX: copied from line_write, verify if it is correct!*/
	if(tty->stopped)
		return;

	spin_lock_irqsave(&line->lock, flags);
	err = flush_buffer(line);
	/*if (err == 1)
		err = 0;*/
	spin_unlock_irqrestore(&line->lock, flags);
	//return err;
}

/* We map both ->flush_chars and ->put_char (which go in pair) onto ->flush_buffer
 * and ->write. Hope it's not that bad.*/
void line_flush_chars(struct tty_struct *tty)
{
	line_flush_buffer(tty);
}

void line_put_char(struct tty_struct *tty, unsigned char ch)
{
	line_write(tty, &ch, sizeof(ch));
}

int line_write(struct tty_struct *tty, const unsigned char *buf, int len)
{
	struct line *line = tty->driver_data;
	unsigned long flags;
	int n, err, ret = 0;

	if(tty->stopped)
		return 0;

	spin_lock_irqsave(&line->lock, flags);
	if (line->head != line->tail) {
		ret = buffer_data(line, buf, len);
		err = flush_buffer(line);
		if (err <= 0 && (err != -EAGAIN || !ret))
			ret = err;
	} else {
		n = write_chan(&line->chan_list, buf, len,
			       line->driver->write_irq);
		if (n < 0) {
			ret = n;
			goto out_up;
		}

		len -= n;
		ret += n;
		if (len > 0)
			ret += buffer_data(line, buf + n, len);
	}
out_up:
	spin_unlock_irqrestore(&line->lock, flags);
	return ret;
}

void line_set_termios(struct tty_struct *tty, struct ktermios * old)
{
	/* nothing */
}

static const struct {
	int  cmd;
	char *level;
	char *name;
} tty_ioctls[] = {
	/* don't print these, they flood the log ... */
	{ TCGETS,      NULL,       "TCGETS"      },
        { TCSETS,      NULL,       "TCSETS"      },
        { TCSETSW,     NULL,       "TCSETSW"     },
        { TCFLSH,      NULL,       "TCFLSH"      },
        { TCSBRK,      NULL,       "TCSBRK"      },

	/* general tty stuff */
        { TCSETSF,     KERN_DEBUG, "TCSETSF"     },
        { TCGETA,      KERN_DEBUG, "TCGETA"      },
        { TIOCMGET,    KERN_DEBUG, "TIOCMGET"    },
        { TCSBRKP,     KERN_DEBUG, "TCSBRKP"     },
        { TIOCMSET,    KERN_DEBUG, "TIOCMSET"    },

	/* linux-specific ones */
	{ TIOCLINUX,   KERN_INFO,  "TIOCLINUX"   },
	{ KDGKBMODE,   KERN_INFO,  "KDGKBMODE"   },
	{ KDGKBTYPE,   KERN_INFO,  "KDGKBTYPE"   },
	{ KDSIGACCEPT, KERN_INFO,  "KDSIGACCEPT" },
};

int line_ioctl(struct tty_struct *tty, struct file * file,
	       unsigned int cmd, unsigned long arg)
{
	int ret;
	int i;

	ret = 0;
	switch(cmd) {
#ifdef TIOCGETP
	case TIOCGETP:
	case TIOCSETP:
	case TIOCSETN:
#endif
#ifdef TIOCGETC
	case TIOCGETC:
	case TIOCSETC:
#endif
#ifdef TIOCGLTC
	case TIOCGLTC:
	case TIOCSLTC:
#endif
	case TCGETS:
	case TCSETSF:
	case TCSETSW:
	case TCSETS:
	case TCGETA:
	case TCSETAF:
	case TCSETAW:
	case TCSETA:
	case TCXONC:
	case TCFLSH:
	case TIOCOUTQ:
	case TIOCINQ:
	case TIOCGLCKTRMIOS:
	case TIOCSLCKTRMIOS:
	case TIOCPKT:
	case TIOCGSOFTCAR:
	case TIOCSSOFTCAR:
		return -ENOIOCTLCMD;
#if 0
	case TCwhatever:
		/* do something */
		break;
#endif
	default:
		for (i = 0; i < ARRAY_SIZE(tty_ioctls); i++)
			if (cmd == tty_ioctls[i].cmd)
				break;
		if (i < ARRAY_SIZE(tty_ioctls)) {
			if (NULL != tty_ioctls[i].level)
				printk("%s%s: %s: ioctl %s called\n",
				       tty_ioctls[i].level, __FUNCTION__,
				       tty->name, tty_ioctls[i].name);
		} else {
			printk(KERN_ERR "%s: %s: unknown ioctl: 0x%x\n",
			       __FUNCTION__, tty->name, cmd);
		}
		ret = -ENOIOCTLCMD;
		break;
	}
	return ret;
}

void line_throttle(struct tty_struct *tty)
{
	struct line *line = tty->driver_data;

	deactivate_chan(&line->chan_list, line->driver->read_irq);
	line->throttled = 1;
}

void line_unthrottle(struct tty_struct *tty)
{
	struct line *line = tty->driver_data;

	line->throttled = 0;
	chan_interrupt(&line->chan_list, &line->task, tty,
		       line->driver->read_irq);

	/* Maybe there is enough stuff pending that calling the interrupt
	 * throttles us again.  In this case, line->throttled will be 1
	 * again and we shouldn't turn the interrupt back on.
	 */
	if(!line->throttled)
		reactivate_chan(&line->chan_list, line->driver->read_irq);
}

static irqreturn_t line_write_interrupt(int irq, void *data)
{
	struct chan *chan = data;
	struct line *line = chan->line;
	struct tty_struct *tty = line->tty;
	int err;

	/* Interrupts are disabled here because we registered the interrupt with
	 * IRQF_DISABLED (see line_setup_irq).*/

	spin_lock(&line->lock);
	err = flush_buffer(line);
	if (err == 0) {
		return IRQ_NONE;
	} else if(err < 0) {
		line->head = line->buffer;
		line->tail = line->buffer;
	}
	spin_unlock(&line->lock);

	if(tty == NULL)
		return IRQ_NONE;

	if (test_bit(TTY_DO_WRITE_WAKEUP, &tty->flags) &&
	   (tty->ldisc.write_wakeup != NULL))
		(tty->ldisc.write_wakeup)(tty);

	/* BLOCKING mode
	 * In blocking mode, everything sleeps on tty->write_wait.
	 * Sleeping in the console driver would break non-blocking
	 * writes.
	 */

	if (waitqueue_active(&tty->write_wait))
		wake_up_interruptible(&tty->write_wait);
	return IRQ_HANDLED;
}

int line_setup_irq(int fd, int input, int output, struct line *line, void *data)
{
	const struct line_driver *driver = line->driver;
	int err = 0, flags = IRQF_DISABLED | IRQF_SHARED | IRQF_SAMPLE_RANDOM;

	if (input)
		err = um_request_irq(driver->read_irq, fd, IRQ_READ,
				       line_interrupt, flags,
				       driver->read_irq_name, data);
	if (err)
		return err;
	if (output)
		err = um_request_irq(driver->write_irq, fd, IRQ_WRITE,
					line_write_interrupt, flags,
					driver->write_irq_name, data);
	line->have_irq = 1;
	return err;
}

/* Normally, a driver like this can rely mostly on the tty layer
 * locking, particularly when it comes to the driver structure.
 * However, in this case, mconsole requests can come in "from the
 * side", and race with opens and closes.
 *
 * mconsole config requests will want to be sure the device isn't in
 * use, and get_config, open, and close will want a stable
 * configuration.  The checking and modification of the configuration
 * is done under a spinlock.  Checking whether the device is in use is
 * line->tty->count > 1, also under the spinlock.
 *
 * tty->count serves to decide whether the device should be enabled or
 * disabled on the host.  If it's equal to 1, then we are doing the
 * first open or last close.  Otherwise, open and close just return.
 */

int line_open(struct line *lines, struct tty_struct *tty)
{
	struct line *line = &lines[tty->index];
	int err = -ENODEV;

	spin_lock(&line->count_lock);
	if(!line->valid)
		goto out_unlock;

	err = 0;
	if(tty->count > 1)
		goto out_unlock;

	spin_unlock(&line->count_lock);

	tty->driver_data = line;
	line->tty = tty;

	enable_chan(line);
	INIT_DELAYED_WORK(&line->task, line_timer_cb);

	if(!line->sigio){
		chan_enable_winch(&line->chan_list, tty);
		line->sigio = 1;
	}

	chan_window_size(&line->chan_list, &tty->winsize.ws_row,
			 &tty->winsize.ws_col);

	return err;

out_unlock:
	spin_unlock(&line->count_lock);
	return err;
}

static void unregister_winch(struct tty_struct *tty);

void line_close(struct tty_struct *tty, struct file * filp)
{
	struct line *line = tty->driver_data;

	/* If line_open fails (and tty->driver_data is never set),
	 * tty_open will call line_close.  So just return in this case.
	 */
	if(line == NULL)
		return;

	/* We ignore the error anyway! */
	flush_buffer(line);

	spin_lock(&line->count_lock);
	if(!line->valid)
		goto out_unlock;

	if(tty->count > 1)
		goto out_unlock;

	spin_unlock(&line->count_lock);

	line->tty = NULL;
	tty->driver_data = NULL;

	if(line->sigio){
		unregister_winch(tty);
		line->sigio = 0;
        }

	return;

out_unlock:
	spin_unlock(&line->count_lock);
}

void close_lines(struct line *lines, int nlines)
{
	int i;

	for(i = 0; i < nlines; i++)
		close_chan(&lines[i].chan_list, 0);
}

static int setup_one_line(struct line *lines, int n, char *init, int init_prio,
			  char **error_out)
{
	struct line *line = &lines[n];
	int err = -EINVAL;

	spin_lock(&line->count_lock);

	if(line->tty != NULL){
		*error_out = "Device is already open";
		goto out;
	}

	if (line->init_pri <= init_prio){
		line->init_pri = init_prio;
		if (!strcmp(init, "none"))
			line->valid = 0;
		else {
			line->init_str = init;
			line->valid = 1;
		}
	}
	err = 0;
out:
	spin_unlock(&line->count_lock);
	return err;
}

/* Common setup code for both startup command line and mconsole initialization.
 * @lines contains the array (of size @num) to modify;
 * @init is the setup string;
 * @error_out is an error string in the case of failure;
 */

int line_setup(struct line *lines, unsigned int num, char *init,
	       char **error_out)
{
	int i, n, err;
	char *end;

	if(*init == '=') {
		/* We said con=/ssl= instead of con#=, so we are configuring all
		 * consoles at once.*/
		n = -1;
	}
	else {
		n = simple_strtoul(init, &end, 0);
		if(*end != '='){
			*error_out = "Couldn't parse device number";
			return -EINVAL;
		}
		init = end;
	}
	init++;

	if (n >= (signed int) num) {
		*error_out = "Device number out of range";
		return -EINVAL;
	}
	else if (n >= 0){
		err = setup_one_line(lines, n, init, INIT_ONE, error_out);
		if(err)
			return err;
	}
	else {
		for(i = 0; i < num; i++){
			err = setup_one_line(lines, i, init, INIT_ALL,
					     error_out);
			if(err)
				return err;
		}
	}
	return n == -1 ? num : n;
}

int line_config(struct line *lines, unsigned int num, char *str,
		const struct chan_opts *opts, char **error_out)
{
	struct line *line;
	char *new;
	int n;

	if(*str == '='){
		*error_out = "Can't configure all devices from mconsole";
		return -EINVAL;
	}

	new = kstrdup(str, GFP_KERNEL);
	if(new == NULL){
		*error_out = "Failed to allocate memory";
		return -ENOMEM;
	}
	n = line_setup(lines, num, new, error_out);
	if(n < 0)
		return n;

	line = &lines[n];
	return parse_chan_pair(line->init_str, line, n, opts, error_out);
}

int line_get_config(char *name, struct line *lines, unsigned int num, char *str,
		    int size, char **error_out)
{
	struct line *line;
	char *end;
	int dev, n = 0;

	dev = simple_strtoul(name, &end, 0);
	if((*end != '\0') || (end == name)){
		*error_out = "line_get_config failed to parse device number";
		return 0;
	}

	if((dev < 0) || (dev >= num)){
		*error_out = "device number out of range";
		return 0;
	}

	line = &lines[dev];

	spin_lock(&line->count_lock);
	if(!line->valid)
		CONFIG_CHUNK(str, size, n, "none", 1);
	else if(line->tty == NULL)
		CONFIG_CHUNK(str, size, n, line->init_str, 1);
	else n = chan_config_string(&line->chan_list, str, size, error_out);
	spin_unlock(&line->count_lock);

	return n;
}

int line_id(char **str, int *start_out, int *end_out)
{
	char *end;
        int n;

	n = simple_strtoul(*str, &end, 0);
	if((*end != '\0') || (end == *str))
                return -1;

        *str = end;
        *start_out = n;
        *end_out = n;
        return n;
}

int line_remove(struct line *lines, unsigned int num, int n, char **error_out)
{
	int err;
	char config[sizeof("conxxxx=none\0")];

	sprintf(config, "%d=none", n);
	err = line_setup(lines, num, config, error_out);
	if(err >= 0)
		err = 0;
	return err;
}

struct tty_driver *register_lines(struct line_driver *line_driver,
				  const struct tty_operations *ops,
				  struct line *lines, int nlines)
{
	int i;
	struct tty_driver *driver = alloc_tty_driver(nlines);

	if (!driver)
		return NULL;

	driver->driver_name = line_driver->name;
	driver->name = line_driver->device_name;
	driver->major = line_driver->major;
	driver->minor_start = line_driver->minor_start;
	driver->type = line_driver->type;
	driver->subtype = line_driver->subtype;
	driver->flags = TTY_DRIVER_REAL_RAW;
	driver->init_termios = tty_std_termios;
	tty_set_operations(driver, ops);

	if (tty_register_driver(driver)) {
		printk("%s: can't register %s driver\n",
		       __FUNCTION__,line_driver->name);
		put_tty_driver(driver);
		return NULL;
	}

	for(i = 0; i < nlines; i++){
		if(!lines[i].valid)
			tty_unregister_device(driver, i);
	}

	mconsole_register_dev(&line_driver->mc);
	return driver;
}

static DEFINE_SPINLOCK(winch_handler_lock);
static LIST_HEAD(winch_handlers);

void lines_init(struct line *lines, int nlines, struct chan_opts *opts)
{
	struct line *line;
	char *error;
	int i;

	for(i = 0; i < nlines; i++){
		line = &lines[i];
		INIT_LIST_HEAD(&line->chan_list);

		if(line->init_str == NULL)
			continue;

		line->init_str = kstrdup(line->init_str, GFP_KERNEL);
		if(line->init_str == NULL)
			printk("lines_init - kstrdup returned NULL\n");

		if(parse_chan_pair(line->init_str, line, i, opts, &error)){
			printk("parse_chan_pair failed for device %d : %s\n",
			       i, error);
			line->valid = 0;
		}
	}
}

struct winch {
	struct list_head list;
	int fd;
	int tty_fd;
	int pid;
	struct tty_struct *tty;
};

static irqreturn_t winch_interrupt(int irq, void *data)
{
	struct winch *winch = data;
	struct tty_struct *tty;
	struct line *line;
	int err;
	char c;

	if(winch->fd != -1){
		err = generic_read(winch->fd, &c, NULL);
		if(err < 0){
			if(err != -EAGAIN){
				printk("winch_interrupt : read failed, "
				       "errno = %d\n", -err);
				printk("fd %d is losing SIGWINCH support\n",
				       winch->tty_fd);
				return IRQ_HANDLED;
			}
			goto out;
		}
	}
	tty  = winch->tty;
	if (tty != NULL) {
		line = tty->driver_data;
		chan_window_size(&line->chan_list, &tty->winsize.ws_row,
				 &tty->winsize.ws_col);
		kill_pgrp(tty->pgrp, SIGWINCH, 1);
	}
 out:
	if(winch->fd != -1)
		reactivate_fd(winch->fd, WINCH_IRQ);
	return IRQ_HANDLED;
}

void register_winch_irq(int fd, int tty_fd, int pid, struct tty_struct *tty)
{
	struct winch *winch;

	winch = kmalloc(sizeof(*winch), GFP_KERNEL);
	if (winch == NULL) {
		printk("register_winch_irq - kmalloc failed\n");
		return;
	}

	*winch = ((struct winch) { .list  	= LIST_HEAD_INIT(winch->list),
				   .fd  	= fd,
				   .tty_fd 	= tty_fd,
				   .pid  	= pid,
				   .tty 	= tty });

	spin_lock(&winch_handler_lock);
	list_add(&winch->list, &winch_handlers);
	spin_unlock(&winch_handler_lock);

	if(um_request_irq(WINCH_IRQ, fd, IRQ_READ, winch_interrupt,
			  IRQF_DISABLED | IRQF_SHARED | IRQF_SAMPLE_RANDOM,
			  "winch", winch) < 0)
		printk("register_winch_irq - failed to register IRQ\n");
}

static void free_winch(struct winch *winch)
{
	list_del(&winch->list);

	if(winch->pid != -1)
		os_kill_process(winch->pid, 1);
	if(winch->fd != -1)
		os_close_file(winch->fd);

	free_irq(WINCH_IRQ, winch);
	kfree(winch);
}

static void unregister_winch(struct tty_struct *tty)
{
	struct list_head *ele;
	struct winch *winch;

	spin_lock(&winch_handler_lock);

	list_for_each(ele, &winch_handlers){
		winch = list_entry(ele, struct winch, list);
                if(winch->tty == tty){
			free_winch(winch);
			break;
                }
        }
	spin_unlock(&winch_handler_lock);
}

static void winch_cleanup(void)
{
	struct list_head *ele, *next;
	struct winch *winch;

	spin_lock(&winch_handler_lock);

	list_for_each_safe(ele, next, &winch_handlers){
		winch = list_entry(ele, struct winch, list);
		free_winch(winch);
	}

	spin_unlock(&winch_handler_lock);
}
__uml_exitcall(winch_cleanup);

char *add_xterm_umid(char *base)
{
	char *umid, *title;
	int len;

	umid = get_umid();
	if(*umid == '\0')
		return base;

	len = strlen(base) + strlen(" ()") + strlen(umid) + 1;
	title = kmalloc(len, GFP_KERNEL);
	if(title == NULL){
		printk("Failed to allocate buffer for xterm title\n");
		return base;
	}

	snprintf(title, len, "%s (%s)", base, umid);
	return title;
}
