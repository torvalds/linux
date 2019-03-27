%{

#ifdef YYBISON
#define YYSTYPE int
#define YYLEX_PARAM &yylval
#define YYLEX_DECL() yylex(YYSTYPE *yylval)
#define YYERROR_DECL() yyerror(const char *s)
int YYLEX_DECL();
static void YYERROR_DECL();
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
yylex(YYSTYPE *value)
{
    return value ? 0 : -1;
}

static void
yyerror(const char* s)
{
    printf("%s\n", s);
}
