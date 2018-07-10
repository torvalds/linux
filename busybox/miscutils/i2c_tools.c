/* vi: set sw=4 ts=4: */
/*
 * Minimal i2c-tools implementation for busybox.
 * Parts of code ported from i2c-tools:
 * 		http://www.lm-sensors.org/wiki/I2CTools.
 *
 * Copyright (C) 2014 by Bartosz Golaszewski <bartekgola@gmail.com>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config I2CGET
//config:	bool "i2cget (5.6 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Read from I2C/SMBus chip registers.
//config:
//config:config I2CSET
//config:	bool "i2cset (6.9 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Set I2C registers.
//config:
//config:config I2CDUMP
//config:	bool "i2cdump (7.2 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Examine I2C registers.
//config:
//config:config I2CDETECT
//config:	bool "i2cdetect (7.2 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Detect I2C chips.
//config:

//applet:IF_I2CGET(APPLET(i2cget, BB_DIR_USR_SBIN, BB_SUID_DROP))
//applet:IF_I2CSET(APPLET(i2cset, BB_DIR_USR_SBIN, BB_SUID_DROP))
//applet:IF_I2CDUMP(APPLET(i2cdump, BB_DIR_USR_SBIN, BB_SUID_DROP))
//applet:IF_I2CDETECT(APPLET(i2cdetect, BB_DIR_USR_SBIN, BB_SUID_DROP))
/* not NOEXEC: if hw operation stalls, use less memory in "hung" process */

//kbuild:lib-$(CONFIG_I2CGET) += i2c_tools.o
//kbuild:lib-$(CONFIG_I2CSET) += i2c_tools.o
//kbuild:lib-$(CONFIG_I2CDUMP) += i2c_tools.o
//kbuild:lib-$(CONFIG_I2CDETECT) += i2c_tools.o

/*
 * Unsupported stuff:
 *
 * - upstream i2c-tools can also look-up i2c busses by name, we only accept
 *   numbers,
 * - bank and bankreg parameters for i2cdump are not supported because of
 *   their limited usefulness (see i2cdump manual entry for more info),
 * - i2cdetect doesn't look for bus info in /proc as it does in upstream, but
 *   it shouldn't be a problem in modern kernels.
 */

#include "libbb.h"

#include <linux/i2c.h>

#define I2CDUMP_NUM_REGS		256

#define I2CDETECT_MODE_AUTO		0
#define I2CDETECT_MODE_QUICK		1
#define I2CDETECT_MODE_READ		2

/* linux/i2c-dev.h from i2c-tools overwrites the one from linux uapi
 * and defines symbols already defined by linux/i2c.h.
 * Also, it defines a bunch of static inlines which we would rather NOT
 * inline. What a mess.
 * We need only these definitions from linux/i2c-dev.h:
 */
#define I2C_SLAVE			0x0703
#define I2C_SLAVE_FORCE			0x0706
#define I2C_FUNCS			0x0705
#define I2C_PEC				0x0708
#define I2C_SMBUS			0x0720
struct i2c_smbus_ioctl_data {
	__u8 read_write;
	__u8 command;
	__u32 size;
	union i2c_smbus_data *data;
};
/* end linux/i2c-dev.h */

/*
 * This is needed for ioctl_or_perror_and_die() since it only accepts pointers.
 */
static ALWAYS_INLINE void *itoptr(int i)
{
	return (void*)(intptr_t)i;
}

static int32_t i2c_smbus_access(int fd, char read_write, uint8_t cmd,
				int size, union i2c_smbus_data *data)
{
	struct i2c_smbus_ioctl_data args;

	args.read_write = read_write;
	args.command = cmd;
	args.size = size;
	args.data = data;

	return ioctl(fd, I2C_SMBUS, &args);
}

static int32_t i2c_smbus_read_byte(int fd)
{
	union i2c_smbus_data data;
	int err;

	err = i2c_smbus_access(fd, I2C_SMBUS_READ, 0, I2C_SMBUS_BYTE, &data);
	if (err < 0)
		return err;

	return data.byte;
}

#if ENABLE_I2CGET || ENABLE_I2CSET || ENABLE_I2CDUMP
static int32_t i2c_smbus_write_byte(int fd, uint8_t val)
{
	return i2c_smbus_access(fd, I2C_SMBUS_WRITE,
				val, I2C_SMBUS_BYTE, NULL);
}

static int32_t i2c_smbus_read_byte_data(int fd, uint8_t cmd)
{
	union i2c_smbus_data data;
	int err;

	err = i2c_smbus_access(fd, I2C_SMBUS_READ, cmd,
			       I2C_SMBUS_BYTE_DATA, &data);
	if (err < 0)
		return err;

	return data.byte;
}

static int32_t i2c_smbus_read_word_data(int fd, uint8_t cmd)
{
	union i2c_smbus_data data;
	int err;

	err = i2c_smbus_access(fd, I2C_SMBUS_READ, cmd,
			       I2C_SMBUS_WORD_DATA, &data);
	if (err < 0)
		return err;

	return data.word;
}
#endif /* ENABLE_I2CGET || ENABLE_I2CSET || ENABLE_I2CDUMP */

#if ENABLE_I2CSET
static int32_t i2c_smbus_write_byte_data(int file,
					 uint8_t cmd, uint8_t value)
{
	union i2c_smbus_data data;

	data.byte = value;

	return i2c_smbus_access(file, I2C_SMBUS_WRITE, cmd,
				I2C_SMBUS_BYTE_DATA, &data);
}

static int32_t i2c_smbus_write_word_data(int file, uint8_t cmd, uint16_t value)
{
	union i2c_smbus_data data;

	data.word = value;

	return i2c_smbus_access(file, I2C_SMBUS_WRITE, cmd,
				I2C_SMBUS_WORD_DATA, &data);
}

static int32_t i2c_smbus_write_block_data(int file, uint8_t cmd,
				   uint8_t length, const uint8_t *values)
{
	union i2c_smbus_data data;

	if (length > I2C_SMBUS_BLOCK_MAX)
		length = I2C_SMBUS_BLOCK_MAX;

	memcpy(data.block+1, values, length);
	data.block[0] = length;

	return i2c_smbus_access(file, I2C_SMBUS_WRITE, cmd,
				I2C_SMBUS_BLOCK_DATA, &data);
}

static int32_t i2c_smbus_write_i2c_block_data(int file, uint8_t cmd,
				       uint8_t length, const uint8_t *values)
{
	union i2c_smbus_data data;

	if (length > I2C_SMBUS_BLOCK_MAX)
		length = I2C_SMBUS_BLOCK_MAX;

	memcpy(data.block+1, values, length);
	data.block[0] = length;

	return i2c_smbus_access(file, I2C_SMBUS_WRITE, cmd,
				I2C_SMBUS_I2C_BLOCK_BROKEN, &data);
}
#endif /* ENABLE_I2CSET */

#if ENABLE_I2CDUMP
/*
 * Returns the number of bytes read, vals must hold at
 * least I2C_SMBUS_BLOCK_MAX bytes.
 */
static int32_t i2c_smbus_read_block_data(int fd, uint8_t cmd, uint8_t *vals)
{
	union i2c_smbus_data data;
	int i, err;

	err = i2c_smbus_access(fd, I2C_SMBUS_READ, cmd,
			       I2C_SMBUS_BLOCK_DATA, &data);
	if (err < 0)
		return err;

	for (i = 1; i <= data.block[0]; i++)
		*vals++ = data.block[i];
	return data.block[0];
}

static int32_t i2c_smbus_read_i2c_block_data(int fd, uint8_t cmd,
					     uint8_t len, uint8_t *vals)
{
	union i2c_smbus_data data;
	int i, err;

	if (len > I2C_SMBUS_BLOCK_MAX)
		len = I2C_SMBUS_BLOCK_MAX;
	data.block[0] = len;

	err = i2c_smbus_access(fd, I2C_SMBUS_READ, cmd,
			       len == 32 ? I2C_SMBUS_I2C_BLOCK_BROKEN :
					   I2C_SMBUS_I2C_BLOCK_DATA, &data);
	if (err < 0)
		return err;

	for (i = 1; i <= data.block[0]; i++)
		*vals++ = data.block[i];
	return data.block[0];
}
#endif /* ENABLE_I2CDUMP */

#if ENABLE_I2CDETECT
static int32_t i2c_smbus_write_quick(int fd, uint8_t val)
{
	return i2c_smbus_access(fd, val, 0, I2C_SMBUS_QUICK, NULL);
}
#endif /* ENABLE_I2CDETECT */

static int i2c_bus_lookup(const char *bus_str)
{
	return xstrtou_range(bus_str, 10, 0, 0xfffff);
}

#if ENABLE_I2CGET || ENABLE_I2CSET || ENABLE_I2CDUMP
static int i2c_parse_bus_addr(const char *addr_str)
{
	/* Slave address must be in range 0x03 - 0x77. */
	return xstrtou_range(addr_str, 16, 0x03, 0x77);
}

static void i2c_set_pec(int fd, int pec)
{
	ioctl_or_perror_and_die(fd, I2C_PEC,
				itoptr(pec ? 1 : 0),
				"can't set PEC");
}

static void i2c_set_slave_addr(int fd, int addr, int force)
{
	ioctl_or_perror_and_die(fd, force ? I2C_SLAVE_FORCE : I2C_SLAVE,
				itoptr(addr),
				"can't set address to 0x%02x", addr);
}
#endif /* ENABLE_I2CGET || ENABLE_I2CSET || ENABLE_I2CDUMP */

#if ENABLE_I2CGET || ENABLE_I2CSET
static int i2c_parse_data_addr(const char *data_addr)
{
	/* Data address must be an 8 bit integer. */
	return xstrtou_range(data_addr, 16, 0, 0xff);
}
#endif /* ENABLE_I2CGET || ENABLE_I2CSET */

/*
 * Opens the device file associated with given i2c bus.
 *
 * Upstream i2c-tools also support opening devices by i2c bus name
 * but we drop it here for size reduction.
 */
static int i2c_dev_open(int i2cbus)
{
	char filename[sizeof("/dev/i2c-%d") + sizeof(int)*3];
	int fd;

	sprintf(filename, "/dev/i2c-%d", i2cbus);
	fd = open(filename, O_RDWR);
	if (fd < 0) {
		if (errno == ENOENT) {
			filename[8] = '/'; /* change to "/dev/i2c/%d" */
			fd = xopen(filename, O_RDWR);
		} else {
			bb_perror_msg_and_die("can't open '%s'", filename);
		}
	}

	return fd;
}

/* Size reducing helpers for xxx_check_funcs(). */
static void get_funcs_matrix(int fd, unsigned long *funcs)
{
	ioctl_or_perror_and_die(fd, I2C_FUNCS, funcs,
			"can't get adapter functionality matrix");
}

#if ENABLE_I2CGET || ENABLE_I2CSET || ENABLE_I2CDUMP
static void check_funcs_test_end(int funcs, int pec, const char *err)
{
	if (pec && !(funcs & (I2C_FUNC_SMBUS_PEC | I2C_FUNC_I2C)))
		bb_error_msg("warning: adapter does not support PEC");

	if (err)
		bb_error_msg_and_die(
			"adapter has no %s capability", err);
}
#endif /* ENABLE_I2CGET || ENABLE_I2CSET || ENABLE_I2CDUMP */

/*
 * The below functions emit an error message and exit if the adapter doesn't
 * support desired functionalities.
 */
#if ENABLE_I2CGET || ENABLE_I2CDUMP
static void check_read_funcs(int fd, int mode, int data_addr, int pec)
{
	unsigned long funcs;
	const char *err = NULL;

	get_funcs_matrix(fd, &funcs);
	switch (mode) {
	case I2C_SMBUS_BYTE:
		if (!(funcs & I2C_FUNC_SMBUS_READ_BYTE)) {
			err = "SMBus receive byte";
			break;
		}
		if (data_addr >= 0 && !(funcs & I2C_FUNC_SMBUS_WRITE_BYTE))
			err = "SMBus send byte";
		break;
	case I2C_SMBUS_BYTE_DATA:
		if (!(funcs & I2C_FUNC_SMBUS_READ_BYTE_DATA))
			err = "SMBus read byte";
		break;
	case I2C_SMBUS_WORD_DATA:
		if (!(funcs & I2C_FUNC_SMBUS_READ_WORD_DATA))
			err = "SMBus read word";
		break;
#if ENABLE_I2CDUMP
	case I2C_SMBUS_BLOCK_DATA:
		if (!(funcs & I2C_FUNC_SMBUS_READ_BLOCK_DATA))
			err = "SMBus block read";
		break;

	case I2C_SMBUS_I2C_BLOCK_DATA:
		if (!(funcs & I2C_FUNC_SMBUS_READ_I2C_BLOCK))
			err = "I2C block read";
		break;
#endif /* ENABLE_I2CDUMP */
	default:
		bb_error_msg_and_die("internal error");
	}
	check_funcs_test_end(funcs, pec, err);
}
#endif /* ENABLE_I2CGET || ENABLE_I2CDUMP */

#if ENABLE_I2CSET
static void check_write_funcs(int fd, int mode, int pec)
{
	unsigned long funcs;
	const char *err = NULL;

	get_funcs_matrix(fd, &funcs);
	switch (mode) {
	case I2C_SMBUS_BYTE:
		if (!(funcs & I2C_FUNC_SMBUS_WRITE_BYTE))
			err = "SMBus send byte";
		break;

	case I2C_SMBUS_BYTE_DATA:
		if (!(funcs & I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
			err = "SMBus write byte";
		break;

	case I2C_SMBUS_WORD_DATA:
		if (!(funcs & I2C_FUNC_SMBUS_WRITE_WORD_DATA))
			err = "SMBus write word";
		break;

	case I2C_SMBUS_BLOCK_DATA:
		if (!(funcs & I2C_FUNC_SMBUS_WRITE_BLOCK_DATA))
			err = "SMBus block write";
		break;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		if (!(funcs & I2C_FUNC_SMBUS_WRITE_I2C_BLOCK))
			err = "I2C block write";
		break;
	}
	check_funcs_test_end(funcs, pec, err);
}
#endif /* ENABLE_I2CSET */

static void confirm_or_abort(void)
{
	fprintf(stderr, "Continue? [y/N] ");
	if (!bb_ask_y_confirmation())
		bb_error_msg_and_die("aborting");
}

/*
 * Return only if user confirms the action, abort otherwise.
 *
 * The messages displayed here are much less elaborate than their i2c-tools
 * counterparts - this is done for size reduction.
 */
static void confirm_action(int bus_addr, int mode, int data_addr, int pec)
{
	bb_error_msg("WARNING! This program can confuse your I2C bus");

	/* Don't let the user break his/her EEPROMs */
	if (bus_addr >= 0x50 && bus_addr <= 0x57 && pec) {
		bb_error_msg_and_die("this is I2C not smbus - using PEC on I2C "
			"devices may result in data loss, aborting");
	}

	if (mode == I2C_SMBUS_BYTE && data_addr >= 0 && pec)
		bb_error_msg("WARNING! May interpret a write byte command "
			"with PEC as a write byte data command");

	if (pec)
		bb_error_msg("PEC checking enabled");

	confirm_or_abort();
}

#if ENABLE_I2CGET
//usage:#define i2cget_trivial_usage
//usage:       "[-fy] BUS CHIP-ADDRESS [DATA-ADDRESS [MODE]]"
//usage:#define i2cget_full_usage "\n\n"
//usage:       "Read from I2C/SMBus chip registers"
//usage:     "\n"
//usage:     "\n	I2CBUS	I2C bus number"
//usage:     "\n	ADDRESS	0x03-0x77"
//usage:     "\nMODE is:"
//usage:     "\n	b	Read byte data (default)"
//usage:     "\n	w	Read word data"
//usage:     "\n	c	Write byte/read byte"
//usage:     "\n	Append p for SMBus PEC"
//usage:     "\n"
//usage:     "\n	-f	Force access"
//usage:     "\n	-y	Disable interactive mode"
int i2cget_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int i2cget_main(int argc UNUSED_PARAM, char **argv)
{
	const unsigned opt_f = (1 << 0), opt_y = (1 << 1);

	int bus_num, bus_addr, data_addr = -1, status;
	int mode = I2C_SMBUS_BYTE, pec = 0, fd;
	unsigned opts;

	opts = getopt32(argv, "^" "fy" "\0" "-2:?4"/*from 2 to 4 args*/);
	argv += optind;

	bus_num = i2c_bus_lookup(argv[0]);
	bus_addr = i2c_parse_bus_addr(argv[1]);

	if (argv[2]) {
		data_addr = i2c_parse_data_addr(argv[2]);
		mode = I2C_SMBUS_BYTE_DATA;
		if (argv[3]) {
			switch (argv[3][0]) {
			case 'b':	/* Already set */		break;
			case 'w':	mode = I2C_SMBUS_WORD_DATA;	break;
			case 'c':	mode = I2C_SMBUS_BYTE;		break;
			default:
				bb_error_msg("invalid mode");
				bb_show_usage();
			}
			pec = argv[3][1] == 'p';
		}
	}

	fd = i2c_dev_open(bus_num);
	check_read_funcs(fd, mode, data_addr, pec);
	i2c_set_slave_addr(fd, bus_addr, opts & opt_f);

	if (!(opts & opt_y))
		confirm_action(bus_addr, mode, data_addr, pec);

	if (pec)
		i2c_set_pec(fd, 1);

	switch (mode) {
	case I2C_SMBUS_BYTE:
		if (data_addr >= 0) {
			status = i2c_smbus_write_byte(fd, data_addr);
			if (status < 0)
				bb_error_msg("warning - write failed");
		}
		status = i2c_smbus_read_byte(fd);
		break;
	case I2C_SMBUS_WORD_DATA:
		status = i2c_smbus_read_word_data(fd, data_addr);
		break;
	default: /* I2C_SMBUS_BYTE_DATA */
		status = i2c_smbus_read_byte_data(fd, data_addr);
	}
	close(fd);

	if (status < 0)
		bb_perror_msg_and_die("read failed");

	printf("0x%0*x\n", mode == I2C_SMBUS_WORD_DATA ? 4 : 2, status);

	return 0;
}
#endif /* ENABLE_I2CGET */

#if ENABLE_I2CSET
//usage:#define i2cset_trivial_usage
//usage:       "[-fy] [-m MASK] BUS CHIP-ADDRESS DATA-ADDRESS [VALUE] ... [MODE]"
//usage:#define i2cset_full_usage "\n\n"
//usage:       "Set I2C registers"
//usage:     "\n"
//usage:     "\n	I2CBUS	I2C bus number"
//usage:     "\n	ADDRESS	0x03-0x77"
//usage:     "\nMODE is:"
//usage:     "\n	c	Byte, no value"
//usage:     "\n	b	Byte data (default)"
//usage:     "\n	w	Word data"
//usage:     "\n	i	I2C block data"
//usage:     "\n	s	SMBus block data"
//usage:     "\n	Append p for SMBus PEC"
//usage:     "\n"
//usage:     "\n	-f	Force access"
//usage:     "\n	-y	Disable interactive mode"
//usage:     "\n	-r	Read back and compare the result"
//usage:     "\n	-m MASK	Mask specifying which bits to write"
int i2cset_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int i2cset_main(int argc, char **argv)
{
	const unsigned opt_f = (1 << 0), opt_y = (1 << 1),
			      opt_m = (1 << 2), opt_r = (1 << 3);

	int bus_num, bus_addr, data_addr, mode = I2C_SMBUS_BYTE, pec = 0;
	int val, blen, mask, fd, status;
	unsigned char block[I2C_SMBUS_BLOCK_MAX];
	char *opt_m_arg = NULL;
	unsigned opts;

	opts = getopt32(argv, "^"
		"fym:r"
		"\0" "-3", /* minimum 3 args */
		&opt_m_arg
	);
	argv += optind;
	argc -= optind;
	argc--; /* now argv[argc] is last arg */

	bus_num = i2c_bus_lookup(argv[0]);
	bus_addr = i2c_parse_bus_addr(argv[1]);
	data_addr = i2c_parse_data_addr(argv[2]);

	if (argv[3]) {
		if (!argv[4] && argv[3][0] != 'c') {
			mode = I2C_SMBUS_BYTE_DATA; /* Implicit b */
		} else {
			switch (argv[argc][0]) {
			case 'c': /* Already set */
				break;
			case 'b': mode = I2C_SMBUS_BYTE_DATA;
				break;
			case 'w': mode = I2C_SMBUS_WORD_DATA;
				break;
			case 's': mode = I2C_SMBUS_BLOCK_DATA;
				break;
			case 'i': mode = I2C_SMBUS_I2C_BLOCK_DATA;
				break;
			default:
				bb_error_msg("invalid mode");
				bb_show_usage();
			}

			pec = (argv[argc][1] == 'p');
			if (mode == I2C_SMBUS_BLOCK_DATA
			 || mode == I2C_SMBUS_I2C_BLOCK_DATA
			) {
				if (pec && mode == I2C_SMBUS_I2C_BLOCK_DATA)
					bb_error_msg_and_die(
						"PEC not supported for I2C "
						"block writes");
				if (opts & opt_m)
					bb_error_msg_and_die(
						"mask not supported for block "
						"writes");
			}
		}
	}

	/* Prepare the value(s) to be written according to current mode. */
	mask = 0;
	blen = 0;
	switch (mode) {
	case I2C_SMBUS_BYTE_DATA:
		val = xstrtou_range(argv[3], 0, 0, 0xff);
		break;
	case I2C_SMBUS_WORD_DATA:
		val = xstrtou_range(argv[3], 0, 0, 0xffff);
		break;
	case I2C_SMBUS_BLOCK_DATA:
	case I2C_SMBUS_I2C_BLOCK_DATA:
		for (blen = 3; blen < argc; blen++)
			block[blen - 3] = xstrtou_range(argv[blen], 0, 0, 0xff);
		blen -= 3;
		val = -1;
		break;
	default:
		val = -1;
		break;
	}

	if (opts & opt_m) {
		mask = xstrtou_range(opt_m_arg, 0, 0,
				(mode == I2C_SMBUS_BYTE ||
				 mode == I2C_SMBUS_BYTE_DATA) ? 0xff : 0xffff);
	}

	fd = i2c_dev_open(bus_num);
	check_write_funcs(fd, mode, pec);
	i2c_set_slave_addr(fd, bus_addr, opts & opt_f);

	if (!(opts & opt_y))
		confirm_action(bus_addr, mode, data_addr, pec);

	/*
	 * If we're using mask - read the current value here and adjust the
	 * value to be written.
	 */
	if (opts & opt_m) {
		int tmpval;

		switch (mode) {
		case I2C_SMBUS_BYTE:
			tmpval = i2c_smbus_read_byte(fd);
			break;
		case I2C_SMBUS_WORD_DATA:
			tmpval = i2c_smbus_read_word_data(fd, data_addr);
			break;
		default:
			tmpval = i2c_smbus_read_byte_data(fd, data_addr);
		}

		if (tmpval < 0)
			bb_perror_msg_and_die("can't read old value");

		val = (val & mask) | (tmpval & ~mask);

		if (!(opts & opt_y)) {
			bb_error_msg("old value 0x%0*x, write mask "
				"0x%0*x, will write 0x%0*x to register "
				"0x%02x",
				mode == I2C_SMBUS_WORD_DATA ? 4 : 2, tmpval,
				mode == I2C_SMBUS_WORD_DATA ? 4 : 2, mask,
				mode == I2C_SMBUS_WORD_DATA ? 4 : 2, val,
				data_addr);
			confirm_or_abort();
		}
	}

	if (pec)
		i2c_set_pec(fd, 1);

	switch (mode) {
	case I2C_SMBUS_BYTE:
		status = i2c_smbus_write_byte(fd, data_addr);
		break;
	case I2C_SMBUS_WORD_DATA:
		status = i2c_smbus_write_word_data(fd, data_addr, val);
		break;
	case I2C_SMBUS_BLOCK_DATA:
		status = i2c_smbus_write_block_data(fd, data_addr,
						    blen, block);
		break;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		status = i2c_smbus_write_i2c_block_data(fd, data_addr,
							blen, block);
		break;
	default: /* I2C_SMBUS_BYTE_DATA */
		status = i2c_smbus_write_byte_data(fd, data_addr, val);
		break;
	}
	if (status < 0)
		bb_perror_msg_and_die("write failed");

	if (pec)
		i2c_set_pec(fd, 0); /* Clear PEC. */

	/* No readback required - we're done. */
	if (!(opts & opt_r))
		return 0;

	switch (mode) {
	case I2C_SMBUS_BYTE:
		status = i2c_smbus_read_byte(fd);
		val = data_addr;
		break;
	case I2C_SMBUS_WORD_DATA:
		status = i2c_smbus_read_word_data(fd, data_addr);
		break;
	default: /* I2C_SMBUS_BYTE_DATA */
		status = i2c_smbus_read_byte_data(fd, data_addr);
	}

	if (status < 0) {
		puts("Warning - readback failed");
	} else
	if (status != val) {
		printf("Warning - data mismatch - wrote "
		       "0x%0*x, read back 0x%0*x\n",
		       mode == I2C_SMBUS_WORD_DATA ? 4 : 2, val,
		       mode == I2C_SMBUS_WORD_DATA ? 4 : 2, status);
	} else {
		printf("Value 0x%0*x written, readback matched\n",
		       mode == I2C_SMBUS_WORD_DATA ? 4 : 2, val);
	}

	return 0;
}
#endif /* ENABLE_I2CSET */

#if ENABLE_I2CDUMP
static int read_block_data(int buf_fd, int mode, int *block)
{
	uint8_t cblock[I2C_SMBUS_BLOCK_MAX + I2CDUMP_NUM_REGS];
	int res, blen = 0, tmp, i;

	if (mode == I2C_SMBUS_BLOCK_DATA) {
		blen = i2c_smbus_read_block_data(buf_fd, 0, cblock);
		if (blen <= 0)
			goto fail;
	} else {
		for (res = 0; res < I2CDUMP_NUM_REGS; res += tmp) {
			tmp = i2c_smbus_read_i2c_block_data(
					buf_fd, res, I2C_SMBUS_BLOCK_MAX,
					cblock + res);
			if (tmp <= 0) {
				blen = tmp;
				goto fail;
			}
		}

		if (res >= I2CDUMP_NUM_REGS)
			res = I2CDUMP_NUM_REGS;

		for (i = 0; i < res; i++)
			block[i] = cblock[i];

		if (mode != I2C_SMBUS_BLOCK_DATA)
			for (i = res; i < I2CDUMP_NUM_REGS; i++)
				block[i] = -1;
	}

	return blen;

 fail:
	bb_error_msg_and_die("block read failed: %d", blen);
}

/* Dump all but word data. */
static void dump_data(int bus_fd, int mode, unsigned first,
		      unsigned last, int *block, int blen)
{
	int i, j, res;

	puts("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f"
	     "    0123456789abcdef");

	for (i = 0; i < I2CDUMP_NUM_REGS; i += 0x10) {
		if (mode == I2C_SMBUS_BLOCK_DATA && i >= blen)
			break;
		if (i/16 < first/16)
			continue;
		if (i/16 > last/16)
			break;

		printf("%02x: ", i);
		for (j = 0; j < 16; j++) {
			fflush_all();
			/* Skip unwanted registers */
			if (i+j < first || i+j > last) {
				printf("   ");
				if (mode == I2C_SMBUS_WORD_DATA) {
					printf("   ");
					j++;
				}
				continue;
			}

			switch (mode) {
			case I2C_SMBUS_BYTE_DATA:
				res = i2c_smbus_read_byte_data(bus_fd, i+j);
				block[i+j] = res;
				break;
			case I2C_SMBUS_WORD_DATA:
				res = i2c_smbus_read_word_data(bus_fd, i+j);
				if (res < 0) {
					block[i+j] = res;
					block[i+j+1] = res;
				} else {
					block[i+j] = res & 0xff;
					block[i+j+1] = res >> 8;
				}
				break;
			case I2C_SMBUS_BYTE:
				res = i2c_smbus_read_byte(bus_fd);
				block[i+j] = res;
				break;
			default:
				res = block[i+j];
			}

			if (mode == I2C_SMBUS_BLOCK_DATA &&
			    i+j >= blen) {
				printf("   ");
			} else if (res < 0) {
				printf("XX ");
				if (mode == I2C_SMBUS_WORD_DATA)
					printf("XX ");
			} else {
				printf("%02x ", block[i+j]);
				if (mode == I2C_SMBUS_WORD_DATA)
					printf("%02x ", block[i+j+1]);
			}

			if (mode == I2C_SMBUS_WORD_DATA)
				j++;
		}
		printf("   ");

		for (j = 0; j < 16; j++) {
			if (mode == I2C_SMBUS_BLOCK_DATA && i+j >= blen)
				break;
			/* Skip unwanted registers */
			if (i+j < first || i+j > last) {
				bb_putchar(' ');
				continue;
			}

			res = block[i+j];
			if (res < 0) {
				bb_putchar('X');
			} else if (res == 0x00 || res == 0xff) {
				bb_putchar('.');
			} else if (res < 32 || res >= 127) {
				bb_putchar('?');
			} else {
				bb_putchar(res);
			}
		}
		bb_putchar('\n');
	}
}

static void dump_word_data(int bus_fd, unsigned first, unsigned last)
{
	int i, j, rv;

	/* Word data. */
	puts("     0,8  1,9  2,a  3,b  4,c  5,d  6,e  7,f");
	for (i = 0; i < 256; i += 8) {
		if (i/8 < first/8)
			continue;
		if (i/8 > last/8)
			break;

		printf("%02x: ", i);
		for (j = 0; j < 8; j++) {
			/* Skip unwanted registers. */
			if (i+j < first || i+j > last) {
				printf("     ");
				continue;
			}

			rv = i2c_smbus_read_word_data(bus_fd, i+j);
			if (rv < 0)
				printf("XXXX ");
			else
				printf("%04x ", rv & 0xffff);
		}
		bb_putchar('\n');
	}
}

//usage:#define i2cdump_trivial_usage
//usage:       "[-fy] [-r FIRST-LAST] BUS ADDR [MODE]"
//usage:#define i2cdump_full_usage "\n\n"
//usage:       "Examine I2C registers"
//usage:     "\n"
//usage:     "\n	I2CBUS	I2C bus number"
//usage:     "\n	ADDRESS	0x03-0x77"
//usage:     "\nMODE is:"
//usage:     "\n	b	Byte (default)"
//usage:     "\n	w	Word"
//usage:     "\n	W	Word on even register addresses"
//usage:     "\n	i	I2C block"
//usage:     "\n	s	SMBus block"
//usage:     "\n	c	Consecutive byte"
//usage:     "\n	Append p for SMBus PEC"
//usage:     "\n"
//usage:     "\n	-f	Force access"
//usage:     "\n	-y	Disable interactive mode"
//usage:     "\n	-r	Limit the number of registers being accessed"
int i2cdump_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int i2cdump_main(int argc UNUSED_PARAM, char **argv)
{
	const unsigned opt_f = (1 << 0), opt_y = (1 << 1),
			      opt_r = (1 << 2);

	int bus_num, bus_addr, mode = I2C_SMBUS_BYTE_DATA, even = 0, pec = 0;
	unsigned first = 0x00, last = 0xff, opts;
	int block[I2CDUMP_NUM_REGS];
	char *opt_r_str, *dash;
	int fd, res;

	opts = getopt32(argv, "^"
		"fyr:"
		"\0" "-2:?3" /* from 2 to 3 args */,
		&opt_r_str
	);
	argv += optind;

	bus_num = i2c_bus_lookup(argv[0]);
	bus_addr = i2c_parse_bus_addr(argv[1]);

	if (argv[2]) {
		switch (argv[2][0]) {
		case 'b': /* Already set. */			break;
		case 'c': mode = I2C_SMBUS_BYTE;		break;
		case 'w': mode = I2C_SMBUS_WORD_DATA;		break;
		case 'W':
			mode = I2C_SMBUS_WORD_DATA;
			even = 1;
			break;
		case 's': mode = I2C_SMBUS_BLOCK_DATA;		break;
		case 'i': mode = I2C_SMBUS_I2C_BLOCK_DATA;	break;
		default:
			bb_error_msg_and_die("invalid mode");
		}

		if (argv[2][1] == 'p') {
			if (argv[2][0] == 'W' || argv[2][0] == 'i') {
				bb_error_msg_and_die(
					"pec not supported for -W and -i");
			} else {
				pec = 1;
			}
		}
	}

	if (opts & opt_r) {
		first = strtol(opt_r_str, &dash, 0);
		if (dash == opt_r_str || *dash != '-' || first > 0xff)
			bb_error_msg_and_die("invalid range");
		last = xstrtou_range(++dash, 0, first, 0xff);

		/* Range is not available for every mode. */
		switch (mode) {
		case I2C_SMBUS_BYTE:
		case I2C_SMBUS_BYTE_DATA:
			break;
		case I2C_SMBUS_WORD_DATA:
			if (!even || (!(first % 2) && last % 2))
				break;
			/* Fall through */
		default:
			bb_error_msg_and_die(
				"range not compatible with selected mode");
		}
	}

	fd = i2c_dev_open(bus_num);
	check_read_funcs(fd, mode, -1 /* data_addr */, pec);
	i2c_set_slave_addr(fd, bus_addr, opts & opt_f);

	if (pec)
		i2c_set_pec(fd, 1);

	if (!(opts & opt_y))
		confirm_action(bus_addr, mode, -1 /* data_addr */, pec);

	/* All but word data. */
	if (mode != I2C_SMBUS_WORD_DATA || even) {
		int blen = 0;

		if (mode == I2C_SMBUS_BLOCK_DATA || mode == I2C_SMBUS_I2C_BLOCK_DATA)
			blen = read_block_data(fd, mode, block);

		if (mode == I2C_SMBUS_BYTE) {
			res = i2c_smbus_write_byte(fd, first);
			if (res < 0)
				bb_perror_msg_and_die("write start address");
		}

		dump_data(fd, mode, first, last, block, blen);
	} else {
		dump_word_data(fd, first, last);
	}

	return 0;
}
#endif /* ENABLE_I2CDUMP */

#if ENABLE_I2CDETECT
enum adapter_type {
	ADT_DUMMY = 0,
	ADT_ISA,
	ADT_I2C,
	ADT_SMBUS,
};

struct adap_desc {
	const char *funcs;
	const char *algo;
};

static const struct adap_desc adap_descs[] = {
	{ .funcs	= "dummy",
	  .algo		= "Dummy bus", },
	{ .funcs	= "isa",
	  .algo		= "ISA bus", },
	{ .funcs	= "i2c",
	  .algo		= "I2C adapter", },
	{ .funcs	= "smbus",
	  .algo		= "SMBus adapter", },
};

struct i2c_func
{
	long value;
	const char* name;
};

static const struct i2c_func i2c_funcs_tab[] = {
	{ .value = I2C_FUNC_I2C,
	  .name = "I2C" },
	{ .value = I2C_FUNC_SMBUS_QUICK,
	  .name = "SMBus quick command" },
	{ .value = I2C_FUNC_SMBUS_WRITE_BYTE,
	  .name = "SMBus send byte" },
	{ .value = I2C_FUNC_SMBUS_READ_BYTE,
	  .name = "SMBus receive byte" },
	{ .value = I2C_FUNC_SMBUS_WRITE_BYTE_DATA,
	  .name = "SMBus write byte" },
	{ .value = I2C_FUNC_SMBUS_READ_BYTE_DATA,
	  .name = "SMBus read byte" },
	{ .value = I2C_FUNC_SMBUS_WRITE_WORD_DATA,
	  .name = "SMBus write word" },
	{ .value = I2C_FUNC_SMBUS_READ_WORD_DATA,
	  .name = "SMBus read word" },
	{ .value = I2C_FUNC_SMBUS_PROC_CALL,
	  .name = "SMBus process call" },
	{ .value = I2C_FUNC_SMBUS_WRITE_BLOCK_DATA,
	  .name = "SMBus block write" },
	{ .value = I2C_FUNC_SMBUS_READ_BLOCK_DATA,
	  .name = "SMBus block read" },
	{ .value = I2C_FUNC_SMBUS_BLOCK_PROC_CALL,
	  .name = "SMBus block process call" },
	{ .value = I2C_FUNC_SMBUS_PEC,
	  .name = "SMBus PEC" },
	{ .value = I2C_FUNC_SMBUS_WRITE_I2C_BLOCK,
	  .name = "I2C block write" },
	{ .value = I2C_FUNC_SMBUS_READ_I2C_BLOCK,
	  .name = "I2C block read" },
	{ .value = 0, .name = NULL }
};

static enum adapter_type i2cdetect_get_funcs(int bus)
{
	enum adapter_type ret;
	unsigned long funcs;
	int fd;

	fd = i2c_dev_open(bus);

	get_funcs_matrix(fd, &funcs);
	if (funcs & I2C_FUNC_I2C)
		ret = ADT_I2C;
	else if (funcs & (I2C_FUNC_SMBUS_BYTE |
			  I2C_FUNC_SMBUS_BYTE_DATA |
			  I2C_FUNC_SMBUS_WORD_DATA))
		ret = ADT_SMBUS;
	else
		ret = ADT_DUMMY;

	close(fd);

	return ret;
}

static void NORETURN list_i2c_busses_and_exit(void)
{
	const char *const i2cdev_path = "/sys/class/i2c-dev";

	char path[NAME_MAX], name[128];
	struct dirent *de, *subde;
	enum adapter_type adt;
	DIR *dir, *subdir;
	int rv, bus;
	char *pos;
	FILE *fp;

	/*
	 * XXX Upstream i2cdetect also looks for i2c bus info in /proc/bus/i2c,
	 * but we won't bother since it's only useful on older kernels (before
	 * 2.6.5). We expect sysfs to be present and mounted at /sys/.
	 */

	dir = xopendir(i2cdev_path);
	while ((de = readdir(dir))) {
		if (de->d_name[0] == '.')
			continue;

		/* Simple version for ISA chips. */
		snprintf(path, NAME_MAX, "%s/%s/name",
			 i2cdev_path, de->d_name);
		fp = fopen(path, "r");
		if (fp == NULL) {
			snprintf(path, NAME_MAX,
				 "%s/%s/device/name",
				 i2cdev_path, de->d_name);
			fp = fopen(path, "r");
		}

		/* Non-ISA chips require the hard-way. */
		if (fp == NULL) {
			snprintf(path, NAME_MAX,
				 "%s/%s/device/name",
				 i2cdev_path, de->d_name);
			subdir = opendir(path);
			if (subdir == NULL)
				continue;

			while ((subde = readdir(subdir))) {
				if (subde->d_name[0] == '.')
					continue;

				if (is_prefixed_with(subde->d_name, "i2c-")) {
					snprintf(path, NAME_MAX,
						 "%s/%s/device/%s/name",
						 i2cdev_path, de->d_name,
						 subde->d_name);
					fp = fopen(path, "r");
					break;
				}
			}
		}

		if (fp != NULL) {
			/*
			 * Get the rest of the info and display a line
			 * for a single bus.
			 */
			memset(name, 0, sizeof(name));
			pos = fgets(name, sizeof(name), fp);
			fclose(fp);
			if (pos == NULL)
				continue;

			pos = strchr(name, '\n');
			if (pos != NULL)
				*pos = '\0';

			rv = sscanf(de->d_name, "i2c-%d", &bus);
			if (rv != 1)
				continue;

			if (is_prefixed_with(name, "ISA"))
				adt = ADT_ISA;
			else
				adt = i2cdetect_get_funcs(bus);

			printf(
				"i2c-%d\t%-10s\t%-32s\t%s\n",
				bus, adap_descs[adt].funcs,
				name, adap_descs[adt].algo);
		}
	}

	exit(EXIT_SUCCESS);
}

static void NORETURN no_support(const char *cmd)
{
	bb_error_msg_and_die("bus doesn't support %s", cmd);
}

static void will_skip(const char *cmd)
{
	bb_error_msg(
		"warning: can't use %s command, "
		"will skip some addresses", cmd);
}

//usage:#define i2cdetect_trivial_usage
//usage:       "-l | -F I2CBUS | [-ya] [-q|-r] I2CBUS [FIRST LAST]"
//usage:#define i2cdetect_full_usage "\n\n"
//usage:       "Detect I2C chips"
//usage:     "\n"
//usage:     "\n	-l	List installed buses"
//usage:     "\n	-F BUS#	List functionalities on this bus"
//usage:     "\n	-y	Disable interactive mode"
//usage:     "\n	-a	Force scanning of non-regular addresses"
//usage:     "\n	-q	Use smbus quick write commands for probing (default)"
//usage:     "\n	-r	Use smbus read byte commands for probing"
//usage:     "\n	FIRST and LAST limit probing range"
int i2cdetect_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int i2cdetect_main(int argc UNUSED_PARAM, char **argv)
{
	const unsigned opt_y = (1 << 0), opt_a = (1 << 1),
			      opt_q = (1 << 2), opt_r = (1 << 3),
			      opt_F = (1 << 4), opt_l = (1 << 5);

	int fd, bus_num, i, j, mode = I2CDETECT_MODE_AUTO, status, cmd;
	unsigned first = 0x03, last = 0x77, opts;
	unsigned long funcs;

	opts = getopt32(argv, "^"
			"yaqrFl"
			"\0"
			"q--r:r--q:"/*mutually exclusive*/
			"?3"/*up to 3 args*/
	);
	argv += optind;

	if (opts & opt_l)
		list_i2c_busses_and_exit();

	if (!argv[0])
		bb_show_usage();

	bus_num = i2c_bus_lookup(argv[0]);
	fd = i2c_dev_open(bus_num);
	get_funcs_matrix(fd, &funcs);

	if (opts & opt_F) {
		/* Only list the functionalities. */
		printf("Functionalities implemented by bus #%d\n", bus_num);
		for (i = 0; i2c_funcs_tab[i].value; i++) {
			printf("%-32s %s\n", i2c_funcs_tab[i].name,
			       funcs & i2c_funcs_tab[i].value ? "yes" : "no");
		}

		return EXIT_SUCCESS;
	}

	if (opts & opt_r)
		mode = I2CDETECT_MODE_READ;
	else if (opts & opt_q)
		mode = I2CDETECT_MODE_QUICK;

	if (opts & opt_a) {
		first = 0x00;
		last = 0x7f;
	}

	/* Read address range. */
	if (argv[1]) {
		first = xstrtou_range(argv[1], 16, first, last);
		if (argv[2])
			last = xstrtou_range(argv[2], 16, first, last);
	}

	if (!(funcs & (I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_READ_BYTE))) {
		no_support("detection commands");
	} else
	if (mode == I2CDETECT_MODE_QUICK && !(funcs & I2C_FUNC_SMBUS_QUICK)) {
		no_support("SMBus quick write");
	} else
	if (mode == I2CDETECT_MODE_READ && !(funcs & I2C_FUNC_SMBUS_READ_BYTE)) {
		no_support("SMBus receive byte");
	}

	if (mode == I2CDETECT_MODE_AUTO) {
		if (!(funcs & I2C_FUNC_SMBUS_QUICK))
			will_skip("SMBus quick write");
		if (!(funcs & I2C_FUNC_SMBUS_READ_BYTE))
			will_skip("SMBus receive byte");
	}

	if (!(opts & opt_y))
		confirm_action(-1, -1, -1, 0);

	puts("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");
	for (i = 0; i < 128; i += 16) {
		printf("%02x: ", i);
		for (j = 0; j < 16; j++) {
			fflush_all();

			cmd = mode;
			if (mode == I2CDETECT_MODE_AUTO) {
				if ((i+j >= 0x30 && i+j <= 0x37) ||
				    (i+j >= 0x50 && i+j <= 0x5F))
					cmd = I2CDETECT_MODE_READ;
				else
					cmd = I2CDETECT_MODE_QUICK;
			}

			/* Skip unwanted addresses. */
			if (i+j < first
			 || i+j > last
			 || (cmd == I2CDETECT_MODE_READ && !(funcs & I2C_FUNC_SMBUS_READ_BYTE))
			 || (cmd == I2CDETECT_MODE_QUICK && !(funcs & I2C_FUNC_SMBUS_QUICK)))
			{
				printf("   ");
				continue;
			}

			status = ioctl(fd, I2C_SLAVE, itoptr(i + j));
			if (status < 0) {
				if (errno == EBUSY) {
					printf("UU ");
					continue;
				}

				bb_perror_msg_and_die(
					"can't set address to 0x%02x", i + j);
			}

			switch (cmd) {
			case I2CDETECT_MODE_READ:
				/*
				 * This is known to lock SMBus on various
				 * write-only chips (mainly clock chips).
				 */
				status = i2c_smbus_read_byte(fd);
				break;
			default: /* I2CDETECT_MODE_QUICK: */
				/*
				 * This is known to corrupt the Atmel
				 * AT24RF08 EEPROM.
				 */
				status = i2c_smbus_write_quick(fd,
							       I2C_SMBUS_WRITE);
				break;
			}

			if (status < 0)
				printf("-- ");
			else
				printf("%02x ", i+j);
		}
		bb_putchar('\n');
	}

	return 0;
}
#endif /* ENABLE_I2CDETECT */
