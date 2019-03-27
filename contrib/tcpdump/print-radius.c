/*
 * Copyright (C) 2000 Alfredo Andres Omella.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in
 *      the documentation and/or other materials provided with the
 *      distribution.
 *   3. The names of the authors may not be used to endorse or promote
 *      products derived from this software without specific prior
 *      written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/* \summary: Radius protocol printer */

/*
 * Radius printer routines as specified on:
 *
 * RFC 2865:
 *      "Remote Authentication Dial In User Service (RADIUS)"
 *
 * RFC 2866:
 *      "RADIUS Accounting"
 *
 * RFC 2867:
 *      "RADIUS Accounting Modifications for Tunnel Protocol Support"
 *
 * RFC 2868:
 *      "RADIUS Attributes for Tunnel Protocol Support"
 *
 * RFC 2869:
 *      "RADIUS Extensions"
 *
 * RFC 3580:
 *      "IEEE 802.1X Remote Authentication Dial In User Service (RADIUS)"
 *      "Usage Guidelines"
 *
 * RFC 4675:
 *      "RADIUS Attributes for Virtual LAN and Priority Support"
 *
 * RFC 5176:
 *      "Dynamic Authorization Extensions to RADIUS"
 *
 * Alfredo Andres Omella (aandres@s21sec.com) v0.1 2000/09/15
 *
 * TODO: Among other things to print ok MacIntosh and Vendor values
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <string.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"
#include "oui.h"

static const char tstr[] = " [|radius]";

#define TAM_SIZE(x) (sizeof(x)/sizeof(x[0]) )

#define PRINT_HEX(bytes_len, ptr_data)                               \
           while(bytes_len)                                          \
           {                                                         \
              ND_PRINT((ndo, "%02X", *ptr_data ));                   \
              ptr_data++;                                            \
              bytes_len--;                                           \
           }


/* Radius packet codes */
#define RADCMD_ACCESS_REQ   1 /* Access-Request      */
#define RADCMD_ACCESS_ACC   2 /* Access-Accept       */
#define RADCMD_ACCESS_REJ   3 /* Access-Reject       */
#define RADCMD_ACCOUN_REQ   4 /* Accounting-Request  */
#define RADCMD_ACCOUN_RES   5 /* Accounting-Response */
#define RADCMD_ACCESS_CHA  11 /* Access-Challenge    */
#define RADCMD_STATUS_SER  12 /* Status-Server       */
#define RADCMD_STATUS_CLI  13 /* Status-Client       */
#define RADCMD_DISCON_REQ  40 /* Disconnect-Request  */
#define RADCMD_DISCON_ACK  41 /* Disconnect-ACK      */
#define RADCMD_DISCON_NAK  42 /* Disconnect-NAK      */
#define RADCMD_COA_REQ     43 /* CoA-Request         */
#define RADCMD_COA_ACK     44 /* CoA-ACK             */
#define RADCMD_COA_NAK     45 /* CoA-NAK             */
#define RADCMD_RESERVED   255 /* Reserved            */

static const struct tok radius_command_values[] = {
    { RADCMD_ACCESS_REQ, "Access-Request" },
    { RADCMD_ACCESS_ACC, "Access-Accept" },
    { RADCMD_ACCESS_REJ, "Access-Reject" },
    { RADCMD_ACCOUN_REQ, "Accounting-Request" },
    { RADCMD_ACCOUN_RES, "Accounting-Response" },
    { RADCMD_ACCESS_CHA, "Access-Challenge" },
    { RADCMD_STATUS_SER, "Status-Server" },
    { RADCMD_STATUS_CLI, "Status-Client" },
    { RADCMD_DISCON_REQ, "Disconnect-Request" },
    { RADCMD_DISCON_ACK, "Disconnect-ACK" },
    { RADCMD_DISCON_NAK, "Disconnect-NAK" },
    { RADCMD_COA_REQ,    "CoA-Request" },
    { RADCMD_COA_ACK,    "CoA-ACK" },
    { RADCMD_COA_NAK,    "CoA-NAK" },
    { RADCMD_RESERVED,   "Reserved" },
    { 0, NULL}
};

/********************************/
/* Begin Radius Attribute types */
/********************************/
#define SERV_TYPE    6
#define FRM_IPADDR   8
#define LOG_IPHOST  14
#define LOG_SERVICE 15
#define FRM_IPX     23
#define SESSION_TIMEOUT   27
#define IDLE_TIMEOUT      28
#define FRM_ATALK_LINK    37
#define FRM_ATALK_NETWORK 38

#define ACCT_DELAY        41
#define ACCT_SESSION_TIME 46

#define EGRESS_VLAN_ID   56
#define EGRESS_VLAN_NAME 58

#define TUNNEL_TYPE        64
#define TUNNEL_MEDIUM      65
#define TUNNEL_CLIENT_END  66
#define TUNNEL_SERVER_END  67
#define TUNNEL_PASS        69

#define ARAP_PASS          70
#define ARAP_FEATURES      71

#define TUNNEL_PRIV_GROUP  81
#define TUNNEL_ASSIGN_ID   82
#define TUNNEL_PREFERENCE  83

#define ARAP_CHALLENGE_RESP 84
#define ACCT_INT_INTERVAL   85

#define TUNNEL_CLIENT_AUTH 90
#define TUNNEL_SERVER_AUTH 91
/********************************/
/* End Radius Attribute types */
/********************************/

#define RFC4675_TAGGED   0x31
#define RFC4675_UNTAGGED 0x32

static const struct tok rfc4675_tagged[] = {
    { RFC4675_TAGGED,   "Tagged" },
    { RFC4675_UNTAGGED, "Untagged" },
    { 0, NULL}
};


static void print_attr_string(netdissect_options *, register const u_char *, u_int, u_short );
static void print_attr_num(netdissect_options *, register const u_char *, u_int, u_short );
static void print_vendor_attr(netdissect_options *, register const u_char *, u_int, u_short );
static void print_attr_address(netdissect_options *, register const u_char *, u_int, u_short);
static void print_attr_time(netdissect_options *, register const u_char *, u_int, u_short);
static void print_attr_strange(netdissect_options *, register const u_char *, u_int, u_short);


struct radius_hdr { uint8_t  code; /* Radius packet code  */
                    uint8_t  id;   /* Radius packet id    */
                    uint16_t len;  /* Radius total length */
                    uint8_t  auth[16]; /* Authenticator   */
                  };

#define MIN_RADIUS_LEN	20

struct radius_attr { uint8_t type; /* Attribute type   */
                     uint8_t len;  /* Attribute length */
                   };


/* Service-Type Attribute standard values */
static const char *serv_type[]={ NULL,
                                "Login",
                                "Framed",
                                "Callback Login",
                                "Callback Framed",
                                "Outbound",
                                "Administrative",
                                "NAS Prompt",
                                "Authenticate Only",
                                "Callback NAS Prompt",
                                "Call Check",
                                "Callback Administrative",
                               };

/* Framed-Protocol Attribute standard values */
static const char *frm_proto[]={ NULL,
                                 "PPP",
                                 "SLIP",
                                 "ARAP",
                                 "Gandalf proprietary",
                                 "Xylogics IPX/SLIP",
                                 "X.75 Synchronous",
                               };

/* Framed-Routing Attribute standard values */
static const char *frm_routing[]={ "None",
                                   "Send",
                                   "Listen",
                                   "Send&Listen",
                                 };

/* Framed-Compression Attribute standard values */
static const char *frm_comp[]={ "None",
                                "VJ TCP/IP",
                                "IPX",
                                "Stac-LZS",
                              };

/* Login-Service Attribute standard values */
static const char *login_serv[]={ "Telnet",
                                  "Rlogin",
                                  "TCP Clear",
                                  "PortMaster(proprietary)",
                                  "LAT",
                                  "X.25-PAD",
                                  "X.25-T3POS",
                                  "Unassigned",
                                  "TCP Clear Quiet",
                                };


/* Termination-Action Attribute standard values */
static const char *term_action[]={ "Default",
                                   "RADIUS-Request",
                                 };

/* Ingress-Filters Attribute standard values */
static const char *ingress_filters[]={ NULL,
                                       "Enabled",
                                       "Disabled",
                                     };

/* NAS-Port-Type Attribute standard values */
static const char *nas_port_type[]={ "Async",
                                     "Sync",
                                     "ISDN Sync",
                                     "ISDN Async V.120",
                                     "ISDN Async V.110",
                                     "Virtual",
                                     "PIAFS",
                                     "HDLC Clear Channel",
                                     "X.25",
                                     "X.75",
                                     "G.3 Fax",
                                     "SDSL",
                                     "ADSL-CAP",
                                     "ADSL-DMT",
                                     "ISDN-DSL",
                                     "Ethernet",
                                     "xDSL",
                                     "Cable",
                                     "Wireless - Other",
                                     "Wireless - IEEE 802.11",
                                   };

/* Acct-Status-Type Accounting Attribute standard values */
static const char *acct_status[]={ NULL,
                                   "Start",
                                   "Stop",
                                   "Interim-Update",
                                   "Unassigned",
                                   "Unassigned",
                                   "Unassigned",
                                   "Accounting-On",
                                   "Accounting-Off",
                                   "Tunnel-Start",
                                   "Tunnel-Stop",
                                   "Tunnel-Reject",
                                   "Tunnel-Link-Start",
                                   "Tunnel-Link-Stop",
                                   "Tunnel-Link-Reject",
                                   "Failed",
                                 };

/* Acct-Authentic Accounting Attribute standard values */
static const char *acct_auth[]={ NULL,
                                 "RADIUS",
                                 "Local",
                                 "Remote",
                               };

/* Acct-Terminate-Cause Accounting Attribute standard values */
static const char *acct_term[]={ NULL,
                                 "User Request",
                                 "Lost Carrier",
                                 "Lost Service",
                                 "Idle Timeout",
                                 "Session Timeout",
                                 "Admin Reset",
                                 "Admin Reboot",
                                 "Port Error",
                                 "NAS Error",
                                 "NAS Request",
                                 "NAS Reboot",
                                 "Port Unneeded",
                                 "Port Preempted",
                                 "Port Suspended",
                                 "Service Unavailable",
                                 "Callback",
                                 "User Error",
                                 "Host Request",
                               };

/* Tunnel-Type Attribute standard values */
static const char *tunnel_type[]={ NULL,
                                   "PPTP",
                                   "L2F",
                                   "L2TP",
                                   "ATMP",
                                   "VTP",
                                   "AH",
                                   "IP-IP",
                                   "MIN-IP-IP",
                                   "ESP",
                                   "GRE",
                                   "DVS",
                                   "IP-in-IP Tunneling",
                                   "VLAN",
                                 };

/* Tunnel-Medium-Type Attribute standard values */
static const char *tunnel_medium[]={ NULL,
                                     "IPv4",
                                     "IPv6",
                                     "NSAP",
                                     "HDLC",
                                     "BBN 1822",
                                     "802",
                                     "E.163",
                                     "E.164",
                                     "F.69",
                                     "X.121",
                                     "IPX",
                                     "Appletalk",
                                     "Decnet IV",
                                     "Banyan Vines",
                                     "E.164 with NSAP subaddress",
                                   };

/* ARAP-Zone-Access Attribute standard values */
static const char *arap_zone[]={ NULL,
                                 "Only access to dfl zone",
                                 "Use zone filter inc.",
                                 "Not used",
                                 "Use zone filter exc.",
                               };

static const char *prompt[]={ "No Echo",
                              "Echo",
                            };


static struct attrtype {
                  const char *name;      /* Attribute name                 */
                  const char **subtypes; /* Standard Values (if any)       */
                  u_char siz_subtypes;   /* Size of total standard values  */
                  u_char first_subtype;  /* First standard value is 0 or 1 */
                  void (*print_func)(netdissect_options *, register const u_char *, u_int, u_short);
                } attr_type[]=
  {
     { NULL,                              NULL, 0, 0, NULL               },
     { "User-Name",                       NULL, 0, 0, print_attr_string  },
     { "User-Password",                   NULL, 0, 0, NULL               },
     { "CHAP-Password",                   NULL, 0, 0, NULL               },
     { "NAS-IP-Address",                  NULL, 0, 0, print_attr_address },
     { "NAS-Port",                        NULL, 0, 0, print_attr_num     },
     { "Service-Type",                    serv_type, TAM_SIZE(serv_type)-1, 1, print_attr_num },
     { "Framed-Protocol",                 frm_proto, TAM_SIZE(frm_proto)-1, 1, print_attr_num },
     { "Framed-IP-Address",               NULL, 0, 0, print_attr_address },
     { "Framed-IP-Netmask",               NULL, 0, 0, print_attr_address },
     { "Framed-Routing",                  frm_routing, TAM_SIZE(frm_routing), 0, print_attr_num },
     { "Filter-Id",                       NULL, 0, 0, print_attr_string  },
     { "Framed-MTU",                      NULL, 0, 0, print_attr_num     },
     { "Framed-Compression",              frm_comp, TAM_SIZE(frm_comp),   0, print_attr_num },
     { "Login-IP-Host",                   NULL, 0, 0, print_attr_address },
     { "Login-Service",                   login_serv, TAM_SIZE(login_serv), 0, print_attr_num },
     { "Login-TCP-Port",                  NULL, 0, 0, print_attr_num     },
     { "Unassigned",                      NULL, 0, 0, NULL }, /*17*/
     { "Reply-Message",                   NULL, 0, 0, print_attr_string },
     { "Callback-Number",                 NULL, 0, 0, print_attr_string },
     { "Callback-Id",                     NULL, 0, 0, print_attr_string },
     { "Unassigned",                      NULL, 0, 0, NULL }, /*21*/
     { "Framed-Route",                    NULL, 0, 0, print_attr_string },
     { "Framed-IPX-Network",              NULL, 0, 0, print_attr_num    },
     { "State",                           NULL, 0, 0, print_attr_string },
     { "Class",                           NULL, 0, 0, print_attr_string },
     { "Vendor-Specific",                 NULL, 0, 0, print_vendor_attr },
     { "Session-Timeout",                 NULL, 0, 0, print_attr_num    },
     { "Idle-Timeout",                    NULL, 0, 0, print_attr_num    },
     { "Termination-Action",              term_action, TAM_SIZE(term_action), 0, print_attr_num },
     { "Called-Station-Id",               NULL, 0, 0, print_attr_string },
     { "Calling-Station-Id",              NULL, 0, 0, print_attr_string },
     { "NAS-Identifier",                  NULL, 0, 0, print_attr_string },
     { "Proxy-State",                     NULL, 0, 0, print_attr_string },
     { "Login-LAT-Service",               NULL, 0, 0, print_attr_string },
     { "Login-LAT-Node",                  NULL, 0, 0, print_attr_string },
     { "Login-LAT-Group",                 NULL, 0, 0, print_attr_string },
     { "Framed-AppleTalk-Link",           NULL, 0, 0, print_attr_num    },
     { "Framed-AppleTalk-Network",        NULL, 0, 0, print_attr_num    },
     { "Framed-AppleTalk-Zone",           NULL, 0, 0, print_attr_string },
     { "Acct-Status-Type",                acct_status, TAM_SIZE(acct_status)-1, 1, print_attr_num },
     { "Acct-Delay-Time",                 NULL, 0, 0, print_attr_num    },
     { "Acct-Input-Octets",               NULL, 0, 0, print_attr_num    },
     { "Acct-Output-Octets",              NULL, 0, 0, print_attr_num    },
     { "Acct-Session-Id",                 NULL, 0, 0, print_attr_string },
     { "Acct-Authentic",                  acct_auth, TAM_SIZE(acct_auth)-1, 1, print_attr_num },
     { "Acct-Session-Time",               NULL, 0, 0, print_attr_num },
     { "Acct-Input-Packets",              NULL, 0, 0, print_attr_num },
     { "Acct-Output-Packets",             NULL, 0, 0, print_attr_num },
     { "Acct-Terminate-Cause",            acct_term, TAM_SIZE(acct_term)-1, 1, print_attr_num },
     { "Acct-Multi-Session-Id",           NULL, 0, 0, print_attr_string },
     { "Acct-Link-Count",                 NULL, 0, 0, print_attr_num },
     { "Acct-Input-Gigawords",            NULL, 0, 0, print_attr_num },
     { "Acct-Output-Gigawords",           NULL, 0, 0, print_attr_num },
     { "Unassigned",                      NULL, 0, 0, NULL }, /*54*/
     { "Event-Timestamp",                 NULL, 0, 0, print_attr_time },
     { "Egress-VLANID",                   NULL, 0, 0, print_attr_num },
     { "Ingress-Filters",                 ingress_filters, TAM_SIZE(ingress_filters)-1, 1, print_attr_num },
     { "Egress-VLAN-Name",                NULL, 0, 0, print_attr_string },
     { "User-Priority-Table",             NULL, 0, 0, NULL },
     { "CHAP-Challenge",                  NULL, 0, 0, print_attr_string },
     { "NAS-Port-Type",                   nas_port_type, TAM_SIZE(nas_port_type), 0, print_attr_num },
     { "Port-Limit",                      NULL, 0, 0, print_attr_num },
     { "Login-LAT-Port",                  NULL, 0, 0, print_attr_string }, /*63*/
     { "Tunnel-Type",                     tunnel_type, TAM_SIZE(tunnel_type)-1, 1, print_attr_num },
     { "Tunnel-Medium-Type",              tunnel_medium, TAM_SIZE(tunnel_medium)-1, 1, print_attr_num },
     { "Tunnel-Client-Endpoint",          NULL, 0, 0, print_attr_string },
     { "Tunnel-Server-Endpoint",          NULL, 0, 0, print_attr_string },
     { "Acct-Tunnel-Connection",          NULL, 0, 0, print_attr_string },
     { "Tunnel-Password",                 NULL, 0, 0, print_attr_string  },
     { "ARAP-Password",                   NULL, 0, 0, print_attr_strange },
     { "ARAP-Features",                   NULL, 0, 0, print_attr_strange },
     { "ARAP-Zone-Access",                arap_zone, TAM_SIZE(arap_zone)-1, 1, print_attr_num }, /*72*/
     { "ARAP-Security",                   NULL, 0, 0, print_attr_string },
     { "ARAP-Security-Data",              NULL, 0, 0, print_attr_string },
     { "Password-Retry",                  NULL, 0, 0, print_attr_num    },
     { "Prompt",                          prompt, TAM_SIZE(prompt), 0, print_attr_num },
     { "Connect-Info",                    NULL, 0, 0, print_attr_string   },
     { "Configuration-Token",             NULL, 0, 0, print_attr_string   },
     { "EAP-Message",                     NULL, 0, 0, print_attr_string   },
     { "Message-Authenticator",           NULL, 0, 0, print_attr_string }, /*80*/
     { "Tunnel-Private-Group-ID",         NULL, 0, 0, print_attr_string },
     { "Tunnel-Assignment-ID",            NULL, 0, 0, print_attr_string },
     { "Tunnel-Preference",               NULL, 0, 0, print_attr_num    },
     { "ARAP-Challenge-Response",         NULL, 0, 0, print_attr_strange },
     { "Acct-Interim-Interval",           NULL, 0, 0, print_attr_num     },
     { "Acct-Tunnel-Packets-Lost",        NULL, 0, 0, print_attr_num }, /*86*/
     { "NAS-Port-Id",                     NULL, 0, 0, print_attr_string },
     { "Framed-Pool",                     NULL, 0, 0, print_attr_string },
     { "CUI",                             NULL, 0, 0, print_attr_string },
     { "Tunnel-Client-Auth-ID",           NULL, 0, 0, print_attr_string },
     { "Tunnel-Server-Auth-ID",           NULL, 0, 0, print_attr_string },
     { "Unassigned",                      NULL, 0, 0, NULL }, /*92*/
     { "Unassigned",                      NULL, 0, 0, NULL }  /*93*/
  };


/*****************************/
/* Print an attribute string */
/* value pointed by 'data'   */
/* and 'length' size.        */
/*****************************/
/* Returns nothing.          */
/*****************************/
static void
print_attr_string(netdissect_options *ndo,
                  register const u_char *data, u_int length, u_short attr_code)
{
   register u_int i;

   ND_TCHECK2(data[0],length);

   switch(attr_code)
   {
      case TUNNEL_PASS:
           if (length < 3)
              goto trunc;
           if (*data && (*data <=0x1F) )
              ND_PRINT((ndo, "Tag[%u] ", *data));
           else
              ND_PRINT((ndo, "Tag[Unused] "));
           data++;
           length--;
           ND_PRINT((ndo, "Salt %u ", EXTRACT_16BITS(data)));
           data+=2;
           length-=2;
        break;
      case TUNNEL_CLIENT_END:
      case TUNNEL_SERVER_END:
      case TUNNEL_PRIV_GROUP:
      case TUNNEL_ASSIGN_ID:
      case TUNNEL_CLIENT_AUTH:
      case TUNNEL_SERVER_AUTH:
           if (*data <= 0x1F)
           {
              if (length < 1)
                 goto trunc;
              if (*data)
                ND_PRINT((ndo, "Tag[%u] ", *data));
              else
                ND_PRINT((ndo, "Tag[Unused] "));
              data++;
              length--;
           }
        break;
      case EGRESS_VLAN_NAME:
           if (length < 1)
              goto trunc;
           ND_PRINT((ndo, "%s (0x%02x) ",
                  tok2str(rfc4675_tagged,"Unknown tag",*data),
                  *data));
           data++;
           length--;
        break;
   }

   for (i=0; i < length && *data; i++, data++)
       ND_PRINT((ndo, "%c", (*data < 32 || *data > 126) ? '.' : *data));

   return;

   trunc:
      ND_PRINT((ndo, "%s", tstr));
}

/*
 * print vendor specific attributes
 */
static void
print_vendor_attr(netdissect_options *ndo,
                  register const u_char *data, u_int length, u_short attr_code _U_)
{
    u_int idx;
    u_int vendor_id;
    u_int vendor_type;
    u_int vendor_length;

    if (length < 4)
        goto trunc;
    ND_TCHECK2(*data, 4);
    vendor_id = EXTRACT_32BITS(data);
    data+=4;
    length-=4;

    ND_PRINT((ndo, "Vendor: %s (%u)",
           tok2str(smi_values,"Unknown",vendor_id),
           vendor_id));

    while (length >= 2) {
	ND_TCHECK2(*data, 2);

        vendor_type = *(data);
        vendor_length = *(data+1);

        if (vendor_length < 2)
        {
            ND_PRINT((ndo, "\n\t    Vendor Attribute: %u, Length: %u (bogus, must be >= 2)",
                   vendor_type,
                   vendor_length));
            return;
        }
        if (vendor_length > length)
        {
            ND_PRINT((ndo, "\n\t    Vendor Attribute: %u, Length: %u (bogus, goes past end of vendor-specific attribute)",
                   vendor_type,
                   vendor_length));
            return;
        }
        data+=2;
        vendor_length-=2;
        length-=2;
	ND_TCHECK2(*data, vendor_length);

        ND_PRINT((ndo, "\n\t    Vendor Attribute: %u, Length: %u, Value: ",
               vendor_type,
               vendor_length));
        for (idx = 0; idx < vendor_length ; idx++, data++)
            ND_PRINT((ndo, "%c", (*data < 32 || *data > 126) ? '.' : *data));
        length-=vendor_length;
    }
    return;

   trunc:
     ND_PRINT((ndo, "%s", tstr));
}

/******************************/
/* Print an attribute numeric */
/* value pointed by 'data'    */
/* and 'length' size.         */
/******************************/
/* Returns nothing.           */
/******************************/
static void
print_attr_num(netdissect_options *ndo,
               register const u_char *data, u_int length, u_short attr_code)
{
   uint32_t timeout;

   if (length != 4)
   {
       ND_PRINT((ndo, "ERROR: length %u != 4", length));
       return;
   }

   ND_TCHECK2(data[0],4);
                          /* This attribute has standard values */
   if (attr_type[attr_code].siz_subtypes)
   {
      static const char **table;
      uint32_t data_value;
      table = attr_type[attr_code].subtypes;

      if ( (attr_code == TUNNEL_TYPE) || (attr_code == TUNNEL_MEDIUM) )
      {
         if (!*data)
            ND_PRINT((ndo, "Tag[Unused] "));
         else
            ND_PRINT((ndo, "Tag[%d] ", *data));
         data++;
         data_value = EXTRACT_24BITS(data);
      }
      else
      {
         data_value = EXTRACT_32BITS(data);
      }
      if ( data_value <= (uint32_t)(attr_type[attr_code].siz_subtypes - 1 +
            attr_type[attr_code].first_subtype) &&
	   data_value >= attr_type[attr_code].first_subtype )
         ND_PRINT((ndo, "%s", table[data_value]));
      else
         ND_PRINT((ndo, "#%u", data_value));
   }
   else
   {
      switch(attr_code) /* Be aware of special cases... */
      {
        case FRM_IPX:
             if (EXTRACT_32BITS( data) == 0xFFFFFFFE )
                ND_PRINT((ndo, "NAS Select"));
             else
                ND_PRINT((ndo, "%d", EXTRACT_32BITS(data)));
          break;

        case SESSION_TIMEOUT:
        case IDLE_TIMEOUT:
        case ACCT_DELAY:
        case ACCT_SESSION_TIME:
        case ACCT_INT_INTERVAL:
             timeout = EXTRACT_32BITS( data);
             if ( timeout < 60 )
                ND_PRINT((ndo,  "%02d secs", timeout));
             else
             {
                if ( timeout < 3600 )
                   ND_PRINT((ndo,  "%02d:%02d min",
                          timeout / 60, timeout % 60));
                else
                   ND_PRINT((ndo, "%02d:%02d:%02d hours",
                          timeout / 3600, (timeout % 3600) / 60,
                          timeout % 60));
             }
          break;

        case FRM_ATALK_LINK:
             if (EXTRACT_32BITS(data) )
                ND_PRINT((ndo, "%d", EXTRACT_32BITS(data)));
             else
                ND_PRINT((ndo, "Unnumbered"));
          break;

        case FRM_ATALK_NETWORK:
             if (EXTRACT_32BITS(data) )
                ND_PRINT((ndo, "%d", EXTRACT_32BITS(data)));
             else
                ND_PRINT((ndo, "NAS assigned"));
          break;

        case TUNNEL_PREFERENCE:
            if (*data)
               ND_PRINT((ndo, "Tag[%d] ", *data));
            else
               ND_PRINT((ndo, "Tag[Unused] "));
            data++;
            ND_PRINT((ndo, "%d", EXTRACT_24BITS(data)));
          break;

        case EGRESS_VLAN_ID:
            ND_PRINT((ndo, "%s (0x%02x) ",
                   tok2str(rfc4675_tagged,"Unknown tag",*data),
                   *data));
            data++;
            ND_PRINT((ndo, "%d", EXTRACT_24BITS(data)));
          break;

        default:
             ND_PRINT((ndo, "%d", EXTRACT_32BITS(data)));
          break;

      } /* switch */

   } /* if-else */

   return;

   trunc:
     ND_PRINT((ndo, "%s", tstr));
}

/*****************************/
/* Print an attribute IPv4   */
/* address value pointed by  */
/* 'data' and 'length' size. */
/*****************************/
/* Returns nothing.          */
/*****************************/
static void
print_attr_address(netdissect_options *ndo,
                   register const u_char *data, u_int length, u_short attr_code)
{
   if (length != 4)
   {
       ND_PRINT((ndo, "ERROR: length %u != 4", length));
       return;
   }

   ND_TCHECK2(data[0],4);

   switch(attr_code)
   {
      case FRM_IPADDR:
      case LOG_IPHOST:
           if (EXTRACT_32BITS(data) == 0xFFFFFFFF )
              ND_PRINT((ndo, "User Selected"));
           else
              if (EXTRACT_32BITS(data) == 0xFFFFFFFE )
                 ND_PRINT((ndo, "NAS Select"));
              else
                 ND_PRINT((ndo, "%s",ipaddr_string(ndo, data)));
      break;

      default:
          ND_PRINT((ndo, "%s", ipaddr_string(ndo, data)));
      break;
   }

   return;

   trunc:
     ND_PRINT((ndo, "%s", tstr));
}

/*************************************/
/* Print an attribute of 'secs since */
/* January 1, 1970 00:00 UTC' value  */
/* pointed by 'data' and 'length'    */
/* size.                             */
/*************************************/
/* Returns nothing.                  */
/*************************************/
static void
print_attr_time(netdissect_options *ndo,
                register const u_char *data, u_int length, u_short attr_code _U_)
{
   time_t attr_time;
   char string[26];

   if (length != 4)
   {
       ND_PRINT((ndo, "ERROR: length %u != 4", length));
       return;
   }

   ND_TCHECK2(data[0],4);

   attr_time = EXTRACT_32BITS(data);
   strlcpy(string, ctime(&attr_time), sizeof(string));
   /* Get rid of the newline */
   string[24] = '\0';
   ND_PRINT((ndo, "%.24s", string));
   return;

   trunc:
     ND_PRINT((ndo, "%s", tstr));
}

/***********************************/
/* Print an attribute of 'strange' */
/* data format pointed by 'data'   */
/* and 'length' size.              */
/***********************************/
/* Returns nothing.                */
/***********************************/
static void
print_attr_strange(netdissect_options *ndo,
                   register const u_char *data, u_int length, u_short attr_code)
{
   u_short len_data;

   switch(attr_code)
   {
      case ARAP_PASS:
           if (length != 16)
           {
               ND_PRINT((ndo, "ERROR: length %u != 16", length));
               return;
           }
           ND_PRINT((ndo, "User_challenge ("));
           ND_TCHECK2(data[0],8);
           len_data = 8;
           PRINT_HEX(len_data, data);
           ND_PRINT((ndo, ") User_resp("));
           ND_TCHECK2(data[0],8);
           len_data = 8;
           PRINT_HEX(len_data, data);
           ND_PRINT((ndo, ")"));
        break;

      case ARAP_FEATURES:
           if (length != 14)
           {
               ND_PRINT((ndo, "ERROR: length %u != 14", length));
               return;
           }
           ND_TCHECK2(data[0],1);
           if (*data)
              ND_PRINT((ndo, "User can change password"));
           else
              ND_PRINT((ndo, "User cannot change password"));
           data++;
           ND_TCHECK2(data[0],1);
           ND_PRINT((ndo, ", Min password length: %d", *data));
           data++;
           ND_PRINT((ndo, ", created at: "));
           ND_TCHECK2(data[0],4);
           len_data = 4;
           PRINT_HEX(len_data, data);
           ND_PRINT((ndo, ", expires in: "));
           ND_TCHECK2(data[0],4);
           len_data = 4;
           PRINT_HEX(len_data, data);
           ND_PRINT((ndo, ", Current Time: "));
           ND_TCHECK2(data[0],4);
           len_data = 4;
           PRINT_HEX(len_data, data);
        break;

      case ARAP_CHALLENGE_RESP:
           if (length < 8)
           {
               ND_PRINT((ndo, "ERROR: length %u != 8", length));
               return;
           }
           ND_TCHECK2(data[0],8);
           len_data = 8;
           PRINT_HEX(len_data, data);
        break;
   }
   return;

   trunc:
     ND_PRINT((ndo, "%s", tstr));
}

static void
radius_attrs_print(netdissect_options *ndo,
                   register const u_char *attr, u_int length)
{
   register const struct radius_attr *rad_attr = (const struct radius_attr *)attr;
   const char *attr_string;

   while (length > 0)
   {
     if (length < 2)
        goto trunc;
     ND_TCHECK(*rad_attr);

     if (rad_attr->type > 0 && rad_attr->type < TAM_SIZE(attr_type))
	attr_string = attr_type[rad_attr->type].name;
     else
	attr_string = "Unknown";
     if (rad_attr->len < 2)
     {
	ND_PRINT((ndo, "\n\t  %s Attribute (%u), length: %u (bogus, must be >= 2)",
               attr_string,
               rad_attr->type,
               rad_attr->len));
	return;
     }
     if (rad_attr->len > length)
     {
	ND_PRINT((ndo, "\n\t  %s Attribute (%u), length: %u (bogus, goes past end of packet)",
               attr_string,
               rad_attr->type,
               rad_attr->len));
        return;
     }
     ND_PRINT((ndo, "\n\t  %s Attribute (%u), length: %u, Value: ",
            attr_string,
            rad_attr->type,
            rad_attr->len));

     if (rad_attr->type < TAM_SIZE(attr_type))
     {
         if (rad_attr->len > 2)
         {
             if ( attr_type[rad_attr->type].print_func )
                 (*attr_type[rad_attr->type].print_func)(
                     ndo, ((const u_char *)(rad_attr+1)),
                     rad_attr->len - 2, rad_attr->type);
         }
     }
     /* do we also want to see a hex dump ? */
     if (ndo->ndo_vflag> 1)
         print_unknown_data(ndo, (const u_char *)rad_attr+2, "\n\t    ", (rad_attr->len)-2);

     length-=(rad_attr->len);
     rad_attr = (const struct radius_attr *)( ((const char *)(rad_attr))+rad_attr->len);
   }
   return;

trunc:
   ND_PRINT((ndo, "%s", tstr));
}

void
radius_print(netdissect_options *ndo,
             const u_char *dat, u_int length)
{
   register const struct radius_hdr *rad;
   u_int len, auth_idx;

   ND_TCHECK2(*dat, MIN_RADIUS_LEN);
   rad = (const struct radius_hdr *)dat;
   len = EXTRACT_16BITS(&rad->len);

   if (len < MIN_RADIUS_LEN)
   {
	  ND_PRINT((ndo, "%s", tstr));
	  return;
   }

   if (len > length)
	  len = length;

   if (ndo->ndo_vflag < 1) {
       ND_PRINT((ndo, "RADIUS, %s (%u), id: 0x%02x length: %u",
              tok2str(radius_command_values,"Unknown Command",rad->code),
              rad->code,
              rad->id,
              len));
       return;
   }
   else {
       ND_PRINT((ndo, "RADIUS, length: %u\n\t%s (%u), id: 0x%02x, Authenticator: ",
              len,
              tok2str(radius_command_values,"Unknown Command",rad->code),
              rad->code,
              rad->id));

       for(auth_idx=0; auth_idx < 16; auth_idx++)
            ND_PRINT((ndo, "%02x", rad->auth[auth_idx]));
   }

   if (len > MIN_RADIUS_LEN)
      radius_attrs_print(ndo, dat + MIN_RADIUS_LEN, len - MIN_RADIUS_LEN);
   return;

trunc:
   ND_PRINT((ndo, "%s", tstr));
}
