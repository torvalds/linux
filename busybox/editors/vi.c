/* vi: set sw=4 ts=4: */
/*
 * tiny vi.c: A small 'vi' clone
 * Copyright (C) 2000, 2001 Sterling Huxley <sterling@europa.com>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/*
 * Things To Do:
 *	EXINIT
 *	$HOME/.exrc  and  ./.exrc
 *	add magic to search	/foo.*bar
 *	add :help command
 *	:map macros
 *	if mark[] values were line numbers rather than pointers
 *      it would be easier to change the mark when add/delete lines
 *	More intelligence in refresh()
 *	":r !cmd"  and  "!cmd"  to filter text through an external command
 *	An "ex" line oriented mode- maybe using "cmdedit"
 */
//config:config VI
//config:	bool "vi (22 kb)"
//config:	default y
//config:	help
//config:	'vi' is a text editor. More specifically, it is the One True
//config:	text editor <grin>. It does, however, have a rather steep
//config:	learning curve. If you are not already comfortable with 'vi'
//config:	you may wish to use something else.
//config:
//config:config FEATURE_VI_MAX_LEN
//config:	int "Maximum screen width"
//config:	range 256 16384
//config:	default 4096
//config:	depends on VI
//config:	help
//config:	Contrary to what you may think, this is not eating much.
//config:	Make it smaller than 4k only if you are very limited on memory.
//config:
//config:config FEATURE_VI_8BIT
//config:	bool "Allow to display 8-bit chars (otherwise shows dots)"
//config:	default n
//config:	depends on VI
//config:	help
//config:	If your terminal can display characters with high bit set,
//config:	you may want to enable this. Note: vi is not Unicode-capable.
//config:	If your terminal combines several 8-bit bytes into one character
//config:	(as in Unicode mode), this will not work properly.
//config:
//config:config FEATURE_VI_COLON
//config:	bool "Enable \":\" colon commands (no \"ex\" mode)"
//config:	default y
//config:	depends on VI
//config:	help
//config:	Enable a limited set of colon commands. This does not
//config:	provide an "ex" mode.
//config:
//config:config FEATURE_VI_YANKMARK
//config:	bool "Enable yank/put commands and mark cmds"
//config:	default y
//config:	depends on VI
//config:	help
//config:	This enables you to use yank and put, as well as mark.
//config:
//config:config FEATURE_VI_SEARCH
//config:	bool "Enable search and replace cmds"
//config:	default y
//config:	depends on VI
//config:	help
//config:	Select this if you wish to be able to do search and replace.
//config:
//config:config FEATURE_VI_REGEX_SEARCH
//config:	bool "Enable regex in search and replace"
//config:	default n   # Uses GNU regex, which may be unavailable. FIXME
//config:	depends on FEATURE_VI_SEARCH
//config:	help
//config:	Use extended regex search.
//config:
//config:config FEATURE_VI_USE_SIGNALS
//config:	bool "Catch signals"
//config:	default y
//config:	depends on VI
//config:	help
//config:	Selecting this option will make vi signal aware. This will support
//config:	SIGWINCH to deal with Window Changes, catch ^Z and ^C and alarms.
//config:
//config:config FEATURE_VI_DOT_CMD
//config:	bool "Remember previous cmd and \".\" cmd"
//config:	default y
//config:	depends on VI
//config:	help
//config:	Make vi remember the last command and be able to repeat it.
//config:
//config:config FEATURE_VI_READONLY
//config:	bool "Enable -R option and \"view\" mode"
//config:	default y
//config:	depends on VI
//config:	help
//config:	Enable the read-only command line option, which allows the user to
//config:	open a file in read-only mode.
//config:
//config:config FEATURE_VI_SETOPTS
//config:	bool "Enable settable options, ai ic showmatch"
//config:	default y
//config:	depends on VI
//config:	help
//config:	Enable the editor to set some (ai, ic, showmatch) options.
//config:
//config:config FEATURE_VI_SET
//config:	bool "Support :set"
//config:	default y
//config:	depends on VI
//config:
//config:config FEATURE_VI_WIN_RESIZE
//config:	bool "Handle window resize"
//config:	default y
//config:	depends on VI
//config:	help
//config:	Behave nicely with terminals that get resized.
//config:
//config:config FEATURE_VI_ASK_TERMINAL
//config:	bool "Use 'tell me cursor position' ESC sequence to measure window"
//config:	default y
//config:	depends on VI
//config:	help
//config:	If terminal size can't be retrieved and $LINES/$COLUMNS are not set,
//config:	this option makes vi perform a last-ditch effort to find it:
//config:	position cursor to 999,999 and ask terminal to report real
//config:	cursor position using "ESC [ 6 n" escape sequence, then read stdin.
//config:	This is not clean but helps a lot on serial lines and such.
//config:
//config:config FEATURE_VI_UNDO
//config:	bool "Support undo command \"u\""
//config:	default y
//config:	depends on VI
//config:	help
//config:	Support the 'u' command to undo insertion, deletion, and replacement
//config:	of text.
//config:
//config:config FEATURE_VI_UNDO_QUEUE
//config:	bool "Enable undo operation queuing"
//config:	default y
//config:	depends on FEATURE_VI_UNDO
//config:	help
//config:	The vi undo functions can use an intermediate queue to greatly lower
//config:	malloc() calls and overhead. When the maximum size of this queue is
//config:	reached, the contents of the queue are committed to the undo stack.
//config:	This increases the size of the undo code and allows some undo
//config:	operations (especially un-typing/backspacing) to be far more useful.
//config:
//config:config FEATURE_VI_UNDO_QUEUE_MAX
//config:	int "Maximum undo character queue size"
//config:	default 256
//config:	range 32 65536
//config:	depends on FEATURE_VI_UNDO_QUEUE
//config:	help
//config:	This option sets the number of bytes used at runtime for the queue.
//config:	Smaller values will create more undo objects and reduce the amount
//config:	of typed or backspaced characters that are grouped into one undo
//config:	operation; larger values increase the potential size of each undo
//config:	and will generally malloc() larger objects and less frequently.
//config:	Unless you want more (or less) frequent "undo points" while typing,
//config:	you should probably leave this unchanged.

//applet:IF_VI(APPLET(vi, BB_DIR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_VI) += vi.o

//usage:#define vi_trivial_usage
//usage:       "[OPTIONS] [FILE]..."
//usage:#define vi_full_usage "\n\n"
//usage:       "Edit FILE\n"
//usage:	IF_FEATURE_VI_COLON(
//usage:     "\n	-c CMD	Initial command to run ($EXINIT also available)"
//usage:	)
//usage:	IF_FEATURE_VI_READONLY(
//usage:     "\n	-R	Read-only"
//usage:	)
//usage:     "\n	-H	List available features"

#include "libbb.h"
/* Should be after libbb.h: on some systems regex.h needs sys/types.h: */
#if ENABLE_FEATURE_VI_REGEX_SEARCH
# include <regex.h>
#endif

/* the CRASHME code is unmaintained, and doesn't currently build */
#define ENABLE_FEATURE_VI_CRASHME 0


#if ENABLE_LOCALE_SUPPORT

#if ENABLE_FEATURE_VI_8BIT
//FIXME: this does not work properly for Unicode anyway
# define Isprint(c) (isprint)(c)
#else
# define Isprint(c) isprint_asciionly(c)
#endif

#else

/* 0x9b is Meta-ESC */
#if ENABLE_FEATURE_VI_8BIT
# define Isprint(c) ((unsigned char)(c) >= ' ' && (c) != 0x7f && (unsigned char)(c) != 0x9b)
#else
# define Isprint(c) ((unsigned char)(c) >= ' ' && (unsigned char)(c) < 0x7f)
#endif

#endif


enum {
	MAX_TABSTOP = 32, // sanity limit
	// User input len. Need not be extra big.
	// Lines in file being edited *can* be bigger than this.
	MAX_INPUT_LEN = 128,
	// Sanity limits. We have only one buffer of this size.
	MAX_SCR_COLS = CONFIG_FEATURE_VI_MAX_LEN,
	MAX_SCR_ROWS = CONFIG_FEATURE_VI_MAX_LEN,
};

/* VT102 ESC sequences.
 * See "Xterm Control Sequences"
 * http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 */
#define ESC "\033"
/* Inverse/Normal text */
#define ESC_BOLD_TEXT ESC"[7m"
#define ESC_NORM_TEXT ESC"[m"
/* Bell */
#define ESC_BELL "\007"
/* Clear-to-end-of-line */
#define ESC_CLEAR2EOL ESC"[K"
/* Clear-to-end-of-screen.
 * (We use default param here.
 * Full sequence is "ESC [ <num> J",
 * <num> is 0/1/2 = "erase below/above/all".)
 */
#define ESC_CLEAR2EOS ESC"[J"
/* Cursor to given coordinate (1,1: top left) */
#define ESC_SET_CURSOR_POS ESC"[%u;%uH"
//UNUSED
///* Cursor up and down */
//#define ESC_CURSOR_UP   ESC"[A"
//#define ESC_CURSOR_DOWN "\n"

#if ENABLE_FEATURE_VI_DOT_CMD || ENABLE_FEATURE_VI_YANKMARK
// cmds modifying text[]
// vda: removed "aAiIs" as they switch us into insert mode
// and remembering input for replay after them makes no sense
static const char modifying_cmds[] ALIGN1 = "cCdDJoOpPrRxX<>~";
#endif

enum {
	YANKONLY = FALSE,
	YANKDEL = TRUE,
	FORWARD = 1,	// code depends on "1"  for array index
	BACK = -1,	// code depends on "-1" for array index
	LIMITED = 0,	// how much of text[] in char_search
	FULL = 1,	// how much of text[] in char_search

	S_BEFORE_WS = 1,	// used in skip_thing() for moving "dot"
	S_TO_WS = 2,		// used in skip_thing() for moving "dot"
	S_OVER_WS = 3,		// used in skip_thing() for moving "dot"
	S_END_PUNCT = 4,	// used in skip_thing() for moving "dot"
	S_END_ALNUM = 5,	// used in skip_thing() for moving "dot"
};


/* vi.c expects chars to be unsigned. */
/* busybox build system provides that, but it's better */
/* to audit and fix the source */

struct globals {
	/* many references - keep near the top of globals */
	char *text, *end;       // pointers to the user data in memory
	char *dot;              // where all the action takes place
	int text_size;		// size of the allocated buffer

	/* the rest */
	smallint vi_setops;
#define VI_AUTOINDENT 1
#define VI_SHOWMATCH  2
#define VI_IGNORECASE 4
#define VI_ERR_METHOD 8
#define autoindent (vi_setops & VI_AUTOINDENT)
#define showmatch  (vi_setops & VI_SHOWMATCH )
#define ignorecase (vi_setops & VI_IGNORECASE)
/* indicate error with beep or flash */
#define err_method (vi_setops & VI_ERR_METHOD)

#if ENABLE_FEATURE_VI_READONLY
	smallint readonly_mode;
#define SET_READONLY_FILE(flags)        ((flags) |= 0x01)
#define SET_READONLY_MODE(flags)        ((flags) |= 0x02)
#define UNSET_READONLY_FILE(flags)      ((flags) &= 0xfe)
#else
#define SET_READONLY_FILE(flags)        ((void)0)
#define SET_READONLY_MODE(flags)        ((void)0)
#define UNSET_READONLY_FILE(flags)      ((void)0)
#endif

	smallint editing;        // >0 while we are editing a file
	                         // [code audit says "can be 0, 1 or 2 only"]
	smallint cmd_mode;       // 0=command  1=insert 2=replace
	int modified_count;      // buffer contents changed if !0
	int last_modified_count; // = -1;
	int save_argc;           // how many file names on cmd line
	int cmdcnt;              // repetition count
	unsigned rows, columns;	 // the terminal screen is this size
#if ENABLE_FEATURE_VI_ASK_TERMINAL
	int get_rowcol_error;
#endif
	int crow, ccol;          // cursor is on Crow x Ccol
	int offset;              // chars scrolled off the screen to the left
	int have_status_msg;     // is default edit status needed?
	                         // [don't make smallint!]
	int last_status_cksum;   // hash of current status line
	char *current_filename;
	char *screenbegin;       // index into text[], of top line on the screen
	char *screen;            // pointer to the virtual screen buffer
	int screensize;          //            and its size
	int tabstop;
	int last_forward_char;   // last char searched for with 'f' (int because of Unicode)
	char erase_char;         // the users erase character
	char last_input_char;    // last char read from user

#if ENABLE_FEATURE_VI_DOT_CMD
	smallint adding2q;	 // are we currently adding user input to q
	int lmc_len;             // length of last_modifying_cmd
	char *ioq, *ioq_start;   // pointer to string for get_one_char to "read"
#endif
#if ENABLE_FEATURE_VI_USE_SIGNALS || ENABLE_FEATURE_VI_CRASHME
	int my_pid;
#endif
#if ENABLE_FEATURE_VI_SEARCH
	char *last_search_pattern; // last pattern from a '/' or '?' search
#endif

	/* former statics */
#if ENABLE_FEATURE_VI_YANKMARK
	char *edit_file__cur_line;
#endif
	int refresh__old_offset;
	int format_edit_status__tot;

	/* a few references only */
#if ENABLE_FEATURE_VI_YANKMARK
	int YDreg, Ureg;        // default delete register and orig line for "U"
	char *reg[28];          // named register a-z, "D", and "U" 0-25,26,27
	char *mark[28];         // user marks points somewhere in text[]-  a-z and previous context ''
	char *context_start, *context_end;
#endif
#if ENABLE_FEATURE_VI_USE_SIGNALS
	sigjmp_buf restart;     // catch_sig()
#endif
	struct termios term_orig; // remember what the cooked mode was
#if ENABLE_FEATURE_VI_COLON
	char *initial_cmds[3];  // currently 2 entries, NULL terminated
#endif
	// Should be just enough to hold a key sequence,
	// but CRASHME mode uses it as generated command buffer too
#if ENABLE_FEATURE_VI_CRASHME
	char readbuffer[128];
#else
	char readbuffer[KEYCODE_BUFFER_SIZE];
#endif
#define STATUS_BUFFER_LEN  200
	char status_buffer[STATUS_BUFFER_LEN]; // messages to the user
#if ENABLE_FEATURE_VI_DOT_CMD
	char last_modifying_cmd[MAX_INPUT_LEN];	// last modifying cmd for "."
#endif
	char get_input_line__buf[MAX_INPUT_LEN]; /* former static */

	char scr_out_buf[MAX_SCR_COLS + MAX_TABSTOP * 2];
#if ENABLE_FEATURE_VI_UNDO
// undo_push() operations
#define UNDO_INS         0
#define UNDO_DEL         1
#define UNDO_INS_CHAIN   2
#define UNDO_DEL_CHAIN   3
// UNDO_*_QUEUED must be equal to UNDO_xxx ORed with UNDO_QUEUED_FLAG
#define UNDO_QUEUED_FLAG 4
#define UNDO_INS_QUEUED  4
#define UNDO_DEL_QUEUED  5
#define UNDO_USE_SPOS   32
#define UNDO_EMPTY      64
// Pass-through flags for functions that can be undone
#define NO_UNDO          0
#define ALLOW_UNDO       1
#define ALLOW_UNDO_CHAIN 2
# if ENABLE_FEATURE_VI_UNDO_QUEUE
#define ALLOW_UNDO_QUEUED 3
	char undo_queue_state;
	int undo_q;
	char *undo_queue_spos;	// Start position of queued operation
	char undo_queue[CONFIG_FEATURE_VI_UNDO_QUEUE_MAX];
# else
// If undo queuing disabled, don't invoke the missing queue logic
#define ALLOW_UNDO_QUEUED 1
# endif

	struct undo_object {
		struct undo_object *prev;	// Linking back avoids list traversal (LIFO)
		int start;		// Offset where the data should be restored/deleted
		int length;		// total data size
		uint8_t u_type;		// 0=deleted, 1=inserted, 2=swapped
		char undo_text[1];	// text that was deleted (if deletion)
	} *undo_stack_tail;
#endif /* ENABLE_FEATURE_VI_UNDO */
};
#define G (*ptr_to_globals)
#define text           (G.text          )
#define text_size      (G.text_size     )
#define end            (G.end           )
#define dot            (G.dot           )
#define reg            (G.reg           )

#define vi_setops               (G.vi_setops          )
#define editing                 (G.editing            )
#define cmd_mode                (G.cmd_mode           )
#define modified_count          (G.modified_count     )
#define last_modified_count     (G.last_modified_count)
#define save_argc               (G.save_argc          )
#define cmdcnt                  (G.cmdcnt             )
#define rows                    (G.rows               )
#define columns                 (G.columns            )
#define crow                    (G.crow               )
#define ccol                    (G.ccol               )
#define offset                  (G.offset             )
#define status_buffer           (G.status_buffer      )
#define have_status_msg         (G.have_status_msg    )
#define last_status_cksum       (G.last_status_cksum  )
#define current_filename        (G.current_filename   )
#define screen                  (G.screen             )
#define screensize              (G.screensize         )
#define screenbegin             (G.screenbegin        )
#define tabstop                 (G.tabstop            )
#define last_forward_char       (G.last_forward_char  )
#define erase_char              (G.erase_char         )
#define last_input_char         (G.last_input_char    )
#if ENABLE_FEATURE_VI_READONLY
#define readonly_mode           (G.readonly_mode      )
#else
#define readonly_mode           0
#endif
#define adding2q                (G.adding2q           )
#define lmc_len                 (G.lmc_len            )
#define ioq                     (G.ioq                )
#define ioq_start               (G.ioq_start          )
#define my_pid                  (G.my_pid             )
#define last_search_pattern     (G.last_search_pattern)

#define edit_file__cur_line     (G.edit_file__cur_line)
#define refresh__old_offset     (G.refresh__old_offset)
#define format_edit_status__tot (G.format_edit_status__tot)

#define YDreg          (G.YDreg         )
#define Ureg           (G.Ureg          )
#define mark           (G.mark          )
#define context_start  (G.context_start )
#define context_end    (G.context_end   )
#define restart        (G.restart       )
#define term_orig      (G.term_orig     )
#define initial_cmds   (G.initial_cmds  )
#define readbuffer     (G.readbuffer    )
#define scr_out_buf    (G.scr_out_buf   )
#define last_modifying_cmd  (G.last_modifying_cmd )
#define get_input_line__buf (G.get_input_line__buf)

#if ENABLE_FEATURE_VI_UNDO
#define undo_stack_tail  (G.undo_stack_tail )
# if ENABLE_FEATURE_VI_UNDO_QUEUE
#define undo_queue_state (G.undo_queue_state)
#define undo_q           (G.undo_q          )
#define undo_queue       (G.undo_queue      )
#define undo_queue_spos  (G.undo_queue_spos )
# endif
#endif

#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
	last_modified_count = -1; \
	/* "" but has space for 2 chars: */ \
	IF_FEATURE_VI_SEARCH(last_search_pattern = xzalloc(2);) \
} while (0)


static void edit_file(char *);	// edit one file
static void do_cmd(int);	// execute a command
static int next_tabstop(int);
static void sync_cursor(char *, int *, int *);	// synchronize the screen cursor to dot
static char *begin_line(char *);	// return pointer to cur line B-o-l
static char *end_line(char *);	// return pointer to cur line E-o-l
static char *prev_line(char *);	// return pointer to prev line B-o-l
static char *next_line(char *);	// return pointer to next line B-o-l
static char *end_screen(void);	// get pointer to last char on screen
static int count_lines(char *, char *);	// count line from start to stop
static char *find_line(int);	// find beginning of line #li
static char *move_to_col(char *, int);	// move "p" to column l
static void dot_left(void);	// move dot left- dont leave line
static void dot_right(void);	// move dot right- dont leave line
static void dot_begin(void);	// move dot to B-o-l
static void dot_end(void);	// move dot to E-o-l
static void dot_next(void);	// move dot to next line B-o-l
static void dot_prev(void);	// move dot to prev line B-o-l
static void dot_scroll(int, int);	// move the screen up or down
static void dot_skip_over_ws(void);	// move dot pat WS
static char *bound_dot(char *);	// make sure  text[0] <= P < "end"
static char *new_screen(int, int);	// malloc virtual screen memory
#if !ENABLE_FEATURE_VI_UNDO
#define char_insert(a,b,c) char_insert(a,b)
#endif
static char *char_insert(char *, char, int);	// insert the char c at 'p'
// might reallocate text[]! use p += stupid_insert(p, ...),
// and be careful to not use pointers into potentially freed text[]!
static uintptr_t stupid_insert(char *, char);	// stupidly insert the char c at 'p'
static int find_range(char **, char **, char);	// return pointers for an object
static int st_test(char *, int, int, char *);	// helper for skip_thing()
static char *skip_thing(char *, int, int, int);	// skip some object
static char *find_pair(char *, char);	// find matching pair ()  []  {}
#if !ENABLE_FEATURE_VI_UNDO
#define text_hole_delete(a,b,c) text_hole_delete(a,b)
#endif
static char *text_hole_delete(char *, char *, int);	// at "p", delete a 'size' byte hole
// might reallocate text[]! use p += text_hole_make(p, ...),
// and be careful to not use pointers into potentially freed text[]!
static uintptr_t text_hole_make(char *, int);	// at "p", make a 'size' byte hole
#if !ENABLE_FEATURE_VI_UNDO
#define yank_delete(a,b,c,d,e) yank_delete(a,b,c,d)
#endif
static char *yank_delete(char *, char *, int, int, int);	// yank text[] into register then delete
static void show_help(void);	// display some help info
static void rawmode(void);	// set "raw" mode on tty
static void cookmode(void);	// return to "cooked" mode on tty
// sleep for 'h' 1/100 seconds, return 1/0 if stdin is (ready for read)/(not ready)
static int mysleep(int);
static int readit(void);	// read (maybe cursor) key from stdin
static int get_one_char(void);	// read 1 char from stdin
// file_insert might reallocate text[]!
static int file_insert(const char *, char *, int);
static int file_write(char *, char *, char *);
static void place_cursor(int, int);
static void screen_erase(void);
static void clear_to_eol(void);
static void clear_to_eos(void);
static void go_bottom_and_clear_to_eol(void);
static void standout_start(void);	// send "start reverse video" sequence
static void standout_end(void);	// send "end reverse video" sequence
static void flash(int);		// flash the terminal screen
static void show_status_line(void);	// put a message on the bottom line
static void status_line(const char *, ...);     // print to status buf
static void status_line_bold(const char *, ...);
static void status_line_bold_errno(const char *fn);
static void not_implemented(const char *); // display "Not implemented" message
static int format_edit_status(void);	// format file status on status line
static void redraw(int);	// force a full screen refresh
static char* format_line(char* /*, int*/);
static void refresh(int);	// update the terminal from screen[]

static void indicate_error(void);       // use flash or beep to indicate error
static void Hit_Return(void);

#if ENABLE_FEATURE_VI_SEARCH
static char *char_search(char *, const char *, int, int);	// search for pattern starting at p
#endif
#if ENABLE_FEATURE_VI_COLON
static char *get_one_address(char *, int *);	// get colon addr, if present
static char *get_address(char *, int *, int *);	// get two colon addrs, if present
#endif
static void colon(char *);	// execute the "colon" mode cmds
#if ENABLE_FEATURE_VI_USE_SIGNALS
static void winch_sig(int);	// catch window size changes
static void suspend_sig(int);	// catch ctrl-Z
static void catch_sig(int);     // catch ctrl-C and alarm time-outs
#endif
#if ENABLE_FEATURE_VI_DOT_CMD
static void start_new_cmd_q(char);	// new queue for command
static void end_cmd_q(void);	// stop saving input chars
#else
#define end_cmd_q() ((void)0)
#endif
#if ENABLE_FEATURE_VI_SETOPTS
static void showmatching(char *);	// show the matching pair ()  []  {}
#endif
#if ENABLE_FEATURE_VI_YANKMARK || (ENABLE_FEATURE_VI_COLON && ENABLE_FEATURE_VI_SEARCH) || ENABLE_FEATURE_VI_CRASHME
// might reallocate text[]! use p += string_insert(p, ...),
// and be careful to not use pointers into potentially freed text[]!
# if !ENABLE_FEATURE_VI_UNDO
#define string_insert(a,b,c) string_insert(a,b)
# endif
static uintptr_t string_insert(char *, const char *, int);	// insert the string at 'p'
#endif
#if ENABLE_FEATURE_VI_YANKMARK
static char *text_yank(char *, char *, int);	// save copy of "p" into a register
static char what_reg(void);		// what is letter of current YDreg
static void check_context(char);	// remember context for '' command
#endif
#if ENABLE_FEATURE_VI_UNDO
static void flush_undo_data(void);
static void undo_push(char *, unsigned int, unsigned char);	// Push an operation on the undo stack
static void undo_pop(void);	// Undo the last operation
# if ENABLE_FEATURE_VI_UNDO_QUEUE
static void undo_queue_commit(void);	// Flush any queued objects to the undo stack
# else
# define undo_queue_commit() ((void)0)
# endif
#else
#define flush_undo_data()   ((void)0)
#define undo_queue_commit() ((void)0)
#endif

#if ENABLE_FEATURE_VI_CRASHME
static void crash_dummy();
static void crash_test();
static int crashme = 0;
#endif

static void write1(const char *out)
{
	fputs(out, stdout);
}

int vi_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int vi_main(int argc, char **argv)
{
	int c;

	INIT_G();

#if ENABLE_FEATURE_VI_UNDO
	/* undo_stack_tail = NULL; - already is */
#if ENABLE_FEATURE_VI_UNDO_QUEUE
	undo_queue_state = UNDO_EMPTY;
	/* undo_q = 0; - already is  */
#endif
#endif

#if ENABLE_FEATURE_VI_USE_SIGNALS || ENABLE_FEATURE_VI_CRASHME
	my_pid = getpid();
#endif
#if ENABLE_FEATURE_VI_CRASHME
	srand((long) my_pid);
#endif
#ifdef NO_SUCH_APPLET_YET
	/* If we aren't "vi", we are "view" */
	if (ENABLE_FEATURE_VI_READONLY && applet_name[2]) {
		SET_READONLY_MODE(readonly_mode);
	}
#endif

	// autoindent is not default in vim 7.3
	vi_setops = /*VI_AUTOINDENT |*/ VI_SHOWMATCH | VI_IGNORECASE;
	//  1-  process $HOME/.exrc file (not inplemented yet)
	//  2-  process EXINIT variable from environment
	//  3-  process command line args
#if ENABLE_FEATURE_VI_COLON
	{
		char *p = getenv("EXINIT");
		if (p && *p)
			initial_cmds[0] = xstrndup(p, MAX_INPUT_LEN);
	}
#endif
	while ((c = getopt(argc, argv, "hCRH" IF_FEATURE_VI_COLON("c:"))) != -1) {
		switch (c) {
#if ENABLE_FEATURE_VI_CRASHME
		case 'C':
			crashme = 1;
			break;
#endif
#if ENABLE_FEATURE_VI_READONLY
		case 'R':		// Read-only flag
			SET_READONLY_MODE(readonly_mode);
			break;
#endif
#if ENABLE_FEATURE_VI_COLON
		case 'c':		// cmd line vi command
			if (*optarg)
				initial_cmds[initial_cmds[0] != NULL] = xstrndup(optarg, MAX_INPUT_LEN);
			break;
#endif
		case 'H':
			show_help();
			/* fall through */
		default:
			bb_show_usage();
			return 1;
		}
	}

	// The argv array can be used by the ":next"  and ":rewind" commands
	argv += optind;
	argc -= optind;

	//----- This is the main file handling loop --------------
	save_argc = argc;
	optind = 0;
	// "Save cursor, use alternate screen buffer, clear screen"
	write1(ESC"[?1049h");
	while (1) {
		edit_file(argv[optind]); /* param might be NULL */
		if (++optind >= argc)
			break;
	}
	// "Use normal screen buffer, restore cursor"
	write1(ESC"[?1049l");
	//-----------------------------------------------------------

	return 0;
}

/* read text from file or create an empty buf */
/* will also update current_filename */
static int init_text_buffer(char *fn)
{
	int rc;

	/* allocate/reallocate text buffer */
	free(text);
	text_size = 10240;
	screenbegin = dot = end = text = xzalloc(text_size);

	if (fn != current_filename) {
		free(current_filename);
		current_filename = xstrdup(fn);
	}
	rc = file_insert(fn, text, 1);
	if (rc < 0) {
		// file doesnt exist. Start empty buf with dummy line
		char_insert(text, '\n', NO_UNDO);
	}

	flush_undo_data();
	modified_count = 0;
	last_modified_count = -1;
#if ENABLE_FEATURE_VI_YANKMARK
	/* init the marks */
	memset(mark, 0, sizeof(mark));
#endif
	return rc;
}

#if ENABLE_FEATURE_VI_WIN_RESIZE
static int query_screen_dimensions(void)
{
	int err = get_terminal_width_height(STDIN_FILENO, &columns, &rows);
	if (rows > MAX_SCR_ROWS)
		rows = MAX_SCR_ROWS;
	if (columns > MAX_SCR_COLS)
		columns = MAX_SCR_COLS;
	return err;
}
#else
static ALWAYS_INLINE int query_screen_dimensions(void)
{
	return 0;
}
#endif

static void edit_file(char *fn)
{
#if ENABLE_FEATURE_VI_YANKMARK
#define cur_line edit_file__cur_line
#endif
	int c;
#if ENABLE_FEATURE_VI_USE_SIGNALS
	int sig;
#endif

	editing = 1;	// 0 = exit, 1 = one file, 2 = multiple files
	rawmode();
	rows = 24;
	columns = 80;
	IF_FEATURE_VI_ASK_TERMINAL(G.get_rowcol_error =) query_screen_dimensions();
#if ENABLE_FEATURE_VI_ASK_TERMINAL
	if (G.get_rowcol_error /* TODO? && no input on stdin */) {
		uint64_t k;
		write1(ESC"[999;999H" ESC"[6n");
		fflush_all();
		k = read_key(STDIN_FILENO, readbuffer, /*timeout_ms:*/ 100);
		if ((int32_t)k == KEYCODE_CURSOR_POS) {
			uint32_t rc = (k >> 32);
			columns = (rc & 0x7fff);
			if (columns > MAX_SCR_COLS)
				columns = MAX_SCR_COLS;
			rows = ((rc >> 16) & 0x7fff);
			if (rows > MAX_SCR_ROWS)
				rows = MAX_SCR_ROWS;
		}
	}
#endif
	new_screen(rows, columns);	// get memory for virtual screen
	init_text_buffer(fn);

#if ENABLE_FEATURE_VI_YANKMARK
	YDreg = 26;			// default Yank/Delete reg
	Ureg = 27;			// hold orig line for "U" cmd
	mark[26] = mark[27] = text;	// init "previous context"
#endif

	last_forward_char = last_input_char = '\0';
	crow = 0;
	ccol = 0;

#if ENABLE_FEATURE_VI_USE_SIGNALS
	signal(SIGINT, catch_sig);
	signal(SIGWINCH, winch_sig);
	signal(SIGTSTP, suspend_sig);
	sig = sigsetjmp(restart, 1);
	if (sig != 0) {
		screenbegin = dot = text;
	}
#endif

	cmd_mode = 0;		// 0=command  1=insert  2='R'eplace
	cmdcnt = 0;
	tabstop = 8;
	offset = 0;			// no horizontal offset
	c = '\0';
#if ENABLE_FEATURE_VI_DOT_CMD
	free(ioq_start);
	ioq = ioq_start = NULL;
	lmc_len = 0;
	adding2q = 0;
#endif

#if ENABLE_FEATURE_VI_COLON
	{
		char *p, *q;
		int n = 0;

		while ((p = initial_cmds[n]) != NULL) {
			do {
				q = p;
				p = strchr(q, '\n');
				if (p)
					while (*p == '\n')
						*p++ = '\0';
				if (*q)
					colon(q);
			} while (p);
			free(initial_cmds[n]);
			initial_cmds[n] = NULL;
			n++;
		}
	}
#endif
	redraw(FALSE);			// dont force every col re-draw
	//------This is the main Vi cmd handling loop -----------------------
	while (editing > 0) {
#if ENABLE_FEATURE_VI_CRASHME
		if (crashme > 0) {
			if ((end - text) > 1) {
				crash_dummy();	// generate a random command
			} else {
				crashme = 0;
				string_insert(text, "\n\n#####  Ran out of text to work on.  #####\n\n", NO_UNDO); // insert the string
				dot = text;
				refresh(FALSE);
			}
		}
#endif
		last_input_char = c = get_one_char();	// get a cmd from user
#if ENABLE_FEATURE_VI_YANKMARK
		// save a copy of the current line- for the 'U" command
		if (begin_line(dot) != cur_line) {
			cur_line = begin_line(dot);
			text_yank(begin_line(dot), end_line(dot), Ureg);
		}
#endif
#if ENABLE_FEATURE_VI_DOT_CMD
		// These are commands that change text[].
		// Remember the input for the "." command
		if (!adding2q && ioq_start == NULL
		 && cmd_mode == 0 // command mode
		 && c > '\0' // exclude NUL and non-ASCII chars
		 && c < 0x7f // (Unicode and such)
		 && strchr(modifying_cmds, c)
		) {
			start_new_cmd_q(c);
		}
#endif
		do_cmd(c);		// execute the user command

		// poll to see if there is input already waiting. if we are
		// not able to display output fast enough to keep up, skip
		// the display update until we catch up with input.
		if (!readbuffer[0] && mysleep(0) == 0) {
			// no input pending - so update output
			refresh(FALSE);
			show_status_line();
		}
#if ENABLE_FEATURE_VI_CRASHME
		if (crashme > 0)
			crash_test();	// test editor variables
#endif
	}
	//-------------------------------------------------------------------

	go_bottom_and_clear_to_eol();
	cookmode();
#undef cur_line
}

//----- The Colon commands -------------------------------------
#if ENABLE_FEATURE_VI_COLON
static char *get_one_address(char *p, int *addr)	// get colon addr, if present
{
	int st;
	char *q;
	IF_FEATURE_VI_YANKMARK(char c;)
	IF_FEATURE_VI_SEARCH(char *pat;)

	*addr = -1;			// assume no addr
	if (*p == '.') {	// the current line
		p++;
		q = begin_line(dot);
		*addr = count_lines(text, q);
	}
#if ENABLE_FEATURE_VI_YANKMARK
	else if (*p == '\'') {	// is this a mark addr
		p++;
		c = tolower(*p);
		p++;
		if (c >= 'a' && c <= 'z') {
			// we have a mark
			c = c - 'a';
			q = mark[(unsigned char) c];
			if (q != NULL) {	// is mark valid
				*addr = count_lines(text, q);
			}
		}
	}
#endif
#if ENABLE_FEATURE_VI_SEARCH
	else if (*p == '/') {	// a search pattern
		q = strchrnul(++p, '/');
		pat = xstrndup(p, q - p); // save copy of pattern
		p = q;
		if (*p == '/')
			p++;
		q = char_search(dot, pat, FORWARD, FULL);
		if (q != NULL) {
			*addr = count_lines(text, q);
		}
		free(pat);
	}
#endif
	else if (*p == '$') {	// the last line in file
		p++;
		q = begin_line(end - 1);
		*addr = count_lines(text, q);
	} else if (isdigit(*p)) {	// specific line number
		sscanf(p, "%d%n", addr, &st);
		p += st;
	} else {
		// unrecognized address - assume -1
		*addr = -1;
	}
	return p;
}

static char *get_address(char *p, int *b, int *e)	// get two colon addrs, if present
{
	//----- get the address' i.e., 1,3   'a,'b  -----
	// get FIRST addr, if present
	while (isblank(*p))
		p++;				// skip over leading spaces
	if (*p == '%') {			// alias for 1,$
		p++;
		*b = 1;
		*e = count_lines(text, end-1);
		goto ga0;
	}
	p = get_one_address(p, b);
	while (isblank(*p))
		p++;
	if (*p == ',') {			// is there a address separator
		p++;
		while (isblank(*p))
			p++;
		// get SECOND addr, if present
		p = get_one_address(p, e);
	}
 ga0:
	while (isblank(*p))
		p++;				// skip over trailing spaces
	return p;
}

#if ENABLE_FEATURE_VI_SET && ENABLE_FEATURE_VI_SETOPTS
static void setops(const char *args, const char *opname, int flg_no,
			const char *short_opname, int opt)
{
	const char *a = args + flg_no;
	int l = strlen(opname) - 1; /* opname have + ' ' */

	// maybe strncmp? we had tons of erroneous strncasecmp's...
	if (strncasecmp(a, opname, l) == 0
	 || strncasecmp(a, short_opname, 2) == 0
	) {
		if (flg_no)
			vi_setops &= ~opt;
		else
			vi_setops |= opt;
	}
}
#endif

#endif /* FEATURE_VI_COLON */

// buf must be no longer than MAX_INPUT_LEN!
static void colon(char *buf)
{
#if !ENABLE_FEATURE_VI_COLON
	/* Simple ":cmd" handler with minimal set of commands */
	char *p = buf;
	int cnt;

	if (*p == ':')
		p++;
	cnt = strlen(p);
	if (cnt == 0)
		return;
	if (strncmp(p, "quit", cnt) == 0
	 || strncmp(p, "q!", cnt) == 0
	) {
		if (modified_count && p[1] != '!') {
			status_line_bold("No write since last change (:%s! overrides)", p);
		} else {
			editing = 0;
		}
		return;
	}
	if (strncmp(p, "write", cnt) == 0
	 || strncmp(p, "wq", cnt) == 0
	 || strncmp(p, "wn", cnt) == 0
	 || (p[0] == 'x' && !p[1])
	) {
		if (modified_count != 0 || p[0] != 'x') {
			cnt = file_write(current_filename, text, end - 1);
		}
		if (cnt < 0) {
			if (cnt == -1)
				status_line_bold("Write error: "STRERROR_FMT STRERROR_ERRNO);
		} else {
			modified_count = 0;
			last_modified_count = -1;
			status_line("'%s' %dL, %dC",
				current_filename,
				count_lines(text, end - 1), cnt
			);
			if (p[0] == 'x'
			 || p[1] == 'q' || p[1] == 'n'
			 || p[1] == 'Q' || p[1] == 'N'
			) {
				editing = 0;
			}
		}
		return;
	}
	if (strncmp(p, "file", cnt) == 0) {
		last_status_cksum = 0;	// force status update
		return;
	}
	if (sscanf(p, "%d", &cnt) > 0) {
		dot = find_line(cnt);
		dot_skip_over_ws();
		return;
	}
	not_implemented(p);
#else

	char c, *buf1, *q, *r;
	char *fn, cmd[MAX_INPUT_LEN], args[MAX_INPUT_LEN];
	int i, l, li, b, e;
	int useforce;
# if ENABLE_FEATURE_VI_SEARCH || ENABLE_FEATURE_ALLOW_EXEC
	char *orig_buf;
# endif

	// :3154	// if (-e line 3154) goto it  else stay put
	// :4,33w! foo	// write a portion of buffer to file "foo"
	// :w		// write all of buffer to current file
	// :q		// quit
	// :q!		// quit- dont care about modified file
	// :'a,'z!sort -u   // filter block through sort
	// :'f		// goto mark "f"
	// :'fl		// list literal the mark "f" line
	// :.r bar	// read file "bar" into buffer before dot
	// :/123/,/abc/d    // delete lines from "123" line to "abc" line
	// :/xyz/	// goto the "xyz" line
	// :s/find/replace/ // substitute pattern "find" with "replace"
	// :!<cmd>	// run <cmd> then return
	//

	if (!buf[0])
		goto ret;
	if (*buf == ':')
		buf++;			// move past the ':'

	li = i = 0;
	b = e = -1;
	q = text;			// assume 1,$ for the range
	r = end - 1;
	li = count_lines(text, end - 1);
	fn = current_filename;

	// look for optional address(es)  :.  :1  :1,9   :'q,'a   :%
	buf = get_address(buf, &b, &e);

# if ENABLE_FEATURE_VI_SEARCH || ENABLE_FEATURE_ALLOW_EXEC
	// remember orig command line
	orig_buf = buf;
# endif

	// get the COMMAND into cmd[]
	buf1 = cmd;
	while (*buf != '\0') {
		if (isspace(*buf))
			break;
		*buf1++ = *buf++;
	}
	*buf1 = '\0';
	// get any ARGuments
	while (isblank(*buf))
		buf++;
	strcpy(args, buf);
	useforce = FALSE;
	buf1 = last_char_is(cmd, '!');
	if (buf1) {
		useforce = TRUE;
		*buf1 = '\0';   // get rid of !
	}
	if (b >= 0) {
		// if there is only one addr, then the addr
		// is the line number of the single line the
		// user wants. So, reset the end
		// pointer to point at end of the "b" line
		q = find_line(b);	// what line is #b
		r = end_line(q);
		li = 1;
	}
	if (e >= 0) {
		// we were given two addrs.  change the
		// end pointer to the addr given by user.
		r = find_line(e);	// what line is #e
		r = end_line(r);
		li = e - b + 1;
	}
	// ------------ now look for the command ------------
	i = strlen(cmd);
	if (i == 0) {		// :123CR goto line #123
		if (b >= 0) {
			dot = find_line(b);	// what line is #b
			dot_skip_over_ws();
		}
	}
# if ENABLE_FEATURE_ALLOW_EXEC
	else if (cmd[0] == '!') {	// run a cmd
		int retcode;
		// :!ls   run the <cmd>
		go_bottom_and_clear_to_eol();
		cookmode();
		retcode = system(orig_buf + 1);	// run the cmd
		if (retcode)
			printf("\nshell returned %i\n\n", retcode);
		rawmode();
		Hit_Return();			// let user see results
	}
# endif
	else if (cmd[0] == '=' && !cmd[1]) {	// where is the address
		if (b < 0) {	// no addr given- use defaults
			b = e = count_lines(text, dot);
		}
		status_line("%d", b);
	} else if (strncmp(cmd, "delete", i) == 0) {	// delete lines
		if (b < 0) {	// no addr given- use defaults
			q = begin_line(dot);	// assume .,. for the range
			r = end_line(dot);
		}
		dot = yank_delete(q, r, 1, YANKDEL, ALLOW_UNDO);	// save, then delete lines
		dot_skip_over_ws();
	} else if (strncmp(cmd, "edit", i) == 0) {	// Edit a file
		int size;

		// don't edit, if the current file has been modified
		if (modified_count && !useforce) {
			status_line_bold("No write since last change (:%s! overrides)", cmd);
			goto ret;
		}
		if (args[0]) {
			// the user supplied a file name
			fn = args;
		} else if (current_filename && current_filename[0]) {
			// no user supplied name- use the current filename
			// fn = current_filename;  was set by default
		} else {
			// no user file name, no current name- punt
			status_line_bold("No current filename");
			goto ret;
		}

		size = init_text_buffer(fn);

# if ENABLE_FEATURE_VI_YANKMARK
		if (Ureg >= 0 && Ureg < 28) {
			free(reg[Ureg]);	//   free orig line reg- for 'U'
			reg[Ureg] = NULL;
		}
		if (YDreg >= 0 && YDreg < 28) {
			free(reg[YDreg]);	//   free default yank/delete register
			reg[YDreg] = NULL;
		}
# endif
		// how many lines in text[]?
		li = count_lines(text, end - 1);
		status_line("'%s'%s"
			IF_FEATURE_VI_READONLY("%s")
			" %dL, %dC",
			current_filename,
			(size < 0 ? " [New file]" : ""),
			IF_FEATURE_VI_READONLY(
				((readonly_mode) ? " [Readonly]" : ""),
			)
			li, (int)(end - text)
		);
	} else if (strncmp(cmd, "file", i) == 0) {	// what File is this
		if (b != -1 || e != -1) {
			status_line_bold("No address allowed on this command");
			goto ret;
		}
		if (args[0]) {
			// user wants a new filename
			free(current_filename);
			current_filename = xstrdup(args);
		} else {
			// user wants file status info
			last_status_cksum = 0;	// force status update
		}
	} else if (strncmp(cmd, "features", i) == 0) {	// what features are available
		// print out values of all features
		go_bottom_and_clear_to_eol();
		cookmode();
		show_help();
		rawmode();
		Hit_Return();
	} else if (strncmp(cmd, "list", i) == 0) {	// literal print line
		if (b < 0) {	// no addr given- use defaults
			q = begin_line(dot);	// assume .,. for the range
			r = end_line(dot);
		}
		go_bottom_and_clear_to_eol();
		puts("\r");
		for (; q <= r; q++) {
			int c_is_no_print;

			c = *q;
			c_is_no_print = (c & 0x80) && !Isprint(c);
			if (c_is_no_print) {
				c = '.';
				standout_start();
			}
			if (c == '\n') {
				write1("$\r");
			} else if (c < ' ' || c == 127) {
				bb_putchar('^');
				if (c == 127)
					c = '?';
				else
					c += '@';
			}
			bb_putchar(c);
			if (c_is_no_print)
				standout_end();
		}
		Hit_Return();
	} else if (strncmp(cmd, "quit", i) == 0 // quit
	        || strncmp(cmd, "next", i) == 0 // edit next file
	        || strncmp(cmd, "prev", i) == 0 // edit previous file
	) {
		int n;
		if (useforce) {
			if (*cmd == 'q') {
				// force end of argv list
				optind = save_argc;
			}
			editing = 0;
			goto ret;
		}
		// don't exit if the file been modified
		if (modified_count) {
			status_line_bold("No write since last change (:%s! overrides)", cmd);
			goto ret;
		}
		// are there other file to edit
		n = save_argc - optind - 1;
		if (*cmd == 'q' && n > 0) {
			status_line_bold("%d more file(s) to edit", n);
			goto ret;
		}
		if (*cmd == 'n' && n <= 0) {
			status_line_bold("No more files to edit");
			goto ret;
		}
		if (*cmd == 'p') {
			// are there previous files to edit
			if (optind < 1) {
				status_line_bold("No previous files to edit");
				goto ret;
			}
			optind -= 2;
		}
		editing = 0;
	} else if (strncmp(cmd, "read", i) == 0) {	// read file into text[]
		int size;

		fn = args;
		if (!fn[0]) {
			status_line_bold("No filename given");
			goto ret;
		}
		if (b < 0) {	// no addr given- use defaults
			q = begin_line(dot);	// assume "dot"
		}
		// read after current line- unless user said ":0r foo"
		if (b != 0) {
			q = next_line(q);
			// read after last line
			if (q == end-1)
				++q;
		}
		{ // dance around potentially-reallocated text[]
			uintptr_t ofs = q - text;
			size = file_insert(fn, q, 0);
			q = text + ofs;
		}
		if (size < 0)
			goto ret;	// nothing was inserted
		// how many lines in text[]?
		li = count_lines(q, q + size - 1);
		status_line("'%s'"
			IF_FEATURE_VI_READONLY("%s")
			" %dL, %dC",
			fn,
			IF_FEATURE_VI_READONLY((readonly_mode ? " [Readonly]" : ""),)
			li, size
		);
		if (size > 0) {
			// if the insert is before "dot" then we need to update
			if (q <= dot)
				dot += size;
		}
	} else if (strncmp(cmd, "rewind", i) == 0) {	// rewind cmd line args
		if (modified_count && !useforce) {
			status_line_bold("No write since last change (:%s! overrides)", cmd);
		} else {
			// reset the filenames to edit
			optind = -1; /* start from 0th file */
			editing = 0;
		}
# if ENABLE_FEATURE_VI_SET
	} else if (strncmp(cmd, "set", i) == 0) {	// set or clear features
#  if ENABLE_FEATURE_VI_SETOPTS
		char *argp;
#  endif
		i = 0;			// offset into args
		// only blank is regarded as args delimiter. What about tab '\t'?
		if (!args[0] || strcasecmp(args, "all") == 0) {
			// print out values of all options
#  if ENABLE_FEATURE_VI_SETOPTS
			status_line_bold(
				"%sautoindent "
				"%sflash "
				"%signorecase "
				"%sshowmatch "
				"tabstop=%u",
				autoindent ? "" : "no",
				err_method ? "" : "no",
				ignorecase ? "" : "no",
				showmatch ? "" : "no",
				tabstop
			);
#  endif
			goto ret;
		}
#  if ENABLE_FEATURE_VI_SETOPTS
		argp = args;
		while (*argp) {
			if (strncmp(argp, "no", 2) == 0)
				i = 2;		// ":set noautoindent"
			setops(argp, "autoindent ", i, "ai", VI_AUTOINDENT);
			setops(argp, "flash "     , i, "fl", VI_ERR_METHOD);
			setops(argp, "ignorecase ", i, "ic", VI_IGNORECASE);
			setops(argp, "showmatch " , i, "sm", VI_SHOWMATCH );
			if (strncmp(argp + i, "tabstop=", 8) == 0) {
				int t = 0;
				sscanf(argp + i+8, "%u", &t);
				if (t > 0 && t <= MAX_TABSTOP)
					tabstop = t;
			}
			argp = skip_non_whitespace(argp);
			argp = skip_whitespace(argp);
		}
#  endif /* FEATURE_VI_SETOPTS */
# endif /* FEATURE_VI_SET */

# if ENABLE_FEATURE_VI_SEARCH
	} else if (cmd[0] == 's') {	// substitute a pattern with a replacement pattern
		char *F, *R, *flags;
		size_t len_F, len_R;
		int gflag;		// global replace flag
#  if ENABLE_FEATURE_VI_UNDO
		int dont_chain_first_item = ALLOW_UNDO;
#  endif

		// F points to the "find" pattern
		// R points to the "replace" pattern
		// replace the cmd line delimiters "/" with NULs
		c = orig_buf[1];	// what is the delimiter
		F = orig_buf + 2;	// start of "find"
		R = strchr(F, c);	// middle delimiter
		if (!R)
			goto colon_s_fail;
		len_F = R - F;
		*R++ = '\0';	// terminate "find"
		flags = strchr(R, c);
		if (!flags)
			goto colon_s_fail;
		len_R = flags - R;
		*flags++ = '\0';	// terminate "replace"
		gflag = *flags;

		q = begin_line(q);
		if (b < 0) {	// maybe :s/foo/bar/
			q = begin_line(dot);      // start with cur line
			b = count_lines(text, q); // cur line number
		}
		if (e < 0)
			e = b;		// maybe :.s/foo/bar/

		for (i = b; i <= e; i++) {	// so, :20,23 s \0 find \0 replace \0
			char *ls = q;		// orig line start
			char *found;
 vc4:
			found = char_search(q, F, FORWARD, LIMITED);	// search cur line only for "find"
			if (found) {
				uintptr_t bias;
				// we found the "find" pattern - delete it
				// For undo support, the first item should not be chained
				text_hole_delete(found, found + len_F - 1, dont_chain_first_item);
#  if ENABLE_FEATURE_VI_UNDO
				dont_chain_first_item = ALLOW_UNDO_CHAIN;
#  endif
				// insert the "replace" patern
				bias = string_insert(found, R, ALLOW_UNDO_CHAIN);
				found += bias;
				ls += bias;
				/*q += bias; - recalculated anyway */
				// check for "global"  :s/foo/bar/g
				if (gflag == 'g') {
					if ((found + len_R) < end_line(ls)) {
						q = found + len_R;
						goto vc4;	// don't let q move past cur line
					}
				}
			}
			q = next_line(ls);
		}
# endif /* FEATURE_VI_SEARCH */
	} else if (strncmp(cmd, "version", i) == 0) {  // show software version
		status_line(BB_VER);
	} else if (strncmp(cmd, "write", i) == 0  // write text to file
	        || strncmp(cmd, "wq", i) == 0
	        || strncmp(cmd, "wn", i) == 0
	        || (cmd[0] == 'x' && !cmd[1])
	) {
		int size;
		//int forced = FALSE;

		// is there a file name to write to?
		if (args[0]) {
			fn = args;
		}
# if ENABLE_FEATURE_VI_READONLY
		if (readonly_mode && !useforce) {
			status_line_bold("'%s' is read only", fn);
			goto ret;
		}
# endif
		//if (useforce) {
			// if "fn" is not write-able, chmod u+w
			// sprintf(syscmd, "chmod u+w %s", fn);
			// system(syscmd);
			// forced = TRUE;
		//}
		if (modified_count != 0 || cmd[0] != 'x') {
			size = r - q + 1;
			l = file_write(fn, q, r);
		} else {
			size = 0;
			l = 0;
		}
		//if (useforce && forced) {
			// chmod u-w
			// sprintf(syscmd, "chmod u-w %s", fn);
			// system(syscmd);
			// forced = FALSE;
		//}
		if (l < 0) {
			if (l == -1)
				status_line_bold_errno(fn);
		} else {
			// how many lines written
			li = count_lines(q, q + l - 1);
			status_line("'%s' %dL, %dC", fn, li, l);
			if (l == size) {
				if (q == text && q + l == end) {
					modified_count = 0;
					last_modified_count = -1;
				}
				if (cmd[0] == 'x'
				 || cmd[1] == 'q' || cmd[1] == 'n'
				 || cmd[1] == 'Q' || cmd[1] == 'N'
				) {
					editing = 0;
				}
			}
		}
# if ENABLE_FEATURE_VI_YANKMARK
	} else if (strncmp(cmd, "yank", i) == 0) {	// yank lines
		if (b < 0) {	// no addr given- use defaults
			q = begin_line(dot);	// assume .,. for the range
			r = end_line(dot);
		}
		text_yank(q, r, YDreg);
		li = count_lines(q, r);
		status_line("Yank %d lines (%d chars) into [%c]",
				li, strlen(reg[YDreg]), what_reg());
# endif
	} else {
		// cmd unknown
		not_implemented(cmd);
	}
 ret:
	dot = bound_dot(dot);	// make sure "dot" is valid
	return;
# if ENABLE_FEATURE_VI_SEARCH
 colon_s_fail:
	status_line(":s expression missing delimiters");
# endif
#endif /* FEATURE_VI_COLON */
}

static void Hit_Return(void)
{
	int c;

	standout_start();
	write1("[Hit return to continue]");
	standout_end();
	while ((c = get_one_char()) != '\n' && c != '\r')
		continue;
	redraw(TRUE);		// force redraw all
}

static int next_tabstop(int col)
{
	return col + ((tabstop - 1) - (col % tabstop));
}

//----- Synchronize the cursor to Dot --------------------------
static NOINLINE void sync_cursor(char *d, int *row, int *col)
{
	char *beg_cur;	// begin and end of "d" line
	char *tp;
	int cnt, ro, co;

	beg_cur = begin_line(d);	// first char of cur line

	if (beg_cur < screenbegin) {
		// "d" is before top line on screen
		// how many lines do we have to move
		cnt = count_lines(beg_cur, screenbegin);
 sc1:
		screenbegin = beg_cur;
		if (cnt > (rows - 1) / 2) {
			// we moved too many lines. put "dot" in middle of screen
			for (cnt = 0; cnt < (rows - 1) / 2; cnt++) {
				screenbegin = prev_line(screenbegin);
			}
		}
	} else {
		char *end_scr;	// begin and end of screen
		end_scr = end_screen();	// last char of screen
		if (beg_cur > end_scr) {
			// "d" is after bottom line on screen
			// how many lines do we have to move
			cnt = count_lines(end_scr, beg_cur);
			if (cnt > (rows - 1) / 2)
				goto sc1;	// too many lines
			for (ro = 0; ro < cnt - 1; ro++) {
				// move screen begin the same amount
				screenbegin = next_line(screenbegin);
				// now, move the end of screen
				end_scr = next_line(end_scr);
				end_scr = end_line(end_scr);
			}
		}
	}
	// "d" is on screen- find out which row
	tp = screenbegin;
	for (ro = 0; ro < rows - 1; ro++) {	// drive "ro" to correct row
		if (tp == beg_cur)
			break;
		tp = next_line(tp);
	}

	// find out what col "d" is on
	co = 0;
	while (tp < d) { // drive "co" to correct column
		if (*tp == '\n') //vda || *tp == '\0')
			break;
		if (*tp == '\t') {
			// handle tabs like real vi
			if (d == tp && cmd_mode) {
				break;
			}
			co = next_tabstop(co);
		} else if ((unsigned char)*tp < ' ' || *tp == 0x7f) {
			co++; // display as ^X, use 2 columns
		}
		co++;
		tp++;
	}

	// "co" is the column where "dot" is.
	// The screen has "columns" columns.
	// The currently displayed columns are  0+offset -- columns+ofset
	// |-------------------------------------------------------------|
	//               ^ ^                                ^
	//        offset | |------- columns ----------------|
	//
	// If "co" is already in this range then we do not have to adjust offset
	//      but, we do have to subtract the "offset" bias from "co".
	// If "co" is outside this range then we have to change "offset".
	// If the first char of a line is a tab the cursor will try to stay
	//  in column 7, but we have to set offset to 0.

	if (co < 0 + offset) {
		offset = co;
	}
	if (co >= columns + offset) {
		offset = co - columns + 1;
	}
	// if the first char of the line is a tab, and "dot" is sitting on it
	//  force offset to 0.
	if (d == beg_cur && *d == '\t') {
		offset = 0;
	}
	co -= offset;

	*row = ro;
	*col = co;
}

//----- Text Movement Routines ---------------------------------
static char *begin_line(char *p) // return pointer to first char cur line
{
	if (p > text) {
		p = memrchr(text, '\n', p - text);
		if (!p)
			return text;
		return p + 1;
	}
	return p;
}

static char *end_line(char *p) // return pointer to NL of cur line
{
	if (p < end - 1) {
		p = memchr(p, '\n', end - p - 1);
		if (!p)
			return end - 1;
	}
	return p;
}

static char *dollar_line(char *p) // return pointer to just before NL line
{
	p = end_line(p);
	// Try to stay off of the Newline
	if (*p == '\n' && (p - begin_line(p)) > 0)
		p--;
	return p;
}

static char *prev_line(char *p) // return pointer first char prev line
{
	p = begin_line(p);	// goto beginning of cur line
	if (p > text && p[-1] == '\n')
		p--;			// step to prev line
	p = begin_line(p);	// goto beginning of prev line
	return p;
}

static char *next_line(char *p) // return pointer first char next line
{
	p = end_line(p);
	if (p < end - 1 && *p == '\n')
		p++;			// step to next line
	return p;
}

//----- Text Information Routines ------------------------------
static char *end_screen(void)
{
	char *q;
	int cnt;

	// find new bottom line
	q = screenbegin;
	for (cnt = 0; cnt < rows - 2; cnt++)
		q = next_line(q);
	q = end_line(q);
	return q;
}

// count line from start to stop
static int count_lines(char *start, char *stop)
{
	char *q;
	int cnt;

	if (stop < start) { // start and stop are backwards- reverse them
		q = start;
		start = stop;
		stop = q;
	}
	cnt = 0;
	stop = end_line(stop);
	while (start <= stop && start <= end - 1) {
		start = end_line(start);
		if (*start == '\n')
			cnt++;
		start++;
	}
	return cnt;
}

static char *find_line(int li)	// find beginning of line #li
{
	char *q;

	for (q = text; li > 1; li--) {
		q = next_line(q);
	}
	return q;
}

//----- Dot Movement Routines ----------------------------------
static void dot_left(void)
{
	undo_queue_commit();
	if (dot > text && dot[-1] != '\n')
		dot--;
}

static void dot_right(void)
{
	undo_queue_commit();
	if (dot < end - 1 && *dot != '\n')
		dot++;
}

static void dot_begin(void)
{
	undo_queue_commit();
	dot = begin_line(dot);	// return pointer to first char cur line
}

static void dot_end(void)
{
	undo_queue_commit();
	dot = end_line(dot);	// return pointer to last char cur line
}

static char *move_to_col(char *p, int l)
{
	int co;

	p = begin_line(p);
	co = 0;
	while (co < l && p < end) {
		if (*p == '\n') //vda || *p == '\0')
			break;
		if (*p == '\t') {
			co = next_tabstop(co);
		} else if (*p < ' ' || *p == 127) {
			co++; // display as ^X, use 2 columns
		}
		co++;
		p++;
	}
	return p;
}

static void dot_next(void)
{
	undo_queue_commit();
	dot = next_line(dot);
}

static void dot_prev(void)
{
	undo_queue_commit();
	dot = prev_line(dot);
}

static void dot_scroll(int cnt, int dir)
{
	char *q;

	undo_queue_commit();
	for (; cnt > 0; cnt--) {
		if (dir < 0) {
			// scroll Backwards
			// ctrl-Y scroll up one line
			screenbegin = prev_line(screenbegin);
		} else {
			// scroll Forwards
			// ctrl-E scroll down one line
			screenbegin = next_line(screenbegin);
		}
	}
	// make sure "dot" stays on the screen so we dont scroll off
	if (dot < screenbegin)
		dot = screenbegin;
	q = end_screen();	// find new bottom line
	if (dot > q)
		dot = begin_line(q);	// is dot is below bottom line?
	dot_skip_over_ws();
}

static void dot_skip_over_ws(void)
{
	// skip WS
	while (isspace(*dot) && *dot != '\n' && dot < end - 1)
		dot++;
}

static char *bound_dot(char *p) // make sure  text[0] <= P < "end"
{
	if (p >= end && end > text) {
		p = end - 1;
		indicate_error();
	}
	if (p < text) {
		p = text;
		indicate_error();
	}
	return p;
}

//----- Helper Utility Routines --------------------------------

//----------------------------------------------------------------
//----- Char Routines --------------------------------------------
/* Chars that are part of a word-
 *    0123456789_ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz
 * Chars that are Not part of a word (stoppers)
 *    !"#$%&'()*+,-./:;<=>?@[\]^`{|}~
 * Chars that are WhiteSpace
 *    TAB NEWLINE VT FF RETURN SPACE
 * DO NOT COUNT NEWLINE AS WHITESPACE
 */

static char *new_screen(int ro, int co)
{
	int li;

	free(screen);
	screensize = ro * co + 8;
	screen = xmalloc(screensize);
	// initialize the new screen. assume this will be a empty file.
	screen_erase();
	//   non-existent text[] lines start with a tilde (~).
	for (li = 1; li < ro - 1; li++) {
		screen[(li * co) + 0] = '~';
	}
	return screen;
}

#if ENABLE_FEATURE_VI_SEARCH

# if ENABLE_FEATURE_VI_REGEX_SEARCH

// search for pattern starting at p
static char *char_search(char *p, const char *pat, int dir, int range)
{
	struct re_pattern_buffer preg;
	const char *err;
	char *q;
	int i;
	int size;

	re_syntax_options = RE_SYNTAX_POSIX_EXTENDED;
	if (ignorecase)
		re_syntax_options = RE_SYNTAX_POSIX_EXTENDED | RE_ICASE;

	memset(&preg, 0, sizeof(preg));
	err = re_compile_pattern(pat, strlen(pat), &preg);
	if (err != NULL) {
		status_line_bold("bad search pattern '%s': %s", pat, err);
		return p;
	}

	// assume a LIMITED forward search
	q = end - 1;
	if (dir == BACK)
		q = text;
	// RANGE could be negative if we are searching backwards
	range = q - p;
	q = p;
	size = range;
	if (range < 0) {
		size = -size;
		q = p - size;
		if (q < text)
			q = text;
	}
	// search for the compiled pattern, preg, in p[]
	// range < 0: search backward
	// range > 0: search forward
	// 0 < start < size
	// re_search() < 0: not found or error
	// re_search() >= 0: index of found pattern
	//           struct pattern   char     int   int    int    struct reg
	// re_search(*pattern_buffer, *string, size, start, range, *regs)
	i = re_search(&preg, q, size, /*start:*/ 0, range, /*struct re_registers*:*/ NULL);
	regfree(&preg);
	if (i < 0)
		return NULL;
	if (dir == FORWARD)
		p = p + i;
	else
		p = p - i;
	return p;
}

# else

#  if ENABLE_FEATURE_VI_SETOPTS
static int mycmp(const char *s1, const char *s2, int len)
{
	if (ignorecase) {
		return strncasecmp(s1, s2, len);
	}
	return strncmp(s1, s2, len);
}
#  else
#   define mycmp strncmp
#  endif

static char *char_search(char *p, const char *pat, int dir, int range)
{
	char *start, *stop;
	int len;

	len = strlen(pat);
	if (dir == FORWARD) {
		stop = end - 1;	// assume range is p..end-1
		if (range == LIMITED)
			stop = next_line(p);	// range is to next line
		for (start = p; start < stop; start++) {
			if (mycmp(start, pat, len) == 0) {
				return start;
			}
		}
	} else if (dir == BACK) {
		stop = text;	// assume range is text..p
		if (range == LIMITED)
			stop = prev_line(p);	// range is to prev line
		for (start = p - len; start >= stop; start--) {
			if (mycmp(start, pat, len) == 0) {
				return start;
			}
		}
	}
	// pattern not found
	return NULL;
}

# endif

#endif /* FEATURE_VI_SEARCH */

static char *char_insert(char *p, char c, int undo) // insert the char c at 'p'
{
	if (c == 22) {		// Is this an ctrl-V?
		p += stupid_insert(p, '^');	// use ^ to indicate literal next
		refresh(FALSE);	// show the ^
		c = get_one_char();
		*p = c;
#if ENABLE_FEATURE_VI_UNDO
		switch (undo) {
			case ALLOW_UNDO:
				undo_push(p, 1, UNDO_INS);
				break;
			case ALLOW_UNDO_CHAIN:
				undo_push(p, 1, UNDO_INS_CHAIN);
				break;
# if ENABLE_FEATURE_VI_UNDO_QUEUE
			case ALLOW_UNDO_QUEUED:
				undo_push(p, 1, UNDO_INS_QUEUED);
				break;
# endif
		}
#else
		modified_count++;
#endif /* ENABLE_FEATURE_VI_UNDO */
		p++;
	} else if (c == 27) {	// Is this an ESC?
		cmd_mode = 0;
		undo_queue_commit();
		cmdcnt = 0;
		end_cmd_q();	// stop adding to q
		last_status_cksum = 0;	// force status update
		if ((p[-1] != '\n') && (dot > text)) {
			p--;
		}
	} else if (c == erase_char || c == 8 || c == 127) { // Is this a BS
		if (p > text) {
			p--;
			p = text_hole_delete(p, p, ALLOW_UNDO_QUEUED);	// shrink buffer 1 char
		}
	} else {
		// insert a char into text[]
		if (c == 13)
			c = '\n';	// translate \r to \n
#if ENABLE_FEATURE_VI_UNDO
# if ENABLE_FEATURE_VI_UNDO_QUEUE
		if (c == '\n')
			undo_queue_commit();
# endif
		switch (undo) {
			case ALLOW_UNDO:
				undo_push(p, 1, UNDO_INS);
				break;
			case ALLOW_UNDO_CHAIN:
				undo_push(p, 1, UNDO_INS_CHAIN);
				break;
# if ENABLE_FEATURE_VI_UNDO_QUEUE
			case ALLOW_UNDO_QUEUED:
				undo_push(p, 1, UNDO_INS_QUEUED);
				break;
# endif
		}
#else
		modified_count++;
#endif /* ENABLE_FEATURE_VI_UNDO */
		p += 1 + stupid_insert(p, c);	// insert the char
#if ENABLE_FEATURE_VI_SETOPTS
		if (showmatch && strchr(")]}", c) != NULL) {
			showmatching(p - 1);
		}
		if (autoindent && c == '\n') {	// auto indent the new line
			char *q;
			size_t len;
			q = prev_line(p);	// use prev line as template
			len = strspn(q, " \t"); // space or tab
			if (len) {
				uintptr_t bias;
				bias = text_hole_make(p, len);
				p += bias;
				q += bias;
#if ENABLE_FEATURE_VI_UNDO
				undo_push(p, len, UNDO_INS);
#endif
				memcpy(p, q, len);
				p += len;
			}
		}
#endif
	}
	return p;
}

// might reallocate text[]! use p += stupid_insert(p, ...),
// and be careful to not use pointers into potentially freed text[]!
static uintptr_t stupid_insert(char *p, char c) // stupidly insert the char c at 'p'
{
	uintptr_t bias;
	bias = text_hole_make(p, 1);
	p += bias;
	*p = c;
	return bias;
}

static int find_range(char **start, char **stop, char c)
{
	char *save_dot, *p, *q, *t;
	int cnt, multiline = 0;

	save_dot = dot;
	p = q = dot;

	if (strchr("cdy><", c)) {
		// these cmds operate on whole lines
		p = q = begin_line(p);
		for (cnt = 1; cnt < cmdcnt; cnt++) {
			q = next_line(q);
		}
		q = end_line(q);
	} else if (strchr("^%$0bBeEfth\b\177", c)) {
		// These cmds operate on char positions
		do_cmd(c);		// execute movement cmd
		q = dot;
	} else if (strchr("wW", c)) {
		do_cmd(c);		// execute movement cmd
		// if we are at the next word's first char
		// step back one char
		// but check the possibilities when it is true
		if (dot > text && ((isspace(dot[-1]) && !isspace(dot[0]))
				|| (ispunct(dot[-1]) && !ispunct(dot[0]))
				|| (isalnum(dot[-1]) && !isalnum(dot[0]))))
			dot--;		// move back off of next word
		if (dot > text && *dot == '\n')
			dot--;		// stay off NL
		q = dot;
	} else if (strchr("H-k{", c)) {
		// these operate on multi-lines backwards
		q = end_line(dot);	// find NL
		do_cmd(c);		// execute movement cmd
		dot_begin();
		p = dot;
	} else if (strchr("L+j}\r\n", c)) {
		// these operate on multi-lines forwards
		p = begin_line(dot);
		do_cmd(c);		// execute movement cmd
		dot_end();		// find NL
		q = dot;
	} else {
		// nothing -- this causes any other values of c to
		// represent the one-character range under the
		// cursor.  this is correct for ' ' and 'l', but
		// perhaps no others.
		//
	}
	if (q < p) {
		t = q;
		q = p;
		p = t;
	}

	// backward char movements don't include start position
	if (q > p && strchr("^0bBh\b\177", c)) q--;

	multiline = 0;
	for (t = p; t <= q; t++) {
		if (*t == '\n') {
			multiline = 1;
			break;
		}
	}

	*start = p;
	*stop = q;
	dot = save_dot;
	return multiline;
}

static int st_test(char *p, int type, int dir, char *tested)
{
	char c, c0, ci;
	int test, inc;

	inc = dir;
	c = c0 = p[0];
	ci = p[inc];
	test = 0;

	if (type == S_BEFORE_WS) {
		c = ci;
		test = (!isspace(c) || c == '\n');
	}
	if (type == S_TO_WS) {
		c = c0;
		test = (!isspace(c) || c == '\n');
	}
	if (type == S_OVER_WS) {
		c = c0;
		test = isspace(c);
	}
	if (type == S_END_PUNCT) {
		c = ci;
		test = ispunct(c);
	}
	if (type == S_END_ALNUM) {
		c = ci;
		test = (isalnum(c) || c == '_');
	}
	*tested = c;
	return test;
}

static char *skip_thing(char *p, int linecnt, int dir, int type)
{
	char c;

	while (st_test(p, type, dir, &c)) {
		// make sure we limit search to correct number of lines
		if (c == '\n' && --linecnt < 1)
			break;
		if (dir >= 0 && p >= end - 1)
			break;
		if (dir < 0 && p <= text)
			break;
		p += dir;		// move to next char
	}
	return p;
}

// find matching char of pair  ()  []  {}
// will crash if c is not one of these
static char *find_pair(char *p, const char c)
{
	const char *braces = "()[]{}";
	char match;
	int dir, level;

	dir = strchr(braces, c) - braces;
	dir ^= 1;
	match = braces[dir];
	dir = ((dir & 1) << 1) - 1; /* 1 for ([{, -1 for )\} */

	// look for match, count levels of pairs  (( ))
	level = 1;
	for (;;) {
		p += dir;
		if (p < text || p >= end)
			return NULL;
		if (*p == c)
			level++;	// increase pair levels
		if (*p == match) {
			level--;	// reduce pair level
			if (level == 0)
				return p; // found matching pair
		}
	}
}

#if ENABLE_FEATURE_VI_SETOPTS
// show the matching char of a pair,  ()  []  {}
static void showmatching(char *p)
{
	char *q, *save_dot;

	// we found half of a pair
	q = find_pair(p, *p);	// get loc of matching char
	if (q == NULL) {
		indicate_error();	// no matching char
	} else {
		// "q" now points to matching pair
		save_dot = dot;	// remember where we are
		dot = q;		// go to new loc
		refresh(FALSE);	// let the user see it
		mysleep(40);	// give user some time
		dot = save_dot;	// go back to old loc
		refresh(FALSE);
	}
}
#endif /* FEATURE_VI_SETOPTS */

#if ENABLE_FEATURE_VI_UNDO
static void flush_undo_data(void)
{
	struct undo_object *undo_entry;

	while (undo_stack_tail) {
		undo_entry = undo_stack_tail;
		undo_stack_tail = undo_entry->prev;
		free(undo_entry);
	}
}

// Undo functions and hooks added by Jody Bruchon (jody@jodybruchon.com)
static void undo_push(char *src, unsigned int length, uint8_t u_type)	// Add to the undo stack
{
	struct undo_object *undo_entry;

	// "u_type" values
	// UNDO_INS: insertion, undo will remove from buffer
	// UNDO_DEL: deleted text, undo will restore to buffer
	// UNDO_{INS,DEL}_CHAIN: Same as above but also calls undo_pop() when complete
	// The CHAIN operations are for handling multiple operations that the user
	// performs with a single action, i.e. REPLACE mode or find-and-replace commands
	// UNDO_{INS,DEL}_QUEUED: If queuing feature is enabled, allow use of the queue
	// for the INS/DEL operation. The raw values should be equal to the values of
	// UNDO_{INS,DEL} ORed with UNDO_QUEUED_FLAG

#if ENABLE_FEATURE_VI_UNDO_QUEUE
	// This undo queuing functionality groups multiple character typing or backspaces
	// into a single large undo object. This greatly reduces calls to malloc() for
	// single-character operations while typing and has the side benefit of letting
	// an undo operation remove chunks of text rather than a single character.
	switch (u_type) {
	case UNDO_EMPTY:	// Just in case this ever happens...
		return;
	case UNDO_DEL_QUEUED:
		if (length != 1)
			return;	// Only queue single characters
		switch (undo_queue_state) {
		case UNDO_EMPTY:
			undo_queue_state = UNDO_DEL;
		case UNDO_DEL:
			undo_queue_spos = src;
			undo_q++;
			undo_queue[CONFIG_FEATURE_VI_UNDO_QUEUE_MAX - undo_q] = *src;
			// If queue is full, dump it into an object
			if (undo_q == CONFIG_FEATURE_VI_UNDO_QUEUE_MAX)
				undo_queue_commit();
			return;
		case UNDO_INS:
			// Switch from storing inserted text to deleted text
			undo_queue_commit();
			undo_push(src, length, UNDO_DEL_QUEUED);
			return;
		}
		break;
	case UNDO_INS_QUEUED:
		if (length != 1)
			return;
		switch (undo_queue_state) {
		case UNDO_EMPTY:
			undo_queue_state = UNDO_INS;
			undo_queue_spos = src;
		case UNDO_INS:
			undo_q++;	// Don't need to save any data for insertions
			if (undo_q == CONFIG_FEATURE_VI_UNDO_QUEUE_MAX)
				undo_queue_commit();
			return;
		case UNDO_DEL:
			// Switch from storing deleted text to inserted text
			undo_queue_commit();
			undo_push(src, length, UNDO_INS_QUEUED);
			return;
		}
		break;
	}
#else
	// If undo queuing is disabled, ignore the queuing flag entirely
	u_type = u_type & ~UNDO_QUEUED_FLAG;
#endif

	// Allocate a new undo object
	if (u_type == UNDO_DEL || u_type == UNDO_DEL_CHAIN) {
		// For UNDO_DEL objects, save deleted text
		if ((src + length) == end)
			length--;
		// If this deletion empties text[], strip the newline. When the buffer becomes
		// zero-length, a newline is added back, which requires this to compensate.
		undo_entry = xzalloc(offsetof(struct undo_object, undo_text) + length);
		memcpy(undo_entry->undo_text, src, length);
	} else {
		undo_entry = xzalloc(sizeof(*undo_entry));
	}
	undo_entry->length = length;
#if ENABLE_FEATURE_VI_UNDO_QUEUE
	if ((u_type & UNDO_USE_SPOS) != 0) {
		undo_entry->start = undo_queue_spos - text;	// use start position from queue
	} else {
		undo_entry->start = src - text;	// use offset from start of text buffer
	}
	u_type = (u_type & ~UNDO_USE_SPOS);
#else
	undo_entry->start = src - text;
#endif
	undo_entry->u_type = u_type;

	// Push it on undo stack
	undo_entry->prev = undo_stack_tail;
	undo_stack_tail = undo_entry;
	modified_count++;
}

static void undo_pop(void)	// Undo the last operation
{
	int repeat;
	char *u_start, *u_end;
	struct undo_object *undo_entry;

	// Commit pending undo queue before popping (should be unnecessary)
	undo_queue_commit();

	undo_entry = undo_stack_tail;
	// Check for an empty undo stack
	if (!undo_entry) {
		status_line("Already at oldest change");
		return;
	}

	switch (undo_entry->u_type) {
	case UNDO_DEL:
	case UNDO_DEL_CHAIN:
		// make hole and put in text that was deleted; deallocate text
		u_start = text + undo_entry->start;
		text_hole_make(u_start, undo_entry->length);
		memcpy(u_start, undo_entry->undo_text, undo_entry->length);
		status_line("Undo [%d] %s %d chars at position %d",
			modified_count, "restored",
			undo_entry->length, undo_entry->start
		);
		break;
	case UNDO_INS:
	case UNDO_INS_CHAIN:
		// delete what was inserted
		u_start = undo_entry->start + text;
		u_end = u_start - 1 + undo_entry->length;
		text_hole_delete(u_start, u_end, NO_UNDO);
		status_line("Undo [%d] %s %d chars at position %d",
			modified_count, "deleted",
			undo_entry->length, undo_entry->start
		);
		break;
	}
	repeat = 0;
	switch (undo_entry->u_type) {
	// If this is the end of a chain, lower modification count and refresh display
	case UNDO_DEL:
	case UNDO_INS:
		dot = (text + undo_entry->start);
		refresh(FALSE);
		break;
	case UNDO_DEL_CHAIN:
	case UNDO_INS_CHAIN:
		repeat = 1;
		break;
	}
	// Deallocate the undo object we just processed
	undo_stack_tail = undo_entry->prev;
	free(undo_entry);
	modified_count--;
	// For chained operations, continue popping all the way down the chain.
	if (repeat) {
		undo_pop();	// Follow the undo chain if one exists
	}
}

#if ENABLE_FEATURE_VI_UNDO_QUEUE
static void undo_queue_commit(void)	// Flush any queued objects to the undo stack
{
	// Pushes the queue object onto the undo stack
	if (undo_q > 0) {
		// Deleted character undo events grow from the end
		undo_push(undo_queue + CONFIG_FEATURE_VI_UNDO_QUEUE_MAX - undo_q,
			undo_q,
			(undo_queue_state | UNDO_USE_SPOS)
		);
		undo_queue_state = UNDO_EMPTY;
		undo_q = 0;
	}
}
#endif

#endif /* ENABLE_FEATURE_VI_UNDO */

// open a hole in text[]
// might reallocate text[]! use p += text_hole_make(p, ...),
// and be careful to not use pointers into potentially freed text[]!
static uintptr_t text_hole_make(char *p, int size)	// at "p", make a 'size' byte hole
{
	uintptr_t bias = 0;

	if (size <= 0)
		return bias;
	end += size;		// adjust the new END
	if (end >= (text + text_size)) {
		char *new_text;
		text_size += end - (text + text_size) + 10240;
		new_text = xrealloc(text, text_size);
		bias = (new_text - text);
		screenbegin += bias;
		dot         += bias;
		end         += bias;
		p           += bias;
#if ENABLE_FEATURE_VI_YANKMARK
		{
			int i;
			for (i = 0; i < ARRAY_SIZE(mark); i++)
				if (mark[i])
					mark[i] += bias;
		}
#endif
		text = new_text;
	}
	memmove(p + size, p, end - size - p);
	memset(p, ' ', size);	// clear new hole
	return bias;
}

//  close a hole in text[]
//  "undo" value indicates if this operation should be undo-able
static char *text_hole_delete(char *p, char *q, int undo) // delete "p" through "q", inclusive
{
	char *src, *dest;
	int cnt, hole_size;

	// move forwards, from beginning
	// assume p <= q
	src = q + 1;
	dest = p;
	if (q < p) {		// they are backward- swap them
		src = p + 1;
		dest = q;
	}
	hole_size = q - p + 1;
	cnt = end - src;
#if ENABLE_FEATURE_VI_UNDO
	switch (undo) {
		case NO_UNDO:
			break;
		case ALLOW_UNDO:
			undo_push(p, hole_size, UNDO_DEL);
			break;
		case ALLOW_UNDO_CHAIN:
			undo_push(p, hole_size, UNDO_DEL_CHAIN);
			break;
# if ENABLE_FEATURE_VI_UNDO_QUEUE
		case ALLOW_UNDO_QUEUED:
			undo_push(p, hole_size, UNDO_DEL_QUEUED);
			break;
# endif
	}
	modified_count--;
#endif
	if (src < text || src > end)
		goto thd0;
	if (dest < text || dest >= end)
		goto thd0;
	modified_count++;
	if (src >= end)
		goto thd_atend;	// just delete the end of the buffer
	memmove(dest, src, cnt);
 thd_atend:
	end = end - hole_size;	// adjust the new END
	if (dest >= end)
		dest = end - 1;	// make sure dest in below end-1
	if (end <= text)
		dest = end = text;	// keep pointers valid
 thd0:
	return dest;
}

// copy text into register, then delete text.
// if dist <= 0, do not include, or go past, a NewLine
//
static char *yank_delete(char *start, char *stop, int dist, int yf, int undo)
{
	char *p;

	// make sure start <= stop
	if (start > stop) {
		// they are backwards, reverse them
		p = start;
		start = stop;
		stop = p;
	}
	if (dist <= 0) {
		// we cannot cross NL boundaries
		p = start;
		if (*p == '\n')
			return p;
		// dont go past a NewLine
		for (; p + 1 <= stop; p++) {
			if (p[1] == '\n') {
				stop = p;	// "stop" just before NewLine
				break;
			}
		}
	}
	p = start;
#if ENABLE_FEATURE_VI_YANKMARK
	text_yank(start, stop, YDreg);
#endif
	if (yf == YANKDEL) {
		p = text_hole_delete(start, stop, undo);
	}					// delete lines
	return p;
}

static void show_help(void)
{
	puts("These features are available:"
#if ENABLE_FEATURE_VI_SEARCH
	"\n\tPattern searches with / and ?"
#endif
#if ENABLE_FEATURE_VI_DOT_CMD
	"\n\tLast command repeat with ."
#endif
#if ENABLE_FEATURE_VI_YANKMARK
	"\n\tLine marking with 'x"
	"\n\tNamed buffers with \"x"
#endif
#if ENABLE_FEATURE_VI_READONLY
	//not implemented: "\n\tReadonly if vi is called as \"view\""
	//redundant: usage text says this too: "\n\tReadonly with -R command line arg"
#endif
#if ENABLE_FEATURE_VI_SET
	"\n\tSome colon mode commands with :"
#endif
#if ENABLE_FEATURE_VI_SETOPTS
	"\n\tSettable options with \":set\""
#endif
#if ENABLE_FEATURE_VI_USE_SIGNALS
	"\n\tSignal catching- ^C"
	"\n\tJob suspend and resume with ^Z"
#endif
#if ENABLE_FEATURE_VI_WIN_RESIZE
	"\n\tAdapt to window re-sizes"
#endif
	);
}

#if ENABLE_FEATURE_VI_DOT_CMD
static void start_new_cmd_q(char c)
{
	// get buffer for new cmd
	// if there is a current cmd count put it in the buffer first
	if (cmdcnt > 0) {
		lmc_len = sprintf(last_modifying_cmd, "%d%c", cmdcnt, c);
	} else { // just save char c onto queue
		last_modifying_cmd[0] = c;
		lmc_len = 1;
	}
	adding2q = 1;
}

static void end_cmd_q(void)
{
#if ENABLE_FEATURE_VI_YANKMARK
	YDreg = 26;			// go back to default Yank/Delete reg
#endif
	adding2q = 0;
}
#endif /* FEATURE_VI_DOT_CMD */

#if ENABLE_FEATURE_VI_YANKMARK \
 || (ENABLE_FEATURE_VI_COLON && ENABLE_FEATURE_VI_SEARCH) \
 || ENABLE_FEATURE_VI_CRASHME
// might reallocate text[]! use p += string_insert(p, ...),
// and be careful to not use pointers into potentially freed text[]!
static uintptr_t string_insert(char *p, const char *s, int undo) // insert the string at 'p'
{
	uintptr_t bias;
	int i;

	i = strlen(s);
#if ENABLE_FEATURE_VI_UNDO
	switch (undo) {
		case ALLOW_UNDO:
			undo_push(p, i, UNDO_INS);
			break;
		case ALLOW_UNDO_CHAIN:
			undo_push(p, i, UNDO_INS_CHAIN);
			break;
	}
#endif
	bias = text_hole_make(p, i);
	p += bias;
	memcpy(p, s, i);
#if ENABLE_FEATURE_VI_YANKMARK
	{
		int cnt;
		for (cnt = 0; *s != '\0'; s++) {
			if (*s == '\n')
				cnt++;
		}
		status_line("Put %d lines (%d chars) from [%c]", cnt, i, what_reg());
	}
#endif
	return bias;
}
#endif

#if ENABLE_FEATURE_VI_YANKMARK
static char *text_yank(char *p, char *q, int dest)	// copy text into a register
{
	int cnt = q - p;
	if (cnt < 0) {		// they are backwards- reverse them
		p = q;
		cnt = -cnt;
	}
	free(reg[dest]);	//  if already a yank register, free it
	reg[dest] = xstrndup(p, cnt + 1);
	return p;
}

static char what_reg(void)
{
	char c;

	c = 'D';			// default to D-reg
	if (0 <= YDreg && YDreg <= 25)
		c = 'a' + (char) YDreg;
	if (YDreg == 26)
		c = 'D';
	if (YDreg == 27)
		c = 'U';
	return c;
}

static void check_context(char cmd)
{
	// A context is defined to be "modifying text"
	// Any modifying command establishes a new context.

	if (dot < context_start || dot > context_end) {
		if (strchr(modifying_cmds, cmd) != NULL) {
			// we are trying to modify text[]- make this the current context
			mark[27] = mark[26];	// move cur to prev
			mark[26] = dot;	// move local to cur
			context_start = prev_line(prev_line(dot));
			context_end = next_line(next_line(dot));
			//loiter= start_loiter= now;
		}
	}
}

static char *swap_context(char *p) // goto new context for '' command make this the current context
{
	char *tmp;

	// the current context is in mark[26]
	// the previous context is in mark[27]
	// only swap context if other context is valid
	if (text <= mark[27] && mark[27] <= end - 1) {
		tmp = mark[27];
		mark[27] = p;
		mark[26] = p = tmp;
		context_start = prev_line(prev_line(prev_line(p)));
		context_end = next_line(next_line(next_line(p)));
	}
	return p;
}
#endif /* FEATURE_VI_YANKMARK */

//----- Set terminal attributes --------------------------------
static void rawmode(void)
{
	// no TERMIOS_CLEAR_ISIG: leave ISIG on - allow signals
	set_termios_to_raw(STDIN_FILENO, &term_orig, TERMIOS_RAW_CRNL);
	erase_char = term_orig.c_cc[VERASE];
}

static void cookmode(void)
{
	fflush_all();
	tcsetattr_stdin_TCSANOW(&term_orig);
}

#if ENABLE_FEATURE_VI_USE_SIGNALS
//----- Come here when we get a window resize signal ---------
static void winch_sig(int sig UNUSED_PARAM)
{
	int save_errno = errno;
	// FIXME: do it in main loop!!!
	signal(SIGWINCH, winch_sig);
	query_screen_dimensions();
	new_screen(rows, columns);	// get memory for virtual screen
	redraw(TRUE);		// re-draw the screen
	errno = save_errno;
}

//----- Come here when we get a continue signal -------------------
static void cont_sig(int sig UNUSED_PARAM)
{
	int save_errno = errno;
	rawmode(); // terminal to "raw"
	last_status_cksum = 0; // force status update
	redraw(TRUE); // re-draw the screen

	signal(SIGTSTP, suspend_sig);
	signal(SIGCONT, SIG_DFL);
	//kill(my_pid, SIGCONT); // huh? why? we are already "continued"...
	errno = save_errno;
}

//----- Come here when we get a Suspend signal -------------------
static void suspend_sig(int sig UNUSED_PARAM)
{
	int save_errno = errno;
	go_bottom_and_clear_to_eol();
	cookmode(); // terminal to "cooked"

	signal(SIGCONT, cont_sig);
	signal(SIGTSTP, SIG_DFL);
	kill(my_pid, SIGTSTP);
	errno = save_errno;
}

//----- Come here when we get a signal ---------------------------
static void catch_sig(int sig)
{
	signal(SIGINT, catch_sig);
	siglongjmp(restart, sig);
}
#endif /* FEATURE_VI_USE_SIGNALS */

static int mysleep(int hund)	// sleep for 'hund' 1/100 seconds or stdin ready
{
	struct pollfd pfd[1];

	if (hund != 0)
		fflush_all();

	pfd[0].fd = STDIN_FILENO;
	pfd[0].events = POLLIN;
	return safe_poll(pfd, 1, hund*10) > 0;
}

//----- IO Routines --------------------------------------------
static int readit(void) // read (maybe cursor) key from stdin
{
	int c;

	fflush_all();

	// Wait for input. TIMEOUT = -1 makes read_key wait even
	// on nonblocking stdin.
	// Note: read_key sets errno to 0 on success.
 again:
	c = read_key(STDIN_FILENO, readbuffer, /*timeout:*/ -1);
	if (c == -1) { // EOF/error
		if (errno == EAGAIN) // paranoia
			goto again;
		go_bottom_and_clear_to_eol();
		cookmode(); // terminal to "cooked"
		bb_error_msg_and_die("can't read user input");
	}
	return c;
}

//----- IO Routines --------------------------------------------
static int get_one_char(void)
{
	int c;

#if ENABLE_FEATURE_VI_DOT_CMD
	if (!adding2q) {
		// we are not adding to the q.
		// but, we may be reading from a q
		if (ioq == 0) {
			// there is no current q, read from STDIN
			c = readit();	// get the users input
		} else {
			// there is a queue to get chars from first
			// careful with correct sign expansion!
			c = (unsigned char)*ioq++;
			if (c == '\0') {
				// the end of the q, read from STDIN
				free(ioq_start);
				ioq_start = ioq = 0;
				c = readit();	// get the users input
			}
		}
	} else {
		// adding STDIN chars to q
		c = readit();	// get the users input
		if (lmc_len >= MAX_INPUT_LEN - 1) {
			status_line_bold("last_modifying_cmd overrun");
		} else {
			// add new char to q
			last_modifying_cmd[lmc_len++] = c;
		}
	}
#else
	c = readit();		// get the users input
#endif /* FEATURE_VI_DOT_CMD */
	return c;
}

// Get input line (uses "status line" area)
static char *get_input_line(const char *prompt)
{
	// char [MAX_INPUT_LEN]
#define buf get_input_line__buf

	int c;
	int i;

	strcpy(buf, prompt);
	last_status_cksum = 0;	// force status update
	go_bottom_and_clear_to_eol();
	write1(prompt);      // write out the :, /, or ? prompt

	i = strlen(buf);
	while (i < MAX_INPUT_LEN) {
		c = get_one_char();
		if (c == '\n' || c == '\r' || c == 27)
			break;		// this is end of input
		if (c == erase_char || c == 8 || c == 127) {
			// user wants to erase prev char
			buf[--i] = '\0';
			write1("\b \b"); // erase char on screen
			if (i <= 0) // user backs up before b-o-l, exit
				break;
		} else if (c > 0 && c < 256) { // exclude Unicode
			// (TODO: need to handle Unicode)
			buf[i] = c;
			buf[++i] = '\0';
			bb_putchar(c);
		}
	}
	refresh(FALSE);
	return buf;
#undef buf
}

// might reallocate text[]!
static int file_insert(const char *fn, char *p, int initial)
{
	int cnt = -1;
	int fd, size;
	struct stat statbuf;

	if (p < text)
		p = text;
	if (p > end)
		p = end;

	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		if (!initial)
			status_line_bold_errno(fn);
		return cnt;
	}

	/* Validate file */
	if (fstat(fd, &statbuf) < 0) {
		status_line_bold_errno(fn);
		goto fi;
	}
	if (!S_ISREG(statbuf.st_mode)) {
		status_line_bold("'%s' is not a regular file", fn);
		goto fi;
	}
	size = (statbuf.st_size < INT_MAX ? (int)statbuf.st_size : INT_MAX);
	p += text_hole_make(p, size);
	cnt = full_read(fd, p, size);
	if (cnt < 0) {
		status_line_bold_errno(fn);
		p = text_hole_delete(p, p + size - 1, NO_UNDO);	// un-do buffer insert
	} else if (cnt < size) {
		// There was a partial read, shrink unused space
		p = text_hole_delete(p + cnt, p + size - 1, NO_UNDO);
		status_line_bold("can't read '%s'", fn);
	}
 fi:
	close(fd);

#if ENABLE_FEATURE_VI_READONLY
	if (initial
	 && ((access(fn, W_OK) < 0) ||
		/* root will always have access()
		 * so we check fileperms too */
		!(statbuf.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH))
	    )
	) {
		SET_READONLY_FILE(readonly_mode);
	}
#endif
	return cnt;
}

static int file_write(char *fn, char *first, char *last)
{
	int fd, cnt, charcnt;

	if (fn == 0) {
		status_line_bold("No current filename");
		return -2;
	}
	/* By popular request we do not open file with O_TRUNC,
	 * but instead ftruncate() it _after_ successful write.
	 * Might reduce amount of data lost on power fail etc.
	 */
	fd = open(fn, (O_WRONLY | O_CREAT), 0666);
	if (fd < 0)
		return -1;
	cnt = last - first + 1;
	charcnt = full_write(fd, first, cnt);
	ftruncate(fd, charcnt);
	if (charcnt == cnt) {
		// good write
		//modified_count = FALSE;
	} else {
		charcnt = 0;
	}
	close(fd);
	return charcnt;
}

//----- Terminal Drawing ---------------------------------------
// The terminal is made up of 'rows' line of 'columns' columns.
// classically this would be 24 x 80.
//  screen coordinates
//  0,0     ...     0,79
//  1,0     ...     1,79
//  .       ...     .
//  .       ...     .
//  22,0    ...     22,79
//  23,0    ...     23,79   <- status line

//----- Move the cursor to row x col (count from 0, not 1) -------
static void place_cursor(int row, int col)
{
	char cm1[sizeof(ESC_SET_CURSOR_POS) + sizeof(int)*3 * 2];

	if (row < 0) row = 0;
	if (row >= rows) row = rows - 1;
	if (col < 0) col = 0;
	if (col >= columns) col = columns - 1;

	sprintf(cm1, ESC_SET_CURSOR_POS, row + 1, col + 1);
	write1(cm1);
}

//----- Erase from cursor to end of line -----------------------
static void clear_to_eol(void)
{
	write1(ESC_CLEAR2EOL);
}

static void go_bottom_and_clear_to_eol(void)
{
	place_cursor(rows - 1, 0);
	clear_to_eol();
}

//----- Erase from cursor to end of screen -----------------------
static void clear_to_eos(void)
{
	write1(ESC_CLEAR2EOS);
}

//----- Start standout mode ------------------------------------
static void standout_start(void)
{
	write1(ESC_BOLD_TEXT);
}

//----- End standout mode --------------------------------------
static void standout_end(void)
{
	write1(ESC_NORM_TEXT);
}

//----- Flash the screen  --------------------------------------
static void flash(int h)
{
	standout_start();
	redraw(TRUE);
	mysleep(h);
	standout_end();
	redraw(TRUE);
}

static void indicate_error(void)
{
#if ENABLE_FEATURE_VI_CRASHME
	if (crashme > 0)
		return;			// generate a random command
#endif
	if (!err_method) {
		write1(ESC_BELL);
	} else {
		flash(10);
	}
}

//----- Screen[] Routines --------------------------------------
//----- Erase the Screen[] memory ------------------------------
static void screen_erase(void)
{
	memset(screen, ' ', screensize);	// clear new screen
}

static int bufsum(char *buf, int count)
{
	int sum = 0;
	char *e = buf + count;

	while (buf < e)
		sum += (unsigned char) *buf++;
	return sum;
}

//----- Draw the status line at bottom of the screen -------------
static void show_status_line(void)
{
	int cnt = 0, cksum = 0;

	// either we already have an error or status message, or we
	// create one.
	if (!have_status_msg) {
		cnt = format_edit_status();
		cksum = bufsum(status_buffer, cnt);
	}
	if (have_status_msg || ((cnt > 0 && last_status_cksum != cksum))) {
		last_status_cksum = cksum;		// remember if we have seen this line
		go_bottom_and_clear_to_eol();
		write1(status_buffer);
		if (have_status_msg) {
			if (((int)strlen(status_buffer) - (have_status_msg - 1)) >
					(columns - 1) ) {
				have_status_msg = 0;
				Hit_Return();
			}
			have_status_msg = 0;
		}
		place_cursor(crow, ccol);  // put cursor back in correct place
	}
	fflush_all();
}

//----- format the status buffer, the bottom line of screen ------
// format status buffer, with STANDOUT mode
static void status_line_bold(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	strcpy(status_buffer, ESC_BOLD_TEXT);
	vsprintf(status_buffer + sizeof(ESC_BOLD_TEXT)-1, format, args);
	strcat(status_buffer, ESC_NORM_TEXT);
	va_end(args);

	have_status_msg = 1 + sizeof(ESC_BOLD_TEXT) + sizeof(ESC_NORM_TEXT) - 2;
}

static void status_line_bold_errno(const char *fn)
{
	status_line_bold("'%s' "STRERROR_FMT, fn STRERROR_ERRNO);
}

// format status buffer
static void status_line(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vsprintf(status_buffer, format, args);
	va_end(args);

	have_status_msg = 1;
}

// copy s to buf, convert unprintable
static void print_literal(char *buf, const char *s)
{
	char *d;
	unsigned char c;

	buf[0] = '\0';
	if (!s[0])
		s = "(NULL)";

	d = buf;
	for (; *s; s++) {
		int c_is_no_print;

		c = *s;
		c_is_no_print = (c & 0x80) && !Isprint(c);
		if (c_is_no_print) {
			strcpy(d, ESC_NORM_TEXT);
			d += sizeof(ESC_NORM_TEXT)-1;
			c = '.';
		}
		if (c < ' ' || c == 0x7f) {
			*d++ = '^';
			c |= '@'; /* 0x40 */
			if (c == 0x7f)
				c = '?';
		}
		*d++ = c;
		*d = '\0';
		if (c_is_no_print) {
			strcpy(d, ESC_BOLD_TEXT);
			d += sizeof(ESC_BOLD_TEXT)-1;
		}
		if (*s == '\n') {
			*d++ = '$';
			*d = '\0';
		}
		if (d - buf > MAX_INPUT_LEN - 10) // paranoia
			break;
	}
}

static void not_implemented(const char *s)
{
	char buf[MAX_INPUT_LEN];

	print_literal(buf, s);
	status_line_bold("\'%s\' is not implemented", buf);
}

// show file status on status line
static int format_edit_status(void)
{
	static const char cmd_mode_indicator[] ALIGN1 = "-IR-";

#define tot format_edit_status__tot

	int cur, percent, ret, trunc_at;

	// modified_count is now a counter rather than a flag.  this
	// helps reduce the amount of line counting we need to do.
	// (this will cause a mis-reporting of modified status
	// once every MAXINT editing operations.)

	// it would be nice to do a similar optimization here -- if
	// we haven't done a motion that could have changed which line
	// we're on, then we shouldn't have to do this count_lines()
	cur = count_lines(text, dot);

	// count_lines() is expensive.
	// Call it only if something was changed since last time
	// we were here:
	if (modified_count != last_modified_count) {
		tot = cur + count_lines(dot, end - 1) - 1;
		last_modified_count = modified_count;
	}

	//    current line         percent
	//   -------------    ~~ ----------
	//    total lines            100
	if (tot > 0) {
		percent = (100 * cur) / tot;
	} else {
		cur = tot = 0;
		percent = 100;
	}

	trunc_at = columns < STATUS_BUFFER_LEN-1 ?
		columns : STATUS_BUFFER_LEN-1;

	ret = snprintf(status_buffer, trunc_at+1,
#if ENABLE_FEATURE_VI_READONLY
		"%c %s%s%s %d/%d %d%%",
#else
		"%c %s%s %d/%d %d%%",
#endif
		cmd_mode_indicator[cmd_mode & 3],
		(current_filename != NULL ? current_filename : "No file"),
#if ENABLE_FEATURE_VI_READONLY
		(readonly_mode ? " [Readonly]" : ""),
#endif
		(modified_count ? " [Modified]" : ""),
		cur, tot, percent);

	if (ret >= 0 && ret < trunc_at)
		return ret;  /* it all fit */

	return trunc_at;  /* had to truncate */
#undef tot
}

//----- Force refresh of all Lines -----------------------------
static void redraw(int full_screen)
{
	place_cursor(0, 0);
	clear_to_eos();
	screen_erase();		// erase the internal screen buffer
	last_status_cksum = 0;	// force status update
	refresh(full_screen);	// this will redraw the entire display
	show_status_line();
}

//----- Format a text[] line into a buffer ---------------------
static char* format_line(char *src /*, int li*/)
{
	unsigned char c;
	int co;
	int ofs = offset;
	char *dest = scr_out_buf; // [MAX_SCR_COLS + MAX_TABSTOP * 2]

	c = '~'; // char in col 0 in non-existent lines is '~'
	co = 0;
	while (co < columns + tabstop) {
		// have we gone past the end?
		if (src < end) {
			c = *src++;
			if (c == '\n')
				break;
			if ((c & 0x80) && !Isprint(c)) {
				c = '.';
			}
			if (c < ' ' || c == 0x7f) {
				if (c == '\t') {
					c = ' ';
					//      co %    8     !=     7
					while ((co % tabstop) != (tabstop - 1)) {
						dest[co++] = c;
					}
				} else {
					dest[co++] = '^';
					if (c == 0x7f)
						c = '?';
					else
						c += '@'; // Ctrl-X -> 'X'
				}
			}
		}
		dest[co++] = c;
		// discard scrolled-off-to-the-left portion,
		// in tabstop-sized pieces
		if (ofs >= tabstop && co >= tabstop) {
			memmove(dest, dest + tabstop, co);
			co -= tabstop;
			ofs -= tabstop;
		}
		if (src >= end)
			break;
	}
	// check "short line, gigantic offset" case
	if (co < ofs)
		ofs = co;
	// discard last scrolled off part
	co -= ofs;
	dest += ofs;
	// fill the rest with spaces
	if (co < columns)
		memset(&dest[co], ' ', columns - co);
	return dest;
}

//----- Refresh the changed screen lines -----------------------
// Copy the source line from text[] into the buffer and note
// if the current screenline is different from the new buffer.
// If they differ then that line needs redrawing on the terminal.
//
static void refresh(int full_screen)
{
#define old_offset refresh__old_offset

	int li, changed;
	char *tp, *sp;		// pointer into text[] and screen[]

	if (ENABLE_FEATURE_VI_WIN_RESIZE IF_FEATURE_VI_ASK_TERMINAL(&& !G.get_rowcol_error) ) {
		unsigned c = columns, r = rows;
		query_screen_dimensions();
		full_screen |= (c - columns) | (r - rows);
	}
	sync_cursor(dot, &crow, &ccol);	// where cursor will be (on "dot")
	tp = screenbegin;	// index into text[] of top line

	// compare text[] to screen[] and mark screen[] lines that need updating
	for (li = 0; li < rows - 1; li++) {
		int cs, ce;				// column start & end
		char *out_buf;
		// format current text line
		out_buf = format_line(tp /*, li*/);

		// skip to the end of the current text[] line
		if (tp < end) {
			char *t = memchr(tp, '\n', end - tp);
			if (!t) t = end - 1;
			tp = t + 1;
		}

		// see if there are any changes between virtual screen and out_buf
		changed = FALSE;	// assume no change
		cs = 0;
		ce = columns - 1;
		sp = &screen[li * columns];	// start of screen line
		if (full_screen) {
			// force re-draw of every single column from 0 - columns-1
			goto re0;
		}
		// compare newly formatted buffer with virtual screen
		// look forward for first difference between buf and screen
		for (; cs <= ce; cs++) {
			if (out_buf[cs] != sp[cs]) {
				changed = TRUE;	// mark for redraw
				break;
			}
		}

		// look backward for last difference between out_buf and screen
		for (; ce >= cs; ce--) {
			if (out_buf[ce] != sp[ce]) {
				changed = TRUE;	// mark for redraw
				break;
			}
		}
		// now, cs is index of first diff, and ce is index of last diff

		// if horz offset has changed, force a redraw
		if (offset != old_offset) {
 re0:
			changed = TRUE;
		}

		// make a sanity check of columns indexes
		if (cs < 0) cs = 0;
		if (ce > columns - 1) ce = columns - 1;
		if (cs > ce) { cs = 0; ce = columns - 1; }
		// is there a change between virtual screen and out_buf
		if (changed) {
			// copy changed part of buffer to virtual screen
			memcpy(sp+cs, out_buf+cs, ce-cs+1);
			place_cursor(li, cs);
			// write line out to terminal
			fwrite(&sp[cs], ce - cs + 1, 1, stdout);
		}
	}

	place_cursor(crow, ccol);

	old_offset = offset;
#undef old_offset
}

//---------------------------------------------------------------------
//----- the Ascii Chart -----------------------------------------------
//
//  00 nul   01 soh   02 stx   03 etx   04 eot   05 enq   06 ack   07 bel
//  08 bs    09 ht    0a nl    0b vt    0c np    0d cr    0e so    0f si
//  10 dle   11 dc1   12 dc2   13 dc3   14 dc4   15 nak   16 syn   17 etb
//  18 can   19 em    1a sub   1b esc   1c fs    1d gs    1e rs    1f us
//  20 sp    21 !     22 "     23 #     24 $     25 %     26 &     27 '
//  28 (     29 )     2a *     2b +     2c ,     2d -     2e .     2f /
//  30 0     31 1     32 2     33 3     34 4     35 5     36 6     37 7
//  38 8     39 9     3a :     3b ;     3c <     3d =     3e >     3f ?
//  40 @     41 A     42 B     43 C     44 D     45 E     46 F     47 G
//  48 H     49 I     4a J     4b K     4c L     4d M     4e N     4f O
//  50 P     51 Q     52 R     53 S     54 T     55 U     56 V     57 W
//  58 X     59 Y     5a Z     5b [     5c \     5d ]     5e ^     5f _
//  60 `     61 a     62 b     63 c     64 d     65 e     66 f     67 g
//  68 h     69 i     6a j     6b k     6c l     6d m     6e n     6f o
//  70 p     71 q     72 r     73 s     74 t     75 u     76 v     77 w
//  78 x     79 y     7a z     7b {     7c |     7d }     7e ~     7f del
//---------------------------------------------------------------------

//----- Execute a Vi Command -----------------------------------
static void do_cmd(int c)
{
	char *p, *q, *save_dot;
	char buf[12];
	int dir;
	int cnt, i, j;
	int c1;

//	c1 = c; // quiet the compiler
//	cnt = yf = 0; // quiet the compiler
//	p = q = save_dot = buf; // quiet the compiler
	memset(buf, '\0', sizeof(buf));

	show_status_line();

	/* if this is a cursor key, skip these checks */
	switch (c) {
		case KEYCODE_UP:
		case KEYCODE_DOWN:
		case KEYCODE_LEFT:
		case KEYCODE_RIGHT:
		case KEYCODE_HOME:
		case KEYCODE_END:
		case KEYCODE_PAGEUP:
		case KEYCODE_PAGEDOWN:
		case KEYCODE_DELETE:
			goto key_cmd_mode;
	}

	if (cmd_mode == 2) {
		//  flip-flop Insert/Replace mode
		if (c == KEYCODE_INSERT)
			goto dc_i;
		// we are 'R'eplacing the current *dot with new char
		if (*dot == '\n') {
			// don't Replace past E-o-l
			cmd_mode = 1;	// convert to insert
			undo_queue_commit();
		} else {
			if (1 <= c || Isprint(c)) {
				if (c != 27)
					dot = yank_delete(dot, dot, 0, YANKDEL, ALLOW_UNDO);	// delete char
				dot = char_insert(dot, c, ALLOW_UNDO_CHAIN);	// insert new char
			}
			goto dc1;
		}
	}
	if (cmd_mode == 1) {
		//  hitting "Insert" twice means "R" replace mode
		if (c == KEYCODE_INSERT) goto dc5;
		// insert the char c at "dot"
		if (1 <= c || Isprint(c)) {
			dot = char_insert(dot, c, ALLOW_UNDO_QUEUED);
		}
		goto dc1;
	}

 key_cmd_mode:
	switch (c) {
		//case 0x01:	// soh
		//case 0x09:	// ht
		//case 0x0b:	// vt
		//case 0x0e:	// so
		//case 0x0f:	// si
		//case 0x10:	// dle
		//case 0x11:	// dc1
		//case 0x13:	// dc3
#if ENABLE_FEATURE_VI_CRASHME
	case 0x14:			// dc4  ctrl-T
		crashme = (crashme == 0) ? 1 : 0;
		break;
#endif
		//case 0x16:	// syn
		//case 0x17:	// etb
		//case 0x18:	// can
		//case 0x1c:	// fs
		//case 0x1d:	// gs
		//case 0x1e:	// rs
		//case 0x1f:	// us
		//case '!':	// !-
		//case '#':	// #-
		//case '&':	// &-
		//case '(':	// (-
		//case ')':	// )-
		//case '*':	// *-
		//case '=':	// =-
		//case '@':	// @-
		//case 'F':	// F-
		//case 'K':	// K-
		//case 'Q':	// Q-
		//case 'S':	// S-
		//case 'T':	// T-
		//case 'V':	// V-
		//case '[':	// [-
		//case '\\':	// \-
		//case ']':	// ]-
		//case '_':	// _-
		//case '`':	// `-
		//case 'v':	// v-
	default:			// unrecognized command
		buf[0] = c;
		buf[1] = '\0';
		not_implemented(buf);
		end_cmd_q();	// stop adding to q
	case 0x00:			// nul- ignore
		break;
	case 2:			// ctrl-B  scroll up   full screen
	case KEYCODE_PAGEUP:	// Cursor Key Page Up
		dot_scroll(rows - 2, -1);
		break;
	case 4:			// ctrl-D  scroll down half screen
		dot_scroll((rows - 2) / 2, 1);
		break;
	case 5:			// ctrl-E  scroll down one line
		dot_scroll(1, 1);
		break;
	case 6:			// ctrl-F  scroll down full screen
	case KEYCODE_PAGEDOWN:	// Cursor Key Page Down
		dot_scroll(rows - 2, 1);
		break;
	case 7:			// ctrl-G  show current status
		last_status_cksum = 0;	// force status update
		break;
	case 'h':			// h- move left
	case KEYCODE_LEFT:	// cursor key Left
	case 8:		// ctrl-H- move left    (This may be ERASE char)
	case 0x7f:	// DEL- move left   (This may be ERASE char)
		do {
			dot_left();
		} while (--cmdcnt > 0);
		break;
	case 10:			// Newline ^J
	case 'j':			// j- goto next line, same col
	case KEYCODE_DOWN:	// cursor key Down
		do {
			dot_next();		// go to next B-o-l
			// try stay in same col
			dot = move_to_col(dot, ccol + offset);
		} while (--cmdcnt > 0);
		break;
	case 12:			// ctrl-L  force redraw whole screen
	case 18:			// ctrl-R  force redraw
		place_cursor(0, 0);
		clear_to_eos();
		//mysleep(10); // why???
		screen_erase();	// erase the internal screen buffer
		last_status_cksum = 0;	// force status update
		refresh(TRUE);	// this will redraw the entire display
		break;
	case 13:			// Carriage Return ^M
	case '+':			// +- goto next line
		do {
			dot_next();
			dot_skip_over_ws();
		} while (--cmdcnt > 0);
		break;
	case 21:			// ctrl-U  scroll up   half screen
		dot_scroll((rows - 2) / 2, -1);
		break;
	case 25:			// ctrl-Y  scroll up one line
		dot_scroll(1, -1);
		break;
	case 27:			// esc
		if (cmd_mode == 0)
			indicate_error();
		cmd_mode = 0;	// stop insrting
		undo_queue_commit();
		end_cmd_q();
		last_status_cksum = 0;	// force status update
		break;
	case ' ':			// move right
	case 'l':			// move right
	case KEYCODE_RIGHT:	// Cursor Key Right
		do {
			dot_right();
		} while (--cmdcnt > 0);
		break;
#if ENABLE_FEATURE_VI_YANKMARK
	case '"':			// "- name a register to use for Delete/Yank
		c1 = (get_one_char() | 0x20) - 'a'; // | 0x20 is tolower()
		if ((unsigned)c1 <= 25) { // a-z?
			YDreg = c1;
		} else {
			indicate_error();
		}
		break;
	case '\'':			// '- goto a specific mark
		c1 = (get_one_char() | 0x20);
		if ((unsigned)(c1 - 'a') <= 25) { // a-z?
			c1 = (c1 - 'a');
			// get the b-o-l
			q = mark[c1];
			if (text <= q && q < end) {
				dot = q;
				dot_begin();	// go to B-o-l
				dot_skip_over_ws();
			}
		} else if (c1 == '\'') {	// goto previous context
			dot = swap_context(dot);	// swap current and previous context
			dot_begin();	// go to B-o-l
			dot_skip_over_ws();
		} else {
			indicate_error();
		}
		break;
	case 'm':			// m- Mark a line
		// this is really stupid.  If there are any inserts or deletes
		// between text[0] and dot then this mark will not point to the
		// correct location! It could be off by many lines!
		// Well..., at least its quick and dirty.
		c1 = (get_one_char() | 0x20) - 'a';
		if ((unsigned)c1 <= 25) { // a-z?
			// remember the line
			mark[c1] = dot;
		} else {
			indicate_error();
		}
		break;
	case 'P':			// P- Put register before
	case 'p':			// p- put register after
		p = reg[YDreg];
		if (p == NULL) {
			status_line_bold("Nothing in register %c", what_reg());
			break;
		}
		// are we putting whole lines or strings
		if (strchr(p, '\n') != NULL) {
			if (c == 'P') {
				dot_begin();	// putting lines- Put above
			}
			if (c == 'p') {
				// are we putting after very last line?
				if (end_line(dot) == (end - 1)) {
					dot = end;	// force dot to end of text[]
				} else {
					dot_next();	// next line, then put before
				}
			}
		} else {
			if (c == 'p')
				dot_right();	// move to right, can move to NL
		}
		string_insert(dot, p, ALLOW_UNDO);	// insert the string
		end_cmd_q();	// stop adding to q
		break;
	case 'U':			// U- Undo; replace current line with original version
		if (reg[Ureg] != NULL) {
			p = begin_line(dot);
			q = end_line(dot);
			p = text_hole_delete(p, q, ALLOW_UNDO);	// delete cur line
			p += string_insert(p, reg[Ureg], ALLOW_UNDO_CHAIN);	// insert orig line
			dot = p;
			dot_skip_over_ws();
		}
		break;
#endif /* FEATURE_VI_YANKMARK */
#if ENABLE_FEATURE_VI_UNDO
	case 'u':	// u- undo last operation
		undo_pop();
		break;
#endif
	case '$':			// $- goto end of line
	case KEYCODE_END:		// Cursor Key End
		for (;;) {
			dot = end_line(dot);
			if (--cmdcnt <= 0)
				break;
			dot_next();
		}
		break;
	case '%':			// %- find matching char of pair () [] {}
		for (q = dot; q < end && *q != '\n'; q++) {
			if (strchr("()[]{}", *q) != NULL) {
				// we found half of a pair
				p = find_pair(q, *q);
				if (p == NULL) {
					indicate_error();
				} else {
					dot = p;
				}
				break;
			}
		}
		if (*q == '\n')
			indicate_error();
		break;
	case 'f':			// f- forward to a user specified char
		last_forward_char = get_one_char();	// get the search char
		//
		// dont separate these two commands. 'f' depends on ';'
		//
		//**** fall through to ... ';'
	case ';':			// ;- look at rest of line for last forward char
		do {
			if (last_forward_char == 0)
				break;
			q = dot + 1;
			while (q < end - 1 && *q != '\n' && *q != last_forward_char) {
				q++;
			}
			if (*q == last_forward_char)
				dot = q;
		} while (--cmdcnt > 0);
		break;
	case ',':           // repeat latest 'f' in opposite direction
		if (last_forward_char == 0)
			break;
		do {
			q = dot - 1;
			while (q >= text && *q != '\n' && *q != last_forward_char) {
				q--;
			}
			if (q >= text && *q == last_forward_char)
				dot = q;
		} while (--cmdcnt > 0);
		break;

	case '-':			// -- goto prev line
		do {
			dot_prev();
			dot_skip_over_ws();
		} while (--cmdcnt > 0);
		break;
#if ENABLE_FEATURE_VI_DOT_CMD
	case '.':			// .- repeat the last modifying command
		// Stuff the last_modifying_cmd back into stdin
		// and let it be re-executed.
		if (lmc_len > 0) {
			last_modifying_cmd[lmc_len] = 0;
			ioq = ioq_start = xstrdup(last_modifying_cmd);
		}
		break;
#endif
#if ENABLE_FEATURE_VI_SEARCH
	case '?':			// /- search for a pattern
	case '/':			// /- search for a pattern
		buf[0] = c;
		buf[1] = '\0';
		q = get_input_line(buf);	// get input line- use "status line"
		if (q[0] && !q[1]) {
			if (last_search_pattern[0])
				last_search_pattern[0] = c;
			goto dc3; // if no pat re-use old pat
		}
		if (q[0]) {       // strlen(q) > 1: new pat- save it and find
			// there is a new pat
			free(last_search_pattern);
			last_search_pattern = xstrdup(q);
			goto dc3;	// now find the pattern
		}
		// user changed mind and erased the "/"-  do nothing
		break;
	case 'N':			// N- backward search for last pattern
		dir = BACK;		// assume BACKWARD search
		p = dot - 1;
		if (last_search_pattern[0] == '?') {
			dir = FORWARD;
			p = dot + 1;
		}
		goto dc4;		// now search for pattern
		break;
	case 'n':			// n- repeat search for last pattern
		// search rest of text[] starting at next char
		// if search fails return orignal "p" not the "p+1" address
		do {
			const char *msg;
 dc3:
			dir = FORWARD;	// assume FORWARD search
			p = dot + 1;
			if (last_search_pattern[0] == '?') {
				dir = BACK;
				p = dot - 1;
			}
 dc4:
			q = char_search(p, last_search_pattern + 1, dir, FULL);
			if (q != NULL) {
				dot = q;	// good search, update "dot"
				msg = NULL;
				goto dc2;
			}
			// no pattern found between "dot" and "end"- continue at top
			p = text;
			if (dir == BACK) {
				p = end - 1;
			}
			q = char_search(p, last_search_pattern + 1, dir, FULL);
			if (q != NULL) {	// found something
				dot = q;	// found new pattern- goto it
				msg = "search hit BOTTOM, continuing at TOP";
				if (dir == BACK) {
					msg = "search hit TOP, continuing at BOTTOM";
				}
			} else {
				msg = "Pattern not found";
			}
 dc2:
			if (msg)
				status_line_bold("%s", msg);
		} while (--cmdcnt > 0);
		break;
	case '{':			// {- move backward paragraph
		q = char_search(dot, "\n\n", BACK, FULL);
		if (q != NULL) {	// found blank line
			dot = next_line(q);	// move to next blank line
		}
		break;
	case '}':			// }- move forward paragraph
		q = char_search(dot, "\n\n", FORWARD, FULL);
		if (q != NULL) {	// found blank line
			dot = next_line(q);	// move to next blank line
		}
		break;
#endif /* FEATURE_VI_SEARCH */
	case '0':			// 0- goto beginning of line
	case '1':			// 1-
	case '2':			// 2-
	case '3':			// 3-
	case '4':			// 4-
	case '5':			// 5-
	case '6':			// 6-
	case '7':			// 7-
	case '8':			// 8-
	case '9':			// 9-
		if (c == '0' && cmdcnt < 1) {
			dot_begin();	// this was a standalone zero
		} else {
			cmdcnt = cmdcnt * 10 + (c - '0');	// this 0 is part of a number
		}
		break;
	case ':':			// :- the colon mode commands
		p = get_input_line(":");	// get input line- use "status line"
		colon(p);		// execute the command
		break;
	case '<':			// <- Left  shift something
	case '>':			// >- Right shift something
		cnt = count_lines(text, dot);	// remember what line we are on
		c1 = get_one_char();	// get the type of thing to delete
		find_range(&p, &q, c1);
		yank_delete(p, q, 1, YANKONLY, NO_UNDO);	// save copy before change
		p = begin_line(p);
		q = end_line(q);
		i = count_lines(p, q);	// # of lines we are shifting
		for ( ; i > 0; i--, p = next_line(p)) {
			if (c == '<') {
				// shift left- remove tab or 8 spaces
				if (*p == '\t') {
					// shrink buffer 1 char
					text_hole_delete(p, p, NO_UNDO);
				} else if (*p == ' ') {
					// we should be calculating columns, not just SPACE
					for (j = 0; *p == ' ' && j < tabstop; j++) {
						text_hole_delete(p, p, NO_UNDO);
					}
				}
			} else if (c == '>') {
				// shift right -- add tab or 8 spaces
				char_insert(p, '\t', ALLOW_UNDO);
			}
		}
		dot = find_line(cnt);	// what line were we on
		dot_skip_over_ws();
		end_cmd_q();	// stop adding to q
		break;
	case 'A':			// A- append at e-o-l
		dot_end();		// go to e-o-l
		//**** fall through to ... 'a'
	case 'a':			// a- append after current char
		if (*dot != '\n')
			dot++;
		goto dc_i;
		break;
	case 'B':			// B- back a blank-delimited Word
	case 'E':			// E- end of a blank-delimited word
	case 'W':			// W- forward a blank-delimited word
		dir = FORWARD;
		if (c == 'B')
			dir = BACK;
		do {
			if (c == 'W' || isspace(dot[dir])) {
				dot = skip_thing(dot, 1, dir, S_TO_WS);
				dot = skip_thing(dot, 2, dir, S_OVER_WS);
			}
			if (c != 'W')
				dot = skip_thing(dot, 1, dir, S_BEFORE_WS);
		} while (--cmdcnt > 0);
		break;
	case 'C':			// C- Change to e-o-l
	case 'D':			// D- delete to e-o-l
		save_dot = dot;
		dot = dollar_line(dot);	// move to before NL
		// copy text into a register and delete
		dot = yank_delete(save_dot, dot, 0, YANKDEL, ALLOW_UNDO);	// delete to e-o-l
		if (c == 'C')
			goto dc_i;	// start inserting
#if ENABLE_FEATURE_VI_DOT_CMD
		if (c == 'D')
			end_cmd_q();	// stop adding to q
#endif
		break;
	case 'g': // 'gg' goto a line number (vim) (default: very first line)
		c1 = get_one_char();
		if (c1 != 'g') {
			buf[0] = 'g';
			// c1 < 0 if the key was special. Try "g<up-arrow>"
			// TODO: if Unicode?
			buf[1] = (c1 >= 0 ? c1 : '*');
			buf[2] = '\0';
			not_implemented(buf);
			break;
		}
		if (cmdcnt == 0)
			cmdcnt = 1;
		/* fall through */
	case 'G':		// G- goto to a line number (default= E-O-F)
		dot = end - 1;				// assume E-O-F
		if (cmdcnt > 0) {
			dot = find_line(cmdcnt);	// what line is #cmdcnt
		}
		dot_skip_over_ws();
		break;
	case 'H':			// H- goto top line on screen
		dot = screenbegin;
		if (cmdcnt > (rows - 1)) {
			cmdcnt = (rows - 1);
		}
		if (--cmdcnt > 0) {
			do_cmd('+');
		}
		dot_skip_over_ws();
		break;
	case 'I':			// I- insert before first non-blank
		dot_begin();	// 0
		dot_skip_over_ws();
		//**** fall through to ... 'i'
	case 'i':			// i- insert before current char
	case KEYCODE_INSERT:	// Cursor Key Insert
 dc_i:
		cmd_mode = 1;	// start inserting
		undo_queue_commit();	// commit queue when cmd_mode changes
		break;
	case 'J':			// J- join current and next lines together
		do {
			dot_end();		// move to NL
			if (dot < end - 1) {	// make sure not last char in text[]
#if ENABLE_FEATURE_VI_UNDO
				undo_push(dot, 1, UNDO_DEL);
				*dot++ = ' ';	// replace NL with space
				undo_push((dot - 1), 1, UNDO_INS_CHAIN);
#else
				*dot++ = ' ';
				modified_count++;
#endif
				while (isblank(*dot)) {	// delete leading WS
					text_hole_delete(dot, dot, ALLOW_UNDO_CHAIN);
				}
			}
		} while (--cmdcnt > 0);
		end_cmd_q();	// stop adding to q
		break;
	case 'L':			// L- goto bottom line on screen
		dot = end_screen();
		if (cmdcnt > (rows - 1)) {
			cmdcnt = (rows - 1);
		}
		if (--cmdcnt > 0) {
			do_cmd('-');
		}
		dot_begin();
		dot_skip_over_ws();
		break;
	case 'M':			// M- goto middle line on screen
		dot = screenbegin;
		for (cnt = 0; cnt < (rows-1) / 2; cnt++)
			dot = next_line(dot);
		break;
	case 'O':			// O- open a empty line above
		//    0i\n ESC -i
		p = begin_line(dot);
		if (p[-1] == '\n') {
			dot_prev();
	case 'o':			// o- open a empty line below; Yes, I know it is in the middle of the "if (..."
			dot_end();
			dot = char_insert(dot, '\n', ALLOW_UNDO);
		} else {
			dot_begin();	// 0
			dot = char_insert(dot, '\n', ALLOW_UNDO);	// i\n ESC
			dot_prev();	// -
		}
		goto dc_i;
		break;
	case 'R':			// R- continuous Replace char
 dc5:
		cmd_mode = 2;
		undo_queue_commit();
		break;
	case KEYCODE_DELETE:
		if (dot < end - 1)
			dot = yank_delete(dot, dot, 1, YANKDEL, ALLOW_UNDO);
		break;
	case 'X':			// X- delete char before dot
	case 'x':			// x- delete the current char
	case 's':			// s- substitute the current char
		dir = 0;
		if (c == 'X')
			dir = -1;
		do {
			if (dot[dir] != '\n') {
				if (c == 'X')
					dot--;	// delete prev char
				dot = yank_delete(dot, dot, 0, YANKDEL, ALLOW_UNDO);	// delete char
			}
		} while (--cmdcnt > 0);
		end_cmd_q();	// stop adding to q
		if (c == 's')
			goto dc_i;	// start inserting
		break;
	case 'Z':			// Z- if modified, {write}; exit
		// ZZ means to save file (if necessary), then exit
		c1 = get_one_char();
		if (c1 != 'Z') {
			indicate_error();
			break;
		}
		if (modified_count) {
			if (ENABLE_FEATURE_VI_READONLY && readonly_mode) {
				status_line_bold("'%s' is read only", current_filename);
				break;
			}
			cnt = file_write(current_filename, text, end - 1);
			if (cnt < 0) {
				if (cnt == -1)
					status_line_bold("Write error: "STRERROR_FMT STRERROR_ERRNO);
			} else if (cnt == (end - 1 - text + 1)) {
				editing = 0;
			}
		} else {
			editing = 0;
		}
		break;
	case '^':			// ^- move to first non-blank on line
		dot_begin();
		dot_skip_over_ws();
		break;
	case 'b':			// b- back a word
	case 'e':			// e- end of word
		dir = FORWARD;
		if (c == 'b')
			dir = BACK;
		do {
			if ((dot + dir) < text || (dot + dir) > end - 1)
				break;
			dot += dir;
			if (isspace(*dot)) {
				dot = skip_thing(dot, (c == 'e') ? 2 : 1, dir, S_OVER_WS);
			}
			if (isalnum(*dot) || *dot == '_') {
				dot = skip_thing(dot, 1, dir, S_END_ALNUM);
			} else if (ispunct(*dot)) {
				dot = skip_thing(dot, 1, dir, S_END_PUNCT);
			}
		} while (--cmdcnt > 0);
		break;
	case 'c':			// c- change something
	case 'd':			// d- delete something
#if ENABLE_FEATURE_VI_YANKMARK
	case 'y':			// y- yank   something
	case 'Y':			// Y- Yank a line
#endif
	{
		int yf, ml, whole = 0;
		yf = YANKDEL;	// assume either "c" or "d"
#if ENABLE_FEATURE_VI_YANKMARK
		if (c == 'y' || c == 'Y')
			yf = YANKONLY;
#endif
		c1 = 'y';
		if (c != 'Y')
			c1 = get_one_char();	// get the type of thing to delete
		// determine range, and whether it spans lines
		ml = find_range(&p, &q, c1);
		place_cursor(0, 0);
		if (c1 == 27) {	// ESC- user changed mind and wants out
			c = c1 = 27;	// Escape- do nothing
		} else if (strchr("wW", c1)) {
			if (c == 'c') {
				// don't include trailing WS as part of word
				while (isblank(*q)) {
					if (q <= text || q[-1] == '\n')
						break;
					q--;
				}
			}
			dot = yank_delete(p, q, ml, yf, ALLOW_UNDO);	// delete word
		} else if (strchr("^0bBeEft%$ lh\b\177", c1)) {
			// partial line copy text into a register and delete
			dot = yank_delete(p, q, ml, yf, ALLOW_UNDO);	// delete word
		} else if (strchr("cdykjHL+-{}\r\n", c1)) {
			// whole line copy text into a register and delete
			dot = yank_delete(p, q, ml, yf, ALLOW_UNDO);	// delete lines
			whole = 1;
		} else {
			// could not recognize object
			c = c1 = 27;	// error-
			ml = 0;
			indicate_error();
		}
		if (ml && whole) {
			if (c == 'c') {
				dot = char_insert(dot, '\n', ALLOW_UNDO_CHAIN);
				// on the last line of file don't move to prev line
				if (whole && dot != (end-1)) {
					dot_prev();
				}
			} else if (c == 'd') {
				dot_begin();
				dot_skip_over_ws();
			}
		}
		if (c1 != 27) {
			// if CHANGING, not deleting, start inserting after the delete
			if (c == 'c') {
				strcpy(buf, "Change");
				goto dc_i;	// start inserting
			}
			if (c == 'd') {
				strcpy(buf, "Delete");
			}
#if ENABLE_FEATURE_VI_YANKMARK
			if (c == 'y' || c == 'Y') {
				strcpy(buf, "Yank");
			}
			p = reg[YDreg];
			q = p + strlen(p);
			for (cnt = 0; p <= q; p++) {
				if (*p == '\n')
					cnt++;
			}
			status_line("%s %d lines (%d chars) using [%c]",
				buf, cnt, strlen(reg[YDreg]), what_reg());
#endif
			end_cmd_q();	// stop adding to q
		}
		break;
	}
	case 'k':			// k- goto prev line, same col
	case KEYCODE_UP:		// cursor key Up
		do {
			dot_prev();
			dot = move_to_col(dot, ccol + offset);	// try stay in same col
		} while (--cmdcnt > 0);
		break;
	case 'r':			// r- replace the current char with user input
		c1 = get_one_char();	// get the replacement char
		if (*dot != '\n') {
#if ENABLE_FEATURE_VI_UNDO
			undo_push(dot, 1, UNDO_DEL);
			*dot = c1;
			undo_push(dot, 1, UNDO_INS_CHAIN);
#else
			*dot = c1;
			modified_count++;
#endif
		}
		end_cmd_q();	// stop adding to q
		break;
	case 't':			// t- move to char prior to next x
		last_forward_char = get_one_char();
		do_cmd(';');
		if (*dot == last_forward_char)
			dot_left();
		last_forward_char = 0;
		break;
	case 'w':			// w- forward a word
		do {
			if (isalnum(*dot) || *dot == '_') {	// we are on ALNUM
				dot = skip_thing(dot, 1, FORWARD, S_END_ALNUM);
			} else if (ispunct(*dot)) {	// we are on PUNCT
				dot = skip_thing(dot, 1, FORWARD, S_END_PUNCT);
			}
			if (dot < end - 1)
				dot++;		// move over word
			if (isspace(*dot)) {
				dot = skip_thing(dot, 2, FORWARD, S_OVER_WS);
			}
		} while (--cmdcnt > 0);
		break;
	case 'z':			// z-
		c1 = get_one_char();	// get the replacement char
		cnt = 0;
		if (c1 == '.')
			cnt = (rows - 2) / 2;	// put dot at center
		if (c1 == '-')
			cnt = rows - 2;	// put dot at bottom
		screenbegin = begin_line(dot);	// start dot at top
		dot_scroll(cnt, -1);
		break;
	case '|':			// |- move to column "cmdcnt"
		dot = move_to_col(dot, cmdcnt - 1);	// try to move to column
		break;
	case '~':			// ~- flip the case of letters   a-z -> A-Z
		do {
#if ENABLE_FEATURE_VI_UNDO
			if (islower(*dot)) {
				undo_push(dot, 1, UNDO_DEL);
				*dot = toupper(*dot);
				undo_push(dot, 1, UNDO_INS_CHAIN);
			} else if (isupper(*dot)) {
				undo_push(dot, 1, UNDO_DEL);
				*dot = tolower(*dot);
				undo_push(dot, 1, UNDO_INS_CHAIN);
			}
#else
			if (islower(*dot)) {
				*dot = toupper(*dot);
				modified_count++;
			} else if (isupper(*dot)) {
				*dot = tolower(*dot);
				modified_count++;
			}
#endif
			dot_right();
		} while (--cmdcnt > 0);
		end_cmd_q();	// stop adding to q
		break;
		//----- The Cursor and Function Keys -----------------------------
	case KEYCODE_HOME:	// Cursor Key Home
		dot_begin();
		break;
		// The Fn keys could point to do_macro which could translate them
#if 0
	case KEYCODE_FUN1:	// Function Key F1
	case KEYCODE_FUN2:	// Function Key F2
	case KEYCODE_FUN3:	// Function Key F3
	case KEYCODE_FUN4:	// Function Key F4
	case KEYCODE_FUN5:	// Function Key F5
	case KEYCODE_FUN6:	// Function Key F6
	case KEYCODE_FUN7:	// Function Key F7
	case KEYCODE_FUN8:	// Function Key F8
	case KEYCODE_FUN9:	// Function Key F9
	case KEYCODE_FUN10:	// Function Key F10
	case KEYCODE_FUN11:	// Function Key F11
	case KEYCODE_FUN12:	// Function Key F12
		break;
#endif
	}

 dc1:
	// if text[] just became empty, add back an empty line
	if (end == text) {
		char_insert(text, '\n', NO_UNDO);	// start empty buf with dummy line
		dot = text;
	}
	// it is OK for dot to exactly equal to end, otherwise check dot validity
	if (dot != end) {
		dot = bound_dot(dot);	// make sure "dot" is valid
	}
#if ENABLE_FEATURE_VI_YANKMARK
	check_context(c);	// update the current context
#endif

	if (!isdigit(c))
		cmdcnt = 0;		// cmd was not a number, reset cmdcnt
	cnt = dot - begin_line(dot);
	// Try to stay off of the Newline
	if (*dot == '\n' && cnt > 0 && cmd_mode == 0)
		dot--;
}

/* NB!  the CRASHME code is unmaintained, and doesn't currently build */
#if ENABLE_FEATURE_VI_CRASHME
static int totalcmds = 0;
static int Mp = 85;             // Movement command Probability
static int Np = 90;             // Non-movement command Probability
static int Dp = 96;             // Delete command Probability
static int Ip = 97;             // Insert command Probability
static int Yp = 98;             // Yank command Probability
static int Pp = 99;             // Put command Probability
static int M = 0, N = 0, I = 0, D = 0, Y = 0, P = 0, U = 0;
static const char chars[20] = "\t012345 abcdABCD-=.$";
static const char *const words[20] = {
	"this", "is", "a", "test",
	"broadcast", "the", "emergency", "of",
	"system", "quick", "brown", "fox",
	"jumped", "over", "lazy", "dogs",
	"back", "January", "Febuary", "March"
};
static const char *const lines[20] = {
	"You should have received a copy of the GNU General Public License\n",
	"char c, cm, *cmd, *cmd1;\n",
	"generate a command by percentages\n",
	"Numbers may be typed as a prefix to some commands.\n",
	"Quit, discarding changes!\n",
	"Forced write, if permission originally not valid.\n",
	"In general, any ex or ed command (such as substitute or delete).\n",
	"I have tickets available for the Blazers vs LA Clippers for Monday, Janurary 1 at 1:00pm.\n",
	"Please get w/ me and I will go over it with you.\n",
	"The following is a list of scheduled, committed changes.\n",
	"1.   Launch Norton Antivirus (Start, Programs, Norton Antivirus)\n",
	"Reminder....Town Meeting in Central Perk cafe today at 3:00pm.\n",
	"Any question about transactions please contact Sterling Huxley.\n",
	"I will try to get back to you by Friday, December 31.\n",
	"This Change will be implemented on Friday.\n",
	"Let me know if you have problems accessing this;\n",
	"Sterling Huxley recently added you to the access list.\n",
	"Would you like to go to lunch?\n",
	"The last command will be automatically run.\n",
	"This is too much english for a computer geek.\n",
};
static char *multilines[20] = {
	"You should have received a copy of the GNU General Public License\n",
	"char c, cm, *cmd, *cmd1;\n",
	"generate a command by percentages\n",
	"Numbers may be typed as a prefix to some commands.\n",
	"Quit, discarding changes!\n",
	"Forced write, if permission originally not valid.\n",
	"In general, any ex or ed command (such as substitute or delete).\n",
	"I have tickets available for the Blazers vs LA Clippers for Monday, Janurary 1 at 1:00pm.\n",
	"Please get w/ me and I will go over it with you.\n",
	"The following is a list of scheduled, committed changes.\n",
	"1.   Launch Norton Antivirus (Start, Programs, Norton Antivirus)\n",
	"Reminder....Town Meeting in Central Perk cafe today at 3:00pm.\n",
	"Any question about transactions please contact Sterling Huxley.\n",
	"I will try to get back to you by Friday, December 31.\n",
	"This Change will be implemented on Friday.\n",
	"Let me know if you have problems accessing this;\n",
	"Sterling Huxley recently added you to the access list.\n",
	"Would you like to go to lunch?\n",
	"The last command will be automatically run.\n",
	"This is too much english for a computer geek.\n",
};

// create a random command to execute
static void crash_dummy()
{
	static int sleeptime;   // how long to pause between commands
	char c, cm, *cmd, *cmd1;
	int i, cnt, thing, rbi, startrbi, percent;

	// "dot" movement commands
	cmd1 = " \n\r\002\004\005\006\025\0310^$-+wWeEbBhjklHL";

	// is there already a command running?
	if (readbuffer[0] > 0)
		goto cd1;
 cd0:
	readbuffer[0] = 'X';
	startrbi = rbi = 1;
	sleeptime = 0;          // how long to pause between commands
	memset(readbuffer, '\0', sizeof(readbuffer));
	// generate a command by percentages
	percent = (int) lrand48() % 100;        // get a number from 0-99
	if (percent < Mp) {     //  Movement commands
		// available commands
		cmd = cmd1;
		M++;
	} else if (percent < Np) {      //  non-movement commands
		cmd = "mz<>\'\"";       // available commands
		N++;
	} else if (percent < Dp) {      //  Delete commands
		cmd = "dx";             // available commands
		D++;
	} else if (percent < Ip) {      //  Inset commands
		cmd = "iIaAsrJ";        // available commands
		I++;
	} else if (percent < Yp) {      //  Yank commands
		cmd = "yY";             // available commands
		Y++;
	} else if (percent < Pp) {      //  Put commands
		cmd = "pP";             // available commands
		P++;
	} else {
		// We do not know how to handle this command, try again
		U++;
		goto cd0;
	}
	// randomly pick one of the available cmds from "cmd[]"
	i = (int) lrand48() % strlen(cmd);
	cm = cmd[i];
	if (strchr(":\024", cm))
		goto cd0;               // dont allow colon or ctrl-T commands
	readbuffer[rbi++] = cm; // put cmd into input buffer

	// now we have the command-
	// there are 1, 2, and multi char commands
	// find out which and generate the rest of command as necessary
	if (strchr("dmryz<>\'\"", cm)) {        // 2-char commands
		cmd1 = " \n\r0$^-+wWeEbBhjklHL";
		if (cm == 'm' || cm == '\'' || cm == '\"') {    // pick a reg[]
			cmd1 = "abcdefghijklmnopqrstuvwxyz";
		}
		thing = (int) lrand48() % strlen(cmd1); // pick a movement command
		c = cmd1[thing];
		readbuffer[rbi++] = c;  // add movement to input buffer
	}
	if (strchr("iIaAsc", cm)) {     // multi-char commands
		if (cm == 'c') {
			// change some thing
			thing = (int) lrand48() % strlen(cmd1); // pick a movement command
			c = cmd1[thing];
			readbuffer[rbi++] = c;  // add movement to input buffer
		}
		thing = (int) lrand48() % 4;    // what thing to insert
		cnt = (int) lrand48() % 10;     // how many to insert
		for (i = 0; i < cnt; i++) {
			if (thing == 0) {       // insert chars
				readbuffer[rbi++] = chars[((int) lrand48() % strlen(chars))];
			} else if (thing == 1) {        // insert words
				strcat(readbuffer, words[(int) lrand48() % 20]);
				strcat(readbuffer, " ");
				sleeptime = 0;  // how fast to type
			} else if (thing == 2) {        // insert lines
				strcat(readbuffer, lines[(int) lrand48() % 20]);
				sleeptime = 0;  // how fast to type
			} else {        // insert multi-lines
				strcat(readbuffer, multilines[(int) lrand48() % 20]);
				sleeptime = 0;  // how fast to type
			}
		}
		strcat(readbuffer, ESC);
	}
	readbuffer[0] = strlen(readbuffer + 1);
 cd1:
	totalcmds++;
	if (sleeptime > 0)
		mysleep(sleeptime);      // sleep 1/100 sec
}

// test to see if there are any errors
static void crash_test()
{
	static time_t oldtim;

	time_t tim;
	char d[2], msg[80];

	msg[0] = '\0';
	if (end < text) {
		strcat(msg, "end<text ");
	}
	if (end > textend) {
		strcat(msg, "end>textend ");
	}
	if (dot < text) {
		strcat(msg, "dot<text ");
	}
	if (dot > end) {
		strcat(msg, "dot>end ");
	}
	if (screenbegin < text) {
		strcat(msg, "screenbegin<text ");
	}
	if (screenbegin > end - 1) {
		strcat(msg, "screenbegin>end-1 ");
	}

	if (msg[0]) {
		printf("\n\n%d: \'%c\' %s\n\n\n%s[Hit return to continue]%s",
			totalcmds, last_input_char, msg, ESC_BOLD_TEXT, ESC_NORM_TEXT);
		fflush_all();
		while (safe_read(STDIN_FILENO, d, 1) > 0) {
			if (d[0] == '\n' || d[0] == '\r')
				break;
		}
	}
	tim = time(NULL);
	if (tim >= (oldtim + 3)) {
		sprintf(status_buffer,
				"Tot=%d: M=%d N=%d I=%d D=%d Y=%d P=%d U=%d size=%d",
				totalcmds, M, N, I, D, Y, P, U, end - text + 1);
		oldtim = tim;
	}
}
#endif
