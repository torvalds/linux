/* vi: set sw=4 ts=4:
 *
 * Apply a "universal" diff.
 * Adapted from toybox's patch implementation.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * see http://www.opengroup.org/onlinepubs/009695399/utilities/patch.html
 * (But only does -u, because who still cares about "ed"?)
 *
 * TODO:
 * -b backup
 * -l treat all whitespace as a single space
 * -d chdir first
 * -D define wrap #ifdef and #ifndef around changes
 * -o outfile output here instead of in place
 * -r rejectfile write rejected hunks to this file
 *
 * -f force (no questions asked)
 * -F fuzz (number, default 2)
 * [file] which file to patch
 */
//config:config PATCH
//config:	bool "patch (9.1 kb)"
//config:	default y
//config:	help
//config:	Apply a unified diff formatted patch.

//applet:IF_PATCH(APPLET(patch, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_PATCH) += patch.o

//usage:#define patch_trivial_usage
//usage:       "[OPTIONS] [ORIGFILE [PATCHFILE]]"
//usage:#define patch_full_usage "\n\n"
//usage:       "	-p N	Strip N leading components from file names"
//usage:     "\n	-i DIFF	Read DIFF instead of stdin"
//usage:     "\n	-R	Reverse patch"
//usage:     "\n	-N	Ignore already applied patches"
//usage:     "\n	-E	Remove output files if they become empty"
//usage:	IF_LONG_OPTS(
//usage:     "\n	--dry-run	Don't actually change files"
//usage:	)
/* -u "interpret as unified diff" is supported but not documented: this info is not useful for --help */
//usage:
//usage:#define patch_example_usage
//usage:       "$ patch -p1 < example.diff\n"
//usage:       "$ patch -p0 -i example.diff"

#include "libbb.h"

#define PATCH_DEBUG  0

// libbb candidate?

struct double_list {
	struct double_list *next;
	struct double_list *prev;
	char *data;
};

// Free all the elements of a linked list
// Call freeit() on each element before freeing it.
static void dlist_free(struct double_list *list, void (*freeit)(void *data))
{
	while (list) {
		void *pop = list;
		list = list->next;
		freeit(pop);
		// Bail out also if list is circular.
		if (list == pop) break;
	}
}

// Add an entry before "list" element in (circular) doubly linked list
static struct double_list *dlist_add(struct double_list **list, char *data)
{
	struct double_list *llist;
	struct double_list *line = xmalloc(sizeof(*line));

	line->data = data;
	llist = *list;
	if (llist) {
		struct double_list *p;
		line->next = llist;
		p = line->prev = llist->prev;
		// (list is circular, we assume p is never NULL)
		p->next = line;
		llist->prev = line;
	} else
		*list = line->next = line->prev = line;

	return line;
}


struct globals {
	char *infile;
	long prefix;

	struct double_list *current_hunk;

	long oldline, oldlen, newline, newlen;
	long linenum;
	int context, state, hunknum;
	int filein, fileout;
	char *tempname;

	int exitval;
};
#define TT (*ptr_to_globals)
#define INIT_TT() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(TT))); \
} while (0)


#define FLAG_STR "Rup:i:NEfg"
/* FLAG_REVERSE must be == 1! Code uses this fact. */
#define FLAG_REVERSE  (1 << 0)
#define FLAG_u        (1 << 1)
#define FLAG_PATHLEN  (1 << 2)
#define FLAG_INPUT    (1 << 3)
#define FLAG_IGNORE   (1 << 4)
#define FLAG_RMEMPTY  (1 << 5)
#define FLAG_f_unused (1 << 6)
#define FLAG_g_unused (1 << 7)
#define FLAG_dry_run  ((1 << 8) * ENABLE_LONG_OPTS)


// Dispose of a line of input, either by writing it out or discarding it.

// state < 2: just free
// state = 2: write whole line to stderr
// state = 3: write whole line to fileout
// state > 3: write line+1 to fileout when *line != state

static void do_line(void *data)
{
	struct double_list *dlist = data;

	if (TT.state>1 && *dlist->data != TT.state)
		fdprintf(TT.state == 2 ? 2 : TT.fileout,
			"%s\n", dlist->data+(TT.state>3 ? 1 : 0));

	if (PATCH_DEBUG) fdprintf(2, "DO %d: %s\n", TT.state, dlist->data);

	free(dlist->data);
	free(dlist);
}

static void finish_oldfile(void)
{
	if (TT.tempname) {
		// Copy the rest of the data and replace the original with the copy.
		char *temp;

		if (TT.filein != -1) {
			bb_copyfd_eof(TT.filein, TT.fileout);
			xclose(TT.filein);
		}
		xclose(TT.fileout);

		if (!ENABLE_LONG_OPTS || TT.tempname[0]) { /* not --dry-run? */
			temp = xstrdup(TT.tempname);
			temp[strlen(temp) - 6] = '\0';
			rename(TT.tempname, temp);
			free(temp);
			free(TT.tempname);
		}

		TT.tempname = NULL;
	}
	TT.fileout = TT.filein = -1;
}

static void fail_hunk(void)
{
	if (!TT.current_hunk) return;

	fdprintf(2, "Hunk %d FAILED %ld/%ld.\n", TT.hunknum, TT.oldline, TT.newline);
	TT.exitval = 1;

	// If we got to this point, we've seeked to the end.  Discard changes to
	// this file and advance to next file.

	TT.state = 2;
	TT.current_hunk->prev->next = NULL;
	dlist_free(TT.current_hunk, do_line);
	TT.current_hunk = NULL;

	// Abort the copy and delete the temporary file.
	close(TT.filein);
	close(TT.fileout);
	if (!ENABLE_LONG_OPTS || TT.tempname[0]) { /* not --dry-run? */
		unlink(TT.tempname);
		free(TT.tempname);
	}
	TT.tempname = NULL;

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
	/* Do we try "dummy" revert to check whether
	 * to silently skip this hunk? Used to implement -N.
	 */
	int dummy_revert = 0;

	// Break doubly linked list so we can use singly linked traversal function.
	TT.current_hunk->prev->next = NULL;

	// Match EOF if there aren't as many ending context lines as beginning
	for (plist = TT.current_hunk; plist; plist = plist->next) {
		if (plist->data[0]==' ') matcheof++;
		else matcheof = 0;
		if (PATCH_DEBUG) fdprintf(2, "HUNK:%s\n", plist->data);
	}
	matcheof = !matcheof || matcheof < TT.context;

	if (PATCH_DEBUG) fdprintf(2,"MATCHEOF=%c\n", matcheof ? 'Y' : 'N');

	// Loop through input data searching for this hunk.  Match all context
	// lines and all lines to be removed until we've found the end of a
	// complete hunk.
	plist = TT.current_hunk;
	buf = NULL;
	if (reverse ? TT.oldlen : TT.newlen) for (;;) {
//FIXME: this performs 1-byte reads:
		char *data = xmalloc_reads(TT.filein, NULL);

		TT.linenum++;

		// Figure out which line of hunk to compare with next.  (Skip lines
		// of the hunk we'd be adding.)
		while (plist && *plist->data == "+-"[reverse]) {
			if (data && strcmp(data, plist->data+1) == 0) {
				if (!backwarn) {
					backwarn = TT.linenum;
					if (option_mask32 & FLAG_IGNORE) {
						dummy_revert = 1;
						reverse ^= 1;
						continue;
					}
				}
			}
			plist = plist->next;
		}

		// Is this EOF?
		if (!data) {
			if (PATCH_DEBUG) fdprintf(2, "INEOF\n");

			// Does this hunk need to match EOF?
			if (!plist && matcheof) break;

			if (backwarn)
				fdprintf(2,"Possibly reversed hunk %d at %ld\n",
					TT.hunknum, TT.linenum);

			// File ended before we found a place for this hunk.
			fail_hunk();
			goto done;
		}

		if (PATCH_DEBUG) fdprintf(2, "IN: %s\n", data);
		check = dlist_add(&buf, data);

		// Compare this line with next expected line of hunk.
		// todo: teach the strcmp() to ignore whitespace.

		// A match can fail because the next line doesn't match, or because
		// we hit the end of a hunk that needed EOF, and this isn't EOF.

		// If match failed, flush first line of buffered data and
		// recheck buffered data for a new match until we find one or run
		// out of buffer.

		for (;;) {
			while (plist && *plist->data == "+-"[reverse]) {
				if (strcmp(check->data, plist->data+1) == 0
				 && !backwarn
				) {
					backwarn = TT.linenum;
					if (option_mask32 & FLAG_IGNORE) {
						dummy_revert = 1;
						reverse ^= 1;
					}
				}
				plist = plist->next;
			}
			if (!plist || strcmp(check->data, plist->data+1)) {
				// Match failed.  Write out first line of buffered data and
				// recheck remaining buffered data for a new match.

				if (PATCH_DEBUG)
					fdprintf(2, "NOT: %s\n", plist ? plist->data : "EOF");

				TT.state = 3;
				check = buf;
				buf = buf->next;
				check->prev->next = buf;
				buf->prev = check->prev;
				do_line(check);
				plist = TT.current_hunk;

				// If we've reached the end of the buffer without confirming a
				// match, read more lines.
				if (check == buf) {
					buf = NULL;
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
	TT.state = "-+"[reverse ^ dummy_revert];
	dlist_free(TT.current_hunk, do_line);
	TT.current_hunk = NULL;
	TT.state = 1;
done:
	if (buf) {
		buf->prev->next = NULL;
		dlist_free(buf, do_line);
	}

	return TT.state;
}

// Read a patch file and find hunks, opening/creating/deleting files.
// Call apply_one_hunk() on each hunk.

// state 0: Not in a hunk, look for +++.
// state 1: Found +++ file indicator, look for @@
// state 2: In hunk: counting initial context lines
// state 3: In hunk: getting body
// Like GNU patch, we don't require a --- line before the +++, and
// also allow the --- after the +++ line.

int patch_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int patch_main(int argc UNUSED_PARAM, char **argv)
{
	int opts;
	int reverse, state = 0;
	char *oldname = NULL, *newname = NULL;
	char *opt_p, *opt_i;
	long oldlen = oldlen; /* for compiler */
	long newlen = newlen; /* for compiler */

#if ENABLE_LONG_OPTS
	static const char patch_longopts[] ALIGN1 =
		"reverse\0"               No_argument       "R"
		"unified\0"               No_argument       "u"
		"strip\0"                 Required_argument "p"
		"input\0"                 Required_argument "i"
		"forward\0"               No_argument       "N"
# if ENABLE_DESKTOP
		"remove-empty-files\0"    No_argument       "E" /*ignored*/
		/* "debug"                Required_argument "x" */
# endif
		/* "Assume user knows what [s]he is doing, do not ask any questions": */
		"force\0"                 No_argument       "f" /*ignored*/
# if ENABLE_DESKTOP
		/* "Controls actions when a file is under RCS or SCCS control,
		 * and does not exist or is read-only and matches the default version,
		 * or when a file is under ClearCase control and does not exist..."
		 * IOW: rather obscure option.
		 * But Gentoo's portage does use -g0
		 */
		"get\0"                   Required_argument "g" /*ignored*/
# endif
		"dry-run\0"               No_argument       "\xfd"
# if ENABLE_DESKTOP
		"backup-if-mismatch\0"    No_argument       "\xfe" /*ignored*/
		"no-backup-if-mismatch\0" No_argument       "\xff" /*ignored*/
# endif
		;
#endif

	INIT_TT();

#if ENABLE_LONG_OPTS
	opts = getopt32long(argv, FLAG_STR, patch_longopts, &opt_p, &opt_i);
#else
	opts = getopt32(argv, FLAG_STR, &opt_p, &opt_i);
#endif
	//bb_error_msg_and_die("opts:%x", opts);

	argv += optind;
	reverse = opts & FLAG_REVERSE;
	TT.prefix = (opts & FLAG_PATHLEN) ? xatoi(opt_p) : 0; // can be negative!
	TT.filein = TT.fileout = -1;
	if (opts & FLAG_INPUT) {
		xmove_fd(xopen_stdin(opt_i), STDIN_FILENO);
	} else {
		if (argv[0] && argv[1]) {
			xmove_fd(xopen_stdin(argv[1]), STDIN_FILENO);
		}
	}

	// Loop through the lines in the patch
	for(;;) {
		char *patchline;

		patchline = xmalloc_fgetline(stdin);
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

				if (*patchline != '+') oldlen--;
				if (*patchline != '-') newlen--;

				// Context line?
				if (*patchline==' ' && state==2) TT.context++;
				else state=3;

				// If we've consumed all expected hunk lines, apply the hunk.

				if (!oldlen && !newlen) state = apply_one_hunk();
				continue;
			}
			fail_hunk();
			state = 0;
			continue;
		}

		// Open a new file?
		if (is_prefixed_with(patchline, "--- ") || is_prefixed_with(patchline, "+++ ")) {
			char *s, **name = reverse ? &newname : &oldname;
			int i;

			if (*patchline == '+') {
				name = reverse ? &oldname : &newname;
				state = 1;
			}

			finish_oldfile();

			if (!argv[0]) {
				free(*name);
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
			}

			// We defer actually opening the file because svn produces broken
			// patches that don't signal they want to create a new file the
			// way the patch man page says, so you have to read the first hunk
			// and _guess_.

		// Start a new hunk?  Usually @@ -oldline,oldlen +newline,newlen @@
		// but a missing ,value means the value is 1.
		} else if (state == 1 && is_prefixed_with(patchline, "@@ -")) {
			int i;
			char *s = patchline+4;

			// Read oldline[,oldlen] +newline[,newlen]

			TT.oldlen = oldlen = TT.newlen = newlen = 1;
			TT.oldline = strtol(s, &s, 10);
			if (*s == ',') TT.oldlen = oldlen = strtol(s+1, &s, 10);
			TT.newline = strtol(s+2, &s, 10);
			if (*s == ',') TT.newlen = newlen = strtol(s+1, &s, 10);

			if (oldlen < 1 && newlen < 1)
				bb_error_msg_and_die("Really? %s", patchline);

			TT.context = 0;
			state = 2;

			// If the --- line is missing or malformed, either oldname
			// or (for -R) newname could be NULL -- but not both.  Like
			// GNU patch, proceed based on the +++ line, and avoid SEGVs.
			if (!oldname)
				oldname = xstrdup("MISSING_FILENAME");
			if (!newname)
				newname = xstrdup("MISSING_FILENAME");

			// If this is the first hunk, open the file.
			if (TT.filein == -1) {
				int oldsum, newsum, empty = 0;
				char *name;

				oldsum = TT.oldline + oldlen;
				newsum = TT.newline + newlen;

				name = reverse ? oldname : newname;

				// We're deleting oldname if new file is /dev/null (before -p)
				// or if new hunk is empty (zero context) after patching
				if (strcmp(name, "/dev/null") == 0 || !(reverse ? oldsum : newsum)) {
					name = reverse ? newname : oldname;
					empty = 1;
				}

				// Handle -p path truncation.
				for (i = 0, s = name; *s;) {
					if ((option_mask32 & FLAG_PATHLEN) && TT.prefix == i)
						break;
					if (*s++ != '/')
						continue;
					while (*s == '/')
						s++;
					i++;
					name = s;
				}
				// If "patch FILE_TO_PATCH", completely ignore name from patch
				if (argv[0])
					name = argv[0];

				if (empty) {
					// File is empty after the patches have been applied
					state = 0;
					if (option_mask32 & FLAG_RMEMPTY) {
						// If flag -E or --remove-empty-files is set
						printf("removing %s\n", name);
						if (!(opts & FLAG_dry_run))
							xunlink(name);
					} else {
						printf("patching file %s\n", name);
						if (!(opts & FLAG_dry_run))
							xclose(xopen(name, O_WRONLY | O_TRUNC));
					}
				// If we've got a file to open, do so.
				} else if (!(option_mask32 & FLAG_PATHLEN) || i <= TT.prefix) {
					struct stat statbuf;

					// If the old file was null, we're creating a new one.
					if (strcmp(oldname, "/dev/null") == 0 || !oldsum) {
						printf("creating %s\n", name);
						if (!(opts & FLAG_dry_run)) {
							s = strrchr(name, '/');
							if (s) {
								*s = '\0';
								bb_make_directory(name, -1, FILEUTILS_RECUR);
								*s = '/';
							}
							TT.filein = xopen(name, O_CREAT|O_EXCL|O_RDWR);
						} else {
							TT.filein = xopen("/dev/null", O_RDONLY);
						}
					} else {
						printf("patching file %s\n", name);
						TT.filein = xopen(name, O_RDONLY);
					}

					if (!(opts & FLAG_dry_run)) {
						TT.tempname = xasprintf("%sXXXXXX", name);
						TT.fileout = xmkstemp(TT.tempname);
						// Set permissions of output file
						fstat(TT.filein, &statbuf);
						fchmod(TT.fileout, statbuf.st_mode);
					} else {
						TT.tempname = (char*)"";
						TT.fileout = xopen("/dev/null", O_WRONLY);
					}
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
		free(oldname);
		free(newname);
	}

	return TT.exitval;
}
