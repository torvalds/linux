/*
 * Marvell hostbridge routines
 *
 * Author: Mark A. Greer <source@mvista.com>
 *
 * 2004, 2005, 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <stdarg.h>
#include <stddef.h>
#include "types.h"
#include "elf.h"
#include "page.h"
#include "string.h"
#include "stdio.h"
#include "io.h"
#include "ops.h"
#include "mv64x60.h"

#define PCI_DEVFN(slot,func)	((((slot) & 0x1f) << 3) | ((func) & 0x07))

#define MV64x60_CPU2MEM_WINDOWS			4
#define MV64x60_CPU2MEM_0_BASE			0x0008
#define MV64x60_CPU2MEM_0_SIZE			0x0010
#define MV64x60_CPU2MEM_1_BASE			0x0208
#define MV64x60_CPU2MEM_1_SIZE			0x0210
#define MV64x60_CPU2MEM_2_BASE			0x0018
#define MV64x60_CPU2MEM_2_SIZE			0x0020
#define MV64x60_CPU2MEM_3_BASE			0x0218
#define MV64x60_CPU2MEM_3_SIZE			0x0220

#define MV64x60_ENET2MEM_BAR_ENABLE		0x2290
#define MV64x60_ENET2MEM_0_BASE			0x2200
#define MV64x60_ENET2MEM_0_SIZE			0x2204
#define MV64x60_ENET2MEM_1_BASE			0x2208
#define MV64x60_ENET2MEM_1_SIZE			0x220c
#define MV64x60_ENET2MEM_2_BASE			0x2210
#define MV64x60_ENET2MEM_2_SIZE			0x2214
#define MV64x60_ENET2MEM_3_BASE			0x2218
#define MV64x60_ENET2MEM_3_SIZE			0x221c
#define MV64x60_ENET2MEM_4_BASE			0x2220
#define MV64x60_ENET2MEM_4_SIZE			0x2224
#define MV64x60_ENET2MEM_5_BASE			0x2228
#define MV64x60_ENET2MEM_5_SIZE			0x222c
#define MV64x60_ENET2MEM_ACC_PROT_0		0x2294
#define MV64x60_ENET2MEM_ACC_PROT_1		0x2298
#define MV64x60_ENET2MEM_ACC_PROT_2		0x229c

#define MV64x60_MPSC2MEM_BAR_ENABLE		0xf250
#define MV64x60_MPSC2MEM_0_BASE			0xf200
#define MV64x60_MPSC2MEM_0_SIZE			0xf204
#define MV64x60_MPSC2MEM_1_BASE			0xf208
#define MV64x60_MPSC2MEM_1_SIZE			0xf20c
#define MV64x60_MPSC2MEM_2_BASE			0xf210
#define MV64x60_MPSC2MEM_2_SIZE			0xf214
#define MV64x60_MPSC2MEM_3_BASE			0xf218
#define MV64x60_MPSC2MEM_3_SIZE			0xf21c
#define MV64x60_MPSC_0_REMAP			0xf240
#define MV64x60_MPSC_1_REMAP			0xf244
#define MV64x60_MPSC2MEM_ACC_PROT_0		0xf254
#define MV64x60_MPSC2MEM_ACC_PROT_1		0xf258
#define MV64x60_MPSC2REGS_BASE			0xf25c

#define MV64x60_IDMA2MEM_BAR_ENABLE		0x0a80
#define MV64x60_IDMA2MEM_0_BASE			0x0a00
#define MV64x60_IDMA2MEM_0_SIZE			0x0a04
#define MV64x60_IDMA2MEM_1_BASE			0x0a08
#define MV64x60_IDMA2MEM_1_SIZE			0x0a0c
#define MV64x60_IDMA2MEM_2_BASE			0x0a10
#define MV64x60_IDMA2MEM_2_SIZE			0x0a14
#define MV64x60_IDMA2MEM_3_BASE			0x0a18
#define MV64x60_IDMA2MEM_3_SIZE			0x0a1c
#define MV64x60_IDMA2MEM_4_BASE			0x0a20
#define MV64x60_IDMA2MEM_4_SIZE			0x0a24
#define MV64x60_IDMA2MEM_5_BASE			0x0a28
#define MV64x60_IDMA2MEM_5_SIZE			0x0a2c
#define MV64x60_IDMA2MEM_6_BASE			0x0a30
#define MV64x60_IDMA2MEM_6_SIZE			0x0a34
#define MV64x60_IDMA2MEM_7_BASE			0x0a38
#define MV64x60_IDMA2MEM_7_SIZE			0x0a3c
#define MV64x60_IDMA2MEM_ACC_PROT_0		0x0a70
#define MV64x60_IDMA2MEM_ACC_PROT_1		0x0a74
#define MV64x60_IDMA2MEM_ACC_PROT_2		0x0a78
#define MV64x60_IDMA2MEM_ACC_PROT_3		0x0a7c

#define MV64x60_PCI_ACC_CNTL_WINDOWS		6
#define MV64x60_PCI0_PCI_DECODE_CNTL		0x0d3c
#define MV64x60_PCI1_PCI_DECODE_CNTL		0x0dbc

#define MV64x60_PCI0_BAR_ENABLE			0x0c3c
#define MV64x60_PCI02MEM_0_SIZE			0x0c08
#define MV64x60_PCI0_ACC_CNTL_0_BASE_LO		0x1e00
#define MV64x60_PCI0_ACC_CNTL_0_BASE_HI		0x1e04
#define MV64x60_PCI0_ACC_CNTL_0_SIZE		0x1e08
#define MV64x60_PCI0_ACC_CNTL_1_BASE_LO		0x1e10
#define MV64x60_PCI0_ACC_CNTL_1_BASE_HI		0x1e14
#define MV64x60_PCI0_ACC_CNTL_1_SIZE		0x1e18
#define MV64x60_PCI0_ACC_CNTL_2_BASE_LO		0x1e20
#define MV64x60_PCI0_ACC_CNTL_2_BASE_HI		0x1e24
#define MV64x60_PCI0_ACC_CNTL_2_SIZE		0x1e28
#define MV64x60_PCI0_ACC_CNTL_3_BASE_LO		0x1e30
#define MV64x60_PCI0_ACC_CNTL_3_BASE_HI		0x1e34
#define MV64x60_PCI0_ACC_CNTL_3_SIZE		0x1e38
#define MV64x60_PCI0_ACC_CNTL_4_BASE_LO		0x1e40
#define MV64x60_PCI0_ACC_CNTL_4_BASE_HI		0x1e44
#define MV64x60_PCI0_ACC_CNTL_4_SIZE		0x1e48
#define MV64x60_PCI0_ACC_CNTL_5_BASE_LO		0x1e50
#define MV64x60_PCI0_ACC_CNTL_5_BASE_HI		0x1e54
#define MV64x60_PCI0_ACC_CNTL_5_SIZE		0x1e58

#define MV64x60_PCI1_BAR_ENABLE			0x0cbc
#define MV64x60_PCI12MEM_0_SIZE			0x0c88
#define MV64x60_PCI1_ACC_CNTL_0_BASE_LO		0x1e80
#define MV64x60_PCI1_ACC_CNTL_0_BASE_HI		0x1e84
#define MV64x60_PCI1_ACC_CNTL_0_SIZE		0x1e88
#define MV64x60_PCI1_ACC_CNTL_1_BASE_LO		0x1e90
#define MV64x60_PCI1_ACC_CNTL_1_BASE_HI		0x1e94
#define MV64x60_PCI1_ACC_CNTL_1_SIZE		0x1e98
#define MV64x60_PCI1_ACC_CNTL_2_BASE_LO		0x1ea0
#define MV64x60_PCI1_ACC_CNTL_2_BASE_HI		0x1ea4
#define MV64x60_PCI1_ACC_CNTL_2_SIZE		0x1ea8
#define MV64x60_PCI1_ACC_CNTL_3_BASE_LO		0x1eb0
#define MV64x60_PCI1_ACC_CNTL_3_BASE_HI		0x1eb4
#define MV64x60_PCI1_ACC_CNTL_3_SIZE		0x1eb8
#define MV64x60_PCI1_ACC_CNTL_4_BASE_LO		0x1ec0
#define MV64x60_PCI1_ACC_CNTL_4_BASE_HI		0x1ec4
#define MV64x60_PCI1_ACC_CNTL_4_SIZE		0x1ec8
#define MV64x60_PCI1_ACC_CNTL_5_BASE_LO		0x1ed0
#define MV64x60_PCI1_ACC_CNTL_5_BASE_HI		0x1ed4
#define MV64x60_PCI1_ACC_CNTL_5_SIZE		0x1ed8

#define MV64x60_CPU2PCI_SWAP_NONE		0x01000000

#define MV64x60_CPU2PCI0_IO_BASE		0x0048
#define MV64x60_CPU2PCI0_IO_SIZE		0x0050
#define MV64x60_CPU2PCI0_IO_REMAP		0x00f0
#define MV64x60_CPU2PCI0_MEM_0_BASE		0x0058
#define MV64x60_CPU2PCI0_MEM_0_SIZE		0x0060
#define MV64x60_CPU2PCI0_MEM_0_REMAP_LO		0x00f8
#define MV64x60_CPU2PCI0_MEM_0_REMAP_HI		0x0320

#define MV64x60_CPU2PCI1_IO_BASE		0x0090
#define MV64x60_CPU2PCI1_IO_SIZE		0x0098
#define MV64x60_CPU2PCI1_IO_REMAP		0x0108
#define MV64x60_CPU2PCI1_MEM_0_BASE		0x00a0
#define MV64x60_CPU2PCI1_MEM_0_SIZE		0x00a8
#define MV64x60_CPU2PCI1_MEM_0_REMAP_LO		0x0110
#define MV64x60_CPU2PCI1_MEM_0_REMAP_HI		0x0340

struct mv64x60_mem_win {
	u32 hi;
	u32 lo;
	u32 size;
};

struct mv64x60_pci_win {
	u32 fcn;
	u32 hi;
	u32 lo;
	u32 size;
};

/* PCI config access routines */
struct {
	u32 addr;
	u32 data;
} static mv64x60_pci_cfgio[2] = {
	{ /* hose 0 */
		.addr	= 0xcf8,
		.data	= 0xcfc,
	},
	{ /* hose 1 */
		.addr	= 0xc78,
		.data	= 0xc7c,
	}
};

u32 mv64x60_cfg_read(u8 *bridge_base, u8 hose, u8 bus, u8 devfn, u8 offset)
{
	out_le32((u32 *)(bridge_base + mv64x60_pci_cfgio[hose].addr),
			(1 << 31) | (bus << 16) | (devfn << 8) | offset);
	return in_le32((u32 *)(bridge_base + mv64x60_pci_cfgio[hose].data));
}

void mv64x60_cfg_write(u8 *bridge_base, u8 hose, u8 bus, u8 devfn, u8 offset,
		u32 val)
{
	out_le32((u32 *)(bridge_base + mv64x60_pci_cfgio[hose].addr),
			(1 << 31) | (bus << 16) | (devfn << 8) | offset);
	out_le32((u32 *)(bridge_base + mv64x60_pci_cfgio[hose].data), val);
}

/* I/O ctlr -> system memory setup */
static struct mv64x60_mem_win mv64x60_cpu2mem[MV64x60_CPU2MEM_WINDOWS] = {
	{
		.lo	= MV64x60_CPU2MEM_0_BASE,
		.size	= MV64x60_CPU2MEM_0_SIZE,
	},
	{
		.lo	= MV64x60_CPU2MEM_1_BASE,
		.size	= MV64x60_CPU2MEM_1_SIZE,
	},
	{
		.lo	= MV64x60_CPU2MEM_2_BASE,
		.size	= MV64x60_CPU2MEM_2_SIZE,
	},
	{
		.lo	= MV64x60_CPU2MEM_3_BASE,
		.size	= MV64x60_CPU2MEM_3_SIZE,
	},
};

static struct mv64x60_mem_win mv64x60_enet2mem[MV64x60_CPU2MEM_WINDOWS] = {
	{
		.lo	= MV64x60_ENET2MEM_0_BASE,
		.size	= MV64x60_ENET2MEM_0_SIZE,
	},
	{
		.lo	= MV64x60_ENET2MEM_1_BASE,
		.size	= MV64x60_ENET2MEM_1_SIZE,
	},
	{
		.lo	= MV64x60_ENET2MEM_2_BASE,
		.size	= MV64x60_ENET2MEM_2_SIZE,
	},
	{
		.lo	= MV64x60_ENET2MEM_3_BASE,
		.size	= MV64x60_ENET2MEM_3_SIZE,
	},
};

static struct mv64x60_mem_win mv64x60_mpsc2mem[MV64x60_CPU2MEM_WINDOWS] = {
	{
		.lo	= MV64x60_MPSC2MEM_0_BASE,
		.size	= MV64x60_MPSC2MEM_0_SIZE,
	},
	{
		.lo	= MV64x60_MPSC2MEM_1_BASE,
		.size	= MV64x60_MPSC2MEM_1_SIZE,
	},
	{
		.lo	= MV64x60_MPSC2MEM_2_BASE,
		.size	= MV64x60_MPSC2MEM_2_SIZE,
	},
	{
		.lo	= MV64x60_MPSC2MEM_3_BASE,
		.size	= MV64x60_MPSC2MEM_3_SIZE,
	},
};

static struct mv64x60_mem_win mv64x60_idma2mem[MV64x60_CPU2MEM_WINDOWS] = {
	{
		.lo	= MV64x60_IDMA2MEM_0_BASE,
		.size	= MV64x60_IDMA2MEM_0_SIZE,
	},
	{
		.lo	= MV64x60_IDMA2MEM_1_BASE,
		.size	= MV64x60_IDMA2MEM_1_SIZE,
	},
	{
		.lo	= MV64x60_IDMA2MEM_2_BASE,
		.size	= MV64x60_IDMA2MEM_2_SIZE,
	},
	{
		.lo	= MV64x60_IDMA2MEM_3_BASE,
		.size	= MV64x60_IDMA2MEM_3_SIZE,
	},
};

static u32 mv64x60_dram_selects[MV64x60_CPU2MEM_WINDOWS] = {0xe,0xd,0xb,0x7};

/*
 * ENET, MPSC, and IDMA ctlrs on the MV64x60 have separate windows that
 * must be set up so that the respective ctlr can access system memory.
 * Configure them to be same as cpu->memory windows.
 */
void mv64x60_config_ctlr_windows(u8 *bridge_base, u8 *bridge_pbase,
		u8 is_coherent)
{
	u32 i, base, size, enables, prot = 0, snoop_bits = 0;

	/* Disable ctlr->mem windows */
	out_le32((u32 *)(bridge_base + MV64x60_ENET2MEM_BAR_ENABLE), 0x3f);
	out_le32((u32 *)(bridge_base + MV64x60_MPSC2MEM_BAR_ENABLE), 0xf);
	out_le32((u32 *)(bridge_base + MV64x60_ENET2MEM_BAR_ENABLE), 0xff);

	if (is_coherent)
		snoop_bits = 0x2 << 12; /* Writeback */

	enables = in_le32((u32 *)(bridge_base + MV64x60_CPU_BAR_ENABLE)) & 0xf;

	for (i=0; i<MV64x60_CPU2MEM_WINDOWS; i++) {
		if (enables & (1 << i)) /* Set means disabled */
			continue;

		base = in_le32((u32 *)(bridge_base + mv64x60_cpu2mem[i].lo))
			<< 16;
		base |= snoop_bits | (mv64x60_dram_selects[i] << 8);
		size = in_le32((u32 *)(bridge_base + mv64x60_cpu2mem[i].size))
			<< 16;
		prot |= (0x3 << (i << 1)); /* RW access */

		out_le32((u32 *)(bridge_base + mv64x60_enet2mem[i].lo), base);
		out_le32((u32 *)(bridge_base + mv64x60_enet2mem[i].size), size);
		out_le32((u32 *)(bridge_base + mv64x60_mpsc2mem[i].lo), base);
		out_le32((u32 *)(bridge_base + mv64x60_mpsc2mem[i].size), size);
		out_le32((u32 *)(bridge_base + mv64x60_idma2mem[i].lo), base);
		out_le32((u32 *)(bridge_base + mv64x60_idma2mem[i].size), size);
	}

	out_le32((u32 *)(bridge_base + MV64x60_ENET2MEM_ACC_PROT_0), prot);
	out_le32((u32 *)(bridge_base + MV64x60_ENET2MEM_ACC_PROT_1), prot);
	out_le32((u32 *)(bridge_base + MV64x60_ENET2MEM_ACC_PROT_2), prot);
	out_le32((u32 *)(bridge_base + MV64x60_MPSC2MEM_ACC_PROT_0), prot);
	out_le32((u32 *)(bridge_base + MV64x60_MPSC2MEM_ACC_PROT_1), prot);
	out_le32((u32 *)(bridge_base + MV64x60_IDMA2MEM_ACC_PROT_0), prot);
	out_le32((u32 *)(bridge_base + MV64x60_IDMA2MEM_ACC_PROT_1), prot);
	out_le32((u32 *)(bridge_base + MV64x60_IDMA2MEM_ACC_PROT_2), prot);
	out_le32((u32 *)(bridge_base + MV64x60_IDMA2MEM_ACC_PROT_3), prot);

	/* Set mpsc->bridge's reg window to the bridge's internal registers. */
	out_le32((u32 *)(bridge_base + MV64x60_MPSC2REGS_BASE),
			(u32)bridge_pbase);

	out_le32((u32 *)(bridge_base + MV64x60_ENET2MEM_BAR_ENABLE), enables);
	out_le32((u32 *)(bridge_base + MV64x60_MPSC2MEM_BAR_ENABLE), enables);
	out_le32((u32 *)(bridge_base + MV64x60_IDMA2MEM_BAR_ENABLE), enables);
}

/* PCI MEM -> system memory, et. al. setup */
static struct mv64x60_pci_win mv64x60_pci2mem[2] = {
	{ /* hose 0 */
		.fcn	= 0,
		.hi	= 0x14,
		.lo	= 0x10,
		.size	= MV64x60_PCI02MEM_0_SIZE,
	},
	{ /* hose 1 */
		.fcn	= 0,
		.hi	= 0x94,
		.lo	= 0x90,
		.size	= MV64x60_PCI12MEM_0_SIZE,
	},
};

static struct
mv64x60_mem_win mv64x60_pci_acc[2][MV64x60_PCI_ACC_CNTL_WINDOWS] = {
	{ /* hose 0 */
		{
			.hi	= MV64x60_PCI0_ACC_CNTL_0_BASE_HI,
			.lo	= MV64x60_PCI0_ACC_CNTL_0_BASE_LO,
			.size	= MV64x60_PCI0_ACC_CNTL_0_SIZE,
		},
		{
			.hi	= MV64x60_PCI0_ACC_CNTL_1_BASE_HI,
			.lo	= MV64x60_PCI0_ACC_CNTL_1_BASE_LO,
			.size	= MV64x60_PCI0_ACC_CNTL_1_SIZE,
		},
		{
			.hi	= MV64x60_PCI0_ACC_CNTL_2_BASE_HI,
			.lo	= MV64x60_PCI0_ACC_CNTL_2_BASE_LO,
			.size	= MV64x60_PCI0_ACC_CNTL_2_SIZE,
		},
		{
			.hi	= MV64x60_PCI0_ACC_CNTL_3_BASE_HI,
			.lo	= MV64x60_PCI0_ACC_CNTL_3_BASE_LO,
			.size	= MV64x60_PCI0_ACC_CNTL_3_SIZE,
		},
	},
	{ /* hose 1 */
		{
			.hi	= MV64x60_PCI1_ACC_CNTL_0_BASE_HI,
			.lo	= MV64x60_PCI1_ACC_CNTL_0_BASE_LO,
			.size	= MV64x60_PCI1_ACC_CNTL_0_SIZE,
		},
		{
			.hi	= MV64x60_PCI1_ACC_CNTL_1_BASE_HI,
			.lo	= MV64x60_PCI1_ACC_CNTL_1_BASE_LO,
			.size	= MV64x60_PCI1_ACC_CNTL_1_SIZE,
		},
		{
			.hi	= MV64x60_PCI1_ACC_CNTL_2_BASE_HI,
			.lo	= MV64x60_PCI1_ACC_CNTL_2_BASE_LO,
			.size	= MV64x60_PCI1_ACC_CNTL_2_SIZE,
		},
		{
			.hi	= MV64x60_PCI1_ACC_CNTL_3_BASE_HI,
			.lo	= MV64x60_PCI1_ACC_CNTL_3_BASE_LO,
			.size	= MV64x60_PCI1_ACC_CNTL_3_SIZE,
		},
	},
};

static struct mv64x60_mem_win mv64x60_pci2reg[2] = {
	{
		.hi	= 0x24,
		.lo	= 0x20,
		.size	= 0,
	},
	{
		.hi	= 0xa4,
		.lo	= 0xa0,
		.size	= 0,
	},
};

/* Only need to use 1 window (per hose) to get access to all of system memory */
void mv64x60_config_pci_windows(u8 *bridge_base, u8 *bridge_pbase, u8 hose,
		u8 bus, u32 mem_size, u32 acc_bits)
{
	u32 i, offset, bar_enable, enables;

	/* Disable all windows but PCI MEM -> Bridge's regs window */
	enables = ~(1 << 9);
	bar_enable = hose ? MV64x60_PCI1_BAR_ENABLE : MV64x60_PCI0_BAR_ENABLE;
	out_le32((u32 *)(bridge_base + bar_enable), enables);

	for (i=0; i<MV64x60_PCI_ACC_CNTL_WINDOWS; i++)
		out_le32((u32 *)(bridge_base + mv64x60_pci_acc[hose][i].lo), 0);

	/* If mem_size is 0, leave windows disabled */
	if (mem_size == 0)
		return;

	/* Cause automatic updates of PCI remap regs */
	offset = hose ?
		MV64x60_PCI1_PCI_DECODE_CNTL : MV64x60_PCI0_PCI_DECODE_CNTL;
	i = in_le32((u32 *)(bridge_base + offset));
	out_le32((u32 *)(bridge_base + offset), i & ~0x1);

	mem_size = (mem_size - 1) & 0xfffff000;

	/* Map PCI MEM addr 0 -> System Mem addr 0 */
	mv64x60_cfg_write(bridge_base, hose, bus,
			PCI_DEVFN(0, mv64x60_pci2mem[hose].fcn),
			mv64x60_pci2mem[hose].hi, 0);
	mv64x60_cfg_write(bridge_base, hose, bus,
			PCI_DEVFN(0, mv64x60_pci2mem[hose].fcn),
			mv64x60_pci2mem[hose].lo, 0);
	out_le32((u32 *)(bridge_base + mv64x60_pci2mem[hose].size),mem_size);

	acc_bits |= MV64x60_PCI_ACC_CNTL_ENABLE;
	out_le32((u32 *)(bridge_base + mv64x60_pci_acc[hose][0].hi), 0);
	out_le32((u32 *)(bridge_base + mv64x60_pci_acc[hose][0].lo), acc_bits);
	out_le32((u32 *)(bridge_base + mv64x60_pci_acc[hose][0].size),mem_size);

	/* Set PCI MEM->bridge's reg window to where they are in CPU mem map */
	i = (u32)bridge_base;
	i &= 0xffff0000;
	i |= (0x2 << 1);
	mv64x60_cfg_write(bridge_base, hose, bus, PCI_DEVFN(0,0),
			mv64x60_pci2reg[hose].hi, 0);
	mv64x60_cfg_write(bridge_base, hose, bus, PCI_DEVFN(0,0),
			mv64x60_pci2reg[hose].lo, i);

	enables &= ~0x1; /* Enable PCI MEM -> System Mem window 0 */
	out_le32((u32 *)(bridge_base + bar_enable), enables);
}

/* CPU -> PCI I/O & MEM setup */
struct mv64x60_cpu2pci_win mv64x60_cpu2pci_io[2] = {
	{ /* hose 0 */
		.lo		= MV64x60_CPU2PCI0_IO_BASE,
		.size		= MV64x60_CPU2PCI0_IO_SIZE,
		.remap_hi	= 0,
		.remap_lo	= MV64x60_CPU2PCI0_IO_REMAP,
	},
	{ /* hose 1 */
		.lo		= MV64x60_CPU2PCI1_IO_BASE,
		.size		= MV64x60_CPU2PCI1_IO_SIZE,
		.remap_hi	= 0,
		.remap_lo	= MV64x60_CPU2PCI1_IO_REMAP,
	},
};

struct mv64x60_cpu2pci_win mv64x60_cpu2pci_mem[2] = {
	{ /* hose 0 */
		.lo		= MV64x60_CPU2PCI0_MEM_0_BASE,
		.size		= MV64x60_CPU2PCI0_MEM_0_SIZE,
		.remap_hi	= MV64x60_CPU2PCI0_MEM_0_REMAP_HI,
		.remap_lo	= MV64x60_CPU2PCI0_MEM_0_REMAP_LO,
	},
	{ /* hose 1 */
		.lo		= MV64x60_CPU2PCI1_MEM_0_BASE,
		.size		= MV64x60_CPU2PCI1_MEM_0_SIZE,
		.remap_hi	= MV64x60_CPU2PCI1_MEM_0_REMAP_HI,
		.remap_lo	= MV64x60_CPU2PCI1_MEM_0_REMAP_LO,
	},
};

/* Only need to set up 1 window to pci mem space */
void mv64x60_config_cpu2pci_window(u8 *bridge_base, u8 hose, u32 pci_base_hi,
		u32 pci_base_lo, u32 cpu_base, u32 size,
		struct mv64x60_cpu2pci_win *offset_tbl)
{
	cpu_base >>= 16;
	cpu_base |= MV64x60_CPU2PCI_SWAP_NONE;
	out_le32((u32 *)(bridge_base + offset_tbl[hose].lo), cpu_base);

	if (offset_tbl[hose].remap_hi != 0)
		out_le32((u32 *)(bridge_base + offset_tbl[hose].remap_hi),
				pci_base_hi);
	out_le32((u32 *)(bridge_base + offset_tbl[hose].remap_lo),
			pci_base_lo >> 16);

	size = (size - 1) >> 16;
	out_le32((u32 *)(bridge_base + offset_tbl[hose].size), size);
}

/* Read mem ctlr to get the amount of mem in system */
u32 mv64x60_get_mem_size(u8 *bridge_base)
{
	u32 enables, i, v;
	u32 mem = 0;

	enables = in_le32((u32 *)(bridge_base + MV64x60_CPU_BAR_ENABLE)) & 0xf;

	for (i=0; i<MV64x60_CPU2MEM_WINDOWS; i++)
		if (!(enables & (1<<i))) {
			v = in_le32((u32*)(bridge_base
						+ mv64x60_cpu2mem[i].size));
			v = ((v & 0xffff) + 1) << 16;
			mem += v;
		}

	return mem;
}

/* Get physical address of bridge's registers */
u8 *mv64x60_get_bridge_pbase(void)
{
	u32 v[2];
	void *devp;

	devp = finddevice("/mv64x60");
	if (devp == NULL)
		goto err_out;
	if (getprop(devp, "reg", v, sizeof(v)) != sizeof(v))
		goto err_out;

	return (u8 *)v[0];

err_out:
	return 0;
}

/* Get virtual address of bridge's registers */
u8 *mv64x60_get_bridge_base(void)
{
	u32 v;
	void *devp;

	devp = finddevice("/mv64x60");
	if (devp == NULL)
		goto err_out;
	if (getprop(devp, "virtual-reg", &v, sizeof(v)) != sizeof(v))
		goto err_out;

	return (u8 *)v;

err_out:
	return 0;
}

u8 mv64x60_is_coherent(void)
{
	u32 v;
	void *devp;

	devp = finddevice("/");
	if (devp == NULL)
		return 1; /* Assume coherency on */

	if (getprop(devp, "coherency-off", &v, sizeof(v)) < 0)
		return 1; /* Coherency on */
	else
		return 0;
}
