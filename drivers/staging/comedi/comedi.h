/*
    include/comedi.h (installed as /usr/include/comedi.h)
    header file for comedi

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1998-2001 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef _COMEDI_H
#define _COMEDI_H

#define COMEDI_MAJORVERSION	0
#define COMEDI_MINORVERSION	7
#define COMEDI_MICROVERSION	76
#define VERSION	"0.7.76"

/* comedi's major device number */
#define COMEDI_MAJOR 98

/*
   maximum number of minor devices.  This can be increased, although
   kernel structures are currently statically allocated, thus you
   don't want this to be much more than you actually use.
 */
#define COMEDI_NDEVICES 16

/* number of config options in the config structure */
#define COMEDI_NDEVCONFOPTS 32
/*length of nth chunk of firmware data*/
#define COMEDI_DEVCONF_AUX_DATA3_LENGTH		25
#define COMEDI_DEVCONF_AUX_DATA2_LENGTH		26
#define COMEDI_DEVCONF_AUX_DATA1_LENGTH		27
#define COMEDI_DEVCONF_AUX_DATA0_LENGTH		28
/* most significant 32 bits of pointer address (if needed) */
#define COMEDI_DEVCONF_AUX_DATA_HI		29
/* least significant 32 bits of pointer address */
#define COMEDI_DEVCONF_AUX_DATA_LO		30
#define COMEDI_DEVCONF_AUX_DATA_LENGTH		31	/* total data length */

/* max length of device and driver names */
#define COMEDI_NAMELEN 20

/* packs and unpacks a channel/range number */

#define CR_PACK(chan, rng, aref)					\
	((((aref)&0x3)<<24) | (((rng)&0xff)<<16) | (chan))
#define CR_PACK_FLAGS(chan, range, aref, flags)				\
	(CR_PACK(chan, range, aref) | ((flags) & CR_FLAGS_MASK))

#define CR_CHAN(a)	((a)&0xffff)
#define CR_RANGE(a)	(((a)>>16)&0xff)
#define CR_AREF(a)	(((a)>>24)&0x03)

#define CR_FLAGS_MASK	0xfc000000
#define CR_ALT_FILTER	(1<<26)
#define CR_DITHER	CR_ALT_FILTER
#define CR_DEGLITCH	CR_ALT_FILTER
#define CR_ALT_SOURCE	(1<<27)
#define CR_EDGE		(1<<30)
#define CR_INVERT	(1<<31)

#define AREF_GROUND	0x00	/* analog ref = analog ground */
#define AREF_COMMON	0x01	/* analog ref = analog common */
#define AREF_DIFF	0x02	/* analog ref = differential */
#define AREF_OTHER	0x03	/* analog ref = other (undefined) */

/* counters -- these are arbitrary values */
#define GPCT_RESET		0x0001
#define GPCT_SET_SOURCE		0x0002
#define GPCT_SET_GATE		0x0004
#define GPCT_SET_DIRECTION	0x0008
#define GPCT_SET_OPERATION	0x0010
#define GPCT_ARM		0x0020
#define GPCT_DISARM		0x0040
#define GPCT_GET_INT_CLK_FRQ	0x0080

#define GPCT_INT_CLOCK		0x0001
#define GPCT_EXT_PIN		0x0002
#define GPCT_NO_GATE		0x0004
#define GPCT_UP			0x0008
#define GPCT_DOWN		0x0010
#define GPCT_HWUD		0x0020
#define GPCT_SIMPLE_EVENT	0x0040
#define GPCT_SINGLE_PERIOD	0x0080
#define GPCT_SINGLE_PW		0x0100
#define GPCT_CONT_PULSE_OUT	0x0200
#define GPCT_SINGLE_PULSE_OUT	0x0400

/* instructions */

#define INSN_MASK_WRITE		0x8000000
#define INSN_MASK_READ		0x4000000
#define INSN_MASK_SPECIAL	0x2000000

#define INSN_READ		(0 | INSN_MASK_READ)
#define INSN_WRITE		(1 | INSN_MASK_WRITE)
#define INSN_BITS		(2 | INSN_MASK_READ|INSN_MASK_WRITE)
#define INSN_CONFIG		(3 | INSN_MASK_READ|INSN_MASK_WRITE)
#define INSN_GTOD		(4 | INSN_MASK_READ|INSN_MASK_SPECIAL)
#define INSN_WAIT		(5 | INSN_MASK_WRITE|INSN_MASK_SPECIAL)
#define INSN_INTTRIG		(6 | INSN_MASK_WRITE|INSN_MASK_SPECIAL)

/* trigger flags */
/* These flags are used in comedi_trig structures */

#define TRIG_BOGUS	0x0001	/* do the motions */
#define TRIG_DITHER	0x0002	/* enable dithering */
#define TRIG_DEGLITCH	0x0004	/* enable deglitching */
	/*#define TRIG_RT       0x0008 *//* perform op in real time */
#define TRIG_CONFIG	0x0010	/* perform configuration, not triggering */
#define TRIG_WAKE_EOS	0x0020	/* wake up on end-of-scan events */
	/*#define TRIG_WRITE    0x0040*//* write to bidirectional devices */

/* command flags */
/* These flags are used in comedi_cmd structures */

/* try to use a real-time interrupt while performing command */
#define CMDF_PRIORITY		0x00000008

#define TRIG_RT		CMDF_PRIORITY	/* compatibility definition */

#define CMDF_WRITE		0x00000040
#define TRIG_WRITE	CMDF_WRITE	/* compatibility definition */

#define CMDF_RAWDATA		0x00000080

#define COMEDI_EV_START		0x00040000
#define COMEDI_EV_SCAN_BEGIN	0x00080000
#define COMEDI_EV_CONVERT	0x00100000
#define COMEDI_EV_SCAN_END	0x00200000
#define COMEDI_EV_STOP		0x00400000

#define TRIG_ROUND_MASK		0x00030000
#define TRIG_ROUND_NEAREST	0x00000000
#define TRIG_ROUND_DOWN		0x00010000
#define TRIG_ROUND_UP		0x00020000
#define TRIG_ROUND_UP_NEXT	0x00030000

/* trigger sources */

#define TRIG_ANY	0xffffffff
#define TRIG_INVALID	0x00000000

#define TRIG_NONE	0x00000001 /* never trigger */
#define TRIG_NOW	0x00000002 /* trigger now + N ns */
#define TRIG_FOLLOW	0x00000004 /* trigger on next lower level trig */
#define TRIG_TIME	0x00000008 /* trigger at time N ns */
#define TRIG_TIMER	0x00000010 /* trigger at rate N ns */
#define TRIG_COUNT	0x00000020 /* trigger when count reaches N */
#define TRIG_EXT	0x00000040 /* trigger on external signal N */
#define TRIG_INT	0x00000080 /* trigger on comedi-internal signal N */
#define TRIG_OTHER	0x00000100 /* driver defined */

/* subdevice flags */

#define SDF_BUSY	0x0001	/* device is busy */
#define SDF_BUSY_OWNER	0x0002	/* device is busy with your job */
#define SDF_LOCKED	0x0004	/* subdevice is locked */
#define SDF_LOCK_OWNER	0x0008	/* you own lock */
#define SDF_MAXDATA	0x0010	/* maxdata depends on channel */
#define SDF_FLAGS	0x0020	/* flags depend on channel */
#define SDF_RANGETYPE	0x0040	/* range type depends on channel */
#define SDF_MODE0	0x0080	/* can do mode 0 */
#define SDF_MODE1	0x0100	/* can do mode 1 */
#define SDF_MODE2	0x0200	/* can do mode 2 */
#define SDF_MODE3	0x0400	/* can do mode 3 */
#define SDF_MODE4	0x0800	/* can do mode 4 */
#define SDF_CMD		0x1000	/* can do commands (deprecated) */
#define SDF_SOFT_CALIBRATED	0x2000 /* subdevice uses software calibration */
#define SDF_CMD_WRITE		0x4000 /* can do output commands */
#define SDF_CMD_READ		0x8000 /* can do input commands */

/* subdevice can be read (e.g. analog input) */
#define SDF_READABLE	0x00010000
/* subdevice can be written (e.g. analog output) */
#define SDF_WRITABLE	0x00020000
#define SDF_WRITEABLE	SDF_WRITABLE	/* spelling error in API */
/* subdevice does not have externally visible lines */
#define SDF_INTERNAL	0x00040000
#define SDF_GROUND	0x00100000	/* can do aref=ground */
#define SDF_COMMON	0x00200000	/* can do aref=common */
#define SDF_DIFF	0x00400000	/* can do aref=diff */
#define SDF_OTHER	0x00800000	/* can do aref=other */
#define SDF_DITHER	0x01000000	/* can do dithering */
#define SDF_DEGLITCH	0x02000000	/* can do deglitching */
#define SDF_MMAP	0x04000000	/* can do mmap() */
#define SDF_RUNNING	0x08000000	/* subdevice is acquiring data */
#define SDF_LSAMPL	0x10000000	/* subdevice uses 32-bit samples */
#define SDF_PACKED	0x20000000	/* subdevice can do packed DIO */
/* re recyle these flags for PWM */
#define SDF_PWM_COUNTER SDF_MODE0	/* PWM can automatically switch off */
#define SDF_PWM_HBRIDGE SDF_MODE1	/* PWM is signed (H-bridge) */

/* subdevice types */

enum comedi_subdevice_type {
	COMEDI_SUBD_UNUSED,	/* unused by driver */
	COMEDI_SUBD_AI,		/* analog input */
	COMEDI_SUBD_AO,		/* analog output */
	COMEDI_SUBD_DI,		/* digital input */
	COMEDI_SUBD_DO,		/* digital output */
	COMEDI_SUBD_DIO,	/* digital input/output */
	COMEDI_SUBD_COUNTER,	/* counter */
	COMEDI_SUBD_TIMER,	/* timer */
	COMEDI_SUBD_MEMORY,	/* memory, EEPROM, DPRAM */
	COMEDI_SUBD_CALIB,	/* calibration DACs */
	COMEDI_SUBD_PROC,	/* processor, DSP */
	COMEDI_SUBD_SERIAL,	/* serial IO */
	COMEDI_SUBD_PWM		/* PWM */
};

/* configuration instructions */

enum configuration_ids {
	INSN_CONFIG_DIO_INPUT = 0,
	INSN_CONFIG_DIO_OUTPUT = 1,
	INSN_CONFIG_DIO_OPENDRAIN = 2,
	INSN_CONFIG_ANALOG_TRIG = 16,
/*	INSN_CONFIG_WAVEFORM = 17, */
/*	INSN_CONFIG_TRIG = 18, */
/*	INSN_CONFIG_COUNTER = 19, */
	INSN_CONFIG_ALT_SOURCE = 20,
	INSN_CONFIG_DIGITAL_TRIG = 21,
	INSN_CONFIG_BLOCK_SIZE = 22,
	INSN_CONFIG_TIMER_1 = 23,
	INSN_CONFIG_FILTER = 24,
	INSN_CONFIG_CHANGE_NOTIFY = 25,

	INSN_CONFIG_SERIAL_CLOCK = 26,	/*ALPHA*/
	INSN_CONFIG_BIDIRECTIONAL_DATA = 27,
	INSN_CONFIG_DIO_QUERY = 28,
	INSN_CONFIG_PWM_OUTPUT = 29,
	INSN_CONFIG_GET_PWM_OUTPUT = 30,
	INSN_CONFIG_ARM = 31,
	INSN_CONFIG_DISARM = 32,
	INSN_CONFIG_GET_COUNTER_STATUS = 33,
	INSN_CONFIG_RESET = 34,
	/* Use CTR as single pulsegenerator */
	INSN_CONFIG_GPCT_SINGLE_PULSE_GENERATOR = 1001,
	/* Use CTR as pulsetraingenerator */
	INSN_CONFIG_GPCT_PULSE_TRAIN_GENERATOR = 1002,
	/* Use the counter as encoder */
	INSN_CONFIG_GPCT_QUADRATURE_ENCODER = 1003,
	INSN_CONFIG_SET_GATE_SRC = 2001,	/* Set gate source */
	INSN_CONFIG_GET_GATE_SRC = 2002,	/* Get gate source */
	/* Set master clock source */
	INSN_CONFIG_SET_CLOCK_SRC = 2003,
	INSN_CONFIG_GET_CLOCK_SRC = 2004, /* Get master clock source */
	INSN_CONFIG_SET_OTHER_SRC = 2005, /* Set other source */
	/* INSN_CONFIG_GET_OTHER_SRC = 2006,*//* Get other source */
	/* Get size in bytes of subdevice's on-board fifos used during
	 * streaming input/output */
	INSN_CONFIG_GET_HARDWARE_BUFFER_SIZE = 2006,
	INSN_CONFIG_SET_COUNTER_MODE = 4097,
	/* INSN_CONFIG_8254_SET_MODE is deprecated */
	INSN_CONFIG_8254_SET_MODE = INSN_CONFIG_SET_COUNTER_MODE,
	INSN_CONFIG_8254_READ_STATUS = 4098,
	INSN_CONFIG_SET_ROUTING = 4099,
	INSN_CONFIG_GET_ROUTING = 4109,
	/* PWM */
	INSN_CONFIG_PWM_SET_PERIOD = 5000,	/* sets frequency */
	INSN_CONFIG_PWM_GET_PERIOD = 5001,	/* gets frequency */
	INSN_CONFIG_GET_PWM_STATUS = 5002,	/* is it running? */
	/* sets H bridge: duty cycle and sign bit for a relay at the
	 * same time */
	INSN_CONFIG_PWM_SET_H_BRIDGE = 5003,
	/* gets H bridge data: duty cycle and the sign bit */
	INSN_CONFIG_PWM_GET_H_BRIDGE = 5004
};

enum comedi_io_direction {
	COMEDI_INPUT = 0,
	COMEDI_OUTPUT = 1,
	COMEDI_OPENDRAIN = 2
};

enum comedi_support_level {
	COMEDI_UNKNOWN_SUPPORT = 0,
	COMEDI_SUPPORTED,
	COMEDI_UNSUPPORTED
};

/* ioctls */

#define CIO 'd'
#define COMEDI_DEVCONFIG _IOW(CIO, 0, struct comedi_devconfig)
#define COMEDI_DEVINFO _IOR(CIO, 1, struct comedi_devinfo)
#define COMEDI_SUBDINFO _IOR(CIO, 2, struct comedi_subdinfo)
#define COMEDI_CHANINFO _IOR(CIO, 3, struct comedi_chaninfo)
#define COMEDI_TRIG _IOWR(CIO, 4, comedi_trig)
#define COMEDI_LOCK _IO(CIO, 5)
#define COMEDI_UNLOCK _IO(CIO, 6)
#define COMEDI_CANCEL _IO(CIO, 7)
#define COMEDI_RANGEINFO _IOR(CIO, 8, struct comedi_rangeinfo)
#define COMEDI_CMD _IOR(CIO, 9, struct comedi_cmd)
#define COMEDI_CMDTEST _IOR(CIO, 10, struct comedi_cmd)
#define COMEDI_INSNLIST _IOR(CIO, 11, struct comedi_insnlist)
#define COMEDI_INSN _IOR(CIO, 12, struct comedi_insn)
#define COMEDI_BUFCONFIG _IOR(CIO, 13, struct comedi_bufconfig)
#define COMEDI_BUFINFO _IOWR(CIO, 14, struct comedi_bufinfo)
#define COMEDI_POLL _IO(CIO, 15)

/* structures */

struct comedi_trig {
	unsigned int subdev;	/* subdevice */
	unsigned int mode;	/* mode */
	unsigned int flags;
	unsigned int n_chan;	/* number of channels */
	unsigned int *chanlist;	/* channel/range list */
	short *data;		/* data list, size depends on subd flags */
	unsigned int n;		/* number of scans */
	unsigned int trigsrc;
	unsigned int trigvar;
	unsigned int trigvar1;
	unsigned int data_len;
	unsigned int unused[3];
};

struct comedi_insn {
	unsigned int insn;
	unsigned int n;
	unsigned int __user *data;
	unsigned int subdev;
	unsigned int chanspec;
	unsigned int unused[3];
};

struct comedi_insnlist {
	unsigned int n_insns;
	struct comedi_insn __user *insns;
};

struct comedi_cmd {
	unsigned int subdev;
	unsigned int flags;

	unsigned int start_src;
	unsigned int start_arg;

	unsigned int scan_begin_src;
	unsigned int scan_begin_arg;

	unsigned int convert_src;
	unsigned int convert_arg;

	unsigned int scan_end_src;
	unsigned int scan_end_arg;

	unsigned int stop_src;
	unsigned int stop_arg;

	unsigned int *chanlist;	/* channel/range list */
	unsigned int chanlist_len;

	short __user *data; /* data list, size depends on subd flags */
	unsigned int data_len;
};

struct comedi_chaninfo {
	unsigned int subdev;
	unsigned int __user *maxdata_list;
	unsigned int __user *flaglist;
	unsigned int __user *rangelist;
	unsigned int unused[4];
};

struct comedi_rangeinfo {
	unsigned int range_type;
	void __user *range_ptr;
};

struct comedi_krange {
	int min;	/* fixed point, multiply by 1e-6 */
	int max;	/* fixed point, multiply by 1e-6 */
	unsigned int flags;
};

struct comedi_subdinfo {
	unsigned int type;
	unsigned int n_chan;
	unsigned int subd_flags;
	unsigned int timer_type;
	unsigned int len_chanlist;
	unsigned int maxdata;
	unsigned int flags;		/* channel flags */
	unsigned int range_type;	/* lookup in kernel */
	unsigned int settling_time_0;
	/* see support_level enum for values */
	unsigned insn_bits_support;
	unsigned int unused[8];
};

struct comedi_devinfo {
	unsigned int version_code;
	unsigned int n_subdevs;
	char driver_name[COMEDI_NAMELEN];
	char board_name[COMEDI_NAMELEN];
	int read_subdevice;
	int write_subdevice;
	int unused[30];
};

struct comedi_devconfig {
	char board_name[COMEDI_NAMELEN];
	int options[COMEDI_NDEVCONFOPTS];
};

struct comedi_bufconfig {
	unsigned int subdevice;
	unsigned int flags;

	unsigned int maximum_size;
	unsigned int size;

	unsigned int unused[4];
};

struct comedi_bufinfo {
	unsigned int subdevice;
	unsigned int bytes_read;

	unsigned int buf_write_ptr;
	unsigned int buf_read_ptr;
	unsigned int buf_write_count;
	unsigned int buf_read_count;

	unsigned int bytes_written;

	unsigned int unused[4];
};

/* range stuff */

#define __RANGE(a, b)	((((a)&0xffff)<<16)|((b)&0xffff))

#define RANGE_OFFSET(a)		(((a)>>16)&0xffff)
#define RANGE_LENGTH(b)		((b)&0xffff)

#define RF_UNIT(flags)		((flags)&0xff)
#define RF_EXTERNAL		(1<<8)

#define UNIT_volt		0
#define UNIT_mA			1
#define UNIT_none		2

#define COMEDI_MIN_SPEED	((unsigned int)0xffffffff)

/* callback stuff */
/* only relevant to kernel modules. */

#define COMEDI_CB_EOS		1	/* end of scan */
#define COMEDI_CB_EOA		2	/* end of acquisition/output */
#define COMEDI_CB_BLOCK		4	/* data has arrived:
					 * wakes up read() / write() */
#define COMEDI_CB_EOBUF		8	/* DEPRECATED: end of buffer */
#define COMEDI_CB_ERROR		16	/* card error during acquisition */
#define COMEDI_CB_OVERFLOW	32	/* buffer overflow/underflow */

/**********************************************************/
/* everything after this line is ALPHA */
/**********************************************************/

/*
  8254 specific configuration.

  It supports two config commands:

  0 ID: INSN_CONFIG_SET_COUNTER_MODE
  1 8254 Mode
    I8254_MODE0, I8254_MODE1, ..., I8254_MODE5
    OR'ed with:
    I8254_BCD, I8254_BINARY

  0 ID: INSN_CONFIG_8254_READ_STATUS
  1 <-- Status byte returned here.
    B7 = Output
    B6 = NULL Count
    B5 - B0 Current mode.

*/

enum i8254_mode {
	I8254_MODE0 = (0 << 1),	/* Interrupt on terminal count */
	I8254_MODE1 = (1 << 1),	/* Hardware retriggerable one-shot */
	I8254_MODE2 = (2 << 1),	/* Rate generator */
	I8254_MODE3 = (3 << 1),	/* Square wave mode */
	I8254_MODE4 = (4 << 1),	/* Software triggered strobe */
	I8254_MODE5 = (5 << 1),	/* Hardware triggered strobe
				 * (retriggerable) */
	I8254_BCD = 1,		/* use binary-coded decimal instead of binary
				 * (pretty useless) */
	I8254_BINARY = 0
};

static inline unsigned NI_USUAL_PFI_SELECT(unsigned pfi_channel)
{
	if (pfi_channel < 10)
		return 0x1 + pfi_channel;
	else
		return 0xb + pfi_channel;
}

static inline unsigned NI_USUAL_RTSI_SELECT(unsigned rtsi_channel)
{
	if (rtsi_channel < 7)
		return 0xb + rtsi_channel;
	else
		return 0x1b;
}

/* mode bits for NI general-purpose counters, set with
 * INSN_CONFIG_SET_COUNTER_MODE */
#define NI_GPCT_COUNTING_MODE_SHIFT 16
#define NI_GPCT_INDEX_PHASE_BITSHIFT 20
#define NI_GPCT_COUNTING_DIRECTION_SHIFT 24
enum ni_gpct_mode_bits {
	NI_GPCT_GATE_ON_BOTH_EDGES_BIT = 0x4,
	NI_GPCT_EDGE_GATE_MODE_MASK = 0x18,
	NI_GPCT_EDGE_GATE_STARTS_STOPS_BITS = 0x0,
	NI_GPCT_EDGE_GATE_STOPS_STARTS_BITS = 0x8,
	NI_GPCT_EDGE_GATE_STARTS_BITS = 0x10,
	NI_GPCT_EDGE_GATE_NO_STARTS_NO_STOPS_BITS = 0x18,
	NI_GPCT_STOP_MODE_MASK = 0x60,
	NI_GPCT_STOP_ON_GATE_BITS = 0x00,
	NI_GPCT_STOP_ON_GATE_OR_TC_BITS = 0x20,
	NI_GPCT_STOP_ON_GATE_OR_SECOND_TC_BITS = 0x40,
	NI_GPCT_LOAD_B_SELECT_BIT = 0x80,
	NI_GPCT_OUTPUT_MODE_MASK = 0x300,
	NI_GPCT_OUTPUT_TC_PULSE_BITS = 0x100,
	NI_GPCT_OUTPUT_TC_TOGGLE_BITS = 0x200,
	NI_GPCT_OUTPUT_TC_OR_GATE_TOGGLE_BITS = 0x300,
	NI_GPCT_HARDWARE_DISARM_MASK = 0xc00,
	NI_GPCT_NO_HARDWARE_DISARM_BITS = 0x000,
	NI_GPCT_DISARM_AT_TC_BITS = 0x400,
	NI_GPCT_DISARM_AT_GATE_BITS = 0x800,
	NI_GPCT_DISARM_AT_TC_OR_GATE_BITS = 0xc00,
	NI_GPCT_LOADING_ON_TC_BIT = 0x1000,
	NI_GPCT_LOADING_ON_GATE_BIT = 0x4000,
	NI_GPCT_COUNTING_MODE_MASK = 0x7 << NI_GPCT_COUNTING_MODE_SHIFT,
	NI_GPCT_COUNTING_MODE_NORMAL_BITS =
		0x0 << NI_GPCT_COUNTING_MODE_SHIFT,
	NI_GPCT_COUNTING_MODE_QUADRATURE_X1_BITS =
		0x1 << NI_GPCT_COUNTING_MODE_SHIFT,
	NI_GPCT_COUNTING_MODE_QUADRATURE_X2_BITS =
		0x2 << NI_GPCT_COUNTING_MODE_SHIFT,
	NI_GPCT_COUNTING_MODE_QUADRATURE_X4_BITS =
		0x3 << NI_GPCT_COUNTING_MODE_SHIFT,
	NI_GPCT_COUNTING_MODE_TWO_PULSE_BITS =
		0x4 << NI_GPCT_COUNTING_MODE_SHIFT,
	NI_GPCT_COUNTING_MODE_SYNC_SOURCE_BITS =
		0x6 << NI_GPCT_COUNTING_MODE_SHIFT,
	NI_GPCT_INDEX_PHASE_MASK = 0x3 << NI_GPCT_INDEX_PHASE_BITSHIFT,
	NI_GPCT_INDEX_PHASE_LOW_A_LOW_B_BITS =
		0x0 << NI_GPCT_INDEX_PHASE_BITSHIFT,
	NI_GPCT_INDEX_PHASE_LOW_A_HIGH_B_BITS =
		0x1 << NI_GPCT_INDEX_PHASE_BITSHIFT,
	NI_GPCT_INDEX_PHASE_HIGH_A_LOW_B_BITS =
		0x2 << NI_GPCT_INDEX_PHASE_BITSHIFT,
	NI_GPCT_INDEX_PHASE_HIGH_A_HIGH_B_BITS =
		0x3 << NI_GPCT_INDEX_PHASE_BITSHIFT,
	NI_GPCT_INDEX_ENABLE_BIT = 0x400000,
	NI_GPCT_COUNTING_DIRECTION_MASK =
		0x3 << NI_GPCT_COUNTING_DIRECTION_SHIFT,
	NI_GPCT_COUNTING_DIRECTION_DOWN_BITS =
		0x00 << NI_GPCT_COUNTING_DIRECTION_SHIFT,
	NI_GPCT_COUNTING_DIRECTION_UP_BITS =
		0x1 << NI_GPCT_COUNTING_DIRECTION_SHIFT,
	NI_GPCT_COUNTING_DIRECTION_HW_UP_DOWN_BITS =
		0x2 << NI_GPCT_COUNTING_DIRECTION_SHIFT,
	NI_GPCT_COUNTING_DIRECTION_HW_GATE_BITS =
		0x3 << NI_GPCT_COUNTING_DIRECTION_SHIFT,
	NI_GPCT_RELOAD_SOURCE_MASK = 0xc000000,
	NI_GPCT_RELOAD_SOURCE_FIXED_BITS = 0x0,
	NI_GPCT_RELOAD_SOURCE_SWITCHING_BITS = 0x4000000,
	NI_GPCT_RELOAD_SOURCE_GATE_SELECT_BITS = 0x8000000,
	NI_GPCT_OR_GATE_BIT = 0x10000000,
	NI_GPCT_INVERT_OUTPUT_BIT = 0x20000000
};

/* Bits for setting a clock source with
 * INSN_CONFIG_SET_CLOCK_SRC when using NI general-purpose counters. */
enum ni_gpct_clock_source_bits {
	NI_GPCT_CLOCK_SRC_SELECT_MASK = 0x3f,
	NI_GPCT_TIMEBASE_1_CLOCK_SRC_BITS = 0x0,
	NI_GPCT_TIMEBASE_2_CLOCK_SRC_BITS = 0x1,
	NI_GPCT_TIMEBASE_3_CLOCK_SRC_BITS = 0x2,
	NI_GPCT_LOGIC_LOW_CLOCK_SRC_BITS = 0x3,
	NI_GPCT_NEXT_GATE_CLOCK_SRC_BITS = 0x4,
	NI_GPCT_NEXT_TC_CLOCK_SRC_BITS = 0x5,
	/* NI 660x-specific */
	NI_GPCT_SOURCE_PIN_i_CLOCK_SRC_BITS = 0x6,
	NI_GPCT_PXI10_CLOCK_SRC_BITS = 0x7,
	NI_GPCT_PXI_STAR_TRIGGER_CLOCK_SRC_BITS = 0x8,
	NI_GPCT_ANALOG_TRIGGER_OUT_CLOCK_SRC_BITS = 0x9,
	NI_GPCT_PRESCALE_MODE_CLOCK_SRC_MASK = 0x30000000,
	NI_GPCT_NO_PRESCALE_CLOCK_SRC_BITS = 0x0,
	/* divide source by 2 */
	NI_GPCT_PRESCALE_X2_CLOCK_SRC_BITS = 0x10000000,
	/* divide source by 8 */
	NI_GPCT_PRESCALE_X8_CLOCK_SRC_BITS = 0x20000000,
	NI_GPCT_INVERT_CLOCK_SRC_BIT = 0x80000000
};
static inline unsigned NI_GPCT_SOURCE_PIN_CLOCK_SRC_BITS(unsigned n)
{
	/* NI 660x-specific */
	return 0x10 + n;
}
static inline unsigned NI_GPCT_RTSI_CLOCK_SRC_BITS(unsigned n)
{
	return 0x18 + n;
}
static inline unsigned NI_GPCT_PFI_CLOCK_SRC_BITS(unsigned n)
{
	/* no pfi on NI 660x */
	return 0x20 + n;
}

/* Possibilities for setting a gate source with
INSN_CONFIG_SET_GATE_SRC when using NI general-purpose counters.
May be bitwise-or'd with CR_EDGE or CR_INVERT. */
enum ni_gpct_gate_select {
	/* m-series gates */
	NI_GPCT_TIMESTAMP_MUX_GATE_SELECT = 0x0,
	NI_GPCT_AI_START2_GATE_SELECT = 0x12,
	NI_GPCT_PXI_STAR_TRIGGER_GATE_SELECT = 0x13,
	NI_GPCT_NEXT_OUT_GATE_SELECT = 0x14,
	NI_GPCT_AI_START1_GATE_SELECT = 0x1c,
	NI_GPCT_NEXT_SOURCE_GATE_SELECT = 0x1d,
	NI_GPCT_ANALOG_TRIGGER_OUT_GATE_SELECT = 0x1e,
	NI_GPCT_LOGIC_LOW_GATE_SELECT = 0x1f,
	/* more gates for 660x */
	NI_GPCT_SOURCE_PIN_i_GATE_SELECT = 0x100,
	NI_GPCT_GATE_PIN_i_GATE_SELECT = 0x101,
	/* more gates for 660x "second gate" */
	NI_GPCT_UP_DOWN_PIN_i_GATE_SELECT = 0x201,
	NI_GPCT_SELECTED_GATE_GATE_SELECT = 0x21e,
	/* m-series "second gate" sources are unknown,
	 * we should add them here with an offset of 0x300 when
	 * known. */
	NI_GPCT_DISABLED_GATE_SELECT = 0x8000,
};
static inline unsigned NI_GPCT_GATE_PIN_GATE_SELECT(unsigned n)
{
	return 0x102 + n;
}
static inline unsigned NI_GPCT_RTSI_GATE_SELECT(unsigned n)
{
	return NI_USUAL_RTSI_SELECT(n);
}
static inline unsigned NI_GPCT_PFI_GATE_SELECT(unsigned n)
{
	return NI_USUAL_PFI_SELECT(n);
}
static inline unsigned NI_GPCT_UP_DOWN_PIN_GATE_SELECT(unsigned n)
{
	return 0x202 + n;
}

/* Possibilities for setting a source with
INSN_CONFIG_SET_OTHER_SRC when using NI general-purpose counters. */
enum ni_gpct_other_index {
	NI_GPCT_SOURCE_ENCODER_A,
	NI_GPCT_SOURCE_ENCODER_B,
	NI_GPCT_SOURCE_ENCODER_Z
};
enum ni_gpct_other_select {
	/* m-series gates */
	/* Still unknown, probably only need NI_GPCT_PFI_OTHER_SELECT */
	NI_GPCT_DISABLED_OTHER_SELECT = 0x8000,
};
static inline unsigned NI_GPCT_PFI_OTHER_SELECT(unsigned n)
{
	return NI_USUAL_PFI_SELECT(n);
}

/* start sources for ni general-purpose counters for use with
INSN_CONFIG_ARM */
enum ni_gpct_arm_source {
	NI_GPCT_ARM_IMMEDIATE = 0x0,
	NI_GPCT_ARM_PAIRED_IMMEDIATE = 0x1, /* Start both the counter
					     * and the adjacent paired
					     * counter simultaneously */
	/* NI doesn't document bits for selecting hardware arm triggers.
	 * If the NI_GPCT_ARM_UNKNOWN bit is set, we will pass the least
	 * significant bits (3 bits for 660x or 5 bits for m-series)
	 * through to the hardware.  This will at least allow someone to
	 * figure out what the bits do later. */
	NI_GPCT_ARM_UNKNOWN = 0x1000,
};

/* digital filtering options for ni 660x for use with INSN_CONFIG_FILTER. */
enum ni_gpct_filter_select {
	NI_GPCT_FILTER_OFF = 0x0,
	NI_GPCT_FILTER_TIMEBASE_3_SYNC = 0x1,
	NI_GPCT_FILTER_100x_TIMEBASE_1 = 0x2,
	NI_GPCT_FILTER_20x_TIMEBASE_1 = 0x3,
	NI_GPCT_FILTER_10x_TIMEBASE_1 = 0x4,
	NI_GPCT_FILTER_2x_TIMEBASE_1 = 0x5,
	NI_GPCT_FILTER_2x_TIMEBASE_3 = 0x6
};

/* PFI digital filtering options for ni m-series for use with
 * INSN_CONFIG_FILTER. */
enum ni_pfi_filter_select {
	NI_PFI_FILTER_OFF = 0x0,
	NI_PFI_FILTER_125ns = 0x1,
	NI_PFI_FILTER_6425ns = 0x2,
	NI_PFI_FILTER_2550us = 0x3
};

/* master clock sources for ni mio boards and INSN_CONFIG_SET_CLOCK_SRC */
enum ni_mio_clock_source {
	NI_MIO_INTERNAL_CLOCK = 0,
	NI_MIO_RTSI_CLOCK = 1,	/* doesn't work for m-series, use
				   NI_MIO_PLL_RTSI_CLOCK() */
	/* the NI_MIO_PLL_* sources are m-series only */
	NI_MIO_PLL_PXI_STAR_TRIGGER_CLOCK = 2,
	NI_MIO_PLL_PXI10_CLOCK = 3,
	NI_MIO_PLL_RTSI0_CLOCK = 4
};
static inline unsigned NI_MIO_PLL_RTSI_CLOCK(unsigned rtsi_channel)
{
	return NI_MIO_PLL_RTSI0_CLOCK + rtsi_channel;
}

/* Signals which can be routed to an NI RTSI pin with INSN_CONFIG_SET_ROUTING.
 The numbers assigned are not arbitrary, they correspond to the bits required
 to program the board. */
enum ni_rtsi_routing {
	NI_RTSI_OUTPUT_ADR_START1 = 0,
	NI_RTSI_OUTPUT_ADR_START2 = 1,
	NI_RTSI_OUTPUT_SCLKG = 2,
	NI_RTSI_OUTPUT_DACUPDN = 3,
	NI_RTSI_OUTPUT_DA_START1 = 4,
	NI_RTSI_OUTPUT_G_SRC0 = 5,
	NI_RTSI_OUTPUT_G_GATE0 = 6,
	NI_RTSI_OUTPUT_RGOUT0 = 7,
	NI_RTSI_OUTPUT_RTSI_BRD_0 = 8,
	NI_RTSI_OUTPUT_RTSI_OSC = 12	/* pre-m-series always have RTSI
					 * clock on line 7 */
};
static inline unsigned NI_RTSI_OUTPUT_RTSI_BRD(unsigned n)
{
	return NI_RTSI_OUTPUT_RTSI_BRD_0 + n;
}

/* Signals which can be routed to an NI PFI pin on an m-series board with
 * INSN_CONFIG_SET_ROUTING.  These numbers are also returned by
 * INSN_CONFIG_GET_ROUTING on pre-m-series boards, even though their routing
 * cannot be changed.  The numbers assigned are not arbitrary, they correspond
 * to the bits required to program the board. */
enum ni_pfi_routing {
	NI_PFI_OUTPUT_PFI_DEFAULT = 0,
	NI_PFI_OUTPUT_AI_START1 = 1,
	NI_PFI_OUTPUT_AI_START2 = 2,
	NI_PFI_OUTPUT_AI_CONVERT = 3,
	NI_PFI_OUTPUT_G_SRC1 = 4,
	NI_PFI_OUTPUT_G_GATE1 = 5,
	NI_PFI_OUTPUT_AO_UPDATE_N = 6,
	NI_PFI_OUTPUT_AO_START1 = 7,
	NI_PFI_OUTPUT_AI_START_PULSE = 8,
	NI_PFI_OUTPUT_G_SRC0 = 9,
	NI_PFI_OUTPUT_G_GATE0 = 10,
	NI_PFI_OUTPUT_EXT_STROBE = 11,
	NI_PFI_OUTPUT_AI_EXT_MUX_CLK = 12,
	NI_PFI_OUTPUT_GOUT0 = 13,
	NI_PFI_OUTPUT_GOUT1 = 14,
	NI_PFI_OUTPUT_FREQ_OUT = 15,
	NI_PFI_OUTPUT_PFI_DO = 16,
	NI_PFI_OUTPUT_I_ATRIG = 17,
	NI_PFI_OUTPUT_RTSI0 = 18,
	NI_PFI_OUTPUT_PXI_STAR_TRIGGER_IN = 26,
	NI_PFI_OUTPUT_SCXI_TRIG1 = 27,
	NI_PFI_OUTPUT_DIO_CHANGE_DETECT_RTSI = 28,
	NI_PFI_OUTPUT_CDI_SAMPLE = 29,
	NI_PFI_OUTPUT_CDO_UPDATE = 30
};
static inline unsigned NI_PFI_OUTPUT_RTSI(unsigned rtsi_channel)
{
	return NI_PFI_OUTPUT_RTSI0 + rtsi_channel;
}

/* Signals which can be routed to output on a NI PFI pin on a 660x board
 with INSN_CONFIG_SET_ROUTING.  The numbers assigned are
 not arbitrary, they correspond to the bits required
 to program the board.  Lines 0 to 7 can only be set to
 NI_660X_PFI_OUTPUT_DIO.  Lines 32 to 39 can only be set to
 NI_660X_PFI_OUTPUT_COUNTER. */
enum ni_660x_pfi_routing {
	NI_660X_PFI_OUTPUT_COUNTER = 1,	/* counter */
	NI_660X_PFI_OUTPUT_DIO = 2,	/* static digital output */
};

/* NI External Trigger lines.  These values are not arbitrary, but are related
 * to the bits required to program the board (offset by 1 for historical
 * reasons). */
static inline unsigned NI_EXT_PFI(unsigned pfi_channel)
{
	return NI_USUAL_PFI_SELECT(pfi_channel) - 1;
}
static inline unsigned NI_EXT_RTSI(unsigned rtsi_channel)
{
	return NI_USUAL_RTSI_SELECT(rtsi_channel) - 1;
}

/* status bits for INSN_CONFIG_GET_COUNTER_STATUS */
enum comedi_counter_status_flags {
	COMEDI_COUNTER_ARMED = 0x1,
	COMEDI_COUNTER_COUNTING = 0x2,
	COMEDI_COUNTER_TERMINAL_COUNT = 0x4,
};

/* Clock sources for CDIO subdevice on NI m-series boards.  Used as the
 * scan_begin_arg for a comedi_command. These sources may also be bitwise-or'd
 * with CR_INVERT to change polarity. */
enum ni_m_series_cdio_scan_begin_src {
	NI_CDIO_SCAN_BEGIN_SRC_GROUND = 0,
	NI_CDIO_SCAN_BEGIN_SRC_AI_START = 18,
	NI_CDIO_SCAN_BEGIN_SRC_AI_CONVERT = 19,
	NI_CDIO_SCAN_BEGIN_SRC_PXI_STAR_TRIGGER = 20,
	NI_CDIO_SCAN_BEGIN_SRC_G0_OUT = 28,
	NI_CDIO_SCAN_BEGIN_SRC_G1_OUT = 29,
	NI_CDIO_SCAN_BEGIN_SRC_ANALOG_TRIGGER = 30,
	NI_CDIO_SCAN_BEGIN_SRC_AO_UPDATE = 31,
	NI_CDIO_SCAN_BEGIN_SRC_FREQ_OUT = 32,
	NI_CDIO_SCAN_BEGIN_SRC_DIO_CHANGE_DETECT_IRQ = 33
};
static inline unsigned NI_CDIO_SCAN_BEGIN_SRC_PFI(unsigned pfi_channel)
{
	return NI_USUAL_PFI_SELECT(pfi_channel);
}
static inline unsigned NI_CDIO_SCAN_BEGIN_SRC_RTSI(unsigned rtsi_channel)
{
	return NI_USUAL_RTSI_SELECT(rtsi_channel);
}

/* scan_begin_src for scan_begin_arg==TRIG_EXT with analog output command on NI
 * boards.  These scan begin sources can also be bitwise-or'd with CR_INVERT to
 * change polarity. */
static inline unsigned NI_AO_SCAN_BEGIN_SRC_PFI(unsigned pfi_channel)
{
	return NI_USUAL_PFI_SELECT(pfi_channel);
}
static inline unsigned NI_AO_SCAN_BEGIN_SRC_RTSI(unsigned rtsi_channel)
{
	return NI_USUAL_RTSI_SELECT(rtsi_channel);
}

/* Bits for setting a clock source with
 * INSN_CONFIG_SET_CLOCK_SRC when using NI frequency output subdevice. */
enum ni_freq_out_clock_source_bits {
	NI_FREQ_OUT_TIMEBASE_1_DIV_2_CLOCK_SRC,	/* 10 MHz */
	NI_FREQ_OUT_TIMEBASE_2_CLOCK_SRC	/* 100 KHz */
};

/* Values for setting a clock source with INSN_CONFIG_SET_CLOCK_SRC for
 * 8254 counter subdevices on Amplicon DIO boards (amplc_dio200 driver). */
enum amplc_dio_clock_source {
	AMPLC_DIO_CLK_CLKN,	/* per channel external clock
				   input/output pin (pin is only an
				   input when clock source set to this
				   value, otherwise it is an output) */
	AMPLC_DIO_CLK_10MHZ,	/* 10 MHz internal clock */
	AMPLC_DIO_CLK_1MHZ,	/* 1 MHz internal clock */
	AMPLC_DIO_CLK_100KHZ,	/* 100 kHz internal clock */
	AMPLC_DIO_CLK_10KHZ,	/* 10 kHz internal clock */
	AMPLC_DIO_CLK_1KHZ,	/* 1 kHz internal clock */
	AMPLC_DIO_CLK_OUTNM1,	/* output of preceding counter channel
				   (for channel 0, preceding counter
				   channel is channel 2 on preceding
				   counter subdevice, for first counter
				   subdevice, preceding counter
				   subdevice is the last counter
				   subdevice) */
	AMPLC_DIO_CLK_EXT	/* per chip external input pin */
};

/* Values for setting a gate source with INSN_CONFIG_SET_GATE_SRC for
 * 8254 counter subdevices on Amplicon DIO boards (amplc_dio200 driver). */
enum amplc_dio_gate_source {
	AMPLC_DIO_GAT_VCC,	/* internal high logic level */
	AMPLC_DIO_GAT_GND,	/* internal low logic level */
	AMPLC_DIO_GAT_GATN,	/* per channel external gate input */
	AMPLC_DIO_GAT_NOUTNM2,	/* negated output of counter channel
				   minus 2 (for channels 0 or 1,
				   channel minus 2 is channel 1 or 2 on
				   the preceding counter subdevice, for
				   the first counter subdevice the
				   preceding counter subdevice is the
				   last counter subdevice) */
	AMPLC_DIO_GAT_RESERVED4,
	AMPLC_DIO_GAT_RESERVED5,
	AMPLC_DIO_GAT_RESERVED6,
	AMPLC_DIO_GAT_RESERVED7
};

#endif /* _COMEDI_H */
