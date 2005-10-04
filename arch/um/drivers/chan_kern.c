/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <linux/tty_flip.h>
#include <asm/irq.h>
#include "chan_kern.h"
#include "user_util.h"
#include "kern.h"
#include "irq_user.h"
#include "sigio.h"
#include "line.h"
#include "os.h"

/* XXX: could well be moved to somewhere else, if needed. */
static int my_printf(const char * fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

static int my_printf(const char * fmt, ...)
{
	/* Yes, can be called on atomic context.*/
	char *buf = kmalloc(4096, GFP_ATOMIC);
	va_list args;
	int r;

	if (!buf) {
		/* We print directly fmt.
		 * Yes, yes, yes, feel free to complain. */
		r = strlen(fmt);
	} else {
		va_start(args, fmt);
		r = vsprintf(buf, fmt, args);
		va_end(args);
		fmt = buf;
	}

	if (r)
		r = os_write_file(1, fmt, r);
	return r;

}

#ifdef CONFIG_NOCONFIG_CHAN
/* Despite its name, there's no added trailing newline. */
static int my_puts(const char * buf)
{
	return os_write_file(1, buf, strlen(buf));
}

static void *not_configged_init(char *str, int device, struct chan_opts *opts)
{
	my_puts("Using a channel type which is configured out of "
	       "UML\n");
	return(NULL);
}

static int not_configged_open(int input, int output, int primary, void *data,
			      char **dev_out)
{
	my_puts("Using a channel type which is configured out of "
	       "UML\n");
	return(-ENODEV);
}

static void not_configged_close(int fd, void *data)
{
	my_puts("Using a channel type which is configured out of "
	       "UML\n");
}

static int not_configged_read(int fd, char *c_out, void *data)
{
	my_puts("Using a channel type which is configured out of "
	       "UML\n");
	return(-EIO);
}

static int not_configged_write(int fd, const char *buf, int len, void *data)
{
	my_puts("Using a channel type which is configured out of "
	       "UML\n");
	return(-EIO);
}

static int not_configged_console_write(int fd, const char *buf, int len,
				       void *data)
{
	my_puts("Using a channel type which is configured out of "
	       "UML\n");
	return(-EIO);
}

static int not_configged_window_size(int fd, void *data, unsigned short *rows,
				     unsigned short *cols)
{
	my_puts("Using a channel type which is configured out of "
	       "UML\n");
	return(-ENODEV);
}

static void not_configged_free(void *data)
{
	my_puts("Using a channel type which is configured out of "
	       "UML\n");
}

static struct chan_ops not_configged_ops = {
	.init		= not_configged_init,
	.open		= not_configged_open,
	.close		= not_configged_close,
	.read		= not_configged_read,
	.write		= not_configged_write,
	.console_write	= not_configged_console_write,
	.window_size	= not_configged_window_size,
	.free		= not_configged_free,
	.winch		= 0,
};
#endif /* CONFIG_NOCONFIG_CHAN */

void generic_close(int fd, void *unused)
{
	os_close_file(fd);
}

int generic_read(int fd, char *c_out, void *unused)
{
	int n;

	n = os_read_file(fd, c_out, sizeof(*c_out));

	if(n == -EAGAIN)
		return(0);
	else if(n == 0)
		return(-EIO);
	return(n);
}

/* XXX Trivial wrapper around os_write_file */

int generic_write(int fd, const char *buf, int n, void *unused)
{
	return(os_write_file(fd, buf, n));
}

int generic_window_size(int fd, void *unused, unsigned short *rows_out,
			unsigned short *cols_out)
{
	int rows, cols;
	int ret;

	ret = os_window_size(fd, &rows, &cols);
	if(ret < 0)
		return(ret);

	ret = ((*rows_out != rows) || (*cols_out != cols));

	*rows_out = rows;
	*cols_out = cols;

	return(ret);
}

void generic_free(void *data)
{
	kfree(data);
}

static void tty_receive_char(struct tty_struct *tty, char ch)
{
	if(tty == NULL) return;

	if(I_IXON(tty) && !I_IXOFF(tty) && !tty->raw) {
		if(ch == STOP_CHAR(tty)){
			stop_tty(tty);
			return;
		}
		else if(ch == START_CHAR(tty)){
			start_tty(tty);
			return;
		}
	}

	if((tty->flip.flag_buf_ptr == NULL) || 
	   (tty->flip.char_buf_ptr == NULL))
		return;
	tty_insert_flip_char(tty, ch, TTY_NORMAL);
}

static int open_one_chan(struct chan *chan, int input, int output, int primary)
{
	int fd;

	if(chan->opened) return(0);
	if(chan->ops->open == NULL) fd = 0;
	else fd = (*chan->ops->open)(input, output, primary, chan->data,
				     &chan->dev);
	if(fd < 0) return(fd);
	chan->fd = fd;

	chan->opened = 1;
	return(0);
}

int open_chan(struct list_head *chans)
{
	struct list_head *ele;
	struct chan *chan;
	int ret, err = 0;

	list_for_each(ele, chans){
		chan = list_entry(ele, struct chan, list);
		ret = open_one_chan(chan, chan->input, chan->output,
				    chan->primary);
		if(chan->primary) err = ret;
	}
	return(err);
}

void chan_enable_winch(struct list_head *chans, struct tty_struct *tty)
{
	struct list_head *ele;
	struct chan *chan;

	list_for_each(ele, chans){
		chan = list_entry(ele, struct chan, list);
		if(chan->primary && chan->output && chan->ops->winch){
			register_winch(chan->fd, tty);
			return;
		}
	}
}

void enable_chan(struct list_head *chans, struct tty_struct *tty)
{
	struct list_head *ele;
	struct chan *chan;

	list_for_each(ele, chans){
		chan = list_entry(ele, struct chan, list);
		if(!chan->opened) continue;

		line_setup_irq(chan->fd, chan->input, chan->output, tty);
	}
}

void close_chan(struct list_head *chans)
{
	struct chan *chan;

	/* Close in reverse order as open in case more than one of them
	 * refers to the same device and they save and restore that device's
	 * state.  Then, the first one opened will have the original state,
	 * so it must be the last closed.
	 */
	list_for_each_entry_reverse(chan, chans, list) {
		if(!chan->opened) continue;
		if(chan->ops->close != NULL)
			(*chan->ops->close)(chan->fd, chan->data);
		chan->opened = 0;
		chan->fd = -1;
	}
}

int write_chan(struct list_head *chans, const char *buf, int len, 
	       int write_irq)
{
	struct list_head *ele;
	struct chan *chan = NULL;
	int n, ret = 0;

	list_for_each(ele, chans) {
		chan = list_entry(ele, struct chan, list);
		if (!chan->output || (chan->ops->write == NULL))
			continue;
		n = chan->ops->write(chan->fd, buf, len, chan->data);
		if (chan->primary) {
			ret = n;
			if ((ret == -EAGAIN) || ((ret >= 0) && (ret < len)))
				reactivate_fd(chan->fd, write_irq);
		}
	}
	return(ret);
}

int console_write_chan(struct list_head *chans, const char *buf, int len)
{
	struct list_head *ele;
	struct chan *chan;
	int n, ret = 0;

	list_for_each(ele, chans){
		chan = list_entry(ele, struct chan, list);
		if(!chan->output || (chan->ops->console_write == NULL))
			continue;
		n = chan->ops->console_write(chan->fd, buf, len, chan->data);
		if(chan->primary) ret = n;
	}
	return(ret);
}

int console_open_chan(struct line *line, struct console *co, struct chan_opts *opts)
{
	if (!list_empty(&line->chan_list))
		return 0;

	if (0 != parse_chan_pair(line->init_str, &line->chan_list,
				 line->init_pri, co->index, opts))
		return -1;
	if (0 != open_chan(&line->chan_list))
		return -1;
	printk("Console initialized on /dev/%s%d\n",co->name,co->index);
	return 0;
}

int chan_window_size(struct list_head *chans, unsigned short *rows_out,
		      unsigned short *cols_out)
{
	struct list_head *ele;
	struct chan *chan;

	list_for_each(ele, chans){
		chan = list_entry(ele, struct chan, list);
		if(chan->primary){
			if(chan->ops->window_size == NULL) return(0);
			return(chan->ops->window_size(chan->fd, chan->data,
						      rows_out, cols_out));
		}
	}
	return(0);
}

void free_one_chan(struct chan *chan)
{
	list_del(&chan->list);
	if(chan->ops->free != NULL)
		(*chan->ops->free)(chan->data);
	free_irq_by_fd(chan->fd);
	if(chan->primary && chan->output) ignore_sigio_fd(chan->fd);
	kfree(chan);
}

void free_chan(struct list_head *chans)
{
	struct list_head *ele, *next;
	struct chan *chan;

	list_for_each_safe(ele, next, chans){
		chan = list_entry(ele, struct chan, list);
		free_one_chan(chan);
	}
}

static int one_chan_config_string(struct chan *chan, char *str, int size,
				  char **error_out)
{
	int n = 0;

	if(chan == NULL){
		CONFIG_CHUNK(str, size, n, "none", 1);
		return(n);
	}

	CONFIG_CHUNK(str, size, n, chan->ops->type, 0);

	if(chan->dev == NULL){
		CONFIG_CHUNK(str, size, n, "", 1);
		return(n);
	}

	CONFIG_CHUNK(str, size, n, ":", 0);
	CONFIG_CHUNK(str, size, n, chan->dev, 0);

	return(n);
}

static int chan_pair_config_string(struct chan *in, struct chan *out, 
				   char *str, int size, char **error_out)
{
	int n;

	n = one_chan_config_string(in, str, size, error_out);
	str += n;
	size -= n;

	if(in == out){
		CONFIG_CHUNK(str, size, n, "", 1);
		return(n);
	}

	CONFIG_CHUNK(str, size, n, ",", 1);
	n = one_chan_config_string(out, str, size, error_out);
	str += n;
	size -= n;
	CONFIG_CHUNK(str, size, n, "", 1);

	return(n);
}

int chan_config_string(struct list_head *chans, char *str, int size, 
		       char **error_out)
{
	struct list_head *ele;
	struct chan *chan, *in = NULL, *out = NULL;

	list_for_each(ele, chans){
		chan = list_entry(ele, struct chan, list);
		if(!chan->primary)
			continue;
		if(chan->input)
			in = chan;
		if(chan->output)
			out = chan;
	}

	return(chan_pair_config_string(in, out, str, size, error_out));
}

struct chan_type {
	char *key;
	struct chan_ops *ops;
};

struct chan_type chan_table[] = {
	{ "fd", &fd_ops },

#ifdef CONFIG_NULL_CHAN
	{ "null", &null_ops },
#else
	{ "null", &not_configged_ops },
#endif

#ifdef CONFIG_PORT_CHAN
	{ "port", &port_ops },
#else
	{ "port", &not_configged_ops },
#endif

#ifdef CONFIG_PTY_CHAN
	{ "pty", &pty_ops },
	{ "pts", &pts_ops },
#else
	{ "pty", &not_configged_ops },
	{ "pts", &not_configged_ops },
#endif

#ifdef CONFIG_TTY_CHAN
	{ "tty", &tty_ops },
#else
	{ "tty", &not_configged_ops },
#endif

#ifdef CONFIG_XTERM_CHAN
	{ "xterm", &xterm_ops },
#else
	{ "xterm", &not_configged_ops },
#endif
};

static struct chan *parse_chan(char *str, int pri, int device, 
			       struct chan_opts *opts)
{
	struct chan_type *entry;
	struct chan_ops *ops;
	struct chan *chan;
	void *data;
	int i;

	ops = NULL;
	data = NULL;
	for(i = 0; i < sizeof(chan_table)/sizeof(chan_table[0]); i++){
		entry = &chan_table[i];
		if(!strncmp(str, entry->key, strlen(entry->key))){
			ops = entry->ops;
			str += strlen(entry->key);
			break;
		}
	}
	if(ops == NULL){
		my_printf("parse_chan couldn't parse \"%s\"\n",
		       str);
		return(NULL);
	}
	if(ops->init == NULL) return(NULL); 
	data = (*ops->init)(str, device, opts);
	if(data == NULL) return(NULL);

	chan = kmalloc(sizeof(*chan), GFP_ATOMIC);
	if(chan == NULL) return(NULL);
	*chan = ((struct chan) { .list	 	= LIST_HEAD_INIT(chan->list),
				 .primary	= 1,
				 .input		= 0,
				 .output 	= 0,
				 .opened  	= 0,
				 .fd 		= -1,
				 .pri 		= pri,
				 .ops 		= ops,
				 .data 		= data });
	return(chan);
}

int parse_chan_pair(char *str, struct list_head *chans, int pri, int device,
		    struct chan_opts *opts)
{
	struct chan *new, *chan;
	char *in, *out;

	if(!list_empty(chans)){
		chan = list_entry(chans->next, struct chan, list);
		if(chan->pri >= pri) return(0);
		free_chan(chans);
		INIT_LIST_HEAD(chans);
	}

	out = strchr(str, ',');
	if(out != NULL){
		in = str;
		*out = '\0';
		out++;
		new = parse_chan(in, pri, device, opts);
		if(new == NULL) return(-1);
		new->input = 1;
		list_add(&new->list, chans);

		new = parse_chan(out, pri, device, opts);
		if(new == NULL) return(-1);
		list_add(&new->list, chans);
		new->output = 1;
	}
	else {
		new = parse_chan(str, pri, device, opts);
		if(new == NULL) return(-1);
		list_add(&new->list, chans);
		new->input = 1;
		new->output = 1;
	}
	return(0);
}

int chan_out_fd(struct list_head *chans)
{
	struct list_head *ele;
	struct chan *chan;

	list_for_each(ele, chans){
		chan = list_entry(ele, struct chan, list);
		if(chan->primary && chan->output)
			return(chan->fd);
	}
	return(-1);
}

void chan_interrupt(struct list_head *chans, struct work_struct *task,
		    struct tty_struct *tty, int irq)
{
	struct list_head *ele, *next;
	struct chan *chan;
	int err;
	char c;

	list_for_each_safe(ele, next, chans){
		chan = list_entry(ele, struct chan, list);
		if(!chan->input || (chan->ops->read == NULL)) continue;
		do {
			if((tty != NULL) && 
			   (tty->flip.count >= TTY_FLIPBUF_SIZE)){
				schedule_work(task);
				goto out;
			}
			err = chan->ops->read(chan->fd, &c, chan->data);
			if(err > 0)
				tty_receive_char(tty, c);
		} while(err > 0);

		if(err == 0) reactivate_fd(chan->fd, irq);
		if(err == -EIO){
			if(chan->primary){
				if(tty != NULL)
					tty_hangup(tty);
				line_disable(tty, irq);
				close_chan(chans);
				free_chan(chans);
				return;
			}
			else {
				if(chan->ops->close != NULL)
					chan->ops->close(chan->fd, chan->data);
				free_one_chan(chan);
			}
		}
	}
 out:
	if(tty) tty_flip_buffer_push(tty);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
