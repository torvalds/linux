/*
 * Linux-DVB Driver for DiBcom's DiB9000 and demodulator-family.
 *
 * Copyright (C) 2005-10 DiBcom (http://www.dibcom.fr/)
 *
 * This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/mutex.h>

#include "dvb_math.h"
#include "dvb_frontend.h"

#include "dib9000.h"
#include "dibx000_common.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "turn on debugging (default: 0)");

#define dprintk(fmt, arg...) do {					\
	if (debug)							\
		printk(KERN_DEBUG pr_fmt("%s: " fmt),			\
		       __func__, ##arg);				\
} while (0)

#define MAX_NUMBER_OF_FRONTENDS 6

struct i2c_device {
	struct i2c_adapter *i2c_adap;
	u8 i2c_addr;
	u8 *i2c_read_buffer;
	u8 *i2c_write_buffer;
};

struct dib9000_pid_ctrl {
#define DIB9000_PID_FILTER_CTRL 0
#define DIB9000_PID_FILTER      1
	u8 cmd;
	u8 id;
	u16 pid;
	u8 onoff;
};

struct dib9000_state {
	struct i2c_device i2c;

	struct dibx000_i2c_master i2c_master;
	struct i2c_adapter tuner_adap;
	struct i2c_adapter component_bus;

	u16 revision;
	u8 reg_offs;

	enum frontend_tune_state tune_state;
	u32 status;
	struct dvb_frontend_parametersContext channel_status;

	u8 fe_id;

#define DIB9000_GPIO_DEFAULT_DIRECTIONS 0xffff
	u16 gpio_dir;
#define DIB9000_GPIO_DEFAULT_VALUES     0x0000
	u16 gpio_val;
#define DIB9000_GPIO_DEFAULT_PWM_POS    0xffff
	u16 gpio_pwm_pos;

	union {			/* common for all chips */
		struct {
			u8 mobile_mode:1;
		} host;

		struct {
			struct dib9000_fe_memory_map {
				u16 addr;
				u16 size;
			} fe_mm[18];
			u8 memcmd;

			struct mutex mbx_if_lock;	/* to protect read/write operations */
			struct mutex mbx_lock;	/* to protect the whole mailbox handling */

			struct mutex mem_lock;	/* to protect the memory accesses */
			struct mutex mem_mbx_lock;	/* to protect the memory-based mailbox */

#define MBX_MAX_WORDS (256 - 200 - 2)
#define DIB9000_MSG_CACHE_SIZE 2
			u16 message_cache[DIB9000_MSG_CACHE_SIZE][MBX_MAX_WORDS];
			u8 fw_is_running;
		} risc;
	} platform;

	union {			/* common for all platforms */
		struct {
			struct dib9000_config cfg;
		} d9;
	} chip;

	struct dvb_frontend *fe[MAX_NUMBER_OF_FRONTENDS];
	u16 component_bus_speed;

	/* for the I2C transfer */
	struct i2c_msg msg[2];
	u8 i2c_write_buffer[255];
	u8 i2c_read_buffer[255];
	struct mutex demod_lock;
	u8 get_frontend_internal;
	struct dib9000_pid_ctrl pid_ctrl[10];
	s8 pid_ctrl_index; /* -1: empty list; -2: do not use the list */
};

static const u32 fe_info[44] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0
};

enum dib9000_power_mode {
	DIB9000_POWER_ALL = 0,

	DIB9000_POWER_NO,
	DIB9000_POWER_INTERF_ANALOG_AGC,
	DIB9000_POWER_COR4_DINTLV_ICIRM_EQUAL_CFROD,
	DIB9000_POWER_COR4_CRY_ESRAM_MOUT_NUD,
	DIB9000_POWER_INTERFACE_ONLY,
};

enum dib9000_out_messages {
	OUT_MSG_HBM_ACK,
	OUT_MSG_HOST_BUF_FAIL,
	OUT_MSG_REQ_VERSION,
	OUT_MSG_BRIDGE_I2C_W,
	OUT_MSG_BRIDGE_I2C_R,
	OUT_MSG_BRIDGE_APB_W,
	OUT_MSG_BRIDGE_APB_R,
	OUT_MSG_SCAN_CHANNEL,
	OUT_MSG_MONIT_DEMOD,
	OUT_MSG_CONF_GPIO,
	OUT_MSG_DEBUG_HELP,
	OUT_MSG_SUBBAND_SEL,
	OUT_MSG_ENABLE_TIME_SLICE,
	OUT_MSG_FE_FW_DL,
	OUT_MSG_FE_CHANNEL_SEARCH,
	OUT_MSG_FE_CHANNEL_TUNE,
	OUT_MSG_FE_SLEEP,
	OUT_MSG_FE_SYNC,
	OUT_MSG_CTL_MONIT,

	OUT_MSG_CONF_SVC,
	OUT_MSG_SET_HBM,
	OUT_MSG_INIT_DEMOD,
	OUT_MSG_ENABLE_DIVERSITY,
	OUT_MSG_SET_OUTPUT_MODE,
	OUT_MSG_SET_PRIORITARY_CHANNEL,
	OUT_MSG_ACK_FRG,
	OUT_MSG_INIT_PMU,
};

enum dib9000_in_messages {
	IN_MSG_DATA,
	IN_MSG_FRAME_INFO,
	IN_MSG_CTL_MONIT,
	IN_MSG_ACK_FREE_ITEM,
	IN_MSG_DEBUG_BUF,
	IN_MSG_MPE_MONITOR,
	IN_MSG_RAWTS_MONITOR,
	IN_MSG_END_BRIDGE_I2C_RW,
	IN_MSG_END_BRIDGE_APB_RW,
	IN_MSG_VERSION,
	IN_MSG_END_OF_SCAN,
	IN_MSG_MONIT_DEMOD,
	IN_MSG_ERROR,
	IN_MSG_FE_FW_DL_DONE,
	IN_MSG_EVENT,
	IN_MSG_ACK_CHANGE_SVC,
	IN_MSG_HBM_PROF,
};

/* memory_access requests */
#define FE_MM_W_CHANNEL                   0
#define FE_MM_W_FE_INFO                   1
#define FE_MM_RW_SYNC                     2

#define FE_SYNC_CHANNEL          1
#define FE_SYNC_W_GENERIC_MONIT	 2
#define FE_SYNC_COMPONENT_ACCESS 3

#define FE_MM_R_CHANNEL_SEARCH_STATE      3
#define FE_MM_R_CHANNEL_UNION_CONTEXT     4
#define FE_MM_R_FE_INFO                   5
#define FE_MM_R_FE_MONITOR                6

#define FE_MM_W_CHANNEL_HEAD              7
#define FE_MM_W_CHANNEL_UNION             8
#define FE_MM_W_CHANNEL_CONTEXT           9
#define FE_MM_R_CHANNEL_UNION            10
#define FE_MM_R_CHANNEL_CONTEXT          11
#define FE_MM_R_CHANNEL_TUNE_STATE       12

#define FE_MM_R_GENERIC_MONITORING_SIZE	 13
#define FE_MM_W_GENERIC_MONITORING	     14
#define FE_MM_R_GENERIC_MONITORING	     15

#define FE_MM_W_COMPONENT_ACCESS         16
#define FE_MM_RW_COMPONENT_ACCESS_BUFFER 17
static int dib9000_risc_apb_access_read(struct dib9000_state *state, u32 address, u16 attribute, const u8 * tx, u32 txlen, u8 * b, u32 len);
static int dib9000_risc_apb_access_write(struct dib9000_state *state, u32 address, u16 attribute, const u8 * b, u32 len);

static u16 to_fw_output_mode(u16 mode)
{
	switch (mode) {
	case OUTMODE_HIGH_Z:
		return 0;
	case OUTMODE_MPEG2_PAR_GATED_CLK:
		return 4;
	case OUTMODE_MPEG2_PAR_CONT_CLK:
		return 8;
	case OUTMODE_MPEG2_SERIAL:
		return 16;
	case OUTMODE_DIVERSITY:
		return 128;
	case OUTMODE_MPEG2_FIFO:
		return 2;
	case OUTMODE_ANALOG_ADC:
		return 1;
	default:
		return 0;
	}
}

static int dib9000_read16_attr(struct dib9000_state *state, u16 reg, u8 *b, u32 len, u16 attribute)
{
	u32 chunk_size = 126;
	u32 l;
	int ret;

	if (state->platform.risc.fw_is_running && (reg < 1024))
		return dib9000_risc_apb_access_read(state, reg, attribute, NULL, 0, b, len);

	memset(state->msg, 0, 2 * sizeof(struct i2c_msg));
	state->msg[0].addr = state->i2c.i2c_addr >> 1;
	state->msg[0].flags = 0;
	state->msg[0].buf = state->i2c_write_buffer;
	state->msg[0].len = 2;
	state->msg[1].addr = state->i2c.i2c_addr >> 1;
	state->msg[1].flags = I2C_M_RD;
	state->msg[1].buf = b;
	state->msg[1].len = len;

	state->i2c_write_buffer[0] = reg >> 8;
	state->i2c_write_buffer[1] = reg & 0xff;

	if (attribute & DATA_BUS_ACCESS_MODE_8BIT)
		state->i2c_write_buffer[0] |= (1 << 5);
	if (attribute & DATA_BUS_ACCESS_MODE_NO_ADDRESS_INCREMENT)
		state->i2c_write_buffer[0] |= (1 << 4);

	do {
		l = len < chunk_size ? len : chunk_size;
		state->msg[1].len = l;
		state->msg[1].buf = b;
		ret = i2c_transfer(state->i2c.i2c_adap, state->msg, 2) != 2 ? -EREMOTEIO : 0;
		if (ret != 0) {
			dprintk("i2c read error on %d\n", reg);
			return -EREMOTEIO;
		}

		b += l;
		len -= l;

		if (!(attribute & DATA_BUS_ACCESS_MODE_NO_ADDRESS_INCREMENT))
			reg += l / 2;
	} while ((ret == 0) && len);

	return 0;
}

static u16 dib9000_i2c_read16(struct i2c_device *i2c, u16 reg)
{
	struct i2c_msg msg[2] = {
		{.addr = i2c->i2c_addr >> 1, .flags = 0,
			.buf = i2c->i2c_write_buffer, .len = 2},
		{.addr = i2c->i2c_addr >> 1, .flags = I2C_M_RD,
			.buf = i2c->i2c_read_buffer, .len = 2},
	};

	i2c->i2c_write_buffer[0] = reg >> 8;
	i2c->i2c_write_buffer[1] = reg & 0xff;

	if (i2c_transfer(i2c->i2c_adap, msg, 2) != 2) {
		dprintk("read register %x error\n", reg);
		return 0;
	}

	return (i2c->i2c_read_buffer[0] << 8) | i2c->i2c_read_buffer[1];
}

static inline u16 dib9000_read_word(struct dib9000_state *state, u16 reg)
{
	if (dib9000_read16_attr(state, reg, state->i2c_read_buffer, 2, 0) != 0)
		return 0;
	return (state->i2c_read_buffer[0] << 8) | state->i2c_read_buffer[1];
}

static inline u16 dib9000_read_word_attr(struct dib9000_state *state, u16 reg, u16 attribute)
{
	if (dib9000_read16_attr(state, reg, state->i2c_read_buffer, 2,
				attribute) != 0)
		return 0;
	return (state->i2c_read_buffer[0] << 8) | state->i2c_read_buffer[1];
}

#define dib9000_read16_noinc_attr(state, reg, b, len, attribute) dib9000_read16_attr(state, reg, b, len, (attribute) | DATA_BUS_ACCESS_MODE_NO_ADDRESS_INCREMENT)

static int dib9000_write16_attr(struct dib9000_state *state, u16 reg, const u8 *buf, u32 len, u16 attribute)
{
	u32 chunk_size = 126;
	u32 l;
	int ret;

	if (state->platform.risc.fw_is_running && (reg < 1024)) {
		if (dib9000_risc_apb_access_write
		    (state, reg, DATA_BUS_ACCESS_MODE_16BIT | DATA_BUS_ACCESS_MODE_NO_ADDRESS_INCREMENT | attribute, buf, len) != 0)
			return -EINVAL;
		return 0;
	}

	memset(&state->msg[0], 0, sizeof(struct i2c_msg));
	state->msg[0].addr = state->i2c.i2c_addr >> 1;
	state->msg[0].flags = 0;
	state->msg[0].buf = state->i2c_write_buffer;
	state->msg[0].len = len + 2;

	state->i2c_write_buffer[0] = (reg >> 8) & 0xff;
	state->i2c_write_buffer[1] = (reg) & 0xff;

	if (attribute & DATA_BUS_ACCESS_MODE_8BIT)
		state->i2c_write_buffer[0] |= (1 << 5);
	if (attribute & DATA_BUS_ACCESS_MODE_NO_ADDRESS_INCREMENT)
		state->i2c_write_buffer[0] |= (1 << 4);

	do {
		l = len < chunk_size ? len : chunk_size;
		state->msg[0].len = l + 2;
		memcpy(&state->i2c_write_buffer[2], buf, l);

		ret = i2c_transfer(state->i2c.i2c_adap, state->msg, 1) != 1 ? -EREMOTEIO : 0;

		buf += l;
		len -= l;

		if (!(attribute & DATA_BUS_ACCESS_MODE_NO_ADDRESS_INCREMENT))
			reg += l / 2;
	} while ((ret == 0) && len);

	return ret;
}

static int dib9000_i2c_write16(struct i2c_device *i2c, u16 reg, u16 val)
{
	struct i2c_msg msg = {
		.addr = i2c->i2c_addr >> 1, .flags = 0,
		.buf = i2c->i2c_write_buffer, .len = 4
	};

	i2c->i2c_write_buffer[0] = (reg >> 8) & 0xff;
	i2c->i2c_write_buffer[1] = reg & 0xff;
	i2c->i2c_write_buffer[2] = (val >> 8) & 0xff;
	i2c->i2c_write_buffer[3] = val & 0xff;

	return i2c_transfer(i2c->i2c_adap, &msg, 1) != 1 ? -EREMOTEIO : 0;
}

static inline int dib9000_write_word(struct dib9000_state *state, u16 reg, u16 val)
{
	u8 b[2] = { val >> 8, val & 0xff };
	return dib9000_write16_attr(state, reg, b, 2, 0);
}

static inline int dib9000_write_word_attr(struct dib9000_state *state, u16 reg, u16 val, u16 attribute)
{
	u8 b[2] = { val >> 8, val & 0xff };
	return dib9000_write16_attr(state, reg, b, 2, attribute);
}

#define dib9000_write(state, reg, buf, len) dib9000_write16_attr(state, reg, buf, len, 0)
#define dib9000_write16_noinc(state, reg, buf, len) dib9000_write16_attr(state, reg, buf, len, DATA_BUS_ACCESS_MODE_NO_ADDRESS_INCREMENT)
#define dib9000_write16_noinc_attr(state, reg, buf, len, attribute) dib9000_write16_attr(state, reg, buf, len, DATA_BUS_ACCESS_MODE_NO_ADDRESS_INCREMENT | (attribute))

#define dib9000_mbx_send(state, id, data, len) dib9000_mbx_send_attr(state, id, data, len, 0)
#define dib9000_mbx_get_message(state, id, msg, len) dib9000_mbx_get_message_attr(state, id, msg, len, 0)

#define MAC_IRQ      (1 << 1)
#define IRQ_POL_MSK  (1 << 4)

#define dib9000_risc_mem_read_chunks(state, b, len) dib9000_read16_attr(state, 1063, b, len, DATA_BUS_ACCESS_MODE_8BIT | DATA_BUS_ACCESS_MODE_NO_ADDRESS_INCREMENT)
#define dib9000_risc_mem_write_chunks(state, buf, len) dib9000_write16_attr(state, 1063, buf, len, DATA_BUS_ACCESS_MODE_8BIT | DATA_BUS_ACCESS_MODE_NO_ADDRESS_INCREMENT)

static void dib9000_risc_mem_setup_cmd(struct dib9000_state *state, u32 addr, u32 len, u8 reading)
{
	u8 b[14] = { 0 };

/*      dprintk("%d memcmd: %d %d %d\n", state->fe_id, addr, addr+len, len); */
/*      b[0] = 0 << 7; */
	b[1] = 1;

/*      b[2] = 0; */
/*      b[3] = 0; */
	b[4] = (u8) (addr >> 8);
	b[5] = (u8) (addr & 0xff);

/*      b[10] = 0; */
/*      b[11] = 0; */
	b[12] = (u8) (addr >> 8);
	b[13] = (u8) (addr & 0xff);

	addr += len;
/*      b[6] = 0; */
/*      b[7] = 0; */
	b[8] = (u8) (addr >> 8);
	b[9] = (u8) (addr & 0xff);

	dib9000_write(state, 1056, b, 14);
	if (reading)
		dib9000_write_word(state, 1056, (1 << 15) | 1);
	state->platform.risc.memcmd = -1;	/* if it was called directly reset it - to force a future setup-call to set it */
}

static void dib9000_risc_mem_setup(struct dib9000_state *state, u8 cmd)
{
	struct dib9000_fe_memory_map *m = &state->platform.risc.fe_mm[cmd & 0x7f];
	/* decide whether we need to "refresh" the memory controller */
	if (state->platform.risc.memcmd == cmd &&	/* same command */
	    !(cmd & 0x80 && m->size < 67))	/* and we do not want to read something with less than 67 bytes looping - working around a bug in the memory controller */
		return;
	dib9000_risc_mem_setup_cmd(state, m->addr, m->size, cmd & 0x80);
	state->platform.risc.memcmd = cmd;
}

static int dib9000_risc_mem_read(struct dib9000_state *state, u8 cmd, u8 * b, u16 len)
{
	if (!state->platform.risc.fw_is_running)
		return -EIO;

	if (mutex_lock_interruptible(&state->platform.risc.mem_lock) < 0) {
		dprintk("could not get the lock\n");
		return -EINTR;
	}
	dib9000_risc_mem_setup(state, cmd | 0x80);
	dib9000_risc_mem_read_chunks(state, b, len);
	mutex_unlock(&state->platform.risc.mem_lock);
	return 0;
}

static int dib9000_risc_mem_write(struct dib9000_state *state, u8 cmd, const u8 * b)
{
	struct dib9000_fe_memory_map *m = &state->platform.risc.fe_mm[cmd];
	if (!state->platform.risc.fw_is_running)
		return -EIO;

	if (mutex_lock_interruptible(&state->platform.risc.mem_lock) < 0) {
		dprintk("could not get the lock\n");
		return -EINTR;
	}
	dib9000_risc_mem_setup(state, cmd);
	dib9000_risc_mem_write_chunks(state, b, m->size);
	mutex_unlock(&state->platform.risc.mem_lock);
	return 0;
}

static int dib9000_firmware_download(struct dib9000_state *state, u8 risc_id, u16 key, const u8 * code, u32 len)
{
	u16 offs;

	if (risc_id == 1)
		offs = 16;
	else
		offs = 0;

	/* config crtl reg */
	dib9000_write_word(state, 1024 + offs, 0x000f);
	dib9000_write_word(state, 1025 + offs, 0);
	dib9000_write_word(state, 1031 + offs, key);

	dprintk("going to download %dB of microcode\n", len);
	if (dib9000_write16_noinc(state, 1026 + offs, (u8 *) code, (u16) len) != 0) {
		dprintk("error while downloading microcode for RISC %c\n", 'A' + risc_id);
		return -EIO;
	}

	dprintk("Microcode for RISC %c loaded\n", 'A' + risc_id);

	return 0;
}

static int dib9000_mbx_host_init(struct dib9000_state *state, u8 risc_id)
{
	u16 mbox_offs;
	u16 reset_reg;
	u16 tries = 1000;

	if (risc_id == 1)
		mbox_offs = 16;
	else
		mbox_offs = 0;

	/* Reset mailbox  */
	dib9000_write_word(state, 1027 + mbox_offs, 0x8000);

	/* Read reset status */
	do {
		reset_reg = dib9000_read_word(state, 1027 + mbox_offs);
		msleep(100);
	} while ((reset_reg & 0x8000) && --tries);

	if (reset_reg & 0x8000) {
		dprintk("MBX: init ERROR, no response from RISC %c\n", 'A' + risc_id);
		return -EIO;
	}
	dprintk("MBX: initialized\n");
	return 0;
}

#define MAX_MAILBOX_TRY 100
static int dib9000_mbx_send_attr(struct dib9000_state *state, u8 id, u16 * data, u8 len, u16 attr)
{
	u8 *d, b[2];
	u16 tmp;
	u16 size;
	u32 i;
	int ret = 0;

	if (!state->platform.risc.fw_is_running)
		return -EINVAL;

	if (mutex_lock_interruptible(&state->platform.risc.mbx_if_lock) < 0) {
		dprintk("could not get the lock\n");
		return -EINTR;
	}
	tmp = MAX_MAILBOX_TRY;
	do {
		size = dib9000_read_word_attr(state, 1043, attr) & 0xff;
		if ((size + len + 1) > MBX_MAX_WORDS && --tmp) {
			dprintk("MBX: RISC mbx full, retrying\n");
			msleep(100);
		} else
			break;
	} while (1);

	/*dprintk( "MBX: size: %d\n", size); */

	if (tmp == 0) {
		ret = -EINVAL;
		goto out;
	}
#ifdef DUMP_MSG
	dprintk("--> %02x %d %*ph\n", id, len + 1, len, data);
#endif

	/* byte-order conversion - works on big (where it is not necessary) or little endian */
	d = (u8 *) data;
	for (i = 0; i < len; i++) {
		tmp = data[i];
		*d++ = tmp >> 8;
		*d++ = tmp & 0xff;
	}

	/* write msg */
	b[0] = id;
	b[1] = len + 1;
	if (dib9000_write16_noinc_attr(state, 1045, b, 2, attr) != 0 || dib9000_write16_noinc_attr(state, 1045, (u8 *) data, len * 2, attr) != 0) {
		ret = -EIO;
		goto out;
	}

	/* update register nb_mes_in_RX */
	ret = (u8) dib9000_write_word_attr(state, 1043, 1 << 14, attr);

out:
	mutex_unlock(&state->platform.risc.mbx_if_lock);

	return ret;
}

static u8 dib9000_mbx_read(struct dib9000_state *state, u16 * data, u8 risc_id, u16 attr)
{
#ifdef DUMP_MSG
	u16 *d = data;
#endif

	u16 tmp, i;
	u8 size;
	u8 mc_base;

	if (!state->platform.risc.fw_is_running)
		return 0;

	if (mutex_lock_interruptible(&state->platform.risc.mbx_if_lock) < 0) {
		dprintk("could not get the lock\n");
		return 0;
	}
	if (risc_id == 1)
		mc_base = 16;
	else
		mc_base = 0;

	/* Length and type in the first word */
	*data = dib9000_read_word_attr(state, 1029 + mc_base, attr);

	size = *data & 0xff;
	if (size <= MBX_MAX_WORDS) {
		data++;
		size--;		/* Initial word already read */

		dib9000_read16_noinc_attr(state, 1029 + mc_base, (u8 *) data, size * 2, attr);

		/* to word conversion */
		for (i = 0; i < size; i++) {
			tmp = *data;
			*data = (tmp >> 8) | (tmp << 8);
			data++;
		}

#ifdef DUMP_MSG
		dprintk("<--\n");
		for (i = 0; i < size + 1; i++)
			dprintk("%04x\n", d[i]);
		dprintk("\n");
#endif
	} else {
		dprintk("MBX: message is too big for message cache (%d), flushing message\n", size);
		size--;		/* Initial word already read */
		while (size--)
			dib9000_read16_noinc_attr(state, 1029 + mc_base, (u8 *) data, 2, attr);
	}
	/* Update register nb_mes_in_TX */
	dib9000_write_word_attr(state, 1028 + mc_base, 1 << 14, attr);

	mutex_unlock(&state->platform.risc.mbx_if_lock);

	return size + 1;
}

static int dib9000_risc_debug_buf(struct dib9000_state *state, u16 * data, u8 size)
{
	u32 ts = data[1] << 16 | data[0];
	char *b = (char *)&data[2];

	b[2 * (size - 2) - 1] = '\0';	/* Bullet proof the buffer */
	if (*b == '~') {
		b++;
		dprintk("%s\n", b);
	} else
		dprintk("RISC%d: %d.%04d %s\n",
			state->fe_id,
			ts / 10000, ts % 10000, *b ? b : "<empty>");
	return 1;
}

static int dib9000_mbx_fetch_to_cache(struct dib9000_state *state, u16 attr)
{
	int i;
	u8 size;
	u16 *block;
	/* find a free slot */
	for (i = 0; i < DIB9000_MSG_CACHE_SIZE; i++) {
		block = state->platform.risc.message_cache[i];
		if (*block == 0) {
			size = dib9000_mbx_read(state, block, 1, attr);

/*                      dprintk( "MBX: fetched %04x message to cache\n", *block); */

			switch (*block >> 8) {
			case IN_MSG_DEBUG_BUF:
				dib9000_risc_debug_buf(state, block + 1, size);	/* debug-messages are going to be printed right away */
				*block = 0;	/* free the block */
				break;
#if 0
			case IN_MSG_DATA:	/* FE-TRACE */
				dib9000_risc_data_process(state, block + 1, size);
				*block = 0;
				break;
#endif
			default:
				break;
			}

			return 1;
		}
	}
	dprintk("MBX: no free cache-slot found for new message...\n");
	return -1;
}

static u8 dib9000_mbx_count(struct dib9000_state *state, u8 risc_id, u16 attr)
{
	if (risc_id == 0)
		return (u8) (dib9000_read_word_attr(state, 1028, attr) >> 10) & 0x1f;	/* 5 bit field */
	else
		return (u8) (dib9000_read_word_attr(state, 1044, attr) >> 8) & 0x7f;	/* 7 bit field */
}

static int dib9000_mbx_process(struct dib9000_state *state, u16 attr)
{
	int ret = 0;

	if (!state->platform.risc.fw_is_running)
		return -1;

	if (mutex_lock_interruptible(&state->platform.risc.mbx_lock) < 0) {
		dprintk("could not get the lock\n");
		return -1;
	}

	if (dib9000_mbx_count(state, 1, attr))	/* 1=RiscB */
		ret = dib9000_mbx_fetch_to_cache(state, attr);

	dib9000_read_word_attr(state, 1229, attr);	/* Clear the IRQ */
/*      if (tmp) */
/*              dprintk( "cleared IRQ: %x\n", tmp); */
	mutex_unlock(&state->platform.risc.mbx_lock);

	return ret;
}

static int dib9000_mbx_get_message_attr(struct dib9000_state *state, u16 id, u16 * msg, u8 * size, u16 attr)
{
	u8 i;
	u16 *block;
	u16 timeout = 30;

	*msg = 0;
	do {
		/* dib9000_mbx_get_from_cache(); */
		for (i = 0; i < DIB9000_MSG_CACHE_SIZE; i++) {
			block = state->platform.risc.message_cache[i];
			if ((*block >> 8) == id) {
				*size = (*block & 0xff) - 1;
				memcpy(msg, block + 1, (*size) * 2);
				*block = 0;	/* free the block */
				i = 0;	/* signal that we found a message */
				break;
			}
		}

		if (i == 0)
			break;

		if (dib9000_mbx_process(state, attr) == -1)	/* try to fetch one message - if any */
			return -1;

	} while (--timeout);

	if (timeout == 0) {
		dprintk("waiting for message %d timed out\n", id);
		return -1;
	}

	return i == 0;
}

static int dib9000_risc_check_version(struct dib9000_state *state)
{
	u8 r[4];
	u8 size;
	u16 fw_version = 0;

	if (dib9000_mbx_send(state, OUT_MSG_REQ_VERSION, &fw_version, 1) != 0)
		return -EIO;

	if (dib9000_mbx_get_message(state, IN_MSG_VERSION, (u16 *) r, &size) < 0)
		return -EIO;

	fw_version = (r[0] << 8) | r[1];
	dprintk("RISC: ver: %d.%02d (IC: %d)\n", fw_version >> 10, fw_version & 0x3ff, (r[2] << 8) | r[3]);

	if ((fw_version >> 10) != 7)
		return -EINVAL;

	switch (fw_version & 0x3ff) {
	case 11:
	case 12:
	case 14:
	case 15:
	case 16:
	case 17:
		break;
	default:
		dprintk("RISC: invalid firmware version");
		return -EINVAL;
	}

	dprintk("RISC: valid firmware version");
	return 0;
}

static int dib9000_fw_boot(struct dib9000_state *state, const u8 * codeA, u32 lenA, const u8 * codeB, u32 lenB)
{
	/* Reconfig pool mac ram */
	dib9000_write_word(state, 1225, 0x02);	/* A: 8k C, 4 k D - B: 32k C 6 k D - IRAM 96k */
	dib9000_write_word(state, 1226, 0x05);

	/* Toggles IP crypto to Host APB interface. */
	dib9000_write_word(state, 1542, 1);

	/* Set jump and no jump in the dma box */
	dib9000_write_word(state, 1074, 0);
	dib9000_write_word(state, 1075, 0);

	/* Set MAC as APB Master. */
	dib9000_write_word(state, 1237, 0);

	/* Reset the RISCs */
	if (codeA != NULL)
		dib9000_write_word(state, 1024, 2);
	else
		dib9000_write_word(state, 1024, 15);
	if (codeB != NULL)
		dib9000_write_word(state, 1040, 2);

	if (codeA != NULL)
		dib9000_firmware_download(state, 0, 0x1234, codeA, lenA);
	if (codeB != NULL)
		dib9000_firmware_download(state, 1, 0x1234, codeB, lenB);

	/* Run the RISCs */
	if (codeA != NULL)
		dib9000_write_word(state, 1024, 0);
	if (codeB != NULL)
		dib9000_write_word(state, 1040, 0);

	if (codeA != NULL)
		if (dib9000_mbx_host_init(state, 0) != 0)
			return -EIO;
	if (codeB != NULL)
		if (dib9000_mbx_host_init(state, 1) != 0)
			return -EIO;

	msleep(100);
	state->platform.risc.fw_is_running = 1;

	if (dib9000_risc_check_version(state) != 0)
		return -EINVAL;

	state->platform.risc.memcmd = 0xff;
	return 0;
}

static u16 dib9000_identify(struct i2c_device *client)
{
	u16 value;

	value = dib9000_i2c_read16(client, 896);
	if (value != 0x01b3) {
		dprintk("wrong Vendor ID (0x%x)\n", value);
		return 0;
	}

	value = dib9000_i2c_read16(client, 897);
	if (value != 0x4000 && value != 0x4001 && value != 0x4002 && value != 0x4003 && value != 0x4004 && value != 0x4005) {
		dprintk("wrong Device ID (0x%x)\n", value);
		return 0;
	}

	/* protect this driver to be used with 7000PC */
	if (value == 0x4000 && dib9000_i2c_read16(client, 769) == 0x4000) {
		dprintk("this driver does not work with DiB7000PC\n");
		return 0;
	}

	switch (value) {
	case 0x4000:
		dprintk("found DiB7000MA/PA/MB/PB\n");
		break;
	case 0x4001:
		dprintk("found DiB7000HC\n");
		break;
	case 0x4002:
		dprintk("found DiB7000MC\n");
		break;
	case 0x4003:
		dprintk("found DiB9000A\n");
		break;
	case 0x4004:
		dprintk("found DiB9000H\n");
		break;
	case 0x4005:
		dprintk("found DiB9000M\n");
		break;
	}

	return value;
}

static void dib9000_set_power_mode(struct dib9000_state *state, enum dib9000_power_mode mode)
{
	/* by default everything is going to be powered off */
	u16 reg_903 = 0x3fff, reg_904 = 0xffff, reg_905 = 0xffff, reg_906;
	u8 offset;

	if (state->revision == 0x4003 || state->revision == 0x4004 || state->revision == 0x4005)
		offset = 1;
	else
		offset = 0;

	reg_906 = dib9000_read_word(state, 906 + offset) | 0x3;	/* keep settings for RISC */

	/* now, depending on the requested mode, we power on */
	switch (mode) {
		/* power up everything in the demod */
	case DIB9000_POWER_ALL:
		reg_903 = 0x0000;
		reg_904 = 0x0000;
		reg_905 = 0x0000;
		reg_906 = 0x0000;
		break;

		/* just leave power on the control-interfaces: GPIO and (I2C or SDIO or SRAM) */
	case DIB9000_POWER_INTERFACE_ONLY:	/* TODO power up either SDIO or I2C or SRAM */
		reg_905 &= ~((1 << 7) | (1 << 6) | (1 << 5) | (1 << 2));
		break;

	case DIB9000_POWER_INTERF_ANALOG_AGC:
		reg_903 &= ~((1 << 15) | (1 << 14) | (1 << 11) | (1 << 10));
		reg_905 &= ~((1 << 7) | (1 << 6) | (1 << 5) | (1 << 4) | (1 << 2));
		reg_906 &= ~((1 << 0));
		break;

	case DIB9000_POWER_COR4_DINTLV_ICIRM_EQUAL_CFROD:
		reg_903 = 0x0000;
		reg_904 = 0x801f;
		reg_905 = 0x0000;
		reg_906 &= ~((1 << 0));
		break;

	case DIB9000_POWER_COR4_CRY_ESRAM_MOUT_NUD:
		reg_903 = 0x0000;
		reg_904 = 0x8000;
		reg_905 = 0x010b;
		reg_906 &= ~((1 << 0));
		break;
	default:
	case DIB9000_POWER_NO:
		break;
	}

	/* always power down unused parts */
	if (!state->platform.host.mobile_mode)
		reg_904 |= (1 << 7) | (1 << 6) | (1 << 4) | (1 << 2) | (1 << 1);

	/* P_sdio_select_clk = 0 on MC and after */
	if (state->revision != 0x4000)
		reg_906 <<= 1;

	dib9000_write_word(state, 903 + offset, reg_903);
	dib9000_write_word(state, 904 + offset, reg_904);
	dib9000_write_word(state, 905 + offset, reg_905);
	dib9000_write_word(state, 906 + offset, reg_906);
}

static int dib9000_fw_reset(struct dvb_frontend *fe)
{
	struct dib9000_state *state = fe->demodulator_priv;

	dib9000_write_word(state, 1817, 0x0003);

	dib9000_write_word(state, 1227, 1);
	dib9000_write_word(state, 1227, 0);

	switch ((state->revision = dib9000_identify(&state->i2c))) {
	case 0x4003:
	case 0x4004:
	case 0x4005:
		state->reg_offs = 1;
		break;
	default:
		return -EINVAL;
	}

	/* reset the i2c-master to use the host interface */
	dibx000_reset_i2c_master(&state->i2c_master);

	dib9000_set_power_mode(state, DIB9000_POWER_ALL);

	/* unforce divstr regardless whether i2c enumeration was done or not */
	dib9000_write_word(state, 1794, dib9000_read_word(state, 1794) & ~(1 << 1));
	dib9000_write_word(state, 1796, 0);
	dib9000_write_word(state, 1805, 0x805);

	/* restart all parts */
	dib9000_write_word(state, 898, 0xffff);
	dib9000_write_word(state, 899, 0xffff);
	dib9000_write_word(state, 900, 0x0001);
	dib9000_write_word(state, 901, 0xff19);
	dib9000_write_word(state, 902, 0x003c);

	dib9000_write_word(state, 898, 0);
	dib9000_write_word(state, 899, 0);
	dib9000_write_word(state, 900, 0);
	dib9000_write_word(state, 901, 0);
	dib9000_write_word(state, 902, 0);

	dib9000_write_word(state, 911, state->chip.d9.cfg.if_drives);

	dib9000_set_power_mode(state, DIB9000_POWER_INTERFACE_ONLY);

	return 0;
}

static int dib9000_risc_apb_access_read(struct dib9000_state *state, u32 address, u16 attribute, const u8 * tx, u32 txlen, u8 * b, u32 len)
{
	u16 mb[10];
	u8 i, s;

	if (address >= 1024 || !state->platform.risc.fw_is_running)
		return -EINVAL;

	/* dprintk( "APB access thru rd fw %d %x\n", address, attribute); */

	mb[0] = (u16) address;
	mb[1] = len / 2;
	dib9000_mbx_send_attr(state, OUT_MSG_BRIDGE_APB_R, mb, 2, attribute);
	switch (dib9000_mbx_get_message_attr(state, IN_MSG_END_BRIDGE_APB_RW, mb, &s, attribute)) {
	case 1:
		s--;
		for (i = 0; i < s; i++) {
			b[i * 2] = (mb[i + 1] >> 8) & 0xff;
			b[i * 2 + 1] = (mb[i + 1]) & 0xff;
		}
		return 0;
	default:
		return -EIO;
	}
	return -EIO;
}

static int dib9000_risc_apb_access_write(struct dib9000_state *state, u32 address, u16 attribute, const u8 * b, u32 len)
{
	u16 mb[10];
	u8 s, i;

	if (address >= 1024 || !state->platform.risc.fw_is_running)
		return -EINVAL;

	if (len > 18)
		return -EINVAL;

	/* dprintk( "APB access thru wr fw %d %x\n", address, attribute); */

	mb[0] = (u16)address;
	for (i = 0; i + 1 < len; i += 2)
		mb[1 + i / 2] = b[i] << 8 | b[i + 1];
	if (len & 1)
		mb[1 + len / 2] = b[len - 1] << 8;

	dib9000_mbx_send_attr(state, OUT_MSG_BRIDGE_APB_W, mb, (3 + len) / 2, attribute);
	return dib9000_mbx_get_message_attr(state, IN_MSG_END_BRIDGE_APB_RW, mb, &s, attribute) == 1 ? 0 : -EINVAL;
}

static int dib9000_fw_memmbx_sync(struct dib9000_state *state, u8 i)
{
	u8 index_loop = 10;

	if (!state->platform.risc.fw_is_running)
		return 0;
	dib9000_risc_mem_write(state, FE_MM_RW_SYNC, &i);
	do {
		dib9000_risc_mem_read(state, FE_MM_RW_SYNC, state->i2c_read_buffer, 1);
	} while (state->i2c_read_buffer[0] && index_loop--);

	if (index_loop > 0)
		return 0;
	return -EIO;
}

static int dib9000_fw_init(struct dib9000_state *state)
{
	struct dibGPIOFunction *f;
	u16 b[40] = { 0 };
	u8 i;
	u8 size;

	if (dib9000_fw_boot(state, NULL, 0, state->chip.d9.cfg.microcode_B_fe_buffer, state->chip.d9.cfg.microcode_B_fe_size) != 0)
		return -EIO;

	/* initialize the firmware */
	for (i = 0; i < ARRAY_SIZE(state->chip.d9.cfg.gpio_function); i++) {
		f = &state->chip.d9.cfg.gpio_function[i];
		if (f->mask) {
			switch (f->function) {
			case BOARD_GPIO_FUNCTION_COMPONENT_ON:
				b[0] = (u16) f->mask;
				b[1] = (u16) f->direction;
				b[2] = (u16) f->value;
				break;
			case BOARD_GPIO_FUNCTION_COMPONENT_OFF:
				b[3] = (u16) f->mask;
				b[4] = (u16) f->direction;
				b[5] = (u16) f->value;
				break;
			}
		}
	}
	if (dib9000_mbx_send(state, OUT_MSG_CONF_GPIO, b, 15) != 0)
		return -EIO;

	/* subband */
	b[0] = state->chip.d9.cfg.subband.size;	/* type == 0 -> GPIO - PWM not yet supported */
	for (i = 0; i < state->chip.d9.cfg.subband.size; i++) {
		b[1 + i * 4] = state->chip.d9.cfg.subband.subband[i].f_mhz;
		b[2 + i * 4] = (u16) state->chip.d9.cfg.subband.subband[i].gpio.mask;
		b[3 + i * 4] = (u16) state->chip.d9.cfg.subband.subband[i].gpio.direction;
		b[4 + i * 4] = (u16) state->chip.d9.cfg.subband.subband[i].gpio.value;
	}
	b[1 + i * 4] = 0;	/* fe_id */
	if (dib9000_mbx_send(state, OUT_MSG_SUBBAND_SEL, b, 2 + 4 * i) != 0)
		return -EIO;

	/* 0 - id, 1 - no_of_frontends */
	b[0] = (0 << 8) | 1;
	/* 0 = i2c-address demod, 0 = tuner */
	b[1] = (0 << 8) | (0);
	b[2] = (u16) (((state->chip.d9.cfg.xtal_clock_khz * 1000) >> 16) & 0xffff);
	b[3] = (u16) (((state->chip.d9.cfg.xtal_clock_khz * 1000)) & 0xffff);
	b[4] = (u16) ((state->chip.d9.cfg.vcxo_timer >> 16) & 0xffff);
	b[5] = (u16) ((state->chip.d9.cfg.vcxo_timer) & 0xffff);
	b[6] = (u16) ((state->chip.d9.cfg.timing_frequency >> 16) & 0xffff);
	b[7] = (u16) ((state->chip.d9.cfg.timing_frequency) & 0xffff);
	b[29] = state->chip.d9.cfg.if_drives;
	if (dib9000_mbx_send(state, OUT_MSG_INIT_DEMOD, b, ARRAY_SIZE(b)) != 0)
		return -EIO;

	if (dib9000_mbx_send(state, OUT_MSG_FE_FW_DL, NULL, 0) != 0)
		return -EIO;

	if (dib9000_mbx_get_message(state, IN_MSG_FE_FW_DL_DONE, b, &size) < 0)
		return -EIO;

	if (size > ARRAY_SIZE(b)) {
		dprintk("error : firmware returned %dbytes needed but the used buffer has only %dbytes\n Firmware init ABORTED", size,
			(int)ARRAY_SIZE(b));
		return -EINVAL;
	}

	for (i = 0; i < size; i += 2) {
		state->platform.risc.fe_mm[i / 2].addr = b[i + 0];
		state->platform.risc.fe_mm[i / 2].size = b[i + 1];
	}

	return 0;
}

static void dib9000_fw_set_channel_head(struct dib9000_state *state)
{
	u8 b[9];
	u32 freq = state->fe[0]->dtv_property_cache.frequency / 1000;
	if (state->fe_id % 2)
		freq += 101;

	b[0] = (u8) ((freq >> 0) & 0xff);
	b[1] = (u8) ((freq >> 8) & 0xff);
	b[2] = (u8) ((freq >> 16) & 0xff);
	b[3] = (u8) ((freq >> 24) & 0xff);
	b[4] = (u8) ((state->fe[0]->dtv_property_cache.bandwidth_hz / 1000 >> 0) & 0xff);
	b[5] = (u8) ((state->fe[0]->dtv_property_cache.bandwidth_hz / 1000 >> 8) & 0xff);
	b[6] = (u8) ((state->fe[0]->dtv_property_cache.bandwidth_hz / 1000 >> 16) & 0xff);
	b[7] = (u8) ((state->fe[0]->dtv_property_cache.bandwidth_hz / 1000 >> 24) & 0xff);
	b[8] = 0x80;		/* do not wait for CELL ID when doing autosearch */
	if (state->fe[0]->dtv_property_cache.delivery_system == SYS_DVBT)
		b[8] |= 1;
	dib9000_risc_mem_write(state, FE_MM_W_CHANNEL_HEAD, b);
}

static int dib9000_fw_get_channel(struct dvb_frontend *fe)
{
	struct dib9000_state *state = fe->demodulator_priv;
	struct dibDVBTChannel {
		s8 spectrum_inversion;

		s8 nfft;
		s8 guard;
		s8 constellation;

		s8 hrch;
		s8 alpha;
		s8 code_rate_hp;
		s8 code_rate_lp;
		s8 select_hp;

		s8 intlv_native;
	};
	struct dibDVBTChannel *ch;
	int ret = 0;

	if (mutex_lock_interruptible(&state->platform.risc.mem_mbx_lock) < 0) {
		dprintk("could not get the lock\n");
		return -EINTR;
	}
	if (dib9000_fw_memmbx_sync(state, FE_SYNC_CHANNEL) < 0) {
		ret = -EIO;
		goto error;
	}

	dib9000_risc_mem_read(state, FE_MM_R_CHANNEL_UNION,
			state->i2c_read_buffer, sizeof(struct dibDVBTChannel));
	ch = (struct dibDVBTChannel *)state->i2c_read_buffer;


	switch (ch->spectrum_inversion & 0x7) {
	case 1:
		state->fe[0]->dtv_property_cache.inversion = INVERSION_ON;
		break;
	case 0:
		state->fe[0]->dtv_property_cache.inversion = INVERSION_OFF;
		break;
	default:
	case -1:
		state->fe[0]->dtv_property_cache.inversion = INVERSION_AUTO;
		break;
	}
	switch (ch->nfft) {
	case 0:
		state->fe[0]->dtv_property_cache.transmission_mode = TRANSMISSION_MODE_2K;
		break;
	case 2:
		state->fe[0]->dtv_property_cache.transmission_mode = TRANSMISSION_MODE_4K;
		break;
	case 1:
		state->fe[0]->dtv_property_cache.transmission_mode = TRANSMISSION_MODE_8K;
		break;
	default:
	case -1:
		state->fe[0]->dtv_property_cache.transmission_mode = TRANSMISSION_MODE_AUTO;
		break;
	}
	switch (ch->guard) {
	case 0:
		state->fe[0]->dtv_property_cache.guard_interval = GUARD_INTERVAL_1_32;
		break;
	case 1:
		state->fe[0]->dtv_property_cache.guard_interval = GUARD_INTERVAL_1_16;
		break;
	case 2:
		state->fe[0]->dtv_property_cache.guard_interval = GUARD_INTERVAL_1_8;
		break;
	case 3:
		state->fe[0]->dtv_property_cache.guard_interval = GUARD_INTERVAL_1_4;
		break;
	default:
	case -1:
		state->fe[0]->dtv_property_cache.guard_interval = GUARD_INTERVAL_AUTO;
		break;
	}
	switch (ch->constellation) {
	case 2:
		state->fe[0]->dtv_property_cache.modulation = QAM_64;
		break;
	case 1:
		state->fe[0]->dtv_property_cache.modulation = QAM_16;
		break;
	case 0:
		state->fe[0]->dtv_property_cache.modulation = QPSK;
		break;
	default:
	case -1:
		state->fe[0]->dtv_property_cache.modulation = QAM_AUTO;
		break;
	}
	switch (ch->hrch) {
	case 0:
		state->fe[0]->dtv_property_cache.hierarchy = HIERARCHY_NONE;
		break;
	case 1:
		state->fe[0]->dtv_property_cache.hierarchy = HIERARCHY_1;
		break;
	default:
	case -1:
		state->fe[0]->dtv_property_cache.hierarchy = HIERARCHY_AUTO;
		break;
	}
	switch (ch->code_rate_hp) {
	case 1:
		state->fe[0]->dtv_property_cache.code_rate_HP = FEC_1_2;
		break;
	case 2:
		state->fe[0]->dtv_property_cache.code_rate_HP = FEC_2_3;
		break;
	case 3:
		state->fe[0]->dtv_property_cache.code_rate_HP = FEC_3_4;
		break;
	case 5:
		state->fe[0]->dtv_property_cache.code_rate_HP = FEC_5_6;
		break;
	case 7:
		state->fe[0]->dtv_property_cache.code_rate_HP = FEC_7_8;
		break;
	default:
	case -1:
		state->fe[0]->dtv_property_cache.code_rate_HP = FEC_AUTO;
		break;
	}
	switch (ch->code_rate_lp) {
	case 1:
		state->fe[0]->dtv_property_cache.code_rate_LP = FEC_1_2;
		break;
	case 2:
		state->fe[0]->dtv_property_cache.code_rate_LP = FEC_2_3;
		break;
	case 3:
		state->fe[0]->dtv_property_cache.code_rate_LP = FEC_3_4;
		break;
	case 5:
		state->fe[0]->dtv_property_cache.code_rate_LP = FEC_5_6;
		break;
	case 7:
		state->fe[0]->dtv_property_cache.code_rate_LP = FEC_7_8;
		break;
	default:
	case -1:
		state->fe[0]->dtv_property_cache.code_rate_LP = FEC_AUTO;
		break;
	}

error:
	mutex_unlock(&state->platform.risc.mem_mbx_lock);
	return ret;
}

static int dib9000_fw_set_channel_union(struct dvb_frontend *fe)
{
	struct dib9000_state *state = fe->demodulator_priv;
	struct dibDVBTChannel {
		s8 spectrum_inversion;

		s8 nfft;
		s8 guard;
		s8 constellation;

		s8 hrch;
		s8 alpha;
		s8 code_rate_hp;
		s8 code_rate_lp;
		s8 select_hp;

		s8 intlv_native;
	};
	struct dibDVBTChannel ch;

	switch (state->fe[0]->dtv_property_cache.inversion) {
	case INVERSION_ON:
		ch.spectrum_inversion = 1;
		break;
	case INVERSION_OFF:
		ch.spectrum_inversion = 0;
		break;
	default:
	case INVERSION_AUTO:
		ch.spectrum_inversion = -1;
		break;
	}
	switch (state->fe[0]->dtv_property_cache.transmission_mode) {
	case TRANSMISSION_MODE_2K:
		ch.nfft = 0;
		break;
	case TRANSMISSION_MODE_4K:
		ch.nfft = 2;
		break;
	case TRANSMISSION_MODE_8K:
		ch.nfft = 1;
		break;
	default:
	case TRANSMISSION_MODE_AUTO:
		ch.nfft = 1;
		break;
	}
	switch (state->fe[0]->dtv_property_cache.guard_interval) {
	case GUARD_INTERVAL_1_32:
		ch.guard = 0;
		break;
	case GUARD_INTERVAL_1_16:
		ch.guard = 1;
		break;
	case GUARD_INTERVAL_1_8:
		ch.guard = 2;
		break;
	case GUARD_INTERVAL_1_4:
		ch.guard = 3;
		break;
	default:
	case GUARD_INTERVAL_AUTO:
		ch.guard = -1;
		break;
	}
	switch (state->fe[0]->dtv_property_cache.modulation) {
	case QAM_64:
		ch.constellation = 2;
		break;
	case QAM_16:
		ch.constellation = 1;
		break;
	case QPSK:
		ch.constellation = 0;
		break;
	default:
	case QAM_AUTO:
		ch.constellation = -1;
		break;
	}
	switch (state->fe[0]->dtv_property_cache.hierarchy) {
	case HIERARCHY_NONE:
		ch.hrch = 0;
		break;
	case HIERARCHY_1:
	case HIERARCHY_2:
	case HIERARCHY_4:
		ch.hrch = 1;
		break;
	default:
	case HIERARCHY_AUTO:
		ch.hrch = -1;
		break;
	}
	ch.alpha = 1;
	switch (state->fe[0]->dtv_property_cache.code_rate_HP) {
	case FEC_1_2:
		ch.code_rate_hp = 1;
		break;
	case FEC_2_3:
		ch.code_rate_hp = 2;
		break;
	case FEC_3_4:
		ch.code_rate_hp = 3;
		break;
	case FEC_5_6:
		ch.code_rate_hp = 5;
		break;
	case FEC_7_8:
		ch.code_rate_hp = 7;
		break;
	default:
	case FEC_AUTO:
		ch.code_rate_hp = -1;
		break;
	}
	switch (state->fe[0]->dtv_property_cache.code_rate_LP) {
	case FEC_1_2:
		ch.code_rate_lp = 1;
		break;
	case FEC_2_3:
		ch.code_rate_lp = 2;
		break;
	case FEC_3_4:
		ch.code_rate_lp = 3;
		break;
	case FEC_5_6:
		ch.code_rate_lp = 5;
		break;
	case FEC_7_8:
		ch.code_rate_lp = 7;
		break;
	default:
	case FEC_AUTO:
		ch.code_rate_lp = -1;
		break;
	}
	ch.select_hp = 1;
	ch.intlv_native = 1;

	dib9000_risc_mem_write(state, FE_MM_W_CHANNEL_UNION, (u8 *) &ch);

	return 0;
}

static int dib9000_fw_tune(struct dvb_frontend *fe)
{
	struct dib9000_state *state = fe->demodulator_priv;
	int ret = 10, search = state->channel_status.status == CHANNEL_STATUS_PARAMETERS_UNKNOWN;
	s8 i;

	switch (state->tune_state) {
	case CT_DEMOD_START:
		dib9000_fw_set_channel_head(state);

		/* write the channel context - a channel is initialized to 0, so it is OK */
		dib9000_risc_mem_write(state, FE_MM_W_CHANNEL_CONTEXT, (u8 *) fe_info);
		dib9000_risc_mem_write(state, FE_MM_W_FE_INFO, (u8 *) fe_info);

		if (search)
			dib9000_mbx_send(state, OUT_MSG_FE_CHANNEL_SEARCH, NULL, 0);
		else {
			dib9000_fw_set_channel_union(fe);
			dib9000_mbx_send(state, OUT_MSG_FE_CHANNEL_TUNE, NULL, 0);
		}
		state->tune_state = CT_DEMOD_STEP_1;
		break;
	case CT_DEMOD_STEP_1:
		if (search)
			dib9000_risc_mem_read(state, FE_MM_R_CHANNEL_SEARCH_STATE, state->i2c_read_buffer, 1);
		else
			dib9000_risc_mem_read(state, FE_MM_R_CHANNEL_TUNE_STATE, state->i2c_read_buffer, 1);
		i = (s8)state->i2c_read_buffer[0];
		switch (i) {	/* something happened */
		case 0:
			break;
		case -2:	/* tps locks are "slower" than MPEG locks -> even in autosearch data is OK here */
			if (search)
				state->status = FE_STATUS_DEMOD_SUCCESS;
			else {
				state->tune_state = CT_DEMOD_STOP;
				state->status = FE_STATUS_LOCKED;
			}
			break;
		default:
			state->status = FE_STATUS_TUNE_FAILED;
			state->tune_state = CT_DEMOD_STOP;
			break;
		}
		break;
	default:
		ret = FE_CALLBACK_TIME_NEVER;
		break;
	}

	return ret;
}

static int dib9000_fw_set_diversity_in(struct dvb_frontend *fe, int onoff)
{
	struct dib9000_state *state = fe->demodulator_priv;
	u16 mode = (u16) onoff;
	return dib9000_mbx_send(state, OUT_MSG_ENABLE_DIVERSITY, &mode, 1);
}

static int dib9000_fw_set_output_mode(struct dvb_frontend *fe, int mode)
{
	struct dib9000_state *state = fe->demodulator_priv;
	u16 outreg, smo_mode;

	dprintk("setting output mode for demod %p to %d\n", fe, mode);

	switch (mode) {
	case OUTMODE_MPEG2_PAR_GATED_CLK:
		outreg = (1 << 10);	/* 0x0400 */
		break;
	case OUTMODE_MPEG2_PAR_CONT_CLK:
		outreg = (1 << 10) | (1 << 6);	/* 0x0440 */
		break;
	case OUTMODE_MPEG2_SERIAL:
		outreg = (1 << 10) | (2 << 6) | (0 << 1);	/* 0x0482 */
		break;
	case OUTMODE_DIVERSITY:
		outreg = (1 << 10) | (4 << 6);	/* 0x0500 */
		break;
	case OUTMODE_MPEG2_FIFO:
		outreg = (1 << 10) | (5 << 6);
		break;
	case OUTMODE_HIGH_Z:
		outreg = 0;
		break;
	default:
		dprintk("Unhandled output_mode passed to be set for demod %p\n", &state->fe[0]);
		return -EINVAL;
	}

	dib9000_write_word(state, 1795, outreg);

	switch (mode) {
	case OUTMODE_MPEG2_PAR_GATED_CLK:
	case OUTMODE_MPEG2_PAR_CONT_CLK:
	case OUTMODE_MPEG2_SERIAL:
	case OUTMODE_MPEG2_FIFO:
		smo_mode = (dib9000_read_word(state, 295) & 0x0010) | (1 << 1);
		if (state->chip.d9.cfg.output_mpeg2_in_188_bytes)
			smo_mode |= (1 << 5);
		dib9000_write_word(state, 295, smo_mode);
		break;
	}

	outreg = to_fw_output_mode(mode);
	return dib9000_mbx_send(state, OUT_MSG_SET_OUTPUT_MODE, &outreg, 1);
}

static int dib9000_tuner_xfer(struct i2c_adapter *i2c_adap, struct i2c_msg msg[], int num)
{
	struct dib9000_state *state = i2c_get_adapdata(i2c_adap);
	u16 i, len, t, index_msg;

	for (index_msg = 0; index_msg < num; index_msg++) {
		if (msg[index_msg].flags & I2C_M_RD) {	/* read */
			len = msg[index_msg].len;
			if (len > 16)
				len = 16;

			if (dib9000_read_word(state, 790) != 0)
				dprintk("TunerITF: read busy\n");

			dib9000_write_word(state, 784, (u16) (msg[index_msg].addr));
			dib9000_write_word(state, 787, (len / 2) - 1);
			dib9000_write_word(state, 786, 1);	/* start read */

			i = 1000;
			while (dib9000_read_word(state, 790) != (len / 2) && i)
				i--;

			if (i == 0)
				dprintk("TunerITF: read failed\n");

			for (i = 0; i < len; i += 2) {
				t = dib9000_read_word(state, 785);
				msg[index_msg].buf[i] = (t >> 8) & 0xff;
				msg[index_msg].buf[i + 1] = (t) & 0xff;
			}
			if (dib9000_read_word(state, 790) != 0)
				dprintk("TunerITF: read more data than expected\n");
		} else {
			i = 1000;
			while (dib9000_read_word(state, 789) && i)
				i--;
			if (i == 0)
				dprintk("TunerITF: write busy\n");

			len = msg[index_msg].len;
			if (len > 16)
				len = 16;

			for (i = 0; i < len; i += 2)
				dib9000_write_word(state, 785, (msg[index_msg].buf[i] << 8) | msg[index_msg].buf[i + 1]);
			dib9000_write_word(state, 784, (u16) msg[index_msg].addr);
			dib9000_write_word(state, 787, (len / 2) - 1);
			dib9000_write_word(state, 786, 0);	/* start write */

			i = 1000;
			while (dib9000_read_word(state, 791) > 0 && i)
				i--;
			if (i == 0)
				dprintk("TunerITF: write failed\n");
		}
	}
	return num;
}

int dib9000_fw_set_component_bus_speed(struct dvb_frontend *fe, u16 speed)
{
	struct dib9000_state *state = fe->demodulator_priv;

	state->component_bus_speed = speed;
	return 0;
}
EXPORT_SYMBOL(dib9000_fw_set_component_bus_speed);

static int dib9000_fw_component_bus_xfer(struct i2c_adapter *i2c_adap, struct i2c_msg msg[], int num)
{
	struct dib9000_state *state = i2c_get_adapdata(i2c_adap);
	u8 type = 0;		/* I2C */
	u8 port = DIBX000_I2C_INTERFACE_GPIO_3_4;
	u16 scl = state->component_bus_speed;	/* SCL frequency */
	struct dib9000_fe_memory_map *m = &state->platform.risc.fe_mm[FE_MM_RW_COMPONENT_ACCESS_BUFFER];
	u8 p[13] = { 0 };

	p[0] = type;
	p[1] = port;
	p[2] = msg[0].addr << 1;

	p[3] = (u8) scl & 0xff;	/* scl */
	p[4] = (u8) (scl >> 8);

	p[7] = 0;
	p[8] = 0;

	p[9] = (u8) (msg[0].len);
	p[10] = (u8) (msg[0].len >> 8);
	if ((num > 1) && (msg[1].flags & I2C_M_RD)) {
		p[11] = (u8) (msg[1].len);
		p[12] = (u8) (msg[1].len >> 8);
	} else {
		p[11] = 0;
		p[12] = 0;
	}

	if (mutex_lock_interruptible(&state->platform.risc.mem_mbx_lock) < 0) {
		dprintk("could not get the lock\n");
		return 0;
	}

	dib9000_risc_mem_write(state, FE_MM_W_COMPONENT_ACCESS, p);

	{			/* write-part */
		dib9000_risc_mem_setup_cmd(state, m->addr, msg[0].len, 0);
		dib9000_risc_mem_write_chunks(state, msg[0].buf, msg[0].len);
	}

	/* do the transaction */
	if (dib9000_fw_memmbx_sync(state, FE_SYNC_COMPONENT_ACCESS) < 0) {
		mutex_unlock(&state->platform.risc.mem_mbx_lock);
		return 0;
	}

	/* read back any possible result */
	if ((num > 1) && (msg[1].flags & I2C_M_RD))
		dib9000_risc_mem_read(state, FE_MM_RW_COMPONENT_ACCESS_BUFFER, msg[1].buf, msg[1].len);

	mutex_unlock(&state->platform.risc.mem_mbx_lock);

	return num;
}

static u32 dib9000_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static const struct i2c_algorithm dib9000_tuner_algo = {
	.master_xfer = dib9000_tuner_xfer,
	.functionality = dib9000_i2c_func,
};

static const struct i2c_algorithm dib9000_component_bus_algo = {
	.master_xfer = dib9000_fw_component_bus_xfer,
	.functionality = dib9000_i2c_func,
};

struct i2c_adapter *dib9000_get_tuner_interface(struct dvb_frontend *fe)
{
	struct dib9000_state *st = fe->demodulator_priv;
	return &st->tuner_adap;
}
EXPORT_SYMBOL(dib9000_get_tuner_interface);

struct i2c_adapter *dib9000_get_component_bus_interface(struct dvb_frontend *fe)
{
	struct dib9000_state *st = fe->demodulator_priv;
	return &st->component_bus;
}
EXPORT_SYMBOL(dib9000_get_component_bus_interface);

struct i2c_adapter *dib9000_get_i2c_master(struct dvb_frontend *fe, enum dibx000_i2c_interface intf, int gating)
{
	struct dib9000_state *st = fe->demodulator_priv;
	return dibx000_get_i2c_adapter(&st->i2c_master, intf, gating);
}
EXPORT_SYMBOL(dib9000_get_i2c_master);

int dib9000_set_i2c_adapter(struct dvb_frontend *fe, struct i2c_adapter *i2c)
{
	struct dib9000_state *st = fe->demodulator_priv;

	st->i2c.i2c_adap = i2c;
	return 0;
}
EXPORT_SYMBOL(dib9000_set_i2c_adapter);

static int dib9000_cfg_gpio(struct dib9000_state *st, u8 num, u8 dir, u8 val)
{
	st->gpio_dir = dib9000_read_word(st, 773);
	st->gpio_dir &= ~(1 << num);	/* reset the direction bit */
	st->gpio_dir |= (dir & 0x1) << num;	/* set the new direction */
	dib9000_write_word(st, 773, st->gpio_dir);

	st->gpio_val = dib9000_read_word(st, 774);
	st->gpio_val &= ~(1 << num);	/* reset the direction bit */
	st->gpio_val |= (val & 0x01) << num;	/* set the new value */
	dib9000_write_word(st, 774, st->gpio_val);

	dprintk("gpio dir: %04x: gpio val: %04x\n", st->gpio_dir, st->gpio_val);

	return 0;
}

int dib9000_set_gpio(struct dvb_frontend *fe, u8 num, u8 dir, u8 val)
{
	struct dib9000_state *state = fe->demodulator_priv;
	return dib9000_cfg_gpio(state, num, dir, val);
}
EXPORT_SYMBOL(dib9000_set_gpio);

int dib9000_fw_pid_filter_ctrl(struct dvb_frontend *fe, u8 onoff)
{
	struct dib9000_state *state = fe->demodulator_priv;
	u16 val;
	int ret;

	if ((state->pid_ctrl_index != -2) && (state->pid_ctrl_index < 9)) {
		/* postpone the pid filtering cmd */
		dprintk("pid filter cmd postpone\n");
		state->pid_ctrl_index++;
		state->pid_ctrl[state->pid_ctrl_index].cmd = DIB9000_PID_FILTER_CTRL;
		state->pid_ctrl[state->pid_ctrl_index].onoff = onoff;
		return 0;
	}

	if (mutex_lock_interruptible(&state->demod_lock) < 0) {
		dprintk("could not get the lock\n");
		return -EINTR;
	}

	val = dib9000_read_word(state, 294 + 1) & 0xffef;
	val |= (onoff & 0x1) << 4;

	dprintk("PID filter enabled %d\n", onoff);
	ret = dib9000_write_word(state, 294 + 1, val);
	mutex_unlock(&state->demod_lock);
	return ret;

}
EXPORT_SYMBOL(dib9000_fw_pid_filter_ctrl);

int dib9000_fw_pid_filter(struct dvb_frontend *fe, u8 id, u16 pid, u8 onoff)
{
	struct dib9000_state *state = fe->demodulator_priv;
	int ret;

	if (state->pid_ctrl_index != -2) {
		/* postpone the pid filtering cmd */
		dprintk("pid filter postpone\n");
		if (state->pid_ctrl_index < 9) {
			state->pid_ctrl_index++;
			state->pid_ctrl[state->pid_ctrl_index].cmd = DIB9000_PID_FILTER;
			state->pid_ctrl[state->pid_ctrl_index].id = id;
			state->pid_ctrl[state->pid_ctrl_index].pid = pid;
			state->pid_ctrl[state->pid_ctrl_index].onoff = onoff;
		} else
			dprintk("can not add any more pid ctrl cmd\n");
		return 0;
	}

	if (mutex_lock_interruptible(&state->demod_lock) < 0) {
		dprintk("could not get the lock\n");
		return -EINTR;
	}
	dprintk("Index %x, PID %d, OnOff %d\n", id, pid, onoff);
	ret = dib9000_write_word(state, 300 + 1 + id,
			onoff ? (1 << 13) | pid : 0);
	mutex_unlock(&state->demod_lock);
	return ret;
}
EXPORT_SYMBOL(dib9000_fw_pid_filter);

int dib9000_firmware_post_pll_init(struct dvb_frontend *fe)
{
	struct dib9000_state *state = fe->demodulator_priv;
	return dib9000_fw_init(state);
}
EXPORT_SYMBOL(dib9000_firmware_post_pll_init);

static void dib9000_release(struct dvb_frontend *demod)
{
	struct dib9000_state *st = demod->demodulator_priv;
	u8 index_frontend;

	for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (st->fe[index_frontend] != NULL); index_frontend++)
		dvb_frontend_detach(st->fe[index_frontend]);

	dibx000_exit_i2c_master(&st->i2c_master);

	i2c_del_adapter(&st->tuner_adap);
	i2c_del_adapter(&st->component_bus);
	kfree(st->fe[0]);
	kfree(st);
}

static int dib9000_wakeup(struct dvb_frontend *fe)
{
	return 0;
}

static int dib9000_sleep(struct dvb_frontend *fe)
{
	struct dib9000_state *state = fe->demodulator_priv;
	u8 index_frontend;
	int ret = 0;

	if (mutex_lock_interruptible(&state->demod_lock) < 0) {
		dprintk("could not get the lock\n");
		return -EINTR;
	}
	for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
		ret = state->fe[index_frontend]->ops.sleep(state->fe[index_frontend]);
		if (ret < 0)
			goto error;
	}
	ret = dib9000_mbx_send(state, OUT_MSG_FE_SLEEP, NULL, 0);

error:
	mutex_unlock(&state->demod_lock);
	return ret;
}

static int dib9000_fe_get_tune_settings(struct dvb_frontend *fe, struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 1000;
	return 0;
}

static int dib9000_get_frontend(struct dvb_frontend *fe,
				struct dtv_frontend_properties *c)
{
	struct dib9000_state *state = fe->demodulator_priv;
	u8 index_frontend, sub_index_frontend;
	enum fe_status stat;
	int ret = 0;

	if (state->get_frontend_internal == 0) {
		if (mutex_lock_interruptible(&state->demod_lock) < 0) {
			dprintk("could not get the lock\n");
			return -EINTR;
		}
	}

	for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
		state->fe[index_frontend]->ops.read_status(state->fe[index_frontend], &stat);
		if (stat & FE_HAS_SYNC) {
			dprintk("TPS lock on the slave%i\n", index_frontend);

			/* synchronize the cache with the other frontends */
			state->fe[index_frontend]->ops.get_frontend(state->fe[index_frontend], c);
			for (sub_index_frontend = 0; (sub_index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[sub_index_frontend] != NULL);
			     sub_index_frontend++) {
				if (sub_index_frontend != index_frontend) {
					state->fe[sub_index_frontend]->dtv_property_cache.modulation =
					    state->fe[index_frontend]->dtv_property_cache.modulation;
					state->fe[sub_index_frontend]->dtv_property_cache.inversion =
					    state->fe[index_frontend]->dtv_property_cache.inversion;
					state->fe[sub_index_frontend]->dtv_property_cache.transmission_mode =
					    state->fe[index_frontend]->dtv_property_cache.transmission_mode;
					state->fe[sub_index_frontend]->dtv_property_cache.guard_interval =
					    state->fe[index_frontend]->dtv_property_cache.guard_interval;
					state->fe[sub_index_frontend]->dtv_property_cache.hierarchy =
					    state->fe[index_frontend]->dtv_property_cache.hierarchy;
					state->fe[sub_index_frontend]->dtv_property_cache.code_rate_HP =
					    state->fe[index_frontend]->dtv_property_cache.code_rate_HP;
					state->fe[sub_index_frontend]->dtv_property_cache.code_rate_LP =
					    state->fe[index_frontend]->dtv_property_cache.code_rate_LP;
					state->fe[sub_index_frontend]->dtv_property_cache.rolloff =
					    state->fe[index_frontend]->dtv_property_cache.rolloff;
				}
			}
			ret = 0;
			goto return_value;
		}
	}

	/* get the channel from master chip */
	ret = dib9000_fw_get_channel(fe);
	if (ret != 0)
		goto return_value;

	/* synchronize the cache with the other frontends */
	for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
		state->fe[index_frontend]->dtv_property_cache.inversion = c->inversion;
		state->fe[index_frontend]->dtv_property_cache.transmission_mode = c->transmission_mode;
		state->fe[index_frontend]->dtv_property_cache.guard_interval = c->guard_interval;
		state->fe[index_frontend]->dtv_property_cache.modulation = c->modulation;
		state->fe[index_frontend]->dtv_property_cache.hierarchy = c->hierarchy;
		state->fe[index_frontend]->dtv_property_cache.code_rate_HP = c->code_rate_HP;
		state->fe[index_frontend]->dtv_property_cache.code_rate_LP = c->code_rate_LP;
		state->fe[index_frontend]->dtv_property_cache.rolloff = c->rolloff;
	}
	ret = 0;

return_value:
	if (state->get_frontend_internal == 0)
		mutex_unlock(&state->demod_lock);
	return ret;
}

static int dib9000_set_tune_state(struct dvb_frontend *fe, enum frontend_tune_state tune_state)
{
	struct dib9000_state *state = fe->demodulator_priv;
	state->tune_state = tune_state;
	if (tune_state == CT_DEMOD_START)
		state->status = FE_STATUS_TUNE_PENDING;

	return 0;
}

static u32 dib9000_get_status(struct dvb_frontend *fe)
{
	struct dib9000_state *state = fe->demodulator_priv;
	return state->status;
}

static int dib9000_set_channel_status(struct dvb_frontend *fe, struct dvb_frontend_parametersContext *channel_status)
{
	struct dib9000_state *state = fe->demodulator_priv;

	memcpy(&state->channel_status, channel_status, sizeof(struct dvb_frontend_parametersContext));
	return 0;
}

static int dib9000_set_frontend(struct dvb_frontend *fe)
{
	struct dib9000_state *state = fe->demodulator_priv;
	int sleep_time, sleep_time_slave;
	u32 frontend_status;
	u8 nbr_pending, exit_condition, index_frontend, index_frontend_success;
	struct dvb_frontend_parametersContext channel_status;

	/* check that the correct parameters are set */
	if (state->fe[0]->dtv_property_cache.frequency == 0) {
		dprintk("dib9000: must specify frequency\n");
		return 0;
	}

	if (state->fe[0]->dtv_property_cache.bandwidth_hz == 0) {
		dprintk("dib9000: must specify bandwidth\n");
		return 0;
	}

	state->pid_ctrl_index = -1; /* postpone the pid filtering cmd */
	if (mutex_lock_interruptible(&state->demod_lock) < 0) {
		dprintk("could not get the lock\n");
		return 0;
	}

	fe->dtv_property_cache.delivery_system = SYS_DVBT;

	/* set the master status */
	if (state->fe[0]->dtv_property_cache.transmission_mode == TRANSMISSION_MODE_AUTO ||
	    state->fe[0]->dtv_property_cache.guard_interval == GUARD_INTERVAL_AUTO ||
	    state->fe[0]->dtv_property_cache.modulation == QAM_AUTO ||
	    state->fe[0]->dtv_property_cache.code_rate_HP == FEC_AUTO) {
		/* no channel specified, autosearch the channel */
		state->channel_status.status = CHANNEL_STATUS_PARAMETERS_UNKNOWN;
	} else
		state->channel_status.status = CHANNEL_STATUS_PARAMETERS_SET;

	/* set mode and status for the different frontends */
	for (index_frontend = 0; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
		dib9000_fw_set_diversity_in(state->fe[index_frontend], 1);

		/* synchronization of the cache */
		memcpy(&state->fe[index_frontend]->dtv_property_cache, &fe->dtv_property_cache, sizeof(struct dtv_frontend_properties));

		state->fe[index_frontend]->dtv_property_cache.delivery_system = SYS_DVBT;
		dib9000_fw_set_output_mode(state->fe[index_frontend], OUTMODE_HIGH_Z);

		dib9000_set_channel_status(state->fe[index_frontend], &state->channel_status);
		dib9000_set_tune_state(state->fe[index_frontend], CT_DEMOD_START);
	}

	/* actual tune */
	exit_condition = 0;	/* 0: tune pending; 1: tune failed; 2:tune success */
	index_frontend_success = 0;
	do {
		sleep_time = dib9000_fw_tune(state->fe[0]);
		for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
			sleep_time_slave = dib9000_fw_tune(state->fe[index_frontend]);
			if (sleep_time == FE_CALLBACK_TIME_NEVER)
				sleep_time = sleep_time_slave;
			else if ((sleep_time_slave != FE_CALLBACK_TIME_NEVER) && (sleep_time_slave > sleep_time))
				sleep_time = sleep_time_slave;
		}
		if (sleep_time != FE_CALLBACK_TIME_NEVER)
			msleep(sleep_time / 10);
		else
			break;

		nbr_pending = 0;
		exit_condition = 0;
		index_frontend_success = 0;
		for (index_frontend = 0; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
			frontend_status = -dib9000_get_status(state->fe[index_frontend]);
			if (frontend_status > -FE_STATUS_TUNE_PENDING) {
				exit_condition = 2;	/* tune success */
				index_frontend_success = index_frontend;
				break;
			}
			if (frontend_status == -FE_STATUS_TUNE_PENDING)
				nbr_pending++;	/* some frontends are still tuning */
		}
		if ((exit_condition != 2) && (nbr_pending == 0))
			exit_condition = 1;	/* if all tune are done and no success, exit: tune failed */

	} while (exit_condition == 0);

	/* check the tune result */
	if (exit_condition == 1) {	/* tune failed */
		dprintk("tune failed\n");
		mutex_unlock(&state->demod_lock);
		/* tune failed; put all the pid filtering cmd to junk */
		state->pid_ctrl_index = -1;
		return 0;
	}

	dprintk("tune success on frontend%i\n", index_frontend_success);

	/* synchronize all the channel cache */
	state->get_frontend_internal = 1;
	dib9000_get_frontend(state->fe[0], &state->fe[0]->dtv_property_cache);
	state->get_frontend_internal = 0;

	/* retune the other frontends with the found channel */
	channel_status.status = CHANNEL_STATUS_PARAMETERS_SET;
	for (index_frontend = 0; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
		/* only retune the frontends which was not tuned success */
		if (index_frontend != index_frontend_success) {
			dib9000_set_channel_status(state->fe[index_frontend], &channel_status);
			dib9000_set_tune_state(state->fe[index_frontend], CT_DEMOD_START);
		}
	}
	do {
		sleep_time = FE_CALLBACK_TIME_NEVER;
		for (index_frontend = 0; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
			if (index_frontend != index_frontend_success) {
				sleep_time_slave = dib9000_fw_tune(state->fe[index_frontend]);
				if (sleep_time == FE_CALLBACK_TIME_NEVER)
					sleep_time = sleep_time_slave;
				else if ((sleep_time_slave != FE_CALLBACK_TIME_NEVER) && (sleep_time_slave > sleep_time))
					sleep_time = sleep_time_slave;
			}
		}
		if (sleep_time != FE_CALLBACK_TIME_NEVER)
			msleep(sleep_time / 10);
		else
			break;

		nbr_pending = 0;
		for (index_frontend = 0; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
			if (index_frontend != index_frontend_success) {
				frontend_status = -dib9000_get_status(state->fe[index_frontend]);
				if ((index_frontend != index_frontend_success) && (frontend_status == -FE_STATUS_TUNE_PENDING))
					nbr_pending++;	/* some frontends are still tuning */
			}
		}
	} while (nbr_pending != 0);

	/* set the output mode */
	dib9000_fw_set_output_mode(state->fe[0], state->chip.d9.cfg.output_mode);
	for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++)
		dib9000_fw_set_output_mode(state->fe[index_frontend], OUTMODE_DIVERSITY);

	/* turn off the diversity for the last frontend */
	dib9000_fw_set_diversity_in(state->fe[index_frontend - 1], 0);

	mutex_unlock(&state->demod_lock);
	if (state->pid_ctrl_index >= 0) {
		u8 index_pid_filter_cmd;
		u8 pid_ctrl_index = state->pid_ctrl_index;

		state->pid_ctrl_index = -2;
		for (index_pid_filter_cmd = 0;
				index_pid_filter_cmd <= pid_ctrl_index;
				index_pid_filter_cmd++) {
			if (state->pid_ctrl[index_pid_filter_cmd].cmd == DIB9000_PID_FILTER_CTRL)
				dib9000_fw_pid_filter_ctrl(state->fe[0],
						state->pid_ctrl[index_pid_filter_cmd].onoff);
			else if (state->pid_ctrl[index_pid_filter_cmd].cmd == DIB9000_PID_FILTER)
				dib9000_fw_pid_filter(state->fe[0],
						state->pid_ctrl[index_pid_filter_cmd].id,
						state->pid_ctrl[index_pid_filter_cmd].pid,
						state->pid_ctrl[index_pid_filter_cmd].onoff);
		}
	}
	/* do not postpone any more the pid filtering */
	state->pid_ctrl_index = -2;

	return 0;
}

static u16 dib9000_read_lock(struct dvb_frontend *fe)
{
	struct dib9000_state *state = fe->demodulator_priv;

	return dib9000_read_word(state, 535);
}

static int dib9000_read_status(struct dvb_frontend *fe, enum fe_status *stat)
{
	struct dib9000_state *state = fe->demodulator_priv;
	u8 index_frontend;
	u16 lock = 0, lock_slave = 0;

	if (mutex_lock_interruptible(&state->demod_lock) < 0) {
		dprintk("could not get the lock\n");
		return -EINTR;
	}
	for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++)
		lock_slave |= dib9000_read_lock(state->fe[index_frontend]);

	lock = dib9000_read_word(state, 535);

	*stat = 0;

	if ((lock & 0x8000) || (lock_slave & 0x8000))
		*stat |= FE_HAS_SIGNAL;
	if ((lock & 0x3000) || (lock_slave & 0x3000))
		*stat |= FE_HAS_CARRIER;
	if ((lock & 0x0100) || (lock_slave & 0x0100))
		*stat |= FE_HAS_VITERBI;
	if (((lock & 0x0038) == 0x38) || ((lock_slave & 0x0038) == 0x38))
		*stat |= FE_HAS_SYNC;
	if ((lock & 0x0008) || (lock_slave & 0x0008))
		*stat |= FE_HAS_LOCK;

	mutex_unlock(&state->demod_lock);

	return 0;
}

static int dib9000_read_ber(struct dvb_frontend *fe, u32 * ber)
{
	struct dib9000_state *state = fe->demodulator_priv;
	u16 *c;
	int ret = 0;

	if (mutex_lock_interruptible(&state->demod_lock) < 0) {
		dprintk("could not get the lock\n");
		return -EINTR;
	}
	if (mutex_lock_interruptible(&state->platform.risc.mem_mbx_lock) < 0) {
		dprintk("could not get the lock\n");
		ret = -EINTR;
		goto error;
	}
	if (dib9000_fw_memmbx_sync(state, FE_SYNC_CHANNEL) < 0) {
		mutex_unlock(&state->platform.risc.mem_mbx_lock);
		ret = -EIO;
		goto error;
	}
	dib9000_risc_mem_read(state, FE_MM_R_FE_MONITOR,
			state->i2c_read_buffer, 16 * 2);
	mutex_unlock(&state->platform.risc.mem_mbx_lock);

	c = (u16 *)state->i2c_read_buffer;

	*ber = c[10] << 16 | c[11];

error:
	mutex_unlock(&state->demod_lock);
	return ret;
}

static int dib9000_read_signal_strength(struct dvb_frontend *fe, u16 * strength)
{
	struct dib9000_state *state = fe->demodulator_priv;
	u8 index_frontend;
	u16 *c = (u16 *)state->i2c_read_buffer;
	u16 val;
	int ret = 0;

	if (mutex_lock_interruptible(&state->demod_lock) < 0) {
		dprintk("could not get the lock\n");
		return -EINTR;
	}
	*strength = 0;
	for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++) {
		state->fe[index_frontend]->ops.read_signal_strength(state->fe[index_frontend], &val);
		if (val > 65535 - *strength)
			*strength = 65535;
		else
			*strength += val;
	}

	if (mutex_lock_interruptible(&state->platform.risc.mem_mbx_lock) < 0) {
		dprintk("could not get the lock\n");
		ret = -EINTR;
		goto error;
	}
	if (dib9000_fw_memmbx_sync(state, FE_SYNC_CHANNEL) < 0) {
		mutex_unlock(&state->platform.risc.mem_mbx_lock);
		ret = -EIO;
		goto error;
	}
	dib9000_risc_mem_read(state, FE_MM_R_FE_MONITOR, (u8 *) c, 16 * 2);
	mutex_unlock(&state->platform.risc.mem_mbx_lock);

	val = 65535 - c[4];
	if (val > 65535 - *strength)
		*strength = 65535;
	else
		*strength += val;

error:
	mutex_unlock(&state->demod_lock);
	return ret;
}

static u32 dib9000_get_snr(struct dvb_frontend *fe)
{
	struct dib9000_state *state = fe->demodulator_priv;
	u16 *c = (u16 *)state->i2c_read_buffer;
	u32 n, s, exp;
	u16 val;

	if (mutex_lock_interruptible(&state->platform.risc.mem_mbx_lock) < 0) {
		dprintk("could not get the lock\n");
		return 0;
	}
	if (dib9000_fw_memmbx_sync(state, FE_SYNC_CHANNEL) < 0) {
		mutex_unlock(&state->platform.risc.mem_mbx_lock);
		return 0;
	}
	dib9000_risc_mem_read(state, FE_MM_R_FE_MONITOR, (u8 *) c, 16 * 2);
	mutex_unlock(&state->platform.risc.mem_mbx_lock);

	val = c[7];
	n = (val >> 4) & 0xff;
	exp = ((val & 0xf) << 2);
	val = c[8];
	exp += ((val >> 14) & 0x3);
	if ((exp & 0x20) != 0)
		exp -= 0x40;
	n <<= exp + 16;

	s = (val >> 6) & 0xFF;
	exp = (val & 0x3F);
	if ((exp & 0x20) != 0)
		exp -= 0x40;
	s <<= exp + 16;

	if (n > 0) {
		u32 t = (s / n) << 16;
		return t + ((s << 16) - n * t) / n;
	}
	return 0xffffffff;
}

static int dib9000_read_snr(struct dvb_frontend *fe, u16 * snr)
{
	struct dib9000_state *state = fe->demodulator_priv;
	u8 index_frontend;
	u32 snr_master;

	if (mutex_lock_interruptible(&state->demod_lock) < 0) {
		dprintk("could not get the lock\n");
		return -EINTR;
	}
	snr_master = dib9000_get_snr(fe);
	for (index_frontend = 1; (index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL); index_frontend++)
		snr_master += dib9000_get_snr(state->fe[index_frontend]);

	if ((snr_master >> 16) != 0) {
		snr_master = 10 * intlog10(snr_master >> 16);
		*snr = snr_master / ((1 << 24) / 10);
	} else
		*snr = 0;

	mutex_unlock(&state->demod_lock);

	return 0;
}

static int dib9000_read_unc_blocks(struct dvb_frontend *fe, u32 * unc)
{
	struct dib9000_state *state = fe->demodulator_priv;
	u16 *c = (u16 *)state->i2c_read_buffer;
	int ret = 0;

	if (mutex_lock_interruptible(&state->demod_lock) < 0) {
		dprintk("could not get the lock\n");
		return -EINTR;
	}
	if (mutex_lock_interruptible(&state->platform.risc.mem_mbx_lock) < 0) {
		dprintk("could not get the lock\n");
		ret = -EINTR;
		goto error;
	}
	if (dib9000_fw_memmbx_sync(state, FE_SYNC_CHANNEL) < 0) {
		mutex_unlock(&state->platform.risc.mem_mbx_lock);
		ret = -EIO;
		goto error;
	}
	dib9000_risc_mem_read(state, FE_MM_R_FE_MONITOR, (u8 *) c, 16 * 2);
	mutex_unlock(&state->platform.risc.mem_mbx_lock);

	*unc = c[12];

error:
	mutex_unlock(&state->demod_lock);
	return ret;
}

int dib9000_i2c_enumeration(struct i2c_adapter *i2c, int no_of_demods, u8 default_addr, u8 first_addr)
{
	int k = 0, ret = 0;
	u8 new_addr = 0;
	struct i2c_device client = {.i2c_adap = i2c };

	client.i2c_write_buffer = kzalloc(4 * sizeof(u8), GFP_KERNEL);
	if (!client.i2c_write_buffer) {
		dprintk("%s: not enough memory\n", __func__);
		return -ENOMEM;
	}
	client.i2c_read_buffer = kzalloc(4 * sizeof(u8), GFP_KERNEL);
	if (!client.i2c_read_buffer) {
		dprintk("%s: not enough memory\n", __func__);
		ret = -ENOMEM;
		goto error_memory;
	}

	client.i2c_addr = default_addr + 16;
	dib9000_i2c_write16(&client, 1796, 0x0);

	for (k = no_of_demods - 1; k >= 0; k--) {
		/* designated i2c address */
		new_addr = first_addr + (k << 1);
		client.i2c_addr = default_addr;

		dib9000_i2c_write16(&client, 1817, 3);
		dib9000_i2c_write16(&client, 1796, 0);
		dib9000_i2c_write16(&client, 1227, 1);
		dib9000_i2c_write16(&client, 1227, 0);

		client.i2c_addr = new_addr;
		dib9000_i2c_write16(&client, 1817, 3);
		dib9000_i2c_write16(&client, 1796, 0);
		dib9000_i2c_write16(&client, 1227, 1);
		dib9000_i2c_write16(&client, 1227, 0);

		if (dib9000_identify(&client) == 0) {
			client.i2c_addr = default_addr;
			if (dib9000_identify(&client) == 0) {
				dprintk("DiB9000 #%d: not identified\n", k);
				ret = -EIO;
				goto error;
			}
		}

		dib9000_i2c_write16(&client, 1795, (1 << 10) | (4 << 6));
		dib9000_i2c_write16(&client, 1794, (new_addr << 2) | 2);

		dprintk("IC %d initialized (to i2c_address 0x%x)\n", k, new_addr);
	}

	for (k = 0; k < no_of_demods; k++) {
		new_addr = first_addr | (k << 1);
		client.i2c_addr = new_addr;

		dib9000_i2c_write16(&client, 1794, (new_addr << 2));
		dib9000_i2c_write16(&client, 1795, 0);
	}

error:
	kfree(client.i2c_read_buffer);
error_memory:
	kfree(client.i2c_write_buffer);

	return ret;
}
EXPORT_SYMBOL(dib9000_i2c_enumeration);

int dib9000_set_slave_frontend(struct dvb_frontend *fe, struct dvb_frontend *fe_slave)
{
	struct dib9000_state *state = fe->demodulator_priv;
	u8 index_frontend = 1;

	while ((index_frontend < MAX_NUMBER_OF_FRONTENDS) && (state->fe[index_frontend] != NULL))
		index_frontend++;
	if (index_frontend < MAX_NUMBER_OF_FRONTENDS) {
		dprintk("set slave fe %p to index %i\n", fe_slave, index_frontend);
		state->fe[index_frontend] = fe_slave;
		return 0;
	}

	dprintk("too many slave frontend\n");
	return -ENOMEM;
}
EXPORT_SYMBOL(dib9000_set_slave_frontend);

struct dvb_frontend *dib9000_get_slave_frontend(struct dvb_frontend *fe, int slave_index)
{
	struct dib9000_state *state = fe->demodulator_priv;

	if (slave_index >= MAX_NUMBER_OF_FRONTENDS)
		return NULL;
	return state->fe[slave_index];
}
EXPORT_SYMBOL(dib9000_get_slave_frontend);

static const struct dvb_frontend_ops dib9000_ops;
struct dvb_frontend *dib9000_attach(struct i2c_adapter *i2c_adap, u8 i2c_addr, const struct dib9000_config *cfg)
{
	struct dvb_frontend *fe;
	struct dib9000_state *st;
	st = kzalloc(sizeof(struct dib9000_state), GFP_KERNEL);
	if (st == NULL)
		return NULL;
	fe = kzalloc(sizeof(struct dvb_frontend), GFP_KERNEL);
	if (fe == NULL) {
		kfree(st);
		return NULL;
	}

	memcpy(&st->chip.d9.cfg, cfg, sizeof(struct dib9000_config));
	st->i2c.i2c_adap = i2c_adap;
	st->i2c.i2c_addr = i2c_addr;
	st->i2c.i2c_write_buffer = st->i2c_write_buffer;
	st->i2c.i2c_read_buffer = st->i2c_read_buffer;

	st->gpio_dir = DIB9000_GPIO_DEFAULT_DIRECTIONS;
	st->gpio_val = DIB9000_GPIO_DEFAULT_VALUES;
	st->gpio_pwm_pos = DIB9000_GPIO_DEFAULT_PWM_POS;

	mutex_init(&st->platform.risc.mbx_if_lock);
	mutex_init(&st->platform.risc.mbx_lock);
	mutex_init(&st->platform.risc.mem_lock);
	mutex_init(&st->platform.risc.mem_mbx_lock);
	mutex_init(&st->demod_lock);
	st->get_frontend_internal = 0;

	st->pid_ctrl_index = -2;

	st->fe[0] = fe;
	fe->demodulator_priv = st;
	memcpy(&st->fe[0]->ops, &dib9000_ops, sizeof(struct dvb_frontend_ops));

	/* Ensure the output mode remains at the previous default if it's
	 * not specifically set by the caller.
	 */
	if ((st->chip.d9.cfg.output_mode != OUTMODE_MPEG2_SERIAL) && (st->chip.d9.cfg.output_mode != OUTMODE_MPEG2_PAR_GATED_CLK))
		st->chip.d9.cfg.output_mode = OUTMODE_MPEG2_FIFO;

	if (dib9000_identify(&st->i2c) == 0)
		goto error;

	dibx000_init_i2c_master(&st->i2c_master, DIB7000MC, st->i2c.i2c_adap, st->i2c.i2c_addr);

	st->tuner_adap.dev.parent = i2c_adap->dev.parent;
	strncpy(st->tuner_adap.name, "DIB9000_FW TUNER ACCESS", sizeof(st->tuner_adap.name));
	st->tuner_adap.algo = &dib9000_tuner_algo;
	st->tuner_adap.algo_data = NULL;
	i2c_set_adapdata(&st->tuner_adap, st);
	if (i2c_add_adapter(&st->tuner_adap) < 0)
		goto error;

	st->component_bus.dev.parent = i2c_adap->dev.parent;
	strncpy(st->component_bus.name, "DIB9000_FW COMPONENT BUS ACCESS", sizeof(st->component_bus.name));
	st->component_bus.algo = &dib9000_component_bus_algo;
	st->component_bus.algo_data = NULL;
	st->component_bus_speed = 340;
	i2c_set_adapdata(&st->component_bus, st);
	if (i2c_add_adapter(&st->component_bus) < 0)
		goto component_bus_add_error;

	dib9000_fw_reset(fe);

	return fe;

component_bus_add_error:
	i2c_del_adapter(&st->tuner_adap);
error:
	kfree(st);
	return NULL;
}
EXPORT_SYMBOL(dib9000_attach);

static const struct dvb_frontend_ops dib9000_ops = {
	.delsys = { SYS_DVBT },
	.info = {
		 .name = "DiBcom 9000",
		 .frequency_min = 44250000,
		 .frequency_max = 867250000,
		 .frequency_stepsize = 62500,
		 .caps = FE_CAN_INVERSION_AUTO |
		 FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		 FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
		 FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
		 FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO | FE_CAN_RECOVER | FE_CAN_HIERARCHY_AUTO,
		 },

	.release = dib9000_release,

	.init = dib9000_wakeup,
	.sleep = dib9000_sleep,

	.set_frontend = dib9000_set_frontend,
	.get_tune_settings = dib9000_fe_get_tune_settings,
	.get_frontend = dib9000_get_frontend,

	.read_status = dib9000_read_status,
	.read_ber = dib9000_read_ber,
	.read_signal_strength = dib9000_read_signal_strength,
	.read_snr = dib9000_read_snr,
	.read_ucblocks = dib9000_read_unc_blocks,
};

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@posteo.de>");
MODULE_AUTHOR("Olivier Grenie <olivier.grenie@parrot.com>");
MODULE_DESCRIPTION("Driver for the DiBcom 9000 COFDM demodulator");
MODULE_LICENSE("GPL");
