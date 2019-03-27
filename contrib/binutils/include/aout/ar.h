/* archive file definition for GNU software

   Copyright 2001 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/* So far this is correct for BSDish archives.  Don't forget that
   files must begin on an even byte boundary. */

#ifndef __GNU_AR_H__
#define __GNU_AR_H__

/* Note that the usual '\n' in magic strings may translate to different
   characters, as allowed by ANSI.  '\012' has a fixed value, and remains
   compatible with existing BSDish archives. */

#define ARMAG  "!<arch>\012"	/* For COFF and a.out archives */
#define ARMAGB "!<bout>\012"	/* For b.out archives */
#define SARMAG 8
#define ARFMAG "`\012"

/* The ar_date field of the armap (__.SYMDEF) member of an archive
   must be greater than the modified date of the entire file, or 
   BSD-derived linkers complain.  We originally write the ar_date with
   this offset from the real file's mod-time.  After finishing the
   file, we rewrite ar_date if it's not still greater than the mod date.  */

#define ARMAP_TIME_OFFSET       60

struct ar_hdr {
  char ar_name[16];		/* name of this member */
  char ar_date[12];		/* file mtime */
  char ar_uid[6];		/* owner uid; printed as decimal */
  char ar_gid[6];		/* owner gid; printed as decimal */
  char ar_mode[8];		/* file mode, printed as octal   */
  char ar_size[10];		/* file size, printed as decimal */
  char ar_fmag[2];		/* should contain ARFMAG */
};

#endif /* __GNU_AR_H__ */
