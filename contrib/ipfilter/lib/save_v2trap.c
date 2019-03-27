#include "ipf.h"
#include "netinet/ipl.h"
#include "ipmon.h"
#include <ctype.h>

static u_char sysuptime[] = { 6, 8, 0x2b, 6, 1, 2, 1, 1, 3, 0 };
/*
 * Enterprise number OID:
 * 1.3.6.1.4.1.9932
 */
static u_char ipf_trap0_1[] = { 6, 10, 0x2b, 6, 1, 4, 1, 0xcd, 0x4c, 1, 1, 1 };
static u_char ipf_trap0_2[] = { 6, 10, 0x2b, 6, 1, 4, 1, 0xcd, 0x4c, 1, 1, 2 };

static int writeint __P((u_char *, int));
static int writelength __P((u_char *, u_int));
static int maketrap_v2 __P((char *, u_char *, int, u_char *, int));
static void snmpv2_destroy __P((void *));
static void *snmpv2_dup __P((void *));
static int snmpv2_match __P((void *, void *));
static void *snmpv2_parse __P((char **));
static void snmpv2_print __P((void *));
static int snmpv2_send __P((void *, ipmon_msg_t *));


int sendtrap_v2_0 __P((int, char *, char *, int));

static char def_community[] = "public";	/* ublic */

typedef struct snmpv2_opts_s {
	char			*community;
	char			*server;
	int			fd;
	int			v6;
	int			ref;
#ifdef USE_INET6
	struct sockaddr_in6	sin6;
#endif
	struct sockaddr_in	sin;
} snmpv2_opts_t;

ipmon_saver_t snmpv2saver = {
	"snmpv2",
	snmpv2_destroy,
	snmpv2_dup,		/* dup */
	snmpv2_match,		/* match */
	snmpv2_parse,
	snmpv2_print,
	snmpv2_send
};


static int
snmpv2_match(ctx1, ctx2)
	void *ctx1, *ctx2;
{
	snmpv2_opts_t *s1 = ctx1, *s2 = ctx2;

	if (s1->v6 != s2->v6)
		return 1;

	if (strcmp(s1->community, s2->community))
		return 1;

#ifdef USE_INET6
	if (s1->v6 == 1) {
		if (memcmp(&s1->sin6, &s2->sin6, sizeof(s1->sin6)))
			return 1;
	} else
#endif
	{
		if (memcmp(&s1->sin, &s2->sin, sizeof(s1->sin)))
			return 1;
	}

	return 0;
}


static void *
snmpv2_dup(ctx)
	void *ctx;
{
	snmpv2_opts_t *s = ctx;

	s->ref++;
	return s;
}


static void
snmpv2_print(ctx)
        void *ctx;
{
	snmpv2_opts_t *snmpv2 = ctx;

	printf("%s ", snmpv2->community);
#ifdef USE_INET6
	if (snmpv2->v6 == 1) {
		char buf[80];

		printf("%s", inet_ntop(AF_INET6, &snmpv2->sin6.sin6_addr, buf,
				       sizeof(snmpv2->sin6.sin6_addr)));
	} else
#endif
	{
		printf("%s", inet_ntoa(snmpv2->sin.sin_addr));
	}
}


static void *
snmpv2_parse(char **strings)
{
	snmpv2_opts_t *ctx;
	int result;
	char *str;
	char *s;

	if (strings[0] == NULL || strings[0][0] == '\0')
		return NULL;
	if (strchr(*strings, ' ') == NULL)
		return NULL;

	str = strdup(*strings);

	ctx = calloc(1, sizeof(*ctx));
	if (ctx == NULL) {
		free(str);
		return NULL;
	}

	ctx->fd = -1;

	s = strchr(str, ' ');
	*s++ = '\0';
	ctx->community = str;

	while (ISSPACE(*s))
		s++;
	if (!*s) {
		free(str);
		free(ctx);
		return NULL;
	}

#ifdef USE_INET6
	if (strchr(s, ':') == NULL) {
		result = inet_pton(AF_INET, s, &ctx->sin.sin_addr);
		if (result == 1) {
			ctx->fd = socket(AF_INET, SOCK_DGRAM, 0);
			if (ctx->fd >= 0) {
				ctx->sin.sin_family = AF_INET;
				ctx->sin.sin_port = htons(162);
				if (connect(ctx->fd,
					    (struct sockaddr *)&ctx->sin,
					    sizeof(ctx->sin)) != 0) {
						snmpv2_destroy(ctx);
						return NULL;
				}
			}
		}
	} else {
		result = inet_pton(AF_INET6, s, &ctx->sin6.sin6_addr);
		if (result == 1) {
			ctx->v6 = 1;
			ctx->fd = socket(AF_INET6, SOCK_DGRAM, 0);
			if (ctx->fd >= 0) {
				ctx->sin6.sin6_family = AF_INET6;
				ctx->sin6.sin6_port = htons(162);
				if (connect(ctx->fd,
					    (struct sockaddr *)&ctx->sin6,
					    sizeof(ctx->sin6)) != 0) {
						snmpv2_destroy(ctx);
						return NULL;
				}
			}
		}
	}
#else
	result = inet_aton(s, &ctx->sin.sin_addr);
	if (result == 1) {
		ctx->fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (ctx->fd >= 0) {
			ctx->sin.sin_family = AF_INET;
			ctx->sin.sin_port = htons(162);
			if (connect(ctx->fd, (struct sockaddr *)&ctx->sin,
				    sizeof(ctx->sin)) != 0) {
					snmpv2_destroy(ctx);
					return NULL;
			}
		}
	}
#endif

	if (result != 1) {
		free(str);
		free(ctx);
		return NULL;
	}

	ctx->ref = 1;

	return ctx;
}


static void
snmpv2_destroy(ctx)
	void *ctx;
{
	snmpv2_opts_t *v2 = ctx;

	v2->ref--;
	if (v2->ref > 0)
		return;

	if (v2->community)
		free(v2->community);
	if (v2->fd >= 0)
		close(v2->fd);
	free(v2);
}


static int
snmpv2_send(ctx, msg)
	void *ctx;
	ipmon_msg_t *msg;
{
	snmpv2_opts_t *v2 = ctx;

	return sendtrap_v2_0(v2->fd, v2->community,
			     msg->imm_msg, msg->imm_msglen);
}
static int
writelength(buffer, value)
	u_char *buffer;
	u_int value;
{
	u_int n = htonl(value);
	int len;

	if (value < 128) {
		*buffer = value;
		return 1;
	}
	if (value > 0xffffff)
		len = 4;
	else if (value > 0xffff)
		len = 3;
	else if (value > 0xff)
		len = 2;
	else
		len = 1;

	*buffer = 0x80 | len;

	bcopy((u_char *)&n + 4 - len, buffer + 1, len);

	return len + 1;
}


static int
writeint(buffer, value)
	u_char *buffer;
	int value;
{
	u_char *s = buffer;
	u_int n = value;

	if (value == 0) {
		*buffer = 0;
		return 1;
	}

	if (n >  4194304) {
		*s++ = 0x80 | (n / 4194304);
		n -= 4194304 * (n / 4194304);
	}
	if (n >  32768) {
		*s++ = 0x80 | (n / 32768);
		n -= 32768 * (n / 327678);
	}
	if (n > 128) {
		*s++ = 0x80 | (n / 128);
		n -= (n / 128) * 128;
	}
	*s++ = (u_char)n;

	return s - buffer;
}



/*
 * First style of traps is:
 * 1.3.6.1.4.1.9932.1.1
 */
static int
maketrap_v2(community, buffer, bufsize, msg, msglen)
	char *community;
	u_char *buffer;
	int bufsize;
	u_char *msg;
	int msglen;
{
	u_char *s = buffer, *t, *pdulen;
	u_char *varlen;
	int basesize = 77;
	u_short len;
	int trapmsglen;
	int pdulensz;
	int varlensz;
	int baselensz;
	int n;

	if (community == NULL || *community == '\0')
		community = def_community;
	basesize += strlen(community) + msglen;

	if (basesize + 8 > bufsize)
		return 0;

	memset(buffer, 0xff, bufsize);
	*s++ = 0x30;		/* Sequence */

	if (basesize - 1 >= 128) {
		baselensz = 2;
		basesize++;
	} else {
		baselensz = 1;
	}
	s += baselensz;
	*s++ = 0x02;		/* Integer32 */
	*s++ = 0x01;		/* length 1 */
	*s++ = 0x01;		/* version 2 */
	*s++ = 0x04;		/* octet string */
	*s++ = strlen(community);		/* length of "public" */
	bcopy(community, s, s[-1]);
	s += s[-1];
	*s++ = 0xA7;		/* PDU(7) */
	pdulen = s++;
	if (basesize - (s - buffer) >= 128) {
		pdulensz = 2;
		basesize++;
		s++;
	} else {
		pdulensz = 1;
	}
	/* request id */
	*s++ = 0x2;	/* integer */
	*s++ = 0x4;	/* len 4 */
	*s++ = 0x0;	/* noError */
	*s++ = 0x0;	/* noError */
	*s++ = 0x0;	/* noError */
	*s++ = 0x0;	/* noError */

	/* error status */
	*s++ = 0x2;	/* integer */
	*s++ = 0x1;	/* len 1 */
	*s++ = 0x0;	/* noError */

	/* error-index */
	*s++ = 0x2;	/* integer */
	*s++ = 0x1;	/* len 1 */
	*s++ = 0x0;	/* noError */

	*s++ = 0x30;	/* sequence */
	varlen = s++;
	if (basesize - (s - buffer) >= 128) {
		varlensz = 2;
		basesize++;
		s++;
	} else {
		varlensz = 1;
	}

	*s++ = 0x30;	/* sequence */
	*s++ = sizeof(sysuptime) + 6;

	bcopy(sysuptime, s, sizeof(sysuptime));
	s += sizeof(sysuptime);

	*s++ = 0x43;	/* Timestamp */
	*s++ = 0x04;	/* TimeTicks */
	*s++ = 0x0;
	*s++ = 0x0;
	*s++ = 0x0;
	*s++ = 0x0;

	*s++ = 0x30;
	t = s + 1;
	bcopy(ipf_trap0_1, t, sizeof(ipf_trap0_1));
	t += sizeof(ipf_trap0_1);

	*t++ = 0x2;		/* Integer */
	n = writeint(t + 1, IPFILTER_VERSION);
	*t = n;
	t += n + 1;

	len = t - s - 1;
	writelength(s, len);

	s = t;
	*s++ = 0x30;
	if (msglen < 128) {
		if (msglen + 1 + 1 + sizeof(ipf_trap0_2) >= 128)
			trapmsglen = 2;
		else
			trapmsglen = 1;
	} else {
		if (msglen + 2 + 1 + sizeof(ipf_trap0_2) >= 128)
			trapmsglen = 2;
		else
			trapmsglen = 1;
	}
	t = s + trapmsglen;
	bcopy(ipf_trap0_2, t, sizeof(ipf_trap0_2));
	t += sizeof(ipf_trap0_2);

	*t++ = 0x4;		/* Octet string */
	n = writelength(t, msglen);
	t += n;
	bcopy(msg, t, msglen);
	t += msglen;

	len = t - s - trapmsglen;
	writelength(s, len);

	len = t - varlen - varlensz;
	writelength(varlen, len);		/* pdu length */

	len = t - pdulen - pdulensz;
	writelength(pdulen, len);		/* pdu length */

	len = t - buffer - baselensz - 1;
	writelength(buffer + 1, len);	/* length of trap */

	return t - buffer;
}


int
sendtrap_v2_0(fd, community, msg, msglen)
	int fd;
	char *community, *msg;
	int msglen;
{

	u_char buffer[1500];
	int n;

	n = maketrap_v2(community, buffer, sizeof(buffer),
			(u_char *)msg, msglen);
	if (n > 0) {
		return send(fd, buffer, n, 0);
	}

	return 0;
}
