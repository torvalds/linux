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
#endif
%}

%token <cval> GLOBAL LOCAL
%token <tval> REAL INTEGER
%token <id>   NAME

%type <nlist> declaration(<id>) namelist(<cval>, <tval>) locnamelist(<tval>)
%type <cval>  class
%type <tval>  type

%destructor	{
		  namelist *p = $$;
		  while (p != NULL)
		  { namelist *pp = p;
		    p = p->next;
		    free(pp->s); free(pp);
		  }
		} <nlist>

%union
{
    class	cval;
    type	tval;
    namelist *	nlist;
    name	id;
}

%start declaration

%%
declaration($d): class type namelist($1, $2)
	{ $$ = $3; }
	| type locnamelist($1)
	{ $$ = $2; }
	;

class	: GLOBAL { $$ = cGLOBAL; }
	| LOCAL  { $$ = cLOCAL; }
	;

type	: REAL    { $$ = tREAL; }
	| INTEGER { $$ = tINTEGER; }
	;

namelist: namelist($c) NAME
	    { $$->s = mksymbol($<tval>t, $<cval>c, $2);
	      $$->next = $1;
	    }
	| NAME
	    { $$->s = mksymbol($t, $c, $1);
	      $$->next = NULL;
	    }
	;

locnamelist($t): namelist(cLOCAL, $t)
	{ $$ = $1; }
	;
%%

extern int YYLEX_DECL();
extern void YYERROR_DECL();
