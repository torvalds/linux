/* Adapted from toybox's patch. */

/* vi: set sw=4 ts=4:
 *
 * patch.c - Apply a "universal" diff.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * see http://www.opengroup.org/onlinepubs/009695399/utilities/patch.html
 * (But only does -u, because who still cares about "ed"?)
 *
 * TODO:
 * -b backup
 * -l treat all whitespace as a single space
 * -N ignore already applied
 * -d chdir first
 * -D define wrap #ifdef and #ifndef around changes
 * -o outfile output here instead of in place
 * -r rejectfile write rejected hunks to this file
 *
 * -E remove empty files --remove-empty-files
 * -f force (no questions asked)
 * -F fuzz (number, default 2)
 * [file] which file to patch

USE_PATCH(NEWTOY(patch, USE_TOYBOX_DEBUG("x")"up#i:R", TOYFLAG_USR|TOYFLAG_BIN))

config PATCH
	bool "patch (9.1 kb)"
	default y
	help
	  usage: patch [-i file] [-p depth] [-Ru]

	  Apply a unified diff to one or more files.

	  -i	Input file (defaults=stdin)
	  -p	number of '/' to strip from start of file paths (default=all)
	  -R	Reverse patch.
	  -u	Ignored (only handles "unified" diffs)

	  This version of patch only handles unified diffs, and only modifies
	  a file when all all hunks to that file apply.  Patch prints failed
	  hunks to stderr, and exits with nonzero status if any hunks fail.

	  A file compared against /dev/null (or with a date <= the epoch) is
	  created/deleted as appropriate.
*/
#include "libbb.h"

struct double_list {
	struct double_list *next;
	struct double_list *prev;
	char *data;
};

// Return the first item from the list, advancing the list (which must be called
// as &list)
static
void *TOY_llist_pop(void *list)
{
	// I'd use a void ** for the argument, and even accept the typecast in all
	// callers as documentation you need the &, except the stupid compiler
	// would then scream about type-punned pointers.  Screw it.
	void **llist = (void **)list;
	void **next = (void **)*llist;
	*llist = *next;

	return (void *)next;
}

// Free all the elements of a linked list
// if freeit!=NULL call freeit() on each element before freeing it.
static
void TOY_llist_free(void *list, void (*freeit)(void *data))
{
	while (list) {
		void *pop = TOY_llist_pop(&list);
		if (freeit) freeit(pop);
		else free(pop);

		// End doubly linked list too.
		if (list==pop) break;
	}
}

// Add an entry to the end off a doubly linked list
static
struct double_list *dlist_add(struct double_list **list, char *data)
{
	struct double_list *line = xmalloc(sizeof(struct double_list));

	line->data = data;
	if (*list) {
		line->next = *list;
		line->prev = (*list)->prev;
		(*list)->prev->next = line;
		(*list)->prev = line;
	} else *list = line->next = line->prev = line;

	return line;
}

// Ensure entire path exists.
// If mode != -1 set permissions on newly created dirs.
// Requires that path string be writable (for temporary null terminators).
static
void xmkpath(char *path, int mode)
{
	char *p, old;
	mode_t mask;
	int rc;
	struct stat st;

	for (p = path; ; p++) {
		if (!*p || *p == '/') {
			old = *p;
			*p = rc = 0;
			if (stat(path, &st) || !S_ISDIR(st.st_mode)) {
				if (mode != -1) {
					mask = umask(0);
					rc = mkdir(path, mode);
					umask(mask);
				} else rc = mkdir(path, 0777);
			}
			*p = old;
			if(rc) bb_perror_msg_and_die("mkpath '%s'", path);
		}
		if (!*p) break;
	}
}

// Slow, but small.
static
char *get_rawline(int fd, long *plen, char end)
{
	char c, *buf = NULL;
	long len = 0;

	for (;;) {
		if (1>read(fd, &c, 1)) break;
		if (!(len & 63)) buf=xrealloc(buf, len+65);
		if ((buf[len++]=c) == end) break;
	}
	if (buf) buf[len]=0;
	if (plen) *plen = len;

	return buf;
}

static
char *get_line(int fd)
{
	long len;
	char *buf = get_rawline(fd, &len, '\n');

	if (buf && buf[--len]=='\n') buf[len]=0;

	return buf;
}

// Copy the rest of in to out and close both files.
static
void xsendfile(int in, int out)
{
	long len;
	char buf[4096];

	if (in<0) return;
	for (;;) {
		len = safe_read(in, buf, 4096);
		if (len<1) break;
		xwrite(out, buf, len);
	}
}

// Copy the rest of the data and replace the original with the copy.
static
void replace_tempfile(int fdin, int fdout, char **tempname)
{
	char *temp = xstrdup(*tempname);

	temp[strlen(temp)-6]=0;
	if (fdin != -1) {
		xsendfile(fdin, fdout);
		xclose(fdin);
	}
	xclose(fdout);
	rename(*tempname, temp);
	free(*tempname);
	free(temp);
	*tempname = NULL;
}

// Open a temporary file to copy an existing file into.
static
int copy_tempfile(int fdin, char *name, char **tempname)
{
	struct stat statbuf;
	int fd;

	*tempname = xasprintf("%sXXXXXX", name);
	fd = mkstemp(*tempname);
	if(-1 == fd) bb_perror_msg_and_die("no temp file");

	// Set permissions of output file
	fstat(fdin, &statbuf);
	fchmod(fd, statbuf.st_mode);

	return fd;
}

// Abort the copy and delete the temporary file.
static
void delete_tempfile(int fdin, int fdout, char **tempname)
{
	close(fdin);
	close(fdout);
	unlink(*tempname);
	free(*tempname);
	*tempname = NULL;
}



struct globals {
	char *infile;
	long prefix;

	struct double_list *current_hunk;
	long oldline, oldlen, newline, newlen, linenum;
	int context, state, filein, fileout, filepatch, hunknum;
	char *tempname;

	// was toys.foo:
	int exitval;
};
#define TT (*ptr_to_globals)
#define INIT_TT() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(TT))); \
} while (0)


//bbox had: "p:i:RN"
#define FLAG_STR "Rup:i:x"
/* FLAG_REVERSE must be == 1! Code uses this fact. */
#define FLAG_REVERSE (1 << 0)
#define FLAG_u       (1 << 1)
#define FLAG_PATHLEN (1 << 2)
#define FLAG_INPUT   (1 << 3)
//non-standard:
#define FLAG_DEBUG   (1 << 4)

// Dispose of a line of input, either by writing it out or discarding it.

// state < 2: just free
// state = 2: write whole line to stderr
// state = 3: write whole line to fileout
// state > 3: write line+1 to fileout when *line != state

#define PATCH_DEBUG (option_mask32 & FLAG_DEBUG)

static void do_line(void *data)
{
	struct double_list *dlist = (struct double_list *)data;

	if (TT.state>1 && *dlist->data != TT.state)
		fdprintf(TT.state == 2 ? 2 : TT.fileout,
			"%s\n", dlist->data+(TT.state>3 ? 1 : 0));

	if (PATCH_DEBUG) fdprintf(2, "DO %d: %s\n", TT.state, dlist->data);

	free(dlist->data);
	free(data);
}

static void finish_oldfile(void)
{
	if (TT.tempname) replace_tempfile(TT.filein, TT.fileout, &TT.tempname);
	TT.fileout = TT.filein = -1;
}

static void fail_hunk(void)
{
	if (!TT.current_hunk) return;
	TT.current_hunk->prev->next = 0;

	fdprintf(2, "Hunk %d FAILED %ld/%ld.\n", TT.hunknum, TT.oldline, TT.newline);
	TT.exitval = 1;

	// If we got to this point, we've seeked to the end.  Discard changes to
	// this file and advance to next file.

	TT.state = 2;
	TOY_llist_free(TT.current_hunk, do_line);
	TT.current_hunk = NULL;
	delete_tempfile(TT.filein, TT.fileout, &TT.tempname);
	TT.state = 0;
}

// Given a hunk of a unified diff, make the appropriate change to the file.
// This does not use the location information, but instead treats a hunk
// as a sort of regex.  Copies data from input to output until it finds
// the change to be made, then outputs the changed data and returns.
// (Finding EOF first is an error.)  This is a single pass operation, so
// multiple hunks must occur in order in the file.

static int apply_one_hunk(void)
{
	struct double_list *plist, *buf = NULL, *check;
	int matcheof = 0, reverse = option_mask32 & FLAG_REVERSE, backwarn = 0;

	// Break doubly linked list so we can use singly linked traversal function.
	TT.current_hunk->prev->next = NULL;

	// Match EOF if there aren't as many ending context lines as beginning
	for (plist = TT.current_hunk; plist; plist = plist->next) {
		if (plist->data[0]==' ') matcheof++;
		else matcheof = 0;
		if (PATCH_DEBUG) fdprintf(2, "HUNK:%s\n", plist->data);
	}
	matcheof = matcheof < TT.context;

	if (PATCH_DEBUG) fdprintf(2,"MATCHEOF=%c\n", matcheof ? 'Y' : 'N');

	// Loop through input data searching for this hunk.  Match all context
	// lines and all lines to be removed until we've found the end of a
	// complete hunk.
	plist = TT.current_hunk;
	buf = NULL;
	if (TT.context) for (;;) {
		char *data = get_line(TT.filein);

		TT.linenum++;

		// Figure out which line of hunk to compare with next.  (Skip lines
		// of the hunk we'd be adding.)
		while (plist && *plist->data == "+-"[reverse]) {
			if (data && strcmp(data, plist->data+1) == 0) {
				if (!backwarn) {
					fdprintf(2,"Possibly reversed hunk %d at %ld\n",
						TT.hunknum, TT.linenum);
					backwarn++;
				}
			}
			plist = plist->next;
		}

		// Is this EOF?
		if (!data) {
			if (PATCH_DEBUG) fdprintf(2, "INEOF\n");

			// Does this hunk need to match EOF?
			if (!plist && matcheof) break;

			// File ended before we found a place for this hunk.
			fail_hunk();
			goto done;
		} else if (PATCH_DEBUG) fdprintf(2, "IN: %s\n", data);
		check = dlist_add(&buf, data);

		// Compare this line with next expected line of hunk.
		// todo: teach the strcmp() to ignore whitespace.

		// A match can fail because the next line doesn't match, or because
		// we hit the end of a hunk that needed EOF, and this isn't EOF.

		// If match failed, flush first line of buffered data and
		// recheck buffered data for a new match until we find one or run
		// out of buffer.

		for (;;) {
			if (!plist || strcmp(check->data, plist->data+1)) {
				// Match failed.  Write out first line of buffered data and
				// recheck remaining buffered data for a new match.

				if (PATCH_DEBUG)
					fdprintf(2, "NOT: %s\n", plist->data);

				TT.state = 3;
				check = TOY_llist_pop(&buf);
				check->prev->next = buf;
				buf->prev = check->prev;
				do_line(check);
				plist = TT.current_hunk;

				// If we've reached the end of the buffer without confirming a
				// match, read more lines.
				if (check==buf) {
					buf = 0;
					break;
				}
				check = buf;
			} else {
				if (PATCH_DEBUG)
					fdprintf(2, "MAYBE: %s\n", plist->data);
				// This line matches.  Advance plist, detect successful match.
				plist = plist->next;
				if (!plist && !matcheof) goto out;
				check = check->next;
				if (check == buf) break;
			}
		}
	}
out:
	// We have a match.  Emit changed data.
	TT.state = "-+"[reverse];
	TOY_llist_free(TT.current_hunk, do_line);
	TT.current_hunk = NULL;
	TT.state = 1;
done:
	if (buf) {
		buf->prev->next = NULL;
		TOY_llist_free(buf, do_line);
	}

	return TT.state;
}

// Read a patch file and find hunks, opening/creating/deleting files.
// Call apply_one_hunk() on each hunk.

// state 0: Not in a hunk, look for +++.
// state 1: Found +++ file indicator, look for @@
// state 2: In hunk: counting initial context lines
// state 3: In hunk: getting body

int patch_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int patch_main(int argc UNUSED_PARAM, char **argv)
{
	int opts;
	int reverse, state = 0;
	char *oldname = NULL, *newname = NULL;
	char *opt_p, *opt_i;

	INIT_TT();

	opts = getopt32(argv, FLAG_STR, &opt_p, &opt_i);
	reverse = opts & FLAG_REVERSE;
	TT.prefix = (opts & FLAG_PATHLEN) ? xatoi(opt_p) : 0; // can be negative!
	if (opts & FLAG_INPUT) TT.filepatch = xopen(opt_i, O_RDONLY);
	TT.filein = TT.fileout = -1;

	// Loop through the lines in the patch
	for(;;) {
		char *patchline;

		patchline = get_line(TT.filepatch);
		if (!patchline) break;

		// Other versions of patch accept damaged patches,
		// so we need to also.
		if (!*patchline) {
			free(patchline);
			patchline = xstrdup(" ");
		}

		// Are we assembling a hunk?
		if (state >= 2) {
			if (*patchline==' ' || *patchline=='+' || *patchline=='-') {
				dlist_add(&TT.current_hunk, patchline);

				if (*patchline != '+') TT.oldlen--;
				if (*patchline != '-') TT.newlen--;

				// Context line?
				if (*patchline==' ' && state==2) TT.context++;
				else state=3;

				// If we've consumed all expected hunk lines, apply the hunk.

				if (!TT.oldlen && !TT.newlen) state = apply_one_hunk();
				continue;
			}
			fail_hunk();
			state = 0;
			continue;
		}

		// Open a new file?
		if (!strncmp("--- ", patchline, 4) || !strncmp("+++ ", patchline, 4)) {
			char *s, **name = reverse ? &newname : &oldname;
			int i;

			if (*patchline == '+') {
				name = reverse ? &oldname : &newname;
				state = 1;
			}

			free(*name);
			finish_oldfile();

			// Trim date from end of filename (if any).  We don't care.
			for (s = patchline+4; *s && *s!='\t'; s++)
				if (*s=='\\' && s[1]) s++;
			i = atoi(s);
			if (i>1900 && i<=1970)
				*name = xstrdup("/dev/null");
			else {
				*s = 0;
				*name = xstrdup(patchline+4);
			}

			// We defer actually opening the file because svn produces broken
			// patches that don't signal they want to create a new file the
			// way the patch man page says, so you have to read the first hunk
			// and _guess_.

		// Start a new hunk?
		} else if (state == 1 && !strncmp("@@ -", patchline, 4)) {
			int i;

			i = sscanf(patchline+4, "%ld,%ld +%ld,%ld", &TT.oldline,
						&TT.oldlen, &TT.newline, &TT.newlen);
			if (i != 4)
				bb_error_msg_and_die("corrupt hunk %d at %ld", TT.hunknum, TT.linenum);

			TT.context = 0;
			state = 2;

			// If this is the first hunk, open the file.
			if (TT.filein == -1) {
				int oldsum, newsum, del = 0;
				char *s, *name;

				oldsum = TT.oldline + TT.oldlen;
				newsum = TT.newline + TT.newlen;

				name = reverse ? oldname : newname;

				// We're deleting oldname if new file is /dev/null (before -p)
				// or if new hunk is empty (zero context) after patching
				if (strcmp(name, "/dev/null") == 0 || !(reverse ? oldsum : newsum)) {
					name = reverse ? newname : oldname;
					del++;
				}

				// handle -p path truncation.
				for (i=0, s = name; *s;) {
					if ((option_mask32 & FLAG_PATHLEN) && TT.prefix == i) break;
					if (*(s++)=='/') {
						name = s;
						i++;
					}
				}

				if (del) {
					printf("removing %s\n", name);
					xunlink(name);
					state = 0;
				// If we've got a file to open, do so.
				} else if (!(option_mask32 & FLAG_PATHLEN) || i <= TT.prefix) {
					// If the old file was null, we're creating a new one.
					if (strcmp(oldname, "/dev/null") == 0 || !oldsum) {
						printf("creating %s\n", name);
						s = strrchr(name, '/');
						if (s) {
							*s = 0;
							xmkpath(name, -1);
							*s = '/';
						}
						TT.filein = xopen(name, O_CREAT|O_EXCL|O_RDWR);
					} else {
						printf("patching file %s\n", name);
						TT.filein = xopen(name, O_RDWR);
					}
					TT.fileout = copy_tempfile(TT.filein, name, &TT.tempname);
					TT.linenum = 0;
					TT.hunknum = 0;
				}
			}

			TT.hunknum++;

			continue;
		}

		// If we didn't continue above, discard this line.
		free(patchline);
	}

	finish_oldfile();

	if (ENABLE_FEATURE_CLEAN_UP) {
		close(TT.filepatch);
		free(oldname);
		free(newname);
	}

	return TT.exitval;
}
