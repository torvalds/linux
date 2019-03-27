%pure_parser

%parse_param { int regs[26] }
%parse_param { int *base }

%lex_param { int *base }

%{
# include <stdio.h>
# include <ctype.h>

#ifdef YYBISON
#define YYSTYPE int
#define YYLEX_PARAM base
#define YYLEX_DECL() yylex(YYSTYPE *yylval, int *YYLEX_PARAM)
#define YYERROR_DECL() yyerror(int regs[26], int *base, const char *s)
int YYLEX_DECL();
static void YYERROR_DECL();
#endif

%}

%start list

%token DIGIT LETTER

%token OCT1 '\177'
%token HEX1 '\xff'
%token HEX2 '\xFF'
%token HEX3 '\x7f'
%token STR1 "\x7f\177\\\n"
%token STR2 "\x7f\
\177\\\n"

%token BELL '\a'
%token BS   '\b'
%token NL   '\n'
%token LF   '\f'
%token CR   '\r'
%token TAB  '\t'
%token VT   '\v'

%union
{
    char *	cval;
    int		ival;
    double	dval;
}

%0 '@'
%2 '~'
%> '^'
%< '#'

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
            {  printf("%d\n",$<ival>1);}
      |  LETTER '=' expr
            {  regs[$<ival>1] = $<ival>3; }
      ;

expr  :  '(' expr ')'
            {  $<ival>$ = $<ival>2; }
      |  expr '+' expr
            {  $<ival>$ = $<ival>1 + $<ival>3; }
      |  expr '-' expr
            {  $<ival>$ = $<ival>1 - $<ival>3; }
      |  expr '*' expr
            {  $<ival>$ = $<ival>1 * $<ival>3; }
      |  expr '/' expr
            {  $<ival>$ = $<ival>1 / $<ival>3; }
      |  expr '%' expr
            {  $<ival>$ = $<ival>1 % $<ival>3; }
      |  expr '&' expr
            {  $<ival>$ = $<ival>1 & $<ival>3; }
      |  expr '|' expr
            {  $<ival>$ = $<ival>1 | $<ival>3; }
      |  '-' expr %prec UMINUS
            {  $<ival>$ = - $<ival>2; }
      |  LETTER
            {  $<ival>$ = regs[$<ival>1]; }
      |  number
      ;

number:  DIGIT
         {  $<ival>$ = $<ival>1; (*base) = ($<ival>1==0) ? 8 : 10; }
      |  number DIGIT
         {  $<ival>$ = (*base) * $<ival>1 + $<ival>2; }
      ;

%% /* start of programs */

#ifdef YYBYACC
extern int YYLEX_DECL();
#endif

int
main (void)
{
    int regs[26];
    int base = 10;

    while(!feof(stdin)) {
	yyparse(regs, &base);
    }
    return 0;
}

#define UNUSED(x) ((void)(x))

static void
YYERROR_DECL()
{
    UNUSED(regs); /* %parse-param regs is not actually used here */
    UNUSED(base); /* %parse-param base is not actually used here */
    fprintf(stderr, "%s\n", s);
}

int
YYLEX_DECL()
{
	/* lexical analysis routine */
	/* returns LETTER for a lower case letter, yylval = 0 through 25 */
	/* return DIGIT for a digit, yylval = 0 through 9 */
	/* all other characters are returned immediately */

    int c;

    while( (c=getchar()) == ' ' )   { /* skip blanks */ }

    /* c is now nonblank */

    if( islower( c )) {
	yylval->ival = (c - 'a');
	return ( LETTER );
    }
    if( isdigit( c )) {
	yylval->ival = (c - '0') % (*base);
	return ( DIGIT );
    }
    return( c );
}
