/*
 * securechasetrace.c
 * Where all the hard work concerning secure tracing is done
 *
 * (c) 2005, 2006 NLnet Labs
 *
 * See the file LICENSE for the license
 *
 */

#include "drill.h"
#include <ldns/ldns.h>

#define SELF "[S]"  /* self sig ok */
#define TRUST "[T]" /* chain from parent */
#define BOGUS "[B]" /* bogus */
#define UNSIGNED "[U]" /* no relevant dnssec data found */

#if 0
/* See if there is a key/ds in trusted that matches
 * a ds in *ds. 
 */
static ldns_rr_list *
ds_key_match(ldns_rr_list *ds, ldns_rr_list *trusted)
{
	size_t i, j;
	bool match;
	ldns_rr *rr_i, *rr_j;
	ldns_rr_list *keys;

	if (!trusted || !ds) {
		return NULL;
	}

	match = false;
	keys = ldns_rr_list_new();
	if (!keys) {
		return NULL;
	}

	if (!ds || !trusted) {
		return NULL;
	}

	for (i = 0; i < ldns_rr_list_rr_count(trusted); i++) {
		rr_i = ldns_rr_list_rr(trusted, i);
		for (j = 0; j < ldns_rr_list_rr_count(ds); j++) {

			rr_j = ldns_rr_list_rr(ds, j);
			if (ldns_rr_compare_ds(rr_i, rr_j)) {
				match = true;
				/* only allow unique RRs to match */
				ldns_rr_set_push_rr(keys, rr_i); 
			}
		}
	}
	if (match) {
		return keys;
	} else {
		return NULL;
	}
}
#endif

static ldns_pkt *
get_dnssec_pkt(ldns_resolver *r, ldns_rdf *name, ldns_rr_type t) 
{
	ldns_pkt *p = NULL;
	p = ldns_resolver_query(r, name, t, LDNS_RR_CLASS_IN, 0); 
	if (!p) {
		return NULL;
	} else {
		if (verbosity >= 5) {
			ldns_pkt_print(stdout, p);
		}
		return p;
	}
}

#ifdef HAVE_SSL
/* 
 * retrieve keys for this zone
 */
static ldns_pkt_type
get_key(ldns_pkt *p, ldns_rdf *apexname, ldns_rr_list **rrlist, ldns_rr_list **opt_sig)
{
	return get_dnssec_rr(p, apexname, LDNS_RR_TYPE_DNSKEY, rrlist, opt_sig);
}

/*
 * check to see if we can find a DS rrset here which we can then follow
 */
static ldns_pkt_type
get_ds(ldns_pkt *p, ldns_rdf *ownername, ldns_rr_list **rrlist, ldns_rr_list **opt_sig)
{
	return get_dnssec_rr(p, ownername, LDNS_RR_TYPE_DS, rrlist, opt_sig);
}
#endif /* HAVE_SSL */

static void
remove_resolver_nameservers(ldns_resolver *res)
{
	ldns_rdf *pop;
	
	/* remove the old nameserver from the resolver */
	while((pop = ldns_resolver_pop_nameserver(res))) {
		ldns_rdf_deep_free(pop);
	}

}

/*ldns_pkt **/
#ifdef HAVE_SSL
int
do_secure_trace(ldns_resolver *local_res, ldns_rdf *name, ldns_rr_type t,
                ldns_rr_class c, ldns_rr_list *trusted_keys, ldns_rdf *start_name
               )
{
	ldns_resolver *res;
	ldns_pkt *p, *local_p;
	ldns_rr_list *new_nss;
	ldns_rr_list *ns_addr;
	ldns_rdf *pop;
	ldns_rdf **labels = NULL;
	ldns_status status, st;
	ssize_t i;
	size_t j;
	size_t k;
	size_t l;
	uint8_t labels_count = 0;

	/* dnssec */
	ldns_rr_list *key_list;
	ldns_rr_list *key_sig_list;
	ldns_rr_list *ds_list;
	ldns_rr_list *ds_sig_list;
	ldns_rr_list *correct_key_list;
	ldns_rr_list *trusted_ds_rrs;
	bool new_keys_trusted = false;
	ldns_rr_list *current_correct_keys;
	ldns_rr_list *dataset;

	ldns_rr_list *nsec_rrs = NULL;
	ldns_rr_list *nsec_rr_sigs = NULL;

	/* empty non-terminal check */
	bool ent;
	ldns_rr  *nsecrr;      /* The nsec that proofs the non-terminal */
	ldns_rdf *hashed_name; /* The query hashed with nsec3 params */
	ldns_rdf *label0;      /* The first label of an nsec3 owner name */

	/* glue handling */
	ldns_rr_list *new_ns_addr;
	ldns_rr_list *old_ns_addr;
	ldns_rr *ns_rr;

	int result = 0;

	/* printing niceness */
	const ldns_rr_descriptor *descriptor;

	descriptor = ldns_rr_descript(t);

	new_nss = NULL;
	ns_addr = NULL;
	key_list = NULL;
	ds_list = NULL;

	p = NULL;
	local_p = NULL;
	res = ldns_resolver_new();
	key_sig_list = NULL;
	ds_sig_list = NULL;

	if (!res) {
		error("Memory allocation failed");
		result = -1;
		return result;
	}

	correct_key_list = ldns_rr_list_new();
	if (!correct_key_list) {
		error("Memory allocation failed");
		result = -1;
		return result;
	}

	trusted_ds_rrs = ldns_rr_list_new();
	if (!trusted_ds_rrs) {
		error("Memory allocation failed");
		result = -1;
		return result;
	}
        /* Add all preset trusted DS signatures to the list of trusted DS RRs. */
        for (j = 0; j < ldns_rr_list_rr_count(trusted_keys); j++) {
            ldns_rr* one_rr = ldns_rr_list_rr(trusted_keys, j);
            if (ldns_rr_get_type(one_rr)  == LDNS_RR_TYPE_DS) {
                ldns_rr_list_push_rr(trusted_ds_rrs, ldns_rr_clone(one_rr));
            }
        }

	/* transfer some properties of local_res to res */
	ldns_resolver_set_ip6(res,
			ldns_resolver_ip6(local_res));
	ldns_resolver_set_port(res,
			ldns_resolver_port(local_res));
	ldns_resolver_set_debug(res,
			ldns_resolver_debug(local_res));
	ldns_resolver_set_fail(res,
			ldns_resolver_fail(local_res));
	ldns_resolver_set_usevc(res,
			ldns_resolver_usevc(local_res));
	ldns_resolver_set_random(res,
			ldns_resolver_random(local_res));
	ldns_resolver_set_source(res,
			ldns_resolver_source(local_res));
	ldns_resolver_set_recursive(local_res, true);

	ldns_resolver_set_recursive(res, false);
	ldns_resolver_set_dnssec_cd(res, false);
	ldns_resolver_set_dnssec(res, true);

	/* setup the root nameserver in the new resolver */
	status = ldns_resolver_push_nameserver_rr_list(res, global_dns_root);
	if (status != LDNS_STATUS_OK) {
		printf("ERRRRR: %s\n", ldns_get_errorstr_by_id(status));
		ldns_rr_list_print(stdout, global_dns_root);
		result = status;
		goto done;
	}
	labels_count = ldns_dname_label_count(name);
	if (start_name) {
		if (ldns_dname_is_subdomain(name, start_name)) {
			labels_count -= ldns_dname_label_count(start_name);
		} else {
			fprintf(stderr, "Error; ");
			ldns_rdf_print(stderr, name);
			fprintf(stderr, " is not a subdomain of ");
			ldns_rdf_print(stderr, start_name);
			fprintf(stderr, "\n");
			goto done;
		}
	}
	labels = LDNS_XMALLOC(ldns_rdf*, labels_count + 2);
	if (!labels) {
		goto done;
	}
	labels[0] = ldns_dname_new_frm_str(LDNS_ROOT_LABEL_STR);
	labels[1] = ldns_rdf_clone(name);
	for(i = 2 ; i < (ssize_t)labels_count + 2; i++) {
		labels[i] = ldns_dname_left_chop(labels[i - 1]);
	}

	/* get the nameserver for the label
	 * ask: dnskey and ds for the label
	 */
	for(i = (ssize_t)labels_count + 1; i > 0; i--) {
		status = ldns_resolver_send(&local_p, res, labels[i], LDNS_RR_TYPE_NS, c, 0);

		if (verbosity >= 5) {
			ldns_pkt_print(stdout, local_p);
		}

		new_nss = ldns_pkt_rr_list_by_type(local_p,
					LDNS_RR_TYPE_NS, LDNS_SECTION_ANSWER);
 		if (!new_nss) {
			/* if it's a delegation, servers put them in the auth section */
			new_nss = ldns_pkt_rr_list_by_type(local_p,
					LDNS_RR_TYPE_NS, LDNS_SECTION_AUTHORITY);
		}

		/* if this is the final step there might not be nameserver records
		   of course if the data is in the apex, there are, so cover both
		   cases */
		if (new_nss || i > 1) {
			for(j = 0; j < ldns_rr_list_rr_count(new_nss); j++) {
				ns_rr = ldns_rr_list_rr(new_nss, j);
				pop = ldns_rr_rdf(ns_rr, 0);
				if (!pop) {
					printf("nopo\n");
					break;
				}
				/* retrieve it's addresses */
				/* trust glue? */
				new_ns_addr = NULL;
				if (ldns_dname_is_subdomain(pop, labels[i])) {
					new_ns_addr = ldns_pkt_rr_list_by_name_and_type(local_p, pop, LDNS_RR_TYPE_A, LDNS_SECTION_ADDITIONAL);
				}
				if (!new_ns_addr || ldns_rr_list_rr_count(new_ns_addr) == 0) {
					new_ns_addr = ldns_get_rr_list_addr_by_name(res, pop, c, 0);
				}
				if (!new_ns_addr || ldns_rr_list_rr_count(new_ns_addr) == 0) {
					new_ns_addr = ldns_get_rr_list_addr_by_name(local_res, pop, c, 0);
				}

				if (new_ns_addr) {
					old_ns_addr = ns_addr;
					ns_addr = ldns_rr_list_cat_clone(ns_addr, new_ns_addr);
					ldns_rr_list_deep_free(old_ns_addr);
				}
				ldns_rr_list_deep_free(new_ns_addr);
			}
			ldns_rr_list_deep_free(new_nss);

			if (ns_addr) {
				remove_resolver_nameservers(res);

				if (ldns_resolver_push_nameserver_rr_list(res, ns_addr) !=
						LDNS_STATUS_OK) {
					error("Error adding new nameservers");
					ldns_pkt_free(local_p);
					goto done;
				}
				ldns_rr_list_deep_free(ns_addr);
			} else {
				status = ldns_verify_denial(local_p, labels[i], LDNS_RR_TYPE_NS, &nsec_rrs, &nsec_rr_sigs);

				/* verify the nsec3 themselves*/
				if (verbosity >= 4) {
					printf("NSEC(3) Records to verify:\n");
					ldns_rr_list_print(stdout, nsec_rrs);
					printf("With signatures:\n");
					ldns_rr_list_print(stdout, nsec_rr_sigs);
					printf("correct keys:\n");
					ldns_rr_list_print(stdout, correct_key_list);
				}

				if (status == LDNS_STATUS_OK) {
					if ((st = ldns_verify(nsec_rrs, nsec_rr_sigs, trusted_keys, NULL)) == LDNS_STATUS_OK) {
						fprintf(stdout, "%s ", TRUST);
						fprintf(stdout, "Existence denied: ");
						ldns_rdf_print(stdout, labels[i]);
	/*
						if (descriptor && descriptor->_name) {
							printf(" %s", descriptor->_name);
						} else {
							printf(" TYPE%u", t);
						}
	*/					fprintf(stdout, " NS\n");
					} else if ((st = ldns_verify(nsec_rrs, nsec_rr_sigs, correct_key_list, NULL)) == LDNS_STATUS_OK) {
						fprintf(stdout, "%s ", SELF);
						fprintf(stdout, "Existence denied: ");
						ldns_rdf_print(stdout, labels[i]);
	/*
						if (descriptor && descriptor->_name) {
							printf(" %s", descriptor->_name);
						} else {
							printf(" TYPE%u", t);
						}
	*/
						fprintf(stdout, " NS\n");
					} else {
						fprintf(stdout, "%s ", BOGUS);
						result = 1;
						printf(";; Error verifying denial of existence for name ");
						ldns_rdf_print(stdout, labels[i]);
	/*
						printf(" type ");
						if (descriptor && descriptor->_name) {
							printf("%s", descriptor->_name);
						} else {
							printf("TYPE%u", t);
						}
	*/					printf("NS: %s\n", ldns_get_errorstr_by_id(st));
					}
				} else {
					fprintf(stdout, "%s ", BOGUS);
					result = 1;
					printf(";; Error verifying denial of existence for name ");
					ldns_rdf_print(stdout, labels[i]);
					printf("NS: %s\n", ldns_get_errorstr_by_id(status));
				}

				/* there might be an empty non-terminal, in which case we need to continue */
				ent = false;
				for (j = 0; j < ldns_rr_list_rr_count(nsec_rrs); j++) {
					nsecrr = ldns_rr_list_rr(nsec_rrs, j);
					/* For NSEC when the next name is a subdomain of the question */
					if (ldns_rr_get_type(nsecrr) == LDNS_RR_TYPE_NSEC &&
							ldns_dname_is_subdomain(ldns_rr_rdf(nsecrr, 0), labels[i])) {
						ent = true;

					/* For NSEC3, the hash matches the name and the type bitmap is empty*/
					} else if (ldns_rr_get_type(nsecrr) == LDNS_RR_TYPE_NSEC3) {
						hashed_name = ldns_nsec3_hash_name_frm_nsec3(nsecrr, labels[i]);
						label0 = ldns_dname_label(ldns_rr_owner(nsecrr), 0);
						if (hashed_name && label0 &&
								ldns_dname_compare(hashed_name, label0) == 0 &&
								ldns_nsec3_bitmap(nsecrr) == NULL) {
							ent = true;
						}
						if (label0) {
							LDNS_FREE(label0);
						}
						if (hashed_name) {
							LDNS_FREE(hashed_name);
						}
					}
				}
				if (!ent) {
					ldns_rr_list_deep_free(nsec_rrs);
					ldns_rr_list_deep_free(nsec_rr_sigs);
					ldns_pkt_free(local_p);
					goto done;
				} else {
					printf(";; There is an empty non-terminal here, continue\n");
					continue;
				}
			}

			if (ldns_resolver_nameserver_count(res) == 0) {
				error("No nameservers found for this node");
				goto done;
			}
		}
		ldns_pkt_free(local_p);

		fprintf(stdout, ";; Domain: ");
		ldns_rdf_print(stdout, labels[i]);
		fprintf(stdout, "\n");

		/* retrieve keys for current domain, and verify them
		   if they match an already trusted DS, or if one of the
		   keys used to sign these is trusted, add the keys to
		   the trusted list */
		p = get_dnssec_pkt(res, labels[i], LDNS_RR_TYPE_DNSKEY);
		(void) get_key(p, labels[i], &key_list, &key_sig_list);
		if (key_sig_list) {
			if (key_list) {
				current_correct_keys = ldns_rr_list_new();
				if ((st = ldns_verify(key_list, key_sig_list, key_list, current_correct_keys)) ==
						LDNS_STATUS_OK) {
					/* add all signed keys (don't just add current_correct, you'd miss
					 * the zsk's then */
					for (j = 0; j < ldns_rr_list_rr_count(key_list); j++) {
						ldns_rr_list_push_rr(correct_key_list, ldns_rr_clone(ldns_rr_list_rr(key_list, j)));
					}

					/* check whether these keys were signed
					 * by a trusted keys. if so, these
					 * keys are also trusted */
					new_keys_trusted = false;
					for (k = 0; k < ldns_rr_list_rr_count(current_correct_keys); k++) {
						for (j = 0; j < ldns_rr_list_rr_count(trusted_ds_rrs); j++) {
							if (ldns_rr_compare_ds(ldns_rr_list_rr(current_correct_keys, k),
								    ldns_rr_list_rr(trusted_ds_rrs, j))) {
								new_keys_trusted = true;
							}
						}
					}

					/* also all keys are trusted if one of the current correct keys is trusted */
					for (k = 0; k < ldns_rr_list_rr_count(current_correct_keys); k++) {
						for (j = 0; j < ldns_rr_list_rr_count(trusted_keys); j++) {
							if (ldns_rr_compare(ldns_rr_list_rr(current_correct_keys, k),
								            ldns_rr_list_rr(trusted_keys, j)) == 0) {
								            new_keys_trusted = true;
							}
						}
					}


					if (new_keys_trusted) {
						ldns_rr_list_push_rr_list(trusted_keys, key_list);
						print_rr_list_abbr(stdout, key_list, TRUST);
						ldns_rr_list_free(key_list);
						key_list = NULL;
					} else {
						if (verbosity >= 2) {
							printf(";; Signature ok but no chain to a trusted key or ds record\n");
						}
						print_rr_list_abbr(stdout, key_list, SELF);
						ldns_rr_list_deep_free(key_list);
						key_list = NULL;
					}
				} else {
					print_rr_list_abbr(stdout, key_list, BOGUS);
					result = 2;
					ldns_rr_list_deep_free(key_list);
					key_list = NULL;
				}
				ldns_rr_list_free(current_correct_keys);
				current_correct_keys = NULL;
			} else {
				printf(";; No DNSKEY record found for ");
				ldns_rdf_print(stdout, labels[i]);
				printf("\n");
			}
		}

		ldns_pkt_free(p);
		ldns_rr_list_deep_free(key_sig_list);
		key_sig_list = NULL;

		/* check the DS records for the next child domain */
		if (i > 1) {
			p = get_dnssec_pkt(res, labels[i-1], LDNS_RR_TYPE_DS);
			(void) get_ds(p, labels[i-1], &ds_list, &ds_sig_list);
			if (!ds_list) {
				ldns_pkt_free(p);
				if (ds_sig_list) {
					ldns_rr_list_deep_free(ds_sig_list);
				}
				p = get_dnssec_pkt(res, name, LDNS_RR_TYPE_DNSKEY);
				(void) get_ds(p, NULL, &ds_list, &ds_sig_list); 
			}
			if (ds_sig_list) {
				if (ds_list) {
					if (verbosity >= 4) {
						printf("VERIFYING:\n");
						printf("DS LIST:\n");
						ldns_rr_list_print(stdout, ds_list);
						printf("SIGS:\n");
						ldns_rr_list_print(stdout, ds_sig_list);
						printf("KEYS:\n");
						ldns_rr_list_print(stdout, correct_key_list);
					}

					current_correct_keys = ldns_rr_list_new();

					if ((st = ldns_verify(ds_list, ds_sig_list, correct_key_list, current_correct_keys)) ==
							LDNS_STATUS_OK) {
						/* if the ds is signed by a trusted key and a key from correct keys
						   matches that ds, add that key to the trusted keys */
						new_keys_trusted = false;
						if (verbosity >= 2) {
							printf("Checking if signing key is trusted:\n");
						}
						for (j = 0; j < ldns_rr_list_rr_count(current_correct_keys); j++) {
							if (verbosity >= 2) {
								printf("New key: ");
								ldns_rr_print(stdout, ldns_rr_list_rr(current_correct_keys, j));
							}
							for (k = 0; k < ldns_rr_list_rr_count(trusted_keys); k++) {
								if (verbosity >= 2) {
									printf("\tTrusted key: ");
									ldns_rr_print(stdout, ldns_rr_list_rr(trusted_keys, k));
								}
								if (ldns_rr_compare(ldns_rr_list_rr(current_correct_keys, j),
								    ldns_rr_list_rr(trusted_keys, k)) == 0) {
								    	if (verbosity >= 2) {
								    		printf("Key is now trusted!\n");
									}
									for (l = 0; l < ldns_rr_list_rr_count(ds_list); l++) {
										ldns_rr_list_push_rr(trusted_ds_rrs, ldns_rr_clone(ldns_rr_list_rr(ds_list, l)));
										new_keys_trusted = true;
									}
								}
							}
						}
						if (new_keys_trusted) {
							print_rr_list_abbr(stdout, ds_list, TRUST);
						} else {
							print_rr_list_abbr(stdout, ds_list, SELF);
						}
					} else {
						result = 3;
						print_rr_list_abbr(stdout, ds_list, BOGUS);
					}

					ldns_rr_list_free(current_correct_keys);
					current_correct_keys = NULL;
				} else {
					/* wait apparently there were no keys either, go back to the ds packet */
					ldns_pkt_free(p);
					ldns_rr_list_deep_free(ds_sig_list);
					p = get_dnssec_pkt(res, labels[i-1], LDNS_RR_TYPE_DS);
					(void) get_ds(p, labels[i-1], &ds_list, &ds_sig_list);
					
					status = ldns_verify_denial(p, labels[i-1], LDNS_RR_TYPE_DS, &nsec_rrs, &nsec_rr_sigs);

					if (verbosity >= 4) {
						printf("NSEC(3) Records to verify:\n");
						ldns_rr_list_print(stdout, nsec_rrs);
						printf("With signatures:\n");
						ldns_rr_list_print(stdout, nsec_rr_sigs);
						printf("correct keys:\n");
						ldns_rr_list_print(stdout, correct_key_list);
					}

					if (status == LDNS_STATUS_OK) {
						if ((st = ldns_verify(nsec_rrs, nsec_rr_sigs, trusted_keys, NULL)) == LDNS_STATUS_OK) {
							fprintf(stdout, "%s ", TRUST);
							fprintf(stdout, "Existence denied: ");
							ldns_rdf_print(stdout, labels[i-1]);
							printf(" DS");
							fprintf(stdout, "\n");
						} else if ((st = ldns_verify(nsec_rrs, nsec_rr_sigs, correct_key_list, NULL)) == LDNS_STATUS_OK) {
							fprintf(stdout, "%s ", SELF);
							fprintf(stdout, "Existence denied: ");
							ldns_rdf_print(stdout, labels[i-1]);
							printf(" DS");
							fprintf(stdout, "\n");
						} else {
							result = 4;
							fprintf(stdout, "%s ", BOGUS);
							printf("Error verifying denial of existence for ");
							ldns_rdf_print(stdout, labels[i-1]);
							printf(" DS");
							printf(": %s\n", ldns_get_errorstr_by_id(st));
						}
						
					
					} else {
						if (status == LDNS_STATUS_CRYPTO_NO_RRSIG) {
							printf(";; No DS for ");
							ldns_rdf_print(stdout, labels[i - 1]);
						} else {
							printf("[B] Unable to verify denial of existence for ");
							ldns_rdf_print(stdout, labels[i - 1]);
							printf(" DS: %s\n", ldns_get_errorstr_by_id(status));
						}
					}
					if (verbosity >= 2) {
						printf(";; No ds record for delegation\n");
					}
				}
			}
			ldns_rr_list_deep_free(ds_list);
			ldns_pkt_free(p);
		} else {
			/* if this is the last label, just verify the data and stop */
			p = get_dnssec_pkt(res, labels[i], t);
			(void) get_dnssec_rr(p, labels[i], t, &dataset, &key_sig_list);
			if (dataset && ldns_rr_list_rr_count(dataset) > 0) {
				if (key_sig_list && ldns_rr_list_rr_count(key_sig_list) > 0) {

					/* If this is a wildcard, you must be able to deny exact match */
					if ((st = ldns_verify(dataset, key_sig_list, trusted_keys, NULL)) == LDNS_STATUS_OK) {
						fprintf(stdout, "%s ", TRUST);
						ldns_rr_list_print(stdout, dataset);
					} else if ((st = ldns_verify(dataset, key_sig_list, correct_key_list, NULL)) == LDNS_STATUS_OK) {
						fprintf(stdout, "%s ", SELF);
						ldns_rr_list_print(stdout, dataset);
					} else {
						result = 5;
						fprintf(stdout, "%s ", BOGUS);
						ldns_rr_list_print(stdout, dataset);
						printf(";; Error: %s\n", ldns_get_errorstr_by_id(st));
					}
				} else {
					fprintf(stdout, "%s ", UNSIGNED);
					ldns_rr_list_print(stdout, dataset);
				}
				ldns_rr_list_deep_free(dataset);
			} else {
				status = ldns_verify_denial(p, name, t, &nsec_rrs, &nsec_rr_sigs);
				if (status == LDNS_STATUS_OK) {
					/* verify the nsec3 themselves*/
					if (verbosity >= 5) {
						printf("NSEC(3) Records to verify:\n");
						ldns_rr_list_print(stdout, nsec_rrs);
						printf("With signatures:\n");
						ldns_rr_list_print(stdout, nsec_rr_sigs);
						printf("correct keys:\n");
						ldns_rr_list_print(stdout, correct_key_list);
/*
						printf("trusted keys at %p:\n", trusted_keys);
						ldns_rr_list_print(stdout, trusted_keys);
*/					}
					
					if ((st = ldns_verify(nsec_rrs, nsec_rr_sigs, trusted_keys, NULL)) == LDNS_STATUS_OK) {
						fprintf(stdout, "%s ", TRUST);
						fprintf(stdout, "Existence denied: ");
						ldns_rdf_print(stdout, name);
						if (descriptor && descriptor->_name) {
							printf(" %s", descriptor->_name);
						} else {
							printf(" TYPE%u", t);
						}
						fprintf(stdout, "\n");
					} else if ((st = ldns_verify(nsec_rrs, nsec_rr_sigs, correct_key_list, NULL)) == LDNS_STATUS_OK) {
						fprintf(stdout, "%s ", SELF);
						fprintf(stdout, "Existence denied: ");
						ldns_rdf_print(stdout, name);
						if (descriptor && descriptor->_name) {
							printf(" %s", descriptor->_name);
						} else {
							printf(" TYPE%u", t);
						}
						fprintf(stdout, "\n");
					} else {
						result = 6;
						fprintf(stdout, "%s ", BOGUS);
						printf("Error verifying denial of existence for ");
						ldns_rdf_print(stdout, name);
						printf(" type ");
						if (descriptor && descriptor->_name) {
							printf("%s", descriptor->_name);
						} else {
							printf("TYPE%u", t);
						}
						printf(": %s\n", ldns_get_errorstr_by_id(st));
					}
					
					ldns_rr_list_deep_free(nsec_rrs);
					ldns_rr_list_deep_free(nsec_rr_sigs);
				} else {
/*
*/
					if (status == LDNS_STATUS_CRYPTO_NO_RRSIG) {
						printf("%s ", UNSIGNED);
						printf("No data found for: ");
						ldns_rdf_print(stdout, name);
						printf(" type ");
						if (descriptor && descriptor->_name) {
							printf("%s", descriptor->_name);
						} else {
							printf("TYPE%u", t);
						}
						printf("\n");
					} else {
						printf("[B] Unable to verify denial of existence for ");
						ldns_rdf_print(stdout, name);
						printf(" type ");
						if (descriptor && descriptor->_name) {
							printf("%s", descriptor->_name);
						} else {
							printf("TYPE%u", t);
						}
						printf("\n");
					}
				
				}
			}
			ldns_pkt_free(p);
		}

		new_nss = NULL;
		ns_addr = NULL;
		ldns_rr_list_deep_free(key_list);
		key_list = NULL;
		ldns_rr_list_deep_free(key_sig_list);
		key_sig_list = NULL;
		ds_list = NULL;
		ldns_rr_list_deep_free(ds_sig_list);
		ds_sig_list = NULL;
	}
	printf(";;" SELF " self sig OK; " BOGUS " bogus; " TRUST " trusted\n");
	/* verbose mode?
	printf("Trusted keys:\n");
	ldns_rr_list_print(stdout, trusted_keys);
	printf("trusted dss:\n");
	ldns_rr_list_print(stdout, trusted_ds_rrs);
	*/

	done:
	ldns_rr_list_deep_free(trusted_ds_rrs);
	ldns_rr_list_deep_free(correct_key_list);
	ldns_resolver_deep_free(res);
	if (labels) {
		for(i = 0 ; i < (ssize_t)labels_count + 2; i++) {
			ldns_rdf_deep_free(labels[i]);
		}
		LDNS_FREE(labels);
	}
	return result;
}
#endif /* HAVE_SSL */
