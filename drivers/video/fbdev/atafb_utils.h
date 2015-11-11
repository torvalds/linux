#ifndef _VIDEO_ATAFB_UTILS_H
#define _VIDEO_ATAFB_UTILS_H

/* ================================================================= */
/*                      Utility Assembler Functions                  */
/* ================================================================= */

/* ====================================================================== */

/* Those of a delicate disposition might like to skip the next couple of
 * pages.
 *
 * These functions are drop in replacements for memmove and
 * memset(_, 0, _). However their five instances add at least a kilobyte
 * to the object file. You have been warned.
 *
 * Not a great fan of assembler for the sake of it, but I think
 * that these routines are at least 10 times faster than their C
 * equivalents for large blits, and that's important to the lowest level of
 * a graphics driver. Question is whether some scheme with the blitter
 * would be faster. I suspect not for simple text system - not much
 * asynchrony.
 *
 * Code is very simple, just gruesome expansion. Basic strategy is to
 * increase data moved/cleared at each step to 16 bytes to reduce
 * instruction per data move overhead. movem might be faster still
 * For more than 15 bytes, we try to align the write direction on a
 * longword boundary to get maximum speed. This is even more gruesome.
 * Unaligned read/write used requires 68020+ - think this is a problem?
 *
 * Sorry!
 */


/* ++roman: I've optimized Robert's original versions in some minor
 * aspects, e.g. moveq instead of movel, let gcc choose the registers,
 * use movem in some places...
 * For other modes than 1 plane, lots of more such assembler functions
 * were needed (e.g. the ones using movep or expanding color values).
 */

/* ++andreas: more optimizations:
   subl #65536,d0 replaced by clrw d0; subql #1,d0 for dbcc
   addal is faster than addaw
   movep is rather expensive compared to ordinary move's
   some functions rewritten in C for clarity, no speed loss */

static inline void *fb_memclear_small(void *s, size_t count)
{
	if (!count)
		return 0;

	asm volatile ("\n"
		"	lsr.l	#1,%1 ; jcc 1f ; move.b %2,-(%0)\n"
		"1:	lsr.l	#1,%1 ; jcc 1f ; move.w %2,-(%0)\n"
		"1:	lsr.l	#1,%1 ; jcc 1f ; move.l %2,-(%0)\n"
		"1:	lsr.l	#1,%1 ; jcc 1f ; move.l %2,-(%0) ; move.l %2,-(%0)\n"
		"1:"
		: "=a" (s), "=d" (count)
		: "d" (0), "0" ((char *)s + count), "1" (count));
	asm volatile ("\n"
		"	subq.l  #1,%1\n"
		"	jcs	3f\n"
		"	move.l	%2,%%d4; move.l %2,%%d5; move.l %2,%%d6\n"
		"2:	movem.l	%2/%%d4/%%d5/%%d6,-(%0)\n"
		"	dbra	%1,2b\n"
		"3:"
		: "=a" (s), "=d" (count)
		: "d" (0), "0" (s), "1" (count)
		: "d4", "d5", "d6"
		);

	return 0;
}


static inline void *fb_memclear(void *s, size_t count)
{
	if (!count)
		return 0;

	if (count < 16) {
		asm volatile ("\n"
			"	lsr.l	#1,%1 ; jcc 1f ; clr.b (%0)+\n"
			"1:	lsr.l	#1,%1 ; jcc 1f ; clr.w (%0)+\n"
			"1:	lsr.l	#1,%1 ; jcc 1f ; clr.l (%0)+\n"
			"1:	lsr.l	#1,%1 ; jcc 1f ; clr.l (%0)+ ; clr.l (%0)+\n"
			"1:"
			: "=a" (s), "=d" (count)
			: "0" (s), "1" (count));
	} else {
		long tmp;
		asm volatile ("\n"
			"	move.l	%1,%2\n"
			"	lsr.l	#1,%2 ; jcc 1f ; clr.b (%0)+ ; subq.w #1,%1\n"
			"	lsr.l	#1,%2 ; jcs 2f\n"  /* %0 increased=>bit 2 switched*/
			"	clr.w	(%0)+  ; subq.w  #2,%1 ; jra 2f\n"
			"1:	lsr.l	#1,%2 ; jcc 2f\n"
			"	clr.w	(%0)+  ; subq.w  #2,%1\n"
			"2:	move.w	%1,%2; lsr.l #2,%1 ; jeq 6f\n"
			"	lsr.l	#1,%1 ; jcc 3f ; clr.l (%0)+\n"
			"3:	lsr.l	#1,%1 ; jcc 4f ; clr.l (%0)+ ; clr.l (%0)+\n"
			"4:	subq.l	#1,%1 ; jcs 6f\n"
			"5:	clr.l	(%0)+; clr.l (%0)+ ; clr.l (%0)+ ; clr.l (%0)+\n"
			"	dbra	%1,5b ; clr.w %1; subq.l #1,%1; jcc 5b\n"
			"6:	move.w	%2,%1; btst #1,%1 ; jeq 7f ; clr.w (%0)+\n"
			"7:	btst	#0,%1 ; jeq 8f ; clr.b (%0)+\n"
			"8:"
			: "=a" (s), "=d" (count), "=d" (tmp)
			: "0" (s), "1" (count));
	}

	return 0;
}


static inline void *fb_memset255(void *s, size_t count)
{
	if (!count)
		return 0;

	asm volatile ("\n"
		"	lsr.l	#1,%1 ; jcc 1f ; move.b %2,-(%0)\n"
		"1:	lsr.l	#1,%1 ; jcc 1f ; move.w %2,-(%0)\n"
		"1:	lsr.l	#1,%1 ; jcc 1f ; move.l %2,-(%0)\n"
		"1:	lsr.l	#1,%1 ; jcc 1f ; move.l %2,-(%0) ; move.l %2,-(%0)\n"
		"1:"
		: "=a" (s), "=d" (count)
		: "d" (-1), "0" ((char *)s+count), "1" (count));
	asm volatile ("\n"
		"	subq.l	#1,%1 ; jcs 3f\n"
		"	move.l	%2,%%d4; move.l %2,%%d5; move.l %2,%%d6\n"
		"2:	movem.l	%2/%%d4/%%d5/%%d6,-(%0)\n"
		"	dbra	%1,2b\n"
		"3:"
		: "=a" (s), "=d" (count)
		: "d" (-1), "0" (s), "1" (count)
		: "d4", "d5", "d6");

	return 0;
}


static inline void *fb_memmove(void *d, const void *s, size_t count)
{
	if (d < s) {
		if (count < 16) {
			asm volatile ("\n"
				"	lsr.l	#1,%2 ; jcc 1f ; move.b (%1)+,(%0)+\n"
				"1:	lsr.l	#1,%2 ; jcc 1f ; move.w (%1)+,(%0)+\n"
				"1:	lsr.l	#1,%2 ; jcc 1f ; move.l (%1)+,(%0)+\n"
				"1:	lsr.l	#1,%2 ; jcc 1f ; move.l (%1)+,(%0)+ ; move.l (%1)+,(%0)+\n"
				"1:"
				: "=a" (d), "=a" (s), "=d" (count)
				: "0" (d), "1" (s), "2" (count));
		} else {
			long tmp;
			asm volatile ("\n"
				"	move.l	%0,%3\n"
				"	lsr.l	#1,%3 ; jcc 1f ; move.b (%1)+,(%0)+ ; subqw #1,%2\n"
				"	lsr.l	#1,%3 ; jcs 2f\n"  /* %0 increased=>bit 2 switched*/
				"	move.w	(%1)+,(%0)+  ; subqw  #2,%2 ; jra 2f\n"
				"1:	lsr.l   #1,%3 ; jcc 2f\n"
				"	move.w	(%1)+,(%0)+  ; subqw  #2,%2\n"
				"2:	move.w	%2,%-; lsr.l #2,%2 ; jeq 6f\n"
				"	lsr.l	#1,%2 ; jcc 3f ; move.l (%1)+,(%0)+\n"
				"3:	lsr.l	#1,%2 ; jcc 4f ; move.l (%1)+,(%0)+ ; move.l (%1)+,(%0)+\n"
				"4:	subq.l	#1,%2 ; jcs 6f\n"
				"5:	move.l	(%1)+,(%0)+; move.l (%1)+,(%0)+\n"
				"	move.l	(%1)+,(%0)+; move.l (%1)+,(%0)+\n"
				"	dbra	%2,5b ; clr.w %2; subq.l #1,%2; jcc 5b\n"
				"6:	move.w	%+,%2; btst #1,%2 ; jeq 7f ; move.w (%1)+,(%0)+\n"
				"7:	btst	#0,%2 ; jeq 8f ; move.b (%1)+,(%0)+\n"
				"8:"
				: "=a" (d), "=a" (s), "=d" (count), "=d" (tmp)
				: "0" (d), "1" (s), "2" (count));
		}
	} else {
		if (count < 16) {
			asm volatile ("\n"
				"	lsr.l	#1,%2 ; jcc 1f ; move.b -(%1),-(%0)\n"
				"1:	lsr.l	#1,%2 ; jcc 1f ; move.w -(%1),-(%0)\n"
				"1:	lsr.l	#1,%2 ; jcc 1f ; move.l -(%1),-(%0)\n"
				"1:	lsr.l	#1,%2 ; jcc 1f ; move.l -(%1),-(%0) ; move.l -(%1),-(%0)\n"
				"1:"
				: "=a" (d), "=a" (s), "=d" (count)
				: "0" ((char *) d + count), "1" ((char *) s + count), "2" (count));
		} else {
			long tmp;

			asm volatile ("\n"
				"	move.l	%0,%3\n"
				"	lsr.l	#1,%3 ; jcc 1f ; move.b -(%1),-(%0) ; subqw #1,%2\n"
				"	lsr.l	#1,%3 ; jcs 2f\n"  /* %0 increased=>bit 2 switched*/
				"	move.w	-(%1),-(%0) ; subqw  #2,%2 ; jra 2f\n"
				"1:	lsr.l	#1,%3 ; jcc 2f\n"
				"	move.w	-(%1),-(%0) ; subqw  #2,%2\n"
				"2:	move.w	%2,%-; lsr.l #2,%2 ; jeq 6f\n"
				"	lsr.l	#1,%2 ; jcc 3f ; move.l -(%1),-(%0)\n"
				"3:	lsr.l	#1,%2 ; jcc 4f ; move.l -(%1),-(%0) ; move.l -(%1),-(%0)\n"
				"4:	subq.l	#1,%2 ; jcs 6f\n"
				"5:	move.l	-(%1),-(%0); move.l -(%1),-(%0)\n"
				"	move.l	-(%1),-(%0); move.l -(%1),-(%0)\n"
				"	dbra	%2,5b ; clr.w %2; subq.l #1,%2; jcc 5b\n"
				"6:	move.w	%+,%2; btst #1,%2 ; jeq 7f ; move.w -(%1),-(%0)\n"
				"7:	btst	#0,%2 ; jeq 8f ; move.b -(%1),-(%0)\n"
				"8:"
				: "=a" (d), "=a" (s), "=d" (count), "=d" (tmp)
				: "0" ((char *) d + count), "1" ((char *) s + count), "2" (count));
		}
	}

	return 0;
}


/* ++andreas: Simple and fast version of memmove, assumes size is
   divisible by 16, suitable for moving the whole screen bitplane */
static inline void fast_memmove(char *dst, const char *src, size_t size)
{
	if (!size)
		return;
	if (dst < src)
		asm volatile ("\n"
			"1:	movem.l	(%0)+,%%d0/%%d1/%%a0/%%a1\n"
			"	movem.l	%%d0/%%d1/%%a0/%%a1,%1@\n"
			"	addq.l	#8,%1; addq.l #8,%1\n"
			"	dbra	%2,1b\n"
			"	clr.w	%2; subq.l #1,%2\n"
			"	jcc	1b"
			: "=a" (src), "=a" (dst), "=d" (size)
			: "0" (src), "1" (dst), "2" (size / 16 - 1)
			: "d0", "d1", "a0", "a1", "memory");
	else
		asm volatile ("\n"
			"1:	subq.l	#8,%0; subq.l #8,%0\n"
			"	movem.l	%0@,%%d0/%%d1/%%a0/%%a1\n"
			"	movem.l	%%d0/%%d1/%%a0/%%a1,-(%1)\n"
			"	dbra	%2,1b\n"
			"	clr.w	%2; subq.l #1,%2\n"
			"	jcc 1b"
			: "=a" (src), "=a" (dst), "=d" (size)
			: "0" (src + size), "1" (dst + size), "2" (size / 16 - 1)
			: "d0", "d1", "a0", "a1", "memory");
}

#ifdef BPL

/*
 * This expands a up to 8 bit color into two longs
 * for movel operations.
 */
static const u32 four2long[] = {
	0x00000000, 0x000000ff, 0x0000ff00, 0x0000ffff,
	0x00ff0000, 0x00ff00ff, 0x00ffff00, 0x00ffffff,
	0xff000000, 0xff0000ff, 0xff00ff00, 0xff00ffff,
	0xffff0000, 0xffff00ff, 0xffffff00, 0xffffffff,
};

static inline void expand8_col2mask(u8 c, u32 m[])
{
	m[0] = four2long[c & 15];
#if BPL > 4
	m[1] = four2long[c >> 4];
#endif
}

static inline void expand8_2col2mask(u8 fg, u8 bg, u32 fgm[], u32 bgm[])
{
	fgm[0] = four2long[fg & 15] ^ (bgm[0] = four2long[bg & 15]);
#if BPL > 4
	fgm[1] = four2long[fg >> 4] ^ (bgm[1] = four2long[bg >> 4]);
#endif
}

/*
 * set an 8bit value to a color
 */
static inline void fill8_col(u8 *dst, u32 m[])
{
	u32 tmp = m[0];
	dst[0] = tmp;
	dst[2] = (tmp >>= 8);
#if BPL > 2
	dst[4] = (tmp >>= 8);
	dst[6] = tmp >> 8;
#endif
#if BPL > 4
	tmp = m[1];
	dst[8] = tmp;
	dst[10] = (tmp >>= 8);
	dst[12] = (tmp >>= 8);
	dst[14] = tmp >> 8;
#endif
}

/*
 * set an 8bit value according to foreground/background color
 */
static inline void fill8_2col(u8 *dst, u8 fg, u8 bg, u32 mask)
{
	u32 fgm[2], bgm[2], tmp;

	expand8_2col2mask(fg, bg, fgm, bgm);

	mask |= mask << 8;
#if BPL > 2
	mask |= mask << 16;
#endif
	tmp = (mask & fgm[0]) ^ bgm[0];
	dst[0] = tmp;
	dst[2] = (tmp >>= 8);
#if BPL > 2
	dst[4] = (tmp >>= 8);
	dst[6] = tmp >> 8;
#endif
#if BPL > 4
	tmp = (mask & fgm[1]) ^ bgm[1];
	dst[8] = tmp;
	dst[10] = (tmp >>= 8);
	dst[12] = (tmp >>= 8);
	dst[14] = tmp >> 8;
#endif
}

static const u32 two2word[] = {
	0x00000000, 0xffff0000, 0x0000ffff, 0xffffffff
};

static inline void expand16_col2mask(u8 c, u32 m[])
{
	m[0] = two2word[c & 3];
#if BPL > 2
	m[1] = two2word[(c >> 2) & 3];
#endif
#if BPL > 4
	m[2] = two2word[(c >> 4) & 3];
	m[3] = two2word[c >> 6];
#endif
}

static inline void expand16_2col2mask(u8 fg, u8 bg, u32 fgm[], u32 bgm[])
{
	bgm[0] = two2word[bg & 3];
	fgm[0] = two2word[fg & 3] ^ bgm[0];
#if BPL > 2
	bgm[1] = two2word[(bg >> 2) & 3];
	fgm[1] = two2word[(fg >> 2) & 3] ^ bgm[1];
#endif
#if BPL > 4
	bgm[2] = two2word[(bg >> 4) & 3];
	fgm[2] = two2word[(fg >> 4) & 3] ^ bgm[2];
	bgm[3] = two2word[bg >> 6];
	fgm[3] = two2word[fg >> 6] ^ bgm[3];
#endif
}

static inline u32 *fill16_col(u32 *dst, int rows, u32 m[])
{
	while (rows) {
		*dst++ = m[0];
#if BPL > 2
		*dst++ = m[1];
#endif
#if BPL > 4
		*dst++ = m[2];
		*dst++ = m[3];
#endif
		rows--;
	}
	return dst;
}

static inline void memmove32_col(void *dst, void *src, u32 mask, u32 h, u32 bytes)
{
	u32 *s, *d, v;

        s = src;
        d = dst;
        do {
                v = (*s++ & mask) | (*d  & ~mask);
                *d++ = v;
#if BPL > 2
                v = (*s++ & mask) | (*d  & ~mask);
                *d++ = v;
#endif
#if BPL > 4
                v = (*s++ & mask) | (*d  & ~mask);
                *d++ = v;
                v = (*s++ & mask) | (*d  & ~mask);
                *d++ = v;
#endif
                d = (u32 *)((u8 *)d + bytes);
                s = (u32 *)((u8 *)s + bytes);
        } while (--h);
}

#endif

#endif /* _VIDEO_ATAFB_UTILS_H */
