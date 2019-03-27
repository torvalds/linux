%{ /* mcparse.y -- parser for Windows mc files
  Copyright 2007
  Free Software Foundation, Inc.
  
  Parser for Windows mc files
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

/* This is a parser for Windows rc files.  It is based on the parser
   by Gunther Ebert <gunther.ebert@ixos-leipzig.de>.  */

#include "sysdep.h"
#include "bfd.h"
#include "bucomm.h"
#include "libiberty.h"
#include "windmc.h"
#include "safe-ctype.h"

static rc_uint_type mc_last_id = 0;
static rc_uint_type mc_sefa_val = 0;
static unichar *mc_last_symbol = NULL;
static const mc_keyword *mc_cur_severity = NULL;
static const mc_keyword *mc_cur_facility = NULL;
static mc_node *cur_node = NULL;

%}

%union
{
  rc_uint_type ival;
  unichar *ustr;
  const mc_keyword *tok;
  mc_node *nod;
};

%start input

%token NL
%token<ustr> MCIDENT MCFILENAME MCLINE MCCOMMENT
%token<tok> MCTOKEN
%token MCENDLINE
%token MCLANGUAGENAMES MCFACILITYNAMES MCSEVERITYNAMES MCOUTPUTBASE MCMESSAGEIDTYPEDEF
%token MCLANGUAGE MCMESSAGEID MCSEVERITY MCFACILITY MCSYMBOLICNAME
%token <ival> MCNUMBER

%type<ival> id vid sefasy_def
%type<ustr> alias_name token lines comments
%type<tok> lang

%%
input:	  entities
	;

entities:
	  /* empty */
	| entities entity
	;
entity:	  global_section
	| message
	| comments
	  {
	    cur_node = mc_add_node ();
	    cur_node->user_text = $1;
	  }
	| error	{ mc_fatal ("syntax error"); }
;

global_section:
	  MCSEVERITYNAMES '=' '(' severitymaps ')'
	| MCSEVERITYNAMES '=' '(' severitymaps error { mc_fatal ("missing ')' in SeverityNames"); }
	| MCSEVERITYNAMES '=' error { mc_fatal ("missing '(' in SeverityNames"); }
	| MCSEVERITYNAMES error { mc_fatal ("missing '=' for SeverityNames"); }
	| MCLANGUAGENAMES '=' '(' langmaps ')'
	| MCLANGUAGENAMES '=' '(' langmaps error { mc_fatal ("missing ')' in LanguageNames"); }
	| MCLANGUAGENAMES '=' error { mc_fatal ("missing '(' in LanguageNames"); }
	| MCLANGUAGENAMES error { mc_fatal ("missing '=' for LanguageNames"); }
	| MCFACILITYNAMES '=' '(' facilitymaps ')'
	| MCFACILITYNAMES '=' '(' facilitymaps error { mc_fatal ("missing ')' in FacilityNames"); }
	| MCFACILITYNAMES '=' error { mc_fatal ("missing '(' in FacilityNames"); }
	| MCFACILITYNAMES error { mc_fatal ("missing '=' for FacilityNames"); }
	| MCOUTPUTBASE '=' MCNUMBER
	  {
	    if ($3 != 10 && $3 != 16)
	      mc_fatal ("OutputBase allows 10 or 16 as value");
	    mcset_out_values_are_decimal = ($3 == 10 ? 1 : 0);
	  }
	| MCMESSAGEIDTYPEDEF '=' MCIDENT
	  {
	    mcset_msg_id_typedef = $3;
	  }
	| MCMESSAGEIDTYPEDEF '=' error
	  {
	    mc_fatal ("MessageIdTypedef expects an identifier");
	  }
	| MCMESSAGEIDTYPEDEF error
	  {
	    mc_fatal ("missing '=' for MessageIdTypedef");
	  }
;

severitymaps:
	  severitymap
	| severitymaps severitymap
	| error { mc_fatal ("severity ident missing"); }
;

severitymap:
	  token '=' MCNUMBER alias_name
	  {
	    mc_add_keyword ($1, MCTOKEN, "severity", $3, $4);
	  }
	| token '=' error { mc_fatal ("severity number missing"); }
	| token error { mc_fatal ("severity missing '='"); }
;

facilitymaps:
	  facilitymap
	| facilitymaps facilitymap
	| error { mc_fatal ("missing ident in FacilityNames"); }
;

facilitymap:
	  token '=' MCNUMBER alias_name
	  {
	    mc_add_keyword ($1, MCTOKEN, "facility", $3, $4);
	  }
	| token '=' error { mc_fatal ("facility number missing"); }
	| token error { mc_fatal ("facility missing '='"); }
;

langmaps:
	  langmap
	| langmaps langmap
	| error { mc_fatal ("missing ident in LanguageNames"); }
;

langmap:
	  token '=' MCNUMBER lex_want_filename ':' MCFILENAME
	  {
	    mc_add_keyword ($1, MCTOKEN, "language", $3, $6);
	  }
	| token '=' MCNUMBER lex_want_filename ':' error { mc_fatal ("missing filename in LanguageNames"); }
	| token '=' MCNUMBER error { mc_fatal ("missing ':' in LanguageNames"); }
	| token '=' error { mc_fatal ("missing language code in LanguageNames"); }
	| token error { mc_fatal ("missing '=' for LanguageNames"); }
;

alias_name:
	  /* empty */
	  {
	    $$ = NULL;
	  }
	| ':' MCIDENT
	  {
	    $$ = $2;
	  }
	| ':' error { mc_fatal ("illegal token in identifier"); $$ = NULL; }
;

message:
	  id sefasy_def
	  {
	    cur_node = mc_add_node ();
	    cur_node->symbol = mc_last_symbol;
	    cur_node->facility = mc_cur_facility;
	    cur_node->severity = mc_cur_severity;
	    cur_node->id = ($1 & 0xffffUL);
	    cur_node->vid = ($1 & 0xffffUL) | mc_sefa_val;
	    mc_last_id = $1;
	  }
	  lang_entities
;

id:	  MCMESSAGEID '=' vid { $$ = $3; }
	| MCMESSAGEID '=' error { mc_fatal ("missing number in MessageId"); $$ = 0; }
	| MCMESSAGEID error { mc_fatal ("missing '=' for MessageId"); $$ = 0; }
;

vid:	  /* empty */
	  {
	    $$ = ++mc_last_id;
	  }
	| MCNUMBER
	  {
	    $$ = $1;
	  }
	| '+' MCNUMBER
	  {
	    $$ = mc_last_id + $2;
	  }
	| '+' error { mc_fatal ("missing number after MessageId '+'"); }
;

sefasy_def:
	  /* empty */
	  {
	    $$ = 0;
	    mc_sefa_val = (mcset_custom_bit ? 1 : 0) << 29;
	    mc_last_symbol = NULL;
	    mc_cur_severity = NULL;
	    mc_cur_facility = NULL;
	  }
	| sefasy_def severity
	  {
	    if ($1 & 1)
	      mc_warn (_("duplicate definition of Severity"));
	    $$ = $1 | 1;
	  }
	| sefasy_def facility
	  {
	    if ($1 & 2)
	      mc_warn (_("duplicate definition of Facility"));
	    $$ = $1 | 2;
	  }
	| sefasy_def symbol
	  {
	    if ($1 & 4)
	      mc_warn (_("duplicate definition of SymbolicName"));
	    $$ = $1 | 4;
	  }
;

severity: MCSEVERITY '=' MCTOKEN
	  {
	    mc_sefa_val &= ~ (0x3UL << 30);
	    mc_sefa_val |= (($3->nval & 0x3UL) << 30);
	    mc_cur_severity = $3;
	  }
;

facility: MCFACILITY '=' MCTOKEN
	  {
	    mc_sefa_val &= ~ (0xfffUL << 16);
	    mc_sefa_val |= (($3->nval & 0xfffUL) << 16);
	    mc_cur_facility = $3;
	  }
;

symbol: MCSYMBOLICNAME '=' MCIDENT
	{
	  mc_last_symbol = $3;
	}
;

lang_entities:
	  lang_entity
	| lang_entities lang_entity
;

lang_entity:
	  lang lex_want_line lines MCENDLINE
	  {
	    mc_node_lang *h;
	    h = mc_add_node_lang (cur_node, $1, cur_node->vid);
	    h->message = $3;
	    if (mcset_max_message_length != 0 && unichar_len (h->message) > mcset_max_message_length)
	      mc_warn ("message length to long");
	  }
;

lines:	  MCLINE
	  {
	    $$ = $1;
	  }
	| lines MCLINE
	  {
	    unichar *h;
	    rc_uint_type l1,l2;
	    l1 = unichar_len ($1);
	    l2 = unichar_len ($2);
	    h = (unichar *) res_alloc ((l1 + l2 + 1) * sizeof (unichar));
	    if (l1) memcpy (h, $1, l1 * sizeof (unichar));
	    if (l2) memcpy (&h[l1], $2, l2 * sizeof (unichar));
	    h[l1 + l2] = 0;
	    $$ = h;
	  }
	| error { mc_fatal ("missing end of message text"); $$ = NULL; }
	| lines error { mc_fatal ("missing end of message text"); $$ = $1; }
;

comments: MCCOMMENT { $$ = $1; }
	| comments MCCOMMENT
	  {
	    unichar *h;
	    rc_uint_type l1,l2;
	    l1 = unichar_len ($1);
	    l2 = unichar_len ($2);
	    h = (unichar *) res_alloc ((l1 + l2 + 1) * sizeof (unichar));
	    if (l1) memcpy (h, $1, l1 * sizeof (unichar));
	    if (l2) memcpy (&h[l1], $2, l2 * sizeof (unichar));
	    h[l1 + l2] = 0;
	    $$ = h;
	  }
;

lang:	  MCLANGUAGE lex_want_nl '=' MCTOKEN NL
	  {
	    $$ = $4;
	  }
	| MCLANGUAGE lex_want_nl '=' MCIDENT NL
	  {
	    $$ = NULL;
	    mc_fatal (_("undeclared language identifier"));
	  }
	| MCLANGUAGE lex_want_nl '=' token error
	  {
	    $$ = NULL;
	    mc_fatal ("missing newline after Language");
	  }
	| MCLANGUAGE lex_want_nl '=' error
	  {
	    $$ = NULL;
	    mc_fatal ("missing ident for Language");
	  }
	| MCLANGUAGE error
	  {
	    $$ = NULL;
	    mc_fatal ("missing '=' for Language");
	  }
;

token: 	MCIDENT { $$ = $1; }
	|  MCTOKEN { $$ = $1->usz; }
;

lex_want_nl:
	  /* Empty */	{ mclex_want_nl = 1; }
;

lex_want_line:
	  /* Empty */	{ mclex_want_line = 1; }
;

lex_want_filename:
	  /* Empty */	{ mclex_want_filename = 1; }
;

%%

/* Something else.  */
