/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *
 ******************************************************************************/

#define _OSDEP_SERVICE_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <linux/vmalloc.h>
#include <rtw_ioctl_set.h>
/*
* Translate the OS dependent @param error_code to OS independent RTW_STATUS_CODE
* @return: one of RTW_STATUS_CODE
*/
inline int RTW_STATUS_CODE(int error_code) {
	if (error_code >=0)
		return _SUCCESS;

	switch (error_code) {
		default:
			return _FAIL;
	}
}

u32 rtw_atoi(u8* s)
{

	int num=0,flag=0;
	int i;
	for (i=0;i<=strlen(s);i++)
	{
	  if (s[i] >= '0' && s[i] <= '9')
		 num = num * 10 + s[i] -'0';
	  else if (s[0] == '-' && i==0)
		 flag =1;
	  else
		  break;
	 }

	if (flag == 1)
	   num = num * -1;

	 return(num);
}

inline u8* _rtw_vmalloc(u32 sz)
{
	u8	*pbuf;

	pbuf = vmalloc(sz);
	return pbuf;
}

int	_rtw_memcmp(void *dst, void *src, u32 sz)
{
/* under Linux/GNU/GLibc, the return value of memcmp for two same mem. chunk is 0 */
	if (!(memcmp(dst, src, sz)))
		return true;
	else
		return false;
}

/*
For the following list_xxx operations,
caller must guarantee the atomic context.
Otherwise, there will be racing condition.
*/
u32	rtw_is_list_empty(struct list_head *phead)
{
	if (list_empty(phead))
		return true;
	else
		return false;
}

void rtw_list_insert_head(struct list_head *plist, struct list_head *phead)
{
	list_add(plist, phead);
}

void rtw_list_insert_tail(struct list_head *plist, struct list_head *phead)
{
	list_add_tail(plist, phead);
}

/* Caller must check if the list is empty before calling list_del_init */

void _rtw_init_sema(struct  semaphore *sema, int init_val)
{
	sema_init(sema, init_val);
}

void _rtw_free_sema(struct  semaphore *sema)
{
}

void _rtw_up_sema(struct  semaphore *sema)
{
	up(sema);
}

u32 _rtw_down_sema(struct  semaphore *sema)
{
	if (down_interruptible(sema))
		return _FAIL;
	else
		return _SUCCESS;
}

void	_rtw_mutex_init(_mutex *pmutex)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	mutex_init(pmutex);
#else
	init_MUTEX(pmutex);
#endif
}

void	_rtw_mutex_free(_mutex *pmutex)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	mutex_destroy(pmutex);
#endif
}

void	_rtw_spinlock_init(spinlock_t *plock)
{
	spin_lock_init(plock);
}

void	_rtw_spinlock_free(spinlock_t *plock)
{
}

inline void	_rtw_spinlock(spinlock_t *plock)
{
	spin_lock(plock);
}

inline void	_rtw_spinunlock(spinlock_t *plock)
{
	spin_unlock(plock);
}

inline void	_rtw_spinlock_ex(spinlock_t *plock)
{
	spin_lock(plock);
}

inline void	_rtw_spinunlock_ex(spinlock_t *plock)
{
	spin_unlock(plock);
}

void	_rtw_init_queue(struct __queue *pqueue)
{

	INIT_LIST_HEAD(&(pqueue->queue));

	_rtw_spinlock_init(&(pqueue->lock));
}

u32	  _rtw_queue_empty(struct __queue *pqueue)
{
	return (rtw_is_list_empty(&(pqueue->queue)));
}

u32 rtw_end_of_queue_search(struct list_head *head, struct list_head *plist)
{
	if (head == plist)
		return true;
	else
		return false;
}

u32	rtw_get_current_time(void)
{
	return jiffies;
}

inline u32 rtw_systime_to_ms(u32 systime)
{
	return systime * 1000 / HZ;
}

inline u32 rtw_ms_to_systime(u32 ms)
{
	return ms * HZ / 1000;
}

/*  the input parameter start use the same unit as returned by rtw_get_current_time */
inline s32 rtw_get_passing_time_ms(u32 start)
{
	return rtw_systime_to_ms(jiffies-start);
}

inline s32 rtw_get_time_interval_ms(u32 start, u32 end)
{
	return rtw_systime_to_ms(end-start);
}

void rtw_sleep_schedulable(int ms)
{
    u32 delta;

    delta = (ms * HZ)/1000;/* ms) */
    if (delta == 0) {
        delta = 1;/*  1 ms */
    }
    set_current_state(TASK_INTERRUPTIBLE);
    if (schedule_timeout(delta) != 0) {
        return ;
    }
    return;
}

void rtw_msleep_os(int ms)
{
	msleep((unsigned int)ms);
}

void rtw_usleep_os(int us)
{
      if (1 < (us/1000))
                msleep(1);
      else
		msleep((us/1000) + 1);
}

#ifdef DBG_DELAY_OS
void _rtw_mdelay_os(int ms, const char *func, const int line)
{
	DBG_8192D("%s:%d %s(%d)\n", func, line, __func__, ms);
	mdelay((unsigned long)ms);
}

void _rtw_udelay_os(int us, const char *func, const int line)
{
	DBG_8192D("%s:%d %s(%d)\n", func, line, __func__, us);
      udelay((unsigned long)us);
}
#else
void rtw_mdelay_os(int ms)
{
	mdelay((unsigned long)ms);
}

void rtw_udelay_os(int us)
{
      udelay((unsigned long)us);
}
#endif

void rtw_yield_os(void)
{
	yield();
}

#define RTW_SUSPEND_LOCK_NAME "rtw_wifi"

#ifdef CONFIG_WAKELOCK
static struct wake_lock rtw_suspend_lock;
#elif defined(CONFIG_ANDROID_POWER)
static android_suspend_lock_t rtw_suspend_lock ={
	.name = RTW_SUSPEND_LOCK_NAME
};
#endif

inline void rtw_suspend_lock_init(void)
{
	#ifdef CONFIG_WAKELOCK
	wake_lock_init(&rtw_suspend_lock, WAKE_LOCK_SUSPEND, RTW_SUSPEND_LOCK_NAME);
	#elif defined(CONFIG_ANDROID_POWER)
	android_init_suspend_lock(&rtw_suspend_lock);
	#endif
}

inline void rtw_suspend_lock_uninit(void)
{
	#ifdef CONFIG_WAKELOCK
	wake_lock_destroy(&rtw_suspend_lock);
	#elif defined(CONFIG_ANDROID_POWER)
	android_uninit_suspend_lock(&rtw_suspend_lock);
	#endif
}

inline void rtw_lock_suspend(void)
{
	#ifdef CONFIG_WAKELOCK
	wake_lock(&rtw_suspend_lock);
	#elif defined(CONFIG_ANDROID_POWER)
	android_lock_suspend(&rtw_suspend_lock);
	#endif
}

inline void rtw_unlock_suspend(void)
{
	#ifdef CONFIG_WAKELOCK
	wake_unlock(&rtw_suspend_lock);
	#elif defined(CONFIG_ANDROID_POWER)
	android_unlock_suspend(&rtw_suspend_lock);
	#endif
}

inline void ATOMIC_SET(ATOMIC_T *v, int i)
{
	atomic_set(v,i);
}

inline int ATOMIC_READ(ATOMIC_T *v)
{
	return atomic_read(v);
}

inline void ATOMIC_ADD(ATOMIC_T *v, int i)
{
	atomic_add(i,v);
}

inline void ATOMIC_SUB(ATOMIC_T *v, int i)
{
	atomic_sub(i,v);
}

inline void ATOMIC_INC(ATOMIC_T *v)
{
	atomic_inc(v);
}

inline void ATOMIC_DEC(ATOMIC_T *v)
{
	atomic_dec(v);
}

inline int ATOMIC_ADD_RETURN(ATOMIC_T *v, int i)
{
	return atomic_add_return(i,v);
}

inline int ATOMIC_SUB_RETURN(ATOMIC_T *v, int i)
{
	return atomic_sub_return(i,v);
}

inline int ATOMIC_INC_RETURN(ATOMIC_T *v)
{
	return atomic_inc_return(v);
}

inline int ATOMIC_DEC_RETURN(ATOMIC_T *v)
{
	return atomic_dec_return(v);
}

/*
* Open a file with the specific @param path, @param flag, @param mode
* @param fpp the pointer of struct file pointer to get struct file pointer while file opening is success
* @param path the path of the file to open
* @param flag file operation flags, please refer to linux document
* @param mode please refer to linux document
* @return Linux specific error code
*/
static int openFile(struct file **fpp, char *path, int flag, int mode)
{
	struct file *fp;

	fp=filp_open(path, flag, mode);
	if (IS_ERR(fp)) {
		*fpp=NULL;
		return PTR_ERR(fp);
	}
	else {
		*fpp=fp;
		return 0;
	}
}

/*
* Close the file with the specific @param fp
* @param fp the pointer of struct file to close
* @return always 0
*/
static int closeFile(struct file *fp)
{
	filp_close(fp,NULL);
	return 0;
}

static int readFile(struct file *fp,char __user *buf,int len)
{
	int rlen=0, sum=0;

	if (!fp->f_op || !fp->f_op->read)
		return -EPERM;

	while (sum<len) {
		rlen=fp->f_op->read(fp,buf+sum,len-sum, &fp->f_pos);
		if (rlen>0)
			sum+=rlen;
		else if (0 != rlen)
			return rlen;
		else
			break;
	}

	return  sum;
}

static int writeFile(struct file *fp,char __user *buf,int len)
{
	int wlen=0, sum=0;

	if (!fp->f_op || !fp->f_op->write)
		return -EPERM;

	while (sum<len) {
		wlen=fp->f_op->write(fp, buf+sum, len-sum, &fp->f_pos);
		if (wlen>0)
			sum+=wlen;
		else if (0 != wlen)
			return wlen;
		else
			break;
	}

	return sum;
}

/*
* Test if the specifi @param path is a file and readable
* @param path the path of the file to test
* @return Linux specific error code
*/
static int isFileReadable(char *path)
{
	struct file *fp;
	int ret = 0;
	mm_segment_t oldfs;
	char __user buf;

	fp=filp_open(path, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		ret = PTR_ERR(fp);
	}
	else {
		oldfs = get_fs(); set_fs(get_ds());

		if (1!=readFile(fp, &buf, 1))
			ret = PTR_ERR(fp);

		set_fs(oldfs);
		filp_close(fp,NULL);
	}
	return ret;
}

/*
* Open the file with @param path and retrive the file content into memory starting from @param buf for @param sz at most
* @param path the path of the file to open and read
* @param buf the starting address of the buffer to store file content
* @param sz how many bytes to read at most
* @return the byte we've read, or Linux specific error code
*/
static int retriveFromFile(char *path, u8 __user *buf, u32 sz)
{
	int ret =-1;
	mm_segment_t oldfs;
	struct file *fp;

	if (path && buf) {
		if (0 == (ret=openFile(&fp,path, O_RDONLY, 0))) {
			DBG_8192D("%s openFile path:%s fp=%p\n",__func__, path ,fp);

			oldfs = get_fs(); set_fs(get_ds());
			ret=readFile(fp, buf, sz);
			set_fs(oldfs);
			closeFile(fp);

			DBG_8192D("%s readFile, ret:%d\n",__func__, ret);

		} else {
			DBG_8192D("%s openFile path:%s Fail, ret:%d\n",__func__, path, ret);
		}
	} else {
		DBG_8192D("%s NULL pointer\n",__func__);
		ret =  -EINVAL;
	}
	return ret;
}

/*
* Open the file with @param path and wirte @param sz byte of data starting from @param buf into the file
* @param path the path of the file to open and write
* @param buf the starting address of the data to write into file
* @param sz how many bytes to write at most
* @return the byte we've written, or Linux specific error code
*/
static int storeToFile(char *path, u8 __user *buf, u32 sz)
{
	int ret =0;
	mm_segment_t oldfs;
	struct file *fp;

	if (path && buf) {
		if (0 == (ret=openFile(&fp, path, O_CREAT|O_WRONLY, 0666))) {
			DBG_8192D("%s openFile path:%s fp=%p\n",__func__, path ,fp);

			oldfs = get_fs(); set_fs(get_ds());
			ret=writeFile(fp, buf, sz);
			set_fs(oldfs);
			closeFile(fp);

			DBG_8192D("%s writeFile, ret:%d\n",__func__, ret);

		} else {
			DBG_8192D("%s openFile path:%s Fail, ret:%d\n",__func__, path, ret);
		}
	} else {
		DBG_8192D("%s NULL pointer\n",__func__);
		ret =  -EINVAL;
	}
	return ret;
}

/*
* Test if the specifi @param path is a file and readable
* @param path the path of the file to test
* @return true or false
*/
int rtw_is_file_readable(char *path)
{
	if (isFileReadable(path) == 0)
		return true;
	else
		return false;
}

/*
* Open the file with @param path and retrive the file content into memory starting from @param buf for @param sz at most
* @param path the path of the file to open and read
* @param buf the starting address of the buffer to store file content
* @param sz how many bytes to read at most
* @return the byte we've read
*/
int rtw_retrive_from_file(char *path, u8 __user *buf, u32 sz)
{
	int ret =retriveFromFile(path, buf, sz);
	return ret>=0?ret:0;
}

/*
* Open the file with @param path and wirte @param sz byte of data starting from @param buf into the file
* @param path the path of the file to open and write
* @param buf the starting address of the data to write into file
* @param sz how many bytes to write at most
* @return the byte we've written
*/
int rtw_store_to_file(char *path, u8 __user *buf, u32 sz)
{
	int ret =storeToFile(path, buf, sz);
	return ret>=0?ret:0;
}

struct net_device *rtw_alloc_etherdev_with_old_priv(int sizeof_priv, void *old_priv)
{
	struct net_device *pnetdev;
	struct rtw_netdev_priv_indicator *pnpi;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
	pnetdev = alloc_etherdev_mq(sizeof(struct rtw_netdev_priv_indicator), 4);
#else
	pnetdev = alloc_etherdev(sizeof(struct rtw_netdev_priv_indicator));
#endif
	if (!pnetdev)
		goto RETURN;

	pnpi = netdev_priv(pnetdev);
	pnpi->priv=old_priv;
	pnpi->sizeof_priv=sizeof_priv;

RETURN:
	return pnetdev;
}

struct net_device *rtw_alloc_etherdev(int sizeof_priv)
{
	struct net_device *pnetdev;
	struct rtw_netdev_priv_indicator *pnpi;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
	pnetdev = alloc_etherdev_mq(sizeof(struct rtw_netdev_priv_indicator), 4);
#else
	pnetdev = alloc_etherdev(sizeof(struct rtw_netdev_priv_indicator));
#endif
	if (!pnetdev)
		goto RETURN;

	pnpi = netdev_priv(pnetdev);

	pnpi->priv = vzalloc(sizeof_priv);
	if (!pnpi->priv) {
		free_netdev(pnetdev);
		pnetdev = NULL;
		goto RETURN;
	}

	pnpi->sizeof_priv=sizeof_priv;
RETURN:
	return pnetdev;
}

void rtw_free_netdev(struct net_device * netdev)
{
	struct rtw_netdev_priv_indicator *pnpi;

	if (!netdev)
		goto RETURN;

	pnpi = netdev_priv(netdev);

	if (!pnpi->priv)
		goto RETURN;

	vfree(pnpi->priv);
	free_netdev(netdev);

RETURN:
	return;
}

/*
* Jeff: this function should be called under ioctl (rtnl_lock is accquired) while
* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
*/
int rtw_change_ifname(struct rtw_adapter *padapter, const char *ifname)
{
	struct net_device *pnetdev;
	struct net_device *cur_pnetdev;
	struct rereg_nd_name_data *rereg_priv;
	int ret;

	if (!padapter)
		goto error;

	cur_pnetdev = padapter->pnetdev;
	rereg_priv = &padapter->rereg_nd_name_priv;

	/* free the old_pnetdev */
	if (rereg_priv->old_pnetdev) {
		free_netdev(rereg_priv->old_pnetdev);
		rereg_priv->old_pnetdev = NULL;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26))
	if (!rtnl_is_locked())
		unregister_netdev(cur_pnetdev);
	else
#endif
		unregister_netdevice(cur_pnetdev);

	rtw_proc_remove_one(cur_pnetdev);

	rereg_priv->old_pnetdev=cur_pnetdev;

	pnetdev = rtw_init_netdev(padapter);
	if (!pnetdev)  {
		ret = -1;
		goto error;
	}

	SET_NETDEV_DEV(pnetdev, dvobj_to_dev(adapter_to_dvobj(padapter)));

	rtw_init_netdev_name(pnetdev, ifname);

	memcpy(pnetdev->dev_addr, padapter->eeprompriv.mac_addr, ETH_ALEN);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26))
	if (!rtnl_is_locked())
		ret = register_netdev(pnetdev);
	else
#endif
		ret = register_netdevice(pnetdev);

	if (ret != 0) {
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("register_netdev() failed\n"));
		goto error;
	}

	rtw_proc_init_one(pnetdev);

	return 0;

error:

	return -1;
}

u64 rtw_modular64(u64 x, u64 y)
{
	return do_div(x, y);
}

u64 rtw_division64(u64 x, u64 y)
{
	do_div(x, y);
	return x;
}

void rtw_buf_free(u8 **buf, u32 *buf_len)
{
	if (!buf || !buf_len)
		return;

	if (*buf) {
		*buf_len = 0;
		kfree(*buf);
		*buf = NULL;
	}
}

void rtw_buf_update(u8 **buf, u32 *buf_len, u8 *src, u32 src_len)
{
	u32 ori_len = 0, dup_len = 0;
	u8 *ori = NULL;
	u8 *dup = NULL;

	if (!buf || !buf_len)
		return;

	if (!src || !src_len)
		goto keep_ori;

	/* duplicate src */
	dup = kmalloc(src_len, GFP_ATOMIC);
	if (dup) {
		dup_len = src_len;
		memcpy(dup, src, dup_len);
	}

keep_ori:
	ori = *buf;
	ori_len = *buf_len;

	/* replace buf with dup */
	*buf_len = 0;
	*buf = dup;
	*buf_len = dup_len;

	/* free ori */
	if (ori && ori_len > 0)
		kfree(ori);
}

/**
 * rtw_cbuf_full - test if cbuf is full
 * @cbuf: pointer of struct rtw_cbuf
 *
 * Returns: true if cbuf is full
 */
inline bool rtw_cbuf_full(struct rtw_cbuf *cbuf)
{
	return (cbuf->write == cbuf->read-1)? true : false;
}

/**
 * rtw_cbuf_empty - test if cbuf is empty
 * @cbuf: pointer of struct rtw_cbuf
 *
 * Returns: true if cbuf is empty
 */
inline bool rtw_cbuf_empty(struct rtw_cbuf *cbuf)
{
	return (cbuf->write == cbuf->read)? true : false;
}

/**
 * rtw_cbuf_push - push a pointer into cbuf
 * @cbuf: pointer of struct rtw_cbuf
 * @buf: pointer to push in
 *
 * Lock free operation, be careful of the use scheme
 * Returns: true push success
 */
bool rtw_cbuf_push(struct rtw_cbuf *cbuf, void *buf)
{
	if (rtw_cbuf_full(cbuf))
		return _FAIL;

	cbuf->bufs[cbuf->write] = buf;
	cbuf->write = (cbuf->write+1)%cbuf->size;

	return _SUCCESS;
}

/**
 * rtw_cbuf_pop - pop a pointer from cbuf
 * @cbuf: pointer of struct rtw_cbuf
 *
 * Lock free operation, be careful of the use scheme
 * Returns: pointer popped out
 */
void *rtw_cbuf_pop(struct rtw_cbuf *cbuf)
{
	void *buf;
	if (rtw_cbuf_empty(cbuf))
		return NULL;

	buf = cbuf->bufs[cbuf->read];
	cbuf->read = (cbuf->read+1)%cbuf->size;

	return buf;
}

/**
 * rtw_cbuf_alloc - allocte a rtw_cbuf with given size and do initialization
 * @size: size of pointer
 *
 * Returns: pointer of srtuct rtw_cbuf, NULL for allocation failure
 */
struct rtw_cbuf *rtw_cbuf_alloc(u32 size)
{
	struct rtw_cbuf *cbuf;

	cbuf = (struct rtw_cbuf *)kmalloc(sizeof(*cbuf) + sizeof(void*) * size,
					  GFP_KERNEL);

	if (cbuf) {
		cbuf->write = cbuf->read = 0;
		cbuf->size = size;
	}

	return cbuf;
}

/**
 * rtw_cbuf_free - free the given rtw_cbuf
 * @cbuf: pointer of struct rtw_cbuf to free
 */
void rtw_cbuf_free(struct rtw_cbuf *cbuf)
{
	kfree(cbuf);
}
