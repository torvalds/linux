/*
 * resolver.c
 *
 * resolver implementation
 *
 * a Net::DNS like library for C
 *
 * (c) NLnet Labs, 2004-2006
 *
 * See the file LICENSE for the license
 */

#include <ldns/config.h>

#include <ldns/ldns.h>
#include <strings.h>

/* Access function for reading
 * and setting the different Resolver
 * options */

/* read */
uint16_t
ldns_resolver_port(const ldns_resolver *r)
{
	return r->_port;
}

ldns_rdf *
ldns_resolver_source(const ldns_resolver *r)
{
	return r->_source;
}

uint16_t
ldns_resolver_edns_udp_size(const ldns_resolver *r)
{
	        return r->_edns_udp_size;
}

uint8_t
ldns_resolver_retry(const ldns_resolver *r)
{
	return r->_retry;
}

uint8_t
ldns_resolver_retrans(const ldns_resolver *r)
{
	return r->_retrans;
}

bool
ldns_resolver_fallback(const ldns_resolver *r)
{
	return r->_fallback;
}

uint8_t
ldns_resolver_ip6(const ldns_resolver *r)
{
	return r->_ip6;
}

bool
ldns_resolver_recursive(const ldns_resolver *r)
{
	return r->_recursive;
}

bool
ldns_resolver_debug(const ldns_resolver *r)
{
	return r->_debug;
}

bool
ldns_resolver_dnsrch(const ldns_resolver *r)
{
	return r->_dnsrch;
}

bool
ldns_resolver_fail(const ldns_resolver *r)
{
	return r->_fail;
}

bool
ldns_resolver_defnames(const ldns_resolver *r)
{
	return r->_defnames;
}

ldns_rdf *
ldns_resolver_domain(const ldns_resolver *r)
{
	return r->_domain;
}

ldns_rdf **
ldns_resolver_searchlist(const ldns_resolver *r)
{
	return r->_searchlist;
}

ldns_rdf **
ldns_resolver_nameservers(const ldns_resolver *r)
{
	return r->_nameservers;
}

size_t
ldns_resolver_nameserver_count(const ldns_resolver *r)
{
	return r->_nameserver_count;
}

bool
ldns_resolver_dnssec(const ldns_resolver *r)
{
	return r->_dnssec;
}

bool
ldns_resolver_dnssec_cd(const ldns_resolver *r)
{
	return r->_dnssec_cd;
}

ldns_rr_list *
ldns_resolver_dnssec_anchors(const ldns_resolver *r)
{
    return r->_dnssec_anchors;
}

bool
ldns_resolver_trusted_key(const ldns_resolver *r, ldns_rr_list * keys, ldns_rr_list * trusted_keys)
{
  size_t i;
  bool result = false;

  ldns_rr_list * trust_anchors;
  ldns_rr * cur_rr;

  if (!r || !keys) { return false; }

  trust_anchors = ldns_resolver_dnssec_anchors(r);

  if (!trust_anchors) { return false; }

  for (i = 0; i < ldns_rr_list_rr_count(keys); i++) {

    cur_rr = ldns_rr_list_rr(keys, i);
    if (ldns_rr_list_contains_rr(trust_anchors, cur_rr)) {
      if (trusted_keys) { ldns_rr_list_push_rr(trusted_keys, cur_rr); }
      result = true;
    }
  }

  return result;
}

bool
ldns_resolver_igntc(const ldns_resolver *r)
{
	return r->_igntc;
}

bool
ldns_resolver_usevc(const ldns_resolver *r)
{
	return r->_usevc;
}

size_t *
ldns_resolver_rtt(const ldns_resolver *r)
{
	return r->_rtt;
}

size_t
ldns_resolver_nameserver_rtt(const ldns_resolver *r, size_t pos)
{
	size_t *rtt;

	assert(r != NULL);

	rtt = ldns_resolver_rtt(r);

	if (pos >= ldns_resolver_nameserver_count(r)) {
		/* error ?*/
		return 0;
	} else {
		return rtt[pos];
	}

}

struct timeval
ldns_resolver_timeout(const ldns_resolver *r)
{
	return r->_timeout;
}

const char *
ldns_resolver_tsig_keyname(const ldns_resolver *r)
{
	return r->_tsig_keyname;
}

const char *
ldns_resolver_tsig_algorithm(const ldns_resolver *r)
{
	return r->_tsig_algorithm;
}

const char *
ldns_resolver_tsig_keydata(const ldns_resolver *r)
{
	return r->_tsig_keydata;
}

bool
ldns_resolver_random(const ldns_resolver *r)
{
	return r->_random;
}

size_t
ldns_resolver_searchlist_count(const ldns_resolver *r)
{
	return r->_searchlist_count;
}

/* write */
void
ldns_resolver_set_port(ldns_resolver *r, uint16_t p)
{
	r->_port = p;
}

void
ldns_resolver_set_source(ldns_resolver *r, ldns_rdf *s)
{
	r->_source = s;
}

ldns_rdf *
ldns_resolver_pop_nameserver(ldns_resolver *r)
{
	ldns_rdf **nameservers;
	ldns_rdf *pop;
	size_t ns_count;
	size_t *rtt;

	assert(r != NULL);

	ns_count = ldns_resolver_nameserver_count(r);
	nameservers = ldns_resolver_nameservers(r);
	rtt = ldns_resolver_rtt(r);
	if (ns_count == 0 || !nameservers) {
		return NULL;
	}

	pop = nameservers[ns_count - 1];

	if (ns_count == 1) {
		LDNS_FREE(nameservers);
		LDNS_FREE(rtt);

		ldns_resolver_set_nameservers(r, NULL);
		ldns_resolver_set_rtt(r, NULL);
	} else {
		nameservers = LDNS_XREALLOC(nameservers, ldns_rdf *, 
				(ns_count - 1));
		rtt = LDNS_XREALLOC(rtt, size_t, (ns_count - 1));

	        ldns_resolver_set_nameservers(r, nameservers);
	        ldns_resolver_set_rtt(r, rtt);
	}
	/* decr the count */
	ldns_resolver_dec_nameserver_count(r);
	return pop;
}

ldns_status
ldns_resolver_push_nameserver(ldns_resolver *r, const ldns_rdf *n)
{
	ldns_rdf **nameservers;
	size_t ns_count;
	size_t *rtt;

	if (ldns_rdf_get_type(n) != LDNS_RDF_TYPE_A &&
			ldns_rdf_get_type(n) != LDNS_RDF_TYPE_AAAA) {
		return LDNS_STATUS_ERR;
	}

	ns_count = ldns_resolver_nameserver_count(r);
	nameservers = ldns_resolver_nameservers(r);
	rtt = ldns_resolver_rtt(r);

	/* make room for the next one */
	if (ns_count == 0) {
		nameservers = LDNS_XMALLOC(ldns_rdf *, 1);
	} else {
		nameservers = LDNS_XREALLOC(nameservers, ldns_rdf *, (ns_count + 1));
	}
        if(!nameservers)
                return LDNS_STATUS_MEM_ERR;

	/* set the new value in the resolver */
	ldns_resolver_set_nameservers(r, nameservers);

	/* don't forget the rtt */
	if (ns_count == 0) {
		rtt = LDNS_XMALLOC(size_t, 1);
	} else {
		rtt = LDNS_XREALLOC(rtt, size_t, (ns_count + 1));
	}
        if(!rtt)
                return LDNS_STATUS_MEM_ERR;

	/* slide n in its slot. */
	/* we clone it here, because then we can free the original
	 * rr's where it stood */
	nameservers[ns_count] = ldns_rdf_clone(n);
	rtt[ns_count] = LDNS_RESOLV_RTT_MIN;
	ldns_resolver_incr_nameserver_count(r);
	ldns_resolver_set_rtt(r, rtt);
	return LDNS_STATUS_OK;
}

ldns_status
ldns_resolver_push_nameserver_rr(ldns_resolver *r, const ldns_rr *rr)
{
	ldns_rdf *address;
	if ((!rr) || (ldns_rr_get_type(rr) != LDNS_RR_TYPE_A &&
			ldns_rr_get_type(rr) != LDNS_RR_TYPE_AAAA)) {
		return LDNS_STATUS_ERR;
	}
	address = ldns_rr_rdf(rr, 0); /* extract the ip number */
	if (address) {
		return ldns_resolver_push_nameserver(r, address);
	} else {
		return LDNS_STATUS_ERR;
	}
}

ldns_status
ldns_resolver_push_nameserver_rr_list(ldns_resolver *r, const ldns_rr_list *rrlist)
{
	ldns_rr *rr;
	ldns_status stat;
	size_t i;

	stat = LDNS_STATUS_OK;
	if (rrlist) {
		for(i = 0; i < ldns_rr_list_rr_count(rrlist); i++) {
			rr = ldns_rr_list_rr(rrlist, i);
			if (ldns_resolver_push_nameserver_rr(r, rr) != LDNS_STATUS_OK) {
				stat = LDNS_STATUS_ERR;
				break;
			}
		}
		return stat;
	} else {
		return LDNS_STATUS_ERR;
	}
}

void
ldns_resolver_set_edns_udp_size(ldns_resolver *r, uint16_t s)
{
	        r->_edns_udp_size = s;
}

void
ldns_resolver_set_recursive(ldns_resolver *r, bool re)
{
	r->_recursive = re;
}

void
ldns_resolver_set_dnssec(ldns_resolver *r, bool d)
{
	r->_dnssec = d;
}

void
ldns_resolver_set_dnssec_cd(ldns_resolver *r, bool d)
{
	r->_dnssec_cd = d;
}

void
ldns_resolver_set_dnssec_anchors(ldns_resolver *r, ldns_rr_list * l)
{
  r->_dnssec_anchors = l;
}

ldns_status
ldns_resolver_push_dnssec_anchor(ldns_resolver *r, ldns_rr *rr)
{
  ldns_rr_list * trust_anchors;

  if ((!rr) || (ldns_rr_get_type(rr) != LDNS_RR_TYPE_DNSKEY &&
                ldns_rr_get_type(rr) != LDNS_RR_TYPE_DS)) {

    return LDNS_STATUS_ERR;
  }

  if (!(trust_anchors = ldns_resolver_dnssec_anchors(r))) { /* Initialize */
    trust_anchors = ldns_rr_list_new();
    ldns_resolver_set_dnssec_anchors(r, trust_anchors);
  }

  return (ldns_rr_list_push_rr(trust_anchors, ldns_rr_clone(rr))) ? LDNS_STATUS_OK : LDNS_STATUS_ERR;
}

void
ldns_resolver_set_igntc(ldns_resolver *r, bool i)
{
	r->_igntc = i;
}

void
ldns_resolver_set_usevc(ldns_resolver *r, bool vc)
{
	r->_usevc = vc;
}

void
ldns_resolver_set_debug(ldns_resolver *r, bool d)
{
	r->_debug = d;
}

void
ldns_resolver_set_ip6(ldns_resolver *r, uint8_t ip6)
{
	r->_ip6 = ip6;
}

void
ldns_resolver_set_fail(ldns_resolver *r, bool f)
{
	r->_fail =f;
}

static void
ldns_resolver_set_searchlist_count(ldns_resolver *r, size_t c)
{
	r->_searchlist_count = c;
}

void
ldns_resolver_set_nameserver_count(ldns_resolver *r, size_t c)
{
	r->_nameserver_count = c;
}

void
ldns_resolver_set_dnsrch(ldns_resolver *r, bool d)
{
	r->_dnsrch = d;
}

void
ldns_resolver_set_retry(ldns_resolver *r, uint8_t retry)
{
	r->_retry = retry;
}

void
ldns_resolver_set_retrans(ldns_resolver *r, uint8_t retrans)
{
	r->_retrans = retrans;
}

void
ldns_resolver_set_fallback(ldns_resolver *r, bool fallback)
{
	r->_fallback = fallback;
}

void
ldns_resolver_set_nameservers(ldns_resolver *r, ldns_rdf **n)
{
	r->_nameservers = n;
}

void
ldns_resolver_set_defnames(ldns_resolver *r, bool d)
{
	r->_defnames = d;
}

void
ldns_resolver_set_rtt(ldns_resolver *r, size_t *rtt)
{
	r->_rtt = rtt;
}

void
ldns_resolver_set_nameserver_rtt(ldns_resolver *r, size_t pos, size_t value)
{
	size_t *rtt;

	assert(r != NULL);

	rtt = ldns_resolver_rtt(r);

	if (pos >= ldns_resolver_nameserver_count(r)) {
		/* error ?*/
	} else {
		rtt[pos] = value;
	}

}

void
ldns_resolver_incr_nameserver_count(ldns_resolver *r)
{
	size_t c;

	c = ldns_resolver_nameserver_count(r);
	ldns_resolver_set_nameserver_count(r, ++c);
}

void
ldns_resolver_dec_nameserver_count(ldns_resolver *r)
{
	size_t c;

	c = ldns_resolver_nameserver_count(r);
	if (c == 0) {
		return;
	} else {
		ldns_resolver_set_nameserver_count(r, --c);
	}
}

void
ldns_resolver_set_domain(ldns_resolver *r, ldns_rdf *d)
{
	r->_domain = d;
}

void
ldns_resolver_set_timeout(ldns_resolver *r, struct timeval timeout)
{
	r->_timeout.tv_sec = timeout.tv_sec;
	r->_timeout.tv_usec = timeout.tv_usec;
}

void
ldns_resolver_push_searchlist(ldns_resolver *r, ldns_rdf *d)
{
	ldns_rdf **searchlist;
	size_t list_count;

	if (ldns_rdf_get_type(d) != LDNS_RDF_TYPE_DNAME) {
		return;
	}

	list_count = ldns_resolver_searchlist_count(r);
	searchlist = ldns_resolver_searchlist(r);

	searchlist = LDNS_XREALLOC(searchlist, ldns_rdf *, (list_count + 1));
	if (searchlist) {
		r->_searchlist = searchlist;

		searchlist[list_count] = ldns_rdf_clone(d);
		ldns_resolver_set_searchlist_count(r, list_count + 1);
	} /* no way to report mem err */
}

void
ldns_resolver_set_tsig_keyname(ldns_resolver *r, const char *tsig_keyname)
{
	LDNS_FREE(r->_tsig_keyname);
	r->_tsig_keyname = strdup(tsig_keyname);
}

void
ldns_resolver_set_tsig_algorithm(ldns_resolver *r, const char *tsig_algorithm)
{
	LDNS_FREE(r->_tsig_algorithm);
	r->_tsig_algorithm = strdup(tsig_algorithm);
}

void
ldns_resolver_set_tsig_keydata(ldns_resolver *r, const char *tsig_keydata)
{
	LDNS_FREE(r->_tsig_keydata);
	r->_tsig_keydata = strdup(tsig_keydata);
}

void
ldns_resolver_set_random(ldns_resolver *r, bool b)
{
	r->_random = b;
}

/* more sophisticated functions */
ldns_resolver *
ldns_resolver_new(void)
{
	ldns_resolver *r;

	r = LDNS_MALLOC(ldns_resolver);
	if (!r) {
		return NULL;
	}

	r->_searchlist = NULL;
	r->_nameservers = NULL;
	r->_rtt = NULL;

	/* defaults are filled out */
	ldns_resolver_set_searchlist_count(r, 0);
	ldns_resolver_set_nameserver_count(r, 0);
	ldns_resolver_set_usevc(r, 0);
	ldns_resolver_set_port(r, LDNS_PORT);
	ldns_resolver_set_domain(r, NULL);
	ldns_resolver_set_defnames(r, false);
	ldns_resolver_set_retry(r, 3);
	ldns_resolver_set_retrans(r, 2);
	ldns_resolver_set_fallback(r, true);
	ldns_resolver_set_fail(r, false);
	ldns_resolver_set_edns_udp_size(r, 0);
	ldns_resolver_set_dnssec(r, false);
	ldns_resolver_set_dnssec_cd(r, false);
	ldns_resolver_set_dnssec_anchors(r, NULL);
	ldns_resolver_set_ip6(r, LDNS_RESOLV_INETANY);
	ldns_resolver_set_igntc(r, false);
	ldns_resolver_set_recursive(r, false);
	ldns_resolver_set_dnsrch(r, true);
	ldns_resolver_set_source(r, NULL);
	ldns_resolver_set_ixfr_serial(r, 0);

	/* randomize the nameserver to be queried
	 * when there are multiple
	 */
	ldns_resolver_set_random(r, true);

	ldns_resolver_set_debug(r, 0);

	r->_timeout.tv_sec = LDNS_DEFAULT_TIMEOUT_SEC;
	r->_timeout.tv_usec = LDNS_DEFAULT_TIMEOUT_USEC;

	r->_socket = -1;
	r->_axfr_soa_count = 0;
	r->_axfr_i = 0;
	r->_cur_axfr_pkt = NULL;

	r->_tsig_keyname = NULL;
	r->_tsig_keydata = NULL;
	r->_tsig_algorithm = NULL;
	return r;
}

ldns_resolver *
ldns_resolver_clone(ldns_resolver *src)
{
	ldns_resolver *dst;
	size_t i;

	assert(src != NULL);

	if (!(dst = LDNS_MALLOC(ldns_resolver))) return NULL;
	(void) memcpy(dst, src, sizeof(ldns_resolver));

	if (dst->_searchlist_count == 0)
		dst->_searchlist = NULL;
	else {
		if (!(dst->_searchlist =
		    LDNS_XMALLOC(ldns_rdf *, dst->_searchlist_count)))
			goto error;
		for (i = 0; i < dst->_searchlist_count; i++)
			if (!(dst->_searchlist[i] =
			    ldns_rdf_clone(src->_searchlist[i]))) {
				dst->_searchlist_count = i;
				goto error_searchlist;
			}
	}
	if (dst->_nameserver_count == 0) {
		dst->_nameservers = NULL;
		dst->_rtt = NULL;
	} else {
		if (!(dst->_nameservers =
		    LDNS_XMALLOC(ldns_rdf *, dst->_nameserver_count)))
			goto error_searchlist;
		for (i = 0; i < dst->_nameserver_count; i++)
			if (!(dst->_nameservers[i] =
			    ldns_rdf_clone(src->_nameservers[i]))) {
				dst->_nameserver_count = i;
				goto error_nameservers;
			}
		if (!(dst->_rtt =
		    LDNS_XMALLOC(size_t, dst->_nameserver_count)))
			goto error_nameservers;
		(void) memcpy(dst->_rtt, src->_rtt,
		    sizeof(size_t) * dst->_nameserver_count);
	}
	if (dst->_domain && (!(dst->_domain = ldns_rdf_clone(src->_domain))))
		goto error_rtt;

	if (dst->_tsig_keyname &&
	    (!(dst->_tsig_keyname = strdup(src->_tsig_keyname))))
		goto error_domain;

	if (dst->_tsig_keydata &&
	    (!(dst->_tsig_keydata = strdup(src->_tsig_keydata))))
		goto error_tsig_keyname;

	if (dst->_tsig_algorithm &&
	    (!(dst->_tsig_algorithm = strdup(src->_tsig_algorithm))))
		goto error_tsig_keydata;

	if (dst->_cur_axfr_pkt &&
	    (!(dst->_cur_axfr_pkt = ldns_pkt_clone(src->_cur_axfr_pkt))))
		goto error_tsig_algorithm;

	if (dst->_dnssec_anchors &&
	    (!(dst->_dnssec_anchors=ldns_rr_list_clone(src->_dnssec_anchors))))
		goto error_cur_axfr_pkt;

	return dst;

error_cur_axfr_pkt:
	ldns_pkt_free(dst->_cur_axfr_pkt);
error_tsig_algorithm:
	LDNS_FREE(dst->_tsig_algorithm);
error_tsig_keydata:
	LDNS_FREE(dst->_tsig_keydata);
error_tsig_keyname:
	LDNS_FREE(dst->_tsig_keyname);
error_domain:
	ldns_rdf_deep_free(dst->_domain);
error_rtt:
	LDNS_FREE(dst->_rtt);
error_nameservers:
	for (i = 0; i < dst->_nameserver_count; i++)
		ldns_rdf_deep_free(dst->_nameservers[i]);
	LDNS_FREE(dst->_nameservers);
error_searchlist:
	for (i = 0; i < dst->_searchlist_count; i++)
		ldns_rdf_deep_free(dst->_searchlist[i]);
	LDNS_FREE(dst->_searchlist);
error:
	LDNS_FREE(dst);
	return NULL;
}


ldns_status
ldns_resolver_new_frm_fp(ldns_resolver **res, FILE *fp)
{
	return ldns_resolver_new_frm_fp_l(res, fp, NULL);
}

ldns_status
ldns_resolver_new_frm_fp_l(ldns_resolver **res, FILE *fp, int *line_nr)
{
	ldns_resolver *r;
	const char *keyword[LDNS_RESOLV_KEYWORDS];
	char word[LDNS_MAX_LINELEN + 1];
	int8_t expect;
	uint8_t i;
	ldns_rdf *tmp;
#ifdef HAVE_SSL
	ldns_rr *tmp_rr;
#endif
	ssize_t gtr, bgtr;
	ldns_buffer *b;
        int lnr = 0, oldline;
	FILE* myfp = fp;
        if(!line_nr) line_nr = &lnr;

	if(!fp) {
		myfp = fopen("/etc/resolv.conf", "r");
		if(!myfp)
			return LDNS_STATUS_FILE_ERR;
	}

	/* do this better
	 * expect =
	 * 0: keyword
	 * 1: default domain dname
	 * 2: NS aaaa or a record
	 */

	/* recognized keywords */
	keyword[LDNS_RESOLV_NAMESERVER] = "nameserver";
	keyword[LDNS_RESOLV_DEFDOMAIN] = "domain";
	keyword[LDNS_RESOLV_SEARCH] = "search";
	/* these two are read but not used atm TODO */
	keyword[LDNS_RESOLV_SORTLIST] = "sortlist";
	keyword[LDNS_RESOLV_OPTIONS] = "options";
	keyword[LDNS_RESOLV_ANCHOR] = "anchor";
	expect = LDNS_RESOLV_KEYWORD;

	r = ldns_resolver_new();
	if (!r) {
		if(!fp) fclose(myfp);
		return LDNS_STATUS_MEM_ERR;
	}

	gtr = 1;
	word[0] = 0;
        oldline = *line_nr;
        expect = LDNS_RESOLV_KEYWORD;
	while (gtr > 0) {
		/* check comments */
		if (word[0] == '#') {
                        word[0]='x';
                        if(oldline == *line_nr) {
                                /* skip until end of line */
                                int c;
                                do {
                                        c = fgetc(myfp);
                                } while(c != EOF && c != '\n');
                                if(c=='\n') (*line_nr)++;
                        }
			/* and read next to prepare for further parsing */
                        oldline = *line_nr;
			continue;
		}
                oldline = *line_nr;
		switch(expect) {
			case LDNS_RESOLV_KEYWORD:
				/* keyword */
				gtr = ldns_fget_token_l(myfp, word, LDNS_PARSE_NORMAL, 0, line_nr);
				if (gtr != 0) {
                                        if(word[0] == '#') continue;
					for(i = 0; i < LDNS_RESOLV_KEYWORDS; i++) {
						if (strcasecmp(keyword[i], word) == 0) {
							/* chosen the keyword and
							 * expect values carefully
	        					 */
							expect = i;
							break;
						}
					}
					/* no keyword recognized */
					if (expect == LDNS_RESOLV_KEYWORD) {
						/* skip line */
						/*
						ldns_resolver_deep_free(r);
						if(!fp) fclose(myfp);
						return LDNS_STATUS_SYNTAX_KEYWORD_ERR;
						*/
					}
				}
				break;
			case LDNS_RESOLV_DEFDOMAIN:
				/* default domain dname */
				gtr = ldns_fget_token_l(myfp, word, LDNS_PARSE_NORMAL, 0, line_nr);
				if (gtr == 0) {
					if(!fp) fclose(myfp);
					return LDNS_STATUS_SYNTAX_MISSING_VALUE_ERR;
				}
                                if(word[0] == '#') {
                                        expect = LDNS_RESOLV_KEYWORD;
                                        continue;
                                }
				tmp = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_DNAME, word);
				if (!tmp) {
					ldns_resolver_deep_free(r);
					if(!fp) fclose(myfp);
					return LDNS_STATUS_SYNTAX_DNAME_ERR;
				}

				/* DOn't free, because we copy the pointer */
				ldns_resolver_set_domain(r, tmp);
				expect = LDNS_RESOLV_KEYWORD;
				break;
			case LDNS_RESOLV_NAMESERVER:
				/* NS aaaa or a record */
				gtr = ldns_fget_token_l(myfp, word, LDNS_PARSE_NORMAL, 0, line_nr);
				if (gtr == 0) {
					if(!fp) fclose(myfp);
					return LDNS_STATUS_SYNTAX_MISSING_VALUE_ERR;
				}
                                if(word[0] == '#') {
                                        expect = LDNS_RESOLV_KEYWORD;
                                        continue;
                                }
                                if(strchr(word, '%')) {
                                        /* snip off interface labels,
                                         * fe80::222:19ff:fe31:4222%eth0 */
                                        strchr(word, '%')[0]=0;
                                }
				tmp = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_AAAA, word);
				if (!tmp) {
					/* try ip4 */
					tmp = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_A, word);
				}
				/* could not parse it, exit */
				if (!tmp) {
					ldns_resolver_deep_free(r);
					if(!fp) fclose(myfp);
					return LDNS_STATUS_SYNTAX_ERR;
				}
				(void)ldns_resolver_push_nameserver(r, tmp);
				ldns_rdf_deep_free(tmp);
				expect = LDNS_RESOLV_KEYWORD;
				break;
			case LDNS_RESOLV_SEARCH:
				/* search list domain dname */
				gtr = ldns_fget_token_l(myfp, word, LDNS_PARSE_SKIP_SPACE, 0, line_nr);
				b = LDNS_MALLOC(ldns_buffer);
				if(!b) {
					ldns_resolver_deep_free(r);
					if(!fp) fclose(myfp);
					return LDNS_STATUS_MEM_ERR;
				}

				ldns_buffer_new_frm_data(b, word, (size_t) gtr);
				if(ldns_buffer_status(b) != LDNS_STATUS_OK) {
					LDNS_FREE(b);
					ldns_resolver_deep_free(r);
					if(!fp) fclose(myfp);
					return LDNS_STATUS_MEM_ERR;
				}
				bgtr = ldns_bget_token(b, word, LDNS_PARSE_NORMAL, (size_t) gtr + 1);
				while (bgtr > 0) {
					gtr -= bgtr;
                                        if(word[0] == '#') {
                                                expect = LDNS_RESOLV_KEYWORD;
                                                break;
                                        }
					tmp = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_DNAME, word);
					if (!tmp) {
						ldns_resolver_deep_free(r);
						ldns_buffer_free(b);
						if(!fp) fclose(myfp);
						return LDNS_STATUS_SYNTAX_DNAME_ERR;
					}

					ldns_resolver_push_searchlist(r, tmp);

					ldns_rdf_deep_free(tmp);
					bgtr = ldns_bget_token(b, word, LDNS_PARSE_NORMAL,
					    (size_t) gtr + 1);
				}
				ldns_buffer_free(b);
				if (expect != LDNS_RESOLV_KEYWORD) {
					gtr = 1;
					expect = LDNS_RESOLV_KEYWORD;
				}
				break;
			case LDNS_RESOLV_SORTLIST:
				gtr = ldns_fget_token_l(myfp, word, LDNS_PARSE_SKIP_SPACE, 0, line_nr);
				/* sortlist not implemented atm */
				expect = LDNS_RESOLV_KEYWORD;
				break;
			case LDNS_RESOLV_OPTIONS:
				gtr = ldns_fget_token_l(myfp, word, LDNS_PARSE_SKIP_SPACE, 0, line_nr);
				/* options not implemented atm */
				expect = LDNS_RESOLV_KEYWORD;
				break;
			case LDNS_RESOLV_ANCHOR:
				/* a file containing a DNSSEC trust anchor */
				gtr = ldns_fget_token_l(myfp, word, LDNS_PARSE_NORMAL, 0, line_nr);
				if (gtr == 0) {
					ldns_resolver_deep_free(r);
					if(!fp) fclose(myfp);
					return LDNS_STATUS_SYNTAX_MISSING_VALUE_ERR;
				}
                                if(word[0] == '#') {
                                        expect = LDNS_RESOLV_KEYWORD;
                                        continue;
                                }

#ifdef HAVE_SSL
				tmp_rr = ldns_read_anchor_file(word);
				(void) ldns_resolver_push_dnssec_anchor(r, tmp_rr);
				ldns_rr_free(tmp_rr);
#endif
				expect = LDNS_RESOLV_KEYWORD;
				break;
		}
	}

	if(!fp)
		fclose(myfp);

	if (res) {
		*res = r;
		return LDNS_STATUS_OK;
	} else {
		ldns_resolver_deep_free(r);
		return LDNS_STATUS_NULL;
	}
}

ldns_status
ldns_resolver_new_frm_file(ldns_resolver **res, const char *filename)
{
	ldns_resolver *r;
	FILE *fp;
	ldns_status s;

	if (!filename) {
		fp = fopen(LDNS_RESOLV_CONF, "r");

	} else {
		fp = fopen(filename, "r");
	}
	if (!fp) {
		return LDNS_STATUS_FILE_ERR;
	}

	s = ldns_resolver_new_frm_fp(&r, fp);
	fclose(fp);
	if (s == LDNS_STATUS_OK) {
		if (res) {
			*res = r;
			return LDNS_STATUS_OK;
		} else  {
			ldns_resolver_free(r);
			return LDNS_STATUS_NULL;
		}
	}
	return s;
}

void
ldns_resolver_free(ldns_resolver *res)
{
	LDNS_FREE(res);
}

void
ldns_resolver_deep_free(ldns_resolver *res)
{
	size_t i;

	if (res) {
		close_socket(res->_socket);

		if (res->_searchlist) {
			for (i = 0; i < ldns_resolver_searchlist_count(res); i++) {
				ldns_rdf_deep_free(res->_searchlist[i]);
			}
			LDNS_FREE(res->_searchlist);
		}
		if (res->_nameservers) {
			for (i = 0; i < res->_nameserver_count; i++) {
				ldns_rdf_deep_free(res->_nameservers[i]);
			}
			LDNS_FREE(res->_nameservers);
		}
		if (ldns_resolver_domain(res)) {
			ldns_rdf_deep_free(ldns_resolver_domain(res));
		}
		if (res->_tsig_keyname) {
			LDNS_FREE(res->_tsig_keyname);
		}
		if (res->_tsig_keydata) {
			LDNS_FREE(res->_tsig_keydata);
		}
		if (res->_tsig_algorithm) {
			LDNS_FREE(res->_tsig_algorithm);
		}

		if (res->_cur_axfr_pkt) {
			ldns_pkt_free(res->_cur_axfr_pkt);
		}

		if (res->_rtt) {
			LDNS_FREE(res->_rtt);
		}
		if (res->_dnssec_anchors) {
			ldns_rr_list_deep_free(res->_dnssec_anchors);
		}
		LDNS_FREE(res);
	}
}

ldns_status
ldns_resolver_search_status(ldns_pkt** pkt,
		ldns_resolver *r, const  ldns_rdf *name,
		ldns_rr_type t, ldns_rr_class c, uint16_t flags)
{
	ldns_rdf *new_name;
	ldns_rdf **search_list;
	size_t i;
	ldns_status s = LDNS_STATUS_OK;
	ldns_rdf root_dname = { 1, LDNS_RDF_TYPE_DNAME, (void *)"" };

	if (ldns_dname_absolute(name)) {
		/* query as-is */
		return ldns_resolver_query_status(pkt, r, name, t, c, flags);
	} else if (ldns_resolver_dnsrch(r)) {
		search_list = ldns_resolver_searchlist(r);
		for (i = 0; i <= ldns_resolver_searchlist_count(r); i++) {
			if (i == ldns_resolver_searchlist_count(r)) {
				new_name = ldns_dname_cat_clone(name,
						&root_dname);
			} else {
				new_name = ldns_dname_cat_clone(name,
						search_list[i]);
			}

			s = ldns_resolver_query_status(pkt, r,
					new_name, t, c, flags);
			ldns_rdf_free(new_name);
			if (pkt && *pkt) {
				if (s == LDNS_STATUS_OK && 
						ldns_pkt_get_rcode(*pkt) ==
						LDNS_RCODE_NOERROR) {

					return LDNS_STATUS_OK;
				}
				ldns_pkt_free(*pkt);
				*pkt = NULL;
			}
		}
	}
	return s;
}

ldns_pkt *
ldns_resolver_search(const ldns_resolver *r,const  ldns_rdf *name,
	ldns_rr_type t, ldns_rr_class c, uint16_t flags)
{
	ldns_pkt* pkt = NULL;
	if (ldns_resolver_search_status(&pkt, (ldns_resolver *)r,
				name, t, c, flags) != LDNS_STATUS_OK) {
		ldns_pkt_free(pkt);
	}
	return pkt;
}

ldns_status
ldns_resolver_query_status(ldns_pkt** pkt,
		ldns_resolver *r, const ldns_rdf *name,
		ldns_rr_type t, ldns_rr_class c, uint16_t flags)
{
	ldns_rdf *newname;
	ldns_status status;

	if (!ldns_resolver_defnames(r) || !ldns_resolver_domain(r)) {
		return ldns_resolver_send(pkt, r, name, t, c, flags);
	}

	newname = ldns_dname_cat_clone(name, ldns_resolver_domain(r));
	if (!newname) {
		return LDNS_STATUS_MEM_ERR;
	}
	status = ldns_resolver_send(pkt, r, newname, t, c, flags);
	ldns_rdf_free(newname);
	return status;
}

ldns_pkt *
ldns_resolver_query(const ldns_resolver *r, const ldns_rdf *name,
	ldns_rr_type t, ldns_rr_class c, uint16_t flags)
{
	ldns_pkt* pkt = NULL;
	if (ldns_resolver_query_status(&pkt, (ldns_resolver *)r,
				name, t, c, flags) != LDNS_STATUS_OK) {
		ldns_pkt_free(pkt);
	}
	return pkt;
}

static size_t *
ldns_resolver_backup_rtt(ldns_resolver *r)
{
	size_t *new_rtt;
	size_t *old_rtt = ldns_resolver_rtt(r);

	if (old_rtt && ldns_resolver_nameserver_count(r)) {
		new_rtt = LDNS_XMALLOC(size_t
				, ldns_resolver_nameserver_count(r));
		memcpy(new_rtt, old_rtt, sizeof(size_t)
				* ldns_resolver_nameserver_count(r));
		ldns_resolver_set_rtt(r, new_rtt);
		return old_rtt;
	}
	return NULL;
}

static void
ldns_resolver_restore_rtt(ldns_resolver *r, size_t *old_rtt)
{
	size_t *cur_rtt = ldns_resolver_rtt(r);

	if (cur_rtt) {
		LDNS_FREE(cur_rtt);
	}
	ldns_resolver_set_rtt(r, old_rtt);
}

ldns_status
ldns_resolver_send_pkt(ldns_pkt **answer, ldns_resolver *r,
				   ldns_pkt *query_pkt)
{
	ldns_pkt *answer_pkt = NULL;
	ldns_status stat = LDNS_STATUS_OK;
	size_t *rtt;

	stat = ldns_send(&answer_pkt, (ldns_resolver *)r, query_pkt);
	if (stat != LDNS_STATUS_OK) {
		if(answer_pkt) {
			ldns_pkt_free(answer_pkt);
			answer_pkt = NULL;
		}
	} else {
		/* if tc=1 fall back to EDNS and/or TCP */
		/* check for tcp first (otherwise we don't care about tc=1) */
		if (!ldns_resolver_usevc(r) && ldns_resolver_fallback(r)) {
			if (ldns_pkt_tc(answer_pkt)) {
				/* was EDNS0 set? */
				if (ldns_pkt_edns_udp_size(query_pkt) == 0) {
					ldns_pkt_set_edns_udp_size(query_pkt
							, 4096);
					ldns_pkt_free(answer_pkt);
					answer_pkt = NULL;
					/* Nameservers should not become 
					 * unreachable because fragments are
					 * dropped (network error). We might
					 * still have success with TCP.
					 * Therefore maintain reachability
					 * statuses of the nameservers by
					 * backup and restore the rtt list.
					 */
					rtt = ldns_resolver_backup_rtt(r);
					stat = ldns_send(&answer_pkt, r
							, query_pkt);
					ldns_resolver_restore_rtt(r, rtt);
				}
				/* either way, if it is still truncated, use TCP */
				if (stat != LDNS_STATUS_OK ||
				    ldns_pkt_tc(answer_pkt)) {
					ldns_resolver_set_usevc(r, true);
					ldns_pkt_free(answer_pkt);
					stat = ldns_send(&answer_pkt, r, query_pkt);
					ldns_resolver_set_usevc(r, false);
				}
			}
		}
	}

	if (answer) {
		*answer = answer_pkt;
	}

	return stat;
}

ldns_status
ldns_resolver_prepare_query_pkt(ldns_pkt **query_pkt, ldns_resolver *r,
                                const ldns_rdf *name, ldns_rr_type t,
                                ldns_rr_class c, uint16_t flags)
{
	struct timeval now;
	ldns_rr* soa = NULL;

	/* prepare a question pkt from the parameters
	 * and then send this */
	if (t == LDNS_RR_TYPE_IXFR) {
		ldns_rdf *owner_rdf;
		ldns_rdf *mname_rdf;
		ldns_rdf *rname_rdf;
		ldns_rdf *serial_rdf;
		ldns_rdf *refresh_rdf;
		ldns_rdf *retry_rdf;
		ldns_rdf *expire_rdf;
		ldns_rdf *minimum_rdf;
		soa = ldns_rr_new();

		if (!soa) {
			return LDNS_STATUS_ERR;
		}
		owner_rdf = ldns_rdf_clone(name);
		if (!owner_rdf) {
			ldns_rr_free(soa);
			return LDNS_STATUS_ERR;
		}
		ldns_rr_set_owner(soa, owner_rdf);
		ldns_rr_set_type(soa, LDNS_RR_TYPE_SOA);
		ldns_rr_set_class(soa, c);
		ldns_rr_set_question(soa, false);
		if (ldns_str2rdf_dname(&mname_rdf, ".") != LDNS_STATUS_OK) {
			ldns_rr_free(soa);
			return LDNS_STATUS_ERR;
		} else	ldns_rr_push_rdf(soa, mname_rdf);
		if (ldns_str2rdf_dname(&rname_rdf, ".") != LDNS_STATUS_OK) {
			ldns_rr_free(soa);
			return LDNS_STATUS_ERR;
		} else	ldns_rr_push_rdf(soa, rname_rdf);
		serial_rdf = ldns_native2rdf_int32(LDNS_RDF_TYPE_INT32, ldns_resolver_get_ixfr_serial(r));
		if (!serial_rdf) {
			ldns_rr_free(soa);
			return LDNS_STATUS_ERR;
		} else	ldns_rr_push_rdf(soa, serial_rdf);
		refresh_rdf = ldns_native2rdf_int32(LDNS_RDF_TYPE_INT32, 0);
		if (!refresh_rdf) {
			ldns_rr_free(soa);
			return LDNS_STATUS_ERR;
		} else	ldns_rr_push_rdf(soa, refresh_rdf);
		retry_rdf = ldns_native2rdf_int32(LDNS_RDF_TYPE_INT32, 0);
		if (!retry_rdf) {
			ldns_rr_free(soa);
			return LDNS_STATUS_ERR;
		} else	ldns_rr_push_rdf(soa, retry_rdf);
		expire_rdf = ldns_native2rdf_int32(LDNS_RDF_TYPE_INT32, 0);
		if (!expire_rdf) {
			ldns_rr_free(soa);
			return LDNS_STATUS_ERR;
		} else	ldns_rr_push_rdf(soa, expire_rdf);
		minimum_rdf = ldns_native2rdf_int32(LDNS_RDF_TYPE_INT32, 0);
		if (!minimum_rdf) {
			ldns_rr_free(soa);
			return LDNS_STATUS_ERR;
		} else	ldns_rr_push_rdf(soa, minimum_rdf);

		*query_pkt = ldns_pkt_ixfr_request_new(ldns_rdf_clone(name),
			c, flags, soa);
	} else {
		*query_pkt = ldns_pkt_query_new(ldns_rdf_clone(name), t, c, flags);
	}
	if (!*query_pkt) {
		ldns_rr_free(soa);
		return LDNS_STATUS_ERR;
	}

	/* set DO bit if necessary */
	if (ldns_resolver_dnssec(r)) {
		if (ldns_resolver_edns_udp_size(r) == 0) {
			ldns_resolver_set_edns_udp_size(r, 4096);
		}
		ldns_pkt_set_edns_do(*query_pkt, true);
		if (ldns_resolver_dnssec_cd(r) || (flags & LDNS_CD)) {
			ldns_pkt_set_cd(*query_pkt, true);
		}
	}

	/* transfer the udp_edns_size from the resolver to the packet */
	if (ldns_resolver_edns_udp_size(r) != 0) {
		ldns_pkt_set_edns_udp_size(*query_pkt, ldns_resolver_edns_udp_size(r));
	}

	/* set the timestamp */
	now.tv_sec = time(NULL);
	now.tv_usec = 0;
	ldns_pkt_set_timestamp(*query_pkt, now);


	if (ldns_resolver_debug(r)) {
		ldns_pkt_print(stdout, *query_pkt);
	}

	/* only set the id if it is not set yet */
	if (ldns_pkt_id(*query_pkt) == 0) {
		ldns_pkt_set_random_id(*query_pkt);
	}

	return LDNS_STATUS_OK;
}

ldns_status
ldns_resolver_send(ldns_pkt **answer, ldns_resolver *r, const ldns_rdf *name,
		ldns_rr_type t, ldns_rr_class c, uint16_t flags)
{
	ldns_pkt *query_pkt;
	ldns_pkt *answer_pkt;
	ldns_status status;

	assert(r != NULL);
	assert(name != NULL);

	answer_pkt = NULL;

	/* do all the preprocessing here, then fire of an query to
	 * the network */

	if (0 == t) {
		t= LDNS_RR_TYPE_A;
	}
	if (0 == c) {
		c= LDNS_RR_CLASS_IN;
	}
	if (0 == ldns_resolver_nameserver_count(r)) {
		return LDNS_STATUS_RES_NO_NS;
	}
	if (ldns_rdf_get_type(name) != LDNS_RDF_TYPE_DNAME) {
		return LDNS_STATUS_RES_QUERY;
	}

	status = ldns_resolver_prepare_query_pkt(&query_pkt, r, name,
	                                         t, c, flags);
	if (status != LDNS_STATUS_OK) {
		return status;
	}

	/* if tsig values are set, tsign it */
	/* TODO: make last 3 arguments optional too? maybe make complete
	         rr instead of separate values in resolver (and packet)
	  Jelte
	  should this go in pkt_prepare?
	*/
	if (ldns_resolver_tsig_keyname(r) && ldns_resolver_tsig_keydata(r)) {
#ifdef HAVE_SSL
		status = ldns_pkt_tsig_sign(query_pkt,
		                            ldns_resolver_tsig_keyname(r),
		                            ldns_resolver_tsig_keydata(r),
		                            300, ldns_resolver_tsig_algorithm(r), NULL);
		if (status != LDNS_STATUS_OK) {
			ldns_pkt_free(query_pkt);
			return LDNS_STATUS_CRYPTO_TSIG_ERR;
		}
#else
		ldns_pkt_free(query_pkt);
	        return LDNS_STATUS_CRYPTO_TSIG_ERR;
#endif /* HAVE_SSL */
	}

	status = ldns_resolver_send_pkt(&answer_pkt, r, query_pkt);
	ldns_pkt_free(query_pkt);

	/* allows answer to be NULL when not interested in return value */
	if (answer) {
		*answer = answer_pkt;
	}
	return status;
}

ldns_rr *
ldns_axfr_next(ldns_resolver *resolver)
{
	ldns_rr *cur_rr;
	uint8_t *packet_wire;
	size_t packet_wire_size;
	ldns_status status;

	/* check if start() has been called */
	if (!resolver || resolver->_socket == -1) {
		return NULL;
	}

	if (resolver->_cur_axfr_pkt) {
		if (resolver->_axfr_i == ldns_pkt_ancount(resolver->_cur_axfr_pkt)) {
			ldns_pkt_free(resolver->_cur_axfr_pkt);
			resolver->_cur_axfr_pkt = NULL;
			return ldns_axfr_next(resolver);
		}
		cur_rr = ldns_rr_clone(ldns_rr_list_rr(
					ldns_pkt_answer(resolver->_cur_axfr_pkt),
					resolver->_axfr_i));
		resolver->_axfr_i++;
		if (ldns_rr_get_type(cur_rr) == LDNS_RR_TYPE_SOA) {
			resolver->_axfr_soa_count++;
			if (resolver->_axfr_soa_count >= 2) {

				close_socket(resolver->_socket);

				ldns_pkt_free(resolver->_cur_axfr_pkt);
				resolver->_cur_axfr_pkt = NULL;
			}
		}
		return cur_rr;
	} else {
               packet_wire = ldns_tcp_read_wire_timeout(resolver->_socket, &packet_wire_size, resolver->_timeout);
		if(!packet_wire)
			return NULL;

		status = ldns_wire2pkt(&resolver->_cur_axfr_pkt, packet_wire,
				     packet_wire_size);
		LDNS_FREE(packet_wire);

		resolver->_axfr_i = 0;
		if (status != LDNS_STATUS_OK) {
			/* TODO: make status return type of this function (...api change) */
#ifdef STDERR_MSGS
			fprintf(stderr, "Error parsing rr during AXFR: %s\n", ldns_get_errorstr_by_id(status));
#endif

			/* we must now also close the socket, otherwise subsequent uses of the
			   same resolver structure will fail because the link is still open or
			   in an undefined state */

			close_socket(resolver->_socket);

			return NULL;
		} else if (ldns_pkt_get_rcode(resolver->_cur_axfr_pkt) != 0) {
#ifdef STDERR_MSGS
			ldns_lookup_table *rcode = ldns_lookup_by_id(
				ldns_rcodes,(int) ldns_pkt_get_rcode(
				resolver->_cur_axfr_pkt));
			if (rcode) {
				fprintf(stderr, "Error in AXFR: %s\n", 
						rcode->name);
			} else {
				fprintf(stderr, "Error in AXFR: %d\n", 
						(int) ldns_pkt_get_rcode(
						resolver->_cur_axfr_pkt));
			}
#endif

			/* we must now also close the socket, otherwise subsequent uses of the
			   same resolver structure will fail because the link is still open or
			   in an undefined state */

			close_socket(resolver->_socket);

			return NULL;
		} else {
			return ldns_axfr_next(resolver);
		}

	}

}

/* this function is needed to abort a transfer that is in progress;
 * without it an aborted transfer will lead to the AXFR code in the
 * library staying in an indetermined state because the socket for the
 * AXFR is never closed
 */
void
ldns_axfr_abort(ldns_resolver *resolver)
{
	/* Only abort if an actual AXFR is in progress */
	if (resolver->_socket != 0)
	{
#ifndef USE_WINSOCK
		close(resolver->_socket);
#else
		closesocket(resolver->_socket);
#endif
		resolver->_socket = 0;
	}
}

bool
ldns_axfr_complete(const ldns_resolver *res)
{
	/* complete when soa count is 2? */
	return res->_axfr_soa_count == 2;
}

ldns_pkt *
ldns_axfr_last_pkt(const ldns_resolver *res)
{
	return res->_cur_axfr_pkt;
}

void
ldns_resolver_set_ixfr_serial(ldns_resolver *r, uint32_t serial)
{
	r->_serial = serial;
}

uint32_t
ldns_resolver_get_ixfr_serial(const ldns_resolver *res)
{
	return res->_serial;
}


/* random isn't really that good */
void
ldns_resolver_nameservers_randomize(ldns_resolver *r)
{
	uint16_t i, j;
	ldns_rdf **ns, *tmpns;
	size_t *rtt, tmprtt;

	/* should I check for ldns_resolver_random?? */
	assert(r != NULL);

	ns = ldns_resolver_nameservers(r);
	rtt = ldns_resolver_rtt(r);
	for (i = 0; i < ldns_resolver_nameserver_count(r); i++) {
		j = ldns_get_random() % ldns_resolver_nameserver_count(r);
		tmpns = ns[i];
		ns[i] = ns[j];
		ns[j] = tmpns;
		tmprtt = rtt[i];
		rtt[i] = rtt[j];
		rtt[j] = tmprtt;
	}
	ldns_resolver_set_nameservers(r, ns);
}

