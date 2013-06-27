/*
 * drivers/usb/sunxi_usb/usbc/usbc_phy.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * daniel <daniel@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */


#include  "usbc_i.h"


/*
 ***************************************************************************
 *
 * 定义USB PHY控制寄存器位
 *
 ***************************************************************************
 */

//Common Control Bits for Both PHYs
#define  USBC_PHY_PLL_BW				0x03
#define  USBC_PHY_RES45_CAL_EN			0x0c

//Private Control Bits for Each PHY
#define  USBC_PHY_TX_AMPLITUDE_TUNE		0x20
#define  USBC_PHY_TX_SLEWRATE_TUNE		0x22
#define  USBC_PHY_VBUSVALID_TH_SEL		0x25
#define  USBC_PHY_PULLUP_RES_SEL		0x27
#define  USBC_PHY_OTG_FUNC_EN			0x28
#define  USBC_PHY_VBUS_DET_EN			0x29
#define  USBC_PHY_DISCON_TH_SEL			0x2a

#if 0
/*
 ***************************************************************************
 *
 * read out one bit of USB PHY register
 *
 ***************************************************************************
 */
static __u32 __USBC_PHY_REG_READ(__u32 usbc_base_addr, __u32 usbc_phy_reg_addr)
{
  	__u32 reg_val = 0;
  	__u32 i = 0;

  	USBC_Writeb(usbc_phy_reg_addr, USBC_REG_PHYCTL(USBC0_REGS_BASE) + 1);
  	for(i=0; i<0x4; i++);
  	reg_val = USBC_Readb(USBC_REG_PHYCTL(USBC0_REGS_BASE) + 2);
  	if(usbc_base_addr == USBC0_REGS_BASE)
  		return (reg_val & 0x1);
  	else
  		return ((reg_val >> 1) & 0x1);
}

/*
 ***************************************************************************
 *
 * Write one bit of USB PHY register
 *
 ***************************************************************************
 */
static void __USBC_PHY_REG_WRITE(__u32 usbc_base_addr, __u32 usbc_phy_reg_addr, __u32 usbc_phy_reg_data)
{
  	__u32 reg_val = 0;

  	USBC_Writeb(usbc_phy_reg_addr, USBC_REG_PHYCTL(USBC0_REGS_BASE) + 1);
  	reg_val = USBC_Readb(USBC_REG_PHYCTL(USBC0_REGS_BASE));
  	reg_val &= ~(0x1 << 7);
  	reg_val |= (usbc_phy_reg_data & 0x1) << 7;
  	if(usbc_base_addr == USBC0_REGS_BASE){
  		reg_val &= ~0x1;
  		USBC_Writeb(reg_val, USBC_REG_PHYCTL(USBC0_REGS_BASE));
  		reg_val |= 0x1;
  		USBC_Writeb(reg_val, USBC_REG_PHYCTL(USBC0_REGS_BASE));
  		reg_val &= ~0x1;
  		USBC_Writeb(reg_val, USBC_REG_PHYCTL(USBC0_REGS_BASE));
  	}else{
  		reg_val &= ~0x2;
  		USBC_Writeb(reg_val, USBC_REG_PHYCTL(USBC0_REGS_BASE));
  		reg_val |= 0x2;
  		USBC_Writeb(reg_val, USBC_REG_PHYCTL(USBC0_REGS_BASE));
  		reg_val &= ~0x2;
  		USBC_Writeb(reg_val, USBC_REG_PHYCTL(USBC0_REGS_BASE));
  	}
}


/*
 ***************************************************************************
 *
 * Set USB PLL BandWidth, val = 0~3, defualt = 0x2
 *
 ***************************************************************************
 */
/*
static void __USBC_PHY_SET_PLL_BW(__u32 val)
{
    __USBC_PHY_REG_WRITE(USBC0_REGS_BASE, USBC_PHY_PLL_BW, val);
    __USBC_PHY_REG_WRITE(USBC0_REGS_BASE, USBC_PHY_PLL_BW + 1, val >> 1);
}
*/

/*
 ***************************************************************************
 *
 * Enable/Disable USB res45 Calibration, val = 0--Disable；1--Enable, default = 0
 *
 ***************************************************************************
 */
static void __USBC_PHY_RES45_CALIBRATION_ENABLE(__u32 val)
{
    __USBC_PHY_REG_WRITE(USBC0_REGS_BASE, USBC_PHY_RES45_CAL_EN, val);
}

/*
 ***************************************************************************
 *
 * Set USB TX Signal Amplitude, val = 0~3, default = 0x0
 *
 ***************************************************************************
 */
static void __USBC_PHY_SET_TX_AMPLITUDE(__u32 usbc_base_addr, __u32 val)
{
    __USBC_PHY_REG_WRITE(usbc_base_addr, USBC_PHY_TX_AMPLITUDE_TUNE, val);
    __USBC_PHY_REG_WRITE(usbc_base_addr, USBC_PHY_TX_AMPLITUDE_TUNE + 1, val >> 1);
}

/*
 ***************************************************************************
 *
 * Set USB TX Signal Slew Rate, val = 0~7, default = 0x5
 *
 ***************************************************************************
 */
static void __USBC_PHY_SET_TX_SLEWRATE(__u32 usbc_base_addr, __u32 val)
{
    __USBC_PHY_REG_WRITE(usbc_base_addr, USBC_PHY_TX_SLEWRATE_TUNE, val);
    __USBC_PHY_REG_WRITE(usbc_base_addr, USBC_PHY_TX_SLEWRATE_TUNE + 1, val >> 1);
    __USBC_PHY_REG_WRITE(usbc_base_addr, USBC_PHY_TX_SLEWRATE_TUNE + 2, val >> 2);
}

/*
 ***************************************************************************
 *
 * Set USB VBUS Valid Threshold, val = 0~3, default = 2
 *
 ***************************************************************************
 */
/*
static void __USBC_PHY_SET_VBUS_VALID_THRESHOLD(__u32 usbc_base_addr, __u32 val)
{
    __USBC_PHY_REG_WRITE(usbc_base_addr, USBC_PHY_VBUSVALID_TH_SEL, val);
    __USBC_PHY_REG_WRITE(usbc_base_addr, USBC_PHY_VBUSVALID_TH_SEL + 1, val >> 1);
}
*/

/*
 ***************************************************************************
 *
 * Enable/Diasble USB OTG Function, val = 0--Disable；1--Enable, default = 1
 *
 ***************************************************************************
 */
/*
static void __USBC_PHY_OTG_FUNC_ENABLE(__u32 usbc_base_addr, __u32 val)
{
    __USBC_PHY_REG_WRITE(usbc_base_addr, USBC_PHY_OTG_FUNC_EN, val);
}
*/

/*
 ***************************************************************************
 *
 * Enable/Diasble USB VBUS Detect Function, val = 0--Disable；1--Enable, default = 1
 *
 ***************************************************************************
 */
/*
static void __USBC_PHY_VBUS_DET_ENABLE(__u32 usbc_base_addr, __u32 val)
{
    __USBC_PHY_REG_WRITE(usbc_base_addr, USBC_PHY_VBUS_DET_EN, val);
}
*/

/*
 ***************************************************************************
 *
 * Set USB Disconnect Detect Threshold, val = 0~3, default = 1
 *
 ***************************************************************************
 */
static void __USBC_PHY_SET_DISCON_DET_THRESHOLD(__u32 usbc_base_addr, __u32 val)
{
    __USBC_PHY_REG_WRITE(usbc_base_addr, USBC_PHY_DISCON_TH_SEL, val);
    __USBC_PHY_REG_WRITE(usbc_base_addr, USBC_PHY_DISCON_TH_SEL + 1, val >> 1);
}
#endif

/*
***********************************************************************************
*                     USBC_PHY_SetCommonConfig
*
* Description:
*    Phy的公共设置，用于USB PHY的公共初始化
*
* Arguments:
*    NULL
*
* Returns:
*    NULL
*
* note:
*    无
*
***********************************************************************************
*/
void USBC_PHY_SetCommonConfig(void)
{
    //__USBC_PHY_RES45_CALIBRATION_ENABLE(1);
}

/*
***********************************************************************************
*                     USBC_PHY_SetPrivateConfig
*
* Description:
*    USB PHY的各自设置
*
* Arguments:
*    hUSB       :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*
* Returns:
*    NULL
*
* note:
*    无
*
***********************************************************************************
*/
void USBC_PHY_SetPrivateConfig(__hdle hUSB)
{
//    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
//
//	if(usbc_otg == NULL){
//		return ;
//	}
//
//    USBC_REG_set_bit_l(0, USBC_REG_PHYTUNE(usbc_otg->base_addr));
//    USBC_REG_set_bit_l(7, USBC_REG_PHYTUNE(usbc_otg->base_addr));
//    USBC_REG_set_bit_l(6, USBC_REG_PHYTUNE(usbc_otg->base_addr));
//    USBC_REG_set_bit_l(5, USBC_REG_PHYTUNE(usbc_otg->base_addr));
//    USBC_REG_set_bit_l(4, USBC_REG_PHYTUNE(usbc_otg->base_addr));
//    //__USBC_PHY_SET_TX_AMPLITUDE(usbc_otg->base_addr, 2);
//    //__USBC_PHY_SET_TX_SLEWRATE(usbc_otg->base_addr, 6);
//    //__USBC_PHY_SET_DISCON_DET_THRESHOLD(usbc_otg->base_addr, 3);
}

/*
***********************************************************************************
*                     USBC_PHY_GetCommonConfig
*
* Description:
*    读取Phy的公共设置，主要用于Debug，看Phy的设置是否正确
*
* Arguments:
*    NULL
*
* Returns:
*    32bits的USB PHY公共设置值
*
* note:
*    无
*
***********************************************************************************
*/
__u32 USBC_PHY_GetCommonConfig(void)
{
    __u32 reg_val = 0;
/*
    __u32 i = 0;

    reg_val = 0;
	for(i=0; i<0x20; i++)
	{
		reg_val = reg_val << 1;
		reg_val |= __USBC_PHY_REG_READ(USBC0_REGS_BASE, (0x1f - i)) & 0x1;
	}
*/
	return reg_val;
}

/*
***********************************************************************************
*                                usb_phy0_write
*Description:
*    写usb phy0的phy寄存器，主要用于phy0 standby时的写入
*
*Arguments:
*    address,  data,   dmask
*
*returns:
*    return the data wrote
*
*note:
*    no
************************************************************************************
*/

static __u32 usb_phy0_write(__u32 addr, __u32 data, __u32 dmask, __u32 usbc_base_addr)
{
	__u32 i=0;

	data = data & 0x0f;
	addr = addr & 0x0f;
	dmask = dmask & 0x0f;

	USBC_Writeb((dmask<<4)|data, usbc_base_addr + 0x404 + 2);
	USBC_Writeb(addr|0x10, usbc_base_addr + 0x404);
	for(i=0;i<5;i++);
	USBC_Writeb(addr|0x30, usbc_base_addr + 0x404);
	for(i=0;i<5;i++);
	USBC_Writeb(addr|0x10, usbc_base_addr + 0x404);
	for(i=0;i<5;i++);
	return (USBC_Readb(usbc_base_addr + 0x404 + 3) & 0x0f);
}

/*
*******************************************************************************
*                     USBC_phy_Standby
*
* Description:
*    Standby the usb phy with the input usb phy index number
*
* Parameters:
*    usb phy index number, which used to select the phy to standby
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void USBC_phy_Standby(__hdle hUSB, __u32 phy_index)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(phy_index == 0){
        usb_phy0_write(0xB, 0x8, 0xf, usbc_otg->base_addr);
        usb_phy0_write(0x7, 0xf, 0xf, usbc_otg->base_addr);
        usb_phy0_write(0x1, 0xf, 0xf, usbc_otg->base_addr);
        usb_phy0_write(0x2, 0xf, 0xf, usbc_otg->base_addr);
    }

	return;
}

/*
*******************************************************************************
*                     USBC_Phy_Standby_Recover
*
* Description:
*    Recover the standby phy with the input index number
*
* Parameters:
*    usb phy index number
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void USBC_Phy_Standby_Recover(__hdle hUSB, __u32 phy_index)
{
	__u32 i;

	if(phy_index == 0){
		for(i=0; i<0x10; i++);
	}

	return;
}

/*
*******************************************************************************
*                     USBC_Phy_GetCsr
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static __u32 USBC_Phy_GetCsr(__u32 usbc_no)
{
	__u32 val = 0x0;

	switch(usbc_no){
		case 0:
			val = SW_VA_USB0_IO_BASE + 0x404;
		break;

		case 1:
			val = SW_VA_USB0_IO_BASE + 0x404;
		break;

		case 2:
			val = SW_VA_USB0_IO_BASE + 0x404;
		break;

		default:
		break;
	}

	return val;
}

/*
*******************************************************************************
*                     USBC_Phy_TpRead
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
#if 0
static __u32 USBC_Phy_TpRead(__u32 usbc_no, __u32 addr, __u32 len)
{
	__u32 temp = 0, ret = 0;
	__u32 i=0;
	__u32 j=0;

	for(j = len; j > 0; j--)
	{
		/* set  the bit address to be read */
		temp = USBC_Readl(USBC_Phy_GetCsr(usbc_no));
		temp &= ~(0xff << 8);
		temp |= ((addr + j -1) << 8);
		USBC_Writel(temp, USBC_Phy_GetCsr(usbc_no));

		for(i = 0; i < 0x4; i++);

		temp = USBC_Readl(USBC_Phy_GetCsr(usbc_no));
		ret <<= 1;
		ret |= ((temp >> (16 + usbc_no)) & 0x1);
	}

	return ret;
}
#endif

/*
*******************************************************************************
*                     USBC_Phy_TpWrite
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static __u32 USBC_Phy_TpWrite(__u32 usbc_no, __u32 addr, __u32 data, __u32 len)
{
	__u32 temp = 0, dtmp = 0;
//	__u32 i=0;
	__u32 j=0;

	dtmp = data;
	for(j = 0; j < len; j++)
	{
		/* set  the bit address to be write */
		temp = USBC_Readl(USBC_Phy_GetCsr(usbc_no));
		temp &= ~(0xff << 8);
		temp |= ((addr + j) << 8);
		USBC_Writel(temp, USBC_Phy_GetCsr(usbc_no));

		temp = USBC_Readb(USBC_Phy_GetCsr(usbc_no));
		temp &= ~(0x1 << 7);
		temp |= (dtmp & 0x1) << 7;
		temp &= ~(0x1 << (usbc_no << 1));
		USBC_Writeb(temp, USBC_Phy_GetCsr(usbc_no));

		temp = USBC_Readb(USBC_Phy_GetCsr(usbc_no));
		temp |= (0x1 << (usbc_no << 1));
		USBC_Writeb( temp, USBC_Phy_GetCsr(usbc_no));

		temp = USBC_Readb(USBC_Phy_GetCsr(usbc_no));
		temp &= ~(0x1 << (usbc_no <<1 ));
		USBC_Writeb(temp, USBC_Phy_GetCsr(usbc_no));
		dtmp >>= 1;
	}

	return data;
}

/*
*******************************************************************************
*                     USBC_Phy_Read
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
#if 0
static __u32 USBC_Phy_Read(__u32 usbc_no, __u32 addr, __u32 len)
{
	return USBC_Phy_TpRead(usbc_no, addr, len);
}
#endif

/*
*******************************************************************************
*                     USBC_Phy_Write
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static __u32 USBC_Phy_Write(__u32 usbc_no, __u32 addr, __u32 data, __u32 len)
{
	return USBC_Phy_TpWrite(usbc_no, addr, data, len);
}

/*
*******************************************************************************
*                     UsbPhyInit
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void UsbPhyInit(__u32 usbc_no)
{
//	DMSG_INFO("csr1: usbc%d: 0x%x\n", usbc_no, (u32)USBC_Readl(USBC_Phy_GetCsr(usbc_no)));

    /* 调节45欧阻抗 */
	if(usbc_no == 0){
	    USBC_Phy_Write(usbc_no, 0x0c, 0x01, 1);
	}

//	DMSG_INFO("csr2-0: usbc%d: 0x%x\n", usbc_no, (u32)USBC_Phy_Read(usbc_no, 0x0c, 1));

    /* 调整 USB0 PHY 的幅度和速率 */
	USBC_Phy_Write(usbc_no, 0x20, 0x14, 5);

//	DMSG_INFO("csr2-1: usbc%d: 0x%x\n", usbc_no, (u32)USBC_Phy_Read(usbc_no, 0x20, 5));

    /* 调节 disconnect 域值 */
#ifdef CONFIG_ARCH_SUN4I
	USBC_Phy_Write(usbc_no, 0x2a, 3, 2);
#else
	USBC_Phy_Write(usbc_no, 0x2a, 2, 2);
#endif

//	DMSG_INFO("csr2: usbc%d: 0x%x\n", usbc_no, (u32)USBC_Phy_Read(usbc_no, 0x2a, 2));
//	DMSG_INFO("csr3: usbc%d: 0x%x\n", usbc_no, (u32)USBC_Readl(USBC_Phy_GetCsr(usbc_no)));

	return;
}

/*
*******************************************************************************
*                     UsbPhyEndReset
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void UsbPhyEndReset(__u32 usbc_no)
{
	int i;

	if(usbc_no == 0){
		//Disable Sequelch Detect for a while before Release USB Reset
		USBC_Phy_Write(usbc_no, 0x3c, 0x2, 2);
		for(i=0; i<0x100; i++);
		USBC_Phy_Write(usbc_no, 0x3c, 0x0, 2);
	}

	return;
}

