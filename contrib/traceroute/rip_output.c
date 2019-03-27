/* A sample version of rip_output() from /sys/netinet/raw_ip.c */

rip_output(m, so)
	register struct mbuf *m;
	struct socket *so;
{
	register struct ip *ip;
	int error;
	struct rawcb *rp = sotorawcb(so);
	struct sockaddr_in *sin;
#if BSD>=43
	short proto = rp->rcb_proto.sp_protocol;
#else
	short proto = so->so_proto->pr_protocol;
#endif
	/*
	 * if the protocol is IPPROTO_RAW, the user handed us a
	 * complete IP packet.  Otherwise, allocate an mbuf for a
	 * header and fill it in as needed.
	 */
	if (proto != IPPROTO_RAW) {
		/*
		 * Calculate data length and get an mbuf
		 * for IP header.
		 */
		int len = 0;
		struct mbuf *m0;

		for (m0 = m; m; m = m->m_next)
			len += m->m_len;

		m = m_get(M_DONTWAIT, MT_HEADER);
		if (m == 0) {
			m = m0;
			error = ENOBUFS;
			goto bad;
		}
		m->m_off = MMAXOFF - sizeof(struct ip);
		m->m_len = sizeof(struct ip);
		m->m_next = m0;

		ip = mtod(m, struct ip *);
		ip->ip_tos = 0;
		ip->ip_off = 0;
		ip->ip_p = proto;
		ip->ip_len = sizeof(struct ip) + len;
		ip->ip_ttl = MAXTTL;
	} else
		ip = mtod(m, struct ip *);

	if (rp->rcb_flags & RAW_LADDR) {
		sin = (struct sockaddr_in *)&rp->rcb_laddr;
		if (sin->sin_family != AF_INET) {
			error = EAFNOSUPPORT;
			goto bad;
		}
		ip->ip_src.s_addr = sin->sin_addr.s_addr;
	} else
		ip->ip_src.s_addr = 0;

	ip->ip_dst = ((struct sockaddr_in *)&rp->rcb_faddr)->sin_addr;

#if BSD>=43
	return (ip_output(m, rp->rcb_options, &rp->rcb_route,
	   (so->so_options & SO_DONTROUTE) | IP_ALLOWBROADCAST));
#else
	return (ip_output(m, (struct mbuf *)0, &rp->rcb_route,
	   (so->so_options & SO_DONTROUTE) | IP_ALLOWBROADCAST));
#endif
bad:
	m_freem(m);
	return (error);
}
