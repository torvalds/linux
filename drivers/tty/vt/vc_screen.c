// SPDX-License-Identifier: GPL-2.0
/*
 * Provide access to virtual console memory.
 * /dev/vcs: the screen as it is being viewed right now (possibly scrolled)
 * /dev/vcsN: the screen of /dev/ttyN (1 <= N <= 63)
 *            [minor: N]
 *
 * /dev/vcsaN: idem, but including attributes, and prefixed with
 *	the 4 bytes lines,columns,x,y (as screendump used to give).
 *	Attribute/character pair is in native endianity.
 *            [minor: N+128]
 *
 * /dev/vcsuN: similar to /dev/vcsaN but using 4-byte unicode values
 *	instead of 1-byte screen glyph values.
 *            [minor: N+64]
 *
 * /dev/vcsuaN: same idea as /dev/vcsaN for unicode (not yet implemented).
 *
 * This replaces screendump and part of selection, so that the system
 * administrator can control access using file system permissions.
 *
 * aeb@cwi.nl - efter Friedas begravelse - 950211
 *
 * machek@k332.feld.cvut.cz - modified not to send characters to wrong console
 *	 - fixed some fatal off-by-one bugs (0-- no longer == -1 -> looping and looping and looping...)
 *	 - making it shorter - scr_readw are macros which expand in PRETTY long code
 */

#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/tty.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/kbd_kern.h>
#include <linux/console.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/notifier.h>

#include <linux/uaccess.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>

#define HEADER_SIZE	4u
#define CON_BUF_SIZE (IS_ENABLED(CONFIG_BASE_SMALL) ? 256 : PAGE_SIZE)

/*
 * Our minor space:
 *
 *   0 ... 63	glyph mode without attributes
 *  64 ... 127	unicode mode without attributes
 * 128 ... 191	glyph mode with attributes
 * 192 ... 255	unused (reserved for unicode with attributes)
 *
 * This relies on MAX_NR_CONSOLES being  <= 63, meaning 63 actual consoles
 * with minors 0, 64, 128 and 192 being proxies for the foreground console.
 */
#if MAX_NR_CONSOLES > 63
#warning "/dev/vcs* devices may not accommodate more than 63 consoles"
#endif

#define console(inode)		(iminor(inode) & 63)
#define use_unicode(inode)	(iminor(inode) & 64)
#define use_attributes(inode)	(iminor(inode) & 128)


struct vcs_poll_data {
	struct notifier_block notifier;
	unsigned int cons_num;
	int event;
	wait_queue_head_t waitq;
	struct fasync_struct *fasync;
};

static int
vcs_notifier(struct notifier_block *nb, unsigned long code, void *_param)
{
	struct vt_notifier_param *param = _param;
	struct vc_data *vc = param->vc;
	struct vcs_poll_data *poll =
		container_of(nb, struct vcs_poll_data, notifier);
	int currcons = poll->cons_num;
	int fa_band;

	switch (code) {
	case VT_UPDATE:
		fa_band = POLL_PRI;
		break;
	case VT_DEALLOCATE:
		fa_band = POLL_HUP;
		break;
	default:
		return NOTIFY_DONE;
	}

	if (currcons == 0)
		currcons = fg_console;
	else
		currcons--;
	if (currcons != vc->vc_num)
		return NOTIFY_DONE;

	poll->event = code;
	wake_up_interruptible(&poll->waitq);
	kill_fasync(&poll->fasync, SIGIO, fa_band);
	return NOTIFY_OK;
}

static void
vcs_poll_data_free(struct vcs_poll_data *poll)
{
	unregister_vt_notifier(&poll->notifier);
	kfree(poll);
}

static struct vcs_poll_data *
vcs_poll_data_get(struct file *file)
{
	struct vcs_poll_data *poll = file->private_data, *kill = NULL;

	if (poll)
		return poll;

	poll = kzalloc(sizeof(*poll), GFP_KERNEL);
	if (!poll)
		return NULL;
	poll->cons_num = console(file_inode(file));
	init_waitqueue_head(&poll->waitq);
	poll->notifier.notifier_call = vcs_notifier;
	/*
	 * In order not to lose any update event, we must pretend one might
	 * have occurred before we have a chance to register our notifier.
	 * This is also how user space has come to detect which kernels
	 * support POLLPRI on /dev/vcs* devices i.e. using poll() with
	 * POLLPRI and a zero timeout.
	 */
	poll->event = VT_UPDATE;

	if (register_vt_notifier(&poll->notifier) != 0) {
		kfree(poll);
		return NULL;
	}

	/*
	 * This code may be called either through ->poll() or ->fasync().
	 * If we have two threads using the same file descriptor, they could
	 * both enter this function, both notice that the structure hasn't
	 * been allocated yet and go ahead allocating it in parallel, but
	 * only one of them must survive and be shared otherwise we'd leak
	 * memory with a dangling notifier callback.
	 */
	spin_lock(&file->f_lock);
	if (!file->private_data) {
		file->private_data = poll;
	} else {
		/* someone else raced ahead of us */
		kill = poll;
		poll = file->private_data;
	}
	spin_unlock(&file->f_lock);
	if (kill)
		vcs_poll_data_free(kill);

	return poll;
}

/**
 * vcs_vc - return VC for @inode
 * @inode: inode for which to return a VC
 * @viewed: returns whether this console is currently foreground (viewed)
 *
 * Must be called with console_lock.
 */
static struct vc_data *vcs_vc(struct inode *inode, bool *viewed)
{
	unsigned int currcons = console(inode);

	WARN_CONSOLE_UNLOCKED();

	if (currcons == 0) {
		currcons = fg_console;
		if (viewed)
			*viewed = true;
	} else {
		currcons--;
		if (viewed)
			*viewed = false;
	}
	return vc_cons[currcons].d;
}

/**
 * vcs_size - return size for a VC in @vc
 * @vc: which VC
 * @attr: does it use attributes?
 * @unicode: is it unicode?
 *
 * Must be called with console_lock.
 */
static int vcs_size(const struct vc_data *vc, bool attr, bool unicode)
{
	int size;

	WARN_CONSOLE_UNLOCKED();

	size = vc->vc_rows * vc->vc_cols;

	if (attr) {
		if (unicode)
			return -EOPNOTSUPP;

		size = 2 * size + HEADER_SIZE;
	} else if (unicode)
		size *= 4;

	return size;
}

static loff_t vcs_lseek(struct file *file, loff_t offset, int orig)
{
	struct inode *inode = file_inode(file);
	struct vc_data *vc;
	int size;

	console_lock();
	vc = vcs_vc(inode, NULL);
	if (!vc) {
		console_unlock();
		return -ENXIO;
	}

	size = vcs_size(vc, use_attributes(inode), use_unicode(inode));
	console_unlock();
	if (size < 0)
		return size;
	return fixed_size_llseek(file, offset, orig, size);
}

static int vcs_read_buf_uni(struct vc_data *vc, char *con_buf,
		unsigned int pos, unsigned int count, bool viewed)
{
	unsigned int nr, row, col, maxcol = vc->vc_cols;
	int ret;

	ret = vc_uniscr_check(vc);
	if (ret)
		return ret;

	pos /= 4;
	row = pos / maxcol;
	col = pos % maxcol;
	nr = maxcol - col;
	do {
		if (nr > count / 4)
			nr = count / 4;
		vc_uniscr_copy_line(vc, con_buf, viewed, row, col, nr);
		con_buf += nr * 4;
		count -= nr * 4;
		row++;
		col = 0;
		nr = maxcol;
	} while (count);

	return 0;
}

static void vcs_read_buf_noattr(const struct vc_data *vc, char *con_buf,
		unsigned int pos, unsigned int count, bool viewed)
{
	u16 *org;
	unsigned int col, maxcol = vc->vc_cols;

	org = screen_pos(vc, pos, viewed);
	col = pos % maxcol;
	pos += maxcol - col;

	while (count-- > 0) {
		*con_buf++ = (vcs_scr_readw(vc, org++) & 0xff);
		if (++col == maxcol) {
			org = screen_pos(vc, pos, viewed);
			col = 0;
			pos += maxcol;
		}
	}
}

static unsigned int vcs_read_buf(const struct vc_data *vc, char *con_buf,
		unsigned int pos, unsigned int count, bool viewed,
		unsigned int *skip)
{
	u16 *org, *con_buf16;
	unsigned int col, maxcol = vc->vc_cols;
	unsigned int filled = count;

	if (pos < HEADER_SIZE) {
		/* clamp header values if they don't fit */
		con_buf[0] = min(vc->vc_rows, 0xFFu);
		con_buf[1] = min(vc->vc_cols, 0xFFu);
		getconsxy(vc, con_buf + 2);

		*skip += pos;
		count += pos;
		if (count > CON_BUF_SIZE) {
			count = CON_BUF_SIZE;
			filled = count - pos;
		}

		/* Advance state pointers and move on. */
		count -= min(HEADER_SIZE, count);
		pos = HEADER_SIZE;
		con_buf += HEADER_SIZE;
		/* If count >= 0, then pos is even... */
	} else if (pos & 1) {
		/*
		 * Skip first byte for output if start address is odd. Update
		 * region sizes up/down depending on free space in buffer.
		 */
		(*skip)++;
		if (count < CON_BUF_SIZE)
			count++;
		else
			filled--;
	}

	if (!count)
		return filled;

	pos -= HEADER_SIZE;
	pos /= 2;
	col = pos % maxcol;

	org = screen_pos(vc, pos, viewed);
	pos += maxcol - col;

	/*
	 * Buffer has even length, so we can always copy character + attribute.
	 * We do not copy last byte to userspace if count is odd.
	 */
	count = (count + 1) / 2;
	con_buf16 = (u16 *)con_buf;

	while (count) {
		*con_buf16++ = vcs_scr_readw(vc, org++);
		count--;
		if (++col == maxcol) {
			org = screen_pos(vc, pos, viewed);
			col = 0;
			pos += maxcol;
		}
	}

	return filled;
}

static ssize_t
vcs_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct inode *inode = file_inode(file);
	struct vc_data *vc;
	struct vcs_poll_data *poll;
	unsigned int read;
	ssize_t ret;
	char *con_buf;
	loff_t pos;
	bool viewed, attr, uni_mode;

	con_buf = (char *) __get_free_page(GFP_KERNEL);
	if (!con_buf)
		return -ENOMEM;

	pos = *ppos;

	/* Select the proper current console and verify
	 * sanity of the situation under the console lock.
	 */
	console_lock();

	uni_mode = use_unicode(inode);
	attr = use_attributes(inode);

	ret = -EINVAL;
	if (pos < 0)
		goto unlock_out;
	/* we enforce 32-bit alignment for pos and count in unicode mode */
	if (uni_mode && (pos | count) & 3)
		goto unlock_out;

	poll = file->private_data;
	if (count && poll)
		poll->event = 0;
	read = 0;
	ret = 0;
	while (count) {
		unsigned int this_round, skip = 0;
		int size;

		vc = vcs_vc(inode, &viewed);
		if (!vc) {
			ret = -ENXIO;
			break;
		}

		/* Check whether we are above size each round,
		 * as copy_to_user at the end of this loop
		 * could sleep.
		 */
		size = vcs_size(vc, attr, uni_mode);
		if (size < 0) {
			ret = size;
			break;
		}
		if (pos >= size)
			break;
		if (count > size - pos)
			count = size - pos;

		this_round = count;
		if (this_round > CON_BUF_SIZE)
			this_round = CON_BUF_SIZE;

		/* Perform the whole read into the local con_buf.
		 * Then we can drop the console spinlock and safely
		 * attempt to move it to userspace.
		 */

		if (uni_mode) {
			ret = vcs_read_buf_uni(vc, con_buf, pos, this_round,
					viewed);
			if (ret)
				break;
		} else if (!attr) {
			vcs_read_buf_noattr(vc, con_buf, pos, this_round,
					viewed);
		} else {
			this_round = vcs_read_buf(vc, con_buf, pos, this_round,
					viewed, &skip);
		}

		/* Finally, release the console semaphore while we push
		 * all the data to userspace from our temporary buffer.
		 *
		 * AKPM: Even though it's a semaphore, we should drop it because
		 * the pagefault handling code may want to call printk().
		 */

		console_unlock();
		ret = copy_to_user(buf, con_buf + skip, this_round);
		console_lock();

		if (ret) {
			read += this_round - ret;
			ret = -EFAULT;
			break;
		}
		buf += this_round;
		pos += this_round;
		read += this_round;
		count -= this_round;
	}
	*ppos += read;
	if (read)
		ret = read;
unlock_out:
	console_unlock();
	free_page((unsigned long) con_buf);
	return ret;
}

static u16 *vcs_write_buf_noattr(struct vc_data *vc, const char *con_buf,
		unsigned int pos, unsigned int count, bool viewed, u16 **org0)
{
	u16 *org;
	unsigned int col, maxcol = vc->vc_cols;

	*org0 = org = screen_pos(vc, pos, viewed);
	col = pos % maxcol;
	pos += maxcol - col;

	while (count > 0) {
		unsigned char c = *con_buf++;

		count--;
		vcs_scr_writew(vc,
			       (vcs_scr_readw(vc, org) & 0xff00) | c, org);
		org++;
		if (++col == maxcol) {
			org = screen_pos(vc, pos, viewed);
			col = 0;
			pos += maxcol;
		}
	}

	return org;
}

/*
 * Compilers (gcc 10) are unable to optimize the swap in cpu_to_le16. So do it
 * the poor man way.
 */
static inline u16 vc_compile_le16(u8 hi, u8 lo)
{
#ifdef __BIG_ENDIAN
	return (lo << 8u) | hi;
#else
	return (hi << 8u) | lo;
#endif
}

static u16 *vcs_write_buf(struct vc_data *vc, const char *con_buf,
		unsigned int pos, unsigned int count, bool viewed, u16 **org0)
{
	u16 *org;
	unsigned int col, maxcol = vc->vc_cols;
	unsigned char c;

	/* header */
	if (pos < HEADER_SIZE) {
		char header[HEADER_SIZE];

		getconsxy(vc, header + 2);
		while (pos < HEADER_SIZE && count > 0) {
			count--;
			header[pos++] = *con_buf++;
		}
		if (!viewed)
			putconsxy(vc, header + 2);
	}

	if (!count)
		return NULL;

	pos -= HEADER_SIZE;
	col = (pos/2) % maxcol;

	*org0 = org = screen_pos(vc, pos/2, viewed);

	/* odd pos -- the first single character */
	if (pos & 1) {
		count--;
		c = *con_buf++;
		vcs_scr_writew(vc, vc_compile_le16(c, vcs_scr_readw(vc, org)),
				org);
		org++;
		pos++;
		if (++col == maxcol) {
			org = screen_pos(vc, pos/2, viewed);
			col = 0;
		}
	}

	pos /= 2;
	pos += maxcol - col;

	/* even pos -- handle attr+character pairs */
	while (count > 1) {
		unsigned short w;

		w = get_unaligned(((unsigned short *)con_buf));
		vcs_scr_writew(vc, w, org++);
		con_buf += 2;
		count -= 2;
		if (++col == maxcol) {
			org = screen_pos(vc, pos, viewed);
			col = 0;
			pos += maxcol;
		}
	}

	if (!count)
		return org;

	/* odd pos -- the remaining character */
	c = *con_buf++;
	vcs_scr_writew(vc, vc_compile_le16(vcs_scr_readw(vc, org) >> 8, c),
				org);

	return org;
}

static ssize_t
vcs_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct inode *inode = file_inode(file);
	struct vc_data *vc;
	char *con_buf;
	u16 *org0, *org;
	unsigned int written;
	int size;
	ssize_t ret;
	loff_t pos;
	bool viewed, attr;

	if (use_unicode(inode))
		return -EOPNOTSUPP;

	con_buf = (char *) __get_free_page(GFP_KERNEL);
	if (!con_buf)
		return -ENOMEM;

	pos = *ppos;

	/* Select the proper current console and verify
	 * sanity of the situation under the console lock.
	 */
	console_lock();

	attr = use_attributes(inode);
	ret = -ENXIO;
	vc = vcs_vc(inode, &viewed);
	if (!vc)
		goto unlock_out;

	size = vcs_size(vc, attr, false);
	if (size < 0) {
		ret = size;
		goto unlock_out;
	}
	ret = -EINVAL;
	if (pos < 0 || pos > size)
		goto unlock_out;
	if (count > size - pos)
		count = size - pos;
	written = 0;
	while (count) {
		unsigned int this_round = count;

		if (this_round > CON_BUF_SIZE)
			this_round = CON_BUF_SIZE;

		/* Temporarily drop the console lock so that we can read
		 * in the write data from userspace safely.
		 */
		console_unlock();
		ret = copy_from_user(con_buf, buf, this_round);
		console_lock();

		if (ret) {
			this_round -= ret;
			if (!this_round) {
				/* Abort loop if no data were copied. Otherwise
				 * fail with -EFAULT.
				 */
				if (written)
					break;
				ret = -EFAULT;
				goto unlock_out;
			}
		}

		/* The vc might have been freed or vcs_size might have changed
		 * while we slept to grab the user buffer, so recheck.
		 * Return data written up to now on failure.
		 */
		vc = vcs_vc(inode, &viewed);
		if (!vc) {
			if (written)
				break;
			ret = -ENXIO;
			goto unlock_out;
		}
		size = vcs_size(vc, attr, false);
		if (size < 0) {
			if (written)
				break;
			ret = size;
			goto unlock_out;
		}
		if (pos >= size)
			break;
		if (this_round > size - pos)
			this_round = size - pos;

		/* OK, now actually push the write to the console
		 * under the lock using the local kernel buffer.
		 */

		if (attr)
			org = vcs_write_buf(vc, con_buf, pos, this_round,
					viewed, &org0);
		else
			org = vcs_write_buf_noattr(vc, con_buf, pos, this_round,
					viewed, &org0);

		count -= this_round;
		written += this_round;
		buf += this_round;
		pos += this_round;
		if (org)
			update_region(vc, (unsigned long)(org0), org - org0);
	}
	*ppos += written;
	ret = written;
	if (written)
		vcs_scr_updated(vc);

unlock_out:
	console_unlock();
	free_page((unsigned long) con_buf);
	return ret;
}

static __poll_t
vcs_poll(struct file *file, poll_table *wait)
{
	struct vcs_poll_data *poll = vcs_poll_data_get(file);
	__poll_t ret = DEFAULT_POLLMASK|EPOLLERR;

	if (poll) {
		poll_wait(file, &poll->waitq, wait);
		switch (poll->event) {
		case VT_UPDATE:
			ret = DEFAULT_POLLMASK|EPOLLPRI;
			break;
		case VT_DEALLOCATE:
			ret = DEFAULT_POLLMASK|EPOLLHUP|EPOLLERR;
			break;
		case 0:
			ret = DEFAULT_POLLMASK;
			break;
		}
	}
	return ret;
}

static int
vcs_fasync(int fd, struct file *file, int on)
{
	struct vcs_poll_data *poll = file->private_data;

	if (!poll) {
		/* don't allocate anything if all we want is disable fasync */
		if (!on)
			return 0;
		poll = vcs_poll_data_get(file);
		if (!poll)
			return -ENOMEM;
	}

	return fasync_helper(fd, file, on, &poll->fasync);
}

static int
vcs_open(struct inode *inode, struct file *filp)
{
	unsigned int currcons = console(inode);
	bool attr = use_attributes(inode);
	bool uni_mode = use_unicode(inode);
	int ret = 0;

	/* we currently don't support attributes in unicode mode */
	if (attr && uni_mode)
		return -EOPNOTSUPP;

	console_lock();
	if(currcons && !vc_cons_allocated(currcons-1))
		ret = -ENXIO;
	console_unlock();
	return ret;
}

static int vcs_release(struct inode *inode, struct file *file)
{
	struct vcs_poll_data *poll = file->private_data;

	if (poll)
		vcs_poll_data_free(poll);
	return 0;
}

static const struct file_operations vcs_fops = {
	.llseek		= vcs_lseek,
	.read		= vcs_read,
	.write		= vcs_write,
	.poll		= vcs_poll,
	.fasync		= vcs_fasync,
	.open		= vcs_open,
	.release	= vcs_release,
};

static const struct class vc_class = {
	.name = "vc",
};

void vcs_make_sysfs(int index)
{
	device_create(&vc_class, NULL, MKDEV(VCS_MAJOR, index + 1), NULL, "vcs%u", index + 1);
	device_create(&vc_class, NULL, MKDEV(VCS_MAJOR, index + 65), NULL, "vcsu%u", index + 1);
	device_create(&vc_class, NULL, MKDEV(VCS_MAJOR, index + 129), NULL, "vcsa%u", index + 1);
}

void vcs_remove_sysfs(int index)
{
	device_destroy(&vc_class, MKDEV(VCS_MAJOR, index + 1));
	device_destroy(&vc_class, MKDEV(VCS_MAJOR, index + 65));
	device_destroy(&vc_class, MKDEV(VCS_MAJOR, index + 129));
}

int __init vcs_init(void)
{
	unsigned int i;

	if (register_chrdev(VCS_MAJOR, "vcs", &vcs_fops))
		panic("unable to get major %d for vcs device", VCS_MAJOR);
	if (class_register(&vc_class))
		panic("unable to create vc_class");

	device_create(&vc_class, NULL, MKDEV(VCS_MAJOR, 0), NULL, "vcs");
	device_create(&vc_class, NULL, MKDEV(VCS_MAJOR, 64), NULL, "vcsu");
	device_create(&vc_class, NULL, MKDEV(VCS_MAJOR, 128), NULL, "vcsa");
	for (i = 0; i < MIN_NR_CONSOLES; i++)
		vcs_make_sysfs(i);
	return 0;
}
