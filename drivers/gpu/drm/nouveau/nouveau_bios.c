/*
 * Copyright 2005-2006 Erik Waling
 * Copyright 2006 Stephane Marchesin
 * Copyright 2007-2009 Stuart Bennett
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
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "drmP.h"
#define NV_DEBUG_NOTRACE
#include "nouveau_drv.h"
#include "nouveau_hw.h"

/* these defines are made up */
#define NV_CIO_CRE_44_HEADA 0x0
#define NV_CIO_CRE_44_HEADB 0x3
#define FEATURE_MOBILE 0x10	/* also FEATURE_QUADRO for BMP */
#define LEGACY_I2C_CRT 0x80
#define LEGACY_I2C_PANEL 0x81
#define LEGACY_I2C_TV 0x82

#define EDID1_LEN 128

#define BIOSLOG(sip, fmt, arg...) NV_DEBUG(sip->dev, fmt, ##arg)
#define LOG_OLD_VALUE(x)

#define ROM16(x) le16_to_cpu(*(uint16_t *)&(x))
#define ROM32(x) le32_to_cpu(*(uint32_t *)&(x))

struct init_exec {
	bool execute;
	bool repeat;
};

static bool nv_cksum(const uint8_t *data, unsigned int length)
{
	/*
	 * There's a few checksums in the BIOS, so here's a generic checking
	 * function.
	 */
	int i;
	uint8_t sum = 0;

	for (i = 0; i < length; i++)
		sum += data[i];

	if (sum)
		return true;

	return false;
}

static int
score_vbios(struct drm_device *dev, const uint8_t *data, const bool writeable)
{
	if (!(data[0] == 0x55 && data[1] == 0xAA)) {
		NV_TRACEWARN(dev, "... BIOS signature not found\n");
		return 0;
	}

	if (nv_cksum(data, data[2] * 512)) {
		NV_TRACEWARN(dev, "... BIOS checksum invalid\n");
		/* if a ro image is somewhat bad, it's probably all rubbish */
		return writeable ? 2 : 1;
	} else
		NV_TRACE(dev, "... appears to be valid\n");

	return 3;
}

static void load_vbios_prom(struct drm_device *dev, uint8_t *data)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t pci_nv_20, save_pci_nv_20;
	int pcir_ptr;
	int i;

	if (dev_priv->card_type >= NV_50)
		pci_nv_20 = 0x88050;
	else
		pci_nv_20 = NV_PBUS_PCI_NV_20;

	/* enable ROM access */
	save_pci_nv_20 = nvReadMC(dev, pci_nv_20);
	nvWriteMC(dev, pci_nv_20,
		  save_pci_nv_20 & ~NV_PBUS_PCI_NV_20_ROM_SHADOW_ENABLED);

	/* bail if no rom signature */
	if (nv_rd08(dev, NV_PROM_OFFSET) != 0x55 ||
	    nv_rd08(dev, NV_PROM_OFFSET + 1) != 0xaa)
		goto out;

	/* additional check (see note below) - read PCI record header */
	pcir_ptr = nv_rd08(dev, NV_PROM_OFFSET + 0x18) |
		   nv_rd08(dev, NV_PROM_OFFSET + 0x19) << 8;
	if (nv_rd08(dev, NV_PROM_OFFSET + pcir_ptr) != 'P' ||
	    nv_rd08(dev, NV_PROM_OFFSET + pcir_ptr + 1) != 'C' ||
	    nv_rd08(dev, NV_PROM_OFFSET + pcir_ptr + 2) != 'I' ||
	    nv_rd08(dev, NV_PROM_OFFSET + pcir_ptr + 3) != 'R')
		goto out;

	/* on some 6600GT/6800LE prom reads are messed up.  nvclock alleges a
	 * a good read may be obtained by waiting or re-reading (cargocult: 5x)
	 * each byte.  we'll hope pramin has something usable instead
	 */
	for (i = 0; i < NV_PROM_SIZE; i++)
		data[i] = nv_rd08(dev, NV_PROM_OFFSET + i);

out:
	/* disable ROM access */
	nvWriteMC(dev, pci_nv_20,
		  save_pci_nv_20 | NV_PBUS_PCI_NV_20_ROM_SHADOW_ENABLED);
}

static void load_vbios_pramin(struct drm_device *dev, uint8_t *data)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t old_bar0_pramin = 0;
	int i;

	if (dev_priv->card_type >= NV_50) {
		uint32_t vbios_vram = (nv_rd32(dev, 0x619f04) & ~0xff) << 8;

		if (!vbios_vram)
			vbios_vram = (nv_rd32(dev, 0x1700) << 16) + 0xf0000;

		old_bar0_pramin = nv_rd32(dev, 0x1700);
		nv_wr32(dev, 0x1700, vbios_vram >> 16);
	}

	/* bail if no rom signature */
	if (nv_rd08(dev, NV_PRAMIN_OFFSET) != 0x55 ||
	    nv_rd08(dev, NV_PRAMIN_OFFSET + 1) != 0xaa)
		goto out;

	for (i = 0; i < NV_PROM_SIZE; i++)
		data[i] = nv_rd08(dev, NV_PRAMIN_OFFSET + i);

out:
	if (dev_priv->card_type >= NV_50)
		nv_wr32(dev, 0x1700, old_bar0_pramin);
}

static void load_vbios_pci(struct drm_device *dev, uint8_t *data)
{
	void __iomem *rom = NULL;
	size_t rom_len;
	int ret;

	ret = pci_enable_rom(dev->pdev);
	if (ret)
		return;

	rom = pci_map_rom(dev->pdev, &rom_len);
	if (!rom)
		goto out;
	memcpy_fromio(data, rom, rom_len);
	pci_unmap_rom(dev->pdev, rom);

out:
	pci_disable_rom(dev->pdev);
}

struct methods {
	const char desc[8];
	void (*loadbios)(struct drm_device *, uint8_t *);
	const bool rw;
};

static struct methods nv04_methods[] = {
	{ "PROM", load_vbios_prom, false },
	{ "PRAMIN", load_vbios_pramin, true },
	{ "PCIROM", load_vbios_pci, true },
};

static struct methods nv50_methods[] = {
	{ "PRAMIN", load_vbios_pramin, true },
	{ "PROM", load_vbios_prom, false },
	{ "PCIROM", load_vbios_pci, true },
};

#define METHODCNT 3

static bool NVShadowVBIOS(struct drm_device *dev, uint8_t *data)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct methods *methods;
	int i;
	int testscore = 3;
	int scores[METHODCNT];

	if (nouveau_vbios) {
		methods = nv04_methods;
		for (i = 0; i < METHODCNT; i++)
			if (!strcasecmp(nouveau_vbios, methods[i].desc))
				break;

		if (i < METHODCNT) {
			NV_INFO(dev, "Attempting to use BIOS image from %s\n",
				methods[i].desc);

			methods[i].loadbios(dev, data);
			if (score_vbios(dev, data, methods[i].rw))
				return true;
		}

		NV_ERROR(dev, "VBIOS source \'%s\' invalid\n", nouveau_vbios);
	}

	if (dev_priv->card_type < NV_50)
		methods = nv04_methods;
	else
		methods = nv50_methods;

	for (i = 0; i < METHODCNT; i++) {
		NV_TRACE(dev, "Attempting to load BIOS image from %s\n",
			 methods[i].desc);
		data[0] = data[1] = 0;	/* avoid reuse of previous image */
		methods[i].loadbios(dev, data);
		scores[i] = score_vbios(dev, data, methods[i].rw);
		if (scores[i] == testscore)
			return true;
	}

	while (--testscore > 0) {
		for (i = 0; i < METHODCNT; i++) {
			if (scores[i] == testscore) {
				NV_TRACE(dev, "Using BIOS image from %s\n",
					 methods[i].desc);
				methods[i].loadbios(dev, data);
				return true;
			}
		}
	}

	NV_ERROR(dev, "No valid BIOS image found\n");
	return false;
}

struct init_tbl_entry {
	char *name;
	uint8_t id;
	int (*handler)(struct nvbios *, uint16_t, struct init_exec *);
};

struct bit_entry {
	uint8_t id[2];
	uint16_t length;
	uint16_t offset;
};

static int parse_init_table(struct nvbios *, unsigned int, struct init_exec *);

#define MACRO_INDEX_SIZE	2
#define MACRO_SIZE		8
#define CONDITION_SIZE		12
#define IO_FLAG_CONDITION_SIZE	9
#define IO_CONDITION_SIZE	5
#define MEM_INIT_SIZE		66

static void still_alive(void)
{
#if 0
	sync();
	msleep(2);
#endif
}

static uint32_t
munge_reg(struct nvbios *bios, uint32_t reg)
{
	struct drm_nouveau_private *dev_priv = bios->dev->dev_private;
	struct dcb_entry *dcbent = bios->display.output;

	if (dev_priv->card_type < NV_50)
		return reg;

	if (reg & 0x40000000) {
		BUG_ON(!dcbent);

		reg += (ffs(dcbent->or) - 1) * 0x800;
		if ((reg & 0x20000000) && !(dcbent->sorconf.link & 1))
			reg += 0x00000080;
	}

	reg &= ~0x60000000;
	return reg;
}

static int
valid_reg(struct nvbios *bios, uint32_t reg)
{
	struct drm_nouveau_private *dev_priv = bios->dev->dev_private;
	struct drm_device *dev = bios->dev;

	/* C51 has misaligned regs on purpose. Marvellous */
	if (reg & 0x2 || (reg & 0x1 && dev_priv->VBIOS.pub.chip_version != 0x51)) {
		NV_ERROR(dev, "========== misaligned reg 0x%08X ==========\n",
			 reg);
		return 0;
	}
	/*
	 * Warn on C51 regs that have not been verified accessible in
	 * mmiotracing
	 */
	if (reg & 0x1 && dev_priv->VBIOS.pub.chip_version == 0x51 &&
	    reg != 0x130d && reg != 0x1311 && reg != 0x60081d)
		NV_WARN(dev, "=== C51 misaligned reg 0x%08X not verified ===\n",
			reg);

	/* Trust the init scripts on G80 */
	if (dev_priv->card_type >= NV_50)
		return 1;

	#define WITHIN(x, y, z) ((x >= y) && (x < y + z))
	if (WITHIN(reg, NV_PMC_OFFSET, NV_PMC_SIZE))
		return 1;
	if (WITHIN(reg, NV_PBUS_OFFSET, NV_PBUS_SIZE))
		return 1;
	if (WITHIN(reg, NV_PFIFO_OFFSET, NV_PFIFO_SIZE))
		return 1;
	if (dev_priv->VBIOS.pub.chip_version >= 0x30 &&
	    (WITHIN(reg, 0x4000, 0x600) || reg == 0x00004600))
		return 1;
	if (dev_priv->VBIOS.pub.chip_version >= 0x40 &&
						WITHIN(reg, 0xc000, 0x48))
		return 1;
	if (dev_priv->VBIOS.pub.chip_version >= 0x17 && reg == 0x0000d204)
		return 1;
	if (dev_priv->VBIOS.pub.chip_version >= 0x40) {
		if (reg == 0x00011014 || reg == 0x00020328)
			return 1;
		if (WITHIN(reg, 0x88000, NV_PBUS_SIZE)) /* new PBUS */
			return 1;
	}
	if (WITHIN(reg, NV_PFB_OFFSET, NV_PFB_SIZE))
		return 1;
	if (WITHIN(reg, NV_PEXTDEV_OFFSET, NV_PEXTDEV_SIZE))
		return 1;
	if (WITHIN(reg, NV_PCRTC0_OFFSET, NV_PCRTC0_SIZE * 2))
		return 1;
	if (WITHIN(reg, NV_PRAMDAC0_OFFSET, NV_PRAMDAC0_SIZE * 2))
		return 1;
	if (dev_priv->VBIOS.pub.chip_version >= 0x17 && reg == 0x0070fff0)
		return 1;
	if (dev_priv->VBIOS.pub.chip_version == 0x51 &&
				WITHIN(reg, NV_PRAMIN_OFFSET, NV_PRAMIN_SIZE))
		return 1;
	#undef WITHIN

	NV_ERROR(dev, "========== unknown reg 0x%08X ==========\n", reg);

	return 0;
}

static bool
valid_idx_port(struct nvbios *bios, uint16_t port)
{
	struct drm_nouveau_private *dev_priv = bios->dev->dev_private;
	struct drm_device *dev = bios->dev;

	/*
	 * If adding more ports here, the read/write functions below will need
	 * updating so that the correct mmio range (PRMCIO, PRMDIO, PRMVIO) is
	 * used for the port in question
	 */
	if (dev_priv->card_type < NV_50) {
		if (port == NV_CIO_CRX__COLOR)
			return true;
		if (port == NV_VIO_SRX)
			return true;
	} else {
		if (port == NV_CIO_CRX__COLOR)
			return true;
	}

	NV_ERROR(dev, "========== unknown indexed io port 0x%04X ==========\n",
		 port);

	return false;
}

static bool
valid_port(struct nvbios *bios, uint16_t port)
{
	struct drm_device *dev = bios->dev;

	/*
	 * If adding more ports here, the read/write functions below will need
	 * updating so that the correct mmio range (PRMCIO, PRMDIO, PRMVIO) is
	 * used for the port in question
	 */
	if (port == NV_VIO_VSE2)
		return true;

	NV_ERROR(dev, "========== unknown io port 0x%04X ==========\n", port);

	return false;
}

static uint32_t
bios_rd32(struct nvbios *bios, uint32_t reg)
{
	uint32_t data;

	reg = munge_reg(bios, reg);
	if (!valid_reg(bios, reg))
		return 0;

	/*
	 * C51 sometimes uses regs with bit0 set in the address. For these
	 * cases there should exist a translation in a BIOS table to an IO
	 * port address which the BIOS uses for accessing the reg
	 *
	 * These only seem to appear for the power control regs to a flat panel,
	 * and the GPIO regs at 0x60081*.  In C51 mmio traces the normal regs
	 * for 0x1308 and 0x1310 are used - hence the mask below.  An S3
	 * suspend-resume mmio trace from a C51 will be required to see if this
	 * is true for the power microcode in 0x14.., or whether the direct IO
	 * port access method is needed
	 */
	if (reg & 0x1)
		reg &= ~0x1;

	data = nv_rd32(bios->dev, reg);

	BIOSLOG(bios, "	Read:  Reg: 0x%08X, Data: 0x%08X\n", reg, data);

	return data;
}

static void
bios_wr32(struct nvbios *bios, uint32_t reg, uint32_t data)
{
	struct drm_nouveau_private *dev_priv = bios->dev->dev_private;

	reg = munge_reg(bios, reg);
	if (!valid_reg(bios, reg))
		return;

	/* see note in bios_rd32 */
	if (reg & 0x1)
		reg &= 0xfffffffe;

	LOG_OLD_VALUE(bios_rd32(bios, reg));
	BIOSLOG(bios, "	Write: Reg: 0x%08X, Data: 0x%08X\n", reg, data);

	if (dev_priv->VBIOS.execute) {
		still_alive();
		nv_wr32(bios->dev, reg, data);
	}
}

static uint8_t
bios_idxprt_rd(struct nvbios *bios, uint16_t port, uint8_t index)
{
	struct drm_nouveau_private *dev_priv = bios->dev->dev_private;
	struct drm_device *dev = bios->dev;
	uint8_t data;

	if (!valid_idx_port(bios, port))
		return 0;

	if (dev_priv->card_type < NV_50) {
		if (port == NV_VIO_SRX)
			data = NVReadVgaSeq(dev, bios->state.crtchead, index);
		else	/* assume NV_CIO_CRX__COLOR */
			data = NVReadVgaCrtc(dev, bios->state.crtchead, index);
	} else {
		uint32_t data32;

		data32 = bios_rd32(bios, NV50_PDISPLAY_VGACRTC(index & ~3));
		data = (data32 >> ((index & 3) << 3)) & 0xff;
	}

	BIOSLOG(bios, "	Indexed IO read:  Port: 0x%04X, Index: 0x%02X, "
		      "Head: 0x%02X, Data: 0x%02X\n",
		port, index, bios->state.crtchead, data);
	return data;
}

static void
bios_idxprt_wr(struct nvbios *bios, uint16_t port, uint8_t index, uint8_t data)
{
	struct drm_nouveau_private *dev_priv = bios->dev->dev_private;
	struct drm_device *dev = bios->dev;

	if (!valid_idx_port(bios, port))
		return;

	/*
	 * The current head is maintained in the nvbios member  state.crtchead.
	 * We trap changes to CR44 and update the head variable and hence the
	 * register set written.
	 * As CR44 only exists on CRTC0, we update crtchead to head0 in advance
	 * of the write, and to head1 after the write
	 */
	if (port == NV_CIO_CRX__COLOR && index == NV_CIO_CRE_44 &&
	    data != NV_CIO_CRE_44_HEADB)
		bios->state.crtchead = 0;

	LOG_OLD_VALUE(bios_idxprt_rd(bios, port, index));
	BIOSLOG(bios, "	Indexed IO write: Port: 0x%04X, Index: 0x%02X, "
		      "Head: 0x%02X, Data: 0x%02X\n",
		port, index, bios->state.crtchead, data);

	if (bios->execute && dev_priv->card_type < NV_50) {
		still_alive();
		if (port == NV_VIO_SRX)
			NVWriteVgaSeq(dev, bios->state.crtchead, index, data);
		else	/* assume NV_CIO_CRX__COLOR */
			NVWriteVgaCrtc(dev, bios->state.crtchead, index, data);
	} else
	if (bios->execute) {
		uint32_t data32, shift = (index & 3) << 3;

		still_alive();

		data32  = bios_rd32(bios, NV50_PDISPLAY_VGACRTC(index & ~3));
		data32 &= ~(0xff << shift);
		data32 |= (data << shift);
		bios_wr32(bios, NV50_PDISPLAY_VGACRTC(index & ~3), data32);
	}

	if (port == NV_CIO_CRX__COLOR &&
	    index == NV_CIO_CRE_44 && data == NV_CIO_CRE_44_HEADB)
		bios->state.crtchead = 1;
}

static uint8_t
bios_port_rd(struct nvbios *bios, uint16_t port)
{
	uint8_t data, head = bios->state.crtchead;

	if (!valid_port(bios, port))
		return 0;

	data = NVReadPRMVIO(bios->dev, head, NV_PRMVIO0_OFFSET + port);

	BIOSLOG(bios, "	IO read:  Port: 0x%04X, Head: 0x%02X, Data: 0x%02X\n",
		port, head, data);

	return data;
}

static void
bios_port_wr(struct nvbios *bios, uint16_t port, uint8_t data)
{
	int head = bios->state.crtchead;

	if (!valid_port(bios, port))
		return;

	LOG_OLD_VALUE(bios_port_rd(bios, port));
	BIOSLOG(bios, "	IO write: Port: 0x%04X, Head: 0x%02X, Data: 0x%02X\n",
		port, head, data);

	if (!bios->execute)
		return;

	still_alive();
	NVWritePRMVIO(bios->dev, head, NV_PRMVIO0_OFFSET + port, data);
}

static bool
io_flag_condition_met(struct nvbios *bios, uint16_t offset, uint8_t cond)
{
	/*
	 * The IO flag condition entry has 2 bytes for the CRTC port; 1 byte
	 * for the CRTC index; 1 byte for the mask to apply to the value
	 * retrieved from the CRTC; 1 byte for the shift right to apply to the
	 * masked CRTC value; 2 bytes for the offset to the flag array, to
	 * which the shifted value is added; 1 byte for the mask applied to the
	 * value read from the flag array; and 1 byte for the value to compare
	 * against the masked byte from the flag table.
	 */

	uint16_t condptr = bios->io_flag_condition_tbl_ptr + cond * IO_FLAG_CONDITION_SIZE;
	uint16_t crtcport = ROM16(bios->data[condptr]);
	uint8_t crtcindex = bios->data[condptr + 2];
	uint8_t mask = bios->data[condptr + 3];
	uint8_t shift = bios->data[condptr + 4];
	uint16_t flagarray = ROM16(bios->data[condptr + 5]);
	uint8_t flagarraymask = bios->data[condptr + 7];
	uint8_t cmpval = bios->data[condptr + 8];
	uint8_t data;

	BIOSLOG(bios, "0x%04X: Port: 0x%04X, Index: 0x%02X, Mask: 0x%02X, "
		      "Shift: 0x%02X, FlagArray: 0x%04X, FAMask: 0x%02X, "
		      "Cmpval: 0x%02X\n",
		offset, crtcport, crtcindex, mask, shift, flagarray, flagarraymask, cmpval);

	data = bios_idxprt_rd(bios, crtcport, crtcindex);

	data = bios->data[flagarray + ((data & mask) >> shift)];
	data &= flagarraymask;

	BIOSLOG(bios, "0x%04X: Checking if 0x%02X equals 0x%02X\n",
		offset, data, cmpval);

	return (data == cmpval);
}

static bool
bios_condition_met(struct nvbios *bios, uint16_t offset, uint8_t cond)
{
	/*
	 * The condition table entry has 4 bytes for the address of the
	 * register to check, 4 bytes for a mask to apply to the register and
	 * 4 for a test comparison value
	 */

	uint16_t condptr = bios->condition_tbl_ptr + cond * CONDITION_SIZE;
	uint32_t reg = ROM32(bios->data[condptr]);
	uint32_t mask = ROM32(bios->data[condptr + 4]);
	uint32_t cmpval = ROM32(bios->data[condptr + 8]);
	uint32_t data;

	BIOSLOG(bios, "0x%04X: Cond: 0x%02X, Reg: 0x%08X, Mask: 0x%08X\n",
		offset, cond, reg, mask);

	data = bios_rd32(bios, reg) & mask;

	BIOSLOG(bios, "0x%04X: Checking if 0x%08X equals 0x%08X\n",
		offset, data, cmpval);

	return (data == cmpval);
}

static bool
io_condition_met(struct nvbios *bios, uint16_t offset, uint8_t cond)
{
	/*
	 * The IO condition entry has 2 bytes for the IO port address; 1 byte
	 * for the index to write to io_port; 1 byte for the mask to apply to
	 * the byte read from io_port+1; and 1 byte for the value to compare
	 * against the masked byte.
	 */

	uint16_t condptr = bios->io_condition_tbl_ptr + cond * IO_CONDITION_SIZE;
	uint16_t io_port = ROM16(bios->data[condptr]);
	uint8_t port_index = bios->data[condptr + 2];
	uint8_t mask = bios->data[condptr + 3];
	uint8_t cmpval = bios->data[condptr + 4];

	uint8_t data = bios_idxprt_rd(bios, io_port, port_index) & mask;

	BIOSLOG(bios, "0x%04X: Checking if 0x%02X equals 0x%02X\n",
		offset, data, cmpval);

	return (data == cmpval);
}

static int
nv50_pll_set(struct drm_device *dev, uint32_t reg, uint32_t clk)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t reg0 = nv_rd32(dev, reg + 0);
	uint32_t reg1 = nv_rd32(dev, reg + 4);
	struct nouveau_pll_vals pll;
	struct pll_lims pll_limits;
	int ret;

	ret = get_pll_limits(dev, reg, &pll_limits);
	if (ret)
		return ret;

	clk = nouveau_calc_pll_mnp(dev, &pll_limits, clk, &pll);
	if (!clk)
		return -ERANGE;

	reg0 = (reg0 & 0xfff8ffff) | (pll.log2P << 16);
	reg1 = (reg1 & 0xffff0000) | (pll.N1 << 8) | pll.M1;

	if (dev_priv->VBIOS.execute) {
		still_alive();
		nv_wr32(dev, reg + 4, reg1);
		nv_wr32(dev, reg + 0, reg0);
	}

	return 0;
}

static int
setPLL(struct nvbios *bios, uint32_t reg, uint32_t clk)
{
	struct drm_device *dev = bios->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	/* clk in kHz */
	struct pll_lims pll_lim;
	struct nouveau_pll_vals pllvals;
	int ret;

	if (dev_priv->card_type >= NV_50)
		return nv50_pll_set(dev, reg, clk);

	/* high regs (such as in the mac g5 table) are not -= 4 */
	ret = get_pll_limits(dev, reg > 0x405c ? reg : reg - 4, &pll_lim);
	if (ret)
		return ret;

	clk = nouveau_calc_pll_mnp(dev, &pll_lim, clk, &pllvals);
	if (!clk)
		return -ERANGE;

	if (bios->execute) {
		still_alive();
		nouveau_hw_setpll(dev, reg, &pllvals);
	}

	return 0;
}

static int dcb_entry_idx_from_crtchead(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;

	/*
	 * For the results of this function to be correct, CR44 must have been
	 * set (using bios_idxprt_wr to set crtchead), CR58 set for CR57 = 0,
	 * and the DCB table parsed, before the script calling the function is
	 * run.  run_digital_op_script is example of how to do such setup
	 */

	uint8_t dcb_entry = NVReadVgaCrtc5758(dev, bios->state.crtchead, 0);

	if (dcb_entry > bios->bdcb.dcb.entries) {
		NV_ERROR(dev, "CR58 doesn't have a valid DCB entry currently "
				"(%02X)\n", dcb_entry);
		dcb_entry = 0x7f;	/* unused / invalid marker */
	}

	return dcb_entry;
}

static struct nouveau_i2c_chan *
init_i2c_device_find(struct drm_device *dev, int i2c_index)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct bios_parsed_dcb *bdcb = &dev_priv->VBIOS.bdcb;

	if (i2c_index == 0xff) {
		/* note: dcb_entry_idx_from_crtchead needs pre-script set-up */
		int idx = dcb_entry_idx_from_crtchead(dev), shift = 0;
		int default_indices = bdcb->i2c_default_indices;

		if (idx != 0x7f && bdcb->dcb.entry[idx].i2c_upper_default)
			shift = 4;

		i2c_index = (default_indices >> shift) & 0xf;
	}
	if (i2c_index == 0x80)	/* g80+ */
		i2c_index = bdcb->i2c_default_indices & 0xf;

	return nouveau_i2c_find(dev, i2c_index);
}

static uint32_t get_tmds_index_reg(struct drm_device *dev, uint8_t mlv)
{
	/*
	 * For mlv < 0x80, it is an index into a table of TMDS base addresses.
	 * For mlv == 0x80 use the "or" value of the dcb_entry indexed by
	 * CR58 for CR57 = 0 to index a table of offsets to the basic
	 * 0x6808b0 address.
	 * For mlv == 0x81 use the "or" value of the dcb_entry indexed by
	 * CR58 for CR57 = 0 to index a table of offsets to the basic
	 * 0x6808b0 address, and then flip the offset by 8.
	 */

	struct drm_nouveau_private *dev_priv = dev->dev_private;
	const int pramdac_offset[13] = {
		0, 0, 0x8, 0, 0x2000, 0, 0, 0, 0x2008, 0, 0, 0, 0x2000 };
	const uint32_t pramdac_table[4] = {
		0x6808b0, 0x6808b8, 0x6828b0, 0x6828b8 };

	if (mlv >= 0x80) {
		int dcb_entry, dacoffset;

		/* note: dcb_entry_idx_from_crtchead needs pre-script set-up */
		dcb_entry = dcb_entry_idx_from_crtchead(dev);
		if (dcb_entry == 0x7f)
			return 0;
		dacoffset = pramdac_offset[
				dev_priv->VBIOS.bdcb.dcb.entry[dcb_entry].or];
		if (mlv == 0x81)
			dacoffset ^= 8;
		return 0x6808b0 + dacoffset;
	} else {
		if (mlv > ARRAY_SIZE(pramdac_table)) {
			NV_ERROR(dev, "Magic Lookup Value too big (%02X)\n",
									mlv);
			return 0;
		}
		return pramdac_table[mlv];
	}
}

static int
init_io_restrict_prog(struct nvbios *bios, uint16_t offset,
		      struct init_exec *iexec)
{
	/*
	 * INIT_IO_RESTRICT_PROG   opcode: 0x32 ('2')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (16 bit): CRTC port
	 * offset + 3  (8  bit): CRTC index
	 * offset + 4  (8  bit): mask
	 * offset + 5  (8  bit): shift
	 * offset + 6  (8  bit): count
	 * offset + 7  (32 bit): register
	 * offset + 11 (32 bit): configuration 1
	 * ...
	 *
	 * Starting at offset + 11 there are "count" 32 bit values.
	 * To find out which value to use read index "CRTC index" on "CRTC
	 * port", AND this value with "mask" and then bit shift right "shift"
	 * bits.  Read the appropriate value using this index and write to
	 * "register"
	 */

	uint16_t crtcport = ROM16(bios->data[offset + 1]);
	uint8_t crtcindex = bios->data[offset + 3];
	uint8_t mask = bios->data[offset + 4];
	uint8_t shift = bios->data[offset + 5];
	uint8_t count = bios->data[offset + 6];
	uint32_t reg = ROM32(bios->data[offset + 7]);
	uint8_t config;
	uint32_t configval;
	int len = 11 + count * 4;

	if (!iexec->execute)
		return len;

	BIOSLOG(bios, "0x%04X: Port: 0x%04X, Index: 0x%02X, Mask: 0x%02X, "
		      "Shift: 0x%02X, Count: 0x%02X, Reg: 0x%08X\n",
		offset, crtcport, crtcindex, mask, shift, count, reg);

	config = (bios_idxprt_rd(bios, crtcport, crtcindex) & mask) >> shift;
	if (config > count) {
		NV_ERROR(bios->dev,
			 "0x%04X: Config 0x%02X exceeds maximal bound 0x%02X\n",
			 offset, config, count);
		return 0;
	}

	configval = ROM32(bios->data[offset + 11 + config * 4]);

	BIOSLOG(bios, "0x%04X: Writing config %02X\n", offset, config);

	bios_wr32(bios, reg, configval);

	return len;
}

static int
init_repeat(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_REPEAT   opcode: 0x33 ('3')
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): count
	 *
	 * Execute script following this opcode up to INIT_REPEAT_END
	 * "count" times
	 */

	uint8_t count = bios->data[offset + 1];
	uint8_t i;

	/* no iexec->execute check by design */

	BIOSLOG(bios, "0x%04X: Repeating following segment %d times\n",
		offset, count);

	iexec->repeat = true;

	/*
	 * count - 1, as the script block will execute once when we leave this
	 * opcode -- this is compatible with bios behaviour as:
	 * a) the block is always executed at least once, even if count == 0
	 * b) the bios interpreter skips to the op following INIT_END_REPEAT,
	 * while we don't
	 */
	for (i = 0; i < count - 1; i++)
		parse_init_table(bios, offset + 2, iexec);

	iexec->repeat = false;

	return 2;
}

static int
init_io_restrict_pll(struct nvbios *bios, uint16_t offset,
		     struct init_exec *iexec)
{
	/*
	 * INIT_IO_RESTRICT_PLL   opcode: 0x34 ('4')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (16 bit): CRTC port
	 * offset + 3  (8  bit): CRTC index
	 * offset + 4  (8  bit): mask
	 * offset + 5  (8  bit): shift
	 * offset + 6  (8  bit): IO flag condition index
	 * offset + 7  (8  bit): count
	 * offset + 8  (32 bit): register
	 * offset + 12 (16 bit): frequency 1
	 * ...
	 *
	 * Starting at offset + 12 there are "count" 16 bit frequencies (10kHz).
	 * Set PLL register "register" to coefficients for frequency n,
	 * selected by reading index "CRTC index" of "CRTC port" ANDed with
	 * "mask" and shifted right by "shift".
	 *
	 * If "IO flag condition index" > 0, and condition met, double
	 * frequency before setting it.
	 */

	uint16_t crtcport = ROM16(bios->data[offset + 1]);
	uint8_t crtcindex = bios->data[offset + 3];
	uint8_t mask = bios->data[offset + 4];
	uint8_t shift = bios->data[offset + 5];
	int8_t io_flag_condition_idx = bios->data[offset + 6];
	uint8_t count = bios->data[offset + 7];
	uint32_t reg = ROM32(bios->data[offset + 8]);
	uint8_t config;
	uint16_t freq;
	int len = 12 + count * 2;

	if (!iexec->execute)
		return len;

	BIOSLOG(bios, "0x%04X: Port: 0x%04X, Index: 0x%02X, Mask: 0x%02X, "
		      "Shift: 0x%02X, IO Flag Condition: 0x%02X, "
		      "Count: 0x%02X, Reg: 0x%08X\n",
		offset, crtcport, crtcindex, mask, shift,
		io_flag_condition_idx, count, reg);

	config = (bios_idxprt_rd(bios, crtcport, crtcindex) & mask) >> shift;
	if (config > count) {
		NV_ERROR(bios->dev,
			 "0x%04X: Config 0x%02X exceeds maximal bound 0x%02X\n",
			 offset, config, count);
		return 0;
	}

	freq = ROM16(bios->data[offset + 12 + config * 2]);

	if (io_flag_condition_idx > 0) {
		if (io_flag_condition_met(bios, offset, io_flag_condition_idx)) {
			BIOSLOG(bios, "0x%04X: Condition fulfilled -- "
				      "frequency doubled\n", offset);
			freq *= 2;
		} else
			BIOSLOG(bios, "0x%04X: Condition not fulfilled -- "
				      "frequency unchanged\n", offset);
	}

	BIOSLOG(bios, "0x%04X: Reg: 0x%08X, Config: 0x%02X, Freq: %d0kHz\n",
		offset, reg, config, freq);

	setPLL(bios, reg, freq * 10);

	return len;
}

static int
init_end_repeat(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_END_REPEAT   opcode: 0x36 ('6')
	 *
	 * offset      (8 bit): opcode
	 *
	 * Marks the end of the block for INIT_REPEAT to repeat
	 */

	/* no iexec->execute check by design */

	/*
	 * iexec->repeat flag necessary to go past INIT_END_REPEAT opcode when
	 * we're not in repeat mode
	 */
	if (iexec->repeat)
		return 0;

	return 1;
}

static int
init_copy(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_COPY   opcode: 0x37 ('7')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): register
	 * offset + 5  (8  bit): shift
	 * offset + 6  (8  bit): srcmask
	 * offset + 7  (16 bit): CRTC port
	 * offset + 9  (8 bit): CRTC index
	 * offset + 10  (8 bit): mask
	 *
	 * Read index "CRTC index" on "CRTC port", AND with "mask", OR with
	 * (REGVAL("register") >> "shift" & "srcmask") and write-back to CRTC
	 * port
	 */

	uint32_t reg = ROM32(bios->data[offset + 1]);
	uint8_t shift = bios->data[offset + 5];
	uint8_t srcmask = bios->data[offset + 6];
	uint16_t crtcport = ROM16(bios->data[offset + 7]);
	uint8_t crtcindex = bios->data[offset + 9];
	uint8_t mask = bios->data[offset + 10];
	uint32_t data;
	uint8_t crtcdata;

	if (!iexec->execute)
		return 11;

	BIOSLOG(bios, "0x%04X: Reg: 0x%08X, Shift: 0x%02X, SrcMask: 0x%02X, "
		      "Port: 0x%04X, Index: 0x%02X, Mask: 0x%02X\n",
		offset, reg, shift, srcmask, crtcport, crtcindex, mask);

	data = bios_rd32(bios, reg);

	if (shift < 0x80)
		data >>= shift;
	else
		data <<= (0x100 - shift);

	data &= srcmask;

	crtcdata  = bios_idxprt_rd(bios, crtcport, crtcindex) & mask;
	crtcdata |= (uint8_t)data;
	bios_idxprt_wr(bios, crtcport, crtcindex, crtcdata);

	return 11;
}

static int
init_not(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_NOT   opcode: 0x38 ('8')
	 *
	 * offset      (8  bit): opcode
	 *
	 * Invert the current execute / no-execute condition (i.e. "else")
	 */
	if (iexec->execute)
		BIOSLOG(bios, "0x%04X: ------ Skipping following commands  ------\n", offset);
	else
		BIOSLOG(bios, "0x%04X: ------ Executing following commands ------\n", offset);

	iexec->execute = !iexec->execute;
	return 1;
}

static int
init_io_flag_condition(struct nvbios *bios, uint16_t offset,
		       struct init_exec *iexec)
{
	/*
	 * INIT_IO_FLAG_CONDITION   opcode: 0x39 ('9')
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): condition number
	 *
	 * Check condition "condition number" in the IO flag condition table.
	 * If condition not met skip subsequent opcodes until condition is
	 * inverted (INIT_NOT), or we hit INIT_RESUME
	 */

	uint8_t cond = bios->data[offset + 1];

	if (!iexec->execute)
		return 2;

	if (io_flag_condition_met(bios, offset, cond))
		BIOSLOG(bios, "0x%04X: Condition fulfilled -- continuing to execute\n", offset);
	else {
		BIOSLOG(bios, "0x%04X: Condition not fulfilled -- skipping following commands\n", offset);
		iexec->execute = false;
	}

	return 2;
}

static int
init_idx_addr_latched(struct nvbios *bios, uint16_t offset,
		      struct init_exec *iexec)
{
	/*
	 * INIT_INDEX_ADDRESS_LATCHED   opcode: 0x49 ('I')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): control register
	 * offset + 5  (32 bit): data register
	 * offset + 9  (32 bit): mask
	 * offset + 13 (32 bit): data
	 * offset + 17 (8  bit): count
	 * offset + 18 (8  bit): address 1
	 * offset + 19 (8  bit): data 1
	 * ...
	 *
	 * For each of "count" address and data pairs, write "data n" to
	 * "data register", read the current value of "control register",
	 * and write it back once ANDed with "mask", ORed with "data",
	 * and ORed with "address n"
	 */

	uint32_t controlreg = ROM32(bios->data[offset + 1]);
	uint32_t datareg = ROM32(bios->data[offset + 5]);
	uint32_t mask = ROM32(bios->data[offset + 9]);
	uint32_t data = ROM32(bios->data[offset + 13]);
	uint8_t count = bios->data[offset + 17];
	int len = 18 + count * 2;
	uint32_t value;
	int i;

	if (!iexec->execute)
		return len;

	BIOSLOG(bios, "0x%04X: ControlReg: 0x%08X, DataReg: 0x%08X, "
		      "Mask: 0x%08X, Data: 0x%08X, Count: 0x%02X\n",
		offset, controlreg, datareg, mask, data, count);

	for (i = 0; i < count; i++) {
		uint8_t instaddress = bios->data[offset + 18 + i * 2];
		uint8_t instdata = bios->data[offset + 19 + i * 2];

		BIOSLOG(bios, "0x%04X: Address: 0x%02X, Data: 0x%02X\n",
			offset, instaddress, instdata);

		bios_wr32(bios, datareg, instdata);
		value  = bios_rd32(bios, controlreg) & mask;
		value |= data;
		value |= instaddress;
		bios_wr32(bios, controlreg, value);
	}

	return len;
}

static int
init_io_restrict_pll2(struct nvbios *bios, uint16_t offset,
		      struct init_exec *iexec)
{
	/*
	 * INIT_IO_RESTRICT_PLL2   opcode: 0x4A ('J')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (16 bit): CRTC port
	 * offset + 3  (8  bit): CRTC index
	 * offset + 4  (8  bit): mask
	 * offset + 5  (8  bit): shift
	 * offset + 6  (8  bit): count
	 * offset + 7  (32 bit): register
	 * offset + 11 (32 bit): frequency 1
	 * ...
	 *
	 * Starting at offset + 11 there are "count" 32 bit frequencies (kHz).
	 * Set PLL register "register" to coefficients for frequency n,
	 * selected by reading index "CRTC index" of "CRTC port" ANDed with
	 * "mask" and shifted right by "shift".
	 */

	uint16_t crtcport = ROM16(bios->data[offset + 1]);
	uint8_t crtcindex = bios->data[offset + 3];
	uint8_t mask = bios->data[offset + 4];
	uint8_t shift = bios->data[offset + 5];
	uint8_t count = bios->data[offset + 6];
	uint32_t reg = ROM32(bios->data[offset + 7]);
	int len = 11 + count * 4;
	uint8_t config;
	uint32_t freq;

	if (!iexec->execute)
		return len;

	BIOSLOG(bios, "0x%04X: Port: 0x%04X, Index: 0x%02X, Mask: 0x%02X, "
		      "Shift: 0x%02X, Count: 0x%02X, Reg: 0x%08X\n",
		offset, crtcport, crtcindex, mask, shift, count, reg);

	if (!reg)
		return len;

	config = (bios_idxprt_rd(bios, crtcport, crtcindex) & mask) >> shift;
	if (config > count) {
		NV_ERROR(bios->dev,
			 "0x%04X: Config 0x%02X exceeds maximal bound 0x%02X\n",
			 offset, config, count);
		return 0;
	}

	freq = ROM32(bios->data[offset + 11 + config * 4]);

	BIOSLOG(bios, "0x%04X: Reg: 0x%08X, Config: 0x%02X, Freq: %dkHz\n",
		offset, reg, config, freq);

	setPLL(bios, reg, freq);

	return len;
}

static int
init_pll2(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_PLL2   opcode: 0x4B ('K')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): register
	 * offset + 5  (32 bit): freq
	 *
	 * Set PLL register "register" to coefficients for frequency "freq"
	 */

	uint32_t reg = ROM32(bios->data[offset + 1]);
	uint32_t freq = ROM32(bios->data[offset + 5]);

	if (!iexec->execute)
		return 9;

	BIOSLOG(bios, "0x%04X: Reg: 0x%04X, Freq: %dkHz\n",
		offset, reg, freq);

	setPLL(bios, reg, freq);
	return 9;
}

static int
init_i2c_byte(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_I2C_BYTE   opcode: 0x4C ('L')
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): DCB I2C table entry index
	 * offset + 2  (8 bit): I2C slave address
	 * offset + 3  (8 bit): count
	 * offset + 4  (8 bit): I2C register 1
	 * offset + 5  (8 bit): mask 1
	 * offset + 6  (8 bit): data 1
	 * ...
	 *
	 * For each of "count" registers given by "I2C register n" on the device
	 * addressed by "I2C slave address" on the I2C bus given by
	 * "DCB I2C table entry index", read the register, AND the result with
	 * "mask n" and OR it with "data n" before writing it back to the device
	 */

	uint8_t i2c_index = bios->data[offset + 1];
	uint8_t i2c_address = bios->data[offset + 2];
	uint8_t count = bios->data[offset + 3];
	int len = 4 + count * 3;
	struct nouveau_i2c_chan *chan;
	struct i2c_msg msg;
	int i;

	if (!iexec->execute)
		return len;

	BIOSLOG(bios, "0x%04X: DCBI2CIndex: 0x%02X, I2CAddress: 0x%02X, "
		      "Count: 0x%02X\n",
		offset, i2c_index, i2c_address, count);

	chan = init_i2c_device_find(bios->dev, i2c_index);
	if (!chan)
		return 0;

	for (i = 0; i < count; i++) {
		uint8_t i2c_reg = bios->data[offset + 4 + i * 3];
		uint8_t mask = bios->data[offset + 5 + i * 3];
		uint8_t data = bios->data[offset + 6 + i * 3];
		uint8_t value;

		msg.addr = i2c_address;
		msg.flags = I2C_M_RD;
		msg.len = 1;
		msg.buf = &value;
		if (i2c_transfer(&chan->adapter, &msg, 1) != 1)
			return 0;

		BIOSLOG(bios, "0x%04X: I2CReg: 0x%02X, Value: 0x%02X, "
			      "Mask: 0x%02X, Data: 0x%02X\n",
			offset, i2c_reg, value, mask, data);

		value = (value & mask) | data;

		if (bios->execute) {
			msg.addr = i2c_address;
			msg.flags = 0;
			msg.len = 1;
			msg.buf = &value;
			if (i2c_transfer(&chan->adapter, &msg, 1) != 1)
				return 0;
		}
	}

	return len;
}

static int
init_zm_i2c_byte(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_ZM_I2C_BYTE   opcode: 0x4D ('M')
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): DCB I2C table entry index
	 * offset + 2  (8 bit): I2C slave address
	 * offset + 3  (8 bit): count
	 * offset + 4  (8 bit): I2C register 1
	 * offset + 5  (8 bit): data 1
	 * ...
	 *
	 * For each of "count" registers given by "I2C register n" on the device
	 * addressed by "I2C slave address" on the I2C bus given by
	 * "DCB I2C table entry index", set the register to "data n"
	 */

	uint8_t i2c_index = bios->data[offset + 1];
	uint8_t i2c_address = bios->data[offset + 2];
	uint8_t count = bios->data[offset + 3];
	int len = 4 + count * 2;
	struct nouveau_i2c_chan *chan;
	struct i2c_msg msg;
	int i;

	if (!iexec->execute)
		return len;

	BIOSLOG(bios, "0x%04X: DCBI2CIndex: 0x%02X, I2CAddress: 0x%02X, "
		      "Count: 0x%02X\n",
		offset, i2c_index, i2c_address, count);

	chan = init_i2c_device_find(bios->dev, i2c_index);
	if (!chan)
		return 0;

	for (i = 0; i < count; i++) {
		uint8_t i2c_reg = bios->data[offset + 4 + i * 2];
		uint8_t data = bios->data[offset + 5 + i * 2];

		BIOSLOG(bios, "0x%04X: I2CReg: 0x%02X, Data: 0x%02X\n",
			offset, i2c_reg, data);

		if (bios->execute) {
			msg.addr = i2c_address;
			msg.flags = 0;
			msg.len = 1;
			msg.buf = &data;
			if (i2c_transfer(&chan->adapter, &msg, 1) != 1)
				return 0;
		}
	}

	return len;
}

static int
init_zm_i2c(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_ZM_I2C   opcode: 0x4E ('N')
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): DCB I2C table entry index
	 * offset + 2  (8 bit): I2C slave address
	 * offset + 3  (8 bit): count
	 * offset + 4  (8 bit): data 1
	 * ...
	 *
	 * Send "count" bytes ("data n") to the device addressed by "I2C slave
	 * address" on the I2C bus given by "DCB I2C table entry index"
	 */

	uint8_t i2c_index = bios->data[offset + 1];
	uint8_t i2c_address = bios->data[offset + 2];
	uint8_t count = bios->data[offset + 3];
	int len = 4 + count;
	struct nouveau_i2c_chan *chan;
	struct i2c_msg msg;
	uint8_t data[256];
	int i;

	if (!iexec->execute)
		return len;

	BIOSLOG(bios, "0x%04X: DCBI2CIndex: 0x%02X, I2CAddress: 0x%02X, "
		      "Count: 0x%02X\n",
		offset, i2c_index, i2c_address, count);

	chan = init_i2c_device_find(bios->dev, i2c_index);
	if (!chan)
		return 0;

	for (i = 0; i < count; i++) {
		data[i] = bios->data[offset + 4 + i];

		BIOSLOG(bios, "0x%04X: Data: 0x%02X\n", offset, data[i]);
	}

	if (bios->execute) {
		msg.addr = i2c_address;
		msg.flags = 0;
		msg.len = count;
		msg.buf = data;
		if (i2c_transfer(&chan->adapter, &msg, 1) != 1)
			return 0;
	}

	return len;
}

static int
init_tmds(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_TMDS   opcode: 0x4F ('O')	(non-canon name)
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): magic lookup value
	 * offset + 2  (8 bit): TMDS address
	 * offset + 3  (8 bit): mask
	 * offset + 4  (8 bit): data
	 *
	 * Read the data reg for TMDS address "TMDS address", AND it with mask
	 * and OR it with data, then write it back
	 * "magic lookup value" determines which TMDS base address register is
	 * used -- see get_tmds_index_reg()
	 */

	uint8_t mlv = bios->data[offset + 1];
	uint32_t tmdsaddr = bios->data[offset + 2];
	uint8_t mask = bios->data[offset + 3];
	uint8_t data = bios->data[offset + 4];
	uint32_t reg, value;

	if (!iexec->execute)
		return 5;

	BIOSLOG(bios, "0x%04X: MagicLookupValue: 0x%02X, TMDSAddr: 0x%02X, "
		      "Mask: 0x%02X, Data: 0x%02X\n",
		offset, mlv, tmdsaddr, mask, data);

	reg = get_tmds_index_reg(bios->dev, mlv);
	if (!reg)
		return 0;

	bios_wr32(bios, reg,
		  tmdsaddr | NV_PRAMDAC_FP_TMDS_CONTROL_WRITE_DISABLE);
	value = (bios_rd32(bios, reg + 4) & mask) | data;
	bios_wr32(bios, reg + 4, value);
	bios_wr32(bios, reg, tmdsaddr);

	return 5;
}

static int
init_zm_tmds_group(struct nvbios *bios, uint16_t offset,
		   struct init_exec *iexec)
{
	/*
	 * INIT_ZM_TMDS_GROUP   opcode: 0x50 ('P')	(non-canon name)
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): magic lookup value
	 * offset + 2  (8 bit): count
	 * offset + 3  (8 bit): addr 1
	 * offset + 4  (8 bit): data 1
	 * ...
	 *
	 * For each of "count" TMDS address and data pairs write "data n" to
	 * "addr n".  "magic lookup value" determines which TMDS base address
	 * register is used -- see get_tmds_index_reg()
	 */

	uint8_t mlv = bios->data[offset + 1];
	uint8_t count = bios->data[offset + 2];
	int len = 3 + count * 2;
	uint32_t reg;
	int i;

	if (!iexec->execute)
		return len;

	BIOSLOG(bios, "0x%04X: MagicLookupValue: 0x%02X, Count: 0x%02X\n",
		offset, mlv, count);

	reg = get_tmds_index_reg(bios->dev, mlv);
	if (!reg)
		return 0;

	for (i = 0; i < count; i++) {
		uint8_t tmdsaddr = bios->data[offset + 3 + i * 2];
		uint8_t tmdsdata = bios->data[offset + 4 + i * 2];

		bios_wr32(bios, reg + 4, tmdsdata);
		bios_wr32(bios, reg, tmdsaddr);
	}

	return len;
}

static int
init_cr_idx_adr_latch(struct nvbios *bios, uint16_t offset,
		      struct init_exec *iexec)
{
	/*
	 * INIT_CR_INDEX_ADDRESS_LATCHED   opcode: 0x51 ('Q')
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): CRTC index1
	 * offset + 2  (8 bit): CRTC index2
	 * offset + 3  (8 bit): baseaddr
	 * offset + 4  (8 bit): count
	 * offset + 5  (8 bit): data 1
	 * ...
	 *
	 * For each of "count" address and data pairs, write "baseaddr + n" to
	 * "CRTC index1" and "data n" to "CRTC index2"
	 * Once complete, restore initial value read from "CRTC index1"
	 */
	uint8_t crtcindex1 = bios->data[offset + 1];
	uint8_t crtcindex2 = bios->data[offset + 2];
	uint8_t baseaddr = bios->data[offset + 3];
	uint8_t count = bios->data[offset + 4];
	int len = 5 + count;
	uint8_t oldaddr, data;
	int i;

	if (!iexec->execute)
		return len;

	BIOSLOG(bios, "0x%04X: Index1: 0x%02X, Index2: 0x%02X, "
		      "BaseAddr: 0x%02X, Count: 0x%02X\n",
		offset, crtcindex1, crtcindex2, baseaddr, count);

	oldaddr = bios_idxprt_rd(bios, NV_CIO_CRX__COLOR, crtcindex1);

	for (i = 0; i < count; i++) {
		bios_idxprt_wr(bios, NV_CIO_CRX__COLOR, crtcindex1,
				     baseaddr + i);
		data = bios->data[offset + 5 + i];
		bios_idxprt_wr(bios, NV_CIO_CRX__COLOR, crtcindex2, data);
	}

	bios_idxprt_wr(bios, NV_CIO_CRX__COLOR, crtcindex1, oldaddr);

	return len;
}

static int
init_cr(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_CR   opcode: 0x52 ('R')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (8  bit): CRTC index
	 * offset + 2  (8  bit): mask
	 * offset + 3  (8  bit): data
	 *
	 * Assign the value of at "CRTC index" ANDed with mask and ORed with
	 * data back to "CRTC index"
	 */

	uint8_t crtcindex = bios->data[offset + 1];
	uint8_t mask = bios->data[offset + 2];
	uint8_t data = bios->data[offset + 3];
	uint8_t value;

	if (!iexec->execute)
		return 4;

	BIOSLOG(bios, "0x%04X: Index: 0x%02X, Mask: 0x%02X, Data: 0x%02X\n",
		offset, crtcindex, mask, data);

	value  = bios_idxprt_rd(bios, NV_CIO_CRX__COLOR, crtcindex) & mask;
	value |= data;
	bios_idxprt_wr(bios, NV_CIO_CRX__COLOR, crtcindex, value);

	return 4;
}

static int
init_zm_cr(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_ZM_CR   opcode: 0x53 ('S')
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): CRTC index
	 * offset + 2  (8 bit): value
	 *
	 * Assign "value" to CRTC register with index "CRTC index".
	 */

	uint8_t crtcindex = ROM32(bios->data[offset + 1]);
	uint8_t data = bios->data[offset + 2];

	if (!iexec->execute)
		return 3;

	bios_idxprt_wr(bios, NV_CIO_CRX__COLOR, crtcindex, data);

	return 3;
}

static int
init_zm_cr_group(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_ZM_CR_GROUP   opcode: 0x54 ('T')
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): count
	 * offset + 2  (8 bit): CRTC index 1
	 * offset + 3  (8 bit): value 1
	 * ...
	 *
	 * For "count", assign "value n" to CRTC register with index
	 * "CRTC index n".
	 */

	uint8_t count = bios->data[offset + 1];
	int len = 2 + count * 2;
	int i;

	if (!iexec->execute)
		return len;

	for (i = 0; i < count; i++)
		init_zm_cr(bios, offset + 2 + 2 * i - 1, iexec);

	return len;
}

static int
init_condition_time(struct nvbios *bios, uint16_t offset,
		    struct init_exec *iexec)
{
	/*
	 * INIT_CONDITION_TIME   opcode: 0x56 ('V')
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): condition number
	 * offset + 2  (8 bit): retries / 50
	 *
	 * Check condition "condition number" in the condition table.
	 * Bios code then sleeps for 2ms if the condition is not met, and
	 * repeats up to "retries" times, but on one C51 this has proved
	 * insufficient.  In mmiotraces the driver sleeps for 20ms, so we do
	 * this, and bail after "retries" times, or 2s, whichever is less.
	 * If still not met after retries, clear execution flag for this table.
	 */

	uint8_t cond = bios->data[offset + 1];
	uint16_t retries = bios->data[offset + 2] * 50;
	unsigned cnt;

	if (!iexec->execute)
		return 3;

	if (retries > 100)
		retries = 100;

	BIOSLOG(bios, "0x%04X: Condition: 0x%02X, Retries: 0x%02X\n",
		offset, cond, retries);

	if (!bios->execute) /* avoid 2s delays when "faking" execution */
		retries = 1;

	for (cnt = 0; cnt < retries; cnt++) {
		if (bios_condition_met(bios, offset, cond)) {
			BIOSLOG(bios, "0x%04X: Condition met, continuing\n",
								offset);
			break;
		} else {
			BIOSLOG(bios, "0x%04X: "
				"Condition not met, sleeping for 20ms\n",
								offset);
			msleep(20);
		}
	}

	if (!bios_condition_met(bios, offset, cond)) {
		NV_WARN(bios->dev,
			"0x%04X: Condition still not met after %dms, "
			"skipping following opcodes\n", offset, 20 * retries);
		iexec->execute = false;
	}

	return 3;
}

static int
init_zm_reg_sequence(struct nvbios *bios, uint16_t offset,
		     struct init_exec *iexec)
{
	/*
	 * INIT_ZM_REG_SEQUENCE   opcode: 0x58 ('X')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): base register
	 * offset + 5  (8  bit): count
	 * offset + 6  (32 bit): value 1
	 * ...
	 *
	 * Starting at offset + 6 there are "count" 32 bit values.
	 * For "count" iterations set "base register" + 4 * current_iteration
	 * to "value current_iteration"
	 */

	uint32_t basereg = ROM32(bios->data[offset + 1]);
	uint32_t count = bios->data[offset + 5];
	int len = 6 + count * 4;
	int i;

	if (!iexec->execute)
		return len;

	BIOSLOG(bios, "0x%04X: BaseReg: 0x%08X, Count: 0x%02X\n",
		offset, basereg, count);

	for (i = 0; i < count; i++) {
		uint32_t reg = basereg + i * 4;
		uint32_t data = ROM32(bios->data[offset + 6 + i * 4]);

		bios_wr32(bios, reg, data);
	}

	return len;
}

static int
init_sub_direct(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_SUB_DIRECT   opcode: 0x5B ('[')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (16 bit): subroutine offset (in bios)
	 *
	 * Calls a subroutine that will execute commands until INIT_DONE
	 * is found.
	 */

	uint16_t sub_offset = ROM16(bios->data[offset + 1]);

	if (!iexec->execute)
		return 3;

	BIOSLOG(bios, "0x%04X: Executing subroutine at 0x%04X\n",
		offset, sub_offset);

	parse_init_table(bios, sub_offset, iexec);

	BIOSLOG(bios, "0x%04X: End of 0x%04X subroutine\n", offset, sub_offset);

	return 3;
}

static int
init_copy_nv_reg(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_COPY_NV_REG   opcode: 0x5F ('_')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): src reg
	 * offset + 5  (8  bit): shift
	 * offset + 6  (32 bit): src mask
	 * offset + 10 (32 bit): xor
	 * offset + 14 (32 bit): dst reg
	 * offset + 18 (32 bit): dst mask
	 *
	 * Shift REGVAL("src reg") right by (signed) "shift", AND result with
	 * "src mask", then XOR with "xor". Write this OR'd with
	 * (REGVAL("dst reg") AND'd with "dst mask") to "dst reg"
	 */

	uint32_t srcreg = *((uint32_t *)(&bios->data[offset + 1]));
	uint8_t shift = bios->data[offset + 5];
	uint32_t srcmask = *((uint32_t *)(&bios->data[offset + 6]));
	uint32_t xor = *((uint32_t *)(&bios->data[offset + 10]));
	uint32_t dstreg = *((uint32_t *)(&bios->data[offset + 14]));
	uint32_t dstmask = *((uint32_t *)(&bios->data[offset + 18]));
	uint32_t srcvalue, dstvalue;

	if (!iexec->execute)
		return 22;

	BIOSLOG(bios, "0x%04X: SrcReg: 0x%08X, Shift: 0x%02X, SrcMask: 0x%08X, "
		      "Xor: 0x%08X, DstReg: 0x%08X, DstMask: 0x%08X\n",
		offset, srcreg, shift, srcmask, xor, dstreg, dstmask);

	srcvalue = bios_rd32(bios, srcreg);

	if (shift < 0x80)
		srcvalue >>= shift;
	else
		srcvalue <<= (0x100 - shift);

	srcvalue = (srcvalue & srcmask) ^ xor;

	dstvalue = bios_rd32(bios, dstreg) & dstmask;

	bios_wr32(bios, dstreg, dstvalue | srcvalue);

	return 22;
}

static int
init_zm_index_io(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_ZM_INDEX_IO   opcode: 0x62 ('b')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (16 bit): CRTC port
	 * offset + 3  (8  bit): CRTC index
	 * offset + 4  (8  bit): data
	 *
	 * Write "data" to index "CRTC index" of "CRTC port"
	 */
	uint16_t crtcport = ROM16(bios->data[offset + 1]);
	uint8_t crtcindex = bios->data[offset + 3];
	uint8_t data = bios->data[offset + 4];

	if (!iexec->execute)
		return 5;

	bios_idxprt_wr(bios, crtcport, crtcindex, data);

	return 5;
}

static int
init_compute_mem(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_COMPUTE_MEM   opcode: 0x63 ('c')
	 *
	 * offset      (8 bit): opcode
	 *
	 * This opcode is meant to set NV_PFB_CFG0 (0x100200) appropriately so
	 * that the hardware can correctly calculate how much VRAM it has
	 * (and subsequently report that value in NV_PFB_CSTATUS (0x10020C))
	 *
	 * The implementation of this opcode in general consists of two parts:
	 * 1) determination of the memory bus width
	 * 2) determination of how many of the card's RAM pads have ICs attached
	 *
	 * 1) is done by a cunning combination of writes to offsets 0x1c and
	 * 0x3c in the framebuffer, and seeing whether the written values are
	 * read back correctly. This then affects bits 4-7 of NV_PFB_CFG0
	 *
	 * 2) is done by a cunning combination of writes to an offset slightly
	 * less than the maximum memory reported by NV_PFB_CSTATUS, then seeing
	 * if the test pattern can be read back. This then affects bits 12-15 of
	 * NV_PFB_CFG0
	 *
	 * In this context a "cunning combination" may include multiple reads
	 * and writes to varying locations, often alternating the test pattern
	 * and 0, doubtless to make sure buffers are filled, residual charges
	 * on tracks are removed etc.
	 *
	 * Unfortunately, the "cunning combination"s mentioned above, and the
	 * changes to the bits in NV_PFB_CFG0 differ with nearly every bios
	 * trace I have.
	 *
	 * Therefore, we cheat and assume the value of NV_PFB_CFG0 with which
	 * we started was correct, and use that instead
	 */

	/* no iexec->execute check by design */

	/*
	 * This appears to be a NOP on G8x chipsets, both io logs of the VBIOS
	 * and kmmio traces of the binary driver POSTing the card show nothing
	 * being done for this opcode.  why is it still listed in the table?!
	 */

	struct drm_nouveau_private *dev_priv = bios->dev->dev_private;

	if (dev_priv->card_type >= NV_50)
		return 1;

	/*
	 * On every card I've seen, this step gets done for us earlier in
	 * the init scripts
	uint8_t crdata = bios_idxprt_rd(dev, NV_VIO_SRX, 0x01);
	bios_idxprt_wr(dev, NV_VIO_SRX, 0x01, crdata | 0x20);
	 */

	/*
	 * This also has probably been done in the scripts, but an mmio trace of
	 * s3 resume shows nvidia doing it anyway (unlike the NV_VIO_SRX write)
	 */
	bios_wr32(bios, NV_PFB_REFCTRL, NV_PFB_REFCTRL_VALID_1);

	/* write back the saved configuration value */
	bios_wr32(bios, NV_PFB_CFG0, bios->state.saved_nv_pfb_cfg0);

	return 1;
}

static int
init_reset(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_RESET   opcode: 0x65 ('e')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): register
	 * offset + 5  (32 bit): value1
	 * offset + 9  (32 bit): value2
	 *
	 * Assign "value1" to "register", then assign "value2" to "register"
	 */

	uint32_t reg = ROM32(bios->data[offset + 1]);
	uint32_t value1 = ROM32(bios->data[offset + 5]);
	uint32_t value2 = ROM32(bios->data[offset + 9]);
	uint32_t pci_nv_19, pci_nv_20;

	/* no iexec->execute check by design */

	pci_nv_19 = bios_rd32(bios, NV_PBUS_PCI_NV_19);
	bios_wr32(bios, NV_PBUS_PCI_NV_19, 0);
	bios_wr32(bios, reg, value1);

	udelay(10);

	bios_wr32(bios, reg, value2);
	bios_wr32(bios, NV_PBUS_PCI_NV_19, pci_nv_19);

	pci_nv_20 = bios_rd32(bios, NV_PBUS_PCI_NV_20);
	pci_nv_20 &= ~NV_PBUS_PCI_NV_20_ROM_SHADOW_ENABLED;	/* 0xfffffffe */
	bios_wr32(bios, NV_PBUS_PCI_NV_20, pci_nv_20);

	return 13;
}

static int
init_configure_mem(struct nvbios *bios, uint16_t offset,
		   struct init_exec *iexec)
{
	/*
	 * INIT_CONFIGURE_MEM   opcode: 0x66 ('f')
	 *
	 * offset      (8 bit): opcode
	 *
	 * Equivalent to INIT_DONE on bios version 3 or greater.
	 * For early bios versions, sets up the memory registers, using values
	 * taken from the memory init table
	 */

	/* no iexec->execute check by design */

	uint16_t meminitoffs = bios->legacy.mem_init_tbl_ptr + MEM_INIT_SIZE * (bios_idxprt_rd(bios, NV_CIO_CRX__COLOR, NV_CIO_CRE_SCRATCH4__INDEX) >> 4);
	uint16_t seqtbloffs = bios->legacy.sdr_seq_tbl_ptr, meminitdata = meminitoffs + 6;
	uint32_t reg, data;

	if (bios->major_version > 2)
		return 0;

	bios_idxprt_wr(bios, NV_VIO_SRX, NV_VIO_SR_CLOCK_INDEX, bios_idxprt_rd(
		       bios, NV_VIO_SRX, NV_VIO_SR_CLOCK_INDEX) | 0x20);

	if (bios->data[meminitoffs] & 1)
		seqtbloffs = bios->legacy.ddr_seq_tbl_ptr;

	for (reg = ROM32(bios->data[seqtbloffs]);
	     reg != 0xffffffff;
	     reg = ROM32(bios->data[seqtbloffs += 4])) {

		switch (reg) {
		case NV_PFB_PRE:
			data = NV_PFB_PRE_CMD_PRECHARGE;
			break;
		case NV_PFB_PAD:
			data = NV_PFB_PAD_CKE_NORMAL;
			break;
		case NV_PFB_REF:
			data = NV_PFB_REF_CMD_REFRESH;
			break;
		default:
			data = ROM32(bios->data[meminitdata]);
			meminitdata += 4;
			if (data == 0xffffffff)
				continue;
		}

		bios_wr32(bios, reg, data);
	}

	return 1;
}

static int
init_configure_clk(struct nvbios *bios, uint16_t offset,
		   struct init_exec *iexec)
{
	/*
	 * INIT_CONFIGURE_CLK   opcode: 0x67 ('g')
	 *
	 * offset      (8 bit): opcode
	 *
	 * Equivalent to INIT_DONE on bios version 3 or greater.
	 * For early bios versions, sets up the NVClk and MClk PLLs, using
	 * values taken from the memory init table
	 */

	/* no iexec->execute check by design */

	uint16_t meminitoffs = bios->legacy.mem_init_tbl_ptr + MEM_INIT_SIZE * (bios_idxprt_rd(bios, NV_CIO_CRX__COLOR, NV_CIO_CRE_SCRATCH4__INDEX) >> 4);
	int clock;

	if (bios->major_version > 2)
		return 0;

	clock = ROM16(bios->data[meminitoffs + 4]) * 10;
	setPLL(bios, NV_PRAMDAC_NVPLL_COEFF, clock);

	clock = ROM16(bios->data[meminitoffs + 2]) * 10;
	if (bios->data[meminitoffs] & 1) /* DDR */
		clock *= 2;
	setPLL(bios, NV_PRAMDAC_MPLL_COEFF, clock);

	return 1;
}

static int
init_configure_preinit(struct nvbios *bios, uint16_t offset,
		       struct init_exec *iexec)
{
	/*
	 * INIT_CONFIGURE_PREINIT   opcode: 0x68 ('h')
	 *
	 * offset      (8 bit): opcode
	 *
	 * Equivalent to INIT_DONE on bios version 3 or greater.
	 * For early bios versions, does early init, loading ram and crystal
	 * configuration from straps into CR3C
	 */

	/* no iexec->execute check by design */

	uint32_t straps = bios_rd32(bios, NV_PEXTDEV_BOOT_0);
	uint8_t cr3c = ((straps << 2) & 0xf0) | (straps & (1 << 6));

	if (bios->major_version > 2)
		return 0;

	bios_idxprt_wr(bios, NV_CIO_CRX__COLOR,
			     NV_CIO_CRE_SCRATCH4__INDEX, cr3c);

	return 1;
}

static int
init_io(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_IO   opcode: 0x69 ('i')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (16 bit): CRTC port
	 * offset + 3  (8  bit): mask
	 * offset + 4  (8  bit): data
	 *
	 * Assign ((IOVAL("crtc port") & "mask") | "data") to "crtc port"
	 */

	struct drm_nouveau_private *dev_priv = bios->dev->dev_private;
	uint16_t crtcport = ROM16(bios->data[offset + 1]);
	uint8_t mask = bios->data[offset + 3];
	uint8_t data = bios->data[offset + 4];

	if (!iexec->execute)
		return 5;

	BIOSLOG(bios, "0x%04X: Port: 0x%04X, Mask: 0x%02X, Data: 0x%02X\n",
		offset, crtcport, mask, data);

	/*
	 * I have no idea what this does, but NVIDIA do this magic sequence
	 * in the places where this INIT_IO happens..
	 */
	if (dev_priv->card_type >= NV_50 && crtcport == 0x3c3 && data == 1) {
		int i;

		bios_wr32(bios, 0x614100, (bios_rd32(
			  bios, 0x614100) & 0x0fffffff) | 0x00800000);

		bios_wr32(bios, 0x00e18c, bios_rd32(
			  bios, 0x00e18c) | 0x00020000);

		bios_wr32(bios, 0x614900, (bios_rd32(
			  bios, 0x614900) & 0x0fffffff) | 0x00800000);

		bios_wr32(bios, 0x000200, bios_rd32(
			  bios, 0x000200) & ~0x40000000);

		mdelay(10);

		bios_wr32(bios, 0x00e18c, bios_rd32(
			  bios, 0x00e18c) & ~0x00020000);

		bios_wr32(bios, 0x000200, bios_rd32(
			  bios, 0x000200) | 0x40000000);

		bios_wr32(bios, 0x614100, 0x00800018);
		bios_wr32(bios, 0x614900, 0x00800018);

		mdelay(10);

		bios_wr32(bios, 0x614100, 0x10000018);
		bios_wr32(bios, 0x614900, 0x10000018);

		for (i = 0; i < 3; i++)
			bios_wr32(bios, 0x614280 + (i*0x800), bios_rd32(
				  bios, 0x614280 + (i*0x800)) & 0xf0f0f0f0);

		for (i = 0; i < 2; i++)
			bios_wr32(bios, 0x614300 + (i*0x800), bios_rd32(
				  bios, 0x614300 + (i*0x800)) & 0xfffff0f0);

		for (i = 0; i < 3; i++)
			bios_wr32(bios, 0x614380 + (i*0x800), bios_rd32(
				  bios, 0x614380 + (i*0x800)) & 0xfffff0f0);

		for (i = 0; i < 2; i++)
			bios_wr32(bios, 0x614200 + (i*0x800), bios_rd32(
				  bios, 0x614200 + (i*0x800)) & 0xfffffff0);

		for (i = 0; i < 2; i++)
			bios_wr32(bios, 0x614108 + (i*0x800), bios_rd32(
				  bios, 0x614108 + (i*0x800)) & 0x0fffffff);
		return 5;
	}

	bios_port_wr(bios, crtcport, (bios_port_rd(bios, crtcport) & mask) |
									data);
	return 5;
}

static int
init_sub(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_SUB   opcode: 0x6B ('k')
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): script number
	 *
	 * Execute script number "script number", as a subroutine
	 */

	uint8_t sub = bios->data[offset + 1];

	if (!iexec->execute)
		return 2;

	BIOSLOG(bios, "0x%04X: Calling script %d\n", offset, sub);

	parse_init_table(bios,
			 ROM16(bios->data[bios->init_script_tbls_ptr + sub * 2]),
			 iexec);

	BIOSLOG(bios, "0x%04X: End of script %d\n", offset, sub);

	return 2;
}

static int
init_ram_condition(struct nvbios *bios, uint16_t offset,
		   struct init_exec *iexec)
{
	/*
	 * INIT_RAM_CONDITION   opcode: 0x6D ('m')
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): mask
	 * offset + 2  (8 bit): cmpval
	 *
	 * Test if (NV_PFB_BOOT_0 & "mask") equals "cmpval".
	 * If condition not met skip subsequent opcodes until condition is
	 * inverted (INIT_NOT), or we hit INIT_RESUME
	 */

	uint8_t mask = bios->data[offset + 1];
	uint8_t cmpval = bios->data[offset + 2];
	uint8_t data;

	if (!iexec->execute)
		return 3;

	data = bios_rd32(bios, NV_PFB_BOOT_0) & mask;

	BIOSLOG(bios, "0x%04X: Checking if 0x%08X equals 0x%08X\n",
		offset, data, cmpval);

	if (data == cmpval)
		BIOSLOG(bios, "0x%04X: Condition fulfilled -- continuing to execute\n", offset);
	else {
		BIOSLOG(bios, "0x%04X: Condition not fulfilled -- skipping following commands\n", offset);
		iexec->execute = false;
	}

	return 3;
}

static int
init_nv_reg(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_NV_REG   opcode: 0x6E ('n')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): register
	 * offset + 5  (32 bit): mask
	 * offset + 9  (32 bit): data
	 *
	 * Assign ((REGVAL("register") & "mask") | "data") to "register"
	 */

	uint32_t reg = ROM32(bios->data[offset + 1]);
	uint32_t mask = ROM32(bios->data[offset + 5]);
	uint32_t data = ROM32(bios->data[offset + 9]);

	if (!iexec->execute)
		return 13;

	BIOSLOG(bios, "0x%04X: Reg: 0x%08X, Mask: 0x%08X, Data: 0x%08X\n",
		offset, reg, mask, data);

	bios_wr32(bios, reg, (bios_rd32(bios, reg) & mask) | data);

	return 13;
}

static int
init_macro(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_MACRO   opcode: 0x6F ('o')
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): macro number
	 *
	 * Look up macro index "macro number" in the macro index table.
	 * The macro index table entry has 1 byte for the index in the macro
	 * table, and 1 byte for the number of times to repeat the macro.
	 * The macro table entry has 4 bytes for the register address and
	 * 4 bytes for the value to write to that register
	 */

	uint8_t macro_index_tbl_idx = bios->data[offset + 1];
	uint16_t tmp = bios->macro_index_tbl_ptr + (macro_index_tbl_idx * MACRO_INDEX_SIZE);
	uint8_t macro_tbl_idx = bios->data[tmp];
	uint8_t count = bios->data[tmp + 1];
	uint32_t reg, data;
	int i;

	if (!iexec->execute)
		return 2;

	BIOSLOG(bios, "0x%04X: Macro: 0x%02X, MacroTableIndex: 0x%02X, "
		      "Count: 0x%02X\n",
		offset, macro_index_tbl_idx, macro_tbl_idx, count);

	for (i = 0; i < count; i++) {
		uint16_t macroentryptr = bios->macro_tbl_ptr + (macro_tbl_idx + i) * MACRO_SIZE;

		reg = ROM32(bios->data[macroentryptr]);
		data = ROM32(bios->data[macroentryptr + 4]);

		bios_wr32(bios, reg, data);
	}

	return 2;
}

static int
init_done(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_DONE   opcode: 0x71 ('q')
	 *
	 * offset      (8  bit): opcode
	 *
	 * End the current script
	 */

	/* mild retval abuse to stop parsing this table */
	return 0;
}

static int
init_resume(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_RESUME   opcode: 0x72 ('r')
	 *
	 * offset      (8  bit): opcode
	 *
	 * End the current execute / no-execute condition
	 */

	if (iexec->execute)
		return 1;

	iexec->execute = true;
	BIOSLOG(bios, "0x%04X: ---- Executing following commands ----\n", offset);

	return 1;
}

static int
init_time(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_TIME   opcode: 0x74 ('t')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (16 bit): time
	 *
	 * Sleep for "time" microseconds.
	 */

	unsigned time = ROM16(bios->data[offset + 1]);

	if (!iexec->execute)
		return 3;

	BIOSLOG(bios, "0x%04X: Sleeping for 0x%04X microseconds\n",
		offset, time);

	if (time < 1000)
		udelay(time);
	else
		msleep((time + 900) / 1000);

	return 3;
}

static int
init_condition(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_CONDITION   opcode: 0x75 ('u')
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): condition number
	 *
	 * Check condition "condition number" in the condition table.
	 * If condition not met skip subsequent opcodes until condition is
	 * inverted (INIT_NOT), or we hit INIT_RESUME
	 */

	uint8_t cond = bios->data[offset + 1];

	if (!iexec->execute)
		return 2;

	BIOSLOG(bios, "0x%04X: Condition: 0x%02X\n", offset, cond);

	if (bios_condition_met(bios, offset, cond))
		BIOSLOG(bios, "0x%04X: Condition fulfilled -- continuing to execute\n", offset);
	else {
		BIOSLOG(bios, "0x%04X: Condition not fulfilled -- skipping following commands\n", offset);
		iexec->execute = false;
	}

	return 2;
}

static int
init_io_condition(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_IO_CONDITION  opcode: 0x76
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): condition number
	 *
	 * Check condition "condition number" in the io condition table.
	 * If condition not met skip subsequent opcodes until condition is
	 * inverted (INIT_NOT), or we hit INIT_RESUME
	 */

	uint8_t cond = bios->data[offset + 1];

	if (!iexec->execute)
		return 2;

	BIOSLOG(bios, "0x%04X: IO condition: 0x%02X\n", offset, cond);

	if (io_condition_met(bios, offset, cond))
		BIOSLOG(bios, "0x%04X: Condition fulfilled -- continuing to execute\n", offset);
	else {
		BIOSLOG(bios, "0x%04X: Condition not fulfilled -- skipping following commands\n", offset);
		iexec->execute = false;
	}

	return 2;
}

static int
init_index_io(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_INDEX_IO   opcode: 0x78 ('x')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (16 bit): CRTC port
	 * offset + 3  (8  bit): CRTC index
	 * offset + 4  (8  bit): mask
	 * offset + 5  (8  bit): data
	 *
	 * Read value at index "CRTC index" on "CRTC port", AND with "mask",
	 * OR with "data", write-back
	 */

	uint16_t crtcport = ROM16(bios->data[offset + 1]);
	uint8_t crtcindex = bios->data[offset + 3];
	uint8_t mask = bios->data[offset + 4];
	uint8_t data = bios->data[offset + 5];
	uint8_t value;

	if (!iexec->execute)
		return 6;

	BIOSLOG(bios, "0x%04X: Port: 0x%04X, Index: 0x%02X, Mask: 0x%02X, "
		      "Data: 0x%02X\n",
		offset, crtcport, crtcindex, mask, data);

	value = (bios_idxprt_rd(bios, crtcport, crtcindex) & mask) | data;
	bios_idxprt_wr(bios, crtcport, crtcindex, value);

	return 6;
}

static int
init_pll(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_PLL   opcode: 0x79 ('y')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): register
	 * offset + 5  (16 bit): freq
	 *
	 * Set PLL register "register" to coefficients for frequency (10kHz)
	 * "freq"
	 */

	uint32_t reg = ROM32(bios->data[offset + 1]);
	uint16_t freq = ROM16(bios->data[offset + 5]);

	if (!iexec->execute)
		return 7;

	BIOSLOG(bios, "0x%04X: Reg: 0x%08X, Freq: %d0kHz\n", offset, reg, freq);

	setPLL(bios, reg, freq * 10);

	return 7;
}

static int
init_zm_reg(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_ZM_REG   opcode: 0x7A ('z')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): register
	 * offset + 5  (32 bit): value
	 *
	 * Assign "value" to "register"
	 */

	uint32_t reg = ROM32(bios->data[offset + 1]);
	uint32_t value = ROM32(bios->data[offset + 5]);

	if (!iexec->execute)
		return 9;

	if (reg == 0x000200)
		value |= 1;

	bios_wr32(bios, reg, value);

	return 9;
}

static int
init_ram_restrict_pll(struct nvbios *bios, uint16_t offset,
		      struct init_exec *iexec)
{
	/*
	 * INIT_RAM_RESTRICT_PLL   opcode: 0x87 ('')
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): PLL type
	 * offset + 2 (32 bit): frequency 0
	 *
	 * Uses the RAMCFG strap of PEXTDEV_BOOT as an index into the table at
	 * ram_restrict_table_ptr.  The value read from there is used to select
	 * a frequency from the table starting at 'frequency 0' to be
	 * programmed into the PLL corresponding to 'type'.
	 *
	 * The PLL limits table on cards using this opcode has a mapping of
	 * 'type' to the relevant registers.
	 */

	struct drm_device *dev = bios->dev;
	uint32_t strap = (bios_rd32(bios, NV_PEXTDEV_BOOT_0) & 0x0000003c) >> 2;
	uint8_t index = bios->data[bios->ram_restrict_tbl_ptr + strap];
	uint8_t type = bios->data[offset + 1];
	uint32_t freq = ROM32(bios->data[offset + 2 + (index * 4)]);
	uint8_t *pll_limits = &bios->data[bios->pll_limit_tbl_ptr], *entry;
	int len = 2 + bios->ram_restrict_group_count * 4;
	int i;

	if (!iexec->execute)
		return len;

	if (!bios->pll_limit_tbl_ptr || (pll_limits[0] & 0xf0) != 0x30) {
		NV_ERROR(dev, "PLL limits table not version 3.x\n");
		return len; /* deliberate, allow default clocks to remain */
	}

	entry = pll_limits + pll_limits[1];
	for (i = 0; i < pll_limits[3]; i++, entry += pll_limits[2]) {
		if (entry[0] == type) {
			uint32_t reg = ROM32(entry[3]);

			BIOSLOG(bios, "0x%04X: "
				      "Type %02x Reg 0x%08x Freq %dKHz\n",
				offset, type, reg, freq);

			setPLL(bios, reg, freq);
			return len;
		}
	}

	NV_ERROR(dev, "PLL type 0x%02x not found in PLL limits table", type);
	return len;
}

static int
init_8c(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_8C   opcode: 0x8C ('')
	 *
	 * NOP so far....
	 *
	 */

	return 1;
}

static int
init_8d(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_8D   opcode: 0x8D ('')
	 *
	 * NOP so far....
	 *
	 */

	return 1;
}

static int
init_gpio(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_GPIO   opcode: 0x8E ('')
	 *
	 * offset      (8 bit): opcode
	 *
	 * Loop over all entries in the DCB GPIO table, and initialise
	 * each GPIO according to various values listed in each entry
	 */

	const uint32_t nv50_gpio_reg[4] = { 0xe104, 0xe108, 0xe280, 0xe284 };
	const uint32_t nv50_gpio_ctl[2] = { 0xe100, 0xe28c };
	const uint8_t *gpio_table = &bios->data[bios->bdcb.gpio_table_ptr];
	const uint8_t *gpio_entry;
	int i;

	if (!iexec->execute)
		return 1;

	if (bios->bdcb.version != 0x40) {
		NV_ERROR(bios->dev, "DCB table not version 4.0\n");
		return 0;
	}

	if (!bios->bdcb.gpio_table_ptr) {
		NV_WARN(bios->dev, "Invalid pointer to INIT_8E table\n");
		return 0;
	}

	gpio_entry = gpio_table + gpio_table[1];
	for (i = 0; i < gpio_table[2]; i++, gpio_entry += gpio_table[3]) {
		uint32_t entry = ROM32(gpio_entry[0]), r, s, v;
		int line = (entry & 0x0000001f);

		BIOSLOG(bios, "0x%04X: Entry: 0x%08X\n", offset, entry);

		if ((entry & 0x0000ff00) == 0x0000ff00)
			continue;

		r = nv50_gpio_reg[line >> 3];
		s = (line & 0x07) << 2;
		v = bios_rd32(bios, r) & ~(0x00000003 << s);
		if (entry & 0x01000000)
			v |= (((entry & 0x60000000) >> 29) ^ 2) << s;
		else
			v |= (((entry & 0x18000000) >> 27) ^ 2) << s;
		bios_wr32(bios, r, v);

		r = nv50_gpio_ctl[line >> 4];
		s = (line & 0x0f);
		v = bios_rd32(bios, r) & ~(0x00010001 << s);
		switch ((entry & 0x06000000) >> 25) {
		case 1:
			v |= (0x00000001 << s);
			break;
		case 2:
			v |= (0x00010000 << s);
			break;
		default:
			break;
		}
		bios_wr32(bios, r, v);
	}

	return 1;
}

static int
init_ram_restrict_zm_reg_group(struct nvbios *bios, uint16_t offset,
			       struct init_exec *iexec)
{
	/*
	 * INIT_RAM_RESTRICT_ZM_REG_GROUP   opcode: 0x8F ('')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): reg
	 * offset + 5  (8  bit): regincrement
	 * offset + 6  (8  bit): count
	 * offset + 7  (32 bit): value 1,1
	 * ...
	 *
	 * Use the RAMCFG strap of PEXTDEV_BOOT as an index into the table at
	 * ram_restrict_table_ptr. The value read from here is 'n', and
	 * "value 1,n" gets written to "reg". This repeats "count" times and on
	 * each iteration 'm', "reg" increases by "regincrement" and
	 * "value m,n" is used. The extent of n is limited by a number read
	 * from the 'M' BIT table, herein called "blocklen"
	 */

	uint32_t reg = ROM32(bios->data[offset + 1]);
	uint8_t regincrement = bios->data[offset + 5];
	uint8_t count = bios->data[offset + 6];
	uint32_t strap_ramcfg, data;
	/* previously set by 'M' BIT table */
	uint16_t blocklen = bios->ram_restrict_group_count * 4;
	int len = 7 + count * blocklen;
	uint8_t index;
	int i;


	if (!iexec->execute)
		return len;

	if (!blocklen) {
		NV_ERROR(bios->dev,
			 "0x%04X: Zero block length - has the M table "
			 "been parsed?\n", offset);
		return 0;
	}

	strap_ramcfg = (bios_rd32(bios, NV_PEXTDEV_BOOT_0) >> 2) & 0xf;
	index = bios->data[bios->ram_restrict_tbl_ptr + strap_ramcfg];

	BIOSLOG(bios, "0x%04X: Reg: 0x%08X, RegIncrement: 0x%02X, "
		      "Count: 0x%02X, StrapRamCfg: 0x%02X, Index: 0x%02X\n",
		offset, reg, regincrement, count, strap_ramcfg, index);

	for (i = 0; i < count; i++) {
		data = ROM32(bios->data[offset + 7 + index * 4 + blocklen * i]);

		bios_wr32(bios, reg, data);

		reg += regincrement;
	}

	return len;
}

static int
init_copy_zm_reg(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_COPY_ZM_REG   opcode: 0x90 ('')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): src reg
	 * offset + 5  (32 bit): dst reg
	 *
	 * Put contents of "src reg" into "dst reg"
	 */

	uint32_t srcreg = ROM32(bios->data[offset + 1]);
	uint32_t dstreg = ROM32(bios->data[offset + 5]);

	if (!iexec->execute)
		return 9;

	bios_wr32(bios, dstreg, bios_rd32(bios, srcreg));

	return 9;
}

static int
init_zm_reg_group_addr_latched(struct nvbios *bios, uint16_t offset,
			       struct init_exec *iexec)
{
	/*
	 * INIT_ZM_REG_GROUP_ADDRESS_LATCHED   opcode: 0x91 ('')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): dst reg
	 * offset + 5  (8  bit): count
	 * offset + 6  (32 bit): data 1
	 * ...
	 *
	 * For each of "count" values write "data n" to "dst reg"
	 */

	uint32_t reg = ROM32(bios->data[offset + 1]);
	uint8_t count = bios->data[offset + 5];
	int len = 6 + count * 4;
	int i;

	if (!iexec->execute)
		return len;

	for (i = 0; i < count; i++) {
		uint32_t data = ROM32(bios->data[offset + 6 + 4 * i]);
		bios_wr32(bios, reg, data);
	}

	return len;
}

static int
init_reserved(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_RESERVED   opcode: 0x92 ('')
	 *
	 * offset      (8 bit): opcode
	 *
	 * Seemingly does nothing
	 */

	return 1;
}

static int
init_96(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_96   opcode: 0x96 ('')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): sreg
	 * offset + 5  (8  bit): sshift
	 * offset + 6  (8  bit): smask
	 * offset + 7  (8  bit): index
	 * offset + 8  (32 bit): reg
	 * offset + 12 (32 bit): mask
	 * offset + 16 (8  bit): shift
	 *
	 */

	uint16_t xlatptr = bios->init96_tbl_ptr + (bios->data[offset + 7] * 2);
	uint32_t reg = ROM32(bios->data[offset + 8]);
	uint32_t mask = ROM32(bios->data[offset + 12]);
	uint32_t val;

	val = bios_rd32(bios, ROM32(bios->data[offset + 1]));
	if (bios->data[offset + 5] < 0x80)
		val >>= bios->data[offset + 5];
	else
		val <<= (0x100 - bios->data[offset + 5]);
	val &= bios->data[offset + 6];

	val   = bios->data[ROM16(bios->data[xlatptr]) + val];
	val <<= bios->data[offset + 16];

	if (!iexec->execute)
		return 17;

	bios_wr32(bios, reg, (bios_rd32(bios, reg) & mask) | val);
	return 17;
}

static int
init_97(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_97   opcode: 0x97 ('')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): register
	 * offset + 5  (32 bit): mask
	 * offset + 9  (32 bit): value
	 *
	 * Adds "value" to "register" preserving the fields specified
	 * by "mask"
	 */

	uint32_t reg = ROM32(bios->data[offset + 1]);
	uint32_t mask = ROM32(bios->data[offset + 5]);
	uint32_t add = ROM32(bios->data[offset + 9]);
	uint32_t val;

	val = bios_rd32(bios, reg);
	val = (val & mask) | ((val + add) & ~mask);

	if (!iexec->execute)
		return 13;

	bios_wr32(bios, reg, val);
	return 13;
}

static int
init_auxch(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_AUXCH   opcode: 0x98 ('')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): address
	 * offset + 5  (8  bit): count
	 * offset + 6  (8  bit): mask 0
	 * offset + 7  (8  bit): data 0
	 *  ...
	 *
	 */

	struct drm_device *dev = bios->dev;
	struct nouveau_i2c_chan *auxch;
	uint32_t addr = ROM32(bios->data[offset + 1]);
	uint8_t count = bios->data[offset + 5];
	int len = 6 + count * 2;
	int ret, i;

	if (!bios->display.output) {
		NV_ERROR(dev, "INIT_AUXCH: no active output\n");
		return 0;
	}

	auxch = init_i2c_device_find(dev, bios->display.output->i2c_index);
	if (!auxch) {
		NV_ERROR(dev, "INIT_AUXCH: couldn't get auxch %d\n",
			 bios->display.output->i2c_index);
		return 0;
	}

	if (!iexec->execute)
		return len;

	offset += 6;
	for (i = 0; i < count; i++, offset += 2) {
		uint8_t data;

		ret = nouveau_dp_auxch(auxch, 9, addr, &data, 1);
		if (ret) {
			NV_ERROR(dev, "INIT_AUXCH: rd auxch fail %d\n", ret);
			return 0;
		}

		data &= bios->data[offset + 0];
		data |= bios->data[offset + 1];

		ret = nouveau_dp_auxch(auxch, 8, addr, &data, 1);
		if (ret) {
			NV_ERROR(dev, "INIT_AUXCH: wr auxch fail %d\n", ret);
			return 0;
		}
	}

	return len;
}

static int
init_zm_auxch(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_ZM_AUXCH   opcode: 0x99 ('')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (32 bit): address
	 * offset + 5  (8  bit): count
	 * offset + 6  (8  bit): data 0
	 *  ...
	 *
	 */

	struct drm_device *dev = bios->dev;
	struct nouveau_i2c_chan *auxch;
	uint32_t addr = ROM32(bios->data[offset + 1]);
	uint8_t count = bios->data[offset + 5];
	int len = 6 + count;
	int ret, i;

	if (!bios->display.output) {
		NV_ERROR(dev, "INIT_ZM_AUXCH: no active output\n");
		return 0;
	}

	auxch = init_i2c_device_find(dev, bios->display.output->i2c_index);
	if (!auxch) {
		NV_ERROR(dev, "INIT_ZM_AUXCH: couldn't get auxch %d\n",
			 bios->display.output->i2c_index);
		return 0;
	}

	if (!iexec->execute)
		return len;

	offset += 6;
	for (i = 0; i < count; i++, offset++) {
		ret = nouveau_dp_auxch(auxch, 8, addr, &bios->data[offset], 1);
		if (ret) {
			NV_ERROR(dev, "INIT_ZM_AUXCH: wr auxch fail %d\n", ret);
			return 0;
		}
	}

	return len;
}

static struct init_tbl_entry itbl_entry[] = {
	/* command name                       , id  , length  , offset  , mult    , command handler                 */
	/* INIT_PROG (0x31, 15, 10, 4) removed due to no example of use */
	{ "INIT_IO_RESTRICT_PROG"             , 0x32, init_io_restrict_prog           },
	{ "INIT_REPEAT"                       , 0x33, init_repeat                     },
	{ "INIT_IO_RESTRICT_PLL"              , 0x34, init_io_restrict_pll            },
	{ "INIT_END_REPEAT"                   , 0x36, init_end_repeat                 },
	{ "INIT_COPY"                         , 0x37, init_copy                       },
	{ "INIT_NOT"                          , 0x38, init_not                        },
	{ "INIT_IO_FLAG_CONDITION"            , 0x39, init_io_flag_condition          },
	{ "INIT_INDEX_ADDRESS_LATCHED"        , 0x49, init_idx_addr_latched           },
	{ "INIT_IO_RESTRICT_PLL2"             , 0x4A, init_io_restrict_pll2           },
	{ "INIT_PLL2"                         , 0x4B, init_pll2                       },
	{ "INIT_I2C_BYTE"                     , 0x4C, init_i2c_byte                   },
	{ "INIT_ZM_I2C_BYTE"                  , 0x4D, init_zm_i2c_byte                },
	{ "INIT_ZM_I2C"                       , 0x4E, init_zm_i2c                     },
	{ "INIT_TMDS"                         , 0x4F, init_tmds                       },
	{ "INIT_ZM_TMDS_GROUP"                , 0x50, init_zm_tmds_group              },
	{ "INIT_CR_INDEX_ADDRESS_LATCHED"     , 0x51, init_cr_idx_adr_latch           },
	{ "INIT_CR"                           , 0x52, init_cr                         },
	{ "INIT_ZM_CR"                        , 0x53, init_zm_cr                      },
	{ "INIT_ZM_CR_GROUP"                  , 0x54, init_zm_cr_group                },
	{ "INIT_CONDITION_TIME"               , 0x56, init_condition_time             },
	{ "INIT_ZM_REG_SEQUENCE"              , 0x58, init_zm_reg_sequence            },
	/* INIT_INDIRECT_REG (0x5A, 7, 0, 0) removed due to no example of use */
	{ "INIT_SUB_DIRECT"                   , 0x5B, init_sub_direct                 },
	{ "INIT_COPY_NV_REG"                  , 0x5F, init_copy_nv_reg                },
	{ "INIT_ZM_INDEX_IO"                  , 0x62, init_zm_index_io                },
	{ "INIT_COMPUTE_MEM"                  , 0x63, init_compute_mem                },
	{ "INIT_RESET"                        , 0x65, init_reset                      },
	{ "INIT_CONFIGURE_MEM"                , 0x66, init_configure_mem              },
	{ "INIT_CONFIGURE_CLK"                , 0x67, init_configure_clk              },
	{ "INIT_CONFIGURE_PREINIT"            , 0x68, init_configure_preinit          },
	{ "INIT_IO"                           , 0x69, init_io                         },
	{ "INIT_SUB"                          , 0x6B, init_sub                        },
	{ "INIT_RAM_CONDITION"                , 0x6D, init_ram_condition              },
	{ "INIT_NV_REG"                       , 0x6E, init_nv_reg                     },
	{ "INIT_MACRO"                        , 0x6F, init_macro                      },
	{ "INIT_DONE"                         , 0x71, init_done                       },
	{ "INIT_RESUME"                       , 0x72, init_resume                     },
	/* INIT_RAM_CONDITION2 (0x73, 9, 0, 0) removed due to no example of use */
	{ "INIT_TIME"                         , 0x74, init_time                       },
	{ "INIT_CONDITION"                    , 0x75, init_condition                  },
	{ "INIT_IO_CONDITION"                 , 0x76, init_io_condition               },
	{ "INIT_INDEX_IO"                     , 0x78, init_index_io                   },
	{ "INIT_PLL"                          , 0x79, init_pll                        },
	{ "INIT_ZM_REG"                       , 0x7A, init_zm_reg                     },
	{ "INIT_RAM_RESTRICT_PLL"             , 0x87, init_ram_restrict_pll           },
	{ "INIT_8C"                           , 0x8C, init_8c                         },
	{ "INIT_8D"                           , 0x8D, init_8d                         },
	{ "INIT_GPIO"                         , 0x8E, init_gpio                       },
	{ "INIT_RAM_RESTRICT_ZM_REG_GROUP"    , 0x8F, init_ram_restrict_zm_reg_group  },
	{ "INIT_COPY_ZM_REG"                  , 0x90, init_copy_zm_reg                },
	{ "INIT_ZM_REG_GROUP_ADDRESS_LATCHED" , 0x91, init_zm_reg_group_addr_latched  },
	{ "INIT_RESERVED"                     , 0x92, init_reserved                   },
	{ "INIT_96"                           , 0x96, init_96                         },
	{ "INIT_97"                           , 0x97, init_97                         },
	{ "INIT_AUXCH"                        , 0x98, init_auxch                      },
	{ "INIT_ZM_AUXCH"                     , 0x99, init_zm_auxch                   },
	{ NULL                                , 0   , NULL                            }
};

#define MAX_TABLE_OPS 1000

static int
parse_init_table(struct nvbios *bios, unsigned int offset,
		 struct init_exec *iexec)
{
	/*
	 * Parses all commands in an init table.
	 *
	 * We start out executing all commands found in the init table. Some
	 * opcodes may change the status of iexec->execute to SKIP, which will
	 * cause the following opcodes to perform no operation until the value
	 * is changed back to EXECUTE.
	 */

	int count = 0, i, res;
	uint8_t id;

	/*
	 * Loop until INIT_DONE causes us to break out of the loop
	 * (or until offset > bios length just in case... )
	 * (and no more than MAX_TABLE_OPS iterations, just in case... )
	 */
	while ((offset < bios->length) && (count++ < MAX_TABLE_OPS)) {
		id = bios->data[offset];

		/* Find matching id in itbl_entry */
		for (i = 0; itbl_entry[i].name && (itbl_entry[i].id != id); i++)
			;

		if (itbl_entry[i].name) {
			BIOSLOG(bios, "0x%04X: [ (0x%02X) - %s ]\n",
				offset, itbl_entry[i].id, itbl_entry[i].name);

			/* execute eventual command handler */
			res = (*itbl_entry[i].handler)(bios, offset, iexec);
			if (!res)
				break;
			/*
			 * Add the offset of the current command including all data
			 * of that command. The offset will then be pointing on the
			 * next op code.
			 */
			offset += res;
		} else {
			NV_ERROR(bios->dev,
				 "0x%04X: Init table command not found: "
				 "0x%02X\n", offset, id);
			return -ENOENT;
		}
	}

	if (offset >= bios->length)
		NV_WARN(bios->dev,
			"Offset 0x%04X greater than known bios image length.  "
			"Corrupt image?\n", offset);
	if (count >= MAX_TABLE_OPS)
		NV_WARN(bios->dev,
			"More than %d opcodes to a table is unlikely, "
			"is the bios image corrupt?\n", MAX_TABLE_OPS);

	return 0;
}

static void
parse_init_tables(struct nvbios *bios)
{
	/* Loops and calls parse_init_table() for each present table. */

	int i = 0;
	uint16_t table;
	struct init_exec iexec = {true, false};

	if (bios->old_style_init) {
		if (bios->init_script_tbls_ptr)
			parse_init_table(bios, bios->init_script_tbls_ptr, &iexec);
		if (bios->extra_init_script_tbl_ptr)
			parse_init_table(bios, bios->extra_init_script_tbl_ptr, &iexec);

		return;
	}

	while ((table = ROM16(bios->data[bios->init_script_tbls_ptr + i]))) {
		NV_INFO(bios->dev,
			"Parsing VBIOS init table %d at offset 0x%04X\n",
			i / 2, table);
		BIOSLOG(bios, "0x%04X: ------ Executing following commands ------\n", table);

		parse_init_table(bios, table, &iexec);
		i += 2;
	}
}

static uint16_t clkcmptable(struct nvbios *bios, uint16_t clktable, int pxclk)
{
	int compare_record_len, i = 0;
	uint16_t compareclk, scriptptr = 0;

	if (bios->major_version < 5) /* pre BIT */
		compare_record_len = 3;
	else
		compare_record_len = 4;

	do {
		compareclk = ROM16(bios->data[clktable + compare_record_len * i]);
		if (pxclk >= compareclk * 10) {
			if (bios->major_version < 5) {
				uint8_t tmdssub = bios->data[clktable + 2 + compare_record_len * i];
				scriptptr = ROM16(bios->data[bios->init_script_tbls_ptr + tmdssub * 2]);
			} else
				scriptptr = ROM16(bios->data[clktable + 2 + compare_record_len * i]);
			break;
		}
		i++;
	} while (compareclk);

	return scriptptr;
}

static void
run_digital_op_script(struct drm_device *dev, uint16_t scriptptr,
		      struct dcb_entry *dcbent, int head, bool dl)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;
	struct init_exec iexec = {true, false};

	NV_TRACE(dev, "0x%04X: Parsing digital output script table\n",
		 scriptptr);
	bios_idxprt_wr(bios, NV_CIO_CRX__COLOR, NV_CIO_CRE_44,
		       head ? NV_CIO_CRE_44_HEADB : NV_CIO_CRE_44_HEADA);
	/* note: if dcb entries have been merged, index may be misleading */
	NVWriteVgaCrtc5758(dev, head, 0, dcbent->index);
	parse_init_table(bios, scriptptr, &iexec);

	nv04_dfp_bind_head(dev, dcbent, head, dl);
}

static int call_lvds_manufacturer_script(struct drm_device *dev, struct dcb_entry *dcbent, int head, enum LVDS_script script)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;
	uint8_t sub = bios->data[bios->fp.xlated_entry + script] + (bios->fp.link_c_increment && dcbent->or & OUTPUT_C ? 1 : 0);
	uint16_t scriptofs = ROM16(bios->data[bios->init_script_tbls_ptr + sub * 2]);

	if (!bios->fp.xlated_entry || !sub || !scriptofs)
		return -EINVAL;

	run_digital_op_script(dev, scriptofs, dcbent, head, bios->fp.dual_link);

	if (script == LVDS_PANEL_OFF) {
		/* off-on delay in ms */
		msleep(ROM16(bios->data[bios->fp.xlated_entry + 7]));
	}
#ifdef __powerpc__
	/* Powerbook specific quirks */
	if (script == LVDS_RESET && ((dev->pci_device & 0xffff) == 0x0179 || (dev->pci_device & 0xffff) == 0x0329))
		nv_write_tmds(dev, dcbent->or, 0, 0x02, 0x72);
	if ((dev->pci_device & 0xffff) == 0x0179 || (dev->pci_device & 0xffff) == 0x0189 || (dev->pci_device & 0xffff) == 0x0329) {
		if (script == LVDS_PANEL_ON) {
			bios_wr32(bios, NV_PBUS_DEBUG_DUALHEAD_CTL, bios_rd32(bios, NV_PBUS_DEBUG_DUALHEAD_CTL) | (1 << 31));
			bios_wr32(bios, NV_PCRTC_GPIO_EXT, bios_rd32(bios, NV_PCRTC_GPIO_EXT) | 1);
		}
		if (script == LVDS_PANEL_OFF) {
			bios_wr32(bios, NV_PBUS_DEBUG_DUALHEAD_CTL, bios_rd32(bios, NV_PBUS_DEBUG_DUALHEAD_CTL) & ~(1 << 31));
			bios_wr32(bios, NV_PCRTC_GPIO_EXT, bios_rd32(bios, NV_PCRTC_GPIO_EXT) & ~3);
		}
	}
#endif

	return 0;
}

static int run_lvds_table(struct drm_device *dev, struct dcb_entry *dcbent, int head, enum LVDS_script script, int pxclk)
{
	/*
	 * The BIT LVDS table's header has the information to setup the
	 * necessary registers. Following the standard 4 byte header are:
	 * A bitmask byte and a dual-link transition pxclk value for use in
	 * selecting the init script when not using straps; 4 script pointers
	 * for panel power, selected by output and on/off; and 8 table pointers
	 * for panel init, the needed one determined by output, and bits in the
	 * conf byte. These tables are similar to the TMDS tables, consisting
	 * of a list of pxclks and script pointers.
	 */
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;
	unsigned int outputset = (dcbent->or == 4) ? 1 : 0;
	uint16_t scriptptr = 0, clktable;
	uint8_t clktableptr = 0;

	/*
	 * For now we assume version 3.0 table - g80 support will need some
	 * changes
	 */

	switch (script) {
	case LVDS_INIT:
		return -ENOSYS;
	case LVDS_BACKLIGHT_ON:
	case LVDS_PANEL_ON:
		scriptptr = ROM16(bios->data[bios->fp.lvdsmanufacturerpointer + 7 + outputset * 2]);
		break;
	case LVDS_BACKLIGHT_OFF:
	case LVDS_PANEL_OFF:
		scriptptr = ROM16(bios->data[bios->fp.lvdsmanufacturerpointer + 11 + outputset * 2]);
		break;
	case LVDS_RESET:
		if (dcbent->lvdsconf.use_straps_for_mode) {
			if (bios->fp.dual_link)
				clktableptr += 2;
			if (bios->fp.BITbit1)
				clktableptr++;
		} else {
			/* using EDID */
			uint8_t fallback = bios->data[bios->fp.lvdsmanufacturerpointer + 4];
			int fallbackcmpval = (dcbent->or == 4) ? 4 : 1;

			if (bios->fp.dual_link) {
				clktableptr += 2;
				fallbackcmpval *= 2;
			}
			if (fallbackcmpval & fallback)
				clktableptr++;
		}

		/* adding outputset * 8 may not be correct */
		clktable = ROM16(bios->data[bios->fp.lvdsmanufacturerpointer + 15 + clktableptr * 2 + outputset * 8]);
		if (!clktable) {
			NV_ERROR(dev, "Pixel clock comparison table not found\n");
			return -ENOENT;
		}
		scriptptr = clkcmptable(bios, clktable, pxclk);
	}

	if (!scriptptr) {
		NV_ERROR(dev, "LVDS output init script not found\n");
		return -ENOENT;
	}
	run_digital_op_script(dev, scriptptr, dcbent, head, bios->fp.dual_link);

	return 0;
}

int call_lvds_script(struct drm_device *dev, struct dcb_entry *dcbent, int head, enum LVDS_script script, int pxclk)
{
	/*
	 * LVDS operations are multiplexed in an effort to present a single API
	 * which works with two vastly differing underlying structures.
	 * This acts as the demux
	 */

	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;
	uint8_t lvds_ver = bios->data[bios->fp.lvdsmanufacturerpointer];
	uint32_t sel_clk_binding, sel_clk;
	int ret;

	if (bios->fp.last_script_invoc == (script << 1 | head) || !lvds_ver ||
	    (lvds_ver >= 0x30 && script == LVDS_INIT))
		return 0;

	if (!bios->fp.lvds_init_run) {
		bios->fp.lvds_init_run = true;
		call_lvds_script(dev, dcbent, head, LVDS_INIT, pxclk);
	}

	if (script == LVDS_PANEL_ON && bios->fp.reset_after_pclk_change)
		call_lvds_script(dev, dcbent, head, LVDS_RESET, pxclk);
	if (script == LVDS_RESET && bios->fp.power_off_for_reset)
		call_lvds_script(dev, dcbent, head, LVDS_PANEL_OFF, pxclk);

	NV_TRACE(dev, "Calling LVDS script %d:\n", script);

	/* don't let script change pll->head binding */
	sel_clk_binding = bios_rd32(bios, NV_PRAMDAC_SEL_CLK) & 0x50000;

	if (lvds_ver < 0x30)
		ret = call_lvds_manufacturer_script(dev, dcbent, head, script);
	else
		ret = run_lvds_table(dev, dcbent, head, script, pxclk);

	bios->fp.last_script_invoc = (script << 1 | head);

	sel_clk = NVReadRAMDAC(dev, 0, NV_PRAMDAC_SEL_CLK) & ~0x50000;
	NVWriteRAMDAC(dev, 0, NV_PRAMDAC_SEL_CLK, sel_clk | sel_clk_binding);
	/* some scripts set a value in NV_PBUS_POWERCTRL_2 and break video overlay */
	nvWriteMC(dev, NV_PBUS_POWERCTRL_2, 0);

	return ret;
}

struct lvdstableheader {
	uint8_t lvds_ver, headerlen, recordlen;
};

static int parse_lvds_manufacturer_table_header(struct drm_device *dev, struct nvbios *bios, struct lvdstableheader *lth)
{
	/*
	 * BMP version (0xa) LVDS table has a simple header of version and
	 * record length. The BIT LVDS table has the typical BIT table header:
	 * version byte, header length byte, record length byte, and a byte for
	 * the maximum number of records that can be held in the table.
	 */

	uint8_t lvds_ver, headerlen, recordlen;

	memset(lth, 0, sizeof(struct lvdstableheader));

	if (bios->fp.lvdsmanufacturerpointer == 0x0) {
		NV_ERROR(dev, "Pointer to LVDS manufacturer table invalid\n");
		return -EINVAL;
	}

	lvds_ver = bios->data[bios->fp.lvdsmanufacturerpointer];

	switch (lvds_ver) {
	case 0x0a:	/* pre NV40 */
		headerlen = 2;
		recordlen = bios->data[bios->fp.lvdsmanufacturerpointer + 1];
		break;
	case 0x30:	/* NV4x */
		headerlen = bios->data[bios->fp.lvdsmanufacturerpointer + 1];
		if (headerlen < 0x1f) {
			NV_ERROR(dev, "LVDS table header not understood\n");
			return -EINVAL;
		}
		recordlen = bios->data[bios->fp.lvdsmanufacturerpointer + 2];
		break;
	case 0x40:	/* G80/G90 */
		headerlen = bios->data[bios->fp.lvdsmanufacturerpointer + 1];
		if (headerlen < 0x7) {
			NV_ERROR(dev, "LVDS table header not understood\n");
			return -EINVAL;
		}
		recordlen = bios->data[bios->fp.lvdsmanufacturerpointer + 2];
		break;
	default:
		NV_ERROR(dev,
			 "LVDS table revision %d.%d not currently supported\n",
			 lvds_ver >> 4, lvds_ver & 0xf);
		return -ENOSYS;
	}

	lth->lvds_ver = lvds_ver;
	lth->headerlen = headerlen;
	lth->recordlen = recordlen;

	return 0;
}

static int
get_fp_strap(struct drm_device *dev, struct nvbios *bios)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	/*
	 * The fp strap is normally dictated by the "User Strap" in
	 * PEXTDEV_BOOT_0[20:16], but on BMP cards when bit 2 of the
	 * Internal_Flags struct at 0x48 is set, the user strap gets overriden
	 * by the PCI subsystem ID during POST, but not before the previous user
	 * strap has been committed to CR58 for CR57=0xf on head A, which may be
	 * read and used instead
	 */

	if (bios->major_version < 5 && bios->data[0x48] & 0x4)
		return NVReadVgaCrtc5758(dev, 0, 0xf) & 0xf;

	if (dev_priv->card_type >= NV_50)
		return (bios_rd32(bios, NV_PEXTDEV_BOOT_0) >> 24) & 0xf;
	else
		return (bios_rd32(bios, NV_PEXTDEV_BOOT_0) >> 16) & 0xf;
}

static int parse_fp_mode_table(struct drm_device *dev, struct nvbios *bios)
{
	uint8_t *fptable;
	uint8_t fptable_ver, headerlen = 0, recordlen, fpentries = 0xf, fpindex;
	int ret, ofs, fpstrapping;
	struct lvdstableheader lth;

	if (bios->fp.fptablepointer == 0x0) {
		/* Apple cards don't have the fp table; the laptops use DDC */
		/* The table is also missing on some x86 IGPs */
#ifndef __powerpc__
		NV_ERROR(dev, "Pointer to flat panel table invalid\n");
#endif
		bios->pub.digital_min_front_porch = 0x4b;
		return 0;
	}

	fptable = &bios->data[bios->fp.fptablepointer];
	fptable_ver = fptable[0];

	switch (fptable_ver) {
	/*
	 * BMP version 0x5.0x11 BIOSen have version 1 like tables, but no
	 * version field, and miss one of the spread spectrum/PWM bytes.
	 * This could affect early GF2Go parts (not seen any appropriate ROMs
	 * though). Here we assume that a version of 0x05 matches this case
	 * (combining with a BMP version check would be better), as the
	 * common case for the panel type field is 0x0005, and that is in
	 * fact what we are reading the first byte of.
	 */
	case 0x05:	/* some NV10, 11, 15, 16 */
		recordlen = 42;
		ofs = -1;
		break;
	case 0x10:	/* some NV15/16, and NV11+ */
		recordlen = 44;
		ofs = 0;
		break;
	case 0x20:	/* NV40+ */
		headerlen = fptable[1];
		recordlen = fptable[2];
		fpentries = fptable[3];
		/*
		 * fptable[4] is the minimum
		 * RAMDAC_FP_HCRTC -> RAMDAC_FP_HSYNC_START gap
		 */
		bios->pub.digital_min_front_porch = fptable[4];
		ofs = -7;
		break;
	default:
		NV_ERROR(dev,
			 "FP table revision %d.%d not currently supported\n",
			 fptable_ver >> 4, fptable_ver & 0xf);
		return -ENOSYS;
	}

	if (!bios->is_mobile) /* !mobile only needs digital_min_front_porch */
		return 0;

	ret = parse_lvds_manufacturer_table_header(dev, bios, &lth);
	if (ret)
		return ret;

	if (lth.lvds_ver == 0x30 || lth.lvds_ver == 0x40) {
		bios->fp.fpxlatetableptr = bios->fp.lvdsmanufacturerpointer +
							lth.headerlen + 1;
		bios->fp.xlatwidth = lth.recordlen;
	}
	if (bios->fp.fpxlatetableptr == 0x0) {
		NV_ERROR(dev, "Pointer to flat panel xlat table invalid\n");
		return -EINVAL;
	}

	fpstrapping = get_fp_strap(dev, bios);

	fpindex = bios->data[bios->fp.fpxlatetableptr +
					fpstrapping * bios->fp.xlatwidth];

	if (fpindex > fpentries) {
		NV_ERROR(dev, "Bad flat panel table index\n");
		return -ENOENT;
	}

	/* nv4x cards need both a strap value and fpindex of 0xf to use DDC */
	if (lth.lvds_ver > 0x10)
		bios->pub.fp_no_ddc = fpstrapping != 0xf || fpindex != 0xf;

	/*
	 * If either the strap or xlated fpindex value are 0xf there is no
	 * panel using a strap-derived bios mode present.  this condition
	 * includes, but is different from, the DDC panel indicator above
	 */
	if (fpstrapping == 0xf || fpindex == 0xf)
		return 0;

	bios->fp.mode_ptr = bios->fp.fptablepointer + headerlen +
			    recordlen * fpindex + ofs;

	NV_TRACE(dev, "BIOS FP mode: %dx%d (%dkHz pixel clock)\n",
		 ROM16(bios->data[bios->fp.mode_ptr + 11]) + 1,
		 ROM16(bios->data[bios->fp.mode_ptr + 25]) + 1,
		 ROM16(bios->data[bios->fp.mode_ptr + 7]) * 10);

	return 0;
}

bool nouveau_bios_fp_mode(struct drm_device *dev, struct drm_display_mode *mode)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;
	uint8_t *mode_entry = &bios->data[bios->fp.mode_ptr];

	if (!mode)	/* just checking whether we can produce a mode */
		return bios->fp.mode_ptr;

	memset(mode, 0, sizeof(struct drm_display_mode));
	/*
	 * For version 1.0 (version in byte 0):
	 * bytes 1-2 are "panel type", including bits on whether Colour/mono,
	 * single/dual link, and type (TFT etc.)
	 * bytes 3-6 are bits per colour in RGBX
	 */
	mode->clock = ROM16(mode_entry[7]) * 10;
	/* bytes 9-10 is HActive */
	mode->hdisplay = ROM16(mode_entry[11]) + 1;
	/*
	 * bytes 13-14 is HValid Start
	 * bytes 15-16 is HValid End
	 */
	mode->hsync_start = ROM16(mode_entry[17]) + 1;
	mode->hsync_end = ROM16(mode_entry[19]) + 1;
	mode->htotal = ROM16(mode_entry[21]) + 1;
	/* bytes 23-24, 27-30 similarly, but vertical */
	mode->vdisplay = ROM16(mode_entry[25]) + 1;
	mode->vsync_start = ROM16(mode_entry[31]) + 1;
	mode->vsync_end = ROM16(mode_entry[33]) + 1;
	mode->vtotal = ROM16(mode_entry[35]) + 1;
	mode->flags |= (mode_entry[37] & 0x10) ?
			DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC;
	mode->flags |= (mode_entry[37] & 0x1) ?
			DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC;
	/*
	 * bytes 38-39 relate to spread spectrum settings
	 * bytes 40-43 are something to do with PWM
	 */

	mode->status = MODE_OK;
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);
	return bios->fp.mode_ptr;
}

int nouveau_bios_parse_lvds_table(struct drm_device *dev, int pxclk, bool *dl, bool *if_is_24bit)
{
	/*
	 * The LVDS table header is (mostly) described in
	 * parse_lvds_manufacturer_table_header(): the BIT header additionally
	 * contains the dual-link transition pxclk (in 10s kHz), at byte 5 - if
	 * straps are not being used for the panel, this specifies the frequency
	 * at which modes should be set up in the dual link style.
	 *
	 * Following the header, the BMP (ver 0xa) table has several records,
	 * indexed by a seperate xlat table, indexed in turn by the fp strap in
	 * EXTDEV_BOOT. Each record had a config byte, followed by 6 script
	 * numbers for use by INIT_SUB which controlled panel init and power,
	 * and finally a dword of ms to sleep between power off and on
	 * operations.
	 *
	 * In the BIT versions, the table following the header serves as an
	 * integrated config and xlat table: the records in the table are
	 * indexed by the FP strap nibble in EXTDEV_BOOT, and each record has
	 * two bytes - the first as a config byte, the second for indexing the
	 * fp mode table pointed to by the BIT 'D' table
	 *
	 * DDC is not used until after card init, so selecting the correct table
	 * entry and setting the dual link flag for EDID equipped panels,
	 * requiring tests against the native-mode pixel clock, cannot be done
	 * until later, when this function should be called with non-zero pxclk
	 */
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;
	int fpstrapping = get_fp_strap(dev, bios), lvdsmanufacturerindex = 0;
	struct lvdstableheader lth;
	uint16_t lvdsofs;
	int ret, chip_version = bios->pub.chip_version;

	ret = parse_lvds_manufacturer_table_header(dev, bios, &lth);
	if (ret)
		return ret;

	switch (lth.lvds_ver) {
	case 0x0a:	/* pre NV40 */
		lvdsmanufacturerindex = bios->data[
					bios->fp.fpxlatemanufacturertableptr +
					fpstrapping];

		/* we're done if this isn't the EDID panel case */
		if (!pxclk)
			break;

		if (chip_version < 0x25) {
			/* nv17 behaviour
			 *
			 * It seems the old style lvds script pointer is reused
			 * to select 18/24 bit colour depth for EDID panels.
			 */
			lvdsmanufacturerindex =
				(bios->legacy.lvds_single_a_script_ptr & 1) ?
									2 : 0;
			if (pxclk >= bios->fp.duallink_transition_clk)
				lvdsmanufacturerindex++;
		} else if (chip_version < 0x30) {
			/* nv28 behaviour (off-chip encoder)
			 *
			 * nv28 does a complex dance of first using byte 121 of
			 * the EDID to choose the lvdsmanufacturerindex, then
			 * later attempting to match the EDID manufacturer and
			 * product IDs in a table (signature 'pidt' (panel id
			 * table?)), setting an lvdsmanufacturerindex of 0 and
			 * an fp strap of the match index (or 0xf if none)
			 */
			lvdsmanufacturerindex = 0;
		} else {
			/* nv31, nv34 behaviour */
			lvdsmanufacturerindex = 0;
			if (pxclk >= bios->fp.duallink_transition_clk)
				lvdsmanufacturerindex = 2;
			if (pxclk >= 140000)
				lvdsmanufacturerindex = 3;
		}

		/*
		 * nvidia set the high nibble of (cr57=f, cr58) to
		 * lvdsmanufacturerindex in this case; we don't
		 */
		break;
	case 0x30:	/* NV4x */
	case 0x40:	/* G80/G90 */
		lvdsmanufacturerindex = fpstrapping;
		break;
	default:
		NV_ERROR(dev, "LVDS table revision not currently supported\n");
		return -ENOSYS;
	}

	lvdsofs = bios->fp.xlated_entry = bios->fp.lvdsmanufacturerpointer + lth.headerlen + lth.recordlen * lvdsmanufacturerindex;
	switch (lth.lvds_ver) {
	case 0x0a:
		bios->fp.power_off_for_reset = bios->data[lvdsofs] & 1;
		bios->fp.reset_after_pclk_change = bios->data[lvdsofs] & 2;
		bios->fp.dual_link = bios->data[lvdsofs] & 4;
		bios->fp.link_c_increment = bios->data[lvdsofs] & 8;
		*if_is_24bit = bios->data[lvdsofs] & 16;
		break;
	case 0x30:
		/*
		 * My money would be on there being a 24 bit interface bit in
		 * this table, but I have no example of a laptop bios with a
		 * 24 bit panel to confirm that. Hence we shout loudly if any
		 * bit other than bit 0 is set (I've not even seen bit 1)
		 */
		if (bios->data[lvdsofs] > 1)
			NV_ERROR(dev,
				 "You have a very unusual laptop display; please report it\n");
		/*
		 * No sign of the "power off for reset" or "reset for panel
		 * on" bits, but it's safer to assume we should
		 */
		bios->fp.power_off_for_reset = true;
		bios->fp.reset_after_pclk_change = true;
		/*
		 * It's ok lvdsofs is wrong for nv4x edid case; dual_link is
		 * over-written, and BITbit1 isn't used
		 */
		bios->fp.dual_link = bios->data[lvdsofs] & 1;
		bios->fp.BITbit1 = bios->data[lvdsofs] & 2;
		bios->fp.duallink_transition_clk = ROM16(bios->data[bios->fp.lvdsmanufacturerpointer + 5]) * 10;
		break;
	case 0x40:
		bios->fp.dual_link = bios->data[lvdsofs] & 1;
		bios->fp.if_is_24bit = bios->data[lvdsofs] & 2;
		bios->fp.strapless_is_24bit = bios->data[bios->fp.lvdsmanufacturerpointer + 4];
		bios->fp.duallink_transition_clk = ROM16(bios->data[bios->fp.lvdsmanufacturerpointer + 5]) * 10;
		break;
	}

	/* set dual_link flag for EDID case */
	if (pxclk && (chip_version < 0x25 || chip_version > 0x28))
		bios->fp.dual_link = (pxclk >= bios->fp.duallink_transition_clk);

	*dl = bios->fp.dual_link;

	return 0;
}

static uint8_t *
bios_output_config_match(struct drm_device *dev, struct dcb_entry *dcbent,
			 uint16_t record, int record_len, int record_nr)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;
	uint32_t entry;
	uint16_t table;
	int i, v;

	for (i = 0; i < record_nr; i++, record += record_len) {
		table = ROM16(bios->data[record]);
		if (!table)
			continue;
		entry = ROM32(bios->data[table]);

		v = (entry & 0x000f0000) >> 16;
		if (!(v & dcbent->or))
			continue;

		v = (entry & 0x000000f0) >> 4;
		if (v != dcbent->location)
			continue;

		v = (entry & 0x0000000f);
		if (v != dcbent->type)
			continue;

		return &bios->data[table];
	}

	return NULL;
}

void *
nouveau_bios_dp_table(struct drm_device *dev, struct dcb_entry *dcbent,
		      int *length)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;
	uint8_t *table;

	if (!bios->display.dp_table_ptr) {
		NV_ERROR(dev, "No pointer to DisplayPort table\n");
		return NULL;
	}
	table = &bios->data[bios->display.dp_table_ptr];

	if (table[0] != 0x21) {
		NV_ERROR(dev, "DisplayPort table version 0x%02x unknown\n",
			 table[0]);
		return NULL;
	}

	*length = table[4];
	return bios_output_config_match(dev, dcbent,
					bios->display.dp_table_ptr + table[1],
					table[2], table[3]);
}

int
nouveau_bios_run_display_table(struct drm_device *dev, struct dcb_entry *dcbent,
			       uint32_t sub, int pxclk)
{
	/*
	 * The display script table is located by the BIT 'U' table.
	 *
	 * It contains an array of pointers to various tables describing
	 * a particular output type.  The first 32-bits of the output
	 * tables contains similar information to a DCB entry, and is
	 * used to decide whether that particular table is suitable for
	 * the output you want to access.
	 *
	 * The "record header length" field here seems to indicate the
	 * offset of the first configuration entry in the output tables.
	 * This is 10 on most cards I've seen, but 12 has been witnessed
	 * on DP cards, and there's another script pointer within the
	 * header.
	 *
	 * offset + 0   ( 8 bits): version
	 * offset + 1   ( 8 bits): header length
	 * offset + 2   ( 8 bits): record length
	 * offset + 3   ( 8 bits): number of records
	 * offset + 4   ( 8 bits): record header length
	 * offset + 5   (16 bits): pointer to first output script table
	 */

	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct init_exec iexec = {true, false};
	struct nvbios *bios = &dev_priv->VBIOS;
	uint8_t *table = &bios->data[bios->display.script_table_ptr];
	uint8_t *otable = NULL;
	uint16_t script;
	int i = 0;

	if (!bios->display.script_table_ptr) {
		NV_ERROR(dev, "No pointer to output script table\n");
		return 1;
	}

	/*
	 * Nothing useful has been in any of the pre-2.0 tables I've seen,
	 * so until they are, we really don't need to care.
	 */
	if (table[0] < 0x20)
		return 1;

	if (table[0] != 0x20 && table[0] != 0x21) {
		NV_ERROR(dev, "Output script table version 0x%02x unknown\n",
			 table[0]);
		return 1;
	}

	/*
	 * The output script tables describing a particular output type
	 * look as follows:
	 *
	 * offset + 0   (32 bits): output this table matches (hash of DCB)
	 * offset + 4   ( 8 bits): unknown
	 * offset + 5   ( 8 bits): number of configurations
	 * offset + 6   (16 bits): pointer to some script
	 * offset + 8   (16 bits): pointer to some script
	 *
	 * headerlen == 10
	 * offset + 10           : configuration 0
	 *
	 * headerlen == 12
	 * offset + 10           : pointer to some script
	 * offset + 12           : configuration 0
	 *
	 * Each config entry is as follows:
	 *
	 * offset + 0   (16 bits): unknown, assumed to be a match value
	 * offset + 2   (16 bits): pointer to script table (clock set?)
	 * offset + 4   (16 bits): pointer to script table (reset?)
	 *
	 * There doesn't appear to be a count value to say how many
	 * entries exist in each script table, instead, a 0 value in
	 * the first 16-bit word seems to indicate both the end of the
	 * list and the default entry.  The second 16-bit word in the
	 * script tables is a pointer to the script to execute.
	 */

	NV_DEBUG_KMS(dev, "Searching for output entry for %d %d %d\n",
			dcbent->type, dcbent->location, dcbent->or);
	otable = bios_output_config_match(dev, dcbent, table[1] +
					  bios->display.script_table_ptr,
					  table[2], table[3]);
	if (!otable) {
		NV_ERROR(dev, "Couldn't find matching output script table\n");
		return 1;
	}

	if (pxclk < -2 || pxclk > 0) {
		/* Try to find matching script table entry */
		for (i = 0; i < otable[5]; i++) {
			if (ROM16(otable[table[4] + i*6]) == sub)
				break;
		}

		if (i == otable[5]) {
			NV_ERROR(dev, "Table 0x%04x not found for %d/%d, "
				      "using first\n",
				 sub, dcbent->type, dcbent->or);
			i = 0;
		}
	}

	bios->display.output = dcbent;

	if (pxclk == 0) {
		script = ROM16(otable[6]);
		if (!script) {
			NV_DEBUG_KMS(dev, "output script 0 not found\n");
			return 1;
		}

		NV_TRACE(dev, "0x%04X: parsing output script 0\n", script);
		parse_init_table(bios, script, &iexec);
	} else
	if (pxclk == -1) {
		script = ROM16(otable[8]);
		if (!script) {
			NV_DEBUG_KMS(dev, "output script 1 not found\n");
			return 1;
		}

		NV_TRACE(dev, "0x%04X: parsing output script 1\n", script);
		parse_init_table(bios, script, &iexec);
	} else
	if (pxclk == -2) {
		if (table[4] >= 12)
			script = ROM16(otable[10]);
		else
			script = 0;
		if (!script) {
			NV_DEBUG_KMS(dev, "output script 2 not found\n");
			return 1;
		}

		NV_TRACE(dev, "0x%04X: parsing output script 2\n", script);
		parse_init_table(bios, script, &iexec);
	} else
	if (pxclk > 0) {
		script = ROM16(otable[table[4] + i*6 + 2]);
		if (script)
			script = clkcmptable(bios, script, pxclk);
		if (!script) {
			NV_ERROR(dev, "clock script 0 not found\n");
			return 1;
		}

		NV_TRACE(dev, "0x%04X: parsing clock script 0\n", script);
		parse_init_table(bios, script, &iexec);
	} else
	if (pxclk < 0) {
		script = ROM16(otable[table[4] + i*6 + 4]);
		if (script)
			script = clkcmptable(bios, script, -pxclk);
		if (!script) {
			NV_DEBUG_KMS(dev, "clock script 1 not found\n");
			return 1;
		}

		NV_TRACE(dev, "0x%04X: parsing clock script 1\n", script);
		parse_init_table(bios, script, &iexec);
	}

	return 0;
}


int run_tmds_table(struct drm_device *dev, struct dcb_entry *dcbent, int head, int pxclk)
{
	/*
	 * the pxclk parameter is in kHz
	 *
	 * This runs the TMDS regs setting code found on BIT bios cards
	 *
	 * For ffs(or) == 1 use the first table, for ffs(or) == 2 and
	 * ffs(or) == 3, use the second.
	 */

	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;
	int cv = bios->pub.chip_version;
	uint16_t clktable = 0, scriptptr;
	uint32_t sel_clk_binding, sel_clk;

	/* pre-nv17 off-chip tmds uses scripts, post nv17 doesn't */
	if (cv >= 0x17 && cv != 0x1a && cv != 0x20 &&
	    dcbent->location != DCB_LOC_ON_CHIP)
		return 0;

	switch (ffs(dcbent->or)) {
	case 1:
		clktable = bios->tmds.output0_script_ptr;
		break;
	case 2:
	case 3:
		clktable = bios->tmds.output1_script_ptr;
		break;
	}

	if (!clktable) {
		NV_ERROR(dev, "Pixel clock comparison table not found\n");
		return -EINVAL;
	}

	scriptptr = clkcmptable(bios, clktable, pxclk);

	if (!scriptptr) {
		NV_ERROR(dev, "TMDS output init script not found\n");
		return -ENOENT;
	}

	/* don't let script change pll->head binding */
	sel_clk_binding = bios_rd32(bios, NV_PRAMDAC_SEL_CLK) & 0x50000;
	run_digital_op_script(dev, scriptptr, dcbent, head, pxclk >= 165000);
	sel_clk = NVReadRAMDAC(dev, 0, NV_PRAMDAC_SEL_CLK) & ~0x50000;
	NVWriteRAMDAC(dev, 0, NV_PRAMDAC_SEL_CLK, sel_clk | sel_clk_binding);

	return 0;
}

int get_pll_limits(struct drm_device *dev, uint32_t limit_match, struct pll_lims *pll_lim)
{
	/*
	 * PLL limits table
	 *
	 * Version 0x10: NV30, NV31
	 * One byte header (version), one record of 24 bytes
	 * Version 0x11: NV36 - Not implemented
	 * Seems to have same record style as 0x10, but 3 records rather than 1
	 * Version 0x20: Found on Geforce 6 cards
	 * Trivial 4 byte BIT header. 31 (0x1f) byte record length
	 * Version 0x21: Found on Geforce 7, 8 and some Geforce 6 cards
	 * 5 byte header, fifth byte of unknown purpose. 35 (0x23) byte record
	 * length in general, some (integrated) have an extra configuration byte
	 * Version 0x30: Found on Geforce 8, separates the register mapping
	 * from the limits tables.
	 */

	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;
	int cv = bios->pub.chip_version, pllindex = 0;
	uint8_t pll_lim_ver = 0, headerlen = 0, recordlen = 0, entries = 0;
	uint32_t crystal_strap_mask, crystal_straps;

	if (!bios->pll_limit_tbl_ptr) {
		if (cv == 0x30 || cv == 0x31 || cv == 0x35 || cv == 0x36 ||
		    cv >= 0x40) {
			NV_ERROR(dev, "Pointer to PLL limits table invalid\n");
			return -EINVAL;
		}
	} else
		pll_lim_ver = bios->data[bios->pll_limit_tbl_ptr];

	crystal_strap_mask = 1 << 6;
	/* open coded dev->twoHeads test */
	if (cv > 0x10 && cv != 0x15 && cv != 0x1a && cv != 0x20)
		crystal_strap_mask |= 1 << 22;
	crystal_straps = nvReadEXTDEV(dev, NV_PEXTDEV_BOOT_0) &
							crystal_strap_mask;

	switch (pll_lim_ver) {
	/*
	 * We use version 0 to indicate a pre limit table bios (single stage
	 * pll) and load the hard coded limits instead.
	 */
	case 0:
		break;
	case 0x10:
	case 0x11:
		/*
		 * Strictly v0x11 has 3 entries, but the last two don't seem
		 * to get used.
		 */
		headerlen = 1;
		recordlen = 0x18;
		entries = 1;
		pllindex = 0;
		break;
	case 0x20:
	case 0x21:
	case 0x30:
	case 0x40:
		headerlen = bios->data[bios->pll_limit_tbl_ptr + 1];
		recordlen = bios->data[bios->pll_limit_tbl_ptr + 2];
		entries = bios->data[bios->pll_limit_tbl_ptr + 3];
		break;
	default:
		NV_ERROR(dev, "PLL limits table revision 0x%X not currently "
				"supported\n", pll_lim_ver);
		return -ENOSYS;
	}

	/* initialize all members to zero */
	memset(pll_lim, 0, sizeof(struct pll_lims));

	if (pll_lim_ver == 0x10 || pll_lim_ver == 0x11) {
		uint8_t *pll_rec = &bios->data[bios->pll_limit_tbl_ptr + headerlen + recordlen * pllindex];

		pll_lim->vco1.minfreq = ROM32(pll_rec[0]);
		pll_lim->vco1.maxfreq = ROM32(pll_rec[4]);
		pll_lim->vco2.minfreq = ROM32(pll_rec[8]);
		pll_lim->vco2.maxfreq = ROM32(pll_rec[12]);
		pll_lim->vco1.min_inputfreq = ROM32(pll_rec[16]);
		pll_lim->vco2.min_inputfreq = ROM32(pll_rec[20]);
		pll_lim->vco1.max_inputfreq = pll_lim->vco2.max_inputfreq = INT_MAX;

		/* these values taken from nv30/31/36 */
		pll_lim->vco1.min_n = 0x1;
		if (cv == 0x36)
			pll_lim->vco1.min_n = 0x5;
		pll_lim->vco1.max_n = 0xff;
		pll_lim->vco1.min_m = 0x1;
		pll_lim->vco1.max_m = 0xd;
		pll_lim->vco2.min_n = 0x4;
		/*
		 * On nv30, 31, 36 (i.e. all cards with two stage PLLs with this
		 * table version (apart from nv35)), N2 is compared to
		 * maxN2 (0x46) and 10 * maxM2 (0x4), so set maxN2 to 0x28 and
		 * save a comparison
		 */
		pll_lim->vco2.max_n = 0x28;
		if (cv == 0x30 || cv == 0x35)
			/* only 5 bits available for N2 on nv30/35 */
			pll_lim->vco2.max_n = 0x1f;
		pll_lim->vco2.min_m = 0x1;
		pll_lim->vco2.max_m = 0x4;
		pll_lim->max_log2p = 0x7;
		pll_lim->max_usable_log2p = 0x6;
	} else if (pll_lim_ver == 0x20 || pll_lim_ver == 0x21) {
		uint16_t plloffs = bios->pll_limit_tbl_ptr + headerlen;
		uint32_t reg = 0; /* default match */
		uint8_t *pll_rec;
		int i;

		/*
		 * First entry is default match, if nothing better. warn if
		 * reg field nonzero
		 */
		if (ROM32(bios->data[plloffs]))
			NV_WARN(dev, "Default PLL limit entry has non-zero "
				       "register field\n");

		if (limit_match > MAX_PLL_TYPES)
			/* we've been passed a reg as the match */
			reg = limit_match;
		else /* limit match is a pll type */
			for (i = 1; i < entries && !reg; i++) {
				uint32_t cmpreg = ROM32(bios->data[plloffs + recordlen * i]);

				if (limit_match == NVPLL &&
				    (cmpreg == NV_PRAMDAC_NVPLL_COEFF || cmpreg == 0x4000))
					reg = cmpreg;
				if (limit_match == MPLL &&
				    (cmpreg == NV_PRAMDAC_MPLL_COEFF || cmpreg == 0x4020))
					reg = cmpreg;
				if (limit_match == VPLL1 &&
				    (cmpreg == NV_PRAMDAC_VPLL_COEFF || cmpreg == 0x4010))
					reg = cmpreg;
				if (limit_match == VPLL2 &&
				    (cmpreg == NV_RAMDAC_VPLL2 || cmpreg == 0x4018))
					reg = cmpreg;
			}

		for (i = 1; i < entries; i++)
			if (ROM32(bios->data[plloffs + recordlen * i]) == reg) {
				pllindex = i;
				break;
			}

		pll_rec = &bios->data[plloffs + recordlen * pllindex];

		BIOSLOG(bios, "Loading PLL limits for reg 0x%08x\n",
			pllindex ? reg : 0);

		/*
		 * Frequencies are stored in tables in MHz, kHz are more
		 * useful, so we convert.
		 */

		/* What output frequencies can each VCO generate? */
		pll_lim->vco1.minfreq = ROM16(pll_rec[4]) * 1000;
		pll_lim->vco1.maxfreq = ROM16(pll_rec[6]) * 1000;
		pll_lim->vco2.minfreq = ROM16(pll_rec[8]) * 1000;
		pll_lim->vco2.maxfreq = ROM16(pll_rec[10]) * 1000;

		/* What input frequencies they accept (past the m-divider)? */
		pll_lim->vco1.min_inputfreq = ROM16(pll_rec[12]) * 1000;
		pll_lim->vco2.min_inputfreq = ROM16(pll_rec[14]) * 1000;
		pll_lim->vco1.max_inputfreq = ROM16(pll_rec[16]) * 1000;
		pll_lim->vco2.max_inputfreq = ROM16(pll_rec[18]) * 1000;

		/* What values are accepted as multiplier and divider? */
		pll_lim->vco1.min_n = pll_rec[20];
		pll_lim->vco1.max_n = pll_rec[21];
		pll_lim->vco1.min_m = pll_rec[22];
		pll_lim->vco1.max_m = pll_rec[23];
		pll_lim->vco2.min_n = pll_rec[24];
		pll_lim->vco2.max_n = pll_rec[25];
		pll_lim->vco2.min_m = pll_rec[26];
		pll_lim->vco2.max_m = pll_rec[27];

		pll_lim->max_usable_log2p = pll_lim->max_log2p = pll_rec[29];
		if (pll_lim->max_log2p > 0x7)
			/* pll decoding in nv_hw.c assumes never > 7 */
			NV_WARN(dev, "Max log2 P value greater than 7 (%d)\n",
				pll_lim->max_log2p);
		if (cv < 0x60)
			pll_lim->max_usable_log2p = 0x6;
		pll_lim->log2p_bias = pll_rec[30];

		if (recordlen > 0x22)
			pll_lim->refclk = ROM32(pll_rec[31]);

		if (recordlen > 0x23 && pll_rec[35])
			NV_WARN(dev,
				"Bits set in PLL configuration byte (%x)\n",
				pll_rec[35]);

		/* C51 special not seen elsewhere */
		if (cv == 0x51 && !pll_lim->refclk) {
			uint32_t sel_clk = bios_rd32(bios, NV_PRAMDAC_SEL_CLK);

			if (((limit_match == NV_PRAMDAC_VPLL_COEFF || limit_match == VPLL1) && sel_clk & 0x20) ||
			    ((limit_match == NV_RAMDAC_VPLL2 || limit_match == VPLL2) && sel_clk & 0x80)) {
				if (bios_idxprt_rd(bios, NV_CIO_CRX__COLOR, NV_CIO_CRE_CHIP_ID_INDEX) < 0xa3)
					pll_lim->refclk = 200000;
				else
					pll_lim->refclk = 25000;
			}
		}
	} else if (pll_lim_ver == 0x30) { /* ver 0x30 */
		uint8_t *entry = &bios->data[bios->pll_limit_tbl_ptr + headerlen];
		uint8_t *record = NULL;
		int i;

		BIOSLOG(bios, "Loading PLL limits for register 0x%08x\n",
			limit_match);

		for (i = 0; i < entries; i++, entry += recordlen) {
			if (ROM32(entry[3]) == limit_match) {
				record = &bios->data[ROM16(entry[1])];
				break;
			}
		}

		if (!record) {
			NV_ERROR(dev, "Register 0x%08x not found in PLL "
				 "limits table", limit_match);
			return -ENOENT;
		}

		pll_lim->vco1.minfreq = ROM16(record[0]) * 1000;
		pll_lim->vco1.maxfreq = ROM16(record[2]) * 1000;
		pll_lim->vco2.minfreq = ROM16(record[4]) * 1000;
		pll_lim->vco2.maxfreq = ROM16(record[6]) * 1000;
		pll_lim->vco1.min_inputfreq = ROM16(record[8]) * 1000;
		pll_lim->vco2.min_inputfreq = ROM16(record[10]) * 1000;
		pll_lim->vco1.max_inputfreq = ROM16(record[12]) * 1000;
		pll_lim->vco2.max_inputfreq = ROM16(record[14]) * 1000;
		pll_lim->vco1.min_n = record[16];
		pll_lim->vco1.max_n = record[17];
		pll_lim->vco1.min_m = record[18];
		pll_lim->vco1.max_m = record[19];
		pll_lim->vco2.min_n = record[20];
		pll_lim->vco2.max_n = record[21];
		pll_lim->vco2.min_m = record[22];
		pll_lim->vco2.max_m = record[23];
		pll_lim->max_usable_log2p = pll_lim->max_log2p = record[25];
		pll_lim->log2p_bias = record[27];
		pll_lim->refclk = ROM32(record[28]);
	} else if (pll_lim_ver) { /* ver 0x40 */
		uint8_t *entry = &bios->data[bios->pll_limit_tbl_ptr + headerlen];
		uint8_t *record = NULL;
		int i;

		BIOSLOG(bios, "Loading PLL limits for register 0x%08x\n",
			limit_match);

		for (i = 0; i < entries; i++, entry += recordlen) {
			if (ROM32(entry[3]) == limit_match) {
				record = &bios->data[ROM16(entry[1])];
				break;
			}
		}

		if (!record) {
			NV_ERROR(dev, "Register 0x%08x not found in PLL "
				 "limits table", limit_match);
			return -ENOENT;
		}

		pll_lim->vco1.minfreq = ROM16(record[0]) * 1000;
		pll_lim->vco1.maxfreq = ROM16(record[2]) * 1000;
		pll_lim->vco1.min_inputfreq = ROM16(record[4]) * 1000;
		pll_lim->vco1.max_inputfreq = ROM16(record[6]) * 1000;
		pll_lim->vco1.min_m = record[8];
		pll_lim->vco1.max_m = record[9];
		pll_lim->vco1.min_n = record[10];
		pll_lim->vco1.max_n = record[11];
		pll_lim->min_p = record[12];
		pll_lim->max_p = record[13];
		/* where did this go to?? */
		if (limit_match == 0x00614100 || limit_match == 0x00614900)
			pll_lim->refclk = 27000;
		else
			pll_lim->refclk = 100000;
	}

	/*
	 * By now any valid limit table ought to have set a max frequency for
	 * vco1, so if it's zero it's either a pre limit table bios, or one
	 * with an empty limit table (seen on nv18)
	 */
	if (!pll_lim->vco1.maxfreq) {
		pll_lim->vco1.minfreq = bios->fminvco;
		pll_lim->vco1.maxfreq = bios->fmaxvco;
		pll_lim->vco1.min_inputfreq = 0;
		pll_lim->vco1.max_inputfreq = INT_MAX;
		pll_lim->vco1.min_n = 0x1;
		pll_lim->vco1.max_n = 0xff;
		pll_lim->vco1.min_m = 0x1;
		if (crystal_straps == 0) {
			/* nv05 does this, nv11 doesn't, nv10 unknown */
			if (cv < 0x11)
				pll_lim->vco1.min_m = 0x7;
			pll_lim->vco1.max_m = 0xd;
		} else {
			if (cv < 0x11)
				pll_lim->vco1.min_m = 0x8;
			pll_lim->vco1.max_m = 0xe;
		}
		if (cv < 0x17 || cv == 0x1a || cv == 0x20)
			pll_lim->max_log2p = 4;
		else
			pll_lim->max_log2p = 5;
		pll_lim->max_usable_log2p = pll_lim->max_log2p;
	}

	if (!pll_lim->refclk)
		switch (crystal_straps) {
		case 0:
			pll_lim->refclk = 13500;
			break;
		case (1 << 6):
			pll_lim->refclk = 14318;
			break;
		case (1 << 22):
			pll_lim->refclk = 27000;
			break;
		case (1 << 22 | 1 << 6):
			pll_lim->refclk = 25000;
			break;
		}

#if 0 /* for easy debugging */
	ErrorF("pll.vco1.minfreq: %d\n", pll_lim->vco1.minfreq);
	ErrorF("pll.vco1.maxfreq: %d\n", pll_lim->vco1.maxfreq);
	ErrorF("pll.vco2.minfreq: %d\n", pll_lim->vco2.minfreq);
	ErrorF("pll.vco2.maxfreq: %d\n", pll_lim->vco2.maxfreq);

	ErrorF("pll.vco1.min_inputfreq: %d\n", pll_lim->vco1.min_inputfreq);
	ErrorF("pll.vco1.max_inputfreq: %d\n", pll_lim->vco1.max_inputfreq);
	ErrorF("pll.vco2.min_inputfreq: %d\n", pll_lim->vco2.min_inputfreq);
	ErrorF("pll.vco2.max_inputfreq: %d\n", pll_lim->vco2.max_inputfreq);

	ErrorF("pll.vco1.min_n: %d\n", pll_lim->vco1.min_n);
	ErrorF("pll.vco1.max_n: %d\n", pll_lim->vco1.max_n);
	ErrorF("pll.vco1.min_m: %d\n", pll_lim->vco1.min_m);
	ErrorF("pll.vco1.max_m: %d\n", pll_lim->vco1.max_m);
	ErrorF("pll.vco2.min_n: %d\n", pll_lim->vco2.min_n);
	ErrorF("pll.vco2.max_n: %d\n", pll_lim->vco2.max_n);
	ErrorF("pll.vco2.min_m: %d\n", pll_lim->vco2.min_m);
	ErrorF("pll.vco2.max_m: %d\n", pll_lim->vco2.max_m);

	ErrorF("pll.max_log2p: %d\n", pll_lim->max_log2p);
	ErrorF("pll.log2p_bias: %d\n", pll_lim->log2p_bias);

	ErrorF("pll.refclk: %d\n", pll_lim->refclk);
#endif

	return 0;
}

static void parse_bios_version(struct drm_device *dev, struct nvbios *bios, uint16_t offset)
{
	/*
	 * offset + 0  (8 bits): Micro version
	 * offset + 1  (8 bits): Minor version
	 * offset + 2  (8 bits): Chip version
	 * offset + 3  (8 bits): Major version
	 */

	bios->major_version = bios->data[offset + 3];
	bios->pub.chip_version = bios->data[offset + 2];
	NV_TRACE(dev, "Bios version %02x.%02x.%02x.%02x\n",
		 bios->data[offset + 3], bios->data[offset + 2],
		 bios->data[offset + 1], bios->data[offset]);
}

static void parse_script_table_pointers(struct nvbios *bios, uint16_t offset)
{
	/*
	 * Parses the init table segment for pointers used in script execution.
	 *
	 * offset + 0  (16 bits): init script tables pointer
	 * offset + 2  (16 bits): macro index table pointer
	 * offset + 4  (16 bits): macro table pointer
	 * offset + 6  (16 bits): condition table pointer
	 * offset + 8  (16 bits): io condition table pointer
	 * offset + 10 (16 bits): io flag condition table pointer
	 * offset + 12 (16 bits): init function table pointer
	 */

	bios->init_script_tbls_ptr = ROM16(bios->data[offset]);
	bios->macro_index_tbl_ptr = ROM16(bios->data[offset + 2]);
	bios->macro_tbl_ptr = ROM16(bios->data[offset + 4]);
	bios->condition_tbl_ptr = ROM16(bios->data[offset + 6]);
	bios->io_condition_tbl_ptr = ROM16(bios->data[offset + 8]);
	bios->io_flag_condition_tbl_ptr = ROM16(bios->data[offset + 10]);
	bios->init_function_tbl_ptr = ROM16(bios->data[offset + 12]);
}

static int parse_bit_A_tbl_entry(struct drm_device *dev, struct nvbios *bios, struct bit_entry *bitentry)
{
	/*
	 * Parses the load detect values for g80 cards.
	 *
	 * offset + 0 (16 bits): loadval table pointer
	 */

	uint16_t load_table_ptr;
	uint8_t version, headerlen, entrylen, num_entries;

	if (bitentry->length != 3) {
		NV_ERROR(dev, "Do not understand BIT A table\n");
		return -EINVAL;
	}

	load_table_ptr = ROM16(bios->data[bitentry->offset]);

	if (load_table_ptr == 0x0) {
		NV_ERROR(dev, "Pointer to BIT loadval table invalid\n");
		return -EINVAL;
	}

	version = bios->data[load_table_ptr];

	if (version != 0x10) {
		NV_ERROR(dev, "BIT loadval table version %d.%d not supported\n",
			 version >> 4, version & 0xF);
		return -ENOSYS;
	}

	headerlen = bios->data[load_table_ptr + 1];
	entrylen = bios->data[load_table_ptr + 2];
	num_entries = bios->data[load_table_ptr + 3];

	if (headerlen != 4 || entrylen != 4 || num_entries != 2) {
		NV_ERROR(dev, "Do not understand BIT loadval table\n");
		return -EINVAL;
	}

	/* First entry is normal dac, 2nd tv-out perhaps? */
	bios->pub.dactestval = ROM32(bios->data[load_table_ptr + headerlen]) & 0x3ff;

	return 0;
}

static int parse_bit_C_tbl_entry(struct drm_device *dev, struct nvbios *bios, struct bit_entry *bitentry)
{
	/*
	 * offset + 8  (16 bits): PLL limits table pointer
	 *
	 * There's more in here, but that's unknown.
	 */

	if (bitentry->length < 10) {
		NV_ERROR(dev, "Do not understand BIT C table\n");
		return -EINVAL;
	}

	bios->pll_limit_tbl_ptr = ROM16(bios->data[bitentry->offset + 8]);

	return 0;
}

static int parse_bit_display_tbl_entry(struct drm_device *dev, struct nvbios *bios, struct bit_entry *bitentry)
{
	/*
	 * Parses the flat panel table segment that the bit entry points to.
	 * Starting at bitentry->offset:
	 *
	 * offset + 0  (16 bits): ??? table pointer - seems to have 18 byte
	 * records beginning with a freq.
	 * offset + 2  (16 bits): mode table pointer
	 */

	if (bitentry->length != 4) {
		NV_ERROR(dev, "Do not understand BIT display table\n");
		return -EINVAL;
	}

	bios->fp.fptablepointer = ROM16(bios->data[bitentry->offset + 2]);

	return 0;
}

static int parse_bit_init_tbl_entry(struct drm_device *dev, struct nvbios *bios, struct bit_entry *bitentry)
{
	/*
	 * Parses the init table segment that the bit entry points to.
	 *
	 * See parse_script_table_pointers for layout
	 */

	if (bitentry->length < 14) {
		NV_ERROR(dev, "Do not understand init table\n");
		return -EINVAL;
	}

	parse_script_table_pointers(bios, bitentry->offset);

	if (bitentry->length >= 16)
		bios->some_script_ptr = ROM16(bios->data[bitentry->offset + 14]);
	if (bitentry->length >= 18)
		bios->init96_tbl_ptr = ROM16(bios->data[bitentry->offset + 16]);

	return 0;
}

static int parse_bit_i_tbl_entry(struct drm_device *dev, struct nvbios *bios, struct bit_entry *bitentry)
{
	/*
	 * BIT 'i' (info?) table
	 *
	 * offset + 0  (32 bits): BIOS version dword (as in B table)
	 * offset + 5  (8  bits): BIOS feature byte (same as for BMP?)
	 * offset + 13 (16 bits): pointer to table containing DAC load
	 * detection comparison values
	 *
	 * There's other things in the table, purpose unknown
	 */

	uint16_t daccmpoffset;
	uint8_t dacver, dacheaderlen;

	if (bitentry->length < 6) {
		NV_ERROR(dev, "BIT i table too short for needed information\n");
		return -EINVAL;
	}

	parse_bios_version(dev, bios, bitentry->offset);

	/*
	 * bit 4 seems to indicate a mobile bios (doesn't suffer from BMP's
	 * Quadro identity crisis), other bits possibly as for BMP feature byte
	 */
	bios->feature_byte = bios->data[bitentry->offset + 5];
	bios->is_mobile = bios->feature_byte & FEATURE_MOBILE;

	if (bitentry->length < 15) {
		NV_WARN(dev, "BIT i table not long enough for DAC load "
			       "detection comparison table\n");
		return -EINVAL;
	}

	daccmpoffset = ROM16(bios->data[bitentry->offset + 13]);

	/* doesn't exist on g80 */
	if (!daccmpoffset)
		return 0;

	/*
	 * The first value in the table, following the header, is the
	 * comparison value, the second entry is a comparison value for
	 * TV load detection.
	 */

	dacver = bios->data[daccmpoffset];
	dacheaderlen = bios->data[daccmpoffset + 1];

	if (dacver != 0x00 && dacver != 0x10) {
		NV_WARN(dev, "DAC load detection comparison table version "
			       "%d.%d not known\n", dacver >> 4, dacver & 0xf);
		return -ENOSYS;
	}

	bios->pub.dactestval = ROM32(bios->data[daccmpoffset + dacheaderlen]);
	bios->pub.tvdactestval = ROM32(bios->data[daccmpoffset + dacheaderlen + 4]);

	return 0;
}

static int parse_bit_lvds_tbl_entry(struct drm_device *dev, struct nvbios *bios, struct bit_entry *bitentry)
{
	/*
	 * Parses the LVDS table segment that the bit entry points to.
	 * Starting at bitentry->offset:
	 *
	 * offset + 0  (16 bits): LVDS strap xlate table pointer
	 */

	if (bitentry->length != 2) {
		NV_ERROR(dev, "Do not understand BIT LVDS table\n");
		return -EINVAL;
	}

	/*
	 * No idea if it's still called the LVDS manufacturer table, but
	 * the concept's close enough.
	 */
	bios->fp.lvdsmanufacturerpointer = ROM16(bios->data[bitentry->offset]);

	return 0;
}

static int
parse_bit_M_tbl_entry(struct drm_device *dev, struct nvbios *bios,
		      struct bit_entry *bitentry)
{
	/*
	 * offset + 2  (8  bits): number of options in an
	 * 	INIT_RAM_RESTRICT_ZM_REG_GROUP opcode option set
	 * offset + 3  (16 bits): pointer to strap xlate table for RAM
	 * 	restrict option selection
	 *
	 * There's a bunch of bits in this table other than the RAM restrict
	 * stuff that we don't use - their use currently unknown
	 */

	/*
	 * Older bios versions don't have a sufficiently long table for
	 * what we want
	 */
	if (bitentry->length < 0x5)
		return 0;

	if (bitentry->id[1] < 2) {
		bios->ram_restrict_group_count = bios->data[bitentry->offset + 2];
		bios->ram_restrict_tbl_ptr = ROM16(bios->data[bitentry->offset + 3]);
	} else {
		bios->ram_restrict_group_count = bios->data[bitentry->offset + 0];
		bios->ram_restrict_tbl_ptr = ROM16(bios->data[bitentry->offset + 1]);
	}

	return 0;
}

static int parse_bit_tmds_tbl_entry(struct drm_device *dev, struct nvbios *bios, struct bit_entry *bitentry)
{
	/*
	 * Parses the pointer to the TMDS table
	 *
	 * Starting at bitentry->offset:
	 *
	 * offset + 0  (16 bits): TMDS table pointer
	 *
	 * The TMDS table is typically found just before the DCB table, with a
	 * characteristic signature of 0x11,0x13 (1.1 being version, 0x13 being
	 * length?)
	 *
	 * At offset +7 is a pointer to a script, which I don't know how to
	 * run yet.
	 * At offset +9 is a pointer to another script, likewise
	 * Offset +11 has a pointer to a table where the first word is a pxclk
	 * frequency and the second word a pointer to a script, which should be
	 * run if the comparison pxclk frequency is less than the pxclk desired.
	 * This repeats for decreasing comparison frequencies
	 * Offset +13 has a pointer to a similar table
	 * The selection of table (and possibly +7/+9 script) is dictated by
	 * "or" from the DCB.
	 */

	uint16_t tmdstableptr, script1, script2;

	if (bitentry->length != 2) {
		NV_ERROR(dev, "Do not understand BIT TMDS table\n");
		return -EINVAL;
	}

	tmdstableptr = ROM16(bios->data[bitentry->offset]);

	if (tmdstableptr == 0x0) {
		NV_ERROR(dev, "Pointer to TMDS table invalid\n");
		return -EINVAL;
	}

	/* nv50+ has v2.0, but we don't parse it atm */
	if (bios->data[tmdstableptr] != 0x11) {
		NV_WARN(dev,
			"TMDS table revision %d.%d not currently supported\n",
			bios->data[tmdstableptr] >> 4, bios->data[tmdstableptr] & 0xf);
		return -ENOSYS;
	}

	/*
	 * These two scripts are odd: they don't seem to get run even when
	 * they are not stubbed.
	 */
	script1 = ROM16(bios->data[tmdstableptr + 7]);
	script2 = ROM16(bios->data[tmdstableptr + 9]);
	if (bios->data[script1] != 'q' || bios->data[script2] != 'q')
		NV_WARN(dev, "TMDS table script pointers not stubbed\n");

	bios->tmds.output0_script_ptr = ROM16(bios->data[tmdstableptr + 11]);
	bios->tmds.output1_script_ptr = ROM16(bios->data[tmdstableptr + 13]);

	return 0;
}

static int
parse_bit_U_tbl_entry(struct drm_device *dev, struct nvbios *bios,
		      struct bit_entry *bitentry)
{
	/*
	 * Parses the pointer to the G80 output script tables
	 *
	 * Starting at bitentry->offset:
	 *
	 * offset + 0  (16 bits): output script table pointer
	 */

	uint16_t outputscripttableptr;

	if (bitentry->length != 3) {
		NV_ERROR(dev, "Do not understand BIT U table\n");
		return -EINVAL;
	}

	outputscripttableptr = ROM16(bios->data[bitentry->offset]);
	bios->display.script_table_ptr = outputscripttableptr;
	return 0;
}

static int
parse_bit_displayport_tbl_entry(struct drm_device *dev, struct nvbios *bios,
				struct bit_entry *bitentry)
{
	bios->display.dp_table_ptr = ROM16(bios->data[bitentry->offset]);
	return 0;
}

struct bit_table {
	const char id;
	int (* const parse_fn)(struct drm_device *, struct nvbios *, struct bit_entry *);
};

#define BIT_TABLE(id, funcid) ((struct bit_table){ id, parse_bit_##funcid##_tbl_entry })

static int
parse_bit_table(struct nvbios *bios, const uint16_t bitoffset,
		struct bit_table *table)
{
	struct drm_device *dev = bios->dev;
	uint8_t maxentries = bios->data[bitoffset + 4];
	int i, offset;
	struct bit_entry bitentry;

	for (i = 0, offset = bitoffset + 6; i < maxentries; i++, offset += 6) {
		bitentry.id[0] = bios->data[offset];

		if (bitentry.id[0] != table->id)
			continue;

		bitentry.id[1] = bios->data[offset + 1];
		bitentry.length = ROM16(bios->data[offset + 2]);
		bitentry.offset = ROM16(bios->data[offset + 4]);

		return table->parse_fn(dev, bios, &bitentry);
	}

	NV_INFO(dev, "BIT table '%c' not found\n", table->id);
	return -ENOSYS;
}

static int
parse_bit_structure(struct nvbios *bios, const uint16_t bitoffset)
{
	int ret;

	/*
	 * The only restriction on parsing order currently is having 'i' first
	 * for use of bios->*_version or bios->feature_byte while parsing;
	 * functions shouldn't be actually *doing* anything apart from pulling
	 * data from the image into the bios struct, thus no interdependencies
	 */
	ret = parse_bit_table(bios, bitoffset, &BIT_TABLE('i', i));
	if (ret) /* info? */
		return ret;
	if (bios->major_version >= 0x60) /* g80+ */
		parse_bit_table(bios, bitoffset, &BIT_TABLE('A', A));
	ret = parse_bit_table(bios, bitoffset, &BIT_TABLE('C', C));
	if (ret)
		return ret;
	parse_bit_table(bios, bitoffset, &BIT_TABLE('D', display));
	ret = parse_bit_table(bios, bitoffset, &BIT_TABLE('I', init));
	if (ret)
		return ret;
	parse_bit_table(bios, bitoffset, &BIT_TABLE('M', M)); /* memory? */
	parse_bit_table(bios, bitoffset, &BIT_TABLE('L', lvds));
	parse_bit_table(bios, bitoffset, &BIT_TABLE('T', tmds));
	parse_bit_table(bios, bitoffset, &BIT_TABLE('U', U));
	parse_bit_table(bios, bitoffset, &BIT_TABLE('d', displayport));

	return 0;
}

static int parse_bmp_structure(struct drm_device *dev, struct nvbios *bios, unsigned int offset)
{
	/*
	 * Parses the BMP structure for useful things, but does not act on them
	 *
	 * offset +   5: BMP major version
	 * offset +   6: BMP minor version
	 * offset +   9: BMP feature byte
	 * offset +  10: BCD encoded BIOS version
	 *
	 * offset +  18: init script table pointer (for bios versions < 5.10h)
	 * offset +  20: extra init script table pointer (for bios
	 * versions < 5.10h)
	 *
	 * offset +  24: memory init table pointer (used on early bios versions)
	 * offset +  26: SDR memory sequencing setup data table
	 * offset +  28: DDR memory sequencing setup data table
	 *
	 * offset +  54: index of I2C CRTC pair to use for CRT output
	 * offset +  55: index of I2C CRTC pair to use for TV output
	 * offset +  56: index of I2C CRTC pair to use for flat panel output
	 * offset +  58: write CRTC index for I2C pair 0
	 * offset +  59: read CRTC index for I2C pair 0
	 * offset +  60: write CRTC index for I2C pair 1
	 * offset +  61: read CRTC index for I2C pair 1
	 *
	 * offset +  67: maximum internal PLL frequency (single stage PLL)
	 * offset +  71: minimum internal PLL frequency (single stage PLL)
	 *
	 * offset +  75: script table pointers, as described in
	 * parse_script_table_pointers
	 *
	 * offset +  89: TMDS single link output A table pointer
	 * offset +  91: TMDS single link output B table pointer
	 * offset +  95: LVDS single link output A table pointer
	 * offset + 105: flat panel timings table pointer
	 * offset + 107: flat panel strapping translation table pointer
	 * offset + 117: LVDS manufacturer panel config table pointer
	 * offset + 119: LVDS manufacturer strapping translation table pointer
	 *
	 * offset + 142: PLL limits table pointer
	 *
	 * offset + 156: minimum pixel clock for LVDS dual link
	 */

	uint8_t *bmp = &bios->data[offset], bmp_version_major, bmp_version_minor;
	uint16_t bmplength;
	uint16_t legacy_scripts_offset, legacy_i2c_offset;

	/* load needed defaults in case we can't parse this info */
	bios->bdcb.dcb.i2c[0].write = NV_CIO_CRE_DDC_WR__INDEX;
	bios->bdcb.dcb.i2c[0].read = NV_CIO_CRE_DDC_STATUS__INDEX;
	bios->bdcb.dcb.i2c[1].write = NV_CIO_CRE_DDC0_WR__INDEX;
	bios->bdcb.dcb.i2c[1].read = NV_CIO_CRE_DDC0_STATUS__INDEX;
	bios->pub.digital_min_front_porch = 0x4b;
	bios->fmaxvco = 256000;
	bios->fminvco = 128000;
	bios->fp.duallink_transition_clk = 90000;

	bmp_version_major = bmp[5];
	bmp_version_minor = bmp[6];

	NV_TRACE(dev, "BMP version %d.%d\n",
		 bmp_version_major, bmp_version_minor);

	/*
	 * Make sure that 0x36 is blank and can't be mistaken for a DCB
	 * pointer on early versions
	 */
	if (bmp_version_major < 5)
		*(uint16_t *)&bios->data[0x36] = 0;

	/*
	 * Seems that the minor version was 1 for all major versions prior
	 * to 5. Version 6 could theoretically exist, but I suspect BIT
	 * happened instead.
	 */
	if ((bmp_version_major < 5 && bmp_version_minor != 1) || bmp_version_major > 5) {
		NV_ERROR(dev, "You have an unsupported BMP version. "
				"Please send in your bios\n");
		return -ENOSYS;
	}

	if (bmp_version_major == 0)
		/* nothing that's currently useful in this version */
		return 0;
	else if (bmp_version_major == 1)
		bmplength = 44; /* exact for 1.01 */
	else if (bmp_version_major == 2)
		bmplength = 48; /* exact for 2.01 */
	else if (bmp_version_major == 3)
		bmplength = 54;
		/* guessed - mem init tables added in this version */
	else if (bmp_version_major == 4 || bmp_version_minor < 0x1)
		/* don't know if 5.0 exists... */
		bmplength = 62;
		/* guessed - BMP I2C indices added in version 4*/
	else if (bmp_version_minor < 0x6)
		bmplength = 67; /* exact for 5.01 */
	else if (bmp_version_minor < 0x10)
		bmplength = 75; /* exact for 5.06 */
	else if (bmp_version_minor == 0x10)
		bmplength = 89; /* exact for 5.10h */
	else if (bmp_version_minor < 0x14)
		bmplength = 118; /* exact for 5.11h */
	else if (bmp_version_minor < 0x24)
		/*
		 * Not sure of version where pll limits came in;
		 * certainly exist by 0x24 though.
		 */
		/* length not exact: this is long enough to get lvds members */
		bmplength = 123;
	else if (bmp_version_minor < 0x27)
		/*
		 * Length not exact: this is long enough to get pll limit
		 * member
		 */
		bmplength = 144;
	else
		/*
		 * Length not exact: this is long enough to get dual link
		 * transition clock.
		 */
		bmplength = 158;

	/* checksum */
	if (nv_cksum(bmp, 8)) {
		NV_ERROR(dev, "Bad BMP checksum\n");
		return -EINVAL;
	}

	/*
	 * Bit 4 seems to indicate either a mobile bios or a quadro card --
	 * mobile behaviour consistent (nv11+), quadro only seen nv18gl-nv36gl
	 * (not nv10gl), bit 5 that the flat panel tables are present, and
	 * bit 6 a tv bios.
	 */
	bios->feature_byte = bmp[9];

	parse_bios_version(dev, bios, offset + 10);

	if (bmp_version_major < 5 || bmp_version_minor < 0x10)
		bios->old_style_init = true;
	legacy_scripts_offset = 18;
	if (bmp_version_major < 2)
		legacy_scripts_offset -= 4;
	bios->init_script_tbls_ptr = ROM16(bmp[legacy_scripts_offset]);
	bios->extra_init_script_tbl_ptr = ROM16(bmp[legacy_scripts_offset + 2]);

	if (bmp_version_major > 2) {	/* appears in BMP 3 */
		bios->legacy.mem_init_tbl_ptr = ROM16(bmp[24]);
		bios->legacy.sdr_seq_tbl_ptr = ROM16(bmp[26]);
		bios->legacy.ddr_seq_tbl_ptr = ROM16(bmp[28]);
	}

	legacy_i2c_offset = 0x48;	/* BMP version 2 & 3 */
	if (bmplength > 61)
		legacy_i2c_offset = offset + 54;
	bios->legacy.i2c_indices.crt = bios->data[legacy_i2c_offset];
	bios->legacy.i2c_indices.tv = bios->data[legacy_i2c_offset + 1];
	bios->legacy.i2c_indices.panel = bios->data[legacy_i2c_offset + 2];
	bios->bdcb.dcb.i2c[0].write = bios->data[legacy_i2c_offset + 4];
	bios->bdcb.dcb.i2c[0].read = bios->data[legacy_i2c_offset + 5];
	bios->bdcb.dcb.i2c[1].write = bios->data[legacy_i2c_offset + 6];
	bios->bdcb.dcb.i2c[1].read = bios->data[legacy_i2c_offset + 7];

	if (bmplength > 74) {
		bios->fmaxvco = ROM32(bmp[67]);
		bios->fminvco = ROM32(bmp[71]);
	}
	if (bmplength > 88)
		parse_script_table_pointers(bios, offset + 75);
	if (bmplength > 94) {
		bios->tmds.output0_script_ptr = ROM16(bmp[89]);
		bios->tmds.output1_script_ptr = ROM16(bmp[91]);
		/*
		 * Never observed in use with lvds scripts, but is reused for
		 * 18/24 bit panel interface default for EDID equipped panels
		 * (if_is_24bit not set directly to avoid any oscillation).
		 */
		bios->legacy.lvds_single_a_script_ptr = ROM16(bmp[95]);
	}
	if (bmplength > 108) {
		bios->fp.fptablepointer = ROM16(bmp[105]);
		bios->fp.fpxlatetableptr = ROM16(bmp[107]);
		bios->fp.xlatwidth = 1;
	}
	if (bmplength > 120) {
		bios->fp.lvdsmanufacturerpointer = ROM16(bmp[117]);
		bios->fp.fpxlatemanufacturertableptr = ROM16(bmp[119]);
	}
	if (bmplength > 143)
		bios->pll_limit_tbl_ptr = ROM16(bmp[142]);

	if (bmplength > 157)
		bios->fp.duallink_transition_clk = ROM16(bmp[156]) * 10;

	return 0;
}

static uint16_t findstr(uint8_t *data, int n, const uint8_t *str, int len)
{
	int i, j;

	for (i = 0; i <= (n - len); i++) {
		for (j = 0; j < len; j++)
			if (data[i + j] != str[j])
				break;
		if (j == len)
			return i;
	}

	return 0;
}

static int
read_dcb_i2c_entry(struct drm_device *dev, int dcb_version, uint8_t *i2ctable, int index, struct dcb_i2c_entry *i2c)
{
	uint8_t dcb_i2c_ver = dcb_version, headerlen = 0, entry_len = 4;
	int i2c_entries = DCB_MAX_NUM_I2C_ENTRIES;
	int recordoffset = 0, rdofs = 1, wrofs = 0;
	uint8_t port_type = 0;

	if (!i2ctable)
		return -EINVAL;

	if (dcb_version >= 0x30) {
		if (i2ctable[0] != dcb_version) /* necessary? */
			NV_WARN(dev,
				"DCB I2C table version mismatch (%02X vs %02X)\n",
				i2ctable[0], dcb_version);
		dcb_i2c_ver = i2ctable[0];
		headerlen = i2ctable[1];
		if (i2ctable[2] <= DCB_MAX_NUM_I2C_ENTRIES)
			i2c_entries = i2ctable[2];
		else
			NV_WARN(dev,
				"DCB I2C table has more entries than indexable "
				"(%d entries, max index 15)\n", i2ctable[2]);
		entry_len = i2ctable[3];
		/* [4] is i2c_default_indices, read in parse_dcb_table() */
	}
	/*
	 * It's your own fault if you call this function on a DCB 1.1 BIOS --
	 * the test below is for DCB 1.2
	 */
	if (dcb_version < 0x14) {
		recordoffset = 2;
		rdofs = 0;
		wrofs = 1;
	}

	if (index == 0xf)
		return 0;
	if (index > i2c_entries) {
		NV_ERROR(dev, "DCB I2C index too big (%d > %d)\n",
			 index, i2ctable[2]);
		return -ENOENT;
	}
	if (i2ctable[headerlen + entry_len * index + 3] == 0xff) {
		NV_ERROR(dev, "DCB I2C entry invalid\n");
		return -EINVAL;
	}

	if (dcb_i2c_ver >= 0x30) {
		port_type = i2ctable[headerlen + recordoffset + 3 + entry_len * index];

		/*
		 * Fixup for chips using same address offset for read and
		 * write.
		 */
		if (port_type == 4)	/* seen on C51 */
			rdofs = wrofs = 1;
		if (port_type >= 5)	/* G80+ */
			rdofs = wrofs = 0;
	}

	if (dcb_i2c_ver >= 0x40 && port_type != 5 && port_type != 6)
		NV_WARN(dev, "DCB I2C table has port type %d\n", port_type);

	i2c->port_type = port_type;
	i2c->read = i2ctable[headerlen + recordoffset + rdofs + entry_len * index];
	i2c->write = i2ctable[headerlen + recordoffset + wrofs + entry_len * index];

	return 0;
}

static struct dcb_gpio_entry *
new_gpio_entry(struct nvbios *bios)
{
	struct parsed_dcb_gpio *gpio = &bios->bdcb.gpio;

	return &gpio->entry[gpio->entries++];
}

struct dcb_gpio_entry *
nouveau_bios_gpio_entry(struct drm_device *dev, enum dcb_gpio_tag tag)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;
	int i;

	for (i = 0; i < bios->bdcb.gpio.entries; i++) {
		if (bios->bdcb.gpio.entry[i].tag != tag)
			continue;

		return &bios->bdcb.gpio.entry[i];
	}

	return NULL;
}

static void
parse_dcb30_gpio_entry(struct nvbios *bios, uint16_t offset)
{
	struct dcb_gpio_entry *gpio;
	uint16_t ent = ROM16(bios->data[offset]);
	uint8_t line = ent & 0x1f,
		tag = ent >> 5 & 0x3f,
		flags = ent >> 11 & 0x1f;

	if (tag == 0x3f)
		return;

	gpio = new_gpio_entry(bios);

	gpio->tag = tag;
	gpio->line = line;
	gpio->invert = flags != 4;
}

static void
parse_dcb40_gpio_entry(struct nvbios *bios, uint16_t offset)
{
	struct dcb_gpio_entry *gpio;
	uint32_t ent = ROM32(bios->data[offset]);
	uint8_t line = ent & 0x1f,
		tag = ent >> 8 & 0xff;

	if (tag == 0xff)
		return;

	gpio = new_gpio_entry(bios);

	/* Currently unused, we may need more fields parsed at some
	 * point. */
	gpio->tag = tag;
	gpio->line = line;
}

static void
parse_dcb_gpio_table(struct nvbios *bios)
{
	struct drm_device *dev = bios->dev;
	uint16_t gpio_table_ptr = bios->bdcb.gpio_table_ptr;
	uint8_t *gpio_table = &bios->data[gpio_table_ptr];
	int header_len = gpio_table[1],
	    entries = gpio_table[2],
	    entry_len = gpio_table[3];
	void (*parse_entry)(struct nvbios *, uint16_t) = NULL;
	int i;

	if (bios->bdcb.version >= 0x40) {
		if (gpio_table_ptr && entry_len != 4) {
			NV_WARN(dev, "Invalid DCB GPIO table entry length.\n");
			return;
		}

		parse_entry = parse_dcb40_gpio_entry;

	} else if (bios->bdcb.version >= 0x30) {
		if (gpio_table_ptr && entry_len != 2) {
			NV_WARN(dev, "Invalid DCB GPIO table entry length.\n");
			return;
		}

		parse_entry = parse_dcb30_gpio_entry;

	} else if (bios->bdcb.version >= 0x22) {
		/*
		 * DCBs older than v3.0 don't really have a GPIO
		 * table, instead they keep some GPIO info at fixed
		 * locations.
		 */
		uint16_t dcbptr = ROM16(bios->data[0x36]);
		uint8_t *tvdac_gpio = &bios->data[dcbptr - 5];

		if (tvdac_gpio[0] & 1) {
			struct dcb_gpio_entry *gpio = new_gpio_entry(bios);

			gpio->tag = DCB_GPIO_TVDAC0;
			gpio->line = tvdac_gpio[1] >> 4;
			gpio->invert = tvdac_gpio[0] & 2;
		}
	}

	if (!gpio_table_ptr)
		return;

	if (entries > DCB_MAX_NUM_GPIO_ENTRIES) {
		NV_WARN(dev, "Too many entries in the DCB GPIO table.\n");
		entries = DCB_MAX_NUM_GPIO_ENTRIES;
	}

	for (i = 0; i < entries; i++)
		parse_entry(bios, gpio_table_ptr + header_len + entry_len * i);
}

struct dcb_connector_table_entry *
nouveau_bios_connector_entry(struct drm_device *dev, int index)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;
	struct dcb_connector_table_entry *cte;

	if (index >= bios->bdcb.connector.entries)
		return NULL;

	cte = &bios->bdcb.connector.entry[index];
	if (cte->type == 0xff)
		return NULL;

	return cte;
}

static void
parse_dcb_connector_table(struct nvbios *bios)
{
	struct drm_device *dev = bios->dev;
	struct dcb_connector_table *ct = &bios->bdcb.connector;
	struct dcb_connector_table_entry *cte;
	uint8_t *conntab = &bios->data[bios->bdcb.connector_table_ptr];
	uint8_t *entry;
	int i;

	if (!bios->bdcb.connector_table_ptr) {
		NV_DEBUG_KMS(dev, "No DCB connector table present\n");
		return;
	}

	NV_INFO(dev, "DCB connector table: VHER 0x%02x %d %d %d\n",
		conntab[0], conntab[1], conntab[2], conntab[3]);
	if ((conntab[0] != 0x30 && conntab[0] != 0x40) ||
	    (conntab[3] != 2 && conntab[3] != 4)) {
		NV_ERROR(dev, "  Unknown!  Please report.\n");
		return;
	}

	ct->entries = conntab[2];

	entry = conntab + conntab[1];
	cte = &ct->entry[0];
	for (i = 0; i < conntab[2]; i++, entry += conntab[3], cte++) {
		if (conntab[3] == 2)
			cte->entry = ROM16(entry[0]);
		else
			cte->entry = ROM32(entry[0]);
		cte->type  = (cte->entry & 0x000000ff) >> 0;
		cte->index = (cte->entry & 0x00000f00) >> 8;
		switch (cte->entry & 0x00033000) {
		case 0x00001000:
			cte->gpio_tag = 0x07;
			break;
		case 0x00002000:
			cte->gpio_tag = 0x08;
			break;
		case 0x00010000:
			cte->gpio_tag = 0x51;
			break;
		case 0x00020000:
			cte->gpio_tag = 0x52;
			break;
		default:
			cte->gpio_tag = 0xff;
			break;
		}

		if (cte->type == 0xff)
			continue;

		NV_INFO(dev, "  %d: 0x%08x: type 0x%02x idx %d tag 0x%02x\n",
			i, cte->entry, cte->type, cte->index, cte->gpio_tag);
	}
}

static struct dcb_entry *new_dcb_entry(struct parsed_dcb *dcb)
{
	struct dcb_entry *entry = &dcb->entry[dcb->entries];

	memset(entry, 0, sizeof(struct dcb_entry));
	entry->index = dcb->entries++;

	return entry;
}

static void fabricate_vga_output(struct parsed_dcb *dcb, int i2c, int heads)
{
	struct dcb_entry *entry = new_dcb_entry(dcb);

	entry->type = 0;
	entry->i2c_index = i2c;
	entry->heads = heads;
	entry->location = DCB_LOC_ON_CHIP;
	/* "or" mostly unused in early gen crt modesetting, 0 is fine */
}

static void fabricate_dvi_i_output(struct parsed_dcb *dcb, bool twoHeads)
{
	struct dcb_entry *entry = new_dcb_entry(dcb);

	entry->type = 2;
	entry->i2c_index = LEGACY_I2C_PANEL;
	entry->heads = twoHeads ? 3 : 1;
	entry->location = !DCB_LOC_ON_CHIP;	/* ie OFF CHIP */
	entry->or = 1;	/* means |0x10 gets set on CRE_LCD__INDEX */
	entry->duallink_possible = false; /* SiI164 and co. are single link */

#if 0
	/*
	 * For dvi-a either crtc probably works, but my card appears to only
	 * support dvi-d.  "nvidia" still attempts to program it for dvi-a,
	 * doing the full fp output setup (program 0x6808.. fp dimension regs,
	 * setting 0x680848 to 0x10000111 to enable, maybe setting 0x680880);
	 * the monitor picks up the mode res ok and lights up, but no pixel
	 * data appears, so the board manufacturer probably connected up the
	 * sync lines, but missed the video traces / components
	 *
	 * with this introduction, dvi-a left as an exercise for the reader.
	 */
	fabricate_vga_output(dcb, LEGACY_I2C_PANEL, entry->heads);
#endif
}

static void fabricate_tv_output(struct parsed_dcb *dcb, bool twoHeads)
{
	struct dcb_entry *entry = new_dcb_entry(dcb);

	entry->type = 1;
	entry->i2c_index = LEGACY_I2C_TV;
	entry->heads = twoHeads ? 3 : 1;
	entry->location = !DCB_LOC_ON_CHIP;	/* ie OFF CHIP */
}

static bool
parse_dcb20_entry(struct drm_device *dev, struct bios_parsed_dcb *bdcb,
		  uint32_t conn, uint32_t conf, struct dcb_entry *entry)
{
	entry->type = conn & 0xf;
	entry->i2c_index = (conn >> 4) & 0xf;
	entry->heads = (conn >> 8) & 0xf;
	if (bdcb->version >= 0x40)
		entry->connector = (conn >> 12) & 0xf;
	entry->bus = (conn >> 16) & 0xf;
	entry->location = (conn >> 20) & 0x3;
	entry->or = (conn >> 24) & 0xf;
	/*
	 * Normal entries consist of a single bit, but dual link has the
	 * next most significant bit set too
	 */
	entry->duallink_possible =
			((1 << (ffs(entry->or) - 1)) * 3 == entry->or);

	switch (entry->type) {
	case OUTPUT_ANALOG:
		/*
		 * Although the rest of a CRT conf dword is usually
		 * zeros, mac biosen have stuff there so we must mask
		 */
		entry->crtconf.maxfreq = (bdcb->version < 0x30) ?
					 (conf & 0xffff) * 10 :
					 (conf & 0xff) * 10000;
		break;
	case OUTPUT_LVDS:
		{
		uint32_t mask;
		if (conf & 0x1)
			entry->lvdsconf.use_straps_for_mode = true;
		if (bdcb->version < 0x22) {
			mask = ~0xd;
			/*
			 * The laptop in bug 14567 lies and claims to not use
			 * straps when it does, so assume all DCB 2.0 laptops
			 * use straps, until a broken EDID using one is produced
			 */
			entry->lvdsconf.use_straps_for_mode = true;
			/*
			 * Both 0x4 and 0x8 show up in v2.0 tables; assume they
			 * mean the same thing (probably wrong, but might work)
			 */
			if (conf & 0x4 || conf & 0x8)
				entry->lvdsconf.use_power_scripts = true;
		} else {
			mask = ~0x5;
			if (conf & 0x4)
				entry->lvdsconf.use_power_scripts = true;
		}
		if (conf & mask) {
			/*
			 * Until we even try to use these on G8x, it's
			 * useless reporting unknown bits.  They all are.
			 */
			if (bdcb->version >= 0x40)
				break;

			NV_ERROR(dev, "Unknown LVDS configuration bits, "
				      "please report\n");
		}
		break;
		}
	case OUTPUT_TV:
	{
		if (bdcb->version >= 0x30)
			entry->tvconf.has_component_output = conf & (0x8 << 4);
		else
			entry->tvconf.has_component_output = false;

		break;
	}
	case OUTPUT_DP:
		entry->dpconf.sor.link = (conf & 0x00000030) >> 4;
		entry->dpconf.link_bw = (conf & 0x00e00000) >> 21;
		switch ((conf & 0x0f000000) >> 24) {
		case 0xf:
			entry->dpconf.link_nr = 4;
			break;
		case 0x3:
			entry->dpconf.link_nr = 2;
			break;
		default:
			entry->dpconf.link_nr = 1;
			break;
		}
		break;
	case OUTPUT_TMDS:
		entry->tmdsconf.sor.link = (conf & 0x00000030) >> 4;
		break;
	case 0xe:
		/* weird g80 mobile type that "nv" treats as a terminator */
		bdcb->dcb.entries--;
		return false;
	}

	/* unsure what DCB version introduces this, 3.0? */
	if (conf & 0x100000)
		entry->i2c_upper_default = true;

	return true;
}

static bool
parse_dcb15_entry(struct drm_device *dev, struct parsed_dcb *dcb,
		  uint32_t conn, uint32_t conf, struct dcb_entry *entry)
{
	if (conn != 0xf0003f00 && conn != 0xf2247f10 && conn != 0xf2204001 &&
	    conn != 0xf2204301 && conn != 0xf2204311 && conn != 0xf2208001 &&
	    conn != 0xf2244001 && conn != 0xf2244301 && conn != 0xf2244311 &&
	    conn != 0xf4204011 && conn != 0xf4208011 && conn != 0xf4248011 &&
	    conn != 0xf2045ff2 && conn != 0xf2045f14 && conn != 0xf207df14 &&
	    conn != 0xf2205004 && conn != 0xf2209004) {
		NV_ERROR(dev, "Unknown DCB 1.5 entry, please report\n");

		/* cause output setting to fail for !TV, so message is seen */
		if ((conn & 0xf) != 0x1)
			dcb->entries = 0;

		return false;
	}
	/* most of the below is a "best guess" atm */
	entry->type = conn & 0xf;
	if (entry->type == 2)
		/* another way of specifying straps based lvds... */
		entry->type = OUTPUT_LVDS;
	if (entry->type == 4) { /* digital */
		if (conn & 0x10)
			entry->type = OUTPUT_LVDS;
		else
			entry->type = OUTPUT_TMDS;
	}
	/* what's in bits 5-13? could be some encoder maker thing, in tv case */
	entry->i2c_index = (conn >> 14) & 0xf;
	/* raw heads field is in range 0-1, so move to 1-2 */
	entry->heads = ((conn >> 18) & 0x7) + 1;
	entry->location = (conn >> 21) & 0xf;
	/* unused: entry->bus = (conn >> 25) & 0x7; */
	/* set or to be same as heads -- hopefully safe enough */
	entry->or = entry->heads;
	entry->duallink_possible = false;

	switch (entry->type) {
	case OUTPUT_ANALOG:
		entry->crtconf.maxfreq = (conf & 0xffff) * 10;
		break;
	case OUTPUT_LVDS:
		/*
		 * This is probably buried in conn's unknown bits.
		 * This will upset EDID-ful models, if they exist
		 */
		entry->lvdsconf.use_straps_for_mode = true;
		entry->lvdsconf.use_power_scripts = true;
		break;
	case OUTPUT_TMDS:
		/*
		 * Invent a DVI-A output, by copying the fields of the DVI-D
		 * output; reported to work by math_b on an NV20(!).
		 */
		fabricate_vga_output(dcb, entry->i2c_index, entry->heads);
		break;
	case OUTPUT_TV:
		entry->tvconf.has_component_output = false;
		break;
	}

	return true;
}

static bool parse_dcb_entry(struct drm_device *dev, struct bios_parsed_dcb *bdcb,
			    uint32_t conn, uint32_t conf)
{
	struct dcb_entry *entry = new_dcb_entry(&bdcb->dcb);
	bool ret;

	if (bdcb->version >= 0x20)
		ret = parse_dcb20_entry(dev, bdcb, conn, conf, entry);
	else
		ret = parse_dcb15_entry(dev, &bdcb->dcb, conn, conf, entry);
	if (!ret)
		return ret;

	read_dcb_i2c_entry(dev, bdcb->version, bdcb->i2c_table,
			   entry->i2c_index, &bdcb->dcb.i2c[entry->i2c_index]);

	return true;
}

static
void merge_like_dcb_entries(struct drm_device *dev, struct parsed_dcb *dcb)
{
	/*
	 * DCB v2.0 lists each output combination separately.
	 * Here we merge compatible entries to have fewer outputs, with
	 * more options
	 */

	int i, newentries = 0;

	for (i = 0; i < dcb->entries; i++) {
		struct dcb_entry *ient = &dcb->entry[i];
		int j;

		for (j = i + 1; j < dcb->entries; j++) {
			struct dcb_entry *jent = &dcb->entry[j];

			if (jent->type == 100) /* already merged entry */
				continue;

			/* merge heads field when all other fields the same */
			if (jent->i2c_index == ient->i2c_index &&
			    jent->type == ient->type &&
			    jent->location == ient->location &&
			    jent->or == ient->or) {
				NV_TRACE(dev, "Merging DCB entries %d and %d\n",
					 i, j);
				ient->heads |= jent->heads;
				jent->type = 100; /* dummy value */
			}
		}
	}

	/* Compact entries merged into others out of dcb */
	for (i = 0; i < dcb->entries; i++) {
		if (dcb->entry[i].type == 100)
			continue;

		if (newentries != i) {
			dcb->entry[newentries] = dcb->entry[i];
			dcb->entry[newentries].index = newentries;
		}
		newentries++;
	}

	dcb->entries = newentries;
}

static int parse_dcb_table(struct drm_device *dev, struct nvbios *bios, bool twoHeads)
{
	struct bios_parsed_dcb *bdcb = &bios->bdcb;
	struct parsed_dcb *dcb;
	uint16_t dcbptr, i2ctabptr = 0;
	uint8_t *dcbtable;
	uint8_t headerlen = 0x4, entries = DCB_MAX_NUM_ENTRIES;
	bool configblock = true;
	int recordlength = 8, confofs = 4;
	int i;

	dcb = bios->pub.dcb = &bdcb->dcb;
	dcb->entries = 0;

	/* get the offset from 0x36 */
	dcbptr = ROM16(bios->data[0x36]);

	if (dcbptr == 0x0) {
		NV_WARN(dev, "No output data (DCB) found in BIOS, "
			       "assuming a CRT output exists\n");
		/* this situation likely means a really old card, pre DCB */
		fabricate_vga_output(dcb, LEGACY_I2C_CRT, 1);

		if (nv04_tv_identify(dev,
				     bios->legacy.i2c_indices.tv) >= 0)
			fabricate_tv_output(dcb, twoHeads);

		return 0;
	}

	dcbtable = &bios->data[dcbptr];

	/* get DCB version */
	bdcb->version = dcbtable[0];
	NV_TRACE(dev, "Found Display Configuration Block version %d.%d\n",
		 bdcb->version >> 4, bdcb->version & 0xf);

	if (bdcb->version >= 0x20) { /* NV17+ */
		uint32_t sig;

		if (bdcb->version >= 0x30) { /* NV40+ */
			headerlen = dcbtable[1];
			entries = dcbtable[2];
			recordlength = dcbtable[3];
			i2ctabptr = ROM16(dcbtable[4]);
			sig = ROM32(dcbtable[6]);
			bdcb->gpio_table_ptr = ROM16(dcbtable[10]);
			bdcb->connector_table_ptr = ROM16(dcbtable[20]);
		} else {
			i2ctabptr = ROM16(dcbtable[2]);
			sig = ROM32(dcbtable[4]);
			headerlen = 8;
		}

		if (sig != 0x4edcbdcb) {
			NV_ERROR(dev, "Bad Display Configuration Block "
					"signature (%08X)\n", sig);
			return -EINVAL;
		}
	} else if (bdcb->version >= 0x15) { /* some NV11 and NV20 */
		char sig[8] = { 0 };

		strncpy(sig, (char *)&dcbtable[-7], 7);
		i2ctabptr = ROM16(dcbtable[2]);
		recordlength = 10;
		confofs = 6;

		if (strcmp(sig, "DEV_REC")) {
			NV_ERROR(dev, "Bad Display Configuration Block "
					"signature (%s)\n", sig);
			return -EINVAL;
		}
	} else {
		/*
		 * v1.4 (some NV15/16, NV11+) seems the same as v1.5, but always
		 * has the same single (crt) entry, even when tv-out present, so
		 * the conclusion is this version cannot really be used.
		 * v1.2 tables (some NV6/10, and NV15+) normally have the same
		 * 5 entries, which are not specific to the card and so no use.
		 * v1.2 does have an I2C table that read_dcb_i2c_table can
		 * handle, but cards exist (nv11 in #14821) with a bad i2c table
		 * pointer, so use the indices parsed in parse_bmp_structure.
		 * v1.1 (NV5+, maybe some NV4) is entirely unhelpful
		 */
		NV_TRACEWARN(dev, "No useful information in BIOS output table; "
				  "adding all possible outputs\n");
		fabricate_vga_output(dcb, LEGACY_I2C_CRT, 1);

		/*
		 * Attempt to detect TV before DVI because the test
		 * for the former is more accurate and it rules the
		 * latter out.
		 */
		if (nv04_tv_identify(dev,
				     bios->legacy.i2c_indices.tv) >= 0)
			fabricate_tv_output(dcb, twoHeads);

		else if (bios->tmds.output0_script_ptr ||
			 bios->tmds.output1_script_ptr)
			fabricate_dvi_i_output(dcb, twoHeads);

		return 0;
	}

	if (!i2ctabptr)
		NV_WARN(dev, "No pointer to DCB I2C port table\n");
	else {
		bdcb->i2c_table = &bios->data[i2ctabptr];
		if (bdcb->version >= 0x30)
			bdcb->i2c_default_indices = bdcb->i2c_table[4];
	}

	parse_dcb_gpio_table(bios);
	parse_dcb_connector_table(bios);

	if (entries > DCB_MAX_NUM_ENTRIES)
		entries = DCB_MAX_NUM_ENTRIES;

	for (i = 0; i < entries; i++) {
		uint32_t connection, config = 0;

		connection = ROM32(dcbtable[headerlen + recordlength * i]);
		if (configblock)
			config = ROM32(dcbtable[headerlen + confofs + recordlength * i]);

		/* seen on an NV11 with DCB v1.5 */
		if (connection == 0x00000000)
			break;

		/* seen on an NV17 with DCB v2.0 */
		if (connection == 0xffffffff)
			break;

		if ((connection & 0x0000000f) == 0x0000000f)
			continue;

		NV_TRACEWARN(dev, "Raw DCB entry %d: %08x %08x\n",
			     dcb->entries, connection, config);

		if (!parse_dcb_entry(dev, bdcb, connection, config))
			break;
	}

	/*
	 * apart for v2.1+ not being known for requiring merging, this
	 * guarantees dcbent->index is the index of the entry in the rom image
	 */
	if (bdcb->version < 0x21)
		merge_like_dcb_entries(dev, dcb);

	return dcb->entries ? 0 : -ENXIO;
}

static void
fixup_legacy_connector(struct nvbios *bios)
{
	struct bios_parsed_dcb *bdcb = &bios->bdcb;
	struct parsed_dcb *dcb = &bdcb->dcb;
	int high = 0, i;

	/*
	 * DCB 3.0 also has the table in most cases, but there are some cards
	 * where the table is filled with stub entries, and the DCB entriy
	 * indices are all 0.  We don't need the connector indices on pre-G80
	 * chips (yet?) so limit the use to DCB 4.0 and above.
	 */
	if (bdcb->version >= 0x40)
		return;

	/*
	 * No known connector info before v3.0, so make it up.  the rule here
	 * is: anything on the same i2c bus is considered to be on the same
	 * connector.  any output without an associated i2c bus is assigned
	 * its own unique connector index.
	 */
	for (i = 0; i < dcb->entries; i++) {
		if (dcb->entry[i].i2c_index == 0xf)
			continue;

		/*
		 * Ignore the I2C index for on-chip TV-out, as there
		 * are cards with bogus values (nv31m in bug 23212),
		 * and it's otherwise useless.
		 */
		if (dcb->entry[i].type == OUTPUT_TV &&
		    dcb->entry[i].location == DCB_LOC_ON_CHIP) {
			dcb->entry[i].i2c_index = 0xf;
			continue;
		}

		dcb->entry[i].connector = dcb->entry[i].i2c_index;
		if (dcb->entry[i].connector > high)
			high = dcb->entry[i].connector;
	}

	for (i = 0; i < dcb->entries; i++) {
		if (dcb->entry[i].i2c_index != 0xf)
			continue;

		dcb->entry[i].connector = ++high;
	}
}

static void
fixup_legacy_i2c(struct nvbios *bios)
{
	struct parsed_dcb *dcb = &bios->bdcb.dcb;
	int i;

	for (i = 0; i < dcb->entries; i++) {
		if (dcb->entry[i].i2c_index == LEGACY_I2C_CRT)
			dcb->entry[i].i2c_index = bios->legacy.i2c_indices.crt;
		if (dcb->entry[i].i2c_index == LEGACY_I2C_PANEL)
			dcb->entry[i].i2c_index = bios->legacy.i2c_indices.panel;
		if (dcb->entry[i].i2c_index == LEGACY_I2C_TV)
			dcb->entry[i].i2c_index = bios->legacy.i2c_indices.tv;
	}
}

static int load_nv17_hwsq_ucode_entry(struct drm_device *dev, struct nvbios *bios, uint16_t hwsq_offset, int entry)
{
	/*
	 * The header following the "HWSQ" signature has the number of entries,
	 * and the entry size
	 *
	 * An entry consists of a dword to write to the sequencer control reg
	 * (0x00001304), followed by the ucode bytes, written sequentially,
	 * starting at reg 0x00001400
	 */

	uint8_t bytes_to_write;
	uint16_t hwsq_entry_offset;
	int i;

	if (bios->data[hwsq_offset] <= entry) {
		NV_ERROR(dev, "Too few entries in HW sequencer table for "
				"requested entry\n");
		return -ENOENT;
	}

	bytes_to_write = bios->data[hwsq_offset + 1];

	if (bytes_to_write != 36) {
		NV_ERROR(dev, "Unknown HW sequencer entry size\n");
		return -EINVAL;
	}

	NV_TRACE(dev, "Loading NV17 power sequencing microcode\n");

	hwsq_entry_offset = hwsq_offset + 2 + entry * bytes_to_write;

	/* set sequencer control */
	bios_wr32(bios, 0x00001304, ROM32(bios->data[hwsq_entry_offset]));
	bytes_to_write -= 4;

	/* write ucode */
	for (i = 0; i < bytes_to_write; i += 4)
		bios_wr32(bios, 0x00001400 + i, ROM32(bios->data[hwsq_entry_offset + i + 4]));

	/* twiddle NV_PBUS_DEBUG_4 */
	bios_wr32(bios, NV_PBUS_DEBUG_4, bios_rd32(bios, NV_PBUS_DEBUG_4) | 0x18);

	return 0;
}

static int load_nv17_hw_sequencer_ucode(struct drm_device *dev,
					struct nvbios *bios)
{
	/*
	 * BMP based cards, from NV17, need a microcode loading to correctly
	 * control the GPIO etc for LVDS panels
	 *
	 * BIT based cards seem to do this directly in the init scripts
	 *
	 * The microcode entries are found by the "HWSQ" signature.
	 */

	const uint8_t hwsq_signature[] = { 'H', 'W', 'S', 'Q' };
	const int sz = sizeof(hwsq_signature);
	int hwsq_offset;

	hwsq_offset = findstr(bios->data, bios->length, hwsq_signature, sz);
	if (!hwsq_offset)
		return 0;

	/* always use entry 0? */
	return load_nv17_hwsq_ucode_entry(dev, bios, hwsq_offset + sz, 0);
}

uint8_t *nouveau_bios_embedded_edid(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;
	const uint8_t edid_sig[] = {
			0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };
	uint16_t offset = 0;
	uint16_t newoffset;
	int searchlen = NV_PROM_SIZE;

	if (bios->fp.edid)
		return bios->fp.edid;

	while (searchlen) {
		newoffset = findstr(&bios->data[offset], searchlen,
								edid_sig, 8);
		if (!newoffset)
			return NULL;
		offset += newoffset;
		if (!nv_cksum(&bios->data[offset], EDID1_LEN))
			break;

		searchlen -= offset;
		offset++;
	}

	NV_TRACE(dev, "Found EDID in BIOS\n");

	return bios->fp.edid = &bios->data[offset];
}

void
nouveau_bios_run_init_table(struct drm_device *dev, uint16_t table,
			    struct dcb_entry *dcbent)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;
	struct init_exec iexec = { true, false };

	bios->display.output = dcbent;
	parse_init_table(bios, table, &iexec);
	bios->display.output = NULL;
}

static bool NVInitVBIOS(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;

	memset(bios, 0, sizeof(struct nvbios));
	bios->dev = dev;

	if (!NVShadowVBIOS(dev, bios->data))
		return false;

	bios->length = NV_PROM_SIZE;
	return true;
}

static int nouveau_parse_vbios_struct(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;
	const uint8_t bit_signature[] = { 0xff, 0xb8, 'B', 'I', 'T' };
	const uint8_t bmp_signature[] = { 0xff, 0x7f, 'N', 'V', 0x0 };
	int offset;

	offset = findstr(bios->data, bios->length,
					bit_signature, sizeof(bit_signature));
	if (offset) {
		NV_TRACE(dev, "BIT BIOS found\n");
		return parse_bit_structure(bios, offset + 6);
	}

	offset = findstr(bios->data, bios->length,
					bmp_signature, sizeof(bmp_signature));
	if (offset) {
		NV_TRACE(dev, "BMP BIOS found\n");
		return parse_bmp_structure(dev, bios, offset);
	}

	NV_ERROR(dev, "No known BIOS signature found\n");
	return -ENODEV;
}

int
nouveau_run_vbios_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;
	int i, ret = 0;

	NVLockVgaCrtcs(dev, false);
	if (nv_two_heads(dev))
		NVSetOwner(dev, bios->state.crtchead);

	if (bios->major_version < 5)	/* BMP only */
		load_nv17_hw_sequencer_ucode(dev, bios);

	if (bios->execute) {
		bios->fp.last_script_invoc = 0;
		bios->fp.lvds_init_run = false;
	}

	parse_init_tables(bios);

	/*
	 * Runs some additional script seen on G8x VBIOSen.  The VBIOS'
	 * parser will run this right after the init tables, the binary
	 * driver appears to run it at some point later.
	 */
	if (bios->some_script_ptr) {
		struct init_exec iexec = {true, false};

		NV_INFO(dev, "Parsing VBIOS init table at offset 0x%04X\n",
			bios->some_script_ptr);
		parse_init_table(bios, bios->some_script_ptr, &iexec);
	}

	if (dev_priv->card_type >= NV_50) {
		for (i = 0; i < bios->bdcb.dcb.entries; i++) {
			nouveau_bios_run_display_table(dev,
						       &bios->bdcb.dcb.entry[i],
						       0, 0);
		}
	}

	NVLockVgaCrtcs(dev, true);

	return ret;
}

static void
nouveau_bios_i2c_devices_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;
	struct dcb_i2c_entry *entry;
	int i;

	entry = &bios->bdcb.dcb.i2c[0];
	for (i = 0; i < DCB_MAX_NUM_I2C_ENTRIES; i++, entry++)
		nouveau_i2c_fini(dev, entry);
}

int
nouveau_bios_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;
	uint32_t saved_nv_pextdev_boot_0;
	bool was_locked;
	int ret;

	dev_priv->vbios = &bios->pub;

	if (!NVInitVBIOS(dev))
		return -ENODEV;

	ret = nouveau_parse_vbios_struct(dev);
	if (ret)
		return ret;

	ret = parse_dcb_table(dev, bios, nv_two_heads(dev));
	if (ret)
		return ret;

	fixup_legacy_i2c(bios);
	fixup_legacy_connector(bios);

	if (!bios->major_version)	/* we don't run version 0 bios */
		return 0;

	/* these will need remembering across a suspend */
	saved_nv_pextdev_boot_0 = bios_rd32(bios, NV_PEXTDEV_BOOT_0);
	bios->state.saved_nv_pfb_cfg0 = bios_rd32(bios, NV_PFB_CFG0);

	/* init script execution disabled */
	bios->execute = false;

	/* ... unless card isn't POSTed already */
	if (dev_priv->card_type >= NV_10 &&
	    NVReadVgaCrtc(dev, 0, 0x00) == 0 &&
	    NVReadVgaCrtc(dev, 0, 0x1a) == 0) {
		NV_INFO(dev, "Adaptor not initialised\n");
		if (dev_priv->card_type < NV_50) {
			NV_ERROR(dev, "Unable to POST this chipset\n");
			return -ENODEV;
		}

		NV_INFO(dev, "Running VBIOS init tables\n");
		bios->execute = true;
	}

	bios_wr32(bios, NV_PEXTDEV_BOOT_0, saved_nv_pextdev_boot_0);

	ret = nouveau_run_vbios_init(dev);
	if (ret) {
		dev_priv->vbios = NULL;
		return ret;
	}

	/* feature_byte on BMP is poor, but init always sets CR4B */
	was_locked = NVLockVgaCrtcs(dev, false);
	if (bios->major_version < 5)
		bios->is_mobile = NVReadVgaCrtc(dev, 0, NV_CIO_CRE_4B) & 0x40;

	/* all BIT systems need p_f_m_t for digital_min_front_porch */
	if (bios->is_mobile || bios->major_version >= 5)
		ret = parse_fp_mode_table(dev, bios);
	NVLockVgaCrtcs(dev, was_locked);

	/* allow subsequent scripts to execute */
	bios->execute = true;

	return 0;
}

void
nouveau_bios_takedown(struct drm_device *dev)
{
	nouveau_bios_i2c_devices_takedown(dev);
}
