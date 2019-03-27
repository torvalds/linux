%{

#ifdef YYBISON
int yylex(void);
static void yyerror(const char *);
#endif

%}
%%
S: error
%%

#include <stdio.h>

#ifdef YYBYACC
extern int YYLEX_DECL();
#endif

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
