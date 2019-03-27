/* os9k.h  -  OS-9000 i386 module header definitions
   Copyright 2000 Free Software Foundation, Inc.

This file is part of GNU CC.
   
GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

#if !defined(_MODULE_H)
#define _MODULE_H

#define _MPF386

/* Size of common header less parity field.  */
#define N_M_PARITY  (sizeof(mh_com)-sizeof(unisgned short))
#define OLD_M_PARITY 46
#define M_PARITY N_M_PARITY

#ifdef _MPF68K
#define MODSYNC 0x4afc		/* Module header sync code for 680x0 processors.  */
#endif

#ifdef _MPF386
#define MODSYNC 0x4afc		/* Module header sync code for 80386 processors.  */
#endif

#define MODREV	1		/* Module format revision 1.  */
#define CRCCON	0x800063	/* CRC polynomial constant.  */

/* Module access permission values.  */
#define MP_OWNER_READ	0x0001
#define MP_OWNER_WRITE	0x0002
#define MP_OWNER_EXEC	0x0004
#define MP_GROUP_READ	0x0010
#define MP_GROUP_WRITE	0x0020
#define MP_GROUP_EXEC	0x0040
#define MP_WORLD_READ	0x0100
#define MP_WORLD_WRITE	0x0200
#define MP_WORLD_EXEC	0x0400
#define MP_WORLD_ACCESS	0x0777
#define MP_OWNER_MASK	0x000f
#define MP_GROUP_MASK	0x00f0
#define MP_WORLD_MASK	0x0f00
#define MP_SYSTM_MASK	0xf000

/* Module Type/Language values.  */
#define MT_ANY		0
#define MT_PROGRAM	0x0001
#define MT_SUBROUT	0x0002
#define MT_MULTI	0x0003
#define MT_DATA		0x0004
#define MT_TRAPLIB	0x000b
#define MT_SYSTEM	0x000c
#define MT_FILEMAN	0x000d
#define MT_DEVDRVR	0x000e 
#define MT_DEVDESC	0x000f
#define MT_MASK		0xff00

#define ML_ANY		0
#define ML_OBJECT	1
#define ML_ICODE	2
#define ML_PCODE	3
#define ML_CCODE	4
#define ML_CBLCODE	5
#define ML_FRTNCODE	6
#define ML_MASK		0x00ff

#define mktypelang(type, lang)	(((type) << 8) | (lang))

/* Module Attribute values.  */
#define MA_REENT	0x80
#define MA_GHOST	0x40
#define MA_SUPER	0x20
#define MA_MASK		0xff00
#define MR_MASK		0x00ff

#define mkattrevs(attr, revs)	(((attr) << 8) | (revs))

#define m_user 		m_owner.grp_usr.usr
#define m_group 	m_owner.grp_usr.grp
#define m_group_user	m_owner.group_user

/* Macro definitions for accessing module header fields.  */
#define MODNAME(mod) ((u_char*)((u_char*)mod + ((Mh_com)mod)->m_name))
#if 0
/* Appears not to be used, and the u_int32 typedef is gone (because it
   conflicted with a Mach header.  */
#define MODSIZE(mod) ((u_int32)((Mh_com)mod)->m_size)
#endif /* 0 */
#define MHCOM_BYTES_SIZE 80
#define N_BADMAG(a) (((a).a_info) != MODSYNC)

typedef struct mh_com
{
  /* Sync bytes ($4afc).  */
  unsigned char m_sync[2];
  unsigned char m_sysrev[2];	/* System revision check value.  */
  unsigned char m_size[4];	/* Module size.  */
  unsigned char m_owner[4];	/* Group/user id.  */
  unsigned char m_name[4];	/* Offset to module name.  */
  unsigned char m_access[2];	/* Access permissions.  */
  unsigned char m_tylan[2];	/* Type/lang.  */
  unsigned char m_attrev[2];	/* Rev/attr.  */
  unsigned char m_edit[2];	/* Edition.  */
  unsigned char m_needs[4];	/* Module hardware requirements flags. (reserved).  */
  unsigned char m_usage[4];	/* Comment string offset.  */
  unsigned char m_symbol[4];	/* Symbol table offset.  */
  unsigned char m_exec[4];	/* Offset to execution entry point.  */
  unsigned char m_excpt[4];	/* Offset to exception entry point.  */
  unsigned char m_data[4];	/* Data storage requirement.  */
  unsigned char m_stack[4];	/* Stack size.  */
  unsigned char m_idata[4];	/* Offset to initialized data.  */
  unsigned char m_idref[4];	/* Offset to data reference lists.  */
  unsigned char m_init[4];	/* Initialization routine offset.  */
  unsigned char m_term[4];	/* Termination routine offset.  */
  unsigned char m_ident[2];	/* Ident code for ident program.  */
  char          m_spare[8];	/* Reserved bytes.  */
  unsigned char m_parity[2]; 	/* Header parity.  */
} mh_com,*Mh_com;

/* Executable memory module.  */
typedef mh_com *Mh_exec,mh_exec;

/* Data memory module.  */
typedef mh_com *Mh_data,mh_data;

/* File manager memory module.  */
typedef mh_com *Mh_fman,mh_fman;

/* Device driver module.  */
typedef mh_com *Mh_drvr,mh_drvr;

/* Trap handler module.  */
typedef	mh_com mh_trap, *Mh_trap;

/* Device descriptor module.  */
typedef	mh_com *Mh_dev,mh_dev;

/* Configuration module.  */
typedef mh_com *Mh_config, mh_config;

#if 0 

#if !defined(_MODDIR_H)
/* Go get _os_fmod (and others).  */
#include <moddir.h>
#endif

error_code _os_crc (void *, u_int32, int *);
error_code _os_datmod (char *, u_int32, u_int16 *, u_int16 *, u_int32, void **, mh_data **);
error_code _os_get_moddir (void *, u_int32 *);
error_code _os_initdata (mh_com *, void *);
error_code _os_link (char **, mh_com **, void **, u_int16 *, u_int16 *);
error_code _os_linkm (mh_com *, void **, u_int16 *, u_int16 *);
error_code _os_load (char *, mh_com **, void **, u_int32, u_int16 *, u_int16 *, u_int32);
error_code _os_mkmodule (char *, u_int32, u_int16 *, u_int16 *, u_int32, void **, mh_com **, u_int32);
error_code _os_modaddr (void *, mh_com **);
error_code _os_setcrc (mh_com *);
error_code _os_slink (u_int32, char *, void **, void **, mh_com **);
error_code _os_slinkm (u_int32, mh_com *, void **, void **);
error_code _os_unlink (mh_com *);
error_code _os_unload (char *, u_int32);
error_code _os_tlink (u_int32, char *, void **, mh_trap **, void *, u_int32);
error_code _os_tlinkm (u_int32, mh_com *, void **, void *, u_int32);
error_code _os_iodel (mh_com *);
error_code _os_vmodul (mh_com *, mh_com *, u_int32);
#endif /* 0 */

#endif
