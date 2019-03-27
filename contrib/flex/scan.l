/* scan.l - scanner for flex input -*-C-*- */

%{
/*  Copyright (c) 1990 The Regents of the University of California. */
/*  All rights reserved. */

/*  This code is derived from software contributed to Berkeley by */
/*  Vern Paxson. */

/*  The United States Government has rights in this work pursuant */
/*  to contract no. DE-AC03-76SF00098 between the United States */
/*  Department of Energy and the University of California. */

/*  This file is part of flex. */

/*  Redistribution and use in source and binary forms, with or without */
/*  modification, are permitted provided that the following conditions */
/*  are met: */

/*  1. Redistributions of source code must retain the above copyright */
/*     notice, this list of conditions and the following disclaimer. */
/*  2. Redistributions in binary form must reproduce the above copyright */
/*     notice, this list of conditions and the following disclaimer in the */
/*     documentation and/or other materials provided with the distribution. */

/*  Neither the name of the University nor the names of its contributors */
/*  may be used to endorse or promote products derived from this software */
/*  without specific prior written permission. */

/*  THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR */
/*  IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED */
/*  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR */
/*  PURPOSE. */

#include "flexdef.h"
#include "parse.h"
extern bool tablesverify, tablesext;
extern int trlcontxt; /* Set in  parse.y for each rule. */
extern const char *escaped_qstart, *escaped_qend;

#define ACTION_ECHO add_action( yytext )
#define ACTION_IFDEF(def, should_define) \
	{ \
	if ( should_define ) \
		action_define( def, 1 ); \
	}

#define ACTION_ECHO_QSTART add_action (escaped_qstart)
#define ACTION_ECHO_QEND   add_action (escaped_qend)

#define ACTION_M4_IFDEF(def, should_define) \
    do{ \
        if ( should_define ) \
            buf_m4_define( &m4defs_buf, def, NULL);\
        else \
            buf_m4_undefine( &m4defs_buf, def);\
    } while(0)

#define MARK_END_OF_PROLOG mark_prolog();

#define YY_DECL \
	int flexscan()

#define RETURNCHAR \
	yylval = (unsigned char) yytext[0]; \
	return CHAR;

#define RETURNNAME \
	if(yyleng < MAXLINE) \
         { \
	strcpy( nmstr, yytext ); \
	 } \
	else \
	 { \
	   synerr(_("Input line too long\n")); \
	   FLEX_EXIT(EXIT_FAILURE);  \
	 }  \
	return NAME;

#define PUT_BACK_STRING(str, start) \
	for ( i = strlen( str ) - 1; i >= start; --i ) \
		unput((str)[i])

#define CHECK_REJECT(str) \
	if ( all_upper( str ) ) \
		reject = true;

#define CHECK_YYMORE(str) \
	if ( all_lower( str ) ) \
		yymore_used = true;

#define YY_USER_INIT \
	if ( getenv("POSIXLY_CORRECT") ) \
		posix_compat = true;

%}

%option caseless nodefault stack noyy_top_state
%option nostdinit

%x SECT2 SECT2PROLOG SECT3 CODEBLOCK PICKUPDEF SC CARETISBOL NUM QUOTE
%x FIRSTCCL CCL ACTION RECOVER COMMENT ACTION_STRING PERCENT_BRACE_ACTION
%x OPTION LINEDIR CODEBLOCK_MATCH_BRACE
%x GROUP_WITH_PARAMS
%x GROUP_MINUS_PARAMS
%x EXTENDED_COMMENT
%x COMMENT_DISCARD

WS		[[:blank:]]+
OPTWS		[[:blank:]]*
NOT_WS		[^[:blank:]\r\n]

NL		\r?\n

NAME		([[:alpha:]_][[:alnum:]_-]*)
NOT_NAME	[^[:alpha:]_*\n]+

SCNAME		{NAME}

ESCSEQ		(\\([^\n]|[0-7]{1,3}|x[[:xdigit:]]{1,2}))

FIRST_CCL_CHAR	([^\\\n]|{ESCSEQ})
CCL_CHAR	([^\\\n\]]|{ESCSEQ})
CCL_EXPR	("[:"^?[[:alpha:]]+":]")

LEXOPT		[aceknopr]

M4QSTART    "[["
M4QEND      "]]"

%%
	static int bracelevel, didadef, indented_code;
	static int doing_rule_action = false;
	static int option_sense;

	int doing_codeblock = false;
	int i, brace_depth=0, brace_start_line=0;
	Char nmdef[MAXLINE];


<INITIAL>{
	^{WS}		indented_code = true; BEGIN(CODEBLOCK);
	^"/*"		ACTION_ECHO; yy_push_state( COMMENT );
	^#{OPTWS}line{WS}	yy_push_state( LINEDIR );
	^"%s"{NAME}?	return SCDECL;
	^"%x"{NAME}?	return XSCDECL;
	^"%{".*{NL}	{
			++linenum;
			line_directive_out( (FILE *) 0, 1 );
			indented_code = false;
			BEGIN(CODEBLOCK);
			}
    ^"%top"[[:blank:]]*"{"[[:blank:]]*{NL}    {
                brace_start_line = linenum;
                ++linenum;
                buf_linedir( &top_buf, infilename?infilename:"<stdin>", linenum);
                brace_depth = 1;
                yy_push_state(CODEBLOCK_MATCH_BRACE);
            }

    ^"%top".*   synerr( _("malformed '%top' directive") );

	{WS}		/* discard */

	^"%%".*		{
			sectnum = 2;
			bracelevel = 0;
			mark_defs1();
			line_directive_out( (FILE *) 0, 1 );
			BEGIN(SECT2PROLOG);
			return SECTEND;
			}

	^"%pointer".*{NL}	yytext_is_array = false; ++linenum;
	^"%array".*{NL}		yytext_is_array = true; ++linenum;

	^"%option"	BEGIN(OPTION); return OPTION_OP;

	^"%"{LEXOPT}{OPTWS}[[:digit:]]*{OPTWS}{NL}	++linenum; /* ignore */
	^"%"{LEXOPT}{WS}.*{NL}	++linenum;	/* ignore */

	/* xgettext: no-c-format */
	^"%"[^sxaceknopr{}].*	synerr( _( "unrecognized '%' directive" ) );

	^{NAME}		{
			if(yyleng < MAXLINE)
        		 {
			strcpy( nmstr, yytext );
			 }
			else
			 {
			   synerr( _("Definition name too long\n"));
			   FLEX_EXIT(EXIT_FAILURE);
			 }

			didadef = false;
			BEGIN(PICKUPDEF);
			}

	{SCNAME}	RETURNNAME;
	^{OPTWS}{NL}	++linenum; /* allows blank lines in section 1 */
	{OPTWS}{NL}	ACTION_ECHO; ++linenum; /* maybe end of comment line */
}


<COMMENT>{
	"*/"		ACTION_ECHO; yy_pop_state();
	"*"		ACTION_ECHO;
    {M4QSTART}  ACTION_ECHO_QSTART;
    {M4QEND}    ACTION_ECHO_QEND;
	[^*\n]      ACTION_ECHO;
	{NL}	    ++linenum; ACTION_ECHO;
}

<COMMENT_DISCARD>{
        /* This is the same as COMMENT, but is discarded rather than output. */
	"*/"		yy_pop_state();
    "*"         ;
	[^*\n]      ;
	{NL}	    ++linenum;
}

<EXTENDED_COMMENT>{
    ")"         yy_pop_state();
    [^\n\)]+      ;
    {NL}        ++linenum;        
}

<LINEDIR>{
	\n		yy_pop_state();
	[[:digit:]]+	linenum = myctoi( yytext );

	\"[^"\n]*\"	{
			flex_free( (void *) infilename );
			infilename = copy_string( yytext + 1 );
			infilename[strlen( infilename ) - 1] = '\0';
			}
	.		/* ignore spurious characters */
}

<CODEBLOCK>{
	^"%}".*{NL}	++linenum; BEGIN(INITIAL);

    {M4QSTART}  ACTION_ECHO_QSTART;
    {M4QEND}    ACTION_ECHO_QEND;
	.	        ACTION_ECHO;

	{NL}		{
			++linenum;
			ACTION_ECHO;
			if ( indented_code )
				BEGIN(INITIAL);
			}
}

<CODEBLOCK_MATCH_BRACE>{
    "}"     {
                if( --brace_depth == 0){
                    /* TODO: Matched. */
                    yy_pop_state();
                }else
                    buf_strnappend(&top_buf, yytext, yyleng);
            }

    "{"     {
                brace_depth++;
                buf_strnappend(&top_buf, yytext, yyleng);
            }

    {NL}    {
                ++linenum;
                buf_strnappend(&top_buf, yytext, yyleng);
            }

    {M4QSTART}  buf_strnappend(&top_buf, escaped_qstart, strlen(escaped_qstart));
    {M4QEND}    buf_strnappend(&top_buf, escaped_qend, strlen(escaped_qend));

    [^{}\r\n]  {
                buf_strnappend(&top_buf, yytext, yyleng);
               }

    <<EOF>>     {
                linenum = brace_start_line;
                synerr(_("Unmatched '{'"));
                yyterminate();
                }
}


<PICKUPDEF>{
	{WS}		/* separates name and definition */

	{NOT_WS}[^\r\n]*	{
 		        if(yyleng < MAXLINE)
 		         {
			strcpy( (char *) nmdef, yytext );
 		         }
 		        else
 		         {
 		           format_synerr( _("Definition value for {%s} too long\n"), nmstr);
 		           FLEX_EXIT(EXIT_FAILURE);
			 }
			/* Skip trailing whitespace. */
			for ( i = strlen( (char *) nmdef ) - 1;
			      i >= 0 && (nmdef[i] == ' ' || nmdef[i] == '\t');
			      --i )
				;

			nmdef[i + 1] = '\0';

			ndinstal( nmstr, nmdef );
			didadef = true;
			}

	{NL}		{
			if ( ! didadef )
				synerr( _( "incomplete name definition" ) );
			BEGIN(INITIAL);
			++linenum;
			}
}


<OPTION>{
	{NL}		++linenum; BEGIN(INITIAL);
	{WS}		option_sense = true;

	"="		return '=';

	no		option_sense = ! option_sense;

	7bit		csize = option_sense ? 128 : 256;
	8bit		csize = option_sense ? 256 : 128;

	align		long_align = option_sense;
	always-interactive	{
			ACTION_M4_IFDEF( "M4""_YY_ALWAYS_INTERACTIVE", option_sense );
            interactive = option_sense;
			}
	array		yytext_is_array = option_sense;
    ansi-definitions ansi_func_defs = option_sense;
    ansi-prototypes  ansi_func_protos = option_sense;
	backup		backing_up_report = option_sense;
	batch		interactive = ! option_sense;
    bison-bridge     bison_bridge_lval = option_sense;
    bison-locations  { if((bison_bridge_lloc = option_sense))
                            bison_bridge_lval = true;
                     }
	"c++"		C_plus_plus = option_sense;
	caseful|case-sensitive		sf_set_case_ins(!option_sense);
	caseless|case-insensitive	sf_set_case_ins(option_sense);
	debug		ddebug = option_sense;
	default		spprdflt = ! option_sense;
	ecs		useecs = option_sense;
	fast		{
			useecs = usemecs = false;
			use_read = fullspd = true;
			}
	full		{
			useecs = usemecs = false;
			use_read = fulltbl = true;
			}
	input		ACTION_IFDEF("YY_NO_INPUT", ! option_sense);
	interactive	interactive = option_sense;
	lex-compat	lex_compat = option_sense;
	posix-compat	posix_compat = option_sense;
	main		{
			ACTION_M4_IFDEF( "M4""_YY_MAIN", option_sense);
            /* Override yywrap */
            if( option_sense == true )
                do_yywrap = false;
			}
	meta-ecs	usemecs = option_sense;
	never-interactive	{
			ACTION_M4_IFDEF( "M4""_YY_NEVER_INTERACTIVE", option_sense );
            interactive = !option_sense;
			}
	perf-report	performance_report += option_sense ? 1 : -1;
	pointer		yytext_is_array = ! option_sense;
	read		use_read = option_sense;
    reentrant   reentrant = option_sense;
	reject		reject_really_used = option_sense;
	stack		ACTION_M4_IFDEF( "M4""_YY_STACK_USED", option_sense );
	stdinit		do_stdinit = option_sense;
	stdout		use_stdout = option_sense;
    unistd      ACTION_IFDEF("YY_NO_UNISTD_H", ! option_sense);
	unput		ACTION_M4_IFDEF("M4""_YY_NO_UNPUT", ! option_sense);
	verbose		printstats = option_sense;
	warn		nowarn = ! option_sense;
	yylineno	do_yylineno = option_sense; ACTION_M4_IFDEF("M4""_YY_USE_LINENO", option_sense);
	yymore		yymore_really_used = option_sense;
	yywrap      do_yywrap = option_sense;

	yy_push_state	ACTION_M4_IFDEF("M4""_YY_NO_PUSH_STATE", ! option_sense);
	yy_pop_state	ACTION_M4_IFDEF("M4""_YY_NO_POP_STATE", ! option_sense);
	yy_top_state	ACTION_M4_IFDEF("M4""_YY_NO_TOP_STATE", ! option_sense);

	yy_scan_buffer	ACTION_M4_IFDEF("M4""_YY_NO_SCAN_BUFFER", ! option_sense);
	yy_scan_bytes	ACTION_M4_IFDEF("M4""_YY_NO_SCAN_BYTES", ! option_sense);
	yy_scan_string	ACTION_M4_IFDEF("M4""_YY_NO_SCAN_STRING", ! option_sense);

    yyalloc         ACTION_M4_IFDEF("M4""_YY_NO_FLEX_ALLOC", ! option_sense);
    yyrealloc       ACTION_M4_IFDEF("M4""_YY_NO_FLEX_REALLOC", ! option_sense);
    yyfree          ACTION_M4_IFDEF("M4""_YY_NO_FLEX_FREE", ! option_sense);

    yyget_debug     ACTION_M4_IFDEF("M4""_YY_NO_GET_DEBUG", ! option_sense);
    yyset_debug     ACTION_M4_IFDEF("M4""_YY_NO_SET_DEBUG", ! option_sense);
    yyget_extra     ACTION_M4_IFDEF("M4""_YY_NO_GET_EXTRA", ! option_sense);
    yyset_extra     ACTION_M4_IFDEF("M4""_YY_NO_SET_EXTRA", ! option_sense);
    yyget_leng      ACTION_M4_IFDEF("M4""_YY_NO_GET_LENG", ! option_sense);
    yyget_text      ACTION_M4_IFDEF("M4""_YY_NO_GET_TEXT", ! option_sense);
    yyget_lineno    ACTION_M4_IFDEF("M4""_YY_NO_GET_LINENO", ! option_sense);
    yyset_lineno    ACTION_M4_IFDEF("M4""_YY_NO_SET_LINENO", ! option_sense);
    yyget_in        ACTION_M4_IFDEF("M4""_YY_NO_GET_IN", ! option_sense);
    yyset_in        ACTION_M4_IFDEF("M4""_YY_NO_SET_IN", ! option_sense);
    yyget_out       ACTION_M4_IFDEF("M4""_YY_NO_GET_OUT", ! option_sense);
    yyset_out       ACTION_M4_IFDEF("M4""_YY_NO_SET_OUT", ! option_sense);
    yyget_lval      ACTION_M4_IFDEF("M4""_YY_NO_GET_LVAL", ! option_sense);
    yyset_lval      ACTION_M4_IFDEF("M4""_YY_NO_SET_LVAL", ! option_sense);
    yyget_lloc      ACTION_M4_IFDEF("M4""_YY_NO_GET_LLOC", ! option_sense);
    yyset_lloc      ACTION_M4_IFDEF("M4""_YY_NO_SET_LLOC", ! option_sense);

	extra-type	return OPT_EXTRA_TYPE;
	outfile		return OPT_OUTFILE;
	prefix		return OPT_PREFIX;
	yyclass		return OPT_YYCLASS;
	header(-file)?      return OPT_HEADER;
	tables-file         return OPT_TABLES;
	tables-verify   {
                    tablesverify = option_sense;
                    if(!tablesext && option_sense)
                        tablesext = true;
                    }


	\"[^"\n]*\"	{
			if(yyleng-1 < MAXLINE)
        		 {
			strcpy( nmstr, yytext + 1 );
			 }
			else
			 {
			   synerr( _("Option line too long\n"));
			   FLEX_EXIT(EXIT_FAILURE);
			 }
			nmstr[strlen( nmstr ) - 1] = '\0';
			return NAME;
			}

	(([a-mo-z]|n[a-np-z])[[:alpha:]\-+]*)|.	{
			format_synerr( _( "unrecognized %%option: %s" ),
				yytext );
			BEGIN(RECOVER);
			}
}

<RECOVER>.*{NL}		++linenum; BEGIN(INITIAL);


<SECT2PROLOG>{
	^"%{".*	++bracelevel; yyless( 2 );	/* eat only %{ */
	^"%}".*	--bracelevel; yyless( 2 );	/* eat only %} */

	^{WS}.*	ACTION_ECHO;	/* indented code in prolog */

	^{NOT_WS}.*	{	/* non-indented code */
			if ( bracelevel <= 0 )
				{ /* not in %{ ... %} */
				yyless( 0 );	/* put it all back */
				yy_set_bol( 1 );
				mark_prolog();
				BEGIN(SECT2);
				}
			else
				ACTION_ECHO;
			}

	.		ACTION_ECHO;
	{NL}	++linenum; ACTION_ECHO;

	<<EOF>>		{
			mark_prolog();
			sectnum = 0;
			yyterminate(); /* to stop the parser */
			}
}

<SECT2>{
	^{OPTWS}{NL}	++linenum; /* allow blank lines in section 2 */

	^{OPTWS}"%{"	{
			indented_code = false;
			doing_codeblock = true;
			bracelevel = 1;
			BEGIN(PERCENT_BRACE_ACTION);
			}

	^{OPTWS}"<"	    {
                        /* Allow "<" to appear in (?x) patterns. */
                        if (!sf_skip_ws())
                            BEGIN(SC);
                        return '<';
                    }
	^{OPTWS}"^"	return '^';
	\"		BEGIN(QUOTE); return '"';
	"{"/[[:digit:]]	{
			BEGIN(NUM);
			if ( lex_compat || posix_compat )
				return BEGIN_REPEAT_POSIX;
			else
				return BEGIN_REPEAT_FLEX;
			}
	"$"/([[:blank:]]|{NL})	return '$';

	{WS}"%{"		{
			bracelevel = 1;
			BEGIN(PERCENT_BRACE_ACTION);

			if ( in_rule )
				{
				doing_rule_action = true;
				in_rule = false;
				return '\n';
				}
			}
	{WS}"|".*{NL}	{
                        if (sf_skip_ws()){
                            /* We're in the middle of a (?x: ) pattern. */
                            /* Push back everything starting at the "|" */
                            size_t amt;
                            amt = strchr (yytext, '|') - yytext;
                            yyless(amt);
                        }
                        else {
                            continued_action = true;
                            ++linenum;
                            return '\n';
                        }
                    }

	^{WS}"/*"	{

                if (sf_skip_ws()){
                    /* We're in the middle of a (?x: ) pattern. */
                    yy_push_state(COMMENT_DISCARD);
                }
                else{
                    yyless( yyleng - 2 );	/* put back '/', '*' */
                    bracelevel = 0;
                    continued_action = false;
                    BEGIN(ACTION);
                }
			}

	^{WS}		/* allow indented rules */ ;

	{WS}		{
            if (sf_skip_ws()){
                /* We're in the middle of a (?x: ) pattern. */
            }
            else{
                /* This rule is separate from the one below because
                 * otherwise we get variable trailing context, so
                 * we can't build the scanner using -{f,F}.
                 */
                bracelevel = 0;
                continued_action = false;
                BEGIN(ACTION);

                if ( in_rule )
                    {
                    doing_rule_action = true;
                    in_rule = false;
                    return '\n';
                    }
            }
			}

	{OPTWS}{NL}	{
            if (sf_skip_ws()){
                /* We're in the middle of a (?x: ) pattern. */
                ++linenum;
            }
            else{
                bracelevel = 0;
                continued_action = false;
                BEGIN(ACTION);
                unput( '\n' );	/* so <ACTION> sees it */

                if ( in_rule )
                    {
                    doing_rule_action = true;
                    in_rule = false;
                    return '\n';
                    }
            }
			}

	^{OPTWS}"<<EOF>>"	|
	"<<EOF>>"	return EOF_OP;

	^"%%".*		{
			sectnum = 3;
			BEGIN(SECT3);
			outn("/* Begin user sect3 */");
			yyterminate(); /* to stop the parser */
			}

	"["({FIRST_CCL_CHAR}|{CCL_EXPR})({CCL_CHAR}|{CCL_EXPR})*	{
			int cclval;

			if(yyleng < MAXLINE)
        		 {
			strcpy( nmstr, yytext );
			 }
			else
			 {
			   synerr( _("Input line too long\n"));
			   FLEX_EXIT(EXIT_FAILURE);
			 }

			/* Check to see if we've already encountered this
			 * ccl.
			 */
			if (0 /* <--- This "0" effectively disables the reuse of a
                   * character class (purely based on its source text).
                   * The reason it was disabled is so yacc/bison can parse
                   * ccl operations, such as ccl difference and union.
                   */
                &&  (cclval = ccllookup( (Char *) nmstr )) != 0 )
				{
				if ( input() != ']' )
					synerr( _( "bad character class" ) );

				yylval = cclval;
				++cclreuse;
				return PREVCCL;
				}
			else
				{
				/* We fudge a bit.  We know that this ccl will
				 * soon be numbered as lastccl + 1 by cclinit.
				 */
				cclinstal( (Char *) nmstr, lastccl + 1 );

				/* Push back everything but the leading bracket
				 * so the ccl can be rescanned.
				 */
				yyless( 1 );

				BEGIN(FIRSTCCL);
				return '[';
				}
			}
    "{-}"       return CCL_OP_DIFF;
    "{+}"       return CCL_OP_UNION;


    /* Check for :space: at the end of the rule so we don't
     * wrap the expanded regex in '(' ')' -- breaking trailing
     * context.
     */
	"{"{NAME}"}"[[:space:]]?	 {
			Char *nmdefptr;
            int end_is_ws, end_ch;

            end_ch = yytext[yyleng-1];
            end_is_ws = end_ch != '}' ? 1 : 0;

 			if(yyleng-1 < MAXLINE)
         		 {
			strcpy( nmstr, yytext + 1 );
 			 }
 			else
 			 {
 			   synerr( _("Input line too long\n"));
 			   FLEX_EXIT(EXIT_FAILURE);
 			 }
nmstr[yyleng - 2 - end_is_ws] = '\0';  /* chop trailing brace */

			if ( (nmdefptr = ndlookup( nmstr )) == 0 )
				format_synerr(
					_( "undefined definition {%s}" ),
						nmstr );

			else
				{ /* push back name surrounded by ()'s */
				int len = strlen( (char *) nmdefptr );
                if (end_is_ws)
                    unput(end_ch);

				if ( lex_compat || nmdefptr[0] == '^' ||
				     (len > 0 && nmdefptr[len - 1] == '$')
                     || (end_is_ws && trlcontxt && !sf_skip_ws()))
					{ /* don't use ()'s after all */
					PUT_BACK_STRING((char *) nmdefptr, 0);

					if ( nmdefptr[0] == '^' )
						BEGIN(CARETISBOL);
					}

				else
					{
					unput(')');
					PUT_BACK_STRING((char *) nmdefptr, 0);
					unput('(');
					}
				}
			}

    "/*"        {
                    if (sf_skip_ws())
                        yy_push_state(COMMENT_DISCARD);
                    else{
                        /* Push back the "*" and return "/" as usual. */
                        yyless(1);
                        return '/';
                    }
                }

    "(?#"       {
                    if (lex_compat || posix_compat){
                        /* Push back the "?#" and treat it like a normal parens. */
                        yyless(1);
                        sf_push(); 
                        return '(';
                    }
                    else
                        yy_push_state(EXTENDED_COMMENT);
                }
    "(?"        {
                    sf_push();
                    if (lex_compat || posix_compat)
                        /* Push back the "?" and treat it like a normal parens. */
                        yyless(1);
                    else
                        BEGIN(GROUP_WITH_PARAMS);
                    return '(';
                }
    "("         sf_push(); return '(';
    ")"         sf_pop(); return ')';

	[/|*+?.(){}]	return (unsigned char) yytext[0];
	.		RETURNCHAR;
}


<SC>{
	{OPTWS}{NL}{OPTWS}	++linenum;	/* Allow blank lines & continuations */
	[,*]		return (unsigned char) yytext[0];
	">"		BEGIN(SECT2); return '>';
	">"/^		BEGIN(CARETISBOL); return '>';
	{SCNAME}	RETURNNAME;
	.		{
			format_synerr( _( "bad <start condition>: %s" ),
				yytext );
			}
}

<CARETISBOL>"^"		BEGIN(SECT2); return '^';


<QUOTE>{
	[^"\n]		RETURNCHAR;
	\"		BEGIN(SECT2); return '"';

	{NL}		{
			synerr( _( "missing quote" ) );
			BEGIN(SECT2);
			++linenum;
			return '"';
			}
}

<GROUP_WITH_PARAMS>{
    ":"     BEGIN(SECT2);
    "-"     BEGIN(GROUP_MINUS_PARAMS);
    i       sf_set_case_ins(1);
    s       sf_set_dot_all(1);
    x       sf_set_skip_ws(1);
}
<GROUP_MINUS_PARAMS>{
    ":"     BEGIN(SECT2);
    i       sf_set_case_ins(0);
    s       sf_set_dot_all(0);
    x       sf_set_skip_ws(0);
}

<FIRSTCCL>{
	"^"/[^-\]\n]	BEGIN(CCL); return '^';
	"^"/("-"|"]")	return '^';
	.		BEGIN(CCL); RETURNCHAR;
}

<CCL>{
	-/[^\]\n]	return '-';
	[^\]\n]		RETURNCHAR;
	"]"		BEGIN(SECT2); return ']';
	.|{NL}		{
			synerr( _( "bad character class" ) );
			BEGIN(SECT2);
			return ']';
			}
}

<FIRSTCCL,CCL>{
	"[:alnum:]"	BEGIN(CCL); return CCE_ALNUM;
	"[:alpha:]"	BEGIN(CCL); return CCE_ALPHA;
	"[:blank:]"	BEGIN(CCL); return CCE_BLANK;
	"[:cntrl:]"	BEGIN(CCL); return CCE_CNTRL;
	"[:digit:]"	BEGIN(CCL); return CCE_DIGIT;
	"[:graph:]"	BEGIN(CCL); return CCE_GRAPH;
	"[:lower:]"	BEGIN(CCL); return CCE_LOWER;
	"[:print:]"	BEGIN(CCL); return CCE_PRINT;
	"[:punct:]"	BEGIN(CCL); return CCE_PUNCT;
	"[:space:]"	BEGIN(CCL); return CCE_SPACE;
	"[:upper:]"	BEGIN(CCL); return CCE_UPPER;
	"[:xdigit:]"	BEGIN(CCL); return CCE_XDIGIT;

	"[:^alnum:]"	BEGIN(CCL); return CCE_NEG_ALNUM;
	"[:^alpha:]"	BEGIN(CCL); return CCE_NEG_ALPHA;
	"[:^blank:]"	BEGIN(CCL); return CCE_NEG_BLANK;
	"[:^cntrl:]"	BEGIN(CCL); return CCE_NEG_CNTRL;
	"[:^digit:]"	BEGIN(CCL); return CCE_NEG_DIGIT;
	"[:^graph:]"	BEGIN(CCL); return CCE_NEG_GRAPH;
	"[:^lower:]"	BEGIN(CCL); return CCE_NEG_LOWER;
	"[:^print:]"	BEGIN(CCL); return CCE_NEG_PRINT;
	"[:^punct:]"	BEGIN(CCL); return CCE_NEG_PUNCT;
	"[:^space:]"	BEGIN(CCL); return CCE_NEG_SPACE;
	"[:^upper:]"	BEGIN(CCL); return CCE_NEG_UPPER;
	"[:^xdigit:]"	BEGIN(CCL); return CCE_NEG_XDIGIT;
	{CCL_EXPR}	{
			format_synerr(
				_( "bad character class expression: %s" ),
					yytext );
			BEGIN(CCL); return CCE_ALNUM;
			}
}

<NUM>{
	[[:digit:]]+	{
			yylval = myctoi( yytext );
			return NUMBER;
			}

	","		return ',';
	"}"		{
			BEGIN(SECT2);
			if ( lex_compat || posix_compat )
				return END_REPEAT_POSIX;
			else
				return END_REPEAT_FLEX;
			}

	.		{
			synerr( _( "bad character inside {}'s" ) );
			BEGIN(SECT2);
			return '}';
			}

	{NL}		{
			synerr( _( "missing }" ) );
			BEGIN(SECT2);
			++linenum;
			return '}';
			}
}


<PERCENT_BRACE_ACTION>{
	{OPTWS}"%}".*		bracelevel = 0;

	<ACTION>"/*"		ACTION_ECHO; yy_push_state( COMMENT );

	<CODEBLOCK,ACTION>{
		"reject"	{
			ACTION_ECHO;
			CHECK_REJECT(yytext);
			}
		"yymore"	{
			ACTION_ECHO;
			CHECK_YYMORE(yytext);
			}
	}

    {M4QSTART}  ACTION_ECHO_QSTART;
    {M4QEND}    ACTION_ECHO_QEND;
    .           ACTION_ECHO;
	{NL}		{
			++linenum;
			ACTION_ECHO;
			if ( bracelevel == 0 ||
			     (doing_codeblock && indented_code) )
				{
				if ( doing_rule_action )
					add_action( "\tYY_BREAK\n" );

				doing_rule_action = doing_codeblock = false;
				BEGIN(SECT2);
				}
			}
}


	/* Reject and YYmore() are checked for above, in PERCENT_BRACE_ACTION */
<ACTION>{
	"{"		ACTION_ECHO; ++bracelevel;
	"}"		ACTION_ECHO; --bracelevel;
    {M4QSTART}  ACTION_ECHO_QSTART;
    {M4QEND}    ACTION_ECHO_QEND;
	[^[:alpha:]_{}"'/\n\[\]]+	ACTION_ECHO;
    [\[\]]      ACTION_ECHO;
	{NAME}		ACTION_ECHO;
	"'"([^'\\\n]|\\.)*"'"	ACTION_ECHO; /* character constant */
	\"		ACTION_ECHO; BEGIN(ACTION_STRING);
	{NL}		{
			++linenum;
			ACTION_ECHO;
			if ( bracelevel == 0 )
				{
				if ( doing_rule_action )
					add_action( "\tYY_BREAK\n" );

				doing_rule_action = false;
				BEGIN(SECT2);
				}
			}
	.		ACTION_ECHO;
}

<ACTION_STRING>{
	[^"\\\n]+	ACTION_ECHO;
	\\.		ACTION_ECHO;
	{NL}		++linenum; ACTION_ECHO; BEGIN(ACTION);
	\"		ACTION_ECHO; BEGIN(ACTION);
	.		ACTION_ECHO;
}

<COMMENT,COMMENT_DISCARD,ACTION,ACTION_STRING><<EOF>>	{
			synerr( _( "EOF encountered inside an action" ) );
			yyterminate();
			}

<EXTENDED_COMMENT,GROUP_WITH_PARAMS,GROUP_MINUS_PARAMS><<EOF>>	{
			synerr( _( "EOF encountered inside pattern" ) );
			yyterminate();
			}

<SECT2,QUOTE,FIRSTCCL,CCL>{ESCSEQ}	{
			yylval = myesc( (Char *) yytext );

			if ( YY_START == FIRSTCCL )
				BEGIN(CCL);

			return CHAR;
			}


<SECT3>{
    {M4QSTART}  fwrite (escaped_qstart, 1, strlen(escaped_qstart), yyout);
    {M4QEND}    fwrite (escaped_qend, 1, strlen(escaped_qend), yyout);
	[^\[\]\n]*(\n?) ECHO;
	(.|\n)      ECHO;
	<<EOF>>		sectnum = 0; yyterminate();
}

<*>.|\n			format_synerr( _( "bad character: %s" ), yytext );

%%


int yywrap()
	{
	if ( --num_input_files > 0 )
		{
		set_input_file( *++input_files );
		return 0;
		}

	else
		return 1;
	}


/* set_input_file - open the given file (if NULL, stdin) for scanning */

void set_input_file( file )
char *file;
	{
	if ( file && strcmp( file, "-" ) )
		{
		infilename = copy_string( file );
		yyin = fopen( infilename, "r" );

		if ( yyin == NULL )
			lerrsf( _( "can't open %s" ), file );
		}

	else
		{
		yyin = stdin;
		infilename = copy_string( "<stdin>" );
		}

	linenum = 1;
	}


/* Wrapper routines for accessing the scanner's malloc routines. */

void *flex_alloc( size )
size_t size;
	{
	return (void *) malloc( size );
	}

void *flex_realloc( ptr, size )
void *ptr;
size_t size;
	{
	return (void *) realloc( ptr, size );
	}

void flex_free( ptr )
void *ptr;
	{
	if ( ptr )
		free( ptr );
	}
