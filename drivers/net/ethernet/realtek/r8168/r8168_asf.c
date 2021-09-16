// SPDX-License-Identifier: GPL-2.0-only
/*
################################################################################
#
# r8168 is the Linux device driver released for Realtek Gigabit Ethernet
# controllers with PCI-Express interface.
#
# Copyright(c) 2021 Realtek Semiconductor Corp. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, see <http://www.gnu.org/licenses/>.
#
# Author:
# Realtek NIC software team <nicfae@realtek.com>
# No. 2, Innovation Road II, Hsinchu Science Park, Hsinchu 300, Taiwan
#
################################################################################
*/

/************************************************************************************
 *  This product is covered by one or more of the following patents:
 *  US6,570,884, US6,115,776, and US6,327,625.
 ***********************************************************************************/

#include <linux/module.h>
#include <linux/version.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/crc32.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/init.h>
#include <linux/rtnetlink.h>

#include <asm/uaccess.h>

#include "r8168.h"
#include "r8168_asf.h"
#include "rtl_eeprom.h"

int rtl8168_asf_ioctl(struct net_device *dev,
                      struct ifreq *ifr)
{
        struct rtl8168_private *tp = netdev_priv(dev);
        void *user_data = ifr->ifr_data;
        struct asf_ioctl_struct asf_usrdata;
        unsigned long flags;

        if (tp->mcfg != CFG_METHOD_7 && tp->mcfg != CFG_METHOD_8)
                return -EOPNOTSUPP;

        if (copy_from_user(&asf_usrdata, user_data, sizeof(struct asf_ioctl_struct)))
                return -EFAULT;

        spin_lock_irqsave(&tp->lock, flags);

        switch (asf_usrdata.offset) {
        case HBPeriod:
                rtl8168_asf_hbperiod(tp, asf_usrdata.arg, asf_usrdata.u.data);
                break;
        case WD8Timer:
                break;
        case WD16Rst:
                rtl8168_asf_wd16rst(tp, asf_usrdata.arg, asf_usrdata.u.data);
                break;
        case WD8Rst:
                rtl8168_asf_time_period(tp, asf_usrdata.arg, WD8Rst, asf_usrdata.u.data);
                break;
        case LSnsrPollCycle:
                rtl8168_asf_time_period(tp, asf_usrdata.arg, LSnsrPollCycle, asf_usrdata.u.data);
                break;
        case ASFSnsrPollPrd:
                rtl8168_asf_time_period(tp, asf_usrdata.arg, ASFSnsrPollPrd, asf_usrdata.u.data);
                break;
        case AlertReSendItvl:
                rtl8168_asf_time_period(tp, asf_usrdata.arg, AlertReSendItvl, asf_usrdata.u.data);
                break;
        case SMBAddr:
                rtl8168_asf_rw_hexadecimal(tp, asf_usrdata.arg, SMBAddr, RW_ONE_BYTE, asf_usrdata.u.data);
                break;
        case ASFConfigR0:
                rtl8168_asf_config_regs(tp, asf_usrdata.arg, ASFConfigR0, asf_usrdata.u.data);
                break;
        case ASFConfigR1:
                rtl8168_asf_config_regs(tp, asf_usrdata.arg, ASFConfigR1, asf_usrdata.u.data);
                break;
        case ConsoleMA:
                rtl8168_asf_console_mac(tp, asf_usrdata.arg, asf_usrdata.u.data);
                break;
        case ConsoleIP:
                rtl8168_asf_ip_address(tp, asf_usrdata.arg, ConsoleIP, asf_usrdata.u.data);
                break;
        case IPAddr:
                rtl8168_asf_ip_address(tp, asf_usrdata.arg, IPAddr, asf_usrdata.u.data);
                break;
        case UUID:
                rtl8168_asf_rw_uuid(tp, asf_usrdata.arg, asf_usrdata.u.data);
                break;
        case IANA:
                rtl8168_asf_rw_iana(tp, asf_usrdata.arg, asf_usrdata.u.data);
                break;
        case SysID:
                rtl8168_asf_rw_systemid(tp, asf_usrdata.arg, asf_usrdata.u.data);
                break;
        case Community:
                rtl8168_asf_community_string(tp, asf_usrdata.arg, asf_usrdata.u.string);
                break;
        case StringLength:
                rtl8168_asf_community_string_len(tp, asf_usrdata.arg, asf_usrdata.u.data);
                break;
        case FmCapMsk:
                rtl8168_asf_capability_masks(tp, asf_usrdata.arg, FmCapMsk, asf_usrdata.u.data);
                break;
        case SpCMDMsk:
                rtl8168_asf_capability_masks(tp, asf_usrdata.arg, SpCMDMsk, asf_usrdata.u.data);
                break;
        case SysCapMsk:
                rtl8168_asf_capability_masks(tp, asf_usrdata.arg, SysCapMsk, asf_usrdata.u.data);
                break;
        case RmtRstAddr:
                rtl8168_asf_rw_hexadecimal(tp, asf_usrdata.arg, RmtRstAddr, RW_ONE_BYTE, asf_usrdata.u.data);
                break;
        case RmtRstCmd:
                rtl8168_asf_rw_hexadecimal(tp, asf_usrdata.arg, RmtRstCmd, RW_ONE_BYTE, asf_usrdata.u.data);
                break;
        case RmtRstData:
                rtl8168_asf_rw_hexadecimal(tp, asf_usrdata.arg, RmtRstData, RW_ONE_BYTE, asf_usrdata.u.data);
                break;
        case RmtPwrOffAddr:
                rtl8168_asf_rw_hexadecimal(tp, asf_usrdata.arg, RmtPwrOffAddr, RW_ONE_BYTE, asf_usrdata.u.data);
                break;
        case RmtPwrOffCmd:
                rtl8168_asf_rw_hexadecimal(tp, asf_usrdata.arg, RmtPwrOffCmd, RW_ONE_BYTE, asf_usrdata.u.data);
                break;
        case RmtPwrOffData:
                rtl8168_asf_rw_hexadecimal(tp, asf_usrdata.arg, RmtPwrOffData, RW_ONE_BYTE, asf_usrdata.u.data);
                break;
        case RmtPwrOnAddr:
                rtl8168_asf_rw_hexadecimal(tp, asf_usrdata.arg, RmtPwrOnAddr, RW_ONE_BYTE, asf_usrdata.u.data);
                break;
        case RmtPwrOnCmd:
                rtl8168_asf_rw_hexadecimal(tp, asf_usrdata.arg, RmtPwrOnCmd, RW_ONE_BYTE, asf_usrdata.u.data);
                break;
        case RmtPwrOnData:
                rtl8168_asf_rw_hexadecimal(tp, asf_usrdata.arg, RmtPwrOnData, RW_ONE_BYTE, asf_usrdata.u.data);
                break;
        case RmtPCRAddr:
                rtl8168_asf_rw_hexadecimal(tp, asf_usrdata.arg, RmtPCRAddr, RW_ONE_BYTE, asf_usrdata.u.data);
                break;
        case RmtPCRCmd:
                rtl8168_asf_rw_hexadecimal(tp, asf_usrdata.arg, RmtPCRCmd, RW_ONE_BYTE, asf_usrdata.u.data);
                break;
        case RmtPCRData:
                rtl8168_asf_rw_hexadecimal(tp, asf_usrdata.arg, RmtPCRData, RW_ONE_BYTE, asf_usrdata.u.data);
                break;
        case ASFSnsr0Addr:
                rtl8168_asf_rw_hexadecimal(tp, asf_usrdata.arg, ASFSnsr0Addr, RW_ONE_BYTE, asf_usrdata.u.data);
                break;
        case LSnsrAddr0:
                rtl8168_asf_rw_hexadecimal(tp, asf_usrdata.arg, LSnsrAddr0, RW_ONE_BYTE, asf_usrdata.u.data);
                break;
        case KO:
                /* Get/Set Key Operation */
                rtl8168_asf_key_access(tp, asf_usrdata.arg, KO, asf_usrdata.u.data);
                break;
        case KA:
                /* Get/Set Key Administrator */
                rtl8168_asf_key_access(tp, asf_usrdata.arg, KA, asf_usrdata.u.data);
                break;
        case KG:
                /* Get/Set Key Generation */
                rtl8168_asf_key_access(tp, asf_usrdata.arg, KG, asf_usrdata.u.data);
                break;
        case KR:
                /* Get/Set Key Random */
                rtl8168_asf_key_access(tp, asf_usrdata.arg, KR, asf_usrdata.u.data);
                break;
        default:
                spin_unlock_irqrestore(&tp->lock, flags);
                return -EOPNOTSUPP;
        }

        spin_unlock_irqrestore(&tp->lock, flags);

        if (copy_to_user(user_data, &asf_usrdata, sizeof(struct asf_ioctl_struct)))
                return -EFAULT;

        return 0;
}

void rtl8168_asf_hbperiod(struct rtl8168_private *tp, int arg, unsigned int *data)
{
        if (arg == ASF_GET)
                data[ASFHBPERIOD] = rtl8168_eri_read(tp, HBPeriod, RW_TWO_BYTES, ERIAR_ASF);
        else if (arg == ASF_SET) {
                rtl8168_eri_write(tp, HBPeriod, RW_TWO_BYTES, data[ASFHBPERIOD], ERIAR_ASF);
                rtl8168_eri_write(tp, 0x1EC, RW_ONE_BYTE, 0x07, ERIAR_ASF);
        }
}

void rtl8168_asf_wd16rst(struct rtl8168_private *tp, int arg, unsigned int *data)
{
        data[ASFWD16RST] = rtl8168_eri_read(tp, WD16Rst, RW_TWO_BYTES, ERIAR_ASF);
}

void rtl8168_asf_console_mac(struct rtl8168_private *tp, int arg, unsigned int *data)
{
        int i;

        if (arg == ASF_GET) {
                for (i = 0; i < 6; i++)
                        data[i] = rtl8168_eri_read(tp, ConsoleMA + i, RW_ONE_BYTE, ERIAR_ASF);
        } else if (arg == ASF_SET) {
                for (i = 0; i < 6; i++)
                        rtl8168_eri_write(tp, ConsoleMA + i, RW_ONE_BYTE, data[i], ERIAR_ASF);

                /* write the new console MAC address to EEPROM */
                rtl8168_eeprom_write_sc(tp, 70, (data[1] << 8) | data[0]);
                rtl8168_eeprom_write_sc(tp, 71, (data[3] << 8) | data[2]);
                rtl8168_eeprom_write_sc(tp, 72, (data[5] << 8) | data[4]);
        }
}

void rtl8168_asf_ip_address(struct rtl8168_private *tp, int arg, int offset, unsigned int *data)
{
        int i;
        int eeprom_off = 0;

        if (arg == ASF_GET) {
                for (i = 0; i < 4; i++)
                        data[i] = rtl8168_eri_read(tp, offset + i, RW_ONE_BYTE, ERIAR_ASF);
        } else if (arg == ASF_SET) {
                for (i = 0; i < 4; i++)
                        rtl8168_eri_write(tp, offset + i, RW_ONE_BYTE, data[i], ERIAR_ASF);

                if (offset == ConsoleIP)
                        eeprom_off = 73;
                else if (offset == IPAddr)
                        eeprom_off = 75;

                /* write the new IP address to EEPROM */
                rtl8168_eeprom_write_sc(tp, eeprom_off, (data[1] << 8) | data[0]);
                rtl8168_eeprom_write_sc(tp, eeprom_off + 1, (data[3] << 8) | data[2]);

        }
}

void rtl8168_asf_config_regs(struct rtl8168_private *tp, int arg, int offset, unsigned int *data)
{
        unsigned int value;

        if (arg == ASF_GET) {
                data[ASFCAPABILITY] = (rtl8168_eri_read(tp, offset, RW_ONE_BYTE, ERIAR_ASF) & data[ASFCONFIG]) ? FUNCTION_ENABLE : FUNCTION_DISABLE;
        } else if (arg == ASF_SET) {
                value = rtl8168_eri_read(tp, offset, RW_ONE_BYTE, ERIAR_ASF);

                if (data[ASFCAPABILITY] == FUNCTION_ENABLE)
                        value |= data[ASFCONFIG];
                else if (data[ASFCAPABILITY] == FUNCTION_DISABLE)
                        value &= ~data[ASFCONFIG];

                rtl8168_eri_write(tp, offset, RW_ONE_BYTE, value, ERIAR_ASF);
        }
}

void rtl8168_asf_capability_masks(struct rtl8168_private *tp, int arg, int offset, unsigned int *data)
{
        unsigned int len, bit_mask;

        bit_mask = DISABLE_MASK;

        if (offset == FmCapMsk) {
                /* System firmware capabilities */
                len = RW_FOUR_BYTES;
                if (data[ASFCAPMASK] == FUNCTION_ENABLE)
                        bit_mask = FMW_CAP_MASK;
        } else if (offset == SpCMDMsk) {
                /* Special commands */
                len = RW_TWO_BYTES;
                if (data[ASFCAPMASK] == FUNCTION_ENABLE)
                        bit_mask = SPC_CMD_MASK;
        } else {
                /* System capability (offset == SysCapMsk)*/
                len = RW_ONE_BYTE;
                if (data[ASFCAPMASK] == FUNCTION_ENABLE)
                        bit_mask = SYS_CAP_MASK;
        }

        if (arg == ASF_GET)
                data[ASFCAPMASK] = rtl8168_eri_read(tp, offset, len, ERIAR_ASF) ? FUNCTION_ENABLE : FUNCTION_DISABLE;
        else /* arg == ASF_SET */
                rtl8168_eri_write(tp, offset, len, bit_mask, ERIAR_ASF);
}

void rtl8168_asf_community_string(struct rtl8168_private *tp, int arg, char *string)
{
        int i;

        if (arg == ASF_GET) {
                for (i = 0; i < COMMU_STR_MAX_LEN; i++)
                        string[i] = rtl8168_eri_read(tp, Community + i, RW_ONE_BYTE, ERIAR_ASF);
        } else { /* arg == ASF_SET */
                for (i = 0; i < COMMU_STR_MAX_LEN; i++)
                        rtl8168_eri_write(tp, Community + i, RW_ONE_BYTE, string[i], ERIAR_ASF);
        }
}

void rtl8168_asf_community_string_len(struct rtl8168_private *tp, int arg, unsigned int *data)
{
        if (arg == ASF_GET)
                data[ASFCOMMULEN] = rtl8168_eri_read(tp, StringLength, RW_ONE_BYTE, ERIAR_ASF);
        else /* arg == ASF_SET */
                rtl8168_eri_write(tp, StringLength, RW_ONE_BYTE, data[ASFCOMMULEN], ERIAR_ASF);
}

void rtl8168_asf_time_period(struct rtl8168_private *tp, int arg, int offset, unsigned int *data)
{
        int pos = 0;

        if (offset == WD8Rst)
                pos = ASFWD8RESET;
        else if (offset == LSnsrPollCycle)
                pos = ASFLSNRPOLLCYC;
        else if (offset == ASFSnsrPollPrd)
                pos = ASFSNRPOLLCYC;
        else if (offset == AlertReSendItvl)
                pos = ASFALERTRESND;

        if (arg == ASF_GET)
                data[pos] = rtl8168_eri_read(tp, offset, RW_ONE_BYTE, ERIAR_ASF);
        else /* arg == ASF_SET */
                rtl8168_eri_write(tp, offset, RW_ONE_BYTE, data[pos], ERIAR_ASF);

}

void rtl8168_asf_key_access(struct rtl8168_private *tp, int arg, int offset, unsigned int *data)
{
        int i, j;
        int key_off = 0;

        if (arg == ASF_GET) {
                for (i = 0; i < KEY_LEN; i++)
                        data[i] = rtl8168_eri_read(tp, offset + KEY_LEN - (i + 1), RW_ONE_BYTE, ERIAR_ASF);
        } else {
                if (offset == KO)
                        key_off = 162;
                else if (offset == KA)
                        key_off = 172;
                else if (offset == KG)
                        key_off = 182;
                else if (offset == KR)
                        key_off = 192;

                /* arg == ASF_SET */
                for (i = 0; i < KEY_LEN; i++)
                        rtl8168_eri_write(tp, offset + KEY_LEN - (i + 1), RW_ONE_BYTE, data[i], ERIAR_ASF);

                /* write the new key to EEPROM */
                for (i = 0, j = 19; i < 10; i++, j = j - 2)
                        rtl8168_eeprom_write_sc(tp, key_off + i, (data[j - 1] << 8) | data[j]);
        }
}

void rtl8168_asf_rw_hexadecimal(struct rtl8168_private *tp, int arg, int offset, int len, unsigned int *data)
{
        if (arg == ASF_GET)
                data[ASFRWHEXNUM] = rtl8168_eri_read(tp, offset, len, ERIAR_ASF);
        else /* arg == ASF_SET */
                rtl8168_eri_write(tp, offset, len, data[ASFRWHEXNUM], ERIAR_ASF);
}

void rtl8168_asf_rw_systemid(struct rtl8168_private *tp, int arg, unsigned int *data)
{
        int i;

        if (arg == ASF_GET)
                for (i = 0; i < SYSID_LEN ; i++)
                        data[i] = rtl8168_eri_read(tp, SysID + i, RW_ONE_BYTE, ERIAR_ASF);
        else /* arg == ASF_SET */
                for (i = 0; i < SYSID_LEN ; i++)
                        rtl8168_eri_write(tp, SysID + i, RW_ONE_BYTE, data[i], ERIAR_ASF);
}

void rtl8168_asf_rw_iana(struct rtl8168_private *tp, int arg, unsigned int *data)
{
        int i;

        if (arg == ASF_GET)
                for (i = 0; i < RW_FOUR_BYTES; i++)
                        data[i] = rtl8168_eri_read(tp, IANA + i, RW_ONE_BYTE, ERIAR_ASF);
        else /* arg == ASF_SET */
                for (i = 0; i < RW_FOUR_BYTES; i++)
                        rtl8168_eri_write(tp, IANA + i, RW_ONE_BYTE, data[i], ERIAR_ASF);
}

void rtl8168_asf_rw_uuid(struct rtl8168_private *tp, int arg, unsigned int *data)
{
        int i, j;

        if (arg == ASF_GET)
                for (i = UUID_LEN - 1, j = 0; i >= 0 ; i--, j++)
                        data[j] = rtl8168_eri_read(tp, UUID + i, RW_ONE_BYTE, ERIAR_ASF);
        else /* arg == ASF_SET */
                for (i = UUID_LEN - 1, j = 0; i >= 0 ; i--, j++)
                        rtl8168_eri_write(tp, UUID + i, RW_ONE_BYTE, data[j], ERIAR_ASF);
}
