/* Copyright (c) 2014 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/bcma/bcma.h>
#include <linux/sched.h>
#include <asm/unaligned.h>

#include <soc.h>
#include <chipcommon.h>
#include <brcmu_utils.h>
#include <brcmu_wifi.h>
#include <brcm_hw_ids.h>

#include "debug.h"
#include "bus.h"
#include "commonring.h"
#include "msgbuf.h"
#include "pcie.h"
#include "firmware.h"
#include "chip.h"
#include "core.h"
#include "common.h"


enum brcmf_pcie_state {
	BRCMFMAC_PCIE_STATE_DOWN,
	BRCMFMAC_PCIE_STATE_UP
};

BRCMF_FW_NVRAM_DEF(43602, "brcmfmac43602-pcie.bin", "brcmfmac43602-pcie.txt");
BRCMF_FW_NVRAM_DEF(4350, "brcmfmac4350-pcie.bin", "brcmfmac4350-pcie.txt");
BRCMF_FW_NVRAM_DEF(4350C, "brcmfmac4350c2-pcie.bin", "brcmfmac4350c2-pcie.txt");
BRCMF_FW_NVRAM_DEF(4356, "brcmfmac4356-pcie.bin", "brcmfmac4356-pcie.txt");
BRCMF_FW_NVRAM_DEF(43570, "brcmfmac43570-pcie.bin", "brcmfmac43570-pcie.txt");
BRCMF_FW_NVRAM_DEF(4358, "brcmfmac4358-pcie.bin", "brcmfmac4358-pcie.txt");
BRCMF_FW_NVRAM_DEF(4359, "brcmfmac4359-pcie.bin", "brcmfmac4359-pcie.txt");
BRCMF_FW_NVRAM_DEF(4365B, "brcmfmac4365b-pcie.bin", "brcmfmac4365b-pcie.txt");
BRCMF_FW_NVRAM_DEF(4365C, "brcmfmac4365c-pcie.bin", "brcmfmac4365c-pcie.txt");
BRCMF_FW_NVRAM_DEF(4366B, "brcmfmac4366b-pcie.bin", "brcmfmac4366b-pcie.txt");
BRCMF_FW_NVRAM_DEF(4366C, "brcmfmac4366c-pcie.bin", "brcmfmac4366c-pcie.txt");
BRCMF_FW_NVRAM_DEF(4371, "brcmfmac4371-pcie.bin", "brcmfmac4371-pcie.txt");

static struct brcmf_firmware_mapping brcmf_pcie_fwnames[] = {
	BRCMF_FW_NVRAM_ENTRY(BRCM_CC_43602_CHIP_ID, 0xFFFFFFFF, 43602),
	BRCMF_FW_NVRAM_ENTRY(BRCM_CC_43465_CHIP_ID, 0xFFFFFFF0, 4366C),
	BRCMF_FW_NVRAM_ENTRY(BRCM_CC_4350_CHIP_ID, 0x000000FF, 4350C),
	BRCMF_FW_NVRAM_ENTRY(BRCM_CC_4350_CHIP_ID, 0xFFFFFF00, 4350),
	BRCMF_FW_NVRAM_ENTRY(BRCM_CC_43525_CHIP_ID, 0xFFFFFFF0, 4365C),
	BRCMF_FW_NVRAM_ENTRY(BRCM_CC_4356_CHIP_ID, 0xFFFFFFFF, 4356),
	BRCMF_FW_NVRAM_ENTRY(BRCM_CC_43567_CHIP_ID, 0xFFFFFFFF, 43570),
	BRCMF_FW_NVRAM_ENTRY(BRCM_CC_43569_CHIP_ID, 0xFFFFFFFF, 43570),
	BRCMF_FW_NVRAM_ENTRY(BRCM_CC_43570_CHIP_ID, 0xFFFFFFFF, 43570),
	BRCMF_FW_NVRAM_ENTRY(BRCM_CC_4358_CHIP_ID, 0xFFFFFFFF, 4358),
	BRCMF_FW_NVRAM_ENTRY(BRCM_CC_4359_CHIP_ID, 0xFFFFFFFF, 4359),
	BRCMF_FW_NVRAM_ENTRY(BRCM_CC_4365_CHIP_ID, 0x0000000F, 4365B),
	BRCMF_FW_NVRAM_ENTRY(BRCM_CC_4365_CHIP_ID, 0xFFFFFFF0, 4365C),
	BRCMF_FW_NVRAM_ENTRY(BRCM_CC_4366_CHIP_ID, 0x0000000F, 4366B),
	BRCMF_FW_NVRAM_ENTRY(BRCM_CC_4366_CHIP_ID, 0xFFFFFFF0, 4366C),
	BRCMF_FW_NVRAM_ENTRY(BRCM_CC_4371_CHIP_ID, 0xFFFFFFFF, 4371),
};

#define BRCMF_PCIE_FW_UP_TIMEOUT		2000 /* msec */

#define BRCMF_PCIE_REG_MAP_SIZE			(32 * 1024)

/* backplane addres space accessed by BAR0 */
#define	BRCMF_PCIE_BAR0_WINDOW			0x80
#define BRCMF_PCIE_BAR0_REG_SIZE		0x1000
#define	BRCMF_PCIE_BAR0_WRAPPERBASE		0x70

#define BRCMF_PCIE_BAR0_WRAPBASE_DMP_OFFSET	0x1000
#define BRCMF_PCIE_BARO_PCIE_ENUM_OFFSET	0x2000

#define BRCMF_PCIE_ARMCR4REG_BANKIDX		0x40
#define BRCMF_PCIE_ARMCR4REG_BANKPDA		0x4C

#define BRCMF_PCIE_REG_INTSTATUS		0x90
#define BRCMF_PCIE_REG_INTMASK			0x94
#define BRCMF_PCIE_REG_SBMBX			0x98

#define BRCMF_PCIE_REG_LINK_STATUS_CTRL		0xBC

#define BRCMF_PCIE_PCIE2REG_INTMASK		0x24
#define BRCMF_PCIE_PCIE2REG_MAILBOXINT		0x48
#define BRCMF_PCIE_PCIE2REG_MAILBOXMASK		0x4C
#define BRCMF_PCIE_PCIE2REG_CONFIGADDR		0x120
#define BRCMF_PCIE_PCIE2REG_CONFIGDATA		0x124
#define BRCMF_PCIE_PCIE2REG_H2D_MAILBOX		0x140

#define BRCMF_PCIE2_INTA			0x01
#define BRCMF_PCIE2_INTB			0x02

#define BRCMF_PCIE_INT_0			0x01
#define BRCMF_PCIE_INT_1			0x02
#define BRCMF_PCIE_INT_DEF			(BRCMF_PCIE_INT_0 | \
						 BRCMF_PCIE_INT_1)

#define BRCMF_PCIE_MB_INT_FN0_0			0x0100
#define BRCMF_PCIE_MB_INT_FN0_1			0x0200
#define	BRCMF_PCIE_MB_INT_D2H0_DB0		0x10000
#define	BRCMF_PCIE_MB_INT_D2H0_DB1		0x20000
#define	BRCMF_PCIE_MB_INT_D2H1_DB0		0x40000
#define	BRCMF_PCIE_MB_INT_D2H1_DB1		0x80000
#define	BRCMF_PCIE_MB_INT_D2H2_DB0		0x100000
#define	BRCMF_PCIE_MB_INT_D2H2_DB1		0x200000
#define	BRCMF_PCIE_MB_INT_D2H3_DB0		0x400000
#define	BRCMF_PCIE_MB_INT_D2H3_DB1		0x800000

#define BRCMF_PCIE_MB_INT_D2H_DB		(BRCMF_PCIE_MB_INT_D2H0_DB0 | \
						 BRCMF_PCIE_MB_INT_D2H0_DB1 | \
						 BRCMF_PCIE_MB_INT_D2H1_DB0 | \
						 BRCMF_PCIE_MB_INT_D2H1_DB1 | \
						 BRCMF_PCIE_MB_INT_D2H2_DB0 | \
						 BRCMF_PCIE_MB_INT_D2H2_DB1 | \
						 BRCMF_PCIE_MB_INT_D2H3_DB0 | \
						 BRCMF_PCIE_MB_INT_D2H3_DB1)

#define BRCMF_PCIE_MIN_SHARED_VERSION		5
#define BRCMF_PCIE_MAX_SHARED_VERSION		5
#define BRCMF_PCIE_SHARED_VERSION_MASK		0x00FF
#define BRCMF_PCIE_SHARED_DMA_INDEX		0x10000
#define BRCMF_PCIE_SHARED_DMA_2B_IDX		0x100000

#define BRCMF_PCIE_FLAGS_HTOD_SPLIT		0x4000
#define BRCMF_PCIE_FLAGS_DTOH_SPLIT		0x8000

#define BRCMF_SHARED_MAX_RXBUFPOST_OFFSET	34
#define BRCMF_SHARED_RING_BASE_OFFSET		52
#define BRCMF_SHARED_RX_DATAOFFSET_OFFSET	36
#define BRCMF_SHARED_CONSOLE_ADDR_OFFSET	20
#define BRCMF_SHARED_HTOD_MB_DATA_ADDR_OFFSET	40
#define BRCMF_SHARED_DTOH_MB_DATA_ADDR_OFFSET	44
#define BRCMF_SHARED_RING_INFO_ADDR_OFFSET	48
#define BRCMF_SHARED_DMA_SCRATCH_LEN_OFFSET	52
#define BRCMF_SHARED_DMA_SCRATCH_ADDR_OFFSET	56
#define BRCMF_SHARED_DMA_RINGUPD_LEN_OFFSET	64
#define BRCMF_SHARED_DMA_RINGUPD_ADDR_OFFSET	68

#define BRCMF_RING_H2D_RING_COUNT_OFFSET	0
#define BRCMF_RING_D2H_RING_COUNT_OFFSET	1
#define BRCMF_RING_H2D_RING_MEM_OFFSET		4
#define BRCMF_RING_H2D_RING_STATE_OFFSET	8

#define BRCMF_RING_MEM_BASE_ADDR_OFFSET		8
#define BRCMF_RING_MAX_ITEM_OFFSET		4
#define BRCMF_RING_LEN_ITEMS_OFFSET		6
#define BRCMF_RING_MEM_SZ			16
#define BRCMF_RING_STATE_SZ			8

#define BRCMF_SHARED_RING_H2D_W_IDX_PTR_OFFSET	4
#define BRCMF_SHARED_RING_H2D_R_IDX_PTR_OFFSET	8
#define BRCMF_SHARED_RING_D2H_W_IDX_PTR_OFFSET	12
#define BRCMF_SHARED_RING_D2H_R_IDX_PTR_OFFSET	16
#define BRCMF_SHARED_RING_H2D_WP_HADDR_OFFSET	20
#define BRCMF_SHARED_RING_H2D_RP_HADDR_OFFSET	28
#define BRCMF_SHARED_RING_D2H_WP_HADDR_OFFSET	36
#define BRCMF_SHARED_RING_D2H_RP_HADDR_OFFSET	44
#define BRCMF_SHARED_RING_TCM_MEMLOC_OFFSET	0
#define BRCMF_SHARED_RING_MAX_SUB_QUEUES	52

#define BRCMF_DEF_MAX_RXBUFPOST			255

#define BRCMF_CONSOLE_BUFADDR_OFFSET		8
#define BRCMF_CONSOLE_BUFSIZE_OFFSET		12
#define BRCMF_CONSOLE_WRITEIDX_OFFSET		16

#define BRCMF_DMA_D2H_SCRATCH_BUF_LEN		8
#define BRCMF_DMA_D2H_RINGUPD_BUF_LEN		1024

#define BRCMF_D2H_DEV_D3_ACK			0x00000001
#define BRCMF_D2H_DEV_DS_ENTER_REQ		0x00000002
#define BRCMF_D2H_DEV_DS_EXIT_NOTE		0x00000004

#define BRCMF_H2D_HOST_D3_INFORM		0x00000001
#define BRCMF_H2D_HOST_DS_ACK			0x00000002
#define BRCMF_H2D_HOST_D0_INFORM_IN_USE		0x00000008
#define BRCMF_H2D_HOST_D0_INFORM		0x00000010

#define BRCMF_PCIE_MBDATA_TIMEOUT		msecs_to_jiffies(2000)

#define BRCMF_PCIE_CFGREG_STATUS_CMD		0x4
#define BRCMF_PCIE_CFGREG_PM_CSR		0x4C
#define BRCMF_PCIE_CFGREG_MSI_CAP		0x58
#define BRCMF_PCIE_CFGREG_MSI_ADDR_L		0x5C
#define BRCMF_PCIE_CFGREG_MSI_ADDR_H		0x60
#define BRCMF_PCIE_CFGREG_MSI_DATA		0x64
#define BRCMF_PCIE_CFGREG_LINK_STATUS_CTRL	0xBC
#define BRCMF_PCIE_CFGREG_LINK_STATUS_CTRL2	0xDC
#define BRCMF_PCIE_CFGREG_RBAR_CTRL		0x228
#define BRCMF_PCIE_CFGREG_PML1_SUB_CTRL1	0x248
#define BRCMF_PCIE_CFGREG_REG_BAR2_CONFIG	0x4E0
#define BRCMF_PCIE_CFGREG_REG_BAR3_CONFIG	0x4F4
#define BRCMF_PCIE_LINK_STATUS_CTRL_ASPM_ENAB	3

/* Magic number at a magic location to find RAM size */
#define BRCMF_RAMSIZE_MAGIC			0x534d4152	/* SMAR */
#define BRCMF_RAMSIZE_OFFSET			0x6c


struct brcmf_pcie_console {
	u32 base_addr;
	u32 buf_addr;
	u32 bufsize;
	u32 read_idx;
	u8 log_str[256];
	u8 log_idx;
};

struct brcmf_pcie_shared_info {
	u32 tcm_base_address;
	u32 flags;
	struct brcmf_pcie_ringbuf *commonrings[BRCMF_NROF_COMMON_MSGRINGS];
	struct brcmf_pcie_ringbuf *flowrings;
	u16 max_rxbufpost;
	u32 nrof_flowrings;
	u32 rx_dataoffset;
	u32 htod_mb_data_addr;
	u32 dtoh_mb_data_addr;
	u32 ring_info_addr;
	struct brcmf_pcie_console console;
	void *scratch;
	dma_addr_t scratch_dmahandle;
	void *ringupd;
	dma_addr_t ringupd_dmahandle;
};

struct brcmf_pcie_core_info {
	u32 base;
	u32 wrapbase;
};

struct brcmf_pciedev_info {
	enum brcmf_pcie_state state;
	bool in_irq;
	struct pci_dev *pdev;
	char fw_name[BRCMF_FW_NAME_LEN];
	char nvram_name[BRCMF_FW_NAME_LEN];
	void __iomem *regs;
	void __iomem *tcm;
	u32 ram_base;
	u32 ram_size;
	struct brcmf_chip *ci;
	u32 coreid;
	struct brcmf_pcie_shared_info shared;
	wait_queue_head_t mbdata_resp_wait;
	bool mbdata_completed;
	bool irq_allocated;
	bool wowl_enabled;
	u8 dma_idx_sz;
	void *idxbuf;
	u32 idxbuf_sz;
	dma_addr_t idxbuf_dmahandle;
	u16 (*read_ptr)(struct brcmf_pciedev_info *devinfo, u32 mem_offset);
	void (*write_ptr)(struct brcmf_pciedev_info *devinfo, u32 mem_offset,
			  u16 value);
	struct brcmf_mp_device *settings;
};

struct brcmf_pcie_ringbuf {
	struct brcmf_commonring commonring;
	dma_addr_t dma_handle;
	u32 w_idx_addr;
	u32 r_idx_addr;
	struct brcmf_pciedev_info *devinfo;
	u8 id;
};


static const u32 brcmf_ring_max_item[BRCMF_NROF_COMMON_MSGRINGS] = {
	BRCMF_H2D_MSGRING_CONTROL_SUBMIT_MAX_ITEM,
	BRCMF_H2D_MSGRING_RXPOST_SUBMIT_MAX_ITEM,
	BRCMF_D2H_MSGRING_CONTROL_COMPLETE_MAX_ITEM,
	BRCMF_D2H_MSGRING_TX_COMPLETE_MAX_ITEM,
	BRCMF_D2H_MSGRING_RX_COMPLETE_MAX_ITEM
};

static const u32 brcmf_ring_itemsize[BRCMF_NROF_COMMON_MSGRINGS] = {
	BRCMF_H2D_MSGRING_CONTROL_SUBMIT_ITEMSIZE,
	BRCMF_H2D_MSGRING_RXPOST_SUBMIT_ITEMSIZE,
	BRCMF_D2H_MSGRING_CONTROL_COMPLETE_ITEMSIZE,
	BRCMF_D2H_MSGRING_TX_COMPLETE_ITEMSIZE,
	BRCMF_D2H_MSGRING_RX_COMPLETE_ITEMSIZE
};


static u32
brcmf_pcie_read_reg32(struct brcmf_pciedev_info *devinfo, u32 reg_offset)
{
	void __iomem *address = devinfo->regs + reg_offset;

	return (ioread32(address));
}


static void
brcmf_pcie_write_reg32(struct brcmf_pciedev_info *devinfo, u32 reg_offset,
		       u32 value)
{
	void __iomem *address = devinfo->regs + reg_offset;

	iowrite32(value, address);
}


static u8
brcmf_pcie_read_tcm8(struct brcmf_pciedev_info *devinfo, u32 mem_offset)
{
	void __iomem *address = devinfo->tcm + mem_offset;

	return (ioread8(address));
}


static u16
brcmf_pcie_read_tcm16(struct brcmf_pciedev_info *devinfo, u32 mem_offset)
{
	void __iomem *address = devinfo->tcm + mem_offset;

	return (ioread16(address));
}


static void
brcmf_pcie_write_tcm16(struct brcmf_pciedev_info *devinfo, u32 mem_offset,
		       u16 value)
{
	void __iomem *address = devinfo->tcm + mem_offset;

	iowrite16(value, address);
}


static u16
brcmf_pcie_read_idx(struct brcmf_pciedev_info *devinfo, u32 mem_offset)
{
	u16 *address = devinfo->idxbuf + mem_offset;

	return (*(address));
}


static void
brcmf_pcie_write_idx(struct brcmf_pciedev_info *devinfo, u32 mem_offset,
		     u16 value)
{
	u16 *address = devinfo->idxbuf + mem_offset;

	*(address) = value;
}


static u32
brcmf_pcie_read_tcm32(struct brcmf_pciedev_info *devinfo, u32 mem_offset)
{
	void __iomem *address = devinfo->tcm + mem_offset;

	return (ioread32(address));
}


static void
brcmf_pcie_write_tcm32(struct brcmf_pciedev_info *devinfo, u32 mem_offset,
		       u32 value)
{
	void __iomem *address = devinfo->tcm + mem_offset;

	iowrite32(value, address);
}


static u32
brcmf_pcie_read_ram32(struct brcmf_pciedev_info *devinfo, u32 mem_offset)
{
	void __iomem *addr = devinfo->tcm + devinfo->ci->rambase + mem_offset;

	return (ioread32(addr));
}


static void
brcmf_pcie_write_ram32(struct brcmf_pciedev_info *devinfo, u32 mem_offset,
		       u32 value)
{
	void __iomem *addr = devinfo->tcm + devinfo->ci->rambase + mem_offset;

	iowrite32(value, addr);
}


static void
brcmf_pcie_copy_mem_todev(struct brcmf_pciedev_info *devinfo, u32 mem_offset,
			  void *srcaddr, u32 len)
{
	void __iomem *address = devinfo->tcm + mem_offset;
	__le32 *src32;
	__le16 *src16;
	u8 *src8;

	if (((ulong)address & 4) || ((ulong)srcaddr & 4) || (len & 4)) {
		if (((ulong)address & 2) || ((ulong)srcaddr & 2) || (len & 2)) {
			src8 = (u8 *)srcaddr;
			while (len) {
				iowrite8(*src8, address);
				address++;
				src8++;
				len--;
			}
		} else {
			len = len / 2;
			src16 = (__le16 *)srcaddr;
			while (len) {
				iowrite16(le16_to_cpu(*src16), address);
				address += 2;
				src16++;
				len--;
			}
		}
	} else {
		len = len / 4;
		src32 = (__le32 *)srcaddr;
		while (len) {
			iowrite32(le32_to_cpu(*src32), address);
			address += 4;
			src32++;
			len--;
		}
	}
}


static void
brcmf_pcie_copy_dev_tomem(struct brcmf_pciedev_info *devinfo, u32 mem_offset,
			  void *dstaddr, u32 len)
{
	void __iomem *address = devinfo->tcm + mem_offset;
	__le32 *dst32;
	__le16 *dst16;
	u8 *dst8;

	if (((ulong)address & 4) || ((ulong)dstaddr & 4) || (len & 4)) {
		if (((ulong)address & 2) || ((ulong)dstaddr & 2) || (len & 2)) {
			dst8 = (u8 *)dstaddr;
			while (len) {
				*dst8 = ioread8(address);
				address++;
				dst8++;
				len--;
			}
		} else {
			len = len / 2;
			dst16 = (__le16 *)dstaddr;
			while (len) {
				*dst16 = cpu_to_le16(ioread16(address));
				address += 2;
				dst16++;
				len--;
			}
		}
	} else {
		len = len / 4;
		dst32 = (__le32 *)dstaddr;
		while (len) {
			*dst32 = cpu_to_le32(ioread32(address));
			address += 4;
			dst32++;
			len--;
		}
	}
}


#define WRITECC32(devinfo, reg, value) brcmf_pcie_write_reg32(devinfo, \
		CHIPCREGOFFS(reg), value)


static void
brcmf_pcie_select_core(struct brcmf_pciedev_info *devinfo, u16 coreid)
{
	const struct pci_dev *pdev = devinfo->pdev;
	struct brcmf_core *core;
	u32 bar0_win;

	core = brcmf_chip_get_core(devinfo->ci, coreid);
	if (core) {
		bar0_win = core->base;
		pci_write_config_dword(pdev, BRCMF_PCIE_BAR0_WINDOW, bar0_win);
		if (pci_read_config_dword(pdev, BRCMF_PCIE_BAR0_WINDOW,
					  &bar0_win) == 0) {
			if (bar0_win != core->base) {
				bar0_win = core->base;
				pci_write_config_dword(pdev,
						       BRCMF_PCIE_BAR0_WINDOW,
						       bar0_win);
			}
		}
	} else {
		brcmf_err("Unsupported core selected %x\n", coreid);
	}
}


static void brcmf_pcie_reset_device(struct brcmf_pciedev_info *devinfo)
{
	struct brcmf_core *core;
	u16 cfg_offset[] = { BRCMF_PCIE_CFGREG_STATUS_CMD,
			     BRCMF_PCIE_CFGREG_PM_CSR,
			     BRCMF_PCIE_CFGREG_MSI_CAP,
			     BRCMF_PCIE_CFGREG_MSI_ADDR_L,
			     BRCMF_PCIE_CFGREG_MSI_ADDR_H,
			     BRCMF_PCIE_CFGREG_MSI_DATA,
			     BRCMF_PCIE_CFGREG_LINK_STATUS_CTRL2,
			     BRCMF_PCIE_CFGREG_RBAR_CTRL,
			     BRCMF_PCIE_CFGREG_PML1_SUB_CTRL1,
			     BRCMF_PCIE_CFGREG_REG_BAR2_CONFIG,
			     BRCMF_PCIE_CFGREG_REG_BAR3_CONFIG };
	u32 i;
	u32 val;
	u32 lsc;

	if (!devinfo->ci)
		return;

	/* Disable ASPM */
	brcmf_pcie_select_core(devinfo, BCMA_CORE_PCIE2);
	pci_read_config_dword(devinfo->pdev, BRCMF_PCIE_REG_LINK_STATUS_CTRL,
			      &lsc);
	val = lsc & (~BRCMF_PCIE_LINK_STATUS_CTRL_ASPM_ENAB);
	pci_write_config_dword(devinfo->pdev, BRCMF_PCIE_REG_LINK_STATUS_CTRL,
			       val);

	/* Watchdog reset */
	brcmf_pcie_select_core(devinfo, BCMA_CORE_CHIPCOMMON);
	WRITECC32(devinfo, watchdog, 4);
	msleep(100);

	/* Restore ASPM */
	brcmf_pcie_select_core(devinfo, BCMA_CORE_PCIE2);
	pci_write_config_dword(devinfo->pdev, BRCMF_PCIE_REG_LINK_STATUS_CTRL,
			       lsc);

	core = brcmf_chip_get_core(devinfo->ci, BCMA_CORE_PCIE2);
	if (core->rev <= 13) {
		for (i = 0; i < ARRAY_SIZE(cfg_offset); i++) {
			brcmf_pcie_write_reg32(devinfo,
					       BRCMF_PCIE_PCIE2REG_CONFIGADDR,
					       cfg_offset[i]);
			val = brcmf_pcie_read_reg32(devinfo,
				BRCMF_PCIE_PCIE2REG_CONFIGDATA);
			brcmf_dbg(PCIE, "config offset 0x%04x, value 0x%04x\n",
				  cfg_offset[i], val);
			brcmf_pcie_write_reg32(devinfo,
					       BRCMF_PCIE_PCIE2REG_CONFIGDATA,
					       val);
		}
	}
}


static void brcmf_pcie_attach(struct brcmf_pciedev_info *devinfo)
{
	u32 config;

	brcmf_pcie_select_core(devinfo, BCMA_CORE_PCIE2);
	/* BAR1 window may not be sized properly */
	brcmf_pcie_select_core(devinfo, BCMA_CORE_PCIE2);
	brcmf_pcie_write_reg32(devinfo, BRCMF_PCIE_PCIE2REG_CONFIGADDR, 0x4e0);
	config = brcmf_pcie_read_reg32(devinfo, BRCMF_PCIE_PCIE2REG_CONFIGDATA);
	brcmf_pcie_write_reg32(devinfo, BRCMF_PCIE_PCIE2REG_CONFIGDATA, config);

	device_wakeup_enable(&devinfo->pdev->dev);
}


static int brcmf_pcie_enter_download_state(struct brcmf_pciedev_info *devinfo)
{
	if (devinfo->ci->chip == BRCM_CC_43602_CHIP_ID) {
		brcmf_pcie_select_core(devinfo, BCMA_CORE_ARM_CR4);
		brcmf_pcie_write_reg32(devinfo, BRCMF_PCIE_ARMCR4REG_BANKIDX,
				       5);
		brcmf_pcie_write_reg32(devinfo, BRCMF_PCIE_ARMCR4REG_BANKPDA,
				       0);
		brcmf_pcie_write_reg32(devinfo, BRCMF_PCIE_ARMCR4REG_BANKIDX,
				       7);
		brcmf_pcie_write_reg32(devinfo, BRCMF_PCIE_ARMCR4REG_BANKPDA,
				       0);
	}
	return 0;
}


static int brcmf_pcie_exit_download_state(struct brcmf_pciedev_info *devinfo,
					  u32 resetintr)
{
	struct brcmf_core *core;

	if (devinfo->ci->chip == BRCM_CC_43602_CHIP_ID) {
		core = brcmf_chip_get_core(devinfo->ci, BCMA_CORE_INTERNAL_MEM);
		brcmf_chip_resetcore(core, 0, 0, 0);
	}

	if (!brcmf_chip_set_active(devinfo->ci, resetintr))
		return -EINVAL;
	return 0;
}


static int
brcmf_pcie_send_mb_data(struct brcmf_pciedev_info *devinfo, u32 htod_mb_data)
{
	struct brcmf_pcie_shared_info *shared;
	u32 addr;
	u32 cur_htod_mb_data;
	u32 i;

	shared = &devinfo->shared;
	addr = shared->htod_mb_data_addr;
	cur_htod_mb_data = brcmf_pcie_read_tcm32(devinfo, addr);

	if (cur_htod_mb_data != 0)
		brcmf_dbg(PCIE, "MB transaction is already pending 0x%04x\n",
			  cur_htod_mb_data);

	i = 0;
	while (cur_htod_mb_data != 0) {
		msleep(10);
		i++;
		if (i > 100)
			return -EIO;
		cur_htod_mb_data = brcmf_pcie_read_tcm32(devinfo, addr);
	}

	brcmf_pcie_write_tcm32(devinfo, addr, htod_mb_data);
	pci_write_config_dword(devinfo->pdev, BRCMF_PCIE_REG_SBMBX, 1);
	pci_write_config_dword(devinfo->pdev, BRCMF_PCIE_REG_SBMBX, 1);

	return 0;
}


static void brcmf_pcie_handle_mb_data(struct brcmf_pciedev_info *devinfo)
{
	struct brcmf_pcie_shared_info *shared;
	u32 addr;
	u32 dtoh_mb_data;

	shared = &devinfo->shared;
	addr = shared->dtoh_mb_data_addr;
	dtoh_mb_data = brcmf_pcie_read_tcm32(devinfo, addr);

	if (!dtoh_mb_data)
		return;

	brcmf_pcie_write_tcm32(devinfo, addr, 0);

	brcmf_dbg(PCIE, "D2H_MB_DATA: 0x%04x\n", dtoh_mb_data);
	if (dtoh_mb_data & BRCMF_D2H_DEV_DS_ENTER_REQ)  {
		brcmf_dbg(PCIE, "D2H_MB_DATA: DEEP SLEEP REQ\n");
		brcmf_pcie_send_mb_data(devinfo, BRCMF_H2D_HOST_DS_ACK);
		brcmf_dbg(PCIE, "D2H_MB_DATA: sent DEEP SLEEP ACK\n");
	}
	if (dtoh_mb_data & BRCMF_D2H_DEV_DS_EXIT_NOTE)
		brcmf_dbg(PCIE, "D2H_MB_DATA: DEEP SLEEP EXIT\n");
	if (dtoh_mb_data & BRCMF_D2H_DEV_D3_ACK) {
		brcmf_dbg(PCIE, "D2H_MB_DATA: D3 ACK\n");
		devinfo->mbdata_completed = true;
		wake_up(&devinfo->mbdata_resp_wait);
	}
}


static void brcmf_pcie_bus_console_init(struct brcmf_pciedev_info *devinfo)
{
	struct brcmf_pcie_shared_info *shared;
	struct brcmf_pcie_console *console;
	u32 addr;

	shared = &devinfo->shared;
	console = &shared->console;
	addr = shared->tcm_base_address + BRCMF_SHARED_CONSOLE_ADDR_OFFSET;
	console->base_addr = brcmf_pcie_read_tcm32(devinfo, addr);

	addr = console->base_addr + BRCMF_CONSOLE_BUFADDR_OFFSET;
	console->buf_addr = brcmf_pcie_read_tcm32(devinfo, addr);
	addr = console->base_addr + BRCMF_CONSOLE_BUFSIZE_OFFSET;
	console->bufsize = brcmf_pcie_read_tcm32(devinfo, addr);

	brcmf_dbg(FWCON, "Console: base %x, buf %x, size %d\n",
		  console->base_addr, console->buf_addr, console->bufsize);
}


static void brcmf_pcie_bus_console_read(struct brcmf_pciedev_info *devinfo)
{
	struct brcmf_pcie_console *console;
	u32 addr;
	u8 ch;
	u32 newidx;

	if (!BRCMF_FWCON_ON())
		return;

	console = &devinfo->shared.console;
	addr = console->base_addr + BRCMF_CONSOLE_WRITEIDX_OFFSET;
	newidx = brcmf_pcie_read_tcm32(devinfo, addr);
	while (newidx != console->read_idx) {
		addr = console->buf_addr + console->read_idx;
		ch = brcmf_pcie_read_tcm8(devinfo, addr);
		console->read_idx++;
		if (console->read_idx == console->bufsize)
			console->read_idx = 0;
		if (ch == '\r')
			continue;
		console->log_str[console->log_idx] = ch;
		console->log_idx++;
		if ((ch != '\n') &&
		    (console->log_idx == (sizeof(console->log_str) - 2))) {
			ch = '\n';
			console->log_str[console->log_idx] = ch;
			console->log_idx++;
		}
		if (ch == '\n') {
			console->log_str[console->log_idx] = 0;
			pr_debug("CONSOLE: %s", console->log_str);
			console->log_idx = 0;
		}
	}
}


static void brcmf_pcie_intr_disable(struct brcmf_pciedev_info *devinfo)
{
	brcmf_pcie_write_reg32(devinfo, BRCMF_PCIE_PCIE2REG_MAILBOXMASK, 0);
}


static void brcmf_pcie_intr_enable(struct brcmf_pciedev_info *devinfo)
{
	brcmf_pcie_write_reg32(devinfo, BRCMF_PCIE_PCIE2REG_MAILBOXMASK,
			       BRCMF_PCIE_MB_INT_D2H_DB |
			       BRCMF_PCIE_MB_INT_FN0_0 |
			       BRCMF_PCIE_MB_INT_FN0_1);
}


static irqreturn_t brcmf_pcie_quick_check_isr(int irq, void *arg)
{
	struct brcmf_pciedev_info *devinfo = (struct brcmf_pciedev_info *)arg;

	if (brcmf_pcie_read_reg32(devinfo, BRCMF_PCIE_PCIE2REG_MAILBOXINT)) {
		brcmf_pcie_intr_disable(devinfo);
		brcmf_dbg(PCIE, "Enter\n");
		return IRQ_WAKE_THREAD;
	}
	return IRQ_NONE;
}


static irqreturn_t brcmf_pcie_isr_thread(int irq, void *arg)
{
	struct brcmf_pciedev_info *devinfo = (struct brcmf_pciedev_info *)arg;
	u32 status;

	devinfo->in_irq = true;
	status = brcmf_pcie_read_reg32(devinfo, BRCMF_PCIE_PCIE2REG_MAILBOXINT);
	brcmf_dbg(PCIE, "Enter %x\n", status);
	if (status) {
		brcmf_pcie_write_reg32(devinfo, BRCMF_PCIE_PCIE2REG_MAILBOXINT,
				       status);
		if (status & (BRCMF_PCIE_MB_INT_FN0_0 |
			      BRCMF_PCIE_MB_INT_FN0_1))
			brcmf_pcie_handle_mb_data(devinfo);
		if (status & BRCMF_PCIE_MB_INT_D2H_DB) {
			if (devinfo->state == BRCMFMAC_PCIE_STATE_UP)
				brcmf_proto_msgbuf_rx_trigger(
							&devinfo->pdev->dev);
		}
	}
	brcmf_pcie_bus_console_read(devinfo);
	if (devinfo->state == BRCMFMAC_PCIE_STATE_UP)
		brcmf_pcie_intr_enable(devinfo);
	devinfo->in_irq = false;
	return IRQ_HANDLED;
}


static int brcmf_pcie_request_irq(struct brcmf_pciedev_info *devinfo)
{
	struct pci_dev *pdev;

	pdev = devinfo->pdev;

	brcmf_pcie_intr_disable(devinfo);

	brcmf_dbg(PCIE, "Enter\n");

	pci_enable_msi(pdev);
	if (request_threaded_irq(pdev->irq, brcmf_pcie_quick_check_isr,
				 brcmf_pcie_isr_thread, IRQF_SHARED,
				 "brcmf_pcie_intr", devinfo)) {
		pci_disable_msi(pdev);
		brcmf_err("Failed to request IRQ %d\n", pdev->irq);
		return -EIO;
	}
	devinfo->irq_allocated = true;
	return 0;
}


static void brcmf_pcie_release_irq(struct brcmf_pciedev_info *devinfo)
{
	struct pci_dev *pdev;
	u32 status;
	u32 count;

	if (!devinfo->irq_allocated)
		return;

	pdev = devinfo->pdev;

	brcmf_pcie_intr_disable(devinfo);
	free_irq(pdev->irq, devinfo);
	pci_disable_msi(pdev);

	msleep(50);
	count = 0;
	while ((devinfo->in_irq) && (count < 20)) {
		msleep(50);
		count++;
	}
	if (devinfo->in_irq)
		brcmf_err("Still in IRQ (processing) !!!\n");

	status = brcmf_pcie_read_reg32(devinfo, BRCMF_PCIE_PCIE2REG_MAILBOXINT);
	brcmf_pcie_write_reg32(devinfo, BRCMF_PCIE_PCIE2REG_MAILBOXINT, status);

	devinfo->irq_allocated = false;
}


static int brcmf_pcie_ring_mb_write_rptr(void *ctx)
{
	struct brcmf_pcie_ringbuf *ring = (struct brcmf_pcie_ringbuf *)ctx;
	struct brcmf_pciedev_info *devinfo = ring->devinfo;
	struct brcmf_commonring *commonring = &ring->commonring;

	if (devinfo->state != BRCMFMAC_PCIE_STATE_UP)
		return -EIO;

	brcmf_dbg(PCIE, "W r_ptr %d (%d), ring %d\n", commonring->r_ptr,
		  commonring->w_ptr, ring->id);

	devinfo->write_ptr(devinfo, ring->r_idx_addr, commonring->r_ptr);

	return 0;
}


static int brcmf_pcie_ring_mb_write_wptr(void *ctx)
{
	struct brcmf_pcie_ringbuf *ring = (struct brcmf_pcie_ringbuf *)ctx;
	struct brcmf_pciedev_info *devinfo = ring->devinfo;
	struct brcmf_commonring *commonring = &ring->commonring;

	if (devinfo->state != BRCMFMAC_PCIE_STATE_UP)
		return -EIO;

	brcmf_dbg(PCIE, "W w_ptr %d (%d), ring %d\n", commonring->w_ptr,
		  commonring->r_ptr, ring->id);

	devinfo->write_ptr(devinfo, ring->w_idx_addr, commonring->w_ptr);

	return 0;
}


static int brcmf_pcie_ring_mb_ring_bell(void *ctx)
{
	struct brcmf_pcie_ringbuf *ring = (struct brcmf_pcie_ringbuf *)ctx;
	struct brcmf_pciedev_info *devinfo = ring->devinfo;

	if (devinfo->state != BRCMFMAC_PCIE_STATE_UP)
		return -EIO;

	brcmf_dbg(PCIE, "RING !\n");
	/* Any arbitrary value will do, lets use 1 */
	brcmf_pcie_write_reg32(devinfo, BRCMF_PCIE_PCIE2REG_H2D_MAILBOX, 1);

	return 0;
}


static int brcmf_pcie_ring_mb_update_rptr(void *ctx)
{
	struct brcmf_pcie_ringbuf *ring = (struct brcmf_pcie_ringbuf *)ctx;
	struct brcmf_pciedev_info *devinfo = ring->devinfo;
	struct brcmf_commonring *commonring = &ring->commonring;

	if (devinfo->state != BRCMFMAC_PCIE_STATE_UP)
		return -EIO;

	commonring->r_ptr = devinfo->read_ptr(devinfo, ring->r_idx_addr);

	brcmf_dbg(PCIE, "R r_ptr %d (%d), ring %d\n", commonring->r_ptr,
		  commonring->w_ptr, ring->id);

	return 0;
}


static int brcmf_pcie_ring_mb_update_wptr(void *ctx)
{
	struct brcmf_pcie_ringbuf *ring = (struct brcmf_pcie_ringbuf *)ctx;
	struct brcmf_pciedev_info *devinfo = ring->devinfo;
	struct brcmf_commonring *commonring = &ring->commonring;

	if (devinfo->state != BRCMFMAC_PCIE_STATE_UP)
		return -EIO;

	commonring->w_ptr = devinfo->read_ptr(devinfo, ring->w_idx_addr);

	brcmf_dbg(PCIE, "R w_ptr %d (%d), ring %d\n", commonring->w_ptr,
		  commonring->r_ptr, ring->id);

	return 0;
}


static void *
brcmf_pcie_init_dmabuffer_for_device(struct brcmf_pciedev_info *devinfo,
				     u32 size, u32 tcm_dma_phys_addr,
				     dma_addr_t *dma_handle)
{
	void *ring;
	u64 address;

	ring = dma_alloc_coherent(&devinfo->pdev->dev, size, dma_handle,
				  GFP_KERNEL);
	if (!ring)
		return NULL;

	address = (u64)*dma_handle;
	brcmf_pcie_write_tcm32(devinfo, tcm_dma_phys_addr,
			       address & 0xffffffff);
	brcmf_pcie_write_tcm32(devinfo, tcm_dma_phys_addr + 4, address >> 32);

	memset(ring, 0, size);

	return (ring);
}


static struct brcmf_pcie_ringbuf *
brcmf_pcie_alloc_dma_and_ring(struct brcmf_pciedev_info *devinfo, u32 ring_id,
			      u32 tcm_ring_phys_addr)
{
	void *dma_buf;
	dma_addr_t dma_handle;
	struct brcmf_pcie_ringbuf *ring;
	u32 size;
	u32 addr;

	size = brcmf_ring_max_item[ring_id] * brcmf_ring_itemsize[ring_id];
	dma_buf = brcmf_pcie_init_dmabuffer_for_device(devinfo, size,
			tcm_ring_phys_addr + BRCMF_RING_MEM_BASE_ADDR_OFFSET,
			&dma_handle);
	if (!dma_buf)
		return NULL;

	addr = tcm_ring_phys_addr + BRCMF_RING_MAX_ITEM_OFFSET;
	brcmf_pcie_write_tcm16(devinfo, addr, brcmf_ring_max_item[ring_id]);
	addr = tcm_ring_phys_addr + BRCMF_RING_LEN_ITEMS_OFFSET;
	brcmf_pcie_write_tcm16(devinfo, addr, brcmf_ring_itemsize[ring_id]);

	ring = kzalloc(sizeof(*ring), GFP_KERNEL);
	if (!ring) {
		dma_free_coherent(&devinfo->pdev->dev, size, dma_buf,
				  dma_handle);
		return NULL;
	}
	brcmf_commonring_config(&ring->commonring, brcmf_ring_max_item[ring_id],
				brcmf_ring_itemsize[ring_id], dma_buf);
	ring->dma_handle = dma_handle;
	ring->devinfo = devinfo;
	brcmf_commonring_register_cb(&ring->commonring,
				     brcmf_pcie_ring_mb_ring_bell,
				     brcmf_pcie_ring_mb_update_rptr,
				     brcmf_pcie_ring_mb_update_wptr,
				     brcmf_pcie_ring_mb_write_rptr,
				     brcmf_pcie_ring_mb_write_wptr, ring);

	return (ring);
}


static void brcmf_pcie_release_ringbuffer(struct device *dev,
					  struct brcmf_pcie_ringbuf *ring)
{
	void *dma_buf;
	u32 size;

	if (!ring)
		return;

	dma_buf = ring->commonring.buf_addr;
	if (dma_buf) {
		size = ring->commonring.depth * ring->commonring.item_len;
		dma_free_coherent(dev, size, dma_buf, ring->dma_handle);
	}
	kfree(ring);
}


static void brcmf_pcie_release_ringbuffers(struct brcmf_pciedev_info *devinfo)
{
	u32 i;

	for (i = 0; i < BRCMF_NROF_COMMON_MSGRINGS; i++) {
		brcmf_pcie_release_ringbuffer(&devinfo->pdev->dev,
					      devinfo->shared.commonrings[i]);
		devinfo->shared.commonrings[i] = NULL;
	}
	kfree(devinfo->shared.flowrings);
	devinfo->shared.flowrings = NULL;
	if (devinfo->idxbuf) {
		dma_free_coherent(&devinfo->pdev->dev,
				  devinfo->idxbuf_sz,
				  devinfo->idxbuf,
				  devinfo->idxbuf_dmahandle);
		devinfo->idxbuf = NULL;
	}
}


static int brcmf_pcie_init_ringbuffers(struct brcmf_pciedev_info *devinfo)
{
	struct brcmf_pcie_ringbuf *ring;
	struct brcmf_pcie_ringbuf *rings;
	u32 ring_addr;
	u32 d2h_w_idx_ptr;
	u32 d2h_r_idx_ptr;
	u32 h2d_w_idx_ptr;
	u32 h2d_r_idx_ptr;
	u32 addr;
	u32 ring_mem_ptr;
	u32 i;
	u64 address;
	u32 bufsz;
	u16 max_sub_queues;
	u8 idx_offset;

	ring_addr = devinfo->shared.ring_info_addr;
	brcmf_dbg(PCIE, "Base ring addr = 0x%08x\n", ring_addr);
	addr = ring_addr + BRCMF_SHARED_RING_MAX_SUB_QUEUES;
	max_sub_queues = brcmf_pcie_read_tcm16(devinfo, addr);

	if (devinfo->dma_idx_sz != 0) {
		bufsz = (BRCMF_NROF_D2H_COMMON_MSGRINGS + max_sub_queues) *
			devinfo->dma_idx_sz * 2;
		devinfo->idxbuf = dma_alloc_coherent(&devinfo->pdev->dev, bufsz,
						     &devinfo->idxbuf_dmahandle,
						     GFP_KERNEL);
		if (!devinfo->idxbuf)
			devinfo->dma_idx_sz = 0;
	}

	if (devinfo->dma_idx_sz == 0) {
		addr = ring_addr + BRCMF_SHARED_RING_D2H_W_IDX_PTR_OFFSET;
		d2h_w_idx_ptr = brcmf_pcie_read_tcm32(devinfo, addr);
		addr = ring_addr + BRCMF_SHARED_RING_D2H_R_IDX_PTR_OFFSET;
		d2h_r_idx_ptr = brcmf_pcie_read_tcm32(devinfo, addr);
		addr = ring_addr + BRCMF_SHARED_RING_H2D_W_IDX_PTR_OFFSET;
		h2d_w_idx_ptr = brcmf_pcie_read_tcm32(devinfo, addr);
		addr = ring_addr + BRCMF_SHARED_RING_H2D_R_IDX_PTR_OFFSET;
		h2d_r_idx_ptr = brcmf_pcie_read_tcm32(devinfo, addr);
		idx_offset = sizeof(u32);
		devinfo->write_ptr = brcmf_pcie_write_tcm16;
		devinfo->read_ptr = brcmf_pcie_read_tcm16;
		brcmf_dbg(PCIE, "Using TCM indices\n");
	} else {
		memset(devinfo->idxbuf, 0, bufsz);
		devinfo->idxbuf_sz = bufsz;
		idx_offset = devinfo->dma_idx_sz;
		devinfo->write_ptr = brcmf_pcie_write_idx;
		devinfo->read_ptr = brcmf_pcie_read_idx;

		h2d_w_idx_ptr = 0;
		addr = ring_addr + BRCMF_SHARED_RING_H2D_WP_HADDR_OFFSET;
		address = (u64)devinfo->idxbuf_dmahandle;
		brcmf_pcie_write_tcm32(devinfo, addr, address & 0xffffffff);
		brcmf_pcie_write_tcm32(devinfo, addr + 4, address >> 32);

		h2d_r_idx_ptr = h2d_w_idx_ptr + max_sub_queues * idx_offset;
		addr = ring_addr + BRCMF_SHARED_RING_H2D_RP_HADDR_OFFSET;
		address += max_sub_queues * idx_offset;
		brcmf_pcie_write_tcm32(devinfo, addr, address & 0xffffffff);
		brcmf_pcie_write_tcm32(devinfo, addr + 4, address >> 32);

		d2h_w_idx_ptr = h2d_r_idx_ptr + max_sub_queues * idx_offset;
		addr = ring_addr + BRCMF_SHARED_RING_D2H_WP_HADDR_OFFSET;
		address += max_sub_queues * idx_offset;
		brcmf_pcie_write_tcm32(devinfo, addr, address & 0xffffffff);
		brcmf_pcie_write_tcm32(devinfo, addr + 4, address >> 32);

		d2h_r_idx_ptr = d2h_w_idx_ptr +
				BRCMF_NROF_D2H_COMMON_MSGRINGS * idx_offset;
		addr = ring_addr + BRCMF_SHARED_RING_D2H_RP_HADDR_OFFSET;
		address += BRCMF_NROF_D2H_COMMON_MSGRINGS * idx_offset;
		brcmf_pcie_write_tcm32(devinfo, addr, address & 0xffffffff);
		brcmf_pcie_write_tcm32(devinfo, addr + 4, address >> 32);
		brcmf_dbg(PCIE, "Using host memory indices\n");
	}

	addr = ring_addr + BRCMF_SHARED_RING_TCM_MEMLOC_OFFSET;
	ring_mem_ptr = brcmf_pcie_read_tcm32(devinfo, addr);

	for (i = 0; i < BRCMF_NROF_H2D_COMMON_MSGRINGS; i++) {
		ring = brcmf_pcie_alloc_dma_and_ring(devinfo, i, ring_mem_ptr);
		if (!ring)
			goto fail;
		ring->w_idx_addr = h2d_w_idx_ptr;
		ring->r_idx_addr = h2d_r_idx_ptr;
		ring->id = i;
		devinfo->shared.commonrings[i] = ring;

		h2d_w_idx_ptr += idx_offset;
		h2d_r_idx_ptr += idx_offset;
		ring_mem_ptr += BRCMF_RING_MEM_SZ;
	}

	for (i = BRCMF_NROF_H2D_COMMON_MSGRINGS;
	     i < BRCMF_NROF_COMMON_MSGRINGS; i++) {
		ring = brcmf_pcie_alloc_dma_and_ring(devinfo, i, ring_mem_ptr);
		if (!ring)
			goto fail;
		ring->w_idx_addr = d2h_w_idx_ptr;
		ring->r_idx_addr = d2h_r_idx_ptr;
		ring->id = i;
		devinfo->shared.commonrings[i] = ring;

		d2h_w_idx_ptr += idx_offset;
		d2h_r_idx_ptr += idx_offset;
		ring_mem_ptr += BRCMF_RING_MEM_SZ;
	}

	devinfo->shared.nrof_flowrings =
			max_sub_queues - BRCMF_NROF_H2D_COMMON_MSGRINGS;
	rings = kcalloc(devinfo->shared.nrof_flowrings, sizeof(*ring),
			GFP_KERNEL);
	if (!rings)
		goto fail;

	brcmf_dbg(PCIE, "Nr of flowrings is %d\n",
		  devinfo->shared.nrof_flowrings);

	for (i = 0; i < devinfo->shared.nrof_flowrings; i++) {
		ring = &rings[i];
		ring->devinfo = devinfo;
		ring->id = i + BRCMF_NROF_COMMON_MSGRINGS;
		brcmf_commonring_register_cb(&ring->commonring,
					     brcmf_pcie_ring_mb_ring_bell,
					     brcmf_pcie_ring_mb_update_rptr,
					     brcmf_pcie_ring_mb_update_wptr,
					     brcmf_pcie_ring_mb_write_rptr,
					     brcmf_pcie_ring_mb_write_wptr,
					     ring);
		ring->w_idx_addr = h2d_w_idx_ptr;
		ring->r_idx_addr = h2d_r_idx_ptr;
		h2d_w_idx_ptr += idx_offset;
		h2d_r_idx_ptr += idx_offset;
	}
	devinfo->shared.flowrings = rings;

	return 0;

fail:
	brcmf_err("Allocating ring buffers failed\n");
	brcmf_pcie_release_ringbuffers(devinfo);
	return -ENOMEM;
}


static void
brcmf_pcie_release_scratchbuffers(struct brcmf_pciedev_info *devinfo)
{
	if (devinfo->shared.scratch)
		dma_free_coherent(&devinfo->pdev->dev,
				  BRCMF_DMA_D2H_SCRATCH_BUF_LEN,
				  devinfo->shared.scratch,
				  devinfo->shared.scratch_dmahandle);
	if (devinfo->shared.ringupd)
		dma_free_coherent(&devinfo->pdev->dev,
				  BRCMF_DMA_D2H_RINGUPD_BUF_LEN,
				  devinfo->shared.ringupd,
				  devinfo->shared.ringupd_dmahandle);
}

static int brcmf_pcie_init_scratchbuffers(struct brcmf_pciedev_info *devinfo)
{
	u64 address;
	u32 addr;

	devinfo->shared.scratch = dma_alloc_coherent(&devinfo->pdev->dev,
		BRCMF_DMA_D2H_SCRATCH_BUF_LEN,
		&devinfo->shared.scratch_dmahandle, GFP_KERNEL);
	if (!devinfo->shared.scratch)
		goto fail;

	memset(devinfo->shared.scratch, 0, BRCMF_DMA_D2H_SCRATCH_BUF_LEN);

	addr = devinfo->shared.tcm_base_address +
	       BRCMF_SHARED_DMA_SCRATCH_ADDR_OFFSET;
	address = (u64)devinfo->shared.scratch_dmahandle;
	brcmf_pcie_write_tcm32(devinfo, addr, address & 0xffffffff);
	brcmf_pcie_write_tcm32(devinfo, addr + 4, address >> 32);
	addr = devinfo->shared.tcm_base_address +
	       BRCMF_SHARED_DMA_SCRATCH_LEN_OFFSET;
	brcmf_pcie_write_tcm32(devinfo, addr, BRCMF_DMA_D2H_SCRATCH_BUF_LEN);

	devinfo->shared.ringupd = dma_alloc_coherent(&devinfo->pdev->dev,
		BRCMF_DMA_D2H_RINGUPD_BUF_LEN,
		&devinfo->shared.ringupd_dmahandle, GFP_KERNEL);
	if (!devinfo->shared.ringupd)
		goto fail;

	memset(devinfo->shared.ringupd, 0, BRCMF_DMA_D2H_RINGUPD_BUF_LEN);

	addr = devinfo->shared.tcm_base_address +
	       BRCMF_SHARED_DMA_RINGUPD_ADDR_OFFSET;
	address = (u64)devinfo->shared.ringupd_dmahandle;
	brcmf_pcie_write_tcm32(devinfo, addr, address & 0xffffffff);
	brcmf_pcie_write_tcm32(devinfo, addr + 4, address >> 32);
	addr = devinfo->shared.tcm_base_address +
	       BRCMF_SHARED_DMA_RINGUPD_LEN_OFFSET;
	brcmf_pcie_write_tcm32(devinfo, addr, BRCMF_DMA_D2H_RINGUPD_BUF_LEN);
	return 0;

fail:
	brcmf_err("Allocating scratch buffers failed\n");
	brcmf_pcie_release_scratchbuffers(devinfo);
	return -ENOMEM;
}


static void brcmf_pcie_down(struct device *dev)
{
}


static int brcmf_pcie_tx(struct device *dev, struct sk_buff *skb)
{
	return 0;
}


static int brcmf_pcie_tx_ctlpkt(struct device *dev, unsigned char *msg,
				uint len)
{
	return 0;
}


static int brcmf_pcie_rx_ctlpkt(struct device *dev, unsigned char *msg,
				uint len)
{
	return 0;
}


static void brcmf_pcie_wowl_config(struct device *dev, bool enabled)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pciedev *buspub = bus_if->bus_priv.pcie;
	struct brcmf_pciedev_info *devinfo = buspub->devinfo;

	brcmf_dbg(PCIE, "Configuring WOWL, enabled=%d\n", enabled);
	devinfo->wowl_enabled = enabled;
}


static size_t brcmf_pcie_get_ramsize(struct device *dev)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pciedev *buspub = bus_if->bus_priv.pcie;
	struct brcmf_pciedev_info *devinfo = buspub->devinfo;

	return devinfo->ci->ramsize - devinfo->ci->srsize;
}


static int brcmf_pcie_get_memdump(struct device *dev, void *data, size_t len)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pciedev *buspub = bus_if->bus_priv.pcie;
	struct brcmf_pciedev_info *devinfo = buspub->devinfo;

	brcmf_dbg(PCIE, "dump at 0x%08X: len=%zu\n", devinfo->ci->rambase, len);
	brcmf_pcie_copy_dev_tomem(devinfo, devinfo->ci->rambase, data, len);
	return 0;
}


static const struct brcmf_bus_ops brcmf_pcie_bus_ops = {
	.txdata = brcmf_pcie_tx,
	.stop = brcmf_pcie_down,
	.txctl = brcmf_pcie_tx_ctlpkt,
	.rxctl = brcmf_pcie_rx_ctlpkt,
	.wowl_config = brcmf_pcie_wowl_config,
	.get_ramsize = brcmf_pcie_get_ramsize,
	.get_memdump = brcmf_pcie_get_memdump,
};


static void
brcmf_pcie_adjust_ramsize(struct brcmf_pciedev_info *devinfo, u8 *data,
			  u32 data_len)
{
	__le32 *field;
	u32 newsize;

	if (data_len < BRCMF_RAMSIZE_OFFSET + 8)
		return;

	field = (__le32 *)&data[BRCMF_RAMSIZE_OFFSET];
	if (le32_to_cpup(field) != BRCMF_RAMSIZE_MAGIC)
		return;
	field++;
	newsize = le32_to_cpup(field);

	brcmf_dbg(PCIE, "Found ramsize info in FW, adjusting to 0x%x\n",
		  newsize);
	devinfo->ci->ramsize = newsize;
}


static int
brcmf_pcie_init_share_ram_info(struct brcmf_pciedev_info *devinfo,
			       u32 sharedram_addr)
{
	struct brcmf_pcie_shared_info *shared;
	u32 addr;
	u32 version;

	shared = &devinfo->shared;
	shared->tcm_base_address = sharedram_addr;

	shared->flags = brcmf_pcie_read_tcm32(devinfo, sharedram_addr);
	version = shared->flags & BRCMF_PCIE_SHARED_VERSION_MASK;
	brcmf_dbg(PCIE, "PCIe protocol version %d\n", version);
	if ((version > BRCMF_PCIE_MAX_SHARED_VERSION) ||
	    (version < BRCMF_PCIE_MIN_SHARED_VERSION)) {
		brcmf_err("Unsupported PCIE version %d\n", version);
		return -EINVAL;
	}

	/* check firmware support dma indicies */
	if (shared->flags & BRCMF_PCIE_SHARED_DMA_INDEX) {
		if (shared->flags & BRCMF_PCIE_SHARED_DMA_2B_IDX)
			devinfo->dma_idx_sz = sizeof(u16);
		else
			devinfo->dma_idx_sz = sizeof(u32);
	}

	addr = sharedram_addr + BRCMF_SHARED_MAX_RXBUFPOST_OFFSET;
	shared->max_rxbufpost = brcmf_pcie_read_tcm16(devinfo, addr);
	if (shared->max_rxbufpost == 0)
		shared->max_rxbufpost = BRCMF_DEF_MAX_RXBUFPOST;

	addr = sharedram_addr + BRCMF_SHARED_RX_DATAOFFSET_OFFSET;
	shared->rx_dataoffset = brcmf_pcie_read_tcm32(devinfo, addr);

	addr = sharedram_addr + BRCMF_SHARED_HTOD_MB_DATA_ADDR_OFFSET;
	shared->htod_mb_data_addr = brcmf_pcie_read_tcm32(devinfo, addr);

	addr = sharedram_addr + BRCMF_SHARED_DTOH_MB_DATA_ADDR_OFFSET;
	shared->dtoh_mb_data_addr = brcmf_pcie_read_tcm32(devinfo, addr);

	addr = sharedram_addr + BRCMF_SHARED_RING_INFO_ADDR_OFFSET;
	shared->ring_info_addr = brcmf_pcie_read_tcm32(devinfo, addr);

	brcmf_dbg(PCIE, "max rx buf post %d, rx dataoffset %d\n",
		  shared->max_rxbufpost, shared->rx_dataoffset);

	brcmf_pcie_bus_console_init(devinfo);

	return 0;
}


static int brcmf_pcie_download_fw_nvram(struct brcmf_pciedev_info *devinfo,
					const struct firmware *fw, void *nvram,
					u32 nvram_len)
{
	u32 sharedram_addr;
	u32 sharedram_addr_written;
	u32 loop_counter;
	int err;
	u32 address;
	u32 resetintr;

	brcmf_dbg(PCIE, "Halt ARM.\n");
	err = brcmf_pcie_enter_download_state(devinfo);
	if (err)
		return err;

	brcmf_dbg(PCIE, "Download FW %s\n", devinfo->fw_name);
	brcmf_pcie_copy_mem_todev(devinfo, devinfo->ci->rambase,
				  (void *)fw->data, fw->size);

	resetintr = get_unaligned_le32(fw->data);
	release_firmware(fw);

	/* reset last 4 bytes of RAM address. to be used for shared
	 * area. This identifies when FW is running
	 */
	brcmf_pcie_write_ram32(devinfo, devinfo->ci->ramsize - 4, 0);

	if (nvram) {
		brcmf_dbg(PCIE, "Download NVRAM %s\n", devinfo->nvram_name);
		address = devinfo->ci->rambase + devinfo->ci->ramsize -
			  nvram_len;
		brcmf_pcie_copy_mem_todev(devinfo, address, nvram, nvram_len);
		brcmf_fw_nvram_free(nvram);
	} else {
		brcmf_dbg(PCIE, "No matching NVRAM file found %s\n",
			  devinfo->nvram_name);
	}

	sharedram_addr_written = brcmf_pcie_read_ram32(devinfo,
						       devinfo->ci->ramsize -
						       4);
	brcmf_dbg(PCIE, "Bring ARM in running state\n");
	err = brcmf_pcie_exit_download_state(devinfo, resetintr);
	if (err)
		return err;

	brcmf_dbg(PCIE, "Wait for FW init\n");
	sharedram_addr = sharedram_addr_written;
	loop_counter = BRCMF_PCIE_FW_UP_TIMEOUT / 50;
	while ((sharedram_addr == sharedram_addr_written) && (loop_counter)) {
		msleep(50);
		sharedram_addr = brcmf_pcie_read_ram32(devinfo,
						       devinfo->ci->ramsize -
						       4);
		loop_counter--;
	}
	if (sharedram_addr == sharedram_addr_written) {
		brcmf_err("FW failed to initialize\n");
		return -ENODEV;
	}
	brcmf_dbg(PCIE, "Shared RAM addr: 0x%08x\n", sharedram_addr);

	return (brcmf_pcie_init_share_ram_info(devinfo, sharedram_addr));
}


static int brcmf_pcie_get_resource(struct brcmf_pciedev_info *devinfo)
{
	struct pci_dev *pdev;
	int err;
	phys_addr_t  bar0_addr, bar1_addr;
	ulong bar1_size;

	pdev = devinfo->pdev;

	err = pci_enable_device(pdev);
	if (err) {
		brcmf_err("pci_enable_device failed err=%d\n", err);
		return err;
	}

	pci_set_master(pdev);

	/* Bar-0 mapped address */
	bar0_addr = pci_resource_start(pdev, 0);
	/* Bar-1 mapped address */
	bar1_addr = pci_resource_start(pdev, 2);
	/* read Bar-1 mapped memory range */
	bar1_size = pci_resource_len(pdev, 2);
	if ((bar1_size == 0) || (bar1_addr == 0)) {
		brcmf_err("BAR1 Not enabled, device size=%ld, addr=%#016llx\n",
			  bar1_size, (unsigned long long)bar1_addr);
		return -EINVAL;
	}

	devinfo->regs = ioremap_nocache(bar0_addr, BRCMF_PCIE_REG_MAP_SIZE);
	devinfo->tcm = ioremap_nocache(bar1_addr, bar1_size);

	if (!devinfo->regs || !devinfo->tcm) {
		brcmf_err("ioremap() failed (%p,%p)\n", devinfo->regs,
			  devinfo->tcm);
		return -EINVAL;
	}
	brcmf_dbg(PCIE, "Phys addr : reg space = %p base addr %#016llx\n",
		  devinfo->regs, (unsigned long long)bar0_addr);
	brcmf_dbg(PCIE, "Phys addr : mem space = %p base addr %#016llx size 0x%x\n",
		  devinfo->tcm, (unsigned long long)bar1_addr,
		  (unsigned int)bar1_size);

	return 0;
}


static void brcmf_pcie_release_resource(struct brcmf_pciedev_info *devinfo)
{
	if (devinfo->tcm)
		iounmap(devinfo->tcm);
	if (devinfo->regs)
		iounmap(devinfo->regs);

	pci_disable_device(devinfo->pdev);
}


static int brcmf_pcie_attach_bus(struct brcmf_pciedev_info *devinfo)
{
	int ret;

	/* Attach to the common driver interface */
	ret = brcmf_attach(&devinfo->pdev->dev, devinfo->settings);
	if (ret) {
		brcmf_err("brcmf_attach failed\n");
	} else {
		ret = brcmf_bus_start(&devinfo->pdev->dev);
		if (ret)
			brcmf_err("dongle is not responding\n");
	}

	return ret;
}


static u32 brcmf_pcie_buscore_prep_addr(const struct pci_dev *pdev, u32 addr)
{
	u32 ret_addr;

	ret_addr = addr & (BRCMF_PCIE_BAR0_REG_SIZE - 1);
	addr &= ~(BRCMF_PCIE_BAR0_REG_SIZE - 1);
	pci_write_config_dword(pdev, BRCMF_PCIE_BAR0_WINDOW, addr);

	return ret_addr;
}


static u32 brcmf_pcie_buscore_read32(void *ctx, u32 addr)
{
	struct brcmf_pciedev_info *devinfo = (struct brcmf_pciedev_info *)ctx;

	addr = brcmf_pcie_buscore_prep_addr(devinfo->pdev, addr);
	return brcmf_pcie_read_reg32(devinfo, addr);
}


static void brcmf_pcie_buscore_write32(void *ctx, u32 addr, u32 value)
{
	struct brcmf_pciedev_info *devinfo = (struct brcmf_pciedev_info *)ctx;

	addr = brcmf_pcie_buscore_prep_addr(devinfo->pdev, addr);
	brcmf_pcie_write_reg32(devinfo, addr, value);
}


static int brcmf_pcie_buscoreprep(void *ctx)
{
	return brcmf_pcie_get_resource(ctx);
}


static int brcmf_pcie_buscore_reset(void *ctx, struct brcmf_chip *chip)
{
	struct brcmf_pciedev_info *devinfo = (struct brcmf_pciedev_info *)ctx;
	u32 val;

	devinfo->ci = chip;
	brcmf_pcie_reset_device(devinfo);

	val = brcmf_pcie_read_reg32(devinfo, BRCMF_PCIE_PCIE2REG_MAILBOXINT);
	if (val != 0xffffffff)
		brcmf_pcie_write_reg32(devinfo, BRCMF_PCIE_PCIE2REG_MAILBOXINT,
				       val);

	return 0;
}


static void brcmf_pcie_buscore_activate(void *ctx, struct brcmf_chip *chip,
					u32 rstvec)
{
	struct brcmf_pciedev_info *devinfo = (struct brcmf_pciedev_info *)ctx;

	brcmf_pcie_write_tcm32(devinfo, 0, rstvec);
}


static const struct brcmf_buscore_ops brcmf_pcie_buscore_ops = {
	.prepare = brcmf_pcie_buscoreprep,
	.reset = brcmf_pcie_buscore_reset,
	.activate = brcmf_pcie_buscore_activate,
	.read32 = brcmf_pcie_buscore_read32,
	.write32 = brcmf_pcie_buscore_write32,
};

static void brcmf_pcie_setup(struct device *dev, const struct firmware *fw,
			     void *nvram, u32 nvram_len)
{
	struct brcmf_bus *bus = dev_get_drvdata(dev);
	struct brcmf_pciedev *pcie_bus_dev = bus->bus_priv.pcie;
	struct brcmf_pciedev_info *devinfo = pcie_bus_dev->devinfo;
	struct brcmf_commonring **flowrings;
	int ret;
	u32 i;

	brcmf_pcie_attach(devinfo);

	/* Some of the firmwares have the size of the memory of the device
	 * defined inside the firmware. This is because part of the memory in
	 * the device is shared and the devision is determined by FW. Parse
	 * the firmware and adjust the chip memory size now.
	 */
	brcmf_pcie_adjust_ramsize(devinfo, (u8 *)fw->data, fw->size);

	ret = brcmf_pcie_download_fw_nvram(devinfo, fw, nvram, nvram_len);
	if (ret)
		goto fail;

	devinfo->state = BRCMFMAC_PCIE_STATE_UP;

	ret = brcmf_pcie_init_ringbuffers(devinfo);
	if (ret)
		goto fail;

	ret = brcmf_pcie_init_scratchbuffers(devinfo);
	if (ret)
		goto fail;

	brcmf_pcie_select_core(devinfo, BCMA_CORE_PCIE2);
	ret = brcmf_pcie_request_irq(devinfo);
	if (ret)
		goto fail;

	/* hook the commonrings in the bus structure. */
	for (i = 0; i < BRCMF_NROF_COMMON_MSGRINGS; i++)
		bus->msgbuf->commonrings[i] =
				&devinfo->shared.commonrings[i]->commonring;

	flowrings = kcalloc(devinfo->shared.nrof_flowrings, sizeof(*flowrings),
			    GFP_KERNEL);
	if (!flowrings)
		goto fail;

	for (i = 0; i < devinfo->shared.nrof_flowrings; i++)
		flowrings[i] = &devinfo->shared.flowrings[i].commonring;
	bus->msgbuf->flowrings = flowrings;

	bus->msgbuf->rx_dataoffset = devinfo->shared.rx_dataoffset;
	bus->msgbuf->max_rxbufpost = devinfo->shared.max_rxbufpost;
	bus->msgbuf->nrof_flowrings = devinfo->shared.nrof_flowrings;

	init_waitqueue_head(&devinfo->mbdata_resp_wait);

	brcmf_pcie_intr_enable(devinfo);
	if (brcmf_pcie_attach_bus(devinfo) == 0)
		return;

	brcmf_pcie_bus_console_read(devinfo);

fail:
	device_release_driver(dev);
}

static int
brcmf_pcie_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int ret;
	struct brcmf_pciedev_info *devinfo;
	struct brcmf_pciedev *pcie_bus_dev;
	struct brcmf_bus *bus;
	u16 domain_nr;
	u16 bus_nr;

	domain_nr = pci_domain_nr(pdev->bus) + 1;
	bus_nr = pdev->bus->number;
	brcmf_dbg(PCIE, "Enter %x:%x (%d/%d)\n", pdev->vendor, pdev->device,
		  domain_nr, bus_nr);

	ret = -ENOMEM;
	devinfo = kzalloc(sizeof(*devinfo), GFP_KERNEL);
	if (devinfo == NULL)
		return ret;

	devinfo->pdev = pdev;
	pcie_bus_dev = NULL;
	devinfo->ci = brcmf_chip_attach(devinfo, &brcmf_pcie_buscore_ops);
	if (IS_ERR(devinfo->ci)) {
		ret = PTR_ERR(devinfo->ci);
		devinfo->ci = NULL;
		goto fail;
	}

	pcie_bus_dev = kzalloc(sizeof(*pcie_bus_dev), GFP_KERNEL);
	if (pcie_bus_dev == NULL) {
		ret = -ENOMEM;
		goto fail;
	}

	devinfo->settings = brcmf_get_module_param(&devinfo->pdev->dev,
						   BRCMF_BUSTYPE_PCIE,
						   devinfo->ci->chip,
						   devinfo->ci->chiprev);
	if (!devinfo->settings) {
		ret = -ENOMEM;
		goto fail;
	}

	bus = kzalloc(sizeof(*bus), GFP_KERNEL);
	if (!bus) {
		ret = -ENOMEM;
		goto fail;
	}
	bus->msgbuf = kzalloc(sizeof(*bus->msgbuf), GFP_KERNEL);
	if (!bus->msgbuf) {
		ret = -ENOMEM;
		kfree(bus);
		goto fail;
	}

	/* hook it all together. */
	pcie_bus_dev->devinfo = devinfo;
	pcie_bus_dev->bus = bus;
	bus->dev = &pdev->dev;
	bus->bus_priv.pcie = pcie_bus_dev;
	bus->ops = &brcmf_pcie_bus_ops;
	bus->proto_type = BRCMF_PROTO_MSGBUF;
	bus->chip = devinfo->coreid;
	bus->wowl_supported = pci_pme_capable(pdev, PCI_D3hot);
	dev_set_drvdata(&pdev->dev, bus);

	ret = brcmf_fw_map_chip_to_name(devinfo->ci->chip, devinfo->ci->chiprev,
					brcmf_pcie_fwnames,
					ARRAY_SIZE(brcmf_pcie_fwnames),
					devinfo->fw_name, devinfo->nvram_name);
	if (ret)
		goto fail_bus;

	ret = brcmf_fw_get_firmwares_pcie(bus->dev, BRCMF_FW_REQUEST_NVRAM |
						    BRCMF_FW_REQ_NV_OPTIONAL,
					  devinfo->fw_name, devinfo->nvram_name,
					  brcmf_pcie_setup, domain_nr, bus_nr);
	if (ret == 0)
		return 0;
fail_bus:
	kfree(bus->msgbuf);
	kfree(bus);
fail:
	brcmf_err("failed %x:%x\n", pdev->vendor, pdev->device);
	brcmf_pcie_release_resource(devinfo);
	if (devinfo->ci)
		brcmf_chip_detach(devinfo->ci);
	if (devinfo->settings)
		brcmf_release_module_param(devinfo->settings);
	kfree(pcie_bus_dev);
	kfree(devinfo);
	return ret;
}


static void
brcmf_pcie_remove(struct pci_dev *pdev)
{
	struct brcmf_pciedev_info *devinfo;
	struct brcmf_bus *bus;

	brcmf_dbg(PCIE, "Enter\n");

	bus = dev_get_drvdata(&pdev->dev);
	if (bus == NULL)
		return;

	devinfo = bus->bus_priv.pcie->devinfo;

	devinfo->state = BRCMFMAC_PCIE_STATE_DOWN;
	if (devinfo->ci)
		brcmf_pcie_intr_disable(devinfo);

	brcmf_detach(&pdev->dev);

	kfree(bus->bus_priv.pcie);
	kfree(bus->msgbuf->flowrings);
	kfree(bus->msgbuf);
	kfree(bus);

	brcmf_pcie_release_irq(devinfo);
	brcmf_pcie_release_scratchbuffers(devinfo);
	brcmf_pcie_release_ringbuffers(devinfo);
	brcmf_pcie_reset_device(devinfo);
	brcmf_pcie_release_resource(devinfo);

	if (devinfo->ci)
		brcmf_chip_detach(devinfo->ci);
	if (devinfo->settings)
		brcmf_release_module_param(devinfo->settings);

	kfree(devinfo);
	dev_set_drvdata(&pdev->dev, NULL);
}


#ifdef CONFIG_PM


static int brcmf_pcie_pm_enter_D3(struct device *dev)
{
	struct brcmf_pciedev_info *devinfo;
	struct brcmf_bus *bus;

	brcmf_dbg(PCIE, "Enter\n");

	bus = dev_get_drvdata(dev);
	devinfo = bus->bus_priv.pcie->devinfo;

	brcmf_bus_change_state(bus, BRCMF_BUS_DOWN);

	devinfo->mbdata_completed = false;
	brcmf_pcie_send_mb_data(devinfo, BRCMF_H2D_HOST_D3_INFORM);

	wait_event_timeout(devinfo->mbdata_resp_wait, devinfo->mbdata_completed,
			   BRCMF_PCIE_MBDATA_TIMEOUT);
	if (!devinfo->mbdata_completed) {
		brcmf_err("Timeout on response for entering D3 substate\n");
		return -EIO;
	}

	devinfo->state = BRCMFMAC_PCIE_STATE_DOWN;

	return 0;
}


static int brcmf_pcie_pm_leave_D3(struct device *dev)
{
	struct brcmf_pciedev_info *devinfo;
	struct brcmf_bus *bus;
	struct pci_dev *pdev;
	int err;

	brcmf_dbg(PCIE, "Enter\n");

	bus = dev_get_drvdata(dev);
	devinfo = bus->bus_priv.pcie->devinfo;
	brcmf_dbg(PCIE, "Enter, dev=%p, bus=%p\n", dev, bus);

	/* Check if device is still up and running, if so we are ready */
	if (brcmf_pcie_read_reg32(devinfo, BRCMF_PCIE_PCIE2REG_INTMASK) != 0) {
		brcmf_dbg(PCIE, "Try to wakeup device....\n");
		if (brcmf_pcie_send_mb_data(devinfo, BRCMF_H2D_HOST_D0_INFORM))
			goto cleanup;
		brcmf_dbg(PCIE, "Hot resume, continue....\n");
		devinfo->state = BRCMFMAC_PCIE_STATE_UP;
		brcmf_pcie_select_core(devinfo, BCMA_CORE_PCIE2);
		brcmf_bus_change_state(bus, BRCMF_BUS_UP);
		brcmf_pcie_intr_enable(devinfo);
		return 0;
	}

cleanup:
	brcmf_chip_detach(devinfo->ci);
	devinfo->ci = NULL;
	pdev = devinfo->pdev;
	brcmf_pcie_remove(pdev);

	err = brcmf_pcie_probe(pdev, NULL);
	if (err)
		brcmf_err("probe after resume failed, err=%d\n", err);

	return err;
}


static const struct dev_pm_ops brcmf_pciedrvr_pm = {
	.suspend = brcmf_pcie_pm_enter_D3,
	.resume = brcmf_pcie_pm_leave_D3,
	.freeze = brcmf_pcie_pm_enter_D3,
	.restore = brcmf_pcie_pm_leave_D3,
};


#endif /* CONFIG_PM */


#define BRCMF_PCIE_DEVICE(dev_id)	{ BRCM_PCIE_VENDOR_ID_BROADCOM, dev_id,\
	PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_NETWORK_OTHER << 8, 0xffff00, 0 }
#define BRCMF_PCIE_DEVICE_SUB(dev_id, subvend, subdev)	{ \
	BRCM_PCIE_VENDOR_ID_BROADCOM, dev_id,\
	subvend, subdev, PCI_CLASS_NETWORK_OTHER << 8, 0xffff00, 0 }

static struct pci_device_id brcmf_pcie_devid_table[] = {
	BRCMF_PCIE_DEVICE(BRCM_PCIE_4350_DEVICE_ID),
	BRCMF_PCIE_DEVICE(BRCM_PCIE_4356_DEVICE_ID),
	BRCMF_PCIE_DEVICE(BRCM_PCIE_43567_DEVICE_ID),
	BRCMF_PCIE_DEVICE(BRCM_PCIE_43570_DEVICE_ID),
	BRCMF_PCIE_DEVICE(BRCM_PCIE_4358_DEVICE_ID),
	BRCMF_PCIE_DEVICE(BRCM_PCIE_4359_DEVICE_ID),
	BRCMF_PCIE_DEVICE(BRCM_PCIE_43602_DEVICE_ID),
	BRCMF_PCIE_DEVICE(BRCM_PCIE_43602_2G_DEVICE_ID),
	BRCMF_PCIE_DEVICE(BRCM_PCIE_43602_5G_DEVICE_ID),
	BRCMF_PCIE_DEVICE(BRCM_PCIE_43602_RAW_DEVICE_ID),
	BRCMF_PCIE_DEVICE(BRCM_PCIE_4365_DEVICE_ID),
	BRCMF_PCIE_DEVICE(BRCM_PCIE_4365_2G_DEVICE_ID),
	BRCMF_PCIE_DEVICE(BRCM_PCIE_4365_5G_DEVICE_ID),
	BRCMF_PCIE_DEVICE_SUB(0x4365, BRCM_PCIE_VENDOR_ID_BROADCOM, 0x4365),
	BRCMF_PCIE_DEVICE(BRCM_PCIE_4366_DEVICE_ID),
	BRCMF_PCIE_DEVICE(BRCM_PCIE_4366_2G_DEVICE_ID),
	BRCMF_PCIE_DEVICE(BRCM_PCIE_4366_5G_DEVICE_ID),
	BRCMF_PCIE_DEVICE(BRCM_PCIE_4371_DEVICE_ID),
	{ /* end: all zeroes */ }
};


MODULE_DEVICE_TABLE(pci, brcmf_pcie_devid_table);


static struct pci_driver brcmf_pciedrvr = {
	.node = {},
	.name = KBUILD_MODNAME,
	.id_table = brcmf_pcie_devid_table,
	.probe = brcmf_pcie_probe,
	.remove = brcmf_pcie_remove,
#ifdef CONFIG_PM
	.driver.pm = &brcmf_pciedrvr_pm,
#endif
};


void brcmf_pcie_register(void)
{
	int err;

	brcmf_dbg(PCIE, "Enter\n");
	err = pci_register_driver(&brcmf_pciedrvr);
	if (err)
		brcmf_err("PCIE driver registration failed, err=%d\n", err);
}


void brcmf_pcie_exit(void)
{
	brcmf_dbg(PCIE, "Enter\n");
	pci_unregister_driver(&brcmf_pciedrvr);
}
