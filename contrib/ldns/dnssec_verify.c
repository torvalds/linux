#include <ldns/config.h>

#include <ldns/ldns.h>

#include <strings.h>
#include <time.h>

#ifdef HAVE_SSL
/* this entire file is rather useless when you don't have
 * crypto...
 */
#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/md5.h>

ldns_dnssec_data_chain *
ldns_dnssec_data_chain_new(void)
{
	ldns_dnssec_data_chain *nc = LDNS_CALLOC(ldns_dnssec_data_chain, 1);
        if(!nc) return NULL;
	/* 
	 * not needed anymore because CALLOC initalizes everything to zero.

	nc->rrset = NULL;
	nc->parent_type = 0;
	nc->parent = NULL;
	nc->signatures = NULL;
	nc->packet_rcode = 0;
	nc->packet_qtype = 0;
	nc->packet_nodata = false;

	 */
	return nc;
}

void
ldns_dnssec_data_chain_free(ldns_dnssec_data_chain *chain)
{
	LDNS_FREE(chain);
}

void
ldns_dnssec_data_chain_deep_free(ldns_dnssec_data_chain *chain)
{
	ldns_rr_list_deep_free(chain->rrset);
	ldns_rr_list_deep_free(chain->signatures);
	if (chain->parent) {
		ldns_dnssec_data_chain_deep_free(chain->parent);
	}
	LDNS_FREE(chain);
}

void
ldns_dnssec_data_chain_print_fmt(FILE *out, const ldns_output_format *fmt,
		const ldns_dnssec_data_chain *chain)
{
	ldns_lookup_table *rcode;
	const ldns_rr_descriptor *rr_descriptor;
	if (chain) {
		ldns_dnssec_data_chain_print_fmt(out, fmt, chain->parent);
		if (ldns_rr_list_rr_count(chain->rrset) > 0) {
			rcode = ldns_lookup_by_id(ldns_rcodes,
								 (int) chain->packet_rcode);
			if (rcode) {
				fprintf(out, ";; rcode: %s\n", rcode->name);
			}

			rr_descriptor = ldns_rr_descript(chain->packet_qtype);
			if (rr_descriptor && rr_descriptor->_name) {
				fprintf(out, ";; qtype: %s\n", rr_descriptor->_name);
			} else if (chain->packet_qtype != 0) {
				fprintf(out, "TYPE%u", 
					   chain->packet_qtype);
			}
			if (chain->packet_nodata) {
				fprintf(out, ";; NODATA response\n");
			}
			fprintf(out, "rrset:\n");
			ldns_rr_list_print_fmt(out, fmt, chain->rrset);
			fprintf(out, "sigs:\n");
			ldns_rr_list_print_fmt(out, fmt, chain->signatures);
			fprintf(out, "---\n");
		} else {
			fprintf(out, "<no data>\n");
		}
	}
}
void
ldns_dnssec_data_chain_print(FILE *out, const ldns_dnssec_data_chain *chain)
{
	ldns_dnssec_data_chain_print_fmt(
			out, ldns_output_format_default, chain);
}


static void
ldns_dnssec_build_data_chain_dnskey(ldns_resolver *res,
					    uint16_t qflags,
					    const ldns_pkt *pkt,
					    ldns_rr_list *signatures,
						ldns_dnssec_data_chain *new_chain,
						ldns_rdf *key_name,
						ldns_rr_class c) {
	ldns_rr_list *keys;
	ldns_pkt *my_pkt;
	if (signatures && ldns_rr_list_rr_count(signatures) > 0) {
		new_chain->signatures = ldns_rr_list_clone(signatures);
		new_chain->parent_type = 0;

		keys = ldns_pkt_rr_list_by_name_and_type(
				  pkt,
				 key_name,
				 LDNS_RR_TYPE_DNSKEY,
				 LDNS_SECTION_ANY_NOQUESTION
			  );
		if (!keys) {
			my_pkt = ldns_resolver_query(res,
									key_name,
									LDNS_RR_TYPE_DNSKEY,
									c,
									qflags);
			if (my_pkt) {
			keys = ldns_pkt_rr_list_by_name_and_type(
					  my_pkt,
					 key_name,
					 LDNS_RR_TYPE_DNSKEY,
					 LDNS_SECTION_ANY_NOQUESTION
				  );
			new_chain->parent = ldns_dnssec_build_data_chain(res,
													qflags,
													keys,
													my_pkt,
													NULL);
			new_chain->parent->packet_qtype = LDNS_RR_TYPE_DNSKEY;
			ldns_pkt_free(my_pkt);
			}
		} else {
			new_chain->parent = ldns_dnssec_build_data_chain(res,
													qflags,
													keys,
													pkt,
													NULL);
			new_chain->parent->packet_qtype = LDNS_RR_TYPE_DNSKEY;
		}
		ldns_rr_list_deep_free(keys);
	}
}

static void
ldns_dnssec_build_data_chain_other(ldns_resolver *res,
					    uint16_t qflags,
						ldns_dnssec_data_chain *new_chain,
						ldns_rdf *key_name,
						ldns_rr_class c,
						ldns_rr_list *dss)
{
	/* 'self-signed', parent is a DS */
	
	/* okay, either we have other keys signing the current one,
	 * or the current
	 * one should have a DS record in the parent zone.
	 * How do we find this out? Try both?
	 *
	 * request DNSKEYS for current zone,
	 * add all signatures to current level
	 */
	ldns_pkt *my_pkt;
	ldns_rr_list *signatures2;
	
	new_chain->parent_type = 1;

	my_pkt = ldns_resolver_query(res,
							key_name,
							LDNS_RR_TYPE_DS,
							c,
							qflags);
	if (my_pkt) {
	dss = ldns_pkt_rr_list_by_name_and_type(my_pkt,
									key_name,
									LDNS_RR_TYPE_DS,
									LDNS_SECTION_ANY_NOQUESTION
									);
	if (dss) {
		new_chain->parent = ldns_dnssec_build_data_chain(res,
												qflags,
												dss,
												my_pkt,
												NULL);
		new_chain->parent->packet_qtype = LDNS_RR_TYPE_DS;
		ldns_rr_list_deep_free(dss);
	}
	ldns_pkt_free(my_pkt);
	}

	my_pkt = ldns_resolver_query(res,
							key_name,
							LDNS_RR_TYPE_DNSKEY,
							c,
							qflags);
	if (my_pkt) {
	signatures2 = ldns_pkt_rr_list_by_name_and_type(my_pkt,
										   key_name,
										   LDNS_RR_TYPE_RRSIG,
										   LDNS_SECTION_ANSWER);
	if (signatures2) {
		if (new_chain->signatures) {
			printf("There were already sigs!\n");
			ldns_rr_list_deep_free(new_chain->signatures);
			printf("replacing the old sigs\n");
		}
		new_chain->signatures = signatures2;
	}
	ldns_pkt_free(my_pkt);
	}
}

static ldns_dnssec_data_chain *
ldns_dnssec_build_data_chain_nokeyname(ldns_resolver *res,
                                       uint16_t qflags,
                                       ldns_rr *orig_rr,
                                       const ldns_rr_list *rrset,
                                       ldns_dnssec_data_chain *new_chain)
{
	ldns_rdf *possible_parent_name;
	ldns_pkt *my_pkt;
	/* apparently we were not able to find a signing key, so
	   we assume the chain ends here
	*/
	/* try parents for auth denial of DS */
	if (orig_rr) {
		possible_parent_name = ldns_rr_owner(orig_rr);
	} else if (rrset && ldns_rr_list_rr_count(rrset) > 0) {
		possible_parent_name = ldns_rr_owner(ldns_rr_list_rr(rrset, 0));
	} else {
		/* no information to go on, give up */
		return new_chain;
	}

	my_pkt = ldns_resolver_query(res,
	              possible_parent_name,
	              LDNS_RR_TYPE_DS,
	              LDNS_RR_CLASS_IN,
	              qflags);
	if (!my_pkt) {
		return new_chain;
	}

	if (ldns_pkt_ancount(my_pkt) > 0) {
		/* add error, no sigs but DS in parent */
		/*ldns_pkt_print(stdout, my_pkt);*/
		ldns_pkt_free(my_pkt);
	} else {
		/* are there signatures? */
		new_chain->parent =  ldns_dnssec_build_data_chain(res, 
		                          qflags, 
		                          NULL,
		                          my_pkt,
		                          NULL);

		new_chain->parent->packet_qtype = LDNS_RR_TYPE_DS;
		
	}
	return new_chain;
}


ldns_dnssec_data_chain *
ldns_dnssec_build_data_chain(ldns_resolver *res,
					    uint16_t qflags,
					    const ldns_rr_list *rrset,
					    const ldns_pkt *pkt,
					    ldns_rr *orig_rr)
{
	ldns_rr_list *signatures = NULL;
	ldns_rr_list *dss = NULL;
	
	ldns_rr_list *my_rrset;

	ldns_pkt *my_pkt;

	ldns_rdf *name = NULL, *key_name = NULL;
	ldns_rr_type type = 0;
	ldns_rr_class c = 0;

	bool other_rrset = false;

	ldns_dnssec_data_chain *new_chain = ldns_dnssec_data_chain_new();

	assert(pkt != NULL);

	if (!ldns_dnssec_pkt_has_rrsigs(pkt)) {
		/* hmm. no dnssec data in the packet. go up to try and deny
		 * DS? */
		return new_chain;
	}

	if (orig_rr) {
		new_chain->rrset = ldns_rr_list_new();
		ldns_rr_list_push_rr(new_chain->rrset, orig_rr);
		new_chain->parent = ldns_dnssec_build_data_chain(res,
											    qflags,
											    rrset,
											    pkt,
											    NULL);
		new_chain->packet_rcode = ldns_pkt_get_rcode(pkt);
		new_chain->packet_qtype = ldns_rr_get_type(orig_rr);
		if (ldns_pkt_ancount(pkt) == 0) {
			new_chain->packet_nodata = true;
		}
		return new_chain;
	}
	
	if (!rrset || ldns_rr_list_rr_count(rrset) < 1) {
		/* hmm, no data, do we have denial? only works if pkt was given,
		   otherwise caller has to do the check himself */
		new_chain->packet_nodata = true;
		if (pkt) {
			my_rrset = ldns_pkt_rr_list_by_type(pkt,
										 LDNS_RR_TYPE_NSEC,
										 LDNS_SECTION_ANY_NOQUESTION
										 );
			if (my_rrset) {
				if (ldns_rr_list_rr_count(my_rrset) > 0) {
					type = LDNS_RR_TYPE_NSEC;
					other_rrset = true;
				} else {
					ldns_rr_list_deep_free(my_rrset);
					my_rrset = NULL;
				}
			} else {
				/* nothing, try nsec3 */
				my_rrset = ldns_pkt_rr_list_by_type(pkt,
						     LDNS_RR_TYPE_NSEC3,
							LDNS_SECTION_ANY_NOQUESTION);
				if (my_rrset) {
					if (ldns_rr_list_rr_count(my_rrset) > 0) {
						type = LDNS_RR_TYPE_NSEC3;
						other_rrset = true;
					} else {
						ldns_rr_list_deep_free(my_rrset);
						my_rrset = NULL;
					}
				} else {
					/* nothing, stop */
					/* try parent zone? for denied insecure? */
					return new_chain;
				}
			}
		} else {
			return new_chain;
		}
	} else {
		my_rrset = (ldns_rr_list *) rrset;
	}
	
	if (my_rrset && ldns_rr_list_rr_count(my_rrset) > 0) {
		new_chain->rrset = ldns_rr_list_clone(my_rrset);
		name = ldns_rr_owner(ldns_rr_list_rr(my_rrset, 0));
		type = ldns_rr_get_type(ldns_rr_list_rr(my_rrset, 0));
		c = ldns_rr_get_class(ldns_rr_list_rr(my_rrset, 0));
	}
	
	if (other_rrset) {
		ldns_rr_list_deep_free(my_rrset);
	}
	
	/* normally there will only be 1 signature 'set'
	   but there can be more than 1 denial (wildcards)
	   so check for NSEC
	*/
	if (type == LDNS_RR_TYPE_NSEC || type == LDNS_RR_TYPE_NSEC3) {
		/* just throw in all signatures, the tree builder must sort
		   this out */
		if (pkt) {
			signatures = ldns_dnssec_pkt_get_rrsigs_for_type(pkt, type);
		} else {
			my_pkt = ldns_resolver_query(res, name, type, c, qflags);
			if (my_pkt) {
			signatures = ldns_dnssec_pkt_get_rrsigs_for_type(pkt, type);
			ldns_pkt_free(my_pkt);
			}
		}
	} else {
		if (pkt) {
			signatures =
				ldns_dnssec_pkt_get_rrsigs_for_name_and_type(pkt,
													name,
													type);
		}
		if (!signatures) {
			my_pkt = ldns_resolver_query(res, name, type, c, qflags);
			if (my_pkt) {
			signatures =
				ldns_dnssec_pkt_get_rrsigs_for_name_and_type(my_pkt,
													name,
													type);
			ldns_pkt_free(my_pkt);
			}
		}
	}

	if (signatures && ldns_rr_list_rr_count(signatures) > 0) {
		key_name = ldns_rr_rdf(ldns_rr_list_rr(signatures, 0), 7);
	}
	if (!key_name) {
		if (signatures) {
			ldns_rr_list_deep_free(signatures);
		}
		return ldns_dnssec_build_data_chain_nokeyname(res,
		                                              qflags,
		                                              orig_rr,
		                                              rrset,
		                                              new_chain);
	}
	if (type != LDNS_RR_TYPE_DNSKEY) {
		ldns_dnssec_build_data_chain_dnskey(res,
		                                    qflags,
		                                    pkt,
		                                    signatures,
		                                    new_chain,
		                                    key_name,
		                                    c
		                                   );
	} else {
		ldns_dnssec_build_data_chain_other(res,
		                                   qflags,
		                                   new_chain,
		                                   key_name,
		                                   c,
		                                   dss
		                                  );
	}
	if (signatures) {
		ldns_rr_list_deep_free(signatures);
	}
	return new_chain;
}

ldns_dnssec_trust_tree *
ldns_dnssec_trust_tree_new(void)
{
	ldns_dnssec_trust_tree *new_tree = LDNS_XMALLOC(ldns_dnssec_trust_tree,
										   1);
        if(!new_tree) return NULL;
	new_tree->rr = NULL;
	new_tree->rrset = NULL;
	new_tree->parent_count = 0;

	return new_tree;
}

void
ldns_dnssec_trust_tree_free(ldns_dnssec_trust_tree *tree)
{
	size_t i;
	if (tree) {
		for (i = 0; i < tree->parent_count; i++) {
			ldns_dnssec_trust_tree_free(tree->parents[i]);
		}
	}
	LDNS_FREE(tree);
}

size_t
ldns_dnssec_trust_tree_depth(ldns_dnssec_trust_tree *tree)
{
	size_t result = 0;
	size_t parent = 0;
	size_t i;
	
	for (i = 0; i < tree->parent_count; i++) {
		parent = ldns_dnssec_trust_tree_depth(tree->parents[i]);
		if (parent > result) {
			result = parent;
		}
	}
	return 1 + result;
}

/* TODO ldns_ */
static void
print_tabs(FILE *out, size_t nr, uint8_t *map, size_t treedepth)
{
	size_t i;
	for (i = 0; i < nr; i++) {
		if (i == nr - 1) {
			fprintf(out, "|---");
		} else if (map && i < treedepth && map[i] == 1) {
			fprintf(out, "|   ");
		} else {
			fprintf(out, "    ");
		}
	}
}

static void
ldns_dnssec_trust_tree_print_sm_fmt(FILE *out, 
		const ldns_output_format *fmt,
		ldns_dnssec_trust_tree *tree,
		size_t tabs,
		bool extended,
		uint8_t *sibmap,
		size_t treedepth)
{
	size_t i;
	const ldns_rr_descriptor *descriptor;
	bool mapset = false;
	
	if (!sibmap) {
		treedepth = ldns_dnssec_trust_tree_depth(tree);
		sibmap = LDNS_XMALLOC(uint8_t, treedepth);
                if(!sibmap)
                        return; /* mem err */
		memset(sibmap, 0, treedepth);
		mapset = true;
	}
	
	if (tree) {
		if (tree->rr) {
			print_tabs(out, tabs, sibmap, treedepth);
			ldns_rdf_print(out, ldns_rr_owner(tree->rr));
			descriptor = ldns_rr_descript(ldns_rr_get_type(tree->rr));

			if (descriptor->_name) {
				fprintf(out, " (%s", descriptor->_name);
			} else {
				fprintf(out, " (TYPE%d", 
					   ldns_rr_get_type(tree->rr));
			}
			if (tabs > 0) {
				if (ldns_rr_get_type(tree->rr) == LDNS_RR_TYPE_DNSKEY) {
					fprintf(out, " keytag: %u",
					        (unsigned int) ldns_calc_keytag(tree->rr));
					fprintf(out, " alg: ");
					ldns_rdf_print(out, ldns_rr_rdf(tree->rr, 2));
					fprintf(out, " flags: ");
					ldns_rdf_print(out, ldns_rr_rdf(tree->rr, 0));
				} else if (ldns_rr_get_type(tree->rr) == LDNS_RR_TYPE_DS) {
					fprintf(out, " keytag: ");
					ldns_rdf_print(out, ldns_rr_rdf(tree->rr, 0));
					fprintf(out, " digest type: ");
					ldns_rdf_print(out, ldns_rr_rdf(tree->rr, 2));
				}
				if (ldns_rr_get_type(tree->rr) == LDNS_RR_TYPE_NSEC) {
					fprintf(out, " ");
					ldns_rdf_print(out, ldns_rr_rdf(tree->rr, 0));
					fprintf(out, " ");
					ldns_rdf_print(out, ldns_rr_rdf(tree->rr, 1));
				}
			}
			
			fprintf(out, ")\n");
			for (i = 0; i < tree->parent_count; i++) {
				if (tree->parent_count > 1 && i < tree->parent_count - 1) {
					sibmap[tabs] = 1;
				} else {
					sibmap[tabs] = 0;
				}
				/* only print errors */
				if (ldns_rr_get_type(tree->parents[i]->rr) == 
				    LDNS_RR_TYPE_NSEC ||
				    ldns_rr_get_type(tree->parents[i]->rr) ==
				    LDNS_RR_TYPE_NSEC3) {
					if (tree->parent_status[i] == LDNS_STATUS_OK) {
						print_tabs(out, tabs + 1, sibmap, treedepth);
						if (tabs == 0 &&
						    ldns_rr_get_type(tree->rr) == LDNS_RR_TYPE_NS &&
							ldns_rr_rd_count(tree->rr) > 0) {
							fprintf(out, "Existence of DS is denied by:\n");
						} else {
							fprintf(out, "Existence is denied by:\n");
						}
					} else {
						/* NS records aren't signed */
						if (ldns_rr_get_type(tree->rr) == LDNS_RR_TYPE_NS) {
							fprintf(out, "Existence of DS is denied by:\n");
						} else {
							print_tabs(out, tabs + 1, sibmap, treedepth);
							fprintf(out,
								   "Error in denial of existence: %s\n",
								   ldns_get_errorstr_by_id(
									   tree->parent_status[i]));
						}
					}
				} else
					if (tree->parent_status[i] != LDNS_STATUS_OK) {
						print_tabs(out, tabs + 1, sibmap, treedepth);
						fprintf(out,
							   "%s:\n",
							   ldns_get_errorstr_by_id(
							       tree->parent_status[i]));
						if (tree->parent_status[i]
						    == LDNS_STATUS_SSL_ERR) {
							printf("; SSL Error: ");
							ERR_load_crypto_strings();
							ERR_print_errors_fp(stdout);
							printf("\n");
						}
						ldns_rr_print_fmt(out, fmt, 
							tree->
							parent_signature[i]);
						printf("For RRset:\n");
						ldns_rr_list_print_fmt(out, fmt,
								tree->rrset);
						printf("With key:\n");
						ldns_rr_print_fmt(out, fmt,
							tree->parents[i]->rr);
					}
				ldns_dnssec_trust_tree_print_sm_fmt(out, fmt,
						tree->parents[i],
						tabs+1,
						extended,
						sibmap,
						treedepth);
			}
		} else {
			print_tabs(out, tabs, sibmap, treedepth);
			fprintf(out, "<no data>\n");
		}
	} else {
		fprintf(out, "<null pointer>\n");
	}
	
	if (mapset) {
		LDNS_FREE(sibmap);
	}
}

void
ldns_dnssec_trust_tree_print_fmt(FILE *out, const ldns_output_format *fmt,
		ldns_dnssec_trust_tree *tree,
		size_t tabs,
		bool extended)
{
	ldns_dnssec_trust_tree_print_sm_fmt(out, fmt, 
			tree, tabs, extended, NULL, 0);
}

void
ldns_dnssec_trust_tree_print(FILE *out,
		ldns_dnssec_trust_tree *tree,
		size_t tabs,
		bool extended)
{
	ldns_dnssec_trust_tree_print_fmt(out, ldns_output_format_default, 
			tree, tabs, extended);
}


ldns_status
ldns_dnssec_trust_tree_add_parent(ldns_dnssec_trust_tree *tree,
                                  const ldns_dnssec_trust_tree *parent,
                                  const ldns_rr *signature,
                                  const ldns_status parent_status)
{
	if (tree
	    && parent
	    && tree->parent_count < LDNS_DNSSEC_TRUST_TREE_MAX_PARENTS) {
		/*
		  printf("Add parent for: ");
		  ldns_rr_print(stdout, tree->rr);
		  printf("parent: ");
		  ldns_rr_print(stdout, parent->rr);
		*/
		tree->parents[tree->parent_count] =
			(ldns_dnssec_trust_tree *) parent;
		tree->parent_status[tree->parent_count] = parent_status;
		tree->parent_signature[tree->parent_count] = (ldns_rr *) signature;
		tree->parent_count++;
		return LDNS_STATUS_OK;
	} else {
		return LDNS_STATUS_ERR;
	}
}

/* if rr is null, take the first from the rrset */
ldns_dnssec_trust_tree *
ldns_dnssec_derive_trust_tree_time(
		ldns_dnssec_data_chain *data_chain, 
		ldns_rr *rr, 
		time_t check_time
		)
{
	ldns_rr_list *cur_rrset;
	ldns_rr_list *cur_sigs;
	ldns_rr *cur_rr = NULL;
	ldns_rr *cur_sig_rr;
	size_t i, j;

	ldns_dnssec_trust_tree *new_tree = ldns_dnssec_trust_tree_new();
        if(!new_tree)
                return NULL;
	
	if (data_chain && data_chain->rrset) {
		cur_rrset = data_chain->rrset;
	
		cur_sigs = data_chain->signatures;

		if (rr) {
			cur_rr = rr;
		}

		if (!cur_rr && ldns_rr_list_rr_count(cur_rrset) > 0) {
			cur_rr = ldns_rr_list_rr(cur_rrset, 0);
		}

		if (cur_rr) {
			new_tree->rr = cur_rr;
			new_tree->rrset = cur_rrset;
			/* there are three possibilities:
			   1 - 'normal' rrset, signed by a key
			   2 - dnskey signed by other dnskey
			   3 - dnskey proven by higher level DS
			   (data denied by nsec is a special case that can
			   occur in multiple places)
				   
			*/
			if (cur_sigs) {
				for (i = 0; i < ldns_rr_list_rr_count(cur_sigs); i++) {
					/* find the appropriate key in the parent list */
					cur_sig_rr = ldns_rr_list_rr(cur_sigs, i);

					if (ldns_rr_get_type(cur_rr) == LDNS_RR_TYPE_NSEC) {
						if (ldns_dname_compare(ldns_rr_owner(cur_sig_rr),
										   ldns_rr_owner(cur_rr)))
							{
								/* find first that does match */

								for (j = 0;
								     j < ldns_rr_list_rr_count(cur_rrset) && 
										ldns_dname_compare(ldns_rr_owner(cur_sig_rr),ldns_rr_owner(cur_rr)) != 0;
								     j++) {
									cur_rr = ldns_rr_list_rr(cur_rrset, j);
									
								}
								if (ldns_dname_compare(ldns_rr_owner(cur_sig_rr), 
												   ldns_rr_owner(cur_rr)))
									{
										break;
									}
							}
							
					}
					/* option 1 */
					if (data_chain->parent) {
						ldns_dnssec_derive_trust_tree_normal_rrset_time(
						    new_tree,
						    data_chain,
						    cur_sig_rr,
						    check_time);
					}

					/* option 2 */
					ldns_dnssec_derive_trust_tree_dnskey_rrset_time(
					    new_tree,
					    data_chain,
					    cur_rr,
					    cur_sig_rr,
					    check_time);
				}
					
				ldns_dnssec_derive_trust_tree_ds_rrset_time(
						new_tree, data_chain, 
						cur_rr, check_time);
			} else {
				/* no signatures? maybe it's nsec data */
					
				/* just add every rr from parent as new parent */
				ldns_dnssec_derive_trust_tree_no_sig_time(
					new_tree, data_chain, check_time);
			}
		}
	}

	return new_tree;
}

ldns_dnssec_trust_tree *
ldns_dnssec_derive_trust_tree(ldns_dnssec_data_chain *data_chain, ldns_rr *rr)
{
	return ldns_dnssec_derive_trust_tree_time(data_chain, rr, ldns_time(NULL));
}

void
ldns_dnssec_derive_trust_tree_normal_rrset_time(
		ldns_dnssec_trust_tree *new_tree, 
		ldns_dnssec_data_chain *data_chain, 
		ldns_rr *cur_sig_rr,
		time_t check_time)
{
	size_t i, j;
	ldns_rr_list *cur_rrset = ldns_rr_list_clone(data_chain->rrset); 
	ldns_dnssec_trust_tree *cur_parent_tree;
	ldns_rr *cur_parent_rr;
	uint16_t cur_keytag;
	ldns_rr_list *tmp_rrset = NULL;
	ldns_status cur_status;

	cur_keytag = ldns_rdf2native_int16(ldns_rr_rrsig_keytag(cur_sig_rr));
	
	for (j = 0; j < ldns_rr_list_rr_count(data_chain->parent->rrset); j++) {
		cur_parent_rr = ldns_rr_list_rr(data_chain->parent->rrset, j);
		if (ldns_rr_get_type(cur_parent_rr) == LDNS_RR_TYPE_DNSKEY) {
			if (ldns_calc_keytag(cur_parent_rr) == cur_keytag) {

				/* TODO: check wildcard nsec too */
				if (cur_rrset && ldns_rr_list_rr_count(cur_rrset) > 0) {
					tmp_rrset = cur_rrset;
					if (ldns_rr_get_type(ldns_rr_list_rr(cur_rrset, 0))
					    == LDNS_RR_TYPE_NSEC ||
					    ldns_rr_get_type(ldns_rr_list_rr(cur_rrset, 0))
					    == LDNS_RR_TYPE_NSEC3) {
						/* might contain different names! 
						   sort and split */
						ldns_rr_list_sort(cur_rrset);
						assert(tmp_rrset == cur_rrset);
						tmp_rrset = ldns_rr_list_pop_rrset(cur_rrset);
						
						/* with nsecs, this might be the wrong one */
						while (tmp_rrset &&
						       ldns_rr_list_rr_count(cur_rrset) > 0 &&
						       ldns_dname_compare(
								ldns_rr_owner(ldns_rr_list_rr(
										        tmp_rrset, 0)),
								ldns_rr_owner(cur_sig_rr)) != 0) {
							ldns_rr_list_deep_free(tmp_rrset);
							tmp_rrset =
								ldns_rr_list_pop_rrset(cur_rrset);
						}
					}
					cur_status = ldns_verify_rrsig_time(
							tmp_rrset, 
							cur_sig_rr, 
							cur_parent_rr,
							check_time);
					if (tmp_rrset && tmp_rrset != cur_rrset
							) {
						ldns_rr_list_deep_free(
								tmp_rrset);
						tmp_rrset = NULL;
					}
					/* avoid dupes */
					for (i = 0; i < new_tree->parent_count; i++) {
						if (cur_parent_rr == new_tree->parents[i]->rr) {
							goto done;
						}
					}

					cur_parent_tree =
						ldns_dnssec_derive_trust_tree_time(
								data_chain->parent,
						                cur_parent_rr,
								check_time);
					(void)ldns_dnssec_trust_tree_add_parent(new_tree,
					           cur_parent_tree,
					           cur_sig_rr,
					           cur_status);
				}
			}
		}
	}
 done:
	ldns_rr_list_deep_free(cur_rrset);
}

void
ldns_dnssec_derive_trust_tree_normal_rrset(ldns_dnssec_trust_tree *new_tree,
                                           ldns_dnssec_data_chain *data_chain,
                                           ldns_rr *cur_sig_rr)
{
	ldns_dnssec_derive_trust_tree_normal_rrset_time(
			new_tree, data_chain, cur_sig_rr, ldns_time(NULL));
}

void
ldns_dnssec_derive_trust_tree_dnskey_rrset_time(
		ldns_dnssec_trust_tree *new_tree, 
		ldns_dnssec_data_chain *data_chain, 
		ldns_rr *cur_rr, 
		ldns_rr *cur_sig_rr,
		time_t check_time)
{
	size_t j;
	ldns_rr_list *cur_rrset = data_chain->rrset;
	ldns_dnssec_trust_tree *cur_parent_tree;
	ldns_rr *cur_parent_rr;
	uint16_t cur_keytag;
	ldns_status cur_status;

	cur_keytag = ldns_rdf2native_int16(ldns_rr_rrsig_keytag(cur_sig_rr));

	for (j = 0; j < ldns_rr_list_rr_count(cur_rrset); j++) {
		cur_parent_rr = ldns_rr_list_rr(cur_rrset, j);
		if (cur_parent_rr != cur_rr &&
		    ldns_rr_get_type(cur_parent_rr) == LDNS_RR_TYPE_DNSKEY) {
			if (ldns_calc_keytag(cur_parent_rr) == cur_keytag
			    ) {
				cur_parent_tree = ldns_dnssec_trust_tree_new();
				cur_parent_tree->rr = cur_parent_rr;
				cur_parent_tree->rrset = cur_rrset;
				cur_status = ldns_verify_rrsig_time(
						cur_rrset, cur_sig_rr, 
						cur_parent_rr, check_time);
				(void) ldns_dnssec_trust_tree_add_parent(new_tree,
				            cur_parent_tree, cur_sig_rr, cur_status);
			}
		}
	}
}

void
ldns_dnssec_derive_trust_tree_dnskey_rrset(ldns_dnssec_trust_tree *new_tree,
                                           ldns_dnssec_data_chain *data_chain,
                                           ldns_rr *cur_rr,
                                           ldns_rr *cur_sig_rr)
{
	ldns_dnssec_derive_trust_tree_dnskey_rrset_time(
			new_tree, data_chain, cur_rr, cur_sig_rr, ldns_time(NULL));
}

void
ldns_dnssec_derive_trust_tree_ds_rrset_time(
		ldns_dnssec_trust_tree *new_tree,
		ldns_dnssec_data_chain *data_chain, 
		ldns_rr *cur_rr,
		time_t check_time)
{
	size_t j, h;
	ldns_rr_list *cur_rrset = data_chain->rrset;
	ldns_dnssec_trust_tree *cur_parent_tree;
	ldns_rr *cur_parent_rr;

	/* try the parent to see whether there are DSs there */
	if (ldns_rr_get_type(cur_rr) == LDNS_RR_TYPE_DNSKEY &&
	    data_chain->parent &&
	    data_chain->parent->rrset
	    ) {
		for (j = 0;
			j < ldns_rr_list_rr_count(data_chain->parent->rrset);
			j++) {
			cur_parent_rr = ldns_rr_list_rr(data_chain->parent->rrset, j);
			if (ldns_rr_get_type(cur_parent_rr) == LDNS_RR_TYPE_DS) {
				for (h = 0; h < ldns_rr_list_rr_count(cur_rrset); h++) {
					cur_rr = ldns_rr_list_rr(cur_rrset, h);
					if (ldns_rr_compare_ds(cur_rr, cur_parent_rr)) {
						cur_parent_tree =
							ldns_dnssec_derive_trust_tree_time(
							    data_chain->parent, 
							    cur_parent_rr,
							    check_time);
						(void) ldns_dnssec_trust_tree_add_parent(
						            new_tree,
						            cur_parent_tree,
						            NULL,
						            LDNS_STATUS_OK);
					} else {
						/*ldns_rr_print(stdout, cur_parent_rr);*/
					}
				}
			}
		}
	}
}

void
ldns_dnssec_derive_trust_tree_ds_rrset(ldns_dnssec_trust_tree *new_tree,
                                       ldns_dnssec_data_chain *data_chain,
                                       ldns_rr *cur_rr)
{
	ldns_dnssec_derive_trust_tree_ds_rrset_time(
			new_tree, data_chain, cur_rr, ldns_time(NULL));
}

void
ldns_dnssec_derive_trust_tree_no_sig_time(
		ldns_dnssec_trust_tree *new_tree, 
		ldns_dnssec_data_chain *data_chain,
		time_t check_time)
{
	size_t i;
	ldns_rr_list *cur_rrset;
	ldns_rr *cur_parent_rr;
	ldns_dnssec_trust_tree *cur_parent_tree;
	ldns_status result;
	
	if (data_chain->parent && data_chain->parent->rrset) {
		cur_rrset = data_chain->parent->rrset;
		/* nsec? */
		if (cur_rrset && ldns_rr_list_rr_count(cur_rrset) > 0) {
			if (ldns_rr_get_type(ldns_rr_list_rr(cur_rrset, 0)) ==
			    LDNS_RR_TYPE_NSEC3) {
				result = ldns_dnssec_verify_denial_nsec3(
					        new_tree->rr,
						   cur_rrset,
						   data_chain->parent->signatures,
						   data_chain->packet_rcode,
						   data_chain->packet_qtype,
						   data_chain->packet_nodata);
			} else if (ldns_rr_get_type(ldns_rr_list_rr(cur_rrset, 0)) ==
					 LDNS_RR_TYPE_NSEC) {
				result = ldns_dnssec_verify_denial(
					        new_tree->rr,
						   cur_rrset,
						   data_chain->parent->signatures);
			} else {
				/* unsigned zone, unsigned parent */
				result = LDNS_STATUS_OK;
			}
		} else {
			result = LDNS_STATUS_DNSSEC_NSEC_RR_NOT_COVERED;
		}
		for (i = 0; i < ldns_rr_list_rr_count(cur_rrset); i++) {
			cur_parent_rr = ldns_rr_list_rr(cur_rrset, i);
			cur_parent_tree = 
				ldns_dnssec_derive_trust_tree_time(
						data_chain->parent, 
						cur_parent_rr,
						check_time);
			(void) ldns_dnssec_trust_tree_add_parent(new_tree,
			            cur_parent_tree, NULL, result);
		}
	}
}

void
ldns_dnssec_derive_trust_tree_no_sig(ldns_dnssec_trust_tree *new_tree,
                                     ldns_dnssec_data_chain *data_chain)
{
	ldns_dnssec_derive_trust_tree_no_sig_time(
			new_tree, data_chain, ldns_time(NULL));
}

/*
 * returns OK if there is a path from tree to key with only OK
 * the (first) error in between otherwise
 * or NOT_FOUND if the key wasn't present at all
 */
ldns_status
ldns_dnssec_trust_tree_contains_keys(ldns_dnssec_trust_tree *tree,
							  ldns_rr_list *trusted_keys)
{
	size_t i;
	ldns_status result = LDNS_STATUS_CRYPTO_NO_DNSKEY;
	bool equal;
	ldns_status parent_result;
	
	if (tree && trusted_keys && ldns_rr_list_rr_count(trusted_keys) > 0)
		{ if (tree->rr) {
				for (i = 0; i < ldns_rr_list_rr_count(trusted_keys); i++) {
					equal = ldns_rr_compare_ds(
							  tree->rr,
							  ldns_rr_list_rr(trusted_keys, i));
					if (equal) {
						result = LDNS_STATUS_OK;
						return result;
					}
				}
			}
			for (i = 0; i < tree->parent_count; i++) {
				parent_result =
					ldns_dnssec_trust_tree_contains_keys(tree->parents[i],
												  trusted_keys);
				if (parent_result != LDNS_STATUS_CRYPTO_NO_DNSKEY) {
					if (tree->parent_status[i] != LDNS_STATUS_OK) {
						result = tree->parent_status[i];
					} else {
						if (tree->rr &&
						    ldns_rr_get_type(tree->rr)
						    == LDNS_RR_TYPE_NSEC &&
						    parent_result == LDNS_STATUS_OK
						    ) {
							result =
								LDNS_STATUS_DNSSEC_EXISTENCE_DENIED;
						} else {
							result = parent_result;
						}
					}
				}
			}
		} else {
		result = LDNS_STATUS_ERR;
	}
	
	return result;
}

ldns_status
ldns_verify_time(
		const ldns_rr_list *rrset,
		const ldns_rr_list *rrsig, 
		const ldns_rr_list *keys, 
		time_t check_time,
		ldns_rr_list *good_keys
		)
{
	uint16_t i;
	ldns_status verify_result = LDNS_STATUS_ERR;

	if (!rrset || !rrsig || !keys) {
		return LDNS_STATUS_ERR;
	}

	if (ldns_rr_list_rr_count(rrset) < 1) {
		return LDNS_STATUS_ERR;
	}

	if (ldns_rr_list_rr_count(rrsig) < 1) {
		return LDNS_STATUS_CRYPTO_NO_RRSIG;
	}
	
	if (ldns_rr_list_rr_count(keys) < 1) {
		verify_result = LDNS_STATUS_CRYPTO_NO_TRUSTED_DNSKEY;
	} else {
		for (i = 0; i < ldns_rr_list_rr_count(rrsig); i++) {
			ldns_status s = ldns_verify_rrsig_keylist_time(
					rrset, ldns_rr_list_rr(rrsig, i), 
					keys, check_time, good_keys);
			/* try a little to get more descriptive error */
			if(s == LDNS_STATUS_OK) {
				verify_result = LDNS_STATUS_OK;
			} else if(verify_result == LDNS_STATUS_ERR)
				verify_result = s;
			else if(s !=  LDNS_STATUS_ERR && verify_result ==
				LDNS_STATUS_CRYPTO_NO_MATCHING_KEYTAG_DNSKEY)
				verify_result = s;
		}
	}
	return verify_result;
}

ldns_status
ldns_verify(ldns_rr_list *rrset, ldns_rr_list *rrsig, const ldns_rr_list *keys, 
		  ldns_rr_list *good_keys)
{
	return ldns_verify_time(rrset, rrsig, keys, ldns_time(NULL), good_keys);
}

ldns_status
ldns_verify_notime(ldns_rr_list *rrset, ldns_rr_list *rrsig,
	const ldns_rr_list *keys, ldns_rr_list *good_keys)
{
	uint16_t i;
	ldns_status verify_result = LDNS_STATUS_ERR;

	if (!rrset || !rrsig || !keys) {
		return LDNS_STATUS_ERR;
	}

	if (ldns_rr_list_rr_count(rrset) < 1) {
		return LDNS_STATUS_ERR;
	}

	if (ldns_rr_list_rr_count(rrsig) < 1) {
		return LDNS_STATUS_CRYPTO_NO_RRSIG;
	}

	if (ldns_rr_list_rr_count(keys) < 1) {
		verify_result = LDNS_STATUS_CRYPTO_NO_TRUSTED_DNSKEY;
	} else {
		for (i = 0; i < ldns_rr_list_rr_count(rrsig); i++) {
			ldns_status s = ldns_verify_rrsig_keylist_notime(rrset,
				ldns_rr_list_rr(rrsig, i), keys, good_keys);

			/* try a little to get more descriptive error */
			if (s == LDNS_STATUS_OK) {
				verify_result = LDNS_STATUS_OK;
			} else if (verify_result == LDNS_STATUS_ERR) {
				verify_result = s;
			} else if (s !=  LDNS_STATUS_ERR && verify_result ==
				LDNS_STATUS_CRYPTO_NO_MATCHING_KEYTAG_DNSKEY) {
				verify_result = s;
			}
		}
	}
	return verify_result;
}

ldns_rr_list *
ldns_fetch_valid_domain_keys_time(const ldns_resolver *res,
                             const ldns_rdf *domain,
                             const ldns_rr_list *keys,
			     time_t check_time,
                             ldns_status *status)
{
	ldns_rr_list * trusted_keys = NULL;
	ldns_rr_list * ds_keys = NULL;
	ldns_rdf * prev_parent_domain;
	ldns_rdf *      parent_domain;
	ldns_rr_list * parent_keys = NULL;

	if (res && domain && keys) {

		if ((trusted_keys = ldns_validate_domain_dnskey_time(res,
                                         domain, keys, check_time))) {
			*status = LDNS_STATUS_OK;
		} else {
			/* No trusted keys in this domain, we'll have to find some in the parent domain */
			*status = LDNS_STATUS_CRYPTO_NO_TRUSTED_DNSKEY;

			parent_domain = ldns_dname_left_chop(domain);
			while (parent_domain && /* Fail if we are at the root*/
					ldns_rdf_size(parent_domain) > 0) {
	
				if ((parent_keys = 
					ldns_fetch_valid_domain_keys_time(res,
					     parent_domain,
					     keys,
					     check_time,
					     status))) {
					/* Check DS records */
					if ((ds_keys =
						ldns_validate_domain_ds_time(res,
						     domain,
						     parent_keys,
						     check_time))) {
						trusted_keys =
						ldns_fetch_valid_domain_keys_time(
								res, 
								domain, 
								ds_keys, 
								check_time,
								status);
						ldns_rr_list_deep_free(ds_keys);
					} else {
						/* No valid DS at the parent -- fail */
						*status = LDNS_STATUS_CRYPTO_NO_TRUSTED_DS ;
					}
					ldns_rr_list_deep_free(parent_keys);
					break;
				} else {
					parent_domain = ldns_dname_left_chop((
						prev_parent_domain 
							= parent_domain
						));
					ldns_rdf_deep_free(prev_parent_domain);
				}
			}
			if (parent_domain) {
				ldns_rdf_deep_free(parent_domain);
			}
		}
	}
	return trusted_keys;
}

ldns_rr_list *
ldns_fetch_valid_domain_keys(const ldns_resolver *res,
                             const ldns_rdf *domain,
                             const ldns_rr_list *keys,
                             ldns_status *status)
{
	return ldns_fetch_valid_domain_keys_time(
			res, domain, keys, ldns_time(NULL), status);
}

ldns_rr_list *
ldns_validate_domain_dnskey_time(
		const ldns_resolver * res,
		const ldns_rdf * domain,
		const ldns_rr_list * keys,
		time_t check_time
		)
{
	ldns_pkt * keypkt;
	ldns_rr * cur_key;
	uint16_t key_i; uint16_t key_j; uint16_t key_k;
	uint16_t sig_i; ldns_rr * cur_sig;

	ldns_rr_list * domain_keys = NULL;
	ldns_rr_list * domain_sigs = NULL;
	ldns_rr_list * trusted_keys = NULL;

	/* Fetch keys for the domain */
	keypkt = ldns_resolver_query(res, domain,
		LDNS_RR_TYPE_DNSKEY, LDNS_RR_CLASS_IN, LDNS_RD);
	if (keypkt) {
		domain_keys = ldns_pkt_rr_list_by_type(keypkt,
									    LDNS_RR_TYPE_DNSKEY,
									    LDNS_SECTION_ANSWER);
		domain_sigs = ldns_pkt_rr_list_by_type(keypkt,
									    LDNS_RR_TYPE_RRSIG,
									    LDNS_SECTION_ANSWER);

		/* Try to validate the record using our keys */
		for (key_i=0; key_i< ldns_rr_list_rr_count(domain_keys); key_i++) {
      
			cur_key = ldns_rr_list_rr(domain_keys, key_i);
			for (key_j=0; key_j<ldns_rr_list_rr_count(keys); key_j++) {
				if (ldns_rr_compare_ds(ldns_rr_list_rr(keys, key_j),
								   cur_key)) {
          
					/* Current key is trusted -- validate */
					trusted_keys = ldns_rr_list_new();
          
					for (sig_i=0;
						sig_i<ldns_rr_list_rr_count(domain_sigs);
						sig_i++) {
						cur_sig = ldns_rr_list_rr(domain_sigs, sig_i);
						/* Avoid non-matching sigs */
						if (ldns_rdf2native_int16(
							   ldns_rr_rrsig_keytag(cur_sig))
						    == ldns_calc_keytag(cur_key)) {
							if (ldns_verify_rrsig_time(
									domain_keys,
									cur_sig,
									cur_key,
									check_time)
							    == LDNS_STATUS_OK) {
                
								/* Push the whole rrset 
								   -- we can't do much more */
								for (key_k=0;
									key_k<ldns_rr_list_rr_count(
											domain_keys);
									key_k++) {
									ldns_rr_list_push_rr(
									    trusted_keys,
									    ldns_rr_clone(
										   ldns_rr_list_rr(
											  domain_keys,
											  key_k)));
								}
                
								ldns_rr_list_deep_free(domain_keys);
								ldns_rr_list_deep_free(domain_sigs);
								ldns_pkt_free(keypkt);
								return trusted_keys;
							}
						}
					}
	  
					/* Only push our trusted key */
					ldns_rr_list_push_rr(trusted_keys,
									 ldns_rr_clone(cur_key));
				}
			}
		}

		ldns_rr_list_deep_free(domain_keys);
		ldns_rr_list_deep_free(domain_sigs);
		ldns_pkt_free(keypkt);

	} else {
		/* LDNS_STATUS_CRYPTO_NO_DNSKEY */
	}
    
	return trusted_keys;
}

ldns_rr_list *
ldns_validate_domain_dnskey(const ldns_resolver * res,
					   const ldns_rdf * domain,
					   const ldns_rr_list * keys)
{
	return ldns_validate_domain_dnskey_time(
			res, domain, keys, ldns_time(NULL));
}

ldns_rr_list *
ldns_validate_domain_ds_time(
		const ldns_resolver *res, 
		const ldns_rdf * domain,
		const ldns_rr_list * keys,
		time_t check_time)
{
	ldns_pkt * dspkt;
	uint16_t key_i;
	ldns_rr_list * rrset = NULL;
	ldns_rr_list * sigs = NULL;
	ldns_rr_list * trusted_keys = NULL;

	/* Fetch DS for the domain */
	dspkt = ldns_resolver_query(res, domain,
		LDNS_RR_TYPE_DS, LDNS_RR_CLASS_IN, LDNS_RD);
	if (dspkt) {
		rrset = ldns_pkt_rr_list_by_type(dspkt,
								   LDNS_RR_TYPE_DS,
								   LDNS_SECTION_ANSWER);
		sigs = ldns_pkt_rr_list_by_type(dspkt,
								  LDNS_RR_TYPE_RRSIG,
								  LDNS_SECTION_ANSWER);

		/* Validate sigs */
		if (ldns_verify_time(rrset, sigs, keys, check_time, NULL)
			       	== LDNS_STATUS_OK) {
			trusted_keys = ldns_rr_list_new();
			for (key_i=0; key_i<ldns_rr_list_rr_count(rrset); key_i++) {
				ldns_rr_list_push_rr(trusted_keys,
								 ldns_rr_clone(ldns_rr_list_rr(rrset,
														 key_i)
											)
								 );
			}
		}

		ldns_rr_list_deep_free(rrset);
		ldns_rr_list_deep_free(sigs);
		ldns_pkt_free(dspkt);

	} else {
		/* LDNS_STATUS_CRYPTO_NO_DS */
	}

	return trusted_keys;
}

ldns_rr_list *
ldns_validate_domain_ds(const ldns_resolver *res,
				    const ldns_rdf * domain,
				    const ldns_rr_list * keys)
{
	return ldns_validate_domain_ds_time(res, domain, keys, ldns_time(NULL));
}

ldns_status
ldns_verify_trusted_time(
		ldns_resolver *res, 
		ldns_rr_list *rrset, 
		ldns_rr_list * rrsigs, 
		time_t check_time,
		ldns_rr_list * validating_keys
		)
{
	uint16_t sig_i; uint16_t key_i;
	ldns_rr * cur_sig; ldns_rr * cur_key;
	ldns_rr_list * trusted_keys = NULL;
	ldns_status result = LDNS_STATUS_ERR;

	if (!res || !rrset || !rrsigs) {
		return LDNS_STATUS_ERR;
	}

	if (ldns_rr_list_rr_count(rrset) < 1) {
		return LDNS_STATUS_ERR;
	}

	if (ldns_rr_list_rr_count(rrsigs) < 1) {
		return LDNS_STATUS_CRYPTO_NO_RRSIG;
	}
  
	/* Look at each sig */
	for (sig_i=0; sig_i < ldns_rr_list_rr_count(rrsigs); sig_i++) {

		cur_sig = ldns_rr_list_rr(rrsigs, sig_i);
		/* Get a valid signer key and validate the sig */
		if ((trusted_keys = ldns_fetch_valid_domain_keys_time(
					res, 
					ldns_rr_rrsig_signame(cur_sig), 
					ldns_resolver_dnssec_anchors(res), 
					check_time,
					&result))) {

			for (key_i = 0;
				key_i < ldns_rr_list_rr_count(trusted_keys);
				key_i++) {
				cur_key = ldns_rr_list_rr(trusted_keys, key_i);

				if ((result = ldns_verify_rrsig_time(rrset,
								cur_sig, 
								cur_key,
								check_time))
				    == LDNS_STATUS_OK) {
					if (validating_keys) {
						ldns_rr_list_push_rr(validating_keys,
										 ldns_rr_clone(cur_key));
					}
					ldns_rr_list_deep_free(trusted_keys);
					return LDNS_STATUS_OK;
				} 
			}
		}
	}

	ldns_rr_list_deep_free(trusted_keys);
	return result;
}

ldns_status
ldns_verify_trusted(
		ldns_resolver *res,
		ldns_rr_list *rrset, 
		ldns_rr_list * rrsigs, 
		ldns_rr_list * validating_keys)
{
	return ldns_verify_trusted_time(
			res, rrset, rrsigs, ldns_time(NULL), validating_keys);
}


ldns_status
ldns_dnssec_verify_denial(ldns_rr *rr,
                          ldns_rr_list *nsecs,
                          ldns_rr_list *rrsigs)
{
	ldns_rdf *rr_name;
	ldns_rdf *wildcard_name;
	ldns_rdf *chopped_dname;
	ldns_rr *cur_nsec;
	size_t i;
	ldns_status result;
	/* needed for wildcard check on exact match */
	ldns_rr *rrsig;
	bool name_covered = false;
	bool type_covered = false;
	bool wildcard_covered = false;
	bool wildcard_type_covered = false;

	wildcard_name = ldns_dname_new_frm_str("*");
	rr_name = ldns_rr_owner(rr);
	chopped_dname = ldns_dname_left_chop(rr_name);
	result = ldns_dname_cat(wildcard_name, chopped_dname);
	ldns_rdf_deep_free(chopped_dname);
	if (result != LDNS_STATUS_OK) {
		return result;
	}
	
	for  (i = 0; i < ldns_rr_list_rr_count(nsecs); i++) {
		cur_nsec = ldns_rr_list_rr(nsecs, i);
		if (ldns_dname_compare(rr_name, ldns_rr_owner(cur_nsec)) == 0) {
			/* see section 5.4 of RFC4035, if the label count of the NSEC's
			   RRSIG is equal, then it is proven that wildcard expansion 
			   could not have been used to match the request */
			rrsig = ldns_dnssec_get_rrsig_for_name_and_type(
					  ldns_rr_owner(cur_nsec),
					  ldns_rr_get_type(cur_nsec),
					  rrsigs);
			if (rrsig && ldns_rdf2native_int8(ldns_rr_rrsig_labels(rrsig))
			    == ldns_dname_label_count(rr_name)) {
				wildcard_covered = true;
			}
			
			if (ldns_nsec_bitmap_covers_type(ldns_nsec_get_bitmap(cur_nsec),
									   ldns_rr_get_type(rr))) {
				type_covered = true;
			}
		}
		if (ldns_nsec_covers_name(cur_nsec, rr_name)) {
			name_covered = true;
		}
		
		if (ldns_dname_compare(wildcard_name,
						   ldns_rr_owner(cur_nsec)) == 0) {
			if (ldns_nsec_bitmap_covers_type(ldns_nsec_get_bitmap(cur_nsec),
									   ldns_rr_get_type(rr))) {
				wildcard_type_covered = true;
			}
		}
		
		if (ldns_nsec_covers_name(cur_nsec, wildcard_name)) {
			wildcard_covered = true;
		}
		
	}
	
	ldns_rdf_deep_free(wildcard_name);
	
	if (type_covered || !name_covered) {
		return LDNS_STATUS_DNSSEC_NSEC_RR_NOT_COVERED;
	}
	
	if (wildcard_type_covered || !wildcard_covered) {
		return LDNS_STATUS_DNSSEC_NSEC_WILDCARD_NOT_COVERED;
	}

	return LDNS_STATUS_OK;
}

ldns_status
ldns_dnssec_verify_denial_nsec3_match( ldns_rr *rr
				     , ldns_rr_list *nsecs
				     , ATTR_UNUSED(ldns_rr_list *rrsigs)
				     , ldns_pkt_rcode packet_rcode
				     , ldns_rr_type packet_qtype
				     , bool packet_nodata
				     , ldns_rr **match
				     )
{
	ldns_rdf *closest_encloser;
	ldns_rdf *wildcard;
	ldns_rdf *hashed_wildcard_name;
	bool wildcard_covered = false;
	ldns_rdf *zone_name;
	ldns_rdf *hashed_name;
	/* self assignment to suppress uninitialized warning */
	ldns_rdf *next_closer = next_closer;
	ldns_rdf *hashed_next_closer;
	size_t i;
	ldns_status result = LDNS_STATUS_DNSSEC_NSEC_RR_NOT_COVERED;

	if (match) {
		*match = NULL;
	}

	zone_name = ldns_dname_left_chop(ldns_rr_owner(ldns_rr_list_rr(nsecs,0)));

	/* section 8.4 */
	if (packet_rcode == LDNS_RCODE_NXDOMAIN) {
		closest_encloser = ldns_dnssec_nsec3_closest_encloser(
						   ldns_rr_owner(rr),
						   ldns_rr_get_type(rr),
						   nsecs);
                if(!closest_encloser) {
                        result = LDNS_STATUS_DNSSEC_NSEC_RR_NOT_COVERED;
                        goto done;
                }

		wildcard = ldns_dname_new_frm_str("*");
		(void) ldns_dname_cat(wildcard, closest_encloser);

		for (i = 0; i < ldns_rr_list_rr_count(nsecs); i++) {
			hashed_wildcard_name =
				ldns_nsec3_hash_name_frm_nsec3(ldns_rr_list_rr(nsecs, 0),
										 wildcard
										 );
			(void) ldns_dname_cat(hashed_wildcard_name, zone_name);

			if (ldns_nsec_covers_name(ldns_rr_list_rr(nsecs, i),
								 hashed_wildcard_name)) {
				wildcard_covered = true;
				if (match) {
					*match = ldns_rr_list_rr(nsecs, i);
				}
			}
			ldns_rdf_deep_free(hashed_wildcard_name);
		}

		if (! wildcard_covered) {
			result = LDNS_STATUS_DNSSEC_NSEC_WILDCARD_NOT_COVERED;
		} else {
			result = LDNS_STATUS_OK;
		}
		ldns_rdf_deep_free(closest_encloser);
		ldns_rdf_deep_free(wildcard);

	} else if (packet_nodata && packet_qtype != LDNS_RR_TYPE_DS) {
		/* section 8.5 */
		hashed_name = ldns_nsec3_hash_name_frm_nsec3(
		                   ldns_rr_list_rr(nsecs, 0),
		                   ldns_rr_owner(rr));
		(void) ldns_dname_cat(hashed_name, zone_name);
		for (i = 0; i < ldns_rr_list_rr_count(nsecs); i++) {
			if (ldns_dname_compare(hashed_name,
			         ldns_rr_owner(ldns_rr_list_rr(nsecs, i)))
			    == 0) {
				if (!ldns_nsec_bitmap_covers_type(
					    ldns_nsec3_bitmap(ldns_rr_list_rr(nsecs, i)),
					    packet_qtype)
				    &&
				    !ldns_nsec_bitmap_covers_type(
					    ldns_nsec3_bitmap(ldns_rr_list_rr(nsecs, i)),
					    LDNS_RR_TYPE_CNAME)) {
					result = LDNS_STATUS_OK;
					if (match) {
						*match = ldns_rr_list_rr(nsecs, i);
					}
					goto done;
				}
			}
		}
		result = LDNS_STATUS_DNSSEC_NSEC_RR_NOT_COVERED;
		/* wildcard no data? section 8.7 */
		closest_encloser = ldns_dnssec_nsec3_closest_encloser(
				   ldns_rr_owner(rr),
				   ldns_rr_get_type(rr),
				   nsecs);
		if(!closest_encloser) {
			result = LDNS_STATUS_NSEC3_ERR;
			goto done;
		}
		wildcard = ldns_dname_new_frm_str("*");
		(void) ldns_dname_cat(wildcard, closest_encloser);
		for (i = 0; i < ldns_rr_list_rr_count(nsecs); i++) {
			hashed_wildcard_name =
				ldns_nsec3_hash_name_frm_nsec3(ldns_rr_list_rr(nsecs, 0),
					 wildcard);
			(void) ldns_dname_cat(hashed_wildcard_name, zone_name);

			if (ldns_dname_compare(hashed_wildcard_name,
			         ldns_rr_owner(ldns_rr_list_rr(nsecs, i)))
			    == 0) {
				if (!ldns_nsec_bitmap_covers_type(
					    ldns_nsec3_bitmap(ldns_rr_list_rr(nsecs, i)),
					    packet_qtype)
				    &&
				    !ldns_nsec_bitmap_covers_type(
					    ldns_nsec3_bitmap(ldns_rr_list_rr(nsecs, i)),
					    LDNS_RR_TYPE_CNAME)) {
					result = LDNS_STATUS_OK;
					if (match) {
						*match = ldns_rr_list_rr(nsecs, i);
					}
				}
			}
			ldns_rdf_deep_free(hashed_wildcard_name);
			if (result == LDNS_STATUS_OK) {
				break;
			}
		}
		ldns_rdf_deep_free(closest_encloser);
		ldns_rdf_deep_free(wildcard);
	} else if (packet_nodata && packet_qtype == LDNS_RR_TYPE_DS) {
		/* section 8.6 */
		/* note: up to XXX this is the same as for 8.5 */
		hashed_name = ldns_nsec3_hash_name_frm_nsec3(ldns_rr_list_rr(nsecs,
														 0),
											ldns_rr_owner(rr)
											);
		(void) ldns_dname_cat(hashed_name, zone_name);
		for (i = 0; i < ldns_rr_list_rr_count(nsecs); i++) {
			if (ldns_dname_compare(hashed_name,
							   ldns_rr_owner(ldns_rr_list_rr(nsecs,
													   i)))
			    == 0) {
				if (!ldns_nsec_bitmap_covers_type(
					    ldns_nsec3_bitmap(ldns_rr_list_rr(nsecs, i)),
					    LDNS_RR_TYPE_DS)
				    && 
				    !ldns_nsec_bitmap_covers_type(
					    ldns_nsec3_bitmap(ldns_rr_list_rr(nsecs, i)),
					    LDNS_RR_TYPE_CNAME)) {
					result = LDNS_STATUS_OK;
					if (match) {
						*match = ldns_rr_list_rr(nsecs, i);
					}
					goto done;
				}
			}
		}

		/* XXX see note above */
		result = LDNS_STATUS_DNSSEC_NSEC_RR_NOT_COVERED;

		closest_encloser = ldns_dnssec_nsec3_closest_encloser(
				   ldns_rr_owner(rr),
				   ldns_rr_get_type(rr),
				   nsecs);
		if(!closest_encloser) {
			result = LDNS_STATUS_NSEC3_ERR;
			goto done;
		}
		/* Now check if we have a Opt-Out NSEC3 that covers the "next closer"*/

		if (ldns_dname_label_count(closest_encloser) + 1
		    >= ldns_dname_label_count(ldns_rr_owner(rr))) {
			
			/* Query name *is* the "next closer". */
			hashed_next_closer = hashed_name;
		} else {

			/* "next closer" has less labels than the query name.
			 * Create the name and hash it.
			 */
			next_closer = ldns_dname_clone_from(
					ldns_rr_owner(rr),
					ldns_dname_label_count(ldns_rr_owner(rr))
					- (ldns_dname_label_count(closest_encloser) + 1)
					);
			hashed_next_closer = ldns_nsec3_hash_name_frm_nsec3(
					ldns_rr_list_rr(nsecs, 0),
					next_closer
					);
			(void) ldns_dname_cat(hashed_next_closer, zone_name);
		}
		/* Find the NSEC3 that covers the "next closer" */
		for (i = 0; i < ldns_rr_list_rr_count(nsecs); i++) {
			if (ldns_nsec_covers_name(ldns_rr_list_rr(nsecs, i),
			                          hashed_next_closer) && 
				ldns_nsec3_optout(ldns_rr_list_rr(nsecs, i))) {

				result = LDNS_STATUS_OK;
				if (match) {
					*match = ldns_rr_list_rr(nsecs, i);
				}
				break;
			}
		}
		if (ldns_dname_label_count(closest_encloser) + 1
		    < ldns_dname_label_count(ldns_rr_owner(rr))) {

			/* "next closer" has less labels than the query name.
			 * Dispose of the temporary variables that held that name.
			 */
			ldns_rdf_deep_free(hashed_next_closer);
			ldns_rdf_deep_free(next_closer);
		}
		ldns_rdf_deep_free(closest_encloser);
	}

 done:
	ldns_rdf_deep_free(zone_name);
	return result;
}

ldns_status
ldns_dnssec_verify_denial_nsec3(ldns_rr *rr,
						  ldns_rr_list *nsecs,
						  ldns_rr_list *rrsigs,
						  ldns_pkt_rcode packet_rcode,
						  ldns_rr_type packet_qtype,
						  bool packet_nodata)
{
	return ldns_dnssec_verify_denial_nsec3_match(
				rr, nsecs, rrsigs, packet_rcode,
				packet_qtype, packet_nodata, NULL
	       );
}

#ifdef USE_GOST
EVP_PKEY*
ldns_gost2pkey_raw(const unsigned char* key, size_t keylen)
{
	/* prefix header for X509 encoding */
	uint8_t asn[37] = { 0x30, 0x63, 0x30, 0x1c, 0x06, 0x06, 0x2a, 0x85, 
		0x03, 0x02, 0x02, 0x13, 0x30, 0x12, 0x06, 0x07, 0x2a, 0x85, 
		0x03, 0x02, 0x02, 0x23, 0x01, 0x06, 0x07, 0x2a, 0x85, 0x03, 
		0x02, 0x02, 0x1e, 0x01, 0x03, 0x43, 0x00, 0x04, 0x40};
	unsigned char encoded[37+64];
	const unsigned char* pp;
	if(keylen != 64) {
		/* key wrong size */
		return NULL;
	}

	/* create evp_key */
	memmove(encoded, asn, 37);
	memmove(encoded+37, key, 64);
	pp = (unsigned char*)&encoded[0];

	return d2i_PUBKEY(NULL, &pp, (int)sizeof(encoded));
}

static ldns_status
ldns_verify_rrsig_gost_raw(const unsigned char* sig, size_t siglen, 
	const ldns_buffer* rrset, const unsigned char* key, size_t keylen)
{
	EVP_PKEY *evp_key;
	ldns_status result;

	(void) ldns_key_EVP_load_gost_id();
	evp_key = ldns_gost2pkey_raw(key, keylen);
	if(!evp_key) {
		/* could not convert key */
		return LDNS_STATUS_CRYPTO_BOGUS;
	}

	/* verify signature */
	result = ldns_verify_rrsig_evp_raw(sig, siglen, rrset, 
		evp_key, EVP_get_digestbyname("md_gost94"));
	EVP_PKEY_free(evp_key);

	return result;
}
#endif

#ifdef USE_ED25519
EVP_PKEY*
ldns_ed255192pkey_raw(const unsigned char* key, size_t keylen)
{
        const unsigned char* pp = key; /* pp gets modified by o2i() */
        EVP_PKEY *evp_key;
        EC_KEY *ec;
	if(keylen != 32)
		return NULL; /* wrong length */
        ec = EC_KEY_new_by_curve_name(NID_X25519);
	if(!ec) return NULL;
        if(!o2i_ECPublicKey(&ec, &pp, (int)keylen)) {
                EC_KEY_free(ec);
                return NULL;
	}
        evp_key = EVP_PKEY_new();
        if(!evp_key) {
                EC_KEY_free(ec);
                return NULL;
        }
        if (!EVP_PKEY_assign_EC_KEY(evp_key, ec)) {
		EVP_PKEY_free(evp_key);
		EC_KEY_free(ec);
		return NULL;
	}
        return evp_key;
}

static ldns_status
ldns_verify_rrsig_ed25519_raw(unsigned char* sig, size_t siglen,
	ldns_buffer* rrset, unsigned char* key, size_t keylen)
{
        EVP_PKEY *evp_key;
        ldns_status result;

        evp_key = ldns_ed255192pkey_raw(key, keylen);
        if(!evp_key) {
		/* could not convert key */
		return LDNS_STATUS_CRYPTO_BOGUS;
        }
	result = ldns_verify_rrsig_evp_raw(sig, siglen, rrset, evp_key,
		EVP_sha512());
	EVP_PKEY_free(evp_key);
	return result;
}
#endif /* USE_ED25519 */

#ifdef USE_ED448
EVP_PKEY*
ldns_ed4482pkey_raw(const unsigned char* key, size_t keylen)
{
        const unsigned char* pp = key; /* pp gets modified by o2i() */
        EVP_PKEY *evp_key;
        EC_KEY *ec;
	if(keylen != 57)
		return NULL; /* wrong length */
        ec = EC_KEY_new_by_curve_name(NID_X448);
	if(!ec) return NULL;
        if(!o2i_ECPublicKey(&ec, &pp, (int)keylen)) {
                EC_KEY_free(ec);
                return NULL;
	}
        evp_key = EVP_PKEY_new();
        if(!evp_key) {
                EC_KEY_free(ec);
                return NULL;
        }
        if (!EVP_PKEY_assign_EC_KEY(evp_key, ec)) {
		EVP_PKEY_free(evp_key);
		EC_KEY_free(ec);
		return NULL;
	}
        return evp_key;
}

static ldns_status
ldns_verify_rrsig_ed448_raw(unsigned char* sig, size_t siglen,
	ldns_buffer* rrset, unsigned char* key, size_t keylen)
{
        EVP_PKEY *evp_key;
        ldns_status result;

        evp_key = ldns_ed4482pkey_raw(key, keylen);
        if(!evp_key) {
		/* could not convert key */
		return LDNS_STATUS_CRYPTO_BOGUS;
        }
	result = ldns_verify_rrsig_evp_raw(sig, siglen, rrset, evp_key,
		EVP_sha512());
	EVP_PKEY_free(evp_key);
	return result;
}
#endif /* USE_ED448 */

#ifdef USE_ECDSA
EVP_PKEY*
ldns_ecdsa2pkey_raw(const unsigned char* key, size_t keylen, uint8_t algo)
{
	unsigned char buf[256+2]; /* sufficient for 2*384/8+1 */
        const unsigned char* pp = buf;
        EVP_PKEY *evp_key;
        EC_KEY *ec;
	/* check length, which uncompressed must be 2 bignums */
        if(algo == LDNS_ECDSAP256SHA256) {
		if(keylen != 2*256/8) return NULL;
                ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
        } else if(algo == LDNS_ECDSAP384SHA384) {
		if(keylen != 2*384/8) return NULL;
                ec = EC_KEY_new_by_curve_name(NID_secp384r1);
        } else    ec = NULL;
        if(!ec) return NULL;
	if(keylen+1 > sizeof(buf))
		return NULL; /* sanity check */
	/* prepend the 0x02 (from docs) (or actually 0x04 from implementation
	 * of openssl) for uncompressed data */
	buf[0] = POINT_CONVERSION_UNCOMPRESSED;
	memmove(buf+1, key, keylen);
        if(!o2i_ECPublicKey(&ec, &pp, (int)keylen+1)) {
                EC_KEY_free(ec);
                return NULL;
        }
        evp_key = EVP_PKEY_new();
        if(!evp_key) {
                EC_KEY_free(ec);
                return NULL;
        }
        if (!EVP_PKEY_assign_EC_KEY(evp_key, ec)) {
		EVP_PKEY_free(evp_key);
		EC_KEY_free(ec);
		return NULL;
	}
        return evp_key;
}

static ldns_status
ldns_verify_rrsig_ecdsa_raw(unsigned char* sig, size_t siglen, 
	ldns_buffer* rrset, unsigned char* key, size_t keylen, uint8_t algo)
{
        EVP_PKEY *evp_key;
        ldns_status result;
        const EVP_MD *d;

        evp_key = ldns_ecdsa2pkey_raw(key, keylen, algo);
        if(!evp_key) {
		/* could not convert key */
		return LDNS_STATUS_CRYPTO_BOGUS;
        }
        if(algo == LDNS_ECDSAP256SHA256)
                d = EVP_sha256();
        else    d = EVP_sha384(); /* LDNS_ECDSAP384SHA384 */
	result = ldns_verify_rrsig_evp_raw(sig, siglen, rrset, evp_key, d);
	EVP_PKEY_free(evp_key);
	return result;
}
#endif

ldns_status
ldns_verify_rrsig_buffers(ldns_buffer *rawsig_buf, ldns_buffer *verify_buf, 
					 ldns_buffer *key_buf, uint8_t algo)
{
	return ldns_verify_rrsig_buffers_raw(
			 (unsigned char*)ldns_buffer_begin(rawsig_buf),
			 ldns_buffer_position(rawsig_buf),
			 verify_buf,
			 (unsigned char*)ldns_buffer_begin(key_buf), 
			 ldns_buffer_position(key_buf), algo);
}

ldns_status
ldns_verify_rrsig_buffers_raw(unsigned char* sig, size_t siglen,
						ldns_buffer *verify_buf, unsigned char* key, size_t keylen, 
						uint8_t algo)
{
	/* check for right key */
	switch(algo) {
#ifdef USE_DSA
	case LDNS_DSA:
	case LDNS_DSA_NSEC3:
		return ldns_verify_rrsig_dsa_raw(sig,
								   siglen,
								   verify_buf,
								   key,
								   keylen);
		break;
#endif
	case LDNS_RSASHA1:
	case LDNS_RSASHA1_NSEC3:
		return ldns_verify_rrsig_rsasha1_raw(sig,
									  siglen,
									  verify_buf,
									  key,
									  keylen);
		break;
#ifdef USE_SHA2
	case LDNS_RSASHA256:
		return ldns_verify_rrsig_rsasha256_raw(sig,
									    siglen,
									    verify_buf,
									    key,
									    keylen);
		break;
	case LDNS_RSASHA512:
		return ldns_verify_rrsig_rsasha512_raw(sig,
									    siglen,
									    verify_buf,
									    key,
									    keylen);
		break;
#endif
#ifdef USE_GOST
	case LDNS_ECC_GOST:
		return ldns_verify_rrsig_gost_raw(sig, siglen, verify_buf,
			key, keylen);
		break;
#endif
#ifdef USE_ECDSA
        case LDNS_ECDSAP256SHA256:
        case LDNS_ECDSAP384SHA384:
		return ldns_verify_rrsig_ecdsa_raw(sig, siglen, verify_buf,
			key, keylen, algo);
		break;
#endif
#ifdef USE_ED25519
	case LDNS_ED25519:
		return ldns_verify_rrsig_ed25519_raw(sig, siglen, verify_buf,
			key, keylen);
		break;
#endif
#ifdef USE_ED448
	case LDNS_ED448:
		return ldns_verify_rrsig_ed448_raw(sig, siglen, verify_buf,
			key, keylen);
		break;
#endif
	case LDNS_RSAMD5:
		return ldns_verify_rrsig_rsamd5_raw(sig,
									 siglen,
									 verify_buf,
									 key,
									 keylen);
		break;
	default:
		/* do you know this alg?! */
		return LDNS_STATUS_CRYPTO_UNKNOWN_ALGO;
	}
}


/**
 * Reset the ttl in the rrset with the orig_ttl from the sig 
 * and update owner name if it was wildcard 
 * Also canonicalizes the rrset.
 * @param rrset: rrset to modify
 * @param sig: signature to take TTL and wildcard values from
 */
static void
ldns_rrset_use_signature_ttl(ldns_rr_list* rrset_clone, const ldns_rr* rrsig)
{
	uint32_t orig_ttl;
	uint16_t i;
	uint8_t label_count;
	ldns_rdf *wildcard_name;
	ldns_rdf *wildcard_chopped;
	ldns_rdf *wildcard_chopped_tmp;
	
	if ((rrsig == NULL) || ldns_rr_rd_count(rrsig) < 4) {
		return;
	}

	orig_ttl = ldns_rdf2native_int32( ldns_rr_rdf(rrsig, 3));
	label_count = ldns_rdf2native_int8(ldns_rr_rdf(rrsig, 2));

	for(i = 0; i < ldns_rr_list_rr_count(rrset_clone); i++) {
		if (label_count < 
		    ldns_dname_label_count(
			   ldns_rr_owner(ldns_rr_list_rr(rrset_clone, i)))) {
			(void) ldns_str2rdf_dname(&wildcard_name, "*");
			wildcard_chopped = ldns_rdf_clone(ldns_rr_owner(
				ldns_rr_list_rr(rrset_clone, i)));
			while (label_count < ldns_dname_label_count(wildcard_chopped)) {
				wildcard_chopped_tmp = ldns_dname_left_chop(
					wildcard_chopped);
				ldns_rdf_deep_free(wildcard_chopped);
				wildcard_chopped = wildcard_chopped_tmp;
			}
			(void) ldns_dname_cat(wildcard_name, wildcard_chopped);
			ldns_rdf_deep_free(wildcard_chopped);
			ldns_rdf_deep_free(ldns_rr_owner(ldns_rr_list_rr(
				rrset_clone, i)));
			ldns_rr_set_owner(ldns_rr_list_rr(rrset_clone, i), 
				wildcard_name);
		}
		ldns_rr_set_ttl(ldns_rr_list_rr(rrset_clone, i), orig_ttl);
		/* convert to lowercase */
		ldns_rr2canonical(ldns_rr_list_rr(rrset_clone, i));
	}
}

/**
 * Make raw signature buffer out of rrsig
 * @param rawsig_buf: raw signature buffer for result
 * @param rrsig: signature to convert
 * @return OK or more specific error.
 */
static ldns_status
ldns_rrsig2rawsig_buffer(ldns_buffer* rawsig_buf, const ldns_rr* rrsig)
{
	uint8_t sig_algo;
       
	if (rrsig == NULL) {
		return LDNS_STATUS_CRYPTO_NO_RRSIG;
	}
	if (ldns_rr_rdf(rrsig, 1) == NULL) {
		return LDNS_STATUS_MISSING_RDATA_FIELDS_RRSIG;
	}
	sig_algo = ldns_rdf2native_int8(ldns_rr_rdf(rrsig, 1));
	/* check for known and implemented algo's now (otherwise 
	 * the function could return a wrong error
	 */
	/* create a buffer with signature rdata */
	/* for some algorithms we need other data than for others... */
	/* (the DSA API wants DER encoding for instance) */

	switch(sig_algo) {
	case LDNS_RSAMD5:
	case LDNS_RSASHA1:
	case LDNS_RSASHA1_NSEC3:
#ifdef USE_SHA2
	case LDNS_RSASHA256:
	case LDNS_RSASHA512:
#endif
#ifdef USE_GOST
	case LDNS_ECC_GOST:
#endif
		if (ldns_rr_rdf(rrsig, 8) == NULL) {
			return LDNS_STATUS_MISSING_RDATA_FIELDS_RRSIG;
		}
		if (ldns_rdf2buffer_wire(rawsig_buf, ldns_rr_rdf(rrsig, 8))
			       	!= LDNS_STATUS_OK) {
			return LDNS_STATUS_MEM_ERR;
		}
		break;
#ifdef USE_DSA
	case LDNS_DSA:
	case LDNS_DSA_NSEC3:
		/* EVP takes rfc2459 format, which is a tad longer than dns format */
		if (ldns_rr_rdf(rrsig, 8) == NULL) {
			return LDNS_STATUS_MISSING_RDATA_FIELDS_RRSIG;
		}
		if (ldns_convert_dsa_rrsig_rdf2asn1(
					rawsig_buf, ldns_rr_rdf(rrsig, 8)) 
				!= LDNS_STATUS_OK) {
			/*
			  if (ldns_rdf2buffer_wire(rawsig_buf,
			  ldns_rr_rdf(rrsig, 8)) != LDNS_STATUS_OK) {
			*/
			return LDNS_STATUS_MEM_ERR;
		}
		break;
#endif
#ifdef USE_ECDSA
        case LDNS_ECDSAP256SHA256:
        case LDNS_ECDSAP384SHA384:
                /* EVP produces an ASN prefix on the signature, which is
                 * not used in the DNS */
		if (ldns_rr_rdf(rrsig, 8) == NULL) {
			return LDNS_STATUS_MISSING_RDATA_FIELDS_RRSIG;
		}
		if (ldns_convert_ecdsa_rrsig_rdf2asn1(
					rawsig_buf, ldns_rr_rdf(rrsig, 8))
				!= LDNS_STATUS_OK) {
			return LDNS_STATUS_MEM_ERR;
                }
                break;
#endif
#ifdef USE_ED25519
	case LDNS_ED25519:
                /* EVP produces an ASN prefix on the signature, which is
                 * not used in the DNS */
		if (ldns_rr_rdf(rrsig, 8) == NULL) {
			return LDNS_STATUS_MISSING_RDATA_FIELDS_RRSIG;
		}
		if (ldns_convert_ed25519_rrsig_rdf2asn1(
			rawsig_buf, ldns_rr_rdf(rrsig, 8)) != LDNS_STATUS_OK) {
			return LDNS_STATUS_MEM_ERR;
                }
		break;
#endif
#ifdef USE_ED448
	case LDNS_ED448:
                /* EVP produces an ASN prefix on the signature, which is
                 * not used in the DNS */
		if (ldns_rr_rdf(rrsig, 8) == NULL) {
			return LDNS_STATUS_MISSING_RDATA_FIELDS_RRSIG;
		}
		if (ldns_convert_ed448_rrsig_rdf2asn1(
			rawsig_buf, ldns_rr_rdf(rrsig, 8)) != LDNS_STATUS_OK) {
			return LDNS_STATUS_MEM_ERR;
                }
		break;
#endif
	case LDNS_DH:
	case LDNS_ECC:
	case LDNS_INDIRECT:
		return LDNS_STATUS_CRYPTO_ALGO_NOT_IMPL;
	default:
		return LDNS_STATUS_CRYPTO_UNKNOWN_ALGO;
	}
	return LDNS_STATUS_OK;
}

/**
 * Check RRSIG timestamps against the given 'now' time.
 * @param rrsig: signature to check.
 * @param now: the current time in seconds epoch.
 * @return status code LDNS_STATUS_OK if all is fine.
 */
static ldns_status
ldns_rrsig_check_timestamps(const ldns_rr* rrsig, time_t now)
{
	int32_t inception, expiration;
	
	/* check the signature time stamps */
	inception = (int32_t)ldns_rdf2native_time_t(
		ldns_rr_rrsig_inception(rrsig));
	expiration = (int32_t)ldns_rdf2native_time_t(
		ldns_rr_rrsig_expiration(rrsig));

	if (expiration - inception < 0) {
		/* bad sig, expiration before inception?? Tsssg */
		return LDNS_STATUS_CRYPTO_EXPIRATION_BEFORE_INCEPTION;
	}
	if (((int32_t) now) - inception < 0) {
		/* bad sig, inception date has not yet come to pass */
		return LDNS_STATUS_CRYPTO_SIG_NOT_INCEPTED;
	}
	if (expiration - ((int32_t) now) < 0) {
		/* bad sig, expiration date has passed */
		return LDNS_STATUS_CRYPTO_SIG_EXPIRED;
	}
	return LDNS_STATUS_OK;
}

/**
 * Prepare for verification.
 * @param rawsig_buf: raw signature buffer made ready.
 * @param verify_buf: data for verification buffer made ready.
 * @param rrset_clone: made ready.
 * @param rrsig: signature to prepare for.
 * @return LDNS_STATUS_OK is all went well. Otherwise specific error.
 */
static ldns_status
ldns_prepare_for_verify(ldns_buffer* rawsig_buf, ldns_buffer* verify_buf, 
	ldns_rr_list* rrset_clone, const ldns_rr* rrsig)
{
	ldns_status result;

	/* canonicalize the sig */
	ldns_dname2canonical(ldns_rr_owner(rrsig));
	
	/* check if the typecovered is equal to the type checked */
	if (ldns_rdf2rr_type(ldns_rr_rrsig_typecovered(rrsig)) !=
	    ldns_rr_get_type(ldns_rr_list_rr(rrset_clone, 0)))
		return LDNS_STATUS_CRYPTO_TYPE_COVERED_ERR;
	
	/* create a buffer with b64 signature rdata */
	result = ldns_rrsig2rawsig_buffer(rawsig_buf, rrsig);
	if(result != LDNS_STATUS_OK)
		return result;

	/* use TTL from signature. Use wildcard names for wildcards */
	/* also canonicalizes rrset_clone */
	ldns_rrset_use_signature_ttl(rrset_clone, rrsig);

	/* sort the rrset in canonical order  */
	ldns_rr_list_sort(rrset_clone);

	/* put the signature rr (without the b64) to the verify_buf */
	if (ldns_rrsig2buffer_wire(verify_buf, rrsig) != LDNS_STATUS_OK)
		return LDNS_STATUS_MEM_ERR;

	/* add the rrset in verify_buf */
	if(ldns_rr_list2buffer_wire(verify_buf, rrset_clone) 
		!= LDNS_STATUS_OK)
		return LDNS_STATUS_MEM_ERR;

	return LDNS_STATUS_OK;
}

/**
 * Check if a key matches a signature.
 * Checks keytag, sigalgo and signature.
 * @param rawsig_buf: raw signature buffer for verify
 * @param verify_buf: raw data buffer for verify
 * @param rrsig: the rrsig
 * @param key: key to attempt.
 * @return LDNS_STATUS_OK if OK, else some specific error.
 */
static ldns_status
ldns_verify_test_sig_key(ldns_buffer* rawsig_buf, ldns_buffer* verify_buf, 
	const ldns_rr* rrsig, ldns_rr* key)
{
	uint8_t sig_algo;
       
	if (rrsig == NULL) {
		return LDNS_STATUS_CRYPTO_NO_RRSIG;
	}
	if (ldns_rr_rdf(rrsig, 1) == NULL) {
		return LDNS_STATUS_MISSING_RDATA_FIELDS_RRSIG;
	}
	sig_algo = ldns_rdf2native_int8(ldns_rr_rdf(rrsig, 1));

	/* before anything, check if the keytags match */
	if (ldns_calc_keytag(key)
	    ==
	    ldns_rdf2native_int16(ldns_rr_rrsig_keytag(rrsig))
	    ) {
		ldns_buffer* key_buf = ldns_buffer_new(LDNS_MAX_PACKETLEN);
		ldns_status result = LDNS_STATUS_ERR;

		/* put the key-data in a buffer, that's the third rdf, with
		 * the base64 encoded key data */
		if (ldns_rr_rdf(key, 3) == NULL) {
			ldns_buffer_free(key_buf);
			return LDNS_STATUS_MISSING_RDATA_FIELDS_KEY;
		}
		if (ldns_rdf2buffer_wire(key_buf, ldns_rr_rdf(key, 3))
			       	!= LDNS_STATUS_OK) {
			ldns_buffer_free(key_buf); 
			/* returning is bad might screw up
			   good keys later in the list
			   what to do? */
			return LDNS_STATUS_ERR;
		}

		if (ldns_rr_rdf(key, 2) == NULL) {
			result = LDNS_STATUS_MISSING_RDATA_FIELDS_KEY;
		}
		else if (sig_algo == ldns_rdf2native_int8(
					ldns_rr_rdf(key, 2))) {
			result = ldns_verify_rrsig_buffers(rawsig_buf, 
				verify_buf, key_buf, sig_algo);
		} else {
			/* No keys with the corresponding algorithm are found */
			result = LDNS_STATUS_CRYPTO_NO_MATCHING_KEYTAG_DNSKEY;
		}

		ldns_buffer_free(key_buf); 
		return result;
	}
	else {
		/* No keys with the corresponding keytag are found */
		return LDNS_STATUS_CRYPTO_NO_MATCHING_KEYTAG_DNSKEY;
	}
}

/* 
 * to verify:
 * - create the wire fmt of the b64 key rdata
 * - create the wire fmt of the sorted rrset
 * - create the wire fmt of the b64 sig rdata
 * - create the wire fmt of the sig without the b64 rdata
 * - cat the sig data (without b64 rdata) to the rrset
 * - verify the rrset+sig, with the b64 data and the b64 key data
 */
ldns_status
ldns_verify_rrsig_keylist_time(
		const ldns_rr_list *rrset,
		const ldns_rr *rrsig,
		const ldns_rr_list *keys, 
		time_t check_time,
		ldns_rr_list *good_keys)
{
	ldns_status result;
	ldns_rr_list *valid = ldns_rr_list_new();
	if (!valid)
		return LDNS_STATUS_MEM_ERR;

	result = ldns_verify_rrsig_keylist_notime(rrset, rrsig, keys, valid);
	if(result != LDNS_STATUS_OK) {
		ldns_rr_list_free(valid); 
		return result;
	}

	/* check timestamps last; its OK except time */
	result = ldns_rrsig_check_timestamps(rrsig, check_time);
	if(result != LDNS_STATUS_OK) {
		ldns_rr_list_free(valid); 
		return result;
	}

	ldns_rr_list_cat(good_keys, valid);
	ldns_rr_list_free(valid);
	return LDNS_STATUS_OK;
}

/* 
 * to verify:
 * - create the wire fmt of the b64 key rdata
 * - create the wire fmt of the sorted rrset
 * - create the wire fmt of the b64 sig rdata
 * - create the wire fmt of the sig without the b64 rdata
 * - cat the sig data (without b64 rdata) to the rrset
 * - verify the rrset+sig, with the b64 data and the b64 key data
 */
ldns_status
ldns_verify_rrsig_keylist(ldns_rr_list *rrset,
					 ldns_rr *rrsig,
					 const ldns_rr_list *keys, 
					 ldns_rr_list *good_keys)
{
	return ldns_verify_rrsig_keylist_time(
			rrset, rrsig, keys, ldns_time(NULL), good_keys);
}

ldns_status
ldns_verify_rrsig_keylist_notime(const ldns_rr_list *rrset,
					 const ldns_rr *rrsig,
					 const ldns_rr_list *keys, 
					 ldns_rr_list *good_keys)
{
	ldns_buffer *rawsig_buf;
	ldns_buffer *verify_buf;
	uint16_t i;
	ldns_status result, status;
	ldns_rr_list *rrset_clone;
	ldns_rr_list *validkeys;

	if (!rrset) {
		return LDNS_STATUS_ERR;
	}

	validkeys = ldns_rr_list_new();
	if (!validkeys) {
		return LDNS_STATUS_MEM_ERR;
	}
	
	/* clone the rrset so that we can fiddle with it */
	rrset_clone = ldns_rr_list_clone(rrset);

	/* create the buffers which will certainly hold the raw data */
	rawsig_buf = ldns_buffer_new(LDNS_MAX_PACKETLEN);
	verify_buf  = ldns_buffer_new(LDNS_MAX_PACKETLEN);

	result = ldns_prepare_for_verify(rawsig_buf, verify_buf, 
		rrset_clone, rrsig);
	if(result != LDNS_STATUS_OK) {
		ldns_buffer_free(verify_buf);
		ldns_buffer_free(rawsig_buf);
		ldns_rr_list_deep_free(rrset_clone);
		ldns_rr_list_free(validkeys);
		return result;
	}

	result = LDNS_STATUS_CRYPTO_NO_MATCHING_KEYTAG_DNSKEY;
	for(i = 0; i < ldns_rr_list_rr_count(keys); i++) {
		status = ldns_verify_test_sig_key(rawsig_buf, verify_buf, 
			rrsig, ldns_rr_list_rr(keys, i));
		if (status == LDNS_STATUS_OK) {
			/* one of the keys has matched, don't break
			 * here, instead put the 'winning' key in
			 * the validkey list and return the list 
			 * later */
			if (!ldns_rr_list_push_rr(validkeys, 
				ldns_rr_list_rr(keys,i))) {
				/* couldn't push the key?? */
				ldns_buffer_free(rawsig_buf);
				ldns_buffer_free(verify_buf);
				ldns_rr_list_deep_free(rrset_clone);
				ldns_rr_list_free(validkeys);
				return LDNS_STATUS_MEM_ERR;
			}

			result = status;
		}

		if (result == LDNS_STATUS_CRYPTO_NO_MATCHING_KEYTAG_DNSKEY) {
			result = status;
		}
	}

	/* no longer needed */
	ldns_rr_list_deep_free(rrset_clone);
	ldns_buffer_free(rawsig_buf);
	ldns_buffer_free(verify_buf);

	if (ldns_rr_list_rr_count(validkeys) == 0) {
		/* no keys were added, return last error */
		ldns_rr_list_free(validkeys); 
		return result;
	}

	/* do not check timestamps */

	ldns_rr_list_cat(good_keys, validkeys);
	ldns_rr_list_free(validkeys);
	return LDNS_STATUS_OK;
}

ldns_status
ldns_verify_rrsig_time(
		ldns_rr_list *rrset, 
		ldns_rr *rrsig, 
		ldns_rr *key, 
		time_t check_time)
{
	ldns_buffer *rawsig_buf;
	ldns_buffer *verify_buf;
	ldns_status result;
	ldns_rr_list *rrset_clone;

	if (!rrset) {
		return LDNS_STATUS_NO_DATA;
	}
	/* clone the rrset so that we can fiddle with it */
	rrset_clone = ldns_rr_list_clone(rrset);
	/* create the buffers which will certainly hold the raw data */
	rawsig_buf = ldns_buffer_new(LDNS_MAX_PACKETLEN);
	verify_buf  = ldns_buffer_new(LDNS_MAX_PACKETLEN);

	result = ldns_prepare_for_verify(rawsig_buf, verify_buf, 
		rrset_clone, rrsig);
	if(result != LDNS_STATUS_OK) {
		ldns_rr_list_deep_free(rrset_clone);
		ldns_buffer_free(rawsig_buf);
		ldns_buffer_free(verify_buf);
		return result;
	}
	result = ldns_verify_test_sig_key(rawsig_buf, verify_buf, 
		rrsig, key);
	/* no longer needed */
	ldns_rr_list_deep_free(rrset_clone);
	ldns_buffer_free(rawsig_buf);
	ldns_buffer_free(verify_buf);

	/* check timestamp last, apart from time its OK */
	if(result == LDNS_STATUS_OK)
		result = ldns_rrsig_check_timestamps(rrsig, check_time);

	return result;
}

ldns_status
ldns_verify_rrsig(ldns_rr_list *rrset, ldns_rr *rrsig, ldns_rr *key)
{
	return ldns_verify_rrsig_time(rrset, rrsig, key, ldns_time(NULL));
}


ldns_status
ldns_verify_rrsig_evp(ldns_buffer *sig,
				  ldns_buffer *rrset,
				  EVP_PKEY *key,
				  const EVP_MD *digest_type)
{
	return ldns_verify_rrsig_evp_raw(
			 (unsigned char*)ldns_buffer_begin(sig),
			 ldns_buffer_position(sig),
			 rrset,
			 key,
			 digest_type);
}

ldns_status
ldns_verify_rrsig_evp_raw(const unsigned char *sig, size_t siglen, 
					 const ldns_buffer *rrset, EVP_PKEY *key, const EVP_MD *digest_type)
{
	EVP_MD_CTX *ctx;
	int res;

#ifdef HAVE_EVP_MD_CTX_NEW
	ctx = EVP_MD_CTX_new();
#else
	ctx = (EVP_MD_CTX*)malloc(sizeof(*ctx));
	if(ctx) EVP_MD_CTX_init(ctx);
#endif
	if(!ctx)
		return LDNS_STATUS_MEM_ERR;
	
	EVP_VerifyInit(ctx, digest_type);
	EVP_VerifyUpdate(ctx,
				  ldns_buffer_begin(rrset),
				  ldns_buffer_position(rrset));
	res = EVP_VerifyFinal(ctx, sig, (unsigned int) siglen, key);
	
	EVP_MD_CTX_destroy(ctx);
	
	if (res == 1) {
		return LDNS_STATUS_OK;
	} else if (res == 0) {
		return LDNS_STATUS_CRYPTO_BOGUS;
	}
	/* TODO how to communicate internal SSL error?
	   let caller use ssl's get_error() */
	return LDNS_STATUS_SSL_ERR;
}

ldns_status
ldns_verify_rrsig_dsa(ldns_buffer *sig, ldns_buffer *rrset, ldns_buffer *key)
{
	return ldns_verify_rrsig_dsa_raw(
			 (unsigned char*) ldns_buffer_begin(sig),
			 ldns_buffer_position(sig),
			 rrset,
			 (unsigned char*) ldns_buffer_begin(key),
			 ldns_buffer_position(key));
}

ldns_status
ldns_verify_rrsig_rsasha1(ldns_buffer *sig, ldns_buffer *rrset, ldns_buffer *key)
{
	return ldns_verify_rrsig_rsasha1_raw(
			 (unsigned char*)ldns_buffer_begin(sig),
			 ldns_buffer_position(sig),
			 rrset,
			 (unsigned char*) ldns_buffer_begin(key),
			 ldns_buffer_position(key));
}

ldns_status
ldns_verify_rrsig_rsamd5(ldns_buffer *sig, ldns_buffer *rrset, ldns_buffer *key)
{
	return ldns_verify_rrsig_rsamd5_raw(
			 (unsigned char*)ldns_buffer_begin(sig),
			 ldns_buffer_position(sig),
			 rrset,
			 (unsigned char*) ldns_buffer_begin(key),
			 ldns_buffer_position(key));
}

ldns_status
ldns_verify_rrsig_dsa_raw(unsigned char* sig, size_t siglen,
					 ldns_buffer* rrset, unsigned char* key, size_t keylen)
{
#ifdef USE_DSA
	EVP_PKEY *evp_key;
	ldns_status result;

	evp_key = EVP_PKEY_new();
	if (EVP_PKEY_assign_DSA(evp_key, ldns_key_buf2dsa_raw(key, keylen))) {
		result = ldns_verify_rrsig_evp_raw(sig,
								siglen,
								rrset,
								evp_key,
# ifdef HAVE_EVP_DSS1
								EVP_dss1()
# else
								EVP_sha1()
# endif
								);
	} else {
		result = LDNS_STATUS_SSL_ERR;
	}
	EVP_PKEY_free(evp_key);
	return result;
#else
	(void)sig; (void)siglen; (void)rrset; (void)key; (void)keylen;
	return LDNS_STATUS_CRYPTO_ALGO_NOT_IMPL;
#endif
}

ldns_status
ldns_verify_rrsig_rsasha1_raw(unsigned char* sig, size_t siglen,
						ldns_buffer* rrset, unsigned char* key, size_t keylen)
{
	EVP_PKEY *evp_key;
	ldns_status result;

	evp_key = EVP_PKEY_new();
	if (EVP_PKEY_assign_RSA(evp_key, ldns_key_buf2rsa_raw(key, keylen))) {
		result = ldns_verify_rrsig_evp_raw(sig,
								siglen,
								rrset,
								evp_key,
								EVP_sha1());
	} else {
		result = LDNS_STATUS_SSL_ERR;
	}
	EVP_PKEY_free(evp_key);

	return result;
}

ldns_status
ldns_verify_rrsig_rsasha256_raw(unsigned char* sig,
						  size_t siglen,
						  ldns_buffer* rrset,
						  unsigned char* key,
						  size_t keylen)
{
#ifdef USE_SHA2
	EVP_PKEY *evp_key;
	ldns_status result;

	evp_key = EVP_PKEY_new();
	if (EVP_PKEY_assign_RSA(evp_key, ldns_key_buf2rsa_raw(key, keylen))) {
		result = ldns_verify_rrsig_evp_raw(sig,
								siglen,
								rrset,
								evp_key,
								EVP_sha256());
	} else {
		result = LDNS_STATUS_SSL_ERR;
	}
	EVP_PKEY_free(evp_key);

	return result;
#else
	/* touch these to prevent compiler warnings */
	(void) sig;
	(void) siglen;
	(void) rrset;
	(void) key;
	(void) keylen;
	return LDNS_STATUS_CRYPTO_UNKNOWN_ALGO;
#endif
}

ldns_status
ldns_verify_rrsig_rsasha512_raw(unsigned char* sig,
						  size_t siglen,
						  ldns_buffer* rrset,
						  unsigned char* key,
						  size_t keylen)
{
#ifdef USE_SHA2
	EVP_PKEY *evp_key;
	ldns_status result;

	evp_key = EVP_PKEY_new();
	if (EVP_PKEY_assign_RSA(evp_key, ldns_key_buf2rsa_raw(key, keylen))) {
		result = ldns_verify_rrsig_evp_raw(sig,
								siglen,
								rrset,
								evp_key,
								EVP_sha512());
	} else {
		result = LDNS_STATUS_SSL_ERR;
	}
	EVP_PKEY_free(evp_key);

	return result;
#else
	/* touch these to prevent compiler warnings */
	(void) sig;
	(void) siglen;
	(void) rrset;
	(void) key;
	(void) keylen;
	return LDNS_STATUS_CRYPTO_UNKNOWN_ALGO;
#endif
}


ldns_status
ldns_verify_rrsig_rsamd5_raw(unsigned char* sig,
					    size_t siglen,
					    ldns_buffer* rrset,
					    unsigned char* key,
					    size_t keylen)
{
	EVP_PKEY *evp_key;
	ldns_status result;

	evp_key = EVP_PKEY_new();
	if (EVP_PKEY_assign_RSA(evp_key, ldns_key_buf2rsa_raw(key, keylen))) {
		result = ldns_verify_rrsig_evp_raw(sig,
								siglen,
								rrset,
								evp_key,
								EVP_md5());
	} else {
		result = LDNS_STATUS_SSL_ERR;
	}
	EVP_PKEY_free(evp_key);

	return result;
}

#endif
