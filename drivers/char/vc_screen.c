/*
 * linux/drivers/char/vc_screen.c
 *
 * Provide access to virtual console memory.
 * /dev/vcs0: the screen as it is being viewed right now (possibly scrolled)
 * /dev/vcsN: the screen of /dev/ttyN (1 <= N <= 63)
 *            [minor: N]
 *
 * /dev/vcsaN: idem, but including attributes, and prefixed with
 *	the 4 bytes lines,columns,x,y (as screendump used to give).
 *	Attribute/character pair is in native endianity.
 *            [minor: N+128]
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
#include <linux/tty.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/kbd_kern.h>
#include <linux/console.h>
#include <linux/smp_lock.h>
#include <linux/device.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>

#undef attr
#undef org
#undef addr
#define HEADER_SIZE	4

static int
vcs_size(struct inode *inode)
{
	int size;
	int minor = iminor(inode);
	int currcons = minor & 127;
	struct vc_data *vc;

	if (currcons == 0)
		currcons = fg_console;
	else
		currcons--;
	if (!vc_cons_allocated(currcons))
		return -ENXIO;
	vc = vc_cons[currcons].d;

	size = vc->vc_rows * vc->vc_cols;

	if (minor & 128)
		size = 2*size + HEADER_SIZE;
	return size;
}

static loff_t vcs_lseek(struct file *file, loff_t offset, int orig)
{
	int size;

	down(&con_buf_sem);
	size = vcs_size(file->f_path.dentry->d_inode);
	switch (orig) {
		default:
			up(&con_buf_sem);
			return -EINVAL;
		case 2:
			offset += size;
			break;
		case 1:
			offset += file->f_pos;
		case 0:
			break;
	}
	if (offset < 0 || offset > size) {
		up(&con_buf_sem);
		return -EINVAL;
	}
	file->f_pos = offset;
	up(&con_buf_sem);
	return file->f_pos;
}


static ssize_t
vcs_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	unsigned int currcons = iminor(inode);
	struct vc_data *vc;
	long pos;
	long viewed, attr, read;
	int col, maxcol;
	unsigned short *org = NULL;
	ssize_t ret;

	down(&con_buf_sem);

	pos = *ppos;

	/* Select the proper current console and verify
	 * sanity of the situation under the console lock.
	 */
	acquire_console_sem();

	attr = (currcons & 128);
	currcons = (currcons & 127);
	if (currcons == 0) {
		currcons = fg_console;
		viewed = 1;
	} else {
		currcons--;
		viewed = 0;
	}
	ret = -ENXIO;
	if (!vc_cons_allocated(currcons))
		goto unlock_out;
	vc = vc_cons[currcons].d;

	ret = -EINVAL;
	if (pos < 0)
		goto unlock_out;
	read = 0;
	ret = 0;
	while (count) {
		char *con_buf0, *con_buf_start;
		long this_round, size;
		ssize_t orig_count;
		long p = pos;

		/* Check whether we are above size each round,
		 * as copy_to_user at the end of this loop
		 * could sleep.
		 */
		size = vcs_size(inode);
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

		con_buf_start = con_buf0 = con_buf;
		orig_count = this_round;
		maxcol = vc->vc_cols;
		if (!attr) {
			org = screen_pos(vc, p, viewed);
			col = p % maxcol;
			p += maxcol - col;
			while (this_round-- > 0) {
				*con_buf0++ = (vcs_scr_readw(vc, org++) & 0xff);
				if (++col == maxcol) {
					org = screen_pos(vc, p, viewed);
					col = 0;
					p += maxcol;
				}
			}
		} else {
			if (p < HEADER_SIZE) {
				size_t tmp_count;

				con_buf0[0] = (char)vc->vc_rows;
				con_buf0[1] = (char)vc->vc_cols;
				getconsxy(vc, con_buf0 + 2);

				con_buf_start += p;
				this_round += p;
				if (this_round > CON_BUF_SIZE) {
					this_round = CON_BUF_SIZE;
					orig_count = this_round - p;
				}

				tmp_count = HEADER_SIZE;
				if (tmp_count > this_round)
					tmp_count = this_round;

				/* Advance state pointers and move on. */
				this_round -= tmp_count;
				p = HEADER_SIZE;
				con_buf0 = con_buf + HEADER_SIZE;
				/* If this_round >= 0, then p is even... */
			} else if (p & 1) {
				/* Skip first byte for output if start address is odd
				 * Update region sizes up/down depending on free
				 * space in buffer.
				 */
				con_buf_start++;
				if (this_round < CON_BUF_SIZE)
					this_round++;
				else
					orig_count--;
			}
			if (this_round > 0) {
				unsigned short *tmp_buf = (unsigned short *)con_buf0;

				p -= HEADER_SIZE;
				p /= 2;
				col = p % maxcol;

				org = screen_pos(vc, p, viewed);
				p += maxcol - col;

				/* Buffer has even length, so we can always copy
				 * character + attribute. We do not copy last byte
				 * to userspace if this_round is odd.
				 */
				this_round = (this_round + 1) >> 1;

				while (this_round) {
					*tmp_buf++ = vcs_scr_readw(vc, org++);
					this_round --;
					if (++col == maxcol) {
						org = screen_pos(vc, p, viewed);
						col = 0;
						p += maxcol;
					}
				}
			}
		}

		/* Finally, release the console semaphore while we push
		 * all the data to userspace from our temporary buffer.
		 *
		 * AKPM: Even though it's a semaphore, we should drop it because
		 * the pagefault handling code may want to call printk().
		 */

		release_console_sem();
		ret = copy_to_user(buf, con_buf_start, orig_count);
		acquire_console_sem();

		if (ret) {
			read += (orig_count - ret);
			ret = -EFAULT;
			break;
		}
		buf += orig_count;
		pos += orig_count;
		read += orig_count;
		count -= orig_count;
	}
	*ppos += read;
	if (read)
		ret = read;
unlock_out:
	release_console_sem();
	up(&con_buf_sem);
	return ret;
}

static ssize_t
vcs_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	unsigned int currcons = iminor(inode);
	struct vc_data *vc;
	long pos;
	long viewed, attr, size, written;
	char *con_buf0;
	int col, maxcol;
	u16 *org0 = NULL, *org = NULL;
	size_t ret;

	down(&con_buf_sem);

	pos = *ppos;

	/* Select the proper current console and verify
	 * sanity of the situation under the console lock.
	 */
	acquire_console_sem();

	attr = (currcons & 128);
	currcons = (currcons & 127);

	if (currcons == 0) {
		currcons = fg_console;
		viewed = 1;
	} else {
		currcons--;
		viewed = 0;
	}
	ret = -ENXIO;
	if (!vc_cons_allocated(currcons))
		goto unlock_out;
	vc = vc_cons[currcons].d;

	size = vcs_size(inode);
	ret = -EINVAL;
	if (pos < 0 || pos > size)
		goto unlock_out;
	if (count > size - pos)
		count = size - pos;
	written = 0;
	while (count) {
		long this_round = count;
		size_t orig_count;
		long p;

		if (this_round > CON_BUF_SIZE)
			this_round = CON_BUF_SIZE;

		/* Temporarily drop the console lock so that we can read
		 * in the write data from userspace safely.
		 */
		release_console_sem();
		ret = copy_from_user(con_buf, buf, this_round);
		acquire_console_sem();

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

		/* The vcs_size might have changed while we slept to grab
		 * the user buffer, so recheck.
		 * Return data written up to now on failure.
		 */
		size = vcs_size(inode);
		if (pos >= size)
			break;
		if (this_round > size - pos)
			this_round = size - pos;

		/* OK, now actually push the write to the console
		 * under the lock using the local kernel buffer.
		 */

		con_buf0 = con_buf;
		orig_count = this_round;
		maxcol = vc->vc_cols;
		p = pos;
		if (!attr) {
			org0 = org = screen_pos(vc, p, viewed);
			col = p % maxcol;
			p += maxcol - col;

			while (this_round > 0) {
				unsigned char c = *con_buf0++;

				this_round--;
				vcs_scr_writew(vc,
					       (vcs_scr_readw(vc, org) & 0xff00) | c, org);
				org++;
				if (++col == maxcol) {
					org = screen_pos(vc, p, viewed);
					col = 0;
					p += maxcol;
				}
			}
		} else {
			if (p < HEADER_SIZE) {
				char header[HEADER_SIZE];

				getconsxy(vc, header + 2);
				while (p < HEADER_SIZE && this_round > 0) {
					this_round--;
					header[p++] = *con_buf0++;
				}
				if (!viewed)
					putconsxy(vc, header + 2);
			}
			p -= HEADER_SIZE;
			col = (p/2) % maxcol;
			if (this_round > 0) {
				org0 = org = screen_pos(vc, p/2, viewed);
				if ((p & 1) && this_round > 0) {
					char c;

					this_round--;
					c = *con_buf0++;
#ifdef __BIG_ENDIAN
					vcs_scr_writew(vc, c |
					     (vcs_scr_readw(vc, org) & 0xff00), org);
#else
					vcs_scr_writew(vc, (c << 8) |
					     (vcs_scr_readw(vc, org) & 0xff), org);
#endif
					org++;
					p++;
					if (++col == maxcol) {
						org = screen_pos(vc, p/2, viewed);
						col = 0;
					}
				}
				p /= 2;
				p += maxcol - col;
			}
			while (this_round > 1) {
				unsigned short w;

				w = get_unaligned(((unsigned short *)con_buf0));
				vcs_scr_writew(vc, w, org++);
				con_buf0 += 2;
				this_round -= 2;
				if (++col == maxcol) {
					org = screen_pos(vc, p, viewed);
					col = 0;
					p += maxcol;
				}
			}
			if (this_round > 0) {
				unsigned char c;

				c = *con_buf0++;
#ifdef __BIG_ENDIAN
				vcs_scr_writew(vc, (vcs_scr_readw(vc, org) & 0xff) | (c << 8), org);
#else
				vcs_scr_writew(vc, (vcs_scr_readw(vc, org) & 0xff00) | c, org);
#endif
			}
		}
		count -= orig_count;
		written += orig_count;
		buf += orig_count;
		pos += orig_count;
		if (org0)
			update_region(vc, (unsigned long)(org0), org - org0);
	}
	*ppos += written;
	ret = written;

unlock_out:
	release_console_sem();

	up(&con_buf_sem);

	return ret;
}

static int
vcs_open(struct inode *inode, struct file *filp)
{
	unsigned int currcons = iminor(inode) & 127;
	if(currcons && !vc_cons_allocated(currcons-1))
		return -ENXIO;
	return 0;
}

static const struct file_operations vcs_fops = {
	.llseek		= vcs_lseek,
	.read		= vcs_read,
	.write		= vcs_write,
	.open		= vcs_open,
};

static struct class *vc_class;

void vcs_make_sysfs(struct tty_struct *tty)
{
	device_create(vc_class, NULL, MKDEV(VCS_MAJOR, tty->index + 1),
			"vcs%u", tty->index + 1);
	device_create(vc_class, NULL, MKDEV(VCS_MAJOR, tty->index + 129),
			"vcsa%u", tty->index + 1);
}

void vcs_remove_sysfs(struct tty_struct *tty)
{
	device_destroy(vc_class, MKDEV(VCS_MAJOR, tty->index + 1));
	device_destroy(vc_class, MKDEV(VCS_MAJOR, tty->index + 129));
}

int __init vcs_init(void)
{
	if (register_chrdev(VCS_MAJOR, "vcs", &vcs_fops))
		panic("unable to get major %d for vcs device", VCS_MAJOR);
	vc_class = class_create(THIS_MODULE, "vc");

	device_create(vc_class, NULL, MKDEV(VCS_MAJOR, 0), "vcs");
	device_create(vc_class, NULL, MKDEV(VCS_MAJOR, 128), "vcsa");
	return 0;
}
