/*
 * Author: Mark A. Greer <source@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef _PPC_BOOT_MV64x60_H_
#define _PPC_BOOT_MV64x60_H_

#define MV64x60_CPU_BAR_ENABLE			0x0278

#define MV64x60_PCI_ACC_CNTL_ENABLE		(1<<0)
#define MV64x60_PCI_ACC_CNTL_REQ64		(1<<1)
#define MV64x60_PCI_ACC_CNTL_SNOOP_NONE		0x00000000
#define MV64x60_PCI_ACC_CNTL_SNOOP_WT		0x00000004
#define MV64x60_PCI_ACC_CNTL_SNOOP_WB		0x00000008
#define MV64x60_PCI_ACC_CNTL_SNOOP_MASK		0x0000000c
#define MV64x60_PCI_ACC_CNTL_ACCPROT		(1<<4)
#define MV64x60_PCI_ACC_CNTL_WRPROT		(1<<5)
#define MV64x60_PCI_ACC_CNTL_SWAP_BYTE		0x00000000
#define MV64x60_PCI_ACC_CNTL_SWAP_NONE		0x00000040
#define MV64x60_PCI_ACC_CNTL_SWAP_BYTE_WORD	0x00000080
#define MV64x60_PCI_ACC_CNTL_SWAP_WORD		0x000000c0
#define MV64x60_PCI_ACC_CNTL_SWAP_MASK		0x000000c0
#define MV64x60_PCI_ACC_CNTL_MBURST_32_BYTES	0x00000000
#define MV64x60_PCI_ACC_CNTL_MBURST_64_BYTES	0x00000100
#define MV64x60_PCI_ACC_CNTL_MBURST_128_BYTES	0x00000200
#define MV64x60_PCI_ACC_CNTL_MBURST_MASK	0x00000300
#define MV64x60_PCI_ACC_CNTL_RDSIZE_32_BYTES	0x00000000
#define MV64x60_PCI_ACC_CNTL_RDSIZE_64_BYTES	0x00000400
#define MV64x60_PCI_ACC_CNTL_RDSIZE_128_BYTES	0x00000800
#define MV64x60_PCI_ACC_CNTL_RDSIZE_256_BYTES	0x00000c00
#define MV64x60_PCI_ACC_CNTL_RDSIZE_MASK	0x00000c00

struct mv64x60_cpu2pci_win {
	u32 lo;
	u32 size;
	u32 remap_hi;
	u32 remap_lo;
};

extern struct mv64x60_cpu2pci_win mv64x60_cpu2pci_io[2];
extern struct mv64x60_cpu2pci_win mv64x60_cpu2pci_mem[2];

u32 mv64x60_cfg_read(u8 *bridge_base, u8 hose, u8 bus, u8 devfn,
		u8 offset);
void mv64x60_cfg_write(u8 *bridge_base, u8 hose, u8 bus, u8 devfn,
		u8 offset, u32 val);

void mv64x60_config_ctlr_windows(u8 *bridge_base, u8 *bridge_pbase,
		u8 is_coherent);
void mv64x60_config_pci_windows(u8 *bridge_base, u8 *bridge_pbase, u8 hose,
		u8 bus, u32 mem_size, u32 acc_bits);
void mv64x60_config_cpu2pci_window(u8 *bridge_base, u8 hose, u32 pci_base_hi,
		u32 pci_base_lo, u32 cpu_base, u32 size,
		struct mv64x60_cpu2pci_win *offset_tbl);
u32 mv64x60_get_mem_size(u8 *bridge_base);
u8 *mv64x60_get_bridge_pbase(void);
u8 *mv64x60_get_bridge_base(void);
u8 mv64x60_is_coherent(void);

int mv64x60_i2c_open(void);
int mv64x60_i2c_read(u32 devaddr, u8 *buf, u32 offset, u32 offset_size,
		u32 count);
void mv64x60_i2c_close(void);

#endif /* _PPC_BOOT_MV64x60_H_ */
