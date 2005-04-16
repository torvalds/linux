/* Driver for Microtech DPCM-USB CompactFlash/SmartMedia reader
 *
 * $Id: dpcm.c,v 1.4 2001/06/11 02:54:25 mdharm Exp $
 *
 * DPCM driver v0.1:
 *
 * First release
 *
 * Current development and maintenance by:
 *   (c) 2000 Brian Webb (webbb@earthlink.net)
 *
 * This device contains both a CompactFlash card reader, which
 * uses the Control/Bulk w/o Interrupt protocol and
 * a SmartMedia card reader that uses the same protocol
 * as the SDDR09.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>

#include "usb.h"
#include "transport.h"
#include "protocol.h"
#include "debug.h"
#include "dpcm.h"
#include "sddr09.h"

/*
 * Transport for the Microtech DPCM-USB
 *
 */
int dpcm_transport(struct scsi_cmnd *srb, struct us_data *us)
{
  int ret;

  if(srb == NULL)
    return USB_STOR_TRANSPORT_ERROR;

  US_DEBUGP("dpcm_transport: LUN=%d\n", srb->device->lun);

  switch(srb->device->lun) {
  case 0:

    /*
     * LUN 0 corresponds to the CompactFlash card reader.
     */
    ret = usb_stor_CB_transport(srb, us);
    break;

#ifdef CONFIG_USB_STORAGE_SDDR09
  case 1:

    /*
     * LUN 1 corresponds to the SmartMedia card reader.
     */

    /*
     * Set the LUN to 0 (just in case).
     */
    srb->device->lun = 0; us->srb->device->lun = 0;
    ret = sddr09_transport(srb, us);
    srb->device->lun = 1; us->srb->device->lun = 1;
    break;

#endif

  default:
    US_DEBUGP("dpcm_transport: Invalid LUN %d\n", srb->device->lun);
    ret = USB_STOR_TRANSPORT_ERROR;
    break;
  }
  return ret;
}
