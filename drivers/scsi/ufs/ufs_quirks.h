/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
 */

#ifndef _UFS_QUIRKS_H_
#define _UFS_QUIRKS_H_

/* return true if s1 is a prefix of s2 */
#define STR_PRFX_EQUAL(s1, s2) !strncmp(s1, s2, strlen(s1))

#define UFS_ANY_VENDOR 0xFFFF
#define UFS_ANY_MODEL  "ANY_MODEL"

#define UFS_VENDOR_MICRON      0x12C
#define UFS_VENDOR_SAMSUNG     0x1CE
#define UFS_VENDOR_SKHYNIX     0x1AD
#define UFS_VENDOR_TOSHIBA     0x198
#define UFS_VENDOR_WDC         0x145

/**
 * ufs_dev_fix - ufs device quirk info
 * @card: ufs card details
 * @quirk: device quirk
 */
struct ufs_dev_fix {
	u16 wmanufacturerid;
	u8 *model;
	unsigned int quirk;
};

#define END_FIX { }

/* add specific device quirk */
#define UFS_FIX(_vendor, _model, _quirk) { \
	.wmanufacturerid = (_vendor),\
	.model = (_model),		   \
	.quirk = (_quirk),		   \
}

/*
 * Some vendor's UFS device sends back to back NACs for the DL data frames
 * causing the host controller to raise the DFES error status. Sometimes
 * such UFS devices send back to back NAC without waiting for new
 * retransmitted DL frame from the host and in such cases it might be possible
 * the Host UniPro goes into bad state without raising the DFES error
 * interrupt. If this happens then all the pending commands would timeout
 * only after respective SW command (which is generally too large).
 *
 * We can workaround such device behaviour like this:
 * - As soon as SW sees the DL NAC error, it should schedule the error handler
 * - Error handler would sleep for 50ms to see if there are any fatal errors
 *   raised by UFS controller.
 *    - If there are fatal errors then SW does normal error recovery.
 *    - If there are no fatal errors then SW sends the NOP command to device
 *      to check if link is alive.
 *        - If NOP command times out, SW does normal error recovery
 *        - If NOP command succeed, skip the error handling.
 *
 * If DL NAC error is seen multiple times with some vendor's UFS devices then
 * enable this quirk to initiate quick error recovery and also silence related
 * error logs to reduce spamming of kernel logs.
 */
#define UFS_DEVICE_QUIRK_RECOVERY_FROM_DL_NAC_ERRORS (1 << 2)

/*
 * Few Toshiba UFS device models advertise RX_MIN_ACTIVATETIME_CAPABILITY as
 * 600us which may not be enough for reliable hibern8 exit hardware sequence
 * from UFS device.
 * To workaround this issue, host should set its PA_TACTIVATE time to 1ms even
 * if device advertises RX_MIN_ACTIVATETIME_CAPABILITY less than 1ms.
 */
#define UFS_DEVICE_QUIRK_PA_TACTIVATE	(1 << 4)

/*
 * It seems some UFS devices may keep drawing more than sleep current
 * (atleast for 500us) from UFS rails (especially from VCCQ rail).
 * To avoid this situation, add 2ms delay before putting these UFS
 * rails in LPM mode.
 */
#define UFS_DEVICE_QUIRK_DELAY_BEFORE_LPM	(1 << 6)

/*
 * Some UFS devices require host PA_TACTIVATE to be lower than device
 * PA_TACTIVATE, enabling this quirk ensure this.
 */
#define UFS_DEVICE_QUIRK_HOST_PA_TACTIVATE	(1 << 7)

/*
 * The max. value PA_SaveConfigTime is 250 (10us) but this is not enough for
 * some vendors.
 * Gear switch from PWM to HS may fail even with this max. PA_SaveConfigTime.
 * Gear switch can be issued by host controller as an error recovery and any
 * software delay will not help on this case so we need to increase
 * PA_SaveConfigTime to >32us as per vendor recommendation.
 */
#define UFS_DEVICE_QUIRK_HOST_PA_SAVECONFIGTIME	(1 << 8)

/*
 * Some UFS devices require VS_DebugSaveConfigTime is 0x10,
 * enabling this quirk ensure this.
 */
#define UFS_DEVICE_QUIRK_HOST_VS_DEBUGSAVECONFIGTIME	(1 << 9)

/*
 * Some pre-3.1 UFS devices can support extended features by upgrading
 * the firmware. Enable this quirk to make UFS core driver probe and enable
 * supported features on such devices.
 */
#define UFS_DEVICE_QUIRK_SUPPORT_EXTENDED_FEATURES (1 << 10)

/*
 * Some UFS devices require delay after VCC power rail is turned-off.
 * Enable this quirk to introduce 5ms delays after VCC power-off during
 * suspend flow.
 */
#define UFS_DEVICE_QUIRK_DELAY_AFTER_LPM        (1 << 11)

/*
 * Some UFS devices require L2P entry should be swapped before being sent to the
 * UFS device for HPB READ command.
 */
#define UFS_DEVICE_QUIRK_SWAP_L2P_ENTRY_FOR_HPB_READ (1 << 12)

#endif /* UFS_QUIRKS_H_ */
