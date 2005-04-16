/* $Id: ia32_ioctl.c,v 1.25 2002/10/11 07:17:06 ak Exp $
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *
 * Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 2001,2002  Andi Kleen, SuSE Labs 
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * ioctls.
 */

#define INCLUDES
#include <linux/syscalls.h>
#include "compat_ioctl.c"
#include <asm/mtrr.h>
#include <asm/ia32.h>

#define CODE
#include "compat_ioctl.c"
  
#ifndef TIOCGDEV
#define TIOCGDEV       _IOR('T',0x32, unsigned int)
#endif
static int tiocgdev(unsigned fd, unsigned cmd,  unsigned int __user *ptr) 
{ 

	struct file *file = fget(fd);
	struct tty_struct *real_tty;

	if (!file)
		return -EBADF;
	if (file->f_op->ioctl != tty_ioctl)
		return -EINVAL; 
	real_tty = (struct tty_struct *)file->private_data;
	if (!real_tty) 	
		return -EINVAL; 
	return put_user(new_encode_dev(tty_devnum(real_tty)), ptr); 
} 

#define RTC_IRQP_READ32	_IOR('p', 0x0b, unsigned int)	 /* Read IRQ rate   */
#define RTC_IRQP_SET32	_IOW('p', 0x0c, unsigned int)	 /* Set IRQ rate    */
#define RTC_EPOCH_READ32	_IOR('p', 0x0d, unsigned)	 /* Read epoch      */
#define RTC_EPOCH_SET32		_IOW('p', 0x0e, unsigned)	 /* Set epoch       */

static int rtc32_ioctl(unsigned fd, unsigned cmd, unsigned long arg) 
{ 
	unsigned long val;
	mm_segment_t oldfs = get_fs(); 
	int ret; 
	
	switch (cmd) { 
	case RTC_IRQP_READ32: 
		set_fs(KERNEL_DS); 
		ret = sys_ioctl(fd, RTC_IRQP_READ, (unsigned long)&val); 
		set_fs(oldfs); 
		if (!ret)
			ret = put_user(val, (unsigned int __user *) arg); 
		return ret; 

	case RTC_IRQP_SET32: 
		cmd = RTC_IRQP_SET; 
		break; 

	case RTC_EPOCH_READ32:
		set_fs(KERNEL_DS); 
		ret = sys_ioctl(fd, RTC_EPOCH_READ, (unsigned long) &val); 
		set_fs(oldfs); 
		if (!ret)
			ret = put_user(val, (unsigned int __user *) arg); 
		return ret; 

	case RTC_EPOCH_SET32:
		cmd = RTC_EPOCH_SET; 
		break; 
	} 
	return sys_ioctl(fd,cmd,arg); 
} 

/* /proc/mtrr ioctls */


struct mtrr_sentry32
{
    compat_ulong_t base;    /*  Base address     */
    compat_uint_t size;    /*  Size of region   */
    compat_uint_t type;     /*  Type of region   */
};

struct mtrr_gentry32
{
    compat_ulong_t regnum;   /*  Register number  */
    compat_uint_t base;    /*  Base address     */
    compat_uint_t size;    /*  Size of region   */
    compat_uint_t type;     /*  Type of region   */
};

#define	MTRR_IOCTL_BASE	'M'

#define MTRRIOC32_ADD_ENTRY        _IOW(MTRR_IOCTL_BASE,  0, struct mtrr_sentry32)
#define MTRRIOC32_SET_ENTRY        _IOW(MTRR_IOCTL_BASE,  1, struct mtrr_sentry32)
#define MTRRIOC32_DEL_ENTRY        _IOW(MTRR_IOCTL_BASE,  2, struct mtrr_sentry32)
#define MTRRIOC32_GET_ENTRY        _IOWR(MTRR_IOCTL_BASE, 3, struct mtrr_gentry32)
#define MTRRIOC32_KILL_ENTRY       _IOW(MTRR_IOCTL_BASE,  4, struct mtrr_sentry32)
#define MTRRIOC32_ADD_PAGE_ENTRY   _IOW(MTRR_IOCTL_BASE,  5, struct mtrr_sentry32)
#define MTRRIOC32_SET_PAGE_ENTRY   _IOW(MTRR_IOCTL_BASE,  6, struct mtrr_sentry32)
#define MTRRIOC32_DEL_PAGE_ENTRY   _IOW(MTRR_IOCTL_BASE,  7, struct mtrr_sentry32)
#define MTRRIOC32_GET_PAGE_ENTRY   _IOWR(MTRR_IOCTL_BASE, 8, struct mtrr_gentry32)
#define MTRRIOC32_KILL_PAGE_ENTRY  _IOW(MTRR_IOCTL_BASE,  9, struct mtrr_sentry32)


static int mtrr_ioctl32(unsigned int fd, unsigned int cmd, unsigned long arg)
{ 
	struct mtrr_gentry g;
	struct mtrr_sentry s;
	int get = 0, err = 0; 
	struct mtrr_gentry32 __user *g32 = (struct mtrr_gentry32 __user *)arg; 
	mm_segment_t oldfs = get_fs(); 

	switch (cmd) { 
#define SET(x) case MTRRIOC32_ ## x ## _ENTRY: cmd = MTRRIOC_ ## x ## _ENTRY; break 
#define GET(x) case MTRRIOC32_ ## x ## _ENTRY: cmd = MTRRIOC_ ## x ## _ENTRY; get=1; break
		SET(ADD);
		SET(SET); 
		SET(DEL);
		GET(GET); 
		SET(KILL);
		SET(ADD_PAGE); 
		SET(SET_PAGE); 
		SET(DEL_PAGE); 
		GET(GET_PAGE); 
		SET(KILL_PAGE); 
	} 
	
	if (get) { 
		err = get_user(g.regnum, &g32->regnum);
		err |= get_user(g.base, &g32->base);
		err |= get_user(g.size, &g32->size);
		err |= get_user(g.type, &g32->type); 

		arg = (unsigned long)&g; 
	} else { 
		struct mtrr_sentry32 __user *s32 = (struct mtrr_sentry32 __user *)arg;
		err = get_user(s.base, &s32->base);
		err |= get_user(s.size, &s32->size);
		err |= get_user(s.type, &s32->type);

		arg = (unsigned long)&s; 
	} 
	if (err) return err;
	
	set_fs(KERNEL_DS); 
	err = sys_ioctl(fd, cmd, arg); 
	set_fs(oldfs); 
		
	if (!err && get) { 
		err = put_user(g.base, &g32->base);
		err |= put_user(g.size, &g32->size);
		err |= put_user(g.regnum, &g32->regnum);
		err |= put_user(g.type, &g32->type); 
	} 
	return err;
} 

#define HANDLE_IOCTL(cmd,handler) { (cmd), (ioctl_trans_handler_t)(handler) }, 
#define COMPATIBLE_IOCTL(cmd) HANDLE_IOCTL(cmd,sys_ioctl)

struct ioctl_trans ioctl_start[] = { 
#include <linux/compat_ioctl.h>
#define DECLARES
#include "compat_ioctl.c"
COMPATIBLE_IOCTL(HDIO_SET_KEEPSETTINGS)
COMPATIBLE_IOCTL(HDIO_SCAN_HWIF)
COMPATIBLE_IOCTL(BLKRASET)
COMPATIBLE_IOCTL(0x4B50)   /* KDGHWCLK - not in the kernel, but don't complain */
COMPATIBLE_IOCTL(0x4B51)   /* KDSHWCLK - not in the kernel, but don't complain */
COMPATIBLE_IOCTL(FIOQSIZE)

/* And these ioctls need translation */
HANDLE_IOCTL(TIOCGDEV, tiocgdev)
/* realtime device */
HANDLE_IOCTL(RTC_IRQP_READ,  rtc32_ioctl)
HANDLE_IOCTL(RTC_IRQP_READ32,rtc32_ioctl)
HANDLE_IOCTL(RTC_IRQP_SET32, rtc32_ioctl)
HANDLE_IOCTL(RTC_EPOCH_READ32, rtc32_ioctl)
HANDLE_IOCTL(RTC_EPOCH_SET32, rtc32_ioctl)
/* take care of sizeof(sizeof()) breakage */
/* mtrr */
HANDLE_IOCTL(MTRRIOC32_ADD_ENTRY, mtrr_ioctl32)
HANDLE_IOCTL(MTRRIOC32_SET_ENTRY, mtrr_ioctl32)
HANDLE_IOCTL(MTRRIOC32_DEL_ENTRY, mtrr_ioctl32)
HANDLE_IOCTL(MTRRIOC32_GET_ENTRY, mtrr_ioctl32)
HANDLE_IOCTL(MTRRIOC32_KILL_ENTRY, mtrr_ioctl32)
HANDLE_IOCTL(MTRRIOC32_ADD_PAGE_ENTRY, mtrr_ioctl32)
HANDLE_IOCTL(MTRRIOC32_SET_PAGE_ENTRY, mtrr_ioctl32)
HANDLE_IOCTL(MTRRIOC32_DEL_PAGE_ENTRY, mtrr_ioctl32)
HANDLE_IOCTL(MTRRIOC32_GET_PAGE_ENTRY, mtrr_ioctl32)
HANDLE_IOCTL(MTRRIOC32_KILL_PAGE_ENTRY, mtrr_ioctl32)
}; 

int ioctl_table_size = ARRAY_SIZE(ioctl_start);

