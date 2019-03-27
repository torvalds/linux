/* Disassembly display.

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

#include "defs.h"
#include "symtab.h"
#include "breakpoint.h"
#include "frame.h"
#include "value.h"
#include "source.h"
#include "disasm.h"
#include "gdb_string.h"
#include "tui/tui.h"
#include "tui/tui-data.h"
#include "tui/tui-win.h"
#include "tui/tui-layout.h"
#include "tui/tui-winsource.h"
#include "tui/tui-stack.h"
#include "tui/tui-file.h"

#include "gdb_curses.h"

struct tui_asm_line 
{
  CORE_ADDR addr;
  char* addr_string;
  char* insn;
};

/* Function to set the disassembly window's content.
   Disassemble count lines starting at pc.
   Return address of the count'th instruction after pc.  */
static CORE_ADDR
tui_disassemble (struct tui_asm_line* asm_lines, CORE_ADDR pc, int count)
{
  struct ui_file *gdb_dis_out;

  /* now init the ui_file structure */
  gdb_dis_out = tui_sfileopen (256);

  /* Now construct each line */
  for (; count > 0; count--, asm_lines++)
    {
      if (asm_lines->addr_string)
        xfree (asm_lines->addr_string);
      if (asm_lines->insn)
        xfree (asm_lines->insn);
      
      print_address (pc, gdb_dis_out);
      asm_lines->addr = pc;
      asm_lines->addr_string = xstrdup (tui_file_get_strbuf (gdb_dis_out));

      ui_file_rewind (gdb_dis_out);

      pc = pc + gdb_print_insn (pc, gdb_dis_out);

      asm_lines->insn = xstrdup (tui_file_get_strbuf (gdb_dis_out));

      /* reset the buffer to empty */
      ui_file_rewind (gdb_dis_out);
    }
  ui_file_delete (gdb_dis_out);
  return pc;
}

/* Find the disassembly address that corresponds to FROM lines
   above or below the PC.  Variable sized instructions are taken
   into account by the algorithm.  */
static CORE_ADDR
tui_find_disassembly_address (CORE_ADDR pc, int from)
{
  CORE_ADDR new_low;
  int max_lines;
  int i;
  struct tui_asm_line* asm_lines;

  max_lines = (from > 0) ? from : - from;
  if (max_lines <= 1)
     return pc;

  asm_lines = (struct tui_asm_line*) alloca (sizeof (struct tui_asm_line)
                                         * max_lines);
  memset (asm_lines, 0, sizeof (struct tui_asm_line) * max_lines);

  new_low = pc;
  if (from > 0)
    {
      tui_disassemble (asm_lines, pc, max_lines);
      new_low = asm_lines[max_lines - 1].addr;
    }
  else
    {
      CORE_ADDR last_addr;
      int pos;
      struct minimal_symbol* msymbol;
              
      /* Find backward an address which is a symbol
         and for which disassembling from that address will fill
         completely the window.  */
      pos = max_lines - 1;
      do {
         new_low -= 1 * max_lines;
         msymbol = lookup_minimal_symbol_by_pc_section (new_low, 0);

         if (msymbol)
            new_low = SYMBOL_VALUE_ADDRESS (msymbol);
         else
            new_low += 1 * max_lines;

         tui_disassemble (asm_lines, new_low, max_lines);
         last_addr = asm_lines[pos].addr;
      } while (last_addr > pc && msymbol);

      /* Scan forward disassembling one instruction at a time
         until the last visible instruction of the window
         matches the pc.  We keep the disassembled instructions
         in the 'lines' window and shift it downward (increasing
         its addresses).  */
      if (last_addr < pc)
        do
          {
            CORE_ADDR next_addr;
                 
            pos++;
            if (pos >= max_lines)
              pos = 0;

            next_addr = tui_disassemble (&asm_lines[pos], last_addr, 1);

            /* If there are some problems while disassembling exit.  */
            if (next_addr <= last_addr)
              break;
            last_addr = next_addr;
          } while (last_addr <= pc);
      pos++;
      if (pos >= max_lines)
         pos = 0;
      new_low = asm_lines[pos].addr;
    }
  for (i = 0; i < max_lines; i++)
    {
      xfree (asm_lines[i].addr_string);
      xfree (asm_lines[i].insn);
    }
  return new_low;
}

/* Function to set the disassembly window's content.  */
enum tui_status
tui_set_disassem_content (CORE_ADDR pc)
{
  enum tui_status ret = TUI_FAILURE;
  int i;
  int offset = TUI_DISASM_WIN->detail.source_info.horizontal_offset;
  int line_width, max_lines;
  CORE_ADDR cur_pc;
  struct tui_gen_win_info * locator = tui_locator_win_info_ptr ();
  int tab_len = tui_default_tab_len ();
  struct tui_asm_line* asm_lines;
  int insn_pos;
  int addr_size, max_size;
  char* line;
  
  if (pc == 0)
    return TUI_FAILURE;

  ret = tui_alloc_source_buffer (TUI_DISASM_WIN);
  if (ret != TUI_SUCCESS)
    return ret;

  TUI_DISASM_WIN->detail.source_info.start_line_or_addr.addr = pc;
  cur_pc = (CORE_ADDR)
    (((struct tui_win_element *) locator->content[0])->which_element.locator.addr);

  max_lines = TUI_DISASM_WIN->generic.height - 2;	/* account for hilite */

  /* Get temporary table that will hold all strings (addr & insn).  */
  asm_lines = (struct tui_asm_line*) alloca (sizeof (struct tui_asm_line)
                                         * max_lines);
  memset (asm_lines, 0, sizeof (struct tui_asm_line) * max_lines);

  line_width = TUI_DISASM_WIN->generic.width - 1;

  tui_disassemble (asm_lines, pc, max_lines);

  /* See what is the maximum length of an address and of a line.  */
  addr_size = 0;
  max_size = 0;
  for (i = 0; i < max_lines; i++)
    {
      size_t len = strlen (asm_lines[i].addr_string);
      if (len > addr_size)
        addr_size = len;

      len = strlen (asm_lines[i].insn) + tab_len;
      if (len > max_size)
        max_size = len;
    }
  max_size += addr_size + tab_len;

  /* Allocate memory to create each line.  */
  line = (char*) alloca (max_size);
  insn_pos = (1 + (addr_size / tab_len)) * tab_len;

  /* Now construct each line */
  for (i = 0; i < max_lines; i++)
    {
      struct tui_win_element * element;
      struct tui_source_element* src;
      int cur_len;

      element = (struct tui_win_element *) TUI_DISASM_WIN->generic.content[i];
      src = &element->which_element.source;
      strcpy (line, asm_lines[i].addr_string);
      cur_len = strlen (line);

      /* Add spaces to make the instructions start on the same column */
      while (cur_len < insn_pos)
        {
          strcat (line, " ");
          cur_len++;
        }

      strcat (line, asm_lines[i].insn);

      /* Now copy the line taking the offset into account */
      if (strlen (line) > offset)
        strcpy (src->line, &line[offset]);
      else
        src->line[0] = '\0';

      src->line_or_addr.addr = asm_lines[i].addr;
      src->is_exec_point = asm_lines[i].addr == cur_pc;

      /* See whether there is a breakpoint installed.  */
      src->has_break = (!src->is_exec_point
                       && breakpoint_here_p (pc) != no_breakpoint_here);

      xfree (asm_lines[i].addr_string);
      xfree (asm_lines[i].insn);
    }
  TUI_DISASM_WIN->generic.content_size = i;
  return TUI_SUCCESS;
}


/* Function to display the disassembly window with disassembled code.   */
void
tui_show_disassem (CORE_ADDR start_addr)
{
  struct symtab *s = find_pc_symtab (start_addr);
  struct tui_win_info * win_with_focus = tui_win_with_focus ();
  union tui_line_or_address val;

  val.addr = start_addr;
  tui_add_win_to_layout (DISASSEM_WIN);
  tui_update_source_window (TUI_DISASM_WIN, s, val, FALSE);
  /*
     ** if the focus was in the src win, put it in the asm win, if the
     ** source view isn't split
   */
  if (tui_current_layout () != SRC_DISASSEM_COMMAND && win_with_focus == TUI_SRC_WIN)
    tui_set_win_focus_to (TUI_DISASM_WIN);

  return;
}


/* Function to display the disassembly window.   */
void
tui_show_disassem_and_update_source (CORE_ADDR start_addr)
{
  struct symtab_and_line sal;

  tui_show_disassem (start_addr);
  if (tui_current_layout () == SRC_DISASSEM_COMMAND)
    {
      union tui_line_or_address val;

      /*
         ** Update what is in the source window if it is displayed too,
         ** note that it follows what is in the disassembly window and visa-versa
       */
      sal = find_pc_line (start_addr, 0);
      val.line_no = sal.line;
      tui_update_source_window (TUI_SRC_WIN, sal.symtab, val, TRUE);
      if (sal.symtab)
	{
	  set_current_source_symtab_and_line (&sal);
	  tui_update_locator_filename (sal.symtab->filename);
	}
      else
	tui_update_locator_filename ("?");
    }

  return;
}

CORE_ADDR
tui_get_begin_asm_address (void)
{
  struct tui_gen_win_info * locator;
  struct tui_locator_element * element;
  CORE_ADDR addr;

  locator = tui_locator_win_info_ptr ();
  element = &((struct tui_win_element *) locator->content[0])->which_element.locator;

  if (element->addr == 0)
    {
      struct minimal_symbol *main_symbol;

      /* Find address of the start of program.
         Note: this should be language specific.  */
      main_symbol = lookup_minimal_symbol ("main", NULL, NULL);
      if (main_symbol == 0)
        main_symbol = lookup_minimal_symbol ("MAIN", NULL, NULL);
      if (main_symbol == 0)
        main_symbol = lookup_minimal_symbol ("_start", NULL, NULL);
      if (main_symbol)
        addr = SYMBOL_VALUE_ADDRESS (main_symbol);
      else
        addr = 0;
    }
  else				/* the target is executing */
    addr = element->addr;

  return addr;
}

/* Determine what the low address will be to display in the TUI's
   disassembly window.  This may or may not be the same as the
   low address input.  */
CORE_ADDR
tui_get_low_disassembly_address (CORE_ADDR low, CORE_ADDR pc)
{
  int pos;

  /* Determine where to start the disassembly so that the pc is about in the
     middle of the viewport.  */
  pos = tui_default_win_viewport_height (DISASSEM_WIN, DISASSEM_COMMAND) / 2;
  pc = tui_find_disassembly_address (pc, -pos);

  if (pc < low)
    pc = low;
  return pc;
}

/* Scroll the disassembly forward or backward vertically.  */
void
tui_vertical_disassem_scroll (enum tui_scroll_direction scroll_direction,
			      int num_to_scroll)
{
  if (TUI_DISASM_WIN->generic.content != NULL)
    {
      CORE_ADDR pc;
      tui_win_content content;
      struct symtab *s;
      union tui_line_or_address val;
      int max_lines, dir;
      struct symtab_and_line cursal = get_current_source_symtab_and_line ();

      content = (tui_win_content) TUI_DISASM_WIN->generic.content;
      if (cursal.symtab == (struct symtab *) NULL)
	s = find_pc_symtab (get_frame_pc (deprecated_selected_frame));
      else
	s = cursal.symtab;

      /* account for hilite */
      max_lines = TUI_DISASM_WIN->generic.height - 2;
      pc = content[0]->which_element.source.line_or_addr.addr;
      dir = (scroll_direction == FORWARD_SCROLL) ? max_lines : - max_lines;

      val.addr = tui_find_disassembly_address (pc, dir);
      tui_update_source_window_as_is (TUI_DISASM_WIN, s, val, FALSE);
    }
}
