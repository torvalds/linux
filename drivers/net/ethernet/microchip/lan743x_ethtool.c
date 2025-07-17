/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (C) 2018 Microchip Technology Inc. */

#include <linux/netdevice.h>
#include <linux/net_tstamp.h>
#include <linux/pci.h>
#include <linux/phy.h>
#include "lan743x_main.h"
#include "lan743x_ethtool.h"
#include <linux/sched.h>
#include <linux/iopoll.h>

/* eeprom */
#define LAN743X_EEPROM_MAGIC		    (0x74A5)
#define LAN743X_OTP_MAGIC		    (0x74F3)
#define EEPROM_INDICATOR_1		    (0xA5)
#define EEPROM_INDICATOR_2		    (0xAA)
#define EEPROM_MAC_OFFSET		    (0x01)
#define MAX_EEPROM_SIZE			    (512)
#define MAX_OTP_SIZE			    (1024)
#define MAX_HS_OTP_SIZE			    (8 * 1024)
#define MAX_HS_EEPROM_SIZE		    (64 * 1024)
#define OTP_INDICATOR_1			    (0xF3)
#define OTP_INDICATOR_2			    (0xF7)

#define LOCK_TIMEOUT_MAX_CNT		    (100) // 1 sec (10 msce * 100)

#define LAN743X_CSR_READ_OP(offset)	     lan743x_csr_read(adapter, offset)

static int lan743x_otp_power_up(struct lan743x_adapter *adapter)
{
	u32 reg_value;

	reg_value = lan743x_csr_read(adapter, OTP_PWR_DN);

	if (reg_value & OTP_PWR_DN_PWRDN_N_) {
		/* clear it and wait to be cleared */
		reg_value &= ~OTP_PWR_DN_PWRDN_N_;
		lan743x_csr_write(adapter, OTP_PWR_DN, reg_value);

		usleep_range(100, 20000);
	}

	return 0;
}

static void lan743x_otp_power_down(struct lan743x_adapter *adapter)
{
	u32 reg_value;

	reg_value = lan743x_csr_read(adapter, OTP_PWR_DN);
	if (!(reg_value & OTP_PWR_DN_PWRDN_N_)) {
		/* set power down bit */
		reg_value |= OTP_PWR_DN_PWRDN_N_;
		lan743x_csr_write(adapter, OTP_PWR_DN, reg_value);
	}
}

static void lan743x_otp_set_address(struct lan743x_adapter *adapter,
				    u32 address)
{
	lan743x_csr_write(adapter, OTP_ADDR_HIGH, (address >> 8) & 0x03);
	lan743x_csr_write(adapter, OTP_ADDR_LOW, address & 0xFF);
}

static void lan743x_otp_read_go(struct lan743x_adapter *adapter)
{
	lan743x_csr_write(adapter, OTP_FUNC_CMD, OTP_FUNC_CMD_READ_);
	lan743x_csr_write(adapter, OTP_CMD_GO, OTP_CMD_GO_GO_);
}

static int lan743x_otp_wait_till_not_busy(struct lan743x_adapter *adapter)
{
	unsigned long timeout;
	u32 reg_val;

	timeout = jiffies + HZ;
	do {
		if (time_after(jiffies, timeout)) {
			netif_warn(adapter, drv, adapter->netdev,
				   "Timeout on OTP_STATUS completion\n");
			return -EIO;
		}
		udelay(1);
		reg_val = lan743x_csr_read(adapter, OTP_STATUS);
	} while (reg_val & OTP_STATUS_BUSY_);

	return 0;
}

static int lan743x_otp_read(struct lan743x_adapter *adapter, u32 offset,
			    u32 length, u8 *data)
{
	int ret;
	int i;

	if (offset + length > MAX_OTP_SIZE)
		return -EINVAL;

	ret = lan743x_otp_power_up(adapter);
	if (ret < 0)
		return ret;

	ret = lan743x_otp_wait_till_not_busy(adapter);
	if (ret < 0)
		return ret;

	for (i = 0; i < length; i++) {
		lan743x_otp_set_address(adapter, offset + i);

		lan743x_otp_read_go(adapter);
		ret = lan743x_otp_wait_till_not_busy(adapter);
		if (ret < 0)
			return ret;
		data[i] = lan743x_csr_read(adapter, OTP_READ_DATA);
	}

	lan743x_otp_power_down(adapter);

	return 0;
}

static int lan743x_otp_write(struct lan743x_adapter *adapter, u32 offset,
			     u32 length, u8 *data)
{
	int ret;
	int i;

	if (offset + length > MAX_OTP_SIZE)
		return -EINVAL;

	ret = lan743x_otp_power_up(adapter);
	if (ret < 0)
		return ret;

	ret = lan743x_otp_wait_till_not_busy(adapter);
	if (ret < 0)
		return ret;

	/* set to BYTE program mode */
	lan743x_csr_write(adapter, OTP_PRGM_MODE, OTP_PRGM_MODE_BYTE_);

	for (i = 0; i < length; i++) {
		lan743x_otp_set_address(adapter, offset + i);

		lan743x_csr_write(adapter, OTP_PRGM_DATA, data[i]);
		lan743x_csr_write(adapter, OTP_TST_CMD, OTP_TST_CMD_PRGVRFY_);
		lan743x_csr_write(adapter, OTP_CMD_GO, OTP_CMD_GO_GO_);

		ret = lan743x_otp_wait_till_not_busy(adapter);
		if (ret < 0)
			return ret;
	}

	lan743x_otp_power_down(adapter);

	return 0;
}

int lan743x_hs_syslock_acquire(struct lan743x_adapter *adapter,
			       u16 timeout)
{
	u16 timeout_cnt = 0;
	u32 val;

	do {
		spin_lock(&adapter->eth_syslock_spinlock);
		if (adapter->eth_syslock_acquire_cnt == 0) {
			lan743x_csr_write(adapter, ETH_SYSTEM_SYS_LOCK_REG,
					  SYS_LOCK_REG_ENET_SS_LOCK_);
			val = lan743x_csr_read(adapter,
					       ETH_SYSTEM_SYS_LOCK_REG);
			if (val & SYS_LOCK_REG_ENET_SS_LOCK_) {
				adapter->eth_syslock_acquire_cnt++;
				WARN_ON(adapter->eth_syslock_acquire_cnt == 0);
				spin_unlock(&adapter->eth_syslock_spinlock);
				break;
			}
		} else {
			adapter->eth_syslock_acquire_cnt++;
			WARN_ON(adapter->eth_syslock_acquire_cnt == 0);
			spin_unlock(&adapter->eth_syslock_spinlock);
			break;
		}

		spin_unlock(&adapter->eth_syslock_spinlock);

		if (timeout_cnt++ < timeout)
			usleep_range(10000, 11000);
		else
			return -ETIMEDOUT;
	} while (true);

	return 0;
}

void lan743x_hs_syslock_release(struct lan743x_adapter *adapter)
{
	u32 val;

	spin_lock(&adapter->eth_syslock_spinlock);
	WARN_ON(adapter->eth_syslock_acquire_cnt == 0);

	if (adapter->eth_syslock_acquire_cnt) {
		adapter->eth_syslock_acquire_cnt--;
		if (adapter->eth_syslock_acquire_cnt == 0) {
			lan743x_csr_write(adapter, ETH_SYSTEM_SYS_LOCK_REG, 0);
			val = lan743x_csr_read(adapter,
					       ETH_SYSTEM_SYS_LOCK_REG);
			WARN_ON((val & SYS_LOCK_REG_ENET_SS_LOCK_) != 0);
		}
	}

	spin_unlock(&adapter->eth_syslock_spinlock);
}

static void lan743x_hs_otp_power_up(struct lan743x_adapter *adapter)
{
	u32 reg_value;

	reg_value = lan743x_csr_read(adapter, HS_OTP_PWR_DN);
	if (reg_value & OTP_PWR_DN_PWRDN_N_) {
		reg_value &= ~OTP_PWR_DN_PWRDN_N_;
		lan743x_csr_write(adapter, HS_OTP_PWR_DN, reg_value);
		/* To flush the posted write so the subsequent delay is
		 * guaranteed to happen after the write at the hardware
		 */
		lan743x_csr_read(adapter, HS_OTP_PWR_DN);
		udelay(1);
	}
}

static void lan743x_hs_otp_power_down(struct lan743x_adapter *adapter)
{
	u32 reg_value;

	reg_value = lan743x_csr_read(adapter, HS_OTP_PWR_DN);
	if (!(reg_value & OTP_PWR_DN_PWRDN_N_)) {
		reg_value |= OTP_PWR_DN_PWRDN_N_;
		lan743x_csr_write(adapter, HS_OTP_PWR_DN, reg_value);
		/* To flush the posted write so the subsequent delay is
		 * guaranteed to happen after the write at the hardware
		 */
		lan743x_csr_read(adapter, HS_OTP_PWR_DN);
		udelay(1);
	}
}

static void lan743x_hs_otp_set_address(struct lan743x_adapter *adapter,
				       u32 address)
{
	lan743x_csr_write(adapter, HS_OTP_ADDR_HIGH, (address >> 8) & 0x03);
	lan743x_csr_write(adapter, HS_OTP_ADDR_LOW, address & 0xFF);
}

static void lan743x_hs_otp_read_go(struct lan743x_adapter *adapter)
{
	lan743x_csr_write(adapter, HS_OTP_FUNC_CMD, OTP_FUNC_CMD_READ_);
	lan743x_csr_write(adapter, HS_OTP_CMD_GO, OTP_CMD_GO_GO_);
}

static int lan743x_hs_otp_cmd_cmplt_chk(struct lan743x_adapter *adapter)
{
	u32 val;

	return readx_poll_timeout(LAN743X_CSR_READ_OP, HS_OTP_STATUS, val,
				  !(val & OTP_STATUS_BUSY_),
				  80, 10000);
}

static int lan743x_hs_otp_read(struct lan743x_adapter *adapter, u32 offset,
			       u32 length, u8 *data)
{
	int ret;
	int i;

	if (offset + length > MAX_HS_OTP_SIZE)
		return -EINVAL;

	ret = lan743x_hs_syslock_acquire(adapter, LOCK_TIMEOUT_MAX_CNT);
	if (ret < 0)
		return ret;

	lan743x_hs_otp_power_up(adapter);

	ret = lan743x_hs_otp_cmd_cmplt_chk(adapter);
	if (ret < 0)
		goto power_down;

	lan743x_hs_syslock_release(adapter);

	for (i = 0; i < length; i++) {
		ret = lan743x_hs_syslock_acquire(adapter,
						 LOCK_TIMEOUT_MAX_CNT);
		if (ret < 0)
			return ret;

		lan743x_hs_otp_set_address(adapter, offset + i);

		lan743x_hs_otp_read_go(adapter);
		ret = lan743x_hs_otp_cmd_cmplt_chk(adapter);
		if (ret < 0)
			goto power_down;

		data[i] = lan743x_csr_read(adapter, HS_OTP_READ_DATA);

		lan743x_hs_syslock_release(adapter);
	}

	ret = lan743x_hs_syslock_acquire(adapter,
					 LOCK_TIMEOUT_MAX_CNT);
	if (ret < 0)
		return ret;

power_down:
	lan743x_hs_otp_power_down(adapter);
	lan743x_hs_syslock_release(adapter);

	return ret;
}

static int lan743x_hs_otp_write(struct lan743x_adapter *adapter, u32 offset,
				u32 length, u8 *data)
{
	int ret;
	int i;

	if (offset + length > MAX_HS_OTP_SIZE)
		return -EINVAL;

	ret = lan743x_hs_syslock_acquire(adapter, LOCK_TIMEOUT_MAX_CNT);
	if (ret < 0)
		return ret;

	lan743x_hs_otp_power_up(adapter);

	ret = lan743x_hs_otp_cmd_cmplt_chk(adapter);
	if (ret < 0)
		goto power_down;

	/* set to BYTE program mode */
	lan743x_csr_write(adapter, HS_OTP_PRGM_MODE, OTP_PRGM_MODE_BYTE_);

	lan743x_hs_syslock_release(adapter);

	for (i = 0; i < length; i++) {
		ret = lan743x_hs_syslock_acquire(adapter,
						 LOCK_TIMEOUT_MAX_CNT);
		if (ret < 0)
			return ret;

		lan743x_hs_otp_set_address(adapter, offset + i);

		lan743x_csr_write(adapter, HS_OTP_PRGM_DATA, data[i]);
		lan743x_csr_write(adapter, HS_OTP_TST_CMD,
				  OTP_TST_CMD_PRGVRFY_);
		lan743x_csr_write(adapter, HS_OTP_CMD_GO, OTP_CMD_GO_GO_);

		ret = lan743x_hs_otp_cmd_cmplt_chk(adapter);
		if (ret < 0)
			goto power_down;

		lan743x_hs_syslock_release(adapter);
	}

	ret = lan743x_hs_syslock_acquire(adapter, LOCK_TIMEOUT_MAX_CNT);
	if (ret < 0)
		return ret;

power_down:
	lan743x_hs_otp_power_down(adapter);
	lan743x_hs_syslock_release(adapter);

	return ret;
}

static int lan743x_eeprom_wait(struct lan743x_adapter *adapter)
{
	unsigned long start_time = jiffies;
	u32 val;

	do {
		val = lan743x_csr_read(adapter, E2P_CMD);

		if (!(val & E2P_CMD_EPC_BUSY_) ||
		    (val & E2P_CMD_EPC_TIMEOUT_))
			break;
		usleep_range(40, 100);
	} while (!time_after(jiffies, start_time + HZ));

	if (val & (E2P_CMD_EPC_TIMEOUT_ | E2P_CMD_EPC_BUSY_)) {
		netif_warn(adapter, drv, adapter->netdev,
			   "EEPROM read operation timeout\n");
		return -EIO;
	}

	return 0;
}

static int lan743x_eeprom_confirm_not_busy(struct lan743x_adapter *adapter)
{
	unsigned long start_time = jiffies;
	u32 val;

	do {
		val = lan743x_csr_read(adapter, E2P_CMD);

		if (!(val & E2P_CMD_EPC_BUSY_))
			return 0;

		usleep_range(40, 100);
	} while (!time_after(jiffies, start_time + HZ));

	netif_warn(adapter, drv, adapter->netdev, "EEPROM is busy\n");
	return -EIO;
}

static int lan743x_eeprom_read(struct lan743x_adapter *adapter,
			       u32 offset, u32 length, u8 *data)
{
	int retval;
	u32 val;
	int i;

	if (offset + length > MAX_EEPROM_SIZE)
		return -EINVAL;

	retval = lan743x_eeprom_confirm_not_busy(adapter);
	if (retval)
		return retval;

	for (i = 0; i < length; i++) {
		val = E2P_CMD_EPC_BUSY_ | E2P_CMD_EPC_CMD_READ_;
		val |= (offset & E2P_CMD_EPC_ADDR_MASK_);
		lan743x_csr_write(adapter, E2P_CMD, val);

		retval = lan743x_eeprom_wait(adapter);
		if (retval < 0)
			return retval;

		val = lan743x_csr_read(adapter, E2P_DATA);
		data[i] = val & 0xFF;
		offset++;
	}

	return 0;
}

static int lan743x_eeprom_write(struct lan743x_adapter *adapter,
				u32 offset, u32 length, u8 *data)
{
	int retval;
	u32 val;
	int i;

	if (offset + length > MAX_EEPROM_SIZE)
		return -EINVAL;

	retval = lan743x_eeprom_confirm_not_busy(adapter);
	if (retval)
		return retval;

	/* Issue write/erase enable command */
	val = E2P_CMD_EPC_BUSY_ | E2P_CMD_EPC_CMD_EWEN_;
	lan743x_csr_write(adapter, E2P_CMD, val);

	retval = lan743x_eeprom_wait(adapter);
	if (retval < 0)
		return retval;

	for (i = 0; i < length; i++) {
		/* Fill data register */
		val = data[i];
		lan743x_csr_write(adapter, E2P_DATA, val);

		/* Send "write" command */
		val = E2P_CMD_EPC_BUSY_ | E2P_CMD_EPC_CMD_WRITE_;
		val |= (offset & E2P_CMD_EPC_ADDR_MASK_);
		lan743x_csr_write(adapter, E2P_CMD, val);

		retval = lan743x_eeprom_wait(adapter);
		if (retval < 0)
			return retval;

		offset++;
	}

	return 0;
}

static int lan743x_hs_eeprom_cmd_cmplt_chk(struct lan743x_adapter *adapter)
{
	u32 val;

	return readx_poll_timeout(LAN743X_CSR_READ_OP, HS_E2P_CMD, val,
				  (!(val & HS_E2P_CMD_EPC_BUSY_) ||
				    (val & HS_E2P_CMD_EPC_TIMEOUT_)),
				  50, 10000);
}

static int lan743x_hs_eeprom_read(struct lan743x_adapter *adapter,
				  u32 offset, u32 length, u8 *data)
{
	int retval;
	u32 val;
	int i;

	if (offset + length > MAX_HS_EEPROM_SIZE)
		return -EINVAL;

	retval = lan743x_hs_syslock_acquire(adapter, LOCK_TIMEOUT_MAX_CNT);
	if (retval < 0)
		return retval;

	retval = lan743x_hs_eeprom_cmd_cmplt_chk(adapter);
	lan743x_hs_syslock_release(adapter);
	if (retval < 0)
		return retval;

	for (i = 0; i < length; i++) {
		retval = lan743x_hs_syslock_acquire(adapter,
						    LOCK_TIMEOUT_MAX_CNT);
		if (retval < 0)
			return retval;

		val = HS_E2P_CMD_EPC_BUSY_ | HS_E2P_CMD_EPC_CMD_READ_;
		val |= (offset & HS_E2P_CMD_EPC_ADDR_MASK_);
		lan743x_csr_write(adapter, HS_E2P_CMD, val);
		retval = lan743x_hs_eeprom_cmd_cmplt_chk(adapter);
		if (retval < 0) {
			lan743x_hs_syslock_release(adapter);
			return retval;
		}

		val = lan743x_csr_read(adapter, HS_E2P_DATA);

		lan743x_hs_syslock_release(adapter);

		data[i] = val & 0xFF;
		offset++;
	}

	return 0;
}

static int lan743x_hs_eeprom_write(struct lan743x_adapter *adapter,
				   u32 offset, u32 length, u8 *data)
{
	int retval;
	u32 val;
	int i;

	if (offset + length > MAX_HS_EEPROM_SIZE)
		return -EINVAL;

	retval = lan743x_hs_syslock_acquire(adapter, LOCK_TIMEOUT_MAX_CNT);
	if (retval < 0)
		return retval;

	retval = lan743x_hs_eeprom_cmd_cmplt_chk(adapter);
	lan743x_hs_syslock_release(adapter);
	if (retval < 0)
		return retval;

	for (i = 0; i < length; i++) {
		retval = lan743x_hs_syslock_acquire(adapter,
						    LOCK_TIMEOUT_MAX_CNT);
		if (retval < 0)
			return retval;

		/* Fill data register */
		val = data[i];
		lan743x_csr_write(adapter, HS_E2P_DATA, val);

		/* Send "write" command */
		val = HS_E2P_CMD_EPC_BUSY_ | HS_E2P_CMD_EPC_CMD_WRITE_;
		val |= (offset & HS_E2P_CMD_EPC_ADDR_MASK_);
		lan743x_csr_write(adapter, HS_E2P_CMD, val);

		retval = lan743x_hs_eeprom_cmd_cmplt_chk(adapter);
		lan743x_hs_syslock_release(adapter);
		if (retval < 0)
			return retval;

		offset++;
	}

	return 0;
}

static void lan743x_ethtool_get_drvinfo(struct net_device *netdev,
					struct ethtool_drvinfo *info)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	strscpy(info->driver, DRIVER_NAME, sizeof(info->driver));
	strscpy(info->bus_info,
		pci_name(adapter->pdev), sizeof(info->bus_info));
}

static u32 lan743x_ethtool_get_msglevel(struct net_device *netdev)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	return adapter->msg_enable;
}

static void lan743x_ethtool_set_msglevel(struct net_device *netdev,
					 u32 msglevel)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	adapter->msg_enable = msglevel;
}

static int lan743x_ethtool_get_eeprom_len(struct net_device *netdev)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	if (adapter->flags & LAN743X_ADAPTER_FLAG_OTP)
		return adapter->is_pci11x1x ? MAX_HS_OTP_SIZE : MAX_OTP_SIZE;

	return adapter->is_pci11x1x ? MAX_HS_EEPROM_SIZE : MAX_EEPROM_SIZE;
}

static int lan743x_ethtool_get_eeprom(struct net_device *netdev,
				      struct ethtool_eeprom *ee, u8 *data)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);
	int ret = 0;

	if (adapter->flags & LAN743X_ADAPTER_FLAG_OTP) {
		if (adapter->is_pci11x1x)
			ret = lan743x_hs_otp_read(adapter, ee->offset,
						  ee->len, data);
		else
			ret = lan743x_otp_read(adapter, ee->offset,
					       ee->len, data);
	} else {
		if (adapter->is_pci11x1x)
			ret = lan743x_hs_eeprom_read(adapter, ee->offset,
						     ee->len, data);
		else
			ret = lan743x_eeprom_read(adapter, ee->offset,
						  ee->len, data);
	}

	return ret;
}

static int lan743x_ethtool_set_eeprom(struct net_device *netdev,
				      struct ethtool_eeprom *ee, u8 *data)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);
	int ret = -EINVAL;

	if (adapter->flags & LAN743X_ADAPTER_FLAG_OTP) {
		/* Beware!  OTP is One Time Programming ONLY! */
		if (ee->magic == LAN743X_OTP_MAGIC) {
			if (adapter->is_pci11x1x)
				ret = lan743x_hs_otp_write(adapter, ee->offset,
							   ee->len, data);
			else
				ret = lan743x_otp_write(adapter, ee->offset,
							ee->len, data);
		}
	} else {
		if (ee->magic == LAN743X_EEPROM_MAGIC) {
			if (adapter->is_pci11x1x)
				ret = lan743x_hs_eeprom_write(adapter,
							      ee->offset,
							      ee->len, data);
			else
				ret = lan743x_eeprom_write(adapter, ee->offset,
							   ee->len, data);
		}
	}

	return ret;
}

static const char lan743x_set0_hw_cnt_strings[][ETH_GSTRING_LEN] = {
	"RX FCS Errors",
	"RX Alignment Errors",
	"Rx Fragment Errors",
	"RX Jabber Errors",
	"RX Undersize Frame Errors",
	"RX Oversize Frame Errors",
	"RX Dropped Frames",
	"RX Unicast Byte Count",
	"RX Broadcast Byte Count",
	"RX Multicast Byte Count",
	"RX Unicast Frames",
	"RX Broadcast Frames",
	"RX Multicast Frames",
	"RX Pause Frames",
	"RX 64 Byte Frames",
	"RX 65 - 127 Byte Frames",
	"RX 128 - 255 Byte Frames",
	"RX 256 - 511 Bytes Frames",
	"RX 512 - 1023 Byte Frames",
	"RX 1024 - 1518 Byte Frames",
	"RX Greater 1518 Byte Frames",
};

static const char lan743x_set1_sw_cnt_strings[][ETH_GSTRING_LEN] = {
	"RX Queue 0 Frames",
	"RX Queue 1 Frames",
	"RX Queue 2 Frames",
	"RX Queue 3 Frames",
};

static const char lan743x_tx_queue_cnt_strings[][ETH_GSTRING_LEN] = {
	"TX Queue 0 Frames",
	"TX Queue 1 Frames",
	"TX Queue 2 Frames",
	"TX Queue 3 Frames",
	"TX Total Queue Frames",
};

static const char lan743x_set2_hw_cnt_strings[][ETH_GSTRING_LEN] = {
	"RX Total Frames",
	"EEE RX LPI Transitions",
	"EEE RX LPI Time",
	"RX Counter Rollover Status",
	"TX FCS Errors",
	"TX Excess Deferral Errors",
	"TX Carrier Errors",
	"TX Bad Byte Count",
	"TX Single Collisions",
	"TX Multiple Collisions",
	"TX Excessive Collision",
	"TX Late Collisions",
	"TX Unicast Byte Count",
	"TX Broadcast Byte Count",
	"TX Multicast Byte Count",
	"TX Unicast Frames",
	"TX Broadcast Frames",
	"TX Multicast Frames",
	"TX Pause Frames",
	"TX 64 Byte Frames",
	"TX 65 - 127 Byte Frames",
	"TX 128 - 255 Byte Frames",
	"TX 256 - 511 Bytes Frames",
	"TX 512 - 1023 Byte Frames",
	"TX 1024 - 1518 Byte Frames",
	"TX Greater 1518 Byte Frames",
	"TX Total Frames",
	"EEE TX LPI Transitions",
	"EEE TX LPI Time",
	"TX Counter Rollover Status",
};

static const u32 lan743x_set0_hw_cnt_addr[] = {
	STAT_RX_FCS_ERRORS,
	STAT_RX_ALIGNMENT_ERRORS,
	STAT_RX_FRAGMENT_ERRORS,
	STAT_RX_JABBER_ERRORS,
	STAT_RX_UNDERSIZE_FRAME_ERRORS,
	STAT_RX_OVERSIZE_FRAME_ERRORS,
	STAT_RX_DROPPED_FRAMES,
	STAT_RX_UNICAST_BYTE_COUNT,
	STAT_RX_BROADCAST_BYTE_COUNT,
	STAT_RX_MULTICAST_BYTE_COUNT,
	STAT_RX_UNICAST_FRAMES,
	STAT_RX_BROADCAST_FRAMES,
	STAT_RX_MULTICAST_FRAMES,
	STAT_RX_PAUSE_FRAMES,
	STAT_RX_64_BYTE_FRAMES,
	STAT_RX_65_127_BYTE_FRAMES,
	STAT_RX_128_255_BYTE_FRAMES,
	STAT_RX_256_511_BYTES_FRAMES,
	STAT_RX_512_1023_BYTE_FRAMES,
	STAT_RX_1024_1518_BYTE_FRAMES,
	STAT_RX_GREATER_1518_BYTE_FRAMES,
};

static const u32 lan743x_set2_hw_cnt_addr[] = {
	STAT_RX_TOTAL_FRAMES,
	STAT_EEE_RX_LPI_TRANSITIONS,
	STAT_EEE_RX_LPI_TIME,
	STAT_RX_COUNTER_ROLLOVER_STATUS,
	STAT_TX_FCS_ERRORS,
	STAT_TX_EXCESS_DEFERRAL_ERRORS,
	STAT_TX_CARRIER_ERRORS,
	STAT_TX_BAD_BYTE_COUNT,
	STAT_TX_SINGLE_COLLISIONS,
	STAT_TX_MULTIPLE_COLLISIONS,
	STAT_TX_EXCESSIVE_COLLISION,
	STAT_TX_LATE_COLLISIONS,
	STAT_TX_UNICAST_BYTE_COUNT,
	STAT_TX_BROADCAST_BYTE_COUNT,
	STAT_TX_MULTICAST_BYTE_COUNT,
	STAT_TX_UNICAST_FRAMES,
	STAT_TX_BROADCAST_FRAMES,
	STAT_TX_MULTICAST_FRAMES,
	STAT_TX_PAUSE_FRAMES,
	STAT_TX_64_BYTE_FRAMES,
	STAT_TX_65_127_BYTE_FRAMES,
	STAT_TX_128_255_BYTE_FRAMES,
	STAT_TX_256_511_BYTES_FRAMES,
	STAT_TX_512_1023_BYTE_FRAMES,
	STAT_TX_1024_1518_BYTE_FRAMES,
	STAT_TX_GREATER_1518_BYTE_FRAMES,
	STAT_TX_TOTAL_FRAMES,
	STAT_EEE_TX_LPI_TRANSITIONS,
	STAT_EEE_TX_LPI_TIME,
	STAT_TX_COUNTER_ROLLOVER_STATUS
};

static const char lan743x_priv_flags_strings[][ETH_GSTRING_LEN] = {
	"OTP_ACCESS",
};

static void lan743x_ethtool_get_strings(struct net_device *netdev,
					u32 stringset, u8 *data)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(data, lan743x_set0_hw_cnt_strings,
		       sizeof(lan743x_set0_hw_cnt_strings));
		memcpy(&data[sizeof(lan743x_set0_hw_cnt_strings)],
		       lan743x_set1_sw_cnt_strings,
		       sizeof(lan743x_set1_sw_cnt_strings));
		memcpy(&data[sizeof(lan743x_set0_hw_cnt_strings) +
		       sizeof(lan743x_set1_sw_cnt_strings)],
		       lan743x_set2_hw_cnt_strings,
		       sizeof(lan743x_set2_hw_cnt_strings));
		if (adapter->is_pci11x1x) {
			memcpy(&data[sizeof(lan743x_set0_hw_cnt_strings) +
			       sizeof(lan743x_set1_sw_cnt_strings) +
			       sizeof(lan743x_set2_hw_cnt_strings)],
			       lan743x_tx_queue_cnt_strings,
			       sizeof(lan743x_tx_queue_cnt_strings));
		}
		break;
	case ETH_SS_PRIV_FLAGS:
		memcpy(data, lan743x_priv_flags_strings,
		       sizeof(lan743x_priv_flags_strings));
		break;
	}
}

static void lan743x_ethtool_get_ethtool_stats(struct net_device *netdev,
					      struct ethtool_stats *stats,
					      u64 *data)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);
	u64 total_queue_count = 0;
	int data_index = 0;
	u64 pkt_cnt;
	u32 buf;
	int i;

	for (i = 0; i < ARRAY_SIZE(lan743x_set0_hw_cnt_addr); i++) {
		buf = lan743x_csr_read(adapter, lan743x_set0_hw_cnt_addr[i]);
		data[data_index++] = (u64)buf;
	}
	for (i = 0; i < ARRAY_SIZE(adapter->rx); i++)
		data[data_index++] = (u64)(adapter->rx[i].frame_count);
	for (i = 0; i < ARRAY_SIZE(lan743x_set2_hw_cnt_addr); i++) {
		buf = lan743x_csr_read(adapter, lan743x_set2_hw_cnt_addr[i]);
		data[data_index++] = (u64)buf;
	}
	if (adapter->is_pci11x1x) {
		for (i = 0; i < ARRAY_SIZE(adapter->tx); i++) {
			pkt_cnt = (u64)(adapter->tx[i].frame_count);
			data[data_index++] = pkt_cnt;
			total_queue_count += pkt_cnt;
		}
		data[data_index++] = total_queue_count;
	}
}

static u32 lan743x_ethtool_get_priv_flags(struct net_device *netdev)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	return adapter->flags;
}

static int lan743x_ethtool_set_priv_flags(struct net_device *netdev, u32 flags)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	adapter->flags = flags;

	return 0;
}

static int lan743x_ethtool_get_sset_count(struct net_device *netdev, int sset)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	switch (sset) {
	case ETH_SS_STATS:
	{
		int ret;

		ret = ARRAY_SIZE(lan743x_set0_hw_cnt_strings);
		ret += ARRAY_SIZE(lan743x_set1_sw_cnt_strings);
		ret += ARRAY_SIZE(lan743x_set2_hw_cnt_strings);
		if (adapter->is_pci11x1x)
			ret += ARRAY_SIZE(lan743x_tx_queue_cnt_strings);
		return ret;
	}
	case ETH_SS_PRIV_FLAGS:
		return ARRAY_SIZE(lan743x_priv_flags_strings);
	default:
		return -EOPNOTSUPP;
	}
}

static int lan743x_ethtool_get_rxnfc(struct net_device *netdev,
				     struct ethtool_rxnfc *rxnfc,
				     u32 *rule_locs)
{
	switch (rxnfc->cmd) {
	case ETHTOOL_GRXFH:
		rxnfc->data = 0;
		switch (rxnfc->flow_type) {
		case TCP_V4_FLOW:case UDP_V4_FLOW:
		case TCP_V6_FLOW:case UDP_V6_FLOW:
			rxnfc->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
			fallthrough;
		case IPV4_FLOW: case IPV6_FLOW:
			rxnfc->data |= RXH_IP_SRC | RXH_IP_DST;
			return 0;
		}
		break;
	case ETHTOOL_GRXRINGS:
		rxnfc->data = LAN743X_USED_RX_CHANNELS;
		return 0;
	}
	return -EOPNOTSUPP;
}

static u32 lan743x_ethtool_get_rxfh_key_size(struct net_device *netdev)
{
	return 40;
}

static u32 lan743x_ethtool_get_rxfh_indir_size(struct net_device *netdev)
{
	return 128;
}

static int lan743x_ethtool_get_rxfh(struct net_device *netdev,
				    struct ethtool_rxfh_param *rxfh)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	if (rxfh->indir) {
		int dw_index;
		int byte_index = 0;

		for (dw_index = 0; dw_index < 32; dw_index++) {
			u32 four_entries =
				lan743x_csr_read(adapter, RFE_INDX(dw_index));

			byte_index = dw_index << 2;
			rxfh->indir[byte_index + 0] =
				((four_entries >> 0) & 0x000000FF);
			rxfh->indir[byte_index + 1] =
				((four_entries >> 8) & 0x000000FF);
			rxfh->indir[byte_index + 2] =
				((four_entries >> 16) & 0x000000FF);
			rxfh->indir[byte_index + 3] =
				((four_entries >> 24) & 0x000000FF);
		}
	}
	if (rxfh->key) {
		int dword_index;
		int byte_index = 0;

		for (dword_index = 0; dword_index < 10; dword_index++) {
			u32 four_entries =
				lan743x_csr_read(adapter,
						 RFE_HASH_KEY(dword_index));

			byte_index = dword_index << 2;
			rxfh->key[byte_index + 0] =
				((four_entries >> 0) & 0x000000FF);
			rxfh->key[byte_index + 1] =
				((four_entries >> 8) & 0x000000FF);
			rxfh->key[byte_index + 2] =
				((four_entries >> 16) & 0x000000FF);
			rxfh->key[byte_index + 3] =
				((four_entries >> 24) & 0x000000FF);
		}
	}
	rxfh->hfunc = ETH_RSS_HASH_TOP;
	return 0;
}

static int lan743x_ethtool_set_rxfh(struct net_device *netdev,
				    struct ethtool_rxfh_param *rxfh,
				    struct netlink_ext_ack *extack)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);
	u32 *indir = rxfh->indir;
	u8 *key = rxfh->key;

	if (rxfh->hfunc != ETH_RSS_HASH_NO_CHANGE &&
	    rxfh->hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	if (indir) {
		u32 indir_value = 0;
		int dword_index = 0;
		int byte_index = 0;

		for (dword_index = 0; dword_index < 32; dword_index++) {
			byte_index = dword_index << 2;
			indir_value =
				(((indir[byte_index + 0] & 0x000000FF) << 0) |
				((indir[byte_index + 1] & 0x000000FF) << 8) |
				((indir[byte_index + 2] & 0x000000FF) << 16) |
				((indir[byte_index + 3] & 0x000000FF) << 24));
			lan743x_csr_write(adapter, RFE_INDX(dword_index),
					  indir_value);
		}
	}
	if (key) {
		int dword_index = 0;
		int byte_index = 0;
		u32 key_value = 0;

		for (dword_index = 0; dword_index < 10; dword_index++) {
			byte_index = dword_index << 2;
			key_value =
				((((u32)(key[byte_index + 0])) << 0) |
				(((u32)(key[byte_index + 1])) << 8) |
				(((u32)(key[byte_index + 2])) << 16) |
				(((u32)(key[byte_index + 3])) << 24));
			lan743x_csr_write(adapter, RFE_HASH_KEY(dword_index),
					  key_value);
		}
	}
	return 0;
}

static int lan743x_ethtool_get_ts_info(struct net_device *netdev,
				       struct kernel_ethtool_ts_info *ts_info)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	ts_info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				   SOF_TIMESTAMPING_TX_HARDWARE |
				   SOF_TIMESTAMPING_RX_HARDWARE |
				   SOF_TIMESTAMPING_RAW_HARDWARE;

	if (adapter->ptp.ptp_clock)
		ts_info->phc_index = ptp_clock_index(adapter->ptp.ptp_clock);

	ts_info->tx_types = BIT(HWTSTAMP_TX_OFF) |
			    BIT(HWTSTAMP_TX_ON) |
			    BIT(HWTSTAMP_TX_ONESTEP_SYNC);
	ts_info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			      BIT(HWTSTAMP_FILTER_ALL) |
			      BIT(HWTSTAMP_FILTER_PTP_V2_EVENT);
	return 0;
}

static int lan743x_ethtool_get_eee(struct net_device *netdev,
				   struct ethtool_keee *eee)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	return phylink_ethtool_get_eee(adapter->phylink, eee);
}

static int lan743x_ethtool_set_eee(struct net_device *netdev,
				   struct ethtool_keee *eee)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	return phylink_ethtool_set_eee(adapter->phylink, eee);
}

static int
lan743x_ethtool_set_link_ksettings(struct net_device *netdev,
				   const struct ethtool_link_ksettings *cmd)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	return phylink_ethtool_ksettings_set(adapter->phylink, cmd);
}

static int
lan743x_ethtool_get_link_ksettings(struct net_device *netdev,
				   struct ethtool_link_ksettings *cmd)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	return phylink_ethtool_ksettings_get(adapter->phylink, cmd);
}

#ifdef CONFIG_PM
static void lan743x_ethtool_get_wol(struct net_device *netdev,
				    struct ethtool_wolinfo *wol)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	wol->supported = 0;
	wol->wolopts = 0;

	phylink_ethtool_get_wol(adapter->phylink, wol);

	if (wol->supported != adapter->phy_wol_supported)
		netif_warn(adapter, drv, adapter->netdev,
			   "PHY changed its supported WOL! old=%x, new=%x\n",
			   adapter->phy_wol_supported, wol->supported);

	wol->supported |= MAC_SUPPORTED_WAKES;

	if (adapter->is_pci11x1x)
		wol->supported |= WAKE_MAGICSECURE;

	wol->wolopts |= adapter->wolopts;
	if (adapter->wolopts & WAKE_MAGICSECURE)
		memcpy(wol->sopass, adapter->sopass, sizeof(wol->sopass));
}

static int lan743x_ethtool_set_wol(struct net_device *netdev,
				   struct ethtool_wolinfo *wol)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	/* WAKE_MAGICSEGURE is a modifier of and only valid together with
	 * WAKE_MAGIC
	 */
	if ((wol->wolopts & WAKE_MAGICSECURE) && !(wol->wolopts & WAKE_MAGIC))
		return -EINVAL;

	if (netdev->phydev) {
		struct ethtool_wolinfo phy_wol;
		int ret;

		phy_wol.wolopts = wol->wolopts & adapter->phy_wol_supported;

		/* If WAKE_MAGICSECURE was requested, filter out WAKE_MAGIC
		 * for PHYs that do not support WAKE_MAGICSECURE
		 */
		if (wol->wolopts & WAKE_MAGICSECURE &&
		    !(adapter->phy_wol_supported & WAKE_MAGICSECURE))
			phy_wol.wolopts &= ~WAKE_MAGIC;

		ret = phylink_ethtool_set_wol(adapter->phylink, wol);
		if (ret && (ret != -EOPNOTSUPP))
			return ret;

		if (ret == -EOPNOTSUPP)
			adapter->phy_wolopts = 0;
		else
			adapter->phy_wolopts = phy_wol.wolopts;
	} else {
		adapter->phy_wolopts = 0;
	}

	adapter->wolopts = 0;
	wol->wolopts &= ~adapter->phy_wolopts;
	if (wol->wolopts & WAKE_UCAST)
		adapter->wolopts |= WAKE_UCAST;
	if (wol->wolopts & WAKE_MCAST)
		adapter->wolopts |= WAKE_MCAST;
	if (wol->wolopts & WAKE_BCAST)
		adapter->wolopts |= WAKE_BCAST;
	if (wol->wolopts & WAKE_MAGIC)
		adapter->wolopts |= WAKE_MAGIC;
	if (wol->wolopts & WAKE_PHY)
		adapter->wolopts |= WAKE_PHY;
	if (wol->wolopts & WAKE_ARP)
		adapter->wolopts |= WAKE_ARP;
	if (wol->wolopts & WAKE_MAGICSECURE &&
	    wol->wolopts & WAKE_MAGIC) {
		memcpy(adapter->sopass, wol->sopass, sizeof(wol->sopass));
		adapter->wolopts |= WAKE_MAGICSECURE;
	} else {
		memset(adapter->sopass, 0, sizeof(u8) * SOPASS_MAX);
	}

	wol->wolopts = adapter->wolopts | adapter->phy_wolopts;
	device_set_wakeup_enable(&adapter->pdev->dev, (bool)wol->wolopts);

	return 0;
}
#endif /* CONFIG_PM */

static void lan743x_common_regs(struct net_device *dev, void *p)
{
	struct lan743x_adapter *adapter = netdev_priv(dev);
	u32 *rb = p;

	memset(p, 0, (MAX_LAN743X_ETH_COMMON_REGS * sizeof(u32)));

	rb[ETH_PRIV_FLAGS] = adapter->flags;
	rb[ETH_ID_REV]     = lan743x_csr_read(adapter, ID_REV);
	rb[ETH_FPGA_REV]   = lan743x_csr_read(adapter, FPGA_REV);
	rb[ETH_STRAP_READ] = lan743x_csr_read(adapter, STRAP_READ);
	rb[ETH_INT_STS]    = lan743x_csr_read(adapter, INT_STS);
	rb[ETH_HW_CFG]     = lan743x_csr_read(adapter, HW_CFG);
	rb[ETH_PMT_CTL]    = lan743x_csr_read(adapter, PMT_CTL);
	rb[ETH_E2P_CMD]    = lan743x_csr_read(adapter, E2P_CMD);
	rb[ETH_E2P_DATA]   = lan743x_csr_read(adapter, E2P_DATA);
	rb[ETH_MAC_CR]     = lan743x_csr_read(adapter, MAC_CR);
	rb[ETH_MAC_RX]     = lan743x_csr_read(adapter, MAC_RX);
	rb[ETH_MAC_TX]     = lan743x_csr_read(adapter, MAC_TX);
	rb[ETH_FLOW]       = lan743x_csr_read(adapter, MAC_FLOW);
	rb[ETH_MII_ACC]    = lan743x_csr_read(adapter, MAC_MII_ACC);
	rb[ETH_MII_DATA]   = lan743x_csr_read(adapter, MAC_MII_DATA);
	rb[ETH_EEE_TX_LPI_REQ_DLY]  = lan743x_csr_read(adapter,
						       MAC_EEE_TX_LPI_REQ_DLY_CNT);
	rb[ETH_WUCSR]      = lan743x_csr_read(adapter, MAC_WUCSR);
	rb[ETH_WK_SRC]     = lan743x_csr_read(adapter, MAC_WK_SRC);
}

static void lan743x_sgmii_regs(struct net_device *dev, void *p)
{
	struct lan743x_adapter *adp = netdev_priv(dev);
	u32 *rb = p;
	u16 idx;
	int val;
	struct {
		u8 id;
		u8 dev;
		u16 addr;
	} regs[] = {
		{ ETH_SR_VSMMD_DEV_ID1,                MDIO_MMD_VEND1, 0x0002},
		{ ETH_SR_VSMMD_DEV_ID2,                MDIO_MMD_VEND1, 0x0003},
		{ ETH_SR_VSMMD_PCS_ID1,                MDIO_MMD_VEND1, 0x0004},
		{ ETH_SR_VSMMD_PCS_ID2,                MDIO_MMD_VEND1, 0x0005},
		{ ETH_SR_VSMMD_STS,                    MDIO_MMD_VEND1, 0x0008},
		{ ETH_SR_VSMMD_CTRL,                   MDIO_MMD_VEND1, 0x0009},
		{ ETH_SR_MII_CTRL,                     MDIO_MMD_VEND2, 0x0000},
		{ ETH_SR_MII_STS,                      MDIO_MMD_VEND2, 0x0001},
		{ ETH_SR_MII_DEV_ID1,                  MDIO_MMD_VEND2, 0x0002},
		{ ETH_SR_MII_DEV_ID2,                  MDIO_MMD_VEND2, 0x0003},
		{ ETH_SR_MII_AN_ADV,                   MDIO_MMD_VEND2, 0x0004},
		{ ETH_SR_MII_LP_BABL,                  MDIO_MMD_VEND2, 0x0005},
		{ ETH_SR_MII_EXPN,                     MDIO_MMD_VEND2, 0x0006},
		{ ETH_SR_MII_EXT_STS,                  MDIO_MMD_VEND2, 0x000F},
		{ ETH_SR_MII_TIME_SYNC_ABL,            MDIO_MMD_VEND2, 0x0708},
		{ ETH_SR_MII_TIME_SYNC_TX_MAX_DLY_LWR, MDIO_MMD_VEND2, 0x0709},
		{ ETH_SR_MII_TIME_SYNC_TX_MAX_DLY_UPR, MDIO_MMD_VEND2, 0x070A},
		{ ETH_SR_MII_TIME_SYNC_TX_MIN_DLY_LWR, MDIO_MMD_VEND2, 0x070B},
		{ ETH_SR_MII_TIME_SYNC_TX_MIN_DLY_UPR, MDIO_MMD_VEND2, 0x070C},
		{ ETH_SR_MII_TIME_SYNC_RX_MAX_DLY_LWR, MDIO_MMD_VEND2, 0x070D},
		{ ETH_SR_MII_TIME_SYNC_RX_MAX_DLY_UPR, MDIO_MMD_VEND2, 0x070E},
		{ ETH_SR_MII_TIME_SYNC_RX_MIN_DLY_LWR, MDIO_MMD_VEND2, 0x070F},
		{ ETH_SR_MII_TIME_SYNC_RX_MIN_DLY_UPR, MDIO_MMD_VEND2, 0x0710},
		{ ETH_VR_MII_DIG_CTRL1,                MDIO_MMD_VEND2, 0x8000},
		{ ETH_VR_MII_AN_CTRL,                  MDIO_MMD_VEND2, 0x8001},
		{ ETH_VR_MII_AN_INTR_STS,              MDIO_MMD_VEND2, 0x8002},
		{ ETH_VR_MII_TC,                       MDIO_MMD_VEND2, 0x8003},
		{ ETH_VR_MII_DBG_CTRL,                 MDIO_MMD_VEND2, 0x8005},
		{ ETH_VR_MII_EEE_MCTRL0,               MDIO_MMD_VEND2, 0x8006},
		{ ETH_VR_MII_EEE_TXTIMER,              MDIO_MMD_VEND2, 0x8008},
		{ ETH_VR_MII_EEE_RXTIMER,              MDIO_MMD_VEND2, 0x8009},
		{ ETH_VR_MII_LINK_TIMER_CTRL,          MDIO_MMD_VEND2, 0x800A},
		{ ETH_VR_MII_EEE_MCTRL1,               MDIO_MMD_VEND2, 0x800B},
		{ ETH_VR_MII_DIG_STS,                  MDIO_MMD_VEND2, 0x8010},
		{ ETH_VR_MII_ICG_ERRCNT1,              MDIO_MMD_VEND2, 0x8011},
		{ ETH_VR_MII_GPIO,                     MDIO_MMD_VEND2, 0x8015},
		{ ETH_VR_MII_EEE_LPI_STATUS,           MDIO_MMD_VEND2, 0x8016},
		{ ETH_VR_MII_EEE_WKERR,                MDIO_MMD_VEND2, 0x8017},
		{ ETH_VR_MII_MISC_STS,                 MDIO_MMD_VEND2, 0x8018},
		{ ETH_VR_MII_RX_LSTS,                  MDIO_MMD_VEND2, 0x8020},
		{ ETH_VR_MII_GEN2_GEN4_TX_BSTCTRL0,    MDIO_MMD_VEND2, 0x8038},
		{ ETH_VR_MII_GEN2_GEN4_TX_LVLCTRL0,    MDIO_MMD_VEND2, 0x803A},
		{ ETH_VR_MII_GEN2_GEN4_TXGENCTRL0,     MDIO_MMD_VEND2, 0x803C},
		{ ETH_VR_MII_GEN2_GEN4_TXGENCTRL1,     MDIO_MMD_VEND2, 0x803D},
		{ ETH_VR_MII_GEN4_TXGENCTRL2,          MDIO_MMD_VEND2, 0x803E},
		{ ETH_VR_MII_GEN2_GEN4_TX_STS,         MDIO_MMD_VEND2, 0x8048},
		{ ETH_VR_MII_GEN2_GEN4_RXGENCTRL0,     MDIO_MMD_VEND2, 0x8058},
		{ ETH_VR_MII_GEN2_GEN4_RXGENCTRL1,     MDIO_MMD_VEND2, 0x8059},
		{ ETH_VR_MII_GEN4_RXEQ_CTRL,           MDIO_MMD_VEND2, 0x805B},
		{ ETH_VR_MII_GEN4_RXLOS_CTRL0,         MDIO_MMD_VEND2, 0x805D},
		{ ETH_VR_MII_GEN2_GEN4_MPLL_CTRL0,     MDIO_MMD_VEND2, 0x8078},
		{ ETH_VR_MII_GEN2_GEN4_MPLL_CTRL1,     MDIO_MMD_VEND2, 0x8079},
		{ ETH_VR_MII_GEN2_GEN4_MPLL_STS,       MDIO_MMD_VEND2, 0x8088},
		{ ETH_VR_MII_GEN2_GEN4_LVL_CTRL,       MDIO_MMD_VEND2, 0x8090},
		{ ETH_VR_MII_GEN4_MISC_CTRL2,          MDIO_MMD_VEND2, 0x8093},
		{ ETH_VR_MII_GEN2_GEN4_MISC_CTRL0,     MDIO_MMD_VEND2, 0x8099},
		{ ETH_VR_MII_GEN2_GEN4_MISC_CTRL1,     MDIO_MMD_VEND2, 0x809A},
		{ ETH_VR_MII_SNPS_CR_CTRL,             MDIO_MMD_VEND2, 0x80A0},
		{ ETH_VR_MII_SNPS_CR_ADDR,             MDIO_MMD_VEND2, 0x80A1},
		{ ETH_VR_MII_SNPS_CR_DATA,             MDIO_MMD_VEND2, 0x80A2},
		{ ETH_VR_MII_DIG_CTRL2,                MDIO_MMD_VEND2, 0x80E1},
		{ ETH_VR_MII_DIG_ERRCNT,               MDIO_MMD_VEND2, 0x80E2},
	};

	for (idx = 0; idx < ARRAY_SIZE(regs); idx++) {
		val = lan743x_sgmii_read(adp, regs[idx].dev, regs[idx].addr);
		if (val < 0)
			rb[regs[idx].id] = 0xFFFF;
		else
			rb[regs[idx].id] = val;
	}
}

static int lan743x_get_regs_len(struct net_device *dev)
{
	struct lan743x_adapter *adapter = netdev_priv(dev);
	u32 num_regs = MAX_LAN743X_ETH_COMMON_REGS;

	if (adapter->is_sgmii_en)
		num_regs += MAX_LAN743X_ETH_SGMII_REGS;

	return num_regs * sizeof(u32);
}

static void lan743x_get_regs(struct net_device *dev,
			     struct ethtool_regs *regs, void *p)
{
	struct lan743x_adapter *adapter = netdev_priv(dev);
	int regs_len;

	regs_len = lan743x_get_regs_len(dev);
	memset(p, 0, regs_len);

	regs->version = LAN743X_ETH_REG_VERSION;
	regs->len = regs_len;

	lan743x_common_regs(dev, p);
	p = (u32 *)p + MAX_LAN743X_ETH_COMMON_REGS;

	if (adapter->is_sgmii_en) {
		lan743x_sgmii_regs(dev, p);
		p = (u32 *)p + MAX_LAN743X_ETH_SGMII_REGS;
	}
}

static void lan743x_get_pauseparam(struct net_device *dev,
				   struct ethtool_pauseparam *pause)
{
	struct lan743x_adapter *adapter = netdev_priv(dev);

	phylink_ethtool_get_pauseparam(adapter->phylink, pause);
}

static int lan743x_set_pauseparam(struct net_device *dev,
				  struct ethtool_pauseparam *pause)
{
	struct lan743x_adapter *adapter = netdev_priv(dev);

	return phylink_ethtool_set_pauseparam(adapter->phylink, pause);
}

const struct ethtool_ops lan743x_ethtool_ops = {
	.get_drvinfo = lan743x_ethtool_get_drvinfo,
	.get_msglevel = lan743x_ethtool_get_msglevel,
	.set_msglevel = lan743x_ethtool_set_msglevel,
	.get_link = ethtool_op_get_link,

	.get_eeprom_len = lan743x_ethtool_get_eeprom_len,
	.get_eeprom = lan743x_ethtool_get_eeprom,
	.set_eeprom = lan743x_ethtool_set_eeprom,
	.get_strings = lan743x_ethtool_get_strings,
	.get_ethtool_stats = lan743x_ethtool_get_ethtool_stats,
	.get_priv_flags = lan743x_ethtool_get_priv_flags,
	.set_priv_flags = lan743x_ethtool_set_priv_flags,
	.get_sset_count = lan743x_ethtool_get_sset_count,
	.get_rxnfc = lan743x_ethtool_get_rxnfc,
	.get_rxfh_key_size = lan743x_ethtool_get_rxfh_key_size,
	.get_rxfh_indir_size = lan743x_ethtool_get_rxfh_indir_size,
	.get_rxfh = lan743x_ethtool_get_rxfh,
	.set_rxfh = lan743x_ethtool_set_rxfh,
	.get_ts_info = lan743x_ethtool_get_ts_info,
	.get_eee = lan743x_ethtool_get_eee,
	.set_eee = lan743x_ethtool_set_eee,
	.get_link_ksettings = lan743x_ethtool_get_link_ksettings,
	.set_link_ksettings = lan743x_ethtool_set_link_ksettings,
	.get_regs_len = lan743x_get_regs_len,
	.get_regs = lan743x_get_regs,
	.get_pauseparam = lan743x_get_pauseparam,
	.set_pauseparam = lan743x_set_pauseparam,
#ifdef CONFIG_PM
	.get_wol = lan743x_ethtool_get_wol,
	.set_wol = lan743x_ethtool_set_wol,
#endif
};
