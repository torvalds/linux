/* GDB variable objects API.
   Copyright 1999, 2000 Free Software Foundation, Inc.

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

#ifndef VAROBJ_H
#define VAROBJ_H 1

#include "symtab.h"
#include "gdbtypes.h"

/* Enumeration for the format types */
enum varobj_display_formats
  {
    FORMAT_NATURAL,		/* What gdb actually calls 'natural' */
    FORMAT_BINARY,		/* Binary display                    */
    FORMAT_DECIMAL,		/* Decimal display                   */
    FORMAT_HEXADECIMAL,		/* Hex display                       */
    FORMAT_OCTAL		/* Octal display                     */
  };

enum varobj_type
  {
    USE_SPECIFIED_FRAME,        /* Use the frame passed to varobj_create */
    USE_CURRENT_FRAME,          /* Use the current frame */
    USE_SELECTED_FRAME          /* Always reevaluate in selected frame */
  };
    
/* String representations of gdb's format codes (defined in varobj.c) */
extern char *varobj_format_string[];

/* Languages supported by this variable objects system. */
enum varobj_languages
  {
    vlang_unknown = 0, vlang_c, vlang_cplus, vlang_java, vlang_end
  };

/* String representations of gdb's known languages (defined in varobj.c) */
extern char *varobj_language_string[];

/* Struct thar describes a variable object instance */
struct varobj;

/* API functions */

extern struct varobj *varobj_create (char *objname,
				     char *expression, CORE_ADDR frame,
				     enum varobj_type type);

extern char *varobj_gen_name (void);

extern struct varobj *varobj_get_handle (char *name);

extern char *varobj_get_objname (struct varobj *var);

extern char *varobj_get_expression (struct varobj *var);

extern int varobj_delete (struct varobj *var, char ***dellist,
			  int only_children);

extern enum varobj_display_formats varobj_set_display_format (
							 struct varobj *var,
					enum varobj_display_formats format);

extern enum varobj_display_formats varobj_get_display_format (
							struct varobj *var);

extern int varobj_get_num_children (struct varobj *var);

extern int varobj_list_children (struct varobj *var,
				 struct varobj ***childlist);

extern char *varobj_get_type (struct varobj *var);

extern enum varobj_languages varobj_get_language (struct varobj *var);

extern int varobj_get_attributes (struct varobj *var);

extern char *varobj_get_value (struct varobj *var);

extern int varobj_set_value (struct varobj *var, char *expression);

extern int varobj_list (struct varobj ***rootlist);

extern int varobj_update (struct varobj **varp, struct varobj ***changelist);

#endif /* VAROBJ_H */
