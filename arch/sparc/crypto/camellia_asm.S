#include <linux/linkage.h>
#include <asm/visasm.h>

#include "opcodes.h"

#define CAMELLIA_6ROUNDS(KEY_BASE, I0, I1) \
	CAMELLIA_F(KEY_BASE +  0, I1, I0, I1) \
	CAMELLIA_F(KEY_BASE +  2, I0, I1, I0) \
	CAMELLIA_F(KEY_BASE +  4, I1, I0, I1) \
	CAMELLIA_F(KEY_BASE +  6, I0, I1, I0) \
	CAMELLIA_F(KEY_BASE +  8, I1, I0, I1) \
	CAMELLIA_F(KEY_BASE + 10, I0, I1, I0)

#define CAMELLIA_6ROUNDS_FL_FLI(KEY_BASE, I0, I1) \
	CAMELLIA_6ROUNDS(KEY_BASE, I0, I1) \
	CAMELLIA_FL(KEY_BASE + 12, I0, I0) \
	CAMELLIA_FLI(KEY_BASE + 14, I1, I1)

	.data

	.align	8
SIGMA:	.xword	0xA09E667F3BCC908B
	.xword	0xB67AE8584CAA73B2
	.xword	0xC6EF372FE94F82BE
	.xword	0x54FF53A5F1D36F1C
	.xword	0x10E527FADE682D1D
	.xword	0xB05688C2B3E6C1FD

	.text

	.align	32
ENTRY(camellia_sparc64_key_expand)
	/* %o0=in_key, %o1=encrypt_key, %o2=key_len, %o3=decrypt_key */
	VISEntry
	ld	[%o0 + 0x00], %f0	! i0, k[0]
	ld	[%o0 + 0x04], %f1	! i1, k[1]
	ld	[%o0 + 0x08], %f2	! i2, k[2]
	ld	[%o0 + 0x0c], %f3	! i3, k[3]
	std	%f0, [%o1 + 0x00]	! k[0, 1]
	fsrc2	%f0, %f28
	std	%f2, [%o1 + 0x08]	! k[2, 3]
	cmp	%o2, 16
	be	10f
	 fsrc2	%f2, %f30

	ld	[%o0 + 0x10], %f0
	ld	[%o0 + 0x14], %f1
	std	%f0, [%o1 + 0x20]	! k[8, 9]
	cmp	%o2, 24
	fone	%f10
	be,a	1f
	 fxor	%f10, %f0, %f2
	ld	[%o0 + 0x18], %f2
	ld	[%o0 + 0x1c], %f3
1:
	std	%f2, [%o1 + 0x28]	! k[10, 11]
	fxor	%f28, %f0, %f0
	fxor	%f30, %f2, %f2

10:
	sethi	%hi(SIGMA), %g3
	or	%g3, %lo(SIGMA), %g3
	ldd	[%g3 + 0x00], %f16
	ldd	[%g3 + 0x08], %f18
	ldd	[%g3 + 0x10], %f20
	ldd	[%g3 + 0x18], %f22
	ldd	[%g3 + 0x20], %f24
	ldd	[%g3 + 0x28], %f26
	CAMELLIA_F(16, 2, 0, 2)
	CAMELLIA_F(18, 0, 2, 0)
	fxor	%f28, %f0, %f0
	fxor	%f30, %f2, %f2
	CAMELLIA_F(20, 2, 0, 2)
	CAMELLIA_F(22, 0, 2, 0)

#define ROTL128(S01, S23, TMP1, TMP2, N)	\
	srlx	S01, (64 - N), TMP1;		\
	sllx	S01, N, S01;			\
	srlx	S23, (64 - N), TMP2;		\
	sllx	S23, N, S23;			\
	or	S01, TMP2, S01;			\
	or	S23, TMP1, S23

	cmp	%o2, 16
	bne	1f
	 nop
	/* 128-bit key */
	std	%f0, [%o1 + 0x10]	! k[ 4,  5]
	std	%f2, [%o1 + 0x18]	! k[ 6,  7]
	MOVDTOX_F0_O4
	MOVDTOX_F2_O5
	ROTL128(%o4, %o5, %g2, %g3, 15)
	stx	%o4, [%o1 + 0x30]	! k[12, 13]
	stx	%o5, [%o1 + 0x38]	! k[14, 15]
	ROTL128(%o4, %o5, %g2, %g3, 15)
	stx	%o4, [%o1 + 0x40]	! k[16, 17]
	stx	%o5, [%o1 + 0x48]	! k[18, 19]
	ROTL128(%o4, %o5, %g2, %g3, 15)
	stx	%o4, [%o1 + 0x60]	! k[24, 25]
	ROTL128(%o4, %o5, %g2, %g3, 15)
	stx	%o4, [%o1 + 0x70]	! k[28, 29]
	stx	%o5, [%o1 + 0x78]	! k[30, 31]
	ROTL128(%o4, %o5, %g2, %g3, 34)
	stx	%o4, [%o1 + 0xa0]	! k[40, 41]
	stx	%o5, [%o1 + 0xa8]	! k[42, 43]
	ROTL128(%o4, %o5, %g2, %g3, 17)
	stx	%o4, [%o1 + 0xc0]	! k[48, 49]
	stx	%o5, [%o1 + 0xc8]	! k[50, 51]

	ldx	[%o1 + 0x00], %o4	! k[ 0,  1]
	ldx	[%o1 + 0x08], %o5	! k[ 2,  3]
	ROTL128(%o4, %o5, %g2, %g3, 15)
	stx	%o4, [%o1 + 0x20]	! k[ 8,  9]
	stx	%o5, [%o1 + 0x28]	! k[10, 11]
	ROTL128(%o4, %o5, %g2, %g3, 30)
	stx	%o4, [%o1 + 0x50]	! k[20, 21]
	stx	%o5, [%o1 + 0x58]	! k[22, 23]
	ROTL128(%o4, %o5, %g2, %g3, 15)
	stx	%o5, [%o1 + 0x68]	! k[26, 27]
	ROTL128(%o4, %o5, %g2, %g3, 17)
	stx	%o4, [%o1 + 0x80]	! k[32, 33]
	stx	%o5, [%o1 + 0x88]	! k[34, 35]
	ROTL128(%o4, %o5, %g2, %g3, 17)
	stx	%o4, [%o1 + 0x90]	! k[36, 37]
	stx	%o5, [%o1 + 0x98]	! k[38, 39]
	ROTL128(%o4, %o5, %g2, %g3, 17)
	stx	%o4, [%o1 + 0xb0]	! k[44, 45]
	stx	%o5, [%o1 + 0xb8]	! k[46, 47]

	ba,pt	%xcc, 2f
	 mov	(3 * 16 * 4), %o0

1:
	/* 192-bit or 256-bit key */
	std	%f0, [%o1 + 0x30]	! k[12, 13]
	std	%f2, [%o1 + 0x38]	! k[14, 15]
	ldd	[%o1 + 0x20], %f4	! k[ 8,  9]
	ldd	[%o1 + 0x28], %f6	! k[10, 11]
	fxor	%f0, %f4, %f0
	fxor	%f2, %f6, %f2
	CAMELLIA_F(24, 2, 0, 2)
	CAMELLIA_F(26, 0, 2, 0)
	std	%f0, [%o1 + 0x10]	! k[ 4,  5]
	std	%f2, [%o1 + 0x18]	! k[ 6,  7]
	MOVDTOX_F0_O4
	MOVDTOX_F2_O5
	ROTL128(%o4, %o5, %g2, %g3, 30)
	stx	%o4, [%o1 + 0x50]	! k[20, 21]
	stx	%o5, [%o1 + 0x58]	! k[22, 23]
	ROTL128(%o4, %o5, %g2, %g3, 30)
	stx	%o4, [%o1 + 0xa0]	! k[40, 41]
	stx	%o5, [%o1 + 0xa8]	! k[42, 43]
	ROTL128(%o4, %o5, %g2, %g3, 51)
	stx	%o4, [%o1 + 0x100]	! k[64, 65]
	stx	%o5, [%o1 + 0x108]	! k[66, 67]
	ldx	[%o1 + 0x20], %o4	! k[ 8,  9]
	ldx	[%o1 + 0x28], %o5	! k[10, 11]
	ROTL128(%o4, %o5, %g2, %g3, 15)
	stx	%o4, [%o1 + 0x20]	! k[ 8,  9]
	stx	%o5, [%o1 + 0x28]	! k[10, 11]
	ROTL128(%o4, %o5, %g2, %g3, 15)
	stx	%o4, [%o1 + 0x40]	! k[16, 17]
	stx	%o5, [%o1 + 0x48]	! k[18, 19]
	ROTL128(%o4, %o5, %g2, %g3, 30)
	stx	%o4, [%o1 + 0x90]	! k[36, 37]
	stx	%o5, [%o1 + 0x98]	! k[38, 39]
	ROTL128(%o4, %o5, %g2, %g3, 34)
	stx	%o4, [%o1 + 0xd0]	! k[52, 53]
	stx	%o5, [%o1 + 0xd8]	! k[54, 55]
	ldx	[%o1 + 0x30], %o4	! k[12, 13]
	ldx	[%o1 + 0x38], %o5	! k[14, 15]
	ROTL128(%o4, %o5, %g2, %g3, 15)
	stx	%o4, [%o1 + 0x30]	! k[12, 13]
	stx	%o5, [%o1 + 0x38]	! k[14, 15]
	ROTL128(%o4, %o5, %g2, %g3, 30)
	stx	%o4, [%o1 + 0x70]	! k[28, 29]
	stx	%o5, [%o1 + 0x78]	! k[30, 31]
	srlx	%o4, 32, %g2
	srlx	%o5, 32, %g3
	stw	%o4, [%o1 + 0xc0]	! k[48]
	stw	%g3, [%o1 + 0xc4]	! k[49]
	stw	%o5, [%o1 + 0xc8]	! k[50]
	stw	%g2, [%o1 + 0xcc]	! k[51]
	ROTL128(%o4, %o5, %g2, %g3, 49)
	stx	%o4, [%o1 + 0xe0]	! k[56, 57]
	stx	%o5, [%o1 + 0xe8]	! k[58, 59]
	ldx	[%o1 + 0x00], %o4	! k[ 0,  1]
	ldx	[%o1 + 0x08], %o5	! k[ 2,  3]
	ROTL128(%o4, %o5, %g2, %g3, 45)
	stx	%o4, [%o1 + 0x60]	! k[24, 25]
	stx	%o5, [%o1 + 0x68]	! k[26, 27]
	ROTL128(%o4, %o5, %g2, %g3, 15)
	stx	%o4, [%o1 + 0x80]	! k[32, 33]
	stx	%o5, [%o1 + 0x88]	! k[34, 35]
	ROTL128(%o4, %o5, %g2, %g3, 17)
	stx	%o4, [%o1 + 0xb0]	! k[44, 45]
	stx	%o5, [%o1 + 0xb8]	! k[46, 47]
	ROTL128(%o4, %o5, %g2, %g3, 34)
	stx	%o4, [%o1 + 0xf0]	! k[60, 61]
	stx	%o5, [%o1 + 0xf8]	! k[62, 63]
	mov	(4 * 16 * 4), %o0
2:
	add	%o1, %o0, %o1
	ldd	[%o1 + 0x00], %f0
	ldd	[%o1 + 0x08], %f2
	std	%f0, [%o3 + 0x00]
	std	%f2, [%o3 + 0x08]
	add	%o3, 0x10, %o3
1:
	sub	%o1, (16 * 4), %o1
	ldd	[%o1 + 0x38], %f0
	ldd	[%o1 + 0x30], %f2
	ldd	[%o1 + 0x28], %f4
	ldd	[%o1 + 0x20], %f6
	ldd	[%o1 + 0x18], %f8
	ldd	[%o1 + 0x10], %f10
	std	%f0, [%o3 + 0x00]
	std	%f2, [%o3 + 0x08]
	std	%f4, [%o3 + 0x10]
	std	%f6, [%o3 + 0x18]
	std	%f8, [%o3 + 0x20]
	std	%f10, [%o3 + 0x28]

	ldd	[%o1 + 0x08], %f0
	ldd	[%o1 + 0x00], %f2
	std	%f0, [%o3 + 0x30]
	std	%f2, [%o3 + 0x38]
	subcc	%o0, (16 * 4), %o0
	bne,pt	%icc, 1b
	 add	%o3, (16 * 4), %o3

	std	%f2, [%o3 - 0x10]
	std	%f0, [%o3 - 0x08]

	retl
	 VISExit
ENDPROC(camellia_sparc64_key_expand)

	.align	32
ENTRY(camellia_sparc64_crypt)
	/* %o0=key, %o1=input, %o2=output, %o3=key_len */
	VISEntry

	ld	[%o1 + 0x00], %f0
	ld	[%o1 + 0x04], %f1
	ld	[%o1 + 0x08], %f2
	ld	[%o1 + 0x0c], %f3

	ldd	[%o0 + 0x00], %f4
	ldd	[%o0 + 0x08], %f6

	cmp	%o3, 16
	fxor	%f4, %f0, %f0
	be	1f
	 fxor	%f6, %f2, %f2

	ldd	[%o0 + 0x10], %f8
	ldd	[%o0 + 0x18], %f10
	ldd	[%o0 + 0x20], %f12
	ldd	[%o0 + 0x28], %f14
	ldd	[%o0 + 0x30], %f16
	ldd	[%o0 + 0x38], %f18
	ldd	[%o0 + 0x40], %f20
	ldd	[%o0 + 0x48], %f22
	add	%o0, 0x40, %o0

	CAMELLIA_6ROUNDS_FL_FLI( 8, 0, 2)

1:
	ldd	[%o0 + 0x10], %f8
	ldd	[%o0 + 0x18], %f10
	ldd	[%o0 + 0x20], %f12
	ldd	[%o0 + 0x28], %f14
	ldd	[%o0 + 0x30], %f16
	ldd	[%o0 + 0x38], %f18
	ldd	[%o0 + 0x40], %f20
	ldd	[%o0 + 0x48], %f22
	ldd	[%o0 + 0x50], %f24
	ldd	[%o0 + 0x58], %f26
	ldd	[%o0 + 0x60], %f28
	ldd	[%o0 + 0x68], %f30
	ldd	[%o0 + 0x70], %f32
	ldd	[%o0 + 0x78], %f34
	ldd	[%o0 + 0x80], %f36
	ldd	[%o0 + 0x88], %f38
	ldd	[%o0 + 0x90], %f40
	ldd	[%o0 + 0x98], %f42
	ldd	[%o0 + 0xa0], %f44
	ldd	[%o0 + 0xa8], %f46
	ldd	[%o0 + 0xb0], %f48
	ldd	[%o0 + 0xb8], %f50
	ldd	[%o0 + 0xc0], %f52
	ldd	[%o0 + 0xc8], %f54

	CAMELLIA_6ROUNDS_FL_FLI( 8, 0, 2)
	CAMELLIA_6ROUNDS_FL_FLI(24, 0, 2)
	CAMELLIA_6ROUNDS(40, 0, 2)
	fxor	%f52, %f2, %f2
	fxor	%f54, %f0, %f0

	st	%f2, [%o2 + 0x00]
	st	%f3, [%o2 + 0x04]
	st	%f0, [%o2 + 0x08]
	st	%f1, [%o2 + 0x0c]

	retl
	 VISExit
ENDPROC(camellia_sparc64_crypt)

	.align	32
ENTRY(camellia_sparc64_load_keys)
	/* %o0=key, %o1=key_len */
	VISEntry
	ldd	[%o0 + 0x00], %f4
	ldd	[%o0 + 0x08], %f6
	ldd	[%o0 + 0x10], %f8
	ldd	[%o0 + 0x18], %f10
	ldd	[%o0 + 0x20], %f12
	ldd	[%o0 + 0x28], %f14
	ldd	[%o0 + 0x30], %f16
	ldd	[%o0 + 0x38], %f18
	ldd	[%o0 + 0x40], %f20
	ldd	[%o0 + 0x48], %f22
	ldd	[%o0 + 0x50], %f24
	ldd	[%o0 + 0x58], %f26
	ldd	[%o0 + 0x60], %f28
	ldd	[%o0 + 0x68], %f30
	ldd	[%o0 + 0x70], %f32
	ldd	[%o0 + 0x78], %f34
	ldd	[%o0 + 0x80], %f36
	ldd	[%o0 + 0x88], %f38
	ldd	[%o0 + 0x90], %f40
	ldd	[%o0 + 0x98], %f42
	ldd	[%o0 + 0xa0], %f44
	ldd	[%o0 + 0xa8], %f46
	ldd	[%o0 + 0xb0], %f48
	ldd	[%o0 + 0xb8], %f50
	ldd	[%o0 + 0xc0], %f52
	retl
	 ldd	[%o0 + 0xc8], %f54
ENDPROC(camellia_sparc64_load_keys)

	.align	32
ENTRY(camellia_sparc64_ecb_crypt_3_grand_rounds)
	/* %o0=input, %o1=output, %o2=len, %o3=key */
1:	ldd	[%o0 + 0x00], %f0
	ldd	[%o0 + 0x08], %f2
	add	%o0, 0x10, %o0
	fxor	%f4, %f0, %f0
	fxor	%f6, %f2, %f2
	CAMELLIA_6ROUNDS_FL_FLI( 8, 0, 2)
	CAMELLIA_6ROUNDS_FL_FLI(24, 0, 2)
	CAMELLIA_6ROUNDS(40, 0, 2)
	fxor	%f52, %f2, %f2
	fxor	%f54, %f0, %f0
	std	%f2, [%o1 + 0x00]
	std	%f0, [%o1 + 0x08]
	subcc	%o2, 0x10, %o2
	bne,pt	%icc, 1b
	 add	%o1, 0x10, %o1
	retl
	 nop
ENDPROC(camellia_sparc64_ecb_crypt_3_grand_rounds)

	.align	32
ENTRY(camellia_sparc64_ecb_crypt_4_grand_rounds)
	/* %o0=input, %o1=output, %o2=len, %o3=key */
1:	ldd	[%o0 + 0x00], %f0
	ldd	[%o0 + 0x08], %f2
	add	%o0, 0x10, %o0
	fxor	%f4, %f0, %f0
	fxor	%f6, %f2, %f2
	CAMELLIA_6ROUNDS_FL_FLI( 8, 0, 2)
	ldd	[%o3 + 0xd0], %f8
	ldd	[%o3 + 0xd8], %f10
	ldd	[%o3 + 0xe0], %f12
	ldd	[%o3 + 0xe8], %f14
	ldd	[%o3 + 0xf0], %f16
	ldd	[%o3 + 0xf8], %f18
	ldd	[%o3 + 0x100], %f20
	ldd	[%o3 + 0x108], %f22
	CAMELLIA_6ROUNDS_FL_FLI(24, 0, 2)
	CAMELLIA_6ROUNDS_FL_FLI(40, 0, 2)
	CAMELLIA_F(8, 2, 0, 2)
	CAMELLIA_F(10, 0, 2, 0)
	ldd	[%o3 + 0x10], %f8
	ldd	[%o3 + 0x18], %f10
	CAMELLIA_F(12, 2, 0, 2)
	CAMELLIA_F(14, 0, 2, 0)
	ldd	[%o3 + 0x20], %f12
	ldd	[%o3 + 0x28], %f14
	CAMELLIA_F(16, 2, 0, 2)
	CAMELLIA_F(18, 0, 2, 0)
	ldd	[%o3 + 0x30], %f16
	ldd	[%o3 + 0x38], %f18
	fxor	%f20, %f2, %f2
	fxor	%f22, %f0, %f0
	ldd	[%o3 + 0x40], %f20
	ldd	[%o3 + 0x48], %f22
	std	%f2, [%o1 + 0x00]
	std	%f0, [%o1 + 0x08]
	subcc	%o2, 0x10, %o2
	bne,pt	%icc, 1b
	 add	%o1, 0x10, %o1
	retl
	 nop
ENDPROC(camellia_sparc64_ecb_crypt_4_grand_rounds)

	.align	32
ENTRY(camellia_sparc64_cbc_encrypt_3_grand_rounds)
	/* %o0=input, %o1=output, %o2=len, %o3=key, %o4=IV */
	ldd	[%o4 + 0x00], %f60
	ldd	[%o4 + 0x08], %f62
1:	ldd	[%o0 + 0x00], %f0
	ldd	[%o0 + 0x08], %f2
	add	%o0, 0x10, %o0
	fxor	%f60, %f0, %f0
	fxor	%f62, %f2, %f2
	fxor	%f4, %f0, %f0
	fxor	%f6, %f2, %f2
	CAMELLIA_6ROUNDS_FL_FLI( 8, 0, 2)
	CAMELLIA_6ROUNDS_FL_FLI(24, 0, 2)
	CAMELLIA_6ROUNDS(40, 0, 2)
	fxor	%f52, %f2, %f60
	fxor	%f54, %f0, %f62
	std	%f60, [%o1 + 0x00]
	std	%f62, [%o1 + 0x08]
	subcc	%o2, 0x10, %o2
	bne,pt	%icc, 1b
	 add	%o1, 0x10, %o1
	std	%f60, [%o4 + 0x00]
	retl
	 std	%f62, [%o4 + 0x08]
ENDPROC(camellia_sparc64_cbc_encrypt_3_grand_rounds)

	.align	32
ENTRY(camellia_sparc64_cbc_encrypt_4_grand_rounds)
	/* %o0=input, %o1=output, %o2=len, %o3=key, %o4=IV */
	ldd	[%o4 + 0x00], %f60
	ldd	[%o4 + 0x08], %f62
1:	ldd	[%o0 + 0x00], %f0
	ldd	[%o0 + 0x08], %f2
	add	%o0, 0x10, %o0
	fxor	%f60, %f0, %f0
	fxor	%f62, %f2, %f2
	fxor	%f4, %f0, %f0
	fxor	%f6, %f2, %f2
	CAMELLIA_6ROUNDS_FL_FLI( 8, 0, 2)
	ldd	[%o3 + 0xd0], %f8
	ldd	[%o3 + 0xd8], %f10
	ldd	[%o3 + 0xe0], %f12
	ldd	[%o3 + 0xe8], %f14
	ldd	[%o3 + 0xf0], %f16
	ldd	[%o3 + 0xf8], %f18
	ldd	[%o3 + 0x100], %f20
	ldd	[%o3 + 0x108], %f22
	CAMELLIA_6ROUNDS_FL_FLI(24, 0, 2)
	CAMELLIA_6ROUNDS_FL_FLI(40, 0, 2)
	CAMELLIA_F(8, 2, 0, 2)
	CAMELLIA_F(10, 0, 2, 0)
	ldd	[%o3 + 0x10], %f8
	ldd	[%o3 + 0x18], %f10
	CAMELLIA_F(12, 2, 0, 2)
	CAMELLIA_F(14, 0, 2, 0)
	ldd	[%o3 + 0x20], %f12
	ldd	[%o3 + 0x28], %f14
	CAMELLIA_F(16, 2, 0, 2)
	CAMELLIA_F(18, 0, 2, 0)
	ldd	[%o3 + 0x30], %f16
	ldd	[%o3 + 0x38], %f18
	fxor	%f20, %f2, %f60
	fxor	%f22, %f0, %f62
	ldd	[%o3 + 0x40], %f20
	ldd	[%o3 + 0x48], %f22
	std	%f60, [%o1 + 0x00]
	std	%f62, [%o1 + 0x08]
	subcc	%o2, 0x10, %o2
	bne,pt	%icc, 1b
	 add	%o1, 0x10, %o1
	std	%f60, [%o4 + 0x00]
	retl
	 std	%f62, [%o4 + 0x08]
ENDPROC(camellia_sparc64_cbc_encrypt_4_grand_rounds)

	.align	32
ENTRY(camellia_sparc64_cbc_decrypt_3_grand_rounds)
	/* %o0=input, %o1=output, %o2=len, %o3=key, %o4=IV */
	ldd	[%o4 + 0x00], %f60
	ldd	[%o4 + 0x08], %f62
1:	ldd	[%o0 + 0x00], %f56
	ldd	[%o0 + 0x08], %f58
	add	%o0, 0x10, %o0
	fxor	%f4, %f56, %f0
	fxor	%f6, %f58, %f2
	CAMELLIA_6ROUNDS_FL_FLI( 8, 0, 2)
	CAMELLIA_6ROUNDS_FL_FLI(24, 0, 2)
	CAMELLIA_6ROUNDS(40, 0, 2)
	fxor	%f52, %f2, %f2
	fxor	%f54, %f0, %f0
	fxor	%f60, %f2, %f2
	fxor	%f62, %f0, %f0
	fsrc2	%f56, %f60
	fsrc2	%f58, %f62
	std	%f2, [%o1 + 0x00]
	std	%f0, [%o1 + 0x08]
	subcc	%o2, 0x10, %o2
	bne,pt	%icc, 1b
	 add	%o1, 0x10, %o1
	std	%f60, [%o4 + 0x00]
	retl
	 std	%f62, [%o4 + 0x08]
ENDPROC(camellia_sparc64_cbc_decrypt_3_grand_rounds)

	.align	32
ENTRY(camellia_sparc64_cbc_decrypt_4_grand_rounds)
	/* %o0=input, %o1=output, %o2=len, %o3=key, %o4=IV */
	ldd	[%o4 + 0x00], %f60
	ldd	[%o4 + 0x08], %f62
1:	ldd	[%o0 + 0x00], %f56
	ldd	[%o0 + 0x08], %f58
	add	%o0, 0x10, %o0
	fxor	%f4, %f56, %f0
	fxor	%f6, %f58, %f2
	CAMELLIA_6ROUNDS_FL_FLI( 8, 0, 2)
	ldd	[%o3 + 0xd0], %f8
	ldd	[%o3 + 0xd8], %f10
	ldd	[%o3 + 0xe0], %f12
	ldd	[%o3 + 0xe8], %f14
	ldd	[%o3 + 0xf0], %f16
	ldd	[%o3 + 0xf8], %f18
	ldd	[%o3 + 0x100], %f20
	ldd	[%o3 + 0x108], %f22
	CAMELLIA_6ROUNDS_FL_FLI(24, 0, 2)
	CAMELLIA_6ROUNDS_FL_FLI(40, 0, 2)
	CAMELLIA_F(8, 2, 0, 2)
	CAMELLIA_F(10, 0, 2, 0)
	ldd	[%o3 + 0x10], %f8
	ldd	[%o3 + 0x18], %f10
	CAMELLIA_F(12, 2, 0, 2)
	CAMELLIA_F(14, 0, 2, 0)
	ldd	[%o3 + 0x20], %f12
	ldd	[%o3 + 0x28], %f14
	CAMELLIA_F(16, 2, 0, 2)
	CAMELLIA_F(18, 0, 2, 0)
	ldd	[%o3 + 0x30], %f16
	ldd	[%o3 + 0x38], %f18
	fxor	%f20, %f2, %f2
	fxor	%f22, %f0, %f0
	ldd	[%o3 + 0x40], %f20
	ldd	[%o3 + 0x48], %f22
	fxor	%f60, %f2, %f2
	fxor	%f62, %f0, %f0
	fsrc2	%f56, %f60
	fsrc2	%f58, %f62
	std	%f2, [%o1 + 0x00]
	std	%f0, [%o1 + 0x08]
	subcc	%o2, 0x10, %o2
	bne,pt	%icc, 1b
	 add	%o1, 0x10, %o1
	std	%f60, [%o4 + 0x00]
	retl
	 std	%f62, [%o4 + 0x08]
ENDPROC(camellia_sparc64_cbc_decrypt_4_grand_rounds)
