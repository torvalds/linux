%{ /* rcparse.y -- parser for Windows rc files
   Copyright 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2005, 2007
   Free Software Foundation, Inc.
   Written by Ian Lance Taylor, Cygnus Support.
   Extended by Kai Tietz, Onevision.

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

/* This is a parser for Windows rc files.  It is based on the parser
   by Gunther Ebert <gunther.ebert@ixos-leipzig.de>.  */

#include "sysdep.h"
#include "bfd.h"
#include "bucomm.h"
#include "libiberty.h"
#include "windres.h"
#include "safe-ctype.h"

/* The current language.  */

static unsigned short language;

/* The resource information during a sub statement.  */

static rc_res_res_info sub_res_info;

/* Dialog information.  This is built by the nonterminals styles and
   controls.  */

static rc_dialog dialog;

/* This is used when building a style.  It is modified by the
   nonterminal styleexpr.  */

static unsigned long style;

/* These are used when building a control.  They are set before using
   control_params.  */

static rc_uint_type base_style;
static rc_uint_type default_style;
static rc_res_id class;
static rc_res_id res_text_field;
static unichar null_unichar;

/* This is used for COMBOBOX, LISTBOX and EDITTEXT which
   do not allow resource 'text' field in control definition. */
static const rc_res_id res_null_text = { 1, {{0, &null_unichar}}};

%}

%union
{
  rc_accelerator acc;
  rc_accelerator *pacc;
  rc_dialog_control *dialog_control;
  rc_menuitem *menuitem;
  struct
  {
    rc_rcdata_item *first;
    rc_rcdata_item *last;
  } rcdata;
  rc_rcdata_item *rcdata_item;
  rc_fixed_versioninfo *fixver;
  rc_ver_info *verinfo;
  rc_ver_stringinfo *verstring;
  rc_ver_varinfo *vervar;
  rc_toolbar_item *toobar_item;
  rc_res_id id;
  rc_res_res_info res_info;
  struct
  {
    rc_uint_type on;
    rc_uint_type off;
  } memflags;
  struct
  {
    rc_uint_type val;
    /* Nonzero if this number was explicitly specified as long.  */
    int dword;
  } i;
  rc_uint_type il;
  rc_uint_type is;
  const char *s;
  struct
  {
    rc_uint_type length;
    const char *s;
  } ss;
  unichar *uni;
  struct
  {
    rc_uint_type length;
    const unichar *s;
  } suni;
};

%token BEG END
%token ACCELERATORS VIRTKEY ASCII NOINVERT SHIFT CONTROL ALT
%token BITMAP
%token CURSOR
%token DIALOG DIALOGEX EXSTYLE CAPTION CLASS STYLE
%token AUTO3STATE AUTOCHECKBOX AUTORADIOBUTTON CHECKBOX COMBOBOX CTEXT
%token DEFPUSHBUTTON EDITTEXT GROUPBOX LISTBOX LTEXT PUSHBOX PUSHBUTTON
%token RADIOBUTTON RTEXT SCROLLBAR STATE3 USERBUTTON
%token BEDIT HEDIT IEDIT
%token FONT
%token ICON
%token ANICURSOR ANIICON DLGINCLUDE DLGINIT FONTDIR HTML MANIFEST PLUGPLAY VXD TOOLBAR BUTTON
%token LANGUAGE CHARACTERISTICS VERSIONK
%token MENU MENUEX MENUITEM SEPARATOR POPUP CHECKED GRAYED HELP INACTIVE
%token MENUBARBREAK MENUBREAK
%token MESSAGETABLE
%token RCDATA
%token STRINGTABLE
%token VERSIONINFO FILEVERSION PRODUCTVERSION FILEFLAGSMASK FILEFLAGS
%token FILEOS FILETYPE FILESUBTYPE BLOCKSTRINGFILEINFO BLOCKVARFILEINFO
%token VALUE
%token <s> BLOCK
%token MOVEABLE FIXED PURE IMPURE PRELOAD LOADONCALL DISCARDABLE
%token NOT
%token <uni> QUOTEDUNISTRING
%token <s> QUOTEDSTRING STRING
%token <i> NUMBER
%token <suni> SIZEDUNISTRING
%token <ss> SIZEDSTRING
%token IGNORED_TOKEN

%type <pacc> acc_entries
%type <acc> acc_entry acc_event
%type <dialog_control> control control_params
%type <menuitem> menuitems menuitem menuexitems menuexitem
%type <rcdata> optrcdata_data optrcdata_data_int rcdata_data
%type <rcdata_item> opt_control_data
%type <fixver> fixedverinfo
%type <verinfo> verblocks
%type <verstring> vervals
%type <vervar> vertrans
%type <toobar_item> toolbar_data
%type <res_info> suboptions memflags_move_discard memflags_move
%type <memflags> memflag
%type <id> id rcdata_id optresidc resref resid cresid
%type <il> exstyle parennumber
%type <il> numexpr posnumexpr cnumexpr optcnumexpr cposnumexpr
%type <is> acc_options acc_option menuitem_flags menuitem_flag
%type <s> file_name
%type <uni> res_unicode_string resname res_unicode_string_concat
%type <ss> sizedstring
%type <suni> sizedunistring
%type <i> sizednumexpr sizedposnumexpr

%left '|'
%left '^'
%left '&'
%left '+' '-'
%left '*' '/' '%'
%right '~' NEG

%%

input:
	  /* empty */
	| input accelerator
	| input bitmap
	| input cursor
	| input dialog
	| input font
	| input icon
	| input language
	| input menu
	| input menuex
	| input messagetable
	| input stringtable
	| input toolbar
	| input user
	| input versioninfo
	| input IGNORED_TOKEN
	;

/* Accelerator resources.  */

accelerator:
	  id ACCELERATORS suboptions BEG acc_entries END
	  {
	    define_accelerator ($1, &$3, $5);
	    if (yychar != YYEMPTY)
	      YYERROR;
	    rcparse_discard_strings ();
	  }
	;

acc_entries:
	  /* empty */
	  {
	    $$ = NULL;
	  }
	| acc_entries acc_entry
	  {
	    rc_accelerator *a;

	    a = (rc_accelerator *) res_alloc (sizeof *a);
	    *a = $2;
	    if ($1 == NULL)
	      $$ = a;
	    else
	      {
		rc_accelerator **pp;

		for (pp = &$1->next; *pp != NULL; pp = &(*pp)->next)
		  ;
		*pp = a;
		$$ = $1;
	      }
	  }
	;

acc_entry:
	  acc_event cposnumexpr
	  {
	    $$ = $1;
	    $$.id = $2;
	  }
	| acc_event cposnumexpr ',' acc_options
	  {
	    $$ = $1;
	    $$.id = $2;
	    $$.flags |= $4;
	    if (($$.flags & ACC_VIRTKEY) == 0
		&& ($$.flags & (ACC_SHIFT | ACC_CONTROL)) != 0)
	      rcparse_warning (_("inappropriate modifiers for non-VIRTKEY"));
	  }
	;

acc_event:
	  QUOTEDSTRING
	  {
	    const char *s = $1;
	    char ch;

	    $$.next = NULL;
	    $$.id = 0;
	    ch = *s;
	    if (ch != '^')
	      $$.flags = 0;
	    else
	      {
		$$.flags = ACC_CONTROL | ACC_VIRTKEY;
		++s;
		ch = TOUPPER (s[0]);
	      }
	    $$.key = ch;
	    if (s[1] != '\0')
	      rcparse_warning (_("accelerator should only be one character"));
	  }
	| posnumexpr
	  {
	    $$.next = NULL;
	    $$.flags = 0;
	    $$.id = 0;
	    $$.key = $1;
	  }
	;

acc_options:
	  acc_option
	  {
	    $$ = $1;
	  }
	| acc_options ',' acc_option
	  {
	    $$ = $1 | $3;
	  }
	/* I've had one report that the comma is optional.  */
	| acc_options acc_option
	  {
	    $$ = $1 | $2;
	  }
	;

acc_option:
	  VIRTKEY
	  {
	    $$ = ACC_VIRTKEY;
	  }
	| ASCII
	  {
	    /* This is just the absence of VIRTKEY.  */
	    $$ = 0;
	  }
	| NOINVERT
	  {
	    $$ = ACC_NOINVERT;
	  }
	| SHIFT
	  {
	    $$ = ACC_SHIFT;
	  }
	| CONTROL
	  {
	    $$ = ACC_CONTROL;
	  }
	| ALT
	  {
	    $$ = ACC_ALT;
	  }
	;

/* Bitmap resources.  */

bitmap:
	  id BITMAP memflags_move file_name
	  {
	    define_bitmap ($1, &$3, $4);
	    if (yychar != YYEMPTY)
	      YYERROR;
	    rcparse_discard_strings ();
	  }
	;

/* Cursor resources.  */

cursor:
	  id CURSOR memflags_move_discard file_name
	  {
	    define_cursor ($1, &$3, $4);
	    if (yychar != YYEMPTY)
	      YYERROR;
	    rcparse_discard_strings ();
	  }
	;

/* Dialog resources.  */

dialog:
	  id DIALOG memflags_move exstyle posnumexpr cnumexpr cnumexpr
	    cnumexpr
	    {
	      memset (&dialog, 0, sizeof dialog);
	      dialog.x = $5;
	      dialog.y = $6;
	      dialog.width = $7;
	      dialog.height = $8;
	      dialog.style = WS_POPUP | WS_BORDER | WS_SYSMENU;
	      dialog.exstyle = $4;
	      dialog.menu.named = 1;
	      dialog.class.named = 1;
	      dialog.font = NULL;
	      dialog.ex = NULL;
	      dialog.controls = NULL;
	      sub_res_info = $3;
	      style = 0;
	    }
	    styles BEG controls END
	  {
	    define_dialog ($1, &sub_res_info, &dialog);
	    if (yychar != YYEMPTY)
	      YYERROR;
	    rcparse_discard_strings ();
	  }
	| id DIALOGEX memflags_move exstyle posnumexpr cnumexpr cnumexpr
	    cnumexpr
	    {
	      memset (&dialog, 0, sizeof dialog);
	      dialog.x = $5;
	      dialog.y = $6;
	      dialog.width = $7;
	      dialog.height = $8;
	      dialog.style = WS_POPUP | WS_BORDER | WS_SYSMENU;
	      dialog.exstyle = $4;
	      dialog.menu.named = 1;
	      dialog.class.named = 1;
	      dialog.font = NULL;
	      dialog.ex = ((rc_dialog_ex *)
			   res_alloc (sizeof (rc_dialog_ex)));
	      memset (dialog.ex, 0, sizeof (rc_dialog_ex));
	      dialog.controls = NULL;
	      sub_res_info = $3;
	      style = 0;
	    }
	    styles BEG controls END
	  {
	    define_dialog ($1, &sub_res_info, &dialog);
	    if (yychar != YYEMPTY)
	      YYERROR;
	    rcparse_discard_strings ();
	  }
	| id DIALOGEX memflags_move exstyle posnumexpr cnumexpr cnumexpr
	    cnumexpr cnumexpr
	    {
	      memset (&dialog, 0, sizeof dialog);
	      dialog.x = $5;
	      dialog.y = $6;
	      dialog.width = $7;
	      dialog.height = $8;
	      dialog.style = WS_POPUP | WS_BORDER | WS_SYSMENU;
	      dialog.exstyle = $4;
	      dialog.menu.named = 1;
	      dialog.class.named = 1;
	      dialog.font = NULL;
	      dialog.ex = ((rc_dialog_ex *)
			   res_alloc (sizeof (rc_dialog_ex)));
	      memset (dialog.ex, 0, sizeof (rc_dialog_ex));
	      dialog.ex->help = $9;
	      dialog.controls = NULL;
	      sub_res_info = $3;
	      style = 0;
	    }
	    styles BEG controls END
	  {
	    define_dialog ($1, &sub_res_info, &dialog);
	    if (yychar != YYEMPTY)
	      YYERROR;
	    rcparse_discard_strings ();
	  }
	;

exstyle:
	  /* empty */
	  {
	    $$ = 0;
	  }
	| EXSTYLE '=' numexpr
	  {
	    $$ = $3;
	  }
	;

styles:
	  /* empty */
	| styles CAPTION res_unicode_string_concat
	  {
	    dialog.style |= WS_CAPTION;
	    style |= WS_CAPTION;
	    dialog.caption = $3;
	  }
	| styles CLASS id
	  {
	    dialog.class = $3;
	  }
	| styles STYLE
	    styleexpr
	  {
	    dialog.style = style;
	  }
	| styles EXSTYLE numexpr
	  {
	    dialog.exstyle = $3;
	  }
	| styles CLASS res_unicode_string_concat
	  {
	    res_unistring_to_id (& dialog.class, $3);
	  }
	| styles FONT numexpr ',' res_unicode_string_concat
	  {
	    dialog.style |= DS_SETFONT;
	    style |= DS_SETFONT;
	    dialog.pointsize = $3;
	    dialog.font = $5;
	    if (dialog.ex != NULL)
	      {
		dialog.ex->weight = 0;
		dialog.ex->italic = 0;
		dialog.ex->charset = 1;
	      }
	  }
	| styles FONT numexpr ',' res_unicode_string_concat cnumexpr
	  {
	    dialog.style |= DS_SETFONT;
	    style |= DS_SETFONT;
	    dialog.pointsize = $3;
	    dialog.font = $5;
	    if (dialog.ex == NULL)
	      rcparse_warning (_("extended FONT requires DIALOGEX"));
	    else
	      {
		dialog.ex->weight = $6;
		dialog.ex->italic = 0;
		dialog.ex->charset = 1;
	      }
	  }
	| styles FONT numexpr ',' res_unicode_string_concat cnumexpr cnumexpr
	  {
	    dialog.style |= DS_SETFONT;
	    style |= DS_SETFONT;
	    dialog.pointsize = $3;
	    dialog.font = $5;
	    if (dialog.ex == NULL)
	      rcparse_warning (_("extended FONT requires DIALOGEX"));
	    else
	      {
		dialog.ex->weight = $6;
		dialog.ex->italic = $7;
		dialog.ex->charset = 1;
	      }
	  }
	| styles FONT numexpr ',' res_unicode_string_concat cnumexpr cnumexpr cnumexpr
	  {
	    dialog.style |= DS_SETFONT;
	    style |= DS_SETFONT;
	    dialog.pointsize = $3;
	    dialog.font = $5;
	    if (dialog.ex == NULL)
	      rcparse_warning (_("extended FONT requires DIALOGEX"));
	    else
	      {
		dialog.ex->weight = $6;
		dialog.ex->italic = $7;
		dialog.ex->charset = $8;
	      }
	  }
	| styles MENU id
	  {
	    dialog.menu = $3;
	  }
	| styles CHARACTERISTICS numexpr
	  {
	    sub_res_info.characteristics = $3;
	  }
	| styles LANGUAGE numexpr cnumexpr
	  {
	    sub_res_info.language = $3 | ($4 << SUBLANG_SHIFT);
	  }
	| styles VERSIONK numexpr
	  {
	    sub_res_info.version = $3;
	  }
	;

controls:
	  /* empty */
	| controls control
	  {
	    rc_dialog_control **pp;

	    for (pp = &dialog.controls; *pp != NULL; pp = &(*pp)->next)
	      ;
	    *pp = $2;
	  }
	;

control:
	  AUTO3STATE optresidc
	    {
	      default_style = BS_AUTO3STATE | WS_TABSTOP;
	      base_style = BS_AUTO3STATE;
	      class.named = 0;
	      class.u.id = CTL_BUTTON;
	      res_text_field = $2;	
	    }
	    control_params
	  {
	    $$ = $4;
	  }
	| AUTOCHECKBOX optresidc
	    {
	      default_style = BS_AUTOCHECKBOX | WS_TABSTOP;
	      base_style = BS_AUTOCHECKBOX;
	      class.named = 0;
	      class.u.id = CTL_BUTTON;
	      res_text_field = $2;	
	    }
	    control_params
	  {
	    $$ = $4;
	  }
	| AUTORADIOBUTTON optresidc
	    {
	      default_style = BS_AUTORADIOBUTTON | WS_TABSTOP;
	      base_style = BS_AUTORADIOBUTTON;
	      class.named = 0;
	      class.u.id = CTL_BUTTON;
	      res_text_field = $2;	
	    }
	    control_params
	  {
	    $$ = $4;
	  }
	| BEDIT optresidc
	    {
	      default_style = ES_LEFT | WS_BORDER | WS_TABSTOP;
	      base_style = ES_LEFT | WS_BORDER | WS_TABSTOP;
	      class.named = 0;
	      class.u.id = CTL_EDIT;
	      res_text_field = $2;	
	    }
	    control_params
	  {
	    $$ = $4;
	    if (dialog.ex == NULL)
	      rcparse_warning (_("BEDIT requires DIALOGEX"));
	    res_string_to_id (&$$->class, "BEDIT");
	  }
	| CHECKBOX optresidc
	    {
	      default_style = BS_CHECKBOX | WS_TABSTOP;
	      base_style = BS_CHECKBOX | WS_TABSTOP;
	      class.named = 0;
	      class.u.id = CTL_BUTTON;
	      res_text_field = $2;	
	    }
	    control_params
	  {
	    $$ = $4;
	  }
	| COMBOBOX
	    {
	      /* This is as per MSDN documentation.  With some (???)
		 versions of MS rc.exe their is no default style.  */
	      default_style = CBS_SIMPLE | WS_TABSTOP;
	      base_style = 0;
	      class.named = 0;
	      class.u.id = CTL_COMBOBOX;
	      res_text_field = res_null_text;	
	    }
	    control_params
	  {
	    $$ = $3;
	  }
	| CONTROL optresidc numexpr cresid control_styleexpr cnumexpr
	    cnumexpr cnumexpr cnumexpr optcnumexpr opt_control_data
	  {
	    $$ = define_control ($2, $3, $6, $7, $8, $9, $4, style, $10);
	    if ($11 != NULL)
	      {
		if (dialog.ex == NULL)
		  rcparse_warning (_("control data requires DIALOGEX"));
		$$->data = $11;
	      }
	  }
	| CONTROL optresidc numexpr cresid control_styleexpr cnumexpr
	    cnumexpr cnumexpr cnumexpr cnumexpr cnumexpr opt_control_data
	  {
	    $$ = define_control ($2, $3, $6, $7, $8, $9, $4, style, $10);
	    if (dialog.ex == NULL)
	      rcparse_warning (_("help ID requires DIALOGEX"));
	    $$->help = $11;
	    $$->data = $12;
	  }
	| CTEXT optresidc
	    {
	      default_style = SS_CENTER | WS_GROUP;
	      base_style = SS_CENTER;
	      class.named = 0;
	      class.u.id = CTL_STATIC;
	      res_text_field = $2;	
	    }
	    control_params
	  {
	    $$ = $4;
	  }
	| DEFPUSHBUTTON optresidc
	    {
	      default_style = BS_DEFPUSHBUTTON | WS_TABSTOP;
	      base_style = BS_DEFPUSHBUTTON | WS_TABSTOP;
	      class.named = 0;
	      class.u.id = CTL_BUTTON;
	      res_text_field = $2;	
	    }
	    control_params
	  {
	    $$ = $4;
	  }
	| EDITTEXT
	    {
	      default_style = ES_LEFT | WS_BORDER | WS_TABSTOP;
	      base_style = ES_LEFT | WS_BORDER | WS_TABSTOP;
	      class.named = 0;
	      class.u.id = CTL_EDIT;
	      res_text_field = res_null_text;	
	    }
	    control_params
	  {
	    $$ = $3;
	  }
	| GROUPBOX optresidc
	    {
	      default_style = BS_GROUPBOX;
	      base_style = BS_GROUPBOX;
	      class.named = 0;
	      class.u.id = CTL_BUTTON;
	      res_text_field = $2;	
	    }
	    control_params
	  {
	    $$ = $4;
	  }
	| HEDIT optresidc
	    {
	      default_style = ES_LEFT | WS_BORDER | WS_TABSTOP;
	      base_style = ES_LEFT | WS_BORDER | WS_TABSTOP;
	      class.named = 0;
	      class.u.id = CTL_EDIT;
	      res_text_field = $2;	
	    }
	    control_params
	  {
	    $$ = $4;
	    if (dialog.ex == NULL)
	      rcparse_warning (_("IEDIT requires DIALOGEX"));
	    res_string_to_id (&$$->class, "HEDIT");
	  }
	| ICON resref numexpr cnumexpr cnumexpr opt_control_data
          {
	    $$ = define_icon_control ($2, $3, $4, $5, 0, 0, 0, $6,
				      dialog.ex);
          }
	| ICON resref numexpr cnumexpr cnumexpr cnumexpr cnumexpr
	    opt_control_data
          {
	    $$ = define_icon_control ($2, $3, $4, $5, 0, 0, 0, $8,
				      dialog.ex);
          }
	| ICON resref numexpr cnumexpr cnumexpr cnumexpr cnumexpr
	    icon_styleexpr optcnumexpr opt_control_data
          {
	    $$ = define_icon_control ($2, $3, $4, $5, style, $9, 0, $10,
				      dialog.ex);
          }
	| ICON resref numexpr cnumexpr cnumexpr cnumexpr cnumexpr
	    icon_styleexpr cnumexpr cnumexpr opt_control_data
          {
	    $$ = define_icon_control ($2, $3, $4, $5, style, $9, $10, $11,
				      dialog.ex);
          }
	| IEDIT optresidc
	    {
	      default_style = ES_LEFT | WS_BORDER | WS_TABSTOP;
	      base_style = ES_LEFT | WS_BORDER | WS_TABSTOP;
	      class.named = 0;
	      class.u.id = CTL_EDIT;
	      res_text_field = $2;	
	    }
	    control_params
	  {
	    $$ = $4;
	    if (dialog.ex == NULL)
	      rcparse_warning (_("IEDIT requires DIALOGEX"));
	    res_string_to_id (&$$->class, "IEDIT");
	  }
	| LISTBOX
	    {
	      default_style = LBS_NOTIFY | WS_BORDER;
	      base_style = LBS_NOTIFY | WS_BORDER;
	      class.named = 0;
	      class.u.id = CTL_LISTBOX;
	      res_text_field = res_null_text;	
	    }
	    control_params
	  {
	    $$ = $3;
	  }
	| LTEXT optresidc
	    {
	      default_style = SS_LEFT | WS_GROUP;
	      base_style = SS_LEFT;
	      class.named = 0;
	      class.u.id = CTL_STATIC;
	      res_text_field = $2;	
	    }
	    control_params
	  {
	    $$ = $4;
	  }
	| PUSHBOX optresidc
	    {
	      default_style = BS_PUSHBOX | WS_TABSTOP;
	      base_style = BS_PUSHBOX;
	      class.named = 0;
	      class.u.id = CTL_BUTTON;
	    }
	    control_params
	  {
	    $$ = $4;
	  }
	| PUSHBUTTON optresidc
	    {
	      default_style = BS_PUSHBUTTON | WS_TABSTOP;
	      base_style = BS_PUSHBUTTON | WS_TABSTOP;
	      class.named = 0;
	      class.u.id = CTL_BUTTON;
	      res_text_field = $2;	
	    }
	    control_params
	  {
	    $$ = $4;
	  }
	| RADIOBUTTON optresidc
	    {
	      default_style = BS_RADIOBUTTON | WS_TABSTOP;
	      base_style = BS_RADIOBUTTON;
	      class.named = 0;
	      class.u.id = CTL_BUTTON;
	      res_text_field = $2;	
	    }
	    control_params
	  {
	    $$ = $4;
	  }
	| RTEXT optresidc
	    {
	      default_style = SS_RIGHT | WS_GROUP;
	      base_style = SS_RIGHT;
	      class.named = 0;
	      class.u.id = CTL_STATIC;
	      res_text_field = $2;	
	    }
	    control_params
	  {
	    $$ = $4;
	  }
	| SCROLLBAR
	    {
	      default_style = SBS_HORZ;
	      base_style = 0;
	      class.named = 0;
	      class.u.id = CTL_SCROLLBAR;
	      res_text_field = res_null_text;	
	    }
	    control_params
	  {
	    $$ = $3;
	  }
	| STATE3 optresidc
	    {
	      default_style = BS_3STATE | WS_TABSTOP;
	      base_style = BS_3STATE;
	      class.named = 0;
	      class.u.id = CTL_BUTTON;
	      res_text_field = $2;	
	    }
	    control_params
	  {
	    $$ = $4;
	  }
	| USERBUTTON resref numexpr ',' numexpr ',' numexpr ','
	    numexpr ',' numexpr ',' 
	    { style = WS_CHILD | WS_VISIBLE; }
	    styleexpr optcnumexpr
	  {
	    rc_res_id cid;
	    cid.named = 0;
	    cid.u.id = CTL_BUTTON;
	    $$ = define_control ($2, $3, $5, $7, $9, $11, cid,
				 style, $15);
	  }
	;

/* Parameters for a control.  The static variables DEFAULT_STYLE,
   BASE_STYLE, and CLASS must be initialized before this nonterminal
   is used.  DEFAULT_STYLE is the style to use if no style expression
   is specified.  BASE_STYLE is the base style to use if a style
   expression is specified; the style expression modifies the base
   style.  CLASS is the class of the control.  */

control_params:
	  numexpr cnumexpr cnumexpr cnumexpr cnumexpr opt_control_data
	  {
	    $$ = define_control (res_text_field, $1, $2, $3, $4, $5, class,
				 default_style | WS_CHILD | WS_VISIBLE, 0);
	    if ($6 != NULL)
	      {
		if (dialog.ex == NULL)
		  rcparse_warning (_("control data requires DIALOGEX"));
		$$->data = $6;
	      }
	  }
	| numexpr cnumexpr cnumexpr cnumexpr cnumexpr
	    control_params_styleexpr optcnumexpr opt_control_data
	  {
	    $$ = define_control (res_text_field, $1, $2, $3, $4, $5, class, style, $7);
	    if ($8 != NULL)
	      {
		if (dialog.ex == NULL)
		  rcparse_warning (_("control data requires DIALOGEX"));
		$$->data = $8;
	      }
	  }
	| numexpr cnumexpr cnumexpr cnumexpr cnumexpr
	    control_params_styleexpr cnumexpr cnumexpr opt_control_data
	  {
	    $$ = define_control (res_text_field, $1, $2, $3, $4, $5, class, style, $7);
	    if (dialog.ex == NULL)
	      rcparse_warning (_("help ID requires DIALOGEX"));
	    $$->help = $8;
	    $$->data = $9;
	  }
	;

cresid:
	  ',' resid
	  {
	    if ($2.named)
	      res_unistring_to_id (&$$, $2.u.n.name);
	    else
	      $$=$2;
	  }
	;

optresidc:
	  /* empty */
	  {
	    res_string_to_id (&$$, "");
	  }
	| resid ',' { $$=$1; }
	;

resid:
	  posnumexpr
	  {
	    $$.named = 0;
	    $$.u.id = $1;
	  }
	| res_unicode_string
	  {
	    $$.named = 1;
	    $$.u.n.name = $1;
	    $$.u.n.length = unichar_len ($1);
	  }
	;

opt_control_data:
	  /* empty */
	  {
	    $$ = NULL;
	  }
	| BEG optrcdata_data END
	  {
	    $$ = $2.first;
	  }
	;

/* These only exist to parse a reduction out of a common case.  */

control_styleexpr:
	  ','
	  { style = WS_CHILD | WS_VISIBLE; }
	  styleexpr
	;

icon_styleexpr:
	  ','
	  { style = SS_ICON | WS_CHILD | WS_VISIBLE; }
	  styleexpr
	;

control_params_styleexpr:
	  ','
	  { style = base_style | WS_CHILD | WS_VISIBLE; }
	  styleexpr
	;

/* Font resources.  */

font:
	  id FONT memflags_move_discard file_name
	  {
	    define_font ($1, &$3, $4);
	    if (yychar != YYEMPTY)
	      YYERROR;
	    rcparse_discard_strings ();
	  }
	;

/* Icon resources.  */

icon:
	  id ICON memflags_move_discard file_name
	  {
	    define_icon ($1, &$3, $4);
	    if (yychar != YYEMPTY)
	      YYERROR;
	    rcparse_discard_strings ();
	  }
	;

/* Language command.  This changes the static variable language, which
   affects all subsequent resources.  */

language:
	  LANGUAGE numexpr cnumexpr
	  {
	    language = $2 | ($3 << SUBLANG_SHIFT);
	  }
	;

/* Menu resources.  */

menu:
	  id MENU suboptions BEG menuitems END
	  {
	    define_menu ($1, &$3, $5);
	    if (yychar != YYEMPTY)
	      YYERROR;
	    rcparse_discard_strings ();
	  }
	;

menuitems:
	  /* empty */
	  {
	    $$ = NULL;
	  }
	| menuitems menuitem
	  {
	    if ($1 == NULL)
	      $$ = $2;
	    else
	      {
		rc_menuitem **pp;

		for (pp = &$1->next; *pp != NULL; pp = &(*pp)->next)
		  ;
		*pp = $2;
		$$ = $1;
	      }
	  }
	;

menuitem:
	  MENUITEM res_unicode_string_concat cnumexpr menuitem_flags
	  {
	    $$ = define_menuitem ($2, $3, $4, 0, 0, NULL);
	  }
	| MENUITEM SEPARATOR
	  {
	    $$ = define_menuitem (NULL, 0, 0, 0, 0, NULL);
	  }
	| POPUP res_unicode_string_concat menuitem_flags BEG menuitems END
	  {
	    $$ = define_menuitem ($2, 0, $3, 0, 0, $5);
	  }
	;

menuitem_flags:
	  /* empty */
	  {
	    $$ = 0;
	  }
	| menuitem_flags ',' menuitem_flag
	  {
	    $$ = $1 | $3;
	  }
	| menuitem_flags menuitem_flag
	  {
	    $$ = $1 | $2;
	  }
	;

menuitem_flag:
	  CHECKED
	  {
	    $$ = MENUITEM_CHECKED;
	  }
	| GRAYED
	  {
	    $$ = MENUITEM_GRAYED;
	  }
	| HELP
	  {
	    $$ = MENUITEM_HELP;
	  }
	| INACTIVE
	  {
	    $$ = MENUITEM_INACTIVE;
	  }
	| MENUBARBREAK
	  {
	    $$ = MENUITEM_MENUBARBREAK;
	  }
	| MENUBREAK
	  {
	    $$ = MENUITEM_MENUBREAK;
	  }
	;

/* Menuex resources.  */

menuex:
	  id MENUEX suboptions BEG menuexitems END
	  {
	    define_menu ($1, &$3, $5);
	    if (yychar != YYEMPTY)
	      YYERROR;
	    rcparse_discard_strings ();
	  }
	;

menuexitems:
	  /* empty */
	  {
	    $$ = NULL;
	  }
	| menuexitems menuexitem
	  {
	    if ($1 == NULL)
	      $$ = $2;
	    else
	      {
		rc_menuitem **pp;

		for (pp = &$1->next; *pp != NULL; pp = &(*pp)->next)
		  ;
		*pp = $2;
		$$ = $1;
	      }
	  }
	;

menuexitem:
	  MENUITEM res_unicode_string_concat
	  {
	    $$ = define_menuitem ($2, 0, 0, 0, 0, NULL);
	  }
	| MENUITEM res_unicode_string_concat cnumexpr
	  {
	    $$ = define_menuitem ($2, $3, 0, 0, 0, NULL);
	  }
	| MENUITEM res_unicode_string_concat cnumexpr cnumexpr optcnumexpr
	  {
	    $$ = define_menuitem ($2, $3, $4, $5, 0, NULL);
	  }
 	| MENUITEM SEPARATOR
 	  {
 	    $$ = define_menuitem (NULL, 0, 0, 0, 0, NULL);
 	  }
	| POPUP res_unicode_string_concat BEG menuexitems END
	  {
	    $$ = define_menuitem ($2, 0, 0, 0, 0, $4);
	  }
	| POPUP res_unicode_string_concat cnumexpr BEG menuexitems END
	  {
	    $$ = define_menuitem ($2, $3, 0, 0, 0, $5);
	  }
	| POPUP res_unicode_string_concat cnumexpr cnumexpr BEG menuexitems END
	  {
	    $$ = define_menuitem ($2, $3, $4, 0, 0, $6);
	  }
	| POPUP res_unicode_string_concat cnumexpr cnumexpr cnumexpr optcnumexpr
	    BEG menuexitems END
	  {
	    $$ = define_menuitem ($2, $3, $4, $5, $6, $8);
	  }
	;

/* Messagetable resources.  */

messagetable:
	  id MESSAGETABLE memflags_move file_name
	  {
	    define_messagetable ($1, &$3, $4);
	    if (yychar != YYEMPTY)
	      YYERROR;
	    rcparse_discard_strings ();
	  }
	;

/* We use a different lexing algorithm, because rcdata strings may
   contain embedded null bytes, and we need to know the length to use.  */

optrcdata_data:
	  {
	    rcparse_rcdata ();
	  }
	  optrcdata_data_int
	  {
	    rcparse_normal ();
	    $$ = $2;
	  }
	;

optrcdata_data_int:
	  /* empty */
	  {
	    $$.first = NULL;
	    $$.last = NULL;
	  }
	| rcdata_data
	  {
	    $$ = $1;
	  }
	;

rcdata_data:
	  sizedstring
	  {
	    rc_rcdata_item *ri;

	    ri = define_rcdata_string ($1.s, $1.length);
	    $$.first = ri;
	    $$.last = ri;
	  }
	| sizedunistring
	  {
	    rc_rcdata_item *ri;

	    ri = define_rcdata_unistring ($1.s, $1.length);
	    $$.first = ri;
	    $$.last = ri;
	  }
	| sizednumexpr
	  {
	    rc_rcdata_item *ri;

	    ri = define_rcdata_number ($1.val, $1.dword);
	    $$.first = ri;
	    $$.last = ri;
	  }
	| rcdata_data ',' sizedstring
	  {
	    rc_rcdata_item *ri;

	    ri = define_rcdata_string ($3.s, $3.length);
	    $$.first = $1.first;
	    $1.last->next = ri;
	    $$.last = ri;
	  }
	| rcdata_data ',' sizedunistring
	  {
	    rc_rcdata_item *ri;

	    ri = define_rcdata_unistring ($3.s, $3.length);
	    $$.first = $1.first;
	    $1.last->next = ri;
	    $$.last = ri;
	  }
	| rcdata_data ',' sizednumexpr
	  {
	    rc_rcdata_item *ri;

	    ri = define_rcdata_number ($3.val, $3.dword);
	    $$.first = $1.first;
	    $1.last->next = ri;
	    $$.last = ri;
	  }
	;

/* Stringtable resources.  */

stringtable:
	  STRINGTABLE suboptions BEG 
	    { sub_res_info = $2; }
	    string_data END
	;

string_data:
	  /* empty */
	| string_data numexpr res_unicode_string_concat
	  {
	    define_stringtable (&sub_res_info, $2, $3);
	    if (yychar != YYEMPTY)
	      YYERROR;
	    rcparse_discard_strings ();
	  }
	| string_data numexpr ',' res_unicode_string_concat
	  {
	    define_stringtable (&sub_res_info, $2, $4);
	    if (yychar != YYEMPTY)
	      YYERROR;
	    rcparse_discard_strings ();
	  }
	;

rcdata_id:
	id
	  {
	    $$=$1;
	  }
      | HTML
	{
	  $$.named = 0;
	  $$.u.id = 23;
	}
      | RCDATA
        {
          $$.named = 0;
          $$.u.id = RT_RCDATA;
        }
      | MANIFEST
        {
          $$.named = 0;
          $$.u.id = RT_MANIFEST;
        }
      | PLUGPLAY
        {
          $$.named = 0;
          $$.u.id = RT_PLUGPLAY;
        }
      | VXD
        {
          $$.named = 0;
          $$.u.id = RT_VXD;
        }
      | DLGINCLUDE
        {
          $$.named = 0;
          $$.u.id = RT_DLGINCLUDE;
        }
      | DLGINIT
        {
          $$.named = 0;
          $$.u.id = RT_DLGINIT;
        }
      | ANICURSOR
        {
          $$.named = 0;
          $$.u.id = RT_ANICURSOR;
        }
      | ANIICON
        {
          $$.named = 0;
          $$.u.id = RT_ANIICON;
        }
      ;

/* User defined resources.  We accept general suboptions in the
   file_name case to keep the parser happy.  */

user:
	  id rcdata_id suboptions BEG optrcdata_data END
	  {
	    define_user_data ($1, $2, &$3, $5.first);
	    if (yychar != YYEMPTY)
	      YYERROR;
	    rcparse_discard_strings ();
	  }
	| id rcdata_id suboptions file_name
	  {
	    define_user_file ($1, $2, &$3, $4);
	    if (yychar != YYEMPTY)
	      YYERROR;
	    rcparse_discard_strings ();
	  }
	;

toolbar:
	id TOOLBAR suboptions numexpr cnumexpr BEG toolbar_data END
	{
	  define_toolbar ($1, &$3, $4, $5, $7);
	}
	;

toolbar_data: /* empty */ { $$= NULL; }
	| toolbar_data BUTTON id
	{
	  rc_toolbar_item *c,*n;
	  c = $1;
	  n= (rc_toolbar_item *)
	      res_alloc (sizeof (rc_toolbar_item));
	  if (c != NULL)
	    while (c->next != NULL)
	      c = c->next;
	  n->prev = c;
	  n->next = NULL;
	  if (c != NULL)
	    c->next = n;
	  n->id = $3;
	  if ($1 == NULL)
	    $$ = n;
	  else
	    $$ = $1;
	}
	| toolbar_data SEPARATOR
	{
	  rc_toolbar_item *c,*n;
	  c = $1;
	  n= (rc_toolbar_item *)
	      res_alloc (sizeof (rc_toolbar_item));
	  if (c != NULL)
	    while (c->next != NULL)
	      c = c->next;
	  n->prev = c;
	  n->next = NULL;
	  if (c != NULL)
	    c->next = n;
	  n->id.named = 0;
	  n->id.u.id = 0;
	  if ($1 == NULL)
	    $$ = n;
	  else
	    $$ = $1;
	}
	;

/* Versioninfo resources.  */

versioninfo:
	  id VERSIONINFO fixedverinfo BEG verblocks END
	  {
	    define_versioninfo ($1, language, $3, $5);
	    if (yychar != YYEMPTY)
	      YYERROR;
	    rcparse_discard_strings ();
	  }
	;

fixedverinfo:
	  /* empty */
	  {
	    $$ = ((rc_fixed_versioninfo *)
		  res_alloc (sizeof (rc_fixed_versioninfo)));
	    memset ($$, 0, sizeof (rc_fixed_versioninfo));
	  }
	| fixedverinfo FILEVERSION numexpr cnumexpr cnumexpr cnumexpr
	  {
	    $1->file_version_ms = ($3 << 16) | $4;
	    $1->file_version_ls = ($5 << 16) | $6;
	    $$ = $1;
	  }
	| fixedverinfo PRODUCTVERSION numexpr cnumexpr cnumexpr cnumexpr
	  {
	    $1->product_version_ms = ($3 << 16) | $4;
	    $1->product_version_ls = ($5 << 16) | $6;
	    $$ = $1;
	  }
	| fixedverinfo FILEFLAGSMASK numexpr
	  {
	    $1->file_flags_mask = $3;
	    $$ = $1;
	  }
	| fixedverinfo FILEFLAGS numexpr
	  {
	    $1->file_flags = $3;
	    $$ = $1;
	  }
	| fixedverinfo FILEOS numexpr
	  {
	    $1->file_os = $3;
	    $$ = $1;
	  }
	| fixedverinfo FILETYPE numexpr
	  {
	    $1->file_type = $3;
	    $$ = $1;
	  }
	| fixedverinfo FILESUBTYPE numexpr
	  {
	    $1->file_subtype = $3;
	    $$ = $1;
	  }
	;

/* To handle verblocks successfully, the lexer handles BLOCK
   specially.  A BLOCK "StringFileInfo" is returned as
   BLOCKSTRINGFILEINFO.  A BLOCK "VarFileInfo" is returned as
   BLOCKVARFILEINFO.  A BLOCK with some other string returns BLOCK
   with the string as the value.  */

verblocks:
	  /* empty */
	  {
	    $$ = NULL;
	  }
	| verblocks BLOCKSTRINGFILEINFO BEG BLOCK BEG vervals END END
	  {
	    $$ = append_ver_stringfileinfo ($1, $4, $6);
	  }
	| verblocks BLOCKVARFILEINFO BEG VALUE res_unicode_string_concat vertrans END
	  {
	    $$ = append_ver_varfileinfo ($1, $5, $6);
	  }
	;

vervals:
	  /* empty */
	  {
	    $$ = NULL;
	  }
	| vervals VALUE res_unicode_string_concat ',' res_unicode_string_concat
	  {
	    $$ = append_verval ($1, $3, $5);
	  }
	;

vertrans:
	  /* empty */
	  {
	    $$ = NULL;
	  }
	| vertrans cnumexpr cnumexpr
	  {
	    $$ = append_vertrans ($1, $2, $3);
	  }
	;

/* A resource ID.  */

id:
	  posnumexpr
	  {
	    $$.named = 0;
	    $$.u.id = $1;
	  }
	| resname
	  {
	    res_unistring_to_id (&$$, $1);
	  }
	;

/* A resource reference.  */

resname:
	  res_unicode_string
	  {
	    $$ = $1;
	  }
	| STRING
	  {
	    unichar *h = NULL;
	    unicode_from_ascii ((rc_uint_type *) NULL, &h, $1);
	    $$ = h;
	  }
	;


resref:
	  posnumexpr ','
	  {
	    $$.named = 0;
	    $$.u.id = $1;
	  }
	| resname
	  {
	    res_unistring_to_id (&$$, $1);
	  }
	| resname ','
	  {
	    res_unistring_to_id (&$$, $1);
	  }
	;

/* Generic suboptions.  These may appear before the BEGIN in any
   multiline statement.  */

suboptions:
	  /* empty */
	  {
	    memset (&$$, 0, sizeof (rc_res_res_info));
	    $$.language = language;
	    /* FIXME: Is this the right default?  */
	    $$.memflags = MEMFLAG_MOVEABLE | MEMFLAG_PURE | MEMFLAG_DISCARDABLE;
	  }
	| suboptions memflag
	  {
	    $$ = $1;
	    $$.memflags |= $2.on;
	    $$.memflags &=~ $2.off;
	  }
	| suboptions CHARACTERISTICS numexpr
	  {
	    $$ = $1;
	    $$.characteristics = $3;
	  }
	| suboptions LANGUAGE numexpr cnumexpr
	  {
	    $$ = $1;
	    $$.language = $3 | ($4 << SUBLANG_SHIFT);
	  }
	| suboptions VERSIONK numexpr
	  {
	    $$ = $1;
	    $$.version = $3;
	  }
	;

/* Memory flags which default to MOVEABLE and DISCARDABLE.  */

memflags_move_discard:
	  /* empty */
	  {
	    memset (&$$, 0, sizeof (rc_res_res_info));
	    $$.language = language;
	    $$.memflags = MEMFLAG_MOVEABLE | MEMFLAG_DISCARDABLE;
	  }
	| memflags_move_discard memflag
	  {
	    $$ = $1;
	    $$.memflags |= $2.on;
	    $$.memflags &=~ $2.off;
	  }
	;

/* Memory flags which default to MOVEABLE.  */

memflags_move:
	  /* empty */
	  {
	    memset (&$$, 0, sizeof (rc_res_res_info));
	    $$.language = language;
	    $$.memflags = MEMFLAG_MOVEABLE | MEMFLAG_PURE | MEMFLAG_DISCARDABLE;
	  }
	| memflags_move memflag
	  {
	    $$ = $1;
	    $$.memflags |= $2.on;
	    $$.memflags &=~ $2.off;
	  }
	;

/* Memory flags.  This returns a struct with two integers, because we
   sometimes want to set bits and we sometimes want to clear them.  */

memflag:
	  MOVEABLE
	  {
	    $$.on = MEMFLAG_MOVEABLE;
	    $$.off = 0;
	  }
	| FIXED
	  {
	    $$.on = 0;
	    $$.off = MEMFLAG_MOVEABLE;
	  }
	| PURE
	  {
	    $$.on = MEMFLAG_PURE;
	    $$.off = 0;
	  }
	| IMPURE
	  {
	    $$.on = 0;
	    $$.off = MEMFLAG_PURE;
	  }
	| PRELOAD
	  {
	    $$.on = MEMFLAG_PRELOAD;
	    $$.off = 0;
	  }
	| LOADONCALL
	  {
	    $$.on = 0;
	    $$.off = MEMFLAG_PRELOAD;
	  }
	| DISCARDABLE
	  {
	    $$.on = MEMFLAG_DISCARDABLE;
	    $$.off = 0;
	  }
	;

/* A file name.  */

file_name:
	  QUOTEDSTRING
	  {
	    $$ = $1;
	  }
	| STRING
	  {
	    $$ = $1;
	  }
	;

/* Concat string */
res_unicode_string_concat:
	  res_unicode_string
	  {
	    $$ = $1;
	  }
	|
	  res_unicode_string_concat res_unicode_string
	  {
	    rc_uint_type l1 = unichar_len ($1);
	    rc_uint_type l2 = unichar_len ($2);
	    unichar *h = (unichar *) res_alloc ((l1 + l2 + 1) * sizeof (unichar));
	    if (l1 != 0)
	      memcpy (h, $1, l1 * sizeof (unichar));
	    if (l2 != 0)
	      memcpy (h + l1, $2, l2  * sizeof (unichar));
	    h[l1 + l2] = 0;
	    $$ = h;
	  }
	;

res_unicode_string:
	  QUOTEDUNISTRING
	  {
	    $$ = unichar_dup ($1);
	  }
	| QUOTEDSTRING
	  {
	    unichar *h = NULL;
	    unicode_from_ascii ((rc_uint_type *) NULL, &h, $1);
	    $$ = h;
	  }
	;

sizedstring:
	  SIZEDSTRING
	  {
	    $$ = $1;
	  }
	| sizedstring SIZEDSTRING
	  {
	    rc_uint_type l = $1.length + $2.length;
	    char *h = (char *) res_alloc (l);
	    memcpy (h, $1.s, $1.length);
	    memcpy (h + $1.length, $2.s, $2.length);
	    $$.s = h;
	    $$.length = l;
	  }
	;

sizedunistring:
	  SIZEDUNISTRING
	  {
	    $$ = $1;
	  }
	| sizedunistring SIZEDUNISTRING
	  {
	    rc_uint_type l = $1.length + $2.length;
	    unichar *h = (unichar *) res_alloc (l * sizeof (unichar));
	    memcpy (h, $1.s, $1.length * sizeof (unichar));
	    memcpy (h + $1.length, $2.s, $2.length  * sizeof (unichar));
	    $$.s = h;
	    $$.length = l;
	  }
	;

/* A style expression.  This changes the static variable STYLE.  We do
   it this way because rc appears to permit a style to be set to
   something like
       WS_GROUP | NOT WS_TABSTOP
   to mean that a default of WS_TABSTOP should be removed.  Anything
   which wants to accept a style must first set STYLE to the default
   value.  The styleexpr nonterminal will change STYLE as specified by
   the user.  Note that we do not accept arbitrary expressions here,
   just numbers separated by '|'.  */

styleexpr:
	  parennumber
	  {
	    style |= $1;
	  }
	| NOT parennumber
	  {
	    style &=~ $2;
	  }
	| styleexpr '|' parennumber
	  {
	    style |= $3;
	  }
	| styleexpr '|' NOT parennumber
	  {
	    style &=~ $4;
	  }
	;

parennumber:
	  NUMBER
	  {
	    $$ = $1.val;
	  }
	| '(' numexpr ')'
	  {
	    $$ = $2;
	  }
	;

/* An optional expression with a leading comma.  */

optcnumexpr:
	  /* empty */
	  {
	    $$ = 0;
	  }
	| cnumexpr
	  {
	    $$ = $1;
	  }
	;

/* An expression with a leading comma.  */

cnumexpr:
	  ',' numexpr
	  {
	    $$ = $2;
	  }
	;

/* A possibly negated numeric expression.  */

numexpr:
	  sizednumexpr
	  {
	    $$ = $1.val;
	  }
	;

/* A possibly negated expression with a size.  */

sizednumexpr:
	  NUMBER
	  {
	    $$ = $1;
	  }
	| '(' sizednumexpr ')'
	  {
	    $$ = $2;
	  }
	| '~' sizednumexpr %prec '~'
	  {
	    $$.val = ~ $2.val;
	    $$.dword = $2.dword;
	  }
	| '-' sizednumexpr %prec NEG
	  {
	    $$.val = - $2.val;
	    $$.dword = $2.dword;
	  }
	| sizednumexpr '*' sizednumexpr
	  {
	    $$.val = $1.val * $3.val;
	    $$.dword = $1.dword || $3.dword;
	  }
	| sizednumexpr '/' sizednumexpr
	  {
	    $$.val = $1.val / $3.val;
	    $$.dword = $1.dword || $3.dword;
	  }
	| sizednumexpr '%' sizednumexpr
	  {
	    $$.val = $1.val % $3.val;
	    $$.dword = $1.dword || $3.dword;
	  }
	| sizednumexpr '+' sizednumexpr
	  {
	    $$.val = $1.val + $3.val;
	    $$.dword = $1.dword || $3.dword;
	  }
	| sizednumexpr '-' sizednumexpr
	  {
	    $$.val = $1.val - $3.val;
	    $$.dword = $1.dword || $3.dword;
	  }
	| sizednumexpr '&' sizednumexpr
	  {
	    $$.val = $1.val & $3.val;
	    $$.dword = $1.dword || $3.dword;
	  }
	| sizednumexpr '^' sizednumexpr
	  {
	    $$.val = $1.val ^ $3.val;
	    $$.dword = $1.dword || $3.dword;
	  }
	| sizednumexpr '|' sizednumexpr
	  {
	    $$.val = $1.val | $3.val;
	    $$.dword = $1.dword || $3.dword;
	  }
	;

/* An expression with a leading comma which does not use unary
   negation.  */

cposnumexpr:
	  ',' posnumexpr
	  {
	    $$ = $2;
	  }
	;

/* An expression which does not use unary negation.  */

posnumexpr:
	  sizedposnumexpr
	  {
	    $$ = $1.val;
	  }
	;

/* An expression which does not use unary negation.  We separate unary
   negation to avoid parsing conflicts when two numeric expressions
   appear consecutively.  */

sizedposnumexpr:
	  NUMBER
	  {
	    $$ = $1;
	  }
	| '(' sizednumexpr ')'
	  {
	    $$ = $2;
	  }
	| '~' sizednumexpr %prec '~'
	  {
	    $$.val = ~ $2.val;
	    $$.dword = $2.dword;
	  }
	| sizedposnumexpr '*' sizednumexpr
	  {
	    $$.val = $1.val * $3.val;
	    $$.dword = $1.dword || $3.dword;
	  }
	| sizedposnumexpr '/' sizednumexpr
	  {
	    $$.val = $1.val / $3.val;
	    $$.dword = $1.dword || $3.dword;
	  }
	| sizedposnumexpr '%' sizednumexpr
	  {
	    $$.val = $1.val % $3.val;
	    $$.dword = $1.dword || $3.dword;
	  }
	| sizedposnumexpr '+' sizednumexpr
	  {
	    $$.val = $1.val + $3.val;
	    $$.dword = $1.dword || $3.dword;
	  }
	| sizedposnumexpr '-' sizednumexpr
	  {
	    $$.val = $1.val - $3.val;
	    $$.dword = $1.dword || $3.dword;
	  }
	| sizedposnumexpr '&' sizednumexpr
	  {
	    $$.val = $1.val & $3.val;
	    $$.dword = $1.dword || $3.dword;
	  }
	| sizedposnumexpr '^' sizednumexpr
	  {
	    $$.val = $1.val ^ $3.val;
	    $$.dword = $1.dword || $3.dword;
	  }
	| sizedposnumexpr '|' sizednumexpr
	  {
	    $$.val = $1.val | $3.val;
	    $$.dword = $1.dword || $3.dword;
	  }
	;

%%

/* Set the language from the command line.  */

void
rcparse_set_language (int lang)
{
  language = lang;
}
