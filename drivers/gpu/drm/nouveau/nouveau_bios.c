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
#include "nouveau_encoder.h"
#include "nouveau_gpio.h"

#include <linux/io-mapping.h>

/* these defines are made up */
#define NV_CIO_CRE_44_HEADA 0x0
#define NV_CIO_CRE_44_HEADB 0x3
#define FEATURE_MOBILE 0x10	/* also FEATURE_QUADRO for BMP */

#define EDID1_LEN 128

#define BIOSLOG(sip, fmt, arg...) NV_DEBUG(sip->dev, fmt, ##arg)
#define LOG_OLD_VALUE(x)

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
score_vbios(struct nvbios *bios, const bool writeable)
{
	if (!bios->data || bios->data[0] != 0x55 || bios->data[1] != 0xAA) {
		NV_TRACEWARN(bios->dev, "... BIOS signature not found\n");
		return 0;
	}

	if (nv_cksum(bios->data, bios->data[2] * 512)) {
		NV_TRACEWARN(bios->dev, "... BIOS checksum invalid\n");
		/* if a ro image is somewhat bad, it's probably all rubbish */
		return writeable ? 2 : 1;
	}

	NV_TRACE(bios->dev, "... appears to be valid\n");
	return 3;
}

static void
bios_shadow_prom(struct nvbios *bios)
{
	struct drm_device *dev = bios->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u32 pcireg, access;
	u16 pcir;
	int i;

	/* enable access to rom */
	if (dev_priv->card_type >= NV_50)
		pcireg = 0x088050;
	else
		pcireg = NV_PBUS_PCI_NV_20;
	access = nv_mask(dev, pcireg, 0x00000001, 0x00000000);

	/* bail if no rom signature, with a workaround for a PROM reading
	 * issue on some chipsets.  the first read after a period of
	 * inactivity returns the wrong result, so retry the first header
	 * byte a few times before giving up as a workaround
	 */
	i = 16;
	do {
		if (nv_rd08(dev, NV_PROM_OFFSET + 0) == 0x55)
			break;
	} while (i--);

	if (!i || nv_rd08(dev, NV_PROM_OFFSET + 1) != 0xaa)
		goto out;

	/* additional check (see note below) - read PCI record header */
	pcir = nv_rd08(dev, NV_PROM_OFFSET + 0x18) |
	       nv_rd08(dev, NV_PROM_OFFSET + 0x19) << 8;
	if (nv_rd08(dev, NV_PROM_OFFSET + pcir + 0) != 'P' ||
	    nv_rd08(dev, NV_PROM_OFFSET + pcir + 1) != 'C' ||
	    nv_rd08(dev, NV_PROM_OFFSET + pcir + 2) != 'I' ||
	    nv_rd08(dev, NV_PROM_OFFSET + pcir + 3) != 'R')
		goto out;

	/* read entire bios image to system memory */
	bios->length = nv_rd08(dev, NV_PROM_OFFSET + 2) * 512;
	bios->data = kmalloc(bios->length, GFP_KERNEL);
	if (bios->data) {
		for (i = 0; i < bios->length; i++)
			bios->data[i] = nv_rd08(dev, NV_PROM_OFFSET + i);
	}

out:
	/* disable access to rom */
	nv_wr32(dev, pcireg, access);
}

static void
bios_shadow_pramin(struct nvbios *bios)
{
	struct drm_device *dev = bios->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u32 bar0 = 0;
	int i;

	if (dev_priv->card_type >= NV_50) {
		u64 addr = (u64)(nv_rd32(dev, 0x619f04) & 0xffffff00) << 8;
		if (!addr) {
			addr  = (u64)nv_rd32(dev, 0x001700) << 16;
			addr += 0xf0000;
		}

		bar0 = nv_mask(dev, 0x001700, 0xffffffff, addr >> 16);
	}

	/* bail if no rom signature */
	if (nv_rd08(dev, NV_PRAMIN_OFFSET + 0) != 0x55 ||
	    nv_rd08(dev, NV_PRAMIN_OFFSET + 1) != 0xaa)
		goto out;

	bios->length = nv_rd08(dev, NV_PRAMIN_OFFSET + 2) * 512;
	bios->data = kmalloc(bios->length, GFP_KERNEL);
	if (bios->data) {
		for (i = 0; i < bios->length; i++)
			bios->data[i] = nv_rd08(dev, NV_PRAMIN_OFFSET + i);
	}

out:
	if (dev_priv->card_type >= NV_50)
		nv_wr32(dev, 0x001700, bar0);
}

static void
bios_shadow_pci(struct nvbios *bios)
{
	struct pci_dev *pdev = bios->dev->pdev;
	size_t length;

	if (!pci_enable_rom(pdev)) {
		void __iomem *rom = pci_map_rom(pdev, &length);
		if (rom && length) {
			bios->data = kmalloc(length, GFP_KERNEL);
			if (bios->data) {
				memcpy_fromio(bios->data, rom, length);
				bios->length = length;
			}
		}
		if (rom)
			pci_unmap_rom(pdev, rom);

		pci_disable_rom(pdev);
	}
}

static void
bios_shadow_acpi(struct nvbios *bios)
{
	struct pci_dev *pdev = bios->dev->pdev;
	int ptr, len, ret;
	u8 data[3];

	if (!nouveau_acpi_rom_supported(pdev))
		return;

	ret = nouveau_acpi_get_bios_chunk(data, 0, sizeof(data));
	if (ret != sizeof(data))
		return;

	bios->length = min(data[2] * 512, 65536);
	bios->data = kmalloc(bios->length, GFP_KERNEL);
	if (!bios->data)
		return;

	len = bios->length;
	ptr = 0;
	while (len) {
		int size = (len > ROM_BIOS_PAGE) ? ROM_BIOS_PAGE : len;

		ret = nouveau_acpi_get_bios_chunk(bios->data, ptr, size);
		if (ret != size) {
			kfree(bios->data);
			bios->data = NULL;
			return;
		}

		len -= size;
		ptr += size;
	}
}

struct methods {
	const char desc[8];
	void (*shadow)(struct nvbios *);
	const bool rw;
	int score;
	u32 size;
	u8 *data;
};

static bool
bios_shadow(struct drm_device *dev)
{
	struct methods shadow_methods[] = {
		{ "PRAMIN", bios_shadow_pramin, true, 0, 0, NULL },
		{ "PROM", bios_shadow_prom, false, 0, 0, NULL },
		{ "ACPI", bios_shadow_acpi, true, 0, 0, NULL },
		{ "PCIROM", bios_shadow_pci, true, 0, 0, NULL },
		{}
	};
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->vbios;
	struct methods *mthd, *best;

	if (nouveau_vbios) {
		mthd = shadow_methods;
		do {
			if (strcasecmp(nouveau_vbios, mthd->desc))
				continue;
			NV_INFO(dev, "VBIOS source: %s\n", mthd->desc);

			mthd->shadow(bios);
			mthd->score = score_vbios(bios, mthd->rw);
			if (mthd->score)
				return true;
		} while ((++mthd)->shadow);

		NV_ERROR(dev, "VBIOS source \'%s\' invalid\n", nouveau_vbios);
	}

	mthd = shadow_methods;
	do {
		NV_TRACE(dev, "Checking %s for VBIOS\n", mthd->desc);
		mthd->shadow(bios);
		mthd->score = score_vbios(bios, mthd->rw);
		mthd->size = bios->length;
		mthd->data = bios->data;
	} while (mthd->score != 3 && (++mthd)->shadow);

	mthd = shadow_methods;
	best = mthd;
	do {
		if (mthd->score > best->score) {
			kfree(best->data);
			best = mthd;
		}
	} while ((++mthd)->shadow);

	if (best->score) {
		NV_TRACE(dev, "Using VBIOS from %s\n", best->desc);
		bios->length = best->size;
		bios->data = best->data;
		return true;
	}

	NV_ERROR(dev, "No valid VBIOS image found\n");
	return false;
}

struct init_tbl_entry {
	char *name;
	uint8_t id;
	/* Return:
	 *  > 0: success, length of opcode
	 *    0: success, but abort further parsing of table (INIT_DONE etc)
	 *  < 0: failure, table parsing will be aborted
	 */
	int (*handler)(struct nvbios *, uint16_t, struct init_exec *);
};

static int parse_init_table(struct nvbios *, uint16_t, struct init_exec *);

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
	mdelay(2);
#endif
}

static uint32_t
munge_reg(struct nvbios *bios, uint32_t reg)
{
	struct drm_nouveau_private *dev_priv = bios->dev->dev_private;
	struct dcb_entry *dcbent = bios->display.output;

	if (dev_priv->card_type < NV_50)
		return reg;

	if (reg & 0x80000000) {
		BUG_ON(bios->display.crtc < 0);
		reg += bios->display.crtc * 0x800;
	}

	if (reg & 0x40000000) {
		BUG_ON(!dcbent);

		reg += (ffs(dcbent->or) - 1) * 0x800;
		if ((reg & 0x20000000) && !(dcbent->sorconf.link & 1))
			reg += 0x00000080;
	}

	reg &= ~0xe0000000;
	return reg;
}

static int
valid_reg(struct nvbios *bios, uint32_t reg)
{
	struct drm_nouveau_private *dev_priv = bios->dev->dev_private;
	struct drm_device *dev = bios->dev;

	/* C51 has misaligned regs on purpose. Marvellous */
	if (reg & 0x2 ||
	    (reg & 0x1 && dev_priv->vbios.chip_version != 0x51))
		NV_ERROR(dev, "======= misaligned reg 0x%08X =======\n", reg);

	/* warn on C51 regs that haven't been verified accessible in tracing */
	if (reg & 0x1 && dev_priv->vbios.chip_version == 0x51 &&
	    reg != 0x130d && reg != 0x1311 && reg != 0x60081d)
		NV_WARN(dev, "=== C51 misaligned reg 0x%08X not verified ===\n",
			reg);

	if (reg >= (8*1024*1024)) {
		NV_ERROR(dev, "=== reg 0x%08x out of mapped bounds ===\n", reg);
		return 0;
	}

	return 1;
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

	if (dev_priv->vbios.execute) {
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
	struct nouveau_pll_vals pll;
	struct pll_lims pll_limits;
	u32 ctrl, mask, coef;
	int ret;

	ret = get_pll_limits(dev, reg, &pll_limits);
	if (ret)
		return ret;

	clk = nouveau_calc_pll_mnp(dev, &pll_limits, clk, &pll);
	if (!clk)
		return -ERANGE;

	coef = pll.N1 << 8 | pll.M1;
	ctrl = pll.log2P << 16;
	mask = 0x00070000;
	if (reg == 0x004008) {
		mask |= 0x01f80000;
		ctrl |= (pll_limits.log2p_bias << 19);
		ctrl |= (pll.log2P << 22);
	}

	if (!dev_priv->vbios.execute)
		return 0;

	nv_mask(dev, reg + 0, mask, ctrl);
	nv_wr32(dev, reg + 4, coef);
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
	struct nvbios *bios = &dev_priv->vbios;

	/*
	 * For the results of this function to be correct, CR44 must have been
	 * set (using bios_idxprt_wr to set crtchead), CR58 set for CR57 = 0,
	 * and the DCB table parsed, before the script calling the function is
	 * run.  run_digital_op_script is example of how to do such setup
	 */

	uint8_t dcb_entry = NVReadVgaCrtc5758(dev, bios->state.crtchead, 0);

	if (dcb_entry > bios->dcb.entries) {
		NV_ERROR(dev, "CR58 doesn't have a valid DCB entry currently "
				"(%02X)\n", dcb_entry);
		dcb_entry = 0x7f;	/* unused / invalid marker */
	}

	return dcb_entry;
}

static struct nouveau_i2c_chan *
init_i2c_device_find(struct drm_device *dev, int i2c_index)
{
	if (i2c_index == 0xff) {
		struct drm_nouveau_private *dev_priv = dev->dev_private;
		struct dcb_table *dcb = &dev_priv->vbios.dcb;
		/* note: dcb_entry_idx_from_crtchead needs pre-script set-up */
		int idx = dcb_entry_idx_from_crtchead(dev);

		i2c_index = NV_I2C_DEFAULT(0);
		if (idx != 0x7f && dcb->entry[idx].i2c_upper_default)
			i2c_index = NV_I2C_DEFAULT(1);
	}

	return nouveau_i2c_find(dev, i2c_index);
}

static uint32_t
get_tmds_index_reg(struct drm_device *dev, uint8_t mlv)
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
	struct nvbios *bios = &dev_priv->vbios;
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
		dacoffset = pramdac_offset[bios->dcb.entry[dcb_entry].or];
		if (mlv == 0x81)
			dacoffset ^= 8;
		return 0x6808b0 + dacoffset;
	} else {
		if (mlv >= ARRAY_SIZE(pramdac_table)) {
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
		return len;
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
		return len;
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
init_dp_condition(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_DP_CONDITION   opcode: 0x3A ('')
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): "sub" opcode
	 * offset + 2  (8 bit): unknown
	 *
	 */

	struct dcb_entry *dcb = bios->display.output;
	struct drm_device *dev = bios->dev;
	uint8_t cond = bios->data[offset + 1];
	uint8_t *table, *entry;

	BIOSLOG(bios, "0x%04X: subop 0x%02X\n", offset, cond);

	if (!iexec->execute)
		return 3;

	table = nouveau_dp_bios_data(dev, dcb, &entry);
	if (!table)
		return 3;

	switch (cond) {
	case 0:
		entry = dcb_conn(dev, dcb->connector);
		if (!entry || entry[0] != DCB_CONNECTOR_eDP)
			iexec->execute = false;
		break;
	case 1:
	case 2:
		if ((table[0]  < 0x40 && !(entry[5] & cond)) ||
		    (table[0] == 0x40 && !(entry[4] & cond)))
			iexec->execute = false;
		break;
	case 5:
	{
		struct nouveau_i2c_chan *auxch;
		int ret;

		auxch = nouveau_i2c_find(dev, bios->display.output->i2c_index);
		if (!auxch) {
			NV_ERROR(dev, "0x%04X: couldn't get auxch\n", offset);
			return 3;
		}

		ret = nouveau_dp_auxch(auxch, 9, 0xd, &cond, 1);
		if (ret) {
			NV_ERROR(dev, "0x%04X: auxch rd fail: %d\n", offset, ret);
			return 3;
		}

		if (!(cond & 1))
			iexec->execute = false;
	}
		break;
	default:
		NV_WARN(dev, "0x%04X: unknown INIT_3A op: %d\n", offset, cond);
		break;
	}

	if (iexec->execute)
		BIOSLOG(bios, "0x%04X: continuing to execute\n", offset);
	else
		BIOSLOG(bios, "0x%04X: skipping following commands\n", offset);

	return 3;
}

static int
init_op_3b(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_3B   opcode: 0x3B ('')
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): crtc index
	 *
	 */

	uint8_t or = ffs(bios->display.output->or) - 1;
	uint8_t index = bios->data[offset + 1];
	uint8_t data;

	if (!iexec->execute)
		return 2;

	data = bios_idxprt_rd(bios, 0x3d4, index);
	bios_idxprt_wr(bios, 0x3d4, index, data & ~(1 << or));
	return 2;
}

static int
init_op_3c(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_3C   opcode: 0x3C ('')
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): crtc index
	 *
	 */

	uint8_t or = ffs(bios->display.output->or) - 1;
	uint8_t index = bios->data[offset + 1];
	uint8_t data;

	if (!iexec->execute)
		return 2;

	data = bios_idxprt_rd(bios, 0x3d4, index);
	bios_idxprt_wr(bios, 0x3d4, index, data | (1 << or));
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
		return len;
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

	struct drm_device *dev = bios->dev;
	uint8_t i2c_index = bios->data[offset + 1];
	uint8_t i2c_address = bios->data[offset + 2] >> 1;
	uint8_t count = bios->data[offset + 3];
	struct nouveau_i2c_chan *chan;
	int len = 4 + count * 3;
	int ret, i;

	if (!iexec->execute)
		return len;

	BIOSLOG(bios, "0x%04X: DCBI2CIndex: 0x%02X, I2CAddress: 0x%02X, "
		      "Count: 0x%02X\n",
		offset, i2c_index, i2c_address, count);

	chan = init_i2c_device_find(dev, i2c_index);
	if (!chan) {
		NV_ERROR(dev, "0x%04X: i2c bus not found\n", offset);
		return len;
	}

	for (i = 0; i < count; i++) {
		uint8_t reg = bios->data[offset + 4 + i * 3];
		uint8_t mask = bios->data[offset + 5 + i * 3];
		uint8_t data = bios->data[offset + 6 + i * 3];
		union i2c_smbus_data val;

		ret = i2c_smbus_xfer(&chan->adapter, i2c_address, 0,
				     I2C_SMBUS_READ, reg,
				     I2C_SMBUS_BYTE_DATA, &val);
		if (ret < 0) {
			NV_ERROR(dev, "0x%04X: i2c rd fail: %d\n", offset, ret);
			return len;
		}

		BIOSLOG(bios, "0x%04X: I2CReg: 0x%02X, Value: 0x%02X, "
			      "Mask: 0x%02X, Data: 0x%02X\n",
			offset, reg, val.byte, mask, data);

		if (!bios->execute)
			continue;

		val.byte &= mask;
		val.byte |= data;
		ret = i2c_smbus_xfer(&chan->adapter, i2c_address, 0,
				     I2C_SMBUS_WRITE, reg,
				     I2C_SMBUS_BYTE_DATA, &val);
		if (ret < 0) {
			NV_ERROR(dev, "0x%04X: i2c wr fail: %d\n", offset, ret);
			return len;
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

	struct drm_device *dev = bios->dev;
	uint8_t i2c_index = bios->data[offset + 1];
	uint8_t i2c_address = bios->data[offset + 2] >> 1;
	uint8_t count = bios->data[offset + 3];
	struct nouveau_i2c_chan *chan;
	int len = 4 + count * 2;
	int ret, i;

	if (!iexec->execute)
		return len;

	BIOSLOG(bios, "0x%04X: DCBI2CIndex: 0x%02X, I2CAddress: 0x%02X, "
		      "Count: 0x%02X\n",
		offset, i2c_index, i2c_address, count);

	chan = init_i2c_device_find(dev, i2c_index);
	if (!chan) {
		NV_ERROR(dev, "0x%04X: i2c bus not found\n", offset);
		return len;
	}

	for (i = 0; i < count; i++) {
		uint8_t reg = bios->data[offset + 4 + i * 2];
		union i2c_smbus_data val;

		val.byte = bios->data[offset + 5 + i * 2];

		BIOSLOG(bios, "0x%04X: I2CReg: 0x%02X, Data: 0x%02X\n",
			offset, reg, val.byte);

		if (!bios->execute)
			continue;

		ret = i2c_smbus_xfer(&chan->adapter, i2c_address, 0,
				     I2C_SMBUS_WRITE, reg,
				     I2C_SMBUS_BYTE_DATA, &val);
		if (ret < 0) {
			NV_ERROR(dev, "0x%04X: i2c wr fail: %d\n", offset, ret);
			return len;
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

	struct drm_device *dev = bios->dev;
	uint8_t i2c_index = bios->data[offset + 1];
	uint8_t i2c_address = bios->data[offset + 2] >> 1;
	uint8_t count = bios->data[offset + 3];
	int len = 4 + count;
	struct nouveau_i2c_chan *chan;
	struct i2c_msg msg;
	uint8_t data[256];
	int ret, i;

	if (!iexec->execute)
		return len;

	BIOSLOG(bios, "0x%04X: DCBI2CIndex: 0x%02X, I2CAddress: 0x%02X, "
		      "Count: 0x%02X\n",
		offset, i2c_index, i2c_address, count);

	chan = init_i2c_device_find(dev, i2c_index);
	if (!chan) {
		NV_ERROR(dev, "0x%04X: i2c bus not found\n", offset);
		return len;
	}

	for (i = 0; i < count; i++) {
		data[i] = bios->data[offset + 4 + i];

		BIOSLOG(bios, "0x%04X: Data: 0x%02X\n", offset, data[i]);
	}

	if (bios->execute) {
		msg.addr = i2c_address;
		msg.flags = 0;
		msg.len = count;
		msg.buf = data;
		ret = i2c_transfer(&chan->adapter, &msg, 1);
		if (ret != 1) {
			NV_ERROR(dev, "0x%04X: i2c wr fail: %d\n", offset, ret);
			return len;
		}
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

	struct drm_device *dev = bios->dev;
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
	if (!reg) {
		NV_ERROR(dev, "0x%04X: no tmds_index_reg\n", offset);
		return 5;
	}

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

	struct drm_device *dev = bios->dev;
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
	if (!reg) {
		NV_ERROR(dev, "0x%04X: no tmds_index_reg\n", offset);
		return len;
	}

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
			mdelay(20);
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
init_ltime(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_LTIME   opcode: 0x57 ('V')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (16 bit): time
	 *
	 * Sleep for "time" milliseconds.
	 */

	unsigned time = ROM16(bios->data[offset + 1]);

	if (!iexec->execute)
		return 3;

	BIOSLOG(bios, "0x%04X: Sleeping for 0x%04X milliseconds\n",
		offset, time);

	mdelay(time);

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
init_jump(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_JUMP   opcode: 0x5C ('\')
	 *
	 * offset      (8  bit): opcode
	 * offset + 1  (16 bit): offset (in bios)
	 *
	 * Continue execution of init table from 'offset'
	 */

	uint16_t jmp_offset = ROM16(bios->data[offset + 1]);

	if (!iexec->execute)
		return 3;

	BIOSLOG(bios, "0x%04X: Jump to 0x%04X\n", offset, jmp_offset);
	return jmp_offset - offset;
}

static int
init_i2c_if(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_I2C_IF   opcode: 0x5E ('^')
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): DCB I2C table entry index
	 * offset + 2  (8 bit): I2C slave address
	 * offset + 3  (8 bit): I2C register
	 * offset + 4  (8 bit): mask
	 * offset + 5  (8 bit): data
	 *
	 * Read the register given by "I2C register" on the device addressed
	 * by "I2C slave address" on the I2C bus given by "DCB I2C table
	 * entry index". Compare the result AND "mask" to "data".
	 * If they're not equal, skip subsequent opcodes until condition is
	 * inverted (INIT_NOT), or we hit INIT_RESUME
	 */

	uint8_t i2c_index = bios->data[offset + 1];
	uint8_t i2c_address = bios->data[offset + 2] >> 1;
	uint8_t reg = bios->data[offset + 3];
	uint8_t mask = bios->data[offset + 4];
	uint8_t data = bios->data[offset + 5];
	struct nouveau_i2c_chan *chan;
	union i2c_smbus_data val;
	int ret;

	/* no execute check by design */

	BIOSLOG(bios, "0x%04X: DCBI2CIndex: 0x%02X, I2CAddress: 0x%02X\n",
		offset, i2c_index, i2c_address);

	chan = init_i2c_device_find(bios->dev, i2c_index);
	if (!chan)
		return -ENODEV;

	ret = i2c_smbus_xfer(&chan->adapter, i2c_address, 0,
			     I2C_SMBUS_READ, reg,
			     I2C_SMBUS_BYTE_DATA, &val);
	if (ret < 0) {
		BIOSLOG(bios, "0x%04X: I2CReg: 0x%02X, Value: [no device], "
			      "Mask: 0x%02X, Data: 0x%02X\n",
			offset, reg, mask, data);
		iexec->execute = 0;
		return 6;
	}

	BIOSLOG(bios, "0x%04X: I2CReg: 0x%02X, Value: 0x%02X, "
		      "Mask: 0x%02X, Data: 0x%02X\n",
		offset, reg, val.byte, mask, data);

	iexec->execute = ((val.byte & mask) == data);

	return 6;
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

static inline void
bios_md32(struct nvbios *bios, uint32_t reg,
	  uint32_t mask, uint32_t val)
{
	bios_wr32(bios, reg, (bios_rd32(bios, reg) & ~mask) | val);
}

static uint32_t
peek_fb(struct drm_device *dev, struct io_mapping *fb,
	uint32_t off)
{
	uint32_t val = 0;

	if (off < pci_resource_len(dev->pdev, 1)) {
		uint8_t __iomem *p =
			io_mapping_map_atomic_wc(fb, off & PAGE_MASK);

		val = ioread32(p + (off & ~PAGE_MASK));

		io_mapping_unmap_atomic(p);
	}

	return val;
}

static void
poke_fb(struct drm_device *dev, struct io_mapping *fb,
	uint32_t off, uint32_t val)
{
	if (off < pci_resource_len(dev->pdev, 1)) {
		uint8_t __iomem *p =
			io_mapping_map_atomic_wc(fb, off & PAGE_MASK);

		iowrite32(val, p + (off & ~PAGE_MASK));
		wmb();

		io_mapping_unmap_atomic(p);
	}
}

static inline bool
read_back_fb(struct drm_device *dev, struct io_mapping *fb,
	     uint32_t off, uint32_t val)
{
	poke_fb(dev, fb, off, val);
	return val == peek_fb(dev, fb, off);
}

static int
nv04_init_compute_mem(struct nvbios *bios)
{
	struct drm_device *dev = bios->dev;
	uint32_t patt = 0xdeadbeef;
	struct io_mapping *fb;
	int i;

	/* Map the framebuffer aperture */
	fb = io_mapping_create_wc(pci_resource_start(dev->pdev, 1),
				  pci_resource_len(dev->pdev, 1));
	if (!fb)
		return -ENOMEM;

	/* Sequencer and refresh off */
	NVWriteVgaSeq(dev, 0, 1, NVReadVgaSeq(dev, 0, 1) | 0x20);
	bios_md32(bios, NV04_PFB_DEBUG_0, 0, NV04_PFB_DEBUG_0_REFRESH_OFF);

	bios_md32(bios, NV04_PFB_BOOT_0, ~0,
		  NV04_PFB_BOOT_0_RAM_AMOUNT_16MB |
		  NV04_PFB_BOOT_0_RAM_WIDTH_128 |
		  NV04_PFB_BOOT_0_RAM_TYPE_SGRAM_16MBIT);

	for (i = 0; i < 4; i++)
		poke_fb(dev, fb, 4 * i, patt);

	poke_fb(dev, fb, 0x400000, patt + 1);

	if (peek_fb(dev, fb, 0) == patt + 1) {
		bios_md32(bios, NV04_PFB_BOOT_0, NV04_PFB_BOOT_0_RAM_TYPE,
			  NV04_PFB_BOOT_0_RAM_TYPE_SDRAM_16MBIT);
		bios_md32(bios, NV04_PFB_DEBUG_0,
			  NV04_PFB_DEBUG_0_REFRESH_OFF, 0);

		for (i = 0; i < 4; i++)
			poke_fb(dev, fb, 4 * i, patt);

		if ((peek_fb(dev, fb, 0xc) & 0xffff) != (patt & 0xffff))
			bios_md32(bios, NV04_PFB_BOOT_0,
				  NV04_PFB_BOOT_0_RAM_WIDTH_128 |
				  NV04_PFB_BOOT_0_RAM_AMOUNT,
				  NV04_PFB_BOOT_0_RAM_AMOUNT_8MB);

	} else if ((peek_fb(dev, fb, 0xc) & 0xffff0000) !=
		   (patt & 0xffff0000)) {
		bios_md32(bios, NV04_PFB_BOOT_0,
			  NV04_PFB_BOOT_0_RAM_WIDTH_128 |
			  NV04_PFB_BOOT_0_RAM_AMOUNT,
			  NV04_PFB_BOOT_0_RAM_AMOUNT_4MB);

	} else if (peek_fb(dev, fb, 0) != patt) {
		if (read_back_fb(dev, fb, 0x800000, patt))
			bios_md32(bios, NV04_PFB_BOOT_0,
				  NV04_PFB_BOOT_0_RAM_AMOUNT,
				  NV04_PFB_BOOT_0_RAM_AMOUNT_8MB);
		else
			bios_md32(bios, NV04_PFB_BOOT_0,
				  NV04_PFB_BOOT_0_RAM_AMOUNT,
				  NV04_PFB_BOOT_0_RAM_AMOUNT_4MB);

		bios_md32(bios, NV04_PFB_BOOT_0, NV04_PFB_BOOT_0_RAM_TYPE,
			  NV04_PFB_BOOT_0_RAM_TYPE_SGRAM_8MBIT);

	} else if (!read_back_fb(dev, fb, 0x800000, patt)) {
		bios_md32(bios, NV04_PFB_BOOT_0, NV04_PFB_BOOT_0_RAM_AMOUNT,
			  NV04_PFB_BOOT_0_RAM_AMOUNT_8MB);

	}

	/* Refresh on, sequencer on */
	bios_md32(bios, NV04_PFB_DEBUG_0, NV04_PFB_DEBUG_0_REFRESH_OFF, 0);
	NVWriteVgaSeq(dev, 0, 1, NVReadVgaSeq(dev, 0, 1) & ~0x20);

	io_mapping_free(fb);
	return 0;
}

static const uint8_t *
nv05_memory_config(struct nvbios *bios)
{
	/* Defaults for BIOSes lacking a memory config table */
	static const uint8_t default_config_tab[][2] = {
		{ 0x24, 0x00 },
		{ 0x28, 0x00 },
		{ 0x24, 0x01 },
		{ 0x1f, 0x00 },
		{ 0x0f, 0x00 },
		{ 0x17, 0x00 },
		{ 0x06, 0x00 },
		{ 0x00, 0x00 }
	};
	int i = (bios_rd32(bios, NV_PEXTDEV_BOOT_0) &
		 NV_PEXTDEV_BOOT_0_RAMCFG) >> 2;

	if (bios->legacy.mem_init_tbl_ptr)
		return &bios->data[bios->legacy.mem_init_tbl_ptr + 2 * i];
	else
		return default_config_tab[i];
}

static int
nv05_init_compute_mem(struct nvbios *bios)
{
	struct drm_device *dev = bios->dev;
	const uint8_t *ramcfg = nv05_memory_config(bios);
	uint32_t patt = 0xdeadbeef;
	struct io_mapping *fb;
	int i, v;

	/* Map the framebuffer aperture */
	fb = io_mapping_create_wc(pci_resource_start(dev->pdev, 1),
				  pci_resource_len(dev->pdev, 1));
	if (!fb)
		return -ENOMEM;

	/* Sequencer off */
	NVWriteVgaSeq(dev, 0, 1, NVReadVgaSeq(dev, 0, 1) | 0x20);

	if (bios_rd32(bios, NV04_PFB_BOOT_0) & NV04_PFB_BOOT_0_UMA_ENABLE)
		goto out;

	bios_md32(bios, NV04_PFB_DEBUG_0, NV04_PFB_DEBUG_0_REFRESH_OFF, 0);

	/* If present load the hardcoded scrambling table */
	if (bios->legacy.mem_init_tbl_ptr) {
		uint32_t *scramble_tab = (uint32_t *)&bios->data[
			bios->legacy.mem_init_tbl_ptr + 0x10];

		for (i = 0; i < 8; i++)
			bios_wr32(bios, NV04_PFB_SCRAMBLE(i),
				  ROM32(scramble_tab[i]));
	}

	/* Set memory type/width/length defaults depending on the straps */
	bios_md32(bios, NV04_PFB_BOOT_0, 0x3f, ramcfg[0]);

	if (ramcfg[1] & 0x80)
		bios_md32(bios, NV04_PFB_CFG0, 0, NV04_PFB_CFG0_SCRAMBLE);

	bios_md32(bios, NV04_PFB_CFG1, 0x700001, (ramcfg[1] & 1) << 20);
	bios_md32(bios, NV04_PFB_CFG1, 0, 1);

	/* Probe memory bus width */
	for (i = 0; i < 4; i++)
		poke_fb(dev, fb, 4 * i, patt);

	if (peek_fb(dev, fb, 0xc) != patt)
		bios_md32(bios, NV04_PFB_BOOT_0,
			  NV04_PFB_BOOT_0_RAM_WIDTH_128, 0);

	/* Probe memory length */
	v = bios_rd32(bios, NV04_PFB_BOOT_0) & NV04_PFB_BOOT_0_RAM_AMOUNT;

	if (v == NV04_PFB_BOOT_0_RAM_AMOUNT_32MB &&
	    (!read_back_fb(dev, fb, 0x1000000, ++patt) ||
	     !read_back_fb(dev, fb, 0, ++patt)))
		bios_md32(bios, NV04_PFB_BOOT_0, NV04_PFB_BOOT_0_RAM_AMOUNT,
			  NV04_PFB_BOOT_0_RAM_AMOUNT_16MB);

	if (v == NV04_PFB_BOOT_0_RAM_AMOUNT_16MB &&
	    !read_back_fb(dev, fb, 0x800000, ++patt))
		bios_md32(bios, NV04_PFB_BOOT_0, NV04_PFB_BOOT_0_RAM_AMOUNT,
			  NV04_PFB_BOOT_0_RAM_AMOUNT_8MB);

	if (!read_back_fb(dev, fb, 0x400000, ++patt))
		bios_md32(bios, NV04_PFB_BOOT_0, NV04_PFB_BOOT_0_RAM_AMOUNT,
			  NV04_PFB_BOOT_0_RAM_AMOUNT_4MB);

out:
	/* Sequencer on */
	NVWriteVgaSeq(dev, 0, 1, NVReadVgaSeq(dev, 0, 1) & ~0x20);

	io_mapping_free(fb);
	return 0;
}

static int
nv10_init_compute_mem(struct nvbios *bios)
{
	struct drm_device *dev = bios->dev;
	struct drm_nouveau_private *dev_priv = bios->dev->dev_private;
	const int mem_width[] = { 0x10, 0x00, 0x20 };
	const int mem_width_count = (dev_priv->chipset >= 0x17 ? 3 : 2);
	uint32_t patt = 0xdeadbeef;
	struct io_mapping *fb;
	int i, j, k;

	/* Map the framebuffer aperture */
	fb = io_mapping_create_wc(pci_resource_start(dev->pdev, 1),
				  pci_resource_len(dev->pdev, 1));
	if (!fb)
		return -ENOMEM;

	bios_wr32(bios, NV10_PFB_REFCTRL, NV10_PFB_REFCTRL_VALID_1);

	/* Probe memory bus width */
	for (i = 0; i < mem_width_count; i++) {
		bios_md32(bios, NV04_PFB_CFG0, 0x30, mem_width[i]);

		for (j = 0; j < 4; j++) {
			for (k = 0; k < 4; k++)
				poke_fb(dev, fb, 0x1c, 0);

			poke_fb(dev, fb, 0x1c, patt);
			poke_fb(dev, fb, 0x3c, 0);

			if (peek_fb(dev, fb, 0x1c) == patt)
				goto mem_width_found;
		}
	}

mem_width_found:
	patt <<= 1;

	/* Probe amount of installed memory */
	for (i = 0; i < 4; i++) {
		int off = bios_rd32(bios, NV04_PFB_FIFO_DATA) - 0x100000;

		poke_fb(dev, fb, off, patt);
		poke_fb(dev, fb, 0, 0);

		peek_fb(dev, fb, 0);
		peek_fb(dev, fb, 0);
		peek_fb(dev, fb, 0);
		peek_fb(dev, fb, 0);

		if (peek_fb(dev, fb, off) == patt)
			goto amount_found;
	}

	/* IC missing - disable the upper half memory space. */
	bios_md32(bios, NV04_PFB_CFG0, 0x1000, 0);

amount_found:
	io_mapping_free(fb);
	return 0;
}

static int
nv20_init_compute_mem(struct nvbios *bios)
{
	struct drm_device *dev = bios->dev;
	struct drm_nouveau_private *dev_priv = bios->dev->dev_private;
	uint32_t mask = (dev_priv->chipset >= 0x25 ? 0x300 : 0x900);
	uint32_t amount, off;
	struct io_mapping *fb;

	/* Map the framebuffer aperture */
	fb = io_mapping_create_wc(pci_resource_start(dev->pdev, 1),
				  pci_resource_len(dev->pdev, 1));
	if (!fb)
		return -ENOMEM;

	bios_wr32(bios, NV10_PFB_REFCTRL, NV10_PFB_REFCTRL_VALID_1);

	/* Allow full addressing */
	bios_md32(bios, NV04_PFB_CFG0, 0, mask);

	amount = bios_rd32(bios, NV04_PFB_FIFO_DATA);
	for (off = amount; off > 0x2000000; off -= 0x2000000)
		poke_fb(dev, fb, off - 4, off);

	amount = bios_rd32(bios, NV04_PFB_FIFO_DATA);
	if (amount != peek_fb(dev, fb, amount - 4))
		/* IC missing - disable the upper half memory space. */
		bios_md32(bios, NV04_PFB_CFG0, mask, 0);

	io_mapping_free(fb);
	return 0;
}

static int
init_compute_mem(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_COMPUTE_MEM   opcode: 0x63 ('c')
	 *
	 * offset      (8 bit): opcode
	 *
	 * This opcode is meant to set the PFB memory config registers
	 * appropriately so that we can correctly calculate how much VRAM it
	 * has (on nv10 and better chipsets the amount of installed VRAM is
	 * subsequently reported in NV_PFB_CSTATUS (0x10020C)).
	 *
	 * The implementation of this opcode in general consists of several
	 * parts:
	 *
	 * 1) Determination of memory type and density. Only necessary for
	 *    really old chipsets, the memory type reported by the strap bits
	 *    (0x101000) is assumed to be accurate on nv05 and newer.
	 *
	 * 2) Determination of the memory bus width. Usually done by a cunning
	 *    combination of writes to offsets 0x1c and 0x3c in the fb, and
	 *    seeing whether the written values are read back correctly.
	 *
	 *    Only necessary on nv0x-nv1x and nv34, on the other cards we can
	 *    trust the straps.
	 *
	 * 3) Determination of how many of the card's RAM pads have ICs
	 *    attached, usually done by a cunning combination of writes to an
	 *    offset slightly less than the maximum memory reported by
	 *    NV_PFB_CSTATUS, then seeing if the test pattern can be read back.
	 *
	 * This appears to be a NOP on IGPs and NV4x or newer chipsets, both io
	 * logs of the VBIOS and kmmio traces of the binary driver POSTing the
	 * card show nothing being done for this opcode. Why is it still listed
	 * in the table?!
	 */

	/* no iexec->execute check by design */

	struct drm_nouveau_private *dev_priv = bios->dev->dev_private;
	int ret;

	if (dev_priv->chipset >= 0x40 ||
	    dev_priv->chipset == 0x1a ||
	    dev_priv->chipset == 0x1f)
		ret = 0;
	else if (dev_priv->chipset >= 0x20 &&
		 dev_priv->chipset != 0x34)
		ret = nv20_init_compute_mem(bios);
	else if (dev_priv->chipset >= 0x10)
		ret = nv10_init_compute_mem(bios);
	else if (dev_priv->chipset >= 0x5)
		ret = nv05_init_compute_mem(bios);
	else
		ret = nv04_init_compute_mem(bios);

	if (ret)
		return ret;

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
	bios_wr32(bios, NV_PBUS_PCI_NV_19, pci_nv_19 & ~0xf00);

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
		case NV04_PFB_PRE:
			data = NV04_PFB_PRE_CMD_PRECHARGE;
			break;
		case NV04_PFB_PAD:
			data = NV04_PFB_PAD_CKE_NORMAL;
			break;
		case NV04_PFB_REF:
			data = NV04_PFB_REF_CMD_REFRESH;
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
	uint8_t cr3c = ((straps << 2) & 0xf0) | (straps & 0x40) >> 6;

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
	 * Test if (NV04_PFB_BOOT_0 & "mask") equals "cmpval".
	 * If condition not met skip subsequent opcodes until condition is
	 * inverted (INIT_NOT), or we hit INIT_RESUME
	 */

	uint8_t mask = bios->data[offset + 1];
	uint8_t cmpval = bios->data[offset + 2];
	uint8_t data;

	if (!iexec->execute)
		return 3;

	data = bios_rd32(bios, NV04_PFB_BOOT_0) & mask;

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
		mdelay((time + 900) / 1000);

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

	if (iexec->execute && bios->execute)
		nouveau_gpio_reset(bios->dev);

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

	/* critical! to know the length of the opcode */;
	if (!blocklen) {
		NV_ERROR(bios->dev,
			 "0x%04X: Zero block length - has the M table "
			 "been parsed?\n", offset);
		return -EINVAL;
	}

	if (!iexec->execute)
		return len;

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
		return len;
	}

	auxch = init_i2c_device_find(dev, bios->display.output->i2c_index);
	if (!auxch) {
		NV_ERROR(dev, "INIT_AUXCH: couldn't get auxch %d\n",
			 bios->display.output->i2c_index);
		return len;
	}

	if (!iexec->execute)
		return len;

	offset += 6;
	for (i = 0; i < count; i++, offset += 2) {
		uint8_t data;

		ret = nouveau_dp_auxch(auxch, 9, addr, &data, 1);
		if (ret) {
			NV_ERROR(dev, "INIT_AUXCH: rd auxch fail %d\n", ret);
			return len;
		}

		data &= bios->data[offset + 0];
		data |= bios->data[offset + 1];

		ret = nouveau_dp_auxch(auxch, 8, addr, &data, 1);
		if (ret) {
			NV_ERROR(dev, "INIT_AUXCH: wr auxch fail %d\n", ret);
			return len;
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
		return len;
	}

	auxch = init_i2c_device_find(dev, bios->display.output->i2c_index);
	if (!auxch) {
		NV_ERROR(dev, "INIT_ZM_AUXCH: couldn't get auxch %d\n",
			 bios->display.output->i2c_index);
		return len;
	}

	if (!iexec->execute)
		return len;

	offset += 6;
	for (i = 0; i < count; i++, offset++) {
		ret = nouveau_dp_auxch(auxch, 8, addr, &bios->data[offset], 1);
		if (ret) {
			NV_ERROR(dev, "INIT_ZM_AUXCH: wr auxch fail %d\n", ret);
			return len;
		}
	}

	return len;
}

static int
init_i2c_long_if(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * INIT_I2C_LONG_IF   opcode: 0x9A ('')
	 *
	 * offset      (8 bit): opcode
	 * offset + 1  (8 bit): DCB I2C table entry index
	 * offset + 2  (8 bit): I2C slave address
	 * offset + 3  (16 bit): I2C register
	 * offset + 5  (8 bit): mask
	 * offset + 6  (8 bit): data
	 *
	 * Read the register given by "I2C register" on the device addressed
	 * by "I2C slave address" on the I2C bus given by "DCB I2C table
	 * entry index". Compare the result AND "mask" to "data".
	 * If they're not equal, skip subsequent opcodes until condition is
	 * inverted (INIT_NOT), or we hit INIT_RESUME
	 */

	uint8_t i2c_index = bios->data[offset + 1];
	uint8_t i2c_address = bios->data[offset + 2] >> 1;
	uint8_t reglo = bios->data[offset + 3];
	uint8_t reghi = bios->data[offset + 4];
	uint8_t mask = bios->data[offset + 5];
	uint8_t data = bios->data[offset + 6];
	struct nouveau_i2c_chan *chan;
	uint8_t buf0[2] = { reghi, reglo };
	uint8_t buf1[1];
	struct i2c_msg msg[2] = {
		{ i2c_address, 0, 1, buf0 },
		{ i2c_address, I2C_M_RD, 1, buf1 },
	};
	int ret;

	/* no execute check by design */

	BIOSLOG(bios, "0x%04X: DCBI2CIndex: 0x%02X, I2CAddress: 0x%02X\n",
		offset, i2c_index, i2c_address);

	chan = init_i2c_device_find(bios->dev, i2c_index);
	if (!chan)
		return -ENODEV;


	ret = i2c_transfer(&chan->adapter, msg, 2);
	if (ret < 0) {
		BIOSLOG(bios, "0x%04X: I2CReg: 0x%02X:0x%02X, Value: [no device], "
			      "Mask: 0x%02X, Data: 0x%02X\n",
			offset, reghi, reglo, mask, data);
		iexec->execute = 0;
		return 7;
	}

	BIOSLOG(bios, "0x%04X: I2CReg: 0x%02X:0x%02X, Value: 0x%02X, "
		      "Mask: 0x%02X, Data: 0x%02X\n",
		offset, reghi, reglo, buf1[0], mask, data);

	iexec->execute = ((buf1[0] & mask) == data);

	return 7;
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
	{ "INIT_DP_CONDITION"                 , 0x3A, init_dp_condition               },
	{ "INIT_OP_3B"                        , 0x3B, init_op_3b                      },
	{ "INIT_OP_3C"                        , 0x3C, init_op_3c                      },
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
	{ "INIT_LTIME"                        , 0x57, init_ltime                      },
	{ "INIT_ZM_REG_SEQUENCE"              , 0x58, init_zm_reg_sequence            },
	/* INIT_INDIRECT_REG (0x5A, 7, 0, 0) removed due to no example of use */
	{ "INIT_SUB_DIRECT"                   , 0x5B, init_sub_direct                 },
	{ "INIT_JUMP"                         , 0x5C, init_jump                       },
	{ "INIT_I2C_IF"                       , 0x5E, init_i2c_if                     },
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
	{ "INIT_I2C_LONG_IF"                  , 0x9A, init_i2c_long_if                },
	{ NULL                                , 0   , NULL                            }
};

#define MAX_TABLE_OPS 1000

static int
parse_init_table(struct nvbios *bios, uint16_t offset, struct init_exec *iexec)
{
	/*
	 * Parses all commands in an init table.
	 *
	 * We start out executing all commands found in the init table. Some
	 * opcodes may change the status of iexec->execute to SKIP, which will
	 * cause the following opcodes to perform no operation until the value
	 * is changed back to EXECUTE.
	 */

	int count = 0, i, ret;
	uint8_t id;

	/* catch NULL script pointers */
	if (offset == 0)
		return 0;

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

		if (!itbl_entry[i].name) {
			NV_ERROR(bios->dev,
				 "0x%04X: Init table command not found: "
				 "0x%02X\n", offset, id);
			return -ENOENT;
		}

		BIOSLOG(bios, "0x%04X: [ (0x%02X) - %s ]\n", offset,
			itbl_entry[i].id, itbl_entry[i].name);

		/* execute eventual command handler */
		ret = (*itbl_entry[i].handler)(bios, offset, iexec);
		if (ret < 0) {
			NV_ERROR(bios->dev, "0x%04X: Failed parsing init "
				 "table opcode: %s %d\n", offset,
				 itbl_entry[i].name, ret);
		}

		if (ret <= 0)
			break;

		/*
		 * Add the offset of the current command including all data
		 * of that command. The offset will then be pointing on the
		 * next op code.
		 */
		offset += ret;
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
	struct nvbios *bios = &dev_priv->vbios;
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
	struct nvbios *bios = &dev_priv->vbios;
	uint8_t sub = bios->data[bios->fp.xlated_entry + script] + (bios->fp.link_c_increment && dcbent->or & OUTPUT_C ? 1 : 0);
	uint16_t scriptofs = ROM16(bios->data[bios->init_script_tbls_ptr + sub * 2]);

	if (!bios->fp.xlated_entry || !sub || !scriptofs)
		return -EINVAL;

	run_digital_op_script(dev, scriptofs, dcbent, head, bios->fp.dual_link);

	if (script == LVDS_PANEL_OFF) {
		/* off-on delay in ms */
		mdelay(ROM16(bios->data[bios->fp.xlated_entry + 7]));
	}
#ifdef __powerpc__
	/* Powerbook specific quirks */
	if (script == LVDS_RESET &&
	    (dev->pci_device == 0x0179 || dev->pci_device == 0x0189 ||
	     dev->pci_device == 0x0329))
		nv_write_tmds(dev, dcbent->or, 0, 0x02, 0x72);
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
	struct nvbios *bios = &dev_priv->vbios;
	unsigned int outputset = (dcbent->or == 4) ? 1 : 0;
	uint16_t scriptptr = 0, clktable;

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
		clktable = bios->fp.lvdsmanufacturerpointer + 15;
		if (dcbent->or == 4)
			clktable += 8;

		if (dcbent->lvdsconf.use_straps_for_mode) {
			if (bios->fp.dual_link)
				clktable += 4;
			if (bios->fp.if_is_24bit)
				clktable += 2;
		} else {
			/* using EDID */
			int cmpval_24bit = (dcbent->or == 4) ? 4 : 1;

			if (bios->fp.dual_link) {
				clktable += 4;
				cmpval_24bit <<= 1;
			}

			if (bios->fp.strapless_is_24bit & cmpval_24bit)
				clktable += 2;
		}

		clktable = ROM16(bios->data[clktable]);
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
	struct nvbios *bios = &dev_priv->vbios;
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
		bios->digital_min_front_porch = 0x4b;
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
		bios->digital_min_front_porch = fptable[4];
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
		bios->fp_no_ddc = fpstrapping != 0xf || fpindex != 0xf;

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
	struct nvbios *bios = &dev_priv->vbios;
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
	 * indexed by a separate xlat table, indexed in turn by the fp strap in
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
	struct nvbios *bios = &dev_priv->vbios;
	int fpstrapping = get_fp_strap(dev, bios), lvdsmanufacturerindex = 0;
	struct lvdstableheader lth;
	uint16_t lvdsofs;
	int ret, chip_version = bios->chip_version;

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
	case 0x40:
		/*
		 * No sign of the "power off for reset" or "reset for panel
		 * on" bits, but it's safer to assume we should
		 */
		bios->fp.power_off_for_reset = true;
		bios->fp.reset_after_pclk_change = true;

		/*
		 * It's ok lvdsofs is wrong for nv4x edid case; dual_link is
		 * over-written, and if_is_24bit isn't used
		 */
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

/* BIT 'U'/'d' table encoder subtables have hashes matching them to
 * a particular set of encoders.
 *
 * This function returns true if a particular DCB entry matches.
 */
bool
bios_encoder_match(struct dcb_entry *dcb, u32 hash)
{
	if ((hash & 0x000000f0) != (dcb->location << 4))
		return false;
	if ((hash & 0x0000000f) != dcb->type)
		return false;
	if (!(hash & (dcb->or << 16)))
		return false;

	switch (dcb->type) {
	case OUTPUT_TMDS:
	case OUTPUT_LVDS:
	case OUTPUT_DP:
		if (hash & 0x00c00000) {
			if (!(hash & (dcb->sorconf.link << 22)))
				return false;
		}
	default:
		return true;
	}
}

int
nouveau_bios_run_display_table(struct drm_device *dev, u16 type, int pclk,
			       struct dcb_entry *dcbent, int crtc)
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
	struct nvbios *bios = &dev_priv->vbios;
	uint8_t *table = &bios->data[bios->display.script_table_ptr];
	uint8_t *otable = NULL;
	uint16_t script;
	int i;

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
	for (i = 0; i < table[3]; i++) {
		otable = ROMPTR(dev, table[table[1] + (i * table[2])]);
		if (otable && bios_encoder_match(dcbent, ROM32(otable[0])))
			break;
	}

	if (!otable) {
		NV_DEBUG_KMS(dev, "failed to match any output table\n");
		return 1;
	}

	if (pclk < -2 || pclk > 0) {
		/* Try to find matching script table entry */
		for (i = 0; i < otable[5]; i++) {
			if (ROM16(otable[table[4] + i*6]) == type)
				break;
		}

		if (i == otable[5]) {
			NV_ERROR(dev, "Table 0x%04x not found for %d/%d, "
				      "using first\n",
				 type, dcbent->type, dcbent->or);
			i = 0;
		}
	}

	if (pclk == 0) {
		script = ROM16(otable[6]);
		if (!script) {
			NV_DEBUG_KMS(dev, "output script 0 not found\n");
			return 1;
		}

		NV_DEBUG_KMS(dev, "0x%04X: parsing output script 0\n", script);
		nouveau_bios_run_init_table(dev, script, dcbent, crtc);
	} else
	if (pclk == -1) {
		script = ROM16(otable[8]);
		if (!script) {
			NV_DEBUG_KMS(dev, "output script 1 not found\n");
			return 1;
		}

		NV_DEBUG_KMS(dev, "0x%04X: parsing output script 1\n", script);
		nouveau_bios_run_init_table(dev, script, dcbent, crtc);
	} else
	if (pclk == -2) {
		if (table[4] >= 12)
			script = ROM16(otable[10]);
		else
			script = 0;
		if (!script) {
			NV_DEBUG_KMS(dev, "output script 2 not found\n");
			return 1;
		}

		NV_DEBUG_KMS(dev, "0x%04X: parsing output script 2\n", script);
		nouveau_bios_run_init_table(dev, script, dcbent, crtc);
	} else
	if (pclk > 0) {
		script = ROM16(otable[table[4] + i*6 + 2]);
		if (script)
			script = clkcmptable(bios, script, pclk);
		if (!script) {
			NV_DEBUG_KMS(dev, "clock script 0 not found\n");
			return 1;
		}

		NV_DEBUG_KMS(dev, "0x%04X: parsing clock script 0\n", script);
		nouveau_bios_run_init_table(dev, script, dcbent, crtc);
	} else
	if (pclk < 0) {
		script = ROM16(otable[table[4] + i*6 + 4]);
		if (script)
			script = clkcmptable(bios, script, -pclk);
		if (!script) {
			NV_DEBUG_KMS(dev, "clock script 1 not found\n");
			return 1;
		}

		NV_DEBUG_KMS(dev, "0x%04X: parsing clock script 1\n", script);
		nouveau_bios_run_init_table(dev, script, dcbent, crtc);
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
	struct nvbios *bios = &dev_priv->vbios;
	int cv = bios->chip_version;
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

struct pll_mapping {
	u8  type;
	u32 reg;
};

static struct pll_mapping nv04_pll_mapping[] = {
	{ PLL_CORE  , NV_PRAMDAC_NVPLL_COEFF },
	{ PLL_MEMORY, NV_PRAMDAC_MPLL_COEFF },
	{ PLL_VPLL0 , NV_PRAMDAC_VPLL_COEFF },
	{ PLL_VPLL1 , NV_RAMDAC_VPLL2 },
	{}
};

static struct pll_mapping nv40_pll_mapping[] = {
	{ PLL_CORE  , 0x004000 },
	{ PLL_MEMORY, 0x004020 },
	{ PLL_VPLL0 , NV_PRAMDAC_VPLL_COEFF },
	{ PLL_VPLL1 , NV_RAMDAC_VPLL2 },
	{}
};

static struct pll_mapping nv50_pll_mapping[] = {
	{ PLL_CORE  , 0x004028 },
	{ PLL_SHADER, 0x004020 },
	{ PLL_UNK03 , 0x004000 },
	{ PLL_MEMORY, 0x004008 },
	{ PLL_UNK40 , 0x00e810 },
	{ PLL_UNK41 , 0x00e818 },
	{ PLL_UNK42 , 0x00e824 },
	{ PLL_VPLL0 , 0x614100 },
	{ PLL_VPLL1 , 0x614900 },
	{}
};

static struct pll_mapping nv84_pll_mapping[] = {
	{ PLL_CORE  , 0x004028 },
	{ PLL_SHADER, 0x004020 },
	{ PLL_MEMORY, 0x004008 },
	{ PLL_VDEC  , 0x004030 },
	{ PLL_UNK41 , 0x00e818 },
	{ PLL_VPLL0 , 0x614100 },
	{ PLL_VPLL1 , 0x614900 },
	{}
};

u32
get_pll_register(struct drm_device *dev, enum pll_types type)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->vbios;
	struct pll_mapping *map;
	int i;

	if (dev_priv->card_type < NV_40)
		map = nv04_pll_mapping;
	else
	if (dev_priv->card_type < NV_50)
		map = nv40_pll_mapping;
	else {
		u8 *plim = &bios->data[bios->pll_limit_tbl_ptr];

		if (plim[0] >= 0x30) {
			u8 *entry = plim + plim[1];
			for (i = 0; i < plim[3]; i++, entry += plim[2]) {
				if (entry[0] == type)
					return ROM32(entry[3]);
			}

			return 0;
		}

		if (dev_priv->chipset == 0x50)
			map = nv50_pll_mapping;
		else
			map = nv84_pll_mapping;
	}

	while (map->reg) {
		if (map->type == type)
			return map->reg;
		map++;
	}

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
	struct nvbios *bios = &dev_priv->vbios;
	int cv = bios->chip_version, pllindex = 0;
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

	/* if we were passed a type rather than a register, figure
	 * out the register and store it
	 */
	if (limit_match > PLL_MAX)
		pll_lim->reg = limit_match;
	else {
		pll_lim->reg = get_pll_register(dev, limit_match);
		if (!pll_lim->reg)
			return -ENOENT;
	}

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
		uint8_t *pll_rec;
		int i;

		/*
		 * First entry is default match, if nothing better. warn if
		 * reg field nonzero
		 */
		if (ROM32(bios->data[plloffs]))
			NV_WARN(dev, "Default PLL limit entry has non-zero "
				       "register field\n");

		for (i = 1; i < entries; i++)
			if (ROM32(bios->data[plloffs + recordlen * i]) == pll_lim->reg) {
				pllindex = i;
				break;
			}

		if ((dev_priv->card_type >= NV_50) && (pllindex == 0)) {
			NV_ERROR(dev, "Register 0x%08x not found in PLL "
				 "limits table", pll_lim->reg);
			return -ENOENT;
		}

		pll_rec = &bios->data[plloffs + recordlen * pllindex];

		BIOSLOG(bios, "Loading PLL limits for reg 0x%08x\n",
			pllindex ? pll_lim->reg : 0);

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

			if ((pll_lim->reg == NV_PRAMDAC_VPLL_COEFF && sel_clk & 0x20) ||
			    (pll_lim->reg == NV_RAMDAC_VPLL2 && sel_clk & 0x80)) {
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
			pll_lim->reg);

		for (i = 0; i < entries; i++, entry += recordlen) {
			if (ROM32(entry[3]) == pll_lim->reg) {
				record = &bios->data[ROM16(entry[1])];
				break;
			}
		}

		if (!record) {
			NV_ERROR(dev, "Register 0x%08x not found in PLL "
				 "limits table", pll_lim->reg);
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
			pll_lim->reg);

		for (i = 0; i < entries; i++, entry += recordlen) {
			if (ROM32(entry[3]) == pll_lim->reg) {
				record = &bios->data[ROM16(entry[1])];
				break;
			}
		}

		if (!record) {
			NV_ERROR(dev, "Register 0x%08x not found in PLL "
				 "limits table", pll_lim->reg);
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
		pll_lim->refclk = ROM16(entry[9]) * 1000;
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

	NV_DEBUG(dev, "pll.vco1.minfreq: %d\n", pll_lim->vco1.minfreq);
	NV_DEBUG(dev, "pll.vco1.maxfreq: %d\n", pll_lim->vco1.maxfreq);
	NV_DEBUG(dev, "pll.vco1.min_inputfreq: %d\n", pll_lim->vco1.min_inputfreq);
	NV_DEBUG(dev, "pll.vco1.max_inputfreq: %d\n", pll_lim->vco1.max_inputfreq);
	NV_DEBUG(dev, "pll.vco1.min_n: %d\n", pll_lim->vco1.min_n);
	NV_DEBUG(dev, "pll.vco1.max_n: %d\n", pll_lim->vco1.max_n);
	NV_DEBUG(dev, "pll.vco1.min_m: %d\n", pll_lim->vco1.min_m);
	NV_DEBUG(dev, "pll.vco1.max_m: %d\n", pll_lim->vco1.max_m);
	if (pll_lim->vco2.maxfreq) {
		NV_DEBUG(dev, "pll.vco2.minfreq: %d\n", pll_lim->vco2.minfreq);
		NV_DEBUG(dev, "pll.vco2.maxfreq: %d\n", pll_lim->vco2.maxfreq);
		NV_DEBUG(dev, "pll.vco2.min_inputfreq: %d\n", pll_lim->vco2.min_inputfreq);
		NV_DEBUG(dev, "pll.vco2.max_inputfreq: %d\n", pll_lim->vco2.max_inputfreq);
		NV_DEBUG(dev, "pll.vco2.min_n: %d\n", pll_lim->vco2.min_n);
		NV_DEBUG(dev, "pll.vco2.max_n: %d\n", pll_lim->vco2.max_n);
		NV_DEBUG(dev, "pll.vco2.min_m: %d\n", pll_lim->vco2.min_m);
		NV_DEBUG(dev, "pll.vco2.max_m: %d\n", pll_lim->vco2.max_m);
	}
	if (!pll_lim->max_p) {
		NV_DEBUG(dev, "pll.max_log2p: %d\n", pll_lim->max_log2p);
		NV_DEBUG(dev, "pll.log2p_bias: %d\n", pll_lim->log2p_bias);
	} else {
		NV_DEBUG(dev, "pll.min_p: %d\n", pll_lim->min_p);
		NV_DEBUG(dev, "pll.max_p: %d\n", pll_lim->max_p);
	}
	NV_DEBUG(dev, "pll.refclk: %d\n", pll_lim->refclk);

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
	bios->chip_version = bios->data[offset + 2];
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
		NV_DEBUG(dev, "Pointer to BIT loadval table invalid\n");
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
	bios->dactestval = ROM32(bios->data[load_table_ptr + headerlen]) & 0x3ff;

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

	bios->dactestval = ROM32(bios->data[daccmpoffset + dacheaderlen]);
	bios->tvdactestval = ROM32(bios->data[daccmpoffset + dacheaderlen + 4]);

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

	if (bitentry->version < 2) {
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
	if (!tmdstableptr) {
		NV_ERROR(dev, "Pointer to TMDS table invalid\n");
		return -EINVAL;
	}

	NV_INFO(dev, "TMDS table version %d.%d\n",
		bios->data[tmdstableptr] >> 4, bios->data[tmdstableptr] & 0xf);

	/* nv50+ has v2.0, but we don't parse it atm */
	if (bios->data[tmdstableptr] != 0x11)
		return -ENOSYS;

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

struct bit_table {
	const char id;
	int (* const parse_fn)(struct drm_device *, struct nvbios *, struct bit_entry *);
};

#define BIT_TABLE(id, funcid) ((struct bit_table){ id, parse_bit_##funcid##_tbl_entry })

int
bit_table(struct drm_device *dev, u8 id, struct bit_entry *bit)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->vbios;
	u8 entries, *entry;

	if (bios->type != NVBIOS_BIT)
		return -ENODEV;

	entries = bios->data[bios->offset + 10];
	entry   = &bios->data[bios->offset + 12];
	while (entries--) {
		if (entry[0] == id) {
			bit->id = entry[0];
			bit->version = entry[1];
			bit->length = ROM16(entry[2]);
			bit->offset = ROM16(entry[4]);
			bit->data = ROMPTR(dev, entry[4]);
			return 0;
		}

		entry += bios->data[bios->offset + 9];
	}

	return -ENOENT;
}

static int
parse_bit_table(struct nvbios *bios, const uint16_t bitoffset,
		struct bit_table *table)
{
	struct drm_device *dev = bios->dev;
	struct bit_entry bitentry;

	if (bit_table(dev, table->id, &bitentry) == 0)
		return table->parse_fn(dev, bios, &bitentry);

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
	bios->digital_min_front_porch = 0x4b;
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

void *
dcb_table(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u8 *dcb = NULL;

	if (dev_priv->card_type > NV_04)
		dcb = ROMPTR(dev, dev_priv->vbios.data[0x36]);
	if (!dcb) {
		NV_WARNONCE(dev, "No DCB data found in VBIOS\n");
		return NULL;
	}

	if (dcb[0] >= 0x41) {
		NV_WARNONCE(dev, "DCB version 0x%02x unknown\n", dcb[0]);
		return NULL;
	} else
	if (dcb[0] >= 0x30) {
		if (ROM32(dcb[6]) == 0x4edcbdcb)
			return dcb;
	} else
	if (dcb[0] >= 0x20) {
		if (ROM32(dcb[4]) == 0x4edcbdcb)
			return dcb;
	} else
	if (dcb[0] >= 0x15) {
		if (!memcmp(&dcb[-7], "DEV_REC", 7))
			return dcb;
	} else {
		/*
		 * v1.4 (some NV15/16, NV11+) seems the same as v1.5, but
		 * always has the same single (crt) entry, even when tv-out
		 * present, so the conclusion is this version cannot really
		 * be used.
		 *
		 * v1.2 tables (some NV6/10, and NV15+) normally have the
		 * same 5 entries, which are not specific to the card and so
		 * no use.
		 *
		 * v1.2 does have an I2C table that read_dcb_i2c_table can
		 * handle, but cards exist (nv11 in #14821) with a bad i2c
		 * table pointer, so use the indices parsed in
		 * parse_bmp_structure.
		 *
		 * v1.1 (NV5+, maybe some NV4) is entirely unhelpful
		 */
		NV_WARNONCE(dev, "No useful DCB data in VBIOS\n");
		return NULL;
	}

	NV_WARNONCE(dev, "DCB header validation failed\n");
	return NULL;
}

void *
dcb_outp(struct drm_device *dev, u8 idx)
{
	u8 *dcb = dcb_table(dev);
	if (dcb && dcb[0] >= 0x30) {
		if (idx < dcb[2])
			return dcb + dcb[1] + (idx * dcb[3]);
	} else
	if (dcb && dcb[0] >= 0x20) {
		u8 *i2c = ROMPTR(dev, dcb[2]);
		u8 *ent = dcb + 8 + (idx * 8);
		if (i2c && ent < i2c)
			return ent;
	} else
	if (dcb && dcb[0] >= 0x15) {
		u8 *i2c = ROMPTR(dev, dcb[2]);
		u8 *ent = dcb + 4 + (idx * 10);
		if (i2c && ent < i2c)
			return ent;
	}

	return NULL;
}

int
dcb_outp_foreach(struct drm_device *dev, void *data,
		 int (*exec)(struct drm_device *, void *, int idx, u8 *outp))
{
	int ret, idx = -1;
	u8 *outp = NULL;
	while ((outp = dcb_outp(dev, ++idx))) {
		if (ROM32(outp[0]) == 0x00000000)
			break; /* seen on an NV11 with DCB v1.5 */
		if (ROM32(outp[0]) == 0xffffffff)
			break; /* seen on an NV17 with DCB v2.0 */

		if ((outp[0] & 0x0f) == OUTPUT_UNUSED)
			continue;
		if ((outp[0] & 0x0f) == OUTPUT_EOL)
			break;

		ret = exec(dev, data, idx, outp);
		if (ret)
			return ret;
	}

	return 0;
}

u8 *
dcb_conntab(struct drm_device *dev)
{
	u8 *dcb = dcb_table(dev);
	if (dcb && dcb[0] >= 0x30 && dcb[1] >= 0x16) {
		u8 *conntab = ROMPTR(dev, dcb[0x14]);
		if (conntab && conntab[0] >= 0x30 && conntab[0] <= 0x40)
			return conntab;
	}
	return NULL;
}

u8 *
dcb_conn(struct drm_device *dev, u8 idx)
{
	u8 *conntab = dcb_conntab(dev);
	if (conntab && idx < conntab[2])
		return conntab + conntab[1] + (idx * conntab[3]);
	return NULL;
}

static struct dcb_entry *new_dcb_entry(struct dcb_table *dcb)
{
	struct dcb_entry *entry = &dcb->entry[dcb->entries];

	memset(entry, 0, sizeof(struct dcb_entry));
	entry->index = dcb->entries++;

	return entry;
}

static void fabricate_dcb_output(struct dcb_table *dcb, int type, int i2c,
				 int heads, int or)
{
	struct dcb_entry *entry = new_dcb_entry(dcb);

	entry->type = type;
	entry->i2c_index = i2c;
	entry->heads = heads;
	if (type != OUTPUT_ANALOG)
		entry->location = !DCB_LOC_ON_CHIP; /* ie OFF CHIP */
	entry->or = or;
}

static bool
parse_dcb20_entry(struct drm_device *dev, struct dcb_table *dcb,
		  uint32_t conn, uint32_t conf, struct dcb_entry *entry)
{
	entry->type = conn & 0xf;
	entry->i2c_index = (conn >> 4) & 0xf;
	entry->heads = (conn >> 8) & 0xf;
	entry->connector = (conn >> 12) & 0xf;
	entry->bus = (conn >> 16) & 0xf;
	entry->location = (conn >> 20) & 0x3;
	entry->or = (conn >> 24) & 0xf;

	switch (entry->type) {
	case OUTPUT_ANALOG:
		/*
		 * Although the rest of a CRT conf dword is usually
		 * zeros, mac biosen have stuff there so we must mask
		 */
		entry->crtconf.maxfreq = (dcb->version < 0x30) ?
					 (conf & 0xffff) * 10 :
					 (conf & 0xff) * 10000;
		break;
	case OUTPUT_LVDS:
		{
		uint32_t mask;
		if (conf & 0x1)
			entry->lvdsconf.use_straps_for_mode = true;
		if (dcb->version < 0x22) {
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
			mask = ~0x7;
			if (conf & 0x2)
				entry->lvdsconf.use_acpi_for_edid = true;
			if (conf & 0x4)
				entry->lvdsconf.use_power_scripts = true;
			entry->lvdsconf.sor.link = (conf & 0x00000030) >> 4;
		}
		if (conf & mask) {
			/*
			 * Until we even try to use these on G8x, it's
			 * useless reporting unknown bits.  They all are.
			 */
			if (dcb->version >= 0x40)
				break;

			NV_ERROR(dev, "Unknown LVDS configuration bits, "
				      "please report\n");
		}
		break;
		}
	case OUTPUT_TV:
	{
		if (dcb->version >= 0x30)
			entry->tvconf.has_component_output = conf & (0x8 << 4);
		else
			entry->tvconf.has_component_output = false;

		break;
	}
	case OUTPUT_DP:
		entry->dpconf.sor.link = (conf & 0x00000030) >> 4;
		switch ((conf & 0x00e00000) >> 21) {
		case 0:
			entry->dpconf.link_bw = 162000;
			break;
		default:
			entry->dpconf.link_bw = 270000;
			break;
		}
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
		if (dcb->version >= 0x40)
			entry->tmdsconf.sor.link = (conf & 0x00000030) >> 4;
		else if (dcb->version >= 0x30)
			entry->tmdsconf.slave_addr = (conf & 0x00000700) >> 8;
		else if (dcb->version >= 0x22)
			entry->tmdsconf.slave_addr = (conf & 0x00000070) >> 4;

		break;
	case OUTPUT_EOL:
		/* weird g80 mobile type that "nv" treats as a terminator */
		dcb->entries--;
		return false;
	default:
		break;
	}

	if (dcb->version < 0x40) {
		/* Normal entries consist of a single bit, but dual link has
		 * the next most significant bit set too
		 */
		entry->duallink_possible =
			((1 << (ffs(entry->or) - 1)) * 3 == entry->or);
	} else {
		entry->duallink_possible = (entry->sorconf.link == 3);
	}

	/* unsure what DCB version introduces this, 3.0? */
	if (conf & 0x100000)
		entry->i2c_upper_default = true;

	return true;
}

static bool
parse_dcb15_entry(struct drm_device *dev, struct dcb_table *dcb,
		  uint32_t conn, uint32_t conf, struct dcb_entry *entry)
{
	switch (conn & 0x0000000f) {
	case 0:
		entry->type = OUTPUT_ANALOG;
		break;
	case 1:
		entry->type = OUTPUT_TV;
		break;
	case 2:
	case 4:
		if (conn & 0x10)
			entry->type = OUTPUT_LVDS;
		else
			entry->type = OUTPUT_TMDS;
		break;
	case 3:
		entry->type = OUTPUT_LVDS;
		break;
	default:
		NV_ERROR(dev, "Unknown DCB type %d\n", conn & 0x0000000f);
		return false;
	}

	entry->i2c_index = (conn & 0x0003c000) >> 14;
	entry->heads = ((conn & 0x001c0000) >> 18) + 1;
	entry->or = entry->heads; /* same as heads, hopefully safe enough */
	entry->location = (conn & 0x01e00000) >> 21;
	entry->bus = (conn & 0x0e000000) >> 25;
	entry->duallink_possible = false;

	switch (entry->type) {
	case OUTPUT_ANALOG:
		entry->crtconf.maxfreq = (conf & 0xffff) * 10;
		break;
	case OUTPUT_TV:
		entry->tvconf.has_component_output = false;
		break;
	case OUTPUT_LVDS:
		if ((conn & 0x00003f00) >> 8 != 0x10)
			entry->lvdsconf.use_straps_for_mode = true;
		entry->lvdsconf.use_power_scripts = true;
		break;
	default:
		break;
	}

	return true;
}

static
void merge_like_dcb_entries(struct drm_device *dev, struct dcb_table *dcb)
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

static bool
apply_dcb_encoder_quirks(struct drm_device *dev, int idx, u32 *conn, u32 *conf)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct dcb_table *dcb = &dev_priv->vbios.dcb;

	/* Dell Precision M6300
	 *   DCB entry 2: 02025312 00000010
	 *   DCB entry 3: 02026312 00000020
	 *
	 * Identical, except apparently a different connector on a
	 * different SOR link.  Not a clue how we're supposed to know
	 * which one is in use if it even shares an i2c line...
	 *
	 * Ignore the connector on the second SOR link to prevent
	 * nasty problems until this is sorted (assuming it's not a
	 * VBIOS bug).
	 */
	if (nv_match_device(dev, 0x040d, 0x1028, 0x019b)) {
		if (*conn == 0x02026312 && *conf == 0x00000020)
			return false;
	}

	/* GeForce3 Ti 200
	 *
	 * DCB reports an LVDS output that should be TMDS:
	 *   DCB entry 1: f2005014 ffffffff
	 */
	if (nv_match_device(dev, 0x0201, 0x1462, 0x8851)) {
		if (*conn == 0xf2005014 && *conf == 0xffffffff) {
			fabricate_dcb_output(dcb, OUTPUT_TMDS, 1, 1, 1);
			return false;
		}
	}

	/* XFX GT-240X-YA
	 *
	 * So many things wrong here, replace the entire encoder table..
	 */
	if (nv_match_device(dev, 0x0ca3, 0x1682, 0x3003)) {
		if (idx == 0) {
			*conn = 0x02001300; /* VGA, connector 1 */
			*conf = 0x00000028;
		} else
		if (idx == 1) {
			*conn = 0x01010312; /* DVI, connector 0 */
			*conf = 0x00020030;
		} else
		if (idx == 2) {
			*conn = 0x01010310; /* VGA, connector 0 */
			*conf = 0x00000028;
		} else
		if (idx == 3) {
			*conn = 0x02022362; /* HDMI, connector 2 */
			*conf = 0x00020010;
		} else {
			*conn = 0x0000000e; /* EOL */
			*conf = 0x00000000;
		}
	}

	/* Some other twisted XFX board (rhbz#694914)
	 *
	 * The DVI/VGA encoder combo that's supposed to represent the
	 * DVI-I connector actually point at two different ones, and
	 * the HDMI connector ends up paired with the VGA instead.
	 *
	 * Connector table is missing anything for VGA at all, pointing it
	 * an invalid conntab entry 2 so we figure it out ourself.
	 */
	if (nv_match_device(dev, 0x0615, 0x1682, 0x2605)) {
		if (idx == 0) {
			*conn = 0x02002300; /* VGA, connector 2 */
			*conf = 0x00000028;
		} else
		if (idx == 1) {
			*conn = 0x01010312; /* DVI, connector 0 */
			*conf = 0x00020030;
		} else
		if (idx == 2) {
			*conn = 0x04020310; /* VGA, connector 0 */
			*conf = 0x00000028;
		} else
		if (idx == 3) {
			*conn = 0x02021322; /* HDMI, connector 1 */
			*conf = 0x00020010;
		} else {
			*conn = 0x0000000e; /* EOL */
			*conf = 0x00000000;
		}
	}

	return true;
}

static void
fabricate_dcb_encoder_table(struct drm_device *dev, struct nvbios *bios)
{
	struct dcb_table *dcb = &bios->dcb;
	int all_heads = (nv_two_heads(dev) ? 3 : 1);

#ifdef __powerpc__
	/* Apple iMac G4 NV17 */
	if (of_machine_is_compatible("PowerMac4,5")) {
		fabricate_dcb_output(dcb, OUTPUT_TMDS, 0, all_heads, 1);
		fabricate_dcb_output(dcb, OUTPUT_ANALOG, 1, all_heads, 2);
		return;
	}
#endif

	/* Make up some sane defaults */
	fabricate_dcb_output(dcb, OUTPUT_ANALOG,
			     bios->legacy.i2c_indices.crt, 1, 1);

	if (nv04_tv_identify(dev, bios->legacy.i2c_indices.tv) >= 0)
		fabricate_dcb_output(dcb, OUTPUT_TV,
				     bios->legacy.i2c_indices.tv,
				     all_heads, 0);

	else if (bios->tmds.output0_script_ptr ||
		 bios->tmds.output1_script_ptr)
		fabricate_dcb_output(dcb, OUTPUT_TMDS,
				     bios->legacy.i2c_indices.panel,
				     all_heads, 1);
}

static int
parse_dcb_entry(struct drm_device *dev, void *data, int idx, u8 *outp)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct dcb_table *dcb = &dev_priv->vbios.dcb;
	u32 conf = (dcb->version >= 0x20) ? ROM32(outp[4]) : ROM32(outp[6]);
	u32 conn = ROM32(outp[0]);
	bool ret;

	if (apply_dcb_encoder_quirks(dev, idx, &conn, &conf)) {
		struct dcb_entry *entry = new_dcb_entry(dcb);

		NV_TRACEWARN(dev, "DCB outp %02d: %08x %08x\n", idx, conn, conf);

		if (dcb->version >= 0x20)
			ret = parse_dcb20_entry(dev, dcb, conn, conf, entry);
		else
			ret = parse_dcb15_entry(dev, dcb, conn, conf, entry);
		if (!ret)
			return 1; /* stop parsing */

		/* Ignore the I2C index for on-chip TV-out, as there
		 * are cards with bogus values (nv31m in bug 23212),
		 * and it's otherwise useless.
		 */
		if (entry->type == OUTPUT_TV &&
		    entry->location == DCB_LOC_ON_CHIP)
			entry->i2c_index = 0x0f;
	}

	return 0;
}

static void
dcb_fake_connectors(struct nvbios *bios)
{
	struct dcb_table *dcbt = &bios->dcb;
	u8 map[16] = { };
	int i, idx = 0;

	/* heuristic: if we ever get a non-zero connector field, assume
	 * that all the indices are valid and we don't need fake them.
	 */
	for (i = 0; i < dcbt->entries; i++) {
		if (dcbt->entry[i].connector)
			return;
	}

	/* no useful connector info available, we need to make it up
	 * ourselves.  the rule here is: anything on the same i2c bus
	 * is considered to be on the same connector.  any output
	 * without an associated i2c bus is assigned its own unique
	 * connector index.
	 */
	for (i = 0; i < dcbt->entries; i++) {
		u8 i2c = dcbt->entry[i].i2c_index;
		if (i2c == 0x0f) {
			dcbt->entry[i].connector = idx++;
		} else {
			if (!map[i2c])
				map[i2c] = ++idx;
			dcbt->entry[i].connector = map[i2c] - 1;
		}
	}

	/* if we created more than one connector, destroy the connector
	 * table - just in case it has random, rather than stub, entries.
	 */
	if (i > 1) {
		u8 *conntab = dcb_conntab(bios->dev);
		if (conntab)
			conntab[0] = 0x00;
	}
}

static int
parse_dcb_table(struct drm_device *dev, struct nvbios *bios)
{
	struct dcb_table *dcb = &bios->dcb;
	u8 *dcbt, *conn;
	int idx;

	dcbt = dcb_table(dev);
	if (!dcbt) {
		/* handle pre-DCB boards */
		if (bios->type == NVBIOS_BMP) {
			fabricate_dcb_encoder_table(dev, bios);
			return 0;
		}

		return -EINVAL;
	}

	NV_TRACE(dev, "DCB version %d.%d\n", dcbt[0] >> 4, dcbt[0] & 0xf);

	dcb->version = dcbt[0];
	dcb_outp_foreach(dev, NULL, parse_dcb_entry);

	/*
	 * apart for v2.1+ not being known for requiring merging, this
	 * guarantees dcbent->index is the index of the entry in the rom image
	 */
	if (dcb->version < 0x21)
		merge_like_dcb_entries(dev, dcb);

	if (!dcb->entries)
		return -ENXIO;

	/* dump connector table entries to log, if any exist */
	idx = -1;
	while ((conn = dcb_conn(dev, ++idx))) {
		if (conn[0] != 0xff) {
			NV_TRACE(dev, "DCB conn %02d: ", idx);
			if (dcb_conntab(dev)[3] < 4)
				printk("%04x\n", ROM16(conn[0]));
			else
				printk("%08x\n", ROM32(conn[0]));
		}
	}
	dcb_fake_connectors(bios);
	return 0;
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
	struct nvbios *bios = &dev_priv->vbios;
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
			    struct dcb_entry *dcbent, int crtc)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->vbios;
	struct init_exec iexec = { true, false };

	spin_lock_bh(&bios->lock);
	bios->display.output = dcbent;
	bios->display.crtc = crtc;
	parse_init_table(bios, table, &iexec);
	bios->display.output = NULL;
	spin_unlock_bh(&bios->lock);
}

void
nouveau_bios_init_exec(struct drm_device *dev, uint16_t table)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->vbios;
	struct init_exec iexec = { true, false };

	parse_init_table(bios, table, &iexec);
}

static bool NVInitVBIOS(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->vbios;

	memset(bios, 0, sizeof(struct nvbios));
	spin_lock_init(&bios->lock);
	bios->dev = dev;

	return bios_shadow(dev);
}

static int nouveau_parse_vbios_struct(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->vbios;
	const uint8_t bit_signature[] = { 0xff, 0xb8, 'B', 'I', 'T' };
	const uint8_t bmp_signature[] = { 0xff, 0x7f, 'N', 'V', 0x0 };
	int offset;

	offset = findstr(bios->data, bios->length,
					bit_signature, sizeof(bit_signature));
	if (offset) {
		NV_TRACE(dev, "BIT BIOS found\n");
		bios->type = NVBIOS_BIT;
		bios->offset = offset;
		return parse_bit_structure(bios, offset + 6);
	}

	offset = findstr(bios->data, bios->length,
					bmp_signature, sizeof(bmp_signature));
	if (offset) {
		NV_TRACE(dev, "BMP BIOS found\n");
		bios->type = NVBIOS_BMP;
		bios->offset = offset;
		return parse_bmp_structure(dev, bios, offset);
	}

	NV_ERROR(dev, "No known BIOS signature found\n");
	return -ENODEV;
}

int
nouveau_run_vbios_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->vbios;
	int i, ret = 0;

	/* Reset the BIOS head to 0. */
	bios->state.crtchead = 0;

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
		for (i = 0; i < bios->dcb.entries; i++) {
			nouveau_bios_run_display_table(dev, 0, 0,
						       &bios->dcb.entry[i], -1);
		}
	}

	return ret;
}

static bool
nouveau_bios_posted(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	unsigned htotal;

	if (dev_priv->card_type >= NV_50) {
		if (NVReadVgaCrtc(dev, 0, 0x00) == 0 &&
		    NVReadVgaCrtc(dev, 0, 0x1a) == 0)
			return false;
		return true;
	}

	htotal  = NVReadVgaCrtc(dev, 0, 0x06);
	htotal |= (NVReadVgaCrtc(dev, 0, 0x07) & 0x01) << 8;
	htotal |= (NVReadVgaCrtc(dev, 0, 0x07) & 0x20) << 4;
	htotal |= (NVReadVgaCrtc(dev, 0, 0x25) & 0x01) << 10;
	htotal |= (NVReadVgaCrtc(dev, 0, 0x41) & 0x01) << 11;

	return (htotal != 0);
}

int
nouveau_bios_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->vbios;
	int ret;

	if (!NVInitVBIOS(dev))
		return -ENODEV;

	ret = nouveau_parse_vbios_struct(dev);
	if (ret)
		return ret;

	ret = nouveau_i2c_init(dev);
	if (ret)
		return ret;

	ret = nouveau_mxm_init(dev);
	if (ret)
		return ret;

	ret = parse_dcb_table(dev, bios);
	if (ret)
		return ret;

	if (!bios->major_version)	/* we don't run version 0 bios */
		return 0;

	/* init script execution disabled */
	bios->execute = false;

	/* ... unless card isn't POSTed already */
	if (!nouveau_bios_posted(dev)) {
		NV_INFO(dev, "Adaptor not initialised, "
			"running VBIOS init tables.\n");
		bios->execute = true;
	}
	if (nouveau_force_post)
		bios->execute = true;

	ret = nouveau_run_vbios_init(dev);
	if (ret)
		return ret;

	/* feature_byte on BMP is poor, but init always sets CR4B */
	if (bios->major_version < 5)
		bios->is_mobile = NVReadVgaCrtc(dev, 0, NV_CIO_CRE_4B) & 0x40;

	/* all BIT systems need p_f_m_t for digital_min_front_porch */
	if (bios->is_mobile || bios->major_version >= 5)
		ret = parse_fp_mode_table(dev, bios);

	/* allow subsequent scripts to execute */
	bios->execute = true;

	return 0;
}

void
nouveau_bios_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	nouveau_mxm_fini(dev);
	nouveau_i2c_fini(dev);

	kfree(dev_priv->vbios.data);
}
