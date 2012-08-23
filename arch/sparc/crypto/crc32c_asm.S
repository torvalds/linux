#include <linux/linkage.h>
#include <asm/visasm.h>
#include <asm/asi.h>

#define F3F(x,y,z)	(((x)<<30)|((y)<<19)|((z)<<5))

#define FPD_ENCODE(x)	(((x) >> 5) | ((x) & ~(0x20)))

#define RS1(x)		(FPD_ENCODE(x) << 14)
#define RS2(x)		(FPD_ENCODE(x) <<  0)
#define RD(x)		(FPD_ENCODE(x) << 25)

#define CRC32C(a,b,c)	\
	.word		(F3F(2,0x36,0x147)|RS1(a)|RS2(b)|RD(c));

ENTRY(crc32c_sparc64)
	/* %o0=crc32p, %o1=data_ptr, %o2=len */
	VISEntryHalf
	lda	[%o0] ASI_PL, %f1
1:	ldd	[%o1], %f2
	CRC32C(0,2,0)
	subcc	%o2, 8, %o2
	bne,pt	%icc, 1b
	 add	%o1, 0x8, %o1
	sta	%f1, [%o0] ASI_PL
	VISExitHalf
2:	retl
	 nop
ENDPROC(crc32c_sparc64)
