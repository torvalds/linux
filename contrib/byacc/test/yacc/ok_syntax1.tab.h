#define DIGIT 257
#define LETTER 258
#define OCT1 259
#define HEX1 260
#define HEX2 261
#define HEX3 262
#define STR1 263
#define STR2 265
#define BELL 266
#define BS 267
#define NL 268
#define LF 269
#define CR 270
#define TAB 271
#define VT 272
#define UMINUS 273
#ifdef YYSTYPE
#undef  YYSTYPE_IS_DECLARED
#define YYSTYPE_IS_DECLARED 1
#endif
#ifndef YYSTYPE_IS_DECLARED
#define YYSTYPE_IS_DECLARED 1
typedef union
{
    char *	cval;
    int		ival;
    double	dval;
} YYSTYPE;
#endif /* !YYSTYPE_IS_DECLARED */
extern YYSTYPE ok_syntax1_lval;
