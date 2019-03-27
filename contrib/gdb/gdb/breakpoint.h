/* Data structures associated with breakpoints in GDB.
   Copyright 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001,
   2002, 2003, 2004
   Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#if !defined (BREAKPOINT_H)
#define BREAKPOINT_H 1

#include "frame.h"
#include "value.h"

#include "gdb-events.h"

struct value;
struct block;

/* This is the maximum number of bytes a breakpoint instruction can take.
   Feel free to increase it.  It's just used in a few places to size
   arrays that should be independent of the target architecture.  */

#define	BREAKPOINT_MAX	16

/* Type of breakpoint. */
/* FIXME In the future, we should fold all other breakpoint-like things into
   here.  This includes:

   * single-step (for machines where we have to simulate single stepping)
   (probably, though perhaps it is better for it to look as much as
   possible like a single-step to wait_for_inferior).  */

enum bptype
  {
    bp_none = 0,		/* Eventpoint has been deleted. */
    bp_breakpoint,		/* Normal breakpoint */
    bp_hardware_breakpoint,	/* Hardware assisted breakpoint */
    bp_until,			/* used by until command */
    bp_finish,			/* used by finish command */
    bp_watchpoint,		/* Watchpoint */
    bp_hardware_watchpoint,	/* Hardware assisted watchpoint */
    bp_read_watchpoint,		/* read watchpoint, (hardware assisted) */
    bp_access_watchpoint,	/* access watchpoint, (hardware assisted) */
    bp_longjmp,			/* secret breakpoint to find longjmp() */
    bp_longjmp_resume,		/* secret breakpoint to escape longjmp() */

    /* Used by wait_for_inferior for stepping over subroutine calls, for
       stepping over signal handlers, and for skipping prologues.  */
    bp_step_resume,

    /* Used by wait_for_inferior for stepping over signal handlers.  */
    bp_through_sigtramp,

    /* Used to detect when a watchpoint expression has gone out of
       scope.  These breakpoints are usually not visible to the user.

       This breakpoint has some interesting properties:

       1) There's always a 1:1 mapping between watchpoints
       on local variables and watchpoint_scope breakpoints.

       2) It automatically deletes itself and the watchpoint it's
       associated with when hit.

       3) It can never be disabled.  */
    bp_watchpoint_scope,

    /* The breakpoint at the end of a call dummy.  */
    /* FIXME: What if the function we are calling longjmp()s out of the
       call, or the user gets out with the "return" command?  We currently
       have no way of cleaning up the breakpoint in these (obscure) situations.
       (Probably can solve this by noticing longjmp, "return", etc., it's
       similar to noticing when a watchpoint on a local variable goes out
       of scope (with hardware support for watchpoints)).  */
    bp_call_dummy,

    /* Some dynamic linkers (HP, maybe Solaris) can arrange for special
       code in the inferior to run when significant events occur in the
       dynamic linker (for example a library is loaded or unloaded).

       By placing a breakpoint in this magic code GDB will get control
       when these significant events occur.  GDB can then re-examine
       the dynamic linker's data structures to discover any newly loaded
       dynamic libraries.  */
    bp_shlib_event,

    /* Some multi-threaded systems can arrange for a location in the 
       inferior to be executed when certain thread-related events occur
       (such as thread creation or thread death).

       By placing a breakpoint at one of these locations, GDB will get
       control when these events occur.  GDB can then update its thread
       lists etc.  */

    bp_thread_event,

    /* On the same principal, an overlay manager can arrange to call a
       magic location in the inferior whenever there is an interesting
       change in overlay status.  GDB can update its overlay tables
       and fiddle with breakpoints in overlays when this breakpoint 
       is hit.  */

    bp_overlay_event, 

    /* These breakpoints are used to implement the "catch load" command
       on platforms whose dynamic linkers support such functionality.  */
    bp_catch_load,

    /* These breakpoints are used to implement the "catch unload" command
       on platforms whose dynamic linkers support such functionality.  */
    bp_catch_unload,

    /* These are not really breakpoints, but are catchpoints that
       implement the "catch fork", "catch vfork" and "catch exec" commands
       on platforms whose kernel support such functionality.  (I.e.,
       kernels which can raise an event when a fork or exec occurs, as
       opposed to the debugger setting breakpoints on functions named
       "fork" or "exec".) */
    bp_catch_fork,
    bp_catch_vfork,
    bp_catch_exec,

    /* These are catchpoints to implement "catch catch" and "catch throw"
       commands for C++ exception handling. */
    bp_catch_catch,
    bp_catch_throw


  };

/* States of enablement of breakpoint. */

enum enable_state
  {
    bp_disabled,	/* The eventpoint is inactive, and cannot trigger. */
    bp_enabled,		/* The eventpoint is active, and can trigger. */
    bp_shlib_disabled,	/* The eventpoint's address is in an unloaded solib.
			   The eventpoint will be automatically enabled 
			   and reset when that solib is loaded. */
    bp_call_disabled,	/* The eventpoint has been disabled while a call 
			   into the inferior is "in flight", because some 
			   eventpoints interfere with the implementation of 
			   a call on some targets.  The eventpoint will be 
			   automatically enabled and reset when the call 
			   "lands" (either completes, or stops at another 
			   eventpoint). */
    bp_permanent	/* There is a breakpoint instruction hard-wired into
			   the target's code.  Don't try to write another
			   breakpoint instruction on top of it, or restore
			   its value.  Step over it using the architecture's
			   SKIP_INSN macro.  */
  };


/* Disposition of breakpoint.  Ie: what to do after hitting it. */

enum bpdisp
  {
    disp_del,			/* Delete it */
    disp_del_at_next_stop,	/* Delete at next stop, whether hit or not */
    disp_disable,		/* Disable it */
    disp_donttouch		/* Leave it alone */
  };

enum target_hw_bp_type
  {
    hw_write   = 0, 		/* Common  HW watchpoint */
    hw_read    = 1, 		/* Read    HW watchpoint */
    hw_access  = 2, 		/* Access  HW watchpoint */
    hw_execute = 3		/* Execute HW breakpoint */
  };

/* GDB maintains two types of information about each breakpoint (or
   watchpoint, or other related event).  The first type corresponds
   to struct breakpoint; this is a relatively high-level structure
   which contains the source location(s), stopping conditions, user
   commands to execute when the breakpoint is hit, and so forth.

   The second type of information corresponds to struct bp_location.
   Each breakpoint has one or (eventually) more locations associated
   with it, which represent target-specific and machine-specific
   mechanisms for stopping the program.  For instance, a watchpoint
   expression may require multiple hardware watchpoints in order to
   catch all changes in the value of the expression being watched.  */

enum bp_loc_type
{
  bp_loc_software_breakpoint,
  bp_loc_hardware_breakpoint,
  bp_loc_hardware_watchpoint,
  bp_loc_other			/* Miscellaneous...  */
};

struct bp_location
{
  /* Chain pointer to the next breakpoint location.  */
  struct bp_location *next;

  /* Type of this breakpoint location.  */
  enum bp_loc_type loc_type;

  /* Each breakpoint location must belong to exactly one higher-level
     breakpoint.  This and the DUPLICATE flag are more straightforward
     than reference counting.  */
  struct breakpoint *owner;

  /* Nonzero if this breakpoint is now inserted.  */
  char inserted;

  /* Nonzero if this is not the first breakpoint in the list
     for the given address.  */
  char duplicate;

  /* If we someday support real thread-specific breakpoints, then
     the breakpoint location will need a thread identifier.  */

  /* Data for specific breakpoint types.  These could be a union, but
     simplicity is more important than memory usage for breakpoints.  */

  /* Note that zero is a perfectly valid code address on some platforms
     (for example, the mn10200 (OBSOLETE) and mn10300 simulators).  NULL
     is not a special value for this field.  Valid for all types except
     bp_loc_other.  */
  CORE_ADDR address;

  /* For any breakpoint type with an address, this is the BFD section
     associated with the address.  Used primarily for overlay debugging.  */
  asection *section;

  /* "Real" contents of byte where breakpoint has been inserted.
     Valid only when breakpoints are in the program.  Under the complete
     control of the target insert_breakpoint and remove_breakpoint routines.
     No other code should assume anything about the value(s) here.
     Valid only for bp_loc_software_breakpoint.  */
  char shadow_contents[BREAKPOINT_MAX];

  /* Address at which breakpoint was requested, either by the user or
     by GDB for internal breakpoints.  This will usually be the same
     as ``address'' (above) except for cases in which
     ADJUST_BREAKPOINT_ADDRESS has computed a different address at
     which to place the breakpoint in order to comply with a
     processor's architectual constraints.  */
  CORE_ADDR requested_address;
};

/* This structure is a collection of function pointers that, if available,
   will be called instead of the performing the default action for this
   bptype.  */

struct breakpoint_ops 
{
  /* The normal print routine for this breakpoint, called when we
     hit it.  */
  enum print_stop_action (*print_it) (struct breakpoint *);

  /* Display information about this breakpoint, for "info breakpoints".  */
  void (*print_one) (struct breakpoint *, CORE_ADDR *);

  /* Display information about this breakpoint after setting it (roughly
     speaking; this is called from "mention").  */
  void (*print_mention) (struct breakpoint *);
};

/* Note that the ->silent field is not currently used by any commands
   (though the code is in there if it was to be, and set_raw_breakpoint
   does set it to 0).  I implemented it because I thought it would be
   useful for a hack I had to put in; I'm going to leave it in because
   I can see how there might be times when it would indeed be useful */

/* This is for a breakpoint or a watchpoint.  */

struct breakpoint
  {
    struct breakpoint *next;
    /* Type of breakpoint. */
    enum bptype type;
    /* Zero means disabled; remember the info but don't break here.  */
    enum enable_state enable_state;
    /* What to do with this breakpoint after we hit it. */
    enum bpdisp disposition;
    /* Number assigned to distinguish breakpoints.  */
    int number;

    /* Location(s) associated with this high-level breakpoint.  */
    struct bp_location *loc;

    /* Line number of this address.  */

    int line_number;

    /* Source file name of this address.  */

    char *source_file;

    /* Non-zero means a silent breakpoint (don't print frame info
       if we stop here). */
    unsigned char silent;
    /* Number of stops at this breakpoint that should
       be continued automatically before really stopping.  */
    int ignore_count;
    /* Chain of command lines to execute when this breakpoint is hit.  */
    struct command_line *commands;
    /* Stack depth (address of frame).  If nonzero, break only if fp
       equals this.  */
    struct frame_id frame_id;
    /* Conditional.  Break only if this expression's value is nonzero.  */
    struct expression *cond;

    /* String we used to set the breakpoint (malloc'd).  */
    char *addr_string;
    /* Language we used to set the breakpoint.  */
    enum language language;
    /* Input radix we used to set the breakpoint.  */
    int input_radix;
    /* String form of the breakpoint condition (malloc'd), or NULL if there
       is no condition.  */
    char *cond_string;
    /* String form of exp (malloc'd), or NULL if none.  */
    char *exp_string;

    /* The expression we are watching, or NULL if not a watchpoint.  */
    struct expression *exp;
    /* The largest block within which it is valid, or NULL if it is
       valid anywhere (e.g. consists just of global symbols).  */
    struct block *exp_valid_block;
    /* Value of the watchpoint the last time we checked it.  */
    struct value *val;

    /* Holds the value chain for a hardware watchpoint expression.  */
    struct value *val_chain;

    /* Holds the address of the related watchpoint_scope breakpoint
       when using watchpoints on local variables (might the concept
       of a related breakpoint be useful elsewhere, if not just call
       it the watchpoint_scope breakpoint or something like that. FIXME).  */
    struct breakpoint *related_breakpoint;

    /* Holds the frame address which identifies the frame this
       watchpoint should be evaluated in, or `null' if the watchpoint
       should be evaluated on the outermost frame.  */
    struct frame_id watchpoint_frame;

    /* Thread number for thread-specific breakpoint, or -1 if don't care */
    int thread;

    /* Count of the number of times this breakpoint was taken, dumped
       with the info, but not used for anything else.  Useful for
       seeing how many times you hit a break prior to the program
       aborting, so you can back up to just before the abort.  */
    int hit_count;

    /* Filename of a dynamically-linked library (dll), used for
       bp_catch_load and bp_catch_unload (malloc'd), or NULL if any
       library is significant.  */
    char *dll_pathname;

    /* Filename of a dll whose state change (e.g., load or unload)
       triggered this catchpoint.  This field is only valid immediately
       after this catchpoint has triggered.  */
    char *triggered_dll_pathname;

    /* Process id of a child process whose forking triggered this
       catchpoint.  This field is only valid immediately after this
       catchpoint has triggered.  */
    int forked_inferior_pid;

    /* Filename of a program whose exec triggered this catchpoint.
       This field is only valid immediately after this catchpoint has
       triggered.  */
    char *exec_pathname;

    /* Methods associated with this breakpoint.  */
    struct breakpoint_ops *ops;

    /* Was breakpoint issued from a tty?  Saved for the use of pending breakpoints.  */
    int from_tty;

    /* Flag value for pending breakpoint.
       first bit  : 0 non-temporary, 1 temporary.
       second bit : 0 normal breakpoint, 1 hardware breakpoint. */
    int flag;

    /* Is breakpoint pending on shlib loads?  */
    int pending;
  };

/* The following stuff is an abstract data type "bpstat" ("breakpoint
   status").  This provides the ability to determine whether we have
   stopped at a breakpoint, and what we should do about it.  */

typedef struct bpstats *bpstat;

/* Interface:  */
/* Clear a bpstat so that it says we are not at any breakpoint.
   Also free any storage that is part of a bpstat.  */
extern void bpstat_clear (bpstat *);

/* Return a copy of a bpstat.  Like "bs1 = bs2" but all storage that
   is part of the bpstat is copied as well.  */
extern bpstat bpstat_copy (bpstat);

extern bpstat bpstat_stop_status (CORE_ADDR pc, ptid_t ptid);

/* This bpstat_what stuff tells wait_for_inferior what to do with a
   breakpoint (a challenging task).  */

enum bpstat_what_main_action
  {
    /* Perform various other tests; that is, this bpstat does not
       say to perform any action (e.g. failed watchpoint and nothing
       else).  */
    BPSTAT_WHAT_KEEP_CHECKING,

    /* Rather than distinguish between noisy and silent stops here, it
       might be cleaner to have bpstat_print make that decision (also
       taking into account stop_print_frame and source_only).  But the
       implications are a bit scary (interaction with auto-displays, etc.),
       so I won't try it.  */

    /* Stop silently.  */
    BPSTAT_WHAT_STOP_SILENT,

    /* Stop and print.  */
    BPSTAT_WHAT_STOP_NOISY,

    /* Remove breakpoints, single step once, then put them back in and
       go back to what we were doing.  It's possible that this should be
       removed from the main_action and put into a separate field, to more
       cleanly handle BPSTAT_WHAT_CLEAR_LONGJMP_RESUME_SINGLE.  */
    BPSTAT_WHAT_SINGLE,

    /* Set longjmp_resume breakpoint, remove all other breakpoints,
       and continue.  The "remove all other breakpoints" part is required
       if we are also stepping over another breakpoint as well as doing
       the longjmp handling.  */
    BPSTAT_WHAT_SET_LONGJMP_RESUME,

    /* Clear longjmp_resume breakpoint, then handle as
       BPSTAT_WHAT_KEEP_CHECKING.  */
    BPSTAT_WHAT_CLEAR_LONGJMP_RESUME,

    /* Clear longjmp_resume breakpoint, then handle as BPSTAT_WHAT_SINGLE.  */
    BPSTAT_WHAT_CLEAR_LONGJMP_RESUME_SINGLE,

    /* Clear step resume breakpoint, and keep checking.  */
    BPSTAT_WHAT_STEP_RESUME,

    /* Clear through_sigtramp breakpoint, muck with trap_expected, and keep
       checking.  */
    BPSTAT_WHAT_THROUGH_SIGTRAMP,

    /* Check the dynamic linker's data structures for new libraries, then
       keep checking.  */
    BPSTAT_WHAT_CHECK_SHLIBS,

    /* Check the dynamic linker's data structures for new libraries, then
       resume out of the dynamic linker's callback, stop and print.  */
    BPSTAT_WHAT_CHECK_SHLIBS_RESUME_FROM_HOOK,

    /* This is just used to keep track of how many enums there are.  */
    BPSTAT_WHAT_LAST
  };

struct bpstat_what
  {
    enum bpstat_what_main_action main_action;

    /* Did we hit a call dummy breakpoint?  This only goes with a main_action
       of BPSTAT_WHAT_STOP_SILENT or BPSTAT_WHAT_STOP_NOISY (the concept of
       continuing from a call dummy without popping the frame is not a
       useful one).  */
    int call_dummy;
  };

/* The possible return values for print_bpstat, print_it_normal,
   print_it_done, print_it_noop. */
enum print_stop_action
  {
    PRINT_UNKNOWN = -1,
    PRINT_SRC_AND_LOC,
    PRINT_SRC_ONLY,
    PRINT_NOTHING
  };

/* Tell what to do about this bpstat.  */
struct bpstat_what bpstat_what (bpstat);

/* Find the bpstat associated with a breakpoint.  NULL otherwise. */
bpstat bpstat_find_breakpoint (bpstat, struct breakpoint *);

/* Find a step_resume breakpoint associated with this bpstat.
   (If there are multiple step_resume bp's on the list, this function
   will arbitrarily pick one.)

   It is an error to use this function if BPSTAT doesn't contain a
   step_resume breakpoint.

   See wait_for_inferior's use of this function.
 */
extern struct breakpoint *bpstat_find_step_resume_breakpoint (bpstat);

/* Nonzero if a signal that we got in wait() was due to circumstances
   explained by the BS.  */
/* Currently that is true if we have hit a breakpoint, or if there is
   a watchpoint enabled.  */
#define bpstat_explains_signal(bs) ((bs) != NULL)

/* Nonzero if we should step constantly (e.g. watchpoints on machines
   without hardware support).  This isn't related to a specific bpstat,
   just to things like whether watchpoints are set.  */
extern int bpstat_should_step (void);

/* Nonzero if there are enabled hardware watchpoints. */
extern int bpstat_have_active_hw_watchpoints (void);

/* Print a message indicating what happened.  Returns nonzero to
   say that only the source line should be printed after this (zero
   return means print the frame as well as the source line).  */
extern enum print_stop_action bpstat_print (bpstat);

/* Return the breakpoint number of the first breakpoint we are stopped
   at.  *BSP upon return is a bpstat which points to the remaining
   breakpoints stopped at (but which is not guaranteed to be good for
   anything but further calls to bpstat_num).
   Return 0 if passed a bpstat which does not indicate any breakpoints.  */
extern int bpstat_num (bpstat *);

/* Perform actions associated with having stopped at *BSP.  Actually, we just
   use this for breakpoint commands.  Perhaps other actions will go here
   later, but this is executed at a late time (from the command loop).  */
extern void bpstat_do_actions (bpstat *);

/* Modify BS so that the actions will not be performed.  */
extern void bpstat_clear_actions (bpstat);

/* Given a bpstat that records zero or more triggered eventpoints, this
   function returns another bpstat which contains only the catchpoints
   on that first list, if any.
 */
extern void bpstat_get_triggered_catchpoints (bpstat, bpstat *);

/* Implementation:  */

/* Values used to tell the printing routine how to behave for this bpstat. */
enum bp_print_how
  {
    /* This is used when we want to do a normal printing of the reason
       for stopping. The output will depend on the type of eventpoint
       we are dealing with. This is the default value, most commonly
       used. */
    print_it_normal,
    /* This is used when nothing should be printed for this bpstat entry.  */
    print_it_noop,
    /* This is used when everything which needs to be printed has
       already been printed.  But we still want to print the frame.  */
    print_it_done
  };

struct bpstats
  {
    /* Linked list because there can be two breakpoints at the same
       place, and a bpstat reflects the fact that both have been hit.  */
    bpstat next;
    /* Breakpoint that we are at.  */
    struct breakpoint *breakpoint_at;
    /* Commands left to be done.  */
    struct command_line *commands;
    /* Old value associated with a watchpoint.  */
    struct value *old_val;

    /* Nonzero if this breakpoint tells us to print the frame.  */
    char print;

    /* Nonzero if this breakpoint tells us to stop.  */
    char stop;

    /* Tell bpstat_print and print_bp_stop_message how to print stuff
       associated with this element of the bpstat chain.  */
    enum bp_print_how print_it;
  };

enum inf_context
  {
    inf_starting,
    inf_running,
    inf_exited
  };

/* The possible return values for breakpoint_here_p.
   We guarantee that zero always means "no breakpoint here".  */
enum breakpoint_here
  {
    no_breakpoint_here = 0,
    ordinary_breakpoint_here,
    permanent_breakpoint_here
  };


/* Prototypes for breakpoint-related functions.  */

extern enum breakpoint_here breakpoint_here_p (CORE_ADDR);

extern int breakpoint_inserted_here_p (CORE_ADDR);

extern int software_breakpoint_inserted_here_p (CORE_ADDR);

/* FIXME: cagney/2002-11-10: The current [generic] dummy-frame code
   implements a functional superset of this function.  The only reason
   it hasn't been removed is because some architectures still don't
   use the new framework.  Once they have been fixed, this can go.  */
struct frame_info;
extern int deprecated_frame_in_dummy (struct frame_info *);

extern int breakpoint_thread_match (CORE_ADDR, ptid_t);

extern void until_break_command (char *, int, int);

extern void breakpoint_re_set (void);

extern void breakpoint_re_set_thread (struct breakpoint *);

extern int ep_is_exception_catchpoint (struct breakpoint *);

extern struct breakpoint *set_momentary_breakpoint
  (struct symtab_and_line, struct frame_id, enum bptype);

extern void set_ignore_count (int, int, int);

extern void set_default_breakpoint (int, CORE_ADDR, struct symtab *, int);

extern void mark_breakpoints_out (void);

extern void breakpoint_init_inferior (enum inf_context);

extern struct cleanup *make_cleanup_delete_breakpoint (struct breakpoint *);

extern struct cleanup *make_exec_cleanup_delete_breakpoint (struct breakpoint *);

extern void delete_breakpoint (struct breakpoint *);

extern void breakpoint_auto_delete (bpstat);

extern void breakpoint_clear_ignore_counts (void);

extern void break_command (char *, int);

extern void hbreak_command_wrapper (char *, int);
extern void thbreak_command_wrapper (char *, int);
extern void rbreak_command_wrapper (char *, int);
extern void watch_command_wrapper (char *, int);
extern void awatch_command_wrapper (char *, int);
extern void rwatch_command_wrapper (char *, int);
extern void tbreak_command (char *, int);

extern int insert_breakpoints (void);

extern int remove_breakpoints (void);

/* This function can be used to physically insert eventpoints from the
   specified traced inferior process, without modifying the breakpoint
   package's state.  This can be useful for those targets which support
   following the processes of a fork() or vfork() system call, when both
   of the resulting two processes are to be followed.  */
extern int reattach_breakpoints (int);

/* This function can be used to update the breakpoint package's state
   after an exec() system call has been executed.

   This function causes the following:

   - All eventpoints are marked "not inserted".
   - All eventpoints with a symbolic address are reset such that
   the symbolic address must be reevaluated before the eventpoints
   can be reinserted.
   - The solib breakpoints are explicitly removed from the breakpoint
   list.
   - A step-resume breakpoint, if any, is explicitly removed from the
   breakpoint list.
   - All eventpoints without a symbolic address are removed from the
   breakpoint list. */
extern void update_breakpoints_after_exec (void);

/* This function can be used to physically remove hardware breakpoints
   and watchpoints from the specified traced inferior process, without
   modifying the breakpoint package's state.  This can be useful for
   those targets which support following the processes of a fork() or
   vfork() system call, when one of the resulting two processes is to
   be detached and allowed to run free.

   It is an error to use this function on the process whose id is
   inferior_ptid.  */
extern int detach_breakpoints (int);

extern void enable_longjmp_breakpoint (void);
extern void disable_longjmp_breakpoint (void);
extern void enable_overlay_breakpoints (void);
extern void disable_overlay_breakpoints (void);

extern void set_longjmp_resume_breakpoint (CORE_ADDR, struct frame_id);
/* These functions respectively disable or reenable all currently
   enabled watchpoints.  When disabled, the watchpoints are marked
   call_disabled.  When reenabled, they are marked enabled.

   The intended client of these functions is call_function_by_hand.

   The inferior must be stopped, and all breakpoints removed, when
   these functions are used.

   The need for these functions is that on some targets (e.g., HP-UX),
   gdb is unable to unwind through the dummy frame that is pushed as
   part of the implementation of a call command.  Watchpoints can
   cause the inferior to stop in places where this frame is visible,
   and that can cause execution control to become very confused.

   Note that if a user sets breakpoints in an interactively called
   function, the call_disabled watchpoints will have been reenabled
   when the first such breakpoint is reached.  However, on targets
   that are unable to unwind through the call dummy frame, watches
   of stack-based storage may then be deleted, because gdb will
   believe that their watched storage is out of scope.  (Sigh.) */
extern void disable_watchpoints_before_interactive_call_start (void);

extern void enable_watchpoints_after_interactive_call_stop (void);


extern void clear_breakpoint_hit_counts (void);

extern int get_number (char **);

extern int get_number_or_range (char **);

/* The following are for displays, which aren't really breakpoints, but
   here is as good a place as any for them.  */

extern void disable_current_display (void);

extern void do_displays (void);

extern void disable_display (int);

extern void clear_displays (void);

extern void disable_breakpoint (struct breakpoint *);

extern void enable_breakpoint (struct breakpoint *);

extern void make_breakpoint_permanent (struct breakpoint *);

extern struct breakpoint *create_solib_event_breakpoint (CORE_ADDR);

extern struct breakpoint *create_thread_event_breakpoint (CORE_ADDR);

extern void remove_solib_event_breakpoints (void);

extern void remove_thread_event_breakpoints (void);

extern void disable_breakpoints_in_shlibs (int silent);

extern void re_enable_breakpoints_in_shlibs (void);

extern void create_solib_load_event_breakpoint (char *, int, char *, char *);

extern void create_solib_unload_event_breakpoint (char *, int,
						  char *, char *);

extern void create_fork_event_catchpoint (int, char *);

extern void create_vfork_event_catchpoint (int, char *);

extern void create_exec_event_catchpoint (int, char *);

/* This function returns TRUE if ep is a catchpoint. */
extern int ep_is_catchpoint (struct breakpoint *);

/* This function returns TRUE if ep is a catchpoint of a
   shared library (aka dynamically-linked library) event,
   such as a library load or unload. */
extern int ep_is_shlib_catchpoint (struct breakpoint *);

extern struct breakpoint *set_breakpoint_sal (struct symtab_and_line);

/* Enable breakpoints and delete when hit.  Called with ARG == NULL
   deletes all breakpoints. */
extern void delete_command (char *arg, int from_tty);

/* Pull all H/W watchpoints from the target. Return non-zero if the
   remove fails. */
extern int remove_hw_watchpoints (void);

#endif /* !defined (BREAKPOINT_H) */
