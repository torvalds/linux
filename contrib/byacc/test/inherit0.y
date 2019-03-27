%{
extern void mksymbol(int t, int c, int id);

#ifdef YYBISON
#define YYLEX_DECL() yylex(void)
#define YYERROR_DECL() yyerror(const char *s)
extern int YYLEX_DECL();
extern void YYERROR_DECL();
#endif
%}

%token GLOBAL LOCAL
%token REAL INTEGER
%token NAME

%start declaration

%%
declaration: class type namelist
	{ $$ = $3; }
	| type locnamelist
	{ $$ = $2; }
	;

class	: GLOBAL { $$ = 1; }
	| LOCAL  { $$ = 2; }
	;

type	: REAL    { $$ = 1; }
	| INTEGER { $$ = 2; }
	;

namelist: namelist NAME
	    { mksymbol($0, $-1, $2); }
	| NAME
	    { mksymbol($0, $-1, $1); }
	;

locnamelist:
	{ $$ = 2; }   /* set up semantic stack for <class>: LOCAL */
	{ $$ = $-1; } /* copy <type> to where <namelist> expects it */
	namelist
	{ $$ = $3; }
	;
%%

extern int YYLEX_DECL();
extern void YYERROR_DECL();
