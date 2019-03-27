#include "ipf.h"
#include "ipmon.h"

static void *nothing_parse __P((char **));
static void nothing_destroy __P((void *));
static int nothing_send __P((void *, ipmon_msg_t *));

typedef struct nothing_opts_s {
	FILE	*fp;
	int	raw;
	char	*path;
} nothing_opts_t;

ipmon_saver_t nothingsaver = {
	"nothing",
	nothing_destroy,
	NULL,		/* dup */
	NULL,		/* match */
	nothing_parse,
	NULL,		/* print */
	nothing_send
};


static void *
nothing_parse(char **strings)
{
	void *ctx;

#if 0
	strings = strings;	/* gcc -Wextra */
#endif

	ctx = calloc(1, sizeof(void *));

	return ctx;
}


static void
nothing_destroy(ctx)
	void *ctx;
{
	free(ctx);
}


static int
nothing_send(ctx, msg)
	void *ctx;
	ipmon_msg_t *msg;
{
#if 0
	ctx = ctx;	/* gcc -Wextra */
	msg = msg;	/* gcc -Wextra */
#endif
	/*
	 * Do nothing
	 */
	return 0;
}

