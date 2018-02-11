// SPDX-License-Identifier: GPL-2.0
/*
 *    SE/HMC Drive (Read) Cache Functions
 *
 *    Copyright IBM Corp. 2013
 *    Author(s): Ralf Hoppe (rhoppe@de.ibm.com)
 *
 */

#define KMSG_COMPONENT "hmcdrv"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/jiffies.h>

#include "hmcdrv_ftp.h"
#include "hmcdrv_cache.h"

#define HMCDRV_CACHE_TIMEOUT		30 /* aging timeout in seconds */

/**
 * struct hmcdrv_cache_entry - file cache (only used on read/dir)
 * @id: FTP command ID
 * @content: kernel-space buffer, 4k aligned
 * @len: size of @content cache (0 if caching disabled)
 * @ofs: start of content within file (-1 if no cached content)
 * @fname: file name
 * @fsize: file size
 * @timeout: cache timeout in jiffies
 *
 * Notice that the first three members (id, fname, fsize) are cached on all
 * read/dir requests. But content is cached only under some preconditions.
 * Uncached content is signalled by a negative value of @ofs.
 */
struct hmcdrv_cache_entry {
	enum hmcdrv_ftp_cmdid id;
	char fname[HMCDRV_FTP_FIDENT_MAX];
	size_t fsize;
	loff_t ofs;
	unsigned long timeout;
	void *content;
	size_t len;
};

static int hmcdrv_cache_order; /* cache allocated page order */

static struct hmcdrv_cache_entry hmcdrv_cache_file = {
	.fsize = SIZE_MAX,
	.ofs = -1,
	.len = 0,
	.fname = {'\0'}
};

/**
 * hmcdrv_cache_get() - looks for file data/content in read cache
 * @ftp: pointer to FTP command specification
 *
 * Return: number of bytes read from cache or a negative number if nothing
 * in content cache (for the file/cmd specified in @ftp)
 */
static ssize_t hmcdrv_cache_get(const struct hmcdrv_ftp_cmdspec *ftp)
{
	loff_t pos; /* position in cache (signed) */
	ssize_t len;

	if ((ftp->id != hmcdrv_cache_file.id) ||
	    strcmp(hmcdrv_cache_file.fname, ftp->fname))
		return -1;

	if (ftp->ofs >= hmcdrv_cache_file.fsize) /* EOF ? */
		return 0;

	if ((hmcdrv_cache_file.ofs < 0) || /* has content? */
	    time_after(jiffies, hmcdrv_cache_file.timeout))
		return -1;

	/* there seems to be cached content - calculate the maximum number
	 * of bytes that can be returned (regarding file size and offset)
	 */
	len = hmcdrv_cache_file.fsize - ftp->ofs;

	if (len > ftp->len)
		len = ftp->len;

	/* check if the requested chunk falls into our cache (which starts
	 * at offset 'hmcdrv_cache_file.ofs' in the file of interest)
	 */
	pos = ftp->ofs - hmcdrv_cache_file.ofs;

	if ((pos >= 0) &&
	    ((pos + len) <= hmcdrv_cache_file.len)) {

		memcpy(ftp->buf,
		       hmcdrv_cache_file.content + pos,
		       len);
		pr_debug("using cached content of '%s', returning %zd/%zd bytes\n",
			 hmcdrv_cache_file.fname, len,
			 hmcdrv_cache_file.fsize);

		return len;
	}

	return -1;
}

/**
 * hmcdrv_cache_do() - do a HMC drive CD/DVD transfer with cache update
 * @ftp: pointer to FTP command specification
 * @func: FTP transfer function to be used
 *
 * Return: number of bytes read/written or a (negative) error code
 */
static ssize_t hmcdrv_cache_do(const struct hmcdrv_ftp_cmdspec *ftp,
			       hmcdrv_cache_ftpfunc func)
{
	ssize_t len;

	/* only cache content if the read/dir cache really exists
	 * (hmcdrv_cache_file.len > 0), is large enough to handle the
	 * request (hmcdrv_cache_file.len >= ftp->len) and there is a need
	 * to do so (ftp->len > 0)
	 */
	if ((ftp->len > 0) && (hmcdrv_cache_file.len >= ftp->len)) {

		/* because the cache is not located at ftp->buf, we have to
		 * assemble a new HMC drive FTP cmd specification (pointing
		 * to our cache, and using the increased size)
		 */
		struct hmcdrv_ftp_cmdspec cftp = *ftp; /* make a copy */
		cftp.buf = hmcdrv_cache_file.content;  /* and update */
		cftp.len = hmcdrv_cache_file.len;      /* buffer data */

		len = func(&cftp, &hmcdrv_cache_file.fsize); /* now do */

		if (len > 0) {
			pr_debug("caching %zd bytes content for '%s'\n",
				 len, ftp->fname);

			if (len > ftp->len)
				len = ftp->len;

			hmcdrv_cache_file.ofs = ftp->ofs;
			hmcdrv_cache_file.timeout = jiffies +
				HMCDRV_CACHE_TIMEOUT * HZ;
			memcpy(ftp->buf, hmcdrv_cache_file.content, len);
		}
	} else {
		len = func(ftp, &hmcdrv_cache_file.fsize);
		hmcdrv_cache_file.ofs = -1; /* invalidate content */
	}

	if (len > 0) {
		/* cache some file info (FTP command, file name and file
		 * size) unconditionally
		 */
		strlcpy(hmcdrv_cache_file.fname, ftp->fname,
			HMCDRV_FTP_FIDENT_MAX);
		hmcdrv_cache_file.id = ftp->id;
		pr_debug("caching cmd %d, file size %zu for '%s'\n",
			 ftp->id, hmcdrv_cache_file.fsize, ftp->fname);
	}

	return len;
}

/**
 * hmcdrv_cache_cmd() - perform a cached HMC drive CD/DVD transfer
 * @ftp: pointer to FTP command specification
 * @func: FTP transfer function to be used
 *
 * Attention: Notice that this function is not reentrant - so the caller
 * must ensure exclusive execution.
 *
 * Return: number of bytes read/written or a (negative) error code
 */
ssize_t hmcdrv_cache_cmd(const struct hmcdrv_ftp_cmdspec *ftp,
			 hmcdrv_cache_ftpfunc func)
{
	ssize_t len;

	if ((ftp->id == HMCDRV_FTP_DIR) || /* read cache */
	    (ftp->id == HMCDRV_FTP_NLIST) ||
	    (ftp->id == HMCDRV_FTP_GET)) {

		len = hmcdrv_cache_get(ftp);

		if (len >= 0) /* got it from cache ? */
			return len; /* yes */

		len = hmcdrv_cache_do(ftp, func);

		if (len >= 0)
			return len;

	} else {
		len = func(ftp, NULL); /* simply do original command */
	}

	/* invalidate the (read) cache in case there was a write operation
	 * or an error on read/dir
	 */
	hmcdrv_cache_file.id = HMCDRV_FTP_NOOP;
	hmcdrv_cache_file.fsize = LLONG_MAX;
	hmcdrv_cache_file.ofs = -1;

	return len;
}

/**
 * hmcdrv_cache_startup() - startup of HMC drive cache
 * @cachesize: cache size
 *
 * Return: 0 on success, else a (negative) error code
 */
int hmcdrv_cache_startup(size_t cachesize)
{
	if (cachesize > 0) { /* perform caching ? */
		hmcdrv_cache_order = get_order(cachesize);
		hmcdrv_cache_file.content =
			(void *) __get_free_pages(GFP_KERNEL | GFP_DMA,
						  hmcdrv_cache_order);

		if (!hmcdrv_cache_file.content) {
			pr_err("Allocating the requested cache size of %zu bytes failed\n",
			       cachesize);
			return -ENOMEM;
		}

		pr_debug("content cache enabled, size is %zu bytes\n",
			 cachesize);
	}

	hmcdrv_cache_file.len = cachesize;
	return 0;
}

/**
 * hmcdrv_cache_shutdown() - shutdown of HMC drive cache
 */
void hmcdrv_cache_shutdown(void)
{
	if (hmcdrv_cache_file.content) {
		free_pages((unsigned long) hmcdrv_cache_file.content,
			   hmcdrv_cache_order);
		hmcdrv_cache_file.content = NULL;
	}

	hmcdrv_cache_file.id = HMCDRV_FTP_NOOP;
	hmcdrv_cache_file.fsize = LLONG_MAX;
	hmcdrv_cache_file.ofs = -1;
	hmcdrv_cache_file.len = 0; /* no cache */
}
