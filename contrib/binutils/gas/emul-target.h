/* emul-target.h.  Default values for struct emulation defined in emul.h
   Copyright 1995 Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef emul_init
#define emul_init			common_emul_init
#endif

#ifndef emul_bfd_name
#define emul_bfd_name			default_emul_bfd_name
#endif

#ifndef emul_local_labels_fb
#define emul_local_labels_fb		0
#endif

#ifndef emul_local_labels_dollar
#define emul_local_labels_dollar	0
#endif

#ifndef emul_leading_underscore
#define emul_leading_underscore		2
#endif

#ifndef emul_strip_underscore
#define emul_strip_underscore		0
#endif

#ifndef emul_default_endian
#define emul_default_endian		2
#endif

#ifndef emul_fake_label_name
#define emul_fake_label_name		0
#endif

struct emulation emul_struct_name =
  {
    0,
    emul_name,
    emul_init,
    emul_bfd_name,
    emul_local_labels_fb, emul_local_labels_dollar,
    emul_leading_underscore, emul_strip_underscore,
    emul_default_endian,
    emul_fake_label_name,
    emul_format,
  };
