// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2022 Beijing WangXun Technology Co., Ltd. */

#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/iopoll.h>
#include <linux/pci.h>

#include "wx_type.h"
#include "wx_hw.h"

static void wx_intr_disable(struct wx_hw *wxhw, u64 qmask)
{
	u32 mask;

	mask = (qmask & 0xFFFFFFFF);
	if (mask)
		wr32(wxhw, WX_PX_IMS(0), mask);

	if (wxhw->mac.type == wx_mac_sp) {
		mask = (qmask >> 32);
		if (mask)
			wr32(wxhw, WX_PX_IMS(1), mask);
	}
}

/* cmd_addr is used for some special command:
 * 1. to be sector address, when implemented erase sector command
 * 2. to be flash address when implemented read, write flash address
 */
static int wx_fmgr_cmd_op(struct wx_hw *wxhw, u32 cmd, u32 cmd_addr)
{
	u32 cmd_val = 0, val = 0;

	cmd_val = WX_SPI_CMD_CMD(cmd) |
		  WX_SPI_CMD_CLK(WX_SPI_CLK_DIV) |
		  cmd_addr;
	wr32(wxhw, WX_SPI_CMD, cmd_val);

	return read_poll_timeout(rd32, val, (val & 0x1), 10, 100000,
				 false, wxhw, WX_SPI_STATUS);
}

static int wx_flash_read_dword(struct wx_hw *wxhw, u32 addr, u32 *data)
{
	int ret = 0;

	ret = wx_fmgr_cmd_op(wxhw, WX_SPI_CMD_READ_DWORD, addr);
	if (ret < 0)
		return ret;

	*data = rd32(wxhw, WX_SPI_DATA);

	return ret;
}

int wx_check_flash_load(struct wx_hw *hw, u32 check_bit)
{
	u32 reg = 0;
	int err = 0;

	/* if there's flash existing */
	if (!(rd32(hw, WX_SPI_STATUS) &
	      WX_SPI_STATUS_FLASH_BYPASS)) {
		/* wait hw load flash done */
		err = read_poll_timeout(rd32, reg, !(reg & check_bit), 20000, 2000000,
					false, hw, WX_SPI_ILDR_STATUS);
		if (err < 0)
			wx_err(hw, "Check flash load timeout.\n");
	}

	return err;
}
EXPORT_SYMBOL(wx_check_flash_load);

void wx_control_hw(struct wx_hw *wxhw, bool drv)
{
	if (drv) {
		/* Let firmware know the driver has taken over */
		wr32m(wxhw, WX_CFG_PORT_CTL,
		      WX_CFG_PORT_CTL_DRV_LOAD, WX_CFG_PORT_CTL_DRV_LOAD);
	} else {
		/* Let firmware take over control of hw */
		wr32m(wxhw, WX_CFG_PORT_CTL,
		      WX_CFG_PORT_CTL_DRV_LOAD, 0);
	}
}
EXPORT_SYMBOL(wx_control_hw);

/**
 * wx_mng_present - returns 0 when management capability is present
 * @wxhw: pointer to hardware structure
 */
int wx_mng_present(struct wx_hw *wxhw)
{
	u32 fwsm;

	fwsm = rd32(wxhw, WX_MIS_ST);
	if (fwsm & WX_MIS_ST_MNG_INIT_DN)
		return 0;
	else
		return -EACCES;
}
EXPORT_SYMBOL(wx_mng_present);

/* Software lock to be held while software semaphore is being accessed. */
static DEFINE_MUTEX(wx_sw_sync_lock);

/**
 *  wx_release_sw_sync - Release SW semaphore
 *  @wxhw: pointer to hardware structure
 *  @mask: Mask to specify which semaphore to release
 *
 *  Releases the SW semaphore for the specified
 *  function (CSR, PHY0, PHY1, EEPROM, Flash)
 **/
static void wx_release_sw_sync(struct wx_hw *wxhw, u32 mask)
{
	mutex_lock(&wx_sw_sync_lock);
	wr32m(wxhw, WX_MNG_SWFW_SYNC, mask, 0);
	mutex_unlock(&wx_sw_sync_lock);
}

/**
 *  wx_acquire_sw_sync - Acquire SW semaphore
 *  @wxhw: pointer to hardware structure
 *  @mask: Mask to specify which semaphore to acquire
 *
 *  Acquires the SW semaphore for the specified
 *  function (CSR, PHY0, PHY1, EEPROM, Flash)
 **/
static int wx_acquire_sw_sync(struct wx_hw *wxhw, u32 mask)
{
	u32 sem = 0;
	int ret = 0;

	mutex_lock(&wx_sw_sync_lock);
	ret = read_poll_timeout(rd32, sem, !(sem & mask),
				5000, 2000000, false, wxhw, WX_MNG_SWFW_SYNC);
	if (!ret) {
		sem |= mask;
		wr32(wxhw, WX_MNG_SWFW_SYNC, sem);
	} else {
		wx_err(wxhw, "SW Semaphore not granted: 0x%x.\n", sem);
	}
	mutex_unlock(&wx_sw_sync_lock);

	return ret;
}

/**
 *  wx_host_interface_command - Issue command to manageability block
 *  @wxhw: pointer to the HW structure
 *  @buffer: contains the command to write and where the return status will
 *   be placed
 *  @length: length of buffer, must be multiple of 4 bytes
 *  @timeout: time in ms to wait for command completion
 *  @return_data: read and return data from the buffer (true) or not (false)
 *   Needed because FW structures are big endian and decoding of
 *   these fields can be 8 bit or 16 bit based on command. Decoding
 *   is not easily understood without making a table of commands.
 *   So we will leave this up to the caller to read back the data
 *   in these cases.
 **/
int wx_host_interface_command(struct wx_hw *wxhw, u32 *buffer,
			      u32 length, u32 timeout, bool return_data)
{
	u32 hdr_size = sizeof(struct wx_hic_hdr);
	u32 hicr, i, bi, buf[64] = {};
	int status = 0;
	u32 dword_len;
	u16 buf_len;

	if (length == 0 || length > WX_HI_MAX_BLOCK_BYTE_LENGTH) {
		wx_err(wxhw, "Buffer length failure buffersize=%d.\n", length);
		return -EINVAL;
	}

	status = wx_acquire_sw_sync(wxhw, WX_MNG_SWFW_SYNC_SW_MB);
	if (status != 0)
		return status;

	/* Calculate length in DWORDs. We must be DWORD aligned */
	if ((length % (sizeof(u32))) != 0) {
		wx_err(wxhw, "Buffer length failure, not aligned to dword");
		status = -EINVAL;
		goto rel_out;
	}

	dword_len = length >> 2;

	/* The device driver writes the relevant command block
	 * into the ram area.
	 */
	for (i = 0; i < dword_len; i++) {
		wr32a(wxhw, WX_MNG_MBOX, i, (__force u32)cpu_to_le32(buffer[i]));
		/* write flush */
		buf[i] = rd32a(wxhw, WX_MNG_MBOX, i);
	}
	/* Setting this bit tells the ARC that a new command is pending. */
	wr32m(wxhw, WX_MNG_MBOX_CTL,
	      WX_MNG_MBOX_CTL_SWRDY, WX_MNG_MBOX_CTL_SWRDY);

	status = read_poll_timeout(rd32, hicr, hicr & WX_MNG_MBOX_CTL_FWRDY, 1000,
				   timeout * 1000, false, wxhw, WX_MNG_MBOX_CTL);

	/* Check command completion */
	if (status) {
		wx_dbg(wxhw, "Command has failed with no status valid.\n");

		buf[0] = rd32(wxhw, WX_MNG_MBOX);
		if ((buffer[0] & 0xff) != (~buf[0] >> 24)) {
			status = -EINVAL;
			goto rel_out;
		}
		if ((buf[0] & 0xff0000) >> 16 == 0x80) {
			wx_dbg(wxhw, "It's unknown cmd.\n");
			status = -EINVAL;
			goto rel_out;
		}

		wx_dbg(wxhw, "write value:\n");
		for (i = 0; i < dword_len; i++)
			wx_dbg(wxhw, "%x ", buffer[i]);
		wx_dbg(wxhw, "read value:\n");
		for (i = 0; i < dword_len; i++)
			wx_dbg(wxhw, "%x ", buf[i]);
	}

	if (!return_data)
		goto rel_out;

	/* Calculate length in DWORDs */
	dword_len = hdr_size >> 2;

	/* first pull in the header so we know the buffer length */
	for (bi = 0; bi < dword_len; bi++) {
		buffer[bi] = rd32a(wxhw, WX_MNG_MBOX, bi);
		le32_to_cpus(&buffer[bi]);
	}

	/* If there is any thing in data position pull it in */
	buf_len = ((struct wx_hic_hdr *)buffer)->buf_len;
	if (buf_len == 0)
		goto rel_out;

	if (length < buf_len + hdr_size) {
		wx_err(wxhw, "Buffer not large enough for reply message.\n");
		status = -EFAULT;
		goto rel_out;
	}

	/* Calculate length in DWORDs, add 3 for odd lengths */
	dword_len = (buf_len + 3) >> 2;

	/* Pull in the rest of the buffer (bi is where we left off) */
	for (; bi <= dword_len; bi++) {
		buffer[bi] = rd32a(wxhw, WX_MNG_MBOX, bi);
		le32_to_cpus(&buffer[bi]);
	}

rel_out:
	wx_release_sw_sync(wxhw, WX_MNG_SWFW_SYNC_SW_MB);
	return status;
}
EXPORT_SYMBOL(wx_host_interface_command);

/**
 *  wx_read_ee_hostif_data - Read EEPROM word using a host interface cmd
 *  assuming that the semaphore is already obtained.
 *  @wxhw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to read
 *  @data: word read from the EEPROM
 *
 *  Reads a 16 bit word from the EEPROM using the hostif.
 **/
static int wx_read_ee_hostif_data(struct wx_hw *wxhw, u16 offset, u16 *data)
{
	struct wx_hic_read_shadow_ram buffer;
	int status;

	buffer.hdr.req.cmd = FW_READ_SHADOW_RAM_CMD;
	buffer.hdr.req.buf_lenh = 0;
	buffer.hdr.req.buf_lenl = FW_READ_SHADOW_RAM_LEN;
	buffer.hdr.req.checksum = FW_DEFAULT_CHECKSUM;

	/* convert offset from words to bytes */
	buffer.address = (__force u32)cpu_to_be32(offset * 2);
	/* one word */
	buffer.length = (__force u16)cpu_to_be16(sizeof(u16));

	status = wx_host_interface_command(wxhw, (u32 *)&buffer, sizeof(buffer),
					   WX_HI_COMMAND_TIMEOUT, false);

	if (status != 0)
		return status;

	*data = (u16)rd32a(wxhw, WX_MNG_MBOX, FW_NVM_DATA_OFFSET);

	return status;
}

/**
 *  wx_read_ee_hostif - Read EEPROM word using a host interface cmd
 *  @wxhw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to read
 *  @data: word read from the EEPROM
 *
 *  Reads a 16 bit word from the EEPROM using the hostif.
 **/
int wx_read_ee_hostif(struct wx_hw *wxhw, u16 offset, u16 *data)
{
	int status = 0;

	status = wx_acquire_sw_sync(wxhw, WX_MNG_SWFW_SYNC_SW_FLASH);
	if (status == 0) {
		status = wx_read_ee_hostif_data(wxhw, offset, data);
		wx_release_sw_sync(wxhw, WX_MNG_SWFW_SYNC_SW_FLASH);
	}

	return status;
}
EXPORT_SYMBOL(wx_read_ee_hostif);

/**
 *  wx_read_ee_hostif_buffer- Read EEPROM word(s) using hostif
 *  @wxhw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to read
 *  @words: number of words
 *  @data: word(s) read from the EEPROM
 *
 *  Reads a 16 bit word(s) from the EEPROM using the hostif.
 **/
int wx_read_ee_hostif_buffer(struct wx_hw *wxhw,
			     u16 offset, u16 words, u16 *data)
{
	struct wx_hic_read_shadow_ram buffer;
	u32 current_word = 0;
	u16 words_to_read;
	u32 value = 0;
	int status;
	u32 i;

	/* Take semaphore for the entire operation. */
	status = wx_acquire_sw_sync(wxhw, WX_MNG_SWFW_SYNC_SW_FLASH);
	if (status != 0)
		return status;

	while (words) {
		if (words > FW_MAX_READ_BUFFER_SIZE / 2)
			words_to_read = FW_MAX_READ_BUFFER_SIZE / 2;
		else
			words_to_read = words;

		buffer.hdr.req.cmd = FW_READ_SHADOW_RAM_CMD;
		buffer.hdr.req.buf_lenh = 0;
		buffer.hdr.req.buf_lenl = FW_READ_SHADOW_RAM_LEN;
		buffer.hdr.req.checksum = FW_DEFAULT_CHECKSUM;

		/* convert offset from words to bytes */
		buffer.address = (__force u32)cpu_to_be32((offset + current_word) * 2);
		buffer.length = (__force u16)cpu_to_be16(words_to_read * 2);

		status = wx_host_interface_command(wxhw, (u32 *)&buffer,
						   sizeof(buffer),
						   WX_HI_COMMAND_TIMEOUT,
						   false);

		if (status != 0) {
			wx_err(wxhw, "Host interface command failed\n");
			goto out;
		}

		for (i = 0; i < words_to_read; i++) {
			u32 reg = WX_MNG_MBOX + (FW_NVM_DATA_OFFSET << 2) + 2 * i;

			value = rd32(wxhw, reg);
			data[current_word] = (u16)(value & 0xffff);
			current_word++;
			i++;
			if (i < words_to_read) {
				value >>= 16;
				data[current_word] = (u16)(value & 0xffff);
				current_word++;
			}
		}
		words -= words_to_read;
	}

out:
	wx_release_sw_sync(wxhw, WX_MNG_SWFW_SYNC_SW_FLASH);
	return status;
}
EXPORT_SYMBOL(wx_read_ee_hostif_buffer);

/**
 *  wx_calculate_checksum - Calculate checksum for buffer
 *  @buffer: pointer to EEPROM
 *  @length: size of EEPROM to calculate a checksum for
 *  Calculates the checksum for some buffer on a specified length.  The
 *  checksum calculated is returned.
 **/
static u8 wx_calculate_checksum(u8 *buffer, u32 length)
{
	u8 sum = 0;
	u32 i;

	if (!buffer)
		return 0;

	for (i = 0; i < length; i++)
		sum += buffer[i];

	return (u8)(0 - sum);
}

/**
 *  wx_reset_hostif - send reset cmd to fw
 *  @wxhw: pointer to hardware structure
 *
 *  Sends reset cmd to firmware through the manageability
 *  block.
 **/
int wx_reset_hostif(struct wx_hw *wxhw)
{
	struct wx_hic_reset reset_cmd;
	int ret_val = 0;
	int i;

	reset_cmd.hdr.cmd = FW_RESET_CMD;
	reset_cmd.hdr.buf_len = FW_RESET_LEN;
	reset_cmd.hdr.cmd_or_resp.cmd_resv = FW_CEM_CMD_RESERVED;
	reset_cmd.lan_id = wxhw->bus.func;
	reset_cmd.reset_type = (u16)wxhw->reset_type;
	reset_cmd.hdr.checksum = 0;
	reset_cmd.hdr.checksum = wx_calculate_checksum((u8 *)&reset_cmd,
						       (FW_CEM_HDR_LEN +
							reset_cmd.hdr.buf_len));

	for (i = 0; i <= FW_CEM_MAX_RETRIES; i++) {
		ret_val = wx_host_interface_command(wxhw, (u32 *)&reset_cmd,
						    sizeof(reset_cmd),
						    WX_HI_COMMAND_TIMEOUT,
						    true);
		if (ret_val != 0)
			continue;

		if (reset_cmd.hdr.cmd_or_resp.ret_status ==
		    FW_CEM_RESP_STATUS_SUCCESS)
			ret_val = 0;
		else
			ret_val = -EFAULT;

		break;
	}

	return ret_val;
}
EXPORT_SYMBOL(wx_reset_hostif);

/**
 *  wx_init_eeprom_params - Initialize EEPROM params
 *  @wxhw: pointer to hardware structure
 *
 *  Initializes the EEPROM parameters wx_eeprom_info within the
 *  wx_hw struct in order to set up EEPROM access.
 **/
void wx_init_eeprom_params(struct wx_hw *wxhw)
{
	struct wx_eeprom_info *eeprom = &wxhw->eeprom;
	u16 eeprom_size;
	u16 data = 0x80;

	if (eeprom->type == wx_eeprom_uninitialized) {
		eeprom->semaphore_delay = 10;
		eeprom->type = wx_eeprom_none;

		if (!(rd32(wxhw, WX_SPI_STATUS) &
		      WX_SPI_STATUS_FLASH_BYPASS)) {
			eeprom->type = wx_flash;

			eeprom_size = 4096;
			eeprom->word_size = eeprom_size >> 1;

			wx_dbg(wxhw, "Eeprom params: type = %d, size = %d\n",
			       eeprom->type, eeprom->word_size);
		}
	}

	if (wxhw->mac.type == wx_mac_sp) {
		if (wx_read_ee_hostif(wxhw, WX_SW_REGION_PTR, &data)) {
			wx_err(wxhw, "NVM Read Error\n");
			return;
		}
		data = data >> 1;
	}

	eeprom->sw_region_offset = data;
}
EXPORT_SYMBOL(wx_init_eeprom_params);

/**
 *  wx_get_mac_addr - Generic get MAC address
 *  @wxhw: pointer to hardware structure
 *  @mac_addr: Adapter MAC address
 *
 *  Reads the adapter's MAC address from first Receive Address Register (RAR0)
 *  A reset of the adapter must be performed prior to calling this function
 *  in order for the MAC address to have been loaded from the EEPROM into RAR0
 **/
void wx_get_mac_addr(struct wx_hw *wxhw, u8 *mac_addr)
{
	u32 rar_high;
	u32 rar_low;
	u16 i;

	wr32(wxhw, WX_PSR_MAC_SWC_IDX, 0);
	rar_high = rd32(wxhw, WX_PSR_MAC_SWC_AD_H);
	rar_low = rd32(wxhw, WX_PSR_MAC_SWC_AD_L);

	for (i = 0; i < 2; i++)
		mac_addr[i] = (u8)(rar_high >> (1 - i) * 8);

	for (i = 0; i < 4; i++)
		mac_addr[i + 2] = (u8)(rar_low >> (3 - i) * 8);
}
EXPORT_SYMBOL(wx_get_mac_addr);

/**
 *  wx_set_rar - Set Rx address register
 *  @wxhw: pointer to hardware structure
 *  @index: Receive address register to write
 *  @addr: Address to put into receive address register
 *  @pools: VMDq "set" or "pool" index
 *  @enable_addr: set flag that address is active
 *
 *  Puts an ethernet address into a receive address register.
 **/
int wx_set_rar(struct wx_hw *wxhw, u32 index, u8 *addr, u64 pools,
	       u32 enable_addr)
{
	u32 rar_entries = wxhw->mac.num_rar_entries;
	u32 rar_low, rar_high;

	/* Make sure we are using a valid rar index range */
	if (index >= rar_entries) {
		wx_err(wxhw, "RAR index %d is out of range.\n", index);
		return -EINVAL;
	}

	/* select the MAC address */
	wr32(wxhw, WX_PSR_MAC_SWC_IDX, index);

	/* setup VMDq pool mapping */
	wr32(wxhw, WX_PSR_MAC_SWC_VM_L, pools & 0xFFFFFFFF);
	if (wxhw->mac.type == wx_mac_sp)
		wr32(wxhw, WX_PSR_MAC_SWC_VM_H, pools >> 32);

	/* HW expects these in little endian so we reverse the byte
	 * order from network order (big endian) to little endian
	 *
	 * Some parts put the VMDq setting in the extra RAH bits,
	 * so save everything except the lower 16 bits that hold part
	 * of the address and the address valid bit.
	 */
	rar_low = ((u32)addr[5] |
		  ((u32)addr[4] << 8) |
		  ((u32)addr[3] << 16) |
		  ((u32)addr[2] << 24));
	rar_high = ((u32)addr[1] |
		   ((u32)addr[0] << 8));
	if (enable_addr != 0)
		rar_high |= WX_PSR_MAC_SWC_AD_H_AV;

	wr32(wxhw, WX_PSR_MAC_SWC_AD_L, rar_low);
	wr32m(wxhw, WX_PSR_MAC_SWC_AD_H,
	      (WX_PSR_MAC_SWC_AD_H_AD(~0) |
	       WX_PSR_MAC_SWC_AD_H_ADTYPE(~0) |
	       WX_PSR_MAC_SWC_AD_H_AV),
	      rar_high);

	return 0;
}
EXPORT_SYMBOL(wx_set_rar);

/**
 *  wx_clear_rar - Remove Rx address register
 *  @wxhw: pointer to hardware structure
 *  @index: Receive address register to write
 *
 *  Clears an ethernet address from a receive address register.
 **/
int wx_clear_rar(struct wx_hw *wxhw, u32 index)
{
	u32 rar_entries = wxhw->mac.num_rar_entries;

	/* Make sure we are using a valid rar index range */
	if (index >= rar_entries) {
		wx_err(wxhw, "RAR index %d is out of range.\n", index);
		return -EINVAL;
	}

	/* Some parts put the VMDq setting in the extra RAH bits,
	 * so save everything except the lower 16 bits that hold part
	 * of the address and the address valid bit.
	 */
	wr32(wxhw, WX_PSR_MAC_SWC_IDX, index);

	wr32(wxhw, WX_PSR_MAC_SWC_VM_L, 0);
	wr32(wxhw, WX_PSR_MAC_SWC_VM_H, 0);

	wr32(wxhw, WX_PSR_MAC_SWC_AD_L, 0);
	wr32m(wxhw, WX_PSR_MAC_SWC_AD_H,
	      (WX_PSR_MAC_SWC_AD_H_AD(~0) |
	       WX_PSR_MAC_SWC_AD_H_ADTYPE(~0) |
	       WX_PSR_MAC_SWC_AD_H_AV),
	      0);

	return 0;
}
EXPORT_SYMBOL(wx_clear_rar);

/**
 *  wx_clear_vmdq - Disassociate a VMDq pool index from a rx address
 *  @wxhw: pointer to hardware struct
 *  @rar: receive address register index to disassociate
 *  @vmdq: VMDq pool index to remove from the rar
 **/
static int wx_clear_vmdq(struct wx_hw *wxhw, u32 rar, u32 __maybe_unused vmdq)
{
	u32 rar_entries = wxhw->mac.num_rar_entries;
	u32 mpsar_lo, mpsar_hi;

	/* Make sure we are using a valid rar index range */
	if (rar >= rar_entries) {
		wx_err(wxhw, "RAR index %d is out of range.\n", rar);
		return -EINVAL;
	}

	wr32(wxhw, WX_PSR_MAC_SWC_IDX, rar);
	mpsar_lo = rd32(wxhw, WX_PSR_MAC_SWC_VM_L);
	mpsar_hi = rd32(wxhw, WX_PSR_MAC_SWC_VM_H);

	if (!mpsar_lo && !mpsar_hi)
		return 0;

	/* was that the last pool using this rar? */
	if (mpsar_lo == 0 && mpsar_hi == 0 && rar != 0)
		wx_clear_rar(wxhw, rar);

	return 0;
}

/**
 *  wx_init_uta_tables - Initialize the Unicast Table Array
 *  @wxhw: pointer to hardware structure
 **/
static void wx_init_uta_tables(struct wx_hw *wxhw)
{
	int i;

	wx_dbg(wxhw, " Clearing UTA\n");

	for (i = 0; i < 128; i++)
		wr32(wxhw, WX_PSR_UC_TBL(i), 0);
}

/**
 *  wx_init_rx_addrs - Initializes receive address filters.
 *  @wxhw: pointer to hardware structure
 *
 *  Places the MAC address in receive address register 0 and clears the rest
 *  of the receive address registers. Clears the multicast table. Assumes
 *  the receiver is in reset when the routine is called.
 **/
void wx_init_rx_addrs(struct wx_hw *wxhw)
{
	u32 rar_entries = wxhw->mac.num_rar_entries;
	u32 psrctl;
	int i;

	/* If the current mac address is valid, assume it is a software override
	 * to the permanent address.
	 * Otherwise, use the permanent address from the eeprom.
	 */
	if (!is_valid_ether_addr(wxhw->mac.addr)) {
		/* Get the MAC address from the RAR0 for later reference */
		wx_get_mac_addr(wxhw, wxhw->mac.addr);
		wx_dbg(wxhw, "Keeping Current RAR0 Addr = %pM\n", wxhw->mac.addr);
	} else {
		/* Setup the receive address. */
		wx_dbg(wxhw, "Overriding MAC Address in RAR[0]\n");
		wx_dbg(wxhw, "New MAC Addr = %pM\n", wxhw->mac.addr);

		wx_set_rar(wxhw, 0, wxhw->mac.addr, 0, WX_PSR_MAC_SWC_AD_H_AV);

		if (wxhw->mac.type == wx_mac_sp) {
			/* clear VMDq pool/queue selection for RAR 0 */
			wx_clear_vmdq(wxhw, 0, WX_CLEAR_VMDQ_ALL);
		}
	}

	/* Zero out the other receive addresses. */
	wx_dbg(wxhw, "Clearing RAR[1-%d]\n", rar_entries - 1);
	for (i = 1; i < rar_entries; i++) {
		wr32(wxhw, WX_PSR_MAC_SWC_IDX, i);
		wr32(wxhw, WX_PSR_MAC_SWC_AD_L, 0);
		wr32(wxhw, WX_PSR_MAC_SWC_AD_H, 0);
	}

	/* Clear the MTA */
	wxhw->addr_ctrl.mta_in_use = 0;
	psrctl = rd32(wxhw, WX_PSR_CTL);
	psrctl &= ~(WX_PSR_CTL_MO | WX_PSR_CTL_MFE);
	psrctl |= wxhw->mac.mc_filter_type << WX_PSR_CTL_MO_SHIFT;
	wr32(wxhw, WX_PSR_CTL, psrctl);
	wx_dbg(wxhw, " Clearing MTA\n");
	for (i = 0; i < wxhw->mac.mcft_size; i++)
		wr32(wxhw, WX_PSR_MC_TBL(i), 0);

	wx_init_uta_tables(wxhw);
}
EXPORT_SYMBOL(wx_init_rx_addrs);

void wx_disable_rx(struct wx_hw *wxhw)
{
	u32 pfdtxgswc;
	u32 rxctrl;

	rxctrl = rd32(wxhw, WX_RDB_PB_CTL);
	if (rxctrl & WX_RDB_PB_CTL_RXEN) {
		pfdtxgswc = rd32(wxhw, WX_PSR_CTL);
		if (pfdtxgswc & WX_PSR_CTL_SW_EN) {
			pfdtxgswc &= ~WX_PSR_CTL_SW_EN;
			wr32(wxhw, WX_PSR_CTL, pfdtxgswc);
			wxhw->mac.set_lben = true;
		} else {
			wxhw->mac.set_lben = false;
		}
		rxctrl &= ~WX_RDB_PB_CTL_RXEN;
		wr32(wxhw, WX_RDB_PB_CTL, rxctrl);

		if (!(((wxhw->subsystem_device_id & WX_NCSI_MASK) == WX_NCSI_SUP) ||
		      ((wxhw->subsystem_device_id & WX_WOL_MASK) == WX_WOL_SUP))) {
			/* disable mac receiver */
			wr32m(wxhw, WX_MAC_RX_CFG,
			      WX_MAC_RX_CFG_RE, 0);
		}
	}
}
EXPORT_SYMBOL(wx_disable_rx);

/**
 *  wx_disable_pcie_master - Disable PCI-express master access
 *  @wxhw: pointer to hardware structure
 *
 *  Disables PCI-Express master access and verifies there are no pending
 *  requests.
 **/
int wx_disable_pcie_master(struct wx_hw *wxhw)
{
	int status = 0;
	u32 val;

	/* Always set this bit to ensure any future transactions are blocked */
	pci_clear_master(wxhw->pdev);

	/* Exit if master requests are blocked */
	if (!(rd32(wxhw, WX_PX_TRANSACTION_PENDING)))
		return 0;

	/* Poll for master request bit to clear */
	status = read_poll_timeout(rd32, val, !val, 100, WX_PCI_MASTER_DISABLE_TIMEOUT,
				   false, wxhw, WX_PX_TRANSACTION_PENDING);
	if (status < 0)
		wx_err(wxhw, "PCIe transaction pending bit did not clear.\n");

	return status;
}
EXPORT_SYMBOL(wx_disable_pcie_master);

/**
 *  wx_stop_adapter - Generic stop Tx/Rx units
 *  @wxhw: pointer to hardware structure
 *
 *  Sets the adapter_stopped flag within wx_hw struct. Clears interrupts,
 *  disables transmit and receive units. The adapter_stopped flag is used by
 *  the shared code and drivers to determine if the adapter is in a stopped
 *  state and should not touch the hardware.
 **/
int wx_stop_adapter(struct wx_hw *wxhw)
{
	u16 i;

	/* Set the adapter_stopped flag so other driver functions stop touching
	 * the hardware
	 */
	wxhw->adapter_stopped = true;

	/* Disable the receive unit */
	wx_disable_rx(wxhw);

	/* Set interrupt mask to stop interrupts from being generated */
	wx_intr_disable(wxhw, WX_INTR_ALL);

	/* Clear any pending interrupts, flush previous writes */
	wr32(wxhw, WX_PX_MISC_IC, 0xffffffff);
	wr32(wxhw, WX_BME_CTL, 0x3);

	/* Disable the transmit unit.  Each queue must be disabled. */
	for (i = 0; i < wxhw->mac.max_tx_queues; i++) {
		wr32m(wxhw, WX_PX_TR_CFG(i),
		      WX_PX_TR_CFG_SWFLSH | WX_PX_TR_CFG_ENABLE,
		      WX_PX_TR_CFG_SWFLSH);
	}

	/* Disable the receive unit by stopping each queue */
	for (i = 0; i < wxhw->mac.max_rx_queues; i++) {
		wr32m(wxhw, WX_PX_RR_CFG(i),
		      WX_PX_RR_CFG_RR_EN, 0);
	}

	/* flush all queues disables */
	WX_WRITE_FLUSH(wxhw);

	/* Prevent the PCI-E bus from hanging by disabling PCI-E master
	 * access and verify no pending requests
	 */
	return wx_disable_pcie_master(wxhw);
}
EXPORT_SYMBOL(wx_stop_adapter);

void wx_reset_misc(struct wx_hw *wxhw)
{
	int i;

	/* receive packets that size > 2048 */
	wr32m(wxhw, WX_MAC_RX_CFG, WX_MAC_RX_CFG_JE, WX_MAC_RX_CFG_JE);

	/* clear counters on read */
	wr32m(wxhw, WX_MMC_CONTROL,
	      WX_MMC_CONTROL_RSTONRD, WX_MMC_CONTROL_RSTONRD);

	wr32m(wxhw, WX_MAC_RX_FLOW_CTRL,
	      WX_MAC_RX_FLOW_CTRL_RFE, WX_MAC_RX_FLOW_CTRL_RFE);

	wr32(wxhw, WX_MAC_PKT_FLT, WX_MAC_PKT_FLT_PR);

	wr32m(wxhw, WX_MIS_RST_ST,
	      WX_MIS_RST_ST_RST_INIT, 0x1E00);

	/* errata 4: initialize mng flex tbl and wakeup flex tbl*/
	wr32(wxhw, WX_PSR_MNG_FLEX_SEL, 0);
	for (i = 0; i < 16; i++) {
		wr32(wxhw, WX_PSR_MNG_FLEX_DW_L(i), 0);
		wr32(wxhw, WX_PSR_MNG_FLEX_DW_H(i), 0);
		wr32(wxhw, WX_PSR_MNG_FLEX_MSK(i), 0);
	}
	wr32(wxhw, WX_PSR_LAN_FLEX_SEL, 0);
	for (i = 0; i < 16; i++) {
		wr32(wxhw, WX_PSR_LAN_FLEX_DW_L(i), 0);
		wr32(wxhw, WX_PSR_LAN_FLEX_DW_H(i), 0);
		wr32(wxhw, WX_PSR_LAN_FLEX_MSK(i), 0);
	}

	/* set pause frame dst mac addr */
	wr32(wxhw, WX_RDB_PFCMACDAL, 0xC2000001);
	wr32(wxhw, WX_RDB_PFCMACDAH, 0x0180);
}
EXPORT_SYMBOL(wx_reset_misc);

/**
 *  wx_get_pcie_msix_counts - Gets MSI-X vector count
 *  @wxhw: pointer to hardware structure
 *  @msix_count: number of MSI interrupts that can be obtained
 *  @max_msix_count: number of MSI interrupts that mac need
 *
 *  Read PCIe configuration space, and get the MSI-X vector count from
 *  the capabilities table.
 **/
int wx_get_pcie_msix_counts(struct wx_hw *wxhw, u16 *msix_count, u16 max_msix_count)
{
	struct pci_dev *pdev = wxhw->pdev;
	struct device *dev = &pdev->dev;
	int pos;

	*msix_count = 1;
	pos = pci_find_capability(pdev, PCI_CAP_ID_MSIX);
	if (!pos) {
		dev_err(dev, "Unable to find MSI-X Capabilities\n");
		return -EINVAL;
	}
	pci_read_config_word(pdev,
			     pos + PCI_MSIX_FLAGS,
			     msix_count);
	*msix_count &= WX_PCIE_MSIX_TBL_SZ_MASK;
	/* MSI-X count is zero-based in HW */
	*msix_count += 1;

	if (*msix_count > max_msix_count)
		*msix_count = max_msix_count;

	return 0;
}
EXPORT_SYMBOL(wx_get_pcie_msix_counts);

int wx_sw_init(struct wx_hw *wxhw)
{
	struct pci_dev *pdev = wxhw->pdev;
	u32 ssid = 0;
	int err = 0;

	wxhw->vendor_id = pdev->vendor;
	wxhw->device_id = pdev->device;
	wxhw->revision_id = pdev->revision;
	wxhw->oem_svid = pdev->subsystem_vendor;
	wxhw->oem_ssid = pdev->subsystem_device;
	wxhw->bus.device = PCI_SLOT(pdev->devfn);
	wxhw->bus.func = PCI_FUNC(pdev->devfn);

	if (wxhw->oem_svid == PCI_VENDOR_ID_WANGXUN) {
		wxhw->subsystem_vendor_id = pdev->subsystem_vendor;
		wxhw->subsystem_device_id = pdev->subsystem_device;
	} else {
		err = wx_flash_read_dword(wxhw, 0xfffdc, &ssid);
		if (!err)
			wxhw->subsystem_device_id = swab16((u16)ssid);

		return err;
	}

	return 0;
}
EXPORT_SYMBOL(wx_sw_init);

MODULE_LICENSE("GPL");
