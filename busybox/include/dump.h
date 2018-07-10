/* vi: set sw=4 ts=4: */

PUSH_AND_SET_FUNCTION_VISIBILITY_TO_HIDDEN

#define	F_IGNORE	0x01		/* %_A */
#define	F_SETREP	0x02		/* rep count set, not default */
#define	F_ADDRESS	0x001		/* print offset */
#define	F_BPAD		0x002		/* blank pad */
#define	F_C		0x004		/* %_c */
#define	F_CHAR		0x008		/* %c */
#define	F_DBL		0x010		/* %[EefGf] */
#define	F_INT		0x020		/* %[di] */
#define	F_P		0x040		/* %_p */
#define	F_STR		0x080		/* %s */
#define	F_U		0x100		/* %_u */
#define	F_UINT		0x200		/* %[ouXx] */
#define	F_TEXT		0x400		/* no conversions */

enum dump_vflag_t { ALL, DUP, FIRST, WAIT };	/* -v values */

typedef struct PR {
	struct PR *nextpr;		/* next print unit */
	unsigned flags;			/* flag values */
	int bcnt;			/* byte count */
	char *cchar;			/* conversion character */
	char *fmt;			/* printf format */
	char *nospace;			/* no whitespace version */
} PR;

typedef struct FU {
	struct FU *nextfu;		/* next format unit */
	struct PR *nextpr;		/* next print unit */
	unsigned flags;			/* flag values */
	int reps;			/* repetition count */
	int bcnt;			/* byte count */
	char *fmt;			/* format string */
} FU;

typedef struct FS {			/* format strings */
	struct FS *nextfs;		/* linked list of format strings */
	struct FU *nextfu;		/* linked list of format units */
	int bcnt;
} FS;

typedef struct dumper_t {
	off_t dump_skip;                /* bytes to skip */
	int dump_length;                /* max bytes to read */
	smallint dump_vflag;            /*enum dump_vflag_t*/
	FS *fshead;
} dumper_t;

dumper_t* alloc_dumper(void) FAST_FUNC;
extern void bb_dump_add(dumper_t *dumper, const char *fmt) FAST_FUNC;
extern int bb_dump_dump(dumper_t *dumper, char **argv) FAST_FUNC;

POP_SAVED_FUNCTION_VISIBILITY
