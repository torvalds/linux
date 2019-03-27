/* windmc.c -- a program to compile Windows message files.
   Copyright 2007
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

/* This program can read and comile Windows message format.

   It is based on information taken from the following sources:

   * Microsoft documentation.

   * The wmc program, written by Bertho A. Stultiens (BS). */

#include "sysdep.h"
#include <assert.h>
#include <time.h>
#include "bfd.h"
#include "getopt.h"
#include "bucomm.h"
#include "libiberty.h"
#include "safe-ctype.h"
#include "obstack.h"

#include "windmc.h"
#include "windint.h"

/* Defines a message compiler element item with length and offset
   information.  */
typedef struct mc_msg_item
{
  rc_uint_type res_len;
  rc_uint_type res_off;
  struct bin_messagetable_item *res;
} mc_msg_item;

/* Defined in bfd/binary.c.  Used to set architecture and machine of input
   binary files.  */
extern enum bfd_architecture  bfd_external_binary_architecture;
extern unsigned long          bfd_external_machine;

int target_is_bigendian = 0;
const char *def_target_arch;

/* Globals and static variable definitions. */

/* bfd global helper struct variable.  */
static struct
{
  bfd *abfd;
  asection *sec;
} mc_bfd;

/* Memory list.  */
mc_node *mc_nodes = NULL;
static mc_node_lang **mc_nodes_lang = NULL;
static int mc_nodes_lang_count = 0;
static mc_keyword **mc_severity_codes = NULL;
static int mc_severity_codes_count = 0;
static mc_keyword **mc_facility_codes = NULL;
static int mc_facility_codes_count = 0;

/* When we are building a resource tree, we allocate everything onto
   an obstack, so that we can free it all at once if we want.  */
#define obstack_chunk_alloc xmalloc
#define obstack_chunk_free free

/* The resource building obstack.  */
static struct obstack res_obstack;

/* Flag variables.  */
/* Set by -C. Set the default code page to be used for input text file.  */
static rc_uint_type mcset_codepage_in = 0;

/* Set by -O. Set the default code page to be used for output text files.  */
static rc_uint_type mcset_codepage_out = 0;

/* Set by -b. .BIN filename should have .mc filename_ included for uniqueness.  */
static int mcset_prefix_bin = 0;

/* The base name of the .mc file.  */
static const char *mcset_mc_basename = "unknown";

/* Set by -e <ext>. Specify the extension for the header file.  */
static const char *mcset_header_ext = ".h";

/* Set by -h <path>. Gives the path of where to create the C include file.  */
static const char *mcset_header_dir = "./";

/* Set by -r <path>. Gives the path of where to create the RC include file
   and the binary message resource files it includes. */
static const char *mcset_rc_dir = "./";

/* Modified by -a & -u. By -u input file is unicode, by -a is ASCII (default).  */
static int mcset_text_in_is_unicode = 0;

/* Modified by -A & -U. By -U bin file is unicode (default), by -A is ASCII.  */
static int mcset_bin_out_is_unicode = 1;

/* Set by -c. Sets the Customer bit in all the message ID's.  */
int mcset_custom_bit = 0;

/* Set by -o. Generate OLE2 header file. Use HRESULT definition instead of
   status code definition.  */
static int mcset_use_hresult = 0;

/* Set by -m <msglen>. Generate a warning if the size of any message exceeds
   maxmsglen characters.  */
rc_uint_type mcset_max_message_length = 0;

/* Set by -d. Sets message values in header to decimal initially.  */
int mcset_out_values_are_decimal = 0;

/* Set by -n. terminates all strings with null's in the message tables.  */
static int mcset_automatic_null_termination = 0;

/* The type used for message id output in header.  */
unichar *mcset_msg_id_typedef = NULL;

/* Set by -x path. Geberated debug C file for mapping ID's to text.  */
static const char *mcset_dbg_dir = NULL;

/* getopt long name definitions.  */
static const struct option long_options[] =
{
  {"binprefix", no_argument, 0, 'b'},
  {"target", required_argument, 0, 'F'},
  {"extension", required_argument, 0, 'e'},
  {"headerdir", required_argument, 0, 'h'},
  {"rcdir", required_argument, 0, 'r'},
  {"verbose", no_argument, 0, 'v'},
  {"codepage_in", required_argument, 0, 'C'},
  {"codepage_out", required_argument, 0, 'O'},
  {"maxlength", required_argument, 0, 'm'},
  {"ascii_in", no_argument, 0, 'a'},
  {"ascii_out", no_argument, 0, 'A'},
  {"unicode_in", no_argument, 0, 'u'},
  {"unicode_out", no_argument, 0, 'U'},
  {"customflag", no_argument, 0, 'c'},
  {"decimal_values", no_argument, 0, 'd'},
  {"hresult_use", no_argument, 0, 'o'},
  {"nullterminate", no_argument, 0, 'n'},
  {"xdbg", required_argument, 0, 'x'},
  {"version", no_argument, 0, 'V'},
  {"help", no_argument, 0, 'H'},
  {0, no_argument, 0, 0}
};


/* Initialize the resource building obstack.  */
static void
res_init (void)
{
  obstack_init (&res_obstack);
}

/* Allocate space on the resource building obstack.  */
void *
res_alloc (rc_uint_type bytes)
{
  return (void *) obstack_alloc (&res_obstack, (size_t) bytes);
}

static FILE *
mc_create_path_text_file (const char *path, const char *ext)
{
  FILE *ret;
  size_t len = 1;
  char *hsz;

  len += (path != NULL ? strlen (path) : 0);
  len += strlen (mcset_mc_basename);
  len += (ext != NULL ? strlen (ext) : 0);
  hsz = xmalloc (len);
  sprintf (hsz, "%s%s%s", (path != NULL ? path : ""), mcset_mc_basename,
    (ext != NULL ? ext : ""));
  if ((ret = fopen (hsz, "wb")) == NULL)
    fatal (_("can't create %s file ,%s' for output.\n"), (ext ? ext : "text"), hsz);
  free (hsz);
  return ret;
}

static void
usage (FILE *stream, int status)
{
  fprintf (stream, _("Usage: %s [option(s)] [input-file]\n"),
	   program_name);
  fprintf (stream, _(" The options are:\n\
  -a --ascii_in                Read input file as ASCII file\n\
  -A --ascii_out               Write binary messages as ASCII\n\
  -b --binprefix               .bin filename is prefixed by .mc filename_ for uniqueness.\n\
  -c --customflag              Set custom flags for messages\n\
  -C --codepage_in=<val>       Set codepage when reading mc text file\n\
  -d --decimal_values          Print values to text files decimal\n\
  -e --extension=<extension>   Set header extension used on export header file\n\
  -F --target <target>         Specify output target for endianess.\n\
  -h --headerdir=<directory>   Set the export directory for headers\n\
  -u --unicode_in              Read input file as UTF16 file\n\
  -U --unicode_out             Write binary messages as UFT16\n\
  -m --maxlength=<val>         Set the maximal allowed message length\n\
  -n --nullterminate           Automatic add a zero termination to strings\n\
  -o --hresult_use             Use HRESULT definition instead of status code definition\n\
  -O --codepage_out=<val>      Set codepage used for writing text file\n\
  -r --rcdir=<directory>       Set the export directory for rc files\n\
  -x --xdbg=<directory>        Where to create the .dbg C include file\n\
                               that maps message ID's to their symbolic name.\n\
"));
  fprintf (stream, _("\
  -H --help                    Print this help message\n\
  -v --verbose                 Verbose - tells you what it's doing\n\
  -V --version                 Print version information\n"));

  list_supported_targets (program_name, stream);

  if (REPORT_BUGS_TO[0] && status == 0)
    fprintf (stream, _("Report bugs to %s\n"), REPORT_BUGS_TO);

  exit (status);
}

static void
set_endianess (bfd *abfd, const char *target)
{
  const bfd_target *target_vec;

  def_target_arch = NULL;
  target_vec = bfd_find_target (target, abfd);
  if (! target_vec)
    fatal ("Can't detect target endianess and architecture.");
  target_is_bigendian = ((target_vec->byteorder == BFD_ENDIAN_BIG) ? 1 : 0);
  {
    const char *tname = target_vec->name;
    const char **arch = bfd_arch_list ();

    if (arch && tname)
      {
	if (strchr (tname, '-') != NULL)
	  tname = strchr (tname, '-') + 1;
	while (*arch != NULL)
	  {
	    const char *in_a = strstr (*arch, tname);
	    char end_ch = (in_a ? in_a[strlen (tname)] : 0);
	    if (in_a && (in_a == *arch || in_a[-1] == ':')
	        && end_ch == 0)
	      {
		def_target_arch = *arch;
		break;
	      }
	    arch++;
	  }
      }
    if (! def_target_arch)
      fatal ("Can't detect architecture.");
  }
}

static int
probe_codepage (rc_uint_type *cp, int *is_uni, const char *pswitch, int defmode)
{
  if (*is_uni == -1)
    {
      if (*cp != CP_UTF16)
	*is_uni = defmode;
      else
	*is_uni = 1;
    }
  if (*is_uni)
    {
      if (*cp != 0 && *cp != CP_UTF16)
	{
	  fprintf (stderr, _("%s: warning: "), program_name);
	  fprintf (stderr, _("A codepage was specified switch ,%s' and UTF16.\n"), pswitch);
	  fprintf (stderr, _("\tcodepage settings are ignored.\n"));
	}
      *cp = CP_UTF16;
      return 1;
    }
  if (*cp == CP_UTF16)
    {
      *is_uni = 1;
      return 1;
    }
  if (*cp == 0)
    *cp = 1252;
  if (! unicode_is_valid_codepage (*cp))
	fatal ("Code page 0x%x is unknown.", (unsigned int) *cp);
  *is_uni = 0;
  return 1;
}

mc_node *
mc_add_node (void)
{
  mc_node *ret;

  ret = res_alloc (sizeof (mc_node));
  memset (ret, 0, sizeof (mc_node));
  if (! mc_nodes)
    mc_nodes = ret;
  else
    {
      mc_node *h = mc_nodes;

      while (h->next != NULL)
	h = h->next;
      h->next = ret;
    }
  return ret;
}

mc_node_lang *
mc_add_node_lang (mc_node *root, const mc_keyword *lang, rc_uint_type vid)
{
  mc_node_lang *ret, *h, *p;

  if (! lang || ! root)
    fatal (_("try to add a ill language."));
  ret = res_alloc (sizeof (mc_node_lang));
  memset (ret, 0, sizeof (mc_node_lang));
  ret->lang = lang;
  ret->vid = vid;
  if ((h = root->sub) == NULL)
    root->sub = ret;
  else
    {
      p = NULL;
      while (h != NULL)
	{
	  if (h->lang->nval > lang->nval)
	    break;
	  if (h->lang->nval == lang->nval)
	    {
	      if (h->vid > vid)
		break;
	      if (h->vid == vid)
		fatal ("double defined message id %ld.\n", (long) vid);
	    }
	  h = (p = h)->next;
	}
      ret->next = h;
      if (! p)
	root->sub = ret;
      else
	p->next = ret;
    }
  return ret;
}

static char *
convert_unicode_to_ACP (const unichar *usz)
{
  char *s;
  rc_uint_type l;

  if (! usz)
    return NULL;
  codepage_from_unicode (&l, usz, &s, mcset_codepage_out);
  if (! s)
    fatal ("unicode string not mappable to ASCII codepage 0x%lx.\n", (long) mcset_codepage_out);
  return s;
}

static void
write_dbg_define (FILE *fp, const unichar *sym_name, const unichar *typecast)
{
  char *sym;

  if (!sym_name || sym_name[0] == 0)
    return;
  sym = convert_unicode_to_ACP (sym_name);
  fprintf (fp, "  {(");
  if (typecast)
    unicode_print (fp, typecast, unichar_len (typecast));
  else
    fprintf (fp, "DWORD");
  fprintf (fp, ") %s, \"%s\" },\n", sym, sym);
}

static void
write_header_define (FILE *fp, const unichar *sym_name, rc_uint_type vid, const unichar *typecast, mc_node_lang *nl)
{
  char *sym;
  char *tdef = NULL;

  if (!sym_name || sym_name[0] == 0)
    {
      if (nl != NULL)
	{
	  if (mcset_out_values_are_decimal)
	    fprintf (fp, "//\n// MessageId: 0x%lu\n//\n", (unsigned long) vid);
	  else
	    fprintf (fp, "//\n// MessageId: 0x%lx\n//\n", (unsigned long) vid);
	}
      return;
    }
  sym = convert_unicode_to_ACP (sym_name);
  if (typecast && typecast[0] != 0)
    tdef = convert_unicode_to_ACP (typecast);
  fprintf (fp, "//\n// MessageId: %s\n//\n", sym);
  if (! mcset_out_values_are_decimal)
    fprintf (fp, "#define %s %s%s%s 0x%lx\n\n", sym,
      (tdef ? "(" : ""), (tdef ? tdef : ""), (tdef ? ")" : ""),
    (unsigned long) vid);
  else
    fprintf (fp, "#define %s %s%s%s 0x%lu\n\n", sym,
      (tdef ? "(" : ""), (tdef ? tdef : ""), (tdef ? ")" : ""),
    (unsigned long) vid);
}

static int
sort_mc_node_lang (const void *l, const void *r)
{
  const mc_node_lang *l1 = *((const mc_node_lang **)l);
  const mc_node_lang *r1 = *((const mc_node_lang **)r);

  if (l == r)
    return 0;
  if (l1->lang != r1->lang)
    {
      if (l1->lang->nval < r1->lang->nval)
	return -1;
      return 1;
    }
  if (l1->vid == r1->vid)
    return 0;
  if (l1->vid < r1->vid)
    return -1;
  return 1;
}

static int
sort_keyword_by_nval (const void *l, const void *r)
{
  const mc_keyword *l1 = *((const mc_keyword **)l);
  const mc_keyword *r1 = *((const mc_keyword **)r);
  rc_uint_type len1, len2;
  int e;

  if (l == r)
    return 0;
  if (l1->nval != r1->nval)
    {
      if (l1->nval < r1->nval)
	return -1;
      return 1;
    }
  len1 = unichar_len (l1->usz);
  len2 = unichar_len (r1->usz);
  if (len1 <= len2)
    e = memcmp (l1->usz, r1->usz, sizeof (unichar) * len1);
  else
    e = memcmp (l1->usz, r1->usz, sizeof (unichar) * len2);
  if (e)
    return e;
  if (len1 < len2)
    return -1;
  else if (len1 > len2)
    return 1;
  return 0;
}

static void
do_sorts (void)
{
  mc_node *h;
  mc_node_lang *n;
  const mc_keyword *k;
  int i;

  /* Sort message by their language and id ascending.  */
  mc_nodes_lang_count = 0;

  h = mc_nodes;
  while (h != NULL)
    {
      n = h->sub;
      while (n != NULL)
	{
	  mc_nodes_lang_count +=1;
	  n = n->next;
	}
      h = h->next;
    }

  if (mc_nodes_lang_count != 0)
    {
      h = mc_nodes;
      i = 0;
      mc_nodes_lang = xmalloc (sizeof (mc_node_lang *) * mc_nodes_lang_count);

      while (h != NULL)
	{
	  n = h->sub;
	  while (n != NULL)
	    {
	      mc_nodes_lang[i++] = n;
	      n = n->next;
	    }
	  h = h->next;
	}
      qsort (mc_nodes_lang, (size_t) mc_nodes_lang_count, sizeof (mc_node_lang *), sort_mc_node_lang);
    }
  /* Sort facility code definitions by there id ascending.  */
  i = 0;
  while ((k = enum_facility (i)) != NULL)
    ++i;
  mc_facility_codes_count = i;
  if (i != 0)
    {
      mc_facility_codes = xmalloc (sizeof (mc_keyword *) * i);
      i = 0;
      while ((k = enum_facility (i)) != NULL)
	mc_facility_codes[i++] = (mc_keyword *) k;
      qsort (mc_facility_codes, (size_t) mc_facility_codes_count, sizeof (mc_keyword *), sort_keyword_by_nval);
    }

  /* Sort severity code definitions by there id ascending.  */
  i = 0;
  while ((k = enum_severity (i)) != NULL)
    ++i;
  mc_severity_codes_count = i;
  if (i != 0)
    {
      mc_severity_codes = xmalloc (sizeof (mc_keyword *) * i);
      i = 0;
      while ((k = enum_severity (i)) != NULL)
	mc_severity_codes[i++] = (mc_keyword *) k;
      qsort (mc_severity_codes, (size_t) mc_severity_codes_count, sizeof (mc_keyword *), sort_keyword_by_nval);
    }
}

static int
mc_get_block_count (mc_node_lang **nl, int elems)
{
  rc_uint_type exid;
  int i, ret;

  if (! nl)
    return 0;
  i = 0;
  ret = 0;
  while (i < elems)
    {
      ret++;
      exid = nl[i++]->vid;
      while (i < elems && nl[i]->vid == exid + 1)
	exid = nl[i++]->vid;
    }
  return ret;
}

static bfd *
windmc_open_as_binary (const char *filename)
{
  bfd *abfd;

  abfd = bfd_openw (filename, "binary");
  if (! abfd)
    fatal ("can't open `%s' for output", filename);

  return abfd;
}

static void
target_put_16 (void *p, rc_uint_type value)
{
  assert (!! p);

  if (target_is_bigendian)
    bfd_putb16 (value, p);
  else
    bfd_putl16 (value, p);
}

static void
target_put_32 (void *p, rc_uint_type value)
{
  assert (!! p);

  if (target_is_bigendian)
    bfd_putb32 (value, p);
  else
    bfd_putl32 (value, p);
}

static struct bin_messagetable_item *
mc_generate_bin_item (mc_node_lang *n, rc_uint_type *res_len)
{
  struct bin_messagetable_item *ret = NULL;
  rc_uint_type len;

  *res_len = 0;
  if (mcset_bin_out_is_unicode == 1)
    {
      unichar *ht = n->message;
      rc_uint_type txt_len;

      txt_len = unichar_len (n->message);
      if (mcset_automatic_null_termination && txt_len != 0)
	{
	  while (txt_len > 0 && ht[txt_len - 1] > 0 && ht[txt_len - 1] < 0x20)
	    ht[--txt_len] = 0;
	}
      txt_len *= sizeof (unichar);
      len = BIN_MESSAGETABLE_ITEM_SIZE + txt_len + sizeof (unichar);
      ret = res_alloc ((len + 3) & ~3);
      memset (ret, 0, (len + 3) & ~3);
      target_put_16 (ret->length, (len + 3) & ~3);
      target_put_16 (ret->flags, MESSAGE_RESOURCE_UNICODE);
      txt_len = 0;
      while (*ht != 0)
	{
	  target_put_16 (ret->data + txt_len, *ht++);
	  txt_len += 2;
	}
    }
  else
    {
      rc_uint_type txt_len, l;
      char *cvt_txt;

      codepage_from_unicode( &l, n->message, &cvt_txt, n->lang->lang_info.wincp);
      if (! cvt_txt)
	fatal ("Failed to convert message to language codepage.\n");
      txt_len = strlen (cvt_txt);
      if (mcset_automatic_null_termination && txt_len > 0)
	{
	  while (txt_len > 0 && cvt_txt[txt_len - 1] > 0 && cvt_txt[txt_len - 1] < 0x20)
	    cvt_txt[--txt_len] = 0;
	}
      len = BIN_MESSAGETABLE_ITEM_SIZE + txt_len + 1;
      ret = res_alloc ((len + 3) & ~3);
      memset (ret, 0, (len + 3) & ~3);
      target_put_16 (ret->length, (len + 3) & ~3);
      target_put_16 (ret->flags, 0);
      strcpy ((char *) ret->data, cvt_txt);
    }
  *res_len = (len + 3) & ~3;
  return ret;
}

static void
mc_write_blocks (struct bin_messagetable *mtbl, mc_node_lang **nl, mc_msg_item *ml, int elems)
{
  int i, idx = 0;
  rc_uint_type exid;

  if (! nl)
    return;
  i = 0;
  while (i < elems)
    {
      target_put_32 (mtbl->items[idx].lowid, nl[i]->vid);
      target_put_32 (mtbl->items[idx].highid, nl[i]->vid);
      target_put_32 (mtbl->items[idx].offset, ml[i].res_off);
      exid = nl[i++]->vid;
      while (i < elems && nl[i]->vid == exid + 1)
	{
	  target_put_32 (mtbl->items[idx].highid, nl[i]->vid);
	  exid = nl[i++]->vid;
	}
      ++idx;
    }
}

static void
set_windmc_bfd_content (const void *data, rc_uint_type off, rc_uint_type length)
{
  if (! bfd_set_section_contents (mc_bfd.abfd, mc_bfd.sec, data, off, length))
    bfd_fatal ("bfd_set_section_contents");
}

static void
windmc_write_bin (const char *filename, mc_node_lang **nl, int elems)
{
  unsigned long sec_length = 1;
  int block_count, i;
  mc_msg_item *mi;
  struct bin_messagetable *mtbl;
  rc_uint_type dta_off, dta_start;

  if (elems <= 0)
    return;
  mc_bfd.abfd = windmc_open_as_binary (filename);
  mc_bfd.sec = bfd_make_section (mc_bfd.abfd, ".data");
  if (mc_bfd.sec == NULL)
    bfd_fatal ("bfd_make_section");
  if (! bfd_set_section_flags (mc_bfd.abfd, mc_bfd.sec,
			       (SEC_HAS_CONTENTS | SEC_ALLOC
			        | SEC_LOAD | SEC_DATA)))
    bfd_fatal ("bfd_set_section_flags");
  /* Requiring this is probably a bug in BFD.  */
  mc_bfd.sec->output_section = mc_bfd.sec;

  block_count = mc_get_block_count (nl, elems);

  dta_off = (rc_uint_type) ((BIN_MESSAGETABLE_BLOCK_SIZE * block_count) + BIN_MESSAGETABLE_SIZE - 4);
  dta_start = dta_off = (dta_off + 3) & ~3;
  mi = xmalloc (sizeof (mc_msg_item) * elems);
  mtbl = xmalloc (dta_start);

  /* Clear header region.  */
  memset (mtbl, 0, dta_start);
  target_put_32 (mtbl->cblocks, block_count);
  /* Prepare items section for output.  */
  for (i = 0; i < elems; i++)
    {
      mi[i].res_off = dta_off;
      mi[i].res = mc_generate_bin_item (nl[i], &mi[i].res_len);
      dta_off += mi[i].res_len;
    }
  sec_length = (dta_off + 3) & ~3;
  if (! bfd_set_section_size (mc_bfd.abfd, mc_bfd.sec, sec_length))
    bfd_fatal ("bfd_set_section_size");
  /* Make sure we write the complete block.  */
  set_windmc_bfd_content ("\0", sec_length - 1, 1);

  /* Write block information.  */
  mc_write_blocks (mtbl, nl, mi, elems);

  set_windmc_bfd_content (mtbl, 0, dta_start);

  /* Write items.  */
  for (i = 0; i < elems; i++)
    set_windmc_bfd_content (mi[i].res, mi[i].res_off, mi[i].res_len);

  free (mtbl);
  free (mi);
  bfd_close (mc_bfd.abfd);
  mc_bfd.abfd = NULL;
  mc_bfd.sec = NULL;
}

static void
write_bin (void)
{
  mc_node_lang *n = NULL;
  int i, c;

  if (! mc_nodes_lang_count)
    return;

  i = 0;
  while (i < mc_nodes_lang_count)
    {
      char *nd;
      char *filename;

      if (n && n->lang == mc_nodes_lang[i]->lang)
	{
	  i++;
	  continue;
	}
      n = mc_nodes_lang[i];
      c = i + 1;
      while (c < mc_nodes_lang_count && n->lang == mc_nodes_lang[c]->lang)
	c++;
      nd = convert_unicode_to_ACP (n->lang->sval);

      /* Prepare filename for binary output.  */
      filename = xmalloc (strlen (nd) + 4 + 1 + strlen (mcset_mc_basename) + 1 + strlen (mcset_rc_dir));
      strcpy (filename, mcset_rc_dir);
      if (mcset_prefix_bin)
	sprintf (filename + strlen (filename), "%s_", mcset_mc_basename);
      strcat (filename, nd);
      strcat (filename, ".bin");

      /* Write message file.  */
      windmc_write_bin (filename, &mc_nodes_lang[i], (c - i));

      free (filename);
      i = c;
    }
}

static void
write_rc (FILE *fp)
{
  mc_node_lang *n;
  int i, l;

  fprintf (fp,
    "/* Do not edit this file manually.\n"
    "   This file is autogenerated by windmc.  */\n\n");
  if (! mc_nodes_lang_count)
    return;
  n = NULL;
  i = 0;
  for (l = 0; l < mc_nodes_lang_count; l++)
    {
      if (n && n->lang == mc_nodes_lang[l]->lang)
	continue;
      ++i;
      n = mc_nodes_lang[l];
      fprintf (fp, "\n// Country: %s\n// Language: %s\n#pragma code_page(%u)\n",
	n->lang->lang_info.country, n->lang->lang_info.name,
	(unsigned) n->lang->lang_info.wincp);
      fprintf (fp, "LANGUAGE 0x%lx, 0x%lx\n", (long) (n->lang->nval & 0x3ff),
	(long) ((n->lang->nval & 0xffff) >> 10));
      fprintf (fp, "1 MESSAGETABLE \"");
      if (mcset_prefix_bin)
	fprintf (fp, "%s_", mcset_mc_basename);
      unicode_print (fp, n->lang->sval, unichar_len (n->lang->sval));
      fprintf (fp, ".bin\"\n");
    }
}

static void
write_dbg (FILE *fp)
{
  mc_node *h;

  fprintf (fp,
    "/* Do not edit this file manually.\n"
    "   This file is autogenerated by windmc.\n\n"
    "   This file maps each message ID value in to a text string that contains\n"
    "   the symbolic name used for it.  */\n\n");

  fprintf (fp,
    "struct %sSymbolicName\n"
    "{\n  ", mcset_mc_basename);
  if (mcset_msg_id_typedef)
    unicode_print (fp, mcset_msg_id_typedef, unichar_len (mcset_msg_id_typedef));
  else
    fprintf (fp, "DWORD");
  fprintf (fp,
    " MessageId;\n"
    "  char *SymbolicName;\n"
    "} %sSymbolicNames[] =\n"
    "{\n", mcset_mc_basename);
  h = mc_nodes;
  while (h != NULL)
    {
      if (h->symbol)
	write_dbg_define (fp, h->symbol, mcset_msg_id_typedef);
      h = h->next;
    }
  fprintf (fp, "  { (");
  if (mcset_msg_id_typedef)
    unicode_print (fp, mcset_msg_id_typedef, unichar_len (mcset_msg_id_typedef));
  else
    fprintf (fp, "DWORD");
  fprintf (fp,
    ") 0xffffffff, NULL }\n"
    "};\n");
}

static void
write_header (FILE *fp)
{
  char *s;
  int i;
  const mc_keyword *key;
  mc_node *h;

  fprintf (fp,
    "/* Do not edit this file manually.\n"
    "   This file is autogenerated by windmc.  */\n\n"
    "//\n//  The values are 32 bit layed out as follows:\n//\n"
    "//   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1\n"
    "//   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0\n"
    "//  +---+-+-+-----------------------+-------------------------------+\n"
    "//  |Sev|C|R|     Facility          |               Code            |\n"
    "//  +---+-+-+-----------------------+-------------------------------+\n//\n"
    "//  where\n//\n"
    "//      C    - is the Customer code flag\n//\n"
    "//      R    - is a reserved bit\n//\n"
    "//      Code - is the facility's status code\n//\n");

  h = mc_nodes;

  fprintf (fp, "//      Sev  - is the severity code\n//\n");
  if (mc_severity_codes_count != 0)
    {
      for (i = 0; i < mc_severity_codes_count; i++)
	{
	  key = mc_severity_codes[i];
	  fprintf (fp, "//           %s - %02lx\n", convert_unicode_to_ACP (key->usz),
		   (unsigned long) key->nval);
	  if (key->sval && key->sval[0] != 0)
	    {
	      if (! mcset_out_values_are_decimal)
		fprintf (fp, "#define %s 0x%lx\n", convert_unicode_to_ACP (key->sval),
			 (unsigned long) key->nval);
	      else
		fprintf (fp, "#define %s 0x%lu\n", convert_unicode_to_ACP (key->sval),
			 (unsigned long) key->nval);
	    }
	}
      fprintf (fp, "//\n");
    }
  fprintf (fp, "//      Facility - is the facility code\n//\n");
  if (mc_facility_codes_count != 0)
    {
      for (i = 0; i < mc_facility_codes_count; i++)
	{
	  key = mc_facility_codes[i];
	  fprintf (fp, "//           %s - %04lx\n", convert_unicode_to_ACP (key->usz),
		   (unsigned long) key->nval);
	  if (key->sval && key->sval[0] != 0)
	    {
	      if (! mcset_out_values_are_decimal)
		fprintf (fp, "#define %s 0x%lx\n", convert_unicode_to_ACP (key->sval),
			 (unsigned long) key->nval);
	      else
		fprintf (fp, "#define %s 0x%lu\n", convert_unicode_to_ACP (key->sval),
			 (unsigned long) key->nval);
	    }
	}
      fprintf (fp, "//\n");
    }
  fprintf (fp, "\n");
  while (h != NULL)
    {
      if (h->user_text)
	{
	  s = convert_unicode_to_ACP (h->user_text);
	  if (s)
	    fprintf (fp, "%s", s);
	}
      if (h->symbol)
	write_header_define (fp, h->symbol, h->vid, mcset_msg_id_typedef, h->sub);
      h = h->next;
    }
}

static const char *
mc_unify_path (const char *path)
{
  char *end;
  char *hsz;

  if (! path || *path == 0)
    return "./";
  hsz = xmalloc (strlen (path) + 2);
  strcpy (hsz, path);
  end = hsz + strlen (hsz);
  if (hsz[-1] != '/' && hsz[-1] != '\\')
    strcpy (end, "/");
  while ((end = strchr (hsz, '\\')) != NULL)
    *end = '/';
  return hsz;
}

int main (int, char **);

int
main (int argc, char **argv)
{
  FILE *h_fp;
  int c;
  char *target, *input_filename;
  int verbose;

#if defined (HAVE_SETLOCALE) && defined (HAVE_LC_MESSAGES)
  setlocale (LC_MESSAGES, "");
#endif
#if defined (HAVE_SETLOCALE)
  setlocale (LC_CTYPE, "");
#endif
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  program_name = argv[0];
  xmalloc_set_program_name (program_name);

  expandargv (&argc, &argv);

  bfd_init ();
  set_default_bfd_target ();

  target = NULL;
  verbose = 0;
  input_filename = NULL;

  res_init ();

  while ((c = getopt_long (argc, argv, "C:F:O:h:e:m:r:x:aAbcdHunoUvV", long_options,
			   (int *) 0)) != EOF)
    {
      switch (c)
	{
	case 'b':
	  mcset_prefix_bin = 1;
	  break;
	case 'e':
	  {
	    mcset_header_ext = optarg;
	    if (mcset_header_ext[0] != '.' && mcset_header_ext[0] != 0)
	      {
		char *hsz = xmalloc (strlen (mcset_header_ext) + 2);

		sprintf (hsz, ".%s", mcset_header_ext);
		mcset_header_ext = hsz;
	      }
	  }
	  break;
	case 'h':
	  mcset_header_dir = mc_unify_path (optarg);
	  break;
	case 'r':
	  mcset_rc_dir = mc_unify_path (optarg);
	  break;
	case 'a':
	  mcset_text_in_is_unicode = 0;
	  break;
	case 'x':
	  if (*optarg != 0)
	    mcset_dbg_dir = mc_unify_path (optarg);
	  break;
	case 'A':
	  mcset_bin_out_is_unicode = 0;
	  break;
	case 'd':
	  mcset_out_values_are_decimal = 1;
	  break;
	case 'u':
	  mcset_text_in_is_unicode = 1;
	  break;
	case 'U':
	  mcset_bin_out_is_unicode = 1;
	  break;
	case 'c':
	  mcset_custom_bit = 1;
	  break;
	case 'n':
	  mcset_automatic_null_termination = 1;
	  break;
	case 'o':
	  mcset_use_hresult = 1;
	  fatal ("option -o is not implemented until yet.\n");
	  break;
	case 'F':
	  target = optarg;
	  break;
	case 'v':
	  verbose ++;
	  break;
	case 'm':
	  mcset_max_message_length = strtol (optarg, (char **) NULL, 10);
	  break;
	case 'C':
	  mcset_codepage_in = strtol (optarg, (char **) NULL, 10);
	  break;
	case 'O':
	  mcset_codepage_out = strtol (optarg, (char **) NULL, 10);
	  break;
	case '?':
	case 'H':
	  usage (stdout, 0);
	  break;
	case 'V':
	  print_version ("windmc");
	  break;

	default:
	  usage (stderr, 1);
	  break;
	}
    }
  if (input_filename == NULL && optind < argc)
    {
      input_filename = argv[optind];
      ++optind;
    }

  set_endianess (NULL, target);

  if (input_filename == NULL)
    {
      fprintf (stderr, "Error: No input file was specified.\n");
      usage (stderr, 1);
    }
  mc_set_inputfile (input_filename);

  if (!probe_codepage (&mcset_codepage_in, &mcset_text_in_is_unicode, "codepage_in", 0))
    usage (stderr, 1);
  if (mcset_codepage_out == 0)
    mcset_codepage_out = 1252;
  if (! unicode_is_valid_codepage (mcset_codepage_out))
    fatal ("Code page 0x%x is unknown.", (unsigned int) mcset_codepage_out);
  if (mcset_codepage_out == CP_UTF16)
    fatal ("UTF16 is no valid text output code page.");
  if (verbose)
    {
      fprintf (stderr, "// Default target is %s and it is %s endian.\n",
	def_target_arch, (target_is_bigendian ? "big" : "little"));
      fprintf (stderr, "// Input codepage: 0x%x\n", (unsigned int) mcset_codepage_in);
      fprintf (stderr, "// Output codepage: 0x%x\n", (unsigned int) mcset_codepage_out);
    }

  if (argc != optind)
    usage (stderr, 1);

  /* Initialize mcset_mc_basename.  */
  {
    const char *bn, *bn2;
    char *hsz;

    bn = strrchr (input_filename, '/');
    bn2 = strrchr (input_filename, '\\');
    if (! bn)
      bn = bn2;
    if (bn && bn2 && bn < bn2)
      bn = bn2;
    if (! bn)
      bn = input_filename;
    else
      bn++;
    mcset_mc_basename = hsz = xstrdup (bn);

    /* Cut of right-hand extension.  */
    if ((hsz = strrchr (hsz, '.')) != NULL)
      *hsz = 0;
  }

  /* Load the input file and do code page transformations to UTF16.  */
  {
    unichar *u;
    rc_uint_type ul;
    char *buff;
    long flen;
    FILE *fp = fopen (input_filename, "rb");

    if (!fp)
      fatal (_("unable to open file ,%s' for input.\n"), input_filename);

    fseek (fp, 0, SEEK_END);
    flen = ftell (fp);
    fseek (fp, 0, SEEK_SET);
    buff = malloc (flen + 3);
    memset (buff, 0, flen + 3);
    fread (buff, 1, flen, fp);
    fclose (fp);
    if (mcset_text_in_is_unicode != 1)
      {
	unicode_from_codepage (&ul, &u, buff, mcset_codepage_in);
	if (! u)
	  fatal ("Failed to convert input to UFT16\n");
	mc_set_content (u);
      }
    else
      {
	if ((flen & 1) != 0)
	  fatal (_("input file does not seems to be UFT16.\n"));
	mc_set_content ((unichar *) buff);
      }
    free (buff);
  }

  while (yyparse ())
    ;

  do_sorts ();

  h_fp = mc_create_path_text_file (mcset_header_dir, mcset_header_ext);
  write_header (h_fp);
  fclose (h_fp);

  h_fp = mc_create_path_text_file (mcset_rc_dir, ".rc");
  write_rc (h_fp);
  fclose (h_fp);

  if (mcset_dbg_dir != NULL)
    {
      h_fp = mc_create_path_text_file (mcset_dbg_dir, ".dbg");
      write_dbg (h_fp);
      fclose (h_fp);
    }
  write_bin ();

  if (mc_nodes_lang)
    free (mc_nodes_lang);
  if (mc_severity_codes)
    free (mc_severity_codes);
  if (mc_facility_codes)
    free (mc_facility_codes);

  xexit (0);
  return 0;
}
