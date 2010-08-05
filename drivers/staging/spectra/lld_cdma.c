/*
 * NAND Flash Controller Device Driver
 * Copyright (c) 2009, Intel Corporation and its suppliers.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <linux/fs.h>
#include <linux/slab.h>

#include "spectraswconfig.h"
#include "lld.h"
#include "lld_nand.h"
#include "lld_cdma.h"
#include "lld_emu.h"
#include "flash.h"
#include "nand_regs.h"

#define MAX_PENDING_CMDS    4
#define MODE_02             (0x2 << 26)

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     CDMA_Data_Cmd
* Inputs:   cmd code (aligned for hw)
*               data: pointer to source or destination
*               block: block address
*               page: page address
*               num: num pages to transfer
* Outputs:      PASS
* Description:  This function takes the parameters and puts them
*                   into the "pending commands" array.
*               It does not parse or validate the parameters.
*               The array index is same as the tag.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 CDMA_Data_CMD(u8 cmd, u8 *data, u32 block, u16 page, u16 num, u16 flags)
{
	u8 bank;

	nand_dbg_print(NAND_DBG_DEBUG, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	if (0 == cmd)
		nand_dbg_print(NAND_DBG_DEBUG,
		"%s, Line %d, Illegal cmd (0)\n", __FILE__, __LINE__);

	/* If a command of another bank comes, then first execute */
	/* pending commands of the current bank, then set the new */
	/* bank as current bank */
	bank = block / (DeviceInfo.wTotalBlocks / totalUsedBanks);
	if (bank != info.flash_bank) {
		nand_dbg_print(NAND_DBG_WARN,
			"Will access new bank. old bank: %d, new bank: %d\n",
			info.flash_bank, bank);
		if (CDMA_Execute_CMDs()) {
			printk(KERN_ERR "CDMA_Execute_CMDs fail!\n");
			return FAIL;
		}
		info.flash_bank = bank;
	}

	info.pcmds[info.pcmds_num].CMD = cmd;
	info.pcmds[info.pcmds_num].DataAddr = data;
	info.pcmds[info.pcmds_num].Block = block;
	info.pcmds[info.pcmds_num].Page = page;
	info.pcmds[info.pcmds_num].PageCount = num;
	info.pcmds[info.pcmds_num].DataDestAddr = 0;
	info.pcmds[info.pcmds_num].DataSrcAddr = 0;
	info.pcmds[info.pcmds_num].MemCopyByteCnt = 0;
	info.pcmds[info.pcmds_num].Flags = flags;
	info.pcmds[info.pcmds_num].Status = 0xB0B;

	switch (cmd) {
	case WRITE_MAIN_SPARE_CMD:
		Conv_Main_Spare_Data_Log2Phy_Format(data, num);
		break;
	case WRITE_SPARE_CMD:
		Conv_Spare_Data_Log2Phy_Format(data);
		break;
	default:
		break;
	}

	info.pcmds_num++;

	if (info.pcmds_num >= MAX_PENDING_CMDS) {
		if (CDMA_Execute_CMDs()) {
			printk(KERN_ERR "CDMA_Execute_CMDs fail!\n");
			return FAIL;
		}
	}

	return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     CDMA_MemCopy_CMD
* Inputs:       dest: pointer to destination
*               src:  pointer to source
*               count: num bytes to transfer
* Outputs:      PASS
* Description:  This function takes the parameters and puts them
*                   into the "pending commands" array.
*               It does not parse or validate the parameters.
*               The array index is same as the tag.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 CDMA_MemCopy_CMD(u8 *dest, u8 *src, u32 byte_cnt, u16 flags)
{
	nand_dbg_print(NAND_DBG_DEBUG, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	info.pcmds[info.pcmds_num].CMD = MEMCOPY_CMD;
	info.pcmds[info.pcmds_num].DataAddr = 0;
	info.pcmds[info.pcmds_num].Block = 0;
	info.pcmds[info.pcmds_num].Page = 0;
	info.pcmds[info.pcmds_num].PageCount = 0;
	info.pcmds[info.pcmds_num].DataDestAddr = dest;
	info.pcmds[info.pcmds_num].DataSrcAddr = src;
	info.pcmds[info.pcmds_num].MemCopyByteCnt = byte_cnt;
	info.pcmds[info.pcmds_num].Flags = flags;
	info.pcmds[info.pcmds_num].Status = 0xB0B;

	info.pcmds_num++;

	if (info.pcmds_num >= MAX_PENDING_CMDS) {
		if (CDMA_Execute_CMDs()) {
			printk(KERN_ERR "CDMA_Execute_CMDs fail!\n");
			return FAIL;
		}
	}

	return PASS;
}

#if 0
/* Prints the PendingCMDs array */
void print_pending_cmds(void)
{
	u16 i;

	nand_dbg_print(NAND_DBG_DEBUG, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	for (i = 0; i < info.pcmds_num; i++) {
		nand_dbg_print(NAND_DBG_DEBUG, "\ni: %d\n", i);
		switch (info.pcmds[i].CMD) {
		case ERASE_CMD:
			nand_dbg_print(NAND_DBG_DEBUG,
				"Erase Command (0x%x)\n",
				info.pcmds[i].CMD);
			break;
		case WRITE_MAIN_CMD:
			nand_dbg_print(NAND_DBG_DEBUG,
				"Write Main Command (0x%x)\n",
				info.pcmds[i].CMD);
			break;
		case WRITE_MAIN_SPARE_CMD:
			nand_dbg_print(NAND_DBG_DEBUG,
				"Write Main Spare Command (0x%x)\n",
				info.pcmds[i].CMD);
			break;
		case READ_MAIN_SPARE_CMD:
			nand_dbg_print(NAND_DBG_DEBUG,
				"Read Main Spare Command (0x%x)\n",
				info.pcmds[i].CMD);
			break;
		case READ_MAIN_CMD:
			nand_dbg_print(NAND_DBG_DEBUG,
				"Read Main Command (0x%x)\n",
				info.pcmds[i].CMD);
			break;
		case MEMCOPY_CMD:
			nand_dbg_print(NAND_DBG_DEBUG,
				"Memcopy Command (0x%x)\n",
				info.pcmds[i].CMD);
			break;
		case DUMMY_CMD:
			nand_dbg_print(NAND_DBG_DEBUG,
				"Dummy Command (0x%x)\n",
				info.pcmds[i].CMD);
			break;
		default:
			nand_dbg_print(NAND_DBG_DEBUG,
				"Illegal Command (0x%x)\n",
				info.pcmds[i].CMD);
			break;
		}

		nand_dbg_print(NAND_DBG_DEBUG, "DataAddr: 0x%x\n",
			(u32)info.pcmds[i].DataAddr);
		nand_dbg_print(NAND_DBG_DEBUG, "Block: %d\n",
			info.pcmds[i].Block);
		nand_dbg_print(NAND_DBG_DEBUG, "Page: %d\n",
			info.pcmds[i].Page);
		nand_dbg_print(NAND_DBG_DEBUG, "PageCount: %d\n",
			info.pcmds[i].PageCount);
		nand_dbg_print(NAND_DBG_DEBUG, "DataDestAddr: 0x%x\n",
			(u32)info.pcmds[i].DataDestAddr);
		nand_dbg_print(NAND_DBG_DEBUG, "DataSrcAddr: 0x%x\n",
			(u32)info.pcmds[i].DataSrcAddr);
		nand_dbg_print(NAND_DBG_DEBUG, "MemCopyByteCnt: %d\n",
			info.pcmds[i].MemCopyByteCnt);
		nand_dbg_print(NAND_DBG_DEBUG, "Flags: 0x%x\n",
			info.pcmds[i].Flags);
		nand_dbg_print(NAND_DBG_DEBUG, "Status: 0x%x\n",
			info.pcmds[i].Status);
	}
}

/* Print the CDMA descriptors */
void print_cdma_descriptors(void)
{
	struct cdma_descriptor *pc;
	int i;

	pc = (struct cdma_descriptor *)info.cdma_desc_buf;

	nand_dbg_print(NAND_DBG_DEBUG, "\nWill dump cdma descriptors:\n");

	for (i = 0; i < info.cdma_num; i++) {
		nand_dbg_print(NAND_DBG_DEBUG, "\ni: %d\n", i);
		nand_dbg_print(NAND_DBG_DEBUG,
			"NxtPointerHi: 0x%x, NxtPointerLo: 0x%x\n",
			pc[i].NxtPointerHi, pc[i].NxtPointerLo);
		nand_dbg_print(NAND_DBG_DEBUG,
			"FlashPointerHi: 0x%x, FlashPointerLo: 0x%x\n",
			pc[i].FlashPointerHi, pc[i].FlashPointerLo);
		nand_dbg_print(NAND_DBG_DEBUG, "CommandType: 0x%x\n",
			pc[i].CommandType);
		nand_dbg_print(NAND_DBG_DEBUG,
			"MemAddrHi: 0x%x, MemAddrLo: 0x%x\n",
			pc[i].MemAddrHi, pc[i].MemAddrLo);
		nand_dbg_print(NAND_DBG_DEBUG, "CommandFlags: 0x%x\n",
			pc[i].CommandFlags);
		nand_dbg_print(NAND_DBG_DEBUG, "Channel: %d, Status: 0x%x\n",
			pc[i].Channel, pc[i].Status);
		nand_dbg_print(NAND_DBG_DEBUG,
			"MemCopyPointerHi: 0x%x, MemCopyPointerLo: 0x%x\n",
			pc[i].MemCopyPointerHi, pc[i].MemCopyPointerLo);
		nand_dbg_print(NAND_DBG_DEBUG,
			"Reserved12: 0x%x, Reserved13: 0x%x, "
			"Reserved14: 0x%x, pcmd: %d\n",
			pc[i].Reserved12, pc[i].Reserved13,
			pc[i].Reserved14, pc[i].pcmd);
	}
}

/* Print the Memory copy descriptors */
static void print_memcp_descriptors(void)
{
	struct memcpy_descriptor *pm;
	int i;

	pm = (struct memcpy_descriptor *)info.memcp_desc_buf;

	nand_dbg_print(NAND_DBG_DEBUG, "\nWill dump mem_cpy descriptors:\n");

	for (i = 0; i < info.cdma_num; i++) {
		nand_dbg_print(NAND_DBG_DEBUG, "\ni: %d\n", i);
		nand_dbg_print(NAND_DBG_DEBUG,
			"NxtPointerHi: 0x%x, NxtPointerLo: 0x%x\n",
			pm[i].NxtPointerHi, pm[i].NxtPointerLo);
		nand_dbg_print(NAND_DBG_DEBUG,
			"SrcAddrHi: 0x%x, SrcAddrLo: 0x%x\n",
			pm[i].SrcAddrHi, pm[i].SrcAddrLo);
		nand_dbg_print(NAND_DBG_DEBUG,
			"DestAddrHi: 0x%x, DestAddrLo: 0x%x\n",
			pm[i].DestAddrHi, pm[i].DestAddrLo);
		nand_dbg_print(NAND_DBG_DEBUG, "XferSize: %d\n",
			pm[i].XferSize);
		nand_dbg_print(NAND_DBG_DEBUG, "MemCopyFlags: 0x%x\n",
			pm[i].MemCopyFlags);
		nand_dbg_print(NAND_DBG_DEBUG, "MemCopyStatus: %d\n",
			pm[i].MemCopyStatus);
		nand_dbg_print(NAND_DBG_DEBUG, "reserved9: 0x%x\n",
			pm[i].reserved9);
		nand_dbg_print(NAND_DBG_DEBUG, "reserved10: 0x%x\n",
			pm[i].reserved10);
		nand_dbg_print(NAND_DBG_DEBUG, "reserved11: 0x%x\n",
			pm[i].reserved11);
		nand_dbg_print(NAND_DBG_DEBUG, "reserved12: 0x%x\n",
			pm[i].reserved12);
		nand_dbg_print(NAND_DBG_DEBUG, "reserved13: 0x%x\n",
			pm[i].reserved13);
		nand_dbg_print(NAND_DBG_DEBUG, "reserved14: 0x%x\n",
			pm[i].reserved14);
		nand_dbg_print(NAND_DBG_DEBUG, "reserved15: 0x%x\n",
			pm[i].reserved15);
	}
}
#endif

/* Reset cdma_descriptor chain to 0 */
static void reset_cdma_desc(int i)
{
	struct cdma_descriptor *ptr;

	BUG_ON(i >= MAX_DESCS);

	ptr = (struct cdma_descriptor *)info.cdma_desc_buf;

	ptr[i].NxtPointerHi = 0;
	ptr[i].NxtPointerLo = 0;
	ptr[i].FlashPointerHi = 0;
	ptr[i].FlashPointerLo = 0;
	ptr[i].CommandType = 0;
	ptr[i].MemAddrHi = 0;
	ptr[i].MemAddrLo = 0;
	ptr[i].CommandFlags = 0;
	ptr[i].Channel = 0;
	ptr[i].Status = 0;
	ptr[i].MemCopyPointerHi = 0;
	ptr[i].MemCopyPointerLo = 0;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     CDMA_UpdateEventStatus
* Inputs:       none
* Outputs:      none
* Description:  This function update the event status of all the channels
*               when an error condition is reported.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
void CDMA_UpdateEventStatus(void)
{
	int i, j, active_chan;
	struct cdma_descriptor *ptr;

	nand_dbg_print(NAND_DBG_DEBUG, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	ptr = (struct cdma_descriptor *)info.cdma_desc_buf;

	for (j = 0; j < info.cdma_num; j++) {
		/* Check for the descriptor with failure */
		if ((ptr[j].Status & CMD_DMA_DESC_FAIL))
			break;

	}

	/* All the previous cmd's status for this channel must be good */
	for (i = 0; i < j; i++) {
		if (ptr[i].pcmd != 0xff)
			info.pcmds[ptr[i].pcmd].Status = CMD_PASS;
	}

	/* Abort the channel with type 0 reset command. It resets the */
	/* selected channel after the descriptor completes the flash */
	/* operation and status has been updated for the descriptor. */
	/* Memory Copy and Sync associated with this descriptor will */
	/* not be executed */
	active_chan = ioread32(FlashReg + CHNL_ACTIVE);
	if ((active_chan & (1 << info.flash_bank)) == (1 << info.flash_bank)) {
		iowrite32(MODE_02 | (0 << 4), FlashMem); /* Type 0 reset */
		iowrite32((0xF << 4) | info.flash_bank, FlashMem + 0x10);
	} else { /* Should not reached here */
		printk(KERN_ERR "Error! Used bank is not set in"
			" reg CHNL_ACTIVE\n");
	}
}

static void cdma_trans(u16 chan)
{
	u32 addr;

	addr = info.cdma_desc;

	iowrite32(MODE_10 | (chan << 24), FlashMem);
	iowrite32((1 << 7) | chan, FlashMem + 0x10);

	iowrite32(MODE_10 | (chan << 24) | ((0x0FFFF & (addr >> 16)) << 8),
		FlashMem);
	iowrite32((1 << 7) | (1 << 4) | 0, FlashMem + 0x10);

	iowrite32(MODE_10 | (chan << 24) | ((0x0FFFF & addr) << 8), FlashMem);
	iowrite32((1 << 7) | (1 << 5) | 0, FlashMem + 0x10);

	iowrite32(MODE_10 | (chan << 24), FlashMem);
	iowrite32((1 << 7) | (1 << 5) | (1 << 4) | 0, FlashMem + 0x10);
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     CDMA_Execute_CMDs (for use with CMD_DMA)
* Inputs:       tag_count:  the number of pending cmds to do
* Outputs:      PASS/FAIL
* Description:  Build the SDMA chain(s) by making one CMD-DMA descriptor
*               for each pending command, start the CDMA engine, and return.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 CDMA_Execute_CMDs(void)
{
	int i, ret;
	u64 flash_add;
	u32 ptr;
	dma_addr_t map_addr, next_ptr;
	u16 status = PASS;
	u16 tmp_c;
	struct cdma_descriptor *pc;
	struct memcpy_descriptor *pm;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	/* No pending cmds to execute, just exit */
	if (0 == info.pcmds_num) {
		nand_dbg_print(NAND_DBG_TRACE,
			"No pending cmds to execute. Just exit.\n");
		return PASS;
	}

	for (i = 0; i < MAX_DESCS; i++)
		reset_cdma_desc(i);

	pc = (struct cdma_descriptor *)info.cdma_desc_buf;
	pm = (struct memcpy_descriptor *)info.memcp_desc_buf;

	info.cdma_desc = virt_to_bus(info.cdma_desc_buf);
	info.memcp_desc = virt_to_bus(info.memcp_desc_buf);
	next_ptr = info.cdma_desc;
	info.cdma_num = 0;

	for (i = 0; i < info.pcmds_num; i++) {
		if (info.pcmds[i].Block >= DeviceInfo.wTotalBlocks) {
			info.pcmds[i].Status = CMD_NOT_DONE;
			continue;
		}

		next_ptr += sizeof(struct cdma_descriptor);
		pc[info.cdma_num].NxtPointerHi = next_ptr >> 16;
		pc[info.cdma_num].NxtPointerLo = next_ptr & 0xffff;

		/* Use the Block offset within a bank */
		tmp_c = info.pcmds[i].Block /
			(DeviceInfo.wTotalBlocks / totalUsedBanks);
		flash_add = (u64)(info.pcmds[i].Block - tmp_c *
			(DeviceInfo.wTotalBlocks / totalUsedBanks)) *
			DeviceInfo.wBlockDataSize +
			(u64)(info.pcmds[i].Page) *
			DeviceInfo.wPageDataSize;

		ptr = MODE_10 | (info.flash_bank << 24) |
			(u32)GLOB_u64_Div(flash_add,
				DeviceInfo.wPageDataSize);
		pc[info.cdma_num].FlashPointerHi = ptr >> 16;
		pc[info.cdma_num].FlashPointerLo = ptr & 0xffff;

		if ((info.pcmds[i].CMD == WRITE_MAIN_SPARE_CMD) ||
			(info.pcmds[i].CMD == READ_MAIN_SPARE_CMD)) {
			/* Descriptor to set Main+Spare Access Mode */
			pc[info.cdma_num].CommandType = 0x43;
			pc[info.cdma_num].CommandFlags =
				(0 << 10) | (1 << 9) | (0 << 8) | 0x40;
			pc[info.cdma_num].MemAddrHi = 0;
			pc[info.cdma_num].MemAddrLo = 0;
			pc[info.cdma_num].Channel = 0;
			pc[info.cdma_num].Status = 0;
			pc[info.cdma_num].pcmd = i;

			info.cdma_num++;
			BUG_ON(info.cdma_num >= MAX_DESCS);

			reset_cdma_desc(info.cdma_num);
			next_ptr += sizeof(struct cdma_descriptor);
			pc[info.cdma_num].NxtPointerHi = next_ptr >> 16;
			pc[info.cdma_num].NxtPointerLo = next_ptr & 0xffff;
			pc[info.cdma_num].FlashPointerHi = ptr >> 16;
			pc[info.cdma_num].FlashPointerLo = ptr & 0xffff;
		}

		switch (info.pcmds[i].CMD) {
		case ERASE_CMD:
			pc[info.cdma_num].CommandType = 1;
			pc[info.cdma_num].CommandFlags =
				(0 << 10) | (1 << 9) | (0 << 8) | 0x40;
			pc[info.cdma_num].MemAddrHi = 0;
			pc[info.cdma_num].MemAddrLo = 0;
			break;

		case WRITE_MAIN_CMD:
			pc[info.cdma_num].CommandType =
				0x2100 | info.pcmds[i].PageCount;
			pc[info.cdma_num].CommandFlags =
				(0 << 10) | (1 << 9) | (0 << 8) | 0x40;
			map_addr = virt_to_bus(info.pcmds[i].DataAddr);
			pc[info.cdma_num].MemAddrHi = map_addr >> 16;
			pc[info.cdma_num].MemAddrLo = map_addr & 0xffff;
			break;

		case READ_MAIN_CMD:
			pc[info.cdma_num].CommandType =
				0x2000 | info.pcmds[i].PageCount;
			pc[info.cdma_num].CommandFlags =
				(0 << 10) | (1 << 9) | (0 << 8) | 0x40;
			map_addr = virt_to_bus(info.pcmds[i].DataAddr);
			pc[info.cdma_num].MemAddrHi = map_addr >> 16;
			pc[info.cdma_num].MemAddrLo = map_addr & 0xffff;
			break;

		case WRITE_MAIN_SPARE_CMD:
			pc[info.cdma_num].CommandType =
				0x2100 | info.pcmds[i].PageCount;
			pc[info.cdma_num].CommandFlags =
				(0 << 10) | (1 << 9) | (0 << 8) | 0x40;
			map_addr = virt_to_bus(info.pcmds[i].DataAddr);
			pc[info.cdma_num].MemAddrHi = map_addr >> 16;
			pc[info.cdma_num].MemAddrLo = map_addr & 0xffff;
			break;

		case READ_MAIN_SPARE_CMD:
			pc[info.cdma_num].CommandType =
				0x2000 | info.pcmds[i].PageCount;
			pc[info.cdma_num].CommandFlags =
				(0 << 10) | (1 << 9) | (0 << 8) | 0x40;
			map_addr = virt_to_bus(info.pcmds[i].DataAddr);
			pc[info.cdma_num].MemAddrHi = map_addr >> 16;
			pc[info.cdma_num].MemAddrLo = map_addr & 0xffff;
			break;

		case MEMCOPY_CMD:
			pc[info.cdma_num].CommandType = 0xFFFF; /* NOP cmd */
			/* Set bit 11 to let the CDMA engine continue to */
			/* execute only after it has finished processing   */
			/* the memcopy descriptor.                        */
			/* Also set bit 10 and bit 9 to 1                  */
			pc[info.cdma_num].CommandFlags = 0x0E40;
			map_addr = info.memcp_desc + info.cdma_num *
					sizeof(struct memcpy_descriptor);
			pc[info.cdma_num].MemCopyPointerHi = map_addr >> 16;
			pc[info.cdma_num].MemCopyPointerLo = map_addr & 0xffff;

			pm[info.cdma_num].NxtPointerHi = 0;
			pm[info.cdma_num].NxtPointerLo = 0;

			map_addr = virt_to_bus(info.pcmds[i].DataSrcAddr);
			pm[info.cdma_num].SrcAddrHi = map_addr >> 16;
			pm[info.cdma_num].SrcAddrLo = map_addr & 0xffff;
			map_addr = virt_to_bus(info.pcmds[i].DataDestAddr);
			pm[info.cdma_num].DestAddrHi = map_addr >> 16;
			pm[info.cdma_num].DestAddrLo = map_addr & 0xffff;

			pm[info.cdma_num].XferSize =
				info.pcmds[i].MemCopyByteCnt;
			pm[info.cdma_num].MemCopyFlags =
				(0 << 15 | 0 << 14 | 27 << 8 | 0x40);
			pm[info.cdma_num].MemCopyStatus = 0;
			break;

		case DUMMY_CMD:
		default:
			pc[info.cdma_num].CommandType = 0XFFFF;
			pc[info.cdma_num].CommandFlags =
				(0 << 10) | (1 << 9) | (0 << 8) | 0x40;
			pc[info.cdma_num].MemAddrHi = 0;
			pc[info.cdma_num].MemAddrLo = 0;
			break;
		}

		pc[info.cdma_num].Channel = 0;
		pc[info.cdma_num].Status = 0;
		pc[info.cdma_num].pcmd = i;

		info.cdma_num++;
		BUG_ON(info.cdma_num >= MAX_DESCS);

		if ((info.pcmds[i].CMD == WRITE_MAIN_SPARE_CMD) ||
			(info.pcmds[i].CMD == READ_MAIN_SPARE_CMD)) {
			/* Descriptor to set back Main Area Access Mode */
			reset_cdma_desc(info.cdma_num);
			next_ptr += sizeof(struct cdma_descriptor);
			pc[info.cdma_num].NxtPointerHi = next_ptr >> 16;
			pc[info.cdma_num].NxtPointerLo = next_ptr & 0xffff;

			pc[info.cdma_num].FlashPointerHi = ptr >> 16;
			pc[info.cdma_num].FlashPointerLo = ptr & 0xffff;

			pc[info.cdma_num].CommandType = 0x42;
			pc[info.cdma_num].CommandFlags =
				(0 << 10) | (1 << 9) | (0 << 8) | 0x40;
			pc[info.cdma_num].MemAddrHi = 0;
			pc[info.cdma_num].MemAddrLo = 0;

			pc[info.cdma_num].Channel = 0;
			pc[info.cdma_num].Status = 0;
			pc[info.cdma_num].pcmd = i;

			info.cdma_num++;
			BUG_ON(info.cdma_num >= MAX_DESCS);
		}
	}

	/* Add a dummy descriptor at end of the CDMA chain */
	reset_cdma_desc(info.cdma_num);
	ptr = MODE_10 | (info.flash_bank << 24);
	pc[info.cdma_num].FlashPointerHi = ptr >> 16;
	pc[info.cdma_num].FlashPointerLo = ptr & 0xffff;
	pc[info.cdma_num].CommandType = 0xFFFF; /* NOP command */
	/* Set Command Flags for the last CDMA descriptor: */
	/* set Continue bit (bit 9) to 0 and Interrupt bit (bit 8) to 1 */
	pc[info.cdma_num].CommandFlags =
		(0 << 10) | (0 << 9) | (1 << 8) | 0x40;
	pc[info.cdma_num].pcmd = 0xff; /* Set it to an illegal value */
	info.cdma_num++;
	BUG_ON(info.cdma_num >= MAX_DESCS);

	iowrite32(1, FlashReg + GLOBAL_INT_ENABLE);  /* Enable Interrupt */

	iowrite32(1, FlashReg + DMA_ENABLE);
	/* Wait for DMA to be enabled before issuing the next command */
	while (!(ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
		;
	cdma_trans(info.flash_bank);

	ret = wait_for_completion_timeout(&info.complete, 50 * HZ);
	if (!ret)
		printk(KERN_ERR "Wait for completion timeout "
			"in %s, Line %d\n", __FILE__, __LINE__);
	status = info.ret;

	info.pcmds_num = 0; /* Clear the pending cmds number to 0 */

	return status;
}

int is_cdma_interrupt(void)
{
	u32 ints_b0, ints_b1, ints_b2, ints_b3, ints_cdma;
	u32 int_en_mask;
	u32 cdma_int_en_mask;

	nand_dbg_print(NAND_DBG_DEBUG, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	/* Set the global Enable masks for only those interrupts
	 * that are supported */
	cdma_int_en_mask = (DMA_INTR__DESC_COMP_CHANNEL0 |
			DMA_INTR__DESC_COMP_CHANNEL1 |
			DMA_INTR__DESC_COMP_CHANNEL2 |
			DMA_INTR__DESC_COMP_CHANNEL3 |
			DMA_INTR__MEMCOPY_DESC_COMP);

	int_en_mask = (INTR_STATUS0__ECC_ERR |
		INTR_STATUS0__PROGRAM_FAIL |
		INTR_STATUS0__ERASE_FAIL);

	ints_b0 = ioread32(FlashReg + INTR_STATUS0) & int_en_mask;
	ints_b1 = ioread32(FlashReg + INTR_STATUS1) & int_en_mask;
	ints_b2 = ioread32(FlashReg + INTR_STATUS2) & int_en_mask;
	ints_b3 = ioread32(FlashReg + INTR_STATUS3) & int_en_mask;
	ints_cdma = ioread32(FlashReg + DMA_INTR) & cdma_int_en_mask;

	nand_dbg_print(NAND_DBG_WARN, "ints_bank0 to ints_bank3: "
			"0x%x, 0x%x, 0x%x, 0x%x, ints_cdma: 0x%x\n",
			ints_b0, ints_b1, ints_b2, ints_b3, ints_cdma);

	if (ints_b0 || ints_b1 || ints_b2 || ints_b3 || ints_cdma) {
		return 1;
	} else {
		iowrite32(ints_b0, FlashReg + INTR_STATUS0);
		iowrite32(ints_b1, FlashReg + INTR_STATUS1);
		iowrite32(ints_b2, FlashReg + INTR_STATUS2);
		iowrite32(ints_b3, FlashReg + INTR_STATUS3);
		nand_dbg_print(NAND_DBG_DEBUG,
			"Not a NAND controller interrupt! Ignore it.\n");
		return 0;
	}
}

static void update_event_status(void)
{
	int i;
	struct cdma_descriptor *ptr;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	ptr = (struct cdma_descriptor *)info.cdma_desc_buf;

	for (i = 0; i < info.cdma_num; i++) {
		if (ptr[i].pcmd != 0xff)
			info.pcmds[ptr[i].pcmd].Status = CMD_PASS;
		if ((ptr[i].CommandType == 0x41) ||
			(ptr[i].CommandType == 0x42) ||
			(ptr[i].CommandType == 0x43))
			continue;

		switch (info.pcmds[ptr[i].pcmd].CMD) {
		case READ_MAIN_SPARE_CMD:
			Conv_Main_Spare_Data_Phy2Log_Format(
				info.pcmds[ptr[i].pcmd].DataAddr,
				info.pcmds[ptr[i].pcmd].PageCount);
			break;
		case READ_SPARE_CMD:
			Conv_Spare_Data_Phy2Log_Format(
				info.pcmds[ptr[i].pcmd].DataAddr);
			break;
		}
	}
}

static u16 do_ecc_for_desc(u32 ch, u8 *buf, u16 page)
{
	u16 event = EVENT_NONE;
	u16 err_byte;
	u16 err_page = 0;
	u8 err_sector;
	u8 err_device;
	u16 ecc_correction_info;
	u16 err_address;
	u32 eccSectorSize;
	u8 *err_pos;

	nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	eccSectorSize = ECC_SECTOR_SIZE * (DeviceInfo.wDevicesConnected);

	do {
		if (0 == ch)
			err_page = ioread32(FlashReg + ERR_PAGE_ADDR0);
		else if (1 == ch)
			err_page = ioread32(FlashReg + ERR_PAGE_ADDR1);
		else if (2 == ch)
			err_page = ioread32(FlashReg + ERR_PAGE_ADDR2);
		else if (3 == ch)
			err_page = ioread32(FlashReg + ERR_PAGE_ADDR3);

		err_address = ioread32(FlashReg + ECC_ERROR_ADDRESS);
		err_byte = err_address & ECC_ERROR_ADDRESS__OFFSET;
		err_sector = ((err_address &
			ECC_ERROR_ADDRESS__SECTOR_NR) >> 12);

		ecc_correction_info = ioread32(FlashReg + ERR_CORRECTION_INFO);
		err_device = ((ecc_correction_info &
			ERR_CORRECTION_INFO__DEVICE_NR) >> 8);

		if (ecc_correction_info & ERR_CORRECTION_INFO__ERROR_TYPE) {
			event = EVENT_UNCORRECTABLE_DATA_ERROR;
		} else {
			event = EVENT_CORRECTABLE_DATA_ERROR_FIXED;
			if (err_byte < ECC_SECTOR_SIZE) {
				err_pos = buf +
					(err_page - page) *
					DeviceInfo.wPageDataSize +
					err_sector * eccSectorSize +
					err_byte *
					DeviceInfo.wDevicesConnected +
					err_device;
				*err_pos ^= ecc_correction_info &
					ERR_CORRECTION_INFO__BYTEMASK;
			}
		}
	} while (!(ecc_correction_info & ERR_CORRECTION_INFO__LAST_ERR_INFO));

	return event;
}

static u16 process_ecc_int(u32 c, u16 *p_desc_num)
{
	struct cdma_descriptor *ptr;
	u16 j;
	int event = EVENT_PASS;

	nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	if (c != info.flash_bank)
		printk(KERN_ERR "Error!info.flash_bank is %d, while c is %d\n",
				info.flash_bank, c);

	ptr = (struct cdma_descriptor *)info.cdma_desc_buf;

	for (j = 0; j < info.cdma_num; j++)
		if ((ptr[j].Status & CMD_DMA_DESC_COMP) != CMD_DMA_DESC_COMP)
			break;

	*p_desc_num = j; /* Pass the descripter number found here */

	if (j >= info.cdma_num) {
		printk(KERN_ERR "Can not find the correct descriptor number "
			"when ecc interrupt triggered!"
			"info.cdma_num: %d, j: %d\n", info.cdma_num, j);
		return EVENT_UNCORRECTABLE_DATA_ERROR;
	}

	event = do_ecc_for_desc(c, info.pcmds[ptr[j].pcmd].DataAddr,
		info.pcmds[ptr[j].pcmd].Page);

	if (EVENT_UNCORRECTABLE_DATA_ERROR == event) {
		printk(KERN_ERR "Uncorrectable ECC error!"
			"info.cdma_num: %d, j: %d, "
			"pending cmd CMD: 0x%x, "
			"Block: 0x%x, Page: 0x%x, PageCount: 0x%x\n",
			info.cdma_num, j,
			info.pcmds[ptr[j].pcmd].CMD,
			info.pcmds[ptr[j].pcmd].Block,
			info.pcmds[ptr[j].pcmd].Page,
			info.pcmds[ptr[j].pcmd].PageCount);

		if (ptr[j].pcmd != 0xff)
			info.pcmds[ptr[j].pcmd].Status = CMD_FAIL;
		CDMA_UpdateEventStatus();
	}

	return event;
}

static void process_prog_erase_fail_int(u16 desc_num)
{
	struct cdma_descriptor *ptr;

	nand_dbg_print(NAND_DBG_DEBUG, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	ptr = (struct cdma_descriptor *)info.cdma_desc_buf;

	if (ptr[desc_num].pcmd != 0xFF)
		info.pcmds[ptr[desc_num].pcmd].Status = CMD_FAIL;

	CDMA_UpdateEventStatus();
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     CDMA_Event_Status (for use with CMD_DMA)
* Inputs:       none
* Outputs:      Event_Status code
* Description:  This function is called after an interrupt has happened
*               It reads the HW status register and ...tbd
*               It returns the appropriate event status
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16  CDMA_Event_Status(void)
{
	u32 ints_addr[4] = {INTR_STATUS0, INTR_STATUS1,
		INTR_STATUS2, INTR_STATUS3};
	u32 dma_intr_bit[4] = {DMA_INTR__DESC_COMP_CHANNEL0,
		DMA_INTR__DESC_COMP_CHANNEL1,
		DMA_INTR__DESC_COMP_CHANNEL2,
		DMA_INTR__DESC_COMP_CHANNEL3};
	u32 cdma_int_status, int_status;
	u32 ecc_enable = 0;
	u16 event = EVENT_PASS;
	u16 cur_desc = 0;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	ecc_enable = ioread32(FlashReg + ECC_ENABLE);

	while (1) {
		int_status = ioread32(FlashReg + ints_addr[info.flash_bank]);
		if (ecc_enable && (int_status & INTR_STATUS0__ECC_ERR)) {
			event = process_ecc_int(info.flash_bank, &cur_desc);
			iowrite32(INTR_STATUS0__ECC_ERR,
				FlashReg + ints_addr[info.flash_bank]);
			if (EVENT_UNCORRECTABLE_DATA_ERROR == event) {
				nand_dbg_print(NAND_DBG_WARN,
					"ints_bank0 to ints_bank3: "
					"0x%x, 0x%x, 0x%x, 0x%x, "
					"ints_cdma: 0x%x\n",
					ioread32(FlashReg + INTR_STATUS0),
					ioread32(FlashReg + INTR_STATUS1),
					ioread32(FlashReg + INTR_STATUS2),
					ioread32(FlashReg + INTR_STATUS3),
					ioread32(FlashReg + DMA_INTR));
				break;
			}
		} else if (int_status & INTR_STATUS0__PROGRAM_FAIL) {
			printk(KERN_ERR "NAND program fail interrupt!\n");
			process_prog_erase_fail_int(cur_desc);
			event = EVENT_PROGRAM_FAILURE;
			break;
		} else if (int_status & INTR_STATUS0__ERASE_FAIL) {
			printk(KERN_ERR "NAND erase fail interrupt!\n");
			process_prog_erase_fail_int(cur_desc);
			event = EVENT_ERASE_FAILURE;
			break;
		} else {
			cdma_int_status = ioread32(FlashReg + DMA_INTR);
			if (cdma_int_status & dma_intr_bit[info.flash_bank]) {
				iowrite32(dma_intr_bit[info.flash_bank],
					FlashReg + DMA_INTR);
				update_event_status();
				event = EVENT_PASS;
				break;
			}
		}
	}

	int_status = ioread32(FlashReg + ints_addr[info.flash_bank]);
	iowrite32(int_status, FlashReg + ints_addr[info.flash_bank]);
	cdma_int_status = ioread32(FlashReg + DMA_INTR);
	iowrite32(cdma_int_status, FlashReg + DMA_INTR);

	iowrite32(0, FlashReg + DMA_ENABLE);
	while ((ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
		;

	return event;
}



