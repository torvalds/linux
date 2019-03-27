%parse-param { struct parser_param *param } { int flag }

%{
#include <stdlib.h>

typedef enum {cGLOBAL, cLOCAL} class;
typedef enum {tREAL, tINTEGER} type;
typedef char * name;

struct symbol { class c; type t; name id; };
typedef struct symbol symbol;

struct namelist { symbol *s; struct namelist *next; };
typedef struct namelist namelist;

struct parser_param {
	int *rtrn;
	symbol ss;
};

extern symbol *mksymbol(type t, class c, name id);

#ifdef YYBISON
#define YYLEX_DECL() yylex(void)
#define YYERROR_DECL() yyerror(const char *s)
#endif
%}

%token <cval> GLOBAL LOCAL
%token <tval> REAL INTEGER
%token <id>   NAME

%type <nlist> declaration
%type <nlist> locnamelist
%type <cval>  class
%type <tval>  type
%type <nlist>  namelist

%destructor { if (!param->rtrn) close($$); } <file>

%destructor	{
		  namelist *p = $$;
		  while (p != NULL)
		  { namelist *pp = p;
		    p = p->next;
		    free(pp->s); free(pp);
		  }
		} declaration

%union
{
    class	cval;
    type	tval;
    namelist *	nlist;
    name	id;
}

%start declaration

%%
declaration: class type namelist'(' class ',' type ')'
	{ $$ = $3; }
	| type locnamelist '(' class ')'
	{ $$ = $2; }
	;

class	: GLOBAL { $$ = cGLOBAL; }
	| LOCAL  { $$ = cLOCAL; }
	;

type	: REAL    { $$ = tREAL; }
	| INTEGER { $$ = tINTEGER; }
	;

namelist: namelist NAME
	    { $$->s = mksymbol($<tval>0, $<cval>0, $2);
	      $$->next = $1;
	    }
	| NAME
	    { $$->s = mksymbol(0, 0, $1);
	      $$->next = NULL;
	    }
	;

locnamelist: namelist '(' LOCAL ',' type ')'
	{ $$ = $1; }
	;
%%

extern int YYLEX_DECL();
extern void YYERROR_DECL();
