/* SPDX-License-Identifier: LGPL-2.0+ */
/*
 * comedi.h
 * header file for COMEDI user API
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1998-2001 David A. Schleef <ds@schleef.org>
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
 * maximum number of minor devices.  This can be increased, although
 * kernel structures are currently statically allocated, thus you
 * don't want this to be much more than you actually use.
 */
#define COMEDI_NDEVICES 16

/* number of config options in the config structure */
#define COMEDI_NDEVCONFOPTS 32

/*
 * NOTE: 'comedi_config --init-data' is deprecated
 *
 * The following indexes in the config options were used by
 * comedi_config to pass firmware blobs from user space to the
 * comedi drivers. The request_firmware() hotplug interface is
 * now used by all comedi drivers instead.
 */

/* length of nth chunk of firmware data -*/
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
	((((aref) & 0x3) << 24) | (((rng) & 0xff) << 16) | (chan))
#define CR_PACK_FLAGS(chan, range, aref, flags)				\
	(CR_PACK(chan, range, aref) | ((flags) & CR_FLAGS_MASK))

#define CR_CHAN(a)	((a) & 0xffff)
#define CR_RANGE(a)	(((a) >> 16) & 0xff)
#define CR_AREF(a)	(((a) >> 24) & 0x03)

#define CR_FLAGS_MASK	0xfc000000
#define CR_ALT_FILTER	0x04000000
#define CR_DITHER	CR_ALT_FILTER
#define CR_DEGLITCH	CR_ALT_FILTER
#define CR_ALT_SOURCE	0x08000000
#define CR_EDGE		0x40000000
#define CR_INVERT	0x80000000

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
#define INSN_BITS		(2 | INSN_MASK_READ | INSN_MASK_WRITE)
#define INSN_CONFIG		(3 | INSN_MASK_READ | INSN_MASK_WRITE)
#define INSN_GTOD		(4 | INSN_MASK_READ | INSN_MASK_SPECIAL)
#define INSN_WAIT		(5 | INSN_MASK_WRITE | INSN_MASK_SPECIAL)
#define INSN_INTTRIG		(6 | INSN_MASK_WRITE | INSN_MASK_SPECIAL)

/* command flags */
/* These flags are used in comedi_cmd structures */

#define CMDF_BOGUS		0x00000001	/* do the motions */

/* try to use a real-time interrupt while performing command */
#define CMDF_PRIORITY		0x00000008

/* wake up on end-of-scan events */
#define CMDF_WAKE_EOS		0x00000020

#define CMDF_WRITE		0x00000040

#define CMDF_RAWDATA		0x00000080

/* timer rounding definitions */
#define CMDF_ROUND_MASK		0x00030000
#define CMDF_ROUND_NEAREST	0x00000000
#define CMDF_ROUND_DOWN		0x00010000
#define CMDF_ROUND_UP		0x00020000
#define CMDF_ROUND_UP_NEXT	0x00030000

#define COMEDI_EV_START		0x00040000
#define COMEDI_EV_SCAN_BEGIN	0x00080000
#define COMEDI_EV_CONVERT	0x00100000
#define COMEDI_EV_SCAN_END	0x00200000
#define COMEDI_EV_STOP		0x00400000

/* compatibility definitions */
#define TRIG_BOGUS		CMDF_BOGUS
#define TRIG_RT			CMDF_PRIORITY
#define TRIG_WAKE_EOS		CMDF_WAKE_EOS
#define TRIG_WRITE		CMDF_WRITE
#define TRIG_ROUND_MASK		CMDF_ROUND_MASK
#define TRIG_ROUND_NEAREST	CMDF_ROUND_NEAREST
#define TRIG_ROUND_DOWN		CMDF_ROUND_DOWN
#define TRIG_ROUND_UP		CMDF_ROUND_UP
#define TRIG_ROUND_UP_NEXT	CMDF_ROUND_UP_NEXT

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
#define SDF_PWM_COUNTER 0x0080	/* PWM can automatically switch off */
#define SDF_PWM_HBRIDGE 0x0100	/* PWM is signed (H-bridge) */
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

/* subdevice types */

/**
 * enum comedi_subdevice_type - COMEDI subdevice types
 * @COMEDI_SUBD_UNUSED:		Unused subdevice.
 * @COMEDI_SUBD_AI:		Analog input.
 * @COMEDI_SUBD_AO:		Analog output.
 * @COMEDI_SUBD_DI:		Digital input.
 * @COMEDI_SUBD_DO:		Digital output.
 * @COMEDI_SUBD_DIO:		Digital input/output.
 * @COMEDI_SUBD_COUNTER:	Counter.
 * @COMEDI_SUBD_TIMER:		Timer.
 * @COMEDI_SUBD_MEMORY:		Memory, EEPROM, DPRAM.
 * @COMEDI_SUBD_CALIB:		Calibration DACs.
 * @COMEDI_SUBD_PROC:		Processor, DSP.
 * @COMEDI_SUBD_SERIAL:		Serial I/O.
 * @COMEDI_SUBD_PWM:		Pulse-Width Modulation output.
 */
enum comedi_subdevice_type {
	COMEDI_SUBD_UNUSED,
	COMEDI_SUBD_AI,
	COMEDI_SUBD_AO,
	COMEDI_SUBD_DI,
	COMEDI_SUBD_DO,
	COMEDI_SUBD_DIO,
	COMEDI_SUBD_COUNTER,
	COMEDI_SUBD_TIMER,
	COMEDI_SUBD_MEMORY,
	COMEDI_SUBD_CALIB,
	COMEDI_SUBD_PROC,
	COMEDI_SUBD_SERIAL,
	COMEDI_SUBD_PWM
};

/* configuration instructions */

/**
 * enum comedi_io_direction - COMEDI I/O directions
 * @COMEDI_INPUT:	Input.
 * @COMEDI_OUTPUT:	Output.
 * @COMEDI_OPENDRAIN:	Open-drain (or open-collector) output.
 *
 * These are used by the %INSN_CONFIG_DIO_QUERY configuration instruction to
 * report a direction.  They may also be used in other places where a direction
 * needs to be specified.
 */
enum comedi_io_direction {
	COMEDI_INPUT = 0,
	COMEDI_OUTPUT = 1,
	COMEDI_OPENDRAIN = 2
};

/**
 * enum configuration_ids - COMEDI configuration instruction codes
 * @INSN_CONFIG_DIO_INPUT:	Configure digital I/O as input.
 * @INSN_CONFIG_DIO_OUTPUT:	Configure digital I/O as output.
 * @INSN_CONFIG_DIO_OPENDRAIN:	Configure digital I/O as open-drain (or open
 *				collector) output.
 * @INSN_CONFIG_ANALOG_TRIG:	Configure analog trigger.
 * @INSN_CONFIG_ALT_SOURCE:	Configure alternate input source.
 * @INSN_CONFIG_DIGITAL_TRIG:	Configure digital trigger.
 * @INSN_CONFIG_BLOCK_SIZE:	Configure block size for DMA transfers.
 * @INSN_CONFIG_TIMER_1:	Configure divisor for external clock.
 * @INSN_CONFIG_FILTER:		Configure a filter.
 * @INSN_CONFIG_CHANGE_NOTIFY:	Configure change notification for digital
 *				inputs.  (New drivers should use
 *				%INSN_CONFIG_DIGITAL_TRIG instead.)
 * @INSN_CONFIG_SERIAL_CLOCK:	Configure clock for serial I/O.
 * @INSN_CONFIG_BIDIRECTIONAL_DATA: Send and receive byte over serial I/O.
 * @INSN_CONFIG_DIO_QUERY:	Query direction of digital I/O channel.
 * @INSN_CONFIG_PWM_OUTPUT:	Configure pulse-width modulator output.
 * @INSN_CONFIG_GET_PWM_OUTPUT:	Get pulse-width modulator output configuration.
 * @INSN_CONFIG_ARM:		Arm a subdevice or channel.
 * @INSN_CONFIG_DISARM:		Disarm a subdevice or channel.
 * @INSN_CONFIG_GET_COUNTER_STATUS: Get counter status.
 * @INSN_CONFIG_RESET:		Reset a subdevice or channel.
 * @INSN_CONFIG_GPCT_SINGLE_PULSE_GENERATOR: Configure counter/timer as
 *				single pulse generator.
 * @INSN_CONFIG_GPCT_PULSE_TRAIN_GENERATOR: Configure counter/timer as
 *				pulse train generator.
 * @INSN_CONFIG_GPCT_QUADRATURE_ENCODER: Configure counter as a quadrature
 *				encoder.
 * @INSN_CONFIG_SET_GATE_SRC:	Set counter/timer gate source.
 * @INSN_CONFIG_GET_GATE_SRC:	Get counter/timer gate source.
 * @INSN_CONFIG_SET_CLOCK_SRC:	Set counter/timer master clock source.
 * @INSN_CONFIG_GET_CLOCK_SRC:	Get counter/timer master clock source.
 * @INSN_CONFIG_SET_OTHER_SRC:	Set counter/timer "other" source.
 * @INSN_CONFIG_GET_HARDWARE_BUFFER_SIZE: Get size (in bytes) of subdevice's
 *				on-board FIFOs used during streaming
 *				input/output.
 * @INSN_CONFIG_SET_COUNTER_MODE: Set counter/timer mode.
 * @INSN_CONFIG_8254_SET_MODE:	(Deprecated) Same as
 *				%INSN_CONFIG_SET_COUNTER_MODE.
 * @INSN_CONFIG_8254_READ_STATUS: Read status of 8254 counter channel.
 * @INSN_CONFIG_SET_ROUTING:	Set routing for a channel.
 * @INSN_CONFIG_GET_ROUTING:	Get routing for a channel.
 * @INSN_CONFIG_PWM_SET_PERIOD: Set PWM period in nanoseconds.
 * @INSN_CONFIG_PWM_GET_PERIOD: Get PWM period in nanoseconds.
 * @INSN_CONFIG_GET_PWM_STATUS: Get PWM status.
 * @INSN_CONFIG_PWM_SET_H_BRIDGE: Set PWM H bridge duty cycle and polarity for
 *				a relay simultaneously.
 * @INSN_CONFIG_PWM_GET_H_BRIDGE: Get PWM H bridge duty cycle and polarity.
 */
enum configuration_ids {
	INSN_CONFIG_DIO_INPUT = COMEDI_INPUT,
	INSN_CONFIG_DIO_OUTPUT = COMEDI_OUTPUT,
	INSN_CONFIG_DIO_OPENDRAIN = COMEDI_OPENDRAIN,
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
	INSN_CONFIG_GPCT_SINGLE_PULSE_GENERATOR = 1001,
	INSN_CONFIG_GPCT_PULSE_TRAIN_GENERATOR = 1002,
	INSN_CONFIG_GPCT_QUADRATURE_ENCODER = 1003,
	INSN_CONFIG_SET_GATE_SRC = 2001,
	INSN_CONFIG_GET_GATE_SRC = 2002,
	INSN_CONFIG_SET_CLOCK_SRC = 2003,
	INSN_CONFIG_GET_CLOCK_SRC = 2004,
	INSN_CONFIG_SET_OTHER_SRC = 2005,
	INSN_CONFIG_GET_HARDWARE_BUFFER_SIZE = 2006,
	INSN_CONFIG_SET_COUNTER_MODE = 4097,
	INSN_CONFIG_8254_SET_MODE = INSN_CONFIG_SET_COUNTER_MODE,
	INSN_CONFIG_8254_READ_STATUS = 4098,
	INSN_CONFIG_SET_ROUTING = 4099,
	INSN_CONFIG_GET_ROUTING = 4109,
	INSN_CONFIG_PWM_SET_PERIOD = 5000,
	INSN_CONFIG_PWM_GET_PERIOD = 5001,
	INSN_CONFIG_GET_PWM_STATUS = 5002,
	INSN_CONFIG_PWM_SET_H_BRIDGE = 5003,
	INSN_CONFIG_PWM_GET_H_BRIDGE = 5004
};

/**
 * enum comedi_digital_trig_op - operations for configuring a digital trigger
 * @COMEDI_DIGITAL_TRIG_DISABLE:	Return digital trigger to its default,
 *					inactive, unconfigured state.
 * @COMEDI_DIGITAL_TRIG_ENABLE_EDGES:	Set rising and/or falling edge inputs
 *					that each can fire the trigger.
 * @COMEDI_DIGITAL_TRIG_ENABLE_LEVELS:	Set a combination of high and/or low
 *					level inputs that can fire the trigger.
 *
 * These are used with the %INSN_CONFIG_DIGITAL_TRIG configuration instruction.
 * The data for the configuration instruction is as follows...
 *
 *   data[%0] = %INSN_CONFIG_DIGITAL_TRIG
 *
 *   data[%1] = trigger ID
 *
 *   data[%2] = configuration operation
 *
 *   data[%3] = configuration parameter 1
 *
 *   data[%4] = configuration parameter 2
 *
 *   data[%5] = configuration parameter 3
 *
 * The trigger ID (data[%1]) is used to differentiate multiple digital triggers
 * belonging to the same subdevice.  The configuration operation (data[%2]) is
 * one of the enum comedi_digital_trig_op values.  The configuration
 * parameters (data[%3], data[%4], and data[%5]) depend on the operation; they
 * are not used with %COMEDI_DIGITAL_TRIG_DISABLE.
 *
 * For %COMEDI_DIGITAL_TRIG_ENABLE_EDGES and %COMEDI_DIGITAL_TRIG_ENABLE_LEVELS,
 * configuration parameter 1 (data[%3]) contains a "left-shift" value that
 * specifies the input corresponding to bit 0 of configuration parameters 2
 * and 3.  This is useful if the trigger has more than 32 inputs.
 *
 * For %COMEDI_DIGITAL_TRIG_ENABLE_EDGES, configuration parameter 2 (data[%4])
 * specifies which of up to 32 inputs have rising-edge sensitivity, and
 * configuration parameter 3 (data[%5]) specifies which of up to 32 inputs
 * have falling-edge sensitivity that can fire the trigger.
 *
 * For %COMEDI_DIGITAL_TRIG_ENABLE_LEVELS, configuration parameter 2 (data[%4])
 * specifies which of up to 32 inputs must be at a high level, and
 * configuration parameter 3 (data[%5]) specifies which of up to 32 inputs
 * must be at a low level for the trigger to fire.
 *
 * Some sequences of %INSN_CONFIG_DIGITAL_TRIG instructions may have a (partly)
 * accumulative effect, depending on the low-level driver.  This is useful
 * when setting up a trigger that has more than 32 inputs, or has a combination
 * of edge- and level-triggered inputs.
 */
enum comedi_digital_trig_op {
	COMEDI_DIGITAL_TRIG_DISABLE = 0,
	COMEDI_DIGITAL_TRIG_ENABLE_EDGES = 1,
	COMEDI_DIGITAL_TRIG_ENABLE_LEVELS = 2
};

/**
 * enum comedi_support_level - support level for a COMEDI feature
 * @COMEDI_UNKNOWN_SUPPORT:	Unspecified support for feature.
 * @COMEDI_SUPPORTED:		Feature is supported.
 * @COMEDI_UNSUPPORTED:		Feature is unsupported.
 */
enum comedi_support_level {
	COMEDI_UNKNOWN_SUPPORT = 0,
	COMEDI_SUPPORTED,
	COMEDI_UNSUPPORTED
};

/**
 * enum comedi_counter_status_flags - counter status bits
 * @COMEDI_COUNTER_ARMED:		Counter is armed.
 * @COMEDI_COUNTER_COUNTING:		Counter is counting.
 * @COMEDI_COUNTER_TERMINAL_COUNT:	Counter reached terminal count.
 *
 * These bitwise values are used by the %INSN_CONFIG_GET_COUNTER_STATUS
 * configuration instruction to report the status of a counter.
 */
enum comedi_counter_status_flags {
	COMEDI_COUNTER_ARMED = 0x1,
	COMEDI_COUNTER_COUNTING = 0x2,
	COMEDI_COUNTER_TERMINAL_COUNT = 0x4,
};

/* ioctls */

#define CIO 'd'
#define COMEDI_DEVCONFIG _IOW(CIO, 0, struct comedi_devconfig)
#define COMEDI_DEVINFO _IOR(CIO, 1, struct comedi_devinfo)
#define COMEDI_SUBDINFO _IOR(CIO, 2, struct comedi_subdinfo)
#define COMEDI_CHANINFO _IOR(CIO, 3, struct comedi_chaninfo)
/* _IOWR(CIO, 4, ...) is reserved */
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
#define COMEDI_SETRSUBD _IO(CIO, 16)
#define COMEDI_SETWSUBD _IO(CIO, 17)

/* structures */

/**
 * struct comedi_insn - COMEDI instruction
 * @insn:	COMEDI instruction type (%INSN_xxx).
 * @n:		Length of @data[].
 * @data:	Pointer to data array operated on by the instruction.
 * @subdev:	Subdevice index.
 * @chanspec:	A packed "chanspec" value consisting of channel number,
 *		analog range index, analog reference type, and flags.
 * @unused:	Reserved for future use.
 *
 * This is used with the %COMEDI_INSN ioctl, and indirectly with the
 * %COMEDI_INSNLIST ioctl.
 */
struct comedi_insn {
	unsigned int insn;
	unsigned int n;
	unsigned int __user *data;
	unsigned int subdev;
	unsigned int chanspec;
	unsigned int unused[3];
};

/**
 * struct comedi_insnlist - list of COMEDI instructions
 * @n_insns:	Number of COMEDI instructions.
 * @insns:	Pointer to array COMEDI instructions.
 *
 * This is used with the %COMEDI_INSNLIST ioctl.
 */
struct comedi_insnlist {
	unsigned int n_insns;
	struct comedi_insn __user *insns;
};

/**
 * struct comedi_cmd - COMEDI asynchronous acquisition command details
 * @subdev:		Subdevice index.
 * @flags:		Command flags (%CMDF_xxx).
 * @start_src:		"Start acquisition" trigger source (%TRIG_xxx).
 * @start_arg:		"Start acquisition" trigger argument.
 * @scan_begin_src:	"Scan begin" trigger source.
 * @scan_begin_arg:	"Scan begin" trigger argument.
 * @convert_src:	"Convert" trigger source.
 * @convert_arg:	"Convert" trigger argument.
 * @scan_end_src:	"Scan end" trigger source.
 * @scan_end_arg:	"Scan end" trigger argument.
 * @stop_src:		"Stop acquisition" trigger source.
 * @stop_arg:		"Stop acquisition" trigger argument.
 * @chanlist:		Pointer to array of "chanspec" values, containing a
 *			sequence of channel numbers packed with analog range
 *			index, etc.
 * @chanlist_len:	Number of channels in sequence.
 * @data:		Pointer to miscellaneous set-up data (not used).
 * @data_len:		Length of miscellaneous set-up data.
 *
 * This is used with the %COMEDI_CMD or %COMEDI_CMDTEST ioctl to set-up
 * or validate an asynchronous acquisition command.  The ioctl may modify
 * the &struct comedi_cmd and copy it back to the caller.
 *
 * Optional command @flags values that can be ORed together...
 *
 * %CMDF_BOGUS - makes %COMEDI_CMD ioctl return error %EAGAIN instead of
 * starting the command.
 *
 * %CMDF_PRIORITY - requests "hard real-time" processing (which is not
 * supported in this version of COMEDI).
 *
 * %CMDF_WAKE_EOS - requests the command makes data available for reading
 * after every "scan" period.
 *
 * %CMDF_WRITE - marks the command as being in the "write" (to device)
 * direction.  This does not need to be specified by the caller unless the
 * subdevice supports commands in either direction.
 *
 * %CMDF_RAWDATA - prevents the command from "munging" the data between the
 * COMEDI sample format and the raw hardware sample format.
 *
 * %CMDF_ROUND_NEAREST - requests timing periods to be rounded to nearest
 * supported values.
 *
 * %CMDF_ROUND_DOWN - requests timing periods to be rounded down to supported
 * values (frequencies rounded up).
 *
 * %CMDF_ROUND_UP - requests timing periods to be rounded up to supported
 * values (frequencies rounded down).
 *
 * Trigger source values for @start_src, @scan_begin_src, @convert_src,
 * @scan_end_src, and @stop_src...
 *
 * %TRIG_ANY - "all ones" value used to test which trigger sources are
 * supported.
 *
 * %TRIG_INVALID - "all zeroes" value used to indicate that all requested
 * trigger sources are invalid.
 *
 * %TRIG_NONE - never trigger (often used as a @stop_src value).
 *
 * %TRIG_NOW - trigger after '_arg' nanoseconds.
 *
 * %TRIG_FOLLOW - trigger follows another event.
 *
 * %TRIG_TIMER - trigger every '_arg' nanoseconds.
 *
 * %TRIG_COUNT - trigger when count '_arg' is reached.
 *
 * %TRIG_EXT - trigger on external signal specified by '_arg'.
 *
 * %TRIG_INT - trigger on internal, software trigger specified by '_arg'.
 *
 * %TRIG_OTHER - trigger on other, driver-defined signal specified by '_arg'.
 */
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

	unsigned int *chanlist;
	unsigned int chanlist_len;

	short __user *data;
	unsigned int data_len;
};

/**
 * struct comedi_chaninfo - used to retrieve per-channel information
 * @subdev:		Subdevice index.
 * @maxdata_list:	Optional pointer to per-channel maximum data values.
 * @flaglist:		Optional pointer to per-channel flags.
 * @rangelist:		Optional pointer to per-channel range types.
 * @unused:		Reserved for future use.
 *
 * This is used with the %COMEDI_CHANINFO ioctl to get per-channel information
 * for the subdevice.  Use of this requires knowledge of the number of channels
 * and subdevice flags obtained using the %COMEDI_SUBDINFO ioctl.
 *
 * The @maxdata_list member must be %NULL unless the %SDF_MAXDATA subdevice
 * flag is set.  The @flaglist member must be %NULL unless the %SDF_FLAGS
 * subdevice flag is set.  The @rangelist member must be %NULL unless the
 * %SDF_RANGETYPE subdevice flag is set.  Otherwise, the arrays they point to
 * must be at least as long as the number of channels.
 */
struct comedi_chaninfo {
	unsigned int subdev;
	unsigned int __user *maxdata_list;
	unsigned int __user *flaglist;
	unsigned int __user *rangelist;
	unsigned int unused[4];
};

/**
 * struct comedi_rangeinfo - used to retrieve the range table for a channel
 * @range_type:		Encodes subdevice index (bits 27:24), channel index
 *			(bits 23:16) and range table length (bits 15:0).
 * @range_ptr:		Pointer to array of @struct comedi_krange to be filled
 *			in with the range table for the channel or subdevice.
 *
 * This is used with the %COMEDI_RANGEINFO ioctl to retrieve the range table
 * for a specific channel (if the subdevice has the %SDF_RANGETYPE flag set to
 * indicate that the range table depends on the channel), or for the subdevice
 * as a whole (if the %SDF_RANGETYPE flag is clear, indicating the range table
 * is shared by all channels).
 *
 * The @range_type value is an input to the ioctl and comes from a previous
 * use of the %COMEDI_SUBDINFO ioctl (if the %SDF_RANGETYPE flag is clear),
 * or the %COMEDI_CHANINFO ioctl (if the %SDF_RANGETYPE flag is set).
 */
struct comedi_rangeinfo {
	unsigned int range_type;
	void __user *range_ptr;
};

/**
 * struct comedi_krange - describes a range in a range table
 * @min:	Minimum value in millionths (1e-6) of a unit.
 * @max:	Maximum value in millionths (1e-6) of a unit.
 * @flags:	Indicates the units (in bits 7:0) OR'ed with optional flags.
 *
 * A range table is associated with a single channel, or with all channels in a
 * subdevice, and a list of one or more ranges.  A %struct comedi_krange
 * describes the physical range of units for one of those ranges.  Sample
 * values in COMEDI are unsigned from %0 up to some 'maxdata' value.  The
 * mapping from sample values to physical units is assumed to be nomimally
 * linear (for the purpose of describing the range), with sample value %0
 * mapping to @min, and the 'maxdata' sample value mapping to @max.
 *
 * The currently defined units are %UNIT_volt (%0), %UNIT_mA (%1), and
 * %UNIT_none (%2).  The @min and @max values are the physical range multiplied
 * by 1e6, so a @max value of %1000000 (with %UNIT_volt) represents a maximal
 * value of 1 volt.
 *
 * The only defined flag value is %RF_EXTERNAL (%0x100), indicating that the
 * the range needs to be multiplied by an external reference.
 */
struct comedi_krange {
	int min;
	int max;
	unsigned int flags;
};

/**
 * struct comedi_subdinfo - used to retrieve information about a subdevice
 * @type:		Type of subdevice from &enum comedi_subdevice_type.
 * @n_chan:		Number of channels the subdevice supports.
 * @subd_flags:		A mixture of static and dynamic flags describing
 *			aspects of the subdevice and its current state.
 * @timer_type:		Timer type.  Always set to %5 ("nanosecond timer").
 * @len_chanlist:	Maximum length of a channel list if the subdevice
 *			supports asynchronous acquisition commands.
 * @maxdata:		Maximum sample value for all channels if the
 *			%SDF_MAXDATA subdevice flag is clear.
 * @flags:		Channel flags for all channels if the %SDF_FLAGS
 *			subdevice flag is clear.
 * @range_type:		The range type for all channels if the %SDF_RANGETYPE
 *			subdevice flag is clear.  Encodes the subdevice index
 *			(bits 27:24), a dummy channel index %0 (bits 23:16),
 *			and the range table length (bits 15:0).
 * @settling_time_0:	Not used.
 * @insn_bits_support:	Set to %COMEDI_SUPPORTED if the subdevice supports the
 *			%INSN_BITS instruction, or to %COMEDI_UNSUPPORTED if it
 *			does not.
 * @unused:		Reserved for future use.
 *
 * This is used with the %COMEDI_SUBDINFO ioctl which copies an array of
 * &struct comedi_subdinfo back to user space, with one element per subdevice.
 * Use of this requires knowledge of the number of subdevices obtained from
 * the %COMEDI_DEVINFO ioctl.
 *
 * These are the @subd_flags values that may be ORed together...
 *
 * %SDF_BUSY - the subdevice is busy processing an asynchronous command or a
 * synchronous instruction.
 *
 * %SDF_BUSY_OWNER - the subdevice is busy processing an asynchronous
 * acquisition command started on the current file object (the file object
 * issuing the %COMEDI_SUBDINFO ioctl).
 *
 * %SDF_LOCKED - the subdevice is locked by a %COMEDI_LOCK ioctl.
 *
 * %SDF_LOCK_OWNER - the subdevice is locked by a %COMEDI_LOCK ioctl from the
 * current file object.
 *
 * %SDF_MAXDATA - maximum sample values are channel-specific.
 *
 * %SDF_FLAGS - channel flags are channel-specific.
 *
 * %SDF_RANGETYPE - range types are channel-specific.
 *
 * %SDF_PWM_COUNTER - PWM can switch off automatically.
 *
 * %SDF_PWM_HBRIDGE - or PWM is signed (H-bridge).
 *
 * %SDF_CMD - the subdevice supports asynchronous commands.
 *
 * %SDF_SOFT_CALIBRATED - the subdevice uses software calibration.
 *
 * %SDF_CMD_WRITE - the subdevice supports asynchronous commands in the output
 * ("write") direction.
 *
 * %SDF_CMD_READ - the subdevice supports asynchronous commands in the input
 * ("read") direction.
 *
 * %SDF_READABLE - the subdevice is readable (e.g. analog input).
 *
 * %SDF_WRITABLE (aliased as %SDF_WRITEABLE) - the subdevice is writable (e.g.
 * analog output).
 *
 * %SDF_INTERNAL - the subdevice has no externally visible lines.
 *
 * %SDF_GROUND - the subdevice can use ground as an analog reference.
 *
 * %SDF_COMMON - the subdevice can use a common analog reference.
 *
 * %SDF_DIFF - the subdevice can use differential inputs (or outputs).
 *
 * %SDF_OTHER - the subdevice can use some other analog reference.
 *
 * %SDF_DITHER - the subdevice can do dithering.
 *
 * %SDF_DEGLITCH - the subdevice can do deglitching.
 *
 * %SDF_MMAP - this is never set.
 *
 * %SDF_RUNNING - an asynchronous command is still running.
 *
 * %SDF_LSAMPL - the subdevice uses "long" (32-bit) samples (for asynchronous
 * command data).
 *
 * %SDF_PACKED - the subdevice packs several DIO samples into a single sample
 * (for asynchronous command data).
 *
 * No "channel flags" (@flags) values are currently defined.
 */
struct comedi_subdinfo {
	unsigned int type;
	unsigned int n_chan;
	unsigned int subd_flags;
	unsigned int timer_type;
	unsigned int len_chanlist;
	unsigned int maxdata;
	unsigned int flags;
	unsigned int range_type;
	unsigned int settling_time_0;
	unsigned int insn_bits_support;
	unsigned int unused[8];
};

/**
 * struct comedi_devinfo - used to retrieve information about a COMEDI device
 * @version_code:	COMEDI version code.
 * @n_subdevs:		Number of subdevices the device has.
 * @driver_name:	Null-terminated COMEDI driver name.
 * @board_name:		Null-terminated COMEDI board name.
 * @read_subdevice:	Index of the current "read" subdevice (%-1 if none).
 * @write_subdevice:	Index of the current "write" subdevice (%-1 if none).
 * @unused:		Reserved for future use.
 *
 * This is used with the %COMEDI_DEVINFO ioctl to get basic information about
 * the device.
 */
struct comedi_devinfo {
	unsigned int version_code;
	unsigned int n_subdevs;
	char driver_name[COMEDI_NAMELEN];
	char board_name[COMEDI_NAMELEN];
	int read_subdevice;
	int write_subdevice;
	int unused[30];
};

/**
 * struct comedi_devconfig - used to configure a legacy COMEDI device
 * @board_name:		Null-terminated string specifying the type of board
 *			to configure.
 * @options:		An array of integer configuration options.
 *
 * This is used with the %COMEDI_DEVCONFIG ioctl to configure a "legacy" COMEDI
 * device, such as an ISA card.  Not all COMEDI drivers support this.  Those
 * that do either expect the specified board name to match one of a list of
 * names registered with the COMEDI core, or expect the specified board name
 * to match the COMEDI driver name itself.  The configuration options are
 * handled in a driver-specific manner.
 */
struct comedi_devconfig {
	char board_name[COMEDI_NAMELEN];
	int options[COMEDI_NDEVCONFOPTS];
};

/**
 * struct comedi_bufconfig - used to set or get buffer size for a subdevice
 * @subdevice:		Subdevice index.
 * @flags:		Not used.
 * @maximum_size:	Maximum allowed buffer size.
 * @size:		Buffer size.
 * @unused:		Reserved for future use.
 *
 * This is used with the %COMEDI_BUFCONFIG ioctl to get or configure the
 * maximum buffer size and current buffer size for a COMEDI subdevice that
 * supports asynchronous commands.  If the subdevice does not support
 * asynchronous commands, @maximum_size and @size are ignored and set to 0.
 *
 * On ioctl input, non-zero values of @maximum_size and @size specify a
 * new maximum size and new current size (in bytes), respectively.  These
 * will by rounded up to a multiple of %PAGE_SIZE.  Specifying a new maximum
 * size requires admin capabilities.
 *
 * On ioctl output, @maximum_size and @size and set to the current maximum
 * buffer size and current buffer size, respectively.
 */
struct comedi_bufconfig {
	unsigned int subdevice;
	unsigned int flags;

	unsigned int maximum_size;
	unsigned int size;

	unsigned int unused[4];
};

/**
 * struct comedi_bufinfo - used to manipulate buffer position for a subdevice
 * @subdevice:		Subdevice index.
 * @bytes_read:		Specify amount to advance read position for an
 *			asynchronous command in the input ("read") direction.
 * @buf_write_ptr:	Current write position (index) within the buffer.
 * @buf_read_ptr:	Current read position (index) within the buffer.
 * @buf_write_count:	Total amount written, modulo 2^32.
 * @buf_read_count:	Total amount read, modulo 2^32.
 * @bytes_written:	Specify amount to advance write position for an
 *			asynchronous command in the output ("write") direction.
 * @unused:		Reserved for future use.
 *
 * This is used with the %COMEDI_BUFINFO ioctl to optionally advance the
 * current read or write position in an asynchronous acquisition data buffer,
 * and to get the current read and write positions in the buffer.
 */
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

#define __RANGE(a, b)	((((a) & 0xffff) << 16) | ((b) & 0xffff))

#define RANGE_OFFSET(a)		(((a) >> 16) & 0xffff)
#define RANGE_LENGTH(b)		((b) & 0xffff)

#define RF_UNIT(flags)		((flags) & 0xff)
#define RF_EXTERNAL		0x100

#define UNIT_volt		0
#define UNIT_mA			1
#define UNIT_none		2

#define COMEDI_MIN_SPEED	0xffffffffu

/**********************************************************/
/* everything after this line is ALPHA */
/**********************************************************/

/*
 * 8254 specific configuration.
 *
 * It supports two config commands:
 *
 * 0 ID: INSN_CONFIG_SET_COUNTER_MODE
 * 1 8254 Mode
 * I8254_MODE0, I8254_MODE1, ..., I8254_MODE5
 * OR'ed with:
 * I8254_BCD, I8254_BINARY
 *
 * 0 ID: INSN_CONFIG_8254_READ_STATUS
 * 1 <-- Status byte returned here.
 * B7 = Output
 * B6 = NULL Count
 * B5 - B0 Current mode.
 */

enum i8254_mode {
	I8254_MODE0 = (0 << 1),	/* Interrupt on terminal count */
	I8254_MODE1 = (1 << 1),	/* Hardware retriggerable one-shot */
	I8254_MODE2 = (2 << 1),	/* Rate generator */
	I8254_MODE3 = (3 << 1),	/* Square wave mode */
	I8254_MODE4 = (4 << 1),	/* Software triggered strobe */
	/* Hardware triggered strobe (retriggerable) */
	I8254_MODE5 = (5 << 1),
	/* Use binary-coded decimal instead of binary (pretty useless) */
	I8254_BCD = 1,
	I8254_BINARY = 0
};

#define NI_USUAL_PFI_SELECT(x)	(((x) < 10) ? (0x1 + (x)) : (0xb + (x)))
#define NI_USUAL_RTSI_SELECT(x)	(((x) < 7) ? (0xb + (x)) : 0x1b)

/*
 * mode bits for NI general-purpose counters, set with
 * INSN_CONFIG_SET_COUNTER_MODE
 */
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

/*
 * Bits for setting a clock source with
 * INSN_CONFIG_SET_CLOCK_SRC when using NI general-purpose counters.
 */
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

/* NI 660x-specific */
#define NI_GPCT_SOURCE_PIN_CLOCK_SRC_BITS(x)	(0x10 + (x))

#define NI_GPCT_RTSI_CLOCK_SRC_BITS(x)		(0x18 + (x))

/* no pfi on NI 660x */
#define NI_GPCT_PFI_CLOCK_SRC_BITS(x)		(0x20 + (x))

/*
 * Possibilities for setting a gate source with
 * INSN_CONFIG_SET_GATE_SRC when using NI general-purpose counters.
 * May be bitwise-or'd with CR_EDGE or CR_INVERT.
 */
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
	/*
	 * m-series "second gate" sources are unknown,
	 * we should add them here with an offset of 0x300 when
	 * known.
	 */
	NI_GPCT_DISABLED_GATE_SELECT = 0x8000,
};

#define NI_GPCT_GATE_PIN_GATE_SELECT(x)		(0x102 + (x))
#define NI_GPCT_RTSI_GATE_SELECT(x)		NI_USUAL_RTSI_SELECT(x)
#define NI_GPCT_PFI_GATE_SELECT(x)		NI_USUAL_PFI_SELECT(x)
#define NI_GPCT_UP_DOWN_PIN_GATE_SELECT(x)	(0x202 + (x))

/*
 * Possibilities for setting a source with
 * INSN_CONFIG_SET_OTHER_SRC when using NI general-purpose counters.
 */
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

#define NI_GPCT_PFI_OTHER_SELECT(x)	NI_USUAL_PFI_SELECT(x)

/*
 * start sources for ni general-purpose counters for use with
 * INSN_CONFIG_ARM
 */
enum ni_gpct_arm_source {
	NI_GPCT_ARM_IMMEDIATE = 0x0,
	/*
	 * Start both the counter and the adjacent paired counter simultaneously
	 */
	NI_GPCT_ARM_PAIRED_IMMEDIATE = 0x1,
	/*
	 * If the NI_GPCT_HW_ARM bit is set, we will pass the least significant
	 * bits (3 bits for 660x or 5 bits for m-series) through to the
	 * hardware. To select a hardware trigger, pass the appropriate select
	 * bit, e.g.,
	 * NI_GPCT_HW_ARM | NI_GPCT_AI_START1_GATE_SELECT or
	 * NI_GPCT_HW_ARM | NI_GPCT_PFI_GATE_SELECT(pfi_number)
	 */
	NI_GPCT_HW_ARM = 0x1000,
	NI_GPCT_ARM_UNKNOWN = NI_GPCT_HW_ARM,	/* for backward compatibility */
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

/*
 * PFI digital filtering options for ni m-series for use with
 * INSN_CONFIG_FILTER.
 */
enum ni_pfi_filter_select {
	NI_PFI_FILTER_OFF = 0x0,
	NI_PFI_FILTER_125ns = 0x1,
	NI_PFI_FILTER_6425ns = 0x2,
	NI_PFI_FILTER_2550us = 0x3
};

/* master clock sources for ni mio boards and INSN_CONFIG_SET_CLOCK_SRC */
enum ni_mio_clock_source {
	NI_MIO_INTERNAL_CLOCK = 0,
	/*
	 * Doesn't work for m-series, use NI_MIO_PLL_RTSI_CLOCK()
	 * the NI_MIO_PLL_* sources are m-series only
	 */
	NI_MIO_RTSI_CLOCK = 1,
	NI_MIO_PLL_PXI_STAR_TRIGGER_CLOCK = 2,
	NI_MIO_PLL_PXI10_CLOCK = 3,
	NI_MIO_PLL_RTSI0_CLOCK = 4
};

#define NI_MIO_PLL_RTSI_CLOCK(x)	(NI_MIO_PLL_RTSI0_CLOCK + (x))

/*
 * Signals which can be routed to an NI RTSI pin with INSN_CONFIG_SET_ROUTING.
 * The numbers assigned are not arbitrary, they correspond to the bits required
 * to program the board.
 */
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
	/* Pre-m-series always have RTSI clock on line 7 */
	NI_RTSI_OUTPUT_RTSI_OSC = 12
};

#define NI_RTSI_OUTPUT_RTSI_BRD(x)	(NI_RTSI_OUTPUT_RTSI_BRD_0 + (x))

/*
 * Signals which can be routed to an NI PFI pin on an m-series board with
 * INSN_CONFIG_SET_ROUTING.  These numbers are also returned by
 * INSN_CONFIG_GET_ROUTING on pre-m-series boards, even though their routing
 * cannot be changed.  The numbers assigned are not arbitrary, they correspond
 * to the bits required to program the board.
 */
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

#define NI_PFI_OUTPUT_RTSI(x)		(NI_PFI_OUTPUT_RTSI0 + (x))

/*
 * Signals which can be routed to output on a NI PFI pin on a 660x board
 * with INSN_CONFIG_SET_ROUTING.  The numbers assigned are
 * not arbitrary, they correspond to the bits required
 * to program the board.  Lines 0 to 7 can only be set to
 * NI_660X_PFI_OUTPUT_DIO.  Lines 32 to 39 can only be set to
 * NI_660X_PFI_OUTPUT_COUNTER.
 */
enum ni_660x_pfi_routing {
	NI_660X_PFI_OUTPUT_COUNTER = 1,	/* counter */
	NI_660X_PFI_OUTPUT_DIO = 2,	/* static digital output */
};

/*
 * NI External Trigger lines.  These values are not arbitrary, but are related
 * to the bits required to program the board (offset by 1 for historical
 * reasons).
 */
#define NI_EXT_PFI(x)			(NI_USUAL_PFI_SELECT(x) - 1)
#define NI_EXT_RTSI(x)			(NI_USUAL_RTSI_SELECT(x) - 1)

/*
 * Clock sources for CDIO subdevice on NI m-series boards.  Used as the
 * scan_begin_arg for a comedi_command. These sources may also be bitwise-or'd
 * with CR_INVERT to change polarity.
 */
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

#define NI_CDIO_SCAN_BEGIN_SRC_PFI(x)	NI_USUAL_PFI_SELECT(x)
#define NI_CDIO_SCAN_BEGIN_SRC_RTSI(x)	NI_USUAL_RTSI_SELECT(x)

/*
 * scan_begin_src for scan_begin_arg==TRIG_EXT with analog output command on NI
 * boards.  These scan begin sources can also be bitwise-or'd with CR_INVERT to
 * change polarity.
 */
#define NI_AO_SCAN_BEGIN_SRC_PFI(x)	NI_USUAL_PFI_SELECT(x)
#define NI_AO_SCAN_BEGIN_SRC_RTSI(x)	NI_USUAL_RTSI_SELECT(x)

/*
 * Bits for setting a clock source with
 * INSN_CONFIG_SET_CLOCK_SRC when using NI frequency output subdevice.
 */
enum ni_freq_out_clock_source_bits {
	NI_FREQ_OUT_TIMEBASE_1_DIV_2_CLOCK_SRC,	/* 10 MHz */
	NI_FREQ_OUT_TIMEBASE_2_CLOCK_SRC	/* 100 KHz */
};

/*
 * Values for setting a clock source with INSN_CONFIG_SET_CLOCK_SRC for
 * 8254 counter subdevices on Amplicon DIO boards (amplc_dio200 driver).
 */
enum amplc_dio_clock_source {
	/*
	 * Per channel external clock
	 * input/output pin (pin is only an
	 * input when clock source set to this value,
	 * otherwise it is an output)
	 */
	AMPLC_DIO_CLK_CLKN,
	AMPLC_DIO_CLK_10MHZ,	/* 10 MHz internal clock */
	AMPLC_DIO_CLK_1MHZ,	/* 1 MHz internal clock */
	AMPLC_DIO_CLK_100KHZ,	/* 100 kHz internal clock */
	AMPLC_DIO_CLK_10KHZ,	/* 10 kHz internal clock */
	AMPLC_DIO_CLK_1KHZ,	/* 1 kHz internal clock */
	/*
	 * Output of preceding counter channel
	 * (for channel 0, preceding counter
	 * channel is channel 2 on preceding
	 * counter subdevice, for first counter
	 * subdevice, preceding counter
	 * subdevice is the last counter
	 * subdevice)
	 */
	AMPLC_DIO_CLK_OUTNM1,
	AMPLC_DIO_CLK_EXT,	/* per chip external input pin */
	/* the following are "enhanced" clock sources for PCIe models */
	AMPLC_DIO_CLK_VCC,	/* clock input HIGH */
	AMPLC_DIO_CLK_GND,	/* clock input LOW */
	AMPLC_DIO_CLK_PAT_PRESENT, /* "pattern present" signal */
	AMPLC_DIO_CLK_20MHZ	/* 20 MHz internal clock */
};

/*
 * Values for setting a clock source with INSN_CONFIG_SET_CLOCK_SRC for
 * timer subdevice on some Amplicon DIO PCIe boards (amplc_dio200 driver).
 */
enum amplc_dio_ts_clock_src {
	AMPLC_DIO_TS_CLK_1GHZ,	/* 1 ns period with 20 ns granularity */
	AMPLC_DIO_TS_CLK_1MHZ,	/* 1 us period */
	AMPLC_DIO_TS_CLK_1KHZ	/* 1 ms period */
};

/*
 * Values for setting a gate source with INSN_CONFIG_SET_GATE_SRC for
 * 8254 counter subdevices on Amplicon DIO boards (amplc_dio200 driver).
 */
enum amplc_dio_gate_source {
	AMPLC_DIO_GAT_VCC,	/* internal high logic level */
	AMPLC_DIO_GAT_GND,	/* internal low logic level */
	AMPLC_DIO_GAT_GATN,	/* per channel external gate input */
	/*
	 * negated output of counter channel minus 2
	 * (for channels 0 or 1, channel minus 2 is channel 1 or 2 on
	 * the preceding counter subdevice, for the first counter subdevice
	 * the preceding counter subdevice is the last counter subdevice)
	 */
	AMPLC_DIO_GAT_NOUTNM2,
	AMPLC_DIO_GAT_RESERVED4,
	AMPLC_DIO_GAT_RESERVED5,
	AMPLC_DIO_GAT_RESERVED6,
	AMPLC_DIO_GAT_RESERVED7,
	/* the following are "enhanced" gate sources for PCIe models */
	AMPLC_DIO_GAT_NGATN = 6, /* negated per channel gate input */
	/* non-negated output of counter channel minus 2 */
	AMPLC_DIO_GAT_OUTNM2,
	AMPLC_DIO_GAT_PAT_PRESENT, /* "pattern present" signal */
	AMPLC_DIO_GAT_PAT_OCCURRED, /* "pattern occurred" latched */
	AMPLC_DIO_GAT_PAT_GONE,	/* "pattern gone away" latched */
	AMPLC_DIO_GAT_NPAT_PRESENT, /* negated "pattern present" */
	AMPLC_DIO_GAT_NPAT_OCCURRED, /* negated "pattern occurred" */
	AMPLC_DIO_GAT_NPAT_GONE	/* negated "pattern gone away" */
};

/*
 * Values for setting a clock source with INSN_CONFIG_SET_CLOCK_SRC for
 * the counter subdevice on the Kolter Electronic PCI-Counter board
 * (ke_counter driver).
 */
enum ke_counter_clock_source {
	KE_CLK_20MHZ,	/* internal 20MHz (default) */
	KE_CLK_4MHZ,	/* internal 4MHz (option) */
	KE_CLK_EXT	/* external clock on pin 21 of D-Sub */
};

#endif /* _COMEDI_H */
