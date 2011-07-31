/******************************************************************************
*                                                                             *
*  easycap_main.c                                                             *
*                                                                             *
*  Video driver for EasyCAP USB2.0 Video Capture Device DC60                  *
*                                                                             *
*                                                                             *
******************************************************************************/
/*
 *
 *  Copyright (C) 2010 R.M. Thomas <rmthomas@sciolus.org>
 *
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
*/
/*****************************************************************************/

#include "easycap.h"
#include "easycap_standard.h"

int easycap_debug;
module_param(easycap_debug, int, S_IRUGO | S_IWUSR);

/*---------------------------------------------------------------------------*/
/*
 *  PARAMETERS APPLICABLE TO ENTIRE DRIVER, I.E. BOTH VIDEO AND AUDIO
 */
/*---------------------------------------------------------------------------*/
struct usb_device_id easycap_usb_device_id_table[] = {
{ USB_DEVICE(USB_EASYCAP_VENDOR_ID, USB_EASYCAP_PRODUCT_ID) },
{ }
};
MODULE_DEVICE_TABLE(usb, easycap_usb_device_id_table);
struct usb_driver easycap_usb_driver = {
.name = "easycap",
.id_table = easycap_usb_device_id_table,
.probe = easycap_usb_probe,
.disconnect = easycap_usb_disconnect,
};
/*---------------------------------------------------------------------------*/
/*
 *  PARAMETERS USED WHEN REGISTERING THE VIDEO INTERFACE
 *
 *  NOTE: SOME KERNELS IGNORE usb_class_driver.minor_base, AS MENTIONED BY
 *        CORBET ET AL. "LINUX DEVICE DRIVERS", 3rd EDITION, PAGE 253.
 *        THIS IS THE CASE FOR OpenSUSE.
 */
/*---------------------------------------------------------------------------*/
const struct file_operations easycap_fops = {
	.owner		= THIS_MODULE,
	.open		= easycap_open,
	.release	= easycap_release,
	.unlocked_ioctl	= easycap_ioctl,
	.poll		= easycap_poll,
	.mmap		= easycap_mmap,
	.llseek		= no_llseek,
};
struct vm_operations_struct easycap_vm_ops = {
.open  = easycap_vma_open,
.close = easycap_vma_close,
.fault = easycap_vma_fault,
};
struct usb_class_driver easycap_class = {
.name = "usb/easycap%d",
.fops = &easycap_fops,
.minor_base = USB_SKEL_MINOR_BASE,
};

/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
#if defined(EASYCAP_IS_VIDEODEV_CLIENT)
#if defined(EASYCAP_NEEDS_V4L2_FOPS)
const struct v4l2_file_operations v4l2_fops = {
	.owner		= THIS_MODULE,
	.open		= easycap_open_noinode,
	.release	= easycap_release_noinode,
	.unlocked_ioctl	= easycap_ioctl,
	.poll		= easycap_poll,
	.mmap		= easycap_mmap,
};
#endif /*EASYCAP_NEEDS_V4L2_FOPS*/
int video_device_many /*=0*/;
struct video_device *pvideo_array[VIDEO_DEVICE_MANY], *pvideo_device;
#endif /*EASYCAP_IS_VIDEODEV_CLIENT*/
/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/

/*--------------------------------------------------------------------------*/
/*
 *  PARAMETERS USED WHEN REGISTERING THE AUDIO INTERFACE
 */
/*--------------------------------------------------------------------------*/
const struct file_operations easysnd_fops = {
	.owner		= THIS_MODULE,
	.open		= easysnd_open,
	.release	= easysnd_release,
	.unlocked_ioctl	= easysnd_ioctl,
	.read		= easysnd_read,
	.llseek		= no_llseek,
};
struct usb_class_driver easysnd_class = {
.name = "usb/easysnd%d",
.fops = &easysnd_fops,
.minor_base = USB_SKEL_MINOR_BASE,
};
/****************************************************************************/
/*--------------------------------------------------------------------------*/
/*
 *  IT IS NOT APPROPRIATE FOR easycap_open() TO SUBMIT THE VIDEO URBS HERE,
 *  BECAUSE THERE WILL ALWAYS BE SUBSEQUENT NEGOTIATION OF TV STANDARD AND
 *  FORMAT BY IOCTL AND IT IS INADVISABLE TO HAVE THE URBS RUNNING WHILE
 *  REGISTERS OF THE SA7113H ARE BEING MANIPULATED.
 *
 *  THE SUBMISSION OF VIDEO URBS IS THEREFORE DELAYED UNTIL THE IOCTL COMMAND
 *  STREAMON IS RECEIVED.
 */
/*--------------------------------------------------------------------------*/
/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
#if defined(EASYCAP_IS_VIDEODEV_CLIENT)
int
easycap_open_noinode(struct file *file)
{
return easycap_open((struct inode *)NULL, file);
}
#endif /*EASYCAP_IS_VIDEODEV_CLIENT*/
/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
int
easycap_open(struct inode *inode, struct file *file)
{
#if (!defined(EASYCAP_IS_VIDEODEV_CLIENT))
struct usb_interface *pusb_interface;
#endif /*EASYCAP_IS_VIDEODEV_CLIENT*/
struct usb_device *p;
struct easycap *peasycap;
int i, k, m, rc;

JOT(4, "\n");
SAY("==========OPEN=========\n");

peasycap = (struct easycap *)NULL;
#if (!defined(EASYCAP_IS_VIDEODEV_CLIENT))
if ((struct inode *)NULL == inode) {
	SAY("ERROR: inode is NULL.\n");
	return -EFAULT;
}
pusb_interface = usb_find_interface(&easycap_usb_driver, iminor(inode));
if (!pusb_interface) {
	SAY("ERROR: pusb_interface is NULL.\n");
	return -EFAULT;
}
peasycap = usb_get_intfdata(pusb_interface);
/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
#else
for (i = 0;  i < video_device_many;  i++) {
	pvideo_device = pvideo_array[i];
	if ((struct video_device *)NULL != pvideo_device) {
		peasycap = (struct easycap *)video_get_drvdata(pvideo_device);
		break;
	}
}
/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
#endif /*EASYCAP_IS_VIDEODEV_CLIENT*/
if ((struct easycap *)NULL == peasycap) {
	SAY("MISTAKE: peasycap is NULL\n");
	return -EFAULT;
}
file->private_data = peasycap;
/*---------------------------------------------------------------------------*/
/*
 *  INITIALIZATION
 */
/*---------------------------------------------------------------------------*/
JOT(4, "starting initialization\n");

for (k = 0;  k < FRAME_BUFFER_MANY;  k++) {
	for (m = 0;  m < FRAME_BUFFER_SIZE/PAGE_SIZE;  m++)
		memset(peasycap->frame_buffer[k][m].pgo, 0, PAGE_SIZE);
}
p = peasycap->pusb_device;
if ((struct usb_device *)NULL == p) {
	SAY("ERROR: peasycap->pusb_device is NULL\n");
	return -EFAULT;
} else {
	JOT(16, "0x%08lX=peasycap->pusb_device\n", \
					(long int)peasycap->pusb_device);
}
rc = wakeup_device(peasycap->pusb_device);
if (0 == rc)
	JOT(8, "wakeup_device() OK\n");
else {
	SAY("ERROR: wakeup_device() returned %i\n", rc);
	return -EFAULT;
}
rc = setup_stk(p);  peasycap->input = 0;
if (0 == rc)
	JOT(8, "setup_stk() OK\n");
else {
	SAY("ERROR: setup_stk() returned %i\n", rc);
	return -EFAULT;
}
rc = setup_saa(p);
if (0 == rc)
	JOT(8, "setup_saa() OK\n");
else {
	SAY("ERROR: setup_saa() returned %i\n", rc);
	return -EFAULT;
}
rc = check_saa(p);
if (0 == rc)
	JOT(8, "check_saa() OK\n");
else if (-8 < rc)
	SAY("check_saa() returned %i\n", rc);
else {
	SAY("ERROR: check_saa() returned %i\n", rc);
	return -EFAULT;
}
peasycap->standard_offset = -1;
/*---------------------------------------------------------------------------*/
#if defined(PREFER_NTSC)

rc = adjust_standard(peasycap, V4L2_STD_NTSC_M);
if (0 == rc)
	JOT(8, "adjust_standard(.,NTSC_M) OK\n");
else {
	SAY("ERROR: adjust_standard(.,NTSC_M) returned %i\n", rc);
	return -EFAULT;
}
rc = adjust_format(peasycap, 640, 480, V4L2_PIX_FMT_UYVY, V4L2_FIELD_NONE, \
									false);
if (0 <= rc)
	JOT(8, "adjust_format(.,640,480,UYVY) OK\n");
else {
	SAY("ERROR: adjust_format(.,640,480,UYVY) returned %i\n", rc);
	return -EFAULT;
}

#else

rc = adjust_standard(peasycap, \
		(V4L2_STD_PAL_B | V4L2_STD_PAL_G | V4L2_STD_PAL_H | \
		V4L2_STD_PAL_I | V4L2_STD_PAL_N));
if (0 == rc)
	JOT(8, "adjust_standard(.,PAL_BGHIN) OK\n");
else {
	SAY("ERROR: adjust_standard(.,PAL_BGHIN) returned %i\n", rc);
	return -EFAULT;
}
rc = adjust_format(peasycap, 640, 480, V4L2_PIX_FMT_UYVY, V4L2_FIELD_NONE, \
									false);
if (0 <= rc)
	JOT(8, "adjust_format(.,640,480,uyvy,false) OK\n");
else {
	SAY("ERROR: adjust_format(.,640,480,uyvy,false) returned %i\n", rc);
	return -EFAULT;
}

#endif /* !PREFER_NTSC*/
/*---------------------------------------------------------------------------*/
rc = adjust_brightness(peasycap, -8192);
if (0 != rc) {
	SAY("ERROR: adjust_brightness(default) returned %i\n", rc);
	return -EFAULT;
}
rc = adjust_contrast(peasycap, -8192);
if (0 != rc) {
	SAY("ERROR: adjust_contrast(default) returned %i\n", rc);
	return -EFAULT;
}
rc = adjust_saturation(peasycap, -8192);
if (0 != rc) {
	SAY("ERROR: adjust_saturation(default) returned %i\n", rc);
	return -EFAULT;
}
rc = adjust_hue(peasycap, -8192);
if (0 != rc) {
	SAY("ERROR: adjust_hue(default) returned %i\n", rc);
	return -EFAULT;
}
/*---------------------------------------------------------------------------*/
rc = usb_set_interface(peasycap->pusb_device, peasycap->video_interface, \
						peasycap->video_altsetting_on);
if (0 == rc)
	JOT(8, "usb_set_interface(.,%i,%i) OK\n", peasycap->video_interface, \
						peasycap->video_altsetting_on);
else {
	SAY("ERROR: usb_set_interface() returned %i\n", rc);
	return -EFAULT;
}
rc = start_100(p);
if (0 == rc)
	JOT(8, "start_100() OK\n");
else {
	SAY("ERROR: start_100() returned %i\n", rc);
	return -EFAULT;
}
peasycap->video_isoc_sequence = VIDEO_ISOC_BUFFER_MANY - 1;
peasycap->video_idle = 0;
peasycap->video_junk = 0;
for (i = 0; i < 180; i++)
	peasycap->merit[i] = 0;
peasycap->video_eof = 0;
peasycap->audio_eof = 0;

do_gettimeofday(&peasycap->timeval7);

peasycap->fudge = 0;

JOT(4, "finished initialization\n");
return 0;
}
/*****************************************************************************/
int
submit_video_urbs(struct easycap *peasycap)
{
struct data_urb *pdata_urb;
struct urb *purb;
struct list_head *plist_head;
int j, isbad, m, rc;
int isbuf;

if ((struct list_head *)NULL == peasycap->purb_video_head) {
	SAY("ERROR: peasycap->urb_video_head uninitialized\n");
	return -EFAULT;
}
if ((struct usb_device *)NULL == peasycap->pusb_device) {
	SAY("ERROR: peasycap->pusb_device is NULL\n");
	return -EFAULT;
}
if (!peasycap->video_isoc_streaming) {








	JOT(4, "submission of all video urbs\n");
	if (0 != ready_saa(peasycap->pusb_device)) {
		SAY("ERROR: not ready to capture after waiting " \
							"one second\n");
		SAY(".....  continuing anyway\n");
	}
	isbad = 0;  m = 0;
	list_for_each(plist_head, (peasycap->purb_video_head)) {
		pdata_urb = list_entry(plist_head, struct data_urb, list_head);
		if (NULL != pdata_urb) {
			purb = pdata_urb->purb;
			if (NULL != purb) {
				isbuf = pdata_urb->isbuf;
				purb->interval = 1;
				purb->dev = peasycap->pusb_device;
				purb->pipe = \
					usb_rcvisocpipe(peasycap->pusb_device,\
					peasycap->video_endpointnumber);
				purb->transfer_flags = URB_ISO_ASAP;
				purb->transfer_buffer = \
					peasycap->video_isoc_buffer[isbuf].pgo;
				purb->transfer_buffer_length = \
					peasycap->video_isoc_buffer_size;
				purb->complete = easycap_complete;
				purb->context = peasycap;
				purb->start_frame = 0;
				purb->number_of_packets = \
					peasycap->video_isoc_framesperdesc;

				for (j = 0;  j < peasycap->\
					video_isoc_framesperdesc; j++) {
						purb->iso_frame_desc[j].\
						offset = j * \
						peasycap->\
						video_isoc_maxframesize;
						purb->iso_frame_desc[j].\
						length = peasycap->\
						video_isoc_maxframesize;
					}

				rc = usb_submit_urb(purb, GFP_KERNEL);
				if (0 != rc) {
					isbad++;
					SAY("ERROR: usb_submit_urb() failed " \
							"for urb with rc:\n");
					switch (rc) {
					case -ENOMEM: {
						SAY("ENOMEM\n");
						break;
					}
					case -ENODEV: {
						SAY("ENODEV\n");
						break;
					}
					case -ENXIO: {
						SAY("ENXIO\n");
						break;
					}
					case -EINVAL: {
						SAY("EINVAL\n");
						break;
					}
					case -EAGAIN: {
						SAY("EAGAIN\n");
						break;
					}
					case -EFBIG: {
						SAY("EFBIG\n");
						break;
					}
					case -EPIPE: {
						SAY("EPIPE\n");
						break;
					}
					case -EMSGSIZE: {
						SAY("EMSGSIZE\n");
						break;
					}
					default: {
						SAY("unknown error code %i\n",\
									 rc);
						break;
					}
					}
				} else {
					m++;
				}
				} else {
					isbad++;
				}
			} else {
				 isbad++;
			}
		}
	if (isbad) {
		JOT(4, "attempting cleanup instead of submitting\n");
		list_for_each(plist_head, (peasycap->purb_video_head)) {
			pdata_urb = list_entry(plist_head, struct data_urb, \
								list_head);
			if (NULL != pdata_urb) {
				purb = pdata_urb->purb;
				if (NULL != purb)
					usb_kill_urb(purb);
			}
		}
		peasycap->video_isoc_streaming = 0;
	} else {
		peasycap->video_isoc_streaming = 1;
		JOT(4, "submitted %i video urbs\n", m);
	}






} else {
	JOT(4, "already streaming video urbs\n");
}
return 0;
}
/*****************************************************************************/
int
kill_video_urbs(struct easycap *peasycap)
{
int m;
struct list_head *plist_head;
struct data_urb *pdata_urb;

if ((struct easycap *)NULL == peasycap) {
	SAY("ERROR: peasycap is NULL\n");
	return -EFAULT;
}
if (peasycap->video_isoc_streaming) {



	if ((struct list_head *)NULL != peasycap->purb_video_head) {
		peasycap->video_isoc_streaming = 0;
		JOT(4, "killing video urbs\n");
		m = 0;
		list_for_each(plist_head, (peasycap->purb_video_head)) {
			pdata_urb = list_entry(plist_head, struct data_urb, \
								list_head);
			if ((struct data_urb *)NULL != pdata_urb) {
				if ((struct urb *)NULL != pdata_urb->purb) {
					usb_kill_urb(pdata_urb->purb);
					m++;
				}
			}
		}
		JOT(4, "%i video urbs killed\n", m);
	} else {
		SAY("ERROR: peasycap->purb_video_head is NULL\n");
		return -EFAULT;
	}
} else {
	JOT(8, "%i=video_isoc_streaming, no video urbs killed\n", \
					peasycap->video_isoc_streaming);
}
return 0;
}
/****************************************************************************/
/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
#if defined(EASYCAP_IS_VIDEODEV_CLIENT)
int
easycap_release_noinode(struct file *file)
{
return easycap_release((struct inode *)NULL, file);
}
#endif /*EASYCAP_IS_VIDEODEV_CLIENT*/
/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
/*--------------------------------------------------------------------------*/
int
easycap_release(struct inode *inode, struct file *file)
{
#if (!defined(EASYCAP_IS_VIDEODEV_CLIENT))
struct easycap *peasycap;

JOT(4, "\n");

peasycap = file->private_data;
if (NULL == peasycap) {
	SAY("ERROR:  peasycap is NULL.\n");
	SAY("ending unsuccessfully\n");
	return -EFAULT;
}
if (0 != kill_video_urbs(peasycap)) {
	SAY("ERROR: kill_video_urbs() failed\n");
	return -EFAULT;
}
JOT(4, "ending successfully\n");
/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
#else
#
/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
#endif /*EASYCAP_IS_VIDEODEV_CLIENT*/

return 0;
}
/****************************************************************************/
/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
#if defined(EASYCAP_IS_VIDEODEV_CLIENT)
int
videodev_release(struct video_device *pvd)
{
struct easycap *peasycap;
int i, j, k;

JOT(4, "\n");

k = 0;
for (i = 0;  i < video_device_many;  i++) {
	pvideo_device = pvideo_array[i];
	if ((struct video_device *)NULL != pvideo_device) {
		if (pvd->minor == pvideo_device->minor) {
			peasycap = (struct easycap *)\
					video_get_drvdata(pvideo_device);
			if ((struct easycap *)NULL == peasycap) {
				SAY("ERROR:  peasycap is NULL\n");
				SAY("ending unsuccessfully\n");
				return -EFAULT;
			}
			if (0 != kill_video_urbs(peasycap)) {
				SAY("ERROR: kill_video_urbs() failed\n");
				return -EFAULT;
			}
			JOT(4, "freeing video_device structure: " \
							"/dev/video%i\n", i);
			kfree((void *)pvideo_device);
			for (j = i;  j < (VIDEO_DEVICE_MANY - 1);  j++)
				pvideo_array[j] = pvideo_array[j + 1];
			video_device_many--;  k++;
			break;
		}
	}
}
if (!k) {
	SAY("ERROR: lost video_device structure for %i=minor\n", pvd->minor);
	SAY("cannot free: may cause memory leak\n");
	SAY("ending unsuccessfully\n");
	return -EFAULT;
}

JOT(4, "ending successfully\n");
return 0;
}
#endif /*EASYCAP_IS_VIDEODEV_CLIENT*/
/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
/****************************************************************************/
/*--------------------------------------------------------------------------*/
/*
 *  THIS FUNCTION IS CALLED FROM WITHIN easycap_usb_disconnect().
 *  BY THIS STAGE THE DEVICE HAS ALREADY BEEN PHYSICALLY UNPLUGGED.
 *  peasycap->pusb_device IS NO LONGER VALID AND SHOULD HAVE BEEN SET TO NULL.
 */
/*---------------------------------------------------------------------------*/
void
easycap_delete(struct kref *pkref)
{
int k, m, lost;
int allocation_video_urb, allocation_video_page, allocation_video_struct;
int allocation_audio_urb, allocation_audio_page, allocation_audio_struct;
int registered_video, registered_audio;
struct easycap *peasycap;
struct data_urb *pdata_urb;
struct list_head *plist_head, *plist_next;

JOT(4, "\n");

peasycap = container_of(pkref, struct easycap, kref);
if ((struct easycap *)NULL == peasycap) {
	SAY("ERROR: peasycap is NULL: cannot perform deletions\n");
	return;
}
/*---------------------------------------------------------------------------*/
/*
 *  FREE VIDEO.
 */
/*---------------------------------------------------------------------------*/
if ((struct list_head *)NULL != peasycap->purb_video_head) {
	JOT(4, "freeing video urbs\n");
	m = 0;
	list_for_each(plist_head, (peasycap->purb_video_head)) {
		pdata_urb = list_entry(plist_head, struct data_urb, list_head);
		if (NULL == pdata_urb)
			JOT(4, "ERROR: pdata_urb is NULL\n");
		else {
			if ((struct urb *)NULL != pdata_urb->purb) {
				usb_free_urb(pdata_urb->purb);
				pdata_urb->purb = (struct urb *)NULL;
				peasycap->allocation_video_urb -= 1;
				m++;
			}
		}
	}

	JOT(4, "%i video urbs freed\n", m);
/*---------------------------------------------------------------------------*/
	JOT(4, "freeing video data_urb structures.\n");
	m = 0;
	list_for_each_safe(plist_head, plist_next, peasycap->purb_video_head) {
		pdata_urb = list_entry(plist_head, struct data_urb, list_head);
		if ((struct data_urb *)NULL != pdata_urb) {
			kfree(pdata_urb);  pdata_urb = (struct data_urb *)NULL;
			peasycap->allocation_video_struct -= \
						sizeof(struct data_urb);
			m++;
		}
	}
	JOT(4, "%i video data_urb structures freed\n", m);
	JOT(4, "setting peasycap->purb_video_head=NULL\n");
	peasycap->purb_video_head = (struct list_head *)NULL;
	} else {
JOT(4, "peasycap->purb_video_head is NULL\n");
}
/*---------------------------------------------------------------------------*/
JOT(4, "freeing video isoc buffers.\n");
m = 0;
for (k = 0;  k < VIDEO_ISOC_BUFFER_MANY;  k++) {
	if ((void *)NULL != peasycap->video_isoc_buffer[k].pgo) {
		free_pages((unsigned long)\
				(peasycap->video_isoc_buffer[k].pgo), \
				VIDEO_ISOC_ORDER);
		peasycap->video_isoc_buffer[k].pgo = (void *)NULL;
		peasycap->allocation_video_page -= \
				((unsigned int)(0x01 << VIDEO_ISOC_ORDER));
		m++;
	}
}
JOT(4, "isoc video buffers freed: %i pages\n", m * (0x01 << VIDEO_ISOC_ORDER));
/*---------------------------------------------------------------------------*/
JOT(4, "freeing video field buffers.\n");
lost = 0;
for (k = 0;  k < FIELD_BUFFER_MANY;  k++) {
	for (m = 0;  m < FIELD_BUFFER_SIZE/PAGE_SIZE;  m++) {
		if ((void *)NULL != peasycap->field_buffer[k][m].pgo) {
			free_page((unsigned long)\
					(peasycap->field_buffer[k][m].pgo));
			peasycap->field_buffer[k][m].pgo = (void *)NULL;
			peasycap->allocation_video_page -= 1;
			lost++;
		}
	}
}
JOT(4, "video field buffers freed: %i pages\n", lost);
/*---------------------------------------------------------------------------*/
JOT(4, "freeing video frame buffers.\n");
lost = 0;
for (k = 0;  k < FRAME_BUFFER_MANY;  k++) {
	for (m = 0;  m < FRAME_BUFFER_SIZE/PAGE_SIZE;  m++) {
		if ((void *)NULL != peasycap->frame_buffer[k][m].pgo) {
			free_page((unsigned long)\
					(peasycap->frame_buffer[k][m].pgo));
			peasycap->frame_buffer[k][m].pgo = (void *)NULL;
			peasycap->allocation_video_page -= 1;
			lost++;
		}
	}
}
JOT(4, "video frame buffers freed: %i pages\n", lost);
/*---------------------------------------------------------------------------*/
/*
 *  FREE AUDIO.
 */
/*---------------------------------------------------------------------------*/
if ((struct list_head *)NULL != peasycap->purb_audio_head) {
	JOT(4, "freeing audio urbs\n");
	m = 0;
	list_for_each(plist_head, (peasycap->purb_audio_head)) {
		pdata_urb = list_entry(plist_head, struct data_urb, list_head);
		if (NULL == pdata_urb)
			JOT(4, "ERROR: pdata_urb is NULL\n");
		else {
			if ((struct urb *)NULL != pdata_urb->purb) {
				usb_free_urb(pdata_urb->purb);
				pdata_urb->purb = (struct urb *)NULL;
				peasycap->allocation_audio_urb -= 1;
				m++;
			}
		}
	}
	JOT(4, "%i audio urbs freed\n", m);
/*---------------------------------------------------------------------------*/
	JOT(4, "freeing audio data_urb structures.\n");
	m = 0;
	list_for_each_safe(plist_head, plist_next, peasycap->purb_audio_head) {
		pdata_urb = list_entry(plist_head, struct data_urb, list_head);
		if ((struct data_urb *)NULL != pdata_urb) {
			kfree(pdata_urb);  pdata_urb = (struct data_urb *)NULL;
			peasycap->allocation_audio_struct -= \
						sizeof(struct data_urb);
			m++;
		}
	}
JOT(4, "%i audio data_urb structures freed\n", m);
JOT(4, "setting peasycap->purb_audio_head=NULL\n");
peasycap->purb_audio_head = (struct list_head *)NULL;
} else {
JOT(4, "peasycap->purb_audio_head is NULL\n");
}
/*---------------------------------------------------------------------------*/
JOT(4, "freeing audio isoc buffers.\n");
m = 0;
for (k = 0;  k < AUDIO_ISOC_BUFFER_MANY;  k++) {
	if ((void *)NULL != peasycap->audio_isoc_buffer[k].pgo) {
		free_pages((unsigned long)\
				(peasycap->audio_isoc_buffer[k].pgo), \
				AUDIO_ISOC_ORDER);
		peasycap->audio_isoc_buffer[k].pgo = (void *)NULL;
		peasycap->allocation_audio_page -= \
				((unsigned int)(0x01 << AUDIO_ISOC_ORDER));
		m++;
	}
}
JOT(4, "easysnd_delete(): isoc audio buffers freed: %i pages\n", \
					m * (0x01 << AUDIO_ISOC_ORDER));
/*---------------------------------------------------------------------------*/
JOT(4, "freeing audio buffers.\n");
lost = 0;
for (k = 0;  k < peasycap->audio_buffer_page_many;  k++) {
	if ((void *)NULL != peasycap->audio_buffer[k].pgo) {
		free_page((unsigned long)(peasycap->audio_buffer[k].pgo));
		peasycap->audio_buffer[k].pgo = (void *)NULL;
		peasycap->allocation_audio_page -= 1;
		lost++;
	}
}
JOT(4, "easysnd_delete(): audio buffers freed: %i pages\n", lost);
/*---------------------------------------------------------------------------*/
JOT(4, "freeing easycap structure.\n");
allocation_video_urb    = peasycap->allocation_video_urb;
allocation_video_page   = peasycap->allocation_video_page;
allocation_video_struct = peasycap->allocation_video_struct;
registered_video        = peasycap->registered_video;
allocation_audio_urb    = peasycap->allocation_audio_urb;
allocation_audio_page   = peasycap->allocation_audio_page;
allocation_audio_struct = peasycap->allocation_audio_struct;
registered_audio        = peasycap->registered_audio;
m = 0;
if ((struct easycap *)NULL != peasycap) {
	kfree(peasycap);  peasycap = (struct easycap *)NULL;
	allocation_video_struct -= sizeof(struct easycap);
	m++;
}
JOT(4, "%i easycap structure freed\n", m);
/*---------------------------------------------------------------------------*/

SAY("%8i= video urbs     after all deletions\n", allocation_video_urb);
SAY("%8i= video pages    after all deletions\n", allocation_video_page);
SAY("%8i= video structs  after all deletions\n", allocation_video_struct);
SAY("%8i= video devices  after all deletions\n", registered_video);
SAY("%8i= audio urbs     after all deletions\n", allocation_audio_urb);
SAY("%8i= audio pages    after all deletions\n", allocation_audio_page);
SAY("%8i= audio structs  after all deletions\n", allocation_audio_struct);
SAY("%8i= audio devices  after all deletions\n", registered_audio);

JOT(4, "ending.\n");
return;
}
/*****************************************************************************/
unsigned int easycap_poll(struct file *file, poll_table *wait)
{
struct easycap *peasycap;

JOT(8, "\n");

if (NULL == ((poll_table *)wait))
	JOT(8, "WARNING:  poll table pointer is NULL ... continuing\n");
if (NULL == ((struct file *)file)) {
	SAY("ERROR:  file pointer is NULL\n");
	return -EFAULT;
}
peasycap = file->private_data;
if (NULL == peasycap) {
	SAY("ERROR:  peasycap is NULL\n");
	return -EFAULT;
}
peasycap->polled = 1;

if (0 == easycap_dqbuf(peasycap, 0))
	return POLLIN | POLLRDNORM;
else
	return POLLERR;

}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  IF mode IS NONZERO THIS ROUTINE RETURNS -EAGAIN RATHER THAN BLOCKING.
 */
/*---------------------------------------------------------------------------*/
int
easycap_dqbuf(struct easycap *peasycap, int mode)
{
int miss, rc;

JOT(8, "\n");

if (NULL == peasycap) {
	SAY("ERROR:  peasycap is NULL\n");
	return -EFAULT;
}
/*---------------------------------------------------------------------------*/
/*
 *  WAIT FOR FIELD 0
 */
/*---------------------------------------------------------------------------*/
miss = 0;
if (mutex_lock_interruptible(&(peasycap->mutex_mmap_video[0])))
	return -ERESTARTSYS;
while ((peasycap->field_read == peasycap->field_fill) || \
				(0 != (0xFF00 & peasycap->field_buffer\
					[peasycap->field_read][0].kount)) || \
				(0 != (0x00FF & peasycap->field_buffer\
					[peasycap->field_read][0].kount))) {
	mutex_unlock(&(peasycap->mutex_mmap_video[0]));

	if (mode)
		return -EAGAIN;

	JOT(8, "first wait  on wq_video, " \
				"%i=field_read  %i=field_fill\n", \
				peasycap->field_read, peasycap->field_fill);

	msleep(1);
	if (0 != (wait_event_interruptible(peasycap->wq_video, \
			(peasycap->video_idle || peasycap->video_eof  || \
			((peasycap->field_read != peasycap->field_fill) && \
				(0 == (0xFF00 & peasycap->field_buffer\
					[peasycap->field_read][0].kount)) && \
				(0 == (0x00FF & peasycap->field_buffer\
					[peasycap->field_read][0].kount))))))){
		SAY("aborted by signal\n");
		return -EIO;
		}
	if (peasycap->video_idle) {
		JOT(8, "%i=peasycap->video_idle\n", peasycap->video_idle);
		return -EIO;
	}
	if (peasycap->video_eof) {
		JOT(8, "%i=peasycap->video_eof\n", peasycap->video_eof);
		debrief(peasycap);
		kill_video_urbs(peasycap);
		return -EIO;
	}
miss++;
if (mutex_lock_interruptible(&(peasycap->mutex_mmap_video[0])))
	return -ERESTARTSYS;
}
mutex_unlock(&(peasycap->mutex_mmap_video[0]));
JOT(8, "first awakening on wq_video after %i waits\n", miss);

rc = field2frame(peasycap);
if (0 != rc)
	SAY("ERROR: field2frame() returned %i\n", rc);

if (true == peasycap->offerfields) {
	peasycap->frame_read = peasycap->frame_fill;
	(peasycap->frame_fill)++;
	if (peasycap->frame_buffer_many <= peasycap->frame_fill)
		peasycap->frame_fill = 0;

	if (0x01 & easycap_standard[peasycap->standard_offset].mask) {
		peasycap->frame_buffer[peasycap->frame_read][0].kount = \
							V4L2_FIELD_BOTTOM;
	} else {
		peasycap->frame_buffer[peasycap->frame_read][0].kount = \
							V4L2_FIELD_TOP;
	}
JOT(8, "setting:    %i=peasycap->frame_read\n", peasycap->frame_read);
JOT(8, "bumped to:  %i=peasycap->frame_fill\n", peasycap->frame_fill);
}
/*---------------------------------------------------------------------------*/
/*
 *  WAIT FOR FIELD 1
 */
/*---------------------------------------------------------------------------*/
miss = 0;
if (mutex_lock_interruptible(&(peasycap->mutex_mmap_video[0])))
	return -ERESTARTSYS;
while ((peasycap->field_read == peasycap->field_fill) || \
				(0 != (0xFF00 & peasycap->field_buffer\
					[peasycap->field_read][0].kount)) || \
				(0 == (0x00FF & peasycap->field_buffer\
					[peasycap->field_read][0].kount))) {
	mutex_unlock(&(peasycap->mutex_mmap_video[0]));

	if (mode)
		return -EAGAIN;

	JOT(8, "second wait on wq_video, " \
				"%i=field_read  %i=field_fill\n", \
				peasycap->field_read, peasycap->field_fill);
	msleep(1);
	if (0 != (wait_event_interruptible(peasycap->wq_video, \
			(peasycap->video_idle || peasycap->video_eof  || \
			((peasycap->field_read != peasycap->field_fill) && \
				(0 == (0xFF00 & peasycap->field_buffer\
					[peasycap->field_read][0].kount)) && \
				(0 != (0x00FF & peasycap->field_buffer\
					[peasycap->field_read][0].kount))))))){
		SAY("aborted by signal\n");
		return -EIO;
	}
	if (peasycap->video_idle) {
		JOT(8, "%i=peasycap->video_idle\n", peasycap->video_idle);
		return -EIO;
	}
	if (peasycap->video_eof) {
		JOT(8, "%i=peasycap->video_eof\n", peasycap->video_eof);
		debrief(peasycap);
		kill_video_urbs(peasycap);
		return -EIO;
	}
miss++;
if (mutex_lock_interruptible(&(peasycap->mutex_mmap_video[0])))
	return -ERESTARTSYS;
}
mutex_unlock(&(peasycap->mutex_mmap_video[0]));
JOT(8, "second awakening on wq_video after %i waits\n", miss);

rc = field2frame(peasycap);
if (0 != rc)
	SAY("ERROR: field2frame() returned %i\n", rc);

peasycap->frame_read = peasycap->frame_fill;
peasycap->queued[peasycap->frame_read] = 0;
peasycap->done[peasycap->frame_read]   = V4L2_BUF_FLAG_DONE;

(peasycap->frame_fill)++;
if (peasycap->frame_buffer_many <= peasycap->frame_fill)
	peasycap->frame_fill = 0;

if (0x01 & easycap_standard[peasycap->standard_offset].mask) {
	peasycap->frame_buffer[peasycap->frame_read][0].kount = \
							V4L2_FIELD_TOP;
} else {
	peasycap->frame_buffer[peasycap->frame_read][0].kount = \
							V4L2_FIELD_BOTTOM;
}

JOT(8, "setting:    %i=peasycap->frame_read\n", peasycap->frame_read);
JOT(8, "bumped to:  %i=peasycap->frame_fill\n", peasycap->frame_fill);

return 0;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  BY DEFINITION, odd IS true  FOR THE FIELD OCCUPYING LINES 1,3,5,...,479
 *                 odd IS false FOR THE FIELD OCCUPYING LINES 0,2,4,...,478
 *
 *  WHEN BOOLEAN PARAMETER decimatepixel IS true, ONLY THE FIELD FOR WHICH
 *  odd==false IS TRANSFERRED TO THE FRAME BUFFER.
 *
 *  THE BOOLEAN PARAMETER offerfields IS true ONLY WHEN THE USER PROGRAM
 *  CHOOSES THE OPTION V4L2_FIELD_ALTERNATE.  NO USERSPACE PROGRAM TESTED
 *  TO DATE HAS DONE THIS.  BUGS ARE LIKELY.
 */
/*---------------------------------------------------------------------------*/
int
field2frame(struct easycap *peasycap)
{
static struct timeval timeval0;
struct timeval timeval;
long long int above, below;
__u32 remainder;
struct signed_div_result sdr;

void *pex, *pad;
int kex, kad, mex, mad, rex, rad, rad2;
int c2, c3, w2, w3, cz, wz;
int rc, bytesperpixel, multiplier, much, more, over, rump, caches;
__u8 mask, margin;
bool odd, isuy, decimatepixel, offerfields;

JOT(8, "=====  parity %i, field buffer %i --> frame buffer %i\n", \
			peasycap->field_buffer[peasycap->field_read][0].kount,\
			peasycap->field_read, peasycap->frame_fill);
JOT(8, "=====  %i=bytesperpixel\n", peasycap->bytesperpixel);
if (true == peasycap->offerfields)
	JOT(8, "===== offerfields\n");

/*---------------------------------------------------------------------------*/
/*
 *  REJECT OR CLEAN BAD FIELDS
 */
/*---------------------------------------------------------------------------*/
if (peasycap->field_read == peasycap->field_fill) {
	SAY("ERROR: on entry, still filling field buffer %i\n", \
							peasycap->field_read);
	return 0;
}
#if defined(EASYCAP_TESTCARD)
easycap_testcard(peasycap, peasycap->field_read);
#else
if (0 != (0x0400 & peasycap->field_buffer[peasycap->field_read][0].kount))
	easycap_testcard(peasycap, peasycap->field_read);
#endif /*EASYCAP_TESTCARD*/
/*---------------------------------------------------------------------------*/

offerfields = peasycap->offerfields;
bytesperpixel = peasycap->bytesperpixel;
decimatepixel = peasycap->decimatepixel;

if ((2 != bytesperpixel) && \
			(3 != bytesperpixel) && \
			(4 != bytesperpixel)) {
	SAY("MISTAKE: %i=bytesperpixel\n", bytesperpixel);
	return -EFAULT;
}
if (true == decimatepixel)
	multiplier = 2;
else
	multiplier = 1;

w2 = 2 * multiplier * (peasycap->width);
w3 = bytesperpixel * \
		multiplier * \
		(peasycap->width);
wz = multiplier * \
		(peasycap->height) * \
		multiplier * \
		(peasycap->width);

kex = peasycap->field_read;  mex = 0;
kad = peasycap->frame_fill;  mad = 0;

pex = peasycap->field_buffer[kex][0].pgo;  rex = PAGE_SIZE;
pad = peasycap->frame_buffer[kad][0].pgo;  rad = PAGE_SIZE;
if (peasycap->field_buffer[kex][0].kount)
	odd = true;
else
	odd = false;

if ((true == odd) && (false == offerfields) &&(false == decimatepixel)) {
	JOT(8, "  initial skipping    %4i          bytes p.%4i\n", \
							w3/multiplier, mad);
	pad += (w3 / multiplier);  rad -= (w3 / multiplier);
}
isuy = true;
mask = 0;  rump = 0;  caches = 0;

cz = 0;
while (cz < wz) {
	/*-------------------------------------------------------------------*/
	/*
	**  PROCESS ONE LINE OF FRAME AT FULL RESOLUTION:
	**  READ   w2   BYTES FROM FIELD BUFFER,
	**  WRITE  w3   BYTES TO FRAME BUFFER
	**/
	/*-------------------------------------------------------------------*/
	if (false == decimatepixel) {
		over = w2;
		do {
			much = over;  more = 0;  margin = 0;  mask = 0x00;
			if (rex < much)
				much = rex;
			rump = 0;

			if (much % 2) {
				SAY("MISTAKE: much is odd\n");
				return -EFAULT;
			}

			more = (bytesperpixel * \
					much) / 2;
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
			if (1 < bytesperpixel) {
				if ((rad * \
					2) < (much * \
						bytesperpixel)) {
					/*
					**   INJUDICIOUS ALTERATION OF THIS
					**   BLOCK WILL CAUSE BREAKAGE.
					**   BEWARE.
					**/
					rad2 = rad + bytesperpixel - 1;
					much = ((((2 * \
						rad2)/bytesperpixel)/2) * 2);
					rump = ((bytesperpixel * \
							much) / 2) - rad;
					more = rad;
					}
				mask = (__u8)rump;
				margin = 0;
				if (much == rex) {
					mask |= 0x04;
					if ((mex + 1) < FIELD_BUFFER_SIZE/ \
								PAGE_SIZE) {
						margin = *((__u8 *)(peasycap->\
							field_buffer\
							[kex][mex + 1].pgo));
					} else
						mask |= 0x08;
				}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
			} else {
				SAY("MISTAKE: %i=bytesperpixel\n", \
						bytesperpixel);
				return -EFAULT;
			}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
			if (rump)
				caches++;

			rc = redaub(peasycap, pad, pex, much, more, \
							mask, margin, isuy);
			if (0 > rc) {
				SAY("ERROR: redaub() failed\n");
				return -EFAULT;
			}
			if (much % 4) {
				if (isuy)
					isuy = false;
				else
					isuy = true;
			}
			over -= much;   cz += much;
			pex  += much;  rex -= much;
			if (!rex) {
				mex++;
				pex = peasycap->field_buffer[kex][mex].pgo;
				rex = PAGE_SIZE;
			}
			pad  += more;
			rad -= more;
			if (!rad) {
				mad++;
				pad = peasycap->frame_buffer[kad][mad].pgo;
				rad = PAGE_SIZE;
				if (rump) {
					pad += rump;
					rad -= rump;
				}
			}
		} while (over);
/*---------------------------------------------------------------------------*/
/*
 *  SKIP  w3 BYTES IN TARGET FRAME BUFFER,
 *  UNLESS IT IS THE LAST LINE OF AN ODD FRAME
 */
/*---------------------------------------------------------------------------*/
		if (((false == odd) || (cz != wz))&&(false == offerfields)) {
			over = w3;
			do {
				if (!rad) {
					mad++;
					pad = peasycap->frame_buffer\
						[kad][mad].pgo;
					rad = PAGE_SIZE;
				}
				more = over;
				if (rad < more)
					more = rad;
				over -= more;
				pad  += more;
				rad  -= more;
			} while (over);
		}
/*---------------------------------------------------------------------------*/
/*
 *  PROCESS ONE LINE OF FRAME AT REDUCED RESOLUTION:
 *  ONLY IF false==odd,
 *  READ   w2   BYTES FROM FIELD BUFFER,
 *  WRITE  w3 / 2  BYTES TO FRAME BUFFER
 */
/*---------------------------------------------------------------------------*/
	} else if (false == odd) {
		over = w2;
		do {
			much = over;  more = 0;  margin = 0;  mask = 0x00;
			if (rex < much)
				much = rex;
			rump = 0;

			if (much % 2) {
				SAY("MISTAKE: much is odd\n");
				return -EFAULT;
			}

			more = (bytesperpixel * \
					much) / 4;
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
			if (1 < bytesperpixel) {
				if ((rad * 4) < (much * \
						bytesperpixel)) {
					/*
					**   INJUDICIOUS ALTERATION OF THIS
					**   BLOCK WILL CAUSE BREAKAGE.
					**   BEWARE.
					**/
					rad2 = rad + bytesperpixel - 1;
					much = ((((2 * rad2)/bytesperpixel)/2)\
									* 4);
					rump = ((bytesperpixel * \
							much) / 4) - rad;
					more = rad;
					}
				mask = (__u8)rump;
				margin = 0;
				if (much == rex) {
					mask |= 0x04;
					if ((mex + 1) < FIELD_BUFFER_SIZE/ \
								PAGE_SIZE) {
						margin = *((__u8 *)(peasycap->\
							field_buffer\
							[kex][mex + 1].pgo));
						}
					else
						mask |= 0x08;
					}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
				} else {
					SAY("MISTAKE: %i=bytesperpixel\n", \
						bytesperpixel);
					return -EFAULT;
				}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
			if (rump)
				caches++;

			rc = redaub(peasycap, pad, pex, much, more, \
							mask, margin, isuy);
			if (0 > rc) {
				SAY("ERROR: redaub() failed\n");
				return -EFAULT;
			}
			over -= much;   cz += much;
			pex  += much;  rex -= much;
			if (!rex) {
				mex++;
				pex = peasycap->field_buffer[kex][mex].pgo;
				rex = PAGE_SIZE;
			}
			pad  += more;
			rad -= more;
			if (!rad) {
				mad++;
				pad = peasycap->frame_buffer[kad][mad].pgo;
				rad = PAGE_SIZE;
				if (rump) {
					pad += rump;
					rad -= rump;
				}
			}
		} while (over);
/*---------------------------------------------------------------------------*/
/*
 *  OTHERWISE JUST
 *  READ   w2   BYTES FROM FIELD BUFFER AND DISCARD THEM
 */
/*---------------------------------------------------------------------------*/
	} else {
		over = w2;
		do {
			if (!rex) {
				mex++;
				pex = peasycap->field_buffer[kex][mex].pgo;
				rex = PAGE_SIZE;
			}
			much = over;
			if (rex < much)
				much = rex;
			over -= much;
			cz += much;
			pex  += much;
			rex -= much;
		} while (over);
	}
}
/*---------------------------------------------------------------------------*/
/*
 *  SANITY CHECKS
 */
/*---------------------------------------------------------------------------*/
c2 = (mex + 1)*PAGE_SIZE - rex;
if (cz != c2)
	SAY("ERROR: discrepancy %i in bytes read\n", c2 - cz);
c3 = (mad + 1)*PAGE_SIZE - rad;

if (false == decimatepixel) {
	if (bytesperpixel * \
		cz != c3) \
		SAY("ERROR: discrepancy %i in bytes written\n", \
						c3 - (bytesperpixel * \
									cz));
} else {
	if (false == odd) {
		if (bytesperpixel * \
			cz != (4 * c3))
			SAY("ERROR: discrepancy %i in bytes written\n", \
						(2*c3)-(bytesperpixel * \
									cz));
		} else {
			if (0 != c3)
				SAY("ERROR: discrepancy %i " \
						"in bytes written\n", c3);
		}
}
if (rump)
	SAY("ERROR: undischarged cache at end of line in frame buffer\n");

JOT(8, "===== field2frame(): %i bytes --> %i bytes (incl skip)\n", c2, c3);
JOT(8, "===== field2frame(): %i=mad  %i=rad\n", mad, rad);

if (true == odd)
	JOT(8, "+++++ field2frame():  frame buffer %i is full\n", kad);

if (peasycap->field_read == peasycap->field_fill)
	SAY("WARNING: on exit, filling field buffer %i\n", \
							peasycap->field_read);
/*---------------------------------------------------------------------------*/
/*
 *  CALCULATE VIDEO STREAMING RATE
 */
/*---------------------------------------------------------------------------*/
do_gettimeofday(&timeval);
if (timeval0.tv_sec) {
	below = ((long long int)(1000000)) * \
		((long long int)(timeval.tv_sec  - timeval0.tv_sec)) + \
			 (long long int)(timeval.tv_usec - timeval0.tv_usec);
	above = (long long int)1000000;

	sdr = signed_div(above, below);
	above = sdr.quotient;
	remainder = (__u32)sdr.remainder;

	JOT(8, "video streaming at %3lli.%03i fields per second\n", above, \
							(remainder/1000));
}
timeval0 = timeval;

if (caches)
	JOT(8, "%i=caches\n", caches);
return 0;
}
/*****************************************************************************/
struct signed_div_result
signed_div(long long int above, long long int below)
{
struct signed_div_result sdr;

if (((0 <= above) && (0 <= below)) || ((0  > above) && (0  > below))) {
	sdr.remainder = (unsigned long long int) do_div(above, below);
	sdr.quotient  = (long long int) above;
} else {
	if (0 > above)
		above = -above;
	if (0 > below)
		below = -below;
	sdr.remainder = (unsigned long long int) do_div(above, below);
	sdr.quotient  = -((long long int) above);
}
return sdr;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  DECIMATION AND COLOURSPACE CONVERSION.
 *
 *  THIS ROUTINE REQUIRES THAT ALL THE DATA TO BE READ RESIDES ON ONE PAGE
 *  AND THAT ALL THE DATA TO BE WRITTEN RESIDES ON ONE (DIFFERENT) PAGE.
 *  THE CALLING ROUTINE MUST ENSURE THAT THIS REQUIREMENT IS MET, AND MUST
 *  ALSO ENSURE THAT much IS EVEN.
 *
 *  much BYTES ARE READ, AT LEAST (bytesperpixel * much)/2 BYTES ARE WRITTEN
 *  IF THERE IS NO DECIMATION, HALF THIS AMOUNT IF THERE IS DECIMATION.
 *
 *  mask IS ZERO WHEN NO SPECIAL BEHAVIOUR REQUIRED. OTHERWISE IT IS SET THUS:
 *     0x03 & mask =  number of bytes to be written to cache instead of to
 *                    frame buffer
 *     0x04 & mask => use argument margin to set the chrominance for last pixel
 *     0x08 & mask => do not set the chrominance for last pixel
 *
 *  YUV to RGB CONVERSION IS (OR SHOULD BE) ITU-R BT 601.
 *
 *  THERE IS A LOT OF CODE REPETITION IN THIS ROUTINE IN ORDER TO AVOID
 *  INEFFICIENT SWITCHING INSIDE INNER LOOPS.  REARRANGING THE LOGIC TO
 *  REDUCE CODE LENGTH WILL GENERALLY IMPAIR RUNTIME PERFORMANCE.  BEWARE.
 */
/*---------------------------------------------------------------------------*/
int
redaub(struct easycap *peasycap, void *pad, void *pex, int much, int more, \
					__u8 mask, __u8 margin, bool isuy)
{
static __s32 ay[256], bu[256], rv[256], gu[256], gv[256];
static __u8 cache[8], *pcache;
__u8 r, g, b, y, u, v, c, *p2, *p3, *pz, *pr;
int  bytesperpixel;
bool byteswaporder, decimatepixel, last;
int j, rump;
__s32 s32;

if (much % 2) {
	SAY("MISTAKE: much is odd\n");
	return -EFAULT;
}
bytesperpixel = peasycap->bytesperpixel;
byteswaporder = peasycap->byteswaporder;
decimatepixel = peasycap->decimatepixel;

/*---------------------------------------------------------------------------*/
if (!bu[255]) {
	for (j = 0; j < 112; j++) {
		s32 = (0xFF00 & (453 * j)) >> 8;
		bu[j + 128] =  s32; bu[127 - j] = -s32;
		s32 = (0xFF00 & (359 * j)) >> 8;
		rv[j + 128] =  s32; rv[127 - j] = -s32;
		s32 = (0xFF00 & (88 * j)) >> 8;
		gu[j + 128] =  s32; gu[127 - j] = -s32;
		s32 = (0xFF00 & (183 * j)) >> 8;
		gv[j + 128] =  s32; gv[127 - j] = -s32;
	}
	for (j = 0; j < 16; j++) {
		bu[j] = bu[16]; rv[j] = rv[16];
		gu[j] = gu[16]; gv[j] = gv[16];
	}
	for (j = 240; j < 256; j++) {
		bu[j] = bu[239]; rv[j] = rv[239];
		gu[j] = gu[239]; gv[j] = gv[239];
	}
	for (j =  16; j < 236; j++)
		ay[j] = j;
	for (j =   0; j <  16; j++)
		ay[j] = ay[16];
	for (j = 236; j < 256; j++)
		ay[j] = ay[235];
	JOT(8, "lookup tables are prepared\n");
}
if ((__u8 *)NULL == pcache)
	pcache = &cache[0];
/*---------------------------------------------------------------------------*/
/*
 *  TRANSFER CONTENTS OF CACHE TO THE FRAME BUFFER
 */
/*---------------------------------------------------------------------------*/
if (!pcache) {
	SAY("MISTAKE: pcache is NULL\n");
	return -EFAULT;
}

if (pcache != &cache[0])
	JOT(16, "cache has %i bytes\n", (int)(pcache - &cache[0]));
p2 = &cache[0];
p3 = (__u8 *)pad - (int)(pcache - &cache[0]);
while (p2 < pcache) {
	*p3++ = *p2;  p2++;
}
pcache = &cache[0];
if (p3 != pad) {
	SAY("MISTAKE: pointer misalignment\n");
	return -EFAULT;
}
/*---------------------------------------------------------------------------*/
rump = (int)(0x03 & mask);
u = 0; v = 0;
p2 = (__u8 *)pex;  pz = p2 + much;  pr = p3 + more;  last = false;
p2++;

if (true == isuy)
	u = *(p2 - 1);
else
	v = *(p2 - 1);

if (rump)
	JOT(16, "%4i=much  %4i=more  %i=rump\n", much, more, rump);

/*---------------------------------------------------------------------------*/
switch (bytesperpixel) {
case 2: {
	if (false == decimatepixel) {
		memcpy(pad, pex, (size_t)much);
		if (false == byteswaporder)
			/*---------------------------------------------------*/
			/*
			**  UYVY
			*/
			/*---------------------------------------------------*/
			return 0;
		else {
			/*---------------------------------------------------*/
			/*
			**  YUYV
			*/
			/*---------------------------------------------------*/
			p3 = (__u8 *)pad;  pz = p3 + much;
			while  (pz > p3) {
				c = *p3;
				*p3 = *(p3 + 1);
				*(p3 + 1) = c;
				p3 += 2;
			}
			return 0;
		}
	} else {
		if (false == byteswaporder) {
			/*---------------------------------------------------*/
			/*
			**  UYVY DECIMATED
			*/
			/*---------------------------------------------------*/
			p2 = (__u8 *)pex;  p3 = (__u8 *)pad;  pz = p2 + much;
			while (pz > p2) {
				*p3 = *p2;
				*(p3 + 1) = *(p2 + 1);
				*(p3 + 2) = *(p2 + 2);
				*(p3 + 3) = *(p2 + 3);
				p3 += 4;  p2 += 8;
			}
			return 0;
		} else {
			/*---------------------------------------------------*/
			/*
			**  YUYV DECIMATED
			**/
			/*---------------------------------------------------*/
			p2 = (__u8 *)pex;  p3 = (__u8 *)pad;  pz = p2 + much;
			while (pz > p2) {
				*p3 = *(p2 + 1);
				*(p3 + 1) = *p2;
				*(p3 + 2) = *(p2 + 3);
				*(p3 + 3) = *(p2 + 2);
				p3 += 4;  p2 += 8;
			}
			return 0;
		}
	}
	break;
	}
case 3:
	{
	if (false == decimatepixel) {
		if (false == byteswaporder) {
			/*---------------------------------------------------*/
			/*
			**  RGB
			**/
			/*---------------------------------------------------*/
			while (pz > p2) {
				if (pr <= (p3 + bytesperpixel))
					last = true;
				else
					last = false;
				y = *p2;
				if ((true == last) && (0x0C & mask)) {
					if (0x04 & mask) {
						if (true == isuy)
							v = margin;
						else
							u = margin;
					} else
						if (0x08 & mask)
							;
				} else {
					if (true == isuy)
						v = *(p2 + 1);
					else
						u = *(p2 + 1);
				}

				s32 = ay[(int)y] + rv[(int)v];
				r = (255 < s32) ? 255 : ((0 > s32) ? \
							0 : (__u8)s32);
				s32 = ay[(int)y] - gu[(int)u] - gv[(int)v];
				g = (255 < s32) ? 255 : ((0 > s32) ? \
							0 : (__u8)s32);
				s32 = ay[(int)y] + bu[(int)u];
				b = (255 < s32) ? 255 : ((0 > s32) ? \
							0 : (__u8)s32);

				if ((true == last) && rump) {
					pcache = &cache[0];
					switch (bytesperpixel - rump) {
					case 1: {
						*p3 = r;
						*pcache++ = g;
						*pcache++ = b;
						break;
					}
					case 2: {
						*p3 = r;
						*(p3 + 1) = g;
						*pcache++ = b;
						break;
					}
					default: {
						SAY("MISTAKE: %i=rump\n", \
							bytesperpixel - rump);
						return -EFAULT;
					}
					}
				} else {
					*p3 = r;
					*(p3 + 1) = g;
					*(p3 + 2) = b;
				}
				p2 += 2;
				if (true == isuy)
					isuy = false;
				else
					isuy = true;
				p3 += bytesperpixel;
			}
			return 0;
		} else {
			/*---------------------------------------------------*/
			/*
			**  BGR
			*/
			/*---------------------------------------------------*/
			while (pz > p2) {
				if (pr <= (p3 + bytesperpixel))
					last = true;
				else
					last = false;
				y = *p2;
				if ((true == last) && (0x0C & mask)) {
					if (0x04 & mask) {
						if (true == isuy)
							v = margin;
						else
							u = margin;
					}
				else
					if (0x08 & mask)
						;
				} else {
					if (true == isuy)
						v = *(p2 + 1);
					else
						u = *(p2 + 1);
				}

				s32 = ay[(int)y] + rv[(int)v];
				r = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);
				s32 = ay[(int)y] - gu[(int)u] - gv[(int)v];
				g = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);
				s32 = ay[(int)y] + bu[(int)u];
				b = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);

				if ((true == last) && rump) {
					pcache = &cache[0];
					switch (bytesperpixel - rump) {
					case 1: {
						*p3 = b;
						*pcache++ = g;
						*pcache++ = r;
						break;
					}
					case 2: {
						*p3 = b;
						*(p3 + 1) = g;
						*pcache++ = r;
						break;
					}
					default: {
						SAY("MISTAKE: %i=rump\n", \
							bytesperpixel - rump);
						return -EFAULT;
					}
					}
				} else {
					*p3 = b;
					*(p3 + 1) = g;
					*(p3 + 2) = r;
					}
				p2 += 2;
				if (true == isuy)
					isuy = false;
				else
					isuy = true;
				p3 += bytesperpixel;
				}
			}
		return 0;
	} else {
		if (false == byteswaporder) {
			/*---------------------------------------------------*/
			/*
			**  RGB DECIMATED
			*/
			/*---------------------------------------------------*/
			while (pz > p2) {
				if (pr <= (p3 + bytesperpixel))
					last = true;
				else
					last = false;
				y = *p2;
				if ((true == last) && (0x0C & mask)) {
					if (0x04 & mask) {
						if (true == isuy)
							v = margin;
						else
							u = margin;
					} else
						if (0x08 & mask)
							;
				} else {
					if (true == isuy)
						v = *(p2 + 1);
					else
						u = *(p2 + 1);
				}

				if (true == isuy) {
					s32 = ay[(int)y] + rv[(int)v];
					r = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);
					s32 = ay[(int)y] - gu[(int)u] - \
								gv[(int)v];
					g = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);
					s32 = ay[(int)y] + bu[(int)u];
					b = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);

					if ((true == last) && rump) {
						pcache = &cache[0];
						switch (bytesperpixel - rump) {
						case 1: {
							*p3 = r;
							*pcache++ = g;
							*pcache++ = b;
							break;
						}
						case 2: {
							*p3 = r;
							*(p3 + 1) = g;
							*pcache++ = b;
							break;
						}
						default: {
							SAY("MISTAKE: " \
							"%i=rump\n", \
							bytesperpixel - rump);
							return -EFAULT;
						}
						}
					} else {
						*p3 = r;
						*(p3 + 1) = g;
						*(p3 + 2) = b;
					}
					isuy = false;
					p3 += bytesperpixel;
				} else {
					isuy = true;
				}
				p2 += 2;
			}
			return 0;
		} else {
			/*---------------------------------------------------*/
			/*
			 *  BGR DECIMATED
			 */
			/*---------------------------------------------------*/
			while (pz > p2) {
				if (pr <= (p3 + bytesperpixel))
					last = true;
				else
					last = false;
				y = *p2;
				if ((true == last) && (0x0C & mask)) {
					if (0x04 & mask) {
						if (true == isuy)
							v = margin;
						else
							u = margin;
					} else
						if (0x08 & mask)
							;
				} else {
					if (true == isuy)
						v = *(p2 + 1);
					else
						u = *(p2 + 1);
				}

				if (true == isuy) {

					s32 = ay[(int)y] + rv[(int)v];
					r = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);
					s32 = ay[(int)y] - gu[(int)u] - \
								gv[(int)v];
					g = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);
					s32 = ay[(int)y] + bu[(int)u];
					b = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);

					if ((true == last) && rump) {
						pcache = &cache[0];
						switch (bytesperpixel - rump) {
						case 1: {
							*p3 = b;
							*pcache++ = g;
							*pcache++ = r;
							break;
						}
						case 2: {
							*p3 = b;
							*(p3 + 1) = g;
							*pcache++ = r;
							break;
						}
						default: {
							SAY("MISTAKE: " \
							"%i=rump\n", \
							bytesperpixel - rump);
							return -EFAULT;
						}
						}
					} else {
						*p3 = b;
						*(p3 + 1) = g;
						*(p3 + 2) = r;
						}
					isuy = false;
					p3 += bytesperpixel;
					}
				else
					isuy = true;
				p2 += 2;
				}
			return 0;
			}
		}
	break;
	}
case 4:
	{
	if (false == decimatepixel) {
		if (false == byteswaporder) {
			/*---------------------------------------------------*/
			/*
			**  RGBA
			*/
			/*---------------------------------------------------*/
			while (pz > p2) {
				if (pr <= (p3 + bytesperpixel))
					last = true;
				else
					last = false;
				y = *p2;
				if ((true == last) && (0x0C & mask)) {
					if (0x04 & mask) {
						if (true == isuy)
							v = margin;
						else
							u = margin;
					} else
						 if (0x08 & mask)
							;
				} else {
					if (true == isuy)
						v = *(p2 + 1);
					else
						u = *(p2 + 1);
				}

				s32 = ay[(int)y] + rv[(int)v];
				r = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);
				s32 = ay[(int)y] - gu[(int)u] - gv[(int)v];
				g = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);
				s32 = ay[(int)y] + bu[(int)u];
				b = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);

				if ((true == last) && rump) {
					pcache = &cache[0];
					switch (bytesperpixel - rump) {
					case 1: {
						*p3 = r;
						*pcache++ = g;
						*pcache++ = b;
						*pcache++ = 0;
						break;
					}
					case 2: {
						*p3 = r;
						*(p3 + 1) = g;
						*pcache++ = b;
						*pcache++ = 0;
						break;
					}
					case 3: {
						*p3 = r;
						*(p3 + 1) = g;
						*(p3 + 2) = b;
						*pcache++ = 0;
						break;
					}
					default: {
						SAY("MISTAKE: %i=rump\n", \
							bytesperpixel - rump);
						return -EFAULT;
					}
					}
				} else {
					*p3 = r;
					*(p3 + 1) = g;
					*(p3 + 2) = b;
					*(p3 + 3) = 0;
				}
				p2 += 2;
				if (true == isuy)
					isuy = false;
				else
					isuy = true;
				p3 += bytesperpixel;
			}
			return 0;
		} else {
			/*---------------------------------------------------*/
			/*
			**  BGRA
			*/
			/*---------------------------------------------------*/
			while (pz > p2) {
				if (pr <= (p3 + bytesperpixel))
					last = true;
				else
					last = false;
				y = *p2;
				if ((true == last) && (0x0C & mask)) {
					if (0x04 & mask) {
						if (true == isuy)
							v = margin;
						else
							u = margin;
					} else
						 if (0x08 & mask)
							;
				} else {
					if (true == isuy)
						v = *(p2 + 1);
					else
						u = *(p2 + 1);
				}

				s32 = ay[(int)y] + rv[(int)v];
				r = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);
				s32 = ay[(int)y] - gu[(int)u] - gv[(int)v];
				g = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);
				s32 = ay[(int)y] + bu[(int)u];
				b = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);

				if ((true == last) && rump) {
					pcache = &cache[0];
					switch (bytesperpixel - rump) {
					case 1: {
						*p3 = b;
						*pcache++ = g;
						*pcache++ = r;
						*pcache++ = 0;
						break;
					}
					case 2: {
						*p3 = b;
						*(p3 + 1) = g;
						*pcache++ = r;
						*pcache++ = 0;
						break;
					}
					case 3: {
						*p3 = b;
						*(p3 + 1) = g;
						*(p3 + 2) = r;
						*pcache++ = 0;
						break;
					}
					default: {
						SAY("MISTAKE: %i=rump\n", \
							bytesperpixel - rump);
						return -EFAULT;
					}
					}
				} else {
					*p3 = b;
					*(p3 + 1) = g;
					*(p3 + 2) = r;
					*(p3 + 3) = 0;
				}
				p2 += 2;
				if (true == isuy)
					isuy = false;
				else
					isuy = true;
				p3 += bytesperpixel;
			}
		}
		return 0;
	} else {
		if (false == byteswaporder) {
			/*---------------------------------------------------*/
			/*
			**  RGBA DECIMATED
			*/
			/*---------------------------------------------------*/
			while (pz > p2) {
				if (pr <= (p3 + bytesperpixel))
					last = true;
				else
					last = false;
				y = *p2;
				if ((true == last) && (0x0C & mask)) {
					if (0x04 & mask) {
						if (true == isuy)
							v = margin;
						else
							u = margin;
					} else
						if (0x08 & mask)
							;
				} else {
					if (true == isuy)
						v = *(p2 + 1);
					else
						u = *(p2 + 1);
				}

				if (true == isuy) {

					s32 = ay[(int)y] + rv[(int)v];
					r = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);
					s32 = ay[(int)y] - gu[(int)u] - \
								gv[(int)v];
					g = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);
					s32 = ay[(int)y] + bu[(int)u];
					b = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);

					if ((true == last) && rump) {
						pcache = &cache[0];
						switch (bytesperpixel - rump) {
						case 1: {
							*p3 = r;
							*pcache++ = g;
							*pcache++ = b;
							*pcache++ = 0;
							break;
						}
						case 2: {
							*p3 = r;
							*(p3 + 1) = g;
							*pcache++ = b;
							*pcache++ = 0;
							break;
						}
						case 3: {
							*p3 = r;
							*(p3 + 1) = g;
							*(p3 + 2) = b;
							*pcache++ = 0;
							break;
						}
						default: {
							SAY("MISTAKE: " \
							"%i=rump\n", \
							bytesperpixel - \
							rump);
							return -EFAULT;
							}
						}
					} else {
						*p3 = r;
						*(p3 + 1) = g;
						*(p3 + 2) = b;
						*(p3 + 3) = 0;
						}
					isuy = false;
					p3 += bytesperpixel;
				} else
					isuy = true;
				p2 += 2;
			}
			return 0;
		} else {
			/*---------------------------------------------------*/
			/*
			**  BGRA DECIMATED
			*/
			/*---------------------------------------------------*/
			while (pz > p2) {
				if (pr <= (p3 + bytesperpixel))
					last = true;
				else
					last = false;
				y = *p2;
				if ((true == last) && (0x0C & mask)) {
					if (0x04 & mask) {
						if (true == isuy)
							v = margin;
						else
							u = margin;
					} else
						if (0x08 & mask)
							;
				} else {
					if (true == isuy)
						v = *(p2 + 1);
					else
						u = *(p2 + 1);
				}

				if (true == isuy) {
					s32 = ay[(int)y] + rv[(int)v];
					r = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);
					s32 = ay[(int)y] - gu[(int)u] - \
								gv[(int)v];
					g = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);
					s32 = ay[(int)y] + bu[(int)u];
					b = (255 < s32) ? 255 : ((0 > s32) ? \
								0 : (__u8)s32);

					if ((true == last) && rump) {
						pcache = &cache[0];
						switch (bytesperpixel - rump) {
						case 1: {
							*p3 = b;
							*pcache++ = g;
							*pcache++ = r;
							*pcache++ = 0;
							break;
						}
						case 2: {
							*p3 = b;
							*(p3 + 1) = g;
							*pcache++ = r;
							*pcache++ = 0;
							break;
						}
						case 3: {
							*p3 = b;
							*(p3 + 1) = g;
							*(p3 + 2) = r;
							*pcache++ = 0;
							break;
						}
						default: {
							SAY("MISTAKE: " \
							"%i=rump\n", \
							bytesperpixel - rump);
							return -EFAULT;
						}
						}
					} else {
						*p3 = b;
						*(p3 + 1) = g;
						*(p3 + 2) = r;
						*(p3 + 3) = 0;
					}
					isuy = false;
					p3 += bytesperpixel;
				} else
					isuy = true;
					p2 += 2;
				}
				return 0;
			}
		}
	break;
	}
default: {
	SAY("MISTAKE: %i=bytesperpixel\n", bytesperpixel);
	return -EFAULT;
	}
}
return 0;
}
/*****************************************************************************/
void
debrief(struct easycap *peasycap)
{
if ((struct usb_device *)NULL != peasycap->pusb_device) {
	check_stk(peasycap->pusb_device);
	check_saa(peasycap->pusb_device);
	sayreadonly(peasycap);
	SAY("%i=peasycap->field_fill\n", peasycap->field_fill);
	SAY("%i=peasycap->field_read\n", peasycap->field_read);
	SAY("%i=peasycap->frame_fill\n", peasycap->frame_fill);
	SAY("%i=peasycap->frame_read\n", peasycap->frame_read);
}
return;
}
/*****************************************************************************/
void
sayreadonly(struct easycap *peasycap)
{
static int done;
int got00, got1F, got60, got61, got62;

if ((!done) && ((struct usb_device *)NULL != peasycap->pusb_device)) {
	done = 1;
	got00 = read_saa(peasycap->pusb_device, 0x00);
	got1F = read_saa(peasycap->pusb_device, 0x1F);
	got60 = read_saa(peasycap->pusb_device, 0x60);
	got61 = read_saa(peasycap->pusb_device, 0x61);
	got62 = read_saa(peasycap->pusb_device, 0x62);
	SAY("0x%02X=reg0x00  0x%02X=reg0x1F\n", got00, got1F);
	SAY("0x%02X=reg0x60  0x%02X=reg0x61  0x%02X=reg0x62\n", \
							got60, got61, got62);
}
return;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  SEE CORBET ET AL. "LINUX DEVICE DRIVERS", 3rd EDITION, PAGES 430-434
 */
/*---------------------------------------------------------------------------*/
int easycap_mmap(struct file *file, struct vm_area_struct *pvma)
{

JOT(8, "\n");

pvma->vm_ops = &easycap_vm_ops;
pvma->vm_flags |= VM_RESERVED;
if (NULL != file)
	pvma->vm_private_data = file->private_data;
easycap_vma_open(pvma);
return 0;
}
/*****************************************************************************/
void
easycap_vma_open(struct vm_area_struct *pvma)
{
struct easycap *peasycap;

peasycap = pvma->vm_private_data;
if (NULL != peasycap)
	peasycap->vma_many++;

JOT(8, "%i=peasycap->vma_many\n", peasycap->vma_many);

return;
}
/*****************************************************************************/
void
easycap_vma_close(struct vm_area_struct *pvma)
{
struct easycap *peasycap;

peasycap = pvma->vm_private_data;
if (NULL != peasycap) {
	peasycap->vma_many--;
	JOT(8, "%i=peasycap->vma_many\n", peasycap->vma_many);
}
return;
}
/*****************************************************************************/
int
easycap_vma_fault(struct vm_area_struct *pvma, struct vm_fault *pvmf)
{
int k, m, retcode;
void *pbuf;
struct page *page;
struct easycap *peasycap;

retcode = VM_FAULT_NOPAGE;
pbuf = (void *)NULL;
page = (struct page *)NULL;

if (NULL == pvma) {
	SAY("pvma is NULL\n");
	return retcode;
}
if (NULL == pvmf) {
	SAY("pvmf is NULL\n");
	return retcode;
}

k = (pvmf->pgoff) / (FRAME_BUFFER_SIZE/PAGE_SIZE);
m = (pvmf->pgoff) % (FRAME_BUFFER_SIZE/PAGE_SIZE);

if (!m)
	JOT(4, "%4i=k, %4i=m\n", k, m);
else
	JOT(16, "%4i=k, %4i=m\n", k, m);

if ((0 > k) || (FRAME_BUFFER_MANY <= k)) {
	SAY("ERROR: buffer index %i out of range\n", k);
	return retcode;
}
if ((0 > m) || (FRAME_BUFFER_SIZE/PAGE_SIZE <= m)) {
	SAY("ERROR: page number  %i out of range\n", m);
	return retcode;
}
peasycap = pvma->vm_private_data;
if (NULL == peasycap) {
	SAY("ERROR: peasycap is NULL\n");
	return retcode;
}
mutex_lock(&(peasycap->mutex_mmap_video[0]));
/*---------------------------------------------------------------------------*/
pbuf = peasycap->frame_buffer[k][m].pgo;
if (NULL == pbuf) {
	SAY("ERROR:  pbuf is NULL\n");
	goto finish;
}
page = virt_to_page(pbuf);
if (NULL == page) {
	SAY("ERROR:  page is NULL\n");
	goto finish;
}
get_page(page);
/*---------------------------------------------------------------------------*/
finish:
mutex_unlock(&(peasycap->mutex_mmap_video[0]));
if (NULL == page) {
	SAY("ERROR:  page is NULL after get_page(page)\n");
} else {
	pvmf->page = page;
	retcode = VM_FAULT_MINOR;
}
return retcode;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  ON COMPLETION OF A VIDEO URB ITS DATA IS COPIED TO THE FIELD BUFFERS
 *  PROVIDED peasycap->video_idle IS ZER0.  REGARDLESS OF THIS BEING TRUE,
 *  IT IS RESUBMITTED PROVIDED peasycap->video_isoc_streaming IS NOT ZERO.
 *
 *  THIS FUNCTION IS AN INTERRUPT SERVICE ROUTINE AND MUST NOT SLEEP.
 *
 *  INFORMATION ABOUT THE VALIDITY OF THE CONTENTS OF THE FIELD BUFFER ARE
 *  STORED IN THE TWO-BYTE STATUS PARAMETER
 *        peasycap->field_buffer[peasycap->field_fill][0].kount
 *  NOTICE THAT THE INFORMATION IS STORED ONLY WITH PAGE 0 OF THE FIELD BUFFER.
 *
 *  THE LOWER BYTE CONTAINS THE FIELD PARITY BYTE FURNISHED BY THE SAA7113H
 *  CHIP.
 *
 *  THE UPPER BYTE IS ZERO IF NO PROBLEMS, OTHERWISE:
 *      0 != (kount & 0x8000)   => AT LEAST ONE URB COMPLETED WITH ERRORS
 *      0 != (kount & 0x4000)   => BUFFER HAS TOO MUCH DATA
 *      0 != (kount & 0x2000)   => BUFFER HAS NOT ENOUGH DATA
 *      0 != (kount & 0x0400)   => FIELD WAS SUBMITTED BY BRIDGER ROUTINE
 *      0 != (kount & 0x0200)   => FIELD BUFFER NOT YET CHECKED
 *      0 != (kount & 0x0100)   => BUFFER HAS TWO EXTRA BYTES - WHY?
 */
/*---------------------------------------------------------------------------*/
void
easycap_complete(struct urb *purb)
{
static int mt;
struct easycap *peasycap;
struct data_buffer *pfield_buffer;
char errbuf[16];
int i, more, much, leap, rc, last;
int videofieldamount;
unsigned int override;
int framestatus, framelength, frameactual, frameoffset;
__u8 *pu;
#if defined(BRIDGER)
struct timeval timeval;
long long usec;
#endif /*BRIDGER*/

if (NULL == purb) {
	SAY("ERROR: easycap_complete(): purb is NULL\n");
	return;
}
peasycap = purb->context;
if (NULL == peasycap) {
	SAY("ERROR: easycap_complete(): peasycap is NULL\n");
	return;
}

if (peasycap->video_eof)
	return;

for (i = 0; i < VIDEO_ISOC_BUFFER_MANY; i++)
	if (purb->transfer_buffer == peasycap->video_isoc_buffer[i].pgo)
		break;
JOT(16, "%2i=urb\n", i);
last = peasycap->video_isoc_sequence;
if ((((VIDEO_ISOC_BUFFER_MANY - 1) == last) && \
						(0 != i)) || \
	(((VIDEO_ISOC_BUFFER_MANY - 1) != last) && \
						((last + 1) != i))) {
	SAY("ERROR: out-of-order urbs %i,%i ... continuing\n", last, i);
}
peasycap->video_isoc_sequence = i;

if (peasycap->video_idle) {
	JOT(16, "%i=video_idle  %i=video_isoc_streaming\n", \
			peasycap->video_idle, peasycap->video_isoc_streaming);
	if (peasycap->video_isoc_streaming) {
		rc = usb_submit_urb(purb, GFP_ATOMIC);
		if (0 != rc) {
			SAY("ERROR: while %i=video_idle, " \
					"usb_submit_urb() failed with rc:\n", \
							peasycap->video_idle);
			switch (rc) {
			case -ENOMEM: {
				SAY("ENOMEM\n");
				break;
			}
			case -ENODEV: {
				SAY("ENODEV\n");
				break;
			}
			case -ENXIO: {
				SAY("ENXIO\n");
				break;
			}
			case -EINVAL: {
				SAY("EINVAL\n");
				break;
			}
			case -EAGAIN: {
				SAY("EAGAIN\n");
				break;
			}
			case -EFBIG: {
				SAY("EFBIG\n");
				break;
			}
			case -EPIPE: {
				SAY("EPIPE\n");
				break;
			}
			case -EMSGSIZE: {
				SAY("EMSGSIZE\n");
				break;
			}
			case -ENOSPC: {
				SAY("ENOSPC\n");
				break;
			}
			default: {
				SAY("0x%08X\n", rc);
				break;
			}
			}
		}
	}
return;
}
override = 0;
/*---------------------------------------------------------------------------*/
if (FIELD_BUFFER_MANY <= peasycap->field_fill) {
	SAY("ERROR: bad peasycap->field_fill\n");
	return;
}
if (purb->status) {
	if ((-ESHUTDOWN == purb->status) || (-ENOENT == purb->status)) {
		JOT(8, "urb status -ESHUTDOWN or -ENOENT\n");
		return;
	}

	(peasycap->field_buffer[peasycap->field_fill][0].kount) |= 0x8000 ;
	SAY("ERROR: bad urb status:\n");
	switch (purb->status) {
	case -EINPROGRESS: {
		SAY("-EINPROGRESS\n"); break;
	}
	case -ENOSR: {
		SAY("-ENOSR\n"); break;
	}
	case -EPIPE: {
		SAY("-EPIPE\n"); break;
	}
	case -EOVERFLOW: {
		SAY("-EOVERFLOW\n"); break;
	}
	case -EPROTO: {
		SAY("-EPROTO\n"); break;
	}
	case -EILSEQ: {
		SAY("-EILSEQ\n"); break;
	}
	case -ETIMEDOUT: {
		SAY("-ETIMEDOUT\n"); break;
	}
	case -EMSGSIZE: {
		SAY("-EMSGSIZE\n"); break;
	}
	case -EOPNOTSUPP: {
		SAY("-EOPNOTSUPP\n"); break;
	}
	case -EPFNOSUPPORT: {
		SAY("-EPFNOSUPPORT\n"); break;
	}
	case -EAFNOSUPPORT: {
		SAY("-EAFNOSUPPORT\n"); break;
	}
	case -EADDRINUSE: {
		SAY("-EADDRINUSE\n"); break;
	}
	case -EADDRNOTAVAIL: {
		SAY("-EADDRNOTAVAIL\n"); break;
	}
	case -ENOBUFS: {
		SAY("-ENOBUFS\n"); break;
	}
	case -EISCONN: {
		SAY("-EISCONN\n"); break;
	}
	case -ENOTCONN: {
		SAY("-ENOTCONN\n"); break;
	}
	case -ESHUTDOWN: {
		SAY("-ESHUTDOWN\n"); break;
	}
	case -ENOENT: {
		SAY("-ENOENT\n"); break;
	}
	case -ECONNRESET: {
		SAY("-ECONNRESET\n"); break;
	}
	case -ENOSPC: {
		SAY("ENOSPC\n"); break;
	}
	default: {
		SAY("unknown error code 0x%08X\n", purb->status); break;
	}
	}
/*---------------------------------------------------------------------------*/
} else {
	for (i = 0;  i < purb->number_of_packets; i++) {
		if (0 != purb->iso_frame_desc[i].status) {
			(peasycap->field_buffer\
				[peasycap->field_fill][0].kount) |= 0x8000 ;
			switch (purb->iso_frame_desc[i].status) {
			case  0: {
				strcpy(&errbuf[0], "OK"); break;
			}
			case -ENOENT: {
				strcpy(&errbuf[0], "-ENOENT"); break;
			}
			case -EINPROGRESS: {
				strcpy(&errbuf[0], "-EINPROGRESS"); break;
			}
			case -EPROTO: {
				strcpy(&errbuf[0], "-EPROTO"); break;
			}
			case -EILSEQ: {
				strcpy(&errbuf[0], "-EILSEQ"); break;
			}
			case -ETIME: {
				strcpy(&errbuf[0], "-ETIME"); break;
			}
			case -ETIMEDOUT: {
				strcpy(&errbuf[0], "-ETIMEDOUT"); break;
			}
			case -EPIPE: {
				strcpy(&errbuf[0], "-EPIPE"); break;
			}
			case -ECOMM: {
				strcpy(&errbuf[0], "-ECOMM"); break;
			}
			case -ENOSR: {
				strcpy(&errbuf[0], "-ENOSR"); break;
			}
			case -EOVERFLOW: {
				strcpy(&errbuf[0], "-EOVERFLOW"); break;
			}
			case -EREMOTEIO: {
				strcpy(&errbuf[0], "-EREMOTEIO"); break;
			}
			case -ENODEV: {
				strcpy(&errbuf[0], "-ENODEV"); break;
			}
			case -EXDEV: {
				strcpy(&errbuf[0], "-EXDEV"); break;
			}
			case -EINVAL: {
				strcpy(&errbuf[0], "-EINVAL"); break;
			}
			case -ECONNRESET: {
				strcpy(&errbuf[0], "-ECONNRESET"); break;
			}
			case -ENOSPC: {
				SAY("ENOSPC\n"); break;
			}
			case -ESHUTDOWN: {
				strcpy(&errbuf[0], "-ESHUTDOWN"); break;
			}
			default: {
				strcpy(&errbuf[0], "unknown error"); break;
			}
			}
		}
		framestatus = purb->iso_frame_desc[i].status;
		framelength = purb->iso_frame_desc[i].length;
		frameactual = purb->iso_frame_desc[i].actual_length;
		frameoffset = purb->iso_frame_desc[i].offset;

		JOT(16, "frame[%2i]:" \
				"%4i=status "  \
				"%4i=actual "  \
				"%4i=length "  \
				"%5i=offset\n", \
			i, framestatus, frameactual, framelength, frameoffset);
		if (!purb->iso_frame_desc[i].status) {
			more = purb->iso_frame_desc[i].actual_length;
			pfield_buffer = &peasycap->field_buffer\
				  [peasycap->field_fill][peasycap->field_page];
			videofieldamount = (peasycap->field_page * \
				PAGE_SIZE) + \
				(int)(pfield_buffer->pto - pfield_buffer->pgo);
		if (4 == more)
			mt++;
		if (4 < more) {
			if (mt) {
				JOT(8, "%4i empty video urb frames\n", mt);
				mt = 0;
			}
			if (FIELD_BUFFER_MANY <= peasycap->field_fill) {
				SAY("ERROR: bad peasycap->field_fill\n");
				return;
			}
			if (FIELD_BUFFER_SIZE/PAGE_SIZE <= \
							peasycap->field_page) {
				SAY("ERROR: bad peasycap->field_page\n");
				return;
			}
			pfield_buffer = &peasycap->field_buffer\
				[peasycap->field_fill][peasycap->field_page];
			pu = (__u8 *)(purb->transfer_buffer + \
					purb->iso_frame_desc[i].offset);
			if (0x80 & *pu)
				leap = 8;
			else
				leap = 4;
/*--------------------------------------------------------------------------*/
/*
 *  EIGHT-BYTE END-OF-VIDEOFIELD MARKER.
 *  NOTE:  A SUCCESSION OF URB FRAMES FOLLOWING THIS ARE EMPTY,
 *         CORRESPONDING TO THE FIELD FLYBACK (VERTICAL BLANKING) PERIOD.
 *
 *  PROVIDED THE FIELD BUFFER CONTAINS GOOD DATA AS INDICATED BY A ZERO UPPER
 *  BYTE OF
 *        peasycap->field_buffer[peasycap->field_fill][0].kount
 *  THE CONTENTS OF THE FIELD BUFFER ARE OFFERED TO dqbuf(), field_read IS
 *  UPDATED AND field_fill IS BUMPED.  IF THE FIELD BUFFER CONTAINS BAD DATA
 *  NOTHING IS OFFERED TO dqbuf().
 *
 *  THE DECISION ON WHETHER THE PARITY OF THE OFFERED FIELD BUFFER IS RIGHT
 *  RESTS WITH dqbuf().
 */
/*---------------------------------------------------------------------------*/
			if ((8 == more) || override) {
				if (videofieldamount > \
						peasycap->videofieldamount) {
					if (2 == videofieldamount - \
							peasycap->\
							videofieldamount)
						(peasycap->field_buffer\
						[peasycap->field_fill]\
							[0].kount) |= 0x0100;
					else
						(peasycap->field_buffer\
						[peasycap->field_fill]\
							[0].kount) |= 0x4000;
					} else if (videofieldamount < \
							peasycap->\
							videofieldamount) {
						(peasycap->field_buffer\
						[peasycap->field_fill]\
							[0].kount) |= 0x2000;
					}
				if (!(0xFF00 & peasycap->field_buffer\
						[peasycap->field_fill]\
						[0].kount)) {
					(peasycap->video_junk)--;
					if (-16 > peasycap->video_junk)
						peasycap->video_junk = -16;
					peasycap->field_read = \
							(peasycap->\
								field_fill)++;

					if (FIELD_BUFFER_MANY <= \
						peasycap->field_fill)
						peasycap->field_fill = 0;
					peasycap->field_page = 0;
					pfield_buffer = &peasycap->\
						field_buffer\
						[peasycap->field_fill]\
						[peasycap->field_page];
					pfield_buffer->pto = \
							pfield_buffer->pgo;

					JOT(8, "bumped to: %i=peasycap->" \
						"field_fill  %i=parity\n", \
						peasycap->field_fill, \
						0x00FF & pfield_buffer->kount);
					JOT(8, "field buffer %i has %i " \
						"bytes fit to be read\n", \
						peasycap->field_read, \
						videofieldamount);
					JOT(8, "wakeup call to wq_video, " \
						"%i=field_read %i=field_fill "\
						"%i=parity\n", \
						peasycap->field_read, \
						peasycap->field_fill, \
						0x00FF & peasycap->\
						field_buffer[peasycap->\
						field_read][0].kount);
					wake_up_interruptible(&(peasycap->\
								wq_video));
					do_gettimeofday(&peasycap->timeval7);
					} else {
					peasycap->video_junk++;
					JOT(8, "field buffer %i had %i " \
						"bytes, now discarded\n", \
						peasycap->field_fill, \
						videofieldamount);

					(peasycap->field_fill)++;

					if (FIELD_BUFFER_MANY <= \
							peasycap->field_fill)
						peasycap->field_fill = 0;
					peasycap->field_page = 0;
					pfield_buffer = \
						&peasycap->field_buffer\
						[peasycap->field_fill]\
						[peasycap->field_page];
					pfield_buffer->pto = \
							pfield_buffer->pgo;

					JOT(8, "bumped to: %i=peasycap->" \
						"field_fill  %i=parity\n", \
						peasycap->field_fill, \
						0x00FF & pfield_buffer->kount);
				}
				if (8 == more) {
					JOT(8, "end-of-field: received " \
						"parity byte 0x%02X\n", \
						(0xFF & *pu));
					if (0x40 & *pu)
						pfield_buffer->kount = 0x0000;
					else
						pfield_buffer->kount = 0x0001;
					JOT(8, "end-of-field: 0x%02X=kount\n",\
						0xFF & pfield_buffer->kount);
				}
			}
/*---------------------------------------------------------------------------*/
/*
 *  COPY more BYTES FROM ISOC BUFFER TO FIELD BUFFER
 */
/*---------------------------------------------------------------------------*/
			pu += leap;
			more -= leap;

			if (FIELD_BUFFER_MANY <= peasycap->field_fill) {
				SAY("ERROR: bad peasycap->field_fill\n");
				return;
			}
			if (FIELD_BUFFER_SIZE/PAGE_SIZE <= \
							peasycap->field_page) {
				SAY("ERROR: bad peasycap->field_page\n");
				return;
			}
			pfield_buffer = &peasycap->field_buffer\
				[peasycap->field_fill][peasycap->field_page];
			while (more) {
				pfield_buffer = &peasycap->field_buffer\
						[peasycap->field_fill]\
						[peasycap->field_page];
				if (PAGE_SIZE < (pfield_buffer->pto - \
							pfield_buffer->pgo)) {
					SAY("ERROR: bad pfield_buffer->pto\n");
					return;
				}
				if (PAGE_SIZE == (pfield_buffer->pto - \
							pfield_buffer->pgo)) {
					(peasycap->field_page)++;
					if (FIELD_BUFFER_SIZE/PAGE_SIZE <= \
							peasycap->field_page) {
						JOT(16, "wrapping peasycap->" \
							"field_page\n");
						peasycap->field_page = 0;
					}
					pfield_buffer = &peasycap->\
							field_buffer\
							[peasycap->field_fill]\
							[peasycap->field_page];
					pfield_buffer->pto = \
							pfield_buffer->pgo;
				}

				much = PAGE_SIZE - (int)(pfield_buffer->pto - \
							pfield_buffer->pgo);

				if (much > more)
					much = more;
				memcpy(pfield_buffer->pto, pu, much);
				pu += much;
				(pfield_buffer->pto) += much;
				more -= much;
				}
			}
		}
	}
}
/*---------------------------------------------------------------------------*/
/*
 *
 *
 *             *** UNDER DEVELOPMENT/TESTING - NOT READY YET! ***
 *
 *
 *
 *  VIDEOTAPES MAY HAVE BEEN MANUALLY PAUSED AND RESTARTED DURING RECORDING.
 *  THIS CAUSES LOSS OF SYNC, CONFUSING DOWNSTREAM USERSPACE PROGRAMS WHICH
 *  MAY INTERPRET THE INTERRUPTION AS A SYMPTOM OF LATENCY.  TO OVERCOME THIS
 *  THE DRIVER BRIDGES THE HIATUS BY SENDING DUMMY VIDEO FRAMES AT ROUGHLY
 *  THE RIGHT TIME INTERVALS IN THE HOPE OF PERSUADING THE DOWNSTREAM USERSPACE
 *  PROGRAM TO RESUME NORMAL SERVICE WHEN THE INTERRUPTION IS OVER.
 */
/*---------------------------------------------------------------------------*/
#if defined(BRIDGER)
do_gettimeofday(&timeval);
if (peasycap->timeval7.tv_sec) {
	usec = 1000000*(timeval.tv_sec  - peasycap->timeval7.tv_sec) + \
			(timeval.tv_usec - peasycap->timeval7.tv_usec);
	if (usec > (peasycap->usec + peasycap->tolerate)) {
		JOT(8, "bridging hiatus\n");
		peasycap->video_junk = 0;
		peasycap->field_buffer[peasycap->field_fill][0].kount |= 0x0400;

		peasycap->field_read = (peasycap->field_fill)++;

		if (FIELD_BUFFER_MANY <= peasycap->field_fill) \
						peasycap->field_fill = 0;
		peasycap->field_page = 0;
		pfield_buffer = &peasycap->field_buffer\
				[peasycap->field_fill][peasycap->field_page];
		pfield_buffer->pto = pfield_buffer->pgo;

		JOT(8, "bumped to: %i=peasycap->field_fill  %i=parity\n", \
			peasycap->field_fill, 0x00FF & pfield_buffer->kount);
		JOT(8, "field buffer %i has %i bytes to be overwritten\n", \
			peasycap->field_read, videofieldamount);
		JOT(8, "wakeup call to wq_video, " \
			"%i=field_read %i=field_fill %i=parity\n", \
			peasycap->field_read, peasycap->field_fill, \
			0x00FF & \
			peasycap->field_buffer[peasycap->field_read][0].kount);
		wake_up_interruptible(&(peasycap->wq_video));
		do_gettimeofday(&peasycap->timeval7);
	}
}
#endif /*BRIDGER*/
/*---------------------------------------------------------------------------*/
/*
 *  RESUBMIT THIS URB, UNLESS A SEVERE PERSISTENT ERROR CONDITION EXISTS.
 *
 *  IF THE WAIT QUEUES ARE NOT CLEARED IN RESPONSE TO AN ERROR CONDITION
 *  THE USERSPACE PROGRAM, E.G. mplayer, MAY HANG ON EXIT.   BEWARE.
 */
/*---------------------------------------------------------------------------*/
if (VIDEO_ISOC_BUFFER_MANY <= peasycap->video_junk) {
	SAY("easycap driver shutting down on condition green\n");
	peasycap->video_eof = 1;
	peasycap->audio_eof = 1;
	peasycap->video_junk = -VIDEO_ISOC_BUFFER_MANY;
	wake_up_interruptible(&(peasycap->wq_video));
	wake_up_interruptible(&(peasycap->wq_audio));
	return;
}
if (peasycap->video_isoc_streaming) {
	rc = usb_submit_urb(purb, GFP_ATOMIC);
	if (0 != rc) {
		SAY("ERROR: while %i=video_idle, usb_submit_urb() failed " \
					"with rc:\n", peasycap->video_idle);
		switch (rc) {
		case -ENOMEM: {
			SAY("ENOMEM\n"); break;
		}
		case -ENODEV: {
			SAY("ENODEV\n"); break;
		}
		case -ENXIO: {
			SAY("ENXIO\n"); break;
		}
		case -EINVAL: {
			SAY("EINVAL\n"); break;
		}
		case -EAGAIN: {
			SAY("EAGAIN\n"); break;
		}
		case -EFBIG: {
			SAY("EFBIG\n"); break;
		}
		case -EPIPE: {
			SAY("EPIPE\n"); break;
		}
		case -EMSGSIZE: {
			SAY("EMSGSIZE\n");  break;
		}
		case -ENOSPC: {
			SAY("ENOSPC\n"); break;
		}
		default: {
			SAY("0x%08X\n", rc); break;
		}
		}
	}
}
return;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *
 *                                  FIXME
 *
 *
 *  THIS FUNCTION ASSUMES THAT, ON EACH AND EVERY OCCASION THAT THE DEVICE IS
 *  PHYSICALLY PLUGGED IN, INTERFACE 0 IS PROBED FIRST.
 *  IF THIS IS NOT TRUE, THERE IS THE POSSIBILITY OF AN Oops.
 *
 *  THIS HAS NEVER BEEN A PROBLEM IN PRACTICE, BUT SOMETHING SEEMS WRONG HERE.
 */
/*---------------------------------------------------------------------------*/
int
easycap_usb_probe(struct usb_interface *pusb_interface, \
						const struct usb_device_id *id)
{
struct usb_device *pusb_device, *pusb_device1;
struct usb_host_interface *pusb_host_interface;
struct usb_endpoint_descriptor *pepd;
struct usb_interface_descriptor *pusb_interface_descriptor;
struct usb_interface_assoc_descriptor *pusb_interface_assoc_descriptor;
struct urb *purb;
static struct easycap *peasycap /*=NULL*/;
struct data_urb *pdata_urb;
size_t wMaxPacketSize;
int ISOCwMaxPacketSize;
int BULKwMaxPacketSize;
int INTwMaxPacketSize;
int CTRLwMaxPacketSize;
__u8 bEndpointAddress;
__u8 ISOCbEndpointAddress;
__u8 INTbEndpointAddress;
int isin, i, j, k, m;
__u8 bInterfaceNumber;
__u8 bInterfaceClass;
__u8 bInterfaceSubClass;
void *pbuf;
int okalt[8], isokalt;
int okepn[8], isokepn;
int okmps[8], isokmps;
int maxpacketsize;
int rc;

JOT(4, "\n");

if ((struct usb_interface *)NULL == pusb_interface) {
	SAY("ERROR: pusb_interface is NULL\n");
	return -EFAULT;
}
/*---------------------------------------------------------------------------*/
/*
 *  GET POINTER TO STRUCTURE usb_device
 */
/*---------------------------------------------------------------------------*/
pusb_device1 = container_of(pusb_interface->dev.parent, \
						struct usb_device, dev);
if ((struct usb_device *)NULL == pusb_device1) {
	SAY("ERROR: pusb_device1 is NULL\n");
	return -EFAULT;
}
pusb_device = usb_get_dev(pusb_device1);
if ((struct usb_device *)NULL == pusb_device) {
	SAY("ERROR: pusb_device is NULL\n");
	return -EFAULT;
}
if ((unsigned long int)pusb_device1 != (unsigned long int)pusb_device) {
	JOT(4, "ERROR: pusb_device1 != pusb_device\n");
	return -EFAULT;
}

JOT(4, "bNumConfigurations=%i\n", pusb_device->descriptor.bNumConfigurations);

/*---------------------------------------------------------------------------*/
pusb_host_interface = pusb_interface->cur_altsetting;
if (NULL == pusb_host_interface) {
	SAY("ERROR: pusb_host_interface is NULL\n");
	return -EFAULT;
}
pusb_interface_descriptor = &(pusb_host_interface->desc);
if (NULL == pusb_interface_descriptor) {
	SAY("ERROR: pusb_interface_descriptor is NULL\n");
	return -EFAULT;
}
/*---------------------------------------------------------------------------*/
/*
 *  GET PROPERTIES OF PROBED INTERFACE
 */
/*---------------------------------------------------------------------------*/
bInterfaceNumber = pusb_interface_descriptor->bInterfaceNumber;
bInterfaceClass = pusb_interface_descriptor->bInterfaceClass;
bInterfaceSubClass = pusb_interface_descriptor->bInterfaceSubClass;

JOT(4, "intf[%i]: pusb_interface->num_altsetting=%i\n", \
			bInterfaceNumber, pusb_interface->num_altsetting);
JOT(4, "intf[%i]: pusb_interface->cur_altsetting - " \
			"pusb_interface->altsetting=%li\n", bInterfaceNumber, \
			(long int)(pusb_interface->cur_altsetting - \
						pusb_interface->altsetting));
switch (bInterfaceClass) {
case USB_CLASS_AUDIO: {
	JOT(4, "intf[%i]: bInterfaceClass=0x%02X=USB_CLASS_AUDIO\n", \
				bInterfaceNumber, bInterfaceClass); break;
	}
case USB_CLASS_VIDEO: {
	JOT(4, "intf[%i]: bInterfaceClass=0x%02X=USB_CLASS_VIDEO\n", \
				bInterfaceNumber, bInterfaceClass); break;
	}
case USB_CLASS_VENDOR_SPEC: {
	JOT(4, "intf[%i]: bInterfaceClass=0x%02X=USB_CLASS_VENDOR_SPEC\n", \
				bInterfaceNumber, bInterfaceClass); break;
	}
default:
	break;
}
switch (bInterfaceSubClass) {
case 0x01: {
	JOT(4, "intf[%i]: bInterfaceSubClass=0x%02X=AUDIOCONTROL\n", \
			bInterfaceNumber, bInterfaceSubClass); break;
}
case 0x02: {
	JOT(4, "intf[%i]: bInterfaceSubClass=0x%02X=AUDIOSTREAMING\n", \
			bInterfaceNumber, bInterfaceSubClass); break;
}
case 0x03: {
	JOT(4, "intf[%i]: bInterfaceSubClass=0x%02X=MIDISTREAMING\n", \
			bInterfaceNumber, bInterfaceSubClass); break;
}
default:
	break;
}
/*---------------------------------------------------------------------------*/
pusb_interface_assoc_descriptor = pusb_interface->intf_assoc;
if (NULL != pusb_interface_assoc_descriptor) {
	JOT(4, "intf[%i]: bFirstInterface=0x%02X  bInterfaceCount=0x%02X\n", \
			bInterfaceNumber, \
			pusb_interface_assoc_descriptor->bFirstInterface, \
			pusb_interface_assoc_descriptor->bInterfaceCount);
} else {
JOT(4, "intf[%i]: pusb_interface_assoc_descriptor is NULL\n", \
							bInterfaceNumber);
}
/*---------------------------------------------------------------------------*/
/*
 *  A NEW struct easycap IS ALWAYS ALLOCATED WHEN INTERFACE 0 IS PROBED.
 *  IT IS NOT POSSIBLE HERE TO FREE ANY EXISTING struct easycap.  THIS
 *  SHOULD HAVE BEEN DONE BY easycap_delete() WHEN THE DEVICE WAS PHYSICALLY
 *  UNPLUGGED.
 */
/*---------------------------------------------------------------------------*/
if (0 == bInterfaceNumber) {
	peasycap = kzalloc(sizeof(struct easycap), GFP_KERNEL);
	if (NULL == peasycap) {
		SAY("ERROR: Could not allocate peasycap\n");
		return -ENOMEM;
	} else {
		peasycap->allocation_video_struct = sizeof(struct easycap);
		peasycap->allocation_video_page = 0;
		peasycap->allocation_video_urb = 0;
		peasycap->allocation_audio_struct = 0;
		peasycap->allocation_audio_page = 0;
		peasycap->allocation_audio_urb = 0;
	}
/*---------------------------------------------------------------------------*/
/*
 *  INITIALIZE THE NEW easycap STRUCTURE.
 *  NO PARAMETERS ARE SPECIFIED HERE REQUIRING THE SETTING OF REGISTERS.
 *  THAT IS DONE FIRST BY easycap_open() AND LATER BY easycap_ioctl().
 */
/*---------------------------------------------------------------------------*/
	peasycap->pusb_device = pusb_device;
	peasycap->pusb_interface = pusb_interface;

	kref_init(&peasycap->kref);
	JOT(8, "intf[%i]: after kref_init(..._video) " \
			"%i=peasycap->kref.refcount.counter\n", \
			bInterfaceNumber, peasycap->kref.refcount.counter);

	init_waitqueue_head(&(peasycap->wq_video));
	init_waitqueue_head(&(peasycap->wq_audio));

	mutex_init(&(peasycap->mutex_timeval0));
	mutex_init(&(peasycap->mutex_timeval1));

	for (k = 0; k < FRAME_BUFFER_MANY; k++)
		mutex_init(&(peasycap->mutex_mmap_video[k]));

	peasycap->ilk = 0;
	peasycap->microphone = false;

	peasycap->video_interface = -1;
	peasycap->video_altsetting_on = -1;
	peasycap->video_altsetting_off = -1;
	peasycap->video_endpointnumber = -1;
	peasycap->video_isoc_maxframesize = -1;
	peasycap->video_isoc_buffer_size = -1;

	peasycap->audio_interface = -1;
	peasycap->audio_altsetting_on = -1;
	peasycap->audio_altsetting_off = -1;
	peasycap->audio_endpointnumber = -1;
	peasycap->audio_isoc_maxframesize = -1;
	peasycap->audio_isoc_buffer_size = -1;

	peasycap->frame_buffer_many = FRAME_BUFFER_MANY;

	if ((struct mutex *)NULL == &(peasycap->mutex_mmap_video[0])) {
		SAY("ERROR: &(peasycap->mutex_mmap_video[%i]) is NULL\n", 0);
		return -EFAULT;
	}
/*---------------------------------------------------------------------------*/
/*
 *  DYNAMICALLY FILL IN THE AVAILABLE FORMATS.
 */
/*---------------------------------------------------------------------------*/
	rc = fillin_formats();
	if (0 > rc) {
		SAY("ERROR: fillin_formats() returned %i\n", rc);
		return -EFAULT;
	}
	JOT(4, "%i formats available\n", rc);
	} else {
/*---------------------------------------------------------------------------*/
		if ((struct easycap *)NULL == peasycap) {
			SAY("ERROR: peasycap is NULL " \
					"when probing interface %i\n", \
							bInterfaceNumber);
			return -EFAULT;
		}

	JOT(8, "kref_get() with %i=peasycap->kref.refcount.counter\n", \
					(int)peasycap->kref.refcount.counter);
	kref_get(&peasycap->kref);
}
/*---------------------------------------------------------------------------*/
if ((USB_CLASS_VIDEO == bInterfaceClass) || \
	(USB_CLASS_VENDOR_SPEC == bInterfaceClass)) {
	if (-1 == peasycap->video_interface) {
		peasycap->video_interface = bInterfaceNumber;
		JOT(4, "setting peasycap->video_interface=%i\n", \
						peasycap->video_interface);
	} else {
		if (peasycap->video_interface != bInterfaceNumber) {
			SAY("ERROR: attempting to reset " \
					"peasycap->video_interface\n");
			SAY("...... continuing with " \
					"%i=peasycap->video_interface\n", \
					peasycap->video_interface);
		}
	}
} else if ((USB_CLASS_AUDIO == bInterfaceClass) && \
						(0x02 == bInterfaceSubClass)) {
	if (-1 == peasycap->audio_interface) {
		peasycap->audio_interface = bInterfaceNumber;
		JOT(4, "setting peasycap->audio_interface=%i\n", \
						 peasycap->audio_interface);
	} else {
		if (peasycap->audio_interface != bInterfaceNumber) {
			SAY("ERROR: attempting to reset " \
					"peasycap->audio_interface\n");
			SAY("...... continuing with " \
					"%i=peasycap->audio_interface\n", \
					peasycap->audio_interface);
		}
	}
}
/*---------------------------------------------------------------------------*/
/*
 *  INVESTIGATE ALL ALTSETTINGS.
 *  DONE IN DETAIL BECAUSE USB DEVICE 05e1:0408 HAS DISPARATE INCARNATIONS.
 */
/*---------------------------------------------------------------------------*/
isokalt = 0;
isokepn = 0;
isokmps = 0;

for (i = 0; i < pusb_interface->num_altsetting; i++) {
	pusb_host_interface = &(pusb_interface->altsetting[i]);
	if ((struct usb_host_interface *)NULL == pusb_host_interface) {
		SAY("ERROR: pusb_host_interface is NULL\n");
		return -EFAULT;
	}
	pusb_interface_descriptor = &(pusb_host_interface->desc);
	if ((struct usb_interface_descriptor *)NULL == \
						pusb_interface_descriptor) {
		SAY("ERROR: pusb_interface_descriptor is NULL\n");
		return -EFAULT;
	}

	JOT(4, "intf[%i]alt[%i]: desc.bDescriptorType=0x%02X\n", \
	bInterfaceNumber, i, pusb_interface_descriptor->bDescriptorType);
	JOT(4, "intf[%i]alt[%i]: desc.bInterfaceNumber=0x%02X\n", \
	bInterfaceNumber, i, pusb_interface_descriptor->bInterfaceNumber);
	JOT(4, "intf[%i]alt[%i]: desc.bAlternateSetting=0x%02X\n", \
	bInterfaceNumber, i, pusb_interface_descriptor->bAlternateSetting);
	JOT(4, "intf[%i]alt[%i]: desc.bNumEndpoints=0x%02X\n", \
	bInterfaceNumber, i, pusb_interface_descriptor->bNumEndpoints);
	JOT(4, "intf[%i]alt[%i]: desc.bInterfaceClass=0x%02X\n", \
	bInterfaceNumber, i, pusb_interface_descriptor->bInterfaceClass);
	JOT(4, "intf[%i]alt[%i]: desc.bInterfaceSubClass=0x%02X\n", \
	bInterfaceNumber, i, pusb_interface_descriptor->bInterfaceSubClass);
	JOT(4, "intf[%i]alt[%i]: desc.bInterfaceProtocol=0x%02X\n", \
	bInterfaceNumber, i, pusb_interface_descriptor->bInterfaceProtocol);
	JOT(4, "intf[%i]alt[%i]: desc.iInterface=0x%02X\n", \
	bInterfaceNumber, i, pusb_interface_descriptor->iInterface);

	ISOCwMaxPacketSize = -1;
	BULKwMaxPacketSize = -1;
	INTwMaxPacketSize = -1;
	CTRLwMaxPacketSize = -1;
	ISOCbEndpointAddress = 0;
	INTbEndpointAddress = 0;

	if (0 == pusb_interface_descriptor->bNumEndpoints)
				JOT(4, "intf[%i]alt[%i] has no endpoints\n", \
							bInterfaceNumber, i);
/*---------------------------------------------------------------------------*/
	for (j = 0; j < pusb_interface_descriptor->bNumEndpoints; j++) {
		pepd = &(pusb_host_interface->endpoint[j].desc);
		if ((struct usb_endpoint_descriptor *)NULL == pepd) {
			SAY("ERROR:  pepd is NULL.\n");
			SAY("...... skipping\n");
			continue;
		}
		wMaxPacketSize = le16_to_cpu(pepd->wMaxPacketSize);
		bEndpointAddress = pepd->bEndpointAddress;

		JOT(4, "intf[%i]alt[%i]end[%i]: bEndpointAddress=0x%X\n", \
				bInterfaceNumber, i, j, \
				pepd->bEndpointAddress);
		JOT(4, "intf[%i]alt[%i]end[%i]: bmAttributes=0x%X\n", \
				bInterfaceNumber, i, j, \
				pepd->bmAttributes);
		JOT(4, "intf[%i]alt[%i]end[%i]: wMaxPacketSize=%i\n", \
				bInterfaceNumber, i, j, \
				pepd->wMaxPacketSize);
		JOT(4, "intf[%i]alt[%i]end[%i]: bInterval=%i\n",
				bInterfaceNumber, i, j, \
				pepd->bInterval);

		if (pepd->bEndpointAddress & USB_DIR_IN) {
			JOT(4, "intf[%i]alt[%i]end[%i] is an  IN  endpoint\n",\
						bInterfaceNumber, i, j);
			isin = 1;
		} else {
			JOT(4, "intf[%i]alt[%i]end[%i] is an  OUT endpoint\n",\
						bInterfaceNumber, i, j);
			SAY("ERROR: OUT endpoint unexpected\n");
			SAY("...... continuing\n");
			isin = 0;
		}
		if ((pepd->bmAttributes & \
				USB_ENDPOINT_XFERTYPE_MASK) == \
				USB_ENDPOINT_XFER_ISOC) {
			JOT(4, "intf[%i]alt[%i]end[%i] is an ISOC endpoint\n",\
						bInterfaceNumber, i, j);
			if (isin) {
				switch (bInterfaceClass) {
				case USB_CLASS_VIDEO:
				case USB_CLASS_VENDOR_SPEC: {
					if (!peasycap) {
						SAY("MISTAKE: " \
							"peasycap is NULL\n");
						return -EFAULT;
					}
					if (pepd->wMaxPacketSize) {
						if (8 > isokalt) {
							okalt[isokalt] = i;
							JOT(4,\
							"%i=okalt[%i]\n", \
							okalt[isokalt], \
							isokalt);
							isokalt++;
						}
						if (8 > isokepn) {
							okepn[isokepn] = \
							pepd->\
							bEndpointAddress & \
							0x0F;
							JOT(4,\
							"%i=okepn[%i]\n", \
							okepn[isokepn], \
							isokepn);
							isokepn++;
						}
						if (8 > isokmps) {
							okmps[isokmps] = \
							le16_to_cpu(pepd->\
							wMaxPacketSize);
							JOT(4,\
							"%i=okmps[%i]\n", \
							okmps[isokmps], \
							isokmps);
							isokmps++;
						}
					} else {
						if (-1 == peasycap->\
							video_altsetting_off) {
							peasycap->\
							video_altsetting_off =\
									 i;
							JOT(4, "%i=video_" \
							"altsetting_off " \
								"<====\n", \
							peasycap->\
							video_altsetting_off);
						} else {
							SAY("ERROR: peasycap" \
							"->video_altsetting_" \
							"off already set\n");
							SAY("...... " \
							"continuing with " \
							"%i=peasycap->video_" \
							"altsetting_off\n", \
							peasycap->\
							video_altsetting_off);
						}
					}
					break;
				}
				case USB_CLASS_AUDIO: {
					if (0x02 != bInterfaceSubClass)
						break;
					if (!peasycap) {
						SAY("MISTAKE: " \
						"peasycap is NULL\n");
						return -EFAULT;
					}
					if (pepd->wMaxPacketSize) {
						if (8 > isokalt) {
							okalt[isokalt] = i ;
							JOT(4,\
							"%i=okalt[%i]\n", \
							okalt[isokalt], \
							isokalt);
							isokalt++;
						}
						if (8 > isokepn) {
							okepn[isokepn] = \
							pepd->\
							bEndpointAddress & \
							0x0F;
							JOT(4,\
							"%i=okepn[%i]\n", \
							okepn[isokepn], \
							isokepn);
							isokepn++;
						}
						if (8 > isokmps) {
							okmps[isokmps] = \
							le16_to_cpu(pepd->\
							wMaxPacketSize);
							JOT(4,\
							"%i=okmps[%i]\n",\
							okmps[isokmps], \
							isokmps);
							isokmps++;
						}
					} else {
						if (-1 == peasycap->\
							audio_altsetting_off) {
							peasycap->\
							audio_altsetting_off =\
									 i;
							JOT(4, "%i=audio_" \
							"altsetting_off " \
							"<====\n", \
							peasycap->\
							audio_altsetting_off);
						} else {
							SAY("ERROR: peasycap" \
							"->audio_altsetting_" \
							"off already set\n");
							SAY("...... " \
							"continuing with " \
							"%i=peasycap->\
							audio_altsetting_" \
							"off\n",
							peasycap->\
							audio_altsetting_off);
						}
					}
				break;
				}
				default:
					break;
				}
			}
		} else if ((pepd->bmAttributes & \
						USB_ENDPOINT_XFERTYPE_MASK) ==\
						USB_ENDPOINT_XFER_BULK) {
			JOT(4, "intf[%i]alt[%i]end[%i] is a  BULK endpoint\n",\
						bInterfaceNumber, i, j);
		} else if ((pepd->bmAttributes & \
						USB_ENDPOINT_XFERTYPE_MASK) ==\
						USB_ENDPOINT_XFER_INT) {
			JOT(4, "intf[%i]alt[%i]end[%i] is an  INT endpoint\n",\
						bInterfaceNumber, i, j);
		} else {
			JOT(4, "intf[%i]alt[%i]end[%i] is a  CTRL endpoint\n",\
						bInterfaceNumber, i, j);
		}
		if (0 == pepd->wMaxPacketSize) {
			JOT(4, "intf[%i]alt[%i]end[%i] " \
						"has zero packet size\n", \
						bInterfaceNumber, i, j);
		}
	}
}
/*---------------------------------------------------------------------------*/
/*
 *  PERFORM INITIALIZATION OF THE PROBED INTERFACE
 */
/*---------------------------------------------------------------------------*/
JOT(4, "initialization begins for interface %i\n", \
				pusb_interface_descriptor->bInterfaceNumber);
switch (bInterfaceNumber) {
/*---------------------------------------------------------------------------*/
/*
 *  INTERFACE 0 IS THE VIDEO INTERFACE
 */
/*---------------------------------------------------------------------------*/
case 0: {
	if (!peasycap) {
		SAY("MISTAKE: peasycap is NULL\n");
		return -EFAULT;
	}
	if (!isokalt) {
		SAY("ERROR:  no viable video_altsetting_on\n");
		return -ENOENT;
	} else {
		peasycap->video_altsetting_on = okalt[isokalt - 1];
		JOT(4, "%i=video_altsetting_on <====\n", \
					peasycap->video_altsetting_on);
	}
	if (!isokepn) {
		SAY("ERROR:  no viable video_endpointnumber\n");
		return -ENOENT;
	} else {
		peasycap->video_endpointnumber = okepn[isokepn - 1];
		JOT(4, "%i=video_endpointnumber\n", \
					peasycap->video_endpointnumber);
		}
	if (!isokmps) {
		SAY("ERROR:  no viable video_maxpacketsize\n");
		return -ENOENT;
/*---------------------------------------------------------------------------*/
/*
 *  DECIDE THE VIDEO STREAMING PARAMETERS
 */
/*---------------------------------------------------------------------------*/
	} else {
		maxpacketsize = okmps[isokmps - 1] - 1024;
		if (USB_2_0_MAXPACKETSIZE > maxpacketsize) {
			peasycap->video_isoc_maxframesize = maxpacketsize;
		} else {
			peasycap->video_isoc_maxframesize = \
							USB_2_0_MAXPACKETSIZE;
		}
		JOT(4, "%i=video_isoc_maxframesize\n", \
					peasycap->video_isoc_maxframesize);
		if (0 >= peasycap->video_isoc_maxframesize) {
			SAY("ERROR:  bad video_isoc_maxframesize\n");
			return -ENOENT;
		}
		peasycap->video_isoc_framesperdesc = VIDEO_ISOC_FRAMESPERDESC;
		JOT(4, "%i=video_isoc_framesperdesc\n", \
					peasycap->video_isoc_framesperdesc);
		if (0 >= peasycap->video_isoc_framesperdesc) {
			SAY("ERROR:  bad video_isoc_framesperdesc\n");
			return -ENOENT;
		}
		peasycap->video_isoc_buffer_size = \
					peasycap->video_isoc_maxframesize * \
					peasycap->video_isoc_framesperdesc;
		JOT(4, "%i=video_isoc_buffer_size\n", \
					peasycap->video_isoc_buffer_size);
		if ((PAGE_SIZE << VIDEO_ISOC_ORDER) < \
					peasycap->video_isoc_buffer_size) {
			SAY("MISTAKE: " \
				"peasycap->video_isoc_buffer_size too big\n");
			return -EFAULT;
		}
	}
/*---------------------------------------------------------------------------*/
	if (-1 == peasycap->video_interface) {
		SAY("MISTAKE:  video_interface is unset\n");
		return -EFAULT;
	}
	if (-1 == peasycap->video_altsetting_on) {
		SAY("MISTAKE:  video_altsetting_on is unset\n");
		return -EFAULT;
	}
	if (-1 == peasycap->video_altsetting_off) {
		SAY("MISTAKE:  video_interface_off is unset\n");
		return -EFAULT;
	}
	if (-1 == peasycap->video_endpointnumber) {
		SAY("MISTAKE:  video_endpointnumber is unset\n");
		return -EFAULT;
	}
	if (-1 == peasycap->video_isoc_maxframesize) {
		SAY("MISTAKE:  video_isoc_maxframesize is unset\n");
		return -EFAULT;
	}
	if (-1 == peasycap->video_isoc_buffer_size) {
		SAY("MISTAKE:  video_isoc_buffer_size is unset\n");
		return -EFAULT;
	}
/*---------------------------------------------------------------------------*/
/*
 *  ALLOCATE MEMORY FOR VIDEO BUFFERS.  LISTS MUST BE INITIALIZED FIRST.
 */
/*---------------------------------------------------------------------------*/
	INIT_LIST_HEAD(&(peasycap->urb_video_head));
	peasycap->purb_video_head = &(peasycap->urb_video_head);
/*---------------------------------------------------------------------------*/
	JOT(4, "allocating %i frame buffers of size %li\n",  \
			FRAME_BUFFER_MANY, (long int)FRAME_BUFFER_SIZE);
	JOT(4, ".... each scattered over %li pages\n", \
						FRAME_BUFFER_SIZE/PAGE_SIZE);

	for (k = 0;  k < FRAME_BUFFER_MANY;  k++) {
		for (m = 0;  m < FRAME_BUFFER_SIZE/PAGE_SIZE;  m++) {
			if ((void *)NULL != peasycap->frame_buffer[k][m].pgo)
				SAY("attempting to reallocate frame " \
								" buffers\n");
			else {
				pbuf = (void *)__get_free_page(GFP_KERNEL);
				if ((void *)NULL == pbuf) {
					SAY("ERROR: Could not allocate frame "\
						"buffer %i page %i\n", k, m);
					return -ENOMEM;
				} else
					peasycap->allocation_video_page += 1;
				peasycap->frame_buffer[k][m].pgo = pbuf;
			}
			peasycap->frame_buffer[k][m].pto = \
					peasycap->frame_buffer[k][m].pgo;
		}
	}

	peasycap->frame_fill = 0;
	peasycap->frame_read = 0;
	JOT(4, "allocation of frame buffers done:  %i pages\n", k * \
								m);
/*---------------------------------------------------------------------------*/
	JOT(4, "allocating %i field buffers of size %li\n",  \
			FIELD_BUFFER_MANY, (long int)FIELD_BUFFER_SIZE);
	JOT(4, ".... each scattered over %li pages\n", \
					FIELD_BUFFER_SIZE/PAGE_SIZE);

	for (k = 0;  k < FIELD_BUFFER_MANY;  k++) {
		for (m = 0;  m < FIELD_BUFFER_SIZE/PAGE_SIZE;  m++) {
			if ((void *)NULL != peasycap->field_buffer[k][m].pgo) {
				SAY("ERROR: attempting to reallocate " \
							"field buffers\n");
			} else {
				pbuf = (void *) __get_free_page(GFP_KERNEL);
				if ((void *)NULL == pbuf) {
					SAY("ERROR: Could not allocate field" \
						" buffer %i page %i\n", k, m);
					return -ENOMEM;
					}
				else
					peasycap->allocation_video_page += 1;
				peasycap->field_buffer[k][m].pgo = pbuf;
				}
			peasycap->field_buffer[k][m].pto = \
					peasycap->field_buffer[k][m].pgo;
		}
		peasycap->field_buffer[k][0].kount = 0x0200;
	}
	peasycap->field_fill = 0;
	peasycap->field_page = 0;
	peasycap->field_read = 0;
	JOT(4, "allocation of field buffers done:  %i pages\n", k * \
								m);
/*---------------------------------------------------------------------------*/
	JOT(4, "allocating %i isoc video buffers of size %i\n",  \
					VIDEO_ISOC_BUFFER_MANY, \
					peasycap->video_isoc_buffer_size);
	JOT(4, ".... each occupying contiguous memory pages\n");

	for (k = 0;  k < VIDEO_ISOC_BUFFER_MANY; k++) {
		pbuf = (void *)__get_free_pages(GFP_KERNEL, VIDEO_ISOC_ORDER);
		if (NULL == pbuf) {
			SAY("ERROR: Could not allocate isoc video buffer " \
								"%i\n", k);
			return -ENOMEM;
		} else
			peasycap->allocation_video_page += \
				((unsigned int)(0x01 << VIDEO_ISOC_ORDER));

		peasycap->video_isoc_buffer[k].pgo = pbuf;
		peasycap->video_isoc_buffer[k].pto = pbuf + \
					peasycap->video_isoc_buffer_size;
		peasycap->video_isoc_buffer[k].kount = k;
	}
	JOT(4, "allocation of isoc video buffers done: %i pages\n", \
					k * (0x01 << VIDEO_ISOC_ORDER));
/*---------------------------------------------------------------------------*/
/*
 *  ALLOCATE AND INITIALIZE MULTIPLE struct urb ...
 */
/*---------------------------------------------------------------------------*/
	JOT(4, "allocating %i struct urb.\n", VIDEO_ISOC_BUFFER_MANY);
	JOT(4, "using %i=peasycap->video_isoc_framesperdesc\n", \
					peasycap->video_isoc_framesperdesc);
	JOT(4, "using %i=peasycap->video_isoc_maxframesize\n", \
					peasycap->video_isoc_maxframesize);
	JOT(4, "using %i=peasycap->video_isoc_buffer_sizen", \
					peasycap->video_isoc_buffer_size);

	for (k = 0;  k < VIDEO_ISOC_BUFFER_MANY; k++) {
		purb = usb_alloc_urb(peasycap->video_isoc_framesperdesc, \
								GFP_KERNEL);
		if (NULL == purb) {
			SAY("ERROR: usb_alloc_urb returned NULL for buffer " \
								"%i\n", k);
			return -ENOMEM;
		} else
			peasycap->allocation_video_urb += 1;
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
		pdata_urb = kzalloc(sizeof(struct data_urb), GFP_KERNEL);
		if (NULL == pdata_urb) {
			SAY("ERROR: Could not allocate struct data_urb.\n");
			return -ENOMEM;
		} else
			peasycap->allocation_video_struct += \
						sizeof(struct data_urb);

		pdata_urb->purb = purb;
		pdata_urb->isbuf = k;
		pdata_urb->length = 0;
		list_add_tail(&(pdata_urb->list_head), \
						peasycap->purb_video_head);
/*---------------------------------------------------------------------------*/
/*
 *  ... AND INITIALIZE THEM
 */
/*---------------------------------------------------------------------------*/
		if (!k) {
			JOT(4, "initializing video urbs thus:\n");
			JOT(4, "  purb->interval = 1;\n");
			JOT(4, "  purb->dev = peasycap->pusb_device;\n");
			JOT(4, "  purb->pipe = usb_rcvisocpipe" \
					"(peasycap->pusb_device,%i);\n", \
					peasycap->video_endpointnumber);
			JOT(4, "  purb->transfer_flags = URB_ISO_ASAP;\n");
			JOT(4, "  purb->transfer_buffer = peasycap->" \
					"video_isoc_buffer[.].pgo;\n");
			JOT(4, "  purb->transfer_buffer_length = %i;\n", \
					peasycap->video_isoc_buffer_size);
			JOT(4, "  purb->complete = easycap_complete;\n");
			JOT(4, "  purb->context = peasycap;\n");
			JOT(4, "  purb->start_frame = 0;\n");
			JOT(4, "  purb->number_of_packets = %i;\n", \
					peasycap->video_isoc_framesperdesc);
			JOT(4, "  for (j = 0; j < %i; j++)\n", \
					peasycap->video_isoc_framesperdesc);
			JOT(4, "    {\n");
			JOT(4, "    purb->iso_frame_desc[j].offset = j*%i;\n",\
					peasycap->video_isoc_maxframesize);
			JOT(4, "    purb->iso_frame_desc[j].length = %i;\n", \
					peasycap->video_isoc_maxframesize);
			JOT(4, "    }\n");
		}

		purb->interval = 1;
		purb->dev = peasycap->pusb_device;
		purb->pipe = usb_rcvisocpipe(peasycap->pusb_device, \
					peasycap->video_endpointnumber);
		purb->transfer_flags = URB_ISO_ASAP;
		purb->transfer_buffer = peasycap->video_isoc_buffer[k].pgo;
		purb->transfer_buffer_length = \
					peasycap->video_isoc_buffer_size;
		purb->complete = easycap_complete;
		purb->context = peasycap;
		purb->start_frame = 0;
		purb->number_of_packets = peasycap->video_isoc_framesperdesc;
		for (j = 0;  j < peasycap->video_isoc_framesperdesc; j++) {
			purb->iso_frame_desc[j].offset = j * \
					peasycap->video_isoc_maxframesize;
			purb->iso_frame_desc[j].length = \
					peasycap->video_isoc_maxframesize;
		}
	}
	JOT(4, "allocation of %i struct urb done.\n", k);
/*--------------------------------------------------------------------------*/
/*
 *  SAVE POINTER peasycap IN THIS INTERFACE.
 */
/*--------------------------------------------------------------------------*/
	usb_set_intfdata(pusb_interface, peasycap);
/*--------------------------------------------------------------------------*/
/*
 *  THE VIDEO DEVICE CAN BE REGISTERED NOW, AS IT IS READY.
 */
/*--------------------------------------------------------------------------*/
#if (!defined(EASYCAP_IS_VIDEODEV_CLIENT))
	if (0 != (usb_register_dev(pusb_interface, &easycap_class))) {
		err("Not able to get a minor for this device");
		usb_set_intfdata(pusb_interface, NULL);
		return -ENODEV;
	} else
		(peasycap->registered_video)++;
	SAY("easycap attached to minor #%d\n", pusb_interface->minor);
	break;
/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
#else
	pvideo_device = (struct video_device *)\
			kzalloc(sizeof(struct video_device), GFP_KERNEL);
	if ((struct video_device *)NULL == pvideo_device) {
		SAY("ERROR: Could not allocate structure video_device\n");
		return -ENOMEM;
	}
	if (VIDEO_DEVICE_MANY <= video_device_many) {
		SAY("ERROR: Too many /dev/videos\n");
		return -ENOMEM;
	}
	pvideo_array[video_device_many] = pvideo_device;  video_device_many++;

	strcpy(&pvideo_device->name[0], "easycapdc60");
#if defined(EASYCAP_NEEDS_V4L2_FOPS)
	pvideo_device->fops = &v4l2_fops;
#else
	pvideo_device->fops = &easycap_fops;
#endif /*EASYCAP_NEEDS_V4L2_FOPS*/
	pvideo_device->minor = -1;
	pvideo_device->release = (void *)(&videodev_release);

	video_set_drvdata(pvideo_device, (void *)peasycap);

	rc = video_register_device(pvideo_device, VFL_TYPE_GRABBER, -1);
	if (0 != rc) {
		err("Not able to register with videodev");
		videodev_release(pvideo_device);
		return -ENODEV;
	} else {
		peasycap->pvideo_device = pvideo_device;
		(peasycap->registered_video)++;
		JOT(4, "registered with videodev: %i=minor\n", \
							pvideo_device->minor);
	}
/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
#endif /*EASYCAP_IS_VIDEODEV_CLIENT*/
	break;
}
/*--------------------------------------------------------------------------*/
/*
 *  INTERFACE 1 IS THE AUDIO CONTROL INTERFACE
 *  INTERFACE 2 IS THE AUDIO STREAMING INTERFACE
 */
/*--------------------------------------------------------------------------*/
case 1: {
/*--------------------------------------------------------------------------*/
/*
 *  SAVE POINTER peasycap IN INTERFACE 1
 */
/*--------------------------------------------------------------------------*/
	usb_set_intfdata(pusb_interface, peasycap);
	JOT(4, "no initialization required for interface %i\n", \
				pusb_interface_descriptor->bInterfaceNumber);
	break;
}
/*--------------------------------------------------------------------------*/
case 2: {
	if (!peasycap) {
		SAY("MISTAKE: peasycap is NULL\n");
		return -EFAULT;
	}
	if (!isokalt) {
		SAY("ERROR:  no viable audio_altsetting_on\n");
		return -ENOENT;
	} else {
		peasycap->audio_altsetting_on = okalt[isokalt - 1];
		JOT(4, "%i=audio_altsetting_on <====\n", \
						peasycap->audio_altsetting_on);
	}
	if (!isokepn) {
		SAY("ERROR:  no viable audio_endpointnumber\n");
		return -ENOENT;
	} else {
		peasycap->audio_endpointnumber = okepn[isokepn - 1];
		JOT(4, "%i=audio_endpointnumber\n", \
					peasycap->audio_endpointnumber);
	}
	if (!isokmps) {
		SAY("ERROR:  no viable audio_maxpacketsize\n");
		return -ENOENT;
	} else {
		peasycap->audio_isoc_maxframesize = okmps[isokmps - 1];
		JOT(4, "%i=audio_isoc_maxframesize\n", \
					peasycap->audio_isoc_maxframesize);
		if (0 >= peasycap->audio_isoc_maxframesize) {
			SAY("ERROR:  bad audio_isoc_maxframesize\n");
			return -ENOENT;
		}
		if (9 == peasycap->audio_isoc_maxframesize) {
			peasycap->ilk |= 0x02;
			SAY("hardware is FOUR-CVBS\n");
			peasycap->microphone = true;
			peasycap->audio_pages_per_fragment = 4;
		} else if (256 == peasycap->audio_isoc_maxframesize) {
			peasycap->ilk &= ~0x02;
			SAY("hardware is CVBS+S-VIDEO\n");
			peasycap->microphone = false;
			peasycap->audio_pages_per_fragment = 4;
		} else {
			SAY("hardware is unidentified:\n");
			SAY("%i=audio_isoc_maxframesize\n", \
					peasycap->audio_isoc_maxframesize);
			return -ENOENT;
		}

		peasycap->audio_bytes_per_fragment = \
					peasycap->audio_pages_per_fragment * \
								PAGE_SIZE ;
		peasycap->audio_buffer_page_many = (AUDIO_FRAGMENT_MANY * \
					peasycap->audio_pages_per_fragment);

		JOT(4, "%6i=AUDIO_FRAGMENT_MANY\n", AUDIO_FRAGMENT_MANY);
		JOT(4, "%6i=audio_pages_per_fragment\n", \
					peasycap->audio_pages_per_fragment);
		JOT(4, "%6i=audio_bytes_per_fragment\n", \
					peasycap->audio_bytes_per_fragment);
		JOT(4, "%6i=audio_buffer_page_many\n", \
					peasycap->audio_buffer_page_many);

		peasycap->audio_isoc_framesperdesc = 128;

		JOT(4, "%i=audio_isoc_framesperdesc\n", \
					peasycap->audio_isoc_framesperdesc);
		if (0 >= peasycap->audio_isoc_framesperdesc) {
			SAY("ERROR:  bad audio_isoc_framesperdesc\n");
			return -ENOENT;
		}

		peasycap->audio_isoc_buffer_size = \
				peasycap->audio_isoc_maxframesize * \
				peasycap->audio_isoc_framesperdesc;
		JOT(4, "%i=audio_isoc_buffer_size\n", \
					peasycap->audio_isoc_buffer_size);
		if (AUDIO_ISOC_BUFFER_SIZE < \
					peasycap->audio_isoc_buffer_size) {
			SAY("MISTAKE:  audio_isoc_buffer_size bigger "
			"than %li=AUDIO_ISOC_BUFFER_SIZE\n", \
						AUDIO_ISOC_BUFFER_SIZE);
			return -EFAULT;
		}
	}

	if (-1 == peasycap->audio_interface) {
		SAY("MISTAKE:  audio_interface is unset\n");
		return -EFAULT;
	}
	if (-1 == peasycap->audio_altsetting_on) {
		SAY("MISTAKE:  audio_altsetting_on is unset\n");
		return -EFAULT;
	}
	if (-1 == peasycap->audio_altsetting_off) {
		SAY("MISTAKE:  audio_interface_off is unset\n");
		return -EFAULT;
	}
	if (-1 == peasycap->audio_endpointnumber) {
		SAY("MISTAKE:  audio_endpointnumber is unset\n");
		return -EFAULT;
	}
	if (-1 == peasycap->audio_isoc_maxframesize) {
		SAY("MISTAKE:  audio_isoc_maxframesize is unset\n");
		return -EFAULT;
	}
	if (-1 == peasycap->audio_isoc_buffer_size) {
		SAY("MISTAKE:  audio_isoc_buffer_size is unset\n");
		return -EFAULT;
	}
/*---------------------------------------------------------------------------*/
/*
 *  ALLOCATE MEMORY FOR AUDIO BUFFERS.  LISTS MUST BE INITIALIZED FIRST.
 */
/*---------------------------------------------------------------------------*/
	INIT_LIST_HEAD(&(peasycap->urb_audio_head));
	peasycap->purb_audio_head = &(peasycap->urb_audio_head);

	JOT(4, "allocating an audio buffer\n");
	JOT(4, ".... scattered over %i pages\n", \
					peasycap->audio_buffer_page_many);

	for (k = 0;  k < peasycap->audio_buffer_page_many;  k++) {
		if ((void *)NULL != peasycap->audio_buffer[k].pgo) {
			SAY("ERROR: attempting to reallocate audio buffers\n");
		} else {
			pbuf = (void *) __get_free_page(GFP_KERNEL);
			if ((void *)NULL == pbuf) {
				SAY("ERROR: Could not allocate audio " \
							"buffer page %i\n", k);
				return -ENOMEM;
			} else
				peasycap->allocation_audio_page += 1;

			peasycap->audio_buffer[k].pgo = pbuf;
		}
		peasycap->audio_buffer[k].pto = peasycap->audio_buffer[k].pgo;
	}

	peasycap->audio_fill = 0;
	peasycap->audio_read = 0;
	JOT(4, "allocation of audio buffer done:  %i pages\n", k);
/*---------------------------------------------------------------------------*/
	JOT(4, "allocating %i isoc audio buffers of size %i\n",  \
		AUDIO_ISOC_BUFFER_MANY, peasycap->audio_isoc_buffer_size);
	JOT(4, ".... each occupying contiguous memory pages\n");

	for (k = 0;  k < AUDIO_ISOC_BUFFER_MANY;  k++) {
		pbuf = (void *)__get_free_pages(GFP_KERNEL, AUDIO_ISOC_ORDER);
		if (NULL == pbuf) {
			SAY("ERROR: Could not allocate isoc audio buffer " \
							"%i\n", k);
			return -ENOMEM;
		} else
			peasycap->allocation_audio_page += \
				((unsigned int)(0x01 << AUDIO_ISOC_ORDER));

		peasycap->audio_isoc_buffer[k].pgo = pbuf;
		peasycap->audio_isoc_buffer[k].pto = pbuf + \
		peasycap->audio_isoc_buffer_size;
		peasycap->audio_isoc_buffer[k].kount = k;
	}
	JOT(4, "allocation of isoc audio buffers done.\n");
/*---------------------------------------------------------------------------*/
/*
 *  ALLOCATE AND INITIALIZE MULTIPLE struct urb ...
 */
/*---------------------------------------------------------------------------*/
	JOT(4, "allocating %i struct urb.\n", AUDIO_ISOC_BUFFER_MANY);
	JOT(4, "using %i=peasycap->audio_isoc_framesperdesc\n", \
					peasycap->audio_isoc_framesperdesc);
	JOT(4, "using %i=peasycap->audio_isoc_maxframesize\n", \
					peasycap->audio_isoc_maxframesize);
	JOT(4, "using %i=peasycap->audio_isoc_buffer_size\n", \
					peasycap->audio_isoc_buffer_size);

	for (k = 0;  k < AUDIO_ISOC_BUFFER_MANY; k++) {
		purb = usb_alloc_urb(peasycap->audio_isoc_framesperdesc, \
								GFP_KERNEL);
		if (NULL == purb) {
			SAY("ERROR: usb_alloc_urb returned NULL for buffer " \
							"%i\n", k);
			return -ENOMEM;
		} else
			peasycap->allocation_audio_urb += 1 ;
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
		pdata_urb = kzalloc(sizeof(struct data_urb), GFP_KERNEL);
		if (NULL == pdata_urb) {
			SAY("ERROR: Could not allocate struct data_urb.\n");
			return -ENOMEM;
		} else
			peasycap->allocation_audio_struct += \
						sizeof(struct data_urb);

		pdata_urb->purb = purb;
		pdata_urb->isbuf = k;
		pdata_urb->length = 0;
		list_add_tail(&(pdata_urb->list_head), \
						peasycap->purb_audio_head);
/*---------------------------------------------------------------------------*/
/*
 *  ... AND INITIALIZE THEM
 */
/*---------------------------------------------------------------------------*/
		if (!k) {
			JOT(4, "initializing audio urbs thus:\n");
			JOT(4, "  purb->interval = 1;\n");
			JOT(4, "  purb->dev = peasycap->pusb_device;\n");
			JOT(4, "  purb->pipe = usb_rcvisocpipe(peasycap->" \
					"pusb_device,%i);\n", \
					peasycap->audio_endpointnumber);
			JOT(4, "  purb->transfer_flags = URB_ISO_ASAP;\n");
			JOT(4, "  purb->transfer_buffer = " \
				"peasycap->audio_isoc_buffer[.].pgo;\n");
			JOT(4, "  purb->transfer_buffer_length = %i;\n", \
					peasycap->audio_isoc_buffer_size);
			JOT(4, "  purb->complete = easysnd_complete;\n");
			JOT(4, "  purb->context = peasycap;\n");
			JOT(4, "  purb->start_frame = 0;\n");
			JOT(4, "  purb->number_of_packets = %i;\n", \
					peasycap->audio_isoc_framesperdesc);
			JOT(4, "  for (j = 0; j < %i; j++)\n", \
					peasycap->audio_isoc_framesperdesc);
			JOT(4, "    {\n");
			JOT(4, "    purb->iso_frame_desc[j].offset = j*%i;\n",\
					peasycap->audio_isoc_maxframesize);
			JOT(4, "    purb->iso_frame_desc[j].length = %i;\n", \
					peasycap->audio_isoc_maxframesize);
			JOT(4, "    }\n");
			}

		purb->interval = 1;
		purb->dev = peasycap->pusb_device;
		purb->pipe = usb_rcvisocpipe(peasycap->pusb_device, \
					peasycap->audio_endpointnumber);
		purb->transfer_flags = URB_ISO_ASAP;
		purb->transfer_buffer = peasycap->audio_isoc_buffer[k].pgo;
		purb->transfer_buffer_length = \
					peasycap->audio_isoc_buffer_size;
		purb->complete = easysnd_complete;
		purb->context = peasycap;
		purb->start_frame = 0;
		purb->number_of_packets = peasycap->audio_isoc_framesperdesc;
		for (j = 0;  j < peasycap->audio_isoc_framesperdesc; j++) {
			purb->iso_frame_desc[j].offset = j * \
					peasycap->audio_isoc_maxframesize;
			purb->iso_frame_desc[j].length = \
					peasycap->audio_isoc_maxframesize;
		}
	}
	JOT(4, "allocation of %i struct urb done.\n", k);
/*---------------------------------------------------------------------------*/
/*
 *  SAVE POINTER peasycap IN THIS INTERFACE.
 */
/*---------------------------------------------------------------------------*/
	usb_set_intfdata(pusb_interface, peasycap);
/*---------------------------------------------------------------------------*/
/*
 *  THE AUDIO DEVICE CAN BE REGISTERED NOW, AS IT IS READY.
 */
/*---------------------------------------------------------------------------*/
	rc = usb_register_dev(pusb_interface, &easysnd_class);
	if (0 != rc) {
		err("Not able to get a minor for this device.");
		usb_set_intfdata(pusb_interface, NULL);
		return -ENODEV;
	} else
		(peasycap->registered_audio)++;
/*---------------------------------------------------------------------------*/
/*
 *  LET THE USER KNOW WHAT NODE THE AUDIO DEVICE IS ATTACHED TO.
 */
/*---------------------------------------------------------------------------*/
	SAY("easysnd attached to minor #%d\n", pusb_interface->minor);
	break;
}
/*---------------------------------------------------------------------------*/
/*
 *  INTERFACES OTHER THAN 0, 1 AND 2 ARE UNEXPECTED
 */
/*---------------------------------------------------------------------------*/
default: {
	JOT(4, "ERROR: unexpected interface %i\n", bInterfaceNumber);
	return -EINVAL;
}
}
JOT(4, "ends successfully for interface %i\n", \
				pusb_interface_descriptor->bInterfaceNumber);
return 0;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  WHEN THIS FUNCTION IS CALLED THE DEVICE HAS ALREADY BEEN PHYSICALLY
 *  UNPLUGGED.
 *  HENCE peasycap->pusb_device IS NO LONGER VALID AND MUST BE SET TO NULL.
 */
/*---------------------------------------------------------------------------*/
void
easycap_usb_disconnect(struct usb_interface *pusb_interface)
{
struct usb_host_interface *pusb_host_interface;
struct usb_interface_descriptor *pusb_interface_descriptor;
__u8 bInterfaceNumber;
struct easycap *peasycap;

struct list_head *plist_head;
struct data_urb *pdata_urb;
int minor, m;

JOT(4, "\n");

if ((struct usb_interface *)NULL == pusb_interface) {
	JOT(4, "ERROR: pusb_interface is NULL\n");
	return;
}
pusb_host_interface = pusb_interface->cur_altsetting;
if ((struct usb_host_interface *)NULL == pusb_host_interface) {
	JOT(4, "ERROR: pusb_host_interface is NULL\n");
	return;
}
pusb_interface_descriptor = &(pusb_host_interface->desc);
if ((struct usb_interface_descriptor *)NULL == pusb_interface_descriptor) {
	JOT(4, "ERROR: pusb_interface_descriptor is NULL\n");
	return;
}
bInterfaceNumber = pusb_interface_descriptor->bInterfaceNumber;
minor = pusb_interface->minor;
JOT(4, "intf[%i]: minor=%i\n", bInterfaceNumber, minor);

peasycap = usb_get_intfdata(pusb_interface);
if ((struct easycap *)NULL == peasycap)
	SAY("ERROR: peasycap is NULL\n");
else {
	peasycap->pusb_device = (struct usb_device *)NULL;
	switch (bInterfaceNumber) {
/*---------------------------------------------------------------------------*/
	case 0: {
		if ((struct list_head *)NULL != peasycap->purb_video_head) {
			JOT(4, "killing video urbs\n");
			m = 0;
			list_for_each(plist_head, (peasycap->purb_video_head))
				{
				pdata_urb = list_entry(plist_head, \
						struct data_urb, list_head);
				if ((struct data_urb *)NULL != pdata_urb) {
					if ((struct urb *)NULL != \
							pdata_urb->purb) {
						usb_kill_urb(pdata_urb->purb);
						m++;
					}
				}
			}
			JOT(4, "%i video urbs killed\n", m);
		} else
			SAY("ERROR: peasycap->purb_video_head is NULL\n");
		break;
	}
/*---------------------------------------------------------------------------*/
	case 2: {
		if ((struct list_head *)NULL != peasycap->purb_audio_head) {
			JOT(4, "killing audio urbs\n");
			m = 0;
			list_for_each(plist_head, \
						(peasycap->purb_audio_head)) {
				pdata_urb = list_entry(plist_head, \
						struct data_urb, list_head);
				if ((struct data_urb *)NULL != pdata_urb) {
					if ((struct urb *)NULL != \
							pdata_urb->purb) {
						usb_kill_urb(pdata_urb->purb);
						m++;
					}
				}
			}
			JOT(4, "%i audio urbs killed\n", m);
		} else
			SAY("ERROR: peasycap->purb_audio_head is NULL\n");
		break;
	}
/*---------------------------------------------------------------------------*/
	default:
		break;
	}
}
/*--------------------------------------------------------------------------*/
/*
 *  DEREGISTER
 */
/*--------------------------------------------------------------------------*/
switch (bInterfaceNumber) {
case 0: {
#if (!defined(EASYCAP_IS_VIDEODEV_CLIENT))
	if ((struct easycap *)NULL == peasycap) {
		SAY("ERROR: peasycap has become NULL\n");
	} else {
		lock_kernel();
		usb_deregister_dev(pusb_interface, &easycap_class);
		(peasycap->registered_video)--;

		JOT(4, "intf[%i]: usb_deregister_dev()\n", bInterfaceNumber);
		unlock_kernel();
		SAY("easycap detached from minor #%d\n", minor);
	}
/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
#else
	if ((struct easycap *)NULL == peasycap)
		SAY("ERROR: peasycap has become NULL\n");
	else {
		lock_kernel();
		video_unregister_device(peasycap->pvideo_device);
		(peasycap->registered_video)--;
		unlock_kernel();
		JOT(4, "unregistered with videodev: %i=minor\n", \
							pvideo_device->minor);
	}
/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
#endif /*EASYCAP_IS_VIDEODEV_CLIENT*/
	break;
}
case 2: {
	lock_kernel();

	usb_deregister_dev(pusb_interface, &easysnd_class);
	if ((struct easycap *)NULL != peasycap)
		(peasycap->registered_audio)--;

	JOT(4, "intf[%i]: usb_deregister_dev()\n", bInterfaceNumber);
	unlock_kernel();

	SAY("easysnd detached from minor #%d\n", minor);
	break;
}
default:
	break;
}
/*---------------------------------------------------------------------------*/
/*
 *  CALL easycap_delete() IF NO REMAINING REFERENCES TO peasycap
 */
/*---------------------------------------------------------------------------*/
if ((struct easycap *)NULL == peasycap) {
	SAY("ERROR: peasycap has become NULL\n");
	SAY("cannot call kref_put()\n");
	SAY("ending unsuccessfully: may cause memory leak\n");
	return;
}
if (!peasycap->kref.refcount.counter) {
	SAY("ERROR: peasycap->kref.refcount.counter is zero " \
						"so cannot call kref_put()\n");
	SAY("ending unsuccessfully: may cause memory leak\n");
	return;
}
JOT(4, "intf[%i]: kref_put() with %i=peasycap->kref.refcount.counter\n", \
		bInterfaceNumber, (int)peasycap->kref.refcount.counter);
kref_put(&peasycap->kref, easycap_delete);
JOT(4, "intf[%i]: kref_put() done.\n", bInterfaceNumber);
/*---------------------------------------------------------------------------*/

JOT(4, "ends\n");
return;
}
/*****************************************************************************/
int __init
easycap_module_init(void)
{
int result;

SAY("========easycap=======\n");
JOT(4, "begins.  %i=debug\n", easycap_debug);
SAY("version: " EASYCAP_DRIVER_VERSION "\n");
/*---------------------------------------------------------------------------*/
/*
 *  REGISTER THIS DRIVER WITH THE USB SUBSYTEM.
 */
/*---------------------------------------------------------------------------*/
JOT(4, "registering driver easycap\n");

result = usb_register(&easycap_usb_driver);
if (0 != result)
	SAY("ERROR:  usb_register returned %i\n", result);

JOT(4, "ends\n");
return result;
}
/*****************************************************************************/
void __exit
easycap_module_exit(void)
{
JOT(4, "begins\n");

/*---------------------------------------------------------------------------*/
/*
 *  DEREGISTER THIS DRIVER WITH THE USB SUBSYTEM.
 */
/*---------------------------------------------------------------------------*/
usb_deregister(&easycap_usb_driver);

JOT(4, "ends\n");
}
/*****************************************************************************/

module_init(easycap_module_init);
module_exit(easycap_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("R.M. Thomas <rmthomas@sciolus.org>");
MODULE_DESCRIPTION(EASYCAP_DRIVER_DESCRIPTION);
MODULE_VERSION(EASYCAP_DRIVER_VERSION);
#if defined(EASYCAP_DEBUG)
MODULE_PARM_DESC(easycap_debug, "debug: 0 (default), 1, 2,...");
#endif /*EASYCAP_DEBUG*/
/*****************************************************************************/
