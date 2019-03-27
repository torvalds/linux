%{
# include <stdio.h>
# include <ctype.h>

int regs[26];
int base;

int yylex(void);
static void yyerror(const char *s);

%}

%start list

%token OP_ADD "ADD"
%token OP_SUB "SUB"
%token OP_MUL "MUL"
%token OP_DIV "DIV"
%token OP_MOD "MOD"
%token OP_AND "AND"

%token DIGIT LETTER

%left '|'
%left '&'
%left '+' '-'
%left '*' '/' '%'
%left UMINUS   /* supplies precedence for unary minus */

%% /* beginning of rules section */

list  :  /* empty */
      |  list stat '\n'
      |  list error '\n'
            {  yyerrok ; }
      ;

stat  :  expr
            {  printf("%d\n",$1);}
      |  LETTER '=' expr
            {  regs[$1] = $3; }
      ;

expr  :  '(' expr ')'
            {  $$ = $2; }
      |  expr OP_ADD expr
            {  $$ = $1 + $3; }
      |  expr OP_SUB expr
            {  $$ = $1 - $3; }
      |  expr OP_MUL expr
            {  $$ = $1 * $3; }
      |  expr OP_DIV expr
            {  $$ = $1 / $3; }
      |  expr OP_MOD expr
            {  $$ = $1 % $3; }
      |  expr OP_AND expr
            {  $$ = $1 & $3; }
      |  expr '|' expr
            {  $$ = $1 | $3; }
      |  OP_SUB expr %prec UMINUS
            {  $$ = - $2; }
      |  LETTER
            {  $$ = regs[$1]; }
      |  number
      ;

number:  DIGIT
         {  $$ = $1; base = ($1==0) ? 8 : 10; }
      |  number DIGIT
         {  $$ = base * $1 + $2; }
      ;

%% /* start of programs */

int
main (void)
{
    while(!feof(stdin)) {
	yyparse();
    }
    return 0;
}

static void
yyerror(const char *s)
{
    fprintf(stderr, "%s\n", s);
}

int
yylex(void) {
	/* lexical analysis routine */
	/* returns LETTER for a lower case letter, yylval = 0 through 25 */
	/* return DIGIT for a digit, yylval = 0 through 9 */
	/* all other characters are returned immediately */

    int c;

    while( (c=getchar()) == ' ' )   { /* skip blanks */ }

    /* c is now nonblank */

    if( islower( c )) {
	yylval = c - 'a';
	return ( LETTER );
    }
    if( isdigit( c )) {
	yylval = c - '0';
	return ( DIGIT );
    }
    return( c );
}
