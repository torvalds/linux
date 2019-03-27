/*
 * root.c
 * Function to handle to the rootservers
 * and to update and prime them
 * (c) 2005 NLnet Labs
 *
 * See the file LICENSE for the license
 *
 */

#include "drill.h"
#include <ldns/ldns.h>
#include <errno.h>

/* a global list of the root-servers */
ldns_rr_list *global_dns_root = NULL;

/* put a hardcoded list in the root and
 * init the root rrlist structure */
void
init_root(void)
{
	ldns_rr *r;
	
	global_dns_root = ldns_rr_list_new();

	(void)ldns_rr_new_frm_str(&r, "A.ROOT-SERVERS.NET.      3600000      A     198.41.0.4", 0, NULL, NULL);
	ldns_rr_list_push_rr(global_dns_root, r);
	(void)ldns_rr_new_frm_str(&r, "A.ROOT-SERVERS.NET.      3600000      AAAA  2001:503:BA3E::2:30", 0, NULL, NULL);
	ldns_rr_list_push_rr(global_dns_root, r);
	(void)ldns_rr_new_frm_str(&r, "B.ROOT-SERVERS.NET.      3600000      A     192.228.79.201", 0, NULL, NULL);
	ldns_rr_list_push_rr(global_dns_root, r);
	(void)ldns_rr_new_frm_str(&r, "C.ROOT-SERVERS.NET.      3600000      A     192.33.4.12", 0, NULL, NULL);
	ldns_rr_list_push_rr(global_dns_root, r);
	(void)ldns_rr_new_frm_str(&r, "D.ROOT-SERVERS.NET.      3600000      A     128.8.10.90", 0, NULL, NULL);
	ldns_rr_list_push_rr(global_dns_root, r);
	(void)ldns_rr_new_frm_str(&r, "E.ROOT-SERVERS.NET.      3600000      A     192.203.230.10", 0, NULL, NULL);
	ldns_rr_list_push_rr(global_dns_root, r);
	(void)ldns_rr_new_frm_str(&r, "F.ROOT-SERVERS.NET.      3600000      A     192.5.5.241", 0, NULL, NULL);
	ldns_rr_list_push_rr(global_dns_root, r);
	(void)ldns_rr_new_frm_str(&r, "F.ROOT-SERVERS.NET.      3600000      AAAA  2001:500:2F::F", 0, NULL, NULL);
	ldns_rr_list_push_rr(global_dns_root, r);
	(void)ldns_rr_new_frm_str(&r, "G.ROOT-SERVERS.NET.      3600000      A     192.112.36.4", 0, NULL, NULL);
	ldns_rr_list_push_rr(global_dns_root, r);
	(void)ldns_rr_new_frm_str(&r, "H.ROOT-SERVERS.NET.      3600000      A     128.63.2.53", 0, NULL, NULL);
	ldns_rr_list_push_rr(global_dns_root, r);
	(void)ldns_rr_new_frm_str(&r, "H.ROOT-SERVERS.NET.      3600000      AAAA  2001:500:1::803F:235", 0, NULL, NULL);
	ldns_rr_list_push_rr(global_dns_root, r);
	(void)ldns_rr_new_frm_str(&r, "I.ROOT-SERVERS.NET.      3600000      A     192.36.148.17", 0, NULL, NULL);
	ldns_rr_list_push_rr(global_dns_root, r);
	(void)ldns_rr_new_frm_str(&r, "J.ROOT-SERVERS.NET.      3600000      A     192.58.128.30", 0, NULL, NULL);
	ldns_rr_list_push_rr(global_dns_root, r);
	(void)ldns_rr_new_frm_str(&r, "J.ROOT-SERVERS.NET.      3600000      AAAA  2001:503:C27::2:30", 0, NULL, NULL);
	ldns_rr_list_push_rr(global_dns_root, r);
	(void)ldns_rr_new_frm_str(&r, "K.ROOT-SERVERS.NET.      3600000      A     193.0.14.129 ", 0, NULL, NULL);
	ldns_rr_list_push_rr(global_dns_root, r);
	(void)ldns_rr_new_frm_str(&r, "K.ROOT-SERVERS.NET.      3600000      AAAA  2001:7FD::1", 0, NULL, NULL);
	ldns_rr_list_push_rr(global_dns_root, r);
	(void)ldns_rr_new_frm_str(&r, "L.ROOT-SERVERS.NET.      3600000      A     199.7.83.42", 0, NULL, NULL);
	ldns_rr_list_push_rr(global_dns_root, r);
	(void)ldns_rr_new_frm_str(&r, "L.ROOT-SERVERS.NET.      3600000      AAAA  2001:500:3::42   ", 0, NULL, NULL);
	ldns_rr_list_push_rr(global_dns_root, r);
	(void)ldns_rr_new_frm_str(&r, "M.ROOT-SERVERS.NET.      3600000      A     202.12.27.33", 0, NULL, NULL);
	ldns_rr_list_push_rr(global_dns_root, r);
	(void)ldns_rr_new_frm_str(&r, "M.ROOT-SERVERS.NET.      3600000      AAAA  2001:DC3::35", 0, NULL, NULL);
	ldns_rr_list_push_rr(global_dns_root, r);
}

/*
 * Read a hints file as root
 *
 * The file with the given path should contain a list of NS RRs
 * for the root zone and A records for those NS RRs.
 * Read them, check them, and append the a records to the rr list given.
 */
ldns_rr_list *
read_root_hints(const char *filename)
{
	FILE *fp = NULL;
	int line_nr = 0;
	ldns_zone *z;
	ldns_status status;
	ldns_rr_list *addresses = NULL;
	ldns_rr *rr;
	size_t i;

	fp = fopen(filename, "r");
	if (!fp) {
		fprintf(stderr, "Unable to open %s for reading: %s\n", filename, strerror(errno));
		return NULL;
	}

	status = ldns_zone_new_frm_fp_l(&z, fp, NULL, 0, 0, &line_nr);
	fclose(fp);
	if (status != LDNS_STATUS_OK) {
		fprintf(stderr, "Error reading root hints file: %s\n", ldns_get_errorstr_by_id(status));
		return NULL;
	} else {
		addresses = ldns_rr_list_new();
		for (i = 0; i < ldns_rr_list_rr_count(ldns_zone_rrs(z)); i++) { 
			rr = ldns_rr_list_rr(ldns_zone_rrs(z), i);
			/*if ((address_family == 0 || address_family == 1) &&
			*/
			if ( ldns_rr_get_type(rr) == LDNS_RR_TYPE_A ) {
				ldns_rr_list_push_rr(addresses, ldns_rr_clone(rr));
			}
			/*if ((address_family == 0 || address_family == 2) &&*/
			if ( ldns_rr_get_type(rr) == LDNS_RR_TYPE_AAAA) {
				ldns_rr_list_push_rr(addresses, ldns_rr_clone(rr));
			}
		}
		ldns_zone_deep_free(z);
		return addresses;
	}
}


void
clear_root(void)
{
	ldns_rr_list_deep_free(global_dns_root);
}
