/*
 |	new_curse.c
 |
 |	A subset of curses developed for use with ae.
 |
 |	written by Hugh Mahon
 |
 |      Copyright (c) 1986, 1987, 1988, 1991, 1992, 1993, 1994, 1995, 2009 Hugh Mahon
 |      All rights reserved.
 |      
 |      Redistribution and use in source and binary forms, with or without
 |      modification, are permitted provided that the following conditions
 |      are met:
 |      
 |          * Redistributions of source code must retain the above copyright
 |            notice, this list of conditions and the following disclaimer.
 |          * Redistributions in binary form must reproduce the above
 |            copyright notice, this list of conditions and the following
 |            disclaimer in the documentation and/or other materials provided
 |            with the distribution.
 |      
 |      THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 |      "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 |      LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 |      FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 |      COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 |      INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 |      BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 |      LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 |      CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 |      LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 |      ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 |      POSSIBILITY OF SUCH DAMAGE.
 |
 |	
 |	All are rights reserved.
 |
 |	$Header: /home/hugh/sources/old_ae/RCS/new_curse.c,v 1.54 2002/09/21 00:47:14 hugh Exp $
 |
 */

char *copyright_message[] = { "Copyright (c) 1986, 1987, 1988, 1991, 1992, 1993, 1994, 1995, 2009 Hugh Mahon",
				"All rights are reserved."};

char * new_curse_name= "@(#) new_curse.c $Revision: 1.54 $";

#include "new_curse.h"
#include <signal.h>
#include <fcntl.h>

#ifdef SYS5
#include <string.h>
#else
#include <strings.h>
#endif

#ifdef BSD_SELECT
#include <sys/types.h>
#include <sys/time.h>

#ifdef SLCT_HDR
#include <sys/select.h>  /* on AIX */
#endif /* SLCT_HDR */

#endif /* BSD_SELECT */

#ifdef HAS_STDLIB
#include <stdlib.h>
#endif

#if defined(__STDC__)
#include <stdarg.h>
#else
#include <varargs.h>
#endif

#ifdef HAS_UNISTD
#include <unistd.h>
#endif

#ifdef HAS_SYS_IOCTL
#include <sys/ioctl.h>
#endif


WINDOW *curscr;
static WINDOW *virtual_scr;
WINDOW *stdscr;
WINDOW *last_window_refreshed;

#ifdef TIOCGWINSZ
	struct winsize ws;
#endif

#define min(a, b)	(a < b ? a : b)
#define highbitset(a)	((a) & 0x80)

#ifndef CAP
#define String_Out(table, stack, place) Info_Out(table, stack, place)
#else
#define String_Out(table, stack, place) Cap_Out(table, stack, place)
#endif

#define bw__ 0	/* booleans	*/
#define am__ 1
#define xb__ 2
#define xs__ 3	/* hp glitch (standout not erased by overwrite)	*/
#define xn__ 4
#define eo__ 5
#define gn__ 6	/* generic type terminal	*/
#define hc__ 7	/* hardcopy terminal		*/
#define km__ 8
#define hs__ 9
#define in__ 10
#define da__ 11
#define db__ 12
#define mi__ 13	/* safe to move during insert mode	*/
#define ms__ 14	/* safe to move during standout mode	*/
#define os__ 15
#define es__ 16
#define xt__ 17
#define hz__ 18	/* hazeltine glitch	*/
#define ul__ 19
#define xo__ 20
#define chts__ 21
#define nxon__ 22
#define nrrmc__ 23
#define npc__ 24
#define mc5i__ 25

#define co__ 0	/* number of columns	*/	/* numbers		*/
#define it__ 1	/* spaces per tab	*/
#define li__ 2	/* number of lines	*/
#define lm__ 3
#define sg__ 4	/* magic cookie glitch	*/
#define pb__ 5
#define vt__ 6
#define ws__ 7

#define cols__ 0
#define lines__ 2
#define xmc__ 4
#define vt__ 6
#define wsl__ 7
#define nlab__ 8
#define lh__ 9
#define lw__ 10

#define bt__ 0	/* back tab		*/	/* strings	*/
#define bl__ 1	/* bell			*/
#define cr__ 2	/* carriage return	*/
#define cs__ 3	/* change scroll region	*/
#define ct__ 4	/* clear all tab stops	*/
#define cl__ 5	/* clear screen and home cursor	*/
#define ce__ 6	/* clear to end of line	*/
#define cd__ 7	/* clear to end of display	*/
#define ch__ 8	/* set cursor column	*/
#define CC__ 9	/* term, settable cmd char in 	*/
#define cm__ 10	/* screen rel cursor motion, row, column	*/
#define do__ 11	/* down one line	*/
#define ho__ 12	/* home cursor	*/
#define vi__ 13	/* make cursor invisible	*/
#define le__ 14	/* move cursor left one space	*/
#define CM__ 15	/* memory rel cursor addressing	*/
#define ve__ 16	/* make cursor appear normal	*/
#define nd__ 17	/* non-destructive space (cursor right)	*/
#define ll__ 18	/* last line, first col	*/
#define up__ 19	/* cursor up		*/
#define vs__ 20
#define dc__ 21	/* delete character	*/
#define dl__ 22	/* delete line		*/
#define ds__ 23
#define hd__ 24
#define as__ 25
#define mb__ 26
#define md__ 27	/* turn on bold		*/
#define ti__ 28
#define dm__ 29	/* turn on delete mode	*/
#define mh__ 30	/* half bright mode	*/
#define im__ 31	/* insert mode		*/
#define mk__ 32
#define mp__ 33
#define mr__ 34
#define so__ 35	/* enter standout mode	*/
#define us__ 36
#define ec__ 37
#define ae__ 38
#define me__ 39
#define te__ 40
#define ed__ 41
#define ei__ 42	/* exit insert mode	*/
#define se__ 43	/* exit standout mode	*/
#define ue__ 44
#define vb__ 45
#define ff__ 46
#define fs__ 47
#define i1__ 48
#define i2__ 49
#define i3__ 50
#define if__ 51
#define ic__ 52
#define al__ 53
#define ip__ 54
#define kb__ 55		/* backspace key	*/
#define ka__ 56
#define kC__ 57
#define kt__ 58
#define kD__ 59
#define kL__ 60
#define kd__ 61
#define kM__ 62
#define kE__ 63
#define kS__ 64
#define k0__ 65
#define k1__ 66
#define kf10__ 67
#define k2__ 68
#define k3__ 69
#define k4__ 70
#define k5__ 71
#define k6__ 72
#define k7__ 73
#define k8__ 74
#define k9__ 75
#define kh__ 76
#define kI__ 77
#define kA__ 78
#define kl__ 79
#define kH__ 80
#define kN__ 81
#define kP__ 82
#define kr__ 83
#define kF__ 84
#define kR__ 85
#define kT__ 86
#define ku__ 87	/* key up	*/
#define ke__ 88
#define ks__ 89
#define l0__ 90
#define l1__ 91
#define la__ 92
#define l2__ 93
#define l3__ 94
#define l4__ 95
#define l5__ 96
#define l6__ 97
#define l7__ 98
#define l8__ 99
#define l9__ 100
#define mo__ 101
#define mm__ 102
#define nw__ 103
#define pc__ 104
#define DC__ 105
#define DL__ 106
#define DO__ 107
#define IC__ 118
#define SF__ 109
#define AL__ 110
#define LE__ 111
#define RI__ 112
#define SR__ 113
#define UP__ 114
#define pk__ 115
#define pl__ 116
#define px__ 117
#define ps__ 118
#define pf__ 119
#define po__ 120
#define rp__ 121
#define r1__ 122
#define r2__ 123
#define r3__ 124
#define rf__ 125
#define rc__ 126
#define cv__ 127
#define sc__ 128
#define sf__ 129
#define sr__ 130
#define sa__ 131	/* sgr	*/
#define st__ 132
#define wi__ 133
#define ta__ 134
#define ts__ 135
#define uc__ 136
#define hu__ 137
#define iP__ 138
#define K1__ 139
#define K2__ 140
#define K3__ 141
#define K4__ 142
#define K5__ 143
#define pO__ 144
#define ml__ 145
#define mu__ 146
#define rmp__ 145
#define acsc__ 146
#define pln__ 147
#define kcbt__ 148
#define smxon__ 149
#define rmxon__ 150
#define smam__ 151
#define rmam__ 152
#define xonc__ 153
#define xoffc__ 154
#define enacs__ 155
#define smln__ 156
#define rmln__ 157
#define kbeg__ 158
#define kcan__ 159
#define kclo__ 160
#define kcmd__ 161
#define kcpy__ 162
#define kcrt__ 163
#define kend__ 164
#define kent__ 165
#define kext__ 166
#define kfnd__ 167
#define khlp__ 168
#define kmrk__ 169
#define kmsg__ 170
#define kmov__ 171
#define knxt__ 172
#define kopn__ 173
#define kopt__ 174
#define kprv__ 175
#define kprt__ 176
#define krdo__ 177
#define kref__ 178
#define krfr__ 179
#define krpl__ 180
#define krst__ 181
#define kres__ 182
#define ksav__ 183
#define kspd__ 184
#define kund__ 185
#define kBEG__ 186
#define kCAN__ 187
#define kCMD__ 188
#define kCPY__ 189
#define kCRT__ 190
#define kDC__ 191
#define kDL__ 192
#define kslt__ 193
#define kEND__ 194
#define kEOL__ 195
#define kEXT__ 196
#define kFND__ 197
#define kHLP__ 198
#define kHOM__ 199
#define kIC__ 200
#define kLFT__ 201
#define kMSG__ 202
#define kMOV__ 203
#define kNXT__ 204
#define kOPT__ 205
#define kPRV__ 206
#define kPRT__ 207
#define kRDO__ 208
#define kRPL__ 209
#define kRIT__ 210
#define kRES__ 211
#define kSAV__ 212
#define kSPD__ 213
#define kUND__ 214
#define rfi__ 215
#define kf11__ 216
#define kf12__ 217
#define kf13__ 218
#define kf14__ 219
#define kf15__ 220
#define kf16__ 221
#define kf17__ 222
#define kf18__ 223
#define kf19__ 224
#define kf20__ 225
#define kf21__ 226
#define kf22__ 227
#define kf23__ 228
#define kf24__ 229
#define kf25__ 230
#define kf26__ 231
#define kf27__ 232
#define kf28__ 233
#define kf29__ 234
#define kf30__ 235
#define kf31__ 236
#define kf32__ 237
#define kf33__ 238
#define kf34__ 239
#define kf35__ 240
#define kf36__ 241
#define kf37__ 242
#define kf38__ 243
#define kf39__ 244
#define kf40__ 245
#define kf41__ 246
#define kf42__ 247
#define kf43__ 248
#define kf44__ 249
#define kf45__ 250
#define kf46__ 251
#define kf47__ 252
#define kf48__ 253
#define kf49__ 254
#define kf50__ 255
#define kf51__ 256
#define kf52__ 257
#define kf53__ 258
#define kf54__ 259
#define kf55__ 260
#define kf56__ 261
#define kf57__ 262
#define kf58__ 263
#define kf59__ 264
#define kf60__ 265
#define kf61__ 266
#define kf62__ 267
#define kf63__ 268
#define el1__ 269
#define mgc__ 270
#define smgl__ 271
#define smgr__ 272

#ifdef CAP
char *Boolean_names[] = {
"bw", "am", "xb", "xs", "xn", "eo", "gn", "hc", "km", "hs", "in", "da", "db", 
"mi", "ms", "os", "es", "xt", "hz", "ul", "xo", "HC", "nx", "NR", "NP", "5i"
}; 

char *Number_names[] = { 
"co#", "it#", "li#", "lm#", "sg#", "pb#", "vt#", "ws#", "Nl#", "lh#", "lw#"
};

char *String_names[] = {
"bt=", "bl=", "cr=", "cs=", "ct=", "cl=", "ce=", "cd=", "ch=", "CC=", "cm=", 
"do=", "ho=", "vi=", "le=", "CM=", "ve=", "nd=", "ll=", "up=", "vs=", "dc=", 
"dl=", "ds=", "hd=", "as=", "mb=", "md=", "ti=", "dm=", "mh=", "im=", "mk=", 
"mp=", "mr=", "so=", "us=", "ec=", "ae=", "me=", "te=", "ed=", "ei=", "se=", 
"ue=", "vb=", "ff=", "fs=", "i1=", "i2=", "i3=", "if=", "ic=", "al=", "ip=", 
"kb=", "ka=", "kC=", "kt=", "kD=", "kL=", "kd=", "kM=", "kE=", "kS=", "k0=", 
"k1=", "k;=", "k2=", "k3=", "k4=", "k5=", "k6=", "k7=", "k8=", "k9=", "kh=", 
"kI=", "kA=", "kl=", "kH=", "kN=", "kP=", "kr=", "kF=", "kR=", "kT=", "ku=", 
"ke=", "ks=", "l0=", "l1=", "la=", "l2=", "l3=", "l4=", "l5=", "l6=", "l7=", 
"l8=", "l9=", "mo=", "mm=", "nw=", "pc=", "DC=", "DL=", "DO=", "IC=", "SF=", 
"AL=", "LE=", "RI=", "SR=", "UP=", "pk=", "pl=", "px=", "ps=", "pf=", "po=", 
"rp=", "r1=", "r2=", "r3=", "rf=", "rc=", "cv=", "sc=", "sf=", "sr=", "sa=", 
"st=", "wi=", "ta=", "ts=", "uc=", "hu=", "iP=", "K1=", "K3=", "K2=", "K4=", 
"K5=", "pO=", "rP=", "ac=", "pn=", "kB=", "SX=", "RX=", "SA=", "RA=", "XN=", 
"XF=", "eA=", "LO=", "LF=", "@1=", "@2=", "@3=", "@4=", "@5=", "@6=", "@7=", 
"@8=", "@9=", "@0=", "%1=", "%2=", "%3=", "%4=", "%5=", "%6=", "%7=", "%8=", 
"%9=", "%0=", "&1=", "&2=", "&3=", "&4=", "&5=", "&6=", "&7=", "&8=", "&9=", 
"&0=", "*1=", "*2=", "*3=", "*4=", "*5=", "*6=", "*7=", "*8=", "*9=", "*0=", 
"#1=", "#2=", "#3=", "#4=", "%a=", "%b=", "%c=", "%d=", "%e=", "%f=", "%g=", 
"%h=", "%i=", "%j=", "!1=", "!2=", "!3=", "RF=", "F1=", "F2=", "F3=", "F4=", 
"F5=", "F6=", "F7=", "F8=", "F9=", "FA=", "FB=", "FC=", "FD=", "FE=", "FF=", 
"FG=", "FH=", "FI=", "FJ=", "FK=", "FL=", "FM=", "FN=", "FO=", "FP=", "FQ=", 
"FR=", "FS=", "FT=", "FU=", "FV=", "FW=", "FX=", "FY=", "FZ=", "Fa=", "Fb=", 
"Fc=", "Fd=", "Fe=", "Ff=", "Fg=", "Fh=", "Fi=", "Fj=", "Fk=", "Fl=", "Fm=", 
"Fn=", "Fo=", "Fp=", "Fq=", "Fr=", "cb=", "MC=", "ML=", "MR="
};
#endif

char *new_curse = "October 1987";

char in_buff[100];	/* buffer for ungetch			*/
int bufp;		/* next free position in in_buff	*/

char *TERMINAL_TYPE = NULL; /* terminal type to be gotten from environment	*/
int CFOUND = FALSE;
int Data_Line_len = 0;
int Max_Key_len;	/* max length of a sequence sent by a key	*/
char *Data_Line = NULL;
char *TERM_PATH = NULL;
char *TERM_data_ptr = NULL;
char *Term_File_name = NULL;	/* name of file containing terminal description	*/
FILE *TFP;		/* file pointer to file with terminal des.	*/
int Fildes;		/* file descriptor for terminfo file		*/
int STAND = FALSE;	/* is standout mode activated?			*/
int TERM_INFO = FALSE;	/* is terminfo being used (TRUE), or termcap (FALSE) */
int Time_Out;	/* set when time elapsed while trying to read function key */
int Curr_x;		/* current x position on screen			*/
int Curr_y;		/* current y position on the screen		*/
int LINES;
int COLS;
int Move_It;		/* flag to move cursor if magic cookie glitch	*/
int initialized = FALSE;	/* tells whether new_curse is initialized	*/
float speed;
float chars_per_millisecond;
int Repaint_screen;	/* if an operation to change screen impossible, repaint screen	*/
int Intr;		/* storeage for interrupt character		*/
int Parity;		/* 0 = no parity, 1 = odd parity, 2 = even parity */
int Noblock;		/* for BSD systems				*/
int Num_bits;	/* number of bits per character	*/
int Flip_Bytes;	/* some systems have byte order reversed	*/
int interrupt_flag = FALSE;	/* set true if SIGWINCH received	*/

#ifndef CAP
char *Strings;
#endif

#if !defined(TERMCAP)
#define TERMCAP "/etc/termcap"
#endif 

struct KEYS {
	int length;	/* length of string sent by key			*/
	char *string;	/* string sent by key				*/
	int value;	/* CURSES value of key (9-bit)			*/
	};

struct KEY_STACK {
	struct KEYS *element;
	struct KEY_STACK *next;
	};

struct KEY_STACK *KEY_TOS = NULL;
struct KEY_STACK *KEY_POINT;

/*
 |
 |	Not all systems have good terminal information, so we will define 
 |	keyboard information here for the most widely used terminal type, 
 |	the VT100.
 |
 */

struct KEYS vt100[] = 
	{
		{ 3, "\033[A", 0403 },	/* key up 	*/
		{ 3, "\033[C", 0405 },	/* key right	*/
		{ 3, "\033[D", 0404 },	/* key left	*/

		{ 4, "\033[6~", 0522 },	/* key next page	*/
		{ 4, "\033[5~", 0523 },	/* key prev page	*/
		{ 3, "\033[[", 0550 },	/* key end	*/
		{ 3, "\033[@", 0406 },	/* key home	*/
		{ 4, "\033[2~", 0513 },	/* key insert char	*/

		{ 3, "\033[y", 0410 },	/* key F0	*/
		{ 3, "\033[P", 0411 },	/* key F1	*/
		{ 3, "\033[Q", 0412 },	/* key F2	*/
		{ 3, "\033[R", 0413 },	/* key F3	*/
		{ 3, "\033[S", 0414 },	/* key F4	*/
		{ 3, "\033[t", 0415 },	/* key F5	*/
		{ 3, "\033[u", 0416 },	/* key F6	*/
		{ 3, "\033[v", 0417 },	/* key F7	*/
		{ 3, "\033[l", 0420 },	/* key F8	*/
		{ 3, "\033[w", 0421 },	/* key F9	*/
		{ 3, "\033[x", 0422 },	/* key F10	*/

		{ 5, "\033[10~", 0410 },	/* key F0	*/
		{ 5, "\033[11~", 0411 },	/* key F1	*/
		{ 5, "\033[12~", 0412 },	/* key F2	*/
		{ 5, "\033[13~", 0413 },	/* key F3	*/
		{ 5, "\033[14~", 0414 },	/* key F4	*/
		{ 5, "\033[15~", 0415 },	/* key F5	*/
		{ 5, "\033[17~", 0416 },	/* key F6	*/
		{ 5, "\033[18~", 0417 },	/* key F7	*/
		{ 5, "\033[19~", 0420 },	/* key F8	*/
		{ 5, "\033[20~", 0421 },	/* key F9	*/
		{ 5, "\033[21~", 0422 },	/* key F10	*/
		{ 5, "\033[23~", 0423 },	/* key F11	*/
		{ 5, "\033[24~", 0424 },	/* key F12	*/
		{ 3, "\033[q", 0534 },	/* ka1 upper-left of keypad	*/
		{ 3, "\033[s", 0535 },	/* ka3 upper-right of keypad	*/
		{ 3, "\033[r", 0536 },	/* kb2 center of keypad	*/
 		{ 3, "\033[p", 0537 },	/* kc1 lower-left of keypad	*/
		{ 3, "\033[n", 0540 },	/* kc3 lower-right of keypad	*/

		/*
		 |	The following are the same keys as above, but with 
		 |	a different character following the escape char.
		 */

		{ 3, "\033OA", 0403 },	/* key up 	*/
		{ 3, "\033OC", 0405 },	/* key right	*/
		{ 3, "\033OD", 0404 },	/* key left	*/
		{ 3, "\033OB", 0402 },	/* key down	*/
		{ 4, "\033O6~", 0522 },	/* key next page	*/
		{ 4, "\033O5~", 0523 },	/* key prev page	*/
		{ 3, "\033O[", 0550 },	/* key end	*/
		{ 3, "\033O@", 0406 },	/* key home	*/
		{ 4, "\033O2~", 0513 },	/* key insert char	*/

		{ 3, "\033Oy", 0410 },	/* key F0	*/
		{ 3, "\033OP", 0411 },	/* key F1	*/
		{ 3, "\033OQ", 0412 },	/* key F2	*/
		{ 3, "\033OR", 0413 },	/* key F3	*/
		{ 3, "\033OS", 0414 },	/* key F4	*/
		{ 3, "\033Ot", 0415 },	/* key F5	*/
		{ 3, "\033Ou", 0416 },	/* key F6	*/
		{ 3, "\033Ov", 0417 },	/* key F7	*/
		{ 3, "\033Ol", 0420 },	/* key F8	*/
		{ 3, "\033Ow", 0421 },	/* key F9	*/
		{ 3, "\033Ox", 0422 },	/* key F10	*/

		{ 5, "\033O10~", 0410 },	/* key F0	*/
		{ 5, "\033O11~", 0411 },	/* key F1	*/
		{ 5, "\033O12~", 0412 },	/* key F2	*/
		{ 5, "\033O13~", 0413 },	/* key F3	*/
		{ 5, "\033O14~", 0414 },	/* key F4	*/
		{ 5, "\033O15~", 0415 },	/* key F5	*/
		{ 5, "\033O17~", 0416 },	/* key F6	*/
		{ 5, "\033O18~", 0417 },	/* key F7	*/
		{ 5, "\033O19~", 0420 },	/* key F8	*/
		{ 5, "\033O20~", 0421 },	/* key F9	*/
		{ 5, "\033O21~", 0422 },	/* key F10	*/
		{ 5, "\033O23~", 0423 },	/* key F11	*/
		{ 5, "\033O24~", 0424 },	/* key F12	*/
		{ 3, "\033Oq", 0534 },	/* ka1 upper-left of keypad	*/
		{ 3, "\033Os", 0535 },	/* ka3 upper-right of keypad	*/
		{ 3, "\033Or", 0536 },	/* kb2 center of keypad	*/
 		{ 3, "\033Op", 0537 },	/* kc1 lower-left of keypad	*/
		{ 3, "\033On", 0540 },	/* kc3 lower-right of keypad	*/

		{ 0, "", 0 }	/* end	*/
	};

struct Parameters {
	int value;
	struct Parameters *next;
	};

int Key_vals[] = { 
	0407, 0526, 0515, 0525, 0512, 0510, 0402, 0514, 0517, 0516, 0410, 0411, 
	0422, 0412, 0413, 0414, 0415, 0416, 0417, 0420, 0421, 0406, 0513, 0511, 
	0404, 0533, 0522, 0523, 0405, 0520, 0521, 0524, 0403, 
	0534, 0535, 0536, 0537, 0540, 0541, 0542, 0543, 0544, 0545, 0546, 0547, 
	0550, 0527, 0551, 0552, 0553, 0554, 0555, 0556, 0557, 0560, 0561, 0562, 
	0532, 0563, 0564, 0565, 0566, 0567, 0570, 0571, 0627, 0630, 0572, 0573, 
	0574, 0575, 0576, 0577, 0600, 0601, 0602, 0603, 0604, 0605, 0606, 0607, 
	0610, 0611, 0612, 0613, 0614, 0615, 0616, 0617, 0620, 0621, 0622, 0623, 
	0624, 0625, 0626, 0423, 0424, 0425, 0426, 0427, 0430, 0431, 
	0432, 0433, 0434, 0435, 0436, 0437, 0440, 0441, 0442, 0443, 0444, 0445, 
	0446, 0447, 0450, 0451, 0452, 0453, 0454, 0455, 0456, 0457, 0460, 0461, 
	0462, 0463, 0464, 0465, 0466, 0467, 0470, 0471, 0472, 0473, 0474, 0475, 
	0476, 0477, 0500, 0501, 0502, 0503, 0504, 0505, 0506, 0507
};

int attributes_set[9];

static int nc_attributes = 0;	/* global attributes for new_curse to observe */

#ifdef SYS5
struct termio Terminal;
struct termio Saved_tty;
#else
struct sgttyb Terminal;
struct sgttyb Saved_tty;
#endif

char *tc_;

int Booleans[128];	
int Numbers[128];
char *String_table[1024];

int *virtual_lines;

static char nc_scrolling_ability = FALSE;

char *terminfo_path[] = {
        "/usr/lib/terminfo", 
        "/usr/share/lib/terminfo", 
        "/usr/share/terminfo", 
        NULL 
        };

#ifdef CAP

#if defined(__STDC__) || defined(__cplusplus)
#define P_(s) s
#else
#define P_(s) ()
#endif /* __STDC__ */

int tc_Get_int P_((int));
void CAP_PARSE P_((void));
void Find_term P_((void));

#undef P_

#endif /* CAP */


#ifndef __STDC__
#ifndef HAS_STDLIB
extern char *fgets();
extern char *malloc();
extern char *getenv();
FILE *fopen();			/* declaration for open function	*/
#endif /* HAS_STDLIB */
#endif /* __STDC__ */

#ifdef SIGWINCH

/*
 |	Copy the contents of one window to another.
 */

void 
copy_window(origin, destination)
WINDOW *origin, *destination;
{
	int row, column;
	struct _line *orig, *dest;

	orig = origin->first_line;
	dest = destination->first_line;

	for (row = 0; 
		row < (min(origin->Num_lines, destination->Num_lines)); 
			row++)
	{
		for (column = 0; 
		    column < (min(origin->Num_cols, destination->Num_cols)); 
			column++)
		{
			dest->row[column] = orig->row[column];
			dest->attributes[column] = orig->attributes[column];
		}
		dest->changed = orig->changed;
		dest->scroll = orig->scroll;
		dest->last_char = min(orig->last_char, destination->Num_cols);
		orig = orig->next_screen;
		dest = dest->next_screen;
	}
	destination->LX = min((destination->Num_cols - 1), origin->LX);
	destination->LY = min((destination->Num_lines - 1), origin->LY);
	destination->Attrib = origin->Attrib;
	destination->scroll_up = origin->scroll_up;
	destination->scroll_down = origin->scroll_down;
	destination->SCROLL_CLEAR = origin->SCROLL_CLEAR;
}

void 
reinitscr(foo)
int foo; 
{
	WINDOW *local_virt;
	WINDOW *local_std;
	WINDOW *local_cur;

	signal(SIGWINCH, reinitscr);
#ifdef TIOCGWINSZ
	if (ioctl(0, TIOCGWINSZ, &ws) >= 0)
	{
		if (ws.ws_row == LINES && ws.ws_col == COLS) 
			return;
		if (ws.ws_row > 0) 
			LINES = ws.ws_row;
		if (ws.ws_col > 0) 
			COLS = ws.ws_col;
	}
#endif /* TIOCGWINSZ */
	local_virt = newwin(LINES, COLS, 0, 0);
	local_std = newwin(LINES, COLS, 0, 0);
	local_cur = newwin(LINES, COLS, 0, 0);
	copy_window(virtual_scr, local_virt);
	copy_window(stdscr, local_std);
	copy_window(curscr, local_cur);
	delwin(virtual_scr);
	delwin(stdscr);
	delwin(curscr);
	virtual_scr = local_virt;
	stdscr = local_std;
	curscr = local_cur;
	free(virtual_lines);
	virtual_lines = (int *) malloc(LINES * (sizeof(int)));
	interrupt_flag = TRUE;
}
#endif /* SIGWINCH */

void 
initscr()		/* initialize terminal for operations	*/
{
	int value;
	int counter;
	char *lines_string;
	char *columns_string;
#ifdef CAP
	char *pointer;
#endif /* CAP */

#ifdef DIAG
printf("starting initscr \n");fflush(stdout);
#endif
	if (initialized)
		return;
#ifdef BSD_SELECT
	setbuf(stdin, NULL);
#endif /* BSD_SELECT */
	Flip_Bytes = FALSE;
	Parity = 0;
	Time_Out = FALSE;
	bufp = 0;
	Move_It = FALSE;
	Noblock = FALSE;
#ifdef SYS5
	value = ioctl(0, TCGETA, &Terminal);
	if (Terminal.c_cflag & PARENB)
	{
		if (Terminal.c_cflag & PARENB)
			Parity = 1;
		else
			Parity = 2;
	}
	if ((Terminal.c_cflag & CS8) == CS8)
	{
		Num_bits = 8;
	}
	else if ((Terminal.c_cflag & CS7) == CS7)
		Num_bits = 7;
	else if ((Terminal.c_cflag & CS6) == CS6)
		Num_bits = 6;
	else
		Num_bits = 5;
	value = Terminal.c_cflag & 037;
	switch (value) {
	case 01:	speed = 50.0;
		break;
	case 02:	speed = 75.0;
		break;
	case 03:	speed = 110.0;
		break;
	case 04:	speed = 134.5;
		break;
	case 05:	speed = 150.0;
		break;
	case 06:	speed = 200.0;
		break;
	case 07:	speed = 300.0;
		break;
	case 010:	speed = 600.0;
		break;
	case 011:	speed = 900.0;
		break;
	case 012:	speed = 1200.0;
		break;
	case 013:	speed = 1800.0;
		break;
	case 014:	speed = 2400.0;
		break;
	case 015:	speed = 3600.0;
		break;
	case 016:	speed = 4800.0;
		break;
	case 017:	speed = 7200.0;
		break;
	case 020:	speed = 9600.0;
		break;
	case 021:	speed = 19200.0;
		break;
	case 022:	speed = 38400.0;
		break;
	default:	speed = 0.0;
	}
#else
	value = ioctl(0, TIOCGETP, &Terminal);
	if (Terminal.sg_flags & EVENP)
		Parity = 2;
	else if (Terminal.sg_flags & ODDP)
		Parity = 1;
	value = Terminal.sg_ospeed;
	switch (value) {
	case 01:	speed = 50.0;
		break;
	case 02:	speed = 75.0;
		break;
	case 03:	speed = 110.0;
		break;
	case 04:	speed = 134.5;
		break;
	case 05:	speed = 150.0;
		break;
	case 06:	speed = 200.0;
		break;
	case 07:	speed = 300.0;
		break;
	case 010:	speed = 600.0;
		break;
	case 011:	speed = 1200.0;
		break;
	case 012:	speed = 1800.0;
		break;
	case 013:	speed = 2400.0;
		break;
	case 014:	speed = 4800.0;
		break;
	case 015:	speed = 9600.0;
		break;
	default:	speed = 0.0;
	}
#endif
	chars_per_millisecond = (0.001 * speed) / 8.0;
	TERMINAL_TYPE = getenv("TERM");
	if (TERMINAL_TYPE == NULL)
	{
		printf("unknown terminal type\n");
		exit(0);
	}
#ifndef CAP
	Fildes = -1;
	TERM_PATH = getenv("TERMINFO");
	if (TERM_PATH != NULL)
	{
		Data_Line_len = 23 + strlen(TERM_PATH) + strlen(TERMINAL_TYPE);
		Term_File_name = malloc(Data_Line_len);
		sprintf(Term_File_name, "%s/%c/%s", TERM_PATH, *TERMINAL_TYPE, TERMINAL_TYPE);
		Fildes = open(Term_File_name, O_RDONLY);
		if (Fildes == -1)
		{
        		sprintf(Term_File_name, "%s/%x/%s", TERM_PATH, *TERMINAL_TYPE, TERMINAL_TYPE);
        		Fildes = open(Term_File_name, O_RDONLY);
		}
	}
	counter = 0;
	while ((Fildes == -1) && (terminfo_path[counter] != NULL))
	{
		TERM_PATH = terminfo_path[counter];
		Data_Line_len = 23 + strlen(TERM_PATH) + strlen(TERMINAL_TYPE);
		Term_File_name = malloc(Data_Line_len);
		sprintf(Term_File_name, "%s/%c/%s", TERM_PATH, *TERMINAL_TYPE, TERMINAL_TYPE);
		Fildes = open(Term_File_name, O_RDONLY);
		if (Fildes == -1)
		{
        		sprintf(Term_File_name, "%s/%x/%s", TERM_PATH, *TERMINAL_TYPE, TERMINAL_TYPE);
        		Fildes = open(Term_File_name, O_RDONLY);
		}
		counter++;
	}
	if (Fildes == -1)
	{
		free(Term_File_name);
		Term_File_name = NULL;
	}
	else
		TERM_INFO = INFO_PARSE();
#else
	/*
	 |	termcap information can be in the TERMCAP env variable, if so 
	 |	use that, otherwise check the /etc/termcap file
	 */
	if ((pointer = Term_File_name = getenv("TERMCAP")) != NULL)
	{
		if (*Term_File_name != '/')
			Term_File_name = TERMCAP;
	}
	else
	{
		Term_File_name = TERMCAP;
	}
	if ((TFP = fopen(Term_File_name, "r")) == NULL)
	{
		printf("unable to open %s file \n", TERMCAP);
		exit(0);
	}
 	for (value = 0; value < 1024; value++)	
		String_table[value] = NULL;
	for (value = 0; value < 128; value++)	
		Booleans[value] = 0;
	for (value = 0; value < 128; value++)	
		Numbers[value] = 0;
	Data_Line = malloc(512);
	if (pointer && *pointer != '/')
	{
		TERM_data_ptr = pointer;
		CAP_PARSE();
	}
	else
	{
		Find_term();
		CAP_PARSE();
	}
#endif
	if (String_table[pc__] == NULL) 
		String_table[pc__] = "\0";
	if ((String_table[cm__] == NULL) || (Booleans[hc__]))
	{
		fprintf(stderr, "sorry, unable to use this terminal type for screen editing\n");
		exit(0);
	}
	Key_Get();
	keys_vt100();
	LINES = Numbers[li__];
	COLS = Numbers[co__];
	if ((lines_string = getenv("LINES")) != NULL)
	{
		value = atoi(lines_string);
		if (value > 0)
			LINES = value;
	}
	if ((columns_string = getenv("COLUMNS")) != NULL)
	{
		value = atoi(columns_string);
		if (value > 0)
			COLS = value;
	}
#ifdef TIOCGWINSZ
	/*
	 |	get the window size
	 */
	if (ioctl(0, TIOCGWINSZ, &ws) >= 0)
	{
		if (ws.ws_row > 0)
			LINES = ws.ws_row;
		if (ws.ws_col > 0)
			COLS = ws.ws_col;
	}
#endif
	virtual_scr = newwin(LINES, COLS, 0, 0);
	stdscr = newwin(LINES, COLS, 0, 0);
	curscr = newwin(LINES, COLS, 0, 0);
	wmove(stdscr, 0, 0);
	werase(stdscr);
	Repaint_screen = TRUE;
	initialized = TRUE;
	virtual_lines = (int *) malloc(LINES * (sizeof(int)));

#ifdef SIGWINCH
	/*
	 |	reset size of windows and LINES and COLS if term window 
	 |	changes size
	 */
	signal(SIGWINCH, reinitscr);
#endif /* SIGWINCH */

	/*
	 |	check if scrolling is available
	 */

	nc_scrolling_ability = ((String_table[al__] != NULL) && 
				(String_table[dl__])) || ((String_table[cs__]) 
				&& (String_table[sr__]));

}

#ifndef CAP
int 
Get_int()		/* get a two-byte integer from the terminfo file */
{
	int High_byte;
	int Low_byte;
	int temp;

	Low_byte = *((unsigned char *) TERM_data_ptr++);
	High_byte = *((unsigned char *) TERM_data_ptr++);
	if (Flip_Bytes)
	{
		temp = Low_byte;
		Low_byte = High_byte;
		High_byte = temp;
	}
	if ((High_byte == 255) && (Low_byte == 255))
		return (-1);
	else
		return(Low_byte + (High_byte * 256));
}

int 
INFO_PARSE()		/* parse off the data in the terminfo data file	*/
{
	int offset;
	int magic_number = 0;
	int counter = 0;
	int Num_names = 0;
	int Num_bools = 0;
	int Num_ints = 0;
	int Num_strings = 0;
	int string_table_len = 0;
	char *temp_ptr;

	TERM_data_ptr = Data_Line = malloc((10240 * (sizeof(char))));
	Data_Line_len = read(Fildes, Data_Line, 10240);
	if ((Data_Line_len >= 10240) || (Data_Line_len < 0))
		return(0);
	/*
	 |	get magic number
	 */
	magic_number = Get_int();
	/*
	 |	if magic number not right, reverse byte order and check again
	 */
	if (magic_number != 282)
	{
		Flip_Bytes = TRUE;
		TERM_data_ptr--;
		TERM_data_ptr--;
		magic_number = Get_int();
		if (magic_number != 282)
			return(0);
	}
	/*
	 |	get the number of each type in the terminfo data file
	 */
	Num_names = Get_int();
	Num_bools = Get_int();
	Num_ints = Get_int();
	Num_strings = Get_int();
	string_table_len = Get_int();
	Strings = malloc(string_table_len);
	while (Num_names > 0)
	{
		TERM_data_ptr++;
		Num_names--;
	}
	counter = 0;
	while (Num_bools)
	{
		Num_bools--;
		Booleans[counter++] = *TERM_data_ptr++;
	}
	if ((unsigned long)TERM_data_ptr & 1)	/* force alignment	*/
		TERM_data_ptr++;
	counter = 0;
	while (Num_ints)
	{
		Num_ints--;
		Numbers[counter] = Get_int();
		counter++;
	}
	temp_ptr = TERM_data_ptr + Num_strings + Num_strings;
	memcpy(Strings, temp_ptr, string_table_len);
	counter = bt__;
	while (Num_strings)
	{
		Num_strings--;
		if ((offset=Get_int()) != -1)
		{
			if (String_table[counter] == NULL)
				String_table[counter] = Strings + offset;
		}
		else
			String_table[counter] = NULL;
		counter++;
	}
	close(Fildes);
	free(Data_Line);
	return(TRUE);
}
#endif		/* ifndef CAP	*/

int 
AtoI()		/* convert ascii text to integers	*/
{
	int Temp;

	Temp = 0;
	while ((*TERM_data_ptr >= '0') && (*TERM_data_ptr <= '9'))
	{
		Temp = (Temp * 10) + (*TERM_data_ptr - '0');
		TERM_data_ptr++;
	}
	return(Temp);
}

void 
Key_Get()		/* create linked list with all key sequences obtained from terminal database	*/
{
	int Counter;
	int Klen;
	int key_def;
	struct KEY_STACK *Spoint;

	Max_Key_len = 0;
	Counter = 0;
	key_def = kb__;
	while (key_def <= kf63__)
	{
		if (key_def == ke__)
			key_def = K1__;
		else if (key_def == (K5__ + 1))
			key_def = kcbt__;
		else if (key_def == (kcbt__ + 1))
			key_def = kbeg__;
		else if (key_def == (kUND__ + 1))
			key_def = kf11__;
		if (String_table[key_def] != NULL)
		{
			if (KEY_TOS == NULL)
				Spoint = KEY_TOS = (struct KEY_STACK *) malloc(sizeof(struct KEY_STACK));
			else
			{
				Spoint = KEY_TOS;
				while (Spoint->next != NULL)
					Spoint = Spoint->next;
				Spoint->next = (struct KEY_STACK *) malloc(sizeof(struct KEY_STACK));
				Spoint = Spoint->next;
			}
			Spoint->next = NULL;
			Spoint->element = (struct KEYS *) malloc(sizeof(struct KEYS));
			Spoint->element->string = String_table[key_def];
			Spoint->element->length = strlen(String_table[key_def]);
			Spoint->element->value = Key_vals[Counter];
			Klen = strlen(Spoint->element->string);
			if (Klen > Max_Key_len)
				Max_Key_len = Klen;
			/*
			 |  Some terminal types accept keystrokes of the form
			 |  \E[A and \EOA, substituting '[' for 'O'.  Make a 
			 |  duplicate of such key strings (since the 
			 |  database will only have one version) so new_curse 
			 |  can understand both.
			 */
			if ((Spoint->element->length > 1) && 
			    ((String_table[key_def][1] == '[') || 
			     (String_table[key_def][1] == 'O')))
			{
				Spoint->next = (struct KEY_STACK *) malloc(sizeof(struct KEY_STACK));
				Spoint = Spoint->next;
				Spoint->next = NULL;
				Spoint->element = (struct KEYS *) malloc(sizeof(struct KEYS));
				Spoint->element->length = strlen(String_table[key_def]);
				Spoint->element->string = malloc(Spoint->element->length + 1);
				strcpy(Spoint->element->string, String_table[key_def]);
				Spoint->element->value = Key_vals[Counter];
				Klen = strlen(Spoint->element->string);
				if (Klen > Max_Key_len)
					Max_Key_len = Klen;
			
				if (String_table[key_def][1] == '[')
					Spoint->element->string[1] = 'O';
				else
					Spoint->element->string[1] = '[';
			}
		}
		key_def++;
		Counter++;
	}
}

/*
 |	insert information about keys for a vt100 terminal
 */

void
keys_vt100()
{
	int counter;
	int Klen;
	struct KEY_STACK *Spoint;

	Spoint = KEY_TOS;
	while (Spoint->next != NULL)
		Spoint = Spoint->next;
	for (counter = 0; vt100[counter].length != 0; counter++)
	{
		Spoint->next = (struct KEY_STACK *) malloc(sizeof(struct KEY_STACK));
		Spoint = Spoint->next;
		Spoint->next = NULL;
		Spoint->element = &vt100[counter];
		Klen = strlen(Spoint->element->string);
		if (Klen > Max_Key_len)
			Max_Key_len = Klen;
	}
}

#ifdef CAP
char *
String_Get(param)		/* read the string */
char *param;
{
	char *String;
	char *Temp;
	int Counter;

	if (param == NULL)
	{
		while (*TERM_data_ptr != '=')
			TERM_data_ptr++;
		Temp = ++TERM_data_ptr;
		Counter = 1;
		while ((*Temp != ':') && (*Temp != (char)NULL))
		{
			Counter++;
			Temp++;
		}
		if (Counter == 1)	/* no data */
			return(NULL);
		String = Temp = malloc(Counter);
		while ((*TERM_data_ptr != ':') && (*TERM_data_ptr != (char)NULL))
		{
			if (*TERM_data_ptr == '\\')
			{
				TERM_data_ptr++;
				if (*TERM_data_ptr == 'n')
					*Temp = '\n';
				else if (*TERM_data_ptr == 't')
					*Temp = '\t';
				else if (*TERM_data_ptr == 'b')
					*Temp = '\b';
				else if (*TERM_data_ptr == 'r')
					*Temp = '\r';
				else if (*TERM_data_ptr == 'f')
					*Temp = '\f';
				else if ((*TERM_data_ptr == 'e') || (*TERM_data_ptr == 'E'))
					*Temp = '\033';		/* escape */
				else if (*TERM_data_ptr == '\\')
					*Temp = '\\';
				else if (*TERM_data_ptr == '\'')
					*Temp = '\'';
				else if ((*TERM_data_ptr >= '0') && (*TERM_data_ptr <= '9'))
				{
					Counter = 0;
					while ((*TERM_data_ptr >= '0') && (*TERM_data_ptr <= '9'))
					{
						Counter = (8 * Counter) + (*TERM_data_ptr - '0');
						TERM_data_ptr++;  /* ? */
					}
					*Temp = Counter;
					TERM_data_ptr--;
				}
				TERM_data_ptr++;
				Temp++;
			}
			else if (*TERM_data_ptr == '^')
			{
				TERM_data_ptr++;
				if ((*TERM_data_ptr >= '@') && (*TERM_data_ptr <= '_'))
					*Temp = *TERM_data_ptr - '@';
				else if (*TERM_data_ptr == '?')
					*Temp = 127;
				TERM_data_ptr++;
				Temp++;
			}
			else
				*Temp++ = *TERM_data_ptr++;
		}
		*Temp = (char)NULL;
		param = String;
	}
	else
	{
		while ((*TERM_data_ptr != (char)NULL) && (*TERM_data_ptr != ':'))
			TERM_data_ptr++;
	}
	return(param);
}

int 
tc_Get_int(param)		/* read the integer			*/
int param;
{
	int Itemp;

	if (param == 0)
	{
		while ((*TERM_data_ptr != (char)NULL) && (*TERM_data_ptr != '#'))
			TERM_data_ptr++;
		TERM_data_ptr++;
		Itemp = AtoI();
		param = Itemp;
	}
	else
	{
		while (*TERM_data_ptr != ':')
			TERM_data_ptr++;
	}
	return(param);
}

void 
Find_term()		/* find terminal description in termcap file	*/
{
	char *Name;
	char *Ftemp;

	Ftemp = Name = malloc(strlen(TERMINAL_TYPE) + 2);
	strcpy(Name, TERMINAL_TYPE);
	while (*Ftemp != (char)NULL)
		Ftemp++;
	*Ftemp++ = '|';
	*Ftemp = (char)NULL;
	CFOUND = FALSE;
	Data_Line_len = strlen(TERMINAL_TYPE) + 1;
	while ((!CFOUND) && ((TERM_data_ptr=fgets(Data_Line, 512, TFP)) != NULL))
	{
		if ((*TERM_data_ptr != ' ') && (*TERM_data_ptr != '\t') && (*TERM_data_ptr != '#'))
		{
			while ((!CFOUND) && (*TERM_data_ptr != (char)NULL))
			{
				CFOUND = !strncmp(TERM_data_ptr, Name, Data_Line_len);
				while ((*TERM_data_ptr != (char)NULL) && (*TERM_data_ptr != '|') && (*TERM_data_ptr != '#') && (*TERM_data_ptr != ':'))
					TERM_data_ptr++;
				if (*TERM_data_ptr == '|')
					TERM_data_ptr++;
				else if (!CFOUND)
					*TERM_data_ptr = (char)NULL;
			}
		}
	}
	if (!CFOUND)
	{
		printf("terminal type %s not found\n", TERMINAL_TYPE);
		exit(0);
	}
}

void 
CAP_PARSE()		/* parse off the data in the termcap data file	*/
{
	int offset;
	int found;

	do
	{
		while (*TERM_data_ptr != (char)NULL)
		{
			for (found = FALSE, offset = 0; (!found) && (offset < 26); offset++)
			{
				if (!strncmp(TERM_data_ptr, Boolean_names[offset], 2))
				{
					found = TRUE;
					Booleans[offset] = TRUE;
				}
			}
			if (!found)
			{
				for (found = FALSE, offset = 0; (!found) && (offset < lw__); offset++)
				{
					if (!strncmp(TERM_data_ptr, Number_names[offset], 3))
					{
						found = TRUE;
						Numbers[offset] = tc_Get_int(Numbers[offset]);
					}
				}
			}
			if (!found)
			{
				for (found = FALSE, offset = 0; (!found) && (offset < smgr__); offset++)
				{
					if (!strncmp(TERM_data_ptr, String_names[offset], 3))
					{
						found = TRUE;
						String_table[offset] = String_Get(String_table[offset]);
					}
				}
			}

			if (!strncmp(TERM_data_ptr, "tc=", 3))
				tc_ = String_Get(NULL);
			while ((*TERM_data_ptr != ':') && (*TERM_data_ptr != (char)NULL))
				TERM_data_ptr++;
			if (*TERM_data_ptr == ':')
				TERM_data_ptr++;
		}
	} while (((TERM_data_ptr = fgets(Data_Line, 512, TFP)) != NULL) && ((*TERM_data_ptr == ' ') || (*TERM_data_ptr == '\t')));
	if (tc_ != NULL)
	{
		TERMINAL_TYPE = tc_;
		rewind(TFP);
		Find_term();
		tc_ = NULL;
		CAP_PARSE();
	}
	else
		fclose(TFP);
}
#endif		/* ifdef CAP	*/

struct _line *
Screenalloc(columns)
int columns;
{
	int i;
	struct _line *tmp;

	tmp = (struct _line *) malloc(sizeof (struct _line));
	tmp->row = malloc(columns + 1);
	tmp->attributes = malloc(columns + 1);
	tmp->prev_screen = NULL;
	tmp->next_screen = NULL;
	for (i = 0; i < columns; i++)
	{
		tmp->row[i] = ' ';
		tmp->attributes[i] = '\0';
	}
	tmp->scroll = tmp->changed = FALSE;
	tmp->row[0] = '\0';
	tmp->attributes[0] = '\0';
	tmp->row[columns] = '\0';
	tmp->attributes[columns] = '\0';
	tmp->last_char = 0;
	return(tmp);
}

WINDOW *newwin(lines, cols, start_l, start_c)
int lines, cols;	/* number of lines and columns to be in window	*/
int start_l, start_c;	/* starting line and column to be inwindow	*/
{
	WINDOW *Ntemp;
	struct _line *temp_screen;
	int i;

	Ntemp = (WINDOW *) malloc(sizeof(WINDOW));
	Ntemp->SR = start_l;
	Ntemp->SC = start_c;
	Ntemp->Num_lines = lines;
	Ntemp->Num_cols = cols;
	Ntemp->LX = 0;
	Ntemp->LY = 0;
	Ntemp->scroll_down = Ntemp->scroll_up = 0;
	Ntemp->SCROLL_CLEAR = FALSE;
	Ntemp->Attrib = FALSE;
	Ntemp->first_line = temp_screen = Screenalloc(cols);
	Ntemp->first_line->number = 0;
	Ntemp->line_array = (struct _line **) malloc(LINES * sizeof(struct _line *));
	
	Ntemp->line_array[0] = Ntemp->first_line;

	for (i = 1; i < lines; i++)
	{
		temp_screen->next_screen = Screenalloc(cols);
		temp_screen->next_screen->number = i;
		temp_screen->next_screen->prev_screen = temp_screen;
		temp_screen = temp_screen->next_screen;
		Ntemp->line_array[i] = temp_screen;
	}
	Ntemp->first_line->prev_screen = NULL;
	temp_screen->next_screen = NULL;
	return(Ntemp);
}

#ifdef CAP
void 
Cap_Out(string, p_list, place)	/* interpret the output string if necessary */
char *string;
int p_list[];			/* stack of values	*/
int place;			/* place keeper of top of stack	*/
{
	char *Otemp;		/* temporary string pointer to parse output */
	int delay;
	int p1, p2, temp;
	float chars;

	if (string == NULL)
		return;

	if (p_list != NULL)
	{
		p1 = p_list[--place];
		p2 = p_list[--place];
	}
	delay = 0;
	Otemp = string;
	if ((*Otemp >= '0') && (*Otemp <= '9'))
	{
		delay = atoi(Otemp);
		while ((*Otemp >= '0') && (*Otemp <= '9'))
			Otemp++;
		if (*Otemp == '*')
			Otemp++;
	}
	while (*Otemp != (char)NULL)
	{
		if (*Otemp == '%')
		{
			Otemp++;
			if ((*Otemp == 'd') || (*Otemp == '2') || (*Otemp == '3') || (*Otemp == '.') || (*Otemp == '+')) 
			{
				if (*Otemp == 'd')
				 	printf("%d", p1);
				else if (*Otemp == '2')
					printf("%02d", p1);
				else if (*Otemp == '3')
					printf("%03d", p1);
				else if (*Otemp == '+')
				{
					Otemp++;
					p1 += *Otemp;
					putchar(p1);
				}
				else if (*Otemp == '.')
					putchar(p1);
				p1 = p2;
				p2 = 0;
			}
			else if (*Otemp == '>')
			{
				Otemp++;
				if (p1 > *Otemp)
				{
					Otemp++;
					p1 += *Otemp;
				}
				else
					Otemp++;
			}
			else if (*Otemp == 'r')
			{
				temp = p1;
				p1 = p2;
				p2 = temp;
			}
			else if (*Otemp == 'i')
			{
				p1++;
				p2++;
			}
			else if (*Otemp == '%')
				putchar(*Otemp);
			else if (*Otemp == 'n')
			{
				p1 ^= 0140;
				p2 ^= 0140;
			}
			else if (*Otemp == 'B')
			{
				p1 = (16 * (p1/10)) + (p1 % 10);
				p2 = (16 * (p2/10)) + (p2 % 10);
			}
			else if (*Otemp == 'D')
			{
				p1 = (p1 - 2 * (p1 % 16));
				p2 = (p2 - 2 * (p2 % 16));
			}
		}
		else
			putchar (*Otemp);
		Otemp++;
	}
	if (delay != 0)
	{
		chars = delay * chars_per_millisecond;
		delay = chars;
		if ((chars - delay) > 0.0)
			delay++;
		for (; delay > 0; delay--)
			putchar(*String_table[pc__]);
	}
	fflush(stdout);
}

#else

	char *Otemp;		/* temporary string pointer to parse output */
	float chars;
	int p[10];
	int variable[27];

int 
Operation(Temp_Stack, place)	/* handle conditional operations	*/
int Temp_Stack[];
int place;
{
	int temp;

	if (*Otemp == 'd')
	{
		Otemp++;
		temp = Temp_Stack[--place];
	 	printf("%d", temp);
	}
	else if (!strncmp(Otemp, "2d", 2))
	{
		temp = Temp_Stack[--place];
		printf("%2d", temp);
		Otemp++;
		Otemp++;
	}
	else if (!strncmp(Otemp, "3d", 2))
	{
		temp = Temp_Stack[--place];
		printf("%0d", temp);
		Otemp++;
		Otemp++;
	}
	else if (!strncmp(Otemp, "02d", 3))
	{
		temp = Temp_Stack[--place];
		printf("%02d", temp);
		Otemp++;
		Otemp++;
		Otemp++;
	}
	else if (!strncmp(Otemp, "03d", 3))
	{
		temp = Temp_Stack[--place];
		printf("%03d", temp);
		Otemp++;
		Otemp++;
		Otemp++;
	}
	else if (*Otemp == '+')
	{
		Otemp++;
		temp = Temp_Stack[--place];
		temp += Temp_Stack[--place];
		Temp_Stack[place++] = temp;
	}
	else if (*Otemp == '-')
	{
		Otemp++;
		temp = Temp_Stack[--place];
		temp -= Temp_Stack[--place];
		Temp_Stack[place++] = temp;
	}
	else if (*Otemp == '*')
	{
		Otemp++;
		temp = Temp_Stack[--place];
		temp *= Temp_Stack[--place];
		Temp_Stack[place++] = temp;
	}
	else if (*Otemp == '/')
	{
		Otemp++;
		temp = Temp_Stack[--place];
		temp /= Temp_Stack[--place];
		Temp_Stack[place++] = temp;
	}
	else if (*Otemp == 'm')
	{
		Otemp++;
		temp = Temp_Stack[--place];
		temp %= Temp_Stack[--place];
		Temp_Stack[place++] = temp;
	}
	else if (*Otemp == '&')
	{
		Otemp++;
		temp = Temp_Stack[--place];
		temp &= Temp_Stack[--place];
		Temp_Stack[place++] = temp;
	}
	else if (*Otemp == '|')
	{
		Otemp++;
		temp = Temp_Stack[--place];
		temp |= Temp_Stack[--place];
		Temp_Stack[place++] = temp;
	}
	else if (*Otemp == '^')
	{
		Otemp++;
		temp = Temp_Stack[--place];
		temp ^= Temp_Stack[--place];
		Temp_Stack[place++] = temp;
	}
	else if (*Otemp == '=')
	{
		Otemp++;
		temp = Temp_Stack[--place];
		temp = (temp == Temp_Stack[--place]);
		Temp_Stack[place++] = temp;
	}
	else if (*Otemp == '>')
	{
		Otemp++;
		temp = Temp_Stack[--place];
		temp = temp > Temp_Stack[--place];
		Temp_Stack[place++] = temp;
	}
	else if (*Otemp == '<')
	{
		Otemp++;
		temp = Temp_Stack[--place];
		temp = temp < Temp_Stack[--place];
		Temp_Stack[place++] = temp;
	}
	else if (*Otemp == 'c')
	{
		Otemp++;
		putchar(Temp_Stack[--place]);
	}
	else if (*Otemp == 'i')
	{
		Otemp++;
		p[1]++;
		p[2]++;
	}
	else if (*Otemp == '%')
	{
		putchar(*Otemp);
		Otemp++;
	}
	else if (*Otemp == '!')
	{
		temp = ! Temp_Stack[--place];
		Temp_Stack[place++] = temp;
		Otemp++;
	}
	else if (*Otemp == '~')
	{
		temp = ~Temp_Stack[--place];
		Temp_Stack[place++] = temp;
		Otemp++;
	}
	else if (*Otemp == 'p')
	{
		Otemp++;
		Temp_Stack[place++] = p[*Otemp - '0'];
		Otemp++;
	}
	else if (*Otemp == 'P')
	{
		Otemp++;
		Temp_Stack[place++] = variable[*Otemp - 'a'];
		Otemp++;
	}
	else if (*Otemp == 'g')
	{
		Otemp++;
		variable[*Otemp - 'a'] = Temp_Stack[--place];
		Otemp++;
	}
	else if (*Otemp == '\'')
	{
		Otemp++;
		Temp_Stack[place++] = *Otemp;
		Otemp++;
		Otemp++;
	}
	else if (*Otemp == '{')
	{
		Otemp++;
		temp = atoi(Otemp);
		Temp_Stack[place++] = temp;
		while (*Otemp != '}')
			Otemp++;
		Otemp++;
	}
	return(place);
}

void 
Info_Out(string, p_list, place)	/* interpret the output string if necessary */
char *string;
int p_list[];
int place;
{
	char *tchar;
	int delay;
	int temp;
	int Cond_FLAG;
	int EVAL;
	int Cond_Stack[128];
	int Cond_place;
	int Stack[128];
	int Top_of_stack;

	if (string == NULL)
		return;

	Cond_FLAG = FALSE;
	Cond_place = 0;
	Top_of_stack = 0;
	p[0] = 0;
	p[1] = 0;
	p[2] = 0;
	p[3] = 0;
	p[4] = 0;
	p[5] = 0;
	p[6] = 0;
	p[7] = 0;
	p[8] = 0;
	p[9] = 0;
	if (p_list != NULL)
	{
		for (temp = 1; (place != 0); temp++)
		{
			p[temp] = p_list[--place];
		}
	}
	delay = 0;
	Otemp = string;
	while (*Otemp != '\0')
	{
		if (*Otemp == '%')
		{
			Otemp++;
			if ((*Otemp == '?') || (*Otemp == 't') || (*Otemp == 'e') || (*Otemp == ';'))
			{
				if (*Otemp == '?')
				{
					Otemp++;
					Cond_FLAG = TRUE;
					EVAL = TRUE;
					while (EVAL)
					{
						/*
						 |  find the end of the 
						 |  conditional statement
						 */
						while ((strncmp(Otemp, "%t", 2)) && (*Otemp != '\0'))
						{
							/*
							 |  move past '%'
							 */
							Otemp++;
							Cond_place = Operation(Cond_Stack, Cond_place);
						}

						/*
						 |  if condition is true
						 */
						if ((Cond_place > 0) && (Cond_Stack[Cond_place-1]))
						{
							/*
							 |  end conditional 
							 |  parsing
							 */
							EVAL = FALSE;
							Otemp++;
							Otemp++;
						}
						else	/* condition is false */
						{
							/*
							 |  find 'else' or end 
							 |  of if statement
							 */
							while ((strncmp(Otemp, "%e", 2)) && (strncmp(Otemp, "%;", 2)) && (*Otemp != '\0'))
								Otemp++;
							/*
							 |  if an 'else' found
							 */
							if ((*Otemp != '\0') && (!strncmp(Otemp, "%e", 2)))
							{
								Otemp++;
								Otemp++;
								tchar = Otemp;
								/*
								 |  check for 'then' part
								 */
								while ((*tchar != '\0') && (strncmp(tchar, "%t", 2)) && (strncmp(tchar, "%;", 2)))
									tchar++;
								/*
								 |  if end of string
								 */
								if (*tchar == '\0')
								{
									EVAL = FALSE;
									Cond_FLAG = FALSE;
									Otemp = tchar;
								}
								/*
								 |  if end of if found,
								 |  set up to parse 
								 |  info
								 */
								else if (!strncmp(tchar, "%;", 2))
									EVAL = FALSE;
								/*
								 |  otherwise, check 
								 |  conditional in 
								 |  'else'
								 */
							}
							/*
							 |  if end of if found,
							 |  get out of if 
							 |  statement
							 */
							else if ((*Otemp != '\0') && (!strncmp(Otemp, "%;", 2)))
							{
								EVAL = FALSE;
								Otemp++;
								Otemp++;
							}
							else /* Otemp == NULL */
							{
								EVAL = FALSE;
								Cond_FLAG = FALSE;
							}
						}
					}
				}
				else
				{
					Otemp++;
					Cond_FLAG = FALSE;
					if (*Otemp != ';')
					{
						while ((*Otemp != '\0') && (strncmp(Otemp, "%;", 2)))
							Otemp++;
						if (*Otemp != '\0')
						{
							Otemp++;
							Otemp++;
						}
					}
					else
						Otemp++;
				}
			}
			else
			{
				Top_of_stack = Operation(Stack, Top_of_stack);
			}
		}
		else if (!strncmp(Otemp, "$<", 2))
		{
			Otemp++;
			Otemp++;
			delay = atoi(Otemp);
			while (*Otemp != '>')
				Otemp++;
			Otemp++;
			chars = delay * chars_per_millisecond;
			delay = chars;
			if ((chars - delay) > 0.0)
				delay++;
			if (String_table[pc__] == NULL)
				temp = 0;
			else
				temp = *String_table[pc__];
			for (; delay > 0; delay--)
				putc(temp, stdout);
		}
		else
		{
			putchar(*Otemp);
			Otemp++;
		}
	}
	fflush(stdout);
}
#endif

void 
wmove(window, row, column)	/* move cursor to indicated position in window */
WINDOW *window;
int row, column;
{
	if ((row < window->Num_lines) && (column < window->Num_cols))
	{
		window->LX = column;
		window->LY = row;
	}
}

void 
clear_line(line, column, cols)
struct _line *line;
int column;
int cols;
{
	int j;

	if (column > line->last_char)
	{
		for (j = line->last_char; j < column; j++)
		{
			line->row[j] = ' ';
			line->attributes[j] = '\0';
		}
	}
	line->last_char = column;
	line->row[column] = '\0';
	line->attributes[column] = '\0';
	line->changed = TRUE;
}

void 
werase(window)			/* clear the specified window		*/
WINDOW *window;
{
	int i;
	struct _line *tmp;

	window->SCROLL_CLEAR = CLEAR;
	window->scroll_up = window->scroll_down = 0;
	for (i = 0, tmp = window->first_line; i < window->Num_lines; i++, tmp = tmp->next_screen)
		clear_line(tmp, 0, window->Num_cols);
}

void 
wclrtoeol(window)	/* erase from current cursor position to end of line */
WINDOW *window;
{
	int column, row;
	struct _line *tmp;

	window->SCROLL_CLEAR = CHANGE;
	column = window->LX;
	row = window->LY;
	for (row = 0, tmp = window->first_line; row < window->LY; row++)
		tmp = tmp->next_screen;
	clear_line(tmp, column, window->Num_cols);
}

void 
wrefresh(window)		/* flush all previous output		*/
WINDOW *window;
{
	wnoutrefresh(window);
#ifdef DIAG
{
	struct _line *temp;
	int value;
	fprintf(stderr, "columns=%d, lines=%d, SC=%d, SR=%d\n",window->Num_cols, window->Num_lines, window->SC, window->SR);
	for (value = 0, temp = window->first_line; value < window->Num_lines; value++, temp = temp->next_screen)
	{
		if (temp->number == -1)
			fprintf(stderr, "line moved ");
		if (temp->scroll)
			fprintf(stderr, "scroll_x is set:  ");
		fprintf(stderr, "lc%d=%s|\n", temp->last_char, temp->row);
	}
	fprintf(stderr, "+-------------------- virtual screen ----------------------------------------+\n");
	fprintf(stderr, "columns=%d, lines=%d \n",virtual_scr->Num_cols, virtual_scr->Num_lines);
	for (value = 0, temp = virtual_scr->first_line; value < virtual_scr->Num_lines; value++, temp = temp->next_screen)
	{
		if (temp->number == -1)
			fprintf(stderr, "line moved ");
		if (temp->scroll)
			fprintf(stderr, "scroll_x is set:  ");
		fprintf(stderr, "lc%d=%s|\n", temp->last_char, temp->row);
	}
	fprintf(stderr, "columns=%d, lines=%d \n",curscr->Num_cols, curscr->Num_lines);
	for (value = 0, temp = curscr->first_line; value < curscr->Num_lines; value++, temp = temp->next_screen)
		fprintf(stderr, "line=%s|\n", temp->row);
}
#endif
	doupdate();
	virtual_scr->SCROLL_CLEAR = FALSE;
	virtual_scr->scroll_down = virtual_scr->scroll_up = 0;
	fflush(stdout);
}

void 
touchwin(window)
WINDOW *window;
{
	struct _line *user_line;
	int line_counter = 0;

	for (line_counter = 0, user_line = window->first_line; 
		line_counter < window->Num_lines; line_counter++)
	{
		user_line->changed = TRUE;
	}
	window->SCROLL_CLEAR = TRUE;
}

void 
wnoutrefresh(window)
WINDOW *window;
{
	struct _line *user_line;
	struct _line *virtual_line;
	int line_counter = 0;
	int user_col = 0;
	int virt_col = 0;

	if (window->SR >= virtual_scr->Num_lines)
		return;
	user_line = window->first_line;
	virtual_line = virtual_scr->first_line;
	virtual_scr->SCROLL_CLEAR = window->SCROLL_CLEAR;
	virtual_scr->LX = window->LX + window->SC;
	virtual_scr->LY = window->LY + window->SR;
	virtual_scr->scroll_up = window->scroll_up;
	virtual_scr->scroll_down = window->scroll_down;
	if ((last_window_refreshed == window) && (!window->SCROLL_CLEAR))
		return;
	for (line_counter = 0; line_counter < window->SR; line_counter++)
	{
		virtual_line = virtual_line->next_screen;
	}
	for (line_counter = 0; (line_counter < window->Num_lines)
		&& ((line_counter + window->SR) < virtual_scr->Num_lines); 
			line_counter++)
	{
		if ((last_window_refreshed != window) || (user_line->changed) || ((SCROLL | CLEAR) & window->SCROLL_CLEAR))
		{
			for (user_col = 0, virt_col = window->SC; 
				(virt_col < virtual_scr->Num_cols) 
				  && (user_col < user_line->last_char); 
				  	virt_col++, user_col++)
			{
				virtual_line->row[virt_col] = user_line->row[user_col];
				virtual_line->attributes[virt_col] = user_line->attributes[user_col];
			}
			for (user_col = user_line->last_char, 
			     virt_col = window->SC + user_line->last_char; 
				(virt_col < virtual_scr->Num_cols) 
				  && (user_col < window->Num_cols); 
				  	virt_col++, user_col++)
			{
				virtual_line->row[virt_col] = ' ';
				virtual_line->attributes[virt_col] = '\0';
			}
		}
		if (virtual_scr->Num_cols != window->Num_cols)
		{
			if (virtual_line->last_char < (user_line->last_char + window->SC))
			{
				if (virtual_line->row[virtual_line->last_char] == '\0')
					virtual_line->row[virtual_line->last_char] = ' ';
				virtual_line->last_char = 
					min(virtual_scr->Num_cols, 
					  (user_line->last_char + window->SC));
			}
		}
		else
			virtual_line->last_char = user_line->last_char;
		virtual_line->row[virtual_line->last_char] = '\0';
		virtual_line->changed = user_line->changed;
		virtual_line = virtual_line->next_screen;
		user_line = user_line->next_screen;
	}
	window->SCROLL_CLEAR = FALSE;
	window->scroll_up = window->scroll_down = 0;
	last_window_refreshed = window;
}

void 
flushinp()			/* flush input				*/
{
}

void 
ungetch(c)			/* push a character back on input	*/
int c;
{
	if (bufp < 100)
		in_buff[bufp++] = c;
}

#ifdef BSD_SELECT
int 
timed_getchar()
{
	struct timeval tv;
	fd_set fds;
	int ret_val;
	int nfds = 1;
	char temp;

	FD_ZERO(&fds);
	tv.tv_sec = 0;
	tv.tv_usec = 500000;  /* half a second */
	FD_SET(0, &fds);
	Time_Out = FALSE; /* just in case */

	ret_val = select(nfds, &fds, 0, 0, &tv);

	/*
	 |	if ret_val is less than zero, there was no input
	 |	otherwise, get a character and return it
	 */

	if (ret_val <= 0)
	{ 
		Time_Out = TRUE;
		return(-1);
	}

	return(read(0, &temp, 1)? temp : -1);
}
#endif

int 
wgetch(window)			/* get character from specified window	*/
WINDOW *window;
{
	int in_value;
	char temp;
#ifndef SYS5
	int old_arg;
#endif /* SYS5 */

#ifdef BSD_SELECT
	if (Noblock)
		in_value = ((bufp > 0) ? in_buff[--bufp] : timed_getchar());
	else
		in_value = ((bufp > 0) ? in_buff[--bufp] : read(0, &temp, 1)? temp : -1);
#else /* BSD_SELECT */
#ifdef SYS5
	in_value = ((bufp > 0) ? in_buff[--bufp] : 
					(read(0, &temp, 1)> 0) ? temp : -1);
#else /* SYS5 */
	if (Noblock)
	{
		Time_Out = FALSE;
		old_arg = fcntl(0, F_GETFL, 0);
		in_value = fcntl(0, F_SETFL, old_arg | FNDELAY);
	}
	in_value = ((bufp > 0) ? in_buff[--bufp] : read(0, &temp, 1)? temp : -1);
	if (Noblock)
	{
		fcntl(0, F_SETFL, old_arg);
		if (Time_Out)
			in_value = -1;
	}
#endif /* SYS5 */
#endif /* BSD_SELECT */

	if (in_value != -1) 
	{
		in_value &= 0xff;
		if ((Parity) && (Num_bits < 8))	
				/* strip eighth bit if parity in use */
		in_value &= 0177;
	}
	else if (interrupt_flag)
	{
		interrupt_flag = FALSE;
		in_value = wgetch(window);
	}

	if ((in_value == '\033') || (in_value == '\037'))/* escape character */
		in_value = Get_key(in_value);
	return(in_value);
}

#ifndef BSD_SELECT
void 
Clear(arg)		/* notify that time out has occurred	*/
int arg;
{
	Time_Out = TRUE;
#ifdef DEBUG
fprintf(stderr, "inside Clear()\n");
fflush(stderr);
#endif /* DEBUG */
}
#endif /* BSD_SELECT */

int 
Get_key(first_char)			/* try to decode key sequence	*/
int first_char;				/* first character of sequence	*/
{
	int in_char;
	int Count;
	char string[128];
	char *Gtemp;
	int Found;
#ifdef SYS5
	struct termio Gterminal;
#else
	struct sgttyb Gterminal;
#endif
	struct KEY_STACK *St_point;
#if (!defined( BSD_SELECT)) || (!defined(SYS5))
	int value;
#endif /* BSD_SELECT */

	Count = 0;
	Gtemp = string;
	string[Count++] = first_char;
	string[Count] = '\0';
	Time_Out = FALSE;
#ifndef BSD_SELECT
	signal(SIGALRM, Clear);
	value = alarm(1);
#endif /* BSD_SELECT */
	Noblock = TRUE;
#ifdef SYS5
	Gterminal.c_cc[VTIME] = 0;		/* timeout value	*/
	Gterminal.c_lflag &= ~ICANON;	/* disable canonical operation	*/
	Gterminal.c_lflag &= ~ECHO;		/* disable echo		*/
#endif
	Count = 1;
	Found = FALSE;
	while ((Count < Max_Key_len) && (!Time_Out) && (!Found))
	{
		in_char = wgetch(stdscr);
#ifdef DEBUG
fprintf(stderr, "back in GetKey()\n");
fflush(stderr);
#endif /* DEBUG */
		if (in_char != -1)
		{
			string[Count++] = in_char;
			string[Count] = '\0';
			St_point = KEY_TOS;
			while ((St_point != NULL) && (!Found))
			{
				if (!strcmp(string, St_point->element->string))
					Found = TRUE;
				else
					St_point = St_point->next;
			}
		}
	}
#ifndef BSD_SELECT
	if (!Time_Out)
		value = alarm(0);
#endif /* BSD_SELECT */
#ifdef SYS5
/*	value = ioctl(0, TCSETA, &Terminal);*/
#else
	value = ioctl(0, TIOCSETP, &Terminal);
/*	value = fcntl(0, F_SETFL, old_arg);*/
#endif
	Noblock = FALSE;
	if (Found)
	{
		return(St_point->element->value);
	}
	else
	{
		while (Count > 1)
		{
			if ((string[--Count] != -1) && 
					((unsigned char) (string[Count]) != 255))
			{
#ifdef DIAG
fprintf(stderr, "ungetting character %d\n", string[Count]);fflush(stdout);
#endif
				ungetch(string[Count]);
			}
		}
		return(first_char);
	}
}

void 
waddch(window, c)	/* output the character in the specified window	*/
WINDOW *window;
int c;
{
	int column, j;
	int shift;	/* number of spaces to shift if a tab		*/
	struct _line *tmpline;

#ifdef DIAG
/*printf("starting waddch \n");fflush(stdout);*/
#endif
	column = window->LX;
	if (c == '\t')
	{
		shift = (column + 1) % 8;
		if (shift == 0)
			shift++;
		else
			shift = 9 - shift;
		while (shift > 0)
		{
			shift--;
			waddch(window, ' ');
		}
	}
	else if ((column < window->Num_cols) && (window->LY < window->Num_lines))
	{
		if ((c == '~') && (Booleans[hz__]))
			c = '@';

		if (( c != '\b') && (c != '\n') && (c != '\r'))
		{
			tmpline = window->line_array[window->LY];
			tmpline->row[column] = c;
			tmpline->attributes[column] = window->Attrib;
			tmpline->changed = TRUE;
			if (column >= tmpline->last_char)
			{
				if (column > tmpline->last_char)
					for (j = tmpline->last_char; j < column; j++)
					{
						tmpline->row[j] = ' ';
						tmpline->attributes[j] = '\0';
					}
				tmpline->row[column + 1] = '\0';
				tmpline->attributes[column + 1] = '\0';
				tmpline->last_char = column + 1;
			}
		}
		if (c == '\n')
		{
			wclrtoeol(window);
			window->LX = window->Num_cols;
		}
		else if (c == '\r')
			window->LX = 0;
		else if (c == '\b')
			window->LX--;
		else
			window->LX++;
	}
	if (window->LX >= window->Num_cols)
	{
		window->LX = 0;
		window->LY++;
		if (window->LY >= window->Num_lines)
		{
			window->LY = window->Num_lines - 1;
/*			window->LY = row;
			wmove(window, 0, 0);
			wdeleteln(window);
			wmove(window, row, 0);*/
		}
	}
	window->SCROLL_CLEAR = CHANGE;
}

void 
winsertln(window)	/* insert a blank line into the specified window */
WINDOW *window;
{
	int row, column;
	struct _line *tmp;
	struct _line *tmp1;

	window->scroll_down += 1;
	window->SCROLL_CLEAR = SCROLL;
	column = window->LX;
	row = window->LY;
	for (row = 0, tmp = window->first_line; (row < window->Num_lines) && (tmp->next_screen != NULL); row++)
		tmp = tmp->next_screen;
	if (tmp->prev_screen != NULL)
		tmp->prev_screen->next_screen = NULL;
	tmp1 = tmp;
	clear_line(tmp1, 0, window->Num_cols);
	tmp1->number = -1;
	for (row = 0, tmp = window->first_line; (row < window->LY) && (tmp->next_screen != NULL); row++)
		tmp = tmp->next_screen;
	if ((window->LY == (window->Num_lines - 1)) && (window->Num_lines > 1))
	{
		tmp1->next_screen = tmp->next_screen;
		tmp->next_screen = tmp1;
		tmp->changed = TRUE;
		tmp->next_screen->prev_screen = tmp;
	}
	else if (window->Num_lines > 1)
	{
		if (tmp->prev_screen != NULL)
			tmp->prev_screen->next_screen = tmp1;
		tmp1->prev_screen = tmp->prev_screen;
		tmp->prev_screen = tmp1;
		tmp1->next_screen = tmp;
		tmp->changed = TRUE;
		tmp->scroll = DOWN;
	}
	if (window->LY == 0)
		window->first_line = tmp1;

	for (row = 0, tmp1 = window->first_line; 
		row < window->Num_lines; row++)
	{
		window->line_array[row] = tmp1;
		tmp1 = tmp1->next_screen;
	}
}

void 
wdeleteln(window)	/* delete a line in the specified window */
WINDOW *window;
{
	int row, column;
	struct _line *tmp;
	struct _line  *tmpline;

	if (window->Num_lines > 1)
	{
		window->scroll_up += 1;
		window->SCROLL_CLEAR = SCROLL;
		column = window->LX;
		row = window->LY;
		for (row = 0, tmp = window->first_line; row < window->LY; row++)
			tmp = tmp->next_screen;
		if (window->LY == 0)
			window->first_line = tmp->next_screen;
		if (tmp->prev_screen != NULL)
			tmp->prev_screen->next_screen = tmp->next_screen;
		if (tmp->next_screen != NULL)
		{
			tmp->next_screen->changed = TRUE;
			tmp->next_screen->scroll = UP;
			tmp->next_screen->prev_screen = tmp->prev_screen;
		}
		tmpline = tmp;
		clear_line(tmpline, 0, window->Num_cols);
		tmpline->number = -1;
		for (row = 0, tmp = window->first_line; tmp->next_screen != NULL; row++)
			tmp = tmp->next_screen;
		if (tmp != NULL)
		{
			tmp->next_screen = tmpline;
			tmp->next_screen->prev_screen = tmp;
			tmp->changed = TRUE;
			tmp = tmp->next_screen;
		}
		else
			tmp = tmpline;
		tmp->next_screen = NULL;

		for (row = 0, tmp = window->first_line; row < window->Num_lines; row++)
		{
			window->line_array[row] = tmp;
			tmp = tmp->next_screen;
		}
	}
	else
	{
		clear_line(window->first_line, 0, window->Num_cols);
	}
}

void 
wclrtobot(window)	/* delete from current position to end of the window */
WINDOW *window;
{
	int row, column;
	struct _line *tmp;

	window->SCROLL_CLEAR |= CLEAR;
	column = window->LX;
	row = window->LY;
	for (row = 0, tmp = window->first_line; row < window->LY; row++)
		tmp = tmp->next_screen;
	clear_line(tmp, column, window->Num_cols);
	for (row = (window->LY + 1); row < window->Num_lines; row++)
	{
		tmp = tmp->next_screen;
		clear_line(tmp, 0, window->Num_cols);
	}
	wmove(window, row, column);
}

void 
wstandout(window)	/* begin standout mode in window	*/
WINDOW *window;
{
	if (Numbers[sg__] < 1)	/* if not magic cookie glitch	*/
		window->Attrib |= A_STANDOUT;
}

void 
wstandend(window)	/* end standout mode in window	*/
WINDOW *window;
{
	window->Attrib &= ~A_STANDOUT;
}

void 
waddstr(window, string)	/* write 'string' in window	*/
WINDOW *window;
char *string;
{
	char *wstring;

	for (wstring = string; *wstring != '\0'; wstring++)
		waddch(window, *wstring);
}

void 
clearok(window, flag)	/* erase screen and redraw at next refresh	*/
WINDOW *window;
int flag;
{
	Repaint_screen = TRUE;
}


void 
echo()			/* turn on echoing				*/
{
	int value;

#ifdef SYS5
	Terminal.c_lflag |= ECHO;		/* enable echo		*/
	value = ioctl(0, TCSETA, &Terminal);	/* set characteristics	*/
#else
	Terminal.sg_flags |= ECHO;		/* enable echo		*/
	value = ioctl(0, TIOCSETP, &Terminal);	/* set characteristics	*/
#endif
}

void 
noecho()		/* turn off echoing				*/
{
	int value;

#ifdef SYS5
	Terminal.c_lflag &= ~ECHO;		/* disable echo		*/
	value = ioctl(0, TCSETA, &Terminal);	/* set characteristics	*/
#else
	Terminal.sg_flags &= ~ECHO;		/* disable echo		*/
	value = ioctl(0, TIOCSETP, &Terminal);	/* set characteristics	*/
#endif
}

void 
raw()			/* set to read characters immediately		*/
{
	int value;

#ifdef SYS5
	Intr = Terminal.c_cc[VINTR];	/* get the interrupt character	*/
	Terminal.c_lflag &= ~ICANON;	/* disable canonical operation	*/
	Terminal.c_lflag &= ~ISIG;	/* disable signal checking	*/
#ifdef FLUSHO
	Terminal.c_lflag &= ~FLUSHO;
#endif
#ifdef PENDIN
	Terminal.c_lflag &= ~PENDIN;
#endif
#ifdef IEXTEN
	Terminal.c_lflag &= ~IEXTEN;
#endif
	Terminal.c_cc[VMIN] = 1;		/* minimum of one character */
	Terminal.c_cc[VTIME] = 0;		/* timeout value	*/
	Terminal.c_cc[VINTR] = 0;		/* eliminate interrupt	*/
	value = ioctl(0, TCSETA, &Terminal);	/* set characteristics	*/
#else
	Terminal.sg_flags |= RAW;	/* enable raw mode		*/
	value = ioctl(0, TIOCSETP, &Terminal);	/* set characteristics	*/
#endif
}

void 
noraw()			/* set to normal character read mode		*/
{
	int value;

#ifdef SYS5
	Terminal.c_lflag |= ICANON;	/* enable canonical operation	*/
	Terminal.c_lflag |= ISIG;	/* enable signal checking	*/
	Terminal.c_cc[VEOF] = 4;		/* EOF character = 4	*/
	Terminal.c_cc[VEOL] = '\0';	/* EOL = 0		*/
	Terminal.c_cc[VINTR] = Intr;		/* reset interrupt char	*/
	value = ioctl(0, TCSETA, &Terminal);	/* set characteristics	*/
#else
	Terminal.sg_flags &= ~RAW;	/* disable raw mode		*/
	value = ioctl(0, TIOCSETP, &Terminal);	/* set characteristics	*/
/*	old_arg = fcntl(0, F_GETFL, 0);
	value = fcntl(0, F_SETFL, old_arg & ~FNDELAY);*/
#endif
}

void 
nl()
{
	int value;

#ifdef SYS5
	Terminal.c_iflag |= ICRNL;	/* enable carriage-return to line-feed mapping	*/
	value = ioctl(0, TCSETA, &Terminal);	/* set characteristics	*/
#endif
}

void 
nonl()
{
	int value;

#ifdef SYS5
	Terminal.c_iflag &= ~ICRNL;	/* disable carriage-return to line-feed mapping	*/
	Terminal.c_iflag &= ~IGNCR;	/* do not ignore carriage-return	*/
	value = ioctl(0, TCSETA, &Terminal);	/* set characteristics	*/
#endif
}

void 
saveterm()
{
}

void 
fixterm()
{
}

void 
resetterm()
{
}

void 
nodelay(window, flag)
WINDOW *window;
int flag;
{
}

void 
idlok(window, flag)
WINDOW *window;
int flag;
{
}

void 
keypad(window, flag)
WINDOW *window;
int flag;
{
	if (flag)
		String_Out(String_table[ks__], NULL, 0);
	else
		String_Out(String_table[ke__], NULL, 0);
}

void 
savetty()		/* save current tty stats			*/
{
	int value;

#ifdef SYS5
	value = ioctl(0, TCGETA, &Saved_tty);	/* set characteristics	*/
#else
	value = ioctl(0, TIOCGETP, &Saved_tty);	/* set characteristics	*/
#endif
}

void 
resetty()		/* restore previous tty stats			*/
{
	int value;

#ifdef SYS5
	value = ioctl(0, TCSETA, &Saved_tty);	/* set characteristics	*/
#else
	value = ioctl(0, TIOCSETP, &Saved_tty);	/* set characteristics	*/
#endif
}

void 
endwin()		/* end windows					*/
{
	keypad(stdscr, FALSE);
	initialized = FALSE;
	delwin(curscr);
	delwin(virtual_scr);
	delwin(stdscr);
#ifndef SYS5
{
	int old_arg, value;
/*	old_arg = fcntl(0, F_GETFL, 0);
	value = fcntl(0, F_SETFL, old_arg & ~FNDELAY);*/
}
#endif
}

void 
delwin(window)		/* delete the window structure			*/
WINDOW *window;
{
	int i;

	for (i = 1; (i < window->Num_lines) && (window->first_line->next_screen != NULL); i++)
	{
		window->first_line = window->first_line->next_screen;
		free(window->first_line->prev_screen->row);
		free(window->first_line->prev_screen->attributes);
		free(window->first_line->prev_screen);
	}
	if (window == last_window_refreshed)
		last_window_refreshed = 0;
	if (window->first_line != NULL)
	{
		free(window->first_line->row);
		free(window->first_line->attributes);
		free(window->first_line);
		free(window);
	}
}

#ifndef __STDC__
void 
wprintw(va_alist)
va_dcl
#else /* __STDC__ */
void 
wprintw(WINDOW *window, const char *format, ...)
#endif /* __STDC__ */
{
#ifndef __STDC__
	WINDOW *window;
	char *format;
	va_list ap;
#else
	va_list ap;
#endif
	int value;
	char *fpoint;
	char *wtemp;

#ifndef __STDC__
	va_start(ap);
	window = va_arg(ap, WINDOW *);
	format = va_arg(ap, char *);
#else /* __STDC__ */
	va_start(ap, format);
#endif /* __STDC__ */

	fpoint = (char *) format;
	while (*fpoint != '\0')
	{
		if (*fpoint == '%')
		{
			fpoint++;
			if (*fpoint == 'd')
			{
				value = va_arg(ap, int);
				iout(window, value);
			}
			else if (*fpoint == 'c')
			{
				value = va_arg(ap, int);
				waddch(window, value);
			}
			else if (*fpoint == 's')
			{
				wtemp = va_arg(ap, char *);
					waddstr(window, wtemp);
			}
			fpoint++;
		}
		else if (*fpoint == '\\')
		{
			fpoint++;
			if (*fpoint == 'n')
				waddch(window, '\n');
			else if ((*fpoint >= '0') && (*fpoint <= '9'))
			{
				value = 0;
				while ((*fpoint >= '0') && (*fpoint <= '9'))
				{
					value = (value * 8) + (*fpoint - '0');
					fpoint++;
				}
				waddch(window, value);
			}
			fpoint++;
		}
		else
			waddch(window, *fpoint++);
	}
#ifdef __STDC__
	va_end(ap);
#endif /* __STDC__ */
}

void 
iout(window, value)	/* output characters		*/
WINDOW *window;
int value;
{
	int i;

	if ((i = value / 10) != 0)
		iout(window, i);
	waddch(window, ((value % 10) + '0'));
}

int 
Comp_line(line1, line2)		/* compare lines	*/
struct _line *line1;
struct _line *line2;
{
	int count1;
	int i;
	char *att1, *att2;
	char *c1, *c2;

	if (line1->last_char != line2->last_char)
		return(2);

	c1 = line1->row;
	c2 = line2->row;
	att1 = line1->attributes;
	att2 = line2->attributes;
	i = 0;
	while ((c1[i] != '\0') && (c2[i] != '\0') && (c1[i] == c2[i]) && (att1[i] == att2[i]))
		i++;
	count1 = i + 1;
	if ((count1 == 1) && (c1[i] == '\0') && (c2[i] == '\0'))
		count1 = 0;			/* both lines blank	*/
	else if ((c1[i] == '\0') && (c2[i] == '\0'))
		count1 = -1;			/* equal		*/
	else
		count1 = 1;			/* lines unequal	*/
	return(count1);
}

struct _line *
Insert_line(row, end_row, window)	/* insert line into screen */
int row;
int end_row;
WINDOW *window;
{
	int i;
	struct _line *tmp;
	struct _line *tmp1;

	for (i = 0, tmp = curscr->first_line; i < window->SR; i++)
		tmp = tmp->next_screen;
	if ((end_row + window->SR) == 0)
		curscr->first_line = curscr->first_line->next_screen;
	top_of_win = tmp;
	/*
	 |	find bottom line to delete
	 */
	for (i = 0, tmp = top_of_win; (tmp->next_screen != NULL) && (i < end_row); i++)
		tmp = tmp->next_screen;
	if (tmp->prev_screen != NULL)
		tmp->prev_screen->next_screen = tmp->next_screen;
	if (tmp->next_screen != NULL)
		tmp->next_screen->prev_screen = tmp->prev_screen;
	tmp1 = tmp;
	/*
	 |	clear deleted line
	 */
	clear_line(tmp, 0, window->Num_cols);
	tmp1->number = -1;
	for (i = 0, tmp = curscr->first_line; (tmp->next_screen != NULL) && (i < window->SR); i++)
		tmp = tmp->next_screen;
	top_of_win = tmp;
	for (i = 0, tmp = top_of_win; i < row; i++)
		tmp = tmp->next_screen;
	if ((tmp->prev_screen != NULL) && (window->Num_lines > 0))
		tmp->prev_screen->next_screen = tmp1;
	tmp1->prev_screen = tmp->prev_screen;
	tmp->prev_screen = tmp1;
	tmp1->next_screen = tmp;
	if ((row + window->SR) == 0)
		curscr->first_line = tmp1;
	if (tmp1->next_screen != NULL)
		tmp1 = tmp1->next_screen;

	if ((!String_table[cs__]) && (end_row < window->Num_lines))
	{
		Position(window, (window->SR + end_row), 0);
		String_Out(String_table[dl__], NULL, 0);
	}
	Position(window, (window->SR + row), 0);
	if (String_table[al__] != NULL)
		String_Out(String_table[al__], NULL, 0);
	else
		String_Out(String_table[sr__], NULL, 0);

	for (i = 0, top_of_win = curscr->first_line; (top_of_win->next_screen != NULL) && (i < window->SR); i++)
		top_of_win = top_of_win->next_screen;
	return(tmp1);
}


struct _line *
Delete_line(row, end_row, window)	/* delete a line on screen */
int row;
int end_row;
WINDOW *window;
{
	int i;
	struct _line *tmp;
	struct _line *tmp1;
	struct _line *tmp2;

	i = 0;
	tmp = curscr->first_line;
	while (i < window->SR)
	{
		i++;
		tmp = tmp->next_screen;
	}
	/*
	 |	find line to delete
	 */
	top_of_win = tmp;
	if ((row + window->SR) == 0)
		curscr->first_line = top_of_win->next_screen;
	for (i = 0, tmp = top_of_win; i < row; i++)
		tmp = tmp->next_screen;
	if (tmp->prev_screen != NULL)
		tmp->prev_screen->next_screen = tmp->next_screen;
	if (tmp->next_screen != NULL)
		tmp->next_screen->prev_screen = tmp->prev_screen;
	tmp2 = tmp->next_screen;
	tmp1 = tmp;
	/*
	 |	clear deleted line
	 */
	clear_line(tmp1, 0, window->Num_cols);
	tmp1->number = -1;
	/*
	 |	find location to insert deleted line
	 */
	for (i = 0, tmp = curscr->first_line; (tmp->next_screen != NULL) && (i < window->SR); i++)
		tmp = tmp->next_screen;
	top_of_win = tmp;
	for (i = 0, tmp = top_of_win; (i < end_row) && (tmp->next_screen != NULL); i++)
		tmp = tmp->next_screen;
	tmp1->next_screen = tmp;
	tmp1->prev_screen = tmp->prev_screen;
	if (tmp1->prev_screen != NULL)
		tmp1->prev_screen->next_screen = tmp1;
	tmp->prev_screen = tmp1;

	Position(window, (window->SR + row), 0);
	String_Out(String_table[dl__], NULL, 0);
	if ((!String_table[cs__]) && (end_row < window->Num_lines))
	{
		Position(window, (window->SR + end_row), 0);
		String_Out(String_table[al__], NULL, 0);
	}
	else if ((String_table[cs__] != NULL) && (String_table[dl__] == NULL))
	{
		Position(window, (window->SR + end_row), 0);
		putchar('\n');
	}

	if (row == (window->Num_lines-1))
		tmp2 = tmp1;
	if ((row + window->SR) == 0)
		curscr->first_line = top_of_win = tmp2;
	return(tmp2);
}

void 
CLEAR_TO_EOL(window, row, column)
WINDOW *window;
int row, column;
{
	int x, y;
	struct _line *tmp1;

	for (y = 0, tmp1 = curscr->first_line; (y < (window->SR+row)) && (tmp1->next_screen != NULL); y++)
		tmp1 = tmp1->next_screen;
	for (x = column; x<window->Num_cols; x++)
	{
		tmp1->row[x] = ' ';
		tmp1->attributes[x] = '\0';
	}
	tmp1->row[column] = '\0';
	tmp1->last_char = column;
	if (column < COLS)
	{
		if (STAND)
		{
			STAND = FALSE;
			Position(window, row, column);
			attribute_off();
		}
		if (String_table[ce__] != NULL)
			String_Out(String_table[ce__], NULL, 0);
		else
		{
			for (x = column; x < window->Num_cols; x++)
				putchar(' ');
			Curr_x = x;
		}
	}
}

int 
check_delete(window, line, offset, pointer_new, pointer_old)
WINDOW *window;
int line, offset;
struct _line *pointer_new, *pointer_old;
{
	int end_old;
	int end_new;
	int k;
	int changed;
	char *old_lin;
	char *new_lin;
	char *old_att;
	char *new_att;
	
	changed = FALSE;
	new_lin = pointer_new->row;
	new_att = pointer_new->attributes;
	old_lin = pointer_old->row;
	old_att = pointer_old->attributes;
	end_old = end_new = offset;
	while (((new_lin[end_new] != old_lin[end_old]) || (new_att[end_new] != old_att[end_old])) && (old_lin[end_old] != '\0') && (new_lin[end_old] != '\0'))
		end_old++;
	if (old_lin[end_old] != '\0')
	{
		k = 0;
		while ((old_lin[end_old+k] == new_lin[end_new+k]) && (new_att[end_new+k] == old_att[end_old+k]) && (new_lin[end_new+k] != '\0') && (old_lin[end_old+k] != '\0') && (k < 10))
			k++;
		if ((k > 8) || ((new_lin[end_new+k] == '\0') && (k != 0)))
		{
			if (new_lin[end_new+k] == '\0')
			{
				Position(window, line, (end_new+k));
				CLEAR_TO_EOL(window, line, (end_new+k));
			}
			Position(window, line, offset);
			for (k = offset; k < end_old; k++)
				Char_del(old_lin, old_att, offset, window->Num_cols);
			while ((old_lin[offset] != '\0') && (offset < COLS))
				offset++;
			pointer_old->last_char = offset;
			changed = TRUE;
		}
	}
	return(changed);
}

/*
 |	Check if characters were inserted in the middle of a line, and if 
 |	so, insert them.
 */

int 
check_insert(window, line, offset, pointer_new, pointer_old)
WINDOW *window;
int line, offset;
struct _line *pointer_new, *pointer_old;
{
	int changed;
	int end_old, end_new;
	int k;
	int same = FALSE;
	int old_off;
	int insert;
	char *old_lin;
	char *new_lin;
	char *old_att;
	char *new_att;

	changed = FALSE;
	new_lin = pointer_new->row;
	new_att = pointer_new->attributes;
	old_lin = pointer_old->row;
	old_att = pointer_old->attributes;
	end_old = end_new = offset;
	while (((new_lin[end_new] != old_lin[end_old]) || (new_att[end_new] != old_att[end_old])) && (new_lin[end_new] != '\0') && (old_lin[end_new] != '\0'))
		end_new++;
	if (new_lin[end_new] != '\0')
	{
		k = 0;
		while ((old_lin[end_old+k] == new_lin[end_new+k]) && (old_att[end_old+k] == new_att[end_new+k]) && (new_lin[end_new+k] != '\0') && (old_lin[end_old+k] != '\0') && (k < 10))
			k++;
		/*
		 |  check for commonality between rest of lines (are the old 
		 |  and new lines the same, except for a chunk in the middle?)
		 |  if the rest of the lines are common, do not insert text
		 */
		old_off = end_new;
		while ((old_lin[old_off] != '\0') && (new_lin[old_off] != '\0') && (old_lin[old_off] == new_lin[old_off]) && (old_att[old_off] == new_att[old_off]))
			old_off++;
		if ((old_lin[old_off] == new_lin[old_off]) && (old_att[old_off] == new_att[old_off]))
			same = TRUE;
		if ((!same) && ((k > 8) || ((new_lin[end_new+k] == '\0') && (k != 0))))
		{
			Position(window, line, offset);
			insert = FALSE;
			if (String_table[ic__] == NULL)
			{
				String_Out(String_table[im__], NULL, 0);
				insert = TRUE;
			}
			for (k = offset; k < end_new; k++)
			{
				if (!insert)
					String_Out(String_table[ic__], NULL, 0);
				Char_ins(old_lin, old_att, new_lin[k], new_att[k], k, window->Num_cols);
			}
			if (insert)
				String_Out(String_table[ei__], NULL, 0);
			while ((old_lin[offset] != '\0') && (offset < COLS))
				offset++;
			pointer_old->last_char = offset;
			changed = TRUE;
		}
	}
	return(changed);
}

void 
doupdate()
{
	WINDOW *window;
	int similar;
	int diff;
	int begin_old, begin_new;
	int end_old, end_new;
	int count1, j;
	int from_top, tmp_ft, offset;
	int changed;
	int first_time;
	int first_same;
	int last_same;
	int list[10];
	int bottom;

	struct _line *curr;
	struct _line *virt;
	struct _line *old;

	struct _line *new;

	struct _line *old1, *new1;

	char *cur_lin;
	char *vrt_lin;
	char *cur_att;
	char *vrt_att;
	char *att1, *att2;
	char *c1, *c2;

	char NC_chinese = FALSE;	/* flag to indicate handling Chinese */

	window = virtual_scr;

	if ((nc_attributes & A_NC_BIG5) != 0)
		NC_chinese = TRUE;

	if (Repaint_screen)
	{
		if (String_table[cl__])
			String_Out(String_table[cl__], NULL, 0);
		else
		{
			from_top = 0;
			while (from_top < LINES)
			{
				Position(curscr, from_top, 0);
				if (String_table[ce__] != NULL)
					String_Out(String_table[ce__], NULL, 0);
				else
				{
					for (j = 0; j < window->Num_cols; j++)
						putchar(' ');
				}
				from_top++;
			}
		}
		for (from_top = 0, curr = curscr->first_line; from_top < curscr->Num_lines; from_top++, curr = curr->next_screen)
		{
			Position(curscr, from_top, 0);
			for (j = 0; (curr->row[j] != '\0') && (j < curscr->Num_cols); j++)
			{
				Char_out(curr->row[j], curr->attributes[j], curr->row, curr->attributes, j);
			}
			if (STAND)
			{
				STAND = FALSE;
				Position(curscr, from_top, j);
				attribute_off();
			}
		}
		Repaint_screen = FALSE;
	}

	similar = 0;
	diff = FALSE;
	top_of_win = curscr->first_line;

	for (from_top = 0, curr = top_of_win, virt = window->first_line; 
			from_top < window->Num_lines; from_top++)
	{
		virtual_lines[from_top] = TRUE;
		if ((similar = Comp_line(curr, virt)) > 0)
		{
			virtual_lines[from_top] = FALSE;
			diff = TRUE;
		}
		curr = curr->next_screen;
		virt = virt->next_screen;
	}

	from_top = 0;
	virt = window->first_line;
	curr = top_of_win;
	similar = 0;
	/*
	 |  if the window has lines that are different, check for scrolling
	 */
	if (diff)
	{
		last_same = -1;
		changed = FALSE;
		for (first_same = window->Num_lines; 
		    (first_same > from_top) && (virtual_lines[first_same - 1]);
		     first_same--)
			;
		for (last_same = 0;
		    (last_same < window->Num_lines) && (virtual_lines[last_same]== FALSE);
		     last_same++)
			;
		while ((from_top < first_same) && nc_scrolling_ability)
					/* check entire lines for diffs	*/
		{

			if (from_top >= last_same)
			{
				for (last_same = from_top; 
				     (last_same < window->Num_lines) && 
				     (virtual_lines[last_same] == FALSE);
				      last_same++)
					;
			}
			if (!virtual_lines[from_top])
			{
				diff = TRUE;
				/*
				 |	check for lines deleted (scroll up)
				 */
				for (tmp_ft = from_top+1, old = curr->next_screen; 
					((window->scroll_up) && (diff) && 
					(tmp_ft < last_same) && 
					(!virtual_lines[tmp_ft]));
						tmp_ft++)
				{
					if ((Comp_line(old, virt) == -1) && (!virtual_lines[from_top]))
					{
						/*
						 |	Find the bottom of the 
						 |	area that should be 
						 |	scrolled.
						 */
						for (bottom = tmp_ft, old1 = old, 
						     new1 = virt, count1 = 0;
							(bottom < window->Num_lines) && 
								(Comp_line(old1, new1) <= 0);
								bottom++, old1 = old1->next_screen, 
								new1 = new1->next_screen, 
								count1++)
							;
						if (count1 > 3)
						{
							if (String_table[cs__]) /* scrolling region */
							{
								list[1] = from_top;
								list[0] = min((bottom - 1), (window->Num_lines - 1));
								String_Out(String_table[cs__], list, 2);
								Curr_y = Curr_x = -1;
							}

							for (offset = (tmp_ft - from_top); (offset > 0); offset--)
							{
								old = Delete_line(from_top, min((bottom - 1), (window->Num_lines - 1)), window);
								diff = FALSE;
							}

							if (String_table[cs__]) /* scrolling region */
							{
								list[1] = 0;
								list[0] = LINES - 1;
								String_Out(String_table[cs__], list, 2);
								Curr_y = Curr_x = -1;
							}

							top_of_win = curscr->first_line;
							curr = top_of_win;
							for (offset = 0; offset < from_top; offset++)
								curr = curr->next_screen;
							for (offset = from_top, old=curr, new=virt; 
							   offset < window->Num_lines; 
							   old=old->next_screen, new=new->next_screen,
							   offset++)
							{
								similar = Comp_line(old, new);
								virtual_lines[offset] = (similar > 0 ? FALSE : TRUE);
							}
						}
					}
					else
						old = old->next_screen;
				}
				/*
				 |	check for lines inserted (scroll down)
				 */
				for (tmp_ft = from_top-1, old = curr->prev_screen; 
					((window->scroll_down) && (tmp_ft >= 0) && 
					(diff) && 
					(!virtual_lines[tmp_ft])); 
					  tmp_ft--)
				{
					if (Comp_line(old, virt) == -1)
					{
						/*
						 |	Find the bottom of the 
						 |	area that should be 
						 |	scrolled.
						 */
						for (bottom = from_top, old1 = old, 
						     new1 = virt, count1 = 0;
							(bottom < window->Num_lines) && 
								(Comp_line(old1, new1) <= 0);
								bottom++, old1 = old1->next_screen, 
								new1 = new1->next_screen, 
								count1++)
							;
						if (count1 > 3)
						{
							if (String_table[cs__]) /* scrolling region */
							{
								list[1] = tmp_ft;
								list[0] = min((bottom - 1), (window->Num_lines - 1));
								String_Out(String_table[cs__], list, 2);
								Curr_y = Curr_x = -1;
							}

							for (offset = (from_top - tmp_ft); (offset > 0); offset--)
							{
								old = Insert_line(tmp_ft, min((bottom - 1), (window->Num_lines -1)), window);
								diff = FALSE;
							}

							if (String_table[cs__]) /* scrolling region */
							{
								list[1] = 0;
								list[0] = LINES - 1;
								String_Out(String_table[cs__], list, 2);
								Curr_y = Curr_x = -1;
							}

							top_of_win = curscr->first_line;
							curr = top_of_win;
							for (offset = 0; offset < from_top; offset++)
								curr = curr->next_screen;
							for (offset = from_top, old=curr, new=virt; 
							   offset < window->Num_lines; 
							   old=old->next_screen, new=new->next_screen,
							   offset++)
							{
								similar = Comp_line(old, new);
								virtual_lines[offset] = (similar > 0 ? FALSE : TRUE);
							}
						}
					}
					else
						old = old->prev_screen;
				}
			}
			from_top++;
			curr = curr->next_screen;
			virt = virt->next_screen;
		}
	}


	/*
	 |	Scrolling done, now need to insert, delete, or modify text 
	 |	within lines.
	 */

	for (from_top = 0, curr = curscr->first_line; from_top < window->SR; from_top++)
		curr = curr->next_screen;
	top_of_win = curr;
	for (from_top = 0, curr = top_of_win, virt = window->first_line; from_top < window->Num_lines; from_top++, curr = curr->next_screen, virt = virt->next_screen)
	{

		/*
		 |	If either 'insert mode' or 'insert char' are 
		 |	available, enter the following 'if' statement, 
		 |	else, need to simply rewrite the contents of the line
		 |	at the point where the contents of the line change.
		 */

		if (((String_table[ic__]) || (String_table[im__])) && 
		    (String_table[dc__]) && (curr->row[0] != '\0') &&
		    (!NC_chinese))
		{
			j = 0;
			first_time = TRUE;
			vrt_lin = virt->row;
			vrt_att = virt->attributes;
			cur_lin = curr->row;
			cur_att = curr->attributes;
			while ((vrt_lin[j] != '\0') && (j < window->Num_cols))
			{
				if ((STAND) && (Booleans[xs__]))
				{
					while ((vrt_lin[j] == cur_lin[j]) && (vrt_att[j] == cur_att[j]) && (vrt_lin[j] != '\0') && (vrt_att[j]))
						j++;
					if ((STAND) && (!vrt_att[j]))
					{
						STAND = FALSE;
						Position(window, from_top, j);
						attribute_off();
						attribute_off();
					}
				}
				else
				{
					while ((vrt_lin[j] == cur_lin[j]) && (vrt_att[j] == cur_att[j]) && (vrt_lin[j] != '\0'))
						j++;
				}
				if ((vrt_att[j] != cur_att[j]) && (cur_att[j]) && (Booleans[xs__]))
				{
					Position(window, from_top, j);
/*					CLEAR_TO_EOL(window, from_top, j);*/
					attribute_off();
					attribute_off();
				}
				if (vrt_lin[j] != '\0')
				{
					begin_new = j;
					begin_old = j;
					end_old = j;
					end_new = j;
					if ((first_time) && (virt->changed))
					{
						if (curr->last_char <= virt->last_char)
							changed = check_insert(window, from_top, j, virt, curr);
					}
					changed = check_delete(window, from_top, j, virt, curr);
					first_time = FALSE;
					virt->changed = FALSE;
					if (!changed)
						changed = check_insert(window, from_top, j, virt, curr);
					if (((!changed) || (cur_lin[j] != vrt_lin[j]) || (cur_att[j] != vrt_att[j])) && (j < window->Num_cols))
					{
						if ((vrt_lin[j] == ' ') && (cur_lin[j] == '\0') && (vrt_att[j] == cur_att[j]))
							cur_lin[j] = ' ';
						else
						{
							Position(window, from_top, j);
							Char_out(vrt_lin[j], vrt_att[j], cur_lin, cur_att, j);
						}
					}
					if ((vrt_lin[j] != '\0'))
						j++;
				}
				if ((STAND) && (!vrt_att[j]))
				{
					STAND = FALSE;
					Position(window, from_top, j);
					attribute_off();
				}
			}
			if ((vrt_lin[j] == '\0') && (cur_lin[j] != '\0'))
			{
				Position(window, from_top, j);
				CLEAR_TO_EOL(window, from_top, j);
			}
		}
		else /*if ((similar != -1) && (similar != 0))*/
		{
			j = 0;
			c1 = curr->row;
			att1 = curr->attributes;
			c2 = virt->row;
			att2 = virt->attributes;
			while ((j < window->Num_cols) && (c2[j] != '\0'))
			{
				while ((c1[j] == c2[j]) && (att1[j] == att2[j]) && (j < window->Num_cols) && (c2[j] != '\0'))
					j++;

				/*
				 |	if previous character is an eight bit 
				 |	char, start redraw from that character
				 */

				if ((NC_chinese) && (highbitset(c1[j - 1])))
					j--;
				begin_old = j;
				begin_new = j;
				if ((j < window->Num_cols) && (c2[j] != '\0'))
				{
					Position(window, from_top, begin_old);
					CLEAR_TO_EOL(window, from_top, j);
					Position(window, from_top, begin_old);
					for (j = begin_old; (c2[j] != '\0') && (j < window->Num_cols); j++)
						Char_out(c2[j], att2[j], c1, att1, j);
				}
			}
			if ((c2[j] == '\0') && (c1[j] != '\0'))
			{
				Position(window, from_top, j);
				CLEAR_TO_EOL(window, from_top, j);
			}
		}
		if (STAND)
		{
			STAND = FALSE;
			Position(window, from_top, j);
			attribute_off();
		}
		virt->number = from_top;
	}
	Position(window, window->LY, window->LX);
}

void 
Position(window, row, col)	/* position the cursor for output on the screen	*/
WINDOW *window;
int row;
int col;
{
	int list[10];
	int place;

	int pos_row;
	int pos_column;

	pos_row = row + window->SR;
	pos_column = col + window->SC;
	if ((pos_row != Curr_y) || (pos_column != Curr_x))
	{
		if (String_table[cm__] != NULL) /* && (row < window->Num_lines) && (column < window->Num_cols))*/ 
		{
			place = 0;
			list[place++] = pos_column;
			list[place++] = pos_row;
			String_Out(String_table[cm__], list, place);
			if ((STAND) && (!Booleans[ms__]))
				attribute_on();
		}
		Curr_x = pos_column;
		Curr_y = pos_row;
	}
}

void 
Char_del(line, attrib, offset, maxlen)	/* delete chars from line	*/
char *line;
char *attrib;
int offset;
int maxlen;
{
	int one, two;

	for (one = offset, two = offset+1; (line[one] != '\0') && (one < maxlen); one++, two++)
	{
		line[one] = line[two];
		attrib[one] = attrib[two];
	}
	String_Out(String_table[dc__], NULL, 0);
}

void 
Char_ins(line, attrib, newc, newatt, offset, maxlen)	/* insert chars in line	*/
char *line;
char *attrib;
char newc;
char newatt;
int offset;
int maxlen;
{
	int one, two;

	one = 0;
	while ((line[one] != '\0') && (one < (maxlen - 2)))
		one++;
	for (two = one + 1; (two > offset); one--, two--)
	{
		line[two] = line[one];
		attrib[two] = attrib[one];
	}
	line[offset] = newc;
	attrib[offset] = newatt;
	Char_out(newc, newatt, line, attrib, offset);
}

void 
attribute_on()
{
	if (String_table[sa__])
	{
		attributes_set[0] = 1;
		String_Out(String_table[sa__], attributes_set, 1);
	}
	else if (String_table[so__])
		String_Out(String_table[so__], NULL, 0);
}

void 
attribute_off()
{
	if (String_table[me__])
		String_Out(String_table[me__], NULL, 0);
	else if (String_table[sa__])
	{
		attributes_set[0] = 0;
		String_Out(String_table[sa__], attributes_set, 1);
	}
	else if (String_table[se__])
		String_Out(String_table[se__], NULL, 0);
}

void 
Char_out(newc, newatt, line, attrib, offset)	/* output character with proper attribute	*/
char newc;
char newatt;
char *line;
char *attrib;
int offset;
{


	if ((newatt) && (!STAND))
	{
		STAND = TRUE;
		attribute_on();
	}
	else if ((STAND) && (!newatt))
	{
		STAND = FALSE;
		attribute_off();
	}

	if ((newatt) && (STAND) && (Booleans[xs__]))
	{
		attribute_on();
	}

	if (!((Curr_y >= (LINES - 1)) && (Curr_x >= (COLS - 1))))
	{
		putchar(newc);
		line[offset] = newc;
		attrib[offset] = newatt;
	}
	Curr_x++;
}

/*
 |
 |	The two routines that follow, nc_setattrib(), nc_clearattrib(), are 
 |	hacks that notify new_curse to handle characters that have the high 
 |	bit set as the first of two bytes of a multi-byte string.
 |
 */

void 
nc_setattrib(flag)
int flag;
{
	nc_attributes |= flag;
}

void 
nc_clearattrib(flag)
int flag;
{
	nc_attributes &= ~flag;
}

