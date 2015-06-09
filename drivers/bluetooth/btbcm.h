/*
 *
 *  Bluetooth support for Broadcom devices
 *
 *  Copyright (C) 2015  Intel Corporation
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#define BCM_UART_CLOCK_48MHZ	0x01
#define BCM_UART_CLOCK_24MHZ	0x02

struct bcm_update_uart_baud_rate {
	__le16 zero;
	__le32 baud_rate;
} __packed;

struct bcm_write_uart_clock_setting {
	__u8 type;
} __packed;

#if IS_ENABLED(CONFIG_BT_BCM)

int btbcm_check_bdaddr(struct hci_dev *hdev);
int btbcm_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr);
int btbcm_patchram(struct hci_dev *hdev, const struct firmware *fw);

int btbcm_setup_patchram(struct hci_dev *hdev);
int btbcm_setup_apple(struct hci_dev *hdev);

int btbcm_initialize(struct hci_dev *hdev, char *fw_name, size_t len);
int btbcm_finalize(struct hci_dev *hdev);

#else

static inline int btbcm_check_bdaddr(struct hci_dev *hdev)
{
	return -EOPNOTSUPP;
}

static inline int btbcm_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	return -EOPNOTSUPP;
}

static inline int btbcm_patchram(struct hci_dev *hdev, const struct firmware *fw)
{
	return -EOPNOTSUPP;
}

static inline int btbcm_setup_patchram(struct hci_dev *hdev)
{
	return 0;
}

static inline int btbcm_setup_apple(struct hci_dev *hdev)
{
	return 0;
}

static inline int btbcm_initialize(struct hci_dev *hdev, char *fw_name,
				   size_t len)
{
	return 0;
}

static inline int btbcm_finalize(struct hci_dev *hdev)
{
	return 0;
}

#endif
