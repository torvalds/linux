%{ /* defparse.y - parser for .def files */

/*  Copyright 1995, 1997, 1998, 1999, 2001, 2004, 2007
    Free Software Foundation, Inc.

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
    Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "sysdep.h"
#include "bfd.h"
#include "libiberty.h"
#include "dlltool.h"
%}

%union {
  char *id;
  int number;
};

%token NAME LIBRARY DESCRIPTION STACKSIZE HEAPSIZE CODE DATA
%token SECTIONS EXPORTS IMPORTS VERSIONK BASE CONSTANT
%token READ WRITE EXECUTE SHARED NONSHARED NONAME PRIVATE
%token SINGLE MULTIPLE INITINSTANCE INITGLOBAL TERMINSTANCE TERMGLOBAL
%token <id> ID
%token <number> NUMBER
%type  <number> opt_base opt_ordinal opt_NONAME opt_CONSTANT opt_DATA opt_PRIVATE
%type  <number> attr attr_list opt_number
%type  <id> opt_name opt_equal_name 

%%

start: start command
	| command
	;

command: 
		NAME opt_name opt_base { def_name ($2, $3); }
	|	LIBRARY opt_name opt_base option_list { def_library ($2, $3); }
	|	EXPORTS explist 
	|	DESCRIPTION ID { def_description ($2);}
	|	STACKSIZE NUMBER opt_number { def_stacksize ($2, $3);}
	|	HEAPSIZE NUMBER opt_number { def_heapsize ($2, $3);}
	|	CODE attr_list { def_code ($2);}
	|	DATA attr_list  { def_data ($2);}
	|	SECTIONS seclist
	|	IMPORTS implist
	|	VERSIONK NUMBER { def_version ($2,0);}
	|	VERSIONK NUMBER '.' NUMBER { def_version ($2,$4);}
	;


explist:
		/* EMPTY */
	|	explist expline
	;

expline:
		ID opt_equal_name opt_ordinal opt_NONAME opt_CONSTANT opt_DATA opt_PRIVATE
			{ def_exports ($1, $2, $3, $4, $5, $6, $7);}
	;
implist:	
		implist impline
	|	impline
	;

impline:
               ID '=' ID '.' ID '.' ID     { def_import ($1,$3,$5,$7, 0); }
       |       ID '=' ID '.' ID '.' NUMBER { def_import ($1,$3,$5, 0,$7); }
       |       ID '=' ID '.' ID            { def_import ($1,$3, 0,$5, 0); }
       |       ID '=' ID '.' NUMBER        { def_import ($1,$3, 0, 0,$5); }
       |       ID '.' ID '.' ID            { def_import ( 0,$1,$3,$5, 0); }
       |       ID '.' ID '.' NUMBER        { def_import ( 0,$1,$3, 0,$5); }
       |       ID '.' ID                   { def_import ( 0,$1, 0,$3, 0); }
       |       ID '.' NUMBER               { def_import ( 0,$1, 0, 0,$3); }
;

seclist:
		seclist secline
	|	secline
	;

secline:
	ID attr_list { def_section ($1,$2);}
	;

attr_list:
	attr_list opt_comma attr
	| attr
	;

opt_comma:
	','
	| 
	;
opt_number: ',' NUMBER { $$=$2;}
	|	   { $$=-1;}
	;
	
attr:
		READ { $$ = 1; }
	|	WRITE { $$ = 2; }
	|	EXECUTE { $$ = 4; }
	|	SHARED { $$ = 8; }
	|	NONSHARED { $$ = 0; }
	|	SINGLE { $$ = 0; }
	|	MULTIPLE { $$ = 0; }
	;

opt_CONSTANT:
		CONSTANT {$$=1;}
	|		 {$$=0;}
	;

opt_NONAME:
		NONAME {$$=1;}
	|		 {$$=0;}
	;

opt_DATA:
		DATA { $$ = 1; }
	|	     { $$ = 0; }
	;

opt_PRIVATE:
		PRIVATE { $$ = 1; }
	|		{ $$ = 0; }
	;

opt_name: ID		{ $$ =$1; }
	| ID '.' ID	
	  { 
	    char *name = xmalloc (strlen ($1) + 1 + strlen ($3) + 1);
	    sprintf (name, "%s.%s", $1, $3);
	    $$ = name;
	  }
	|		{ $$=""; }
	;

opt_ordinal: 
	  '@' NUMBER     { $$=$2;}
	|                { $$=-1;}
	;

opt_equal_name:
          '=' ID	{ $$ = $2; }
	| '=' ID '.' ID	
	  { 
	    char *name = xmalloc (strlen ($2) + 1 + strlen ($4) + 1);
	    sprintf (name, "%s.%s", $2, $4);
	    $$ = name;
	  }
        | 		{ $$ =  0; }			 
	;

opt_base: BASE	'=' NUMBER	{ $$= $3;}
	|	{ $$=-1;}
	;

option_list:
		/* empty */
	|	option_list opt_comma option
	;

option:
		INITINSTANCE
	|	INITGLOBAL
	|	TERMINSTANCE
	|	TERMGLOBAL
	;
