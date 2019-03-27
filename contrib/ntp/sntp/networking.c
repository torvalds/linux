#include <config.h>
#include "networking.h"
#include "ntp_debug.h"


/* Send a packet */
int
sendpkt (
	SOCKET rsock,
	sockaddr_u *dest,
	struct pkt *pkt,
	int len
	)
{
	int cc;

#ifdef DEBUG
	if (debug > 2) {
		printf("sntp sendpkt: Packet data:\n");
		pkt_output(pkt, len, stdout);
	}
#endif
	TRACE(1, ("sntp sendpkt: Sending packet to %s ...\n",
		  sptoa(dest)));

	cc = sendto(rsock, (void *)pkt, len, 0, &dest->sa, 
		    SOCKLEN(dest));
	if (cc == SOCKET_ERROR) {
		msyslog(LOG_ERR, "Send to %s failed, %m",
			sptoa(dest));
		return FALSE;
	}
	TRACE(1, ("Packet sent.\n"));

	return TRUE;
}


/* Receive raw data */
int
recvdata(
	SOCKET		rsock,
	sockaddr_u *	sender,
	void *		rdata,
	int		rdata_length
	)
{
	GETSOCKNAME_SOCKLEN_TYPE slen;
	int recvc;

	slen = sizeof(*sender);
	recvc = recvfrom(rsock, rdata, rdata_length, 0,
			 &sender->sa, &slen);
	if (recvc < 0)
		return recvc;
#ifdef DEBUG
	if (debug > 2) {
		printf("Received %d bytes from %s:\n", recvc, sptoa(sender));
		pkt_output((struct pkt *)rdata, recvc, stdout);
	}
#endif
	return recvc;
}

/* Parsing from a short 'struct pkt' directly is bound to create
 * coverity warnings. These are hard to avoid, as the formal declaration
 * does not reflect the true layout in the presence of autokey extension
 * fields. Parsing and skipping the extension fields of a received packet
 * until there's only the MAC left is better done in this separate
 * function.
 */
static void*
skip_efields(
	u_int32 *head,	/* head of extension chain 	*/
	u_int32 *tail	/* tail/end of extension chain	*/
	)
{
	
	u_int nlen;	/* next extension length */
	while ((tail - head) > 6) {
		nlen = ntohl(*head) & 0xffff;
		++head;
		nlen = (nlen + 3) >> 2;
		if (nlen > (u_int)(tail - head) || nlen < 4)
			return NULL;	/* Blooper! Inconsistent! */
		head += nlen;
	}
	return head;
}

/*
** Check if it's data for us and whether it's useable or not.
**
** If not, return a failure code so we can delete this server from our list
** and continue with another one.
*/
int
process_pkt (
	struct pkt *rpkt,
	sockaddr_u *sender,
	int pkt_len,
	int mode,
	struct pkt *spkt,
	const char * func_name
	)
{
	u_int		key_id;
	struct key *	pkt_key;
	int		is_authentic;
	int		mac_size;
	u_int		exten_len;
	u_int32 *       exten_end;
	u_int32 *       packet_end;
	l_fp		sent_xmt;
	l_fp		resp_org;

	// key_id = 0;
	pkt_key = NULL;
	is_authentic = (HAVE_OPT(AUTHENTICATION)) ? 0 : -1;

	/*
	 * Parse the extension field if present. We figure out whether
	 * an extension field is present by measuring the MAC size. If
	 * the number of words following the packet header is 0, no MAC
	 * is present and the packet is not authenticated. If 1, the
	 * packet is a crypto-NAK; if 3, the packet is authenticated
	 * with DES; if 5, the packet is authenticated with MD5; if 6,
	 * the packet is authenticated with SHA. If 2 or 4, the packet
	 * is a runt and discarded forthwith. If greater than 6, an
	 * extension field is present, so we subtract the length of the
	 * field and go around again.
	 */
	if (pkt_len < (int)LEN_PKT_NOMAC || (pkt_len & 3) != 0) {
		msyslog(LOG_ERR,
			"%s: Incredible packet length: %d.  Discarding.",
			func_name, pkt_len);
		return PACKET_UNUSEABLE;
	}

	/* HMS: the following needs a bit of work */
	/* Note: pkt_len must be a multiple of 4 at this point! */
	packet_end = (void*)((char*)rpkt + pkt_len);
	exten_end = skip_efields(rpkt->exten, packet_end);
	if (NULL == exten_end) {
		msyslog(LOG_ERR,
			"%s: Missing extension field.  Discarding.",
			func_name);
		return PACKET_UNUSEABLE;
	}

	/* get size of MAC in cells; can be zero */
	exten_len = (u_int)(packet_end - exten_end);

	/* deduce action required from remaining length */
	switch (exten_len) {

	case 0:	/* no Legacy MAC */
		break;

	case 1:	/* crypto NAK */		
		/* Only if the keyID is 0 and there were no EFs */
		key_id = ntohl(*exten_end);
		printf("Crypto NAK = 0x%08x from %s\n", key_id, stoa(sender));
		break;

	case 3: /* key ID + 3DES MAC -- unsupported! */
		msyslog(LOG_ERR,
			"%s: Key ID + 3DES MAC is unsupported.  Discarding.",
			func_name);
		return PACKET_UNUSEABLE;

	case 5:	/* key ID + MD5 MAC */
	case 6:	/* key ID + SHA MAC */
		/*
		** Look for the key used by the server in the specified
		** keyfile and if existent, fetch it or else leave the
		** pointer untouched
		*/
		key_id = ntohl(*exten_end);
		get_key(key_id, &pkt_key);
		if (!pkt_key) {
			printf("unrecognized key ID = 0x%08x\n", key_id);
			break;
		}
		/*
		** Seems like we've got a key with matching keyid.
		**
		** Generate a md5sum of the packet with the key from our
		** keyfile and compare those md5sums.
		*/
		mac_size = exten_len << 2;
		if (!auth_md5(rpkt, pkt_len - mac_size,
			      mac_size - 4, pkt_key)) {
			is_authentic = FALSE;
			break;
		}
		/* Yay! Things worked out! */
		is_authentic = TRUE;
		TRACE(1, ("sntp %s: packet from %s authenticated using key id %d.\n",
			  func_name, stoa(sender), key_id));
		break;

	default:
		msyslog(LOG_ERR,
			"%s: Unexpected extension length: %d.  Discarding.",
			func_name, exten_len);
		return PACKET_UNUSEABLE;
	}

	switch (is_authentic) {

	case -1:	/* unknown */
		break;

	case 0:		/* not authentic */
		return SERVER_AUTH_FAIL;
		break;

	case 1:		/* authentic */
		break;

	default:	/* error */
		break;
	}

	/* Check for server's ntp version */
	if (PKT_VERSION(rpkt->li_vn_mode) < NTP_OLDVERSION ||
		PKT_VERSION(rpkt->li_vn_mode) > NTP_VERSION) {
		msyslog(LOG_ERR,
			"%s: Packet shows wrong version (%d)",
			func_name, PKT_VERSION(rpkt->li_vn_mode));
		return SERVER_UNUSEABLE;
	} 
	/* We want a server to sync with */
	if (PKT_MODE(rpkt->li_vn_mode) != mode &&
	    PKT_MODE(rpkt->li_vn_mode) != MODE_PASSIVE) {
		msyslog(LOG_ERR,
			"%s: mode %d stratum %d", func_name, 
			PKT_MODE(rpkt->li_vn_mode), rpkt->stratum);
		return SERVER_UNUSEABLE;
	}
	/* Stratum is unspecified (0) check what's going on */
	if (STRATUM_PKT_UNSPEC == rpkt->stratum) {
		char *ref_char;

		TRACE(1, ("%s: Stratum unspecified, going to check for KOD (stratum: %d)\n", 
			  func_name, rpkt->stratum));
		ref_char = (char *) &rpkt->refid;
		TRACE(1, ("%s: Packet refid: %c%c%c%c\n", func_name,
			  ref_char[0], ref_char[1], ref_char[2], ref_char[3]));
		/* If it's a KOD packet we'll just use the KOD information */
		if (ref_char[0] != 'X') {
			if (strncmp(ref_char, "DENY", 4) == 0)
				return KOD_DEMOBILIZE;
			if (strncmp(ref_char, "RSTR", 4) == 0)
				return KOD_DEMOBILIZE;
			if (strncmp(ref_char, "RATE", 4) == 0)
				return KOD_RATE;
			/*
			** There are other interesting kiss codes which
			** might be interesting for authentication.
			*/
		}
	}
	/* If the server is not synced it's not really useable for us */
	if (LEAP_NOTINSYNC == PKT_LEAP(rpkt->li_vn_mode)) {
		msyslog(LOG_ERR,
			"%s: %s not in sync, skipping this server",
			func_name, stoa(sender));
		return SERVER_UNUSEABLE;
	}

	/*
	 * Decode the org timestamp and make sure we're getting a response
	 * to our last request, but only if we're not in broadcast mode.
	 */
	if (MODE_BROADCAST == mode)
		return pkt_len;

	if (!L_ISEQU(&rpkt->org, &spkt->xmt)) {
		NTOHL_FP(&rpkt->org, &resp_org);
		NTOHL_FP(&spkt->xmt, &sent_xmt);
		msyslog(LOG_ERR,
			"%s response org expected to match sent xmt",
			stoa(sender));
		msyslog(LOG_ERR, "resp org: %s", prettydate(&resp_org));
		msyslog(LOG_ERR, "sent xmt: %s", prettydate(&sent_xmt));
		return PACKET_UNUSEABLE;
	}

	return pkt_len;
}
