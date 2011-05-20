/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/otg_ipmate/linux/drivers/dwc_otg_attr.c $
 * $Revision: #5 $
 * $Date: 2005/09/15 $
 * $Change: 537387 $
 *
 * Synopsys HS OTG Linux Software Driver and documentation (hereinafter,
 * "Software") is an Unsupported proprietary work of Synopsys, Inc. unless
 * otherwise expressly agreed to in writing between Synopsys and you.
 * 
 * The Software IS NOT an item of Licensed Software or Licensed Product under
 * any End User Software License Agreement or Agreement for Licensed Product
 * with Synopsys or any supplement thereto. You are permitted to use and
 * redistribute this Software in source and binary forms, with or without
 * modification, provided that redistributions of source code must retain this
 * notice. You may not view, use, disclose, copy or distribute this file or
 * any information contained herein except pursuant to this license grant from
 * Synopsys. If you do not agree with this notice, including the disclaimer
 * below, then you are not authorized to use the Software.
 * 
 * THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS" BASIS
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * ========================================================================== */

/** @file 
 *
 * The diagnostic interface will provide access to the controller for
 * bringing up the hardware and testing.  The Linux driver attributes
 * feature will be used to provide the Linux Diagnostic
 * Interface. These attributes are accessed through sysfs.
 */

/** @page "Linux Module Attributes" 
 *
 * The Linux module attributes feature is used to provide the Linux
 * Diagnostic Interface.  These attributes are accessed through sysfs.
 * The diagnostic interface will provide access to the controller for
 * bringing up the hardware and testing.


 The following table shows the attributes.
 <table>
 <tr>
 <td><b> Name</b></td>
 <td><b> Description</b></td>
 <td><b> Access</b></td>
 </tr>
 
 <tr>
 <td> mode </td>
 <td> Returns the current mode: 0 for device mode, 1 for host mode</td>
 <td> Read</td>
 </tr>
 
 <tr>
 <td> hnpcapable </td>
 <td> Gets or sets the "HNP-capable" bit in the Core USB Configuraton Register.
 Read returns the current value.</td>
 <td> Read/Write</td>
 </tr>
 
 <tr>
 <td> srpcapable </td>
 <td> Gets or sets the "SRP-capable" bit in the Core USB Configuraton Register.
 Read returns the current value.</td>
 <td> Read/Write</td>
 </tr>
 
 <tr>
 <td> hnp </td>
 <td> Initiates the Host Negotiation Protocol.  Read returns the status.</td>
 <td> Read/Write</td>
 </tr>
 
 <tr>
 <td> srp </td>
 <td> Initiates the Session Request Protocol.  Read returns the status.</td>
 <td> Read/Write</td>
 </tr>
 
 <tr>
 <td> buspower </td>
 <td> Gets or sets the Power State of the bus (0 - Off or 1 - On)</td>
 <td> Read/Write</td>
 </tr>
 
 <tr>
 <td> bussuspend </td>
 <td> Suspends the USB bus.</td>
 <td> Read/Write</td>
 </tr>
 
 <tr>
 <td> busconnected </td>
 <td> Gets the connection status of the bus</td>
 <td> Read</td>
 </tr>
 
 <tr>
 <td> gotgctl </td>
 <td> Gets or sets the Core Control Status Register.</td>
 <td> Read/Write</td>
 </tr>
 
 <tr>
 <td> gusbcfg </td>
 <td> Gets or sets the Core USB Configuration Register</td>
 <td> Read/Write</td>
 </tr>
 
 <tr>
 <td> grxfsiz </td>
 <td> Gets or sets the Receive FIFO Size Register</td>
 <td> Read/Write</td>
 </tr>
 
 <tr>
 <td> gnptxfsiz </td>
 <td> Gets or sets the non-periodic Transmit Size Register</td>
 <td> Read/Write</td>
 </tr>
 
 <tr>
 <td> gpvndctl </td>
 <td> Gets or sets the PHY Vendor Control Register</td>
 <td> Read/Write</td>
 </tr>
 
 <tr>
 <td> ggpio </td>
 <td> Gets the value in the lower 16-bits of the General Purpose IO Register
 or sets the upper 16 bits.</td>
 <td> Read/Write</td>
 </tr>
 
 <tr>
 <td> guid </td>
 <td> Gets or sets the value of the User ID Register</td>
 <td> Read/Write</td>
 </tr>
 
 <tr>
 <td> gsnpsid </td>
 <td> Gets the value of the Synopsys ID Regester</td>
 <td> Read</td>
 </tr>
 
 <tr>
 <td> devspeed </td>
 <td> Gets or sets the device speed setting in the DCFG register</td>
 <td> Read/Write</td>
 </tr>
 
 <tr>
 <td> enumspeed </td>
 <td> Gets the device enumeration Speed.</td>
 <td> Read</td>
 </tr>
 
 <tr>
 <td> hptxfsiz </td>
 <td> Gets the value of the Host Periodic Transmit FIFO</td>
 <td> Read</td>
 </tr>
 
 <tr>
 <td> hprt0 </td>
 <td> Gets or sets the value in the Host Port Control and Status Register</td>
 <td> Read/Write</td>
 </tr>
 
 <tr>
 <td> regoffset </td>
 <td> Sets the register offset for the next Register Access</td>
 <td> Read/Write</td>
 </tr>
 
 <tr>
 <td> regvalue </td>
 <td> Gets or sets the value of the register at the offset in the regoffset attribute.</td>
 <td> Read/Write</td>
 </tr>
 
 <tr>
 <td> remote_wakeup </td>
 <td> On read, shows the status of Remote Wakeup. On write, initiates a remote
 wakeup of the host. When bit 0 is 1 and Remote Wakeup is enabled, the Remote
 Wakeup signalling bit in the Device Control Register is set for 1
 milli-second.</td>
 <td> Read/Write</td>
 </tr>
 
 <tr>
 <td> regdump </td>
 <td> Dumps the contents of core registers.</td>
 <td> Read</td>
 </tr>
 
 <tr>
 <td> hcddump </td>
 <td> Dumps the current HCD state.</td>
 <td> Read</td>
 </tr>
 
 <tr>
 <td> hcd_frrem </td>
 <td> Shows the average value of the Frame Remaining
 field in the Host Frame Number/Frame Remaining register when an SOF interrupt
 occurs. This can be used to determine the average interrupt latency. Also
 shows the average Frame Remaining value for start_transfer and the "a" and
 "b" sample points. The "a" and "b" sample points may be used during debugging
 bto determine how long it takes to execute a section of the HCD code.</td>
 <td> Read</td>
 </tr>
 
 <tr>
 <td> rd_reg_test </td>
 <td> Displays the time required to read the GNPTXFSIZ register many times
 (the output shows the number of times the register is read).
 <td> Read</td>
 </tr>
 
 <tr>
 <td> wr_reg_test </td>
 <td> Displays the time required to write the GNPTXFSIZ register many times
 (the output shows the number of times the register is written).
 <td> Read</td>
 </tr>
 
 </table>
 
 Example usage:
 To get the current mode:
 cat /sys/devices/lm0/mode
 
 To power down the USB:
 echo 0 > /sys/devices/lm0/buspower
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/stat.h>  /* permission constants */

#include <asm/sizes.h>
#include <asm/io.h>

#include "linux/dwc_otg_plat.h"
#include "dwc_otg_attr.h"
#include "dwc_otg_driver.h"
#include "dwc_otg_pcd.h"
#include "dwc_otg_hcd.h"

/*
 * MACROs for defining sysfs attribute
 */
#define DWC_OTG_DEVICE_ATTR_BITFIELD_SHOW(_otg_attr_name_,_addr_,_mask_,_shift_,_string_) \
static ssize_t _otg_attr_name_##_show (struct device *_dev, struct device_attribute *attr, char *buf) \
{ \
        dwc_otg_device_t *otg_dev = _dev->platform_data;\
	uint32_t val; \
	val = dwc_read_reg32 (_addr_); \
	val = (val & (_mask_)) >> _shift_; \
	return sprintf (buf, "%s = 0x%x\n", _string_, val); \
}
#define DWC_OTG_DEVICE_ATTR_BITFIELD_STORE(_otg_attr_name_,_addr_,_mask_,_shift_,_string_) \
static ssize_t _otg_attr_name_##_store (struct device *_dev, struct device_attribute *attr, const char *buf, size_t count) \
{ \
        dwc_otg_device_t *otg_dev = _dev->platform_data;\
	uint32_t set = simple_strtoul(buf, NULL, 16); \
	uint32_t clear = set; \
	clear = ((~clear) << _shift_) & _mask_; \
	set = (set << _shift_) & _mask_; \
	dev_dbg(_dev, "Storing Address=0x%08x Set=0x%08x Clear=0x%08x\n", (uint32_t)_addr_, set, clear); \
	dwc_modify_reg32(_addr_, clear, set); \
	return count; \
}

#define DWC_OTG_DEVICE_ATTR_BITFIELD_RW(_otg_attr_name_,_addr_,_mask_,_shift_,_string_) \
DWC_OTG_DEVICE_ATTR_BITFIELD_SHOW(_otg_attr_name_,_addr_,_mask_,_shift_,_string_) \
DWC_OTG_DEVICE_ATTR_BITFIELD_STORE(_otg_attr_name_,_addr_,_mask_,_shift_,_string_) \
DEVICE_ATTR(_otg_attr_name_,0644,_otg_attr_name_##_show,_otg_attr_name_##_store);

#define DWC_OTG_DEVICE_ATTR_BITFIELD_RO(_otg_attr_name_,_addr_,_mask_,_shift_,_string_) \
DWC_OTG_DEVICE_ATTR_BITFIELD_SHOW(_otg_attr_name_,_addr_,_mask_,_shift_,_string_) \
DEVICE_ATTR(_otg_attr_name_,0444,_otg_attr_name_##_show,NULL);

/*
 * MACROs for defining sysfs attribute for 32-bit registers
 */
#define DWC_OTG_DEVICE_ATTR_REG_SHOW(_otg_attr_name_,_addr_,_string_) \
static ssize_t _otg_attr_name_##_show (struct device *_dev, struct device_attribute *attr, char *buf) \
{ \
        dwc_otg_device_t *otg_dev = _dev->platform_data;\
	uint32_t val; \
	val = dwc_read_reg32 (_addr_); \
	return sprintf (buf, "%s = 0x%08x\n", _string_, val); \
}
#define DWC_OTG_DEVICE_ATTR_REG_STORE(_otg_attr_name_,_addr_,_string_) \
static ssize_t _otg_attr_name_##_store (struct device *_dev, struct device_attribute *attr, const char *buf, size_t count) \
{ \
        dwc_otg_device_t *otg_dev = _dev->platform_data;\
	uint32_t val = simple_strtoul(buf, NULL, 16); \
	dev_dbg(_dev, "Storing Address=0x%08x Val=0x%08x\n", (uint32_t)_addr_, val); \
	dwc_write_reg32(_addr_, val); \
	return count; \
}

#define DWC_OTG_DEVICE_ATTR_REG32_RW(_otg_attr_name_,_addr_,_string_) \
DWC_OTG_DEVICE_ATTR_REG_SHOW(_otg_attr_name_,_addr_,_string_) \
DWC_OTG_DEVICE_ATTR_REG_STORE(_otg_attr_name_,_addr_,_string_) \
DEVICE_ATTR(_otg_attr_name_,0644,_otg_attr_name_##_show,_otg_attr_name_##_store);

#define DWC_OTG_DEVICE_ATTR_REG32_RO(_otg_attr_name_,_addr_,_string_) \
DWC_OTG_DEVICE_ATTR_REG_SHOW(_otg_attr_name_,_addr_,_string_) \
DEVICE_ATTR(_otg_attr_name_,0444,_otg_attr_name_##_show,NULL);


/** @name Functions for Show/Store of Attributes */
/**@{*/

/**
 * Show the register offset of the Register Access.
 */
static ssize_t regoffset_show( struct device *_dev,
							struct device_attribute *attr, char *buf) 
{
        dwc_otg_device_t *otg_dev = _dev->platform_data;
        
	return snprintf(buf, sizeof("0xFFFFFFFF\n")+1,"0x%08x\n", otg_dev->reg_offset);
}

/**
 * Set the register offset for the next Register Access 	Read/Write
 */
static ssize_t regoffset_store( struct device *_dev, struct device_attribute *attr,
							const char *buf, size_t count ) 
{
        dwc_otg_device_t *otg_dev = _dev->platform_data;
	uint32_t offset = simple_strtoul(buf, NULL, 16);
	//dev_dbg(_dev, "Offset=0x%08x\n", offset);
	if (offset < SZ_256K ) {
		otg_dev->reg_offset = offset;
	}
	else {
		dev_err( _dev, "invalid offset\n" );
	}

	return count;
}
DEVICE_ATTR(regoffset, S_IRUGO|S_IWUSR, regoffset_show, regoffset_store);


/**
 * Show the value of the register at the offset in the reg_offset
 * attribute.
 */
static ssize_t regvalue_show( struct device *_dev,
								struct device_attribute *attr, char *buf) 
{
	dwc_otg_device_t *otg_dev = _dev->platform_data;
	uint32_t val;
	volatile uint32_t *addr;
        
	if (otg_dev->reg_offset != 0xFFFFFFFF && 
	    0 != otg_dev->base) {
		/* Calculate the address */
		addr = (uint32_t*)(otg_dev->reg_offset + 
				   (uint8_t*)otg_dev->base);
		//dev_dbg(_dev, "@0x%08x\n", (unsigned)addr); 
		val = dwc_read_reg32( addr );             
		return snprintf(buf, sizeof("Reg@0xFFFFFFFF = 0xFFFFFFFF\n")+1,
				"Reg@0x%06x = 0x%08x\n", 
				otg_dev->reg_offset, val);
	}
	else {
		dev_err(_dev, "Invalid offset (0x%0x)\n", 
			otg_dev->reg_offset);
		return sprintf(buf, "invalid offset\n" );
	}
}

/**
 * Store the value in the register at the offset in the reg_offset
 * attribute.
 * 
 */
static ssize_t regvalue_store( struct device *_dev,
								struct device_attribute *attr, 
								const char *buf,  size_t count ) 
{
        dwc_otg_device_t *otg_dev = _dev->platform_data;
	volatile uint32_t * addr;
	uint32_t val = simple_strtoul(buf, NULL, 16);
	//dev_dbg(_dev, "Offset=0x%08x Val=0x%08x\n", otg_dev->reg_offset, val);
	if (otg_dev->reg_offset != 0xFFFFFFFF && 0 != otg_dev->base) {
		/* Calculate the address */
		addr = (uint32_t*)(otg_dev->reg_offset + 
				   (uint8_t*)otg_dev->base);
		//dev_dbg(_dev, "@0x%08x\n", (unsigned)addr); 
		dwc_write_reg32( addr, val );
	}
	else {
		dev_err(_dev, "Invalid Register Offset (0x%08x)\n", 
			otg_dev->reg_offset);
	}
	return count;
}
DEVICE_ATTR(regvalue,  S_IRUGO|S_IWUSR, regvalue_show, regvalue_store);

/*
 * Attributes
 */
DWC_OTG_DEVICE_ATTR_BITFIELD_RO(mode,&(otg_dev->core_if->core_global_regs->gotgctl),(1<<20),20,"Mode");
DWC_OTG_DEVICE_ATTR_BITFIELD_RW(hnpcapable,&(otg_dev->core_if->core_global_regs->gusbcfg),(1<<9),9,"Mode");
DWC_OTG_DEVICE_ATTR_BITFIELD_RW(srpcapable,&(otg_dev->core_if->core_global_regs->gusbcfg),(1<<8),8,"Mode");

//DWC_OTG_DEVICE_ATTR_BITFIELD_RW(buspower,&(otg_dev->core_if->core_global_regs->gotgctl),(1<<8),8,"Mode");
//DWC_OTG_DEVICE_ATTR_BITFIELD_RW(bussuspend,&(otg_dev->core_if->core_global_regs->gotgctl),(1<<8),8,"Mode");
DWC_OTG_DEVICE_ATTR_BITFIELD_RO(busconnected,otg_dev->core_if->host_if->hprt0,0x01,0,"Bus Connected");

DWC_OTG_DEVICE_ATTR_REG32_RW(gotgctl,&(otg_dev->core_if->core_global_regs->gotgctl),"GOTGCTL");
DWC_OTG_DEVICE_ATTR_REG32_RW(gusbcfg,&(otg_dev->core_if->core_global_regs->gusbcfg),"GUSBCFG");
DWC_OTG_DEVICE_ATTR_REG32_RW(grxfsiz,&(otg_dev->core_if->core_global_regs->grxfsiz),"GRXFSIZ");
DWC_OTG_DEVICE_ATTR_REG32_RW(gnptxfsiz,&(otg_dev->core_if->core_global_regs->gnptxfsiz),"GNPTXFSIZ");
DWC_OTG_DEVICE_ATTR_REG32_RW(gpvndctl,&(otg_dev->core_if->core_global_regs->gpvndctl),"GPVNDCTL");
DWC_OTG_DEVICE_ATTR_REG32_RW(ggpio,&(otg_dev->core_if->core_global_regs->ggpio),"GGPIO");
DWC_OTG_DEVICE_ATTR_REG32_RW(guid,&(otg_dev->core_if->core_global_regs->guid),"GUID");
DWC_OTG_DEVICE_ATTR_REG32_RO(gsnpsid,&(otg_dev->core_if->core_global_regs->gsnpsid),"GSNPSID");
DWC_OTG_DEVICE_ATTR_BITFIELD_RW(devspeed,&(otg_dev->core_if->dev_if->dev_global_regs->dcfg),0x3,0,"Device Speed");
DWC_OTG_DEVICE_ATTR_BITFIELD_RO(enumspeed,&(otg_dev->core_if->dev_if->dev_global_regs->dsts),0x6,1,"Device Enumeration Speed");

DWC_OTG_DEVICE_ATTR_REG32_RO(hptxfsiz,&(otg_dev->core_if->core_global_regs->hptxfsiz),"HPTXFSIZ");
DWC_OTG_DEVICE_ATTR_REG32_RW(hprt0,otg_dev->core_if->host_if->hprt0,"HPRT0");


/**
 * @todo Add code to initiate the HNP.
 */
/**
 * Show the HNP status bit
 */
static ssize_t hnp_show( struct device *_dev,
								struct device_attribute *attr,  char *buf) 
{
        dwc_otg_device_t *otg_dev = _dev->platform_data;
	gotgctl_data_t val;
	val.d32 = dwc_read_reg32 (&(otg_dev->core_if->core_global_regs->gotgctl));
	return sprintf (buf, "HstNegScs = 0x%x\n", val.b.hstnegscs);
}

/**
 * Set the HNP Request bit
 */
static ssize_t hnp_store( struct device *_dev,
								struct device_attribute *attr, 
								const char *buf, size_t count ) 
{
        dwc_otg_device_t *otg_dev = _dev->platform_data;
	uint32_t in = simple_strtoul(buf, NULL, 16);
	uint32_t *addr = (uint32_t *)&(otg_dev->core_if->core_global_regs->gotgctl);
	gotgctl_data_t mem;
	mem.d32 = dwc_read_reg32(addr);
	mem.b.hnpreq = in;
	dev_dbg(_dev, "Storing Address=0x%08x Data=0x%08x\n", (uint32_t)addr, mem.d32);
	dwc_write_reg32(addr, mem.d32);
	return count;
}
DEVICE_ATTR(hnp, 0644, hnp_show, hnp_store);

/**
 * @todo Add code to initiate the SRP.
 */
/**
 * Show the SRP status bit
 */
static ssize_t srp_show( struct device *_dev,
								struct device_attribute *attr,  char *buf) 
{
#ifndef DWC_HOST_ONLY
        dwc_otg_device_t *otg_dev = _dev->platform_data;
	gotgctl_data_t val;
	val.d32 = dwc_read_reg32 (&(otg_dev->core_if->core_global_regs->gotgctl));
	return sprintf (buf, "SesReqScs = 0x%x\n", val.b.sesreqscs);
#else
	return sprintf(buf, "Host Only Mode!\n");
#endif
}



/**
 * Set the SRP Request bit
 */
static ssize_t srp_store( struct device *_dev,
								struct device_attribute *attr, 
								const char *buf, size_t count ) 
{
#ifndef DWC_HOST_ONLY
        dwc_otg_device_t *otg_dev = _dev->platform_data;
	dwc_otg_pcd_initiate_srp(otg_dev->pcd);
#endif
	return count;
}
DEVICE_ATTR(srp, 0644, srp_show, srp_store);

/**
 * @todo Need to do more for power on/off?
 */
/**
 * Show the Bus Power status
 */
static ssize_t buspower_show( struct device *_dev,
								struct device_attribute *attr,  char *buf) 
{
        dwc_otg_device_t *otg_dev = _dev->platform_data;
	hprt0_data_t val;
	val.d32 = dwc_read_reg32 (otg_dev->core_if->host_if->hprt0);
	return sprintf (buf, "Bus Power = 0x%x\n", val.b.prtpwr);
}


/**
 * Set the Bus Power status
 */
static ssize_t buspower_store( struct device *_dev,
								struct device_attribute *attr, 
								const char *buf, size_t count ) 
{
        dwc_otg_device_t *otg_dev = _dev->platform_data;
	uint32_t on = simple_strtoul(buf, NULL, 16);
	uint32_t *addr = (uint32_t *)otg_dev->core_if->host_if->hprt0;
	hprt0_data_t mem;

	mem.d32 = dwc_read_reg32(addr);
	mem.b.prtpwr = on;

	//dev_dbg(_dev, "Storing Address=0x%08x Data=0x%08x\n", (uint32_t)addr, mem.d32);
	dwc_write_reg32(addr, mem.d32);

	return count;
}
DEVICE_ATTR(buspower, 0644, buspower_show, buspower_store);

/**
 * @todo Need to do more for suspend?
 */
/**
 * Show the Bus Suspend status
 */
static ssize_t bussuspend_show( struct device *_dev,
								struct device_attribute *attr,  char *buf) 
{
        dwc_otg_device_t *otg_dev = _dev->platform_data;
	hprt0_data_t val;
	val.d32 = dwc_read_reg32 (otg_dev->core_if->host_if->hprt0);
	return sprintf (buf, "Bus Suspend = 0x%x\n", val.b.prtsusp);
}

/**
 * Set the Bus Suspend status
 */
static ssize_t bussuspend_store( struct device *_dev,
								struct device_attribute *attr, 
								const char *buf, size_t count ) 
{
        dwc_otg_device_t *otg_dev = _dev->platform_data;
	uint32_t in = simple_strtoul(buf, NULL, 16);
	uint32_t *addr = (uint32_t *)otg_dev->core_if->host_if->hprt0;
	hprt0_data_t mem;
	mem.d32 = dwc_read_reg32(addr);
	mem.b.prtsusp = in;
	dev_dbg(_dev, "Storing Address=0x%08x Data=0x%08x\n", (uint32_t)addr, mem.d32);
	dwc_write_reg32(addr, mem.d32);
	return count;
}
DEVICE_ATTR(bussuspend, 0644, bussuspend_show, bussuspend_store);

/**
 * Show the status of Remote Wakeup.
 */
static ssize_t remote_wakeup_show( struct device *_dev, 
										struct device_attribute *attr,char *buf) 
{
#ifndef DWC_HOST_ONLY
        dwc_otg_device_t *otg_dev = _dev->platform_data;
	dctl_data_t val;
	val.d32 = 
                dwc_read_reg32( &otg_dev->core_if->dev_if->dev_global_regs->dctl);
	return sprintf( buf, "Remote Wakeup = %d Enabled = %d\n", 
                        val.b.rmtwkupsig, otg_dev->pcd->remote_wakeup_enable);
#else
	return sprintf(buf, "Host Only Mode!\n");
#endif
}
/**
 * Initiate a remote wakeup of the host.  The Device control register
 * Remote Wakeup Signal bit is written if the PCD Remote wakeup enable
 * flag is set.
 * 
 */
static ssize_t remote_wakeup_store( struct device *_dev, 
						struct device_attribute *attr,
						const char *buf,  size_t count ) 
{
#ifndef DWC_HOST_ONLY
        uint32_t val = simple_strtoul(buf, NULL, 16);        
	dwc_otg_device_t *otg_dev = _dev->platform_data;
	if (val&1) {
		dwc_otg_pcd_remote_wakeup(otg_dev->pcd, 1);
	}
	else {
		dwc_otg_pcd_remote_wakeup(otg_dev->pcd, 0);
	}
#endif
	return count;
}
static DEVICE_ATTR(remote_wakeup,  S_IRUGO|S_IWUSR, remote_wakeup_show, 
            remote_wakeup_store);

/**
 * Dump global registers and either host or device registers (depending on the
 * current mode of the core).
 */
static ssize_t regdump_show( struct device *_dev, 
								struct device_attribute *attr, char *buf) 
{
        dwc_otg_device_t *otg_dev = _dev->platform_data;
        dwc_otg_dump_global_registers( otg_dev->core_if);
        if (dwc_otg_is_host_mode(otg_dev->core_if)) {
                dwc_otg_dump_host_registers( otg_dev->core_if);
        } else {
                dwc_otg_dump_dev_registers( otg_dev->core_if);
        }
   	return sprintf( buf, "Register Dump\n" );
}

DEVICE_ATTR(regdump, S_IRUGO|S_IWUSR, regdump_show, 0);

/**
 * Dump the current hcd state.
 */
static ssize_t hcddump_show( struct device *_dev, 
								struct device_attribute *attr, char *buf) 
{
#ifndef DWC_DEVICE_ONLY
        dwc_otg_device_t *otg_dev = _dev->platform_data;
	dwc_otg_hcd_dump_state(otg_dev->hcd);
#endif
   	return sprintf( buf, "HCD Dump\n" );
}

DEVICE_ATTR(hcddump, S_IRUGO|S_IWUSR, hcddump_show, 0);

/**
 * Dump the average frame remaining at SOF. This can be used to
 * determine average interrupt latency. Frame remaining is also shown for
 * start transfer and two additional sample points.
 */
static ssize_t hcd_frrem_show( struct device *_dev,
								struct device_attribute *attr,  char *buf) 
{
#ifndef DWC_DEVICE_ONLY
        dwc_otg_device_t *otg_dev = _dev->platform_data;
	dwc_otg_hcd_dump_frrem(otg_dev->hcd);
#endif
   	return sprintf( buf, "HCD Dump Frame Remaining\n" );
}

DEVICE_ATTR(hcd_frrem, S_IRUGO|S_IWUSR, hcd_frrem_show, 0);

/**
 * Displays the time required to read the GNPTXFSIZ register many times (the
 * output shows the number of times the register is read).
 */
#define RW_REG_COUNT 10000000
#define MSEC_PER_JIFFIE 1000/HZ	
static ssize_t rd_reg_test_show( struct device *_dev, 
								struct device_attribute *attr, char *buf) 
{
	int i;
	int time;
	int start_jiffies;
        dwc_otg_device_t *otg_dev = _dev->platform_data;

	DWC_PRINT("HZ %d, MSEC_PER_JIFFIE %d, loops_per_jiffy %lu\n",
	       HZ, MSEC_PER_JIFFIE, loops_per_jiffy);
	start_jiffies = jiffies;
	for (i = 0; i < RW_REG_COUNT; i++) {
		dwc_read_reg32(&otg_dev->core_if->core_global_regs->gnptxfsiz);
	}
	time = jiffies - start_jiffies;
   	return sprintf( buf, "Time to read GNPTXFSIZ reg %d times: %d msecs (%d jiffies)\n",
			RW_REG_COUNT, time * MSEC_PER_JIFFIE, time );
}

DEVICE_ATTR(rd_reg_test, S_IRUGO|S_IWUSR, rd_reg_test_show, 0);

/**
 * Displays the time required to write the GNPTXFSIZ register many times (the
 * output shows the number of times the register is written).
 */
static ssize_t wr_reg_test_show( struct device *_dev, 
								struct device_attribute *attr, char *buf) 
{
	int i;
	int time;
	int start_jiffies;
        dwc_otg_device_t *otg_dev = _dev->platform_data;
	uint32_t reg_val;

	DWC_PRINT("HZ %d, MSEC_PER_JIFFIE %d, loops_per_jiffy %lu\n",
	       HZ, MSEC_PER_JIFFIE, loops_per_jiffy);
	reg_val = dwc_read_reg32(&otg_dev->core_if->core_global_regs->gnptxfsiz);
	start_jiffies = jiffies;
	for (i = 0; i < RW_REG_COUNT; i++) {
		dwc_write_reg32(&otg_dev->core_if->core_global_regs->gnptxfsiz, reg_val);
	}
	time = jiffies - start_jiffies;
   	return sprintf( buf, "Time to write GNPTXFSIZ reg %d times: %d msecs (%d jiffies)\n",
			RW_REG_COUNT, time * MSEC_PER_JIFFIE, time);
}

DEVICE_ATTR(wr_reg_test, S_IRUGO|S_IWUSR, wr_reg_test_show, 0);
extern int dwc_debug(dwc_otg_core_if_t *core_if, int flag);

static ssize_t debug_show( struct device *_dev, 
								struct device_attribute *attr, char *buf) 
{
    dwc_otg_device_t *otg_dev = _dev->platform_data;
    dwc_otg_dump_global_registers( otg_dev->core_if);
    if (dwc_otg_is_host_mode(otg_dev->core_if)) {
            dwc_otg_dump_host_registers( otg_dev->core_if);
    } else {
            dwc_otg_dump_dev_registers( otg_dev->core_if);
    }
   	return sprintf( buf, "Register Dump\n" );
}
static ssize_t debug_store( struct device *_dev,
								struct device_attribute *attr, 
								const char *buf, size_t count ) 
{
	uint32_t val = simple_strtoul(buf, NULL, 16);
    dwc_otg_device_t *otg_dev = _dev->platform_data;
    dwc_otg_dump_global_registers( otg_dev->core_if);
    dwc_debug(otg_dev->core_if,val);
   	return count;
}

DEVICE_ATTR(debug, S_IRUGO|S_IWUSR, debug_show, debug_store);


/**@}*/

/**
 * Create the device files
 */
void dwc_otg_attr_create (struct device *dev)
{
	int error;
	error = device_create_file(dev, &dev_attr_regoffset);
	error |= device_create_file(dev, &dev_attr_regvalue);
	error |= device_create_file(dev, &dev_attr_mode);
	error |= device_create_file(dev, &dev_attr_hnpcapable);
	error |= device_create_file(dev, &dev_attr_srpcapable);
	error |= device_create_file(dev, &dev_attr_hnp);
	error |= device_create_file(dev, &dev_attr_srp);
	error |= device_create_file(dev, &dev_attr_buspower);
	error |= device_create_file(dev, &dev_attr_bussuspend);
	error |= device_create_file(dev, &dev_attr_busconnected);
	error |= device_create_file(dev, &dev_attr_gotgctl);
	error |= device_create_file(dev, &dev_attr_gusbcfg);
	error |= device_create_file(dev, &dev_attr_grxfsiz);
	error |= device_create_file(dev, &dev_attr_gnptxfsiz);
	error |= device_create_file(dev, &dev_attr_gpvndctl);
	error |= device_create_file(dev, &dev_attr_ggpio);
	error |= device_create_file(dev, &dev_attr_guid);
	error |= device_create_file(dev, &dev_attr_gsnpsid);
	error |= device_create_file(dev, &dev_attr_devspeed);
	error |= device_create_file(dev, &dev_attr_enumspeed);
	error |= device_create_file(dev, &dev_attr_hptxfsiz);
	error |= device_create_file(dev, &dev_attr_hprt0);
	error |= device_create_file(dev, &dev_attr_remote_wakeup);
	error |= device_create_file(dev, &dev_attr_regdump);
	error |= device_create_file(dev, &dev_attr_hcddump);
	error |= device_create_file(dev, &dev_attr_hcd_frrem);
	error |= device_create_file(dev, &dev_attr_rd_reg_test);
	error |= device_create_file(dev, &dev_attr_wr_reg_test);
	error |= device_create_file(dev, &dev_attr_debug);
	if (error)
		pr_err("DWC_OTG: Creating some device files failed\n");
}

/**
 * Remove the device files
 */
void dwc_otg_attr_remove (struct device *dev)
{
	device_remove_file(dev, &dev_attr_regoffset);
	device_remove_file(dev, &dev_attr_regvalue);
	device_remove_file(dev, &dev_attr_mode);
	device_remove_file(dev, &dev_attr_hnpcapable);
	device_remove_file(dev, &dev_attr_srpcapable);
	device_remove_file(dev, &dev_attr_hnp);
	device_remove_file(dev, &dev_attr_srp);
	device_remove_file(dev, &dev_attr_buspower);
	device_remove_file(dev, &dev_attr_bussuspend);
	device_remove_file(dev, &dev_attr_busconnected);
	device_remove_file(dev, &dev_attr_gotgctl);
	device_remove_file(dev, &dev_attr_gusbcfg);
	device_remove_file(dev, &dev_attr_grxfsiz);
	device_remove_file(dev, &dev_attr_gnptxfsiz);
	device_remove_file(dev, &dev_attr_gpvndctl);
	device_remove_file(dev, &dev_attr_ggpio);
	device_remove_file(dev, &dev_attr_guid);
	device_remove_file(dev, &dev_attr_gsnpsid);
	device_remove_file(dev, &dev_attr_devspeed);
	device_remove_file(dev, &dev_attr_enumspeed);
	device_remove_file(dev, &dev_attr_hptxfsiz);
	device_remove_file(dev, &dev_attr_hprt0);
	device_remove_file(dev, &dev_attr_remote_wakeup);
	device_remove_file(dev, &dev_attr_regdump);
	device_remove_file(dev, &dev_attr_hcddump);
	device_remove_file(dev, &dev_attr_hcd_frrem);
	device_remove_file(dev, &dev_attr_rd_reg_test);
	device_remove_file(dev, &dev_attr_wr_reg_test);
	device_remove_file(dev, &dev_attr_debug);
}
