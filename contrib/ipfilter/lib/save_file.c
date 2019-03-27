#include "ipf.h"
#include "ipmon.h"

static void *file_parse __P((char **));
static void file_destroy __P((void *));
static int file_send __P((void *, ipmon_msg_t *));
static void file_print __P((void *));
static int file_match __P((void *, void *));
static void *file_dup __P((void *));

typedef struct file_opts_s {
	FILE	*fp;
	int	raw;
	char	*path;
	int	ref;
} file_opts_t;

ipmon_saver_t filesaver = {
	"file",
	file_destroy,
	file_dup,
	file_match,
	file_parse,
	file_print,
	file_send
};


static void *
file_parse(strings)
	char **strings;
{
	file_opts_t *ctx;

	ctx = calloc(1, sizeof(*ctx));
	if (ctx == NULL)
		return NULL;

	if (strings[0] != NULL && strings[0][0] != '\0') {
		ctx->ref = 1;
		if (!strncmp(strings[0], "raw://", 6)) {
			ctx->raw = 1;
			ctx->path = strdup(strings[0] + 6);
			ctx->fp = fopen(ctx->path, "ab");
		} else if (!strncmp(strings[0], "file://", 7)) {
			ctx->path = strdup(strings[0] + 7);
			ctx->fp = fopen(ctx->path, "a");
		} else {
			free(ctx);
			ctx = NULL;
		}
	} else {
		free(ctx);
		ctx = NULL;
	}

	return ctx;
}


static int
file_match(ctx1, ctx2)
	void *ctx1, *ctx2;
{
	file_opts_t *f1 = ctx1, *f2 = ctx2;

	if (f1->raw != f2->raw)
		return 1;
	if (strcmp(f1->path, f2->path))
		return 1;
	return 0;
}


static void *
file_dup(ctx)
	void *ctx;
{
	file_opts_t *f = ctx;

	f->ref++;
	return f;
}


static void
file_print(ctx)
	void *ctx;
{
	file_opts_t *file = ctx;

	if (file->raw)
		printf("raw://");
	else
		printf("file://");
	printf("%s", file->path);
}


static void
file_destroy(ctx)
	void *ctx;
{
	file_opts_t *file = ctx;

	file->ref--;
	if (file->ref > 0)
		return;

	if (file->path != NULL)
		free(file->path);
	free(file);
}


static int
file_send(ctx, msg)
	void *ctx;
	ipmon_msg_t *msg;
{
	file_opts_t *file = ctx;

	if (file->raw) {
		fwrite(msg->imm_data, msg->imm_dsize, 1, file->fp);
	} else {
		fprintf(file->fp, "%s", msg->imm_msg);
	}
	return 0;
}

