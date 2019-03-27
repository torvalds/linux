/* resbin.c -- manipulate the Windows binary resource format.
   Copyright 1997, 1998, 1999, 2002, 2003, 2007
   Free Software Foundation, Inc.
   Written by Ian Lance Taylor, Cygnus Support.
   Rewritten by Kai Tietz, Onevision.

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

/* This file contains functions to convert between the binary resource
   format and the internal structures that we want to use.  The same
   binary resource format is used in both res and COFF files.  */

#include "sysdep.h"
#include "bfd.h"
#include "bucomm.h"
#include "libiberty.h"
#include "windres.h"

/* Local functions.  */

static void toosmall (const char *);

static unichar *get_unicode (windres_bfd *, const bfd_byte *, rc_uint_type, rc_uint_type *);
static int get_resid (windres_bfd *, rc_res_id *, const bfd_byte *, rc_uint_type);
static rc_res_resource *bin_to_res_generic (windres_bfd *, enum rc_res_type,
					    const bfd_byte *, rc_uint_type);
static rc_res_resource *bin_to_res_cursor (windres_bfd *, const bfd_byte *, rc_uint_type);
static rc_res_resource *bin_to_res_menu (windres_bfd *,const bfd_byte *, rc_uint_type);
static rc_menuitem *bin_to_res_menuitems (windres_bfd *, const bfd_byte *, rc_uint_type,
					  rc_uint_type *);
static rc_menuitem *bin_to_res_menuexitems (windres_bfd *, const bfd_byte *, rc_uint_type,
					    rc_uint_type *);
static rc_res_resource *bin_to_res_dialog (windres_bfd *, const bfd_byte *, rc_uint_type);
static rc_res_resource *bin_to_res_string (windres_bfd *,const bfd_byte *, rc_uint_type);
static rc_res_resource *bin_to_res_fontdir (windres_bfd *, const bfd_byte *, rc_uint_type);
static rc_res_resource *bin_to_res_accelerators (windres_bfd *, const bfd_byte *, rc_uint_type);
static rc_res_resource *bin_to_res_rcdata (windres_bfd *, const bfd_byte *, rc_uint_type, int);
static rc_res_resource *bin_to_res_group_cursor (windres_bfd *, const bfd_byte *, rc_uint_type);
static rc_res_resource *bin_to_res_group_icon (windres_bfd *, const bfd_byte *, rc_uint_type);
static rc_res_resource *bin_to_res_version (windres_bfd *, const bfd_byte *, rc_uint_type);
static rc_res_resource *bin_to_res_userdata (windres_bfd *, const bfd_byte *, rc_uint_type);
static rc_res_resource *bin_to_res_toolbar (windres_bfd *, const bfd_byte *, rc_uint_type);
static void get_version_header (windres_bfd *, const bfd_byte *, rc_uint_type, const char *,
				unichar **, rc_uint_type *, rc_uint_type *, rc_uint_type *,
				rc_uint_type *);

/* Given a resource type ID, a pointer to data, a length, return a
   rc_res_resource structure which represents that resource.  The caller
   is responsible for initializing the res_info and coff_info fields
   of the returned structure.  */

rc_res_resource *
bin_to_res (windres_bfd *wrbfd, rc_res_id type, const bfd_byte *data,
	    rc_uint_type length)
{
  if (type.named)
    return bin_to_res_userdata (wrbfd, data, length);
  else
    {
      switch (type.u.id)
	{
	default:
	  return bin_to_res_userdata (wrbfd, data, length);
	case RT_CURSOR:
	  return bin_to_res_cursor (wrbfd, data, length);
	case RT_BITMAP:
	  return bin_to_res_generic (wrbfd, RES_TYPE_BITMAP, data, length);
	case RT_ICON:
	  return bin_to_res_generic (wrbfd, RES_TYPE_ICON, data, length);
	case RT_MENU:
	  return bin_to_res_menu (wrbfd, data, length);
	case RT_DIALOG:
	  return bin_to_res_dialog (wrbfd, data, length);
	case RT_STRING:
	  return bin_to_res_string (wrbfd, data, length);
	case RT_FONTDIR:
	  return bin_to_res_fontdir (wrbfd, data, length);
	case RT_FONT:
	  return bin_to_res_generic (wrbfd, RES_TYPE_FONT, data, length);
	case RT_ACCELERATOR:
	  return bin_to_res_accelerators (wrbfd, data, length);
	case RT_RCDATA:
	  return bin_to_res_rcdata (wrbfd, data, length, RES_TYPE_RCDATA);
	case RT_MESSAGETABLE:
	  return bin_to_res_generic (wrbfd, RES_TYPE_MESSAGETABLE, data, length);
	case RT_GROUP_CURSOR:
	  return bin_to_res_group_cursor (wrbfd, data, length);
	case RT_GROUP_ICON:
	  return bin_to_res_group_icon (wrbfd, data, length);
	case RT_VERSION:
	  return bin_to_res_version (wrbfd, data, length);
	case RT_TOOLBAR:
	  return  bin_to_res_toolbar (wrbfd, data, length);

	}
    }
}

/* Give an error if the binary data is too small.  */

static void
toosmall (const char *msg)
{
  fatal (_("%s: not enough binary data"), msg);
}

/* Swap in a NULL terminated unicode string.  */

static unichar *
get_unicode (windres_bfd *wrbfd, const bfd_byte *data, rc_uint_type length,
	     rc_uint_type *retlen)
{
  rc_uint_type c, i;
  unichar *ret;

  c = 0;
  while (1)
    {
      if (length < c * 2 + 2)
	toosmall (_("null terminated unicode string"));
      if (windres_get_16 (wrbfd, data + c * 2, 2) == 0)
	break;
      ++c;
    }

  ret = (unichar *) res_alloc ((c + 1) * sizeof (unichar));

  for (i = 0; i < c; i++)
    ret[i] = windres_get_16 (wrbfd, data + i * 2, 2);
  ret[i] = 0;

  if (retlen != NULL)
    *retlen = c;

  return ret;
}

/* Get a resource identifier.  This returns the number of bytes used.  */

static int
get_resid (windres_bfd *wrbfd, rc_res_id *id, const bfd_byte *data,
	   rc_uint_type length)
{
  rc_uint_type first;

  if (length < 2)
    toosmall (_("resource ID"));

  first = windres_get_16 (wrbfd, data, 2);
  if (first == 0xffff)
    {
      if (length < 4)
	toosmall (_("resource ID"));
      id->named = 0;
      id->u.id = windres_get_16 (wrbfd, data + 2, 2);
      return 4;
    }
  else
    {
      id->named = 1;
      id->u.n.name = get_unicode (wrbfd, data, length, &id->u.n.length);
      return id->u.n.length * 2 + 2;
    }
}

/* Convert a resource which just stores uninterpreted data from
   binary.  */

rc_res_resource *
bin_to_res_generic (windres_bfd *wrbfd ATTRIBUTE_UNUSED, enum rc_res_type type,
		    const bfd_byte *data, rc_uint_type length)
{
  rc_res_resource *r;

  r = (rc_res_resource *) res_alloc (sizeof (rc_res_resource));
  r->type = type;
  r->u.data.data = data;
  r->u.data.length = length;

  return r;
}

/* Convert a cursor resource from binary.  */

rc_res_resource *
bin_to_res_cursor (windres_bfd *wrbfd, const bfd_byte *data, rc_uint_type length)
{
  rc_cursor *c;
  rc_res_resource *r;

  if (length < 4)
    toosmall (_("cursor"));

  c = (rc_cursor *) res_alloc (sizeof (rc_cursor));
  c->xhotspot = windres_get_16 (wrbfd, data, 2);
  c->yhotspot = windres_get_16 (wrbfd, data + 2, 2);
  c->length = length - 4;
  c->data = data + 4;

  r = (rc_res_resource *) res_alloc (sizeof *r);
  r->type = RES_TYPE_CURSOR;
  r->u.cursor = c;

  return r;
}

/* Convert a menu resource from binary.  */

rc_res_resource *
bin_to_res_menu (windres_bfd *wrbfd, const bfd_byte *data, rc_uint_type length)
{
  rc_res_resource *r;
  rc_menu *m;
  rc_uint_type version, read;

  r = (rc_res_resource *) res_alloc (sizeof *r);
  r->type = RES_TYPE_MENU;

  m = (rc_menu *) res_alloc (sizeof (rc_menu));
  r->u.menu = m;

  if (length < 2)
    toosmall (_("menu header"));

  version = windres_get_16 (wrbfd, data, 2);

  if (version == 0)
    {
      if (length < 4)
	toosmall (_("menu header"));
      m->help = 0;
      m->items = bin_to_res_menuitems (wrbfd, data + 4, length - 4, &read);
    }
  else if (version == 1)
    {
      rc_uint_type offset;

      if (length < 8)
	toosmall (_("menuex header"));
      m->help = windres_get_32 (wrbfd, data + 4, 4);
      offset = windres_get_16 (wrbfd, data + 2, 2);
      if (offset + 4 >= length)
	toosmall (_("menuex offset"));
      m->items = bin_to_res_menuexitems (wrbfd, data + 4 + offset,
					 length - (4 + offset), &read);
    }
  else
    fatal (_("unsupported menu version %d"), (int) version);

  return r;
}

/* Convert menu items from binary.  */

static rc_menuitem *
bin_to_res_menuitems (windres_bfd *wrbfd, const bfd_byte *data, rc_uint_type length,
		      rc_uint_type *read)
{
  rc_menuitem *first, **pp;

  first = NULL;
  pp = &first;

  *read = 0;

  while (length > 0)
    {
      rc_uint_type flags, slen, itemlen;
      rc_uint_type stroff;
      rc_menuitem *mi;

      if (length < 4)
	toosmall (_("menuitem header"));

      mi = (rc_menuitem *) res_alloc (sizeof *mi);
      mi->state = 0;
      mi->help = 0;

      flags = windres_get_16 (wrbfd, data, 2);
      mi->type = flags &~ (MENUITEM_POPUP | MENUITEM_ENDMENU);

      if ((flags & MENUITEM_POPUP) == 0)
	stroff = 4;
      else
	stroff = 2;

      if (length < stroff + 2)
	toosmall (_("menuitem header"));

      if (windres_get_16 (wrbfd, data + stroff, 2) == 0)
	{
	  slen = 0;
	  mi->text = NULL;
	}
      else
	mi->text = get_unicode (wrbfd, data + stroff, length - stroff, &slen);

      itemlen = stroff + slen * 2 + 2;

      if ((flags & MENUITEM_POPUP) == 0)
	{
	  mi->popup = NULL;
	  mi->id = windres_get_16 (wrbfd, data + 2, 2);
	}
      else
	{
	  rc_uint_type subread;

	  mi->id = 0;
	  mi->popup = bin_to_res_menuitems (wrbfd, data + itemlen, length - itemlen,
	  				    &subread);
	  itemlen += subread;
	}

      mi->next = NULL;
      *pp = mi;
      pp = &mi->next;

      data += itemlen;
      length -= itemlen;
      *read += itemlen;

      if ((flags & MENUITEM_ENDMENU) != 0)
	return first;
    }

  return first;
}

/* Convert menuex items from binary.  */

static rc_menuitem *
bin_to_res_menuexitems (windres_bfd *wrbfd, const bfd_byte *data, rc_uint_type length,
			rc_uint_type *read)
{
  rc_menuitem *first, **pp;

  first = NULL;
  pp = &first;

  *read = 0;

  while (length > 0)
    {
      rc_uint_type flags, slen;
      rc_uint_type itemlen;
      rc_menuitem *mi;

      if (length < 16)
	toosmall (_("menuitem header"));

      mi = (rc_menuitem *) res_alloc (sizeof (rc_menuitem));
      mi->type = windres_get_32 (wrbfd, data, 4);
      mi->state = windres_get_32 (wrbfd, data + 4, 4);
      mi->id = windres_get_32 (wrbfd, data + 8, 4);

      flags = windres_get_16 (wrbfd, data + 12, 2);

      if (windres_get_16 (wrbfd, data + 14, 2) == 0)
	{
	  slen = 0;
	  mi->text = NULL;
	}
      else
	mi->text = get_unicode (wrbfd, data + 14, length - 14, &slen);

      itemlen = 14 + slen * 2 + 2;
      itemlen = (itemlen + 3) &~ 3;

      if ((flags & 1) == 0)
	{
	  mi->popup = NULL;
	  mi->help = 0;
	}
      else
	{
	  rc_uint_type subread;

	  if (length < itemlen + 4)
	    toosmall (_("menuitem"));
	  mi->help = windres_get_32 (wrbfd, data + itemlen, 4);
	  itemlen += 4;

	  mi->popup = bin_to_res_menuexitems (wrbfd, data + itemlen,
					      length - itemlen, &subread);
	  itemlen += subread;
	}

      mi->next = NULL;
      *pp = mi;
      pp = &mi->next;

      data += itemlen;
      length -= itemlen;
      *read += itemlen;

      if ((flags & 0x80) != 0)
	return first;
    }

  return first;
}

/* Convert a dialog resource from binary.  */

static rc_res_resource *
bin_to_res_dialog (windres_bfd *wrbfd, const bfd_byte *data, rc_uint_type length)
{
  rc_uint_type signature;
  rc_dialog *d;
  rc_uint_type c, sublen, i;
  rc_uint_type off;
  rc_dialog_control **pp;
  rc_res_resource *r;

  if (length < 18)
    toosmall (_("dialog header"));

  d = (rc_dialog *) res_alloc (sizeof (rc_dialog));

  signature = windres_get_16 (wrbfd, data + 2, 2);
  if (signature != 0xffff)
    {
      d->ex = NULL;
      d->style = windres_get_32 (wrbfd, data, 4);
      d->exstyle = windres_get_32 (wrbfd, data + 4, 4);
      off = 8;
    }
  else
    {
      int version;

      version = windres_get_16 (wrbfd, data, 2);
      if (version != 1)
	fatal (_("unexpected DIALOGEX version %d"), version);

      d->ex = (rc_dialog_ex *) res_alloc (sizeof (rc_dialog_ex));
      d->ex->help = windres_get_32 (wrbfd, data + 4, 4);
      d->exstyle = windres_get_32 (wrbfd, data + 8, 4);
      d->style = windres_get_32 (wrbfd, data + 12, 4);
      off = 16;
    }

  if (length < off + 10)
    toosmall (_("dialog header"));

  c = windres_get_16 (wrbfd, data + off, 2);
  d->x = windres_get_16 (wrbfd, data + off + 2, 2);
  d->y = windres_get_16 (wrbfd, data + off + 4, 2);
  d->width = windres_get_16 (wrbfd, data + off + 6, 2);
  d->height = windres_get_16 (wrbfd, data + off + 8, 2);

  off += 10;

  sublen = get_resid (wrbfd, &d->menu, data + off, length - off);
  off += sublen;

  sublen = get_resid (wrbfd, &d->class, data + off, length - off);
  off += sublen;

  d->caption = get_unicode (wrbfd, data + off, length - off, &sublen);
  off += sublen * 2 + 2;
  if (sublen == 0)
    d->caption = NULL;

  if ((d->style & DS_SETFONT) == 0)
    {
      d->pointsize = 0;
      d->font = NULL;
      if (d->ex != NULL)
	{
	  d->ex->weight = 0;
	  d->ex->italic = 0;
	  d->ex->charset = 1; /* Default charset.  */
	}
    }
  else
    {
      if (length < off + 2)
	toosmall (_("dialog font point size"));

      d->pointsize = windres_get_16 (wrbfd, data + off, 2);
      off += 2;

      if (d->ex != NULL)
	{
	  if (length < off + 4)
	    toosmall (_("dialogex font information"));
	  d->ex->weight = windres_get_16 (wrbfd, data + off, 2);
	  d->ex->italic = windres_get_8 (wrbfd, data + off + 2, 1);
	  d->ex->charset = windres_get_8 (wrbfd, data + off + 3, 1);
	  off += 4;
	}

      d->font = get_unicode (wrbfd, data + off, length - off, &sublen);
      off += sublen * 2 + 2;
    }

  d->controls = NULL;
  pp = &d->controls;

  for (i = 0; i < c; i++)
    {
      rc_dialog_control *dc;
      int datalen;

      off = (off + 3) &~ 3;

      dc = (rc_dialog_control *) res_alloc (sizeof (rc_dialog_control));

      if (d->ex == NULL)
	{
	  if (length < off + 8)
	    toosmall (_("dialog control"));

	  dc->style = windres_get_32 (wrbfd, data + off, 4);
	  dc->exstyle = windres_get_32 (wrbfd, data + off + 4, 4);
	  dc->help = 0;
	  off += 8;
	}
      else
	{
	  if (length < off + 12)
	    toosmall (_("dialogex control"));
	  dc->help = windres_get_32 (wrbfd, data + off, 4);
	  dc->exstyle = windres_get_32 (wrbfd, data + off + 4, 4);
	  dc->style = windres_get_32 (wrbfd, data + off + 8, 4);
	  off += 12;
	}

      if (length < off + (d->ex != NULL ? 2 : 0) + 10)
	toosmall (_("dialog control"));

      dc->x = windres_get_16 (wrbfd, data + off, 2);
      dc->y = windres_get_16 (wrbfd, data + off + 2, 2);
      dc->width = windres_get_16 (wrbfd, data + off + 4, 2);
      dc->height = windres_get_16 (wrbfd, data + off + 6, 2);

      if (d->ex != NULL)
	dc->id = windres_get_32 (wrbfd, data + off + 8, 4);
      else
	dc->id = windres_get_16 (wrbfd, data + off + 8, 2);

      off += 10 + (d->ex != NULL ? 2 : 0);

      sublen = get_resid (wrbfd, &dc->class, data + off, length - off);
      off += sublen;

      sublen = get_resid (wrbfd, &dc->text, data + off, length - off);
      off += sublen;

      if (length < off + 2)
	toosmall (_("dialog control end"));

      datalen = windres_get_16 (wrbfd, data + off, 2);
      off += 2;

      if (datalen == 0)
	dc->data = NULL;
      else
	{
	  off = (off + 3) &~ 3;

	  if (length < off + datalen)
	    toosmall (_("dialog control data"));

	  dc->data = ((rc_rcdata_item *)
		      res_alloc (sizeof (rc_rcdata_item)));
	  dc->data->next = NULL;
	  dc->data->type = RCDATA_BUFFER;
	  dc->data->u.buffer.length = datalen;
	  dc->data->u.buffer.data = data + off;

	  off += datalen;
	}

      dc->next = NULL;
      *pp = dc;
      pp = &dc->next;
    }

  r = (rc_res_resource *) res_alloc (sizeof *r);
  r->type = RES_TYPE_DIALOG;
  r->u.dialog = d;

  return r;
}

/* Convert a stringtable resource from binary.  */

static rc_res_resource *
bin_to_res_string (windres_bfd *wrbfd, const bfd_byte *data, rc_uint_type length)
{
  rc_stringtable *st;
  int i;
  rc_res_resource *r;

  st = (rc_stringtable *) res_alloc (sizeof (rc_stringtable));

  for (i = 0; i < 16; i++)
    {
      unsigned int slen;

      if (length < 2)
	toosmall (_("stringtable string length"));
      slen = windres_get_16 (wrbfd, data, 2);
      st->strings[i].length = slen;

      if (slen > 0)
	{
	  unichar *s;
	  unsigned int j;

	  if (length < 2 + 2 * slen)
	    toosmall (_("stringtable string"));

	  s = (unichar *) res_alloc (slen * sizeof (unichar));
	  st->strings[i].string = s;

	  for (j = 0; j < slen; j++)
	    s[j] = windres_get_16 (wrbfd, data + 2 + j * 2, 2);
	}

      data += 2 + 2 * slen;
      length -= 2 + 2 * slen;
    }

  r = (rc_res_resource *) res_alloc (sizeof *r);
  r->type = RES_TYPE_STRINGTABLE;
  r->u.stringtable = st;

  return r;
}

/* Convert a fontdir resource from binary.  */

static rc_res_resource *
bin_to_res_fontdir (windres_bfd *wrbfd, const bfd_byte *data, rc_uint_type length)
{
  rc_uint_type c, i;
  rc_fontdir *first, **pp;
  rc_res_resource *r;

  if (length < 2)
    toosmall (_("fontdir header"));

  c = windres_get_16 (wrbfd, data, 2);

  first = NULL;
  pp = &first;

  for (i = 0; i < c; i++)
    {
      const struct bin_fontdir_item *bfi;
      rc_fontdir *fd;
      unsigned int off;

      if (length < 56)
	toosmall (_("fontdir"));

      bfi = (const struct bin_fontdir_item *) data;
      fd = (rc_fontdir *) res_alloc (sizeof *fd);
      fd->index = windres_get_16 (wrbfd, bfi->index, 2);

      /* To work out the length of the fontdir data, we must get the
         length of the device name and face name strings, even though
         we don't store them in the rc_fontdir.  The
         documentation says that these are NULL terminated char
         strings, not Unicode strings.  */

      off = 56;

      while (off < length && data[off] != '\0')
	++off;
      if (off >= length)
	toosmall (_("fontdir device name"));
      ++off;

      while (off < length && data[off] != '\0')
	++off;
      if (off >= length)
	toosmall (_("fontdir face name"));
      ++off;

      fd->length = off;
      fd->data = data;

      fd->next = NULL;
      *pp = fd;
      pp = &fd->next;

      /* The documentation does not indicate that any rounding is
         required.  */

      data += off;
      length -= off;
    }

  r = (rc_res_resource *) res_alloc (sizeof *r);
  r->type = RES_TYPE_FONTDIR;
  r->u.fontdir = first;

  return r;
}

/* Convert an accelerators resource from binary.  */

static rc_res_resource *
bin_to_res_accelerators (windres_bfd *wrbfd, const bfd_byte *data, rc_uint_type length)
{
  rc_accelerator *first, **pp;
  rc_res_resource *r;

  first = NULL;
  pp = &first;

  while (1)
    {
      rc_accelerator *a;

      if (length < 8)
	toosmall (_("accelerator"));

      a = (rc_accelerator *) res_alloc (sizeof (rc_accelerator));

      a->flags = windres_get_16 (wrbfd, data, 2);
      a->key = windres_get_16 (wrbfd, data + 2, 2);
      a->id = windres_get_16 (wrbfd, data + 4, 2);

      a->next = NULL;
      *pp = a;
      pp = &a->next;

      if ((a->flags & ACC_LAST) != 0)
	break;

      data += 8;
      length -= 8;
    }

  r = (rc_res_resource *) res_alloc (sizeof (rc_res_resource));
  r->type = RES_TYPE_ACCELERATOR;
  r->u.acc = first;

  return r;
}

/* Convert an rcdata resource from binary.  */

static rc_res_resource *
bin_to_res_rcdata (windres_bfd *wrbfd ATTRIBUTE_UNUSED, const bfd_byte *data,
		   rc_uint_type length, int rctyp)
{
  rc_rcdata_item *ri;
  rc_res_resource *r;

  ri = (rc_rcdata_item *) res_alloc (sizeof (rc_rcdata_item));

  ri->next = NULL;
  ri->type = RCDATA_BUFFER;
  ri->u.buffer.length = length;
  ri->u.buffer.data = data;

  r = (rc_res_resource *) res_alloc (sizeof *r);
  r->type = rctyp;
  r->u.rcdata = ri;

  return r;
}

/* Convert a group cursor resource from binary.  */

static rc_res_resource *
bin_to_res_group_cursor (windres_bfd *wrbfd, const bfd_byte *data, rc_uint_type length)
{
  int type, c, i;
  rc_group_cursor *first, **pp;
  rc_res_resource *r;

  if (length < 6)
    toosmall (_("group cursor header"));

  type = windres_get_16 (wrbfd, data + 2, 2);
  if (type != 2)
    fatal (_("unexpected group cursor type %d"), type);

  c = windres_get_16 (wrbfd, data + 4, 2);

  data += 6;
  length -= 6;

  first = NULL;
  pp = &first;

  for (i = 0; i < c; i++)
    {
      rc_group_cursor *gc;

      if (length < 14)
	toosmall (_("group cursor"));

      gc = (rc_group_cursor *) res_alloc (sizeof *gc);

      gc->width = windres_get_16 (wrbfd, data, 2);
      gc->height = windres_get_16 (wrbfd, data + 2, 2);
      gc->planes = windres_get_16 (wrbfd, data + 4, 2);
      gc->bits = windres_get_16 (wrbfd, data + 6, 2);
      gc->bytes = windres_get_32 (wrbfd, data + 8, 4);
      gc->index = windres_get_16 (wrbfd, data + 12, 2);

      gc->next = NULL;
      *pp = gc;
      pp = &gc->next;

      data += 14;
      length -= 14;
    }

  r = (rc_res_resource *) res_alloc (sizeof (rc_res_resource));
  r->type = RES_TYPE_GROUP_CURSOR;
  r->u.group_cursor = first;

  return r;
}

/* Convert a group icon resource from binary.  */

static rc_res_resource *
bin_to_res_group_icon (windres_bfd *wrbfd, const bfd_byte *data, rc_uint_type length)
{
  int type, c, i;
  rc_group_icon *first, **pp;
  rc_res_resource *r;

  if (length < 6)
    toosmall (_("group icon header"));

  type = windres_get_16 (wrbfd, data + 2, 2);
  if (type != 1)
    fatal (_("unexpected group icon type %d"), type);

  c = windres_get_16 (wrbfd, data + 4, 2);

  data += 6;
  length -= 6;

  first = NULL;
  pp = &first;

  for (i = 0; i < c; i++)
    {
      rc_group_icon *gi;

      if (length < 14)
	toosmall (_("group icon"));

      gi = (rc_group_icon *) res_alloc (sizeof (rc_group_icon));

      gi->width = windres_get_8 (wrbfd, data, 1);
      gi->height = windres_get_8 (wrbfd, data + 1, 1);
      gi->colors = windres_get_8 (wrbfd, data + 2, 1);
      gi->planes = windres_get_16 (wrbfd, data + 4, 2);
      gi->bits = windres_get_16 (wrbfd, data + 6, 2);
      gi->bytes = windres_get_32 (wrbfd, data + 8, 4);
      gi->index = windres_get_16 (wrbfd, data + 12, 2);

      gi->next = NULL;
      *pp = gi;
      pp = &gi->next;

      data += 14;
      length -= 14;
    }

  r = (rc_res_resource *) res_alloc (sizeof *r);
  r->type = RES_TYPE_GROUP_ICON;
  r->u.group_icon = first;

  return r;
}

/* Extract data from a version header.  If KEY is not NULL, then the
   key must be KEY; otherwise, the key is returned in *PKEY.  This
   sets *LEN to the total length, *VALLEN to the value length, *TYPE
   to the type, and *OFF to the offset to the children.  */

static void
get_version_header (windres_bfd *wrbfd, const bfd_byte *data, rc_uint_type length,
		    const char *key, unichar **pkey,
		    rc_uint_type *len, rc_uint_type *vallen, rc_uint_type *type,
		    rc_uint_type *off)
{
  if (length < 8)
    toosmall (key);

  *len = windres_get_16 (wrbfd, data, 2);
  *vallen = windres_get_16 (wrbfd, data + 2, 2);
  *type = windres_get_16 (wrbfd, data + 4, 2);

  *off = 6;

  length -= 6;
  data += 6;

  if (key == NULL)
    {
      rc_uint_type sublen;

      *pkey = get_unicode (wrbfd, data, length, &sublen);
      *off += (sublen + 1) * sizeof (unichar);
    }
  else
    {
      while (1)
	{
	  if (length < 2)
	    toosmall (key);
	  if (windres_get_16 (wrbfd, data, 2) != (bfd_byte) *key)
	    fatal (_("unexpected version string"));

	  *off += 2;
	  length -= 2;
	  data += 2;

	  if (*key == '\0')
	    break;

	  ++key;
	}
    }

  *off = (*off + 3) &~ 3;
}

/* Convert a version resource from binary.  */

static rc_res_resource *
bin_to_res_version (windres_bfd *wrbfd, const bfd_byte *data, rc_uint_type length)
{
  rc_uint_type verlen, vallen, type, off;
  rc_fixed_versioninfo *fi;
  rc_ver_info *first, **pp;
  rc_versioninfo *v;
  rc_res_resource *r;

  get_version_header (wrbfd, data, length, "VS_VERSION_INFO",
		      (unichar **) NULL, &verlen, &vallen, &type, &off);

  if ((unsigned int) verlen != length)
    fatal (_("version length %d does not match resource length %lu"),
	   (int) verlen, (unsigned long) length);

  if (type != 0)
    fatal (_("unexpected version type %d"), (int) type);

  data += off;
  length -= off;

  if (vallen == 0)
    fi = NULL;
  else
    {
      unsigned long signature, fiv;

      if (vallen != 52)
	fatal (_("unexpected fixed version information length %ld"), (long) vallen);

      if (length < 52)
	toosmall (_("fixed version info"));

      signature = windres_get_32 (wrbfd, data, 4);
      if (signature != 0xfeef04bd)
	fatal (_("unexpected fixed version signature %lu"), signature);

      fiv = windres_get_32 (wrbfd, data + 4, 4);
      if (fiv != 0 && fiv != 0x10000)
	fatal (_("unexpected fixed version info version %lu"), fiv);

      fi = (rc_fixed_versioninfo *) res_alloc (sizeof (rc_fixed_versioninfo));

      fi->file_version_ms = windres_get_32 (wrbfd, data + 8, 4);
      fi->file_version_ls = windres_get_32 (wrbfd, data + 12, 4);
      fi->product_version_ms = windres_get_32 (wrbfd, data + 16, 4);
      fi->product_version_ls = windres_get_32 (wrbfd, data + 20, 4);
      fi->file_flags_mask = windres_get_32 (wrbfd, data + 24, 4);
      fi->file_flags = windres_get_32 (wrbfd, data + 28, 4);
      fi->file_os = windres_get_32 (wrbfd, data + 32, 4);
      fi->file_type = windres_get_32 (wrbfd, data + 36, 4);
      fi->file_subtype = windres_get_32 (wrbfd, data + 40, 4);
      fi->file_date_ms = windres_get_32 (wrbfd, data + 44, 4);
      fi->file_date_ls = windres_get_32 (wrbfd, data + 48, 4);

      data += 52;
      length -= 52;
    }

  first = NULL;
  pp = &first;

  while (length > 0)
    {
      rc_ver_info *vi;
      int ch;

      if (length < 8)
	toosmall (_("version var info"));

      vi = (rc_ver_info *) res_alloc (sizeof (rc_ver_info));

      ch = windres_get_16 (wrbfd, data + 6, 2);

      if (ch == 'S')
	{
	  rc_ver_stringinfo **ppvs;

	  vi->type = VERINFO_STRING;

	  get_version_header (wrbfd, data, length, "StringFileInfo",
			      (unichar **) NULL, &verlen, &vallen, &type,
			      &off);

	  if (vallen != 0)
	    fatal (_("unexpected stringfileinfo value length %ld"), (long) vallen);

	  data += off;
	  length -= off;

	  get_version_header (wrbfd, data, length, (const char *) NULL,
			      &vi->u.string.language, &verlen, &vallen,
			      &type, &off);

	  if (vallen != 0)
	    fatal (_("unexpected version stringtable value length %ld"), (long) vallen);

	  data += off;
	  length -= off;
	  verlen -= off;

	  vi->u.string.strings = NULL;
	  ppvs = &vi->u.string.strings;

	  /* It's convenient to round verlen to a 4 byte alignment,
             since we round the subvariables in the loop.  */
	  verlen = (verlen + 3) &~ 3;

	  while (verlen > 0)
	    {
	      rc_ver_stringinfo *vs;
	      rc_uint_type subverlen, vslen, valoff;

	      vs = (rc_ver_stringinfo *) res_alloc (sizeof *vs);

	      get_version_header (wrbfd, data, length,
				  (const char *) NULL, &vs->key, &subverlen,
				  &vallen, &type, &off);

	      subverlen = (subverlen + 3) &~ 3;

	      data += off;
	      length -= off;

	      vs->value = get_unicode (wrbfd, data, length, &vslen);
	      valoff = vslen * 2 + 2;
	      valoff = (valoff + 3) &~ 3;

	      if (off + valoff != subverlen)
		fatal (_("unexpected version string length %ld != %ld + %ld"),
		       (long) subverlen, (long) off, (long) valoff);

	      vs->next = NULL;
	      *ppvs = vs;
	      ppvs = &vs->next;

	      data += valoff;
	      length -= valoff;

	      if (verlen < subverlen)
		fatal (_("unexpected version string length %ld < %ld"),
		       (long) verlen, (long) subverlen);

	      verlen -= subverlen;
	    }
	}
      else if (ch == 'V')
	{
	  rc_ver_varinfo **ppvv;

	  vi->type = VERINFO_VAR;

	  get_version_header (wrbfd, data, length, "VarFileInfo",
			      (unichar **) NULL, &verlen, &vallen, &type,
			      &off);

	  if (vallen != 0)
	    fatal (_("unexpected varfileinfo value length %ld"), (long) vallen);

	  data += off;
	  length -= off;

	  get_version_header (wrbfd, data, length, (const char *) NULL,
			      &vi->u.var.key, &verlen, &vallen, &type, &off);

	  data += off;
	  length -= off;

	  vi->u.var.var = NULL;
	  ppvv = &vi->u.var.var;

	  while (vallen > 0)
	    {
	      rc_ver_varinfo *vv;

	      if (length < 4)
		toosmall (_("version varfileinfo"));

	      vv = (rc_ver_varinfo *) res_alloc (sizeof (rc_ver_varinfo));

	      vv->language = windres_get_16 (wrbfd, data, 2);
	      vv->charset = windres_get_16 (wrbfd, data + 2, 2);

	      vv->next = NULL;
	      *ppvv = vv;
	      ppvv = &vv->next;

	      data += 4;
	      length -= 4;

	      if (vallen < 4)
		fatal (_("unexpected version value length %ld"), (long) vallen);

	      vallen -= 4;
	    }
	}
      else
	fatal (_("unexpected version string"));

      vi->next = NULL;
      *pp = vi;
      pp = &vi->next;
    }

  v = (rc_versioninfo *) res_alloc (sizeof (rc_versioninfo));
  v->fixed = fi;
  v->var = first;

  r = (rc_res_resource *) res_alloc (sizeof *r);
  r->type = RES_TYPE_VERSIONINFO;
  r->u.versioninfo = v;

  return r;
}

/* Convert an arbitrary user defined resource from binary.  */

static rc_res_resource *
bin_to_res_userdata (windres_bfd *wrbfd ATTRIBUTE_UNUSED, const bfd_byte *data,
		     rc_uint_type length)
{
  rc_rcdata_item *ri;
  rc_res_resource *r;

  ri = (rc_rcdata_item *) res_alloc (sizeof (rc_rcdata_item));

  ri->next = NULL;
  ri->type = RCDATA_BUFFER;
  ri->u.buffer.length = length;
  ri->u.buffer.data = data;

  r = (rc_res_resource *) res_alloc (sizeof *r);
  r->type = RES_TYPE_USERDATA;
  r->u.rcdata = ri;

  return r;
}

static rc_res_resource *
bin_to_res_toolbar (windres_bfd *wrbfd, const bfd_byte *data, rc_uint_type length)
{
  rc_toolbar *ri;
  rc_res_resource *r;
  rc_uint_type i;

  ri = (rc_toolbar *) res_alloc (sizeof (rc_toolbar));
  ri->button_width = windres_get_32 (wrbfd, data, 4);
  ri->button_height = windres_get_32 (wrbfd, data + 4, 4);
  ri->nitems = windres_get_32 (wrbfd, data + 8, 4);
  ri->items = NULL;

  data += 12;
  length -= 12;
  for (i=0 ; i < ri->nitems; i++)
  {
    rc_toolbar_item *it;
    it = (rc_toolbar_item *) res_alloc (sizeof (rc_toolbar_item));
    it->id.named = 0;
    it->id.u.id = (int) windres_get_32 (wrbfd, data, 4);
    it->prev = it->next = NULL;
    data += 4;
    length -= 4;
    if(ri->items) {
      rc_toolbar_item *ii = ri->items;
      while (ii->next != NULL)
	ii = ii->next;
      it->prev = ii;
      ii->next = it;
    }
    else
      ri->items = it;
  }
  r = (rc_res_resource *) res_alloc (sizeof *r);
  r->type = RES_TYPE_TOOLBAR;
  r->u.toolbar = ri;
  return r;
}


/* Local functions used to convert resources to binary format.  */

static rc_uint_type resid_to_bin (windres_bfd *, rc_uint_type, rc_res_id);
static rc_uint_type unicode_to_bin (windres_bfd *, rc_uint_type, const unichar *);
static rc_uint_type res_to_bin_accelerator (windres_bfd *, rc_uint_type, const rc_accelerator *);
static rc_uint_type res_to_bin_cursor (windres_bfd *, rc_uint_type, const rc_cursor *);
static rc_uint_type res_to_bin_group_cursor (windres_bfd *, rc_uint_type, const rc_group_cursor *);
static rc_uint_type res_to_bin_dialog (windres_bfd *, rc_uint_type, const rc_dialog *);
static rc_uint_type res_to_bin_fontdir (windres_bfd *, rc_uint_type, const rc_fontdir *);
static rc_uint_type res_to_bin_group_icon (windres_bfd *, rc_uint_type, const rc_group_icon *);
static rc_uint_type res_to_bin_menu (windres_bfd *, rc_uint_type, const rc_menu *);
static rc_uint_type res_to_bin_menuitems (windres_bfd *, rc_uint_type, const rc_menuitem *);
static rc_uint_type res_to_bin_menuexitems (windres_bfd *, rc_uint_type, const rc_menuitem *);
static rc_uint_type res_to_bin_rcdata (windres_bfd *, rc_uint_type, const rc_rcdata_item *);
static rc_uint_type res_to_bin_stringtable (windres_bfd *, rc_uint_type, const rc_stringtable *);
static rc_uint_type string_to_unicode_bin (windres_bfd *, rc_uint_type, const char *);
static rc_uint_type res_to_bin_toolbar (windres_bfd *, rc_uint_type, rc_toolbar *tb);
static rc_uint_type res_to_bin_versioninfo (windres_bfd *, rc_uint_type, const rc_versioninfo *);
static rc_uint_type res_to_bin_generic (windres_bfd *, rc_uint_type, rc_uint_type,
					const bfd_byte *);

/* Convert a resource to binary.  */

rc_uint_type
res_to_bin (windres_bfd *wrbfd, rc_uint_type off, const rc_res_resource *res)
{
  switch (res->type)
    {
    case RES_TYPE_BITMAP:
    case RES_TYPE_FONT:
    case RES_TYPE_ICON:
    case RES_TYPE_MESSAGETABLE:
      return res_to_bin_generic (wrbfd, off, res->u.data.length, res->u.data.data);
    case RES_TYPE_ACCELERATOR:
      return res_to_bin_accelerator (wrbfd, off, res->u.acc);
    case RES_TYPE_CURSOR:
      return res_to_bin_cursor (wrbfd, off, res->u.cursor);
    case RES_TYPE_GROUP_CURSOR:
      return res_to_bin_group_cursor (wrbfd, off, res->u.group_cursor);
    case RES_TYPE_DIALOG:
      return res_to_bin_dialog (wrbfd, off, res->u.dialog);
    case RES_TYPE_FONTDIR:
      return res_to_bin_fontdir (wrbfd, off, res->u.fontdir);
    case RES_TYPE_GROUP_ICON:
      return res_to_bin_group_icon (wrbfd, off, res->u.group_icon);
    case RES_TYPE_MENU:
      return res_to_bin_menu (wrbfd, off, res->u.menu);
    case RES_TYPE_STRINGTABLE:
      return res_to_bin_stringtable (wrbfd, off, res->u.stringtable);
    case RES_TYPE_VERSIONINFO:
      return res_to_bin_versioninfo (wrbfd, off, res->u.versioninfo);
    case RES_TYPE_TOOLBAR:
      return res_to_bin_toolbar (wrbfd, off, res->u.toolbar);
    case RES_TYPE_USERDATA:
    case RES_TYPE_RCDATA:
    default:
      return res_to_bin_rcdata (wrbfd, off, res->u.rcdata);
    }
}

/* Convert a resource ID to binary.  This always returns exactly one
   bindata structure.  */

static rc_uint_type
resid_to_bin (windres_bfd *wrbfd, rc_uint_type off, rc_res_id id)
{
  if (! id.named)
    {
      if (wrbfd)
	{
	  struct bin_res_id bri;
	  
	  windres_put_16 (wrbfd, bri.sig, 0xffff);
	  windres_put_16 (wrbfd, bri.id, id.u.id);
	  set_windres_bfd_content (wrbfd, &bri, off, BIN_RES_ID);
	}
      off += BIN_RES_ID;
    }
  else
    {
      rc_uint_type len = (id.u.n.name ? unichar_len (id.u.n.name) : 0);
      if (wrbfd)
	{
	  bfd_byte *d = (bfd_byte *) reswr_alloc ((len + 1) * sizeof (unichar));
	  rc_uint_type i;
	  for (i = 0; i < len; i++)
	    windres_put_16 (wrbfd, d + (i * sizeof (unichar)), id.u.n.name[i]);
	  windres_put_16 (wrbfd, d + (len * sizeof (unichar)), 0);
	  set_windres_bfd_content (wrbfd, d, off, (len + 1) * sizeof (unichar));
    }
      off += (rc_uint_type) ((len + 1) * sizeof (unichar));
    }
  return off;
}

/* Convert a null terminated unicode string to binary.  This always
   returns exactly one bindata structure.  */

static rc_uint_type
unicode_to_bin (windres_bfd *wrbfd, rc_uint_type off, const unichar *str)
{
  rc_uint_type len = 0;

  if (str != NULL)
    len = unichar_len (str);

  if (wrbfd)
    {
      bfd_byte *d;
      rc_uint_type i;
      d = (bfd_byte *) reswr_alloc ( (len + 1) * sizeof (unichar));
      for (i = 0; i < len; i++)
	windres_put_16 (wrbfd, d + (i * sizeof (unichar)), str[i]);
      windres_put_16 (wrbfd, d + (len * sizeof (unichar)), 0);
      set_windres_bfd_content (wrbfd, d, off, (len + 1) * sizeof (unichar));
    }
  off += (rc_uint_type) ((len + 1) * sizeof (unichar));

  return off;
}

/* Convert an accelerator resource to binary.  */

static rc_uint_type
res_to_bin_accelerator (windres_bfd *wrbfd, rc_uint_type off,
			const rc_accelerator *accelerators)
{
  bindata *first, **pp;
  const rc_accelerator *a;

  first = NULL;
  pp = &first;

  for (a = accelerators; a != NULL; a = a->next)
    {
      if (wrbfd)
	{
	  struct bin_accelerator ba;

	  windres_put_16 (wrbfd, ba.flags, a->flags | (a->next != NULL ? 0 : ACC_LAST));
	  windres_put_16 (wrbfd, ba.key, a->key);
	  windres_put_16 (wrbfd, ba.id, a->id);
	  windres_put_16 (wrbfd, ba.pad, 0);
	  set_windres_bfd_content (wrbfd, &ba, off, BIN_ACCELERATOR_SIZE);
    }
      off += BIN_ACCELERATOR_SIZE;
    }
  return off;
}

/* Convert a cursor resource to binary.  */

static rc_uint_type
res_to_bin_cursor (windres_bfd *wrbfd, rc_uint_type off, const rc_cursor *c)
{
  if (wrbfd)
    {
      struct bin_cursor bc;

      windres_put_16 (wrbfd, bc.xhotspot, c->xhotspot);
      windres_put_16 (wrbfd, bc.yhotspot, c->yhotspot);
      set_windres_bfd_content (wrbfd, &bc, off, BIN_CURSOR_SIZE);
      if (c->length)
	set_windres_bfd_content (wrbfd, c->data, off + BIN_CURSOR_SIZE, c->length);
    }
  off = (off + BIN_CURSOR_SIZE + (rc_uint_type) c->length);
  return off;
}

/* Convert a group cursor resource to binary.  */

static rc_uint_type
res_to_bin_group_cursor (windres_bfd *wrbfd, rc_uint_type off,
			 const rc_group_cursor *group_cursors)
{
  int c = 0;
  const rc_group_cursor *gc;
  struct bin_group_cursor bgc;
  struct bin_group_cursor_item bgci;
  rc_uint_type start = off;

  off += BIN_GROUP_CURSOR_SIZE;

  for (c = 0, gc = group_cursors; gc != NULL; gc = gc->next, c++)
    {
      if (wrbfd)
	{
	  windres_put_16 (wrbfd, bgci.width, gc->width);
	  windres_put_16 (wrbfd, bgci.height, gc->height);
	  windres_put_16 (wrbfd, bgci.planes, gc->planes);
	  windres_put_16 (wrbfd, bgci.bits, gc->bits);
	  windres_put_32 (wrbfd, bgci.bytes, gc->bytes);
	  windres_put_16 (wrbfd, bgci.index, gc->index);
	  set_windres_bfd_content (wrbfd, &bgci, off, BIN_GROUP_CURSOR_ITEM_SIZE);
    }

      off += BIN_GROUP_CURSOR_ITEM_SIZE;
    }
  if (wrbfd)
    {
      windres_put_16 (wrbfd, bgc.sig1, 0);
      windres_put_16 (wrbfd, bgc.sig2, 2);
      windres_put_16 (wrbfd, bgc.nitems, c);
      set_windres_bfd_content (wrbfd, &bgc, start, BIN_GROUP_CURSOR_SIZE);
    }
  return off;
}

/* Convert a dialog resource to binary.  */

static rc_uint_type
res_to_bin_dialog (windres_bfd *wrbfd, rc_uint_type off, const rc_dialog *dialog)
{
  rc_uint_type off_delta;
  rc_uint_type start, marker;
  int dialogex;
  int c;
  rc_dialog_control *dc;
  struct bin_dialogex bdx;
  struct bin_dialog bd;

  off_delta = off;
  start = off;
  dialogex = extended_dialog (dialog);

  if (wrbfd)
    {
  if (! dialogex)
    {
	  windres_put_32 (wrbfd, bd.style, dialog->style);
	  windres_put_32 (wrbfd, bd.exstyle, dialog->exstyle);
	  windres_put_16 (wrbfd, bd.x, dialog->x);
	  windres_put_16 (wrbfd, bd.y, dialog->y);
	  windres_put_16 (wrbfd, bd.width, dialog->width);
	  windres_put_16 (wrbfd, bd.height, dialog->height);
    }
  else
    {
	  windres_put_16 (wrbfd, bdx.sig1, 1);
	  windres_put_16 (wrbfd, bdx.sig2, 0xffff);
	  windres_put_32 (wrbfd, bdx.help, (dialog->ex ? dialog->ex->help : 0));
	  windres_put_32 (wrbfd, bdx.exstyle, dialog->exstyle);
	  windres_put_32 (wrbfd, bdx.style, dialog->style);
	  windres_put_16 (wrbfd, bdx.x, dialog->x);
	  windres_put_16 (wrbfd, bdx.y, dialog->y);
	  windres_put_16 (wrbfd, bdx.width, dialog->width);
	  windres_put_16 (wrbfd, bdx.height, dialog->height);
	}
    }

  off += (dialogex != 0 ? BIN_DIALOGEX_SIZE : BIN_DIALOG_SIZE);

  off = resid_to_bin (wrbfd, off, dialog->menu);
  off = resid_to_bin (wrbfd, off, dialog->class);
  off = unicode_to_bin (wrbfd, off, dialog->caption);

  if ((dialog->style & DS_SETFONT) != 0)
    {
      if (wrbfd)
	{
	  if (! dialogex)
	    {
	      struct bin_dialogfont bdf;
	      windres_put_16 (wrbfd, bdf.pointsize, dialog->pointsize);
	      set_windres_bfd_content (wrbfd, &bdf, off, BIN_DIALOGFONT_SIZE);
	    }
	  else
	    {
	      struct bin_dialogexfont bdxf;
	      windres_put_16 (wrbfd, bdxf.pointsize, dialog->pointsize);
	      windres_put_16 (wrbfd, bdxf.weight, (dialog->ex == NULL ? 0 : dialog->ex->weight));
	      windres_put_8 (wrbfd, bdxf.italic, (dialog->ex == NULL ? 0 : dialog->ex->italic));
	      windres_put_8 (wrbfd, bdxf.charset, (dialog->ex == NULL ? 1 : dialog->ex->charset));
	      set_windres_bfd_content (wrbfd, &bdxf, off, BIN_DIALOGEXFONT_SIZE);
	    }
	}
      off += (dialogex ? BIN_DIALOGEXFONT_SIZE : BIN_DIALOGFONT_SIZE);
      off = unicode_to_bin (wrbfd, off, dialog->font);
    }
  for (c = 0, dc = dialog->controls; dc != NULL; dc = dc->next, c++)
    {
      bfd_byte dc_rclen[2];

      off += (4 - ((off - off_delta) & 3)) & 3;
      if (wrbfd)
	{
      if (! dialogex)
	{
	      struct bin_dialog_control bdc;

	      windres_put_32 (wrbfd, bdc.style, dc->style);
	      windres_put_32 (wrbfd, bdc.exstyle, dc->exstyle);
	      windres_put_16 (wrbfd, bdc.x, dc->x);
	      windres_put_16 (wrbfd, bdc.y, dc->y);
	      windres_put_16 (wrbfd, bdc.width, dc->width);
	      windres_put_16 (wrbfd, bdc.height, dc->height);
	      windres_put_16 (wrbfd, bdc.id, dc->id);
	      set_windres_bfd_content (wrbfd, &bdc, off, BIN_DIALOG_CONTROL_SIZE);
	}
      else
	{
	      struct bin_dialogex_control bdc;

	      windres_put_32 (wrbfd, bdc.help, dc->help);
	      windres_put_32 (wrbfd, bdc.exstyle, dc->exstyle);
	      windres_put_32 (wrbfd, bdc.style, dc->style);
	      windres_put_16 (wrbfd, bdc.x, dc->x);
	      windres_put_16 (wrbfd, bdc.y, dc->y);
	      windres_put_16 (wrbfd, bdc.width, dc->width);
	      windres_put_16 (wrbfd, bdc.height, dc->height);
	      windres_put_32 (wrbfd, bdc.id, dc->id);
	      set_windres_bfd_content (wrbfd, &bdc, off, BIN_DIALOGEX_CONTROL_SIZE);
	    }
	}      
      off += (dialogex != 0 ? BIN_DIALOGEX_CONTROL_SIZE : BIN_DIALOG_CONTROL_SIZE);

      off = resid_to_bin (wrbfd, off, dc->class);
      off = resid_to_bin (wrbfd, off, dc->text);

      marker = off; /* Save two bytes for size of optional data.  */
      off += 2;

      if (dc->data == NULL)
        {
	  if (wrbfd)
	    windres_put_16 (wrbfd, dc_rclen, 0);
	}
      else
	{
	  rc_uint_type saved_off = off;
	  rc_uint_type old_off;
	  off += (4 - ((off - off_delta) & 3)) & 3;

	  old_off = off;
	  off = res_to_bin_rcdata (wrbfd, off, dc->data);
	  if ((off - old_off) == 0)
	    old_off = off = saved_off;
	  if (wrbfd)
	    windres_put_16 (wrbfd, dc_rclen, off - old_off);
	    }
      if (wrbfd)
	set_windres_bfd_content (wrbfd, dc_rclen, marker, 2);
	}

  if (wrbfd)
    {
      windres_put_16 (wrbfd, (dialogex != 0 ? bdx.off : bd.off), c);
      if (! dialogex)
	set_windres_bfd_content (wrbfd, &bd, start, BIN_DIALOG_SIZE);
      else
	set_windres_bfd_content (wrbfd, &bdx, start, BIN_DIALOGEX_SIZE);
    }

  return off;
}

/* Convert a fontdir resource to binary.  */
static rc_uint_type
res_to_bin_fontdir (windres_bfd *wrbfd, rc_uint_type off, const rc_fontdir *fontdirs)
{
  rc_uint_type start;
  int c;
  const rc_fontdir *fd;

  start = off;
  off += 2;

  for (c = 0, fd = fontdirs; fd != NULL; fd = fd->next, c++)
    {
      if (wrbfd)
	{
	  bfd_byte d[2];
	  windres_put_16 (wrbfd, d, fd->index);
	  set_windres_bfd_content (wrbfd, d, off, 2);
	  if (fd->length)
	    set_windres_bfd_content (wrbfd, fd->data, off + 2, fd->length);
	}
      off += (rc_uint_type) fd->length + 2;
    }

  if (wrbfd)
    {
      bfd_byte d[2];
      windres_put_16 (wrbfd, d, c);
      set_windres_bfd_content (wrbfd, d, start, 2);
    }
  return off;
}

/* Convert a group icon resource to binary.  */

static rc_uint_type
res_to_bin_group_icon (windres_bfd *wrbfd, rc_uint_type off, const rc_group_icon *group_icons)
{
  rc_uint_type start;
  struct bin_group_icon bgi;
  int c;
  const rc_group_icon *gi;

  start = off;
  off += BIN_GROUP_ICON_SIZE;

  for (c = 0, gi = group_icons; gi != NULL; gi = gi->next, c++)
    {
      struct bin_group_icon_item bgii;

      if (wrbfd)
	{
	  windres_put_8 (wrbfd, bgii.width, gi->width);
	  windres_put_8 (wrbfd, bgii.height, gi->height);
	  windres_put_8 (wrbfd, bgii.colors, gi->colors);
	  windres_put_8 (wrbfd, bgii.pad, 0);
	  windres_put_16 (wrbfd, bgii.planes, gi->planes);
	  windres_put_16 (wrbfd, bgii.bits, gi->bits);
	  windres_put_32 (wrbfd, bgii.bytes, gi->bytes);
	  windres_put_16 (wrbfd, bgii.index, gi->index);
	  set_windres_bfd_content (wrbfd, &bgii, off, BIN_GROUP_ICON_ITEM_SIZE);
	}
      off += BIN_GROUP_ICON_ITEM_SIZE;
    }

  if (wrbfd)
    {
      windres_put_16 (wrbfd, bgi.sig1, 0);
      windres_put_16 (wrbfd, bgi.sig2, 1);
      windres_put_16 (wrbfd, bgi.count, c);
      set_windres_bfd_content (wrbfd, &bgi, start, BIN_GROUP_ICON_SIZE);
    }
  return off;
}

/* Convert a menu resource to binary.  */

static rc_uint_type
res_to_bin_menu (windres_bfd *wrbfd, rc_uint_type off, const rc_menu *menu)
{
  int menuex;

  menuex = extended_menu (menu);

  if (wrbfd)
    {
  if (! menuex)
    {
	  struct bin_menu bm;
	  windres_put_16 (wrbfd, bm.sig1, 0);
	  windres_put_16 (wrbfd, bm.sig2, 0);
	  set_windres_bfd_content (wrbfd, &bm, off, BIN_MENU_SIZE);
    }
  else
    {
	  struct bin_menuex bm;
	  windres_put_16 (wrbfd, bm.sig1, 1);
	  windres_put_16 (wrbfd, bm.sig2, 4);
	  windres_put_32 (wrbfd, bm.help, menu->help);
	  set_windres_bfd_content (wrbfd, &bm, off, BIN_MENUEX_SIZE);
    }
    }
  off += (menuex != 0 ? BIN_MENUEX_SIZE : BIN_MENU_SIZE);
  if (! menuex)
    {
      off = res_to_bin_menuitems (wrbfd, off, menu->items);
    }
  else
    {
      off = res_to_bin_menuexitems (wrbfd, off, menu->items);
    }
  return off;
}

/* Convert menu items to binary.  */

static rc_uint_type
res_to_bin_menuitems (windres_bfd *wrbfd, rc_uint_type off, const rc_menuitem *items)
{
  const rc_menuitem *mi;

  for (mi = items; mi != NULL; mi = mi->next)
    {
      struct bin_menuitem bmi;
      int flags;

      flags = mi->type;
      if (mi->next == NULL)
	flags |= MENUITEM_ENDMENU;
      if (mi->popup != NULL)
	flags |= MENUITEM_POPUP;

      if (wrbfd)
	{
	  windres_put_16 (wrbfd, bmi.flags, flags);
      if (mi->popup == NULL)
	    windres_put_16 (wrbfd, bmi.id, mi->id);
	  set_windres_bfd_content (wrbfd, &bmi, off,
				   mi->popup == NULL ? BIN_MENUITEM_SIZE
				   		     : BIN_MENUITEM_POPUP_SIZE);
	}
      off += (mi->popup == NULL ? BIN_MENUITEM_SIZE : BIN_MENUITEM_POPUP_SIZE);

      off = unicode_to_bin (wrbfd, off, mi->text);

      if (mi->popup != NULL)
	{
	  off = res_to_bin_menuitems (wrbfd, off, mi->popup);
	}
    }
  return off;
}

/* Convert menuex items to binary.  */

static rc_uint_type
res_to_bin_menuexitems (windres_bfd *wrbfd, rc_uint_type off, const rc_menuitem *items)
{
  rc_uint_type off_delta = off;
  const rc_menuitem *mi;

  for (mi = items; mi != NULL; mi = mi->next)
    {
      struct bin_menuitemex bmi;
      int flags;

      off += (4 - ((off - off_delta) & 3)) & 3;

      flags = 0;
      if (mi->next == NULL)
	flags |= 0x80;
      if (mi->popup != NULL)
	flags |= 1;

      if (wrbfd)
	{
	  windres_put_32 (wrbfd, bmi.type, mi->type);
	  windres_put_32 (wrbfd, bmi.state, mi->state);
	  windres_put_32 (wrbfd, bmi.id, mi->id);
	  windres_put_16 (wrbfd, bmi.flags, flags);
	  set_windres_bfd_content (wrbfd, &bmi, off, BIN_MENUITEMEX_SIZE);
	}
      off += BIN_MENUITEMEX_SIZE;

      off = unicode_to_bin (wrbfd, off, mi->text);

      if (mi->popup != NULL)
	{
	  bfd_byte help[4];

	  off += (4 - ((off - off_delta) & 3)) & 3;

	  if (wrbfd)
	    {
	      windres_put_32 (wrbfd, help, mi->help);
	      set_windres_bfd_content (wrbfd, help, off, 4);
	    }
	  off += 4;
	  off = res_to_bin_menuexitems (wrbfd, off, mi->popup);
	}
    }
  return off;
}

/* Convert an rcdata resource to binary.  This is also used to convert
   other information which happens to be stored in rc_rcdata_item lists
   to binary.  */

static rc_uint_type
res_to_bin_rcdata (windres_bfd *wrbfd, rc_uint_type off, const rc_rcdata_item *items)
{
  const rc_rcdata_item *ri;

  for (ri = items; ri != NULL; ri = ri->next)
    {
      rc_uint_type len;
      switch (ri->type)
	{
	default:
	  abort ();
	case RCDATA_WORD:
	  len = 2;
	  break;
	case RCDATA_DWORD:
	  len = 4;
	  break;
	case RCDATA_STRING:
	  len = ri->u.string.length;
	  break;
	case RCDATA_WSTRING:
	  len = ri->u.wstring.length * sizeof (unichar);
	  break;
	case RCDATA_BUFFER:
	  len = ri->u.buffer.length;
	  break;
	}
      if (wrbfd)
	{
	  bfd_byte h[4];
	  bfd_byte *hp = &h[0];
	  switch (ri->type)
	    {
	    case RCDATA_WORD:
	      windres_put_16 (wrbfd, hp, ri->u.word);
	      break;
	    case RCDATA_DWORD:
	      windres_put_32 (wrbfd, hp, ri->u.dword);
	      break;
	    case RCDATA_STRING:
	      hp = (bfd_byte *) ri->u.string.s;
	  break;
	case RCDATA_WSTRING:
	  {
		rc_uint_type i;

		hp = (bfd_byte *) reswr_alloc (len);
	    for (i = 0; i < ri->u.wstring.length; i++)
		  windres_put_16 (wrbfd, hp + i * sizeof (unichar), ri->u.wstring.w[i]);
	  }
	      break;
	case RCDATA_BUFFER:
	      hp = (bfd_byte *) ri->u.buffer.data;
	  break;
	}
	  set_windres_bfd_content (wrbfd, hp, off, len);
    }
      off += len;
    }
  return off;
}

/* Convert a stringtable resource to binary.  */

static rc_uint_type
res_to_bin_stringtable (windres_bfd *wrbfd, rc_uint_type off,
			const rc_stringtable *st)
{
  int i;

  for (i = 0; i < 16; i++)
    {
      rc_uint_type slen, length;
      unichar *s;

      slen = (rc_uint_type) st->strings[i].length;
      s = st->strings[i].string;

      length = 2 + slen * 2;
      if (wrbfd)
	{
	  bfd_byte *hp;
	  rc_uint_type j;

	  hp = (bfd_byte *) reswr_alloc (length);
	  windres_put_16 (wrbfd, hp, slen);

      for (j = 0; j < slen; j++)
	    windres_put_16 (wrbfd, hp + 2 + j * 2, s[j]);
	  set_windres_bfd_content (wrbfd, hp, off, length);
    }
      off += length;
    }
  return off;
}

/* Convert an ASCII string to a unicode binary string.  This always
   returns exactly one bindata structure.  */

static rc_uint_type
string_to_unicode_bin (windres_bfd *wrbfd, rc_uint_type off, const char *s)
{
  rc_uint_type len;

  len = (rc_uint_type) strlen (s);

  if (wrbfd)
    {
      rc_uint_type i;
      bfd_byte *hp;

      hp = (bfd_byte *) reswr_alloc ((len + 1) * sizeof (unichar));

      for (i = 0; i < len; i++)
	windres_put_16 (wrbfd, hp + i * 2, s[i]);
      windres_put_16 (wrbfd, hp + i * 2, 0);
      set_windres_bfd_content (wrbfd, hp, off, (len + 1) * sizeof (unichar));
    }
  off += (rc_uint_type) ((len + 1) * sizeof (unichar));
  return off;
}

static rc_uint_type
res_to_bin_toolbar (windres_bfd *wrbfd, rc_uint_type off, rc_toolbar *tb)
{
  if (wrbfd)
    {
      struct bin_toolbar bt;
      windres_put_32 (wrbfd, bt.button_width, tb->button_width);
      windres_put_32 (wrbfd, bt.button_height, tb->button_height);
      windres_put_32 (wrbfd, bt.nitems, tb->nitems);
      set_windres_bfd_content (wrbfd, &bt, off, BIN_TOOLBAR_SIZE);
      if (tb->nitems > 0)
	{
	  rc_toolbar_item *it;
	  bfd_byte *ids;
	  rc_uint_type i = 0;

	  ids = (bfd_byte *) reswr_alloc (tb->nitems * 4);
	  it=tb->items;
	  while(it != NULL)
	    {
	      windres_put_32 (wrbfd, ids + i, it->id.u.id);
	      i += 4;
	      it = it->next;
	    }
	  set_windres_bfd_content (wrbfd, ids, off + BIN_TOOLBAR_SIZE, i);
 	}
    }
  off += BIN_TOOLBAR_SIZE + tb->nitems * 4;

  return off;
}

/* Convert a versioninfo resource to binary.  */

static rc_uint_type
res_to_bin_versioninfo (windres_bfd *wrbfd, rc_uint_type off,
			const rc_versioninfo *versioninfo)
{
  rc_uint_type off_delta = off;
  rc_uint_type start;
  struct bin_versioninfo bvi;
  rc_ver_info *vi;

  start = off;
  off += BIN_VERSIONINFO_SIZE;
  off = string_to_unicode_bin (wrbfd, off, "VS_VERSION_INFO");
  off += (4 - ((off - off_delta) & 3)) & 3;

  if (versioninfo->fixed != NULL)
    {
      if (wrbfd)
	{
	  struct bin_fixed_versioninfo bfv;
	  const rc_fixed_versioninfo *fi;

      fi = versioninfo->fixed;
	  windres_put_32 (wrbfd, bfv.sig1, 0xfeef04bd);
	  windres_put_32 (wrbfd, bfv.sig2, 0x10000);
	  windres_put_32 (wrbfd, bfv.file_version, fi->file_version_ms);
	  windres_put_32 (wrbfd, bfv.file_version_ls, fi->file_version_ls);
	  windres_put_32 (wrbfd, bfv.product_version_ms, fi->product_version_ms);
	  windres_put_32 (wrbfd, bfv.product_version_ls, fi->product_version_ls);
	  windres_put_32 (wrbfd, bfv.file_flags_mask, fi->file_flags_mask);
	  windres_put_32 (wrbfd, bfv.file_flags, fi->file_flags);
	  windres_put_32 (wrbfd, bfv.file_os, fi->file_os);
	  windres_put_32 (wrbfd, bfv.file_type, fi->file_type);
	  windres_put_32 (wrbfd, bfv.file_subtype, fi->file_subtype);
	  windres_put_32 (wrbfd, bfv.file_date_ms, fi->file_date_ms);
	  windres_put_32 (wrbfd, bfv.file_date_ls, fi->file_date_ls);
	  set_windres_bfd_content (wrbfd, &bfv, off, BIN_FIXED_VERSIONINFO_SIZE);
	}
      off += BIN_FIXED_VERSIONINFO_SIZE;
    }

  for (vi = versioninfo->var; vi != NULL; vi = vi->next)
    {
      struct bin_ver_info bv;
      rc_uint_type bv_off;

      off += (4 - ((off - off_delta) & 3)) & 3;

      bv_off = off;

      off += BIN_VER_INFO_SIZE;

      switch (vi->type)
	{
	default:
	  abort ();
	case VERINFO_STRING:
	  {
	    struct bin_ver_info bvsd;
	    rc_uint_type vs_off;
	    const rc_ver_stringinfo *vs;

	    off = string_to_unicode_bin (wrbfd, off, "StringFileInfo");
	    off += (4 - ((off - off_delta) & 3)) & 3;

	    vs_off = off;

	    off += BIN_VER_INFO_SIZE;

	    off = unicode_to_bin (wrbfd, off, vi->u.string.language);

	    for (vs = vi->u.string.strings; vs != NULL; vs = vs->next)
	      {
		struct bin_ver_info bvss;
		rc_uint_type vss_off,str_off;

		off += (4 - ((off - off_delta) & 3)) & 3;

		vss_off = off;
		off += BIN_VER_INFO_SIZE;

		off = unicode_to_bin (wrbfd, off, vs->key);

		off += (4 - ((off - off_delta) & 3)) & 3;

		str_off = off;
		off = unicode_to_bin (wrbfd, off, vs->value);
		if (wrbfd)
		  {
		    windres_put_16 (wrbfd, bvss.size, off - vss_off);
		    windres_put_16 (wrbfd, bvss.sig1, (off - str_off) / 2);
		    windres_put_16 (wrbfd, bvss.sig2, 1);
		    set_windres_bfd_content (wrbfd, &bvss, vss_off,
		    			     BIN_VER_INFO_SIZE);
		  }
	      }
	    if (wrbfd)
	      {
		windres_put_16 (wrbfd, bvsd.size, off - vs_off);
		windres_put_16 (wrbfd, bvsd.sig1, 0);
		windres_put_16 (wrbfd, bvsd.sig2, 0);
		set_windres_bfd_content (wrbfd, &bvsd, vs_off,
					 BIN_VER_INFO_SIZE);
	      }
	    break;
	  }

	case VERINFO_VAR:
	  {
	    rc_uint_type vvd_off, vvvd_off;
	    struct bin_ver_info bvvd;
	    const rc_ver_varinfo *vv;

	    off = string_to_unicode_bin (wrbfd, off, "VarFileInfo");

	    off += (4 - ((off - off_delta) & 3)) & 3;

	    vvd_off = off;
	    off += BIN_VER_INFO_SIZE;

	    off = unicode_to_bin (wrbfd, off, vi->u.var.key);

	    off += (4 - ((off - off_delta) & 3)) & 3;

	    vvvd_off = off;

	    for (vv = vi->u.var.var; vv != NULL; vv = vv->next)
	      {
		if (wrbfd)
		  {
		    bfd_byte vvsd[4];

		    windres_put_16 (wrbfd, &vvsd[0], vv->language);
		    windres_put_16 (wrbfd, &vvsd[2], vv->charset);
		    set_windres_bfd_content (wrbfd, vvsd, off, 4);
		  }
		off += 4;
	      }
	    if (wrbfd)
	    {
		windres_put_16 (wrbfd, bvvd.size, off - vvd_off);
		windres_put_16 (wrbfd, bvvd.sig1, off - vvvd_off);
		windres_put_16 (wrbfd, bvvd.sig2, 0);
		set_windres_bfd_content (wrbfd, &bvvd, vvd_off,
					 BIN_VER_INFO_SIZE);
	    }

	    break;
	  }
	}

      if (wrbfd)
	{
	  windres_put_16 (wrbfd, bv.size, off-bv_off);
	  windres_put_16 (wrbfd, bv.sig1, 0);
	  windres_put_16 (wrbfd, bv.sig2, 0);
	  set_windres_bfd_content (wrbfd, &bv, bv_off,
	  			   BIN_VER_INFO_SIZE);
	}
    }

  if (wrbfd)
    {
      windres_put_16 (wrbfd, bvi.size, off - start);
      windres_put_16 (wrbfd, bvi.fixed_size,
		      versioninfo->fixed == NULL ? 0
		      				 : BIN_FIXED_VERSIONINFO_SIZE);
      windres_put_16 (wrbfd, bvi.sig2, 0);
      set_windres_bfd_content (wrbfd, &bvi, start, BIN_VER_INFO_SIZE);
    }
  return off;
}

/* Convert a generic resource to binary.  */

static rc_uint_type
res_to_bin_generic (windres_bfd *wrbfd, rc_uint_type off, rc_uint_type length,
		    const bfd_byte *data)
{
  if (wrbfd && length != 0)
    set_windres_bfd_content (wrbfd, data, off, length);
  return off + (rc_uint_type) length;
}
