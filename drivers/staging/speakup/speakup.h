#ifndef _SPEAKUP_H
#define _SPEAKUP_H

#include "spk_types.h"
#include "i18n.h"

#define SPEAKUP_VERSION "3.1.6"
#define KEY_MAP_VER 119
#define SHIFT_TBL_SIZE 64
#define MAX_DESC_LEN 72

#define TOGGLE_0 .u.n = {NULL, 0, 0, 1, 0, 0, NULL }
#define TOGGLE_1 .u.n = {NULL, 1, 0, 1, 0, 0, NULL }
#define MAXVARLEN 15

#define SYNTH_OK 0x0001
#define B_ALPHA 0x0002
#define ALPHA 0x0003
#define B_CAP 0x0004
#define A_CAP 0x0007
#define B_NUM 0x0008
#define NUM 0x0009
#define ALPHANUM (B_ALPHA|B_NUM)
#define SOME 0x0010
#define MOST 0x0020
#define PUNC 0x0040
#define A_PUNC 0x0041
#define B_WDLM 0x0080
#define WDLM 0x0081
#define B_EXNUM 0x0100
#define CH_RPT 0x0200
#define B_CTL 0x0400
#define A_CTL (B_CTL+SYNTH_OK)
#define B_SYM 0x0800
#define B_CAPSYM (B_CAP|B_SYM)

#define IS_WDLM(x) (spk_chartab[((u_char)x)]&B_WDLM)
#define IS_CHAR(x, type) (spk_chartab[((u_char)x)]&type)
#define IS_TYPE(x, type) ((spk_chartab[((u_char)x)]&type) == type)

int speakup_thread(void *data);
void spk_reset_default_chars(void);
void spk_reset_default_chartab(void);
void synth_start(void);
void synth_insert_next_index(int sent_num);
void spk_reset_index_count(int sc);
void spk_get_index_count(int *linecount, int *sentcount);
int spk_set_key_info(const u_char *key_info, u_char *k_buffer);
char *spk_strlwr(char *s);
char *spk_s2uchar(char *start, char *dest);
int speakup_kobj_init(void);
void speakup_kobj_exit(void);
int spk_chartab_get_value(char *keyword);
void speakup_register_var(struct var_t *var);
void speakup_unregister_var(enum var_id_t var_id);
struct st_var_header *spk_get_var_header(enum var_id_t var_id);
struct st_var_header *spk_var_header_by_name(const char *name);
struct punc_var_t *spk_get_punc_var(enum var_id_t var_id);
int spk_set_num_var(int val, struct st_var_header *var, int how);
int spk_set_string_var(const char *page, struct st_var_header *var, int len);
int spk_set_mask_bits(const char *input, const int which, const int how);
extern special_func spk_special_handler;
int spk_handle_help(struct vc_data *vc, u_char type, u_char ch, u_short key);
int synth_init(char *name);
void synth_release(void);

void spk_do_flush(void);
void speakup_start_ttys(void);
void synth_buffer_add(char ch);
void synth_buffer_clear(void);
void speakup_clear_selection(void);
int speakup_set_selection(struct tty_struct *tty);
int speakup_paste_selection(struct tty_struct *tty);
void speakup_cancel_paste(void);
void speakup_register_devsynth(void);
void speakup_unregister_devsynth(void);
void synth_write(const char *buf, size_t count);
int synth_supports_indexing(void);

extern struct vc_data *spk_sel_cons;
extern unsigned short spk_xs, spk_ys, spk_xe, spk_ye; /* our region points */

extern wait_queue_head_t speakup_event;
extern struct kobject *speakup_kobj;
extern struct task_struct *speakup_task;
extern const u_char spk_key_defaults[];

/* Protect speakup synthesizer list */
extern struct mutex spk_mutex;
extern struct st_spk_t *speakup_console[];
extern struct spk_synth *synth;
extern char spk_pitch_buff[];
extern u_char *spk_our_keys[];
extern short spk_punc_masks[];
extern char spk_str_caps_start[], spk_str_caps_stop[];
extern const struct st_bits_data spk_punc_info[];
extern u_char spk_key_buf[600];
extern char *spk_characters[];
extern char *spk_default_chars[];
extern u_short spk_chartab[];
extern int spk_no_intr, spk_say_ctrl, spk_say_word_ctl, spk_punc_level;
extern int spk_reading_punc, spk_attrib_bleep, spk_bleeps;
extern int spk_bleep_time, spk_bell_pos;
extern int spk_spell_delay, spk_key_echo;
extern short spk_punc_mask;
extern short spk_pitch_shift, synth_flags;
extern bool spk_quiet_boot;
extern char *synth_name;
extern struct bleep spk_unprocessed_sound;

/* Prototypes from fakekey.c. */
int speakup_add_virtual_keyboard(void);
void speakup_remove_virtual_keyboard(void);
void speakup_fake_down_arrow(void);
bool speakup_fake_key_pressed(void);

#endif
