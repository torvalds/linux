/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */
#if !defined(lint)
static const char sccsid[] = "@(#)ip_fil.c	2.41 6/5/96 (C) 1993-2000 Darren Reed";
static const char rcsid[] = "@(#)$Id$";
#endif

#include "ipf.h"


typedef struct {
	int c;
	int e;
	int n;
	int p;
	int s;
} mc_t;


static char *portcmp[] = { "*", "==", "!=", "<", ">", "<=", ">=", "**", "***" };
static int count = 0;

int intcmp __P((const void *, const void *));
static void indent __P((FILE *, int));
static void printeq __P((FILE *, char *, int, int, int));
static void printipeq __P((FILE *, char *, int, int, int));
static void addrule __P((FILE *, frentry_t *));
static void printhooks __P((FILE *, int, int, frgroup_t *));
static void emitheader __P((frgroup_t *, u_int, u_int));
static void emitGroup __P((int, int, void *, frentry_t *, char *,
			   u_int, u_int));
static void emittail __P((void));
static void printCgroup __P((int, frentry_t *, mc_t *, char *));

#define	FRC_IFN	0
#define	FRC_V	1
#define	FRC_P	2
#define	FRC_FL	3
#define	FRC_TOS	4
#define	FRC_TTL	5
#define	FRC_SRC	6
#define	FRC_DST	7
#define	FRC_TCP	8
#define	FRC_SP	9
#define	FRC_DP	10
#define	FRC_OPT	11
#define	FRC_SEC	12
#define	FRC_ATH	13
#define	FRC_ICT	14
#define	FRC_ICC	15
#define	FRC_MAX	16


static	FILE	*cfile = NULL;

/*
 * This is called once per filter rule being loaded to emit data structures
 * required.
 */
void printc(fr)
	frentry_t *fr;
{
	fripf_t *ipf;
	u_long *ulp;
	char *and;
	FILE *fp;
	int i;

	if (fr->fr_family == 6)
		return;
	if ((fr->fr_type != FR_T_IPF) && (fr->fr_type != FR_T_NONE))
		return;
	if ((fr->fr_type == FR_T_IPF) &&
	    ((fr->fr_datype != FRI_NORMAL) || (fr->fr_satype != FRI_NORMAL)))
		return;
	ipf = fr->fr_ipf;

	if (cfile == NULL)
		cfile = fopen("ip_rules.c", "w");
	if (cfile == NULL)
		return;
	fp = cfile;
	if (count == 0) {
		fprintf(fp, "/*\n");
 		fprintf(fp, "* Copyright (C) 2012 by Darren Reed.\n");
 		fprintf(fp, "*\n");
 		fprintf(fp, "* Redistribution and use in source and binary forms are permitted\n");
 		fprintf(fp, "* provided that this notice is preserved and due credit is given\n");
 		fprintf(fp, "* to the original author and the contributors.\n");
 		fprintf(fp, "*/\n\n");

		fprintf(fp, "#include <sys/param.h>\n");
		fprintf(fp, "#include <sys/types.h>\n");
		fprintf(fp, "#include <sys/time.h>\n");
		fprintf(fp, "#include <sys/socket.h>\n");
		fprintf(fp, "#if (__FreeBSD_version >= 40000)\n");
		fprintf(fp, "# if defined(_KERNEL)\n");
		fprintf(fp, "#  include <sys/libkern.h>\n");
		fprintf(fp, "# else\n");
		fprintf(fp, "#  include <sys/unistd.h>\n");
		fprintf(fp, "# endif\n");
		fprintf(fp, "#endif\n");
		fprintf(fp, "#if (__NetBSD_Version__ >= 399000000)\n");
		fprintf(fp, "#else\n");
		fprintf(fp, "# if !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__sgi)\n");
		fprintf(fp, "#  include <sys/systm.h>\n");
		fprintf(fp, "# endif\n");
		fprintf(fp, "#endif\n");
		fprintf(fp, "#include <sys/errno.h>\n");
		fprintf(fp, "#include <sys/param.h>\n");
		fprintf(fp,
"#if !defined(__SVR4) && !defined(__svr4__) && !defined(__hpux)\n");
		fprintf(fp, "# include <sys/mbuf.h>\n");
		fprintf(fp, "#endif\n");
		fprintf(fp,
"#if defined(__FreeBSD__) && (__FreeBSD_version > 220000)\n");
		fprintf(fp, "# include <sys/sockio.h>\n");
		fprintf(fp, "#else\n");
		fprintf(fp, "# include <sys/ioctl.h>\n");
		fprintf(fp, "#endif /* FreeBSD */\n");
		fprintf(fp, "#include <net/if.h>\n");
		fprintf(fp, "#include <netinet/in.h>\n");
		fprintf(fp, "#include <netinet/in_systm.h>\n");
		fprintf(fp, "#include <netinet/ip.h>\n");
		fprintf(fp, "#include <netinet/tcp.h>\n");
		fprintf(fp, "#include \"netinet/ip_compat.h\"\n");
		fprintf(fp, "#include \"netinet/ip_fil.h\"\n\n");
		fprintf(fp, "#include \"netinet/ip_rules.h\"\n\n");
		fprintf(fp, "#ifndef _KERNEL\n");
		fprintf(fp, "# include <string.h>\n");
		fprintf(fp, "#endif /* _KERNEL */\n");
		fprintf(fp, "\n");
		fprintf(fp, "#ifdef IPFILTER_COMPILED\n");
		fprintf(fp, "\n");
		fprintf(fp, "extern ipf_main_softc_t ipfmain;\n");
		fprintf(fp, "\n");
	}

	addrule(fp, fr);
	fr->fr_type |= FR_T_BUILTIN;
	and = "";
	fr->fr_ref = 1;
	i = sizeof(*fr);
	if (i & -(1 - sizeof(*ulp)))
		i += sizeof(u_long);
	for (i /= sizeof(u_long), ulp = (u_long *)fr; i > 0; i--) {
		fprintf(fp, "%s%#lx", and, *ulp++);
		and = ", ";
	}
	fprintf(fp, "\n};\n");
	fr->fr_type &= ~FR_T_BUILTIN;

	count++;

	fflush(fp);
}


static frgroup_t *groups = NULL;


static void addrule(fp, fr)
	FILE *fp;
	frentry_t *fr;
{
	frentry_t *f, **fpp;
	frgroup_t *g;
	u_long *ulp;
	char *ghead;
	char *gname;
	char *and;
	int i;

	f = (frentry_t *)malloc(sizeof(*f));
	bcopy((char *)fr, (char *)f, sizeof(*fr));
	if (fr->fr_ipf) {
		f->fr_ipf = (fripf_t *)malloc(sizeof(*f->fr_ipf));
		bcopy((char *)fr->fr_ipf, (char *)f->fr_ipf,
		      sizeof(*fr->fr_ipf));
	}

	f->fr_next = NULL;
	gname = FR_NAME(fr, fr_group);

	for (g = groups; g != NULL; g = g->fg_next)
		if ((strncmp(g->fg_name, gname, FR_GROUPLEN) == 0) &&
		    (g->fg_flags == (f->fr_flags & FR_INOUT)))
			break;

	if (g == NULL) {
		g = (frgroup_t *)calloc(1, sizeof(*g));
		g->fg_next = groups;
		groups = g;
		g->fg_head = f;
		strncpy(g->fg_name, gname, FR_GROUPLEN);
		g->fg_ref = 0;
		g->fg_flags = f->fr_flags & FR_INOUT;
	}

	for (fpp = &g->fg_start; *fpp != NULL; )
		fpp = &((*fpp)->fr_next);
	*fpp = f;

	if (fr->fr_dsize > 0) {
		fprintf(fp, "\
static u_long ipf%s_rule_data_%s_%u[] = {\n",
			f->fr_flags & FR_INQUE ? "in" : "out",
			g->fg_name, g->fg_ref);
		and = "";
		i = fr->fr_dsize;
		ulp = fr->fr_data;
		for (i /= sizeof(u_long); i > 0; i--) {
			fprintf(fp, "%s%#lx", and, *ulp++);
			and = ", ";
		}
		fprintf(fp, "\n};\n");
	}

	fprintf(fp, "\nstatic u_long %s_rule_%s_%d[] = {\n",
		f->fr_flags & FR_INQUE ? "in" : "out", g->fg_name, g->fg_ref);

	g->fg_ref++;

	if (f->fr_grhead != -1) {
		ghead = FR_NAME(f, fr_grhead);
		for (g = groups; g != NULL; g = g->fg_next)
			if ((strncmp(g->fg_name, ghead, FR_GROUPLEN) == 0) &&
			    g->fg_flags == (f->fr_flags & FR_INOUT))
				break;
		if (g == NULL) {
			g = (frgroup_t *)calloc(1, sizeof(*g));
			g->fg_next = groups;
			groups = g;
			g->fg_head = f;
			strncpy(g->fg_name, ghead, FR_GROUPLEN);
			g->fg_ref = 0;
			g->fg_flags = f->fr_flags & FR_INOUT;
		}
	}
}


int intcmp(c1, c2)
	const void *c1, *c2;
{
	const mc_t *i1 = (const mc_t *)c1, *i2 = (const mc_t *)c2;

	if (i1->n == i2->n) {
		return i1->c - i2->c;
	}
	return i2->n - i1->n;
}


static void indent(fp, in)
	FILE *fp;
	int in;
{
	for (; in; in--)
		fputc('\t', fp);
}

static void printeq(fp, var, m, max, v)
	FILE *fp;
	char *var;
	int m, max, v;
{
	if (m == max)
		fprintf(fp, "%s == %#x) {\n", var, v);
	else
		fprintf(fp, "(%s & %#x) == %#x) {\n", var, m, v);
}

/*
 * Parameters: var - IP# being compared
 *             fl - 0 for positive match, 1 for negative match
 *             m - netmask
 *             v - required address
 */
static void printipeq(fp, var, fl, m, v)
	FILE *fp;
	char *var;
	int fl, m, v;
{
	if (m == 0xffffffff)
		fprintf(fp, "%s ", var);
	else
		fprintf(fp, "(%s & %#x) ", var, m);
	fprintf(fp, "%c", fl ? '!' : '=');
	fprintf(fp, "= %#x) {\n", v);
}


void emit(num, dir, v, fr)
	int num, dir;
	void *v;
	frentry_t *fr;
{
	u_int incnt, outcnt;
	frgroup_t *g;
	frentry_t *f;

	for (g = groups; g != NULL; g = g->fg_next) {
		if (dir == 0 || dir == -1) {
			if ((g->fg_flags & FR_INQUE) == 0)
				continue;
			for (incnt = 0, f = g->fg_start; f != NULL;
			     f = f->fr_next)
				incnt++;
			emitGroup(num, dir, v, fr, g->fg_name, incnt, 0);
		}
		if (dir == 1 || dir == -1) {
			if ((g->fg_flags & FR_OUTQUE) == 0)
				continue;
			for (outcnt = 0, f = g->fg_start; f != NULL;
			     f = f->fr_next)
				outcnt++;
			emitGroup(num, dir, v, fr, g->fg_name, 0, outcnt);
		}
	}

	if (num == -1 && dir == -1) {
		for (g = groups; g != NULL; g = g->fg_next) {
			if ((g->fg_flags & FR_INQUE) != 0) {
				for (incnt = 0, f = g->fg_start; f != NULL;
				     f = f->fr_next)
					incnt++;
				if (incnt > 0)
					emitheader(g, incnt, 0);
			}
			if ((g->fg_flags & FR_OUTQUE) != 0) {
				for (outcnt = 0, f = g->fg_start; f != NULL;
				     f = f->fr_next)
					outcnt++;
				if (outcnt > 0)
					emitheader(g, 0, outcnt);
			}
		}
		emittail();
		fprintf(cfile, "#endif /* IPFILTER_COMPILED */\n");
	}

}


static void emitheader(grp, incount, outcount)
	frgroup_t *grp;
	u_int incount, outcount;
{
	static FILE *fph = NULL;
	frgroup_t *g;

	if (fph == NULL) {
		fph = fopen("ip_rules.h", "w");
		if (fph == NULL)
			return;

		fprintf(fph, "extern int ipfrule_add __P((void));\n");
		fprintf(fph, "extern int ipfrule_remove __P((void));\n");
	}

	printhooks(cfile, incount, outcount, grp);

	if (incount) {
		fprintf(fph, "\n\
extern frentry_t *ipfrule_match_in_%s __P((fr_info_t *, u_32_t *));\n\
extern frentry_t *ipf_rules_in_%s[%d];\n",
			grp->fg_name, grp->fg_name, incount);

		for (g = groups; g != grp; g = g->fg_next)
			if ((strncmp(g->fg_name, grp->fg_name,
				     FR_GROUPLEN) == 0) &&
			    g->fg_flags == grp->fg_flags)
				break;
		if (g == grp) {
			fprintf(fph, "\n\
extern int ipfrule_add_in_%s __P((void));\n\
extern int ipfrule_remove_in_%s __P((void));\n", grp->fg_name, grp->fg_name);
		}
	}
	if (outcount) {
		fprintf(fph, "\n\
extern frentry_t *ipfrule_match_out_%s __P((fr_info_t *, u_32_t *));\n\
extern frentry_t *ipf_rules_out_%s[%d];\n",
			grp->fg_name, grp->fg_name, outcount);

		for (g = groups; g != grp; g = g->fg_next)
			if ((strncmp(g->fg_name, grp->fg_name,
				     FR_GROUPLEN) == 0) &&
			    g->fg_flags == grp->fg_flags)
				break;
		if (g == grp) {
			fprintf(fph, "\n\
extern int ipfrule_add_out_%s __P((void));\n\
extern int ipfrule_remove_out_%s __P((void));\n",
				grp->fg_name, grp->fg_name);
		}
	}
}

static void emittail()
{
	frgroup_t *g;

	fprintf(cfile, "\n\
int ipfrule_add()\n\
{\n\
	int err;\n\
\n");
	for (g = groups; g != NULL; g = g->fg_next)
		fprintf(cfile, "\
	err = ipfrule_add_%s_%s();\n\
	if (err != 0)\n\
		return err;\n",
			(g->fg_flags & FR_INQUE) ? "in" : "out", g->fg_name);
	fprintf(cfile, "\
	return 0;\n");
	fprintf(cfile, "}\n\
\n");

	fprintf(cfile, "\n\
int ipfrule_remove()\n\
{\n\
	int err;\n\
\n");
	for (g = groups; g != NULL; g = g->fg_next)
		fprintf(cfile, "\
	err = ipfrule_remove_%s_%s();\n\
	if (err != 0)\n\
		return err;\n",
			(g->fg_flags & FR_INQUE) ? "in" : "out", g->fg_name);
	fprintf(cfile, "\
	return 0;\n");
	fprintf(cfile, "}\n");
}


static void emitGroup(num, dir, v, fr, group, incount, outcount)
	int num, dir;
	void *v;
	frentry_t *fr;
	char *group;
	u_int incount, outcount;
{
	static FILE *fp = NULL;
	static int header[2] = { 0, 0 };
	static char egroup[FR_GROUPLEN] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	static int openfunc = 0;
	static mc_t *n = NULL;
	static int sin = 0;
	frentry_t *f;
	frgroup_t *g;
	fripf_t *ipf;
	int i, in, j;
	mc_t *m = v;

	if (fp == NULL)
		fp = cfile;
	if (fp == NULL)
		return;
	if (strncmp(egroup, group, FR_GROUPLEN)) {
		for (sin--; sin > 0; sin--) {
			indent(fp, sin);
			fprintf(fp, "}\n");
		}
		if (openfunc == 1) {
			fprintf(fp, "\treturn fr;\n}\n");
			openfunc = 0;
			if (n != NULL) {
				free(n);
				n = NULL;
			}
		}
		sin = 0;
		header[0] = 0;
		header[1] = 0;
		strncpy(egroup, group, FR_GROUPLEN);
	} else if (openfunc == 1 && num < 0) {
		if (n != NULL) {
			free(n);
			n = NULL;
		}
		for (sin--; sin > 0; sin--) {
			indent(fp, sin);
			fprintf(fp, "}\n");
		}
		if (openfunc == 1) {
			fprintf(fp, "\treturn fr;\n}\n");
			openfunc = 0;
		}
	}

	if (dir == -1)
		return;

	for (g = groups; g != NULL; g = g->fg_next) {
		if (dir == 0 && (g->fg_flags & FR_INQUE) == 0)
			continue;
		else if (dir == 1 && (g->fg_flags & FR_OUTQUE) == 0)
			continue;
		if (strncmp(g->fg_name, group, FR_GROUPLEN) != 0)
			continue;
		break;
	}

	/*
	 * Output the array of pointers to rules for this group.
	 */
	if (g != NULL && num == -2 && dir == 0 && header[0] == 0 &&
	    incount != 0) {
		fprintf(fp, "\nfrentry_t *ipf_rules_in_%s[%d] = {",
			group, incount);
		for (f = g->fg_start, i = 0; f != NULL; f = f->fr_next) {
			if ((f->fr_flags & FR_INQUE) == 0)
				continue;
			if ((i & 1) == 0) {
				fprintf(fp, "\n\t");
			}
			fprintf(fp, "(frentry_t *)&in_rule_%s_%d",
				FR_NAME(f, fr_group), i);
			if (i + 1 < incount)
				fprintf(fp, ", ");
			i++;
		}
		fprintf(fp, "\n};\n");
	}

	if (g != NULL && num == -2 && dir == 1 && header[0] == 0 &&
	    outcount != 0) {
		fprintf(fp, "\nfrentry_t *ipf_rules_out_%s[%d] = {",
			group, outcount);
		for (f = g->fg_start, i = 0; f != NULL; f = f->fr_next) {
			if ((f->fr_flags & FR_OUTQUE) == 0)
				continue;
			if ((i & 1) == 0) {
				fprintf(fp, "\n\t");
			}
			fprintf(fp, "(frentry_t *)&out_rule_%s_%d",
				FR_NAME(f, fr_group), i);
			if (i + 1 < outcount)
				fprintf(fp, ", ");
			i++;
		}
		fprintf(fp, "\n};\n");
		fp = NULL;
	}

	if (num < 0)
		return;

	in = 0;
	ipf = fr->fr_ipf;

	/*
	 * If the function header has not been printed then print it now.
	 */
	if (g != NULL && header[dir] == 0) {
		int pdst = 0, psrc = 0;

		openfunc = 1;
		fprintf(fp, "\nfrentry_t *ipfrule_match_%s_%s(fin, passp)\n",
			(dir == 0) ? "in" : "out", group);
		fprintf(fp, "fr_info_t *fin;\n");
		fprintf(fp, "u_32_t *passp;\n");
		fprintf(fp, "{\n");
		fprintf(fp, "\tfrentry_t *fr = NULL;\n");

		/*
		 * Print out any variables that need to be declared.
		 */
		for (f = g->fg_start, i = 0; f != NULL; f = f->fr_next) {
			if (incount + outcount > m[FRC_SRC].e + 1)
				psrc = 1;
			if (incount + outcount > m[FRC_DST].e + 1)
				pdst = 1;
		}
		if (psrc == 1)
			fprintf(fp, "\tu_32_t src = ntohl(%s);\n",
				"fin->fin_fi.fi_saddr");
		if (pdst == 1)
			fprintf(fp, "\tu_32_t dst = ntohl(%s);\n",
				"fin->fin_fi.fi_daddr");
	}

	for (i = 0; i < FRC_MAX; i++) {
		switch(m[i].c)
		{
		case FRC_IFN :
			if (fr->fr_ifnames[0] != -1)
				m[i].s = 1;
			break;
		case FRC_V :
			if (ipf != NULL && ipf->fri_mip.fi_v != 0)
				m[i].s = 1;
			break;
		case FRC_FL :
			if (ipf != NULL && ipf->fri_mip.fi_flx != 0)
				m[i].s = 1;
			break;
		case FRC_P :
			if (ipf != NULL && ipf->fri_mip.fi_p != 0)
				m[i].s = 1;
			break;
		case FRC_TTL :
			if (ipf != NULL && ipf->fri_mip.fi_ttl != 0)
				m[i].s = 1;
			break;
		case FRC_TOS :
			if (ipf != NULL && ipf->fri_mip.fi_tos != 0)
				m[i].s = 1;
			break;
		case FRC_TCP :
			if (ipf == NULL)
				break;
			if ((ipf->fri_ip.fi_p == IPPROTO_TCP) &&
			    fr->fr_tcpfm != 0)
				m[i].s = 1;
			break;
		case FRC_SP :
			if (ipf == NULL)
				break;
			if (fr->fr_scmp == FR_INRANGE)
				m[i].s = 1;
			else if (fr->fr_scmp == FR_OUTRANGE)
				m[i].s = 1;
			else if (fr->fr_scmp != 0)
				m[i].s = 1;
			break;
		case FRC_DP :
			if (ipf == NULL)
				break;
			if (fr->fr_dcmp == FR_INRANGE)
				m[i].s = 1;
			else if (fr->fr_dcmp == FR_OUTRANGE)
				m[i].s = 1;
			else if (fr->fr_dcmp != 0)
				m[i].s = 1;
			break;
		case FRC_SRC :
			if (ipf == NULL)
				break;
			if (fr->fr_satype == FRI_LOOKUP) {
				;
			} else if ((fr->fr_smask != 0) ||
				   (fr->fr_flags & FR_NOTSRCIP) != 0)
				m[i].s = 1;
			break;
		case FRC_DST :
			if (ipf == NULL)
				break;
			if (fr->fr_datype == FRI_LOOKUP) {
				;
			} else if ((fr->fr_dmask != 0) ||
				   (fr->fr_flags & FR_NOTDSTIP) != 0)
				m[i].s = 1;
			break;
		case FRC_OPT :
			if (ipf == NULL)
				break;
			if (fr->fr_optmask != 0)
				m[i].s = 1;
			break;
		case FRC_SEC :
			if (ipf == NULL)
				break;
			if (fr->fr_secmask != 0)
				m[i].s = 1;
			break;
		case FRC_ATH :
			if (ipf == NULL)
				break;
			if (fr->fr_authmask != 0)
				m[i].s = 1;
			break;
		case FRC_ICT :
			if (ipf == NULL)
				break;
			if ((fr->fr_icmpm & 0xff00) != 0)
				m[i].s = 1;
			break;
		case FRC_ICC :
			if (ipf == NULL)
				break;
			if ((fr->fr_icmpm & 0xff) != 0)
				m[i].s = 1;
			break;
		}
	}

	if (!header[dir]) {
		fprintf(fp, "\n");
		header[dir] = 1;
		sin = 0;
	}

	qsort(m, FRC_MAX, sizeof(mc_t), intcmp);

	if (n) {
		/*
		 * Calculate the indentation interval upto the last common
		 * common comparison being made.
		 */
		for (i = 0, in = 1; i < FRC_MAX; i++) {
			if (n[i].c != m[i].c)
				break;
			if (n[i].s != m[i].s)
				break;
			if (n[i].s) {
				if (n[i].n && (n[i].n > n[i].e)) {
					m[i].p++;
					in += m[i].p;
					break;
				}
				if (n[i].e > 0) {
					in++;
				} else
					break;
			}
		}
		if (sin != in) {
			for (j = sin - 1; j >= in; j--) {
				indent(fp, j);
				fprintf(fp, "}\n");
			}
		}
	} else {
		in = 1;
		i = 0;
	}

	/*
	 * print out C code that implements a filter rule.
	 */
	for (; i < FRC_MAX; i++) {
		switch(m[i].c)
		{
		case FRC_IFN :
			if (m[i].s) {
				indent(fp, in);
				fprintf(fp, "if (fin->fin_ifp == ");
				fprintf(fp, "ipf_rules_%s_%s[%d]->fr_ifa) {\n",
					dir ? "out" : "in", group, num);
				in++;
			}
			break;
		case FRC_V :
			if (m[i].s) {
				indent(fp, in);
				fprintf(fp, "if (fin->fin_v == %d) {\n",
					ipf->fri_ip.fi_v);
				in++;
			}
			break;
		case FRC_FL :
			if (m[i].s) {
				indent(fp, in);
				fprintf(fp, "if (");
				printeq(fp, "fin->fin_flx",
				        ipf->fri_mip.fi_flx, 0xf,
					ipf->fri_ip.fi_flx);
				in++;
			}
			break;
		case FRC_P :
			if (m[i].s) {
				indent(fp, in);
				fprintf(fp, "if (fin->fin_p == %d) {\n",
					ipf->fri_ip.fi_p);
				in++;
			}
			break;
		case FRC_TTL :
			if (m[i].s) {
				indent(fp, in);
				fprintf(fp, "if (");
				printeq(fp, "fin->fin_ttl",
					ipf->fri_mip.fi_ttl, 0xff,
					ipf->fri_ip.fi_ttl);
				in++;
			}
			break;
		case FRC_TOS :
			if (m[i].s) {
				indent(fp, in);
				fprintf(fp, "if (fin->fin_tos");
				printeq(fp, "fin->fin_tos",
					ipf->fri_mip.fi_tos, 0xff,
					ipf->fri_ip.fi_tos);
				in++;
			}
			break;
		case FRC_TCP :
			if (m[i].s) {
				indent(fp, in);
				fprintf(fp, "if (");
				printeq(fp, "fin->fin_tcpf", fr->fr_tcpfm,
					0xff, fr->fr_tcpf);
				in++;
			}
			break;
		case FRC_SP :
			if (!m[i].s)
				break;
			if (fr->fr_scmp == FR_INRANGE) {
				indent(fp, in);
				fprintf(fp, "if ((fin->fin_data[0] > %d) && ",
					fr->fr_sport);
				fprintf(fp, "(fin->fin_data[0] < %d)",
					fr->fr_stop);
				fprintf(fp, ") {\n");
				in++;
			} else if (fr->fr_scmp == FR_OUTRANGE) {
				indent(fp, in);
				fprintf(fp, "if ((fin->fin_data[0] < %d) || ",
					fr->fr_sport);
				fprintf(fp, "(fin->fin_data[0] > %d)",
					fr->fr_stop);
				fprintf(fp, ") {\n");
				in++;
			} else if (fr->fr_scmp) {
				indent(fp, in);
				fprintf(fp, "if (fin->fin_data[0] %s %d)",
					portcmp[fr->fr_scmp], fr->fr_sport);
				fprintf(fp, " {\n");
				in++;
			}
			break;
		case FRC_DP :
			if (!m[i].s)
				break;
			if (fr->fr_dcmp == FR_INRANGE) {
				indent(fp, in);
				fprintf(fp, "if ((fin->fin_data[1] > %d) && ",
					fr->fr_dport);
				fprintf(fp, "(fin->fin_data[1] < %d)",
					fr->fr_dtop);
				fprintf(fp, ") {\n");
				in++;
			} else if (fr->fr_dcmp == FR_OUTRANGE) {
				indent(fp, in);
				fprintf(fp, "if ((fin->fin_data[1] < %d) || ",
					fr->fr_dport);
				fprintf(fp, "(fin->fin_data[1] > %d)",
					fr->fr_dtop);
				fprintf(fp, ") {\n");
				in++;
			} else if (fr->fr_dcmp) {
				indent(fp, in);
				fprintf(fp, "if (fin->fin_data[1] %s %d)",
					portcmp[fr->fr_dcmp], fr->fr_dport);
				fprintf(fp, " {\n");
				in++;
			}
			break;
		case FRC_SRC :
			if (!m[i].s)
				break;
			if (fr->fr_satype == FRI_LOOKUP) {
				;
			} else if ((fr->fr_smask != 0) ||
				   (fr->fr_flags & FR_NOTSRCIP) != 0) {
				indent(fp, in);
				fprintf(fp, "if (");
				printipeq(fp, "src",
					  fr->fr_flags & FR_NOTSRCIP,
					  fr->fr_smask, fr->fr_saddr);
				in++;
			}
			break;
		case FRC_DST :
			if (!m[i].s)
				break;
			if (fr->fr_datype == FRI_LOOKUP) {
				;
			} else if ((fr->fr_dmask != 0) ||
				   (fr->fr_flags & FR_NOTDSTIP) != 0) {
				indent(fp, in);
				fprintf(fp, "if (");
				printipeq(fp, "dst",
					  fr->fr_flags & FR_NOTDSTIP,
					  fr->fr_dmask, fr->fr_daddr);
				in++;
			}
			break;
		case FRC_OPT :
			if (m[i].s) {
				indent(fp, in);
				fprintf(fp, "if (");
				printeq(fp, "fin->fin_fi.fi_optmsk",
					fr->fr_optmask, 0xffffffff,
				        fr->fr_optbits);
				in++;
			}
			break;
		case FRC_SEC :
			if (m[i].s) {
				indent(fp, in);
				fprintf(fp, "if (");
				printeq(fp, "fin->fin_fi.fi_secmsk",
					fr->fr_secmask, 0xffff,
					fr->fr_secbits);
				in++;
			}
			break;
		case FRC_ATH :
			if (m[i].s) {
				indent(fp, in);
				fprintf(fp, "if (");
				printeq(fp, "fin->fin_fi.fi_authmsk",
					fr->fr_authmask, 0xffff,
					fr->fr_authbits);
				in++;
			}
			break;
		case FRC_ICT :
			if (m[i].s) {
				indent(fp, in);
				fprintf(fp, "if (");
				printeq(fp, "fin->fin_data[0]",
					fr->fr_icmpm & 0xff00, 0xffff,
					fr->fr_icmp & 0xff00);
				in++;
			}
			break;
		case FRC_ICC :
			if (m[i].s) {
				indent(fp, in);
				fprintf(fp, "if (");
				printeq(fp, "fin->fin_data[0]",
					fr->fr_icmpm & 0xff, 0xffff,
					fr->fr_icmp & 0xff);
				in++;
			}
			break;
		}

	}

	indent(fp, in);
	if (fr->fr_flags & FR_QUICK) {
		fprintf(fp, "return (frentry_t *)&%s_rule_%s_%d;\n",
			fr->fr_flags & FR_INQUE ? "in" : "out",
			FR_NAME(fr, fr_group), num);
	} else {
		fprintf(fp, "fr = (frentry_t *)&%s_rule_%s_%d;\n",
			fr->fr_flags & FR_INQUE ? "in" : "out",
			FR_NAME(fr, fr_group), num);
	}
	if (n == NULL)
		n = (mc_t *)malloc(sizeof(*n) * FRC_MAX);
	bcopy((char *)m, (char *)n, sizeof(*n) * FRC_MAX);
	sin = in;
}


void printC(dir)
	int dir;
{
	static mc_t *m = NULL;
	frgroup_t *g;

	if (m == NULL)
		m = (mc_t *)calloc(FRC_MAX, sizeof(*m));

	for (g = groups; g != NULL; g = g->fg_next) {
		if ((dir == 0) && ((g->fg_flags & FR_INQUE) != 0))
			printCgroup(dir, g->fg_start, m, g->fg_name);
		if ((dir == 1) && ((g->fg_flags & FR_OUTQUE) != 0))
			printCgroup(dir, g->fg_start, m, g->fg_name);
	}

	emit(-1, dir, m, NULL);
}


/*
 * Now print out code to implement all of the rules.
 */
static void printCgroup(dir, top, m, group)
	int dir;
	frentry_t *top;
	mc_t *m;
	char *group;
{
	frentry_t *fr, *fr1;
	int i, n, rn;
	u_int count;

	for (count = 0, fr1 = top; fr1 != NULL; fr1 = fr1->fr_next) {
		if ((dir == 0) && ((fr1->fr_flags & FR_INQUE) != 0))
			count++;
		else if ((dir == 1) && ((fr1->fr_flags & FR_OUTQUE) != 0))
			count++;
	}

	if (dir == 0)
		emitGroup(-2, dir, m, fr1, group, count, 0);
	else if (dir == 1)
		emitGroup(-2, dir, m, fr1, group, 0, count);

	/*
	 * Before printing each rule, check to see how many of its fields are
	 * matched by subsequent rules.
	 */
	for (fr1 = top, rn = 0; fr1 != NULL; fr1 = fr1->fr_next, rn++) {
		if (!dir && !(fr1->fr_flags & FR_INQUE))
			continue;
		if (dir && !(fr1->fr_flags & FR_OUTQUE))
			continue;
		n = 0xfffffff;

		for (i = 0; i < FRC_MAX; i++)
			m[i].e = 0;
		qsort(m, FRC_MAX, sizeof(mc_t), intcmp);

		for (i = 0; i < FRC_MAX; i++) {
			m[i].c = i;
			m[i].e = 0;
			m[i].n = 0;
			m[i].s = 0;
		}

		for (fr = fr1->fr_next; fr; fr = fr->fr_next) {
			if (!dir && !(fr->fr_flags & FR_INQUE))
				continue;
			if (dir && !(fr->fr_flags & FR_OUTQUE))
				continue;

			if ((n & 0x0001) &&
			    !strcmp(fr1->fr_names + fr1->fr_ifnames[0],
				    fr->fr_names + fr->fr_ifnames[0])) {
				m[FRC_IFN].e++;
				m[FRC_IFN].n++;
			} else
				n &= ~0x0001;

			if ((n & 0x0002) && (fr1->fr_family == fr->fr_family)) {
				m[FRC_V].e++;
				m[FRC_V].n++;
			} else
				n &= ~0x0002;

			if ((n & 0x0004) &&
			    (fr->fr_type == fr1->fr_type) &&
			    (fr->fr_type == FR_T_IPF) &&
			    (fr1->fr_mip.fi_flx == fr->fr_mip.fi_flx) &&
			    (fr1->fr_ip.fi_flx == fr->fr_ip.fi_flx)) {
				m[FRC_FL].e++;
				m[FRC_FL].n++;
			} else
				n &= ~0x0004;

			if ((n & 0x0008) &&
			    (fr->fr_type == fr1->fr_type) &&
			    (fr->fr_type == FR_T_IPF) &&
			    (fr1->fr_proto == fr->fr_proto)) {
				m[FRC_P].e++;
				m[FRC_P].n++;
			} else
				n &= ~0x0008;

			if ((n & 0x0010) &&
			    (fr->fr_type == fr1->fr_type) &&
			    (fr->fr_type == FR_T_IPF) &&
			    (fr1->fr_ttl == fr->fr_ttl)) {
				m[FRC_TTL].e++;
				m[FRC_TTL].n++;
			} else
				n &= ~0x0010;

			if ((n & 0x0020) &&
			    (fr->fr_type == fr1->fr_type) &&
			    (fr->fr_type == FR_T_IPF) &&
			    (fr1->fr_tos == fr->fr_tos)) {
				m[FRC_TOS].e++;
				m[FRC_TOS].n++;
			} else
				n &= ~0x0020;

			if ((n & 0x0040) &&
			    (fr->fr_type == fr1->fr_type) &&
			    (fr->fr_type == FR_T_IPF) &&
			    ((fr1->fr_tcpfm == fr->fr_tcpfm) &&
			    (fr1->fr_tcpf == fr->fr_tcpf))) {
				m[FRC_TCP].e++;
				m[FRC_TCP].n++;
			} else
				n &= ~0x0040;

			if ((n & 0x0080) &&
			    (fr->fr_type == fr1->fr_type) &&
			    (fr->fr_type == FR_T_IPF) &&
			    ((fr1->fr_scmp == fr->fr_scmp) &&
			     (fr1->fr_stop == fr->fr_stop) &&
			     (fr1->fr_sport == fr->fr_sport))) {
				m[FRC_SP].e++;
				m[FRC_SP].n++;
			} else
				n &= ~0x0080;

			if ((n & 0x0100) &&
			    (fr->fr_type == fr1->fr_type) &&
			    (fr->fr_type == FR_T_IPF) &&
			    ((fr1->fr_dcmp == fr->fr_dcmp) &&
			     (fr1->fr_dtop == fr->fr_dtop) &&
			     (fr1->fr_dport == fr->fr_dport))) {
				m[FRC_DP].e++;
				m[FRC_DP].n++;
			} else
				n &= ~0x0100;

			if ((n & 0x0200) &&
			    (fr->fr_type == fr1->fr_type) &&
			    (fr->fr_type == FR_T_IPF) &&
			    ((fr1->fr_satype == FRI_LOOKUP) &&
			    (fr->fr_satype == FRI_LOOKUP) &&
			    (fr1->fr_srcnum == fr->fr_srcnum))) {
				m[FRC_SRC].e++;
				m[FRC_SRC].n++;
			} else if ((n & 0x0200) &&
				   (fr->fr_type == fr1->fr_type) &&
				   (fr->fr_type == FR_T_IPF) &&
				   (((fr1->fr_flags & FR_NOTSRCIP) ==
				    (fr->fr_flags & FR_NOTSRCIP)))) {
					if ((fr1->fr_smask == fr->fr_smask) &&
					    (fr1->fr_saddr == fr->fr_saddr))
						m[FRC_SRC].e++;
					else
						n &= ~0x0200;
					if (fr1->fr_smask &&
					    (fr1->fr_saddr & fr1->fr_smask) ==
					    (fr->fr_saddr & fr1->fr_smask)) {
						m[FRC_SRC].n++;
						n |= 0x0200;
					}
			} else {
				n &= ~0x0200;
			}

			if ((n & 0x0400) &&
			    (fr->fr_type == fr1->fr_type) &&
			    (fr->fr_type == FR_T_IPF) &&
			    ((fr1->fr_datype == FRI_LOOKUP) &&
			    (fr->fr_datype == FRI_LOOKUP) &&
			    (fr1->fr_dstnum == fr->fr_dstnum))) {
				m[FRC_DST].e++;
				m[FRC_DST].n++;
			} else if ((n & 0x0400) &&
				   (fr->fr_type == fr1->fr_type) &&
				   (fr->fr_type == FR_T_IPF) &&
				   (((fr1->fr_flags & FR_NOTDSTIP) ==
				    (fr->fr_flags & FR_NOTDSTIP)))) {
					if ((fr1->fr_dmask == fr->fr_dmask) &&
					    (fr1->fr_daddr == fr->fr_daddr))
						m[FRC_DST].e++;
					else
						n &= ~0x0400;
					if (fr1->fr_dmask &&
					    (fr1->fr_daddr & fr1->fr_dmask) ==
					    (fr->fr_daddr & fr1->fr_dmask)) {
						m[FRC_DST].n++;
						n |= 0x0400;
					}
			} else {
				n &= ~0x0400;
			}

			if ((n & 0x0800) &&
			    (fr->fr_type == fr1->fr_type) &&
			    (fr->fr_type == FR_T_IPF) &&
			    (fr1->fr_optmask == fr->fr_optmask) &&
			    (fr1->fr_optbits == fr->fr_optbits)) {
				m[FRC_OPT].e++;
				m[FRC_OPT].n++;
			} else
				n &= ~0x0800;

			if ((n & 0x1000) &&
			    (fr->fr_type == fr1->fr_type) &&
			    (fr->fr_type == FR_T_IPF) &&
			    (fr1->fr_secmask == fr->fr_secmask) &&
			    (fr1->fr_secbits == fr->fr_secbits)) {
				m[FRC_SEC].e++;
				m[FRC_SEC].n++;
			} else
				n &= ~0x1000;

			if ((n & 0x10000) &&
			    (fr->fr_type == fr1->fr_type) &&
			    (fr->fr_type == FR_T_IPF) &&
			    (fr1->fr_authmask == fr->fr_authmask) &&
			    (fr1->fr_authbits == fr->fr_authbits)) {
				m[FRC_ATH].e++;
				m[FRC_ATH].n++;
			} else
				n &= ~0x10000;

			if ((n & 0x20000) &&
			    (fr->fr_type == fr1->fr_type) &&
			    (fr->fr_type == FR_T_IPF) &&
			    ((fr1->fr_icmpm & 0xff00) ==
			     (fr->fr_icmpm & 0xff00)) &&
			    ((fr1->fr_icmp & 0xff00) ==
			     (fr->fr_icmp & 0xff00))) {
				m[FRC_ICT].e++;
				m[FRC_ICT].n++;
			} else
				n &= ~0x20000;

			if ((n & 0x40000) &&
			    (fr->fr_type == fr1->fr_type) &&
			    (fr->fr_type == FR_T_IPF) &&
			    ((fr1->fr_icmpm & 0xff) == (fr->fr_icmpm & 0xff)) &&
			    ((fr1->fr_icmp & 0xff) == (fr->fr_icmp & 0xff))) {
				m[FRC_ICC].e++;
				m[FRC_ICC].n++;
			} else
				n &= ~0x40000;
		}
		/*msort(m);*/

		if (dir == 0)
			emitGroup(rn, dir, m, fr1, group, count, 0);
		else if (dir == 1)
			emitGroup(rn, dir, m, fr1, group, 0, count);
	}
}

static void printhooks(fp, in, out, grp)
	FILE *fp;
	int in;
	int out;
	frgroup_t *grp;
{
	frentry_t *fr;
	char *group;
	int dogrp, i;
	char *instr;

	group = grp->fg_name;
	dogrp = 0;

	if (in && out) {
		fprintf(stderr,
			"printhooks called with both in and out set\n");
		exit(1);
	}

	if (in) {
		instr = "in";
	} else if (out) {
		instr = "out";
	} else {
		instr = "???";
	}
	fprintf(fp, "static frentry_t ipfrule_%s_%s;\n", instr, group);

	fprintf(fp, "\
\n\
int ipfrule_add_%s_%s()\n", instr, group);
	fprintf(fp, "\
{\n\
	int i, j, err = 0, max;\n\
	frentry_t *fp;\n");

	if (dogrp)
		fprintf(fp, "\
	frgroup_t *fg;\n");

	fprintf(fp, "\n");

	for (i = 0, fr = grp->fg_start; fr != NULL; i++, fr = fr->fr_next)
		if (fr->fr_dsize > 0) {
			fprintf(fp, "\
	ipf_rules_%s_%s[%d]->fr_data = &ipf%s_rule_data_%s_%u;\n",
				instr, grp->fg_name, i,
				instr, grp->fg_name, i);
		}
	fprintf(fp, "\
	max = sizeof(ipf_rules_%s_%s)/sizeof(frentry_t *);\n\
	for (i = 0; i < max; i++) {\n\
		fp = ipf_rules_%s_%s[i];\n\
		fp->fr_next = NULL;\n", instr, group, instr, group);

	fprintf(fp, "\
		for (j = i + 1; j < max; j++)\n\
			if (strncmp(fp->fr_names + fp->fr_group,\n\
				    ipf_rules_%s_%s[j]->fr_names +\n\
				    ipf_rules_%s_%s[j]->fr_group,\n\
				    FR_GROUPLEN) == 0) {\n\
				if (ipf_rules_%s_%s[j] != NULL)\n\
					ipf_rules_%s_%s[j]->fr_pnext =\n\
					    &fp->fr_next;\n\
				fp->fr_pnext = &ipf_rules_%s_%s[j];\n\
				fp->fr_next = ipf_rules_%s_%s[j];\n\
				break;\n\
			}\n", instr, group, instr, group, instr, group,
			      instr, group, instr, group, instr, group);
	if (dogrp)
		fprintf(fp, "\
\n\
		if (fp->fr_grhead != -1) {\n\
			fg = fr_addgroup(fp->fr_names + fp->fr_grhead,\n\
					 fp, FR_INQUE, IPL_LOGIPF, 0);\n\
			if (fg != NULL)\n\
				fp->fr_grp = &fg->fg_start;\n\
		}\n");
	fprintf(fp, "\
	}\n\
\n\
	fp = &ipfrule_%s_%s;\n", instr, group);
		fprintf(fp, "\
	bzero((char *)fp, sizeof(*fp));\n\
	fp->fr_type = FR_T_CALLFUNC_BUILTIN;\n\
	fp->fr_flags = FR_%sQUE|FR_NOMATCH;\n\
	fp->fr_data = (void *)ipf_rules_%s_%s[0];\n",
		(in != 0) ? "IN" : "OUT", instr, group);
	fprintf(fp, "\
	fp->fr_dsize = sizeof(ipf_rules_%s_%s[0]);\n",
		instr, group);

	fprintf(fp, "\
	fp->fr_family = AF_INET;\n\
	fp->fr_func = (ipfunc_t)ipfrule_match_%s_%s;\n\
	err = frrequest(&ipfmain, IPL_LOGIPF, SIOCADDFR, (caddr_t)fp,\n\
			ipfmain.ipf_active, 0);\n",
			instr, group);
	fprintf(fp, "\treturn err;\n}\n");

	fprintf(fp, "\n\n\
int ipfrule_remove_%s_%s()\n", instr, group);
	fprintf(fp, "\
{\n\
	int err = 0, i;\n\
	frentry_t *fp;\n\
\n\
	/*\n\
	 * Try to remove the %sbound rule.\n", instr);

	fprintf(fp, "\
	 */\n\
	if (ipfrule_%s_%s.fr_ref > 0) {\n", instr, group);

	fprintf(fp, "\
		err = EBUSY;\n\
	} else {\n");

	fprintf(fp, "\
		i = sizeof(ipf_rules_%s_%s)/sizeof(frentry_t *) - 1;\n\
		for (; i >= 0; i--) {\n\
			fp = ipf_rules_%s_%s[i];\n\
			if (fp->fr_ref > 1) {\n\
				err = EBUSY;\n\
				break;\n\
			}\n\
		}\n\
	}\n\
	if (err == 0)\n\
		err = frrequest(&ipfmain, IPL_LOGIPF, SIOCDELFR,\n\
				(caddr_t)&ipfrule_%s_%s,\n\
				ipfmain.ipf_active, 0);\n",
		instr, group, instr, group, instr, group);
	fprintf(fp, "\
	if (err)\n\
		return err;\n\
\n\n");

	fprintf(fp, "\treturn err;\n}\n");
}
