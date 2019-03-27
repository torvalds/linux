/* FLEX lexer for Ada expressions, for GDB.
   Copyright (C) 1994, 1997, 2000
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
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

/*----------------------------------------------------------------------*/

/* The converted version of this file is to be included in ada-exp.y, */
/* the Ada parser for gdb.  The function yylex obtains characters from */
/* the global pointer lexptr.  It returns a syntactic category for */
/* each successive token and places a semantic value into yylval */
/* (ada-lval), defined by the parser.   */

/* Run flex with (at least) the -i option (case-insensitive), and the -I */
/* option (interactive---no unnecessary lookahead).  */

DIG	[0-9]
NUM10	({DIG}({DIG}|_)*)
HEXDIG	[0-9a-f]
NUM16	({HEXDIG}({HEXDIG}|_)*)
OCTDIG	[0-7]
LETTER	[a-z_]
ID	({LETTER}({LETTER}|{DIG})*|"<"{LETTER}({LETTER}|{DIG})*">")
WHITE	[ \t\n]
TICK	("'"{WHITE}*)
GRAPHIC [a-z0-9 #&'()*+,-./:;<>=_|!$%?@\[\]\\^`{}~]
OPER    ([-+*/=<>&]|"<="|">="|"**"|"/="|"and"|"or"|"xor"|"not"|"mod"|"rem"|"abs")

EXP	(e[+-]{NUM10})
POSEXP  (e"+"?{NUM10})

%{
#define NUMERAL_WIDTH 256
#define LONGEST_SIGN ((ULONGEST) 1 << (sizeof(LONGEST) * HOST_CHAR_BIT - 1))

/* Temporary staging for numeric literals. */
static char numbuf[NUMERAL_WIDTH]; 
 static void canonicalizeNumeral (char* s1, const char*);
static int processInt (const char*, const char*, const char*);
static int processReal (const char*);
static int processId (const char*, int);
static int processAttribute (const char*);
static int find_dot_all (const char*);

#undef YY_DECL
#define YY_DECL static int yylex ( void ) 

#undef YY_INPUT
#define YY_INPUT(BUF, RESULT, MAX_SIZE) \
    if ( *lexptr == '\000' ) \
      (RESULT) = YY_NULL; \
    else \
      { \
        *(BUF) = *lexptr; \
        (RESULT) = 1; \
	lexptr += 1; \
      }

static char *tempbuf = NULL;
static int tempbufsize = 0;
static int tempbuf_len;
static struct block* left_block_context;

static void resize_tempbuf (unsigned int);

static void block_lookup (char*, char*);

static int name_lookup (char*, char*, int*);

static int find_dot_all (const char*);

%}

%s IN_STRING BEFORE_QUAL_QUOTE

%%

{WHITE}		 { }

"--".*		 { yyterminate(); }

{NUM10}{POSEXP}  { 
		   canonicalizeNumeral (numbuf, yytext); 
		   return processInt (NULL, numbuf, strrchr(numbuf, 'e')+1);
		 }

{NUM10}          { 
		   canonicalizeNumeral (numbuf, yytext); 
		   return processInt (NULL, numbuf, NULL);
		 }

{NUM10}"#"{HEXDIG}({HEXDIG}|_)*"#"{POSEXP} {
		   canonicalizeNumeral (numbuf, yytext);
    		   return processInt (numbuf,
				      strchr (numbuf, '#') + 1, 
				      strrchr(numbuf, '#') + 1);
		 }

{NUM10}"#"{HEXDIG}({HEXDIG}|_)*"#" {
		   canonicalizeNumeral (numbuf, yytext);
    		   return processInt (numbuf, strchr (numbuf, '#') + 1, NULL);
		 }

"0x"{HEXDIG}+	{
		  canonicalizeNumeral (numbuf, yytext+2);
		  return processInt ("16#", numbuf, NULL);
		}


{NUM10}"."{NUM10}{EXP} {
		   canonicalizeNumeral (numbuf, yytext); 
		   return processReal (numbuf);
		}

{NUM10}"."{NUM10} {
		   canonicalizeNumeral (numbuf, yytext); 
		   return processReal (numbuf);
		}

{NUM10}"#"{NUM16}"."{NUM16}"#"{EXP} {
                   error ("Based real literals not implemented yet.");
		}

{NUM10}"#"{NUM16}"."{NUM16}"#" {
                   error ("Based real literals not implemented yet.");
		}

<INITIAL>"'"({GRAPHIC}|\")"'" {
		   yylval.typed_val.type = builtin_type_ada_char;
		   yylval.typed_val.val = yytext[1];
		   return CHARLIT;
		}

<INITIAL>"'[\""{HEXDIG}{2}"\"]'"   {
                   int v;
                   yylval.typed_val.type = builtin_type_ada_char;
		   sscanf (yytext+3, "%2x", &v);
		   yylval.typed_val.val = v;
		   return CHARLIT;
		}

\"{OPER}\"/{WHITE}*"(" { return processId (yytext, yyleng); }

<INITIAL>\"	{ 
		   tempbuf_len = 0;
		   BEGIN IN_STRING;
		}

<IN_STRING>{GRAPHIC}*\"  {
		   resize_tempbuf (yyleng+tempbuf_len);
		   strncpy (tempbuf+tempbuf_len, yytext, yyleng-1);
		   tempbuf_len += yyleng-1;
		   yylval.sval.ptr = tempbuf;
		   yylval.sval.length = tempbuf_len;
		   BEGIN INITIAL;
		   return STRING;
		}

<IN_STRING>{GRAPHIC}*"[\""{HEXDIG}{2}"\"]" {
		   int n;
		   resize_tempbuf (yyleng-5+tempbuf_len+1);
		   strncpy (tempbuf+tempbuf_len, yytext, yyleng-6);
		   sscanf(yytext+yyleng-4, "%2x", &n);
		   tempbuf[yyleng-6+tempbuf_len] = (char) n;
		   tempbuf_len += yyleng-5;
		}

<IN_STRING>{GRAPHIC}*"[\"\"\"]" {
		   int n;
		   resize_tempbuf (yyleng-4+tempbuf_len+1);
		   strncpy (tempbuf+tempbuf_len, yytext, yyleng-6);
		   tempbuf[yyleng-5+tempbuf_len] = '"';
		   tempbuf_len += yyleng-4;
		}

if		{ 
		  while (*lexptr != 'i' && *lexptr != 'I') 
		    lexptr -= 1; 
		  yyrestart(NULL); 
		  return 0;
		}

	/* ADA KEYWORDS */

abs		{ return ABS; }
and		{ return _AND_; }
else		{ return ELSE; }
in		{ return IN; }
mod		{ return MOD; }
new		{ return NEW; }
not		{ return NOT; }
null		{ return NULL_PTR; }
or		{ return OR; }
rem		{ return REM; }
then		{ return THEN; }
xor		{ return XOR; }

        /* ATTRIBUTES */

{TICK}[a-zA-Z][a-zA-Z]+ { return processAttribute (yytext+1); }

	/* PUNCTUATION */

"=>"		{ return ARROW; }
".."		{ return DOTDOT; }
"**"		{ return STARSTAR; }
":="		{ return ASSIGN; }
"/="		{ return NOTEQUAL; }
"<="		{ return LEQ; }
">="		{ return GEQ; }

<BEFORE_QUAL_QUOTE>"'" { BEGIN INITIAL; return '\''; }

[-&*+./:<>=|;\[\]] { return yytext[0]; }

","		{ if (paren_depth == 0 && comma_terminates)
		    {
		      lexptr -= 1;
		      yyrestart(NULL);
		      return 0;
		    }
		  else 
		    return ',';
		}

"("		{ paren_depth += 1; return '('; }
")"		{ if (paren_depth == 0) 
		    {
		      lexptr -= 1;
		      yyrestart(NULL);
		      return 0;
		    }
		  else 
 		    {
		      paren_depth -= 1; 
		      return ')';
		    }
		}

"."{WHITE}*all  { return DOT_ALL; }

"."{WHITE}*{ID} { 
	 	  processId (yytext+1, yyleng-1);
	          return DOT_ID; 
		}

{ID}({WHITE}*"."{WHITE}*({ID}|\"{OPER}\"))*(" "*"'")?  { 
                  int all_posn = find_dot_all (yytext);
		  int token_type, segments, k;
		  int quote_follows;

                  if (all_posn == -1 && yytext[yyleng-1] == '\'') 
		    {
		      quote_follows = 1;
		      do { 
			yyless (yyleng-1); 
		      } while (yytext[yyleng-1] == ' ');
		    }
		  else
		    quote_follows = 0;			
		    
                  if (all_posn >= 0)
		    yyless (all_posn);
                  processId(yytext, yyleng);
                  segments = name_lookup (ada_mangle (yylval.ssym.stoken.ptr),
		                          yylval.ssym.stoken.ptr, &token_type);
		  left_block_context = NULL;
		  for (k = yyleng; segments > 0 && k > 0; k -= 1)
                    {
		      if (yytext[k-1] == '.')
			segments -= 1;
		      quote_follows = 0;
		    }
		  if (k <= 0)
		    error ("confused by name %s", yytext);
		  yyless (k);
		  if (quote_follows) 
		    BEGIN BEFORE_QUAL_QUOTE;
		  return token_type;
                }

	/* GDB EXPRESSION CONSTRUCTS  */


"'"[^']+"'"{WHITE}*:: {
                  processId(yytext, yyleng-2);
                  block_lookup (yylval.ssym.stoken.ptr, yylval.ssym.stoken.ptr);
                  return BLOCKNAME;
		}

{ID}({WHITE}*"."{WHITE}*({ID}|\"{OPER}\"))*{WHITE}*::  { 
                  processId(yytext, yyleng-2);
                  block_lookup (ada_mangle (yylval.ssym.stoken.ptr),
                                yylval.ssym.stoken.ptr);
                  return BLOCKNAME;
		}

[{}@]		{ return yytext[0]; }

"$$"		{ yylval.lval = -1; return LAST; }
"$$"{DIG}+	{ yylval.lval = -atoi(yytext+2); return LAST; }
"$"		{ yylval.lval = 0; return LAST; }
"$"{DIG}+	{ yylval.lval = atoi(yytext+1); return LAST; }


	/* REGISTERS AND GDB CONVENIENCE VARIABLES */

"$"({LETTER}|{DIG}|"$")+  {
    		  int c;
		  for (c = 0; c < NUM_REGS; c++)
		    if (REGISTER_NAME (c) &&
                        strcmp (yytext + 1, REGISTER_NAME (c)) == 0)
		      {
			yylval.lval = c;
			return REGNAME;
		      }
		  yylval.sval.ptr = yytext;
		  yylval.sval.length = yyleng;
		  yylval.ivar = 
		    lookup_internalvar (copy_name (yylval.sval) + 1);
		  return INTERNAL_VARIABLE;
		}

	/* CATCH-ALL ERROR CASE */

.		{ error ("Invalid character '%s' in expression.", yytext); }
%%

#include <ctype.h>
#include <string.h>

/* Initialize the lexer for processing new expression */
void
lexer_init (FILE* inp)
{
  BEGIN INITIAL;
  yyrestart (inp);
}


/* Make sure that tempbuf points at an array at least N characters long. */

static void
resize_tempbuf (n)
     unsigned int n;
{
  if (tempbufsize < n)
    {
      tempbufsize = (n+63) & ~63;
      tempbuf = (char*) xrealloc (tempbuf, tempbufsize);
    }
}
 
/* Copy S2 to S1, removing all underscores, and downcasing all letters. */

static void
canonicalizeNumeral (s1,s2)
     char* s1;
     const char* s2;
{
  for (; *s2 != '\000'; s2 += 1) 
    {
      if (*s2 != '_')
	{
	  *s1 = tolower(*s2);
	  s1 += 1;
	}
    }
  s1[0] = '\000';
}

#define HIGH_BYTE_POSN ((sizeof (ULONGEST) - 1) * HOST_CHAR_BIT)

/* True (non-zero) iff DIGIT is a valid digit in radix BASE, 
   where 2 <= BASE <= 16.  */

static int
is_digit_in_base (digit, base)
     unsigned char digit;
     int base;
{
  if (!isxdigit (digit))
    return 0;
  if (base <= 10)
    return (isdigit (digit) && digit < base + '0');
  else 
    return (isdigit (digit) || tolower (digit) < base - 10 + 'a');
}

static int
digit_to_int (c)
     unsigned char c;
{
  if (isdigit (c))
    return c - '0';
  else
    return tolower (c) - 'a' + 10;
}

/* As for strtoul, but for ULONGEST results. */
ULONGEST
strtoulst (num, trailer, base)
     const char *num;
     const char **trailer;
     int base;
{
  unsigned int high_part;
  ULONGEST result;
  int i;
  unsigned char lim;

  if (base < 2 || base > 16)
    {
      errno = EINVAL;
      return 0;
    }
  lim = base - 1 + '0';

  result = high_part = 0;
  for (i = 0; is_digit_in_base (num[i], base); i += 1)
    {
      result = result*base + digit_to_int (num[i]);
      high_part = high_part*base + (unsigned int) (result >> HIGH_BYTE_POSN);
      result &= ((ULONGEST) 1 << HIGH_BYTE_POSN) - 1;
      if (high_part > 0xff) 
	{
	  errno = ERANGE;
	  result = high_part = 0;
	  break;
	}
    }

  if (trailer != NULL)
    *trailer = &num[i];

  return result + ((ULONGEST) high_part << HIGH_BYTE_POSN);
}



/* Interprets the prefix of NUM that consists of digits of the given BASE
   as an integer of that BASE, with the string EXP as an exponent.
   Puts value in yylval, and returns INT, if the string is valid.  Causes
   an error if the number is improperly formated.   BASE, if NULL, defaults 
   to "10", and EXP to "1". The EXP does not contain a leading 'e' or 'E'. */

static int
processInt (base0, num0, exp0)
     const char* num0;
     const char* base0;
     const char* exp0;
{
  ULONGEST result;
  long exp;
  int base;

  char* trailer;

  if (base0 == NULL)
    base = 10;
  else
    {	
      base = strtol (base0, (char**) NULL, 10);
      if (base < 2 || base > 16)
	error ("Invalid base: %d.", base);
    }

  if (exp0 == NULL)
    exp = 0;
  else
    exp = strtol(exp0, (char**) NULL, 10);

  errno = 0;
  result = strtoulst (num0, &trailer, base);
  if (errno == ERANGE)
    error ("Integer literal out of range");
  if (isxdigit(*trailer))
    error ("Invalid digit `%c' in based literal", *trailer);

  while (exp > 0) 
    {
      if (result > (ULONG_MAX / base))
	error ("Integer literal out of range");
      result *= base;
      exp -= 1;
    }
    
  if ((result >> (TARGET_INT_BIT-1)) == 0)
    yylval.typed_val.type = builtin_type_ada_int;
  else if ((result >> (TARGET_LONG_BIT-1)) == 0)
    yylval.typed_val.type = builtin_type_ada_long;
  else if (((result >> (TARGET_LONG_BIT-1)) >> 1) == 0)
    {
      /* We have a number representable as an unsigned integer quantity.
         For consistency with the C treatment, we will treat it as an 
	 anonymous modular (unsigned) quantity.  Alas, the types are such
	 that we need to store .val as a signed quantity.  Sorry 
         for the mess, but C doesn't officially guarantee that a simple
         assignment does the trick (no, it doesn't; read the reference manual).
       */
      yylval.typed_val.type = builtin_type_unsigned_long;
      if (result & LONGEST_SIGN)
	yylval.typed_val.val = 
	  (LONGEST) (result & ~LONGEST_SIGN) 
	  - (LONGEST_SIGN>>1) - (LONGEST_SIGN>>1);
      else
	yylval.typed_val.val = (LONGEST) result;
      return INT;
    }
  else 
    yylval.typed_val.type = builtin_type_ada_long_long;

  yylval.typed_val.val = (LONGEST) result;
  return INT;
}

static int
processReal (num0)
     const char* num0;
{
  if (sizeof (DOUBLEST) <= sizeof (float))
    sscanf (num0, "%g", &yylval.typed_val_float.dval);
  else if (sizeof (DOUBLEST) <= sizeof (double))
    sscanf (num0, "%lg", &yylval.typed_val_float.dval);
  else
    {
#ifdef PRINTF_HAS_LONG_DOUBLE
      sscanf (num0, "%Lg", &yylval.typed_val_float.dval);
#else
      /* Scan it into a double, then convert and assign it to the 
	 long double.  This at least wins with values representable 
	 in the range of doubles. */
      double temp;
      sscanf (num0, "%lg", &temp);
      yylval.typed_val_float.dval = temp;
#endif
    }

  yylval.typed_val_float.type = builtin_type_ada_float;
  if (sizeof(DOUBLEST) >= TARGET_DOUBLE_BIT / TARGET_CHAR_BIT)
    yylval.typed_val_float.type = builtin_type_ada_double;
  if (sizeof(DOUBLEST) >= TARGET_LONG_DOUBLE_BIT / TARGET_CHAR_BIT)
    yylval.typed_val_float.type = builtin_type_ada_long_double;

  return FLOAT;
}

static int
processId (name0, len)
     const char *name0;
     int len;
{
  char* name = xmalloc (len + 11);
  int i0, i;
  
/*  add_name_string_cleanup (name); */
/* FIXME: add_name_string_cleanup should be defined in parse.c */
  while (len > 0 && isspace (name0[len-1]))
    len -= 1;
  i = i0 = 0;
  while (i0 < len) 
    {
      if (isalnum (name0[i0]))
	{
	  name[i] = tolower (name0[i0]);
	  i += 1; i0 += 1;
	}
      else switch (name0[i0]) 
	{
	default:
	  name[i] = name0[i0];
	  i += 1; i0 += 1;
	  break;
	case ' ': case '\t':
	  i0 += 1;
	  break;
	case '\'':
	  i0 += 1;
	  while (i0 < len && name0[i0] != '\'')
	    {
	      name[i] = name0[i0];
	      i += 1; i0 += 1;
	    }
	  i0 += 1;
	  break;
	case '<':
	  i0 += 1;
	  while (i0 < len && name0[i0] != '>')
	    {
	      name[i] = name0[i0];
	      i += 1; i0 += 1;
	    }
	  i0 += 1;
	  break;
	}
    }
  name[i] = '\000';

  yylval.ssym.sym = NULL;
  yylval.ssym.stoken.ptr = name;
  yylval.ssym.stoken.length = i;
  return NAME;
}

static void 
block_lookup (name, err_name)
     char* name;
     char* err_name;
{
  struct symbol** syms;
  struct block** blocks;
  int nsyms;
  struct symtab *symtab;
  nsyms = ada_lookup_symbol_list (name, left_block_context,
				  VAR_DOMAIN, &syms, &blocks);
  if (left_block_context == NULL &&
      (nsyms == 0 || SYMBOL_CLASS (syms[0]) != LOC_BLOCK))
    symtab = lookup_symtab (name);
  else
    symtab = NULL;

  if (symtab != NULL)
    left_block_context = yylval.bval =
      BLOCKVECTOR_BLOCK (BLOCKVECTOR (symtab), STATIC_BLOCK);
  else if (nsyms == 0 || SYMBOL_CLASS (syms[0]) != LOC_BLOCK)
    {
      if (left_block_context == NULL)
	error ("No file or function \"%s\".", err_name);
      else
	error ("No function \"%s\" in specified context.", err_name);
    }
  else 
    {
      left_block_context = yylval.bval = SYMBOL_BLOCK_VALUE (syms[0]); 
      if (nsyms > 1)
	warning ("Function name \"%s\" ambiguous here", err_name);
    }
}

/* Look up NAME0 (assumed to be mangled) as a name in VAR_DOMAIN,
   setting *TOKEN_TYPE to NAME or TYPENAME, depending on what is
   found.  Try first the entire name, then the name without the last 
   segment (i.e., after the last .id), etc., and return the number of
   segments that had to be removed to get a match.  Calls error if no
   matches are found, using ERR_NAME in any error message.  When
   exactly one symbol match is found, it is placed in yylval. */
 
static int
name_lookup (name0, err_name, token_type)
     char* name0;
     char* err_name;
     int* token_type;
{
  struct symbol** syms;
  struct block** blocks;
  struct type* type;
  int len0 = strlen (name0);
  char* name = savestring (name0, len0);
  int nsyms;
  int segments;
 
/*  add_name_string_cleanup (name);*/
/* FIXME: add_name_string_cleanup should be defined in parse.c */
  yylval.ssym.stoken.ptr = name;
  yylval.ssym.stoken.length = strlen (name);
  for (segments = 0; ; segments += 1)
    {
      struct type* preferred_type;
      int i, preferred_index;

      if (left_block_context == NULL) 
	nsyms = ada_lookup_symbol_list (name, expression_context_block, 
					VAR_DOMAIN, &syms, &blocks);
      else
	nsyms = ada_lookup_symbol_list (name, left_block_context, 
					VAR_DOMAIN, &syms, &blocks);

      /* Check for a type definition. */

      /* Look for a symbol that doesn't denote void.  This is (I think) a */
      /* temporary kludge to get around problems in GNAT output. */
      preferred_index = -1; preferred_type = NULL;
      for (i = 0; i < nsyms; i += 1)
	switch (SYMBOL_CLASS (syms[i])) 
	  {
	  case LOC_TYPEDEF:
	    if (ada_prefer_type (SYMBOL_TYPE (syms[i]), preferred_type))
	      {
		preferred_index = i;
		preferred_type = SYMBOL_TYPE (syms[i]);
	      }
	    break;
	  case LOC_REGISTER:
	  case LOC_ARG:
	  case LOC_REF_ARG:
	  case LOC_REGPARM:
	  case LOC_REGPARM_ADDR:
	  case LOC_LOCAL:
	  case LOC_LOCAL_ARG:
	  case LOC_BASEREG:
	  case LOC_BASEREG_ARG:
	    goto NotType;
	  default:
	    break;
	  }
      if (preferred_type != NULL)
	{
/*	  if (TYPE_CODE (preferred_type) == TYPE_CODE_VOID)
	    error ("`%s' matches only void type name(s)", 
		   ada_demangle (name));
*/
/* FIXME: ada_demangle should be defined in defs.h, and is located in ada-lang.c */
/*	  else*/ if (ada_is_object_renaming (syms[preferred_index]))
	    {
	      yylval.ssym.sym = syms[preferred_index];
	      *token_type = OBJECT_RENAMING;
	      return segments;
	    } 
	  else if (ada_renaming_type (SYMBOL_TYPE (syms[preferred_index])) 
                   != NULL)
	    {
	      int result;
	      const char* renaming = 
		ada_simple_renamed_entity (syms[preferred_index]);
	      char* new_name = xmalloc (strlen (renaming) + len0 
					- yylval.ssym.stoken.length + 1);
/*	      add_name_string_cleanup (new_name);*/
/* FIXME: add_name_string_cleanup should be defined in parse.c */
	      strcpy (new_name, renaming);
	      strcat (new_name, name0 + yylval.ssym.stoken.length);
	      result = name_lookup (new_name, err_name, token_type);
	      if (result > segments) 
		error ("Confused by renamed symbol.");
	      return result;
	    }
	  else if (segments == 0)
	    {
	      yylval.tval = preferred_type;
	      *token_type = TYPENAME;
	      return 0;
	    } 
	}

      if (segments == 0)
	{
	  type = lookup_primitive_typename (name);
	  if (type == NULL && DEPRECATED_STREQ ("system__address", name))
	    type = builtin_type_ada_system_address;
	  if (type != NULL)
	    {
	      yylval.tval = type;
	      *token_type = TYPENAME;
	      return 0;
	    }
	}

    NotType:
      if (nsyms == 1) 
	{
	  *token_type = NAME;
	  yylval.ssym.sym = syms[0];
	  yylval.ssym.msym = NULL;
	  yylval.ssym.block = blocks[0];
	  return segments;
	}
      else if (nsyms == 0) {
	int i;
	yylval.ssym.msym = ada_lookup_minimal_symbol (name);
	if (yylval.ssym.msym != NULL)
	  {
	    yylval.ssym.sym = NULL;
	    yylval.ssym.block = NULL;
            *token_type = NAME;
	    return segments;
	  }

	for (i = yylval.ssym.stoken.length - 1; i > 0; i -= 1)
	  {
            if (name[i] == '.')
	      { 
		name[i] = '\0';
		yylval.ssym.stoken.length = i;
		break;
	      }
	    else if (name[i] == '_' && name[i-1] == '_')
	      {
		i -= 1;
		name[i] = '\0';
		yylval.ssym.stoken.length = i;
		break;
	      }
	  }
	if (i <= 0) 
	  {
	    if (!have_full_symbols () && !have_partial_symbols ()
		&& left_block_context == NULL)
	      error ("No symbol table is loaded.  Use the \"file\" command.");
	    if (left_block_context == NULL)
	      error ("No definition of \"%s\" in current context.", 
		     err_name);
	    else
	      error ("No definition of \"%s\" in specified context.", 
		     err_name);
	  }
      }
      else 
	{
	  *token_type = NAME;
	  yylval.ssym.sym = NULL;
	  yylval.ssym.msym = NULL;
	  if (left_block_context == NULL)
	    yylval.ssym.block = expression_context_block;
	  else
	    yylval.ssym.block = left_block_context;
	  return segments;
	}
    }
}

/* Returns the position within STR of the '.' in a
   '.{WHITE}*all' component of a dotted name, or -1 if there is none. */
static int
find_dot_all (str)
     const char* str;
{
  int i;
  for (i = 0; str[i] != '\000'; i += 1)
    {
      if (str[i] == '.')
	{
	  int i0 = i;
	  do 
	    i += 1;
	  while (isspace (str[i]));
	  if (strcmp (str+i, "all") == 0
	      && ! isalnum (str[i+3]) && str[i+3] != '_')
	    return i0;
	}
    }
  return -1;
}    

/* Returns non-zero iff string SUBSEQ matches a subsequence of STR, ignoring
   case. */

static int
subseqMatch (subseq, str)
     const char* subseq;
     const char* str;
{
  if (subseq[0] == '\0')
    return 1;
  else if (str[0] == '\0')
    return 0;
  else if (tolower (subseq[0]) == tolower (str[0]))
    return subseqMatch (subseq+1, str+1) || subseqMatch (subseq, str+1);
  else
    return subseqMatch (subseq, str+1);
}
  

static struct { const char* name; int code; } 
attributes[] = {
  { "address", TICK_ADDRESS },
  { "unchecked_access", TICK_ACCESS },
  { "unrestricted_access", TICK_ACCESS },
  { "access", TICK_ACCESS },
  { "first", TICK_FIRST },
  { "last", TICK_LAST },
  { "length", TICK_LENGTH },
  { "max", TICK_MAX },
  { "min", TICK_MIN },
  { "modulus", TICK_MODULUS },
  { "pos", TICK_POS },
  { "range", TICK_RANGE },
  { "size", TICK_SIZE },
  { "tag", TICK_TAG },
  { "val", TICK_VAL },
  { NULL, -1 }
};

/* Return the syntactic code corresponding to the attribute name or
   abbreviation STR.  */

static int
processAttribute (str)
     const char* str;
{
  int i, k;

  for (i = 0; attributes[i].code != -1; i += 1)
    if (strcasecmp (str, attributes[i].name) == 0)
      return attributes[i].code;

  for (i = 0, k = -1; attributes[i].code != -1; i += 1)
    if (subseqMatch (str, attributes[i].name)) 
      {
	if (k == -1)
	  k = i;
	else 
	  error ("ambiguous attribute name: `%s'", str);
      }
  if (k == -1)
    error ("unrecognized attribute: `%s'", str);

  return attributes[k].code;
}

int
yywrap()
{
  return 1;
}
