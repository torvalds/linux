/* ced_ioc.c
 ioctl part of the 1401 usb device driver for linux.
 Copyright (C) 2010 Cambridge Electronic Design Ltd
 Author Greg P Smith (greg@ced.co.uk)

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/page-flags.h>
#include <linux/pagemap.h>
#include <linux/jiffies.h>

#include "usb1401.h"

/****************************************************************************
** ced_flush_out_buff
**
** Empties the Output buffer and sets int lines. Used from user level only
****************************************************************************/
static void ced_flush_out_buff(DEVICE_EXTENSION *pdx)
{
	dev_dbg(&pdx->interface->dev, "%s: currentState=%d\n",
		__func__, pdx->sCurrentState);
	if (pdx->sCurrentState == U14ERR_TIME)	/* Do nothing if hardware in trouble */
		return;
	/* Kill off any pending I/O */
	/* CharSend_Cancel(pdx);  */
	spin_lock_irq(&pdx->charOutLock);
	pdx->dwNumOutput = 0;
	pdx->dwOutBuffGet = 0;
	pdx->dwOutBuffPut = 0;
	spin_unlock_irq(&pdx->charOutLock);
}

/****************************************************************************
**
** ced_flush_in_buff
**
** Empties the input buffer and sets int lines
****************************************************************************/
static void ced_flush_in_buff(DEVICE_EXTENSION *pdx)
{
	dev_dbg(&pdx->interface->dev, "%s: currentState=%d\n",
		__func__, pdx->sCurrentState);
	if (pdx->sCurrentState == U14ERR_TIME)	/* Do nothing if hardware in trouble */
		return;
	/* Kill off any pending I/O */
	/*     CharRead_Cancel(pDevObject);  */
	spin_lock_irq(&pdx->charInLock);
	pdx->dwNumInput = 0;
	pdx->dwInBuffGet = 0;
	pdx->dwInBuffPut = 0;
	spin_unlock_irq(&pdx->charInLock);
}

/****************************************************************************
** ced_put_chars
**
** Utility routine to copy chars into the output buffer and fire them off.
** called from user mode, holds charOutLock.
****************************************************************************/
static int ced_put_chars(DEVICE_EXTENSION *pdx, const char *pCh,
		    unsigned int uCount)
{
	int iReturn;
	spin_lock_irq(&pdx->charOutLock);	/*  get the output spin lock */
	if ((OUTBUF_SZ - pdx->dwNumOutput) >= uCount) {
		unsigned int u;
		for (u = 0; u < uCount; u++) {
			pdx->outputBuffer[pdx->dwOutBuffPut++] = pCh[u];
			if (pdx->dwOutBuffPut >= OUTBUF_SZ)
				pdx->dwOutBuffPut = 0;
		}
		pdx->dwNumOutput += uCount;
		spin_unlock_irq(&pdx->charOutLock);
		iReturn = ced_send_chars(pdx);	/*  ...give a chance to transmit data */
	} else {
		iReturn = U14ERR_NOOUT;	/*  no room at the out (ha-ha) */
		spin_unlock_irq(&pdx->charOutLock);
	}
	return iReturn;
}

/*****************************************************************************
** Add the data in pData (local pointer) of length n to the output buffer, and
** trigger an output transfer if this is appropriate. User mode.
** Holds the io_mutex
*****************************************************************************/
int ced_send_string(DEVICE_EXTENSION *pdx, const char __user *pData,
	       unsigned int n)
{
	int iReturn = U14ERR_NOERROR;	/*  assume all will be well */
	char buffer[OUTBUF_SZ + 1];	/*  space in our address space for characters */
	if (n > OUTBUF_SZ)	/*  check space in local buffer... */
		return U14ERR_NOOUT;	/*  ...too many characters */
	if (copy_from_user(buffer, pData, n))
		return -EFAULT;
	buffer[n] = 0;		/*  terminate for debug purposes */

	mutex_lock(&pdx->io_mutex);	/*  Protect disconnect from new i/o */
	if (n > 0) {		/*  do nothing if nowt to do! */
		dev_dbg(&pdx->interface->dev, "%s: n=%d>%s<\n",
			__func__, n, buffer);
		iReturn = ced_put_chars(pdx, buffer, n);
	}

	ced_allowi(pdx);		/*  make sure we have input int */
	mutex_unlock(&pdx->io_mutex);

	return iReturn;
}

/****************************************************************************
** ced_send_char
**
** Sends a single character to the 1401. User mode, holds io_mutex.
****************************************************************************/
int ced_send_char(DEVICE_EXTENSION *pdx, char c)
{
	int iReturn;
	mutex_lock(&pdx->io_mutex);	/*  Protect disconnect from new i/o */
	iReturn = ced_put_chars(pdx, &c, 1);
	dev_dbg(&pdx->interface->dev, "ced_send_char >%c< (0x%02x)\n", c, c);
	ced_allowi(pdx);	/*  Make sure char reads are running */
	mutex_unlock(&pdx->io_mutex);
	return iReturn;
}

/***************************************************************************
**
** ced_get_state
**
**  Retrieves state information from the 1401, adjusts the 1401 state held
**  in the device extension to indicate the current 1401 type.
**
**  *state is updated with information about the 1401 state as returned by the
**         1401. The low byte is a code for what 1401 is doing:
**
**  0       normal 1401 operation
**  1       sending chars to host
**  2       sending block data to host
**  3       reading block data from host
**  4       sending an escape sequence to the host
**  0x80    1401 is executing self-test, in which case the upper word
**          is the last error code seen (or zero for no new error).
**
** *error is updated with error information if a self-test error code
**          is returned in the upper word of state.
**
**  both state and error are set to -1 if there are comms problems, and
**  to zero if there is a simple failure.
**
** return error code (U14ERR_NOERROR for OK)
*/
int ced_get_state(DEVICE_EXTENSION *pdx, __u32 *state, __u32 *error)
{
	int nGot;
	dev_dbg(&pdx->interface->dev, "%s: entry\n", __func__);

	*state = 0xFFFFFFFF;	/*  Start off with invalid state */
	nGot = usb_control_msg(pdx->udev, usb_rcvctrlpipe(pdx->udev, 0),
			       GET_STATUS, (D_TO_H | VENDOR | DEVREQ), 0, 0,
			       pdx->statBuf, sizeof(pdx->statBuf), HZ);
	if (nGot != sizeof(pdx->statBuf)) {
		dev_err(&pdx->interface->dev,
			"%s: FAILED, return code %d\n", __func__, nGot);
		pdx->sCurrentState = U14ERR_TIME;	/*  Indicate that things are very wrong indeed */
		*state = 0;	/*  Force status values to a known state */
		*error = 0;
	} else {
		int nDevice;
		dev_dbg(&pdx->interface->dev,
			"%s: Success, state: 0x%x, 0x%x\n",
			__func__, pdx->statBuf[0], pdx->statBuf[1]);

		*state = pdx->statBuf[0];	/*  Return the state values to the calling code */
		*error = pdx->statBuf[1];

		nDevice = pdx->udev->descriptor.bcdDevice >> 8;	/*  1401 type code value */
		switch (nDevice) {	/*  so we can clean up current state */
		case 0:
			pdx->sCurrentState = U14ERR_U1401;
			break;

		default:	/*  allow lots of device codes for future 1401s */
			if ((nDevice >= 1) && (nDevice <= 23))
				pdx->sCurrentState = (short)(nDevice + 6);
			else
				pdx->sCurrentState = U14ERR_ILL;
			break;
		}
	}

	return pdx->sCurrentState >= 0 ? U14ERR_NOERROR : pdx->sCurrentState;
}

/****************************************************************************
** ced_read_write_cancel
**
** Kills off staged read\write request from the USB if one is pending.
****************************************************************************/
int ced_read_write_cancel(DEVICE_EXTENSION *pdx)
{
	dev_dbg(&pdx->interface->dev, "%s: entry %d\n",
		__func__, pdx->bStagedUrbPending);
#ifdef NOT_WRITTEN_YET
	int ntStatus = STATUS_SUCCESS;
	bool bResult = false;
	unsigned int i;
	/*  We can fill this in when we know how we will implement the staged transfer stuff */
	spin_lock_irq(&pdx->stagedLock);

	if (pdx->bStagedUrbPending) {	/*  anything to be cancelled? May need more... */
		dev_info(&pdx->interface - dev,
			 "ced_read_write_cancel about to cancel Urb\n");
		/* Clear the staging done flag */
		/* KeClearEvent(&pdx->StagingDoneEvent); */
		USB_ASSERT(pdx->pStagedIrp != NULL);

		/*  Release the spinlock first otherwise the completion routine may hang */
		/*   on the spinlock while this function hands waiting for the event. */
		spin_unlock_irq(&pdx->stagedLock);
		bResult = IoCancelIrp(pdx->pStagedIrp);	/*  Actually do the cancel */
		if (bResult) {
			LARGE_INTEGER timeout;
			timeout.QuadPart = -10000000;	/*  Use a timeout of 1 second */
			dev_info(&pdx->interface - dev,
				 "%s: about to wait till done\n", __func__);
			ntStatus =
			    KeWaitForSingleObject(&pdx->StagingDoneEvent,
						  Executive, KernelMode, FALSE,
						  &timeout);
		} else {
			dev_info(&pdx->interface - dev,
				 "%s: cancellation failed\n", __func__);
			ntStatus = U14ERR_FAIL;
		}
		USB_KdPrint(DBGLVL_DEFAULT,
			    ("ced_read_write_cancel ntStatus = 0x%x decimal %d\n",
			     ntStatus, ntStatus));
	} else
		spin_unlock_irq(&pdx->stagedLock);

	dev_info(&pdx->interface - dev, "%s: done\n", __func__);
	return ntStatus;
#else
	return U14ERR_NOERROR;
#endif

}

/***************************************************************************
** ced_in_self_test - utility to check in self test. Return 1 for ST, 0 for not or
** a -ve error code if we failed for some reason.
***************************************************************************/
static int ced_in_self_test(DEVICE_EXTENSION *pdx, unsigned int *pState)
{
	unsigned int state, error;
	int iReturn = ced_get_state(pdx, &state, &error);	/*  see if in self-test */
	if (iReturn == U14ERR_NOERROR)	/*  if all still OK */
		iReturn = (state == (unsigned int)-1) ||	/*  TX problem or... */
		    ((state & 0xff) == 0x80);	/*  ...self test */
	*pState = state;	/*  return actual state */
	return iReturn;
}

/***************************************************************************
** ced_is_1401 - ALWAYS CALLED HOLDING THE io_mutex
**
** Tests for the current state of the 1401. Sets sCurrentState:
**
**  U14ERR_NOIF  1401  i/f card not installed (not done here)
**  U14ERR_OFF   1401  apparently not switched on
**  U14ERR_NC    1401  appears to be not connected
**  U14ERR_ILL   1401  if it is there its not very well at all
**  U14ERR_TIME  1401  appears OK, but doesn't communicate - very bad
**  U14ERR_STD   1401  OK and ready for use
**  U14ERR_PLUS  1401+ OK and ready for use
**  U14ERR_U1401 Micro1401 OK and ready for use
**  U14ERR_POWER Power1401 OK and ready for use
**  U14ERR_U14012 Micro1401 mkII OK and ready for use
**
**  Returns TRUE if a 1401 detected and OK, else FALSE
****************************************************************************/
static bool ced_is_1401(DEVICE_EXTENSION *pdx)
{
	int iReturn;
	dev_dbg(&pdx->interface->dev, "%s\n", __func__);

	ced_draw_down(pdx);	/*  wait for, then kill outstanding Urbs */
	ced_flush_in_buff(pdx);	/*  Clear out input buffer & pipe */
	ced_flush_out_buff(pdx);	/*  Clear output buffer & pipe */

	/*  The next call returns 0 if OK, but has returned 1 in the past, meaning that */
	/*  usb_unlock_device() is needed... now it always is */
	iReturn = usb_lock_device_for_reset(pdx->udev, pdx->interface);

	/*  release the io_mutex because if we don't, we will deadlock due to system */
	/*  calls back into the driver. */
	mutex_unlock(&pdx->io_mutex);	/*  locked, so we will not get system calls */
	if (iReturn >= 0) {	/*  if we failed */
		iReturn = usb_reset_device(pdx->udev);	/*  try to do the reset */
		usb_unlock_device(pdx->udev);	/*  undo the lock */
	}

	mutex_lock(&pdx->io_mutex);	/*  hold stuff off while we wait */
	pdx->dwDMAFlag = MODE_CHAR;	/*  Clear DMA mode flag regardless! */
	if (iReturn == 0) {	/*  if all is OK still */
		unsigned int state;
		iReturn = ced_in_self_test(pdx, &state);	/*  see if likely in self test */
		if (iReturn > 0) {	/*  do we need to wait for self-test? */
			unsigned long ulTimeOut = jiffies + 30 * HZ;	/*  when to give up */
			while ((iReturn > 0) && time_before(jiffies, ulTimeOut)) {
				schedule();	/*  let other stuff run */
				iReturn = ced_in_self_test(pdx, &state);	/*  see if done yet */
			}
		}

		if (iReturn == 0)	/*  if all is OK... */
			iReturn = state == 0;	/*  then success is that the state is 0 */
	} else
		iReturn = 0;	/*  we failed */
	pdx->bForceReset = false;	/*  Clear forced reset flag now */

	return iReturn > 0;
}

/****************************************************************************
** ced_quick_check  - ALWAYS CALLED HOLDING THE io_mutex
** This is used to test for a 1401. It will try to do a quick check if all is
**  OK, that is the 1401 was OK the last time it was asked, and there is no DMA
**  in progress, and if the bTestBuff flag is set, the character buffers must be
**  empty too. If the quick check shows that the state is still the same, then
**  all is OK.
**
** If any of the above conditions are not met, or if the state or type of the
**  1401 has changed since the previous test, the full ced_is_1401 test is done, but
**  only if bCanReset is also TRUE.
**
** The return value is TRUE if a useable 1401 is found, FALSE if not
*/
static bool ced_quick_check(DEVICE_EXTENSION *pdx, bool bTestBuff, bool bCanReset)
{
	bool bRet = false;	/*  assume it will fail and we will reset */
	bool bShortTest;

	bShortTest = ((pdx->dwDMAFlag == MODE_CHAR) &&	/*  no DMA running */
		      (!pdx->bForceReset) &&	/*  Not had a real reset forced */
		      (pdx->sCurrentState >= U14ERR_STD));	/*  No 1401 errors stored */

	dev_dbg(&pdx->interface->dev,
		"%s: DMAFlag:%d, state:%d, force:%d, testBuff:%d, short:%d\n",
		__func__, pdx->dwDMAFlag, pdx->sCurrentState, pdx->bForceReset,
		bTestBuff, bShortTest);

	if ((bTestBuff) &&	/*  Buffer check requested, and... */
	    (pdx->dwNumInput || pdx->dwNumOutput)) {	/*  ...characters were in the buffer? */
		bShortTest = false;	/*  Then do the full test */
		dev_dbg(&pdx->interface->dev,
			"%s: will reset as buffers not empty\n", __func__);
	}

	if (bShortTest || !bCanReset) {	/*  Still OK to try the short test? */
				/*  Always test if no reset - we want state update */
		unsigned int state, error;
		dev_dbg(&pdx->interface->dev, "%s: ced_get_state\n", __func__);
		if (ced_get_state(pdx, &state, &error) == U14ERR_NOERROR) {	/*  Check on the 1401 state */
			if ((state & 0xFF) == 0)	/*  If call worked, check the status value */
				bRet = true;	/*  If that was zero, all is OK, no reset needed */
		}
	}

	if (!bRet && bCanReset)	{ /*  If all not OK, then */
		dev_info(&pdx->interface->dev, "%s: ced_is_1401 %d %d %d %d\n",
			 __func__, bShortTest, pdx->sCurrentState, bTestBuff,
			 pdx->bForceReset);
		bRet = ced_is_1401(pdx);	/*   do full test */
	}

	return bRet;
}

/****************************************************************************
** ced_reset
**
** Resets the 1401 and empties the i/o buffers
*****************************************************************************/
int ced_reset(DEVICE_EXTENSION *pdx)
{
	mutex_lock(&pdx->io_mutex);	/*  Protect disconnect from new i/o */
	dev_dbg(&pdx->interface->dev, "%s: About to call ced_quick_check\n",
		__func__);
	ced_quick_check(pdx, true, true);	/*  Check 1401, reset if not OK */
	mutex_unlock(&pdx->io_mutex);
	return U14ERR_NOERROR;
}

/****************************************************************************
** ced_get_char
**
** Gets a single character from the 1401
****************************************************************************/
int ced_get_char(DEVICE_EXTENSION *pdx)
{
	int iReturn = U14ERR_NOIN;	/*  assume we will get  nothing */
	mutex_lock(&pdx->io_mutex);	/*  Protect disconnect from new i/o */

	dev_dbg(&pdx->interface->dev, "%s\n", __func__);

	ced_allowi(pdx);	/*  Make sure char reads are running */
	ced_send_chars(pdx);	/*  and send any buffered chars */

	spin_lock_irq(&pdx->charInLock);
	if (pdx->dwNumInput > 0) {	/*  worth looking */
		iReturn = pdx->inputBuffer[pdx->dwInBuffGet++];
		if (pdx->dwInBuffGet >= INBUF_SZ)
			pdx->dwInBuffGet = 0;
		pdx->dwNumInput--;
	} else
		iReturn = U14ERR_NOIN;	/*  no input data to read */
	spin_unlock_irq(&pdx->charInLock);

	ced_allowi(pdx);	/*  Make sure char reads are running */

	mutex_unlock(&pdx->io_mutex);	/*  Protect disconnect from new i/o */
	return iReturn;
}

/****************************************************************************
** ced_get_string
**
** Gets a string from the 1401. Returns chars up to the next CR or when
** there are no more to read or nowhere to put them. CR is translated to
** 0 and counted as a character. If the string does not end in a 0, we will
** add one, if there is room, but it is not counted as a character.
**
** returns the count of characters (including the terminator, or 0 if none
** or a negative error code.
****************************************************************************/
int ced_get_string(DEVICE_EXTENSION *pdx, char __user *pUser, int n)
{
	int nAvailable;		/*  character in the buffer */
	int iReturn = U14ERR_NOIN;
	if (n <= 0)
		return -ENOMEM;

	mutex_lock(&pdx->io_mutex);	/*  Protect disconnect from new i/o */
	ced_allowi(pdx);	/*  Make sure char reads are running */
	ced_send_chars(pdx);		/*  and send any buffered chars */

	spin_lock_irq(&pdx->charInLock);
	nAvailable = pdx->dwNumInput;	/*  characters available now */
	if (nAvailable > n)	/*  read max of space in pUser... */
		nAvailable = n;	/*  ...or input characters */

	if (nAvailable > 0) {	/*  worth looking? */
		char buffer[INBUF_SZ + 1];	/*  space for a linear copy of data */
		int nGot = 0;
		int nCopyToUser;	/*  number to copy to user */
		char cData;
		do {
			cData = pdx->inputBuffer[pdx->dwInBuffGet++];
			if (cData == CR_CHAR)	/*  replace CR with zero */
				cData = (char)0;

			if (pdx->dwInBuffGet >= INBUF_SZ)
				pdx->dwInBuffGet = 0;	/*  wrap buffer pointer */

			buffer[nGot++] = cData;	/*  save the output */
		} while ((nGot < nAvailable) && cData);

		nCopyToUser = nGot;	/*  what to copy... */
		if (cData) {	/*  do we need null */
			buffer[nGot] = (char)0;	/*  make it tidy */
			if (nGot < n)	/*  if space in user buffer... */
				++nCopyToUser;	/*  ...copy the 0 as well. */
		}

		pdx->dwNumInput -= nGot;
		spin_unlock_irq(&pdx->charInLock);

		dev_dbg(&pdx->interface->dev, "%s: read %d characters >%s<\n",
			__func__, nGot, buffer);
		if (copy_to_user(pUser, buffer, nCopyToUser))
			iReturn = -EFAULT;
		else
			iReturn = nGot;		/*  report characters read */
	} else
		spin_unlock_irq(&pdx->charInLock);

	ced_allowi(pdx);	/*  Make sure char reads are running */
	mutex_unlock(&pdx->io_mutex);	/*  Protect disconnect from new i/o */

	return iReturn;
}

/*******************************************************************************
** Get count of characters in the inout buffer.
*******************************************************************************/
int ced_stat_1401(DEVICE_EXTENSION *pdx)
{
	int iReturn;
	mutex_lock(&pdx->io_mutex);	/*  Protect disconnect from new i/o */
	ced_allowi(pdx);		/*  make sure we allow pending chars */
	ced_send_chars(pdx);		/*  in both directions */
	iReturn = pdx->dwNumInput;	/*  no lock as single read */
	mutex_unlock(&pdx->io_mutex);	/*  Protect disconnect from new i/o */
	return iReturn;
}

/****************************************************************************
** ced_line_count
**
** Returns the number of newline chars in the buffer. There is no need for
** any fancy interlocks as we only read the interrupt routine data, and the
** system is arranged so nothing can be destroyed.
****************************************************************************/
int ced_line_count(DEVICE_EXTENSION *pdx)
{
	int iReturn = 0;	/*  will be count of line ends */

	mutex_lock(&pdx->io_mutex);	/*  Protect disconnect from new i/o */
	ced_allowi(pdx);		/*  Make sure char reads are running */
	ced_send_chars(pdx);		/*  and send any buffered chars */
	spin_lock_irq(&pdx->charInLock);	/*  Get protection */

	if (pdx->dwNumInput > 0) {	/*  worth looking? */
		unsigned int dwIndex = pdx->dwInBuffGet;	/*  start at first available */
		unsigned int dwEnd = pdx->dwInBuffPut;	/*  Position for search end */
		do {
			if (pdx->inputBuffer[dwIndex++] == CR_CHAR)
				++iReturn;	/*  inc count if CR */

			if (dwIndex >= INBUF_SZ)	/*  see if we fall off buff */
				dwIndex = 0;
		} while (dwIndex != dwEnd);	/*  go to last available */
	}

	spin_unlock_irq(&pdx->charInLock);
	dev_dbg(&pdx->interface->dev, "%s: returned %d\n", __func__, iReturn);
	mutex_unlock(&pdx->io_mutex);	/*  Protect disconnect from new i/o */
	return iReturn;
}

/****************************************************************************
** ced_get_out_buf_space
**
** Gets the space in the output buffer. Called from user code.
*****************************************************************************/
int ced_get_out_buf_space(DEVICE_EXTENSION *pdx)
{
	int iReturn;
	mutex_lock(&pdx->io_mutex);	/*  Protect disconnect from new i/o */
	ced_send_chars(pdx);		/*  send any buffered chars */
	iReturn = (int)(OUTBUF_SZ - pdx->dwNumOutput);	/*  no lock needed for single read */
	dev_dbg(&pdx->interface->dev, "%s: %d\n", __func__, iReturn);
	mutex_unlock(&pdx->io_mutex);	/*  Protect disconnect from new i/o */
	return iReturn;
}

/****************************************************************************
**
** ced_clear_area
**
** Clears up a transfer area. This is always called in the context of a user
** request, never from a call-back.
****************************************************************************/
int ced_clear_area(DEVICE_EXTENSION *pdx, int nArea)
{
	int iReturn = U14ERR_NOERROR;

	if ((nArea < 0) || (nArea >= MAX_TRANSAREAS)) {
		iReturn = U14ERR_BADAREA;
		dev_err(&pdx->interface->dev, "%s: Attempt to clear area %d\n",
			__func__, nArea);
	} else {
		TRANSAREA *pTA = &pdx->rTransDef[nArea];	/*  to save typing */
		if (!pTA->bUsed)	/*  if not used... */
			iReturn = U14ERR_NOTSET;	/*  ...nothing to be done */
		else {
			/*  We must save the memory we return as we shouldn't mess with memory while */
			/*  holding a spin lock. */
			struct page **pPages = NULL; /*save page address list*/
			int nPages = 0;	/*  and number of pages */
			int np;

			dev_dbg(&pdx->interface->dev, "%s: area %d\n",
				__func__, nArea);
			spin_lock_irq(&pdx->stagedLock);
			if ((pdx->StagedId == nArea)
			    && (pdx->dwDMAFlag > MODE_CHAR)) {
				iReturn = U14ERR_UNLOCKFAIL;	/*  cannot delete as in use */
				dev_err(&pdx->interface->dev,
					"%s: call on area %d while active\n",
					__func__, nArea);
			} else {
				pPages = pTA->pPages;	/*  save page address list */
				nPages = pTA->nPages;	/*  and page count */
				if (pTA->dwEventSz)	/*  if events flagging in use */
					wake_up_interruptible(&pTA->wqEvent);	/*  release anything that was waiting */

				if (pdx->bXFerWaiting
				    && (pdx->rDMAInfo.wIdent == nArea))
					pdx->bXFerWaiting = false;	/*  Cannot have pending xfer if area cleared */

				/*  Clean out the TRANSAREA except for the wait queue, which is at the end */
				/*  This sets bUsed to false and dwEventSz to 0 to say area not used and no events. */
				memset(pTA, 0,
				       sizeof(TRANSAREA) -
				       sizeof(wait_queue_head_t));
			}
			spin_unlock_irq(&pdx->stagedLock);

			if (pPages) {	/*  if we decided to release the memory */
				/*  Now we must undo the pinning down of the pages. We will assume the worst and mark */
				/*  all the pages as dirty. Don't be tempted to move this up above as you must not be */
				/*  holding a spin lock to do this stuff as it is not atomic. */
				dev_dbg(&pdx->interface->dev, "%s: nPages=%d\n",
					__func__, nPages);

				for (np = 0; np < nPages; ++np) {
					if (pPages[np]) {
						SetPageDirty(pPages[np]);
						page_cache_release(pPages[np]);
					}
				}

				kfree(pPages);
				dev_dbg(&pdx->interface->dev,
					"%s: kfree(pPages) done\n", __func__);
			}
		}
	}

	return iReturn;
}

/****************************************************************************
** ced_set_area
**
** Sets up a transfer area - the functional part. Called by both
** ced_set_transfer and SetCircular.
****************************************************************************/
static int ced_set_area(DEVICE_EXTENSION *pdx, int nArea, char __user *puBuf,
		   unsigned int dwLength, bool bCircular, bool bCircToHost)
{
	/*  Start by working out the page aligned start of the area and the size */
	/*  of the area in pages, allowing for the start not being aligned and the */
	/*  end needing to be rounded up to a page boundary. */
	unsigned long ulStart = ((unsigned long)puBuf) & PAGE_MASK;
	unsigned int ulOffset = ((unsigned long)puBuf) & (PAGE_SIZE - 1);
	int len = (dwLength + ulOffset + PAGE_SIZE - 1) >> PAGE_SHIFT;

	TRANSAREA *pTA = &pdx->rTransDef[nArea];	/*  to save typing */
	struct page **pPages = NULL;	/*  space for page tables */
	int nPages = 0;		/*  and number of pages */

	int iReturn = ced_clear_area(pdx, nArea);	/*  see if OK to use this area */
	if ((iReturn != U14ERR_NOTSET) &&	/*  if not area unused and... */
	    (iReturn != U14ERR_NOERROR))	/*  ...not all OK, then... */
		return iReturn;	/*  ...we cannot use this area */

	if (!access_ok(VERIFY_WRITE, puBuf, dwLength))	/*  if we cannot access the memory... */
		return -EFAULT;	/*  ...then we are done */

	/*  Now allocate space to hold the page pointer and virtual address pointer tables */
	pPages = kmalloc(len * sizeof(struct page *), GFP_KERNEL);
	if (!pPages) {
		iReturn = U14ERR_NOMEMORY;
		goto error;
	}
	dev_dbg(&pdx->interface->dev, "%s: %p, length=%06x, circular %d\n",
		__func__, puBuf, dwLength, bCircular);

	/*  To pin down user pages we must first acquire the mapping semaphore. */
	nPages = get_user_pages_fast(ulStart, len, 1, pPages);
	dev_dbg(&pdx->interface->dev, "%s: nPages = %d\n", __func__, nPages);

	if (nPages > 0) {		/*  if we succeeded */
		/*  If you are tempted to use page_address (form LDD3), forget it. You MUST use */
		/*  kmap() or kmap_atomic() to get a virtual address. page_address will give you */
		/*  (null) or at least it does in this context with an x86 machine. */
		spin_lock_irq(&pdx->stagedLock);
		pTA->lpvBuff = puBuf;	/*  keep start of region (user address) */
		pTA->dwBaseOffset = ulOffset;	/*  save offset in first page to start of xfer */
		pTA->dwLength = dwLength;	/*  Size if the region in bytes */
		pTA->pPages = pPages;	/*  list of pages that are used by buffer */
		pTA->nPages = nPages;	/*  number of pages */

		pTA->bCircular = bCircular;
		pTA->bCircToHost = bCircToHost;

		pTA->aBlocks[0].dwOffset = 0;
		pTA->aBlocks[0].dwSize = 0;
		pTA->aBlocks[1].dwOffset = 0;
		pTA->aBlocks[1].dwSize = 0;
		pTA->bUsed = true;	/*  This is now a used block */

		spin_unlock_irq(&pdx->stagedLock);
		iReturn = U14ERR_NOERROR;	/*  say all was well */
	} else {
		iReturn = U14ERR_LOCKFAIL;
		goto error;
	}

	return iReturn;

error:
	kfree(pPages);
	return iReturn;
}

/****************************************************************************
** ced_set_transfer
**
** Sets up a transfer area record. If the area is already set, we attempt to
** unset it. Unsetting will fail if the area is booked, and a transfer to that
** area is in progress. Otherwise, we will release the area and re-assign it.
****************************************************************************/
int ced_set_transfer(DEVICE_EXTENSION *pdx, struct transfer_area_desc __user *pTD)
{
	int iReturn;
	struct transfer_area_desc td;

	if (copy_from_user(&td, pTD, sizeof(td)))
		return -EFAULT;

	mutex_lock(&pdx->io_mutex);
	dev_dbg(&pdx->interface->dev, "%s: area:%d, size:%08x\n",
		__func__, td.wAreaNum, td.dwLength);
	/*  The strange cast is done so that we don't get warnings in 32-bit linux about the size of the */
	/*  pointer. The pointer is always passed as a 64-bit object so that we don't have problems using */
	/*  a 32-bit program on a 64-bit system. unsigned long is 64-bits on a 64-bit system. */
	iReturn =
	    ced_set_area(pdx, td.wAreaNum,
		    (char __user *)((unsigned long)td.lpvBuff), td.dwLength,
		    false, false);
	mutex_unlock(&pdx->io_mutex);
	return iReturn;
}

/****************************************************************************
** UnSetTransfer
** Erases a transfer area record
****************************************************************************/
int ced_unset_transfer(DEVICE_EXTENSION *pdx, int nArea)
{
	int iReturn;
	mutex_lock(&pdx->io_mutex);
	iReturn = ced_clear_area(pdx, nArea);
	mutex_unlock(&pdx->io_mutex);
	return iReturn;
}

/****************************************************************************
** SetEvent
** Creates an event that we can test for based on a transfer to/from an area.
** The area must be setup for a transfer. We attempt to simulate the Windows
** driver behavior for events (as we don't actually use them), which is to
** pretend that whatever the user asked for was achieved, so we return 1 if
** try to create one, and 0 if they ask to remove (assuming all else was OK).
****************************************************************************/
int SetEvent(DEVICE_EXTENSION *pdx, struct transfer_event __user *pTE)
{
	int iReturn = U14ERR_NOERROR;
	struct transfer_event te;

	/*  get a local copy of the data */
	if (copy_from_user(&te, pTE, sizeof(te)))
		return -EFAULT;

	if (te.wAreaNum >= MAX_TRANSAREAS)	/*  the area must exist */
		return U14ERR_BADAREA;
	else {
		TRANSAREA *pTA = &pdx->rTransDef[te.wAreaNum];
		mutex_lock(&pdx->io_mutex);	/*  make sure we have no competitor */
		spin_lock_irq(&pdx->stagedLock);
		if (pTA->bUsed) {	/*  area must be in use */
			pTA->dwEventSt = te.dwStart;	/*  set area regions */
			pTA->dwEventSz = te.dwLength;	/*  set size (0 cancels it) */
			pTA->bEventToHost = te.wFlags & 1;	/*  set the direction */
			pTA->iWakeUp = 0;	/*  zero the wake up count */
		} else
			iReturn = U14ERR_NOTSET;
		spin_unlock_irq(&pdx->stagedLock);
		mutex_unlock(&pdx->io_mutex);
	}
	return iReturn ==
	    U14ERR_NOERROR ? (te.iSetEvent ? 1 : U14ERR_NOERROR) : iReturn;
}

/****************************************************************************
** WaitEvent
** Sleep the process with a timeout waiting for an event. Returns the number
** of times that a block met the event condition since we last cleared it or
** 0 if timed out, or -ve error (bad area or not set, or signal).
****************************************************************************/
int WaitEvent(DEVICE_EXTENSION *pdx, int nArea, int msTimeOut)
{
	int iReturn;
	if ((unsigned)nArea >= MAX_TRANSAREAS)
		return U14ERR_BADAREA;
	else {
		int iWait;
		TRANSAREA *pTA = &pdx->rTransDef[nArea];
		msTimeOut = (msTimeOut * HZ + 999) / 1000;	/*  convert timeout to jiffies */

		/*  We cannot wait holding the mutex, but we check the flags while holding */
		/*  it. This may well be pointless as another thread could get in between */
		/*  releasing it and the wait call. However, this would have to clear the */
		/*  iWakeUp flag. However, the !pTA-bUsed may help us in this case. */
		mutex_lock(&pdx->io_mutex);	/*  make sure we have no competitor */
		if (!pTA->bUsed || !pTA->dwEventSz)	/*  check something to wait for... */
			return U14ERR_NOTSET;	/*  ...else we do nothing */
		mutex_unlock(&pdx->io_mutex);

		if (msTimeOut)
			iWait =
			    wait_event_interruptible_timeout(pTA->wqEvent,
							     pTA->iWakeUp
							     || !pTA->bUsed,
							     msTimeOut);
		else
			iWait =
			    wait_event_interruptible(pTA->wqEvent, pTA->iWakeUp
						     || !pTA->bUsed);
		if (iWait)
			iReturn = -ERESTARTSYS;	/*  oops - we have had a SIGNAL */
		else
			iReturn = pTA->iWakeUp;	/*  else the wakeup count */

		spin_lock_irq(&pdx->stagedLock);
		pTA->iWakeUp = 0;	/*  clear the flag */
		spin_unlock_irq(&pdx->stagedLock);
	}
	return iReturn;
}

/****************************************************************************
** TestEvent
** Test the event to see if a WaitEvent would return immediately. Returns the
** number of times a block completed since the last call, or 0 if none or a
** negative error.
****************************************************************************/
int TestEvent(DEVICE_EXTENSION *pdx, int nArea)
{
	int iReturn;
	if ((unsigned)nArea >= MAX_TRANSAREAS)
		iReturn = U14ERR_BADAREA;
	else {
		TRANSAREA *pTA = &pdx->rTransDef[nArea];
		mutex_lock(&pdx->io_mutex);	/*  make sure we have no competitor */
		spin_lock_irq(&pdx->stagedLock);
		iReturn = pTA->iWakeUp;	/*  get wakeup count since last call */
		pTA->iWakeUp = 0;	/*  clear the count */
		spin_unlock_irq(&pdx->stagedLock);
		mutex_unlock(&pdx->io_mutex);
	}
	return iReturn;
}

/****************************************************************************
** GetTransferInfo
** Puts the current state of the 1401 in a TGET_TX_BLOCK.
*****************************************************************************/
int GetTransfer(DEVICE_EXTENSION *pdx, TGET_TX_BLOCK __user *pTX)
{
	int iReturn = U14ERR_NOERROR;
	unsigned int dwIdent;

	mutex_lock(&pdx->io_mutex);
	dwIdent = pdx->StagedId;	/*  area ident for last xfer */
	if (dwIdent >= MAX_TRANSAREAS)
		iReturn = U14ERR_BADAREA;
	else {
		/*  Return the best information we have - we don't have physical addresses */
		TGET_TX_BLOCK *tx;

		tx = kzalloc(sizeof(*tx), GFP_KERNEL);
		if (!tx) {
			mutex_unlock(&pdx->io_mutex);
			return -ENOMEM;
		}
		tx->size = pdx->rTransDef[dwIdent].dwLength;
		tx->linear = (long long)((long)pdx->rTransDef[dwIdent].lpvBuff);
		tx->avail = GET_TX_MAXENTRIES;	/*  how many blocks we could return */
		tx->used = 1;	/*  number we actually return */
		tx->entries[0].physical =
		    (long long)(tx->linear + pdx->StagedOffset);
		tx->entries[0].size = tx->size;

		if (copy_to_user(pTX, tx, sizeof(*tx)))
			iReturn = -EFAULT;
		kfree(tx);
	}
	mutex_unlock(&pdx->io_mutex);
	return iReturn;
}

/****************************************************************************
** KillIO1401
**
** Empties the host i/o buffers
****************************************************************************/
int KillIO1401(DEVICE_EXTENSION *pdx)
{
	dev_dbg(&pdx->interface->dev, "%s\n", __func__);
	mutex_lock(&pdx->io_mutex);
	ced_flush_out_buff(pdx);
	ced_flush_in_buff(pdx);
	mutex_unlock(&pdx->io_mutex);
	return U14ERR_NOERROR;
}

/****************************************************************************
** BlkTransState
** Returns a 0 or a 1 for whether DMA is happening. No point holding a mutex
** for this as it only does one read.
*****************************************************************************/
int BlkTransState(DEVICE_EXTENSION *pdx)
{
	int iReturn = pdx->dwDMAFlag != MODE_CHAR;
	dev_dbg(&pdx->interface->dev, "%s: %d\n", __func__, iReturn);
	return iReturn;
}

/****************************************************************************
** StateOf1401
**
** Puts the current state of the 1401 in the Irp return buffer.
*****************************************************************************/
int StateOf1401(DEVICE_EXTENSION *pdx)
{
	int iReturn;
	mutex_lock(&pdx->io_mutex);

	ced_quick_check(pdx, false, false);	/*  get state up to date, no reset */
	iReturn = pdx->sCurrentState;

	mutex_unlock(&pdx->io_mutex);
	dev_dbg(&pdx->interface->dev, "%s: %d\n", __func__, iReturn);

	return iReturn;
}

/****************************************************************************
** StartSelfTest
**
** Initiates a self-test cycle. The assumption is that we have no interrupts
** active, so we should make sure that this is the case.
*****************************************************************************/
int StartSelfTest(DEVICE_EXTENSION *pdx)
{
	int nGot;
	mutex_lock(&pdx->io_mutex);
	dev_dbg(&pdx->interface->dev, "%s\n", __func__);

	ced_draw_down(pdx);	/*  wait for, then kill outstanding Urbs */
	ced_flush_in_buff(pdx);	/*  Clear out input buffer & pipe */
	ced_flush_out_buff(pdx);	/*  Clear output buffer & pipe */
	/* so things stay tidy */
	/* ced_read_write_cancel(pDeviceObject); */
	pdx->dwDMAFlag = MODE_CHAR;	/* Clear DMA mode flags here */

	nGot = usb_control_msg(pdx->udev, usb_rcvctrlpipe(pdx->udev, 0),
			       DB_SELFTEST, (H_TO_D | VENDOR | DEVREQ),
			       0, 0, NULL, 0, HZ); /* allow 1 second timeout */
	pdx->ulSelfTestTime = jiffies + HZ * 30;	/*  30 seconds into the future */

	mutex_unlock(&pdx->io_mutex);
	if (nGot < 0)
		dev_err(&pdx->interface->dev, "%s: err=%d\n", __func__, nGot);
	return nGot < 0 ? U14ERR_FAIL : U14ERR_NOERROR;
}

/****************************************************************************
** CheckSelfTest
**
** Check progress of a self-test cycle
****************************************************************************/
int CheckSelfTest(DEVICE_EXTENSION *pdx, TGET_SELFTEST __user *pGST)
{
	unsigned int state, error;
	int iReturn;
	TGET_SELFTEST gst;	/*  local work space */
	memset(&gst, 0, sizeof(gst));	/*  clear out the space (sets code 0) */

	mutex_lock(&pdx->io_mutex);

	dev_dbg(&pdx->interface->dev, "%s\n", __func__);
	iReturn = ced_get_state(pdx, &state, &error);
	if (iReturn == U14ERR_NOERROR)	/*  Only accept zero if it happens twice */
		iReturn = ced_get_state(pdx, &state, &error);

	if (iReturn != U14ERR_NOERROR) {	/*  Self-test can cause comms errors */
				/*  so we assume still testing */
		dev_err(&pdx->interface->dev,
			"%s: ced_get_state=%d, assuming still testing\n",
			__func__, iReturn);
		state = 0x80;	/*  Force still-testing, no error */
		error = 0;
		iReturn = U14ERR_NOERROR;
	}

	if ((state == -1) && (error == -1)) {	/*  If ced_get_state had problems */
		dev_err(&pdx->interface->dev,
			"%s: ced_get_state failed, assuming still testing\n",
			__func__);
		state = 0x80;	/*  Force still-testing, no error */
		error = 0;
	}

	if ((state & 0xFF) == 0x80) {	/*  If we are still in self-test */
		if (state & 0x00FF0000)	{ /*  Have we got an error? */
			gst.code = (state & 0x00FF0000) >> 16;	/*  read the error code */
			gst.x = error & 0x0000FFFF;	/*  Error data X */
			gst.y = (error & 0xFFFF0000) >> 16;	/*  and data Y */
			dev_dbg(&pdx->interface->dev,
				"Self-test error code %d\n", gst.code);
		} else {		/*  No error, check for timeout */
			unsigned long ulNow = jiffies;	/*  get current time */
			if (time_after(ulNow, pdx->ulSelfTestTime)) {
				gst.code = -2;	/*  Flag the timeout */
				dev_dbg(&pdx->interface->dev,
					"Self-test timed-out\n");
			} else
				dev_dbg(&pdx->interface->dev,
					"Self-test on-going\n");
		}
	} else {
		gst.code = -1;	/*  Flag the test is done */
		dev_dbg(&pdx->interface->dev, "Self-test done\n");
	}

	if (gst.code < 0) {	/*  If we have a problem or finished */
				/*  If using the 2890 we should reset properly */
		if ((pdx->nPipes == 4) && (pdx->s1401Type <= TYPEPOWER))
			ced_is_1401(pdx);	/*  Get 1401 reset and OK */
		else
			ced_quick_check(pdx, true, true);	/*  Otherwise check without reset unless problems */
	}
	mutex_unlock(&pdx->io_mutex);

	if (copy_to_user(pGST, &gst, sizeof(gst)))
		return -EFAULT;

	return iReturn;
}

/****************************************************************************
** TypeOf1401
**
** Returns code for standard, plus, micro1401, power1401 or none
****************************************************************************/
int TypeOf1401(DEVICE_EXTENSION *pdx)
{
	int iReturn = TYPEUNKNOWN;
	mutex_lock(&pdx->io_mutex);
	dev_dbg(&pdx->interface->dev, "%s\n", __func__);

	switch (pdx->s1401Type) {
	case TYPE1401:
		iReturn = U14ERR_STD;
		break;		/*  Handle these types directly */
	case TYPEPLUS:
		iReturn = U14ERR_PLUS;
		break;
	case TYPEU1401:
		iReturn = U14ERR_U1401;
		break;
	default:
		if ((pdx->s1401Type >= TYPEPOWER) && (pdx->s1401Type <= 25))
			iReturn = pdx->s1401Type + 4;	/*  We can calculate types */
		else		/*   for up-coming 1401 designs */
			iReturn = TYPEUNKNOWN;	/*  Don't know or not there */
	}
	dev_dbg(&pdx->interface->dev, "%s %d\n", __func__, iReturn);
	mutex_unlock(&pdx->io_mutex);

	return iReturn;
}

/****************************************************************************
** TransferFlags
**
** Returns flags on block transfer abilities
****************************************************************************/
int TransferFlags(DEVICE_EXTENSION *pdx)
{
	int iReturn = U14TF_MULTIA | U14TF_DIAG |	/*  we always have multiple DMA area */
	    U14TF_NOTIFY | U14TF_CIRCTH;	/*  diagnostics, notify and circular */
	dev_dbg(&pdx->interface->dev, "%s\n", __func__);
	mutex_lock(&pdx->io_mutex);
	if (pdx->bIsUSB2)	/*  Set flag for USB2 if appropriate */
		iReturn |= U14TF_USB2;
	mutex_unlock(&pdx->io_mutex);

	return iReturn;
}

/***************************************************************************
** DbgCmd1401
** Issues a debug\diagnostic command to the 1401 along with a 32-bit datum
** This is a utility command used for dbg operations.
*/
static int DbgCmd1401(DEVICE_EXTENSION *pdx, unsigned char cmd,
		      unsigned int data)
{
	int iReturn;
	dev_dbg(&pdx->interface->dev, "%s: entry\n", __func__);
	iReturn = usb_control_msg(pdx->udev, usb_sndctrlpipe(pdx->udev, 0), cmd,
				  (H_TO_D | VENDOR | DEVREQ),
				  (unsigned short)data,
				  (unsigned short)(data >> 16), NULL, 0, HZ);
						/* allow 1 second timeout */
	if (iReturn < 0)
		dev_err(&pdx->interface->dev, "%s: fail code=%d\n",
			__func__, iReturn);

	return iReturn;
}

/****************************************************************************
** DbgPeek
**
** Execute the diagnostic peek operation. Uses address, width and repeats.
****************************************************************************/
int DbgPeek(DEVICE_EXTENSION *pdx, TDBGBLOCK __user *pDB)
{
	int iReturn;
	TDBGBLOCK db;

	if (copy_from_user(&db, pDB, sizeof(db)))
		return -EFAULT;

	mutex_lock(&pdx->io_mutex);
	dev_dbg(&pdx->interface->dev, "%s: @ %08x\n", __func__, db.iAddr);

	iReturn = DbgCmd1401(pdx, DB_SETADD, db.iAddr);
	if (iReturn == U14ERR_NOERROR)
		iReturn = DbgCmd1401(pdx, DB_WIDTH, db.iWidth);
	if (iReturn == U14ERR_NOERROR)
		iReturn = DbgCmd1401(pdx, DB_REPEATS, db.iRepeats);
	if (iReturn == U14ERR_NOERROR)
		iReturn = DbgCmd1401(pdx, DB_PEEK, 0);
	mutex_unlock(&pdx->io_mutex);

	return iReturn;
}

/****************************************************************************
** DbgPoke
**
** Execute the diagnostic poke operation. Parameters are in the CSBLOCK struct
** in order address, size, repeats and value to poke.
****************************************************************************/
int DbgPoke(DEVICE_EXTENSION *pdx, TDBGBLOCK __user *pDB)
{
	int iReturn;
	TDBGBLOCK db;

	if (copy_from_user(&db, pDB, sizeof(db)))
		return -EFAULT;

	mutex_lock(&pdx->io_mutex);
	dev_dbg(&pdx->interface->dev, "%s: @ %08x\n", __func__, db.iAddr);

	iReturn = DbgCmd1401(pdx, DB_SETADD, db.iAddr);
	if (iReturn == U14ERR_NOERROR)
		iReturn = DbgCmd1401(pdx, DB_WIDTH, db.iWidth);
	if (iReturn == U14ERR_NOERROR)
		iReturn = DbgCmd1401(pdx, DB_REPEATS, db.iRepeats);
	if (iReturn == U14ERR_NOERROR)
		iReturn = DbgCmd1401(pdx, DB_POKE, db.iData);
	mutex_unlock(&pdx->io_mutex);

	return iReturn;
}

/****************************************************************************
** DbgRampData
**
** Execute the diagnostic ramp data operation. Parameters are in the CSBLOCK struct
** in order address, default, enable mask, size and repeats.
****************************************************************************/
int DbgRampData(DEVICE_EXTENSION *pdx, TDBGBLOCK __user *pDB)
{
	int iReturn;
	TDBGBLOCK db;

	if (copy_from_user(&db, pDB, sizeof(db)))
		return -EFAULT;

	mutex_lock(&pdx->io_mutex);
	dev_dbg(&pdx->interface->dev, "%s: @ %08x\n", __func__, db.iAddr);

	iReturn = DbgCmd1401(pdx, DB_SETADD, db.iAddr);
	if (iReturn == U14ERR_NOERROR)
		iReturn = DbgCmd1401(pdx, DB_SETDEF, db.iDefault);
	if (iReturn == U14ERR_NOERROR)
		iReturn = DbgCmd1401(pdx, DB_SETMASK, db.iMask);
	if (iReturn == U14ERR_NOERROR)
		iReturn = DbgCmd1401(pdx, DB_WIDTH, db.iWidth);
	if (iReturn == U14ERR_NOERROR)
		iReturn = DbgCmd1401(pdx, DB_REPEATS, db.iRepeats);
	if (iReturn == U14ERR_NOERROR)
		iReturn = DbgCmd1401(pdx, DB_RAMPD, 0);
	mutex_unlock(&pdx->io_mutex);

	return iReturn;
}

/****************************************************************************
** DbgRampAddr
**
** Execute the diagnostic ramp address operation
****************************************************************************/
int DbgRampAddr(DEVICE_EXTENSION *pdx, TDBGBLOCK __user *pDB)
{
	int iReturn;
	TDBGBLOCK db;

	if (copy_from_user(&db, pDB, sizeof(db)))
		return -EFAULT;

	mutex_lock(&pdx->io_mutex);
	dev_dbg(&pdx->interface->dev, "%s\n", __func__);

	iReturn = DbgCmd1401(pdx, DB_SETDEF, db.iDefault);
	if (iReturn == U14ERR_NOERROR)
		iReturn = DbgCmd1401(pdx, DB_SETMASK, db.iMask);
	if (iReturn == U14ERR_NOERROR)
		iReturn = DbgCmd1401(pdx, DB_WIDTH, db.iWidth);
	if (iReturn == U14ERR_NOERROR)
		iReturn = DbgCmd1401(pdx, DB_REPEATS, db.iRepeats);
	if (iReturn == U14ERR_NOERROR)
		iReturn = DbgCmd1401(pdx, DB_RAMPA, 0);
	mutex_unlock(&pdx->io_mutex);

	return iReturn;
}

/****************************************************************************
** DbgGetData
**
** Retrieve the data resulting from the last debug Peek operation
****************************************************************************/
int DbgGetData(DEVICE_EXTENSION *pdx, TDBGBLOCK __user *pDB)
{
	int iReturn;
	TDBGBLOCK db;
	memset(&db, 0, sizeof(db));	/*  fill returned block with 0s */

	mutex_lock(&pdx->io_mutex);
	dev_dbg(&pdx->interface->dev, "%s\n", __func__);

	/*  Read back the last peeked value from the 1401. */
	iReturn = usb_control_msg(pdx->udev, usb_rcvctrlpipe(pdx->udev, 0),
				  DB_DATA, (D_TO_H | VENDOR | DEVREQ), 0, 0,
				  &db.iData, sizeof(db.iData), HZ);
	if (iReturn == sizeof(db.iData)) {
		if (copy_to_user(pDB, &db, sizeof(db)))
			iReturn = -EFAULT;
		else
			iReturn = U14ERR_NOERROR;
	} else
		dev_err(&pdx->interface->dev, "%s: failed, code %d\n",
			__func__, iReturn);

	mutex_unlock(&pdx->io_mutex);

	return iReturn;
}

/****************************************************************************
** DbgStopLoop
**
** Stop any never-ending debug loop, we just call ced_get_state for USB
**
****************************************************************************/
int DbgStopLoop(DEVICE_EXTENSION *pdx)
{
	int iReturn;
	unsigned int uState, uErr;

	mutex_lock(&pdx->io_mutex);
	dev_dbg(&pdx->interface->dev, "%s\n", __func__);
	iReturn = ced_get_state(pdx, &uState, &uErr);
	mutex_unlock(&pdx->io_mutex);

	return iReturn;
}

/****************************************************************************
** SetCircular
**
** Sets up a transfer area record for circular transfers. If the area is
** already set, we attempt to unset it. Unsetting will fail if the area is
** booked and a transfer to that area is in progress. Otherwise, we will
** release the area and re-assign it.
****************************************************************************/
int SetCircular(DEVICE_EXTENSION *pdx, struct transfer_area_desc __user *pTD)
{
	int iReturn;
	bool bToHost;
	struct transfer_area_desc td;

	if (copy_from_user(&td, pTD, sizeof(td)))
		return -EFAULT;

	mutex_lock(&pdx->io_mutex);
	dev_dbg(&pdx->interface->dev, "%s: area:%d, size:%08x\n",
		__func__, td.wAreaNum, td.dwLength);
	bToHost = td.eSize != 0;	/*  this is used as the tohost flag */

	/*  The strange cast is done so that we don't get warnings in 32-bit linux about the size of the */
	/*  pointer. The pointer is always passed as a 64-bit object so that we don't have problems using */
	/*  a 32-bit program on a 64-bit system. unsigned long is 64-bits on a 64-bit system. */
	iReturn =
	    ced_set_area(pdx, td.wAreaNum,
		    (char __user *)((unsigned long)td.lpvBuff), td.dwLength,
		    true, bToHost);
	mutex_unlock(&pdx->io_mutex);
	return iReturn;
}

/****************************************************************************
** GetCircBlock
**
** Return the next available block of circularly-transferred data.
****************************************************************************/
int GetCircBlock(DEVICE_EXTENSION *pdx, TCIRCBLOCK __user *pCB)
{
	int iReturn = U14ERR_NOERROR;
	unsigned int nArea;
	TCIRCBLOCK cb;

	dev_dbg(&pdx->interface->dev, "%s\n", __func__);

	if (copy_from_user(&cb, pCB, sizeof(cb)))
		return -EFAULT;

	mutex_lock(&pdx->io_mutex);

	nArea = cb.nArea;	/*  Retrieve parameters first */
	cb.dwOffset = 0;	/*  set default result (nothing) */
	cb.dwSize = 0;

	if (nArea < MAX_TRANSAREAS) {	/*  The area number must be OK */
		TRANSAREA *pArea = &pdx->rTransDef[nArea];	/*  Pointer to relevant info */
		spin_lock_irq(&pdx->stagedLock);	/*  Lock others out */

		if ((pArea->bUsed) && (pArea->bCircular) &&	/*  Must be circular area */
		    (pArea->bCircToHost)) {	/*  For now at least must be to host */
			if (pArea->aBlocks[0].dwSize > 0) {	/*  Got anything? */
				cb.dwOffset = pArea->aBlocks[0].dwOffset;
				cb.dwSize = pArea->aBlocks[0].dwSize;
				dev_dbg(&pdx->interface->dev,
					"%s: return block 0: %d bytes at %d\n",
					__func__, cb.dwSize, cb.dwOffset);
			}
		} else
			iReturn = U14ERR_NOTSET;

		spin_unlock_irq(&pdx->stagedLock);
	} else
		iReturn = U14ERR_BADAREA;

	if (copy_to_user(pCB, &cb, sizeof(cb)))
		iReturn = -EFAULT;

	mutex_unlock(&pdx->io_mutex);
	return iReturn;
}

/****************************************************************************
** FreeCircBlock
**
** Frees a block of circularly-transferred data and returns the next one.
****************************************************************************/
int FreeCircBlock(DEVICE_EXTENSION *pdx, TCIRCBLOCK __user *pCB)
{
	int iReturn = U14ERR_NOERROR;
	unsigned int nArea, uStart, uSize;
	TCIRCBLOCK cb;

	dev_dbg(&pdx->interface->dev, "%s\n", __func__);

	if (copy_from_user(&cb, pCB, sizeof(cb)))
		return -EFAULT;

	mutex_lock(&pdx->io_mutex);

	nArea = cb.nArea;	/*  Retrieve parameters first */
	uStart = cb.dwOffset;
	uSize = cb.dwSize;
	cb.dwOffset = 0;	/*  then set default result (nothing) */
	cb.dwSize = 0;

	if (nArea < MAX_TRANSAREAS) {	/*  The area number must be OK */
		TRANSAREA *pArea = &pdx->rTransDef[nArea];	/*  Pointer to relevant info */
		spin_lock_irq(&pdx->stagedLock);	/*  Lock others out */

		if ((pArea->bUsed) && (pArea->bCircular) &&	/*  Must be circular area */
		    (pArea->bCircToHost)) {	/*  For now at least must be to host */
			bool bWaiting = false;

			if ((pArea->aBlocks[0].dwSize >= uSize) &&	/*  Got anything? */
			    (pArea->aBlocks[0].dwOffset == uStart)) {	/*  Must be legal data */
				pArea->aBlocks[0].dwSize -= uSize;
				pArea->aBlocks[0].dwOffset += uSize;
				if (pArea->aBlocks[0].dwSize == 0) {	/*  Have we emptied this block? */
					if (pArea->aBlocks[1].dwSize) {	/*  Is there a second block? */
						pArea->aBlocks[0] = pArea->aBlocks[1];	/*  Copy down block 2 data */
						pArea->aBlocks[1].dwSize = 0;	/*  and mark the second block as unused */
						pArea->aBlocks[1].dwOffset = 0;
					} else
						pArea->aBlocks[0].dwOffset = 0;
				}

				dev_dbg(&pdx->interface->dev,
					"%s: free %d bytes at %d, return %d bytes at %d, wait=%d\n",
					__func__, uSize, uStart,
					pArea->aBlocks[0].dwSize,
					pArea->aBlocks[0].dwOffset,
					pdx->bXFerWaiting);

				/*  Return the next available block of memory as well */
				if (pArea->aBlocks[0].dwSize > 0) {	/*  Got anything? */
					cb.dwOffset =
					    pArea->aBlocks[0].dwOffset;
					cb.dwSize = pArea->aBlocks[0].dwSize;
				}

				bWaiting = pdx->bXFerWaiting;
				if (bWaiting && pdx->bStagedUrbPending) {
					dev_err(&pdx->interface->dev,
						"%s: ERROR: waiting xfer and staged Urb pending!\n",
						__func__);
					bWaiting = false;
				}
			} else {
				dev_err(&pdx->interface->dev,
					"%s: ERROR: freeing %d bytes at %d, block 0 is %d bytes at %d\n",
					__func__, uSize, uStart,
					pArea->aBlocks[0].dwSize,
					pArea->aBlocks[0].dwOffset);
				iReturn = U14ERR_NOMEMORY;
			}

			/*  If we have one, kick off pending transfer */
			if (bWaiting) {	/*  Got a block xfer waiting? */
				int RWMStat =
				    ced_read_write_mem(pdx, !pdx->rDMAInfo.bOutWard,
						 pdx->rDMAInfo.wIdent,
						 pdx->rDMAInfo.dwOffset,
						 pdx->rDMAInfo.dwSize);
				if (RWMStat != U14ERR_NOERROR)
					dev_err(&pdx->interface->dev,
						"%s: rw setup failed %d\n",
						__func__, RWMStat);
			}
		} else
			iReturn = U14ERR_NOTSET;

		spin_unlock_irq(&pdx->stagedLock);
	} else
		iReturn = U14ERR_BADAREA;

	if (copy_to_user(pCB, &cb, sizeof(cb)))
		iReturn = -EFAULT;

	mutex_unlock(&pdx->io_mutex);
	return iReturn;
}
