/* $Id: ioctl32.c,v 1.5 2002/10/18 00:21:43 varenet Exp $
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *
 * Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * ioctls.
 */

#include <linux/syscalls.h>

#define INCLUDES
#include "compat_ioctl.c"

#include <asm/perf.h>
#include <asm/ioctls.h>

#define CODE
#include "compat_ioctl.c"

/* Use this to get at 32-bit user passed pointers. 
   See sys_sparc32.c for description about these. */
#define A(__x) ((unsigned long)(__x))
/* The same for use with copy_from_user() and copy_to_user(). */
#define B(__x) ((void *)(unsigned long)(__x))

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
	drm32_version_t *uversion = (drm32_version_t *)arg;
	char *name_ptr, *date_ptr, *desc_ptr;
	u32 tmp1, tmp2, tmp3;
	drm_version_t kversion;
	mm_segment_t old_fs;
	int ret;

	memset(&kversion, 0, sizeof(kversion));
	if (get_user(kversion.name_len, &uversion->name_len) ||
	    get_user(kversion.date_len, &uversion->date_len) ||
	    get_user(kversion.desc_len, &uversion->desc_len) ||
	    get_user(tmp1, &uversion->name) ||
	    get_user(tmp2, &uversion->date) ||
	    get_user(tmp3, &uversion->desc))
		return -EFAULT;

	name_ptr = (char *) A(tmp1);
	date_ptr = (char *) A(tmp2);
	desc_ptr = (char *) A(tmp3);

	ret = -ENOMEM;
	if (kversion.name_len && name_ptr) {
		kversion.name = kmalloc(kversion.name_len, GFP_KERNEL);
		if (!kversion.name)
			goto out;
	}
	if (kversion.date_len && date_ptr) {
		kversion.date = kmalloc(kversion.date_len, GFP_KERNEL);
		if (!kversion.date)
			goto out;
	}
	if (kversion.desc_len && desc_ptr) {
		kversion.desc = kmalloc(kversion.desc_len, GFP_KERNEL);
		if (!kversion.desc)
			goto out;
	}

        old_fs = get_fs();
	set_fs(KERNEL_DS);
        ret = sys_ioctl (fd, DRM_IOCTL_VERSION, (unsigned long)&kversion);
        set_fs(old_fs);

	if (!ret) {
		if ((kversion.name &&
		     copy_to_user(name_ptr, kversion.name, kversion.name_len)) ||
		    (kversion.date &&
		     copy_to_user(date_ptr, kversion.date, kversion.date_len)) ||
		    (kversion.desc &&
		     copy_to_user(desc_ptr, kversion.desc, kversion.desc_len)))
			ret = -EFAULT;
		if (put_user(kversion.version_major, &uversion->version_major) ||
		    put_user(kversion.version_minor, &uversion->version_minor) ||
		    put_user(kversion.version_patchlevel, &uversion->version_patchlevel) ||
		    put_user(kversion.name_len, &uversion->name_len) ||
		    put_user(kversion.date_len, &uversion->date_len) ||
		    put_user(kversion.desc_len, &uversion->desc_len))
			ret = -EFAULT;
	}

out:
	if (kversion.name)
		kfree(kversion.name);
	if (kversion.date)
		kfree(kversion.date);
	if (kversion.desc)
		kfree(kversion.desc);
	return ret;
}

typedef struct drm32_unique {
	int	unique_len;	  /* Length of unique			    */
	u32	unique;		  /* Unique name for driver instantiation   */
} drm32_unique_t;
#define DRM32_IOCTL_GET_UNIQUE DRM_IOWR(0x01, drm32_unique_t)
#define DRM32_IOCTL_SET_UNIQUE DRM_IOW( 0x10, drm32_unique_t)

static int drm32_getsetunique(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	drm32_unique_t *uarg = (drm32_unique_t *)arg;
	drm_unique_t karg;
	mm_segment_t old_fs;
	char *uptr;
	u32 tmp;
	int ret;

	if (get_user(karg.unique_len, &uarg->unique_len))
		return -EFAULT;
	karg.unique = NULL;

	if (get_user(tmp, &uarg->unique))
		return -EFAULT;

	uptr = (char *) A(tmp);

	if (uptr) {
		karg.unique = kmalloc(karg.unique_len, GFP_KERNEL);
		if (!karg.unique)
			return -ENOMEM;
		if (cmd == DRM32_IOCTL_SET_UNIQUE &&
		    copy_from_user(karg.unique, uptr, karg.unique_len)) {
			kfree(karg.unique);
			return -EFAULT;
		}
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	if (cmd == DRM32_IOCTL_GET_UNIQUE)
		ret = sys_ioctl (fd, DRM_IOCTL_GET_UNIQUE, (unsigned long)&karg);
	else
		ret = sys_ioctl (fd, DRM_IOCTL_SET_UNIQUE, (unsigned long)&karg);
        set_fs(old_fs);

	if (!ret) {
		if (cmd == DRM32_IOCTL_GET_UNIQUE &&
		    uptr != NULL &&
		    copy_to_user(uptr, karg.unique, karg.unique_len))
			ret = -EFAULT;
		if (put_user(karg.unique_len, &uarg->unique_len))
			ret = -EFAULT;
	}

	if (karg.unique != NULL)
		kfree(karg.unique);

	return ret;
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
	drm32_map_t *uarg = (drm32_map_t *) arg;
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

	karg.handle = (void *) A(tmp);

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
	drm32_buf_info_t *uarg = (drm32_buf_info_t *)arg;
	drm_buf_desc_t *ulist;
	drm_buf_info_t karg;
	mm_segment_t old_fs;
	int orig_count, ret;
	u32 tmp;

	if (get_user(karg.count, &uarg->count) ||
	    get_user(tmp, &uarg->list))
		return -EFAULT;

	ulist = (drm_buf_desc_t *) A(tmp);

	orig_count = karg.count;

	karg.list = kmalloc(karg.count * sizeof(drm_buf_desc_t), GFP_KERNEL);
	if (!karg.list)
		return -EFAULT;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, DRM_IOCTL_INFO_BUFS, (unsigned long) &karg);
	set_fs(old_fs);

	if (!ret) {
		if (karg.count <= orig_count &&
		    (copy_to_user(ulist, karg.list,
				  karg.count * sizeof(drm_buf_desc_t))))
			ret = -EFAULT;
		if (put_user(karg.count, &uarg->count))
			ret = -EFAULT;
	}

	kfree(karg.list);

	return ret;
}

typedef struct drm32_buf_free {
	int	       count;
	u32	       list;	/* (int *) */
} drm32_buf_free_t;
#define DRM32_IOCTL_FREE_BUFS  DRM_IOW( 0x1a, drm32_buf_free_t)

static int drm32_free_bufs(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	drm32_buf_free_t *uarg = (drm32_buf_free_t *)arg;
	drm_buf_free_t karg;
	mm_segment_t old_fs;
	int *ulist;
	int ret;
	u32 tmp;

	if (get_user(karg.count, &uarg->count) ||
	    get_user(tmp, &uarg->list))
		return -EFAULT;

	ulist = (int *) A(tmp);

	karg.list = kmalloc(karg.count * sizeof(int), GFP_KERNEL);
	if (!karg.list)
		return -ENOMEM;

	ret = -EFAULT;
	if (copy_from_user(karg.list, ulist, (karg.count * sizeof(int))))
		goto out;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, DRM_IOCTL_FREE_BUFS, (unsigned long) &karg);
	set_fs(old_fs);

out:
	kfree(karg.list);

	return ret;
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
	drm32_buf_map_t *uarg = (drm32_buf_map_t *)arg;
	drm32_buf_pub_t *ulist;
	drm_buf_map_t karg;
	mm_segment_t old_fs;
	int orig_count, ret, i;
	u32 tmp1, tmp2;

	if (get_user(karg.count, &uarg->count) ||
	    get_user(tmp1, &uarg->virtual) ||
	    get_user(tmp2, &uarg->list))
		return -EFAULT;

	karg.virtual = (void *) A(tmp1);
	ulist = (drm32_buf_pub_t *) A(tmp2);

	orig_count = karg.count;

	karg.list = kmalloc(karg.count * sizeof(drm_buf_pub_t), GFP_KERNEL);
	if (!karg.list)
		return -ENOMEM;

	ret = -EFAULT;
	for (i = 0; i < karg.count; i++) {
		if (get_user(karg.list[i].idx, &ulist[i].idx) ||
		    get_user(karg.list[i].total, &ulist[i].total) ||
		    get_user(karg.list[i].used, &ulist[i].used) ||
		    get_user(tmp1, &ulist[i].address))
			goto out;

		karg.list[i].address = (void *) A(tmp1);
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, DRM_IOCTL_MAP_BUFS, (unsigned long) &karg);
	set_fs(old_fs);

	if (!ret) {
		for (i = 0; i < orig_count; i++) {
			tmp1 = (u32) (long) karg.list[i].address;
			if (put_user(karg.list[i].idx, &ulist[i].idx) ||
			    put_user(karg.list[i].total, &ulist[i].total) ||
			    put_user(karg.list[i].used, &ulist[i].used) ||
			    put_user(tmp1, &ulist[i].address)) {
				ret = -EFAULT;
				goto out;
			}
		}
		if (put_user(karg.count, &uarg->count))
			ret = -EFAULT;
	}

out:
	kfree(karg.list);
	return ret;
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
 * 		indice/size arrays even though they are userland
 * 		pointers.  -DaveM
 */
static int drm32_dma(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	drm32_dma_t *uarg = (drm32_dma_t *) arg;
	int *u_si, *u_ss, *u_ri, *u_rs;
	drm_dma_t karg;
	mm_segment_t old_fs;
	int ret;
	u32 tmp1, tmp2, tmp3, tmp4;

	karg.send_indices = karg.send_sizes = NULL;
	karg.request_indices = karg.request_sizes = NULL;

	if (get_user(karg.context, &uarg->context) ||
	    get_user(karg.send_count, &uarg->send_count) ||
	    get_user(tmp1, &uarg->send_indices) ||
	    get_user(tmp2, &uarg->send_sizes) ||
	    get_user(karg.flags, &uarg->flags) ||
	    get_user(karg.request_count, &uarg->request_count) ||
	    get_user(karg.request_size, &uarg->request_size) ||
	    get_user(tmp3, &uarg->request_indices) ||
	    get_user(tmp4, &uarg->request_sizes) ||
	    get_user(karg.granted_count, &uarg->granted_count))
		return -EFAULT;

	u_si = (int *) A(tmp1);
	u_ss = (int *) A(tmp2);
	u_ri = (int *) A(tmp3);
	u_rs = (int *) A(tmp4);

	if (karg.send_count) {
		karg.send_indices = kmalloc(karg.send_count * sizeof(int), GFP_KERNEL);
		karg.send_sizes = kmalloc(karg.send_count * sizeof(int), GFP_KERNEL);

		ret = -ENOMEM;
		if (!karg.send_indices || !karg.send_sizes)
			goto out;

		ret = -EFAULT;
		if (copy_from_user(karg.send_indices, u_si,
				   (karg.send_count * sizeof(int))) ||
		    copy_from_user(karg.send_sizes, u_ss,
				   (karg.send_count * sizeof(int))))
			goto out;
	}

	if (karg.request_count) {
		karg.request_indices = kmalloc(karg.request_count * sizeof(int), GFP_KERNEL);
		karg.request_sizes = kmalloc(karg.request_count * sizeof(int), GFP_KERNEL);

		ret = -ENOMEM;
		if (!karg.request_indices || !karg.request_sizes)
			goto out;

		ret = -EFAULT;
		if (copy_from_user(karg.request_indices, u_ri,
				   (karg.request_count * sizeof(int))) ||
		    copy_from_user(karg.request_sizes, u_rs,
				   (karg.request_count * sizeof(int))))
			goto out;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, DRM_IOCTL_DMA, (unsigned long) &karg);
	set_fs(old_fs);

	if (!ret) {
		if (put_user(karg.context, &uarg->context) ||
		    put_user(karg.send_count, &uarg->send_count) ||
		    put_user(karg.flags, &uarg->flags) ||
		    put_user(karg.request_count, &uarg->request_count) ||
		    put_user(karg.request_size, &uarg->request_size) ||
		    put_user(karg.granted_count, &uarg->granted_count))
			ret = -EFAULT;

		if (karg.send_count) {
			if (copy_to_user(u_si, karg.send_indices,
					 (karg.send_count * sizeof(int))) ||
			    copy_to_user(u_ss, karg.send_sizes,
					 (karg.send_count * sizeof(int))))
				ret = -EFAULT;
		}
		if (karg.request_count) {
			if (copy_to_user(u_ri, karg.request_indices,
					 (karg.request_count * sizeof(int))) ||
			    copy_to_user(u_rs, karg.request_sizes,
					 (karg.request_count * sizeof(int))))
				ret = -EFAULT;
		}
	}

out:
	if (karg.send_indices)
		kfree(karg.send_indices);
	if (karg.send_sizes)
		kfree(karg.send_sizes);
	if (karg.request_indices)
		kfree(karg.request_indices);
	if (karg.request_sizes)
		kfree(karg.request_sizes);

	return ret;
}

typedef struct drm32_ctx_res {
	int		count;
	u32		contexts; /* (drm_ctx_t *) */
} drm32_ctx_res_t;
#define DRM32_IOCTL_RES_CTX    DRM_IOWR(0x26, drm32_ctx_res_t)

static int drm32_res_ctx(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	drm32_ctx_res_t *uarg = (drm32_ctx_res_t *) arg;
	drm_ctx_t *ulist;
	drm_ctx_res_t karg;
	mm_segment_t old_fs;
	int orig_count, ret;
	u32 tmp;

	karg.contexts = NULL;
	if (get_user(karg.count, &uarg->count) ||
	    get_user(tmp, &uarg->contexts))
		return -EFAULT;

	ulist = (drm_ctx_t *) A(tmp);

	orig_count = karg.count;
	if (karg.count && ulist) {
		karg.contexts = kmalloc((karg.count * sizeof(drm_ctx_t)), GFP_KERNEL);
		if (!karg.contexts)
			return -ENOMEM;
		if (copy_from_user(karg.contexts, ulist,
				   (karg.count * sizeof(drm_ctx_t)))) {
			kfree(karg.contexts);
			return -EFAULT;
		}
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, DRM_IOCTL_RES_CTX, (unsigned long) &karg);
	set_fs(old_fs);

	if (!ret) {
		if (orig_count) {
			if (copy_to_user(ulist, karg.contexts,
					 (orig_count * sizeof(drm_ctx_t))))
				ret = -EFAULT;
		}
		if (put_user(karg.count, &uarg->count))
			ret = -EFAULT;
	}

	if (karg.contexts)
		kfree(karg.contexts);

	return ret;
}

#endif

#define HANDLE_IOCTL(cmd, handler) { cmd, (ioctl_trans_handler_t)handler, NULL },
#define COMPATIBLE_IOCTL(cmd) HANDLE_IOCTL(cmd, sys_ioctl) 

#define IOCTL_TABLE_START  struct ioctl_trans ioctl_start[] = {
#define IOCTL_TABLE_END    };

IOCTL_TABLE_START
#include <linux/compat_ioctl.h>

#define DECLARES
#include "compat_ioctl.c"

/* Might be moved to compat_ioctl.h with some ifdefs... */
COMPATIBLE_IOCTL(TIOCSTART)
COMPATIBLE_IOCTL(TIOCSTOP)
COMPATIBLE_IOCTL(TIOCSLTC)

/* PA-specific ioctls */
COMPATIBLE_IOCTL(PA_PERF_ON)
COMPATIBLE_IOCTL(PA_PERF_OFF)
COMPATIBLE_IOCTL(PA_PERF_VERSION)

/* And these ioctls need translation */
HANDLE_IOCTL(SIOCGPPPSTATS, dev_ifsioc)
HANDLE_IOCTL(SIOCGPPPCSTATS, dev_ifsioc)
HANDLE_IOCTL(SIOCGPPPVER, dev_ifsioc)

#if defined(CONFIG_GEN_RTC)
COMPATIBLE_IOCTL(RTC_AIE_ON)
COMPATIBLE_IOCTL(RTC_AIE_OFF)
COMPATIBLE_IOCTL(RTC_UIE_ON)
COMPATIBLE_IOCTL(RTC_UIE_OFF)
COMPATIBLE_IOCTL(RTC_PIE_ON)
COMPATIBLE_IOCTL(RTC_PIE_OFF)
COMPATIBLE_IOCTL(RTC_WIE_ON)
COMPATIBLE_IOCTL(RTC_WIE_OFF)
COMPATIBLE_IOCTL(RTC_ALM_SET)   /* struct rtc_time only has ints */
COMPATIBLE_IOCTL(RTC_ALM_READ)  /* struct rtc_time only has ints */
COMPATIBLE_IOCTL(RTC_RD_TIME)   /* struct rtc_time only has ints */
COMPATIBLE_IOCTL(RTC_SET_TIME)  /* struct rtc_time only has ints */
HANDLE_IOCTL(RTC_IRQP_READ, w_long)
COMPATIBLE_IOCTL(RTC_IRQP_SET)
HANDLE_IOCTL(RTC_EPOCH_READ, w_long)
COMPATIBLE_IOCTL(RTC_EPOCH_SET)
#endif

#if defined(CONFIG_DRM) || defined(CONFIG_DRM_MODULE)
HANDLE_IOCTL(DRM32_IOCTL_VERSION, drm32_version);
HANDLE_IOCTL(DRM32_IOCTL_GET_UNIQUE, drm32_getsetunique);
HANDLE_IOCTL(DRM32_IOCTL_SET_UNIQUE, drm32_getsetunique);
HANDLE_IOCTL(DRM32_IOCTL_ADD_MAP, drm32_addmap);
HANDLE_IOCTL(DRM32_IOCTL_INFO_BUFS, drm32_info_bufs);
HANDLE_IOCTL(DRM32_IOCTL_FREE_BUFS, drm32_free_bufs);
HANDLE_IOCTL(DRM32_IOCTL_MAP_BUFS, drm32_map_bufs);
HANDLE_IOCTL(DRM32_IOCTL_DMA, drm32_dma);
HANDLE_IOCTL(DRM32_IOCTL_RES_CTX, drm32_res_ctx);
#endif /* DRM */
IOCTL_TABLE_END

int ioctl_table_size = ARRAY_SIZE(ioctl_start);
