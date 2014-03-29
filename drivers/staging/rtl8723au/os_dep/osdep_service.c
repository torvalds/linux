/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
 ******************************************************************************/


#define _OSDEP_SERVICE_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <linux/vmalloc.h>

#define RT_TAG	('1178')

/*
* Translate the OS dependent @param error_code to OS independent RTW_STATUS_CODE23a
* @return: one of RTW_STATUS_CODE23a
*/
inline int RTW_STATUS_CODE23a(int error_code)
{
	if (error_code >= 0)
		return _SUCCESS;
	return _FAIL;
}

inline u8 *_rtw_vmalloc(u32 sz)
{
	u8	*pbuf;
	pbuf = vmalloc(sz);

	return pbuf;
}

inline u8 *_rtw_zvmalloc(u32 sz)
{
	u8	*pbuf;
	pbuf = _rtw_vmalloc(sz);
	if (pbuf != NULL)
		memset(pbuf, 0, sz);

	return pbuf;
}

inline void _rtw_vmfree(u8 *pbuf, u32 sz)
{
	vfree(pbuf);
}

void _rtw_init_queue23a(struct rtw_queue *pqueue)
{
	INIT_LIST_HEAD(&pqueue->queue);
	spin_lock_init(&pqueue->lock);
}

u32 _rtw_queue_empty23a(struct rtw_queue *pqueue)
{
	if (list_empty(&pqueue->queue))
		return true;
	else
		return false;
}

u32	rtw_get_current_time(void)
{
	return jiffies;
}

inline u32 rtw_systime_to_ms23a(u32 systime)
{
	return systime * 1000 / HZ;
}

inline u32 rtw_ms_to_systime23a(u32 ms)
{
	return ms * HZ / 1000;
}

/*  the input parameter start use the same unit as returned
 * by rtw_get_current_time
 */
inline s32 rtw_get_passing_time_ms23a(u32 start)
{
	return rtw_systime_to_ms23a(jiffies-start);
}

inline s32 rtw_get_time_interval_ms23a(u32 start, u32 end)
{
	return rtw_systime_to_ms23a(end-start);
}

#define RTW_SUSPEND_LOCK_NAME "rtw_wifi"

inline void rtw_suspend_lock_init(void)
{
}

inline void rtw_suspend_lock_uninit(void)
{
}

inline void rtw_lock_suspend(void)
{
}

inline void rtw_unlock_suspend(void)
{
}

/* Open a file with the specific @param path, @param flag, @param mode
 * @param fpp the pointer of struct file pointer to get struct
 * file pointer while file opening is success
 * @param path the path of the file to open
 * @param flag file operation flags, please refer to linux document
 * @param mode please refer to linux document
 * @return Linux specific error code
 */
static int openFile(struct file **fpp, char *path, int flag, int mode)
{
	struct file *fp;

	fp = filp_open(path, flag, mode);
	if (IS_ERR(fp)) {
		*fpp = NULL;
		return PTR_ERR(fp);
	} else {
		*fpp = fp;
		return 0;
	}
}

/* Close the file with the specific @param fp
 * @param fp the pointer of struct file to close
 * @return always 0
 */
static int closeFile(struct file *fp)
{
	filp_close(fp, NULL);
	return 0;
}

static int readFile(struct file *fp, char *buf, int len)
{
	int rlen = 0, sum = 0;

	if (!fp->f_op || !fp->f_op->read)
		return -EPERM;

	while (sum < len) {
		rlen = fp->f_op->read(fp, buf+sum, len-sum, &fp->f_pos);
		if (rlen > 0)
			sum += rlen;
		else if (0 != rlen)
			return rlen;
		else
			break;
	}
	return  sum;
}

static int writeFile(struct file *fp, char *buf, int len)
{
	int wlen = 0, sum = 0;

	if (!fp->f_op || !fp->f_op->write)
		return -EPERM;

	while (sum < len) {
		wlen = fp->f_op->write(fp, buf+sum, len-sum, &fp->f_pos);
		if (wlen > 0)
			sum += wlen;
		else if (0 != wlen)
			return wlen;
		else
			break;
	}
	return sum;
}

/* Test if the specifi @param path is a file and readable
 * @param path the path of the file to test
 * @return Linux specific error code
 */
static int isFileReadable(char *path)
{
	struct file *fp;
	int ret = 0;
	mm_segment_t oldfs;
	char buf;

	fp = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		ret = PTR_ERR(fp);
	} else {
		oldfs = get_fs();
		set_fs(get_ds());

		if (1 != readFile(fp, &buf, 1))
			ret = PTR_ERR(fp);

		set_fs(oldfs);
		filp_close(fp, NULL);
	}
	return ret;
}

/* Open the file with @param path and retrive the file content into
 * memory starting from @param buf for @param sz at most
 * @param path the path of the file to open and read
 * @param buf the starting address of the buffer to store file content
 * @param sz how many bytes to read at most
 * @return the byte we've read, or Linux specific error code
 */
static int retriveFromFile(char *path, u8 *buf, u32 sz)
{
	int ret = -1;
	mm_segment_t oldfs;
	struct file *fp;

	if (path && buf) {
		ret = openFile(&fp, path, O_RDONLY, 0);
		if (!ret) {
			DBG_8723A("%s openFile path:%s fp =%p\n",
				  __func__, path, fp);

			oldfs = get_fs(); set_fs(get_ds());
			ret = readFile(fp, buf, sz);
			set_fs(oldfs);
			closeFile(fp);

			DBG_8723A("%s readFile, ret:%d\n", __func__, ret);
		} else {
			DBG_8723A("%s openFile path:%s Fail, ret:%d\n",
				  __func__, path, ret);
		}
	} else {
		DBG_8723A("%s NULL pointer\n", __func__);
		ret =  -EINVAL;
	}
	return ret;
}

/* Open the file with @param path and wirte @param sz byte of data starting
 * from @param buf into the file
 * @param path the path of the file to open and write
 * @param buf the starting address of the data to write into file
 * @param sz how many bytes to write at most
 * @return the byte we've written, or Linux specific error code
 */
static int storeToFile(char *path, u8 *buf, u32 sz)
{
	struct file *fp;
	int ret = 0;
	mm_segment_t oldfs;

	if (path && buf) {
		ret = openFile(&fp, path, O_CREAT|O_WRONLY, 0666);
		if (!ret) {
			DBG_8723A("%s openFile path:%s fp =%p\n", __func__,
				  path, fp);

			oldfs = get_fs(); set_fs(get_ds());
			ret = writeFile(fp, buf, sz);
			set_fs(oldfs);
			closeFile(fp);

			DBG_8723A("%s writeFile, ret:%d\n", __func__, ret);
		} else {
			DBG_8723A("%s openFile path:%s Fail, ret:%d\n",
				  __func__, path, ret);
		}
	} else {
		DBG_8723A("%s NULL pointer\n", __func__);
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

/* Open the file with @param path and retrive the file content into memoryi
 * starting from @param buf for @param sz at most
 * @param path the path of the file to open and read
 * @param buf the starting address of the buffer to store file content
 * @param sz how many bytes to read at most
 * @return the byte we've read
 */
int rtw_retrive_from_file(char *path, u8 *buf, u32 sz)
{
	int ret = retriveFromFile(path, buf, sz);
	return ret >= 0 ? ret : 0;
}

/* Open the file with @param path and wirte @param sz byte of
 * data starting from @param buf into the file
 * @param path the path of the file to open and write
 * @param buf the starting address of the data to write into file
 * @param sz how many bytes to write at most
 * @return the byte we've written
 */
int rtw_store_to_file(char *path, u8 *buf, u32 sz)
{
	int ret = storeToFile(path, buf, sz);
	return ret >= 0 ? ret : 0;
}

u64 rtw_modular6423a(u64 x, u64 y)
{
	return do_div(x, y);
}

u64 rtw_division6423a(u64 x, u64 y)
{
	do_div(x, y);
	return x;
}

/* rtw_cbuf_full23a - test if cbuf is full
 * @cbuf: pointer of struct rtw_cbuf
 *
 * Returns: true if cbuf is full
 */
inline bool rtw_cbuf_full23a(struct rtw_cbuf *cbuf)
{
	return (cbuf->write == cbuf->read-1) ? true : false;
}

/* rtw_cbuf_empty23a - test if cbuf is empty
 * @cbuf: pointer of struct rtw_cbuf
 *
 * Returns: true if cbuf is empty
 */
inline bool rtw_cbuf_empty23a(struct rtw_cbuf *cbuf)
{
	return (cbuf->write == cbuf->read) ? true : false;
}

/**
 * rtw_cbuf_push23a - push a pointer into cbuf
 * @cbuf: pointer of struct rtw_cbuf
 * @buf: pointer to push in
 *
 * Lock free operation, be careful of the use scheme
 * Returns: true push success
 */
bool rtw_cbuf_push23a(struct rtw_cbuf *cbuf, void *buf)
{
	if (rtw_cbuf_full23a(cbuf))
		return _FAIL;

	if (0)
		DBG_8723A("%s on %u\n", __func__, cbuf->write);
	cbuf->bufs[cbuf->write] = buf;
	cbuf->write = (cbuf->write+1)%cbuf->size;

	return _SUCCESS;
}

/**
 * rtw_cbuf_pop23a - pop a pointer from cbuf
 * @cbuf: pointer of struct rtw_cbuf
 *
 * Lock free operation, be careful of the use scheme
 * Returns: pointer popped out
 */
void *rtw_cbuf_pop23a(struct rtw_cbuf *cbuf)
{
	void *buf;
	if (rtw_cbuf_empty23a(cbuf))
		return NULL;

	if (0)
		DBG_8723A("%s on %u\n", __func__, cbuf->read);
	buf = cbuf->bufs[cbuf->read];
	cbuf->read = (cbuf->read+1)%cbuf->size;

	return buf;
}

/**
 * rtw_cbuf_alloc23a - allocte a rtw_cbuf with given size and do initialization
 * @size: size of pointer
 *
 * Returns: pointer of srtuct rtw_cbuf, NULL for allocation failure
 */
struct rtw_cbuf *rtw_cbuf_alloc23a(u32 size)
{
	struct rtw_cbuf *cbuf;

	cbuf = kmalloc(sizeof(*cbuf) + sizeof(void *)*size, GFP_KERNEL);

	if (cbuf) {
		cbuf->write = 0;
		cbuf->read = 0;
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
