/* vi: set sw=4 ts=4: */
/*
 * Rewrite by Russ Dill <Russ.Dill@asu.edu> July 2001
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "common.h"

#if defined CONFIG_UDHCP_DEBUG && CONFIG_UDHCP_DEBUG >= 1
unsigned dhcp_verbose;
#endif

const uint8_t MAC_BCAST_ADDR[6] ALIGN2 = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

#if ENABLE_UDHCPC || ENABLE_UDHCPD
/* Supported options are easily added here.
 * See RFC2132 for more options.
 * OPTION_REQ: these options are requested by udhcpc (unless -o).
 */
const struct dhcp_optflag dhcp_optflags[] = {
	/* flags                                    code */
	{ OPTION_IP                   | OPTION_REQ, 0x01 }, /* DHCP_SUBNET        */
	{ OPTION_S32                              , 0x02 }, /* DHCP_TIME_OFFSET   */
	{ OPTION_IP | OPTION_LIST     | OPTION_REQ, 0x03 }, /* DHCP_ROUTER        */
//	{ OPTION_IP | OPTION_LIST                 , 0x04 }, /* DHCP_TIME_SERVER   */
//	{ OPTION_IP | OPTION_LIST                 , 0x05 }, /* DHCP_NAME_SERVER   */
	{ OPTION_IP | OPTION_LIST     | OPTION_REQ, 0x06 }, /* DHCP_DNS_SERVER    */
//	{ OPTION_IP | OPTION_LIST                 , 0x07 }, /* DHCP_LOG_SERVER    */
//	{ OPTION_IP | OPTION_LIST                 , 0x08 }, /* DHCP_COOKIE_SERVER */
	{ OPTION_IP | OPTION_LIST                 , 0x09 }, /* DHCP_LPR_SERVER    */
	{ OPTION_STRING_HOST          | OPTION_REQ, 0x0c }, /* DHCP_HOST_NAME     */
	{ OPTION_U16                              , 0x0d }, /* DHCP_BOOT_SIZE     */
	{ OPTION_STRING_HOST          | OPTION_REQ, 0x0f }, /* DHCP_DOMAIN_NAME   */
	{ OPTION_IP                               , 0x10 }, /* DHCP_SWAP_SERVER   */
	{ OPTION_STRING                           , 0x11 }, /* DHCP_ROOT_PATH     */
	{ OPTION_U8                               , 0x17 }, /* DHCP_IP_TTL        */
	{ OPTION_U16                              , 0x1a }, /* DHCP_MTU           */
//TODO: why do we request DHCP_BROADCAST? Can't we assume that
//in the unlikely case it is different from typical N.N.255.255,
//server would let us know anyway?
	{ OPTION_IP                   | OPTION_REQ, 0x1c }, /* DHCP_BROADCAST     */
	{ OPTION_IP_PAIR | OPTION_LIST            , 0x21 }, /* DHCP_ROUTES        */
	{ OPTION_STRING_HOST                      , 0x28 }, /* DHCP_NIS_DOMAIN    */
	{ OPTION_IP | OPTION_LIST                 , 0x29 }, /* DHCP_NIS_SERVER    */
	{ OPTION_IP | OPTION_LIST     | OPTION_REQ, 0x2a }, /* DHCP_NTP_SERVER    */
	{ OPTION_IP | OPTION_LIST                 , 0x2c }, /* DHCP_WINS_SERVER   */
	{ OPTION_U32                              , 0x33 }, /* DHCP_LEASE_TIME    */
	{ OPTION_IP                               , 0x36 }, /* DHCP_SERVER_ID     */
	{ OPTION_STRING                           , 0x38 }, /* DHCP_ERR_MESSAGE   */
//TODO: must be combined with 'sname' and 'file' handling:
	{ OPTION_STRING_HOST                      , 0x42 }, /* DHCP_TFTP_SERVER_NAME */
	{ OPTION_STRING                           , 0x43 }, /* DHCP_BOOT_FILE     */
//TODO: not a string, but a set of LASCII strings:
//	{ OPTION_STRING                           , 0x4D }, /* DHCP_USER_CLASS    */
#if ENABLE_FEATURE_UDHCP_RFC3397
	{ OPTION_DNS_STRING | OPTION_LIST         , 0x77 }, /* DHCP_DOMAIN_SEARCH */
	{ OPTION_SIP_SERVERS                      , 0x78 }, /* DHCP_SIP_SERVERS   */
#endif
	{ OPTION_STATIC_ROUTES | OPTION_LIST      , 0x79 }, /* DHCP_STATIC_ROUTES */
#if ENABLE_FEATURE_UDHCP_8021Q
	{ OPTION_U16                              , 0x84 }, /* DHCP_VLAN_ID       */
	{ OPTION_U8                               , 0x85 }, /* DHCP_VLAN_PRIORITY */
#endif
	{ OPTION_STRING                           , 0xd1 }, /* DHCP_PXE_CONF_FILE */
	{ OPTION_STRING                           , 0xd2 }, /* DHCP_PXE_PATH_PREFIX */
	{ OPTION_U32                              , 0xd3 }, /* DHCP_REBOOT_TIME   */
	{ OPTION_6RD                              , 0xd4 }, /* DHCP_6RD           */
	{ OPTION_STATIC_ROUTES | OPTION_LIST      , 0xf9 }, /* DHCP_MS_STATIC_ROUTES */
	{ OPTION_STRING                           , 0xfc }, /* DHCP_WPAD          */

	/* Options below have no match in dhcp_option_strings[],
	 * are not passed to dhcpc scripts, and cannot be specified
	 * with "option XXX YYY" syntax in dhcpd config file.
	 * These entries are only used internally by udhcp[cd]
	 * to correctly encode options into packets.
	 */

	{ OPTION_IP                               , 0x32 }, /* DHCP_REQUESTED_IP  */
	{ OPTION_U8                               , 0x35 }, /* DHCP_MESSAGE_TYPE  */
	{ OPTION_U16                              , 0x39 }, /* DHCP_MAX_SIZE      */
//looks like these opts will work just fine even without these defs:
//	{ OPTION_STRING                           , 0x3c }, /* DHCP_VENDOR        */
//	/* not really a string: */
//	{ OPTION_STRING                           , 0x3d }, /* DHCP_CLIENT_ID     */
	{ 0, 0 } /* zeroed terminating entry */
};

/* Used for converting options from incoming packets to env variables
 * for udhcpc script, and for setting options for udhcpd via
 * "opt OPTION_NAME OPTION_VALUE" directives in udhcpd.conf file.
 */
/* Must match dhcp_optflags[] order */
const char dhcp_option_strings[] ALIGN1 =
	"subnet" "\0"           /* DHCP_SUBNET          */
	"timezone" "\0"         /* DHCP_TIME_OFFSET     */
	"router" "\0"           /* DHCP_ROUTER          */
//	"timesrv" "\0"          /* DHCP_TIME_SERVER     */
//	"namesrv" "\0"          /* DHCP_NAME_SERVER     */
	"dns" "\0"              /* DHCP_DNS_SERVER      */
//	"logsrv" "\0"           /* DHCP_LOG_SERVER      */
//	"cookiesrv" "\0"        /* DHCP_COOKIE_SERVER   */
	"lprsrv" "\0"           /* DHCP_LPR_SERVER      */
	"hostname" "\0"         /* DHCP_HOST_NAME       */
	"bootsize" "\0"         /* DHCP_BOOT_SIZE       */
	"domain" "\0"           /* DHCP_DOMAIN_NAME     */
	"swapsrv" "\0"          /* DHCP_SWAP_SERVER     */
	"rootpath" "\0"         /* DHCP_ROOT_PATH       */
	"ipttl" "\0"            /* DHCP_IP_TTL          */
	"mtu" "\0"              /* DHCP_MTU             */
	"broadcast" "\0"        /* DHCP_BROADCAST       */
	"routes" "\0"           /* DHCP_ROUTES          */
	"nisdomain" "\0"        /* DHCP_NIS_DOMAIN      */
	"nissrv" "\0"           /* DHCP_NIS_SERVER      */
	"ntpsrv" "\0"           /* DHCP_NTP_SERVER      */
	"wins" "\0"             /* DHCP_WINS_SERVER     */
	"lease" "\0"            /* DHCP_LEASE_TIME      */
	"serverid" "\0"         /* DHCP_SERVER_ID       */
	"message" "\0"          /* DHCP_ERR_MESSAGE     */
	"tftp" "\0"             /* DHCP_TFTP_SERVER_NAME*/
	"bootfile" "\0"         /* DHCP_BOOT_FILE       */
//	"userclass" "\0"        /* DHCP_USER_CLASS      */
#if ENABLE_FEATURE_UDHCP_RFC3397
	"search" "\0"           /* DHCP_DOMAIN_SEARCH   */
// doesn't work in udhcpd.conf since OPTION_SIP_SERVERS
// is not handled yet by "string->option" conversion code:
	"sipsrv" "\0"           /* DHCP_SIP_SERVERS     */
#endif
	"staticroutes" "\0"     /* DHCP_STATIC_ROUTES   */
#if ENABLE_FEATURE_UDHCP_8021Q
	"vlanid" "\0"           /* DHCP_VLAN_ID         */
	"vlanpriority" "\0"     /* DHCP_VLAN_PRIORITY   */
#endif
	"pxeconffile" "\0"      /* DHCP_PXE_CONF_FILE   */
	"pxepathprefix" "\0"    /* DHCP_PXE_PATH_PREFIX */
	"reboottime" "\0"       /* DHCP_REBOOT_TIME     */
	"ip6rd" "\0"            /* DHCP_6RD             */
	"msstaticroutes" "\0"   /* DHCP_MS_STATIC_ROUTES*/
	"wpad" "\0"             /* DHCP_WPAD            */
	;
#endif

/* Lengths of the option types in binary form.
 * Used by:
 * udhcp_str2optset: to determine how many bytes to allocate.
 * xmalloc_optname_optval: to estimate string length
 * from binary option length: (option[LEN] / dhcp_option_lengths[opt_type])
 * is the number of elements, multiply it by one element's string width
 * (len_of_option_as_string[opt_type]) and you know how wide string you need.
 */
const uint8_t dhcp_option_lengths[] ALIGN1 = {
	[OPTION_IP] =      4,
	[OPTION_IP_PAIR] = 8,
//	[OPTION_BOOLEAN] = 1,
	[OPTION_STRING] =  1,  /* ignored by udhcp_str2optset */
	[OPTION_STRING_HOST] = 1,  /* ignored by udhcp_str2optset */
#if ENABLE_FEATURE_UDHCP_RFC3397
	[OPTION_DNS_STRING] = 1,  /* ignored by both udhcp_str2optset and xmalloc_optname_optval */
	[OPTION_SIP_SERVERS] = 1,
#endif
	[OPTION_U8] =      1,
	[OPTION_U16] =     2,
//	[OPTION_S16] =     2,
	[OPTION_U32] =     4,
	[OPTION_S32] =     4,
	/* Just like OPTION_STRING, we use minimum length here */
	[OPTION_STATIC_ROUTES] = 5,
	[OPTION_6RD] =    12,  /* ignored by udhcp_str2optset */
	/* The above value was chosen as follows:
	 * len_of_option_as_string[] for this option is >60: it's a string of the form
	 * "32 128 ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff 255.255.255.255 ".
	 * Each additional ipv4 address takes 4 bytes in binary option and appends
	 * another "255.255.255.255 " 16-byte string. We can set [OPTION_6RD] = 4
	 * but this severely overestimates string length: instead of 16 bytes,
	 * it adds >60 for every 4 bytes in binary option.
	 * We cheat and declare here that option is in units of 12 bytes.
	 * This adds more than 60 bytes for every three ipv4 addresses - more than enough.
	 * (Even 16 instead of 12 should work, but let's be paranoid).
	 */
};


#if defined CONFIG_UDHCP_DEBUG && CONFIG_UDHCP_DEBUG >= 2
static void log_option(const char *pfx, const uint8_t *opt)
{
	if (dhcp_verbose >= 2) {
		char buf[256 * 2 + 2];
		*bin2hex(buf, (void*) (opt + OPT_DATA), opt[OPT_LEN]) = '\0';
		bb_error_msg("%s: 0x%02x %s", pfx, opt[OPT_CODE], buf);
	}
}
#else
# define log_option(pfx, opt) ((void)0)
#endif

unsigned FAST_FUNC udhcp_option_idx(const char *name, const char *option_strings)
{
	int n = index_in_strings(option_strings, name);
	if (n >= 0)
		return n;

	{
		char *buf, *d;
		const char *s;

		s = option_strings;
		while (*s)
			s += strlen(s) + 1;

		d = buf = xzalloc(s - option_strings);
		s = option_strings;
		while (!(*s == '\0' && s[1] == '\0')) {
			*d++ = (*s == '\0' ? ' ' : *s);
			s++;
		}
		bb_error_msg_and_die("unknown option '%s', known options: %s", name, buf);
	}
}

/* Get an option with bounds checking (warning, result is not aligned) */
uint8_t* FAST_FUNC udhcp_get_option(struct dhcp_packet *packet, int code)
{
	uint8_t *optionptr;
	int len;
	int rem;
	int overload = 0;
	enum {
		FILE_FIELD101  = FILE_FIELD  * 0x101,
		SNAME_FIELD101 = SNAME_FIELD * 0x101,
	};

	/* option bytes: [code][len][data1][data2]..[dataLEN] */
	optionptr = packet->options;
	rem = sizeof(packet->options);
	while (1) {
		if (rem <= 0) {
 complain:
			bb_error_msg("bad packet, malformed option field");
			return NULL;
		}

		/* DHCP_PADDING and DHCP_END have no [len] byte */
		if (optionptr[OPT_CODE] == DHCP_PADDING) {
			rem--;
			optionptr++;
			continue;
		}
		if (optionptr[OPT_CODE] == DHCP_END) {
			if ((overload & FILE_FIELD101) == FILE_FIELD) {
				/* can use packet->file, and didn't look at it yet */
				overload |= FILE_FIELD101; /* "we looked at it" */
				optionptr = packet->file;
				rem = sizeof(packet->file);
				continue;
			}
			if ((overload & SNAME_FIELD101) == SNAME_FIELD) {
				/* can use packet->sname, and didn't look at it yet */
				overload |= SNAME_FIELD101; /* "we looked at it" */
				optionptr = packet->sname;
				rem = sizeof(packet->sname);
				continue;
			}
			break;
		}

		if (rem <= OPT_LEN)
			goto complain; /* complain and return NULL */
		len = 2 + optionptr[OPT_LEN];
		rem -= len;
		if (rem < 0)
			goto complain; /* complain and return NULL */

		if (optionptr[OPT_CODE] == code) {
			log_option("option found", optionptr);
			return optionptr + OPT_DATA;
		}

		if (optionptr[OPT_CODE] == DHCP_OPTION_OVERLOAD) {
			if (len >= 3)
				overload |= optionptr[OPT_DATA];
			/* fall through */
		}
		optionptr += len;
	}

	/* log3 because udhcpc uses it a lot - very noisy */
	log3("option 0x%02x not found", code);
	return NULL;
}

/* Return the position of the 'end' option (no bounds checking) */
int FAST_FUNC udhcp_end_option(uint8_t *optionptr)
{
	int i = 0;

	while (optionptr[i] != DHCP_END) {
		if (optionptr[i] != DHCP_PADDING)
			i += optionptr[i + OPT_LEN] + OPT_DATA-1;
		i++;
	}
	return i;
}

/* Add an option (supplied in binary form) to the options.
 * Option format: [code][len][data1][data2]..[dataLEN]
 */
void FAST_FUNC udhcp_add_binary_option(struct dhcp_packet *packet, uint8_t *addopt)
{
	unsigned len;
	uint8_t *optionptr = packet->options;
	unsigned end = udhcp_end_option(optionptr);

	len = OPT_DATA + addopt[OPT_LEN];
	/* end position + (option code/length + addopt length) + end option */
	if (end + len + 1 >= DHCP_OPTIONS_BUFSIZE) {
//TODO: learn how to use overflow option if we exhaust packet->options[]
		bb_error_msg("option 0x%02x did not fit into the packet",
				addopt[OPT_CODE]);
		return;
	}
	log_option("adding option", addopt);
	memcpy(optionptr + end, addopt, len);
	optionptr[end + len] = DHCP_END;
}

#if ENABLE_UDHCPC || ENABLE_UDHCPD
/* Add an one to four byte option to a packet */
void FAST_FUNC udhcp_add_simple_option(struct dhcp_packet *packet, uint8_t code, uint32_t data)
{
	const struct dhcp_optflag *dh;

	for (dh = dhcp_optflags; dh->code; dh++) {
		if (dh->code == code) {
			uint8_t option[6], len;

			option[OPT_CODE] = code;
			len = dhcp_option_lengths[dh->flags & OPTION_TYPE_MASK];
			option[OPT_LEN] = len;
			if (BB_BIG_ENDIAN)
				data <<= 8 * (4 - len);
			/* Assignment is unaligned! */
			move_to_unaligned32(&option[OPT_DATA], data);
			udhcp_add_binary_option(packet, option);
			return;
		}
	}

	bb_error_msg("can't add option 0x%02x", code);
}
#endif

/* Find option 'code' in opt_list */
struct option_set* FAST_FUNC udhcp_find_option(struct option_set *opt_list, uint8_t code)
{
	while (opt_list && opt_list->data[OPT_CODE] < code)
		opt_list = opt_list->next;

	if (opt_list && opt_list->data[OPT_CODE] == code)
		return opt_list;
	return NULL;
}

/* Parse string to IP in network order */
int FAST_FUNC udhcp_str2nip(const char *str, void *arg)
{
	len_and_sockaddr *lsa;

	lsa = host_and_af2sockaddr(str, 0, AF_INET);
	if (!lsa)
		return 0;
	/* arg maybe unaligned */
	move_to_unaligned32((uint32_t*)arg, lsa->u.sin.sin_addr.s_addr);
	free(lsa);
	return 1;
}

/* udhcp_str2optset:
 * Parse string option representation to binary form and add it to opt_list.
 * Called to parse "udhcpc -x OPTNAME:OPTVAL"
 * and to parse udhcpd.conf's "opt OPTNAME OPTVAL" directives.
 */
/* helper: add an option to the opt_list */
#if !ENABLE_UDHCPC6
#define attach_option(opt_list, optflag, buffer, length, dhcpv6) \
	attach_option(opt_list, optflag, buffer, length)
#endif
static NOINLINE void attach_option(
		struct option_set **opt_list,
		const struct dhcp_optflag *optflag,
		char *buffer,
		int length,
		bool dhcpv6)
{
	IF_NOT_UDHCPC6(bool dhcpv6 = 0;)
	struct option_set *existing;
	char *allocated = NULL;

	if ((optflag->flags & OPTION_TYPE_MASK) == OPTION_BIN) {
		const char *end;
		allocated = xstrdup(buffer); /* more than enough */
		end = hex2bin(allocated, buffer, 255);
		if (errno)
			bb_error_msg_and_die("malformed hex string '%s'", buffer);
		length = end - allocated;
	}
#if ENABLE_FEATURE_UDHCP_RFC3397
	if ((optflag->flags & OPTION_TYPE_MASK) == OPTION_DNS_STRING) {
		/* reuse buffer and length for RFC1035-formatted string */
		allocated = buffer = (char *)dname_enc(NULL, 0, buffer, &length);
	}
#endif

	existing = udhcp_find_option(*opt_list, optflag->code);
	if (!existing) {
		struct option_set *new, **curr;

		/* make a new option */
		log2("attaching option %02x to list", optflag->code);
		new = xmalloc(sizeof(*new));
		if (!dhcpv6) {
			new->data = xmalloc(length + OPT_DATA);
			new->data[OPT_CODE] = optflag->code;
			new->data[OPT_LEN] = length;
			memcpy(new->data + OPT_DATA, (allocated ? allocated : buffer),
					length);
		} else {
			new->data = xmalloc(length + D6_OPT_DATA);
			new->data[D6_OPT_CODE] = optflag->code >> 8;
			new->data[D6_OPT_CODE + 1] = optflag->code & 0xff;
			new->data[D6_OPT_LEN] = length >> 8;
			new->data[D6_OPT_LEN + 1] = length & 0xff;
			memcpy(new->data + D6_OPT_DATA, (allocated ? allocated : buffer),
					length);
		}

		curr = opt_list;
		while (*curr && (*curr)->data[OPT_CODE] < optflag->code)
			curr = &(*curr)->next;

		new->next = *curr;
		*curr = new;
		goto ret;
	}

	if (optflag->flags & OPTION_LIST) {
		unsigned old_len;

		/* add it to an existing option */
		log2("attaching option %02x to existing member of list", optflag->code);
		old_len = existing->data[OPT_LEN];
		if (old_len + length < 255) {
			/* actually 255 is ok too, but adding a space can overlow it */

			existing->data = xrealloc(existing->data, OPT_DATA + 1 + old_len + length);
			if ((optflag->flags & OPTION_TYPE_MASK) == OPTION_STRING
			 || (optflag->flags & OPTION_TYPE_MASK) == OPTION_STRING_HOST
			) {
				/* add space separator between STRING options in a list */
				existing->data[OPT_DATA + old_len] = ' ';
				old_len++;
			}
			memcpy(existing->data + OPT_DATA + old_len, (allocated ? allocated : buffer), length);
			existing->data[OPT_LEN] = old_len + length;
		} /* else, ignore the data, we could put this in a second option in the future */
	} /* else, ignore the new data */

 ret:
	free(allocated);
}

int FAST_FUNC udhcp_str2optset(const char *const_str, void *arg,
		const struct dhcp_optflag *optflags, const char *option_strings,
		bool dhcpv6)
{
	struct option_set **opt_list = arg;
	char *opt;
	char *str;
	const struct dhcp_optflag *optflag;
	struct dhcp_optflag userdef_optflag;
	unsigned optcode;
	int retval;
	/* IP_PAIR needs 8 bytes, STATIC_ROUTES needs 9 max */
	char buffer[9] ALIGNED(4);
	uint16_t *result_u16 = (uint16_t *) buffer;
	uint32_t *result_u32 = (uint32_t *) buffer;

	/* Cheat, the only *const* str possible is "" */
	str = (char *) const_str;
	opt = strtok(str, " \t=:");
	if (!opt)
		return 0;

	optcode = bb_strtou(opt, NULL, 0);
	if (!errno && optcode < 255) {
		/* Raw (numeric) option code.
		 * Initially assume binary (hex-str), but if "str" or 'str'
		 * is seen later, switch to STRING.
		 */
		userdef_optflag.flags = OPTION_BIN;
		userdef_optflag.code = optcode;
		optflag = &userdef_optflag;
	} else {
		optflag = &optflags[udhcp_option_idx(opt, option_strings)];
	}

	/* Loop to handle OPTION_LIST case, else execute just once */
	retval = 0;
	do {
		int length;
		char *val;

		if (optflag->flags == OPTION_BIN) {
			val = strtok(NULL, ""); /* do not split "'q w e'" */
			trim(val);
		} else
			val = strtok(NULL, ", \t");
		if (!val)
			break;

		length = dhcp_option_lengths[optflag->flags & OPTION_TYPE_MASK];
		retval = 0;
		opt = buffer; /* new meaning for variable opt */

		switch (optflag->flags & OPTION_TYPE_MASK) {
		case OPTION_IP:
			retval = udhcp_str2nip(val, buffer);
			break;
		case OPTION_IP_PAIR:
			retval = udhcp_str2nip(val, buffer);
			val = strtok(NULL, ", \t/-");
			if (!val)
				retval = 0;
			if (retval)
				retval = udhcp_str2nip(val, buffer + 4);
			break;
case_OPTION_STRING:
		case OPTION_STRING:
		case OPTION_STRING_HOST:
#if ENABLE_FEATURE_UDHCP_RFC3397
		case OPTION_DNS_STRING:
#endif
			length = strnlen(val, 254);
			if (length > 0) {
				opt = val;
				retval = 1;
			}
			break;
//		case OPTION_BOOLEAN: {
//			static const char no_yes[] ALIGN1 = "no\0yes\0";
//			buffer[0] = retval = index_in_strings(no_yes, val);
//			retval++; /* 0 - bad; 1: "no" 2: "yes" */
//			break;
//		}
		case OPTION_U8:
			buffer[0] = bb_strtou32(val, NULL, 0);
			retval = (errno == 0);
			break;
		/* htonX are macros in older libc's, using temp var
		 * in code below for safety */
		/* TODO: use bb_strtoX? */
		case OPTION_U16: {
			uint32_t tmp = bb_strtou32(val, NULL, 0);
			*result_u16 = htons(tmp);
			retval = (errno == 0 /*&& tmp < 0x10000*/);
			break;
		}
//		case OPTION_S16: {
//			long tmp = bb_strtoi32(val, NULL, 0);
//			*result_u16 = htons(tmp);
//			retval = (errno == 0);
//			break;
//		}
		case OPTION_U32: {
			uint32_t tmp = bb_strtou32(val, NULL, 0);
			*result_u32 = htonl(tmp);
			retval = (errno == 0);
			break;
		}
		case OPTION_S32: {
			int32_t tmp = bb_strtoi32(val, NULL, 0);
			*result_u32 = htonl(tmp);
			retval = (errno == 0);
			break;
		}
		case OPTION_STATIC_ROUTES: {
			/* Input: "a.b.c.d/m" */
			/* Output: mask(1 byte),pfx(0-4 bytes),gw(4 bytes) */
			unsigned mask;
			char *slash = strchr(val, '/');
			if (slash) {
				*slash = '\0';
				retval = udhcp_str2nip(val, buffer + 1);
				buffer[0] = mask = bb_strtou(slash + 1, NULL, 10);
				val = strtok(NULL, ", \t/-");
				if (!val || mask > 32 || errno)
					retval = 0;
				if (retval) {
					length = ((mask + 7) >> 3) + 5;
					retval = udhcp_str2nip(val, buffer + (length - 4));
				}
			}
			break;
		}
		case OPTION_BIN:
			/* Raw (numeric) option code. Is it a string? */
			if (val[0] == '"' || val[0] == '\'') {
				char delim = val[0];
				char *end = last_char_is(val + 1, delim);
				if (end) {
					*end = '\0';
					val++;
					userdef_optflag.flags = OPTION_STRING;
					goto case_OPTION_STRING;
				}
			}
			/* No: hex-str option, handled in attach_option() */
			opt = val;
			retval = 1;
			break;
		default:
			break;
		}

		if (retval)
			attach_option(opt_list, optflag, opt, length, dhcpv6);
	} while (retval && (optflag->flags & OPTION_LIST));

	return retval;
}

/* note: ip is a pointer to an IPv6 in network order, possibly misaliged */
int FAST_FUNC sprint_nip6(char *dest, /*const char *pre,*/ const uint8_t *ip)
{
	char hexstrbuf[16 * 2];
	bin2hex(hexstrbuf, (void*)ip, 16);
	return sprintf(dest, /* "%s" */
		"%.4s:%.4s:%.4s:%.4s:%.4s:%.4s:%.4s:%.4s",
		/* pre, */
		hexstrbuf + 0 * 4,
		hexstrbuf + 1 * 4,
		hexstrbuf + 2 * 4,
		hexstrbuf + 3 * 4,
		hexstrbuf + 4 * 4,
		hexstrbuf + 5 * 4,
		hexstrbuf + 6 * 4,
		hexstrbuf + 7 * 4
	);
}
