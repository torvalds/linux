/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * Added redirect stuff and a variety of bug fixes. (mcn@EnGarde.com)
 */

#include "ipf.h"
#include "kmem.h"


#if !defined(lint)
static const char rcsid[] = "@(#)$Id$";
#endif


/*
 * Print out a NAT rule
 */
void
printnat(np, opts)
	ipnat_t *np;
	int opts;
{
	struct protoent *pr;
	char *base;
	int family;
	int proto;

	if (np->in_v[0] == 4)
		family = AF_INET;
#ifdef USE_INET6
	else if (np->in_v[0] == 6)
		family = AF_INET6;
#endif
	else
		family = AF_UNSPEC;

	if (np->in_flags & IPN_NO)
		PRINTF("no ");

	switch (np->in_redir)
	{
	case NAT_REDIRECT|NAT_ENCAP :
		PRINTF("encap in on");
		proto = np->in_pr[0];
		break;
	case NAT_MAP|NAT_ENCAP :
		PRINTF("encap out on");
		proto = np->in_pr[1];
		break;
	case NAT_REDIRECT|NAT_DIVERTUDP :
		PRINTF("divert in on");
		proto = np->in_pr[0];
		break;
	case NAT_MAP|NAT_DIVERTUDP :
		PRINTF("divert out on");
		proto = np->in_pr[1];
		break;
	case NAT_REDIRECT|NAT_REWRITE :
		PRINTF("rewrite in on");
		proto = np->in_pr[0];
		break;
	case NAT_MAP|NAT_REWRITE :
		PRINTF("rewrite out on");
		proto = np->in_pr[1];
		break;
	case NAT_REDIRECT :
		PRINTF("rdr");
		proto = np->in_pr[0];
		break;
	case NAT_MAP :
		PRINTF("map");
		proto = np->in_pr[1];
		break;
	case NAT_MAPBLK :
		PRINTF("map-block");
		proto = np->in_pr[1];
		break;
	case NAT_BIMAP :
		PRINTF("bimap");
		proto = np->in_pr[0];
		break;
	default :
		FPRINTF(stderr, "unknown value for in_redir: %#x\n",
			np->in_redir);
		proto = np->in_pr[0];
		break;
	}

	pr = getprotobynumber(proto);

	base = np->in_names;
	if (!strcmp(base + np->in_ifnames[0], "-"))
		PRINTF(" \"%s\"", base + np->in_ifnames[0]);
	else
		PRINTF(" %s", base + np->in_ifnames[0]);
	if ((np->in_ifnames[1] != -1) &&
	    (strcmp(base + np->in_ifnames[0], base + np->in_ifnames[1]) != 0)) {
		if (!strcmp(base + np->in_ifnames[1], "-"))
			PRINTF(",\"%s\"", base + np->in_ifnames[1]);
		else
			PRINTF(",%s", base + np->in_ifnames[1]);
	}
	putchar(' ');

	if (family == AF_INET6)
		PRINTF("inet6 ");

	if (np->in_redir & (NAT_REWRITE|NAT_ENCAP|NAT_DIVERTUDP)) {
		if ((proto != 0) || (np->in_flags & IPN_TCPUDP)) {
			PRINTF("proto ");
			printproto(pr, proto, np);
			putchar(' ');
		}
	}

	if (np->in_flags & IPN_FILTER) {
		if (np->in_flags & IPN_NOTSRC)
			PRINTF("! ");
		PRINTF("from ");
		printnataddr(np->in_v[0], np->in_names, &np->in_osrc,
			     np->in_ifnames[0]);
		if (np->in_scmp)
			printportcmp(proto, &np->in_tuc.ftu_src);

		if (np->in_flags & IPN_NOTDST)
			PRINTF(" !");
		PRINTF(" to ");
		printnataddr(np->in_v[0], np->in_names, &np->in_odst,
			     np->in_ifnames[0]);
		if (np->in_dcmp)
			printportcmp(proto, &np->in_tuc.ftu_dst);
	}

	if (np->in_redir & (NAT_ENCAP|NAT_DIVERTUDP)) {
		PRINTF(" -> src ");
		printnataddr(np->in_v[1], np->in_names, &np->in_nsrc,
			     np->in_ifnames[0]);
		if ((np->in_redir & NAT_DIVERTUDP) != 0)
			PRINTF(",%u", np->in_spmin);
		PRINTF(" dst ");
		printnataddr(np->in_v[1], np->in_names, &np->in_ndst,
			     np->in_ifnames[0]);
		if ((np->in_redir & NAT_DIVERTUDP) != 0)
			PRINTF(",%u udp", np->in_dpmin);
		if ((np->in_flags & IPN_PURGE) != 0)
			PRINTF(" purge");
		PRINTF(";\n");

	} else if (np->in_redir & NAT_REWRITE) {
		PRINTF(" -> src ");
		if (np->in_nsrc.na_atype == FRI_LOOKUP &&
		    np->in_nsrc.na_type == IPLT_DSTLIST) {
			PRINTF("dstlist/");
			if (np->in_nsrc.na_subtype == 0)
				PRINTF("%d", np->in_nsrc.na_num);
			else
				PRINTF("%s", base + np->in_nsrc.na_num);
		} else {
			printnataddr(np->in_v[1], np->in_names, &np->in_nsrc,
				     np->in_ifnames[0]);
		}
		if ((((np->in_flags & IPN_TCPUDP) != 0)) &&
		    (np->in_spmin != 0)) {
			if ((np->in_flags & IPN_FIXEDSPORT) != 0) {
				PRINTF(",port = %u", np->in_spmin);
			} else {
				PRINTF(",%u", np->in_spmin);
				if (np->in_spmax != np->in_spmin)
					PRINTF("-%u", np->in_spmax);
			}
		}
		PRINTF(" dst ");
		if (np->in_ndst.na_atype == FRI_LOOKUP &&
		    np->in_ndst.na_type == IPLT_DSTLIST) {
			PRINTF("dstlist/");
			if (np->in_ndst.na_subtype == 0)
				PRINTF("%d", np->in_nsrc.na_num);
			else
				PRINTF("%s", base + np->in_ndst.na_num);
		} else {
			printnataddr(np->in_v[1], np->in_names, &np->in_ndst,
				     np->in_ifnames[0]);
		}
		if ((((np->in_flags & IPN_TCPUDP) != 0)) &&
		    (np->in_dpmin != 0)) {
			if ((np->in_flags & IPN_FIXEDDPORT) != 0) {
				PRINTF(",port = %u", np->in_dpmin);
			} else {
				PRINTF(",%u", np->in_dpmin);
				if (np->in_dpmax != np->in_dpmin)
					PRINTF("-%u", np->in_dpmax);
			}
		}
		if ((np->in_flags & IPN_PURGE) != 0)
			PRINTF(" purge");
		PRINTF(";\n");

	} else if (np->in_redir == NAT_REDIRECT) {
		if (!(np->in_flags & IPN_FILTER)) {
			printnataddr(np->in_v[0], np->in_names, &np->in_odst,
				     np->in_ifnames[0]);
			if (np->in_flags & IPN_TCPUDP) {
				PRINTF(" port %d", np->in_odport);
				if (np->in_odport != np->in_dtop)
					PRINTF("-%d", np->in_dtop);
			}
		}
		if (np->in_flags & IPN_NO) {
			putchar(' ');
			printproto(pr, proto, np);
			PRINTF(";\n");
			return;
		}
		PRINTF(" -> ");
		printnataddr(np->in_v[1], np->in_names, &np->in_ndst,
			     np->in_ifnames[0]);
		if (np->in_flags & IPN_TCPUDP) {
			if ((np->in_flags & IPN_FIXEDDPORT) != 0)
				PRINTF(" port = %d", np->in_dpmin);
			else {
				PRINTF(" port %d", np->in_dpmin);
				if (np->in_dpmin != np->in_dpmax)
					PRINTF("-%d", np->in_dpmax);
			}
		}
		putchar(' ');
		printproto(pr, proto, np);
		if (np->in_flags & IPN_ROUNDR)
			PRINTF(" round-robin");
		if (np->in_flags & IPN_FRAG)
			PRINTF(" frag");
		if (np->in_age[0] != 0 || np->in_age[1] != 0) {
			PRINTF(" age %d/%d", np->in_age[0], np->in_age[1]);
		}
		if (np->in_flags & IPN_STICKY)
			PRINTF(" sticky");
		if (np->in_mssclamp != 0)
			PRINTF(" mssclamp %d", np->in_mssclamp);
		if (np->in_plabel != -1)
			PRINTF(" proxy %s", np->in_names + np->in_plabel);
		if (np->in_tag.ipt_tag[0] != '\0')
			PRINTF(" tag %-.*s", IPFTAG_LEN, np->in_tag.ipt_tag);
		if ((np->in_flags & IPN_PURGE) != 0)
			PRINTF(" purge");
		PRINTF("\n");
		if (opts & OPT_DEBUG)
			PRINTF("\tpmax %u\n", np->in_dpmax);

	} else {
		int protoprinted = 0;

		if (!(np->in_flags & IPN_FILTER)) {
			printnataddr(np->in_v[0], np->in_names, &np->in_osrc,
				     np->in_ifnames[0]);
		}
		if (np->in_flags & IPN_NO) {
			putchar(' ');
			printproto(pr, proto, np);
			PRINTF(";\n");
			return;
		}
		PRINTF(" -> ");
		if (np->in_flags & IPN_SIPRANGE) {
			PRINTF("range ");
			printnataddr(np->in_v[1], np->in_names, &np->in_nsrc,
				     np->in_ifnames[0]);
		} else {
			printnataddr(np->in_v[1], np->in_names, &np->in_nsrc,
				     np->in_ifnames[0]);
		}
		if (np->in_plabel != -1) {
			PRINTF(" proxy port ");
			if (np->in_odport != 0) {
				char *s;

				s = portname(proto, np->in_odport);
				if (s != NULL)
					fputs(s, stdout);
				else
					fputs("???", stdout);
			}
			PRINTF(" %s/", np->in_names + np->in_plabel);
			printproto(pr, proto, NULL);
			protoprinted = 1;
		} else if (np->in_redir == NAT_MAPBLK) {
			if ((np->in_spmin == 0) &&
			    (np->in_flags & IPN_AUTOPORTMAP))
				PRINTF(" ports auto");
			else
				PRINTF(" ports %d", np->in_spmin);
			if (opts & OPT_DEBUG)
				PRINTF("\n\tip modulous %d", np->in_spmax);

		} else if (np->in_spmin || np->in_spmax) {
			if (np->in_flags & IPN_ICMPQUERY) {
				PRINTF(" icmpidmap ");
			} else {
				PRINTF(" portmap ");
			}
			printproto(pr, proto, np);
			protoprinted = 1;
			if (np->in_flags & IPN_AUTOPORTMAP) {
				PRINTF(" auto");
				if (opts & OPT_DEBUG)
					PRINTF(" [%d:%d %d %d]",
					       np->in_spmin, np->in_spmax,
					       np->in_ippip, np->in_ppip);
			} else {
				PRINTF(" %d:%d", np->in_spmin, np->in_spmax);
			}
			if (np->in_flags & IPN_SEQUENTIAL)
				PRINTF(" sequential");
		}

		if (np->in_flags & IPN_FRAG)
			PRINTF(" frag");
		if (np->in_age[0] != 0 || np->in_age[1] != 0) {
			PRINTF(" age %d/%d", np->in_age[0], np->in_age[1]);
		}
		if (np->in_mssclamp != 0)
			PRINTF(" mssclamp %d", np->in_mssclamp);
		if (np->in_tag.ipt_tag[0] != '\0')
			PRINTF(" tag %s", np->in_tag.ipt_tag);
		if (!protoprinted && (np->in_flags & IPN_TCPUDP || proto)) {
			putchar(' ');
			printproto(pr, proto, np);
		}
		if ((np->in_flags & IPN_PURGE) != 0)
			PRINTF(" purge");
		PRINTF("\n");
		if (opts & OPT_DEBUG) {
			PRINTF("\tnextip ");
			printip(family, &np->in_snip);
			PRINTF(" pnext %d\n", np->in_spnext);
		}
	}

	if (opts & OPT_DEBUG) {
		PRINTF("\tspace %lu use %u hits %lu flags %#x proto %d/%d",
			np->in_space, np->in_use, np->in_hits,
			np->in_flags, np->in_pr[0], np->in_pr[1]);
		PRINTF(" hv %u/%u\n", np->in_hv[0], np->in_hv[1]);
		PRINTF("\tifp[0] %p ifp[1] %p apr %p\n",
			np->in_ifps[0], np->in_ifps[1], np->in_apr);
		PRINTF("\ttqehead %p/%p comment %p\n",
			np->in_tqehead[0], np->in_tqehead[1], np->in_comment);
	}
}
