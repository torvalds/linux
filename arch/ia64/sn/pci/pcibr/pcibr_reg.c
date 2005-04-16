/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/interrupt.h>
#include "pci/pcibus_provider_defs.h"
#include "pci/pcidev.h"
#include "pci/tiocp.h"
#include "pci/pic.h"
#include "pci/pcibr_provider.h"

union br_ptr {
	struct tiocp tio;
	struct pic pic;
};

/*
 * Control Register Access -- Read/Write                            0000_0020
 */
void pcireg_control_bit_clr(struct pcibus_info *pcibus_info, uint64_t bits)
{
	union br_ptr *ptr = (union br_ptr *)pcibus_info->pbi_buscommon.bs_base;

	if (pcibus_info) {
		switch (pcibus_info->pbi_bridge_type) {
		case PCIBR_BRIDGETYPE_TIOCP:
			ptr->tio.cp_control &= ~bits;
			break;
		case PCIBR_BRIDGETYPE_PIC:
			ptr->pic.p_wid_control &= ~bits;
			break;
		default:
			panic
			    ("pcireg_control_bit_clr: unknown bridgetype bridge 0x%p",
			     (void *)ptr);
		}
	}
}

void pcireg_control_bit_set(struct pcibus_info *pcibus_info, uint64_t bits)
{
	union br_ptr *ptr = (union br_ptr *)pcibus_info->pbi_buscommon.bs_base;

	if (pcibus_info) {
		switch (pcibus_info->pbi_bridge_type) {
		case PCIBR_BRIDGETYPE_TIOCP:
			ptr->tio.cp_control |= bits;
			break;
		case PCIBR_BRIDGETYPE_PIC:
			ptr->pic.p_wid_control |= bits;
			break;
		default:
			panic
			    ("pcireg_control_bit_set: unknown bridgetype bridge 0x%p",
			     (void *)ptr);
		}
	}
}

/*
 * PCI/PCIX Target Flush Register Access -- Read Only		    0000_0050
 */
uint64_t pcireg_tflush_get(struct pcibus_info *pcibus_info)
{
	union br_ptr *ptr = (union br_ptr *)pcibus_info->pbi_buscommon.bs_base;
	uint64_t ret = 0;

	if (pcibus_info) {
		switch (pcibus_info->pbi_bridge_type) {
		case PCIBR_BRIDGETYPE_TIOCP:
			ret = ptr->tio.cp_tflush;
			break;
		case PCIBR_BRIDGETYPE_PIC:
			ret = ptr->pic.p_wid_tflush;
			break;
		default:
			panic
			    ("pcireg_tflush_get: unknown bridgetype bridge 0x%p",
			     (void *)ptr);
		}
	}

	/* Read of the Target Flush should always return zero */
	if (ret != 0)
		panic("pcireg_tflush_get:Target Flush failed\n");

	return ret;
}

/*
 * Interrupt Status Register Access -- Read Only		    0000_0100
 */
uint64_t pcireg_intr_status_get(struct pcibus_info * pcibus_info)
{
	union br_ptr *ptr = (union br_ptr *)pcibus_info->pbi_buscommon.bs_base;
	uint64_t ret = 0;

	if (pcibus_info) {
		switch (pcibus_info->pbi_bridge_type) {
		case PCIBR_BRIDGETYPE_TIOCP:
			ret = ptr->tio.cp_int_status;
			break;
		case PCIBR_BRIDGETYPE_PIC:
			ret = ptr->pic.p_int_status;
			break;
		default:
			panic
			    ("pcireg_intr_status_get: unknown bridgetype bridge 0x%p",
			     (void *)ptr);
		}
	}
	return ret;
}

/*
 * Interrupt Enable Register Access -- Read/Write                   0000_0108
 */
void pcireg_intr_enable_bit_clr(struct pcibus_info *pcibus_info, uint64_t bits)
{
	union br_ptr *ptr = (union br_ptr *)pcibus_info->pbi_buscommon.bs_base;

	if (pcibus_info) {
		switch (pcibus_info->pbi_bridge_type) {
		case PCIBR_BRIDGETYPE_TIOCP:
			ptr->tio.cp_int_enable &= ~bits;
			break;
		case PCIBR_BRIDGETYPE_PIC:
			ptr->pic.p_int_enable &= ~bits;
			break;
		default:
			panic
			    ("pcireg_intr_enable_bit_clr: unknown bridgetype bridge 0x%p",
			     (void *)ptr);
		}
	}
}

void pcireg_intr_enable_bit_set(struct pcibus_info *pcibus_info, uint64_t bits)
{
	union br_ptr *ptr = (union br_ptr *)pcibus_info->pbi_buscommon.bs_base;

	if (pcibus_info) {
		switch (pcibus_info->pbi_bridge_type) {
		case PCIBR_BRIDGETYPE_TIOCP:
			ptr->tio.cp_int_enable |= bits;
			break;
		case PCIBR_BRIDGETYPE_PIC:
			ptr->pic.p_int_enable |= bits;
			break;
		default:
			panic
			    ("pcireg_intr_enable_bit_set: unknown bridgetype bridge 0x%p",
			     (void *)ptr);
		}
	}
}

/*
 * Intr Host Address Register (int_addr) -- Read/Write  0000_0130 - 0000_0168
 */
void pcireg_intr_addr_addr_set(struct pcibus_info *pcibus_info, int int_n,
			       uint64_t addr)
{
	union br_ptr *ptr = (union br_ptr *)pcibus_info->pbi_buscommon.bs_base;

	if (pcibus_info) {
		switch (pcibus_info->pbi_bridge_type) {
		case PCIBR_BRIDGETYPE_TIOCP:
			ptr->tio.cp_int_addr[int_n] &= ~TIOCP_HOST_INTR_ADDR;
			ptr->tio.cp_int_addr[int_n] |=
			    (addr & TIOCP_HOST_INTR_ADDR);
			break;
		case PCIBR_BRIDGETYPE_PIC:
			ptr->pic.p_int_addr[int_n] &= ~PIC_HOST_INTR_ADDR;
			ptr->pic.p_int_addr[int_n] |=
			    (addr & PIC_HOST_INTR_ADDR);
			break;
		default:
			panic
			    ("pcireg_intr_addr_addr_get: unknown bridgetype bridge 0x%p",
			     (void *)ptr);
		}
	}
}

/*
 * Force Interrupt Register Access -- Write Only	0000_01C0 - 0000_01F8
 */
void pcireg_force_intr_set(struct pcibus_info *pcibus_info, int int_n)
{
	union br_ptr *ptr = (union br_ptr *)pcibus_info->pbi_buscommon.bs_base;

	if (pcibus_info) {
		switch (pcibus_info->pbi_bridge_type) {
		case PCIBR_BRIDGETYPE_TIOCP:
			ptr->tio.cp_force_pin[int_n] = 1;
			break;
		case PCIBR_BRIDGETYPE_PIC:
			ptr->pic.p_force_pin[int_n] = 1;
			break;
		default:
			panic
			    ("pcireg_force_intr_set: unknown bridgetype bridge 0x%p",
			     (void *)ptr);
		}
	}
}

/*
 * Device(x) Write Buffer Flush Reg Access -- Read Only 0000_0240 - 0000_0258
 */
uint64_t pcireg_wrb_flush_get(struct pcibus_info *pcibus_info, int device)
{
	union br_ptr *ptr = (union br_ptr *)pcibus_info->pbi_buscommon.bs_base;
	uint64_t ret = 0;

	if (pcibus_info) {
		switch (pcibus_info->pbi_bridge_type) {
		case PCIBR_BRIDGETYPE_TIOCP:
			ret = ptr->tio.cp_wr_req_buf[device];
			break;
		case PCIBR_BRIDGETYPE_PIC:
			ret = ptr->pic.p_wr_req_buf[device];
			break;
		default:
		      panic("pcireg_wrb_flush_get: unknown bridgetype bridge 0x%p", (void *)ptr);
		}

	}
	/* Read of the Write Buffer Flush should always return zero */
	return ret;
}

void pcireg_int_ate_set(struct pcibus_info *pcibus_info, int ate_index,
			uint64_t val)
{
	union br_ptr *ptr = (union br_ptr *)pcibus_info->pbi_buscommon.bs_base;

	if (pcibus_info) {
		switch (pcibus_info->pbi_bridge_type) {
		case PCIBR_BRIDGETYPE_TIOCP:
			ptr->tio.cp_int_ate_ram[ate_index] = (uint64_t) val;
			break;
		case PCIBR_BRIDGETYPE_PIC:
			ptr->pic.p_int_ate_ram[ate_index] = (uint64_t) val;
			break;
		default:
			panic
			    ("pcireg_int_ate_set: unknown bridgetype bridge 0x%p",
			     (void *)ptr);
		}
	}
}

uint64_t *pcireg_int_ate_addr(struct pcibus_info *pcibus_info, int ate_index)
{
	union br_ptr *ptr = (union br_ptr *)pcibus_info->pbi_buscommon.bs_base;
	uint64_t *ret = (uint64_t *) 0;

	if (pcibus_info) {
		switch (pcibus_info->pbi_bridge_type) {
		case PCIBR_BRIDGETYPE_TIOCP:
			ret =
			    (uint64_t *) & (ptr->tio.cp_int_ate_ram[ate_index]);
			break;
		case PCIBR_BRIDGETYPE_PIC:
			ret =
			    (uint64_t *) & (ptr->pic.p_int_ate_ram[ate_index]);
			break;
		default:
			panic
			    ("pcireg_int_ate_addr: unknown bridgetype bridge 0x%p",
			     (void *)ptr);
		}
	}
	return ret;
}
