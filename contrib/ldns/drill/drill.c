/*
 * drill.c
 * the main file of drill
 * (c) 2005-2008 NLnet Labs
 *
 * See the file LICENSE for the license
 *
 */

#include "drill.h"
#include <ldns/ldns.h>

#ifdef HAVE_SSL
#include <openssl/err.h>
#endif

/* query debug, 2 hex dumps */
int		verbosity;

static int
is_ixfr_with_serial(const char* name, uint32_t *serial)
{
	char* end;
	if (strlen(name) > 5 &&
		strncasecmp(name, "IXFR", 4) == 0 &&
		name[4] == '=') {
		*serial = (uint32_t) strtol((name+5), &end, 10);
		return 1;
	}
	return 0;
}

static void
usage(FILE *stream, const char *progname)
{
	fprintf(stream, "  Usage: %s name [@server] [type] [class]\n", progname);
	fprintf(stream, "\t<name>  can be a domain name or an IP address (-x lookups)\n");
	fprintf(stream, "\t<type>  defaults to A\n");
	fprintf(stream, "\t<class> defaults to IN\n");
	fprintf(stream, "\n\targuments may be placed in random order\n");
	fprintf(stream, "\n  Options:\n");
	fprintf(stream, "\t-D\t\tenable DNSSEC (DO bit)\n");
#ifdef HAVE_SSL
	fprintf(stream, "\t-T\t\ttrace from the root down to <name>\n");
	fprintf(stream, "\t-S\t\tchase signature(s) from <name> to a known key [*]\n");
#endif /*HAVE_SSL*/
	fprintf(stream, "\t-I <address>\tsource address to query from\n");
	fprintf(stream, "\t-V <number>\tverbosity (0-5)\n");
	fprintf(stream, "\t-Q\t\tquiet mode (overrules -V)\n");
	fprintf(stream, "\n");
	fprintf(stream, "\t-f file\t\tread packet from file and send it\n");
	fprintf(stream, "\t-i file\t\tread packet from file and print it\n");
	fprintf(stream, "\t-w file\t\twrite answer packet to file\n");
	fprintf(stream, "\t-q file\t\twrite query packet to file\n");
	fprintf(stream, "\t-h\t\tshow this help\n");
	fprintf(stream, "\t-v\t\tshow version\n");
	fprintf(stream, "\n  Query options:\n");
	fprintf(stream, "\t-4\t\tstay on ip4\n");
	fprintf(stream, "\t-6\t\tstay on ip6\n");
	fprintf(stream, "\t-a\t\tfallback to EDNS0 and TCP if the answer is truncated\n");
	fprintf(stream, "\t-b <bufsize>\tuse <bufsize> as the buffer size (defaults to 512 b)\n");
	fprintf(stream, "\t-c <file>\tuse file for rescursive nameserver configuration"
			"\n\t\t\t(/etc/resolv.conf)\n");
	fprintf(stream, "\t-k <file>\tspecify a file that contains a trusted DNSSEC key [**]\n");
	fprintf(stream, "\t\t\tUsed to verify any signatures in the current answer.\n");
	fprintf(stream, "\t\t\tWhen DNSSEC enabled tracing (-TD) or signature\n"
			"\t\t\tchasing (-S) and no key files are given, keys are read\n"
			"\t\t\tfrom: %s\n",
			LDNS_TRUST_ANCHOR_FILE);
	fprintf(stream, "\t-o <mnemonic>\tset flags to:"
			"\n\t\t\t[QR|qr][AA|aa][TC|tc][RD|rd][CD|cd][RA|ra][AD|ad]\n");
	fprintf(stream, "\t\t\tlowercase: unset bit, uppercase: set bit\n");
	fprintf(stream, "\t-p <port>\tuse <port> as remote port number\n");
	fprintf(stream, "\t-s\t\tshow the DS RR for each key in a packet\n");
	fprintf(stream, "\t-u\t\tsend the query with udp (the default)\n");
	fprintf(stream, "\t-x\t\tdo a reverse lookup\n");
	fprintf(stream, "\twhen doing a secure trace:\n");
	fprintf(stream, "\t-r <file>\tuse file as root servers hint file\n");
	fprintf(stream, "\t-t\t\tsend the query with tcp (connected)\n");
	fprintf(stream, "\t-d <domain>\tuse domain as the start point for the trace\n");
    fprintf(stream, "\t-y <name:key[:algo]>\tspecify named base64 tsig key, and optional an\n\t\t\talgorithm (defaults to hmac-md5.sig-alg.reg.int)\n");
	fprintf(stream, "\t-z\t\tdon't randomize the nameservers before use\n");
	fprintf(stream, "\n  [*] = enables/implies DNSSEC\n");
	fprintf(stream, "  [**] = can be given more than once\n");
	fprintf(stream, "\n  ldns-team@nlnetlabs.nl | http://www.nlnetlabs.nl/ldns/\n");
}

/**
 * Prints the drill version to stderr
 */
static void
version(FILE *stream, const char *progname)
{
        fprintf(stream, "%s version %s (ldns version %s)\n", progname, DRILL_VERSION, ldns_version());
        fprintf(stream, "Written by NLnet Labs.\n");
        fprintf(stream, "\nCopyright (c) 2004-2008 NLnet Labs.\n");
        fprintf(stream, "Licensed under the revised BSD license.\n");
        fprintf(stream, "There is NO warranty; not even for MERCHANTABILITY or FITNESS\n");
        fprintf(stream, "FOR A PARTICULAR PURPOSE.\n");
}


/**
 * Main function of drill
 * parse the arguments and prepare a query
 */
int
main(int argc, char *argv[])
{
        ldns_resolver	*res = NULL;
        ldns_resolver   *cmdline_res = NULL; /* only used to resolv @name names */
	ldns_rr_list	*cmdline_rr_list = NULL;
	ldns_rdf	*cmdline_dname = NULL;
        ldns_rdf 	*qname, *qname_tmp;
        ldns_pkt	*pkt;
        ldns_pkt	*qpkt;
        char 		*serv;
        char 		*src = NULL;
        const char 	*name;
	char		*progname;
	char 		*query_file = NULL;
	char		*answer_file = NULL;
	ldns_buffer	*query_buffer = NULL;
	ldns_rdf 	*serv_rdf;
	ldns_rdf 	*src_rdf = NULL;
	ldns_rr_type 	type;
	ldns_rr_class	clas;
#if 0
	ldns_pkt_opcode opcode = LDNS_PACKET_QUERY;
#endif
	int 		i, c;
	int 		int_type;
	int		int_clas;
	int		PURPOSE;
	char		*tsig_name = NULL;
	char		*tsig_data = NULL;
	char 		*tsig_algorithm = NULL;
	size_t		tsig_separator;
	size_t		tsig_separator2;
	ldns_rr		*axfr_rr;
	ldns_status	status;
	char *type_str;
	uint32_t	serial = 0;
	/* list of keys used in dnssec operations */
	ldns_rr_list	*key_list = ldns_rr_list_new(); 
	/* what key verify the current answer */
	ldns_rr_list 	*key_verified;

	/* resolver options */
	uint16_t	qflags;
	uint16_t 	qbuf;
	uint16_t	qport;
	uint8_t		qfamily;
	bool		qdnssec;
	bool		qfallback;
	bool		qds;
	bool		qusevc;
	bool 		qrandom;
	
	char		*resolv_conf_file = NULL;
	
	ldns_rdf *trace_start_name = NULL;

	int		result = 0;

	uint8_t         s6addr[16];
	char            ip6_arpa_str[74];

#ifdef USE_WINSOCK
	int r;
	WSADATA wsa_data;
#endif

	int_type = -1; serv = NULL; type = 0; 
	int_clas = -1; name = NULL; clas = 0;
	qname = NULL; src = NULL;
	progname = strdup(argv[0]);

#ifdef USE_WINSOCK
	r = WSAStartup(MAKEWORD(2,2), &wsa_data);
	if(r != 0) {
		printf("Failed WSAStartup: %d\n", r);
		result = EXIT_FAILURE;
		goto exit;
	}
#endif /* USE_WINSOCK */
		
	
	PURPOSE = DRILL_QUERY;
	qflags = LDNS_RD;
	qport = LDNS_PORT;
	verbosity = 2;
	qdnssec = false;
	qfamily = LDNS_RESOLV_INETANY;
	qfallback = false;
	qds = false;
	qbuf = 0;
	qusevc = false;
	qrandom = true;
	key_verified = NULL;

	ldns_init_random(NULL, 0);

	/* string from orig drill: "i:w:I46Sk:TNp:b:DsvhVcuaq:f:xr" */
	/* global first, query opt next, option with parm's last
	 * and sorted */ /*  "46DITSVQf:i:w:q:achuvxzy:so:p:b:k:" */
	                               
	while ((c = getopt(argc, argv, "46ab:c:d:Df:hi:I:k:o:p:q:Qr:sStTuvV:w:xy:z")) != -1) {
		switch(c) {
			/* global options */
			case '4':
				qfamily = LDNS_RESOLV_INET;
				break;
			case '6':
				qfamily = LDNS_RESOLV_INET6;
				break;
			case 'D':
				qdnssec = true;
				break;
			case 'I':
				src = optarg;
				break;
			case 'T':
				if (PURPOSE == DRILL_CHASE) {
					fprintf(stderr, "-T and -S cannot be used at the same time.\n");
					exit(EXIT_FAILURE);
				}
				PURPOSE = DRILL_TRACE;
				break;
#ifdef HAVE_SSL
			case 'S':
				if (PURPOSE == DRILL_TRACE) {
					fprintf(stderr, "-T and -S cannot be used at the same time.\n");
					exit(EXIT_FAILURE);
				}
				PURPOSE = DRILL_CHASE;
				break;
#endif /* HAVE_SSL */
			case 'V':
				if (strtok(optarg, "0123456789") != NULL) {
					fprintf(stderr, "-V expects an number as an argument.\n");
					exit(EXIT_FAILURE);
				}
				verbosity = atoi(optarg);
				break;
			case 'Q':
				verbosity = -1;
				break;
			case 'f':
				query_file = optarg;
				break;
			case 'i':
				answer_file = optarg;
				PURPOSE = DRILL_AFROMFILE;
				break;
			case 'w':
				answer_file = optarg;
				break;
			case 'q':
				query_file = optarg;
				PURPOSE = DRILL_QTOFILE;
				break;
			case 'r':
				if (global_dns_root) {
					fprintf(stderr, "There was already a series of root servers set\n");
					exit(EXIT_FAILURE);
				}
				global_dns_root = read_root_hints(optarg);
				if (!global_dns_root) {
					fprintf(stderr, "Unable to read root hints file %s, aborting\n", optarg);
					exit(EXIT_FAILURE);
				}
				break;
			/* query options */
			case 'a':
				qfallback = true;
				break;
			case 'b':
				qbuf = (uint16_t)atoi(optarg);
				if (qbuf == 0) {
					error("%s", "<bufsize> could not be converted");
				}
				break;
			case 'c':
				resolv_conf_file = optarg;
				break;
			case 't':
				qusevc = true;
				break;
			case 'k':
				status = read_key_file(optarg,
						key_list, false);
				if (status != LDNS_STATUS_OK) {
					error("Could not parse the key file %s: %s", optarg, ldns_get_errorstr_by_id(status));
				}
				qdnssec = true; /* enable that too */
				break;
			case 'o':
				/* only looks at the first hit: capital=ON, lowercase=OFF*/
				if (strstr(optarg, "QR")) {
					DRILL_ON(qflags, LDNS_QR);
				}
				if (strstr(optarg, "qr")) {
					DRILL_OFF(qflags, LDNS_QR);
				}
				if (strstr(optarg, "AA")) {
					DRILL_ON(qflags, LDNS_AA);
				}
				if (strstr(optarg, "aa")) {
					DRILL_OFF(qflags, LDNS_AA);
				}
				if (strstr(optarg, "TC")) {
					DRILL_ON(qflags, LDNS_TC);
				}
				if (strstr(optarg, "tc")) {
					DRILL_OFF(qflags, LDNS_TC);
				}
				if (strstr(optarg, "RD")) {
					DRILL_ON(qflags, LDNS_RD);
				}
				if (strstr(optarg, "rd")) {
					DRILL_OFF(qflags, LDNS_RD);
				}
				if (strstr(optarg, "CD")) {
					DRILL_ON(qflags, LDNS_CD);
				}
				if (strstr(optarg, "cd")) {
					DRILL_OFF(qflags, LDNS_CD);
				}
				if (strstr(optarg, "RA")) {
					DRILL_ON(qflags, LDNS_RA);
				}
				if (strstr(optarg, "ra")) {
					DRILL_OFF(qflags, LDNS_RA);
				}
				if (strstr(optarg, "AD")) {
					DRILL_ON(qflags, LDNS_AD);
				}
				if (strstr(optarg, "ad")) {
					DRILL_OFF(qflags, LDNS_AD);
				}
				break;
			case 'p':
				qport = (uint16_t)atoi(optarg);
				if (qport == 0) {
					error("%s", "<port> could not be converted");
				}
				break;
			case 's':
				qds = true;
				break;
			case 'u':
				qusevc = false;
				break;
			case 'v':
				version(stdout, progname);
				result = EXIT_SUCCESS;
				goto exit;
			case 'x':
				PURPOSE = DRILL_REVERSE;
				break;
			case 'y':
#ifdef HAVE_SSL
				if (strchr(optarg, ':')) {
					tsig_separator = (size_t) (strchr(optarg, ':') - optarg);
					if (strchr(optarg + tsig_separator + 1, ':')) {
						tsig_separator2 = (size_t) (strchr(optarg + tsig_separator + 1, ':') - optarg);
						tsig_algorithm = xmalloc(strlen(optarg) - tsig_separator2);
						strncpy(tsig_algorithm, optarg + tsig_separator2 + 1, strlen(optarg) - tsig_separator2);
						tsig_algorithm[strlen(optarg) - tsig_separator2 - 1] = '\0';
					} else {
						tsig_separator2 = strlen(optarg);
						tsig_algorithm = strdup("hmac-md5.sig-alg.reg.int");
					}
					tsig_name = xmalloc(tsig_separator + 1);
					tsig_data = xmalloc(tsig_separator2 - tsig_separator);
					strncpy(tsig_name, optarg, tsig_separator);
					strncpy(tsig_data, optarg + tsig_separator + 1, tsig_separator2 - tsig_separator - 1);
					/* strncpy does not append \0 if source is longer than n */
					tsig_name[tsig_separator] = '\0';
					tsig_data[ tsig_separator2 - tsig_separator - 1] = '\0';
				}
#else
				fprintf(stderr, "TSIG requested, but SSL is not supported\n");
				result = EXIT_FAILURE;
				goto exit;
#endif /* HAVE_SSL */
				break;
			case 'z':
				qrandom = false;
				break;
			case 'd':
				trace_start_name = ldns_dname_new_frm_str(optarg);
				if (!trace_start_name) {
					fprintf(stderr, "Unable to parse argument for -%c\n", c);
					result = EXIT_FAILURE;
					goto exit;
				}
				break;
			case 'h':
				version(stdout, progname);
				usage(stdout, progname);
				result = EXIT_SUCCESS;
				goto exit;
				break;
			default:
				fprintf(stderr, "Unknown argument: -%c, use -h to see usage\n", c);
				result = EXIT_FAILURE;
				goto exit;
		}
	}
	argc -= optind;
	argv += optind;

	if ((PURPOSE == DRILL_CHASE || (PURPOSE == DRILL_TRACE && qdnssec)) &&
			ldns_rr_list_rr_count(key_list) == 0) {

		(void) read_key_file(LDNS_TRUST_ANCHOR_FILE, key_list, true);
	}
	if (ldns_rr_list_rr_count(key_list) > 0) {
		printf(";; Number of trusted keys: %d\n",
				(int) ldns_rr_list_rr_count(key_list));
	}
	/* do a secure trace when requested */
	if (PURPOSE == DRILL_TRACE && qdnssec) {
#ifdef HAVE_SSL
		if (ldns_rr_list_rr_count(key_list) == 0) {
			warning("%s", "No trusted keys were given. Will not be able to verify authenticity!");
		}
		PURPOSE = DRILL_SECTRACE;
#else
		fprintf(stderr, "ldns has not been compiled with OpenSSL support. Secure trace not available\n");
		exit(1);
#endif /* HAVE_SSL */
	}

	/* parse the arguments, with multiple arguments, the last argument
	 * found is used */
	for(i = 0; i < argc; i++) {

		/* if ^@ then it's a server */
		if (argv[i][0] == '@') {
			if (strlen(argv[i]) == 1) {
				warning("%s", "No nameserver given");
				exit(EXIT_FAILURE);
			}
			serv = argv[i] + 1;
			continue;
		}
		/* if has a dot, it's a name */
		if (strchr(argv[i], '.')) {
			name = argv[i];
			continue;
		}
		/* if it matches a type, it's a type */
		if (int_type == -1) {
			type = ldns_get_rr_type_by_name(argv[i]);
			if (type != 0) {
				int_type = 0;
				continue;
			} else if (is_ixfr_with_serial(argv[i], &serial)) {
				type = LDNS_RR_TYPE_IXFR;
				int_type = 0;
				continue;
			}
		}
		/* if it matches a class, it's a class */
		if (int_clas == -1) {
			clas = ldns_get_rr_class_by_name(argv[i]);
			if (clas != 0) {
				int_clas = 0;
				continue;
			}
		}
		/* it all fails assume it's a name */
		name = argv[i];
	}
	/* act like dig and use for . NS */
	if (!name) {
		name = ".";
		int_type = 0;
		type = LDNS_RR_TYPE_NS;
	}
	
	/* defaults if not given */
	if (int_clas == -1) {
		clas = LDNS_RR_CLASS_IN;
	}
	if (int_type == -1) {
		if (PURPOSE != DRILL_REVERSE) {
			type = LDNS_RR_TYPE_A;
		} else {
			type = LDNS_RR_TYPE_PTR;
		}
	}

	if (src) {
		src_rdf = ldns_rdf_new_addr_frm_str(src);
		if(!src_rdf) {
			fprintf(stderr, "-I must be a valid IP[v6] address.\n");
			exit(EXIT_FAILURE);
		}
		if (ldns_rdf_size(src_rdf) == 4) {
			qfamily = LDNS_RESOLV_INET;

		} else if (ldns_rdf_size(src_rdf) == 16) {
			qfamily = LDNS_RESOLV_INET6;
		}
	}

	/* set the nameserver to use */
	if (!serv) {
		/* no server given -- make a resolver from /etc/resolv.conf */
		status = ldns_resolver_new_frm_file(&res, resolv_conf_file);
		if (status != LDNS_STATUS_OK) {
			warning("Could not create a resolver structure: %s (%s)\n"
					"Try drill @localhost if you have a resolver running on your machine.",
				    ldns_get_errorstr_by_id(status), resolv_conf_file);
			result = EXIT_FAILURE;
			goto exit;
		}
	} else {
		res = ldns_resolver_new();
		if (!res || strlen(serv) <= 0) {
			warning("Could not create a resolver structure");
			result = EXIT_FAILURE;
			goto exit;
		}
		/* add the nameserver */
		serv_rdf = ldns_rdf_new_addr_frm_str(serv);
		if (!serv_rdf) {
			/* try to resolv the name if possible */
			status = ldns_resolver_new_frm_file(&cmdline_res, resolv_conf_file);

			if (status != LDNS_STATUS_OK) {
				error("%s", "@server ip could not be converted");
			}
			ldns_resolver_set_dnssec(cmdline_res, qdnssec);
			ldns_resolver_set_ip6(cmdline_res, qfamily);
			ldns_resolver_set_fallback(cmdline_res, qfallback);
			ldns_resolver_set_usevc(cmdline_res, qusevc);
			ldns_resolver_set_source(cmdline_res, src_rdf);

			cmdline_dname = ldns_dname_new_frm_str(serv);

			cmdline_rr_list = ldns_get_rr_list_addr_by_name(
						cmdline_res, 
						cmdline_dname,
						LDNS_RR_CLASS_IN,
						qflags);
			ldns_rdf_deep_free(cmdline_dname);
			if (!cmdline_rr_list) {
				/* This error msg is not always accurate */
				error("%s `%s\'", "could not find any address for the name:", serv);
			} else {
				if (ldns_resolver_push_nameserver_rr_list(
						res, 
						cmdline_rr_list
					) != LDNS_STATUS_OK) {
					error("%s", "pushing nameserver");
				}
			}
		} else {
			if (ldns_resolver_push_nameserver(res, serv_rdf) != LDNS_STATUS_OK) {
				error("%s", "pushing nameserver");
			} else {
				ldns_rdf_deep_free(serv_rdf);
			}
		}
	}
	/* set the resolver options */
	ldns_resolver_set_ixfr_serial(res, serial);
	ldns_resolver_set_port(res, qport);
	ldns_resolver_set_source(res, src_rdf);
	if (verbosity >= 5) {
		ldns_resolver_set_debug(res, true);
	} else {
		ldns_resolver_set_debug(res, false);
	}
	ldns_resolver_set_dnssec(res, qdnssec);
/*	ldns_resolver_set_dnssec_cd(res, qdnssec);*/
	ldns_resolver_set_ip6(res, qfamily);
	ldns_resolver_set_fallback(res, qfallback);
	ldns_resolver_set_usevc(res, qusevc);
	ldns_resolver_set_random(res, qrandom);
	if (qbuf != 0) {
		ldns_resolver_set_edns_udp_size(res, qbuf);
	}

	if (!name && 
	    PURPOSE != DRILL_AFROMFILE &&
	    !query_file
	   ) {
		usage(stdout, progname);
		result = EXIT_FAILURE;
		goto exit;
	}

	if (tsig_name && tsig_data) {
		/* With dig TSIG keys are also specified with -y,
		 * but format with drill is: -y <name:key[:algo]>
		 *             and with dig: -y [hmac:]name:key
		 *
		 * When we detect an unknown tsig algorithm in algo,
		 * but a known algorithm in name, we cane assume dig
		 * order was used.
		 *
		 * Following if statement is to anticipate and correct dig order
		 */
		if (   strcasecmp(tsig_algorithm, "hmac-md5.sig-alg.reg.int")
		    && strcasecmp(tsig_algorithm, "hmac-md5")
		    && strcasecmp(tsig_algorithm, "hmac-sha1")
		    && strcasecmp(tsig_algorithm, "hmac-sha256")
		    && (
		       strcasecmp(tsig_name, "hmac-md5.sig-alg.reg.int")  == 0
		    || strcasecmp(tsig_name, "hmac-md5")                  == 0
		    || strcasecmp(tsig_name, "hmac-sha1")                 == 0
		    || strcasecmp(tsig_name, "hmac-sha256")               == 0
		       )) {

			/* Roll options */
			char *tmp_tsig_algorithm = tsig_name;
			tsig_name      = tsig_data;
			tsig_data      = tsig_algorithm;
			tsig_algorithm = tmp_tsig_algorithm;
		}

		if (strcasecmp(tsig_algorithm, "hmac-md5") == 0) {
			free(tsig_algorithm);
			tsig_algorithm = strdup("hmac-md5.sig-alg.reg.int");
		}

		ldns_resolver_set_tsig_keyname(res, tsig_name);
		ldns_resolver_set_tsig_keydata(res, tsig_data);
		ldns_resolver_set_tsig_algorithm(res, tsig_algorithm);
	}
	
	/* main switching part of drill */
	switch(PURPOSE) {
		case DRILL_TRACE:
			/* do a trace from the root down */
			if (!global_dns_root) {
				init_root();
			}
			qname = ldns_dname_new_frm_str(name);
			if (!qname) {
				error("%s", "parsing query name");
			}
			/* don't care about return packet */
			do_trace(res, qname, type, clas);
			clear_root();
			break;
		case DRILL_SECTRACE:
			/* do a secure trace from the root down */
			if (!global_dns_root) {
				init_root();
			}
			qname = ldns_dname_new_frm_str(name);
			if (!qname) {
				error("%s", "making qname");
			}
			/* don't care about return packet */
#ifdef HAVE_SSL
			result = do_secure_trace(res, qname, type, clas, key_list, trace_start_name);
#endif /* HAVE_SSL */
			clear_root();
			break;
		case DRILL_CHASE:
			qname = ldns_dname_new_frm_str(name);
			if (!qname) {
				error("%s", "making qname");
			}
			
			ldns_resolver_set_dnssec(res, true);
			ldns_resolver_set_dnssec_cd(res, true);
			/* set dnssec implies udp_size of 4096 */
			ldns_resolver_set_edns_udp_size(res, 4096);
			pkt = NULL;
			status = ldns_resolver_query_status(
					&pkt, res, qname, type, clas, qflags);
			if (status != LDNS_STATUS_OK) {
				error("error sending query: %s",
					ldns_get_errorstr_by_id(status));
			}
			if (!pkt) {
				if (status == LDNS_STATUS_OK) {
					error("%s", "error pkt sending");
				}
				result = EXIT_FAILURE;
			} else {
				if (verbosity >= 3) {
					ldns_pkt_print(stdout, pkt);
				}
				
				if (!ldns_pkt_answer(pkt)) {
					mesg("No answer in packet");
				} else {
#ifdef HAVE_SSL
					ldns_resolver_set_dnssec_anchors(res, ldns_rr_list_clone(key_list));
					result = do_chase(res, qname, type,
					                  clas, key_list, 
					                  pkt, qflags, NULL);
					if (result == LDNS_STATUS_OK) {
						if (verbosity != -1) {
							mesg("Chase successful");
						}
						result = 0;
					} else {
						if (verbosity != -1) {
							mesg("Chase failed.");
						}
					}
#endif /* HAVE_SSL */
				}
				ldns_pkt_free(pkt);
			}
			break;
		case DRILL_AFROMFILE:
			pkt = read_hex_pkt(answer_file);
			if (pkt) {
				if (verbosity != -1) {
					ldns_pkt_print(stdout, pkt);
				}
				ldns_pkt_free(pkt);
			}
			
			break;
		case DRILL_QTOFILE:
			qname = ldns_dname_new_frm_str(name);
			if (!qname) {
				error("%s", "making qname");
			}
			status = ldns_resolver_prepare_query_pkt(&qpkt, res, qname, type, clas, qflags);
			if(status != LDNS_STATUS_OK) {
				error("%s", "making query: %s", 
					ldns_get_errorstr_by_id(status));
			}
			dump_hex(qpkt, query_file);
			ldns_pkt_free(qpkt);
			break;
		case DRILL_NSEC:
			break;
		case DRILL_REVERSE:
			/* ipv4 or ipv6 addr? */
			if (strchr(name, ':')) {
				if (!inet_pton(AF_INET6, name, &s6addr)) {
					error("Syntax error: cannot parse IPv6 address\n");
				}
				(void) snprintf(ip6_arpa_str, sizeof(ip6_arpa_str),
				    "%x.%x.%x.%x.%x.%x.%x.%x."
				    "%x.%x.%x.%x.%x.%x.%x.%x."
				    "%x.%x.%x.%x.%x.%x.%x.%x."
				    "%x.%x.%x.%x.%x.%x.%x.%x.ip6.arpa.",
				    (unsigned int)(s6addr[15] & 0x0F),
				    (unsigned int)(s6addr[15] >> 4),
				    (unsigned int)(s6addr[14] & 0x0F),
				    (unsigned int)(s6addr[14] >> 4),
				    (unsigned int)(s6addr[13] & 0x0F),
				    (unsigned int)(s6addr[13] >> 4),
				    (unsigned int)(s6addr[12] & 0x0F),
				    (unsigned int)(s6addr[12] >> 4),
				    (unsigned int)(s6addr[11] & 0x0F),
				    (unsigned int)(s6addr[11] >> 4),
				    (unsigned int)(s6addr[10] & 0x0F),
				    (unsigned int)(s6addr[10] >> 4),
				    (unsigned int)(s6addr[9] & 0x0F),
				    (unsigned int)(s6addr[9] >> 4),
				    (unsigned int)(s6addr[8] & 0x0F),
				    (unsigned int)(s6addr[8] >> 4),
				    (unsigned int)(s6addr[7] & 0x0F),
				    (unsigned int)(s6addr[7] >> 4),
				    (unsigned int)(s6addr[6] & 0x0F),
				    (unsigned int)(s6addr[6] >> 4),
				    (unsigned int)(s6addr[5] & 0x0F),
				    (unsigned int)(s6addr[5] >> 4),
				    (unsigned int)(s6addr[4] & 0x0F),
				    (unsigned int)(s6addr[4] >> 4),
				    (unsigned int)(s6addr[3] & 0x0F),
				    (unsigned int)(s6addr[3] >> 4),
				    (unsigned int)(s6addr[2] & 0x0F),
				    (unsigned int)(s6addr[2] >> 4),
				    (unsigned int)(s6addr[1] & 0x0F),
				    (unsigned int)(s6addr[1] >> 4),
				    (unsigned int)(s6addr[0] & 0x0F),
				    (unsigned int)(s6addr[0] >> 4));

				qname = ldns_dname_new_frm_str(ip6_arpa_str);
			} else {
				qname = ldns_dname_new_frm_str(name);
				qname_tmp = ldns_dname_reverse(qname);
				ldns_rdf_deep_free(qname);
				qname = qname_tmp;
				qname_tmp = ldns_dname_new_frm_str("in-addr.arpa.");
				status = ldns_dname_cat(qname, qname_tmp);
				if (status != LDNS_STATUS_OK) {
					error("%s", "could not create reverse address for ip4: %s\n", ldns_get_errorstr_by_id(status));
				}
				ldns_rdf_deep_free(qname_tmp);
			}
			if (!qname) {
				error("%s", "-x implies an ip address");
			}
			
			/* create a packet and set the RD flag on it */
			pkt = NULL;
			status = ldns_resolver_query_status(
					&pkt, res, qname, type, clas, qflags);
			if (status != LDNS_STATUS_OK) {
				error("error sending query: %s",
					ldns_get_errorstr_by_id(status));
			}
			if (!pkt)  {
				if (status == LDNS_STATUS_OK) {
					error("%s", "pkt sending");
				}
				result = EXIT_FAILURE;
			} else {
				if (verbosity != -1) {
					ldns_pkt_print(stdout, pkt);
				}
				ldns_pkt_free(pkt);
			}
			break;
		case DRILL_QUERY:
		default:
			if (query_file) {
				/* this old way, the query packet needed
				   to be parseable, but we want to be able
				   to send mangled packets, so we need
				   to do it directly */
				#if 0
				qpkt = read_hex_pkt(query_file);
				if (qpkt) {
					status = ldns_resolver_send_pkt(&pkt, res, qpkt);
					if (status != LDNS_STATUS_OK) {
						printf("Error: %s\n", ldns_get_errorstr_by_id(status));
						exit(1);
					}
				} else {
					/* qpkt was bogus, reset pkt */
					pkt = NULL;
				}
				#endif
				query_buffer = read_hex_buffer(query_file);
				if (query_buffer) {
					status = ldns_send_buffer(&pkt, res, query_buffer, NULL);
					ldns_buffer_free(query_buffer);
					if (status != LDNS_STATUS_OK) {
						printf("Error: %s\n", ldns_get_errorstr_by_id(status));
						exit(1);
					}
				} else {
					printf("NO BUFFER\n");
					pkt = NULL;
				}
			} else {
				qname = ldns_dname_new_frm_str(name);
				if (!qname) {
					error("%s", "error in making qname");
				}

				if (type == LDNS_RR_TYPE_AXFR) {
					status = ldns_axfr_start(res, qname, clas);
					if(status != LDNS_STATUS_OK) {
						error("Error starting axfr: %s", 
							ldns_get_errorstr_by_id(status));
					}
					axfr_rr = ldns_axfr_next(res);
					if(!axfr_rr) {
						fprintf(stderr, "AXFR failed.\n");
						ldns_pkt_print(stdout,
							ldns_axfr_last_pkt(res));
						goto exit;
					}
					while (axfr_rr) {
						if (verbosity != -1) {
							ldns_rr_print(stdout, axfr_rr);
						}
						ldns_rr_free(axfr_rr);
						axfr_rr = ldns_axfr_next(res);
					}

					goto exit;
				} else {
					/* create a packet and set the RD flag on it */
					pkt = NULL;
					status = ldns_resolver_query_status(
							&pkt, res, qname,
							type, clas, qflags);
					if (status != LDNS_STATUS_OK) {
						error("error sending query: %s"
						     , ldns_get_errorstr_by_id(
							     status));
					}
				}
			}
			
			if (!pkt)  {
				mesg("No packet received");
				result = EXIT_FAILURE;
			} else {
				if (verbosity != -1) {
					ldns_pkt_print(stdout, pkt);
					if (ldns_pkt_tc(pkt)) {
						fprintf(stdout,
							"\n;; WARNING: The answer packet was truncated; you might want to\n");
						fprintf(stdout,
							";; query again with TCP (-t argument), or EDNS0 (-b for buffer size)\n");
					}
				}
				if (qds) {
					if (verbosity != -1) {
						print_ds_of_keys(pkt);
						printf("\n");
					}
				}
			
				if (ldns_rr_list_rr_count(key_list) > 0) {
					/* -k's were given on the cmd line */
					ldns_rr_list *rrset_verified;
					uint16_t key_count;

					rrset_verified = ldns_pkt_rr_list_by_name_and_type(
							pkt, qname, type, 
							LDNS_SECTION_ANY_NOQUESTION);

					if (type == LDNS_RR_TYPE_ANY) {
						/* don't verify this */
						break;
					}

					if (verbosity != -1) {
						printf("; ");
						ldns_rr_list_print(stdout, rrset_verified);
					}

					/* verify */
#ifdef HAVE_SSL
					key_verified = ldns_rr_list_new();
					result = ldns_pkt_verify(pkt, type, qname, key_list, NULL, key_verified);

					if (result == LDNS_STATUS_ERR) {
						/* is the existence denied then? */
						result = ldns_verify_denial(pkt, qname, type, NULL, NULL);
						if (result == LDNS_STATUS_OK) {
							if (verbosity != -1) {
								printf("Existence denied for ");
								ldns_rdf_print(stdout, qname);
								type_str = ldns_rr_type2str(type);
								printf("\t%s\n", type_str);
								LDNS_FREE(type_str);
							}
						} else {
							if (verbosity != -1) {
								printf("Bad data; RR for name and "
								       "type not found or failed to "
								       "verify, and denial of "
								       "existence failed.\n");
							}
						}
					} else if (result == LDNS_STATUS_OK) {
						for(key_count = 0; key_count < ldns_rr_list_rr_count(key_verified);
								key_count++) {
							if (verbosity != -1) {
								printf("; VALIDATED by id = %u, owner = ",
										(unsigned int)ldns_calc_keytag(
												      ldns_rr_list_rr(key_verified, key_count)));
								ldns_rdf_print(stdout, ldns_rr_owner(
											ldns_rr_list_rr(key_list, key_count)));
								printf("\n");
							}
						}
					} else {
						for(key_count = 0; key_count < ldns_rr_list_rr_count(key_list);
								key_count++) {
							if (verbosity != -1) {
								printf("; %s for id = %u, owner = ",
								       ldns_get_errorstr_by_id(result),
								       (unsigned int)ldns_calc_keytag(
												      ldns_rr_list_rr(key_list, key_count)));
								ldns_rdf_print(stdout, ldns_rr_owner(

								ldns_rr_list_rr(key_list,
								key_count)));
								printf("\n");
							}
						}
					}
					ldns_rr_list_free(key_verified);
#else
					(void) key_count;
#endif /* HAVE_SSL */
				}
				if (answer_file) {
					dump_hex(pkt, answer_file);
				}
				ldns_pkt_free(pkt); 
			}
			
			break;
	}

	exit:
	ldns_rdf_deep_free(qname);
	ldns_rdf_deep_free(src_rdf);
	ldns_resolver_deep_free(res);
	ldns_resolver_deep_free(cmdline_res);
	ldns_rr_list_deep_free(key_list);
	ldns_rr_list_deep_free(cmdline_rr_list);
	ldns_rdf_deep_free(trace_start_name);
	xfree(progname);
	xfree(tsig_name);
	xfree(tsig_data);
	xfree(tsig_algorithm);

#ifdef HAVE_SSL
	CRYPTO_cleanup_all_ex_data();
	ERR_free_strings();
	EVP_cleanup();
#endif
#ifdef USE_WINSOCK
	WSACleanup();
#endif

	return result;
}
