/* TUI window generic functions.

   Copyright 1998, 1999, 2000, 2001, 2002, 2003, 2004 Free Software
   Foundation, Inc.

   Contributed by Hewlett-Packard Company.

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

/* This module contains procedures for handling tui window functions
   like resize, scrolling, scrolling, changing focus, etc.

   Author: Susan B. Macchia  */

#include "defs.h"
#include "command.h"
#include "symtab.h"
#include "breakpoint.h"
#include "frame.h"
#include "cli/cli-cmds.h"
#include "top.h"
#include "source.h"

#include "tui/tui.h"
#include "tui/tui-data.h"
#include "tui/tui-wingeneral.h"
#include "tui/tui-stack.h"
#include "tui/tui-regs.h"
#include "tui/tui-disasm.h"
#include "tui/tui-source.h"
#include "tui/tui-winsource.h"
#include "tui/tui-windata.h"

#include "gdb_curses.h"

#include "gdb_string.h"
#include <ctype.h>
#include "readline/readline.h"

/*******************************
** Static Local Decls
********************************/
static void make_visible_with_new_height (struct tui_win_info *);
static void make_invisible_and_set_new_height (struct tui_win_info *, int);
static enum tui_status tui_adjust_win_heights (struct tui_win_info *, int);
static int new_height_ok (struct tui_win_info *, int);
static void tui_set_tab_width_command (char *, int);
static void tui_refresh_all_command (char *, int);
static void tui_set_win_height_command (char *, int);
static void tui_xdb_set_win_height_command (char *, int);
static void tui_all_windows_info (char *, int);
static void tui_set_focus_command (char *, int);
static void tui_scroll_forward_command (char *, int);
static void tui_scroll_backward_command (char *, int);
static void tui_scroll_left_command (char *, int);
static void tui_scroll_right_command (char *, int);
static void parse_scrolling_args (char *, struct tui_win_info * *, int *);


/***************************************
** DEFINITIONS
***************************************/
#define WIN_HEIGHT_USAGE      "Usage: winheight <win_name> [+ | -] <#lines>\n"
#define XDBWIN_HEIGHT_USAGE   "Usage: w <#lines>\n"
#define FOCUS_USAGE           "Usage: focus {<win> | next | prev}\n"

/***************************************
** PUBLIC FUNCTIONS
***************************************/

#ifndef ACS_LRCORNER
#  define ACS_LRCORNER '+'
#endif
#ifndef ACS_LLCORNER
#  define ACS_LLCORNER '+'
#endif
#ifndef ACS_ULCORNER
#  define ACS_ULCORNER '+'
#endif
#ifndef ACS_URCORNER
#  define ACS_URCORNER '+'
#endif
#ifndef ACS_HLINE
#  define ACS_HLINE '-'
#endif
#ifndef ACS_VLINE
#  define ACS_VLINE '|'
#endif

/* Possible values for tui-border-kind variable.  */
static const char *tui_border_kind_enums[] = {
  "space",
  "ascii",
  "acs",
  NULL
};

/* Possible values for tui-border-mode and tui-active-border-mode.  */
static const char *tui_border_mode_enums[] = {
  "normal",
  "standout",
  "reverse",
  "half",
  "half-standout",
  "bold",
  "bold-standout",
  NULL
};

struct tui_translate
{
  const char *name;
  int value;
};

/* Translation table for border-mode variables.
   The list of values must be terminated by a NULL.
   After the NULL value, an entry defines the default.  */
struct tui_translate tui_border_mode_translate[] = {
  { "normal",		A_NORMAL },
  { "standout",		A_STANDOUT },
  { "reverse",		A_REVERSE },
  { "half",		A_DIM },
  { "half-standout",	A_DIM | A_STANDOUT },
  { "bold",		A_BOLD },
  { "bold-standout",	A_BOLD | A_STANDOUT },
  { 0, 0 },
  { "normal",		A_NORMAL }
};

/* Translation tables for border-kind, one for each border
   character (see wborder, border curses operations).
   -1 is used to indicate the ACS because ACS characters
   are determined at run time by curses (depends on terminal).  */
struct tui_translate tui_border_kind_translate_vline[] = {
  { "space",    ' ' },
  { "ascii",    '|' },
  { "acs",      -1 },
  { 0, 0 },
  { "ascii",    '|' }
};

struct tui_translate tui_border_kind_translate_hline[] = {
  { "space",    ' ' },
  { "ascii",    '-' },
  { "acs",      -1 },
  { 0, 0 },
  { "ascii",    '-' }
};

struct tui_translate tui_border_kind_translate_ulcorner[] = {
  { "space",    ' ' },
  { "ascii",    '+' },
  { "acs",      -1 },
  { 0, 0 },
  { "ascii",    '+' }
};

struct tui_translate tui_border_kind_translate_urcorner[] = {
  { "space",    ' ' },
  { "ascii",    '+' },
  { "acs",      -1 },
  { 0, 0 },
  { "ascii",    '+' }
};

struct tui_translate tui_border_kind_translate_llcorner[] = {
  { "space",    ' ' },
  { "ascii",    '+' },
  { "acs",      -1 },
  { 0, 0 },
  { "ascii",    '+' }
};

struct tui_translate tui_border_kind_translate_lrcorner[] = {
  { "space",    ' ' },
  { "ascii",    '+' },
  { "acs",      -1 },
  { 0, 0 },
  { "ascii",    '+' }
};


/* Tui configuration variables controlled with set/show command.  */
const char *tui_active_border_mode = "bold-standout";
const char *tui_border_mode = "normal";
const char *tui_border_kind = "acs";

/* Tui internal configuration variables.  These variables are
   updated by tui_update_variables to reflect the tui configuration
   variables.  */
chtype tui_border_vline;
chtype tui_border_hline;
chtype tui_border_ulcorner;
chtype tui_border_urcorner;
chtype tui_border_llcorner;
chtype tui_border_lrcorner;

int tui_border_attrs;
int tui_active_border_attrs;

/* Identify the item in the translation table.
   When the item is not recognized, use the default entry.  */
static struct tui_translate *
translate (const char *name, struct tui_translate *table)
{
  while (table->name)
    {
      if (name && strcmp (table->name, name) == 0)
        return table;
      table++;
    }

  /* Not found, return default entry.  */
  table++;
  return table;
}

/* Update the tui internal configuration according to gdb settings.
   Returns 1 if the configuration has changed and the screen should
   be redrawn.  */
int
tui_update_variables (void)
{
  int need_redraw = 0;
  struct tui_translate *entry;

  entry = translate (tui_border_mode, tui_border_mode_translate);
  if (tui_border_attrs != entry->value)
    {
      tui_border_attrs = entry->value;
      need_redraw = 1;
    }
  entry = translate (tui_active_border_mode, tui_border_mode_translate);
  if (tui_active_border_attrs != entry->value)
    {
      tui_active_border_attrs = entry->value;
      need_redraw = 1;
    }

  /* If one corner changes, all characters are changed.
     Only check the first one.  The ACS characters are determined at
     run time by curses terminal management.  */
  entry = translate (tui_border_kind, tui_border_kind_translate_lrcorner);
  if (tui_border_lrcorner != (chtype) entry->value)
    {
      tui_border_lrcorner = (entry->value < 0) ? ACS_LRCORNER : entry->value;
      need_redraw = 1;
    }
  entry = translate (tui_border_kind, tui_border_kind_translate_llcorner);
  tui_border_llcorner = (entry->value < 0) ? ACS_LLCORNER : entry->value;

  entry = translate (tui_border_kind, tui_border_kind_translate_ulcorner);
  tui_border_ulcorner = (entry->value < 0) ? ACS_ULCORNER : entry->value;

  entry = translate (tui_border_kind, tui_border_kind_translate_urcorner);
  tui_border_urcorner = (entry->value < 0) ? ACS_URCORNER : entry->value;

  entry = translate (tui_border_kind, tui_border_kind_translate_hline);
  tui_border_hline = (entry->value < 0) ? ACS_HLINE : entry->value;

  entry = translate (tui_border_kind, tui_border_kind_translate_vline);
  tui_border_vline = (entry->value < 0) ? ACS_VLINE : entry->value;

  return need_redraw;
}

static void
set_tui_cmd (char *args, int from_tty)
{
}

static void
show_tui_cmd (char *args, int from_tty)
{
}

static struct cmd_list_element *tuilist;

static void
tui_command (char *args, int from_tty)
{
  printf_unfiltered ("\"tui\" must be followed by the name of a "
                     "tui command.\n");
  help_list (tuilist, "tui ", -1, gdb_stdout);
}

struct cmd_list_element **
tui_get_cmd_list ()
{
  if (tuilist == 0)
    add_prefix_cmd ("tui", class_tui, tui_command,
                    "Text User Interface commands.",
                    &tuilist, "tui ", 0, &cmdlist);
  return &tuilist;
}

/* Function to initialize gdb commands, for tui window manipulation.  */
void
_initialize_tui_win (void)
{
  struct cmd_list_element *c;
  static struct cmd_list_element *tui_setlist;
  static struct cmd_list_element *tui_showlist;

  /* Define the classes of commands.
     They will appear in the help list in the reverse of this order.  */
  add_prefix_cmd ("tui", class_tui, set_tui_cmd,
                  "TUI configuration variables",
		  &tui_setlist, "set tui ",
		  0/*allow-unknown*/, &setlist);
  add_prefix_cmd ("tui", class_tui, show_tui_cmd,
                  "TUI configuration variables",
		  &tui_showlist, "show tui ",
		  0/*allow-unknown*/, &showlist);

  add_com ("refresh", class_tui, tui_refresh_all_command,
           "Refresh the terminal display.\n");
  if (xdb_commands)
    add_com_alias ("U", "refresh", class_tui, 0);
  add_com ("tabset", class_tui, tui_set_tab_width_command,
           "Set the width (in characters) of tab stops.\n\
Usage: tabset <n>\n");
  add_com ("winheight", class_tui, tui_set_win_height_command,
           "Set the height of a specified window.\n\
Usage: winheight <win_name> [+ | -] <#lines>\n\
Window names are:\n\
src  : the source window\n\
cmd  : the command window\n\
asm  : the disassembly window\n\
regs : the register display\n");
  add_com_alias ("wh", "winheight", class_tui, 0);
  add_info ("win", tui_all_windows_info,
            "List of all displayed windows.\n");
  add_com ("focus", class_tui, tui_set_focus_command,
           "Set focus to named window or next/prev window.\n\
Usage: focus {<win> | next | prev}\n\
Valid Window names are:\n\
src  : the source window\n\
asm  : the disassembly window\n\
regs : the register display\n\
cmd  : the command window\n");
  add_com_alias ("fs", "focus", class_tui, 0);
  add_com ("+", class_tui, tui_scroll_forward_command,
           "Scroll window forward.\nUsage: + [win] [n]\n");
  add_com ("-", class_tui, tui_scroll_backward_command,
           "Scroll window backward.\nUsage: - [win] [n]\n");
  add_com ("<", class_tui, tui_scroll_left_command,
           "Scroll window forward.\nUsage: < [win] [n]\n");
  add_com (">", class_tui, tui_scroll_right_command,
           "Scroll window backward.\nUsage: > [win] [n]\n");
  if (xdb_commands)
    add_com ("w", class_xdb, tui_xdb_set_win_height_command,
             "XDB compatibility command for setting the height of a command window.\n\
Usage: w <#lines>\n");

  /* Define the tui control variables.  */
  c = add_set_enum_cmd
    ("border-kind", no_class,
     tui_border_kind_enums, &tui_border_kind,
     "Set the kind of border for TUI windows.\n"
     "This variable controls the border of TUI windows:\n"
     "space           use a white space\n"
     "ascii           use ascii characters + - | for the border\n"
     "acs             use the Alternate Character Set\n",
     &tui_setlist);
  add_show_from_set (c, &tui_showlist);

  c = add_set_enum_cmd
    ("border-mode", no_class,
     tui_border_mode_enums, &tui_border_mode,
     "Set the attribute mode to use for the TUI window borders.\n"
     "This variable controls the attributes to use for the window borders:\n"
     "normal          normal display\n"
     "standout        use highlight mode of terminal\n"
     "reverse         use reverse video mode\n"
     "half            use half bright\n"
     "half-standout   use half bright and standout mode\n"
     "bold            use extra bright or bold\n"
     "bold-standout   use extra bright or bold with standout mode\n",
     &tui_setlist);
  add_show_from_set (c, &tui_showlist);

  c = add_set_enum_cmd
    ("active-border-mode", no_class,
     tui_border_mode_enums, &tui_active_border_mode,
     "Set the attribute mode to use for the active TUI window border.\n"
     "This variable controls the attributes to use for the active window border:\n"
     "normal          normal display\n"
     "standout        use highlight mode of terminal\n"
     "reverse         use reverse video mode\n"
     "half            use half bright\n"
     "half-standout   use half bright and standout mode\n"
     "bold            use extra bright or bold\n"
     "bold-standout   use extra bright or bold with standout mode\n",
     &tui_setlist);
  add_show_from_set (c, &tui_showlist);
}

/* Update gdb's knowledge of the terminal size.  */
void
tui_update_gdb_sizes (void)
{
  char cmd[50];
  int screenheight, screenwidth;

  rl_get_screen_size (&screenheight, &screenwidth);
  /* Set to TUI command window dimension or use readline values.  */
  sprintf (cmd, "set width %d",
           tui_active ? TUI_CMD_WIN->generic.width : screenwidth);
  execute_command (cmd, 0);
  sprintf (cmd, "set height %d",
           tui_active ? TUI_CMD_WIN->generic.height : screenheight);
  execute_command (cmd, 0);
}


/* Set the logical focus to win_info.    */
void
tui_set_win_focus_to (struct tui_win_info * win_info)
{
  if (win_info != NULL)
    {
      struct tui_win_info * win_with_focus = tui_win_with_focus ();

      if (win_with_focus != NULL
	  && win_with_focus->generic.type != CMD_WIN)
	tui_unhighlight_win (win_with_focus);
      tui_set_win_with_focus (win_info);
      if (win_info->generic.type != CMD_WIN)
	tui_highlight_win (win_info);
    }
}


void
tui_scroll_forward (struct tui_win_info * win_to_scroll, int num_to_scroll)
{
  if (win_to_scroll != TUI_CMD_WIN)
    {
      int _num_to_scroll = num_to_scroll;

      if (num_to_scroll == 0)
	_num_to_scroll = win_to_scroll->generic.height - 3;
      /*
         ** If we are scrolling the source or disassembly window, do a
         ** "psuedo" scroll since not all of the source is in memory,
         ** only what is in the viewport.  If win_to_scroll is the
         ** command window do nothing since the term should handle it.
       */
      if (win_to_scroll == TUI_SRC_WIN)
	tui_vertical_source_scroll (FORWARD_SCROLL, _num_to_scroll);
      else if (win_to_scroll == TUI_DISASM_WIN)
	tui_vertical_disassem_scroll (FORWARD_SCROLL, _num_to_scroll);
      else if (win_to_scroll == TUI_DATA_WIN)
	tui_vertical_data_scroll (FORWARD_SCROLL, _num_to_scroll);
    }
}

void
tui_scroll_backward (struct tui_win_info * win_to_scroll, int num_to_scroll)
{
  if (win_to_scroll != TUI_CMD_WIN)
    {
      int _num_to_scroll = num_to_scroll;

      if (num_to_scroll == 0)
	_num_to_scroll = win_to_scroll->generic.height - 3;
      /*
         ** If we are scrolling the source or disassembly window, do a
         ** "psuedo" scroll since not all of the source is in memory,
         ** only what is in the viewport.  If win_to_scroll is the
         ** command window do nothing since the term should handle it.
       */
      if (win_to_scroll == TUI_SRC_WIN)
	tui_vertical_source_scroll (BACKWARD_SCROLL, _num_to_scroll);
      else if (win_to_scroll == TUI_DISASM_WIN)
	tui_vertical_disassem_scroll (BACKWARD_SCROLL, _num_to_scroll);
      else if (win_to_scroll == TUI_DATA_WIN)
	tui_vertical_data_scroll (BACKWARD_SCROLL, _num_to_scroll);
    }
}


void
tui_scroll_left (struct tui_win_info * win_to_scroll, int num_to_scroll)
{
  if (win_to_scroll != TUI_CMD_WIN)
    {
      int _num_to_scroll = num_to_scroll;

      if (_num_to_scroll == 0)
	_num_to_scroll = 1;
      /*
         ** If we are scrolling the source or disassembly window, do a
         ** "psuedo" scroll since not all of the source is in memory,
         ** only what is in the viewport. If win_to_scroll is the
         ** command window do nothing since the term should handle it.
       */
      if (win_to_scroll == TUI_SRC_WIN || win_to_scroll == TUI_DISASM_WIN)
	tui_horizontal_source_scroll (win_to_scroll, LEFT_SCROLL, _num_to_scroll);
    }
}


void
tui_scroll_right (struct tui_win_info * win_to_scroll, int num_to_scroll)
{
  if (win_to_scroll != TUI_CMD_WIN)
    {
      int _num_to_scroll = num_to_scroll;

      if (_num_to_scroll == 0)
	_num_to_scroll = 1;
      /*
         ** If we are scrolling the source or disassembly window, do a
         ** "psuedo" scroll since not all of the source is in memory,
         ** only what is in the viewport. If win_to_scroll is the
         ** command window do nothing since the term should handle it.
       */
      if (win_to_scroll == TUI_SRC_WIN || win_to_scroll == TUI_DISASM_WIN)
	tui_horizontal_source_scroll (win_to_scroll, RIGHT_SCROLL, _num_to_scroll);
    }
}


/* Scroll a window.  Arguments are passed through a va_list.    */
void
tui_scroll (enum tui_scroll_direction direction,
	    struct tui_win_info * win_to_scroll,
	    int num_to_scroll)
{
  switch (direction)
    {
    case FORWARD_SCROLL:
      tui_scroll_forward (win_to_scroll, num_to_scroll);
      break;
    case BACKWARD_SCROLL:
      tui_scroll_backward (win_to_scroll, num_to_scroll);
      break;
    case LEFT_SCROLL:
      tui_scroll_left (win_to_scroll, num_to_scroll);
      break;
    case RIGHT_SCROLL:
      tui_scroll_right (win_to_scroll, num_to_scroll);
      break;
    default:
      break;
    }
}


void
tui_refresh_all_win (void)
{
  enum tui_win_type type;

  clearok (curscr, TRUE);
  tui_refresh_all (tui_win_list);
  for (type = SRC_WIN; type < MAX_MAJOR_WINDOWS; type++)
    {
      if (tui_win_list[type] && tui_win_list[type]->generic.is_visible)
	{
	  switch (type)
	    {
	    case SRC_WIN:
	    case DISASSEM_WIN:
	      tui_show_source_content (tui_win_list[type]);
	      tui_check_and_display_highlight_if_needed (tui_win_list[type]);
	      tui_erase_exec_info_content (tui_win_list[type]);
	      tui_update_exec_info (tui_win_list[type]);
	      break;
	    case DATA_WIN:
	      tui_refresh_data_win ();
	      break;
	    default:
	      break;
	    }
	}
    }
  tui_show_locator_content ();
}


/* Resize all the windows based on the the terminal size.  This
   function gets called from within the readline sinwinch handler.  */
void
tui_resize_all (void)
{
  int height_diff, width_diff;
  int screenheight, screenwidth;

  rl_get_screen_size (&screenheight, &screenwidth);
  width_diff = screenwidth - tui_term_width ();
  height_diff = screenheight - tui_term_height ();
  if (height_diff || width_diff)
    {
      enum tui_layout_type cur_layout = tui_current_layout ();
      struct tui_win_info * win_with_focus = tui_win_with_focus ();
      struct tui_win_info *first_win;
      struct tui_win_info *second_win;
      struct tui_gen_win_info * locator = tui_locator_win_info_ptr ();
      enum tui_win_type win_type;
      int new_height, split_diff, cmd_split_diff, num_wins_displayed = 2;

#ifdef HAVE_RESIZE_TERM
      resize_term (screenheight, screenwidth);
#endif      
      /* turn keypad off while we resize */
      if (win_with_focus != TUI_CMD_WIN)
	keypad (TUI_CMD_WIN->generic.handle, FALSE);
      tui_update_gdb_sizes ();
      tui_set_term_height_to (screenheight);
      tui_set_term_width_to (screenwidth);
      if (cur_layout == SRC_DISASSEM_COMMAND ||
	cur_layout == SRC_DATA_COMMAND || cur_layout == DISASSEM_DATA_COMMAND)
	num_wins_displayed++;
      split_diff = height_diff / num_wins_displayed;
      cmd_split_diff = split_diff;
      if (height_diff % num_wins_displayed)
	{
	  if (height_diff < 0)
	    cmd_split_diff--;
	  else
	    cmd_split_diff++;
	}
      /* now adjust each window */
      clear ();
      refresh ();
      switch (cur_layout)
	{
	case SRC_COMMAND:
	case DISASSEM_COMMAND:
	  first_win = (struct tui_win_info *) (tui_source_windows ())->list[0];
	  first_win->generic.width += width_diff;
	  locator->width += width_diff;
	  /* check for invalid heights */
	  if (height_diff == 0)
	    new_height = first_win->generic.height;
	  else if ((first_win->generic.height + split_diff) >=
		   (screenheight - MIN_CMD_WIN_HEIGHT - 1))
	    new_height = screenheight - MIN_CMD_WIN_HEIGHT - 1;
	  else if ((first_win->generic.height + split_diff) <= 0)
	    new_height = MIN_WIN_HEIGHT;
	  else
	    new_height = first_win->generic.height + split_diff;

	  make_invisible_and_set_new_height (first_win, new_height);
	  TUI_CMD_WIN->generic.origin.y = locator->origin.y + 1;
	  TUI_CMD_WIN->generic.width += width_diff;
	  new_height = screenheight - TUI_CMD_WIN->generic.origin.y;
	  make_invisible_and_set_new_height (TUI_CMD_WIN, new_height);
	  make_visible_with_new_height (first_win);
	  make_visible_with_new_height (TUI_CMD_WIN);
	  if (first_win->generic.content_size <= 0)
	    tui_erase_source_content (first_win, EMPTY_SOURCE_PROMPT);
	  break;
	default:
	  if (cur_layout == SRC_DISASSEM_COMMAND)
	    {
	      first_win = TUI_SRC_WIN;
	      first_win->generic.width += width_diff;
	      second_win = TUI_DISASM_WIN;
	      second_win->generic.width += width_diff;
	    }
	  else
	    {
	      first_win = TUI_DATA_WIN;
	      first_win->generic.width += width_diff;
	      second_win = (struct tui_win_info *) (tui_source_windows ())->list[0];
	      second_win->generic.width += width_diff;
	    }
	  /* Change the first window's height/width */
	  /* check for invalid heights */
	  if (height_diff == 0)
	    new_height = first_win->generic.height;
	  else if ((first_win->generic.height +
		    second_win->generic.height + (split_diff * 2)) >=
		   (screenheight - MIN_CMD_WIN_HEIGHT - 1))
	    new_height = (screenheight - MIN_CMD_WIN_HEIGHT - 1) / 2;
	  else if ((first_win->generic.height + split_diff) <= 0)
	    new_height = MIN_WIN_HEIGHT;
	  else
	    new_height = first_win->generic.height + split_diff;
	  make_invisible_and_set_new_height (first_win, new_height);

	  locator->width += width_diff;

	  /* Change the second window's height/width */
	  /* check for invalid heights */
	  if (height_diff == 0)
	    new_height = second_win->generic.height;
	  else if ((first_win->generic.height +
		    second_win->generic.height + (split_diff * 2)) >=
		   (screenheight - MIN_CMD_WIN_HEIGHT - 1))
	    {
	      new_height = screenheight - MIN_CMD_WIN_HEIGHT - 1;
	      if (new_height % 2)
		new_height = (new_height / 2) + 1;
	      else
		new_height /= 2;
	    }
	  else if ((second_win->generic.height + split_diff) <= 0)
	    new_height = MIN_WIN_HEIGHT;
	  else
	    new_height = second_win->generic.height + split_diff;
	  second_win->generic.origin.y = first_win->generic.height - 1;
	  make_invisible_and_set_new_height (second_win, new_height);

	  /* Change the command window's height/width */
	  TUI_CMD_WIN->generic.origin.y = locator->origin.y + 1;
	  make_invisible_and_set_new_height (
			     TUI_CMD_WIN, TUI_CMD_WIN->generic.height + cmd_split_diff);
	  make_visible_with_new_height (first_win);
	  make_visible_with_new_height (second_win);
	  make_visible_with_new_height (TUI_CMD_WIN);
	  if (first_win->generic.content_size <= 0)
	    tui_erase_source_content (first_win, EMPTY_SOURCE_PROMPT);
	  if (second_win->generic.content_size <= 0)
	    tui_erase_source_content (second_win, EMPTY_SOURCE_PROMPT);
	  break;
	}
      /*
         ** Now remove all invisible windows, and their content so that they get
         ** created again when called for with the new size
       */
      for (win_type = SRC_WIN; (win_type < MAX_MAJOR_WINDOWS); win_type++)
	{
	  if (win_type != CMD_WIN && (tui_win_list[win_type] != NULL)
	      && !tui_win_list[win_type]->generic.is_visible)
	    {
	      tui_free_window (tui_win_list[win_type]);
	      tui_win_list[win_type] = (struct tui_win_info *) NULL;
	    }
	}
      tui_set_win_resized_to (TRUE);
      /* turn keypad back on, unless focus is in the command window */
      if (win_with_focus != TUI_CMD_WIN)
	keypad (TUI_CMD_WIN->generic.handle, TRUE);
    }
}


/* SIGWINCH signal handler for the tui.  This signal handler is always
   called, even when the readline package clears signals because it is
   set as the old_sigwinch() (TUI only).  */
void
tui_sigwinch_handler (int signal)
{
  /*
     ** Say that a resize was done so that the readline can do it
     ** later when appropriate.
   */
  tui_set_win_resized_to (TRUE);
}



/*************************
** STATIC LOCAL FUNCTIONS
**************************/


static void
tui_scroll_forward_command (char *arg, int from_tty)
{
  int num_to_scroll = 1;
  struct tui_win_info * win_to_scroll;

  /* Make sure the curses mode is enabled.  */
  tui_enable ();
  if (arg == (char *) NULL)
    parse_scrolling_args (arg, &win_to_scroll, (int *) NULL);
  else
    parse_scrolling_args (arg, &win_to_scroll, &num_to_scroll);
  tui_scroll (FORWARD_SCROLL, win_to_scroll, num_to_scroll);
}


static void
tui_scroll_backward_command (char *arg, int from_tty)
{
  int num_to_scroll = 1;
  struct tui_win_info * win_to_scroll;

  /* Make sure the curses mode is enabled.  */
  tui_enable ();
  if (arg == (char *) NULL)
    parse_scrolling_args (arg, &win_to_scroll, (int *) NULL);
  else
    parse_scrolling_args (arg, &win_to_scroll, &num_to_scroll);
  tui_scroll (BACKWARD_SCROLL, win_to_scroll, num_to_scroll);
}


static void
tui_scroll_left_command (char *arg, int from_tty)
{
  int num_to_scroll;
  struct tui_win_info * win_to_scroll;

  /* Make sure the curses mode is enabled.  */
  tui_enable ();
  parse_scrolling_args (arg, &win_to_scroll, &num_to_scroll);
  tui_scroll (LEFT_SCROLL, win_to_scroll, num_to_scroll);
}


static void
tui_scroll_right_command (char *arg, int from_tty)
{
  int num_to_scroll;
  struct tui_win_info * win_to_scroll;

  /* Make sure the curses mode is enabled.  */
  tui_enable ();
  parse_scrolling_args (arg, &win_to_scroll, &num_to_scroll);
  tui_scroll (RIGHT_SCROLL, win_to_scroll, num_to_scroll);
}


/* Set focus to the window named by 'arg'.  */
static void
tui_set_focus (char *arg, int from_tty)
{
  if (arg != (char *) NULL)
    {
      char *buf_ptr = (char *) xstrdup (arg);
      int i;
      struct tui_win_info * win_info = (struct tui_win_info *) NULL;

      for (i = 0; (i < strlen (buf_ptr)); i++)
	buf_ptr[i] = toupper (arg[i]);

      if (subset_compare (buf_ptr, "NEXT"))
	win_info = tui_next_win (tui_win_with_focus ());
      else if (subset_compare (buf_ptr, "PREV"))
	win_info = tui_prev_win (tui_win_with_focus ());
      else
	win_info = tui_partial_win_by_name (buf_ptr);

      if (win_info == (struct tui_win_info *) NULL || !win_info->generic.is_visible)
	warning ("Invalid window specified. \n\
The window name specified must be valid and visible.\n");
      else
	{
	  tui_set_win_focus_to (win_info);
	  keypad (TUI_CMD_WIN->generic.handle, (win_info != TUI_CMD_WIN));
	}

      if (TUI_DATA_WIN && TUI_DATA_WIN->generic.is_visible)
	tui_refresh_data_win ();
      xfree (buf_ptr);
      printf_filtered ("Focus set to %s window.\n",
		       tui_win_name ((struct tui_gen_win_info *) tui_win_with_focus ()));
    }
  else
    warning ("Incorrect Number of Arguments.\n%s", FOCUS_USAGE);
}

static void
tui_set_focus_command (char *arg, int from_tty)
{
  /* Make sure the curses mode is enabled.  */
  tui_enable ();
  tui_set_focus (arg, from_tty);
}


static void
tui_all_windows_info (char *arg, int from_tty)
{
  enum tui_win_type type;
  struct tui_win_info * win_with_focus = tui_win_with_focus ();

  for (type = SRC_WIN; (type < MAX_MAJOR_WINDOWS); type++)
    if (tui_win_list[type] && tui_win_list[type]->generic.is_visible)
      {
	if (win_with_focus == tui_win_list[type])
	  printf_filtered ("        %s\t(%d lines)  <has focus>\n",
			   tui_win_name (&tui_win_list[type]->generic),
			   tui_win_list[type]->generic.height);
	else
	  printf_filtered ("        %s\t(%d lines)\n",
			   tui_win_name (&tui_win_list[type]->generic),
			   tui_win_list[type]->generic.height);
      }
}


static void
tui_refresh_all_command (char *arg, int from_tty)
{
  /* Make sure the curses mode is enabled.  */
  tui_enable ();

  tui_refresh_all_win ();
}


/* Set the height of the specified window.   */
static void
tui_set_tab_width_command (char *arg, int from_tty)
{
  /* Make sure the curses mode is enabled.  */
  tui_enable ();
  if (arg != (char *) NULL)
    {
      int ts;

      ts = atoi (arg);
      if (ts > 0)
	tui_set_default_tab_len (ts);
      else
	warning ("Tab widths greater than 0 must be specified.\n");
    }
}


/* Set the height of the specified window.   */
static void
tui_set_win_height (char *arg, int from_tty)
{
  /* Make sure the curses mode is enabled.  */
  tui_enable ();
  if (arg != (char *) NULL)
    {
      char *buf = xstrdup (arg);
      char *buf_ptr = buf;
      char *wname = (char *) NULL;
      int new_height, i;
      struct tui_win_info * win_info;

      wname = buf_ptr;
      buf_ptr = strchr (buf_ptr, ' ');
      if (buf_ptr != (char *) NULL)
	{
	  *buf_ptr = (char) 0;

	  /*
	     ** Validate the window name
	   */
	  for (i = 0; i < strlen (wname); i++)
	    wname[i] = toupper (wname[i]);
	  win_info = tui_partial_win_by_name (wname);

	  if (win_info == (struct tui_win_info *) NULL || !win_info->generic.is_visible)
	    warning ("Invalid window specified. \n\
The window name specified must be valid and visible.\n");
	  else
	    {
	      /* Process the size */
	      while (*(++buf_ptr) == ' ')
		;

	      if (*buf_ptr != (char) 0)
		{
		  int negate = FALSE;
		  int fixed_size = TRUE;
		  int input_no;;

		  if (*buf_ptr == '+' || *buf_ptr == '-')
		    {
		      if (*buf_ptr == '-')
			negate = TRUE;
		      fixed_size = FALSE;
		      buf_ptr++;
		    }
		  input_no = atoi (buf_ptr);
		  if (input_no > 0)
		    {
		      if (negate)
			input_no *= (-1);
		      if (fixed_size)
			new_height = input_no;
		      else
			new_height = win_info->generic.height + input_no;
		      /*
		         ** Now change the window's height, and adjust all
		         ** other windows around it
		       */
		      if (tui_adjust_win_heights (win_info,
						new_height) == TUI_FAILURE)
			warning ("Invalid window height specified.\n%s",
				 WIN_HEIGHT_USAGE);
		      else
                        tui_update_gdb_sizes ();
		    }
		  else
		    warning ("Invalid window height specified.\n%s",
			     WIN_HEIGHT_USAGE);
		}
	    }
	}
      else
	printf_filtered (WIN_HEIGHT_USAGE);

      if (buf != (char *) NULL)
	xfree (buf);
    }
  else
    printf_filtered (WIN_HEIGHT_USAGE);
}

/* Set the height of the specified window, with va_list.    */
static void
tui_set_win_height_command (char *arg, int from_tty)
{
  /* Make sure the curses mode is enabled.  */
  tui_enable ();
  tui_set_win_height (arg, from_tty);
}


/* XDB Compatibility command for setting the window height.  This will
   increase or decrease the command window by the specified amount.  */
static void
tui_xdb_set_win_height (char *arg, int from_tty)
{
  /* Make sure the curses mode is enabled.  */
  tui_enable ();
  if (arg != (char *) NULL)
    {
      int input_no = atoi (arg);

      if (input_no > 0)
	{			/* Add 1 for the locator */
	  int new_height = tui_term_height () - (input_no + 1);

	  if (!new_height_ok (tui_win_list[CMD_WIN], new_height) ||
	      tui_adjust_win_heights (tui_win_list[CMD_WIN],
				    new_height) == TUI_FAILURE)
	    warning ("Invalid window height specified.\n%s",
		     XDBWIN_HEIGHT_USAGE);
	}
      else
	warning ("Invalid window height specified.\n%s",
		 XDBWIN_HEIGHT_USAGE);
    }
  else
    warning ("Invalid window height specified.\n%s", XDBWIN_HEIGHT_USAGE);
}

/* Set the height of the specified window, with va_list.  */
static void
tui_xdb_set_win_height_command (char *arg, int from_tty)
{
  tui_xdb_set_win_height (arg, from_tty);
}


/* Function to adjust all window heights around the primary.   */
static enum tui_status
tui_adjust_win_heights (struct tui_win_info * primary_win_info, int new_height)
{
  enum tui_status status = TUI_FAILURE;

  if (new_height_ok (primary_win_info, new_height))
    {
      status = TUI_SUCCESS;
      if (new_height != primary_win_info->generic.height)
	{
	  int diff;
	  struct tui_win_info * win_info;
	  struct tui_gen_win_info * locator = tui_locator_win_info_ptr ();
	  enum tui_layout_type cur_layout = tui_current_layout ();

	  diff = (new_height - primary_win_info->generic.height) * (-1);
	  if (cur_layout == SRC_COMMAND || cur_layout == DISASSEM_COMMAND)
	    {
	      struct tui_win_info * src_win_info;

	      make_invisible_and_set_new_height (primary_win_info, new_height);
	      if (primary_win_info->generic.type == CMD_WIN)
		{
		  win_info = (struct tui_win_info *) (tui_source_windows ())->list[0];
		  src_win_info = win_info;
		}
	      else
		{
		  win_info = tui_win_list[CMD_WIN];
		  src_win_info = primary_win_info;
		}
	      make_invisible_and_set_new_height (win_info,
					     win_info->generic.height + diff);
	      TUI_CMD_WIN->generic.origin.y = locator->origin.y + 1;
	      make_visible_with_new_height (win_info);
	      make_visible_with_new_height (primary_win_info);
	      if (src_win_info->generic.content_size <= 0)
		tui_erase_source_content (src_win_info, EMPTY_SOURCE_PROMPT);
	    }
	  else
	    {
	      struct tui_win_info *first_win;
	      struct tui_win_info *second_win;

	      if (cur_layout == SRC_DISASSEM_COMMAND)
		{
		  first_win = TUI_SRC_WIN;
		  second_win = TUI_DISASM_WIN;
		}
	      else
		{
		  first_win = TUI_DATA_WIN;
		  second_win = (struct tui_win_info *) (tui_source_windows ())->list[0];
		}
	      if (primary_win_info == TUI_CMD_WIN)
		{		/*
				   ** Split the change in height accross the 1st & 2nd windows
				   ** adjusting them as well.
				 */
		  int first_split_diff = diff / 2;	/* subtract the locator */
		  int second_split_diff = first_split_diff;

		  if (diff % 2)
		    {
		      if (first_win->generic.height >
			  second_win->generic.height)
			if (diff < 0)
			  first_split_diff--;
			else
			  first_split_diff++;
		      else
			{
			  if (diff < 0)
			    second_split_diff--;
			  else
			    second_split_diff++;
			}
		    }
		  /* make sure that the minimum hieghts are honored */
		  while ((first_win->generic.height + first_split_diff) < 3)
		    {
		      first_split_diff++;
		      second_split_diff--;
		    }
		  while ((second_win->generic.height + second_split_diff) < 3)
		    {
		      second_split_diff++;
		      first_split_diff--;
		    }
		  make_invisible_and_set_new_height (
						  first_win,
				 first_win->generic.height + first_split_diff);
		  second_win->generic.origin.y = first_win->generic.height - 1;
		  make_invisible_and_set_new_height (
		    second_win, second_win->generic.height + second_split_diff);
		  TUI_CMD_WIN->generic.origin.y = locator->origin.y + 1;
		  make_invisible_and_set_new_height (TUI_CMD_WIN, new_height);
		}
	      else
		{
		  if ((TUI_CMD_WIN->generic.height + diff) < 1)
		    {		/*
				   ** If there is no way to increase the command window
				   ** take real estate from the 1st or 2nd window.
				 */
		      if ((TUI_CMD_WIN->generic.height + diff) < 1)
			{
			  int i;
			  for (i = TUI_CMD_WIN->generic.height + diff;
			       (i < 1); i++)
			    if (primary_win_info == first_win)
			      second_win->generic.height--;
			    else
			      first_win->generic.height--;
			}
		    }
		  if (primary_win_info == first_win)
		    make_invisible_and_set_new_height (first_win, new_height);
		  else
		    make_invisible_and_set_new_height (
						    first_win,
						  first_win->generic.height);
		  second_win->generic.origin.y = first_win->generic.height - 1;
		  if (primary_win_info == second_win)
		    make_invisible_and_set_new_height (second_win, new_height);
		  else
		    make_invisible_and_set_new_height (
				      second_win, second_win->generic.height);
		  TUI_CMD_WIN->generic.origin.y = locator->origin.y + 1;
		  if ((TUI_CMD_WIN->generic.height + diff) < 1)
		    make_invisible_and_set_new_height (TUI_CMD_WIN, 1);
		  else
		    make_invisible_and_set_new_height (
				     TUI_CMD_WIN, TUI_CMD_WIN->generic.height + diff);
		}
	      make_visible_with_new_height (TUI_CMD_WIN);
	      make_visible_with_new_height (second_win);
	      make_visible_with_new_height (first_win);
	      if (first_win->generic.content_size <= 0)
		tui_erase_source_content (first_win, EMPTY_SOURCE_PROMPT);
	      if (second_win->generic.content_size <= 0)
		tui_erase_source_content (second_win, EMPTY_SOURCE_PROMPT);
	    }
	}
    }

  return status;
}


/* Function make the target window (and auxillary windows associated
   with the targer) invisible, and set the new height and location.  */
static void
make_invisible_and_set_new_height (struct tui_win_info * win_info, int height)
{
  int i;
  struct tui_gen_win_info * gen_win_info;

  tui_make_invisible (&win_info->generic);
  win_info->generic.height = height;
  if (height > 1)
    win_info->generic.viewport_height = height - 1;
  else
    win_info->generic.viewport_height = height;
  if (win_info != TUI_CMD_WIN)
    win_info->generic.viewport_height--;

  /* Now deal with the auxillary windows associated with win_info */
  switch (win_info->generic.type)
    {
    case SRC_WIN:
    case DISASSEM_WIN:
      gen_win_info = win_info->detail.source_info.execution_info;
      tui_make_invisible (gen_win_info);
      gen_win_info->height = height;
      gen_win_info->origin.y = win_info->generic.origin.y;
      if (height > 1)
	gen_win_info->viewport_height = height - 1;
      else
	gen_win_info->viewport_height = height;
      if (win_info != TUI_CMD_WIN)
	gen_win_info->viewport_height--;

      if (tui_win_has_locator (win_info))
	{
	  gen_win_info = tui_locator_win_info_ptr ();
	  tui_make_invisible (gen_win_info);
	  gen_win_info->origin.y = win_info->generic.origin.y + height;
	}
      break;
    case DATA_WIN:
      /* delete all data item windows */
      for (i = 0; i < win_info->generic.content_size; i++)
	{
	  gen_win_info = (struct tui_gen_win_info *) & ((struct tui_win_element *)
		      win_info->generic.content[i])->which_element.data_window;
	  tui_delete_win (gen_win_info->handle);
	  gen_win_info->handle = (WINDOW *) NULL;
	}
      break;
    default:
      break;
    }
}


/* Function to make the windows with new heights visible.  This means
   re-creating the windows' content since the window had to be
   destroyed to be made invisible.  */
static void
make_visible_with_new_height (struct tui_win_info * win_info)
{
  struct symtab *s;

  tui_make_visible (&win_info->generic);
  tui_check_and_display_highlight_if_needed (win_info);
  switch (win_info->generic.type)
    {
    case SRC_WIN:
    case DISASSEM_WIN:
      tui_free_win_content (win_info->detail.source_info.execution_info);
      tui_make_visible (win_info->detail.source_info.execution_info);
      if (win_info->generic.content != NULL)
	{
	  union tui_line_or_address line_or_addr;
	  struct symtab_and_line cursal
	    = get_current_source_symtab_and_line ();

	  if (win_info->generic.type == SRC_WIN)
	    line_or_addr.line_no =
	      win_info->detail.source_info.start_line_or_addr.line_no;
	  else
	    line_or_addr.addr =
	      win_info->detail.source_info.start_line_or_addr.addr;
	  tui_free_win_content (&win_info->generic);
	  tui_update_source_window (win_info, cursal.symtab, line_or_addr, TRUE);
	}
      else if (deprecated_selected_frame != (struct frame_info *) NULL)
	{
	  union tui_line_or_address line;
	  struct symtab_and_line cursal = get_current_source_symtab_and_line ();


	  s = find_pc_symtab (get_frame_pc (deprecated_selected_frame));
	  if (win_info->generic.type == SRC_WIN)
	    line.line_no = cursal.line;
	  else
	    {
	      find_line_pc (s, cursal.line, &line.addr);
	    }
	  tui_update_source_window (win_info, s, line, TRUE);
	}
      if (tui_win_has_locator (win_info))
	{
	  tui_make_visible (tui_locator_win_info_ptr ());
	  tui_show_locator_content ();
	}
      break;
    case DATA_WIN:
      tui_display_all_data ();
      break;
    case CMD_WIN:
      win_info->detail.command_info.cur_line = 0;
      win_info->detail.command_info.curch = 0;
      wmove (win_info->generic.handle,
	     win_info->detail.command_info.cur_line,
	     win_info->detail.command_info.curch);
      break;
    default:
      break;
    }
}


static int
new_height_ok (struct tui_win_info * primary_win_info, int new_height)
{
  int ok = (new_height < tui_term_height ());

  if (ok)
    {
      int diff;
      enum tui_layout_type cur_layout = tui_current_layout ();

      diff = (new_height - primary_win_info->generic.height) * (-1);
      if (cur_layout == SRC_COMMAND || cur_layout == DISASSEM_COMMAND)
	{
	  ok = ((primary_win_info->generic.type == CMD_WIN &&
		 new_height <= (tui_term_height () - 4) &&
		 new_height >= MIN_CMD_WIN_HEIGHT) ||
		(primary_win_info->generic.type != CMD_WIN &&
		 new_height <= (tui_term_height () - 2) &&
		 new_height >= MIN_WIN_HEIGHT));
	  if (ok)
	    {			/* check the total height */
	      struct tui_win_info * win_info;

	      if (primary_win_info == TUI_CMD_WIN)
		win_info = (struct tui_win_info *) (tui_source_windows ())->list[0];
	      else
		win_info = TUI_CMD_WIN;
	      ok = ((new_height +
		     (win_info->generic.height + diff)) <= tui_term_height ());
	    }
	}
      else
	{
	  int cur_total_height, total_height, min_height = 0;
	  struct tui_win_info *first_win;
	  struct tui_win_info *second_win;

	  if (cur_layout == SRC_DISASSEM_COMMAND)
	    {
	      first_win = TUI_SRC_WIN;
	      second_win = TUI_DISASM_WIN;
	    }
	  else
	    {
	      first_win = TUI_DATA_WIN;
	      second_win = (struct tui_win_info *) (tui_source_windows ())->list[0];
	    }
	  /*
	     ** We could simply add all the heights to obtain the same result
	     ** but below is more explicit since we subtract 1 for the
	     ** line that the first and second windows share, and add one
	     ** for the locator.
	   */
	  total_height = cur_total_height =
	    (first_win->generic.height + second_win->generic.height - 1)
	    + TUI_CMD_WIN->generic.height + 1 /*locator */ ;
	  if (primary_win_info == TUI_CMD_WIN)
	    {
	      /* locator included since first & second win share a line */
	      ok = ((first_win->generic.height +
		     second_win->generic.height + diff) >=
		    (MIN_WIN_HEIGHT * 2) &&
		    new_height >= MIN_CMD_WIN_HEIGHT);
	      if (ok)
		{
		  total_height = new_height + (first_win->generic.height +
					  second_win->generic.height + diff);
		  min_height = MIN_CMD_WIN_HEIGHT;
		}
	    }
	  else
	    {
	      min_height = MIN_WIN_HEIGHT;
	      /*
	         ** First see if we can increase/decrease the command
	         ** window.  And make sure that the command window is
	         ** at least 1 line
	       */
	      ok = ((TUI_CMD_WIN->generic.height + diff) > 0);
	      if (!ok)
		{		/*
				   ** Looks like we have to increase/decrease one of
				   ** the other windows
				 */
		  if (primary_win_info == first_win)
		    ok = (second_win->generic.height + diff) >= min_height;
		  else
		    ok = (first_win->generic.height + diff) >= min_height;
		}
	      if (ok)
		{
		  if (primary_win_info == first_win)
		    total_height = new_height +
		      second_win->generic.height +
		      TUI_CMD_WIN->generic.height + diff;
		  else
		    total_height = new_height +
		      first_win->generic.height +
		      TUI_CMD_WIN->generic.height + diff;
		}
	    }
	  /*
	     ** Now make sure that the proposed total height doesn't exceed
	     ** the old total height.
	   */
	  if (ok)
	    ok = (new_height >= min_height && total_height <= cur_total_height);
	}
    }

  return ok;
}


static void
parse_scrolling_args (char *arg, struct tui_win_info * * win_to_scroll,
		      int *num_to_scroll)
{
  if (num_to_scroll)
    *num_to_scroll = 0;
  *win_to_scroll = tui_win_with_focus ();

  /*
     ** First set up the default window to scroll, in case there is no
     ** window name arg
   */
  if (arg != (char *) NULL)
    {
      char *buf, *buf_ptr;

      /* process the number of lines to scroll */
      buf = buf_ptr = xstrdup (arg);
      if (isdigit (*buf_ptr))
	{
	  char *num_str;

	  num_str = buf_ptr;
	  buf_ptr = strchr (buf_ptr, ' ');
	  if (buf_ptr != (char *) NULL)
	    {
	      *buf_ptr = (char) 0;
	      if (num_to_scroll)
		*num_to_scroll = atoi (num_str);
	      buf_ptr++;
	    }
	  else if (num_to_scroll)
	    *num_to_scroll = atoi (num_str);
	}

      /* process the window name if one is specified */
      if (buf_ptr != (char *) NULL)
	{
	  char *wname;
	  int i;

	  if (*buf_ptr == ' ')
	    while (*(++buf_ptr) == ' ')
	      ;

	  if (*buf_ptr != (char) 0)
	    wname = buf_ptr;
	  else
	    wname = "?";
	  
	  /* Validate the window name */
	  for (i = 0; i < strlen (wname); i++)
	    wname[i] = toupper (wname[i]);
	  *win_to_scroll = tui_partial_win_by_name (wname);

	  if (*win_to_scroll == (struct tui_win_info *) NULL ||
	      !(*win_to_scroll)->generic.is_visible)
	    warning ("Invalid window specified. \n\
The window name specified must be valid and visible.\n");
	  else if (*win_to_scroll == TUI_CMD_WIN)
	    *win_to_scroll = (struct tui_win_info *) (tui_source_windows ())->list[0];
	}
      xfree (buf);
    }
}
