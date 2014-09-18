#include <linux/linkage.h>
#include <asm/visasm.h>

#include "opcodes.h"

	.align	32
ENTRY(des_sparc64_key_expand)
	/* %o0=input_key, %o1=output_key */
	VISEntryHalf
	ld	[%o0 + 0x00], %f0
	ld	[%o0 + 0x04], %f1
	DES_KEXPAND(0, 0, 0)
	DES_KEXPAND(0, 1, 2)
	DES_KEXPAND(2, 3, 6)
	DES_KEXPAND(2, 2, 4)
	DES_KEXPAND(6, 3, 10)
	DES_KEXPAND(6, 2, 8)
	DES_KEXPAND(10, 3, 14)
	DES_KEXPAND(10, 2, 12)
	DES_KEXPAND(14, 1, 16)
	DES_KEXPAND(16, 3, 20)
	DES_KEXPAND(16, 2, 18)
	DES_KEXPAND(20, 3, 24)
	DES_KEXPAND(20, 2, 22)
	DES_KEXPAND(24, 3, 28)
	DES_KEXPAND(24, 2, 26)
	DES_KEXPAND(28, 1, 30)
	std	%f0, [%o1 + 0x00]
	std	%f2, [%o1 + 0x08]
	std	%f4, [%o1 + 0x10]
	std	%f6, [%o1 + 0x18]
	std	%f8, [%o1 + 0x20]
	std	%f10, [%o1 + 0x28]
	std	%f12, [%o1 + 0x30]
	std	%f14, [%o1 + 0x38]
	std	%f16, [%o1 + 0x40]
	std	%f18, [%o1 + 0x48]
	std	%f20, [%o1 + 0x50]
	std	%f22, [%o1 + 0x58]
	std	%f24, [%o1 + 0x60]
	std	%f26, [%o1 + 0x68]
	std	%f28, [%o1 + 0x70]
	std	%f30, [%o1 + 0x78]
	retl
	 VISExitHalf
ENDPROC(des_sparc64_key_expand)

	.align	32
ENTRY(des_sparc64_crypt)
	/* %o0=key, %o1=input, %o2=output */
	VISEntry
	ldd	[%o1 + 0x00], %f32
	ldd	[%o0 + 0x00], %f0
	ldd	[%o0 + 0x08], %f2
	ldd	[%o0 + 0x10], %f4
	ldd	[%o0 + 0x18], %f6
	ldd	[%o0 + 0x20], %f8
	ldd	[%o0 + 0x28], %f10
	ldd	[%o0 + 0x30], %f12
	ldd	[%o0 + 0x38], %f14
	ldd	[%o0 + 0x40], %f16
	ldd	[%o0 + 0x48], %f18
	ldd	[%o0 + 0x50], %f20
	ldd	[%o0 + 0x58], %f22
	ldd	[%o0 + 0x60], %f24
	ldd	[%o0 + 0x68], %f26
	ldd	[%o0 + 0x70], %f28
	ldd	[%o0 + 0x78], %f30
	DES_IP(32, 32)
	DES_ROUND(0, 2, 32, 32)
	DES_ROUND(4, 6, 32, 32)
	DES_ROUND(8, 10, 32, 32)
	DES_ROUND(12, 14, 32, 32)
	DES_ROUND(16, 18, 32, 32)
	DES_ROUND(20, 22, 32, 32)
	DES_ROUND(24, 26, 32, 32)
	DES_ROUND(28, 30, 32, 32)
	DES_IIP(32, 32)
	std	%f32, [%o2 + 0x00]
	retl
	 VISExit
ENDPROC(des_sparc64_crypt)

	.align	32
ENTRY(des_sparc64_load_keys)
	/* %o0=key */
	VISEntry
	ldd	[%o0 + 0x00], %f0
	ldd	[%o0 + 0x08], %f2
	ldd	[%o0 + 0x10], %f4
	ldd	[%o0 + 0x18], %f6
	ldd	[%o0 + 0x20], %f8
	ldd	[%o0 + 0x28], %f10
	ldd	[%o0 + 0x30], %f12
	ldd	[%o0 + 0x38], %f14
	ldd	[%o0 + 0x40], %f16
	ldd	[%o0 + 0x48], %f18
	ldd	[%o0 + 0x50], %f20
	ldd	[%o0 + 0x58], %f22
	ldd	[%o0 + 0x60], %f24
	ldd	[%o0 + 0x68], %f26
	ldd	[%o0 + 0x70], %f28
	retl
	 ldd	[%o0 + 0x78], %f30
ENDPROC(des_sparc64_load_keys)

	.align	32
ENTRY(des_sparc64_ecb_crypt)
	/* %o0=input, %o1=output, %o2=len */
1:	ldd	[%o0 + 0x00], %f32
	add	%o0, 0x08, %o0
	DES_IP(32, 32)
	DES_ROUND(0, 2, 32, 32)
	DES_ROUND(4, 6, 32, 32)
	DES_ROUND(8, 10, 32, 32)
	DES_ROUND(12, 14, 32, 32)
	DES_ROUND(16, 18, 32, 32)
	DES_ROUND(20, 22, 32, 32)
	DES_ROUND(24, 26, 32, 32)
	DES_ROUND(28, 30, 32, 32)
	DES_IIP(32, 32)
	std	%f32, [%o1 + 0x00]
	subcc	%o2, 0x08, %o2
	bne,pt	%icc, 1b
	 add	%o1, 0x08, %o1
	retl
	 nop
ENDPROC(des_sparc64_ecb_crypt)

	.align	32
ENTRY(des_sparc64_cbc_encrypt)
	/* %o0=input, %o1=output, %o2=len, %o3=IV */
	ldd	[%o3 + 0x00], %f32
1:	ldd	[%o0 + 0x00], %f34
	fxor	%f32, %f34, %f32
	DES_IP(32, 32)
	DES_ROUND(0, 2, 32, 32)
	DES_ROUND(4, 6, 32, 32)
	DES_ROUND(8, 10, 32, 32)
	DES_ROUND(12, 14, 32, 32)
	DES_ROUND(16, 18, 32, 32)
	DES_ROUND(20, 22, 32, 32)
	DES_ROUND(24, 26, 32, 32)
	DES_ROUND(28, 30, 32, 32)
	DES_IIP(32, 32)
	std	%f32, [%o1 + 0x00]
	add	%o0, 0x08, %o0
	subcc	%o2, 0x08, %o2
	bne,pt	%icc, 1b
	 add	%o1, 0x08, %o1
	retl
	 std	%f32, [%o3 + 0x00]
ENDPROC(des_sparc64_cbc_encrypt)

	.align	32
ENTRY(des_sparc64_cbc_decrypt)
	/* %o0=input, %o1=output, %o2=len, %o3=IV */
	ldd	[%o3 + 0x00], %f34
1:	ldd	[%o0 + 0x00], %f36
	DES_IP(36, 32)
	DES_ROUND(0, 2, 32, 32)
	DES_ROUND(4, 6, 32, 32)
	DES_ROUND(8, 10, 32, 32)
	DES_ROUND(12, 14, 32, 32)
	DES_ROUND(16, 18, 32, 32)
	DES_ROUND(20, 22, 32, 32)
	DES_ROUND(24, 26, 32, 32)
	DES_ROUND(28, 30, 32, 32)
	DES_IIP(32, 32)
	fxor	%f32, %f34, %f32
	fsrc2	%f36, %f34
	std	%f32, [%o1 + 0x00]
	add	%o0, 0x08, %o0
	subcc	%o2, 0x08, %o2
	bne,pt	%icc, 1b
	 add	%o1, 0x08, %o1
	retl
	 std	%f36, [%o3 + 0x00]
ENDPROC(des_sparc64_cbc_decrypt)

	.align	32
ENTRY(des3_ede_sparc64_crypt)
	/* %o0=key, %o1=input, %o2=output */
	VISEntry
	ldd	[%o1 + 0x00], %f32
	ldd	[%o0 + 0x00], %f0
	ldd	[%o0 + 0x08], %f2
	ldd	[%o0 + 0x10], %f4
	ldd	[%o0 + 0x18], %f6
	ldd	[%o0 + 0x20], %f8
	ldd	[%o0 + 0x28], %f10
	ldd	[%o0 + 0x30], %f12
	ldd	[%o0 + 0x38], %f14
	ldd	[%o0 + 0x40], %f16
	ldd	[%o0 + 0x48], %f18
	ldd	[%o0 + 0x50], %f20
	ldd	[%o0 + 0x58], %f22
	ldd	[%o0 + 0x60], %f24
	ldd	[%o0 + 0x68], %f26
	ldd	[%o0 + 0x70], %f28
	ldd	[%o0 + 0x78], %f30
	DES_IP(32, 32)
	DES_ROUND(0, 2, 32, 32)
	ldd	[%o0 + 0x80], %f0
	ldd	[%o0 + 0x88], %f2
	DES_ROUND(4, 6, 32, 32)
	ldd	[%o0 + 0x90], %f4
	ldd	[%o0 + 0x98], %f6
	DES_ROUND(8, 10, 32, 32)
	ldd	[%o0 + 0xa0], %f8
	ldd	[%o0 + 0xa8], %f10
	DES_ROUND(12, 14, 32, 32)
	ldd	[%o0 + 0xb0], %f12
	ldd	[%o0 + 0xb8], %f14
	DES_ROUND(16, 18, 32, 32)
	ldd	[%o0 + 0xc0], %f16
	ldd	[%o0 + 0xc8], %f18
	DES_ROUND(20, 22, 32, 32)
	ldd	[%o0 + 0xd0], %f20
	ldd	[%o0 + 0xd8], %f22
	DES_ROUND(24, 26, 32, 32)
	ldd	[%o0 + 0xe0], %f24
	ldd	[%o0 + 0xe8], %f26
	DES_ROUND(28, 30, 32, 32)
	ldd	[%o0 + 0xf0], %f28
	ldd	[%o0 + 0xf8], %f30
	DES_IIP(32, 32)
	DES_IP(32, 32)
	DES_ROUND(0, 2, 32, 32)
	ldd	[%o0 + 0x100], %f0
	ldd	[%o0 + 0x108], %f2
	DES_ROUND(4, 6, 32, 32)
	ldd	[%o0 + 0x110], %f4
	ldd	[%o0 + 0x118], %f6
	DES_ROUND(8, 10, 32, 32)
	ldd	[%o0 + 0x120], %f8
	ldd	[%o0 + 0x128], %f10
	DES_ROUND(12, 14, 32, 32)
	ldd	[%o0 + 0x130], %f12
	ldd	[%o0 + 0x138], %f14
	DES_ROUND(16, 18, 32, 32)
	ldd	[%o0 + 0x140], %f16
	ldd	[%o0 + 0x148], %f18
	DES_ROUND(20, 22, 32, 32)
	ldd	[%o0 + 0x150], %f20
	ldd	[%o0 + 0x158], %f22
	DES_ROUND(24, 26, 32, 32)
	ldd	[%o0 + 0x160], %f24
	ldd	[%o0 + 0x168], %f26
	DES_ROUND(28, 30, 32, 32)
	ldd	[%o0 + 0x170], %f28
	ldd	[%o0 + 0x178], %f30
	DES_IIP(32, 32)
	DES_IP(32, 32)
	DES_ROUND(0, 2, 32, 32)
	DES_ROUND(4, 6, 32, 32)
	DES_ROUND(8, 10, 32, 32)
	DES_ROUND(12, 14, 32, 32)
	DES_ROUND(16, 18, 32, 32)
	DES_ROUND(20, 22, 32, 32)
	DES_ROUND(24, 26, 32, 32)
	DES_ROUND(28, 30, 32, 32)
	DES_IIP(32, 32)

	std	%f32, [%o2 + 0x00]
	retl
	 VISExit
ENDPROC(des3_ede_sparc64_crypt)

	.align	32
ENTRY(des3_ede_sparc64_load_keys)
	/* %o0=key */
	VISEntry
	ldd	[%o0 + 0x00], %f0
	ldd	[%o0 + 0x08], %f2
	ldd	[%o0 + 0x10], %f4
	ldd	[%o0 + 0x18], %f6
	ldd	[%o0 + 0x20], %f8
	ldd	[%o0 + 0x28], %f10
	ldd	[%o0 + 0x30], %f12
	ldd	[%o0 + 0x38], %f14
	ldd	[%o0 + 0x40], %f16
	ldd	[%o0 + 0x48], %f18
	ldd	[%o0 + 0x50], %f20
	ldd	[%o0 + 0x58], %f22
	ldd	[%o0 + 0x60], %f24
	ldd	[%o0 + 0x68], %f26
	ldd	[%o0 + 0x70], %f28
	ldd	[%o0 + 0x78], %f30
	ldd	[%o0 + 0x80], %f32
	ldd	[%o0 + 0x88], %f34
	ldd	[%o0 + 0x90], %f36
	ldd	[%o0 + 0x98], %f38
	ldd	[%o0 + 0xa0], %f40
	ldd	[%o0 + 0xa8], %f42
	ldd	[%o0 + 0xb0], %f44
	ldd	[%o0 + 0xb8], %f46
	ldd	[%o0 + 0xc0], %f48
	ldd	[%o0 + 0xc8], %f50
	ldd	[%o0 + 0xd0], %f52
	ldd	[%o0 + 0xd8], %f54
	ldd	[%o0 + 0xe0], %f56
	retl
	 ldd	[%o0 + 0xe8], %f58
ENDPROC(des3_ede_sparc64_load_keys)

#define DES3_LOOP_BODY(X) \
	DES_IP(X, X) \
	DES_ROUND(0, 2, X, X) \
	DES_ROUND(4, 6, X, X) \
	DES_ROUND(8, 10, X, X) \
	DES_ROUND(12, 14, X, X) \
	DES_ROUND(16, 18, X, X) \
	ldd	[%o0 + 0xf0], %f16; \
	ldd	[%o0 + 0xf8], %f18; \
	DES_ROUND(20, 22, X, X) \
	ldd	[%o0 + 0x100], %f20; \
	ldd	[%o0 + 0x108], %f22; \
	DES_ROUND(24, 26, X, X) \
	ldd	[%o0 + 0x110], %f24; \
	ldd	[%o0 + 0x118], %f26; \
	DES_ROUND(28, 30, X, X) \
	ldd	[%o0 + 0x120], %f28; \
	ldd	[%o0 + 0x128], %f30; \
	DES_IIP(X, X) \
	DES_IP(X, X) \
	DES_ROUND(32, 34, X, X) \
	ldd	[%o0 + 0x130], %f0; \
	ldd	[%o0 + 0x138], %f2; \
	DES_ROUND(36, 38, X, X) \
	ldd	[%o0 + 0x140], %f4; \
	ldd	[%o0 + 0x148], %f6; \
	DES_ROUND(40, 42, X, X) \
	ldd	[%o0 + 0x150], %f8; \
	ldd	[%o0 + 0x158], %f10; \
	DES_ROUND(44, 46, X, X) \
	ldd	[%o0 + 0x160], %f12; \
	ldd	[%o0 + 0x168], %f14; \
	DES_ROUND(48, 50, X, X) \
	DES_ROUND(52, 54, X, X) \
	DES_ROUND(56, 58, X, X) \
	DES_ROUND(16, 18, X, X) \
	ldd	[%o0 + 0x170], %f16; \
	ldd	[%o0 + 0x178], %f18; \
	DES_IIP(X, X) \
	DES_IP(X, X) \
	DES_ROUND(20, 22, X, X) \
	ldd	[%o0 + 0x50], %f20; \
	ldd	[%o0 + 0x58], %f22; \
	DES_ROUND(24, 26, X, X) \
	ldd	[%o0 + 0x60], %f24; \
	ldd	[%o0 + 0x68], %f26; \
	DES_ROUND(28, 30, X, X) \
	ldd	[%o0 + 0x70], %f28; \
	ldd	[%o0 + 0x78], %f30; \
	DES_ROUND(0, 2, X, X) \
	ldd	[%o0 + 0x00], %f0; \
	ldd	[%o0 + 0x08], %f2; \
	DES_ROUND(4, 6, X, X) \
	ldd	[%o0 + 0x10], %f4; \
	ldd	[%o0 + 0x18], %f6; \
	DES_ROUND(8, 10, X, X) \
	ldd	[%o0 + 0x20], %f8; \
	ldd	[%o0 + 0x28], %f10; \
	DES_ROUND(12, 14, X, X) \
	ldd	[%o0 + 0x30], %f12; \
	ldd	[%o0 + 0x38], %f14; \
	DES_ROUND(16, 18, X, X) \
	ldd	[%o0 + 0x40], %f16; \
	ldd	[%o0 + 0x48], %f18; \
	DES_IIP(X, X)

	.align	32
ENTRY(des3_ede_sparc64_ecb_crypt)
	/* %o0=key, %o1=input, %o2=output, %o3=len */
1:	ldd	[%o1 + 0x00], %f60
	DES3_LOOP_BODY(60)
	std	%f60, [%o2 + 0x00]
	add	%o1, 0x08, %o1
	subcc	%o3, 0x08, %o3
	bne,pt	%icc, 1b
	 add	%o2, 0x08, %o2
	retl
	 nop
ENDPROC(des3_ede_sparc64_ecb_crypt)

	.align	32
ENTRY(des3_ede_sparc64_cbc_encrypt)
	/* %o0=key, %o1=input, %o2=output, %o3=len, %o4=IV */
	ldd	[%o4 + 0x00], %f60
1:	ldd	[%o1 + 0x00], %f62
	fxor	%f60, %f62, %f60
	DES3_LOOP_BODY(60)
	std	%f60, [%o2 + 0x00]
	add	%o1, 0x08, %o1
	subcc	%o3, 0x08, %o3
	bne,pt	%icc, 1b
	 add	%o2, 0x08, %o2
	retl
	 std	%f60, [%o4 + 0x00]
ENDPROC(des3_ede_sparc64_cbc_encrypt)

	.align	32
ENTRY(des3_ede_sparc64_cbc_decrypt)
	/* %o0=key, %o1=input, %o2=output, %o3=len, %o4=IV */
	ldd	[%o4 + 0x00], %f62
1:	ldx	[%o1 + 0x00], %g1
	MOVXTOD_G1_F60
	DES3_LOOP_BODY(60)
	fxor	%f62, %f60, %f60
	MOVXTOD_G1_F62
	std	%f60, [%o2 + 0x00]
	add	%o1, 0x08, %o1
	subcc	%o3, 0x08, %o3
	bne,pt	%icc, 1b
	 add	%o2, 0x08, %o2
	retl
	 stx	%g1, [%o4 + 0x00]
ENDPROC(des3_ede_sparc64_cbc_decrypt)
