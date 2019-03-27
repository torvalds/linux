%{
void yyerror(const char *fmt, ...);
extern int yylex(void);
%}

%%

token:
