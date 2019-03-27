/*-
 * Copyright (c) 2009 Kai Wang
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
 *
 * $FreeBSD: users/kaiwang27/elftc/libelftc.h 392 2009-05-31 19:17:46Z kaiwang27 $
 * $Id: libelftc.h 3489 2016-08-31 00:12:15Z emaste $
 */

#ifndef	_LIBELFTC_H_
#define	_LIBELFTC_H_

#include <sys/stat.h>

#include <libelf.h>

/*
 * Types meant to be opaque to the consumers of these APIs.
 */
typedef struct _Elftc_Bfd_Target Elftc_Bfd_Target;
typedef struct _Elftc_String_Table Elftc_String_Table;

/* Target types. */
typedef enum {
	ETF_NONE,
	ETF_ELF,
	ETF_BINARY,
	ETF_SREC,
	ETF_IHEX,
	ETF_PE,
	ETF_EFI,
} Elftc_Bfd_Target_Flavor;

/*
 * Demangler flags.
 */

/* Name mangling style. */
#define	ELFTC_DEM_UNKNOWN	0x00000000U /* Not specified. */
#define	ELFTC_DEM_ARM		0x00000001U /* C++ Ann. Ref. Manual. */
#define	ELFTC_DEM_GNU2		0x00000002U /* GNU version 2. */
#define	ELFTC_DEM_GNU3		0x00000004U /* GNU version 3. */

/* Demangling behaviour control. */
#define ELFTC_DEM_NOPARAM	0x00010000U

#ifdef __cplusplus
extern "C" {
#endif
Elftc_Bfd_Target	*elftc_bfd_find_target(const char *_tgt_name);
Elftc_Bfd_Target_Flavor	 elftc_bfd_target_flavor(Elftc_Bfd_Target *_tgt);
unsigned int	elftc_bfd_target_byteorder(Elftc_Bfd_Target *_tgt);
unsigned int	elftc_bfd_target_class(Elftc_Bfd_Target *_tgt);
unsigned int	elftc_bfd_target_machine(Elftc_Bfd_Target *_tgt);
int		elftc_copyfile(int _srcfd,  int _dstfd);
int		elftc_demangle(const char *_mangledname, char *_buffer,
    size_t _bufsize, unsigned int _flags);
const char	*elftc_reloc_type_str(unsigned int mach, unsigned int type);
int		elftc_set_timestamps(const char *_filename, struct stat *_sb);
Elftc_String_Table	*elftc_string_table_create(int _hint);
void		elftc_string_table_destroy(Elftc_String_Table *_table);
Elftc_String_Table	*elftc_string_table_from_section(Elf_Scn *_scn,
    int _hint);
const char	*elftc_string_table_image(Elftc_String_Table *_table,
    size_t *_sz);
size_t		elftc_string_table_insert(Elftc_String_Table *_table,
    const char *_string);
size_t		elftc_string_table_lookup(Elftc_String_Table *_table,
    const char *_string);
int		elftc_string_table_remove(Elftc_String_Table *_table,
    const char *_string);
const char	*elftc_string_table_to_string(Elftc_String_Table *_table,
    size_t offset);
int		elftc_timestamp(time_t *_timestamp);
const char	*elftc_version(void);
#ifdef __cplusplus
}
#endif

#endif	/* _LIBELFTC_H_ */
