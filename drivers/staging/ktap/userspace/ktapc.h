/*
 * ktapc.h
 * only can be included by userspace compiler
 */

#include <ctype.h>

typedef int bool;
#define false 0
#define true 1

#define MAX_INT         ((int)(~0U>>1))
#define UCHAR_MAX	255

#define MAX_SIZET  ((size_t)(~(size_t)0)-2)

#define KTAP_ERRSYNTAX 3

/*
 * KTAP_IDSIZE gives the maximum size for the description of the source
 * of a function in debug information.
 * CHANGE it if you want a different size.
 */
#define KTAP_IDSIZE      60


#define FIRST_RESERVED  257

/*
 * maximum depth for nested C calls and syntactical nested non-terminals
 * in a program. (Value must fit in an unsigned short int.)
 */
#define KTAP_MAXCCALLS          200

#define KTAP_MULTRET     (-1)


#define SHRT_MAX	UCHAR_MAX

#define MAXUPVAL   UCHAR_MAX


/* maximum stack for a ktap function */
#define MAXSTACK        250

#define islalpha(c)   (isalpha(c) || (c) == '_')
#define islalnum(c)   (isalnum(c) || (c) == '_')

#define isreserved(s) ((s)->tsv.tt == KTAP_TSHRSTR && (s)->tsv.extra > 0)

#define ktap_numeq(a,b)		((a)==(b))
#define ktap_numisnan(L,a)	(!ktap_numeq((a), (a)))

#define ktap_numunm(a)		(-(a))

/*
 * ** Comparison and arithmetic functions
 * */

#define KTAP_OPADD       0       /* ORDER TM */
#define KTAP_OPSUB       1
#define KTAP_OPMUL       2
#define KTAP_OPDIV       3
#define KTAP_OPMOD       4
#define KTAP_OPPOW       5
#define KTAP_OPUNM       6

#define KTAP_OPEQ        0
#define KTAP_OPLT        1
#define KTAP_OPLE        2


/*
 * WARNING: if you change the order of this enumeration,
 * grep "ORDER RESERVED"
 */
enum RESERVED {
	/* terminal symbols denoted by reserved words */
	TK_TRACE = FIRST_RESERVED, TK_TRACE_END,
	TK_ARGEVENT, TK_ARGNAME,
	TK_ARG1, TK_ARG2, TK_ARG3, TK_ARG4, TK_ARG5, TK_ARG6, TK_ARG7, TK_ARG8,
	TK_ARG9, TK_PROFILE, TK_TICK,
	TK_AND, TK_BREAK,
	TK_DO, TK_ELSE, TK_ELSEIF, TK_END, TK_FALSE, TK_FOR, TK_FUNCTION,
	TK_GOTO, TK_IF, TK_IN, TK_LOCAL, TK_NIL, TK_NOT, TK_OR, TK_REPEAT,
	TK_RETURN, TK_THEN, TK_TRUE, TK_UNTIL, TK_WHILE,
	/* other terminal symbols */
	TK_CONCAT, TK_DOTS, TK_EQ, TK_GE, TK_LE, TK_NE, TK_INCR, TK_DBCOLON,
	TK_EOS, TK_NUMBER, TK_NAME, TK_STRING
};

/* number of reserved words */
#define NUM_RESERVED    ((int)(TK_WHILE-FIRST_RESERVED + 1))

#define EOZ     (0)                    /* end of stream */

typedef union {
	ktap_number r;
	ktap_string *ts;
} ktap_seminfo;  /* semantics information */


typedef struct ktap_token {
	int token;
	ktap_seminfo seminfo;
} ktap_token;

typedef struct ktap_mbuffer {
	char *buffer;
	size_t n;
	size_t buffsize;
} ktap_mbuffer;

#define mbuff_init(buff)	((buff)->buffer = NULL, (buff)->buffsize = 0)
#define mbuff(buff)		((buff)->buffer)
#define mbuff_reset(buff)	((buff)->n = 0, memset((buff)->buffer, 0, (buff)->buffsize))
#define mbuff_len(buff)		((buff)->n)
#define mbuff_size(buff)	((buff)->buffsize)

#define mbuff_resize(buff, size) \
	(ktapc_realloc((buff)->buffer, (buff)->buffsize, size, char), \
	(buff)->buffsize = size)

#define mbuff_free(buff)        mbuff_resize(buff, 0)


/*
 * state of the lexer plus state of the parser when shared by all
 * functions
 */
typedef struct ktap_lexstate {
	char *ptr; /* source file reading position */
	int current;  /* current character (charint) */
	int linenumber;  /* input line counter */
	int lastline;  /* line of last token `consumed' */
	ktap_token t;  /* current token */
	ktap_token lookahead;  /* look ahead token */
	struct ktap_funcstate *fs;  /* current function (parser) */
	ktap_mbuffer *buff;  /* buffer for tokens */
	struct ktap_dyndata *dyd;  /* dynamic structures used by the parser */
	ktap_string *source;  /* current source name */
	ktap_string *envn;  /* environment variable name */
	char decpoint;  /* locale decimal point */
	int nCcalls;
} ktap_lexstate;


/*
 * Expression descriptor
 */
typedef enum {
	VVOID,        /* no value */
	VNIL,
	VTRUE,
	VFALSE,
	VK,           /* info = index of constant in `k' */
	VKNUM,        /* nval = numerical value */
	VNONRELOC,    /* info = result register */
	VLOCAL,       /* info = local register */
	VUPVAL,       /* info = index of upvalue in 'upvalues' */
	VINDEXED,     /* t = table register/upvalue; idx = index R/K */
	VJMP,         /* info = instruction pc */
	VRELOCABLE,   /* info = instruction pc */
	VCALL,        /* info = instruction pc */
	VVARARG,      /* info = instruction pc */
	VEVENT,
	VEVENTNAME,
	VEVENTARG,
} expkind;


#define vkisvar(k)      (VLOCAL <= (k) && (k) <= VINDEXED)
#define vkisinreg(k)    ((k) == VNONRELOC || (k) == VLOCAL)

typedef struct ktap_expdesc {
	expkind k;
	union {
		struct {  /* for indexed variables (VINDEXED) */
			short idx;  /* index (R/K) */
			u8 t;  /* table (register or upvalue) */
			u8 vt;  /* whether 't' is register (VLOCAL) or upvalue (VUPVAL) */
		} ind;
		int info;  /* for generic use */
		ktap_number nval;  /* for VKNUM */
	} u;
	int t;  /* patch list of `exit when true' */
	int f;  /* patch list of `exit when false' */
} ktap_expdesc;


typedef struct ktap_vardesc {
	short idx;  /* variable index in stack */
} ktap_vardesc;


/* description of pending goto statements and label statements */
typedef struct ktap_labeldesc {
	ktap_string *name;  /* label identifier */
	int pc;  /* position in code */
	int line;  /* line where it appeared */
	u8 nactvar;  /* local level where it appears in current block */
} ktap_labeldesc;


/* list of labels or gotos */
typedef struct ktap_labellist {
	ktap_labeldesc *arr;  /* array */
	int n;  /* number of entries in use */
	int size;  /* array size */
} ktap_labellist;


/* dynamic structures used by the parser */
typedef struct ktap_dyndata {
	struct {  /* list of active local variables */
		ktap_vardesc *arr;
		int n;
		int size;
	} actvar;
	ktap_labellist gt;  /* list of pending gotos */
	ktap_labellist label;   /* list of active labels */
} ktap_dyndata;


/* control of blocks */
struct ktap_blockcnt;  /* defined in lparser.c */


/* state needed to generate code for a given function */
typedef struct ktap_funcstate {
	ktap_proto *f;  /* current function header */
	ktap_table *h;  /* table to find (and reuse) elements in `k' */
	struct ktap_funcstate *prev;  /* enclosing function */
	struct ktap_lexstate *ls;  /* lexical state */
	struct ktap_blockcnt *bl;  /* chain of current blocks */
	int pc;  /* next position to code (equivalent to `ncode') */
	int lasttarget;   /* 'label' of last 'jump label' */
	int jpc;  /* list of pending jumps to `pc' */
	int nk;  /* number of elements in `k' */
	int np;  /* number of elements in `p' */
	int firstlocal;  /* index of first local var (in ktap_dyndata array) */
	short nlocvars;  /* number of elements in 'f->locvars' */
	u8 nactvar;  /* number of active local variables */
	u8 nups;  /* number of upvalues */
	u8 freereg;  /* first free register */
} ktap_funcstate;


/*
 * Marks the end of a patch list. It is an invalid value both as an absolute
 * address, and as a list link (would link an element to itself).
 */
#define NO_JUMP (-1)


/*
 * grep "ORDER OPR" if you change these enums  (ORDER OP)
 */
typedef enum BinOpr {
	OPR_ADD, OPR_SUB, OPR_MUL, OPR_DIV, OPR_MOD, OPR_POW,
	OPR_CONCAT,
	OPR_EQ, OPR_LT, OPR_LE,
	OPR_NE, OPR_GT, OPR_GE,
	OPR_AND, OPR_OR,
	OPR_NOBINOPR
} BinOpr;


typedef enum UnOpr { OPR_MINUS, OPR_NOT, OPR_LEN, OPR_NOUNOPR } UnOpr;


#define getcode(fs,e)   ((fs)->f->code[(e)->u.info])

#define codegen_codeAsBx(fs,o,A,sBx)       codegen_codeABx(fs,o,A,(sBx)+MAXARG_sBx)

#define codegen_setmultret(fs,e)   codegen_setreturns(fs, e, KTAP_MULTRET)

#define codegen_jumpto(fs,t)       codegen_patchlist(fs, codegen_jump(fs), t)


#define ktapc_realloc(v, osize, nsize, t) \
        ((v) = (t *)ktapc_reallocv(v, osize * sizeof(t), nsize * sizeof(t)))

#define ktapc_reallocvector(v,oldn,n,t)	ktapc_realloc(v,oldn,n,t)


#define ktapc_growvector(v,nelems,size,t,limit,e) \
          if ((nelems)+1 > (size)) \
            ((v)=(t *)ktapc_growaux(v,&(size),sizeof(t),limit,e))


void lex_init();
ktap_string *lex_newstring(ktap_lexstate *ls, const char *str, size_t l);
const char *lex_token2str(ktap_lexstate *ls, int token);
void lex_syntaxerror(ktap_lexstate *ls, const char *msg);
void lex_setinput(ktap_lexstate *ls, char *ptr, ktap_string *source, int firstchar);
void lex_next(ktap_lexstate *ls);
int lex_lookahead(ktap_lexstate *ls);
void lex_read_string_until(ktap_lexstate *ls, int c);
ktap_closure *ktapc_parser(char *pos, const char *name);
ktap_string *ktapc_ts_new(const char *str);
int ktapc_ts_eqstr(ktap_string *a, ktap_string *b);
ktap_string *ktapc_ts_newlstr(const char *str, size_t l);
ktap_proto *ktapc_newproto();
ktap_table *ktapc_table_new();
const ktap_value *ktapc_table_get(ktap_table *t, const ktap_value *key);
void ktapc_table_setvalue(ktap_table *t, const ktap_value *key, ktap_value *val);
ktap_closure *ktapc_newlclosure(int n);
char *ktapc_sprintf(const char *fmt, ...);

void *ktapc_reallocv(void *block, size_t osize, size_t nsize);
void *ktapc_growaux(void *block, int *size, size_t size_elems, int limit,
		    const char *what);

void ktapio_exit(void);
int ktapio_create(const char *output_filename);

ktap_string *ktapc_parse_eventdef(ktap_string *eventdef);
void cleanup_event_resources(void);

extern int verbose;
#define verbose_printf(...) \
	if (verbose)	\
		printf("[verbose] " __VA_ARGS__);

#define ktapc_equalobj(t1, t2)	kp_equalobjv(NULL, t1, t2)


#include "../include/ktap_opcodes.h"

int codegen_stringK(ktap_funcstate *fs, ktap_string *s);
void codegen_indexed(ktap_funcstate *fs, ktap_expdesc *t, ktap_expdesc *k);
void codegen_setreturns(ktap_funcstate *fs, ktap_expdesc *e, int nresults);
void codegen_reserveregs(ktap_funcstate *fs, int n);
void codegen_exp2nextreg(ktap_funcstate *fs, ktap_expdesc *e);
void codegen_nil(ktap_funcstate *fs, int from, int n);
void codegen_patchlist(ktap_funcstate *fs, int list, int target);
void codegen_patchclose(ktap_funcstate *fs, int list, int level);
int codegen_jump(ktap_funcstate *fs);
void codegen_patchtohere(ktap_funcstate *fs, int list);
int codegen_codeABx(ktap_funcstate *fs, OpCode o, int a, unsigned int bc);
void codegen_ret(ktap_funcstate *fs, int first, int nret);
void codegen_exp2anyregup(ktap_funcstate *fs, ktap_expdesc *e);
void codegen_exp2val(ktap_funcstate *fs, ktap_expdesc *e);
int codegen_exp2RK(ktap_funcstate *fs, ktap_expdesc *e);
int codegen_codeABC(ktap_funcstate *fs, OpCode o, int a, int b, int c);
void codegen_setlist(ktap_funcstate *fs, int base, int nelems, int tostore);
void codegen_fixline (ktap_funcstate *fs, int line);
void codegen_dischargevars(ktap_funcstate *fs, ktap_expdesc *e);
void codegen_self(ktap_funcstate *fs, ktap_expdesc *e, ktap_expdesc *key);
void codegen_prefix(ktap_funcstate *fs, UnOpr op, ktap_expdesc *e, int line);
void codegen_infix(ktap_funcstate *fs, BinOpr op, ktap_expdesc *v);
void codegen_posfix(ktap_funcstate *fs, BinOpr op, ktap_expdesc *e1, ktap_expdesc *e2, int line);
void codegen_setoneret(ktap_funcstate *fs, ktap_expdesc *e);
void codegen_storevar(ktap_funcstate *fs, ktap_expdesc *var, ktap_expdesc *ex);
void codegen_storeincr(ktap_funcstate *fs, ktap_expdesc *var, ktap_expdesc *ex);
void codegen_goiftrue(ktap_funcstate *fs, ktap_expdesc *e);
int codegen_getlabel(ktap_funcstate *fs);
int codegen_codek(ktap_funcstate *fs, int reg, int k);
int codegen_numberK(ktap_funcstate *fs, ktap_number r);
void codegen_checkstack(ktap_funcstate *fs, int n);
void codegen_goiffalse(ktap_funcstate *fs, ktap_expdesc *e);
void codegen_concat(ktap_funcstate *fs, int *l1, int l2);
int codegen_exp2anyreg(ktap_funcstate *fs, ktap_expdesc *e);

typedef int (*ktap_writer)(const void* p, size_t sz, void* ud);
int ktapc_dump(const ktap_proto *f, ktap_writer w, void *data, int strip);

void ktapc_chunkid(char *out, const char *source, size_t bufflen);
int ktapc_str2d(const char *s, size_t len, ktap_number *result);
int ktapc_hexavalue(int c);
ktap_number ktapc_arith(int op, ktap_number v1, ktap_number v2);
int ktapc_int2fb(unsigned int x);

bool strglobmatch(const char *str, const char *pat);

