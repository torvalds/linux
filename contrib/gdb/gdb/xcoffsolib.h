/* Data structures for RS/6000 shared libraries, for GDB.
   Copyright 1991, 1992, 1993, 1994, 1996, 1997, 2000
   Free Software Foundation, Inc.

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

/* The vmap struct is used to describe the virtual address space of
   the target we are manipulating.  The first entry is always the "exec"
   file.  Subsequent entries correspond to other objects that are
   mapped into the address space of a process created from the "exec" file.
   These are either in response to exec()ing the file, in which case all
   shared libraries are loaded, or a "load" system call, followed by the
   user's issuance of a "load" command.  */

#ifndef XCOFFSOLIB_H
#define XCOFFSOLIB_H

struct vmap
  {
    struct vmap *nxt;		/* ptr to next in chain                 */
    bfd *bfd;			/* BFD for mappable object library      */
    char *name;			/* ptr to object file name              */
    char *member;		/* ptr to member name                   */
    CORE_ADDR tstart;		/* virtual addr where member is mapped  */
    CORE_ADDR tend;		/* virtual upper bound of member        */
    CORE_ADDR tvma;		/* virtual addr of text section in object file */
    CORE_ADDR toffs;		/* offset of text section in object file */
    CORE_ADDR dstart;		/* virtual address of data start        */
    CORE_ADDR dend;		/* virtual address of data end          */
    CORE_ADDR dvma;		/* virtual addr of data section in object file */

    /* This is NULL for the exec-file.  */
    struct objfile *objfile;

    unsigned loaded:1;		/* True if symbols are loaded           */
    unsigned padding:15;
  };


struct vmap_and_bfd
  {
    bfd *pbfd;
    struct vmap *pvmap;
  };

extern struct vmap *vmap;

/* Add symbols for a vmap.  */
extern int vmap_add_symbols (struct vmap *vp);

#endif
