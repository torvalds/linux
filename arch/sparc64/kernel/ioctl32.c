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
#include <linux/ncp_fs.h>
#include <linux/syscalls.h>
#include <asm/fbio.h>
#include <asm/kbio.h>
#include <asm/vuid_event.h>
#include <asm/envctrl.h>
#include <asm/display7seg.h>
#include <asm/openpromio.h>
#include <asm/audioio.h>
#include <asm/watchdog.h>

/* Use this to get at 32-bit user passed pointers. 
 * See sys_sparc32.c for description about it.
 */
#define A(__x) compat_ptr(__x)

static __inline__ void *alloc_user_space(long len)
{
	struct pt_regs *regs = current_thread_info()->kregs;
	unsigned long usp = regs->u_regs[UREG_I6];

	if (!(test_thread_flag(TIF_32BIT)))
		usp += STACK_BIAS;

	return (void *) (usp - len);
}

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

#if defined(CONFIG_DRM) || defined(CONFIG_DRM_MODULE)
/* This really belongs in include/linux/drm.h -DaveM */
#include "../../../drivers/char/drm/drm.h"

typedef struct drm32_version {
	int    version_major;	  /* Major version			    */
	int    version_minor;	  /* Minor version			    */
	int    version_patchlevel;/* Patch level			    */
	int    name_len;	  /* Length of name buffer		    */
	u32    name;		  /* Name of driver			    */
	int    date_len;	  /* Length of date buffer		    */
	u32    date;		  /* User-space buffer to hold date	    */
	int    desc_len;	  /* Length of desc buffer		    */
	u32    desc;		  /* User-space buffer to hold desc	    */
} drm32_version_t;
#define DRM32_IOCTL_VERSION    DRM_IOWR(0x00, drm32_version_t)

static int drm32_version(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	drm32_version_t __user *uversion = (drm32_version_t __user *)arg;
	drm_version_t __user *p = compat_alloc_user_space(sizeof(*p));
	compat_uptr_t addr;
	int n;
	int ret;

	if (clear_user(p, 3 * sizeof(int)) ||
	    get_user(n, &uversion->name_len) ||
	    put_user(n, &p->name_len) ||
	    get_user(addr, &uversion->name) ||
	    put_user(compat_ptr(addr), &p->name) ||
	    get_user(n, &uversion->date_len) ||
	    put_user(n, &p->date_len) ||
	    get_user(addr, &uversion->date) ||
	    put_user(compat_ptr(addr), &p->date) ||
	    get_user(n, &uversion->desc_len) ||
	    put_user(n, &p->desc_len) ||
	    get_user(addr, &uversion->desc) ||
	    put_user(compat_ptr(addr), &p->desc))
		return -EFAULT;

        ret = sys_ioctl(fd, DRM_IOCTL_VERSION, (unsigned long)p);
	if (ret)
		return ret;

	if (copy_in_user(uversion, p, 3 * sizeof(int)) ||
	    get_user(n, &p->name_len) ||
	    put_user(n, &uversion->name_len) ||
	    get_user(n, &p->date_len) ||
	    put_user(n, &uversion->date_len) ||
	    get_user(n, &p->desc_len) ||
	    put_user(n, &uversion->desc_len))
		return -EFAULT;

	return 0;
}

typedef struct drm32_unique {
	int	unique_len;	  /* Length of unique			    */
	u32	unique;		  /* Unique name for driver instantiation   */
} drm32_unique_t;
#define DRM32_IOCTL_GET_UNIQUE DRM_IOWR(0x01, drm32_unique_t)
#define DRM32_IOCTL_SET_UNIQUE DRM_IOW( 0x10, drm32_unique_t)

static int drm32_getsetunique(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	drm32_unique_t __user *uarg = (drm32_unique_t __user *)arg;
	drm_unique_t __user *p = compat_alloc_user_space(sizeof(*p));
	compat_uptr_t addr;
	int n;
	int ret;

	if (get_user(n, &uarg->unique_len) ||
	    put_user(n, &p->unique_len) ||
	    get_user(addr, &uarg->unique) ||
	    put_user(compat_ptr(addr), &p->unique))
		return -EFAULT;

	if (cmd == DRM32_IOCTL_GET_UNIQUE)
		ret = sys_ioctl (fd, DRM_IOCTL_GET_UNIQUE, (unsigned long)p);
	else
		ret = sys_ioctl (fd, DRM_IOCTL_SET_UNIQUE, (unsigned long)p);

	if (ret)
		return ret;

	if (get_user(n, &p->unique_len) || put_user(n, &uarg->unique_len))
		return -EFAULT;

	return 0;
}

typedef struct drm32_map {
	u32		offset;	 /* Requested physical address (0 for SAREA)*/
	u32		size;	 /* Requested physical size (bytes)	    */
	drm_map_type_t	type;	 /* Type of memory to map		    */
	drm_map_flags_t flags;	 /* Flags				    */
	u32		handle;  /* User-space: "Handle" to pass to mmap    */
				 /* Kernel-space: kernel-virtual address    */
	int		mtrr;	 /* MTRR slot used			    */
				 /* Private data			    */
} drm32_map_t;
#define DRM32_IOCTL_ADD_MAP    DRM_IOWR(0x15, drm32_map_t)

static int drm32_addmap(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	drm32_map_t __user *uarg = (drm32_map_t __user *) arg;
	drm_map_t karg;
	mm_segment_t old_fs;
	u32 tmp;
	int ret;

	ret  = get_user(karg.offset, &uarg->offset);
	ret |= get_user(karg.size, &uarg->size);
	ret |= get_user(karg.type, &uarg->type);
	ret |= get_user(karg.flags, &uarg->flags);
	ret |= get_user(tmp, &uarg->handle);
	ret |= get_user(karg.mtrr, &uarg->mtrr);
	if (ret)
		return -EFAULT;

	karg.handle = (void *) (unsigned long) tmp;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, DRM_IOCTL_ADD_MAP, (unsigned long) &karg);
	set_fs(old_fs);

	if (!ret) {
		ret  = put_user(karg.offset, &uarg->offset);
		ret |= put_user(karg.size, &uarg->size);
		ret |= put_user(karg.type, &uarg->type);
		ret |= put_user(karg.flags, &uarg->flags);
		tmp = (u32) (long)karg.handle;
		ret |= put_user(tmp, &uarg->handle);
		ret |= put_user(karg.mtrr, &uarg->mtrr);
		if (ret)
			ret = -EFAULT;
	}

	return ret;
}

typedef struct drm32_buf_info {
	int	       count;	/* Entries in list			     */
	u32	       list;    /* (drm_buf_desc_t *) */ 
} drm32_buf_info_t;
#define DRM32_IOCTL_INFO_BUFS  DRM_IOWR(0x18, drm32_buf_info_t)

static int drm32_info_bufs(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	drm32_buf_info_t __user *uarg = (drm32_buf_info_t __user *)arg;
	drm_buf_info_t __user *p = compat_alloc_user_space(sizeof(*p));
	compat_uptr_t addr;
	int n;
	int ret;

	if (get_user(n, &uarg->count) || put_user(n, &p->count) ||
	    get_user(addr, &uarg->list) || put_user(compat_ptr(addr), &p->list))
		return -EFAULT;

	ret = sys_ioctl(fd, DRM_IOCTL_INFO_BUFS, (unsigned long)p);
	if (ret)
		return ret;

	if (get_user(n, &p->count) || put_user(n, &uarg->count))
		return -EFAULT;

	return 0;
}

typedef struct drm32_buf_free {
	int	       count;
	u32	       list;	/* (int *) */
} drm32_buf_free_t;
#define DRM32_IOCTL_FREE_BUFS  DRM_IOW( 0x1a, drm32_buf_free_t)

static int drm32_free_bufs(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	drm32_buf_free_t __user *uarg = (drm32_buf_free_t __user *)arg;
	drm_buf_free_t __user *p = compat_alloc_user_space(sizeof(*p));
	compat_uptr_t addr;
	int n;

	if (get_user(n, &uarg->count) || put_user(n, &p->count) ||
	    get_user(addr, &uarg->list) || put_user(compat_ptr(addr), &p->list))
		return -EFAULT;

	return sys_ioctl(fd, DRM_IOCTL_FREE_BUFS, (unsigned long)p);
}

typedef struct drm32_buf_pub {
	int		  idx;	       /* Index into master buflist	     */
	int		  total;       /* Buffer size			     */
	int		  used;	       /* Amount of buffer in use (for DMA)  */
	u32		  address;     /* Address of buffer (void *)	     */
} drm32_buf_pub_t;

typedef struct drm32_buf_map {
	int	      count;	/* Length of buflist			    */
	u32	      virtual;	/* Mmaped area in user-virtual (void *)	    */
	u32 	      list;	/* Buffer information (drm_buf_pub_t *)	    */
} drm32_buf_map_t;
#define DRM32_IOCTL_MAP_BUFS   DRM_IOWR(0x19, drm32_buf_map_t)

static int drm32_map_bufs(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	drm32_buf_map_t __user *uarg = (drm32_buf_map_t __user *)arg;
	drm32_buf_pub_t __user *ulist;
	drm_buf_map_t __user *arg64;
	drm_buf_pub_t __user *list;
	int orig_count, ret, i;
	int n;
	compat_uptr_t addr;

	if (get_user(orig_count, &uarg->count))
		return -EFAULT;

	arg64 = compat_alloc_user_space(sizeof(drm_buf_map_t) +
				(size_t)orig_count * sizeof(drm_buf_pub_t));
	list = (void __user *)(arg64 + 1);

	if (put_user(orig_count, &arg64->count) ||
	    put_user(list, &arg64->list) ||
	    get_user(addr, &uarg->virtual) ||
	    put_user(compat_ptr(addr), &arg64->virtual) ||
	    get_user(addr, &uarg->list))
		return -EFAULT;

	ulist = compat_ptr(addr);

	for (i = 0; i < orig_count; i++) {
		if (get_user(n, &ulist[i].idx) ||
		    put_user(n, &list[i].idx) ||
		    get_user(n, &ulist[i].total) ||
		    put_user(n, &list[i].total) ||
		    get_user(n, &ulist[i].used) ||
		    put_user(n, &list[i].used) ||
		    get_user(addr, &ulist[i].address) ||
		    put_user(compat_ptr(addr), &list[i].address))
			return -EFAULT;
	}

	ret = sys_ioctl(fd, DRM_IOCTL_MAP_BUFS, (unsigned long) arg64);
	if (ret)
		return ret;

	for (i = 0; i < orig_count; i++) {
		void __user *p;
		if (get_user(n, &list[i].idx) ||
		    put_user(n, &ulist[i].idx) ||
		    get_user(n, &list[i].total) ||
		    put_user(n, &ulist[i].total) ||
		    get_user(n, &list[i].used) ||
		    put_user(n, &ulist[i].used) ||
		    get_user(p, &list[i].address) ||
		    put_user((unsigned long)p, &ulist[i].address))
			return -EFAULT;
	}

	if (get_user(n, &arg64->count) || put_user(n, &uarg->count))
		return -EFAULT;

	return 0;
}

typedef struct drm32_dma {
				/* Indices here refer to the offset into
				   buflist in drm_buf_get_t.  */
	int		context;	  /* Context handle		    */
	int		send_count;	  /* Number of buffers to send	    */
	u32		send_indices;	  /* List of handles to buffers (int *) */
	u32		send_sizes;	  /* Lengths of data to send (int *) */
	drm_dma_flags_t flags;		  /* Flags			    */
	int		request_count;	  /* Number of buffers requested    */
	int		request_size;	  /* Desired size for buffers	    */
	u32		request_indices;  /* Buffer information (int *)	    */
	u32		request_sizes;    /* (int *) */
	int		granted_count;	  /* Number of buffers granted	    */
} drm32_dma_t;
#define DRM32_IOCTL_DMA	     DRM_IOWR(0x29, drm32_dma_t)

/* RED PEN	The DRM layer blindly dereferences the send/request
 * 		index/size arrays even though they are userland
 * 		pointers.  -DaveM
 */
static int drm32_dma(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	drm32_dma_t __user *uarg = (drm32_dma_t __user *) arg;
	drm_dma_t __user *p = compat_alloc_user_space(sizeof(*p));
	compat_uptr_t addr;
	int ret;

	if (copy_in_user(p, uarg, 2 * sizeof(int)) ||
	    get_user(addr, &uarg->send_indices) ||
	    put_user(compat_ptr(addr), &p->send_indices) ||
	    get_user(addr, &uarg->send_sizes) ||
	    put_user(compat_ptr(addr), &p->send_sizes) ||
	    copy_in_user(&p->flags, &uarg->flags, sizeof(drm_dma_flags_t)) ||
	    copy_in_user(&p->request_count, &uarg->request_count, sizeof(int))||
	    copy_in_user(&p->request_size, &uarg->request_size, sizeof(int)) ||
	    get_user(addr, &uarg->request_indices) ||
	    put_user(compat_ptr(addr), &p->request_indices) ||
	    get_user(addr, &uarg->request_sizes) ||
	    put_user(compat_ptr(addr), &p->request_sizes) ||
	    copy_in_user(&p->granted_count, &uarg->granted_count, sizeof(int)))
		return -EFAULT;

	ret = sys_ioctl(fd, DRM_IOCTL_DMA, (unsigned long)p);
	if (ret)
		return ret;

	if (copy_in_user(uarg, p, 2 * sizeof(int)) ||
	    copy_in_user(&uarg->flags, &p->flags, sizeof(drm_dma_flags_t)) ||
	    copy_in_user(&uarg->request_count, &p->request_count, sizeof(int))||
	    copy_in_user(&uarg->request_size, &p->request_size, sizeof(int)) ||
	    copy_in_user(&uarg->granted_count, &p->granted_count, sizeof(int)))
		return -EFAULT;

	return 0;
}

typedef struct drm32_ctx_res {
	int		count;
	u32		contexts; /* (drm_ctx_t *) */
} drm32_ctx_res_t;
#define DRM32_IOCTL_RES_CTX    DRM_IOWR(0x26, drm32_ctx_res_t)

static int drm32_res_ctx(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	drm32_ctx_res_t __user *uarg = (drm32_ctx_res_t __user *) arg;
	drm_ctx_res_t __user *p = compat_alloc_user_space(sizeof(*p));
	compat_uptr_t addr;
	int ret;

	if (copy_in_user(p, uarg, sizeof(int)) ||
	    get_user(addr, &uarg->contexts) ||
	    put_user(compat_ptr(addr), &p->contexts))
		return -EFAULT;

	ret = sys_ioctl(fd, DRM_IOCTL_RES_CTX, (unsigned long)p);
	if (ret)
		return ret;

	if (copy_in_user(uarg, p, sizeof(int)))
		return -EFAULT;

	return 0;
}

#endif

typedef int (* ioctl32_handler_t)(unsigned int, unsigned int, unsigned long, struct file *);

#define COMPATIBLE_IOCTL(cmd)		HANDLE_IOCTL((cmd),sys_ioctl)
#define HANDLE_IOCTL(cmd,handler)	{ (cmd), (ioctl32_handler_t)(handler), NULL },
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
COMPATIBLE_IOCTL(KIOCTYPE)
COMPATIBLE_IOCTL(KIOCLAYOUT)
COMPATIBLE_IOCTL(KIOCGTRANS)
COMPATIBLE_IOCTL(KIOCTRANS)
COMPATIBLE_IOCTL(KIOCCMD)
COMPATIBLE_IOCTL(KIOCSDIRECT)
COMPATIBLE_IOCTL(KIOCSLED)
COMPATIBLE_IOCTL(KIOCGLED)
COMPATIBLE_IOCTL(KIOCSRATE)
COMPATIBLE_IOCTL(KIOCGRATE)
COMPATIBLE_IOCTL(VUIDSFORMAT)
COMPATIBLE_IOCTL(VUIDGFORMAT)
/* Little v, the video4linux ioctls */
COMPATIBLE_IOCTL(_IOR('p', 20, int[7])) /* RTCGET */
COMPATIBLE_IOCTL(_IOW('p', 21, int[7])) /* RTCSET */
COMPATIBLE_IOCTL(ENVCTRL_RD_WARNING_TEMPERATURE)
COMPATIBLE_IOCTL(ENVCTRL_RD_SHUTDOWN_TEMPERATURE)
COMPATIBLE_IOCTL(ENVCTRL_RD_CPU_TEMPERATURE)
COMPATIBLE_IOCTL(ENVCTRL_RD_FAN_STATUS)
COMPATIBLE_IOCTL(ENVCTRL_RD_VOLTAGE_STATUS)
COMPATIBLE_IOCTL(ENVCTRL_RD_SCSI_TEMPERATURE)
COMPATIBLE_IOCTL(ENVCTRL_RD_ETHERNET_TEMPERATURE)
COMPATIBLE_IOCTL(ENVCTRL_RD_MTHRBD_TEMPERATURE)
COMPATIBLE_IOCTL(ENVCTRL_RD_CPU_VOLTAGE)
COMPATIBLE_IOCTL(ENVCTRL_RD_GLOBALADDRESS)
/* COMPATIBLE_IOCTL(D7SIOCRD) same value as ENVCTRL_RD_VOLTAGE_STATUS */
COMPATIBLE_IOCTL(D7SIOCWR)
COMPATIBLE_IOCTL(D7SIOCTM)
/* OPENPROMIO, SunOS/Solaris only, the NetBSD one's have
 * embedded pointers in the arg which we'd need to clean up...
 */
COMPATIBLE_IOCTL(OPROMGETOPT)
COMPATIBLE_IOCTL(OPROMSETOPT)
COMPATIBLE_IOCTL(OPROMNXTOPT)
COMPATIBLE_IOCTL(OPROMSETOPT2)
COMPATIBLE_IOCTL(OPROMNEXT)
COMPATIBLE_IOCTL(OPROMCHILD)
COMPATIBLE_IOCTL(OPROMGETPROP)
COMPATIBLE_IOCTL(OPROMNXTPROP)
COMPATIBLE_IOCTL(OPROMU2P)
COMPATIBLE_IOCTL(OPROMGETCONS)
COMPATIBLE_IOCTL(OPROMGETFBNAME)
COMPATIBLE_IOCTL(OPROMGETBOOTARGS)
COMPATIBLE_IOCTL(OPROMSETCUR)
COMPATIBLE_IOCTL(OPROMPCI2NODE)
COMPATIBLE_IOCTL(OPROMPATH2NODE)
/* Big L */
COMPATIBLE_IOCTL(LOOP_SET_STATUS64)
COMPATIBLE_IOCTL(LOOP_GET_STATUS64)
/* Big A */
COMPATIBLE_IOCTL(AUDIO_GETINFO)
COMPATIBLE_IOCTL(AUDIO_SETINFO)
COMPATIBLE_IOCTL(AUDIO_DRAIN)
COMPATIBLE_IOCTL(AUDIO_GETDEV)
COMPATIBLE_IOCTL(AUDIO_GETDEV_SUNOS)
COMPATIBLE_IOCTL(AUDIO_FLUSH)
COMPATIBLE_IOCTL(AUTOFS_IOC_EXPIRE_MULTI)
#if defined(CONFIG_DRM) || defined(CONFIG_DRM_MODULE)
COMPATIBLE_IOCTL(DRM_IOCTL_GET_MAGIC)
COMPATIBLE_IOCTL(DRM_IOCTL_IRQ_BUSID)
COMPATIBLE_IOCTL(DRM_IOCTL_AUTH_MAGIC)
COMPATIBLE_IOCTL(DRM_IOCTL_BLOCK)
COMPATIBLE_IOCTL(DRM_IOCTL_UNBLOCK)
COMPATIBLE_IOCTL(DRM_IOCTL_CONTROL)
COMPATIBLE_IOCTL(DRM_IOCTL_ADD_BUFS)
COMPATIBLE_IOCTL(DRM_IOCTL_MARK_BUFS)
COMPATIBLE_IOCTL(DRM_IOCTL_ADD_CTX)
COMPATIBLE_IOCTL(DRM_IOCTL_RM_CTX)
COMPATIBLE_IOCTL(DRM_IOCTL_MOD_CTX)
COMPATIBLE_IOCTL(DRM_IOCTL_GET_CTX)
COMPATIBLE_IOCTL(DRM_IOCTL_SWITCH_CTX)
COMPATIBLE_IOCTL(DRM_IOCTL_NEW_CTX)
COMPATIBLE_IOCTL(DRM_IOCTL_ADD_DRAW)
COMPATIBLE_IOCTL(DRM_IOCTL_RM_DRAW)
COMPATIBLE_IOCTL(DRM_IOCTL_LOCK)
COMPATIBLE_IOCTL(DRM_IOCTL_UNLOCK)
COMPATIBLE_IOCTL(DRM_IOCTL_FINISH)
#endif /* DRM */
COMPATIBLE_IOCTL(WIOCSTART)
COMPATIBLE_IOCTL(WIOCSTOP)
COMPATIBLE_IOCTL(WIOCGSTAT)
/* And these ioctls need translation */
/* Note SIOCRTMSG is no longer, so this is safe and * the user would have seen just an -EINVAL anyways. */
HANDLE_IOCTL(FBIOPUTCMAP32, fbiogetputcmap)
HANDLE_IOCTL(FBIOGETCMAP32, fbiogetputcmap)
HANDLE_IOCTL(FBIOSCURSOR32, fbiogscursor)
#if defined(CONFIG_DRM) || defined(CONFIG_DRM_MODULE)
HANDLE_IOCTL(DRM32_IOCTL_VERSION, drm32_version)
HANDLE_IOCTL(DRM32_IOCTL_GET_UNIQUE, drm32_getsetunique)
HANDLE_IOCTL(DRM32_IOCTL_SET_UNIQUE, drm32_getsetunique)
HANDLE_IOCTL(DRM32_IOCTL_ADD_MAP, drm32_addmap)
HANDLE_IOCTL(DRM32_IOCTL_INFO_BUFS, drm32_info_bufs)
HANDLE_IOCTL(DRM32_IOCTL_FREE_BUFS, drm32_free_bufs)
HANDLE_IOCTL(DRM32_IOCTL_MAP_BUFS, drm32_map_bufs)
HANDLE_IOCTL(DRM32_IOCTL_DMA, drm32_dma)
HANDLE_IOCTL(DRM32_IOCTL_RES_CTX, drm32_res_ctx)
#endif /* DRM */
#if 0
HANDLE_IOCTL(RTC32_IRQP_READ, do_rtc_ioctl)
HANDLE_IOCTL(RTC32_IRQP_SET, do_rtc_ioctl)
HANDLE_IOCTL(RTC32_EPOCH_READ, do_rtc_ioctl)
HANDLE_IOCTL(RTC32_EPOCH_SET, do_rtc_ioctl)
#endif
/* take care of sizeof(sizeof()) breakage */
IOCTL_TABLE_END

int ioctl_table_size = ARRAY_SIZE(ioctl_start);
