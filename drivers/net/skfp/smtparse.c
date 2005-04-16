/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	See the file "skfddi.c" for further information.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/


/*
	parser for SMT parameters
*/

#include "h/types.h"
#include "h/fddi.h"
#include "h/smc.h"
#include "h/smt_p.h"

#define KERNEL
#include "h/smtstate.h"

#ifndef	lint
static const char ID_sccs[] = "@(#)smtparse.c	1.12 98/10/06 (C) SK " ;
#endif

#ifdef	sun
#define _far
#endif

/*
 * convert to BCLK units
 */
#define MS2BCLK(x)      ((x)*12500L)
#define US2BCLK(x)      ((x/10)*125L)

/*
 * parameter table
 */
static struct s_ptab {
	char	*pt_name ;
	u_short	pt_num ;
	u_short	pt_type ;
	u_long	pt_min ;
	u_long	pt_max ;
} ptab[] = {
	{ "PMFPASSWD",0,	0 } ,
	{ "USERDATA",1,		0 } ,
	{ "LERCUTOFFA",2,	1,	4,	15	} ,
	{ "LERCUTOFFB",3,	1,	4,	15	} ,
	{ "LERALARMA",4,	1,	4,	15	} ,
	{ "LERALARMB",5,	1,	4,	15	} ,
	{ "TMAX",6,		1,	5,	165	} ,
	{ "TMIN",7,		1,	5,	165	} ,
	{ "TREQ",8,		1,	5,	165	} ,
	{ "TVX",9,		1,	2500,	10000	} ,
#ifdef ESS
	{ "SBAPAYLOAD",10,	1,	0,	1562	} ,
	{ "SBAOVERHEAD",11,	1,	50,	5000	} ,
	{ "MAXTNEG",12,		1,	5,	165	} ,
	{ "MINSEGMENTSIZE",13,	1,	0,	4478	} ,
	{ "SBACATEGORY",14,	1,	0,	0xffff	} ,
	{ "SYNCHTXMODE",15,	0 } ,
#endif
#ifdef SBA
	{ "SBACOMMAND",16,	0 } ,
	{ "SBAAVAILABLE",17,	1,	0,	100	} ,
#endif
	{ NULL }
} ;

/* Define maximum string size for values and keybuffer */
#define MAX_VAL	40

/*
 * local function declarations
 */
static u_long parse_num(int type, char _far *value, char *v, u_long mn,
			u_long mx, int scale);
static int parse_word(char *buf, char _far *text);

#ifdef SIM
#define DB_MAIN(a,b,c)	printf(a,b,c)
#else
#define DB_MAIN(a,b,c)
#endif

/*
 * BEGIN_MANUAL_ENTRY()
 *
 *	int smt_parse_arg(struct s_smc *,char _far *keyword,int type,
		char _far *value)
 *
 *	parse SMT parameter
 *	*keyword
 *		pointer to keyword, must be \0, \n or \r terminated
 *	*value	pointer to value, either char * or u_long *
 *		if char *
 *			pointer to value, must be \0, \n or \r terminated
 *		if u_long *
 *			contains binary value
 *
 *	type	0: integer
 *		1: string
 *	return
 *		0	parameter parsed ok
 *		!= 0	error
 *	NOTE:
 *		function can be called with DS != SS
 *
 *
 * END_MANUAL_ENTRY()
 */
int smt_parse_arg(struct s_smc *smc, char _far *keyword, int type,
		  char _far *value)
{
	char		keybuf[MAX_VAL+1];
	char		valbuf[MAX_VAL+1];
	char		c ;
	char 		*p ;
	char		*v ;
	char		*d ;
	u_long		val = 0 ;
	struct s_ptab	*pt ;
	int		st ;
	int		i ;

	/*
	 * parse keyword
	 */
	if ((st = parse_word(keybuf,keyword)))
		return(st) ;
	/*
	 * parse value if given as string
	 */
	if (type == 1) {
		if ((st = parse_word(valbuf,value)))
			return(st) ;
	}
	/*
	 * search in table
	 */
	st = 0 ;
	for (pt = ptab ; (v = pt->pt_name) ; pt++) {
		for (p = keybuf ; (c = *p) ; p++,v++) {
			if (c != *v)
				break ;
		}
		if (!c && !*v)
			break ;
	}
	if (!v)
		return(-1) ;
#if	0
	printf("=>%s<==>%s<=\n",pt->pt_name,valbuf) ;
#endif
	/*
	 * set value in MIB
	 */
	if (pt->pt_type)
		val = parse_num(type,value,valbuf,pt->pt_min,pt->pt_max,1) ;
	switch (pt->pt_num) {
	case 0 :
		v = valbuf ;
		d = (char *) smc->mib.fddiPRPMFPasswd ;
		for (i = 0 ; i < (signed)sizeof(smc->mib.fddiPRPMFPasswd) ; i++)
			*d++ = *v++ ;
		DB_MAIN("SET %s = %s\n",pt->pt_name,smc->mib.fddiPRPMFPasswd) ;
		break ;
	case 1 :
		v = valbuf ;
		d = (char *) smc->mib.fddiSMTUserData ;
		for (i = 0 ; i < (signed)sizeof(smc->mib.fddiSMTUserData) ; i++)
			*d++ = *v++ ;
		DB_MAIN("SET %s = %s\n",pt->pt_name,smc->mib.fddiSMTUserData) ;
		break ;
	case 2 :
		smc->mib.p[PA].fddiPORTLer_Cutoff = (u_char) val ;
		DB_MAIN("SET %s = %d\n",
			pt->pt_name,smc->mib.p[PA].fddiPORTLer_Cutoff) ;
		break ;
	case 3 :
		smc->mib.p[PB].fddiPORTLer_Cutoff = (u_char) val ;
		DB_MAIN("SET %s = %d\n",
			pt->pt_name,smc->mib.p[PB].fddiPORTLer_Cutoff) ;
		break ;
	case 4 :
		smc->mib.p[PA].fddiPORTLer_Alarm = (u_char) val ;
		DB_MAIN("SET %s = %d\n",
			pt->pt_name,smc->mib.p[PA].fddiPORTLer_Alarm) ;
		break ;
	case 5 :
		smc->mib.p[PB].fddiPORTLer_Alarm = (u_char) val ;
		DB_MAIN("SET %s = %d\n",
			pt->pt_name,smc->mib.p[PB].fddiPORTLer_Alarm) ;
		break ;
	case 6 :			/* TMAX */
		DB_MAIN("SET %s = %d\n",pt->pt_name,val) ;
		smc->mib.a[PATH0].fddiPATHT_MaxLowerBound =
			(u_long) -MS2BCLK((long)val) ;
		break ;
	case 7 :			/* TMIN */
		DB_MAIN("SET %s = %d\n",pt->pt_name,val) ;
		smc->mib.m[MAC0].fddiMACT_Min =
			(u_long) -MS2BCLK((long)val) ;
		break ;
	case 8 :			/* TREQ */
		DB_MAIN("SET %s = %d\n",pt->pt_name,val) ;
		smc->mib.a[PATH0].fddiPATHMaxT_Req =
			(u_long) -MS2BCLK((long)val) ;
		break ;
	case 9 :			/* TVX */
		DB_MAIN("SET %s = %d \n",pt->pt_name,val) ;
		smc->mib.a[PATH0].fddiPATHTVXLowerBound =
			(u_long) -US2BCLK((long)val) ;
		break ;
#ifdef	ESS
	case 10 :			/* SBAPAYLOAD */
		DB_MAIN("SET %s = %d\n",pt->pt_name,val) ;
		if (smc->mib.fddiESSPayload != val) {
			smc->ess.raf_act_timer_poll = TRUE ;
			smc->mib.fddiESSPayload = val ;
		}
		break ;
	case 11 :			/* SBAOVERHEAD */
		DB_MAIN("SET %s = %d\n",pt->pt_name,val) ;
		smc->mib.fddiESSOverhead = val ;
		break ;
	case 12 :			/* MAXTNEG */
		DB_MAIN("SET %s = %d\n",pt->pt_name,val) ;
		smc->mib.fddiESSMaxTNeg = (u_long) -MS2BCLK((long)val) ;
		break ;
	case 13 :			/* MINSEGMENTSIZE */
		DB_MAIN("SET %s = %d\n",pt->pt_name,val) ;
		smc->mib.fddiESSMinSegmentSize = val ;
		break ;
	case 14 :			/* SBACATEGORY */
		DB_MAIN("SET %s = %d\n",pt->pt_name,val) ;
		smc->mib.fddiESSCategory =
			(smc->mib.fddiESSCategory & 0xffff) |
			((u_long)(val << 16)) ;
		break ;
	case 15 :			/* SYNCHTXMODE */
		/* do not use memcmp(valbuf,"ALL",3) because DS != SS */
		if (valbuf[0] == 'A' && valbuf[1] == 'L' && valbuf[2] == 'L') {
			smc->mib.fddiESSSynchTxMode = TRUE ;
			DB_MAIN("SET %s = %s\n",pt->pt_name,valbuf) ;
		}
		/* if (!memcmp(valbuf,"SPLIT",5)) { */
		if (valbuf[0] == 'S' && valbuf[1] == 'P' && valbuf[2] == 'L' &&
			valbuf[3] == 'I' && valbuf[4] == 'T') {
			DB_MAIN("SET %s = %s\n",pt->pt_name,valbuf) ;
			smc->mib.fddiESSSynchTxMode = FALSE ;
		}
		break ;
#endif
#ifdef	SBA
	case 16 :			/* SBACOMMAND */
		/* if (!memcmp(valbuf,"START",5)) { */
		if (valbuf[0] == 'S' && valbuf[1] == 'T' && valbuf[2] == 'A' &&
			valbuf[3] == 'R' && valbuf[4] == 'T') {
			DB_MAIN("SET %s = %s\n",pt->pt_name,valbuf) ;
			smc->mib.fddiSBACommand = SB_START ;
		}
		/* if (!memcmp(valbuf,"STOP",4)) { */
		if (valbuf[0] == 'S' && valbuf[1] == 'T' && valbuf[2] == 'O' &&
			valbuf[3] == 'P') {
			DB_MAIN("SET %s = %s\n",pt->pt_name,valbuf) ;
			smc->mib.fddiSBACommand = SB_STOP ;
		}
		break ;
	case 17 :			/* SBAAVAILABLE */
		DB_MAIN("SET %s = %d\n",pt->pt_name,val) ;
		smc->mib.fddiSBAAvailable = (u_char) val ;
		break ;
#endif
	}
	return(0) ;
}

static int parse_word(char *buf, char _far *text)
{
	char		c ;
	char 		*p ;
	int		p_len ;
	int		quote ;
	int		i ;
	int		ok ;

	/*
	 * skip leading white space
	 */
	p = buf ;
	for (i = 0 ; i < MAX_VAL ; i++)
		*p++ = 0 ;
	p = buf ;
	p_len = 0 ;
	ok = 0 ;
	while ( (c = *text++) && (c != '\n') && (c != '\r')) {
		if ((c != ' ') && (c != '\t')) {
			ok = 1 ;
			break ;
		}
	}
	if (!ok)
		return(-1) ;
	if (c == '"') {
		quote = 1 ;
	}
	else {
		quote = 0 ;
		text-- ;
	}
	/*
	 * parse valbuf
	 */
	ok = 0 ;
	while (!ok && p_len < MAX_VAL-1 && (c = *text++) && (c != '\n')
		&& (c != '\r')) {
		switch (quote) {
		case 0 :
			if ((c == ' ') || (c == '\t') || (c == '=')) {
				ok = 1 ;
				break ;
			}
			*p++ = c ;
			p_len++ ;
			break ;
		case 2 :
			*p++ = c ;
			p_len++ ;
			quote = 1 ;
			break ;
		case 1 :
			switch (c) {
			case '"' :
				ok = 1 ;
				break ;
			case '\\' :
				quote = 2 ;
				break ;
			default :
				*p++ = c ;
				p_len++ ;
			}
		}
	}
	*p++ = 0 ;
	for (p = buf ; (c = *p) ; p++) {
		if (c >= 'a' && c <= 'z')
			*p = c + 'A' - 'a' ;
	}
	return(0) ;
}

static u_long parse_num(int type, char _far *value, char *v, u_long mn,
			u_long mx, int scale)
{
	u_long	x = 0 ;
	char	c ;

	if (type == 0) {		/* integer */
		u_long _far	*l ;
		u_long		u1 ;

		l = (u_long _far *) value ;
		u1 = *l ;
		/*
		 * if the value is negative take the lower limit
		 */
		if ((long)u1 < 0) {
			if (- ((long)u1) > (long) mx) {
				u1 = 0 ;
			}
			else {
				u1 = (u_long) - ((long)u1) ;
			}
		}
		x = u1 ;
	}
	else {				/* string */
		int	sign = 0 ;

		if (*v == '-') {
			sign = 1 ;
		}
		while ((c = *v++) && (c >= '0') && (c <= '9')) {
			x = x * 10 + c - '0' ;
		}
		if (scale == 10) {
			x *= 10 ;
			if (c == '.') {
				if ((c = *v++) && (c >= '0') && (c <= '9')) {
					x += c - '0' ;
				}
			}
		}
		if (sign)
			x = (u_long) - ((long)x) ;
	}
	/*
	 * if the value is negative
	 *	and the absolute value is outside the limits
	 *		take the lower limit
	 *	else
	 *		take the absoute value
	 */
	if ((long)x < 0) {
		if (- ((long)x) > (long) mx) {
			x = 0 ;
		}
		else {
			x = (u_long) - ((long)x) ;
		}
	}
	if (x < mn)
		return(mn) ;
	else if (x > mx)
		return(mx) ;
	return(x) ;
}

#if 0
struct	s_smc	SMC ;
main()
{
	char	*p ;
	char	*v ;
	char	buf[100] ;
	int	toggle = 0 ;

	while (gets(buf)) {
		p = buf ;
		while (*p && ((*p == ' ') || (*p == '\t')))
			p++ ;

		while (*p && ((*p != ' ') && (*p != '\t')))
			p++ ;

		v = p ;
		while (*v && ((*v == ' ') || (*v == '\t')))
			v++ ;
		if ((*v >= '0') && (*v <= '9')) {
			toggle = !toggle ;
			if (toggle) {
				u_long	l ;
				l = atol(v) ;
				smt_parse_arg(&SMC,buf,0,(char _far *)&l) ;
			}
			else
				smt_parse_arg(&SMC,buf,1,(char _far *)p) ;
		}
		else {
			smt_parse_arg(&SMC,buf,1,(char _far *)p) ;
		}
	}
	exit(0) ;
}
#endif

