/* Handle lists of commands, their decoding and documentation, for GDB.

   Copyright 1986, 1989, 1990, 1991, 1998, 2000, 2001, 2002 Free
   Software Foundation, Inc.

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
#include <ctype.h>
#include "gdb_regex.h"
#include "gdb_string.h"

#include "ui-out.h"

#include "cli/cli-cmds.h"
#include "cli/cli-decode.h"

#ifdef TUI
#include "tui/tui.h"		/* For tui_active et.al.   */
#endif

#include "gdb_assert.h"

/* Prototypes for local functions */

static void undef_cmd_error (char *, char *);

static struct cmd_list_element *find_cmd (char *command,
					  int len,
					  struct cmd_list_element *clist,
					  int ignore_help_classes,
					  int *nfound);

static void help_all (struct ui_file *stream);

/* Set the callback function for the specified command.  For each both
   the commands callback and func() are set.  The latter set to a
   bounce function (unless cfunc / sfunc is NULL that is).  */

static void
do_cfunc (struct cmd_list_element *c, char *args, int from_tty)
{
  c->function.cfunc (args, from_tty); /* Ok.  */
}

void
set_cmd_cfunc (struct cmd_list_element *cmd, cmd_cfunc_ftype *cfunc)
{
  if (cfunc == NULL)
    cmd->func = NULL;
  else
    cmd->func = do_cfunc;
  cmd->function.cfunc = cfunc; /* Ok.  */
}

static void
do_sfunc (struct cmd_list_element *c, char *args, int from_tty)
{
  c->function.sfunc (args, from_tty, c); /* Ok.  */
}

void
set_cmd_sfunc (struct cmd_list_element *cmd, cmd_sfunc_ftype *sfunc)
{
  if (sfunc == NULL)
    cmd->func = NULL;
  else
    cmd->func = do_sfunc;
  cmd->function.sfunc = sfunc; /* Ok.  */
}

int
cmd_cfunc_eq (struct cmd_list_element *cmd,
	      void (*cfunc) (char *args, int from_tty))
{
  return cmd->func == do_cfunc && cmd->function.cfunc == cfunc;
}

void
set_cmd_context (struct cmd_list_element *cmd, void *context)
{
  cmd->context = context;
}

void *
get_cmd_context (struct cmd_list_element *cmd)
{
  return cmd->context;
}

enum cmd_types
cmd_type (struct cmd_list_element *cmd)
{
  return cmd->type;
}

void
set_cmd_completer (struct cmd_list_element *cmd,
		   char **(*completer) (char *text, char *word))
{
  cmd->completer = completer; /* Ok.  */
}


/* Add element named NAME.
   CLASS is the top level category into which commands are broken down
   for "help" purposes.
   FUN should be the function to execute the command;
   it will get a character string as argument, with leading
   and trailing blanks already eliminated.

   DOC is a documentation string for the command.
   Its first line should be a complete sentence.
   It should start with ? for a command that is an abbreviation
   or with * for a command that most users don't need to know about.

   Add this command to command list *LIST.  

   Returns a pointer to the added command (not necessarily the head 
   of *LIST). */

struct cmd_list_element *
add_cmd (char *name, enum command_class class, void (*fun) (char *, int),
	 char *doc, struct cmd_list_element **list)
{
  struct cmd_list_element *c
  = (struct cmd_list_element *) xmalloc (sizeof (struct cmd_list_element));
  struct cmd_list_element *p;

  delete_cmd (name, list);

  if (*list == NULL || strcmp ((*list)->name, name) >= 0)
    {
      c->next = *list;
      *list = c;
    }
  else
    {
      p = *list;
      while (p->next && strcmp (p->next->name, name) <= 0)
	{
	  p = p->next;
	}
      c->next = p->next;
      p->next = c;
    }

  c->name = name;
  c->class = class;
  set_cmd_cfunc (c, fun);
  set_cmd_context (c, NULL);
  c->doc = doc;
  c->flags = 0;
  c->replacement = NULL;
  c->pre_show_hook = NULL;
  c->hook_pre  = NULL;
  c->hook_post = NULL;
  c->hook_in = 0;
  c->prefixlist = NULL;
  c->prefixname = NULL;
  c->allow_unknown = 0;
  c->abbrev_flag = 0;
  set_cmd_completer (c, make_symbol_completion_list);
  c->type = not_set_cmd;
  c->var = NULL;
  c->var_type = var_boolean;
  c->enums = NULL;
  c->user_commands = NULL;
  c->hookee_pre = NULL;
  c->hookee_post = NULL;
  c->cmd_pointer = NULL;

  return c;
}

/* Deprecates a command CMD.
   REPLACEMENT is the name of the command which should be used in place
   of this command, or NULL if no such command exists.

   This function does not check to see if command REPLACEMENT exists
   since gdb may not have gotten around to adding REPLACEMENT when this
   function is called.

   Returns a pointer to the deprecated command.  */

struct cmd_list_element *
deprecate_cmd (struct cmd_list_element *cmd, char *replacement)
{
  cmd->flags |= (CMD_DEPRECATED | DEPRECATED_WARN_USER);

  if (replacement != NULL)
    cmd->replacement = replacement;
  else
    cmd->replacement = NULL;

  return cmd;
}

struct cmd_list_element *
add_alias_cmd (char *name, char *oldname, enum command_class class,
	       int abbrev_flag, struct cmd_list_element **list)
{
  /* Must do this since lookup_cmd tries to side-effect its first arg */
  char *copied_name;
  struct cmd_list_element *old;
  struct cmd_list_element *c;
  copied_name = (char *) alloca (strlen (oldname) + 1);
  strcpy (copied_name, oldname);
  old = lookup_cmd (&copied_name, *list, "", 1, 1);

  if (old == 0)
    {
      delete_cmd (name, list);
      return 0;
    }

  c = add_cmd (name, class, NULL, old->doc, list);
  /* NOTE: Both FUNC and all the FUNCTIONs need to be copied.  */
  c->func = old->func;
  c->function = old->function;
  c->prefixlist = old->prefixlist;
  c->prefixname = old->prefixname;
  c->allow_unknown = old->allow_unknown;
  c->abbrev_flag = abbrev_flag;
  c->cmd_pointer = old;
  return c;
}

/* Like add_cmd but adds an element for a command prefix:
   a name that should be followed by a subcommand to be looked up
   in another command list.  PREFIXLIST should be the address
   of the variable containing that list.  */

struct cmd_list_element *
add_prefix_cmd (char *name, enum command_class class, void (*fun) (char *, int),
		char *doc, struct cmd_list_element **prefixlist,
		char *prefixname, int allow_unknown,
		struct cmd_list_element **list)
{
  struct cmd_list_element *c = add_cmd (name, class, fun, doc, list);
  c->prefixlist = prefixlist;
  c->prefixname = prefixname;
  c->allow_unknown = allow_unknown;
  return c;
}

/* Like add_prefix_cmd but sets the abbrev_flag on the new command. */

struct cmd_list_element *
add_abbrev_prefix_cmd (char *name, enum command_class class,
		       void (*fun) (char *, int), char *doc,
		       struct cmd_list_element **prefixlist, char *prefixname,
		       int allow_unknown, struct cmd_list_element **list)
{
  struct cmd_list_element *c = add_cmd (name, class, fun, doc, list);
  c->prefixlist = prefixlist;
  c->prefixname = prefixname;
  c->allow_unknown = allow_unknown;
  c->abbrev_flag = 1;
  return c;
}

/* This is an empty "cfunc".  */
void
not_just_help_class_command (char *args, int from_tty)
{
}

/* This is an empty "sfunc".  */
static void empty_sfunc (char *, int, struct cmd_list_element *);

static void
empty_sfunc (char *args, int from_tty, struct cmd_list_element *c)
{
}

/* Add element named NAME to command list LIST (the list for set/show
   or some sublist thereof).
   TYPE is set_cmd or show_cmd.
   CLASS is as in add_cmd.
   VAR_TYPE is the kind of thing we are setting.
   VAR is address of the variable being controlled by this command.
   DOC is the documentation string.  */

static struct cmd_list_element *
add_set_or_show_cmd (char *name,
		     enum cmd_types type,
		     enum command_class class,
		     var_types var_type,
		     void *var,
		     char *doc,
		     struct cmd_list_element **list)
{
  struct cmd_list_element *c = add_cmd (name, class, NULL, doc, list);
  gdb_assert (type == set_cmd || type == show_cmd);
  c->type = type;
  c->var_type = var_type;
  c->var = var;
  /* This needs to be something besides NULL so that this isn't
     treated as a help class.  */
  set_cmd_sfunc (c, empty_sfunc);
  return c;
}

/* Add element named NAME to both the command SET_LIST and SHOW_LIST.
   CLASS is as in add_cmd.  VAR_TYPE is the kind of thing we are
   setting.  VAR is address of the variable being controlled by this
   command.  SET_FUNC and SHOW_FUNC are the callback functions (if
   non-NULL).  SET_DOC and SHOW_DOC are the documentation strings.
   SET_RESULT and SHOW_RESULT, if not NULL, are set to the resulting
   command structures.  */

void
add_setshow_cmd_full (char *name,
		      enum command_class class,
		      var_types var_type, void *var,
		      char *set_doc, char *show_doc,
		      cmd_sfunc_ftype *set_func, cmd_sfunc_ftype *show_func,
		      struct cmd_list_element **set_list,
		      struct cmd_list_element **show_list,
		      struct cmd_list_element **set_result,
		      struct cmd_list_element **show_result)
{
  struct cmd_list_element *set;
  struct cmd_list_element *show;
  set = add_set_or_show_cmd (name, set_cmd, class, var_type, var,
			     set_doc, set_list);
  if (set_func != NULL)
    set_cmd_sfunc (set, set_func);
  show = add_set_or_show_cmd (name, show_cmd, class, var_type, var,
			      show_doc, show_list);
  if (show_func != NULL)
    set_cmd_sfunc (show, show_func);

  if (set_result != NULL)
    *set_result = set;
  if (show_result != NULL)
    *show_result = show;
}

/* Add element named NAME to both the command SET_LIST and SHOW_LIST.
   CLASS is as in add_cmd.  VAR_TYPE is the kind of thing we are
   setting.  VAR is address of the variable being controlled by this
   command.  SET_FUNC and SHOW_FUNC are the callback functions (if
   non-NULL).  SET_DOC and SHOW_DOC are the documentation strings.  */

void
add_setshow_cmd (char *name,
		 enum command_class class,
		 var_types var_type, void *var,
		 char *set_doc, char *show_doc,
		 cmd_sfunc_ftype *set_func, cmd_sfunc_ftype *show_func,
		 struct cmd_list_element **set_list,
		 struct cmd_list_element **show_list)
{
  add_setshow_cmd_full (name, class, var_type, var, set_doc, show_doc,
			set_func, show_func, set_list, show_list,
			NULL, NULL);
}

struct cmd_list_element *
add_set_cmd (char *name,
	     enum command_class class,
	     var_types var_type,
	     void *var,
	     char *doc,
	     struct cmd_list_element **list)
{
  return add_set_or_show_cmd (name, set_cmd, class, var_type, var, doc, list);
}

/* Add element named NAME to command list LIST (the list for set
   or some sublist thereof).
   CLASS is as in add_cmd.
   ENUMLIST is a list of strings which may follow NAME.
   VAR is address of the variable which will contain the matching string
   (from ENUMLIST).
   DOC is the documentation string.  */

struct cmd_list_element *
add_set_enum_cmd (char *name,
		  enum command_class class,
		  const char *enumlist[],
		  const char **var,
		  char *doc,
		  struct cmd_list_element **list)
{
  struct cmd_list_element *c
  = add_set_cmd (name, class, var_enum, var, doc, list);
  c->enums = enumlist;

  return c;
}

/* Add an auto-boolean command named NAME to both the set and show
   command list lists.  CLASS is as in add_cmd.  VAR is address of the
   variable which will contain the value.  DOC is the documentation
   string.  FUNC is the corresponding callback.  */
void
add_setshow_auto_boolean_cmd (char *name,
			      enum command_class class,
			      enum auto_boolean *var,
			      char *set_doc, char *show_doc,
			      cmd_sfunc_ftype *set_func,
			      cmd_sfunc_ftype *show_func,
			      struct cmd_list_element **set_list,
			      struct cmd_list_element **show_list)
{
  static const char *auto_boolean_enums[] = { "on", "off", "auto", NULL };
  struct cmd_list_element *c;
  add_setshow_cmd_full (name, class, var_auto_boolean, var,
			set_doc, show_doc, set_func, show_func,
			set_list, show_list,
			&c, NULL);
  c->enums = auto_boolean_enums;
}

/* Add element named NAME to both the set and show command LISTs (the
   list for set/show or some sublist thereof).  CLASS is as in
   add_cmd.  VAR is address of the variable which will contain the
   value.  SET_DOC and SHOW_DOR are the documentation strings.  */
void
add_setshow_boolean_cmd (char *name,
			 enum command_class class,
			 int *var, char *set_doc, char *show_doc,
			 cmd_sfunc_ftype *set_func,
			 cmd_sfunc_ftype *show_func,
			 struct cmd_list_element **set_list,
			 struct cmd_list_element **show_list)
{
  static const char *boolean_enums[] = { "on", "off", NULL };
  struct cmd_list_element *c;
  add_setshow_cmd_full (name, class, var_boolean, var,
			set_doc, show_doc,
			set_func, show_func,
			set_list, show_list,
			&c, NULL);
  c->enums = boolean_enums;
}

/* Add element named NAME to both the set and show command LISTs (the
   list for set/show or some sublist thereof).  CLASS is as in
   add_cmd.  VAR is address of the variable which will contain the
   value.  SET_DOC and SHOW_DOR are the documentation strings.  */
void
add_setshow_uinteger_cmd (char *name,
			  enum command_class class,
			  unsigned int *var, char *set_doc, char *show_doc,
			  cmd_sfunc_ftype *set_func,
			  cmd_sfunc_ftype *show_func,
			  struct cmd_list_element **set_list,
			  struct cmd_list_element **show_list)
{
  add_setshow_cmd_full (name, class, var_uinteger, var,
			set_doc, show_doc,
			set_func, show_func,
			set_list, show_list,
			NULL, NULL);
}

/* Where SETCMD has already been added, add the corresponding show
   command to LIST and return a pointer to the added command (not
   necessarily the head of LIST).  */
/* NOTE: cagney/2002-03-17: The original version of add_show_from_set
   used memcpy() to clone `set' into `show'.  This meant that in
   addition to all the needed fields (var, name, et.al.) some
   unnecessary fields were copied (namely the callback function).  The
   function explictly copies relevant fields.  For a `set' and `show'
   command to share the same callback, the caller must set both
   explicitly.  */
struct cmd_list_element *
add_show_from_set (struct cmd_list_element *setcmd,
		   struct cmd_list_element **list)
{
  char *doc;
  const static char setstring[] = "Set ";

  /* Create a doc string by replacing "Set " at the start of the
     `set'' command's doco with "Show ".  */
  gdb_assert (strncmp (setcmd->doc, setstring, sizeof (setstring) - 1) == 0);
  doc = concat ("Show ", setcmd->doc + sizeof (setstring) - 1, NULL);

  /* Insert the basic command.  */
  return add_set_or_show_cmd (setcmd->name, show_cmd, setcmd->class,
			      setcmd->var_type, setcmd->var, doc, list);
}

/* Remove the command named NAME from the command list.  */

void
delete_cmd (char *name, struct cmd_list_element **list)
{
  struct cmd_list_element *c;
  struct cmd_list_element *p;

  while (*list && strcmp ((*list)->name, name) == 0)
    {
      if ((*list)->hookee_pre)
      (*list)->hookee_pre->hook_pre = 0;   /* Hook slips out of its mouth */
      if ((*list)->hookee_post)
      (*list)->hookee_post->hook_post = 0; /* Hook slips out of its bottom  */
      p = (*list)->next;
      xfree (* list);
      *list = p;
    }

  if (*list)
    for (c = *list; c->next;)
      {
	if (strcmp (c->next->name, name) == 0)
	  {
          if (c->next->hookee_pre)
            c->next->hookee_pre->hook_pre = 0; /* hooked cmd gets away.  */
          if (c->next->hookee_post)
            c->next->hookee_post->hook_post = 0; /* remove post hook */
                                               /* :( no fishing metaphore */
	    p = c->next->next;
	    xfree (c->next);
	    c->next = p;
	  }
	else
	  c = c->next;
      }
}

/* Shorthands to the commands above. */

/* Add an element to the list of info subcommands.  */

struct cmd_list_element *
add_info (char *name, void (*fun) (char *, int), char *doc)
{
  return add_cmd (name, no_class, fun, doc, &infolist);
}

/* Add an alias to the list of info subcommands.  */

struct cmd_list_element *
add_info_alias (char *name, char *oldname, int abbrev_flag)
{
  return add_alias_cmd (name, oldname, 0, abbrev_flag, &infolist);
}

/* Add an element to the list of commands.  */

struct cmd_list_element *
add_com (char *name, enum command_class class, void (*fun) (char *, int),
	 char *doc)
{
  return add_cmd (name, class, fun, doc, &cmdlist);
}

/* Add an alias or abbreviation command to the list of commands.  */

struct cmd_list_element *
add_com_alias (char *name, char *oldname, enum command_class class,
	       int abbrev_flag)
{
  return add_alias_cmd (name, oldname, class, abbrev_flag, &cmdlist);
}

/* Recursively walk the commandlist structures, and print out the
   documentation of commands that match our regex in either their
   name, or their documentation.
*/
void 
apropos_cmd (struct ui_file *stream, struct cmd_list_element *commandlist,
			 struct re_pattern_buffer *regex, char *prefix)
{
  struct cmd_list_element *c;
  int returnvalue=1; /*Needed to avoid double printing*/
  /* Walk through the commands */
  for (c=commandlist;c;c=c->next)
    {
      if (c->name != NULL)
	{
	  /* Try to match against the name*/
	  returnvalue=re_search(regex,c->name,strlen(c->name),0,strlen(c->name),NULL);
	  if (returnvalue >= 0)
	    {
	      /* Stolen from help_cmd_list. We don't directly use
	       * help_cmd_list because it doesn't let us print out
	       * single commands
	       */
	      fprintf_filtered (stream, "%s%s -- ", prefix, c->name);
	      print_doc_line (stream, c->doc);
	      fputs_filtered ("\n", stream);
	      returnvalue=0; /*Set this so we don't print it again.*/
	    }
	}
      if (c->doc != NULL && returnvalue != 0)
	{
	  /* Try to match against documentation */
	  if (re_search(regex,c->doc,strlen(c->doc),0,strlen(c->doc),NULL) >=0)
	    {
	      /* Stolen from help_cmd_list. We don't directly use
	       * help_cmd_list because it doesn't let us print out
	       * single commands
	       */
	      fprintf_filtered (stream, "%s%s -- ", prefix, c->name);
	      print_doc_line (stream, c->doc);
	      fputs_filtered ("\n", stream);
	    }
	}
      /* Check if this command has subcommands */
      if (c->prefixlist != NULL)
	{
	  /* Recursively call ourselves on the subcommand list,
	     passing the right prefix in.
	  */
	  apropos_cmd (stream,*c->prefixlist,regex,c->prefixname);
	}
    }
}

/* This command really has to deal with two things:
 *     1) I want documentation on *this string* (usually called by
 * "help commandname").
 *     2) I want documentation on *this list* (usually called by
 * giving a command that requires subcommands.  Also called by saying
 * just "help".)
 *
 *   I am going to split this into two seperate comamnds, help_cmd and
 * help_list. 
 */

void
help_cmd (char *command, struct ui_file *stream)
{
  struct cmd_list_element *c;
  extern struct cmd_list_element *cmdlist;

  if (!command)
    {
      help_list (cmdlist, "", all_classes, stream);
      return;
    }

  if (strcmp (command, "all") == 0)
    {
      help_all (stream);
      return;
    }

  c = lookup_cmd (&command, cmdlist, "", 0, 0);

  if (c == 0)
    return;

  /* There are three cases here.
     If c->prefixlist is nonzero, we have a prefix command.
     Print its documentation, then list its subcommands.

     If c->func is non NULL, we really have a command.  Print its
     documentation and return.

     If c->func is NULL, we have a class name.  Print its
     documentation (as if it were a command) and then set class to the
     number of this class so that the commands in the class will be
     listed.  */

  fputs_filtered (c->doc, stream);
  fputs_filtered ("\n", stream);

  if (c->prefixlist == 0 && c->func != NULL)
    return;
  fprintf_filtered (stream, "\n");

  /* If this is a prefix command, print it's subcommands */
  if (c->prefixlist)
    help_list (*c->prefixlist, c->prefixname, all_commands, stream);

  /* If this is a class name, print all of the commands in the class */
  if (c->func == NULL)
    help_list (cmdlist, "", c->class, stream);

  if (c->hook_pre || c->hook_post)
    fprintf_filtered (stream,
                      "\nThis command has a hook (or hooks) defined:\n");

  if (c->hook_pre)
    fprintf_filtered (stream, 
                      "\tThis command is run after  : %s (pre hook)\n",
                    c->hook_pre->name);
  if (c->hook_post)
    fprintf_filtered (stream, 
                      "\tThis command is run before : %s (post hook)\n",
                    c->hook_post->name);
}

/*
 * Get a specific kind of help on a command list.
 *
 * LIST is the list.
 * CMDTYPE is the prefix to use in the title string.
 * CLASS is the class with which to list the nodes of this list (see
 * documentation for help_cmd_list below),  As usual, ALL_COMMANDS for
 * everything, ALL_CLASSES for just classes, and non-negative for only things
 * in a specific class.
 * and STREAM is the output stream on which to print things.
 * If you call this routine with a class >= 0, it recurses.
 */
void
help_list (struct cmd_list_element *list, char *cmdtype,
	   enum command_class class, struct ui_file *stream)
{
  int len;
  char *cmdtype1, *cmdtype2;

  /* If CMDTYPE is "foo ", CMDTYPE1 gets " foo" and CMDTYPE2 gets "foo sub"  */
  len = strlen (cmdtype);
  cmdtype1 = (char *) alloca (len + 1);
  cmdtype1[0] = 0;
  cmdtype2 = (char *) alloca (len + 4);
  cmdtype2[0] = 0;
  if (len)
    {
      cmdtype1[0] = ' ';
      strncpy (cmdtype1 + 1, cmdtype, len - 1);
      cmdtype1[len] = 0;
      strncpy (cmdtype2, cmdtype, len - 1);
      strcpy (cmdtype2 + len - 1, " sub");
    }

  if (class == all_classes)
    fprintf_filtered (stream, "List of classes of %scommands:\n\n", cmdtype2);
  else
    fprintf_filtered (stream, "List of %scommands:\n\n", cmdtype2);

  help_cmd_list (list, class, cmdtype, (int) class >= 0, stream);

  if (class == all_classes)
    {
      fprintf_filtered (stream, "\n\
Type \"help%s\" followed by a class name for a list of commands in ",
			cmdtype1);
      wrap_here ("");
      fprintf_filtered (stream, "that class.");
    }

  fprintf_filtered (stream, "\nType \"help%s\" followed by %scommand name ",
		    cmdtype1, cmdtype2);
  wrap_here ("");
  fputs_filtered ("for ", stream);
  wrap_here ("");
  fputs_filtered ("full ", stream);
  wrap_here ("");
  fputs_filtered ("documentation.\n", stream);
  fputs_filtered ("Command name abbreviations are allowed if unambiguous.\n",
		  stream);
}

static void
help_all (struct ui_file *stream)
{
  struct cmd_list_element *c;
  extern struct cmd_list_element *cmdlist;

  for (c = cmdlist; c; c = c->next)
    {
      if (c->abbrev_flag)
        continue;
      /* If this is a prefix command, print it's subcommands */
      if (c->prefixlist)
        help_cmd_list (*c->prefixlist, all_commands, c->prefixname, 0, stream);
    
      /* If this is a class name, print all of the commands in the class */
      else if (c->func == NULL)
        help_cmd_list (cmdlist, c->class, "", 0, stream);
    }
}

/* Print only the first line of STR on STREAM.  */
void
print_doc_line (struct ui_file *stream, char *str)
{
  static char *line_buffer = 0;
  static int line_size;
  char *p;

  if (!line_buffer)
    {
      line_size = 80;
      line_buffer = (char *) xmalloc (line_size);
    }

  p = str;
  while (*p && *p != '\n' && *p != '.' && *p != ',')
    p++;
  if (p - str > line_size - 1)
    {
      line_size = p - str + 1;
      xfree (line_buffer);
      line_buffer = (char *) xmalloc (line_size);
    }
  strncpy (line_buffer, str, p - str);
  line_buffer[p - str] = '\0';
  if (islower (line_buffer[0]))
    line_buffer[0] = toupper (line_buffer[0]);
  ui_out_text (uiout, line_buffer);
}

/*
 * Implement a help command on command list LIST.
 * RECURSE should be non-zero if this should be done recursively on
 * all sublists of LIST.
 * PREFIX is the prefix to print before each command name.
 * STREAM is the stream upon which the output should be written.
 * CLASS should be:
 *      A non-negative class number to list only commands in that
 * class.
 *      ALL_COMMANDS to list all commands in list.
 *      ALL_CLASSES  to list all classes in list.
 *
 *   Note that RECURSE will be active on *all* sublists, not just the
 * ones selected by the criteria above (ie. the selection mechanism
 * is at the low level, not the high-level).
 */
void
help_cmd_list (struct cmd_list_element *list, enum command_class class,
	       char *prefix, int recurse, struct ui_file *stream)
{
  struct cmd_list_element *c;

  for (c = list; c; c = c->next)
    {
      if (c->abbrev_flag == 0 &&
	  (class == all_commands
	   || (class == all_classes && c->func == NULL)
	   || (class == c->class && c->func != NULL)))
	{
	  fprintf_filtered (stream, "%s%s -- ", prefix, c->name);
	  print_doc_line (stream, c->doc);
	  fputs_filtered ("\n", stream);
	}
      if (recurse
	  && c->prefixlist != 0
	  && c->abbrev_flag == 0)
	help_cmd_list (*c->prefixlist, class, c->prefixname, 1, stream);
    }
}


/* Search the input clist for 'command'.  Return the command if
   found (or NULL if not), and return the number of commands
   found in nfound */

static struct cmd_list_element *
find_cmd (char *command, int len, struct cmd_list_element *clist,
	  int ignore_help_classes, int *nfound)
{
  struct cmd_list_element *found, *c;

  found = (struct cmd_list_element *) NULL;
  *nfound = 0;
  for (c = clist; c; c = c->next)
    if (!strncmp (command, c->name, len)
	&& (!ignore_help_classes || c->func))
      {
	found = c;
	(*nfound)++;
	if (c->name[len] == '\0')
	  {
	    *nfound = 1;
	    break;
	  }
      }
  return found;
}

/* This routine takes a line of TEXT and a CLIST in which to start the
   lookup.  When it returns it will have incremented the text pointer past
   the section of text it matched, set *RESULT_LIST to point to the list in
   which the last word was matched, and will return a pointer to the cmd
   list element which the text matches.  It will return NULL if no match at
   all was possible.  It will return -1 (cast appropriately, ick) if ambigous
   matches are possible; in this case *RESULT_LIST will be set to point to
   the list in which there are ambiguous choices (and *TEXT will be set to
   the ambiguous text string).

   If the located command was an abbreviation, this routine returns the base
   command of the abbreviation.

   It does no error reporting whatsoever; control will always return
   to the superior routine.

   In the case of an ambiguous return (-1), *RESULT_LIST will be set to point
   at the prefix_command (ie. the best match) *or* (special case) will be NULL
   if no prefix command was ever found.  For example, in the case of "info a",
   "info" matches without ambiguity, but "a" could be "args" or "address", so
   *RESULT_LIST is set to the cmd_list_element for "info".  So in this case
   RESULT_LIST should not be interpeted as a pointer to the beginning of a
   list; it simply points to a specific command.  In the case of an ambiguous
   return *TEXT is advanced past the last non-ambiguous prefix (e.g.
   "info t" can be "info types" or "info target"; upon return *TEXT has been
   advanced past "info ").

   If RESULT_LIST is NULL, don't set *RESULT_LIST (but don't otherwise
   affect the operation).

   This routine does *not* modify the text pointed to by TEXT.

   If IGNORE_HELP_CLASSES is nonzero, ignore any command list elements which
   are actually help classes rather than commands (i.e. the function field of
   the struct cmd_list_element is NULL).  */

struct cmd_list_element *
lookup_cmd_1 (char **text, struct cmd_list_element *clist,
	      struct cmd_list_element **result_list, int ignore_help_classes)
{
  char *p, *command;
  int len, tmp, nfound;
  struct cmd_list_element *found, *c;
  char *line = *text;

  while (**text == ' ' || **text == '\t')
    (*text)++;

  /* Treating underscores as part of command words is important
     so that "set args_foo()" doesn't get interpreted as
     "set args _foo()".  */
  /* NOTE: cagney/2003-02-13 The `tui_active' was previously
     `tui_version'.  */
  for (p = *text;
       *p && (isalnum (*p) || *p == '-' || *p == '_' ||
#if defined(TUI)
	      (tui_active &&
	       (*p == '+' || *p == '<' || *p == '>' || *p == '$')) ||
#endif
	      (xdb_commands && (*p == '!' || *p == '/' || *p == '?')));
       p++)
    ;

  /* If nothing but whitespace, return 0.  */
  if (p == *text)
    return 0;

  len = p - *text;

  /* *text and p now bracket the first command word to lookup (and
     it's length is len).  We copy this into a local temporary */


  command = (char *) alloca (len + 1);
  for (tmp = 0; tmp < len; tmp++)
    {
      char x = (*text)[tmp];
      command[tmp] = x;
    }
  command[len] = '\0';

  /* Look it up.  */
  found = 0;
  nfound = 0;
  found = find_cmd (command, len, clist, ignore_help_classes, &nfound);

  /* 
     ** We didn't find the command in the entered case, so lower case it
     ** and search again.
   */
  if (!found || nfound == 0)
    {
      for (tmp = 0; tmp < len; tmp++)
	{
	  char x = command[tmp];
	  command[tmp] = isupper (x) ? tolower (x) : x;
	}
      found = find_cmd (command, len, clist, ignore_help_classes, &nfound);
    }

  /* If nothing matches, we have a simple failure.  */
  if (nfound == 0)
    return 0;

  if (nfound > 1)
    {
      if (result_list != NULL)
	/* Will be modified in calling routine
	   if we know what the prefix command is.  */
	*result_list = 0;
      return (struct cmd_list_element *) -1;	/* Ambiguous.  */
    }

  /* We've matched something on this list.  Move text pointer forward. */

  *text = p;

  if (found->cmd_pointer)
    {
      /* We drop the alias (abbreviation) in favor of the command it is
       pointing to.  If the alias is deprecated, though, we need to
       warn the user about it before we drop it.  Note that while we
       are warning about the alias, we may also warn about the command
       itself and we will adjust the appropriate DEPRECATED_WARN_USER
       flags */
      
      if (found->flags & DEPRECATED_WARN_USER)
      deprecated_cmd_warning (&line);
      found = found->cmd_pointer;
    }
  /* If we found a prefix command, keep looking.  */

  if (found->prefixlist)
    {
      c = lookup_cmd_1 (text, *found->prefixlist, result_list,
			ignore_help_classes);
      if (!c)
	{
	  /* Didn't find anything; this is as far as we got.  */
	  if (result_list != NULL)
	    *result_list = clist;
	  return found;
	}
      else if (c == (struct cmd_list_element *) -1)
	{
	  /* We've gotten this far properly, but the next step
	     is ambiguous.  We need to set the result list to the best
	     we've found (if an inferior hasn't already set it).  */
	  if (result_list != NULL)
	    if (!*result_list)
	      /* This used to say *result_list = *found->prefixlist
	         If that was correct, need to modify the documentation
	         at the top of this function to clarify what is supposed
	         to be going on.  */
	      *result_list = found;
	  return c;
	}
      else
	{
	  /* We matched!  */
	  return c;
	}
    }
  else
    {
      if (result_list != NULL)
	*result_list = clist;
      return found;
    }
}

/* All this hair to move the space to the front of cmdtype */

static void
undef_cmd_error (char *cmdtype, char *q)
{
  error ("Undefined %scommand: \"%s\".  Try \"help%s%.*s\".",
	 cmdtype,
	 q,
	 *cmdtype ? " " : "",
	 (int) strlen (cmdtype) - 1,
	 cmdtype);
}

/* Look up the contents of *LINE as a command in the command list LIST.
   LIST is a chain of struct cmd_list_element's.
   If it is found, return the struct cmd_list_element for that command
   and update *LINE to point after the command name, at the first argument.
   If not found, call error if ALLOW_UNKNOWN is zero
   otherwise (or if error returns) return zero.
   Call error if specified command is ambiguous,
   unless ALLOW_UNKNOWN is negative.
   CMDTYPE precedes the word "command" in the error message.

   If INGNORE_HELP_CLASSES is nonzero, ignore any command list
   elements which are actually help classes rather than commands (i.e.
   the function field of the struct cmd_list_element is 0).  */

struct cmd_list_element *
lookup_cmd (char **line, struct cmd_list_element *list, char *cmdtype,
	    int allow_unknown, int ignore_help_classes)
{
  struct cmd_list_element *last_list = 0;
  struct cmd_list_element *c =
  lookup_cmd_1 (line, list, &last_list, ignore_help_classes);

  /* Note: Do not remove trailing whitespace here because this
     would be wrong for complete_command.  Jim Kingdon  */

  if (!c)
    {
      if (!allow_unknown)
	{
	  if (!*line)
	    error ("Lack of needed %scommand", cmdtype);
	  else
	    {
	      char *p = *line, *q;

	      while (isalnum (*p) || *p == '-')
		p++;

	      q = (char *) alloca (p - *line + 1);
	      strncpy (q, *line, p - *line);
	      q[p - *line] = '\0';
	      undef_cmd_error (cmdtype, q);
	    }
	}
      else
	return 0;
    }
  else if (c == (struct cmd_list_element *) -1)
    {
      /* Ambigous.  Local values should be off prefixlist or called
         values.  */
      int local_allow_unknown = (last_list ? last_list->allow_unknown :
				 allow_unknown);
      char *local_cmdtype = last_list ? last_list->prefixname : cmdtype;
      struct cmd_list_element *local_list =
      (last_list ? *(last_list->prefixlist) : list);

      if (local_allow_unknown < 0)
	{
	  if (last_list)
	    return last_list;	/* Found something.  */
	  else
	    return 0;		/* Found nothing.  */
	}
      else
	{
	  /* Report as error.  */
	  int amb_len;
	  char ambbuf[100];

	  for (amb_len = 0;
	       ((*line)[amb_len] && (*line)[amb_len] != ' '
		&& (*line)[amb_len] != '\t');
	       amb_len++)
	    ;

	  ambbuf[0] = 0;
	  for (c = local_list; c; c = c->next)
	    if (!strncmp (*line, c->name, amb_len))
	      {
		if (strlen (ambbuf) + strlen (c->name) + 6 < (int) sizeof ambbuf)
		  {
		    if (strlen (ambbuf))
		      strcat (ambbuf, ", ");
		    strcat (ambbuf, c->name);
		  }
		else
		  {
		    strcat (ambbuf, "..");
		    break;
		  }
	      }
	  error ("Ambiguous %scommand \"%s\": %s.", local_cmdtype,
		 *line, ambbuf);
	  return 0;		/* lint */
	}
    }
  else
    {
      /* We've got something.  It may still not be what the caller
         wants (if this command *needs* a subcommand).  */
      while (**line == ' ' || **line == '\t')
	(*line)++;

      if (c->prefixlist && **line && !c->allow_unknown)
	undef_cmd_error (c->prefixname, *line);

      /* Seems to be what he wants.  Return it.  */
      return c;
    }
  return 0;
}

/* We are here presumably because an alias or command in *TEXT is 
   deprecated and a warning message should be generated.  This function
   decodes *TEXT and potentially generates a warning message as outlined
   below.
   
   Example for 'set endian big' which has a fictitious alias 'seb'.
   
   If alias wasn't used in *TEXT, and the command is deprecated:
   "warning: 'set endian big' is deprecated." 
   
   If alias was used, and only the alias is deprecated:
   "warning: 'seb' an alias for the command 'set endian big' is deprecated."
   
   If alias was used and command is deprecated (regardless of whether the
   alias itself is deprecated:
   
   "warning: 'set endian big' (seb) is deprecated."

   After the message has been sent, clear the appropriate flags in the
   command and/or the alias so the user is no longer bothered.
   
*/
void
deprecated_cmd_warning (char **text)
{
  struct cmd_list_element *alias = NULL;
  struct cmd_list_element *prefix_cmd = NULL;
  struct cmd_list_element *cmd = NULL;
  struct cmd_list_element *c;
  char *type;
 
  if (!lookup_cmd_composition (*text, &alias, &prefix_cmd, &cmd))
    /* return if text doesn't evaluate to a command */
    return;

  if (!((alias ? (alias->flags & DEPRECATED_WARN_USER) : 0)
      || (cmd->flags & DEPRECATED_WARN_USER) ) ) 
    /* return if nothing is deprecated */
    return;
  
  printf_filtered ("Warning:");
  
  if (alias && !(cmd->flags & CMD_DEPRECATED))
    printf_filtered (" '%s', an alias for the", alias->name);
    
  printf_filtered (" command '");
  
  if (prefix_cmd)
    printf_filtered ("%s", prefix_cmd->prefixname);
  
  printf_filtered ("%s", cmd->name);

  if (alias && (cmd->flags & CMD_DEPRECATED))
    printf_filtered ("' (%s) is deprecated.\n", alias->name);
  else
    printf_filtered ("' is deprecated.\n"); 
  

  /* if it is only the alias that is deprecated, we want to indicate the
     new alias, otherwise we'll indicate the new command */

  if (alias && !(cmd->flags & CMD_DEPRECATED))
    {
      if (alias->replacement)
      printf_filtered ("Use '%s'.\n\n", alias->replacement);
      else
      printf_filtered ("No alternative known.\n\n");
     }  
  else
    {
      if (cmd->replacement)
      printf_filtered ("Use '%s'.\n\n", cmd->replacement);
      else
      printf_filtered ("No alternative known.\n\n");
    }

  /* We've warned you, now we'll keep quiet */
  if (alias)
    alias->flags &= ~DEPRECATED_WARN_USER;
  
  cmd->flags &= ~DEPRECATED_WARN_USER;
}



/* Look up the contents of LINE as a command in the command list 'cmdlist'. 
   Return 1 on success, 0 on failure.
   
   If LINE refers to an alias, *alias will point to that alias.
   
   If LINE is a postfix command (i.e. one that is preceeded by a prefix
   command) set *prefix_cmd.
   
   Set *cmd to point to the command LINE indicates.
   
   If any of *alias, *prefix_cmd, or *cmd cannot be determined or do not 
   exist, they are NULL when we return.
   
*/
int
lookup_cmd_composition (char *text,
                      struct cmd_list_element **alias,
                      struct cmd_list_element **prefix_cmd, 
                      struct cmd_list_element **cmd)
{
  char *p, *command;
  int len, tmp, nfound;
  struct cmd_list_element *cur_list;
  struct cmd_list_element *prev_cmd;
  *alias = NULL;
  *prefix_cmd = NULL;
  *cmd = NULL;
  
  cur_list = cmdlist;
  
  while (1)
    { 
      /* Go through as many command lists as we need to 
       to find the command TEXT refers to. */
      
      prev_cmd = *cmd;
      
      while (*text == ' ' || *text == '\t')
      (text)++;
      
      /* Treating underscores as part of command words is important
       so that "set args_foo()" doesn't get interpreted as
       "set args _foo()".  */
      /* NOTE: cagney/2003-02-13 The `tui_active' was previously
	 `tui_version'.  */
      for (p = text;
         *p && (isalnum (*p) || *p == '-' || *p == '_' ||
#if defined(TUI)
                (tui_active &&
                 (*p == '+' || *p == '<' || *p == '>' || *p == '$')) ||
#endif
                (xdb_commands && (*p == '!' || *p == '/' || *p == '?')));
         p++)
      ;
      
      /* If nothing but whitespace, return.  */
      if (p == text)
      return 0;
      
      len = p - text;
      
      /* text and p now bracket the first command word to lookup (and
       it's length is len).  We copy this into a local temporary */
      
      command = (char *) alloca (len + 1);
      for (tmp = 0; tmp < len; tmp++)
      {
        char x = text[tmp];
        command[tmp] = x;
      }
      command[len] = '\0';
      
      /* Look it up.  */
      *cmd = 0;
      nfound = 0;
      *cmd = find_cmd (command, len, cur_list, 1, &nfound);
      
      /* We didn't find the command in the entered case, so lower case it
       and search again.
      */
      if (!*cmd || nfound == 0)
      {
        for (tmp = 0; tmp < len; tmp++)
          {
            char x = command[tmp];
            command[tmp] = isupper (x) ? tolower (x) : x;
          }
        *cmd = find_cmd (command, len, cur_list, 1, &nfound);
      }
      
      if (*cmd == (struct cmd_list_element *) -1)
      {
        return 0;              /* ambiguous */
      }
      
      if (*cmd == NULL)
      return 0;                /* nothing found */
      else
      {
        if ((*cmd)->cmd_pointer)
          {
            /* cmd was actually an alias, we note that an alias was used 
               (by assigning *alais) and we set *cmd. 
             */
            *alias = *cmd;
            *cmd = (*cmd)->cmd_pointer;
          }
        *prefix_cmd = prev_cmd;
      }
      if ((*cmd)->prefixlist)
      cur_list = *(*cmd)->prefixlist;
      else
      return 1;
      
      text = p;
    }
}

/* Helper function for SYMBOL_COMPLETION_FUNCTION.  */

/* Return a vector of char pointers which point to the different
   possible completions in LIST of TEXT.  

   WORD points in the same buffer as TEXT, and completions should be
   returned relative to this position.  For example, suppose TEXT is "foo"
   and we want to complete to "foobar".  If WORD is "oo", return
   "oobar"; if WORD is "baz/foo", return "baz/foobar".  */

char **
complete_on_cmdlist (struct cmd_list_element *list, char *text, char *word)
{
  struct cmd_list_element *ptr;
  char **matchlist;
  int sizeof_matchlist;
  int matches;
  int textlen = strlen (text);

  sizeof_matchlist = 10;
  matchlist = (char **) xmalloc (sizeof_matchlist * sizeof (char *));
  matches = 0;

  for (ptr = list; ptr; ptr = ptr->next)
    if (!strncmp (ptr->name, text, textlen)
	&& !ptr->abbrev_flag
	&& (ptr->func
	    || ptr->prefixlist))
      {
	if (matches == sizeof_matchlist)
	  {
	    sizeof_matchlist *= 2;
	    matchlist = (char **) xrealloc ((char *) matchlist,
					    (sizeof_matchlist
					     * sizeof (char *)));
	  }

	matchlist[matches] = (char *)
	  xmalloc (strlen (word) + strlen (ptr->name) + 1);
	if (word == text)
	  strcpy (matchlist[matches], ptr->name);
	else if (word > text)
	  {
	    /* Return some portion of ptr->name.  */
	    strcpy (matchlist[matches], ptr->name + (word - text));
	  }
	else
	  {
	    /* Return some of text plus ptr->name.  */
	    strncpy (matchlist[matches], word, text - word);
	    matchlist[matches][text - word] = '\0';
	    strcat (matchlist[matches], ptr->name);
	  }
	++matches;
      }

  if (matches == 0)
    {
      xfree (matchlist);
      matchlist = 0;
    }
  else
    {
      matchlist = (char **) xrealloc ((char *) matchlist, ((matches + 1)
							* sizeof (char *)));
      matchlist[matches] = (char *) 0;
    }

  return matchlist;
}

/* Helper function for SYMBOL_COMPLETION_FUNCTION.  */

/* Return a vector of char pointers which point to the different
   possible completions in CMD of TEXT.  

   WORD points in the same buffer as TEXT, and completions should be
   returned relative to this position.  For example, suppose TEXT is "foo"
   and we want to complete to "foobar".  If WORD is "oo", return
   "oobar"; if WORD is "baz/foo", return "baz/foobar".  */

char **
complete_on_enum (const char *enumlist[],
		  char *text,
		  char *word)
{
  char **matchlist;
  int sizeof_matchlist;
  int matches;
  int textlen = strlen (text);
  int i;
  const char *name;

  sizeof_matchlist = 10;
  matchlist = (char **) xmalloc (sizeof_matchlist * sizeof (char *));
  matches = 0;

  for (i = 0; (name = enumlist[i]) != NULL; i++)
    if (strncmp (name, text, textlen) == 0)
      {
	if (matches == sizeof_matchlist)
	  {
	    sizeof_matchlist *= 2;
	    matchlist = (char **) xrealloc ((char *) matchlist,
					    (sizeof_matchlist
					     * sizeof (char *)));
	  }

	matchlist[matches] = (char *)
	  xmalloc (strlen (word) + strlen (name) + 1);
	if (word == text)
	  strcpy (matchlist[matches], name);
	else if (word > text)
	  {
	    /* Return some portion of name.  */
	    strcpy (matchlist[matches], name + (word - text));
	  }
	else
	  {
	    /* Return some of text plus name.  */
	    strncpy (matchlist[matches], word, text - word);
	    matchlist[matches][text - word] = '\0';
	    strcat (matchlist[matches], name);
	  }
	++matches;
      }

  if (matches == 0)
    {
      xfree (matchlist);
      matchlist = 0;
    }
  else
    {
      matchlist = (char **) xrealloc ((char *) matchlist, ((matches + 1)
							* sizeof (char *)));
      matchlist[matches] = (char *) 0;
    }

  return matchlist;
}


/* check function pointer */
int
cmd_func_p (struct cmd_list_element *cmd)
{
  return (cmd->func != NULL);
}


/* call the command function */
void
cmd_func (struct cmd_list_element *cmd, char *args, int from_tty)
{
  if (cmd_func_p (cmd))
    (*cmd->func) (cmd, args, from_tty);
  else
    error ("Invalid command");
}


