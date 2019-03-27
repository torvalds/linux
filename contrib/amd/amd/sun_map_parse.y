%{
/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 2005 Daniel P. Ottavio
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 * File: am-utils/amd/sun_map_parse.y
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>
#include <sun_map.h>


#define SUN_FSTYPE_STR  "fstype="


extern int sun_map_lex(void);
extern int sun_map_error(const char *);
extern void sun_map_tok_setbuff(const char *);
extern int sun_map_parse(void);

struct sun_entry *sun_map_parse_read(const char *);

static struct sun_list *sun_entry_list = NULL;
static struct sun_list *sun_opt_list = NULL;
static struct sun_list *sun_host_list = NULL;
static struct sun_list *sun_location_list = NULL;
static struct sun_list *mountpt_list = NULL;
static char *tmpFsType = NULL;


/*
 * Each get* function returns a pointer to the corresponding global
 * list structure.  If the structure is NULL than a new instance is
 * returned.
 */
static struct sun_list *get_sun_opt_list(void);
static struct sun_list *get_sun_host_list(void);
static struct sun_list *get_sun_location_list(void);
static struct sun_list *get_mountpt_list(void);
static struct sun_list *get_sun_entry_list(void);

%}

%union {
  char strval[2048];
}

%token NEWLINE COMMENT WSPACE
%token <strval> WORD

%%

amap : file
     ;

file : new_lines entries
     | entries
     ;

entries : entry
        | entry new_lines
        | entry new_lines entries
        ;

new_lines : NEWLINE
          | NEWLINE new_lines
          ;

entry : locations {

  struct sun_list *list;
  struct sun_entry *entry;

  /* allocate an entry */
  entry = CALLOC(struct sun_entry);

  /*
   * Assign the global location list to this entry and reset the
   * global pointer.  Reseting the global pointer will create a new
   * list instance next time get_sun_location_list() is called.
   */
  list = get_sun_location_list();
  entry->location_list = (struct sun_location *)list->first;
  sun_location_list = NULL;

   /* Add this entry to the entry list. */
  sun_list_add(get_sun_entry_list(), (qelem *)entry);
}

| '-' options WSPACE locations {

  struct sun_list *list;
  struct sun_entry *entry;

  entry = CALLOC(struct sun_entry);

  /* An fstype may have been defined in the 'options'. */
  if (tmpFsType != NULL) {
    entry->fstype = tmpFsType;
    tmpFsType = NULL;
  }

  /*
   * Assign the global location list to this entry and reset the
   * global pointer.  Reseting the global pointer will create a new
   * list instance next time get_sun_location_list() is called.
   */
  list = get_sun_location_list();
  entry->location_list = (struct sun_location *)list->first;
  sun_location_list = NULL;

  /*
   * Assign the global opt list to this entry and reset the global
   * pointer.  Reseting the global pointer will create a new list
   * instance next time get_sun_opt_list() is called.
   */
  list = get_sun_opt_list();
  entry->opt_list = (struct sun_opt *)list->first;
  sun_opt_list = NULL;

  /* Add this entry to the entry list. */
  sun_list_add(get_sun_entry_list(), (qelem *)entry);
}

| mountpoints {

  struct sun_list *list;
  struct sun_entry *entry;

  /* allocate an entry */
  entry = CALLOC(struct sun_entry);

  /*
   * Assign the global mountpt list to this entry and reset the global
   * pointer.  Reseting the global pointer will create a new list
   * instance next time get_mountpt_list() is called.
   */
  list = get_mountpt_list();
  entry->mountpt_list = (struct sun_mountpt *)list->first;
  mountpt_list = NULL;

  /* Add this entry to the entry list. */
  sun_list_add(get_sun_entry_list(), (qelem *)entry);
}

| '-' options WSPACE mountpoints {

  struct sun_list *list;
  struct sun_entry *entry;

  /* allocate an entry */
  entry = CALLOC(struct sun_entry);

  /* An fstype may have been defined in the 'options'. */
  if (tmpFsType != NULL) {
    entry->fstype = tmpFsType;
    tmpFsType = NULL;
  }

  /*
   * Assign the global mountpt list to this entry and reset the global
   * pointer.  Reseting the global pointer will create a new list
   * instance next time get_mountpt_list() is called.
   */
  list = get_mountpt_list();
  entry->mountpt_list = (struct sun_mountpt *)list->first;
  mountpt_list = NULL;

  /*
   * Assign the global opt list to this entry and reset the global
   * pointer.  Reseting the global pointer will create a new list
   * instance next time get_sun_opt_list() is called.
   */
  list = get_sun_opt_list();
  entry->opt_list = (struct sun_opt *)list->first;
  sun_opt_list = NULL;

  /* Add this entry to the entry list. */
  sun_list_add(get_sun_entry_list(), (qelem *)entry);
}
;

mountpoints : mountpoint
            | mountpoint WSPACE mountpoints
            ;

mountpoint : WORD WSPACE location {

  struct sun_list *list;
  struct sun_mountpt *mountpt;

  /* allocate a mountpt */
  mountpt = CALLOC(struct sun_mountpt);

  /*
   * Assign the global loaction list to this entry and reset the
   * global pointer.  Reseting the global pointer will create a new
   * list instance next time get_sun_location_list() is called.
   */
  list = get_sun_location_list();
  mountpt->location_list = (struct sun_location *)list->first;
  sun_location_list = NULL;

  mountpt->path = xstrdup($1);

  /* Add this mountpt to the mountpt list. */
  sun_list_add(get_mountpt_list(), (qelem *)mountpt);
}

| WORD WSPACE '-' options WSPACE location {

  struct sun_list *list;
  struct sun_mountpt *mountpt;

  /* allocate a mountpt */
  mountpt = CALLOC(struct sun_mountpt);

  /* An fstype may have been defined in the 'options'. */
  if (tmpFsType != NULL) {
    mountpt->fstype = tmpFsType;
    tmpFsType = NULL;
  }

  /*
   * Assign the global location list to this entry and reset the
   * global pointer.  Reseting the global pointer will create a new
   * list instance next time get_sun_location_list() is called.
   */
  list = get_sun_location_list();
  mountpt->location_list = (struct sun_location *)list->first;
  sun_location_list = NULL;

  /*
   * Assign the global opt list to this entry and reset the global
   * pointer.  Reseting the global pointer will create a new list
   * instance next time get_sun_opt_list() is called.
   */
  list = get_sun_opt_list();
  mountpt->opt_list = (struct sun_opt *)list->first;
  sun_opt_list = NULL;

  mountpt->path = xstrdup($1);

  /* Add this mountpt to the mountpt list. */
  sun_list_add(get_mountpt_list(), (qelem *)mountpt);
}
;

locations : location
          | location WSPACE locations
          ;

location : hosts ':' WORD {

  struct sun_list *list;
  struct sun_location *location;

  /* allocate a new location */
  location = CALLOC(struct sun_location);

  /*
   * Assign the global opt list to this entry and reset the global
   * pointer.  Reseting the global pointer will create a new list
   * instance next time get_sun_opt_list() is called.
   */
  list = get_sun_host_list();
  location->host_list = (struct sun_host *)list->first;
  sun_host_list = NULL;

  location->path = xstrdup($3);

  /* Add this location to the location list. */
  sun_list_add(get_sun_location_list(), (qelem *)location);
}

| ':' WORD {

  struct sun_location *location;

  /* allocate a new location */
  location = CALLOC(struct sun_location);

  location->path = xstrdup($2);

  /* Add this location to the location list. */
  sun_list_add(get_sun_location_list(), (qelem *)location);
}
;

hosts : host
      | host ',' hosts
      ;

host : WORD {

  /* allocate a new host */
  struct sun_host *host = CALLOC(struct sun_host);

  host->name = xstrdup($1);

  /* Add this host to the host list. */
  sun_list_add(get_sun_host_list(),(qelem *)host);
}

| WORD weight {

  /*
   * It is assumed that the host for this rule was allocated by the
   * 'weight' rule and assigned to be the last host item on the host
   * list.
   */
  struct sun_host *host = (struct sun_host *)sun_host_list->last;

  host->name = xstrdup($1);
}
;

weight : '(' WORD ')' {

  int val;
  /* allocate a new host */
  struct sun_host *host = CALLOC(struct sun_host);

  val = atoi($2);

  host->weight = val;

  /* Add this host to the host list. */
  sun_list_add(get_sun_host_list(), (qelem *)host);
}
;

options : option
        | option ',' options
        ;

option : WORD {

  char *type;

  /* check if this is an fstype option */
  if ((type = strstr($1,SUN_FSTYPE_STR)) != NULL) {
    /* parse out the fs type from the Sun fstype keyword  */
    if ((type = type + strlen(SUN_FSTYPE_STR)) != NULL) {
      /*
       * This global fstype str will be assigned to the current being
       * parsed later in the parsing.
       */
      tmpFsType = xstrdup(type);
    }
  }
  else {
    /*
     * If it is not an fstype option allocate an opt struct and assign
     * the value.
     */
    struct sun_opt *opt = CALLOC(struct sun_opt);
    opt->str = xstrdup($1);
    /* Add this opt to the opt list. */
    sun_list_add(get_sun_opt_list(), (qelem *)opt);
  }
}

;

%%

/*
 * Parse 'map_data' which is assumed to be a Sun-syle map.  If
 * successful a sun_entry is returned.
 *
 * The parser is designed to parse map entries with out the keys.  For
 * example the entry:
 *
 * usr -ro pluto:/usr/local
 *
 * should be passed to the parser as:
 *
 * -ro pluto:/usr/local
 *
 * The reason for this is that the Amd info services already strip off
 * the key when they read map info.
 */
struct sun_entry *
sun_map_parse_read(const char *map_data)
{
  struct sun_entry *retval = NULL;

  /* pass map_data to lex */
  sun_map_tok_setbuff(map_data);

  /* call yacc */
  sun_map_parse();

  if (sun_entry_list != NULL) {
    /* return the first Sun entry in the list */
    retval = (struct sun_entry*)sun_entry_list->first;
    sun_entry_list = NULL;
  }
  else {
    plog(XLOG_ERROR, "Sun map parser did not produce data structs.");
  }

  return retval;
}


static struct sun_list *
get_sun_entry_list(void)
{
  if (sun_entry_list == NULL) {
    sun_entry_list = CALLOC(struct sun_list);
  }
  return sun_entry_list;
}


static struct sun_list *
get_mountpt_list(void)
{
  if (mountpt_list == NULL) {
    mountpt_list = CALLOC(struct sun_list);
  }
  return mountpt_list;
}


static struct sun_list *
get_sun_location_list(void)
{
  if (sun_location_list == NULL) {
    sun_location_list = CALLOC(struct sun_list);
  }
  return sun_location_list;
}


static struct sun_list *
get_sun_host_list(void)
{
  if (sun_host_list == NULL) {
    sun_host_list = CALLOC(struct sun_list);
  }
  return sun_host_list;
}


static struct sun_list *
get_sun_opt_list(void)
{
  if (sun_opt_list == NULL) {
    sun_opt_list = CALLOC(struct sun_list);
  }
  return sun_opt_list;
}
