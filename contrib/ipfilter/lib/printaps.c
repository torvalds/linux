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


void
printaps(aps, opts, proto)
	ap_session_t *aps;
	int opts, proto;
{
	ipsec_pxy_t ipsec;
	ap_session_t ap;
	ftpinfo_t ftp;
	aproxy_t apr;
	raudio_t ra;

	if (kmemcpy((char *)&ap, (long)aps, sizeof(ap)))
		return;
	if (kmemcpy((char *)&apr, (long)ap.aps_apr, sizeof(apr)))
		return;
	PRINTF("\tproxy %s/%d use %d flags %x\n", apr.apr_label,
		apr.apr_p, apr.apr_ref, apr.apr_flags);
#ifdef	USE_QUAD_T
	PRINTF("\tbytes %"PRIu64" pkts %"PRIu64"",
		(unsigned long long)ap.aps_bytes,
		(unsigned long long)ap.aps_pkts);
#else
	PRINTF("\tbytes %lu pkts %lu", ap.aps_bytes, ap.aps_pkts);
#endif
	PRINTF(" data %s\n", ap.aps_data ? "YES" : "NO");
	if ((proto == IPPROTO_TCP) && (opts & OPT_VERBOSE)) {
		PRINTF("\t\tstate[%u,%u], sel[%d,%d]\n",
			ap.aps_state[0], ap.aps_state[1],
			ap.aps_sel[0], ap.aps_sel[1]);
#if (defined(NetBSD) && (NetBSD >= 199905) && (NetBSD < 1991011)) || \
    (__FreeBSD_version >= 300000) || defined(OpenBSD)
		PRINTF("\t\tseq: off %hd/%hd min %x/%x\n",
			ap.aps_seqoff[0], ap.aps_seqoff[1],
			ap.aps_seqmin[0], ap.aps_seqmin[1]);
		PRINTF("\t\tack: off %hd/%hd min %x/%x\n",
			ap.aps_ackoff[0], ap.aps_ackoff[1],
			ap.aps_ackmin[0], ap.aps_ackmin[1]);
#else
		PRINTF("\t\tseq: off %hd/%hd min %lx/%lx\n",
			ap.aps_seqoff[0], ap.aps_seqoff[1],
			ap.aps_seqmin[0], ap.aps_seqmin[1]);
		PRINTF("\t\tack: off %hd/%hd min %lx/%lx\n",
			ap.aps_ackoff[0], ap.aps_ackoff[1],
			ap.aps_ackmin[0], ap.aps_ackmin[1]);
#endif
	}

	if (!strcmp(apr.apr_label, "raudio") && ap.aps_psiz == sizeof(ra)) {
		if (kmemcpy((char *)&ra, (long)ap.aps_data, sizeof(ra)))
			return;
		PRINTF("\tReal Audio Proxy:\n");
		PRINTF("\t\tSeen PNA: %d\tVersion: %d\tEOS: %d\n",
			ra.rap_seenpna, ra.rap_version, ra.rap_eos);
		PRINTF("\t\tMode: %#x\tSBF: %#x\n", ra.rap_mode, ra.rap_sbf);
		PRINTF("\t\tPorts:pl %hu, pr %hu, sr %hu\n",
			ra.rap_plport, ra.rap_prport, ra.rap_srport);
	} else if (!strcmp(apr.apr_label, "ftp") &&
		   (ap.aps_psiz == sizeof(ftp))) {
		if (kmemcpy((char *)&ftp, (long)ap.aps_data, sizeof(ftp)))
			return;
		PRINTF("\tFTP Proxy:\n");
		PRINTF("\t\tpassok: %d\n", ftp.ftp_passok);
		ftp.ftp_side[0].ftps_buf[FTP_BUFSZ - 1] = '\0';
		ftp.ftp_side[1].ftps_buf[FTP_BUFSZ - 1] = '\0';
		PRINTF("\tClient:\n");
		PRINTF("\t\tseq %x (ack %x) len %d junk %d cmds %d\n",
			ftp.ftp_side[0].ftps_seq[0],
			ftp.ftp_side[0].ftps_seq[1],
			ftp.ftp_side[0].ftps_len, ftp.ftp_side[0].ftps_junk,
			ftp.ftp_side[0].ftps_cmds);
		PRINTF("\t\tbuf [");
		printbuf(ftp.ftp_side[0].ftps_buf, FTP_BUFSZ, 1);
		PRINTF("]\n\tServer:\n");
		PRINTF("\t\tseq %x (ack %x) len %d junk %d cmds %d\n",
			ftp.ftp_side[1].ftps_seq[0],
			ftp.ftp_side[1].ftps_seq[1],
			ftp.ftp_side[1].ftps_len, ftp.ftp_side[1].ftps_junk,
			ftp.ftp_side[1].ftps_cmds);
		PRINTF("\t\tbuf [");
		printbuf(ftp.ftp_side[1].ftps_buf, FTP_BUFSZ, 1);
		PRINTF("]\n");
	} else if (!strcmp(apr.apr_label, "ipsec") &&
		   (ap.aps_psiz == sizeof(ipsec))) {
		if (kmemcpy((char *)&ipsec, (long)ap.aps_data, sizeof(ipsec)))
			return;
		PRINTF("\tIPSec Proxy:\n");
		PRINTF("\t\tICookie %08x%08x RCookie %08x%08x %s\n",
			(u_int)ntohl(ipsec.ipsc_icookie[0]),
			(u_int)ntohl(ipsec.ipsc_icookie[1]),
			(u_int)ntohl(ipsec.ipsc_rcookie[0]),
			(u_int)ntohl(ipsec.ipsc_rcookie[1]),
			ipsec.ipsc_rckset ? "(Set)" : "(Not set)");
	}
}
