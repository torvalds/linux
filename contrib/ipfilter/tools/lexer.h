/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */

#ifdef	NO_YACC
#define	YY_COMMENT	1000
#define	YY_CMP_NE	1001
#define	YY_CMP_LE	1002
#define	YY_RANGE_OUT	1003
#define	YY_CMP_GE	1004
#define	YY_RANGE_IN	1005
#define	YY_HEX		1006
#define	YY_NUMBER	1007
#define	YY_IPV6		1008
#define	YY_STR		1009
#define	YY_IPADDR	1010
#endif

#define	YYBUFSIZ	8192

extern	wordtab_t	*yysettab __P((wordtab_t *));
extern	void		yysetdict __P((wordtab_t *));
extern	void		yysetfixeddict __P((wordtab_t *));
extern	int		yylex __P((void));
extern	void		yyerror __P((char *));
extern	char		*yykeytostr __P((int));
extern	void		yyresetdict __P((void));

extern	FILE	*yyin;
extern	int	yylineNum;
extern	int	yyexpectaddr;
extern	int	yybreakondot;
extern	int	yyvarnext;

