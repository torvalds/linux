/* src/prism2/driver/prism2sta.c
*
* Implements the station functionality for prism2
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
* This file implements the module and linux pcmcia routines for the
* prism2 driver.
*
* --------------------------------------------------------------------
*/

/*================================================================*/
/* System Includes */
#define WLAN_DBVAR	prism2_debug

#include "version.h"


#include <linux/version.h>

#include <linux/module.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,25))
#include <linux/moduleparam.h>
#endif

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/wireless.h>
#include <linux/netdevice.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
#include <linux/tqueue.h>
#else
#include <linux/workqueue.h>
#endif

#include <asm/io.h>
#include <linux/delay.h>
#include <asm/byteorder.h>
#include <linux/if_arp.h>

#if (WLAN_HOSTIF == WLAN_PCMCIA)
#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>
#include <pcmcia/cisreg.h>
#endif

#include "wlan_compat.h"

#if ((WLAN_HOSTIF == WLAN_PLX) || (WLAN_HOSTIF == WLAN_PCI))
#include <linux/ioport.h>
#include <linux/pci.h>
#endif

/*================================================================*/
/* Project Includes */

#include "p80211types.h"
#include "p80211hdr.h"
#include "p80211mgmt.h"
#include "p80211conv.h"
#include "p80211msg.h"
#include "p80211netdev.h"
#include "p80211req.h"
#include "p80211metadef.h"
#include "p80211metastruct.h"
#include "hfa384x.h"
#include "prism2mgmt.h"

/*================================================================*/
/* Local Constants */

/*================================================================*/
/* Local Macros */

/*================================================================*/
/* Local Types */

/*================================================================*/
/* Local Static Definitions */

#if (WLAN_HOSTIF == WLAN_PCMCIA)
#define DRIVER_SUFFIX	"_cs"
#elif (WLAN_HOSTIF == WLAN_PLX)
#define DRIVER_SUFFIX	"_plx"
typedef char* dev_info_t;
#elif (WLAN_HOSTIF == WLAN_PCI)
#define DRIVER_SUFFIX	"_pci"
typedef char* dev_info_t;
#elif (WLAN_HOSTIF == WLAN_USB)
#define DRIVER_SUFFIX	"_usb"
typedef char* dev_info_t;
#else
#error "HOSTIF unsupported or undefined!"
#endif

static char		*version = "prism2" DRIVER_SUFFIX ".o: " WLAN_RELEASE;
static dev_info_t	dev_info = "prism2" DRIVER_SUFFIX;

#if (WLAN_HOSTIF == WLAN_PLX || WLAN_HOSTIF == WLAN_PCI)
#ifdef CONFIG_PM
static int prism2sta_suspend_pci(struct pci_dev *pdev, pm_message_t state);
static int prism2sta_resume_pci(struct pci_dev *pdev);
#endif
#endif

#if (WLAN_HOSTIF == WLAN_PCI)

#endif /* WLAN_PCI */

static wlandevice_t *create_wlan(void);

/*----------------------------------------------------------------*/
/* --Module Parameters */

int      prism2_reset_holdtime=30;	/* Reset hold time in ms */
int	 prism2_reset_settletime=100;	/* Reset settle time in ms */

#if (WLAN_HOSTIF == WLAN_USB)
static int	prism2_doreset=0;		/* Do a reset at init? */
#else
static int      prism2_doreset=1;		/* Do a reset at init? */
int             prism2_bap_timeout=1000;        /* BAP timeout */
int		prism2_irq_evread_max=20;	/* Maximum number of
						 * ev_reads (loops)
						 * in irq handler
						 */
#endif

#ifdef WLAN_INCLUDE_DEBUG
int prism2_debug=0;
module_param( prism2_debug, int, 0644);
MODULE_PARM_DESC(prism2_debug, "prism2 debugging");
#endif

module_param( prism2_doreset, int, 0644);
MODULE_PARM_DESC(prism2_doreset, "Issue a reset on initialization");

module_param( prism2_reset_holdtime, int, 0644);
MODULE_PARM_DESC( prism2_reset_holdtime, "reset hold time in ms");
module_param( prism2_reset_settletime, int, 0644);
MODULE_PARM_DESC( prism2_reset_settletime, "reset settle time in ms");

#if (WLAN_HOSTIF != WLAN_USB)
module_param( prism2_bap_timeout, int, 0644);
MODULE_PARM_DESC(prism2_bap_timeout, "BufferAccessPath Timeout in 10*n us");
module_param( prism2_irq_evread_max, int, 0644);
MODULE_PARM_DESC( prism2_irq_evread_max, "Maximim number of event reads in interrupt handler");
#endif

MODULE_LICENSE("Dual MPL/GPL");

/*================================================================*/
/* Local Function Declarations */

static int	prism2sta_open(wlandevice_t *wlandev);
static int	prism2sta_close(wlandevice_t *wlandev);
static void	prism2sta_reset(wlandevice_t *wlandev );
static int      prism2sta_txframe(wlandevice_t *wlandev, struct sk_buff *skb, p80211_hdr_t *p80211_hdr, p80211_metawep_t *p80211_wep);
static int	prism2sta_mlmerequest(wlandevice_t *wlandev, p80211msg_t *msg);
static int	prism2sta_getcardinfo(wlandevice_t *wlandev);
static int	prism2sta_globalsetup(wlandevice_t *wlandev);
static int      prism2sta_setmulticast(wlandevice_t *wlandev,
				       netdevice_t *dev);

static void	prism2sta_inf_handover(
			wlandevice_t *wlandev, hfa384x_InfFrame_t *inf);
static void	prism2sta_inf_tallies(
			wlandevice_t *wlandev, hfa384x_InfFrame_t *inf);
static void     prism2sta_inf_hostscanresults(
			wlandevice_t *wlandev, hfa384x_InfFrame_t *inf);
static void	prism2sta_inf_scanresults(
			wlandevice_t *wlandev, hfa384x_InfFrame_t *inf);
static void	prism2sta_inf_chinforesults(
			wlandevice_t *wlandev, hfa384x_InfFrame_t *inf);
static void	prism2sta_inf_linkstatus(
			wlandevice_t *wlandev, hfa384x_InfFrame_t *inf);
static void	prism2sta_inf_assocstatus(
			wlandevice_t *wlandev, hfa384x_InfFrame_t *inf);
static void	prism2sta_inf_authreq(
			wlandevice_t *wlandev, hfa384x_InfFrame_t *inf);
static void	prism2sta_inf_authreq_defer(
			wlandevice_t *wlandev, hfa384x_InfFrame_t *inf);
static void	prism2sta_inf_psusercnt(
			wlandevice_t *wlandev, hfa384x_InfFrame_t *inf);

#ifdef CONFIG_PROC_FS
static int
prism2sta_proc_read(
	char	*page,
	char	**start,
	off_t	offset,
	int	count,
	int	*eof,
	void	*data);
#endif

/*================================================================*/
/* Function Definitions */

/*----------------------------------------------------------------
* dmpmem
*
* Debug utility function to dump memory to the kernel debug log.
*
* Arguments:
*	buf	ptr data we want dumped
*	len	length of data
*
* Returns:
*	nothing
* Side effects:
*
* Call context:
*	process thread
*	interrupt
----------------------------------------------------------------*/
inline void dmpmem(void *buf, int n)
{
	int c;
	for ( c= 0; c < n; c++) {
		if ( (c % 16) == 0 ) printk(KERN_DEBUG"dmp[%d]: ", c);
		printk("%02x ", ((UINT8*)buf)[c]);
		if ( (c % 16) == 15 ) printk("\n");
	}
	if ( (c % 16) != 0 ) printk("\n");
}


/*----------------------------------------------------------------
* prism2sta_open
*
* WLAN device open method.  Called from p80211netdev when kernel
* device open (start) method is called in response to the
* SIOCSIIFFLAGS ioctl changing the flags bit IFF_UP
* from clear to set.
*
* Arguments:
*	wlandev		wlan device structure
*
* Returns:
*	0	success
*	>0	f/w reported error
*	<0	driver reported error
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
static int prism2sta_open(wlandevice_t *wlandev)
{
	DBFENTER;

#ifdef ANCIENT_MODULE_CODE
	MOD_INC_USE_COUNT;
#endif

	/* We don't currently have to do anything else.
	 * The setup of the MAC should be subsequently completed via
	 * the mlme commands.
	 * Higher layers know we're ready from dev->start==1 and
	 * dev->tbusy==0.  Our rx path knows to pass up received/
	 * frames because of dev->flags&IFF_UP is true.
	 */

	DBFEXIT;
	return 0;
}


/*----------------------------------------------------------------
* prism2sta_close
*
* WLAN device close method.  Called from p80211netdev when kernel
* device close method is called in response to the
* SIOCSIIFFLAGS ioctl changing the flags bit IFF_UP
* from set to clear.
*
* Arguments:
*	wlandev		wlan device structure
*
* Returns:
*	0	success
*	>0	f/w reported error
*	<0	driver reported error
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
static int prism2sta_close(wlandevice_t *wlandev)
{
	DBFENTER;

#ifdef ANCIENT_MODULE_CODE
	MOD_DEC_USE_COUNT;
#endif

	/* We don't currently have to do anything else.
	 * Higher layers know we're not ready from dev->start==0 and
	 * dev->tbusy==1.  Our rx path knows to not pass up received
	 * frames because of dev->flags&IFF_UP is false.
	 */

	DBFEXIT;
	return 0;
}


/*----------------------------------------------------------------
* prism2sta_reset
*
* Not currently implented.
*
* Arguments:
*	wlandev		wlan device structure
*	none
*
* Returns:
*	nothing
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
static void prism2sta_reset(wlandevice_t *wlandev )
{
	DBFENTER;
	DBFEXIT;
	return;
}


/*----------------------------------------------------------------
* prism2sta_txframe
*
* Takes a frame from p80211 and queues it for transmission.
*
* Arguments:
*	wlandev		wlan device structure
*	pb		packet buffer struct.  Contains an 802.11
*			data frame.
*       p80211_hdr      points to the 802.11 header for the packet.
* Returns:
*	0		Success and more buffs available
*	1		Success but no more buffs
*	2		Allocation failure
*	4		Buffer full or queue busy
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
static int prism2sta_txframe(wlandevice_t *wlandev, struct sk_buff *skb,
			     p80211_hdr_t *p80211_hdr,
			     p80211_metawep_t *p80211_wep)
{
	hfa384x_t		*hw = (hfa384x_t *)wlandev->priv;
	int			result;
	DBFENTER;

	/* If necessary, set the 802.11 WEP bit */
	if ((wlandev->hostwep & (HOSTWEP_PRIVACYINVOKED | HOSTWEP_ENCRYPT)) == HOSTWEP_PRIVACYINVOKED) {
		p80211_hdr->a3.fc |= host2ieee16(WLAN_SET_FC_ISWEP(1));
	}

	result = hfa384x_drvr_txframe(hw, skb, p80211_hdr, p80211_wep);

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* prism2sta_mlmerequest
*
* wlan command message handler.  All we do here is pass the message
* over to the prism2sta_mgmt_handler.
*
* Arguments:
*	wlandev		wlan device structure
*	msg		wlan command message
* Returns:
*	0		success
*	<0		successful acceptance of message, but we're
*			waiting for an async process to finish before
*			we're done with the msg.  When the asynch
*			process is done, we'll call the p80211
*			function p80211req_confirm() .
*	>0		An error occurred while we were handling
*			the message.
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
static int prism2sta_mlmerequest(wlandevice_t *wlandev, p80211msg_t *msg)
{
        hfa384x_t               *hw = (hfa384x_t *)wlandev->priv;

	int result = 0;
	DBFENTER;

	switch( msg->msgcode )
	{
	case DIDmsg_dot11req_mibget :
		WLAN_LOG_DEBUG(2,"Received mibget request\n");
		result = prism2mgmt_mibset_mibget(wlandev, msg);
		break;
	case DIDmsg_dot11req_mibset :
		WLAN_LOG_DEBUG(2,"Received mibset request\n");
		result = prism2mgmt_mibset_mibget(wlandev, msg);
		break;
	case DIDmsg_dot11req_powermgmt :
		WLAN_LOG_DEBUG(2,"Received powermgmt request\n");
		result = prism2mgmt_powermgmt(wlandev, msg);
		break;
	case DIDmsg_dot11req_scan :
		WLAN_LOG_DEBUG(2,"Received scan request\n");
		result = prism2mgmt_scan(wlandev, msg);
		break;
	case DIDmsg_dot11req_scan_results :
		WLAN_LOG_DEBUG(2,"Received scan_results request\n");
		result = prism2mgmt_scan_results(wlandev, msg);
		break;
	case DIDmsg_dot11req_join :
		WLAN_LOG_DEBUG(2,"Received join request\n");
		result = prism2mgmt_join(wlandev, msg);
		break;
	case DIDmsg_dot11req_authenticate :
		WLAN_LOG_DEBUG(2,"Received authenticate request\n");
		result = prism2mgmt_authenticate(wlandev, msg);
		break;
	case DIDmsg_dot11req_deauthenticate :
		WLAN_LOG_DEBUG(2,"Received mlme deauthenticate request\n");
		result = prism2mgmt_deauthenticate(wlandev, msg);
		break;
	case DIDmsg_dot11req_associate :
		WLAN_LOG_DEBUG(2,"Received mlme associate request\n");
		result = prism2mgmt_associate(wlandev, msg);
		break;
	case DIDmsg_dot11req_reassociate :
		WLAN_LOG_DEBUG(2,"Received mlme reassociate request\n");
		result = prism2mgmt_reassociate(wlandev, msg);
		break;
	case DIDmsg_dot11req_disassociate :
		WLAN_LOG_DEBUG(2,"Received mlme disassociate request\n");
		result = prism2mgmt_disassociate(wlandev, msg);
		break;
	case DIDmsg_dot11req_reset :
		WLAN_LOG_DEBUG(2,"Received mlme reset request\n");
		result = prism2mgmt_reset(wlandev, msg);
		break;
	case DIDmsg_dot11req_start :
		WLAN_LOG_DEBUG(2,"Received mlme start request\n");
		result = prism2mgmt_start(wlandev, msg);
		break;
	/*
	 * Prism2 specific messages
	 */
	case DIDmsg_p2req_join :
		WLAN_LOG_DEBUG(2,"Received p2 join request\n");
		result = prism2mgmt_p2_join(wlandev, msg);
		break;
       	case DIDmsg_p2req_readpda :
		WLAN_LOG_DEBUG(2,"Received mlme readpda request\n");
		result = prism2mgmt_readpda(wlandev, msg);
		break;
	case DIDmsg_p2req_readcis :
		WLAN_LOG_DEBUG(2,"Received mlme readcis request\n");
		result = prism2mgmt_readcis(wlandev, msg);
		break;
	case DIDmsg_p2req_auxport_state :
		WLAN_LOG_DEBUG(2,"Received mlme auxport_state request\n");
		result = prism2mgmt_auxport_state(wlandev, msg);
		break;
	case DIDmsg_p2req_auxport_read :
		WLAN_LOG_DEBUG(2,"Received mlme auxport_read request\n");
		result = prism2mgmt_auxport_read(wlandev, msg);
		break;
	case DIDmsg_p2req_auxport_write :
		WLAN_LOG_DEBUG(2,"Received mlme auxport_write request\n");
		result = prism2mgmt_auxport_write(wlandev, msg);
		break;
	case DIDmsg_p2req_low_level :
		WLAN_LOG_DEBUG(2,"Received mlme low_level request\n");
		result = prism2mgmt_low_level(wlandev, msg);
		break;
        case DIDmsg_p2req_test_command :
                WLAN_LOG_DEBUG(2,"Received mlme test_command request\n");
                result = prism2mgmt_test_command(wlandev, msg);
                break;
        case DIDmsg_p2req_mmi_read :
                WLAN_LOG_DEBUG(2,"Received mlme mmi_read request\n");
                result = prism2mgmt_mmi_read(wlandev, msg);
                break;
        case DIDmsg_p2req_mmi_write :
                WLAN_LOG_DEBUG(2,"Received mlme mmi_write request\n");
                result = prism2mgmt_mmi_write(wlandev, msg);
                break;
	case DIDmsg_p2req_ramdl_state :
		WLAN_LOG_DEBUG(2,"Received mlme ramdl_state request\n");
		result = prism2mgmt_ramdl_state(wlandev, msg);
		break;
	case DIDmsg_p2req_ramdl_write :
		WLAN_LOG_DEBUG(2,"Received mlme ramdl_write request\n");
		result = prism2mgmt_ramdl_write(wlandev, msg);
		break;
	case DIDmsg_p2req_flashdl_state :
		WLAN_LOG_DEBUG(2,"Received mlme flashdl_state request\n");
		result = prism2mgmt_flashdl_state(wlandev, msg);
		break;
	case DIDmsg_p2req_flashdl_write :
		WLAN_LOG_DEBUG(2,"Received mlme flashdl_write request\n");
		result = prism2mgmt_flashdl_write(wlandev, msg);
		break;
	case DIDmsg_p2req_dump_state :
		WLAN_LOG_DEBUG(2,"Received mlme dump_state request\n");
		result = prism2mgmt_dump_state(wlandev, msg);
		break;
	case DIDmsg_p2req_channel_info :
		WLAN_LOG_DEBUG(2,"Received mlme channel_info request\n");
		result = prism2mgmt_channel_info(wlandev, msg);
		break;
	case DIDmsg_p2req_channel_info_results :
		WLAN_LOG_DEBUG(2,"Received mlme channel_info_results request\n");
		result = prism2mgmt_channel_info_results(wlandev, msg);
		break;
	/*
	 * Linux specific messages
	 */
	case DIDmsg_lnxreq_hostwep :
		break;   // ignore me.
        case DIDmsg_lnxreq_ifstate :
		{
		p80211msg_lnxreq_ifstate_t	*ifstatemsg;
                WLAN_LOG_DEBUG(2,"Received mlme ifstate request\n");
		ifstatemsg = (p80211msg_lnxreq_ifstate_t*)msg;
                result = prism2sta_ifstate(wlandev, ifstatemsg->ifstate.data);
		ifstatemsg->resultcode.status =
			P80211ENUM_msgitem_status_data_ok;
		ifstatemsg->resultcode.data = result;
		result = 0;
		}
                break;
        case DIDmsg_lnxreq_wlansniff :
                WLAN_LOG_DEBUG(2,"Received mlme wlansniff request\n");
                result = prism2mgmt_wlansniff(wlandev, msg);
                break;
	case DIDmsg_lnxreq_autojoin :
		WLAN_LOG_DEBUG(2,"Received mlme autojoin request\n");
		result = prism2mgmt_autojoin(wlandev, msg);
		break;
	case DIDmsg_p2req_enable :
		WLAN_LOG_DEBUG(2,"Received mlme enable request\n");
		result = prism2mgmt_enable(wlandev, msg);
		break;
	case DIDmsg_lnxreq_commsquality: {
		p80211msg_lnxreq_commsquality_t *qualmsg;

		WLAN_LOG_DEBUG(2,"Received commsquality request\n");

		if (hw->ap)
			break;

		qualmsg = (p80211msg_lnxreq_commsquality_t*) msg;

		qualmsg->link.status = P80211ENUM_msgitem_status_data_ok;
		qualmsg->level.status = P80211ENUM_msgitem_status_data_ok;
		qualmsg->noise.status = P80211ENUM_msgitem_status_data_ok;


		qualmsg->link.data = hfa384x2host_16(hw->qual.CQ_currBSS);
		qualmsg->level.data = hfa384x2host_16(hw->qual.ASL_currBSS);
		qualmsg->noise.data = hfa384x2host_16(hw->qual.ANL_currFC);

		break;
	}
	default:
		WLAN_LOG_WARNING("Unknown mgmt request message 0x%08x", msg->msgcode);
		break;
	}

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* prism2sta_ifstate
*
* Interface state.  This is the primary WLAN interface enable/disable
* handler.  Following the driver/load/deviceprobe sequence, this
* function must be called with a state of "enable" before any other
* commands will be accepted.
*
* Arguments:
*	wlandev		wlan device structure
*	msgp		ptr to msg buffer
*
* Returns:
* 	A p80211 message resultcode value.
*
* Side effects:
*
* Call context:
*	process thread  (usually)
*	interrupt
----------------------------------------------------------------*/
UINT32 prism2sta_ifstate(wlandevice_t *wlandev, UINT32 ifstate)
{
        hfa384x_t               *hw = (hfa384x_t *)wlandev->priv;
	UINT32 			result;
	DBFENTER;

	result = P80211ENUM_resultcode_implementation_failure;

	WLAN_LOG_DEBUG(2, "Current MSD state(%d), requesting(%d)\n",
	                  wlandev->msdstate, ifstate);
	switch (ifstate)
	{
	case P80211ENUM_ifstate_fwload:
		switch (wlandev->msdstate) {
		case WLAN_MSD_HWPRESENT:
			wlandev->msdstate = WLAN_MSD_FWLOAD_PENDING;
			/*
			 * Initialize the device+driver sufficiently
			 * for firmware loading.
			 */
#if (WLAN_HOSTIF != WLAN_USB)
			result=hfa384x_cmd_initialize(hw);
#else
			if ((result=hfa384x_drvr_start(hw))) {
				WLAN_LOG_ERROR(
					"hfa384x_drvr_start() failed,"
					"result=%d\n", (int)result);
				result =
				P80211ENUM_resultcode_implementation_failure;
				wlandev->msdstate = WLAN_MSD_HWPRESENT;
				break;
			}
#endif
			wlandev->msdstate = WLAN_MSD_FWLOAD;
			result = P80211ENUM_resultcode_success;
			break;
		case WLAN_MSD_FWLOAD:
			hfa384x_cmd_initialize(hw);
			result = P80211ENUM_resultcode_success;
			break;
		case WLAN_MSD_RUNNING:
			WLAN_LOG_WARNING(
				"Cannot enter fwload state from enable state,"
				"you must disable first.\n");
			result = P80211ENUM_resultcode_invalid_parameters;
			break;
		case WLAN_MSD_HWFAIL:
		default:
			/* probe() had a problem or the msdstate contains
			 * an unrecognized value, there's nothing we can do.
			 */
			result = P80211ENUM_resultcode_implementation_failure;
			break;
		}
		break;
	case P80211ENUM_ifstate_enable:
		switch (wlandev->msdstate) {
		case WLAN_MSD_HWPRESENT:
		case WLAN_MSD_FWLOAD:
			wlandev->msdstate = WLAN_MSD_RUNNING_PENDING;
			/* Initialize the device+driver for full
			 * operation. Note that this might me an FWLOAD to
			 * to RUNNING transition so we must not do a chip
			 * or board level reset.  Note that on failure,
			 * the MSD state is set to HWPRESENT because we
			 * can't make any assumptions about the state
			 * of the hardware or a previous firmware load.
			 */
			if ((result=hfa384x_drvr_start(hw))) {
				WLAN_LOG_ERROR(
					"hfa384x_drvr_start() failed,"
					"result=%d\n", (int)result);
				result =
				P80211ENUM_resultcode_implementation_failure;
				wlandev->msdstate = WLAN_MSD_HWPRESENT;
				break;
			}

			if ((result=prism2sta_getcardinfo(wlandev))) {
				WLAN_LOG_ERROR(
					"prism2sta_getcardinfo() failed,"
					"result=%d\n", (int)result);
				result =
				P80211ENUM_resultcode_implementation_failure;
				hfa384x_drvr_stop(hw);
				wlandev->msdstate = WLAN_MSD_HWPRESENT;
				break;
			}
			if ((result=prism2sta_globalsetup(wlandev))) {
				WLAN_LOG_ERROR(
					"prism2sta_globalsetup() failed,"
					"result=%d\n", (int)result);
				result =
				P80211ENUM_resultcode_implementation_failure;
				hfa384x_drvr_stop(hw);
				wlandev->msdstate = WLAN_MSD_HWPRESENT;
				break;
			}
			wlandev->msdstate = WLAN_MSD_RUNNING;
			hw->join_ap = 0;
			hw->join_retries = 60;
			result = P80211ENUM_resultcode_success;
			break;
		case WLAN_MSD_RUNNING:
			/* Do nothing, we're already in this state.*/
			result = P80211ENUM_resultcode_success;
			break;
		case WLAN_MSD_HWFAIL:
		default:
			/* probe() had a problem or the msdstate contains
			 * an unrecognized value, there's nothing we can do.
			 */
			result = P80211ENUM_resultcode_implementation_failure;
			break;
		}
		break;
	case P80211ENUM_ifstate_disable:
		switch (wlandev->msdstate) {
		case WLAN_MSD_HWPRESENT:
			/* Do nothing, we're already in this state.*/
			result = P80211ENUM_resultcode_success;
			break;
		case WLAN_MSD_FWLOAD:
		case WLAN_MSD_RUNNING:
			wlandev->msdstate = WLAN_MSD_HWPRESENT_PENDING;
			/*
			 * TODO: Shut down the MAC completely. Here a chip
			 * or board level reset is probably called for.
			 * After a "disable" _all_ results are lost, even
			 * those from a fwload.
			 */
			if (!wlandev->hwremoved)
				netif_carrier_off(wlandev->netdev);

			hfa384x_drvr_stop(hw);

			wlandev->macmode = WLAN_MACMODE_NONE;
			wlandev->msdstate = WLAN_MSD_HWPRESENT;
			result = P80211ENUM_resultcode_success;
			break;
		case WLAN_MSD_HWFAIL:
		default:
			/* probe() had a problem or the msdstate contains
			 * an unrecognized value, there's nothing we can do.
			 */
			result = P80211ENUM_resultcode_implementation_failure;
			break;
		}
		break;
	default:
		result = P80211ENUM_resultcode_invalid_parameters;
		break;
	}

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* prism2sta_getcardinfo
*
* Collect the NICID, firmware version and any other identifiers
* we'd like to have in host-side data structures.
*
* Arguments:
*	wlandev		wlan device structure
*
* Returns:
*	0	success
*	>0	f/w reported error
*	<0	driver reported error
*
* Side effects:
*
* Call context:
*	Either.
----------------------------------------------------------------*/
static int prism2sta_getcardinfo(wlandevice_t *wlandev)
{
	int 			result = 0;
        hfa384x_t               *hw = (hfa384x_t *)wlandev->priv;
	UINT16                  temp;
	UINT8			snum[HFA384x_RID_NICSERIALNUMBER_LEN];
	char			pstr[(HFA384x_RID_NICSERIALNUMBER_LEN * 4) + 1];

	DBFENTER;

	/* Collect version and compatibility info */
	/*  Some are critical, some are not */
	/* NIC identity */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_NICIDENTITY,
			&hw->ident_nic, sizeof(hfa384x_compident_t));
	if ( result ) {
		WLAN_LOG_ERROR("Failed to retrieve NICIDENTITY\n");
		goto failed;
	}

	/* get all the nic id fields in host byte order */
	hw->ident_nic.id = hfa384x2host_16(hw->ident_nic.id);
	hw->ident_nic.variant = hfa384x2host_16(hw->ident_nic.variant);
	hw->ident_nic.major = hfa384x2host_16(hw->ident_nic.major);
	hw->ident_nic.minor = hfa384x2host_16(hw->ident_nic.minor);

	WLAN_LOG_INFO( "ident: nic h/w: id=0x%02x %d.%d.%d\n",
			hw->ident_nic.id, hw->ident_nic.major,
			hw->ident_nic.minor, hw->ident_nic.variant);

	/* Primary f/w identity */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_PRIIDENTITY,
			&hw->ident_pri_fw, sizeof(hfa384x_compident_t));
	if ( result ) {
		WLAN_LOG_ERROR("Failed to retrieve PRIIDENTITY\n");
		goto failed;
	}

	/* get all the private fw id fields in host byte order */
	hw->ident_pri_fw.id = hfa384x2host_16(hw->ident_pri_fw.id);
	hw->ident_pri_fw.variant = hfa384x2host_16(hw->ident_pri_fw.variant);
	hw->ident_pri_fw.major = hfa384x2host_16(hw->ident_pri_fw.major);
	hw->ident_pri_fw.minor = hfa384x2host_16(hw->ident_pri_fw.minor);

	WLAN_LOG_INFO( "ident: pri f/w: id=0x%02x %d.%d.%d\n",
			hw->ident_pri_fw.id, hw->ident_pri_fw.major,
			hw->ident_pri_fw.minor, hw->ident_pri_fw.variant);

	/* Station (Secondary?) f/w identity */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_STAIDENTITY,
			&hw->ident_sta_fw, sizeof(hfa384x_compident_t));
	if ( result ) {
		WLAN_LOG_ERROR("Failed to retrieve STAIDENTITY\n");
		goto failed;
	}

	if (hw->ident_nic.id < 0x8000) {
		WLAN_LOG_ERROR("FATAL: Card is not an Intersil Prism2/2.5/3\n");
		result = -1;
		goto failed;
	}

	/* get all the station fw id fields in host byte order */
	hw->ident_sta_fw.id = hfa384x2host_16(hw->ident_sta_fw.id);
	hw->ident_sta_fw.variant = hfa384x2host_16(hw->ident_sta_fw.variant);
	hw->ident_sta_fw.major = hfa384x2host_16(hw->ident_sta_fw.major);
	hw->ident_sta_fw.minor = hfa384x2host_16(hw->ident_sta_fw.minor);

	/* strip out the 'special' variant bits */
	hw->mm_mods = hw->ident_sta_fw.variant & (BIT14 | BIT15);
	hw->ident_sta_fw.variant &= ~((UINT16)(BIT14 | BIT15));

	if  ( hw->ident_sta_fw.id == 0x1f ) {
		hw->ap = 0;
		WLAN_LOG_INFO(
			"ident: sta f/w: id=0x%02x %d.%d.%d\n",
			hw->ident_sta_fw.id, hw->ident_sta_fw.major,
			hw->ident_sta_fw.minor, hw->ident_sta_fw.variant);
	} else {
		hw->ap = 1;
		WLAN_LOG_INFO(
			"ident:  ap f/w: id=0x%02x %d.%d.%d\n",
			hw->ident_sta_fw.id, hw->ident_sta_fw.major,
			hw->ident_sta_fw.minor, hw->ident_sta_fw.variant);
	}

	/* Compatibility range, Modem supplier */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_MFISUPRANGE,
			&hw->cap_sup_mfi, sizeof(hfa384x_caplevel_t));
	if ( result ) {
		WLAN_LOG_ERROR("Failed to retrieve MFISUPRANGE\n");
		goto failed;
	}

	/* get all the Compatibility range, modem interface supplier
	fields in byte order */
	hw->cap_sup_mfi.role = hfa384x2host_16(hw->cap_sup_mfi.role);
	hw->cap_sup_mfi.id = hfa384x2host_16(hw->cap_sup_mfi.id);
	hw->cap_sup_mfi.variant = hfa384x2host_16(hw->cap_sup_mfi.variant);
	hw->cap_sup_mfi.bottom = hfa384x2host_16(hw->cap_sup_mfi.bottom);
	hw->cap_sup_mfi.top = hfa384x2host_16(hw->cap_sup_mfi.top);

	WLAN_LOG_INFO(
		"MFI:SUP:role=0x%02x:id=0x%02x:var=0x%02x:b/t=%d/%d\n",
		hw->cap_sup_mfi.role, hw->cap_sup_mfi.id,
		hw->cap_sup_mfi.variant, hw->cap_sup_mfi.bottom,
		hw->cap_sup_mfi.top);

	/* Compatibility range, Controller supplier */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_CFISUPRANGE,
			&hw->cap_sup_cfi, sizeof(hfa384x_caplevel_t));
	if ( result ) {
		WLAN_LOG_ERROR("Failed to retrieve CFISUPRANGE\n");
		goto failed;
	}

	/* get all the Compatibility range, controller interface supplier
	fields in byte order */
	hw->cap_sup_cfi.role = hfa384x2host_16(hw->cap_sup_cfi.role);
	hw->cap_sup_cfi.id = hfa384x2host_16(hw->cap_sup_cfi.id);
	hw->cap_sup_cfi.variant = hfa384x2host_16(hw->cap_sup_cfi.variant);
	hw->cap_sup_cfi.bottom = hfa384x2host_16(hw->cap_sup_cfi.bottom);
	hw->cap_sup_cfi.top = hfa384x2host_16(hw->cap_sup_cfi.top);

	WLAN_LOG_INFO(
		"CFI:SUP:role=0x%02x:id=0x%02x:var=0x%02x:b/t=%d/%d\n",
		hw->cap_sup_cfi.role, hw->cap_sup_cfi.id,
		hw->cap_sup_cfi.variant, hw->cap_sup_cfi.bottom,
		hw->cap_sup_cfi.top);

	/* Compatibility range, Primary f/w supplier */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_PRISUPRANGE,
			&hw->cap_sup_pri, sizeof(hfa384x_caplevel_t));
	if ( result ) {
		WLAN_LOG_ERROR("Failed to retrieve PRISUPRANGE\n");
		goto failed;
	}

	/* get all the Compatibility range, primary firmware supplier
	fields in byte order */
	hw->cap_sup_pri.role = hfa384x2host_16(hw->cap_sup_pri.role);
	hw->cap_sup_pri.id = hfa384x2host_16(hw->cap_sup_pri.id);
	hw->cap_sup_pri.variant = hfa384x2host_16(hw->cap_sup_pri.variant);
	hw->cap_sup_pri.bottom = hfa384x2host_16(hw->cap_sup_pri.bottom);
	hw->cap_sup_pri.top = hfa384x2host_16(hw->cap_sup_pri.top);

	WLAN_LOG_INFO(
		"PRI:SUP:role=0x%02x:id=0x%02x:var=0x%02x:b/t=%d/%d\n",
		hw->cap_sup_pri.role, hw->cap_sup_pri.id,
		hw->cap_sup_pri.variant, hw->cap_sup_pri.bottom,
		hw->cap_sup_pri.top);

	/* Compatibility range, Station f/w supplier */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_STASUPRANGE,
			&hw->cap_sup_sta, sizeof(hfa384x_caplevel_t));
	if ( result ) {
		WLAN_LOG_ERROR("Failed to retrieve STASUPRANGE\n");
		goto failed;
	}

	/* get all the Compatibility range, station firmware supplier
	fields in byte order */
	hw->cap_sup_sta.role = hfa384x2host_16(hw->cap_sup_sta.role);
	hw->cap_sup_sta.id = hfa384x2host_16(hw->cap_sup_sta.id);
	hw->cap_sup_sta.variant = hfa384x2host_16(hw->cap_sup_sta.variant);
	hw->cap_sup_sta.bottom = hfa384x2host_16(hw->cap_sup_sta.bottom);
	hw->cap_sup_sta.top = hfa384x2host_16(hw->cap_sup_sta.top);

	if ( hw->cap_sup_sta.id == 0x04 ) {
		WLAN_LOG_INFO(
		"STA:SUP:role=0x%02x:id=0x%02x:var=0x%02x:b/t=%d/%d\n",
		hw->cap_sup_sta.role, hw->cap_sup_sta.id,
		hw->cap_sup_sta.variant, hw->cap_sup_sta.bottom,
		hw->cap_sup_sta.top);
	} else {
		WLAN_LOG_INFO(
		"AP:SUP:role=0x%02x:id=0x%02x:var=0x%02x:b/t=%d/%d\n",
		hw->cap_sup_sta.role, hw->cap_sup_sta.id,
		hw->cap_sup_sta.variant, hw->cap_sup_sta.bottom,
		hw->cap_sup_sta.top);
	}

	/* Compatibility range, primary f/w actor, CFI supplier */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_PRI_CFIACTRANGES,
			&hw->cap_act_pri_cfi, sizeof(hfa384x_caplevel_t));
	if ( result ) {
		WLAN_LOG_ERROR("Failed to retrieve PRI_CFIACTRANGES\n");
		goto failed;
	}

	/* get all the Compatibility range, primary f/w actor, CFI supplier
	fields in byte order */
	hw->cap_act_pri_cfi.role = hfa384x2host_16(hw->cap_act_pri_cfi.role);
	hw->cap_act_pri_cfi.id = hfa384x2host_16(hw->cap_act_pri_cfi.id);
	hw->cap_act_pri_cfi.variant = hfa384x2host_16(hw->cap_act_pri_cfi.variant);
	hw->cap_act_pri_cfi.bottom = hfa384x2host_16(hw->cap_act_pri_cfi.bottom);
	hw->cap_act_pri_cfi.top = hfa384x2host_16(hw->cap_act_pri_cfi.top);

	WLAN_LOG_INFO(
		"PRI-CFI:ACT:role=0x%02x:id=0x%02x:var=0x%02x:b/t=%d/%d\n",
		hw->cap_act_pri_cfi.role, hw->cap_act_pri_cfi.id,
		hw->cap_act_pri_cfi.variant, hw->cap_act_pri_cfi.bottom,
		hw->cap_act_pri_cfi.top);

	/* Compatibility range, sta f/w actor, CFI supplier */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_STA_CFIACTRANGES,
			&hw->cap_act_sta_cfi, sizeof(hfa384x_caplevel_t));
	if ( result ) {
		WLAN_LOG_ERROR("Failed to retrieve STA_CFIACTRANGES\n");
		goto failed;
	}

	/* get all the Compatibility range, station f/w actor, CFI supplier
	fields in byte order */
	hw->cap_act_sta_cfi.role = hfa384x2host_16(hw->cap_act_sta_cfi.role);
	hw->cap_act_sta_cfi.id = hfa384x2host_16(hw->cap_act_sta_cfi.id);
	hw->cap_act_sta_cfi.variant = hfa384x2host_16(hw->cap_act_sta_cfi.variant);
	hw->cap_act_sta_cfi.bottom = hfa384x2host_16(hw->cap_act_sta_cfi.bottom);
	hw->cap_act_sta_cfi.top = hfa384x2host_16(hw->cap_act_sta_cfi.top);

	WLAN_LOG_INFO(
		"STA-CFI:ACT:role=0x%02x:id=0x%02x:var=0x%02x:b/t=%d/%d\n",
		hw->cap_act_sta_cfi.role, hw->cap_act_sta_cfi.id,
		hw->cap_act_sta_cfi.variant, hw->cap_act_sta_cfi.bottom,
		hw->cap_act_sta_cfi.top);

	/* Compatibility range, sta f/w actor, MFI supplier */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_STA_MFIACTRANGES,
			&hw->cap_act_sta_mfi, sizeof(hfa384x_caplevel_t));
	if ( result ) {
		WLAN_LOG_ERROR("Failed to retrieve STA_MFIACTRANGES\n");
		goto failed;
	}

	/* get all the Compatibility range, station f/w actor, MFI supplier
	fields in byte order */
	hw->cap_act_sta_mfi.role = hfa384x2host_16(hw->cap_act_sta_mfi.role);
	hw->cap_act_sta_mfi.id = hfa384x2host_16(hw->cap_act_sta_mfi.id);
	hw->cap_act_sta_mfi.variant = hfa384x2host_16(hw->cap_act_sta_mfi.variant);
	hw->cap_act_sta_mfi.bottom = hfa384x2host_16(hw->cap_act_sta_mfi.bottom);
	hw->cap_act_sta_mfi.top = hfa384x2host_16(hw->cap_act_sta_mfi.top);

	WLAN_LOG_INFO(
		"STA-MFI:ACT:role=0x%02x:id=0x%02x:var=0x%02x:b/t=%d/%d\n",
		hw->cap_act_sta_mfi.role, hw->cap_act_sta_mfi.id,
		hw->cap_act_sta_mfi.variant, hw->cap_act_sta_mfi.bottom,
		hw->cap_act_sta_mfi.top);

	/* Serial Number */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_NICSERIALNUMBER,
			snum, HFA384x_RID_NICSERIALNUMBER_LEN);
	if ( !result ) {
		wlan_mkprintstr(snum, HFA384x_RID_NICSERIALNUMBER_LEN,
				pstr, sizeof(pstr));
		WLAN_LOG_INFO("Prism2 card SN: %s\n", pstr);
	} else {
		WLAN_LOG_ERROR("Failed to retrieve Prism2 Card SN\n");
		goto failed;
	}

	/* Collect the MAC address */
	result = hfa384x_drvr_getconfig(hw, HFA384x_RID_CNFOWNMACADDR,
		wlandev->netdev->dev_addr, WLAN_ADDR_LEN);
	if ( result != 0 ) {
		WLAN_LOG_ERROR("Failed to retrieve mac address\n");
		goto failed;
	}

	/* short preamble is always implemented */
	wlandev->nsdcaps |= P80211_NSDCAP_SHORT_PREAMBLE;

	/* find out if hardware wep is implemented */
	hfa384x_drvr_getconfig16(hw, HFA384x_RID_PRIVACYOPTIMP, &temp);
	if (temp)
		wlandev->nsdcaps |= P80211_NSDCAP_HARDWAREWEP;

	/* get the dBm Scaling constant */
	hfa384x_drvr_getconfig16(hw, HFA384x_RID_CNFDBMADJUST, &temp);
	hw->dbmadjust = temp;

	/* Only enable scan by default on newer firmware */
        if (HFA384x_FIRMWARE_VERSION(hw->ident_sta_fw.major,
                                     hw->ident_sta_fw.minor,
                                     hw->ident_sta_fw.variant) <
	    HFA384x_FIRMWARE_VERSION(1,5,5)) {
		wlandev->nsdcaps |= P80211_NSDCAP_NOSCAN;
	}

	/* TODO: Set any internally managed config items */

	goto done;
failed:
	WLAN_LOG_ERROR("Failed, result=%d\n", result);
done:
	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* prism2sta_globalsetup
*
* Set any global RIDs that we want to set at device activation.
*
* Arguments:
*	wlandev		wlan device structure
*
* Returns:
*	0	success
*	>0	f/w reported error
*	<0	driver reported error
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
static int prism2sta_globalsetup(wlandevice_t *wlandev)
{
	hfa384x_t               *hw = (hfa384x_t *)wlandev->priv;

	/* Set the maximum frame size */
	return hfa384x_drvr_setconfig16(hw, HFA384x_RID_CNFMAXDATALEN,
	                                    WLAN_DATA_MAXLEN);
}

static int prism2sta_setmulticast(wlandevice_t *wlandev, netdevice_t *dev)
{
	int result = 0;
	hfa384x_t               *hw = (hfa384x_t *)wlandev->priv;

	UINT16  promisc;

	DBFENTER;

	/* If we're not ready, what's the point? */
	if ( hw->state != HFA384x_STATE_RUNNING )
		goto exit;

	/* If we're an AP, do nothing here */
	if (hw->ap)
		goto exit;

	if ( (dev->flags & (IFF_PROMISC | IFF_ALLMULTI)) != 0 )
		promisc = P80211ENUM_truth_true;
	else
		promisc = P80211ENUM_truth_false;

	result = hfa384x_drvr_setconfig16_async(hw, HFA384x_RID_PROMISCMODE, promisc);

	/* XXX TODO: configure the multicast list */
	// CLEAR_HW_MULTICAST_LIST
	// struct dev_mc_list element = dev->mc_list;
	// while (element != null) {
	//  HW_ADD_MULTICAST_ADDR(element->dmi_addr, dmi_addrlen)
	//  element = element->next;
	// }

 exit:
	DBFEXIT;
	return result;
}

/*----------------------------------------------------------------
* prism2sta_inf_handover
*
* Handles the receipt of a Handover info frame. Should only be present
* in APs only.
*
* Arguments:
*	wlandev		wlan device structure
*	inf		ptr to info frame (contents in hfa384x order)
*
* Returns:
*	nothing
*
* Side effects:
*
* Call context:
*	interrupt
----------------------------------------------------------------*/
static void prism2sta_inf_handover(wlandevice_t *wlandev, hfa384x_InfFrame_t *inf)
{
	DBFENTER;
	WLAN_LOG_DEBUG(2,"received infoframe:HANDOVER (unhandled)\n");
	DBFEXIT;
	return;
}


/*----------------------------------------------------------------
* prism2sta_inf_tallies
*
* Handles the receipt of a CommTallies info frame.
*
* Arguments:
*	wlandev		wlan device structure
*	inf		ptr to info frame (contents in hfa384x order)
*
* Returns:
*	nothing
*
* Side effects:
*
* Call context:
*	interrupt
----------------------------------------------------------------*/
static void prism2sta_inf_tallies(wlandevice_t *wlandev, hfa384x_InfFrame_t *inf)
{
        hfa384x_t               *hw = (hfa384x_t *)wlandev->priv;
	UINT16			*src16;
	UINT32			*dst;
	UINT32			*src32;
	int			i;
	int			cnt;

	DBFENTER;

	/*
	** Determine if these are 16-bit or 32-bit tallies, based on the
	** record length of the info record.
	*/

	cnt = sizeof(hfa384x_CommTallies32_t) / sizeof(UINT32);
	if (inf->framelen > 22) {
		dst   = (UINT32 *) &hw->tallies;
		src32 = (UINT32 *) &inf->info.commtallies32;
		for (i = 0; i < cnt; i++, dst++, src32++)
			*dst += hfa384x2host_32(*src32);
	} else {
		dst   = (UINT32 *) &hw->tallies;
		src16 = (UINT16 *) &inf->info.commtallies16;
		for (i = 0; i < cnt; i++, dst++, src16++)
			*dst += hfa384x2host_16(*src16);
	}

	DBFEXIT;

	return;
}

/*----------------------------------------------------------------
* prism2sta_inf_scanresults
*
* Handles the receipt of a Scan Results info frame.
*
* Arguments:
*	wlandev		wlan device structure
*	inf		ptr to info frame (contents in hfa384x order)
*
* Returns:
*	nothing
*
* Side effects:
*
* Call context:
*	interrupt
----------------------------------------------------------------*/
static void prism2sta_inf_scanresults(wlandevice_t *wlandev,
				      hfa384x_InfFrame_t *inf)
{

        hfa384x_t               *hw = (hfa384x_t *)wlandev->priv;
	int			nbss;
	hfa384x_ScanResult_t	*sr = &(inf->info.scanresult);
	int			i;
	hfa384x_JoinRequest_data_t	joinreq;
	int			result;
	DBFENTER;

	/* Get the number of results, first in bytes, then in results */
	nbss = (inf->framelen * sizeof(UINT16)) -
		sizeof(inf->infotype) -
		sizeof(inf->info.scanresult.scanreason);
	nbss /= sizeof(hfa384x_ScanResultSub_t);

	/* Print em */
	WLAN_LOG_DEBUG(1,"rx scanresults, reason=%d, nbss=%d:\n",
		inf->info.scanresult.scanreason, nbss);
	for ( i = 0; i < nbss; i++) {
		WLAN_LOG_DEBUG(1, "chid=%d anl=%d sl=%d bcnint=%d\n",
			sr->result[i].chid,
			sr->result[i].anl,
			sr->result[i].sl,
			sr->result[i].bcnint);
		WLAN_LOG_DEBUG(1, "  capinfo=0x%04x proberesp_rate=%d\n",
			sr->result[i].capinfo,
			sr->result[i].proberesp_rate);
	}
	/* issue a join request */
	joinreq.channel = sr->result[0].chid;
	memcpy( joinreq.bssid, sr->result[0].bssid, WLAN_BSSID_LEN);
	result = hfa384x_drvr_setconfig( hw,
			HFA384x_RID_JOINREQUEST,
			&joinreq, HFA384x_RID_JOINREQUEST_LEN);
	if (result) {
		WLAN_LOG_ERROR("setconfig(joinreq) failed, result=%d\n", result);
	}

	DBFEXIT;
	return;
}

/*----------------------------------------------------------------
* prism2sta_inf_hostscanresults
*
* Handles the receipt of a Scan Results info frame.
*
* Arguments:
*	wlandev		wlan device structure
*	inf		ptr to info frame (contents in hfa384x order)
*
* Returns:
*	nothing
*
* Side effects:
*
* Call context:
*	interrupt
----------------------------------------------------------------*/
static void prism2sta_inf_hostscanresults(wlandevice_t *wlandev,
					  hfa384x_InfFrame_t *inf)
{
        hfa384x_t               *hw = (hfa384x_t *)wlandev->priv;
	int			nbss;
	DBFENTER;

	nbss = (inf->framelen - 3) / 32;
	WLAN_LOG_DEBUG(1, "Received %d hostscan results\n", nbss);

	if (nbss > 32)
		nbss = 32;

	if (hw->scanresults)
		kfree(hw->scanresults);

	hw->scanresults = kmalloc(sizeof(hfa384x_InfFrame_t), GFP_ATOMIC);
	memcpy(hw->scanresults, inf, sizeof(hfa384x_InfFrame_t));

	if (nbss == 0)
		nbss = -1;

        /* Notify/wake the sleeping caller. */
        hw->scanflag = nbss;
        wake_up_interruptible(&hw->cmdq);

	DBFEXIT;
};

/*----------------------------------------------------------------
* prism2sta_inf_chinforesults
*
* Handles the receipt of a Channel Info Results info frame.
*
* Arguments:
*	wlandev		wlan device structure
*	inf		ptr to info frame (contents in hfa384x order)
*
* Returns:
*	nothing
*
* Side effects:
*
* Call context:
*	interrupt
----------------------------------------------------------------*/
static void prism2sta_inf_chinforesults(wlandevice_t *wlandev,
					hfa384x_InfFrame_t *inf)
{
        hfa384x_t               *hw = (hfa384x_t *)wlandev->priv;
	unsigned int		i, n;

	DBFENTER;
	hw->channel_info.results.scanchannels =
		hfa384x2host_16(inf->info.chinforesult.scanchannels);
#if 0
	memcpy(&inf->info.chinforesult, &hw->channel_info.results, sizeof(hfa384x_ChInfoResult_t));
#endif

	for (i=0, n=0; i<HFA384x_CHINFORESULT_MAX; i++) {
		if (hw->channel_info.results.scanchannels & (1<<i)) {
			int 	channel=hfa384x2host_16(inf->info.chinforesult.result[n].chid)-1;
			hfa384x_ChInfoResultSub_t *chinforesult=&hw->channel_info.results.result[channel];
			chinforesult->chid   = channel;
			chinforesult->anl    = hfa384x2host_16(inf->info.chinforesult.result[n].anl);
			chinforesult->pnl    = hfa384x2host_16(inf->info.chinforesult.result[n].pnl);
			chinforesult->active = hfa384x2host_16(inf->info.chinforesult.result[n].active);
			WLAN_LOG_DEBUG(2, "chinfo: channel %d, %s level (avg/peak)=%d/%d dB, pcf %d\n",
					channel+1,
					chinforesult->active &
					HFA384x_CHINFORESULT_BSSACTIVE ? "signal" : "noise",
					chinforesult->anl, chinforesult->pnl,
					chinforesult->active & HFA384x_CHINFORESULT_PCFACTIVE ? 1 : 0
			);
			n++;
		}
	}
	atomic_set(&hw->channel_info.done, 2);

	hw->channel_info.count = n;
	DBFEXIT;
	return;
}

void prism2sta_processing_defer(struct work_struct *data)
{
	hfa384x_t		*hw = container_of(data, struct hfa384x, link_bh);
	wlandevice_t            *wlandev = hw->wlandev;
	hfa384x_bytestr32_t ssid;
	int			result;

	DBFENTER;
	/* First let's process the auth frames */
	{
		struct sk_buff          *skb;
		hfa384x_InfFrame_t *inf;

		while ( (skb = skb_dequeue(&hw->authq)) ) {
			inf = (hfa384x_InfFrame_t *) skb->data;
			prism2sta_inf_authreq_defer(wlandev, inf);
		}

	}

	/* Now let's handle the linkstatus stuff */
	if (hw->link_status == hw->link_status_new)
		goto failed;

	hw->link_status = hw->link_status_new;

	switch(hw->link_status) {
	case HFA384x_LINK_NOTCONNECTED:
		/* I'm currently assuming that this is the initial link
		 * state.  It should only be possible immediately
		 * following an Enable command.
		 * Response:
		 * Block Transmits, Ignore receives of data frames
		 */
		netif_carrier_off(wlandev->netdev);

		WLAN_LOG_INFO("linkstatus=NOTCONNECTED (unhandled)\n");
		break;

	case HFA384x_LINK_CONNECTED:
		/* This one indicates a successful scan/join/auth/assoc.
		 * When we have the full MLME complement, this event will
		 * signify successful completion of both mlme_authenticate
		 * and mlme_associate.  State management will get a little
		 * ugly here.
		 * Response:
		 * Indicate authentication and/or association
		 * Enable Transmits, Receives and pass up data frames
		 */

		netif_carrier_on(wlandev->netdev);

		/* If we are joining a specific AP, set our state and reset retries */
		if(hw->join_ap == 1)
			hw->join_ap = 2;
		hw->join_retries = 60;

		/* Don't call this in monitor mode */
		if ( wlandev->netdev->type == ARPHRD_ETHER ) {
			UINT16			portstatus;

			WLAN_LOG_INFO("linkstatus=CONNECTED\n");

			/* For non-usb devices, we can use the sync versions */
			/* Collect the BSSID, and set state to allow tx */

			result = hfa384x_drvr_getconfig(hw,
							HFA384x_RID_CURRENTBSSID,
							wlandev->bssid, WLAN_BSSID_LEN);
			if ( result ) {
				WLAN_LOG_DEBUG(1,
					       "getconfig(0x%02x) failed, result = %d\n",
					       HFA384x_RID_CURRENTBSSID, result);
				goto failed;
			}

			result = hfa384x_drvr_getconfig(hw,
							HFA384x_RID_CURRENTSSID,
							&ssid, sizeof(ssid));
			if ( result ) {
				WLAN_LOG_DEBUG(1,
					       "getconfig(0x%02x) failed, result = %d\n",
					       HFA384x_RID_CURRENTSSID, result);
				goto failed;
			}
			prism2mgmt_bytestr2pstr((hfa384x_bytestr_t *)&ssid,
						(p80211pstrd_t *) &wlandev->ssid);

			/* Collect the port status */
			result = hfa384x_drvr_getconfig16(hw,
							  HFA384x_RID_PORTSTATUS, &portstatus);
			if ( result ) {
				WLAN_LOG_DEBUG(1,
					       "getconfig(0x%02x) failed, result = %d\n",
					       HFA384x_RID_PORTSTATUS, result);
				goto failed;
			}
			wlandev->macmode =
				(portstatus == HFA384x_PSTATUS_CONN_IBSS) ?
				WLAN_MACMODE_IBSS_STA : WLAN_MACMODE_ESS_STA;

			/* Get the ball rolling on the comms quality stuff */
			prism2sta_commsqual_defer(&hw->commsqual_bh);
		}
		break;

	case HFA384x_LINK_DISCONNECTED:
		/* This one indicates that our association is gone.  We've
		 * lost connection with the AP and/or been disassociated.
		 * This indicates that the MAC has completely cleared it's
		 * associated state.  We * should send a deauth indication
		 * (implying disassoc) up * to the MLME.
		 * Response:
		 * Indicate Deauthentication
		 * Block Transmits, Ignore receives of data frames
		 */
		if(hw->join_ap == 2)
		{
			hfa384x_JoinRequest_data_t      joinreq;
			joinreq = hw->joinreq;
			/* Send the join request */
			hfa384x_drvr_setconfig( hw,
				HFA384x_RID_JOINREQUEST,
				&joinreq, HFA384x_RID_JOINREQUEST_LEN);
			WLAN_LOG_INFO("linkstatus=DISCONNECTED (re-submitting join)\n");
		} else {
			if (wlandev->netdev->type == ARPHRD_ETHER)
				WLAN_LOG_INFO("linkstatus=DISCONNECTED (unhandled)\n");
		}
		wlandev->macmode = WLAN_MACMODE_NONE;

		netif_carrier_off(wlandev->netdev);

		break;

	case HFA384x_LINK_AP_CHANGE:
		/* This one indicates that the MAC has decided to and
		 * successfully completed a change to another AP.  We
		 * should probably implement a reassociation indication
		 * in response to this one.  I'm thinking that the the
		 * p80211 layer needs to be notified in case of
		 * buffering/queueing issues.  User mode also needs to be
		 * notified so that any BSS dependent elements can be
		 * updated.
		 * associated state.  We * should send a deauth indication
		 * (implying disassoc) up * to the MLME.
		 * Response:
		 * Indicate Reassociation
		 * Enable Transmits, Receives and pass up data frames
		 */
		WLAN_LOG_INFO("linkstatus=AP_CHANGE\n");

		result = hfa384x_drvr_getconfig(hw,
						HFA384x_RID_CURRENTBSSID,
						wlandev->bssid, WLAN_BSSID_LEN);
		if ( result ) {
			WLAN_LOG_DEBUG(1,
				       "getconfig(0x%02x) failed, result = %d\n",
				       HFA384x_RID_CURRENTBSSID, result);
			goto failed;
		}

		result = hfa384x_drvr_getconfig(hw,
						HFA384x_RID_CURRENTSSID,
						&ssid, sizeof(ssid));
		if ( result ) {
			WLAN_LOG_DEBUG(1,
				       "getconfig(0x%02x) failed, result = %d\n",
				       HFA384x_RID_CURRENTSSID, result);
			goto failed;
		}
		prism2mgmt_bytestr2pstr((hfa384x_bytestr_t *)&ssid,
					(p80211pstrd_t *) &wlandev->ssid);


		hw->link_status = HFA384x_LINK_CONNECTED;
		netif_carrier_on(wlandev->netdev);

		break;

	case HFA384x_LINK_AP_OUTOFRANGE:
		/* This one indicates that the MAC has decided that the
		 * AP is out of range, but hasn't found a better candidate
		 * so the MAC maintains its "associated" state in case
		 * we get back in range.  We should block transmits and
		 * receives in this state.  Do we need an indication here?
		 * Probably not since a polling user-mode element would
		 * get this status from from p2PortStatus(FD40). What about
		 * p80211?
		 * Response:
		 * Block Transmits, Ignore receives of data frames
		 */
		WLAN_LOG_INFO("linkstatus=AP_OUTOFRANGE (unhandled)\n");

		netif_carrier_off(wlandev->netdev);

		break;

	case HFA384x_LINK_AP_INRANGE:
		/* This one indicates that the MAC has decided that the
		 * AP is back in range.  We continue working with our
		 * existing association.
		 * Response:
		 * Enable Transmits, Receives and pass up data frames
		 */
		WLAN_LOG_INFO("linkstatus=AP_INRANGE\n");

		hw->link_status = HFA384x_LINK_CONNECTED;
		netif_carrier_on(wlandev->netdev);

		break;

	case HFA384x_LINK_ASSOCFAIL:
		/* This one is actually a peer to CONNECTED.  We've
		 * requested a join for a given SSID and optionally BSSID.
		 * We can use this one to indicate authentication and
		 * association failures.  The trick is going to be
		 * 1) identifying the failure, and 2) state management.
		 * Response:
		 * Disable Transmits, Ignore receives of data frames
		 */
		if(hw->join_ap && --hw->join_retries > 0)
		{
			hfa384x_JoinRequest_data_t      joinreq;
			joinreq = hw->joinreq;
			/* Send the join request */
			hfa384x_drvr_setconfig( hw,
				HFA384x_RID_JOINREQUEST,
				&joinreq, HFA384x_RID_JOINREQUEST_LEN);
			WLAN_LOG_INFO("linkstatus=ASSOCFAIL (re-submitting join)\n");
		} else {
			WLAN_LOG_INFO("linkstatus=ASSOCFAIL (unhandled)\n");
		}

		netif_carrier_off(wlandev->netdev);

		break;

	default:
		/* This is bad, IO port problems? */
		WLAN_LOG_WARNING(
			"unknown linkstatus=0x%02x\n", hw->link_status);
		goto failed;
		break;
	}

	wlandev->linkstatus = (hw->link_status == HFA384x_LINK_CONNECTED);
#ifdef WIRELESS_EXT
	p80211wext_event_associated(wlandev, wlandev->linkstatus);
#endif

 failed:
	DBFEXIT;
}

/*----------------------------------------------------------------
* prism2sta_inf_linkstatus
*
* Handles the receipt of a Link Status info frame.
*
* Arguments:
*	wlandev		wlan device structure
*	inf		ptr to info frame (contents in hfa384x order)
*
* Returns:
*	nothing
*
* Side effects:
*
* Call context:
*	interrupt
----------------------------------------------------------------*/
static void prism2sta_inf_linkstatus(wlandevice_t *wlandev,
				     hfa384x_InfFrame_t *inf)
{
        hfa384x_t               *hw = (hfa384x_t *)wlandev->priv;

	DBFENTER;

	hw->link_status_new = hfa384x2host_16(inf->info.linkstatus.linkstatus);

	schedule_work(&hw->link_bh);

	DBFEXIT;
	return;
}

/*----------------------------------------------------------------
* prism2sta_inf_assocstatus
*
* Handles the receipt of an Association Status info frame. Should
* be present in APs only.
*
* Arguments:
*	wlandev		wlan device structure
*	inf		ptr to info frame (contents in hfa384x order)
*
* Returns:
*	nothing
*
* Side effects:
*
* Call context:
*	interrupt
----------------------------------------------------------------*/
static void prism2sta_inf_assocstatus(wlandevice_t *wlandev,
				      hfa384x_InfFrame_t *inf)
{
        hfa384x_t               *hw = (hfa384x_t *)wlandev->priv;
	hfa384x_AssocStatus_t	rec;
	int			i;

	DBFENTER;

	memcpy(&rec, &inf->info.assocstatus, sizeof(rec));
	rec.assocstatus = hfa384x2host_16(rec.assocstatus);
	rec.reason      = hfa384x2host_16(rec.reason);

	/*
	** Find the address in the list of authenticated stations.  If it wasn't
	** found, then this address has not been previously authenticated and
	** something weird has happened if this is anything other than an
	** "authentication failed" message.  If the address was found, then
	** set the "associated" flag for that station, based on whether the
	** station is associating or losing its association.  Something weird
	** has also happened if we find the address in the list of authenticated
	** stations but we are getting an "authentication failed" message.
	*/

	for (i = 0; i < hw->authlist.cnt; i++)
		if (memcmp(rec.sta_addr, hw->authlist.addr[i], WLAN_ADDR_LEN) == 0)
			break;

	if (i >= hw->authlist.cnt) {
		if (rec.assocstatus != HFA384x_ASSOCSTATUS_AUTHFAIL)
			WLAN_LOG_WARNING("assocstatus info frame received for non-authenticated station.\n");
	} else {
		hw->authlist.assoc[i] =
			(rec.assocstatus == HFA384x_ASSOCSTATUS_STAASSOC ||
			 rec.assocstatus == HFA384x_ASSOCSTATUS_REASSOC);

		if (rec.assocstatus == HFA384x_ASSOCSTATUS_AUTHFAIL)
			WLAN_LOG_WARNING("authfail assocstatus info frame received for authenticated station.\n");
	}

	DBFEXIT;

	return;
}

/*----------------------------------------------------------------
* prism2sta_inf_authreq
*
* Handles the receipt of an Authentication Request info frame. Should
* be present in APs only.
*
* Arguments:
*	wlandev		wlan device structure
*	inf		ptr to info frame (contents in hfa384x order)
*
* Returns:
*	nothing
*
* Side effects:
*
* Call context:
*	interrupt
*
----------------------------------------------------------------*/
static void prism2sta_inf_authreq(wlandevice_t *wlandev,
				  hfa384x_InfFrame_t *inf)
{
        hfa384x_t               *hw = (hfa384x_t *)wlandev->priv;
	struct sk_buff *skb;

	DBFENTER;

	skb = dev_alloc_skb(sizeof(*inf));
	if (skb) {
		skb_put(skb, sizeof(*inf));
		memcpy(skb->data, inf, sizeof(*inf));
		skb_queue_tail(&hw->authq, skb);
		schedule_work(&hw->link_bh);
	}

	DBFEXIT;
}

static void prism2sta_inf_authreq_defer(wlandevice_t *wlandev,
					hfa384x_InfFrame_t *inf)
{
        hfa384x_t               *hw = (hfa384x_t *)wlandev->priv;
	hfa384x_authenticateStation_data_t  rec;

	int    i, added, result, cnt;
	UINT8  *addr;

	DBFENTER;

	/*
	** Build the AuthenticateStation record.  Initialize it for denying
	** authentication.
	*/

	memcpy(rec.address, inf->info.authreq.sta_addr, WLAN_ADDR_LEN);
	rec.status = P80211ENUM_status_unspec_failure;

	/*
	** Authenticate based on the access mode.
	*/

	switch (hw->accessmode) {
		case WLAN_ACCESS_NONE:

			/*
			** Deny all new authentications.  However, if a station
			** is ALREADY authenticated, then accept it.
			*/

			for (i = 0; i < hw->authlist.cnt; i++)
				if (memcmp(rec.address, hw->authlist.addr[i],
						WLAN_ADDR_LEN) == 0) {
					rec.status = P80211ENUM_status_successful;
					break;
				}

			break;

		case WLAN_ACCESS_ALL:

			/*
			** Allow all authentications.
			*/

			rec.status = P80211ENUM_status_successful;
			break;

		case WLAN_ACCESS_ALLOW:

			/*
			** Only allow the authentication if the MAC address
			** is in the list of allowed addresses.
			**
			** Since this is the interrupt handler, we may be here
			** while the access list is in the middle of being
			** updated.  Choose the list which is currently okay.
			** See "prism2mib_priv_accessallow()" for details.
			*/

			if (hw->allow.modify == 0) {
				cnt  = hw->allow.cnt;
				addr = hw->allow.addr[0];
			} else {
				cnt  = hw->allow.cnt1;
				addr = hw->allow.addr1[0];
			}

			for (i = 0; i < cnt; i++, addr += WLAN_ADDR_LEN)
				if (memcmp(rec.address, addr, WLAN_ADDR_LEN) == 0) {
					rec.status = P80211ENUM_status_successful;
					break;
				}

			break;

		case WLAN_ACCESS_DENY:

			/*
			** Allow the authentication UNLESS the MAC address is
			** in the list of denied addresses.
			**
			** Since this is the interrupt handler, we may be here
			** while the access list is in the middle of being
			** updated.  Choose the list which is currently okay.
			** See "prism2mib_priv_accessdeny()" for details.
			*/

			if (hw->deny.modify == 0) {
				cnt  = hw->deny.cnt;
				addr = hw->deny.addr[0];
			} else {
				cnt  = hw->deny.cnt1;
				addr = hw->deny.addr1[0];
			}

			rec.status = P80211ENUM_status_successful;

			for (i = 0; i < cnt; i++, addr += WLAN_ADDR_LEN)
				if (memcmp(rec.address, addr, WLAN_ADDR_LEN) == 0) {
					rec.status = P80211ENUM_status_unspec_failure;
					break;
				}

			break;
	}

	/*
	** If the authentication is okay, then add the MAC address to the list
	** of authenticated stations.  Don't add the address if it is already in
	** the list.  (802.11b does not seem to disallow a station from issuing
	** an authentication request when the station is already authenticated.
	** Does this sort of thing ever happen?  We might as well do the check
	** just in case.)
	*/

	added = 0;

	if (rec.status == P80211ENUM_status_successful) {
		for (i = 0; i < hw->authlist.cnt; i++)
			if (memcmp(rec.address, hw->authlist.addr[i], WLAN_ADDR_LEN) == 0)
				break;

		if (i >= hw->authlist.cnt) {
			if (hw->authlist.cnt >= WLAN_AUTH_MAX) {
				rec.status = P80211ENUM_status_ap_full;
			} else {
				memcpy(hw->authlist.addr[hw->authlist.cnt],
					rec.address, WLAN_ADDR_LEN);
				hw->authlist.cnt++;
				added = 1;
			}
		}
	}

	/*
	** Send back the results of the authentication.  If this doesn't work,
	** then make sure to remove the address from the authenticated list if
	** it was added.
	*/

	rec.status = host2hfa384x_16(rec.status);
	rec.algorithm = inf->info.authreq.algorithm;

	result = hfa384x_drvr_setconfig(hw, HFA384x_RID_AUTHENTICATESTA,
							&rec, sizeof(rec));
	if (result) {
		if (added) hw->authlist.cnt--;
		WLAN_LOG_ERROR("setconfig(authenticatestation) failed, result=%d\n", result);
	}

	DBFEXIT;

	return;
}


/*----------------------------------------------------------------
* prism2sta_inf_psusercnt
*
* Handles the receipt of a PowerSaveUserCount info frame. Should
* be present in APs only.
*
* Arguments:
*	wlandev		wlan device structure
*	inf		ptr to info frame (contents in hfa384x order)
*
* Returns:
*	nothing
*
* Side effects:
*
* Call context:
*	interrupt
----------------------------------------------------------------*/
static void prism2sta_inf_psusercnt(wlandevice_t *wlandev,
				    hfa384x_InfFrame_t *inf)
{
        hfa384x_t               *hw = (hfa384x_t *)wlandev->priv;

	DBFENTER;

	hw->psusercount = hfa384x2host_16(inf->info.psusercnt.usercnt);

	DBFEXIT;

	return;
}

/*----------------------------------------------------------------
* prism2sta_ev_dtim
*
* Handles the DTIM early warning event.
*
* Arguments:
*	wlandev		wlan device structure
*
* Returns:
*	nothing
*
* Side effects:
*
* Call context:
*	interrupt
----------------------------------------------------------------*/
void prism2sta_ev_dtim(wlandevice_t *wlandev)
{
#if 0
        hfa384x_t               *hw = (hfa384x_t *)wlandev->priv;
#endif
	DBFENTER;
	WLAN_LOG_DEBUG(3, "DTIM event, currently unhandled.\n");
	DBFEXIT;
	return;
}


/*----------------------------------------------------------------
* prism2sta_ev_infdrop
*
* Handles the InfDrop event.
*
* Arguments:
*	wlandev		wlan device structure
*
* Returns:
*	nothing
*
* Side effects:
*
* Call context:
*	interrupt
----------------------------------------------------------------*/
void prism2sta_ev_infdrop(wlandevice_t *wlandev)
{
#if 0
        hfa384x_t               *hw = (hfa384x_t *)wlandev->priv;
#endif
	DBFENTER;
	WLAN_LOG_DEBUG(3, "Info frame dropped due to card mem low.\n");
	DBFEXIT;
	return;
}


/*----------------------------------------------------------------
* prism2sta_ev_info
*
* Handles the Info event.
*
* Arguments:
*	wlandev		wlan device structure
*	inf		ptr to a generic info frame
*
* Returns:
*	nothing
*
* Side effects:
*
* Call context:
*	interrupt
----------------------------------------------------------------*/
void prism2sta_ev_info(wlandevice_t *wlandev, hfa384x_InfFrame_t *inf)
{
	DBFENTER;
	inf->infotype = hfa384x2host_16(inf->infotype);
	/* Dispatch */
	switch ( inf->infotype ) {
		case HFA384x_IT_HANDOVERADDR:
			prism2sta_inf_handover(wlandev, inf);
			break;
		case HFA384x_IT_COMMTALLIES:
			prism2sta_inf_tallies(wlandev, inf);
			break;
               case HFA384x_IT_HOSTSCANRESULTS:
                        prism2sta_inf_hostscanresults(wlandev, inf);
                        break;
		case HFA384x_IT_SCANRESULTS:
			prism2sta_inf_scanresults(wlandev, inf);
			break;
		case HFA384x_IT_CHINFORESULTS:
			prism2sta_inf_chinforesults(wlandev, inf);
			break;
		case HFA384x_IT_LINKSTATUS:
			prism2sta_inf_linkstatus(wlandev, inf);
			break;
		case HFA384x_IT_ASSOCSTATUS:
			prism2sta_inf_assocstatus(wlandev, inf);
			break;
		case HFA384x_IT_AUTHREQ:
			prism2sta_inf_authreq(wlandev, inf);
			break;
		case HFA384x_IT_PSUSERCNT:
			prism2sta_inf_psusercnt(wlandev, inf);
			break;
	        case HFA384x_IT_KEYIDCHANGED:
			WLAN_LOG_WARNING("Unhandled IT_KEYIDCHANGED\n");
			break;
	        case HFA384x_IT_ASSOCREQ:
			WLAN_LOG_WARNING("Unhandled IT_ASSOCREQ\n");
			break;
	        case HFA384x_IT_MICFAILURE:
			WLAN_LOG_WARNING("Unhandled IT_MICFAILURE\n");
			break;
		default:
			WLAN_LOG_WARNING(
				"Unknown info type=0x%02x\n", inf->infotype);
			break;
	}
	DBFEXIT;
	return;
}


/*----------------------------------------------------------------
* prism2sta_ev_txexc
*
* Handles the TxExc event.  A Transmit Exception event indicates
* that the MAC's TX process was unsuccessful - so the packet did
* not get transmitted.
*
* Arguments:
*	wlandev		wlan device structure
*	status		tx frame status word
*
* Returns:
*	nothing
*
* Side effects:
*
* Call context:
*	interrupt
----------------------------------------------------------------*/
void prism2sta_ev_txexc(wlandevice_t *wlandev, UINT16 status)
{
	DBFENTER;

	WLAN_LOG_DEBUG(3, "TxExc status=0x%x.\n", status);

	DBFEXIT;
	return;
}


/*----------------------------------------------------------------
* prism2sta_ev_tx
*
* Handles the Tx event.
*
* Arguments:
*	wlandev		wlan device structure
*	status		tx frame status word
* Returns:
*	nothing
*
* Side effects:
*
* Call context:
*	interrupt
----------------------------------------------------------------*/
void prism2sta_ev_tx(wlandevice_t *wlandev, UINT16 status)
{
	DBFENTER;
	WLAN_LOG_DEBUG(4, "Tx Complete, status=0x%04x\n", status);
	/* update linux network stats */
	wlandev->linux_stats.tx_packets++;
	DBFEXIT;
	return;
}


/*----------------------------------------------------------------
* prism2sta_ev_rx
*
* Handles the Rx event.
*
* Arguments:
*	wlandev		wlan device structure
*
* Returns:
*	nothing
*
* Side effects:
*
* Call context:
*	interrupt
----------------------------------------------------------------*/
void prism2sta_ev_rx(wlandevice_t *wlandev, struct sk_buff *skb)
{
	DBFENTER;

	p80211netdev_rx(wlandev, skb);

	DBFEXIT;
	return;
}

/*----------------------------------------------------------------
* prism2sta_ev_alloc
*
* Handles the Alloc event.
*
* Arguments:
*	wlandev		wlan device structure
*
* Returns:
*	nothing
*
* Side effects:
*
* Call context:
*	interrupt
----------------------------------------------------------------*/
void prism2sta_ev_alloc(wlandevice_t *wlandev)
{
	DBFENTER;

	p80211netdev_wake_queue(wlandev);

	DBFEXIT;
	return;
}

#if (WLAN_HOSTIF == WLAN_PLX || WLAN_HOSTIF == WLAN_PCI)
#ifdef CONFIG_PM
static int prism2sta_suspend_pci(struct pci_dev *pdev, pm_message_t state)
{
       	wlandevice_t		*wlandev;

	wlandev = (wlandevice_t *) pci_get_drvdata(pdev);

	/* reset hardware */
	if (wlandev) {
		prism2sta_ifstate(wlandev, P80211ENUM_ifstate_disable);
		p80211_suspend(wlandev);
	}

	// call a netif_device_detach(wlandev->netdev) ?

	return 0;
}

static int prism2sta_resume_pci (struct pci_dev *pdev)
{
       	wlandevice_t		*wlandev;

	wlandev = (wlandevice_t *) pci_get_drvdata(pdev);

	if (wlandev) {
		prism2sta_ifstate(wlandev, P80211ENUM_ifstate_disable);
		p80211_resume(wlandev);
	}

        return 0;
}
#endif
#endif

/*----------------------------------------------------------------
* create_wlan
*
* Called at module init time.  This creates the wlandevice_t structure
* and initializes it with relevant bits.
*
* Arguments:
*	none
*
* Returns:
*	the created wlandevice_t structure.
*
* Side effects:
* 	also allocates the priv/hw structures.
*
* Call context:
*	process thread
*
----------------------------------------------------------------*/
static wlandevice_t *create_wlan(void)
{
        wlandevice_t    *wlandev = NULL;
	hfa384x_t	*hw = NULL;

      	/* Alloc our structures */
	wlandev =	kmalloc(sizeof(wlandevice_t), GFP_KERNEL);
	hw =		kmalloc(sizeof(hfa384x_t), GFP_KERNEL);

	if (!wlandev || !hw) {
		WLAN_LOG_ERROR("%s: Memory allocation failure.\n", dev_info);
		if (wlandev)	kfree(wlandev);
		if (hw)		kfree(hw);
		return NULL;
	}

	/* Clear all the structs */
	memset(wlandev, 0, sizeof(wlandevice_t));
	memset(hw, 0, sizeof(hfa384x_t));

	/* Initialize the network device object. */
	wlandev->nsdname = dev_info;
	wlandev->msdstate = WLAN_MSD_HWPRESENT_PENDING;
	wlandev->priv = hw;
	wlandev->open = prism2sta_open;
	wlandev->close = prism2sta_close;
	wlandev->reset = prism2sta_reset;
#ifdef CONFIG_PROC_FS
	wlandev->nsd_proc_read = prism2sta_proc_read;
#endif
	wlandev->txframe = prism2sta_txframe;
	wlandev->mlmerequest = prism2sta_mlmerequest;
	wlandev->set_multicast_list = prism2sta_setmulticast;
	wlandev->tx_timeout = hfa384x_tx_timeout;

	wlandev->nsdcaps = P80211_NSDCAP_HWFRAGMENT |
	                   P80211_NSDCAP_AUTOJOIN;

	/* Initialize the device private data stucture. */
        hw->dot11_desired_bss_type = 1;

	return wlandev;
}

#ifdef CONFIG_PROC_FS
static int
prism2sta_proc_read(
	char	*page,
	char	**start,
	off_t	offset,
	int	count,
	int	*eof,
	void	*data)
{
	char	 *p = page;
	wlandevice_t *wlandev = (wlandevice_t *) data;
	hfa384x_t *hw = (hfa384x_t *) wlandev->priv;

	UINT16 hwtype = 0;

	DBFENTER;
	if (offset != 0) {
		*eof = 1;
		goto exit;
	}

	// XXX 0x0001 for prism2.5/3, 0x0000 for prism2.
	hwtype = BIT0;

#if (WLAN_HOSTIF != WLAN_USB)
	if (hw->isram16)
		hwtype |= BIT1;
#endif

#if (WLAN_HOSTIF == WLAN_PCI)
	hwtype |= BIT2;
#endif

#define PRISM2_CVS_ID "$Id: prism2sta.c 1826 2007-03-19 15:37:00Z pizza $"

	p += sprintf(p, "# %s version %s (%s) '%s'\n\n",
		     dev_info,
		     WLAN_RELEASE, WLAN_BUILD_DATE, PRISM2_CVS_ID);

	p += sprintf(p, "# nic h/w: id=0x%02x %d.%d.%d\n",
		     hw->ident_nic.id, hw->ident_nic.major,
		     hw->ident_nic.minor, hw->ident_nic.variant);

	p += sprintf(p, "# pri f/w: id=0x%02x %d.%d.%d\n",
		     hw->ident_pri_fw.id, hw->ident_pri_fw.major,
		     hw->ident_pri_fw.minor, hw->ident_pri_fw.variant);

	if (hw->ident_sta_fw.id == 0x1f) {
		p += sprintf(p, "# sta f/w: id=0x%02x %d.%d.%d\n",
			     hw->ident_sta_fw.id, hw->ident_sta_fw.major,
			     hw->ident_sta_fw.minor, hw->ident_sta_fw.variant);
	} else {
		p += sprintf(p, "# ap f/w: id=0x%02x %d.%d.%d\n",
			     hw->ident_sta_fw.id, hw->ident_sta_fw.major,
			     hw->ident_sta_fw.minor, hw->ident_sta_fw.variant);
	}

#if (WLAN_HOSTIF != WLAN_USB)
	p += sprintf(p, "# initial nic hw type, needed for SSF ramdl\n");
	p += sprintf(p, "initnichw=%04x\n", hwtype);
#endif

 exit:
	DBFEXIT;
	return (p - page);
}
#endif

void prism2sta_commsqual_defer(struct work_struct *data)
{
	hfa384x_t		*hw = container_of(data, struct hfa384x, commsqual_bh);
        wlandevice_t            *wlandev = hw->wlandev;
	hfa384x_bytestr32_t ssid;
	int result = 0;

	DBFENTER;

	if (hw->wlandev->hwremoved)
		goto done;

	/* we don't care if we're in AP mode */
	if ((wlandev->macmode == WLAN_MACMODE_NONE) ||
	    (wlandev->macmode == WLAN_MACMODE_ESS_AP)) {
		goto done;
	}

	/* It only makes sense to poll these in non-IBSS */
	if (wlandev->macmode != WLAN_MACMODE_IBSS_STA) {
		result = hfa384x_drvr_getconfig(hw, HFA384x_RID_DBMCOMMSQUALITY,
						&hw->qual,
						HFA384x_RID_DBMCOMMSQUALITY_LEN);

		if (result) {
			WLAN_LOG_ERROR("error fetching commsqual\n");
			goto done;
		}

		// qual.CQ_currBSS; // link
		// ASL_currBSS;  // level
		// qual.ANL_currFC; // noise

		WLAN_LOG_DEBUG(3, "commsqual %d %d %d\n",
			       hfa384x2host_16(hw->qual.CQ_currBSS),
			       hfa384x2host_16(hw->qual.ASL_currBSS),
			       hfa384x2host_16(hw->qual.ANL_currFC));
	}

	/* Lastly, we need to make sure the BSSID didn't change on us */
	result = hfa384x_drvr_getconfig(hw,
					HFA384x_RID_CURRENTBSSID,
					wlandev->bssid, WLAN_BSSID_LEN);
	if ( result ) {
		WLAN_LOG_DEBUG(1,
			       "getconfig(0x%02x) failed, result = %d\n",
			       HFA384x_RID_CURRENTBSSID, result);
		goto done;
	}

	result = hfa384x_drvr_getconfig(hw,
					HFA384x_RID_CURRENTSSID,
					&ssid, sizeof(ssid));
	if ( result ) {
		WLAN_LOG_DEBUG(1,
			       "getconfig(0x%02x) failed, result = %d\n",
			       HFA384x_RID_CURRENTSSID, result);
		goto done;
	}
	prism2mgmt_bytestr2pstr((hfa384x_bytestr_t *)&ssid,
				(p80211pstrd_t *) &wlandev->ssid);


	/* Reschedule timer */
	mod_timer(&hw->commsqual_timer, jiffies + HZ);

 done:
	DBFEXIT;
}

void prism2sta_commsqual_timer(unsigned long data)
{
        hfa384x_t               *hw = (hfa384x_t *) data;

	DBFENTER;

	schedule_work(&hw->commsqual_bh);

	DBFEXIT;
}
