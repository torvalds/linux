/* $Id: ioctl32.c,v 1.136 2002/01/14 09:49:52 davem Exp $
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *
 * Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 2003  Pavel Machek (pavel@suse.cz)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * ioctls.
 */

#define INCLUDES
#include "compat_ioctl.c"
#include <linux/syscalls.h>
#include <asm/fbio.h>

/* Use this to get at 32-bit user passed pointers. 
 * See sys_sparc32.c for description about it.
 */
#define A(__x) compat_ptr(__x)

#define CODE
#include "compat_ioctl.c"

struct  fbcmap32 {
	int             index;          /* first element (0 origin) */
	int             count;
	u32		red;
	u32		green;
	u32		blue;
};

#define FBIOPUTCMAP32	_IOW('F', 3, struct fbcmap32)
#define FBIOGETCMAP32	_IOW('F', 4, struct fbcmap32)

static int fbiogetputcmap(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct fbcmap32 __user *argp = (void __user *)arg;
	struct fbcmap __user *p = compat_alloc_user_space(sizeof(*p));
	u32 addr;
	int ret;
	
	ret = copy_in_user(p, argp, 2 * sizeof(int));
	ret |= get_user(addr, &argp->red);
	ret |= put_user(compat_ptr(addr), &p->red);
	ret |= get_user(addr, &argp->green);
	ret |= put_user(compat_ptr(addr), &p->green);
	ret |= get_user(addr, &argp->blue);
	ret |= put_user(compat_ptr(addr), &p->blue);
	if (ret)
		return -EFAULT;
	return sys_ioctl(fd, (cmd == FBIOPUTCMAP32) ? FBIOPUTCMAP_SPARC : FBIOGETCMAP_SPARC, (unsigned long)p);
}

struct fbcursor32 {
	short set;		/* what to set, choose from the list above */
	short enable;		/* cursor on/off */
	struct fbcurpos pos;	/* cursor position */
	struct fbcurpos hot;	/* cursor hot spot */
	struct fbcmap32 cmap;	/* color map info */
	struct fbcurpos size;	/* cursor bit map size */
	u32	image;		/* cursor image bits */
	u32	mask;		/* cursor mask bits */
};
	
#define FBIOSCURSOR32	_IOW('F', 24, struct fbcursor32)
#define FBIOGCURSOR32	_IOW('F', 25, struct fbcursor32)

static int fbiogscursor(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct fbcursor __user *p = compat_alloc_user_space(sizeof(*p));
	struct fbcursor32 __user *argp =  (void __user *)arg;
	compat_uptr_t addr;
	int ret;
	
	ret = copy_in_user(p, argp,
			      2 * sizeof (short) + 2 * sizeof(struct fbcurpos));
	ret |= copy_in_user(&p->size, &argp->size, sizeof(struct fbcurpos));
	ret |= copy_in_user(&p->cmap, &argp->cmap, 2 * sizeof(int));
	ret |= get_user(addr, &argp->cmap.red);
	ret |= put_user(compat_ptr(addr), &p->cmap.red);
	ret |= get_user(addr, &argp->cmap.green);
	ret |= put_user(compat_ptr(addr), &p->cmap.green);
	ret |= get_user(addr, &argp->cmap.blue);
	ret |= put_user(compat_ptr(addr), &p->cmap.blue);
	ret |= get_user(addr, &argp->mask);
	ret |= put_user(compat_ptr(addr), &p->mask);
	ret |= get_user(addr, &argp->image);
	ret |= put_user(compat_ptr(addr), &p->image);
	if (ret)
		return -EFAULT;
	return sys_ioctl (fd, FBIOSCURSOR, (unsigned long)p);
}

#define COMPATIBLE_IOCTL(cmd)		HANDLE_IOCTL((cmd),sys_ioctl)
#define HANDLE_IOCTL(cmd,handler)	{ (cmd), (ioctl_trans_handler_t)(handler), NULL },
#define IOCTL_TABLE_START \
	struct ioctl_trans ioctl_start[] = {
#define IOCTL_TABLE_END \
	};

IOCTL_TABLE_START
#include <linux/compat_ioctl.h>
#define DECLARES
#include "compat_ioctl.c"
COMPATIBLE_IOCTL(FBIOGTYPE)
COMPATIBLE_IOCTL(FBIOSATTR)
COMPATIBLE_IOCTL(FBIOGATTR)
COMPATIBLE_IOCTL(FBIOSVIDEO)
COMPATIBLE_IOCTL(FBIOGVIDEO)
COMPATIBLE_IOCTL(FBIOGCURSOR32)  /* This is not implemented yet. Later it should be converted... */
COMPATIBLE_IOCTL(FBIOSCURPOS)
COMPATIBLE_IOCTL(FBIOGCURPOS)
COMPATIBLE_IOCTL(FBIOGCURMAX)
/* Little k */
/* Little v, the video4linux ioctls */
/* And these ioctls need translation */
/* Note SIOCRTMSG is no longer, so this is safe and * the user would have seen just an -EINVAL anyways. */
HANDLE_IOCTL(FBIOPUTCMAP32, fbiogetputcmap)
HANDLE_IOCTL(FBIOGETCMAP32, fbiogetputcmap)
HANDLE_IOCTL(FBIOSCURSOR32, fbiogscursor)
#if 0
HANDLE_IOCTL(RTC32_IRQP_READ, do_rtc_ioctl)
HANDLE_IOCTL(RTC32_IRQP_SET, do_rtc_ioctl)
HANDLE_IOCTL(RTC32_EPOCH_READ, do_rtc_ioctl)
HANDLE_IOCTL(RTC32_EPOCH_SET, do_rtc_ioctl)
#endif
/* take care of sizeof(sizeof()) breakage */
IOCTL_TABLE_END

int ioctl_table_size = ARRAY_SIZE(ioctl_start);
