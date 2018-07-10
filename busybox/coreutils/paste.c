/* vi: set sw=4 ts=4: */
/*
 * paste.c - implementation of the posix paste command
 *
 * Written by Maxime Coste <mawww@kakoune.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config PASTE
//config:	bool "paste (4.5 kb)"
//config:	default y
//config:	help
//config:	paste is used to paste lines of different files together
//config:	and write the result to stdout

//applet:IF_PASTE(APPLET_NOEXEC(paste, paste, BB_DIR_USR_BIN, BB_SUID_DROP, paste))

//kbuild:lib-$(CONFIG_PASTE) += paste.o

//usage:#define paste_trivial_usage
//usage:       "[OPTIONS] [FILE]..."
//usage:#define paste_full_usage "\n\n"
//usage:       "Paste lines from each input file, separated with tab\n"
//usage:     "\n	-d LIST	Use delimiters from LIST, not tab"
//usage:     "\n	-s      Serial: one file at a time"
//usage:
//usage:#define paste_example_usage
//usage:       "# write out directory in four columns\n"
//usage:       "$ ls | paste - - - -\n"
//usage:       "# combine pairs of lines from a file into single lines\n"
//usage:       "$ paste -s -d '\\t\\n' file\n"

#include "libbb.h"

static void paste_files(FILE** files, int file_cnt, char* delims, int del_cnt)
{
	char *line;
	char delim;
	int active_files = file_cnt;
	int i;

	while (active_files > 0) {
		int del_idx = 0;

		for (i = 0; i < file_cnt; ++i) {
			if (files[i] == NULL)
				continue;

			line = xmalloc_fgetline(files[i]);
			if (!line) {
				fclose_if_not_stdin(files[i]);
				files[i] = NULL;
				--active_files;
				continue;
			}
			fputs(line, stdout);
			free(line);
			delim = '\n';
			if (i != file_cnt - 1) {
				delim = delims[del_idx++];
				if (del_idx == del_cnt)
					del_idx = 0;
			}
			if (delim != '\0')
				fputc(delim, stdout);
		}
	}
}

static void paste_files_separate(FILE** files, char* delims, int del_cnt)
{
	char *line, *next_line;
	char delim;
	int i;

	for (i = 0; files[i]; ++i) {
		int del_idx = 0;

		line = NULL;
		while ((next_line = xmalloc_fgetline(files[i])) != NULL) {
			if (line) {
				fputs(line, stdout);
				free(line);
				delim = delims[del_idx++];
				if (del_idx == del_cnt)
					del_idx = 0;
				if (delim != '\0')
					fputc(delim, stdout);
			}
			line = next_line;
		}
		if (line) {
			/* coreutils adds \n even if this is a final line
			 * of the last file and it was not \n-terminated.
			 */
			printf("%s\n", line);
			free(line);
		}
		fclose_if_not_stdin(files[i]);
	}
}

#define PASTE_OPT_DELIMITERS (1 << 0)
#define PASTE_OPT_SEPARATE   (1 << 1)

int paste_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int paste_main(int argc UNUSED_PARAM, char **argv)
{
	char *delims = (char*)"\t";
	int del_cnt = 1;
	unsigned opt;
	int i;

	opt = getopt32(argv, "d:s", &delims);
	argv += optind;

	if (opt & PASTE_OPT_DELIMITERS) {
		if (!delims[0])
			bb_error_msg_and_die("-d '' is not supported");
		/* unknown mappings are not changed: "\z" -> '\\' 'z' */
		/* trailing backslash, if any, is preserved */
		del_cnt = strcpy_and_process_escape_sequences(delims, delims) - delims;
		/* note: handle NUL properly (do not stop at it!): try -d'\t\0\t' */
	}

	if (!argv[0])
		(--argv)[0] = (char*) "-";
	for (i = 0; argv[i]; ++i) {
		argv[i] = (void*) fopen_or_warn_stdin(argv[i]);
		if (!argv[i])
			xfunc_die();
	}

	if (opt & PASTE_OPT_SEPARATE)
		paste_files_separate((FILE**)argv, delims, del_cnt);
	else
		paste_files((FILE**)argv, i, delims, del_cnt);

	fflush_stdout_and_exit(0);
}
