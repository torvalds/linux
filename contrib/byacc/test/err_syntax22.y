%{
int yylex(void);
static void yyerror(const char *);
%}

%union {
	int ival;
	double dval;
}

%token NUMBER
%type <dval> expr

%%

expr  :  '(' recur ')'
	{ foo( $$ = $2 ); }
      ;

recur :  NUMBER
      ;

%%

#include <stdio.h>

int
main(void)
{
    printf("yyparse() = %d\n", yyparse());
    return 0;
}

int
yylex(void)
{
    return -1;
}

static void
yyerror(const char* s)
{
    printf("%s\n", s);
}
