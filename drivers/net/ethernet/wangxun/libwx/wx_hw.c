// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2022 Beijing WangXun Technology Co., Ltd. */

#include <linux/iopoll.h>
#include <linux/pci.h>

#include "wx_type.h"
#include "wx_hw.h"

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
