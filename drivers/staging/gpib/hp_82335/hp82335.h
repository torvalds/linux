/* SPDX-License-Identifier: GPL-2.0 */

/***************************************************************************
 *    copyright            : (C) 2002 by Frank Mori Hess                   *
 ***************************************************************************/

#ifndef _HP82335_H
#define _HP82335_H

#include "tms9914.h"
#include "gpibP.h"

// struct which defines private_data for board
struct hp82335_priv  {
	struct tms9914_priv tms9914_priv;
	unsigned int irq;
	unsigned long raw_iobase;
};

// size of io memory region used
static const int hp82335_rom_size = 0x2000;
static const int hp82335_upper_iomem_size = 0x2000;

// hp82335 register offsets
enum hp_read_regs {
	HPREG_CSR = 0x17f8,
	HPREG_STATUS = 0x1ffc,
};

enum hp_write_regs {
	HPREG_INTR_CLEAR = 0x17f7,
	HPREG_CCR = HPREG_CSR,
};

enum ccr_bits {
	DMA_ENABLE = (1 << 0),   /* DMA enable                  */
	DMA_CHAN_SELECT = (1 << 1),   /* DMA channel select  O=3,1=2 */
	INTR_ENABLE = (1 << 2),   /* interrupt enable            */
	SYS_DISABLE = (1 << 3),   /* system controller disable   */
};

enum csr_bits {
	SWITCH6 = (1 << 0),   /* switch 6 position           */
	SWITCH5 = (1 << 1),   /* switch 5 position           */
	SYS_CONTROLLER = (1 << 2),   /* system controller bit       */
	DMA_ENABLE_STATUS = (1 << 4),   /* DMA enabled                 */
	DMA_CHAN_STATUS = (1 << 5),   /* DMA channel   0=3,1=2       */
	INTR_ENABLE_STATUS = (1 << 6),   /* Interrupt enable            */
	INTR_PENDING = (1 << 7),   /* Interrupt Pending           */
};

#endif	// _HP82335_H
