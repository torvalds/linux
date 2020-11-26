/* SPDX-License-Identifier: GPL-2.0 */
#ifndef SPEAKUP_TYPES_H
#define SPEAKUP_TYPES_H

/* This file includes all of the typedefs and structs used in speakup. */

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/wait.h>		/* for wait_queue */
#include <linux/init.h>		/* for __init */
#include <linux/module.h>
#include <linux/vt_kern.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/io.h>		/* for inb_p, outb_p, inb, outb, etc... */
#include <linux/device.h>

enum var_type_t {
	VAR_NUM = 0,
	VAR_TIME,
	VAR_STRING,
	VAR_PROC
};

enum {
	E_DEFAULT = 0,
	E_SET,
	E_INC,
	E_DEC,
	E_NEW_DEFAULT,
};

/*
 * Note: add new members at the end, speakupmap.h depends on the values of the
 * enum starting from SPELL_DELAY (see inc_dec_var)
 */
enum var_id_t {
	VERSION = 0, SYNTH, SILENT, SYNTH_DIRECT,
	KEYMAP, CHARS,
	PUNC_SOME, PUNC_MOST, PUNC_ALL,
	DELIM, REPEATS, EXNUMBER,
	DELAY, TRIGGER, JIFFY, FULL, /* all timers must be together */
	BLEEP_TIME, CURSOR_TIME, BELL_POS,
	SAY_CONTROL, SAY_WORD_CTL, NO_INTERRUPT, KEY_ECHO,
	SPELL_DELAY, PUNC_LEVEL, READING_PUNC,
	ATTRIB_BLEEP, BLEEPS,
	RATE, PITCH, VOL, TONE, PUNCT, VOICE, FREQUENCY, LANG,
	DIRECT, PAUSE,
	CAPS_START, CAPS_STOP, CHARTAB, INFLECTION,
	MAXVARS
};

typedef int (*special_func)(struct vc_data *vc, u_char type, u_char ch,
		u_short key);

#define COLOR_BUFFER_SIZE 160

struct spk_highlight_color_track {
	/* Count of each background color */
	unsigned int bgcount[8];
	/* Buffer for characters drawn with each background color */
	u16 highbuf[8][COLOR_BUFFER_SIZE];
	/* Current index into highbuf */
	unsigned int highsize[8];
	/* Reading Position for each color */
	u_long rpos[8], rx[8], ry[8];
	/* Real Cursor Y Position */
	ulong cy;
};

struct st_spk_t {
	u_long reading_x, cursor_x;
	u_long reading_y, cursor_y;
	u_long reading_pos, cursor_pos;
	u_long go_x, go_pos;
	u_long w_top, w_bottom, w_left, w_right;
	u_char w_start, w_enabled;
	u_char reading_attr, old_attr;
	char parked, shut_up;
	struct spk_highlight_color_track ht;
	int tty_stopped;
};

/* now some defines to make these easier to use. */
#define spk_shut_up (speakup_console[vc->vc_num]->shut_up)
#define spk_killed (speakup_console[vc->vc_num]->shut_up & 0x40)
#define spk_x (speakup_console[vc->vc_num]->reading_x)
#define spk_cx (speakup_console[vc->vc_num]->cursor_x)
#define spk_y (speakup_console[vc->vc_num]->reading_y)
#define spk_cy (speakup_console[vc->vc_num]->cursor_y)
#define spk_pos (speakup_console[vc->vc_num]->reading_pos)
#define spk_cp (speakup_console[vc->vc_num]->cursor_pos)
#define goto_pos (speakup_console[vc->vc_num]->go_pos)
#define goto_x (speakup_console[vc->vc_num]->go_x)
#define win_top (speakup_console[vc->vc_num]->w_top)
#define win_bottom (speakup_console[vc->vc_num]->w_bottom)
#define win_left (speakup_console[vc->vc_num]->w_left)
#define win_right (speakup_console[vc->vc_num]->w_right)
#define win_start (speakup_console[vc->vc_num]->w_start)
#define win_enabled (speakup_console[vc->vc_num]->w_enabled)
#define spk_attr (speakup_console[vc->vc_num]->reading_attr)
#define spk_old_attr (speakup_console[vc->vc_num]->old_attr)
#define spk_parked (speakup_console[vc->vc_num]->parked)

struct st_var_header {
	char *name;
	enum var_id_t var_id;
	enum var_type_t var_type;
	void *p_val; /* ptr to programs variable to store value */
	void *data;  /* ptr to the vars data */
};

struct num_var_t {
	char *synth_fmt;
	int default_val;
	int low;
	int high;
	short offset, multiplier; /* for fiddling rates etc. */
	char *out_str;  /* if synth needs char representation of number */
	int value;	/* current value */
};

struct punc_var_t {
	enum var_id_t var_id;
	short value;
};

struct string_var_t {
	char *default_val;
};

struct var_t {
	enum var_id_t var_id;
	union {
		struct num_var_t n;
		struct string_var_t s;
	} u;
};

struct st_bits_data { /* punc, repeats, word delim bits */
	char *name;
	char *value;
	short mask;
};

struct synth_indexing {
	char *command;
	unsigned char lowindex;
	unsigned char highindex;
	unsigned char currindex;
};

struct spk_synth;

struct spk_io_ops {
	int (*synth_out)(struct spk_synth *synth, const char ch);
	int (*synth_out_unicode)(struct spk_synth *synth, u16 ch);
	void (*send_xchar)(char ch);
	void (*tiocmset)(unsigned int set, unsigned int clear);
	unsigned char (*synth_in)(void);
	unsigned char (*synth_in_nowait)(void);
	void (*flush_buffer)(void);
	int (*wait_for_xmitr)(struct spk_synth *synth);
};

struct spk_synth {
	struct list_head node;

	const char *name;
	const char *version;
	const char *long_name;
	const char *init;
	char procspeech;
	char clear;
	int delay;
	int trigger;
	int jiffies;
	int full;
	int ser;
	char *dev_name;
	short flags;
	short startup;
	const int checkval; /* for validating a proper synth module */
	struct var_t *vars;
	int *default_pitch;
	int *default_vol;
	struct spk_io_ops *io_ops;
	int (*probe)(struct spk_synth *synth);
	void (*release)(void);
	const char *(*synth_immediate)(struct spk_synth *synth,
				       const char *buff);
	void (*catch_up)(struct spk_synth *synth);
	void (*flush)(struct spk_synth *synth);
	int (*is_alive)(struct spk_synth *synth);
	int (*synth_adjust)(struct st_var_header *var);
	void (*read_buff_add)(u_char c);
	unsigned char (*get_index)(struct spk_synth *synth);
	struct synth_indexing indexing;
	int alive;
	struct attribute_group attributes;
};

/*
 * module_spk_synth() - Helper macro for registering a speakup driver
 * @__spk_synth: spk_synth struct
 * Helper macro for speakup drivers which do not do anything special in module
 * init/exit. This eliminates a lot of boilerplate. Each module may only
 * use this macro once, and calling it replaces module_init() and module_exit()
 */
#define module_spk_synth(__spk_synth) \
	module_driver(__spk_synth, synth_add, synth_remove)

struct speakup_info_t {
	spinlock_t spinlock;
	int port_tts;
	int flushing;
};

struct bleep {
	short freq;
	unsigned long jiffies;
	int active;
};
#endif
