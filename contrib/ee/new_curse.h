/*
 |	new_curse.h
 |
 |	A subset of curses developed for use with ae.
 |
 |	written by Hugh Mahon
 |
 |	THIS MATERIAL IS PROVIDED "AS IS".  THERE ARE
 |	NO WARRANTIES OF ANY KIND WITH REGARD TO THIS
 |	MATERIAL, INCLUDING, BUT NOT LIMITED TO, THE
 |	IMPLIED WARRANTIES OF MERCHANTABILITY AND
 |	FITNESS FOR A PARTICULAR PURPOSE.  Neither
 |	Hewlett-Packard nor Hugh Mahon shall be liable
 |	for errors contained herein, nor for
 |	incidental or consequential damages in
 |	connection with the furnishing, performance or
 |	use of this material.  Neither Hewlett-Packard
 |	nor Hugh Mahon assumes any responsibility for
 |	the use or reliability of this software or
 |	documentation.  This software and
 |	documentation is totally UNSUPPORTED.  There
 |	is no support contract available.  Hewlett-
 |	Packard has done NO Quality Assurance on ANY
 |	of the program or documentation.  You may find
 |	the quality of the materials inferior to
 |	supported materials.
 |
 |	This software is not a product of Hewlett-Packard, Co., or any 
 |	other company.  No support is implied or offered with this software.
 |	You've got the source, and you're on your own.
 |
 |	This software may be distributed under the terms of Larry Wall's 
 |	Artistic license, a copy of which is included in this distribution. 
 |
 |	This notice must be included with this software and any derivatives.
 |
 |	Copyright (c) 1986, 1987, 1988, 1991, 1995 Hugh Mahon
 |	All are rights reserved.
 |
 */

#include <stdio.h>

#ifdef SYS5
#include <termio.h>
#else
#include <sgtty.h>
#include <fcntl.h>
#endif

#define KEY_BREAK	0401
#define KEY_DOWN	0402
#define KEY_UP		0403
#define KEY_LEFT	0404
#define KEY_RIGHT	0405
#define KEY_HOME	0406
#define KEY_BACKSPACE	0407
#define KEY_F0		0410
#define KEY_F(n) 	(KEY_F0+(n))
#define KEY_DL		0510
#define KEY_IL		0511
#define KEY_DC		0512
#define KEY_IC		0513
#define KEY_EIC		0514
#define KEY_CLEAR	0515
#define KEY_EOS		0516
#define KEY_EOL		0517
#define KEY_SF		0520
#define KEY_SR		0521
#define KEY_NPAGE	0522
#define KEY_PPAGE	0523
#define KEY_STAB	0524
#define KEY_CTAB	0525
#define KEY_CATAB	0526
#define KEY_ENTER	0527
#define KEY_SRESET	0530
#define KEY_RESET	0531
#define KEY_PRINT	0532
#define KEY_LL		0533
#define KEY_A1	0534
#define KEY_A3	0535
#define KEY_B2	0536
#define KEY_C1	0537
#define KEY_C3	0540
#define KEY_BTAB	0541
#define KEY_BEG	0542
#define KEY_CANCEL	0543
#define KEY_CLOSE	0544
#define KEY_COMMAND	0545
#define KEY_COPY	0546
#define KEY_CREATE	0547
#define KEY_END	0550
#define KEY_EXIT	0551
#define KEY_FIND	0552
#define KEY_HELP	0553
#define KEY_MARK	0554
#define KEY_MESSAGE	0555
#define KEY_MOVE	0556
#define KEY_NEXT	0557
#define KEY_OPEN	0560
#define KEY_OPTIONS	0561
#define KEY_PREVIOUS	0562
#define KEY_REDO	0563
#define KEY_REFERENCE	0564
#define KEY_REFRESH	0565
#define KEY_REPLACE	0566
#define KEY_RESTART	0567
#define KEY_RESUME	0570
#define KEY_SAVE	0571
#define KEY_SBEG	0572
#define KEY_SCANCEL	0573
#define KEY_SCOMMAND	0574
#define KEY_SCOPY	0575
#define KEY_SCREATE	0576
#define KEY_SDC	0577
#define KEY_SDL	0600
#define KEY_SELECT	0601
#define KEY_SEND	0602
#define KEY_SEOL	0603
#define KEY_SEXIT	0604
#define KEY_SFIND	0605
#define KEY_SHELP	0606
#define KEY_SHOME	0607
#define KEY_SIC	0610
#define KEY_SLEFT	0611
#define KEY_SMESSAGE	0612
#define KEY_SMOVE	0613
#define KEY_SNEXT	0614
#define KEY_SOPTIONS	0615
#define KEY_SPREVIOUS	0616
#define KEY_SPRINT	0617
#define KEY_SREDO	0620
#define KEY_SREPLACE	0621
#define KEY_SRIGHT	0622
#define KEY_SRSUME	0623
#define KEY_SSAVE	0624
#define KEY_SSUSPEND	0625
#define KEY_SUNDO	0626
#define KEY_SUSPEND	0627
#define KEY_UNDO	0630

#define TRUE 1
#define FALSE 0

#define A_STANDOUT 0001		/* standout mode		*/
#define A_NC_BIG5  0x0100	/* Handle Chinese Big5 characters	*/
#define SCROLL 1		/* text has been scrolled	*/
#define CLEAR  2		/* window has been cleared	*/
#define CHANGE 3		/* window has been changed	*/
#define UP 1			/* direction of scroll		*/
#define DOWN 2

struct _line {
	struct _line *next_screen;
	struct _line *prev_screen;
	char *row;
	char *attributes;
	int last_char;
	int changed;
	int scroll;
	int number;
	};

struct _line *top_of_win;

typedef struct WIND {
	int SR;		/* starting row		*/
	int SC;		/* starting column	*/
	int LC;		/* last column		*/
	int LX;		/* last cursor column position	*/
	int LY;		/* last cursor row position	*/
	int Attrib;	/* attributes active in window	*/
	int Num_lines;	/* number of lines		*/
	int Num_cols;	/* number of columns		*/
	int scroll_up;	/* number of lines moved	*/
	int scroll_down;
	int SCROLL_CLEAR;	/* indicates that window has been scrolled or cleared	*/
	struct _line *first_line;
	struct _line **line_array;
	} WINDOW;

extern WINDOW *curscr;
extern WINDOW *stdscr;

extern int LINES, COLS;

#if defined(__STDC__) || defined(__cplusplus)
#define P_(s) s
#else
#define P_(s) ()
#endif

extern void copy_window P_((WINDOW *origin, WINDOW *destination));
extern void reinitscr P_((int));
extern void initscr P_((void));
extern int Get_int P_((void));
extern int INFO_PARSE P_((void));
extern int AtoI P_((void));
extern void Key_Get P_((void));
extern void keys_vt100 P_((void));
extern struct _line *Screenalloc P_((int columns));
extern WINDOW *newwin P_((int lines, int cols, int start_l, int start_c));
extern int Operation P_((int Temp_Stack[], int place));
extern void Info_Out P_((char *string, int p_list[], int place));
extern void wmove P_((WINDOW *window, int row, int column));
extern void clear_line P_((struct _line *line, int column, int cols));
extern void werase P_((WINDOW *window));
extern void wclrtoeol P_((WINDOW *window));
extern void wrefresh P_((WINDOW *window));
extern void touchwin P_((WINDOW *window));
extern void wnoutrefresh P_((WINDOW *window));
extern void flushinp P_((void));
extern void ungetch P_((int c));
extern int wgetch P_((WINDOW *window));
extern void Clear P_((int));
extern int Get_key P_((int first_char));
extern void waddch P_((WINDOW *window, int c));
extern void winsertln P_((WINDOW *window));
extern void wdeleteln P_((WINDOW *window));
extern void wclrtobot P_((WINDOW *window));
extern void wstandout P_((WINDOW *window));
extern void wstandend P_((WINDOW *window));
extern void waddstr P_((WINDOW *window, char *string));
extern void clearok P_((WINDOW *window, int flag));
extern void echo P_((void));
extern void noecho P_((void));
extern void raw P_((void));
extern void noraw P_((void));
extern void nl P_((void));
extern void nonl P_((void));
extern void saveterm P_((void));
extern void fixterm P_((void));
extern void resetterm P_((void));
extern void nodelay P_((WINDOW *window, int flag));
extern void idlok P_((WINDOW *window, int flag));
extern void keypad P_((WINDOW *window, int flag));
extern void savetty P_((void));
extern void resetty P_((void));
extern void endwin P_((void));
extern void delwin P_((WINDOW *window));
extern void wprintw P_((WINDOW *window, const char* format, ...));
extern void iout P_((WINDOW *window, int value));
extern int Comp_line P_((struct _line *line1, struct _line *line2));
extern struct _line *Insert_line P_((int row, int end_row, WINDOW *window));
extern struct _line *Delete_line P_((int row, int end_row, WINDOW *window));
extern void CLEAR_TO_EOL P_((WINDOW *window, int row, int column));
extern int check_delete P_((WINDOW *window, int line, int offset, struct _line *pointer_new, struct _line *pointer_old));
extern int check_insert P_((WINDOW *window, int line, int offset, struct _line *pointer_new, struct _line *pointer_old));
extern void doupdate P_((void));
extern void Position P_((WINDOW *window, int row, int col));
extern void Char_del P_((char *line, char *attrib, int offset, int maxlen));
extern void Char_ins P_((char *line, char *attrib, int newc, int newatt, int offset, int maxlen));
extern void attribute_on P_((void));
extern void attribute_off P_((void));
extern void Char_out P_((int newc, int newatt, char *line, char *attrib, int offset));

extern void nc_setattrib P_((int));
extern void nc_clearattrib P_((int));
#undef P_

