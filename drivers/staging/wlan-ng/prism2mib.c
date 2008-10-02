/* src/prism2/driver/prism2mib.c
*
* Management request for mibset/mibget
*
* Copyright (C) 1999 AbsoluteValue Systems, Inc.  All Rights Reserved.
* --------------------------------------------------------------------
*
* linux-wlan
*
*   The contents of this file are subject to the Mozilla Public
*   License Version 1.1 (the "License"); you may not use this file
*   except in compliance with the License. You may obtain a copy of
*   the License at http://www.mozilla.org/MPL/
*
*   Software distributed under the License is distributed on an "AS
*   IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
*   implied. See the License for the specific language governing
*   rights and limitations under the License.
*
*   Alternatively, the contents of this file may be used under the
*   terms of the GNU Public License version 2 (the "GPL"), in which
*   case the provisions of the GPL are applicable instead of the
*   above.  If you wish to allow the use of your version of this file
*   only under the terms of the GPL and not to allow others to use
*   your version of this file under the MPL, indicate your decision
*   by deleting the provisions above and replace them with the notice
*   and other provisions required by the GPL.  If you do not delete
*   the provisions above, a recipient may use your version of this
*   file under either the MPL or the GPL.
*
* --------------------------------------------------------------------
*
* Inquiries regarding the linux-wlan Open Source project can be
* made directly to:
*
* AbsoluteValue Systems Inc.
* info@linux-wlan.com
* http://www.linux-wlan.com
*
* --------------------------------------------------------------------
*
* Portions of the development of this software were funded by
* Intersil Corporation as part of PRISM(R) chipset product development.
*
* --------------------------------------------------------------------
*
* The functions in this file handle the mibset/mibget management
* functions.
*
* --------------------------------------------------------------------
*/

/*================================================================*/
/* System Includes */
#define WLAN_DBVAR	prism2_debug

#include "version.h"


#include <linux/version.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/wireless.h>
#include <linux/netdevice.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <asm/byteorder.h>

#include "wlan_compat.h"

//#if (WLAN_HOSTIF == WLAN_PCMCIA)
//#include <pcmcia/version.h>
//#include <pcmcia/cs_types.h>
//#include <pcmcia/cs.h>
//#include <pcmcia/cistpl.h>
//#include <pcmcia/ds.h>
//#include <pcmcia/cisreg.h>
//#endif
//
//#if ((WLAN_HOSTIF == WLAN_PLX) || (WLAN_HOSTIF == WLAN_PCI))
//#include <linux/ioport.h>
//#include <linux/pci.h>
//endif

//#if (WLAN_HOSTIF == WLAN_USB)
#include <linux/usb.h>
//#endif

#include "wlan_compat.h"

/*================================================================*/
/* Project Includes */

#include "p80211types.h"
#include "p80211hdr.h"
#include "p80211mgmt.h"
#include "p80211conv.h"
#include "p80211msg.h"
#include "p80211netdev.h"
#include "p80211metadef.h"
#include "p80211metastruct.h"
#include "hfa384x.h"
#include "prism2mgmt.h"

/*================================================================*/
/* Local Constants */

#define MIB_TMP_MAXLEN    200    /* Max length of RID record (in bytes). */

/*================================================================*/
/* Local Types */

#define  F_AP         0x1        /* MIB is supported on Access Points. */
#define  F_STA        0x2        /* MIB is supported on stations. */
#define  F_READ       0x4        /* MIB may be read. */
#define  F_WRITE      0x8        /* MIB may be written. */

typedef struct mibrec
{
    UINT32   did;
    UINT16   flag;
    UINT16   parm1;
    UINT16   parm2;
    UINT16   parm3;
    int      (*func)(struct mibrec                *mib,
                     int                          isget,
                     wlandevice_t                 *wlandev,
                     hfa384x_t                    *hw,
                     p80211msg_dot11req_mibset_t  *msg,
                     void                         *data);
} mibrec_t;

/*================================================================*/
/* Local Function Declarations */

static int prism2mib_bytestr2pstr(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static int prism2mib_bytearea2pstr(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static int prism2mib_uint32(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static int prism2mib_uint32array(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static int prism2mib_uint32offset(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static int prism2mib_truth(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static int prism2mib_preamble(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static int prism2mib_flag(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static int prism2mib_appcfinfoflag(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static int prism2mib_regulatorydomains(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static int prism2mib_wepdefaultkey(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static int prism2mib_powermanagement(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static int prism2mib_privacyinvoked(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static int prism2mib_excludeunencrypted(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static int prism2mib_fragmentationthreshold(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static int prism2mib_operationalrateset(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static int prism2mib_groupaddress(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static int prism2mib_fwid(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static int prism2mib_authalg(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static int prism2mib_authalgenable(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static int prism2mib_priv(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data);

static void prism2mib_priv_authlist(
hfa384x_t      *hw,
prism2sta_authlist_t  *list);

static void prism2mib_priv_accessmode(
hfa384x_t         *hw,
UINT32            mode);

static void prism2mib_priv_accessallow(
hfa384x_t         *hw,
p80211macarray_t  *macarray);

static void prism2mib_priv_accessdeny(
hfa384x_t         *hw,
p80211macarray_t  *macarray);

static void prism2mib_priv_deauthenticate(
hfa384x_t         *hw,
UINT8             *addr);

/*================================================================*/
/* Local Static Definitions */

static mibrec_t mibtab[] = {

    /* dot11smt MIB's */

    { DIDmib_dot11smt_dot11StationConfigTable_dot11StationID,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFOWNMACADDR, HFA384x_RID_CNFOWNMACADDR_LEN, 0,
          prism2mib_bytearea2pstr },
    { DIDmib_dot11smt_dot11StationConfigTable_dot11MediumOccupancyLimit,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_CNFAPPCFINFO, HFA384x_RID_CNFAPPCFINFO_LEN, 0,
          prism2mib_uint32offset },
    { DIDmib_dot11smt_dot11StationConfigTable_dot11CFPollable,
          F_STA | F_READ,
          HFA384x_RID_CFPOLLABLE, 0, 0,
          prism2mib_uint32 },
    { DIDmib_dot11smt_dot11StationConfigTable_dot11CFPPeriod,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_CNFAPPCFINFO, HFA384x_RID_CNFAPPCFINFO_LEN, 1,
          prism2mib_uint32offset },
    { DIDmib_dot11smt_dot11StationConfigTable_dot11CFPMaxDuration,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_CNFAPPCFINFO, HFA384x_RID_CNFAPPCFINFO_LEN, 2,
          prism2mib_uint32offset },
    { DIDmib_dot11smt_dot11StationConfigTable_dot11AuthenticationResponseTimeOut,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFAUTHRSPTIMEOUT, 0, 0,
          prism2mib_uint32 },
    { DIDmib_dot11smt_dot11StationConfigTable_dot11PrivacyOptionImplemented,
          F_AP | F_STA | F_READ,
          HFA384x_RID_PRIVACYOPTIMP, 0, 0,
          prism2mib_uint32 },
    { DIDmib_dot11smt_dot11StationConfigTable_dot11PowerManagementMode,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFPMENABLED, 0, 0,
          prism2mib_powermanagement },
    { DIDmib_dot11smt_dot11StationConfigTable_dot11DesiredSSID,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFDESIREDSSID, HFA384x_RID_CNFDESIREDSSID_LEN, 0,
          prism2mib_bytestr2pstr },
    { DIDmib_dot11smt_dot11StationConfigTable_dot11DesiredBSSType,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_priv },
    { DIDmib_dot11smt_dot11StationConfigTable_dot11OperationalRateSet,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_TXRATECNTL, 0, 0,
          prism2mib_operationalrateset },
    { DIDmib_dot11smt_dot11StationConfigTable_dot11OperationalRateSet,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_TXRATECNTL0, 0, 0,
          prism2mib_operationalrateset },
    { DIDmib_dot11smt_dot11StationConfigTable_dot11BeaconPeriod,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_CNFAPBCNINT, 0, 0,
          prism2mib_uint32 },
    { DIDmib_dot11smt_dot11StationConfigTable_dot11DTIMPeriod,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFOWNDTIMPER, 0, 0,
          prism2mib_uint32 },
    { DIDmib_dot11smt_dot11StationConfigTable_dot11AssociationResponseTimeOut,
          F_AP | F_STA | F_READ,
          HFA384x_RID_PROTOCOLRSPTIME, 0, 0,
          prism2mib_uint32 },
    { DIDmib_dot11smt_dot11AuthenticationAlgorithmsTable_dot11AuthenticationAlgorithm1,
          F_AP | F_STA | F_READ,
          1, 0, 0,
          prism2mib_authalg },
    { DIDmib_dot11smt_dot11AuthenticationAlgorithmsTable_dot11AuthenticationAlgorithm2,
          F_AP | F_STA | F_READ,
          2, 0, 0,
          prism2mib_authalg },
    { DIDmib_dot11smt_dot11AuthenticationAlgorithmsTable_dot11AuthenticationAlgorithm3,
          F_AP | F_STA | F_READ,
          3, 0, 0,
          prism2mib_authalg },
    { DIDmib_dot11smt_dot11AuthenticationAlgorithmsTable_dot11AuthenticationAlgorithm4,
          F_AP | F_STA | F_READ,
          4, 0, 0,
          prism2mib_authalg },
    { DIDmib_dot11smt_dot11AuthenticationAlgorithmsTable_dot11AuthenticationAlgorithm5,
          F_AP | F_STA | F_READ,
          5, 0, 0,
          prism2mib_authalg },
    { DIDmib_dot11smt_dot11AuthenticationAlgorithmsTable_dot11AuthenticationAlgorithm6,
          F_AP | F_STA | F_READ,
          6, 0, 0,
          prism2mib_authalg },
    { DIDmib_dot11smt_dot11AuthenticationAlgorithmsTable_dot11AuthenticationAlgorithmsEnable1,
          F_AP | F_STA | F_READ | F_WRITE,
          1, 0, 0,
          prism2mib_authalgenable },
    { DIDmib_dot11smt_dot11AuthenticationAlgorithmsTable_dot11AuthenticationAlgorithmsEnable2,
          F_AP | F_STA | F_READ | F_WRITE,
          2, 0, 0,
          prism2mib_authalgenable },
    { DIDmib_dot11smt_dot11AuthenticationAlgorithmsTable_dot11AuthenticationAlgorithmsEnable3,
          F_AP | F_STA | F_READ | F_WRITE,
          3, 0, 0,
          prism2mib_authalgenable },
    { DIDmib_dot11smt_dot11AuthenticationAlgorithmsTable_dot11AuthenticationAlgorithmsEnable4,
          F_AP | F_STA | F_READ | F_WRITE,
          4, 0, 0,
          prism2mib_authalgenable },
    { DIDmib_dot11smt_dot11AuthenticationAlgorithmsTable_dot11AuthenticationAlgorithmsEnable5,
          F_AP | F_STA | F_READ | F_WRITE,
          5, 0, 0,
          prism2mib_authalgenable },
    { DIDmib_dot11smt_dot11AuthenticationAlgorithmsTable_dot11AuthenticationAlgorithmsEnable6,
          F_AP | F_STA | F_READ | F_WRITE,
          6, 0, 0,
          prism2mib_authalgenable },
    { DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey0,
          F_AP | F_STA | F_WRITE,
          HFA384x_RID_CNFWEPDEFAULTKEY0, 0, 0,
          prism2mib_wepdefaultkey },
    { DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey1,
          F_AP | F_STA | F_WRITE,
          HFA384x_RID_CNFWEPDEFAULTKEY1, 0, 0,
          prism2mib_wepdefaultkey },
    { DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey2,
          F_AP | F_STA | F_WRITE,
          HFA384x_RID_CNFWEPDEFAULTKEY2, 0, 0,
          prism2mib_wepdefaultkey },
    { DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey3,
          F_AP | F_STA | F_WRITE,
          HFA384x_RID_CNFWEPDEFAULTKEY3, 0, 0,
          prism2mib_wepdefaultkey },
    { DIDmib_dot11smt_dot11PrivacyTable_dot11PrivacyInvoked,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFWEPFLAGS, HFA384x_WEPFLAGS_PRIVINVOKED, 0,
          prism2mib_privacyinvoked },
    { DIDmib_dot11smt_dot11PrivacyTable_dot11WEPDefaultKeyID,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFWEPDEFAULTKEYID, 0, 0,
          prism2mib_uint32 },
    { DIDmib_dot11smt_dot11PrivacyTable_dot11ExcludeUnencrypted,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFWEPFLAGS, HFA384x_WEPFLAGS_EXCLUDE, 0,
          prism2mib_excludeunencrypted },
    { DIDmib_dot11phy_dot11PhyOperationTable_dot11ShortPreambleEnabled,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFSHORTPREAMBLE, 0, 0,
          prism2mib_preamble },

    /* dot11mac MIB's */

    { DIDmib_dot11mac_dot11OperationTable_dot11MACAddress,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFOWNMACADDR, HFA384x_RID_CNFOWNMACADDR_LEN, 0,
          prism2mib_bytearea2pstr },
    { DIDmib_dot11mac_dot11OperationTable_dot11RTSThreshold,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_RTSTHRESH, 0, 0,
          prism2mib_uint32 },
    { DIDmib_dot11mac_dot11OperationTable_dot11RTSThreshold,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_RTSTHRESH0, 0, 0,
          prism2mib_uint32 },
    { DIDmib_dot11mac_dot11OperationTable_dot11ShortRetryLimit,
          F_AP | F_STA | F_READ,
          HFA384x_RID_SHORTRETRYLIMIT, 0, 0,
          prism2mib_uint32 },
    { DIDmib_dot11mac_dot11OperationTable_dot11LongRetryLimit,
          F_AP | F_STA | F_READ,
          HFA384x_RID_LONGRETRYLIMIT, 0, 0,
          prism2mib_uint32 },
    { DIDmib_dot11mac_dot11OperationTable_dot11FragmentationThreshold,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_FRAGTHRESH, 0, 0,
          prism2mib_fragmentationthreshold },
    { DIDmib_dot11mac_dot11OperationTable_dot11FragmentationThreshold,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_FRAGTHRESH0, 0, 0,
          prism2mib_fragmentationthreshold },
    { DIDmib_dot11mac_dot11OperationTable_dot11MaxTransmitMSDULifetime,
          F_AP | F_STA | F_READ,
          HFA384x_RID_MAXTXLIFETIME, 0, 0,
          prism2mib_uint32 },
    { DIDmib_dot11mac_dot11OperationTable_dot11MaxReceiveLifetime,
          F_AP | F_STA | F_READ,
          HFA384x_RID_MAXRXLIFETIME, 0, 0,
          prism2mib_uint32 },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address1,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address2,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address3,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address4,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address5,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address6,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address7,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address8,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address9,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address10,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address11,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address12,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address13,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address14,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address15,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address16,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address17,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address18,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address19,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address20,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address21,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address22,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address23,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address24,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address25,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address26,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address27,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address28,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address29,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address30,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address31,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },
    { DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address32,
          F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_groupaddress },

    /* dot11phy MIB's */

    { DIDmib_dot11phy_dot11PhyOperationTable_dot11PHYType,
          F_AP | F_STA | F_READ,
          HFA384x_RID_PHYTYPE, 0, 0,
          prism2mib_uint32 },
    { DIDmib_dot11phy_dot11PhyOperationTable_dot11TempType,
          F_AP | F_STA | F_READ,
          HFA384x_RID_TEMPTYPE, 0, 0,
          prism2mib_uint32 },
    { DIDmib_dot11phy_dot11PhyDSSSTable_dot11CurrentChannel,
          F_STA | F_READ,
          HFA384x_RID_CURRENTCHANNEL, 0, 0,
          prism2mib_uint32 },
    { DIDmib_dot11phy_dot11PhyDSSSTable_dot11CurrentChannel,
          F_AP | F_READ,
          HFA384x_RID_CNFOWNCHANNEL, 0, 0,
          prism2mib_uint32 },
    { DIDmib_dot11phy_dot11PhyDSSSTable_dot11CurrentCCAMode,
          F_AP | F_STA | F_READ,
          HFA384x_RID_CCAMODE, 0, 0,
          prism2mib_uint32 },

    /* p2Table MIB's */

    { DIDmib_p2_p2Table_p2MMTx,
          F_AP | F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_priv },
    { DIDmib_p2_p2Table_p2EarlyBeacon,
          F_AP | F_READ | F_WRITE,
          BIT7, 0, 0,
          prism2mib_appcfinfoflag },
    { DIDmib_p2_p2Table_p2ReceivedFrameStatistics,
          F_AP | F_STA | F_READ,
          0, 0, 0,
          prism2mib_priv },
    { DIDmib_p2_p2Table_p2CommunicationTallies,
          F_AP | F_STA | F_READ,
          0, 0, 0,
          prism2mib_priv },
    { DIDmib_p2_p2Table_p2Authenticated,
          F_AP | F_READ,
          0, 0, 0,
          prism2mib_priv },
    { DIDmib_p2_p2Table_p2Associated,
          F_AP | F_READ,
          0, 0, 0,
          prism2mib_priv },
    { DIDmib_p2_p2Table_p2PowerSaveUserCount,
          F_AP | F_READ,
          0, 0, 0,
          prism2mib_priv },
    { DIDmib_p2_p2Table_p2Comment,
          F_AP | F_STA | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_priv },
    { DIDmib_p2_p2Table_p2AccessMode,
          F_AP | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_priv },
    { DIDmib_p2_p2Table_p2AccessAllow,
          F_AP | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_priv },
    { DIDmib_p2_p2Table_p2AccessDeny,
          F_AP | F_READ | F_WRITE,
          0, 0, 0,
          prism2mib_priv },
    { DIDmib_p2_p2Table_p2ChannelInfoResults,
          F_AP | F_READ,
          0, 0, 0,
          prism2mib_priv },

    /* p2Static MIB's */

    { DIDmib_p2_p2Static_p2CnfPortType,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFPORTTYPE, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfOwnMACAddress,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFOWNMACADDR, HFA384x_RID_CNFOWNMACADDR_LEN, 0,
          prism2mib_bytearea2pstr },
    { DIDmib_p2_p2Static_p2CnfDesiredSSID,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFDESIREDSSID, HFA384x_RID_CNFDESIREDSSID_LEN, 0,
          prism2mib_bytestr2pstr },
    { DIDmib_p2_p2Static_p2CnfOwnChannel,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFOWNCHANNEL, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfOwnSSID,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFOWNSSID, HFA384x_RID_CNFOWNSSID_LEN, 0,
          prism2mib_bytestr2pstr },
    { DIDmib_p2_p2Static_p2CnfOwnATIMWindow,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFOWNATIMWIN, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfSystemScale,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFSYSSCALE, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfMaxDataLength,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFMAXDATALEN, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfWDSAddress,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFWDSADDR, HFA384x_RID_CNFWDSADDR_LEN, 0,
          prism2mib_bytearea2pstr },
    { DIDmib_p2_p2Static_p2CnfPMEnabled,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFPMENABLED, 0, 0,
          prism2mib_truth },
    { DIDmib_p2_p2Static_p2CnfPMEPS,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFPMEPS, 0, 0,
          prism2mib_truth },
    { DIDmib_p2_p2Static_p2CnfMulticastReceive,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFMULTICASTRX, 0, 0,
          prism2mib_truth },
    { DIDmib_p2_p2Static_p2CnfMaxSleepDuration,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFMAXSLEEPDUR, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfPMHoldoverDuration,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFPMHOLDDUR, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfOwnName,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFOWNNAME, HFA384x_RID_CNFOWNNAME_LEN, 0,
          prism2mib_bytestr2pstr },
    { DIDmib_p2_p2Static_p2CnfOwnDTIMPeriod,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFOWNDTIMPER, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfWDSAddress1,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_CNFWDSADDR1, HFA384x_RID_CNFWDSADDR1_LEN, 0,
          prism2mib_bytearea2pstr },
    { DIDmib_p2_p2Static_p2CnfWDSAddress2,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_CNFWDSADDR2, HFA384x_RID_CNFWDSADDR2_LEN, 0,
          prism2mib_bytearea2pstr },
    { DIDmib_p2_p2Static_p2CnfWDSAddress3,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_CNFWDSADDR3, HFA384x_RID_CNFWDSADDR3_LEN, 0,
          prism2mib_bytearea2pstr },
    { DIDmib_p2_p2Static_p2CnfWDSAddress4,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_CNFWDSADDR4, HFA384x_RID_CNFWDSADDR4_LEN, 0,
          prism2mib_bytearea2pstr },
    { DIDmib_p2_p2Static_p2CnfWDSAddress5,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_CNFWDSADDR5, HFA384x_RID_CNFWDSADDR5_LEN, 0,
          prism2mib_bytearea2pstr },
    { DIDmib_p2_p2Static_p2CnfWDSAddress6,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_CNFWDSADDR6, HFA384x_RID_CNFWDSADDR6_LEN, 0,
          prism2mib_bytearea2pstr },
    { DIDmib_p2_p2Static_p2CnfMulticastPMBuffering,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_CNFMCASTPMBUFF, 0, 0,
          prism2mib_truth },
    { DIDmib_p2_p2Static_p2CnfWEPDefaultKeyID,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFWEPDEFAULTKEYID, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfWEPDefaultKey0,
          F_AP | F_STA | F_WRITE,
          HFA384x_RID_CNFWEPDEFAULTKEY0, 0, 0,
          prism2mib_wepdefaultkey },
    { DIDmib_p2_p2Static_p2CnfWEPDefaultKey1,
          F_AP | F_STA | F_WRITE,
          HFA384x_RID_CNFWEPDEFAULTKEY1, 0, 0,
          prism2mib_wepdefaultkey },
    { DIDmib_p2_p2Static_p2CnfWEPDefaultKey2,
          F_AP | F_STA | F_WRITE,
          HFA384x_RID_CNFWEPDEFAULTKEY2, 0, 0,
          prism2mib_wepdefaultkey },
    { DIDmib_p2_p2Static_p2CnfWEPDefaultKey3,
          F_AP | F_STA | F_WRITE,
          HFA384x_RID_CNFWEPDEFAULTKEY3, 0, 0,
          prism2mib_wepdefaultkey },
    { DIDmib_p2_p2Static_p2CnfWEPFlags,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFWEPFLAGS, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfAuthentication,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFAUTHENTICATION, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfMaxAssociatedStations,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_CNFMAXASSOCSTATIONS, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfTxControl,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFTXCONTROL, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfRoamingMode,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFROAMINGMODE, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfHostAuthentication,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_CNFHOSTAUTHASSOC, 0, 0,
          prism2mib_truth },
    { DIDmib_p2_p2Static_p2CnfRcvCrcError,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFRCVCRCERROR, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfAltRetryCount,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFALTRETRYCNT, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfBeaconInterval,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_CNFAPBCNINT, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfMediumOccupancyLimit,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_CNFAPPCFINFO, HFA384x_RID_CNFAPPCFINFO_LEN, 0,
          prism2mib_uint32offset },
    { DIDmib_p2_p2Static_p2CnfCFPPeriod,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_CNFAPPCFINFO, HFA384x_RID_CNFAPPCFINFO_LEN, 1,
          prism2mib_uint32offset },
    { DIDmib_p2_p2Static_p2CnfCFPMaxDuration,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_CNFAPPCFINFO, HFA384x_RID_CNFAPPCFINFO_LEN, 2,
          prism2mib_uint32offset },
    { DIDmib_p2_p2Static_p2CnfCFPFlags,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_CNFAPPCFINFO, HFA384x_RID_CNFAPPCFINFO_LEN, 3,
          prism2mib_uint32offset },
    { DIDmib_p2_p2Static_p2CnfSTAPCFInfo,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFSTAPCFINFO, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfPriorityQUsage,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFPRIORITYQUSAGE, HFA384x_RID_CNFPRIOQUSAGE_LEN, 0,
          prism2mib_uint32array },
    { DIDmib_p2_p2Static_p2CnfTIMCtrl,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFTIMCTRL, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfThirty2Tally,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFTHIRTY2TALLY, 0, 0,
          prism2mib_truth },
    { DIDmib_p2_p2Static_p2CnfEnhSecurity,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_CNFENHSECURITY, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfShortPreamble,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFSHORTPREAMBLE, 0, 0,
          prism2mib_preamble },
    { DIDmib_p2_p2Static_p2CnfExcludeLongPreamble,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_CNFEXCLONGPREAMBLE, 0, 0,
          prism2mib_truth },
    { DIDmib_p2_p2Static_p2CnfAuthenticationRspTO,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFAUTHRSPTIMEOUT, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfBasicRates,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFBASICRATES, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Static_p2CnfSupportedRates,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFSUPPRATES, 0, 0,
          prism2mib_uint32 },

    /* p2Dynamic MIB's */

    { DIDmib_p2_p2Dynamic_p2CreateIBSS,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_CREATEIBSS, 0, 0,
          prism2mib_truth },
    { DIDmib_p2_p2Dynamic_p2FragmentationThreshold,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_FRAGTHRESH, 0, 0,
          prism2mib_fragmentationthreshold },
    { DIDmib_p2_p2Dynamic_p2RTSThreshold,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_RTSTHRESH, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Dynamic_p2TxRateControl,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_TXRATECNTL, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Dynamic_p2PromiscuousMode,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_PROMISCMODE, 0, 0,
          prism2mib_truth },
    { DIDmib_p2_p2Dynamic_p2FragmentationThreshold0,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_FRAGTHRESH0, 0, 0,
          prism2mib_fragmentationthreshold },
    { DIDmib_p2_p2Dynamic_p2FragmentationThreshold1,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_FRAGTHRESH1, 0, 0,
          prism2mib_fragmentationthreshold },
    { DIDmib_p2_p2Dynamic_p2FragmentationThreshold2,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_FRAGTHRESH2, 0, 0,
          prism2mib_fragmentationthreshold },
    { DIDmib_p2_p2Dynamic_p2FragmentationThreshold3,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_FRAGTHRESH3, 0, 0,
          prism2mib_fragmentationthreshold },
    { DIDmib_p2_p2Dynamic_p2FragmentationThreshold4,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_FRAGTHRESH4, 0, 0,
          prism2mib_fragmentationthreshold },
    { DIDmib_p2_p2Dynamic_p2FragmentationThreshold5,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_FRAGTHRESH5, 0, 0,
          prism2mib_fragmentationthreshold },
    { DIDmib_p2_p2Dynamic_p2FragmentationThreshold6,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_FRAGTHRESH6, 0, 0,
          prism2mib_fragmentationthreshold },
    { DIDmib_p2_p2Dynamic_p2RTSThreshold0,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_RTSTHRESH0, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Dynamic_p2RTSThreshold1,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_RTSTHRESH1, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Dynamic_p2RTSThreshold2,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_RTSTHRESH2, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Dynamic_p2RTSThreshold3,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_RTSTHRESH3, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Dynamic_p2RTSThreshold4,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_RTSTHRESH4, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Dynamic_p2RTSThreshold5,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_RTSTHRESH5, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Dynamic_p2RTSThreshold6,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_RTSTHRESH6, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Dynamic_p2TxRateControl0,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_TXRATECNTL0, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Dynamic_p2TxRateControl1,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_TXRATECNTL1, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Dynamic_p2TxRateControl2,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_TXRATECNTL2, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Dynamic_p2TxRateControl3,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_TXRATECNTL3, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Dynamic_p2TxRateControl4,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_TXRATECNTL4, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Dynamic_p2TxRateControl5,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_TXRATECNTL5, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Dynamic_p2TxRateControl6,
          F_AP | F_READ | F_WRITE,
          HFA384x_RID_TXRATECNTL6, 0, 0,
          prism2mib_uint32 },

    /* p2Behavior MIB's */

    { DIDmib_p2_p2Behavior_p2TickTime,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_ITICKTIME, 0, 0,
          prism2mib_uint32 },

    /* p2NIC MIB's */

    { DIDmib_p2_p2NIC_p2MaxLoadTime,
          F_AP | F_STA | F_READ,
          HFA384x_RID_MAXLOADTIME, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2NIC_p2DLBufferPage,
          F_AP | F_STA | F_READ,
          HFA384x_RID_DOWNLOADBUFFER, HFA384x_RID_DOWNLOADBUFFER_LEN, 0,
          prism2mib_uint32offset },
    { DIDmib_p2_p2NIC_p2DLBufferOffset,
          F_AP | F_STA | F_READ,
          HFA384x_RID_DOWNLOADBUFFER, HFA384x_RID_DOWNLOADBUFFER_LEN, 1,
          prism2mib_uint32offset },
    { DIDmib_p2_p2NIC_p2DLBufferLength,
          F_AP | F_STA | F_READ,
          HFA384x_RID_DOWNLOADBUFFER, HFA384x_RID_DOWNLOADBUFFER_LEN, 2,
          prism2mib_uint32offset },
    { DIDmib_p2_p2NIC_p2PRIIdentity,
          F_AP | F_STA | F_READ,
          HFA384x_RID_PRIIDENTITY, HFA384x_RID_PRIIDENTITY_LEN, 0,
          prism2mib_uint32array },
    { DIDmib_p2_p2NIC_p2PRISupRange,
          F_AP | F_STA | F_READ,
          HFA384x_RID_PRISUPRANGE, HFA384x_RID_PRISUPRANGE_LEN, 0,
          prism2mib_uint32array },
    { DIDmib_p2_p2NIC_p2CFIActRanges,
          F_AP | F_STA | F_READ,
          HFA384x_RID_PRI_CFIACTRANGES, HFA384x_RID_CFIACTRANGES_LEN, 0,
          prism2mib_uint32array },
    { DIDmib_p2_p2NIC_p2BuildSequence,
          F_AP | F_STA | F_READ,
          HFA384x_RID_BUILDSEQ, HFA384x_RID_BUILDSEQ_LEN, 0,
          prism2mib_uint32array },
    { DIDmib_p2_p2NIC_p2PrimaryFWID,
          F_AP | F_STA | F_READ,
          0, 0, 0,
          prism2mib_fwid },
    { DIDmib_p2_p2NIC_p2SecondaryFWID,
          F_AP | F_STA | F_READ,
          0, 0, 0,
          prism2mib_fwid },
    { DIDmib_p2_p2NIC_p2TertiaryFWID,
          F_AP | F_READ,
          0, 0, 0,
          prism2mib_fwid },
    { DIDmib_p2_p2NIC_p2NICSerialNumber,
          F_AP | F_STA | F_READ,
          HFA384x_RID_NICSERIALNUMBER, HFA384x_RID_NICSERIALNUMBER_LEN, 0,
          prism2mib_bytearea2pstr },
    { DIDmib_p2_p2NIC_p2NICIdentity,
          F_AP | F_STA | F_READ,
          HFA384x_RID_NICIDENTITY, HFA384x_RID_NICIDENTITY_LEN, 0,
          prism2mib_uint32array },
    { DIDmib_p2_p2NIC_p2MFISupRange,
          F_AP | F_STA | F_READ,
          HFA384x_RID_MFISUPRANGE, HFA384x_RID_MFISUPRANGE_LEN, 0,
          prism2mib_uint32array },
    { DIDmib_p2_p2NIC_p2CFISupRange,
          F_AP | F_STA | F_READ,
          HFA384x_RID_CFISUPRANGE, HFA384x_RID_CFISUPRANGE_LEN, 0,
          prism2mib_uint32array },
    { DIDmib_p2_p2NIC_p2ChannelList,
          F_AP | F_STA | F_READ,
          HFA384x_RID_CHANNELLIST, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2NIC_p2RegulatoryDomains,
          F_AP | F_STA | F_READ,
          HFA384x_RID_REGULATORYDOMAINS, HFA384x_RID_REGULATORYDOMAINS_LEN, 0,
          prism2mib_regulatorydomains },
    { DIDmib_p2_p2NIC_p2TempType,
          F_AP | F_STA | F_READ,
          HFA384x_RID_TEMPTYPE, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2NIC_p2STAIdentity,
          F_AP | F_STA | F_READ,
          HFA384x_RID_STAIDENTITY, HFA384x_RID_STAIDENTITY_LEN, 0,
          prism2mib_uint32array },
    { DIDmib_p2_p2NIC_p2STASupRange,
          F_AP | F_STA | F_READ,
          HFA384x_RID_STASUPRANGE, HFA384x_RID_STASUPRANGE_LEN, 0,
          prism2mib_uint32array },
    { DIDmib_p2_p2NIC_p2MFIActRanges,
          F_AP | F_STA | F_READ,
          HFA384x_RID_STA_MFIACTRANGES, HFA384x_RID_MFIACTRANGES_LEN, 0,
          prism2mib_uint32array },
    { DIDmib_p2_p2NIC_p2STACFIActRanges,
          F_AP | F_STA | F_READ,
          HFA384x_RID_STA_CFIACTRANGES, HFA384x_RID_CFIACTRANGES2_LEN, 0,
          prism2mib_uint32array },

    /* p2MAC MIB's */

    { DIDmib_p2_p2MAC_p2PortStatus,
          F_STA | F_READ,
          HFA384x_RID_PORTSTATUS, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2MAC_p2CurrentSSID,
          F_STA | F_READ,
          HFA384x_RID_CURRENTSSID, HFA384x_RID_CURRENTSSID_LEN, 0,
          prism2mib_bytestr2pstr },
    { DIDmib_p2_p2MAC_p2CurrentBSSID,
          F_STA | F_READ,
          HFA384x_RID_CURRENTBSSID, HFA384x_RID_CURRENTBSSID_LEN, 0,
          prism2mib_bytearea2pstr },
    { DIDmib_p2_p2MAC_p2CommsQuality,
          F_STA | F_READ,
          HFA384x_RID_COMMSQUALITY, HFA384x_RID_COMMSQUALITY_LEN, 0,
          prism2mib_uint32array },
    { DIDmib_p2_p2MAC_p2CommsQualityCQ,
          F_STA | F_READ,
          HFA384x_RID_COMMSQUALITY, HFA384x_RID_COMMSQUALITY_LEN, 0,
          prism2mib_uint32offset },
    { DIDmib_p2_p2MAC_p2CommsQualityASL,
          F_STA | F_READ,
          HFA384x_RID_COMMSQUALITY, HFA384x_RID_COMMSQUALITY_LEN, 1,
          prism2mib_uint32offset },
    { DIDmib_p2_p2MAC_p2CommsQualityANL,
          F_STA | F_READ,
          HFA384x_RID_COMMSQUALITY, HFA384x_RID_COMMSQUALITY_LEN, 2,
          prism2mib_uint32offset },
    { DIDmib_p2_p2MAC_p2dbmCommsQuality,
          F_STA | F_READ,
          HFA384x_RID_DBMCOMMSQUALITY, HFA384x_RID_DBMCOMMSQUALITY_LEN, 0,
          prism2mib_uint32array },
    { DIDmib_p2_p2MAC_p2dbmCommsQualityCQ,
          F_STA | F_READ,
          HFA384x_RID_COMMSQUALITY, HFA384x_RID_COMMSQUALITY_LEN, 0,
          prism2mib_uint32offset },
    { DIDmib_p2_p2MAC_p2dbmCommsQualityASL,
          F_STA | F_READ,
          HFA384x_RID_COMMSQUALITY, HFA384x_RID_COMMSQUALITY_LEN, 1,
          prism2mib_uint32offset },
    { DIDmib_p2_p2MAC_p2dbmCommsQualityANL,
          F_STA | F_READ,
          HFA384x_RID_COMMSQUALITY, HFA384x_RID_COMMSQUALITY_LEN, 2,
          prism2mib_uint32offset },
    { DIDmib_p2_p2MAC_p2CurrentTxRate,
          F_STA | F_READ,
          HFA384x_RID_CURRENTTXRATE, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2MAC_p2CurrentBeaconInterval,
          F_AP | F_STA | F_READ,
          HFA384x_RID_CURRENTBCNINT, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2MAC_p2StaCurrentScaleThresholds,
          F_STA | F_READ,
          HFA384x_RID_CURRENTSCALETHRESH, HFA384x_RID_STACURSCALETHRESH_LEN, 0,
          prism2mib_uint32array },
    { DIDmib_p2_p2MAC_p2APCurrentScaleThresholds,
          F_AP | F_READ,
          HFA384x_RID_CURRENTSCALETHRESH, HFA384x_RID_APCURSCALETHRESH_LEN, 0,
          prism2mib_uint32array },
    { DIDmib_p2_p2MAC_p2ProtocolRspTime,
          F_AP | F_STA | F_READ,
          HFA384x_RID_PROTOCOLRSPTIME, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2MAC_p2ShortRetryLimit,
          F_AP | F_STA | F_READ,
          HFA384x_RID_SHORTRETRYLIMIT, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2MAC_p2LongRetryLimit,
          F_AP | F_STA | F_READ,
          HFA384x_RID_LONGRETRYLIMIT, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2MAC_p2MaxTransmitLifetime,
          F_AP | F_STA | F_READ,
          HFA384x_RID_MAXTXLIFETIME, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2MAC_p2MaxReceiveLifetime,
          F_AP | F_STA | F_READ,
          HFA384x_RID_MAXRXLIFETIME, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2MAC_p2CFPollable,
          F_STA | F_READ,
          HFA384x_RID_CFPOLLABLE, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2MAC_p2AuthenticationAlgorithms,
          F_AP | F_STA | F_READ,
          HFA384x_RID_AUTHALGORITHMS, HFA384x_RID_AUTHALGORITHMS_LEN, 0,
          prism2mib_uint32array },
    { DIDmib_p2_p2MAC_p2PrivacyOptionImplemented,
          F_AP | F_STA | F_READ,
          HFA384x_RID_PRIVACYOPTIMP, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2MAC_p2CurrentTxRate1,
          F_AP | F_READ,
          HFA384x_RID_CURRENTTXRATE1, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2MAC_p2CurrentTxRate2,
          F_AP | F_READ,
          HFA384x_RID_CURRENTTXRATE2, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2MAC_p2CurrentTxRate3,
          F_AP | F_READ,
          HFA384x_RID_CURRENTTXRATE3, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2MAC_p2CurrentTxRate4,
          F_AP | F_READ,
          HFA384x_RID_CURRENTTXRATE4, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2MAC_p2CurrentTxRate5,
          F_AP | F_READ,
          HFA384x_RID_CURRENTTXRATE5, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2MAC_p2CurrentTxRate6,
          F_AP | F_READ,
          HFA384x_RID_CURRENTTXRATE6, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2MAC_p2OwnMACAddress,
          F_AP | F_READ,
          HFA384x_RID_OWNMACADDRESS, HFA384x_RID_OWNMACADDRESS_LEN, 0,
          prism2mib_bytearea2pstr },

    /* p2Modem MIB's */

    { DIDmib_p2_p2Modem_p2PHYType,
          F_AP | F_STA | F_READ,
          HFA384x_RID_PHYTYPE, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Modem_p2CurrentChannel,
          F_AP | F_STA | F_READ,
          HFA384x_RID_CURRENTCHANNEL, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Modem_p2CurrentPowerState,
          F_AP | F_STA | F_READ,
          HFA384x_RID_CURRENTPOWERSTATE, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Modem_p2CCAMode,
          F_AP | F_STA | F_READ,
          HFA384x_RID_CCAMODE, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Modem_p2TxPowerMax,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_TXPOWERMAX, 0, 0,
          prism2mib_uint32 },
    { DIDmib_dot11phy_dot11PhyTxPowerTable_dot11CurrentTxPowerLevel,
          F_AP | F_STA | F_READ | F_WRITE,
          HFA384x_RID_TXPOWERMAX, 0, 0,
          prism2mib_uint32 },
    { DIDmib_p2_p2Modem_p2SupportedDataRates,
          F_AP | F_STA | F_READ,
          HFA384x_RID_SUPPORTEDDATARATES, HFA384x_RID_SUPPORTEDDATARATES_LEN, 0,
          prism2mib_bytestr2pstr },

    /* And finally, lnx mibs */
    { DIDmib_lnx_lnxConfigTable_lnxRSNAIE,
          F_STA | F_READ | F_WRITE,
          HFA384x_RID_CNFWPADATA, 0, 0,
          prism2mib_priv },
    { 0, 0, 0, 0, 0, NULL}};

/*----------------------------------------------------------------
These MIB's are not supported at this time:

DIDmib_dot11phy_dot11PhyOperationTable_dot11ChannelAgilityPresent
DIDmib_dot11phy_dot11PhyOperationTable_dot11ChannelAgilityEnabled
DIDmib_dot11phy_dot11PhyDSSSTable_dot11PBCCOptionImplemented
DIDmib_dot11phy_dot11RegDomainsSupportedTable_dot11RegDomainsSupportIndex
DIDmib_dot11phy_dot11SupportedDataRatesTxTable_dot11SupportedDataRatesTxIndex
DIDmib_dot11phy_dot11SupportedDataRatesTxTable_dot11SupportedDataRatesTxValue
DIDmib_dot11phy_dot11SupportedDataRatesRxTable_dot11SupportedDataRatesRxIndex
DIDmib_dot11phy_dot11SupportedDataRatesRxTable_dot11SupportedDataRatesRxValue

DIDmib_dot11phy_dot11RegDomainsSupportedTable_dot11RegDomainsSupportValue
TODO: need to investigate why wlan has this as enumerated and Prism2 has this
      as btye str.

DIDmib_dot11phy_dot11PhyDSSSTable_dot11ShortPreambleOptionImplemented
TODO: Find out the firmware version number(s) for identifying
      whether the firmware is capable of short preamble. TRUE or FALSE
      will be returned based on the version of the firmware.

WEP Key mappings aren't supported in the f/w.
DIDmib_dot11smt_dot11WEPKeyMappingsTable_dot11WEPKeyMappingIndex
DIDmib_dot11smt_dot11WEPKeyMappingsTable_dot11WEPKeyMappingAddress
DIDmib_dot11smt_dot11WEPKeyMappingsTable_dot11WEPKeyMappingWEPOn
DIDmib_dot11smt_dot11WEPKeyMappingsTable_dot11WEPKeyMappingValue
DIDmib_dot11smt_dot11PrivacyTable_dot11WEPKeyMappingLength

TODO: implement counters.
DIDmib_dot11smt_dot11PrivacyTable_dot11WEPICVErrorCount
DIDmib_dot11smt_dot11PrivacyTable_dot11WEPExcludedCount
DIDmib_dot11mac_dot11CountersTable_dot11TransmittedFragmentCount
DIDmib_dot11mac_dot11CountersTable_dot11MulticastTransmittedFrameCount
DIDmib_dot11mac_dot11CountersTable_dot11FailedCount
DIDmib_dot11mac_dot11CountersTable_dot11RetryCount
DIDmib_dot11mac_dot11CountersTable_dot11MultipleRetryCount
DIDmib_dot11mac_dot11CountersTable_dot11FrameDuplicateCount
DIDmib_dot11mac_dot11CountersTable_dot11RTSSuccessCount
DIDmib_dot11mac_dot11CountersTable_dot11RTSFailureCount
DIDmib_dot11mac_dot11CountersTable_dot11ACKFailureCount
DIDmib_dot11mac_dot11CountersTable_dot11ReceivedFragmentCount
DIDmib_dot11mac_dot11CountersTable_dot11MulticastReceivedFrameCount
DIDmib_dot11mac_dot11CountersTable_dot11FCSErrorCount
DIDmib_dot11mac_dot11CountersTable_dot11TransmittedFrameCount
DIDmib_dot11mac_dot11CountersTable_dot11WEPUndecryptableCount

TODO: implement sane values for these.
DIDmib_dot11mac_dot11OperationTable_dot11ManufacturerID
DIDmib_dot11mac_dot11OperationTable_dot11ProductID

Not too worried about these at the moment.
DIDmib_dot11phy_dot11PhyAntennaTable_dot11CurrentTxAntenna
DIDmib_dot11phy_dot11PhyAntennaTable_dot11DiversitySupport
DIDmib_dot11phy_dot11PhyAntennaTable_dot11CurrentRxAntenna
DIDmib_dot11phy_dot11PhyTxPowerTable_dot11NumberSupportedPowerLevels
DIDmib_dot11phy_dot11PhyTxPowerTable_dot11TxPowerLevel1
DIDmib_dot11phy_dot11PhyTxPowerTable_dot11TxPowerLevel2
DIDmib_dot11phy_dot11PhyTxPowerTable_dot11TxPowerLevel3
DIDmib_dot11phy_dot11PhyTxPowerTable_dot11TxPowerLevel4
DIDmib_dot11phy_dot11PhyTxPowerTable_dot11TxPowerLevel5
DIDmib_dot11phy_dot11PhyTxPowerTable_dot11TxPowerLevel6
DIDmib_dot11phy_dot11PhyTxPowerTable_dot11TxPowerLevel7
DIDmib_dot11phy_dot11PhyTxPowerTable_dot11TxPowerLevel8
DIDmib_dot11phy_dot11PhyTxPowerTable_dot11CurrentTxPowerLevel

Ummm, FH and IR don't apply
DIDmib_dot11phy_dot11PhyFHSSTable_dot11HopTime
DIDmib_dot11phy_dot11PhyFHSSTable_dot11CurrentChannelNumber
DIDmib_dot11phy_dot11PhyFHSSTable_dot11MaxDwellTime
DIDmib_dot11phy_dot11PhyFHSSTable_dot11CurrentDwellTime
DIDmib_dot11phy_dot11PhyFHSSTable_dot11CurrentSet
DIDmib_dot11phy_dot11PhyFHSSTable_dot11CurrentPattern
DIDmib_dot11phy_dot11PhyFHSSTable_dot11CurrentIndex
DIDmib_dot11phy_dot11PhyDSSSTable_dot11CCAModeSupported
DIDmib_dot11phy_dot11PhyDSSSTable_dot11EDThreshold
DIDmib_dot11phy_dot11PhyIRTable_dot11CCAWatchdogTimerMax
DIDmib_dot11phy_dot11PhyIRTable_dot11CCAWatchdogCountMax
DIDmib_dot11phy_dot11PhyIRTable_dot11CCAWatchdogTimerMin
DIDmib_dot11phy_dot11PhyIRTable_dot11CCAWatchdogCountMin

We just don't have enough antennas right now to worry about this.
DIDmib_dot11phy_dot11AntennasListTable_dot11AntennaListIndex
DIDmib_dot11phy_dot11AntennasListTable_dot11SupportedTxAntenna
DIDmib_dot11phy_dot11AntennasListTable_dot11SupportedRxAntenna
DIDmib_dot11phy_dot11AntennasListTable_dot11DiversitySelectionRx

------------------------------------------------------------------*/

/*================================================================*/
/* Function Definitions */

/*----------------------------------------------------------------
* prism2mgmt_mibset_mibget
*
* Set the value of a mib item.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
*	0	success and done
*	<0	success, but we're waiting for something to finish.
*	>0	an error occurred while handling the message.
* Side effects:
*
* Call context:
*	process thread  (usually)
*	interrupt
----------------------------------------------------------------*/

int prism2mgmt_mibset_mibget(wlandevice_t *wlandev, void *msgp)
{
	hfa384x_t		*hw = wlandev->priv;
	int			result, isget;
	mibrec_t		*mib;
	UINT16			which;

	p80211msg_dot11req_mibset_t	*msg = msgp;
	p80211itemd_t			*mibitem;

	DBFENTER;

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	msg->resultcode.data = P80211ENUM_resultcode_success;

	/*
	** Determine if this is an Access Point or a station.
	*/

	which = hw->ap ? F_AP : F_STA;

	/*
	** Find the MIB in the MIB table.  Note that a MIB may be in the
	** table twice...once for an AP and once for a station.  Make sure
	** to get the correct one.  Note that DID=0 marks the end of the
	** MIB table.
	*/

	mibitem = (p80211itemd_t *) msg->mibattribute.data;

	for (mib = mibtab; mib->did != 0; mib++)
		if (mib->did == mibitem->did && (mib->flag & which))
			break;

	if (mib->did == 0) {
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
		goto done;
	}

	/*
	** Determine if this is a "mibget" or a "mibset".  If this is a
	** "mibget", then make sure that the MIB may be read.  Otherwise,
	** this is a "mibset" so make make sure that the MIB may be written.
	*/

	isget = (msg->msgcode == DIDmsg_dot11req_mibget);

	if (isget) {
		if (!(mib->flag & F_READ)) {
			msg->resultcode.data =
				P80211ENUM_resultcode_cant_get_writeonly_mib;
			goto done;
		}
	} else {
		if (!(mib->flag & F_WRITE)) {
			msg->resultcode.data =
				P80211ENUM_resultcode_cant_set_readonly_mib;
			goto done;
		}
	}

	/*
	** Execute the MIB function.  If things worked okay, then make
	** sure that the MIB function also worked okay.  If so, and this
	** is a "mibget", then the status value must be set for both the
	** "mibattribute" parameter and the mib item within the data
	** portion of the "mibattribute".
	*/

	result = mib->func(mib, isget, wlandev, hw, msg,
			   (void *) mibitem->data);

	if (msg->resultcode.data == P80211ENUM_resultcode_success) {
		if (result != 0) {
			WLAN_LOG_DEBUG(1, "get/set failure, result=%d\n",
					result);
			msg->resultcode.data =
				 P80211ENUM_resultcode_implementation_failure;
		} else {
			if (isget) {
				msg->mibattribute.status =
					P80211ENUM_msgitem_status_data_ok;
				mibitem->status =
					P80211ENUM_msgitem_status_data_ok;
			}
		}
	}

done:
	DBFEXIT;

	return(0);
}

/*----------------------------------------------------------------
* prism2mib_bytestr2pstr
*
* Get/set pstr data to/from a byte string.
*
* MIB record parameters:
*       parm1    Prism2 RID value.
*       parm2    Number of bytes of RID data.
*       parm3    Not used.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_bytestr2pstr(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	int                result;
	p80211pstrd_t      *pstr = (p80211pstrd_t*) data;
	UINT8              bytebuf[MIB_TMP_MAXLEN];
	hfa384x_bytestr_t  *p2bytestr = (hfa384x_bytestr_t*) bytebuf;

	DBFENTER;

	if (isget) {
		result = hfa384x_drvr_getconfig(hw, mib->parm1, bytebuf, mib->parm2);
		prism2mgmt_bytestr2pstr(p2bytestr, pstr);
	} else {
		memset(bytebuf, 0, mib->parm2);
		prism2mgmt_pstr2bytestr(p2bytestr, pstr);
		result = hfa384x_drvr_setconfig(hw, mib->parm1, bytebuf, mib->parm2);
	}

	DBFEXIT;
	return(result);
}

/*----------------------------------------------------------------
* prism2mib_bytearea2pstr
*
* Get/set pstr data to/from a byte area.
*
* MIB record parameters:
*       parm1    Prism2 RID value.
*       parm2    Number of bytes of RID data.
*       parm3    Not used.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_bytearea2pstr(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	int            result;
	p80211pstrd_t  *pstr = (p80211pstrd_t*) data;
	UINT8          bytebuf[MIB_TMP_MAXLEN];

	DBFENTER;

	if (isget) {
		result = hfa384x_drvr_getconfig(hw, mib->parm1, bytebuf, mib->parm2);
		prism2mgmt_bytearea2pstr(bytebuf, pstr,	mib->parm2);
	} else {
		memset(bytebuf, 0, mib->parm2);
		prism2mgmt_pstr2bytearea(bytebuf, pstr);
		result = hfa384x_drvr_setconfig(hw, mib->parm1, bytebuf, mib->parm2);
	}

	DBFEXIT;
	return(result);
}

/*----------------------------------------------------------------
* prism2mib_uint32
*
* Get/set uint32 data.
*
* MIB record parameters:
*       parm1    Prism2 RID value.
*       parm2    Not used.
*       parm3    Not used.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_uint32(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	int     result;
	UINT32  *uint32 = (UINT32*) data;
	UINT8   bytebuf[MIB_TMP_MAXLEN];
	UINT16  *wordbuf = (UINT16*) bytebuf;

	DBFENTER;

	if (isget) {
		result = hfa384x_drvr_getconfig16(hw, mib->parm1, wordbuf);
		*uint32 = *wordbuf;
		/* [MSM] Removed, getconfig16 returns the value in host order.
		 * prism2mgmt_prism2int2p80211int(wordbuf, uint32);
		 */
	} else {
		/* [MSM] Removed, setconfig16 expects host order.
		 * prism2mgmt_p80211int2prism2int(wordbuf, uint32);
		 */
		*wordbuf = *uint32;
		result = hfa384x_drvr_setconfig16(hw, mib->parm1, *wordbuf);
	}

	DBFEXIT;
	return(result);
}

/*----------------------------------------------------------------
* prism2mib_uint32array
*
* Get/set an array of uint32 data.
*
* MIB record parameters:
*       parm1    Prism2 RID value.
*       parm2    Number of bytes of RID data.
*       parm3    Not used.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_uint32array(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	int     result;
	UINT32  *uint32 = (UINT32 *) data;
	UINT8   bytebuf[MIB_TMP_MAXLEN];
	UINT16  *wordbuf = (UINT16*) bytebuf;
	int     i, cnt;

	DBFENTER;

	cnt = mib->parm2 / sizeof(UINT16);

	if (isget) {
		result = hfa384x_drvr_getconfig(hw, mib->parm1, wordbuf, mib->parm2);
		for (i = 0; i < cnt; i++)
			prism2mgmt_prism2int2p80211int(wordbuf+i, uint32+i);
	} else {
		for (i = 0; i < cnt; i++)
			prism2mgmt_p80211int2prism2int(wordbuf+i, uint32+i);
		result = hfa384x_drvr_setconfig(hw, mib->parm1, wordbuf, mib->parm2);
		}

	DBFEXIT;
	return(result);
}

/*----------------------------------------------------------------
* prism2mib_uint32offset
*
* Get/set a single element in an array of uint32 data.
*
* MIB record parameters:
*       parm1    Prism2 RID value.
*       parm2    Number of bytes of RID data.
*       parm3    Element index.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_uint32offset(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	int     result;
	UINT32  *uint32 = (UINT32*) data;
	UINT8   bytebuf[MIB_TMP_MAXLEN];
	UINT16  *wordbuf = (UINT16*) bytebuf;
	UINT16  cnt;

	DBFENTER;

	cnt = mib->parm2 / sizeof(UINT16);

	result = hfa384x_drvr_getconfig(hw, mib->parm1, wordbuf, mib->parm2);
	if (result == 0) {
		if (isget) {
			if (mib->parm3 < cnt)
				prism2mgmt_prism2int2p80211int(wordbuf+mib->parm3, uint32);
			else
				*uint32 = 0;
		} else {
			if (mib->parm3 < cnt) {
				prism2mgmt_p80211int2prism2int(wordbuf+mib->parm3, uint32);
				result = hfa384x_drvr_setconfig(hw, mib->parm1, wordbuf, mib->parm2);
			}
		}
	}

	DBFEXIT;
	return(result);
}

/*----------------------------------------------------------------
* prism2mib_truth
*
* Get/set truth data.
*
* MIB record parameters:
*       parm1    Prism2 RID value.
*       parm2    Not used.
*       parm3    Not used.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_truth(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	int     result;
	UINT32  *uint32 = (UINT32*) data;
	UINT8   bytebuf[MIB_TMP_MAXLEN];
	UINT16  *wordbuf = (UINT16*) bytebuf;

	DBFENTER;

	if (isget) {
		result = hfa384x_drvr_getconfig16(hw, mib->parm1, wordbuf);
		*uint32 = (*wordbuf) ?
				P80211ENUM_truth_true : P80211ENUM_truth_false;
	} else {
		*wordbuf = ((*uint32) == P80211ENUM_truth_true) ? 1 : 0;
		result = hfa384x_drvr_setconfig16(hw, mib->parm1, *wordbuf);
	}

	DBFEXIT;
	return(result);
}

/*----------------------------------------------------------------
* prism2mib_flag
*
* Get/set a flag.
*
* MIB record parameters:
*       parm1    Prism2 RID value.
*       parm2    Bit to get/set.
*       parm3    Not used.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_flag(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	int     result;
	UINT32  *uint32 = (UINT32*) data;
	UINT8   bytebuf[MIB_TMP_MAXLEN];
	UINT16  *wordbuf = (UINT16*) bytebuf;
	UINT32  flags;

	DBFENTER;

	result = hfa384x_drvr_getconfig16(hw, mib->parm1, wordbuf);
	if (result == 0) {
		/* [MSM] Removed, getconfig16 returns the value in host order.
		 * prism2mgmt_prism2int2p80211int(wordbuf, &flags);
		 */
		flags = *wordbuf;
		if (isget) {
			*uint32 = (flags & mib->parm2) ?
				P80211ENUM_truth_true : P80211ENUM_truth_false;
		} else {
			if ((*uint32) == P80211ENUM_truth_true)
				flags |= mib->parm2;
			else
				flags &= ~mib->parm2;
			/* [MSM] Removed, setconfig16 expects host order.
			 * prism2mgmt_p80211int2prism2int(wordbuf, &flags);
			 */
			*wordbuf = flags;
			result = hfa384x_drvr_setconfig16(hw, mib->parm1, *wordbuf);
		}
	}

	DBFEXIT;
	return(result);
}

/*----------------------------------------------------------------
* prism2mib_appcfinfoflag
*
* Get/set a single flag in the APPCFINFO record.
*
* MIB record parameters:
*       parm1    Bit to get/set.
*       parm2    Not used.
*       parm3    Not used.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_appcfinfoflag(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	int     result;
	UINT32  *uint32 = (UINT32*) data;
	UINT8   bytebuf[MIB_TMP_MAXLEN];
	UINT16  *wordbuf = (UINT16*) bytebuf;
	UINT16  word;

	DBFENTER;

	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_CNFAPPCFINFO,
					bytebuf, HFA384x_RID_CNFAPPCFINFO_LEN);
	if (result == 0) {
		if (isget) {
			*uint32 = (hfa384x2host_16(wordbuf[3]) & mib->parm1) ?
				P80211ENUM_truth_true : P80211ENUM_truth_false;
		} else {
			word = hfa384x2host_16(wordbuf[3]);
			word = ((*uint32) == P80211ENUM_truth_true) ?
				(word | mib->parm1) : (word & ~mib->parm1);
			wordbuf[3] = host2hfa384x_16(word);
			result = hfa384x_drvr_setconfig(hw, HFA384x_RID_CNFAPPCFINFO,
					bytebuf, HFA384x_RID_CNFAPPCFINFO_LEN);
		}
	}

	DBFEXIT;
	return(result);
}

/*----------------------------------------------------------------
* prism2mib_regulatorydomains
*
* Get regulatory domain data.
*
* MIB record parameters:
*       parm1    Prism2 RID value.
*       parm2    Number of bytes of RID data.
*       parm3    Not used.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_regulatorydomains(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	int            result;
	UINT32         cnt;
	p80211pstrd_t  *pstr = (p80211pstrd_t*) data;
	UINT8          bytebuf[MIB_TMP_MAXLEN];
	UINT16         *wordbuf = (UINT16*) bytebuf;

	DBFENTER;

	result = 0;

	if (isget) {
		result = hfa384x_drvr_getconfig(hw, mib->parm1, wordbuf, mib->parm2);
		prism2mgmt_prism2int2p80211int(wordbuf, &cnt);
		pstr->len = (UINT8) cnt;
		memcpy(pstr->data, &wordbuf[1], pstr->len);
	}

	DBFEXIT;
	return(result);
}

/*----------------------------------------------------------------
* prism2mib_wepdefaultkey
*
* Get/set WEP default keys.
*
* MIB record parameters:
*       parm1    Prism2 RID value.
*       parm2    Number of bytes of RID data.
*       parm3    Not used.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_wepdefaultkey(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	int            result;
	p80211pstrd_t  *pstr = (p80211pstrd_t*) data;
	UINT8          bytebuf[MIB_TMP_MAXLEN];
	UINT16         len;

	DBFENTER;

	if (isget) {
		result = 0;    /* Should never happen. */
	} else {
		len = (pstr->len > 5) ? HFA384x_RID_CNFWEP128DEFAULTKEY_LEN :
					HFA384x_RID_CNFWEPDEFAULTKEY_LEN;
		memset(bytebuf, 0, len);
		prism2mgmt_pstr2bytearea(bytebuf, pstr);
		result = hfa384x_drvr_setconfig(hw, mib->parm1, bytebuf, len);
	}

	DBFEXIT;
	return(result);
}

/*----------------------------------------------------------------
* prism2mib_powermanagement
*
* Get/set 802.11 power management value.  Note that this is defined differently
* by 802.11 and Prism2:
*
*       Meaning     802.11       Prism2
*        active       1           false
*      powersave      2           true
*
* MIB record parameters:
*       parm1    Prism2 RID value.
*       parm2    Not used.
*       parm3    Not used.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_powermanagement(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	int     result;
	UINT32  *uint32 = (UINT32*) data;
	UINT32  value;

	DBFENTER;

	if (isget) {
		result = prism2mib_uint32(mib, isget, wlandev, hw, msg, &value);
		*uint32 = (value == 0) ? 1 : 2;
	} else {
		value = ((*uint32) == 1) ? 0 : 1;
		result = prism2mib_uint32(mib, isget, wlandev, hw, msg, &value);
	}

	DBFEXIT;
	return(result);
}

/*----------------------------------------------------------------
* prism2mib_preamble
*
* Get/set Prism2 short preamble
*
* MIB record parameters:
*       parm1    Prism2 RID value.
*       parm2    Not used.
*       parm3    Not used.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_preamble(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	int     result;
	UINT32  *uint32 = (UINT32*) data;
	UINT8   bytebuf[MIB_TMP_MAXLEN];
	UINT16  *wordbuf = (UINT16*) bytebuf;

	DBFENTER;

	if (isget) {
		result = hfa384x_drvr_getconfig16(hw, mib->parm1, wordbuf);
		*uint32 = *wordbuf;
	} else {
		*wordbuf = *uint32;
		result = hfa384x_drvr_setconfig16(hw, mib->parm1, *wordbuf);
	}

	DBFEXIT;
	return(result);
}

/*----------------------------------------------------------------
* prism2mib_privacyinvoked
*
* Get/set the dot11PrivacyInvoked value.
*
* MIB record parameters:
*       parm1    Prism2 RID value.
*       parm2    Bit value for PrivacyInvoked flag.
*       parm3    Not used.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_privacyinvoked(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	int     result;

	DBFENTER;

	if (wlandev->hostwep & HOSTWEP_DECRYPT) {
		if (wlandev->hostwep & HOSTWEP_DECRYPT)
			mib->parm2 |= HFA384x_WEPFLAGS_DISABLE_RXCRYPT;
		if (wlandev->hostwep & HOSTWEP_ENCRYPT)
			mib->parm2 |= HFA384x_WEPFLAGS_DISABLE_TXCRYPT;
	}

	result = prism2mib_flag(mib, isget, wlandev, hw, msg, data);

	DBFEXIT;
	return(result);
}

/*----------------------------------------------------------------
* prism2mib_excludeunencrypted
*
* Get/set the dot11ExcludeUnencrypted value.
*
* MIB record parameters:
*       parm1    Prism2 RID value.
*       parm2    Bit value for ExcludeUnencrypted flag.
*       parm3    Not used.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_excludeunencrypted(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	int     result;

	DBFENTER;

	result = prism2mib_flag(mib, isget, wlandev, hw, msg, data);

	DBFEXIT;
	return(result);
}

/*----------------------------------------------------------------
* prism2mib_fragmentationthreshold
*
* Get/set the fragmentation threshold.
*
* MIB record parameters:
*       parm1    Prism2 RID value.
*       parm2    Not used.
*       parm3    Not used.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_fragmentationthreshold(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	int     result;
	UINT32  *uint32 = (UINT32*) data;

	DBFENTER;

	if (!isget)
		if ((*uint32) % 2) {
			WLAN_LOG_WARNING("Attempt to set odd number "
					  "FragmentationThreshold\n");
			msg->resultcode.data = P80211ENUM_resultcode_not_supported;
			return(0);
		}

	result = prism2mib_uint32(mib, isget, wlandev, hw, msg, data);

	DBFEXIT;
	return(result);
}

/*----------------------------------------------------------------
* prism2mib_operationalrateset
*
* Get/set the operational rate set.
*
* MIB record parameters:
*       parm1    Prism2 RID value.
*       parm2    Not used.
*       parm3    Not used.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_operationalrateset(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	int            result;
	p80211pstrd_t  *pstr = (p80211pstrd_t *) data;
	UINT8          bytebuf[MIB_TMP_MAXLEN];
	UINT16         *wordbuf = (UINT16*) bytebuf;

	DBFENTER;

	if (isget) {
		result = hfa384x_drvr_getconfig16(hw, mib->parm1, wordbuf);
		prism2mgmt_get_oprateset(wordbuf, pstr);
	} else {
		prism2mgmt_set_oprateset(wordbuf, pstr);
		result = hfa384x_drvr_setconfig16(hw, mib->parm1, *wordbuf);
		result = hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFSUPPRATES, *wordbuf);
	}

	DBFEXIT;
	return(result);
}

/*----------------------------------------------------------------
* prism2mib_groupaddress
*
* Get/set the dot11GroupAddressesTable.
*
* MIB record parameters:
*       parm1    Not used.
*       parm2    Not used.
*       parm3    Not used.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_groupaddress(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	int            result;
	p80211pstrd_t  *pstr = (p80211pstrd_t *) data;
	UINT8          bytebuf[MIB_TMP_MAXLEN];
	UINT16         len;

	DBFENTER;

	/* TODO: fix this.  f/w doesn't support mcast filters */

	if (isget) {
		prism2mgmt_get_grpaddr(mib->did, pstr, hw);
		return(0);
	}

	result = prism2mgmt_set_grpaddr(mib->did, bytebuf, pstr, hw);
	if (result != 0) {
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
		return(result);
	}

	if (hw->dot11_grpcnt <= MAX_PRISM2_GRP_ADDR) {
		len = hw->dot11_grpcnt * WLAN_ADDR_LEN;
		memcpy(bytebuf, hw->dot11_grp_addr[0], len);
		result = hfa384x_drvr_setconfig(hw, HFA384x_RID_GROUPADDR, bytebuf, len);

		/*
		** Turn off promiscuous mode if count is equal to MAX.  We may
		** have been at a higher count in promiscuous mode and need to
		** turn it off.
		*/

		/* but only if we're not already in promisc mode. :) */
		if ((hw->dot11_grpcnt == MAX_PRISM2_GRP_ADDR) &&
		    !( wlandev->netdev->flags & IFF_PROMISC)) {
			result = hfa384x_drvr_setconfig16(hw,
					     HFA384x_RID_PROMISCMODE, 0);
		}
	} else {

		/*
		** Clear group addresses in card and set to promiscuous mode.
		*/

		memset(bytebuf, 0, sizeof(bytebuf));
		result = hfa384x_drvr_setconfig(hw, HFA384x_RID_GROUPADDR,
						bytebuf, 0);
		if (result == 0) {
			result = hfa384x_drvr_setconfig16(hw,
					HFA384x_RID_PROMISCMODE, 1);
		}
	}

	DBFEXIT;
	return(result);
}

/*----------------------------------------------------------------
* prism2mib_fwid
*
* Get the firmware ID.
*
* MIB record parameters:
*       parm1    Not used.
*       parm2    Not used.
*       parm3    Not used.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_fwid(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	int             result;
	p80211pstrd_t   *pstr = (p80211pstrd_t *) data;
	hfa384x_FWID_t  fwid;

	DBFENTER;

	if (isget) {
		result = hfa384x_drvr_getconfig(hw, HFA384x_RID_FWID,
						&fwid, HFA384x_RID_FWID_LEN);
		if (mib->did == DIDmib_p2_p2NIC_p2PrimaryFWID) {
			fwid.primary[HFA384x_FWID_LEN - 1] = '\0';
			pstr->len = strlen(fwid.primary);
			memcpy(pstr->data, fwid.primary, pstr->len);
		} else {
			fwid.secondary[HFA384x_FWID_LEN - 1] = '\0';
			pstr->len = strlen(fwid.secondary);
			memcpy(pstr->data, fwid.secondary, pstr->len);
		}
	} else
		result = 0;     /* Should never happen. */

	DBFEXIT;
	return(result);
}

/*----------------------------------------------------------------
* prism2mib_authalg
*
* Get values from the AuhtenticationAlgorithmsTable.
*
* MIB record parameters:
*       parm1    Table index (1-6).
*       parm2    Not used.
*       parm3    Not used.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_authalg(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	UINT32  *uint32 = (UINT32*) data;

	DBFENTER;

	/* MSM: pkx supplied code that  code queries RID FD4D....but the f/w's
         *  results are bogus. Therefore, we have to simulate the appropriate
         *  results here in the driver based on our knowledge of existing MAC
         *  features.  That's the whole point behind this ugly function.
         */

	if (isget) {
		msg->resultcode.data = P80211ENUM_resultcode_success;
		switch (mib->parm1) {
			case 1: /* Open System */
				*uint32 = P80211ENUM_authalg_opensystem;
				break;
			case 2: /* SharedKey */
				*uint32 = P80211ENUM_authalg_sharedkey;
				break;
			default:
				*uint32 = 0;
				msg->resultcode.data = P80211ENUM_resultcode_not_supported;
				break;
		}
	}

	DBFEXIT;
	return(0);
}

/*----------------------------------------------------------------
* prism2mib_authalgenable
*
* Get/set the enable values from the AuhtenticationAlgorithmsTable.
*
* MIB record parameters:
*       parm1    Table index (1-6).
*       parm2    Not used.
*       parm3    Not used.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_authalgenable(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	int     result;
	UINT32  *uint32 = (UINT32*) data;

	int     index;
	UINT16  cnf_auth;
	UINT16	mask;

	DBFENTER;

	index = mib->parm1 - 1;

	result = hfa384x_drvr_getconfig16( hw,
			HFA384x_RID_CNFAUTHENTICATION, &cnf_auth);
	WLAN_LOG_DEBUG(2,"cnfAuthentication0=%d, index=%d\n", cnf_auth, index);

	if (isget) {
		if ( index == 0 || index == 1 ) {
			*uint32 = (cnf_auth & (1<<index)) ?
				P80211ENUM_truth_true: P80211ENUM_truth_false;
		} else {
			*uint32 = P80211ENUM_truth_false;
			msg->resultcode.data = P80211ENUM_resultcode_not_supported;
		}
	} else {
		if ( index == 0 || index == 1 ) {
			mask = 1 << index;
			if (*uint32==P80211ENUM_truth_true ) {
				cnf_auth |= mask;
			} else {
				cnf_auth &= ~mask;
			}
			result = hfa384x_drvr_setconfig16( hw,
					HFA384x_RID_CNFAUTHENTICATION, cnf_auth);
			WLAN_LOG_DEBUG(2,"cnfAuthentication:=%d\n", cnf_auth);
			if ( result ) {
				WLAN_LOG_DEBUG(1,"Unable to set p2cnfAuthentication to %d\n", cnf_auth);
				msg->resultcode.data = P80211ENUM_resultcode_implementation_failure;
			}
		} else {
			msg->resultcode.data = P80211ENUM_resultcode_not_supported;
		}
	}

	DBFEXIT;
	return(result);
}

/*----------------------------------------------------------------
* prism2mib_priv
*
* Get/set values in the "priv" data structure.
*
* MIB record parameters:
*       parm1    Not used.
*       parm2    Not used.
*       parm3    Not used.
*
* Arguments:
*       mib      MIB record.
*       isget    MIBGET/MIBSET flag.
*       wlandev  wlan device structure.
*       priv     "priv" structure.
*       hw       "hw" structure.
*       msg      Message structure.
*       data     Data buffer.
*
* Returns:
*       0   - Success.
*       ~0  - Error.
*
----------------------------------------------------------------*/

static int prism2mib_priv(
mibrec_t                     *mib,
int                          isget,
wlandevice_t                 *wlandev,
hfa384x_t                    *hw,
p80211msg_dot11req_mibset_t  *msg,
void                         *data)
{
	UINT32            *uint32 = (UINT32*) data;
	p80211pstrd_t     *pstr = (p80211pstrd_t*) data;
	p80211macarray_t  *macarray = (p80211macarray_t *) data;

	int  i, cnt, result, done;

	prism2sta_authlist_t  old;

	/*
	** "test" is a lot longer than necessary but who cares?  ...as long as
	** it is long enough!
	*/

	UINT8  test[sizeof(wlandev->rx) + sizeof(hw->tallies)];

	DBFENTER;

	switch (mib->did) {
	case DIDmib_p2_p2Table_p2ReceivedFrameStatistics:

		/*
		** Note: The values in this record are changed by the
		** interrupt handler and therefore cannot be guaranteed
		** to be stable while they are being copied.  However,
		** the interrupt handler will take priority over this
		** code.  Hence, if the same values are copied twice,
		** then we are ensured that the values have not been
		** changed.  If they have, then just try again.  Don't
		** try more than 10 times...if we still haven't got it,
		** then the values we do have are probably good enough.
		** This scheme for copying values is used in order to
		** prevent having to block the interrupt handler while
		** we copy the values.
		*/

		if (isget)
			for (i = 0; i < 10; i++) {
				memcpy(data, &wlandev->rx, sizeof(wlandev->rx));
				memcpy(test, &wlandev->rx, sizeof(wlandev->rx));
				if (memcmp(data, test, sizeof(wlandev->rx)) == 0) break;
			}

		break;

	case DIDmib_p2_p2Table_p2CommunicationTallies:

		/*
		** Note: The values in this record are changed by the
		** interrupt handler and therefore cannot be guaranteed
		** to be stable while they are being copied.  See the
		** note above about copying values.
		*/

		if (isget) {
			result = hfa384x_drvr_commtallies(hw);

			/* ?????? We need to wait a bit here for the */
			/*   tallies to get updated. ?????? */
			/* MSM: TODO: The right way to do this is to
			 *      add a "commtallie" wait queue to the
			 *      priv structure that gets run every time
			 *      we receive a commtally info frame.
			 *      This process would sleep on that
			 *      queue and get awakened when the
			 *      the requested info frame arrives.
			 *      Don't have time to do and test this
			 *      right now.
			 */

			/* Ugh, this is nasty. */
			for (i = 0; i < 10; i++) {
				memcpy(data,
				       &hw->tallies,
				       sizeof(hw->tallies));
				memcpy(test,
				       &hw->tallies,
				       sizeof(hw->tallies));
				if ( memcmp(data,
					    test,
					    sizeof(hw->tallies)) == 0)
					break;
			}
		}

		break;

	case DIDmib_p2_p2Table_p2Authenticated:

		if (isget) {
			prism2mib_priv_authlist(hw, &old);

			macarray->cnt = 0;
			for (i = 0; i < old.cnt; i++) {
				if (!old.assoc[i]) {
					memcpy(macarray->data[macarray->cnt], old.addr[i], WLAN_ADDR_LEN);
					macarray->cnt++;
				}
			}
		}

		break;

	case DIDmib_p2_p2Table_p2Associated:

		if (isget) {
			prism2mib_priv_authlist(hw, &old);

			macarray->cnt = 0;
			for (i = 0; i < old.cnt; i++) {
				if (old.assoc[i]) {
					memcpy(macarray->data[macarray->cnt], old.addr[i], WLAN_ADDR_LEN);
					macarray->cnt++;
				}
			}
		}

		break;

	case DIDmib_p2_p2Table_p2PowerSaveUserCount:

		if (isget)
			*uint32 = hw->psusercount;

		break;

	case DIDmib_p2_p2Table_p2Comment:

		if (isget) {
			pstr->len = strlen(hw->comment);
			memcpy(pstr->data, hw->comment, pstr->len);
		} else {
			cnt = pstr->len;
			if (cnt < 0) cnt = 0;
			if (cnt >= sizeof(hw->comment))
				cnt = sizeof(hw->comment)-1;
			memcpy(hw->comment, pstr->data, cnt);
			pstr->data[cnt] = '\0';
		}

		break;

	case DIDmib_p2_p2Table_p2AccessMode:

		if (isget)
			*uint32 = hw->accessmode;
		else
			prism2mib_priv_accessmode(hw, *uint32);

		break;

	case DIDmib_p2_p2Table_p2AccessAllow:

		if (isget) {
			macarray->cnt = hw->allow.cnt;
			memcpy(macarray->data, hw->allow.addr,
			       macarray->cnt*WLAN_ADDR_LEN);
		} else {
			prism2mib_priv_accessallow(hw, macarray);
		}

		break;

	case DIDmib_p2_p2Table_p2AccessDeny:

		if (isget) {
			macarray->cnt = hw->deny.cnt;
			memcpy(macarray->data, hw->deny.addr,
			       macarray->cnt*WLAN_ADDR_LEN);
		} else {
			prism2mib_priv_accessdeny(hw, macarray);
		}

		break;

	case DIDmib_p2_p2Table_p2ChannelInfoResults:

		if (isget) {
			done = atomic_read(&hw->channel_info.done);
			if (done == 0) {
				msg->resultcode.status = P80211ENUM_msgitem_status_no_value;
				break;
			}
			if (done == 1) {
				msg->resultcode.status = P80211ENUM_msgitem_status_incomplete_itemdata;
				break;
			}

			for (i = 0; i < 14; i++, uint32 += 5) {
				uint32[0] = i+1;
				uint32[1] = hw->channel_info.results.result[i].anl;
				uint32[2] = hw->channel_info.results.result[i].pnl;
				uint32[3] = (hw->channel_info.results.result[i].active & HFA384x_CHINFORESULT_BSSACTIVE) ? 1 : 0;
				uint32[4] = (hw->channel_info.results.result[i].active & HFA384x_CHINFORESULT_PCFACTIVE) ? 1 : 0;
			}
		}

		break;

	case DIDmib_dot11smt_dot11StationConfigTable_dot11DesiredBSSType:

		if (isget)
			*uint32 = hw->dot11_desired_bss_type;
		else
			hw->dot11_desired_bss_type = *uint32;

		break;

	case DIDmib_lnx_lnxConfigTable_lnxRSNAIE: {
		hfa384x_WPAData_t wpa;
		if (isget) {
			hfa384x_drvr_getconfig( hw, HFA384x_RID_CNFWPADATA,
						(UINT8 *) &wpa, sizeof(wpa));
			pstr->len = hfa384x2host_16(wpa.datalen);
			memcpy(pstr->data, wpa.data, pstr->len);
		} else {
			wpa.datalen = host2hfa384x_16(pstr->len);
			memcpy(wpa.data, pstr->data, pstr->len);

			result = hfa384x_drvr_setconfig(hw, HFA384x_RID_CNFWPADATA,
				(UINT8 *) &wpa, sizeof(wpa));
		}
		break;
	}
	default:
		WLAN_LOG_ERROR("Unhandled DID 0x%08x\n", mib->did);
	}

	DBFEXIT;
	return(0);
}

/*----------------------------------------------------------------
* prism2mib_priv_authlist
*
* Get a copy of the list of authenticated stations.
*
* Arguments:
*       priv     "priv" structure.
*       list     List of authenticated stations.
*
* Returns:
*	Nothing
*
----------------------------------------------------------------*/

static void prism2mib_priv_authlist(
hfa384x_t             *hw,
prism2sta_authlist_t  *list)
{
	prism2sta_authlist_t  test;
	int                   i;

	DBFENTER;

	/*
	** Note: The values in this record are changed by the interrupt
	** handler and therefore cannot be guaranteed to be stable while
	** they are being copied.  However, the interrupt handler will
	** take priority over this code.  Hence, if the same values are
	** copied twice, then we are ensured that the values have not
	** been changed.  If they have, then just try again.  Don't try
	** more than 10 times...the list of authenticated stations is
	** unlikely to be changing frequently enough that we can't get
	** a snapshot in 10 tries.  Don't try more than this so that we
	** don't risk locking-up for long periods of time.  If we still
	** haven't got the snapshot, then generate an error message and
	** return an empty list (since this is the only valid list that
	** we can guarentee).  This scheme for copying values is used in
	** order to prevent having to block the interrupt handler while
	** we copy the values.
	*/

	for (i = 0; i < 10; i++) {
		memcpy(list, &hw->authlist, sizeof(prism2sta_authlist_t));
		memcpy(&test, &hw->authlist, sizeof(prism2sta_authlist_t));
		if (memcmp(list, &test, sizeof(prism2sta_authlist_t)) == 0)
			break;
	}

	if (i >= 10) {
		list->cnt = 0;
		WLAN_LOG_ERROR("Could not obtain snapshot of authenticated stations.\n");
		}

	DBFEXIT;
	return;
}

/*----------------------------------------------------------------
* prism2mib_priv_accessmode
*
* Set the Access Mode.
*
* Arguments:
*       priv     "priv" structure.
*       hw       "hw" structure.
*       mode     New access mode.
*
* Returns:
*	Nothing
*
----------------------------------------------------------------*/

static void prism2mib_priv_accessmode(
hfa384x_t         *hw,
UINT32            mode)
{
	prism2sta_authlist_t  old;
	int                   i, j, deauth;
	UINT8                 *addr;

	DBFENTER;

	/*
	** If the mode is not changing or it is changing to "All", then it's
	** okay to go ahead without a lot of messing around.  Otherwise, the
	** access mode is changing in a way that may leave some stations
	** authenticated which should not be authenticated.  It will be
	** necessary to de-authenticate these stations.
	*/

	if (mode == WLAN_ACCESS_ALL || mode == hw->accessmode) {
		hw->accessmode = mode;
		return;
	}

	/*
	** Switch to the new access mode.  Once this is done, then the interrupt
	** handler (which uses this value) will be prevented from authenticating
	** ADDITIONAL stations which should not be authenticated.  Then get a
	** copy of the current list of authenticated stations.
	*/

	hw->accessmode = mode;

	prism2mib_priv_authlist(hw, &old);

	/*
	** Now go through the list of previously authenticated stations (some
	** of which might de-authenticate themselves while we are processing it
	** but that is okay).  Any station which no longer matches the access
	** mode, must be de-authenticated.
	*/

	for (i = 0; i < old.cnt; i++) {
		addr = old.addr[i];

		if (mode == WLAN_ACCESS_NONE)
			deauth = 1;
		else {
			if (mode == WLAN_ACCESS_ALLOW) {
				for (j = 0; j < hw->allow.cnt; j++)
					if (memcmp(addr, hw->allow.addr[j],
							WLAN_ADDR_LEN) == 0)
						break;
				deauth = (j >= hw->allow.cnt);
			} else {
				for (j = 0; j < hw->deny.cnt; j++)
					if (memcmp(addr, hw->deny.addr[j],
							WLAN_ADDR_LEN) == 0)
						break;
				deauth = (j < hw->deny.cnt);
			}
		}

		if (deauth) prism2mib_priv_deauthenticate(hw, addr);
	}

	DBFEXIT;
	return;
}

/*----------------------------------------------------------------
* prism2mib_priv_accessallow
*
* Change the list of allowed MAC addresses.
*
* Arguments:
*       priv      "priv" structure.
*       hw        "hw" structure.
*       macarray  New array of MAC addresses.
*
* Returns:
*	Nothing
*
----------------------------------------------------------------*/

static void prism2mib_priv_accessallow(
hfa384x_t         *hw,
p80211macarray_t  *macarray)
{
	prism2sta_authlist_t  old;
	int                   i, j;

	DBFENTER;

	/*
	** Change the access list.  Note that the interrupt handler may be in
	** the middle of using the access list!!!  Since the interrupt handler
	** will	always have priority over this process and this is the only
	** process that will modify the list, this problem can be handled as
	** follows:
	**
	**    1. Set the "modify" flag.
	**    2. Change the first copy of the list.
	**    3. Clear the "modify" flag.
	**    4. Change the backup copy of the list.
	**
	** The interrupt handler will check the "modify" flag.  If NOT set, then
	** the first copy of the list is valid and may be used.  Otherwise, the
	** first copy is being changed but the backup copy is valid and may be
	** used.  Doing things this way prevents having to have the interrupt
	** handler block while the list is being updated.
	*/

	hw->allow.modify = 1;

	hw->allow.cnt = macarray->cnt;
	memcpy(hw->allow.addr, macarray->data, macarray->cnt*WLAN_ADDR_LEN);

	hw->allow.modify = 0;

	hw->allow.cnt1 = macarray->cnt;
	memcpy(hw->allow.addr1, macarray->data, macarray->cnt*WLAN_ADDR_LEN);

	/*
	** If the current access mode is "Allow", then changing the access
	** list may leave some stations authenticated which should not be
	** authenticated.  It will be necessary to de-authenticate these
	** stations.  Otherwise, the list can be changed without a lot of fuss.
	*/

	if (hw->accessmode == WLAN_ACCESS_ALLOW) {

		/*
		** Go through the list of authenticated stations (some of
		** which might de-authenticate themselves while we are
		** processing it but that is okay).  Any station which is
		** no longer in the list of allowed stations, must be
		** de-authenticated.
		*/

		prism2mib_priv_authlist(hw, &old);

		for (i = 0; i < old.cnt; i++) {
			for (j = 0; j < hw->allow.cnt; j++)
				if (memcmp(old.addr[i], hw->allow.addr[j],
							WLAN_ADDR_LEN) == 0)
					break;
			if (j >= hw->allow.cnt)
				prism2mib_priv_deauthenticate(hw, old.addr[i]);
		}
	}

	DBFEXIT;
	return;
}

/*----------------------------------------------------------------
* prism2mib_priv_accessdeny
*
* Change the list of denied MAC addresses.
*
* Arguments:
*       priv      "priv" structure.
*       hw        "hw" structure.
*       macarray  New array of MAC addresses.
*
* Returns:
*	Nothing
*
----------------------------------------------------------------*/

static void prism2mib_priv_accessdeny(
hfa384x_t         *hw,
p80211macarray_t  *macarray)
{
	prism2sta_authlist_t  old;
	int                   i, j;

	DBFENTER;

	/*
	** Change the access list.  Note that the interrupt handler may be in
	** the middle of using the access list!!!  Since the interrupt handler
	** will always have priority over this process and this is the only
	** process that will modify the list, this problem can be handled as
	** follows:
	**
	**    1. Set the "modify" flag.
	**    2. Change the first copy of the list.
	**    3. Clear the "modify" flag.
	**    4. Change the backup copy of the list.
	**
	** The interrupt handler will check the "modify" flag.  If NOT set, then
	** the first copy of the list is valid and may be used.  Otherwise, the
	** first copy is being changed but the backup copy is valid and may be
	** used.  Doing things this way prevents having to have the interrupt
	** handler block while the list is being updated.
	*/

	hw->deny.modify = 1;

	hw->deny.cnt = macarray->cnt;
	memcpy(hw->deny.addr, macarray->data, macarray->cnt*WLAN_ADDR_LEN);

	hw->deny.modify = 0;

	hw->deny.cnt1 = macarray->cnt;
	memcpy(hw->deny.addr1, macarray->data, macarray->cnt*WLAN_ADDR_LEN);

	/*
	** If the current access mode is "Deny", then changing the access
	** list may leave some stations authenticated which should not be
	** authenticated.  It will be necessary to de-authenticate these
	** stations.  Otherwise, the list can be changed without a lot of fuss.
	*/

	if (hw->accessmode == WLAN_ACCESS_DENY) {

		/*
		** Go through the list of authenticated stations (some of
		** which might de-authenticate themselves while we are
		** processing it but that is okay).  Any station which is
		** now in the list of denied stations, must be de-authenticated.
		*/

		prism2mib_priv_authlist(hw, &old);

		for (i = 0; i < old.cnt; i++)
			for (j = 0; j < hw->deny.cnt; j++)
				if (memcmp(old.addr[i], hw->deny.addr[j],
							 WLAN_ADDR_LEN) == 0) {
					prism2mib_priv_deauthenticate(hw, old.addr[i]);
					break;
				}
	}

	DBFEXIT;
	return;
}

/*----------------------------------------------------------------
* prism2mib_priv_deauthenticate
*
* De-authenticate a station.  This is done by sending a HandoverAddress
* information frame to the firmware.  This should work, according to
* Intersil.
*
* Arguments:
*       priv     "priv" structure.
*       hw       "hw" structure.
*       addr     MAC address of station to be de-authenticated.
*
* Returns:
*	Nothing
*
----------------------------------------------------------------*/

static void prism2mib_priv_deauthenticate(
hfa384x_t         *hw,
UINT8             *addr)
{
	DBFENTER;
	hfa384x_drvr_handover(hw, addr);
	DBFEXIT;
	return;
}


/*----------------------------------------------------------------
* prism2mgmt_pstr2bytestr
*
* Convert the pstr data in the WLAN message structure into an hfa384x
* byte string format.
*
* Arguments:
*	bytestr		hfa384x byte string data type
*	pstr		wlan message data
*
* Returns:
*	Nothing
*
----------------------------------------------------------------*/

void prism2mgmt_pstr2bytestr(hfa384x_bytestr_t *bytestr, p80211pstrd_t *pstr)
{
	DBFENTER;

	bytestr->len = host2hfa384x_16((UINT16)(pstr->len));
	memcpy(bytestr->data, pstr->data, pstr->len);
	DBFEXIT;
}


/*----------------------------------------------------------------
* prism2mgmt_pstr2bytearea
*
* Convert the pstr data in the WLAN message structure into an hfa384x
* byte area format.
*
* Arguments:
*	bytearea	hfa384x byte area data type
*	pstr		wlan message data
*
* Returns:
*	Nothing
*
----------------------------------------------------------------*/

void prism2mgmt_pstr2bytearea(UINT8 *bytearea, p80211pstrd_t *pstr)
{
	DBFENTER;

	memcpy(bytearea, pstr->data, pstr->len);
	DBFEXIT;
}


/*----------------------------------------------------------------
* prism2mgmt_bytestr2pstr
*
* Convert the data in an hfa384x byte string format into a
* pstr in the WLAN message.
*
* Arguments:
*	bytestr		hfa384x byte string data type
*	msg		wlan message
*
* Returns:
*	Nothing
*
----------------------------------------------------------------*/

void prism2mgmt_bytestr2pstr(hfa384x_bytestr_t *bytestr, p80211pstrd_t *pstr)
{
	DBFENTER;

	pstr->len = (UINT8)(hfa384x2host_16((UINT16)(bytestr->len)));
	memcpy(pstr->data, bytestr->data, pstr->len);
	DBFEXIT;
}


/*----------------------------------------------------------------
* prism2mgmt_bytearea2pstr
*
* Convert the data in an hfa384x byte area format into a pstr
* in the WLAN message.
*
* Arguments:
*	bytearea	hfa384x byte area data type
*	msg		wlan message
*
* Returns:
*	Nothing
*
----------------------------------------------------------------*/

void prism2mgmt_bytearea2pstr(UINT8 *bytearea, p80211pstrd_t *pstr, int len)
{
	DBFENTER;

	pstr->len = (UINT8)len;
	memcpy(pstr->data, bytearea, len);
	DBFEXIT;
}


/*----------------------------------------------------------------
* prism2mgmt_prism2int2p80211int
*
* Convert an hfa384x integer into a wlan integer
*
* Arguments:
*	prism2enum	pointer to hfa384x integer
*	wlanenum	pointer to p80211 integer
*
* Returns:
*	Nothing
*
----------------------------------------------------------------*/

void prism2mgmt_prism2int2p80211int(UINT16 *prism2int, UINT32 *wlanint)
{
	DBFENTER;

	*wlanint = (UINT32)hfa384x2host_16(*prism2int);
	DBFEXIT;
}


/*----------------------------------------------------------------
* prism2mgmt_p80211int2prism2int
*
* Convert a wlan integer into an hfa384x integer
*
* Arguments:
*	prism2enum	pointer to hfa384x integer
*	wlanenum	pointer to p80211 integer
*
* Returns:
*	Nothing
*
----------------------------------------------------------------*/

void prism2mgmt_p80211int2prism2int(UINT16 *prism2int, UINT32 *wlanint)
{
	DBFENTER;

	*prism2int = host2hfa384x_16((UINT16)(*wlanint));
	DBFEXIT;
}


/*----------------------------------------------------------------
* prism2mgmt_prism2enum2p80211enum
*
* Convert the hfa384x enumerated int into a p80211 enumerated int
*
* Arguments:
*	prism2enum	pointer to hfa384x integer
*	wlanenum	pointer to p80211 integer
*	rid		hfa384x record id
*
* Returns:
*	Nothing
*
----------------------------------------------------------------*/
void prism2mgmt_prism2enum2p80211enum(UINT16 *prism2enum, UINT32 *wlanenum, UINT16 rid)
{
	DBFENTER;

	/* At the moment, the need for this functionality hasn't
	presented itself. All the wlan enumerated values are
	a 1-to-1 match against the Prism2 enumerated values*/
	DBFEXIT;
	return;
}


/*----------------------------------------------------------------
* prism2mgmt_p80211enum2prism2enum
*
* Convert the p80211 enumerated int into an hfa384x enumerated int
*
* Arguments:
*	prism2enum	pointer to hfa384x integer
*	wlanenum	pointer to p80211 integer
*	rid		hfa384x record id
*
* Returns:
*	Nothing
*
----------------------------------------------------------------*/
void prism2mgmt_p80211enum2prism2enum(UINT16 *prism2enum, UINT32 *wlanenum, UINT16 rid)
{
	DBFENTER;

	/* At the moment, the need for this functionality hasn't
	presented itself. All the wlan enumerated values are
	a 1-to-1 match against the Prism2 enumerated values*/
	DBFEXIT;
	return;
}



/*----------------------------------------------------------------
* prism2mgmt_get_oprateset
*
* Convert the hfa384x bit area into a wlan octet string.
*
* Arguments:
*	rate		Prism2 bit area
*	pstr		wlan octet string
*
* Returns:
*	Nothing
*
----------------------------------------------------------------*/
void prism2mgmt_get_oprateset(UINT16 *rate, p80211pstrd_t *pstr)
{
	UINT8	len;
	UINT8	*datarate;

	DBFENTER;

	len = 0;
	datarate = pstr->data;

 	/* 1 Mbps */
	if ( BIT0 & (*rate) ) {
		len += (UINT8)1;
		*datarate = (UINT8)2;
		datarate++;
	}

 	/* 2 Mbps */
	if ( BIT1 & (*rate) ) {
		len += (UINT8)1;
		*datarate = (UINT8)4;
		datarate++;
	}

 	/* 5.5 Mbps */
	if ( BIT2 & (*rate) ) {
		len += (UINT8)1;
		*datarate = (UINT8)11;
		datarate++;
	}

 	/* 11 Mbps */
	if ( BIT3 & (*rate) ) {
		len += (UINT8)1;
		*datarate = (UINT8)22;
		datarate++;
	}

	pstr->len = len;

	DBFEXIT;
	return;
}



/*----------------------------------------------------------------
* prism2mgmt_set_oprateset
*
* Convert the wlan octet string into an hfa384x bit area.
*
* Arguments:
*	rate		Prism2 bit area
*	pstr		wlan octet string
*
* Returns:
*	Nothing
*
----------------------------------------------------------------*/
void prism2mgmt_set_oprateset(UINT16 *rate, p80211pstrd_t *pstr)
{
	UINT8	*datarate;
	int	i;

	DBFENTER;

	*rate = 0;

	datarate = pstr->data;

	for ( i=0; i < pstr->len; i++, datarate++ ) {
		switch (*datarate) {
		case 2: /* 1 Mbps */
			*rate |= BIT0;
			break;
		case 4: /* 2 Mbps */
			*rate |= BIT1;
			break;
		case 11: /* 5.5 Mbps */
			*rate |= BIT2;
			break;
		case 22: /* 11 Mbps */
			*rate |= BIT3;
			break;
		default:
			WLAN_LOG_DEBUG(1, "Unrecoginzed Rate of %d\n",
				*datarate);
			break;
		}
	}

	DBFEXIT;
	return;
}



/*----------------------------------------------------------------
* prism2mgmt_get_grpaddr
*
* Retrieves a particular group address from the list of
* group addresses.
*
* Arguments:
*	did		mibitem did
*	pstr		wlan octet string
*	priv		prism2 driver private data structure
*
* Returns:
*	Nothing
*
----------------------------------------------------------------*/
void prism2mgmt_get_grpaddr(UINT32 did, p80211pstrd_t *pstr,
	hfa384x_t *hw )
{
	int	index;

	DBFENTER;

	index = prism2mgmt_get_grpaddr_index(did);

	if ( index >= 0 ) {
		pstr->len = WLAN_ADDR_LEN;
		memcpy(pstr->data, hw->dot11_grp_addr[index],
			WLAN_ADDR_LEN);
	}

	DBFEXIT;
	return;
}



/*----------------------------------------------------------------
* prism2mgmt_set_grpaddr
*
* Convert the wlan octet string into an hfa384x bit area.
*
* Arguments:
*	did		mibitem did
*	buf
*	groups
*
* Returns:
*	0	Success
*	!0	Error
*
----------------------------------------------------------------*/
int prism2mgmt_set_grpaddr(UINT32 did, UINT8 *prism2buf,
	p80211pstrd_t *pstr, hfa384x_t *hw )
{
	UINT8	no_addr[WLAN_ADDR_LEN];
	int	index;

	DBFENTER;

	memset(no_addr, 0, WLAN_ADDR_LEN);
	if (memcmp(no_addr, pstr->data, WLAN_ADDR_LEN) != 0) {

		/*
		** The address is NOT 0 so we are "adding" an address to the
		** group address list.  Check to make sure we aren't trying
		** to add more than the maximum allowed number of group
		** addresses in the list.  The new address is added to the
		** end of the list regardless of the DID used to add the
		** address.
		*/

		if (hw->dot11_grpcnt >= MAX_GRP_ADDR) return(-1);

		memcpy(hw->dot11_grp_addr[hw->dot11_grpcnt], pstr->data,
								 WLAN_ADDR_LEN);
		hw->dot11_grpcnt += 1;
	} else {

		/*
		** The address is 0.  Interpret this as "deleting" an address
		** from the group address list.  Get the address index from
		** the DID.  If this is within the range of used addresses,
		** then delete the specified address by shifting all following
		** addresses down.  Then clear the last address (which should
		** now be unused).  If the address index is NOT within the
		** range of used addresses, then just ignore the address.
		*/

		index = prism2mgmt_get_grpaddr_index(did);
		if (index >= 0 && index < hw->dot11_grpcnt) {
			hw->dot11_grpcnt -= 1;
			memmove(hw->dot11_grp_addr[index],
				hw->dot11_grp_addr[index + 1],
				((hw->dot11_grpcnt)-index) * WLAN_ADDR_LEN);
			memset(hw->dot11_grp_addr[hw->dot11_grpcnt], 0,
								 WLAN_ADDR_LEN);
		}
	}

	DBFEXIT;
	return(0);
}


/*----------------------------------------------------------------
* prism2mgmt_get_grpaddr_index
*
* Gets the index in the group address list based on the did.
*
* Arguments:
*	did		mibitem did
*
* Returns:
*	>= 0	If valid did
*	< 0	If not valid did
*
----------------------------------------------------------------*/
int prism2mgmt_get_grpaddr_index( UINT32 did )
{
	int	index;

	DBFENTER;

	index = -1;

	switch (did) {
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address1:
		index = 0;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address2:
		index = 1;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address3:
		index = 2;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address4:
		index = 3;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address5:
		index = 4;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address6:
		index = 5;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address7:
		index = 6;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address8:
		index = 7;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address9:
		index = 8;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address10:
		index = 9;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address11:
		index = 10;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address12:
		index = 11;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address13:
		index = 12;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address14:
		index = 13;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address15:
		index = 14;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address16:
		index = 15;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address17:
		index = 16;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address18:
		index = 17;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address19:
		index = 18;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address20:
		index = 19;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address21:
		index = 20;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address22:
		index = 21;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address23:
		index = 22;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address24:
		index = 23;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address25:
		index = 24;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address26:
		index = 25;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address27:
		index = 26;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address28:
		index = 27;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address29:
		index = 28;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address30:
		index = 29;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address31:
		index = 30;
		break;
	case DIDmib_dot11mac_dot11GroupAddressesTable_dot11Address32:
		index = 31;
		break;
	}

	DBFEXIT;
	return index;
}
