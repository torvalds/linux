/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code
 * distributions retain the above copyright notice and this paragraph
 * in its entirety, and (2) distributions including binary code include
 * the above copyright notice and this paragraph in its entirety in
 * the documentation or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND
 * WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, WITHOUT
 * LIMITATION, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 *
 * Original code by Hannes Gredler (hannes@gredler.at)
 */

extern const struct tok oui_values[];
extern const struct tok smi_values[];

#define OUI_ENCAP_ETHER       0x000000  /* encapsulated Ethernet */
#define OUI_CISCO             0x00000c  /* Cisco protocols */
#define OUI_IANA              0x00005E  /* IANA */
#define OUI_NORTEL            0x000081  /* Nortel SONMP */
#define OUI_CISCO_90          0x0000f8  /* Cisco bridging */
#define OUI_RFC2684           0x0080c2  /* RFC 2427/2684 bridged Ethernet */
#define OUI_ATM_FORUM         0x00A03E  /* ATM Forum */
#define OUI_CABLE_BPDU        0x00E02F  /* DOCSIS spanning tree BPDU */
#define OUI_APPLETALK         0x080007  /* Appletalk */
#define OUI_JUNIPER           0x009069  /* Juniper */
#define OUI_HP                0x080009  /* Hewlett-Packard */
#define OUI_IEEE_8021_PRIVATE 0x0080c2  /* IEEE 802.1 Organisation Specific - Annex F */
#define OUI_IEEE_8023_PRIVATE 0x00120f  /* IEEE 802.3 Organisation Specific - Annex G */
#define OUI_TIA               0x0012bb  /* TIA - Telecommunications Industry Association - ANSI/TIA-1057- 2006 */
#define OUI_DCBX              0x001B21  /* DCBX */
#define OUI_NICIRA            0x002320  /* Nicira Networks */
#define OUI_BSN               0x5c16c7  /* Big Switch Networks */
#define OUI_VELLO             0xb0d2f5  /* Vello Systems */
#define OUI_HP2               0x002481  /* HP too */
#define OUI_HPLABS            0x0004ea  /* HP-Labs */
#define OUI_INFOBLOX          0x748771  /* Infoblox Inc */
#define OUI_ONLAB             0xa42305  /* Open Networking Lab */
#define OUI_FREESCALE         0x00049f  /* Freescale */
#define OUI_NETRONOME         0x0015ad  /* Netronome */

/*
 * These are SMI Network Management Private Enterprise Codes for
 * organizations; see
 *
 *	http://www.iana.org/assignments/enterprise-numbers
 *
 * for a list.
 *
 * List taken from Ethereal's epan/sminmpec.h.
 */
#define SMI_IETF                     0 /* reserved - used by the IETF in L2TP? */
#define SMI_ACC                      5
#define SMI_CISCO                    9
#define SMI_HEWLETT_PACKARD          11
#define SMI_SUN_MICROSYSTEMS         42
#define SMI_MERIT                    61
#define SMI_SHIVA                    166
#define SMI_ERICSSON                 193
#define SMI_CISCO_VPN5000            255
#define SMI_LIVINGSTON               307
#define SMI_MICROSOFT                311
#define SMI_3COM                     429
#define SMI_ASCEND                   529
#define SMI_BAY                      1584
#define SMI_FOUNDRY                  1991
#define SMI_VERSANET                 2180
#define SMI_REDBACK                  2352
#define SMI_JUNIPER                  2636
#define SMI_APTIS                    2637
#define SMI_CISCO_VPN3000            3076
#define SMI_COSINE                   3085
#define SMI_SHASTA                   3199
#define SMI_NETSCREEN                3224
#define SMI_NOMADIX                  3309
#define SMI_SIEMENS                  4329
#define SMI_CABLELABS                4491
#define SMI_UNISPHERE                4874
#define SMI_CISCO_BBSM               5263
#define SMI_THE3GPP2                 5535
#define SMI_IP_UNPLUGGED             5925
#define SMI_ISSANNI                  5948
#define SMI_QUINTUM                  6618
#define SMI_INTERLINK                6728
#define SMI_COLUBRIS                 8744
#define SMI_COLUMBIA_UNIVERSITY      11862
#define SMI_THE3GPP                  10415
#define SMI_GEMTEK_SYSTEMS           10529
#define SMI_WIFI_ALLIANCE            14122
