/*
 *      Copyright (C) 1994-1997 Claus-Justus Heine

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License as
 published by the Free Software Foundation; either version 2, or (at
 your option) any later version.
 
 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139,
 USA.
 
 *
 *     This file implements a "generic" interface between the *
 *     zftape-driver and a compression-algorithm. The *
 *     compression-algorithm currently used is a LZ77. I use the *
 *     implementation lzrw3 by Ross N. Williams (Renaissance *
 *     Software). The compression program itself is in the file
 *     lzrw3.c * and lzrw3.h.  To adopt another compression algorithm
 *     the functions * zft_compress() and zft_uncompress() must be
 *     changed * appropriately. See below.
 */

#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/module.h>

#include <linux/zftape.h>

#include <asm/uaccess.h>

#include "../zftape/zftape-init.h"
#include "../zftape/zftape-eof.h"
#include "../zftape/zftape-ctl.h"
#include "../zftape/zftape-write.h"
#include "../zftape/zftape-read.h"
#include "../zftape/zftape-rw.h"
#include "../compressor/zftape-compress.h"
#include "../zftape/zftape-vtbl.h"
#include "../compressor/lzrw3.h"

/*
 *   global variables
 */

/* I handle the allocation of this buffer as a special case, because
 * it's size varies depending on the tape length inserted.
 */

/* local variables 
 */
static void *zftc_wrk_mem = NULL;
static __u8 *zftc_buf     = NULL;
static void *zftc_scratch_buf  = NULL;

/* compression statistics 
 */
static unsigned int zftc_wr_uncompressed = 0;
static unsigned int zftc_wr_compressed   = 0;
static unsigned int zftc_rd_uncompressed = 0;
static unsigned int zftc_rd_compressed   = 0;

/* forward */
static int  zftc_write(int *write_cnt,
		       __u8 *dst_buf, const int seg_sz,
		       const __u8 __user *src_buf, const int req_len,
		       const zft_position *pos, const zft_volinfo *volume);
static int  zftc_read(int *read_cnt,
		      __u8  __user *dst_buf, const int to_do,
		      const __u8 *src_buf, const int seg_sz,
		      const zft_position *pos, const zft_volinfo *volume);
static int  zftc_seek(unsigned int new_block_pos, 
		      zft_position *pos, const zft_volinfo *volume,
		      __u8 *buffer);
static void zftc_lock   (void);
static void zftc_reset  (void);
static void zftc_cleanup(void);
static void zftc_stats      (void);

/* compressed segment. This conforms to QIC-80-MC, Revision K.
 * 
 * Rev. K applies to tapes with `fixed length format' which is
 * indicated by format code 2,3 and 5. See below for format code 4 and 6
 *
 * 2 bytes: offset of compression segment structure
 *          29k > offset >= 29k-18: data from previous segment ens in this
 *                                  segment and no compressed block starts
 *                                  in this segment
 *                     offset == 0: data from previous segment occupies entire
 *                                  segment and continues in next segment
 * n bytes: remainder from previous segment
 * 
 * Rev. K:  
 * 4 bytes: 4 bytes: files set byte offset
 * Post Rev. K and QIC-3020/3020:
 * 8 bytes: 8 bytes: files set byte offset
 * 2 bytes: byte count N (amount of data following)
 *          bit 15 is set if data is compressed, bit 15 is not
 *          set if data is uncompressed
 * N bytes: data (as much as specified in the byte count)
 * 2 bytes: byte count N_1 of next cluster
 * N_1 bytes: data of next cluset
 * 2 bytes: byte count N_2 of next cluster
 * N_2 bytes: ...  
 *
 * Note that the `N' byte count accounts only for the bytes that in the
 * current segment if the cluster spans to the next segment.
 */

typedef struct
{
	int cmpr_pos;             /* actual position in compression buffer */
	int cmpr_sz;              /* what is left in the compression buffer
				   * when copying the compressed data to the
				   * deblock buffer
				   */
	unsigned int first_block; /* location of header information in
				   * this segment
				   */
	unsigned int count;       /* amount of data of current block
				   * contained in current segment 
				   */
	unsigned int offset;      /* offset in current segment */
	unsigned int spans:1;     /* might continue in next segment */
	unsigned int uncmpr;      /* 0x8000 if this block contains
				   * uncompressed data 
				   */
	__s64 foffs;              /* file set byte offset, same as in 
				   * compression map segment
				   */
} cmpr_info;

static cmpr_info cseg; /* static data. Must be kept uptodate and shared by 
			* read, write and seek functions
			*/

#define DUMP_CMPR_INFO(level, msg, info)				\
	TRACE(level, msg "\n"						\
	      KERN_INFO "cmpr_pos   : %d\n"				\
	      KERN_INFO "cmpr_sz    : %d\n"				\
	      KERN_INFO "first_block: %d\n"				\
	      KERN_INFO "count      : %d\n"				\
	      KERN_INFO "offset     : %d\n"				\
	      KERN_INFO "spans      : %d\n"				\
	      KERN_INFO "uncmpr     : 0x%04x\n"				\
	      KERN_INFO "foffs      : " LL_X,				\
	      (info)->cmpr_pos, (info)->cmpr_sz, (info)->first_block,	\
	      (info)->count, (info)->offset, (info)->spans == 1,	\
	      (info)->uncmpr, LL((info)->foffs))

/*   dispatch compression segment info, return error code
 *  
 *   afterwards, cseg->offset points to start of data of the NEXT
 *   compressed block, and cseg->count contains the amount of data
 *   left in the actual compressed block. cseg->spans is set to 1 if
 *   the block is continued in the following segment. Otherwise it is
 *   set to 0. 
 */
static int get_cseg (cmpr_info *cinfo, const __u8 *buff, 
		     const unsigned int seg_sz,
		     const zft_volinfo *volume)
{
	TRACE_FUN(ft_t_flow);

 	cinfo->first_block = GET2(buff, 0);
	if (cinfo->first_block == 0) { /* data spans to next segment */
		cinfo->count  = seg_sz - sizeof(__u16);
		cinfo->offset = seg_sz;
		cinfo->spans = 1;
	} else { /* cluster definetely ends in this segment */
		if (cinfo->first_block > seg_sz) {
			/* data corrupted */
			TRACE_ABORT(-EIO, ft_t_err, "corrupted data:\n"
				    KERN_INFO "segment size: %d\n"
				    KERN_INFO "first block : %d",
				    seg_sz, cinfo->first_block);
		}
	        cinfo->count  = cinfo->first_block - sizeof(__u16);
		cinfo->offset = cinfo->first_block;
		cinfo->spans = 0;
	}
	/* now get the offset the first block should have in the
	 * uncompressed data stream.
	 *
	 * For this magic `18' refer to CRF-3 standard or QIC-80MC,
	 * Rev. K.  
	 */
	if ((seg_sz - cinfo->offset) > 18) {
		if (volume->qic113) { /* > revision K */
			TRACE(ft_t_data_flow, "New QIC-113 compliance");
			cinfo->foffs = GET8(buff, cinfo->offset);
			cinfo->offset += sizeof(__s64); 
		} else {
			TRACE(/* ft_t_data_flow */ ft_t_noise, "pre QIC-113 version");
			cinfo->foffs   = (__s64)GET4(buff, cinfo->offset);
			cinfo->offset += sizeof(__u32); 
		}
	}
	if (cinfo->foffs > volume->size) {
		TRACE_ABORT(-EIO, ft_t_err, "Inconsistency:\n"
			    KERN_INFO "offset in current volume: %d\n"
			    KERN_INFO "size of current volume  : %d",
			    (int)(cinfo->foffs>>10), (int)(volume->size>>10));
	}
	if (cinfo->cmpr_pos + cinfo->count > volume->blk_sz) {
		TRACE_ABORT(-EIO, ft_t_err, "Inconsistency:\n"
			    KERN_INFO "block size : %d\n"
			    KERN_INFO "data record: %d",
			    volume->blk_sz, cinfo->cmpr_pos + cinfo->count);
	}
	DUMP_CMPR_INFO(ft_t_noise /* ft_t_any */, "", cinfo);
	TRACE_EXIT 0;
}

/*  This one is called, when a new cluster starts in same segment.
 *  
 *  Note: if this is the first cluster in the current segment, we must
 *  not check whether there are more than 18 bytes available because
 *  this have already been done in get_cseg() and there may be less
 *  than 18 bytes available due to header information.
 * 
 */
static void get_next_cluster(cmpr_info *cluster, const __u8 *buff, 
			     const int seg_sz, const int finish)
{
	TRACE_FUN(ft_t_flow);

	if (seg_sz - cluster->offset > 18 || cluster->foffs != 0) {
		cluster->count   = GET2(buff, cluster->offset);
		cluster->uncmpr  = cluster->count & 0x8000;
		cluster->count  -= cluster->uncmpr;
		cluster->offset += sizeof(__u16);
		cluster->foffs   = 0;
		if ((cluster->offset + cluster->count) < seg_sz) {
			cluster->spans = 0;
		} else if (cluster->offset + cluster->count == seg_sz) {
			cluster->spans = !finish;
		} else {
			/* either an error or a volume written by an 
			 * old version. If this is a data error, then we'll
			 * catch it later.
			 */
			TRACE(ft_t_data_flow, "Either error or old volume");
			cluster->spans = 1;
			cluster->count = seg_sz - cluster->offset;
		}
	} else {
		cluster->count = 0;
		cluster->spans = 0;
		cluster->foffs = 0;
	}
	DUMP_CMPR_INFO(ft_t_noise /* ft_t_any */ , "", cluster);
	TRACE_EXIT;
}

static void zftc_lock(void)
{
}

/*  this function is needed for zftape_reset_position in zftape-io.c 
 */
static void zftc_reset(void)
{
	TRACE_FUN(ft_t_flow);

	memset((void *)&cseg, '\0', sizeof(cseg));
	zftc_stats();
	TRACE_EXIT;
}

static int cmpr_mem_initialized = 0;
static unsigned int alloc_blksz = 0;

static int zft_allocate_cmpr_mem(unsigned int blksz)
{
	TRACE_FUN(ft_t_flow);

	if (cmpr_mem_initialized && blksz == alloc_blksz) {
		TRACE_EXIT 0;
	}
	TRACE_CATCH(zft_vmalloc_once(&zftc_wrk_mem, CMPR_WRK_MEM_SIZE),
		    zftc_cleanup());
	TRACE_CATCH(zft_vmalloc_always(&zftc_buf, blksz + CMPR_OVERRUN),
		    zftc_cleanup());
	alloc_blksz = blksz;
	TRACE_CATCH(zft_vmalloc_always(&zftc_scratch_buf, blksz+CMPR_OVERRUN),
		    zftc_cleanup());
	cmpr_mem_initialized = 1;
	TRACE_EXIT 0;
}

static void zftc_cleanup(void)
{
	TRACE_FUN(ft_t_flow);

	zft_vfree(&zftc_wrk_mem, CMPR_WRK_MEM_SIZE);
	zft_vfree(&zftc_buf, alloc_blksz + CMPR_OVERRUN);
	zft_vfree(&zftc_scratch_buf, alloc_blksz + CMPR_OVERRUN);
	cmpr_mem_initialized = alloc_blksz = 0;
	TRACE_EXIT;
}

/*****************************************************************************
 *                                                                           *
 *  The following two functions "ftape_compress()" and                       *
 *  "ftape_uncompress()" are the interface to the actual compression         *
 *  algorithm (i.e. they are calling the "compress()" function from          *
 *  the lzrw3 package for now). These routines could quite easily be         *
 *  changed to adopt another compression algorithm instead of lzrw3,         *
 *  which currently is used.                                                 *
 *                                                                           *
 *****************************************************************************/

/* called by zft_compress_write() to perform the compression. Must
 * return the size of the compressed data.
 *
 * NOTE: The size of the compressed data should not exceed the size of
 *       the uncompressed data. Most compression algorithms have means
 *       to store data unchanged if the "compressed" data amount would
 *       exceed the original one. Mostly this is done by storing some
 *       flag-bytes in front of the compressed data to indicate if it
 *       is compressed or not. Thus the worst compression result
 *       length is the original length plus those flag-bytes.
 *
 *       We don't want that, as the QIC-80 standard provides a means
 *       of marking uncompressed blocks by simply setting bit 15 of
 *       the compressed block's length. Thus a compessed block can
 *       have at most a length of 2^15-1 bytes. The QIC-80 standard
 *       restricts the block-length even further, allowing only 29k -
 *       6 bytes.
 *
 *       Currently, the maximum blocksize used by zftape is 28k.
 *
 *       In short: don't exceed the length of the input-package, set
 *       bit 15 of the compressed size to 1 if you have copied data
 *       instead of compressing it.
 */
static int zft_compress(__u8 *in_buffer, unsigned int in_sz, __u8 *out_buffer)
{ 
	__s32 compressed_sz;
	TRACE_FUN(ft_t_flow);
	

	lzrw3_compress(COMPRESS_ACTION_COMPRESS, zftc_wrk_mem,
		       in_buffer, in_sz, out_buffer, &compressed_sz);
	if (TRACE_LEVEL >= ft_t_info) {
		/*  the compiler will optimize this away when
		 *  compiled with NO_TRACE_AT_ALL option
		 */
		TRACE(ft_t_data_flow, "\n"
		      KERN_INFO "before compression: %d bytes\n"
		      KERN_INFO "after compresison : %d bytes", 
		      in_sz, 
		      (int)(compressed_sz < 0 
		      ? -compressed_sz : compressed_sz));
		/*  for statistical purposes
		 */
		zftc_wr_compressed   += (compressed_sz < 0 
					   ? -compressed_sz : compressed_sz);
		zftc_wr_uncompressed += in_sz;
	}
	TRACE_EXIT (int)compressed_sz;
}

/* called by zft_compress_read() to decompress the data. Must
 * return the size of the decompressed data for sanity checks
 * (compared with zft_blk_sz)
 *
 * NOTE: Read the note for zft_compress() above!  If bit 15 of the
 *       parameter in_sz is set, then the data in in_buffer isn't
 *       compressed, which must be handled by the un-compression
 *       algorithm. (I changed lzrw3 to handle this.)
 *
 *  The parameter max_out_sz is needed to prevent buffer overruns when 
 *  uncompressing corrupt data.
 */
static unsigned int zft_uncompress(__u8 *in_buffer, 
				   int in_sz, 
				   __u8 *out_buffer,
				   unsigned int max_out_sz)
{ 
	TRACE_FUN(ft_t_flow);
	
	lzrw3_compress(COMPRESS_ACTION_DECOMPRESS, zftc_wrk_mem,
		       in_buffer, (__s32)in_sz,
		       out_buffer, (__u32 *)&max_out_sz);
	
	if (TRACE_LEVEL >= ft_t_info) {
		TRACE(ft_t_data_flow, "\n"
		      KERN_INFO "before decompression: %d bytes\n"
		      KERN_INFO "after decompression : %d bytes", 
		      in_sz < 0 ? -in_sz : in_sz,(int)max_out_sz);
		/*  for statistical purposes
		 */
		zftc_rd_compressed   += in_sz < 0 ? -in_sz : in_sz;
		zftc_rd_uncompressed += max_out_sz;
	}
	TRACE_EXIT (unsigned int)max_out_sz;
}

/* print some statistics about the efficiency of the compression to
 * the kernel log 
 */
static void zftc_stats(void)
{
	TRACE_FUN(ft_t_flow);

	if (TRACE_LEVEL < ft_t_info) {
		TRACE_EXIT;
	}
	if (zftc_wr_uncompressed != 0) {
		if (zftc_wr_compressed > (1<<14)) {
			TRACE(ft_t_info, "compression statistics (writing):\n"
			      KERN_INFO " compr./uncmpr.   : %3d %%",
			      (((zftc_wr_compressed>>10) * 100)
			       / (zftc_wr_uncompressed>>10)));
		} else {
			TRACE(ft_t_info, "compression statistics (writing):\n"
			      KERN_INFO " compr./uncmpr.   : %3d %%",
			      ((zftc_wr_compressed * 100)
			       / zftc_wr_uncompressed));
		}
	}
	if (zftc_rd_uncompressed != 0) {
		if (zftc_rd_compressed > (1<<14)) {
			TRACE(ft_t_info, "compression statistics (reading):\n"
			      KERN_INFO " compr./uncmpr.   : %3d %%",
			      (((zftc_rd_compressed>>10) * 100)
			       / (zftc_rd_uncompressed>>10)));
		} else {
			TRACE(ft_t_info, "compression statistics (reading):\n"
			      KERN_INFO " compr./uncmpr.   : %3d %%",
			      ((zftc_rd_compressed * 100)
			       / zftc_rd_uncompressed));
		}
	}
	/* only print it once: */
	zftc_wr_uncompressed = 
		zftc_wr_compressed  =
		zftc_rd_uncompressed =
		zftc_rd_compressed   = 0;
	TRACE_EXIT;
}

/* start new compressed block 
 */
static int start_new_cseg(cmpr_info *cluster, 
			  char *dst_buf, 
			  const zft_position *pos,
			  const unsigned int blk_sz,
			  const char *src_buf,
			  const int this_segs_sz,
			  const int qic113)
{
	int size_left;
	int cp_cnt;
	int buf_pos;
	TRACE_FUN(ft_t_flow);

	size_left = this_segs_sz - sizeof(__u16) - cluster->cmpr_sz;
	TRACE(ft_t_data_flow,"\n" 
	      KERN_INFO "segment size   : %d\n"
	      KERN_INFO "compressed_sz: %d\n"
	      KERN_INFO "size_left      : %d",
	      this_segs_sz, cluster->cmpr_sz, size_left);
	if (size_left > 18) { /* start a new cluseter */
		cp_cnt = cluster->cmpr_sz;
		cluster->cmpr_sz = 0;
		buf_pos = cp_cnt + sizeof(__u16);
		PUT2(dst_buf, 0, buf_pos);

		if (qic113) {
			__s64 foffs = pos->volume_pos;
			if (cp_cnt) foffs += (__s64)blk_sz;

			TRACE(ft_t_data_flow, "new style QIC-113 header");
			PUT8(dst_buf, buf_pos, foffs);
			buf_pos += sizeof(__s64);
		} else {
			__u32 foffs = (__u32)pos->volume_pos;
			if (cp_cnt) foffs += (__u32)blk_sz;
			
			TRACE(ft_t_data_flow, "old style QIC-80MC header");
			PUT4(dst_buf, buf_pos, foffs);
			buf_pos += sizeof(__u32);
		}
	} else if (size_left >= 0) {
		cp_cnt = cluster->cmpr_sz;
		cluster->cmpr_sz = 0;
		buf_pos = cp_cnt + sizeof(__u16);
		PUT2(dst_buf, 0, buf_pos);  
		/* zero unused part of segment. */
		memset(dst_buf + buf_pos, '\0', size_left);
		buf_pos = this_segs_sz;
	} else { /* need entire segment and more space */
		PUT2(dst_buf, 0, 0); 
		cp_cnt = this_segs_sz - sizeof(__u16);
		cluster->cmpr_sz  -= cp_cnt;
		buf_pos = this_segs_sz;
	}
	memcpy(dst_buf + sizeof(__u16), src_buf + cluster->cmpr_pos, cp_cnt);
	cluster->cmpr_pos += cp_cnt;
	TRACE_EXIT buf_pos;
}

/* return-value: the number of bytes removed from the user-buffer
 *               `src_buf' or error code
 *
 *  int *write_cnt           : how much actually has been moved to the
 *                             dst_buf. Need not be initialized when
 *                             function returns with an error code
 *                             (negativ return value) 
 *  __u8 *dst_buf            : kernel space buffer where the has to be
 *                             copied to. The contents of this buffers
 *                             goes to a specific segment.
 *  const int seg_sz         : the size of the segment dst_buf will be
 *                             copied to.
 *  const zft_position *pos  : struct containing the coordinates in
 *                             the current volume (byte position,
 *                             segment id of current segment etc)
 *  const zft_volinfo *volume: information about the current volume,
 *                             size etc.
 *  const __u8 *src_buf      : user space buffer that contains the
 *                             data the user wants to be written to
 *                             tape.
 *  const int req_len        : the amount of data the user wants to be
 *                             written to tape.
 */
static int zftc_write(int *write_cnt,
		      __u8 *dst_buf, const int seg_sz,
		      const __u8 __user *src_buf, const int req_len,
		      const zft_position *pos, const zft_volinfo *volume)
{
	int req_len_left = req_len;
	int result;
	int len_left;
	int buf_pos_write = pos->seg_byte_pos;
	TRACE_FUN(ft_t_flow);
	
	/* Note: we do not unlock the module because
	 * there are some values cached in that `cseg' variable.  We
	 * don't don't want to use this information when being
	 * unloaded by kerneld even when the tape is full or when we
	 * cannot allocate enough memory.
	 */
	if (pos->tape_pos > (volume->size-volume->blk_sz-ZFT_CMPR_OVERHEAD)) {
		TRACE_EXIT -ENOSPC;
	}    
	if (zft_allocate_cmpr_mem(volume->blk_sz) < 0) {
		/* should we unlock the module? But it shouldn't 
		 * be locked anyway ...
		 */
		TRACE_EXIT -ENOMEM;
	}
	if (buf_pos_write == 0) { /* fill a new segment */
		*write_cnt = buf_pos_write = start_new_cseg(&cseg,
							    dst_buf,
							    pos,
							    volume->blk_sz,
							    zftc_buf, 
							    seg_sz,
							    volume->qic113);
		if (cseg.cmpr_sz == 0 && cseg.cmpr_pos != 0) {
			req_len_left -= result = volume->blk_sz;
			cseg.cmpr_pos  = 0;
		} else {
			result = 0;
		}
	} else {
		*write_cnt = result = 0;
	}
	
	len_left = seg_sz - buf_pos_write;
	while ((req_len_left > 0) && (len_left > 18)) {
		/* now we have some size left for a new compressed
		 * block.  We know, that the compression buffer is
		 * empty (else there wouldn't be any space left).  
		 */
		if (copy_from_user(zftc_scratch_buf, src_buf + result, 
				   volume->blk_sz) != 0) {
			TRACE_EXIT -EFAULT;
		}
		req_len_left -= volume->blk_sz;
		cseg.cmpr_sz = zft_compress(zftc_scratch_buf, volume->blk_sz, 
					    zftc_buf);
		if (cseg.cmpr_sz < 0) {
			cseg.uncmpr = 0x8000;
			cseg.cmpr_sz = -cseg.cmpr_sz;
		} else {
			cseg.uncmpr = 0;
		}
		/* increment "result" iff we copied the entire
		 * compressed block to the zft_deblock_buf 
		 */
		len_left -= sizeof(__u16);
		if (len_left >= cseg.cmpr_sz) {
			len_left -= cseg.count = cseg.cmpr_sz;
			cseg.cmpr_pos = cseg.cmpr_sz = 0;
			result += volume->blk_sz;
		} else {
			cseg.cmpr_sz       -= 
				cseg.cmpr_pos =
				cseg.count    = len_left;
			len_left = 0;
		}
		PUT2(dst_buf, buf_pos_write, cseg.uncmpr | cseg.count);
		buf_pos_write += sizeof(__u16);
		memcpy(dst_buf + buf_pos_write, zftc_buf, cseg.count);
		buf_pos_write += cseg.count;
		*write_cnt    += cseg.count + sizeof(__u16);
		FT_SIGNAL_EXIT(_DONT_BLOCK);
	}
	/* erase the remainder of the segment if less than 18 bytes
	 * left (18 bytes is due to the QIC-80 standard) 
	 */
	if (len_left <= 18) {
		memset(dst_buf + buf_pos_write, '\0', len_left);
		(*write_cnt) += len_left;
	}
	TRACE(ft_t_data_flow, "returning %d", result);
	TRACE_EXIT result;
}   

/* out:
 *
 * int *read_cnt: the number of bytes we removed from the zft_deblock_buf
 *                (result)
 * int *to_do   : the remaining size of the read-request.
 *
 * in:
 *
 * char *buff          : buff is the address of the upper part of the user
 *                       buffer, that hasn't been filled with data yet.

 * int buf_pos_read    : copy of from _ftape_read()
 * int buf_len_read    : copy of buf_len_rd from _ftape_read()
 * char *zft_deblock_buf: zft_deblock_buf
 * unsigned short blk_sz: the block size valid for this volume, may differ
 *                            from zft_blk_sz.
 * int finish: if != 0 means that this is the last segment belonging
 *  to this volume
 * returns the amount of data actually copied to the user-buffer
 *
 * to_do MUST NOT SHRINK except to indicate an EOF. In this case *to_do has to
 * be set to 0 
 */
static int zftc_read (int *read_cnt, 
		      __u8  __user *dst_buf, const int to_do, 
		      const __u8 *src_buf, const int seg_sz, 
		      const zft_position *pos, const zft_volinfo *volume)
{          
	int uncompressed_sz;         
	int result = 0;
	int remaining = to_do;
	TRACE_FUN(ft_t_flow);

	TRACE_CATCH(zft_allocate_cmpr_mem(volume->blk_sz),);
	if (pos->seg_byte_pos == 0) {
		/* new segment just read
		 */
		TRACE_CATCH(get_cseg(&cseg, src_buf, seg_sz, volume),
			    *read_cnt = 0);
		memcpy(zftc_buf + cseg.cmpr_pos, src_buf + sizeof(__u16), 
		       cseg.count);
		cseg.cmpr_pos += cseg.count;
		*read_cnt      = cseg.offset;
		DUMP_CMPR_INFO(ft_t_noise /* ft_t_any */, "", &cseg);
	} else {
		*read_cnt = 0;
	}
	/* loop and uncompress until user buffer full or
	 * deblock-buffer empty 
	 */
	TRACE(ft_t_data_flow, "compressed_sz: %d, compos : %d, *read_cnt: %d",
	      cseg.cmpr_sz, cseg.cmpr_pos, *read_cnt);
	while ((cseg.spans == 0) && (remaining > 0)) {
		if (cseg.cmpr_pos  != 0) { /* cmpr buf is not empty */
			uncompressed_sz = 
				zft_uncompress(zftc_buf,
					       cseg.uncmpr == 0x8000 ?
					       -cseg.cmpr_pos : cseg.cmpr_pos,
					       zftc_scratch_buf,
					       volume->blk_sz);
			if (uncompressed_sz != volume->blk_sz) {
				*read_cnt = 0;
				TRACE_ABORT(-EIO, ft_t_warn,
				      "Uncompressed blk (%d) != blk size (%d)",
				      uncompressed_sz, volume->blk_sz);
			}       
			if (copy_to_user(dst_buf + result, 
					 zftc_scratch_buf, 
					 uncompressed_sz) != 0 ) {
				TRACE_EXIT -EFAULT;
			}
			remaining      -= uncompressed_sz;
			result     += uncompressed_sz;
			cseg.cmpr_pos  = 0;
		}                                              
		if (remaining > 0) {
			get_next_cluster(&cseg, src_buf, seg_sz, 
					 volume->end_seg == pos->seg_pos);
			if (cseg.count != 0) {
				memcpy(zftc_buf, src_buf + cseg.offset,
				       cseg.count);
				cseg.cmpr_pos = cseg.count;
				cseg.offset  += cseg.count;
				*read_cnt += cseg.count + sizeof(__u16);
			} else {
				remaining = 0;
			}
		}
		TRACE(ft_t_data_flow, "\n" 
		      KERN_INFO "compressed_sz: %d\n"
		      KERN_INFO "compos       : %d\n"
		      KERN_INFO "*read_cnt    : %d",
		      cseg.cmpr_sz, cseg.cmpr_pos, *read_cnt);
	}
	if (seg_sz - cseg.offset <= 18) {
		*read_cnt += seg_sz - cseg.offset;
		TRACE(ft_t_data_flow, "expanding read cnt to: %d", *read_cnt);
	}
	TRACE(ft_t_data_flow, "\n"
	      KERN_INFO "segment size   : %d\n"
	      KERN_INFO "read count     : %d\n"
	      KERN_INFO "buf_pos_read   : %d\n"
	      KERN_INFO "remaining      : %d",
		seg_sz, *read_cnt, pos->seg_byte_pos, 
		seg_sz - *read_cnt - pos->seg_byte_pos);
	TRACE(ft_t_data_flow, "returning: %d", result);
	TRACE_EXIT result;
}                

/* seeks to the new data-position. Reads sometimes a segment.
 *  
 * start_seg and end_seg give the boundaries of the current volume
 * blk_sz is the blk_sz of the current volume as stored in the
 * volume label
 *
 * We don't allow blocksizes less than 1024 bytes, therefore we don't need
 * a 64 bit argument for new_block_pos.
 */

static int seek_in_segment(const unsigned int to_do, cmpr_info  *c_info,
			   const char *src_buf, const int seg_sz, 
			   const int seg_pos, const zft_volinfo *volume);
static int slow_seek_forward_until_error(const unsigned int distance,
					 cmpr_info *c_info, zft_position *pos, 
					 const zft_volinfo *volume, __u8 *buf);
static int search_valid_segment(unsigned int segment,
				const unsigned int end_seg,
				const unsigned int max_foffs,
				zft_position *pos, cmpr_info *c_info,
				const zft_volinfo *volume, __u8 *buf);
static int slow_seek_forward(unsigned int dest, cmpr_info *c_info,
			     zft_position *pos, const zft_volinfo *volume,
			     __u8 *buf);
static int compute_seg_pos(unsigned int dest, zft_position *pos,
			   const zft_volinfo *volume);

#define ZFT_SLOW_SEEK_THRESHOLD  10 /* segments */
#define ZFT_FAST_SEEK_MAX_TRIALS 10 /* times */
#define ZFT_FAST_SEEK_BACKUP     10 /* segments */

static int zftc_seek(unsigned int new_block_pos,
		     zft_position *pos, const zft_volinfo *volume, __u8 *buf)
{
	unsigned int dest;
	int limit;
	int distance;
	int result = 0;
	int seg_dist;
	int new_seg;
	int old_seg = 0;
	int fast_seek_trials = 0;
	TRACE_FUN(ft_t_flow);

	if (new_block_pos == 0) {
		pos->seg_pos      = volume->start_seg;
		pos->seg_byte_pos = 0;
		pos->volume_pos   = 0;
		zftc_reset();
		TRACE_EXIT 0;
	}
	dest = new_block_pos * (volume->blk_sz >> 10);
	distance = dest - (pos->volume_pos >> 10);
	while (distance != 0) {
		seg_dist = compute_seg_pos(dest, pos, volume);
		TRACE(ft_t_noise, "\n"
		      KERN_INFO "seg_dist: %d\n"
		      KERN_INFO "distance: %d\n"
		      KERN_INFO "dest    : %d\n"
		      KERN_INFO "vpos    : %d\n"
		      KERN_INFO "seg_pos : %d\n"
		      KERN_INFO "trials  : %d",
		      seg_dist, distance, dest,
		      (unsigned int)(pos->volume_pos>>10), pos->seg_pos,
		      fast_seek_trials);
		if (distance > 0) {
			if (seg_dist < 0) {
				TRACE(ft_t_bug, "BUG: distance %d > 0, "
				      "segment difference %d < 0",
				      distance, seg_dist);
				result = -EIO;
				break;
			}
			new_seg = pos->seg_pos + seg_dist;
			if (new_seg > volume->end_seg) {
				new_seg = volume->end_seg;
			}
			if (old_seg == new_seg || /* loop */
			    seg_dist <= ZFT_SLOW_SEEK_THRESHOLD ||
			    fast_seek_trials >= ZFT_FAST_SEEK_MAX_TRIALS) {
				TRACE(ft_t_noise, "starting slow seek:\n"
				   KERN_INFO "fast seek failed too often: %s\n"
				   KERN_INFO "near target position      : %s\n"
				   KERN_INFO "looping between two segs  : %s",
				      (fast_seek_trials >= 
				       ZFT_FAST_SEEK_MAX_TRIALS)
				      ? "yes" : "no",
				      (seg_dist <= ZFT_SLOW_SEEK_THRESHOLD) 
				      ? "yes" : "no",
				      (old_seg == new_seg)
				      ? "yes" : "no");
				result = slow_seek_forward(dest, &cseg, 
							   pos, volume, buf);
				break;
			}
			old_seg = new_seg;
			limit = volume->end_seg;
			fast_seek_trials ++;
			for (;;) {
				result = search_valid_segment(new_seg, limit,
							      volume->size,
							      pos, &cseg,
							      volume, buf);
				if (result == 0 || result == -EINTR) {
					break;
				}
				if (new_seg == volume->start_seg) {
					result = -EIO; /* set errror 
							* condition
							*/
					break;
				}
				limit    = new_seg;
				new_seg -= ZFT_FAST_SEEK_BACKUP;
				if (new_seg < volume->start_seg) {
					new_seg = volume->start_seg;
				}
			}
			if (result < 0) {
				TRACE(ft_t_warn,
				      "Couldn't find a readable segment");
				break;
			}
		} else /* if (distance < 0) */ {
			if (seg_dist > 0) {
				TRACE(ft_t_bug, "BUG: distance %d < 0, "
				      "segment difference %d >0",
				      distance, seg_dist);
				result = -EIO;
				break;
			}
			new_seg = pos->seg_pos + seg_dist;
			if (fast_seek_trials > 0 && seg_dist == 0) {
				/* this avoids sticking to the same
				 * segment all the time. On the other hand:
				 * if we got here for the first time, and the
				 * deblock_buffer still contains a valid
				 * segment, then there is no need to skip to 
				 * the previous segment if the desired position
				 * is inside this segment.
				 */
				new_seg --;
			}
			if (new_seg < volume->start_seg) {
				new_seg = volume->start_seg;
			}
			limit   = pos->seg_pos;
			fast_seek_trials ++;
			for (;;) {
				result = search_valid_segment(new_seg, limit,
							      pos->volume_pos,
							      pos, &cseg,
							      volume, buf);
				if (result == 0 || result == -EINTR) {
					break;
				}
				if (new_seg == volume->start_seg) {
					result = -EIO; /* set errror 
							* condition
							*/
					break;
				}
				limit    = new_seg;
				new_seg -= ZFT_FAST_SEEK_BACKUP;
				if (new_seg < volume->start_seg) {
					new_seg = volume->start_seg;
				}
			}
			if (result < 0) {
				TRACE(ft_t_warn,
				      "Couldn't find a readable segment");
				break;
			}
		}
		distance = dest - (pos->volume_pos >> 10);
	}
	TRACE_EXIT result;
}


/*  advance inside the given segment at most to_do bytes.
 *  of kilobytes moved
 */

static int seek_in_segment(const unsigned int to_do,
			   cmpr_info  *c_info,
			   const char *src_buf, 
			   const int seg_sz, 
			   const int seg_pos,
			   const zft_volinfo *volume)
{
	int result = 0;
	int blk_sz = volume->blk_sz >> 10;
	int remaining = to_do;
	TRACE_FUN(ft_t_flow);

	if (c_info->offset == 0) {
		/* new segment just read
		 */
		TRACE_CATCH(get_cseg(c_info, src_buf, seg_sz, volume),);
		c_info->cmpr_pos += c_info->count;
		DUMP_CMPR_INFO(ft_t_noise, "", c_info);
	}
	/* loop and uncompress until user buffer full or
	 * deblock-buffer empty 
	 */
	TRACE(ft_t_noise, "compressed_sz: %d, compos : %d",
	      c_info->cmpr_sz, c_info->cmpr_pos);
	while (c_info->spans == 0 && remaining > 0) {
		if (c_info->cmpr_pos  != 0) { /* cmpr buf is not empty */
			result       += blk_sz;
			remaining    -= blk_sz;
			c_info->cmpr_pos = 0;
		}
		if (remaining > 0) {
			get_next_cluster(c_info, src_buf, seg_sz, 
					 volume->end_seg == seg_pos);
			if (c_info->count != 0) {
				c_info->cmpr_pos = c_info->count;
				c_info->offset  += c_info->count;
			} else {
				break;
			}
		}
		/*  Allow escape from this loop on signal!
		 */
		FT_SIGNAL_EXIT(_DONT_BLOCK);
		DUMP_CMPR_INFO(ft_t_noise, "", c_info);
		TRACE(ft_t_noise, "to_do: %d", remaining);
	}
	if (seg_sz - c_info->offset <= 18) {
		c_info->offset = seg_sz;
	}
	TRACE(ft_t_noise, "\n"
	      KERN_INFO "segment size   : %d\n"
	      KERN_INFO "buf_pos_read   : %d\n"
	      KERN_INFO "remaining      : %d",
	      seg_sz, c_info->offset,
	      seg_sz - c_info->offset);
	TRACE_EXIT result;
}                

static int slow_seek_forward_until_error(const unsigned int distance,
					 cmpr_info *c_info,
					 zft_position *pos, 
					 const zft_volinfo *volume,
					 __u8 *buf)
{
	unsigned int remaining = distance;
	int seg_sz;
	int seg_pos;
	int result;
	TRACE_FUN(ft_t_flow);
	
	seg_pos = pos->seg_pos;
	do {
		TRACE_CATCH(seg_sz = zft_fetch_segment(seg_pos, buf, 
						       FT_RD_AHEAD),);
		/* now we have the contents of the actual segment in
		 * the deblock buffer
		 */
		TRACE_CATCH(result = seek_in_segment(remaining, c_info, buf,
						     seg_sz, seg_pos,volume),);
		remaining        -= result;
		pos->volume_pos  += result<<10;
		pos->seg_pos      = seg_pos;
		pos->seg_byte_pos = c_info->offset;
		seg_pos ++;
		if (seg_pos <= volume->end_seg && c_info->offset == seg_sz) {
			pos->seg_pos ++;
			pos->seg_byte_pos = 0;
			c_info->offset = 0;
		}
		/*  Allow escape from this loop on signal!
		 */
		FT_SIGNAL_EXIT(_DONT_BLOCK);
		TRACE(ft_t_noise, "\n"
		      KERN_INFO "remaining:  %d\n"
		      KERN_INFO "seg_pos:    %d\n"
		      KERN_INFO "end_seg:    %d\n"
		      KERN_INFO "result:     %d",
		      remaining, seg_pos, volume->end_seg, result);  
	} while (remaining > 0 && seg_pos <= volume->end_seg);
	TRACE_EXIT 0;
}

/* return segment id of next segment containing valid data, -EIO otherwise
 */
static int search_valid_segment(unsigned int segment,
				const unsigned int end_seg,
				const unsigned int max_foffs,
				zft_position *pos,
				cmpr_info *c_info,
				const zft_volinfo *volume,
				__u8 *buf)
{
	cmpr_info tmp_info;
	int seg_sz;
	TRACE_FUN(ft_t_flow);
	
	memset(&tmp_info, 0, sizeof(cmpr_info));
	while (segment <= end_seg) {
		FT_SIGNAL_EXIT(_DONT_BLOCK);
		TRACE(ft_t_noise,
		      "Searching readable segment between %d and %d",
		      segment, end_seg);
		seg_sz = zft_fetch_segment(segment, buf, FT_RD_AHEAD);
		if ((seg_sz > 0) &&
		    (get_cseg (&tmp_info, buf, seg_sz, volume) >= 0) &&
		    (tmp_info.foffs != 0 || segment == volume->start_seg)) {
			if ((tmp_info.foffs>>10) > max_foffs) {
				TRACE_ABORT(-EIO, ft_t_noise, "\n"
					    KERN_INFO "cseg.foff: %d\n"
					    KERN_INFO "dest     : %d",
					    (int)(tmp_info.foffs >> 10),
					    max_foffs);
			}
			DUMP_CMPR_INFO(ft_t_noise, "", &tmp_info);
			*c_info           = tmp_info;
			pos->seg_pos      = segment;
			pos->volume_pos   = c_info->foffs;
			pos->seg_byte_pos = c_info->offset;
			TRACE(ft_t_noise, "found segment at %d", segment);
			TRACE_EXIT 0;
		}
		segment++;
	}
	TRACE_EXIT -EIO;
}

static int slow_seek_forward(unsigned int dest,
			     cmpr_info *c_info,
			     zft_position *pos,
			     const zft_volinfo *volume,
			     __u8 *buf)
{
	unsigned int distance;
	int result = 0;
	TRACE_FUN(ft_t_flow);
		
	distance = dest - (pos->volume_pos >> 10);
	while ((distance > 0) &&
	       (result = slow_seek_forward_until_error(distance,
						       c_info,
						       pos,
						       volume,
						       buf)) < 0) {
		if (result == -EINTR) {
			break;
		}
		TRACE(ft_t_noise, "seg_pos: %d", pos->seg_pos);
		/* the failing segment is either pos->seg_pos or
		 * pos->seg_pos + 1. There is no need to further try
		 * that segment, because ftape_read_segment() already
		 * has tried very much to read it. So we start with
		 * following segment, which is pos->seg_pos + 1
		 */
		if(search_valid_segment(pos->seg_pos+1, volume->end_seg, dest,
					pos, c_info,
					volume, buf) < 0) {
			TRACE(ft_t_noise, "search_valid_segment() failed");
			result = -EIO;
			break;
		}
		distance = dest - (pos->volume_pos >> 10);
		result = 0;
		TRACE(ft_t_noise, "segment: %d", pos->seg_pos);
		/* found valid segment, retry the seek */
	}
	TRACE_EXIT result;
}

static int compute_seg_pos(const unsigned int dest,
			   zft_position *pos,
			   const zft_volinfo *volume)
{
	int segment;
	int distance = dest - (pos->volume_pos >> 10);
	unsigned int raw_size;
	unsigned int virt_size;
	unsigned int factor;
	TRACE_FUN(ft_t_flow);

	if (distance >= 0) {
		raw_size  = volume->end_seg - pos->seg_pos + 1;
		virt_size = ((unsigned int)(volume->size>>10) 
			     - (unsigned int)(pos->volume_pos>>10)
			     + FT_SECTORS_PER_SEGMENT - FT_ECC_SECTORS - 1);
		virt_size /= FT_SECTORS_PER_SEGMENT - FT_ECC_SECTORS;
		if (virt_size == 0 || raw_size == 0) {
			TRACE_EXIT 0;
		}
		if (raw_size >= (1<<25)) {
			factor = raw_size/(virt_size>>7);
		} else {
			factor = (raw_size<<7)/virt_size;
		}
		segment = distance/(FT_SECTORS_PER_SEGMENT-FT_ECC_SECTORS);
		segment = (segment * factor)>>7;
	} else {
		raw_size  = pos->seg_pos - volume->start_seg + 1;
		virt_size = ((unsigned int)(pos->volume_pos>>10)
			     + FT_SECTORS_PER_SEGMENT - FT_ECC_SECTORS - 1);
		virt_size /= FT_SECTORS_PER_SEGMENT - FT_ECC_SECTORS;
		if (virt_size == 0 || raw_size == 0) {
			TRACE_EXIT 0;
		}
		if (raw_size >= (1<<25)) {
			factor = raw_size/(virt_size>>7);
		} else {
			factor = (raw_size<<7)/virt_size;
		}
		segment = distance/(FT_SECTORS_PER_SEGMENT-FT_ECC_SECTORS);
	}
	TRACE(ft_t_noise, "factor: %d/%d", factor, 1<<7);
	TRACE_EXIT segment;
}

static struct zft_cmpr_ops cmpr_ops = {
	zftc_write,
	zftc_read,
	zftc_seek,
	zftc_lock,
	zftc_reset,
	zftc_cleanup
};

int zft_compressor_init(void)
{
	TRACE_FUN(ft_t_flow);
	
#ifdef MODULE
	printk(KERN_INFO "zftape compressor v1.00a 970514 for " FTAPE_VERSION "\n");
        if (TRACE_LEVEL >= ft_t_info) {
		printk(
KERN_INFO "(c) 1997 Claus-Justus Heine (claus@momo.math.rwth-aachen.de)\n"
KERN_INFO "Compressor for zftape (lzrw3 algorithm)\n");
        }
#else /* !MODULE */
	/* print a short no-nonsense boot message */
	printk(KERN_INFO "zftape compressor v1.00a 970514\n");
	printk(KERN_INFO "For use with " FTAPE_VERSION "\n");
#endif /* MODULE */
	TRACE(ft_t_info, "zft_compressor_init @ 0x%p", zft_compressor_init);
	TRACE(ft_t_info, "installing compressor for zftape ...");
	TRACE_CATCH(zft_cmpr_register(&cmpr_ops),);
	TRACE_EXIT 0;
}

#ifdef MODULE

MODULE_AUTHOR(
	"(c) 1996, 1997 Claus-Justus Heine (claus@momo.math.rwth-aachen.de");
MODULE_DESCRIPTION(
"Compression routines for zftape. Uses the lzrw3 algorithm by Ross Williams");
MODULE_LICENSE("GPL");

/* Called by modules package when installing the driver
 */
int init_module(void)
{
	return zft_compressor_init();
}

#endif /* MODULE */
