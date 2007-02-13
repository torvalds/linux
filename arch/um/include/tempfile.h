/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __TEMPFILE_H__
#define __TEMPFILE_H__

extern int make_tempfile(const char *template, char **tempname, int do_unlink);

#endif
