/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
#include <subdev/bios.h>
#include <subdev/bios/bit.h>
#include <subdev/bios/bmp.h>
#include <subdev/bios/conn.h>
#include <subdev/bios/dcb.h>
#include <subdev/bios/dp.h>
#include <subdev/bios/gpio.h>
#include <subdev/bios/init.h>
#include <subdev/bios/ramcfg.h>

#include <subdev/devinit.h>
#include <subdev/gpio.h>
#include <subdev/i2c.h>
#include <subdev/vga.h>

#include <linux/kernel.h>

#define bioslog(lvl, fmt, args...) do {                                        \
	nvkm_printk(init->subdev, lvl, info, "0x%08x[%c]: "fmt,                \
		    init->offset, init_exec(init) ?                            \
		    '0' + (init->nested - 1) : ' ', ##args);                   \
} while(0)
#define cont(fmt, args...) do {                                                \
	if (init->subdev->debug >= NV_DBG_TRACE)                               \
		printk(fmt, ##args);                                           \
} while(0)
#define trace(fmt, args...) bioslog(TRACE, fmt, ##args)
#define warn(fmt, args...) bioslog(WARN, fmt, ##args)
#define error(fmt, args...) bioslog(ERROR, fmt, ##args)

/******************************************************************************
 * init parser control flow helpers
 *****************************************************************************/

static inline bool
init_exec(struct nvbios_init *init)
{
	return (init->execute == 1) || ((init->execute & 5) == 5);
}

static inline void
init_exec_set(struct nvbios_init *init, bool exec)
{
	if (exec) init->execute &= 0xfd;
	else      init->execute |= 0x02;
}

static inline void
init_exec_inv(struct nvbios_init *init)
{
	init->execute ^= 0x02;
}

static inline void
init_exec_force(struct nvbios_init *init, bool exec)
{
	if (exec) init->execute |= 0x04;
	else      init->execute &= 0xfb;
}

/******************************************************************************
 * init parser wrappers for normal register/i2c/whatever accessors
 *****************************************************************************/

static inline int
init_or(struct nvbios_init *init)
{
	if (init_exec(init)) {
		if (init->or >= 0)
			return init->or;
		error("script needs OR!!\n");
	}
	return 0;
}

static inline int
init_link(struct nvbios_init *init)
{
	if (init_exec(init)) {
		if (init->link)
			return init->link == 2;
		error("script needs OR link\n");
	}
	return 0;
}

static inline int
init_head(struct nvbios_init *init)
{
	if (init_exec(init)) {
		if (init->head >= 0)
			return init->head;
		error("script needs head\n");
	}
	return 0;
}

static u8
init_conn(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	struct nvbios_connE connE;
	u8  ver, hdr;
	u32 conn;

	if (init_exec(init)) {
		if (init->outp) {
			conn = init->outp->connector;
			conn = nvbios_connEp(bios, conn, &ver, &hdr, &connE);
			if (conn)
				return connE.type;
		}

		error("script needs connector type\n");
	}

	return 0xff;
}

static inline u32
init_nvreg(struct nvbios_init *init, u32 reg)
{
	struct nvkm_devinit *devinit = init->subdev->device->devinit;

	/* C51 (at least) sometimes has the lower bits set which the VBIOS
	 * interprets to mean that access needs to go through certain IO
	 * ports instead.  The NVIDIA binary driver has been seen to access
	 * these through the NV register address, so lets assume we can
	 * do the same
	 */
	reg &= ~0x00000003;

	/* GF8+ display scripts need register addresses mangled a bit to
	 * select a specific CRTC/OR
	 */
	if (init->subdev->device->card_type >= NV_50) {
		if (reg & 0x80000000) {
			reg += init_head(init) * 0x800;
			reg &= ~0x80000000;
		}

		if (reg & 0x40000000) {
			reg += init_or(init) * 0x800;
			reg &= ~0x40000000;
			if (reg & 0x20000000) {
				reg += init_link(init) * 0x80;
				reg &= ~0x20000000;
			}
		}
	}

	if (reg & ~0x00fffffc)
		warn("unknown bits in register 0x%08x\n", reg);

	return nvkm_devinit_mmio(devinit, reg);
}

static u32
init_rd32(struct nvbios_init *init, u32 reg)
{
	struct nvkm_device *device = init->subdev->device;
	reg = init_nvreg(init, reg);
	if (reg != ~0 && init_exec(init))
		return nvkm_rd32(device, reg);
	return 0x00000000;
}

static void
init_wr32(struct nvbios_init *init, u32 reg, u32 val)
{
	struct nvkm_device *device = init->subdev->device;
	reg = init_nvreg(init, reg);
	if (reg != ~0 && init_exec(init))
		nvkm_wr32(device, reg, val);
}

static u32
init_mask(struct nvbios_init *init, u32 reg, u32 mask, u32 val)
{
	struct nvkm_device *device = init->subdev->device;
	reg = init_nvreg(init, reg);
	if (reg != ~0 && init_exec(init)) {
		u32 tmp = nvkm_rd32(device, reg);
		nvkm_wr32(device, reg, (tmp & ~mask) | val);
		return tmp;
	}
	return 0x00000000;
}

static u8
init_rdport(struct nvbios_init *init, u16 port)
{
	if (init_exec(init))
		return nvkm_rdport(init->subdev->device, init->head, port);
	return 0x00;
}

static void
init_wrport(struct nvbios_init *init, u16 port, u8 value)
{
	if (init_exec(init))
		nvkm_wrport(init->subdev->device, init->head, port, value);
}

static u8
init_rdvgai(struct nvbios_init *init, u16 port, u8 index)
{
	struct nvkm_subdev *subdev = init->subdev;
	if (init_exec(init)) {
		int head = init->head < 0 ? 0 : init->head;
		return nvkm_rdvgai(subdev->device, head, port, index);
	}
	return 0x00;
}

static void
init_wrvgai(struct nvbios_init *init, u16 port, u8 index, u8 value)
{
	struct nvkm_device *device = init->subdev->device;

	/* force head 0 for updates to cr44, it only exists on first head */
	if (device->card_type < NV_50) {
		if (port == 0x03d4 && index == 0x44)
			init->head = 0;
	}

	if (init_exec(init)) {
		int head = init->head < 0 ? 0 : init->head;
		nvkm_wrvgai(device, head, port, index, value);
	}

	/* select head 1 if cr44 write selected it */
	if (device->card_type < NV_50) {
		if (port == 0x03d4 && index == 0x44 && value == 3)
			init->head = 1;
	}
}

static struct i2c_adapter *
init_i2c(struct nvbios_init *init, int index)
{
	struct nvkm_i2c *i2c = init->subdev->device->i2c;
	struct nvkm_i2c_bus *bus;

	if (index == 0xff) {
		index = NVKM_I2C_BUS_PRI;
		if (init->outp && init->outp->i2c_upper_default)
			index = NVKM_I2C_BUS_SEC;
	} else
	if (index == 0x80) {
		index = NVKM_I2C_BUS_PRI;
	} else
	if (index == 0x81) {
		index = NVKM_I2C_BUS_SEC;
	}

	bus = nvkm_i2c_bus_find(i2c, index);
	return bus ? &bus->i2c : NULL;
}

static int
init_rdi2cr(struct nvbios_init *init, u8 index, u8 addr, u8 reg)
{
	struct i2c_adapter *adap = init_i2c(init, index);
	if (adap && init_exec(init))
		return nvkm_rdi2cr(adap, addr, reg);
	return -ENODEV;
}

static int
init_wri2cr(struct nvbios_init *init, u8 index, u8 addr, u8 reg, u8 val)
{
	struct i2c_adapter *adap = init_i2c(init, index);
	if (adap && init_exec(init))
		return nvkm_wri2cr(adap, addr, reg, val);
	return -ENODEV;
}

static struct nvkm_i2c_aux *
init_aux(struct nvbios_init *init)
{
	struct nvkm_i2c *i2c = init->subdev->device->i2c;
	if (!init->outp) {
		if (init_exec(init))
			error("script needs output for aux\n");
		return NULL;
	}
	return nvkm_i2c_aux_find(i2c, init->outp->i2c_index);
}

static u8
init_rdauxr(struct nvbios_init *init, u32 addr)
{
	struct nvkm_i2c_aux *aux = init_aux(init);
	u8 data;

	if (aux && init_exec(init)) {
		int ret = nvkm_rdaux(aux, addr, &data, 1);
		if (ret == 0)
			return data;
		trace("auxch read failed with %d\n", ret);
	}

	return 0x00;
}

static int
init_wrauxr(struct nvbios_init *init, u32 addr, u8 data)
{
	struct nvkm_i2c_aux *aux = init_aux(init);
	if (aux && init_exec(init)) {
		int ret = nvkm_wraux(aux, addr, &data, 1);
		if (ret)
			trace("auxch write failed with %d\n", ret);
		return ret;
	}
	return -ENODEV;
}

static void
init_prog_pll(struct nvbios_init *init, u32 id, u32 freq)
{
	struct nvkm_devinit *devinit = init->subdev->device->devinit;
	if (init_exec(init)) {
		int ret = nvkm_devinit_pll_set(devinit, id, freq);
		if (ret)
			warn("failed to prog pll 0x%08x to %dkHz\n", id, freq);
	}
}

/******************************************************************************
 * parsing of bios structures that are required to execute init tables
 *****************************************************************************/

static u16
init_table(struct nvkm_bios *bios, u16 *len)
{
	struct bit_entry bit_I;

	if (!bit_entry(bios, 'I', &bit_I)) {
		*len = bit_I.length;
		return bit_I.offset;
	}

	if (bmp_version(bios) >= 0x0510) {
		*len = 14;
		return bios->bmp_offset + 75;
	}

	return 0x0000;
}

static u16
init_table_(struct nvbios_init *init, u16 offset, const char *name)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u16 len, data = init_table(bios, &len);
	if (data) {
		if (len >= offset + 2) {
			data = nvbios_rd16(bios, data + offset);
			if (data)
				return data;

			warn("%s pointer invalid\n", name);
			return 0x0000;
		}

		warn("init data too short for %s pointer", name);
		return 0x0000;
	}

	warn("init data not found\n");
	return 0x0000;
}

#define init_script_table(b) init_table_((b), 0x00, "script table")
#define init_macro_index_table(b) init_table_((b), 0x02, "macro index table")
#define init_macro_table(b) init_table_((b), 0x04, "macro table")
#define init_condition_table(b) init_table_((b), 0x06, "condition table")
#define init_io_condition_table(b) init_table_((b), 0x08, "io condition table")
#define init_io_flag_condition_table(b) init_table_((b), 0x0a, "io flag conditon table")
#define init_function_table(b) init_table_((b), 0x0c, "function table")
#define init_xlat_table(b) init_table_((b), 0x10, "xlat table");

static u16
init_script(struct nvkm_bios *bios, int index)
{
	struct nvbios_init init = { .subdev = &bios->subdev };
	u16 bmp_ver = bmp_version(bios), data;

	if (bmp_ver && bmp_ver < 0x0510) {
		if (index > 1 || bmp_ver < 0x0100)
			return 0x0000;

		data = bios->bmp_offset + (bmp_ver < 0x0200 ? 14 : 18);
		return nvbios_rd16(bios, data + (index * 2));
	}

	data = init_script_table(&init);
	if (data)
		return nvbios_rd16(bios, data + (index * 2));

	return 0x0000;
}

static u16
init_unknown_script(struct nvkm_bios *bios)
{
	u16 len, data = init_table(bios, &len);
	if (data && len >= 16)
		return nvbios_rd16(bios, data + 14);
	return 0x0000;
}

static u8
init_ram_restrict_group_count(struct nvbios_init *init)
{
	return nvbios_ramcfg_count(init->subdev->device->bios);
}

static u8
init_ram_restrict(struct nvbios_init *init)
{
	/* This appears to be the behaviour of the VBIOS parser, and *is*
	 * important to cache the NV_PEXTDEV_BOOT0 on later chipsets to
	 * avoid fucking up the memory controller (somehow) by reading it
	 * on every INIT_RAM_RESTRICT_ZM_GROUP opcode.
	 *
	 * Preserving the non-caching behaviour on earlier chipsets just
	 * in case *not* re-reading the strap causes similar breakage.
	 */
	if (!init->ramcfg || init->subdev->device->bios->version.major < 0x70)
		init->ramcfg = 0x80000000 | nvbios_ramcfg_index(init->subdev);
	return (init->ramcfg & 0x7fffffff);
}

static u8
init_xlat_(struct nvbios_init *init, u8 index, u8 offset)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u16 table = init_xlat_table(init);
	if (table) {
		u16 data = nvbios_rd16(bios, table + (index * 2));
		if (data)
			return nvbios_rd08(bios, data + offset);
		warn("xlat table pointer %d invalid\n", index);
	}
	return 0x00;
}

/******************************************************************************
 * utility functions used by various init opcode handlers
 *****************************************************************************/

static bool
init_condition_met(struct nvbios_init *init, u8 cond)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u16 table = init_condition_table(init);
	if (table) {
		u32 reg = nvbios_rd32(bios, table + (cond * 12) + 0);
		u32 msk = nvbios_rd32(bios, table + (cond * 12) + 4);
		u32 val = nvbios_rd32(bios, table + (cond * 12) + 8);
		trace("\t[0x%02x] (R[0x%06x] & 0x%08x) == 0x%08x\n",
		      cond, reg, msk, val);
		return (init_rd32(init, reg) & msk) == val;
	}
	return false;
}

static bool
init_io_condition_met(struct nvbios_init *init, u8 cond)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u16 table = init_io_condition_table(init);
	if (table) {
		u16 port = nvbios_rd16(bios, table + (cond * 5) + 0);
		u8 index = nvbios_rd08(bios, table + (cond * 5) + 2);
		u8  mask = nvbios_rd08(bios, table + (cond * 5) + 3);
		u8 value = nvbios_rd08(bios, table + (cond * 5) + 4);
		trace("\t[0x%02x] (0x%04x[0x%02x] & 0x%02x) == 0x%02x\n",
		      cond, port, index, mask, value);
		return (init_rdvgai(init, port, index) & mask) == value;
	}
	return false;
}

static bool
init_io_flag_condition_met(struct nvbios_init *init, u8 cond)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u16 table = init_io_flag_condition_table(init);
	if (table) {
		u16 port = nvbios_rd16(bios, table + (cond * 9) + 0);
		u8 index = nvbios_rd08(bios, table + (cond * 9) + 2);
		u8  mask = nvbios_rd08(bios, table + (cond * 9) + 3);
		u8 shift = nvbios_rd08(bios, table + (cond * 9) + 4);
		u16 data = nvbios_rd16(bios, table + (cond * 9) + 5);
		u8 dmask = nvbios_rd08(bios, table + (cond * 9) + 7);
		u8 value = nvbios_rd08(bios, table + (cond * 9) + 8);
		u8 ioval = (init_rdvgai(init, port, index) & mask) >> shift;
		return (nvbios_rd08(bios, data + ioval) & dmask) == value;
	}
	return false;
}

static inline u32
init_shift(u32 data, u8 shift)
{
	if (shift < 0x80)
		return data >> shift;
	return data << (0x100 - shift);
}

static u32
init_tmds_reg(struct nvbios_init *init, u8 tmds)
{
	/* For mlv < 0x80, it is an index into a table of TMDS base addresses.
	 * For mlv == 0x80 use the "or" value of the dcb_entry indexed by
	 * CR58 for CR57 = 0 to index a table of offsets to the basic
	 * 0x6808b0 address.
	 * For mlv == 0x81 use the "or" value of the dcb_entry indexed by
	 * CR58 for CR57 = 0 to index a table of offsets to the basic
	 * 0x6808b0 address, and then flip the offset by 8.
	 */
	const int pramdac_offset[13] = {
		0, 0, 0x8, 0, 0x2000, 0, 0, 0, 0x2008, 0, 0, 0, 0x2000 };
	const u32 pramdac_table[4] = {
		0x6808b0, 0x6808b8, 0x6828b0, 0x6828b8 };

	if (tmds >= 0x80) {
		if (init->outp) {
			u32 dacoffset = pramdac_offset[init->outp->or];
			if (tmds == 0x81)
				dacoffset ^= 8;
			return 0x6808b0 + dacoffset;
		}

		if (init_exec(init))
			error("tmds opcodes need dcb\n");
	} else {
		if (tmds < ARRAY_SIZE(pramdac_table))
			return pramdac_table[tmds];

		error("tmds selector 0x%02x unknown\n", tmds);
	}

	return 0;
}

/******************************************************************************
 * init opcode handlers
 *****************************************************************************/

/**
 * init_reserved - stub for various unknown/unused single-byte opcodes
 *
 */
static void
init_reserved(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8 opcode = nvbios_rd08(bios, init->offset);
	u8 length, i;

	switch (opcode) {
	case 0xaa:
		length = 4;
		break;
	default:
		length = 1;
		break;
	}

	trace("RESERVED 0x%02x\t", opcode);
	for (i = 1; i < length; i++)
		cont(" 0x%02x", nvbios_rd08(bios, init->offset + i));
	cont("\n");
	init->offset += length;
}

/**
 * INIT_DONE - opcode 0x71
 *
 */
static void
init_done(struct nvbios_init *init)
{
	trace("DONE\n");
	init->offset = 0x0000;
}

/**
 * INIT_IO_RESTRICT_PROG - opcode 0x32
 *
 */
static void
init_io_restrict_prog(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u16 port = nvbios_rd16(bios, init->offset + 1);
	u8 index = nvbios_rd08(bios, init->offset + 3);
	u8  mask = nvbios_rd08(bios, init->offset + 4);
	u8 shift = nvbios_rd08(bios, init->offset + 5);
	u8 count = nvbios_rd08(bios, init->offset + 6);
	u32  reg = nvbios_rd32(bios, init->offset + 7);
	u8 conf, i;

	trace("IO_RESTRICT_PROG\tR[0x%06x] = "
	      "((0x%04x[0x%02x] & 0x%02x) >> %d) [{\n",
	      reg, port, index, mask, shift);
	init->offset += 11;

	conf = (init_rdvgai(init, port, index) & mask) >> shift;
	for (i = 0; i < count; i++) {
		u32 data = nvbios_rd32(bios, init->offset);

		if (i == conf) {
			trace("\t0x%08x *\n", data);
			init_wr32(init, reg, data);
		} else {
			trace("\t0x%08x\n", data);
		}

		init->offset += 4;
	}
	trace("}]\n");
}

/**
 * INIT_REPEAT - opcode 0x33
 *
 */
static void
init_repeat(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8 count = nvbios_rd08(bios, init->offset + 1);
	u16 repeat = init->repeat;

	trace("REPEAT\t0x%02x\n", count);
	init->offset += 2;

	init->repeat = init->offset;
	init->repend = init->offset;
	while (count--) {
		init->offset = init->repeat;
		nvbios_exec(init);
		if (count)
			trace("REPEAT\t0x%02x\n", count);
	}
	init->offset = init->repend;
	init->repeat = repeat;
}

/**
 * INIT_IO_RESTRICT_PLL - opcode 0x34
 *
 */
static void
init_io_restrict_pll(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u16 port = nvbios_rd16(bios, init->offset + 1);
	u8 index = nvbios_rd08(bios, init->offset + 3);
	u8  mask = nvbios_rd08(bios, init->offset + 4);
	u8 shift = nvbios_rd08(bios, init->offset + 5);
	s8  iofc = nvbios_rd08(bios, init->offset + 6);
	u8 count = nvbios_rd08(bios, init->offset + 7);
	u32  reg = nvbios_rd32(bios, init->offset + 8);
	u8 conf, i;

	trace("IO_RESTRICT_PLL\tR[0x%06x] =PLL= "
	      "((0x%04x[0x%02x] & 0x%02x) >> 0x%02x) IOFCOND 0x%02x [{\n",
	      reg, port, index, mask, shift, iofc);
	init->offset += 12;

	conf = (init_rdvgai(init, port, index) & mask) >> shift;
	for (i = 0; i < count; i++) {
		u32 freq = nvbios_rd16(bios, init->offset) * 10;

		if (i == conf) {
			trace("\t%dkHz *\n", freq);
			if (iofc > 0 && init_io_flag_condition_met(init, iofc))
				freq *= 2;
			init_prog_pll(init, reg, freq);
		} else {
			trace("\t%dkHz\n", freq);
		}

		init->offset += 2;
	}
	trace("}]\n");
}

/**
 * INIT_END_REPEAT - opcode 0x36
 *
 */
static void
init_end_repeat(struct nvbios_init *init)
{
	trace("END_REPEAT\n");
	init->offset += 1;

	if (init->repeat) {
		init->repend = init->offset;
		init->offset = 0;
	}
}

/**
 * INIT_COPY - opcode 0x37
 *
 */
static void
init_copy(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32  reg = nvbios_rd32(bios, init->offset + 1);
	u8 shift = nvbios_rd08(bios, init->offset + 5);
	u8 smask = nvbios_rd08(bios, init->offset + 6);
	u16 port = nvbios_rd16(bios, init->offset + 7);
	u8 index = nvbios_rd08(bios, init->offset + 9);
	u8  mask = nvbios_rd08(bios, init->offset + 10);
	u8  data;

	trace("COPY\t0x%04x[0x%02x] &= 0x%02x |= "
	      "((R[0x%06x] %s 0x%02x) & 0x%02x)\n",
	      port, index, mask, reg, (shift & 0x80) ? "<<" : ">>",
	      (shift & 0x80) ? (0x100 - shift) : shift, smask);
	init->offset += 11;

	data  = init_rdvgai(init, port, index) & mask;
	data |= init_shift(init_rd32(init, reg), shift) & smask;
	init_wrvgai(init, port, index, data);
}

/**
 * INIT_NOT - opcode 0x38
 *
 */
static void
init_not(struct nvbios_init *init)
{
	trace("NOT\n");
	init->offset += 1;
	init_exec_inv(init);
}

/**
 * INIT_IO_FLAG_CONDITION - opcode 0x39
 *
 */
static void
init_io_flag_condition(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8 cond = nvbios_rd08(bios, init->offset + 1);

	trace("IO_FLAG_CONDITION\t0x%02x\n", cond);
	init->offset += 2;

	if (!init_io_flag_condition_met(init, cond))
		init_exec_set(init, false);
}

/**
 * INIT_GENERIC_CONDITION - opcode 0x3a
 *
 */
static void
init_generic_condition(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	struct nvbios_dpout info;
	u8  cond = nvbios_rd08(bios, init->offset + 1);
	u8  size = nvbios_rd08(bios, init->offset + 2);
	u8  ver, hdr, cnt, len;
	u16 data;

	trace("GENERIC_CONDITION\t0x%02x 0x%02x\n", cond, size);
	init->offset += 3;

	switch (cond) {
	case 0:
		if (init_conn(init) != DCB_CONNECTOR_eDP)
			init_exec_set(init, false);
		break;
	case 1:
	case 2:
		if ( init->outp &&
		    (data = nvbios_dpout_match(bios, DCB_OUTPUT_DP,
					       (init->outp->or << 0) |
					       (init->outp->sorconf.link << 6),
					       &ver, &hdr, &cnt, &len, &info)))
		{
			if (!(info.flags & cond))
				init_exec_set(init, false);
			break;
		}

		if (init_exec(init))
			warn("script needs dp output table data\n");
		break;
	case 5:
		if (!(init_rdauxr(init, 0x0d) & 1))
			init_exec_set(init, false);
		break;
	default:
		warn("INIT_GENERIC_CONDITON: unknown 0x%02x\n", cond);
		init->offset += size;
		break;
	}
}

/**
 * INIT_IO_MASK_OR - opcode 0x3b
 *
 */
static void
init_io_mask_or(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8 index = nvbios_rd08(bios, init->offset + 1);
	u8    or = init_or(init);
	u8  data;

	trace("IO_MASK_OR\t0x03d4[0x%02x] &= ~(1 << 0x%02x)\n", index, or);
	init->offset += 2;

	data = init_rdvgai(init, 0x03d4, index);
	init_wrvgai(init, 0x03d4, index, data &= ~(1 << or));
}

/**
 * INIT_IO_OR - opcode 0x3c
 *
 */
static void
init_io_or(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8 index = nvbios_rd08(bios, init->offset + 1);
	u8    or = init_or(init);
	u8  data;

	trace("IO_OR\t0x03d4[0x%02x] |= (1 << 0x%02x)\n", index, or);
	init->offset += 2;

	data = init_rdvgai(init, 0x03d4, index);
	init_wrvgai(init, 0x03d4, index, data | (1 << or));
}

/**
 * INIT_ANDN_REG - opcode 0x47
 *
 */
static void
init_andn_reg(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32  reg = nvbios_rd32(bios, init->offset + 1);
	u32 mask = nvbios_rd32(bios, init->offset + 5);

	trace("ANDN_REG\tR[0x%06x] &= ~0x%08x\n", reg, mask);
	init->offset += 9;

	init_mask(init, reg, mask, 0);
}

/**
 * INIT_OR_REG - opcode 0x48
 *
 */
static void
init_or_reg(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32  reg = nvbios_rd32(bios, init->offset + 1);
	u32 mask = nvbios_rd32(bios, init->offset + 5);

	trace("OR_REG\tR[0x%06x] |= 0x%08x\n", reg, mask);
	init->offset += 9;

	init_mask(init, reg, 0, mask);
}

/**
 * INIT_INDEX_ADDRESS_LATCHED - opcode 0x49
 *
 */
static void
init_idx_addr_latched(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32 creg = nvbios_rd32(bios, init->offset + 1);
	u32 dreg = nvbios_rd32(bios, init->offset + 5);
	u32 mask = nvbios_rd32(bios, init->offset + 9);
	u32 data = nvbios_rd32(bios, init->offset + 13);
	u8 count = nvbios_rd08(bios, init->offset + 17);

	trace("INDEX_ADDRESS_LATCHED\tR[0x%06x] : R[0x%06x]\n", creg, dreg);
	trace("\tCTRL &= 0x%08x |= 0x%08x\n", mask, data);
	init->offset += 18;

	while (count--) {
		u8 iaddr = nvbios_rd08(bios, init->offset + 0);
		u8 idata = nvbios_rd08(bios, init->offset + 1);

		trace("\t[0x%02x] = 0x%02x\n", iaddr, idata);
		init->offset += 2;

		init_wr32(init, dreg, idata);
		init_mask(init, creg, ~mask, data | iaddr);
	}
}

/**
 * INIT_IO_RESTRICT_PLL2 - opcode 0x4a
 *
 */
static void
init_io_restrict_pll2(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u16 port = nvbios_rd16(bios, init->offset + 1);
	u8 index = nvbios_rd08(bios, init->offset + 3);
	u8  mask = nvbios_rd08(bios, init->offset + 4);
	u8 shift = nvbios_rd08(bios, init->offset + 5);
	u8 count = nvbios_rd08(bios, init->offset + 6);
	u32  reg = nvbios_rd32(bios, init->offset + 7);
	u8  conf, i;

	trace("IO_RESTRICT_PLL2\t"
	      "R[0x%06x] =PLL= ((0x%04x[0x%02x] & 0x%02x) >> 0x%02x) [{\n",
	      reg, port, index, mask, shift);
	init->offset += 11;

	conf = (init_rdvgai(init, port, index) & mask) >> shift;
	for (i = 0; i < count; i++) {
		u32 freq = nvbios_rd32(bios, init->offset);
		if (i == conf) {
			trace("\t%dkHz *\n", freq);
			init_prog_pll(init, reg, freq);
		} else {
			trace("\t%dkHz\n", freq);
		}
		init->offset += 4;
	}
	trace("}]\n");
}

/**
 * INIT_PLL2 - opcode 0x4b
 *
 */
static void
init_pll2(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32  reg = nvbios_rd32(bios, init->offset + 1);
	u32 freq = nvbios_rd32(bios, init->offset + 5);

	trace("PLL2\tR[0x%06x] =PLL= %dkHz\n", reg, freq);
	init->offset += 9;

	init_prog_pll(init, reg, freq);
}

/**
 * INIT_I2C_BYTE - opcode 0x4c
 *
 */
static void
init_i2c_byte(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8 index = nvbios_rd08(bios, init->offset + 1);
	u8  addr = nvbios_rd08(bios, init->offset + 2) >> 1;
	u8 count = nvbios_rd08(bios, init->offset + 3);

	trace("I2C_BYTE\tI2C[0x%02x][0x%02x]\n", index, addr);
	init->offset += 4;

	while (count--) {
		u8  reg = nvbios_rd08(bios, init->offset + 0);
		u8 mask = nvbios_rd08(bios, init->offset + 1);
		u8 data = nvbios_rd08(bios, init->offset + 2);
		int val;

		trace("\t[0x%02x] &= 0x%02x |= 0x%02x\n", reg, mask, data);
		init->offset += 3;

		val = init_rdi2cr(init, index, addr, reg);
		if (val < 0)
			continue;
		init_wri2cr(init, index, addr, reg, (val & mask) | data);
	}
}

/**
 * INIT_ZM_I2C_BYTE - opcode 0x4d
 *
 */
static void
init_zm_i2c_byte(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8 index = nvbios_rd08(bios, init->offset + 1);
	u8  addr = nvbios_rd08(bios, init->offset + 2) >> 1;
	u8 count = nvbios_rd08(bios, init->offset + 3);

	trace("ZM_I2C_BYTE\tI2C[0x%02x][0x%02x]\n", index, addr);
	init->offset += 4;

	while (count--) {
		u8  reg = nvbios_rd08(bios, init->offset + 0);
		u8 data = nvbios_rd08(bios, init->offset + 1);

		trace("\t[0x%02x] = 0x%02x\n", reg, data);
		init->offset += 2;

		init_wri2cr(init, index, addr, reg, data);
	}
}

/**
 * INIT_ZM_I2C - opcode 0x4e
 *
 */
static void
init_zm_i2c(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8 index = nvbios_rd08(bios, init->offset + 1);
	u8  addr = nvbios_rd08(bios, init->offset + 2) >> 1;
	u8 count = nvbios_rd08(bios, init->offset + 3);
	u8 data[256], i;

	trace("ZM_I2C\tI2C[0x%02x][0x%02x]\n", index, addr);
	init->offset += 4;

	for (i = 0; i < count; i++) {
		data[i] = nvbios_rd08(bios, init->offset);
		trace("\t0x%02x\n", data[i]);
		init->offset++;
	}

	if (init_exec(init)) {
		struct i2c_adapter *adap = init_i2c(init, index);
		struct i2c_msg msg = {
			.addr = addr, .flags = 0, .len = count, .buf = data,
		};
		int ret;

		if (adap && (ret = i2c_transfer(adap, &msg, 1)) != 1)
			warn("i2c wr failed, %d\n", ret);
	}
}

/**
 * INIT_TMDS - opcode 0x4f
 *
 */
static void
init_tmds(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8 tmds = nvbios_rd08(bios, init->offset + 1);
	u8 addr = nvbios_rd08(bios, init->offset + 2);
	u8 mask = nvbios_rd08(bios, init->offset + 3);
	u8 data = nvbios_rd08(bios, init->offset + 4);
	u32 reg = init_tmds_reg(init, tmds);

	trace("TMDS\tT[0x%02x][0x%02x] &= 0x%02x |= 0x%02x\n",
	      tmds, addr, mask, data);
	init->offset += 5;

	if (reg == 0)
		return;

	init_wr32(init, reg + 0, addr | 0x00010000);
	init_wr32(init, reg + 4, data | (init_rd32(init, reg + 4) & mask));
	init_wr32(init, reg + 0, addr);
}

/**
 * INIT_ZM_TMDS_GROUP - opcode 0x50
 *
 */
static void
init_zm_tmds_group(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8  tmds = nvbios_rd08(bios, init->offset + 1);
	u8 count = nvbios_rd08(bios, init->offset + 2);
	u32  reg = init_tmds_reg(init, tmds);

	trace("TMDS_ZM_GROUP\tT[0x%02x]\n", tmds);
	init->offset += 3;

	while (count--) {
		u8 addr = nvbios_rd08(bios, init->offset + 0);
		u8 data = nvbios_rd08(bios, init->offset + 1);

		trace("\t[0x%02x] = 0x%02x\n", addr, data);
		init->offset += 2;

		init_wr32(init, reg + 4, data);
		init_wr32(init, reg + 0, addr);
	}
}

/**
 * INIT_CR_INDEX_ADDRESS_LATCHED - opcode 0x51
 *
 */
static void
init_cr_idx_adr_latch(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8 addr0 = nvbios_rd08(bios, init->offset + 1);
	u8 addr1 = nvbios_rd08(bios, init->offset + 2);
	u8  base = nvbios_rd08(bios, init->offset + 3);
	u8 count = nvbios_rd08(bios, init->offset + 4);
	u8 save0;

	trace("CR_INDEX_ADDR C[%02x] C[%02x]\n", addr0, addr1);
	init->offset += 5;

	save0 = init_rdvgai(init, 0x03d4, addr0);
	while (count--) {
		u8 data = nvbios_rd08(bios, init->offset);

		trace("\t\t[0x%02x] = 0x%02x\n", base, data);
		init->offset += 1;

		init_wrvgai(init, 0x03d4, addr0, base++);
		init_wrvgai(init, 0x03d4, addr1, data);
	}
	init_wrvgai(init, 0x03d4, addr0, save0);
}

/**
 * INIT_CR - opcode 0x52
 *
 */
static void
init_cr(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8 addr = nvbios_rd08(bios, init->offset + 1);
	u8 mask = nvbios_rd08(bios, init->offset + 2);
	u8 data = nvbios_rd08(bios, init->offset + 3);
	u8 val;

	trace("CR\t\tC[0x%02x] &= 0x%02x |= 0x%02x\n", addr, mask, data);
	init->offset += 4;

	val = init_rdvgai(init, 0x03d4, addr) & mask;
	init_wrvgai(init, 0x03d4, addr, val | data);
}

/**
 * INIT_ZM_CR - opcode 0x53
 *
 */
static void
init_zm_cr(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8 addr = nvbios_rd08(bios, init->offset + 1);
	u8 data = nvbios_rd08(bios, init->offset + 2);

	trace("ZM_CR\tC[0x%02x] = 0x%02x\n", addr,  data);
	init->offset += 3;

	init_wrvgai(init, 0x03d4, addr, data);
}

/**
 * INIT_ZM_CR_GROUP - opcode 0x54
 *
 */
static void
init_zm_cr_group(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8 count = nvbios_rd08(bios, init->offset + 1);

	trace("ZM_CR_GROUP\n");
	init->offset += 2;

	while (count--) {
		u8 addr = nvbios_rd08(bios, init->offset + 0);
		u8 data = nvbios_rd08(bios, init->offset + 1);

		trace("\t\tC[0x%02x] = 0x%02x\n", addr, data);
		init->offset += 2;

		init_wrvgai(init, 0x03d4, addr, data);
	}
}

/**
 * INIT_CONDITION_TIME - opcode 0x56
 *
 */
static void
init_condition_time(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8  cond = nvbios_rd08(bios, init->offset + 1);
	u8 retry = nvbios_rd08(bios, init->offset + 2);
	u8  wait = min((u16)retry * 50, 100);

	trace("CONDITION_TIME\t0x%02x 0x%02x\n", cond, retry);
	init->offset += 3;

	if (!init_exec(init))
		return;

	while (wait--) {
		if (init_condition_met(init, cond))
			return;
		mdelay(20);
	}

	init_exec_set(init, false);
}

/**
 * INIT_LTIME - opcode 0x57
 *
 */
static void
init_ltime(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u16 msec = nvbios_rd16(bios, init->offset + 1);

	trace("LTIME\t0x%04x\n", msec);
	init->offset += 3;

	if (init_exec(init))
		mdelay(msec);
}

/**
 * INIT_ZM_REG_SEQUENCE - opcode 0x58
 *
 */
static void
init_zm_reg_sequence(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32 base = nvbios_rd32(bios, init->offset + 1);
	u8 count = nvbios_rd08(bios, init->offset + 5);

	trace("ZM_REG_SEQUENCE\t0x%02x\n", count);
	init->offset += 6;

	while (count--) {
		u32 data = nvbios_rd32(bios, init->offset);

		trace("\t\tR[0x%06x] = 0x%08x\n", base, data);
		init->offset += 4;

		init_wr32(init, base, data);
		base += 4;
	}
}

/**
 * INIT_PLL_INDIRECT - opcode 0x59
 *
 */
static void
init_pll_indirect(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32  reg = nvbios_rd32(bios, init->offset + 1);
	u16 addr = nvbios_rd16(bios, init->offset + 5);
	u32 freq = (u32)nvbios_rd16(bios, addr) * 1000;

	trace("PLL_INDIRECT\tR[0x%06x] =PLL= VBIOS[%04x] = %dkHz\n",
	      reg, addr, freq);
	init->offset += 7;

	init_prog_pll(init, reg, freq);
}

/**
 * INIT_ZM_REG_INDIRECT - opcode 0x5a
 *
 */
static void
init_zm_reg_indirect(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32  reg = nvbios_rd32(bios, init->offset + 1);
	u16 addr = nvbios_rd16(bios, init->offset + 5);
	u32 data = nvbios_rd32(bios, addr);

	trace("ZM_REG_INDIRECT\tR[0x%06x] = VBIOS[0x%04x] = 0x%08x\n",
	      reg, addr, data);
	init->offset += 7;

	init_wr32(init, addr, data);
}

/**
 * INIT_SUB_DIRECT - opcode 0x5b
 *
 */
static void
init_sub_direct(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u16 addr = nvbios_rd16(bios, init->offset + 1);
	u16 save;

	trace("SUB_DIRECT\t0x%04x\n", addr);

	if (init_exec(init)) {
		save = init->offset;
		init->offset = addr;
		if (nvbios_exec(init)) {
			error("error parsing sub-table\n");
			return;
		}
		init->offset = save;
	}

	init->offset += 3;
}

/**
 * INIT_JUMP - opcode 0x5c
 *
 */
static void
init_jump(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u16 offset = nvbios_rd16(bios, init->offset + 1);

	trace("JUMP\t0x%04x\n", offset);

	if (init_exec(init))
		init->offset = offset;
	else
		init->offset += 3;
}

/**
 * INIT_I2C_IF - opcode 0x5e
 *
 */
static void
init_i2c_if(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8 index = nvbios_rd08(bios, init->offset + 1);
	u8  addr = nvbios_rd08(bios, init->offset + 2);
	u8   reg = nvbios_rd08(bios, init->offset + 3);
	u8  mask = nvbios_rd08(bios, init->offset + 4);
	u8  data = nvbios_rd08(bios, init->offset + 5);
	u8 value;

	trace("I2C_IF\tI2C[0x%02x][0x%02x][0x%02x] & 0x%02x == 0x%02x\n",
	      index, addr, reg, mask, data);
	init->offset += 6;
	init_exec_force(init, true);

	value = init_rdi2cr(init, index, addr, reg);
	if ((value & mask) != data)
		init_exec_set(init, false);

	init_exec_force(init, false);
}

/**
 * INIT_COPY_NV_REG - opcode 0x5f
 *
 */
static void
init_copy_nv_reg(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32  sreg = nvbios_rd32(bios, init->offset + 1);
	u8  shift = nvbios_rd08(bios, init->offset + 5);
	u32 smask = nvbios_rd32(bios, init->offset + 6);
	u32  sxor = nvbios_rd32(bios, init->offset + 10);
	u32  dreg = nvbios_rd32(bios, init->offset + 14);
	u32 dmask = nvbios_rd32(bios, init->offset + 18);
	u32 data;

	trace("COPY_NV_REG\tR[0x%06x] &= 0x%08x |= "
	      "((R[0x%06x] %s 0x%02x) & 0x%08x ^ 0x%08x)\n",
	      dreg, dmask, sreg, (shift & 0x80) ? "<<" : ">>",
	      (shift & 0x80) ? (0x100 - shift) : shift, smask, sxor);
	init->offset += 22;

	data = init_shift(init_rd32(init, sreg), shift);
	init_mask(init, dreg, ~dmask, (data & smask) ^ sxor);
}

/**
 * INIT_ZM_INDEX_IO - opcode 0x62
 *
 */
static void
init_zm_index_io(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u16 port = nvbios_rd16(bios, init->offset + 1);
	u8 index = nvbios_rd08(bios, init->offset + 3);
	u8  data = nvbios_rd08(bios, init->offset + 4);

	trace("ZM_INDEX_IO\tI[0x%04x][0x%02x] = 0x%02x\n", port, index, data);
	init->offset += 5;

	init_wrvgai(init, port, index, data);
}

/**
 * INIT_COMPUTE_MEM - opcode 0x63
 *
 */
static void
init_compute_mem(struct nvbios_init *init)
{
	struct nvkm_devinit *devinit = init->subdev->device->devinit;

	trace("COMPUTE_MEM\n");
	init->offset += 1;

	init_exec_force(init, true);
	if (init_exec(init))
		nvkm_devinit_meminit(devinit);
	init_exec_force(init, false);
}

/**
 * INIT_RESET - opcode 0x65
 *
 */
static void
init_reset(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32   reg = nvbios_rd32(bios, init->offset + 1);
	u32 data1 = nvbios_rd32(bios, init->offset + 5);
	u32 data2 = nvbios_rd32(bios, init->offset + 9);
	u32 savepci19;

	trace("RESET\tR[0x%08x] = 0x%08x, 0x%08x", reg, data1, data2);
	init->offset += 13;
	init_exec_force(init, true);

	savepci19 = init_mask(init, 0x00184c, 0x00000f00, 0x00000000);
	init_wr32(init, reg, data1);
	udelay(10);
	init_wr32(init, reg, data2);
	init_wr32(init, 0x00184c, savepci19);
	init_mask(init, 0x001850, 0x00000001, 0x00000000);

	init_exec_force(init, false);
}

/**
 * INIT_CONFIGURE_MEM - opcode 0x66
 *
 */
static u16
init_configure_mem_clk(struct nvbios_init *init)
{
	u16 mdata = bmp_mem_init_table(init->subdev->device->bios);
	if (mdata)
		mdata += (init_rdvgai(init, 0x03d4, 0x3c) >> 4) * 66;
	return mdata;
}

static void
init_configure_mem(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u16 mdata, sdata;
	u32 addr, data;

	trace("CONFIGURE_MEM\n");
	init->offset += 1;

	if (bios->version.major > 2) {
		init_done(init);
		return;
	}
	init_exec_force(init, true);

	mdata = init_configure_mem_clk(init);
	sdata = bmp_sdr_seq_table(bios);
	if (nvbios_rd08(bios, mdata) & 0x01)
		sdata = bmp_ddr_seq_table(bios);
	mdata += 6; /* skip to data */

	data = init_rdvgai(init, 0x03c4, 0x01);
	init_wrvgai(init, 0x03c4, 0x01, data | 0x20);

	for (; (addr = nvbios_rd32(bios, sdata)) != 0xffffffff; sdata += 4) {
		switch (addr) {
		case 0x10021c: /* CKE_NORMAL */
		case 0x1002d0: /* CMD_REFRESH */
		case 0x1002d4: /* CMD_PRECHARGE */
			data = 0x00000001;
			break;
		default:
			data = nvbios_rd32(bios, mdata);
			mdata += 4;
			if (data == 0xffffffff)
				continue;
			break;
		}

		init_wr32(init, addr, data);
	}

	init_exec_force(init, false);
}

/**
 * INIT_CONFIGURE_CLK - opcode 0x67
 *
 */
static void
init_configure_clk(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u16 mdata, clock;

	trace("CONFIGURE_CLK\n");
	init->offset += 1;

	if (bios->version.major > 2) {
		init_done(init);
		return;
	}
	init_exec_force(init, true);

	mdata = init_configure_mem_clk(init);

	/* NVPLL */
	clock = nvbios_rd16(bios, mdata + 4) * 10;
	init_prog_pll(init, 0x680500, clock);

	/* MPLL */
	clock = nvbios_rd16(bios, mdata + 2) * 10;
	if (nvbios_rd08(bios, mdata) & 0x01)
		clock *= 2;
	init_prog_pll(init, 0x680504, clock);

	init_exec_force(init, false);
}

/**
 * INIT_CONFIGURE_PREINIT - opcode 0x68
 *
 */
static void
init_configure_preinit(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32 strap;

	trace("CONFIGURE_PREINIT\n");
	init->offset += 1;

	if (bios->version.major > 2) {
		init_done(init);
		return;
	}
	init_exec_force(init, true);

	strap = init_rd32(init, 0x101000);
	strap = ((strap << 2) & 0xf0) | ((strap & 0x40) >> 6);
	init_wrvgai(init, 0x03d4, 0x3c, strap);

	init_exec_force(init, false);
}

/**
 * INIT_IO - opcode 0x69
 *
 */
static void
init_io(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u16 port = nvbios_rd16(bios, init->offset + 1);
	u8  mask = nvbios_rd16(bios, init->offset + 3);
	u8  data = nvbios_rd16(bios, init->offset + 4);
	u8 value;

	trace("IO\t\tI[0x%04x] &= 0x%02x |= 0x%02x\n", port, mask, data);
	init->offset += 5;

	/* ummm.. yes.. should really figure out wtf this is and why it's
	 * needed some day..  it's almost certainly wrong, but, it also
	 * somehow makes things work...
	 */
	if (bios->subdev.device->card_type >= NV_50 &&
	    port == 0x03c3 && data == 0x01) {
		init_mask(init, 0x614100, 0xf0800000, 0x00800000);
		init_mask(init, 0x00e18c, 0x00020000, 0x00020000);
		init_mask(init, 0x614900, 0xf0800000, 0x00800000);
		init_mask(init, 0x000200, 0x40000000, 0x00000000);
		mdelay(10);
		init_mask(init, 0x00e18c, 0x00020000, 0x00000000);
		init_mask(init, 0x000200, 0x40000000, 0x40000000);
		init_wr32(init, 0x614100, 0x00800018);
		init_wr32(init, 0x614900, 0x00800018);
		mdelay(10);
		init_wr32(init, 0x614100, 0x10000018);
		init_wr32(init, 0x614900, 0x10000018);
	}

	value = init_rdport(init, port) & mask;
	init_wrport(init, port, data | value);
}

/**
 * INIT_SUB - opcode 0x6b
 *
 */
static void
init_sub(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8 index = nvbios_rd08(bios, init->offset + 1);
	u16 addr, save;

	trace("SUB\t0x%02x\n", index);

	addr = init_script(bios, index);
	if (addr && init_exec(init)) {
		save = init->offset;
		init->offset = addr;
		if (nvbios_exec(init)) {
			error("error parsing sub-table\n");
			return;
		}
		init->offset = save;
	}

	init->offset += 2;
}

/**
 * INIT_RAM_CONDITION - opcode 0x6d
 *
 */
static void
init_ram_condition(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8  mask = nvbios_rd08(bios, init->offset + 1);
	u8 value = nvbios_rd08(bios, init->offset + 2);

	trace("RAM_CONDITION\t"
	      "(R[0x100000] & 0x%02x) == 0x%02x\n", mask, value);
	init->offset += 3;

	if ((init_rd32(init, 0x100000) & mask) != value)
		init_exec_set(init, false);
}

/**
 * INIT_NV_REG - opcode 0x6e
 *
 */
static void
init_nv_reg(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32  reg = nvbios_rd32(bios, init->offset + 1);
	u32 mask = nvbios_rd32(bios, init->offset + 5);
	u32 data = nvbios_rd32(bios, init->offset + 9);

	trace("NV_REG\tR[0x%06x] &= 0x%08x |= 0x%08x\n", reg, mask, data);
	init->offset += 13;

	init_mask(init, reg, ~mask, data);
}

/**
 * INIT_MACRO - opcode 0x6f
 *
 */
static void
init_macro(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8  macro = nvbios_rd08(bios, init->offset + 1);
	u16 table;

	trace("MACRO\t0x%02x\n", macro);

	table = init_macro_table(init);
	if (table) {
		u32 addr = nvbios_rd32(bios, table + (macro * 8) + 0);
		u32 data = nvbios_rd32(bios, table + (macro * 8) + 4);
		trace("\t\tR[0x%06x] = 0x%08x\n", addr, data);
		init_wr32(init, addr, data);
	}

	init->offset += 2;
}

/**
 * INIT_RESUME - opcode 0x72
 *
 */
static void
init_resume(struct nvbios_init *init)
{
	trace("RESUME\n");
	init->offset += 1;
	init_exec_set(init, true);
}

/**
 * INIT_STRAP_CONDITION - opcode 0x73
 *
 */
static void
init_strap_condition(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32 mask = nvbios_rd32(bios, init->offset + 1);
	u32 value = nvbios_rd32(bios, init->offset + 5);

	trace("STRAP_CONDITION\t(R[0x101000] & 0x%08x) == 0x%08x\n", mask, value);
	init->offset += 9;

	if ((init_rd32(init, 0x101000) & mask) != value)
		init_exec_set(init, false);
}

/**
 * INIT_TIME - opcode 0x74
 *
 */
static void
init_time(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u16 usec = nvbios_rd16(bios, init->offset + 1);

	trace("TIME\t0x%04x\n", usec);
	init->offset += 3;

	if (init_exec(init)) {
		if (usec < 1000)
			udelay(usec);
		else
			mdelay((usec + 900) / 1000);
	}
}

/**
 * INIT_CONDITION - opcode 0x75
 *
 */
static void
init_condition(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8 cond = nvbios_rd08(bios, init->offset + 1);

	trace("CONDITION\t0x%02x\n", cond);
	init->offset += 2;

	if (!init_condition_met(init, cond))
		init_exec_set(init, false);
}

/**
 * INIT_IO_CONDITION - opcode 0x76
 *
 */
static void
init_io_condition(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8 cond = nvbios_rd08(bios, init->offset + 1);

	trace("IO_CONDITION\t0x%02x\n", cond);
	init->offset += 2;

	if (!init_io_condition_met(init, cond))
		init_exec_set(init, false);
}

/**
 * INIT_ZM_REG16 - opcode 0x77
 *
 */
static void
init_zm_reg16(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32 addr = nvbios_rd32(bios, init->offset + 1);
	u16 data = nvbios_rd16(bios, init->offset + 5);

	trace("ZM_REG\tR[0x%06x] = 0x%04x\n", addr, data);
	init->offset += 7;

	init_wr32(init, addr, data);
}

/**
 * INIT_INDEX_IO - opcode 0x78
 *
 */
static void
init_index_io(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u16 port = nvbios_rd16(bios, init->offset + 1);
	u8 index = nvbios_rd16(bios, init->offset + 3);
	u8  mask = nvbios_rd08(bios, init->offset + 4);
	u8  data = nvbios_rd08(bios, init->offset + 5);
	u8 value;

	trace("INDEX_IO\tI[0x%04x][0x%02x] &= 0x%02x |= 0x%02x\n",
	      port, index, mask, data);
	init->offset += 6;

	value = init_rdvgai(init, port, index) & mask;
	init_wrvgai(init, port, index, data | value);
}

/**
 * INIT_PLL - opcode 0x79
 *
 */
static void
init_pll(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32  reg = nvbios_rd32(bios, init->offset + 1);
	u32 freq = nvbios_rd16(bios, init->offset + 5) * 10;

	trace("PLL\tR[0x%06x] =PLL= %dkHz\n", reg, freq);
	init->offset += 7;

	init_prog_pll(init, reg, freq);
}

/**
 * INIT_ZM_REG - opcode 0x7a
 *
 */
static void
init_zm_reg(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32 addr = nvbios_rd32(bios, init->offset + 1);
	u32 data = nvbios_rd32(bios, init->offset + 5);

	trace("ZM_REG\tR[0x%06x] = 0x%08x\n", addr, data);
	init->offset += 9;

	if (addr == 0x000200)
		data |= 0x00000001;

	init_wr32(init, addr, data);
}

/**
 * INIT_RAM_RESTRICT_PLL - opcde 0x87
 *
 */
static void
init_ram_restrict_pll(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8  type = nvbios_rd08(bios, init->offset + 1);
	u8 count = init_ram_restrict_group_count(init);
	u8 strap = init_ram_restrict(init);
	u8 cconf;

	trace("RAM_RESTRICT_PLL\t0x%02x\n", type);
	init->offset += 2;

	for (cconf = 0; cconf < count; cconf++) {
		u32 freq = nvbios_rd32(bios, init->offset);

		if (cconf == strap) {
			trace("%dkHz *\n", freq);
			init_prog_pll(init, type, freq);
		} else {
			trace("%dkHz\n", freq);
		}

		init->offset += 4;
	}
}

/**
 * INIT_GPIO - opcode 0x8e
 *
 */
static void
init_gpio(struct nvbios_init *init)
{
	struct nvkm_gpio *gpio = init->subdev->device->gpio;

	trace("GPIO\n");
	init->offset += 1;

	if (init_exec(init))
		nvkm_gpio_reset(gpio, DCB_GPIO_UNUSED);
}

/**
 * INIT_RAM_RESTRICT_ZM_GROUP - opcode 0x8f
 *
 */
static void
init_ram_restrict_zm_reg_group(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32 addr = nvbios_rd32(bios, init->offset + 1);
	u8  incr = nvbios_rd08(bios, init->offset + 5);
	u8   num = nvbios_rd08(bios, init->offset + 6);
	u8 count = init_ram_restrict_group_count(init);
	u8 index = init_ram_restrict(init);
	u8 i, j;

	trace("RAM_RESTRICT_ZM_REG_GROUP\t"
	      "R[0x%08x] 0x%02x 0x%02x\n", addr, incr, num);
	init->offset += 7;

	for (i = 0; i < num; i++) {
		trace("\tR[0x%06x] = {\n", addr);
		for (j = 0; j < count; j++) {
			u32 data = nvbios_rd32(bios, init->offset);

			if (j == index) {
				trace("\t\t0x%08x *\n", data);
				init_wr32(init, addr, data);
			} else {
				trace("\t\t0x%08x\n", data);
			}

			init->offset += 4;
		}
		trace("\t}\n");
		addr += incr;
	}
}

/**
 * INIT_COPY_ZM_REG - opcode 0x90
 *
 */
static void
init_copy_zm_reg(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32 sreg = nvbios_rd32(bios, init->offset + 1);
	u32 dreg = nvbios_rd32(bios, init->offset + 5);

	trace("COPY_ZM_REG\tR[0x%06x] = R[0x%06x]\n", dreg, sreg);
	init->offset += 9;

	init_wr32(init, dreg, init_rd32(init, sreg));
}

/**
 * INIT_ZM_REG_GROUP - opcode 0x91
 *
 */
static void
init_zm_reg_group(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32 addr = nvbios_rd32(bios, init->offset + 1);
	u8 count = nvbios_rd08(bios, init->offset + 5);

	trace("ZM_REG_GROUP\tR[0x%06x] =\n", addr);
	init->offset += 6;

	while (count--) {
		u32 data = nvbios_rd32(bios, init->offset);
		trace("\t0x%08x\n", data);
		init_wr32(init, addr, data);
		init->offset += 4;
	}
}

/**
 * INIT_XLAT - opcode 0x96
 *
 */
static void
init_xlat(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32 saddr = nvbios_rd32(bios, init->offset + 1);
	u8 sshift = nvbios_rd08(bios, init->offset + 5);
	u8  smask = nvbios_rd08(bios, init->offset + 6);
	u8  index = nvbios_rd08(bios, init->offset + 7);
	u32 daddr = nvbios_rd32(bios, init->offset + 8);
	u32 dmask = nvbios_rd32(bios, init->offset + 12);
	u8  shift = nvbios_rd08(bios, init->offset + 16);
	u32 data;

	trace("INIT_XLAT\tR[0x%06x] &= 0x%08x |= "
	      "(X%02x((R[0x%06x] %s 0x%02x) & 0x%02x) << 0x%02x)\n",
	      daddr, dmask, index, saddr, (sshift & 0x80) ? "<<" : ">>",
	      (sshift & 0x80) ? (0x100 - sshift) : sshift, smask, shift);
	init->offset += 17;

	data = init_shift(init_rd32(init, saddr), sshift) & smask;
	data = init_xlat_(init, index, data) << shift;
	init_mask(init, daddr, ~dmask, data);
}

/**
 * INIT_ZM_MASK_ADD - opcode 0x97
 *
 */
static void
init_zm_mask_add(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32 addr = nvbios_rd32(bios, init->offset + 1);
	u32 mask = nvbios_rd32(bios, init->offset + 5);
	u32  add = nvbios_rd32(bios, init->offset + 9);
	u32 data;

	trace("ZM_MASK_ADD\tR[0x%06x] &= 0x%08x += 0x%08x\n", addr, mask, add);
	init->offset += 13;

	data =  init_rd32(init, addr);
	data = (data & mask) | ((data + add) & ~mask);
	init_wr32(init, addr, data);
}

/**
 * INIT_AUXCH - opcode 0x98
 *
 */
static void
init_auxch(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32 addr = nvbios_rd32(bios, init->offset + 1);
	u8 count = nvbios_rd08(bios, init->offset + 5);

	trace("AUXCH\tAUX[0x%08x] 0x%02x\n", addr, count);
	init->offset += 6;

	while (count--) {
		u8 mask = nvbios_rd08(bios, init->offset + 0);
		u8 data = nvbios_rd08(bios, init->offset + 1);
		trace("\tAUX[0x%08x] &= 0x%02x |= 0x%02x\n", addr, mask, data);
		mask = init_rdauxr(init, addr) & mask;
		init_wrauxr(init, addr, mask | data);
		init->offset += 2;
	}
}

/**
 * INIT_AUXCH - opcode 0x99
 *
 */
static void
init_zm_auxch(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u32 addr = nvbios_rd32(bios, init->offset + 1);
	u8 count = nvbios_rd08(bios, init->offset + 5);

	trace("ZM_AUXCH\tAUX[0x%08x] 0x%02x\n", addr, count);
	init->offset += 6;

	while (count--) {
		u8 data = nvbios_rd08(bios, init->offset + 0);
		trace("\tAUX[0x%08x] = 0x%02x\n", addr, data);
		init_wrauxr(init, addr, data);
		init->offset += 1;
	}
}

/**
 * INIT_I2C_LONG_IF - opcode 0x9a
 *
 */
static void
init_i2c_long_if(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	u8 index = nvbios_rd08(bios, init->offset + 1);
	u8  addr = nvbios_rd08(bios, init->offset + 2) >> 1;
	u8 reglo = nvbios_rd08(bios, init->offset + 3);
	u8 reghi = nvbios_rd08(bios, init->offset + 4);
	u8  mask = nvbios_rd08(bios, init->offset + 5);
	u8  data = nvbios_rd08(bios, init->offset + 6);
	struct i2c_adapter *adap;

	trace("I2C_LONG_IF\t"
	      "I2C[0x%02x][0x%02x][0x%02x%02x] & 0x%02x == 0x%02x\n",
	      index, addr, reglo, reghi, mask, data);
	init->offset += 7;

	adap = init_i2c(init, index);
	if (adap) {
		u8 i[2] = { reghi, reglo };
		u8 o[1] = {};
		struct i2c_msg msg[] = {
			{ .addr = addr, .flags = 0, .len = 2, .buf = i },
			{ .addr = addr, .flags = I2C_M_RD, .len = 1, .buf = o }
		};
		int ret;

		ret = i2c_transfer(adap, msg, 2);
		if (ret == 2 && ((o[0] & mask) == data))
			return;
	}

	init_exec_set(init, false);
}

/**
 * INIT_GPIO_NE - opcode 0xa9
 *
 */
static void
init_gpio_ne(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;
	struct nvkm_gpio *gpio = bios->subdev.device->gpio;
	struct dcb_gpio_func func;
	u8 count = nvbios_rd08(bios, init->offset + 1);
	u8 idx = 0, ver, len;
	u16 data, i;

	trace("GPIO_NE\t");
	init->offset += 2;

	for (i = init->offset; i < init->offset + count; i++)
		cont("0x%02x ", nvbios_rd08(bios, i));
	cont("\n");

	while ((data = dcb_gpio_parse(bios, 0, idx++, &ver, &len, &func))) {
		if (func.func != DCB_GPIO_UNUSED) {
			for (i = init->offset; i < init->offset + count; i++) {
				if (func.func == nvbios_rd08(bios, i))
					break;
			}

			trace("\tFUNC[0x%02x]", func.func);
			if (i == (init->offset + count)) {
				cont(" *");
				if (init_exec(init))
					nvkm_gpio_reset(gpio, func.func);
			}
			cont("\n");
		}
	}

	init->offset += count;
}

static struct nvbios_init_opcode {
	void (*exec)(struct nvbios_init *);
} init_opcode[] = {
	[0x32] = { init_io_restrict_prog },
	[0x33] = { init_repeat },
	[0x34] = { init_io_restrict_pll },
	[0x36] = { init_end_repeat },
	[0x37] = { init_copy },
	[0x38] = { init_not },
	[0x39] = { init_io_flag_condition },
	[0x3a] = { init_generic_condition },
	[0x3b] = { init_io_mask_or },
	[0x3c] = { init_io_or },
	[0x47] = { init_andn_reg },
	[0x48] = { init_or_reg },
	[0x49] = { init_idx_addr_latched },
	[0x4a] = { init_io_restrict_pll2 },
	[0x4b] = { init_pll2 },
	[0x4c] = { init_i2c_byte },
	[0x4d] = { init_zm_i2c_byte },
	[0x4e] = { init_zm_i2c },
	[0x4f] = { init_tmds },
	[0x50] = { init_zm_tmds_group },
	[0x51] = { init_cr_idx_adr_latch },
	[0x52] = { init_cr },
	[0x53] = { init_zm_cr },
	[0x54] = { init_zm_cr_group },
	[0x56] = { init_condition_time },
	[0x57] = { init_ltime },
	[0x58] = { init_zm_reg_sequence },
	[0x59] = { init_pll_indirect },
	[0x5a] = { init_zm_reg_indirect },
	[0x5b] = { init_sub_direct },
	[0x5c] = { init_jump },
	[0x5e] = { init_i2c_if },
	[0x5f] = { init_copy_nv_reg },
	[0x62] = { init_zm_index_io },
	[0x63] = { init_compute_mem },
	[0x65] = { init_reset },
	[0x66] = { init_configure_mem },
	[0x67] = { init_configure_clk },
	[0x68] = { init_configure_preinit },
	[0x69] = { init_io },
	[0x6b] = { init_sub },
	[0x6d] = { init_ram_condition },
	[0x6e] = { init_nv_reg },
	[0x6f] = { init_macro },
	[0x71] = { init_done },
	[0x72] = { init_resume },
	[0x73] = { init_strap_condition },
	[0x74] = { init_time },
	[0x75] = { init_condition },
	[0x76] = { init_io_condition },
	[0x77] = { init_zm_reg16 },
	[0x78] = { init_index_io },
	[0x79] = { init_pll },
	[0x7a] = { init_zm_reg },
	[0x87] = { init_ram_restrict_pll },
	[0x8c] = { init_reserved },
	[0x8d] = { init_reserved },
	[0x8e] = { init_gpio },
	[0x8f] = { init_ram_restrict_zm_reg_group },
	[0x90] = { init_copy_zm_reg },
	[0x91] = { init_zm_reg_group },
	[0x92] = { init_reserved },
	[0x96] = { init_xlat },
	[0x97] = { init_zm_mask_add },
	[0x98] = { init_auxch },
	[0x99] = { init_zm_auxch },
	[0x9a] = { init_i2c_long_if },
	[0xa9] = { init_gpio_ne },
	[0xaa] = { init_reserved },
};

int
nvbios_exec(struct nvbios_init *init)
{
	struct nvkm_bios *bios = init->subdev->device->bios;

	init->nested++;
	while (init->offset) {
		u8 opcode = nvbios_rd08(bios, init->offset);
		if (opcode >= ARRAY_SIZE(init_opcode) ||
		    !init_opcode[opcode].exec) {
			error("unknown opcode 0x%02x\n", opcode);
			return -EINVAL;
		}

		init_opcode[opcode].exec(init);
	}
	init->nested--;
	return 0;
}

int
nvbios_post(struct nvkm_subdev *subdev, bool execute)
{
	struct nvkm_bios *bios = subdev->device->bios;
	int ret = 0;
	int i = -1;
	u16 data;

	if (execute)
		nvkm_debug(subdev, "running init tables\n");
	while (!ret && (data = (init_script(bios, ++i)))) {
		ret = nvbios_init(subdev, data,
			init.execute = execute ? 1 : 0;
		      );
	}

	/* the vbios parser will run this right after the normal init
	 * tables, whereas the binary driver appears to run it later.
	 */
	if (!ret && (data = init_unknown_script(bios))) {
		ret = nvbios_init(subdev, data,
			init.execute = execute ? 1 : 0;
		      );
	}

	return ret;
}
