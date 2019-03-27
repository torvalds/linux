/****************************************************************************
 * Copyright (c) 1998-2009,2013 Free Software Foundation, Inc.              *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *   Author:  Juergen Pfeifer, 1995,1997                                    *
 ****************************************************************************/

/* $Id: form.h,v 0.23 2013/12/07 17:57:32 tom Exp $ */

#ifndef FORM_H
#define FORM_H

#include <curses.h>
#include <eti.h>

#ifdef __cplusplus
  extern "C" {
#endif

#ifndef FORM_PRIV_H
typedef void *FIELD_CELL;
#endif

#ifndef NCURSES_FIELD_INTERNALS
#define NCURSES_FIELD_INTERNALS /* nothing */
#endif

typedef int Form_Options;
typedef int Field_Options;

	/**********
	*  _PAGE  *
	**********/

typedef struct {
  short pmin;		/* index of first field on page			*/
  short pmax;		/* index of last field on page			*/
  short smin;		/* index of top leftmost field on page		*/
  short smax;		/* index of bottom rightmost field on page	*/
} _PAGE;

	/**********
	*  FIELD  *
	**********/

typedef struct fieldnode {
  unsigned short	status;		/* flags			*/
  short			rows;		/* size in rows			*/
  short			cols;		/* size in cols			*/
  short			frow;		/* first row			*/
  short			fcol;		/* first col			*/
  int			drows;		/* dynamic rows			*/
  int			dcols;		/* dynamic cols			*/
  int			maxgrow;	/* maximum field growth		*/
  int			nrow;		/* off-screen rows		*/
  short			nbuf;		/* additional buffers		*/
  short			just;		/* justification		*/
  short			page;		/* page on form			*/
  short			index;		/* into form -> field		*/
  int			pad;		/* pad character		*/
  chtype		fore;		/* foreground attribute		*/
  chtype		back;		/* background attribute		*/
  Field_Options		opts;		/* options			*/
  struct fieldnode *	snext;		/* sorted order pointer		*/
  struct fieldnode *	sprev;		/* sorted order pointer		*/
  struct fieldnode *	link;		/* linked field chain		*/
  struct formnode *	form;		/* containing form		*/
  struct typenode *	type;		/* field type			*/
  void *		arg;		/* argument for type		*/
  FIELD_CELL *		buf;		/* field buffers		*/
  void *		usrptr;		/* user pointer			*/
  /*
   * The wide-character configuration requires extra information.  Because
   * there are existing applications that manipulate the members of FIELD
   * directly, we cannot make the struct opaque.  Offsets of members up to
   * this point are the same in the narrow- and wide-character configuration.
   * But note that the type of buf depends on the configuration, and is made
   * opaque for that reason.
   */
  NCURSES_FIELD_INTERNALS
} FIELD;


	/*********
	*  FORM  *
	*********/

typedef struct formnode {
  unsigned short	status;	  	/* flags			*/
  short			rows;		/* size in rows			*/
  short			cols;		/* size in cols			*/
  int			currow;		/* current row in field window	*/
  int			curcol;		/* current col in field window	*/
  int			toprow;		/* in scrollable field window	*/
  int			begincol;	/* in horiz. scrollable field	*/
  short			maxfield;	/* number of fields		*/
  short			maxpage;	/* number of pages		*/
  short			curpage;	/* index into page		*/
  Form_Options		opts;		/* options			*/
  WINDOW *		win;		/* window			*/
  WINDOW *		sub;		/* subwindow			*/
  WINDOW *		w;		/* window for current field	*/
  FIELD **		field;		/* field [maxfield]		*/
  FIELD *		current;	/* current field		*/
  _PAGE *		page;		/* page [maxpage]		*/
  void *		usrptr;		/* user pointer			*/

  void			(*forminit)(struct formnode *);
  void			(*formterm)(struct formnode *);
  void			(*fieldinit)(struct formnode *);
  void			(*fieldterm)(struct formnode *);

} FORM;


	/**************
	*  FIELDTYPE  *
	**************/

typedef struct typenode {
  unsigned short	status;			/* flags		    */
  long			ref;			/* reference count	    */
  struct typenode *	left;			/* ptr to operand for |     */
  struct typenode *	right;			/* ptr to operand for |     */

  void* (*makearg)(va_list *);			/* make fieldtype arg	    */
  void* (*copyarg)(const void *);		/* copy fieldtype arg 	    */
  void	(*freearg)(void *);			/* free fieldtype arg	    */

#if NCURSES_INTEROP_FUNCS
  union {
    bool (*ofcheck)(FIELD *,const void *);	/* field validation	    */
    bool (*gfcheck)(FORM*,FIELD *,const void*);	/* generic field validation */
  } fieldcheck;
  union {
    bool (*occheck)(int,const void *);		/* character validation     */
    bool (*gccheck)(int,FORM*,
		    FIELD*,const void*);        /* generic char validation  */
  } charcheck;
  union {
    bool (*onext)(FIELD *,const void *);        /* enumerate next value     */
    bool (*gnext)(FORM*,FIELD*,const void*);    /* generic enumerate next   */
  } enum_next;
  union {
    bool (*oprev)(FIELD *,const void *);	/* enumerate prev value     */
    bool (*gprev)(FORM*,FIELD*,const void*);    /* generic enumerate prev   */
  } enum_prev;
  void* (*genericarg)(void*);                   /* Alternate Arg method     */
#else
  bool	(*fcheck)(FIELD *,const void *);	/* field validation	*/
  bool	(*ccheck)(int,const void *);		/* character validation */

  bool	(*next)(FIELD *,const void *);		/* enumerate next value */
  bool	(*prev)(FIELD *,const void *);		/* enumerate prev value */
#endif
} FIELDTYPE;

typedef void (*Form_Hook)(FORM *);

	/***************************
	*  miscellaneous #defines  *
	***************************/

/* field justification */
#define NO_JUSTIFICATION	(0)
#define JUSTIFY_LEFT		(1)
#define JUSTIFY_CENTER		(2)
#define JUSTIFY_RIGHT		(3)

/* field options */
#define O_VISIBLE		(0x0001U)
#define O_ACTIVE		(0x0002U)
#define O_PUBLIC		(0x0004U)
#define O_EDIT			(0x0008U)
#define O_WRAP			(0x0010U)
#define O_BLANK			(0x0020U)
#define O_AUTOSKIP		(0x0040U)
#define O_NULLOK		(0x0080U)
#define O_PASSOK		(0x0100U)
#define O_STATIC		(0x0200U)

/* form options */
#define O_NL_OVERLOAD		(0x0001U)
#define O_BS_OVERLOAD		(0x0002U)

/* form driver commands */
#define REQ_NEXT_PAGE	 (KEY_MAX + 1)	/* move to next page		*/
#define REQ_PREV_PAGE	 (KEY_MAX + 2)	/* move to previous page	*/
#define REQ_FIRST_PAGE	 (KEY_MAX + 3)	/* move to first page		*/
#define REQ_LAST_PAGE	 (KEY_MAX + 4)	/* move to last page		*/

#define REQ_NEXT_FIELD	 (KEY_MAX + 5)	/* move to next field		*/
#define REQ_PREV_FIELD	 (KEY_MAX + 6)	/* move to previous field	*/
#define REQ_FIRST_FIELD	 (KEY_MAX + 7)	/* move to first field		*/
#define REQ_LAST_FIELD	 (KEY_MAX + 8)	/* move to last field		*/
#define REQ_SNEXT_FIELD	 (KEY_MAX + 9)	/* move to sorted next field	*/
#define REQ_SPREV_FIELD	 (KEY_MAX + 10)	/* move to sorted prev field	*/
#define REQ_SFIRST_FIELD (KEY_MAX + 11)	/* move to sorted first field	*/
#define REQ_SLAST_FIELD	 (KEY_MAX + 12)	/* move to sorted last field	*/
#define REQ_LEFT_FIELD	 (KEY_MAX + 13)	/* move to left to field	*/
#define REQ_RIGHT_FIELD	 (KEY_MAX + 14)	/* move to right to field	*/
#define REQ_UP_FIELD	 (KEY_MAX + 15)	/* move to up to field		*/
#define REQ_DOWN_FIELD	 (KEY_MAX + 16)	/* move to down to field	*/

#define REQ_NEXT_CHAR	 (KEY_MAX + 17)	/* move to next char in field	*/
#define REQ_PREV_CHAR	 (KEY_MAX + 18)	/* move to prev char in field	*/
#define REQ_NEXT_LINE	 (KEY_MAX + 19)	/* move to next line in field	*/
#define REQ_PREV_LINE	 (KEY_MAX + 20)	/* move to prev line in field	*/
#define REQ_NEXT_WORD	 (KEY_MAX + 21)	/* move to next word in field	*/
#define REQ_PREV_WORD	 (KEY_MAX + 22)	/* move to prev word in field	*/
#define REQ_BEG_FIELD	 (KEY_MAX + 23)	/* move to first char in field	*/
#define REQ_END_FIELD	 (KEY_MAX + 24)	/* move after last char in fld	*/
#define REQ_BEG_LINE	 (KEY_MAX + 25)	/* move to beginning of line	*/
#define REQ_END_LINE	 (KEY_MAX + 26)	/* move after last char in line	*/
#define REQ_LEFT_CHAR	 (KEY_MAX + 27)	/* move left in field		*/
#define REQ_RIGHT_CHAR	 (KEY_MAX + 28)	/* move right in field		*/
#define REQ_UP_CHAR	 (KEY_MAX + 29)	/* move up in field		*/
#define REQ_DOWN_CHAR	 (KEY_MAX + 30)	/* move down in field		*/

#define REQ_NEW_LINE	 (KEY_MAX + 31)	/* insert/overlay new line	*/
#define REQ_INS_CHAR	 (KEY_MAX + 32)	/* insert blank char at cursor	*/
#define REQ_INS_LINE	 (KEY_MAX + 33)	/* insert blank line at cursor	*/
#define REQ_DEL_CHAR	 (KEY_MAX + 34)	/* delete char at cursor	*/
#define REQ_DEL_PREV	 (KEY_MAX + 35)	/* delete char before cursor	*/
#define REQ_DEL_LINE	 (KEY_MAX + 36)	/* delete line at cursor	*/
#define REQ_DEL_WORD	 (KEY_MAX + 37)	/* delete word at cursor	*/
#define REQ_CLR_EOL	 (KEY_MAX + 38)	/* clear to end of line		*/
#define REQ_CLR_EOF	 (KEY_MAX + 39)	/* clear to end of field	*/
#define REQ_CLR_FIELD	 (KEY_MAX + 40)	/* clear entire field		*/
#define REQ_OVL_MODE	 (KEY_MAX + 41)	/* begin overlay mode		*/
#define REQ_INS_MODE	 (KEY_MAX + 42)	/* begin insert mode		*/
#define REQ_SCR_FLINE	 (KEY_MAX + 43)	/* scroll field forward a line	*/
#define REQ_SCR_BLINE	 (KEY_MAX + 44)	/* scroll field backward a line	*/
#define REQ_SCR_FPAGE	 (KEY_MAX + 45)	/* scroll field forward a page	*/
#define REQ_SCR_BPAGE	 (KEY_MAX + 46)	/* scroll field backward a page	*/
#define REQ_SCR_FHPAGE	 (KEY_MAX + 47) /* scroll field forward	 half page */
#define REQ_SCR_BHPAGE	 (KEY_MAX + 48) /* scroll field backward half page */
#define REQ_SCR_FCHAR	 (KEY_MAX + 49) /* horizontal scroll char	*/
#define REQ_SCR_BCHAR	 (KEY_MAX + 50) /* horizontal scroll char	*/
#define REQ_SCR_HFLINE	 (KEY_MAX + 51) /* horizontal scroll line	*/
#define REQ_SCR_HBLINE	 (KEY_MAX + 52) /* horizontal scroll line	*/
#define REQ_SCR_HFHALF	 (KEY_MAX + 53) /* horizontal scroll half line	*/
#define REQ_SCR_HBHALF	 (KEY_MAX + 54) /* horizontal scroll half line	*/

#define REQ_VALIDATION	 (KEY_MAX + 55)	/* validate field		*/
#define REQ_NEXT_CHOICE	 (KEY_MAX + 56)	/* display next field choice	*/
#define REQ_PREV_CHOICE	 (KEY_MAX + 57)	/* display prev field choice	*/

#define MIN_FORM_COMMAND (KEY_MAX + 1)	/* used by form_driver		*/
#define MAX_FORM_COMMAND (KEY_MAX + 57)	/* used by form_driver		*/

#if defined(MAX_COMMAND)
#  if (MAX_FORM_COMMAND > MAX_COMMAND)
#    error Something is wrong -- MAX_FORM_COMMAND is greater than MAX_COMMAND
#  elif (MAX_COMMAND != (KEY_MAX + 128))
#    error Something is wrong -- MAX_COMMAND is already inconsistently defined.
#  endif
#else
#  define MAX_COMMAND (KEY_MAX + 128)
#endif

	/*************************
	*  standard field types  *
	*************************/
extern NCURSES_EXPORT_VAR(FIELDTYPE *) TYPE_ALPHA;
extern NCURSES_EXPORT_VAR(FIELDTYPE *) TYPE_ALNUM;
extern NCURSES_EXPORT_VAR(FIELDTYPE *) TYPE_ENUM;
extern NCURSES_EXPORT_VAR(FIELDTYPE *) TYPE_INTEGER;
extern NCURSES_EXPORT_VAR(FIELDTYPE *) TYPE_NUMERIC;
extern NCURSES_EXPORT_VAR(FIELDTYPE *) TYPE_REGEXP;

	/************************************
	*  built-in additional field types  *
	*  They are not defined in SVr4     *
	************************************/
extern NCURSES_EXPORT_VAR(FIELDTYPE *) TYPE_IPV4;      /* Internet IP Version 4 address */

	/***********************
	*  FIELDTYPE routines  *
	***********************/
extern NCURSES_EXPORT(FIELDTYPE *) new_fieldtype (
		    bool (* const field_check)(FIELD *,const void *),
		    bool (* const char_check)(int,const void *));
extern NCURSES_EXPORT(FIELDTYPE *) link_fieldtype(
		    FIELDTYPE *, FIELDTYPE *);

extern NCURSES_EXPORT(int)	free_fieldtype (FIELDTYPE *);
extern NCURSES_EXPORT(int)	set_fieldtype_arg (FIELDTYPE *,
		    void * (* const make_arg)(va_list *),
		    void * (* const copy_arg)(const void *),
		    void (* const free_arg)(void *));
extern NCURSES_EXPORT(int)	 set_fieldtype_choice (FIELDTYPE *,
		    bool (* const next_choice)(FIELD *,const void *),
	      	    bool (* const prev_choice)(FIELD *,const void *));

	/*******************
	*  FIELD routines  *
	*******************/
extern NCURSES_EXPORT(FIELD *)	new_field (int,int,int,int,int,int);
extern NCURSES_EXPORT(FIELD *)	dup_field (FIELD *,int,int);
extern NCURSES_EXPORT(FIELD *)	link_field (FIELD *,int,int);

extern NCURSES_EXPORT(int)	free_field (FIELD *);
extern NCURSES_EXPORT(int)	field_info (const FIELD *,int *,int *,int *,int *,int *,int *);
extern NCURSES_EXPORT(int)	dynamic_field_info (const FIELD *,int *,int *,int *);
extern NCURSES_EXPORT(int)	set_max_field ( FIELD *,int);
extern NCURSES_EXPORT(int)	move_field (FIELD *,int,int);
extern NCURSES_EXPORT(int)	set_field_type (FIELD *,FIELDTYPE *,...);
extern NCURSES_EXPORT(int)	set_new_page (FIELD *,bool);
extern NCURSES_EXPORT(int)	set_field_just (FIELD *,int);
extern NCURSES_EXPORT(int)	field_just (const FIELD *);
extern NCURSES_EXPORT(int)	set_field_fore (FIELD *,chtype);
extern NCURSES_EXPORT(int)	set_field_back (FIELD *,chtype);
extern NCURSES_EXPORT(int)	set_field_pad (FIELD *,int);
extern NCURSES_EXPORT(int)	field_pad (const FIELD *);
extern NCURSES_EXPORT(int)	set_field_buffer (FIELD *,int,const char *);
extern NCURSES_EXPORT(int)	set_field_status (FIELD *,bool);
extern NCURSES_EXPORT(int)	set_field_userptr (FIELD *, void *);
extern NCURSES_EXPORT(int)	set_field_opts (FIELD *,Field_Options);
extern NCURSES_EXPORT(int)	field_opts_on (FIELD *,Field_Options);
extern NCURSES_EXPORT(int)	field_opts_off (FIELD *,Field_Options);

extern NCURSES_EXPORT(chtype)	field_fore (const FIELD *);
extern NCURSES_EXPORT(chtype)	field_back (const FIELD *);

extern NCURSES_EXPORT(bool)	new_page (const FIELD *);
extern NCURSES_EXPORT(bool)	field_status (const FIELD *);

extern NCURSES_EXPORT(void *)	field_arg (const FIELD *);

extern NCURSES_EXPORT(void *)	field_userptr (const FIELD *);

extern NCURSES_EXPORT(FIELDTYPE *)	field_type (const FIELD *);

extern NCURSES_EXPORT(char *)	field_buffer (const FIELD *,int);

extern NCURSES_EXPORT(Field_Options)	field_opts (const FIELD *);

	/******************
	*  FORM routines  *
	******************/

extern NCURSES_EXPORT(FORM *)	new_form (FIELD **);

extern NCURSES_EXPORT(FIELD **)	form_fields (const FORM *);
extern NCURSES_EXPORT(FIELD *)	current_field (const FORM *);

extern NCURSES_EXPORT(WINDOW *)	form_win (const FORM *);
extern NCURSES_EXPORT(WINDOW *)	form_sub (const FORM *);

extern NCURSES_EXPORT(Form_Hook)	form_init (const FORM *);
extern NCURSES_EXPORT(Form_Hook)	form_term (const FORM *);
extern NCURSES_EXPORT(Form_Hook)	field_init (const FORM *);
extern NCURSES_EXPORT(Form_Hook)	field_term (const FORM *);

extern NCURSES_EXPORT(int)	free_form (FORM *);
extern NCURSES_EXPORT(int)	set_form_fields (FORM *,FIELD **);
extern NCURSES_EXPORT(int)	field_count (const FORM *);
extern NCURSES_EXPORT(int)	set_form_win (FORM *,WINDOW *);
extern NCURSES_EXPORT(int)	set_form_sub (FORM *,WINDOW *);
extern NCURSES_EXPORT(int)	set_current_field (FORM *,FIELD *);
extern NCURSES_EXPORT(int)	field_index (const FIELD *);
extern NCURSES_EXPORT(int)	set_form_page (FORM *,int);
extern NCURSES_EXPORT(int)	form_page (const FORM *);
extern NCURSES_EXPORT(int)	scale_form (const FORM *,int *,int *);
extern NCURSES_EXPORT(int)	set_form_init (FORM *,Form_Hook);
extern NCURSES_EXPORT(int)	set_form_term (FORM *,Form_Hook);
extern NCURSES_EXPORT(int)	set_field_init (FORM *,Form_Hook);
extern NCURSES_EXPORT(int)	set_field_term (FORM *,Form_Hook);
extern NCURSES_EXPORT(int)	post_form (FORM *);
extern NCURSES_EXPORT(int)	unpost_form (FORM *);
extern NCURSES_EXPORT(int)	pos_form_cursor (FORM *);
extern NCURSES_EXPORT(int)	form_driver (FORM *,int);
# if NCURSES_WIDECHAR
extern NCURSES_EXPORT(int)	form_driver_w (FORM *,int,wchar_t);
# endif
extern NCURSES_EXPORT(int)	set_form_userptr (FORM *,void *);
extern NCURSES_EXPORT(int)	set_form_opts (FORM *,Form_Options);
extern NCURSES_EXPORT(int)	form_opts_on (FORM *,Form_Options);
extern NCURSES_EXPORT(int)	form_opts_off (FORM *,Form_Options);
extern NCURSES_EXPORT(int)	form_request_by_name (const char *);

extern NCURSES_EXPORT(const char *)	form_request_name (int);

extern NCURSES_EXPORT(void *)	form_userptr (const FORM *);

extern NCURSES_EXPORT(Form_Options)	form_opts (const FORM *);

extern NCURSES_EXPORT(bool)	data_ahead (const FORM *);
extern NCURSES_EXPORT(bool)	data_behind (const FORM *);

#if NCURSES_SP_FUNCS
extern NCURSES_EXPORT(FORM *)	NCURSES_SP_NAME(new_form) (SCREEN*, FIELD **);
#endif

#ifdef __cplusplus
  }
#endif

#endif	/* FORM_H */
