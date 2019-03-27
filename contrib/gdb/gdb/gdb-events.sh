#!/bin/sh

# User Interface Events.
# Copyright 1999, 2000, 2001, 2002, 2004 Free Software Foundation, Inc.
#
# Contributed by Cygnus Solutions.
#
# This file is part of GDB.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

#
# What happens next:
#

# The gdb-events.h/gdb-events.c files this script generates are commited
# and published.

# Any UI module that is installing events is changed so that the
# events are installed using the ``set_gdb_events()'' and
# ``gdb_event_hooks()'' interfaces.  There could prove to be an issue
# here with respect to annotate.  We might need to accomodate a hook
# stack that allows several ui blocks to install their own events.

# Each of the variable events (as currently generated) is converted
# to either a straight function call or a function call with a
# predicate.


IFS=:

read="class returntype function formal actual attrib"

function_list ()
{
  # category:
  #        # -> disable
  #        * -> compatibility - pointer variable that is initialized
  #             by set_gdb_events().
  #        ? -> Predicate and function proper.
  #        f -> always call (must have a void returntype)
  # return-type
  # name
  # formal argument list
  # actual argument list
  # attributes
  # description
  cat <<EOF |
f:void:breakpoint_create:int b:b
f:void:breakpoint_delete:int b:b
f:void:breakpoint_modify:int b:b
f:void:tracepoint_create:int number:number
f:void:tracepoint_delete:int number:number
f:void:tracepoint_modify:int number:number
f:void:architecture_changed:void
f:void:target_changed:void
f:void:selected_frame_level_changed:int level:level
f:void:selected_thread_changed:int thread_num:thread_num
#*:void:annotate_starting_hook:void
#*:void:annotate_stopped_hook:void
#*:void:annotate_signalled_hook:void
#*:void:annotate_signal_hook:void
#*:void:annotate_exited_hook:void
##*:void:print_register_hook:int
##*:CORE_ADDR:find_toc_address_hook:CORE_ADDR
##*:void:sparc_print_register_hook:int regno:regno
#*:void:target_resume_hook:void
#*:void:target_wait_loop_hook:void
#*:void:init_gdb_hook:char *argv0:argv0
#*:void:command_loop_hook:void
#*:void:fputs_unfiltered_hook:const char *linebuff,struct ui_file *stream:linebuff, stream
#*:void:print_frame_info_listing_hook:struct symtab *s, int line, int stopline, int noerror:s, line, stopline, noerror
#*:int:query_hook:const char *query, va_list args:query, args
#*:void:warning_hook:const char *string, va_list args:string, args
#*:void:target_output_hook:char *b:b
#*:void:interactive_hook:void
#*:void:registers_changed_hook:void
#*:void:readline_begin_hook:char *format, ...:format
#*:char *:readline_hook:char *prompt:prompt
#*:void:readline_end_hook:void
#*:int:target_wait_hook:int pid, struct target_waitstatus *status:pid, status
#*:void:call_command_hook:struct cmd_list_element *c, char *cmd, int from_tty:c, cmd, from_tty
#*:NORETURN void:error_hook:void:: ATTR_NORETURN
#*:void:error_begin_hook:void
##*:int:target_architecture_hook:const struct bfd_arch_info *
#*:void:exec_file_display_hook:char *filename:filename
#*:void:file_changed_hook:char *filename:filename
##*:void:specify_exec_file_hook:
#*:int:gdb_load_progress_hook:char *section, unsigned long num:section, num
#*:void:pre_add_symbol_hook:char *name:name
#*:void:post_add_symbol_hook:void
#*:void:selected_frame_level_changed_hook:int level:level
#*:int:gdb_loop_hook:int signo:signo
##*:void:solib_create_inferior_hook:void
##*:void:xcoff_relocate_symtab_hook:unsigned int
EOF
  grep -v '^#'
}

copyright ()
{
  cat <<EOF
/* User Interface Events.

   Copyright 1999, 2001, 2002 Free Software Foundation, Inc.

   Contributed by Cygnus Solutions.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* Work in progress */

/* This file was created with the aid of \`\`gdb-events.sh''.

   The bourn shell script \`\`gdb-events.sh'' creates the files
   \`\`new-gdb-events.c'' and \`\`new-gdb-events.h and then compares
   them against the existing \`\`gdb-events.[hc]''.  Any differences
   found being reported.

   If editing this file, please also run gdb-events.sh and merge any
   changes into that script. Conversely, when making sweeping changes
   to this file, modifying gdb-events.sh and using its output may
   prove easier. */

EOF
}

#
# The .h file
#

exec > new-gdb-events.h
copyright
cat <<EOF

#ifndef GDB_EVENTS_H
#define GDB_EVENTS_H

#ifndef WITH_GDB_EVENTS
#define WITH_GDB_EVENTS 1
#endif
EOF

# pointer declarations
echo ""
echo ""
cat <<EOF
/* COMPAT: pointer variables for old, unconverted events.
   A call to set_gdb_events() will automatically update these. */
EOF
echo ""
function_list | while eval read $read
do
  case "${class}" in
    "*" )
	echo "extern ${returntype} (*${function}_event) (${formal})${attrib};"
	;;
  esac
done

# function typedef's
echo ""
echo ""
cat <<EOF
/* Type definition of all hook functions.
   Recommended pratice is to first declare each hook function using
   the below ftype and then define it. */
EOF
echo ""
function_list | while eval read $read
do
  echo "typedef ${returntype} (gdb_events_${function}_ftype) (${formal});"
done

# gdb_events object
echo ""
echo ""
cat <<EOF
/* gdb-events: object. */
EOF
echo ""
echo "struct gdb_events"
echo "  {"
function_list | while eval read $read
do
  echo "    gdb_events_${function}_ftype *${function}${attrib};"
done
echo "  };"

# function declarations
echo ""
echo ""
cat <<EOF
/* Interface into events functions.
   Where a *_p() predicate is present, it must be called before
   calling the hook proper. */
EOF
function_list | while eval read $read
do
  case "${class}" in
    "*" ) continue ;;
    "?" )
	echo "extern int ${function}_p (void);"
        echo "extern ${returntype} ${function}_event (${formal})${attrib};"
	;;
    "f" )
	echo "extern ${returntype} ${function}_event (${formal})${attrib};"
	;;
  esac
done

# function macros
echo ""
echo ""
cat <<EOF
/* When GDB_EVENTS are not being used, completely disable them. */
EOF
echo ""
echo "#if !WITH_GDB_EVENTS"
function_list | while eval read $read
do
  case "${class}" in
    "*" ) continue ;;
    "?" )
	echo "#define ${function}_event_p() 0"
	echo "#define ${function}_event(${actual}) 0"
	;;
    "f" )
	echo "#define ${function}_event(${actual}) 0"
	;;
  esac
done
echo "#endif"

# our set function
cat <<EOF

/* Install custom gdb-events hooks. */
extern struct gdb_events *set_gdb_event_hooks (struct gdb_events *vector);

/* Deliver any pending events. */
extern void gdb_events_deliver (struct gdb_events *vector);

/* Clear event handlers */
extern void clear_gdb_event_hooks (void);

#if !WITH_GDB_EVENTS
#define set_gdb_events(x) 0
#define set_gdb_event_hooks(x) 0
#define gdb_events_deliver(x) 0
#endif
EOF

# close it off
echo ""
echo "#endif"
exec 1>&2
#../move-if-change new-gdb-events.h gdb-events.h
if test -r gdb-events.h
then
  diff -c gdb-events.h new-gdb-events.h
  if [ $? = 1 ]
  then
    echo "gdb-events.h changed? cp new-gdb-events.h gdb-events.h" 1>&2
  fi
else
  echo "File missing? mv new-gdb-events.h gdb-events.h" 1>&2
fi



#
# C file
#

exec > new-gdb-events.c
copyright
cat <<EOF

#include "defs.h"
#include "gdb-events.h"
#include "gdbcmd.h"

#if WITH_GDB_EVENTS
static struct gdb_events null_event_hooks;
static struct gdb_events queue_event_hooks;
static struct gdb_events *current_event_hooks = &null_event_hooks;
#endif

int gdb_events_debug;
EOF

# global pointer variables - always have this
#echo ""
#function_list | while eval read $read
#do
#  case "${class}" in
#    "*" )
#	echo "${returntype} (*${function}_event) (${formal})${attrib} = 0;"
#	;;
#  esac
#done

# function bodies
echo ""
echo "#if WITH_GDB_EVENTS"
function_list | while eval read $read
do
  case "${class}" in
    "*" ) continue ;;
    "?" )
cat <<EOF

int
${function}_event_p (${formal})
{
  return current_event_hooks->${function};
}

${returntype}
${function}_event (${formal})
{
  return current_events->${function} (${actual});
}
EOF
	;;
     "f" )
cat <<EOF

void
${function}_event (${formal})
{
  if (gdb_events_debug)
    fprintf_unfiltered (gdb_stdlog, "${function}_event\n");
  if (!current_event_hooks->${function})
    return;
  current_event_hooks->${function} (${actual});
}
EOF
	;;
  esac
done
echo ""
echo "#endif"

# Set hooks function
echo ""
cat <<EOF
#if WITH_GDB_EVENTS
struct gdb_events *
set_gdb_event_hooks (struct gdb_events *vector)
{
  struct gdb_events *old_events = current_event_hooks;
  if (vector == NULL)
    current_event_hooks = &queue_event_hooks;
  else
    current_event_hooks = vector;
  return old_events;
EOF
function_list | while eval read $read
do
  case "${class}" in
    "*" )
      echo "  ${function}_event = hooks->${function};"
      ;;
  esac
done
cat <<EOF
}
#endif
EOF

# Clear hooks function
echo ""
cat <<EOF
#if WITH_GDB_EVENTS
void
clear_gdb_event_hooks (void)
{
  set_gdb_event_hooks (&null_event_hooks);
}
#endif
EOF

# event type
echo ""
cat <<EOF
enum gdb_event
{
EOF
function_list | while eval read $read
do
  case "${class}" in
    "f" )
      echo "  ${function},"
      ;;
  esac
done
cat <<EOF
  nr_gdb_events
};
EOF

# event data
echo ""
function_list | while eval read $read
do
  case "${class}" in
    "f" )
      if test ${actual}
      then
        echo "struct ${function}"
        echo "  {"
        echo "    `echo ${formal} | tr '[,]' '[;]'`;"
        echo "  };"
        echo ""
      fi
      ;;
  esac
done

# event queue
cat <<EOF
struct event
  {
    enum gdb_event type;
    struct event *next;
    union
      {
EOF
function_list | while eval read $read
do
  case "${class}" in
    "f" )
      if test ${actual}
      then
        echo "        struct ${function} ${function};"
      fi
      ;;
  esac
done
cat <<EOF
      }
    data;
  };
struct event *pending_events;
struct event *delivering_events;
EOF

# append
echo ""
cat <<EOF
static void
append (struct event *new_event)
{
  struct event **event = &pending_events;
  while ((*event) != NULL)
    event = &((*event)->next);
  (*event) = new_event;
  (*event)->next = NULL;
}
EOF

# schedule a given event
function_list | while eval read $read
do
  case "${class}" in
    "f" )
      echo ""
      echo "static void"
      echo "queue_${function} (${formal})"
      echo "{"
      echo "  struct event *event = XMALLOC (struct event);"
      echo "  event->type = ${function};"
      for arg in `echo ${actual} | tr '[,]' '[:]' | tr -d '[ ]'`; do
        echo "  event->data.${function}.${arg} = ${arg};"
      done
      echo "  append (event);"
      echo "}"
      ;;
  esac
done

# deliver
echo ""
cat <<EOF
void
gdb_events_deliver (struct gdb_events *vector)
{
  /* Just zap any events left around from last time. */
  while (delivering_events != NULL)
    {
      struct event *event = delivering_events;
      delivering_events = event->next;
      xfree (event);
    }
  /* Process any pending events.  Because one of the deliveries could
     bail out we move everything off of the pending queue onto an
     in-progress queue where it can, later, be cleaned up if
     necessary. */
  delivering_events = pending_events;
  pending_events = NULL;
  while (delivering_events != NULL)
    {
      struct event *event = delivering_events;
      switch (event->type)
        {
EOF
function_list | while eval read $read
do
  case "${class}" in
    "f" )
      echo "        case ${function}:"
      if test ${actual}
      then
        echo "          vector->${function}"
        sep="            ("
        ass=""
        for arg in `echo ${actual} | tr '[,]' '[:]' | tr -d '[ ]'`; do
          ass="${ass}${sep}event->data.${function}.${arg}"
	  sep=",
               "
        done
        echo "${ass});"
      else
        echo "          vector->${function} ();"
      fi
      echo "          break;"
      ;;
  esac
done
cat <<EOF
        }
      delivering_events = event->next;
      xfree (event);
    }
}
EOF

# Finally the initialization
echo ""
cat <<EOF
void _initialize_gdb_events (void);
void
_initialize_gdb_events (void)
{
  struct cmd_list_element *c;
#if WITH_GDB_EVENTS
EOF
function_list | while eval read $read
do
  case "${class}" in
    "f" )
      echo "  queue_event_hooks.${function} = queue_${function};"
      ;;
  esac
done
cat <<EOF
#endif

  c = add_set_cmd ("eventdebug", class_maintenance, var_zinteger,
		   (char *) (&gdb_events_debug), "Set event debugging.\n\\
When non-zero, event/notify debugging is enabled.", &setlist);
  deprecate_cmd (c, "set debug event");
  deprecate_cmd (add_show_from_set (c, &showlist), "show debug event");

  add_show_from_set (add_set_cmd ("event",
                                  class_maintenance,
                                  var_zinteger,
                                  (char *) (&gdb_events_debug),
                                  "Set event debugging.\n\\
When non-zero, event/notify debugging is enabled.", &setdebuglist),
		     &showdebuglist);
}
EOF

# close things off
exec 1>&2
#../move-if-change new-gdb-events.c gdb-events.c
# Replace any leading spaces with tabs
sed < new-gdb-events.c > tmp-gdb-events.c \
    -e 's/\(	\)*        /\1	/g'
mv tmp-gdb-events.c new-gdb-events.c
# Move if changed?
if test -r gdb-events.c
then
  diff -c gdb-events.c new-gdb-events.c
  if [ $? = 1 ]
  then
    echo "gdb-events.c changed? cp new-gdb-events.c gdb-events.c" 1>&2
  fi
else
  echo "File missing? mv new-gdb-events.c gdb-events.c" 1>&2
fi
