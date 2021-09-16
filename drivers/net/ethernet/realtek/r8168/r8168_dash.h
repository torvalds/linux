/* SPDX-License-Identifier: GPL-2.0-only */
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

#ifndef _LINUX_R8168_DASH_H
#define _LINUX_R8168_DASH_H

#define SIOCDEVPRIVATE_RTLDASH   SIOCDEVPRIVATE+2

enum rtl_dash_cmd {
        RTL_DASH_ARP_NS_OFFLOAD = 0,
        RTL_DASH_SET_OOB_IPMAC,
        RTL_DASH_NOTIFY_OOB,

        RTL_DASH_SEND_BUFFER_DATA_TO_DASH_FW,
        RTL_DASH_CHECK_SEND_BUFFER_TO_DASH_FW_COMPLETE,
        RTL_DASH_GET_RCV_FROM_FW_BUFFER_DATA,
        RTL_DASH_OOB_REQ,
        RTL_DASH_OOB_ACK,
        RTL_DASH_DETACH_OOB_REQ,
        RTL_DASH_DETACH_OOB_ACK,

        RTL_FW_SET_IPV4 = 0x10,
        RTL_FW_GET_IPV4,
        RTL_FW_SET_IPV6,
        RTL_FW_GET_IPV6,
        RTL_FW_SET_EXT_SNMP,
        RTL_FW_GET_EXT_SNMP,
        RTL_FW_SET_WAKEUP_PATTERN,
        RTL_FW_GET_WAKEUP_PATTERN,
        RTL_FW_DEL_WAKEUP_PATTERN,

        RTLT_DASH_COMMAND_INVALID,
};

struct rtl_dash_ip_mac {
        struct sockaddr ifru_addr;
        struct sockaddr ifru_netmask;
        struct sockaddr ifru_hwaddr;
};

struct rtl_dash_ioctl_struct {
        __u32	cmd;
        __u32	offset;
        __u32	len;
        union {
                __u32	data;
                void *data_buffer;
        };
};

struct settings_ipv4 {
        __u32	IPv4addr;
        __u32	IPv4mask;
        __u32	IPv4Gateway;
};

struct settings_ipv6 {
        __u32	reserved;
        __u32	prefixLen;
        __u16	IPv6addr[8];
        __u16	IPv6Gateway[8];
};

struct settings_ext_snmp {
        __u16	index;
        __u16	oid_get_len;
        __u8	oid_for_get[24];
        __u8	reserved0[26];
        __u16	value_len;
        __u8	value[256];
        __u8	supported;
        __u8	reserved1[27];
};

struct wakeup_pattern {
        __u8	index;
        __u8	valid;
        __u8	start;
        __u8	length;
        __u8	name[36];
        __u8	mask[16];
        __u8	pattern[128];
        __u32	reserved[2];
};

typedef struct _RX_DASH_FROM_FW_DESC {
        __le16 length;
        __le16 status;
        __le32 resv;
        __le64 BufferAddress;
}
RX_DASH_FROM_FW_DESC, *PRX_DASH_FROM_FW_DESC;

typedef struct _TX_DASH_SEND_FW_DESC {
        __le16 length;
        __le16 status;
        __le32 resv;
        __le64 BufferAddress;
}
TX_DASH_SEND_FW_DESC, *PTX_DASH_SEND_FW_DESC;

typedef struct _OSOOBHdr {
        __le32 len;
        u8 type;
        u8 flag;
        u8 hostReqV;
        u8 res;
}
OSOOBHdr, *POSOOBHdr;

typedef struct _RX_DASH_BUFFER_TYPE_2 {
        OSOOBHdr oobhdr;
        u8 RxDataBuffer[0];
}
RX_DASH_BUFFER_TYPE_2, *PRX_DASH_BUFFER_TYPE_2;

#define ALIGN_8                 (0x7)
#define ALIGN_16                (0xf)
#define ALIGN_32                (0x1f)
#define ALIGN_64                (0x3f)
#define ALIGN_256               (0xff)
#define ALIGN_4096              (0xfff)

#define OCP_REG_CONFIG0 (0x10)
#define OCP_REG_CONFIG0_REV_F (0xB8)
#define OCP_REG_DASH_POLL (0x30)
#define OCP_REG_HOST_REQ (0x34)
#define OCP_REG_DASH_REQ (0x35)
#define OCP_REG_CR (0x36)
#define OCP_REG_DMEMSTA (0x38)
#define OCP_REG_GPHYAR (0x60)


#define OCP_REG_CONFIG0_DASHEN           BIT_15
#define OCP_REG_CONFIG0_OOBRESET         BIT_14
#define OCP_REG_CONFIG0_APRDY            BIT_13
#define OCP_REG_CONFIG0_FIRMWARERDY      BIT_12
#define OCP_REG_CONFIG0_DRIVERRDY        BIT_11
#define OCP_REG_CONFIG0_OOB_WDT          BIT_9
#define OCP_REG_CONFIG0_DRV_WAIT_OOB     BIT_8
#define OCP_REG_CONFIG0_TLSEN            BIT_7

#define HW_DASH_SUPPORT_DASH(_M)        ((_M)->HwSuppDashVer > 0)
#define HW_DASH_SUPPORT_TYPE_1(_M)        ((_M)->HwSuppDashVer == 1)
#define HW_DASH_SUPPORT_TYPE_2(_M)        ((_M)->HwSuppDashVer == 2)
#define HW_DASH_SUPPORT_TYPE_3(_M)        ((_M)->HwSuppDashVer == 3)

#define RECV_FROM_FW_BUF_SIZE (2048)
#define SEND_TO_FW_BUF_SIZE (2048)

#define RX_DASH_FROM_FW_OWN BIT_15
#define TX_DASH_SEND_FW_OWN BIT_15

#define TXS_CC3_0       (BIT_0|BIT_1|BIT_2|BIT_3)
#define TXS_EXC         BIT_4
#define TXS_LNKF        BIT_5
#define TXS_OWC         BIT_6
#define TXS_TES         BIT_7
#define TXS_UNF         BIT_9
#define TXS_LGSEN       BIT_11
#define TXS_LS          BIT_12
#define TXS_FS          BIT_13
#define TXS_EOR         BIT_14
#define TXS_OWN         BIT_15

#define TPPool_HRDY     0x20

#define HostReqReg (0xC0)
#define SystemMasterDescStartAddrLow (0xF0)
#define SystemMasterDescStartAddrHigh (0xF4)
#define SystemSlaveDescStartAddrLow (0xF8)
#define SystemSlaveDescStartAddrHigh (0xFC)

//DASH Request Type
#define WSMANREG 0x01
#define OSPUSHDATA 0x02

#define RXS_OWN      BIT_15
#define RXS_EOR      BIT_14
#define RXS_FS       BIT_13
#define RXS_LS       BIT_12

#define ISRIMR_DP_DASH_OK BIT_15
#define ISRIMR_DP_HOST_OK BIT_13
#define ISRIMR_DP_REQSYS_OK BIT_11

#define ISRIMR_DASH_INTR_EN BIT_12
#define ISRIMR_DASH_INTR_CMAC_RESET BIT_15

#define ISRIMR_DASH_TYPE2_ROK BIT_0
#define ISRIMR_DASH_TYPE2_RDU BIT_1
#define ISRIMR_DASH_TYPE2_TOK BIT_2
#define ISRIMR_DASH_TYPE2_TDU BIT_3
#define ISRIMR_DASH_TYPE2_TX_FIFO_FULL BIT_4
#define ISRIMR_DASH_TYPE2_TX_DISABLE_IDLE BIT_5
#define ISRIMR_DASH_TYPE2_RX_DISABLE_IDLE BIT_6

#define CMAC_OOB_STOP 0x25
#define CMAC_OOB_INIT 0x26
#define CMAC_OOB_RESET 0x2a

#define NO_BASE_ADDRESS 0x00000000
#define RTL8168FP_OOBMAC_BASE 0xBAF70000
#define RTL8168FP_CMAC_IOBASE 0xBAF20000
#define RTL8168FP_KVM_BASE 0xBAF80400
#define CMAC_SYNC_REG 0x20
#define CMAC_RXDESC_OFFSET 0x90    //RX: 0x90 - 0x98
#define CMAC_TXDESC_OFFSET 0x98    //TX: 0x98 - 0x9F

/* cmac write/read MMIO register */
#define RTL_CMAC_W8(tp, reg, val8)   writeb ((val8), tp->cmac_ioaddr + (reg))
#define RTL_CMAC_W16(tp, reg, val16) writew ((val16), tp->cmac_ioaddr + (reg))
#define RTL_CMAC_W32(tp, reg, val32) writel ((val32), tp->cmac_ioaddr + (reg))
#define RTL_CMAC_R8(tp, reg)     readb (tp->cmac_ioaddr + (reg))
#define RTL_CMAC_R16(tp, reg)        readw (tp->cmac_ioaddr + (reg))
#define RTL_CMAC_R32(tp, reg)        ((unsigned long) readl (tp->cmac_ioaddr + (reg)))

int rtl8168_dash_ioctl(struct net_device *dev, struct ifreq *ifr);
void HandleDashInterrupt(struct net_device *dev);
int AllocateDashShareMemory(struct net_device *dev);
void FreeAllocatedDashShareMemory(struct net_device *dev);
void DashHwInit(struct net_device *dev);


#endif /* _LINUX_R8168_DASH_H */
