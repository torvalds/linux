/* windint.h -- internal header file for windres program.
   Copyright 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2005, 2007
   Free Software Foundation, Inc.
   Written by Kai Tietz, Onevision.

   This file is part of GNU Binutils.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "winduni.h"

#ifndef WINDINT_H
#define WINDINT_H

/* Use bfd_size_type to ensure a sufficient number of bits.  */
#ifndef DEFINED_RC_UINT_TYPE
#define DEFINED_RC_UINT_TYPE
typedef bfd_size_type rc_uint_type;
#endif

/* Resource directory structure.  */

typedef struct res_hdr
{
  rc_uint_type data_size;
  rc_uint_type header_size;
} res_hdr;

struct __attribute__ ((__packed__)) bin_res_hdr
{
  bfd_byte data_size[4];
  bfd_byte header_size[4];
};
#define BIN_RES_HDR_SIZE 8

struct __attribute__ ((__packed__)) bin_res_id
{
  bfd_byte sig[2]; /* Has to be 0xffff for unnamed ids.  */
  bfd_byte id[2];
};
#define BIN_RES_ID  4

/* This structure is used when converting resource information to
   binary.  */

typedef struct bindata
{
  /* Next data.  */
  struct bindata *next;
  /* Length of data.  */
  rc_uint_type length;
  /* Data.  */
  bfd_byte *data;
} bindata;

/* This structure is used when converting resource information to
   coff.  */
typedef struct coff_res_data
{
  /* Next data.  */
  struct coff_res_data *next;
  /* Length of data.  */
  rc_uint_type length;
  /* Data.  */
  const struct rc_res_resource *res;
} coff_res_data;

/* We represent resources internally as a tree, similar to the tree
   used in the .rsrc section of a COFF file.  The root is a
   rc_res_directory structure.  */

typedef struct rc_res_directory
{
  /* Resource flags.  According to the MS docs, this is currently
     always zero.  */
  rc_uint_type characteristics;
  /* Time/date stamp.  */
  rc_uint_type time;
  /* Major version number.  */
  rc_uint_type major;
  /* Minor version number.  */
  rc_uint_type minor;
  /* Directory entries.  */
  struct rc_res_entry *entries;
} rc_res_directory;

/* A resource ID is stored in a rc_res_id structure.  */

typedef struct rc_res_id
{
  /* Non-zero if this entry has a name rather than an ID.  */
  rc_uint_type named : 1;
  union
  {
    /* If the named field is non-zero, this is the name.  */
    struct
    {
      /* Length of the name.  */
      rc_uint_type length;
      /* Pointer to the name, which is a Unicode string.  */
      unichar *name;
    } n;
    /* If the named field is zero, this is the ID.  */
    rc_uint_type id;
  } u;
} rc_res_id;

/* Each entry in the tree is a rc_res_entry structure.  We mix
   directories and resources because in a COFF file all entries in a
   directory are sorted together, whether the entries are
   subdirectories or resources.  */

typedef struct rc_res_entry
{
  /* Next entry.  */
  struct rc_res_entry *next;
  /* Resource ID.  */
  rc_res_id id;
  /* Non-zero if this entry is a subdirectory rather than a leaf.  */
  rc_uint_type subdir : 1;
  union
  {
    /* If the subdir field is non-zero, this is a pointer to the
       subdirectory.  */
    rc_res_directory *dir;
    /* If the subdir field is zero, this is a pointer to the resource
       data.  */
    struct rc_res_resource *res;
  } u;
} rc_res_entry;

/* Types of resources.  */

enum rc_res_type
{
  RES_TYPE_UNINITIALIZED,
  RES_TYPE_ACCELERATOR,
  RES_TYPE_BITMAP,
  RES_TYPE_CURSOR,
  RES_TYPE_GROUP_CURSOR,
  RES_TYPE_DIALOG,
  RES_TYPE_FONT,
  RES_TYPE_FONTDIR,
  RES_TYPE_ICON,
  RES_TYPE_GROUP_ICON,
  RES_TYPE_MENU,
  RES_TYPE_MESSAGETABLE,
  RES_TYPE_RCDATA,
  RES_TYPE_STRINGTABLE,
  RES_TYPE_USERDATA,
  RES_TYPE_VERSIONINFO,
  RES_TYPE_DLGINCLUDE,
  RES_TYPE_PLUGPLAY,
  RES_TYPE_VXD,
  RES_TYPE_ANICURSOR,
  RES_TYPE_ANIICON,
  RES_TYPE_DLGINIT,
  RES_TYPE_TOOLBAR
};

/* A res file and a COFF file store information differently.  The
   res_info structures holds data which in a res file is stored with
   each resource, but in a COFF file is stored elsewhere.  */

typedef struct rc_res_res_info
{
  /* Language.  In a COFF file, the third level of the directory is
     keyed by the language, so the language of a resource is defined
     by its location in the resource tree.  */
  rc_uint_type language;
  /* Characteristics of the resource.  Entirely user defined.  In a
     COFF file, the rc_res_directory structure has a characteristics
     field, but I don't know if it's related to the one in the res
     file.  */
  rc_uint_type characteristics;
  /* Version of the resource.  Entirely user defined.  In a COFF file,
     the rc_res_directory structure has a characteristics field, but I
     don't know if it's related to the one in the res file.  */
  rc_uint_type version;
  /* Memory flags.  This is a combination of the MEMFLAG values
     defined below.  Most of these values are historical, and are not
     meaningful for win32.  I don't think there is any way to store
     this information in a COFF file.  */
  rc_uint_type memflags;
} rc_res_res_info;

/* Binary layout of rc_res_info.  */

struct __attribute__ ((__packed__)) bin_res_info
{
  bfd_byte version[4];
  bfd_byte memflags[2];
  bfd_byte language[2];
  bfd_byte version2[4];
  bfd_byte characteristics[4];
};
#define BIN_RES_INFO_SIZE 16

/* Each resource in a COFF file has some information which can does
   not appear in a res file.  */

typedef struct rc_res_coff_info
{
  /* The code page used for the data.  I don't really know what this
     should be.  It has something todo with ASCII to Unicode encoding.  */
  rc_uint_type codepage;
  /* A resource entry in a COFF file has a reserved field, which we
     record here when reading a COFF file.  When writing a COFF file,
     we set this field to zero.  */
  rc_uint_type reserved;
} rc_res_coff_info;

/* Resource data is stored in a rc_res_resource structure.  */

typedef struct rc_res_resource
{
  /* The type of resource.  */
  enum rc_res_type type;
  /* The data for the resource.  */
  union
  {
    struct
    {
      rc_uint_type length;
      const bfd_byte *data;
    } data;
    struct rc_accelerator *acc;
    struct rc_cursor *cursor;
    struct rc_group_cursor *group_cursor;
    struct rc_dialog *dialog;
    struct rc_fontdir *fontdir;
    struct rc_group_icon *group_icon;
    struct rc_menu *menu;
    struct rc_rcdata_item *rcdata;
    struct rc_stringtable *stringtable;
    struct rc_rcdata_item *userdata;
    struct rc_versioninfo *versioninfo;
    struct rc_toolbar *toolbar;
  } u;
  /* Information from a res file.  */
  struct rc_res_res_info res_info;
  /* Information from a COFF file.  */
  rc_res_coff_info coff_info;
} rc_res_resource;

#define SUBLANG_SHIFT 10

/* Memory flags in the memflags field of a rc_res_resource.  */

#define MEMFLAG_MOVEABLE	0x10
#define MEMFLAG_PURE		0x20
#define MEMFLAG_PRELOAD		0x40
#define MEMFLAG_DISCARDABLE	0x1000

/* Standard resource type codes.  These are used in the ID field of a
   rc_res_entry structure.  */

#define RT_CURSOR		 1
#define RT_BITMAP		 2
#define RT_ICON			 3
#define RT_MENU			 4
#define RT_DIALOG		 5
#define RT_STRING		 6
#define RT_FONTDIR		 7
#define RT_FONT			 8
#define RT_ACCELERATOR		 9
#define RT_RCDATA		10
#define RT_MESSAGETABLE		11
#define RT_GROUP_CURSOR		12
#define RT_GROUP_ICON		14
#define RT_VERSION		16
#define RT_DLGINCLUDE		17
#define RT_PLUGPLAY		19
#define RT_VXD			20
#define RT_ANICURSOR		21
#define RT_ANIICON		22
#define RT_HTML			23
#define RT_MANIFEST		24
#define RT_DLGINIT		240
#define RT_TOOLBAR		241

/* An accelerator resource is a linked list of these structures.  */

typedef struct rc_accelerator
{
  /* Next accelerator.  */
  struct rc_accelerator *next;
  /* Flags.  A combination of the ACC values defined below.  */
  rc_uint_type flags;
  /* Key value.  */
  rc_uint_type key;
  /* Resource ID.  */
  rc_uint_type id;
} rc_accelerator;

struct __attribute__ ((__packed__)) bin_accelerator
{
  bfd_byte flags[2];
  bfd_byte key[2];
  bfd_byte id[2];
  bfd_byte pad[2];
};
#define BIN_ACCELERATOR_SIZE  8

/* Accelerator flags in the flags field of a rc_accelerator.
   These are the same values that appear in a res file.  I hope.  */

#define ACC_VIRTKEY	0x01
#define ACC_NOINVERT	0x02
#define ACC_SHIFT	0x04
#define ACC_CONTROL	0x08
#define ACC_ALT		0x10
#define ACC_LAST	0x80

/* A cursor resource.  */

typedef struct rc_cursor
{
  /* X coordinate of hotspot.  */
  bfd_signed_vma xhotspot;
  /* Y coordinate of hotspot.  */
  bfd_signed_vma yhotspot;
  /* Length of bitmap data.  */
  rc_uint_type length;
  /* Data.  */
  const bfd_byte *data;
} rc_cursor;

struct __attribute__ ((__packed__)) bin_cursor
{
  bfd_byte xhotspot[2];
  bfd_byte yhotspot[2];
};
#define BIN_CURSOR_SIZE 4

/* A group_cursor resource is a list of rc_i_group_cursor structures.  */

typedef struct rc_group_cursor
{
  /* Next cursor in group.  */
  struct rc_group_cursor *next;
  /* Width.  */
  rc_uint_type width;
  /* Height.  */
  rc_uint_type height;
  /* Planes.  */
  rc_uint_type planes;
  /* Bits per pixel.  */
  rc_uint_type bits;
  /* Number of bytes in cursor resource.  */
  rc_uint_type bytes;
  /* Index of cursor resource.  */
  rc_uint_type index;
} rc_group_cursor;

struct __attribute__ ((__packed__)) bin_group_cursor_item
{
  bfd_byte width[2];
  bfd_byte height[2];
  bfd_byte planes[2];
  bfd_byte bits[2];
  bfd_byte bytes[4];
  bfd_byte index[2];
};
#define BIN_GROUP_CURSOR_ITEM_SIZE 14

struct __attribute__ ((__packed__)) bin_group_cursor
{
  bfd_byte sig1[2];
  bfd_byte sig2[2];
  bfd_byte nitems[2];
  /* struct bin_group_cursor_item item[nitems]; */
};
#define BIN_GROUP_CURSOR_SIZE 6

/* A dialog resource.  */

typedef struct rc_dialog
{
  /* Basic window style.  */
  unsigned int style;
  /* Extended window style.  */
  rc_uint_type exstyle;
  /* X coordinate.  */
  rc_uint_type x;
  /* Y coordinate.  */
  rc_uint_type y;
  /* Width.  */
  rc_uint_type width;
  /* Height.  */
  rc_uint_type height;
  /* Menu name.  */
  rc_res_id menu;
  /* Class name.  */
  rc_res_id class;
  /* Caption.  */
  unichar *caption;
  /* Font point size.  */
  rc_uint_type pointsize;
  /* Font name.  */
  unichar *font;
  /* Extended information for a dialogex.  */
  struct rc_dialog_ex *ex;
  /* Controls.  */
  struct rc_dialog_control *controls;
} rc_dialog;

struct __attribute__ ((__packed__)) bin_dialog
{
  bfd_byte style[4];
  bfd_byte exstyle[4];
  bfd_byte off[2];
  bfd_byte x[2];
  bfd_byte y[2];
  bfd_byte width[2];
  bfd_byte height[2];
};
#define BIN_DIALOG_SIZE 18

/* An extended dialog has additional information.  */

typedef struct rc_dialog_ex
{
  /* Help ID.  */
  rc_uint_type help;
  /* Font weight.  */
  rc_uint_type weight;
  /* Whether the font is italic.  */
  bfd_byte italic;
  /* Character set.  */
  bfd_byte charset;
} rc_dialog_ex;

struct __attribute__ ((__packed__)) bin_dialogex
{
  bfd_byte sig1[2];
  bfd_byte sig2[2];
  bfd_byte help[4];
  bfd_byte exstyle[4];
  bfd_byte style[4];
  bfd_byte off[2];
  bfd_byte x[2];
  bfd_byte y[2];
  bfd_byte width[2];
  bfd_byte height[2];
};
#define BIN_DIALOGEX_SIZE 26

struct __attribute__ ((__packed__)) bin_dialogfont
{
  bfd_byte pointsize[2];
};
#define BIN_DIALOGFONT_SIZE 2

struct __attribute__ ((__packed__)) bin_dialogexfont
{
  bfd_byte pointsize[2];
  bfd_byte weight[2];
  bfd_byte italic[1];
  bfd_byte charset[1];
};
#define BIN_DIALOGEXFONT_SIZE 6

/* Window style flags, from the winsup Defines.h header file.  These
   can appear in the style field of a rc_dialog or a rc_dialog_control.  */

#define CW_USEDEFAULT	0x80000000
#define WS_BORDER	0x800000L
#define WS_CAPTION	0xc00000L
#define WS_CHILD	0x40000000L
#define WS_CHILDWINDOW	0x40000000L
#define WS_CLIPCHILDREN	0x2000000L
#define WS_CLIPSIBLINGS	0x4000000L
#define WS_DISABLED	0x8000000L
#define WS_DLGFRAME	0x400000L
#define WS_GROUP	0x20000L
#define WS_HSCROLL	0x100000L
#define WS_ICONIC	0x20000000L
#define WS_MAXIMIZE	0x1000000L
#define WS_MAXIMIZEBOX	0x10000L
#define WS_MINIMIZE	0x20000000L
#define WS_MINIMIZEBOX	0x20000L
#define WS_OVERLAPPED	0L
#define WS_OVERLAPPEDWINDOW	0xcf0000L
#define WS_POPUP	0x80000000L
#define WS_POPUPWINDOW	0x80880000L
#define WS_SIZEBOX	0x40000L
#define WS_SYSMENU	0x80000L
#define WS_TABSTOP	0x10000L
#define WS_THICKFRAME	0x40000L
#define WS_TILED	0L
#define WS_TILEDWINDOW	0xcf0000L
#define WS_VISIBLE	0x10000000L
#define WS_VSCROLL	0x200000L
#define MDIS_ALLCHILDSTYLES	0x1
#define BS_3STATE	0x5L
#define BS_AUTO3STATE	0x6L
#define BS_AUTOCHECKBOX	0x3L
#define BS_AUTORADIOBUTTON	0x9L
#define BS_BITMAP	0x80L
#define BS_BOTTOM	0x800L
#define BS_CENTER	0x300L
#define BS_CHECKBOX	0x2L
#define BS_DEFPUSHBUTTON	0x1L
#define BS_GROUPBOX	0x7L
#define BS_ICON		0x40L
#define BS_LEFT		0x100L
#define BS_LEFTTEXT	0x20L
#define BS_MULTILINE	0x2000L
#define BS_NOTIFY	0x4000L
#define BS_OWNERDRAW	0xbL
#define BS_PUSHBOX	0xcL		/* FIXME!  What should this be?  */
#define BS_PUSHBUTTON	0L
#define BS_PUSHLIKE	0x1000L
#define BS_RADIOBUTTON	0x4L
#define BS_RIGHT	0x200L
#define BS_RIGHTBUTTON	0x20L
#define BS_TEXT		0L
#define BS_TOP		0x400L
#define BS_USERBUTTON	0x8L
#define BS_VCENTER	0xc00L
#define CBS_AUTOHSCROLL	0x40L
#define CBS_DISABLENOSCROLL	0x800L
#define CBS_DROPDOWN	0x2L
#define CBS_DROPDOWNLIST	0x3L
#define CBS_HASSTRINGS	0x200L
#define CBS_LOWERCASE	0x4000L
#define CBS_NOINTEGRALHEIGHT	0x400L
#define CBS_OEMCONVERT	0x80L
#define CBS_OWNERDRAWFIXED	0x10L
#define CBS_OWNERDRAWVARIABLE	0x20L
#define CBS_SIMPLE	0x1L
#define CBS_SORT	0x100L
#define CBS_UPPERCASE	0x2000L
#define ES_AUTOHSCROLL	0x80L
#define ES_AUTOVSCROLL	0x40L
#define ES_CENTER	0x1L
#define ES_LEFT		0L
#define ES_LOWERCASE	0x10L
#define ES_MULTILINE	0x4L
#define ES_NOHIDESEL	0x100L
#define ES_NUMBER	0x2000L
#define ES_OEMCONVERT	0x400L
#define ES_PASSWORD	0x20L
#define ES_READONLY	0x800L
#define ES_RIGHT	0x2L
#define ES_UPPERCASE	0x8L
#define ES_WANTRETURN	0x1000L
#define LBS_DISABLENOSCROLL	0x1000L
#define LBS_EXTENDEDSEL	0x800L
#define LBS_HASSTRINGS	0x40L
#define LBS_MULTICOLUMN	0x200L
#define LBS_MULTIPLESEL	0x8L
#define LBS_NODATA	0x2000L
#define LBS_NOINTEGRALHEIGHT	0x100L
#define LBS_NOREDRAW	0x4L
#define LBS_NOSEL	0x4000L
#define LBS_NOTIFY	0x1L
#define LBS_OWNERDRAWFIXED	0x10L
#define LBS_OWNERDRAWVARIABLE	0x20L
#define LBS_SORT	0x2L
#define LBS_STANDARD	0xa00003L
#define LBS_USETABSTOPS	0x80L
#define LBS_WANTKEYBOARDINPUT	0x400L
#define SBS_BOTTOMALIGN	0x4L
#define SBS_HORZ	0L
#define SBS_LEFTALIGN	0x2L
#define SBS_RIGHTALIGN	0x4L
#define SBS_SIZEBOX	0x8L
#define SBS_SIZEBOXBOTTOMRIGHTALIGN	0x4L
#define SBS_SIZEBOXTOPLEFTALIGN	0x2L
#define SBS_SIZEGRIP	0x10L
#define SBS_TOPALIGN	0x2L
#define SBS_VERT	0x1L
#define SS_BITMAP	0xeL
#define SS_BLACKFRAME	0x7L
#define SS_BLACKRECT	0x4L
#define SS_CENTER	0x1L
#define SS_CENTERIMAGE	0x200L
#define SS_ENHMETAFILE	0xfL
#define SS_ETCHEDFRAME	0x12L
#define SS_ETCHEDHORZ	0x10L
#define SS_ETCHEDVERT	0x11L
#define SS_GRAYFRAME	0x8L
#define SS_GRAYRECT	0x5L
#define SS_ICON		0x3L
#define SS_LEFT		0L
#define SS_LEFTNOWORDWRAP	0xcL
#define SS_NOPREFIX	0x80L
#define SS_NOTIFY	0x100L
#define SS_OWNERDRAW	0xdL
#define SS_REALSIZEIMAGE	0x800L
#define SS_RIGHT	0x2L
#define SS_RIGHTJUST	0x400L
#define SS_SIMPLE	0xbL
#define SS_SUNKEN	0x1000L
#define SS_USERITEM     0xaL
#define SS_WHITEFRAME	0x9L
#define SS_WHITERECT	0x6L
#define DS_3DLOOK	0x4L
#define DS_ABSALIGN	0x1L
#define DS_CENTER	0x800L
#define DS_CENTERMOUSE	0x1000L
#define DS_CONTEXTHELP	0x2000L
#define DS_CONTROL	0x400L
#define DS_FIXEDSYS	0x8L
#define DS_LOCALEDIT	0x20L
#define DS_MODALFRAME	0x80L
#define DS_NOFAILCREATE	0x10L
#define DS_NOIDLEMSG	0x100L
#define DS_SETFONT	0x40L
#define DS_SETFOREGROUND	0x200L
#define DS_SYSMODAL	0x2L

/* A dialog control.  */

typedef struct rc_dialog_control
{
  /* Next control.  */
  struct rc_dialog_control *next;
  /* ID.  */
  rc_uint_type id;
  /* Style.  */
  rc_uint_type style;
  /* Extended style.  */
  rc_uint_type exstyle;
  /* X coordinate.  */
  rc_uint_type x;
  /* Y coordinate.  */
  rc_uint_type y;
  /* Width.  */
  rc_uint_type width;
  /* Height.  */
  rc_uint_type height;
  /* Class name.  */
  rc_res_id class;
  /* Associated text.  */
  rc_res_id text;
  /* Extra data for the window procedure.  */
  struct rc_rcdata_item *data;
  /* Help ID.  Only used in an extended dialog.  */
  rc_uint_type help;
} rc_dialog_control;

struct __attribute__ ((__packed__)) bin_dialog_control
{
  bfd_byte style[4];
  bfd_byte exstyle[4];
  bfd_byte x[2];
  bfd_byte y[2];
  bfd_byte width[2];
  bfd_byte height[2];
  bfd_byte id[2];
};
#define BIN_DIALOG_CONTROL_SIZE 18

struct __attribute__ ((__packed__)) bin_dialogex_control
{
  bfd_byte help[4];
  bfd_byte exstyle[4];
  bfd_byte style[4];
  bfd_byte x[2];
  bfd_byte y[2];
  bfd_byte width[2];
  bfd_byte height[2];
  bfd_byte id[4];
};
#define BIN_DIALOGEX_CONTROL_SIZE 24

/* Control classes.  These can be used as the ID field in a rc_dialog_control.  */

#define CTL_BUTTON	0x80
#define CTL_EDIT	0x81
#define CTL_STATIC	0x82
#define CTL_LISTBOX	0x83
#define CTL_SCROLLBAR	0x84
#define CTL_COMBOBOX	0x85

/* A fontdir resource is a list of rc_fontdir.  */

typedef struct rc_fontdir
{
  struct rc_fontdir *next;
  /* Index of font entry.  */
  rc_uint_type index;
  /* Length of font information.  */
  rc_uint_type length;
  /* Font information.  */
  const bfd_byte *data;
} rc_fontdir;

struct __attribute__ ((__packed__)) bin_fontdir_item
{
  bfd_byte index[2];
  bfd_byte header[54];
  bfd_byte device_name[1];
  /* bfd_byte face_name[]; */
};

/* A group_icon resource is a list of rc_group_icon.  */

typedef struct rc_group_icon
{
  /* Next icon in group.  */
  struct rc_group_icon *next;
  /* Width.  */
  bfd_byte width;
  /* Height.  */
  bfd_byte height;
  /* Color count.  */
  bfd_byte colors;
  /* Planes.  */
  rc_uint_type planes;
  /* Bits per pixel.  */
  rc_uint_type bits;
  /* Number of bytes in cursor resource.  */
  rc_uint_type bytes;
  /* Index of cursor resource.  */
  rc_uint_type index;
} rc_group_icon;

struct __attribute__ ((__packed__)) bin_group_icon
{
  bfd_byte sig1[2];
  bfd_byte sig2[2];
  bfd_byte count[2];
};
#define BIN_GROUP_ICON_SIZE 6

struct __attribute__ ((__packed__)) bin_group_icon_item
{
  bfd_byte width[1];
  bfd_byte height[1];
  bfd_byte colors[1];
  bfd_byte pad[1];
  bfd_byte planes[2];
  bfd_byte bits[2];
  bfd_byte bytes[4];
  bfd_byte index[2];
};
#define BIN_GROUP_ICON_ITEM_SIZE 14

/* A menu resource.  */

typedef struct rc_menu
{
  /* List of menuitems.  */
  struct rc_menuitem *items;
  /* Help ID.  I don't think there is any way to set this in an rc
     file, but it can appear in the binary format.  */
  rc_uint_type help;
} rc_menu;

struct __attribute__ ((__packed__)) bin_menu
{
  bfd_byte sig1[2];
  bfd_byte sig2[2];
};
#define BIN_MENU_SIZE 4

struct __attribute__ ((__packed__)) bin_menuex
{
  bfd_byte sig1[2];
  bfd_byte sig2[2];
  bfd_byte help[4];
};
#define BIN_MENUEX_SIZE 8

/* A menu resource is a list of rc_menuitem.  */

typedef struct rc_menuitem
{
  /* Next menu item.  */
  struct rc_menuitem *next;
  /* Type.  In a normal menu, rather than a menuex, this is the flags
     field.  */
  rc_uint_type type;
  /* State.  This is only used in a menuex.  */
  rc_uint_type state;
  /* Id.  */
  rc_uint_type id;
  /* Unicode text.  */
  unichar *text;
  /* Popup menu items for a popup.  */
  struct rc_menuitem *popup;
  /* Help ID.  This is only used in a menuex.  */
  rc_uint_type help;
} rc_menuitem;

struct __attribute__ ((__packed__)) bin_menuitem
{
  bfd_byte flags[2];
  bfd_byte id[2];
};
#define BIN_MENUITEM_SIZE  4
#define BIN_MENUITEM_POPUP_SIZE  2

struct __attribute__ ((__packed__)) bin_menuitemex
{
  bfd_byte type[4];
  bfd_byte state[4];
  bfd_byte id[4];
  bfd_byte flags[2];
  /* unicode text */
  /* if popup: align, bfd_byte help[4], align, bin_menuitemex[]; */
};
#define BIN_MENUITEMEX_SIZE 14

/* Menu item flags.  These can appear in the flags field of a rc_menuitem.  */

#define MENUITEM_GRAYED		0x001
#define MENUITEM_INACTIVE	0x002
#define MENUITEM_BITMAP		0x004
#define MENUITEM_OWNERDRAW	0x100
#define MENUITEM_CHECKED	0x008
#define MENUITEM_POPUP		0x010
#define MENUITEM_MENUBARBREAK	0x020
#define MENUITEM_MENUBREAK	0x040
#define MENUITEM_ENDMENU	0x080
#define MENUITEM_HELP	       0x4000

/* An rcdata resource is a pointer to a list of rc_rcdata_item.  */

typedef struct rc_rcdata_item
{
  /* Next data item.  */
  struct rc_rcdata_item *next;
  /* Type of data.  */
  enum
  {
    RCDATA_WORD,
    RCDATA_DWORD,
    RCDATA_STRING,
    RCDATA_WSTRING,
    RCDATA_BUFFER
  } type;
  union
  {
    rc_uint_type word;
    rc_uint_type dword;
    struct
    {
      rc_uint_type length;
      const char *s;
    } string;
    struct
    {
      rc_uint_type length;
      const unichar *w;
    } wstring;
    struct
    {
      rc_uint_type length;
      const bfd_byte *data;
    } buffer;
  } u;
} rc_rcdata_item;

/* A stringtable resource is a pointer to a rc_stringtable.  */

typedef struct rc_stringtable
{
  /* Each stringtable resource is a list of 16 unicode strings.  */
  struct
  {
    /* Length of string.  */
    rc_uint_type length;
    /* String data if length > 0.  */
    unichar *string;
  } strings[16];
} rc_stringtable;

/* A versioninfo resource points to a rc_versioninfo.  */

typedef struct rc_versioninfo
{
  /* Fixed version information.  */
  struct rc_fixed_versioninfo *fixed;
  /* Variable version information.  */
  struct rc_ver_info *var;
} rc_versioninfo;

struct __attribute__ ((__packed__)) bin_versioninfo
{
  bfd_byte size[2];
  bfd_byte fixed_size[2];
  bfd_byte sig2[2];
};
#define BIN_VERSIONINFO_SIZE 6

/* The fixed portion of a versioninfo resource.  */

typedef struct rc_fixed_versioninfo
{
  /* The file version, which is two 32 bit integers.  */
  rc_uint_type file_version_ms;
  rc_uint_type file_version_ls;
  /* The product version, which is two 32 bit integers.  */
  rc_uint_type product_version_ms;
  rc_uint_type product_version_ls;
  /* The file flags mask.  */
  rc_uint_type file_flags_mask;
  /* The file flags.  */
  rc_uint_type file_flags;
  /* The OS type.  */
  rc_uint_type file_os;
  /* The file type.  */
  rc_uint_type file_type;
  /* The file subtype.  */
  rc_uint_type file_subtype;
  /* The date, which in Windows is two 32 bit integers.  */
  rc_uint_type file_date_ms;
  rc_uint_type file_date_ls;
} rc_fixed_versioninfo;

struct __attribute__ ((__packed__)) bin_fixed_versioninfo
{
  bfd_byte sig1[4];
  bfd_byte sig2[4];
  bfd_byte file_version[4];
  bfd_byte file_version_ls[4];
  bfd_byte product_version_ms[4];
  bfd_byte product_version_ls[4];
  bfd_byte file_flags_mask[4];
  bfd_byte file_flags[4];
  bfd_byte file_os[4];
  bfd_byte file_type[4];
  bfd_byte file_subtype[4];
  bfd_byte file_date_ms[4];
  bfd_byte file_date_ls[4];
};
#define BIN_FIXED_VERSIONINFO_SIZE 52

/* A list of variable version information.  */

typedef struct rc_ver_info
{
  /* Next item.  */
  struct rc_ver_info *next;
  /* Type of data.  */
  enum { VERINFO_STRING, VERINFO_VAR } type;
  union
  {
    /* StringFileInfo data.  */
    struct
    {
      /* Language.  */
      unichar *language;
      /* Strings.  */
      struct rc_ver_stringinfo *strings;
    } string;
    /* VarFileInfo data.  */
    struct
    {
      /* Key.  */
      unichar *key;
      /* Values.  */
      struct rc_ver_varinfo *var;
    } var;
  } u;
} rc_ver_info;

struct __attribute__ ((__packed__)) bin_ver_info
{
  bfd_byte size[2];
  bfd_byte sig1[2];
  bfd_byte sig2[2];
};
#define BIN_VER_INFO_SIZE 6

/* A list of string version information.  */

typedef struct rc_ver_stringinfo
{
  /* Next string.  */
  struct rc_ver_stringinfo *next;
  /* Key.  */
  unichar *key;
  /* Value.  */
  unichar *value;
} rc_ver_stringinfo;

/* A list of variable version information.  */

typedef struct rc_ver_varinfo
{
  /* Next item.  */
  struct rc_ver_varinfo *next;
  /* Language ID.  */
  rc_uint_type language;
  /* Character set ID.  */
  rc_uint_type charset;
} rc_ver_varinfo;

typedef struct rc_toolbar_item
{
  struct rc_toolbar_item *next;
  struct rc_toolbar_item *prev;
  rc_res_id id;
} rc_toolbar_item;

struct __attribute__ ((__packed__)) bin_messagetable_item
{
  bfd_byte length[2];
  bfd_byte flags[2];
  bfd_byte data[1];
};
#define BIN_MESSAGETABLE_ITEM_SIZE  4

#define MESSAGE_RESOURCE_UNICODE  0x0001

struct __attribute__ ((__packed__)) bin_messagetable_block
{
  bfd_byte lowid[4];
  bfd_byte highid[4];
  bfd_byte offset[4];
};
#define BIN_MESSAGETABLE_BLOCK_SIZE 12

struct __attribute__ ((__packed__)) bin_messagetable
{
  bfd_byte cblocks[4];
  struct bin_messagetable_block items[1];
};
#define BIN_MESSAGETABLE_SIZE 8

typedef struct rc_toolbar
{
  rc_uint_type button_width;
  rc_uint_type button_height;
  rc_uint_type nitems;
  rc_toolbar_item *items;
} rc_toolbar;

struct __attribute__ ((__packed__)) bin_toolbar
{
  bfd_byte button_width[4];
  bfd_byte button_height[4];
  bfd_byte nitems[4];
  /* { bfd_byte id[4]; } * nitems; */
};
#define BIN_TOOLBAR_SIZE 12

extern int target_is_bigendian;

typedef struct windres_bfd
{
  bfd *abfd;
  asection *sec;
  rc_uint_type kind : 4;
} windres_bfd;

#define WR_KIND_TARGET	  0
#define WR_KIND_BFD	  1
#define WR_KIND_BFD_BIN_L 2
#define WR_KIND_BFD_BIN_B 3

#define WR_KIND(PTR)  (PTR)->kind
#define WR_SECTION(PTR)	(PTR)->sec
#define WR_BFD(PTR) (PTR)->abfd

extern void set_windres_bfd_content (windres_bfd *, const void *, rc_uint_type, rc_uint_type);
extern void get_windres_bfd_content (windres_bfd *, void *, rc_uint_type, rc_uint_type);

extern void windres_put_8 (windres_bfd *, void *, rc_uint_type);
extern void windres_put_16 (windres_bfd *, void *, rc_uint_type);
extern void windres_put_32 (windres_bfd *, void *, rc_uint_type);
extern rc_uint_type windres_get_8 (windres_bfd *, const void *, rc_uint_type);
extern rc_uint_type windres_get_16 (windres_bfd *, const void *, rc_uint_type);
extern rc_uint_type windres_get_32 (windres_bfd *, const void *, rc_uint_type);

extern void set_windres_bfd (windres_bfd *, bfd *, asection *, rc_uint_type);
extern void set_windres_bfd_endianess (windres_bfd *, int);

#endif
