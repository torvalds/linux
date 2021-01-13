// SPDX-License-Identifier: GPL-2.0
/*
 * Description   : JTAG Master driver
 *
 * Copyright (C) 2019 NuvoTon Corporation
 *
 */

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/spi/spi.h>
#include <linux/pinctrl/consumer.h>
#include <linux/gpio/consumer.h>

#define JTAG_PSPI_SPEED		(10 * 1000000)
#define JTAG_SCAN_LEN		256
#define JTAG_MAX_XFER_DATA_LEN	65535

struct tck_bitbang {
	unsigned char     tms;
	unsigned char     tdi; /* TDI bit value to write */
	unsigned char     tdo; /* TDO bit value to read */
};

struct bitbang_packet {
	struct tck_bitbang *data;
	__u32	length;
} __attribute__((__packed__));

struct scan_xfer {
	unsigned int     length;      /* number of bits */
	unsigned char    tdi[JTAG_SCAN_LEN];
	unsigned int     tdi_bytes;
	unsigned char    tdo[JTAG_SCAN_LEN];
	unsigned int     tdo_bytes;
	unsigned int     end_tap_state;
};

struct jtag_xfer {
	__u8	type;
	__u8	direction;
	__u8	from;
	__u8	endstate;
	__u32	padding;
	__u32	length;
	__u64	tdio;
};

struct jtag_tap_state {
	__u8	reset;
	__u8	from;
	__u8	endstate;
	__u8	tck;
};

enum jtagstates {
	jtagtlr,
	jtagrti,
	jtagseldr,
	jtagcapdr,
	jtagshfdr,
	jtagex1dr,
	jtagpaudr,
	jtagex2dr,
	jtagupddr,
	jtagselir,
	jtagcapir,
	jtagshfir,
	jtagex1ir,
	jtagpauir,
	jtagex2ir,
	jtagupdir,
	JTAG_STATE_CURRENT
};

enum JTAG_PIN {
	pin_TCK,
	pin_TDI,
	pin_TDO,
	pin_TMS,
	pin_NUM,
};

enum jtag_reset {
	JTAG_NO_RESET = 0,
	JTAG_FORCE_RESET = 1,
};

enum jtag_xfer_type {
	JTAG_SIR_XFER = 0,
	JTAG_SDR_XFER = 1,
	JTAG_RUNTEST_XFER,
};

enum jtag_xfer_direction {
	JTAG_READ_XFER = 1,
	JTAG_WRITE_XFER = 2,
	JTAG_READ_WRITE_XFER = 3,
};

#define __JTAG_IOCTL_MAGIC	0xb2
#define JTAG_SIOCSTATE	_IOW(__JTAG_IOCTL_MAGIC, 0, struct jtag_tap_state)
#define JTAG_SIOCFREQ	_IOW(__JTAG_IOCTL_MAGIC, 1, unsigned int)
#define JTAG_GIOCFREQ	_IOR(__JTAG_IOCTL_MAGIC, 2, unsigned int)
#define JTAG_IOCXFER	_IOWR(__JTAG_IOCTL_MAGIC, 3, struct jtag_xfer)
#define JTAG_GIOCSTATUS _IOWR(__JTAG_IOCTL_MAGIC, 4, enum jtagstates)
#define JTAG_SIOCMODE	_IOW(__JTAG_IOCTL_MAGIC, 5, unsigned int)
#define JTAG_IOCBITBANG	_IOW(__JTAG_IOCTL_MAGIC, 6, unsigned int)
#define JTAG_RUNTEST    _IOW(__JTAG_IOCTL_MAGIC, 7, unsigned int)

static DEFINE_IDA(jtag_ida);

static unsigned char reverse[16] = {
	0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE,
	0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF
};

#define REVERSE(x)  ((reverse[((x) & 0x0f)] << 4) | reverse[((x) & 0xf0) >> 4])

static DEFINE_SPINLOCK(jtag_file_lock);

struct jtag_info {
	struct device *dev;
	struct spi_device	*spi;
	struct miscdevice miscdev;
	struct gpio_desc	*pins[pin_NUM];
	struct pinctrl		*pinctrl;
	u32 freq;
	u8 tms_level;
	u8 tapstate;
	bool is_open;
	int id;

	/* transmit tck/tdi/tdo by pspi */
	#define MODE_PSPI		0
	/* transmit all signals by gpio */
	#define MODE_GPIO		1
	u8 mode;
};

/* this structure represents a TMS cycle, as expressed in a set of bits and
 * a count of bits (note: there are no start->end state transitions that
 * require more than 1 byte of TMS cycles)
 */
struct tmscycle {
	unsigned char tmsbits;
	unsigned char count;
};

/* this is the complete set TMS cycles for going from any TAP state to
 * any other TAP state, following a “shortest path” rule
 */
const struct tmscycle _tmscyclelookup[][16] = {
/*      TLR        RTI       SelDR      CapDR      SDR      */
/*      Ex1DR      PDR       Ex2DR      UpdDR      SelIR    */
/*      CapIR      SIR       Ex1IR      PIR        Ex2IR    */
/*      UpdIR                                               */
/* TLR */
	{
		{0x01, 1}, {0x00, 1}, {0x02, 2}, {0x02, 3}, {0x02, 4},
		{0x0a, 4}, {0x0a, 5}, {0x2a, 6}, {0x1a, 5}, {0x06, 3},
		{0x06, 4}, {0x06, 5}, {0x16, 5}, {0x16, 6}, {0x56, 7},
		{0x36, 6}
	},
/* RTI */
	{
		{0x07, 3}, {0x00, 1}, {0x01, 1}, {0x01, 2}, {0x01, 3},
		{0x05, 3}, {0x05, 4}, {0x15, 5}, {0x0d, 4}, {0x03, 2},
		{0x03, 3}, {0x03, 4}, {0x0b, 4}, {0x0b, 5}, {0x2b, 6},
		{0x1b, 5}
	},
/* SelDR */
	{
		{0x03, 2}, {0x03, 3}, {0x00, 0}, {0x00, 1}, {0x00, 2},
		{0x02, 2}, {0x02, 3}, {0x0a, 4}, {0x06, 3}, {0x01, 1},
		{0x01, 2}, {0x01, 3}, {0x05, 3}, {0x05, 4}, {0x15, 5},
		{0x0d, 4}
	},
/* CapDR */
	{
		{0x1f, 5}, {0x03, 3}, {0x07, 3}, {0x00, 0}, {0x00, 1},
		{0x01, 1}, {0x01, 2}, {0x05, 3}, {0x03, 2}, {0x0f, 4},
		{0x0f, 5}, {0x0f, 6}, {0x2f, 6}, {0x2f, 7}, {0xaf, 8},
		{0x6f, 7}
	},
/* SDR */
	{
		{0x1f, 5}, {0x03, 3}, {0x07, 3}, {0x07, 4}, {0x00, 0},
		{0x01, 1}, {0x01, 2}, {0x05, 3}, {0x03, 2}, {0x0f, 4},
		{0x0f, 5}, {0x0f, 6}, {0x2f, 6}, {0x2f, 7}, {0xaf, 8},
		{0x6f, 7}
	},
/* Ex1DR */
	{
		{0x0f, 4}, {0x01, 2}, {0x03, 2}, {0x03, 3}, {0x02, 3},
		{0x00, 0}, {0x00, 1}, {0x02, 2}, {0x01, 1}, {0x07, 3},
		{0x07, 4}, {0x07, 5}, {0x17, 5}, {0x17, 6}, {0x57, 7},
		{0x37, 6}
	},
/* PDR */
	{
		{0x1f, 5}, {0x03, 3}, {0x07, 3}, {0x07, 4}, {0x01, 2},
		{0x05, 3}, {0x00, 1}, {0x01, 1}, {0x03, 2}, {0x0f, 4},
		{0x0f, 5}, {0x0f, 6}, {0x2f, 6}, {0x2f, 7}, {0xaf, 8},
		{0x6f, 7}
	},
/* Ex2DR */
	{
		{0x0f, 4}, {0x01, 2}, {0x03, 2}, {0x03, 3}, {0x00, 1},
		{0x02, 2}, {0x02, 3}, {0x00, 0}, {0x01, 1}, {0x07, 3},
		{0x07, 4}, {0x07, 5}, {0x17, 5}, {0x17, 6}, {0x57, 7},
		{0x37, 6}
	},
/* UpdDR */
	{
		{0x07, 3}, {0x00, 1}, {0x01, 1}, {0x01, 2}, {0x01, 3},
		{0x05, 3}, {0x05, 4}, {0x15, 5}, {0x00, 0}, {0x03, 2},
		{0x03, 3}, {0x03, 4}, {0x0b, 4}, {0x0b, 5}, {0x2b, 6},
		{0x1b, 5}
	},
/* SelIR */
	{
		{0x01, 1}, {0x01, 2}, {0x05, 3}, {0x05, 4}, {0x05, 5},
		{0x15, 5}, {0x15, 6}, {0x55, 7}, {0x35, 6}, {0x00, 0},
		{0x00, 1}, {0x00, 2}, {0x02, 2}, {0x02, 3}, {0x0a, 4},
		{0x06, 3}
	},
/* CapIR */
	{
		{0x1f, 5}, {0x03, 3}, {0x07, 3}, {0x07, 4}, {0x07, 5},
		{0x17, 5}, {0x17, 6}, {0x57, 7}, {0x37, 6}, {0x0f, 4},
		{0x00, 0}, {0x00, 1}, {0x01, 1}, {0x01, 2}, {0x05, 3},
		{0x03, 2}
	},
/* SIR */
	{
		{0x1f, 5}, {0x03, 3}, {0x07, 3}, {0x07, 4}, {0x07, 5},
		{0x17, 5}, {0x17, 6}, {0x57, 7}, {0x37, 6}, {0x0f, 4},
		{0x0f, 5}, {0x00, 0}, {0x01, 1}, {0x01, 2}, {0x05, 3},
		{0x03, 2}
	},
/* Ex1IR */
	{
		{0x0f, 4}, {0x01, 2}, {0x03, 2}, {0x03, 3}, {0x03, 4},
		{0x0b, 4}, {0x0b, 5}, {0x2b, 6}, {0x1b, 5}, {0x07, 3},
		{0x07, 4}, {0x02, 3}, {0x00, 0}, {0x00, 1}, {0x02, 2},
		{0x01, 1}
	},
/* PIR */
	{
		{0x1f, 5}, {0x03, 3}, {0x07, 3}, {0x07, 4}, {0x07, 5},
		{0x17, 5}, {0x17, 6}, {0x57, 7}, {0x37, 6}, {0x0f, 4},
		{0x0f, 5}, {0x01, 2}, {0x05, 3}, {0x00, 1}, {0x01, 1},
		{0x03, 2}
	},
/* Ex2IR */
	{
		{0x0f, 4}, {0x01, 2}, {0x03, 2}, {0x03, 3}, {0x03, 4},
		{0x0b, 4}, {0x0b, 5}, {0x2b, 6}, {0x1b, 5}, {0x07, 3},
		{0x07, 4}, {0x00, 1}, {0x02, 2}, {0x02, 3}, {0x00, 0},
		{0x01, 1}
	},
/* UpdIR */
	{
		{0x07, 3}, {0x00, 1}, {0x01, 1}, {0x01, 2}, {0x01, 3},
		{0x05, 3}, {0x05, 4}, {0x15, 5}, {0x0d, 4}, {0x03, 2},
		{0x03, 3}, {0x03, 4}, {0x0b, 4}, {0x0b, 5}, {0x2b, 6},
		{0x00, 0}
	},
};

static u8 TCK_cycle(struct jtag_info *jtag,
		    unsigned char no_tdo, unsigned char TMS,
		    unsigned char TDI)
{
	u32 tdo = 0;

	/* IEEE 1149.1
	 * TMS & TDI shall be sampled by the test logic on the rising edge
	 * test logic shall change TDO on the falling edge
	 */
	gpiod_set_value(jtag->pins[pin_TDI], (int)TDI);
	if (jtag->tms_level != (int)TMS) {
		gpiod_set_value(jtag->pins[pin_TMS], (int)TMS);
		jtag->tms_level = (int)TMS;
	}
	gpiod_set_value(jtag->pins[pin_TCK], 1);
	if (!no_tdo)
		tdo = gpiod_get_value(jtag->pins[pin_TDO]);
	gpiod_set_value(jtag->pins[pin_TCK], 0);

	return tdo;
}

static inline void npcm7xx_jtag_bitbangs(struct jtag_info *jtag,
					 struct bitbang_packet *bitbangs,
					 struct tck_bitbang *bitbang_data)
{
	int i;

	for (i = 0; i < bitbangs->length; i++) {
		bitbang_data[i].tdo =
			TCK_cycle(jtag, 0, bitbang_data[i].tms,
				  bitbang_data[i].tdi);
		cond_resched();
	}
}

static int npcm7xx_jtag_set_tapstate(struct jtag_info *jtag,
				     enum jtagstates from, enum jtagstates to)
{
	unsigned char i;
	unsigned char tmsbits;
	unsigned char count;

	if (from == to)
		return 0;
	if (from == JTAG_STATE_CURRENT)
		from = jtag->tapstate;

	if (from > JTAG_STATE_CURRENT || to > JTAG_STATE_CURRENT)
		return -1;

	if (to == jtagtlr) {
		for (i = 0; i < 9; i++)
			TCK_cycle(jtag, 1, 1, 1);
		jtag->tapstate = jtagtlr;
		return 0;
	}

	tmsbits = _tmscyclelookup[from][to].tmsbits;
	count   = _tmscyclelookup[from][to].count;

	if (count == 0)
		return 0;

	for (i = 0; i < count; i++) {
		TCK_cycle(jtag, 1, (tmsbits & 1), 1);
		tmsbits >>= 1;
	}
	pr_debug("jtag: change state %d -> %d\n", from, to);
	jtag->tapstate = to;
	return 0;
}

static int npcm7xx_jtag_switch_pin_func(struct jtag_info *jtag, u8 mode)
{
	struct pinctrl_state	*state;

	if (mode == MODE_PSPI) {
		state = pinctrl_lookup_state(jtag->pinctrl, "pspi");
		if (IS_ERR(state))
			return -ENOENT;

		pinctrl_gpio_free(desc_to_gpio(jtag->pins[pin_TCK]));
		pinctrl_gpio_free(desc_to_gpio(jtag->pins[pin_TDI]));
		pinctrl_gpio_free(desc_to_gpio(jtag->pins[pin_TDO]));
		pinctrl_select_state(jtag->pinctrl, state);
	} else if (mode == MODE_GPIO) {
		state = pinctrl_lookup_state(jtag->pinctrl, "gpio");
		if (IS_ERR(state))
			return -ENOENT;

		pinctrl_select_state(jtag->pinctrl, state);
		pinctrl_gpio_request(desc_to_gpio(jtag->pins[pin_TCK]));
		pinctrl_gpio_request(desc_to_gpio(jtag->pins[pin_TDI]));
		pinctrl_gpio_request(desc_to_gpio(jtag->pins[pin_TDO]));
		jtag->tms_level = gpiod_get_value(jtag->pins[pin_TMS]);
	}

	return 0;
}

static int npcm7xx_jtag_xfer_spi(struct jtag_info *jtag, u32 xfer_bytes,
				 u8 *out, u8 *in)
{
	struct spi_message m;
	struct spi_transfer spi_xfer;
	int err;
	int i;

	err = npcm7xx_jtag_switch_pin_func(jtag, MODE_PSPI);
	if (err)
		return err;

	for (i = 0; i < xfer_bytes; i++)
		out[i] = REVERSE(out[i]);

	memset(&spi_xfer, 0, sizeof(spi_xfer));
	spi_xfer.speed_hz = jtag->freq;
	spi_xfer.tx_buf = out;
	spi_xfer.rx_buf = in;
	spi_xfer.len = xfer_bytes;

	spi_message_init(&m);
	spi_message_add_tail(&spi_xfer, &m);
	err = spi_sync(jtag->spi, &m);

	for (i = 0; i < xfer_bytes; i++)
		in[i] = REVERSE(in[i]);

	err = npcm7xx_jtag_switch_pin_func(jtag, MODE_GPIO);

	return err;
}

static int npcm7xx_jtag_xfer_gpio(struct jtag_info *jtag,
				  struct jtag_xfer *xfer, u8 *out, u8 *in)
{
	unsigned long *bitmap_tdi = (unsigned long *)out;
	unsigned long *bitmap_tdo = (unsigned long *)in;
	u32 xfer_bits = xfer->length;
	u32 bit_index = 0;
	u8 tdi, tdo, tms;

	while (bit_index < xfer_bits) {
		tdi = 0;
		tms = 0;

		if (test_bit(bit_index, bitmap_tdi))
			tdi = 1;

		/* If this is the last bit, leave TMS high */
		if ((bit_index == xfer_bits - 1) && xfer->endstate != jtagshfdr &&
		    xfer->endstate != jtagshfir && xfer->endstate != JTAG_STATE_CURRENT)
			tms = 1;

		/* shift 1 bit */
		tdo = TCK_cycle(jtag, 0, tms, tdi);
		cond_resched();
		/* If it was the last bit in the scan and the end_tap_state is
		 * something other than shiftDR or shiftIR then go to Exit1.
		 * IMPORTANT Note: if the end_tap_state is ShiftIR/DR and the
		 * next call to this function is a shiftDR/IR then the driver
		 * will not change state!
		 */
		if (tms)
			jtag->tapstate = (jtag->tapstate == jtagshfdr) ?
				jtagex1dr : jtagex1ir;

		if (tdo)
			bitmap_set(bitmap_tdo, bit_index, 1);

		bit_index++;
	}

	return 0;
}

static int npcm7xx_jtag_readwrite_scan(struct jtag_info *jtag,
				       struct jtag_xfer *xfer, u8 *tdi, u8 *tdo)
{
	u32 xfer_bytes = DIV_ROUND_UP(xfer->length, BITS_PER_BYTE);
	u32 remain_bits = xfer->length;
	u32 spi_xfer_bytes = 0;

	if (xfer_bytes > 1 && jtag->mode == MODE_PSPI) {
		/* The last byte should be sent using gpio bitbang
		 * (TMS needed)
		 */
		spi_xfer_bytes = xfer_bytes - 1;
		if (npcm7xx_jtag_xfer_spi(jtag, spi_xfer_bytes, tdi, tdo))
			return -EIO;
		remain_bits -= spi_xfer_bytes * 8;
	}

	if (remain_bits) {
		xfer->length = remain_bits;
		npcm7xx_jtag_xfer_gpio(jtag, xfer, tdi + spi_xfer_bytes,
				       tdo + spi_xfer_bytes);
	}

	npcm7xx_jtag_set_tapstate(jtag, JTAG_STATE_CURRENT, xfer->endstate);

	return 0;
}

static int npcm7xx_jtag_xfer(struct jtag_info *npcm7xx_jtag,
			     struct jtag_xfer *xfer, u8 *data, u32 bytes)
{
	u8 *tdo;
	int ret;

	if (xfer->length == 0)
		return 0;

	tdo = kzalloc(bytes, GFP_KERNEL);
	if (!tdo)
		return -ENOMEM;

	if (xfer->type == JTAG_SIR_XFER)
		npcm7xx_jtag_set_tapstate(npcm7xx_jtag, xfer->from, jtagshfir);
	else if (xfer->type == JTAG_SDR_XFER)
		npcm7xx_jtag_set_tapstate(npcm7xx_jtag, xfer->from, jtagshfdr);

	ret = npcm7xx_jtag_readwrite_scan(npcm7xx_jtag, xfer, data, tdo);
	memcpy(data, tdo, bytes);
	kfree(tdo);

	return ret;
}

/* Run in current state for specific number of tcks */
static int npcm7xx_jtag_runtest(struct jtag_info *jtag, unsigned int tcks)
{
	struct jtag_xfer xfer;
	u32 bytes = DIV_ROUND_UP(tcks, BITS_PER_BYTE);
	u8 *buf;
	u32 i;
	int err;

	if (jtag->mode != MODE_PSPI) {
		for (i = 0; i < tcks; i++) {
			TCK_cycle(jtag, 0, 0, 1);
			cond_resched();
		}
		return 0;
	}

	buf = kzalloc(bytes, GFP_KERNEL);
	xfer.type = JTAG_RUNTEST_XFER;
	xfer.direction = JTAG_WRITE_XFER;
	xfer.from = JTAG_STATE_CURRENT;
	xfer.endstate = JTAG_STATE_CURRENT;
	xfer.length = tcks;

	err = npcm7xx_jtag_xfer(jtag, &xfer, buf, bytes);
	kfree(buf);

	return err;
}

static long jtag_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct jtag_info *priv = file->private_data;
	struct jtag_tap_state tapstate;
	void __user *argp = (void __user *)arg;
	struct jtag_xfer xfer;
	struct bitbang_packet bitbang;
	struct tck_bitbang *bitbang_data;
	u8 *xfer_data;
	u32 data_size;
	u32 value;
	int ret = 0;

	switch (cmd) {
	case JTAG_SIOCFREQ:
		if (get_user(value, (__u32 __user *)arg))
			return -EFAULT;
		if (value <= priv->spi->max_speed_hz) {
			priv->freq = value;
		} else {
			dev_err(priv->dev, "%s: invalid jtag freq %u\n",
				__func__, value);
			ret = -EINVAL;
		}
		break;
	case JTAG_GIOCFREQ:
		if (put_user(priv->freq, (__u32 __user *)arg))
			return -EFAULT;
		break;
	case JTAG_IOCBITBANG:
		if (copy_from_user(&bitbang, (const void __user *)arg,
				   sizeof(struct bitbang_packet)))
			return -EFAULT;

		if (bitbang.length >= JTAG_MAX_XFER_DATA_LEN)
			return -EINVAL;

		data_size = bitbang.length * sizeof(struct tck_bitbang);
		bitbang_data = memdup_user((void __user *)bitbang.data,
					   data_size);
		if (IS_ERR(bitbang_data))
			return -EFAULT;

		npcm7xx_jtag_bitbangs(priv, &bitbang, bitbang_data);
		ret = copy_to_user((void __user *)bitbang.data,
				   (void *)bitbang_data, data_size);
		kfree(bitbang_data);
		if (ret)
			return -EFAULT;
		break;
	case JTAG_SIOCSTATE:
		if (copy_from_user(&tapstate, (const void __user *)arg,
				   sizeof(struct jtag_tap_state)))
			return -EFAULT;

		if (tapstate.from > JTAG_STATE_CURRENT)
			return -EINVAL;

		if (tapstate.endstate > JTAG_STATE_CURRENT)
			return -EINVAL;

		if (tapstate.reset > JTAG_FORCE_RESET)
			return -EINVAL;
		if (tapstate.reset == JTAG_FORCE_RESET)
			npcm7xx_jtag_set_tapstate(priv, JTAG_STATE_CURRENT,
						  jtagtlr);
		npcm7xx_jtag_set_tapstate(priv, tapstate.from,
					  tapstate.endstate);
		break;
	case JTAG_GIOCSTATUS:
		ret = put_user(priv->tapstate, (__u32 __user *)arg);
		break;
	case JTAG_IOCXFER:
		if (copy_from_user(&xfer, argp, sizeof(struct jtag_xfer)))
			return -EFAULT;

		if (xfer.length >= JTAG_MAX_XFER_DATA_LEN)
			return -EINVAL;

		if (xfer.type > JTAG_SDR_XFER)
			return -EINVAL;

		if (xfer.direction > JTAG_READ_WRITE_XFER)
			return -EINVAL;

		if (xfer.from > JTAG_STATE_CURRENT)
			return -EINVAL;

		if (xfer.endstate > JTAG_STATE_CURRENT)
			return -EINVAL;

		data_size = DIV_ROUND_UP(xfer.length, BITS_PER_BYTE);
		xfer_data = memdup_user(u64_to_user_ptr(xfer.tdio), data_size);
		if (IS_ERR(xfer_data))
			return -EFAULT;
		ret = npcm7xx_jtag_xfer(priv, &xfer, xfer_data, data_size);
		if (ret) {
			kfree(xfer_data);
			return -EIO;
		}
		ret = copy_to_user(u64_to_user_ptr(xfer.tdio),
				   (void *)xfer_data, data_size);
		kfree(xfer_data);
		if (ret)
			return -EFAULT;

		if (copy_to_user((void __user *)arg, (void *)&xfer,
				 sizeof(struct jtag_xfer)))
			return -EFAULT;
		break;
	case JTAG_SIOCMODE:
		if (get_user(value, (__u32 __user *)arg))
			return -EFAULT;
		if (value != MODE_GPIO && value != MODE_PSPI)
			return -EINVAL;
		priv->mode = value;
		break;
	case JTAG_RUNTEST:
		ret = npcm7xx_jtag_runtest(priv, (unsigned int)arg);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int jtag_open(struct inode *inode, struct file *file)
{
	struct jtag_info *jtag;

	jtag = container_of(file->private_data, struct jtag_info, miscdev);

	spin_lock(&jtag_file_lock);
	if (jtag->is_open) {
		spin_unlock(&jtag_file_lock);
		return -EBUSY;
	}

	jtag->is_open = true;
	file->private_data = jtag;

	spin_unlock(&jtag_file_lock);

	return 0;
}

static int jtag_release(struct inode *inode, struct file *file)
{
	struct jtag_info *jtag = file->private_data;

	spin_lock(&jtag_file_lock);
	jtag->is_open = false;
	spin_unlock(&jtag_file_lock);

	return 0;
}

const struct file_operations npcm_jtag_fops = {
	.open              = jtag_open,
	.unlocked_ioctl    = jtag_ioctl,
	.release           = jtag_release,
};

static int jtag_register_device(struct jtag_info *jtag)
{
	struct device *dev = jtag->dev;
	int err;
	int id;

	if (!dev)
		return -ENODEV;

	id = ida_simple_get(&jtag_ida, 0, 0, GFP_KERNEL);
	if (id < 0)
		return id;

	jtag->id = id;
	/* register miscdev */
	jtag->miscdev.parent = dev;
	jtag->miscdev.fops =  &npcm_jtag_fops;
	jtag->miscdev.minor = MISC_DYNAMIC_MINOR;
	jtag->miscdev.name = kasprintf(GFP_KERNEL, "jtag%d", id);
	if (!jtag->miscdev.name) {
		err = -ENOMEM;
		goto err;
	}

	err = misc_register(&jtag->miscdev);
	if (err) {
		dev_err(jtag->miscdev.parent,
			"Unable to register device, err %d\n", err);
		kfree(jtag->miscdev.name);
		goto err;
	}

	return 0;

err:
	ida_simple_remove(&jtag_ida, id);
	return err;
}

static int npcm7xx_jtag_init(struct device *dev, struct jtag_info *npcm7xx_jtag)
{
	struct pinctrl		*pinctrl;
	int i;

	pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(pinctrl))
		return PTR_ERR(pinctrl);

	npcm7xx_jtag->pinctrl = pinctrl;

	/* jtag pins */
	npcm7xx_jtag->pins[pin_TCK] = gpiod_get(dev, "tck", GPIOD_OUT_LOW);
	npcm7xx_jtag->pins[pin_TDI] = gpiod_get(dev, "tdi", GPIOD_OUT_HIGH);
	npcm7xx_jtag->pins[pin_TDO] = gpiod_get(dev, "tdo", GPIOD_IN);
	npcm7xx_jtag->pins[pin_TMS] = gpiod_get(dev, "tms", GPIOD_OUT_HIGH);
	for (i = 0; i < pin_NUM; i++) {
		if (IS_ERR(npcm7xx_jtag->pins[i]))
			return PTR_ERR(npcm7xx_jtag->pins[i]);
	}

	npcm7xx_jtag->freq = JTAG_PSPI_SPEED;
	npcm7xx_jtag->tms_level = gpiod_get_value(npcm7xx_jtag->pins[pin_TMS]);
	npcm7xx_jtag_set_tapstate(npcm7xx_jtag, JTAG_STATE_CURRENT, jtagtlr);
	npcm7xx_jtag->mode = MODE_PSPI;

	return 0;
}

static int npcm7xx_jtag_probe(struct spi_device *spi)
{
	struct jtag_info *npcm_jtag;
	int ret;

	dev_info(&spi->dev, "%s", __func__);

	npcm_jtag = kzalloc(sizeof(struct jtag_info), GFP_KERNEL);
	if (!npcm_jtag)
		return -ENOMEM;

	npcm_jtag->dev = &spi->dev;
	npcm_jtag->spi = spi;
	spi->mode = SPI_MODE_0 | SPI_NO_CS;

	/* Initialize device*/
	ret = npcm7xx_jtag_init(&spi->dev, npcm_jtag);
	if (ret)
		goto err;

	/* Register a misc device */
	ret = jtag_register_device(npcm_jtag);
	if (ret) {
		dev_err(&spi->dev, "failed to create device\n");
		goto err;
	}
	spi_set_drvdata(spi, npcm_jtag);

	return 0;
err:
	kfree(npcm_jtag);
	return ret;
}

static int npcm7xx_jtag_remove(struct spi_device  *spi)
{
	struct jtag_info *jtag = spi_get_drvdata(spi);
	int i;

	if (!jtag)
		return 0;

	misc_deregister(&jtag->miscdev);
	kfree(jtag->miscdev.name);
	for (i = 0; i < pin_NUM; i++) {
		gpiod_direction_input(jtag->pins[i]);
		gpiod_put(jtag->pins[i]);
	}
	kfree(jtag);
	ida_simple_remove(&jtag_ida, jtag->id);

	return 0;
}

static const struct of_device_id npcm7xx_jtag_of_match[] = {
	{ .compatible = "nuvoton,npcm750-jtag-master", },
	{},
};
MODULE_DEVICE_TABLE(of, npcm7xx_jtag_of_match);

static struct spi_driver npcm7xx_jtag_driver = {
	.driver = {
		.name		= "npcm7xx_jtag",
		.of_match_table = npcm7xx_jtag_of_match,
	},
	.probe		= npcm7xx_jtag_probe,
	.remove		= npcm7xx_jtag_remove,
};

module_spi_driver(npcm7xx_jtag_driver);

MODULE_AUTHOR("Stanley Chu <yschu@nuvoton.com>");
MODULE_DESCRIPTION("NPCM7xx JTAG Master Driver");
MODULE_LICENSE("GPL");
