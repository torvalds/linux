/* src/prism2/driver/hfa384x.c
*
* Implements the functions of the Intersil hfa384x MAC
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
* This file implements functions that correspond to the prism2/hfa384x
* 802.11 MAC hardware and firmware host interface.
*
* The functions can be considered to represent several levels of
* abstraction.  The lowest level functions are simply C-callable wrappers
* around the register accesses.  The next higher level represents C-callable
* prism2 API functions that match the Intersil documentation as closely
* as is reasonable.  The next higher layer implements common sequences
* of invokations of the API layer (e.g. write to bap, followed by cmd).
*
* Common sequences:
* hfa384x_drvr_xxx	Highest level abstractions provided by the
*			hfa384x code.  They are driver defined wrappers
*			for common sequences.  These functions generally
*			use the services of the lower levels.
*
* hfa384x_drvr_xxxconfig  An example of the drvr level abstraction. These
*			functions are wrappers for the RID get/set
*			sequence. They 	call copy_[to|from]_bap() and
*			cmd_access().	These functions operate on the
*			RIDs and buffers without validation.  The caller
*			is responsible for that.
*
* API wrapper functions:
* hfa384x_cmd_xxx	functions that provide access to the f/w commands.
*			The function arguments correspond to each command
*			argument, even command arguments that get packed
*			into single registers.  These functions _just_
*			issue the command by setting the cmd/parm regs
*			& reading the status/resp regs.  Additional
*			activities required to fully use a command
*			(read/write from/to bap, get/set int status etc.)
*			are implemented separately.  Think of these as
*			C-callable prism2 commands.
*
* Lowest Layer Functions:
* hfa384x_docmd_xxx	These functions implement the sequence required
*			to issue any prism2 command.  Primarily used by the
*			hfa384x_cmd_xxx functions.
*
* hfa384x_bap_xxx	BAP read/write access functions.
*			Note: we usually use BAP0 for non-interrupt context
*			 and BAP1 for interrupt context.
*
* hfa384x_dl_xxx	download related functions.
*
* Driver State Issues:
* Note that there are two pairs of functions that manage the
* 'initialized' and 'running' states of the hw/MAC combo.  The four
* functions are create(), destroy(), start(), and stop().  create()
* sets up the data structures required to support the hfa384x_*
* functions and destroy() cleans them up.  The start() function gets
* the actual hardware running and enables the interrupts.  The stop()
* function shuts the hardware down.  The sequence should be:
* create()
*  .
*  .  Self contained test routines can run here, particularly
*  .  corereset() and test_hostif().
*  .
* start()
*  .
*  .  Do interesting things w/ the hardware
*  .
* stop()
* destroy()
*
* Note that destroy() can be called without calling stop() first.
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
#include <linux/timer.h>
#include <asm/semaphore.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <asm/byteorder.h>
#include <linux/list.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
#include <linux/tqueue.h>
#else
#include <linux/workqueue.h>
#endif

#if (WLAN_HOSTIF == WLAN_PCMCIA)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,13) )
#include <pcmcia/version.h>
#endif
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>
#include <pcmcia/cisreg.h>
#endif

#if ((WLAN_HOSTIF == WLAN_PLX) || (WLAN_HOSTIF == WLAN_PCI))
#include <linux/ioport.h>
#include <linux/pci.h>
#endif

#include "wlan_compat.h"

// XXXX #define CMD_IRQ

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

static const UINT16 crc16tab[256] =
{
	0x0000, 0xc0c1, 0xc181, 0x0140, 0xc301, 0x03c0, 0x0280, 0xc241,
	0xc601, 0x06c0, 0x0780, 0xc741, 0x0500, 0xc5c1, 0xc481, 0x0440,
	0xcc01, 0x0cc0, 0x0d80, 0xcd41, 0x0f00, 0xcfc1, 0xce81, 0x0e40,
	0x0a00, 0xcac1, 0xcb81, 0x0b40, 0xc901, 0x09c0, 0x0880, 0xc841,
	0xd801, 0x18c0, 0x1980, 0xd941, 0x1b00, 0xdbc1, 0xda81, 0x1a40,
	0x1e00, 0xdec1, 0xdf81, 0x1f40, 0xdd01, 0x1dc0, 0x1c80, 0xdc41,
	0x1400, 0xd4c1, 0xd581, 0x1540, 0xd701, 0x17c0, 0x1680, 0xd641,
	0xd201, 0x12c0, 0x1380, 0xd341, 0x1100, 0xd1c1, 0xd081, 0x1040,
	0xf001, 0x30c0, 0x3180, 0xf141, 0x3300, 0xf3c1, 0xf281, 0x3240,
	0x3600, 0xf6c1, 0xf781, 0x3740, 0xf501, 0x35c0, 0x3480, 0xf441,
	0x3c00, 0xfcc1, 0xfd81, 0x3d40, 0xff01, 0x3fc0, 0x3e80, 0xfe41,
	0xfa01, 0x3ac0, 0x3b80, 0xfb41, 0x3900, 0xf9c1, 0xf881, 0x3840,
	0x2800, 0xe8c1, 0xe981, 0x2940, 0xeb01, 0x2bc0, 0x2a80, 0xea41,
	0xee01, 0x2ec0, 0x2f80, 0xef41, 0x2d00, 0xedc1, 0xec81, 0x2c40,
	0xe401, 0x24c0, 0x2580, 0xe541, 0x2700, 0xe7c1, 0xe681, 0x2640,
	0x2200, 0xe2c1, 0xe381, 0x2340, 0xe101, 0x21c0, 0x2080, 0xe041,
	0xa001, 0x60c0, 0x6180, 0xa141, 0x6300, 0xa3c1, 0xa281, 0x6240,
	0x6600, 0xa6c1, 0xa781, 0x6740, 0xa501, 0x65c0, 0x6480, 0xa441,
	0x6c00, 0xacc1, 0xad81, 0x6d40, 0xaf01, 0x6fc0, 0x6e80, 0xae41,
	0xaa01, 0x6ac0, 0x6b80, 0xab41, 0x6900, 0xa9c1, 0xa881, 0x6840,
	0x7800, 0xb8c1, 0xb981, 0x7940, 0xbb01, 0x7bc0, 0x7a80, 0xba41,
	0xbe01, 0x7ec0, 0x7f80, 0xbf41, 0x7d00, 0xbdc1, 0xbc81, 0x7c40,
	0xb401, 0x74c0, 0x7580, 0xb541, 0x7700, 0xb7c1, 0xb681, 0x7640,
	0x7200, 0xb2c1, 0xb381, 0x7340, 0xb101, 0x71c0, 0x7080, 0xb041,
	0x5000, 0x90c1, 0x9181, 0x5140, 0x9301, 0x53c0, 0x5280, 0x9241,
	0x9601, 0x56c0, 0x5780, 0x9741, 0x5500, 0x95c1, 0x9481, 0x5440,
	0x9c01, 0x5cc0, 0x5d80, 0x9d41, 0x5f00, 0x9fc1, 0x9e81, 0x5e40,
	0x5a00, 0x9ac1, 0x9b81, 0x5b40, 0x9901, 0x59c0, 0x5880, 0x9841,
	0x8801, 0x48c0, 0x4980, 0x8941, 0x4b00, 0x8bc1, 0x8a81, 0x4a40,
	0x4e00, 0x8ec1, 0x8f81, 0x4f40, 0x8d01, 0x4dc0, 0x4c80, 0x8c41,
	0x4400, 0x84c1, 0x8581, 0x4540, 0x8701, 0x47c0, 0x4680, 0x8641,
	0x8201, 0x42c0, 0x4380, 0x8341, 0x4100, 0x81c1, 0x8081, 0x4040
};

/*================================================================*/
/* Local Macros */

/*================================================================*/
/* Local Types */

/*================================================================*/
/* Local Static Definitions */
extern int prism2_debug;

/*================================================================*/
/* Local Function Declarations */

static void	hfa384x_int_dtim(wlandevice_t *wlandev);
static void	hfa384x_int_infdrop(wlandevice_t *wlandev);

static void     hfa384x_bap_tasklet(unsigned long data);

static void	hfa384x_int_info(wlandevice_t *wlandev);
static void	hfa384x_int_txexc(wlandevice_t *wlandev);
static void	hfa384x_int_tx(wlandevice_t *wlandev);
static void	hfa384x_int_rx(wlandevice_t *wlandev);

#ifdef CMD_IRQ
static void	hfa384x_int_cmd(wlandevice_t *wlandev);
#endif
static void	hfa384x_int_rxmonitor( wlandevice_t *wlandev,
			UINT16 rxfid, hfa384x_rx_frame_t *rxdesc);
static void	hfa384x_int_alloc(wlandevice_t *wlandev);

static int hfa384x_docmd_wait( hfa384x_t *hw, hfa384x_metacmd_t *cmd);

static int hfa384x_dl_docmd_wait( hfa384x_t *hw, hfa384x_metacmd_t *cmd);

static UINT16
hfa384x_mkcrc16(UINT8 *p, int len);

int hfa384x_copy_to_bap4(hfa384x_t *hw, UINT16 bap, UINT16 id, UINT16 offset,
			 void *buf, UINT len, void* buf2, UINT len2,
			 void *buf3, UINT len3, void* buf4, UINT len4);

/*================================================================*/
/* Function Definitions */

static UINT16
txfid_queue_empty(hfa384x_t *hw)
{
	return (hw->txfid_head == hw->txfid_tail) ? 1 : 0;
}

static UINT16
txfid_queue_remove(hfa384x_t *hw)
{
	UINT16 result= 0;

	if (txfid_queue_empty(hw)) {
		WLAN_LOG_DEBUG(3,"queue empty.\n");
	} else {
		result = hw->txfid_queue[hw->txfid_head];
		hw->txfid_head = (hw->txfid_head + 1) % hw->txfid_N;
	}

	return (UINT16)result;
}

static INT16
txfid_queue_add(hfa384x_t *hw, UINT16 val)
{
	INT16 result = 0;

	if (hw->txfid_head == ((hw->txfid_tail + 1) % hw->txfid_N)) {
		result = -1;
		WLAN_LOG_DEBUG(3,"queue full.\n");
	} else {
		hw->txfid_queue[hw->txfid_tail] = val;
		result = hw->txfid_tail;
		hw->txfid_tail = (hw->txfid_tail + 1) % hw->txfid_N;
	}

	return result;
}

/*----------------------------------------------------------------
* hfa384x_create
*
* Initializes the hfa384x_t data structure for use.  Note this
* does _not_ intialize the actual hardware, just the data structures
* we use to keep track of its state.
*
* Arguments:
*	hw		device structure
*	irq		device irq number
*	iobase		[pcmcia] i/o base address for register access
*			[pci] zero
*			[plx] i/o base address for register access
*	membase		[pcmcia] pcmcia_cs "link" pointer
*			[pci] memory base address for register access
*			[plx] memory base address for card attribute memory
*
* Returns:
*	nothing
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
void hfa384x_create(hfa384x_t *hw, UINT irq, UINT32 iobase,
		    UINT8 __iomem *membase)
{
	DBFENTER;
	memset(hw, 0, sizeof(hfa384x_t));
	hw->irq = irq;
	hw->iobase = iobase;
	hw->membase = membase;
	spin_lock_init(&(hw->cmdlock));

	/* BAP setup */
 	spin_lock_init(&(hw->baplock));
	tasklet_init(&hw->bap_tasklet,
		     hfa384x_bap_tasklet,
		     (unsigned long) hw);

	init_waitqueue_head(&hw->cmdq);
	sema_init(&hw->infofid_sem, 1);

        hw->txfid_head = 0;
        hw->txfid_tail = 0;
        hw->txfid_N = HFA384x_DRVR_FIDSTACKLEN_MAX;
        memset(hw->txfid_queue, 0, sizeof(hw->txfid_queue));

	hw->isram16 = 1;

	/* Init the auth queue head */
	skb_queue_head_init(&hw->authq);

	INIT_WORK2(&hw->link_bh, prism2sta_processing_defer);

        INIT_WORK2(&hw->commsqual_bh, prism2sta_commsqual_defer);

	init_timer(&hw->commsqual_timer);
	hw->commsqual_timer.data = (unsigned long) hw;
	hw->commsqual_timer.function = prism2sta_commsqual_timer;

	hw->link_status = HFA384x_LINK_NOTCONNECTED;
	hw->state = HFA384x_STATE_INIT;

	DBFEXIT;
}

/*----------------------------------------------------------------
* hfa384x_destroy
*
* Partner to hfa384x_create().  This function cleans up the hw
* structure so that it can be freed by the caller using a simple
* kfree.  Currently, this function is just a placeholder.  If, at some
* point in the future, an hw in the 'shutdown' state requires a 'deep'
* kfree, this is where it should be done.  Note that if this function
* is called on a _running_ hw structure, the drvr_stop() function is
* called.
*
* Arguments:
*	hw		device structure
*
* Returns:
*	nothing, this function is not allowed to fail.
*
* Side effects:
*
* Call context:
*	process
----------------------------------------------------------------*/
void
hfa384x_destroy( hfa384x_t *hw)
{
	struct sk_buff *skb;

	DBFENTER;

	if ( hw->state == HFA384x_STATE_RUNNING ) {
		hfa384x_drvr_stop(hw);
	}
	hw->state = HFA384x_STATE_PREINIT;

	if (hw->scanresults) {
		kfree(hw->scanresults);
		hw->scanresults = NULL;
	}

        /* Now to clean out the auth queue */
        while ( (skb = skb_dequeue(&hw->authq)) ) {
                dev_kfree_skb(skb);
        }

	DBFEXIT;
	return;
}

/*----------------------------------------------------------------
* hfa384x_drvr_getconfig
*
* Performs the sequence necessary to read a config/info item.
*
* Arguments:
*	hw		device structure
*	rid		config/info record id (host order)
*	buf		host side record buffer.  Upon return it will
*			contain the body portion of the record (minus the
*			RID and len).
*	len		buffer length (in bytes, should match record length)
*
* Returns:
*	0		success
*	>0		f/w reported error - f/w status code
*	<0		driver reported error
*	-ENODATA 	length mismatch between argument and retrieved
*			record.
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_drvr_getconfig(hfa384x_t *hw, UINT16 rid, void *buf, UINT16 len)
{
	int 		result = 0;
	DBFENTER;

	result = hfa384x_cmd_access( hw, 0, rid, buf, len);

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_drvr_setconfig
*
* Performs the sequence necessary to write a config/info item.
*
* Arguments:
*	hw		device structure
*	rid		config/info record id (in host order)
*	buf		host side record buffer
*	len		buffer length (in bytes)
*
* Returns:
*	0		success
*	>0		f/w reported error - f/w status code
*	<0		driver reported error
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_drvr_setconfig(hfa384x_t *hw, UINT16 rid, void *buf, UINT16 len)
{
	int		result = 0;
	DBFENTER;

	result = hfa384x_cmd_access( hw, 1, rid, buf, len);

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_drvr_readpda
*
* Performs the sequence to read the PDA space.  Note there is no
* drvr_writepda() function.  Writing a PDA is
* generally implemented by a calling component via calls to
* cmd_download and writing to the flash download buffer via the
* aux regs.
*
* Arguments:
*	hw		device structure
*	buf		buffer to store PDA in
*	len		buffer length
*
* Returns:
*	0		success
*	>0		f/w reported error - f/w status code
*	<0		driver reported error
*	-ETIMEOUT	timout waiting for the cmd regs to become
*			available, or waiting for the control reg
*			to indicate the Aux port is enabled.
*	-ENODATA	the buffer does NOT contain a valid PDA.
*			Either the card PDA is bad, or the auxdata
*			reads are giving us garbage.

*
* Side effects:
*
* Call context:
*	process thread or non-card interrupt.
----------------------------------------------------------------*/
int hfa384x_drvr_readpda(hfa384x_t *hw, void *buf, UINT len)
{
	int		result = 0;
	UINT16		*pda = buf;
	int		pdaok = 0;
	int		morepdrs = 1;
	int		currpdr = 0;	/* word offset of the current pdr */
	int		i;
	UINT16		pdrlen;		/* pdr length in bytes, host order */
	UINT16		pdrcode;	/* pdr code, host order */
	UINT16		crc;
	UINT16		pdacrc;
	struct pdaloc {
		UINT32	cardaddr;
		UINT16	auxctl;
	} pdaloc[] =
	{
		{ HFA3842_PDA_BASE,		HFA384x_AUX_CTL_NV},
		{ HFA3842_PDA_BASE,		HFA384x_AUX_CTL_EXTDS},
		{ HFA3841_PDA_BASE,		HFA384x_AUX_CTL_NV},
		{ HFA3841_PDA_BASE,		HFA384x_AUX_CTL_EXTDS},
		{ HFA3841_PDA_BOGUS_BASE,	HFA384x_AUX_CTL_NV}
	};

	DBFENTER;
	/* Check for aux available */
	result = hfa384x_cmd_aux_enable(hw, 0);
	if ( result ) {
		WLAN_LOG_DEBUG(1,"aux_enable() failed. result=%d\n", result);
		goto failed;
	}

	/* Read the pda from each known address.  */
	for ( i = 0; i < (sizeof(pdaloc)/sizeof(pdaloc[0])); i++) {
		WLAN_LOG_DEBUG( 3, "Checking PDA@(0x%08x,%s)\n",
			pdaloc[i].cardaddr,
			pdaloc[i].auxctl == HFA384x_AUX_CTL_NV ?
			"CTL_NV" : "CTL_EXTDS");

		/* Copy bufsize bytes from our current pdaloc */
		hfa384x_copy_from_aux(hw,
			pdaloc[i].cardaddr,
			pdaloc[i].auxctl,
			buf,
			len);

		/* Test for garbage */
		/* Traverse the PDR list Looking for PDA-END */
		pdaok = 1;	/* intially assume good */
		morepdrs = 1;
		currpdr = 0;
		while ( pdaok && morepdrs ) {
			pdrlen = hfa384x2host_16(pda[currpdr]) * 2;
			pdrcode = hfa384x2host_16(pda[currpdr+1]);

			/* Test for completion at END record */
			if ( pdrcode == HFA384x_PDR_END_OF_PDA ) {
				if ( pdrlen == 4 ) {
					morepdrs = 0;
					/* Calculate CRC-16 and compare to PDA
					 * value.  Note the addition of 2 words
					 * for ENDREC.len and ENDREC.code
					 * fields.
					 */
					crc = hfa384x_mkcrc16( (UINT8*)pda,
						(currpdr + 2) * sizeof(UINT16));
					pdacrc =hfa384x2host_16(pda[currpdr+2]);
					if ( crc != pdacrc ) {
						WLAN_LOG_DEBUG(3,
						"PDA crc failed:"
						"calc_crc=0x%04x,"
						"pdr_crc=0x%04x.\n",
						crc, pdacrc);
						pdaok = 0;
					}
				} else {
					WLAN_LOG_DEBUG(3,
					"END record detected w/ "
					"len(%d) != 2, assuming bad PDA\n",
					pdrlen);
					pdaok = 0;

				}
				break;
			}

			/* Test the record length */
			if ( pdrlen > HFA384x_PDR_LEN_MAX || pdrlen == 0) {
				WLAN_LOG_DEBUG(3,
					"pdrlen for address #%d "
					"at %#x:%#x:%d\n",
					i, pdaloc[i].cardaddr,
					pdaloc[i].auxctl, pdrlen);
				WLAN_LOG_DEBUG(3,"pdrlen invalid=%d\n",
					pdrlen);
				pdaok = 0;
				break;
			}

			/* Move to the next pdr */
			if ( morepdrs ) {
				/* note the access to pda[], we need words */
				currpdr += hfa384x2host_16(pda[currpdr]) + 1;
				if (currpdr*sizeof(UINT16) > len) {
					WLAN_LOG_DEBUG(3,
					"Didn't find PDA_END in buffer, "
					"trying next location.\n");
					pdaok = 0;
					break;
				}
			}
		}
		if ( pdaok ) {
			WLAN_LOG_INFO(
				"PDA Read from 0x%08x in %s space.\n",
				pdaloc[i].cardaddr,
				pdaloc[i].auxctl == 0 ? "EXTDS" :
				pdaloc[i].auxctl == 1 ? "NV" :
				pdaloc[i].auxctl == 2 ? "PHY" :
				pdaloc[i].auxctl == 3 ? "ICSRAM" :
				"<bogus auxctl>");
			break;
		}
	}
	result = pdaok ? 0 : -ENODATA;

	if ( result ) {
		WLAN_LOG_DEBUG(3,"Failure: pda is not okay\n");
	}

	hfa384x_cmd_aux_disable(hw);
failed:
	DBFEXIT;
	return result;
}



/*----------------------------------------------------------------
* mkpda_crc
*
* Calculates the CRC16 for the given PDA and inserts the value
* into the end record.
*
* Arguments:
*	pda	ptr to the PDA data structure.
*
* Returns:
*	0	- success
*	~0	- failure (probably an errno)
----------------------------------------------------------------*/
static UINT16
hfa384x_mkcrc16(UINT8 *p, int len)
{
	UINT16	crc = 0;
	UINT8	*lim = p + len;

	while (p < lim) {
		crc = (crc >> 8 ) ^ crc16tab[(crc & 0xff) ^ *p++];
	}

	return crc;
}


/*----------------------------------------------------------------
* hfa384x_drvr_ramdl_enable
*
* Begins the ram download state.  Checks to see that we're not
* already in a download state and that a port isn't enabled.
* Sets the download state and calls cmd_download with the
* ENABLE_VOLATILE subcommand and the exeaddr argument.
*
* Arguments:
*	hw		device structure
*	exeaddr		the card execution address that will be
*                       jumped to when ramdl_disable() is called
*			(host order).
*
* Returns:
*	0		success
*	>0		f/w reported error - f/w status code
*	<0		driver reported error
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_drvr_ramdl_enable(hfa384x_t *hw, UINT32 exeaddr)
{
	int		result = 0;
	UINT16		lowaddr;
	UINT16		hiaddr;
	int		i;
	DBFENTER;
	/* Check that a port isn't active */
	for ( i = 0; i < HFA384x_PORTID_MAX; i++) {
		if ( hw->port_enabled[i] ) {
			WLAN_LOG_DEBUG(1,"Can't download with a port enabled.\n");
			result = -EINVAL;
			goto done;
		}
	}

	/* Check that we're not already in a download state */
	if ( hw->dlstate != HFA384x_DLSTATE_DISABLED ) {
		WLAN_LOG_DEBUG(1,"Download state not disabled.\n");
		result = -EINVAL;
		goto done;
	}

	/* Are we supposed to go into genesis mode? */
	if (exeaddr == 0x3f0000) {
		UINT16 initseq[2] = { 0xe100, 0xffa1 };
		UINT16 readbuf[2];
		UINT8 hcr = 0x0f; /* Default to x16 SRAM */
		hw->isram16 = 1;

		WLAN_LOG_DEBUG(1, "Dropping into Genesis mode\n");

		/* Issue card reset and enable aux port */
		hfa384x_corereset(hw, prism2_reset_holdtime,
				  prism2_reset_settletime, 0);
		hfa384x_cmd_aux_enable(hw, 1);

		/* Genesis set */
		hfa384x_copy_to_aux(hw, 0x7E0038, HFA384x_AUX_CTL_EXTDS,
				    initseq, sizeof(initseq));

		hfa384x_corereset(hw, prism2_reset_holdtime,
				  prism2_reset_settletime, hcr);

		/* Validate memory config */
		hfa384x_copy_to_aux(hw, 0x7E0038, HFA384x_AUX_CTL_EXTDS,
				    initseq, sizeof(initseq));
		hfa384x_copy_from_aux(hw, 0x7E0038, HFA384x_AUX_CTL_EXTDS,
				    readbuf, sizeof(initseq));
		WLAN_HEX_DUMP(3, "readback", readbuf, sizeof(readbuf));

		if (memcmp(initseq, readbuf, sizeof(readbuf))) {
			hcr = 0x1f; /* x8 SRAM */
			hw->isram16 = 0;

			hfa384x_copy_to_aux(hw, 0x7E0038, HFA384x_AUX_CTL_EXTDS,
					    initseq, sizeof(initseq));
			hfa384x_corereset(hw, prism2_reset_holdtime,
					  prism2_reset_settletime, hcr);

			hfa384x_copy_to_aux(hw, 0x7E0038, HFA384x_AUX_CTL_EXTDS,
					    initseq, sizeof(initseq));
			hfa384x_copy_from_aux(hw, 0x7E0038, HFA384x_AUX_CTL_EXTDS,
					    readbuf, sizeof(initseq));
			WLAN_HEX_DUMP(2, "readback", readbuf, sizeof(readbuf));

			if (memcmp(initseq, readbuf, sizeof(readbuf))) {
				WLAN_LOG_ERROR("Genesis mode failed\n");
				result = -1;
				goto done;
			}
		}

		/* Now we're in genesis mode */
		hw->dlstate = HFA384x_DLSTATE_GENESIS;
		goto done;
	}

	/* Retrieve the buffer loc&size and timeout */
	if ( (result = hfa384x_drvr_getconfig(hw, HFA384x_RID_DOWNLOADBUFFER,
				&(hw->bufinfo), sizeof(hw->bufinfo))) ) {
		goto done;
	}
	hw->bufinfo.page = hfa384x2host_16(hw->bufinfo.page);
	hw->bufinfo.offset = hfa384x2host_16(hw->bufinfo.offset);
	hw->bufinfo.len = hfa384x2host_16(hw->bufinfo.len);
	if ( (result = hfa384x_drvr_getconfig16(hw, HFA384x_RID_MAXLOADTIME,
				&(hw->dltimeout))) ) {
		goto done;
	}
	hw->dltimeout = hfa384x2host_16(hw->dltimeout);

	/* Enable the aux port */
	if ( (result = hfa384x_cmd_aux_enable(hw, 0)) ) {
		WLAN_LOG_DEBUG(1,"Aux enable failed, result=%d.\n", result);
		goto done;
	}

	/* Call the download(1,addr) function */
	lowaddr = HFA384x_ADDR_CMD_MKOFF(exeaddr);
	hiaddr =  HFA384x_ADDR_CMD_MKPAGE(exeaddr);

	result = hfa384x_cmd_download(hw, HFA384x_PROGMODE_RAM,
			lowaddr, hiaddr, 0);
	if ( result == 0) {
		/* Set the download state */
		hw->dlstate = HFA384x_DLSTATE_RAMENABLED;
	} else {
		WLAN_LOG_DEBUG(1,"cmd_download(0x%04x, 0x%04x) failed, result=%d.\n",
				lowaddr,hiaddr, result);
		/* Disable  the aux port */
		hfa384x_cmd_aux_disable(hw);
	}

 done:
	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_drvr_ramdl_disable
*
* Ends the ram download state.
*
* Arguments:
*	hw		device structure
*
* Returns:
*	0		success
*	>0		f/w reported error - f/w status code
*	<0		driver reported error
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_drvr_ramdl_disable(hfa384x_t *hw)
{
	DBFENTER;
	/* Check that we're already in the download state */
	if ( ( hw->dlstate != HFA384x_DLSTATE_RAMENABLED ) &&
	     ( hw->dlstate != HFA384x_DLSTATE_GENESIS ) ) {
		return -EINVAL;
	}

	if (hw->dlstate == HFA384x_DLSTATE_GENESIS) {
		hfa384x_corereset(hw, prism2_reset_holdtime,
				  prism2_reset_settletime,
				  hw->isram16 ? 0x07: 0x17);
		goto done;
	}

	/* Disable the aux port */
	hfa384x_cmd_download(hw, HFA384x_PROGMODE_DISABLE, 0, 0 , 0);

 done:
	hw->dlstate = HFA384x_DLSTATE_DISABLED;
	hfa384x_cmd_aux_disable(hw);

	DBFEXIT;
	return 0;
}


/*----------------------------------------------------------------
* hfa384x_drvr_ramdl_write
*
* Performs a RAM download of a chunk of data. First checks to see
* that we're in the RAM download state, then uses the aux functions
* to 1) copy the data, 2) readback and compare.  The download
* state is unaffected.  When all data has been written using
* this function, call drvr_ramdl_disable() to end the download state
* and restart the MAC.
*
* Arguments:
*	hw		device structure
*	daddr		Card address to write to. (host order)
*	buf		Ptr to data to write.
*	len		Length of data (host order).
*
* Returns:
*	0		success
*	>0		f/w reported error - f/w status code
*	<0		driver reported error
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_drvr_ramdl_write(hfa384x_t *hw, UINT32 daddr, void* buf, UINT32 len)
{
	int		result = 0;
	UINT8		*verbuf;
	DBFENTER;
	/* Check that we're in the ram download state */
	if ( ( hw->dlstate != HFA384x_DLSTATE_RAMENABLED ) &&
	     ( hw->dlstate != HFA384x_DLSTATE_GENESIS ) ) {
		return -EINVAL;
	}

	WLAN_LOG_INFO("Writing %d bytes to ram @0x%06x\n", len, daddr);
#if 0
WLAN_HEX_DUMP(1, "dldata", buf, len);
#endif
	/* Copy the data via the aux port */
	hfa384x_copy_to_aux(hw, daddr, HFA384x_AUX_CTL_EXTDS, buf, len);

	/* Create a buffer for the verify */
	verbuf = kmalloc(len, GFP_KERNEL);
	if (verbuf == NULL ) return 1;

	/* Read back and compare */
	hfa384x_copy_from_aux(hw, daddr, HFA384x_AUX_CTL_EXTDS, verbuf, len);

	if ( memcmp(buf, verbuf, len) ) {
		WLAN_LOG_DEBUG(1,"ramdl verify failed!\n");
		result = -EINVAL;
	}

	kfree_s(verbuf, len);
	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_drvr_flashdl_enable
*
* Begins the flash download state.  Checks to see that we're not
* already in a download state and that a port isn't enabled.
* Sets the download state and retrieves the flash download
* buffer location, buffer size, and timeout length.
*
* Arguments:
*	hw		device structure
*
* Returns:
*	0		success
*	>0		f/w reported error - f/w status code
*	<0		driver reported error
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_drvr_flashdl_enable(hfa384x_t *hw)
{
	int		result = 0;
	int		i;

	DBFENTER;
	/* Check that a port isn't active */
	for ( i = 0; i < HFA384x_PORTID_MAX; i++) {
		if ( hw->port_enabled[i] ) {
			WLAN_LOG_DEBUG(1,"called when port enabled.\n");
			return -EINVAL;
		}
	}

	/* Check that we're not already in a download state */
	if ( hw->dlstate != HFA384x_DLSTATE_DISABLED ) {
		return -EINVAL;
	}

	/* Retrieve the buffer loc&size and timeout */
	if ( (result = hfa384x_drvr_getconfig(hw, HFA384x_RID_DOWNLOADBUFFER,
				&(hw->bufinfo), sizeof(hw->bufinfo))) ) {
		return result;
	}
	hw->bufinfo.page = hfa384x2host_16(hw->bufinfo.page);
	hw->bufinfo.offset = hfa384x2host_16(hw->bufinfo.offset);
	hw->bufinfo.len = hfa384x2host_16(hw->bufinfo.len);
	if ( (result = hfa384x_drvr_getconfig16(hw, HFA384x_RID_MAXLOADTIME,
				&(hw->dltimeout))) ) {
		return result;
	}
	hw->dltimeout = hfa384x2host_16(hw->dltimeout);

	/* Enable the aux port */
	if ( (result = hfa384x_cmd_aux_enable(hw, 0)) ) {
		return result;
	}

	hw->dlstate = HFA384x_DLSTATE_FLASHENABLED;
	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_drvr_flashdl_disable
*
* Ends the flash download state.  Note that this will cause the MAC
* firmware to restart.
*
* Arguments:
*	hw		device structure
*
* Returns:
*	0		success
*	>0		f/w reported error - f/w status code
*	<0		driver reported error
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_drvr_flashdl_disable(hfa384x_t *hw)
{
	DBFENTER;
	/* Check that we're already in the download state */
	if ( hw->dlstate != HFA384x_DLSTATE_FLASHENABLED ) {
		return -EINVAL;
	}

	/* There isn't much we can do at this point, so I don't */
	/*  bother  w/ the return value */
	hfa384x_cmd_download(hw, HFA384x_PROGMODE_DISABLE, 0, 0 , 0);
	hw->dlstate = HFA384x_DLSTATE_DISABLED;

	/* Disable the aux port */
	hfa384x_cmd_aux_disable(hw);

	DBFEXIT;
	return 0;
}


/*----------------------------------------------------------------
* hfa384x_drvr_flashdl_write
*
* Performs a FLASH download of a chunk of data. First checks to see
* that we're in the FLASH download state, then sets the download
* mode, uses the aux functions to 1) copy the data to the flash
* buffer, 2) sets the download 'write flash' mode, 3) readback and
* compare.  Lather rinse, repeat as many times an necessary to get
* all the given data into flash.
* When all data has been written using this function (possibly
* repeatedly), call drvr_flashdl_disable() to end the download state
* and restart the MAC.
*
* Arguments:
*	hw		device structure
*	daddr		Card address to write to. (host order)
*	buf		Ptr to data to write.
*	len		Length of data (host order).
*
* Returns:
*	0		success
*	>0		f/w reported error - f/w status code
*	<0		driver reported error
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_drvr_flashdl_write(hfa384x_t *hw, UINT32 daddr, void* buf, UINT32 len)
{
	int		result = 0;
	UINT8		*verbuf;
	UINT32		dlbufaddr;
	UINT32		currlen;
	UINT32		currdaddr;
	UINT16		destlo;
	UINT16		desthi;
	int		nwrites;
	int		i;

	DBFENTER;
	/* Check that we're in the flash download state */
	if ( hw->dlstate != HFA384x_DLSTATE_FLASHENABLED ) {
		return -EINVAL;
	}

	WLAN_LOG_INFO("Download %d bytes to flash @0x%06x\n", len, daddr);

	/* Need a flat address for arithmetic */
	dlbufaddr = HFA384x_ADDR_AUX_MKFLAT(
			hw->bufinfo.page,
			hw->bufinfo.offset);
	verbuf = kmalloc(hw->bufinfo.len, GFP_KERNEL);

#if 0
WLAN_LOG_WARNING("dlbuf@0x%06lx len=%d to=%d\n", dlbufaddr, hw->bufinfo.len, hw->dltimeout);
#endif
	/* Figure out how many times to to the flash prog */
	nwrites = len / hw->bufinfo.len;
	nwrites += (len % hw->bufinfo.len) ? 1 : 0;

	if ( verbuf == NULL ) {
		WLAN_LOG_ERROR("Failed to allocate flash verify buffer\n");
		return 1;
	}
	/* For each */
	for ( i = 0; i < nwrites; i++) {
		/* Get the dest address and len */
		currlen = (len - (hw->bufinfo.len * i)) > hw->bufinfo.len ?
				hw->bufinfo.len :
				(len - (hw->bufinfo.len * i));
		currdaddr = daddr + (hw->bufinfo.len * i);
		destlo = HFA384x_ADDR_CMD_MKOFF(currdaddr);
		desthi = HFA384x_ADDR_CMD_MKPAGE(currdaddr);
		WLAN_LOG_INFO("Writing %d bytes to flash @0x%06x\n", currlen, currdaddr);
#if 0
WLAN_HEX_DUMP(1, "dldata", buf+(hw->bufinfo.len*i), currlen);
#endif
		/* Set the download mode */
		result = hfa384x_cmd_download(hw, HFA384x_PROGMODE_NV,
				destlo, desthi, currlen);
		if ( result ) {
			WLAN_LOG_ERROR("download(NV,lo=%x,hi=%x,len=%x) "
				"cmd failed, result=%d. Aborting d/l\n",
				destlo, desthi, currlen, result);
			goto exit_proc;
		}
		/* copy the data to the flash buffer */
		hfa384x_copy_to_aux(hw, dlbufaddr, HFA384x_AUX_CTL_EXTDS,
					buf+(hw->bufinfo.len*i), currlen);
		/* set the download 'write flash' mode */
		result = hfa384x_cmd_download(hw, HFA384x_PROGMODE_NVWRITE, 0,0,0);
		if ( result ) {
			WLAN_LOG_ERROR(
				"download(NVWRITE,lo=%x,hi=%x,len=%x) "
				"cmd failed, result=%d. Aborting d/l\n",
				destlo, desthi, currlen, result);
			goto exit_proc;
		}
		/* readback and compare, if fail...bail */
		hfa384x_copy_from_aux(hw,
				currdaddr, HFA384x_AUX_CTL_NV,
				verbuf, currlen);

		if ( memcmp(buf+(hw->bufinfo.len*i), verbuf, currlen) ) {
			return -EINVAL;
		}
	}

exit_proc:
         /* DOH! This kfree's for you Mark :-) My forehead hurts... */
         kfree(verbuf);

	/* Leave the firmware in the 'post-prog' mode.  flashdl_disable will */
	/*  actually disable programming mode.  Remember, that will cause the */
	/*  the firmware to effectively reset itself. */

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_cmd_initialize
*
* Issues the initialize command and sets the hw->state based
* on the result.
*
* Arguments:
*	hw		device structure
*
* Returns:
*	0		success
*	>0		f/w reported error - f/w status code
*	<0		driver reported error
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_cmd_initialize(hfa384x_t *hw)
{
	int result = 0;
	int i;
	hfa384x_metacmd_t cmd;

	DBFENTER;

	/* we don't want to be interrupted during the reset */
	hfa384x_setreg(hw, 0, HFA384x_INTEN);
	hfa384x_setreg(hw, 0xffff, HFA384x_EVACK);

	cmd.cmd = HFA384x_CMDCODE_INIT;
	cmd.parm0 = 0;
	cmd.parm1 = 0;
	cmd.parm2 = 0;

	spin_lock_bh(&hw->cmdlock);
	result = hfa384x_docmd_wait(hw, &cmd);
	spin_unlock_bh(&hw->cmdlock);

	if ( result == 0 ) {
		for ( i = 0; i < HFA384x_NUMPORTS_MAX; i++) {
			hw->port_enabled[i] = 0;
		}
	}

        hw->link_status = HFA384x_LINK_NOTCONNECTED;

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_drvr_commtallies
*
* Send a commtallies inquiry to the MAC.  Note that this is an async
* call that will result in an info frame arriving sometime later.
*
* Arguments:
*	hw		device structure
*
* Returns:
*	zero		success.
*
* Side effects:
*
* Call context:
*	process
----------------------------------------------------------------*/
int hfa384x_drvr_commtallies( hfa384x_t *hw )
{
	hfa384x_metacmd_t cmd;
	int result;

	DBFENTER;

	cmd.cmd = HFA384x_CMDCODE_INQ;
	cmd.parm0 = HFA384x_IT_COMMTALLIES;
	cmd.parm1 = 0;
	cmd.parm2 = 0;

	spin_lock_bh(&hw->cmdlock);
	result = hfa384x_docmd_wait(hw, &cmd);
	spin_unlock_bh(&hw->cmdlock);

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_drvr_enable
*
* Issues the enable command to enable communications on one of
* the MACs 'ports'.  Only macport 0 is valid  for stations.
* APs may also enable macports 1-6.  Only ports that are currently
* disabled may be enabled.
*
* Arguments:
*	hw		device structure
*	macport		MAC port number
*
* Returns:
*	0		success
*	>0		f/w reported failure - f/w status code
*	<0		driver reported error (timeout|bad arg)
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_drvr_enable(hfa384x_t *hw, UINT16 macport)
{
	int	result = 0;

	DBFENTER;
	if ((!hw->isap && macport != 0) ||
	    (hw->isap && !(macport <= HFA384x_PORTID_MAX)) ||
	    (hw->port_enabled[macport]) ){
		result = -EINVAL;
	} else {
		result = hfa384x_cmd_enable(hw, macport);
		if ( result == 0 ) {
			hw->port_enabled[macport] = 1;
		}
	}
	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_cmd_enable
*
* Issues the the enable command to enable communications on one of the
* MACs 'ports'.
*
* Arguments:
*	hw		device structure
*	macport		MAC port number
*
* Returns:
*	0		success
*	>0		f/w reported failure - f/w status code
*	<0		driver reported error (timeout|bad arg)
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_cmd_enable(hfa384x_t *hw, UINT16 macport)
{
	int	result = 0;
	hfa384x_metacmd_t cmd;

	DBFENTER;

	cmd.cmd = HFA384x_CMD_CMDCODE_SET(HFA384x_CMDCODE_ENABLE) |
		  HFA384x_CMD_MACPORT_SET(macport);
	cmd.parm0 = 0;
	cmd.parm1 = 0;
	cmd.parm2 = 0;

	spin_lock_bh(&hw->cmdlock);
	result = hfa384x_docmd_wait(hw, &cmd);
	spin_unlock_bh(&hw->cmdlock);

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_drvr_disable
*
* Issues the disable command to stop communications on one of
* the MACs 'ports'.  Only macport 0 is valid  for stations.
* APs may also disable macports 1-6.  Only ports that have been
* previously enabled may be disabled.
*
* Arguments:
*	hw		device structure
*	macport		MAC port number (host order)
*
* Returns:
*	0		success
*	>0		f/w reported failure - f/w status code
*	<0		driver reported error (timeout|bad arg)
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_drvr_disable(hfa384x_t *hw, UINT16 macport)
{
	int	result = 0;

	DBFENTER;
	if ((!hw->isap && macport != 0) ||
	    (hw->isap && !(macport <= HFA384x_PORTID_MAX)) ||
	    !(hw->port_enabled[macport]) ){
		result = -EINVAL;
	} else {
		result = hfa384x_cmd_disable(hw, macport);
		if ( result == 0 ) {
			hw->port_enabled[macport] = 0;
		}
	}
	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_cmd_disable
*
* Issues the command to disable a port.
*
* Arguments:
*	hw		device structure
*	macport		MAC port number (host order)
*
* Returns:
*	0		success
*	>0		f/w reported failure - f/w status code
*	<0		driver reported error (timeout|bad arg)
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_cmd_disable(hfa384x_t *hw, UINT16 macport)
{
	int	result = 0;
	hfa384x_metacmd_t cmd;

	DBFENTER;

	cmd.cmd = HFA384x_CMD_CMDCODE_SET(HFA384x_CMDCODE_DISABLE) |
		  HFA384x_CMD_MACPORT_SET(macport);
	cmd.parm0 = 0;
	cmd.parm1 = 0;
	cmd.parm2 = 0;

	spin_lock_bh(&hw->cmdlock);
	result = hfa384x_docmd_wait(hw, &cmd);
	spin_unlock_bh(&hw->cmdlock);

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_cmd_diagnose
*
* Issues the diagnose command to test the: register interface,
* MAC controller (including loopback), External RAM, Non-volatile
* memory integrity, and synthesizers.  Following execution of this
* command, MAC/firmware are in the 'initial state'.  Therefore,
* the Initialize command should be issued after successful
* completion of this command.  This function may only be called
* when the MAC is in the 'communication disabled' state.
*
* Arguments:
*	hw		device structure
*
* Returns:
*	0		success
*	>0		f/w reported failure - f/w status code
*	<0		driver reported error (timeout|bad arg)
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
#define DIAG_PATTERNA ((UINT16)0xaaaa)
#define DIAG_PATTERNB ((UINT16)0x5555)

int hfa384x_cmd_diagnose(hfa384x_t *hw)
{
	int	result = 0;
	hfa384x_metacmd_t cmd;

	DBFENTER;

	cmd.cmd = HFA384x_CMD_CMDCODE_SET(HFA384x_CMDCODE_DIAG);
	cmd.parm0 = DIAG_PATTERNA;
	cmd.parm1 = DIAG_PATTERNB;
	cmd.parm2 = 0;

	spin_lock_bh(&hw->cmdlock);
	result = hfa384x_docmd_wait(hw, &cmd);
	spin_unlock_bh(&hw->cmdlock);

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_cmd_allocate
*
* Issues the allocate command instructing the firmware to allocate
* a 'frame structure buffer' in MAC controller RAM.  This command
* does not provide the result, it only initiates one of the f/w's
* asynchronous processes to construct the buffer.  When the
* allocation is complete, it will be indicated via the Alloc
* bit in the EvStat register and the FID identifying the allocated
* space will be available from the AllocFID register.  Some care
* should be taken when waiting for the Alloc event.  If a Tx or
* Notify command w/ Reclaim has been previously executed, it's
* possible the first Alloc event after execution of this command
* will be for the reclaimed buffer and not the one you asked for.
* This case must be handled in the Alloc event handler.
*
* Arguments:
*	hw		device structure
*	len		allocation length, must be an even value
*			in the range [4-2400]. (host order)
*
* Returns:
*	0		success
*	>0		f/w reported failure - f/w status code
*	<0		driver reported error (timeout|bad arg)
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_cmd_allocate(hfa384x_t *hw, UINT16 len)
{
	int	result = 0;
	hfa384x_metacmd_t cmd;

	DBFENTER;

	if ( (len % 2) ||
	     len < HFA384x_CMD_ALLOC_LEN_MIN ||
	     len > HFA384x_CMD_ALLOC_LEN_MAX ) {
		result = -EINVAL;
	} else {
		cmd.cmd = HFA384x_CMD_CMDCODE_SET(HFA384x_CMDCODE_ALLOC);
		cmd.parm0 = len;
		cmd.parm1 = 0;
		cmd.parm2 = 0;

		spin_lock_bh(&hw->cmdlock);
		result = hfa384x_docmd_wait(hw, &cmd);
		spin_unlock_bh(&hw->cmdlock);
	}
	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_cmd_transmit
*
* Instructs the firmware to transmit a frame previously copied
* to a given buffer.  This function returns immediately, the Tx
* results are available via the Tx or TxExc events (if the frame
* control bits are set).  The reclaim argument specifies if the
* FID passed will be used by the f/w tx process or returned for
* use w/ another transmit command.  If reclaim is set, expect an
* Alloc event signalling the availibility of the FID for reuse.
*
* NOTE: hw->cmdlock MUST BE HELD before calling this function!
*
* Arguments:
*	hw		device structure
*	reclaim		[0|1] indicates whether the given FID will
*			be handed back (via Alloc event) for reuse.
*			(host order)
*	qos		[0-3] Value to put in the QoS field of the
*			tx command, identifies a queue to place the
*			outgoing frame in.
*			(host order)
*	fid		FID of buffer containing the frame that was
*			previously copied to MAC memory via the bap.
*			(host order)
*
* Returns:
*	0		success
*	>0		f/w reported failure - f/w status code
*	<0		driver reported error (timeout|bad arg)
*
* Side effects:
*	hw->resp0 will contain the FID being used by async tx
*	process.  If reclaim==0, resp0 will be the same as the fid
*	argument.  If reclaim==1, resp0 will be the different and
*	is the value to watch for in the Tx|TxExc to indicate completion
*	of the frame passed in fid.
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_cmd_transmit(hfa384x_t *hw, UINT16 reclaim, UINT16 qos, UINT16 fid)
{
	int	result = 0;
	hfa384x_metacmd_t cmd;

	DBFENTER;
	cmd.cmd = HFA384x_CMD_CMDCODE_SET(HFA384x_CMDCODE_TX) |
		HFA384x_CMD_RECL_SET(reclaim) |
		HFA384x_CMD_QOS_SET(qos);
	cmd.parm0 = fid;
	cmd.parm1 = 0;
	cmd.parm2 = 0;

	result = hfa384x_docmd_wait(hw, &cmd);

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_cmd_clearpersist
*
* Instructs the firmware to clear the persistence bit in a given
* FID.  This has the effect of telling the firmware to drop the
* persistent frame.  The FID must be one that was previously used
* to transmit a PRST frame.
*
* Arguments:
*	hw		device structure
*	fid		FID of the persistent frame (host order)
*
* Returns:
*	0		success
*	>0		f/w reported failure - f/w status code
*	<0		driver reported error (timeout|bad arg)
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_cmd_clearpersist(hfa384x_t *hw, UINT16 fid)
{
	int	result = 0;
	hfa384x_metacmd_t cmd;

	DBFENTER;

	cmd.cmd = HFA384x_CMD_CMDCODE_SET(HFA384x_CMDCODE_CLRPRST);
	cmd.parm0 = fid;
	cmd.parm1 = 0;
	cmd.parm2 = 0;

	spin_lock_bh(&hw->cmdlock);
	result = hfa384x_docmd_wait(hw, &cmd);
	spin_unlock_bh(&hw->cmdlock);

	DBFEXIT;
	return result;
}

/*----------------------------------------------------------------
* hfa384x_cmd_notify
*
* Sends an info frame to the firmware to alter the behavior
* of the f/w asynch processes.  Can only be called when the MAC
* is in the enabled state.
*
* Arguments:
*	hw		device structure
*	reclaim		[0|1] indicates whether the given FID will
*			be handed back (via Alloc event) for reuse.
*			(host order)
*	fid		FID of buffer containing the frame that was
*			previously copied to MAC memory via the bap.
*			(host order)
*
* Returns:
*	0		success
*	>0		f/w reported failure - f/w status code
*	<0		driver reported error (timeout|bad arg)
*
* Side effects:
*	hw->resp0 will contain the FID being used by async notify
*	process.  If reclaim==0, resp0 will be the same as the fid
*	argument.  If reclaim==1, resp0 will be the different.
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_cmd_notify(hfa384x_t *hw, UINT16 reclaim, UINT16 fid,
		       void *buf, UINT16 len)
{
	int	result = 0;
	hfa384x_metacmd_t cmd;

	DBFENTER;
	cmd.cmd = HFA384x_CMD_CMDCODE_SET(HFA384x_CMDCODE_NOTIFY) |
		HFA384x_CMD_RECL_SET(reclaim);
	cmd.parm0 = fid;
	cmd.parm1 = 0;
	cmd.parm2 = 0;

	spin_lock_bh(&hw->cmdlock);

        /* Copy the record to FID */
        result = hfa384x_copy_to_bap(hw, HFA384x_BAP_PROC, hw->infofid, 0, buf, len);
        if ( result ) {
                WLAN_LOG_DEBUG(1,
			"copy_to_bap(%04x, 0, %d) failed, result=0x%x\n",
                        hw->infofid, len, result);
		result = -EIO;
                goto failed;
        }

	result = hfa384x_docmd_wait(hw, &cmd);

 failed:
	spin_unlock_bh(&hw->cmdlock);

	DBFEXIT;
	return result;
}


#if 0
/*----------------------------------------------------------------
* hfa384x_cmd_inquiry
*
* Requests an info frame from the firmware.  The info frame will
* be delivered asynchronously via the Info event.
*
* Arguments:
*	hw		device structure
*	fid		FID of the info frame requested. (host order)
*
* Returns:
*	0		success
*	>0		f/w reported failure - f/w status code
*	<0		driver reported error (timeout|bad arg)
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
static int hfa384x_cmd_inquiry(hfa384x_t *hw, UINT16 fid)
{
	int	result = 0;
	hfa384x_metacmd_t cmd;

	DBFENTER;

	cmd.cmd = HFA384x_CMD_CMDCODE_SET(HFA384x_CMDCODE_INQ);
	cmd.parm0 = fid;
	cmd.parm1 = 0;
	cmd.parm2 = 0;

	spin_lock_bh(&hw->cmdlock);
	result = hfa384x_docmd_wait(hw, &cmd);
	spin_unlock_bh(&hw->cmdlock);

	DBFEXIT;
	return result;
}
#endif


/*----------------------------------------------------------------
* hfa384x_cmd_access
*
* Requests that a given record be copied to/from the record
* buffer.  If we're writing from the record buffer, the contents
* must previously have been written to the record buffer via the
* bap.  If we're reading into the record buffer, the record can
* be read out of the record buffer after this call.
*
* Arguments:
*	hw		device structure
*	write		[0|1] copy the record buffer to the given
*			configuration record. (host order)
*	rid		RID of the record to read/write. (host order)
*	buf		host side record buffer.  Upon return it will
*			contain the body portion of the record (minus the
*			RID and len).
*	len		buffer length (in bytes, should match record length)
*
* Returns:
*	0		success
*	>0		f/w reported failure - f/w status code
*	<0		driver reported error (timeout|bad arg)
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_cmd_access(hfa384x_t *hw, UINT16 write, UINT16 rid,
		       void* buf, UINT16 len)
{
	int	result = 0;
	hfa384x_metacmd_t cmd;
	hfa384x_rec_t	rec;

	DBFENTER;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0))
	/* This should NOT be called in interrupt context! */
	if (in_irq()) {
		WLAN_LOG_ERROR("Krap, in Interrupt context!");
#ifdef WLAN_INCLUDE_DEBUG
		BUG();
#endif
	}
#endif
	spin_lock_bh(&hw->cmdlock);

	if (write) {
		rec.rid = host2hfa384x_16(rid);
		rec.reclen = host2hfa384x_16((len/2) + 1); /* note conversion to words, +1 for rid field */
		/* write the record */
		result = hfa384x_copy_to_bap4( hw, HFA384x_BAP_PROC, rid, 0,
					       &rec, sizeof(rec),
					       buf, len,
					       NULL, 0, NULL, 0);
		if ( result ) {
			WLAN_LOG_DEBUG(3,"Failure writing record header+data\n");
			goto fail;
		}

	}

	cmd.cmd = HFA384x_CMD_CMDCODE_SET(HFA384x_CMDCODE_ACCESS) |
		HFA384x_CMD_WRITE_SET(write);
	cmd.parm0 = rid;
	cmd.parm1 = 0;
	cmd.parm2 = 0;

	result = hfa384x_docmd_wait(hw, &cmd);
	if ( result ) {
		WLAN_LOG_ERROR("Call to hfa384x_docmd_wait failed (%d %d)\n",
				result, cmd.result.resp0);
		goto fail;
	}

	if (!write) {
		result = hfa384x_copy_from_bap( hw, HFA384x_BAP_PROC, rid, 0, &rec, sizeof(rec));
		if ( result ) {
			WLAN_LOG_DEBUG(3,"Call to hfa384x_copy_from_bap failed\n");
			goto fail;
		}

		/* Validate the record length */
		if ( ((hfa384x2host_16(rec.reclen)-1)*2) != len ) {  /* note body len calculation in bytes */
			WLAN_LOG_DEBUG(1, "RID len mismatch, rid=0x%04x hlen=%d fwlen=%d\n",
					rid, len, (hfa384x2host_16(rec.reclen)-1)*2);
			result = -ENODATA;
			goto fail;
		}

		result = hfa384x_copy_from_bap( hw, HFA384x_BAP_PROC, rid, sizeof(rec), buf, len);

	}

 fail:
	spin_unlock_bh(&hw->cmdlock);
	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_cmd_monitor
*
* Enables the 'monitor mode' of the MAC.  Here's the description of
* monitor mode that I've received thus far:
*
*  "The "monitor mode" of operation is that the MAC passes all
*  frames for which the PLCP checks are correct. All received
*  MPDUs are passed to the host with MAC Port = 7, with a
*  receive status of good, FCS error, or undecryptable. Passing
*  certain MPDUs is a violation of the 802.11 standard, but useful
*  for a debugging tool."  Normal communication is not possible
*  while monitor mode is enabled.
*
* Arguments:
*	hw		device structure
*	enable		a code (0x0b|0x0f) that enables/disables
*			monitor mode. (host order)
*
* Returns:
*	0		success
*	>0		f/w reported failure - f/w status code
*	<0		driver reported error (timeout|bad arg)
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_cmd_monitor(hfa384x_t *hw, UINT16 enable)
{
	int	result = 0;
	hfa384x_metacmd_t cmd;

	DBFENTER;

	cmd.cmd = HFA384x_CMD_CMDCODE_SET(HFA384x_CMDCODE_MONITOR) |
		HFA384x_CMD_AINFO_SET(enable);
	cmd.parm0 = 0;
	cmd.parm1 = 0;
	cmd.parm2 = 0;

	spin_lock_bh(&hw->cmdlock);
	result = hfa384x_docmd_wait(hw, &cmd);
	spin_unlock_bh(&hw->cmdlock);

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_cmd_download
*
* Sets the controls for the MAC controller code/data download
* process.  The arguments set the mode and address associated
* with a download.  Note that the aux registers should be enabled
* prior to setting one of the download enable modes.
*
* Arguments:
*	hw		device structure
*	mode		0 - Disable programming and begin code exec
*			1 - Enable volatile mem programming
*			2 - Enable non-volatile mem programming
*			3 - Program non-volatile section from NV download
*			    buffer.
*			(host order)
*	lowaddr
*	highaddr	For mode 1, sets the high & low order bits of
*			the "destination address".  This address will be
*			the execution start address when download is
*			subsequently disabled.
*			For mode 2, sets the high & low order bits of
*			the destination in NV ram.
*			For modes 0 & 3, should be zero. (host order)
*			NOTE: these address args are in CMD format
*	codelen		Length of the data to write in mode 2,
*			zero otherwise. (host order)
*
* Returns:
*	0		success
*	>0		f/w reported failure - f/w status code
*	<0		driver reported error (timeout|bad arg)
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_cmd_download(hfa384x_t *hw, UINT16 mode, UINT16 lowaddr,
				UINT16 highaddr, UINT16 codelen)
{
	int	result = 0;
	hfa384x_metacmd_t cmd;

	DBFENTER;

	cmd.cmd = HFA384x_CMD_CMDCODE_SET(HFA384x_CMDCODE_DOWNLD) |
		HFA384x_CMD_PROGMODE_SET(mode);
	cmd.parm0 = lowaddr;
	cmd.parm1 = highaddr;
	cmd.parm2 = codelen;

	spin_lock_bh(&hw->cmdlock);
	result = hfa384x_dl_docmd_wait(hw, &cmd);
	spin_unlock_bh(&hw->cmdlock);

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_cmd_aux_enable
*
* Goes through the process of enabling the auxilary port.  This
* is necessary prior to raw reads/writes to card data space.
* Direct access to the card data space is only used for downloading
* code and debugging.
* Note that a call to this function is required before attempting
* a download.
*
* Arguments:
*	hw		device structure
*
* Returns:
*	0		success
*	>0		f/w reported failure - f/w status code
*	<0		driver reported error (timeout)
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_cmd_aux_enable(hfa384x_t *hw, int force)
{
	int		result = -ETIMEDOUT;
	unsigned long	flags;
	UINT32		retries_remaining;
	UINT16		reg;
	UINT		auxen_mirror = hw->auxen;

	DBFENTER;

	/* Check for existing enable */
	if ( hw->auxen ) {
		hw->auxen++;
		return 0;
	}

	/* acquire the lock */
	spin_lock_irqsave( &(hw->cmdlock), flags);
	/* wait for cmd register busy bit to clear */
	retries_remaining = 100000;
	do {
		reg = hfa384x_getreg(hw, HFA384x_CMD);
		udelay(10);
	}
	while (HFA384x_CMD_ISBUSY(reg) && --retries_remaining);
	if (retries_remaining != 0) {
		/* busy bit clear, it's OK to write to ParamX regs */
		hfa384x_setreg(hw, HFA384x_AUXPW0,
			HFA384x_PARAM0);
		hfa384x_setreg(hw, HFA384x_AUXPW1,
			HFA384x_PARAM1);
		hfa384x_setreg(hw, HFA384x_AUXPW2,
			HFA384x_PARAM2);

		/* Set the aux enable in the Control register */
		hfa384x_setreg(hw, HFA384x_CONTROL_AUX_DOENABLE,
			HFA384x_CONTROL);

		/* Now wait for completion */
		retries_remaining = 100000;
		do {
			reg = hfa384x_getreg(hw, HFA384x_CONTROL);
			udelay(10);
		}
		while ( ((reg & (BIT14|BIT15)) != HFA384x_CONTROL_AUX_ISENABLED) &&
			--retries_remaining );
		if (retries_remaining != 0) {
			result = 0;
			hw->auxen++;
		}
	}

	/* Force it enabled even if the command failed, if told.. */
	if ((hw->auxen == auxen_mirror) && force)
		hw->auxen++;

	spin_unlock_irqrestore( &(hw->cmdlock), flags);
	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_cmd_aux_disable
*
* Goes through the process of disabling the auxilary port
* enabled with aux_enable().
*
* Arguments:
*	hw		device structure
*
* Returns:
*	0		success
*	>0		f/w reported failure - f/w status code
*	<0		driver reported error (timeout)
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_cmd_aux_disable(hfa384x_t *hw)
{
	int		result = -ETIMEDOUT;
	unsigned long   timeout;
	UINT16		reg = 0;

	DBFENTER;

	/* See if there's more than one enable */
	if (hw->auxen) hw->auxen--;
	if (hw->auxen) return 0;

	/* Clear the aux enable in the Control register */
	hfa384x_setreg(hw, 0, HFA384x_PARAM0);
	hfa384x_setreg(hw, 0, HFA384x_PARAM1);
	hfa384x_setreg(hw, 0, HFA384x_PARAM2);
	hfa384x_setreg(hw, HFA384x_CONTROL_AUX_DODISABLE,
		HFA384x_CONTROL);

	/* Now wait for completion */
	timeout = jiffies + 1*HZ;
	reg = hfa384x_getreg(hw, HFA384x_CONTROL);
	while ( ((reg & (BIT14|BIT15)) != HFA384x_CONTROL_AUX_ISDISABLED) &&
		time_before(jiffies,timeout) ){
		udelay(10);
		reg = hfa384x_getreg(hw, HFA384x_CONTROL);
	}
	if ((reg & (BIT14|BIT15)) == HFA384x_CONTROL_AUX_ISDISABLED ) {
		result = 0;
	}
	DBFEXIT;
	return result;
}

/*----------------------------------------------------------------
* hfa384x_drvr_low_level
*
* Write test commands to the card.  Some test commands don't make
* sense without prior set-up.  For example, continous TX isn't very
* useful until you set the channel.  That functionality should be
*
* Side effects:
*
* Call context:
*      process thread
* -----------------------------------------------------------------*/
int hfa384x_drvr_low_level(hfa384x_t *hw, hfa384x_metacmd_t *cmd)
{
	int             result = 0;
	DBFENTER;

	/* Do i need a host2hfa... conversion ? */
#if 0
	printk(KERN_INFO "%#x %#x %#x %#x\n", cmd->cmd, cmd->parm0, cmd->parm1, cmd->parm2);
#endif
	spin_lock_bh(&hw->cmdlock);
	result = hfa384x_docmd_wait(hw, cmd);
	spin_unlock_bh(&hw->cmdlock);

	DBFEXIT;
	return result;
}


/* TODO: determine if these will ever be needed */
#if 0
int hfa384x_cmd_readmif(hfa384x_t *hw)
{
	DBFENTER;
	DBFEXIT;
	return 0;
}


int hfa384x_cmd_writemif(hfa384x_t *hw)
{
	DBFENTER;
	DBFEXIT;
	return 0;
}
#endif

/*----------------------------------------------------------------
* hfa384x_drvr_mmi_read
*
* Read mmi registers.  mmi is intersil-speak for the baseband
* processor registers.
*
* Arguments:
*       hw              device structure
*       register        The test register to be accessed (must be even #).
*
* Returns:
*       0               success
*       >0              f/w reported error - f/w status code
*       <0              driver reported error
*
* Side effects:
*
* Call context:
*       process thread
----------------------------------------------------------------*/
int hfa384x_drvr_mmi_read(hfa384x_t *hw, UINT32 addr, UINT32 *resp)
{
        int             result = 0;
	hfa384x_metacmd_t cmd;

        DBFENTER;
	cmd.cmd = (UINT16) 0x30;
	cmd.parm0 = (UINT16) addr;
	cmd.parm1 = 0;
	cmd.parm2 = 0;

        /* Do i need a host2hfa... conversion ? */
	spin_lock_bh(&hw->cmdlock);
	result = hfa384x_docmd_wait(hw, &cmd);
	spin_unlock_bh(&hw->cmdlock);

	*resp = (UINT32) cmd.result.resp0;

        DBFEXIT;
        return result;
}

/*----------------------------------------------------------------
* hfa384x_drvr_mmi_write
*
* Read mmi registers.  mmi is intersil-speak for the baseband
* processor registers.
*
* Arguments:
*       hw              device structure
*       addr            The test register to be accessed (must be even #).
*       data            The data value to write to the register.
*
* Returns:
*       0               success
*       >0              f/w reported error - f/w status code
*       <0              driver reported error
*
* Side effects:
*
* Call context:
*       process thread
----------------------------------------------------------------*/

int
hfa384x_drvr_mmi_write(hfa384x_t *hw, UINT32 addr, UINT32 data)
{
        int             result = 0;
	hfa384x_metacmd_t cmd;

        DBFENTER;
	cmd.cmd = (UINT16) 0x31;
	cmd.parm0 = (UINT16) addr;
	cmd.parm1 = (UINT16) data;
	cmd.parm2 = 0;

        WLAN_LOG_DEBUG(1,"mmi write : addr = 0x%08x\n", addr);
        WLAN_LOG_DEBUG(1,"mmi write : data = 0x%08x\n", data);

        /* Do i need a host2hfa... conversion ? */
	spin_lock_bh(&hw->cmdlock);
	result = hfa384x_docmd_wait(hw, &cmd);
	spin_unlock_bh(&hw->cmdlock);

        DBFEXIT;
        return result;
}


/* TODO: determine if these will ever be needed */
#if 0
int hfa384x_cmd_readmif(hfa384x_t *hw)
{
        DBFENTER;
        DBFEXIT;
        return 0;
}


int hfa384x_cmd_writemif(hfa384x_t *hw)
{
        DBFENTER;
        DBFEXIT;
        return 0;
}
#endif



/*----------------------------------------------------------------
* hfa384x_copy_from_bap
*
* Copies a collection of bytes from the MAC controller memory via
* one set of BAP registers.
*
* Arguments:
*	hw		device structure
*	bap		[0|1] which BAP to use
*	id		FID or RID, destined for the select register (host order)
*	offset		An _even_ offset into the buffer for the given
*			FID/RID.  We haven't the means to validate this,
*			so be careful. (host order)
*	buf		ptr to array of bytes
*	len		length of data to transfer in bytes
*
* Returns:
*	0		success
*	>0		f/w reported failure - value of offset reg.
*	<0		driver reported error (timeout|bad arg)
*
* Side effects:
*
* Call context:
*	process thread
*	interrupt
----------------------------------------------------------------*/
int hfa384x_copy_from_bap(hfa384x_t *hw, UINT16 bap, UINT16 id, UINT16 offset,
				void *buf, UINT len)
{
	int		result = 0;
	unsigned long	flags = 0;
	UINT8		*d = (UINT8*)buf;
	UINT		selectreg;
	UINT		offsetreg;
	UINT		datareg;
	UINT		i;
	UINT16		reg = 0;

	DBFENTER;

	/* Validate bap, offset, buf, and len */
	if ( (bap > 1) ||
	     (offset > HFA384x_BAP_OFFSET_MAX) ||
	     (offset % 2) ||
	     (buf == NULL) ||
	     (len > HFA384x_BAP_DATALEN_MAX) ){
	     	result = -EINVAL;
	} else {
		selectreg = (bap == 1) ?  HFA384x_SELECT1 : HFA384x_SELECT0 ;
		offsetreg = (bap == 1) ?  HFA384x_OFFSET1 : HFA384x_OFFSET0 ;
		datareg =   (bap == 1) ?  HFA384x_DATA1 : HFA384x_DATA0 ;

		/* Obtain lock */
		spin_lock_irqsave( &(hw->baplock), flags);

		/* Write id to select reg */
		hfa384x_setreg(hw, id, selectreg);
		/* Write offset to offset reg */
		hfa384x_setreg(hw, offset, offsetreg);
		/* Wait for offset[busy] to clear (see BAP_TIMEOUT) */
		i = 0;
		do {
			reg = hfa384x_getreg(hw, offsetreg);
			if ( i > 0 ) udelay(10);
			i++;
		} while ( i < prism2_bap_timeout && HFA384x_OFFSET_ISBUSY(reg));
#if (WLAN_HOSTIF != WLAN_PCI)
		/* Release lock */
		spin_unlock_irqrestore( &(hw->baplock), flags);
#endif

		if ( HFA384x_OFFSET_ISBUSY(reg) ){
			/* If timeout, return -ETIMEDOUT */
			result = reg;
		} else if ( HFA384x_OFFSET_ISERR(reg) ){
			/* If offset[err] == 1, return -EINVAL */
			result = reg;
		} else {
			/* Read even(len) buf contents from data reg */
			for ( i = 0; i < (len & 0xfffe); i+=2 ) {
				*(UINT16*)(&(d[i])) =
					hfa384x_getreg_noswap(hw, datareg);
			}
			/* If len odd, handle last byte */
			if ( len % 2 ){
				reg = hfa384x_getreg_noswap(hw, datareg);
				d[len-1] = ((UINT8*)(&reg))[0];
			}
		}

		/* According to Intersil errata dated 9/16/02:

		"In PRISM PCI MAC host interface, if both BAPs are concurrently
		 requesing memory access, both will accept the Ack.   There is no
		 firmware workaround possible.  To prevent BAP access failures or
		 hang conditions the host MUST NOT access both BAPs in sucession
		 unless at least 5us elapses between accesses.  The safest choice
		 is to USE ONLY ONE BAP for all data movement operations."

		 What this means:

		 We have to serialize ALL BAP accesses, and furthermore, add a 5us
		 delay after access if we're using a PCI platform.

		 Unfortunately, this means we have to lock out interrupts througout
		 the entire BAP copy.

		 It remains to be seen if "BAP access" means "BAP setup" or the more
		 literal definition of "copying data back and forth"  I'm erring for
		 the latter, safer definition.  -- SLP.

		*/

#if (WLAN_HOSTIF == WLAN_PCI)
		udelay(5);
		/* Release lock */
		spin_unlock_irqrestore( &(hw->baplock), flags);
#endif

	}

	if (result) {
	  WLAN_LOG_DEBUG(1,
			  "copy_from_bap(0x%04x, 0, %d) failed, result=0x%x\n",
			  reg, len, result);
	}
	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_copy_to_bap
*
* Copies a collection of bytes to the MAC controller memory via
* one set of BAP registers.
*
* Arguments:
*	hw		device structure
*	bap		[0|1] which BAP to use
*	id		FID or RID, destined for the select register (host order)
*	offset		An _even_ offset into the buffer for the given
*			FID/RID.  We haven't the means to validate this,
*			so be careful. (host order)
*	buf		ptr to array of bytes
*	len		length of data to transfer (in bytes)
*
* Returns:
*	0		success
*	>0		f/w reported failure - value of offset reg.
*	<0		driver reported error (timeout|bad arg)
*
* Side effects:
*
* Call context:
*	process thread
*	interrupt
----------------------------------------------------------------*/
int hfa384x_copy_to_bap(hfa384x_t *hw, UINT16 bap, UINT16 id, UINT16 offset,
				void *buf, UINT len)
{
	return hfa384x_copy_to_bap4(hw, bap, id, offset, buf, len, NULL, 0, NULL, 0, NULL, 0);
}

int hfa384x_copy_to_bap4(hfa384x_t *hw, UINT16 bap, UINT16 id, UINT16 offset,
			 void *buf, UINT len1, void* buf2, UINT len2,
			 void *buf3, UINT len3, void *buf4, UINT len4)
{
	int		result = 0;
	unsigned long	flags = 0;
	UINT8		*d;
	UINT		selectreg;
	UINT		offsetreg;
	UINT		datareg;
	UINT		i;
	UINT16		reg;

	DBFENTER;

//	printk(KERN_DEBUG "ctb1 %d id %04x o %d %d %d %d %d\n", bap, id, offset, len1, len2, len3, len4);

	/* Validate bap, offset, buf, and len */
	if ( (bap > 1) ||
	     (offset > HFA384x_BAP_OFFSET_MAX) ||
	     (offset % 2) ||
	     (buf == NULL) ||
	     (len1+len2+len3+len4 > HFA384x_BAP_DATALEN_MAX) ){
	     	result = -EINVAL;
	} else {
		selectreg = (bap == 1) ? HFA384x_SELECT1 : HFA384x_SELECT0;
		offsetreg = (bap == 1) ? HFA384x_OFFSET1 : HFA384x_OFFSET0;
		datareg =   (bap == 1) ? HFA384x_DATA1   : HFA384x_DATA0;
		/* Obtain lock */
		spin_lock_irqsave( &(hw->baplock), flags);

		/* Write id to select reg */
		hfa384x_setreg(hw, id, selectreg);
		udelay(10);
		/* Write offset to offset reg */
		hfa384x_setreg(hw, offset, offsetreg);
		/* Wait for offset[busy] to clear (see BAP_TIMEOUT) */
		i = 0;
		do {
			reg = hfa384x_getreg(hw, offsetreg);
			if ( i > 0 ) udelay(10);
			i++;
		} while ( i < prism2_bap_timeout && HFA384x_OFFSET_ISBUSY(reg));

#if (WLAN_HOSTIF != WLAN_PCI)
		/* Release lock */
		spin_unlock_irqrestore( &(hw->baplock), flags);
#endif

		if ( HFA384x_OFFSET_ISBUSY(reg) ){
			/* If timeout, return reg */
			result = reg;
		} else if ( HFA384x_OFFSET_ISERR(reg) ){
			/* If offset[err] == 1, return reg */
			result = reg;
		} else {
			d = (UINT8*)buf;
			/* Write even(len1) buf contents to data reg */
			for ( i = 0; i < (len1 & 0xfffe); i+=2 ) {
				hfa384x_setreg_noswap(hw,
					*(UINT16*)(&(d[i])), datareg);
			}
			if (len1 & 1) {
				UINT16 data;
				UINT8 *b = (UINT8 *) &data;
				b[0] = d[len1-1];
				if (buf2 != NULL) {
					d = (UINT8*)buf2;
					b[1] = d[0];
					len2--;
					buf2++;
				}
				hfa384x_setreg_noswap(hw, data, datareg);
			}
			if ((buf2 != NULL) && (len2 > 0)) {
				/* Write even(len2) buf contents to data reg */
				d = (UINT8*)buf2;
				for ( i = 0; i < (len2 & 0xfffe); i+=2 ) {
					hfa384x_setreg_noswap(hw, *(UINT16*)(&(d[i])), datareg);
				}
				if (len2 & 1) {
					UINT16 data;
					UINT8 *b = (UINT8 *) &data;
					b[0] = d[len2-1];
					if (buf3 != NULL) {
						d = (UINT8*)buf3;
						b[1] = d[0];
						len3--;
						buf3++;
					}
					hfa384x_setreg_noswap(hw, data, datareg);
				}
			}

			if ((buf3 != NULL) && (len3 > 0)) {
				/* Write even(len3) buf contents to data reg */
				d = (UINT8*)buf3;
				for ( i = 0; i < (len3 & 0xfffe); i+=2 ) {
					hfa384x_setreg_noswap(hw, *(UINT16*)(&(d[i])), datareg);
				}
				if (len3 & 1) {
					UINT16 data;
					UINT8 *b = (UINT8 *) &data;
					b[0] = d[len3-1];
					if (buf4 != NULL) {
						d = (UINT8*)buf4;
						b[1] = d[0];
						len4--;
						buf4++;
					}
					hfa384x_setreg_noswap(hw, data, datareg);
				}
			}
			if ((buf4 != NULL) && (len4 > 0)) {
				/* Write even(len4) buf contents to data reg */
				d = (UINT8*)buf4;
				for ( i = 0; i < (len4 & 0xfffe); i+=2 ) {
					hfa384x_setreg_noswap(hw, *(UINT16*)(&(d[i])), datareg);
				}
				if (len4 & 1) {
					UINT16 data;
					UINT8 *b = (UINT8 *) &data;
					b[0] = d[len4-1];
					b[1] = 0;

					hfa384x_setreg_noswap(hw, data, datareg);
				}
			}
//			printk(KERN_DEBUG "ctb2 %d id %04x o %d %d %d %d %d\n", bap, id, offset, len1, len2, len3, len4);

		}

#if (WLAN_HOSTIF == WLAN_PCI)
		udelay(5);
		/* Release lock */
		spin_unlock_irqrestore( &(hw->baplock), flags);
#endif

	}

	if (result)
		WLAN_LOG_ERROR("copy_to_bap() failed.\n");

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_copy_from_aux
*
* Copies a collection of bytes from the controller memory.  The
* Auxiliary port MUST be enabled prior to calling this function.
* We _might_ be in a download state.
*
* Arguments:
*	hw		device structure
*	cardaddr	address in hfa384x data space to read
*	auxctl		address space select
*	buf		ptr to destination host buffer
*	len		length of data to transfer (in bytes)
*
* Returns:
*	nothing
*
* Side effects:
*	buf contains the data copied
*
* Call context:
*	process thread
*	interrupt
----------------------------------------------------------------*/
void
hfa384x_copy_from_aux(
	hfa384x_t *hw, UINT32 cardaddr, UINT32 auxctl, void *buf, UINT len)
{
	UINT16		currpage;
	UINT16		curroffset;
	UINT		i = 0;

	DBFENTER;

	if ( !(hw->auxen) ) {
		WLAN_LOG_DEBUG(1,
			"Attempt to read 0x%04x when aux not enabled\n",
			cardaddr);
		return;

	}
	/* Build appropriate aux page and offset */
	currpage = HFA384x_AUX_MKPAGE(cardaddr);
	curroffset = HFA384x_AUX_MKOFF(cardaddr, auxctl);
	hfa384x_setreg(hw, currpage, HFA384x_AUXPAGE);
	hfa384x_setreg(hw, curroffset, HFA384x_AUXOFFSET);
	udelay(5);	/* beat */

	/* read the data */
	while ( i < len) {
		*((UINT16*)(buf+i)) = hfa384x_getreg_noswap(hw, HFA384x_AUXDATA);
		i+=2;
		curroffset+=2;
		if ( (curroffset&HFA384x_ADDR_AUX_OFF_MASK) >
			HFA384x_ADDR_AUX_OFF_MAX ) {
			currpage++;
			curroffset = 0;
			curroffset = HFA384x_AUX_MKOFF(curroffset, auxctl);
			hfa384x_setreg(hw, currpage, HFA384x_AUXPAGE);
			hfa384x_setreg(hw, curroffset, HFA384x_AUXOFFSET);
			udelay(5);	/* beat */
		}
	}
	/* Make sure the auxctl bits are clear */
	hfa384x_setreg(hw, 0, HFA384x_AUXOFFSET);
	DBFEXIT;
}


/*----------------------------------------------------------------
* hfa384x_copy_to_aux
*
* Copies a collection of bytes to the controller memory.  The
* Auxiliary port MUST be enabled prior to calling this function.
* We _might_ be in a download state.
*
* Arguments:
*	hw		device structure
*	cardaddr	address in hfa384x data space to read
*	auxctl		address space select
*	buf		ptr to destination host buffer
*	len		length of data to transfer (in bytes)
*
* Returns:
*	nothing
*
* Side effects:
*	Controller memory now contains a copy of buf
*
* Call context:
*	process thread
*	interrupt
----------------------------------------------------------------*/
void
hfa384x_copy_to_aux(
	hfa384x_t *hw, UINT32 cardaddr, UINT32 auxctl, void *buf, UINT len)
{
	UINT16		currpage;
	UINT16		curroffset;
	UINT		i = 0;

	DBFENTER;

	if ( !(hw->auxen) ) {
		WLAN_LOG_DEBUG(1,
			"Attempt to read 0x%04x when aux not enabled\n",
			cardaddr);
		return;

	}
	/* Build appropriate aux page and offset */
	currpage = HFA384x_AUX_MKPAGE(cardaddr);
	curroffset = HFA384x_AUX_MKOFF(cardaddr, auxctl);
	hfa384x_setreg(hw, currpage, HFA384x_AUXPAGE);
	hfa384x_setreg(hw, curroffset, HFA384x_AUXOFFSET);
	udelay(5);	/* beat */

	/* write the data */
	while ( i < len) {
		hfa384x_setreg_noswap(hw,
			*((UINT16*)(buf+i)), HFA384x_AUXDATA);
		i+=2;
		curroffset+=2;
		if ( curroffset > HFA384x_ADDR_AUX_OFF_MAX ) {
			currpage++;
			curroffset = 0;
			hfa384x_setreg(hw, currpage, HFA384x_AUXPAGE);
			hfa384x_setreg(hw, curroffset, HFA384x_AUXOFFSET);
			udelay(5);	/* beat */
		}
	}
	DBFEXIT;
}


/*----------------------------------------------------------------
* hfa384x_cmd_wait
*
* Waits for availability of the Command register, then
* issues the given command.  Then polls the Evstat register
* waiting for command completion.  Timeouts shouldn't be
* possible since we're preventing overlapping commands and all
* commands should be cleared and acknowledged.
*
* Arguments:
*	wlandev		device structure
*       cmd             cmd structure.  Includes all arguments and result
*                       data points.  All in host order.
*
* Returns:
*	0		success
*	-ETIMEDOUT	timed out waiting for register ready or
*			command completion
*	>0		command indicated error, Status and Resp0-2 are
*			in hw structure.
*
* Side effects:
*
*
* Call context:
*	process thread
----------------------------------------------------------------*/
static int hfa384x_docmd_wait( hfa384x_t *hw, hfa384x_metacmd_t *cmd)
{
	int		result = -ETIMEDOUT;
	UINT16		reg = 0;
	UINT16          counter;

	DBFENTER;

	hw->cmdflag = 0;
	hw->cmddata = cmd;

	/* wait for the busy bit to clear */
	counter = 0;
	reg = hfa384x_getreg(hw, HFA384x_CMD);
	while ( HFA384x_CMD_ISBUSY(reg) &&
		(counter < 10)) {
		reg = hfa384x_getreg(hw, HFA384x_CMD);
		counter++;
		udelay(10);
	}

	if (HFA384x_CMD_ISBUSY(reg)) {
		WLAN_LOG_ERROR("hfa384x_cmd timeout(1), reg=0x%0hx.\n", reg);
		goto failed;
	}
	if (!HFA384x_CMD_ISBUSY(reg)) {
		/* busy bit clear, write command */
		hfa384x_setreg(hw, cmd->parm0, HFA384x_PARAM0);
		hfa384x_setreg(hw, cmd->parm1, HFA384x_PARAM1);
		hfa384x_setreg(hw, cmd->parm2, HFA384x_PARAM2);
		hfa384x_setreg(hw, cmd->cmd, HFA384x_CMD);

#ifdef CMD_IRQ

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,2,0))
		while (! hw->cmdflag)
			interruptible_sleep_on(&hw->cmdq);
#else
		wait_event_interruptible(hw->cmdq, hw->cmdflag);
#endif
		result = HFA384x_STATUS_RESULT_GET(cmd->status);
#else // CMD_IRQ
		/* Now wait for completion */
		counter = 0;
		reg = hfa384x_getreg(hw, HFA384x_EVSTAT);
		/* Initialization is the problem.  It takes about
                   100ms. "normal" commands are typically is about
                   200-400 us (I've never seen less than 200).  Longer
                   is better so that we're not hammering the bus. */
		while ( !HFA384x_EVSTAT_ISCMD(reg) &&
			(counter < 5000)) {
			reg = hfa384x_getreg(hw, HFA384x_EVSTAT);
			counter++;
			udelay(200);
		}

		if ( HFA384x_EVSTAT_ISCMD(reg) ) {
			result = 0;
			cmd->result.status = hfa384x_getreg(hw, HFA384x_STATUS);
			cmd->result.resp0 = hfa384x_getreg(hw, HFA384x_RESP0);
			cmd->result.resp1 = hfa384x_getreg(hw, HFA384x_RESP1);
			cmd->result.resp2 = hfa384x_getreg(hw, HFA384x_RESP2);
			hfa384x_setreg(hw, HFA384x_EVACK_CMD,
				HFA384x_EVACK);
			result = HFA384x_STATUS_RESULT_GET(cmd->result.status);
		} else {
			WLAN_LOG_ERROR("hfa384x_cmd timeout(2), reg=0x%0hx.\n", reg);
		}
#endif  /* CMD_IRQ */
	}

 failed:
	hw->cmdflag = 0;
	hw->cmddata = NULL;

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_dl_docmd_wait
*
* Waits for availability of the Command register, then
* issues the given command.  Then polls the Evstat register
* waiting for command completion.  Timeouts shouldn't be
* possible since we're preventing overlapping commands and all
* commands should be cleared and acknowledged.
*
* This routine is only used for downloads.  Since it doesn't lock out
* interrupts the system response is much better.
*
* Arguments:
*	wlandev		device structure
*       cmd             cmd structure.  Includes all arguments and result
*                       data points.  All in host order.
*
* Returns:
*	0		success
*	-ETIMEDOUT	timed out waiting for register ready or
*			command completion
*	>0		command indicated error, Status and Resp0-2 are
*			in hw structure.
*
* Side effects:
*
*
* Call context:
*	process thread
----------------------------------------------------------------*/
static int hfa384x_dl_docmd_wait( hfa384x_t *hw, hfa384x_metacmd_t *cmd)
{
	int		result = -ETIMEDOUT;
	unsigned long	timeout;
	UINT16		reg = 0;

	DBFENTER;
	/* wait for the busy bit to clear */
	timeout = jiffies + 1*HZ;
	reg = hfa384x_getreg(hw, HFA384x_CMD);
	while ( HFA384x_CMD_ISBUSY(reg) && time_before( jiffies, timeout) ) {
		reg = hfa384x_getreg(hw, HFA384x_CMD);
		udelay(10);
	}
	if (HFA384x_CMD_ISBUSY(reg)) {
		WLAN_LOG_WARNING("Timed out waiting for cmd register.\n");
		goto failed;
	}

	if (!HFA384x_CMD_ISBUSY(reg)) {
		/* busy bit clear, write command */
		hfa384x_setreg(hw, cmd->parm0, HFA384x_PARAM0);
		hfa384x_setreg(hw, cmd->parm1, HFA384x_PARAM1);
		hfa384x_setreg(hw, cmd->parm2, HFA384x_PARAM2);
		hfa384x_setreg(hw, cmd->cmd, HFA384x_CMD);

		/* Now wait for completion */
		if ( (HFA384x_CMD_CMDCODE_GET(cmd->cmd) == HFA384x_CMDCODE_DOWNLD) ) {
			/* dltimeout is in ms */
			timeout = (((UINT32)hw->dltimeout) / 1000UL) * HZ;
			if ( timeout > 0 ) {
				timeout += jiffies;
			} else {
				timeout = jiffies + 1*HZ;
			}
		} else {
			timeout = jiffies + 1*HZ;
		}
		reg = hfa384x_getreg(hw, HFA384x_EVSTAT);
		while ( !HFA384x_EVSTAT_ISCMD(reg) && time_before(jiffies,timeout) ) {
			udelay(100);
			reg = hfa384x_getreg(hw, HFA384x_EVSTAT);
		}
		if ( HFA384x_EVSTAT_ISCMD(reg) ) {
			result = 0;
			cmd->result.status = hfa384x_getreg(hw, HFA384x_STATUS);
			cmd->result.resp0 = hfa384x_getreg(hw, HFA384x_RESP0);
			cmd->result.resp1 = hfa384x_getreg(hw, HFA384x_RESP1);
			cmd->result.resp2 = hfa384x_getreg(hw, HFA384x_RESP2);
			hfa384x_setreg(hw, HFA384x_EVACK_CMD, HFA384x_EVACK);
			result = HFA384x_STATUS_RESULT_GET(cmd->result.status);
		}
	}

failed:
	DBFEXIT;
	return result;
}

/*----------------------------------------------------------------
* hfa384x_drvr_start
*
* Issues the MAC initialize command, sets up some data structures,
* and enables the interrupts.  After this function completes, the
* low-level stuff should be ready for any/all commands.
*
* Arguments:
*	hw		device structure
* Returns:
*	0		success
*	>0		f/w reported error - f/w status code
*	<0		driver reported error
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_drvr_start(hfa384x_t *hw)
{
	int	result = 0;
	UINT16			reg;
	int			i;
	int			j;
	DBFENTER;

	/* call initialize */
	result = hfa384x_cmd_initialize(hw);
	if (result != 0) {
		WLAN_LOG_ERROR("Initialize command failed.\n");
		goto failed;
	}

	/* make sure interrupts are disabled and any layabout events cleared */
	hfa384x_setreg(hw, 0, HFA384x_INTEN);
	hfa384x_setreg(hw, 0xffff, HFA384x_EVACK);

        hw->txfid_head = 0;
        hw->txfid_tail = 0;
        hw->txfid_N = HFA384x_DRVR_FIDSTACKLEN_MAX;
        memset(hw->txfid_queue, 0, sizeof(hw->txfid_queue));

	/* Allocate tx and notify FIDs */
	/* First, tx */
	for ( i = 0; i < HFA384x_DRVR_FIDSTACKLEN_MAX-1; i++) {
		result = hfa384x_cmd_allocate(hw, HFA384x_DRVR_TXBUF_MAX);
		if (result != 0) {
			WLAN_LOG_ERROR("Allocate(tx) command failed.\n");
			goto failed;
		}
		j = 0;
		do {
			reg = hfa384x_getreg(hw, HFA384x_EVSTAT);
			udelay(10);
			j++;
		} while ( !HFA384x_EVSTAT_ISALLOC(reg) && j < 50); /* 50 is timeout */
		if ( j >= 50 ) {
			WLAN_LOG_ERROR("Timed out waiting for evalloc(tx).\n");
			result = -ETIMEDOUT;
			goto failed;
		}
		reg = hfa384x_getreg(hw, HFA384x_ALLOCFID);

		txfid_queue_add(hw, reg);

		WLAN_LOG_DEBUG(4,"hw->txfid_queue[%d]=0x%04x\n",i,reg);

		reg = HFA384x_EVACK_ALLOC_SET(1);
		hfa384x_setreg(hw, reg, HFA384x_EVACK);

	}

	/* Now, the info frame fid */
	result = hfa384x_cmd_allocate(hw, HFA384x_INFOFRM_MAXLEN);
	if (result != 0) {
		WLAN_LOG_ERROR("Allocate(tx) command failed.\n");
		goto failed;
	}
	i = 0;
	do {
		reg = hfa384x_getreg(hw, HFA384x_EVSTAT);
		udelay(10);
		i++;
	} while ( !HFA384x_EVSTAT_ISALLOC(reg) && i < 50); /* 50 is timeout */
	if ( i >= 50 ) {
		WLAN_LOG_ERROR("Timed out waiting for evalloc(info).\n");
		result = -ETIMEDOUT;
		goto failed;
	}
	hw->infofid = hfa384x_getreg(hw, HFA384x_ALLOCFID);
	reg = HFA384x_EVACK_ALLOC_SET(1);
	hfa384x_setreg(hw, reg, HFA384x_EVACK);
	WLAN_LOG_DEBUG(4,"hw->infofid=0x%04x\n", hw->infofid);

	/* Set swsupport regs to magic # for card presence detection */
	hfa384x_setreg(hw, HFA384x_DRVR_MAGIC, HFA384x_SWSUPPORT0);

	/* Now enable the interrupts and set the running state */
	hfa384x_setreg(hw, 0xffff, HFA384x_EVSTAT);
	hfa384x_events_all(hw);

	hw->state = HFA384x_STATE_RUNNING;

	goto done;
failed:
	WLAN_LOG_ERROR("Failed, result=%d\n", result);
done:
	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_drvr_stop
*
* Issues the initialize command to leave us in the 'reset' state.
*
* Arguments:
*	hw		device structure
* Returns:
*	0		success
*	>0		f/w reported error - f/w status code
*	<0		driver reported error
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_drvr_stop(hfa384x_t *hw)
{
	int	result = 0;
	int i;
	DBFENTER;

	del_timer_sync(&hw->commsqual_timer);

	if ( hw->wlandev->hwremoved ) {
		/* only flush when we're shutting down for good */
		flush_scheduled_work();
	}

	if (hw->state == HFA384x_STATE_RUNNING) {
		/*
		 * Send the MAC initialize cmd.
		 */
		hfa384x_cmd_initialize(hw);

		/*
		 * Make absolutely sure interrupts are disabled and any
		 * layabout events cleared
		 */
		hfa384x_setreg(hw, 0, HFA384x_INTEN);
		hfa384x_setreg(hw, 0xffff, HFA384x_EVACK);
	}

	tasklet_kill(&hw->bap_tasklet);

	hw->link_status = HFA384x_LINK_NOTCONNECTED;
	hw->state = HFA384x_STATE_INIT;

	/* Clear all the port status */
	for ( i = 0; i < HFA384x_NUMPORTS_MAX; i++) {
		hw->port_enabled[i] = 0;
	}

	DBFEXIT;
	return result;
}


/*----------------------------------------------------------------
* hfa384x_drvr_txframe
*
* Takes a frame from prism2sta and queues it for transmission.
*
* Arguments:
*	hw		device structure
*	skb		packet buffer struct.  Contains an 802.11
*			data frame.
*       p80211_hdr      points to the 802.11 header for the packet.
* Returns:
*	0		Success and more buffs available
*	1		Success but no more buffs
*	2		Allocation failure
*	3		MAC Tx command failed
*	4		Buffer full or queue busy
*
* Side effects:
*
* Call context:
*	process thread
----------------------------------------------------------------*/
int hfa384x_drvr_txframe(hfa384x_t *hw, struct sk_buff *skb, p80211_hdr_t *p80211_hdr, p80211_metawep_t *p80211_wep)
{
	hfa384x_tx_frame_t	txdesc;
	UINT16			macq = 0;
	UINT16			fid;
	int			result;

	DBFENTER;

	/* Build Tx frame structure */
	/* Set up the control field */
	memset(&txdesc, 0, sizeof(txdesc));

/* Tx complete and Tx exception disable per dleach.  Might be causing
 * buf depletion
 */
#define DOBOTH 1
#if DOBOTH
	txdesc.tx_control =
		HFA384x_TX_MACPORT_SET(0) | HFA384x_TX_STRUCTYPE_SET(1) |
		HFA384x_TX_TXEX_SET(1) | HFA384x_TX_TXOK_SET(1);
#elif DOEXC
	txdesc.tx_control =
		HFA384x_TX_MACPORT_SET(0) | HFA384x_TX_STRUCTYPE_SET(1) |
		HFA384x_TX_TXEX_SET(1) | HFA384x_TX_TXOK_SET(0);
#else
	txdesc.tx_control =
		HFA384x_TX_MACPORT_SET(0) | HFA384x_TX_STRUCTYPE_SET(1) |
		HFA384x_TX_TXEX_SET(0) | HFA384x_TX_TXOK_SET(0);
#endif

	/* if we're using host WEP, increase size by IV+ICV */
	if (p80211_wep->data) {
		txdesc.data_len = host2hfa384x_16(skb->len+8);
		//		txdesc.tx_control |= HFA384x_TX_NOENCRYPT_SET(1);
	} else {
		txdesc.data_len =  host2hfa384x_16(skb->len);
	}

	txdesc.tx_control = host2hfa384x_16(txdesc.tx_control);
	/* copy the header over to the txdesc */
	memcpy(&(txdesc.frame_control), p80211_hdr, sizeof(p80211_hdr_t));

	/* Since tbusy is set whenever the stack is empty, there should
	 * always be something on the stack if we get to this point.
	 * [MSM]: NOT TRUE!!!!! so I added the test of fid below.
	 */

	/* Allocate FID */

	fid = txfid_queue_remove(hw);

	if ( fid == 0 ) { /* stack or queue was empty */
		return 4;
	}

	/* now let's get the cmdlock */
	spin_lock(&hw->cmdlock);

	/* Copy descriptor+payload to FID */
        if (p80211_wep->data) {
		result = hfa384x_copy_to_bap4(hw, HFA384x_BAP_PROC, fid, 0,
					      &txdesc, sizeof(txdesc),
					      p80211_wep->iv, sizeof(p80211_wep->iv),
					      p80211_wep->data, skb->len,
					      p80211_wep->icv, sizeof(p80211_wep->icv));
	} else {
		result = hfa384x_copy_to_bap4(hw, HFA384x_BAP_PROC, fid, 0,
					      &txdesc, sizeof(txdesc),
					      skb->data, skb->len,
					      NULL, 0, NULL, 0);
	}

	if ( result ) {
		WLAN_LOG_DEBUG(1,
			"copy_to_bap(%04x, %d, %d) failed, result=0x%x\n",
			fid,
		 	sizeof(txdesc),
	 		skb->len,
			result);

		/* put the fid back in the queue */
		txfid_queue_add(hw, fid);

		result = 3;
		goto failed;
	}

	/* Issue Tx command */
	result = hfa384x_cmd_transmit(hw, HFA384x_TXCMD_RECL, macq, fid);

	if ( result != 0 ) {
		txfid_queue_add(hw, fid);

		WLAN_LOG_DEBUG(1,"cmd_tx(%04x) failed, result=%d\n",
			fid, result);
		result = 3;
		goto failed;
	}

	/* indicate we haven't any buffers, int_alloc will clear */
	result = txfid_queue_empty(hw);
failed:

	spin_unlock(&hw->cmdlock);

	DBFEXIT;
	return result;
}

/*----------------------------------------------------------------
* hfa384x_interrupt
*
* Driver interrupt handler.
*
* Arguments:
*	irq		irq number
*	dev_id		pointer to the device
*	regs		registers
*
* Returns:
*	nothing
*
* Side effects:
*	May result in a frame being passed up the stack or an info
*	frame being handled.
*
* Call context:
*	Ummm, could it be interrupt?
----------------------------------------------------------------*/
irqreturn_t hfa384x_interrupt(int irq, void *dev_id PT_REGS)
{
	int			reg;
	wlandevice_t		*wlandev = (wlandevice_t*)dev_id;
	hfa384x_t		*hw = wlandev->priv;
	int			ev_read = 0;
	DBFENTER;

	if (!wlandev || wlandev->hwremoved)
		return IRQ_NONE;  /* Not much we can do w/o hardware */
#if (WLAN_HOSTIF == WLAN_PCMCIA)
	if (hw->iobase == 0)  /* XXX FIXME Properly */
		return IRQ_NONE;
#endif

	for (;;ev_read++) {
		if (ev_read >= prism2_irq_evread_max)
			break;

		/* Check swsupport reg magic # for card presence */
		reg = hfa384x_getreg(hw, HFA384x_SWSUPPORT0);
		if ( reg != HFA384x_DRVR_MAGIC) {
			WLAN_LOG_DEBUG(2, "irq=%d, no magic.  Card removed?.\n", irq);
			break;
		}

		/* read the EvStat register for interrupt enabled events */
		reg = hfa384x_getreg(hw, HFA384x_EVSTAT);

		/* AND with the enabled interrupts */
		reg &= hfa384x_getreg(hw, HFA384x_INTEN);

		/* Handle the events */
		if ( HFA384x_EVSTAT_ISWTERR(reg) ){
			WLAN_LOG_ERROR(
			"Error: WTERR interrupt received (unhandled).\n");
			hfa384x_setreg(hw, HFA384x_EVACK_WTERR_SET(1),
				HFA384x_EVACK);
		}

		if ( HFA384x_EVSTAT_ISINFDROP(reg) ){
			hfa384x_int_infdrop(wlandev);
			hfa384x_setreg(hw, HFA384x_EVACK_INFDROP_SET(1),
				HFA384x_EVACK);
		}

		if (HFA384x_EVSTAT_ISBAP_OP(reg)) {
			/* Disable the BAP interrupts */
			hfa384x_events_nobap(hw);
			tasklet_schedule(&hw->bap_tasklet);
		}

		if ( HFA384x_EVSTAT_ISALLOC(reg) ){
			hfa384x_int_alloc(wlandev);
			hfa384x_setreg(hw, HFA384x_EVACK_ALLOC_SET(1),
				HFA384x_EVACK);
		}

		if ( HFA384x_EVSTAT_ISDTIM(reg) ){
			hfa384x_int_dtim(wlandev);
			hfa384x_setreg(hw, HFA384x_EVACK_DTIM_SET(1),
				HFA384x_EVACK);
		}
#ifdef CMD_IRQ
		if ( HFA384x_EVSTAT_ISCMD(reg) ){
			hfa384x_int_cmd(wlandev);
			hfa384x_setreg(hw, HFA384x_EVACK_CMD_SET(1),
				       HFA384x_EVACK);
		}
#endif

		/* allow the evstat to be updated after the evack */
		udelay(20);
	}

	DBFEXIT;
	return IRQ_HANDLED;
}

#ifdef CMD_IRQ
/*----------------------------------------------------------------
* hfa384x_int_cmd
*
* Handles command completion event.
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
void hfa384x_int_cmd(wlandevice_t *wlandev)
{
	hfa384x_t		*hw = wlandev->priv;
	DBFENTER;

	// check to make sure it's the right command?
	if (hw->cmddata) {
		hw->cmddata->status = hfa384x_getreg(hw, HFA384x_STATUS);
		hw->cmddata->resp0 = hfa384x_getreg(hw, HFA384x_RESP0);
		hw->cmddata->resp1 = hfa384x_getreg(hw, HFA384x_RESP1);
		hw->cmddata->resp2 = hfa384x_getreg(hw, HFA384x_RESP2);
	}
	hw->cmdflag = 1;

	printk(KERN_INFO "um. int_cmd\n");

	wake_up_interruptible(&hw->cmdq);

	// XXXX perform a bap copy too?

	DBFEXIT;
	return;
}
#endif

/*----------------------------------------------------------------
* hfa384x_int_dtim
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
static void hfa384x_int_dtim(wlandevice_t *wlandev)
{
#if 0
	hfa384x_t		*hw = wlandev->priv;
#endif
	DBFENTER;
	prism2sta_ev_dtim(wlandev);
	DBFEXIT;
	return;
}


/*----------------------------------------------------------------
* hfa384x_int_infdrop
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
static void hfa384x_int_infdrop(wlandevice_t *wlandev)
{
#if 0
	hfa384x_t		*hw = wlandev->priv;
#endif
	DBFENTER;
	prism2sta_ev_infdrop(wlandev);
	DBFEXIT;
	return;
}


/*----------------------------------------------------------------
* hfa384x_int_info
*
* Handles the Info event.
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
*	tasklet
----------------------------------------------------------------*/
static void hfa384x_int_info(wlandevice_t *wlandev)
{
	hfa384x_t		*hw = wlandev->priv;
	UINT16			reg;
	hfa384x_InfFrame_t	inf;
	int			result;
	DBFENTER;
	/* Retrieve the FID */
	reg = hfa384x_getreg(hw, HFA384x_INFOFID);

	/* Retrieve the length */
	result = hfa384x_copy_from_bap( hw,
		HFA384x_BAP_INT, reg, 0, &inf.framelen, sizeof(UINT16));
	if ( result ) {
		WLAN_LOG_DEBUG(1,
			"copy_from_bap(0x%04x, 0, %d) failed, result=0x%x\n",
			reg, sizeof(inf), result);
		goto failed;
	}
	inf.framelen = hfa384x2host_16(inf.framelen);

	/* Retrieve the rest */
	result = hfa384x_copy_from_bap( hw,
		HFA384x_BAP_INT, reg, sizeof(UINT16),
		&(inf.infotype), inf.framelen * sizeof(UINT16));
	if ( result ) {
		WLAN_LOG_DEBUG(1,
			"copy_from_bap(0x%04x, 0, %d) failed, result=0x%x\n",
			reg, sizeof(inf), result);
		goto failed;
	}

	prism2sta_ev_info(wlandev, &inf);
failed:
	DBFEXIT;
	return;
}


/*----------------------------------------------------------------
* hfa384x_int_txexc
*
* Handles the TxExc event.  A Transmit Exception event indicates
* that the MAC's TX process was unsuccessful - so the packet did
* not get transmitted.
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
*	tasklet
----------------------------------------------------------------*/
static void hfa384x_int_txexc(wlandevice_t *wlandev)
{
	hfa384x_t		*hw = wlandev->priv;
	UINT16			status;
	UINT16			fid;
	int			result = 0;
	DBFENTER;
	/* Collect the status and display */
	fid = hfa384x_getreg(hw, HFA384x_TXCOMPLFID);
	result = hfa384x_copy_from_bap(hw, HFA384x_BAP_INT, fid, 0, &status, sizeof(status));
	if ( result ) {
		WLAN_LOG_DEBUG(1,
			"copy_from_bap(0x%04x, 0, %d) failed, result=0x%x\n",
			fid, sizeof(status), result);
		goto failed;
	}
	status = hfa384x2host_16(status);
	prism2sta_ev_txexc(wlandev, status);
failed:
	DBFEXIT;
	return;
}


/*----------------------------------------------------------------
* hfa384x_int_tx
*
* Handles the Tx event.
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
*	tasklet
----------------------------------------------------------------*/
static void hfa384x_int_tx(wlandevice_t *wlandev)
{
	hfa384x_t		*hw = wlandev->priv;
	UINT16			fid;
	UINT16			status;
	int			result = 0;
	DBFENTER;
	fid = hfa384x_getreg(hw, HFA384x_TXCOMPLFID);
	result = hfa384x_copy_from_bap(hw, HFA384x_BAP_INT, fid, 0, &status, sizeof(status));
	if ( result ) {
		WLAN_LOG_DEBUG(1,
			"copy_from_bap(0x%04x, 0, %d) failed, result=0x%x\n",
			fid, sizeof(status), result);
		goto failed;
	}
	status = hfa384x2host_16(status);
	prism2sta_ev_tx(wlandev, status);
failed:
	DBFEXIT;
	return;
}

/*----------------------------------------------------------------
* hfa384x_int_rx
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
*	tasklet
----------------------------------------------------------------*/
static void hfa384x_int_rx(wlandevice_t *wlandev)
{
	hfa384x_t		*hw = wlandev->priv;
	UINT16			rxfid;
	hfa384x_rx_frame_t	rxdesc;
	int			result;
	int                     hdrlen;
	UINT16                  fc;
	p80211_rxmeta_t	*rxmeta;
	struct sk_buff          *skb = NULL;
	UINT8 *datap;

	DBFENTER;

	/* Get the FID */
	rxfid = hfa384x_getreg(hw, HFA384x_RXFID);
	/* Get the descriptor (including headers) */
	result = hfa384x_copy_from_bap(hw,
			HFA384x_BAP_INT,
			rxfid,
			0,
			&rxdesc,
			sizeof(rxdesc));
	if ( result ) {
		WLAN_LOG_DEBUG(1,
			"copy_from_bap(0x%04x, %d, %d) failed, result=0x%x\n",
			rxfid,
			0,
			sizeof(rxdesc),
			result);
		goto done;
	}

	/* Byte order convert once up front. */
	rxdesc.status =	hfa384x2host_16(rxdesc.status);
	rxdesc.time =	hfa384x2host_32(rxdesc.time);

	/* drop errors and whatnot in promisc mode */
	if (( wlandev->netdev->flags & IFF_PROMISC ) &&
	    (HFA384x_RXSTATUS_ISFCSERR(rxdesc.status) ||
	     HFA384x_RXSTATUS_ISUNDECR(rxdesc.status)))
	  goto done;

	/* Now handle frame based on port# */
	switch( HFA384x_RXSTATUS_MACPORT_GET(rxdesc.status) )
	{
	case 0:

		fc = ieee2host16(rxdesc.frame_control);

		/* If exclude and we receive an unencrypted, drop it */
		if ( (wlandev->hostwep & HOSTWEP_EXCLUDEUNENCRYPTED) &&
		     !WLAN_GET_FC_ISWEP(fc)) {
			goto done;
		}

		hdrlen = p80211_headerlen(fc);

		/* Allocate the buffer, note CRC (aka FCS). pballoc */
		/* assumes there needs to be space for one */
		skb = dev_alloc_skb(hfa384x2host_16(rxdesc.data_len) + hdrlen + WLAN_CRC_LEN + 2); /* a little extra */

		if ( ! skb ) {
			WLAN_LOG_ERROR("alloc_skb failed.\n");
			goto done;
                }

		skb->dev = wlandev->netdev;

		/* theoretically align the IP header on a 32-bit word. */
		if ( hdrlen == WLAN_HDR_A4_LEN )
			skb_reserve(skb, 2);

		/* Copy the 802.11 hdr to the buffer */
		datap = skb_put(skb, WLAN_HDR_A3_LEN);
		memcpy(datap, &rxdesc.frame_control, WLAN_HDR_A3_LEN);

		/* Snag the A4 address if present */
		if (hdrlen == WLAN_HDR_A4_LEN) {
			datap = skb_put(skb, WLAN_ADDR_LEN);
			memcpy(datap, &rxdesc.address4, WLAN_HDR_A3_LEN);
		}

		/* we can convert the data_len as we passed the original on */
		rxdesc.data_len = hfa384x2host_16(rxdesc.data_len);

		/* Copy the payload data to the buffer */
		if ( rxdesc.data_len > 0 ) {
			datap = skb_put(skb, rxdesc.data_len);
			result = hfa384x_copy_from_bap(hw,
				HFA384x_BAP_INT, rxfid, HFA384x_RX_DATA_OFF,
				datap, rxdesc.data_len);
			if ( result ) {
				WLAN_LOG_DEBUG(1,
					"copy_from_bap(0x%04x, %d, %d) failed, result=0x%x\n",
					rxfid,
					HFA384x_RX_DATA_OFF,
					rxdesc.data_len,
					result);
				goto failed;
			}
		}
		/* the prism2 cards don't return the FCS */
		datap = skb_put(skb, WLAN_CRC_LEN);
		memset (datap, 0xff, WLAN_CRC_LEN);
		skb_reset_mac_header(skb);

		/* Attach the rxmeta, set some stuff */
		p80211skb_rxmeta_attach(wlandev, skb);
		rxmeta = P80211SKB_RXMETA(skb);
		rxmeta->mactime = rxdesc.time;
		rxmeta->rxrate = rxdesc.rate;
		rxmeta->signal = rxdesc.signal - hw->dbmadjust;
		rxmeta->noise = rxdesc.silence - hw->dbmadjust;

		prism2sta_ev_rx(wlandev, skb);
		goto done;
	case 7:

        	if ( ! HFA384x_RXSTATUS_ISFCSERR(rxdesc.status) ) {
                        hfa384x_int_rxmonitor( wlandev, rxfid, &rxdesc);
                } else {
                        WLAN_LOG_DEBUG(3,"Received monitor frame: FCSerr set\n");
                }
		goto done;

	default:

		WLAN_LOG_WARNING("Received frame on unsupported port=%d\n",
			HFA384x_RXSTATUS_MACPORT_GET(rxdesc.status) );
		goto done;
	}

 failed:
	dev_kfree_skb(skb);

 done:
	DBFEXIT;
	return;
}


/*----------------------------------------------------------------
* hfa384x_int_rxmonitor
*
* Helper function for int_rx.  Handles monitor frames.
* Note that this function allocates space for the FCS and sets it
* to 0xffffffff.  The hfa384x doesn't give us the FCS value but the
* higher layers expect it.  0xffffffff is used as a flag to indicate
* the FCS is bogus.
*
* Arguments:
*	wlandev		wlan device structure
*	rxfid		received FID
*	rxdesc		rx descriptor read from card in int_rx
*
* Returns:
*	nothing
*
* Side effects:
*	Allocates an skb and passes it up via the PF_PACKET interface.
* Call context:
*	interrupt
----------------------------------------------------------------*/
static void hfa384x_int_rxmonitor( wlandevice_t *wlandev, UINT16 rxfid,
				   hfa384x_rx_frame_t *rxdesc)
{
	hfa384x_t			*hw = wlandev->priv;
	UINT				hdrlen = 0;
	UINT				datalen = 0;
	UINT				skblen = 0;
	UINT				truncated = 0;
	UINT8				*datap;
	UINT16				fc;
	struct sk_buff			*skb;

	DBFENTER;
	/* Don't forget the status, time, and data_len fields are in host order */
	/* Figure out how big the frame is */
	fc = ieee2host16(rxdesc->frame_control);
	hdrlen = p80211_headerlen(fc);
	datalen = hfa384x2host_16(rxdesc->data_len);

	/* Allocate an ind message+framesize skb */
	skblen = sizeof(p80211msg_lnxind_wlansniffrm_t) +
		hdrlen + datalen + WLAN_CRC_LEN;

	/* sanity check the length */
	if ( skblen >
		(sizeof(p80211msg_lnxind_wlansniffrm_t) +
		WLAN_HDR_A4_LEN + WLAN_DATA_MAXLEN + WLAN_CRC_LEN) ) {
		WLAN_LOG_DEBUG(1, "overlen frm: len=%d\n",
			skblen - sizeof(p80211msg_lnxind_wlansniffrm_t));
	}

	if ( (skb = dev_alloc_skb(skblen)) == NULL ) {
		WLAN_LOG_ERROR("alloc_skb failed trying to allocate %d bytes\n", skblen);
		return;
	}

	/* only prepend the prism header if in the right mode */
	if ((wlandev->netdev->type == ARPHRD_IEEE80211_PRISM) &&
	    (hw->sniffhdr == 0)) {
		p80211msg_lnxind_wlansniffrm_t	*msg;
		datap = skb_put(skb, sizeof(p80211msg_lnxind_wlansniffrm_t));
		msg = (p80211msg_lnxind_wlansniffrm_t*) datap;

		/* Initialize the message members */
		msg->msgcode = DIDmsg_lnxind_wlansniffrm;
		msg->msglen = sizeof(p80211msg_lnxind_wlansniffrm_t);
		strcpy(msg->devname, wlandev->name);

		msg->hosttime.did = DIDmsg_lnxind_wlansniffrm_hosttime;
		msg->hosttime.status = 0;
		msg->hosttime.len = 4;
		msg->hosttime.data = jiffies;

		msg->mactime.did = DIDmsg_lnxind_wlansniffrm_mactime;
		msg->mactime.status = 0;
		msg->mactime.len = 4;
		msg->mactime.data = rxdesc->time * 1000;

		msg->channel.did = DIDmsg_lnxind_wlansniffrm_channel;
		msg->channel.status = 0;
		msg->channel.len = 4;
		msg->channel.data = hw->sniff_channel;

		msg->rssi.did = DIDmsg_lnxind_wlansniffrm_rssi;
		msg->rssi.status = P80211ENUM_msgitem_status_no_value;
		msg->rssi.len = 4;
		msg->rssi.data = 0;

		msg->sq.did = DIDmsg_lnxind_wlansniffrm_sq;
		msg->sq.status = P80211ENUM_msgitem_status_no_value;
		msg->sq.len = 4;
		msg->sq.data = 0;

		msg->signal.did = DIDmsg_lnxind_wlansniffrm_signal;
		msg->signal.status = 0;
		msg->signal.len = 4;
		msg->signal.data = rxdesc->signal;

		msg->noise.did = DIDmsg_lnxind_wlansniffrm_noise;
		msg->noise.status = 0;
		msg->noise.len = 4;
		msg->noise.data = rxdesc->silence;

		msg->rate.did = DIDmsg_lnxind_wlansniffrm_rate;
		msg->rate.status = 0;
		msg->rate.len = 4;
		msg->rate.data = rxdesc->rate / 5; /* set to 802.11 units */

		msg->istx.did = DIDmsg_lnxind_wlansniffrm_istx;
		msg->istx.status = 0;
		msg->istx.len = 4;
		msg->istx.data = P80211ENUM_truth_false;

		msg->frmlen.did = DIDmsg_lnxind_wlansniffrm_frmlen;
		msg->frmlen.status = 0;
		msg->frmlen.len = 4;
		msg->frmlen.data = hdrlen + datalen + WLAN_CRC_LEN;
	} else if ((wlandev->netdev->type == ARPHRD_IEEE80211_PRISM) &&
		   (hw->sniffhdr != 0)) {
		p80211_caphdr_t		*caphdr;
		/* The NEW header format! */
		datap = skb_put(skb, sizeof(p80211_caphdr_t));
		caphdr = (p80211_caphdr_t*) datap;

		caphdr->version =	htonl(P80211CAPTURE_VERSION);
		caphdr->length =	htonl(sizeof(p80211_caphdr_t));
		caphdr->mactime =	__cpu_to_be64(rxdesc->time);
		caphdr->hosttime =	__cpu_to_be64(jiffies);
		caphdr->phytype =	htonl(4); /* dss_dot11_b */
		caphdr->channel =	htonl(hw->sniff_channel);
		caphdr->datarate =	htonl(rxdesc->rate);
		caphdr->antenna =	htonl(0); /* unknown */
		caphdr->priority =	htonl(0); /* unknown */
		caphdr->ssi_type =	htonl(3); /* rssi_raw */
		caphdr->ssi_signal =	htonl(rxdesc->signal);
		caphdr->ssi_noise =	htonl(rxdesc->silence);
		caphdr->preamble =	htonl(0); /* unknown */
		caphdr->encoding =	htonl(1); /* cck */
	}
	/* Copy the 802.11 header to the skb (ctl frames may be less than a full header) */
	datap = skb_put(skb, hdrlen);
	memcpy( datap, &(rxdesc->frame_control), hdrlen);

	/* If any, copy the data from the card to the skb */
	if ( datalen > 0 )
	{
		/* Truncate the packet if the user wants us to */
		UINT	dataread = datalen;
		if(hw->sniff_truncate > 0 && dataread > hw->sniff_truncate) {
			dataread = hw->sniff_truncate;
			truncated = 1;
		}

		datap = skb_put(skb, dataread);
		hfa384x_copy_from_bap(hw,
			HFA384x_BAP_INT, rxfid, HFA384x_RX_DATA_OFF,
			datap, dataread);

		/* check for unencrypted stuff if WEP bit set. */
		if (*(datap - hdrlen + 1) & 0x40) // wep set
		  if ((*(datap) == 0xaa) && (*(datap+1) == 0xaa))
		    *(datap - hdrlen + 1) &= 0xbf; // clear wep; it's the 802.2 header!
	}

	if (!truncated && hw->sniff_fcs) {
		/* Set the FCS */
		datap = skb_put(skb, WLAN_CRC_LEN);
		memset( datap, 0xff, WLAN_CRC_LEN);
	}

	/* pass it back up */
	prism2sta_ev_rx(wlandev, skb);

	DBFEXIT;
	return;
}

/*----------------------------------------------------------------
* hfa384x_int_alloc
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
static void hfa384x_int_alloc(wlandevice_t *wlandev)
{
	hfa384x_t		*hw = wlandev->priv;
	UINT16			fid;
	INT16			result;

	DBFENTER;

	/* Handle the reclaimed FID */
	/*   collect the FID and push it onto the stack */
	fid = hfa384x_getreg(hw, HFA384x_ALLOCFID);

	if ( fid != hw->infofid ) { /* It's a transmit fid */
		WLAN_LOG_DEBUG(5, "int_alloc(%#x)\n", fid);
		result = txfid_queue_add(hw, fid);
		if (result != -1) {
			prism2sta_ev_alloc(wlandev);
			WLAN_LOG_DEBUG(5, "q_add.\n");
		} else {
			WLAN_LOG_DEBUG(5, "q_full.\n");
		}
	} else {
		/* unlock the info fid */
		up(&hw->infofid_sem);
	}

	DBFEXIT;
	return;
}


/*----------------------------------------------------------------
* hfa384x_drvr_handover
*
* Sends a handover notification to the MAC.
*
* Arguments:
*	hw		device structure
*	addr		address of station that's left
*
* Returns:
*	zero		success.
*	-ERESTARTSYS	received signal while waiting for semaphore.
*	-EIO		failed to write to bap, or failed in cmd.
*
* Side effects:
*
* Call context:
*	process thread, NOTE: this call may block on a semaphore!
----------------------------------------------------------------*/
int hfa384x_drvr_handover( hfa384x_t *hw, UINT8 *addr)
{
	int			result = 0;
        hfa384x_HandoverAddr_t  rec;
        UINT                    len;
        DBFENTER;

	/* Acquire the infofid */
	if ( down_interruptible(&hw->infofid_sem) ) {
		result = -ERESTARTSYS;
		goto failed;
	}

        /* Set up the record */
        len = sizeof(hfa384x_HandoverAddr_t);
        rec.framelen = host2hfa384x_16(len/2 - 1);
        rec.infotype = host2hfa384x_16(HFA384x_IT_HANDOVERADDR);
        memcpy(rec.handover_addr, addr, sizeof(rec.handover_addr));

        /* Issue the command */
        result = hfa384x_cmd_notify(hw, 1, hw->infofid, &rec, len);

        if ( result != 0 ) {
                WLAN_LOG_DEBUG(1,"cmd_notify(%04x) failed, result=%d",
                        hw->infofid, result);
		result = -EIO;
                goto failed;
        }

failed:
	DBFEXIT;
	return result;
}

void hfa384x_tx_timeout(wlandevice_t *wlandev)
{
	DBFENTER;

	WLAN_LOG_WARNING("Implement me.\n");

	DBFEXIT;
}

/* Handles all "rx" BAP operations */
static void     hfa384x_bap_tasklet(unsigned long data)
{
	hfa384x_t *hw = (hfa384x_t *) data;
	wlandevice_t *wlandev = hw->wlandev;
	int counter = prism2_irq_evread_max;
	int			reg;

	DBFENTER;

	while (counter-- > 0) {
		/* Get interrupt register */
		reg = hfa384x_getreg(hw, HFA384x_EVSTAT);

		if ((reg == 0xffff) ||
		    !(reg & HFA384x_INT_BAP_OP)) {
			break;
		}

		if ( HFA384x_EVSTAT_ISINFO(reg) ){
			hfa384x_int_info(wlandev);
			hfa384x_setreg(hw, HFA384x_EVACK_INFO_SET(1),
				HFA384x_EVACK);
		}
		if ( HFA384x_EVSTAT_ISTXEXC(reg) ){
			hfa384x_int_txexc(wlandev);
			hfa384x_setreg(hw, HFA384x_EVACK_TXEXC_SET(1),
				HFA384x_EVACK);
		}
		if ( HFA384x_EVSTAT_ISTX(reg) ){
			hfa384x_int_tx(wlandev);
			hfa384x_setreg(hw, HFA384x_EVACK_TX_SET(1),
				HFA384x_EVACK);
		}
		if ( HFA384x_EVSTAT_ISRX(reg) ){
			hfa384x_int_rx(wlandev);
			hfa384x_setreg(hw, HFA384x_EVACK_RX_SET(1),
				HFA384x_EVACK);
		}
	}

	/* re-enable interrupts */
	hfa384x_events_all(hw);

	DBFEXIT;
}
