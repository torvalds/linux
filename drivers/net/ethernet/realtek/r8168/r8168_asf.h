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

#define SIOCDEVPRIVATE_RTLASF   SIOCDEVPRIVATE

#define FUNCTION_ENABLE		1
#define FUNCTION_DISABLE	0

#define ASFCONFIG	0
#define ASFCAPABILITY	1
#define ASFCOMMULEN	0
#define ASFHBPERIOD	0
#define ASFWD16RST	0
#define ASFCAPMASK	0
#define ASFALERTRESND	0
#define ASFLSNRPOLLCYC	0
#define ASFSNRPOLLCYC	0
#define ASFWD8RESET	0
#define ASFRWHEXNUM	0

#define FMW_CAP_MASK	0x0000F867
#define SPC_CMD_MASK	0x1F00
#define SYS_CAP_MASK	0xFF
#define DISABLE_MASK	0x00

#define MAX_DATA_LEN	200
#define MAX_STR_LEN	200

#define COMMU_STR_MAX_LEN	23

#define KEY_LEN		20
#define UUID_LEN	16
#define SYSID_LEN	2

#define RW_ONE_BYTE	1
#define RW_TWO_BYTES	2
#define RW_FOUR_BYTES	4

enum asf_registers {
        HBPeriod	= 0x0000,
        WD8Rst		= 0x0002,
        WD8Timer	= 0x0003,
        WD16Rst		= 0x0004,
        LSnsrPollCycle	= 0x0006,
        ASFSnsrPollPrd	= 0x0007,
        AlertReSendCnt	= 0x0008,
        AlertReSendItvl	= 0x0009,
        SMBAddr		= 0x000A,
        SMBCap		= 0x000B,
        ASFConfigR0	= 0x000C,
        ASFConfigR1	= 0x000D,
        WD16Timer	= 0x000E,
        ConsoleMA	= 0x0010,
        ConsoleIP	= 0x0016,
        IPAddr		= 0x001A,

        UUID		= 0x0020,
        IANA		= 0x0030,
        SysID		= 0x0034,
        Community	= 0x0036,
        StringLength	= 0x004D,
        LC		= 0x004E,
        EntityInst	= 0x004F,
        FmCapMsk	= 0x0050,
        SpCMDMsk	= 0x0054,
        SysCapMsk	= 0x0056,
        WDSysSt		= 0x0057,
        RxMsgType	= 0x0058,
        RxSpCMD		= 0x0059,
        RxSpCMDPa	= 0x005A,
        RxBtOpMsk	= 0x005C,
        RmtRstAddr	= 0x005E,
        RmtRstCmd	= 0x005F,
        RmtRstData	= 0x0060,
        RmtPwrOffAddr	= 0x0061,
        RmtPwrOffCmd	= 0x0062,
        RmtPwrOffData	= 0x0063,
        RmtPwrOnAddr	= 0x0064,
        RmtPwrOnCmd	= 0x0065,
        RmtPwrOnData	= 0x0066,
        RmtPCRAddr	= 0x0067,
        RmtPCRCmd	= 0x0068,
        RmtPCRData	= 0x0069,
        RMCP_IANA	= 0x006A,
        RMCP_OEM	= 0x006E,
        ASFSnsr0Addr	= 0x0070,

        ASFSnsrEvSt	= 0x0073,
        ASFSnsrEvAlert	= 0x0081,

        LSnsrNo		= 0x00AD,
        AssrtEvntMsk	= 0x00AE,
        DeAssrtEvntMsk	= 0x00AF,

        LSnsrAddr0	= 0x00B0,
        LAlertCMD0	= 0x00B1,
        LAlertDataMsk0	= 0x00B2,
        LAlertCmp0	= 0x00B3,
        LAlertESnsrT0	= 0x00B4,
        LAlertET0	= 0x00B5,
        LAlertEOffset0	= 0x00B6,
        LAlertES0	= 0x00B7,
        LAlertSN0	= 0x00B8,
        LAlertEntity0	= 0x00B9,
        LAlertEI0	= 0x00BA,
        LSnsrState0	= 0x00BB,

        LSnsrAddr1	= 0x00BD,
        LAlertCMD1	= 0x00BE,
        LAlertDataMsk1	= 0x00BF,
        LAlertCmp1	= 0x00C0,
        LAlertESnsrT1	= 0x00C1,
        LAlertET1	= 0x00C2,
        LAlertEOffset1	= 0x00C3,
        LAlertES1	= 0x00C4,
        LAlertSN1	= 0x00C5,
        LAlertEntity1	= 0x00C6,
        LAlertEI1	= 0x00C7,
        LSnsrState1	= 0x00C8,

        LSnsrAddr2	= 0x00CA,
        LAlertCMD2	= 0x00CB,
        LAlertDataMsk2	= 0x00CC,
        LAlertCmp2	= 0x00CD,
        LAlertESnsrT2	= 0x00CE,
        LAlertET2	= 0x00CF,
        LAlertEOffset2	= 0x00D0,
        LAlertES2	= 0x00D1,
        LAlertSN2	= 0x00D2,
        LAlertEntity2	= 0x00D3,
        LAlertEI2	= 0x00D4,
        LSnsrState2	= 0x00D5,

        LSnsrAddr3	= 0x00D7,
        LAlertCMD3	= 0x00D8,
        LAlertDataMsk3	= 0x00D9,
        LAlertCmp3	= 0x00DA,
        LAlertESnsrT3	= 0x00DB,
        LAlertET3	= 0x00DC,
        LAlertEOffset3	= 0x00DD,
        LAlertES3	= 0x00DE,
        LAlertSN3	= 0x00DF,
        LAlertEntity3	= 0x00E0,
        LAlertEI3	= 0x00E1,
        LSnsrState3	= 0x00E2,

        LSnsrAddr4	= 0x00E4,
        LAlertCMD4	= 0x00E5,
        LAlertDataMsk4	= 0x00E6,
        LAlertCmp4	= 0x00E7,
        LAlertESnsrT4	= 0x00E8,
        LAlertET4	= 0x00E9,
        LAlertEOffset4	= 0x00EA,
        LAlertES4	= 0x00EB,
        LAlertSN4	= 0x00EC,
        LAlertEntity4	= 0x00ED,
        LAlertEI4	= 0x00EE,
        LSnsrState4	= 0x00EF,

        LSnsrAddr5	= 0x00F1,
        LAlertCMD5	= 0x00F2,
        LAlertDataMsk5	= 0x00F3,
        LAlertCmp5	= 0x00F4,
        LAlertESnsrT5	= 0x00F5,
        LAlertET5	= 0x00F6,
        LAlertEOffset5	= 0x00F7,
        LAlertES5	= 0x00F8,
        LAlertSN5	= 0x00F9,
        LAlertEntity5	= 0x00FA,
        LAlertEI5	= 0x00FB,
        LSnsrState5	= 0x00FC,

        LSnsrAddr6	= 0x00FE,
        LAlertCMD6	= 0x00FF,
        LAlertDataMsk6	= 0x0100,
        LAlertCmp6	= 0x0101,
        LAlertESnsrT6	= 0x0102,
        LAlertET6	= 0x0103,
        LAlertEOffset6	= 0x0104,
        LAlertES6	= 0x0105,
        LAlertSN6	= 0x0106,
        LAlertEntity6	= 0x0107,
        LAlertEI6	= 0x0108,
        LSnsrState6	= 0x0109,

        LSnsrAddr7	= 0x010B,
        LAlertCMD7	= 0x010C,
        LAlertDataMsk7	= 0x010D,
        LAlertCmp7	= 0x010E,
        LAlertESnsrT7	= 0x010F,
        LAlertET7	= 0x0110,
        LAlertEOffset7	= 0x0111,
        LAlertES7	= 0x0112,
        LAlertSN7	= 0x0113,
        LAlertEntity7	= 0x0114,
        LAlertEI7	= 0x0115,
        LSnsrState7	= 0x0116,
        LAssert		= 0x0117,
        LDAssert	= 0x0118,
        IPServiceType	= 0x0119,
        IPIdfr		= 0x011A,
        FlagFOffset	= 0x011C,
        TTL		= 0x011E,
        HbtEI		= 0x011F,
        MgtConSID1	= 0x0120,
        MgtConSID2	= 0x0124,
        MgdCltSID	= 0x0128,
        StCd		= 0x012C,
        MgtConUR	= 0x012D,
        MgtConUNL	= 0x012E,

        AuthPd		= 0x0130,
        IntyPd		= 0x0138,
        MgtConRN	= 0x0140,
        MgdCtlRN	= 0x0150,
        MgtConUN	= 0x0160,
        Rakp2IntCk	= 0x0170,
        KO		= 0x017C,
        KA		= 0x0190,
        KG		= 0x01A4,
        KR		= 0x01B8,
        CP		= 0x01CC,
        CQ		= 0x01D0,
        KC		= 0x01D4,
        ConsoleSid	= 0x01E8,

        SIK1		= 0x01FC,
        SIK2		= 0x0210,
        Udpsrc_port	= 0x0224,
        Udpdes_port	= 0x0226,
        Asf_debug_mux	= 0x0228
};

enum asf_cmdln_opt {
        ASF_GET,
        ASF_SET,
        ASF_HELP
};

struct asf_ioctl_struct {
        unsigned int arg;
        unsigned int offset;
        union {
                unsigned int data[MAX_DATA_LEN];
                char string[MAX_STR_LEN];
        } u;
};

int rtl8168_asf_ioctl(struct net_device *dev, struct ifreq *ifr);
void rtl8168_asf_hbperiod(struct rtl8168_private *tp, int arg, unsigned int *data);
void rtl8168_asf_wd16rst(struct rtl8168_private *tp, int arg, unsigned int *data);
void rtl8168_asf_console_mac(struct rtl8168_private *, int arg, unsigned int *data);
void rtl8168_asf_ip_address(struct rtl8168_private *, int arg, int offset, unsigned int *data);
void rtl8168_asf_config_regs(struct rtl8168_private *tp, int arg, int offset, unsigned int *data);
void rtl8168_asf_capability_masks(struct rtl8168_private *tp, int arg, int offset, unsigned int *data);
void rtl8168_asf_community_string(struct rtl8168_private *tp, int arg, char *string);
void rtl8168_asf_community_string_len(struct rtl8168_private *tp, int arg, unsigned int *data);
void rtl8168_asf_alert_resend_interval(struct rtl8168_private *tp, int arg, unsigned int *data);
void rtl8168_asf_time_period(struct rtl8168_private *tp, int arg, int offset, unsigned int *data);
void rtl8168_asf_key_access(struct rtl8168_private *, int arg, int offset, unsigned int *data);
void rtl8168_asf_rw_hexadecimal(struct rtl8168_private *tp, int arg, int offset, int len, unsigned int *data);
void rtl8168_asf_rw_iana(struct rtl8168_private *tp, int arg, unsigned int *data);
void rtl8168_asf_rw_uuid(struct rtl8168_private *tp, int arg, unsigned int *data);
void rtl8168_asf_rw_systemid(struct rtl8168_private *tp, int arg, unsigned int *data);
