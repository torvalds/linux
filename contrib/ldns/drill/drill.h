/*
 * drill.h
 * the main header file of drill
 * (c) 2005, 2006 NLnet Labs
 *
 * See the file LICENSE for the license
 *
 */
#ifndef _DRILL_H_
#define _DRILL_H_
#include "config.h"

#include "drill_util.h"

#define DRILL_VERSION PACKAGE_VERSION

/* what kind of stuff do we allow */
#define DRILL_QUERY	0
#define DRILL_TRACE	1
#define DRILL_CHASE	2
#define DRILL_AFROMFILE 3
#define DRILL_QTOFILE 	4
#define DRILL_NSEC	5
#define DRILL_REVERSE	6
#define DRILL_SECTRACE 	7

#define DRILL_ON(VAR, BIT) \
(VAR) = (VAR) | (BIT)
#define DRILL_OFF(VAR, BIT) \
(VAR) = (VAR) & ~(BIT)

extern ldns_rr_list *global_dns_root;
extern int verbosity;

void do_trace(ldns_resolver *res,
			    ldns_rdf *name,
			    ldns_rr_type type, 
			    ldns_rr_class c);
ldns_status do_chase(ldns_resolver *res,
				 ldns_rdf *name,
				 ldns_rr_type type, 
				 ldns_rr_class c,
				 ldns_rr_list *trusted_keys, 
				 ldns_pkt *pkt_o,
				 uint16_t qflags,
				 ldns_rr_list *prev_key_list);
int do_secure_trace(ldns_resolver *res,
				ldns_rdf *name,
				ldns_rr_type type, 
				ldns_rr_class c,
				ldns_rr_list *trusted_keys,
				ldns_rdf *start_name);

ldns_rr_list *	get_rr(ldns_resolver *res,
				  ldns_rdf *zname,
				  ldns_rr_type t,
				  ldns_rr_class c);

void drill_pkt_print(FILE *fd, ldns_resolver *r, ldns_pkt *p);
void drill_pkt_print_footer(FILE *fd, ldns_resolver *r, ldns_pkt *p);

ldns_pkt_type get_dnssec_rr(ldns_pkt *p,
					   ldns_rdf *name,
					   ldns_rr_type t,
					   ldns_rr_list **rrlist,
					   ldns_rr_list **sig);

ldns_rr *ldns_nsec3_exact_match(ldns_rdf *qname,
						  ldns_rr_type qtype,
						  ldns_rr_list *nsec3s);

ldns_rdf *ldns_nsec3_closest_encloser(ldns_rdf *qname,
							   ldns_rr_type qtype,
							   ldns_rr_list *nsec3s);

/* verifies denial of existence of *name in *pkt (must contain NSEC or NSEC3 records
 * if *nsec_rrs and *nsec_rr_sigs are given, pointers to the relevant nsecs and their signatures are
 * placed there
 */
ldns_status ldns_verify_denial(ldns_pkt *pkt,
						 ldns_rdf *name,
						 ldns_rr_type type,
						 ldns_rr_list **nsec_rrs, 
						 ldns_rr_list **nsec_rr_sigs);

ldns_pkt *read_hex_pkt(char *filename);
ldns_buffer *read_hex_buffer(char *filename);
void	init_root(void);
ldns_rr_list *read_root_hints(const char *filename);
void clear_root(void);
void	dump_hex(const ldns_pkt *pkt, const char *file);
void warning(const char *fmt, ...);
void	error(const char *fmt, ...);
void	mesg(const char *fmt, ...);

/* screen.c */
void resolver_print_nameservers(ldns_resolver *r);
void print_dnskey(ldns_rr_list *key_list);
void print_ds(ldns_rr_list *ds_list);

#endif /* _DRILL_H_ */
