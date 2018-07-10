/* vi: set sw=4 ts=4: */
/*
 * Mini grep implementation for busybox using libc regex.
 *
 * Copyright (C) 1999,2000,2001 by Lineo, inc. and Mark Whitley
 * Copyright (C) 1999,2000,2001 by Mark Whitley <markw@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/* BB_AUDIT SUSv3 defects - unsupported option -x "match whole line only". */
/* BB_AUDIT GNU defects - always acts as -a.  */
/* http://www.opengroup.org/onlinepubs/007904975/utilities/grep.html */
/*
 * 2004,2006 (C) Vladimir Oleynik <dzo@simtreas.ru> -
 * correction "-e pattern1 -e pattern2" logic and more optimizations.
 * precompiled regex
 *
 * (C) 2006 Jac Goudsmit added -o option
 */
//config:config GREP
//config:	bool "grep (8.5 kb)"
//config:	default y
//config:	help
//config:	grep is used to search files for a specified pattern.
//config:
//config:config EGREP
//config:	bool "egrep (7.6 kb)"
//config:	default y
//config:	help
//config:	Alias to "grep -E".
//config:
//config:config FGREP
//config:	bool "fgrep (7.6 kb)"
//config:	default y
//config:	help
//config:	Alias to "grep -F".
//config:
//config:config FEATURE_GREP_CONTEXT
//config:	bool "Enable before and after context flags (-A, -B and -C)"
//config:	default y
//config:	depends on GREP || EGREP || FGREP
//config:	help
//config:	Print the specified number of leading (-B) and/or trailing (-A)
//config:	context surrounding our matching lines.
//config:	Print the specified number of context lines (-C).

//applet:IF_GREP(APPLET(grep, BB_DIR_BIN, BB_SUID_DROP))
//                APPLET_ODDNAME:name   main  location    suid_type     help
//applet:IF_EGREP(APPLET_ODDNAME(egrep, grep, BB_DIR_BIN, BB_SUID_DROP, egrep))
//applet:IF_FGREP(APPLET_ODDNAME(fgrep, grep, BB_DIR_BIN, BB_SUID_DROP, fgrep))

//kbuild:lib-$(CONFIG_GREP) += grep.o
//kbuild:lib-$(CONFIG_EGREP) += grep.o
//kbuild:lib-$(CONFIG_FGREP) += grep.o

#include "libbb.h"
#include "common_bufsiz.h"
#include "xregex.h"


/* options */
//usage:#define grep_trivial_usage
//usage:       "[-HhnlLoqvsriwFE"
//usage:	IF_EXTRA_COMPAT("z")
//usage:       "] [-m N] "
//usage:	IF_FEATURE_GREP_CONTEXT("[-A/B/C N] ")
//usage:       "PATTERN/-e PATTERN.../-f FILE [FILE]..."
//usage:#define grep_full_usage "\n\n"
//usage:       "Search for PATTERN in FILEs (or stdin)\n"
//usage:     "\n	-H	Add 'filename:' prefix"
//usage:     "\n	-h	Do not add 'filename:' prefix"
//usage:     "\n	-n	Add 'line_no:' prefix"
//usage:     "\n	-l	Show only names of files that match"
//usage:     "\n	-L	Show only names of files that don't match"
//usage:     "\n	-c	Show only count of matching lines"
//usage:     "\n	-o	Show only the matching part of line"
//usage:     "\n	-q	Quiet. Return 0 if PATTERN is found, 1 otherwise"
//usage:     "\n	-v	Select non-matching lines"
//usage:     "\n	-s	Suppress open and read errors"
//usage:     "\n	-r	Recurse"
//usage:     "\n	-i	Ignore case"
//usage:     "\n	-w	Match whole words only"
//usage:     "\n	-x	Match whole lines only"
//usage:     "\n	-F	PATTERN is a literal (not regexp)"
//usage:     "\n	-E	PATTERN is an extended regexp"
//usage:	IF_EXTRA_COMPAT(
//usage:     "\n	-z	Input is NUL terminated"
//usage:	)
//usage:     "\n	-m N	Match up to N times per file"
//usage:	IF_FEATURE_GREP_CONTEXT(
//usage:     "\n	-A N	Print N lines of trailing context"
//usage:     "\n	-B N	Print N lines of leading context"
//usage:     "\n	-C N	Same as '-A N -B N'"
//usage:	)
//usage:     "\n	-e PTRN	Pattern to match"
//usage:     "\n	-f FILE	Read pattern from file"
//usage:
//usage:#define grep_example_usage
//usage:       "$ grep root /etc/passwd\n"
//usage:       "root:x:0:0:root:/root:/bin/bash\n"
//usage:       "$ grep ^[rR]oo. /etc/passwd\n"
//usage:       "root:x:0:0:root:/root:/bin/bash\n"
//usage:
//usage:#define egrep_trivial_usage NOUSAGE_STR
//usage:#define egrep_full_usage ""
//usage:#define fgrep_trivial_usage NOUSAGE_STR
//usage:#define fgrep_full_usage ""

/* -e,-f are lists; -m,-A,-B,-C have numeric param */
#define OPTSTR_GREP \
	"lnqvscFiHhe:*f:*Lorm:+wx" \
	IF_FEATURE_GREP_CONTEXT("A:+B:+C:+") \
	"E" \
	IF_EXTRA_COMPAT("z") \
	"aI"
/* ignored: -a "assume all files to be text" */
/* ignored: -I "assume binary files have no matches" */
enum {
	OPTBIT_l, /* list matched file names only */
	OPTBIT_n, /* print line# */
	OPTBIT_q, /* quiet - exit(EXIT_SUCCESS) of first match */
	OPTBIT_v, /* invert the match, to select non-matching lines */
	OPTBIT_s, /* suppress errors about file open errors */
	OPTBIT_c, /* count matches per file (suppresses normal output) */
	OPTBIT_F, /* literal match */
	OPTBIT_i, /* case-insensitive */
	OPTBIT_H, /* force filename display */
	OPTBIT_h, /* inhibit filename display */
	OPTBIT_e, /* -e PATTERN */
	OPTBIT_f, /* -f FILE_WITH_PATTERNS */
	OPTBIT_L, /* list unmatched file names only */
	OPTBIT_o, /* show only matching parts of lines */
	OPTBIT_r, /* recurse dirs */
	OPTBIT_m, /* -m MAX_MATCHES */
	OPTBIT_w, /* -w whole word match */
	OPTBIT_x, /* -x whole line match */
	IF_FEATURE_GREP_CONTEXT(    OPTBIT_A ,) /* -A NUM: after-match context */
	IF_FEATURE_GREP_CONTEXT(    OPTBIT_B ,) /* -B NUM: before-match context */
	IF_FEATURE_GREP_CONTEXT(    OPTBIT_C ,) /* -C NUM: -A and -B combined */
	OPTBIT_E, /* extended regexp */
	IF_EXTRA_COMPAT(            OPTBIT_z ,) /* input is NUL terminated */
	OPT_l = 1 << OPTBIT_l,
	OPT_n = 1 << OPTBIT_n,
	OPT_q = 1 << OPTBIT_q,
	OPT_v = 1 << OPTBIT_v,
	OPT_s = 1 << OPTBIT_s,
	OPT_c = 1 << OPTBIT_c,
	OPT_F = 1 << OPTBIT_F,
	OPT_i = 1 << OPTBIT_i,
	OPT_H = 1 << OPTBIT_H,
	OPT_h = 1 << OPTBIT_h,
	OPT_e = 1 << OPTBIT_e,
	OPT_f = 1 << OPTBIT_f,
	OPT_L = 1 << OPTBIT_L,
	OPT_o = 1 << OPTBIT_o,
	OPT_r = 1 << OPTBIT_r,
	OPT_m = 1 << OPTBIT_m,
	OPT_w = 1 << OPTBIT_w,
	OPT_x = 1 << OPTBIT_x,
	OPT_A = IF_FEATURE_GREP_CONTEXT(    (1 << OPTBIT_A)) + 0,
	OPT_B = IF_FEATURE_GREP_CONTEXT(    (1 << OPTBIT_B)) + 0,
	OPT_C = IF_FEATURE_GREP_CONTEXT(    (1 << OPTBIT_C)) + 0,
	OPT_E = 1 << OPTBIT_E,
	OPT_z = IF_EXTRA_COMPAT(            (1 << OPTBIT_z)) + 0,
};

#define PRINT_FILES_WITH_MATCHES    (option_mask32 & OPT_l)
#define PRINT_LINE_NUM              (option_mask32 & OPT_n)
#define BE_QUIET                    (option_mask32 & OPT_q)
#define SUPPRESS_ERR_MSGS           (option_mask32 & OPT_s)
#define PRINT_MATCH_COUNTS          (option_mask32 & OPT_c)
#define FGREP_FLAG                  (option_mask32 & OPT_F)
#define PRINT_FILES_WITHOUT_MATCHES (option_mask32 & OPT_L)
#define NUL_DELIMITED               (option_mask32 & OPT_z)

struct globals {
	int max_matches;
#if !ENABLE_EXTRA_COMPAT
	int reflags;
#else
	RE_TRANSLATE_TYPE case_fold; /* RE_TRANSLATE_TYPE is [[un]signed] char* */
#endif
	smalluint invert_search;
	smalluint print_filename;
	smalluint open_errors;
#if ENABLE_FEATURE_GREP_CONTEXT
	smalluint did_print_line;
	int lines_before;
	int lines_after;
	char **before_buf;
	IF_EXTRA_COMPAT(size_t *before_buf_size;)
	int last_line_printed;
#endif
	/* globals used internally */
	llist_t *pattern_head;   /* growable list of patterns to match */
	const char *cur_file;    /* the current file we are reading */
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { \
	setup_common_bufsiz(); \
	BUILD_BUG_ON(sizeof(G) > COMMON_BUFSIZE); \
} while (0)
#define max_matches       (G.max_matches         )
#if !ENABLE_EXTRA_COMPAT
# define reflags          (G.reflags             )
#else
# define case_fold        (G.case_fold           )
/* http://www.delorie.com/gnu/docs/regex/regex_46.html */
# define reflags           re_syntax_options
# undef REG_NOSUB
# undef REG_EXTENDED
# undef REG_ICASE
# define REG_NOSUB    bug:is:here /* should not be used */
/* Just RE_SYNTAX_EGREP is not enough, need to enable {n[,[m]]} too */
# define REG_EXTENDED (RE_SYNTAX_EGREP | RE_INTERVALS | RE_NO_BK_BRACES)
# define REG_ICASE    bug:is:here /* should not be used */
#endif
#define invert_search     (G.invert_search       )
#define print_filename    (G.print_filename      )
#define open_errors       (G.open_errors         )
#define did_print_line    (G.did_print_line      )
#define lines_before      (G.lines_before        )
#define lines_after       (G.lines_after         )
#define before_buf        (G.before_buf          )
#define before_buf_size   (G.before_buf_size     )
#define last_line_printed (G.last_line_printed   )
#define pattern_head      (G.pattern_head        )
#define cur_file          (G.cur_file            )


typedef struct grep_list_data_t {
	char *pattern;
/* for GNU regex, matched_range must be persistent across grep_file() calls */
#if !ENABLE_EXTRA_COMPAT
	regex_t compiled_regex;
	regmatch_t matched_range;
#else
	struct re_pattern_buffer compiled_regex;
	struct re_registers matched_range;
#endif
#define ALLOCATED 1
#define COMPILED 2
	int flg_mem_allocated_compiled;
} grep_list_data_t;

#if !ENABLE_EXTRA_COMPAT
#define print_line(line, line_len, linenum, decoration) \
	print_line(line, linenum, decoration)
#endif
static void print_line(const char *line, size_t line_len, int linenum, char decoration)
{
#if ENABLE_FEATURE_GREP_CONTEXT
	/* Happens when we go to next file, immediately hit match
	 * and try to print prev context... from prev file! Don't do it */
	if (linenum < 1)
		return;
	/* possibly print the little '--' separator */
	if ((lines_before || lines_after) && did_print_line
	 && last_line_printed != linenum - 1
	) {
		puts("--");
	}
	/* guard against printing "--" before first line of first file */
	did_print_line = 1;
	last_line_printed = linenum;
#endif
	if (print_filename)
		printf("%s%c", cur_file, decoration);
	if (PRINT_LINE_NUM)
		printf("%i%c", linenum, decoration);
	/* Emulate weird GNU grep behavior with -ov */
	if ((option_mask32 & (OPT_v|OPT_o)) != (OPT_v|OPT_o)) {
#if !ENABLE_EXTRA_COMPAT
		puts(line);
#else
		fwrite(line, 1, line_len, stdout);
		putchar(NUL_DELIMITED ? '\0' : '\n');
#endif
	}
}

#if ENABLE_EXTRA_COMPAT
/* Unlike getline, this one removes trailing '\n' */
static ssize_t FAST_FUNC bb_getline(char **line_ptr, size_t *line_alloc_len, FILE *file)
{
	ssize_t res_sz;
	char *line;
	int delim = (NUL_DELIMITED ? '\0' : '\n');

	res_sz = getdelim(line_ptr, line_alloc_len, delim, file);
	line = *line_ptr;

	if (res_sz > 0) {
		if (line[res_sz - 1] == delim)
			line[--res_sz] = '\0';
	} else {
		free(line); /* uclibc allocates a buffer even on EOF. WTF? */
	}
	return res_sz;
}
#endif

static int grep_file(FILE *file)
{
	smalluint found;
	int linenum = 0;
	int nmatches = 0;
#if !ENABLE_EXTRA_COMPAT
	char *line;
#else
	char *line = NULL;
	ssize_t line_len;
	size_t line_alloc_len;
# define rm_so start[0]
# define rm_eo end[0]
#endif
#if ENABLE_FEATURE_GREP_CONTEXT
	int print_n_lines_after = 0;
	int curpos = 0; /* track where we are in the circular 'before' buffer */
	int idx = 0; /* used for iteration through the circular buffer */
#else
	enum { print_n_lines_after = 0 };
#endif

	while (
#if !ENABLE_EXTRA_COMPAT
		(line = xmalloc_fgetline(file)) != NULL
#else
		(line_len = bb_getline(&line, &line_alloc_len, file)) >= 0
#endif
	) {
		llist_t *pattern_ptr = pattern_head;
		grep_list_data_t *gl = gl; /* for gcc */

		linenum++;
		found = 0;
		while (pattern_ptr) {
			gl = (grep_list_data_t *)pattern_ptr->data;
			if (FGREP_FLAG) {
				char *match;
				char *str = line;
 opt_f_again:
				match = ((option_mask32 & OPT_i)
					? strcasestr(str, gl->pattern)
					: strstr(str, gl->pattern)
					);
				if (match) {
					if (option_mask32 & OPT_x) {
						if (match != str)
							goto opt_f_not_found;
						if (str[strlen(gl->pattern)] != '\0')
							goto opt_f_not_found;
					} else
					if (option_mask32 & OPT_w) {
						char c = (match != line) ? match[-1] : ' ';
						if (!isalnum(c) && c != '_') {
							c = match[strlen(gl->pattern)];
							if (!c || (!isalnum(c) && c != '_'))
								goto opt_f_found;
						}
						str = match + 1;
						goto opt_f_again;
					}
 opt_f_found:
					found = 1;
 opt_f_not_found: ;
				}
			} else {
#if ENABLE_EXTRA_COMPAT
				unsigned start_pos;
#else
				int match_flg;
#endif
				char *match_at;

				if (!(gl->flg_mem_allocated_compiled & COMPILED)) {
					gl->flg_mem_allocated_compiled |= COMPILED;
#if !ENABLE_EXTRA_COMPAT
					xregcomp(&gl->compiled_regex, gl->pattern, reflags);
#else
					memset(&gl->compiled_regex, 0, sizeof(gl->compiled_regex));
					gl->compiled_regex.translate = case_fold; /* for -i */
					if (re_compile_pattern(gl->pattern, strlen(gl->pattern), &gl->compiled_regex))
						bb_error_msg_and_die("bad regex '%s'", gl->pattern);
#endif
				}
#if !ENABLE_EXTRA_COMPAT
				gl->matched_range.rm_so = 0;
				gl->matched_range.rm_eo = 0;
				match_flg = 0;
#else
				start_pos = 0;
#endif
				match_at = line;
 opt_w_again:
//bb_error_msg("'%s' start_pos:%d line_len:%d", match_at, start_pos, line_len);
				if (
#if !ENABLE_EXTRA_COMPAT
					regexec(&gl->compiled_regex, match_at, 1, &gl->matched_range, match_flg) == 0
#else
					re_search(&gl->compiled_regex, match_at, line_len,
							start_pos, /*range:*/ line_len,
							&gl->matched_range) >= 0
#endif
				) {
					if (option_mask32 & OPT_x) {
						found = (gl->matched_range.rm_so == 0
						         && match_at[gl->matched_range.rm_eo] == '\0');
					} else
					if (!(option_mask32 & OPT_w)) {
						found = 1;
					} else {
						char c = ' ';
						if (match_at > line || gl->matched_range.rm_so != 0) {
							c = match_at[gl->matched_range.rm_so - 1];
						}
						if (!isalnum(c) && c != '_') {
							c = match_at[gl->matched_range.rm_eo];
						}
						if (!isalnum(c) && c != '_') {
							found = 1;
						} else {
			/*
			 * Why check gl->matched_range.rm_eo?
			 * Zero-length match makes -w skip the line:
			 * "echo foo | grep ^" prints "foo",
			 * "echo foo | grep -w ^" prints nothing.
			 * Without such check, we can loop forever.
			 */
#if !ENABLE_EXTRA_COMPAT
							if (gl->matched_range.rm_eo != 0) {
								match_at += gl->matched_range.rm_eo;
								match_flg |= REG_NOTBOL;
								goto opt_w_again;
							}
#else
							if (gl->matched_range.rm_eo > start_pos) {
								start_pos = gl->matched_range.rm_eo;
								goto opt_w_again;
							}
#endif
						}
					}
				}
			}
			/* If it's non-inverted search, we can stop
			 * at first match */
			if (found && !invert_search)
				goto do_found;
			pattern_ptr = pattern_ptr->link;
		} /* while (pattern_ptr) */

		if (found ^ invert_search) {
 do_found:
			/* keep track of matches */
			nmatches++;

			/* quiet/print (non)matching file names only? */
			if (option_mask32 & (OPT_q|OPT_l|OPT_L)) {
				free(line); /* we don't need line anymore */
				if (BE_QUIET) {
					/* manpage says about -q:
					 * "exit immediately with zero status
					 * if any match is found,
					 * even if errors were detected" */
					exit(EXIT_SUCCESS);
				}
				/* if we're just printing filenames, we stop after the first match */
				if (PRINT_FILES_WITH_MATCHES) {
					puts(cur_file);
					/* fall through to "return 1" */
				}
				/* OPT_L aka PRINT_FILES_WITHOUT_MATCHES: return early */
				return 1; /* one match */
			}

#if ENABLE_FEATURE_GREP_CONTEXT
			/* Were we printing context and saw next (unwanted) match? */
			if ((option_mask32 & OPT_m) && nmatches > max_matches)
				break;
#endif

			/* print the matched line */
			if (PRINT_MATCH_COUNTS == 0) {
#if ENABLE_FEATURE_GREP_CONTEXT
				int prevpos = (curpos == 0) ? lines_before - 1 : curpos - 1;

				/* if we were told to print 'before' lines and there is at least
				 * one line in the circular buffer, print them */
				if (lines_before && before_buf[prevpos] != NULL) {
					int first_buf_entry_line_num = linenum - lines_before;

					/* advance to the first entry in the circular buffer, and
					 * figure out the line number is of the first line in the
					 * buffer */
					idx = curpos;
					while (before_buf[idx] == NULL) {
						idx = (idx + 1) % lines_before;
						first_buf_entry_line_num++;
					}

					/* now print each line in the buffer, clearing them as we go */
					while (before_buf[idx] != NULL) {
						print_line(before_buf[idx], before_buf_size[idx], first_buf_entry_line_num, '-');
						free(before_buf[idx]);
						before_buf[idx] = NULL;
						idx = (idx + 1) % lines_before;
						first_buf_entry_line_num++;
					}
				}

				/* make a note that we need to print 'after' lines */
				print_n_lines_after = lines_after;
#endif
				if (option_mask32 & OPT_o) {
					if (FGREP_FLAG) {
						/* -Fo just prints the pattern
						 * (unless -v: -Fov doesn't print anything at all) */
						if (found)
							print_line(gl->pattern, strlen(gl->pattern), linenum, ':');
					} else while (1) {
						unsigned start = gl->matched_range.rm_so;
						unsigned end = gl->matched_range.rm_eo;
						unsigned len = end - start;
						char old = line[end];
						line[end] = '\0';
						/* Empty match is not printed: try "echo test | grep -o ''" */
						if (len != 0)
							print_line(line + start, len, linenum, ':');
						if (old == '\0')
							break;
						line[end] = old;
						if (len == 0)
							end++;
#if !ENABLE_EXTRA_COMPAT
						if (regexec(&gl->compiled_regex, line + end,
								1, &gl->matched_range, REG_NOTBOL) != 0)
							break;
						gl->matched_range.rm_so += end;
						gl->matched_range.rm_eo += end;
#else
						if (re_search(&gl->compiled_regex, line, line_len,
								end, line_len - end,
								&gl->matched_range) < 0)
							break;
#endif
					}
				} else {
					print_line(line, line_len, linenum, ':');
				}
			}
		}
#if ENABLE_FEATURE_GREP_CONTEXT
		else { /* no match */
			/* if we need to print some context lines after the last match, do so */
			if (print_n_lines_after) {
				print_line(line, strlen(line), linenum, '-');
				print_n_lines_after--;
			} else if (lines_before) {
				/* Add the line to the circular 'before' buffer */
				free(before_buf[curpos]);
				before_buf[curpos] = line;
				IF_EXTRA_COMPAT(before_buf_size[curpos] = line_len;)
				curpos = (curpos + 1) % lines_before;
				/* avoid free(line) - we took the line */
				line = NULL;
			}
		}

#endif /* ENABLE_FEATURE_GREP_CONTEXT */
#if !ENABLE_EXTRA_COMPAT
		free(line);
#endif
		/* Did we print all context after last requested match? */
		if ((option_mask32 & OPT_m)
		 && !print_n_lines_after
		 && nmatches == max_matches
		) {
			break;
		}
	} /* while (read line) */

	/* special-case file post-processing for options where we don't print line
	 * matches, just filenames and possibly match counts */

	/* grep -c: print [filename:]count, even if count is zero */
	if (PRINT_MATCH_COUNTS) {
		if (print_filename)
			printf("%s:", cur_file);
		printf("%d\n", nmatches);
	}

	/* grep -L: print just the filename */
	if (PRINT_FILES_WITHOUT_MATCHES) {
		/* nmatches is zero, no need to check it:
		 * we return 1 early if we detected a match
		 * and PRINT_FILES_WITHOUT_MATCHES is set */
		puts(cur_file);
	}

	return nmatches;
}

#if ENABLE_FEATURE_CLEAN_UP
#define new_grep_list_data(p, m) add_grep_list_data(p, m)
static char *add_grep_list_data(char *pattern, int flg_used_mem)
#else
#define new_grep_list_data(p, m) add_grep_list_data(p)
static char *add_grep_list_data(char *pattern)
#endif
{
	grep_list_data_t *gl = xzalloc(sizeof(*gl));
	gl->pattern = pattern;
#if ENABLE_FEATURE_CLEAN_UP
	gl->flg_mem_allocated_compiled = flg_used_mem;
#else
	/*gl->flg_mem_allocated_compiled = 0;*/
#endif
	return (char *)gl;
}

static void load_regexes_from_file(llist_t *fopt)
{
	while (fopt) {
		char *line;
		FILE *fp;
		llist_t *cur = fopt;
		char *ffile = cur->data;

		fopt = cur->link;
		free(cur);
		fp = xfopen_stdin(ffile);
		while ((line = xmalloc_fgetline(fp)) != NULL) {
			llist_add_to(&pattern_head,
				new_grep_list_data(line, ALLOCATED));
		}
		fclose_if_not_stdin(fp);
	}
}

static int FAST_FUNC file_action_grep(const char *filename,
			struct stat *statbuf,
			void* matched,
			int depth UNUSED_PARAM)
{
	FILE *file;

	/* If we are given a link to a directory, we should bail out now, rather
	 * than trying to open the "file" and hoping getline gives us nothing,
	 * since that is not portable across operating systems (FreeBSD for
	 * example will return the raw directory contents). */
	if (S_ISLNK(statbuf->st_mode)) {
		struct stat sb;
		if (stat(filename, &sb) != 0) {
			if (!SUPPRESS_ERR_MSGS)
				bb_simple_perror_msg(filename);
			return 0;
		}
		if (S_ISDIR(sb.st_mode))
			return 1;
	}

	file = fopen_for_read(filename);
	if (file == NULL) {
		if (!SUPPRESS_ERR_MSGS)
			bb_simple_perror_msg(filename);
		open_errors = 1;
		return 0;
	}
	cur_file = filename;
	*(int*)matched += grep_file(file);
	fclose(file);
	return 1;
}

static int grep_dir(const char *dir)
{
	int matched = 0;
	recursive_action(dir,
		/* recurse=yes */ ACTION_RECURSE |
		/* followLinks=command line only */ ACTION_FOLLOWLINKS_L0 |
		/* depthFirst=yes */ ACTION_DEPTHFIRST,
		/* fileAction= */ file_action_grep,
		/* dirAction= */ NULL,
		/* userData= */ &matched,
		/* depth= */ 0);
	return matched;
}

int grep_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int grep_main(int argc UNUSED_PARAM, char **argv)
{
	FILE *file;
	int matched;
	llist_t *fopt = NULL;
#if ENABLE_FEATURE_GREP_CONTEXT
	int Copt, opts;
#endif
	INIT_G();

	/* For grep, exitcode of 1 is "not found". Other errors are 2: */
	xfunc_error_retval = 2;

	/* do normal option parsing */
#if ENABLE_FEATURE_GREP_CONTEXT
	/* -H unsets -h; -C unsets -A,-B */
	opts = getopt32(argv,
		"^" OPTSTR_GREP "\0" "H-h:C-AB",
		&pattern_head, &fopt, &max_matches,
		&lines_after, &lines_before, &Copt);

	if (opts & OPT_C) {
		/* -C unsets prev -A and -B, but following -A or -B
		 * may override it */
		if (!(opts & OPT_A)) /* not overridden */
			lines_after = Copt;
		if (!(opts & OPT_B)) /* not overridden */
			lines_before = Copt;
	}
	/* sanity checks */
	if (opts & (OPT_c|OPT_q|OPT_l|OPT_L)) {
		option_mask32 &= ~OPT_n;
		lines_before = 0;
		lines_after = 0;
	} else if (lines_before > 0) {
		if (lines_before > INT_MAX / sizeof(long long))
			lines_before = INT_MAX / sizeof(long long);
		/* overflow in (lines_before * sizeof(x)) is prevented (above) */
		before_buf = xzalloc(lines_before * sizeof(before_buf[0]));
		IF_EXTRA_COMPAT(before_buf_size = xzalloc(lines_before * sizeof(before_buf_size[0]));)
	}
#else
	/* with auto sanity checks */
	getopt32(argv, "^" OPTSTR_GREP "\0" "H-h:c-n:q-n:l-n:", // why trailing ":"?
		&pattern_head, &fopt, &max_matches);
#endif
	invert_search = ((option_mask32 & OPT_v) != 0); /* 0 | 1 */

	{	/* convert char **argv to grep_list_data_t */
		llist_t *cur;
		for (cur = pattern_head; cur; cur = cur->link)
			cur->data = new_grep_list_data(cur->data, 0);
	}
	if (option_mask32 & OPT_f) {
		load_regexes_from_file(fopt);
		if (!pattern_head) { /* -f EMPTY_FILE? */
			/* GNU grep treats it as "nothing matches" */
			llist_add_to(&pattern_head, new_grep_list_data((char*) "", 0));
			invert_search ^= 1;
		}
	}

	if (ENABLE_FGREP && applet_name[0] == 'f')
		option_mask32 |= OPT_F;

#if !ENABLE_EXTRA_COMPAT
	if (!(option_mask32 & (OPT_o | OPT_w | OPT_x)))
		reflags = REG_NOSUB;
#endif

	if ((ENABLE_EGREP && applet_name[0] == 'e')
	 || (option_mask32 & OPT_E)
	) {
		reflags |= REG_EXTENDED;
	}
#if ENABLE_EXTRA_COMPAT
	else {
		reflags = RE_SYNTAX_GREP;
	}
#endif

	if (option_mask32 & OPT_i) {
#if !ENABLE_EXTRA_COMPAT
		reflags |= REG_ICASE;
#else
		int i;
		case_fold = xmalloc(256);
		for (i = 0; i < 256; i++)
			case_fold[i] = (unsigned char)i;
		for (i = 'a'; i <= 'z'; i++)
			case_fold[i] = (unsigned char)(i - ('a' - 'A'));
#endif
	}

	argv += optind;

	/* if we didn't get a pattern from -e and no command file was specified,
	 * first parameter should be the pattern. no pattern, no worky */
	if (pattern_head == NULL) {
		char *pattern;
		if (*argv == NULL)
			bb_show_usage();
		pattern = new_grep_list_data(*argv++, 0);
		llist_add_to(&pattern_head, pattern);
	}

	/* argv[0..(argc-1)] should be names of file to grep through. If
	 * there is more than one file to grep, we will print the filenames. */
	if (argv[0] && argv[1])
		print_filename = 1;
	/* -H / -h of course override */
	if (option_mask32 & OPT_H)
		print_filename = 1;
	if (option_mask32 & OPT_h)
		print_filename = 0;

	/* If no files were specified, or '-' was specified, take input from
	 * stdin. Otherwise, we grep through all the files specified. */
	matched = 0;
	do {
		cur_file = *argv;
		file = stdin;
		if (!cur_file || LONE_DASH(cur_file)) {
			cur_file = "(standard input)";
		} else {
			if (option_mask32 & OPT_r) {
				struct stat st;
				if (stat(cur_file, &st) == 0 && S_ISDIR(st.st_mode)) {
					if (!(option_mask32 & OPT_h))
						print_filename = 1;
					matched += grep_dir(cur_file);
					goto grep_done;
				}
			}
			/* else: fopen(dir) will succeed, but reading won't */
			file = fopen_for_read(cur_file);
			if (file == NULL) {
				if (!SUPPRESS_ERR_MSGS)
					bb_simple_perror_msg(cur_file);
				open_errors = 1;
				continue;
			}
		}
		matched += grep_file(file);
		fclose_if_not_stdin(file);
 grep_done: ;
	} while (*argv && *++argv);

	/* destroy all the elements in the pattern list */
	if (ENABLE_FEATURE_CLEAN_UP) {
		while (pattern_head) {
			llist_t *pattern_head_ptr = pattern_head;
			grep_list_data_t *gl = (grep_list_data_t *)pattern_head_ptr->data;

			pattern_head = pattern_head->link;
			if (gl->flg_mem_allocated_compiled & ALLOCATED)
				free(gl->pattern);
			if (gl->flg_mem_allocated_compiled & COMPILED)
				regfree(&gl->compiled_regex);
			free(gl);
			free(pattern_head_ptr);
		}
	}
	/* 0 = success, 1 = failed, 2 = error */
	if (open_errors)
		return 2;
	return !matched; /* invert return value: 0 = success, 1 = failed */
}
