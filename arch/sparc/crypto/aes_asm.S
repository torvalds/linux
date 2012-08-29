#include <linux/linkage.h>
#include <asm/visasm.h>

#define F3F(x,y,z)	(((x)<<30)|((y)<<19)|((z)<<5))

#define FPD_ENCODE(x)	(((x) >> 5) | ((x) & ~(0x20)))

#define RS1(x)		(FPD_ENCODE(x) << 14)
#define RS2(x)		(FPD_ENCODE(x) <<  0)
#define RS3(x)		(FPD_ENCODE(x) <<  9)
#define RD(x)		(FPD_ENCODE(x) << 25)
#define IMM5(x)		((x)           <<  9)

#define AES_EROUND01(a,b,c,d)	\
	.word	(F3F(2, 0x19, 0)|RS1(a)|RS2(b)|RS3(c)|RD(d));
#define AES_EROUND23(a,b,c,d)	\
	.word	(F3F(2, 0x19, 1)|RS1(a)|RS2(b)|RS3(c)|RD(d));
#define AES_DROUND01(a,b,c,d)	\
	.word	(F3F(2, 0x19, 2)|RS1(a)|RS2(b)|RS3(c)|RD(d));
#define AES_DROUND23(a,b,c,d)	\
	.word	(F3F(2, 0x19, 3)|RS1(a)|RS2(b)|RS3(c)|RD(d));
#define AES_EROUND01_L(a,b,c,d)	\
	.word	(F3F(2, 0x19, 4)|RS1(a)|RS2(b)|RS3(c)|RD(d));
#define AES_EROUND23_L(a,b,c,d)	\
	.word	(F3F(2, 0x19, 5)|RS1(a)|RS2(b)|RS3(c)|RD(d));
#define AES_DROUND01_L(a,b,c,d)	\
	.word	(F3F(2, 0x19, 6)|RS1(a)|RS2(b)|RS3(c)|RD(d));
#define AES_DROUND23_L(a,b,c,d)	\
	.word	(F3F(2, 0x19, 7)|RS1(a)|RS2(b)|RS3(c)|RD(d));
#define AES_KEXPAND1(a,b,c,d)	\
	.word	(F3F(2, 0x19, 8)|RS1(a)|RS2(b)|IMM5(c)|RD(d));
#define AES_KEXPAND0(a,b,c)	\
	.word	(F3F(2, 0x36, 0x130)|RS1(a)|RS2(b)|RD(c));
#define AES_KEXPAND2(a,b,c)	\
	.word	(F3F(2, 0x36, 0x131)|RS1(a)|RS2(b)|RD(c));

#define MOVXTOD_G3_F4		\
	.word	0x89b02303;
#define MOVXTOD_G7_F6		\
	.word	0x8db02307;
#define MOVXTOD_G3_F0		\
	.word	0x81b02303;
#define MOVXTOD_G7_F2		\
	.word	0x85b02307;
#define MOVXTOD_O0_F0		\
	.word	0x81b02308;
#define MOVXTOD_O5_F0		\
	.word	0x81b0230d;
#define MOVXTOD_O5_F2		\
	.word	0x85b0230d;

#define ENCRYPT_TWO_ROUNDS(KEY_BASE, I0, I1, T0, T1) \
	AES_EROUND01(KEY_BASE +  0, I0, I1, T0) \
	AES_EROUND23(KEY_BASE +  2, I0, I1, T1) \
	AES_EROUND01(KEY_BASE +  4, T0, T1, I0) \
	AES_EROUND23(KEY_BASE +  6, T0, T1, I1)

#define ENCRYPT_TWO_ROUNDS_LAST(KEY_BASE, I0, I1, T0, T1) \
	AES_EROUND01(KEY_BASE +  0, I0, I1, T0) \
	AES_EROUND23(KEY_BASE +  2, I0, I1, T1) \
	AES_EROUND01_L(KEY_BASE +  4, T0, T1, I0) \
	AES_EROUND23_L(KEY_BASE +  6, T0, T1, I1)

	/* 10 rounds */
#define ENCRYPT_128(KEY_BASE, I0, I1, T0, T1) \
	ENCRYPT_TWO_ROUNDS(KEY_BASE +  0, I0, I1, T0, T1) \
	ENCRYPT_TWO_ROUNDS(KEY_BASE +  8, I0, I1, T0, T1) \
	ENCRYPT_TWO_ROUNDS(KEY_BASE + 16, I0, I1, T0, T1) \
	ENCRYPT_TWO_ROUNDS(KEY_BASE + 24, I0, I1, T0, T1) \
	ENCRYPT_TWO_ROUNDS_LAST(KEY_BASE + 32, I0, I1, T0, T1)

	/* 12 rounds */
#define ENCRYPT_192(KEY_BASE, I0, I1, T0, T1) \
	ENCRYPT_TWO_ROUNDS(KEY_BASE +  0, I0, I1, T0, T1) \
	ENCRYPT_TWO_ROUNDS(KEY_BASE +  8, I0, I1, T0, T1) \
	ENCRYPT_TWO_ROUNDS(KEY_BASE + 16, I0, I1, T0, T1) \
	ENCRYPT_TWO_ROUNDS(KEY_BASE + 24, I0, I1, T0, T1) \
	ENCRYPT_TWO_ROUNDS(KEY_BASE + 32, I0, I1, T0, T1) \
	ENCRYPT_TWO_ROUNDS_LAST(KEY_BASE + 40, I0, I1, T0, T1)

	/* 14 rounds */
#define ENCRYPT_256(KEY_BASE, I0, I1, T0, T1) \
	ENCRYPT_TWO_ROUNDS(KEY_BASE +  0, I0, I1, T0, T1) \
	ENCRYPT_TWO_ROUNDS(KEY_BASE +  8, I0, I1, T0, T1) \
	ENCRYPT_TWO_ROUNDS(KEY_BASE + 16, I0, I1, T0, T1) \
	ENCRYPT_TWO_ROUNDS(KEY_BASE + 24, I0, I1, T0, T1) \
	ENCRYPT_TWO_ROUNDS(KEY_BASE + 32, I0, I1, T0, T1) \
	ENCRYPT_TWO_ROUNDS(KEY_BASE + 40, I0, I1, T0, T1) \
	ENCRYPT_TWO_ROUNDS_LAST(KEY_BASE + 48, I0, I1, T0, T1)

#define DECRYPT_TWO_ROUNDS(KEY_BASE, I0, I1, T0, T1) \
	AES_DROUND23(KEY_BASE +  0, I0, I1, T1) \
	AES_DROUND01(KEY_BASE +  2, I0, I1, T0) \
	AES_DROUND23(KEY_BASE +  4, T0, T1, I1) \
	AES_DROUND01(KEY_BASE +  6, T0, T1, I0)

#define DECRYPT_TWO_ROUNDS_LAST(KEY_BASE, I0, I1, T0, T1) \
	AES_DROUND23(KEY_BASE +  0, I0, I1, T1) \
	AES_DROUND01(KEY_BASE +  2, I0, I1, T0) \
	AES_DROUND23_L(KEY_BASE +  4, T0, T1, I1) \
	AES_DROUND01_L(KEY_BASE +  6, T0, T1, I0)

	/* 10 rounds */
#define DECRYPT_128(KEY_BASE, I0, I1, T0, T1) \
	DECRYPT_TWO_ROUNDS(KEY_BASE +  0, I0, I1, T0, T1) \
	DECRYPT_TWO_ROUNDS(KEY_BASE +  8, I0, I1, T0, T1) \
	DECRYPT_TWO_ROUNDS(KEY_BASE + 16, I0, I1, T0, T1) \
	DECRYPT_TWO_ROUNDS(KEY_BASE + 24, I0, I1, T0, T1) \
	DECRYPT_TWO_ROUNDS_LAST(KEY_BASE + 32, I0, I1, T0, T1)

	/* 12 rounds */
#define DECRYPT_192(KEY_BASE, I0, I1, T0, T1) \
	DECRYPT_TWO_ROUNDS(KEY_BASE +  0, I0, I1, T0, T1) \
	DECRYPT_TWO_ROUNDS(KEY_BASE +  8, I0, I1, T0, T1) \
	DECRYPT_TWO_ROUNDS(KEY_BASE + 16, I0, I1, T0, T1) \
	DECRYPT_TWO_ROUNDS(KEY_BASE + 24, I0, I1, T0, T1) \
	DECRYPT_TWO_ROUNDS(KEY_BASE + 32, I0, I1, T0, T1) \
	DECRYPT_TWO_ROUNDS_LAST(KEY_BASE + 40, I0, I1, T0, T1)

	/* 14 rounds */
#define DECRYPT_256(KEY_BASE, I0, I1, T0, T1) \
	DECRYPT_TWO_ROUNDS(KEY_BASE +  0, I0, I1, T0, T1) \
	DECRYPT_TWO_ROUNDS(KEY_BASE +  8, I0, I1, T0, T1) \
	DECRYPT_TWO_ROUNDS(KEY_BASE + 16, I0, I1, T0, T1) \
	DECRYPT_TWO_ROUNDS(KEY_BASE + 24, I0, I1, T0, T1) \
	DECRYPT_TWO_ROUNDS(KEY_BASE + 32, I0, I1, T0, T1) \
	DECRYPT_TWO_ROUNDS(KEY_BASE + 40, I0, I1, T0, T1) \
	DECRYPT_TWO_ROUNDS_LAST(KEY_BASE + 48, I0, I1, T0, T1)

	.align	32
ENTRY(aes_sparc64_key_expand)
	/* %o0=input_key, %o1=output_key, %o2=key_len */
	VISEntry
	ld	[%o0 + 0x00], %f0
	ld	[%o0 + 0x04], %f1
	ld	[%o0 + 0x08], %f2
	ld	[%o0 + 0x0c], %f3

	std	%f0, [%o1 + 0x00]
	std	%f2, [%o1 + 0x08]
	add	%o1, 0x10, %o1

	cmp	%o2, 24
	bl	2f
	 nop

	be	1f
	 nop

	/* 256-bit key expansion */
	ld	[%o0 + 0x10], %f4
	ld	[%o0 + 0x14], %f5
	ld	[%o0 + 0x18], %f6
	ld	[%o0 + 0x1c], %f7

	std	%f4, [%o1 + 0x00]
	std	%f6, [%o1 + 0x08]
	add	%o1, 0x10, %o1

	AES_KEXPAND1(0, 6, 0x0, 8)
	AES_KEXPAND2(2, 8, 10)
	AES_KEXPAND0(4, 10, 12)
	AES_KEXPAND2(6, 12, 14)
	AES_KEXPAND1(8, 14, 0x1, 16)
	AES_KEXPAND2(10, 16, 18)
	AES_KEXPAND0(12, 18, 20)
	AES_KEXPAND2(14, 20, 22)
	AES_KEXPAND1(16, 22, 0x2, 24)
	AES_KEXPAND2(18, 24, 26)
	AES_KEXPAND0(20, 26, 28)
	AES_KEXPAND2(22, 28, 30)
	AES_KEXPAND1(24, 30, 0x3, 32)
	AES_KEXPAND2(26, 32, 34)
	AES_KEXPAND0(28, 34, 36)
	AES_KEXPAND2(30, 36, 38)
	AES_KEXPAND1(32, 38, 0x4, 40)
	AES_KEXPAND2(34, 40, 42)
	AES_KEXPAND0(36, 42, 44)
	AES_KEXPAND2(38, 44, 46)
	AES_KEXPAND1(40, 46, 0x5, 48)
	AES_KEXPAND2(42, 48, 50)
	AES_KEXPAND0(44, 50, 52)
	AES_KEXPAND2(46, 52, 54)
	AES_KEXPAND1(48, 54, 0x6, 56)
	AES_KEXPAND2(50, 56, 58)

	std	%f8, [%o1 + 0x00]
	std	%f10, [%o1 + 0x08]
	std	%f12, [%o1 + 0x10]
	std	%f14, [%o1 + 0x18]
	std	%f16, [%o1 + 0x20]
	std	%f18, [%o1 + 0x28]
	std	%f20, [%o1 + 0x30]
	std	%f22, [%o1 + 0x38]
	std	%f24, [%o1 + 0x40]
	std	%f26, [%o1 + 0x48]
	std	%f28, [%o1 + 0x50]
	std	%f30, [%o1 + 0x58]
	std	%f32, [%o1 + 0x60]
	std	%f34, [%o1 + 0x68]
	std	%f36, [%o1 + 0x70]
	std	%f38, [%o1 + 0x78]
	std	%f40, [%o1 + 0x80]
	std	%f42, [%o1 + 0x88]
	std	%f44, [%o1 + 0x90]
	std	%f46, [%o1 + 0x98]
	std	%f48, [%o1 + 0xa0]
	std	%f50, [%o1 + 0xa8]
	std	%f52, [%o1 + 0xb0]
	std	%f54, [%o1 + 0xb8]
	std	%f56, [%o1 + 0xc0]
	ba,pt	%xcc, 80f
	 std	%f58, [%o1 + 0xc8]

1:	
	/* 192-bit key expansion */
	ld	[%o0 + 0x10], %f4
	ld	[%o0 + 0x14], %f5

	std	%f4, [%o1 + 0x00]
	add	%o1, 0x08, %o1

	AES_KEXPAND1(0, 4, 0x0, 6)
	AES_KEXPAND2(2, 6, 8)
	AES_KEXPAND2(4, 8, 10)
	AES_KEXPAND1(6, 10, 0x1, 12)
	AES_KEXPAND2(8, 12, 14)
	AES_KEXPAND2(10, 14, 16)
	AES_KEXPAND1(12, 16, 0x2, 18)
	AES_KEXPAND2(14, 18, 20)
	AES_KEXPAND2(16, 20, 22)
	AES_KEXPAND1(18, 22, 0x3, 24)
	AES_KEXPAND2(20, 24, 26)
	AES_KEXPAND2(22, 26, 28)
	AES_KEXPAND1(24, 28, 0x4, 30)
	AES_KEXPAND2(26, 30, 32)
	AES_KEXPAND2(28, 32, 34)
	AES_KEXPAND1(30, 34, 0x5, 36)
	AES_KEXPAND2(32, 36, 38)
	AES_KEXPAND2(34, 38, 40)
	AES_KEXPAND1(36, 40, 0x6, 42)
	AES_KEXPAND2(38, 42, 44)
	AES_KEXPAND2(40, 44, 46)
	AES_KEXPAND1(42, 46, 0x7, 48)
	AES_KEXPAND2(44, 48, 50)

	std	%f6, [%o1 + 0x00]
	std	%f8, [%o1 + 0x08]
	std	%f10, [%o1 + 0x10]
	std	%f12, [%o1 + 0x18]
	std	%f14, [%o1 + 0x20]
	std	%f16, [%o1 + 0x28]
	std	%f18, [%o1 + 0x30]
	std	%f20, [%o1 + 0x38]
	std	%f22, [%o1 + 0x40]
	std	%f24, [%o1 + 0x48]
	std	%f26, [%o1 + 0x50]
	std	%f28, [%o1 + 0x58]
	std	%f30, [%o1 + 0x60]
	std	%f32, [%o1 + 0x68]
	std	%f34, [%o1 + 0x70]
	std	%f36, [%o1 + 0x78]
	std	%f38, [%o1 + 0x80]
	std	%f40, [%o1 + 0x88]
	std	%f42, [%o1 + 0x90]
	std	%f44, [%o1 + 0x98]
	std	%f46, [%o1 + 0xa0]
	std	%f48, [%o1 + 0xa8]
	ba,pt	%xcc, 80f
	 std	%f50, [%o1 + 0xb0]

2:
	/* 128-bit key expansion */
	AES_KEXPAND1(0, 2, 0x0, 4)
	AES_KEXPAND2(2, 4, 6)
	AES_KEXPAND1(4, 6, 0x1, 8)
	AES_KEXPAND2(6, 8, 10)
	AES_KEXPAND1(8, 10, 0x2, 12)
	AES_KEXPAND2(10, 12, 14)
	AES_KEXPAND1(12, 14, 0x3, 16)
	AES_KEXPAND2(14, 16, 18)
	AES_KEXPAND1(16, 18, 0x4, 20)
	AES_KEXPAND2(18, 20, 22)
	AES_KEXPAND1(20, 22, 0x5, 24)
	AES_KEXPAND2(22, 24, 26)
	AES_KEXPAND1(24, 26, 0x6, 28)
	AES_KEXPAND2(26, 28, 30)
	AES_KEXPAND1(28, 30, 0x7, 32)
	AES_KEXPAND2(30, 32, 34)
	AES_KEXPAND1(32, 34, 0x8, 36)
	AES_KEXPAND2(34, 36, 38)
	AES_KEXPAND1(36, 38, 0x9, 40)
	AES_KEXPAND2(38, 40, 42)

	std	%f4, [%o1 + 0x00]
	std	%f6, [%o1 + 0x08]
	std	%f8, [%o1 + 0x10]
	std	%f10, [%o1 + 0x18]
	std	%f12, [%o1 + 0x20]
	std	%f14, [%o1 + 0x28]
	std	%f16, [%o1 + 0x30]
	std	%f18, [%o1 + 0x38]
	std	%f20, [%o1 + 0x40]
	std	%f22, [%o1 + 0x48]
	std	%f24, [%o1 + 0x50]
	std	%f26, [%o1 + 0x58]
	std	%f28, [%o1 + 0x60]
	std	%f30, [%o1 + 0x68]
	std	%f32, [%o1 + 0x70]
	std	%f34, [%o1 + 0x78]
	std	%f36, [%o1 + 0x80]
	std	%f38, [%o1 + 0x88]
	std	%f40, [%o1 + 0x90]
	std	%f42, [%o1 + 0x98]
80:
	retl
	 VISExit
ENDPROC(aes_sparc64_key_expand)

	.align		32
ENTRY(aes_sparc64_encrypt_128)
	/* %o0=key, %o1=input, %o2=output */
	VISEntry
	ld		[%o1 + 0x00], %f4
	ld		[%o1 + 0x04], %f5
	ld		[%o1 + 0x08], %f6
	ld		[%o1 + 0x0c], %f7
	ldd		[%o0 + 0x00], %f8
	ldd		[%o0 + 0x08], %f10
	ldd		[%o0 + 0x10], %f12
	ldd		[%o0 + 0x18], %f14
	ldd		[%o0 + 0x20], %f16
	ldd		[%o0 + 0x28], %f18
	ldd		[%o0 + 0x30], %f20
	ldd		[%o0 + 0x38], %f22
	ldd		[%o0 + 0x40], %f24
	ldd		[%o0 + 0x48], %f26
	ldd		[%o0 + 0x50], %f28
	ldd		[%o0 + 0x58], %f30
	ldd		[%o0 + 0x60], %f32
	ldd		[%o0 + 0x68], %f34
	ldd		[%o0 + 0x70], %f36
	ldd		[%o0 + 0x78], %f38
	ldd		[%o0 + 0x80], %f40
	ldd		[%o0 + 0x88], %f42
	ldd		[%o0 + 0x90], %f44
	ldd		[%o0 + 0x98], %f46
	ldd		[%o0 + 0xa0], %f48
	ldd		[%o0 + 0xa8], %f50
	fxor		%f8, %f4, %f4
	fxor		%f10, %f6, %f6
	ENCRYPT_128(12, 4, 6, 0, 2)
	st		%f4, [%o2 + 0x00]
	st		%f5, [%o2 + 0x04]
	st		%f6, [%o2 + 0x08]
	st		%f7, [%o2 + 0x0c]
	retl
	 VISExit
ENDPROC(aes_sparc64_encrypt_128)

	.align		32
ENTRY(aes_sparc64_encrypt_192)
	/* %o0=key, %o1=input, %o2=output */
	VISEntry
	ld		[%o1 + 0x00], %f4
	ld		[%o1 + 0x04], %f5
	ld		[%o1 + 0x08], %f6
	ld		[%o1 + 0x0c], %f7

	ldd		[%o0 + 0x00], %f8
	ldd		[%o0 + 0x08], %f10

	fxor		%f8, %f4, %f4
	fxor		%f10, %f6, %f6

	ldd		[%o0 + 0x10], %f8
	ldd		[%o0 + 0x18], %f10
	ldd		[%o0 + 0x20], %f12
	ldd		[%o0 + 0x28], %f14
	add		%o0, 0x20, %o0

	ENCRYPT_TWO_ROUNDS(8, 4, 6, 0, 2)

	ldd		[%o0 + 0x10], %f12
	ldd		[%o0 + 0x18], %f14
	ldd		[%o0 + 0x20], %f16
	ldd		[%o0 + 0x28], %f18
	ldd		[%o0 + 0x30], %f20
	ldd		[%o0 + 0x38], %f22
	ldd		[%o0 + 0x40], %f24
	ldd		[%o0 + 0x48], %f26
	ldd		[%o0 + 0x50], %f28
	ldd		[%o0 + 0x58], %f30
	ldd		[%o0 + 0x60], %f32
	ldd		[%o0 + 0x68], %f34
	ldd		[%o0 + 0x70], %f36
	ldd		[%o0 + 0x78], %f38
	ldd		[%o0 + 0x80], %f40
	ldd		[%o0 + 0x88], %f42
	ldd		[%o0 + 0x90], %f44
	ldd		[%o0 + 0x98], %f46
	ldd		[%o0 + 0xa0], %f48
	ldd		[%o0 + 0xa8], %f50


	ENCRYPT_128(12, 4, 6, 0, 2)

	st		%f4, [%o2 + 0x00]
	st		%f5, [%o2 + 0x04]
	st		%f6, [%o2 + 0x08]
	st		%f7, [%o2 + 0x0c]

	retl
	 VISExit
ENDPROC(aes_sparc64_encrypt_192)

	.align		32
ENTRY(aes_sparc64_encrypt_256)
	/* %o0=key, %o1=input, %o2=output */
	VISEntry
	ld		[%o1 + 0x00], %f4
	ld		[%o1 + 0x04], %f5
	ld		[%o1 + 0x08], %f6
	ld		[%o1 + 0x0c], %f7

	ldd		[%o0 + 0x00], %f8
	ldd		[%o0 + 0x08], %f10

	fxor		%f8, %f4, %f4
	fxor		%f10, %f6, %f6

	ldd		[%o0 + 0x10], %f8

	ldd		[%o0 + 0x18], %f10
	ldd		[%o0 + 0x20], %f12
	ldd		[%o0 + 0x28], %f14
	add		%o0, 0x20, %o0

	ENCRYPT_TWO_ROUNDS(8, 4, 6, 0, 2)

	ldd		[%o0 + 0x10], %f8

	ldd		[%o0 + 0x18], %f10
	ldd		[%o0 + 0x20], %f12
	ldd		[%o0 + 0x28], %f14
	add		%o0, 0x20, %o0

	ENCRYPT_TWO_ROUNDS(8, 4, 6, 0, 2)

	ldd		[%o0 + 0x10], %f12
	ldd		[%o0 + 0x18], %f14
	ldd		[%o0 + 0x20], %f16
	ldd		[%o0 + 0x28], %f18
	ldd		[%o0 + 0x30], %f20
	ldd		[%o0 + 0x38], %f22
	ldd		[%o0 + 0x40], %f24
	ldd		[%o0 + 0x48], %f26
	ldd		[%o0 + 0x50], %f28
	ldd		[%o0 + 0x58], %f30
	ldd		[%o0 + 0x60], %f32
	ldd		[%o0 + 0x68], %f34
	ldd		[%o0 + 0x70], %f36
	ldd		[%o0 + 0x78], %f38
	ldd		[%o0 + 0x80], %f40
	ldd		[%o0 + 0x88], %f42
	ldd		[%o0 + 0x90], %f44
	ldd		[%o0 + 0x98], %f46
	ldd		[%o0 + 0xa0], %f48
	ldd		[%o0 + 0xa8], %f50

	ENCRYPT_128(12, 4, 6, 0, 2)

	st		%f4, [%o2 + 0x00]
	st		%f5, [%o2 + 0x04]
	st		%f6, [%o2 + 0x08]
	st		%f7, [%o2 + 0x0c]

	retl
	 VISExit
ENDPROC(aes_sparc64_encrypt_256)

	.align		32
ENTRY(aes_sparc64_decrypt_128)
	/* %o0=key, %o1=input, %o2=output */
	VISEntry
	ld		[%o1 + 0x00], %f4
	ld		[%o1 + 0x04], %f5
	ld		[%o1 + 0x08], %f6
	ld		[%o1 + 0x0c], %f7
	ldd		[%o0 + 0xa0], %f8
	ldd		[%o0 + 0xa8], %f10
	ldd		[%o0 + 0x98], %f12
	ldd		[%o0 + 0x90], %f14
	ldd		[%o0 + 0x88], %f16
	ldd		[%o0 + 0x80], %f18
	ldd		[%o0 + 0x78], %f20
	ldd		[%o0 + 0x70], %f22
	ldd		[%o0 + 0x68], %f24
	ldd		[%o0 + 0x60], %f26
	ldd		[%o0 + 0x58], %f28
	ldd		[%o0 + 0x50], %f30
	ldd		[%o0 + 0x48], %f32
	ldd		[%o0 + 0x40], %f34
	ldd		[%o0 + 0x38], %f36
	ldd		[%o0 + 0x30], %f38
	ldd		[%o0 + 0x28], %f40
	ldd		[%o0 + 0x20], %f42
	ldd		[%o0 + 0x18], %f44
	ldd		[%o0 + 0x10], %f46
	ldd		[%o0 + 0x08], %f48
	ldd		[%o0 + 0x00], %f50
	fxor		%f8, %f4, %f4
	fxor		%f10, %f6, %f6
	DECRYPT_128(12, 4, 6, 0, 2)
	st		%f4, [%o2 + 0x00]
	st		%f5, [%o2 + 0x04]
	st		%f6, [%o2 + 0x08]
	st		%f7, [%o2 + 0x0c]
	retl
	 VISExit
ENDPROC(aes_sparc64_decrypt_128)

	.align		32
ENTRY(aes_sparc64_decrypt_192)
	/* %o0=key, %o1=input, %o2=output */
	VISEntry
	ld		[%o1 + 0x00], %f4
	ld		[%o1 + 0x04], %f5
	ld		[%o1 + 0x08], %f6
	ld		[%o1 + 0x0c], %f7
	ldd		[%o0 + 0xc0], %f8
	ldd		[%o0 + 0xc8], %f10
	ldd		[%o0 + 0xb8], %f12
	ldd		[%o0 + 0xb0], %f14
	ldd		[%o0 + 0xa8], %f16
	ldd		[%o0 + 0xa0], %f18
	fxor		%f8, %f4, %f4
	fxor		%f10, %f6, %f6
	ldd		[%o0 + 0x98], %f20
	ldd		[%o0 + 0x90], %f22
	ldd		[%o0 + 0x88], %f24
	ldd		[%o0 + 0x80], %f26
	DECRYPT_TWO_ROUNDS(12, 4, 6, 0, 2)
	ldd		[%o0 + 0x78], %f28
	ldd		[%o0 + 0x70], %f30
	ldd		[%o0 + 0x68], %f32
	ldd		[%o0 + 0x60], %f34
	ldd		[%o0 + 0x58], %f36
	ldd		[%o0 + 0x50], %f38
	ldd		[%o0 + 0x48], %f40
	ldd		[%o0 + 0x40], %f42
	ldd		[%o0 + 0x38], %f44
	ldd		[%o0 + 0x30], %f46
	ldd		[%o0 + 0x28], %f48
	ldd		[%o0 + 0x20], %f50
	ldd		[%o0 + 0x18], %f52
	ldd		[%o0 + 0x10], %f54
	ldd		[%o0 + 0x08], %f56
	ldd		[%o0 + 0x00], %f58
	DECRYPT_128(20, 4, 6, 0, 2)
	st		%f4, [%o2 + 0x00]
	st		%f5, [%o2 + 0x04]
	st		%f6, [%o2 + 0x08]
	st		%f7, [%o2 + 0x0c]
	retl
	 VISExit
ENDPROC(aes_sparc64_decrypt_192)

	.align		32
ENTRY(aes_sparc64_decrypt_256)
	/* %o0=key, %o1=input, %o2=output */
	VISEntry
	ld		[%o1 + 0x00], %f4
	ld		[%o1 + 0x04], %f5
	ld		[%o1 + 0x08], %f6
	ld		[%o1 + 0x0c], %f7
	ldd		[%o0 + 0xe0], %f8
	ldd		[%o0 + 0xe8], %f10
	ldd		[%o0 + 0xd8], %f12
	ldd		[%o0 + 0xd0], %f14
	ldd		[%o0 + 0xc8], %f16
	fxor		%f8, %f4, %f4
	ldd		[%o0 + 0xc0], %f18
	fxor		%f10, %f6, %f6
	ldd		[%o0 + 0xb8], %f20
	AES_DROUND23(12, 4, 6, 2)
	ldd		[%o0 + 0xb0], %f22
	AES_DROUND01(14, 4, 6, 0)
	ldd		[%o0 + 0xa8], %f24
	AES_DROUND23(16, 0, 2, 6)
	ldd		[%o0 + 0xa0], %f26
	AES_DROUND01(18, 0, 2, 4)
	ldd		[%o0 + 0x98], %f12
	AES_DROUND23(20, 4, 6, 2)
	ldd		[%o0 + 0x90], %f14
	AES_DROUND01(22, 4, 6, 0)
	ldd		[%o0 + 0x88], %f16
	AES_DROUND23(24, 0, 2, 6)
	ldd		[%o0 + 0x80], %f18
	AES_DROUND01(26, 0, 2, 4)
	ldd		[%o0 + 0x78], %f20
	AES_DROUND23(12, 4, 6, 2)
	ldd		[%o0 + 0x70], %f22
	AES_DROUND01(14, 4, 6, 0)
	ldd		[%o0 + 0x68], %f24
	AES_DROUND23(16, 0, 2, 6)
	ldd		[%o0 + 0x60], %f26
	AES_DROUND01(18, 0, 2, 4)
	ldd		[%o0 + 0x58], %f28
	AES_DROUND23(20, 4, 6, 2)
	ldd		[%o0 + 0x50], %f30
	AES_DROUND01(22, 4, 6, 0)
	ldd		[%o0 + 0x48], %f32
	AES_DROUND23(24, 0, 2, 6)
	ldd		[%o0 + 0x40], %f34
	AES_DROUND01(26, 0, 2, 4)
	ldd		[%o0 + 0x38], %f36
	AES_DROUND23(28, 4, 6, 2)
	ldd		[%o0 + 0x30], %f38
	AES_DROUND01(30, 4, 6, 0)
	ldd		[%o0 + 0x28], %f40
	AES_DROUND23(32, 0, 2, 6)
	ldd		[%o0 + 0x20], %f42
	AES_DROUND01(34, 0, 2, 4)
	ldd		[%o0 + 0x18], %f44
	AES_DROUND23(36, 4, 6, 2)
	ldd		[%o0 + 0x10], %f46
	AES_DROUND01(38, 4, 6, 0)
	ldd		[%o0 + 0x08], %f48
	AES_DROUND23(40, 0, 2, 6)
	ldd		[%o0 + 0x00], %f50
	AES_DROUND01(42, 0, 2, 4)
	AES_DROUND23(44, 4, 6, 2)
	AES_DROUND01(46, 4, 6, 0)
	AES_DROUND23_L(48, 0, 2, 6)
	AES_DROUND01_L(50, 0, 2, 4)
	st		%f4, [%o2 + 0x00]
	st		%f5, [%o2 + 0x04]
	st		%f6, [%o2 + 0x08]
	st		%f7, [%o2 + 0x0c]
	retl
	 VISExit
ENDPROC(aes_sparc64_decrypt_256)

	.align		32
ENTRY(aes_sparc64_load_encrypt_keys_128)
	/* %o0=key */
	VISEntry
	ldd		[%o0 + 0x10], %f8
	ldd		[%o0 + 0x18], %f10
	ldd		[%o0 + 0x20], %f12
	ldd		[%o0 + 0x28], %f14
	ldd		[%o0 + 0x30], %f16
	ldd		[%o0 + 0x38], %f18
	ldd		[%o0 + 0x40], %f20
	ldd		[%o0 + 0x48], %f22
	ldd		[%o0 + 0x50], %f24
	ldd		[%o0 + 0x58], %f26
	ldd		[%o0 + 0x60], %f28
	ldd		[%o0 + 0x68], %f30
	ldd		[%o0 + 0x70], %f32
	ldd		[%o0 + 0x78], %f34
	ldd		[%o0 + 0x80], %f36
	ldd		[%o0 + 0x88], %f38
	ldd		[%o0 + 0x90], %f40
	ldd		[%o0 + 0x98], %f42
	ldd		[%o0 + 0xa0], %f44
	retl
	 ldd		[%o0 + 0xa8], %f46
ENDPROC(aes_sparc64_load_encrypt_keys_128)

	.align		32
ENTRY(aes_sparc64_load_encrypt_keys_192)
	/* %o0=key */
	VISEntry
	ldd		[%o0 + 0x10], %f8
	ldd		[%o0 + 0x18], %f10
	ldd		[%o0 + 0x20], %f12
	ldd		[%o0 + 0x28], %f14
	ldd		[%o0 + 0x30], %f16
	ldd		[%o0 + 0x38], %f18
	ldd		[%o0 + 0x40], %f20
	ldd		[%o0 + 0x48], %f22
	ldd		[%o0 + 0x50], %f24
	ldd		[%o0 + 0x58], %f26
	ldd		[%o0 + 0x60], %f28
	ldd		[%o0 + 0x68], %f30
	ldd		[%o0 + 0x70], %f32
	ldd		[%o0 + 0x78], %f34
	ldd		[%o0 + 0x80], %f36
	ldd		[%o0 + 0x88], %f38
	ldd		[%o0 + 0x90], %f40
	ldd		[%o0 + 0x98], %f42
	ldd		[%o0 + 0xa0], %f44
	ldd		[%o0 + 0xa8], %f46
	ldd		[%o0 + 0xb0], %f48
	ldd		[%o0 + 0xb8], %f50
	ldd		[%o0 + 0xc0], %f52
	retl
	 ldd		[%o0 + 0xc8], %f54
ENDPROC(aes_sparc64_load_encrypt_keys_192)

	.align		32
ENTRY(aes_sparc64_load_encrypt_keys_256)
	/* %o0=key */
	VISEntry
	ldd		[%o0 + 0x10], %f8
	ldd		[%o0 + 0x18], %f10
	ldd		[%o0 + 0x20], %f12
	ldd		[%o0 + 0x28], %f14
	ldd		[%o0 + 0x30], %f16
	ldd		[%o0 + 0x38], %f18
	ldd		[%o0 + 0x40], %f20
	ldd		[%o0 + 0x48], %f22
	ldd		[%o0 + 0x50], %f24
	ldd		[%o0 + 0x58], %f26
	ldd		[%o0 + 0x60], %f28
	ldd		[%o0 + 0x68], %f30
	ldd		[%o0 + 0x70], %f32
	ldd		[%o0 + 0x78], %f34
	ldd		[%o0 + 0x80], %f36
	ldd		[%o0 + 0x88], %f38
	ldd		[%o0 + 0x90], %f40
	ldd		[%o0 + 0x98], %f42
	ldd		[%o0 + 0xa0], %f44
	ldd		[%o0 + 0xa8], %f46
	ldd		[%o0 + 0xb0], %f48
	ldd		[%o0 + 0xb8], %f50
	ldd		[%o0 + 0xc0], %f52
	ldd		[%o0 + 0xc8], %f54
	ldd		[%o0 + 0xd0], %f56
	ldd		[%o0 + 0xd8], %f58
	ldd		[%o0 + 0xe0], %f60
	retl
	 ldd		[%o0 + 0xe8], %f62
ENDPROC(aes_sparc64_load_encrypt_keys_256)

	.align		32
ENTRY(aes_sparc64_load_decrypt_keys_128)
	/* %o0=key */
	VISEntry
	ldd		[%o0 + 0x98], %f8
	ldd		[%o0 + 0x90], %f10
	ldd		[%o0 + 0x88], %f12
	ldd		[%o0 + 0x80], %f14
	ldd		[%o0 + 0x78], %f16
	ldd		[%o0 + 0x70], %f18
	ldd		[%o0 + 0x68], %f20
	ldd		[%o0 + 0x60], %f22
	ldd		[%o0 + 0x58], %f24
	ldd		[%o0 + 0x50], %f26
	ldd		[%o0 + 0x48], %f28
	ldd		[%o0 + 0x40], %f30
	ldd		[%o0 + 0x38], %f32
	ldd		[%o0 + 0x30], %f34
	ldd		[%o0 + 0x28], %f36
	ldd		[%o0 + 0x20], %f38
	ldd		[%o0 + 0x18], %f40
	ldd		[%o0 + 0x10], %f42
	ldd		[%o0 + 0x08], %f44
	retl
	 ldd		[%o0 + 0x00], %f46
ENDPROC(aes_sparc64_load_decrypt_keys_128)

	.align		32
ENTRY(aes_sparc64_load_decrypt_keys_192)
	/* %o0=key */
	VISEntry
	ldd		[%o0 + 0xb8], %f8
	ldd		[%o0 + 0xb0], %f10
	ldd		[%o0 + 0xa8], %f12
	ldd		[%o0 + 0xa0], %f14
	ldd		[%o0 + 0x98], %f16
	ldd		[%o0 + 0x90], %f18
	ldd		[%o0 + 0x88], %f20
	ldd		[%o0 + 0x80], %f22
	ldd		[%o0 + 0x78], %f24
	ldd		[%o0 + 0x70], %f26
	ldd		[%o0 + 0x68], %f28
	ldd		[%o0 + 0x60], %f30
	ldd		[%o0 + 0x58], %f32
	ldd		[%o0 + 0x50], %f34
	ldd		[%o0 + 0x48], %f36
	ldd		[%o0 + 0x40], %f38
	ldd		[%o0 + 0x38], %f40
	ldd		[%o0 + 0x30], %f42
	ldd		[%o0 + 0x28], %f44
	ldd		[%o0 + 0x20], %f46
	ldd		[%o0 + 0x18], %f48
	ldd		[%o0 + 0x10], %f50
	ldd		[%o0 + 0x08], %f52
	retl
	 ldd		[%o0 + 0x00], %f54
ENDPROC(aes_sparc64_load_decrypt_keys_192)

	.align		32
ENTRY(aes_sparc64_load_decrypt_keys_256)
	/* %o0=key */
	VISEntry
	ldd		[%o0 + 0xd8], %f8
	ldd		[%o0 + 0xd0], %f10
	ldd		[%o0 + 0xc8], %f12
	ldd		[%o0 + 0xc0], %f14
	ldd		[%o0 + 0xb8], %f16
	ldd		[%o0 + 0xb0], %f18
	ldd		[%o0 + 0xa8], %f20
	ldd		[%o0 + 0xa0], %f22
	ldd		[%o0 + 0x98], %f24
	ldd		[%o0 + 0x90], %f26
	ldd		[%o0 + 0x88], %f28
	ldd		[%o0 + 0x80], %f30
	ldd		[%o0 + 0x78], %f32
	ldd		[%o0 + 0x70], %f34
	ldd		[%o0 + 0x68], %f36
	ldd		[%o0 + 0x60], %f38
	ldd		[%o0 + 0x58], %f40
	ldd		[%o0 + 0x50], %f42
	ldd		[%o0 + 0x48], %f44
	ldd		[%o0 + 0x40], %f46
	ldd		[%o0 + 0x38], %f48
	ldd		[%o0 + 0x30], %f50
	ldd		[%o0 + 0x28], %f52
	ldd		[%o0 + 0x20], %f54
	ldd		[%o0 + 0x18], %f56
	ldd		[%o0 + 0x10], %f58
	ldd		[%o0 + 0x08], %f60
	retl
	 ldd		[%o0 + 0x00], %f62
ENDPROC(aes_sparc64_load_decrypt_keys_256)

	.align		32
ENTRY(aes_sparc64_ecb_encrypt_128)
	/* %o0=key, %o1=input, %o2=output, %o3=len */
	ldx		[%o0 + 0x00], %g1
	ldx		[%o0 + 0x08], %g2
1:	ldx		[%o1 + 0x00], %g3
	ldx		[%o1 + 0x08], %g7
	add		%o1, 0x10, %o1
	xor		%g1, %g3, %g3
	xor		%g2, %g7, %g7
	MOVXTOD_G3_F4
	MOVXTOD_G7_F6
	ENCRYPT_128(8, 4, 6, 0, 2)
	std		%f4, [%o2 + 0x00]
	std		%f6, [%o2 + 0x08]
	subcc		%o3, 0x10, %o3
	bne,pt		%xcc, 1b
	 add		%o2, 0x10, %o2
	retl
	 nop
ENDPROC(aes_sparc64_ecb_encrypt_128)

	.align		32
ENTRY(aes_sparc64_ecb_encrypt_192)
	/* %o0=key, %o1=input, %o2=output, %o3=len */
	ldx		[%o0 + 0x00], %g1
	ldx		[%o0 + 0x08], %g2
1:	ldx		[%o1 + 0x00], %g3
	ldx		[%o1 + 0x08], %g7
	add		%o1, 0x10, %o1
	xor		%g1, %g3, %g3
	xor		%g2, %g7, %g7
	MOVXTOD_G3_F4
	MOVXTOD_G7_F6
	ENCRYPT_192(8, 4, 6, 0, 2)
	std		%f4, [%o2 + 0x00]
	std		%f6, [%o2 + 0x08]
	subcc		%o3, 0x10, %o3
	bne,pt		%xcc, 1b
	 add		%o2, 0x10, %o2
	retl
	 nop
ENDPROC(aes_sparc64_ecb_encrypt_192)

	.align		32
ENTRY(aes_sparc64_ecb_encrypt_256)
	/* %o0=key, %o1=input, %o2=output, %o3=len */
	ldx		[%o0 + 0x00], %g1
	ldx		[%o0 + 0x08], %g2
1:	ldx		[%o1 + 0x00], %g3
	ldx		[%o1 + 0x08], %g7
	add		%o1, 0x10, %o1
	xor		%g1, %g3, %g3
	xor		%g2, %g7, %g7
	MOVXTOD_G3_F4
	MOVXTOD_G7_F6
	ENCRYPT_256(8, 4, 6, 0, 2)
	std		%f4, [%o2 + 0x00]
	std		%f6, [%o2 + 0x08]
	subcc		%o3, 0x10, %o3
	bne,pt		%xcc, 1b
	 add		%o2, 0x10, %o2
	retl
	 nop
ENDPROC(aes_sparc64_ecb_encrypt_256)

	.align		32
ENTRY(aes_sparc64_ecb_decrypt_128)
	/* %o0=&key[key_len], %o1=input, %o2=output, %o3=len */
	ldx		[%o0 - 0x10], %g1
	ldx		[%o0 - 0x08], %g2
1:	ldx		[%o1 + 0x00], %g3
	ldx		[%o1 + 0x08], %g7
	add		%o1, 0x10, %o1
	xor		%g1, %g3, %g3
	xor		%g2, %g7, %g7
	MOVXTOD_G3_F4
	MOVXTOD_G7_F6
	DECRYPT_128(8, 4, 6, 0, 2)
	std		%f4, [%o2 + 0x00]
	std		%f6, [%o2 + 0x08]
	subcc		%o3, 0x10, %o3
	bne,pt		%xcc, 1b
	 add		%o2, 0x10, %o2
	retl
	 nop
ENDPROC(aes_sparc64_ecb_decrypt_128)

	.align		32
ENTRY(aes_sparc64_ecb_decrypt_192)
	/* %o0=&key[key_len], %o1=input, %o2=output, %o3=len */
	ldx		[%o0 - 0x10], %g1
	ldx		[%o0 - 0x08], %g2
1:	ldx		[%o1 + 0x00], %g3
	ldx		[%o1 + 0x08], %g7
	add		%o1, 0x10, %o1
	xor		%g1, %g3, %g3
	xor		%g2, %g7, %g7
	MOVXTOD_G3_F4
	MOVXTOD_G7_F6
	DECRYPT_192(8, 4, 6, 0, 2)
	std		%f4, [%o2 + 0x00]
	std		%f6, [%o2 + 0x08]
	subcc		%o3, 0x10, %o3
	bne,pt		%xcc, 1b
	 add		%o2, 0x10, %o2
	retl
	 nop
ENDPROC(aes_sparc64_ecb_decrypt_192)

	.align		32
ENTRY(aes_sparc64_ecb_decrypt_256)
	/* %o0=&key[key_len], %o1=input, %o2=output, %o3=len */
	ldx		[%o0 - 0x10], %g1
	ldx		[%o0 - 0x08], %g2
1:	ldx		[%o1 + 0x00], %g3
	ldx		[%o1 + 0x08], %g7
	add		%o1, 0x10, %o1
	xor		%g1, %g3, %g3
	xor		%g2, %g7, %g7
	MOVXTOD_G3_F4
	MOVXTOD_G7_F6
	DECRYPT_256(8, 4, 6, 0, 2)
	std		%f4, [%o2 + 0x00]
	std		%f6, [%o2 + 0x08]
	subcc		%o3, 0x10, %o3
	bne,pt		%xcc, 1b
	 add		%o2, 0x10, %o2
	retl
	 nop
ENDPROC(aes_sparc64_ecb_decrypt_256)

	.align		32
ENTRY(aes_sparc64_cbc_encrypt_128)
	/* %o0=key, %o1=input, %o2=output, %o3=len, %o4=IV */
	ldd		[%o4 + 0x00], %f4
	ldd		[%o4 + 0x08], %f6
	ldx		[%o0 + 0x00], %g1
	ldx		[%o0 + 0x08], %g2
1:	ldx		[%o1 + 0x00], %g3
	ldx		[%o1 + 0x08], %g7
	add		%o1, 0x10, %o1
	xor		%g1, %g3, %g3
	xor		%g2, %g7, %g7
	MOVXTOD_G3_F0
	MOVXTOD_G7_F2
	fxor		%f4, %f0, %f4
	fxor		%f6, %f2, %f6
	ENCRYPT_128(8, 4, 6, 0, 2)
	std		%f4, [%o2 + 0x00]
	std		%f6, [%o2 + 0x08]
	subcc		%o3, 0x10, %o3
	bne,pt		%xcc, 1b
	 add		%o2, 0x10, %o2
	std		%f4, [%o4 + 0x00]
	std		%f6, [%o4 + 0x08]
	retl
	 nop
ENDPROC(aes_sparc64_cbc_encrypt_128)

	.align		32
ENTRY(aes_sparc64_cbc_encrypt_192)
	/* %o0=key, %o1=input, %o2=output, %o3=len, %o4=IV */
	ldd		[%o4 + 0x00], %f4
	ldd		[%o4 + 0x08], %f6
	ldx		[%o0 + 0x00], %g1
	ldx		[%o0 + 0x08], %g2
1:	ldx		[%o1 + 0x00], %g3
	ldx		[%o1 + 0x08], %g7
	add		%o1, 0x10, %o1
	xor		%g1, %g3, %g3
	xor		%g2, %g7, %g7
	MOVXTOD_G3_F0
	MOVXTOD_G7_F2
	fxor		%f4, %f0, %f4
	fxor		%f6, %f2, %f6
	ENCRYPT_192(8, 4, 6, 0, 2)
	std		%f4, [%o2 + 0x00]
	std		%f6, [%o2 + 0x08]
	subcc		%o3, 0x10, %o3
	bne,pt		%xcc, 1b
	 add		%o2, 0x10, %o2
	std		%f4, [%o4 + 0x00]
	std		%f6, [%o4 + 0x08]
	retl
	 nop
ENDPROC(aes_sparc64_cbc_encrypt_192)

	.align		32
ENTRY(aes_sparc64_cbc_encrypt_256)
	/* %o0=key, %o1=input, %o2=output, %o3=len, %o4=IV */
	ldd		[%o4 + 0x00], %f4
	ldd		[%o4 + 0x08], %f6
	ldx		[%o0 + 0x00], %g1
	ldx		[%o0 + 0x08], %g2
1:	ldx		[%o1 + 0x00], %g3
	ldx		[%o1 + 0x08], %g7
	add		%o1, 0x10, %o1
	xor		%g1, %g3, %g3
	xor		%g2, %g7, %g7
	MOVXTOD_G3_F0
	MOVXTOD_G7_F2
	fxor		%f4, %f0, %f4
	fxor		%f6, %f2, %f6
	ENCRYPT_256(8, 4, 6, 0, 2)
	std		%f4, [%o2 + 0x00]
	std		%f6, [%o2 + 0x08]
	subcc		%o3, 0x10, %o3
	bne,pt		%xcc, 1b
	 add		%o2, 0x10, %o2
	std		%f4, [%o4 + 0x00]
	std		%f6, [%o4 + 0x08]
	retl
	 nop
ENDPROC(aes_sparc64_cbc_encrypt_256)

	.align		32
ENTRY(aes_sparc64_cbc_decrypt_128)
	/* %o0=&key[key_len], %o1=input, %o2=output, %o3=len, %o4=iv */
	ldx		[%o0 - 0x10], %g1
	ldx		[%o0 - 0x08], %g2
	ldx		[%o4 + 0x00], %o0
	ldx		[%o4 + 0x08], %o5
1:	ldx		[%o1 + 0x00], %g3
	ldx		[%o1 + 0x08], %g7
	add		%o1, 0x10, %o1
	xor		%g1, %g3, %g3
	xor		%g2, %g7, %g7
	MOVXTOD_G3_F4
	MOVXTOD_G7_F6
	DECRYPT_128(8, 4, 6, 0, 2)
	MOVXTOD_O0_F0
	MOVXTOD_O5_F2
	xor		%g1, %g3, %o0
	xor		%g2, %g7, %o5
	fxor		%f4, %f0, %f4
	fxor		%f6, %f2, %f6
	std		%f4, [%o2 + 0x00]
	std		%f6, [%o2 + 0x08]
	subcc		%o3, 0x10, %o3
	bne,pt		%xcc, 1b
	 add		%o2, 0x10, %o2
	stx		%o0, [%o4 + 0x00]
	stx		%o5, [%o4 + 0x08]
	retl
	 nop
ENDPROC(aes_sparc64_cbc_decrypt_128)

	.align		32
ENTRY(aes_sparc64_cbc_decrypt_192)
	/* %o0=&key[key_len], %o1=input, %o2=output, %o3=len, %o4=iv */
	ldx		[%o0 - 0x10], %g1
	ldx		[%o0 - 0x08], %g2
	ldx		[%o4 + 0x00], %o0
	ldx		[%o4 + 0x08], %o5
1:	ldx		[%o1 + 0x00], %g3
	ldx		[%o1 + 0x08], %g7
	add		%o1, 0x10, %o1
	xor		%g1, %g3, %g3
	xor		%g2, %g7, %g7
	MOVXTOD_G3_F4
	MOVXTOD_G7_F6
	DECRYPT_192(8, 4, 6, 0, 2)
	MOVXTOD_O0_F0
	MOVXTOD_O5_F2
	xor		%g1, %g3, %o0
	xor		%g2, %g7, %o5
	fxor		%f4, %f0, %f4
	fxor		%f6, %f2, %f6
	std		%f4, [%o2 + 0x00]
	std		%f6, [%o2 + 0x08]
	subcc		%o3, 0x10, %o3
	bne,pt		%xcc, 1b
	 add		%o2, 0x10, %o2
	stx		%o0, [%o4 + 0x00]
	stx		%o5, [%o4 + 0x08]
	retl
	 nop
ENDPROC(aes_sparc64_cbc_decrypt_192)

	.align		32
ENTRY(aes_sparc64_cbc_decrypt_256)
	/* %o0=&key[key_len], %o1=input, %o2=output, %o3=len, %o4=iv */
	ldx		[%o0 - 0x10], %g1
	ldx		[%o0 - 0x08], %g2
	ldx		[%o4 + 0x00], %o0
	ldx		[%o4 + 0x08], %o5
1:	ldx		[%o1 + 0x00], %g3
	ldx		[%o1 + 0x08], %g7
	add		%o1, 0x10, %o1
	xor		%g1, %g3, %g3
	xor		%g2, %g7, %g7
	MOVXTOD_G3_F4
	MOVXTOD_G7_F6
	DECRYPT_256(8, 4, 6, 0, 2)
	MOVXTOD_O0_F0
	MOVXTOD_O5_F2
	xor		%g1, %g3, %o0
	xor		%g2, %g7, %o5
	fxor		%f4, %f0, %f4
	fxor		%f6, %f2, %f6
	std		%f4, [%o2 + 0x00]
	std		%f6, [%o2 + 0x08]
	subcc		%o3, 0x10, %o3
	bne,pt		%xcc, 1b
	 add		%o2, 0x10, %o2
	stx		%o0, [%o4 + 0x00]
	stx		%o5, [%o4 + 0x08]
	retl
	 nop
ENDPROC(aes_sparc64_cbc_decrypt_256)

	.align		32
ENTRY(aes_sparc64_ctr_crypt_128)
	/* %o0=key, %o1=input, %o2=output, %o3=len, %o4=IV */
	ldx		[%o4 + 0x00], %g3
	ldx		[%o4 + 0x08], %g7
	ldx		[%o0 + 0x00], %g1
	ldx		[%o0 + 0x08], %g2
1:	xor		%g1, %g3, %o5
	MOVXTOD_O5_F0
	xor		%g2, %g7, %o5
	MOVXTOD_O5_F2
	add		%g7, 1, %g7
	add		%g3, 1, %o5
	movrz		%g7, %o5, %g3
	ENCRYPT_128(8, 0, 2, 4, 6)
	ldd		[%o1 + 0x00], %f4
	ldd		[%o1 + 0x08], %f6
	fxor		%f4, %f0, %f4
	fxor		%f6, %f2, %f6
	std		%f4, [%o2 + 0x00]
	std		%f6, [%o2 + 0x08]
	subcc		%o3, 0x10, %o3
	add		%o1, 0x10, %o1
	bne,pt		%xcc, 1b
	 add		%o2, 0x10, %o2
	stx		%g3, [%o4 + 0x00]
	stx		%g7, [%o4 + 0x08]
	retl
	 nop
ENDPROC(aes_sparc64_ctr_crypt_128)

	.align		32
ENTRY(aes_sparc64_ctr_crypt_192)
	/* %o0=key, %o1=input, %o2=output, %o3=len, %o4=IV */
	ldx		[%o4 + 0x00], %g3
	ldx		[%o4 + 0x08], %g7
	ldx		[%o0 + 0x00], %g1
	ldx		[%o0 + 0x08], %g2
1:	xor		%g1, %g3, %o5
	MOVXTOD_O5_F0
	xor		%g2, %g7, %o5
	MOVXTOD_O5_F2
	add		%g7, 1, %g7
	add		%g3, 1, %o5
	movrz		%g7, %o5, %g3
	ENCRYPT_192(8, 0, 2, 4, 6)
	ldd		[%o1 + 0x00], %f4
	ldd		[%o1 + 0x08], %f6
	fxor		%f4, %f0, %f4
	fxor		%f6, %f2, %f6
	std		%f4, [%o2 + 0x00]
	std		%f6, [%o2 + 0x08]
	subcc		%o3, 0x10, %o3
	add		%o1, 0x10, %o1
	bne,pt		%xcc, 1b
	 add		%o2, 0x10, %o2
	stx		%g3, [%o4 + 0x00]
	stx		%g7, [%o4 + 0x08]
	retl
	 nop
ENDPROC(aes_sparc64_ctr_crypt_192)

	.align		32
ENTRY(aes_sparc64_ctr_crypt_256)
	/* %o0=key, %o1=input, %o2=output, %o3=len, %o4=IV */
	ldx		[%o4 + 0x00], %g3
	ldx		[%o4 + 0x08], %g7
	ldx		[%o0 + 0x00], %g1
	ldx		[%o0 + 0x08], %g2
1:	xor		%g1, %g3, %o5
	MOVXTOD_O5_F0
	xor		%g2, %g7, %o5
	MOVXTOD_O5_F2
	add		%g7, 1, %g7
	add		%g3, 1, %o5
	movrz		%g7, %o5, %g3
	ENCRYPT_256(8, 0, 2, 4, 6)
	ldd		[%o1 + 0x00], %f4
	ldd		[%o1 + 0x08], %f6
	fxor		%f4, %f0, %f4
	fxor		%f6, %f2, %f6
	std		%f4, [%o2 + 0x00]
	std		%f6, [%o2 + 0x08]
	subcc		%o3, 0x10, %o3
	add		%o1, 0x10, %o1
	bne,pt		%xcc, 1b
	 add		%o2, 0x10, %o2
	stx		%g3, [%o4 + 0x00]
	stx		%g7, [%o4 + 0x08]
	retl
	 nop
ENDPROC(aes_sparc64_ctr_crypt_256)
