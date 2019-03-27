%{
#include "null_gram.h"

void null_error(const char *fmt, ...) {}
int yywrap(void) { return 0; }
%}
%option nounput
%option noinput
%%
