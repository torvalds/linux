/*
 * util.c
 * some handy function needed in drill and not implemented
 * in ldns
 * (c) 2005 NLnet Labs
 *
 * See the file LICENSE for the license
 *
 */

#include "drill.h"
#include <ldns/ldns.h>

#include <errno.h>

static int
read_line(FILE *input, char *line, size_t len)
{
	int i;
	int c;

	for (i = 0; i < (int)len-1; i++) {
		c = getc(input);
		if (c == EOF) {
			return -1;
		} else if (c != '\n') {
			line[i] = c;
		} else {
			break;
		}
	}
	line[i] = '\0';
	return i;
}

/* key_list must be initialized with ldns_rr_list_new() */
ldns_status
read_key_file(const char *filename, ldns_rr_list *key_list, bool silently)
{
	int line_len = 0;
	int line_nr = 0;
	int key_count = 0;
	char line[LDNS_MAX_LINELEN];
	ldns_status status;
	FILE *input_file;
	ldns_rr *rr;

	input_file = fopen(filename, "r");
	if (!input_file) {
		if (! silently) {
			fprintf(stderr, "Error opening %s: %s\n",
				filename, strerror(errno));
		}
		return LDNS_STATUS_ERR;
	}
	while (line_len >= 0) {
		line_len = (int) read_line(input_file, line, sizeof(line));
		line_nr++;
		if (line_len > 0 && line[0] != ';') {
			status = ldns_rr_new_frm_str(&rr, line, 0, NULL, NULL);
			if (status != LDNS_STATUS_OK) {
				if (! silently) {
					fprintf(stderr,
						"Error parsing DNSKEY RR "
						"in line %d: %s\n", line_nr,
						ldns_get_errorstr_by_id(status)
						);
				}
			} else if (ldns_rr_get_type(rr) == LDNS_RR_TYPE_DNSKEY || 
					   ldns_rr_get_type(rr) == LDNS_RR_TYPE_DS) {
				ldns_rr_list_push_rr(key_list, rr);
				key_count++;
			} else {
				ldns_rr_free(rr);
			}
		}
	}
	fclose(input_file);
	if (key_count > 0) {
		return LDNS_STATUS_OK;
	} else {
		/*fprintf(stderr, "No keys read\n");*/
		return LDNS_STATUS_ERR;
	}
}

ldns_rdf *
ldns_rdf_new_addr_frm_str(char *str)
{
	ldns_rdf *a;

	a = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_A, str);
	if (!a) {
		/* maybe ip6 */
		a = ldns_rdf_new_frm_str(LDNS_RDF_TYPE_AAAA, str);
		if (!a) {
			return NULL;
		}
	}
	return a;
}

static inline void
local_print_ds(FILE* out, const char* pre, ldns_rr* ds)
{
	if (out && ds) {
		fprintf(out, "%s", pre);
		ldns_rr_print(out, ds);
		ldns_rr_free(ds);
	}
}

/*
 * For all keys in a packet print the DS 
 */
void
print_ds_of_keys(ldns_pkt *p)
{
	ldns_rr_list *keys;
	uint16_t i;
	ldns_rr *ds;

	/* TODO fix the section stuff, here or in ldns */
	keys = ldns_pkt_rr_list_by_type(p, LDNS_RR_TYPE_DNSKEY,
			LDNS_SECTION_ANSWER);

	/* this also returns the question section rr, which does not
	 * have any data.... and this inturn crashes everything */

	if (keys) {
		for (i = 0; i < ldns_rr_list_rr_count(keys); i++) {
			fprintf(stdout, ";\n; equivalent DS records for key %u:\n",
				(unsigned int)ldns_calc_keytag(ldns_rr_list_rr(keys, i)));

			ds = ldns_key_rr2ds(ldns_rr_list_rr(keys, i), LDNS_SHA1);
			local_print_ds(stdout, "; sha1: ", ds);
			ds = ldns_key_rr2ds(ldns_rr_list_rr(keys, i), LDNS_SHA256);
			local_print_ds(stdout, "; sha256: ", ds);
		}
		ldns_rr_list_deep_free(keys);
	}
}

static void
print_class_type(FILE *fp, ldns_rr *r)
{
	ldns_lookup_table *lt;
        lt = ldns_lookup_by_id(ldns_rr_classes, ldns_rr_get_class(r));
        if (lt) {
               	fprintf(fp, " %s", lt->name);
        } else {
        	fprintf(fp, " CLASS%d", ldns_rr_get_class(r));
        }
	/* okay not THE way - but the quickest */
	switch (ldns_rr_get_type(r)) {
		case LDNS_RR_TYPE_RRSIG:
			fprintf(fp, " RRSIG ");
			break;
		case LDNS_RR_TYPE_DNSKEY:
			fprintf(fp, " DNSKEY ");
			break;
		case LDNS_RR_TYPE_DS:
			fprintf(fp, " DS ");
			break;
		default:
			break;
	}
}


void
print_ds_abbr(FILE *fp, ldns_rr *ds)
{
	if (!ds || (ldns_rr_get_type(ds) != LDNS_RR_TYPE_DS)) {
		return;
	}

	ldns_rdf_print(fp, ldns_rr_owner(ds));
	fprintf(fp, " %d", (int)ldns_rr_ttl(ds));
	print_class_type(fp, ds);
	ldns_rdf_print(fp, ldns_rr_rdf(ds, 0)); fprintf(fp, " ");
	ldns_rdf_print(fp, ldns_rr_rdf(ds, 1)); fprintf(fp, " ");
	ldns_rdf_print(fp, ldns_rr_rdf(ds, 2)); fprintf(fp, " ");
	ldns_rdf_print(fp, ldns_rr_rdf(ds, 3)); fprintf(fp, " ");
}

/* print some of the elements of a signature */
void
print_rrsig_abbr(FILE *fp, ldns_rr *sig) {
	if (!sig || (ldns_rr_get_type(sig) != LDNS_RR_TYPE_RRSIG)) {
		return;
	}

	ldns_rdf_print(fp, ldns_rr_owner(sig));
	fprintf(fp, " %d", (int)ldns_rr_ttl(sig));
	print_class_type(fp, sig);

	/* print a number of rdf's */
	/* typecovered */
	ldns_rdf_print(fp, ldns_rr_rdf(sig, 0)); fprintf(fp, " ");
	/* algo */
	ldns_rdf_print(fp, ldns_rr_rdf(sig, 1)); fprintf(fp, " ");
	/* labels */
	ldns_rdf_print(fp, ldns_rr_rdf(sig, 2)); fprintf(fp, " (\n\t\t\t");
	/* expir */
	ldns_rdf_print(fp, ldns_rr_rdf(sig, 4)); fprintf(fp, " ");
	/* incep */	
	ldns_rdf_print(fp, ldns_rr_rdf(sig, 5)); fprintf(fp, " ");
	/* key-id */	
	ldns_rdf_print(fp, ldns_rr_rdf(sig, 6)); fprintf(fp, " ");
	/* key owner */
	ldns_rdf_print(fp, ldns_rr_rdf(sig, 7)); fprintf(fp, ")");
}

void
print_dnskey_abbr(FILE *fp, ldns_rr *key)
{
        if (!key || (ldns_rr_get_type(key) != LDNS_RR_TYPE_DNSKEY)) {
                return;
        }

        ldns_rdf_print(fp, ldns_rr_owner(key));
        fprintf(fp, " %d", (int)ldns_rr_ttl(key));
	print_class_type(fp, key);

        /* print a number of rdf's */
        /* flags */
        ldns_rdf_print(fp, ldns_rr_rdf(key, 0)); fprintf(fp, " ");
        /* proto */
        ldns_rdf_print(fp, ldns_rr_rdf(key, 1)); fprintf(fp, " ");
        /* algo */
        ldns_rdf_print(fp, ldns_rr_rdf(key, 2));

	if (ldns_rdf2native_int16(ldns_rr_rdf(key, 0)) == 256) {
		fprintf(fp, " ;{id = %u (zsk), size = %db}", (unsigned int)ldns_calc_keytag(key),
				(int)ldns_rr_dnskey_key_size(key));
		return;
	}
	if (ldns_rdf2native_int16(ldns_rr_rdf(key, 0)) == 257) {
		fprintf(fp, " ;{id = %u (ksk), size = %db}", (unsigned int)ldns_calc_keytag(key),
				(int)ldns_rr_dnskey_key_size(key));
		return;
	}
	fprintf(fp, " ;{id = %u, size = %db}", (unsigned int)ldns_calc_keytag(key),
			(int)ldns_rr_dnskey_key_size(key));
}

void
print_rr_list_abbr(FILE *fp, ldns_rr_list *rrlist, const char *usr) 
{
	size_t i;
	ldns_rr_type tp;

	for(i = 0; i < ldns_rr_list_rr_count(rrlist); i++) {
		tp = ldns_rr_get_type(ldns_rr_list_rr(rrlist, i));
		if (i == 0 && tp != LDNS_RR_TYPE_RRSIG) {
			if (usr) {
				fprintf(fp, "%s ", usr);
			}
		}
		switch(tp) {
		case LDNS_RR_TYPE_DNSKEY:
			print_dnskey_abbr(fp, ldns_rr_list_rr(rrlist, i));
			break;
		case LDNS_RR_TYPE_RRSIG:
			print_rrsig_abbr(fp, ldns_rr_list_rr(rrlist, i));
			break;
		case LDNS_RR_TYPE_DS:
			print_ds_abbr(fp, ldns_rr_list_rr(rrlist, i));
			break;
		default:
			/* not handled */
			break;
		}
		fputs("\n", fp);
	}
}

void *
xmalloc(size_t s)
{
	void *p;

	p = malloc(s);
	if (!p) {
		printf("Mem failure\n");
		exit(EXIT_FAILURE);
	}
	return p;
}

void *
xrealloc(void *p, size_t size)
{
	void *q;

	q = realloc(p, size);
	if (!q) {
		printf("Mem failure\n");
		exit(EXIT_FAILURE);
	}
	return q;
}

void
xfree(void *p)
{
	if (p) {
	        free(p);
	}
}
