/*****************************************************************************
* Copyright 2003 - 2008 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

#ifndef CHIPC_INLINE_H
#define CHIPC_INLINE_H

/* ---- Include Files ----------------------------------------------------- */

#include <csp/errno.h>
#include <csp/reg.h>
#include <mach/csp/chipcHw_reg.h>
#include <mach/csp/chipcHw_def.h>

/* ---- Private Constants and Types --------------------------------------- */
typedef enum {
	chipcHw_OPTYPE_BYPASS,	/* Bypass operation */
	chipcHw_OPTYPE_OUTPUT	/* Output operation */
} chipcHw_OPTYPE_e;

/* ---- Public Constants and Types ---------------------------------------- */
/* ---- Public Variable Externs ------------------------------------------- */
/* ---- Public Function Prototypes ---------------------------------------- */
/* ---- Private Function Prototypes --------------------------------------- */
static inline void chipcHw_setClock(chipcHw_CLOCK_e clock,
				    chipcHw_OPTYPE_e type, int mode);

/****************************************************************************/
/**
*  @brief   Get Numeric Chip ID
*
*  This function returns Chip ID that includes the revison number
*
*  @return  Complete numeric Chip ID
*
*/
/****************************************************************************/
static inline uint32_t chipcHw_getChipId(void)
{
	return pChipcHw->ChipId;
}

/****************************************************************************/
/**
*  @brief   Enable Spread Spectrum
*
*  @note chipcHw_Init() must be called earlier
*/
/****************************************************************************/
static inline void chipcHw_enableSpreadSpectrum(void)
{
	if ((pChipcHw->
	     PLLPreDivider & chipcHw_REG_PLL_PREDIVIDER_NDIV_MODE_MASK) !=
	    chipcHw_REG_PLL_PREDIVIDER_NDIV_MODE_INTEGER) {
		ddrcReg_PHY_ADDR_CTL_REGP->ssCfg =
		    (0xFFFF << ddrcReg_PHY_ADDR_SS_CFG_NDIV_AMPLITUDE_SHIFT) |
		    (ddrcReg_PHY_ADDR_SS_CFG_MIN_CYCLE_PER_TICK <<
		     ddrcReg_PHY_ADDR_SS_CFG_CYCLE_PER_TICK_SHIFT);
		ddrcReg_PHY_ADDR_CTL_REGP->ssCtl |=
		    ddrcReg_PHY_ADDR_SS_CTRL_ENABLE;
	}
}

/****************************************************************************/
/**
*  @brief   Disable Spread Spectrum
*
*/
/****************************************************************************/
static inline void chipcHw_disableSpreadSpectrum(void)
{
	ddrcReg_PHY_ADDR_CTL_REGP->ssCtl &= ~ddrcReg_PHY_ADDR_SS_CTRL_ENABLE;
}

/****************************************************************************/
/**
*  @brief   Get Chip Product ID
*
*  This function returns Chip Product ID
*
*  @return  Chip Product ID
*/
/****************************************************************************/
static inline uint32_t chipcHw_getChipProductId(void)
{
	return (pChipcHw->
		 ChipId & chipcHw_REG_CHIPID_BASE_MASK) >>
		chipcHw_REG_CHIPID_BASE_SHIFT;
}

/****************************************************************************/
/**
*  @brief   Get revision number
*
*  This function returns revision number of the chip
*
*  @return  Revision number
*/
/****************************************************************************/
static inline chipcHw_REV_NUMBER_e chipcHw_getChipRevisionNumber(void)
{
	return pChipcHw->ChipId & chipcHw_REG_CHIPID_REV_MASK;
}

/****************************************************************************/
/**
*  @brief   Enables bus interface clock
*
*  Enables  bus interface clock of various device
*
*  @return  void
*
*  @note    use chipcHw_REG_BUS_CLOCK_XXXX for mask
*/
/****************************************************************************/
static inline void chipcHw_busInterfaceClockEnable(uint32_t mask)
{
	reg32_modify_or(&pChipcHw->BusIntfClock, mask);
}

/****************************************************************************/
/**
*  @brief   Disables bus interface clock
*
*  Disables  bus interface clock of various device
*
*  @return  void
*
*  @note    use chipcHw_REG_BUS_CLOCK_XXXX
*/
/****************************************************************************/
static inline void chipcHw_busInterfaceClockDisable(uint32_t mask)
{
	reg32_modify_and(&pChipcHw->BusIntfClock, ~mask);
}

/****************************************************************************/
/**
*  @brief   Get status (enabled/disabled) of bus interface clock
*
*  This function returns the status of devices' bus interface clock
*
*  @return  Bus interface clock
*
*/
/****************************************************************************/
static inline uint32_t chipcHw_getBusInterfaceClockStatus(void)
{
	return pChipcHw->BusIntfClock;
}

/****************************************************************************/
/**
*  @brief   Enables various audio channels
*
*  Enables audio channel
*
*  @return  void
*
*  @note    use chipcHw_REG_AUDIO_CHANNEL_XXXXXX
*/
/****************************************************************************/
static inline void chipcHw_audioChannelEnable(uint32_t mask)
{
	reg32_modify_or(&pChipcHw->AudioEnable, mask);
}

/****************************************************************************/
/**
*  @brief   Disables various audio channels
*
*  Disables audio channel
*
*  @return  void
*
*  @note    use chipcHw_REG_AUDIO_CHANNEL_XXXXXX
*/
/****************************************************************************/
static inline void chipcHw_audioChannelDisable(uint32_t mask)
{
	reg32_modify_and(&pChipcHw->AudioEnable, ~mask);
}

/****************************************************************************/
/**
*  @brief    Soft resets devices
*
*  Soft resets various devices
*
*  @return   void
*
*  @note     use chipcHw_REG_SOFT_RESET_XXXXXX defines
*/
/****************************************************************************/
static inline void chipcHw_softReset(uint64_t mask)
{
	chipcHw_softResetEnable(mask);
	chipcHw_softResetDisable(mask);
}

static inline void chipcHw_softResetDisable(uint64_t mask)
{
	uint32_t ctrl1 = (uint32_t) mask;
	uint32_t ctrl2 = (uint32_t) (mask >> 32);

	/* Deassert module soft reset */
	REG_LOCAL_IRQ_SAVE;
	pChipcHw->SoftReset1 ^= ctrl1;
	pChipcHw->SoftReset2 ^= (ctrl2 & (~chipcHw_REG_SOFT_RESET_UNHOLD_MASK));
	REG_LOCAL_IRQ_RESTORE;
}

static inline void chipcHw_softResetEnable(uint64_t mask)
{
	uint32_t ctrl1 = (uint32_t) mask;
	uint32_t ctrl2 = (uint32_t) (mask >> 32);
	uint32_t unhold = 0;

	REG_LOCAL_IRQ_SAVE;
	pChipcHw->SoftReset1 |= ctrl1;
	/* Mask out unhold request bits */
	pChipcHw->SoftReset2 |= (ctrl2 & (~chipcHw_REG_SOFT_RESET_UNHOLD_MASK));

	/* Process unhold requests */
	if (ctrl2 & chipcHw_REG_SOFT_RESET_VPM_GLOBAL_UNHOLD) {
		unhold = chipcHw_REG_SOFT_RESET_VPM_GLOBAL_HOLD;
	}

	if (ctrl2 & chipcHw_REG_SOFT_RESET_VPM_UNHOLD) {
		unhold |= chipcHw_REG_SOFT_RESET_VPM_HOLD;
	}

	if (ctrl2 & chipcHw_REG_SOFT_RESET_ARM_UNHOLD) {
		unhold |= chipcHw_REG_SOFT_RESET_ARM_HOLD;
	}

	if (unhold) {
		/* Make sure unhold request is effective */
		pChipcHw->SoftReset1 &= ~unhold;
	}
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief    Configures misc CHIP functionality
*
*  Configures CHIP functionality
*
*  @return   void
*
*  @note     use chipcHw_REG_MISC_CTRL_XXXXXX
*/
/****************************************************************************/
static inline void chipcHw_miscControl(uint32_t mask)
{
	reg32_write(&pChipcHw->MiscCtrl, mask);
}

static inline void chipcHw_miscControlDisable(uint32_t mask)
{
	reg32_modify_and(&pChipcHw->MiscCtrl, ~mask);
}

static inline void chipcHw_miscControlEnable(uint32_t mask)
{
	reg32_modify_or(&pChipcHw->MiscCtrl, mask);
}

/****************************************************************************/
/**
*  @brief    Set OTP options
*
*  Set OTP options
*
*  @return   void
*
*  @note     use chipcHw_REG_OTP_XXXXXX
*/
/****************************************************************************/
static inline void chipcHw_setOTPOption(uint64_t mask)
{
	uint32_t ctrl1 = (uint32_t) mask;
	uint32_t ctrl2 = (uint32_t) (mask >> 32);

	reg32_modify_or(&pChipcHw->SoftOTP1, ctrl1);
	reg32_modify_or(&pChipcHw->SoftOTP2, ctrl2);
}

/****************************************************************************/
/**
*  @brief    Get sticky bits
*
*  @return   Sticky bit options of type chipcHw_REG_STICKY_XXXXXX
*
*/
/****************************************************************************/
static inline uint32_t chipcHw_getStickyBits(void)
{
	return pChipcHw->Sticky;
}

/****************************************************************************/
/**
*  @brief    Set sticky bits
*
*  @return   void
*
*  @note     use chipcHw_REG_STICKY_XXXXXX
*/
/****************************************************************************/
static inline void chipcHw_setStickyBits(uint32_t mask)
{
	uint32_t bits = 0;

	REG_LOCAL_IRQ_SAVE;
	if (mask & chipcHw_REG_STICKY_POR_BROM) {
		bits |= chipcHw_REG_STICKY_POR_BROM;
	} else {
		uint32_t sticky;
		sticky = pChipcHw->Sticky;

		if ((mask & chipcHw_REG_STICKY_BOOT_DONE)
		    && (sticky & chipcHw_REG_STICKY_BOOT_DONE) == 0) {
			bits |= chipcHw_REG_STICKY_BOOT_DONE;
		}
		if ((mask & chipcHw_REG_STICKY_GENERAL_1)
		    && (sticky & chipcHw_REG_STICKY_GENERAL_1) == 0) {
			bits |= chipcHw_REG_STICKY_GENERAL_1;
		}
		if ((mask & chipcHw_REG_STICKY_GENERAL_2)
		    && (sticky & chipcHw_REG_STICKY_GENERAL_2) == 0) {
			bits |= chipcHw_REG_STICKY_GENERAL_2;
		}
		if ((mask & chipcHw_REG_STICKY_GENERAL_3)
		    && (sticky & chipcHw_REG_STICKY_GENERAL_3) == 0) {
			bits |= chipcHw_REG_STICKY_GENERAL_3;
		}
		if ((mask & chipcHw_REG_STICKY_GENERAL_4)
		    && (sticky & chipcHw_REG_STICKY_GENERAL_4) == 0) {
			bits |= chipcHw_REG_STICKY_GENERAL_4;
		}
		if ((mask & chipcHw_REG_STICKY_GENERAL_5)
		    && (sticky & chipcHw_REG_STICKY_GENERAL_5) == 0) {
			bits |= chipcHw_REG_STICKY_GENERAL_5;
		}
	}
	pChipcHw->Sticky = bits;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief    Clear sticky bits
*
*  @return   void
*
*  @note     use chipcHw_REG_STICKY_XXXXXX
*/
/****************************************************************************/
static inline void chipcHw_clearStickyBits(uint32_t mask)
{
	uint32_t bits = 0;

	REG_LOCAL_IRQ_SAVE;
	if (mask &
	    (chipcHw_REG_STICKY_BOOT_DONE | chipcHw_REG_STICKY_GENERAL_1 |
	     chipcHw_REG_STICKY_GENERAL_2 | chipcHw_REG_STICKY_GENERAL_3 |
	     chipcHw_REG_STICKY_GENERAL_4 | chipcHw_REG_STICKY_GENERAL_5)) {
		uint32_t sticky = pChipcHw->Sticky;

		if ((mask & chipcHw_REG_STICKY_BOOT_DONE)
		    && (sticky & chipcHw_REG_STICKY_BOOT_DONE)) {
			bits = chipcHw_REG_STICKY_BOOT_DONE;
			mask &= ~chipcHw_REG_STICKY_BOOT_DONE;
		}
		if ((mask & chipcHw_REG_STICKY_GENERAL_1)
		    && (sticky & chipcHw_REG_STICKY_GENERAL_1)) {
			bits |= chipcHw_REG_STICKY_GENERAL_1;
			mask &= ~chipcHw_REG_STICKY_GENERAL_1;
		}
		if ((mask & chipcHw_REG_STICKY_GENERAL_2)
		    && (sticky & chipcHw_REG_STICKY_GENERAL_2)) {
			bits |= chipcHw_REG_STICKY_GENERAL_2;
			mask &= ~chipcHw_REG_STICKY_GENERAL_2;
		}
		if ((mask & chipcHw_REG_STICKY_GENERAL_3)
		    && (sticky & chipcHw_REG_STICKY_GENERAL_3)) {
			bits |= chipcHw_REG_STICKY_GENERAL_3;
			mask &= ~chipcHw_REG_STICKY_GENERAL_3;
		}
		if ((mask & chipcHw_REG_STICKY_GENERAL_4)
		    && (sticky & chipcHw_REG_STICKY_GENERAL_4)) {
			bits |= chipcHw_REG_STICKY_GENERAL_4;
			mask &= ~chipcHw_REG_STICKY_GENERAL_4;
		}
		if ((mask & chipcHw_REG_STICKY_GENERAL_5)
		    && (sticky & chipcHw_REG_STICKY_GENERAL_5)) {
			bits |= chipcHw_REG_STICKY_GENERAL_5;
			mask &= ~chipcHw_REG_STICKY_GENERAL_5;
		}
	}
	pChipcHw->Sticky = bits | mask;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief    Get software strap value
*
*  Retrieves software strap value
*
*  @return   Software strap value
*
*/
/****************************************************************************/
static inline uint32_t chipcHw_getSoftStraps(void)
{
	return pChipcHw->SoftStraps;
}

/****************************************************************************/
/**
*  @brief    Set software override strap options
*
*  set software override strap options
*
*  @return   nothing
*
*/
/****************************************************************************/
static inline void chipcHw_setSoftStraps(uint32_t strapOptions)
{
	reg32_write(&pChipcHw->SoftStraps, strapOptions);
}

/****************************************************************************/
/**
*  @brief   Get Pin Strap Options
*
*  This function returns the raw boot strap options
*
*  @return  strap options
*
*/
/****************************************************************************/
static inline uint32_t chipcHw_getPinStraps(void)
{
	return pChipcHw->PinStraps;
}

/****************************************************************************/
/**
*  @brief   Get Valid Strap Options
*
*  This function returns the valid raw boot strap options
*
*  @return  strap options
*
*/
/****************************************************************************/
static inline uint32_t chipcHw_getValidStraps(void)
{
	uint32_t softStraps;

	/*
	 ** Always return the SoftStraps - bootROM calls chipcHw_initValidStraps
	 ** which copies HW straps to soft straps if there is no override
	 */
	softStraps = chipcHw_getSoftStraps();

	return softStraps;
}

/****************************************************************************/
/**
*  @brief    Initialize valid pin strap options
*
*  Retrieves valid pin strap options by copying HW strap options to soft register
*  (if chipcHw_STRAPS_SOFT_OVERRIDE not set)
*
*  @return   nothing
*
*/
/****************************************************************************/
static inline void chipcHw_initValidStraps(void)
{
	uint32_t softStraps;

	REG_LOCAL_IRQ_SAVE;
	softStraps = chipcHw_getSoftStraps();

	if ((softStraps & chipcHw_STRAPS_SOFT_OVERRIDE) == 0) {
		/* Copy HW straps to software straps */
		chipcHw_setSoftStraps(chipcHw_getPinStraps());
	}
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   Get boot device
*
*  This function returns the device type used in booting the system
*
*  @return  Boot device of type chipcHw_BOOT_DEVICE
*
*/
/****************************************************************************/
static inline chipcHw_BOOT_DEVICE_e chipcHw_getBootDevice(void)
{
	return chipcHw_getValidStraps() & chipcHw_STRAPS_BOOT_DEVICE_MASK;
}

/****************************************************************************/
/**
*  @brief   Get boot mode
*
*  This function returns the way the system was booted
*
*  @return  Boot mode of type chipcHw_BOOT_MODE
*
*/
/****************************************************************************/
static inline chipcHw_BOOT_MODE_e chipcHw_getBootMode(void)
{
	return chipcHw_getValidStraps() & chipcHw_STRAPS_BOOT_MODE_MASK;
}

/****************************************************************************/
/**
*  @brief   Get NAND flash page size
*
*  This function returns the NAND device page size
*
*  @return  Boot NAND device page size
*
*/
/****************************************************************************/
static inline chipcHw_NAND_PAGESIZE_e chipcHw_getNandPageSize(void)
{
	return chipcHw_getValidStraps() & chipcHw_STRAPS_NAND_PAGESIZE_MASK;
}

/****************************************************************************/
/**
*  @brief   Get NAND flash address cycle configuration
*
*  This function returns the NAND flash address cycle configuration
*
*  @return  0 = Do not extra address cycle, 1 = Add extra cycle
*
*/
/****************************************************************************/
static inline int chipcHw_getNandExtraCycle(void)
{
	if (chipcHw_getValidStraps() & chipcHw_STRAPS_NAND_EXTRA_CYCLE) {
		return 1;
	} else {
		return 0;
	}
}

/****************************************************************************/
/**
*  @brief   Activates PIF interface
*
*  This function activates PIF interface by taking control of LCD pins
*
*  @note
*       When activated, LCD pins will be defined as follows for PIF operation
*
*       CLD[17:0]  = pif_data[17:0]
*       CLD[23:18] = pif_address[5:0]
*       CLPOWER    = pif_wr_str
*       CLCP       = pif_rd_str
*       CLAC       = pif_hat1
*       CLFP       = pif_hrdy1
*       CLLP       = pif_hat2
*       GPIO[42]   = pif_hrdy2
*
*       In PIF mode, "pif_hrdy2" overrides other shared function for GPIO[42] pin
*
*/
/****************************************************************************/
static inline void chipcHw_activatePifInterface(void)
{
	reg32_write(&pChipcHw->LcdPifMode, chipcHw_REG_PIF_PIN_ENABLE);
}

/****************************************************************************/
/**
*  @brief   Activates LCD interface
*
*  This function activates LCD interface
*
*  @note
*       When activated, LCD pins will be defined as follows
*
*       CLD[17:0]  = LCD data
*       CLD[23:18] = LCD data
*       CLPOWER    = LCD power
*       CLCP       =
*       CLAC       = LCD ack
*       CLFP       =
*       CLLP       =
*/
/****************************************************************************/
static inline void chipcHw_activateLcdInterface(void)
{
	reg32_write(&pChipcHw->LcdPifMode, chipcHw_REG_LCD_PIN_ENABLE);
}

/****************************************************************************/
/**
*  @brief   Deactivates PIF/LCD interface
*
*  This function deactivates PIF/LCD interface
*
*  @note
*       When deactivated LCD pins will be in rti-stated
*
*/
/****************************************************************************/
static inline void chipcHw_deactivatePifLcdInterface(void)
{
	reg32_write(&pChipcHw->LcdPifMode, 0);
}

/****************************************************************************/
/**
*  @brief   Select GE2
*
*  This function select GE2 as the graphic engine
*
*/
/****************************************************************************/
static inline void chipcHw_selectGE2(void)
{
	reg32_modify_and(&pChipcHw->MiscCtrl, ~chipcHw_REG_MISC_CTRL_GE_SEL);
}

/****************************************************************************/
/**
*  @brief   Select GE3
*
*  This function select GE3 as the graphic engine
*
*/
/****************************************************************************/
static inline void chipcHw_selectGE3(void)
{
	reg32_modify_or(&pChipcHw->MiscCtrl, chipcHw_REG_MISC_CTRL_GE_SEL);
}

/****************************************************************************/
/**
*  @brief   Get to know the configuration of GPIO pin
*
*/
/****************************************************************************/
static inline chipcHw_GPIO_FUNCTION_e chipcHw_getGpioPinFunction(int pin)
{
	return (*((uint32_t *) chipcHw_REG_GPIO_MUX(pin)) &
		(chipcHw_REG_GPIO_MUX_MASK <<
		 chipcHw_REG_GPIO_MUX_POSITION(pin))) >>
	    chipcHw_REG_GPIO_MUX_POSITION(pin);
}

/****************************************************************************/
/**
*  @brief   Configure GPIO pin function
*
*/
/****************************************************************************/
static inline void chipcHw_setGpioPinFunction(int pin,
					      chipcHw_GPIO_FUNCTION_e func)
{
	REG_LOCAL_IRQ_SAVE;
	*((uint32_t *) chipcHw_REG_GPIO_MUX(pin)) &=
	    ~(chipcHw_REG_GPIO_MUX_MASK << chipcHw_REG_GPIO_MUX_POSITION(pin));
	*((uint32_t *) chipcHw_REG_GPIO_MUX(pin)) |=
	    func << chipcHw_REG_GPIO_MUX_POSITION(pin);
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   Set Pin slew rate
*
*  This function sets the slew of individual pin
*
*/
/****************************************************************************/
static inline void chipcHw_setPinSlewRate(uint32_t pin,
					  chipcHw_PIN_SLEW_RATE_e slewRate)
{
	REG_LOCAL_IRQ_SAVE;
	*((uint32_t *) chipcHw_REG_SLEW_RATE(pin)) &=
	    ~(chipcHw_REG_SLEW_RATE_MASK <<
	      chipcHw_REG_SLEW_RATE_POSITION(pin));
	*((uint32_t *) chipcHw_REG_SLEW_RATE(pin)) |=
	    (uint32_t) slewRate << chipcHw_REG_SLEW_RATE_POSITION(pin);
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   Set Pin output drive current
*
*  This function sets output drive current of individual pin
*
*  Note: Avoid the use of the word 'current' since linux headers define this
*        to be the current task.
*/
/****************************************************************************/
static inline void chipcHw_setPinOutputCurrent(uint32_t pin,
					       chipcHw_PIN_CURRENT_STRENGTH_e
					       curr)
{
	REG_LOCAL_IRQ_SAVE;
	*((uint32_t *) chipcHw_REG_CURRENT(pin)) &=
	    ~(chipcHw_REG_CURRENT_MASK << chipcHw_REG_CURRENT_POSITION(pin));
	*((uint32_t *) chipcHw_REG_CURRENT(pin)) |=
	    (uint32_t) curr << chipcHw_REG_CURRENT_POSITION(pin);
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   Set Pin pullup register
*
*  This function sets pullup register of individual pin
*
*/
/****************************************************************************/
static inline void chipcHw_setPinPullup(uint32_t pin, chipcHw_PIN_PULL_e pullup)
{
	REG_LOCAL_IRQ_SAVE;
	*((uint32_t *) chipcHw_REG_PULLUP(pin)) &=
	    ~(chipcHw_REG_PULLUP_MASK << chipcHw_REG_PULLUP_POSITION(pin));
	*((uint32_t *) chipcHw_REG_PULLUP(pin)) |=
	    (uint32_t) pullup << chipcHw_REG_PULLUP_POSITION(pin);
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   Set Pin input type
*
*  This function sets input type of individual pin
*
*/
/****************************************************************************/
static inline void chipcHw_setPinInputType(uint32_t pin,
					   chipcHw_PIN_INPUTTYPE_e inputType)
{
	REG_LOCAL_IRQ_SAVE;
	*((uint32_t *) chipcHw_REG_INPUTTYPE(pin)) &=
	    ~(chipcHw_REG_INPUTTYPE_MASK <<
	      chipcHw_REG_INPUTTYPE_POSITION(pin));
	*((uint32_t *) chipcHw_REG_INPUTTYPE(pin)) |=
	    (uint32_t) inputType << chipcHw_REG_INPUTTYPE_POSITION(pin);
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   Power up the USB PHY
*
*  This function powers up the USB PHY
*
*/
/****************************************************************************/
static inline void chipcHw_powerUpUsbPhy(void)
{
	reg32_modify_and(&pChipcHw->MiscCtrl,
			 chipcHw_REG_MISC_CTRL_USB_POWERON);
}

/****************************************************************************/
/**
*  @brief   Power down the USB PHY
*
*  This function powers down the USB PHY
*
*/
/****************************************************************************/
static inline void chipcHw_powerDownUsbPhy(void)
{
	reg32_modify_or(&pChipcHw->MiscCtrl,
			chipcHw_REG_MISC_CTRL_USB_POWEROFF);
}

/****************************************************************************/
/**
*  @brief   Set the 2nd USB as host
*
*  This function sets the 2nd USB as host
*
*/
/****************************************************************************/
static inline void chipcHw_setUsbHost(void)
{
	reg32_modify_or(&pChipcHw->MiscCtrl,
			chipcHw_REG_MISC_CTRL_USB_MODE_HOST);
}

/****************************************************************************/
/**
*  @brief   Set the 2nd USB as device
*
*  This function sets the 2nd USB as device
*
*/
/****************************************************************************/
static inline void chipcHw_setUsbDevice(void)
{
	reg32_modify_and(&pChipcHw->MiscCtrl,
			 chipcHw_REG_MISC_CTRL_USB_MODE_DEVICE);
}

/****************************************************************************/
/**
*  @brief   Lower layer function to enable/disable a clock of a certain device
*
*  This function enables/disables a core clock
*
*/
/****************************************************************************/
static inline void chipcHw_setClock(chipcHw_CLOCK_e clock,
				    chipcHw_OPTYPE_e type, int mode)
{
	volatile uint32_t *pPLLReg = (uint32_t *) 0x0;
	volatile uint32_t *pClockCtrl = (uint32_t *) 0x0;

	switch (clock) {
	case chipcHw_CLOCK_DDR:
		pPLLReg = &pChipcHw->DDRClock;
		break;
	case chipcHw_CLOCK_ARM:
		pPLLReg = &pChipcHw->ARMClock;
		break;
	case chipcHw_CLOCK_ESW:
		pPLLReg = &pChipcHw->ESWClock;
		break;
	case chipcHw_CLOCK_VPM:
		pPLLReg = &pChipcHw->VPMClock;
		break;
	case chipcHw_CLOCK_ESW125:
		pPLLReg = &pChipcHw->ESW125Clock;
		break;
	case chipcHw_CLOCK_UART:
		pPLLReg = &pChipcHw->UARTClock;
		break;
	case chipcHw_CLOCK_SDIO0:
		pPLLReg = &pChipcHw->SDIO0Clock;
		break;
	case chipcHw_CLOCK_SDIO1:
		pPLLReg = &pChipcHw->SDIO1Clock;
		break;
	case chipcHw_CLOCK_SPI:
		pPLLReg = &pChipcHw->SPIClock;
		break;
	case chipcHw_CLOCK_ETM:
		pPLLReg = &pChipcHw->ETMClock;
		break;
	case chipcHw_CLOCK_USB:
		pPLLReg = &pChipcHw->USBClock;
		if (type == chipcHw_OPTYPE_OUTPUT) {
			if (mode) {
				reg32_modify_and(pPLLReg,
						 ~chipcHw_REG_PLL_CLOCK_POWER_DOWN);
			} else {
				reg32_modify_or(pPLLReg,
						chipcHw_REG_PLL_CLOCK_POWER_DOWN);
			}
		}
		break;
	case chipcHw_CLOCK_LCD:
		pPLLReg = &pChipcHw->LCDClock;
		if (type == chipcHw_OPTYPE_OUTPUT) {
			if (mode) {
				reg32_modify_and(pPLLReg,
						 ~chipcHw_REG_PLL_CLOCK_POWER_DOWN);
			} else {
				reg32_modify_or(pPLLReg,
						chipcHw_REG_PLL_CLOCK_POWER_DOWN);
			}
		}
		break;
	case chipcHw_CLOCK_APM:
		pPLLReg = &pChipcHw->APMClock;
		if (type == chipcHw_OPTYPE_OUTPUT) {
			if (mode) {
				reg32_modify_and(pPLLReg,
						 ~chipcHw_REG_PLL_CLOCK_POWER_DOWN);
			} else {
				reg32_modify_or(pPLLReg,
						chipcHw_REG_PLL_CLOCK_POWER_DOWN);
			}
		}
		break;
	case chipcHw_CLOCK_BUS:
		pClockCtrl = &pChipcHw->ACLKClock;
		break;
	case chipcHw_CLOCK_OTP:
		pClockCtrl = &pChipcHw->OTPClock;
		break;
	case chipcHw_CLOCK_I2C:
		pClockCtrl = &pChipcHw->I2CClock;
		break;
	case chipcHw_CLOCK_I2S0:
		pClockCtrl = &pChipcHw->I2S0Clock;
		break;
	case chipcHw_CLOCK_RTBUS:
		pClockCtrl = &pChipcHw->RTBUSClock;
		break;
	case chipcHw_CLOCK_APM100:
		pClockCtrl = &pChipcHw->APM100Clock;
		break;
	case chipcHw_CLOCK_TSC:
		pClockCtrl = &pChipcHw->TSCClock;
		break;
	case chipcHw_CLOCK_LED:
		pClockCtrl = &pChipcHw->LEDClock;
		break;
	case chipcHw_CLOCK_I2S1:
		pClockCtrl = &pChipcHw->I2S1Clock;
		break;
	}

	if (pPLLReg) {
		switch (type) {
		case chipcHw_OPTYPE_OUTPUT:
			/* PLL clock output enable/disable */
			if (mode) {
				if (clock == chipcHw_CLOCK_DDR) {
					/* DDR clock enable is inverted */
					reg32_modify_and(pPLLReg,
							 ~chipcHw_REG_PLL_CLOCK_OUTPUT_ENABLE);
				} else {
					reg32_modify_or(pPLLReg,
							chipcHw_REG_PLL_CLOCK_OUTPUT_ENABLE);
				}
			} else {
				if (clock == chipcHw_CLOCK_DDR) {
					/* DDR clock disable is inverted */
					reg32_modify_or(pPLLReg,
							chipcHw_REG_PLL_CLOCK_OUTPUT_ENABLE);
				} else {
					reg32_modify_and(pPLLReg,
							 ~chipcHw_REG_PLL_CLOCK_OUTPUT_ENABLE);
				}
			}
			break;
		case chipcHw_OPTYPE_BYPASS:
			/* PLL clock bypass enable/disable */
			if (mode) {
				reg32_modify_or(pPLLReg,
						chipcHw_REG_PLL_CLOCK_BYPASS_SELECT);
			} else {
				reg32_modify_and(pPLLReg,
						 ~chipcHw_REG_PLL_CLOCK_BYPASS_SELECT);
			}
			break;
		}
	} else if (pClockCtrl) {
		switch (type) {
		case chipcHw_OPTYPE_OUTPUT:
			if (mode) {
				reg32_modify_or(pClockCtrl,
						chipcHw_REG_DIV_CLOCK_OUTPUT_ENABLE);
			} else {
				reg32_modify_and(pClockCtrl,
						 ~chipcHw_REG_DIV_CLOCK_OUTPUT_ENABLE);
			}
			break;
		case chipcHw_OPTYPE_BYPASS:
			if (mode) {
				reg32_modify_or(pClockCtrl,
						chipcHw_REG_DIV_CLOCK_BYPASS_SELECT);
			} else {
				reg32_modify_and(pClockCtrl,
						 ~chipcHw_REG_DIV_CLOCK_BYPASS_SELECT);
			}
			break;
		}
	}
}

/****************************************************************************/
/**
*  @brief   Disables a core clock of a certain device
*
*  This function disables a core clock
*
*  @note    no change in power consumption
*/
/****************************************************************************/
static inline void chipcHw_setClockDisable(chipcHw_CLOCK_e clock)
{

	/* Disable output of the clock */
	chipcHw_setClock(clock, chipcHw_OPTYPE_OUTPUT, 0);
}

/****************************************************************************/
/**
*  @brief   Enable a core clock of a certain device
*
*  This function enables a core clock
*
*  @note    no change in power consumption
*/
/****************************************************************************/
static inline void chipcHw_setClockEnable(chipcHw_CLOCK_e clock)
{

	/* Enable output of the clock */
	chipcHw_setClock(clock, chipcHw_OPTYPE_OUTPUT, 1);
}

/****************************************************************************/
/**
*  @brief   Enables bypass clock of a certain device
*
*  This function enables bypass clock
*
*  @note    Doesnot affect the bus interface clock
*/
/****************************************************************************/
static inline void chipcHw_bypassClockEnable(chipcHw_CLOCK_e clock)
{
	/* Enable bypass clock */
	chipcHw_setClock(clock, chipcHw_OPTYPE_BYPASS, 1);
}

/****************************************************************************/
/**
*  @brief   Disabled bypass clock of a certain device
*
*  This function disables bypass clock
*
*  @note    Doesnot affect the bus interface clock
*/
/****************************************************************************/
static inline void chipcHw_bypassClockDisable(chipcHw_CLOCK_e clock)
{
	/* Disable bypass clock */
	chipcHw_setClock(clock, chipcHw_OPTYPE_BYPASS, 0);

}

/****************************************************************************/
/**  @brief Checks if software strap is enabled
 *
 *   @return 1 : When enable
 *           0 : When disable
 */
/****************************************************************************/
static inline int chipcHw_isSoftwareStrapsEnable(void)
{
	return pChipcHw->SoftStraps & 0x00000001;
}

/****************************************************************************/
/**  @brief Enable software strap
 */
/****************************************************************************/
static inline void chipcHw_softwareStrapsEnable(void)
{
	reg32_modify_or(&pChipcHw->SoftStraps, 0x00000001);
}

/****************************************************************************/
/**  @brief Disable software strap
 */
/****************************************************************************/
static inline void chipcHw_softwareStrapsDisable(void)
{
	reg32_modify_and(&pChipcHw->SoftStraps, (~0x00000001));
}

/****************************************************************************/
/**  @brief PLL test enable
 */
/****************************************************************************/
static inline void chipcHw_pllTestEnable(void)
{
	reg32_modify_or(&pChipcHw->PLLConfig,
			chipcHw_REG_PLL_CONFIG_TEST_ENABLE);
}

/****************************************************************************/
/**  @brief PLL2 test enable
 */
/****************************************************************************/
static inline void chipcHw_pll2TestEnable(void)
{
	reg32_modify_or(&pChipcHw->PLLConfig2,
			chipcHw_REG_PLL_CONFIG_TEST_ENABLE);
}

/****************************************************************************/
/**  @brief PLL test disable
 */
/****************************************************************************/
static inline void chipcHw_pllTestDisable(void)
{
	reg32_modify_and(&pChipcHw->PLLConfig,
			 ~chipcHw_REG_PLL_CONFIG_TEST_ENABLE);
}

/****************************************************************************/
/**  @brief PLL2 test disable
 */
/****************************************************************************/
static inline void chipcHw_pll2TestDisable(void)
{
	reg32_modify_and(&pChipcHw->PLLConfig2,
			 ~chipcHw_REG_PLL_CONFIG_TEST_ENABLE);
}

/****************************************************************************/
/**  @brief Get PLL test status
 */
/****************************************************************************/
static inline int chipcHw_isPllTestEnable(void)
{
	return pChipcHw->PLLConfig & chipcHw_REG_PLL_CONFIG_TEST_ENABLE;
}

/****************************************************************************/
/**  @brief Get PLL2 test status
 */
/****************************************************************************/
static inline int chipcHw_isPll2TestEnable(void)
{
	return pChipcHw->PLLConfig2 & chipcHw_REG_PLL_CONFIG_TEST_ENABLE;
}

/****************************************************************************/
/**  @brief PLL test select
 */
/****************************************************************************/
static inline void chipcHw_pllTestSelect(uint32_t val)
{
	REG_LOCAL_IRQ_SAVE;
	pChipcHw->PLLConfig &= ~chipcHw_REG_PLL_CONFIG_TEST_SELECT_MASK;
	pChipcHw->PLLConfig |=
	    (val) << chipcHw_REG_PLL_CONFIG_TEST_SELECT_SHIFT;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**  @brief PLL2 test select
 */
/****************************************************************************/
static inline void chipcHw_pll2TestSelect(uint32_t val)
{

	REG_LOCAL_IRQ_SAVE;
	pChipcHw->PLLConfig2 &= ~chipcHw_REG_PLL_CONFIG_TEST_SELECT_MASK;
	pChipcHw->PLLConfig2 |=
	    (val) << chipcHw_REG_PLL_CONFIG_TEST_SELECT_SHIFT;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**  @brief Get PLL test selected option
 */
/****************************************************************************/
static inline uint8_t chipcHw_getPllTestSelected(void)
{
	return (uint8_t) ((pChipcHw->
			   PLLConfig & chipcHw_REG_PLL_CONFIG_TEST_SELECT_MASK)
			  >> chipcHw_REG_PLL_CONFIG_TEST_SELECT_SHIFT);
}

/****************************************************************************/
/**  @brief Get PLL2 test selected option
 */
/****************************************************************************/
static inline uint8_t chipcHw_getPll2TestSelected(void)
{
	return (uint8_t) ((pChipcHw->
			   PLLConfig2 & chipcHw_REG_PLL_CONFIG_TEST_SELECT_MASK)
			  >> chipcHw_REG_PLL_CONFIG_TEST_SELECT_SHIFT);
}

/****************************************************************************/
/**
*  @brief  Disable the PLL1
*
*/
/****************************************************************************/
static inline void chipcHw_pll1Disable(void)
{
	REG_LOCAL_IRQ_SAVE;
	pChipcHw->PLLConfig |= chipcHw_REG_PLL_CONFIG_POWER_DOWN;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief  Disable the PLL2
*
*/
/****************************************************************************/
static inline void chipcHw_pll2Disable(void)
{
	REG_LOCAL_IRQ_SAVE;
	pChipcHw->PLLConfig2 |= chipcHw_REG_PLL_CONFIG_POWER_DOWN;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   Enables DDR SW phase alignment interrupt
*/
/****************************************************************************/
static inline void chipcHw_ddrPhaseAlignInterruptEnable(void)
{
	REG_LOCAL_IRQ_SAVE;
	pChipcHw->Spare1 |= chipcHw_REG_SPARE1_DDR_PHASE_INTR_ENABLE;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   Disables DDR SW phase alignment interrupt
*/
/****************************************************************************/
static inline void chipcHw_ddrPhaseAlignInterruptDisable(void)
{
	REG_LOCAL_IRQ_SAVE;
	pChipcHw->Spare1 &= ~chipcHw_REG_SPARE1_DDR_PHASE_INTR_ENABLE;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   Set VPM SW phase alignment interrupt mode
*
*  This function sets VPM phase alignment interrupt
*/
/****************************************************************************/
static inline void
chipcHw_vpmPhaseAlignInterruptMode(chipcHw_VPM_HW_PHASE_INTR_e mode)
{
	REG_LOCAL_IRQ_SAVE;
	if (mode == chipcHw_VPM_HW_PHASE_INTR_DISABLE) {
		pChipcHw->Spare1 &= ~chipcHw_REG_SPARE1_VPM_PHASE_INTR_ENABLE;
	} else {
		pChipcHw->Spare1 |= chipcHw_REG_SPARE1_VPM_PHASE_INTR_ENABLE;
	}
	pChipcHw->VPMPhaseCtrl2 =
	    (pChipcHw->
	     VPMPhaseCtrl2 & ~(chipcHw_REG_VPM_INTR_SELECT_MASK <<
			       chipcHw_REG_VPM_INTR_SELECT_SHIFT)) | mode;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   Enable DDR phase alignment in software
*
*/
/****************************************************************************/
static inline void chipcHw_ddrSwPhaseAlignEnable(void)
{
	REG_LOCAL_IRQ_SAVE;
	pChipcHw->DDRPhaseCtrl1 |= chipcHw_REG_DDR_SW_PHASE_CTRL_ENABLE;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   Disable DDR phase alignment in software
*
*/
/****************************************************************************/
static inline void chipcHw_ddrSwPhaseAlignDisable(void)
{
	REG_LOCAL_IRQ_SAVE;
	pChipcHw->DDRPhaseCtrl1 &= ~chipcHw_REG_DDR_SW_PHASE_CTRL_ENABLE;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   Enable DDR phase alignment in hardware
*
*/
/****************************************************************************/
static inline void chipcHw_ddrHwPhaseAlignEnable(void)
{
	REG_LOCAL_IRQ_SAVE;
	pChipcHw->DDRPhaseCtrl1 |= chipcHw_REG_DDR_HW_PHASE_CTRL_ENABLE;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   Disable DDR phase alignment in hardware
*
*/
/****************************************************************************/
static inline void chipcHw_ddrHwPhaseAlignDisable(void)
{
	REG_LOCAL_IRQ_SAVE;
	pChipcHw->DDRPhaseCtrl1 &= ~chipcHw_REG_DDR_HW_PHASE_CTRL_ENABLE;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   Enable VPM phase alignment in software
*
*/
/****************************************************************************/
static inline void chipcHw_vpmSwPhaseAlignEnable(void)
{
	REG_LOCAL_IRQ_SAVE;
	pChipcHw->VPMPhaseCtrl1 |= chipcHw_REG_VPM_SW_PHASE_CTRL_ENABLE;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   Disable VPM phase alignment in software
*
*/
/****************************************************************************/
static inline void chipcHw_vpmSwPhaseAlignDisable(void)
{
	REG_LOCAL_IRQ_SAVE;
	pChipcHw->VPMPhaseCtrl1 &= ~chipcHw_REG_VPM_SW_PHASE_CTRL_ENABLE;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   Enable VPM phase alignment in hardware
*
*/
/****************************************************************************/
static inline void chipcHw_vpmHwPhaseAlignEnable(void)
{
	REG_LOCAL_IRQ_SAVE;
	pChipcHw->VPMPhaseCtrl1 |= chipcHw_REG_VPM_HW_PHASE_CTRL_ENABLE;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   Disable VPM phase alignment in hardware
*
*/
/****************************************************************************/
static inline void chipcHw_vpmHwPhaseAlignDisable(void)
{
	REG_LOCAL_IRQ_SAVE;
	pChipcHw->VPMPhaseCtrl1 &= ~chipcHw_REG_VPM_HW_PHASE_CTRL_ENABLE;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   Set DDR phase alignment margin in hardware
*
*/
/****************************************************************************/
static inline void
chipcHw_setDdrHwPhaseAlignMargin(chipcHw_DDR_HW_PHASE_MARGIN_e margin)
{
	uint32_t ge = 0;
	uint32_t le = 0;

	switch (margin) {
	case chipcHw_DDR_HW_PHASE_MARGIN_STRICT:
		ge = 0x0F;
		le = 0x0F;
		break;
	case chipcHw_DDR_HW_PHASE_MARGIN_MEDIUM:
		ge = 0x03;
		le = 0x3F;
		break;
	case chipcHw_DDR_HW_PHASE_MARGIN_WIDE:
		ge = 0x01;
		le = 0x7F;
		break;
	}

	{
		REG_LOCAL_IRQ_SAVE;

		pChipcHw->DDRPhaseCtrl1 &=
		    ~((chipcHw_REG_DDR_PHASE_VALUE_GE_MASK <<
		       chipcHw_REG_DDR_PHASE_VALUE_GE_SHIFT)
		      || (chipcHw_REG_DDR_PHASE_VALUE_LE_MASK <<
			  chipcHw_REG_DDR_PHASE_VALUE_LE_SHIFT));

		pChipcHw->DDRPhaseCtrl1 |=
		    ((ge << chipcHw_REG_DDR_PHASE_VALUE_GE_SHIFT)
		     || (le << chipcHw_REG_DDR_PHASE_VALUE_LE_SHIFT));

		REG_LOCAL_IRQ_RESTORE;
	}
}

/****************************************************************************/
/**
*  @brief   Set VPM phase alignment margin in hardware
*
*/
/****************************************************************************/
static inline void
chipcHw_setVpmHwPhaseAlignMargin(chipcHw_VPM_HW_PHASE_MARGIN_e margin)
{
	uint32_t ge = 0;
	uint32_t le = 0;

	switch (margin) {
	case chipcHw_VPM_HW_PHASE_MARGIN_STRICT:
		ge = 0x0F;
		le = 0x0F;
		break;
	case chipcHw_VPM_HW_PHASE_MARGIN_MEDIUM:
		ge = 0x03;
		le = 0x3F;
		break;
	case chipcHw_VPM_HW_PHASE_MARGIN_WIDE:
		ge = 0x01;
		le = 0x7F;
		break;
	}

	{
		REG_LOCAL_IRQ_SAVE;

		pChipcHw->VPMPhaseCtrl1 &=
		    ~((chipcHw_REG_VPM_PHASE_VALUE_GE_MASK <<
		       chipcHw_REG_VPM_PHASE_VALUE_GE_SHIFT)
		      || (chipcHw_REG_VPM_PHASE_VALUE_LE_MASK <<
			  chipcHw_REG_VPM_PHASE_VALUE_LE_SHIFT));

		pChipcHw->VPMPhaseCtrl1 |=
		    ((ge << chipcHw_REG_VPM_PHASE_VALUE_GE_SHIFT)
		     || (le << chipcHw_REG_VPM_PHASE_VALUE_LE_SHIFT));

		REG_LOCAL_IRQ_RESTORE;
	}
}

/****************************************************************************/
/**
*  @brief   Checks DDR phase aligned status done by HW
*
*  @return  1: When aligned
*           0: When not aligned
*/
/****************************************************************************/
static inline uint32_t chipcHw_isDdrHwPhaseAligned(void)
{
	return (pChipcHw->
		PhaseAlignStatus & chipcHw_REG_DDR_PHASE_ALIGNED) ? 1 : 0;
}

/****************************************************************************/
/**
*  @brief   Checks VPM phase aligned status done by HW
*
*  @return  1: When aligned
*           0: When not aligned
*/
/****************************************************************************/
static inline uint32_t chipcHw_isVpmHwPhaseAligned(void)
{
	return (pChipcHw->
		PhaseAlignStatus & chipcHw_REG_VPM_PHASE_ALIGNED) ? 1 : 0;
}

/****************************************************************************/
/**
*  @brief   Get DDR phase aligned status done by HW
*
*/
/****************************************************************************/
static inline uint32_t chipcHw_getDdrHwPhaseAlignStatus(void)
{
	return (pChipcHw->
		PhaseAlignStatus & chipcHw_REG_DDR_PHASE_STATUS_MASK) >>
	    chipcHw_REG_DDR_PHASE_STATUS_SHIFT;
}

/****************************************************************************/
/**
*  @brief   Get VPM phase aligned status done by HW
*
*/
/****************************************************************************/
static inline uint32_t chipcHw_getVpmHwPhaseAlignStatus(void)
{
	return (pChipcHw->
		PhaseAlignStatus & chipcHw_REG_VPM_PHASE_STATUS_MASK) >>
	    chipcHw_REG_VPM_PHASE_STATUS_SHIFT;
}

/****************************************************************************/
/**
*  @brief   Get DDR phase control value
*
*/
/****************************************************************************/
static inline uint32_t chipcHw_getDdrPhaseControl(void)
{
	return (pChipcHw->
		PhaseAlignStatus & chipcHw_REG_DDR_PHASE_CTRL_MASK) >>
	    chipcHw_REG_DDR_PHASE_CTRL_SHIFT;
}

/****************************************************************************/
/**
*  @brief   Get VPM phase control value
*
*/
/****************************************************************************/
static inline uint32_t chipcHw_getVpmPhaseControl(void)
{
	return (pChipcHw->
		PhaseAlignStatus & chipcHw_REG_VPM_PHASE_CTRL_MASK) >>
	    chipcHw_REG_VPM_PHASE_CTRL_SHIFT;
}

/****************************************************************************/
/**
*  @brief   DDR phase alignment timeout count
*
*  @note    If HW fails to perform the phase alignment, it will trigger
*           a DDR phase alignment timeout interrupt.
*/
/****************************************************************************/
static inline void chipcHw_ddrHwPhaseAlignTimeout(uint32_t busCycle)
{
	REG_LOCAL_IRQ_SAVE;
	pChipcHw->DDRPhaseCtrl2 &=
	    ~(chipcHw_REG_DDR_PHASE_TIMEOUT_COUNT_MASK <<
	      chipcHw_REG_DDR_PHASE_TIMEOUT_COUNT_SHIFT);
	pChipcHw->DDRPhaseCtrl2 |=
	    (busCycle & chipcHw_REG_DDR_PHASE_TIMEOUT_COUNT_MASK) <<
	    chipcHw_REG_DDR_PHASE_TIMEOUT_COUNT_SHIFT;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   VPM phase alignment timeout count
*
*  @note    If HW fails to perform the phase alignment, it will trigger
*           a VPM phase alignment timeout interrupt.
*/
/****************************************************************************/
static inline void chipcHw_vpmHwPhaseAlignTimeout(uint32_t busCycle)
{
	REG_LOCAL_IRQ_SAVE;
	pChipcHw->VPMPhaseCtrl2 &=
	    ~(chipcHw_REG_VPM_PHASE_TIMEOUT_COUNT_MASK <<
	      chipcHw_REG_VPM_PHASE_TIMEOUT_COUNT_SHIFT);
	pChipcHw->VPMPhaseCtrl2 |=
	    (busCycle & chipcHw_REG_VPM_PHASE_TIMEOUT_COUNT_MASK) <<
	    chipcHw_REG_VPM_PHASE_TIMEOUT_COUNT_SHIFT;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   Clear DDR phase alignment timeout interrupt
*
*/
/****************************************************************************/
static inline void chipcHw_ddrHwPhaseAlignTimeoutInterruptClear(void)
{
	REG_LOCAL_IRQ_SAVE;
	/* Clear timeout interrupt service bit */
	pChipcHw->DDRPhaseCtrl2 |= chipcHw_REG_DDR_INTR_SERVICED;
	pChipcHw->DDRPhaseCtrl2 &= ~chipcHw_REG_DDR_INTR_SERVICED;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   Clear VPM phase alignment timeout interrupt
*
*/
/****************************************************************************/
static inline void chipcHw_vpmHwPhaseAlignTimeoutInterruptClear(void)
{
	REG_LOCAL_IRQ_SAVE;
	/* Clear timeout interrupt service bit */
	pChipcHw->VPMPhaseCtrl2 |= chipcHw_REG_VPM_INTR_SERVICED;
	pChipcHw->VPMPhaseCtrl2 &= ~chipcHw_REG_VPM_INTR_SERVICED;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   DDR phase alignment timeout interrupt enable
*
*/
/****************************************************************************/
static inline void chipcHw_ddrHwPhaseAlignTimeoutInterruptEnable(void)
{
	REG_LOCAL_IRQ_SAVE;
	chipcHw_ddrHwPhaseAlignTimeoutInterruptClear();	/* Recommended */
	/* Enable timeout interrupt */
	pChipcHw->DDRPhaseCtrl2 |= chipcHw_REG_DDR_TIMEOUT_INTR_ENABLE;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   VPM phase alignment timeout interrupt enable
*
*/
/****************************************************************************/
static inline void chipcHw_vpmHwPhaseAlignTimeoutInterruptEnable(void)
{
	REG_LOCAL_IRQ_SAVE;
	chipcHw_vpmHwPhaseAlignTimeoutInterruptClear();	/* Recommended */
	/* Enable timeout interrupt */
	pChipcHw->VPMPhaseCtrl2 |= chipcHw_REG_VPM_TIMEOUT_INTR_ENABLE;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   DDR phase alignment timeout interrupt disable
*
*/
/****************************************************************************/
static inline void chipcHw_ddrHwPhaseAlignTimeoutInterruptDisable(void)
{
	REG_LOCAL_IRQ_SAVE;
	pChipcHw->DDRPhaseCtrl2 &= ~chipcHw_REG_DDR_TIMEOUT_INTR_ENABLE;
	REG_LOCAL_IRQ_RESTORE;
}

/****************************************************************************/
/**
*  @brief   VPM phase alignment timeout interrupt disable
*
*/
/****************************************************************************/
static inline void chipcHw_vpmHwPhaseAlignTimeoutInterruptDisable(void)
{
	REG_LOCAL_IRQ_SAVE;
	pChipcHw->VPMPhaseCtrl2 &= ~chipcHw_REG_VPM_TIMEOUT_INTR_ENABLE;
	REG_LOCAL_IRQ_RESTORE;
}

#endif /* CHIPC_INLINE_H */
