/*-
 * (c) Magerya Vitaly
 *
 * Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved. This file is offered as-is,
 * without any warranty.
 */

#include <ldns/ldns.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* General utilities.
 */

static char *progname;

#define countof(array) (sizeof(array)/sizeof(*(array)))

static void
die(int code, const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    fprintf(stderr, "%s: ", progname);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(code);
}

static int
ndots(const char *name) {
    int n;

    for (n = 0; (name = strchr(name, '.')); n++, name++);
    return n;
}

/* General LDNS-specific utilities.
 */

static ldns_status
ldns_resolver_new_default(ldns_resolver **res) {
    if (ldns_resolver_new_frm_file(res, NULL) == LDNS_STATUS_OK ||
        (*res = ldns_resolver_new()) != NULL)
        return LDNS_STATUS_OK;
    return LDNS_STATUS_MEM_ERR;
}

static ldns_status
ldns_resolver_push_default_servers(ldns_resolver *res) {
    ldns_status status;
    ldns_rdf *addr;

    if ((status = ldns_str2rdf_a(&addr, "127.0.0.1")) != LDNS_STATUS_OK ||
        (status = ldns_resolver_push_nameserver(res, addr)) != LDNS_STATUS_OK)
        return ldns_rdf_deep_free(addr), status;
    ldns_rdf_deep_free(addr);
    if ((status = ldns_str2rdf_aaaa(&addr, "::1")) != LDNS_STATUS_OK ||
        (status = ldns_resolver_push_nameserver(res, addr)) != LDNS_STATUS_OK)
        return ldns_rdf_deep_free(addr), status;
    ldns_rdf_deep_free(addr);
    return LDNS_STATUS_OK;
}

static ldns_rdf *
ldns_rdf_new_addr_frm_str(const char *str) {
    ldns_rdf *addr;

    if ((addr = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_A, str)) == NULL)
        addr = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_AAAA, str);
    return addr;
}

static void
ldns_resolver_remove_nameservers(ldns_resolver *res) {
    while (ldns_resolver_nameserver_count(res) > 0)
        ldns_rdf_deep_free(ldns_resolver_pop_nameserver(res));
}

static ldns_rdf *
ldns_rdf_reverse_a(ldns_rdf *addr, const char *base) {
    char *buf;
    int i, len;

    len = strlen(base);
    buf = alloca(LDNS_IP4ADDRLEN*4 + len + 1);
    for (len = i = 0; i < LDNS_IP4ADDRLEN; i++)
        len += sprintf(&buf[len], "%d.",
            (int)ldns_rdf_data(addr)[LDNS_IP4ADDRLEN - i - 1]);
    sprintf(&buf[len], "%s", base);
    return ldns_dname_new_frm_str(buf);
}

static ldns_rdf *
ldns_rdf_reverse_aaaa(ldns_rdf *addr, const char *base) {
    char *buf;
    int i, len;

    len = strlen(base);
    buf = alloca(LDNS_IP6ADDRLEN*4 + len + 1);
    for (i = 0; i < LDNS_IP6ADDRLEN; i++) {
        uint8_t byte = ldns_rdf_data(addr)[LDNS_IP6ADDRLEN - i - 1];
        sprintf(&buf[i*4], "%x.%x.", byte & 0x0F, byte >> 4);
    }
    sprintf(&buf[LDNS_IP6ADDRLEN*4], "%s", base);
    return ldns_dname_new_frm_str(buf);
}

static ldns_status
ldns_pkt_push_rr_soa(ldns_pkt *pkt, ldns_pkt_section sec,
    const ldns_rdf *name, ldns_rr_class c, uint32_t serial) {
    ldns_rdf *rdf;
    ldns_rr *rr;
    uint32_t n;

    if ((rr = ldns_rr_new_frm_type(LDNS_RR_TYPE_SOA)) == NULL)
        return LDNS_STATUS_MEM_ERR;
    ldns_rr_set_class(rr, c);
    ldns_rr_set_owner(rr, ldns_rdf_clone(name));
    ldns_rr_set_ttl(rr, 0);

    n = 0;
    if ((rdf = ldns_rdf_new_frm_data(LDNS_RDF_TYPE_DNAME, 1, &n)) == NULL)
        goto memerr;
    ldns_rr_set_rdf(rr, rdf, 0);
    ldns_rr_set_rdf(rr, ldns_rdf_clone(rdf), 1);

    n = htonl(serial);
    if ((rdf = ldns_rdf_new_frm_data(LDNS_RDF_TYPE_INT32, 4, &n)) == NULL)
        goto memerr;
    ldns_rr_set_rdf(rr, rdf, 2);

    n = 0;
    if ((rdf = ldns_rdf_new_frm_data(LDNS_RDF_TYPE_PERIOD, 4, &n)) == NULL)
        goto memerr;
    ldns_rr_set_rdf(rr, rdf, 3);
    ldns_rr_set_rdf(rr, ldns_rdf_clone(rdf), 4);
    ldns_rr_set_rdf(rr, ldns_rdf_clone(rdf), 5);
    ldns_rr_set_rdf(rr, ldns_rdf_clone(rdf), 6);

    if (ldns_rr_rdf(rr, 1) == NULL || ldns_rr_rdf(rr, 4) == NULL ||
        ldns_rr_rdf(rr, 5) == NULL || ldns_rr_rdf(rr, 6) == NULL ||
        !ldns_pkt_push_rr(pkt, sec, rr))
        goto memerr;
    return LDNS_STATUS_OK;

memerr:
    ldns_rr_free(rr);
    return LDNS_STATUS_MEM_ERR;
}

static uint32_t
ldns_rr_soa_get_serial(const ldns_rr *rr)
{
    const ldns_rdf *rdf;
  
    if (ldns_rr_get_type(rr) != LDNS_RR_TYPE_SOA) return 0;
    if (ldns_rr_rd_count(rr) != 7) return 0;
    rdf = ldns_rr_rdf(rr, 2);
    if (ldns_rdf_get_type(rdf) != LDNS_RDF_TYPE_INT32) return 0;
    if (ldns_rdf_size(rdf) != 4) return 0;
    return ldns_rdf2native_int32(rdf);
}

static ldns_status
ldns_tcp_start(ldns_resolver *res, ldns_pkt *qpkt, int nameserver) {
    /* This routine is based on ldns_axfr_start, with the major
     * difference in that it takes a query packet explicitly.
     */
    struct sockaddr_storage *ns = NULL;
    size_t ns_len = 0;
    ldns_buffer *qbuf = NULL;
    ldns_status status;

    ns = ldns_rdf2native_sockaddr_storage(
            res->_nameservers[nameserver], ldns_resolver_port(res), &ns_len);
    if (ns == NULL) {
        status = LDNS_STATUS_MEM_ERR;
        goto error;
    }

    res->_socket = ldns_tcp_connect(
            ns, (socklen_t)ns_len, ldns_resolver_timeout(res));
    if (res->_socket <= 0) {
        status = LDNS_STATUS_ADDRESS_ERR;
        goto error;
    }

    qbuf = ldns_buffer_new(LDNS_MAX_PACKETLEN);
    if (qbuf == NULL) {
        status = LDNS_STATUS_MEM_ERR;
        goto error;
    }

    status = ldns_pkt2buffer_wire(qbuf, qpkt);
    if (status != LDNS_STATUS_OK)
        goto error;

    if (ldns_tcp_send_query(qbuf, res->_socket, ns, (socklen_t)ns_len) == 0) {
        status = LDNS_STATUS_NETWORK_ERR;
        goto error;
    }

    ldns_buffer_free(qbuf);
    free(ns);
    return LDNS_STATUS_OK;
 
error:
    ldns_buffer_free(qbuf);
    free(ns);
    if (res->_socket > 0) {
        close(res->_socket);
        res->_socket = 0;
    }
    return status;
}

static ldns_status
ldns_tcp_read(ldns_pkt **answer, ldns_resolver *res) {
    ldns_status status;
    struct timeval t1, t2;
    uint8_t *data;
    size_t size;

    if (res->_socket <= 0)
        return LDNS_STATUS_ERR;

    gettimeofday(&t1, NULL);
    data = ldns_tcp_read_wire_timeout(
            res->_socket, &size, ldns_resolver_timeout(res));
    if (data == NULL)
        goto error;

    status = ldns_wire2pkt(answer, data, size);
    free(data);
    if (status != LDNS_STATUS_OK)
        goto error;

    gettimeofday(&t2, NULL);
    ldns_pkt_set_querytime(*answer,
            (uint32_t)((t2.tv_sec - t1.tv_sec)*1000) +
                (t2.tv_usec - t1.tv_usec)/1000);
    ldns_pkt_set_timestamp(*answer, t2);
    return status;

error:
    close(res->_socket);
    res->_socket = 0;
    return LDNS_STATUS_ERR;
}

static void
ldns_tcp_close(ldns_resolver *res) {
    if (res->_socket > 0) {
        close(res->_socket);
        res->_socket = 0;
    }
}

static ldns_status
ldns_resolver_send_to(ldns_pkt **answer, ldns_resolver *res,
    const ldns_rdf *name, ldns_rr_type t, ldns_rr_class c,
    uint16_t flags, uint32_t ixfr_serial, int nameserver,
    bool close_tcp) {
    ldns_status status = LDNS_STATUS_OK;
    ldns_pkt *qpkt;
    struct timeval now;

    int nscnt = ldns_resolver_nameserver_count(res);
    ldns_rdf **ns = ldns_resolver_nameservers(res);
    size_t *rtt = ldns_resolver_rtt(res);

    ldns_resolver_set_nameservers(res, &ns[nameserver]);
    ldns_resolver_set_rtt(res, &rtt[nameserver]);
    ldns_resolver_set_nameserver_count(res, 1);

    /* The next fragment should have been a call to
     * ldns_resolver_prepare_query_pkt(), but starting with ldns
     * version 1.6.17 that function tries to add it's own SOA
     * records when rr_type is LDNS_RR_TYPE_IXFR, and we don't
     * want that.
     */
    qpkt = ldns_pkt_query_new(ldns_rdf_clone(name), t, c, flags);
    if (qpkt == NULL) {
        status = LDNS_STATUS_ERR;
        goto done;
    }
    now.tv_sec = time(NULL);
    now.tv_usec = 0;
    ldns_pkt_set_timestamp(qpkt, now);
    ldns_pkt_set_random_id(qpkt);

    if (t == LDNS_RR_TYPE_IXFR) {
        status = ldns_pkt_push_rr_soa(qpkt,
            LDNS_SECTION_AUTHORITY, name, c, ixfr_serial);
        if (status != LDNS_STATUS_OK) goto done;
    }
    if (close_tcp) {
        status = ldns_resolver_send_pkt(answer, res, qpkt);
    } else {
        status = ldns_tcp_start(res, qpkt, 0);
        if (status != LDNS_STATUS_OK) goto done;
        status = ldns_tcp_read(answer, res);
        if (status != LDNS_STATUS_OK) goto done;
        ldns_pkt_set_answerfrom(*answer, ldns_rdf_clone(ns[0]));
    }

done:
    ldns_pkt_free(qpkt);

    ldns_resolver_set_nameservers(res, ns);
    ldns_resolver_set_rtt(res, rtt);
    ldns_resolver_set_nameserver_count(res, nscnt);
    return status;
}

static void
ldns_pkt_filter_answer(ldns_pkt *pkt, ldns_rr_type type) {
    int i, j, cnt;
    ldns_rr_list *rrlist;
    ldns_rr *rr;
    ldns_rr_type rrtype;

    rrlist = ldns_pkt_answer(pkt);
    cnt = ldns_rr_list_rr_count(rrlist);
    for (i = j = 0; i < cnt; i++) {
        rr = ldns_rr_list_rr(rrlist, i);
        rrtype = ldns_rr_get_type(rr);
        if (type == LDNS_RR_TYPE_ANY ||
            type == rrtype ||
            (type == LDNS_RR_TYPE_AXFR &&
                (rrtype == LDNS_RR_TYPE_A ||
                rrtype == LDNS_RR_TYPE_AAAA ||
                rrtype == LDNS_RR_TYPE_NS ||
                rrtype == LDNS_RR_TYPE_PTR)))
            ldns_rr_list_set_rr(rrlist, rr, j++);
    }
    ldns_rr_list_set_rr_count(rrlist, j);
}

/* Packet content printing.
 */

static struct {
    ldns_rr_type type;
    const char *text;
} rr_types[] = {
    {LDNS_RR_TYPE_A,        "has address"},
    {LDNS_RR_TYPE_NS,       "name server"},
    {LDNS_RR_TYPE_CNAME,    "is an alias for"},
    {LDNS_RR_TYPE_WKS,      "has well known services"},
    {LDNS_RR_TYPE_PTR,      "domain name pointer"},
    {LDNS_RR_TYPE_HINFO,    "host information"},
    {LDNS_RR_TYPE_MX,       "mail is handled by"},
    {LDNS_RR_TYPE_TXT,      "descriptive text"},
    {LDNS_RR_TYPE_X25,      "x25 address"},
    {LDNS_RR_TYPE_ISDN,     "ISDN address"},
    {LDNS_RR_TYPE_SIG,      "has signature"},
    {LDNS_RR_TYPE_KEY,      "has key"},
    {LDNS_RR_TYPE_AAAA,     "has IPv6 address"},
    {LDNS_RR_TYPE_LOC,      "location"},
};

static void
print_opcode(ldns_pkt_opcode opcode) {
    ldns_lookup_table *lt = ldns_lookup_by_id(ldns_opcodes, opcode);

    if (lt && lt->name)
        printf("%s", lt->name);
    else
        printf("RESERVED%d", opcode);
}

static void
print_rcode(uint8_t rcode) {
    ldns_lookup_table *lt = ldns_lookup_by_id(ldns_rcodes, rcode);

    if (lt && lt->name)
        printf("%s", lt->name);
    else
        printf("RESERVED%d", rcode);
}

static int
print_rr_type(ldns_rr_type type) {
    char *str;
    int n;
    
    str = ldns_rr_type2str(type);
    n = printf("%s", str);
    free(str);
    return n;
}

static int
print_rr_class(ldns_rr_class cls) {
    char *str;
    int n;

    str = ldns_rr_class2str(cls);
    n = printf("%s", str);
    free(str);
    return n;
}

static int
print_rdf(ldns_rdf *rdf) {
    char *str;
    int n;

    str = ldns_rdf2str(rdf);
    n = printf("%s", str);
    free(str);
    return n;
}

static int
print_rdf_nodot(ldns_rdf *rdf) {
    char *str;
    int len, n;

    str = ldns_rdf2str(rdf);
    len = strlen(str);
    n = printf("%.*s", str[len-1] == '.' ? len-1 : len, str);
    free(str);
    return n;
}

static int
print_padding(int fromcol, int tocol) {
    int col = fromcol, nextcol = fromcol + 8 - fromcol%8;

    if (fromcol + 1 > tocol) tocol = fromcol + 1;
    for (; nextcol <= tocol; col = nextcol, nextcol += 8)
        printf("\t");
    for (; col < tocol; col++)
        printf(" ");
    return col - fromcol;
}

static void
print_rr_verbose(ldns_rr *rr) {
    bool isq = ldns_rr_is_question(rr);
    int rdcnt = ldns_rr_rd_count(rr);
    int i, n;

    /* bind9-host does not count the initial ';' here */
    n = isq ? printf(";") : 0;
    n = print_rdf(ldns_rr_owner(rr));
    if (!isq) {
        n += print_padding(n, 24);
        n += printf("%d", ldns_rr_ttl(rr));
    }
    n += print_padding(n, 32);
    n += print_rr_class(ldns_rr_get_class(rr));
    n += print_padding(n, 40);
    n += print_rr_type(ldns_rr_get_type(rr));
    for (i = 0; i < rdcnt; i++) {
        if (i == 0) print_padding(n, 48);
        else printf(" ");
        print_rdf(ldns_rr_rdf(rr, i));
    }
    printf("\n");
}

static void
print_pkt_section_verbose(const char *name, ldns_rr_list *rrlist) {
    int i, cnt = ldns_rr_list_rr_count(rrlist);

    if (cnt == 0)
        return;
    printf(";; %s SECTION:\n", name);
    for (i = 0; i < cnt; i++)
        print_rr_verbose(ldns_rr_list_rr(rrlist, i));
    printf("\n");
}

static void
print_pkt_verbose(ldns_pkt *pkt) {
    int got_flags = 0;

    printf(";; ->>HEADER<<- opcode: ");
    print_opcode(ldns_pkt_get_opcode(pkt));
    printf(", status: ");
    print_rcode(ldns_pkt_get_rcode(pkt));
    printf(", id: %u\n", ldns_pkt_id(pkt));
    printf(";; flags:");
    if (ldns_pkt_qr(pkt)) printf(" qr"), got_flags = 1;
    if (ldns_pkt_aa(pkt)) printf(" aa"), got_flags = 1;
    if (ldns_pkt_tc(pkt)) printf(" tc"), got_flags = 1;
    if (ldns_pkt_rd(pkt)) printf(" rd"), got_flags = 1;
    if (ldns_pkt_ra(pkt)) printf(" ra"), got_flags = 1;
    if (ldns_pkt_ad(pkt)) printf(" ad"), got_flags = 1;
    if (ldns_pkt_cd(pkt)) printf(" cd"), got_flags = 1;
    if (!got_flags) printf(" ");
    printf("; QUERY: %u, ANSWER: %u, AUTHORITY: %u, ADDITIONAL: %u\n",
        ldns_pkt_qdcount(pkt), ldns_pkt_ancount(pkt),
        ldns_pkt_nscount(pkt), ldns_pkt_arcount(pkt));
    if (ldns_pkt_edns(pkt))
        printf(";; EDNS: version: %u, udp=%u\n",
            ldns_pkt_edns_version(pkt), ldns_pkt_edns_udp_size(pkt));
    printf("\n");
    print_pkt_section_verbose("QUESTION", ldns_pkt_question(pkt));
    print_pkt_section_verbose("ANSWER", ldns_pkt_answer(pkt));
    print_pkt_section_verbose("AUTHORITY", ldns_pkt_authority(pkt));
    print_pkt_section_verbose("ADDITIONAL", ldns_pkt_additional(pkt));
}

static void
print_rr_short(ldns_rr *rr) {
    ldns_rr_type type = ldns_rr_get_type(rr);
    size_t i, rdcnt = ldns_rr_rd_count(rr);

    print_rdf_nodot(ldns_rr_owner(rr));
    printf(" ");
    for (i = 0; i < countof(rr_types); i++) {
        if (rr_types[i].type == type) {
            printf("%s", rr_types[i].text);
            goto found;
        }
    }

    printf("has ");
    print_rr_type(type);
    printf(" record");

found:
    for (i = 0; i < rdcnt; i++) {
        printf(" ");
        print_rdf(ldns_rr_rdf(rr, i));
    }
    printf("\n");
}

static void
print_pkt_short(ldns_pkt *pkt, bool print_rr_server) {
    ldns_rr_list *rrlist = ldns_pkt_answer(pkt);
    size_t i;

    for (i = 0; i < ldns_rr_list_rr_count(rrlist); i++) {
        if (print_rr_server) {
            printf("Nameserver ");
            print_rdf(ldns_pkt_answerfrom(pkt));
            printf(":\n\t");
        }
        print_rr_short(ldns_rr_list_rr(rrlist, i));
    }
}

static void
print_received_line(ldns_resolver *res, ldns_pkt *pkt) {
    char *from = ldns_rdf2str(ldns_pkt_answerfrom(pkt));

    printf("Received %zu bytes from %s#%d in %d ms\n",
            ldns_pkt_size(pkt), from, ldns_resolver_port(res),
            ldns_pkt_querytime(pkt));
    free(from);
}

/* Main program.
 *
 * Note that no memory is freed below this line by intention.
 */

#define DEFAULT_TCP_TIMEOUT 10
#define DEFAULT_UDP_TIMEOUT 5

enum operation_mode { M_AXFR, M_IXFR, M_DEFAULT_Q, M_SINGLE_Q, M_SOA };

static enum operation_mode o_mode = M_DEFAULT_Q;
static bool o_ignore_servfail = true;
static bool o_ip6_int = false;
static bool o_print_pkt_server = false;
static bool o_print_rr_server = false;
static bool o_recursive = true;
static bool o_tcp = false;
static bool o_verbose = false;
static char *o_name = NULL;
static char *o_server = NULL;
static int o_ipversion = LDNS_RESOLV_INETANY;
static int o_ndots = 1;
static int o_retries = 1;
static ldns_rr_class o_rrclass = LDNS_RR_CLASS_IN;
static ldns_rr_type o_rrtype = (ldns_rr_type)-1;
static time_t o_timeout = 0;
static uint32_t o_ixfr_serial = 0;

static void
usage(void) {
    fprintf(stderr,
    "Usage: %s [-aCdilrsTvw46] [-c class] [-N ndots] [-R number]\n"
    "       %*c [-t type] [-W wait] name [server]\n"
    "\t-a same as -v -t ANY\n"
    "\t-C query SOA records from all authoritative name servers\n"
    "\t-c use this query class (IN, CH, HS, etc)\n"
    "\t-d produce verbose output, same as -v\n"
    "\t-i use IP6.INT for IPv6 reverse lookups\n"
    "\t-l list records in a zone via AXFR\n"
    "\t-N consider names with at least this many dots as absolute\n"
    "\t-R retry UDP queries this many times\n"
    "\t-r disable recursive query\n"
    "\t-s do not ignore SERVFAIL responses\n"
    "\t-T send query via TCP\n"
    "\t-t use this query type (A, AAAA, MX, etc)\n"
    "\t-v produce verbose output\n"
    "\t-w wait forever for a server reply\n"
    "\t-W wait this many seconds for a reply\n"
    "\t-4 use IPv4 only\n"
    "\t-6 use IPv6 only\n",
    progname, (int)strlen(progname), ' ');
    exit(1);
}

static void
parse_args(int argc, char *argv[]) {
    int ch;

    progname = argv[0];
    while ((ch = getopt(argc, argv, "aCdilrsTvw46c:N:R:t:W:")) != -1) {
        switch (ch) {
        case 'a':
            if (o_mode != M_AXFR)
                o_mode = M_SINGLE_Q;
            o_rrtype = LDNS_RR_TYPE_ANY;
            o_verbose = true;
            break;
        case 'C':
            o_mode = M_SOA;
            o_print_rr_server = true;
            o_rrclass = LDNS_RR_CLASS_IN;
            o_rrtype = LDNS_RR_TYPE_NS;
            break;
        case 'c':
            /* bind9-host sets o_mode to M_SINGLE_Q here */
            o_rrclass = ldns_get_rr_class_by_name(optarg);
            if (o_rrclass <= 0)
                die(2, "invalid class: %s\n", optarg);
            break;
        case 'd': o_verbose = true; break;
        case 'i': o_ip6_int = true; break;
        case 'l':
            o_mode = M_AXFR;
            if (o_rrtype == (ldns_rr_type)-1)
                o_rrtype = LDNS_RR_TYPE_AXFR;
            o_tcp = true;
            break;
        case 'N':
            o_ndots = atoi(optarg);
            if (o_ndots < 0) o_ndots = 0;
            break;
        case 'n':
            /* bind9-host accepts and ignores this option */
            break;
        case 'r': o_recursive = 0; break;
        case 'R':
            o_retries = atoi(optarg);
            if (o_retries <= 0) o_retries = 1;
            if (o_retries > 255) o_retries = 255;
            break;
        case 's': o_ignore_servfail = false; break;
        case 'T': o_tcp = true; break;
        case 't':
            if (o_mode != M_AXFR)
                o_mode = M_SINGLE_Q;
            if (strncasecmp(optarg, "ixfr=", 5) == 0) {
                o_rrtype = LDNS_RR_TYPE_IXFR;
                o_ixfr_serial = atol(optarg + 5);
            } else {
                o_rrtype = ldns_get_rr_type_by_name(optarg);
                if (o_rrtype <= 0)
                    die(2, "invalid type: %s\n", optarg);
            }
            if (o_rrtype == LDNS_RR_TYPE_AXFR) {
                o_mode = M_AXFR;
                o_rrtype = LDNS_RR_TYPE_ANY;
                o_verbose = true;
            }
            if (o_rrtype == LDNS_RR_TYPE_IXFR) {
                o_mode = M_IXFR;
                o_rrtype = LDNS_RR_TYPE_ANY;
            }
            break;
        case 'v': o_verbose = true; break;
        case 'w':
              o_timeout = (time_t)INT_MAX;
              break;
        case 'W':
            o_timeout = atol(optarg);
            if (o_timeout <= 0) o_timeout = 1;
            break;
        case '4': o_ipversion = LDNS_RESOLV_INET; break;
        case '6': o_ipversion = LDNS_RESOLV_INET6; break;
        default:
            usage();
        }
    }
    argc -= optind;
    argv += optind;
    /* bind9-host ignores arguments after the 2-nd one */
    if (argc < 1)
        usage();
    o_name = argv[0];
    if (argc > 1) {
        o_server = argv[1];
        o_print_pkt_server = true;
    }
    if (o_rrtype == (ldns_rr_type)-1)
        o_rrtype = LDNS_RR_TYPE_A;
}

static ldns_rdf*
safe_str2rdf_dname(const char *name) {
    ldns_rdf *dname;
    ldns_status status;

    if ((status = ldns_str2rdf_dname(&dname, name)) != LDNS_STATUS_OK) {
        die(1, "'%s' is not a legal name (%s)",
            name, ldns_get_errorstr_by_id(status));
    }
    return dname;
}

static ldns_rdf*
safe_dname_cat_clone(const ldns_rdf *rd1, const ldns_rdf *rd2) {
    ldns_rdf *result = ldns_dname_cat_clone(rd1, rd2);

    if (!result)
        die(1, "not enought memory for a domain name");
    /* Why doesn't ldns_dname_cat_clone check this condition? */
    if (ldns_rdf_size(result) > LDNS_MAX_DOMAINLEN)
        die(1, "'%s' is not a legal name (%s)\n", ldns_rdf2str(result),
            ldns_get_errorstr_by_id(LDNS_STATUS_DOMAINNAME_OVERFLOW));
    return result;
}

static bool
query(ldns_resolver *res, ldns_rdf *domain, ldns_pkt **pkt, bool close_tcp) {
    ldns_status status;
    ldns_pkt_rcode rcode;
    int i, cnt;

    if (o_verbose) {
        printf("Trying \"");
        print_rdf_nodot(domain);
        printf("\"\n");
    }
    for (cnt = ldns_resolver_nameserver_count(res), i = 0; i < cnt; i++) {
        status = ldns_resolver_send_to(pkt, res, domain, o_rrtype,
            o_rrclass, o_recursive ? LDNS_RD : 0, o_ixfr_serial, i,
            close_tcp);
        if (status != LDNS_STATUS_OK) {
            *pkt = NULL;
            continue;
        }
        if (ldns_pkt_tc(*pkt) && !ldns_resolver_usevc(res)) {
            if (o_verbose)
                printf(";; Truncated, retrying in TCP mode.\n");
            ldns_resolver_set_usevc(res, true);
            status = ldns_resolver_send_to(pkt, res, domain, o_rrtype,
                o_rrclass, o_recursive ? LDNS_RD : 0, o_ixfr_serial, i,
                close_tcp);
            ldns_resolver_set_usevc(res, false);
            if (status != LDNS_STATUS_OK)
                continue;
        }
        rcode = ldns_pkt_get_rcode(*pkt);
        if (o_ignore_servfail && rcode == LDNS_RCODE_SERVFAIL && cnt > 1)
            continue;
        return rcode == LDNS_RCODE_NOERROR;
    }
    if (*pkt == NULL) {
        printf(";; connection timed out; no servers could be reached\n");
        exit(1);
    }
    return false;
}

static ldns_rdf *
search(ldns_resolver *res, ldns_rdf *domain, ldns_pkt **pkt,
    bool absolute, bool close_tcp) {
    ldns_rdf *dname, **searchlist;
    int i, n;

    if (absolute && query(res, domain, pkt, close_tcp))
        return domain;

    if ((dname = ldns_resolver_domain(res)) != NULL) {
        dname = safe_dname_cat_clone(domain, dname);
        if (query(res, dname, pkt, close_tcp))
            return dname;
    }

    searchlist = ldns_resolver_searchlist(res);
    n = ldns_resolver_searchlist_count(res);
    for (i = 0; i < n; i++) {
        dname = safe_dname_cat_clone(domain, searchlist[i]);
        if (query(res, dname, pkt, close_tcp))
            return dname;
    }

    if (!absolute && query(res, domain, pkt, close_tcp))
        return domain;

    return NULL;
}

static void
report(ldns_resolver *res, ldns_rdf *domain, ldns_pkt *pkt) {
    ldns_pkt_rcode rcode;

    if (o_print_pkt_server) {
        printf("Using domain server:\nName: %s\nAddress: ", o_server);
        print_rdf(ldns_pkt_answerfrom(pkt));
        printf("#%d\nAliases: \n\n", ldns_resolver_port(res));
        o_print_pkt_server = false;
    }
    rcode = ldns_pkt_get_rcode(pkt);
    if (rcode != LDNS_RCODE_NOERROR) {
        printf("Host ");
        print_rdf_nodot(domain);
        printf(" not found: %d(", rcode);
        print_rcode(rcode);
        printf(")\n");
    } else {
        if (o_verbose) {
            print_pkt_verbose(pkt);
        } else {
            print_pkt_short(pkt, o_print_rr_server);
            if (o_mode == M_SINGLE_Q &&
                ldns_rr_list_rr_count(ldns_pkt_answer(pkt)) == 0) {
                print_rdf_nodot(domain);
                printf(" has no ");
                print_rr_type(o_rrtype);
                printf(" record\n");
            }
        }
    }
    if (o_verbose)
        print_received_line(res, pkt);
}

static bool
doquery(ldns_resolver *res, ldns_rdf *domain) {
    ldns_pkt *pkt;
    bool q;

    q = query(res, domain, &pkt, true);
    report(res, domain, pkt);
    return q;
}

static bool
doquery_filtered(ldns_resolver *res, ldns_rdf *domain) {
    ldns_pkt *pkt;
    bool q;

    q = query(res, domain, &pkt, true);
    ldns_pkt_filter_answer(pkt, o_rrtype);
    report(res, domain, pkt);
    return q;
}

static bool
dosearch(ldns_resolver *res, ldns_rdf *domain, bool absolute) {
    ldns_pkt *pkt;
    ldns_rdf *dname;

    dname = search(res, domain, &pkt, absolute, true);
    report(res, dname != NULL ? dname : domain, pkt);
    return o_mode != M_DEFAULT_Q ? (dname != NULL) :
        (dname != NULL) &&
        (o_rrtype = LDNS_RR_TYPE_AAAA, doquery_filtered(res, dname)) &&
        (o_rrtype = LDNS_RR_TYPE_MX, doquery_filtered(res, dname));
}

static bool
dozonetransfer(ldns_resolver *res, ldns_rdf *domain, bool absolute) {
    ldns_pkt *pkt, *nextpkt;
    ldns_rdf *dname;
    ldns_rr_type rrtype;
    ldns_rr_list *rrl;
    ldns_rr *rr;
    size_t i, nsoa = 0;
    uint32_t first_serial = 0;

    rrtype = o_rrtype;
    o_rrtype = (o_mode == M_AXFR) ? LDNS_RR_TYPE_AXFR : LDNS_RR_TYPE_IXFR;
    dname = search(res, domain, &pkt, absolute, false);

    for (;;) {
        rrl = ldns_rr_list_clone(ldns_pkt_answer(pkt));
        ldns_pkt_filter_answer(pkt, rrtype);
        report(res, dname != NULL ? dname : domain, pkt);
        if ((dname == NULL) ||
                (ldns_pkt_get_rcode(pkt) != LDNS_RCODE_NOERROR)) {
            printf("; Transfer failed.\n");
            ldns_tcp_close(res);
            return false;
        }
        for (i = 0; i < ldns_rr_list_rr_count(rrl); i++) {
            rr = ldns_rr_list_rr(rrl, i);
            if (nsoa == 0) {
                if (ldns_rr_get_type(rr) != LDNS_RR_TYPE_SOA) {
                    printf("; Transfer failed. "
                           "Didn't start with SOA answer.\n");
                    ldns_tcp_close(res);
                    return false;
                }
                first_serial = ldns_rr_soa_get_serial(rr);
                if ((o_mode == M_IXFR) && (first_serial <= o_ixfr_serial)) {
                    ldns_tcp_close(res);
                    return true;
                }
            }
            if (ldns_rr_get_type(rr) == LDNS_RR_TYPE_SOA) {
                nsoa = nsoa < 2 ? nsoa + 1 : 1;
                if ((nsoa == 2) &&
                        (ldns_rr_soa_get_serial(rr) == first_serial)) {
                    ldns_tcp_close(res);
                    return true;
                }
            }
        }
        if (ldns_tcp_read(&nextpkt, res) != LDNS_STATUS_OK) {
            printf("; Transfer failed.\n");
            return false;
        }
        ldns_pkt_set_answerfrom(nextpkt,
                ldns_rdf_clone(ldns_pkt_answerfrom(pkt)));
        ldns_pkt_free(pkt);
        ldns_rr_list_free(rrl);
        pkt = nextpkt;
    }
}

static bool
dosoa(ldns_resolver *res, ldns_rdf *domain, bool absolute) {
    ldns_rr_list *answer, **nsaddrs;
    ldns_rdf *dname, *addr;
    ldns_pkt *pkt;
    ldns_rr *rr;
    size_t i, j, n, cnt;

    if ((dname = search(res, domain, &pkt, absolute, true)) == NULL)
        return false;

    answer = ldns_pkt_answer(pkt);
    cnt = ldns_rr_list_rr_count(answer);
    nsaddrs = alloca(cnt*sizeof(*nsaddrs));
    for (n = 0, i = 0; i < cnt; i++)
        if ((addr = ldns_rr_ns_nsdname(ldns_rr_list_rr(answer, i))) != NULL)
            nsaddrs[n++] = ldns_get_rr_list_addr_by_name(res, 
                addr, LDNS_RR_CLASS_IN, 0); 

    o_print_pkt_server = false;
    o_recursive = false;
    o_rrtype = LDNS_RR_TYPE_SOA;
    for (i = 0; i < n; i++) {
        cnt = ldns_rr_list_rr_count(nsaddrs[i]);
        for (j = 0; j < cnt; j++) {
            ldns_resolver_remove_nameservers(res);
            rr = ldns_rr_list_rr(nsaddrs[i], j);
            if ((ldns_resolver_ip6(res) == LDNS_RESOLV_INET &&
                ldns_rr_get_type(rr) == LDNS_RR_TYPE_AAAA) ||
                (ldns_resolver_ip6(res) == LDNS_RESOLV_INET6 &&
                ldns_rr_get_type(rr) == LDNS_RR_TYPE_A))
                continue;
            if (ldns_resolver_push_nameserver_rr(res, rr) == LDNS_STATUS_OK)
                /* bind9-host queries for domain, not dname here */
                doquery(res, dname);
        }
    }
    return 0;
}

static void
resolver_set_nameserver_hostname(ldns_resolver *res, const char *server) {
    struct addrinfo hints, *ailist, *ai;
    ldns_status status;
    ldns_rdf *rdf;
    int err;

    memset(&hints, 0, sizeof hints);
    switch (ldns_resolver_ip6(res)) {
    case LDNS_RESOLV_INET: hints.ai_family = PF_INET; break;
    case LDNS_RESOLV_INET6: hints.ai_family = PF_INET6; break;
    default: hints.ai_family = PF_UNSPEC; break;
    }
    hints.ai_socktype = SOCK_STREAM;
    do err = getaddrinfo(server, NULL, &hints, &ailist);
    while (err == EAI_AGAIN);
    if (err != 0)
        die(1, "couldn't get address for '%s': %s", server, gai_strerror(err));
    for (ai = ailist; ai != NULL; ai = ai->ai_next) {
        if ((rdf = ldns_sockaddr_storage2rdf((void*)ai->ai_addr, NULL)) == NULL)
            die(1, "couldn't allocate an rdf: %s",
                ldns_get_errorstr_by_id(LDNS_STATUS_MEM_ERR));
        status = ldns_resolver_push_nameserver(res, rdf);
        if (status != LDNS_STATUS_OK)
            die(1, "couldn't push a nameserver address: %s",
                ldns_get_errorstr_by_id(status));
    }
}

static void
resolver_set_nameserver_str(ldns_resolver *res, const char *server) {
    ldns_rdf *addr;

    ldns_resolver_remove_nameservers(res);
    addr = ldns_rdf_new_addr_frm_str(server);
    if (addr) {
        if (ldns_resolver_push_nameserver(res, addr) != LDNS_STATUS_OK)
            die(1, "couldn't push a nameserver address");
    } else
        resolver_set_nameserver_hostname(res, server);
}

int
main(int argc, char *argv[]) {
    ldns_rdf *addr, *dname;
    ldns_resolver *res;
    ldns_status status;
    struct timeval restimeout;

    parse_args(argc, argv);

    status = ldns_resolver_new_default(&res);
    if (status != LDNS_STATUS_OK)
        die(1, "error creating resolver: %s", ldns_get_errorstr_by_id(status));
    if (ldns_resolver_nameserver_count(res) == 0)
        ldns_resolver_push_default_servers(res);

    ldns_resolver_set_usevc(res, o_tcp);
    restimeout.tv_sec = o_timeout > 0 ? o_timeout :
        o_tcp ? DEFAULT_TCP_TIMEOUT : DEFAULT_UDP_TIMEOUT;
    restimeout.tv_usec = 0;
    ldns_resolver_set_timeout(res, restimeout);
    ldns_resolver_set_retry(res, o_retries+1);
    ldns_resolver_set_ip6(res, o_ipversion);
    ldns_resolver_set_defnames(res, false);
    ldns_resolver_set_fallback(res, false);

    if (o_server)
        resolver_set_nameserver_str(res, o_server);

    if (ldns_str2rdf_a(&addr, o_name) == LDNS_STATUS_OK) {
        dname = ldns_rdf_reverse_a(addr, "in-addr.arpa");
        if (dname == NULL)
            die(1, "can't reverse '%s': %s", o_name,
                ldns_get_errorstr_by_id(LDNS_STATUS_MEM_ERR));
        o_mode = M_SINGLE_Q;
        o_rrtype = LDNS_RR_TYPE_PTR;
        return !doquery(res, dname);
    } else if (ldns_str2rdf_aaaa(&addr, o_name) == LDNS_STATUS_OK) {
        dname = ldns_rdf_reverse_aaaa(addr, o_ip6_int ? "ip6.int" : "ip6.arpa");
        if (dname == NULL)
            die(1, "can't reverse '%s': %s", o_name,
                ldns_get_errorstr_by_id(LDNS_STATUS_MEM_ERR));
        o_mode = M_SINGLE_Q;
        o_rrtype = LDNS_RR_TYPE_PTR;
        return !doquery(res, dname);
    }
    return !(o_mode == M_SOA ? dosoa :
             o_mode == M_AXFR ? dozonetransfer :
             o_mode == M_IXFR ? dozonetransfer :
             dosearch)
        (res, safe_str2rdf_dname(o_name), ndots(o_name) >= o_ndots);
}
