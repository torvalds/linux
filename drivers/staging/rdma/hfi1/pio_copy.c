/*
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "hfi.h"

/* additive distance between non-SOP and SOP space */
#define SOP_DISTANCE (TXE_PIO_SIZE / 2)
#define PIO_BLOCK_MASK (PIO_BLOCK_SIZE-1)
/* number of QUADWORDs in a block */
#define PIO_BLOCK_QWS (PIO_BLOCK_SIZE/sizeof(u64))

/**
 * pio_copy - copy data block to MMIO space
 * @pbuf: a number of blocks allocated within a PIO send context
 * @pbc: PBC to send
 * @from: source, must be 8 byte aligned
 * @count: number of DWORD (32-bit) quantities to copy from source
 *
 * Copy data from source to PIO Send Buffer memory, 8 bytes at a time.
 * Must always write full BLOCK_SIZE bytes blocks.  The first block must
 * be written to the corresponding SOP=1 address.
 *
 * Known:
 * o pbuf->start always starts on a block boundary
 * o pbuf can wrap only at a block boundary
 */
void pio_copy(struct hfi1_devdata *dd, struct pio_buf *pbuf, u64 pbc,
	      const void *from, size_t count)
{
	void __iomem *dest = pbuf->start + SOP_DISTANCE;
	void __iomem *send = dest + PIO_BLOCK_SIZE;
	void __iomem *dend;			/* 8-byte data end */

	/* write the PBC */
	writeq(pbc, dest);
	dest += sizeof(u64);

	/* calculate where the QWORD data ends - in SOP=1 space */
	dend = dest + ((count>>1) * sizeof(u64));

	if (dend < send) {
		/* all QWORD data is within the SOP block, does *not*
		   reach the end of the SOP block */

		while (dest < dend) {
			writeq(*(u64 *)from, dest);
			from += sizeof(u64);
			dest += sizeof(u64);
		}
		/*
		 * No boundary checks are needed here:
		 * 0. We're not on the SOP block boundary
		 * 1. The possible DWORD dangle will still be within
		 *    the SOP block
		 * 2. We cannot wrap except on a block boundary.
		 */
	} else {
		/* QWORD data extends _to_ or beyond the SOP block */

		/* write 8-byte SOP chunk data */
		while (dest < send) {
			writeq(*(u64 *)from, dest);
			from += sizeof(u64);
			dest += sizeof(u64);
		}
		/* drop out of the SOP range */
		dest -= SOP_DISTANCE;
		dend -= SOP_DISTANCE;

		/*
		 * If the wrap comes before or matches the data end,
		 * copy until until the wrap, then wrap.
		 *
		 * If the data ends at the end of the SOP above and
		 * the buffer wraps, then pbuf->end == dend == dest
		 * and nothing will get written, but we will wrap in
		 * case there is a dangling DWORD.
		 */
		if (pbuf->end <= dend) {
			while (dest < pbuf->end) {
				writeq(*(u64 *)from, dest);
				from += sizeof(u64);
				dest += sizeof(u64);
			}

			dest -= pbuf->size;
			dend -= pbuf->size;
		}

		/* write 8-byte non-SOP, non-wrap chunk data */
		while (dest < dend) {
			writeq(*(u64 *)from, dest);
			from += sizeof(u64);
			dest += sizeof(u64);
		}
	}
	/* at this point we have wrapped if we are going to wrap */

	/* write dangling u32, if any */
	if (count & 1) {
		union mix val;

		val.val64 = 0;
		val.val32[0] = *(u32 *)from;
		writeq(val.val64, dest);
		dest += sizeof(u64);
	}
	/* fill in rest of block, no need to check pbuf->end
	   as we only wrap on a block boundary */
	while (((unsigned long)dest & PIO_BLOCK_MASK) != 0) {
		writeq(0, dest);
		dest += sizeof(u64);
	}

	/* finished with this buffer */
	atomic_dec(&pbuf->sc->buffers_allocated);
}

/* USE_SHIFTS is faster in user-space tests on a Xeon X5570 @ 2.93GHz */
#define USE_SHIFTS 1
#ifdef USE_SHIFTS
/*
 * Handle carry bytes using shifts and masks.
 *
 * NOTE: the value the unused portion of carry is expected to always be zero.
 */

/*
 * "zero" shift - bit shift used to zero out upper bytes.  Input is
 * the count of LSB bytes to preserve.
 */
#define zshift(x) (8 * (8-(x)))

/*
 * "merge" shift - bit shift used to merge with carry bytes.  Input is
 * the LSB byte count to move beyond.
 */
#define mshift(x) (8 * (x))

/*
 * Read nbytes bytes from "from" and return them in the LSB bytes
 * of pbuf->carry.  Other bytes are zeroed.  Any previous value
 * pbuf->carry is lost.
 *
 * NOTES:
 * o do not read from from if nbytes is zero
 * o from may _not_ be u64 aligned
 * o nbytes must not span a QW boundary
 */
static inline void read_low_bytes(struct pio_buf *pbuf, const void *from,
							unsigned int nbytes)
{
	unsigned long off;

	if (nbytes == 0) {
		pbuf->carry.val64 = 0;
	} else {
		/* align our pointer */
		off = (unsigned long)from & 0x7;
		from = (void *)((unsigned long)from & ~0x7l);
		pbuf->carry.val64 = ((*(u64 *)from)
				<< zshift(nbytes + off))/* zero upper bytes */
				>> zshift(nbytes);	/* place at bottom */
	}
	pbuf->carry_bytes = nbytes;
}

/*
 * Read nbytes bytes from "from" and put them at the next significant bytes
 * of pbuf->carry.  Unused bytes are zeroed.  It is expected that the extra
 * read does not overfill carry.
 *
 * NOTES:
 * o from may _not_ be u64 aligned
 * o nbytes may span a QW boundary
 */
static inline void read_extra_bytes(struct pio_buf *pbuf,
					const void *from, unsigned int nbytes)
{
	unsigned long off = (unsigned long)from & 0x7;
	unsigned int room, xbytes;

	/* align our pointer */
	from = (void *)((unsigned long)from & ~0x7l);

	/* check count first - don't read anything if count is zero */
	while (nbytes) {
		/* find the number of bytes in this u64 */
		room = 8 - off;	/* this u64 has room for this many bytes */
		xbytes = nbytes > room ? room : nbytes;

		/*
		 * shift down to zero lower bytes, shift up to zero upper
		 * bytes, shift back down to move into place
		 */
		pbuf->carry.val64 |= (((*(u64 *)from)
					>> mshift(off))
					<< zshift(xbytes))
					>> zshift(xbytes+pbuf->carry_bytes);
		off = 0;
		pbuf->carry_bytes += xbytes;
		nbytes -= xbytes;
		from += sizeof(u64);
	}
}

/*
 * Zero extra bytes from the end of pbuf->carry.
 *
 * NOTES:
 * o zbytes <= old_bytes
 */
static inline void zero_extra_bytes(struct pio_buf *pbuf, unsigned int zbytes)
{
	unsigned int remaining;

	if (zbytes == 0)	/* nothing to do */
		return;

	remaining = pbuf->carry_bytes - zbytes;	/* remaining bytes */

	/* NOTE: zshift only guaranteed to work if remaining != 0 */
	if (remaining)
		pbuf->carry.val64 = (pbuf->carry.val64 << zshift(remaining))
					>> zshift(remaining);
	else
		pbuf->carry.val64 = 0;
	pbuf->carry_bytes = remaining;
}

/*
 * Write a quad word using parts of pbuf->carry and the next 8 bytes of src.
 * Put the unused part of the next 8 bytes of src into the LSB bytes of
 * pbuf->carry with the upper bytes zeroed..
 *
 * NOTES:
 * o result must keep unused bytes zeroed
 * o src must be u64 aligned
 */
static inline void merge_write8(
	struct pio_buf *pbuf,
	void __iomem *dest,
	const void *src)
{
	u64 new, temp;

	new = *(u64 *)src;
	temp = pbuf->carry.val64 | (new << mshift(pbuf->carry_bytes));
	writeq(temp, dest);
	pbuf->carry.val64 = new >> zshift(pbuf->carry_bytes);
}

/*
 * Write a quad word using all bytes of carry.
 */
static inline void carry8_write8(union mix carry, void __iomem *dest)
{
	writeq(carry.val64, dest);
}

/*
 * Write a quad word using all the valid bytes of carry.  If carry
 * has zero valid bytes, nothing is written.
 * Returns 0 on nothing written, non-zero on quad word written.
 */
static inline int carry_write8(struct pio_buf *pbuf, void __iomem *dest)
{
	if (pbuf->carry_bytes) {
		/* unused bytes are always kept zeroed, so just write */
		writeq(pbuf->carry.val64, dest);
		return 1;
	}

	return 0;
}

#else /* USE_SHIFTS */
/*
 * Handle carry bytes using byte copies.
 *
 * NOTE: the value the unused portion of carry is left uninitialized.
 */

/*
 * Jump copy - no-loop copy for < 8 bytes.
 */
static inline void jcopy(u8 *dest, const u8 *src, u32 n)
{
	switch (n) {
	case 7:
		*dest++ = *src++;
	case 6:
		*dest++ = *src++;
	case 5:
		*dest++ = *src++;
	case 4:
		*dest++ = *src++;
	case 3:
		*dest++ = *src++;
	case 2:
		*dest++ = *src++;
	case 1:
		*dest++ = *src++;
	}
}

/*
 * Read nbytes from "from" and and place them in the low bytes
 * of pbuf->carry.  Other bytes are left as-is.  Any previous
 * value in pbuf->carry is lost.
 *
 * NOTES:
 * o do not read from from if nbytes is zero
 * o from may _not_ be u64 aligned.
 */
static inline void read_low_bytes(struct pio_buf *pbuf, const void *from,
							unsigned int nbytes)
{
	jcopy(&pbuf->carry.val8[0], from, nbytes);
	pbuf->carry_bytes = nbytes;
}

/*
 * Read nbytes bytes from "from" and put them at the end of pbuf->carry.
 * It is expected that the extra read does not overfill carry.
 *
 * NOTES:
 * o from may _not_ be u64 aligned
 * o nbytes may span a QW boundary
 */
static inline void read_extra_bytes(struct pio_buf *pbuf,
					const void *from, unsigned int nbytes)
{
	jcopy(&pbuf->carry.val8[pbuf->carry_bytes], from, nbytes);
	pbuf->carry_bytes += nbytes;
}

/*
 * Zero extra bytes from the end of pbuf->carry.
 *
 * We do not care about the value of unused bytes in carry, so just
 * reduce the byte count.
 *
 * NOTES:
 * o zbytes <= old_bytes
 */
static inline void zero_extra_bytes(struct pio_buf *pbuf, unsigned int zbytes)
{
	pbuf->carry_bytes -= zbytes;
}

/*
 * Write a quad word using parts of pbuf->carry and the next 8 bytes of src.
 * Put the unused part of the next 8 bytes of src into the low bytes of
 * pbuf->carry.
 */
static inline void merge_write8(
	struct pio_buf *pbuf,
	void *dest,
	const void *src)
{
	u32 remainder = 8 - pbuf->carry_bytes;

	jcopy(&pbuf->carry.val8[pbuf->carry_bytes], src, remainder);
	writeq(pbuf->carry.val64, dest);
	jcopy(&pbuf->carry.val8[0], src+remainder, pbuf->carry_bytes);
}

/*
 * Write a quad word using all bytes of carry.
 */
static inline void carry8_write8(union mix carry, void *dest)
{
	writeq(carry.val64, dest);
}

/*
 * Write a quad word using all the valid bytes of carry.  If carry
 * has zero valid bytes, nothing is written.
 * Returns 0 on nothing written, non-zero on quad word written.
 */
static inline int carry_write8(struct pio_buf *pbuf, void *dest)
{
	if (pbuf->carry_bytes) {
		u64 zero = 0;

		jcopy(&pbuf->carry.val8[pbuf->carry_bytes], (u8 *)&zero,
						8 - pbuf->carry_bytes);
		writeq(pbuf->carry.val64, dest);
		return 1;
	}

	return 0;
}
#endif /* USE_SHIFTS */

/*
 * Segmented PIO Copy - start
 *
 * Start a PIO copy.
 *
 * @pbuf: destination buffer
 * @pbc: the PBC for the PIO buffer
 * @from: data source, QWORD aligned
 * @nbytes: bytes to copy
 */
void seg_pio_copy_start(struct pio_buf *pbuf, u64 pbc,
				const void *from, size_t nbytes)
{
	void __iomem *dest = pbuf->start + SOP_DISTANCE;
	void __iomem *send = dest + PIO_BLOCK_SIZE;
	void __iomem *dend;			/* 8-byte data end */

	writeq(pbc, dest);
	dest += sizeof(u64);

	/* calculate where the QWORD data ends - in SOP=1 space */
	dend = dest + ((nbytes>>3) * sizeof(u64));

	if (dend < send) {
		/* all QWORD data is within the SOP block, does *not*
		   reach the end of the SOP block */

		while (dest < dend) {
			writeq(*(u64 *)from, dest);
			from += sizeof(u64);
			dest += sizeof(u64);
		}
		/*
		 * No boundary checks are needed here:
		 * 0. We're not on the SOP block boundary
		 * 1. The possible DWORD dangle will still be within
		 *    the SOP block
		 * 2. We cannot wrap except on a block boundary.
		 */
	} else {
		/* QWORD data extends _to_ or beyond the SOP block */

		/* write 8-byte SOP chunk data */
		while (dest < send) {
			writeq(*(u64 *)from, dest);
			from += sizeof(u64);
			dest += sizeof(u64);
		}
		/* drop out of the SOP range */
		dest -= SOP_DISTANCE;
		dend -= SOP_DISTANCE;

		/*
		 * If the wrap comes before or matches the data end,
		 * copy until until the wrap, then wrap.
		 *
		 * If the data ends at the end of the SOP above and
		 * the buffer wraps, then pbuf->end == dend == dest
		 * and nothing will get written, but we will wrap in
		 * case there is a dangling DWORD.
		 */
		if (pbuf->end <= dend) {
			while (dest < pbuf->end) {
				writeq(*(u64 *)from, dest);
				from += sizeof(u64);
				dest += sizeof(u64);
			}

			dest -= pbuf->size;
			dend -= pbuf->size;
		}

		/* write 8-byte non-SOP, non-wrap chunk data */
		while (dest < dend) {
			writeq(*(u64 *)from, dest);
			from += sizeof(u64);
			dest += sizeof(u64);
		}
	}
	/* at this point we have wrapped if we are going to wrap */

	/* ...but it doesn't matter as we're done writing */

	/* save dangling bytes, if any */
	read_low_bytes(pbuf, from, nbytes & 0x7);

	pbuf->qw_written = 1 /*PBC*/ + (nbytes >> 3);
}

/*
 * Mid copy helper, "mixed case" - source is 64-bit aligned but carry
 * bytes are non-zero.
 *
 * Whole u64s must be written to the chip, so bytes must be manually merged.
 *
 * @pbuf: destination buffer
 * @from: data source, is QWORD aligned.
 * @nbytes: bytes to copy
 *
 * Must handle nbytes < 8.
 */
static void mid_copy_mix(struct pio_buf *pbuf, const void *from, size_t nbytes)
{
	void __iomem *dest = pbuf->start + (pbuf->qw_written * sizeof(u64));
	void __iomem *dend;			/* 8-byte data end */
	unsigned long qw_to_write = (pbuf->carry_bytes + nbytes) >> 3;
	unsigned long bytes_left = (pbuf->carry_bytes + nbytes) & 0x7;

	/* calculate 8-byte data end */
	dend = dest + (qw_to_write * sizeof(u64));

	if (pbuf->qw_written < PIO_BLOCK_QWS) {
		/*
		 * Still within SOP block.  We don't need to check for
		 * wrap because we are still in the first block and
		 * can only wrap on block boundaries.
		 */
		void __iomem *send;		/* SOP end */
		void __iomem *xend;

		/* calculate the end of data or end of block, whichever
		   comes first */
		send = pbuf->start + PIO_BLOCK_SIZE;
		xend = send < dend ? send : dend;

		/* shift up to SOP=1 space */
		dest += SOP_DISTANCE;
		xend += SOP_DISTANCE;

		/* write 8-byte chunk data */
		while (dest < xend) {
			merge_write8(pbuf, dest, from);
			from += sizeof(u64);
			dest += sizeof(u64);
		}

		/* shift down to SOP=0 space */
		dest -= SOP_DISTANCE;
	}
	/*
	 * At this point dest could be (either, both, or neither):
	 * - at dend
	 * - at the wrap
	 */

	/*
	 * If the wrap comes before or matches the data end,
	 * copy until until the wrap, then wrap.
	 *
	 * If dest is at the wrap, we will fall into the if,
	 * not do the loop, when wrap.
	 *
	 * If the data ends at the end of the SOP above and
	 * the buffer wraps, then pbuf->end == dend == dest
	 * and nothing will get written.
	 */
	if (pbuf->end <= dend) {
		while (dest < pbuf->end) {
			merge_write8(pbuf, dest, from);
			from += sizeof(u64);
			dest += sizeof(u64);
		}

		dest -= pbuf->size;
		dend -= pbuf->size;
	}

	/* write 8-byte non-SOP, non-wrap chunk data */
	while (dest < dend) {
		merge_write8(pbuf, dest, from);
		from += sizeof(u64);
		dest += sizeof(u64);
	}

	/* adjust carry */
	if (pbuf->carry_bytes < bytes_left) {
		/* need to read more */
		read_extra_bytes(pbuf, from, bytes_left - pbuf->carry_bytes);
	} else {
		/* remove invalid bytes */
		zero_extra_bytes(pbuf, pbuf->carry_bytes - bytes_left);
	}

	pbuf->qw_written += qw_to_write;
}

/*
 * Mid copy helper, "straight case" - source pointer is 64-bit aligned
 * with no carry bytes.
 *
 * @pbuf: destination buffer
 * @from: data source, is QWORD aligned
 * @nbytes: bytes to copy
 *
 * Must handle nbytes < 8.
 */
static void mid_copy_straight(struct pio_buf *pbuf,
						const void *from, size_t nbytes)
{
	void __iomem *dest = pbuf->start + (pbuf->qw_written * sizeof(u64));
	void __iomem *dend;			/* 8-byte data end */

	/* calculate 8-byte data end */
	dend = dest + ((nbytes>>3) * sizeof(u64));

	if (pbuf->qw_written < PIO_BLOCK_QWS) {
		/*
		 * Still within SOP block.  We don't need to check for
		 * wrap because we are still in the first block and
		 * can only wrap on block boundaries.
		 */
		void __iomem *send;		/* SOP end */
		void __iomem *xend;

		/* calculate the end of data or end of block, whichever
		   comes first */
		send = pbuf->start + PIO_BLOCK_SIZE;
		xend = send < dend ? send : dend;

		/* shift up to SOP=1 space */
		dest += SOP_DISTANCE;
		xend += SOP_DISTANCE;

		/* write 8-byte chunk data */
		while (dest < xend) {
			writeq(*(u64 *)from, dest);
			from += sizeof(u64);
			dest += sizeof(u64);
		}

		/* shift down to SOP=0 space */
		dest -= SOP_DISTANCE;
	}
	/*
	 * At this point dest could be (either, both, or neither):
	 * - at dend
	 * - at the wrap
	 */

	/*
	 * If the wrap comes before or matches the data end,
	 * copy until until the wrap, then wrap.
	 *
	 * If dest is at the wrap, we will fall into the if,
	 * not do the loop, when wrap.
	 *
	 * If the data ends at the end of the SOP above and
	 * the buffer wraps, then pbuf->end == dend == dest
	 * and nothing will get written.
	 */
	if (pbuf->end <= dend) {
		while (dest < pbuf->end) {
			writeq(*(u64 *)from, dest);
			from += sizeof(u64);
			dest += sizeof(u64);
		}

		dest -= pbuf->size;
		dend -= pbuf->size;
	}

	/* write 8-byte non-SOP, non-wrap chunk data */
	while (dest < dend) {
		writeq(*(u64 *)from, dest);
		from += sizeof(u64);
		dest += sizeof(u64);
	}

	/* we know carry_bytes was zero on entry to this routine */
	read_low_bytes(pbuf, from, nbytes & 0x7);

	pbuf->qw_written += nbytes>>3;
}

/*
 * Segmented PIO Copy - middle
 *
 * Must handle any aligned tail and any aligned source with any byte count.
 *
 * @pbuf: a number of blocks allocated within a PIO send context
 * @from: data source
 * @nbytes: number of bytes to copy
 */
void seg_pio_copy_mid(struct pio_buf *pbuf, const void *from, size_t nbytes)
{
	unsigned long from_align = (unsigned long)from & 0x7;

	if (pbuf->carry_bytes + nbytes < 8) {
		/* not enough bytes to fill a QW */
		read_extra_bytes(pbuf, from, nbytes);
		return;
	}

	if (from_align) {
		/* misaligned source pointer - align it */
		unsigned long to_align;

		/* bytes to read to align "from" */
		to_align = 8 - from_align;

		/*
		 * In the advance-to-alignment logic below, we do not need
		 * to check if we are using more than nbytes.  This is because
		 * if we are here, we already know that carry+nbytes will
		 * fill at least one QW.
		 */
		if (pbuf->carry_bytes + to_align < 8) {
			/* not enough align bytes to fill a QW */
			read_extra_bytes(pbuf, from, to_align);
			from += to_align;
			nbytes -= to_align;
		} else {
			/* bytes to fill carry */
			unsigned long to_fill = 8 - pbuf->carry_bytes;
			/* bytes left over to be read */
			unsigned long extra = to_align - to_fill;
			void __iomem *dest;

			/* fill carry... */
			read_extra_bytes(pbuf, from, to_fill);
			from += to_fill;
			nbytes -= to_fill;

			/* ...now write carry */
			dest = pbuf->start + (pbuf->qw_written * sizeof(u64));

			/*
			 * The two checks immediately below cannot both be
			 * true, hence the else.  If we have wrapped, we
			 * cannot still be within the first block.
			 * Conversely, if we are still in the first block, we
			 * cannot have wrapped.  We do the wrap check first
			 * as that is more likely.
			 */
			/* adjust if we've wrapped */
			if (dest >= pbuf->end)
				dest -= pbuf->size;
			/* jump to SOP range if within the first block */
			else if (pbuf->qw_written < PIO_BLOCK_QWS)
				dest += SOP_DISTANCE;

			carry8_write8(pbuf->carry, dest);
			pbuf->qw_written++;

			/* read any extra bytes to do final alignment */
			/* this will overwrite anything in pbuf->carry */
			read_low_bytes(pbuf, from, extra);
			from += extra;
			nbytes -= extra;
		}

		/* at this point, from is QW aligned */
	}

	if (pbuf->carry_bytes)
		mid_copy_mix(pbuf, from, nbytes);
	else
		mid_copy_straight(pbuf, from, nbytes);
}

/*
 * Segmented PIO Copy - end
 *
 * Write any remainder (in pbuf->carry) and finish writing the whole block.
 *
 * @pbuf: a number of blocks allocated within a PIO send context
 */
void seg_pio_copy_end(struct pio_buf *pbuf)
{
	void __iomem *dest = pbuf->start + (pbuf->qw_written * sizeof(u64));

	/*
	 * The two checks immediately below cannot both be true, hence the
	 * else.  If we have wrapped, we cannot still be within the first
	 * block.  Conversely, if we are still in the first block, we
	 * cannot have wrapped.  We do the wrap check first as that is
	 * more likely.
	 */
	/* adjust if we have wrapped */
	if (dest >= pbuf->end)
		dest -= pbuf->size;
	/* jump to the SOP range if within the first block */
	else if (pbuf->qw_written < PIO_BLOCK_QWS)
		dest += SOP_DISTANCE;

	/* write final bytes, if any */
	if (carry_write8(pbuf, dest)) {
		dest += sizeof(u64);
		/*
		 * NOTE: We do not need to recalculate whether dest needs
		 * SOP_DISTANCE or not.
		 *
		 * If we are in the first block and the dangle write
		 * keeps us in the same block, dest will need
		 * to retain SOP_DISTANCE in the loop below.
		 *
		 * If we are in the first block and the dangle write pushes
		 * us to the next block, then loop below will not run
		 * and dest is not used.  Hence we do not need to update
		 * it.
		 *
		 * If we are past the first block, then SOP_DISTANCE
		 * was never added, so there is nothing to do.
		 */
	}

	/* fill in rest of block */
	while (((unsigned long)dest & PIO_BLOCK_MASK) != 0) {
		writeq(0, dest);
		dest += sizeof(u64);
	}

	/* finished with this buffer */
	atomic_dec(&pbuf->sc->buffers_allocated);
}
