/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */

#include "ipf.h"


/*
 * print the filter structure in a useful way
 */
void
printfr(fp, iocfunc)
	struct	frentry	*fp;
	ioctlfunc_t	iocfunc;
{
	struct protoent	*p;
	u_short	sec[2];
	u_32_t type;
	int pr, af;
	char *s;
	int hash;

	pr = -2;
	type = fp->fr_type & ~FR_T_BUILTIN;

	if ((fp->fr_type & FR_T_BUILTIN) != 0)
		PRINTF("# Builtin: ");

	if (fp->fr_collect != 0)
		PRINTF("%u ", fp->fr_collect);

	if (fp->fr_type == FR_T_CALLFUNC) {
		;
	} else if (fp->fr_func != NULL) {
		PRINTF("call");
		if ((fp->fr_flags & FR_CALLNOW) != 0)
			PRINTF(" now");
		s = kvatoname(fp->fr_func, iocfunc);
		PRINTF(" %s/%u", s ? s : "?", fp->fr_arg);
	} else if (FR_ISPASS(fp->fr_flags))
		PRINTF("pass");
	else if (FR_ISBLOCK(fp->fr_flags)) {
		PRINTF("block");
	} else if ((fp->fr_flags & FR_LOGMASK) == FR_LOG) {
		printlog(fp);
	} else if (FR_ISACCOUNT(fp->fr_flags))
		PRINTF("count");
	else if (FR_ISAUTH(fp->fr_flags))
		PRINTF("auth");
	else if (FR_ISPREAUTH(fp->fr_flags))
		PRINTF("preauth");
	else if (FR_ISNOMATCH(fp->fr_flags))
		PRINTF("nomatch");
	else if (FR_ISDECAPS(fp->fr_flags))
		PRINTF("decapsulate");
	else if (FR_ISSKIP(fp->fr_flags))
		PRINTF("skip %u", fp->fr_arg);
	else {
		PRINTF("%x", fp->fr_flags);
	}
	if (fp->fr_flags & FR_RETICMP) {
		if ((fp->fr_flags & FR_RETMASK) == FR_FAKEICMP)
			PRINTF(" return-icmp-as-dest");
		else if ((fp->fr_flags & FR_RETMASK) == FR_RETICMP)
			PRINTF(" return-icmp");
		if (fp->fr_icode) {
			if (fp->fr_icode <= MAX_ICMPCODE)
				PRINTF("(%s)",
					icmpcodes[(int)fp->fr_icode]);
			else
				PRINTF("(%d)", fp->fr_icode);
		}
	} else if ((fp->fr_flags & FR_RETMASK) == FR_RETRST)
		PRINTF(" return-rst");

	if (fp->fr_flags & FR_OUTQUE)
		PRINTF(" out ");
	else if (fp->fr_flags & FR_INQUE)
		PRINTF(" in ");

	if (((fp->fr_flags & FR_LOGB) == FR_LOGB) ||
	    ((fp->fr_flags & FR_LOGP) == FR_LOGP)) {
		printlog(fp);
		putchar(' ');
	}

	if (fp->fr_flags & FR_QUICK)
		PRINTF("quick ");

	if (fp->fr_ifnames[0] != -1) {
		printifname("on ", fp->fr_names + fp->fr_ifnames[0],
			    fp->fr_ifa);
		if (fp->fr_ifnames[1] != -1 &&
		    strcmp(fp->fr_names + fp->fr_ifnames[1], "*"))
			printifname(",", fp->fr_names + fp->fr_ifnames[1],
				    fp->fr_ifas[1]);
		putchar(' ');
	}

	if (fp->fr_tif.fd_name != -1)
		print_toif(fp->fr_family, "to", fp->fr_names, &fp->fr_tif);
	if (fp->fr_dif.fd_name != -1)
		print_toif(fp->fr_family, "dup-to", fp->fr_names,
			   &fp->fr_dif);
	if (fp->fr_rif.fd_name != -1)
		print_toif(fp->fr_family, "reply-to", fp->fr_names,
			   &fp->fr_rif);
	if (fp->fr_flags & FR_FASTROUTE)
		PRINTF("fastroute ");

	if ((fp->fr_ifnames[2] != -1 &&
	     strcmp(fp->fr_names + fp->fr_ifnames[2], "*")) ||
	    (fp->fr_ifnames[3] != -1 &&
		 strcmp(fp->fr_names + fp->fr_ifnames[3], "*"))) {
		if (fp->fr_flags & FR_OUTQUE)
			PRINTF("in-via ");
		else
			PRINTF("out-via ");

		if (fp->fr_ifnames[2] != -1) {
			printifname("", fp->fr_names + fp->fr_ifnames[2],
				    fp->fr_ifas[2]);
			if (fp->fr_ifnames[3] != -1) {
				printifname(",",
					    fp->fr_names + fp->fr_ifnames[3],
					    fp->fr_ifas[3]);
			}
			putchar(' ');
		}
	}

	if (fp->fr_family == AF_INET) {
		PRINTF("inet ");
		af = AF_INET;
#ifdef USE_INET6
	} else if (fp->fr_family == AF_INET6) {
		PRINTF("inet6 ");
		af = AF_INET6;
#endif
	} else {
		af = -1;
	}

	if (type == FR_T_IPF) {
		if (fp->fr_mip.fi_tos)
			PRINTF("tos %#x ", fp->fr_tos);
		if (fp->fr_mip.fi_ttl)
			PRINTF("ttl %d ", fp->fr_ttl);
		if (fp->fr_flx & FI_TCPUDP) {
			PRINTF("proto tcp/udp ");
			pr = -1;
		} else if (fp->fr_mip.fi_p) {
			pr = fp->fr_ip.fi_p;
			p = getprotobynumber(pr);
			PRINTF("proto ");
			printproto(p, pr, NULL);
			putchar(' ');
		}
	}

	switch (type)
	{
	case FR_T_NONE :
		PRINTF("all");
		break;

	case FR_T_IPF :
		PRINTF("from %s", fp->fr_flags & FR_NOTSRCIP ? "!" : "");
		printaddr(af, fp->fr_satype, fp->fr_names, fp->fr_ifnames[0],
			  &fp->fr_src.s_addr, &fp->fr_smsk.s_addr);
		if (fp->fr_scmp)
			printportcmp(pr, &fp->fr_tuc.ftu_src);

		PRINTF(" to %s", fp->fr_flags & FR_NOTDSTIP ? "!" : "");
		printaddr(af, fp->fr_datype, fp->fr_names, fp->fr_ifnames[0],
			  &fp->fr_dst.s_addr, &fp->fr_dmsk.s_addr);
		if (fp->fr_dcmp)
			printportcmp(pr, &fp->fr_tuc.ftu_dst);

		if (((fp->fr_proto == IPPROTO_ICMP) ||
		     (fp->fr_proto == IPPROTO_ICMPV6)) && fp->fr_icmpm) {
			int	type = fp->fr_icmp, code;
			char	*name;

			type = ntohs(fp->fr_icmp);
			code = type & 0xff;
			type /= 256;
			name = icmptypename(fp->fr_family, type);
			if (name == NULL)
				PRINTF(" icmp-type %d", type);
			else
				PRINTF(" icmp-type %s", name);
			if (ntohs(fp->fr_icmpm) & 0xff)
				PRINTF(" code %d", code);
		}
		if ((fp->fr_proto == IPPROTO_TCP) &&
		    (fp->fr_tcpf || fp->fr_tcpfm)) {
			PRINTF(" flags ");
			printtcpflags(fp->fr_tcpf, fp->fr_tcpfm);
		}
		break;

	case FR_T_BPFOPC :
	    {
		fakebpf_t *fb;
		int i;

		PRINTF("bpf-v%d { \"", fp->fr_family);
		i = fp->fr_dsize / sizeof(*fb);

		for (fb = fp->fr_data, s = ""; i; i--, fb++, s = " ")
			PRINTF("%s%#x %#x %#x %#x", s, fb->fb_c, fb->fb_t,
			       fb->fb_f, fb->fb_k);

		PRINTF("\" }");
		break;
	    }

	case FR_T_COMPIPF :
		break;

	case FR_T_CALLFUNC :
		PRINTF("call function at %p", fp->fr_data);
		break;

	case FR_T_IPFEXPR :
		PRINTF("exp { \"");
		printipfexpr(fp->fr_data);
		PRINTF("\" } ");
		break;

	default :
		PRINTF("[unknown filter type %#x]", fp->fr_type);
		break;
	}

	if ((type == FR_T_IPF) &&
	    ((fp->fr_flx & FI_WITH) || (fp->fr_mflx & FI_WITH) ||
	     fp->fr_optbits || fp->fr_optmask ||
	     fp->fr_secbits || fp->fr_secmask)) {
		char *comma = " ";

		PRINTF(" with");
		if (fp->fr_optbits || fp->fr_optmask ||
		    fp->fr_secbits || fp->fr_secmask) {
			sec[0] = fp->fr_secmask;
			sec[1] = fp->fr_secbits;
			if (fp->fr_family == AF_INET)
				optprint(sec, fp->fr_optmask, fp->fr_optbits);
#ifdef	USE_INET6
			else
				optprintv6(sec, fp->fr_optmask,
					   fp->fr_optbits);
#endif
		} else if (fp->fr_mflx & FI_OPTIONS) {
			fputs(comma, stdout);
			if (!(fp->fr_flx & FI_OPTIONS))
				PRINTF("not ");
			PRINTF("ipopts");
			comma = ",";
		}
		if (fp->fr_mflx & FI_SHORT) {
			fputs(comma, stdout);
			if (!(fp->fr_flx & FI_SHORT))
				PRINTF("not ");
			PRINTF("short");
			comma = ",";
		}
		if (fp->fr_mflx & FI_FRAG) {
			fputs(comma, stdout);
			if (!(fp->fr_flx & FI_FRAG))
				PRINTF("not ");
			PRINTF("frag");
			comma = ",";
		}
		if (fp->fr_mflx & FI_FRAGBODY) {
			fputs(comma, stdout);
			if (!(fp->fr_flx & FI_FRAGBODY))
				PRINTF("not ");
			PRINTF("frag-body");
			comma = ",";
		}
		if (fp->fr_mflx & FI_NATED) {
			fputs(comma, stdout);
			if (!(fp->fr_flx & FI_NATED))
				PRINTF("not ");
			PRINTF("nat");
			comma = ",";
		}
		if (fp->fr_mflx & FI_LOWTTL) {
			fputs(comma, stdout);
			if (!(fp->fr_flx & FI_LOWTTL))
				PRINTF("not ");
			PRINTF("lowttl");
			comma = ",";
		}
		if (fp->fr_mflx & FI_BAD) {
			fputs(comma, stdout);
			if (!(fp->fr_flx & FI_BAD))
				PRINTF("not ");
			PRINTF("bad");
			comma = ",";
		}
		if (fp->fr_mflx & FI_BADSRC) {
			fputs(comma, stdout);
			if (!(fp->fr_flx & FI_BADSRC))
				PRINTF("not ");
			PRINTF("bad-src");
			comma = ",";
		}
		if (fp->fr_mflx & FI_BADNAT) {
			fputs(comma, stdout);
			if (!(fp->fr_flx & FI_BADNAT))
				PRINTF("not ");
			PRINTF("bad-nat");
			comma = ",";
		}
		if (fp->fr_mflx & FI_OOW) {
			fputs(comma, stdout);
			if (!(fp->fr_flx & FI_OOW))
				PRINTF("not ");
			PRINTF("oow");
			comma = ",";
		}
		if (fp->fr_mflx & FI_MBCAST) {
			fputs(comma, stdout);
			if (!(fp->fr_flx & FI_MBCAST))
				PRINTF("not ");
			PRINTF("mbcast");
			comma = ",";
		}
		if (fp->fr_mflx & FI_BROADCAST) {
			fputs(comma, stdout);
			if (!(fp->fr_flx & FI_BROADCAST))
				PRINTF("not ");
			PRINTF("bcast");
			comma = ",";
		}
		if (fp->fr_mflx & FI_MULTICAST) {
			fputs(comma, stdout);
			if (!(fp->fr_flx & FI_MULTICAST))
				PRINTF("not ");
			PRINTF("mcast");
			comma = ",";
		}
		if (fp->fr_mflx & FI_STATE) {
			fputs(comma, stdout);
			if (!(fp->fr_flx & FI_STATE))
				PRINTF("not ");
			PRINTF("state");
			comma = ",";
		}
		if (fp->fr_mflx & FI_V6EXTHDR) {
			fputs(comma, stdout);
			if (!(fp->fr_flx & FI_V6EXTHDR))
				PRINTF("not ");
			PRINTF("v6hdrs");
			comma = ",";
		}
	}

	if (fp->fr_flags & FR_KEEPSTATE) {
		host_track_t *src = &fp->fr_srctrack;
		PRINTF(" keep state");
		if ((fp->fr_flags & (FR_STSTRICT|FR_NEWISN|
				     FR_NOICMPERR|FR_STATESYNC)) ||
		    (fp->fr_statemax != 0) || (fp->fr_age[0] != 0) ||
		    (src->ht_max_nodes != 0)) {
			char *comma = "";
			PRINTF(" (");
			if (fp->fr_statemax != 0) {
				PRINTF("limit %u", fp->fr_statemax);
				comma = ",";
			}
			if (src->ht_max_nodes != 0) {
				PRINTF("%smax-nodes %d", comma,
				       src->ht_max_nodes);
				if (src->ht_max_per_node)
					PRINTF(", max-per-src %d/%d",
					       src->ht_max_per_node,
					       src->ht_netmask);
				comma = ",";
			}
			if (fp->fr_flags & FR_STSTRICT) {
				PRINTF("%sstrict", comma);
				comma = ",";
			}
			if (fp->fr_flags & FR_STLOOSE) {
				PRINTF("%sloose", comma);
				comma = ",";
			}
			if (fp->fr_flags & FR_NEWISN) {
				PRINTF("%snewisn", comma);
				comma = ",";
			}
			if (fp->fr_flags & FR_NOICMPERR) {
				PRINTF("%sno-icmp-err", comma);
				comma = ",";
			}
			if (fp->fr_flags & FR_STATESYNC) {
				PRINTF("%ssync", comma);
				comma = ",";
			}
			if (fp->fr_age[0] || fp->fr_age[1])
				PRINTF("%sage %d/%d", comma, fp->fr_age[0],
				       fp->fr_age[1]);
			PRINTF(")");
		}
	}
	if (fp->fr_flags & FR_KEEPFRAG) {
		PRINTF(" keep frags");
		if (fp->fr_flags & (FR_FRSTRICT)) {
			PRINTF(" (");
			if (fp->fr_flags & FR_FRSTRICT)
				PRINTF("strict");
			PRINTF(")");

		}
	}
	if (fp->fr_isc != (struct ipscan *)-1) {
		if (fp->fr_isctag != -1)
			PRINTF(" scan %s", fp->fr_isctag + fp->fr_names);
		else
			PRINTF(" scan *");
	}
	if (fp->fr_grhead != -1)
		PRINTF(" head %s", fp->fr_names + fp->fr_grhead);
	if (fp->fr_group != -1)
		PRINTF(" group %s", fp->fr_names + fp->fr_group);
	if (fp->fr_logtag != FR_NOLOGTAG || *fp->fr_nattag.ipt_tag) {
		char *s = "";

		PRINTF(" set-tag(");
		if (fp->fr_logtag != FR_NOLOGTAG) {
			PRINTF("log=%u", fp->fr_logtag);
			s = ", ";
		}
		if (*fp->fr_nattag.ipt_tag) {
			PRINTF("%snat=%-.*s", s, IPFTAG_LEN,
				fp->fr_nattag.ipt_tag);
		}
		PRINTF(")");
	}

	if (fp->fr_pps)
		PRINTF(" pps %d", fp->fr_pps);

	if (fp->fr_comment != -1)
		PRINTF(" comment \"%s\"", fp->fr_names + fp->fr_comment);

	hash = 0;
	if ((fp->fr_flags & FR_KEEPSTATE) && (opts & OPT_VERBOSE)) {
		PRINTF(" # count %d", fp->fr_statecnt);
		if (fp->fr_die != 0)
			PRINTF(" rule-ttl %u", fp->fr_die);
		hash = 1;
	} else if (fp->fr_die != 0) {
		PRINTF(" # rule-ttl %u", fp->fr_die);
		hash = 1;
	}
	if (opts & OPT_DEBUG) {
		if (hash == 0)
			putchar('#');
		PRINTF(" ref %d", fp->fr_ref);
	}
	(void)putchar('\n');
}
