%IDENT "check variant syntax features"
%{

// http://dinosaur.compilertools.net/yacc/index.html */

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

typedef struct interval
{
    double lo, hi;
}
INTERVAL;

INTERVAL vmul(double, double, INTERVAL);
INTERVAL vdiv(double, double, INTERVAL);

extern int yylex(void);
static void yyerror(const char *s);

int dcheck(INTERVAL);

double dreg[26];
INTERVAL vreg[26];

%}
%expect 18

%start line
%union
{
	int ival;	// dreg & vreg array index values
	double dval;	// floating point values
	INTERVAL vval;	// interval values
}

%token <ival> DREG VREG		// indices into dreg, vreg arrays */
%token <dval> CONST		// floating point constant */

%type <dval> dexp		// expression */
%type <vval> vexp		// interval expression */

	// precedence information about the operators */

%< '+' '-'			// %< is an obsolete synonym for %left
%< '*' '/'
%> UMINUS			// precedence for unary minus;
				// %> is an obsolete synonym for %right

\\	// beginning of rules section; \\ is an obsolete synonym for %%

lines   : // empty */
	| lines line
	;

line	: dexp '\n'
	{
		(void) printf("%15.8f\n", $1);
	}
	| vexp '\n'
	{
		(void) printf("(%15.8f, %15.8f)\n", $1.lo, $1.hi);
	}
	| DREG '=' dexp '\n'
	{
		dreg[$1] = $3;
	}
	| VREG '=' vexp '\n'
	{
		vreg[$1] = $3;
	}
	| error '\n'
	{
		yyerrok;
	}
	;

dexp	: CONST
	| DREG
	{
		$<dval>$ = dreg[$<ival>1]; // $$ & $1 are sufficient here
	}
	| dexp '+' dexp
	{
		$$ = $1 + $3;
	}
	| dexp '-' dexp
	{
		$$ = $1 - $3;
	}
	| dexp '*' dexp
	{
		$$ = $1 * $3;
	}
	| dexp '/' dexp
	{
		$$ = $1 / $3;
	}
	| '-' dexp %prec UMINUS
	{
		$$ = -$2;
	}
	| '(' dexp ')'
	{
		$$ = $2;
	}
	;

vexp	: dexp
	{
		$$.hi = $$.lo = $1;
	}
	| '(' dexp ',' dexp ')'
	{
		$$.lo = $2;
		$$.hi = $4;
		if ( $$.lo > $$.hi ) 
		{
			(void) printf("interval out of order\n");
			YYERROR;
		}
	}
	| VREG
	{
		$$ = vreg[$1];
	}
	| vexp '+' vexp
	{
		$$.hi = $1.hi + $3.hi;
		$$.lo = $1.lo + $3.lo;
	}
	| dexp '+' vexp
	{
		$$.hi = $1 + $3.hi;
		$$.lo = $1 + $3.lo;
	}
	| vexp '-' vexp
	{
		$$.hi = $1.hi - $3.lo;
		$$.lo = $1.lo - $3.hi;
	}
	| dexp '-' vexp
	{
		$$.hi = $1 - $3.lo;
		$$.lo = $1 - $3.hi;
	}
	| vexp '*' vexp
	{
		$$ = vmul( $1.lo, $1.hi, $3 );
	}
	| dexp '*' vexp
	{
		$$ = vmul ($1, $1, $3 );
	}
	| vexp '/' vexp
	{
		if (dcheck($3)) YYERROR;
		$$ = vdiv ( $1.lo, $1.hi, $3 );
	}
	| dexp '/' vexp
	{
		if (dcheck ( $3 )) YYERROR;
		$$ = vdiv ($1, $1, $3 );
	}
	| '-' vexp %prec UMINUS
	{
		$$.hi = -$2.lo;
		$$.lo = -$2.hi;
	}
	| '(' vexp ')'
	{
		$$ = $2;
	}
	;

\\	/* beginning of subroutines section */

#define BSZ 50			/* buffer size for floating point numbers */

	/* lexical analysis */

static void
yyerror(const char *s)
{
    fprintf(stderr, "%s\n", s);
}

int
yylex(void)
{
    int c;

    while ((c = getchar()) == ' ')
    {				/* skip over blanks */
    }

    if (isupper(c))
    {
	yylval.ival = c - 'A';
	return (VREG);
    }
    if (islower(c))
    {
	yylval.ival = c - 'a';
	return (DREG);
    }

    if (isdigit(c) || c == '.')
    {
	/* gobble up digits, points, exponents */
	char buf[BSZ + 1], *cp = buf;
	int dot = 0, expr = 0;

	for (; (cp - buf) < BSZ; ++cp, c = getchar())
	{

	    *cp = (char) c;
	    if (isdigit(c))
		continue;
	    if (c == '.')
	    {
		if (dot++ || expr)
		    return ('.');	/* will cause syntax error */
		continue;
	    }

	    if (c == 'e')
	    {
		if (expr++)
		    return ('e');	/*  will  cause  syntax  error  */
		continue;
	    }

	    /*  end  of  number  */
	    break;
	}
	*cp = '\0';

	if ((cp - buf) >= BSZ)
	    printf("constant  too  long:  truncated\n");
	else
	    ungetc(c, stdin);	/*  push  back  last  char  read  */
	yylval.dval = atof(buf);
	return (CONST);
    }
    return (c);
}

static INTERVAL
hilo(double a, double b, double c, double d)
{
    /*  returns  the  smallest  interval  containing  a,  b,  c,  and  d  */
    /*  used  by  *,  /  routines  */
    INTERVAL v;

    if (a > b)
    {
	v.hi = a;
	v.lo = b;
    }
    else
    {
	v.hi = b;
	v.lo = a;
    }

    if (c > d)
    {
	if (c > v.hi)
	    v.hi = c;
	if (d < v.lo)
	    v.lo = d;
    }
    else
    {
	if (d > v.hi)
	    v.hi = d;
	if (c < v.lo)
	    v.lo = c;
    }
    return (v);
}

INTERVAL
vmul(double a, double b, INTERVAL v)
{
    return (hilo(a * v.hi, a * v.lo, b * v.hi, b * v.lo));
}

int
dcheck(INTERVAL v)
{
    if (v.hi >= 0. && v.lo <= 0.)
    {
	printf("divisor  interval  contains  0.\n");
	return (1);
    }
    return (0);
}

INTERVAL
vdiv(double a, double b, INTERVAL v)
{
    return (hilo(a / v.hi, a / v.lo, b / v.hi, b / v.lo));
}
