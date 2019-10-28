// SPDX-License-Identifier: GPL-2.0+
/*
 * Front panel driver for Linux
 * Copyright (C) 2000-2008, Willy Tarreau <w@1wt.eu>
 * Copyright (C) 2016-2017 Glider bvba
 *
 * This code drives an LCD module (/dev/lcd), and a keypad (/dev/keypad)
 * connected to a parallel printer port.
 *
 * The LCD module may either be an HD44780-like 8-bit parallel LCD, or a 1-bit
 * serial module compatible with Samsung's KS0074. The pins may be connected in
 * any combination, everything is programmable.
 *
 * The keypad consists in a matrix of push buttons connecting input pins to
 * data output pins or to the ground. The combinations have to be hard-coded
 * in the driver, though several profiles exist and adding new ones is easy.
 *
 * Several profiles are provided for commonly found LCD+keypad modules on the
 * market, such as those found in Nexcom's appliances.
 *
 * FIXME:
 *      - the initialization/deinitialization process is very dirty and should
 *        be rewritten. It may even be buggy.
 *
 * TODO:
 *	- document 24 keys keyboard (3 rows of 8 cols, 32 diodes + 2 inputs)
 *      - make the LCD a part of a virtual screen of Vx*Vy
 *	- make the inputs list smp-safe
 *      - change the keyboard to a double mapping : signals -> key_id -> values
 *        so that applications can change values without knowing signals
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/parport.h>
#include <linux/list.h>

#include <linux/io.h>
#include <linux/uaccess.h>

#include <misc/charlcd.h>

#define KEYPAD_MINOR		185

#define LCD_MAXBYTES		256	/* max burst write */

#define KEYPAD_BUFFER		64

/* poll the keyboard this every second */
#define INPUT_POLL_TIME		(HZ / 50)
/* a key starts to repeat after this times INPUT_POLL_TIME */
#define KEYPAD_REP_START	(10)
/* a key repeats this times INPUT_POLL_TIME */
#define KEYPAD_REP_DELAY	(2)

/* converts an r_str() input to an active high, bits string : 000BAOSE */
#define PNL_PINPUT(a)		((((unsigned char)(a)) ^ 0x7F) >> 3)

#define PNL_PBUSY		0x80	/* inverted input, active low */
#define PNL_PACK		0x40	/* direct input, active low */
#define PNL_POUTPA		0x20	/* direct input, active high */
#define PNL_PSELECD		0x10	/* direct input, active high */
#define PNL_PERRORP		0x08	/* direct input, active low */

#define PNL_PBIDIR		0x20	/* bi-directional ports */
/* high to read data in or-ed with data out */
#define PNL_PINTEN		0x10
#define PNL_PSELECP		0x08	/* inverted output, active low */
#define PNL_PINITP		0x04	/* direct output, active low */
#define PNL_PAUTOLF		0x02	/* inverted output, active low */
#define PNL_PSTROBE		0x01	/* inverted output */

#define PNL_PD0			0x01
#define PNL_PD1			0x02
#define PNL_PD2			0x04
#define PNL_PD3			0x08
#define PNL_PD4			0x10
#define PNL_PD5			0x20
#define PNL_PD6			0x40
#define PNL_PD7			0x80

#define PIN_NONE		0
#define PIN_STROBE		1
#define PIN_D0			2
#define PIN_D1			3
#define PIN_D2			4
#define PIN_D3			5
#define PIN_D4			6
#define PIN_D5			7
#define PIN_D6			8
#define PIN_D7			9
#define PIN_AUTOLF		14
#define PIN_INITP		16
#define PIN_SELECP		17
#define PIN_NOT_SET		127

#define NOT_SET			-1

/* macros to simplify use of the parallel port */
#define r_ctr(x)        (parport_read_control((x)->port))
#define r_dtr(x)        (parport_read_data((x)->port))
#define r_str(x)        (parport_read_status((x)->port))
#define w_ctr(x, y)     (parport_write_control((x)->port, (y)))
#define w_dtr(x, y)     (parport_write_data((x)->port, (y)))

/* this defines which bits are to be used and which ones to be ignored */
/* logical or of the output bits involved in the scan matrix */
static __u8 scan_mask_o;
/* logical or of the input bits involved in the scan matrix */
static __u8 scan_mask_i;

enum input_type {
	INPUT_TYPE_STD,
	INPUT_TYPE_KBD,
};

enum input_state {
	INPUT_ST_LOW,
	INPUT_ST_RISING,
	INPUT_ST_HIGH,
	INPUT_ST_FALLING,
};

struct logical_input {
	struct list_head list;
	__u64 mask;
	__u64 value;
	enum input_type type;
	enum input_state state;
	__u8 rise_time, fall_time;
	__u8 rise_timer, fall_timer, high_timer;

	union {
		struct {	/* valid when type == INPUT_TYPE_STD */
			void (*press_fct)(int);
			void (*release_fct)(int);
			int press_data;
			int release_data;
		} std;
		struct {	/* valid when type == INPUT_TYPE_KBD */
			/* strings can be non null-terminated */
			char press_str[sizeof(void *) + sizeof(int)];
			char repeat_str[sizeof(void *) + sizeof(int)];
			char release_str[sizeof(void *) + sizeof(int)];
		} kbd;
	} u;
};

static LIST_HEAD(logical_inputs);	/* list of all defined logical inputs */

/* physical contacts history
 * Physical contacts are a 45 bits string of 9 groups of 5 bits each.
 * The 8 lower groups correspond to output bits 0 to 7, and the 9th group
 * corresponds to the ground.
 * Within each group, bits are stored in the same order as read on the port :
 * BAPSE (busy=4, ack=3, paper empty=2, select=1, error=0).
 * So, each __u64 is represented like this :
 * 0000000000000000000BAPSEBAPSEBAPSEBAPSEBAPSEBAPSEBAPSEBAPSEBAPSE
 * <-----unused------><gnd><d07><d06><d05><d04><d03><d02><d01><d00>
 */

/* what has just been read from the I/O ports */
static __u64 phys_read;
/* previous phys_read */
static __u64 phys_read_prev;
/* stabilized phys_read (phys_read|phys_read_prev) */
static __u64 phys_curr;
/* previous phys_curr */
static __u64 phys_prev;
/* 0 means that at least one logical signal needs be computed */
static char inputs_stable;

/* these variables are specific to the keypad */
static struct {
	bool enabled;
} keypad;

static char keypad_buffer[KEYPAD_BUFFER];
static int keypad_buflen;
static int keypad_start;
static char keypressed;
static wait_queue_head_t keypad_read_wait;

/* lcd-specific variables */
static struct {
	bool enabled;
	bool initialized;

	int charset;
	int proto;

	/* TODO: use union here? */
	struct {
		int e;
		int rs;
		int rw;
		int cl;
		int da;
		int bl;
	} pins;

	struct charlcd *charlcd;
} lcd;

/* Needed only for init */
static int selected_lcd_type = NOT_SET;

/*
 * Bit masks to convert LCD signals to parallel port outputs.
 * _d_ are values for data port, _c_ are for control port.
 * [0] = signal OFF, [1] = signal ON, [2] = mask
 */
#define BIT_CLR		0
#define BIT_SET		1
#define BIT_MSK		2
#define BIT_STATES	3
/*
 * one entry for each bit on the LCD
 */
#define LCD_BIT_E	0
#define LCD_BIT_RS	1
#define LCD_BIT_RW	2
#define LCD_BIT_BL	3
#define LCD_BIT_CL	4
#define LCD_BIT_DA	5
#define LCD_BITS	6

/*
 * each bit can be either connected to a DATA or CTRL port
 */
#define LCD_PORT_C	0
#define LCD_PORT_D	1
#define LCD_PORTS	2

static unsigned char lcd_bits[LCD_PORTS][LCD_BITS][BIT_STATES];

/*
 * LCD protocols
 */
#define LCD_PROTO_PARALLEL      0
#define LCD_PROTO_SERIAL        1
#define LCD_PROTO_TI_DA8XX_LCD	2

/*
 * LCD character sets
 */
#define LCD_CHARSET_NORMAL      0
#define LCD_CHARSET_KS0074      1

/*
 * LCD types
 */
#define LCD_TYPE_NONE		0
#define LCD_TYPE_CUSTOM		1
#define LCD_TYPE_OLD		2
#define LCD_TYPE_KS0074		3
#define LCD_TYPE_HANTRONIX	4
#define LCD_TYPE_NEXCOM		5

/*
 * keypad types
 */
#define KEYPAD_TYPE_NONE	0
#define KEYPAD_TYPE_OLD		1
#define KEYPAD_TYPE_NEW		2
#define KEYPAD_TYPE_NEXCOM	3

/*
 * panel profiles
 */
#define PANEL_PROFILE_CUSTOM	0
#define PANEL_PROFILE_OLD	1
#define PANEL_PROFILE_NEW	2
#define PANEL_PROFILE_HANTRONIX	3
#define PANEL_PROFILE_NEXCOM	4
#define PANEL_PROFILE_LARGE	5

/*
 * Construct custom config from the kernel's configuration
 */
#define DEFAULT_PARPORT         0
#define DEFAULT_PROFILE         PANEL_PROFILE_LARGE
#define DEFAULT_KEYPAD_TYPE     KEYPAD_TYPE_OLD
#define DEFAULT_LCD_TYPE        LCD_TYPE_OLD
#define DEFAULT_LCD_HEIGHT      2
#define DEFAULT_LCD_WIDTH       40
#define DEFAULT_LCD_BWIDTH      40
#define DEFAULT_LCD_HWIDTH      64
#define DEFAULT_LCD_CHARSET     LCD_CHARSET_NORMAL
#define DEFAULT_LCD_PROTO       LCD_PROTO_PARALLEL

#define DEFAULT_LCD_PIN_E       PIN_AUTOLF
#define DEFAULT_LCD_PIN_RS      PIN_SELECP
#define DEFAULT_LCD_PIN_RW      PIN_INITP
#define DEFAULT_LCD_PIN_SCL     PIN_STROBE
#define DEFAULT_LCD_PIN_SDA     PIN_D0
#define DEFAULT_LCD_PIN_BL      PIN_NOT_SET

#ifdef CONFIG_PANEL_PARPORT
#undef DEFAULT_PARPORT
#define DEFAULT_PARPORT CONFIG_PANEL_PARPORT
#endif

#ifdef CONFIG_PANEL_PROFILE
#undef DEFAULT_PROFILE
#define DEFAULT_PROFILE CONFIG_PANEL_PROFILE
#endif

#if DEFAULT_PROFILE == 0	/* custom */
#ifdef CONFIG_PANEL_KEYPAD
#undef DEFAULT_KEYPAD_TYPE
#define DEFAULT_KEYPAD_TYPE CONFIG_PANEL_KEYPAD
#endif

#ifdef CONFIG_PANEL_LCD
#undef DEFAULT_LCD_TYPE
#define DEFAULT_LCD_TYPE CONFIG_PANEL_LCD
#endif

#ifdef CONFIG_PANEL_LCD_HEIGHT
#undef DEFAULT_LCD_HEIGHT
#define DEFAULT_LCD_HEIGHT CONFIG_PANEL_LCD_HEIGHT
#endif

#ifdef CONFIG_PANEL_LCD_WIDTH
#undef DEFAULT_LCD_WIDTH
#define DEFAULT_LCD_WIDTH CONFIG_PANEL_LCD_WIDTH
#endif

#ifdef CONFIG_PANEL_LCD_BWIDTH
#undef DEFAULT_LCD_BWIDTH
#define DEFAULT_LCD_BWIDTH CONFIG_PANEL_LCD_BWIDTH
#endif

#ifdef CONFIG_PANEL_LCD_HWIDTH
#undef DEFAULT_LCD_HWIDTH
#define DEFAULT_LCD_HWIDTH CONFIG_PANEL_LCD_HWIDTH
#endif

#ifdef CONFIG_PANEL_LCD_CHARSET
#undef DEFAULT_LCD_CHARSET
#define DEFAULT_LCD_CHARSET CONFIG_PANEL_LCD_CHARSET
#endif

#ifdef CONFIG_PANEL_LCD_PROTO
#undef DEFAULT_LCD_PROTO
#define DEFAULT_LCD_PROTO CONFIG_PANEL_LCD_PROTO
#endif

#ifdef CONFIG_PANEL_LCD_PIN_E
#undef DEFAULT_LCD_PIN_E
#define DEFAULT_LCD_PIN_E CONFIG_PANEL_LCD_PIN_E
#endif

#ifdef CONFIG_PANEL_LCD_PIN_RS
#undef DEFAULT_LCD_PIN_RS
#define DEFAULT_LCD_PIN_RS CONFIG_PANEL_LCD_PIN_RS
#endif

#ifdef CONFIG_PANEL_LCD_PIN_RW
#undef DEFAULT_LCD_PIN_RW
#define DEFAULT_LCD_PIN_RW CONFIG_PANEL_LCD_PIN_RW
#endif

#ifdef CONFIG_PANEL_LCD_PIN_SCL
#undef DEFAULT_LCD_PIN_SCL
#define DEFAULT_LCD_PIN_SCL CONFIG_PANEL_LCD_PIN_SCL
#endif

#ifdef CONFIG_PANEL_LCD_PIN_SDA
#undef DEFAULT_LCD_PIN_SDA
#define DEFAULT_LCD_PIN_SDA CONFIG_PANEL_LCD_PIN_SDA
#endif

#ifdef CONFIG_PANEL_LCD_PIN_BL
#undef DEFAULT_LCD_PIN_BL
#define DEFAULT_LCD_PIN_BL CONFIG_PANEL_LCD_PIN_BL
#endif

#endif /* DEFAULT_PROFILE == 0 */

/* global variables */

/* Device single-open policy control */
static atomic_t keypad_available = ATOMIC_INIT(1);

static struct pardevice *pprt;

static int keypad_initialized;

static DEFINE_SPINLOCK(pprt_lock);
static struct timer_list scan_timer;

MODULE_DESCRIPTION("Generic parallel port LCD/Keypad driver");

static int parport = DEFAULT_PARPORT;
module_param(parport, int, 0000);
MODULE_PARM_DESC(parport, "Parallel port index (0=lpt1, 1=lpt2, ...)");

static int profile = DEFAULT_PROFILE;
module_param(profile, int, 0000);
MODULE_PARM_DESC(profile,
		 "1=16x2 old kp; 2=serial 16x2, new kp; 3=16x2 hantronix; "
		 "4=16x2 nexcom; default=40x2, old kp");

static int keypad_type = NOT_SET;
module_param(keypad_type, int, 0000);
MODULE_PARM_DESC(keypad_type,
		 "Keypad type: 0=none, 1=old 6 keys, 2=new 6+1 keys, 3=nexcom 4 keys");

static int lcd_type = NOT_SET;
module_param(lcd_type, int, 0000);
MODULE_PARM_DESC(lcd_type,
		 "LCD type: 0=none, 1=compiled-in, 2=old, 3=serial ks0074, 4=hantronix, 5=nexcom");

static int lcd_height = NOT_SET;
module_param(lcd_height, int, 0000);
MODULE_PARM_DESC(lcd_height, "Number of lines on the LCD");

static int lcd_width = NOT_SET;
module_param(lcd_width, int, 0000);
MODULE_PARM_DESC(lcd_width, "Number of columns on the LCD");

static int lcd_bwidth = NOT_SET;	/* internal buffer width (usually 40) */
module_param(lcd_bwidth, int, 0000);
MODULE_PARM_DESC(lcd_bwidth, "Internal LCD line width (40)");

static int lcd_hwidth = NOT_SET;	/* hardware buffer width (usually 64) */
module_param(lcd_hwidth, int, 0000);
MODULE_PARM_DESC(lcd_hwidth, "LCD line hardware address (64)");

static int lcd_charset = NOT_SET;
module_param(lcd_charset, int, 0000);
MODULE_PARM_DESC(lcd_charset, "LCD character set: 0=standard, 1=KS0074");

static int lcd_proto = NOT_SET;
module_param(lcd_proto, int, 0000);
MODULE_PARM_DESC(lcd_proto,
		 "LCD communication: 0=parallel (//), 1=serial, 2=TI LCD Interface");

/*
 * These are the parallel port pins the LCD control signals are connected to.
 * Set this to 0 if the signal is not used. Set it to its opposite value
 * (negative) if the signal is negated. -MAXINT is used to indicate that the
 * pin has not been explicitly specified.
 *
 * WARNING! no check will be performed about collisions with keypad !
 */

static int lcd_e_pin  = PIN_NOT_SET;
module_param(lcd_e_pin, int, 0000);
MODULE_PARM_DESC(lcd_e_pin,
		 "# of the // port pin connected to LCD 'E' signal, with polarity (-17..17)");

static int lcd_rs_pin = PIN_NOT_SET;
module_param(lcd_rs_pin, int, 0000);
MODULE_PARM_DESC(lcd_rs_pin,
		 "# of the // port pin connected to LCD 'RS' signal, with polarity (-17..17)");

static int lcd_rw_pin = PIN_NOT_SET;
module_param(lcd_rw_pin, int, 0000);
MODULE_PARM_DESC(lcd_rw_pin,
		 "# of the // port pin connected to LCD 'RW' signal, with polarity (-17..17)");

static int lcd_cl_pin = PIN_NOT_SET;
module_param(lcd_cl_pin, int, 0000);
MODULE_PARM_DESC(lcd_cl_pin,
		 "# of the // port pin connected to serial LCD 'SCL' signal, with polarity (-17..17)");

static int lcd_da_pin = PIN_NOT_SET;
module_param(lcd_da_pin, int, 0000);
MODULE_PARM_DESC(lcd_da_pin,
		 "# of the // port pin connected to serial LCD 'SDA' signal, with polarity (-17..17)");

static int lcd_bl_pin = PIN_NOT_SET;
module_param(lcd_bl_pin, int, 0000);
MODULE_PARM_DESC(lcd_bl_pin,
		 "# of the // port pin connected to LCD backlight, with polarity (-17..17)");

/* Deprecated module parameters - consider not using them anymore */

static int lcd_enabled = NOT_SET;
module_param(lcd_enabled, int, 0000);
MODULE_PARM_DESC(lcd_enabled, "Deprecated option, use lcd_type instead");

static int keypad_enabled = NOT_SET;
module_param(keypad_enabled, int, 0000);
MODULE_PARM_DESC(keypad_enabled, "Deprecated option, use keypad_type instead");

/* for some LCD drivers (ks0074) we need a charset conversion table. */
static const unsigned char lcd_char_conv_ks0074[256] = {
	/*          0|8   1|9   2|A   3|B   4|C   5|D   6|E   7|F */
	/* 0x00 */ 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	/* 0x08 */ 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	/* 0x10 */ 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	/* 0x18 */ 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	/* 0x20 */ 0x20, 0x21, 0x22, 0x23, 0xa2, 0x25, 0x26, 0x27,
	/* 0x28 */ 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
	/* 0x30 */ 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	/* 0x38 */ 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
	/* 0x40 */ 0xa0, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
	/* 0x48 */ 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
	/* 0x50 */ 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
	/* 0x58 */ 0x58, 0x59, 0x5a, 0xfa, 0xfb, 0xfc, 0x1d, 0xc4,
	/* 0x60 */ 0x96, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
	/* 0x68 */ 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
	/* 0x70 */ 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
	/* 0x78 */ 0x78, 0x79, 0x7a, 0xfd, 0xfe, 0xff, 0xce, 0x20,
	/* 0x80 */ 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	/* 0x88 */ 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
	/* 0x90 */ 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
	/* 0x98 */ 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
	/* 0xA0 */ 0x20, 0x40, 0xb1, 0xa1, 0x24, 0xa3, 0xfe, 0x5f,
	/* 0xA8 */ 0x22, 0xc8, 0x61, 0x14, 0x97, 0x2d, 0xad, 0x96,
	/* 0xB0 */ 0x80, 0x8c, 0x82, 0x83, 0x27, 0x8f, 0x86, 0xdd,
	/* 0xB8 */ 0x2c, 0x81, 0x6f, 0x15, 0x8b, 0x8a, 0x84, 0x60,
	/* 0xC0 */ 0xe2, 0xe2, 0xe2, 0x5b, 0x5b, 0xae, 0xbc, 0xa9,
	/* 0xC8 */ 0xc5, 0xbf, 0xc6, 0xf1, 0xe3, 0xe3, 0xe3, 0xe3,
	/* 0xD0 */ 0x44, 0x5d, 0xa8, 0xe4, 0xec, 0xec, 0x5c, 0x78,
	/* 0xD8 */ 0xab, 0xa6, 0xe5, 0x5e, 0x5e, 0xe6, 0xaa, 0xbe,
	/* 0xE0 */ 0x7f, 0xe7, 0xaf, 0x7b, 0x7b, 0xaf, 0xbd, 0xc8,
	/* 0xE8 */ 0xa4, 0xa5, 0xc7, 0xf6, 0xa7, 0xe8, 0x69, 0x69,
	/* 0xF0 */ 0xed, 0x7d, 0xa8, 0xe4, 0xec, 0x5c, 0x5c, 0x25,
	/* 0xF8 */ 0xac, 0xa6, 0xea, 0xef, 0x7e, 0xeb, 0xb2, 0x79,
};

static const char old_keypad_profile[][4][9] = {
	{"S0", "Left\n", "Left\n", ""},
	{"S1", "Down\n", "Down\n", ""},
	{"S2", "Up\n", "Up\n", ""},
	{"S3", "Right\n", "Right\n", ""},
	{"S4", "Esc\n", "Esc\n", ""},
	{"S5", "Ret\n", "Ret\n", ""},
	{"", "", "", ""}
};

/* signals, press, repeat, release */
static const char new_keypad_profile[][4][9] = {
	{"S0", "Left\n", "Left\n", ""},
	{"S1", "Down\n", "Down\n", ""},
	{"S2", "Up\n", "Up\n", ""},
	{"S3", "Right\n", "Right\n", ""},
	{"S4s5", "", "Esc\n", "Esc\n"},
	{"s4S5", "", "Ret\n", "Ret\n"},
	{"S4S5", "Help\n", "", ""},
	/* add new signals above this line */
	{"", "", "", ""}
};

/* signals, press, repeat, release */
static const char nexcom_keypad_profile[][4][9] = {
	{"a-p-e-", "Down\n", "Down\n", ""},
	{"a-p-E-", "Ret\n", "Ret\n", ""},
	{"a-P-E-", "Esc\n", "Esc\n", ""},
	{"a-P-e-", "Up\n", "Up\n", ""},
	/* add new signals above this line */
	{"", "", "", ""}
};

static const char (*keypad_profile)[4][9] = old_keypad_profile;

static DECLARE_BITMAP(bits, LCD_BITS);

static void lcd_get_bits(unsigned int port, int *val)
{
	unsigned int bit, state;

	for (bit = 0; bit < LCD_BITS; bit++) {
		state = test_bit(bit, bits) ? BIT_SET : BIT_CLR;
		*val &= lcd_bits[port][bit][BIT_MSK];
		*val |= lcd_bits[port][bit][state];
	}
}

/* sets data port bits according to current signals values */
static int set_data_bits(void)
{
	int val;

	val = r_dtr(pprt);
	lcd_get_bits(LCD_PORT_D, &val);
	w_dtr(pprt, val);
	return val;
}

/* sets ctrl port bits according to current signals values */
static int set_ctrl_bits(void)
{
	int val;

	val = r_ctr(pprt);
	lcd_get_bits(LCD_PORT_C, &val);
	w_ctr(pprt, val);
	return val;
}

/* sets ctrl & data port bits according to current signals values */
static void panel_set_bits(void)
{
	set_data_bits();
	set_ctrl_bits();
}

/*
 * Converts a parallel port pin (from -25 to 25) to data and control ports
 * masks, and data and control port bits. The signal will be considered
 * unconnected if it's on pin 0 or an invalid pin (<-25 or >25).
 *
 * Result will be used this way :
 *   out(dport, in(dport) & d_val[2] | d_val[signal_state])
 *   out(cport, in(cport) & c_val[2] | c_val[signal_state])
 */
static void pin_to_bits(int pin, unsigned char *d_val, unsigned char *c_val)
{
	int d_bit, c_bit, inv;

	d_val[0] = 0;
	c_val[0] = 0;
	d_val[1] = 0;
	c_val[1] = 0;
	d_val[2] = 0xFF;
	c_val[2] = 0xFF;

	if (pin == 0)
		return;

	inv = (pin < 0);
	if (inv)
		pin = -pin;

	d_bit = 0;
	c_bit = 0;

	switch (pin) {
	case PIN_STROBE:	/* strobe, inverted */
		c_bit = PNL_PSTROBE;
		inv = !inv;
		break;
	case PIN_D0...PIN_D7:	/* D0 - D7 = 2 - 9 */
		d_bit = 1 << (pin - 2);
		break;
	case PIN_AUTOLF:	/* autofeed, inverted */
		c_bit = PNL_PAUTOLF;
		inv = !inv;
		break;
	case PIN_INITP:		/* init, direct */
		c_bit = PNL_PINITP;
		break;
	case PIN_SELECP:	/* select_in, inverted */
		c_bit = PNL_PSELECP;
		inv = !inv;
		break;
	default:		/* unknown pin, ignore */
		break;
	}

	if (c_bit) {
		c_val[2] &= ~c_bit;
		c_val[!inv] = c_bit;
	} else if (d_bit) {
		d_val[2] &= ~d_bit;
		d_val[!inv] = d_bit;
	}
}

/*
 * send a serial byte to the LCD panel. The caller is responsible for locking
 * if needed.
 */
static void lcd_send_serial(int byte)
{
	int bit;

	/*
	 * the data bit is set on D0, and the clock on STROBE.
	 * LCD reads D0 on STROBE's rising edge.
	 */
	for (bit = 0; bit < 8; bit++) {
		clear_bit(LCD_BIT_CL, bits);	/* CLK low */
		panel_set_bits();
		if (byte & 1) {
			set_bit(LCD_BIT_DA, bits);
		} else {
			clear_bit(LCD_BIT_DA, bits);
		}

		panel_set_bits();
		udelay(2);  /* maintain the data during 2 us before CLK up */
		set_bit(LCD_BIT_CL, bits);	/* CLK high */
		panel_set_bits();
		udelay(1);  /* maintain the strobe during 1 us */
		byte >>= 1;
	}
}

/* turn the backlight on or off */
static void lcd_backlight(struct charlcd *charlcd, int on)
{
	if (lcd.pins.bl == PIN_NONE)
		return;

	/* The backlight is activated by setting the AUTOFEED line to +5V  */
	spin_lock_irq(&pprt_lock);
	if (on)
		set_bit(LCD_BIT_BL, bits);
	else
		clear_bit(LCD_BIT_BL, bits);
	panel_set_bits();
	spin_unlock_irq(&pprt_lock);
}

/* send a command to the LCD panel in serial mode */
static void lcd_write_cmd_s(struct charlcd *charlcd, int cmd)
{
	spin_lock_irq(&pprt_lock);
	lcd_send_serial(0x1F);	/* R/W=W, RS=0 */
	lcd_send_serial(cmd & 0x0F);
	lcd_send_serial((cmd >> 4) & 0x0F);
	udelay(40);		/* the shortest command takes at least 40 us */
	spin_unlock_irq(&pprt_lock);
}

/* send data to the LCD panel in serial mode */
static void lcd_write_data_s(struct charlcd *charlcd, int data)
{
	spin_lock_irq(&pprt_lock);
	lcd_send_serial(0x5F);	/* R/W=W, RS=1 */
	lcd_send_serial(data & 0x0F);
	lcd_send_serial((data >> 4) & 0x0F);
	udelay(40);		/* the shortest data takes at least 40 us */
	spin_unlock_irq(&pprt_lock);
}

/* send a command to the LCD panel in 8 bits parallel mode */
static void lcd_write_cmd_p8(struct charlcd *charlcd, int cmd)
{
	spin_lock_irq(&pprt_lock);
	/* present the data to the data port */
	w_dtr(pprt, cmd);
	udelay(20);	/* maintain the data during 20 us before the strobe */

	set_bit(LCD_BIT_E, bits);
	clear_bit(LCD_BIT_RS, bits);
	clear_bit(LCD_BIT_RW, bits);
	set_ctrl_bits();

	udelay(40);	/* maintain the strobe during 40 us */

	clear_bit(LCD_BIT_E, bits);
	set_ctrl_bits();

	udelay(120);	/* the shortest command takes at least 120 us */
	spin_unlock_irq(&pprt_lock);
}

/* send data to the LCD panel in 8 bits parallel mode */
static void lcd_write_data_p8(struct charlcd *charlcd, int data)
{
	spin_lock_irq(&pprt_lock);
	/* present the data to the data port */
	w_dtr(pprt, data);
	udelay(20);	/* maintain the data during 20 us before the strobe */

	set_bit(LCD_BIT_E, bits);
	set_bit(LCD_BIT_RS, bits);
	clear_bit(LCD_BIT_RW, bits);
	set_ctrl_bits();

	udelay(40);	/* maintain the strobe during 40 us */

	clear_bit(LCD_BIT_E, bits);
	set_ctrl_bits();

	udelay(45);	/* the shortest data takes at least 45 us */
	spin_unlock_irq(&pprt_lock);
}

/* send a command to the TI LCD panel */
static void lcd_write_cmd_tilcd(struct charlcd *charlcd, int cmd)
{
	spin_lock_irq(&pprt_lock);
	/* present the data to the control port */
	w_ctr(pprt, cmd);
	udelay(60);
	spin_unlock_irq(&pprt_lock);
}

/* send data to the TI LCD panel */
static void lcd_write_data_tilcd(struct charlcd *charlcd, int data)
{
	spin_lock_irq(&pprt_lock);
	/* present the data to the data port */
	w_dtr(pprt, data);
	udelay(60);
	spin_unlock_irq(&pprt_lock);
}

/* fills the display with spaces and resets X/Y */
static void lcd_clear_fast_s(struct charlcd *charlcd)
{
	int pos;

	spin_lock_irq(&pprt_lock);
	for (pos = 0; pos < charlcd->height * charlcd->hwidth; pos++) {
		lcd_send_serial(0x5F);	/* R/W=W, RS=1 */
		lcd_send_serial(' ' & 0x0F);
		lcd_send_serial((' ' >> 4) & 0x0F);
		/* the shortest data takes at least 40 us */
		udelay(40);
	}
	spin_unlock_irq(&pprt_lock);
}

/* fills the display with spaces and resets X/Y */
static void lcd_clear_fast_p8(struct charlcd *charlcd)
{
	int pos;

	spin_lock_irq(&pprt_lock);
	for (pos = 0; pos < charlcd->height * charlcd->hwidth; pos++) {
		/* present the data to the data port */
		w_dtr(pprt, ' ');

		/* maintain the data during 20 us before the strobe */
		udelay(20);

		set_bit(LCD_BIT_E, bits);
		set_bit(LCD_BIT_RS, bits);
		clear_bit(LCD_BIT_RW, bits);
		set_ctrl_bits();

		/* maintain the strobe during 40 us */
		udelay(40);

		clear_bit(LCD_BIT_E, bits);
		set_ctrl_bits();

		/* the shortest data takes at least 45 us */
		udelay(45);
	}
	spin_unlock_irq(&pprt_lock);
}

/* fills the display with spaces and resets X/Y */
static void lcd_clear_fast_tilcd(struct charlcd *charlcd)
{
	int pos;

	spin_lock_irq(&pprt_lock);
	for (pos = 0; pos < charlcd->height * charlcd->hwidth; pos++) {
		/* present the data to the data port */
		w_dtr(pprt, ' ');
		udelay(60);
	}

	spin_unlock_irq(&pprt_lock);
}

static const struct charlcd_ops charlcd_serial_ops = {
	.write_cmd	= lcd_write_cmd_s,
	.write_data	= lcd_write_data_s,
	.clear_fast	= lcd_clear_fast_s,
	.backlight	= lcd_backlight,
};

static const struct charlcd_ops charlcd_parallel_ops = {
	.write_cmd	= lcd_write_cmd_p8,
	.write_data	= lcd_write_data_p8,
	.clear_fast	= lcd_clear_fast_p8,
	.backlight	= lcd_backlight,
};

static const struct charlcd_ops charlcd_tilcd_ops = {
	.write_cmd	= lcd_write_cmd_tilcd,
	.write_data	= lcd_write_data_tilcd,
	.clear_fast	= lcd_clear_fast_tilcd,
	.backlight	= lcd_backlight,
};

/* initialize the LCD driver */
static void lcd_init(void)
{
	struct charlcd *charlcd;

	charlcd = charlcd_alloc(0);
	if (!charlcd)
		return;

	/*
	 * Init lcd struct with load-time values to preserve exact
	 * current functionality (at least for now).
	 */
	charlcd->height = lcd_height;
	charlcd->width = lcd_width;
	charlcd->bwidth = lcd_bwidth;
	charlcd->hwidth = lcd_hwidth;

	switch (selected_lcd_type) {
	case LCD_TYPE_OLD:
		/* parallel mode, 8 bits */
		lcd.proto = LCD_PROTO_PARALLEL;
		lcd.charset = LCD_CHARSET_NORMAL;
		lcd.pins.e = PIN_STROBE;
		lcd.pins.rs = PIN_AUTOLF;

		charlcd->width = 40;
		charlcd->bwidth = 40;
		charlcd->hwidth = 64;
		charlcd->height = 2;
		break;
	case LCD_TYPE_KS0074:
		/* serial mode, ks0074 */
		lcd.proto = LCD_PROTO_SERIAL;
		lcd.charset = LCD_CHARSET_KS0074;
		lcd.pins.bl = PIN_AUTOLF;
		lcd.pins.cl = PIN_STROBE;
		lcd.pins.da = PIN_D0;

		charlcd->width = 16;
		charlcd->bwidth = 40;
		charlcd->hwidth = 16;
		charlcd->height = 2;
		break;
	case LCD_TYPE_NEXCOM:
		/* parallel mode, 8 bits, generic */
		lcd.proto = LCD_PROTO_PARALLEL;
		lcd.charset = LCD_CHARSET_NORMAL;
		lcd.pins.e = PIN_AUTOLF;
		lcd.pins.rs = PIN_SELECP;
		lcd.pins.rw = PIN_INITP;

		charlcd->width = 16;
		charlcd->bwidth = 40;
		charlcd->hwidth = 64;
		charlcd->height = 2;
		break;
	case LCD_TYPE_CUSTOM:
		/* customer-defined */
		lcd.proto = DEFAULT_LCD_PROTO;
		lcd.charset = DEFAULT_LCD_CHARSET;
		/* default geometry will be set later */
		break;
	case LCD_TYPE_HANTRONIX:
		/* parallel mode, 8 bits, hantronix-like */
	default:
		lcd.proto = LCD_PROTO_PARALLEL;
		lcd.charset = LCD_CHARSET_NORMAL;
		lcd.pins.e = PIN_STROBE;
		lcd.pins.rs = PIN_SELECP;

		charlcd->width = 16;
		charlcd->bwidth = 40;
		charlcd->hwidth = 64;
		charlcd->height = 2;
		break;
	}

	/* Overwrite with module params set on loading */
	if (lcd_height != NOT_SET)
		charlcd->height = lcd_height;
	if (lcd_width != NOT_SET)
		charlcd->width = lcd_width;
	if (lcd_bwidth != NOT_SET)
		charlcd->bwidth = lcd_bwidth;
	if (lcd_hwidth != NOT_SET)
		charlcd->hwidth = lcd_hwidth;
	if (lcd_charset != NOT_SET)
		lcd.charset = lcd_charset;
	if (lcd_proto != NOT_SET)
		lcd.proto = lcd_proto;
	if (lcd_e_pin != PIN_NOT_SET)
		lcd.pins.e = lcd_e_pin;
	if (lcd_rs_pin != PIN_NOT_SET)
		lcd.pins.rs = lcd_rs_pin;
	if (lcd_rw_pin != PIN_NOT_SET)
		lcd.pins.rw = lcd_rw_pin;
	if (lcd_cl_pin != PIN_NOT_SET)
		lcd.pins.cl = lcd_cl_pin;
	if (lcd_da_pin != PIN_NOT_SET)
		lcd.pins.da = lcd_da_pin;
	if (lcd_bl_pin != PIN_NOT_SET)
		lcd.pins.bl = lcd_bl_pin;

	/* this is used to catch wrong and default values */
	if (charlcd->width <= 0)
		charlcd->width = DEFAULT_LCD_WIDTH;
	if (charlcd->bwidth <= 0)
		charlcd->bwidth = DEFAULT_LCD_BWIDTH;
	if (charlcd->hwidth <= 0)
		charlcd->hwidth = DEFAULT_LCD_HWIDTH;
	if (charlcd->height <= 0)
		charlcd->height = DEFAULT_LCD_HEIGHT;

	if (lcd.proto == LCD_PROTO_SERIAL) {	/* SERIAL */
		charlcd->ops = &charlcd_serial_ops;

		if (lcd.pins.cl == PIN_NOT_SET)
			lcd.pins.cl = DEFAULT_LCD_PIN_SCL;
		if (lcd.pins.da == PIN_NOT_SET)
			lcd.pins.da = DEFAULT_LCD_PIN_SDA;

	} else if (lcd.proto == LCD_PROTO_PARALLEL) {	/* PARALLEL */
		charlcd->ops = &charlcd_parallel_ops;

		if (lcd.pins.e == PIN_NOT_SET)
			lcd.pins.e = DEFAULT_LCD_PIN_E;
		if (lcd.pins.rs == PIN_NOT_SET)
			lcd.pins.rs = DEFAULT_LCD_PIN_RS;
		if (lcd.pins.rw == PIN_NOT_SET)
			lcd.pins.rw = DEFAULT_LCD_PIN_RW;
	} else {
		charlcd->ops = &charlcd_tilcd_ops;
	}

	if (lcd.pins.bl == PIN_NOT_SET)
		lcd.pins.bl = DEFAULT_LCD_PIN_BL;

	if (lcd.pins.e == PIN_NOT_SET)
		lcd.pins.e = PIN_NONE;
	if (lcd.pins.rs == PIN_NOT_SET)
		lcd.pins.rs = PIN_NONE;
	if (lcd.pins.rw == PIN_NOT_SET)
		lcd.pins.rw = PIN_NONE;
	if (lcd.pins.bl == PIN_NOT_SET)
		lcd.pins.bl = PIN_NONE;
	if (lcd.pins.cl == PIN_NOT_SET)
		lcd.pins.cl = PIN_NONE;
	if (lcd.pins.da == PIN_NOT_SET)
		lcd.pins.da = PIN_NONE;

	if (lcd.charset == NOT_SET)
		lcd.charset = DEFAULT_LCD_CHARSET;

	if (lcd.charset == LCD_CHARSET_KS0074)
		charlcd->char_conv = lcd_char_conv_ks0074;
	else
		charlcd->char_conv = NULL;

	pin_to_bits(lcd.pins.e, lcd_bits[LCD_PORT_D][LCD_BIT_E],
		    lcd_bits[LCD_PORT_C][LCD_BIT_E]);
	pin_to_bits(lcd.pins.rs, lcd_bits[LCD_PORT_D][LCD_BIT_RS],
		    lcd_bits[LCD_PORT_C][LCD_BIT_RS]);
	pin_to_bits(lcd.pins.rw, lcd_bits[LCD_PORT_D][LCD_BIT_RW],
		    lcd_bits[LCD_PORT_C][LCD_BIT_RW]);
	pin_to_bits(lcd.pins.bl, lcd_bits[LCD_PORT_D][LCD_BIT_BL],
		    lcd_bits[LCD_PORT_C][LCD_BIT_BL]);
	pin_to_bits(lcd.pins.cl, lcd_bits[LCD_PORT_D][LCD_BIT_CL],
		    lcd_bits[LCD_PORT_C][LCD_BIT_CL]);
	pin_to_bits(lcd.pins.da, lcd_bits[LCD_PORT_D][LCD_BIT_DA],
		    lcd_bits[LCD_PORT_C][LCD_BIT_DA]);

	lcd.charlcd = charlcd;
	lcd.initialized = true;
}

/*
 * These are the file operation function for user access to /dev/keypad
 */

static ssize_t keypad_read(struct file *file,
			   char __user *buf, size_t count, loff_t *ppos)
{
	unsigned i = *ppos;
	char __user *tmp = buf;

	if (keypad_buflen == 0) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(keypad_read_wait,
					     keypad_buflen != 0))
			return -EINTR;
	}

	for (; count-- > 0 && (keypad_buflen > 0);
	     ++i, ++tmp, --keypad_buflen) {
		put_user(keypad_buffer[keypad_start], tmp);
		keypad_start = (keypad_start + 1) % KEYPAD_BUFFER;
	}
	*ppos = i;

	return tmp - buf;
}

static int keypad_open(struct inode *inode, struct file *file)
{
	int ret;

	ret = -EBUSY;
	if (!atomic_dec_and_test(&keypad_available))
		goto fail;	/* open only once at a time */

	ret = -EPERM;
	if (file->f_mode & FMODE_WRITE)	/* device is read-only */
		goto fail;

	keypad_buflen = 0;	/* flush the buffer on opening */
	return 0;
 fail:
	atomic_inc(&keypad_available);
	return ret;
}

static int keypad_release(struct inode *inode, struct file *file)
{
	atomic_inc(&keypad_available);
	return 0;
}

static const struct file_operations keypad_fops = {
	.read    = keypad_read,		/* read */
	.open    = keypad_open,		/* open */
	.release = keypad_release,	/* close */
	.llseek  = default_llseek,
};

static struct miscdevice keypad_dev = {
	.minor	= KEYPAD_MINOR,
	.name	= "keypad",
	.fops	= &keypad_fops,
};

static void keypad_send_key(const char *string, int max_len)
{
	/* send the key to the device only if a process is attached to it. */
	if (!atomic_read(&keypad_available)) {
		while (max_len-- && keypad_buflen < KEYPAD_BUFFER && *string) {
			keypad_buffer[(keypad_start + keypad_buflen++) %
				      KEYPAD_BUFFER] = *string++;
		}
		wake_up_interruptible(&keypad_read_wait);
	}
}

/* this function scans all the bits involving at least one logical signal,
 * and puts the results in the bitfield "phys_read" (one bit per established
 * contact), and sets "phys_read_prev" to "phys_read".
 *
 * Note: to debounce input signals, we will only consider as switched a signal
 * which is stable across 2 measures. Signals which are different between two
 * reads will be kept as they previously were in their logical form (phys_prev).
 * A signal which has just switched will have a 1 in
 * (phys_read ^ phys_read_prev).
 */
static void phys_scan_contacts(void)
{
	int bit, bitval;
	char oldval;
	char bitmask;
	char gndmask;

	phys_prev = phys_curr;
	phys_read_prev = phys_read;
	phys_read = 0;		/* flush all signals */

	/* keep track of old value, with all outputs disabled */
	oldval = r_dtr(pprt) | scan_mask_o;
	/* activate all keyboard outputs (active low) */
	w_dtr(pprt, oldval & ~scan_mask_o);

	/* will have a 1 for each bit set to gnd */
	bitmask = PNL_PINPUT(r_str(pprt)) & scan_mask_i;
	/* disable all matrix signals */
	w_dtr(pprt, oldval);

	/* now that all outputs are cleared, the only active input bits are
	 * directly connected to the ground
	 */

	/* 1 for each grounded input */
	gndmask = PNL_PINPUT(r_str(pprt)) & scan_mask_i;

	/* grounded inputs are signals 40-44 */
	phys_read |= (__u64)gndmask << 40;

	if (bitmask != gndmask) {
		/*
		 * since clearing the outputs changed some inputs, we know
		 * that some input signals are currently tied to some outputs.
		 * So we'll scan them.
		 */
		for (bit = 0; bit < 8; bit++) {
			bitval = BIT(bit);

			if (!(scan_mask_o & bitval))	/* skip unused bits */
				continue;

			w_dtr(pprt, oldval & ~bitval);	/* enable this output */
			bitmask = PNL_PINPUT(r_str(pprt)) & ~gndmask;
			phys_read |= (__u64)bitmask << (5 * bit);
		}
		w_dtr(pprt, oldval);	/* disable all outputs */
	}
	/*
	 * this is easy: use old bits when they are flapping,
	 * use new ones when stable
	 */
	phys_curr = (phys_prev & (phys_read ^ phys_read_prev)) |
		    (phys_read & ~(phys_read ^ phys_read_prev));
}

static inline int input_state_high(struct logical_input *input)
{
#if 0
	/* FIXME:
	 * this is an invalid test. It tries to catch
	 * transitions from single-key to multiple-key, but
	 * doesn't take into account the contacts polarity.
	 * The only solution to the problem is to parse keys
	 * from the most complex to the simplest combinations,
	 * and mark them as 'caught' once a combination
	 * matches, then unmatch it for all other ones.
	 */

	/* try to catch dangerous transitions cases :
	 * someone adds a bit, so this signal was a false
	 * positive resulting from a transition. We should
	 * invalidate the signal immediately and not call the
	 * release function.
	 * eg: 0 -(press A)-> A -(press B)-> AB : don't match A's release.
	 */
	if (((phys_prev & input->mask) == input->value) &&
	    ((phys_curr & input->mask) >  input->value)) {
		input->state = INPUT_ST_LOW; /* invalidate */
		return 1;
	}
#endif

	if ((phys_curr & input->mask) == input->value) {
		if ((input->type == INPUT_TYPE_STD) &&
		    (input->high_timer == 0)) {
			input->high_timer++;
			if (input->u.std.press_fct)
				input->u.std.press_fct(input->u.std.press_data);
		} else if (input->type == INPUT_TYPE_KBD) {
			/* will turn on the light */
			keypressed = 1;

			if (input->high_timer == 0) {
				char *press_str = input->u.kbd.press_str;

				if (press_str[0]) {
					int s = sizeof(input->u.kbd.press_str);

					keypad_send_key(press_str, s);
				}
			}

			if (input->u.kbd.repeat_str[0]) {
				char *repeat_str = input->u.kbd.repeat_str;

				if (input->high_timer >= KEYPAD_REP_START) {
					int s = sizeof(input->u.kbd.repeat_str);

					input->high_timer -= KEYPAD_REP_DELAY;
					keypad_send_key(repeat_str, s);
				}
				/* we will need to come back here soon */
				inputs_stable = 0;
			}

			if (input->high_timer < 255)
				input->high_timer++;
		}
		return 1;
	}

	/* else signal falling down. Let's fall through. */
	input->state = INPUT_ST_FALLING;
	input->fall_timer = 0;

	return 0;
}

static inline void input_state_falling(struct logical_input *input)
{
#if 0
	/* FIXME !!! same comment as in input_state_high */
	if (((phys_prev & input->mask) == input->value) &&
	    ((phys_curr & input->mask) >  input->value)) {
		input->state = INPUT_ST_LOW;	/* invalidate */
		return;
	}
#endif

	if ((phys_curr & input->mask) == input->value) {
		if (input->type == INPUT_TYPE_KBD) {
			/* will turn on the light */
			keypressed = 1;

			if (input->u.kbd.repeat_str[0]) {
				char *repeat_str = input->u.kbd.repeat_str;

				if (input->high_timer >= KEYPAD_REP_START) {
					int s = sizeof(input->u.kbd.repeat_str);

					input->high_timer -= KEYPAD_REP_DELAY;
					keypad_send_key(repeat_str, s);
				}
				/* we will need to come back here soon */
				inputs_stable = 0;
			}

			if (input->high_timer < 255)
				input->high_timer++;
		}
		input->state = INPUT_ST_HIGH;
	} else if (input->fall_timer >= input->fall_time) {
		/* call release event */
		if (input->type == INPUT_TYPE_STD) {
			void (*release_fct)(int) = input->u.std.release_fct;

			if (release_fct)
				release_fct(input->u.std.release_data);
		} else if (input->type == INPUT_TYPE_KBD) {
			char *release_str = input->u.kbd.release_str;

			if (release_str[0]) {
				int s = sizeof(input->u.kbd.release_str);

				keypad_send_key(release_str, s);
			}
		}

		input->state = INPUT_ST_LOW;
	} else {
		input->fall_timer++;
		inputs_stable = 0;
	}
}

static void panel_process_inputs(void)
{
	struct logical_input *input;

	keypressed = 0;
	inputs_stable = 1;
	list_for_each_entry(input, &logical_inputs, list) {
		switch (input->state) {
		case INPUT_ST_LOW:
			if ((phys_curr & input->mask) != input->value)
				break;
			/* if all needed ones were already set previously,
			 * this means that this logical signal has been
			 * activated by the releasing of another combined
			 * signal, so we don't want to match.
			 * eg: AB -(release B)-> A -(release A)-> 0 :
			 *     don't match A.
			 */
			if ((phys_prev & input->mask) == input->value)
				break;
			input->rise_timer = 0;
			input->state = INPUT_ST_RISING;
			/* fall through */
		case INPUT_ST_RISING:
			if ((phys_curr & input->mask) != input->value) {
				input->state = INPUT_ST_LOW;
				break;
			}
			if (input->rise_timer < input->rise_time) {
				inputs_stable = 0;
				input->rise_timer++;
				break;
			}
			input->high_timer = 0;
			input->state = INPUT_ST_HIGH;
			/* fall through */
		case INPUT_ST_HIGH:
			if (input_state_high(input))
				break;
			/* fall through */
		case INPUT_ST_FALLING:
			input_state_falling(input);
		}
	}
}

static void panel_scan_timer(struct timer_list *unused)
{
	if (keypad.enabled && keypad_initialized) {
		if (spin_trylock_irq(&pprt_lock)) {
			phys_scan_contacts();

			/* no need for the parport anymore */
			spin_unlock_irq(&pprt_lock);
		}

		if (!inputs_stable || phys_curr != phys_prev)
			panel_process_inputs();
	}

	if (keypressed && lcd.enabled && lcd.initialized)
		charlcd_poke(lcd.charlcd);

	mod_timer(&scan_timer, jiffies + INPUT_POLL_TIME);
}

static void init_scan_timer(void)
{
	if (scan_timer.function)
		return;		/* already started */

	timer_setup(&scan_timer, panel_scan_timer, 0);
	scan_timer.expires = jiffies + INPUT_POLL_TIME;
	add_timer(&scan_timer);
}

/* converts a name of the form "({BbAaPpSsEe}{01234567-})*" to a series of bits.
 * if <omask> or <imask> are non-null, they will be or'ed with the bits
 * corresponding to out and in bits respectively.
 * returns 1 if ok, 0 if error (in which case, nothing is written).
 */
static u8 input_name2mask(const char *name, __u64 *mask, __u64 *value,
			  u8 *imask, u8 *omask)
{
	const char sigtab[] = "EeSsPpAaBb";
	u8 im, om;
	__u64 m, v;

	om = 0;
	im = 0;
	m = 0ULL;
	v = 0ULL;
	while (*name) {
		int in, out, bit, neg;
		const char *idx;

		idx = strchr(sigtab, *name);
		if (!idx)
			return 0;	/* input name not found */

		in = idx - sigtab;
		neg = (in & 1);	/* odd (lower) names are negated */
		in >>= 1;
		im |= BIT(in);

		name++;
		if (*name >= '0' && *name <= '7') {
			out = *name - '0';
			om |= BIT(out);
		} else if (*name == '-') {
			out = 8;
		} else {
			return 0;	/* unknown bit name */
		}

		bit = (out * 5) + in;

		m |= 1ULL << bit;
		if (!neg)
			v |= 1ULL << bit;
		name++;
	}
	*mask = m;
	*value = v;
	if (imask)
		*imask |= im;
	if (omask)
		*omask |= om;
	return 1;
}

/* tries to bind a key to the signal name <name>. The key will send the
 * strings <press>, <repeat>, <release> for these respective events.
 * Returns the pointer to the new key if ok, NULL if the key could not be bound.
 */
static struct logical_input *panel_bind_key(const char *name, const char *press,
					    const char *repeat,
					    const char *release)
{
	struct logical_input *key;

	key = kzalloc(sizeof(*key), GFP_KERNEL);
	if (!key)
		return NULL;

	if (!input_name2mask(name, &key->mask, &key->value, &scan_mask_i,
			     &scan_mask_o)) {
		kfree(key);
		return NULL;
	}

	key->type = INPUT_TYPE_KBD;
	key->state = INPUT_ST_LOW;
	key->rise_time = 1;
	key->fall_time = 1;

	strncpy(key->u.kbd.press_str, press, sizeof(key->u.kbd.press_str));
	strncpy(key->u.kbd.repeat_str, repeat, sizeof(key->u.kbd.repeat_str));
	strncpy(key->u.kbd.release_str, release,
		sizeof(key->u.kbd.release_str));
	list_add(&key->list, &logical_inputs);
	return key;
}

#if 0
/* tries to bind a callback function to the signal name <name>. The function
 * <press_fct> will be called with the <press_data> arg when the signal is
 * activated, and so on for <release_fct>/<release_data>
 * Returns the pointer to the new signal if ok, NULL if the signal could not
 * be bound.
 */
static struct logical_input *panel_bind_callback(char *name,
						 void (*press_fct)(int),
						 int press_data,
						 void (*release_fct)(int),
						 int release_data)
{
	struct logical_input *callback;

	callback = kmalloc(sizeof(*callback), GFP_KERNEL);
	if (!callback)
		return NULL;

	memset(callback, 0, sizeof(struct logical_input));
	if (!input_name2mask(name, &callback->mask, &callback->value,
			     &scan_mask_i, &scan_mask_o))
		return NULL;

	callback->type = INPUT_TYPE_STD;
	callback->state = INPUT_ST_LOW;
	callback->rise_time = 1;
	callback->fall_time = 1;
	callback->u.std.press_fct = press_fct;
	callback->u.std.press_data = press_data;
	callback->u.std.release_fct = release_fct;
	callback->u.std.release_data = release_data;
	list_add(&callback->list, &logical_inputs);
	return callback;
}
#endif

static void keypad_init(void)
{
	int keynum;

	init_waitqueue_head(&keypad_read_wait);
	keypad_buflen = 0;	/* flushes any eventual noisy keystroke */

	/* Let's create all known keys */

	for (keynum = 0; keypad_profile[keynum][0][0]; keynum++) {
		panel_bind_key(keypad_profile[keynum][0],
			       keypad_profile[keynum][1],
			       keypad_profile[keynum][2],
			       keypad_profile[keynum][3]);
	}

	init_scan_timer();
	keypad_initialized = 1;
}

/**************************************************/
/* device initialization                          */
/**************************************************/

static void panel_attach(struct parport *port)
{
	struct pardev_cb panel_cb;

	if (port->number != parport)
		return;

	if (pprt) {
		pr_err("%s: port->number=%d parport=%d, already registered!\n",
		       __func__, port->number, parport);
		return;
	}

	memset(&panel_cb, 0, sizeof(panel_cb));
	panel_cb.private = &pprt;
	/* panel_cb.flags = 0 should be PARPORT_DEV_EXCL? */

	pprt = parport_register_dev_model(port, "panel", &panel_cb, 0);
	if (!pprt) {
		pr_err("%s: port->number=%d parport=%d, parport_register_device() failed\n",
		       __func__, port->number, parport);
		return;
	}

	if (parport_claim(pprt)) {
		pr_err("could not claim access to parport%d. Aborting.\n",
		       parport);
		goto err_unreg_device;
	}

	/* must init LCD first, just in case an IRQ from the keypad is
	 * generated at keypad init
	 */
	if (lcd.enabled) {
		lcd_init();
		if (!lcd.charlcd || charlcd_register(lcd.charlcd))
			goto err_unreg_device;
	}

	if (keypad.enabled) {
		keypad_init();
		if (misc_register(&keypad_dev))
			goto err_lcd_unreg;
	}
	return;

err_lcd_unreg:
	if (scan_timer.function)
		del_timer_sync(&scan_timer);
	if (lcd.enabled)
		charlcd_unregister(lcd.charlcd);
err_unreg_device:
	kfree(lcd.charlcd);
	lcd.charlcd = NULL;
	parport_unregister_device(pprt);
	pprt = NULL;
}

static void panel_detach(struct parport *port)
{
	if (port->number != parport)
		return;

	if (!pprt) {
		pr_err("%s: port->number=%d parport=%d, nothing to unregister.\n",
		       __func__, port->number, parport);
		return;
	}
	if (scan_timer.function)
		del_timer_sync(&scan_timer);

	if (keypad.enabled) {
		misc_deregister(&keypad_dev);
		keypad_initialized = 0;
	}

	if (lcd.enabled) {
		charlcd_unregister(lcd.charlcd);
		lcd.initialized = false;
		kfree(lcd.charlcd);
		lcd.charlcd = NULL;
	}

	/* TODO: free all input signals */
	parport_release(pprt);
	parport_unregister_device(pprt);
	pprt = NULL;
}

static struct parport_driver panel_driver = {
	.name = "panel",
	.match_port = panel_attach,
	.detach = panel_detach,
	.devmodel = true,
};

/* init function */
static int __init panel_init_module(void)
{
	int selected_keypad_type = NOT_SET, err;

	/* take care of an eventual profile */
	switch (profile) {
	case PANEL_PROFILE_CUSTOM:
		/* custom profile */
		selected_keypad_type = DEFAULT_KEYPAD_TYPE;
		selected_lcd_type = DEFAULT_LCD_TYPE;
		break;
	case PANEL_PROFILE_OLD:
		/* 8 bits, 2*16, old keypad */
		selected_keypad_type = KEYPAD_TYPE_OLD;
		selected_lcd_type = LCD_TYPE_OLD;

		/* TODO: This two are a little hacky, sort it out later */
		if (lcd_width == NOT_SET)
			lcd_width = 16;
		if (lcd_hwidth == NOT_SET)
			lcd_hwidth = 16;
		break;
	case PANEL_PROFILE_NEW:
		/* serial, 2*16, new keypad */
		selected_keypad_type = KEYPAD_TYPE_NEW;
		selected_lcd_type = LCD_TYPE_KS0074;
		break;
	case PANEL_PROFILE_HANTRONIX:
		/* 8 bits, 2*16 hantronix-like, no keypad */
		selected_keypad_type = KEYPAD_TYPE_NONE;
		selected_lcd_type = LCD_TYPE_HANTRONIX;
		break;
	case PANEL_PROFILE_NEXCOM:
		/* generic 8 bits, 2*16, nexcom keypad, eg. Nexcom. */
		selected_keypad_type = KEYPAD_TYPE_NEXCOM;
		selected_lcd_type = LCD_TYPE_NEXCOM;
		break;
	case PANEL_PROFILE_LARGE:
		/* 8 bits, 2*40, old keypad */
		selected_keypad_type = KEYPAD_TYPE_OLD;
		selected_lcd_type = LCD_TYPE_OLD;
		break;
	}

	/*
	 * Overwrite selection with module param values (both keypad and lcd),
	 * where the deprecated params have lower prio.
	 */
	if (keypad_enabled != NOT_SET)
		selected_keypad_type = keypad_enabled;
	if (keypad_type != NOT_SET)
		selected_keypad_type = keypad_type;

	keypad.enabled = (selected_keypad_type > 0);

	if (lcd_enabled != NOT_SET)
		selected_lcd_type = lcd_enabled;
	if (lcd_type != NOT_SET)
		selected_lcd_type = lcd_type;

	lcd.enabled = (selected_lcd_type > 0);

	if (lcd.enabled) {
		/*
		 * Init lcd struct with load-time values to preserve exact
		 * current functionality (at least for now).
		 */
		lcd.charset = lcd_charset;
		lcd.proto = lcd_proto;
		lcd.pins.e = lcd_e_pin;
		lcd.pins.rs = lcd_rs_pin;
		lcd.pins.rw = lcd_rw_pin;
		lcd.pins.cl = lcd_cl_pin;
		lcd.pins.da = lcd_da_pin;
		lcd.pins.bl = lcd_bl_pin;
	}

	switch (selected_keypad_type) {
	case KEYPAD_TYPE_OLD:
		keypad_profile = old_keypad_profile;
		break;
	case KEYPAD_TYPE_NEW:
		keypad_profile = new_keypad_profile;
		break;
	case KEYPAD_TYPE_NEXCOM:
		keypad_profile = nexcom_keypad_profile;
		break;
	default:
		keypad_profile = NULL;
		break;
	}

	if (!lcd.enabled && !keypad.enabled) {
		/* no device enabled, let's exit */
		pr_err("panel driver disabled.\n");
		return -ENODEV;
	}

	err = parport_register_driver(&panel_driver);
	if (err) {
		pr_err("could not register with parport. Aborting.\n");
		return err;
	}

	if (pprt)
		pr_info("panel driver registered on parport%d (io=0x%lx).\n",
			parport, pprt->port->base);
	else
		pr_info("panel driver not yet registered\n");
	return 0;
}

static void __exit panel_cleanup_module(void)
{
	parport_unregister_driver(&panel_driver);
}

module_init(panel_init_module);
module_exit(panel_cleanup_module);
MODULE_AUTHOR("Willy Tarreau");
MODULE_LICENSE("GPL");

/*
 * Local variables:
 *  c-indent-level: 4
 *  tab-width: 8
 * End:
 */
