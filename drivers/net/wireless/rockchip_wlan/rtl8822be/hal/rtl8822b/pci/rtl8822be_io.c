/******************************************************************************
 *
 * Copyright(c) 2015 - 2016 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTL8822BE_IO_C_

#include <drv_types.h>		/* PADAPTER and etc. */

#ifdef RTK_129X_PLATFORM
#include <soc/realtek/rtd129x_lockapi.h>

#define IO_2K_MASK 0xFFFFF800
#define IO_4K_MASK 0xFFFFF000
#define MAX_RETRY 5

static u32 pci_io_read_129x(struct dvobj_priv *pdvobjpriv, u32 addr, u8 size)
{
	unsigned long mask_addr = pdvobjpriv->mask_addr;
	unsigned long tran_addr = pdvobjpriv->tran_addr;
	u8 busnumber = pdvobjpriv->pcipriv.busnumber;
	u32 rval = 0;
	u32 mask;
	u32 translate_val = 0;
	u32 tmp_addr = addr & 0xFFF;
	_irqL irqL;
	u32 pci_error_status = 0;
	int retry_cnt = 0;
	unsigned long flags;

	_enter_critical(&pdvobjpriv->io_reg_lock, &irqL);

	/* PCIE1.1 0x9804FCEC, PCIE2.0 0x9803CCEC & 0x9803CC68
	 * can't be used because of 1295 hardware issue.
	 */
	if ((tmp_addr == 0xCEC) || ((busnumber == 0x01) &&
	    (tmp_addr == 0xC68))) {
		mask = IO_2K_MASK;
		writel(0xFFFFF800, (u8 *)mask_addr);
		translate_val = readl((u8 *)tran_addr);
		writel(translate_val|(addr&mask), (u8 *)tran_addr);
	} else if (addr >= 0x1000) {
		mask = IO_4K_MASK;
		translate_val = readl((u8 *)tran_addr);
		writel(translate_val|(addr&mask), (u8 *)tran_addr);
	} else
		mask = 0x0;

pci_read_129x_retry:

	/* All RBUS1 driver need to have a workaround for emmc hardware error */
	/* Need to protect 0xXXXX_X8XX~ 0xXXXX_X9XX */
	if ((tmp_addr>0x7FF) && (tmp_addr<0xA00))
		rtk_lockapi_lock(flags, __func__);

	switch (size) {
	case 1:
		rval = readb((u8 *)pdvobjpriv->pci_mem_start + (addr&~mask));
		break;
	case 2:
		rval = readw((u8 *)pdvobjpriv->pci_mem_start + (addr&~mask));
		break;
	case 4:
		rval = readl((u8 *)pdvobjpriv->pci_mem_start + (addr&~mask));
		break;
	default:
		RTW_WARN("RTD129X: %s: wrong size %d\n", __func__, size);
		break;
	}

	if ((tmp_addr>0x7FF) && (tmp_addr<0xA00))
		rtk_lockapi_unlock(flags, __func__);

	//DLLP error patch
	pci_error_status = readl( (u8 *)(pdvobjpriv->ctrl_start + 0x7C));
	if(pci_error_status & 0x1F) {
		writel(pci_error_status, (u8 *)(pdvobjpriv->ctrl_start + 0x7C));
		RTW_WARN("RTD129X: %s: DLLP(#%d) 0x%x reg=0x%x val=0x%x\n", __func__, retry_cnt, pci_error_status, addr, rval);

		if(retry_cnt < MAX_RETRY) {
			retry_cnt++;
			goto pci_read_129x_retry;
		}
	}

	/* PCIE1.1 0x9804FCEC, PCIE2.0 0x9803CCEC & 0x9803CC68
	 * can't be used because of 1295 hardware issue.
	 */
	if ((tmp_addr == 0xCEC) || ((busnumber == 0x01) &&
	    (tmp_addr == 0xC68))) {
		writel(translate_val, (u8 *)tran_addr);
		writel(0xFFFFF000, (u8 *)mask_addr);
	} else if (addr >= 0x1000) {
		writel(translate_val, (u8 *)tran_addr);
	}

	_exit_critical(&pdvobjpriv->io_reg_lock, &irqL);

	return rval;
}

static void pci_io_write_129x(struct dvobj_priv *pdvobjpriv,
			      u32 addr, u8 size, u32 wval)
{
	unsigned long mask_addr = pdvobjpriv->mask_addr;
	unsigned long tran_addr = pdvobjpriv->tran_addr;
	u8 busnumber = pdvobjpriv->pcipriv.busnumber;
	u32 mask;
	u32 translate_val = 0;
	u32 tmp_addr = addr & 0xFFF;
	_irqL irqL;
	unsigned long flags;

	_enter_critical(&pdvobjpriv->io_reg_lock, &irqL);

	/* PCIE1.1 0x9804FCEC, PCIE2.0 0x9803CCEC & 0x9803CC68
	 * can't be used because of 1295 hardware issue.
	 */
	if ((tmp_addr == 0xCEC) || ((busnumber == 0x01) &&
	    (tmp_addr == 0xC68))) {
		mask = IO_2K_MASK;
		writel(0xFFFFF800, (u8 *)mask_addr);
		translate_val = readl((u8 *)tran_addr);
		writel(translate_val|(addr&mask), (u8 *)tran_addr);
	} else if (addr >= 0x1000) {
		mask = IO_4K_MASK;
		translate_val = readl((u8 *)tran_addr);
		writel(translate_val|(addr&mask), (u8 *)tran_addr);
	} else
		mask = 0x0;

	/* All RBUS1 driver need to have a workaround for emmc hardware error */
	/* Need to protect 0xXXXX_X8XX~ 0xXXXX_X9XX */
	if ((tmp_addr>0x7FF) && (tmp_addr<0xA00))
		rtk_lockapi_lock(flags, __func__);

	switch (size) {
	case 1:
		writeb((u8)wval,
		       (u8 *)pdvobjpriv->pci_mem_start + (addr&~mask));
		break;
	case 2:
		writew((u16)wval,
		       (u8 *)pdvobjpriv->pci_mem_start + (addr&~mask));
		break;
	case 4:
		writel((u32)wval,
		       (u8 *)pdvobjpriv->pci_mem_start + (addr&~mask));
		break;
	default:
		RTW_WARN("RTD129X: %s: wrong size %d\n", __func__, size);
		break;
	}

	if ((tmp_addr>0x7FF) && (tmp_addr<0xA00))
		rtk_lockapi_unlock(flags, __func__);

	/* PCIE1.1 0x9804FCEC, PCIE2.0 0x9803CCEC & 0x9803CC68
	 * can't be used because of 1295 hardware issue.
	 */
	if ((tmp_addr == 0xCEC) || ((busnumber == 0x01) &&
	    (tmp_addr == 0xC68))) {
		writel(translate_val, (u8 *)tran_addr);
		writel(0xFFFFF000, (u8 *)mask_addr);
	} else if (addr >= 0x1000) {
		writel(translate_val, (u8 *)tran_addr);
	}

	_exit_critical(&pdvobjpriv->io_reg_lock, &irqL);
}

static u8 pci_read8_129x(struct intf_hdl *phdl, u32 addr)
{
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)phdl->pintf_dev;

	return (u8)pci_io_read_129x(pdvobjpriv, addr, 1);
}

static u16 pci_read16_129x(struct intf_hdl *phdl, u32 addr)
{
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)phdl->pintf_dev;

	return (u16)pci_io_read_129x(pdvobjpriv, addr, 2);
}

static u32 pci_read32_129x(struct intf_hdl *phdl, u32 addr)
{
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)phdl->pintf_dev;

	return (u32)pci_io_read_129x(pdvobjpriv, addr, 4);
}

/*
 * 2009.12.23. by tynli. Suggested by SD1 victorh.
 * For ASPM hang on AMD and Nvidia.
 * 20100212 Tynli: Do read IO operation after write for
 * all PCI bridge suggested by SD1. Origianally this is only for INTEL.
 */
static int pci_write8_129x(struct intf_hdl *phdl, u32 addr, u8 val)
{
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)phdl->pintf_dev;

	pci_io_write_129x(pdvobjpriv, addr, 1, val);
	return 1;
}

static int pci_write16_129x(struct intf_hdl *phdl, u32 addr, u16 val)
{
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)phdl->pintf_dev;

	pci_io_write_129x(pdvobjpriv, addr, 2, val);
	return 2;
}

static int pci_write32_129x(struct intf_hdl *phdl, u32 addr, u32 val)
{
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)phdl->pintf_dev;

	pci_io_write_129x(pdvobjpriv, addr, 4, val);
	return 4;
}

#else /* original*/

static u8 pci_read8(struct intf_hdl *phdl, u32 addr)
{
	struct dvobj_priv *pdvobjpriv = (struct dvobj_priv *)phdl->pintf_dev;

	return 0xff & readb((u8 *)pdvobjpriv->pci_mem_start + addr);
}

static u16 pci_read16(struct intf_hdl *phdl, u32 addr)
{
	struct dvobj_priv *pdvobjpriv = (struct dvobj_priv *)phdl->pintf_dev;

	return readw((u8 *)pdvobjpriv->pci_mem_start + addr);
}

static u32 pci_read32(struct intf_hdl *phdl, u32 addr)
{
	struct dvobj_priv *pdvobjpriv = (struct dvobj_priv *)phdl->pintf_dev;

	return readl((u8 *)pdvobjpriv->pci_mem_start + addr);
}

/*
 * 2009.12.23. by tynli. Suggested by SD1 victorh.
 * For ASPM hang on AMD and Nvidia.
 * 20100212 Tynli: Do read IO operation after write for
 * all PCI bridge suggested by SD1. Origianally this is only for INTEL.
 */
static int pci_write8(struct intf_hdl *phdl, u32 addr, u8 val)
{
	struct dvobj_priv *pdvobjpriv = (struct dvobj_priv *)phdl->pintf_dev;

	writeb(val, (u8 *)pdvobjpriv->pci_mem_start + addr);
	return 1;
}

static int pci_write16(struct intf_hdl *phdl, u32 addr, u16 val)
{
	struct dvobj_priv *pdvobjpriv = (struct dvobj_priv *)phdl->pintf_dev;

	writew(val, (u8 *)pdvobjpriv->pci_mem_start + addr);
	return 2;
}

static int pci_write32(struct intf_hdl *phdl, u32 addr, u32 val)
{
	struct dvobj_priv *pdvobjpriv = (struct dvobj_priv *)phdl->pintf_dev;

	writel(val, (u8 *)pdvobjpriv->pci_mem_start + addr);
	return 4;
}
#endif /* RTK_129X_PLATFORM */

static void pci_read_mem(struct intf_hdl *phdl, u32 addr, u32 cnt, u8 *rmem)
{
	RTW_INFO("%s(%d)fake function\n", __func__, __LINE__);
}

static void pci_write_mem(struct intf_hdl *phdl, u32 addr, u32 cnt, u8 *wmem)
{
	RTW_INFO("%s(%d)fake function\n", __func__, __LINE__);
}

static u32 pci_read_port(struct intf_hdl *phdl, u32 addr, u32 cnt, u8 *rmem)
{
	return 0;
}

static u32 pci_write_port(struct intf_hdl *phdl, u32 addr, u32 cnt, u8 *wmem)
{
	_adapter *padapter = (_adapter *)phdl->padapter;

	padapter->pnetdev->trans_start = jiffies;

	return 0;
}

void rtl8822be_set_intf_ops(struct _io_ops *pops)
{
	_func_enter_;

	_rtw_memset((u8 *)pops, 0, sizeof(struct _io_ops));

#ifdef RTK_129X_PLATFORM
	pops->_read8 = &pci_read8_129x;
	pops->_read16 = &pci_read16_129x;
	pops->_read32 = &pci_read32_129x;
#else
	pops->_read8 = &pci_read8;
	pops->_read16 = &pci_read16;
	pops->_read32 = &pci_read32;
#endif /* RTK_129X_PLATFORM */

	pops->_read_mem = &pci_read_mem;
	pops->_read_port = &pci_read_port;

#ifdef RTK_129X_PLATFORM
	pops->_write8 = &pci_write8_129x;
	pops->_write16 = &pci_write16_129x;
	pops->_write32 = &pci_write32_129x;
#else
	pops->_write8 = &pci_write8;
	pops->_write16 = &pci_write16;
	pops->_write32 = &pci_write32;
#endif /* RTK_129X_PLATFORM */

	pops->_write_mem = &pci_write_mem;
	pops->_write_port = &pci_write_port;

	_func_exit_;

}
