// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _AICBLUETOOTH_H
#define _AICBLUETOOTH_H

int aic_bt_platform_init(struct aic_usb_dev *sdiodev);

void aic_bt_platform_deinit(struct aic_usb_dev *sdiodev);

int rwnx_plat_bin_fw_upload_android(struct aic_usb_dev *sdiodev, u32 fw_addr,
                               char *filename);

int rwnx_plat_m2d_flash_ota_android(struct aic_usb_dev *usbdev, char *filename);

int rwnx_plat_m2d_flash_ota_check(struct aic_usb_dev *usbdev, char *filename);

int rwnx_plat_bin_fw_patch_table_upload_android(struct aic_usb_dev *usbdev, char *filename);

int rwnx_plat_userconfig_upload_android(char *filename);

uint8_t rwnx_atoi(char *value);
uint32_t rwnx_atoli(char *value);

#endif
