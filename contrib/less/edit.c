/*
 * Copyright (C) 1984-2017  Mark Nudelman
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Less License, as specified in the README file.
 *
 * For more information, see the README file.
 */


#include "less.h"
#include "position.h"
#if HAVE_STAT
#include <sys/stat.h>
#endif
#if OS2
#include <signal.h>
#endif

public int fd0 = 0;

extern int new_file;
extern int errmsgs;
extern int cbufs;
extern char *every_first_cmd;
extern int any_display;
extern int force_open;
extern int is_tty;
extern int sigs;
extern IFILE curr_ifile;
extern IFILE old_ifile;
extern struct scrpos initial_scrpos;
extern void *ml_examine;
#if SPACES_IN_FILENAMES
extern char openquote;
extern char closequote;
#endif

#if LOGFILE
extern int logfile;
extern int force_logfile;
extern char *namelogfile;
#endif

#if HAVE_STAT_INO
public dev_t curr_dev;
public ino_t curr_ino;
#endif


/*
 * Textlist functions deal with a list of words separated by spaces.
 * init_textlist sets up a textlist structure.
 * forw_textlist uses that structure to iterate thru the list of
 * words, returning each one as a standard null-terminated string.
 * back_textlist does the same, but runs thru the list backwards.
 */
	public void
init_textlist(tlist, str)
	struct textlist *tlist;
	char *str;
{
	char *s;
#if SPACES_IN_FILENAMES
	int meta_quoted = 0;
	int delim_quoted = 0;
	char *esc = get_meta_escape();
	int esclen = (int) strlen(esc);
#endif
	
	tlist->string = skipsp(str);
	tlist->endstring = tlist->string + strlen(tlist->string);
	for (s = str;  s < tlist->endstring;  s++)
	{
#if SPACES_IN_FILENAMES
		if (meta_quoted)
		{
			meta_quoted = 0;
		} else if (esclen > 0 && s + esclen < tlist->endstring &&
		           strncmp(s, esc, esclen) == 0)
		{
			meta_quoted = 1;
			s += esclen - 1;
		} else if (delim_quoted)
		{
			if (*s == closequote)
				delim_quoted = 0;
		} else /* (!delim_quoted) */
		{
			if (*s == openquote)
				delim_quoted = 1;
			else if (*s == ' ')
				*s = '\0';
		}
#else
		if (*s == ' ')
			*s = '\0';
#endif
	}
}

	public char *
forw_textlist(tlist, prev)
	struct textlist *tlist;
	char *prev;
{
	char *s;
	
	/*
	 * prev == NULL means return the first word in the list.
	 * Otherwise, return the word after "prev".
	 */
	if (prev == NULL)
		s = tlist->string;
	else
		s = prev + strlen(prev);
	if (s >= tlist->endstring)
		return (NULL);
	while (*s == '\0')
		s++;
	if (s >= tlist->endstring)
		return (NULL);
	return (s);
}

	public char *
back_textlist(tlist, prev)
	struct textlist *tlist;
	char *prev;
{
	char *s;
	
	/*
	 * prev == NULL means return the last word in the list.
	 * Otherwise, return the word before "prev".
	 */
	if (prev == NULL)
		s = tlist->endstring;
	else if (prev <= tlist->string)
		return (NULL);
	else
		s = prev - 1;
	while (*s == '\0')
		s--;
	if (s <= tlist->string)
		return (NULL);
	while (s[-1] != '\0' && s > tlist->string)
		s--;
	return (s);
}

/*
 * Close a pipe opened via popen.
 */
	static void
close_pipe(FILE *pipefd)
{
	if (pipefd == NULL)
		return;
#if OS2
	/*
	 * The pclose function of OS/2 emx sometimes fails.
	 * Send SIGINT to the piped process before closing it.
	 */
	kill(pipefd->_pid, SIGINT);
#endif
	pclose(pipefd);
}

/*
 * Close the current input file.
 */
	static void
close_file()
{
	struct scrpos scrpos;
	int chflags;
	FILE *altpipe;
	char *altfilename;
	
	if (curr_ifile == NULL_IFILE)
		return;

	/*
	 * Save the current position so that we can return to
	 * the same position if we edit this file again.
	 */
	get_scrpos(&scrpos, TOP);
	if (scrpos.pos != NULL_POSITION)
	{
		store_pos(curr_ifile, &scrpos);
		lastmark();
	}
	/*
	 * Close the file descriptor, unless it is a pipe.
	 */
	chflags = ch_getflags();
	ch_close();
	/*
	 * If we opened a file using an alternate name,
	 * do special stuff to close it.
	 */
	altfilename = get_altfilename(curr_ifile);
	if (altfilename != NULL)
	{
		altpipe = get_altpipe(curr_ifile);
		if (altpipe != NULL && !(chflags & CH_KEEPOPEN))
		{
			close_pipe(altpipe);
			set_altpipe(curr_ifile, NULL);
		}
		close_altfile(altfilename, get_filename(curr_ifile));
		set_altfilename(curr_ifile, NULL);
	}
	curr_ifile = NULL_IFILE;
#if HAVE_STAT_INO
	curr_ino = curr_dev = 0;
#endif
}

/*
 * Edit a new file (given its name).
 * Filename == "-" means standard input.
 * Filename == NULL means just close the current file.
 */
	public int
edit(filename)
	char *filename;
{
	if (filename == NULL)
		return (edit_ifile(NULL_IFILE));
	return (edit_ifile(get_ifile(filename, curr_ifile)));
}
	
/*
 * Edit a new file (given its IFILE).
 * ifile == NULL means just close the current file.
 */
	public int
edit_ifile(ifile)
	IFILE ifile;
{
	int f;
	int answer;
	int no_display;
	int chflags;
	char *filename;
	char *open_filename;
	char *alt_filename;
	void *altpipe;
	IFILE was_curr_ifile;
	PARG parg;
		
	if (ifile == curr_ifile)
	{
		/*
		 * Already have the correct file open.
		 */
		return (0);
	}

	/*
	 * We must close the currently open file now.
	 * This is necessary to make the open_altfile/close_altfile pairs
	 * nest properly (or rather to avoid nesting at all).
	 * {{ Some stupid implementations of popen() mess up if you do:
	 *    fA = popen("A"); fB = popen("B"); pclose(fA); pclose(fB); }}
	 */
#if LOGFILE
	end_logfile();
#endif
	was_curr_ifile = save_curr_ifile();
	if (curr_ifile != NULL_IFILE)
	{
		chflags = ch_getflags();
		close_file();
		if ((chflags & CH_HELPFILE) && held_ifile(was_curr_ifile) <= 1)
		{
			/*
			 * Don't keep the help file in the ifile list.
			 */
			del_ifile(was_curr_ifile);
			was_curr_ifile = old_ifile;
		}
	}

	if (ifile == NULL_IFILE)
	{
		/*
		 * No new file to open.
		 * (Don't set old_ifile, because if you call edit_ifile(NULL),
		 *  you're supposed to have saved curr_ifile yourself,
		 *  and you'll restore it if necessary.)
		 */
		unsave_ifile(was_curr_ifile);
		return (0);
	}

	filename = save(get_filename(ifile));

	/*
	 * See if LESSOPEN specifies an "alternate" file to open.
	 */
	altpipe = get_altpipe(ifile);
	if (altpipe != NULL)
	{
		/*
		 * File is already open.
		 * chflags and f are not used by ch_init if ifile has 
		 * filestate which should be the case if we're here. 
		 * Set them here to avoid uninitialized variable warnings.
		 */
		chflags = 0; 
		f = -1;
		alt_filename = get_altfilename(ifile);
		open_filename = (alt_filename != NULL) ? alt_filename : filename;
	} else
	{
		if (strcmp(filename, FAKE_HELPFILE) == 0 ||
			 strcmp(filename, FAKE_EMPTYFILE) == 0)
			alt_filename = NULL;
		else
			alt_filename = open_altfile(filename, &f, &altpipe);

		open_filename = (alt_filename != NULL) ? alt_filename : filename;

		chflags = 0;
		if (altpipe != NULL)
		{
			/*
			 * The alternate "file" is actually a pipe.
			 * f has already been set to the file descriptor of the pipe
			 * in the call to open_altfile above.
			 * Keep the file descriptor open because it was opened 
			 * via popen(), and pclose() wants to close it.
			 */
			chflags |= CH_POPENED;
			if (strcmp(filename, "-") == 0)
				chflags |= CH_KEEPOPEN;
		} else if (strcmp(filename, "-") == 0)
		{
			/* 
			 * Use standard input.
			 * Keep the file descriptor open because we can't reopen it.
			 */
			f = fd0;
			chflags |= CH_KEEPOPEN;
			/*
			 * Must switch stdin to BINARY mode.
			 */
			SET_BINARY(f);
#if MSDOS_COMPILER==DJGPPC
			/*
			 * Setting stdin to binary by default causes
			 * Ctrl-C to not raise SIGINT.  We must undo
			 * that side-effect.
			 */
			__djgpp_set_ctrl_c(1);
#endif
		} else if (strcmp(open_filename, FAKE_EMPTYFILE) == 0)
		{
			f = -1;
			chflags |= CH_NODATA;
		} else if (strcmp(open_filename, FAKE_HELPFILE) == 0)
		{
			f = -1;
			chflags |= CH_HELPFILE;
		} else if ((parg.p_string = bad_file(open_filename)) != NULL)
		{
			/*
			 * It looks like a bad file.  Don't try to open it.
			 */
			error("%s", &parg);
			free(parg.p_string);
			err1:
			if (alt_filename != NULL)
			{
				close_pipe(altpipe);
				close_altfile(alt_filename, filename);
				free(alt_filename);
			}
			del_ifile(ifile);
			free(filename);
			/*
			 * Re-open the current file.
			 */
			if (was_curr_ifile == ifile)
			{
				/*
				 * Whoops.  The "current" ifile is the one we just deleted.
				 * Just give up.
				 */
				quit(QUIT_ERROR);
			}
			reedit_ifile(was_curr_ifile);
			return (1);
		} else if ((f = open(open_filename, OPEN_READ)) < 0)
		{
			/*
			 * Got an error trying to open it.
			 */
			parg.p_string = errno_message(filename);
			error("%s", &parg);
			free(parg.p_string);
				goto err1;
		} else 
		{
			chflags |= CH_CANSEEK;
			if (!force_open && !opened(ifile) && bin_file(f))
			{
				/*
				 * Looks like a binary file.  
				 * Ask user if we should proceed.
				 */
				parg.p_string = filename;
				answer = query("\"%s\" may be a binary file.  See it anyway? ",
					&parg);
				if (answer != 'y' && answer != 'Y')
				{
					close(f);
					goto err1;
				}
			}
		}
	}

	/*
	 * Get the new ifile.
	 * Get the saved position for the file.
	 */
	if (was_curr_ifile != NULL_IFILE)
	{
		old_ifile = was_curr_ifile;
		unsave_ifile(was_curr_ifile);
	}
	curr_ifile = ifile;
	set_altfilename(curr_ifile, alt_filename);
	set_altpipe(curr_ifile, altpipe);
	set_open(curr_ifile); /* File has been opened */
	get_pos(curr_ifile, &initial_scrpos);
	new_file = TRUE;
	ch_init(f, chflags);

	if (!(chflags & CH_HELPFILE))
	{
#if LOGFILE
		if (namelogfile != NULL && is_tty)
			use_logfile(namelogfile);
#endif
#if HAVE_STAT_INO
		/* Remember the i-number and device of the opened file. */
		if (strcmp(open_filename, "-") != 0)
		{
			struct stat statbuf;
			int r = stat(open_filename, &statbuf);
			if (r == 0)
			{
				curr_ino = statbuf.st_ino;
				curr_dev = statbuf.st_dev;
			}
		}
#endif
		if (every_first_cmd != NULL)
		{
			ungetcc(CHAR_END_COMMAND);
			ungetsc(every_first_cmd);
		}
	}

	no_display = !any_display;
	flush();
	any_display = TRUE;

	if (is_tty)
	{
		/*
		 * Output is to a real tty.
		 */

		/*
		 * Indicate there is nothing displayed yet.
		 */
		pos_clear();
		clr_linenum();
#if HILITE_SEARCH
		clr_hilite();
#endif
		if (strcmp(filename, FAKE_HELPFILE) && strcmp(filename, FAKE_EMPTYFILE))
			cmd_addhist(ml_examine, filename, 1);
		if (no_display && errmsgs > 0)
		{
			/*
			 * We displayed some messages on error output
			 * (file descriptor 2; see error() function).
			 * Before erasing the screen contents,
			 * display the file name and wait for a keystroke.
			 */
			parg.p_string = filename;
			error("%s", &parg);
		}
	}
	free(filename);
	return (0);
}

/*
 * Edit a space-separated list of files.
 * For each filename in the list, enter it into the ifile list.
 * Then edit the first one.
 */
	public int
edit_list(filelist)
	char *filelist;
{
	IFILE save_ifile;
	char *good_filename;
	char *filename;
	char *gfilelist;
	char *gfilename;
	char *qfilename;
	struct textlist tl_files;
	struct textlist tl_gfiles;

	save_ifile = save_curr_ifile();
	good_filename = NULL;
	
	/*
	 * Run thru each filename in the list.
	 * Try to glob the filename.  
	 * If it doesn't expand, just try to open the filename.
	 * If it does expand, try to open each name in that list.
	 */
	init_textlist(&tl_files, filelist);
	filename = NULL;
	while ((filename = forw_textlist(&tl_files, filename)) != NULL)
	{
		gfilelist = lglob(filename);
		init_textlist(&tl_gfiles, gfilelist);
		gfilename = NULL;
		while ((gfilename = forw_textlist(&tl_gfiles, gfilename)) != NULL)
		{
			qfilename = shell_unquote(gfilename);
			if (edit(qfilename) == 0 && good_filename == NULL)
				good_filename = get_filename(curr_ifile);
			free(qfilename);
		}
		free(gfilelist);
	}
	/*
	 * Edit the first valid filename in the list.
	 */
	if (good_filename == NULL)
	{
		unsave_ifile(save_ifile);
		return (1);
	}
	if (get_ifile(good_filename, curr_ifile) == curr_ifile)
	{
		/*
		 * Trying to edit the current file; don't reopen it.
		 */
		unsave_ifile(save_ifile);
		return (0);
	}
	reedit_ifile(save_ifile);
	return (edit(good_filename));
}

/*
 * Edit the first file in the command line (ifile) list.
 */
	public int
edit_first()
{
	curr_ifile = NULL_IFILE;
	return (edit_next(1));
}

/*
 * Edit the last file in the command line (ifile) list.
 */
	public int
edit_last()
{
	curr_ifile = NULL_IFILE;
	return (edit_prev(1));
}


/*
 * Edit the n-th next or previous file in the command line (ifile) list.
 */
	static int
edit_istep(h, n, dir)
	IFILE h;
	int n;
	int dir;
{
	IFILE next;

	/*
	 * Skip n filenames, then try to edit each filename.
	 */
	for (;;)
	{
		next = (dir > 0) ? next_ifile(h) : prev_ifile(h);
		if (--n < 0)
		{
			if (edit_ifile(h) == 0)
				break;
		}
		if (next == NULL_IFILE)
		{
			/*
			 * Reached end of the ifile list.
			 */
			return (1);
		}
		if (ABORT_SIGS())
		{
			/*
			 * Interrupt breaks out, if we're in a long
			 * list of files that can't be opened.
			 */
			return (1);
		}
		h = next;
	} 
	/*
	 * Found a file that we can edit.
	 */
	return (0);
}

	static int
edit_inext(h, n)
	IFILE h;
	int n;
{
	return (edit_istep(h, n, +1));
}

	public int
edit_next(n)
	int n;
{
	return edit_istep(curr_ifile, n, +1);
}

	static int
edit_iprev(h, n)
	IFILE h;
	int n;
{
	return (edit_istep(h, n, -1));
}

	public int
edit_prev(n)
	int n;
{
	return edit_istep(curr_ifile, n, -1);
}

/*
 * Edit a specific file in the command line (ifile) list.
 */
	public int
edit_index(n)
	int n;
{
	IFILE h;

	h = NULL_IFILE;
	do
	{
		if ((h = next_ifile(h)) == NULL_IFILE)
		{
			/*
			 * Reached end of the list without finding it.
			 */
			return (1);
		}
	} while (get_index(h) != n);

	return (edit_ifile(h));
}

	public IFILE
save_curr_ifile()
{
	if (curr_ifile != NULL_IFILE)
		hold_ifile(curr_ifile, 1);
	return (curr_ifile);
}

	public void
unsave_ifile(save_ifile)
	IFILE save_ifile;
{
	if (save_ifile != NULL_IFILE)
		hold_ifile(save_ifile, -1);
}

/*
 * Reedit the ifile which was previously open.
 */
	public void
reedit_ifile(save_ifile)
	IFILE save_ifile;
{
	IFILE next;
	IFILE prev;

	/*
	 * Try to reopen the ifile.
	 * Note that opening it may fail (maybe the file was removed),
	 * in which case the ifile will be deleted from the list.
	 * So save the next and prev ifiles first.
	 */
	unsave_ifile(save_ifile);
	next = next_ifile(save_ifile);
	prev = prev_ifile(save_ifile);
	if (edit_ifile(save_ifile) == 0)
		return;
	/*
	 * If can't reopen it, open the next input file in the list.
	 */
	if (next != NULL_IFILE && edit_inext(next, 0) == 0)
		return;
	/*
	 * If can't open THAT one, open the previous input file in the list.
	 */
	if (prev != NULL_IFILE && edit_iprev(prev, 0) == 0)
		return;
	/*
	 * If can't even open that, we're stuck.  Just quit.
	 */
	quit(QUIT_ERROR);
}

	public void
reopen_curr_ifile()
{
	IFILE save_ifile = save_curr_ifile();
	close_file();
	reedit_ifile(save_ifile);
}

/*
 * Edit standard input.
 */
	public int
edit_stdin()
{
	if (isatty(fd0))
	{
		error("Missing filename (\"less --help\" for help)", NULL_PARG);
		quit(QUIT_OK);
	}
	return (edit("-"));
}

/*
 * Copy a file directly to standard output.
 * Used if standard output is not a tty.
 */
	public void
cat_file()
{
	int c;

	while ((c = ch_forw_get()) != EOI)
		putchr(c);
	flush();
}

#if LOGFILE

/*
 * If the user asked for a log file and our input file
 * is standard input, create the log file.  
 * We take care not to blindly overwrite an existing file.
 */
	public void
use_logfile(filename)
	char *filename;
{
	int exists;
	int answer;
	PARG parg;

	if (ch_getflags() & CH_CANSEEK)
		/*
		 * Can't currently use a log file on a file that can seek.
		 */
		return;

	/*
	 * {{ We could use access() here. }}
	 */
	exists = open(filename, OPEN_READ);
	if (exists >= 0)
		close(exists);
	exists = (exists >= 0);

	/*
	 * Decide whether to overwrite the log file or append to it.
	 * If it doesn't exist we "overwrite" it.
	 */
	if (!exists || force_logfile)
	{
		/*
		 * Overwrite (or create) the log file.
		 */
		answer = 'O';
	} else
	{
		/*
		 * Ask user what to do.
		 */
		parg.p_string = filename;
		answer = query("Warning: \"%s\" exists; Overwrite, Append or Don't log? ", &parg);
	}

loop:
	switch (answer)
	{
	case 'O': case 'o':
		/*
		 * Overwrite: create the file.
		 */
		logfile = creat(filename, 0644);
		break;
	case 'A': case 'a':
		/*
		 * Append: open the file and seek to the end.
		 */
		logfile = open(filename, OPEN_APPEND);
		if (lseek(logfile, (off_t)0, SEEK_END) == BAD_LSEEK)
		{
			close(logfile);
			logfile = -1;
		}
		break;
	case 'D': case 'd':
		/*
		 * Don't do anything.
		 */
		free(filename);
		return;
	case 'q':
		quit(QUIT_OK);
		/*NOTREACHED*/
	default:
		/*
		 * Eh?
		 */
		answer = query("Overwrite, Append, or Don't log? (Type \"O\", \"A\", \"D\" or \"q\") ", NULL_PARG);
		goto loop;
	}

	if (logfile < 0)
	{
		/*
		 * Error in opening logfile.
		 */
		parg.p_string = filename;
		error("Cannot write to \"%s\"", &parg);
		return;
	}
	SET_BINARY(logfile);
}

#endif
