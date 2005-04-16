/*
 * arch/ppc/boot/common/mpc10x_common.c
 *
 * A routine to find out how much memory the machine has.
 *
 * Based on:
 * arch/ppc/kernel/mpc10x_common.c
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * 2001-2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/pci.h>
#include <asm/types.h>
#include <asm/io.h>
#include "mpc10x.h"

/*
 * *** WARNING - A BAT MUST be set to access the PCI config addr/data regs ***
 */

/*
 * PCI config space macros, similar to indirect_xxx and early_xxx macros.
 * We assume bus 0.
 */
#define MPC10X_CFG_read(val, addr, type, op)	*val = op((type)(addr))
#define MPC10X_CFG_write(val, addr, type, op)	op((type *)(addr), (val))

#define MPC10X_PCI_OP(rw, size, type, op, mask)			 	\
static void								\
mpc10x_##rw##_config_##size(unsigned int *cfg_addr, 			\
		unsigned int *cfg_data, int devfn, int offset,		\
		type val)						\
{									\
	out_be32(cfg_addr, 						\
		 ((offset & 0xfc) << 24) | (devfn << 16)		\
		 | (0 << 8) | 0x80);					\
	MPC10X_CFG_##rw(val, cfg_data + (offset & mask), type, op);	\
	return;    					 		\
}

MPC10X_PCI_OP(read, byte,  u8 *, in_8, 3)
MPC10X_PCI_OP(read, dword, u32 *, in_le32, 0)

/*
 * Read the memory controller registers to determine the amount of memory in
 * the system.  This assumes that the firmware has correctly set up the memory
 * controller registers.  On CONFIG_PPC_PREP, we know we are being called
 * under a PReP memory map. On all other machines, we assume we are under
 * a CHRP memory map.  Further, on CONFIG_PPC_MULTIPLATFORM we must rename
 * this function.
 */
#ifdef CONFIG_PPC_MULTIPLATFORM
#define get_mem_size mpc10x_get_mem_size
#endif
unsigned long
get_mem_size(void)
{
	unsigned int *config_addr, *config_data, val;
	unsigned long start, end, total, offset;
	int i;
	unsigned char bank_enables;

#ifdef CONFIG_PPC_PREP
	config_addr = (unsigned int *)MPC10X_MAPA_CNFG_ADDR;
	config_data = (unsigned int *)MPC10X_MAPA_CNFG_DATA;
#else
	config_addr = (unsigned int *)MPC10X_MAPB_CNFG_ADDR;
	config_data = (unsigned int *)MPC10X_MAPB_CNFG_DATA;
#endif

	mpc10x_read_config_byte(config_addr, config_data, PCI_DEVFN(0,0),
			MPC10X_MCTLR_MEM_BANK_ENABLES, &bank_enables);

	total = 0;

	for (i = 0; i < 8; i++) {
		if (bank_enables & (1 << i)) {
			offset = MPC10X_MCTLR_MEM_START_1 + ((i > 3) ? 4 : 0);
			mpc10x_read_config_dword(config_addr, config_data,
					PCI_DEVFN(0,0), offset, &val);
			start = (val >> ((i & 3) << 3)) & 0xff;

			offset = MPC10X_MCTLR_EXT_MEM_START_1 + ((i>3) ? 4 : 0);
			mpc10x_read_config_dword(config_addr, config_data,
					PCI_DEVFN(0,0), offset, &val);
			val = (val >> ((i & 3) << 3)) & 0x03;
			start = (val << 28) | (start << 20);

			offset = MPC10X_MCTLR_MEM_END_1 + ((i > 3) ? 4 : 0);
			mpc10x_read_config_dword(config_addr, config_data,
					PCI_DEVFN(0,0), offset, &val);
			end = (val >> ((i & 3) << 3)) & 0xff;

			offset = MPC10X_MCTLR_EXT_MEM_END_1 + ((i > 3) ? 4 : 0);
			mpc10x_read_config_dword(config_addr, config_data,
					PCI_DEVFN(0,0), offset, &val);
			val = (val >> ((i & 3) << 3)) & 0x03;
			end = (val << 28) | (end << 20) | 0xfffff;

			total += (end - start + 1);
		}
	}

	return total;
}
