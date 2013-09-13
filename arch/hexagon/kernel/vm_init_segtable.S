/*
 * Initial page table for Linux kernel under Hexagon VM,
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/*
 * These tables are pre-computed and linked into kernel.
 */

#include <asm/vm_mmu.h>
/*  #include <asm/iomap.h>  */

/*
 * Start with mapping PA=0 to both VA=0x0 and VA=0xc000000 as 16MB large pages.
 * No user mode access, RWX, write-back cache.  The entry needs
 * to be replicated for all 4 virtual segments mapping to the page.
 */

/* "Big Kernel Page"  */
#define BKP(pa) (((pa) & __HVM_PTE_PGMASK_4MB)		\
		| __HVM_PTE_R | __HVM_PTE_W | __HVM_PTE_X	\
		| __HEXAGON_C_WB_L2 << 6			\
		| __HVM_PDE_S_16MB)

/*  No cache version  */

#define BKPG_IO(pa) (((pa) & __HVM_PTE_PGMASK_16MB) \
			| __HVM_PTE_R | __HVM_PTE_W | __HVM_PTE_X \
			| __HVM_PDE_S_16MB | __HEXAGON_C_DEV << 6 )

#define FOURK_IO(pa) (((pa) & __HVM_PTE_PGMASK_4KB) \
			| __HVM_PTE_R | __HVM_PTE_W | __HVM_PTE_X \
			| __HEXAGON_C_DEV << 6 )

#define L2_PTR(pa) (((pa) & __HVM_PTE_PGMASK_4KB) \
			| __HVM_PDE_S_4KB  )

#define X __HVM_PDE_S_INVALID

	.p2align 12
	.globl swapper_pg_dir
	.globl _K_init_segtable
swapper_pg_dir:
/* VA 0x00000000 */
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
/* VA 0x40000000 */
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
/* VA 0x80000000 */
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
/*0xa8*/.word X,X,X,X
#ifdef CONFIG_COMET_EARLY_UART_DEBUG
UART_PTE_ENTRY:
/*0xa9*/.word BKPG_IO(0xa9000000),BKPG_IO(0xa9000000),BKPG_IO(0xa9000000),BKPG_IO(0xa9000000)
#else
/*0xa9*/.word X,X,X,X
#endif
/*0xaa*/.word X,X,X,X
/*0xab*/.word X,X,X,X
/*0xac*/.word X,X,X,X
/*0xad*/.word X,X,X,X
/*0xae*/.word X,X,X,X
/*0xaf*/.word X,X,X,X
/*0xb0*/.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
	.word X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
_K_init_segtable:
/* VA 0xC0000000 */
	.word BKP(0x00000000), BKP(0x00400000), BKP(0x00800000), BKP(0x00c00000)
	.word BKP(0x01000000), BKP(0x01400000), BKP(0x01800000), BKP(0x01c00000)
	.word BKP(0x02000000), BKP(0x02400000), BKP(0x02800000), BKP(0x02c00000)
	.word BKP(0x03000000), BKP(0x03400000), BKP(0x03800000), BKP(0x03c00000)
	.word BKP(0x04000000), BKP(0x04400000), BKP(0x04800000), BKP(0x04c00000)
	.word BKP(0x05000000), BKP(0x05400000), BKP(0x05800000), BKP(0x05c00000)
	.word BKP(0x06000000), BKP(0x06400000), BKP(0x06800000), BKP(0x06c00000)
	.word BKP(0x07000000), BKP(0x07400000), BKP(0x07800000), BKP(0x07c00000)

	.word BKP(0x08000000), BKP(0x08400000), BKP(0x08800000), BKP(0x08c00000)
	.word BKP(0x09000000), BKP(0x09400000), BKP(0x09800000), BKP(0x09c00000)
	.word BKP(0x0a000000), BKP(0x0a400000), BKP(0x0a800000), BKP(0x0ac00000)
	.word BKP(0x0b000000), BKP(0x0b400000), BKP(0x0b800000), BKP(0x0bc00000)
	.word BKP(0x0c000000), BKP(0x0c400000), BKP(0x0c800000), BKP(0x0cc00000)
	.word BKP(0x0d000000), BKP(0x0d400000), BKP(0x0d800000), BKP(0x0dc00000)
	.word BKP(0x0e000000), BKP(0x0e400000), BKP(0x0e800000), BKP(0x0ec00000)
	.word BKP(0x0f000000), BKP(0x0f400000), BKP(0x0f800000), BKP(0x0fc00000)

	.word BKP(0x10000000), BKP(0x10400000), BKP(0x10800000), BKP(0x10c00000)
	.word BKP(0x11000000), BKP(0x11400000), BKP(0x11800000), BKP(0x11c00000)
	.word BKP(0x12000000), BKP(0x12400000), BKP(0x12800000), BKP(0x12c00000)
	.word BKP(0x13000000), BKP(0x13400000), BKP(0x13800000), BKP(0x13c00000)
	.word BKP(0x14000000), BKP(0x14400000), BKP(0x14800000), BKP(0x14c00000)
	.word BKP(0x15000000), BKP(0x15400000), BKP(0x15800000), BKP(0x15c00000)
	.word BKP(0x16000000), BKP(0x16400000), BKP(0x16800000), BKP(0x16c00000)
	.word BKP(0x17000000), BKP(0x17400000), BKP(0x17800000), BKP(0x17c00000)

	.word BKP(0x18000000), BKP(0x18400000), BKP(0x18800000), BKP(0x18c00000)
	.word BKP(0x19000000), BKP(0x19400000), BKP(0x19800000), BKP(0x19c00000)
	.word BKP(0x1a000000), BKP(0x1a400000), BKP(0x1a800000), BKP(0x1ac00000)
	.word BKP(0x1b000000), BKP(0x1b400000), BKP(0x1b800000), BKP(0x1bc00000)
	.word BKP(0x1c000000), BKP(0x1c400000), BKP(0x1c800000), BKP(0x1cc00000)
	.word BKP(0x1d000000), BKP(0x1d400000), BKP(0x1d800000), BKP(0x1dc00000)
	.word BKP(0x1e000000), BKP(0x1e400000), BKP(0x1e800000), BKP(0x1ec00000)
	.word BKP(0x1f000000), BKP(0x1f400000), BKP(0x1f800000), BKP(0x1fc00000)

	.word BKP(0x20000000), BKP(0x20400000), BKP(0x20800000), BKP(0x20c00000)
	.word BKP(0x21000000), BKP(0x21400000), BKP(0x21800000), BKP(0x21c00000)
	.word BKP(0x22000000), BKP(0x22400000), BKP(0x22800000), BKP(0x22c00000)
	.word BKP(0x23000000), BKP(0x23400000), BKP(0x23800000), BKP(0x23c00000)
	.word BKP(0x24000000), BKP(0x24400000), BKP(0x24800000), BKP(0x24c00000)
	.word BKP(0x25000000), BKP(0x25400000), BKP(0x25800000), BKP(0x25c00000)
	.word BKP(0x26000000), BKP(0x26400000), BKP(0x26800000), BKP(0x26c00000)
	.word BKP(0x27000000), BKP(0x27400000), BKP(0x27800000), BKP(0x27c00000)

	.word BKP(0x28000000), BKP(0x28400000), BKP(0x28800000), BKP(0x28c00000)
	.word BKP(0x29000000), BKP(0x29400000), BKP(0x29800000), BKP(0x29c00000)
	.word BKP(0x2a000000), BKP(0x2a400000), BKP(0x2a800000), BKP(0x2ac00000)
	.word BKP(0x2b000000), BKP(0x2b400000), BKP(0x2b800000), BKP(0x2bc00000)
	.word BKP(0x2c000000), BKP(0x2c400000), BKP(0x2c800000), BKP(0x2cc00000)
	.word BKP(0x2d000000), BKP(0x2d400000), BKP(0x2d800000), BKP(0x2dc00000)
	.word BKP(0x2e000000), BKP(0x2e400000), BKP(0x2e800000), BKP(0x2ec00000)
	.word BKP(0x2f000000), BKP(0x2f400000), BKP(0x2f800000), BKP(0x2fc00000)

	.word BKP(0x30000000), BKP(0x30400000), BKP(0x30800000), BKP(0x30c00000)
	.word BKP(0x31000000), BKP(0x31400000), BKP(0x31800000), BKP(0x31c00000)
	.word BKP(0x32000000), BKP(0x32400000), BKP(0x32800000), BKP(0x32c00000)
	.word BKP(0x33000000), BKP(0x33400000), BKP(0x33800000), BKP(0x33c00000)
	.word BKP(0x34000000), BKP(0x34400000), BKP(0x34800000), BKP(0x34c00000)
	.word BKP(0x35000000), BKP(0x35400000), BKP(0x35800000), BKP(0x35c00000)
	.word BKP(0x36000000), BKP(0x36400000), BKP(0x36800000), BKP(0x36c00000)
	.word BKP(0x37000000), BKP(0x37400000), BKP(0x37800000), BKP(0x37c00000)

	.word BKP(0x38000000), BKP(0x38400000), BKP(0x38800000), BKP(0x38c00000)
	.word BKP(0x39000000), BKP(0x39400000), BKP(0x39800000), BKP(0x39c00000)
	.word BKP(0x3a000000), BKP(0x3a400000), BKP(0x3a800000), BKP(0x3ac00000)
	.word BKP(0x3b000000), BKP(0x3b400000), BKP(0x3b800000), BKP(0x3bc00000)
	.word BKP(0x3c000000), BKP(0x3c400000), BKP(0x3c800000), BKP(0x3cc00000)
	.word BKP(0x3d000000), BKP(0x3d400000), BKP(0x3d800000), BKP(0x3dc00000)
_K_io_map:
	.word X,X,X,X /* 0x3e000000 - device IO early remap */
	.word X,X,X,X /* 0x3f000000 - hypervisor space*/

#if 0
/*
 * This is in here as an example for devices which need to be mapped really
 * early.
 */
	.p2align 12
	.globl _K_io_kmap
	.globl _K_init_devicetable
_K_init_devicetable:  /*  Should be 4MB worth of entries  */
	.word FOURK_IO(MSM_GPIO1_PHYS),FOURK_IO(MSM_GPIO2_PHYS),FOURK_IO(MSM_SIRC_PHYS),X
	.word FOURK_IO(TLMM_GPIO1_PHYS),X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
	.word X,X,X,X
#endif
