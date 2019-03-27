%{
int yylex(void);
static void yyerror(const char *);
%}

%type <check> expr
%type <rechk> recur

%%

expr  :  '(' recur ')'
	{ foo( $$ = $0 ); }
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
