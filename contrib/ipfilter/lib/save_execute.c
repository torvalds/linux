#include "ipf.h"
#include "ipmon.h"

static void *execute_parse __P((char **));
static void execute_destroy __P((void *));
static int execute_send __P((void *, ipmon_msg_t *));
static void execute_print __P((void *));

typedef struct execute_opts_s {
	char	*path;
} execute_opts_t;

ipmon_saver_t executesaver = {
	"execute",
	execute_destroy,
	NULL,			/* dup */
	NULL,			/* match */
	execute_parse,
	execute_print,
	execute_send
};


static void *
execute_parse(char **strings)
{
	execute_opts_t *ctx;

	ctx = calloc(1, sizeof(*ctx));

	if (ctx != NULL && strings[0] != NULL && strings[0][0] != '\0') {
		ctx->path = strdup(strings[0]);

	} else {
		free(ctx);
		return NULL;
	}

	return ctx;
}


static void
execute_print(ctx)
	void *ctx;
{
	execute_opts_t *exe = ctx;

	printf("%s", exe->path);
}


static void
execute_destroy(ctx)
	void *ctx;
{
	execute_opts_t *exe = ctx;

	if (exe != NULL)
		free(exe->path);
	free(exe);
}


static int
execute_send(ctx, msg)
	void *ctx;
	ipmon_msg_t *msg;
{
	execute_opts_t *exe = ctx;
	FILE *fp;

	fp = popen(exe->path, "w");
	if (fp != NULL) {
		fwrite(msg->imm_msg, msg->imm_msglen, 1, fp);
		pclose(fp);
	}
	return 0;
}

