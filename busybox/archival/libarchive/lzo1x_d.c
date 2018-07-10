/* implementation of the LZO1X decompression algorithm

   This file is part of the LZO real-time data compression library.

   Copyright (C) 1996..2008 Markus Franz Xaver Johannes Oberhumer
   All Rights Reserved.

   Markus F.X.J. Oberhumer <markus@oberhumer.com>
   http://www.oberhumer.com/opensource/lzo/

   The LZO library is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of
   the License, or (at your option) any later version.

   The LZO library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the LZO library; see the file COPYING.
   If not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */
#include "libbb.h"
#include "liblzo.h"

/***********************************************************************
// decompress a block of data.
************************************************************************/
/* safe decompression with overrun testing */
int lzo1x_decompress_safe(const uint8_t* in, unsigned in_len,
		uint8_t* out, unsigned* out_len /*, void* wrkmem */)
{
	register uint8_t* op;
	register const uint8_t* ip;
	register unsigned t;
#if defined(COPY_DICT)
	unsigned m_off;
	const uint8_t* dict_end;
#else
	register const uint8_t* m_pos = NULL; /* possibly not needed */
#endif
	const uint8_t* const ip_end = in + in_len;
#if defined(HAVE_ANY_OP)
	uint8_t* const op_end = out + *out_len;
#endif
#if defined(LZO1Z)
	unsigned last_m_off = 0;
#endif

//	LZO_UNUSED(wrkmem);

#if defined(COPY_DICT)
	if (dict) {
		if (dict_len > M4_MAX_OFFSET) {
			dict += dict_len - M4_MAX_OFFSET;
			dict_len = M4_MAX_OFFSET;
		}
		dict_end = dict + dict_len;
	} else {
		dict_len = 0;
		dict_end = NULL;
	}
#endif /* COPY_DICT */

	*out_len = 0;

	op = out;
	ip = in;

	if (*ip > 17) {
		t = *ip++ - 17;
		if (t < 4)
			goto match_next;
		assert(t > 0); NEED_OP(t); NEED_IP(t+1);
		do *op++ = *ip++; while (--t > 0);
		goto first_literal_run;
	}

	while (TEST_IP && TEST_OP) {
		t = *ip++;
		if (t >= 16)
			goto match;
		/* a literal run */
		if (t == 0) {
			NEED_IP(1);
			while (*ip == 0) {
				t += 255;
				ip++;
				NEED_IP(1);
			}
			TEST_IV(t);
			t += 15 + *ip++;
		}
		/* copy literals */
		assert(t > 0);
		NEED_OP(t+3);
		NEED_IP(t+4);
#if defined(LZO_UNALIGNED_OK_4) || defined(LZO_ALIGNED_OK_4)
# if !defined(LZO_UNALIGNED_OK_4)
		if (PTR_ALIGNED2_4(op, ip))
# endif
		{
			COPY4(op, ip);
			op += 4;
			ip += 4;
			if (--t > 0) {
				if (t >= 4) {
					do {
						COPY4(op, ip);
						op += 4;
						ip += 4;
						t -= 4;
					} while (t >= 4);
					if (t > 0)
						do *op++ = *ip++; while (--t > 0);
				} else {
					do *op++ = *ip++; while (--t > 0);
				}
			}
		}
# if !defined(LZO_UNALIGNED_OK_4)
		else
# endif
#endif
#if !defined(LZO_UNALIGNED_OK_4)
		{
			*op++ = *ip++;
			*op++ = *ip++;
			*op++ = *ip++;
			do *op++ = *ip++; while (--t > 0);
		}
#endif

 first_literal_run:
		t = *ip++;
		if (t >= 16)
			goto match;
#if defined(COPY_DICT)
#if defined(LZO1Z)
		m_off = (1 + M2_MAX_OFFSET) + (t << 6) + (*ip++ >> 2);
		last_m_off = m_off;
#else
		m_off = (1 + M2_MAX_OFFSET) + (t >> 2) + (*ip++ << 2);
#endif
		NEED_OP(3);
		t = 3; COPY_DICT(t,m_off)
#else /* !COPY_DICT */
#if defined(LZO1Z)
		t = (1 + M2_MAX_OFFSET) + (t << 6) + (*ip++ >> 2);
		m_pos = op - t;
		last_m_off = t;
#else
		m_pos = op - (1 + M2_MAX_OFFSET);
		m_pos -= t >> 2;
		m_pos -= *ip++ << 2;
#endif
		TEST_LB(m_pos); NEED_OP(3);
		*op++ = *m_pos++;
		*op++ = *m_pos++;
		*op++ = *m_pos;
#endif /* COPY_DICT */
		goto match_done;

		/* handle matches */
		do {
 match:
			if (t >= 64) {		/* a M2 match */
#if defined(COPY_DICT)
#if defined(LZO1X)
				m_off = 1 + ((t >> 2) & 7) + (*ip++ << 3);
				t = (t >> 5) - 1;
#elif defined(LZO1Y)
				m_off = 1 + ((t >> 2) & 3) + (*ip++ << 2);
				t = (t >> 4) - 3;
#elif defined(LZO1Z)
				m_off = t & 0x1f;
				if (m_off >= 0x1c)
					m_off = last_m_off;
				else {
					m_off = 1 + (m_off << 6) + (*ip++ >> 2);
					last_m_off = m_off;
				}
				t = (t >> 5) - 1;
#endif
#else /* !COPY_DICT */
#if defined(LZO1X)
				m_pos = op - 1;
				m_pos -= (t >> 2) & 7;
				m_pos -= *ip++ << 3;
				t = (t >> 5) - 1;
#elif defined(LZO1Y)
				m_pos = op - 1;
				m_pos -= (t >> 2) & 3;
				m_pos -= *ip++ << 2;
				t = (t >> 4) - 3;
#elif defined(LZO1Z)
				{
					unsigned off = t & 0x1f;
					m_pos = op;
					if (off >= 0x1c) {
						assert(last_m_off > 0);
						m_pos -= last_m_off;
					} else {
						off = 1 + (off << 6) + (*ip++ >> 2);
						m_pos -= off;
						last_m_off = off;
					}
				}
				t = (t >> 5) - 1;
#endif
				TEST_LB(m_pos); assert(t > 0); NEED_OP(t+3-1);
				goto copy_match;
#endif /* COPY_DICT */
			}
			else if (t >= 32) {			/* a M3 match */
				t &= 31;
				if (t == 0) {
					NEED_IP(1);
					while (*ip == 0) {
						t += 255;
						ip++;
						NEED_IP(1);
					}
					TEST_IV(t);
					t += 31 + *ip++;
				}
#if defined(COPY_DICT)
#if defined(LZO1Z)
				m_off = 1 + (ip[0] << 6) + (ip[1] >> 2);
				last_m_off = m_off;
#else
				m_off = 1 + (ip[0] >> 2) + (ip[1] << 6);
#endif
#else /* !COPY_DICT */
#if defined(LZO1Z)
				{
					unsigned off = 1 + (ip[0] << 6) + (ip[1] >> 2);
					m_pos = op - off;
					last_m_off = off;
				}
#elif defined(LZO_UNALIGNED_OK_2) && defined(LZO_ABI_LITTLE_ENDIAN)
				m_pos = op - 1;
				m_pos -= (* (const lzo_ushortp) ip) >> 2;
#else
				m_pos = op - 1;
				m_pos -= (ip[0] >> 2) + (ip[1] << 6);
#endif
#endif /* COPY_DICT */
				ip += 2;
			}
			else if (t >= 16) {			/* a M4 match */
#if defined(COPY_DICT)
				m_off = (t & 8) << 11;
#else /* !COPY_DICT */
				m_pos = op;
				m_pos -= (t & 8) << 11;
#endif /* COPY_DICT */
				t &= 7;
				if (t == 0) {
					NEED_IP(1);
					while (*ip == 0) {
						t += 255;
						ip++;
						NEED_IP(1);
					}
					TEST_IV(t);
					t += 7 + *ip++;
				}
#if defined(COPY_DICT)
#if defined(LZO1Z)
				m_off += (ip[0] << 6) + (ip[1] >> 2);
#else
				m_off += (ip[0] >> 2) + (ip[1] << 6);
#endif
				ip += 2;
				if (m_off == 0)
					goto eof_found;
				m_off += 0x4000;
#if defined(LZO1Z)
				last_m_off = m_off;
#endif
#else /* !COPY_DICT */
#if defined(LZO1Z)
				m_pos -= (ip[0] << 6) + (ip[1] >> 2);
#elif defined(LZO_UNALIGNED_OK_2) && defined(LZO_ABI_LITTLE_ENDIAN)
				m_pos -= (* (const lzo_ushortp) ip) >> 2;
#else
				m_pos -= (ip[0] >> 2) + (ip[1] << 6);
#endif
				ip += 2;
				if (m_pos == op)
					goto eof_found;
				m_pos -= 0x4000;
#if defined(LZO1Z)
				last_m_off = pd((const uint8_t*)op, m_pos);
#endif
#endif /* COPY_DICT */
			}
			else {				/* a M1 match */
#if defined(COPY_DICT)
#if defined(LZO1Z)
				m_off = 1 + (t << 6) + (*ip++ >> 2);
				last_m_off = m_off;
#else
				m_off = 1 + (t >> 2) + (*ip++ << 2);
#endif
				NEED_OP(2);
				t = 2; COPY_DICT(t,m_off)
#else /* !COPY_DICT */
#if defined(LZO1Z)
				t = 1 + (t << 6) + (*ip++ >> 2);
				m_pos = op - t;
				last_m_off = t;
#else
				m_pos = op - 1;
				m_pos -= t >> 2;
				m_pos -= *ip++ << 2;
#endif
				TEST_LB(m_pos); NEED_OP(2);
				*op++ = *m_pos++;
				*op++ = *m_pos;
#endif /* COPY_DICT */
				goto match_done;
			}

			/* copy match */
#if defined(COPY_DICT)

			NEED_OP(t+3-1);
			t += 3-1; COPY_DICT(t,m_off)

#else /* !COPY_DICT */

			TEST_LB(m_pos); assert(t > 0); NEED_OP(t+3-1);
#if defined(LZO_UNALIGNED_OK_4) || defined(LZO_ALIGNED_OK_4)
# if !defined(LZO_UNALIGNED_OK_4)
			if (t >= 2 * 4 - (3 - 1) && PTR_ALIGNED2_4(op,m_pos)) {
				assert((op - m_pos) >= 4);	/* both pointers are aligned */
# else
			if (t >= 2 * 4 - (3 - 1) && (op - m_pos) >= 4) {
# endif
				COPY4(op,m_pos);
				op += 4; m_pos += 4; t -= 4 - (3 - 1);
				do {
					COPY4(op,m_pos);
					op += 4; m_pos += 4; t -= 4;
				} while (t >= 4);
				if (t > 0)
					do *op++ = *m_pos++; while (--t > 0);
			}
			else
#endif
			{
 copy_match:
				*op++ = *m_pos++; *op++ = *m_pos++;
				do *op++ = *m_pos++; while (--t > 0);
			}

#endif /* COPY_DICT */

 match_done:
#if defined(LZO1Z)
			t = ip[-1] & 3;
#else
			t = ip[-2] & 3;
#endif
			if (t == 0)
				break;

			/* copy literals */
 match_next:
			assert(t > 0);
			assert(t < 4);
			NEED_OP(t);
			NEED_IP(t+1);
#if 0
			do *op++ = *ip++; while (--t > 0);
#else
			*op++ = *ip++;
			if (t > 1) {
				*op++ = *ip++;
				if (t > 2)
					*op++ = *ip++;
			}
#endif
			t = *ip++;
		} while (TEST_IP && TEST_OP);
	}

//#if defined(HAVE_TEST_IP) || defined(HAVE_TEST_OP)
	/* no EOF code was found */
	*out_len = pd(op, out);
	return LZO_E_EOF_NOT_FOUND;
//#endif

 eof_found:
	assert(t == 1);
	*out_len = pd(op, out);
	return (ip == ip_end ? LZO_E_OK :
		   (ip < ip_end	 ? LZO_E_INPUT_NOT_CONSUMED : LZO_E_INPUT_OVERRUN));

//#if defined(HAVE_NEED_IP)
 input_overrun:
	*out_len = pd(op, out);
	return LZO_E_INPUT_OVERRUN;
//#endif

//#if defined(HAVE_NEED_OP)
 output_overrun:
	*out_len = pd(op, out);
	return LZO_E_OUTPUT_OVERRUN;
//#endif

//#if defined(LZO_TEST_OVERRUN_LOOKBEHIND)
 lookbehind_overrun:
	*out_len = pd(op, out);
	return LZO_E_LOOKBEHIND_OVERRUN;
//#endif
}
