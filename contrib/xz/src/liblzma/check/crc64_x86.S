/*
 * Speed-optimized CRC64 using slicing-by-four algorithm
 *
 * This uses only i386 instructions, but it is optimized for i686 and later
 * (including e.g. Pentium II/III/IV, Athlon XP, and Core 2).
 *
 * Authors: Igor Pavlov (original CRC32 assembly code)
 *          Lasse Collin (CRC64 adaptation of the modified CRC32 code)
 *
 * This file has been put into the public domain.
 * You can do whatever you want with this file.
 *
 * This code needs lzma_crc64_table, which can be created using the
 * following C code:

uint64_t lzma_crc64_table[4][256];

void
init_table(void)
{
	// ECMA-182
	static const uint64_t poly64 = UINT64_C(0xC96C5795D7870F42);

	for (size_t s = 0; s < 4; ++s) {
		for (size_t b = 0; b < 256; ++b) {
			uint64_t r = s == 0 ? b : lzma_crc64_table[s - 1][b];

			for (size_t i = 0; i < 8; ++i) {
				if (r & 1)
					r = (r >> 1) ^ poly64;
				else
					r >>= 1;
			}

			lzma_crc64_table[s][b] = r;
		}
	}
}

 * The prototype of the CRC64 function:
 * extern uint64_t lzma_crc64(const uint8_t *buf, size_t size, uint64_t crc);
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
#define LZMA_CRC64 MAKE_SYM(__USER_LABEL_PREFIX__, lzma_crc64)
#define LZMA_CRC64_TABLE MAKE_SYM(__USER_LABEL_PREFIX__, lzma_crc64_table)

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
	.globl	LZMA_CRC64

#if !defined(__APPLE__) && !defined(_WIN32) && !defined(__CYGWIN__) \
		&& !defined(__MSDOS__)
	.type	LZMA_CRC64, @function
#endif

	ALIGN(4, 16)
LZMA_CRC64:
	/*
	 * Register usage:
	 * %eax crc LSB
	 * %edx crc MSB
	 * %esi buf
	 * %edi size or buf + size
	 * %ebx lzma_crc64_table
	 * %ebp Table index
	 * %ecx Temporary
	 */
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	pushl	%ebp
	movl	0x14(%esp), %esi /* buf */
	movl	0x18(%esp), %edi /* size */
	movl	0x1C(%esp), %eax /* crc LSB */
	movl	0x20(%esp), %edx /* crc MSB */

	/*
	 * Store the address of lzma_crc64_table to %ebx. This is needed to
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
	movl	$ LZMA_CRC64_TABLE, %ebx
#elif defined(__APPLE__)
	/* Mach-O */
	call	.L_get_pc
.L_pic:
	leal	.L_lzma_crc64_table$non_lazy_ptr-.L_pic(%ebx), %ebx
	movl	(%ebx), %ebx
#else
	/* ELF */
	call	.L_get_pc
	addl	$_GLOBAL_OFFSET_TABLE_, %ebx
	movl	LZMA_CRC64_TABLE@GOT(%ebx), %ebx
#endif

	/* Complement the initial value. */
	notl	%eax
	notl	%edx

.L_align:
	/*
	 * Check if there is enough input to use slicing-by-four.
	 * We need eight bytes, because the loop pre-reads four bytes.
	 */
	cmpl	$8, %edi
	jb	.L_rest

	/* Check if we have reached alignment of four bytes. */
	testl	$3, %esi
	jz	.L_slice

	/* Calculate CRC of the next input byte. */
	movzbl	(%esi), %ebp
	incl	%esi
	movzbl	%al, %ecx
	xorl	%ecx, %ebp
	shrdl	$8, %edx, %eax
	xorl	(%ebx, %ebp, 8), %eax
	shrl	$8, %edx
	xorl	4(%ebx, %ebp, 8), %edx
	decl	%edi
	jmp	.L_align

.L_slice:
	/*
	 * If we get here, there's at least eight bytes of aligned input
	 * available. Make %edi multiple of four bytes. Store the possible
	 * remainder over the "size" variable in the argument stack.
	 */
	movl	%edi, 0x18(%esp)
	andl	$-4, %edi
	subl	%edi, 0x18(%esp)

	/*
	 * Let %edi be buf + size - 4 while running the main loop. This way
	 * we can compare for equality to determine when exit the loop.
	 */
	addl	%esi, %edi
	subl	$4, %edi

	/* Read in the first four aligned bytes. */
	movl	(%esi), %ecx

.L_loop:
	xorl	%eax, %ecx
	movzbl	%cl, %ebp
	movl	0x1800(%ebx, %ebp, 8), %eax
	xorl	%edx, %eax
	movl	0x1804(%ebx, %ebp, 8), %edx
	movzbl	%ch, %ebp
	xorl	0x1000(%ebx, %ebp, 8), %eax
	xorl	0x1004(%ebx, %ebp, 8), %edx
	shrl	$16, %ecx
	movzbl	%cl, %ebp
	xorl	0x0800(%ebx, %ebp, 8), %eax
	xorl	0x0804(%ebx, %ebp, 8), %edx
	movzbl	%ch, %ebp
	addl	$4, %esi
	xorl	(%ebx, %ebp, 8), %eax
	xorl	4(%ebx, %ebp, 8), %edx

	/* Check for end of aligned input. */
	cmpl	%edi, %esi

	/*
	 * Copy the next input byte to %ecx. It is slightly faster to
	 * read it here than at the top of the loop.
	 */
	movl	(%esi), %ecx
	jb	.L_loop

	/*
	 * Process the remaining four bytes, which we have already
	 * copied to %ecx.
	 */
	xorl	%eax, %ecx
	movzbl	%cl, %ebp
	movl	0x1800(%ebx, %ebp, 8), %eax
	xorl	%edx, %eax
	movl	0x1804(%ebx, %ebp, 8), %edx
	movzbl	%ch, %ebp
	xorl	0x1000(%ebx, %ebp, 8), %eax
	xorl	0x1004(%ebx, %ebp, 8), %edx
	shrl	$16, %ecx
	movzbl	%cl, %ebp
	xorl	0x0800(%ebx, %ebp, 8), %eax
	xorl	0x0804(%ebx, %ebp, 8), %edx
	movzbl	%ch, %ebp
	addl	$4, %esi
	xorl	(%ebx, %ebp, 8), %eax
	xorl	4(%ebx, %ebp, 8), %edx

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
	shrdl	$8, %edx, %eax
	xorl	(%ebx, %ebp, 8), %eax
	shrl	$8, %edx
	xorl	4(%ebx, %ebp, 8), %edx
	decl	%edi
	jmp	.L_rest

.L_return:
	/* Complement the final value. */
	notl	%eax
	notl	%edx

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
.L_lzma_crc64_table$non_lazy_ptr:
	.indirect_symbol LZMA_CRC64_TABLE
	.long 0

#elif defined(_WIN32) || defined(__CYGWIN__)
#	ifdef DLL_EXPORT
	/* This is equivalent of __declspec(dllexport). */
	.section .drectve
	.ascii " -export:lzma_crc64"
#	endif

#elif !defined(__MSDOS__)
	/* ELF */
	.size	LZMA_CRC64, .-LZMA_CRC64
#endif

/*
 * This is needed to support non-executable stack. It's ugly to
 * use __linux__ here, but I don't know a way to detect when
 * we are using GNU assembler.
 */
#if defined(__ELF__) && (defined(__FreeBSD__) || defined(__linux__))
	.section	.note.GNU-stack,"",@progbits
#endif
