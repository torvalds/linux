/*
 * Copyright (C) 1984-2017  Mark Nudelman
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Less License, as specified in the README file.
 *
 * For more information, see the README file.
 */


/*
 *	lesskey [-o output] [input]
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * 
 *
 *	Make a .less file.
 *	If no input file is specified, standard input is used.
 *	If no output file is specified, $HOME/.less is used.
 *
 *	The .less file is used to specify (to "less") user-defined
 *	key bindings.  Basically any sequence of 1 to MAX_CMDLEN
 *	keystrokes may be bound to an existing less function.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * 
 *
 *	The input file is an ascii file consisting of a 
 *	sequence of lines of the form:
 *		string <whitespace> action [chars] <newline>
 *
 *	"string" is a sequence of command characters which form
 *		the new user-defined command.  The command
 *		characters may be:
 *		1. The actual character itself.
 *		2. A character preceded by ^ to specify a
 *		   control character (e.g. ^X means control-X).
 *		3. A backslash followed by one to three octal digits
 *		   to specify a character by its octal value.
 *		4. A backslash followed by b, e, n, r or t
 *		   to specify \b, ESC, \n, \r or \t, respectively.
 *		5. Any character (other than those mentioned above) preceded 
 *		   by a \ to specify the character itself (characters which
 *		   must be preceded by \ include ^, \, and whitespace.
 *	"action" is the name of a "less" action, from the table below.
 *	"chars" is an optional sequence of characters which is treated
 *		as keyboard input after the command is executed.
 *
 *	Blank lines and lines which start with # are ignored, 
 *	except for the special control lines:
 *		#command	Signals the beginning of the command
 *				keys section.
 *		#line-edit	Signals the beginning of the line-editing
 *				keys section.
 *		#env		Signals the beginning of the environment
 *				variable section.
 *		#stop		Stops command parsing in less;
 *				causes all default keys to be disabled.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * 
 *
 *	The output file is a non-ascii file, consisting of a header,
 *	one or more sections, and a trailer.
 *	Each section begins with a section header, a section length word
 *	and the section data.  Normally there are three sections:
 *		CMD_SECTION	Definition of command keys.
 *		EDIT_SECTION	Definition of editing keys.
 *		END_SECTION	A special section header, with no 
 *				length word or section data.
 *
 *	Section data consists of zero or more byte sequences of the form:
 *		string <0> <action>
 *	or
 *		string <0> <action|A_EXTRA> chars <0>
 *
 *	"string" is the command string.
 *	"<0>" is one null byte.
 *	"<action>" is one byte containing the action code (the A_xxx value).
 *	If action is ORed with A_EXTRA, the action byte is followed
 *		by the null-terminated "chars" string.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * 
 */

#include "less.h"
#include "lesskey.h"
#include "cmd.h"

struct cmdname
{
	char *cn_name;
	int cn_action;
};

struct cmdname cmdnames[] = 
{
	{ "back-bracket",         A_B_BRACKET },
	{ "back-line",            A_B_LINE },
	{ "back-line-force",      A_BF_LINE },
	{ "back-screen",          A_B_SCREEN },
	{ "back-scroll",          A_B_SCROLL },
	{ "back-search",          A_B_SEARCH },
	{ "back-window",          A_B_WINDOW },
	{ "clear-mark",           A_CLRMARK },
	{ "debug",                A_DEBUG },
	{ "digit",                A_DIGIT },
	{ "display-flag",         A_DISP_OPTION },
	{ "display-option",       A_DISP_OPTION },
	{ "end",                  A_GOEND },
	{ "end-scroll",           A_RRSHIFT },
	{ "examine",              A_EXAMINE },
	{ "filter",               A_FILTER },
	{ "first-cmd",            A_FIRSTCMD },
	{ "firstcmd",             A_FIRSTCMD },
	{ "flush-repaint",        A_FREPAINT },
	{ "forw-bracket",         A_F_BRACKET },
	{ "forw-forever",         A_F_FOREVER },
	{ "forw-until-hilite",    A_F_UNTIL_HILITE },
	{ "forw-line",            A_F_LINE },
	{ "forw-line-force",      A_FF_LINE },
	{ "forw-screen",          A_F_SCREEN },
	{ "forw-screen-force",    A_FF_SCREEN },
	{ "forw-scroll",          A_F_SCROLL },
	{ "forw-search",          A_F_SEARCH },
	{ "forw-window",          A_F_WINDOW },
	{ "goto-end",             A_GOEND },
	{ "goto-end-buffered",    A_GOEND_BUF },
	{ "goto-line",            A_GOLINE },
	{ "goto-mark",            A_GOMARK },
	{ "help",                 A_HELP },
	{ "index-file",           A_INDEX_FILE },
	{ "invalid",              A_UINVALID },
	{ "left-scroll",          A_LSHIFT },
	{ "next-file",            A_NEXT_FILE },
	{ "next-tag",             A_NEXT_TAG },
	{ "noaction",             A_NOACTION },
	{ "no-scroll",            A_LLSHIFT },
	{ "percent",              A_PERCENT },
	{ "pipe",                 A_PIPE },
	{ "prev-file",            A_PREV_FILE },
	{ "prev-tag",             A_PREV_TAG },
	{ "quit",                 A_QUIT },
	{ "remove-file",          A_REMOVE_FILE },
	{ "repaint",              A_REPAINT },
	{ "repaint-flush",        A_FREPAINT },
	{ "repeat-search",        A_AGAIN_SEARCH },
	{ "repeat-search-all",    A_T_AGAIN_SEARCH },
	{ "reverse-search",       A_REVERSE_SEARCH },
	{ "reverse-search-all",   A_T_REVERSE_SEARCH },
	{ "right-scroll",         A_RSHIFT },
	{ "set-mark",             A_SETMARK },
	{ "set-mark-bottom",      A_SETMARKBOT },
	{ "shell",                A_SHELL },
	{ "status",               A_STAT },
	{ "toggle-flag",          A_OPT_TOGGLE },
	{ "toggle-option",        A_OPT_TOGGLE },
	{ "undo-hilite",          A_UNDO_SEARCH },
	{ "version",              A_VERSION },
	{ "visual",               A_VISUAL },
	{ NULL,   0 }
};

struct cmdname editnames[] = 
{
	{ "back-complete",	EC_B_COMPLETE },
	{ "backspace",		EC_BACKSPACE },
	{ "delete",		EC_DELETE },
	{ "down",		EC_DOWN },
	{ "end",		EC_END },
	{ "expand",		EC_EXPAND },
	{ "forw-complete",	EC_F_COMPLETE },
	{ "home",		EC_HOME },
	{ "insert",		EC_INSERT },
	{ "invalid",		EC_UINVALID },
	{ "kill-line",		EC_LINEKILL },
	{ "abort",		EC_ABORT },
	{ "left",		EC_LEFT },
	{ "literal",		EC_LITERAL },
	{ "right",		EC_RIGHT },
	{ "up",			EC_UP },
	{ "word-backspace",	EC_W_BACKSPACE },
	{ "word-delete",	EC_W_DELETE },
	{ "word-left",		EC_W_LEFT },
	{ "word-right",		EC_W_RIGHT },
	{ NULL, 0 }
};

struct table
{
	struct cmdname *names;
	char *pbuffer;
	char buffer[MAX_USERCMD];
};

struct table cmdtable;
struct table edittable;
struct table vartable;
struct table *currtable = &cmdtable;

char fileheader[] = {
	C0_LESSKEY_MAGIC, 
	C1_LESSKEY_MAGIC, 
	C2_LESSKEY_MAGIC, 
	C3_LESSKEY_MAGIC
};
char filetrailer[] = {
	C0_END_LESSKEY_MAGIC, 
	C1_END_LESSKEY_MAGIC, 
	C2_END_LESSKEY_MAGIC
};
char cmdsection[1] =	{ CMD_SECTION };
char editsection[1] =	{ EDIT_SECTION };
char varsection[1] =	{ VAR_SECTION };
char endsection[1] =	{ END_SECTION };

char *infile = NULL;
char *outfile = NULL ;

int linenum;
int errors;

extern char version[];

	void
usage()
{
	fprintf(stderr, "usage: lesskey [-o output] [input]\n");
	exit(1);
}

	char *
mkpathname(dirname, filename)
	char *dirname;
	char *filename;
{
	char *pathname;

	pathname = calloc(strlen(dirname) + strlen(filename) + 2, sizeof(char));
	strcpy(pathname, dirname);
	strcat(pathname, PATHNAME_SEP);
	strcat(pathname, filename);
	return (pathname);
}

/*
 * Figure out the name of a default file (in the user's HOME directory).
 */
	char *
homefile(filename)
	char *filename;
{
	char *p;
	char *pathname;

	if ((p = getenv("HOME")) != NULL && *p != '\0')
		pathname = mkpathname(p, filename);
#if OS2
	else if ((p = getenv("INIT")) != NULL && *p != '\0')
		pathname = mkpathname(p, filename);
#endif
	else
	{
		fprintf(stderr, "cannot find $HOME - using current directory\n");
		pathname = mkpathname(".", filename);
	}
	return (pathname);
}

/*
 * Parse command line arguments.
 */
	void
parse_args(argc, argv)
	int argc;
	char **argv;
{
	char *arg;

	outfile = NULL;
	while (--argc > 0)
	{
		arg = *++argv;
		if (arg[0] != '-')
			/* Arg does not start with "-"; it's not an option. */
			break;
		if (arg[1] == '\0')
			/* "-" means standard input. */
			break;
		if (arg[1] == '-' && arg[2] == '\0')
		{
			/* "--" means end of options. */
			argc--;
			argv++;
			break;
		}
		switch (arg[1])
		{
		case '-':
			if (strncmp(arg, "--output", 8) == 0)
			{
				if (arg[8] == '\0')
					outfile = &arg[8];
				else if (arg[8] == '=')
					outfile = &arg[9];
				else
					usage();
				goto opt_o;
			}
			if (strcmp(arg, "--version") == 0)
			{
				goto opt_V;
			}
			usage();
			break;
		case 'o':
			outfile = &argv[0][2];
		opt_o:
			if (*outfile == '\0')
			{
				if (--argc <= 0)
					usage();
				outfile = *(++argv);
			}
			break;
		case 'V':
		opt_V:
			printf("lesskey  version %s\n", version);
			exit(0);
		default:
			usage();
		}
	}
	if (argc > 1)
		usage();
	/*
	 * Open the input file, or use DEF_LESSKEYINFILE if none specified.
	 */
	if (argc > 0)
		infile = *argv;
	else
		infile = homefile(DEF_LESSKEYINFILE);
}

/*
 * Initialize data structures.
 */
	void
init_tables()
{
	cmdtable.names = cmdnames;
	cmdtable.pbuffer = cmdtable.buffer;

	edittable.names = editnames;
	edittable.pbuffer = edittable.buffer;

	vartable.names = NULL;
	vartable.pbuffer = vartable.buffer;
}

/*
 * Parse one character of a string.
 */
	char *
tstr(pp, xlate)
	char **pp;
	int xlate;
{
	char *p;
	char ch;
	int i;
	static char buf[10];
	static char tstr_control_k[] =
		{ SK_SPECIAL_KEY, SK_CONTROL_K, 6, 1, 1, 1, '\0' };

	p = *pp;
	switch (*p)
	{
	case '\\':
		++p;
		switch (*p)
		{
		case '0': case '1': case '2': case '3':
		case '4': case '5': case '6': case '7':
			/*
			 * Parse an octal number.
			 */
			ch = 0;
			i = 0;
			do
				ch = 8*ch + (*p - '0');
			while (*++p >= '0' && *p <= '7' && ++i < 3);
			*pp = p;
			if (xlate && ch == CONTROL('K'))
				return tstr_control_k;
			buf[0] = ch;
			buf[1] = '\0';
			return (buf);
		case 'b':
			*pp = p+1;
			return ("\b");
		case 'e':
			*pp = p+1;
			buf[0] = ESC;
			buf[1] = '\0';
			return (buf);
		case 'n':
			*pp = p+1;
			return ("\n");
		case 'r':
			*pp = p+1;
			return ("\r");
		case 't':
			*pp = p+1;
			return ("\t");
		case 'k':
			if (xlate)
			{
				switch (*++p)
				{
				case 'u': ch = SK_UP_ARROW; break;
				case 'd': ch = SK_DOWN_ARROW; break;
				case 'r': ch = SK_RIGHT_ARROW; break;
				case 'l': ch = SK_LEFT_ARROW; break;
				case 'U': ch = SK_PAGE_UP; break;
				case 'D': ch = SK_PAGE_DOWN; break;
				case 'h': ch = SK_HOME; break;
				case 'e': ch = SK_END; break;
				case 'x': ch = SK_DELETE; break;
				default:
					error("illegal char after \\k", NULL_PARG);
					*pp = p+1;
					return ("");
				}
				*pp = p+1;
				buf[0] = SK_SPECIAL_KEY;
				buf[1] = ch;
				buf[2] = 6;
				buf[3] = 1;
				buf[4] = 1;
				buf[5] = 1;
				buf[6] = '\0';
				return (buf);
			}
			/* FALLTHRU */
		default:
			/*
			 * Backslash followed by any other char 
			 * just means that char.
			 */
			*pp = p+1;
			buf[0] = *p;
			buf[1] = '\0';
			if (xlate && buf[0] == CONTROL('K'))
				return tstr_control_k;
			return (buf);
		}
	case '^':
		/*
		 * Caret means CONTROL.
		 */
		*pp = p+2;
		buf[0] = CONTROL(p[1]);
		buf[1] = '\0';
		if (buf[0] == CONTROL('K'))
			return tstr_control_k;
		return (buf);
	}
	*pp = p+1;
	buf[0] = *p;
	buf[1] = '\0';
	if (xlate && buf[0] == CONTROL('K'))
		return tstr_control_k;
	return (buf);
}

/*
 * Skip leading spaces in a string.
 */
	public char *
skipsp(s)
	char *s;
{
	while (*s == ' ' || *s == '\t')	
		s++;
	return (s);
}

/*
 * Skip non-space characters in a string.
 */
	public char *
skipnsp(s)
	char *s;
{
	while (*s != '\0' && *s != ' ' && *s != '\t')
		s++;
	return (s);
}

/*
 * Clean up an input line:
 * strip off the trailing newline & any trailing # comment.
 */
	char *
clean_line(s)
	char *s;
{
	int i;

	s = skipsp(s);
	for (i = 0;  s[i] != '\n' && s[i] != '\r' && s[i] != '\0';  i++)
		if (s[i] == '#' && (i == 0 || s[i-1] != '\\'))
			break;
	s[i] = '\0';
	return (s);
}

/*
 * Add a byte to the output command table.
 */
	void
add_cmd_char(c)
	int c;
{
	if (currtable->pbuffer >= currtable->buffer + MAX_USERCMD)
	{
		error("too many commands", NULL_PARG);
		exit(1);
	}
	*(currtable->pbuffer)++ = c;
}

/*
 * Add a string to the output command table.
 */
	void
add_cmd_str(s)
	char *s;
{
	for ( ;  *s != '\0';  s++)
		add_cmd_char(*s);
}

/*
 * See if we have a special "control" line.
 */
	int
control_line(s)
	char *s;
{
#define	PREFIX(str,pat)	(strncmp(str,pat,strlen(pat)) == 0)

	if (PREFIX(s, "#line-edit"))
	{
		currtable = &edittable;
		return (1);
	}
	if (PREFIX(s, "#command"))
	{
		currtable = &cmdtable;
		return (1);
	}
	if (PREFIX(s, "#env"))
	{
		currtable = &vartable;
		return (1);
	}
	if (PREFIX(s, "#stop"))
	{
		add_cmd_char('\0');
		add_cmd_char(A_END_LIST);
		return (1);
	}
	return (0);
}

/*
 * Output some bytes.
 */
	void
fputbytes(fd, buf, len)
	FILE *fd;
	char *buf;
	int len;
{
	while (len-- > 0)
	{
		fwrite(buf, sizeof(char), 1, fd);
		buf++;
	}
}

/*
 * Output an integer, in special KRADIX form.
 */
	void
fputint(fd, val)
	FILE *fd;
	unsigned int val;
{
	char c;

	if (val >= KRADIX*KRADIX)
	{
		fprintf(stderr, "error: integer too big (%d > %d)\n", 
			val, KRADIX*KRADIX);
		exit(1);
	}
	c = val % KRADIX;
	fwrite(&c, sizeof(char), 1, fd);
	c = val / KRADIX;
	fwrite(&c, sizeof(char), 1, fd);
}

/*
 * Find an action, given the name of the action.
 */
	int
findaction(actname)
	char *actname;
{
	int i;

	for (i = 0;  currtable->names[i].cn_name != NULL;  i++)
		if (strcmp(currtable->names[i].cn_name, actname) == 0)
			return (currtable->names[i].cn_action);
	error("unknown action", NULL_PARG);
	return (A_INVALID);
}

	void
error(s, parg)
	char *s;
	PARG *parg;
{
	fprintf(stderr, "line %d: %s\n", linenum, s);
	errors++;
	(void) parg;
}


	void
parse_cmdline(p)
	char *p;
{
	int cmdlen;
	char *actname;
	int action;
	char *s;
	char c;

	/*
	 * Parse the command string and store it in the current table.
	 */
	cmdlen = 0;
	do
	{
		s = tstr(&p, 1);
		cmdlen += (int) strlen(s);
		if (cmdlen > MAX_CMDLEN)
			error("command too long", NULL_PARG);
		else
			add_cmd_str(s);
	} while (*p != ' ' && *p != '\t' && *p != '\0');
	/*
	 * Terminate the command string with a null byte.
	 */
	add_cmd_char('\0');

	/*
	 * Skip white space between the command string
	 * and the action name.
	 * Terminate the action name with a null byte.
	 */
	p = skipsp(p);
	if (*p == '\0')
	{
		error("missing action", NULL_PARG);
		return;
	}
	actname = p;
	p = skipnsp(p);
	c = *p;
	*p = '\0';

	/*
	 * Parse the action name and store it in the current table.
	 */
	action = findaction(actname);

	/*
	 * See if an extra string follows the action name.
	 */
	*p = c;
	p = skipsp(p);
	if (*p == '\0')
	{
		add_cmd_char(action);
	} else
	{
		/*
		 * OR the special value A_EXTRA into the action byte.
		 * Put the extra string after the action byte.
		 */
		add_cmd_char(action | A_EXTRA);
		while (*p != '\0')
			add_cmd_str(tstr(&p, 0));
		add_cmd_char('\0');
	}
}

	void
parse_varline(p)
	char *p;
{
	char *s;

	do
	{
		s = tstr(&p, 0);
		add_cmd_str(s);
	} while (*p != ' ' && *p != '\t' && *p != '=' && *p != '\0');
	/*
	 * Terminate the variable name with a null byte.
	 */
	add_cmd_char('\0');

	p = skipsp(p);
	if (*p++ != '=')
	{
		error("missing =", NULL_PARG);
		return;
	}

	add_cmd_char(EV_OK|A_EXTRA);

	p = skipsp(p);
	while (*p != '\0')
	{
		s = tstr(&p, 0);
		add_cmd_str(s);
	}
	add_cmd_char('\0');
}

/*
 * Parse a line from the lesskey file.
 */
	void
parse_line(line)
	char *line;
{
	char *p;

	/*
	 * See if it is a control line.
	 */
	if (control_line(line))
		return;
	/*
	 * Skip leading white space.
	 * Replace the final newline with a null byte.
	 * Ignore blank lines and comments.
	 */
	p = clean_line(line);
	if (*p == '\0')
		return;

	if (currtable == &vartable)
		parse_varline(p);
	else
		parse_cmdline(p);
}

	int
main(argc, argv)
	int argc;
	char *argv[];
{
	FILE *desc;
	FILE *out;
	char line[1024];

#ifdef WIN32
	if (getenv("HOME") == NULL)
	{
		/*
		 * If there is no HOME environment variable,
		 * try the concatenation of HOMEDRIVE + HOMEPATH.
		 */
		char *drive = getenv("HOMEDRIVE");
		char *path  = getenv("HOMEPATH");
		if (drive != NULL && path != NULL)
		{
			char *env = (char *) calloc(strlen(drive) + 
					strlen(path) + 6, sizeof(char));
			strcpy(env, "HOME=");
			strcat(env, drive);
			strcat(env, path);
			putenv(env);
		}
	}
#endif /* WIN32 */

	/*
	 * Process command line arguments.
	 */
	parse_args(argc, argv);
	init_tables();

	/*
	 * Open the input file.
	 */
	if (strcmp(infile, "-") == 0)
		desc = stdin;
	else if ((desc = fopen(infile, "r")) == NULL)
	{
#if HAVE_PERROR
		perror(infile);
#else
		fprintf(stderr, "Cannot open %s\n", infile);
#endif
		usage();
	}

	/*
	 * Read and parse the input file, one line at a time.
	 */
	errors = 0;
	linenum = 0;
	while (fgets(line, sizeof(line), desc) != NULL)
	{
		++linenum;
		parse_line(line);
	}

	/*
	 * Write the output file.
	 * If no output file was specified, use "$HOME/.less"
	 */
	if (errors > 0)
	{
		fprintf(stderr, "%d errors; no output produced\n", errors);
		exit(1);
	}

	if (outfile == NULL)
		outfile = getenv("LESSKEY");
	if (outfile == NULL)
		outfile = homefile(LESSKEYFILE);
	if ((out = fopen(outfile, "wb")) == NULL)
	{
#if HAVE_PERROR
		perror(outfile);
#else
		fprintf(stderr, "Cannot open %s\n", outfile);
#endif
		exit(1);
	}

	/* File header */
	fputbytes(out, fileheader, sizeof(fileheader));

	/* Command key section */
	fputbytes(out, cmdsection, sizeof(cmdsection));
	fputint(out, cmdtable.pbuffer - cmdtable.buffer);
	fputbytes(out, (char *)cmdtable.buffer, cmdtable.pbuffer-cmdtable.buffer);
	/* Edit key section */
	fputbytes(out, editsection, sizeof(editsection));
	fputint(out, edittable.pbuffer - edittable.buffer);
	fputbytes(out, (char *)edittable.buffer, edittable.pbuffer-edittable.buffer);

	/* Environment variable section */
	fputbytes(out, varsection, sizeof(varsection)); 
	fputint(out, vartable.pbuffer - vartable.buffer);
	fputbytes(out, (char *)vartable.buffer, vartable.pbuffer-vartable.buffer);

	/* File trailer */
	fputbytes(out, endsection, sizeof(endsection));
	fputbytes(out, filetrailer, sizeof(filetrailer));
	return (0);
}
