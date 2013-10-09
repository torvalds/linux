#ifndef __KTAP_TYPES_H__
#define __KTAP_TYPES_H__

/* opcode is copied from lua initially */

#ifdef __KERNEL__
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/semaphore.h>
#include <linux/wait.h>
#else
typedef char u8;
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#endif

typedef struct ktap_parm {
	char *trunk; /* __user */
	int trunk_len;
	int argc;
	char **argv; /* __user */
	int verbose;
	int trace_pid;
	int workload;
	int trace_cpu;
	int print_timestamp;
} ktap_parm;

/*
 * Ioctls that can be done on a ktap fd:
 * todo: use _IO macro in include/uapi/asm-generic/ioctl.h
 */
#define KTAP_CMD_IOC_VERSION		('$' + 0)
#define KTAP_CMD_IOC_RUN		('$' + 1)
#define KTAP_CMD_IOC_EXIT		('$' + 3)

#define KTAP_ENV	"_ENV"

#define KTAP_VERSION_MAJOR       "0"
#define KTAP_VERSION_MINOR       "2"

#define KTAP_VERSION    "ktap " KTAP_VERSION_MAJOR "." KTAP_VERSION_MINOR
#define KTAP_AUTHOR    "Jovi Zhangwei <jovi.zhangwei@gmail.com>"
#define KTAP_COPYRIGHT  KTAP_VERSION "  Copyright (C) 2012-2013, " KTAP_AUTHOR

#define MYINT(s)        (s[0] - '0')
#define VERSION         (MYINT(KTAP_VERSION_MAJOR) * 16 + MYINT(KTAP_VERSION_MINOR))
#define FORMAT          0 /* this is the official format */

#define KTAP_SIGNATURE  "\033ktap"

/* data to catch conversion errors */
#define KTAPC_TAIL      "\x19\x93\r\n\x1a\n"

/* size in bytes of header of binary files */
#define KTAPC_HEADERSIZE	(sizeof(KTAP_SIGNATURE) - sizeof(char) + 2 + \
				 6 + sizeof(KTAPC_TAIL) - sizeof(char))

typedef int ktap_instruction;

typedef union ktap_gcobject ktap_gcobject;

#define CommonHeader ktap_gcobject *next; u8 tt;

struct ktap_state;
typedef int (*ktap_cfunction) (struct ktap_state *ks);

typedef union ktap_string {
	int dummy;  /* ensures maximum alignment for strings */
	struct {
		CommonHeader;
		u8 extra;  /* reserved words for short strings; "has hash" for longs */
		unsigned int hash;
		size_t len;  /* number of characters in string */
	} tsv;
} ktap_string;

#define getstr(ts)	(const char *)((ts) + 1)
#define eqshrstr(a,b)	((a) == (b))

#define svalue(o)       getstr(rawtsvalue(o))


union _ktap_value {
	ktap_gcobject *gc;    /* collectable objects */
	void *p;         /* light userdata */
	int b;           /* booleans */
	ktap_cfunction f; /* light C functions */
	long n;         /* numbers */
};


typedef struct ktap_value {
	union _ktap_value val;
	int type;
} ktap_value;

typedef ktap_value * StkId;



typedef union ktap_udata {
	struct {
		CommonHeader;
		size_t len;  /* number of bytes */
	} uv;
} ktap_udata;

/*
 * Description of an upvalue for function prototypes
 */
typedef struct ktap_upvaldesc {
	ktap_string *name;  /* upvalue name (for debug information) */
	u8 instack;  /* whether it is in stack */
	u8 idx;  /* index of upvalue (in stack or in outer function's list) */
} ktap_upvaldesc;

/*
 * Description of a local variable for function prototypes
 * (used for debug information)
 */
typedef struct ktap_locvar {
	ktap_string *varname;
	int startpc;  /* first point where variable is active */
	int endpc;    /* first point where variable is dead */
} ktap_locvar;


typedef struct ktap_upval {
	CommonHeader;
	ktap_value *v;  /* points to stack or to its own value */
	union {
		ktap_value value;  /* the value (when closed) */
		struct {  /* double linked list (when open) */
			struct ktap_upval *prev;
			struct ktap_upval *next;
		} l;
	} u;
} ktap_upval;


#define KTAP_STACK_MAX_ENTRIES 10

typedef struct ktap_btrace {
	CommonHeader;
	unsigned int nr_entries;
	unsigned long entries[KTAP_STACK_MAX_ENTRIES];
} ktap_btrace;

#define ktap_closure_header \
	CommonHeader; u8 nupvalues; ktap_gcobject *gclist

typedef struct ktap_cclosure {
	ktap_closure_header;
	ktap_cfunction f;
	ktap_value upvalue[1];  /* list of upvalues */
} ktap_cclosure;


typedef struct ktap_lclosure {
	ktap_closure_header;
	struct ktap_proto *p;
	struct ktap_upval *upvals[1];  /* list of upvalues */
} ktap_lclosure;


typedef struct ktap_closure {
	struct ktap_cclosure c;
	struct ktap_lclosure l;
} ktap_closure;


typedef struct ktap_proto {
	CommonHeader;
	ktap_value *k;  /* constants used by the function */
	ktap_instruction *code;
	struct ktap_proto **p;  /* functions defined inside the function */
	int *lineinfo;  /* map from opcodes to source lines (debug information) */
	struct ktap_locvar *locvars;  /* information about local variables (debug information) */
	struct ktap_upvaldesc *upvalues;  /* upvalue information */
	ktap_closure *cache;  /* last created closure with this prototype */
	ktap_string  *source;  /* used for debug information */
	int sizeupvalues;  /* size of 'upvalues' */
	int sizek;  /* size of `k' */
	int sizecode;
	int sizelineinfo;
	int sizep;  /* size of `p' */
	int sizelocvars;
	int linedefined;
	int lastlinedefined;
	u8 numparams;  /* number of fixed parameters */
	u8 is_vararg;
	u8 maxstacksize;  /* maximum stack used by this function */
} ktap_proto;


/*
 * information about a call
 */
typedef struct ktap_callinfo {
	StkId func;  /* function index in the stack */
	StkId top;  /* top for this function */
	struct ktap_callinfo *prev, *next;  /* dynamic call link */
	short nresults;  /* expected number of results from this function */
	u8 callstatus;
	int extra;
	union {
		struct {  /* only for Lua functions */
			StkId base;  /* base for this function */
			const unsigned int *savedpc;
		} l;
		struct {  /* only for C functions */
			int ctx;  /* context info. in case of yields */
			u8 status;
		} c;
	} u;
} ktap_callinfo;


/*
 * ktap_tables
 */
typedef union ktap_tkey {
	struct {
		union _ktap_value value_;
		int tt_;
		struct ktap_tnode *next;  /* for chaining */
	} nk;
	ktap_value tvk;
} ktap_tkey;


typedef struct ktap_tnode {
	ktap_value i_val;
	ktap_tkey i_key;
} ktap_tnode;


typedef struct ktap_table {
	CommonHeader;
#ifdef __KERNEL__
	arch_spinlock_t lock;
#endif
	u8 flags;  /* 1<<p means tagmethod(p) is not present */
	u8 lsizenode;  /* log2 of size of `node' array */
	int sizearray;  /* size of `array' array */
	ktap_value *array;  /* array part */
	ktap_tnode *node;
	ktap_tnode *lastfree;  /* any free position is before this position */
	ktap_gcobject *gclist;
} ktap_table;

#define lmod(s,size)	((int)((s) & ((size)-1)))

enum AGGREGATION_TYPE {
	AGGREGATION_TYPE_COUNT,
	AGGREGATION_TYPE_MAX,
	AGGREGATION_TYPE_MIN,
	AGGREGATION_TYPE_SUM,
	AGGREGATION_TYPE_AVG
};

typedef struct ktap_aggrtable {
	CommonHeader;
	ktap_table **pcpu_tbl;
	ktap_gcobject *gclist;
} ktap_aggrtable;

typedef struct ktap_aggraccval {
	CommonHeader;
	int type;
	int val;
	int more;
} ktap_aggraccval;

typedef struct ktap_stringtable {
	ktap_gcobject **hash;
	int nuse;
	int size;
} ktap_stringtable;

typedef struct ktap_global_state {
	ktap_stringtable strt;  /* hash table for strings */
	ktap_value registry;
	unsigned int seed; /* randonized seed for hashes */
	u8 gcstate; /* state of garbage collector */
	u8 gckind; /* kind of GC running */
	u8 gcrunning; /* true if GC is running */

	ktap_gcobject *allgc; /* list of all collectable objects */

	ktap_upval uvhead; /* head of double-linked list of all open upvalues */

	struct ktap_state *mainthread;
#ifdef __KERNEL__
	ktap_parm *parm;
	pid_t trace_pid;
	struct task_struct *trace_task;
	cpumask_var_t cpumask;
	struct ring_buffer *buffer;
	struct dentry *trace_pipe_dentry;
	int nr_builtin_cfunction;
	ktap_value *cfunction_tbl;
	struct task_struct *task;
	int trace_enabled;
	struct list_head timers;
	struct list_head probe_events_head;
	int exit;
	int wait_user;
	ktap_closure *trace_end_closure;
#endif
	int error;
} ktap_global_state;

typedef struct ktap_state {
	CommonHeader;
	u8 status;
	ktap_global_state *g;
	int stop;
	StkId top;
	ktap_callinfo *ci;
	const unsigned long *oldpc;
	StkId stack_last;
	StkId stack;
	int stacksize;
	ktap_gcobject *openupval;
	ktap_callinfo baseci;

	int debug;
	int version;
	int gcrunning;

	/* list of temp collectable objects, free when thread exit */
	ktap_gcobject *gclist;

#ifdef __KERNEL__
	struct ktap_event *current_event;
	int aggr_accval; /* for temp value storage */
#endif
} ktap_state;


typedef struct gcheader {
	CommonHeader;
} gcheader;

/*
 * Union of all collectable objects
 */
union ktap_gcobject {
	gcheader gch;  /* common header */
	union ktap_string ts;
	union ktap_udata u;
	struct ktap_closure cl;
	struct ktap_table h;
	struct ktap_aggrtable ah;
	struct ktap_aggraccval acc;
	struct ktap_proto p;
	struct ktap_upval uv;
	struct ktap_state th;  /* thread */
 	struct ktap_btrace bt;  /* thread */
};

#define gch(o)	(&(o)->gch)
/* macros to convert a GCObject into a specific value */
#define rawgco2ts(o)	(&((o)->ts))
#define gco2ts(o)       (&rawgco2ts(o)->tsv)

#define gco2uv(o)	(&((o)->uv))

#define obj2gco(v)	((ktap_gcobject *)(v))


#ifdef __KERNEL__
#define ktap_assert(s)
#else
#define ktap_assert(s)
#if 0
#define ktap_assert(s)	\
	do {	\
		if (!s) {	\
			printf("assert failed %s, %d\n", __func__, __LINE__);\
			exit(0);	\
		}	\
	} while(0)
#endif
#endif

#define check_exp(c,e)                (e)


typedef int ktap_number;


#define ktap_number2int(i,n)   ((i)=(int)(n))


/* predefined values in the registry */
#define KTAP_RIDX_MAINTHREAD     1
#define KTAP_RIDX_GLOBALS        2
#define KTAP_RIDX_LAST           KTAP_RIDX_GLOBALS


#define KTAP_TNONE		(-1)

#define KTAP_TNIL		0
#define KTAP_TBOOLEAN		1
#define KTAP_TLIGHTUSERDATA	2
#define KTAP_TNUMBER		3
#define KTAP_TSTRING		4
#define KTAP_TSHRSTR		(KTAP_TSTRING | (0 << 4))  /* short strings */
#define KTAP_TLNGSTR		(KTAP_TSTRING | (1 << 4))  /* long strings */
#define KTAP_TTABLE		5
#define KTAP_TFUNCTION		6
#define KTAP_TLCL		(KTAP_TFUNCTION | (0 << 4))  /* closure */
#define KTAP_TLCF		(KTAP_TFUNCTION | (1 << 4))  /* light C function */
#define KTAP_TCCL		(KTAP_TFUNCTION | (2 << 4))  /* C closure */
#define KTAP_TUSERDATA		7
#define KTAP_TTHREAD		8

#define KTAP_NUMTAGS		9

#define KTAP_TPROTO		11
#define KTAP_TUPVAL		12

#define KTAP_TEVENT		13

#define KTAP_TBTRACE		14

#define KTAP_TAGGRTABLE		15
#define KTAP_TAGGRACCVAL	16
#define KTAP_TAGGRVAL		17

#define ttype(o)	((o->type) & 0x3F)
#define settype(obj, t)	((obj)->type = (t))



/* raw type tag of a TValue */
#define rttype(o)       ((o)->type)

/* tag with no variants (bits 0-3) */
#define novariant(x)    ((x) & 0x0F)

/* type tag of a TValue with no variants (bits 0-3) */
#define ttypenv(o)      (novariant(rttype(o)))

#define val_(o)		((o)->val)

#define bvalue(o)	(val_(o).b)
#define nvalue(o)	(val_(o).n)
#define hvalue(o)	(&val_(o).gc->h)
#define ahvalue(o)	(&val_(o).gc->ah)
#define aggraccvalue(o)	(&val_(o).gc->acc)
#define CLVALUE(o)	(&val_(o).gc->cl.l)
#define clcvalue(o)	(&val_(o).gc->cl.c)
#define clvalue(o)	(&val_(o).gc->cl)
#define rawtsvalue(o)	(&val_(o).gc->ts)
#define pvalue(o)	(&val_(o).p)
#define fvalue(o)	(val_(o).f)
#define rawuvalue(o)	(&val_(o).gc->u)
#define uvalue(o)	(&rawuvalue(o)->uv)
#define evalue(o)	(val_(o).p)
#define btvalue(o)	(&val_(o).gc->bt)

#define gcvalue(o)	(val_(o).gc)

#define isnil(o)	(o->type == KTAP_TNIL)
#define isboolean(o)	(o->type == KTAP_TBOOLEAN)
#define isfalse(o)	(isnil(o) || (isboolean(o) && bvalue(o) == 0))

#define ttisshrstring(o)	((o)->type == KTAP_TSHRSTR)
#define ttisstring(o)		(((o)->type & 0x0F) == KTAP_TSTRING)
#define ttisnumber(o)		((o)->type == KTAP_TNUMBER)
#define ttisfunc(o)		((o)->type == KTAP_TFUNCTION)
#define ttistable(o)		((o)->type == KTAP_TTABLE)
#define ttisaggrtable(o)	((o)->type == KTAP_TAGGRTABLE)
#define ttisaggrval(o)		((o)->type == KTAP_TAGGRVAL)
#define ttisaggracc(o)		((o)->type == KTAP_TAGGRACCVAL)
#define ttisnil(o)		((o)->type == KTAP_TNIL)
#define ttisboolean(o)		((o)->type == KTAP_TBOOLEAN)
#define ttisequal(o1,o2)        ((o1)->type == (o2)->type)
#define ttisevent(o)		((o)->type == KTAP_TEVENT)
#define ttisbtrace(o)		((o)->type == KTAP_TBTRACE)

#define ttisclone(o)		ttisbtrace(o)


#define setnilvalue(obj) \
	{ ktap_value *io = (obj); io->val.n = 0; settype(io, KTAP_TNIL); }

#define setbvalue(obj, x) \
	{ ktap_value *io = (obj); io->val.b = (x); settype(io, KTAP_TBOOLEAN); }

#define setnvalue(obj, x) \
	{ ktap_value *io = (obj); io->val.n = (x); settype(io, KTAP_TNUMBER); }

#define setaggrvalue(obj, x) \
	{ ktap_value *io = (obj); io->val.n = (x); settype(io, KTAP_TAGGRVAL); }

#define setaggraccvalue(obj,x) \
	{ ktap_value *io=(obj); \
	  val_(io).gc = (ktap_gcobject *)(x); settype(io, KTAP_TAGGRACCVAL); }

#define setsvalue(obj, x) \
	{ ktap_value *io = (obj); \
	  ktap_string *x_ = (x); \
	  io->val.gc = (ktap_gcobject *)x_; settype(io, x_->tsv.tt); }

#define setcllvalue(obj, x) \
	{ ktap_value *io = (obj); \
	  io->val.gc = (ktap_gcobject *)x; settype(io, KTAP_TLCL); }

#define sethvalue(obj,x) \
	{ ktap_value *io=(obj); \
	  val_(io).gc = (ktap_gcobject *)(x); settype(io, KTAP_TTABLE); }

#define setahvalue(obj,x) \
	{ ktap_value *io=(obj); \
	  val_(io).gc = (ktap_gcobject *)(x); settype(io, KTAP_TAGGRTABLE); }

#define setfvalue(obj,x) \
	{ ktap_value *io=(obj); val_(io).f=(x); settype(io, KTAP_TLCF); }

#define setthvalue(L,obj,x) \
	{ ktap_value *io=(obj); \
	  val_(io).gc = (ktap_gcobject *)(x); settype(io, KTAP_TTHREAD); }

#define setevalue(obj, x) \
	{ ktap_value *io=(obj); val_(io).p = (x); settype(io, KTAP_TEVENT); }

#define setbtvalue(obj,x) \
	{ ktap_value *io=(obj); \
	  val_(io).gc = (ktap_gcobject *)(x); settype(io, KTAP_TBTRACE); }

#define setobj(obj1,obj2) \
        { const ktap_value *io2=(obj2); ktap_value *io1=(obj1); \
          io1->val = io2->val; io1->type = io2->type; }

#define rawequalobj(t1, t2) \
	(ttisequal(t1, t2) && kp_equalobjv(NULL, t1, t2))

#define equalobj(ks, t1, t2) rawequalobj(t1, t2)

#define incr_top(ks) {ks->top++;}

#define NUMADD(a, b)    ((a) + (b))
#define NUMSUB(a, b)    ((a) - (b))
#define NUMMUL(a, b)    ((a) * (b))
#define NUMDIV(a, b)    ((a) / (b))
#define NUMUNM(a)       (-(a))
#define NUMEQ(a, b)     ((a) == (b))
#define NUMLT(a, b)     ((a) < (b))
#define NUMLE(a, b)     ((a) <= (b))
#define NUMISNAN(a)     (!NUMEQ((a), (a)))

/* todo: floor and pow in kernel */
#define NUMMOD(a, b)    ((a) % (b))
#define NUMPOW(a, b)    (pow(a, b))


ktap_string *kp_tstring_newlstr(ktap_state *ks, const char *str, size_t l);
ktap_string *kp_tstring_newlstr_local(ktap_state *ks, const char *str, size_t l);
ktap_string *kp_tstring_new(ktap_state *ks, const char *str);
ktap_string *kp_tstring_new_local(ktap_state *ks, const char *str);
int kp_tstring_eqstr(ktap_string *a, ktap_string *b);
unsigned int kp_string_hash(const char *str, size_t l, unsigned int seed);
int kp_tstring_eqlngstr(ktap_string *a, ktap_string *b);
int kp_tstring_cmp(const ktap_string *ls, const ktap_string *rs);
void kp_tstring_resize(ktap_state *ks, int newsize);
void kp_tstring_freeall(ktap_state *ks);

ktap_value *kp_table_set(ktap_state *ks, ktap_table *t, const ktap_value *key);
ktap_table *kp_table_new(ktap_state *ks);
const ktap_value *kp_table_getint(ktap_table *t, int key);
void kp_table_setint(ktap_state *ks, ktap_table *t, int key, ktap_value *v);
const ktap_value *kp_table_get(ktap_table *t, const ktap_value *key);
void kp_table_setvalue(ktap_state *ks, ktap_table *t, const ktap_value *key, ktap_value *val);
void kp_table_resize(ktap_state *ks, ktap_table *t, int nasize, int nhsize);
void kp_table_resizearray(ktap_state *ks, ktap_table *t, int nasize);
void kp_table_free(ktap_state *ks, ktap_table *t);
int kp_table_length(ktap_state *ks, ktap_table *t);
void kp_table_dump(ktap_state *ks, ktap_table *t);
void kp_table_clear(ktap_state *ks, ktap_table *t);
void kp_table_histogram(ktap_state *ks, ktap_table *t);
int kp_table_next(ktap_state *ks, ktap_table *t, StkId key);
void kp_table_atomic_inc(ktap_state *ks, ktap_table *t, ktap_value *key, int n);
void kp_aggraccval_dump(ktap_state *ks, ktap_aggraccval *acc);
ktap_aggrtable *kp_aggrtable_new(ktap_state *ks);
ktap_table *kp_aggrtable_synthesis(ktap_state *ks, ktap_aggrtable *ah);
void kp_aggrtable_dump(ktap_state *ks, ktap_aggrtable *ah);
void kp_aggrtable_free(ktap_state *ks, ktap_aggrtable *ah);
void kp_aggrtable_set(ktap_state *ks, ktap_aggrtable *ah,
			ktap_value *key, ktap_value *val);
void kp_aggrtable_get(ktap_state *ks, ktap_aggrtable *ah,
			ktap_value *key, ktap_value *val);
void kp_aggrtable_histogram(ktap_state *ks, ktap_aggrtable *ah);
void kp_obj_dump(ktap_state *ks, const ktap_value *v);
void kp_showobj(ktap_state *ks, const ktap_value *v);
int kp_objlen(ktap_state *ks, const ktap_value *rb);
void kp_objclone(ktap_state *ks, const ktap_value *o, ktap_value *newo,
		 ktap_gcobject **list);
ktap_gcobject *kp_newobject(ktap_state *ks, int type, size_t size, ktap_gcobject **list);
int kp_equalobjv(ktap_state *ks, const ktap_value *t1, const ktap_value *t2);
ktap_closure *kp_newlclosure(ktap_state *ks, int n);
ktap_proto *kp_newproto(ktap_state *ks);
ktap_upval *kp_newupval(ktap_state *ks);
void kp_free_gclist(ktap_state *ks, ktap_gcobject *o);
void kp_free_all_gcobject(ktap_state *ks);
void kp_header(u8 *h);

int kp_str2d(const char *s, size_t len, ktap_number *result);

#define kp_realloc(ks, v, osize, nsize, t) \
	((v) = (t *)kp_reallocv(ks, v, osize * sizeof(t), nsize * sizeof(t)))

#define kp_error(ks, args...) \
	do { \
		kp_printf(ks, "error: "args);	\
		G(ks)->error = 1; \
		kp_exit(ks);	\
	} while(0)

#ifdef __KERNEL__
#define G(ks)   (ks->g)

void *kp_malloc(ktap_state *ks, int size);
void kp_free(ktap_state *ks, void *addr);
void *kp_reallocv(ktap_state *ks, void *addr, int oldsize, int newsize);
void *kp_zalloc(ktap_state *ks, int size);

void kp_printf(ktap_state *ks, const char *fmt, ...);
extern void __kp_puts(ktap_state *ks, const char *str);
extern void __kp_bputs(ktap_state *ks, const char *str);

#define kp_puts(ks, str) ({						\
	static const char *trace_printk_fmt				\
		__attribute__((section("__trace_printk_fmt"))) =	\
		__builtin_constant_p(str) ? str : NULL;			\
									\
	if (__builtin_constant_p(str))					\
		__kp_bputs(ks, trace_printk_fmt);		\
	else								\
		__kp_puts(ks, str);		\
})

#else
/*
 * this is used for ktapc tstring operation, tstring need G(ks)->strt
 * and G(ks)->seed, so ktapc need to init those field
 */
#define G(ks)   (&dummy_global_state)
extern ktap_global_state dummy_global_state;

#define kp_malloc(ks, size)			malloc(size)
#define kp_free(ks, block)			free(block)
#define kp_reallocv(ks, block, osize, nsize)	realloc(block, nsize)
#define kp_printf(ks, args...)			printf(args)
#define kp_puts(ks, str)			printf("%s", str)
#define kp_exit(ks)				exit(EXIT_FAILURE)
#endif

#define __maybe_unused	__attribute__((unused))

/*
 * KTAP_QL describes how error messages quote program elements.
 * CHANGE it if you want a different appearance.
 */
#define KTAP_QL(x)      "'" x "'"
#define KTAP_QS         KTAP_QL("%s")

#endif /* __KTAP_TYPES_H__ */

