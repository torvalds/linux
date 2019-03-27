#include "ipf.h"
#include <ctype.h>


typedef struct ipfopentry {
	int	ipoe_cmd;
	int	ipoe_nbasearg;
	int	ipoe_maxarg;
	int	ipoe_argsize;
	char	*ipoe_word;
} ipfopentry_t;

static ipfopentry_t opwords[17] = {
	{ IPF_EXP_IP_ADDR, 2, 0, 1, "ip.addr" },
	{ IPF_EXP_IP6_ADDR, 2, 0, 4, "ip6.addr" },
	{ IPF_EXP_IP_PR, 1, 0, 1, "ip.p" },
	{ IPF_EXP_IP_SRCADDR, 2, 0, 1, "ip.src" },
	{ IPF_EXP_IP_DSTADDR, 2, 0, 1, "ip.dst" },
	{ IPF_EXP_IP6_SRCADDR, 2, 0, 4, "ip6.src" },
	{ IPF_EXP_IP6_DSTADDR, 2, 0, 4, "ip6.dst" },
	{ IPF_EXP_TCP_PORT, 1, 0, 1, "tcp.port" },
	{ IPF_EXP_TCP_DPORT, 1, 0, 1, "tcp.dport" },
	{ IPF_EXP_TCP_SPORT, 1, 0, 1, "tcp.sport" },
	{ IPF_EXP_TCP_FLAGS, 2, 0, 1, "tcp.flags" },
	{ IPF_EXP_UDP_PORT, 1, 0, 1, "udp.port" },
	{ IPF_EXP_UDP_DPORT, 1, 0, 1, "udp.dport" },
	{ IPF_EXP_UDP_SPORT, 1, 0, 1, "udp.sport" },
	{ IPF_EXP_TCP_STATE, 1, 0, 1, "tcp.state" },
	{ IPF_EXP_IDLE_GT, 1, 1, 1, "idle-gt" },
	{ -1, 0, 0, 0, NULL  }
};


int *
parseipfexpr(line, errorptr)
	char *line;
	char **errorptr;
{
	int not, items, asize, *oplist, osize, i;
	char *temp, *arg, *s, *t, *ops, *error;
	ipfopentry_t *e;
	ipfexp_t *ipfe;

	asize = 0;
	error = NULL;
	oplist = NULL;

	temp = strdup(line);
	if (temp == NULL) {
		error = "strdup failed";
		goto parseerror;
	}

	/*
	 * Eliminate any white spaces to make parsing easier.
	 */
	for (s = temp; *s != '\0'; ) {
		if (ISSPACE(*s))
			strcpy(s, s + 1);
		else
			s++;
	}

	/*
	 * Parse the string.
	 * It should be sets of "ip.dst=1.2.3.4/32;" things.
	 * There must be a "=" or "!=" and it must end in ";".
	 */
	if (temp[strlen(temp) - 1] != ';') {
		error = "last character not ';'";
		goto parseerror;
	}

	/*
	 * Work through the list of complete operands present.
	 */
	for (ops = strtok(temp, ";"); ops != NULL; ops = strtok(NULL, ";")) {
		arg = strchr(ops, '=');
		if ((arg < ops + 2) || (arg == NULL)) {
			error = "bad 'arg' vlaue";
			goto parseerror;
		}

		if (*(arg - 1) == '!') {
			*(arg - 1) = '\0';
			not = 1;
		} else {
			not = 0;
		}
		*arg++ = '\0';


		for (e = opwords; e->ipoe_word; e++) {
			if (strcmp(ops, e->ipoe_word) == 0)
				break;
		}
		if (e->ipoe_word == NULL) {
			error = malloc(32);
			if (error != NULL) {
				sprintf(error, "keyword (%.10s) not found",
					ops);
			}
			goto parseerror;
		}

		/*
		 * Count the number of commas so we know how big to
		 * build the array
		 */
		for (s = arg, items = 1; *s != '\0'; s++)
			if (*s == ',')
				items++;

		if ((e->ipoe_maxarg != 0) && (items > e->ipoe_maxarg)) {
			error = "too many items";
			goto parseerror;
		}

		/*
		 * osize will mark the end of where we have filled up to
		 * and is thus where we start putting new data.
		 */
		osize = asize;
		asize += 4 + (items * e->ipoe_nbasearg * e->ipoe_argsize);
		if (oplist == NULL)
			oplist = calloc(asize + 2, sizeof(int));
		else
			oplist = reallocarray(oplist, asize + 2, sizeof(int));
		if (oplist == NULL) {
			error = "oplist alloc failed";
			goto parseerror;
		}
		ipfe = (ipfexp_t *)(oplist + osize);
		osize += 4;
		ipfe->ipfe_cmd = e->ipoe_cmd;
		ipfe->ipfe_not = not;
		ipfe->ipfe_narg = items * e->ipoe_nbasearg;
		ipfe->ipfe_size = items * e->ipoe_nbasearg * e->ipoe_argsize;
		ipfe->ipfe_size += 4;

		for (s = arg; (*s != '\0') && (osize < asize); s = t) {
			/*
			 * Look for the end of this arg or the ',' to say
			 * there is another following.
			 */
			for (t = s; (*t != '\0') && (*t != ','); t++)
				;
			if (*t == ',')
				*t++ = '\0';

			if (!strcasecmp(ops, "ip.addr") ||
			    !strcasecmp(ops, "ip.src") ||
			    !strcasecmp(ops, "ip.dst")) {
				i6addr_t mask, addr;
				char *delim;

				delim = strchr(s, '/');
				if (delim != NULL) {
					*delim++ = '\0';
					if (genmask(AF_INET, delim,
						    &mask) == -1) {
						error = "genmask failed";
						goto parseerror;
					}
				} else {
					mask.in4.s_addr = 0xffffffff;
				}
				if (gethost(AF_INET, s, &addr) == -1) {
					error = "gethost failed";
					goto parseerror;
				}

				oplist[osize++] = addr.in4.s_addr;
				oplist[osize++] = mask.in4.s_addr;

#ifdef USE_INET6
			} else if (!strcasecmp(ops, "ip6.addr") ||
			    !strcasecmp(ops, "ip6.src") ||
			    !strcasecmp(ops, "ip6.dst")) {
				i6addr_t mask, addr;
				char *delim;

				delim = strchr(s, '/');
				if (delim != NULL) {
					*delim++ = '\0';
					if (genmask(AF_INET6, delim,
						    &mask) == -1) {
						error = "genmask failed";
						goto parseerror;
					}
				} else {
					mask.i6[0] = 0xffffffff;
					mask.i6[1] = 0xffffffff;
					mask.i6[2] = 0xffffffff;
					mask.i6[3] = 0xffffffff;
				}
				if (gethost(AF_INET6, s, &addr) == -1) {
					error = "gethost failed";
					goto parseerror;
				}

				oplist[osize++] = addr.i6[0];
				oplist[osize++] = addr.i6[1];
				oplist[osize++] = addr.i6[2];
				oplist[osize++] = addr.i6[3];
				oplist[osize++] = mask.i6[0];
				oplist[osize++] = mask.i6[1];
				oplist[osize++] = mask.i6[2];
				oplist[osize++] = mask.i6[3];
#endif

			} else if (!strcasecmp(ops, "ip.p")) {
				int p;

				p = getproto(s);
				if (p == -1)
					goto parseerror;
				oplist[osize++] = p;

			} else if (!strcasecmp(ops, "tcp.flags")) {
				u_32_t mask, flags;
				char *delim;

				delim = strchr(s, '/');
				if (delim != NULL) {
					*delim++ = '\0';
					mask = tcpflags(delim);
				} else {
					mask = 0xff;
				}
				flags = tcpflags(s);

				oplist[osize++] = flags;
				oplist[osize++] = mask;


			} else if (!strcasecmp(ops, "tcp.port") ||
			    !strcasecmp(ops, "tcp.sport") ||
			    !strcasecmp(ops, "tcp.dport") ||
			    !strcasecmp(ops, "udp.port") ||
			    !strcasecmp(ops, "udp.sport") ||
			    !strcasecmp(ops, "udp.dport")) {
				char proto[4];
				u_short port;

				strncpy(proto, ops, 3);
				proto[3] = '\0';
				if (getport(NULL, s, &port, proto) == -1)
					goto parseerror;
				oplist[osize++] = port;

			} else if (!strcasecmp(ops, "tcp.state")) {
				oplist[osize++] = atoi(s);

			} else {
				error = "unknown word";
				goto parseerror;
			}
		}
	}

	free(temp);

	if (errorptr != NULL)
		*errorptr = NULL;

	for (i = asize; i > 0; i--)
		oplist[i] = oplist[i - 1];

	oplist[0] = asize + 2;
	oplist[asize + 1] = IPF_EXP_END;

	return oplist;

parseerror:
	if (errorptr != NULL)
		*errorptr = error;
	if (oplist != NULL)
		free(oplist);
	if (temp != NULL)
		free(temp);
	return NULL;
}
