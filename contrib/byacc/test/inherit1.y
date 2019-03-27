%{
#include <stdlib.h>

typedef enum {cGLOBAL, cLOCAL} class;
typedef enum {tREAL, tINTEGER} type;
typedef char * name;

struct symbol { class c; type t; name id; };
typedef struct symbol symbol;

struct namelist { symbol *s; struct namelist *next; };
typedef struct namelist namelist;

extern symbol *mksymbol(type t, class c, name id);

#ifdef YYBISON
#define YYLEX_DECL() yylex(void)
#define YYERROR_DECL() yyerror(const char *s)
extern int YYLEX_DECL();
extern void YYERROR_DECL();
#endif
%}

%token <cval> GLOBAL LOCAL
%token <tval> REAL INTEGER
%token <id>   NAME

%type <nlist> declaration namelist locnamelist
%type <cval>  class
%type <tval>  type

%union
{
    class	cval;
    type	tval;
    namelist *	nlist;
    name	id;
}

%start declaration

%%
declaration: class type namelist
	{ $$ = $3; }
	| type locnamelist
	{ $$ = $2; }
	;

class	: GLOBAL { $$ = cGLOBAL; }
	| LOCAL  { $$ = cLOCAL; }
	;

type	: REAL    { $$ = tREAL; }
	| INTEGER { $$ = tINTEGER; }
	;

namelist: namelist NAME
	    { $$->s = mksymbol($<tval>0, $<cval>-1, $2);
	      $$->next = $1;
	    }
	| NAME
	    { $$->s = mksymbol($<tval>0, $<cval>-1, $1);
	      $$->next = NULL;
	    }
	;

locnamelist:
	{ $<cval>$ = cLOCAL; }    /* set up semantic stack for <class> = LOCAL */
	{ $<tval>$ = $<tval>-1; } /* copy <type> to where <namelist> expects it */
	namelist
	{ $$ = $3; }
	;
%%

extern int YYLEX_DECL();
extern void YYERROR_DECL();
