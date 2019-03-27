/* ntp_scanner.h
 *
 * The header file for a simple lexical analyzer. 
 *
 * Written By:	Sachin Kamboj
 *		University of Delaware
 *		Newark, DE 19711
 * Copyright (c) 2006
 */

#ifndef NTP_SCANNER_H
#define NTP_SCANNER_H

#include "ntp_config.h"

/*
 * ntp.conf syntax is slightly irregular in that some tokens such as
 * hostnames do not require quoting even if they might otherwise be
 * recognized as T_ terminal tokens.  This hand-crafted lexical scanner
 * uses a "followed by" value associated with each keyword to indicate
 * normal scanning of the next token, forced scanning of the next token
 * alone as a T_String, or forced scanning of all tokens to the end of
 * the command as T_String.
 * In the past the identifiers for this functionality ended in _ARG:
 *
 * NO_ARG	->	FOLLBY_TOKEN
 * SINGLE_ARG	->	FOLLBY_STRING
 * MULTIPLE_ARG	->	FOLLBY_STRINGS_TO_EOC
 *
 * Note that some tokens use FOLLBY_TOKEN even though they sometimes
 * are followed by strings.  FOLLBY_STRING is used only when needed to
 * avoid the keyword scanner matching a token where a string is needed.
 *
 * FOLLBY_NON_ACCEPT is an overloading of this field to distinguish
 * non-accepting states (where the state number does not match a T_
 * value).
 */
typedef enum {
	FOLLBY_TOKEN = 0,
	FOLLBY_STRING,
	FOLLBY_STRINGS_TO_EOC,
	FOLLBY_NON_ACCEPTING
} follby;

#define MAXLINE		1024	/* maximum length of line */
#define MAXINCLUDELEVEL	5	/* maximum include file levels */

/* STRUCTURES
 * ----------
 */

/* 
 * Define a structure to hold the FSA for the keywords.
 * The structure is actually a trie.
 *
 * To save space, a single u_int32 encodes four fields, and a fifth
 * (the token completed for terminal states) is implied by the index of
 * the rule within the scan state array, taking advantage of the fact
 * there are more scan states than the highest T_ token number.
 *
 * The lowest 8 bits hold the character the state matches on.
 * Bits 8 and 9 hold the followedby value (0 - 3).  For non-accepting
 *   states (which do not match a completed token) the followedby
 *   value 3 (FOLLBY_NONACCEPTING) denotes that fact.  For accepting
 *   states, values 0 - 2 control whether the scanner forces the
 *   following token(s) to strings.
 * Bits 10 through 20 hold the next state to check not matching
 * this state's character.
 * Bits 21 through 31 hold the next state to check matching the char.
 */

#define S_ST(ch, fb, match_n, other_n) (			\
	(u_char)((ch) & 0xff) |					\
	((u_int32)(fb) << 8) |					\
	((u_int32)(match_n) << 10) |				\
	((u_int32)(other_n) << 21)				\
)

#define SS_CH(ss)	((char)(u_char)((ss) & 0xff))
#define SS_FB(ss)	(((u_int)(ss) >>  8) & 0x3)
#define SS_MATCH_N(ss)	(((u_int)(ss) >> 10) & 0x7ff)
#define SS_OTHER_N(ss)	(((u_int)(ss) >> 21) & 0x7ff)

typedef u_int32 scan_state;

struct LCPOS {
	int nline;
	int ncol;
};

/* Structure to hold a filename, file pointer and positional info.
 * Instances are dynamically allocated, and the file name is copied by
 * value into a dynamic extension of the 'fname' array. (Which *must* be
 * the last field for that reason!)
 */
struct FILE_INFO {
	struct FILE_INFO * st_next;	/* next on stack */
	FILE *		   fpi;		/* File Descriptor */
	int                force_eof;	/* locked or not */
	int                backch;	/* ungetch buffer */
	
	struct LCPOS       curpos;	/* current scan position */
	struct LCPOS       bakpos;	/* last line end for ungetc */
	struct LCPOS       tokpos;	/* current token position */
	struct LCPOS       errpos;	/* error position */

	char               fname[1];	/* (formal only) buffered name */
};


/* SCANNER GLOBAL VARIABLES 
 * ------------------------
 */
extern config_tree cfgt;	  /* Parser output stored here */

/* VARIOUS EXTERNAL DECLARATIONS
 * -----------------------------
 */
extern int old_config_style;

/* VARIOUS SUBROUTINE DECLARATIONS
 * -------------------------------
 */
extern const char *keyword(int token);
extern char *quote_if_needed(char *str);
int yylex(void);

/* managing the input source stack itself */
extern int/*BOOL*/ lex_init_stack(const char * path, const char * mode);
extern void        lex_drop_stack(void);
extern int/*BOOL*/ lex_flush_stack(void);

/* add/remove a nested input source */
extern int/*BOOL*/ lex_push_file(const char * path, const char * mode);
extern int/*BOOL*/ lex_pop_file(void);

/* input stack state query functions */
extern size_t      lex_level(void);
extern int/*BOOL*/ lex_from_file(void);
extern struct FILE_INFO * lex_current(void);

#endif	/* NTP_SCANNER_H */
