/* vi: set sw=4 ts=4: */
/*
 * Mini diff implementation for busybox, adapted from OpenBSD diff.
 *
 * Copyright (C) 2010 by Matheus Izvekov <mizvekov@gmail.com>
 * Copyright (C) 2006 by Robert Sullivan <cogito.ergo.cogito@hotmail.com>
 * Copyright (c) 2003 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F39502-99-1-0512.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/*
 * The following code uses an algorithm due to Harold Stone,
 * which finds a pair of longest identical subsequences in
 * the two files.
 *
 * The major goal is to generate the match vector J.
 * J[i] is the index of the line in file1 corresponding
 * to line i in file0. J[i] = 0 if there is no
 * such line in file1.
 *
 * Lines are hashed so as to work in core. All potential
 * matches are located by sorting the lines of each file
 * on the hash (called "value"). In particular, this
 * collects the equivalence classes in file1 together.
 * Subroutine equiv replaces the value of each line in
 * file0 by the index of the first element of its
 * matching equivalence in (the reordered) file1.
 * To save space equiv squeezes file1 into a single
 * array member in which the equivalence classes
 * are simply concatenated, except that their first
 * members are flagged by changing sign.
 *
 * Next the indices that point into member are unsorted into
 * array class according to the original order of file0.
 *
 * The cleverness lies in routine stone. This marches
 * through the lines of file0, developing a vector klist
 * of "k-candidates". At step i a k-candidate is a matched
 * pair of lines x,y (x in file0, y in file1) such that
 * there is a common subsequence of length k
 * between the first i lines of file0 and the first y
 * lines of file1, but there is no such subsequence for
 * any smaller y. x is the earliest possible mate to y
 * that occurs in such a subsequence.
 *
 * Whenever any of the members of the equivalence class of
 * lines in file1 matable to a line in file0 has serial number
 * less than the y of some k-candidate, that k-candidate
 * with the smallest such y is replaced. The new
 * k-candidate is chained (via pred) to the current
 * k-1 candidate so that the actual subsequence can
 * be recovered. When a member has serial number greater
 * that the y of all k-candidates, the klist is extended.
 * At the end, the longest subsequence is pulled out
 * and placed in the array J by unravel
 *
 * With J in hand, the matches there recorded are
 * checked against reality to assure that no spurious
 * matches have crept in due to hashing. If they have,
 * they are broken, and "jackpot" is recorded--a harmless
 * matter except that a true match for a spuriously
 * mated line may now be unnecessarily reported as a change.
 *
 * Much of the complexity of the program comes simply
 * from trying to minimize core utilization and
 * maximize the range of doable problems by dynamically
 * allocating what is needed and reusing what is not.
 * The core requirements for problems larger than somewhat
 * are (in words) 2*length(file0) + length(file1) +
 * 3*(number of k-candidates installed), typically about
 * 6n words for files of length n.
 */
//config:config DIFF
//config:	bool "diff (13 kb)"
//config:	default y
//config:	help
//config:	diff compares two files or directories and outputs the
//config:	differences between them in a form that can be given to
//config:	the patch command.
//config:
//config:config FEATURE_DIFF_LONG_OPTIONS
//config:	bool "Enable long options"
//config:	default y
//config:	depends on DIFF && LONG_OPTS
//config:
//config:config FEATURE_DIFF_DIR
//config:	bool "Enable directory support"
//config:	default y
//config:	depends on DIFF
//config:	help
//config:	This option enables support for directory and subdirectory
//config:	comparison.

//applet:IF_DIFF(APPLET(diff, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_DIFF) += diff.o

//usage:#define diff_trivial_usage
//usage:       "[-abBdiNqrTstw] [-L LABEL] [-S FILE] [-U LINES] FILE1 FILE2"
//usage:#define diff_full_usage "\n\n"
//usage:       "Compare files line by line and output the differences between them.\n"
//usage:       "This implementation supports unified diffs only.\n"
//usage:     "\n	-a	Treat all files as text"
//usage:     "\n	-b	Ignore changes in the amount of whitespace"
//usage:     "\n	-B	Ignore changes whose lines are all blank"
//usage:     "\n	-d	Try hard to find a smaller set of changes"
//usage:     "\n	-i	Ignore case differences"
//usage:     "\n	-L	Use LABEL instead of the filename in the unified header"
//usage:     "\n	-N	Treat absent files as empty"
//usage:     "\n	-q	Output only whether files differ"
//usage:     "\n	-r	Recurse"
//usage:     "\n	-S	Start with FILE when comparing directories"
//usage:     "\n	-T	Make tabs line up by prefixing a tab when necessary"
//usage:     "\n	-s	Report when two files are the same"
//usage:     "\n	-t	Expand tabs to spaces in output"
//usage:     "\n	-U	Output LINES lines of context"
//usage:     "\n	-w	Ignore all whitespace"

#include "libbb.h"
#include "common_bufsiz.h"

#if 0
# define dbg_error_msg(...) bb_error_msg(__VA_ARGS__)
#else
# define dbg_error_msg(...) ((void)0)
#endif

enum {                  /* print_status() and diffreg() return values */
	STATUS_SAME,    /* files are the same */
	STATUS_DIFFER,  /* files differ */
	STATUS_BINARY,  /* binary files differ */
};

enum {                  /* Commandline flags */
	FLAG_a,
	FLAG_b,
	FLAG_d,
	FLAG_i,
	FLAG_L,         /* never used, handled by getopt32 */
	FLAG_N,
	FLAG_q,
	FLAG_r,
	FLAG_s,
	FLAG_S,         /* never used, handled by getopt32 */
	FLAG_t,
	FLAG_T,
	FLAG_U,         /* never used, handled by getopt32 */
	FLAG_w,
	FLAG_u,         /* ignored, this is the default */
	FLAG_p,         /* not implemented */
	FLAG_B,
	FLAG_E,         /* not implemented */
};
#define FLAG(x) (1 << FLAG_##x)

/* We cache file position to avoid excessive seeking */
typedef struct FILE_and_pos_t {
	FILE *ft_fp;
	off_t ft_pos;
} FILE_and_pos_t;

struct globals {
	smallint exit_status;
	int opt_U_context;
	const char *other_dir;
	char *label[2];
	struct stat stb[2];
};
#define G (*ptr_to_globals)
#define exit_status        (G.exit_status       )
#define opt_U_context      (G.opt_U_context     )
#define label              (G.label             )
#define stb                (G.stb               )
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
	opt_U_context = 3; \
} while (0)

typedef int token_t;

enum {
	/* Public */
	TOK_EMPTY = 1 << 9,  /* Line fully processed, you can proceed to the next */
	TOK_EOF   = 1 << 10, /* File ended */
	/* Private (Only to be used by read_token() */
	TOK_EOL   = 1 << 11, /* we saw EOL (sticky) */
	TOK_SPACE = 1 << 12, /* used -b code, means we are skipping spaces */
	SHIFT_EOF = (sizeof(token_t)*8 - 8) - 1,
	CHAR_MASK = 0x1ff,   /* 8th bit is used to distinguish EOF from 0xff */
};

/* Restores full EOF from one 8th bit: */
//#define TOK2CHAR(t) (((t) << SHIFT_EOF) >> SHIFT_EOF)
/* We don't really need the above, we only need to have EOF != any_real_char: */
#define TOK2CHAR(t) ((t) & CHAR_MASK)

static void seek_ft(FILE_and_pos_t *ft, off_t pos)
{
	if (ft->ft_pos != pos) {
		ft->ft_pos = pos;
		fseeko(ft->ft_fp, pos, SEEK_SET);
	}
}

/* Reads tokens from given fp, handling -b and -w flags
 * The user must reset tok every line start
 */
static int read_token(FILE_and_pos_t *ft, token_t tok)
{
	tok |= TOK_EMPTY;
	while (!(tok & TOK_EOL)) {
		bool is_space;
		int t;

		t = fgetc(ft->ft_fp);
		if (t != EOF)
			ft->ft_pos++;
		is_space = (t == EOF || isspace(t));

		/* If t == EOF (-1), set both TOK_EOF and TOK_EOL */
		tok |= (t & (TOK_EOF + TOK_EOL));
		/* Only EOL? */
		if (t == '\n')
			tok |= TOK_EOL;

		if (option_mask32 & FLAG(i)) /* Handcoded tolower() */
			t = (t >= 'A' && t <= 'Z') ? t - ('A' - 'a') : t;

		if ((option_mask32 & FLAG(w)) && is_space)
			continue;

		/* Trim char value to low 9 bits */
		t &= CHAR_MASK;

		if (option_mask32 & FLAG(b)) {
			/* Was prev char whitespace? */
			if (tok & TOK_SPACE) { /* yes */
				if (is_space) /* this one too, ignore it */
					continue;
				tok &= ~TOK_SPACE;
			} else if (is_space) {
				/* 1st whitespace char.
				 * Set TOK_SPACE and replace char by ' ' */
				t = TOK_SPACE + ' ';
			}
		}
		/* Clear EMPTY */
		tok &= ~(TOK_EMPTY + CHAR_MASK);
		/* Assign char value (low 9 bits) and maybe set TOK_SPACE */
		tok |= t;
		break;
	}
#if 0
	bb_error_msg("fp:%p tok:%x '%c'%s%s%s%s", fp, tok, tok & 0xff
		, tok & TOK_EOF ? " EOF" : ""
		, tok & TOK_EOL ? " EOL" : ""
		, tok & TOK_EMPTY ? " EMPTY" : ""
		, tok & TOK_SPACE ? " SPACE" : ""
	);
#endif
	return tok;
}

struct cand {
	int x;
	int y;
	int pred;
};

static int search(const int *c, int k, int y, const struct cand *list)
{
	int i, j;

	if (list[c[k]].y < y)  /* quick look for typical case */
		return k + 1;

	for (i = 0, j = k + 1;;) {
		const int l = (i + j) >> 1;
		if (l > i) {
			const int t = list[c[l]].y;
			if (t > y)
				j = l;
			else if (t < y)
				i = l;
			else
				return l;
		} else
			return l + 1;
	}
}

static void stone(const int *a, int n, const int *b, int *J, int pref)
{
	const unsigned isq = isqrt(n);
	const unsigned bound =
		(option_mask32 & FLAG(d)) ? UINT_MAX : MAX(256, isq);
	int clen = 1;
	int clistlen = 100;
	int k = 0;
	struct cand *clist = xzalloc(clistlen * sizeof(clist[0]));
	struct cand cand;
	struct cand *q;
	int *klist = xzalloc((n + 2) * sizeof(klist[0]));
	/*clist[0] = (struct cand){0}; - xzalloc did it */
	/*klist[0] = 0; */

	for (cand.x = 1; cand.x <= n; cand.x++) {
		int j = a[cand.x], oldl = 0;
		unsigned numtries = 0;
		if (j == 0)
			continue;
		cand.y = -b[j];
		cand.pred = klist[0];
		do {
			int l, tc;
			if (cand.y <= clist[cand.pred].y)
				continue;
			l = search(klist, k, cand.y, clist);
			if (l != oldl + 1)
				cand.pred = klist[l - 1];
			if (l <= k && clist[klist[l]].y <= cand.y)
				continue;
			if (clen == clistlen) {
				clistlen = clistlen * 11 / 10;
				clist = xrealloc(clist, clistlen * sizeof(clist[0]));
			}
			clist[clen] = cand;
			tc = klist[l];
			klist[l] = clen++;
			if (l <= k) {
				cand.pred = tc;
				oldl = l;
				numtries++;
			} else {
				k++;
				break;
			}
		} while ((cand.y = b[++j]) > 0 && numtries < bound);
	}
	/* Unravel */
	for (q = clist + klist[k]; q->y; q = clist + q->pred)
		J[q->x + pref] = q->y + pref;
	free(klist);
	free(clist);
}

struct line {
	/* 'serial' is not used in the beginning, so we reuse it
	 * to store line offsets, thus reducing memory pressure
	 */
	union {
		unsigned serial;
		off_t offset;
	};
	unsigned value;
};

static void equiv(struct line *a, int n, struct line *b, int m, int *c)
{
	int i = 1, j = 1;

	while (i <= n && j <= m) {
		if (a[i].value < b[j].value)
			a[i++].value = 0;
		else if (a[i].value == b[j].value)
			a[i++].value = j;
		else
			j++;
	}
	while (i <= n)
		a[i++].value = 0;
	b[m + 1].value = 0;
	j = 0;
	while (++j <= m) {
		c[j] = -b[j].serial;
		while (b[j + 1].value == b[j].value) {
			j++;
			c[j] = b[j].serial;
		}
	}
	c[j] = -1;
}

static void unsort(const struct line *f, int l, int *b)
{
	int i;
	int *a = xmalloc((l + 1) * sizeof(a[0]));
	for (i = 1; i <= l; i++)
		a[f[i].serial] = f[i].value;
	for (i = 1; i <= l; i++)
		b[i] = a[i];
	free(a);
}

static int line_compar(const void *a, const void *b)
{
#define l0 ((const struct line*)a)
#define l1 ((const struct line*)b)
	int r = l0->value - l1->value;
	if (r)
		return r;
	return l0->serial - l1->serial;
#undef l0
#undef l1
}

static void fetch(FILE_and_pos_t *ft, const off_t *ix, int a, int b, int ch)
{
	int i, j, col;
	for (i = a; i <= b; i++) {
		seek_ft(ft, ix[i - 1]);
		putchar(ch);
		if (option_mask32 & FLAG(T))
			putchar('\t');
		for (j = 0, col = 0; j < ix[i] - ix[i - 1]; j++) {
			int c = fgetc(ft->ft_fp);
			if (c == EOF) {
				puts("\n\\ No newline at end of file");
				return;
			}
			ft->ft_pos++;
			if (c == '\t' && (option_mask32 & FLAG(t)))
				do putchar(' '); while (++col & 7);
			else {
				putchar(c);
				col++;
			}
		}
	}
}

/* Creates the match vector J, where J[i] is the index
 * of the line in the new file corresponding to the line i
 * in the old file. Lines start at 1 instead of 0, that value
 * being used instead to denote no corresponding line.
 * This vector is dynamically allocated and must be freed by the caller.
 *
 * * fp is an input parameter, where fp[0] and fp[1] are the open
 *   old file and new file respectively.
 * * nlen is an output variable, where nlen[0] and nlen[1]
 *   gets the number of lines in the old and new file respectively.
 * * ix is an output variable, where ix[0] and ix[1] gets
 *   assigned dynamically allocated vectors of the offsets of the lines
 *   of the old and new file respectively. These must be freed by the caller.
 */
static NOINLINE int *create_J(FILE_and_pos_t ft[2], int nlen[2], off_t *ix[2])
{
	int *J, slen[2], *class, *member;
	struct line *nfile[2], *sfile[2];
	int pref = 0, suff = 0, i, j, delta;

	/* Lines of both files are hashed, and in the process
	 * their offsets are stored in the array ix[fileno]
	 * where fileno == 0 points to the old file, and
	 * fileno == 1 points to the new one.
	 */
	for (i = 0; i < 2; i++) {
		unsigned hash;
		token_t tok;
		size_t sz = 100;
		nfile[i] = xmalloc((sz + 3) * sizeof(nfile[i][0]));
		/* ft gets here without the correct position, cant use seek_ft */
		ft[i].ft_pos = 0;
		fseeko(ft[i].ft_fp, 0, SEEK_SET);

		nlen[i] = 0;
		/* We could zalloc nfile, but then zalloc starts showing in gprof at ~1% */
		nfile[i][0].offset = 0;
		goto start; /* saves code */
		while (1) {
			tok = read_token(&ft[i], tok);
			if (!(tok & TOK_EMPTY)) {
				/* Hash algorithm taken from Robert Sedgewick, Algorithms in C, 3d ed., p 578. */
				/*hash = hash * 128 - hash + TOK2CHAR(tok);
				 * gcc insists on optimizing above to "hash * 127 + ...", thus... */
				unsigned o = hash - TOK2CHAR(tok);
				hash = hash * 128 - o; /* we want SPEED here */
				continue;
			}
			if (nlen[i]++ == sz) {
				sz = sz * 3 / 2;
				nfile[i] = xrealloc(nfile[i], (sz + 3) * sizeof(nfile[i][0]));
			}
			/* line_compar needs hashes fit into positive int */
			nfile[i][nlen[i]].value = hash & INT_MAX;
			/* like ftello(ft[i].ft_fp) but faster (avoids lseek syscall) */
			nfile[i][nlen[i]].offset = ft[i].ft_pos;
			if (tok & TOK_EOF) {
				/* EOF counts as a token, so we have to adjust it here */
				nfile[i][nlen[i]].offset++;
				break;
			}
start:
			hash = tok = 0;
		}
		/* Exclude lone EOF line from the end of the file, to make fetch()'s job easier */
		if (nfile[i][nlen[i]].offset - nfile[i][nlen[i] - 1].offset == 1)
			nlen[i]--;
		/* Now we copy the line offsets into ix */
		ix[i] = xmalloc((nlen[i] + 2) * sizeof(ix[i][0]));
		for (j = 0; j < nlen[i] + 1; j++)
			ix[i][j] = nfile[i][j].offset;
	}

	/* length of prefix and suffix is calculated */
	for (; pref < nlen[0] && pref < nlen[1] &&
	       nfile[0][pref + 1].value == nfile[1][pref + 1].value;
	       pref++);
	for (; suff < nlen[0] - pref && suff < nlen[1] - pref &&
	       nfile[0][nlen[0] - suff].value == nfile[1][nlen[1] - suff].value;
	       suff++);
	/* Arrays are pruned by the suffix and prefix length,
	 * the result being sorted and stored in sfile[fileno],
	 * and their sizes are stored in slen[fileno]
	 */
	for (j = 0; j < 2; j++) {
		sfile[j] = nfile[j] + pref;
		slen[j] = nlen[j] - pref - suff;
		for (i = 0; i <= slen[j]; i++)
			sfile[j][i].serial = i;
		qsort(sfile[j] + 1, slen[j], sizeof(*sfile[j]), line_compar);
	}
	/* nfile arrays are reused to reduce memory pressure
	 * The #if zeroed out section performs the same task as the
	 * one in the #else section.
	 * Peak memory usage is higher, but one array copy is avoided
	 * by not using unsort()
	 */
#if 0
	member = xmalloc((slen[1] + 2) * sizeof(member[0]));
	equiv(sfile[0], slen[0], sfile[1], slen[1], member);
	free(nfile[1]);

	class = xmalloc((slen[0] + 1) * sizeof(class[0]));
	for (i = 1; i <= slen[0]; i++) /* Unsorting */
		class[sfile[0][i].serial] = sfile[0][i].value;
	free(nfile[0]);
#else
	member = (int *)nfile[1];
	equiv(sfile[0], slen[0], sfile[1], slen[1], member);
	member = xrealloc(member, (slen[1] + 2) * sizeof(member[0]));

	class = (int *)nfile[0];
	unsort(sfile[0], slen[0], (int *)nfile[0]);
	class = xrealloc(class, (slen[0] + 2) * sizeof(class[0]));
#endif
	J = xmalloc((nlen[0] + 2) * sizeof(J[0]));
	/* The elements of J which fall inside the prefix and suffix regions
	 * are marked as unchanged, while the ones which fall outside
	 * are initialized with 0 (no matches), so that function stone can
	 * then assign them their right values
	 */
	for (i = 0, delta = nlen[1] - nlen[0]; i <= nlen[0]; i++)
		J[i] = i <= pref            ?  i :
		       i > (nlen[0] - suff) ? (i + delta) : 0;
	/* Here the magic is performed */
	stone(class, slen[0], member, J, pref);
	J[nlen[0] + 1] = nlen[1] + 1;

	free(class);
	free(member);

	/* Both files are rescanned, in an effort to find any lines
	 * which, due to limitations intrinsic to any hashing algorithm,
	 * are different but ended up confounded as the same
	 */
	for (i = 1; i <= nlen[0]; i++) {
		if (!J[i])
			continue;

		seek_ft(&ft[0], ix[0][i - 1]);
		seek_ft(&ft[1], ix[1][J[i] - 1]);

		for (j = J[i]; i <= nlen[0] && J[i] == j; i++, j++) {
			token_t tok0 = 0, tok1 = 0;
			do {
				tok0 = read_token(&ft[0], tok0);
				tok1 = read_token(&ft[1], tok1);

				if (((tok0 ^ tok1) & TOK_EMPTY) != 0 /* one is empty (not both) */
				 || (!(tok0 & TOK_EMPTY) && TOK2CHAR(tok0) != TOK2CHAR(tok1))
				) {
					J[i] = 0; /* Break the correspondence */
				}
			} while (!(tok0 & tok1 & TOK_EMPTY));
		}
	}

	return J;
}

static bool diff(FILE* fp[2], char *file[2])
{
	int nlen[2];
	off_t *ix[2];
	FILE_and_pos_t ft[2];
	typedef struct { int a, b; } vec_t[2];
	vec_t *vec = NULL;
	int i = 1, j, k, idx = -1;
	bool anychange = false;
	int *J;

	ft[0].ft_fp = fp[0];
	ft[1].ft_fp = fp[1];
	/* note that ft[i].ft_pos is unintitalized, create_J()
	 * must not assume otherwise */
	J = create_J(ft, nlen, ix);

	do {
		bool nonempty = false;

		while (1) {
			vec_t v;

			for (v[0].a = i; v[0].a <= nlen[0] && J[v[0].a] == J[v[0].a - 1] + 1; v[0].a++)
				continue;
			v[1].a = J[v[0].a - 1] + 1;

			for (v[0].b = v[0].a - 1; v[0].b < nlen[0] && !J[v[0].b + 1]; v[0].b++)
				continue;
			v[1].b = J[v[0].b + 1] - 1;
			/*
			 * Indicate that there is a difference between lines a and b of the 'from' file
			 * to get to lines c to d of the 'to' file. If a is greater than b then there
			 * are no lines in the 'from' file involved and this means that there were
			 * lines appended (beginning at b).  If c is greater than d then there are
			 * lines missing from the 'to' file.
			 */
			if (v[0].a <= v[0].b || v[1].a <= v[1].b) {
				/*
				 * If this change is more than 'context' lines from the
				 * previous change, dump the record and reset it.
				 */
				int ct = (2 * opt_U_context) + 1;
				if (idx >= 0
				 && v[0].a > vec[idx][0].b + ct
				 && v[1].a > vec[idx][1].b + ct
				) {
					break;
				}

				for (j = 0; j < 2; j++)
					for (k = v[j].a; k <= v[j].b; k++)
						nonempty |= (ix[j][k] - ix[j][k - 1] != 1);

				vec = xrealloc_vector(vec, 6, ++idx);
				memcpy(vec[idx], v, sizeof(v));
			}

			i = v[0].b + 1;
			if (i > nlen[0])
				break;
			J[v[0].b] = v[1].b;
		}
		if (idx < 0 || ((option_mask32 & FLAG(B)) && !nonempty))
			goto cont;
		if (!(option_mask32 & FLAG(q))) {
			int lowa;
			vec_t span, *cvp = vec;

			if (!anychange) {
				/* Print the context/unidiff header first time through */
				printf("--- %s\n", label[0] ? label[0] : file[0]);
				printf("+++ %s\n", label[1] ? label[1] : file[1]);
			}

			printf("@@");
			for (j = 0; j < 2; j++) {
				int a = span[j].a = MAX(1, (*cvp)[j].a - opt_U_context);
				int b = span[j].b = MIN(nlen[j], vec[idx][j].b + opt_U_context);

				printf(" %c%d", j ? '+' : '-', MIN(a, b));
				if (a == b)
					continue;
				printf(",%d", (a < b) ? b - a + 1 : 0);
			}
			puts(" @@");
			/*
			 * Output changes in "unified" diff format--the old and new lines
			 * are printed together.
			 */
			for (lowa = span[0].a; ; lowa = (*cvp++)[0].b + 1) {
				bool end = cvp > &vec[idx];
				fetch(&ft[0], ix[0], lowa, end ? span[0].b : (*cvp)[0].a - 1, ' ');
				if (end)
					break;
				for (j = 0; j < 2; j++)
					fetch(&ft[j], ix[j], (*cvp)[j].a, (*cvp)[j].b, j ? '+' : '-');
			}
		}
		anychange = true;
 cont:
		idx = -1;
	} while (i <= nlen[0]);

	free(vec);
	free(ix[0]);
	free(ix[1]);
	free(J);
	return anychange;
}

static int diffreg(char *file[2])
{
	FILE *fp[2];
	bool binary = false, differ = false;
	int status = STATUS_SAME, i;

	fp[0] = stdin;
	fp[1] = stdin;
	for (i = 0; i < 2; i++) {
		int fd = STDIN_FILENO;
		if (!LONE_DASH(file[i])) {
			if (!(option_mask32 & FLAG(N))) {
				fd = open_or_warn(file[i], O_RDONLY);
				if (fd == -1)
					goto out;
			} else {
				/* -N: if some file does not exist compare it like empty */
				fd = open(file[i], O_RDONLY);
				if (fd == -1)
					fd = xopen("/dev/null", O_RDONLY);
			}
		}
		/* Our diff implementation is using seek.
		 * When we meet non-seekable file, we must make a temp copy.
		 */
		if (lseek(fd, 0, SEEK_SET) == -1 && errno == ESPIPE) {
			char name[] = "/tmp/difXXXXXX";
			int fd_tmp = xmkstemp(name);

			unlink(name);
			if (bb_copyfd_eof(fd, fd_tmp) < 0)
				xfunc_die();
			if (fd != STDIN_FILENO)
				close(fd);
			fd = fd_tmp;
			xlseek(fd, 0, SEEK_SET);
		}
		fp[i] = fdopen(fd, "r");
	}

	setup_common_bufsiz();
	while (1) {
		const size_t sz = COMMON_BUFSIZE / 2;
		char *const buf0 = bb_common_bufsiz1;
		char *const buf1 = buf0 + sz;
		int j, k;
		i = fread(buf0, 1, sz, fp[0]);
		j = fread(buf1, 1, sz, fp[1]);
		if (i != j) {
			differ = true;
			i = MIN(i, j);
		}
		if (i == 0)
			break;
		for (k = 0; k < i; k++) {
			if (!buf0[k] || !buf1[k])
				binary = true;
			if (buf0[k] != buf1[k])
				differ = true;
		}
	}
	if (differ) {
		if (binary && !(option_mask32 & FLAG(a)))
			status = STATUS_BINARY;
		else if (diff(fp, file))
			status = STATUS_DIFFER;
	}
	if (status != STATUS_SAME)
		exit_status |= 1;
out:
	fclose_if_not_stdin(fp[0]);
	fclose_if_not_stdin(fp[1]);

	return status;
}

static void print_status(int status, char *path[2])
{
	switch (status) {
	case STATUS_BINARY:
	case STATUS_DIFFER:
		if ((option_mask32 & FLAG(q)) || status == STATUS_BINARY)
			printf("Files %s and %s differ\n", path[0], path[1]);
		break;
	case STATUS_SAME:
		if (option_mask32 & FLAG(s))
			printf("Files %s and %s are identical\n", path[0], path[1]);
		break;
	}
}

#if ENABLE_FEATURE_DIFF_DIR
struct dlist {
	size_t len;
	int s, e;
	char **dl;
};

/* This function adds a filename to dl, the directory listing. */
static int FAST_FUNC add_to_dirlist(const char *filename,
		struct stat *sb UNUSED_PARAM,
		void *userdata, int depth UNUSED_PARAM)
{
	struct dlist *const l = userdata;
	const char *file = filename + l->len;
	while (*file == '/')
		file++;
	l->dl = xrealloc_vector(l->dl, 6, l->e);
	l->dl[l->e] = xstrdup(file);
	l->e++;
	return TRUE;
}

/* If recursion is not set, this function adds the directory
 * to the list and prevents recursive_action from recursing into it.
 */
static int FAST_FUNC skip_dir(const char *filename,
		struct stat *sb, void *userdata,
		int depth)
{
	if (!(option_mask32 & FLAG(r)) && depth) {
		add_to_dirlist(filename, sb, userdata, depth);
		return SKIP;
	}
	if (!(option_mask32 & FLAG(N))) {
		/* -r without -N: no need to recurse into dirs
		 * which do not exist on the "other side".
		 * Testcase: diff -r /tmp /
		 * (it would recurse deep into /proc without this code) */
		struct dlist *const l = userdata;
		filename += l->len;
		if (filename[0]) {
			struct stat osb;
			char *othername = concat_path_file(G.other_dir, filename);
			int r = stat(othername, &osb);
			free(othername);
			if (r != 0 || !S_ISDIR(osb.st_mode)) {
				/* other dir doesn't have similarly named
				 * directory, don't recurse; return 1 upon
				 * exit, just like diffutils' diff */
				exit_status |= 1;
				return SKIP;
			}
		}
	}
	return TRUE;
}

static void diffdir(char *p[2], const char *s_start)
{
	struct dlist list[2];
	int i;

	memset(&list, 0, sizeof(list));
	for (i = 0; i < 2; i++) {
		/*list[i].s = list[i].e = 0; - memset did it */
		/*list[i].dl = NULL; */

		G.other_dir = p[1 - i];
		/* We need to trim root directory prefix.
		 * Using list.len to specify its length,
		 * add_to_dirlist will remove it. */
		list[i].len = strlen(p[i]);
		recursive_action(p[i], ACTION_RECURSE | ACTION_FOLLOWLINKS,
				add_to_dirlist, skip_dir, &list[i], 0);
		/* Sort dl alphabetically.
		 * GNU diff does this ignoring any number of trailing dots.
		 * We don't, so for us dotted files almost always are
		 * first on the list.
		 */
		qsort_string_vector(list[i].dl, list[i].e);
		/* If -S was set, find the starting point. */
		if (!s_start)
			continue;
		while (list[i].s < list[i].e && strcmp(list[i].dl[list[i].s], s_start) < 0)
			list[i].s++;
	}
	/* Now that both dirlist1 and dirlist2 contain sorted directory
	 * listings, we can start to go through dirlist1. If both listings
	 * contain the same file, then do a normal diff. Otherwise, behaviour
	 * is determined by whether the -N flag is set. */
	while (1) {
		char *dp[2];
		int pos;
		int k;

		dp[0] = list[0].s < list[0].e ? list[0].dl[list[0].s] : NULL;
		dp[1] = list[1].s < list[1].e ? list[1].dl[list[1].s] : NULL;
		if (!dp[0] && !dp[1])
			break;
		pos = !dp[0] ? 1 : (!dp[1] ? -1 : strcmp(dp[0], dp[1]));
		k = pos > 0;
		if (pos && !(option_mask32 & FLAG(N))) {
			printf("Only in %s: %s\n", p[k], dp[k]);
			exit_status |= 1;
		} else {
			char *fullpath[2], *path[2]; /* if -N */

			for (i = 0; i < 2; i++) {
				if (pos == 0 || i == k) {
					path[i] = fullpath[i] = concat_path_file(p[i], dp[i]);
					stat(fullpath[i], &stb[i]);
				} else {
					fullpath[i] = concat_path_file(p[i], dp[1 - i]);
					path[i] = (char *)bb_dev_null;
				}
			}
			if (pos)
				stat(fullpath[k], &stb[1 - k]);

			if (S_ISDIR(stb[0].st_mode) && S_ISDIR(stb[1].st_mode))
				printf("Common subdirectories: %s and %s\n", fullpath[0], fullpath[1]);
			else if (!S_ISREG(stb[0].st_mode) && !S_ISDIR(stb[0].st_mode))
				printf("File %s is not a regular file or directory and was skipped\n", fullpath[0]);
			else if (!S_ISREG(stb[1].st_mode) && !S_ISDIR(stb[1].st_mode))
				printf("File %s is not a regular file or directory and was skipped\n", fullpath[1]);
			else if (S_ISDIR(stb[0].st_mode) != S_ISDIR(stb[1].st_mode)) {
				if (S_ISDIR(stb[0].st_mode))
					printf("File %s is a %s while file %s is a %s\n", fullpath[0], "directory", fullpath[1], "regular file");
				else
					printf("File %s is a %s while file %s is a %s\n", fullpath[0], "regular file", fullpath[1], "directory");
			} else
				print_status(diffreg(path), fullpath);

			free(fullpath[0]);
			free(fullpath[1]);
		}
		free(dp[k]);
		list[k].s++;
		if (pos == 0) {
			free(dp[1 - k]);
			list[1 - k].s++;
		}
	}
	if (ENABLE_FEATURE_CLEAN_UP) {
		free(list[0].dl);
		free(list[1].dl);
	}
}
#endif

#if ENABLE_FEATURE_DIFF_LONG_OPTIONS
static const char diff_longopts[] ALIGN1 =
	"ignore-case\0"              No_argument       "i"
	"ignore-tab-expansion\0"     No_argument       "E"
	"ignore-space-change\0"      No_argument       "b"
	"ignore-all-space\0"         No_argument       "w"
	"ignore-blank-lines\0"       No_argument       "B"
	"text\0"                     No_argument       "a"
	"unified\0"                  Required_argument "U"
	"label\0"                    Required_argument "L"
	"show-c-function\0"          No_argument       "p"
	"brief\0"                    No_argument       "q"
	"expand-tabs\0"              No_argument       "t"
	"initial-tab\0"              No_argument       "T"
	"recursive\0"                No_argument       "r"
	"new-file\0"                 No_argument       "N"
	"report-identical-files\0"   No_argument       "s"
	"starting-file\0"            Required_argument "S"
	"minimal\0"                  No_argument       "d"
	;
# define GETOPT32 getopt32long
# define LONGOPTS ,diff_longopts
#else
# define GETOPT32 getopt32
# define LONGOPTS
#endif

int diff_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int diff_main(int argc UNUSED_PARAM, char **argv)
{
	int gotstdin = 0, i;
	char *file[2], *s_start = NULL;
	llist_t *L_arg = NULL;

	INIT_G();

	/* exactly 2 params; collect multiple -L <label>; -U N */
	GETOPT32(argv, "^" "abdiL:*NqrsS:tTU:+wupBE" "\0" "=2"
			LONGOPTS,
			&L_arg, &s_start, &opt_U_context);
	argv += optind;
	while (L_arg)
		label[!!label[0]] = llist_pop(&L_arg);

	/* Compat: "diff file name_which_doesnt_exist" exits with 2 */
	xfunc_error_retval = 2;
	for (i = 0; i < 2; i++) {
		file[i] = argv[i];
		if (LONE_DASH(file[i])) {
			fstat(STDIN_FILENO, &stb[i]);
			gotstdin++;
		} else if (option_mask32 & FLAG(N)) {
			if (stat(file[i], &stb[i]))
				xstat("/dev/null", &stb[i]);
		} else {
			xstat(file[i], &stb[i]);
		}
	}
	xfunc_error_retval = 1;

	if (gotstdin && (S_ISDIR(stb[0].st_mode) || S_ISDIR(stb[1].st_mode)))
		bb_error_msg_and_die("can't compare stdin to a directory");

	/* Compare metadata to check if the files are the same physical file.
	 *
	 * Comment from diffutils source says:
	 * POSIX says that two files are identical if st_ino and st_dev are
	 * the same, but many file systems incorrectly assign the same (device,
	 * inode) pair to two distinct files, including:
	 * GNU/Linux NFS servers that export all local file systems as a
	 * single NFS file system, if a local device number (st_dev) exceeds
	 * 255, or if a local inode number (st_ino) exceeds 16777215.
	 */
	if (ENABLE_DESKTOP
	 && stb[0].st_ino == stb[1].st_ino
	 && stb[0].st_dev == stb[1].st_dev
	 && stb[0].st_size == stb[1].st_size
	 && stb[0].st_mtime == stb[1].st_mtime
	 && stb[0].st_ctime == stb[1].st_ctime
	 && stb[0].st_mode == stb[1].st_mode
	 && stb[0].st_nlink == stb[1].st_nlink
	 && stb[0].st_uid == stb[1].st_uid
	 && stb[0].st_gid == stb[1].st_gid
	) {
		/* files are physically the same; no need to compare them */
		return STATUS_SAME;
	}

	if (S_ISDIR(stb[0].st_mode) && S_ISDIR(stb[1].st_mode)) {
#if ENABLE_FEATURE_DIFF_DIR
		diffdir(file, s_start);
#else
		bb_error_msg_and_die("no support for directory comparison");
#endif
	} else {
		bool dirfile = S_ISDIR(stb[0].st_mode) || S_ISDIR(stb[1].st_mode);
		bool dir = S_ISDIR(stb[1].st_mode);
		if (dirfile) {
			const char *slash = strrchr(file[!dir], '/');
			file[dir] = concat_path_file(file[dir], slash ? slash + 1 : file[!dir]);
			xstat(file[dir], &stb[dir]);
		}
		/* diffreg can get non-regular files here */
		print_status(gotstdin > 1 ? STATUS_SAME : diffreg(file), file);

		if (dirfile)
			free(file[dir]);
	}

	return exit_status;
}
