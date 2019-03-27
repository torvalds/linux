/*-
 * Copyright (c) 2008,2009 Kai Wang
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <libelf.h>
#include <libelftc.h>

#include "_libelftc.h"

ELFTC_VCSID("$Id: libelftc_bfdtarget.c 3516 2017-02-10 02:33:08Z emaste $");

struct _Elftc_Bfd_Target _libelftc_targets[] = {

	{
		.bt_name = "binary",
		.bt_type = ETF_BINARY,
	},

	{
		.bt_name      = "elf32-avr",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS32,
		.bt_machine   = EM_AVR,
	},

	{
		.bt_name      = "elf32-big",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS32,
	},

	{
		.bt_name      = "elf32-bigarm",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS32,
		.bt_machine   = EM_ARM,
	},

	{
		.bt_name      = "elf32-bigmips",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS32,
		.bt_machine   = EM_MIPS,
	},

	{
		.bt_name      = "elf32-i386",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS32,
		.bt_machine   = EM_386,
	},

	{
		.bt_name      = "elf32-i386-freebsd",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS32,
		.bt_machine   = EM_386,
		.bt_osabi     = ELFOSABI_FREEBSD,
	},

	{
		.bt_name      = "elf32-ia64-big",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS32,
		.bt_machine   = EM_IA_64,
	},

	{
		.bt_name      = "elf32-little",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS32,
	},

	{
		.bt_name      = "elf32-littlearm",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS32,
		.bt_machine   = EM_ARM,
	},

	{
		.bt_name      = "elf32-littlemips",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS32,
		.bt_machine   = EM_MIPS,
	},

	{
		.bt_name      = "elf32-powerpc",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS32,
		.bt_machine   = EM_PPC,
	},

	{
		.bt_name      = "elf32-powerpc-freebsd",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS32,
		.bt_machine   = EM_PPC,
		.bt_osabi     = ELFOSABI_FREEBSD,
	},

	{
		.bt_name      = "elf32-powerpcle",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS32,
		.bt_machine   = EM_PPC,
	},

	{
		.bt_name      = "elf32-sh",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS32,
		.bt_machine   = EM_SH,
	},

	{
		.bt_name      = "elf32-shl",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS32,
		.bt_machine   = EM_SH,
	},

	{
		.bt_name      = "elf32-sh-nbsd",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS32,
		.bt_machine   = EM_SH,
		.bt_osabi     = ELFOSABI_NETBSD,
	},

	{
		.bt_name      = "elf32-shl-nbsd",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS32,
		.bt_machine   = EM_SH,
		.bt_osabi     = ELFOSABI_NETBSD,
	},

	{
		.bt_name      = "elf32-shbig-linux",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS32,
		.bt_machine   = EM_SH,
		.bt_osabi     = ELFOSABI_LINUX,
	},

	{
		.bt_name      = "elf32-sh-linux",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS32,
		.bt_machine   = EM_SH,
		.bt_osabi     = ELFOSABI_LINUX,
	},

	{
		.bt_name      = "elf32-sparc",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS32,
		.bt_machine   = EM_SPARC,
	},

	{
		.bt_name      = "elf32-tradbigmips",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS32,
		.bt_machine   = EM_MIPS,
	},

	{
		.bt_name      = "elf32-tradlittlemips",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS32,
		.bt_machine   = EM_MIPS,
	},

	{
		.bt_name      = "elf64-alpha",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_ALPHA,
	},

	{
		.bt_name      = "elf64-alpha-freebsd",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_ALPHA,
		.bt_osabi     = ELFOSABI_FREEBSD
	},

	{
		.bt_name      = "elf64-big",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS64,
	},

	{
		.bt_name      = "elf64-bigmips",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_MIPS,
	},

	{
		.bt_name      = "elf64-ia64-big",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_IA_64,
	},

	{
		.bt_name      = "elf64-ia64-little",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_IA_64,
	},

	{
		.bt_name      = "elf64-little",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS64,
	},

	{
		.bt_name      = "elf64-littleaarch64",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_AARCH64,
	},

	{
		.bt_name      = "elf64-littlemips",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_MIPS,
	},

	{
		.bt_name      = "elf64-powerpc",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_PPC64,
	},

	{
		.bt_name      = "elf64-powerpc-freebsd",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_PPC64,
		.bt_osabi     = ELFOSABI_FREEBSD,
	},

	{
		.bt_name      = "elf64-powerpcle",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_PPC64,
	},

	{
		.bt_name      = "elf64-sh64",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_SH,
	},

	{
		.bt_name      = "elf64-sh64l",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_SH,
	},

	{
		.bt_name      = "elf64-sh64-nbsd",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_SH,
		.bt_osabi     = ELFOSABI_NETBSD,
	},

	{
		.bt_name      = "elf64-sh64l-nbsd",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_SH,
		.bt_osabi     = ELFOSABI_NETBSD,
	},

	{
		.bt_name      = "elf64-sh64big-linux",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_SH,
		.bt_osabi     = ELFOSABI_LINUX,
	},

	{
		.bt_name      = "elf64-sh64-linux",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_SH,
		.bt_osabi     = ELFOSABI_LINUX,
	},

	{
		.bt_name      = "elf64-sparc",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_SPARCV9,
	},

	{
		.bt_name      = "elf64-sparc-freebsd",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_SPARCV9,
		.bt_osabi     = ELFOSABI_FREEBSD
	},

	{
		.bt_name      = "elf64-tradbigmips",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2MSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_MIPS,
	},

	{
		.bt_name      = "elf64-tradlittlemips",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_MIPS,
	},

	{
		.bt_name      = "elf64-x86-64",
		.bt_type      = ETF_ELF,
		.bt_byteorder =	ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_X86_64,
	},

	{
		.bt_name      = "elf64-x86-64-freebsd",
		.bt_type      = ETF_ELF,
		.bt_byteorder = ELFDATA2LSB,
		.bt_elfclass  = ELFCLASS64,
		.bt_machine   = EM_X86_64,
		.bt_osabi     = ELFOSABI_FREEBSD
	},

	{
		.bt_name = "ihex",
		.bt_type = ETF_IHEX,
	},

	{
		.bt_name = "srec",
		.bt_type = ETF_SREC,
	},

	{
		.bt_name = "symbolsrec",
		.bt_type = ETF_SREC,
	},

	{
		.bt_name    = "efi-app-ia32",
		.bt_type    = ETF_EFI,
		.bt_machine = EM_386,
	},

	{
		.bt_name    = "efi-app-x86_64",
		.bt_type    = ETF_EFI,
		.bt_machine = EM_X86_64,
	},

	{
		.bt_name    = "pei-i386",
		.bt_type    = ETF_PE,
		.bt_machine = EM_386,
	},

	{
		.bt_name    = "pei-x86-64",
		.bt_type    = ETF_PE,
		.bt_machine = EM_X86_64,
	},

	{
		.bt_name = NULL,
		.bt_type = ETF_NONE,
	},
};
