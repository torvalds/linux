/*	$FreeBSD$	*/

/*
 * Copyright (C) 2000-2003 by Darren Reed
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * Simple DCE transparent proxy for MSN RPC.
 *
 * ******* NOTE: THIS PROXY DOES NOT DO ADDRESS TRANSLATION ********
 *
 * Id: ip_msnrpc_pxy.c,v 2.17.2.1 2005/02/04 10:22:55 darrenr Exp
 */

#define	IPF_MSNRPC_PROXY

#define	IPF_MINMSNRPCLEN	24
#define	IPF_MSNRPCSKIP		(2 + 19 + 2 + 2 + 2 + 19 + 2 + 2)


typedef	struct	msnrpchdr	{
	u_char	mrh_major;	/* major # == 5 */
	u_char	mrh_minor;	/* minor # == 0 */
	u_char	mrh_type;
	u_char	mrh_flags;
	u_32_t	mrh_endian;
	u_short	mrh_dlen;	/* data size */
	u_short	mrh_alen;	/* authentication length */
	u_32_t	mrh_cid;	/* call identifier */
	u_32_t	mrh_hint;	/* allocation hint */
	u_short	mrh_ctxt;	/* presentation context hint */
	u_char	mrh_ccnt;	/* cancel count */
	u_char	mrh_ans;
} msnrpchdr_t;

int ippr_msnrpc_init __P((void));
void ippr_msnrpc_fini __P((void));
int ippr_msnrpc_new __P((fr_info_t *, ap_session_t *, nat_t *));
int ippr_msnrpc_out __P((fr_info_t *, ap_session_t *, nat_t *));
int ippr_msnrpc_in __P((fr_info_t *, ap_session_t *, nat_t *));
int ippr_msnrpc_check __P((ip_t *, msnrpchdr_t *));

static	frentry_t	msnfr;

int	msn_proxy_init = 0;

/*
 * Initialize local structures.
 */
int ippr_msnrpc_init()
{
	bzero((char *)&msnfr, sizeof(msnfr));
	msnfr.fr_ref = 1;
	msnfr.fr_flags = FR_INQUE|FR_PASS|FR_QUICK|FR_KEEPSTATE;
	MUTEX_INIT(&msnfr.fr_lock, "MSN RPC proxy rule lock");
	msn_proxy_init = 1;

	return 0;
}


void ippr_msnrpc_fini()
{
	if (msn_proxy_init == 1) {
		MUTEX_DESTROY(&msnfr.fr_lock);
		msn_proxy_init = 0;
	}
}


int ippr_msnrpc_new(fin, aps, nat)
fr_info_t *fin;
ap_session_t *aps;
nat_t *nat;
{
	msnrpcinfo_t *mri;

	KMALLOC(mri, msnrpcinfo_t *);
	if (mri == NULL)
		return -1;
	aps->aps_data = mri;
	aps->aps_psiz = sizeof(msnrpcinfo_t);

	bzero((char *)mri, sizeof(*mri));
	mri->mri_cmd[0] = 0xff;
	mri->mri_cmd[1] = 0xff;
	return 0;
}


int ippr_msnrpc_check(ip, mrh)
ip_t *ip;
msnrpchdr_t *mrh;
{
	if (mrh->mrh_major != 5)
		return -1;
	if (mrh->mrh_minor != 0)
		return -1;
	if (mrh->mrh_alen != 0)
		return -1;
	if (mrh->mrh_endian == 0x10) {
		/* Both gateway and packet match endian */
		if (mrh->mrh_dlen > ip->ip_len)
			return -1;
		if (mrh->mrh_type == 0 || mrh->mrh_type == 2)
			if (mrh->mrh_hint > ip->ip_len)
				return -1;
	} else if (mrh->mrh_endian == 0x10000000) {
		/* XXX - Endian mismatch - should be swapping! */
		return -1;
	} else {
		return -1;
	}
	return 0;
}


int ippr_msnrpc_out(fin, ip, aps, nat)
fr_info_t *fin;
ip_t *ip;
ap_session_t *aps;
nat_t *nat;
{
	msnrpcinfo_t *mri;
	msnrpchdr_t *mrh;
	tcphdr_t *tcp;
	int dlen;

	mri = aps->aps_data;
	if (mri == NULL)
		return 0;

	tcp = (tcphdr_t *)fin->fin_dp;
	dlen = fin->fin_dlen - (TCP_OFF(tcp) << 2);
	if (dlen < IPF_MINMSNRPCLEN)
		return 0;

	mrh = (msnrpchdr_t *)((char *)tcp + (TCP_OFF(tcp) << 2));
	if (ippr_msnrpc_check(ip, mrh))
		return 0;

	mri->mri_valid++;

	switch (mrh->mrh_type)
	{
	case 0x0b :	/* BIND */
	case 0x00 :	/* REQUEST */
		break;
	case 0x0c :	/* BIND ACK */
	case 0x02 :	/* RESPONSE */
	default:
		return 0;
	}
	mri->mri_cmd[1] = mrh->mrh_type;
	return 0;
}


int ippr_msnrpc_in(fin, ip, aps, nat)
fr_info_t *fin;
ip_t *ip;
ap_session_t *aps;
nat_t *nat;
{
	tcphdr_t *tcp, tcph, *tcp2 = &tcph;
	int dlen, sz, sz2, i;
	msnrpcinfo_t *mri;
	msnrpchdr_t *mrh;
	fr_info_t fi;
	u_short len;
	char *s;

	mri = aps->aps_data;
	if (mri == NULL)
		return 0;
	tcp = (tcphdr_t *)fin->fin_dp;
	dlen = fin->fin_dlen - (TCP_OFF(tcp) << 2);
	if (dlen < IPF_MINMSNRPCLEN)
		return 0;

	mrh = (msnrpchdr_t *)((char *)tcp + (TCP_OFF(tcp) << 2));
	if (ippr_msnrpc_check(ip, mrh))
		return 0;

	mri->mri_valid++;

	switch (mrh->mrh_type)
	{
	case 0x0c :	/* BIND ACK */
		if (mri->mri_cmd[1] != 0x0b)
			return 0;
		break;
	case 0x02 :	/* RESPONSE */
		if (mri->mri_cmd[1] != 0x00)
			return 0;
		break;
	case 0x0b :	/* BIND */
	case 0x00 :	/* REQUEST */
	default:
		return 0;
	}
	mri->mri_cmd[0] = mrh->mrh_type;
	dlen -= sizeof(*mrh);

	/*
	 * Only processes RESPONSE's
	 */
	if (mrh->mrh_type != 0x02)
		return 0;

	/*
	 * Skip over some bytes...what are these really ?
	 */
	if (dlen <= 44)
		return 0;
	s = (char *)(mrh + 1) + 20;
	dlen -= 20;
	bcopy(s, (char *)&len, sizeof(len));
	if (len == 1) {
		s += 20;
		dlen -= 20;
	} else if (len == 2) {
		s += 24;
		dlen -= 24;
	} else
		return 0;

	if (dlen <= 10)
		return 0;
	dlen -= 10;
	bcopy(s, (char *)&sz, sizeof(sz));
	s += sizeof(sz);
	bcopy(s, (char *)&sz2, sizeof(sz2));
	s += sizeof(sz2);
	if (sz2 != sz)
		return 0;
	if (sz > dlen)
		return 0;
	if (*s++ != 5)
		return 0;
	if (*s++ != 0)
		return 0;
	sz -= IPF_MSNRPCSKIP;
	s += IPF_MSNRPCSKIP;
	dlen -= IPF_MSNRPCSKIP;

	do {
		if (sz < 7 || dlen < 7)
			break;
		bcopy(s, (char *)&len, sizeof(len));
		if (dlen < len)
			break;
		if (sz < len)
			break;

		if (len != 1)
			break;
		sz -= 3;
		i = *(s + 2);
		s += 3;
		dlen -= 3;

		bcopy(s, (char *)&len, sizeof(len));
		if (dlen < len)
			break;
		if (sz < len)
			break;
		s += sizeof(len);

		switch (i)
		{
		case 7 :
			if (len == 2) {
				bcopy(s, (char *)&mri->mri_rport, 2);
				mri->mri_flags |= 1;
			}
			break;
		case 9 :
			if (len == 4) {
				bcopy(s, (char *)&mri->mri_raddr, 4);
				mri->mri_flags |= 2;
			}
			break;
		default :
			break;
		}
		sz -= len;
		s += len;
		dlen -= len;
	} while (sz > 0);

	if (mri->mri_flags == 3) {
		int slen;

		bcopy((char *)fin, (char *)&fi, sizeof(fi));
		bzero((char *)tcp2, sizeof(*tcp2));

		slen = ip->ip_len;
		ip->ip_len = fin->fin_hlen + sizeof(*tcp2);
		bcopy((char *)fin, (char *)&fi, sizeof(fi));
		bzero((char *)tcp2, sizeof(*tcp2));
		tcp2->th_win = htons(8192);
		TCP_OFF_A(tcp2, 5);
		fi.fin_data[0] = htons(mri->mri_rport);
		tcp2->th_sport = mri->mri_rport;
		fi.fin_data[1] = 0;
		tcp2->th_dport = 0;
		fi.fin_state = NULL;
		fi.fin_nat = NULL;
		fi.fin_dlen = sizeof(*tcp2);
		fi.fin_plen = fi.fin_hlen + sizeof(*tcp2);
		fi.fin_dp = (char *)tcp2;
		fi.fin_fi.fi_daddr = ip->ip_dst.s_addr;
		fi.fin_fi.fi_saddr = mri->mri_raddr.s_addr;
		if (!fi.fin_fr)
			fi.fin_fr = &msnfr;
		if (fr_stlookup(&fi, NULL, NULL)) {
			RWLOCK_EXIT(&ipf_state);
		} else {
			(void) fr_addstate(&fi, NULL, SI_W_DPORT|SI_CLONE);
			if (fi.fin_state != NULL)
				fr_statederef(&fi, (ipstate_t **)&fi.fin_state);
		}
		ip->ip_len = slen;
	}
	mri->mri_flags = 0;
	return 0;
}
