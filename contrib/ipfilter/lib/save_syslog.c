#include "ipf.h"
#include "ipmon.h"
#include <syslog.h>

static void *syslog_parse __P((char **));
static void syslog_destroy __P((void *));
static int syslog_send __P((void *, ipmon_msg_t *));
static void syslog_print __P((void *));

typedef struct syslog_opts_s {
	int	facpri;
	int	fac;
	int	pri;
} syslog_opts_t;

ipmon_saver_t syslogsaver = {
	"syslog",
	syslog_destroy,
	NULL,			/* dup */
	NULL,			/* match */
	syslog_parse,
	syslog_print,
	syslog_send
};


static void *
syslog_parse(char **strings)
{
	syslog_opts_t *ctx;
	char *str;
	char *s;

	ctx = calloc(1, sizeof(*ctx));
	if (ctx == NULL)
		return NULL;

	ctx->facpri = -1;

	if (strings[0] != NULL && strings[0][0] != '\0') {
		str = strdup(*strings);
		if (str != NULL && *str != '\0') {
			int fac = -1, pri = -1;

			s = strchr(str, '.');
			if (s != NULL)
				*s++ = '\0';

			if (*str != '\0') {
				fac = fac_findname(str);
				if (fac == -1) {
					free(str);
					free(ctx);
					return NULL;
				}
			}

			if (s != NULL && *s != '\0') {
				pri = pri_findname(s);
				if (pri == -1) {
					free(str);
					free(ctx);
					return NULL;
				}
			}
			free(str);

			ctx->fac = fac;
			ctx->pri = pri;
			if (pri == -1)
				ctx->facpri = fac;
			else if (fac == -1)
				ctx->facpri = pri;
			else
				ctx->facpri = fac | pri;
		} else {
			if (str != NULL)
				free(str);
			free(ctx);
			ctx = NULL;
		}
	}

	return ctx;
}


static void
syslog_print(ctx)
	void *ctx;
{
	syslog_opts_t *sys = ctx;

	if (sys->facpri == -1)
		return;

	if (sys->fac == -1) {
		printf(".%s", pri_toname(sys->pri));
	} else if (sys->pri == -1) {
		printf("%s.", fac_toname(sys->fac));
	} else {
		printf("%s.%s", fac_toname(sys->facpri & LOG_FACMASK),
		       pri_toname(sys->facpri & LOG_PRIMASK));
	}
}


static void
syslog_destroy(ctx)
	void *ctx;
{
	free(ctx);
}


static int
syslog_send(ctx, msg)
	void *ctx;
	ipmon_msg_t *msg;
{
	syslog_opts_t *sys = ctx;
	int facpri;

	if (sys->facpri == -1) {
		facpri = msg->imm_loglevel;
	} else {
		if (sys->pri == -1) {
			facpri = sys->fac | (msg->imm_loglevel & LOG_PRIMASK);
		} else if (sys->fac == -1) {
			facpri = sys->pri | (msg->imm_loglevel & LOG_FACMASK);
		} else {
			facpri = sys->facpri;
		}
	}
	syslog(facpri, "%s", msg->imm_msg);
	return 0;
}
