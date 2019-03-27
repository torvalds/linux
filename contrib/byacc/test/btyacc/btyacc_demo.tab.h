#ifndef _demo__defines_h_
#define _demo__defines_h_

#define PREFIX 257
#define POSTFIX 258
#define ID 259
#define CONSTANT 260
#define EXTERN 261
#define REGISTER 262
#define STATIC 263
#define CONST 264
#define VOLATILE 265
#define IF 266
#define THEN 267
#define ELSE 268
#define CLCL 269
#ifdef YYSTYPE
#undef  YYSTYPE_IS_DECLARED
#define YYSTYPE_IS_DECLARED 1
#endif
#ifndef YYSTYPE_IS_DECLARED
#define YYSTYPE_IS_DECLARED 1
typedef union {
    Scope	*scope;
    Expr	*expr;
    Expr_List	*elist;
    Type	*type;
    Decl	*decl;
    Decl_List	*dlist;
    Code	*code;
    char	*id;
    } YYSTYPE;
#endif /* !YYSTYPE_IS_DECLARED */
extern YYSTYPE demo_lval;

#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
/* Default: YYLTYPE is the text position type. */
typedef struct YYLTYPE
{
    int first_line;
    int first_column;
    int last_line;
    int last_column;
    unsigned source;
} YYLTYPE;
#define YYLTYPE_IS_DECLARED 1
#endif
#define YYRHSLOC(rhs, k) ((rhs)[k])

#endif /* _demo__defines_h_ */
