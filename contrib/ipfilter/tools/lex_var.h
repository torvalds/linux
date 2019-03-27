/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */

extern	long		string_start;
extern	long		string_end;
extern	char		*string_val;
extern	long		pos;

#define YY_INPUT(buf, result, max_size) \
	if (pos >= string_start && pos <= string_end) { \
		buf[0] = string_val[pos - string_start]; \
		pos++; \
		result = 1; \
	} else if ( yy_current_buffer->yy_is_interactive ) \
		{ \
		int c = '*', n; \
		for ( n = 0; n < 1 && \
			     (c = getc( yyin )) != EOF && c != '\n'; ++n ) \
			buf[n] = (char) c; \
		if ( c == '\n' ) \
			buf[n++] = (char) c; \
		if ( c == EOF && ferror( yyin ) ) \
			YY_FATAL_ERROR( "input in flex scanner failed" ); \
		result = n; \
		pos++; \
		} \
	else if ( ((result = fread( buf, 1, 1, yyin )) == 0) \
		  && ferror( yyin ) ) \
		YY_FATAL_ERROR( "input in flex scanner failed" );

#ifdef input
# undef input
# define input() (((pos >= string_start) && (pos < string_end)) ? \
		  yysptr = yysbuf, string_val[pos++ - string_start] : \
		  ((yytchar = yysptr > yysbuf ? U(*--yysptr) : \
		   getc(yyin)) == 10 ? (pos++, yylineno++, yytchar) : \
		  yytchar) == EOF ? (pos++, 0) : (pos++, yytchar))
#endif

#ifdef lex_input
# undef lex_input
# define lex_input() (((pos >= string_start) && (pos < string_end)) ? \
		  yysptr = yysbuf, string_val[pos++ - string_start] : \
		  ((yytchar = yysptr > yysbuf ? U(*--yysptr) : \
		   getc(yyin)) == 10 ? (pos++, yylineno++, yytchar) : \
		  yytchar) == EOF ? (pos++, 0) : (pos++, yytchar))
#endif

#ifdef unput
# undef unput
# define unput(c) { if (pos > 0) pos--; \
		    yytchar = (c); if (yytchar == '\n') yylineno--; \
		    *yysptr++ = yytchar; }
#endif

