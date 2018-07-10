/*
 * This file uses XZ Embedded library code which is written
 * by Lasse Collin <lasse.collin@tukaani.org>
 * and Igor Pavlov <http://7-zip.org/>
 *
 * See README file in unxz/ directory for more information.
 *
 * This file is:
 * Copyright (C) 2010 Denys Vlasenko <vda.linux@googlemail.com>
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include "bb_archive.h"

#define XZ_FUNC FAST_FUNC
#define XZ_EXTERN static

#define XZ_DEC_DYNALLOC

/* Skip check (rather than fail) of unsupported hash functions */
#define XZ_DEC_ANY_CHECK  1

/* We use our own crc32 function */
#define XZ_INTERNAL_CRC32 0
static uint32_t xz_crc32(const uint8_t *buf, size_t size, uint32_t crc)
{
	return ~crc32_block_endian0(~crc, buf, size, global_crc32_table);
}

/* We use arch-optimized unaligned fixed-endian accessors.
 * They have been moved to libbb (proved to be useful elsewhere as well),
 * just check that we have them defined:
 */
#if !defined(get_unaligned_le32) \
 || !defined(get_unaligned_be32) \
 || !defined(put_unaligned_le32) \
 || !defined(put_unaligned_be32)
# error get_unaligned_le32 accessors are not defined
#endif

#include "unxz/xz_dec_bcj.c"
#include "unxz/xz_dec_lzma2.c"
#include "unxz/xz_dec_stream.c"

IF_DESKTOP(long long) int FAST_FUNC
unpack_xz_stream(transformer_state_t *xstate)
{
	enum xz_ret xz_result;
	struct xz_buf iobuf;
	struct xz_dec *state;
	unsigned char *membuf;
	IF_DESKTOP(long long) int total = 0;

	if (!global_crc32_table)
		global_crc32_new_table_le();

	memset(&iobuf, 0, sizeof(iobuf));
	membuf = xmalloc(2 * BUFSIZ);
	iobuf.in = membuf;
	iobuf.out = membuf + BUFSIZ;
	iobuf.out_size = BUFSIZ;

	if (!xstate || xstate->signature_skipped) {
		/* Preload XZ file signature */
		strcpy((char*)membuf, HEADER_MAGIC);
		iobuf.in_size = HEADER_MAGIC_SIZE;
	} /* else: let xz code read & check it */

	/* Limit memory usage to about 64 MiB. */
	state = xz_dec_init(XZ_DYNALLOC, 64*1024*1024);

	xz_result = X_OK;
	while (1) {
		if (iobuf.in_pos == iobuf.in_size) {
			int rd = safe_read(xstate->src_fd, membuf, BUFSIZ);
			if (rd < 0) {
				bb_error_msg(bb_msg_read_error);
				total = -1;
				break;
			}
			if (rd == 0 && xz_result == XZ_STREAM_END)
				break;
			iobuf.in_size = rd;
			iobuf.in_pos = 0;
		}
		if (xz_result == XZ_STREAM_END) {
			/*
			 * Try to start decoding next concatenated stream.
			 * Stream padding must always be a multiple of four
			 * bytes to preserve four-byte alignment. To keep the
			 * code slightly smaller, we aren't as strict here as
			 * the .xz spec requires. We just skip all zero-bytes
			 * without checking the alignment and thus can accept
			 * files that aren't valid, e.g. the XZ utils test
			 * files bad-0pad-empty.xz and bad-0catpad-empty.xz.
			 */
			do {
				if (membuf[iobuf.in_pos] != 0) {
					xz_dec_reset(state);
					goto do_run;
				}
				iobuf.in_pos++;
			} while (iobuf.in_pos < iobuf.in_size);
		}
 do_run:
//		bb_error_msg(">in pos:%d size:%d out pos:%d size:%d",
//				iobuf.in_pos, iobuf.in_size, iobuf.out_pos, iobuf.out_size);
		xz_result = xz_dec_run(state, &iobuf);
//		bb_error_msg("<in pos:%d size:%d out pos:%d size:%d r:%d",
//				iobuf.in_pos, iobuf.in_size, iobuf.out_pos, iobuf.out_size, xz_result);
		if (iobuf.out_pos) {
			xtransformer_write(xstate, iobuf.out, iobuf.out_pos);
			IF_DESKTOP(total += iobuf.out_pos;)
			iobuf.out_pos = 0;
		}
		if (xz_result == XZ_STREAM_END) {
			/*
			 * Can just "break;" here, if not for concatenated
			 * .xz streams.
			 * Checking for padding may require buffer
			 * replenishment. Can't do it here.
			 */
			continue;
		}
		if (xz_result != XZ_OK && xz_result != XZ_UNSUPPORTED_CHECK) {
			bb_error_msg("corrupted data");
			total = -1;
			break;
		}
	}

	xz_dec_end(state);
	free(membuf);

	return total;
}
