/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __H8300_LIBGCC_H__
#define __H8300_LIBGCC_H__

#ifdef __ASSEMBLY__
#define A0 r0
#define A0L r0l
#define A0H r0h

#define A1 r1
#define A1L r1l
#define A1H r1h

#define A2 r2
#define A2L r2l
#define A2H r2h

#define A3 r3
#define A3L r3l
#define A3H r3h

#define S0 r4
#define S0L r4l
#define S0H r4h

#define S1 r5
#define S1L r5l
#define S1H r5h

#define S2 r6
#define S2L r6l
#define S2H r6h

#define PUSHP	push.l
#define POPP	pop.l

#define A0P	er0
#define A1P	er1
#define A2P	er2
#define A3P	er3
#define S0P	er4
#define S1P	er5
#define S2P	er6

#define A0E	e0
#define A1E	e1
#define A2E	e2
#define A3E	e3
#else
#define Wtype   SItype
#define UWtype  USItype
#define HWtype  SItype
#define UHWtype USItype
#define DWtype  DItype
#define UDWtype UDItype
#define UWtype  USItype
#define Wtype   SItype
#define UWtype  USItype
#define W_TYPE_SIZE (4 * BITS_PER_UNIT)
#define BITS_PER_UNIT (8)

typedef          int SItype     __attribute__ ((mode (SI)));
typedef unsigned int USItype    __attribute__ ((mode (SI)));
typedef		 int DItype	__attribute__ ((mode (DI)));
typedef unsigned int UDItype	__attribute__ ((mode (DI)));
struct DWstruct {
	Wtype high, low;
};
typedef union {
	struct DWstruct s;
	DWtype ll;
} DWunion;

typedef int word_type __attribute__ ((mode (__word__)));

#endif

#endif
