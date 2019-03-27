/* Remote File-I/O communications

   Copyright 2003 Free Software Foundation, Inc.

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

/* See the GDB User Guide for details of the GDB remote protocol. */

#ifndef REMOTE_FILEIO_H
#define REMOTE_FILEIO_H

struct cmd_list_element;

/* Unified interface to remote fileio, called in remote.c from
   remote_wait () and remote_async_wait () */
extern void remote_fileio_request (char *buf);

/* Called from _initialize_remote () */
extern void initialize_remote_fileio (
  struct cmd_list_element *remote_set_cmdlist,
  struct cmd_list_element *remote_show_cmdlist);

#endif
