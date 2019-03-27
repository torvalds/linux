/** dnssec_verify */

#ifndef LDNS_DNSSEC_VERIFY_H
#define LDNS_DNSSEC_VERIFY_H

#define LDNS_DNSSEC_TRUST_TREE_MAX_PARENTS 10

#include <ldns/dnssec.h>
#include <ldns/host2str.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Chain structure that contains all DNSSEC data needed to
 * verify an rrset
 */
typedef struct ldns_dnssec_data_chain_struct ldns_dnssec_data_chain;
struct ldns_dnssec_data_chain_struct
{
	ldns_rr_list *rrset;
	ldns_rr_list *signatures;
	ldns_rr_type parent_type;
	ldns_dnssec_data_chain *parent;
	ldns_pkt_rcode packet_rcode;
	ldns_rr_type packet_qtype;
	bool packet_nodata;
};

/**
 * Creates a new dnssec_chain structure
 * \return ldns_dnssec_data_chain *
 */
ldns_dnssec_data_chain *ldns_dnssec_data_chain_new(void);

/**
 * Frees a dnssec_data_chain structure
 *
 * \param[in] *chain The chain to free
 */
void ldns_dnssec_data_chain_free(ldns_dnssec_data_chain *chain);

/**
 * Frees a dnssec_data_chain structure, and all data
 * contained therein
 *
 * \param[in] *chain The dnssec_data_chain to free
 */
void ldns_dnssec_data_chain_deep_free(ldns_dnssec_data_chain *chain);

/**
 * Prints the dnssec_data_chain to the given file stream
 * 
 * \param[in] *out The file stream to print to
 * \param[in] *chain The dnssec_data_chain to print
 */
void ldns_dnssec_data_chain_print(FILE *out, const ldns_dnssec_data_chain *chain);

/**
 * Prints the dnssec_data_chain to the given file stream
 * 
 * \param[in] *out The file stream to print to
 * \param[in] *fmt The format of the textual representation
 * \param[in] *chain The dnssec_data_chain to print
 */
void ldns_dnssec_data_chain_print_fmt(FILE *out, 
		const ldns_output_format *fmt,
		const ldns_dnssec_data_chain *chain);

/**
 * Build an ldns_dnssec_data_chain, which contains all
 * DNSSEC data that is needed to derive the trust tree later
 *
 * The data_set will be cloned
 *
 * \param[in] *res resolver structure for further needed queries
 * \param[in] qflags resolution flags
 * \param[in] *data_set The original rrset where the chain ends
 * \param[in] *pkt optional, can contain the original packet
 * (and hence the sigs and maybe the key)
 * \param[in] *orig_rr The original Resource Record
 *
 * \return the DNSSEC data chain
 */
ldns_dnssec_data_chain *ldns_dnssec_build_data_chain(ldns_resolver *res,
										   const uint16_t qflags,
										   const ldns_rr_list *data_set,
										   const ldns_pkt *pkt,
										   ldns_rr *orig_rr);

/**
 * Tree structure that contains the relation of DNSSEC data,
 * and their cryptographic status.
 *
 * This tree is derived from a data_chain, and can be used
 * to look whether there is a connection between an RRSET
 * and a trusted key. The tree only contains pointers to the
 * data_chain, and therefore one should *never* free() the
 * data_chain when there is still a trust tree derived from
 * that chain.
 *
 * Example tree:
 *     key   key    key
 *       \    |    /
 *        \   |   /
 *         \  |  /
 *            ds
 *            |
 *           key
 *            |
 *           key
 *            |
 *            rr
 *
 * For each signature there is a parent; if the parent
 * pointer is null, it couldn't be found and there was no
 * denial; otherwise is a tree which contains either a
 * DNSKEY, a DS, or a NSEC rr
 */
typedef struct ldns_dnssec_trust_tree_struct ldns_dnssec_trust_tree;
struct ldns_dnssec_trust_tree_struct
{
	ldns_rr *rr;
	/* the complete rrset this rr was in */
	ldns_rr_list *rrset;
	ldns_dnssec_trust_tree *parents[LDNS_DNSSEC_TRUST_TREE_MAX_PARENTS];
	ldns_status parent_status[LDNS_DNSSEC_TRUST_TREE_MAX_PARENTS];
	/** for debugging, add signatures too (you might want
	    those if they contain errors) */
	ldns_rr *parent_signature[LDNS_DNSSEC_TRUST_TREE_MAX_PARENTS];
	size_t parent_count;
};

/**
 * Creates a new (empty) dnssec_trust_tree structure
 *
 * \return ldns_dnssec_trust_tree *
 */
ldns_dnssec_trust_tree *ldns_dnssec_trust_tree_new(void);

/**
 * Frees the dnssec_trust_tree recursively
 *
 * There is no deep free; all data in the trust tree
 * consists of pointers to a data_chain
 *
 * \param[in] tree The tree to free
 */
void ldns_dnssec_trust_tree_free(ldns_dnssec_trust_tree *tree);

/**
 * returns the depth of the trust tree
 *
 * \param[in] tree tree to calculate the depth of
 * \return The depth of the tree
 */
size_t ldns_dnssec_trust_tree_depth(ldns_dnssec_trust_tree *tree);

/**
 * Prints the dnssec_trust_tree structure to the given file
 * stream.
 *
 * If a link status is not LDNS_STATUS_OK; the status and
 * relevant signatures are printed too
 *
 * \param[in] *out The file stream to print to
 * \param[in] tree The trust tree to print
 * \param[in] tabs Prepend each line with tabs*2 spaces
 * \param[in] extended If true, add little explanation lines to the output
 */
void ldns_dnssec_trust_tree_print(FILE *out,
	       	ldns_dnssec_trust_tree *tree,
		size_t tabs,
		bool extended);

/**
 * Prints the dnssec_trust_tree structure to the given file
 * stream.
 *
 * If a link status is not LDNS_STATUS_OK; the status and
 * relevant signatures are printed too
 *
 * \param[in] *out The file stream to print to
 * \param[in] *fmt The format of the textual representation
 * \param[in] tree The trust tree to print
 * \param[in] tabs Prepend each line with tabs*2 spaces
 * \param[in] extended If true, add little explanation lines to the output
 */
void ldns_dnssec_trust_tree_print_fmt(FILE *out,
		const ldns_output_format *fmt,
	       	ldns_dnssec_trust_tree *tree,
		size_t tabs,
		bool extended);

/**
 * Adds a trust tree as a parent for the given trust tree
 *
 * \param[in] *tree The tree to add the parent to
 * \param[in] *parent The parent tree to add
 * \param[in] *parent_signature The RRSIG relevant to this parent/child
 *                              connection
 * \param[in] parent_status The DNSSEC status for this parent, child and RRSIG
 * \return LDNS_STATUS_OK if the addition succeeds, error otherwise
 */
ldns_status ldns_dnssec_trust_tree_add_parent(ldns_dnssec_trust_tree *tree,
									 const ldns_dnssec_trust_tree *parent,
									 const ldns_rr *parent_signature,
									 const ldns_status parent_status);

/**
 * Generates a dnssec_trust_tree for the given rr from the
 * given data_chain
 *
 * This does not clone the actual data; Don't free the
 * data_chain before you are done with this tree
 *
 * \param[in] *data_chain The chain to derive the trust tree from
 * \param[in] *rr The RR this tree will be about
 * \return ldns_dnssec_trust_tree *
 */
ldns_dnssec_trust_tree *ldns_dnssec_derive_trust_tree(
                            ldns_dnssec_data_chain *data_chain,
					   ldns_rr *rr);

/**
 * Generates a dnssec_trust_tree for the given rr from the
 * given data_chain
 *
 * This does not clone the actual data; Don't free the
 * data_chain before you are done with this tree
 *
 * \param[in] *data_chain The chain to derive the trust tree from
 * \param[in] *rr The RR this tree will be about
 * \param[in] check_time the time for which the validation is performed
 * \return ldns_dnssec_trust_tree *
 */
ldns_dnssec_trust_tree *ldns_dnssec_derive_trust_tree_time(
		ldns_dnssec_data_chain *data_chain, 
		ldns_rr *rr, time_t check_time);

/**
 * Sub function for derive_trust_tree that is used for a 'normal' rrset
 *
 * \param[in] new_tree The trust tree that we are building
 * \param[in] data_chain The data chain containing the data for the trust tree
 * \param[in] cur_sig_rr The currently relevant signature
 */
void ldns_dnssec_derive_trust_tree_normal_rrset(
         ldns_dnssec_trust_tree *new_tree,
	    ldns_dnssec_data_chain *data_chain,
	    ldns_rr *cur_sig_rr);

/**
 * Sub function for derive_trust_tree that is used for a 'normal' rrset
 *
 * \param[in] new_tree The trust tree that we are building
 * \param[in] data_chain The data chain containing the data for the trust tree
 * \param[in] cur_sig_rr The currently relevant signature
 * \param[in] check_time the time for which the validation is performed
 */
void ldns_dnssec_derive_trust_tree_normal_rrset_time(
         ldns_dnssec_trust_tree *new_tree,
	    ldns_dnssec_data_chain *data_chain,
	    ldns_rr *cur_sig_rr, time_t check_time);


/**
 * Sub function for derive_trust_tree that is used for DNSKEY rrsets
 *
 * \param[in] new_tree The trust tree that we are building
 * \param[in] data_chain The data chain containing the data for the trust tree
 * \param[in] cur_rr The currently relevant DNSKEY RR
 * \param[in] cur_sig_rr The currently relevant signature
 */
void ldns_dnssec_derive_trust_tree_dnskey_rrset(
         ldns_dnssec_trust_tree *new_tree,
	    ldns_dnssec_data_chain *data_chain,
	    ldns_rr *cur_rr,
	    ldns_rr *cur_sig_rr);

/**
 * Sub function for derive_trust_tree that is used for DNSKEY rrsets
 *
 * \param[in] new_tree The trust tree that we are building
 * \param[in] data_chain The data chain containing the data for the trust tree
 * \param[in] cur_rr The currently relevant DNSKEY RR
 * \param[in] cur_sig_rr The currently relevant signature
 * \param[in] check_time the time for which the validation is performed
 */
void ldns_dnssec_derive_trust_tree_dnskey_rrset_time(
         ldns_dnssec_trust_tree *new_tree,
	    ldns_dnssec_data_chain *data_chain,
	    ldns_rr *cur_rr, ldns_rr *cur_sig_rr,
	    time_t check_time);

/**
 * Sub function for derive_trust_tree that is used for DS rrsets
 *
 * \param[in] new_tree The trust tree that we are building
 * \param[in] data_chain The data chain containing the data for the trust tree
 * \param[in] cur_rr The currently relevant DS RR
 */
void ldns_dnssec_derive_trust_tree_ds_rrset(
         ldns_dnssec_trust_tree *new_tree,
	    ldns_dnssec_data_chain *data_chain,
	    ldns_rr *cur_rr);

/**
 * Sub function for derive_trust_tree that is used for DS rrsets
 *
 * \param[in] new_tree The trust tree that we are building
 * \param[in] data_chain The data chain containing the data for the trust tree
 * \param[in] cur_rr The currently relevant DS RR
 * \param[in] check_time the time for which the validation is performed
 */
void ldns_dnssec_derive_trust_tree_ds_rrset_time(
         ldns_dnssec_trust_tree *new_tree,
	    ldns_dnssec_data_chain *data_chain,
	    ldns_rr *cur_rr, time_t check_time);

/**
 * Sub function for derive_trust_tree that is used when there are no
 * signatures
 *
 * \param[in] new_tree The trust tree that we are building
 * \param[in] data_chain The data chain containing the data for the trust tree
 */
void ldns_dnssec_derive_trust_tree_no_sig(
         ldns_dnssec_trust_tree *new_tree,
	    ldns_dnssec_data_chain *data_chain);

/**
 * Sub function for derive_trust_tree that is used when there are no
 * signatures
 *
 * \param[in] new_tree The trust tree that we are building
 * \param[in] data_chain The data chain containing the data for the trust tree
 * \param[in] check_time the time for which the validation is performed
 */
void ldns_dnssec_derive_trust_tree_no_sig_time(
         ldns_dnssec_trust_tree *new_tree,
	    ldns_dnssec_data_chain *data_chain,
	    time_t check_time);


/**
 * Returns OK if there is a trusted path in the tree to one of 
 * the DNSKEY or DS RRs in the given list
 *
 * \param *tree The trust tree so search
 * \param *keys A ldns_rr_list of DNSKEY and DS rrs to look for
 *
 * \return LDNS_STATUS_OK if there is a trusted path to one of
 *                        the keys, or the *first* error encountered
 *                        if there were no paths
 */
ldns_status ldns_dnssec_trust_tree_contains_keys(
			 ldns_dnssec_trust_tree *tree,
			 ldns_rr_list *keys);

/**
 * Verifies a list of signatures for one rrset.
 *
 * \param[in] rrset the rrset to verify
 * \param[in] rrsig a list of signatures to check
 * \param[in] keys a list of keys to check with
 * \param[out] good_keys  if this is a (initialized) list, the pointer to keys
 *                        from keys that validate one of the signatures
 *                        are added to it
 * \return status LDNS_STATUS_OK if there is at least one correct key
 */
ldns_status ldns_verify(ldns_rr_list *rrset,
				    ldns_rr_list *rrsig,
				    const ldns_rr_list *keys,
				    ldns_rr_list *good_keys);	

/**
 * Verifies a list of signatures for one rrset.
 *
 * \param[in] rrset the rrset to verify
 * \param[in] rrsig a list of signatures to check
 * \param[in] keys a list of keys to check with
 * \param[in] check_time the time for which the validation is performed
 * \param[out] good_keys  if this is a (initialized) list, the pointer to keys
 *                        from keys that validate one of the signatures
 *                        are added to it
 * \return status LDNS_STATUS_OK if there is at least one correct key
 */
ldns_status ldns_verify_time(const ldns_rr_list *rrset,
				    const ldns_rr_list *rrsig,
				    const ldns_rr_list *keys,
				    time_t check_time,
				    ldns_rr_list *good_keys);	


/**
 * Verifies a list of signatures for one rrset, but disregard the time.
 * Inception and Expiration are not checked.
 *
 * \param[in] rrset the rrset to verify
 * \param[in] rrsig a list of signatures to check
 * \param[in] keys a list of keys to check with
 * \param[out] good_keys  if this is a (initialized) list, the pointer to keys
 *                        from keys that validate one of the signatures
 *                        are added to it
 * \return status LDNS_STATUS_OK if there is at least one correct key
 */
ldns_status ldns_verify_notime(ldns_rr_list *rrset,
				    ldns_rr_list *rrsig,
				    const ldns_rr_list *keys,
				    ldns_rr_list *good_keys);	

/**
 * Tries to build an authentication chain from the given 
 * keys down to the queried domain.
 *
 * If we find a valid trust path, return the valid keys for the domain.
 * 
 * \param[in] res the current resolver
 * \param[in] domain the domain we want valid keys for
 * \param[in] keys the current set of trusted keys
 * \param[out] status pointer to the status variable where the result
 *                    code will be stored
 * \return the set of trusted keys for the domain, or NULL if no 
 *         trust path could be built.
 */
ldns_rr_list *ldns_fetch_valid_domain_keys(const ldns_resolver * res,
								   const ldns_rdf * domain,
								   const ldns_rr_list * keys,
								   ldns_status *status);

/**
 * Tries to build an authentication chain from the given 
 * keys down to the queried domain.
 *
 * If we find a valid trust path, return the valid keys for the domain.
 * 
 * \param[in] res the current resolver
 * \param[in] domain the domain we want valid keys for
 * \param[in] keys the current set of trusted keys
 * \param[in] check_time the time for which the validation is performed
 * \param[out] status pointer to the status variable where the result
 *                    code will be stored
 * \return the set of trusted keys for the domain, or NULL if no 
 *         trust path could be built.
 */
ldns_rr_list *ldns_fetch_valid_domain_keys_time(const ldns_resolver * res,
		const ldns_rdf * domain, const ldns_rr_list * keys,
		time_t check_time, ldns_status *status);


/**
 * Validates the DNSKEY RRset for the given domain using the provided 
 * trusted keys.
 *
 * \param[in] res the current resolver
 * \param[in] domain the domain we want valid keys for
 * \param[in] keys the current set of trusted keys
 * \return the set of trusted keys for the domain, or NULL if the RRSET
 *         could not be validated
 */
ldns_rr_list *ldns_validate_domain_dnskey (const ldns_resolver *res,
								   const ldns_rdf *domain,
								   const ldns_rr_list *keys);

/**
 * Validates the DNSKEY RRset for the given domain using the provided 
 * trusted keys.
 *
 * \param[in] res the current resolver
 * \param[in] domain the domain we want valid keys for
 * \param[in] keys the current set of trusted keys
 * \param[in] check_time the time for which the validation is performed
 * \return the set of trusted keys for the domain, or NULL if the RRSET
 *         could not be validated
 */
ldns_rr_list *ldns_validate_domain_dnskey_time(
		const ldns_resolver *res, const ldns_rdf *domain, 
		const ldns_rr_list *keys, time_t check_time);


/**
 * Validates the DS RRset for the given domain using the provided trusted keys.
 *
 * \param[in] res the current resolver
 * \param[in] domain the domain we want valid keys for
 * \param[in] keys the current set of trusted keys
 * \return the set of trusted keys for the domain, or NULL if the RRSET could not be validated
 */
ldns_rr_list *ldns_validate_domain_ds(const ldns_resolver *res,
							   const ldns_rdf *
							   domain,
							   const ldns_rr_list * keys);

/**
 * Validates the DS RRset for the given domain using the provided trusted keys.
 *
 * \param[in] res the current resolver
 * \param[in] domain the domain we want valid keys for
 * \param[in] keys the current set of trusted keys
 * \param[in] check_time the time for which the validation is performed
 * \return the set of trusted keys for the domain, or NULL if the RRSET could not be validated
 */
ldns_rr_list *ldns_validate_domain_ds_time(
		const ldns_resolver *res, const ldns_rdf *domain, 
		const ldns_rr_list * keys, time_t check_time);


/**
 * Verifies a list of signatures for one RRset using a valid trust path.
 *
 * \param[in] res the current resolver
 * \param[in] rrset the rrset to verify
 * \param[in] rrsigs a list of signatures to check
 * \param[out] validating_keys  if this is a (initialized) list, the
 *                              keys from keys that validate one of
 *                              the signatures are added to it
 * \return status LDNS_STATUS_OK if there is at least one correct key
 */
ldns_status ldns_verify_trusted(ldns_resolver *res,
						  ldns_rr_list *rrset,
						  ldns_rr_list *rrsigs,
						  ldns_rr_list *validating_keys);

/**
 * Verifies a list of signatures for one RRset using a valid trust path.
 *
 * \param[in] res the current resolver
 * \param[in] rrset the rrset to verify
 * \param[in] rrsigs a list of signatures to check
 * \param[in] check_time the time for which the validation is performed
 * \param[out] validating_keys  if this is a (initialized) list, the
 *                              keys from keys that validate one of
 *                              the signatures are added to it
 * \return status LDNS_STATUS_OK if there is at least one correct key
 */
ldns_status ldns_verify_trusted_time(
		ldns_resolver *res, ldns_rr_list *rrset, 
		ldns_rr_list *rrsigs, time_t check_time,
		ldns_rr_list *validating_keys);


/**
 * denial is not just a river in egypt
 *
 * \param[in] rr The (query) RR to check the denial of existence for
 * \param[in] nsecs The list of NSEC RRs that are supposed to deny the
 *                  existence of the RR
 * \param[in] rrsigs The RRSIG RR covering the NSEC RRs
 * \return LDNS_STATUS_OK if the NSEC RRs deny the existence, error code
 *                        containing the reason they do not otherwise
 */
ldns_status ldns_dnssec_verify_denial(ldns_rr *rr,
							   ldns_rr_list *nsecs,
							   ldns_rr_list *rrsigs);

/**
 * Denial of existence using NSEC3 records
 * Since NSEC3 is a bit more complicated than normal denial, some
 * context arguments are needed
 *
 * \param[in] rr The (query) RR to check the denial of existence for
 * \param[in] nsecs The list of NSEC3 RRs that are supposed to deny the
 *                  existence of the RR
 * \param[in] rrsigs The RRSIG rr covering the NSEC RRs
 * \param[in] packet_rcode The RCODE value of the packet that provided the
 *                         NSEC3 RRs
 * \param[in] packet_qtype The original query RR type
 * \param[in] packet_nodata True if the providing packet had an empty ANSWER
 *                          section
 * \return LDNS_STATUS_OK if the NSEC3 RRs deny the existence, error code
 *                        containing the reason they do not otherwise
 */
ldns_status ldns_dnssec_verify_denial_nsec3(ldns_rr *rr,
								    ldns_rr_list *nsecs,
								    ldns_rr_list *rrsigs,
								    ldns_pkt_rcode packet_rcode,
								    ldns_rr_type packet_qtype,
								    bool packet_nodata);

/**
 * Same as ldns_status ldns_dnssec_verify_denial_nsec3 but also returns
 * the nsec rr that matched.
 *
 * \param[in] rr The (query) RR to check the denial of existence for
 * \param[in] nsecs The list of NSEC3 RRs that are supposed to deny the
 *                  existence of the RR
 * \param[in] rrsigs The RRSIG rr covering the NSEC RRs
 * \param[in] packet_rcode The RCODE value of the packet that provided the
 *                         NSEC3 RRs
 * \param[in] packet_qtype The original query RR type
 * \param[in] packet_nodata True if the providing packet had an empty ANSWER
 *                          section
 * \param[in] match On match, the given (reference to a) pointer will be set 
 *                  to point to the matching nsec resource record.
 * \return LDNS_STATUS_OK if the NSEC3 RRs deny the existence, error code
 *                        containing the reason they do not otherwise
 */
ldns_status ldns_dnssec_verify_denial_nsec3_match(ldns_rr *rr,
						  ldns_rr_list *nsecs,
						  ldns_rr_list *rrsigs,
						  ldns_pkt_rcode packet_rcode,
						  ldns_rr_type packet_qtype,
						  bool packet_nodata,
						  ldns_rr **match);
/**
 * Verifies the already processed data in the buffers
 * This function should probably not be used directly.
 *
 * \param[in] rawsig_buf Buffer containing signature data to use
 * \param[in] verify_buf Buffer containing data to verify
 * \param[in] key_buf Buffer containing key data to use
 * \param[in] algo Signing algorithm
 * \return status LDNS_STATUS_OK if the data verifies. Error if not.
 */
ldns_status ldns_verify_rrsig_buffers(ldns_buffer *rawsig_buf,
							   ldns_buffer *verify_buf,
							   ldns_buffer *key_buf,
							   uint8_t algo);

/**
 * Like ldns_verify_rrsig_buffers, but uses raw data.
 *
 * \param[in] sig signature data to use
 * \param[in] siglen length of signature data to use
 * \param[in] verify_buf Buffer containing data to verify
 * \param[in] key key data to use
 * \param[in] keylen length of key data to use
 * \param[in] algo Signing algorithm
 * \return status LDNS_STATUS_OK if the data verifies. Error if not.
 */
ldns_status ldns_verify_rrsig_buffers_raw(unsigned char* sig,
								  size_t siglen, 
								  ldns_buffer *verify_buf,
								  unsigned char* key,
								  size_t keylen, 
								  uint8_t algo);

/**
 * Verifies an rrsig. All keys in the keyset are tried.
 * \param[in] rrset the rrset to check
 * \param[in] rrsig the signature of the rrset
 * \param[in] keys the keys to try
 * \param[out] good_keys  if this is a (initialized) list, the pointer to keys
 *                        from keys that validate one of the signatures
 *                        are added to it
 * \return a list of keys which validate the rrsig + rrset. Returns
 * status LDNS_STATUS_OK if at least one key matched. Else an error.
 */
ldns_status ldns_verify_rrsig_keylist(ldns_rr_list *rrset,
							   ldns_rr *rrsig,
							   const ldns_rr_list *keys,
							   ldns_rr_list *good_keys);

/**
 * Verifies an rrsig. All keys in the keyset are tried.
 * \param[in] rrset the rrset to check
 * \param[in] rrsig the signature of the rrset
 * \param[in] keys the keys to try
 * \param[in] check_time the time for which the validation is performed
 * \param[out] good_keys  if this is a (initialized) list, the pointer to keys
 *                        from keys that validate one of the signatures
 *                        are added to it
 * \return a list of keys which validate the rrsig + rrset. Returns
 * status LDNS_STATUS_OK if at least one key matched. Else an error.
 */
ldns_status ldns_verify_rrsig_keylist_time(
		const ldns_rr_list *rrset, const ldns_rr *rrsig, 
		const ldns_rr_list *keys, time_t check_time,
	       	ldns_rr_list *good_keys);


/**
 * Verifies an rrsig. All keys in the keyset are tried. Time is not checked.
 * \param[in] rrset the rrset to check
 * \param[in] rrsig the signature of the rrset
 * \param[in] keys the keys to try
 * \param[out] good_keys  if this is a (initialized) list, the pointer to keys
 *                        from keys that validate one of the signatures
 *                        are added to it
 * \return a list of keys which validate the rrsig + rrset. Returns
 * status LDNS_STATUS_OK if at least one key matched. Else an error.
 */
ldns_status ldns_verify_rrsig_keylist_notime(const ldns_rr_list *rrset,
							   const ldns_rr *rrsig,
							   const ldns_rr_list *keys,
							   ldns_rr_list *good_keys);

/**
 * verify an rrsig with 1 key
 * \param[in] rrset the rrset
 * \param[in] rrsig the rrsig to verify
 * \param[in] key the key to use
 * \return status message wether verification succeeded.
 */
ldns_status ldns_verify_rrsig(ldns_rr_list *rrset,
						ldns_rr *rrsig,
						ldns_rr *key);


/**
 * verify an rrsig with 1 key
 * \param[in] rrset the rrset
 * \param[in] rrsig the rrsig to verify
 * \param[in] key the key to use
 * \param[in] check_time the time for which the validation is performed
 * \return status message wether verification succeeded.
 */
ldns_status ldns_verify_rrsig_time(
		ldns_rr_list *rrset, ldns_rr *rrsig, 
		ldns_rr *key, time_t check_time);


#if LDNS_BUILD_CONFIG_HAVE_SSL
/**
 * verifies a buffer with signature data for a buffer with rrset data 
 * with an EVP_PKEY
 *
 * \param[in] sig the signature data
 * \param[in] rrset the rrset data, sorted and processed for verification
 * \param[in] key the EVP key structure
 * \param[in] digest_type The digest type of the signature
 */
ldns_status ldns_verify_rrsig_evp(ldns_buffer *sig,
						    ldns_buffer *rrset,
						    EVP_PKEY *key,
						    const EVP_MD *digest_type);

/**
 * Like ldns_verify_rrsig_evp, but uses raw signature data.
 * \param[in] sig the signature data, wireformat uncompressed
 * \param[in] siglen length of the signature data
 * \param[in] rrset the rrset data, sorted and processed for verification
 * \param[in] key the EVP key structure
 * \param[in] digest_type The digest type of the signature
 */
ldns_status ldns_verify_rrsig_evp_raw(const unsigned char *sig,
							   size_t siglen,
							   const ldns_buffer *rrset,
							   EVP_PKEY *key,
							   const EVP_MD *digest_type);
#endif

/**
 * verifies a buffer with signature data (DSA) for a buffer with rrset data 
 * with a buffer with key data.
 *
 * \param[in] sig the signature data
 * \param[in] rrset the rrset data, sorted and processed for verification
 * \param[in] key the key data
 */
ldns_status ldns_verify_rrsig_dsa(ldns_buffer *sig,
						    ldns_buffer *rrset,
						    ldns_buffer *key);

/**
 * verifies a buffer with signature data (RSASHA1) for a buffer with rrset data 
 * with a buffer with key data.
 *
 * \param[in] sig the signature data
 * \param[in] rrset the rrset data, sorted and processed for verification
 * \param[in] key the key data
 */
ldns_status ldns_verify_rrsig_rsasha1(ldns_buffer *sig,
							   ldns_buffer *rrset,
							   ldns_buffer *key);

/**
 * verifies a buffer with signature data (RSAMD5) for a buffer with rrset data 
 * with a buffer with key data.
 *
 * \param[in] sig the signature data
 * \param[in] rrset the rrset data, sorted and processed for verification
 * \param[in] key the key data
 */
ldns_status ldns_verify_rrsig_rsamd5(ldns_buffer *sig,
							  ldns_buffer *rrset,
							  ldns_buffer *key);

/**
 * Like ldns_verify_rrsig_dsa, but uses raw signature and key data.
 * \param[in] sig raw uncompressed wireformat signature data
 * \param[in] siglen length of signature data
 * \param[in] rrset ldns buffer with prepared rrset data.
 * \param[in] key raw uncompressed wireformat key data
 * \param[in] keylen length of key data
 */
ldns_status ldns_verify_rrsig_dsa_raw(unsigned char* sig,
							   size_t siglen,
							   ldns_buffer* rrset,
							   unsigned char* key,
							   size_t keylen);

/**
 * Like ldns_verify_rrsig_rsasha1, but uses raw signature and key data.
 * \param[in] sig raw uncompressed wireformat signature data
 * \param[in] siglen length of signature data
 * \param[in] rrset ldns buffer with prepared rrset data.
 * \param[in] key raw uncompressed wireformat key data
 * \param[in] keylen length of key data
 */
ldns_status ldns_verify_rrsig_rsasha1_raw(unsigned char* sig,
								  size_t siglen,
								  ldns_buffer* rrset,
								  unsigned char* key,
								  size_t keylen);

/**
 * Like ldns_verify_rrsig_rsasha256, but uses raw signature and key data.
 * \param[in] sig raw uncompressed wireformat signature data
 * \param[in] siglen length of signature data
 * \param[in] rrset ldns buffer with prepared rrset data.
 * \param[in] key raw uncompressed wireformat key data
 * \param[in] keylen length of key data
 */

ldns_status ldns_verify_rrsig_rsasha256_raw(unsigned char* sig,
								    size_t siglen,
								    ldns_buffer* rrset,
								    unsigned char* key,
								    size_t keylen);

/**
 * Like ldns_verify_rrsig_rsasha512, but uses raw signature and key data.
 * \param[in] sig raw uncompressed wireformat signature data
 * \param[in] siglen length of signature data
 * \param[in] rrset ldns buffer with prepared rrset data.
 * \param[in] key raw uncompressed wireformat key data
 * \param[in] keylen length of key data
 */
ldns_status ldns_verify_rrsig_rsasha512_raw(unsigned char* sig,
								    size_t siglen,
								    ldns_buffer* rrset,
								    unsigned char* key,
								    size_t keylen);

/**
 * Like ldns_verify_rrsig_rsamd5, but uses raw signature and key data.
 * \param[in] sig raw uncompressed wireformat signature data
 * \param[in] siglen length of signature data
 * \param[in] rrset ldns buffer with prepared rrset data.
 * \param[in] key raw uncompressed wireformat key data
 * \param[in] keylen length of key data
 */
ldns_status ldns_verify_rrsig_rsamd5_raw(unsigned char* sig,
								 size_t siglen,
								 ldns_buffer* rrset,
								 unsigned char* key,
								 size_t keylen);

#ifdef __cplusplus
}
#endif

#endif

