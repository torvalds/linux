%{
/*-
 * Written by Pace Willisson (pace@blitz.com)
 * and placed in the public domain.
 *
 * Largely rewritten by J.T. Conklin (jtc@wimsey.com)
 *
 * $FreeBSD$
 */

#include <sys/types.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <unistd.h>

/*
 * POSIX specifies a specific error code for syntax errors.  We exit
 * with this code for all errors.
 */
#define	ERR_EXIT	2

enum valtype {
	integer, numeric_string, string
} ;

struct val {
	enum valtype type;
	union {
		char *s;
		intmax_t i;
	} u;
} ;

char		**av;
int		nonposix;
struct val	*result;

void		assert_to_integer(struct val *);
void		assert_div(intmax_t, intmax_t);
void		assert_minus(intmax_t, intmax_t, intmax_t);
void		assert_plus(intmax_t, intmax_t, intmax_t);
void		assert_times(intmax_t, intmax_t, intmax_t);
int		compare_vals(struct val *, struct val *);
void		free_value(struct val *);
int		is_integer(const char *);
int		is_string(struct val *);
int		is_zero_or_null(struct val *);
struct val	*make_integer(intmax_t);
struct val	*make_str(const char *);
struct val	*op_and(struct val *, struct val *);
struct val	*op_colon(struct val *, struct val *);
struct val	*op_div(struct val *, struct val *);
struct val	*op_eq(struct val *, struct val *);
struct val	*op_ge(struct val *, struct val *);
struct val	*op_gt(struct val *, struct val *);
struct val	*op_le(struct val *, struct val *);
struct val	*op_lt(struct val *, struct val *);
struct val	*op_minus(struct val *, struct val *);
struct val	*op_ne(struct val *, struct val *);
struct val	*op_or(struct val *, struct val *);
struct val	*op_plus(struct val *, struct val *);
struct val	*op_rem(struct val *, struct val *);
struct val	*op_times(struct val *, struct val *);
int		to_integer(struct val *);
void		to_string(struct val *);
int		yyerror(const char *);
int		yylex(void);

%}

%union
{
	struct val *val;
}

%left <val> '|'
%left <val> '&'
%left <val> '=' '>' '<' GE LE NE
%left <val> '+' '-'
%left <val> '*' '/' '%'
%left <val> ':'

%token <val> TOKEN
%type <val> start expr

%%

start: expr { result = $$; }

expr:	TOKEN
	| '(' expr ')' { $$ = $2; }
	| expr '|' expr { $$ = op_or($1, $3); }
	| expr '&' expr { $$ = op_and($1, $3); }
	| expr '=' expr { $$ = op_eq($1, $3); }
	| expr '>' expr { $$ = op_gt($1, $3); }
	| expr '<' expr { $$ = op_lt($1, $3); }
	| expr GE expr  { $$ = op_ge($1, $3); }
	| expr LE expr  { $$ = op_le($1, $3); }
	| expr NE expr  { $$ = op_ne($1, $3); }
	| expr '+' expr { $$ = op_plus($1, $3); }
	| expr '-' expr { $$ = op_minus($1, $3); }
	| expr '*' expr { $$ = op_times($1, $3); }
	| expr '/' expr { $$ = op_div($1, $3); }
	| expr '%' expr { $$ = op_rem($1, $3); }
	| expr ':' expr { $$ = op_colon($1, $3); }
	;

%%

struct val *
make_integer(intmax_t i)
{
	struct val *vp;

	vp = (struct val *)malloc(sizeof(*vp));
	if (vp == NULL)
		errx(ERR_EXIT, "malloc() failed");

	vp->type = integer;
	vp->u.i  = i;
	return (vp);
}

struct val *
make_str(const char *s)
{
	struct val *vp;

	vp = (struct val *)malloc(sizeof(*vp));
	if (vp == NULL || ((vp->u.s = strdup(s)) == NULL))
		errx(ERR_EXIT, "malloc() failed");

	if (is_integer(s))
		vp->type = numeric_string;
	else
		vp->type = string;

	return (vp);
}

void
free_value(struct val *vp)
{
	if (vp->type == string || vp->type == numeric_string)
		free(vp->u.s);
}

int
to_integer(struct val *vp)
{
	intmax_t i;

	/* we can only convert numeric_string to integer, here */
	if (vp->type == numeric_string) {
		errno = 0;
		i  = strtoimax(vp->u.s, (char **)NULL, 10);
		/* just keep as numeric_string, if the conversion fails */
		if (errno != ERANGE) {
			free(vp->u.s);
			vp->u.i = i;
			vp->type = integer;
		}
	}
	return (vp->type == integer);
}

void
assert_to_integer(struct val *vp)
{
	if (vp->type == string)
		errx(ERR_EXIT, "not a decimal number: '%s'", vp->u.s);
	if (!to_integer(vp))
		errx(ERR_EXIT, "operand too large: '%s'", vp->u.s);
}

void
to_string(struct val *vp)
{
	char *tmp;

	if (vp->type == string || vp->type == numeric_string)
		return;

	/*
	 * log_10(x) ~= 0.3 * log_2(x).  Rounding up gives the number
	 * of digits; add one each for the sign and terminating null
	 * character, respectively.
	 */
#define	NDIGITS(x) (3 * (sizeof(x) * CHAR_BIT) / 10 + 1 + 1 + 1)
	tmp = malloc(NDIGITS(vp->u.i));
	if (tmp == NULL)
		errx(ERR_EXIT, "malloc() failed");

	sprintf(tmp, "%jd", vp->u.i);
	vp->type = string;
	vp->u.s  = tmp;
}

int
is_integer(const char *s)
{
	if (nonposix) {
		if (*s == '\0')
			return (1);
		while (isspace((unsigned char)*s))
			s++;
	}
	if (*s == '-' || (nonposix && *s == '+'))
		s++;
	if (*s == '\0')
		return (0);
	while (isdigit((unsigned char)*s))
		s++;
	return (*s == '\0');
}

int
is_string(struct val *vp)
{
	/* only TRUE if this string is not a valid integer */
	return (vp->type == string);
}

int
yylex(void)
{
	char *p;

	if (*av == NULL)
		return (0);

	p = *av++;

	if (strlen(p) == 1) {
		if (strchr("|&=<>+-*/%:()", *p))
			return (*p);
	} else if (strlen(p) == 2 && p[1] == '=') {
		switch (*p) {
		case '>': return (GE);
		case '<': return (LE);
		case '!': return (NE);
		}
	}

	yylval.val = make_str(p);
	return (TOKEN);
}

int
is_zero_or_null(struct val *vp)
{
	if (vp->type == integer)
		return (vp->u.i == 0);

	return (*vp->u.s == 0 || (to_integer(vp) && vp->u.i == 0));
}

int
main(int argc, char *argv[])
{
	int c;

	setlocale(LC_ALL, "");
	if (getenv("EXPR_COMPAT") != NULL
	    || check_utility_compat("expr")) {
		av = argv + 1;
		nonposix = 1;
	} else {
		while ((c = getopt(argc, argv, "e")) != -1) {
			switch (c) {
			case 'e':
				nonposix = 1;
				break;
			default:
				errx(ERR_EXIT,
				    "usage: expr [-e] expression\n");
			}
		}
		av = argv + optind;
	}

	yyparse();

	if (result->type == integer)
		printf("%jd\n", result->u.i);
	else
		printf("%s\n", result->u.s);

	return (is_zero_or_null(result));
}

int
yyerror(const char *s __unused)
{
	errx(ERR_EXIT, "syntax error");
}

struct val *
op_or(struct val *a, struct val *b)
{
	if (!is_zero_or_null(a)) {
		free_value(b);
		return (a);
	}
	free_value(a);
	if (!is_zero_or_null(b))
		return (b);
	free_value(b);
	return (make_integer((intmax_t)0));
}

struct val *
op_and(struct val *a, struct val *b)
{
	if (is_zero_or_null(a) || is_zero_or_null(b)) {
		free_value(a);
		free_value(b);
		return (make_integer((intmax_t)0));
	} else {
		free_value(b);
		return (a);
	}
}

int
compare_vals(struct val *a, struct val *b)
{
	int r;

	if (is_string(a) || is_string(b)) {
		to_string(a);
		to_string(b);
		r = strcoll(a->u.s, b->u.s);
	} else {
		assert_to_integer(a);
		assert_to_integer(b);
		if (a->u.i > b->u.i)
			r = 1;
		else if (a->u.i < b->u.i)
			r = -1;
		else
			r = 0;
	}

	free_value(a);
	free_value(b);
	return (r);
}

struct val *
op_eq(struct val *a, struct val *b)
{
	return (make_integer((intmax_t)(compare_vals(a, b) == 0)));
}

struct val *
op_gt(struct val *a, struct val *b)
{
	return (make_integer((intmax_t)(compare_vals(a, b) > 0)));
}

struct val *
op_lt(struct val *a, struct val *b)
{
	return (make_integer((intmax_t)(compare_vals(a, b) < 0)));
}

struct val *
op_ge(struct val *a, struct val *b)
{
	return (make_integer((intmax_t)(compare_vals(a, b) >= 0)));
}

struct val *
op_le(struct val *a, struct val *b)
{
	return (make_integer((intmax_t)(compare_vals(a, b) <= 0)));
}

struct val *
op_ne(struct val *a, struct val *b)
{
	return (make_integer((intmax_t)(compare_vals(a, b) != 0)));
}

void
assert_plus(intmax_t a, intmax_t b, intmax_t r)
{
	/*
	 * sum of two positive numbers must be positive,
	 * sum of two negative numbers must be negative
	 */
	if ((a > 0 && b > 0 && r <= 0) ||
	    (a < 0 && b < 0 && r >= 0))
		errx(ERR_EXIT, "overflow");
}

struct val *
op_plus(struct val *a, struct val *b)
{
	struct val *r;

	assert_to_integer(a);
	assert_to_integer(b);
	r = make_integer(a->u.i + b->u.i);
	assert_plus(a->u.i, b->u.i, r->u.i);

	free_value(a);
	free_value(b);
	return (r);
}

void
assert_minus(intmax_t a, intmax_t b, intmax_t r)
{
	if ((a >= 0 && b < 0 && r <= 0) ||
	    (a < 0 && b > 0 && r >= 0))
		errx(ERR_EXIT, "overflow");
}

struct val *
op_minus(struct val *a, struct val *b)
{
	struct val *r;

	assert_to_integer(a);
	assert_to_integer(b);
	r = make_integer(a->u.i - b->u.i);
	assert_minus(a->u.i, b->u.i, r->u.i);

	free_value(a);
	free_value(b);
	return (r);
}

/*
 * We depend on undefined behaviour giving a result (in r).
 * To test this result, pass it as volatile.  This prevents
 * optimizing away of the test based on the undefined behaviour.
 */
void
assert_times(intmax_t a, intmax_t b, volatile intmax_t r)
{
	/*
	 * If the first operand is 0, no overflow is possible, 
	 * else the result of the division test must match the
	 * second operand.
	 *
	 * Be careful to avoid overflow in the overflow test, as
	 * in assert_div().  Overflow in division would kill us
	 * with a SIGFPE before getting the test wrong.  In old
	 * buggy versions, optimization used to give a null test
	 * instead of a SIGFPE.
	 */
	if ((a == -1 && b == INTMAX_MIN) || (a != 0 && r / a != b))
		errx(ERR_EXIT, "overflow");
}

struct val *
op_times(struct val *a, struct val *b)
{
	struct val *r;

	assert_to_integer(a);
	assert_to_integer(b);
	r = make_integer(a->u.i * b->u.i);
	assert_times(a->u.i, b->u.i, r->u.i);

	free_value(a);
	free_value(b);
	return (r);
}

void
assert_div(intmax_t a, intmax_t b)
{
	if (b == 0)
		errx(ERR_EXIT, "division by zero");
	/* only INTMAX_MIN / -1 causes overflow */
	if (a == INTMAX_MIN && b == -1)
		errx(ERR_EXIT, "overflow");
}

struct val *
op_div(struct val *a, struct val *b)
{
	struct val *r;

	assert_to_integer(a);
	assert_to_integer(b);
	/* assert based on operands only, not on result */
	assert_div(a->u.i, b->u.i);
	r = make_integer(a->u.i / b->u.i);

	free_value(a);
	free_value(b);
	return (r);
}

struct val *
op_rem(struct val *a, struct val *b)
{
	struct val *r;

	assert_to_integer(a);
	assert_to_integer(b);
	/* pass a=1 to only check for div by zero */
	assert_div(1, b->u.i);
	r = make_integer(a->u.i % b->u.i);

	free_value(a);
	free_value(b);
	return (r);
}

struct val *
op_colon(struct val *a, struct val *b)
{
	regex_t rp;
	regmatch_t rm[2];
	char errbuf[256];
	int eval;
	struct val *v;

	/* coerce both arguments to strings */
	to_string(a);
	to_string(b);

	/* compile regular expression */
	if ((eval = regcomp(&rp, b->u.s, 0)) != 0) {
		regerror(eval, &rp, errbuf, sizeof(errbuf));
		errx(ERR_EXIT, "%s", errbuf);
	}

	/* compare string against pattern */
	/* remember that patterns are anchored to the beginning of the line */
	if (regexec(&rp, a->u.s, (size_t)2, rm, 0) == 0 && rm[0].rm_so == 0)
		if (rm[1].rm_so >= 0) {
			*(a->u.s + rm[1].rm_eo) = '\0';
			v = make_str(a->u.s + rm[1].rm_so);

		} else
			v = make_integer((intmax_t)(rm[0].rm_eo));
	else
		if (rp.re_nsub == 0)
			v = make_integer((intmax_t)0);
		else
			v = make_str("");

	/* free arguments and pattern buffer */
	free_value(a);
	free_value(b);
	regfree(&rp);

	return (v);
}
