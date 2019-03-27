/* Nested struct */
typedef void *EditLine;
typedef void *History;

typedef struct {
	EditLine	*el;
	History		*hist;
} el_mode_t;

struct el_modes_s {
	el_mode_t command;
	el_mode_t string;
	el_mode_t filec;
	el_mode_t mime_enc;
};

struct el_modes_s elm = {
	.command  = { .el = 0, .hist = 0, },
	.string   = { .el = 0, .hist = 0, },
	.filec    = { .el = 0, .hist = 0, },
};
