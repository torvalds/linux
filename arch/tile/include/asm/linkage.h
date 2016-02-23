/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_LINKAGE_H
#define _ASM_TILE_LINKAGE_H

#include <feedback.h>

#define __ALIGN .align 8

/*
 * The STD_ENTRY and STD_ENDPROC macros put the function in a
 * self-named .text.foo section, and if linker feedback collection
 * is enabled, add a suitable call to the feedback collection code.
 * STD_ENTRY_SECTION lets you specify a non-standard section name.
 */

#define STD_ENTRY(name) \
  .pushsection .text.##name, "ax"; \
  ENTRY(name); \
  FEEDBACK_ENTER(name)

#define STD_ENTRY_SECTION(name, section) \
  .pushsection section, "ax"; \
  ENTRY(name); \
  FEEDBACK_ENTER_EXPLICIT(name, section, .Lend_##name - name)

#define STD_ENDPROC(name) \
  ENDPROC(name); \
  .Lend_##name:; \
  .popsection

/* Create a file-static function entry set up for feedback gathering. */
#define STD_ENTRY_LOCAL(name) \
  .pushsection .text.##name, "ax"; \
  ALIGN; \
  name:; \
  FEEDBACK_ENTER(name)

#endif /* _ASM_TILE_LINKAGE_H */
