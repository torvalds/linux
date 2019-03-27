/* Character set conversion support for GDB.
   Copyright 2001 Free Software Foundation, Inc.

   This file is part of GDB.

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
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#ifndef CHARSET_H
#define CHARSET_H


/* If the target program uses a different character set than the host,
   GDB has some support for translating between the two; GDB converts
   characters and strings to the host character set before displaying
   them, and converts characters and strings appearing in expressions
   entered by the user to the target character set.

   At the moment, GDB only supports single-byte, stateless character
   sets.  This includes the ISO-8859 family (ASCII extended with
   accented characters, and (I think) Cyrillic, for European
   languages), and the EBCDIC family (used on IBM's mainframes).
   Unfortunately, it excludes many Asian scripts, the fixed- and
   variable-width Unicode encodings, and other desireable things.
   Patches are welcome!  (For example, it would be nice if the Java
   string support could simply get absorbed into some more general
   multi-byte encoding support.)

   Furthermore, GDB's code pretty much assumes that the host character
   set is some superset of ASCII; there are plenty if ('0' + n)
   expressions and the like.

   When the `iconv' library routine supports a character set meeting
   the requirements above, it's easy to plug an entry into GDB's table
   that uses iconv to handle the details.  */

/* Return the name of the current host/target character set.  The
   result is owned by the charset module; the caller should not free
   it.  */
const char *host_charset (void);
const char *target_charset (void);

/* In general, the set of C backslash escapes (\n, \f) is specific to
   the character set.  Not all character sets will have form feed
   characters, for example.

   The following functions allow GDB to parse and print control
   characters in a character-set-independent way.  They are both
   language-specific (to C and C++) and character-set-specific.
   Putting them here is a compromise.  */


/* If the target character TARGET_CHAR have a backslash escape in the
   C language (i.e., a character like 'n' or 't'), return the host
   character string that should follow the backslash.  Otherwise,
   return zero.

   When this function returns non-zero, the string it returns is
   statically allocated; the caller is not responsible for freeing it.  */
const char *c_target_char_has_backslash_escape (int target_char);


/* If the host character HOST_CHAR is a valid backslash escape in the
   C language for the target character set, return non-zero, and set
   *TARGET_CHAR to the target character the backslash escape represents.
   Otherwise, return zero.  */
int c_parse_backslash (int host_char, int *target_char);


/* Return non-zero if the host character HOST_CHAR can be printed
   literally --- that is, if it can be readably printed as itself in a
   character or string constant.  Return zero if it should be printed
   using some kind of numeric escape, like '\031' in C, '^(25)' in
   Chill, or #25 in Pascal.  */
int host_char_print_literally (int host_char);


/* If the host character HOST_CHAR has an equivalent in the target
   character set, set *TARGET_CHAR to that equivalent, and return
   non-zero.  Otherwise, return zero.  */
int host_char_to_target (int host_char, int *target_char);


/* If the target character TARGET_CHAR has an equivalent in the host
   character set, set *HOST_CHAR to that equivalent, and return
   non-zero.  Otherwise, return zero.  */
int target_char_to_host (int target_char, int *host_char);


/* If the target character TARGET_CHAR has a corresponding control
   character (also in the target character set), set *TARGET_CTRL_CHAR
   to the control character, and return non-zero.  Otherwise, return
   zero.  */
int target_char_to_control_char (int target_char, int *target_ctrl_char);


#endif /* CHARSET_H */
