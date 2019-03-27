/*
 * Speed-optimized CRC32 using slicing-by-eight algorithm
 *
 * This uses only i386 instructions, but it is optimized for i686 and later
 * (including e.g. Pentium II/III/IV, Athlon XP, and Core 2). For i586
 * (e.g. Pentium), slicing-by-four would be better, and even the C version
 * of slicing-by-eight built with gcc -march=i586 tends to be a little bit
 * better than this. Very few probably run this code on i586 or older x86
 * so this shouldn't be a problem in practice.
 *
 * Authors: Igor Pavlov (original version)
 *          Lasse Collin (AT&T syntax, PIC support, better portability)
 *
 * This file has been put into the public domain.
 * You can do whatever you want with this file.
 *
 * This code needs lzma_crc32_table, which can be created using the
 * following C code:

uint32_t lzma_crc32_table[8][256];

void
init_table(void)
{
	// IEEE-802.3
	static const uint32_t poly32 = UINT32_C(0xEDB88320);

	// Castagnoli
	// static const uint32_t poly32 = UINT32_C(0x82F63B78);

	// Koopman
	// static const uint32_t poly32 = UINT32_C(0xEB31D82E);

	for (size_t s = 0; s < 8; ++s) {
		for (size_t b = 0; b < 256; ++b) {
			uint32_t r = s == 0 ? b : lzma_crc32_table[s - 1][b];

			for (size_t i = 0; i < 8; ++i) {
				if (r & 1)
					r = (r >> 1) ^ poly32;
				else
					r >>= 1;
			}

			lzma_crc32_table[s][b] = r;
		}
	}
}

 * The prototype of the CRC32 function:
 * extern uint32_t lzma_crc32(const uint8_t *buf, size_t size, uint32_t crc);
 */

/*
 * On some systems, the functions need to be prefixed. The prefix is
 * usually an underscore.
 */
#ifndef __USER_LABEL_PREFIX__
#	define __USER_LABEL_PREFIX__
#endif
#define MAKE_SYM_CAT(prefix, sym) prefix ## sym
#define MAKE_SYM(prefix, sym) MAKE_SYM_CAT(prefix, sym)
#define LZMA_CRC32 MAKE_SYM(__USER_LABEL_PREFIX__, lzma_crc32)
#define LZMA_CRC32_TABLE MAKE_SYM(__USER_LABEL_PREFIX__, lzma_crc32_table)

/*
 * Solaris assembler doesn't have .p2align, and Darwin uses .align
 * differently than GNU/Linux and Solaris.
 */
#if defined(__APPLE__) || defined(__MSDOS__)
#	define ALIGN(pow2, abs) .align pow2
#else
#	define ALIGN(pow2, abs) .align abs
#endif

	.text
	.globl	LZMA_CRC32

#if !defined(__APPLE__) && !defined(_WIN32) && !defined(__CYGWIN__) \
		&& !defined(__MSDOS__)
	.type	LZMA_CRC32, @function
#endif

	ALIGN(4, 16)
LZMA_CRC32:
	/*
	 * Register usage:
	 * %eax crc
	 * %esi buf
	 * %edi size or buf + size
	 * %ebx lzma_crc32_table
	 * %ebp Table index
	 * %ecx Temporary
	 * %edx Temporary
	 */
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	pushl	%ebp
	movl	0x14(%esp), %esi /* buf */
	movl	0x18(%esp), %edi /* size */
	movl	0x1C(%esp), %eax /* crc */

	/*
	 * Store the address of lzma_crc32_table to %ebx. This is needed to
	 * get position-independent code (PIC).
	 *
	 * The PIC macro is defined by libtool, while __PIC__ is defined
	 * by GCC but only on some systems. Testing for both makes it simpler
	 * to test this code without libtool, and keeps the code working also
	 * when built with libtool but using something else than GCC.
	 *
	 * I understood that libtool may define PIC on Windows even though
	 * the code in Windows DLLs is not PIC in sense that it is in ELF
	 * binaries, so we need a separate check to always use the non-PIC
	 * code on Windows.
	 */
#if (!defined(PIC) && !defined(__PIC__)) \
		|| (defined(_WIN32) || defined(__CYGWIN__))
	/* Not PIC */
	movl	$ LZMA_CRC32_TABLE, %ebx
#elif defined(__APPLE__)
	/* Mach-O */
	call	.L_get_pc
.L_pic:
	leal	.L_lzma_crc32_table$non_lazy_ptr-.L_pic(%ebx), %ebx
	movl	(%ebx), %ebx
#else
	/* ELF */
	call	.L_get_pc
	addl	$_GLOBAL_OFFSET_TABLE_, %ebx
	movl	LZMA_CRC32_TABLE@GOT(%ebx), %ebx
#endif

	/* Complement the initial value. */
	notl	%eax

	ALIGN(4, 16)
.L_align:
	/*
	 * Check if there is enough input to use slicing-by-eight.
	 * We need 16 bytes, because the loop pre-reads eight bytes.
	 */
	cmpl	$16, %edi
	jb	.L_rest

	/* Check if we have reached alignment of eight bytes. */
	testl	$7, %esi
	jz	.L_slice

	/* Calculate CRC of the next input byte. */
	movzbl	(%esi), %ebp
	incl	%esi
	movzbl	%al, %ecx
	xorl	%ecx, %ebp
	shrl	$8, %eax
	xorl	(%ebx, %ebp, 4), %eax
	decl	%edi
	jmp	.L_align

	ALIGN(2, 4)
.L_slice:
	/*
	 * If we get here, there's at least 16 bytes of aligned input
	 * available. Make %edi multiple of eight bytes. Store the possible
	 * remainder over the "size" variable in the argument stack.
	 */
	movl	%edi, 0x18(%esp)
	andl	$-8, %edi
	subl	%edi, 0x18(%esp)

	/*
	 * Let %edi be buf + size - 8 while running the main loop. This way
	 * we can compare for equality to determine when exit the loop.
	 */
	addl	%esi, %edi
	subl	$8, %edi

	/* Read in the first eight aligned bytes. */
	xorl	(%esi), %eax
	movl	4(%esi), %ecx
	movzbl	%cl, %ebp

.L_loop:
	movl	0x0C00(%ebx, %ebp, 4), %edx
	movzbl	%ch, %ebp
	xorl	0x0800(%ebx, %ebp, 4), %edx
	shrl	$16, %ecx
	xorl	8(%esi), %edx
	movzbl	%cl, %ebp
	xorl	0x0400(%ebx, %ebp, 4), %edx
	movzbl	%ch, %ebp
	xorl	(%ebx, %ebp, 4), %edx
	movzbl	%al, %ebp

	/*
	 * Read the next four bytes, for which the CRC is calculated
	 * on the next interation of the loop.
	 */
	movl	12(%esi), %ecx

	xorl	0x1C00(%ebx, %ebp, 4), %edx
	movzbl	%ah, %ebp
	shrl	$16, %eax
	xorl	0x1800(%ebx, %ebp, 4), %edx
	movzbl	%ah, %ebp
	movzbl	%al, %eax
	movl	0x1400(%ebx, %eax, 4), %eax
	addl	$8, %esi
	xorl	%edx, %eax
	xorl	0x1000(%ebx, %ebp, 4), %eax

	/* Check for end of aligned input. */
	cmpl	%edi, %esi
	movzbl	%cl, %ebp
	jne	.L_loop

	/*
	 * Process the remaining eight bytes, which we have already
	 * copied to %ecx and %edx.
	 */
	movl	0x0C00(%ebx, %ebp, 4), %edx
	movzbl	%ch, %ebp
	xorl	0x0800(%ebx, %ebp, 4), %edx
	shrl	$16, %ecx
	movzbl	%cl, %ebp
	xorl	0x0400(%ebx, %ebp, 4), %edx
	movzbl	%ch, %ebp
	xorl	(%ebx, %ebp, 4), %edx
	movzbl	%al, %ebp

	xorl	0x1C00(%ebx, %ebp, 4), %edx
	movzbl	%ah, %ebp
	shrl	$16, %eax
	xorl	0x1800(%ebx, %ebp, 4), %edx
	movzbl	%ah, %ebp
	movzbl	%al, %eax
	movl	0x1400(%ebx, %eax, 4), %eax
	addl	$8, %esi
	xorl	%edx, %eax
	xorl	0x1000(%ebx, %ebp, 4), %eax

	/* Copy the number of remaining bytes to %edi. */
	movl	0x18(%esp), %edi

.L_rest:
	/* Check for end of input. */
	testl	%edi, %edi
	jz	.L_return

	/* Calculate CRC of the next input byte. */
	movzbl	(%esi), %ebp
	incl	%esi
	movzbl	%al, %ecx
	xorl	%ecx, %ebp
	shrl	$8, %eax
	xorl	(%ebx, %ebp, 4), %eax
	decl	%edi
	jmp	.L_rest

.L_return:
	/* Complement the final value. */
	notl	%eax

	popl	%ebp
	popl	%edi
	popl	%esi
	popl	%ebx
	ret

#if defined(PIC) || defined(__PIC__)
	ALIGN(4, 16)
.L_get_pc:
	movl	(%esp), %ebx
	ret
#endif

#if defined(__APPLE__) && (defined(PIC) || defined(__PIC__))
	/* Mach-O PIC */
	.section __IMPORT,__pointers,non_lazy_symbol_pointers
.L_lzma_crc32_table$non_lazy_ptr:
	.indirect_symbol LZMA_CRC32_TABLE
	.long 0

#elif defined(_WIN32) || defined(__CYGWIN__)
#	ifdef DLL_EXPORT
	/* This is equivalent of __declspec(dllexport). */
	.section .drectve
	.ascii " -export:lzma_crc32"
#	endif

#elif !defined(__MSDOS__)
	/* ELF */
	.size	LZMA_CRC32, .-LZMA_CRC32
#endif

/*
 * This is needed to support non-executable stack. It's ugly to
 * use __linux__ here, but I don't know a way to detect when
 * we are using GNU assembler.
 */
#if defined(__ELF__) && (defined(__FreeBSD__) || defined(__linux__))
	.section	.note.GNU-stack,"",@progbits
#endif
