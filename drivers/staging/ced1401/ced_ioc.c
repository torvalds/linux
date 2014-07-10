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
static void ced_flush_out_buff(struct ced_data *ced)
{
	dev_dbg(&ced->interface->dev, "%s: current_state=%d\n",
		__func__, ced->current_state);

	/* Do nothing if hardware in trouble */
	if (ced->current_state == U14ERR_TIME)
		return;
	/* Kill off any pending I/O */
	/* CharSend_Cancel(ced);  */
	spin_lock_irq(&ced->char_out_lock);
	ced->num_output = 0;
	ced->out_buff_get = 0;
	ced->out_buff_put = 0;
	spin_unlock_irq(&ced->char_out_lock);
}

/****************************************************************************
**
** ced_flush_in_buff
**
** Empties the input buffer and sets int lines
****************************************************************************/
static void ced_flush_in_buff(struct ced_data *ced)
{
	dev_dbg(&ced->interface->dev, "%s: current_state=%d\n",
		__func__, ced->current_state);
	if (ced->current_state == U14ERR_TIME)	/* Do nothing if hardware in trouble */
		return;
	/* Kill off any pending I/O */
	/*     CharRead_Cancel(pDevObject);  */
	spin_lock_irq(&ced->char_in_lock);
	ced->num_input = 0;
	ced->in_buff_get = 0;
	ced->in_buff_put = 0;
	spin_unlock_irq(&ced->char_in_lock);
}

/****************************************************************************
** ced_put_chars
**
** Utility routine to copy chars into the output buffer and fire them off.
** called from user mode, holds char_out_lock.
****************************************************************************/
static int ced_put_chars(struct ced_data *ced, const char *ch,
		    unsigned int count)
{
	int ret;
	spin_lock_irq(&ced->char_out_lock);	/*  get the output spin lock */
	if ((OUTBUF_SZ - ced->num_output) >= count) {
		unsigned int u;
		for (u = 0; u < count; u++) {
			ced->output_buffer[ced->out_buff_put++] = ch[u];
			if (ced->out_buff_put >= OUTBUF_SZ)
				ced->out_buff_put = 0;
		}
		ced->num_output += count;
		spin_unlock_irq(&ced->char_out_lock);
		ret = ced_send_chars(ced);	/*  ...give a chance to transmit data */
	} else {
		ret = U14ERR_NOOUT;	/*  no room at the out (ha-ha) */
		spin_unlock_irq(&ced->char_out_lock);
	}
	return ret;
}

/*****************************************************************************
** Add the data in "data" local pointer of length n to the output buffer, and
** trigger an output transfer if this is appropriate. User mode.
** Holds the io_mutex
*****************************************************************************/
int ced_send_string(struct ced_data *ced, const char __user *data,
	       unsigned int n)
{
	int ret = U14ERR_NOERROR;	/* assume all will be well */
	char buffer[OUTBUF_SZ + 1];	/* space in our address space */
					/* for characters             */
	if (n > OUTBUF_SZ)	/*  check space in local buffer... */
		return U14ERR_NOOUT;	/*  ...too many characters */
	if (copy_from_user(buffer, data, n))
		return -EFAULT;
	buffer[n] = 0;		/*  terminate for debug purposes */

	mutex_lock(&ced->io_mutex);	/*  Protect disconnect from new i/o */
	if (n > 0) {		/*  do nothing if nowt to do! */
		dev_dbg(&ced->interface->dev, "%s: n=%d>%s<\n",
			__func__, n, buffer);
		ret = ced_put_chars(ced, buffer, n);
	}

	ced_allowi(ced);		/*  make sure we have input int */
	mutex_unlock(&ced->io_mutex);

	return ret;
}

/****************************************************************************
** ced_send_char
**
** Sends a single character to the 1401. User mode, holds io_mutex.
****************************************************************************/
int ced_send_char(struct ced_data *ced, char c)
{
	int ret;
	mutex_lock(&ced->io_mutex);	/*  Protect disconnect from new i/o */
	ret = ced_put_chars(ced, &c, 1);
	dev_dbg(&ced->interface->dev, "ced_send_char >%c< (0x%02x)\n", c, c);
	ced_allowi(ced);	/*  Make sure char reads are running */
	mutex_unlock(&ced->io_mutex);
	return ret;
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
int ced_get_state(struct ced_data *ced, __u32 *state, __u32 *error)
{
	int got;
	dev_dbg(&ced->interface->dev, "%s: entry\n", __func__);

	*state = 0xFFFFFFFF;	/*  Start off with invalid state */
	got = usb_control_msg(ced->udev, usb_rcvctrlpipe(ced->udev, 0),
			       GET_STATUS, (D_TO_H | VENDOR | DEVREQ), 0, 0,
			       ced->stat_buf, sizeof(ced->stat_buf), HZ);
	if (got != sizeof(ced->stat_buf)) {
		dev_err(&ced->interface->dev,
			"%s: FAILED, return code %d\n", __func__, got);
		/* Indicate that things are very wrong indeed */
		ced->current_state = U14ERR_TIME;
		*state = 0;	/*  Force status values to a known state */
		*error = 0;
	} else {
		int device;
		dev_dbg(&ced->interface->dev,
			"%s: Success, state: 0x%x, 0x%x\n",
			__func__, ced->stat_buf[0], ced->stat_buf[1]);

		/* Return the state values to the calling code */
		*state = ced->stat_buf[0];

		*error = ced->stat_buf[1];

		/* 1401 type code value */
		device = ced->udev->descriptor.bcdDevice >> 8;
		switch (device) {	/*  so we can clean up current state */
		case 0:
			ced->current_state = U14ERR_U1401;
			break;

		default: /* allow lots of device codes for future 1401s */
			if ((device >= 1) && (device <= 23))
				ced->current_state = (short)(device + 6);
			else
				ced->current_state = U14ERR_ILL;
			break;
		}
	}

	return ced->current_state >= 0 ? U14ERR_NOERROR : ced->current_state;
}

/****************************************************************************
** ced_read_write_cancel
**
** Kills off staged read\write request from the USB if one is pending.
****************************************************************************/
int ced_read_write_cancel(struct ced_data *ced)
{
	dev_dbg(&ced->interface->dev, "%s: entry %d\n",
		__func__, ced->staged_urb_pending);
#ifdef NOT_WRITTEN_YET
	int ntStatus = STATUS_SUCCESS;
	bool bResult = false;
	unsigned int i;
	/*  We can fill this in when we know how we will implement the staged transfer stuff */
	spin_lock_irq(&ced->staged_lock);

	if (ced->staged_urb_pending) {	/*  anything to be cancelled? May need more... */
		dev_info(&ced->interface - dev,
			 "ced_read_write_cancel about to cancel Urb\n");
		/* Clear the staging done flag */
		/* KeClearEvent(&ced->StagingDoneEvent); */
		USB_ASSERT(ced->pStagedIrp != NULL);

		/*  Release the spinlock first otherwise the completion routine may hang */
		/*   on the spinlock while this function hands waiting for the event. */
		spin_unlock_irq(&ced->staged_lock);
		bResult = IoCancelIrp(ced->pStagedIrp);	/*  Actually do the cancel */
		if (bResult) {
			LARGE_INTEGER timeout;
			timeout.QuadPart = -10000000;	/*  Use a timeout of 1 second */
			dev_info(&ced->interface - dev,
				 "%s: about to wait till done\n", __func__);
			ntStatus =
			    KeWaitForSingleObject(&ced->StagingDoneEvent,
						  Executive, KernelMode, FALSE,
						  &timeout);
		} else {
			dev_info(&ced->interface - dev,
				 "%s: cancellation failed\n", __func__);
			ntStatus = U14ERR_FAIL;
		}
		USB_KdPrint(DBGLVL_DEFAULT,
			    ("ced_read_write_cancel ntStatus = 0x%x decimal %d\n",
			     ntStatus, ntStatus));
	} else
		spin_unlock_irq(&ced->staged_lock);

	dev_info(&ced->interface - dev, "%s: done\n", __func__);
	return ntStatus;
#else
	return U14ERR_NOERROR;
#endif

}

/***************************************************************************
** ced_in_self_test - utility to check in self test. Return 1 for ST, 0 for not or
** a -ve error code if we failed for some reason.
***************************************************************************/
static int ced_in_self_test(struct ced_data *ced, unsigned int *pState)
{
	unsigned int state, error;
	int iReturn = ced_get_state(ced, &state, &error);	/*  see if in self-test */
	if (iReturn == U14ERR_NOERROR)	/*  if all still OK */
		iReturn = (state == (unsigned int)-1) ||	/*  TX problem or... */
		    ((state & 0xff) == 0x80);	/*  ...self test */
	*pState = state;	/*  return actual state */
	return iReturn;
}

/***************************************************************************
** ced_is_1401 - ALWAYS CALLED HOLDING THE io_mutex
**
** Tests for the current state of the 1401. Sets current_state:
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
static bool ced_is_1401(struct ced_data *ced)
{
	int iReturn;
	dev_dbg(&ced->interface->dev, "%s\n", __func__);

	ced_draw_down(ced);	/*  wait for, then kill outstanding Urbs */
	ced_flush_in_buff(ced);	/*  Clear out input buffer & pipe */
	ced_flush_out_buff(ced);	/*  Clear output buffer & pipe */

	/*  The next call returns 0 if OK, but has returned 1 in the past, meaning that */
	/*  usb_unlock_device() is needed... now it always is */
	iReturn = usb_lock_device_for_reset(ced->udev, ced->interface);

	/*  release the io_mutex because if we don't, we will deadlock due to system */
	/*  calls back into the driver. */
	mutex_unlock(&ced->io_mutex);	/*  locked, so we will not get system calls */
	if (iReturn >= 0) {	/*  if we failed */
		iReturn = usb_reset_device(ced->udev);	/*  try to do the reset */
		usb_unlock_device(ced->udev);	/*  undo the lock */
	}

	mutex_lock(&ced->io_mutex);	/*  hold stuff off while we wait */
	ced->dma_flag = MODE_CHAR;	/*  Clear DMA mode flag regardless! */
	if (iReturn == 0) {	/*  if all is OK still */
		unsigned int state;
		iReturn = ced_in_self_test(ced, &state);	/*  see if likely in self test */
		if (iReturn > 0) {	/*  do we need to wait for self-test? */
			unsigned long ulTimeOut = jiffies + 30 * HZ;	/*  when to give up */
			while ((iReturn > 0) && time_before(jiffies, ulTimeOut)) {
				schedule();	/*  let other stuff run */
				iReturn = ced_in_self_test(ced, &state);	/*  see if done yet */
			}
		}

		if (iReturn == 0)	/*  if all is OK... */
			iReturn = state == 0;	/*  then success is that the state is 0 */
	} else
		iReturn = 0;	/*  we failed */
	ced->force_reset = false;	/*  Clear forced reset flag now */

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
static bool ced_quick_check(struct ced_data *ced, bool bTestBuff, bool bCanReset)
{
	bool bRet = false;	/*  assume it will fail and we will reset */
	bool bShortTest;

	bShortTest = ((ced->dma_flag == MODE_CHAR) &&	/*  no DMA running */
		      (!ced->force_reset) &&	/*  Not had a real reset forced */
		      (ced->current_state >= U14ERR_STD));	/*  No 1401 errors stored */

	dev_dbg(&ced->interface->dev,
		"%s: DMAFlag:%d, state:%d, force:%d, testBuff:%d, short:%d\n",
		__func__, ced->dma_flag, ced->current_state, ced->force_reset,
		bTestBuff, bShortTest);

	if ((bTestBuff) &&	/*  Buffer check requested, and... */
	    (ced->num_input || ced->num_output)) {	/*  ...characters were in the buffer? */
		bShortTest = false;	/*  Then do the full test */
		dev_dbg(&ced->interface->dev,
			"%s: will reset as buffers not empty\n", __func__);
	}

	if (bShortTest || !bCanReset) {	/*  Still OK to try the short test? */
				/*  Always test if no reset - we want state update */
		unsigned int state, error;
		dev_dbg(&ced->interface->dev, "%s: ced_get_state\n", __func__);
		if (ced_get_state(ced, &state, &error) == U14ERR_NOERROR) {	/*  Check on the 1401 state */
			if ((state & 0xFF) == 0)	/*  If call worked, check the status value */
				bRet = true;	/*  If that was zero, all is OK, no reset needed */
		}
	}

	if (!bRet && bCanReset)	{ /*  If all not OK, then */
		dev_info(&ced->interface->dev, "%s: ced_is_1401 %d %d %d %d\n",
			 __func__, bShortTest, ced->current_state, bTestBuff,
			 ced->force_reset);
		bRet = ced_is_1401(ced);	/*   do full test */
	}

	return bRet;
}

/****************************************************************************
** ced_reset
**
** Resets the 1401 and empties the i/o buffers
*****************************************************************************/
int ced_reset(struct ced_data *ced)
{
	mutex_lock(&ced->io_mutex);	/*  Protect disconnect from new i/o */
	dev_dbg(&ced->interface->dev, "%s: About to call ced_quick_check\n",
		__func__);
	ced_quick_check(ced, true, true);	/*  Check 1401, reset if not OK */
	mutex_unlock(&ced->io_mutex);
	return U14ERR_NOERROR;
}

/****************************************************************************
** ced_get_char
**
** Gets a single character from the 1401
****************************************************************************/
int ced_get_char(struct ced_data *ced)
{
	int iReturn = U14ERR_NOIN;	/*  assume we will get  nothing */
	mutex_lock(&ced->io_mutex);	/*  Protect disconnect from new i/o */

	dev_dbg(&ced->interface->dev, "%s\n", __func__);

	ced_allowi(ced);	/*  Make sure char reads are running */
	ced_send_chars(ced);	/*  and send any buffered chars */

	spin_lock_irq(&ced->char_in_lock);
	if (ced->num_input > 0) {	/*  worth looking */
		iReturn = ced->input_buffer[ced->in_buff_get++];
		if (ced->in_buff_get >= INBUF_SZ)
			ced->in_buff_get = 0;
		ced->num_input--;
	} else
		iReturn = U14ERR_NOIN;	/*  no input data to read */
	spin_unlock_irq(&ced->char_in_lock);

	ced_allowi(ced);	/*  Make sure char reads are running */

	mutex_unlock(&ced->io_mutex);	/*  Protect disconnect from new i/o */
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
int ced_get_string(struct ced_data *ced, char __user *pUser, int n)
{
	int nAvailable;		/*  character in the buffer */
	int iReturn = U14ERR_NOIN;
	if (n <= 0)
		return -ENOMEM;

	mutex_lock(&ced->io_mutex);	/*  Protect disconnect from new i/o */
	ced_allowi(ced);	/*  Make sure char reads are running */
	ced_send_chars(ced);		/*  and send any buffered chars */

	spin_lock_irq(&ced->char_in_lock);
	nAvailable = ced->num_input;	/*  characters available now */
	if (nAvailable > n)	/*  read max of space in pUser... */
		nAvailable = n;	/*  ...or input characters */

	if (nAvailable > 0) {	/*  worth looking? */
		char buffer[INBUF_SZ + 1];	/*  space for a linear copy of data */
		int nGot = 0;
		int nCopyToUser;	/*  number to copy to user */
		char cData;
		do {
			cData = ced->input_buffer[ced->in_buff_get++];
			if (cData == CR_CHAR)	/*  replace CR with zero */
				cData = (char)0;

			if (ced->in_buff_get >= INBUF_SZ)
				ced->in_buff_get = 0;	/*  wrap buffer pointer */

			buffer[nGot++] = cData;	/*  save the output */
		} while ((nGot < nAvailable) && cData);

		nCopyToUser = nGot;	/*  what to copy... */
		if (cData) {	/*  do we need null */
			buffer[nGot] = (char)0;	/*  make it tidy */
			if (nGot < n)	/*  if space in user buffer... */
				++nCopyToUser;	/*  ...copy the 0 as well. */
		}

		ced->num_input -= nGot;
		spin_unlock_irq(&ced->char_in_lock);

		dev_dbg(&ced->interface->dev, "%s: read %d characters >%s<\n",
			__func__, nGot, buffer);
		if (copy_to_user(pUser, buffer, nCopyToUser))
			iReturn = -EFAULT;
		else
			iReturn = nGot;		/*  report characters read */
	} else
		spin_unlock_irq(&ced->char_in_lock);

	ced_allowi(ced);	/*  Make sure char reads are running */
	mutex_unlock(&ced->io_mutex);	/*  Protect disconnect from new i/o */

	return iReturn;
}

/*******************************************************************************
** Get count of characters in the inout buffer.
*******************************************************************************/
int ced_stat_1401(struct ced_data *ced)
{
	int iReturn;
	mutex_lock(&ced->io_mutex);	/*  Protect disconnect from new i/o */
	ced_allowi(ced);		/*  make sure we allow pending chars */
	ced_send_chars(ced);		/*  in both directions */
	iReturn = ced->num_input;	/*  no lock as single read */
	mutex_unlock(&ced->io_mutex);	/*  Protect disconnect from new i/o */
	return iReturn;
}

/****************************************************************************
** ced_line_count
**
** Returns the number of newline chars in the buffer. There is no need for
** any fancy interlocks as we only read the interrupt routine data, and the
** system is arranged so nothing can be destroyed.
****************************************************************************/
int ced_line_count(struct ced_data *ced)
{
	int iReturn = 0;	/*  will be count of line ends */

	mutex_lock(&ced->io_mutex);	/*  Protect disconnect from new i/o */
	ced_allowi(ced);		/*  Make sure char reads are running */
	ced_send_chars(ced);		/*  and send any buffered chars */
	spin_lock_irq(&ced->char_in_lock);	/*  Get protection */

	if (ced->num_input > 0) {	/*  worth looking? */
		unsigned int dwIndex = ced->in_buff_get; /* start at first available */
		unsigned int dwEnd = ced->in_buff_put;	/* Position for search end */
		do {
			if (ced->input_buffer[dwIndex++] == CR_CHAR)
				++iReturn;	/*  inc count if CR */

			if (dwIndex >= INBUF_SZ)	/*  see if we fall off buff */
				dwIndex = 0;
		} while (dwIndex != dwEnd);	/*  go to last available */
	}

	spin_unlock_irq(&ced->char_in_lock);
	dev_dbg(&ced->interface->dev, "%s: returned %d\n", __func__, iReturn);
	mutex_unlock(&ced->io_mutex);	/*  Protect disconnect from new i/o */
	return iReturn;
}

/****************************************************************************
** ced_get_out_buf_space
**
** Gets the space in the output buffer. Called from user code.
*****************************************************************************/
int ced_get_out_buf_space(struct ced_data *ced)
{
	int iReturn;
	mutex_lock(&ced->io_mutex);	/*  Protect disconnect from new i/o */
	ced_send_chars(ced);		/*  send any buffered chars */
	iReturn = (int)(OUTBUF_SZ - ced->num_output); /* no lock needed for single read */
	dev_dbg(&ced->interface->dev, "%s: %d\n", __func__, iReturn);
	mutex_unlock(&ced->io_mutex);	/*  Protect disconnect from new i/o */
	return iReturn;
}

/****************************************************************************
**
** ced_clear_area
**
** Clears up a transfer area. This is always called in the context of a user
** request, never from a call-back.
****************************************************************************/
int ced_clear_area(struct ced_data *ced, int nArea)
{
	int iReturn = U14ERR_NOERROR;

	if ((nArea < 0) || (nArea >= MAX_TRANSAREAS)) {
		iReturn = U14ERR_BADAREA;
		dev_err(&ced->interface->dev, "%s: Attempt to clear area %d\n",
			__func__, nArea);
	} else {
		/* to save typing */
		struct transarea *pTA = &ced->trans_def[nArea];
		if (!pTA->used)	/*  if not used... */
			iReturn = U14ERR_NOTSET;	/*  ...nothing to be done */
		else {
			/*  We must save the memory we return as we shouldn't mess with memory while */
			/*  holding a spin lock. */
			struct page **pPages = NULL; /*save page address list*/
			int nPages = 0;	/*  and number of pages */
			int np;

			dev_dbg(&ced->interface->dev, "%s: area %d\n",
				__func__, nArea);
			spin_lock_irq(&ced->staged_lock);
			if ((ced->staged_id == nArea)
			    && (ced->dma_flag > MODE_CHAR)) {
				iReturn = U14ERR_UNLOCKFAIL;	/*  cannot delete as in use */
				dev_err(&ced->interface->dev,
					"%s: call on area %d while active\n",
					__func__, nArea);
			} else {
				pPages = pTA->pages;	/*  save page address list */
				nPages = pTA->n_pages;	/*  and page count */
				if (pTA->event_sz)	/*  if events flagging in use */
					wake_up_interruptible(&pTA->event);	/*  release anything that was waiting */

				if (ced->xfer_waiting
				    && (ced->dma_info.ident == nArea))
					ced->xfer_waiting = false; /* Cannot have pending xfer if area cleared */

				/*  Clean out the struct transarea except for the wait queue, which is at the end */
				/*  This sets used to false and event_sz to 0 to say area not used and no events. */
				memset(pTA, 0,
				       sizeof(struct transarea) -
				       sizeof(wait_queue_head_t));
			}
			spin_unlock_irq(&ced->staged_lock);

			if (pPages) {	/*  if we decided to release the memory */
				/*  Now we must undo the pinning down of the pages. We will assume the worst and mark */
				/*  all the pages as dirty. Don't be tempted to move this up above as you must not be */
				/*  holding a spin lock to do this stuff as it is not atomic. */
				dev_dbg(&ced->interface->dev, "%s: nPages=%d\n",
					__func__, nPages);

				for (np = 0; np < nPages; ++np) {
					if (pPages[np]) {
						SetPageDirty(pPages[np]);
						page_cache_release(pPages[np]);
					}
				}

				kfree(pPages);
				dev_dbg(&ced->interface->dev,
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
** ced_set_transfer and ced_set_circular.
****************************************************************************/
static int ced_set_area(struct ced_data *ced, int nArea, char __user *puBuf,
		   unsigned int dwLength, bool bCircular, bool bCircToHost)
{
	/*  Start by working out the page aligned start of the area and the size */
	/*  of the area in pages, allowing for the start not being aligned and the */
	/*  end needing to be rounded up to a page boundary. */
	unsigned long ulStart = ((unsigned long)puBuf) & PAGE_MASK;
	unsigned int ulOffset = ((unsigned long)puBuf) & (PAGE_SIZE - 1);
	int len = (dwLength + ulOffset + PAGE_SIZE - 1) >> PAGE_SHIFT;

	struct transarea *pTA = &ced->trans_def[nArea];	/*  to save typing */
	struct page **pPages = NULL;	/*  space for page tables */
	int nPages = 0;		/*  and number of pages */

	int iReturn = ced_clear_area(ced, nArea);	/*  see if OK to use this area */
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
	dev_dbg(&ced->interface->dev, "%s: %p, length=%06x, circular %d\n",
		__func__, puBuf, dwLength, bCircular);

	/*  To pin down user pages we must first acquire the mapping semaphore. */
	nPages = get_user_pages_fast(ulStart, len, 1, pPages);
	dev_dbg(&ced->interface->dev, "%s: nPages = %d\n", __func__, nPages);

	if (nPages > 0) {		/*  if we succeeded */
		/*  If you are tempted to use page_address (form LDD3), forget it. You MUST use */
		/*  kmap() or kmap_atomic() to get a virtual address. page_address will give you */
		/*  (null) or at least it does in this context with an x86 machine. */
		spin_lock_irq(&ced->staged_lock);
		pTA->buff = puBuf;	/*  keep start of region (user address) */
		pTA->base_offset = ulOffset;	/*  save offset in first page to start of xfer */
		pTA->length = dwLength;	/*  Size if the region in bytes */
		pTA->pages = pPages;	/*  list of pages that are used by buffer */
		pTA->n_pages = nPages;	/*  number of pages */

		pTA->circular = bCircular;
		pTA->circ_to_host = bCircToHost;

		pTA->blocks[0].offset = 0;
		pTA->blocks[0].size = 0;
		pTA->blocks[1].offset = 0;
		pTA->blocks[1].size = 0;
		pTA->used = true;	/*  This is now a used block */

		spin_unlock_irq(&ced->staged_lock);
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
int ced_set_transfer(struct ced_data *ced, struct transfer_area_desc __user *pTD)
{
	int iReturn;
	struct transfer_area_desc td;

	if (copy_from_user(&td, pTD, sizeof(td)))
		return -EFAULT;

	mutex_lock(&ced->io_mutex);
	dev_dbg(&ced->interface->dev, "%s: area:%d, size:%08x\n",
		__func__, td.wAreaNum, td.dwLength);
	/*  The strange cast is done so that we don't get warnings in 32-bit linux about the size of the */
	/*  pointer. The pointer is always passed as a 64-bit object so that we don't have problems using */
	/*  a 32-bit program on a 64-bit system. unsigned long is 64-bits on a 64-bit system. */
	iReturn =
	    ced_set_area(ced, td.wAreaNum,
		    (char __user *)((unsigned long)td.lpvBuff), td.dwLength,
		    false, false);
	mutex_unlock(&ced->io_mutex);
	return iReturn;
}

/****************************************************************************
** UnSetTransfer
** Erases a transfer area record
****************************************************************************/
int ced_unset_transfer(struct ced_data *ced, int nArea)
{
	int iReturn;
	mutex_lock(&ced->io_mutex);
	iReturn = ced_clear_area(ced, nArea);
	mutex_unlock(&ced->io_mutex);
	return iReturn;
}

/****************************************************************************
** ced_set_event
** Creates an event that we can test for based on a transfer to/from an area.
** The area must be setup for a transfer. We attempt to simulate the Windows
** driver behavior for events (as we don't actually use them), which is to
** pretend that whatever the user asked for was achieved, so we return 1 if
** try to create one, and 0 if they ask to remove (assuming all else was OK).
****************************************************************************/
int ced_set_event(struct ced_data *ced, struct transfer_event __user *pTE)
{
	int iReturn = U14ERR_NOERROR;
	struct transfer_event te;

	/*  get a local copy of the data */
	if (copy_from_user(&te, pTE, sizeof(te)))
		return -EFAULT;

	if (te.wAreaNum >= MAX_TRANSAREAS)	/*  the area must exist */
		return U14ERR_BADAREA;
	else {
		struct transarea *pTA = &ced->trans_def[te.wAreaNum];
		mutex_lock(&ced->io_mutex);	/*  make sure we have no competitor */
		spin_lock_irq(&ced->staged_lock);
		if (pTA->used) {	/*  area must be in use */
			pTA->event_st = te.dwStart;	/*  set area regions */
			pTA->event_sz = te.dwLength;	/*  set size (0 cancels it) */
			pTA->event_to_host = te.wFlags & 1;	/*  set the direction */
			pTA->wake_up = 0;	/*  zero the wake up count */
		} else
			iReturn = U14ERR_NOTSET;
		spin_unlock_irq(&ced->staged_lock);
		mutex_unlock(&ced->io_mutex);
	}
	return iReturn ==
	    U14ERR_NOERROR ? (te.iSetEvent ? 1 : U14ERR_NOERROR) : iReturn;
}

/****************************************************************************
** ced_wait_event
** Sleep the process with a timeout waiting for an event. Returns the number
** of times that a block met the event condition since we last cleared it or
** 0 if timed out, or -ve error (bad area or not set, or signal).
****************************************************************************/
int ced_wait_event(struct ced_data *ced, int nArea, int msTimeOut)
{
	int iReturn;
	if ((unsigned)nArea >= MAX_TRANSAREAS)
		return U14ERR_BADAREA;
	else {
		int iWait;
		struct transarea *pTA = &ced->trans_def[nArea];
		msTimeOut = (msTimeOut * HZ + 999) / 1000;	/*  convert timeout to jiffies */

		/*  We cannot wait holding the mutex, but we check the flags while holding */
		/*  it. This may well be pointless as another thread could get in between */
		/*  releasing it and the wait call. However, this would have to clear the */
		/*  iWakeUp flag. However, the !pTA-bUsed may help us in this case. */
		mutex_lock(&ced->io_mutex);	/*  make sure we have no competitor */
		if (!pTA->used || !pTA->event_sz)	/*  check something to wait for... */
			return U14ERR_NOTSET;	/*  ...else we do nothing */
		mutex_unlock(&ced->io_mutex);

		if (msTimeOut)
			iWait =
			    wait_event_interruptible_timeout(pTA->event,
							     pTA->wake_up
							     || !pTA->used,
							     msTimeOut);
		else
			iWait =
			    wait_event_interruptible(pTA->event, pTA->wake_up
						     || !pTA->used);
		if (iWait)
			iReturn = -ERESTARTSYS;	/*  oops - we have had a SIGNAL */
		else
			iReturn = pTA->wake_up;	/*  else the wakeup count */

		spin_lock_irq(&ced->staged_lock);
		pTA->wake_up = 0;	/*  clear the flag */
		spin_unlock_irq(&ced->staged_lock);
	}
	return iReturn;
}

/****************************************************************************
** ced_test_event
** Test the event to see if a ced_wait_event would return immediately. Returns the
** number of times a block completed since the last call, or 0 if none or a
** negative error.
****************************************************************************/
int ced_test_event(struct ced_data *ced, int nArea)
{
	int iReturn;
	if ((unsigned)nArea >= MAX_TRANSAREAS)
		iReturn = U14ERR_BADAREA;
	else {
		struct transarea *pTA = &ced->trans_def[nArea];
		mutex_lock(&ced->io_mutex);	/*  make sure we have no competitor */
		spin_lock_irq(&ced->staged_lock);
		iReturn = pTA->wake_up;	/*  get wakeup count since last call */
		pTA->wake_up = 0;	/*  clear the count */
		spin_unlock_irq(&ced->staged_lock);
		mutex_unlock(&ced->io_mutex);
	}
	return iReturn;
}

/****************************************************************************
** ced_get_transferInfo
** Puts the current state of the 1401 in a TGET_TX_BLOCK.
*****************************************************************************/
int ced_get_transfer(struct ced_data *ced, TGET_TX_BLOCK __user *pTX)
{
	int iReturn = U14ERR_NOERROR;
	unsigned int dwIdent;

	mutex_lock(&ced->io_mutex);
	dwIdent = ced->staged_id;	/*  area ident for last xfer */
	if (dwIdent >= MAX_TRANSAREAS)
		iReturn = U14ERR_BADAREA;
	else {
		/*  Return the best information we have - we don't have physical addresses */
		TGET_TX_BLOCK *tx;

		tx = kzalloc(sizeof(*tx), GFP_KERNEL);
		if (!tx) {
			mutex_unlock(&ced->io_mutex);
			return -ENOMEM;
		}
		tx->size = ced->trans_def[dwIdent].length;
		tx->linear = (long long)((long)ced->trans_def[dwIdent].buff);
		tx->avail = GET_TX_MAXENTRIES;	/*  how many blocks we could return */
		tx->used = 1;	/*  number we actually return */
		tx->entries[0].physical =
		    (long long)(tx->linear + ced->staged_offset);
		tx->entries[0].size = tx->size;

		if (copy_to_user(pTX, tx, sizeof(*tx)))
			iReturn = -EFAULT;
		kfree(tx);
	}
	mutex_unlock(&ced->io_mutex);
	return iReturn;
}

/****************************************************************************
** ced_kill_io
**
** Empties the host i/o buffers
****************************************************************************/
int ced_kill_io(struct ced_data *ced)
{
	dev_dbg(&ced->interface->dev, "%s\n", __func__);
	mutex_lock(&ced->io_mutex);
	ced_flush_out_buff(ced);
	ced_flush_in_buff(ced);
	mutex_unlock(&ced->io_mutex);
	return U14ERR_NOERROR;
}

/****************************************************************************
** ced_state_of_1401
**
** Puts the current state of the 1401 in the Irp return buffer.
*****************************************************************************/
int ced_state_of_1401(struct ced_data *ced)
{
	int iReturn;
	mutex_lock(&ced->io_mutex);

	ced_quick_check(ced, false, false);	/*  get state up to date, no reset */
	iReturn = ced->current_state;

	mutex_unlock(&ced->io_mutex);
	dev_dbg(&ced->interface->dev, "%s: %d\n", __func__, iReturn);

	return iReturn;
}

/****************************************************************************
** ced_start_self_test
**
** Initiates a self-test cycle. The assumption is that we have no interrupts
** active, so we should make sure that this is the case.
*****************************************************************************/
int ced_start_self_test(struct ced_data *ced)
{
	int nGot;
	mutex_lock(&ced->io_mutex);
	dev_dbg(&ced->interface->dev, "%s\n", __func__);

	ced_draw_down(ced);	/*  wait for, then kill outstanding Urbs */
	ced_flush_in_buff(ced);	/*  Clear out input buffer & pipe */
	ced_flush_out_buff(ced);	/*  Clear output buffer & pipe */
	/* so things stay tidy */
	/* ced_read_write_cancel(pDeviceObject); */
	ced->dma_flag = MODE_CHAR;	/* Clear DMA mode flags here */

	nGot = usb_control_msg(ced->udev, usb_rcvctrlpipe(ced->udev, 0),
			       DB_SELFTEST, (H_TO_D | VENDOR | DEVREQ),
			       0, 0, NULL, 0, HZ); /* allow 1 second timeout */
	ced->self_test_time = jiffies + HZ * 30;	/*  30 seconds into the future */

	mutex_unlock(&ced->io_mutex);
	if (nGot < 0)
		dev_err(&ced->interface->dev, "%s: err=%d\n", __func__, nGot);
	return nGot < 0 ? U14ERR_FAIL : U14ERR_NOERROR;
}

/****************************************************************************
** ced_check_self_test
**
** Check progress of a self-test cycle
****************************************************************************/
int ced_check_self_test(struct ced_data *ced, TGET_SELFTEST __user *pGST)
{
	unsigned int state, error;
	int iReturn;
	TGET_SELFTEST gst;	/*  local work space */
	memset(&gst, 0, sizeof(gst));	/*  clear out the space (sets code 0) */

	mutex_lock(&ced->io_mutex);

	dev_dbg(&ced->interface->dev, "%s\n", __func__);
	iReturn = ced_get_state(ced, &state, &error);
	if (iReturn == U14ERR_NOERROR)	/*  Only accept zero if it happens twice */
		iReturn = ced_get_state(ced, &state, &error);

	if (iReturn != U14ERR_NOERROR) {	/*  Self-test can cause comms errors */
				/*  so we assume still testing */
		dev_err(&ced->interface->dev,
			"%s: ced_get_state=%d, assuming still testing\n",
			__func__, iReturn);
		state = 0x80;	/*  Force still-testing, no error */
		error = 0;
		iReturn = U14ERR_NOERROR;
	}

	if ((state == -1) && (error == -1)) {	/*  If ced_get_state had problems */
		dev_err(&ced->interface->dev,
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
			dev_dbg(&ced->interface->dev,
				"Self-test error code %d\n", gst.code);
		} else {		/*  No error, check for timeout */
			unsigned long ulNow = jiffies;	/*  get current time */
			if (time_after(ulNow, ced->self_test_time)) {
				gst.code = -2;	/*  Flag the timeout */
				dev_dbg(&ced->interface->dev,
					"Self-test timed-out\n");
			} else
				dev_dbg(&ced->interface->dev,
					"Self-test on-going\n");
		}
	} else {
		gst.code = -1;	/*  Flag the test is done */
		dev_dbg(&ced->interface->dev, "Self-test done\n");
	}

	if (gst.code < 0) {	/*  If we have a problem or finished */
				/*  If using the 2890 we should reset properly */
		if ((ced->n_pipes == 4) && (ced->type <= TYPEPOWER))
			ced_is_1401(ced);	/*  Get 1401 reset and OK */
		else
			ced_quick_check(ced, true, true);	/*  Otherwise check without reset unless problems */
	}
	mutex_unlock(&ced->io_mutex);

	if (copy_to_user(pGST, &gst, sizeof(gst)))
		return -EFAULT;

	return iReturn;
}

/****************************************************************************
** ced_type_of_1401
**
** Returns code for standard, plus, micro1401, power1401 or none
****************************************************************************/
int ced_type_of_1401(struct ced_data *ced)
{
	int iReturn = TYPEUNKNOWN;
	mutex_lock(&ced->io_mutex);
	dev_dbg(&ced->interface->dev, "%s\n", __func__);

	switch (ced->type) {
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
		if ((ced->type >= TYPEPOWER) && (ced->type <= 25))
			iReturn = ced->type + 4;	/*  We can calculate types */
		else		/*   for up-coming 1401 designs */
			iReturn = TYPEUNKNOWN;	/*  Don't know or not there */
	}
	dev_dbg(&ced->interface->dev, "%s %d\n", __func__, iReturn);
	mutex_unlock(&ced->io_mutex);

	return iReturn;
}

/****************************************************************************
** ced_transfer_flags
**
** Returns flags on block transfer abilities
****************************************************************************/
int ced_transfer_flags(struct ced_data *ced)
{
	int iReturn = U14TF_MULTIA | U14TF_DIAG |	/*  we always have multiple DMA area */
	    U14TF_NOTIFY | U14TF_CIRCTH;	/*  diagnostics, notify and circular */
	dev_dbg(&ced->interface->dev, "%s\n", __func__);
	mutex_lock(&ced->io_mutex);
	if (ced->is_usb2)	/*  Set flag for USB2 if appropriate */
		iReturn |= U14TF_USB2;
	mutex_unlock(&ced->io_mutex);

	return iReturn;
}

/***************************************************************************
** ced_dbg_cmd
** Issues a debug\diagnostic command to the 1401 along with a 32-bit datum
** This is a utility command used for dbg operations.
*/
static int ced_dbg_cmd(struct ced_data *ced, unsigned char cmd,
		      unsigned int data)
{
	int iReturn;
	dev_dbg(&ced->interface->dev, "%s: entry\n", __func__);
	iReturn = usb_control_msg(ced->udev, usb_sndctrlpipe(ced->udev, 0), cmd,
				  (H_TO_D | VENDOR | DEVREQ),
				  (unsigned short)data,
				  (unsigned short)(data >> 16), NULL, 0, HZ);
						/* allow 1 second timeout */
	if (iReturn < 0)
		dev_err(&ced->interface->dev, "%s: fail code=%d\n",
			__func__, iReturn);

	return iReturn;
}

/****************************************************************************
** ced_dbg_peek
**
** Execute the diagnostic peek operation. Uses address, width and repeats.
****************************************************************************/
int ced_dbg_peek(struct ced_data *ced, TDBGBLOCK __user *pDB)
{
	int iReturn;
	TDBGBLOCK db;

	if (copy_from_user(&db, pDB, sizeof(db)))
		return -EFAULT;

	mutex_lock(&ced->io_mutex);
	dev_dbg(&ced->interface->dev, "%s: @ %08x\n", __func__, db.iAddr);

	iReturn = ced_dbg_cmd(ced, DB_SETADD, db.iAddr);
	if (iReturn == U14ERR_NOERROR)
		iReturn = ced_dbg_cmd(ced, DB_WIDTH, db.iWidth);
	if (iReturn == U14ERR_NOERROR)
		iReturn = ced_dbg_cmd(ced, DB_REPEATS, db.iRepeats);
	if (iReturn == U14ERR_NOERROR)
		iReturn = ced_dbg_cmd(ced, DB_PEEK, 0);
	mutex_unlock(&ced->io_mutex);

	return iReturn;
}

/****************************************************************************
** ced_dbg_poke
**
** Execute the diagnostic poke operation. Parameters are in the CSBLOCK struct
** in order address, size, repeats and value to poke.
****************************************************************************/
int ced_dbg_poke(struct ced_data *ced, TDBGBLOCK __user *pDB)
{
	int iReturn;
	TDBGBLOCK db;

	if (copy_from_user(&db, pDB, sizeof(db)))
		return -EFAULT;

	mutex_lock(&ced->io_mutex);
	dev_dbg(&ced->interface->dev, "%s: @ %08x\n", __func__, db.iAddr);

	iReturn = ced_dbg_cmd(ced, DB_SETADD, db.iAddr);
	if (iReturn == U14ERR_NOERROR)
		iReturn = ced_dbg_cmd(ced, DB_WIDTH, db.iWidth);
	if (iReturn == U14ERR_NOERROR)
		iReturn = ced_dbg_cmd(ced, DB_REPEATS, db.iRepeats);
	if (iReturn == U14ERR_NOERROR)
		iReturn = ced_dbg_cmd(ced, DB_POKE, db.iData);
	mutex_unlock(&ced->io_mutex);

	return iReturn;
}

/****************************************************************************
** ced_dbg_ramp_data
**
** Execute the diagnostic ramp data operation. Parameters are in the CSBLOCK struct
** in order address, default, enable mask, size and repeats.
****************************************************************************/
int ced_dbg_ramp_data(struct ced_data *ced, TDBGBLOCK __user *pDB)
{
	int iReturn;
	TDBGBLOCK db;

	if (copy_from_user(&db, pDB, sizeof(db)))
		return -EFAULT;

	mutex_lock(&ced->io_mutex);
	dev_dbg(&ced->interface->dev, "%s: @ %08x\n", __func__, db.iAddr);

	iReturn = ced_dbg_cmd(ced, DB_SETADD, db.iAddr);
	if (iReturn == U14ERR_NOERROR)
		iReturn = ced_dbg_cmd(ced, DB_SETDEF, db.iDefault);
	if (iReturn == U14ERR_NOERROR)
		iReturn = ced_dbg_cmd(ced, DB_SETMASK, db.iMask);
	if (iReturn == U14ERR_NOERROR)
		iReturn = ced_dbg_cmd(ced, DB_WIDTH, db.iWidth);
	if (iReturn == U14ERR_NOERROR)
		iReturn = ced_dbg_cmd(ced, DB_REPEATS, db.iRepeats);
	if (iReturn == U14ERR_NOERROR)
		iReturn = ced_dbg_cmd(ced, DB_RAMPD, 0);
	mutex_unlock(&ced->io_mutex);

	return iReturn;
}

/****************************************************************************
** ced_dbg_ramp_addr
**
** Execute the diagnostic ramp address operation
****************************************************************************/
int ced_dbg_ramp_addr(struct ced_data *ced, TDBGBLOCK __user *pDB)
{
	int iReturn;
	TDBGBLOCK db;

	if (copy_from_user(&db, pDB, sizeof(db)))
		return -EFAULT;

	mutex_lock(&ced->io_mutex);
	dev_dbg(&ced->interface->dev, "%s\n", __func__);

	iReturn = ced_dbg_cmd(ced, DB_SETDEF, db.iDefault);
	if (iReturn == U14ERR_NOERROR)
		iReturn = ced_dbg_cmd(ced, DB_SETMASK, db.iMask);
	if (iReturn == U14ERR_NOERROR)
		iReturn = ced_dbg_cmd(ced, DB_WIDTH, db.iWidth);
	if (iReturn == U14ERR_NOERROR)
		iReturn = ced_dbg_cmd(ced, DB_REPEATS, db.iRepeats);
	if (iReturn == U14ERR_NOERROR)
		iReturn = ced_dbg_cmd(ced, DB_RAMPA, 0);
	mutex_unlock(&ced->io_mutex);

	return iReturn;
}

/****************************************************************************
** ced_dbg_get_data
**
** Retrieve the data resulting from the last debug Peek operation
****************************************************************************/
int ced_dbg_get_data(struct ced_data *ced, TDBGBLOCK __user *pDB)
{
	int iReturn;
	TDBGBLOCK db;
	memset(&db, 0, sizeof(db));	/*  fill returned block with 0s */

	mutex_lock(&ced->io_mutex);
	dev_dbg(&ced->interface->dev, "%s\n", __func__);

	/*  Read back the last peeked value from the 1401. */
	iReturn = usb_control_msg(ced->udev, usb_rcvctrlpipe(ced->udev, 0),
				  DB_DATA, (D_TO_H | VENDOR | DEVREQ), 0, 0,
				  &db.iData, sizeof(db.iData), HZ);
	if (iReturn == sizeof(db.iData)) {
		if (copy_to_user(pDB, &db, sizeof(db)))
			iReturn = -EFAULT;
		else
			iReturn = U14ERR_NOERROR;
	} else
		dev_err(&ced->interface->dev, "%s: failed, code %d\n",
			__func__, iReturn);

	mutex_unlock(&ced->io_mutex);

	return iReturn;
}

/****************************************************************************
** ced_dbg_stop_loop
**
** Stop any never-ending debug loop, we just call ced_get_state for USB
**
****************************************************************************/
int ced_dbg_stop_loop(struct ced_data *ced)
{
	int iReturn;
	unsigned int uState, uErr;

	mutex_lock(&ced->io_mutex);
	dev_dbg(&ced->interface->dev, "%s\n", __func__);
	iReturn = ced_get_state(ced, &uState, &uErr);
	mutex_unlock(&ced->io_mutex);

	return iReturn;
}

/****************************************************************************
** ced_set_circular
**
** Sets up a transfer area record for circular transfers. If the area is
** already set, we attempt to unset it. Unsetting will fail if the area is
** booked and a transfer to that area is in progress. Otherwise, we will
** release the area and re-assign it.
****************************************************************************/
int ced_set_circular(struct ced_data *ced, struct transfer_area_desc __user *pTD)
{
	int iReturn;
	bool bToHost;
	struct transfer_area_desc td;

	if (copy_from_user(&td, pTD, sizeof(td)))
		return -EFAULT;

	mutex_lock(&ced->io_mutex);
	dev_dbg(&ced->interface->dev, "%s: area:%d, size:%08x\n",
		__func__, td.wAreaNum, td.dwLength);
	bToHost = td.eSize != 0;	/*  this is used as the tohost flag */

	/*  The strange cast is done so that we don't get warnings in 32-bit linux about the size of the */
	/*  pointer. The pointer is always passed as a 64-bit object so that we don't have problems using */
	/*  a 32-bit program on a 64-bit system. unsigned long is 64-bits on a 64-bit system. */
	iReturn =
	    ced_set_area(ced, td.wAreaNum,
		    (char __user *)((unsigned long)td.lpvBuff), td.dwLength,
		    true, bToHost);
	mutex_unlock(&ced->io_mutex);
	return iReturn;
}

/****************************************************************************
** ced_get_circ_block
**
** Return the next available block of circularly-transferred data.
****************************************************************************/
int ced_get_circ_block(struct ced_data *ced, TCIRCBLOCK __user *pCB)
{
	int iReturn = U14ERR_NOERROR;
	unsigned int nArea;
	TCIRCBLOCK cb;

	dev_dbg(&ced->interface->dev, "%s\n", __func__);

	if (copy_from_user(&cb, pCB, sizeof(cb)))
		return -EFAULT;

	mutex_lock(&ced->io_mutex);

	nArea = cb.nArea;	/*  Retrieve parameters first */
	cb.dwOffset = 0;	/*  set default result (nothing) */
	cb.dwSize = 0;

	if (nArea < MAX_TRANSAREAS) {	/*  The area number must be OK */
		/* Pointer to relevant info */
		struct transarea *pArea = &ced->trans_def[nArea];
		spin_lock_irq(&ced->staged_lock);	/*  Lock others out */

		if ((pArea->used) && (pArea->circular) &&	/*  Must be circular area */
		    (pArea->circ_to_host)) {	/*  For now at least must be to host */
			if (pArea->blocks[0].size > 0) {	/*  Got anything? */
				cb.dwOffset = pArea->blocks[0].offset;
				cb.dwSize = pArea->blocks[0].size;
				dev_dbg(&ced->interface->dev,
					"%s: return block 0: %d bytes at %d\n",
					__func__, cb.dwSize, cb.dwOffset);
			}
		} else
			iReturn = U14ERR_NOTSET;

		spin_unlock_irq(&ced->staged_lock);
	} else
		iReturn = U14ERR_BADAREA;

	if (copy_to_user(pCB, &cb, sizeof(cb)))
		iReturn = -EFAULT;

	mutex_unlock(&ced->io_mutex);
	return iReturn;
}

/****************************************************************************
** ced_free_circ_block
**
** Frees a block of circularly-transferred data and returns the next one.
****************************************************************************/
int ced_free_circ_block(struct ced_data *ced, TCIRCBLOCK __user *pCB)
{
	int iReturn = U14ERR_NOERROR;
	unsigned int nArea, uStart, uSize;
	TCIRCBLOCK cb;

	dev_dbg(&ced->interface->dev, "%s\n", __func__);

	if (copy_from_user(&cb, pCB, sizeof(cb)))
		return -EFAULT;

	mutex_lock(&ced->io_mutex);

	nArea = cb.nArea;	/*  Retrieve parameters first */
	uStart = cb.dwOffset;
	uSize = cb.dwSize;
	cb.dwOffset = 0;	/*  then set default result (nothing) */
	cb.dwSize = 0;

	if (nArea < MAX_TRANSAREAS) {	/*  The area number must be OK */
		/* Pointer to relevant info */
		struct transarea *pArea = &ced->trans_def[nArea];
		spin_lock_irq(&ced->staged_lock);	/*  Lock others out */

		if ((pArea->used) && (pArea->circular) &&	/*  Must be circular area */
		    (pArea->circ_to_host)) {	/*  For now at least must be to host */
			bool bWaiting = false;

			if ((pArea->blocks[0].size >= uSize) &&	/*  Got anything? */
			    (pArea->blocks[0].offset == uStart)) {	/*  Must be legal data */
				pArea->blocks[0].size -= uSize;
				pArea->blocks[0].offset += uSize;
				if (pArea->blocks[0].size == 0) {	/*  Have we emptied this block? */
					if (pArea->blocks[1].size) {	/*  Is there a second block? */
						pArea->blocks[0] = pArea->blocks[1];	/*  Copy down block 2 data */
						pArea->blocks[1].size = 0;	/*  and mark the second block as unused */
						pArea->blocks[1].offset = 0;
					} else
						pArea->blocks[0].offset = 0;
				}

				dev_dbg(&ced->interface->dev,
					"%s: free %d bytes at %d, return %d bytes at %d, wait=%d\n",
					__func__, uSize, uStart,
					pArea->blocks[0].size,
					pArea->blocks[0].offset,
					ced->xfer_waiting);

				/*  Return the next available block of memory as well */
				if (pArea->blocks[0].size > 0) {	/*  Got anything? */
					cb.dwOffset =
					    pArea->blocks[0].offset;
					cb.dwSize = pArea->blocks[0].size;
				}

				bWaiting = ced->xfer_waiting;
				if (bWaiting && ced->staged_urb_pending) {
					dev_err(&ced->interface->dev,
						"%s: ERROR: waiting xfer and staged Urb pending!\n",
						__func__);
					bWaiting = false;
				}
			} else {
				dev_err(&ced->interface->dev,
					"%s: ERROR: freeing %d bytes at %d, block 0 is %d bytes at %d\n",
					__func__, uSize, uStart,
					pArea->blocks[0].size,
					pArea->blocks[0].offset);
				iReturn = U14ERR_NOMEMORY;
			}

			/*  If we have one, kick off pending transfer */
			if (bWaiting) {	/*  Got a block xfer waiting? */
				int RWMStat =
				    ced_read_write_mem(ced,
						       !ced->dma_info.outward,
						       ced->dma_info.ident,
						       ced->dma_info.offset,
						       ced->dma_info.size);
				if (RWMStat != U14ERR_NOERROR)
					dev_err(&ced->interface->dev,
						"%s: rw setup failed %d\n",
						__func__, RWMStat);
			}
		} else
			iReturn = U14ERR_NOTSET;

		spin_unlock_irq(&ced->staged_lock);
	} else
		iReturn = U14ERR_BADAREA;

	if (copy_to_user(pCB, &cb, sizeof(cb)))
		iReturn = -EFAULT;

	mutex_unlock(&ced->io_mutex);
	return iReturn;
}
