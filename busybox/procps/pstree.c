/*
 * pstree.c - display process tree
 *
 * Copyright (C) 1993-2002 Werner Almesberger
 * Copyright (C) 2002-2009 Craig Small
 * Copyright (C) 2010 Lauri Kasanen
 *
 * Based on pstree (PSmisc) 22.13.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config PSTREE
//config:	bool "pstree (9.4 kb)"
//config:	default y
//config:	help
//config:	Display a tree of processes.

//applet:IF_PSTREE(APPLET_NOEXEC(pstree, pstree, BB_DIR_USR_BIN, BB_SUID_DROP, pstree))

//kbuild:lib-$(CONFIG_PSTREE) += pstree.o

//usage:#define pstree_trivial_usage
//usage:	"[-p] [PID|USER]"
//usage:#define pstree_full_usage "\n\n"
//usage:       "Display process tree, optionally start from USER or PID\n"
//usage:     "\n	-p	Show pids"

#include "libbb.h"

#define PROC_BASE "/proc"

#define OPT_PID  (1 << 0)

struct child;

#if ENABLE_FEATURE_SHOW_THREADS
/* For threads, we add {...} around the comm, so we need two extra bytes */
# define COMM_DISP_LEN (COMM_LEN + 2)
#else
# define COMM_DISP_LEN COMM_LEN
#endif

typedef struct proc {
	char comm[COMM_DISP_LEN + 1];
//	char flags; - unused, delete?
	pid_t pid;
	uid_t uid;
	struct child *children;
	struct proc *parent;
	struct proc *next;
} PROC;

/* For flags above */
//#define PFLAG_THREAD  0x01

typedef struct child {
	PROC *child;
	struct child *next;
} CHILD;

#define empty_2  "  "
#define branch_2 "|-"
#define vert_2   "| "
#define last_2   "`-"
#define single_3 "---"
#define first_3  "-+-"

struct globals {
	/* 0-based. IOW: the number of chars we printed on current line */
	unsigned cur_x;
	unsigned output_width;

	/* The buffers will be dynamically increased in size as needed */
	unsigned capacity;
	unsigned *width;
	uint8_t *more;

	PROC *list;

	smallint dumped; /* used by dump_by_user */
};
#define G (*ptr_to_globals)
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
} while (0)


/*
 * Allocates additional buffer space for width and more as needed.
 * The first call will allocate the first buffer.
 *
 * bufindex  the index that will be used after the call to this function.
 */
static void ensure_buffer_capacity(int bufindex)
{
	if (bufindex >= G.capacity) {
		G.capacity += 0x100;
		G.width = xrealloc(G.width, G.capacity * sizeof(G.width[0]));
		G.more = xrealloc(G.more, G.capacity * sizeof(G.more[0]));
	}
}

/* NB: this function is never called with "bad" chars
 * (control chars or chars >= 0x7f)
 */
static void out_char(char c)
{
	G.cur_x++;
	if (G.cur_x > G.output_width)
		return;
	if (G.cur_x == G.output_width)
		c = '+';
	putchar(c);
}

/* NB: this function is never called with "bad" chars
 * (control chars or chars >= 0x7f)
 */
static void out_string(const char *str)
{
	while (*str)
		out_char(*str++);
}

static void out_newline(void)
{
	putchar('\n');
	G.cur_x = 0;
}

static PROC *find_proc(pid_t pid)
{
	PROC *walk;

	for (walk = G.list; walk; walk = walk->next)
		if (walk->pid == pid)
			break;

	return walk;
}

static PROC *new_proc(const char *comm, pid_t pid, uid_t uid)
{
	PROC *new = xzalloc(sizeof(*new));

	strcpy(new->comm, comm);
	new->pid = pid;
	new->uid = uid;
	new->next = G.list;

	G.list = new;
	return G.list;
}

static void add_child(PROC *parent, PROC *child)
{
	CHILD *new, **walk;
	int cmp;

	new = xmalloc(sizeof(*new));

	new->child = child;
	for (walk = &parent->children; *walk; walk = &(*walk)->next) {
		cmp = strcmp((*walk)->child->comm, child->comm);
		if (cmp > 0)
			break;
		if (cmp == 0 && (*walk)->child->uid > child->uid)
			break;
	}
	new->next = *walk;
	*walk = new;
}

static void add_proc(const char *comm, pid_t pid, pid_t ppid,
			uid_t uid /*, char isthread*/)
{
	PROC *this, *parent;

	this = find_proc(pid);
	if (!this)
		this = new_proc(comm, pid, uid);
	else {
		strcpy(this->comm, comm);
		this->uid = uid;
	}

	if (pid == ppid)
		ppid = 0;
//	if (isthread)
//		this->flags |= PFLAG_THREAD;

	parent = find_proc(ppid);
	if (!parent)
		parent = new_proc("?", ppid, 0);

	add_child(parent, this);
	this->parent = parent;
}

static int tree_equal(const PROC *a, const PROC *b)
{
	const CHILD *walk_a, *walk_b;

	if (strcmp(a->comm, b->comm) != 0)
		return 0;
	if ((option_mask32 /*& OPT_PID*/) && a->pid != b->pid)
		return 0;

	for (walk_a = a->children, walk_b = b->children;
	  walk_a && walk_b;
	  walk_a = walk_a->next, walk_b = walk_b->next
	) {
		if (!tree_equal(walk_a->child, walk_b->child))
			return 0;
	}

	return !(walk_a || walk_b);
}

static int out_args(const char *mystr)
{
	const char *here;
	int strcount = 0;
	char tmpstr[5];

	for (here = mystr; *here; here++) {
		if (*here == '\\') {
			out_string("\\\\");
			strcount += 2;
		} else if (*here >= ' ' && *here < 0x7f) {
			out_char(*here);
			strcount++;
		} else {
			sprintf(tmpstr, "\\%03o", (unsigned char) *here);
			out_string(tmpstr);
			strcount += 4;
		}
	}

	return strcount;
}

static void
dump_tree(PROC *current, int level, int rep, int leaf, int last, int closing)
{
	CHILD *walk, *next, **scan;
	int lvl, i, add, offset, count, comm_len, first;
	char tmp[sizeof(int)*3 + 4];

	if (!current)
		return;

	if (!leaf) {
		for (lvl = 0; lvl < level; lvl++) {
			i = G.width[lvl] + 1;
			while (--i >= 0)
				out_char(' ');

			if (lvl == level - 1) {
				if (last) {
					out_string(last_2);
				} else {
					out_string(branch_2);
				}
			} else {
				if (G.more[lvl + 1]) {
					out_string(vert_2);
				} else {
					out_string(empty_2);
				}
			}
		}
	}

	add = 0;
	if (rep > 1) {
		add += sprintf(tmp, "%d*[", rep);
		out_string(tmp);
	}
	comm_len = out_args(current->comm);
	if (option_mask32 /*& OPT_PID*/) {
		comm_len += sprintf(tmp, "(%d)", (int)current->pid);
		out_string(tmp);
	}
	offset = G.cur_x;

	if (!current->children)	{
		while (closing--)
			out_char(']');
		out_newline();
	}
	ensure_buffer_capacity(level);
	G.more[level] = !last;

	G.width[level] = comm_len + G.cur_x - offset + add;
	if (G.cur_x >= G.output_width) {
		//out_string(first_3); - why? it won't print anything
		//out_char('+');
		out_newline();
		return;
	}

	first = 1;
	for (walk = current->children; walk; walk = next) {
		count = 0;
		next = walk->next;
		scan = &walk->next;
		while (*scan) {
			if (!tree_equal(walk->child, (*scan)->child))
				scan = &(*scan)->next;
			else {
				if (next == *scan)
					next = (*scan)->next;
				count++;
				*scan = (*scan)->next;
			}
		}
		if (first) {
			out_string(next ? first_3 : single_3);
			first = 0;
		}

		dump_tree(walk->child, level + 1, count + 1,
				walk == current->children, !next,
				closing + (count ? 1 : 0));
	}
}

static void dump_by_user(PROC *current, uid_t uid)
{
	const CHILD *walk;

	if (!current)
		return;

	if (current->uid == uid) {
		if (G.dumped)
			putchar('\n');
		dump_tree(current, 0, 1, 1, 1, 0);
		G.dumped = 1;
		return;
	}
	for (walk = current->children; walk; walk = walk->next)
		dump_by_user(walk->child, uid);
}

#if ENABLE_FEATURE_SHOW_THREADS
static void handle_thread(const char *comm, pid_t pid, pid_t ppid, uid_t uid)
{
	char threadname[COMM_DISP_LEN + 1];
	sprintf(threadname, "{%.*s}", (int)sizeof(threadname) - 3, comm);
	add_proc(threadname, pid, ppid, uid/*, 1*/);
}
#endif

static void mread_proc(void)
{
	procps_status_t *p = NULL;
#if ENABLE_FEATURE_SHOW_THREADS
	pid_t parent = 0;
#endif
	int flags = PSSCAN_COMM | PSSCAN_PID | PSSCAN_PPID | PSSCAN_UIDGID | PSSCAN_TASKS;

	while ((p = procps_scan(p, flags)) != NULL) {
#if ENABLE_FEATURE_SHOW_THREADS
		if (p->pid != p->main_thread_pid)
			handle_thread(p->comm, p->pid, parent, p->uid);
		else
#endif
		{
			add_proc(p->comm, p->pid, p->ppid, p->uid/*, 0*/);
#if ENABLE_FEATURE_SHOW_THREADS
			parent = p->pid;
#endif
		}
	}
}

int pstree_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int pstree_main(int argc UNUSED_PARAM, char **argv)
{
	pid_t pid = 1;
	long uid = 0;

	INIT_G();

	G.output_width = get_terminal_width(0);

	getopt32(argv, "^" "p" "\0" "?1");
	argv += optind;

	if (argv[0]) {
		if (argv[0][0] >= '0' && argv[0][0] <= '9') {
			pid = xatoi(argv[0]);
		} else {
			uid = xuname2uid(argv[0]);
		}
	}

	mread_proc();

	if (!uid)
		dump_tree(find_proc(pid), 0, 1, 1, 1, 0);
	else {
		dump_by_user(find_proc(1), uid);
		if (!G.dumped) {
			bb_error_msg_and_die("no processes found");
		}
	}

	if (ENABLE_FEATURE_CLEAN_UP) {
		free(G.width);
		free(G.more);
	}
	return 0;
}
