/*-
 * Copyright (c) 2008-2011 Joseph Koshy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: _libelf_config.h 3566 2017-08-31 02:28:40Z emaste $
 */

#if defined(__APPLE__) || defined(__DragonFly__)

#if	defined(__amd64__)
#define	LIBELF_ARCH		EM_X86_64
#define	LIBELF_BYTEORDER	ELFDATA2LSB
#define	LIBELF_CLASS		ELFCLASS64
#elif	defined(__i386__)
#define	LIBELF_ARCH		EM_386
#define	LIBELF_BYTEORDER	ELFDATA2LSB
#define	LIBELF_CLASS		ELFCLASS32
#endif

#endif	/* __DragonFly__ */

#ifdef __FreeBSD__

/*
 * Define LIBELF_{ARCH,BYTEORDER,CLASS} based on the machine architecture.
 * See also: <machine/elf.h>.
 */

#if	defined(__amd64__)

#define	LIBELF_ARCH		EM_X86_64
#define	LIBELF_BYTEORDER	ELFDATA2LSB
#define	LIBELF_CLASS		ELFCLASS64

#elif	defined(__aarch64__)

#define	LIBELF_ARCH		EM_AARCH64
#define	LIBELF_BYTEORDER	ELFDATA2LSB
#define	LIBELF_CLASS		ELFCLASS64

#elif	defined(__arm__)

#define	LIBELF_ARCH		EM_ARM
#if	defined(__ARMEB__)	/* Big-endian ARM. */
#define	LIBELF_BYTEORDER	ELFDATA2MSB
#else
#define	LIBELF_BYTEORDER	ELFDATA2LSB
#endif
#define	LIBELF_CLASS		ELFCLASS32

#elif	defined(__i386__)

#define	LIBELF_ARCH		EM_386
#define	LIBELF_BYTEORDER	ELFDATA2LSB
#define	LIBELF_CLASS		ELFCLASS32

#elif	defined(__ia64__)

#define	LIBELF_ARCH		EM_IA_64
#define	LIBELF_BYTEORDER	ELFDATA2LSB
#define	LIBELF_CLASS		ELFCLASS64

#elif	defined(__mips__)

#define	LIBELF_ARCH		EM_MIPS
#if	defined(__MIPSEB__)
#define	LIBELF_BYTEORDER	ELFDATA2MSB
#else
#define	LIBELF_BYTEORDER	ELFDATA2LSB
#endif
#define	LIBELF_CLASS		ELFCLASS32

#elif	defined(__powerpc__)

#define	LIBELF_ARCH		EM_PPC
#define	LIBELF_BYTEORDER	ELFDATA2MSB
#define	LIBELF_CLASS		ELFCLASS32

#elif	defined(__riscv) && (__riscv_xlen == 64)

#define	LIBELF_ARCH		EM_RISCV
#define	LIBELF_BYTEORDER	ELFDATA2LSB
#define	LIBELF_CLASS		ELFCLASS64

#elif	defined(__sparc__)

#define	LIBELF_ARCH		EM_SPARCV9
#define	LIBELF_BYTEORDER	ELFDATA2MSB
#define	LIBELF_CLASS		ELFCLASS64

#else
#error	Unknown FreeBSD architecture.
#endif
#endif  /* __FreeBSD__ */

/*
 * Definitions for Minix3.
 */
#ifdef __minix

#define	LIBELF_ARCH		EM_386
#define	LIBELF_BYTEORDER	ELFDATA2LSB
#define	LIBELF_CLASS		ELFCLASS32

#endif	/* __minix */

#ifdef __NetBSD__

#include <machine/elf_machdep.h>

#if	!defined(ARCH_ELFSIZE)
#error	ARCH_ELFSIZE is not defined.
#endif

#if	ARCH_ELFSIZE == 32
#define	LIBELF_ARCH		ELF32_MACHDEP_ID
#define	LIBELF_BYTEORDER	ELF32_MACHDEP_ENDIANNESS
#define	LIBELF_CLASS		ELFCLASS32
#define	Elf_Note		Elf32_Nhdr
#else
#define	LIBELF_ARCH		ELF64_MACHDEP_ID
#define	LIBELF_BYTEORDER	ELF64_MACHDEP_ENDIANNESS
#define	LIBELF_CLASS		ELFCLASS64
#define	Elf_Note		Elf64_Nhdr
#endif

#endif	/* __NetBSD__ */

#if defined(__OpenBSD__)

#include <machine/exec.h>

#define	LIBELF_ARCH		ELF_TARG_MACH
#define	LIBELF_BYTEORDER	ELF_TARG_DATA
#define	LIBELF_CLASS		ELF_TARG_CLASS

#endif

/*
 * GNU & Linux compatibility.
 *
 * `__linux__' is defined in an environment runs the Linux kernel and glibc.
 * `__GNU__' is defined in an environment runs a GNU kernel (Hurd) and glibc.
 * `__GLIBC__' is defined for an environment that runs glibc over a non-GNU
 *     kernel such as GNU/kFreeBSD.
 */

#if defined(__linux__) || defined(__GNU__) || defined(__GLIBC__)

#if defined(__linux__)

#include "native-elf-format.h"

#define	LIBELF_CLASS		ELFTC_CLASS
#define	LIBELF_ARCH		ELFTC_ARCH
#define	LIBELF_BYTEORDER	ELFTC_BYTEORDER

#endif	/* defined(__linux__) */

#if	LIBELF_CLASS == ELFCLASS32
#define	Elf_Note		Elf32_Nhdr
#elif   LIBELF_CLASS == ELFCLASS64
#define	Elf_Note		Elf64_Nhdr
#else
#error  LIBELF_CLASS needs to be one of ELFCLASS32 or ELFCLASS64
#endif

#endif /* defined(__linux__) || defined(__GNU__) || defined(__GLIBC__) */
