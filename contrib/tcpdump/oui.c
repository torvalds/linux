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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>
#include "netdissect.h"
#include "oui.h"

/* FIXME complete OUI list using a script */

const struct tok oui_values[] = {
    { OUI_ENCAP_ETHER, "Ethernet" },
    { OUI_CISCO, "Cisco" },
    { OUI_IANA, "IANA" },
    { OUI_NORTEL, "Nortel Networks SONMP" },
    { OUI_CISCO_90, "Cisco bridged" },
    { OUI_RFC2684, "Ethernet bridged" },
    { OUI_ATM_FORUM, "ATM Forum" },
    { OUI_CABLE_BPDU, "DOCSIS Spanning Tree" },
    { OUI_APPLETALK, "Appletalk" },
    { OUI_JUNIPER, "Juniper" },
    { OUI_HP, "Hewlett-Packard" },
    { OUI_IEEE_8021_PRIVATE, "IEEE 802.1 Private"},
    { OUI_IEEE_8023_PRIVATE, "IEEE 802.3 Private"},
    { OUI_TIA, "ANSI/TIA"},
    { OUI_DCBX, "DCBX"},
    { OUI_NICIRA, "Nicira Networks" },
    { OUI_BSN, "Big Switch Networks" },
    { OUI_VELLO, "Vello Systems" },
    { OUI_HP2, "HP" },
    { OUI_HPLABS, "HP-Labs" },
    { OUI_INFOBLOX, "Infoblox Inc" },
    { OUI_ONLAB, "Open Networking Lab" },
    { OUI_FREESCALE, "Freescale" },
    { OUI_NETRONOME, "Netronome" },
    { 0, NULL }
};

/*
 * SMI Network Management Private Enterprise Codes for organizations.
 *
 * XXX - these also appear in FreeRadius dictionary files, with items such
 * as
 *
 *	VENDOR          Cisco           9
 *
 * List taken from Ethereal's epan/sminmpec.c.
 */
const struct tok smi_values[] = {
    { SMI_IETF,                 "IETF (reserved)"},
    { SMI_ACC,                  "ACC"},
    { SMI_CISCO,                "Cisco"},
    { SMI_HEWLETT_PACKARD,      "Hewlett Packard"},
    { SMI_SUN_MICROSYSTEMS,     "Sun Microsystems"},
    { SMI_MERIT,                "Merit"},
    { SMI_SHIVA,                "Shiva"},
    { SMI_ERICSSON,             "Ericsson AB"},
    { SMI_CISCO_VPN5000,        "Cisco VPN 5000"},
    { SMI_LIVINGSTON,           "Livingston"},
    { SMI_MICROSOFT,            "Microsoft"},
    { SMI_3COM,                 "3Com"},
    { SMI_ASCEND,               "Ascend"},
    { SMI_BAY,                  "Bay Networks"},
    { SMI_FOUNDRY,              "Foundry"},
    { SMI_VERSANET,             "Versanet"},
    { SMI_REDBACK,              "Redback"},
    { SMI_JUNIPER,              "Juniper Networks"},
    { SMI_APTIS,                "Aptis"},
    { SMI_CISCO_VPN3000,        "Cisco VPN 3000"},
    { SMI_COSINE,               "CoSine Communications"},
    { SMI_NETSCREEN,            "Netscreen"},
    { SMI_SHASTA,               "Shasta"},
    { SMI_NOMADIX,              "Nomadix"},
    { SMI_SIEMENS,              "Siemens"},
    { SMI_CABLELABS,            "CableLabs"},
    { SMI_UNISPHERE,            "Unisphere Networks"},
    { SMI_CISCO_BBSM,           "Cisco BBSM"},
    { SMI_THE3GPP2,             "3rd Generation Partnership Project 2 (3GPP2)"},
    { SMI_IP_UNPLUGGED,         "ipUnplugged"},
    { SMI_ISSANNI,              "Issanni Communications"},
    { SMI_QUINTUM,              "Quintum"},
    { SMI_INTERLINK,            "Interlink"},
    { SMI_COLUBRIS,             "Colubris"},
    { SMI_COLUMBIA_UNIVERSITY,  "Columbia University"},
    { SMI_THE3GPP,              "3GPP"},
    { SMI_GEMTEK_SYSTEMS,       "Gemtek-Systems"},
    { SMI_WIFI_ALLIANCE,        "Wi-Fi Alliance"},
    { 0, NULL}
};
