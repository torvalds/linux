/*
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _UFS_QUIRKS_H_
#define _UFS_QUIRKS_H_

/* return true if s1 is a prefix of s2 */
#define STR_PRFX_EQUAL(s1, s2) !strncmp(s1, s2, strlen(s1))

#define UFS_ANY_VENDOR 0xFFFF
#define UFS_ANY_MODEL  "ANY_MODEL"

#define UFS_VENDOR_MICRON      0x12C
#define UFS_VENDOR_TOSHIBA     0x198
#define UFS_VENDOR_SAMSUNG     0x1CE
#define UFS_VENDOR_SKHYNIX     0x1AD

/**
 * ufs_dev_fix - ufs device quirk info
 * @card: ufs card details
 * @quirk: device quirk
 */
struct ufs_dev_fix {
	struct ufs_dev_desc card;
	unsigned int quirk;
};

#define END_FIX { { 0 }, 0 }

/* add specific device quirk */
#define UFS_FIX(_vendor, _model, _quirk) { \
	.card.wmanufacturerid = (_vendor),\
	.card.model = (_model),		   \
	.quirk = (_quirk),		   \
}

/*
 * If UFS device is having issue in processing LCC (Line Control
 * Command) coming from UFS host controller then enable this quirk.
 * When this quirk is enabled, host controller driver should disable
 * the LCC transmission on UFS host controller (by clearing
 * TX_LCC_ENABLE attribute of host to 0).
 */
#define UFS_DEVICE_QUIRK_BROKEN_LCC (1 << 0)

/*
 * Some UFS devices don't need VCCQ rail for device operations. Enabling this
 * quirk for such devices will make sure that VCCQ rail is not voted.
 */
#define UFS_DEVICE_NO_VCCQ (1 << 1)

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
 * Some UFS devices may not work properly after resume if the link was kept
 * in off state during suspend. Enabling this quirk will not allow the
 * link to be kept in off state during suspend.
 */
#define UFS_DEVICE_QUIRK_NO_LINK_OFF	(1 << 3)

/*
 * Few Toshiba UFS device models advertise RX_MIN_ACTIVATETIME_CAPABILITY as
 * 600us which may not be enough for reliable hibern8 exit hardware sequence
 * from UFS device.
 * To workaround this issue, host should set its PA_TACTIVATE time to 1ms even
 * if device advertises RX_MIN_ACTIVATETIME_CAPABILITY less than 1ms.
 */
#define UFS_DEVICE_QUIRK_PA_TACTIVATE	(1 << 4)

/*
 * Some UFS memory devices may have really low read/write throughput in
 * FAST AUTO mode, enable this quirk to make sure that FAST AUTO mode is
 * never enabled for such devices.
 */
#define UFS_DEVICE_NO_FASTAUTO		(1 << 5)

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

#endif /* UFS_QUIRKS_H_ */
