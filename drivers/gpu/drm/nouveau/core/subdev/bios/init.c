#include <core/engine.h>
#include <core/device.h>

#include <subdev/bios.h>
#include <subdev/bios/bmp.h>
#include <subdev/bios/bit.h>
#include <subdev/bios/conn.h>
#include <subdev/bios/dcb.h>
#include <subdev/bios/dp.h>
#include <subdev/bios/gpio.h>
#include <subdev/bios/init.h>
#include <subdev/devinit.h>
#include <subdev/clock.h>
#include <subdev/i2c.h>
#include <subdev/vga.h>
#include <subdev/gpio.h>

#define bioslog(lvl, fmt, args...) do {                                        \
	nv_printk(init->bios, lvl, "0x%04x[%c]: "fmt, init->offset,            \
		  init_exec(init) ? '0' + (init->nested - 1) : ' ', ##args);   \
} while(0)
#define cont(fmt, args...) do {                                                \
	if (nv_subdev(init->bios)->debug >= NV_DBG_TRACE)                      \
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
		if (init->outp)
			return ffs(init->outp->or) - 1;
		error("script needs OR!!\n");
	}
	return 0;
}

static inline int
init_link(struct nvbios_init *init)
{
	if (init_exec(init)) {
		if (init->outp)
			return !(init->outp->sorconf.link & 1);
		error("script needs OR link\n");
	}
	return 0;
}

static inline int
init_crtc(struct nvbios_init *init)
{
	if (init_exec(init)) {
		if (init->crtc >= 0)
			return init->crtc;
		error("script needs crtc\n");
	}
	return 0;
}

static u8
init_conn(struct nvbios_init *init)
{
	struct nouveau_bios *bios = init->bios;
	u8  ver, len;
	u16 conn;

	if (init_exec(init)) {
		if (init->outp) {
			conn = init->outp->connector;
			conn = dcb_conn(bios, conn, &ver, &len);
			if (conn)
				return nv_ro08(bios, conn);
		}

		error("script needs connector type\n");
	}

	return 0xff;
}

static inline u32
init_nvreg(struct nvbios_init *init, u32 reg)
{
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
	if (nv_device(init->bios)->card_type >= NV_50) {
		if (reg & 0x80000000) {
			reg += init_crtc(init) * 0x800;
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
	return reg;
}

static u32
init_rd32(struct nvbios_init *init, u32 reg)
{
	reg = init_nvreg(init, reg);
	if (init_exec(init))
		return nv_rd32(init->subdev, reg);
	return 0x00000000;
}

static void
init_wr32(struct nvbios_init *init, u32 reg, u32 val)
{
	reg = init_nvreg(init, reg);
	if (init_exec(init))
		nv_wr32(init->subdev, reg, val);
}

static u32
init_mask(struct nvbios_init *init, u32 reg, u32 mask, u32 val)
{
	reg = init_nvreg(init, reg);
	if (init_exec(init)) {
		u32 tmp = nv_rd32(init->subdev, reg);
		nv_wr32(init->subdev, reg, (tmp & ~mask) | val);
		return tmp;
	}
	return 0x00000000;
}

static u8
init_rdport(struct nvbios_init *init, u16 port)
{
	if (init_exec(init))
		return nv_rdport(init->subdev, init->crtc, port);
	return 0x00;
}

static void
init_wrport(struct nvbios_init *init, u16 port, u8 value)
{
	if (init_exec(init))
		nv_wrport(init->subdev, init->crtc, port, value);
}

static u8
init_rdvgai(struct nvbios_init *init, u16 port, u8 index)
{
	struct nouveau_subdev *subdev = init->subdev;
	if (init_exec(init)) {
		int head = init->crtc < 0 ? 0 : init->crtc;
		return nv_rdvgai(subdev, head, port, index);
	}
	return 0x00;
}

static void
init_wrvgai(struct nvbios_init *init, u16 port, u8 index, u8 value)
{
	/* force head 0 for updates to cr44, it only exists on first head */
	if (nv_device(init->subdev)->card_type < NV_50) {
		if (port == 0x03d4 && index == 0x44)
			init->crtc = 0;
	}

	if (init_exec(init)) {
		int head = init->crtc < 0 ? 0 : init->crtc;
		nv_wrvgai(init->subdev, head, port, index, value);
	}

	/* select head 1 if cr44 write selected it */
	if (nv_device(init->subdev)->card_type < NV_50) {
		if (port == 0x03d4 && index == 0x44 && value == 3)
			init->crtc = 1;
	}
}

static struct nouveau_i2c_port *
init_i2c(struct nvbios_init *init, int index)
{
	struct nouveau_i2c *i2c = nouveau_i2c(init->bios);

	if (index == 0xff) {
		index = NV_I2C_DEFAULT(0);
		if (init->outp && init->outp->i2c_upper_default)
			index = NV_I2C_DEFAULT(1);
	} else
	if (index < 0) {
		if (!init->outp) {
			if (init_exec(init))
				error("script needs output for i2c\n");
			return NULL;
		}

		if (index == -2 && init->outp->location) {
			index = NV_I2C_TYPE_EXTAUX(init->outp->extdev);
			return i2c->find_type(i2c, index);
		}

		index = init->outp->i2c_index;
	}

	return i2c->find(i2c, index);
}

static int
init_rdi2cr(struct nvbios_init *init, u8 index, u8 addr, u8 reg)
{
	struct nouveau_i2c_port *port = init_i2c(init, index);
	if (port && init_exec(init))
		return nv_rdi2cr(port, addr, reg);
	return -ENODEV;
}

static int
init_wri2cr(struct nvbios_init *init, u8 index, u8 addr, u8 reg, u8 val)
{
	struct nouveau_i2c_port *port = init_i2c(init, index);
	if (port && init_exec(init))
		return nv_wri2cr(port, addr, reg, val);
	return -ENODEV;
}

static int
init_rdauxr(struct nvbios_init *init, u32 addr)
{
	struct nouveau_i2c_port *port = init_i2c(init, -2);
	u8 data;

	if (port && init_exec(init)) {
		int ret = nv_rdaux(port, addr, &data, 1);
		if (ret)
			return ret;
		return data;
	}

	return -ENODEV;
}

static int
init_wrauxr(struct nvbios_init *init, u32 addr, u8 data)
{
	struct nouveau_i2c_port *port = init_i2c(init, -2);
	if (port && init_exec(init))
		return nv_wraux(port, addr, &data, 1);
	return -ENODEV;
}

static void
init_prog_pll(struct nvbios_init *init, u32 id, u32 freq)
{
	struct nouveau_clock *clk = nouveau_clock(init->bios);
	if (clk && clk->pll_set && init_exec(init)) {
		int ret = clk->pll_set(clk, id, freq);
		if (ret)
			warn("failed to prog pll 0x%08x to %dkHz\n", id, freq);
	}
}

/******************************************************************************
 * parsing of bios structures that are required to execute init tables
 *****************************************************************************/

static u16
init_table(struct nouveau_bios *bios, u16 *len)
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
	struct nouveau_bios *bios = init->bios;
	u16 len, data = init_table(bios, &len);
	if (data) {
		if (len >= offset + 2) {
			data = nv_ro16(bios, data + offset);
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
init_script(struct nouveau_bios *bios, int index)
{
	struct nvbios_init init = { .bios = bios };
	u16 data;

	if (bmp_version(bios) && bmp_version(bios) < 0x0510) {
		if (index > 1)
			return 0x0000;

		data = bios->bmp_offset + (bios->version.major < 2 ? 14 : 18);
		return nv_ro16(bios, data + (index * 2));
	}

	data = init_script_table(&init);
	if (data)
		return nv_ro16(bios, data + (index * 2));

	return 0x0000;
}

static u16
init_unknown_script(struct nouveau_bios *bios)
{
	u16 len, data = init_table(bios, &len);
	if (data && len >= 16)
		return nv_ro16(bios, data + 14);
	return 0x0000;
}

static u16
init_ram_restrict_table(struct nvbios_init *init)
{
	struct nouveau_bios *bios = init->bios;
	struct bit_entry bit_M;
	u16 data = 0x0000;

	if (!bit_entry(bios, 'M', &bit_M)) {
		if (bit_M.version == 1 && bit_M.length >= 5)
			data = nv_ro16(bios, bit_M.offset + 3);
		if (bit_M.version == 2 && bit_M.length >= 3)
			data = nv_ro16(bios, bit_M.offset + 1);
	}

	if (data == 0x0000)
		warn("ram restrict table not found\n");
	return data;
}

static u8
init_ram_restrict_group_count(struct nvbios_init *init)
{
	struct nouveau_bios *bios = init->bios;
	struct bit_entry bit_M;

	if (!bit_entry(bios, 'M', &bit_M)) {
		if (bit_M.version == 1 && bit_M.length >= 5)
			return nv_ro08(bios, bit_M.offset + 2);
		if (bit_M.version == 2 && bit_M.length >= 3)
			return nv_ro08(bios, bit_M.offset + 0);
	}

	return 0x00;
}

static u8
init_ram_restrict_strap(struct nvbios_init *init)
{
	/* This appears to be the behaviour of the VBIOS parser, and *is*
	 * important to cache the NV_PEXTDEV_BOOT0 on later chipsets to
	 * avoid fucking up the memory controller (somehow) by reading it
	 * on every INIT_RAM_RESTRICT_ZM_GROUP opcode.
	 *
	 * Preserving the non-caching behaviour on earlier chipsets just
	 * in case *not* re-reading the strap causes similar breakage.
	 */
	if (!init->ramcfg || init->bios->version.major < 0x70)
		init->ramcfg = init_rd32(init, 0x101000);
	return (init->ramcfg & 0x00000003c) >> 2;
}

static u8
init_ram_restrict(struct nvbios_init *init)
{
	u8  strap = init_ram_restrict_strap(init);
	u16 table = init_ram_restrict_table(init);
	if (table)
		return nv_ro08(init->bios, table + strap);
	return 0x00;
}

static u8
init_xlat_(struct nvbios_init *init, u8 index, u8 offset)
{
	struct nouveau_bios *bios = init->bios;
	u16 table = init_xlat_table(init);
	if (table) {
		u16 data = nv_ro16(bios, table + (index * 2));
		if (data)
			return nv_ro08(bios, data + offset);
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
	struct nouveau_bios *bios = init->bios;
	u16 table = init_condition_table(init);
	if (table) {
		u32 reg = nv_ro32(bios, table + (cond * 12) + 0);
		u32 msk = nv_ro32(bios, table + (cond * 12) + 4);
		u32 val = nv_ro32(bios, table + (cond * 12) + 8);
		trace("\t[0x%02x] (R[0x%06x] & 0x%08x) == 0x%08x\n",
		      cond, reg, msk, val);
		return (init_rd32(init, reg) & msk) == val;
	}
	return false;
}

static bool
init_io_condition_met(struct nvbios_init *init, u8 cond)
{
	struct nouveau_bios *bios = init->bios;
	u16 table = init_io_condition_table(init);
	if (table) {
		u16 port = nv_ro16(bios, table + (cond * 5) + 0);
		u8 index = nv_ro08(bios, table + (cond * 5) + 2);
		u8  mask = nv_ro08(bios, table + (cond * 5) + 3);
		u8 value = nv_ro08(bios, table + (cond * 5) + 4);
		trace("\t[0x%02x] (0x%04x[0x%02x] & 0x%02x) == 0x%02x\n",
		      cond, port, index, mask, value);
		return (init_rdvgai(init, port, index) & mask) == value;
	}
	return false;
}

static bool
init_io_flag_condition_met(struct nvbios_init *init, u8 cond)
{
	struct nouveau_bios *bios = init->bios;
	u16 table = init_io_flag_condition_table(init);
	if (table) {
		u16 port = nv_ro16(bios, table + (cond * 9) + 0);
		u8 index = nv_ro08(bios, table + (cond * 9) + 2);
		u8  mask = nv_ro08(bios, table + (cond * 9) + 3);
		u8 shift = nv_ro08(bios, table + (cond * 9) + 4);
		u16 data = nv_ro16(bios, table + (cond * 9) + 5);
		u8 dmask = nv_ro08(bios, table + (cond * 9) + 7);
		u8 value = nv_ro08(bios, table + (cond * 9) + 8);
		u8 ioval = (init_rdvgai(init, port, index) & mask) >> shift;
		return (nv_ro08(bios, data + ioval) & dmask) == value;
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
	u8 opcode = nv_ro08(init->bios, init->offset);
	trace("RESERVED\t0x%02x\n", opcode);
	init->offset += 1;
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
	struct nouveau_bios *bios = init->bios;
	u16 port = nv_ro16(bios, init->offset + 1);
	u8 index = nv_ro08(bios, init->offset + 3);
	u8  mask = nv_ro08(bios, init->offset + 4);
	u8 shift = nv_ro08(bios, init->offset + 5);
	u8 count = nv_ro08(bios, init->offset + 6);
	u32  reg = nv_ro32(bios, init->offset + 7);
	u8 conf, i;

	trace("IO_RESTRICT_PROG\tR[0x%06x] = "
	      "((0x%04x[0x%02x] & 0x%02x) >> %d) [{\n",
	      reg, port, index, mask, shift);
	init->offset += 11;

	conf = (init_rdvgai(init, port, index) & mask) >> shift;
	for (i = 0; i < count; i++) {
		u32 data = nv_ro32(bios, init->offset);

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
	struct nouveau_bios *bios = init->bios;
	u8 count = nv_ro08(bios, init->offset + 1);
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
	struct nouveau_bios *bios = init->bios;
	u16 port = nv_ro16(bios, init->offset + 1);
	u8 index = nv_ro08(bios, init->offset + 3);
	u8  mask = nv_ro08(bios, init->offset + 4);
	u8 shift = nv_ro08(bios, init->offset + 5);
	s8  iofc = nv_ro08(bios, init->offset + 6);
	u8 count = nv_ro08(bios, init->offset + 7);
	u32  reg = nv_ro32(bios, init->offset + 8);
	u8 conf, i;

	trace("IO_RESTRICT_PLL\tR[0x%06x] =PLL= "
	      "((0x%04x[0x%02x] & 0x%02x) >> 0x%02x) IOFCOND 0x%02x [{\n",
	      reg, port, index, mask, shift, iofc);
	init->offset += 12;

	conf = (init_rdvgai(init, port, index) & mask) >> shift;
	for (i = 0; i < count; i++) {
		u32 freq = nv_ro16(bios, init->offset) * 10;

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
	struct nouveau_bios *bios = init->bios;
	u32  reg = nv_ro32(bios, init->offset + 1);
	u8 shift = nv_ro08(bios, init->offset + 5);
	u8 smask = nv_ro08(bios, init->offset + 6);
	u16 port = nv_ro16(bios, init->offset + 7);
	u8 index = nv_ro08(bios, init->offset + 9);
	u8  mask = nv_ro08(bios, init->offset + 10);
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
	struct nouveau_bios *bios = init->bios;
	u8 cond = nv_ro08(bios, init->offset + 1);

	trace("IO_FLAG_CONDITION\t0x%02x\n", cond);
	init->offset += 2;

	if (!init_io_flag_condition_met(init, cond))
		init_exec_set(init, false);
}

/**
 * INIT_DP_CONDITION - opcode 0x3a
 *
 */
static void
init_dp_condition(struct nvbios_init *init)
{
	struct nouveau_bios *bios = init->bios;
	struct nvbios_dpout info;
	u8  cond = nv_ro08(bios, init->offset + 1);
	u8  unkn = nv_ro08(bios, init->offset + 2);
	u8  ver, hdr, cnt, len;
	u16 data;

	trace("DP_CONDITION\t0x%02x 0x%02x\n", cond, unkn);
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
		warn("unknown dp condition 0x%02x\n", cond);
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
	struct nouveau_bios *bios = init->bios;
	u8 index = nv_ro08(bios, init->offset + 1);
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
	struct nouveau_bios *bios = init->bios;
	u8 index = nv_ro08(bios, init->offset + 1);
	u8    or = init_or(init);
	u8  data;

	trace("IO_OR\t0x03d4[0x%02x] |= (1 << 0x%02x)\n", index, or);
	init->offset += 2;

	data = init_rdvgai(init, 0x03d4, index);
	init_wrvgai(init, 0x03d4, index, data | (1 << or));
}

/**
 * INIT_INDEX_ADDRESS_LATCHED - opcode 0x49
 *
 */
static void
init_idx_addr_latched(struct nvbios_init *init)
{
	struct nouveau_bios *bios = init->bios;
	u32 creg = nv_ro32(bios, init->offset + 1);
	u32 dreg = nv_ro32(bios, init->offset + 5);
	u32 mask = nv_ro32(bios, init->offset + 9);
	u32 data = nv_ro32(bios, init->offset + 13);
	u8 count = nv_ro08(bios, init->offset + 17);

	trace("INDEX_ADDRESS_LATCHED\t"
	      "R[0x%06x] : R[0x%06x]\n\tCTRL &= 0x%08x |= 0x%08x\n",
	      creg, dreg, mask, data);
	init->offset += 18;

	while (count--) {
		u8 iaddr = nv_ro08(bios, init->offset + 0);
		u8 idata = nv_ro08(bios, init->offset + 1);

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
	struct nouveau_bios *bios = init->bios;
	u16 port = nv_ro16(bios, init->offset + 1);
	u8 index = nv_ro08(bios, init->offset + 3);
	u8  mask = nv_ro08(bios, init->offset + 4);
	u8 shift = nv_ro08(bios, init->offset + 5);
	u8 count = nv_ro08(bios, init->offset + 6);
	u32  reg = nv_ro32(bios, init->offset + 7);
	u8  conf, i;

	trace("IO_RESTRICT_PLL2\t"
	      "R[0x%06x] =PLL= ((0x%04x[0x%02x] & 0x%02x) >> 0x%02x) [{\n",
	      reg, port, index, mask, shift);
	init->offset += 11;

	conf = (init_rdvgai(init, port, index) & mask) >> shift;
	for (i = 0; i < count; i++) {
		u32 freq = nv_ro32(bios, init->offset);
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
	struct nouveau_bios *bios = init->bios;
	u32  reg = nv_ro32(bios, init->offset + 1);
	u32 freq = nv_ro32(bios, init->offset + 5);

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
	struct nouveau_bios *bios = init->bios;
	u8 index = nv_ro08(bios, init->offset + 1);
	u8  addr = nv_ro08(bios, init->offset + 2) >> 1;
	u8 count = nv_ro08(bios, init->offset + 3);

	trace("I2C_BYTE\tI2C[0x%02x][0x%02x]\n", index, addr);
	init->offset += 4;

	while (count--) {
		u8  reg = nv_ro08(bios, init->offset + 0);
		u8 mask = nv_ro08(bios, init->offset + 1);
		u8 data = nv_ro08(bios, init->offset + 2);
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
	struct nouveau_bios *bios = init->bios;
	u8 index = nv_ro08(bios, init->offset + 1);
	u8  addr = nv_ro08(bios, init->offset + 2) >> 1;
	u8 count = nv_ro08(bios, init->offset + 3);

	trace("ZM_I2C_BYTE\tI2C[0x%02x][0x%02x]\n", index, addr);
	init->offset += 4;

	while (count--) {
		u8  reg = nv_ro08(bios, init->offset + 0);
		u8 data = nv_ro08(bios, init->offset + 1);

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
	struct nouveau_bios *bios = init->bios;
	u8 index = nv_ro08(bios, init->offset + 1);
	u8  addr = nv_ro08(bios, init->offset + 2) >> 1;
	u8 count = nv_ro08(bios, init->offset + 3);
	u8 data[256], i;

	trace("ZM_I2C\tI2C[0x%02x][0x%02x]\n", index, addr);
	init->offset += 4;

	for (i = 0; i < count; i++) {
		data[i] = nv_ro08(bios, init->offset);
		trace("\t0x%02x\n", data[i]);
		init->offset++;
	}

	if (init_exec(init)) {
		struct nouveau_i2c_port *port = init_i2c(init, index);
		struct i2c_msg msg = {
			.addr = addr, .flags = 0, .len = count, .buf = data,
		};
		int ret;

		if (port && (ret = i2c_transfer(&port->adapter, &msg, 1)) != 1)
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
	struct nouveau_bios *bios = init->bios;
	u8 tmds = nv_ro08(bios, init->offset + 1);
	u8 addr = nv_ro08(bios, init->offset + 2);
	u8 mask = nv_ro08(bios, init->offset + 3);
	u8 data = nv_ro08(bios, init->offset + 4);
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
	struct nouveau_bios *bios = init->bios;
	u8  tmds = nv_ro08(bios, init->offset + 1);
	u8 count = nv_ro08(bios, init->offset + 2);
	u32  reg = init_tmds_reg(init, tmds);

	trace("TMDS_ZM_GROUP\tT[0x%02x]\n", tmds);
	init->offset += 3;

	while (count--) {
		u8 addr = nv_ro08(bios, init->offset + 0);
		u8 data = nv_ro08(bios, init->offset + 1);

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
	struct nouveau_bios *bios = init->bios;
	u8 addr0 = nv_ro08(bios, init->offset + 1);
	u8 addr1 = nv_ro08(bios, init->offset + 2);
	u8  base = nv_ro08(bios, init->offset + 3);
	u8 count = nv_ro08(bios, init->offset + 4);
	u8 save0;

	trace("CR_INDEX_ADDR C[%02x] C[%02x]\n", addr0, addr1);
	init->offset += 5;

	save0 = init_rdvgai(init, 0x03d4, addr0);
	while (count--) {
		u8 data = nv_ro08(bios, init->offset);

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
	struct nouveau_bios *bios = init->bios;
	u8 addr = nv_ro08(bios, init->offset + 1);
	u8 mask = nv_ro08(bios, init->offset + 2);
	u8 data = nv_ro08(bios, init->offset + 3);
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
	struct nouveau_bios *bios = init->bios;
	u8 addr = nv_ro08(bios, init->offset + 1);
	u8 data = nv_ro08(bios, init->offset + 2);

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
	struct nouveau_bios *bios = init->bios;
	u8 count = nv_ro08(bios, init->offset + 1);

	trace("ZM_CR_GROUP\n");
	init->offset += 2;

	while (count--) {
		u8 addr = nv_ro08(bios, init->offset + 0);
		u8 data = nv_ro08(bios, init->offset + 1);

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
	struct nouveau_bios *bios = init->bios;
	u8  cond = nv_ro08(bios, init->offset + 1);
	u8 retry = nv_ro08(bios, init->offset + 2);
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
	struct nouveau_bios *bios = init->bios;
	u16 msec = nv_ro16(bios, init->offset + 1);

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
	struct nouveau_bios *bios = init->bios;
	u32 base = nv_ro32(bios, init->offset + 1);
	u8 count = nv_ro08(bios, init->offset + 5);

	trace("ZM_REG_SEQUENCE\t0x%02x\n", count);
	init->offset += 6;

	while (count--) {
		u32 data = nv_ro32(bios, init->offset);

		trace("\t\tR[0x%06x] = 0x%08x\n", base, data);
		init->offset += 4;

		init_wr32(init, base, data);
		base += 4;
	}
}

/**
 * INIT_SUB_DIRECT - opcode 0x5b
 *
 */
static void
init_sub_direct(struct nvbios_init *init)
{
	struct nouveau_bios *bios = init->bios;
	u16 addr = nv_ro16(bios, init->offset + 1);
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
	struct nouveau_bios *bios = init->bios;
	u16 offset = nv_ro16(bios, init->offset + 1);

	trace("JUMP\t0x%04x\n", offset);
	init->offset = offset;
}

/**
 * INIT_I2C_IF - opcode 0x5e
 *
 */
static void
init_i2c_if(struct nvbios_init *init)
{
	struct nouveau_bios *bios = init->bios;
	u8 index = nv_ro08(bios, init->offset + 1);
	u8  addr = nv_ro08(bios, init->offset + 2);
	u8   reg = nv_ro08(bios, init->offset + 3);
	u8  mask = nv_ro08(bios, init->offset + 4);
	u8  data = nv_ro08(bios, init->offset + 5);
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
	struct nouveau_bios *bios = init->bios;
	u32  sreg = nv_ro32(bios, init->offset + 1);
	u8  shift = nv_ro08(bios, init->offset + 5);
	u32 smask = nv_ro32(bios, init->offset + 6);
	u32  sxor = nv_ro32(bios, init->offset + 10);
	u32  dreg = nv_ro32(bios, init->offset + 14);
	u32 dmask = nv_ro32(bios, init->offset + 18);
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
	struct nouveau_bios *bios = init->bios;
	u16 port = nv_ro16(bios, init->offset + 1);
	u8 index = nv_ro08(bios, init->offset + 3);
	u8  data = nv_ro08(bios, init->offset + 4);

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
	struct nouveau_devinit *devinit = nouveau_devinit(init->bios);

	trace("COMPUTE_MEM\n");
	init->offset += 1;

	init_exec_force(init, true);
	if (init_exec(init) && devinit->meminit)
		devinit->meminit(devinit);
	init_exec_force(init, false);
}

/**
 * INIT_RESET - opcode 0x65
 *
 */
static void
init_reset(struct nvbios_init *init)
{
	struct nouveau_bios *bios = init->bios;
	u32   reg = nv_ro32(bios, init->offset + 1);
	u32 data1 = nv_ro32(bios, init->offset + 5);
	u32 data2 = nv_ro32(bios, init->offset + 9);
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
	u16 mdata = bmp_mem_init_table(init->bios);
	if (mdata)
		mdata += (init_rdvgai(init, 0x03d4, 0x3c) >> 4) * 66;
	return mdata;
}

static void
init_configure_mem(struct nvbios_init *init)
{
	struct nouveau_bios *bios = init->bios;
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
	if (nv_ro08(bios, mdata) & 0x01)
		sdata = bmp_ddr_seq_table(bios);
	mdata += 6; /* skip to data */

	data = init_rdvgai(init, 0x03c4, 0x01);
	init_wrvgai(init, 0x03c4, 0x01, data | 0x20);

	while ((addr = nv_ro32(bios, sdata)) != 0xffffffff) {
		switch (addr) {
		case 0x10021c: /* CKE_NORMAL */
		case 0x1002d0: /* CMD_REFRESH */
		case 0x1002d4: /* CMD_PRECHARGE */
			data = 0x00000001;
			break;
		default:
			data = nv_ro32(bios, mdata);
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
	struct nouveau_bios *bios = init->bios;
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
	clock = nv_ro16(bios, mdata + 4) * 10;
	init_prog_pll(init, 0x680500, clock);

	/* MPLL */
	clock = nv_ro16(bios, mdata + 2) * 10;
	if (nv_ro08(bios, mdata) & 0x01)
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
	struct nouveau_bios *bios = init->bios;
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
	struct nouveau_bios *bios = init->bios;
	u16 port = nv_ro16(bios, init->offset + 1);
	u8  mask = nv_ro16(bios, init->offset + 3);
	u8  data = nv_ro16(bios, init->offset + 4);
	u8 value;

	trace("IO\t\tI[0x%04x] &= 0x%02x |= 0x%02x\n", port, mask, data);
	init->offset += 5;

	/* ummm.. yes.. should really figure out wtf this is and why it's
	 * needed some day..  it's almost certainly wrong, but, it also
	 * somehow makes things work...
	 */
	if (nv_device(init->bios)->card_type >= NV_50 &&
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
	struct nouveau_bios *bios = init->bios;
	u8 index = nv_ro08(bios, init->offset + 1);
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
	struct nouveau_bios *bios = init->bios;
	u8  mask = nv_ro08(bios, init->offset + 1);
	u8 value = nv_ro08(bios, init->offset + 2);

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
	struct nouveau_bios *bios = init->bios;
	u32  reg = nv_ro32(bios, init->offset + 1);
	u32 mask = nv_ro32(bios, init->offset + 5);
	u32 data = nv_ro32(bios, init->offset + 9);

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
	struct nouveau_bios *bios = init->bios;
	u8  macro = nv_ro08(bios, init->offset + 1);
	u16 table;

	trace("MACRO\t0x%02x\n", macro);

	table = init_macro_table(init);
	if (table) {
		u32 addr = nv_ro32(bios, table + (macro * 8) + 0);
		u32 data = nv_ro32(bios, table + (macro * 8) + 4);
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
 * INIT_TIME - opcode 0x74
 *
 */
static void
init_time(struct nvbios_init *init)
{
	struct nouveau_bios *bios = init->bios;
	u16 usec = nv_ro16(bios, init->offset + 1);

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
	struct nouveau_bios *bios = init->bios;
	u8 cond = nv_ro08(bios, init->offset + 1);

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
	struct nouveau_bios *bios = init->bios;
	u8 cond = nv_ro08(bios, init->offset + 1);

	trace("IO_CONDITION\t0x%02x\n", cond);
	init->offset += 2;

	if (!init_io_condition_met(init, cond))
		init_exec_set(init, false);
}

/**
 * INIT_INDEX_IO - opcode 0x78
 *
 */
static void
init_index_io(struct nvbios_init *init)
{
	struct nouveau_bios *bios = init->bios;
	u16 port = nv_ro16(bios, init->offset + 1);
	u8 index = nv_ro16(bios, init->offset + 3);
	u8  mask = nv_ro08(bios, init->offset + 4);
	u8  data = nv_ro08(bios, init->offset + 5);
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
	struct nouveau_bios *bios = init->bios;
	u32  reg = nv_ro32(bios, init->offset + 1);
	u32 freq = nv_ro16(bios, init->offset + 5) * 10;

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
	struct nouveau_bios *bios = init->bios;
	u32 addr = nv_ro32(bios, init->offset + 1);
	u32 data = nv_ro32(bios, init->offset + 5);

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
	struct nouveau_bios *bios = init->bios;
	u8  type = nv_ro08(bios, init->offset + 1);
	u8 count = init_ram_restrict_group_count(init);
	u8 strap = init_ram_restrict(init);
	u8 cconf;

	trace("RAM_RESTRICT_PLL\t0x%02x\n", type);
	init->offset += 2;

	for (cconf = 0; cconf < count; cconf++) {
		u32 freq = nv_ro32(bios, init->offset);

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
	struct nouveau_gpio *gpio = nouveau_gpio(init->bios);

	trace("GPIO\n");
	init->offset += 1;

	if (init_exec(init) && gpio && gpio->reset)
		gpio->reset(gpio, DCB_GPIO_UNUSED);
}

/**
 * INIT_RAM_RESTRICT_ZM_GROUP - opcode 0x8f
 *
 */
static void
init_ram_restrict_zm_reg_group(struct nvbios_init *init)
{
	struct nouveau_bios *bios = init->bios;
	u32 addr = nv_ro32(bios, init->offset + 1);
	u8  incr = nv_ro08(bios, init->offset + 5);
	u8   num = nv_ro08(bios, init->offset + 6);
	u8 count = init_ram_restrict_group_count(init);
	u8 index = init_ram_restrict(init);
	u8 i, j;

	trace("RAM_RESTRICT_ZM_REG_GROUP\t"
	      "R[0x%08x] 0x%02x 0x%02x\n", addr, incr, num);
	init->offset += 7;

	for (i = 0; i < num; i++) {
		trace("\tR[0x%06x] = {\n", addr);
		for (j = 0; j < count; j++) {
			u32 data = nv_ro32(bios, init->offset);

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
	struct nouveau_bios *bios = init->bios;
	u32 sreg = nv_ro32(bios, init->offset + 1);
	u32 dreg = nv_ro32(bios, init->offset + 5);

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
	struct nouveau_bios *bios = init->bios;
	u32 addr = nv_ro32(bios, init->offset + 1);
	u8 count = nv_ro08(bios, init->offset + 5);

	trace("ZM_REG_GROUP\tR[0x%06x] =\n", addr);
	init->offset += 6;

	while (count--) {
		u32 data = nv_ro32(bios, init->offset);
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
	struct nouveau_bios *bios = init->bios;
	u32 saddr = nv_ro32(bios, init->offset + 1);
	u8 sshift = nv_ro08(bios, init->offset + 5);
	u8  smask = nv_ro08(bios, init->offset + 6);
	u8  index = nv_ro08(bios, init->offset + 7);
	u32 daddr = nv_ro32(bios, init->offset + 8);
	u32 dmask = nv_ro32(bios, init->offset + 12);
	u8  shift = nv_ro08(bios, init->offset + 16);
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
	struct nouveau_bios *bios = init->bios;
	u32 addr = nv_ro32(bios, init->offset + 1);
	u32 mask = nv_ro32(bios, init->offset + 5);
	u32  add = nv_ro32(bios, init->offset + 9);
	u32 data;

	trace("ZM_MASK_ADD\tR[0x%06x] &= 0x%08x += 0x%08x\n", addr, mask, add);
	init->offset += 13;

	data  =  init_rd32(init, addr) & mask;
	data |= ((data + add) & ~mask);
	init_wr32(init, addr, data);
}

/**
 * INIT_AUXCH - opcode 0x98
 *
 */
static void
init_auxch(struct nvbios_init *init)
{
	struct nouveau_bios *bios = init->bios;
	u32 addr = nv_ro32(bios, init->offset + 1);
	u8 count = nv_ro08(bios, init->offset + 5);

	trace("AUXCH\tAUX[0x%08x] 0x%02x\n", addr, count);
	init->offset += 6;

	while (count--) {
		u8 mask = nv_ro08(bios, init->offset + 0);
		u8 data = nv_ro08(bios, init->offset + 1);
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
	struct nouveau_bios *bios = init->bios;
	u32 addr = nv_ro32(bios, init->offset + 1);
	u8 count = nv_ro08(bios, init->offset + 5);

	trace("ZM_AUXCH\tAUX[0x%08x] 0x%02x\n", addr, count);
	init->offset += 6;

	while (count--) {
		u8 data = nv_ro08(bios, init->offset + 0);
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
	struct nouveau_bios *bios = init->bios;
	u8 index = nv_ro08(bios, init->offset + 1);
	u8  addr = nv_ro08(bios, init->offset + 2) >> 1;
	u8 reglo = nv_ro08(bios, init->offset + 3);
	u8 reghi = nv_ro08(bios, init->offset + 4);
	u8  mask = nv_ro08(bios, init->offset + 5);
	u8  data = nv_ro08(bios, init->offset + 6);
	struct nouveau_i2c_port *port;

	trace("I2C_LONG_IF\t"
	      "I2C[0x%02x][0x%02x][0x%02x%02x] & 0x%02x == 0x%02x\n",
	      index, addr, reglo, reghi, mask, data);
	init->offset += 7;

	port = init_i2c(init, index);
	if (port) {
		u8 i[2] = { reghi, reglo };
		u8 o[1] = {};
		struct i2c_msg msg[] = {
			{ .addr = addr, .flags = 0, .len = 2, .buf = i },
			{ .addr = addr, .flags = I2C_M_RD, .len = 1, .buf = o }
		};
		int ret;

		ret = i2c_transfer(&port->adapter, msg, 2);
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
	struct nouveau_bios *bios = init->bios;
	struct nouveau_gpio *gpio = nouveau_gpio(bios);
	struct dcb_gpio_func func;
	u8 count = nv_ro08(bios, init->offset + 1);
	u8 idx = 0, ver, len;
	u16 data, i;

	trace("GPIO_NE\t");
	init->offset += 2;

	for (i = init->offset; i < init->offset + count; i++)
		cont("0x%02x ", nv_ro08(bios, i));
	cont("\n");

	while ((data = dcb_gpio_parse(bios, 0, idx++, &ver, &len, &func))) {
		if (func.func != DCB_GPIO_UNUSED) {
			for (i = init->offset; i < init->offset + count; i++) {
				if (func.func == nv_ro08(bios, i))
					break;
			}

			trace("\tFUNC[0x%02x]", func.func);
			if (i == (init->offset + count)) {
				cont(" *");
				if (init_exec(init) && gpio && gpio->reset)
					gpio->reset(gpio, func.func);
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
	[0x3a] = { init_dp_condition },
	[0x3b] = { init_io_mask_or },
	[0x3c] = { init_io_or },
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
	[0x74] = { init_time },
	[0x75] = { init_condition },
	[0x76] = { init_io_condition },
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
};

#define init_opcode_nr (sizeof(init_opcode) / sizeof(init_opcode[0]))

int
nvbios_exec(struct nvbios_init *init)
{
	init->nested++;
	while (init->offset) {
		u8 opcode = nv_ro08(init->bios, init->offset);
		if (opcode >= init_opcode_nr || !init_opcode[opcode].exec) {
			error("unknown opcode 0x%02x\n", opcode);
			return -EINVAL;
		}

		init_opcode[opcode].exec(init);
	}
	init->nested--;
	return 0;
}

int
nvbios_init(struct nouveau_subdev *subdev, bool execute)
{
	struct nouveau_bios *bios = nouveau_bios(subdev);
	int ret = 0;
	int i = -1;
	u16 data;

	if (execute)
		nv_info(bios, "running init tables\n");
	while (!ret && (data = (init_script(bios, ++i)))) {
		struct nvbios_init init = {
			.subdev = subdev,
			.bios = bios,
			.offset = data,
			.outp = NULL,
			.crtc = -1,
			.execute = execute ? 1 : 0,
		};

		ret = nvbios_exec(&init);
	}

	/* the vbios parser will run this right after the normal init
	 * tables, whereas the binary driver appears to run it later.
	 */
	if (!ret && (data = init_unknown_script(bios))) {
		struct nvbios_init init = {
			.subdev = subdev,
			.bios = bios,
			.offset = data,
			.outp = NULL,
			.crtc = -1,
			.execute = execute ? 1 : 0,
		};

		ret = nvbios_exec(&init);
	}

	return 0;
}
