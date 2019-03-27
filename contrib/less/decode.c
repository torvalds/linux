/*
 * Copyright (C) 1984-2017  Mark Nudelman
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Less License, as specified in the README file.
 *
 * For more information, see the README file.
 */


/*
 * Routines to decode user commands.
 *
 * This is all table driven.
 * A command table is a sequence of command descriptors.
 * Each command descriptor is a sequence of bytes with the following format:
 *	<c1><c2>...<cN><0><action>
 * The characters c1,c2,...,cN are the command string; that is,
 * the characters which the user must type.
 * It is terminated by a null <0> byte.
 * The byte after the null byte is the action code associated
 * with the command string.
 * If an action byte is OR-ed with A_EXTRA, this indicates
 * that the option byte is followed by an extra string.
 *
 * There may be many command tables.
 * The first (default) table is built-in.
 * Other tables are read in from "lesskey" files.
 * All the tables are linked together and are searched in order.
 */

#include "less.h"
#include "cmd.h"
#include "lesskey.h"

extern int erase_char, erase2_char, kill_char;
extern int secure;

#define SK(k) \
	SK_SPECIAL_KEY, (k), 6, 1, 1, 1
/*
 * Command table is ordered roughly according to expected
 * frequency of use, so the common commands are near the beginning.
 */

static unsigned char cmdtable[] =
{
	'\r',0,				A_F_LINE,
	'\n',0,				A_F_LINE,
	'e',0,				A_F_LINE,
	'j',0,				A_F_LINE,
	SK(SK_DOWN_ARROW),0,		A_F_LINE,
	CONTROL('E'),0,			A_F_LINE,
	CONTROL('N'),0,			A_F_LINE,
	'k',0,				A_B_LINE,
	'y',0,				A_B_LINE,
	CONTROL('Y'),0,			A_B_LINE,
	SK(SK_CONTROL_K),0,		A_B_LINE,
	CONTROL('P'),0,			A_B_LINE,
	SK(SK_UP_ARROW),0,		A_B_LINE,
	'J',0,				A_FF_LINE,
	'K',0,				A_BF_LINE,
	'Y',0,				A_BF_LINE,
	'd',0,				A_F_SCROLL,
	CONTROL('D'),0,			A_F_SCROLL,
	'u',0,				A_B_SCROLL,
	CONTROL('U'),0,			A_B_SCROLL,
	' ',0,				A_F_SCREEN,
	'f',0,				A_F_SCREEN,
	CONTROL('F'),0,			A_F_SCREEN,
	CONTROL('V'),0,			A_F_SCREEN,
	SK(SK_PAGE_DOWN),0,		A_F_SCREEN,
	'b',0,				A_B_SCREEN,
	CONTROL('B'),0,			A_B_SCREEN,
	ESC,'v',0,			A_B_SCREEN,
	SK(SK_PAGE_UP),0,		A_B_SCREEN,
	'z',0,				A_F_WINDOW,
	'w',0,				A_B_WINDOW,
	ESC,' ',0,			A_FF_SCREEN,
	'F',0,				A_F_FOREVER,
	ESC,'F',0,			A_F_UNTIL_HILITE,
	'R',0,				A_FREPAINT,
	'r',0,				A_REPAINT,
	CONTROL('R'),0,			A_REPAINT,
	CONTROL('L'),0,			A_REPAINT,
	ESC,'u',0,			A_UNDO_SEARCH,
	'g',0,				A_GOLINE,
	SK(SK_HOME),0,			A_GOLINE,
	'<',0,				A_GOLINE,
	ESC,'<',0,			A_GOLINE,
	'p',0,				A_PERCENT,
	'%',0,				A_PERCENT,
	ESC,'[',0,			A_LSHIFT,
	ESC,']',0,			A_RSHIFT,
	ESC,'(',0,			A_LSHIFT,
	ESC,')',0,			A_RSHIFT,
	ESC,'{',0,			A_LLSHIFT,
	ESC,'}',0,			A_RRSHIFT,
	SK(SK_RIGHT_ARROW),0,		A_RSHIFT,
	SK(SK_LEFT_ARROW),0,		A_LSHIFT,
	SK(SK_CTL_RIGHT_ARROW),0,	A_RRSHIFT,
	SK(SK_CTL_LEFT_ARROW),0,	A_LLSHIFT,
	'{',0,				A_F_BRACKET|A_EXTRA,	'{','}',0,
	'}',0,				A_B_BRACKET|A_EXTRA,	'{','}',0,
	'(',0,				A_F_BRACKET|A_EXTRA,	'(',')',0,
	')',0,				A_B_BRACKET|A_EXTRA,	'(',')',0,
	'[',0,				A_F_BRACKET|A_EXTRA,	'[',']',0,
	']',0,				A_B_BRACKET|A_EXTRA,	'[',']',0,
	ESC,CONTROL('F'),0,		A_F_BRACKET,
	ESC,CONTROL('B'),0,		A_B_BRACKET,
	'G',0,				A_GOEND,
	ESC,'G',0,			A_GOEND_BUF,
	ESC,'>',0,			A_GOEND,
	'>',0,				A_GOEND,
	SK(SK_END),0,			A_GOEND,
	'P',0,				A_GOPOS,

	'0',0,				A_DIGIT,
	'1',0,				A_DIGIT,
	'2',0,				A_DIGIT,
	'3',0,				A_DIGIT,
	'4',0,				A_DIGIT,
	'5',0,				A_DIGIT,
	'6',0,				A_DIGIT,
	'7',0,				A_DIGIT,
	'8',0,				A_DIGIT,
	'9',0,				A_DIGIT,
	'.',0,				A_DIGIT,

	'=',0,				A_STAT,
	CONTROL('G'),0,			A_STAT,
	':','f',0,			A_STAT,
	'/',0,				A_F_SEARCH,
	'?',0,				A_B_SEARCH,
	ESC,'/',0,			A_F_SEARCH|A_EXTRA,	'*',0,
	ESC,'?',0,			A_B_SEARCH|A_EXTRA,	'*',0,
	'n',0,				A_AGAIN_SEARCH,
	ESC,'n',0,			A_T_AGAIN_SEARCH,
	'N',0,				A_REVERSE_SEARCH,
	ESC,'N',0,			A_T_REVERSE_SEARCH,
	'&',0,				A_FILTER,
	'm',0,				A_SETMARK,
	'M',0,				A_SETMARKBOT,
	ESC,'m',0,			A_CLRMARK,
	'\'',0,				A_GOMARK,
	CONTROL('X'),CONTROL('X'),0,	A_GOMARK,
	'E',0,				A_EXAMINE,
	':','e',0,			A_EXAMINE,
	CONTROL('X'),CONTROL('V'),0,	A_EXAMINE,
	':','n',0,			A_NEXT_FILE,
	':','p',0,			A_PREV_FILE,
	't',0,				A_NEXT_TAG,
	'T',0,				A_PREV_TAG,
	':','x',0,			A_INDEX_FILE,
	':','d',0,			A_REMOVE_FILE,
	'-',0,				A_OPT_TOGGLE,
	':','t',0,			A_OPT_TOGGLE|A_EXTRA,	't',0,
	's',0,				A_OPT_TOGGLE|A_EXTRA,	'o',0,
	'_',0,				A_DISP_OPTION,
	'|',0,				A_PIPE,
	'v',0,				A_VISUAL,
	'!',0,				A_SHELL,
	'+',0,				A_FIRSTCMD,

	'H',0,				A_HELP,
	'h',0,				A_HELP,
	SK(SK_F1),0,			A_HELP,
	'V',0,				A_VERSION,
	'q',0,				A_QUIT,
	'Q',0,				A_QUIT,
	':','q',0,			A_QUIT,
	':','Q',0,			A_QUIT,
	'Z','Z',0,			A_QUIT
};

static unsigned char edittable[] =
{
	'\t',0,	    			EC_F_COMPLETE,	/* TAB */
	'\17',0,			EC_B_COMPLETE,	/* BACKTAB */
	SK(SK_BACKTAB),0,		EC_B_COMPLETE,	/* BACKTAB */
	ESC,'\t',0,			EC_B_COMPLETE,	/* ESC TAB */
	CONTROL('L'),0,			EC_EXPAND,	/* CTRL-L */
	CONTROL('V'),0,			EC_LITERAL,	/* BACKSLASH */
	CONTROL('A'),0,			EC_LITERAL,	/* BACKSLASH */
   	ESC,'l',0,			EC_RIGHT,	/* ESC l */
	SK(SK_RIGHT_ARROW),0,		EC_RIGHT,	/* RIGHTARROW */
	ESC,'h',0,			EC_LEFT,	/* ESC h */
	SK(SK_LEFT_ARROW),0,		EC_LEFT,	/* LEFTARROW */
	ESC,'b',0,			EC_W_LEFT,	/* ESC b */
	ESC,SK(SK_LEFT_ARROW),0,	EC_W_LEFT,	/* ESC LEFTARROW */
	SK(SK_CTL_LEFT_ARROW),0,	EC_W_LEFT,	/* CTRL-LEFTARROW */
	ESC,'w',0,			EC_W_RIGHT,	/* ESC w */
	ESC,SK(SK_RIGHT_ARROW),0,	EC_W_RIGHT,	/* ESC RIGHTARROW */
	SK(SK_CTL_RIGHT_ARROW),0,	EC_W_RIGHT,	/* CTRL-RIGHTARROW */
	ESC,'i',0,			EC_INSERT,	/* ESC i */
	SK(SK_INSERT),0,		EC_INSERT,	/* INSERT */
	ESC,'x',0,			EC_DELETE,	/* ESC x */
	SK(SK_DELETE),0,		EC_DELETE,	/* DELETE */
	ESC,'X',0,			EC_W_DELETE,	/* ESC X */
	ESC,SK(SK_DELETE),0,		EC_W_DELETE,	/* ESC DELETE */
	SK(SK_CTL_DELETE),0,		EC_W_DELETE,	/* CTRL-DELETE */
	SK(SK_CTL_BACKSPACE),0,		EC_W_BACKSPACE, /* CTRL-BACKSPACE */
	ESC,'\b',0,			EC_W_BACKSPACE,	/* ESC BACKSPACE */
	ESC,'0',0,			EC_HOME,	/* ESC 0 */
	SK(SK_HOME),0,			EC_HOME,	/* HOME */
	ESC,'$',0,			EC_END,		/* ESC $ */
	SK(SK_END),0,			EC_END,		/* END */
	ESC,'k',0,			EC_UP,		/* ESC k */
	SK(SK_UP_ARROW),0,		EC_UP,		/* UPARROW */
	ESC,'j',0,			EC_DOWN,	/* ESC j */
	SK(SK_DOWN_ARROW),0,		EC_DOWN,	/* DOWNARROW */
	CONTROL('G'),0,			EC_ABORT,	/* CTRL-G */
};

/*
 * Structure to support a list of command tables.
 */
struct tablelist
{
	struct tablelist *t_next;
	char *t_start;
	char *t_end;
};

/*
 * List of command tables and list of line-edit tables.
 */
static struct tablelist *list_fcmd_tables = NULL;
static struct tablelist *list_ecmd_tables = NULL;
static struct tablelist *list_var_tables = NULL;
static struct tablelist *list_sysvar_tables = NULL;


/*
 * Expand special key abbreviations in a command table.
 */
	static void
expand_special_keys(table, len)
	char *table;
	int len;
{
	char *fm;
	char *to;
	int a;
	char *repl;
	int klen;

	for (fm = table;  fm < table + len; )
	{
		/*
		 * Rewrite each command in the table with any
		 * special key abbreviations expanded.
		 */
		for (to = fm;  *fm != '\0'; )
		{
			if (*fm != SK_SPECIAL_KEY)
			{
				*to++ = *fm++;
				continue;
			}
			/*
			 * After SK_SPECIAL_KEY, next byte is the type
			 * of special key (one of the SK_* contants),
			 * and the byte after that is the number of bytes,
			 * N, reserved by the abbreviation (including the
			 * SK_SPECIAL_KEY and key type bytes).
			 * Replace all N bytes with the actual bytes
			 * output by the special key on this terminal.
			 */
			repl = special_key_str(fm[1]);
			klen = fm[2] & 0377;
			fm += klen;
			if (repl == NULL || (int) strlen(repl) > klen)
				repl = "\377";
			while (*repl != '\0')
				*to++ = *repl++;
		}
		*to++ = '\0';
		/*
		 * Fill any unused bytes between end of command and 
		 * the action byte with A_SKIP.
		 */
		while (to <= fm)
			*to++ = A_SKIP;
		fm++;
		a = *fm++ & 0377;
		if (a & A_EXTRA)
		{
			while (*fm++ != '\0')
				continue;
		}
	}
}

/*
 * Expand special key abbreviations in a list of command tables.
 */
	static void
expand_cmd_table(tlist)
	struct tablelist *tlist;
{
	struct tablelist *t;
	for (t = tlist;  t != NULL;  t = t->t_next)
	{
		expand_special_keys(t->t_start, t->t_end - t->t_start);
	}
}

/*
 * Expand special key abbreviations in all command tables.
 */
	public void
expand_cmd_tables()
{
	expand_cmd_table(list_fcmd_tables);
	expand_cmd_table(list_ecmd_tables);
	expand_cmd_table(list_var_tables);
	expand_cmd_table(list_sysvar_tables);
}


/*
 * Initialize the command lists.
 */
	public void
init_cmds()
{
	/*
	 * Add the default command tables.
	 */
	add_fcmd_table((char*)cmdtable, sizeof(cmdtable));
	add_ecmd_table((char*)edittable, sizeof(edittable));
#if USERFILE
	/*
	 * For backwards compatibility,
	 * try to add tables in the OLD system lesskey file.
	 */
#ifdef BINDIR
	add_hometable(NULL, BINDIR "/.sysless", 1);
#endif
	/*
	 * Try to add the tables in the system lesskey file.
	 */
	add_hometable("LESSKEY_SYSTEM", LESSKEYFILE_SYS, 1);
	/*
	 * Try to add the tables in the standard lesskey file "$HOME/.less".
	 */
	add_hometable("LESSKEY", LESSKEYFILE, 0);
#endif
}

/*
 * Add a command table.
 */
	static int
add_cmd_table(tlist, buf, len)
	struct tablelist **tlist;
	char *buf;
	int len;
{
	struct tablelist *t;

	if (len == 0)
		return (0);
	/*
	 * Allocate a tablelist structure, initialize it, 
	 * and link it into the list of tables.
	 */
	if ((t = (struct tablelist *) 
			calloc(1, sizeof(struct tablelist))) == NULL)
	{
		return (-1);
	}
	t->t_start = buf;
	t->t_end = buf + len;
	t->t_next = *tlist;
	*tlist = t;
	return (0);
}

/*
 * Add a command table.
 */
	public void
add_fcmd_table(buf, len)
	char *buf;
	int len;
{
	if (add_cmd_table(&list_fcmd_tables, buf, len) < 0)
		error("Warning: some commands disabled", NULL_PARG);
}

/*
 * Add an editing command table.
 */
	public void
add_ecmd_table(buf, len)
	char *buf;
	int len;
{
	if (add_cmd_table(&list_ecmd_tables, buf, len) < 0)
		error("Warning: some edit commands disabled", NULL_PARG);
}

/*
 * Add an environment variable table.
 */
	static void
add_var_table(tlist, buf, len)
	struct tablelist **tlist;
	char *buf;
	int len;
{
	if (add_cmd_table(tlist, buf, len) < 0)
		error("Warning: environment variables from lesskey file unavailable", NULL_PARG);
}

/*
 * Search a single command table for the command string in cmd.
 */
	static int
cmd_search(cmd, table, endtable, sp)
	char *cmd;
	char *table;
	char *endtable;
	char **sp;
{
	char *p;
	char *q;
	int a;

	*sp = NULL;
	for (p = table, q = cmd;  p < endtable;  p++, q++)
	{
		if (*p == *q)
		{
			/*
			 * Current characters match.
			 * If we're at the end of the string, we've found it.
			 * Return the action code, which is the character
			 * after the null at the end of the string
			 * in the command table.
			 */
			if (*p == '\0')
			{
				a = *++p & 0377;
				while (a == A_SKIP)
					a = *++p & 0377;
				if (a == A_END_LIST)
				{
					/*
					 * We get here only if the original
					 * cmd string passed in was empty ("").
					 * I don't think that can happen,
					 * but just in case ...
					 */
					return (A_UINVALID);
				}
				/*
				 * Check for an "extra" string.
				 */
				if (a & A_EXTRA)
				{
					*sp = ++p;
					a &= ~A_EXTRA;
				}
				return (a);
			}
		} else if (*q == '\0')
		{
			/*
			 * Hit the end of the user's command,
			 * but not the end of the string in the command table.
			 * The user's command is incomplete.
			 */
			return (A_PREFIX);
		} else
		{
			/*
			 * Not a match.
			 * Skip ahead to the next command in the
			 * command table, and reset the pointer
			 * to the beginning of the user's command.
			 */
			if (*p == '\0' && p[1] == A_END_LIST)
			{
				/*
				 * A_END_LIST is a special marker that tells 
				 * us to abort the cmd search.
				 */
				return (A_UINVALID);
			}
			while (*p++ != '\0')
				continue;
			while (*p == A_SKIP)
				p++;
			if (*p & A_EXTRA)
				while (*++p != '\0')
					continue;
			q = cmd-1;
		}
	}
	/*
	 * No match found in the entire command table.
	 */
	return (A_INVALID);
}

/*
 * Decode a command character and return the associated action.
 * The "extra" string, if any, is returned in sp.
 */
	static int
cmd_decode(tlist, cmd, sp)
	struct tablelist *tlist;
	char *cmd;
	char **sp;
{
	struct tablelist *t;
	int action = A_INVALID;

	/*
	 * Search thru all the command tables.
	 * Stop when we find an action which is not A_INVALID.
	 */
	for (t = tlist;  t != NULL;  t = t->t_next)
	{
		action = cmd_search(cmd, t->t_start, t->t_end, sp);
		if (action != A_INVALID)
			break;
	}
	if (action == A_UINVALID)
		action = A_INVALID;
	return (action);
}

/*
 * Decode a command from the cmdtables list.
 */
	public int
fcmd_decode(cmd, sp)
	char *cmd;
	char **sp;
{
	return (cmd_decode(list_fcmd_tables, cmd, sp));
}

/*
 * Decode a command from the edittables list.
 */
	public int
ecmd_decode(cmd, sp)
	char *cmd;
	char **sp;
{
	return (cmd_decode(list_ecmd_tables, cmd, sp));
}

/*
 * Get the value of an environment variable.
 * Looks first in the lesskey file, then in the real environment.
 */
	public char *
lgetenv(var)
	char *var;
{
	int a;
	char *s;

	a = cmd_decode(list_var_tables, var, &s);
	if (a == EV_OK)
		return (s);
	s = getenv(var);
	if (s != NULL && *s != '\0')
		return (s);
	a = cmd_decode(list_sysvar_tables, var, &s);
	if (a == EV_OK)
		return (s);
	return (NULL);
}

#if USERFILE
/*
 * Get an "integer" from a lesskey file.
 * Integers are stored in a funny format: 
 * two bytes, low order first, in radix KRADIX.
 */
	static int
gint(sp)
	char **sp;
{
	int n;

	n = *(*sp)++;
	n += *(*sp)++ * KRADIX;
	return (n);
}

/*
 * Process an old (pre-v241) lesskey file.
 */
	static int
old_lesskey(buf, len)
	char *buf;
	int len;
{
	/*
	 * Old-style lesskey file.
	 * The file must end with either 
	 *     ...,cmd,0,action
	 * or  ...,cmd,0,action|A_EXTRA,string,0
	 * So the last byte or the second to last byte must be zero.
	 */
	if (buf[len-1] != '\0' && buf[len-2] != '\0')
		return (-1);
	add_fcmd_table(buf, len);
	return (0);
}

/* 
 * Process a new (post-v241) lesskey file.
 */
	static int
new_lesskey(buf, len, sysvar)
	char *buf;
	int len;
	int sysvar;
{
	char *p;
	int c;
	int n;

	/*
	 * New-style lesskey file.
	 * Extract the pieces.
	 */
	if (buf[len-3] != C0_END_LESSKEY_MAGIC ||
	    buf[len-2] != C1_END_LESSKEY_MAGIC ||
	    buf[len-1] != C2_END_LESSKEY_MAGIC)
		return (-1);
	p = buf + 4;
	for (;;)
	{
		c = *p++;
		switch (c)
		{
		case CMD_SECTION:
			n = gint(&p);
			add_fcmd_table(p, n);
			p += n;
			break;
		case EDIT_SECTION:
			n = gint(&p);
			add_ecmd_table(p, n);
			p += n;
			break;
		case VAR_SECTION:
			n = gint(&p);
			add_var_table((sysvar) ? 
				&list_sysvar_tables : &list_var_tables, p, n);
			p += n;
			break;
		case END_SECTION:
			return (0);
		default:
			/*
			 * Unrecognized section type.
			 */
			return (-1);
		}
	}
}

/*
 * Set up a user command table, based on a "lesskey" file.
 */
	public int
lesskey(filename, sysvar)
	char *filename;
	int sysvar;
{
	char *buf;
	POSITION len;
	long n;
	int f;

	if (secure)
		return (1);
	/*
	 * Try to open the lesskey file.
	 */
	f = open(filename, OPEN_READ);
	if (f < 0)
		return (1);

	/*
	 * Read the file into a buffer.
	 * We first figure out the size of the file and allocate space for it.
	 * {{ Minimal error checking is done here.
	 *    A garbage .less file will produce strange results.
	 *    To avoid a large amount of error checking code here, we
	 *    rely on the lesskey program to generate a good .less file. }}
	 */
	len = filesize(f);
	if (len == NULL_POSITION || len < 3)
	{
		/*
		 * Bad file (valid file must have at least 3 chars).
		 */
		close(f);
		return (-1);
	}
	if ((buf = (char *) calloc((int)len, sizeof(char))) == NULL)
	{
		close(f);
		return (-1);
	}
	if (lseek(f, (off_t)0, SEEK_SET) == BAD_LSEEK)
	{
		free(buf);
		close(f);
		return (-1);
	}
	n = read(f, buf, (unsigned int) len);
	close(f);
	if (n != len)
	{
		free(buf);
		return (-1);
	}

	/*
	 * Figure out if this is an old-style (before version 241)
	 * or new-style lesskey file format.
	 */
	if (buf[0] != C0_LESSKEY_MAGIC || buf[1] != C1_LESSKEY_MAGIC ||
	    buf[2] != C2_LESSKEY_MAGIC || buf[3] != C3_LESSKEY_MAGIC)
		return (old_lesskey(buf, (int)len));
	return (new_lesskey(buf, (int)len, sysvar));
}

/*
 * Add the standard lesskey file "$HOME/.less"
 */
	public void
add_hometable(envname, def_filename, sysvar)
	char *envname;
	char *def_filename;
	int sysvar;
{
	char *filename;
	PARG parg;

	if (envname != NULL && (filename = lgetenv(envname)) != NULL)
		filename = save(filename);
	else if (sysvar)
		filename = save(def_filename);
	else
		filename = homefile(def_filename);
	if (filename == NULL)
		return;
	if (lesskey(filename, sysvar) < 0)
	{
		parg.p_string = filename;
		error("Cannot use lesskey file \"%s\"", &parg);
	}
	free(filename);
}
#endif

/*
 * See if a char is a special line-editing command.
 */
	public int
editchar(c, flags)
	int c;
	int flags;
{
	int action;
	int nch;
	char *s;
	char usercmd[MAX_CMDLEN+1];
	
	/*
	 * An editing character could actually be a sequence of characters;
	 * for example, an escape sequence sent by pressing the uparrow key.
	 * To match the editing string, we use the command decoder
	 * but give it the edit-commands command table
	 * This table is constructed to match the user's keyboard.
	 */
	if (c == erase_char || c == erase2_char)
		return (EC_BACKSPACE);
	if (c == kill_char)
		return (EC_LINEKILL);
		
	/*
	 * Collect characters in a buffer.
	 * Start with the one we have, and get more if we need them.
	 */
	nch = 0;
	do {
	  	if (nch > 0)
			c = getcc();
		usercmd[nch] = c;
		usercmd[nch+1] = '\0';
		nch++;
		action = ecmd_decode(usercmd, &s);
	} while (action == A_PREFIX);
	
	if (flags & EC_NORIGHTLEFT)
	{
		switch (action)
		{
		case EC_RIGHT:
		case EC_LEFT:
			action = A_INVALID;
			break;
		}
	}
#if CMD_HISTORY
	if (flags & EC_NOHISTORY) 
	{
		/*
		 * The caller says there is no history list.
		 * Reject any history-manipulation action.
		 */
		switch (action)
		{
		case EC_UP:
		case EC_DOWN:
			action = A_INVALID;
			break;
		}
	}
#endif
#if TAB_COMPLETE_FILENAME
	if (flags & EC_NOCOMPLETE) 
	{
		/*
		 * The caller says we don't want any filename completion cmds.
		 * Reject them.
		 */
		switch (action)
		{
		case EC_F_COMPLETE:
		case EC_B_COMPLETE:
		case EC_EXPAND:
			action = A_INVALID;
			break;
		}
	}
#endif
	if ((flags & EC_PEEK) || action == A_INVALID)
	{
		/*
		 * We're just peeking, or we didn't understand the command.
		 * Unget all the characters we read in the loop above.
		 * This does NOT include the original character that was 
		 * passed in as a parameter.
		 */
		while (nch > 1) 
		{
			ungetcc(usercmd[--nch]);
		}
	} else
	{
		if (s != NULL)
			ungetsc(s);
	}
	return action;
}

