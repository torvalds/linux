/*
 *
 *  sep_crypto.c - Crypto interface structures
 *
 *  Copyright(c) 2009-2011 Intel Corporation. All rights reserved.
 *  Contributions(c) 2009-2010 Discretix. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *  CONTACTS:
 *
 *  Mark Allyn		mark.a.allyn@intel.com
 *  Jayant Mangalampalli jayant.mangalampalli@intel.com
 *
 *  CHANGES:
 *
 *  2009.06.26	Initial publish
 *  2010.09.14  Upgrade to Medfield
 *  2011.02.22  Enable Kernel Crypto
 *
 */

/* #define DEBUG */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/pci.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/crypto.h>
#include <crypto/internal/hash.h>
#include <crypto/scatterwalk.h>
#include <crypto/sha.h>
#include <crypto/md5.h>
#include <crypto/aes.h>
#include <crypto/des.h>
#include <crypto/hash.h>
#include "sep_driver_hw_defs.h"
#include "sep_driver_config.h"
#include "sep_driver_api.h"
#include "sep_dev.h"
#include "sep_crypto.h"

/* Globals for queuing */
static spinlock_t queue_lock;
static struct crypto_queue sep_queue;

/* Declare of dequeuer */
static void sep_dequeuer(void *data);

/* TESTING */
/**
 * crypto_sep_dump_message - dump the message that is pending
 * @sep: SEP device
 * This will only print dump if DEBUG is set; it does
 * follow kernel debug print enabling
 */
static void crypto_sep_dump_message(struct sep_system_ctx *sctx)
{
#if 0
	u32 *p;
	u32 *i;
	int count;

	p = sctx->sep_used->shared_addr;
	i = (u32 *)sctx->msg;
	for (count = 0; count < 40 * 4; count += 4)
		dev_dbg(&sctx->sep_used->pdev->dev,
			"[PID%d] Word %d of the message is %x (local)%x\n",
				current->pid, count/4, *p++, *i++);
#endif
}

/**
 *	sep_do_callback
 *	@work: pointer to work_struct
 *	This is what is called by the queue; it is generic so that it
 *	can be used by any type of operation as each different callback
 *	function can use the data parameter in its own way
 */
static void sep_do_callback(struct work_struct *work)
{
	struct sep_work_struct *sep_work = container_of(work,
		struct sep_work_struct, work);
	if (sep_work != NULL) {
		(sep_work->callback)(sep_work->data);
		kfree(sep_work);
	} else {
		pr_debug("sep crypto: do callback - NULL container\n");
	}
}

/**
 *	sep_submit_work
 *	@work_queue: pointer to struct_workqueue
 *	@funct: pointer to function to execute
 *	@data: pointer to data; function will know
 *		how to use it
 *	This is a generic API to submit something to
 *	the queue. The callback function will depend
 *	on what operation is to be done
 */
static int sep_submit_work(struct workqueue_struct *work_queue,
	void(*funct)(void *),
	void *data)
{
	struct sep_work_struct *sep_work;
	int result;

	sep_work = kmalloc(sizeof(struct sep_work_struct), GFP_ATOMIC);

	if (sep_work == NULL) {
		pr_debug("sep crypto: cant allocate work structure\n");
		return -ENOMEM;
	}

	sep_work->callback = funct;
	sep_work->data = data;
	INIT_WORK(&sep_work->work, sep_do_callback);
	result = queue_work(work_queue, &sep_work->work);
	if (!result) {
		pr_debug("sep_crypto: queue_work failed\n");
		return -EINVAL;
	}
	return 0;
}

/**
 *	sep_alloc_sg_buf -
 *	@sep: pointer to struct sep_device
 *	@size: total size of area
 *	@block_size: minimum size of chunks
 *	each page is minimum or modulo this size
 *	@returns: pointer to struct scatterlist for new
 *	buffer
 **/
static struct scatterlist *sep_alloc_sg_buf(
	struct sep_device *sep,
	size_t size,
	size_t block_size)
{
	u32 nbr_pages;
	u32 ct1;
	void *buf;
	size_t current_size;
	size_t real_page_size;

	struct scatterlist *sg, *sg_temp;

	if (size == 0)
		return NULL;

	dev_dbg(&sep->pdev->dev, "sep alloc sg buf\n");

	current_size = 0;
	nbr_pages = 0;
	real_page_size = PAGE_SIZE - (PAGE_SIZE % block_size);
	/**
	 * The size of each page must be modulo of the operation
	 * block size; increment by the modified page size until
	 * the total size is reached, then you have the number of
	 * pages
	 */
	while (current_size < size) {
		current_size += real_page_size;
		nbr_pages += 1;
	}

	sg = kmalloc((sizeof(struct scatterlist) * nbr_pages), GFP_ATOMIC);
	if (!sg) {
		dev_warn(&sep->pdev->dev, "Cannot allocate page for new sg\n");
		return NULL;
	}

	sg_init_table(sg, nbr_pages);

	current_size = 0;
	sg_temp = sg;
	for (ct1 = 0; ct1 < nbr_pages; ct1 += 1) {
		buf = (void *)get_zeroed_page(GFP_ATOMIC);
		if (!buf) {
			dev_warn(&sep->pdev->dev,
				"Cannot allocate page for new buffer\n");
			kfree(sg);
			return NULL;
		}

		sg_set_buf(sg_temp, buf, real_page_size);
		if ((size - current_size) > real_page_size) {
			sg_temp->length = real_page_size;
			current_size += real_page_size;
		} else {
			sg_temp->length = (size - current_size);
			current_size = size;
		}
		sg_temp = sg_next(sg);
	}
	return sg;
}

/**
 *	sep_free_sg_buf -
 *	@sg: pointer to struct scatterlist; points to area to free
 */
static void sep_free_sg_buf(struct scatterlist *sg)
{
	struct scatterlist *sg_temp = sg;
		while (sg_temp) {
			free_page((unsigned long)sg_virt(sg_temp));
			sg_temp = sg_next(sg_temp);
		}
		kfree(sg);
}

/**
 *	sep_copy_sg -
 *	@sep: pointer to struct sep_device
 *	@sg_src: pointer to struct scatterlist for source
 *	@sg_dst: pointer to struct scatterlist for destination
 *      @size: size (in bytes) of data to copy
 *
 *	Copy data from one scatterlist to another; both must
 *	be the same size
 */
static void sep_copy_sg(
	struct sep_device *sep,
	struct scatterlist *sg_src,
	struct scatterlist *sg_dst,
	size_t size)
{
	u32 seg_size;
	u32 in_offset, out_offset;

	u32 count = 0;
	struct scatterlist *sg_src_tmp = sg_src;
	struct scatterlist *sg_dst_tmp = sg_dst;
	in_offset = 0;
	out_offset = 0;

	dev_dbg(&sep->pdev->dev, "sep copy sg\n");

	if ((sg_src == NULL) || (sg_dst == NULL) || (size == 0))
		return;

	dev_dbg(&sep->pdev->dev, "sep copy sg not null\n");

	while (count < size) {
		if ((sg_src_tmp->length - in_offset) >
			(sg_dst_tmp->length - out_offset))
			seg_size = sg_dst_tmp->length - out_offset;
		else
			seg_size = sg_src_tmp->length - in_offset;

		if (seg_size > (size - count))
			seg_size = (size = count);

		memcpy(sg_virt(sg_dst_tmp) + out_offset,
			sg_virt(sg_src_tmp) + in_offset,
			seg_size);

		in_offset += seg_size;
		out_offset += seg_size;
		count += seg_size;

		if (in_offset >= sg_src_tmp->length) {
			sg_src_tmp = sg_next(sg_src_tmp);
			in_offset = 0;
		}

		if (out_offset >= sg_dst_tmp->length) {
			sg_dst_tmp = sg_next(sg_dst_tmp);
			out_offset = 0;
		}
	}
}

/**
 *	sep_oddball_pages -
 *	@sep: pointer to struct sep_device
 *	@sg: pointer to struct scatterlist - buffer to check
 *	@size: total data size
 *	@blocksize: minimum block size; must be multiples of this size
 *	@to_copy: 1 means do copy, 0 means do not copy
 *	@new_sg: pointer to location to put pointer to new sg area
 *	@returns: 1 if new scatterlist is needed; 0 if not needed;
 *		error value if operation failed
 *
 *	The SEP device requires all pages to be multiples of the
 *	minimum block size appropriate for the operation
 *	This function check all pages; if any are oddball sizes
 *	(not multiple of block sizes), it creates a new scatterlist.
 *	If the to_copy parameter is set to 1, then a scatter list
 *	copy is performed. The pointer to the new scatterlist is
 *	put into the address supplied by the new_sg parameter; if
 *	no new scatterlist is needed, then a NULL is put into
 *	the location at new_sg.
 *
 */
static int sep_oddball_pages(
	struct sep_device *sep,
	struct scatterlist *sg,
	size_t data_size,
	u32 block_size,
	struct scatterlist **new_sg,
	u32 do_copy)
{
	struct scatterlist *sg_temp;
	u32 flag;
	u32 nbr_pages, page_count;

	dev_dbg(&sep->pdev->dev, "sep oddball\n");
	if ((sg == NULL) || (data_size == 0) || (data_size < block_size))
		return 0;

	dev_dbg(&sep->pdev->dev, "sep oddball not null\n");
	flag = 0;
	nbr_pages = 0;
	page_count = 0;
	sg_temp = sg;

	while (sg_temp) {
		nbr_pages += 1;
		sg_temp = sg_next(sg_temp);
	}

	sg_temp = sg;
	while ((sg_temp) && (flag == 0)) {
		page_count += 1;
		if (sg_temp->length % block_size)
			flag = 1;
		else
			sg_temp = sg_next(sg_temp);
	}

	/* Do not process if last (or only) page is oddball */
	if (nbr_pages == page_count)
		flag = 0;

	if (flag) {
		dev_dbg(&sep->pdev->dev, "sep oddball processing\n");
		*new_sg = sep_alloc_sg_buf(sep, data_size, block_size);
		if (*new_sg == NULL) {
			dev_warn(&sep->pdev->dev, "cannot allocate new sg\n");
			return -ENOMEM;
		}

		if (do_copy)
			sep_copy_sg(sep, sg, *new_sg, data_size);

		return 1;
	} else {
		return 0;
	}
}

/**
 *	sep_copy_offset_sg -
 *	@sep: pointer to struct sep_device;
 *	@sg: pointer to struct scatterlist
 *	@offset: offset into scatterlist memory
 *	@dst: place to put data
 *	@len: length of data
 *	@returns: number of bytes copies
 *
 *	This copies data from scatterlist buffer
 *	offset from beginning - it is needed for
 *	handling tail data in hash
 */
static size_t sep_copy_offset_sg(
	struct sep_device *sep,
	struct scatterlist *sg,
	u32 offset,
	void *dst,
	u32 len)
{
	size_t page_start;
	size_t page_end;
	size_t offset_within_page;
	size_t length_within_page;
	size_t length_remaining;
	size_t current_offset;

	/* Find which page is beginning of segment */
	page_start = 0;
	page_end = sg->length;
	while ((sg) && (offset > page_end)) {
		page_start += sg->length;
		sg = sg_next(sg);
		if (sg)
			page_end += sg->length;
	}

	if (sg == NULL)
		return -ENOMEM;

	offset_within_page = offset - page_start;
	if ((sg->length - offset_within_page) >= len) {
		/* All within this page */
		memcpy(dst, sg_virt(sg) + offset_within_page, len);
		return len;
	} else {
		/* Scattered multiple pages */
		current_offset = 0;
		length_remaining = len;
		while ((sg) && (current_offset < len)) {
			length_within_page = sg->length - offset_within_page;
			if (length_within_page >= length_remaining) {
				memcpy(dst+current_offset,
					sg_virt(sg) + offset_within_page,
					length_remaining);
				length_remaining = 0;
				current_offset = len;
			} else {
				memcpy(dst+current_offset,
					sg_virt(sg) + offset_within_page,
					length_within_page);
				length_remaining -= length_within_page;
				current_offset += length_within_page;
				offset_within_page = 0;
				sg = sg_next(sg);
			}
		}

		if (sg == NULL)
			return -ENOMEM;
	}
	return len;
}

/**
 *	partial_overlap -
 *	@src_ptr: source pointer
 *	@dst_ptr: destination pointer
 *	@nbytes: number of bytes
 *	@returns: 0 for success; -1 for failure
 *	We cannot have any partial overlap. Total overlap
 *	where src is the same as dst is okay
 */
static int partial_overlap(void *src_ptr, void *dst_ptr, u32 nbytes)
{
	/* Check for partial overlap */
	if (src_ptr != dst_ptr) {
		if (src_ptr < dst_ptr) {
			if ((src_ptr + nbytes) > dst_ptr)
				return -EINVAL;
		} else {
			if ((dst_ptr + nbytes) > src_ptr)
				return -EINVAL;
		}
	}

	return 0;
}

/* Debug - prints only if DEBUG is defined; follows kernel debug model */
static void sep_dump(struct sep_device *sep, char *stg, void *start, int len)
{
#if 0
	int ct1;
	u8 *ptt;

	dev_dbg(&sep->pdev->dev,
		"Dump of %s starting at %08lx for %08x bytes\n",
		stg, (unsigned long)start, len);
	for (ct1 = 0; ct1 < len; ct1 += 1) {
		ptt = (u8 *)(start + ct1);
		dev_dbg(&sep->pdev->dev, "%02x ", *ptt);
		if (ct1 % 16 == 15)
			dev_dbg(&sep->pdev->dev, "\n");
	}
	dev_dbg(&sep->pdev->dev, "\n");
#endif
}

/* Debug - prints only if DEBUG is defined; follows kernel debug model */
static void sep_dump_sg(struct sep_device *sep, char *stg,
			struct scatterlist *sg)
{
#if 0
	int ct1, ct2;
	u8 *ptt;

	dev_dbg(&sep->pdev->dev, "Dump of scatterlist %s\n", stg);

	ct1 = 0;
	while (sg) {
		dev_dbg(&sep->pdev->dev, "page %x\n size %x", ct1,
			sg->length);
		dev_dbg(&sep->pdev->dev, "phys addr is %lx",
			(unsigned long)sg_phys(sg));
		ptt = sg_virt(sg);
		for (ct2 = 0; ct2 < sg->length; ct2 += 1) {
			dev_dbg(&sep->pdev->dev, "byte %x is %02x\n",
				ct2, (unsigned char)*(ptt + ct2));
		}

		ct1 += 1;
		sg = sg_next(sg);
	}
	dev_dbg(&sep->pdev->dev, "\n");
#endif
}

/**
 * RFC2451: Weak key check
 * Returns: 1 (weak), 0 (not weak)
 */
static int sep_weak_key(const u8 *key, unsigned int keylen)
{
	static const u8 parity[] = {
	8, 1, 0, 8, 0, 8, 8, 0, 0, 8, 8, 0, 8, 0, 2, 8,
	0, 8, 8, 0, 8, 0, 0, 8, 8,
	0, 0, 8, 0, 8, 8, 3,
	0, 8, 8, 0, 8, 0, 0, 8, 8, 0, 0, 8, 0, 8, 8, 0,
	8, 0, 0, 8, 0, 8, 8, 0, 0,
	8, 8, 0, 8, 0, 0, 8,
	0, 8, 8, 0, 8, 0, 0, 8, 8, 0, 0, 8, 0, 8, 8, 0,
	8, 0, 0, 8, 0, 8, 8, 0, 0,
	8, 8, 0, 8, 0, 0, 8,
	8, 0, 0, 8, 0, 8, 8, 0, 0, 8, 8, 0, 8, 0, 0, 8,
	0, 8, 8, 0, 8, 0, 0, 8, 8,
	0, 0, 8, 0, 8, 8, 0,
	0, 8, 8, 0, 8, 0, 0, 8, 8, 0, 0, 8, 0, 8, 8, 0,
	8, 0, 0, 8, 0, 8, 8, 0, 0,
	8, 8, 0, 8, 0, 0, 8,
	8, 0, 0, 8, 0, 8, 8, 0, 0, 8, 8, 0, 8, 0, 0, 8,
	0, 8, 8, 0, 8, 0, 0, 8, 8,
	0, 0, 8, 0, 8, 8, 0,
	8, 0, 0, 8, 0, 8, 8, 0, 0, 8, 8, 0, 8, 0, 0, 8,
	0, 8, 8, 0, 8, 0, 0, 8, 8,
	0, 0, 8, 0, 8, 8, 0,
	4, 8, 8, 0, 8, 0, 0, 8, 8, 0, 0, 8, 0, 8, 8, 0,
	8, 5, 0, 8, 0, 8, 8, 0, 0,
	8, 8, 0, 8, 0, 6, 8,
	};

	u32 n, w;

	n  = parity[key[0]]; n <<= 4;
	n |= parity[key[1]]; n <<= 4;
	n |= parity[key[2]]; n <<= 4;
	n |= parity[key[3]]; n <<= 4;
	n |= parity[key[4]]; n <<= 4;
	n |= parity[key[5]]; n <<= 4;
	n |= parity[key[6]]; n <<= 4;
	n |= parity[key[7]];
	w = 0x88888888L;

	/* 1 in 10^10 keys passes this test */
	if (!((n - (w >> 3)) & w)) {
		if (n < 0x41415151) {
			if (n < 0x31312121) {
				if (n < 0x14141515) {
					/* 01 01 01 01 01 01 01 01 */
					if (n == 0x11111111)
						goto weak;
					/* 01 1F 01 1F 01 0E 01 0E */
					if (n == 0x13131212)
						goto weak;
				} else {
					/* 01 E0 01 E0 01 F1 01 F1 */
					if (n == 0x14141515)
						goto weak;
					/* 01 FE 01 FE 01 FE 01 FE */
					if (n == 0x16161616)
						goto weak;
				}
			} else {
				if (n < 0x34342525) {
					/* 1F 01 1F 01 0E 01 0E 01 */
					if (n == 0x31312121)
						goto weak;
					/* 1F 1F 1F 1F 0E 0E 0E 0E (?) */
					if (n == 0x33332222)
						goto weak;
				} else {
					/* 1F E0 1F E0 0E F1 0E F1 */
					if (n == 0x34342525)
						goto weak;
					/* 1F FE 1F FE 0E FE 0E FE */
					if (n == 0x36362626)
						goto weak;
				}
			}
		} else {
			if (n < 0x61616161) {
				if (n < 0x44445555) {
					/* E0 01 E0 01 F1 01 F1 01 */
					if (n == 0x41415151)
						goto weak;
					/* E0 1F E0 1F F1 0E F1 0E */
					if (n == 0x43435252)
						goto weak;
				} else {
					/* E0 E0 E0 E0 F1 F1 F1 F1 (?) */
					if (n == 0x44445555)
						goto weak;
					/* E0 FE E0 FE F1 FE F1 FE */
					if (n == 0x46465656)
						goto weak;
				}
			} else {
				if (n < 0x64646565) {
					/* FE 01 FE 01 FE 01 FE 01 */
					if (n == 0x61616161)
						goto weak;
					/* FE 1F FE 1F FE 0E FE 0E */
					if (n == 0x63636262)
						goto weak;
				} else {
					/* FE E0 FE E0 FE F1 FE F1 */
					if (n == 0x64646565)
						goto weak;
					/* FE FE FE FE FE FE FE FE */
					if (n == 0x66666666)
						goto weak;
				}
			}
		}
	}
	return 0;
weak:
	return 1;
}
/**
 *	sep_sg_nents
 */
static u32 sep_sg_nents(struct scatterlist *sg)
{
	u32 ct1 = 0;
	while (sg) {
		ct1 += 1;
		sg = sg_next(sg);
	}

	return ct1;
}

/**
 *	sep_start_msg -
 *	@sctx: pointer to struct sep_system_ctx
 *	@returns: offset to place for the next word in the message
 *	Set up pointer in message pool for new message
 */
static u32 sep_start_msg(struct sep_system_ctx *sctx)
{
	u32 *word_ptr;
	sctx->msg_len_words = 2;
	sctx->msgptr = sctx->msg;
	memset(sctx->msg, 0, SEP_DRIVER_MESSAGE_SHARED_AREA_SIZE_IN_BYTES);
	sctx->msgptr += sizeof(u32) * 2;
	word_ptr = (u32 *)sctx->msgptr;
	*word_ptr = SEP_START_MSG_TOKEN;
	return sizeof(u32) * 2;
}

/**
 *	sep_end_msg -
 *	@sctx: pointer to struct sep_system_ctx
 *	@messages_offset: current message offset
 *	Returns: 0 for success; <0 otherwise
 *	End message; set length and CRC; and
 *	send interrupt to the SEP
 */
static void sep_end_msg(struct sep_system_ctx *sctx, u32 msg_offset)
{
	u32 *word_ptr;
	/* Msg size goes into msg after token */
	sctx->msg_len_words = msg_offset / sizeof(u32) + 1;
	word_ptr = (u32 *)sctx->msgptr;
	word_ptr += 1;
	*word_ptr = sctx->msg_len_words;

	/* CRC (currently 0) goes at end of msg */
	word_ptr = (u32 *)(sctx->msgptr + msg_offset);
	*word_ptr = 0;
}

/**
 *	sep_start_inbound_msg -
 *	@sctx: pointer to struct sep_system_ctx
 *	@msg_offset: offset to place for the next word in the message
 *	@returns: 0 for success; error value for failure
 *	Set up pointer in message pool for inbound message
 */
static u32 sep_start_inbound_msg(struct sep_system_ctx *sctx, u32 *msg_offset)
{
	u32 *word_ptr;
	u32 token;
	u32 error = SEP_OK;

	*msg_offset = sizeof(u32) * 2;
	word_ptr = (u32 *)sctx->msgptr;
	token = *word_ptr;
	sctx->msg_len_words = *(word_ptr + 1);

	if (token != SEP_START_MSG_TOKEN) {
		error = SEP_INVALID_START;
		goto end_function;
	}

end_function:

	return error;
}

/**
 *	sep_write_msg -
 *	@sctx: pointer to struct sep_system_ctx
 *	@in_addr: pointer to start of parameter
 *	@size: size of parameter to copy (in bytes)
 *	@max_size: size to move up offset; SEP mesg is in word sizes
 *	@msg_offset: pointer to current offset (is updated)
 *	@byte_array: flag ti indicate wheter endian must be changed
 *	Copies data into the message area from caller
 */
static void sep_write_msg(struct sep_system_ctx *sctx, void *in_addr,
	u32 size, u32 max_size, u32 *msg_offset, u32 byte_array)
{
	u32 *word_ptr;
	void *void_ptr;
	void_ptr = sctx->msgptr + *msg_offset;
	word_ptr = (u32 *)void_ptr;
	memcpy(void_ptr, in_addr, size);
	*msg_offset += max_size;

	/* Do we need to manipulate endian? */
	if (byte_array) {
		u32 i;
		for (i = 0; i < ((size + 3) / 4); i += 1)
			*(word_ptr + i) = CHG_ENDIAN(*(word_ptr + i));
	}
}

/**
 *	sep_make_header
 *	@sctx: pointer to struct sep_system_ctx
 *	@msg_offset: pointer to current offset (is updated)
 *	@op_code: op code to put into message
 *	Puts op code into message and updates offset
 */
static void sep_make_header(struct sep_system_ctx *sctx, u32 *msg_offset,
			    u32 op_code)
{
	u32 *word_ptr;

	*msg_offset = sep_start_msg(sctx);
	word_ptr = (u32 *)(sctx->msgptr + *msg_offset);
	*word_ptr = op_code;
	*msg_offset += sizeof(u32);
}



/**
 *	sep_read_msg -
 *	@sctx: pointer to struct sep_system_ctx
 *	@in_addr: pointer to start of parameter
 *	@size: size of parameter to copy (in bytes)
 *	@max_size: size to move up offset; SEP mesg is in word sizes
 *	@msg_offset: pointer to current offset (is updated)
 *	@byte_array: flag ti indicate wheter endian must be changed
 *	Copies data out of the message area to caller
 */
static void sep_read_msg(struct sep_system_ctx *sctx, void *in_addr,
	u32 size, u32 max_size, u32 *msg_offset, u32 byte_array)
{
	u32 *word_ptr;
	void *void_ptr;
	void_ptr = sctx->msgptr + *msg_offset;
	word_ptr = (u32 *)void_ptr;

	/* Do we need to manipulate endian? */
	if (byte_array) {
		u32 i;
		for (i = 0; i < ((size + 3) / 4); i += 1)
			*(word_ptr + i) = CHG_ENDIAN(*(word_ptr + i));
	}

	memcpy(in_addr, void_ptr, size);
	*msg_offset += max_size;
}

/**
 *	sep_verify_op -
 *      @sctx: pointer to struct sep_system_ctx
 *	@op_code: expected op_code
 *      @msg_offset: pointer to current offset (is updated)
 *	@returns: 0 for success; error for failure
 */
static u32 sep_verify_op(struct sep_system_ctx *sctx, u32 op_code,
			 u32 *msg_offset)
{
	u32 error;
	u32 in_ary[2];

	struct sep_device *sep = sctx->sep_used;

	dev_dbg(&sep->pdev->dev, "dumping return message\n");
	error = sep_start_inbound_msg(sctx, msg_offset);
	if (error) {
		dev_warn(&sep->pdev->dev,
			"sep_start_inbound_msg error\n");
		return error;
	}

	sep_read_msg(sctx, in_ary, sizeof(u32) * 2, sizeof(u32) * 2,
		msg_offset, 0);

	if (in_ary[0] != op_code) {
		dev_warn(&sep->pdev->dev,
			"sep got back wrong opcode\n");
		dev_warn(&sep->pdev->dev,
			"got back %x; expected %x\n",
			in_ary[0], op_code);
		return SEP_WRONG_OPCODE;
	}

	if (in_ary[1] != SEP_OK) {
		dev_warn(&sep->pdev->dev,
			"sep execution error\n");
		dev_warn(&sep->pdev->dev,
			"got back %x; expected %x\n",
			in_ary[1], SEP_OK);
		return in_ary[0];
	}

return 0;
}

/**
 * sep_read_context -
 * @sctx: pointer to struct sep_system_ctx
 * @msg_offset: point to current place in SEP msg; is updated
 * @dst: pointer to place to put the context
 * @len: size of the context structure (differs for crypro/hash)
 * This function reads the context from the msg area
 * There is a special way the vendor needs to have the maximum
 * length calculated so that the msg_offset is updated properly;
 * it skips over some words in the msg area depending on the size
 * of the context
 */
static void sep_read_context(struct sep_system_ctx *sctx, u32 *msg_offset,
	void *dst, u32 len)
{
	u32 max_length = ((len + 3) / sizeof(u32)) * sizeof(u32);
	sep_read_msg(sctx, dst, len, max_length, msg_offset, 0);
}

/**
 * sep_write_context -
 * @sctx: pointer to struct sep_system_ctx
 * @msg_offset: point to current place in SEP msg; is updated
 * @src: pointer to the current context
 * @len: size of the context structure (differs for crypro/hash)
 * This function writes the context to the msg area
 * There is a special way the vendor needs to have the maximum
 * length calculated so that the msg_offset is updated properly;
 * it skips over some words in the msg area depending on the size
 * of the context
 */
static void sep_write_context(struct sep_system_ctx *sctx, u32 *msg_offset,
	void *src, u32 len)
{
	u32 max_length = ((len + 3) / sizeof(u32)) * sizeof(u32);
	sep_write_msg(sctx, src, len, max_length, msg_offset, 0);
}

/**
 * sep_clear_out -
 * @sctx: pointer to struct sep_system_ctx
 * Clear out crypto related values in sep device structure
 * to enable device to be used by anyone; either kernel
 * crypto or userspace app via middleware
 */
static void sep_clear_out(struct sep_system_ctx *sctx)
{
	if (sctx->src_sg_hold) {
		sep_free_sg_buf(sctx->src_sg_hold);
		sctx->src_sg_hold = NULL;
	}

	if (sctx->dst_sg_hold) {
		sep_free_sg_buf(sctx->dst_sg_hold);
		sctx->dst_sg_hold = NULL;
	}

	sctx->src_sg = NULL;
	sctx->dst_sg = NULL;

	sep_free_dma_table_data_handler(sctx->sep_used, &sctx->dma_ctx);

	if (sctx->i_own_sep) {
		/**
		 * The following unlocks the sep and makes it available
		 * to any other application
		 * First, null out crypto entries in sep before relesing it
		 */
		sctx->sep_used->current_hash_req = NULL;
		sctx->sep_used->current_cypher_req = NULL;
		sctx->sep_used->current_request = 0;
		sctx->sep_used->current_hash_stage = 0;
		sctx->sep_used->sctx = NULL;
		sctx->sep_used->in_kernel = 0;

		sctx->call_status.status = 0;

		/* Remove anything confidentail */
		memset(sctx->sep_used->shared_addr, 0,
			SEP_DRIVER_MESSAGE_SHARED_AREA_SIZE_IN_BYTES);

		sep_queue_status_remove(sctx->sep_used, &sctx->queue_elem);

#ifdef SEP_ENABLE_RUNTIME_PM
		sctx->sep_used->in_use = 0;
		pm_runtime_mark_last_busy(&sctx->sep_used->pdev->dev);
		pm_runtime_put_autosuspend(&sctx->sep_used->pdev->dev);
#endif

		clear_bit(SEP_WORKING_LOCK_BIT, &sctx->sep_used->in_use_flags);
		sctx->sep_used->pid_doing_transaction = 0;

		dev_dbg(&sctx->sep_used->pdev->dev,
			"[PID%d] waking up next transaction\n",
			current->pid);

		clear_bit(SEP_TRANSACTION_STARTED_LOCK_BIT,
			&sctx->sep_used->in_use_flags);
		wake_up(&sctx->sep_used->event_transactions);

		sctx->i_own_sep = 0;
	}
}

/**
  * Release crypto infrastructure from EINPROGRESS and
  * clear sep_dev so that SEP is available to anyone
  */
static void sep_crypto_release(struct sep_system_ctx *sctx, u32 error)
{
	struct ahash_request *hash_req = sctx->current_hash_req;
	struct ablkcipher_request *cypher_req =
		sctx->current_cypher_req;
	struct sep_device *sep = sctx->sep_used;

	sep_clear_out(sctx);

	if (cypher_req != NULL) {
		if (cypher_req->base.complete == NULL) {
			dev_dbg(&sep->pdev->dev,
				"release is null for cypher!");
		} else {
			cypher_req->base.complete(
				&cypher_req->base, error);
		}
	}

	if (hash_req != NULL) {
		if (hash_req->base.complete == NULL) {
			dev_dbg(&sep->pdev->dev,
				"release is null for hash!");
		} else {
			hash_req->base.complete(
				&hash_req->base, error);
		}
	}
}

/**
 *	This is where we grab the sep itself and tell it to do something.
 *	It will sleep if the sep is currently busy
 *	and it will return 0 if sep is now ours; error value if there
 *	were problems
 */
static int sep_crypto_take_sep(struct sep_system_ctx *sctx)
{
	struct sep_device *sep = sctx->sep_used;
	int result;
	struct sep_msgarea_hdr *my_msg_header;

	my_msg_header = (struct sep_msgarea_hdr *)sctx->msg;

	/* add to status queue */
	sctx->queue_elem = sep_queue_status_add(sep, my_msg_header->opcode,
		sctx->nbytes, current->pid,
		current->comm, sizeof(current->comm));

	if (!sctx->queue_elem) {
		dev_dbg(&sep->pdev->dev, "[PID%d] updating queue"
			" status error\n", current->pid);
		return -EINVAL;
	}

	/* get the device; this can sleep */
	result = sep_wait_transaction(sep);
	if (result)
		return result;

	if (sep_dev->power_save_setup == 1)
		pm_runtime_get_sync(&sep_dev->pdev->dev);

	/* Copy in the message */
	memcpy(sep->shared_addr, sctx->msg,
		SEP_DRIVER_MESSAGE_SHARED_AREA_SIZE_IN_BYTES);

	/* Copy in the dcb information if there is any */
	if (sctx->dcb_region) {
		result = sep_activate_dcb_dmatables_context(sep,
			&sctx->dcb_region, &sctx->dmatables_region,
			sctx->dma_ctx);
		if (result)
			return result;
	}

	/* Mark the device so we know how to finish the job in the tasklet */
	if (sctx->current_hash_req)
		sep->current_hash_req = sctx->current_hash_req;
	else
		sep->current_cypher_req = sctx->current_cypher_req;

	sep->current_request = sctx->current_request;
	sep->current_hash_stage = sctx->current_hash_stage;
	sep->sctx = sctx;
	sep->in_kernel = 1;
	sctx->i_own_sep = 1;

	result = sep_send_command_handler(sep);

	dev_dbg(&sep->pdev->dev, "[PID%d]: sending command to the sep\n",
		current->pid);

	if (!result) {
		set_bit(SEP_LEGACY_SENDMSG_DONE_OFFSET,
			&sctx->call_status.status);
		dev_dbg(&sep->pdev->dev, "[PID%d]: command sent okay\n",
			current->pid);
	}

	return result;
}

/* This needs to be run as a work queue as it can be put asleep */
static void sep_crypto_block(void *data)
{
	int int_error;
	u32 msg_offset;
	static u32 msg[10];
	void *src_ptr;
	void *dst_ptr;

	static char small_buf[100];
	ssize_t copy_result;
	int result;

	u32 max_length;
	struct scatterlist *new_sg;
	struct ablkcipher_request *req;
	struct sep_block_ctx *bctx;
	struct crypto_ablkcipher *tfm;
	struct sep_system_ctx *sctx;

	req = (struct ablkcipher_request *)data;
	bctx = ablkcipher_request_ctx(req);
	tfm = crypto_ablkcipher_reqtfm(req);
	sctx = crypto_ablkcipher_ctx(tfm);

	/* start the walk on scatterlists */
	ablkcipher_walk_init(&bctx->walk, req->src, req->dst, req->nbytes);
	dev_dbg(&sctx->sep_used->pdev->dev, "sep crypto block data size of %x\n",
		req->nbytes);

	int_error = ablkcipher_walk_phys(req, &bctx->walk);
	if (int_error) {
		dev_warn(&sctx->sep_used->pdev->dev, "walk phys error %x\n",
			int_error);
		sep_crypto_release(sctx, -ENOMEM);
		return;
	}

	/* check iv */
	if (bctx->des_opmode == SEP_DES_CBC) {
		if (!bctx->walk.iv) {
			dev_warn(&sctx->sep_used->pdev->dev, "no iv found\n");
			sep_crypto_release(sctx, -EINVAL);
			return;
		}

		memcpy(bctx->iv, bctx->walk.iv, SEP_DES_IV_SIZE_BYTES);
		sep_dump(sctx->sep_used, "iv", bctx->iv, SEP_DES_IV_SIZE_BYTES);
	}

	if (bctx->aes_opmode == SEP_AES_CBC) {
		if (!bctx->walk.iv) {
			dev_warn(&sctx->sep_used->pdev->dev, "no iv found\n");
			sep_crypto_release(sctx, -EINVAL);
			return;
		}

		memcpy(bctx->iv, bctx->walk.iv, SEP_AES_IV_SIZE_BYTES);
		sep_dump(sctx->sep_used, "iv", bctx->iv, SEP_AES_IV_SIZE_BYTES);
	}

	dev_dbg(&sctx->sep_used->pdev->dev,
		"crypto block: src is %lx dst is %lx\n",
		(unsigned long)req->src, (unsigned long)req->dst);

	/* Make sure all pages are even block */
	int_error = sep_oddball_pages(sctx->sep_used, req->src,
		req->nbytes, bctx->walk.blocksize, &new_sg, 1);

	if (int_error < 0) {
		dev_warn(&sctx->sep_used->pdev->dev, "oddball page eerror\n");
		sep_crypto_release(sctx, -ENOMEM);
		return;
	} else if (int_error == 1) {
		sctx->src_sg = new_sg;
		sctx->src_sg_hold = new_sg;
	} else {
		sctx->src_sg = req->src;
		sctx->src_sg_hold = NULL;
	}

	int_error = sep_oddball_pages(sctx->sep_used, req->dst,
		req->nbytes, bctx->walk.blocksize, &new_sg, 0);

	if (int_error < 0) {
		dev_warn(&sctx->sep_used->pdev->dev, "walk phys error %x\n",
			int_error);
		sep_crypto_release(sctx, -ENOMEM);
		return;
	} else if (int_error == 1) {
		sctx->dst_sg = new_sg;
		sctx->dst_sg_hold = new_sg;
	} else {
		sctx->dst_sg = req->dst;
		sctx->dst_sg_hold = NULL;
	}

	/* Do we need to perform init; ie; send key to sep? */
	if (sctx->key_sent == 0) {

		dev_dbg(&sctx->sep_used->pdev->dev, "sending key\n");

		/* put together message to SEP */
		/* Start with op code */
		sep_make_header(sctx, &msg_offset, bctx->init_opcode);

		/* now deal with IV */
		if (bctx->init_opcode == SEP_DES_INIT_OPCODE) {
			if (bctx->des_opmode == SEP_DES_CBC) {
				sep_write_msg(sctx, bctx->iv,
					SEP_DES_IV_SIZE_BYTES, sizeof(u32) * 4,
					&msg_offset, 1);
				sep_dump(sctx->sep_used, "initial IV",
					bctx->walk.iv, SEP_DES_IV_SIZE_BYTES);
			} else {
				/* Skip if ECB */
				msg_offset += 4 * sizeof(u32);
			}
		} else {
			max_length = ((SEP_AES_IV_SIZE_BYTES + 3) /
				sizeof(u32)) * sizeof(u32);
			if (bctx->aes_opmode == SEP_AES_CBC) {
				sep_write_msg(sctx, bctx->iv,
					SEP_AES_IV_SIZE_BYTES, max_length,
					&msg_offset, 1);
				sep_dump(sctx->sep_used, "initial IV",
					bctx->walk.iv, SEP_AES_IV_SIZE_BYTES);
			} else {
				/* Skip if ECB */
				msg_offset += max_length;
			}
		}

		/* load the key */
		if (bctx->init_opcode == SEP_DES_INIT_OPCODE) {
			sep_write_msg(sctx, (void *)&sctx->key.des.key1,
				sizeof(u32) * 8, sizeof(u32) * 8,
				&msg_offset, 1);

			msg[0] = (u32)sctx->des_nbr_keys;
			msg[1] = (u32)bctx->des_encmode;
			msg[2] = (u32)bctx->des_opmode;

			sep_write_msg(sctx, (void *)msg,
				sizeof(u32) * 3, sizeof(u32) * 3,
				&msg_offset, 0);
		} else {
			sep_write_msg(sctx, (void *)&sctx->key.aes,
				sctx->keylen,
				SEP_AES_MAX_KEY_SIZE_BYTES,
				&msg_offset, 1);

			msg[0] = (u32)sctx->aes_key_size;
			msg[1] = (u32)bctx->aes_encmode;
			msg[2] = (u32)bctx->aes_opmode;
			msg[3] = (u32)0; /* Secret key is not used */
			sep_write_msg(sctx, (void *)msg,
				sizeof(u32) * 4, sizeof(u32) * 4,
				&msg_offset, 0);
		}

	} else {

		/* set nbytes for queue status */
		sctx->nbytes = req->nbytes;

		/* Key already done; this is for data */
		dev_dbg(&sctx->sep_used->pdev->dev, "sending data\n");

		sep_dump_sg(sctx->sep_used,
			"block sg in", sctx->src_sg);

		/* check for valid data and proper spacing */
		src_ptr = sg_virt(sctx->src_sg);
		dst_ptr = sg_virt(sctx->dst_sg);

		if (!src_ptr || !dst_ptr ||
			(sctx->current_cypher_req->nbytes %
			crypto_ablkcipher_blocksize(tfm))) {

			dev_warn(&sctx->sep_used->pdev->dev,
				"cipher block size odd\n");
			dev_warn(&sctx->sep_used->pdev->dev,
				"cipher block size is %x\n",
				crypto_ablkcipher_blocksize(tfm));
			dev_warn(&sctx->sep_used->pdev->dev,
				"cipher data size is %x\n",
				sctx->current_cypher_req->nbytes);
			sep_crypto_release(sctx, -EINVAL);
			return;
		}

		if (partial_overlap(src_ptr, dst_ptr,
			sctx->current_cypher_req->nbytes)) {
			dev_warn(&sctx->sep_used->pdev->dev,
				"block partial overlap\n");
			sep_crypto_release(sctx, -EINVAL);
			return;
		}

		/* Put together the message */
		sep_make_header(sctx, &msg_offset, bctx->block_opcode);

		/* If des, and size is 1 block, put directly in msg */
		if ((bctx->block_opcode == SEP_DES_BLOCK_OPCODE) &&
			(req->nbytes == crypto_ablkcipher_blocksize(tfm))) {

			dev_dbg(&sctx->sep_used->pdev->dev,
				"writing out one block des\n");

			copy_result = sg_copy_to_buffer(
				sctx->src_sg, sep_sg_nents(sctx->src_sg),
				small_buf, crypto_ablkcipher_blocksize(tfm));

			if (copy_result != crypto_ablkcipher_blocksize(tfm)) {
				dev_warn(&sctx->sep_used->pdev->dev,
					"des block copy faild\n");
				sep_crypto_release(sctx, -ENOMEM);
				return;
			}

			/* Put data into message */
			sep_write_msg(sctx, small_buf,
				crypto_ablkcipher_blocksize(tfm),
				crypto_ablkcipher_blocksize(tfm) * 2,
				&msg_offset, 1);

			/* Put size into message */
			sep_write_msg(sctx, &req->nbytes,
				sizeof(u32), sizeof(u32), &msg_offset, 0);
		} else {
			/* Otherwise, fill out dma tables */
			sctx->dcb_input_data.app_in_address = src_ptr;
			sctx->dcb_input_data.data_in_size = req->nbytes;
			sctx->dcb_input_data.app_out_address = dst_ptr;
			sctx->dcb_input_data.block_size =
				crypto_ablkcipher_blocksize(tfm);
			sctx->dcb_input_data.tail_block_size = 0;
			sctx->dcb_input_data.is_applet = 0;
			sctx->dcb_input_data.src_sg = sctx->src_sg;
			sctx->dcb_input_data.dst_sg = sctx->dst_sg;

			result = sep_create_dcb_dmatables_context_kernel(
				sctx->sep_used,
				&sctx->dcb_region,
				&sctx->dmatables_region,
				&sctx->dma_ctx,
				&sctx->dcb_input_data,
				1);
			if (result) {
				dev_warn(&sctx->sep_used->pdev->dev,
					"crypto dma table create failed\n");
				sep_crypto_release(sctx, -EINVAL);
				return;
			}

			/* Portion of msg is nulled (no data) */
			msg[0] = (u32)0;
			msg[1] = (u32)0;
			msg[2] = (u32)0;
			msg[3] = (u32)0;
			msg[4] = (u32)0;
			sep_write_msg(sctx, (void *)msg,
				sizeof(u32) * 5,
				sizeof(u32) * 5,
				&msg_offset, 0);
		}

		/* Write context into message */
		if (bctx->block_opcode == SEP_DES_BLOCK_OPCODE) {
			sep_write_context(sctx, &msg_offset,
				&bctx->des_private_ctx,
				sizeof(struct sep_des_private_context));
			sep_dump(sctx->sep_used, "ctx to block des",
				&bctx->des_private_ctx, 40);
		} else {
			sep_write_context(sctx, &msg_offset,
				&bctx->aes_private_ctx,
				sizeof(struct sep_aes_private_context));
			sep_dump(sctx->sep_used, "ctx to block aes",
				&bctx->aes_private_ctx, 20);
		}
	}

	/* conclude message and then tell sep to do its thing */
	sctx->done_with_transaction = 0;

	sep_end_msg(sctx, msg_offset);
	result = sep_crypto_take_sep(sctx);
	if (result) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_crypto_take_sep failed\n");
		sep_crypto_release(sctx, -EINVAL);
		return;
	}

	/**
	 * Sep is now working. Lets wait up to 5 seconds
	 * for completion. If it does not complete, we will do
	 * a crypto release with -EINVAL to release the
	 * kernel crypto infrastructure and let the system
	 * continue to boot up
	 * We have to wait this long because some crypto
	 * operations can take a while
	 */

	dev_dbg(&sctx->sep_used->pdev->dev,
		"waiting for done with transaction\n");

	sctx->end_time = jiffies + (SEP_TRANSACTION_WAIT_TIME * HZ);
	while ((time_before(jiffies, sctx->end_time)) &&
		(!sctx->done_with_transaction))
		schedule();

	dev_dbg(&sctx->sep_used->pdev->dev,
		"done waiting for done with transaction\n");

	/* are we done? */
	if (!sctx->done_with_transaction) {
		/* Nope, lets release and tell crypto no */
		dev_warn(&sctx->sep_used->pdev->dev,
			"[PID%d] sep_crypto_block never finished\n",
			current->pid);
		sep_crypto_release(sctx, -EINVAL);
	}
}

/**
 * Post operation (after interrupt) for crypto block
 */
static u32 crypto_post_op(struct sep_device *sep)
{
	/* HERE */
	int int_error;
	u32 u32_error;
	u32 msg_offset;

	ssize_t copy_result;
	static char small_buf[100];

	struct ablkcipher_request *req;
	struct sep_block_ctx *bctx;
	struct sep_system_ctx *sctx;
	struct crypto_ablkcipher *tfm;

	if (!sep->current_cypher_req)
		return -EINVAL;

	/* hold req since we need to submit work after clearing sep */
	req = sep->current_cypher_req;

	bctx = ablkcipher_request_ctx(sep->current_cypher_req);
	tfm = crypto_ablkcipher_reqtfm(sep->current_cypher_req);
	sctx = crypto_ablkcipher_ctx(tfm);

	dev_dbg(&sctx->sep_used->pdev->dev, "crypto post_op\n");
	dev_dbg(&sctx->sep_used->pdev->dev, "crypto post_op message dump\n");
	crypto_sep_dump_message(sctx);

	sctx->done_with_transaction = 1;

	/* first bring msg from shared area to local area */
	memcpy(sctx->msg, sep->shared_addr,
		SEP_DRIVER_MESSAGE_SHARED_AREA_SIZE_IN_BYTES);

	/* Is this the result of performing init (key to SEP */
	if (sctx->key_sent == 0) {

		/* Did SEP do it okay */
		u32_error = sep_verify_op(sctx, bctx->init_opcode,
			&msg_offset);
		if (u32_error) {
			dev_warn(&sctx->sep_used->pdev->dev,
				"aes init error %x\n", u32_error);
			sep_crypto_release(sctx, u32_error);
			return u32_error;
			}

		/* Read Context */
		if (bctx->init_opcode == SEP_DES_INIT_OPCODE) {
			sep_read_context(sctx, &msg_offset,
			&bctx->des_private_ctx,
			sizeof(struct sep_des_private_context));

			sep_dump(sctx->sep_used, "ctx init des",
				&bctx->des_private_ctx, 40);
		} else {
			sep_read_context(sctx, &msg_offset,
			&bctx->aes_private_ctx,
			sizeof(struct sep_des_private_context));

			sep_dump(sctx->sep_used, "ctx init aes",
				&bctx->aes_private_ctx, 20);
		}

		/* We are done with init. Now send out the data */
		/* first release the sep */
		sctx->key_sent = 1;
		sep_crypto_release(sctx, -EINPROGRESS);

		spin_lock_irq(&queue_lock);
		int_error = crypto_enqueue_request(&sep_queue, &req->base);
		spin_unlock_irq(&queue_lock);

		if ((int_error != 0) && (int_error != -EINPROGRESS)) {
			dev_warn(&sctx->sep_used->pdev->dev,
				"spe cypher post op cant queue\n");
			sep_crypto_release(sctx, int_error);
			return int_error;
		}

		/* schedule the data send */
		int_error = sep_submit_work(sep->workqueue, sep_dequeuer,
			(void *)&sep_queue);

		if (int_error) {
			dev_warn(&sep->pdev->dev,
				"cant submit work sep_crypto_block\n");
			sep_crypto_release(sctx, -EINVAL);
			return -EINVAL;
		}

	} else {

		/**
		 * This is the result of a block request
		 */
		dev_dbg(&sctx->sep_used->pdev->dev,
			"crypto_post_op block response\n");

		u32_error = sep_verify_op(sctx, bctx->block_opcode,
			&msg_offset);

		if (u32_error) {
			dev_warn(&sctx->sep_used->pdev->dev,
				"sep block error %x\n", u32_error);
			sep_crypto_release(sctx, u32_error);
			return -EINVAL;
			}

		if (bctx->block_opcode == SEP_DES_BLOCK_OPCODE) {

			dev_dbg(&sctx->sep_used->pdev->dev,
				"post op for DES\n");

			/* special case for 1 block des */
			if (sep->current_cypher_req->nbytes ==
				crypto_ablkcipher_blocksize(tfm)) {

				sep_read_msg(sctx, small_buf,
					crypto_ablkcipher_blocksize(tfm),
					crypto_ablkcipher_blocksize(tfm) * 2,
					&msg_offset, 1);

				dev_dbg(&sctx->sep_used->pdev->dev,
					"reading in block des\n");

				copy_result = sg_copy_from_buffer(
					sctx->dst_sg,
					sep_sg_nents(sctx->dst_sg),
					small_buf,
					crypto_ablkcipher_blocksize(tfm));

				if (copy_result !=
					crypto_ablkcipher_blocksize(tfm)) {

					dev_warn(&sctx->sep_used->pdev->dev,
						"des block copy faild\n");
					sep_crypto_release(sctx, -ENOMEM);
					return -ENOMEM;
				}
			}

			/* Read Context */
			sep_read_context(sctx, &msg_offset,
				&bctx->des_private_ctx,
				sizeof(struct sep_des_private_context));
		} else {

			dev_dbg(&sctx->sep_used->pdev->dev,
				"post op for AES\n");

			/* Skip the MAC Output */
			msg_offset += (sizeof(u32) * 4);

			/* Read Context */
			sep_read_context(sctx, &msg_offset,
				&bctx->aes_private_ctx,
				sizeof(struct sep_aes_private_context));
		}

		sep_dump_sg(sctx->sep_used,
			"block sg out", sctx->dst_sg);

		/* Copy to correct sg if this block had oddball pages */
		if (sctx->dst_sg_hold)
			sep_copy_sg(sctx->sep_used,
				sctx->dst_sg,
				sctx->current_cypher_req->dst,
				sctx->current_cypher_req->nbytes);

		/* finished, release everything */
		sep_crypto_release(sctx, 0);
	}
	return 0;
}

static u32 hash_init_post_op(struct sep_device *sep)
{
	u32 u32_error;
	u32 msg_offset;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(sep->current_hash_req);
	struct sep_hash_ctx *ctx = ahash_request_ctx(sep->current_hash_req);
	struct sep_system_ctx *sctx = crypto_ahash_ctx(tfm);
	dev_dbg(&sctx->sep_used->pdev->dev,
		"hash init post op\n");

	sctx->done_with_transaction = 1;

	/* first bring msg from shared area to local area */
	memcpy(sctx->msg, sep->shared_addr,
		SEP_DRIVER_MESSAGE_SHARED_AREA_SIZE_IN_BYTES);

	u32_error = sep_verify_op(sctx, SEP_HASH_INIT_OPCODE,
		&msg_offset);

	if (u32_error) {
		dev_warn(&sctx->sep_used->pdev->dev, "hash init error %x\n",
			u32_error);
		sep_crypto_release(sctx, u32_error);
		return u32_error;
		}

	/* Read Context */
	sep_read_context(sctx, &msg_offset,
		&ctx->hash_private_ctx,
		sizeof(struct sep_hash_private_context));

	/* Signal to crypto infrastructure and clear out */
	dev_dbg(&sctx->sep_used->pdev->dev, "hash init post op done\n");
	sep_crypto_release(sctx, 0);
	return 0;
}

static u32 hash_update_post_op(struct sep_device *sep)
{
	u32 u32_error;
	u32 msg_offset;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(sep->current_hash_req);
	struct sep_hash_ctx *ctx = ahash_request_ctx(sep->current_hash_req);
	struct sep_system_ctx *sctx = crypto_ahash_ctx(tfm);
	dev_dbg(&sctx->sep_used->pdev->dev,
		"hash update post op\n");

	sctx->done_with_transaction = 1;

	/* first bring msg from shared area to local area */
	memcpy(sctx->msg, sep->shared_addr,
		SEP_DRIVER_MESSAGE_SHARED_AREA_SIZE_IN_BYTES);

	u32_error = sep_verify_op(sctx, SEP_HASH_UPDATE_OPCODE,
		&msg_offset);

	if (u32_error) {
		dev_warn(&sctx->sep_used->pdev->dev, "hash init error %x\n",
			u32_error);
		sep_crypto_release(sctx, u32_error);
		return u32_error;
		}

	/* Read Context */
	sep_read_context(sctx, &msg_offset,
		&ctx->hash_private_ctx,
		sizeof(struct sep_hash_private_context));

	sep_crypto_release(sctx, 0);
	return 0;
}

static u32 hash_final_post_op(struct sep_device *sep)
{
	int max_length;
	u32 u32_error;
	u32 msg_offset;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(sep->current_hash_req);
	struct sep_system_ctx *sctx = crypto_ahash_ctx(tfm);
	dev_dbg(&sctx->sep_used->pdev->dev,
		"hash final post op\n");

	sctx->done_with_transaction = 1;

	/* first bring msg from shared area to local area */
	memcpy(sctx->msg, sep->shared_addr,
		SEP_DRIVER_MESSAGE_SHARED_AREA_SIZE_IN_BYTES);

	u32_error = sep_verify_op(sctx, SEP_HASH_FINISH_OPCODE,
		&msg_offset);

	if (u32_error) {
		dev_warn(&sctx->sep_used->pdev->dev, "hash finish error %x\n",
			u32_error);
		sep_crypto_release(sctx, u32_error);
		return u32_error;
		}

	/* Grab the result */
	if (sctx->current_hash_req->result == NULL) {
		/* Oops, null buffer; error out here */
		dev_warn(&sctx->sep_used->pdev->dev,
			"hash finish null buffer\n");
		sep_crypto_release(sctx, (u32)-ENOMEM);
		return -ENOMEM;
		}

	max_length = (((SEP_HASH_RESULT_SIZE_WORDS * sizeof(u32)) + 3) /
		sizeof(u32)) * sizeof(u32);

	sep_read_msg(sctx,
		sctx->current_hash_req->result,
		crypto_ahash_digestsize(tfm), max_length,
		&msg_offset, 0);

	/* Signal to crypto infrastructure and clear out */
	dev_dbg(&sctx->sep_used->pdev->dev, "hash finish post op done\n");
	sep_crypto_release(sctx, 0);
	return 0;
}

static u32 hash_digest_post_op(struct sep_device *sep)
{
	int max_length;
	u32 u32_error;
	u32 msg_offset;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(sep->current_hash_req);
	struct sep_system_ctx *sctx = crypto_ahash_ctx(tfm);
	dev_dbg(&sctx->sep_used->pdev->dev,
		"hash digest post op\n");

	sctx->done_with_transaction = 1;

	/* first bring msg from shared area to local area */
	memcpy(sctx->msg, sep->shared_addr,
		SEP_DRIVER_MESSAGE_SHARED_AREA_SIZE_IN_BYTES);

	u32_error = sep_verify_op(sctx, SEP_HASH_SINGLE_OPCODE,
		&msg_offset);

	if (u32_error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"hash digest finish error %x\n", u32_error);

		sep_crypto_release(sctx, u32_error);
		return u32_error;
		}

	/* Grab the result */
	if (sctx->current_hash_req->result == NULL) {
		/* Oops, null buffer; error out here */
		dev_warn(&sctx->sep_used->pdev->dev,
			"hash digest finish null buffer\n");
		sep_crypto_release(sctx, (u32)-ENOMEM);
		return -ENOMEM;
		}

	max_length = (((SEP_HASH_RESULT_SIZE_WORDS * sizeof(u32)) + 3) /
		sizeof(u32)) * sizeof(u32);

	sep_read_msg(sctx,
		sctx->current_hash_req->result,
		crypto_ahash_digestsize(tfm), max_length,
		&msg_offset, 0);

	/* Signal to crypto infrastructure and clear out */
	dev_dbg(&sctx->sep_used->pdev->dev,
		"hash digest finish post op done\n");

	sep_crypto_release(sctx, 0);
	return 0;
}

/**
 * The sep_finish function is the function that is schedule (via tasket)
 * by the interrupt service routine when the SEP sends and interrupt
 * This is only called by the interrupt handler as a tasklet.
 */
static void sep_finish(unsigned long data)
{
	unsigned long flags;
	struct sep_device *sep_dev;
	int res;

	res = 0;

	if (data == 0) {
		pr_debug("sep_finish called with null data\n");
		return;
	}

	sep_dev = (struct sep_device *)data;
	if (sep_dev == NULL) {
		pr_debug("sep_finish; sep_dev is NULL\n");
		return;
	}

	spin_lock_irqsave(&sep_dev->busy_lock, flags);
	if (sep_dev->in_kernel == (u32)0) {
		spin_unlock_irqrestore(&sep_dev->busy_lock, flags);
		dev_warn(&sep_dev->pdev->dev,
			"sep_finish; not in kernel operation\n");
		return;
	}
	spin_unlock_irqrestore(&sep_dev->busy_lock, flags);

	/* Did we really do a sep command prior to this? */
	if (0 == test_bit(SEP_LEGACY_SENDMSG_DONE_OFFSET,
		&sep_dev->sctx->call_status.status)) {

		dev_warn(&sep_dev->pdev->dev, "[PID%d] sendmsg not called\n",
			current->pid);
		return;
	}

	if (sep_dev->send_ct != sep_dev->reply_ct) {
		dev_warn(&sep_dev->pdev->dev,
			"[PID%d] poll; no message came back\n",
			current->pid);
		return;
	}

	/* Check for error (In case time ran out) */
	if ((res != 0x0) && (res != 0x8)) {
		dev_warn(&sep_dev->pdev->dev,
			"[PID%d] poll; poll error GPR3 is %x\n",
			current->pid, res);
		return;
	}

	/* What kind of interrupt from sep was this? */
	res = sep_read_reg(sep_dev, HW_HOST_SEP_HOST_GPR2_REG_ADDR);

	dev_dbg(&sep_dev->pdev->dev, "[PID%d] GPR2 at crypto finish is %x\n",
		current->pid, res);

	/* Print request? */
	if ((res >> 30) & 0x1) {
		dev_dbg(&sep_dev->pdev->dev, "[PID%d] sep print req\n",
			current->pid);
		dev_dbg(&sep_dev->pdev->dev, "[PID%d] contents: %s\n",
			current->pid,
			(char *)(sep_dev->shared_addr +
			SEP_DRIVER_PRINTF_OFFSET_IN_BYTES));
		return;
	}

	/* Request for daemon (not currently in POR)? */
	if (res >> 31) {
		dev_dbg(&sep_dev->pdev->dev,
			"[PID%d] sep request; ignoring\n",
			current->pid);
		return;
	}

	/* If we got here, then we have a replay to a sep command */

	dev_dbg(&sep_dev->pdev->dev,
		"[PID%d] sep reply to command; processing request: %x\n",
		current->pid, sep_dev->current_request);

	switch (sep_dev->current_request) {
	case AES_CBC:
	case AES_ECB:
	case DES_CBC:
	case DES_ECB:
		res = crypto_post_op(sep_dev);
		break;
	case SHA1:
	case MD5:
	case SHA224:
	case SHA256:
		switch (sep_dev->current_hash_stage) {
		case HASH_INIT:
			res = hash_init_post_op(sep_dev);
			break;
		case HASH_UPDATE:
			res = hash_update_post_op(sep_dev);
			break;
		case HASH_FINISH:
			res = hash_final_post_op(sep_dev);
			break;
		case HASH_DIGEST:
			res = hash_digest_post_op(sep_dev);
			break;
		default:
			dev_warn(&sep_dev->pdev->dev,
			"invalid stage for hash finish\n");
		}
		break;
	default:
		dev_warn(&sep_dev->pdev->dev,
		"invalid request for finish\n");
	}

	if (res) {
		dev_warn(&sep_dev->pdev->dev,
		"finish returned error %x\n", res);
	}
}

static int sep_hash_cra_init(struct crypto_tfm *tfm)
	{
	struct sep_system_ctx *sctx = crypto_tfm_ctx(tfm);
	const char *alg_name = crypto_tfm_alg_name(tfm);

	sctx->sep_used = sep_dev;

	dev_dbg(&sctx->sep_used->pdev->dev,
		"sep_hash_cra_init name is %s\n", alg_name);

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
		sizeof(struct sep_hash_ctx));
	return 0;
	}

static void sep_hash_cra_exit(struct crypto_tfm *tfm)
{
	struct sep_system_ctx *sctx = crypto_tfm_ctx(tfm);

	dev_dbg(&sctx->sep_used->pdev->dev,
		"sep_hash_cra_exit\n");
	sctx->sep_used = NULL;
}

static void sep_hash_init(void *data)
{
	u32 msg_offset;
	int result;
	struct ahash_request *req;
	struct crypto_ahash *tfm;
	struct sep_hash_ctx *ctx;
	struct sep_system_ctx *sctx;

	req = (struct ahash_request *)data;
	tfm = crypto_ahash_reqtfm(req);
	ctx = ahash_request_ctx(req);
	sctx = crypto_ahash_ctx(tfm);

	dev_dbg(&sctx->sep_used->pdev->dev,
		"sep_hash_init\n");
	sctx->current_hash_stage = HASH_INIT;
	/* opcode and mode */
	sep_make_header(sctx, &msg_offset, SEP_HASH_INIT_OPCODE);
	sep_write_msg(sctx, &ctx->hash_opmode,
		sizeof(u32), sizeof(u32), &msg_offset, 0);
	sep_end_msg(sctx, msg_offset);

	sctx->done_with_transaction = 0;

	result = sep_crypto_take_sep(sctx);
	if (result) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_hash_init take sep failed\n");
		sep_crypto_release(sctx, -EINVAL);
	}

	/**
	 * Sep is now working. Lets wait up to 5 seconds
	 * for completion. If it does not complete, we will do
	 * a crypto release with -EINVAL to release the
	 * kernel crypto infrastructure and let the system
	 * continue to boot up
	 * We have to wait this long because some crypto
	 * operations can take a while
	 */
	dev_dbg(&sctx->sep_used->pdev->dev,
		"waiting for done with transaction\n");

	sctx->end_time = jiffies + (SEP_TRANSACTION_WAIT_TIME * HZ);
	while ((time_before(jiffies, sctx->end_time)) &&
		(!sctx->done_with_transaction))
		schedule();

	dev_dbg(&sctx->sep_used->pdev->dev,
		"done waiting for done with transaction\n");

	/* are we done? */
	if (!sctx->done_with_transaction) {
		/* Nope, lets release and tell crypto no */
		dev_warn(&sctx->sep_used->pdev->dev,
			"[PID%d] sep_hash_init never finished\n",
			current->pid);
		sep_crypto_release(sctx, -EINVAL);
	}
}

static void sep_hash_update(void *data)
{
	int int_error;
	u32 msg_offset;
	u32 len;
	struct sep_hash_internal_context *int_ctx;
	u32 block_size;
	u32 head_len;
	u32 tail_len;
	static u32 msg[10];
	static char small_buf[100];
	void *src_ptr;
	struct scatterlist *new_sg;
	ssize_t copy_result;
	struct ahash_request *req;
	struct crypto_ahash *tfm;
	struct sep_hash_ctx *ctx;
	struct sep_system_ctx *sctx;

	req = (struct ahash_request *)data;
	tfm = crypto_ahash_reqtfm(req);
	ctx = ahash_request_ctx(req);
	sctx = crypto_ahash_ctx(tfm);

	/* length for queue status */
	sctx->nbytes = req->nbytes;

	dev_dbg(&sctx->sep_used->pdev->dev,
		"sep_hash_update\n");
	sctx->current_hash_stage = HASH_UPDATE;
	len = req->nbytes;

	block_size = crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));
	tail_len = req->nbytes % block_size;
	dev_dbg(&sctx->sep_used->pdev->dev, "length is %x\n", len);
	dev_dbg(&sctx->sep_used->pdev->dev, "block_size is %x\n", block_size);
	dev_dbg(&sctx->sep_used->pdev->dev, "tail len is %x\n", tail_len);

	/* Compute header/tail sizes */
	int_ctx = (struct sep_hash_internal_context *)&ctx->
		hash_private_ctx.internal_context;
	head_len = (block_size - int_ctx->prev_update_bytes) % block_size;
	tail_len = (req->nbytes - head_len) % block_size;

	/* Make sure all pages are even block */
	int_error = sep_oddball_pages(sctx->sep_used, req->src,
		req->nbytes,
		block_size, &new_sg, 1);

	if (int_error < 0) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"oddball pages error in crash update\n");
		sep_crypto_release(sctx, -ENOMEM);
		return;
	} else if (int_error == 1) {
		sctx->src_sg = new_sg;
		sctx->src_sg_hold = new_sg;
	} else {
		sctx->src_sg = req->src;
		sctx->src_sg_hold = NULL;
	}

	src_ptr = sg_virt(sctx->src_sg);

	if ((!req->nbytes) || (!ctx->sg)) {
		/* null data */
		src_ptr = NULL;
	}

	sep_dump_sg(sctx->sep_used, "hash block sg in", sctx->src_sg);

	sctx->dcb_input_data.app_in_address = src_ptr;
	sctx->dcb_input_data.data_in_size = req->nbytes - (head_len + tail_len);
	sctx->dcb_input_data.app_out_address = NULL;
	sctx->dcb_input_data.block_size = block_size;
	sctx->dcb_input_data.tail_block_size = 0;
	sctx->dcb_input_data.is_applet = 0;
	sctx->dcb_input_data.src_sg = sctx->src_sg;
	sctx->dcb_input_data.dst_sg = NULL;

	int_error = sep_create_dcb_dmatables_context_kernel(
		sctx->sep_used,
		&sctx->dcb_region,
		&sctx->dmatables_region,
		&sctx->dma_ctx,
		&sctx->dcb_input_data,
		1);
	if (int_error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"hash update dma table create failed\n");
		sep_crypto_release(sctx, -EINVAL);
		return;
	}

	/* Construct message to SEP */
	sep_make_header(sctx, &msg_offset, SEP_HASH_UPDATE_OPCODE);

	msg[0] = (u32)0;
	msg[1] = (u32)0;
	msg[2] = (u32)0;

	sep_write_msg(sctx, msg, sizeof(u32) * 3, sizeof(u32) * 3,
		&msg_offset, 0);

	/* Handle remainders */

	/* Head */
	sep_write_msg(sctx, &head_len, sizeof(u32),
		sizeof(u32), &msg_offset, 0);

	if (head_len) {
		copy_result = sg_copy_to_buffer(
			req->src,
			sep_sg_nents(sctx->src_sg),
			small_buf, head_len);

		if (copy_result != head_len) {
			dev_warn(&sctx->sep_used->pdev->dev,
				"sg head copy failure in hash block\n");
			sep_crypto_release(sctx, -ENOMEM);
			return;
		}

		sep_write_msg(sctx, small_buf, head_len,
			sizeof(u32) * 32, &msg_offset, 1);
	} else {
		msg_offset += sizeof(u32) * 32;
	}

	/* Tail */
	sep_write_msg(sctx, &tail_len, sizeof(u32),
		sizeof(u32), &msg_offset, 0);

	if (tail_len) {
		copy_result = sep_copy_offset_sg(
			sctx->sep_used,
			sctx->src_sg,
			req->nbytes - tail_len,
			small_buf, tail_len);

		if (copy_result != tail_len) {
			dev_warn(&sctx->sep_used->pdev->dev,
				"sg tail copy failure in hash block\n");
			sep_crypto_release(sctx, -ENOMEM);
			return;
		}

		sep_write_msg(sctx, small_buf, tail_len,
			sizeof(u32) * 32, &msg_offset, 1);
	} else {
		msg_offset += sizeof(u32) * 32;
	}

	/* Context */
	sep_write_context(sctx, &msg_offset, &ctx->hash_private_ctx,
		sizeof(struct sep_hash_private_context));

	sep_end_msg(sctx, msg_offset);
	sctx->done_with_transaction = 0;
	int_error = sep_crypto_take_sep(sctx);
	if (int_error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_hash_update take sep failed\n");
		sep_crypto_release(sctx, -EINVAL);
	}

	/**
	 * Sep is now working. Lets wait up to 5 seconds
	 * for completion. If it does not complete, we will do
	 * a crypto release with -EINVAL to release the
	 * kernel crypto infrastructure and let the system
	 * continue to boot up
	 * We have to wait this long because some crypto
	 * operations can take a while
	 */
	dev_dbg(&sctx->sep_used->pdev->dev,
		"waiting for done with transaction\n");

	sctx->end_time = jiffies + (SEP_TRANSACTION_WAIT_TIME * HZ);
	while ((time_before(jiffies, sctx->end_time)) &&
		(!sctx->done_with_transaction))
		schedule();

	dev_dbg(&sctx->sep_used->pdev->dev,
		"done waiting for done with transaction\n");

	/* are we done? */
	if (!sctx->done_with_transaction) {
		/* Nope, lets release and tell crypto no */
		dev_warn(&sctx->sep_used->pdev->dev,
			"[PID%d] sep_hash_update never finished\n",
			current->pid);
		sep_crypto_release(sctx, -EINVAL);
	}
}

static void sep_hash_final(void *data)
{
	u32 msg_offset;
	struct ahash_request *req;
	struct crypto_ahash *tfm;
	struct sep_hash_ctx *ctx;
	struct sep_system_ctx *sctx;
	int result;

	req = (struct ahash_request *)data;
	tfm = crypto_ahash_reqtfm(req);
	ctx = ahash_request_ctx(req);
	sctx = crypto_ahash_ctx(tfm);

	dev_dbg(&sctx->sep_used->pdev->dev,
		"sep_hash_final\n");
	sctx->current_hash_stage = HASH_FINISH;

	/* opcode and mode */
	sep_make_header(sctx, &msg_offset, SEP_HASH_FINISH_OPCODE);

	/* Context */
	sep_write_context(sctx, &msg_offset, &ctx->hash_private_ctx,
		sizeof(struct sep_hash_private_context));

	sep_end_msg(sctx, msg_offset);
	sctx->done_with_transaction = 0;
	result = sep_crypto_take_sep(sctx);
	if (result) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_hash_final take sep failed\n");
		sep_crypto_release(sctx, -EINVAL);
	}

	/**
	 * Sep is now working. Lets wait up to 5 seconds
	 * for completion. If it does not complete, we will do
	 * a crypto release with -EINVAL to release the
	 * kernel crypto infrastructure and let the system
	 * continue to boot up
	 * We have to wait this long because some crypto
	 * operations can take a while
	 */
	dev_dbg(&sctx->sep_used->pdev->dev,
		"waiting for done with transaction\n");

	sctx->end_time = jiffies + (SEP_TRANSACTION_WAIT_TIME * HZ);
	while ((time_before(jiffies, sctx->end_time)) &&
		(!sctx->done_with_transaction))
		schedule();

	dev_dbg(&sctx->sep_used->pdev->dev,
		"done waiting for done with transaction\n");

	/* are we done? */
	if (!sctx->done_with_transaction) {
		/* Nope, lets release and tell crypto no */
		dev_warn(&sctx->sep_used->pdev->dev,
			"[PID%d] sep_hash_final never finished\n",
			current->pid);
		sep_crypto_release(sctx, -EINVAL);
	}
}

static void sep_hash_digest(void *data)
{
	int int_error;
	u32 msg_offset;
	u32 block_size;
	u32 msg[10];
	size_t copy_result;
	int result;
	u32 tail_len;
	static char small_buf[100];
	struct scatterlist *new_sg;
	void *src_ptr;

	struct ahash_request *req;
	struct crypto_ahash *tfm;
	struct sep_hash_ctx *ctx;
	struct sep_system_ctx *sctx;

	req = (struct ahash_request *)data;
	tfm = crypto_ahash_reqtfm(req);
	ctx = ahash_request_ctx(req);
	sctx = crypto_ahash_ctx(tfm);

	dev_dbg(&sctx->sep_used->pdev->dev,
		"sep_hash_digest\n");
	sctx->current_hash_stage = HASH_DIGEST;

	/* length for queue status */
	sctx->nbytes = req->nbytes;

	block_size = crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));
	tail_len = req->nbytes % block_size;
	dev_dbg(&sctx->sep_used->pdev->dev, "length is %x\n", req->nbytes);
	dev_dbg(&sctx->sep_used->pdev->dev, "block_size is %x\n", block_size);
	dev_dbg(&sctx->sep_used->pdev->dev, "tail len is %x\n", tail_len);

	/* Make sure all pages are even block */
	int_error = sep_oddball_pages(sctx->sep_used, req->src,
		req->nbytes,
		block_size, &new_sg, 1);

	if (int_error < 0) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"oddball pages error in crash update\n");
		sep_crypto_release(sctx, -ENOMEM);
		return;
	} else if (int_error == 1) {
		sctx->src_sg = new_sg;
		sctx->src_sg_hold = new_sg;
	} else {
		sctx->src_sg = req->src;
		sctx->src_sg_hold = NULL;
	}

	src_ptr = sg_virt(sctx->src_sg);

	if ((!req->nbytes) || (!ctx->sg)) {
		/* null data */
		src_ptr = NULL;
	}

	sep_dump_sg(sctx->sep_used, "hash block sg in", sctx->src_sg);

	sctx->dcb_input_data.app_in_address = src_ptr;
	sctx->dcb_input_data.data_in_size = req->nbytes - tail_len;
	sctx->dcb_input_data.app_out_address = NULL;
	sctx->dcb_input_data.block_size = block_size;
	sctx->dcb_input_data.tail_block_size = 0;
	sctx->dcb_input_data.is_applet = 0;
	sctx->dcb_input_data.src_sg = sctx->src_sg;
	sctx->dcb_input_data.dst_sg = NULL;

	int_error = sep_create_dcb_dmatables_context_kernel(
		sctx->sep_used,
		&sctx->dcb_region,
		&sctx->dmatables_region,
		&sctx->dma_ctx,
		&sctx->dcb_input_data,
		1);
	if (int_error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"hash update dma table create failed\n");
		sep_crypto_release(sctx, -EINVAL);
		return;
	}

	/* Construct message to SEP */
	sep_make_header(sctx, &msg_offset, SEP_HASH_SINGLE_OPCODE);
	sep_write_msg(sctx, &ctx->hash_opmode,
		sizeof(u32), sizeof(u32), &msg_offset, 0);

	msg[0] = (u32)0;
	msg[1] = (u32)0;
	msg[2] = (u32)0;

	sep_write_msg(sctx, msg, sizeof(u32) * 3, sizeof(u32) * 3,
		&msg_offset, 0);

	/* Tail */
	sep_write_msg(sctx, &tail_len, sizeof(u32),
		sizeof(u32), &msg_offset, 0);

	if (tail_len) {
		copy_result = sep_copy_offset_sg(
			sctx->sep_used,
			sctx->src_sg,
			req->nbytes - tail_len,
			small_buf, tail_len);

		if (copy_result != tail_len) {
			dev_warn(&sctx->sep_used->pdev->dev,
				"sg tail copy failure in hash block\n");
			sep_crypto_release(sctx, -ENOMEM);
			return;
		}

		sep_write_msg(sctx, small_buf, tail_len,
			sizeof(u32) * 32, &msg_offset, 1);
	} else {
		msg_offset += sizeof(u32) * 32;
	}

	sep_end_msg(sctx, msg_offset);

	sctx->done_with_transaction = 0;

	result = sep_crypto_take_sep(sctx);
	if (result) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_hash_digest take sep failed\n");
		sep_crypto_release(sctx, -EINVAL);
	}

	/**
	 * Sep is now working. Lets wait up to 5 seconds
	 * for completion. If it does not complete, we will do
	 * a crypto release with -EINVAL to release the
	 * kernel crypto infrastructure and let the system
	 * continue to boot up
	 * We have to wait this long because some crypto
	 * operations can take a while
	 */
	dev_dbg(&sctx->sep_used->pdev->dev,
		"waiting for done with transaction\n");

	sctx->end_time = jiffies + (SEP_TRANSACTION_WAIT_TIME * HZ);
	while ((time_before(jiffies, sctx->end_time)) &&
		(!sctx->done_with_transaction))
		schedule();

	dev_dbg(&sctx->sep_used->pdev->dev,
		"done waiting for done with transaction\n");

	/* are we done? */
	if (!sctx->done_with_transaction) {
		/* Nope, lets release and tell crypto no */
		dev_warn(&sctx->sep_used->pdev->dev,
			"[PID%d] sep_hash_digest never finished\n",
			current->pid);
		sep_crypto_release(sctx, -EINVAL);
	}
}

/**
 * This is what is called by each of the API's provided
 * in the kernel crypto descriptors. It is run in a process
 * context using the kernel workqueues. Therefore it can
 * be put to sleep.
 */
static void sep_dequeuer(void *data)
{
	struct crypto_queue *this_queue;
	struct crypto_async_request *async_req;
	struct crypto_async_request *backlog;
	struct ablkcipher_request *cypher_req;
	struct ahash_request *hash_req;
	struct sep_system_ctx *sctx;
	struct crypto_ahash *hash_tfm;


	this_queue = (struct crypto_queue *)data;

	spin_lock_irq(&queue_lock);
	backlog = crypto_get_backlog(this_queue);
	async_req = crypto_dequeue_request(this_queue);
	spin_unlock_irq(&queue_lock);

	if (!async_req) {
		pr_debug("sep crypto queue is empty\n");
		return;
	}

	if (backlog) {
		pr_debug("sep crypto backlog set\n");
		if (backlog->complete)
			backlog->complete(backlog, -EINPROGRESS);
		backlog = NULL;
	}

	if (!async_req->tfm) {
		pr_debug("sep crypto queue null tfm\n");
		return;
	}

	if (!async_req->tfm->__crt_alg) {
		pr_debug("sep crypto queue null __crt_alg\n");
		return;
	}

	if (!async_req->tfm->__crt_alg->cra_type) {
		pr_debug("sep crypto queue null cra_type\n");
		return;
	}

	/* we have stuff in the queue */
	if (async_req->tfm->__crt_alg->cra_type !=
		&crypto_ahash_type) {
		/* This is for a cypher */
		pr_debug("sep crypto queue doing cipher\n");
		cypher_req = container_of(async_req,
			struct ablkcipher_request,
			base);
		if (!cypher_req) {
			pr_debug("sep crypto queue null cypher_req\n");
			return;
		}

		sep_crypto_block((void *)cypher_req);
		return;
	} else {
		/* This is a hash */
		pr_debug("sep crypto queue doing hash\n");
		/**
		 * This is a bit more complex than cipher; we
		 * need to figure out what type of operation
		 */
		hash_req = ahash_request_cast(async_req);
		if (!hash_req) {
			pr_debug("sep crypto queue null hash_req\n");
			return;
		}

		hash_tfm = crypto_ahash_reqtfm(hash_req);
		if (!hash_tfm) {
			pr_debug("sep crypto queue null hash_tfm\n");
			return;
		}


		sctx = crypto_ahash_ctx(hash_tfm);
		if (!sctx) {
			pr_debug("sep crypto queue null sctx\n");
			return;
		}

		if (sctx->current_hash_stage == HASH_INIT) {
			pr_debug("sep crypto queue hash init\n");
			sep_hash_init((void *)hash_req);
			return;
		} else if (sctx->current_hash_stage == HASH_UPDATE) {
			pr_debug("sep crypto queue hash update\n");
			sep_hash_update((void *)hash_req);
			return;
		} else if (sctx->current_hash_stage == HASH_FINISH) {
			pr_debug("sep crypto queue hash final\n");
			sep_hash_final((void *)hash_req);
			return;
		} else if (sctx->current_hash_stage == HASH_DIGEST) {
			pr_debug("sep crypto queue hash digest\n");
			sep_hash_digest((void *)hash_req);
			return;
		} else {
			pr_debug("sep crypto queue hash oops nothing\n");
			return;
		}
	}
}

static int sep_sha1_init(struct ahash_request *req)
{
	int error;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sep_hash_ctx *ctx = ahash_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ahash_ctx(tfm);

	dev_dbg(&sctx->sep_used->pdev->dev, "doing sha1 init\n");
	sctx->current_request = SHA1;
	sctx->current_hash_req = req;
	sctx->current_cypher_req = NULL;
	ctx->hash_opmode = SEP_HASH_SHA1;
	sctx->current_hash_stage = HASH_INIT;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep sha1 init cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sha1 init cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;
}

static int sep_sha1_update(struct ahash_request *req)
{
	int error;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sep_hash_ctx *ctx = ahash_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ahash_ctx(tfm);

	dev_dbg(&sctx->sep_used->pdev->dev, "doing sha1 update\n");
	sctx->current_request = SHA1;
	sctx->current_hash_req = req;
	sctx->current_cypher_req = NULL;
	ctx->hash_opmode = SEP_HASH_SHA1;
	sctx->current_hash_stage = HASH_INIT;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep sha1 update cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sha1 update cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;
}

static int sep_sha1_final(struct ahash_request *req)
{
	int error;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sep_hash_ctx *ctx = ahash_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ahash_ctx(tfm);
	dev_dbg(&sctx->sep_used->pdev->dev, "doing sha1 final\n");

	sctx->current_request = SHA1;
	sctx->current_hash_req = req;
	sctx->current_cypher_req = NULL;
	ctx->hash_opmode = SEP_HASH_SHA1;
	sctx->current_hash_stage = HASH_FINISH;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep sha1 final cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sha1 final cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;

}

static int sep_sha1_digest(struct ahash_request *req)
{
	int error;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sep_hash_ctx *ctx = ahash_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ahash_ctx(tfm);
	dev_dbg(&sctx->sep_used->pdev->dev, "doing sha1 digest\n");

	sctx->current_request = SHA1;
	sctx->current_hash_req = req;
	sctx->current_cypher_req = NULL;
	ctx->hash_opmode = SEP_HASH_SHA1;
	sctx->current_hash_stage = HASH_DIGEST;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep sha1 digest cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sha1 digest cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;

}

static int sep_md5_init(struct ahash_request *req)
{
	int error;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sep_hash_ctx *ctx = ahash_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ahash_ctx(tfm);
	dev_dbg(&sctx->sep_used->pdev->dev, "doing md5 init\n");

	sctx->current_request = MD5;
	sctx->current_hash_req = req;
	sctx->current_cypher_req = NULL;
	ctx->hash_opmode = SEP_HASH_MD5;
	sctx->current_hash_stage = HASH_INIT;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep md5 init cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"md5 init cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;

}

static int sep_md5_update(struct ahash_request *req)
{
	int error;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sep_hash_ctx *ctx = ahash_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ahash_ctx(tfm);
	dev_dbg(&sctx->sep_used->pdev->dev, "doing md5 update\n");

	sctx->current_request = MD5;
	sctx->current_hash_req = req;
	sctx->current_cypher_req = NULL;
	ctx->hash_opmode = SEP_HASH_MD5;
	sctx->current_hash_stage = HASH_UPDATE;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"md5 update cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"md5 update cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;
}

static int sep_md5_final(struct ahash_request *req)
{
	int error;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sep_hash_ctx *ctx = ahash_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ahash_ctx(tfm);
	dev_dbg(&sctx->sep_used->pdev->dev, "doing md5 final\n");

	sctx->current_request = MD5;
	sctx->current_hash_req = req;
	sctx->current_cypher_req = NULL;
	ctx->hash_opmode = SEP_HASH_MD5;
	sctx->current_hash_stage = HASH_FINISH;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep md5 final cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"md5 final cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;

}

static int sep_md5_digest(struct ahash_request *req)
{
	int error;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sep_hash_ctx *ctx = ahash_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ahash_ctx(tfm);

	dev_dbg(&sctx->sep_used->pdev->dev, "doing md5 digest\n");
	sctx->current_request = MD5;
	sctx->current_hash_req = req;
	sctx->current_cypher_req = NULL;
	ctx->hash_opmode = SEP_HASH_MD5;
	sctx->current_hash_stage = HASH_DIGEST;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep md5 digest cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"md5 digest cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;
}

static int sep_sha224_init(struct ahash_request *req)
{
	int error;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sep_hash_ctx *ctx = ahash_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ahash_ctx(tfm);
	dev_dbg(&sctx->sep_used->pdev->dev, "doing sha224 init\n");

	sctx->current_request = SHA224;
	sctx->current_hash_req = req;
	sctx->current_cypher_req = NULL;
	ctx->hash_opmode = SEP_HASH_SHA224;
	sctx->current_hash_stage = HASH_INIT;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep sha224 init cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sha224 init cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;
}

static int sep_sha224_update(struct ahash_request *req)
{
	int error;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sep_hash_ctx *ctx = ahash_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ahash_ctx(tfm);
	dev_dbg(&sctx->sep_used->pdev->dev, "doing sha224 update\n");

	sctx->current_request = SHA224;
	sctx->current_hash_req = req;
	sctx->current_cypher_req = NULL;
	ctx->hash_opmode = SEP_HASH_SHA224;
	sctx->current_hash_stage = HASH_UPDATE;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep sha224 update cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sha224 update cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;
}

static int sep_sha224_final(struct ahash_request *req)
{
	int error;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sep_hash_ctx *ctx = ahash_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ahash_ctx(tfm);
	dev_dbg(&sctx->sep_used->pdev->dev, "doing sha224 final\n");

	sctx->current_request = SHA224;
	sctx->current_hash_req = req;
	sctx->current_cypher_req = NULL;
	ctx->hash_opmode = SEP_HASH_SHA224;
	sctx->current_hash_stage = HASH_FINISH;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep sha224 final cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sha224 final cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;
}

static int sep_sha224_digest(struct ahash_request *req)
{
	int error;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sep_hash_ctx *ctx = ahash_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ahash_ctx(tfm);

	dev_dbg(&sctx->sep_used->pdev->dev, "doing 224 digest\n");
	sctx->current_request = SHA224;
	sctx->current_hash_req = req;
	sctx->current_cypher_req = NULL;
	ctx->hash_opmode = SEP_HASH_SHA224;
	sctx->current_hash_stage = HASH_DIGEST;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep sha224 digest cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sha256 digest cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;
}

static int sep_sha256_init(struct ahash_request *req)
{
	int error;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sep_hash_ctx *ctx = ahash_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ahash_ctx(tfm);
	dev_dbg(&sctx->sep_used->pdev->dev, "doing sha256 init\n");

	sctx->current_request = SHA256;
	sctx->current_hash_req = req;
	sctx->current_cypher_req = NULL;
	ctx->hash_opmode = SEP_HASH_SHA256;
	sctx->current_hash_stage = HASH_INIT;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep sha256 init cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sha256 init cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;
}

static int sep_sha256_update(struct ahash_request *req)
{
	int error;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sep_hash_ctx *ctx = ahash_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ahash_ctx(tfm);
	dev_dbg(&sctx->sep_used->pdev->dev, "doing sha256 update\n");

	sctx->current_request = SHA256;
	sctx->current_hash_req = req;
	sctx->current_cypher_req = NULL;
	ctx->hash_opmode = SEP_HASH_SHA256;
	sctx->current_hash_stage = HASH_UPDATE;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep sha256 update cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sha256 update cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;
}

static int sep_sha256_final(struct ahash_request *req)
{
	int error;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sep_hash_ctx *ctx = ahash_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ahash_ctx(tfm);
	dev_dbg(&sctx->sep_used->pdev->dev, "doing sha256 final\n");

	sctx->current_request = SHA256;
	sctx->current_hash_req = req;
	sctx->current_cypher_req = NULL;
	ctx->hash_opmode = SEP_HASH_SHA256;
	sctx->current_hash_stage = HASH_FINISH;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep sha256 final cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sha256 final cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;
}

static int sep_sha256_digest(struct ahash_request *req)
{
	int error;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sep_hash_ctx *ctx = ahash_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ahash_ctx(tfm);

	dev_dbg(&sctx->sep_used->pdev->dev, "doing sha256 digest\n");
	sctx->current_request = SHA256;
	sctx->current_hash_req = req;
	sctx->current_cypher_req = NULL;
	ctx->hash_opmode = SEP_HASH_SHA256;
	sctx->current_hash_stage = HASH_DIGEST;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep sha256 digest cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sha256 digest cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;
}

static int sep_crypto_init(struct crypto_tfm *tfm)
{
	struct sep_system_ctx *sctx = crypto_tfm_ctx(tfm);
	const char *alg_name = crypto_tfm_alg_name(tfm);

	sctx->sep_used = sep_dev;

	if (alg_name == NULL)
		dev_dbg(&sctx->sep_used->pdev->dev, "alg is NULL\n");
	else
		dev_dbg(&sctx->sep_used->pdev->dev, "alg is %s\n", alg_name);

	tfm->crt_ablkcipher.reqsize = sizeof(struct sep_block_ctx);
	dev_dbg(&sctx->sep_used->pdev->dev, "sep_crypto_init\n");
	return 0;
}

static void sep_crypto_exit(struct crypto_tfm *tfm)
{
	struct sep_system_ctx *sctx = crypto_tfm_ctx(tfm);
	dev_dbg(&sctx->sep_used->pdev->dev, "sep_crypto_exit\n");
	sctx->sep_used = NULL;
}

static int sep_aes_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
	unsigned int keylen)
{
	struct sep_system_ctx *sctx = crypto_ablkcipher_ctx(tfm);

	dev_dbg(&sctx->sep_used->pdev->dev, "sep aes setkey\n");

	switch (keylen) {
	case SEP_AES_KEY_128_SIZE:
		sctx->aes_key_size = AES_128;
		break;
	case SEP_AES_KEY_192_SIZE:
		sctx->aes_key_size = AES_192;
		break;
	case SEP_AES_KEY_256_SIZE:
		sctx->aes_key_size = AES_256;
		break;
	case SEP_AES_KEY_512_SIZE:
		sctx->aes_key_size = AES_512;
		break;
	default:
		dev_warn(&sctx->sep_used->pdev->dev, "sep aes key size %x\n",
			keylen);
		return -EINVAL;
	}

	memset(&sctx->key.aes, 0, sizeof(u32) *
		SEP_AES_MAX_KEY_SIZE_WORDS);
	memcpy(&sctx->key.aes, key, keylen);
	sctx->keylen = keylen;
	/* Indicate to encrypt/decrypt function to send key to SEP */
	sctx->key_sent = 0;
	sctx->last_block = 0;

	return 0;
}

static int sep_aes_ecb_encrypt(struct ablkcipher_request *req)
{
	int error;
	struct sep_block_ctx *bctx = ablkcipher_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ablkcipher_ctx(
		crypto_ablkcipher_reqtfm(req));

	dev_dbg(&sctx->sep_used->pdev->dev, "sep aes ecb encrypt\n");
	sctx->current_request = AES_ECB;
	sctx->current_hash_req = NULL;
	sctx->current_cypher_req = req;
	bctx->aes_encmode = SEP_AES_ENCRYPT;
	bctx->aes_opmode = SEP_AES_ECB;
	bctx->init_opcode = SEP_AES_INIT_OPCODE;
	bctx->block_opcode = SEP_AES_BLOCK_OPCODE;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_aes_ecb_encrypt cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_aes_ecb_encrypt cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;
}

static int sep_aes_ecb_decrypt(struct ablkcipher_request *req)
{
	int error;
	struct sep_block_ctx *bctx = ablkcipher_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ablkcipher_ctx(
		crypto_ablkcipher_reqtfm(req));

	dev_dbg(&sctx->sep_used->pdev->dev, "sep aes ecb decrypt\n");
	sctx->current_request = AES_ECB;
	sctx->current_hash_req = NULL;
	sctx->current_cypher_req = req;
	bctx->aes_encmode = SEP_AES_DECRYPT;
	bctx->aes_opmode = SEP_AES_ECB;
	bctx->init_opcode = SEP_AES_INIT_OPCODE;
	bctx->block_opcode = SEP_AES_BLOCK_OPCODE;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_aes_ecb_decrypt cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_aes_ecb_decrypt cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;
}

static int sep_aes_cbc_encrypt(struct ablkcipher_request *req)
{
	int error;
	struct sep_block_ctx *bctx = ablkcipher_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ablkcipher_ctx(
		crypto_ablkcipher_reqtfm(req));

	dev_dbg(&sctx->sep_used->pdev->dev, "sep aes cbc encrypt\n");
	sctx->current_request = AES_CBC;
	sctx->current_hash_req = NULL;
	sctx->current_cypher_req = req;
	bctx->aes_encmode = SEP_AES_ENCRYPT;
	bctx->aes_opmode = SEP_AES_CBC;
	bctx->init_opcode = SEP_AES_INIT_OPCODE;
	bctx->block_opcode = SEP_AES_BLOCK_OPCODE;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_aes_cbc_encrypt cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_aes_cbc_encrypt cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;
}

static int sep_aes_cbc_decrypt(struct ablkcipher_request *req)
{
	int error;
	struct sep_block_ctx *bctx = ablkcipher_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ablkcipher_ctx(
		crypto_ablkcipher_reqtfm(req));

	dev_dbg(&sctx->sep_used->pdev->dev, "sep aes cbc decrypt\n");
	sctx->current_request = AES_CBC;
	sctx->current_hash_req = NULL;
	sctx->current_cypher_req = req;
	bctx->aes_encmode = SEP_AES_DECRYPT;
	bctx->aes_opmode = SEP_AES_CBC;
	bctx->init_opcode = SEP_AES_INIT_OPCODE;
	bctx->block_opcode = SEP_AES_BLOCK_OPCODE;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_aes_cbc_decrypt cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_aes_cbc_decrypt cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;
}

static int sep_des_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
	unsigned int keylen)
{
	struct sep_system_ctx *sctx = crypto_ablkcipher_ctx(tfm);
	struct crypto_tfm *ctfm = crypto_ablkcipher_tfm(tfm);
	u32 *flags  = &ctfm->crt_flags;

	dev_dbg(&sctx->sep_used->pdev->dev, "sep des setkey\n");

	switch (keylen) {
	case DES_KEY_SIZE:
		sctx->des_nbr_keys = DES_KEY_1;
		break;
	case DES_KEY_SIZE * 2:
		sctx->des_nbr_keys = DES_KEY_2;
		break;
	case DES_KEY_SIZE * 3:
		sctx->des_nbr_keys = DES_KEY_3;
		break;
	default:
		dev_dbg(&sctx->sep_used->pdev->dev, "invalid key size %x\n",
			keylen);
		return -EINVAL;
	}

	if ((*flags & CRYPTO_TFM_REQ_WEAK_KEY) &&
		(sep_weak_key(key, keylen))) {

		*flags |= CRYPTO_TFM_RES_WEAK_KEY;
		dev_warn(&sctx->sep_used->pdev->dev, "weak key\n");
		return -EINVAL;
	}

	memset(&sctx->key.des, 0, sizeof(struct sep_des_key));
	memcpy(&sctx->key.des.key1, key, keylen);
	sctx->keylen = keylen;
	/* Indicate to encrypt/decrypt function to send key to SEP */
	sctx->key_sent = 0;
	sctx->last_block = 0;

	return 0;
}

static int sep_des_ebc_encrypt(struct ablkcipher_request *req)
{
	int error;
	struct sep_block_ctx *bctx = ablkcipher_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ablkcipher_ctx(
		crypto_ablkcipher_reqtfm(req));

	dev_dbg(&sctx->sep_used->pdev->dev, "sep des ecb encrypt\n");
	sctx->current_request = DES_ECB;
	sctx->current_hash_req = NULL;
	sctx->current_cypher_req = req;
	bctx->des_encmode = SEP_DES_ENCRYPT;
	bctx->des_opmode = SEP_DES_ECB;
	bctx->init_opcode = SEP_DES_INIT_OPCODE;
	bctx->block_opcode = SEP_DES_BLOCK_OPCODE;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_des_ecb_encrypt cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_des_ecb_encrypt cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;
}

static int sep_des_ebc_decrypt(struct ablkcipher_request *req)
{
	int error;
	struct sep_block_ctx *bctx = ablkcipher_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ablkcipher_ctx(
		crypto_ablkcipher_reqtfm(req));

	dev_dbg(&sctx->sep_used->pdev->dev, "sep des ecb decrypt\n");
	sctx->current_request = DES_ECB;
	sctx->current_hash_req = NULL;
	sctx->current_cypher_req = req;
	bctx->des_encmode = SEP_DES_DECRYPT;
	bctx->des_opmode = SEP_DES_ECB;
	bctx->init_opcode = SEP_DES_INIT_OPCODE;
	bctx->block_opcode = SEP_DES_BLOCK_OPCODE;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_des_ecb_decrypt cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_des_ecb_decrypt cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;
}

static int sep_des_cbc_encrypt(struct ablkcipher_request *req)
{
	int error;
	struct sep_block_ctx *bctx = ablkcipher_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ablkcipher_ctx(
		crypto_ablkcipher_reqtfm(req));

	dev_dbg(&sctx->sep_used->pdev->dev, "sep des cbc encrypt\n");
	sctx->current_request = DES_CBC;
	sctx->current_hash_req = NULL;
	sctx->current_cypher_req = req;
	bctx->des_encmode = SEP_DES_ENCRYPT;
	bctx->des_opmode = SEP_DES_CBC;
	bctx->init_opcode = SEP_DES_INIT_OPCODE;
	bctx->block_opcode = SEP_DES_BLOCK_OPCODE;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_des_cbc_encrypt cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_des_cbc_encrypt cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;
}

static int sep_des_cbc_decrypt(struct ablkcipher_request *req)
{
	int error;
	struct sep_block_ctx *bctx = ablkcipher_request_ctx(req);
	struct sep_system_ctx *sctx = crypto_ablkcipher_ctx(
		crypto_ablkcipher_reqtfm(req));

	dev_dbg(&sctx->sep_used->pdev->dev, "sep des cbc decrypt\n");
	sctx->current_request = DES_CBC;
	sctx->current_hash_req = NULL;
	sctx->current_cypher_req = req;
	bctx->des_encmode = SEP_DES_DECRYPT;
	bctx->des_opmode = SEP_DES_CBC;
	bctx->init_opcode = SEP_DES_INIT_OPCODE;
	bctx->block_opcode = SEP_DES_BLOCK_OPCODE;

	spin_lock_irq(&queue_lock);
	error = crypto_enqueue_request(&sep_queue, &req->base);
	spin_unlock_irq(&queue_lock);

	if ((error != 0) && (error != -EINPROGRESS)) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_des_cbc_decrypt cant enqueue\n");
		sep_crypto_release(sctx, error);
		return error;
	}

	error = sep_submit_work(sctx->sep_used->workqueue, sep_dequeuer,
		(void *)&sep_queue);
	if (error) {
		dev_warn(&sctx->sep_used->pdev->dev,
			"sep_des_cbc_decrypt cannot submit queue\n");
		sep_crypto_release(sctx, -EINVAL);
		return -EINVAL;
	}
	return -EINPROGRESS;
}

static struct ahash_alg hash_algs[] = {
{
	.init		= sep_sha1_init,
	.update		= sep_sha1_update,
	.final		= sep_sha1_final,
	.digest		= sep_sha1_digest,
	.halg		= {
		.digestsize	= SHA1_DIGEST_SIZE,
		.base	= {
		.cra_name		= "sha1",
		.cra_driver_name	= "sha1-sep",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
						CRYPTO_ALG_ASYNC,
		.cra_blocksize		= SHA1_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct sep_system_ctx),
		.cra_alignmask		= 0,
		.cra_module		= THIS_MODULE,
		.cra_init		= sep_hash_cra_init,
		.cra_exit		= sep_hash_cra_exit,
		}
	}
},
{
	.init		= sep_md5_init,
	.update		= sep_md5_update,
	.final		= sep_md5_final,
	.digest		= sep_md5_digest,
	.halg		= {
		.digestsize	= MD5_DIGEST_SIZE,
		.base	= {
		.cra_name		= "md5",
		.cra_driver_name	= "md5-sep",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
						CRYPTO_ALG_ASYNC,
		.cra_blocksize		= SHA1_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct sep_system_ctx),
		.cra_alignmask		= 0,
		.cra_module		= THIS_MODULE,
		.cra_init		= sep_hash_cra_init,
		.cra_exit		= sep_hash_cra_exit,
		}
	}
},
{
	.init		= sep_sha224_init,
	.update		= sep_sha224_update,
	.final		= sep_sha224_final,
	.digest		= sep_sha224_digest,
	.halg		= {
		.digestsize	= SHA224_DIGEST_SIZE,
		.base	= {
		.cra_name		= "sha224",
		.cra_driver_name	= "sha224-sep",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
						CRYPTO_ALG_ASYNC,
		.cra_blocksize		= SHA224_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct sep_system_ctx),
		.cra_alignmask		= 0,
		.cra_module		= THIS_MODULE,
		.cra_init		= sep_hash_cra_init,
		.cra_exit		= sep_hash_cra_exit,
		}
	}
},
{
	.init		= sep_sha256_init,
	.update		= sep_sha256_update,
	.final		= sep_sha256_final,
	.digest		= sep_sha256_digest,
	.halg		= {
		.digestsize	= SHA256_DIGEST_SIZE,
		.base	= {
		.cra_name		= "sha256",
		.cra_driver_name	= "sha256-sep",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
						CRYPTO_ALG_ASYNC,
		.cra_blocksize		= SHA256_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct sep_system_ctx),
		.cra_alignmask		= 0,
		.cra_module		= THIS_MODULE,
		.cra_init		= sep_hash_cra_init,
		.cra_exit		= sep_hash_cra_exit,
		}
	}
}
};

static struct crypto_alg crypto_algs[] = {
{
	.cra_name		= "ecb(aes)",
	.cra_driver_name	= "ecb-aes-sep",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct sep_system_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= sep_crypto_init,
	.cra_exit		= sep_crypto_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.setkey		= sep_aes_setkey,
		.encrypt	= sep_aes_ecb_encrypt,
		.decrypt	= sep_aes_ecb_decrypt,
	}
},
{
	.cra_name		= "cbc(aes)",
	.cra_driver_name	= "cbc-aes-sep",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct sep_system_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= sep_crypto_init,
	.cra_exit		= sep_crypto_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.setkey		= sep_aes_setkey,
		.encrypt	= sep_aes_cbc_encrypt,
		.decrypt	= sep_aes_cbc_decrypt,
	}
},
{
	.cra_name		= "ebc(des)",
	.cra_driver_name	= "ebc-des-sep",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct sep_system_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= sep_crypto_init,
	.cra_exit		= sep_crypto_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= DES_KEY_SIZE,
		.max_keysize	= DES_KEY_SIZE,
		.setkey		= sep_des_setkey,
		.encrypt	= sep_des_ebc_encrypt,
		.decrypt	= sep_des_ebc_decrypt,
	}
},
{
	.cra_name		= "cbc(des)",
	.cra_driver_name	= "cbc-des-sep",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct sep_system_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= sep_crypto_init,
	.cra_exit		= sep_crypto_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= DES_KEY_SIZE,
		.max_keysize	= DES_KEY_SIZE,
		.setkey		= sep_des_setkey,
		.encrypt	= sep_des_cbc_encrypt,
		.decrypt	= sep_des_cbc_decrypt,
	}
},
{
	.cra_name		= "ebc(des3-ede)",
	.cra_driver_name	= "ebc-des3-ede-sep",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct sep_system_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= sep_crypto_init,
	.cra_exit		= sep_crypto_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= DES3_EDE_KEY_SIZE,
		.max_keysize	= DES3_EDE_KEY_SIZE,
		.setkey		= sep_des_setkey,
		.encrypt	= sep_des_ebc_encrypt,
		.decrypt	= sep_des_ebc_decrypt,
	}
},
{
	.cra_name		= "cbc(des3-ede)",
	.cra_driver_name	= "cbc-des3--ede-sep",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct sep_system_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= sep_crypto_init,
	.cra_exit		= sep_crypto_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= DES3_EDE_KEY_SIZE,
		.max_keysize	= DES3_EDE_KEY_SIZE,
		.setkey		= sep_des_setkey,
		.encrypt	= sep_des_cbc_encrypt,
		.decrypt	= sep_des_cbc_decrypt,
	}
}
};

int sep_crypto_setup(void)
{
	int err, i, j, k;
	tasklet_init(&sep_dev->finish_tasklet, sep_finish,
		(unsigned long)sep_dev);

	crypto_init_queue(&sep_queue, SEP_QUEUE_LENGTH);

	sep_dev->workqueue = create_workqueue("sep_crypto_workqueue");
	if (!sep_dev->workqueue) {
		dev_warn(&sep_dev->pdev->dev, "cant create workqueue\n");
		return -ENOMEM;
	}

	i = 0;
	j = 0;

	spin_lock_init(&sep_dev->busy_lock);
	spin_lock_init(&queue_lock);

	err = 0;

	for (i = 0; i < ARRAY_SIZE(hash_algs); i++) {
		err = crypto_register_ahash(&hash_algs[i]);
		if (err)
			goto err_algs;
	}

	err = 0;
	for (j = 0; j < ARRAY_SIZE(crypto_algs); j++) {
		err = crypto_register_alg(&crypto_algs[j]);
		if (err)
			goto err_crypto_algs;
	}

	return err;

err_algs:
	for (k = 0; k < i; k++)
		crypto_unregister_ahash(&hash_algs[k]);
	return err;

err_crypto_algs:
	for (k = 0; k < j; k++)
		crypto_unregister_alg(&crypto_algs[k]);
	goto err_algs;
}

void sep_crypto_takedown(void)
{

	int i;

	for (i = 0; i < ARRAY_SIZE(hash_algs); i++)
		crypto_unregister_ahash(&hash_algs[i]);
	for (i = 0; i < ARRAY_SIZE(crypto_algs); i++)
		crypto_unregister_alg(&crypto_algs[i]);

	tasklet_kill(&sep_dev->finish_tasklet);
}
