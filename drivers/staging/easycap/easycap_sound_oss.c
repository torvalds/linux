/******************************************************************************
*                                                                             *
*  easycap_sound.c                                                            *
*                                                                             *
*  Audio driver for EasyCAP USB2.0 Video Capture Device DC60                  *
*                                                                             *
*                                                                             *
******************************************************************************/
/*
 *
 *  Copyright (C) 2010 R.M. Thomas  <rmthomas@sciolus.org>
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

/*****************************************************************************/
/****************************                       **************************/
/****************************   Open Sound System   **************************/
/****************************                       **************************/
/*****************************************************************************/
/*--------------------------------------------------------------------------*/
/*
 *  PARAMETERS USED WHEN REGISTERING THE AUDIO INTERFACE
 */
/*--------------------------------------------------------------------------*/
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  ON COMPLETION OF AN AUDIO URB ITS DATA IS COPIED TO THE AUDIO BUFFERS
 *  PROVIDED peasycap->audio_idle IS ZERO.  REGARDLESS OF THIS BEING TRUE,
 *  IT IS RESUBMITTED PROVIDED peasycap->audio_isoc_streaming IS NOT ZERO.
 */
/*---------------------------------------------------------------------------*/
void
easyoss_complete(struct urb *purb)
{
	struct easycap *peasycap;
	struct data_buffer *paudio_buffer;
	u8 *p1, *p2;
	s16 tmp;
	int i, j, more, much, leap, rc;
#ifdef UPSAMPLE
	int k;
	s16 oldaudio, newaudio, delta;
#endif /*UPSAMPLE*/

	JOT(16, "\n");

	if (!purb) {
		SAY("ERROR: purb is NULL\n");
		return;
	}
	peasycap = purb->context;
	if (!peasycap) {
		SAY("ERROR: peasycap is NULL\n");
		return;
	}
	if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
		SAY("ERROR: bad peasycap\n");
		return;
	}
	much = 0;
	if (peasycap->audio_idle) {
		JOM(16, "%i=audio_idle  %i=audio_isoc_streaming\n",
		    peasycap->audio_idle, peasycap->audio_isoc_streaming);
		if (peasycap->audio_isoc_streaming) {
			rc = usb_submit_urb(purb, GFP_ATOMIC);
			if (rc) {
				if (-ENODEV != rc && -ENOENT != rc) {
					SAM("ERROR: while %i=audio_idle, "
					    "usb_submit_urb() failed with rc: -%s: %d\n",
					    peasycap->audio_idle,
					    strerror(rc), rc);
				}
			}
		}
		return;
	}
/*---------------------------------------------------------------------------*/
	if (purb->status) {
		if ((-ESHUTDOWN == purb->status) || (-ENOENT == purb->status)) {
			JOM(16, "urb status -ESHUTDOWN or -ENOENT\n");
			return;
		}
		SAM("ERROR: non-zero urb status: -%s: %d\n",
		    strerror(purb->status), purb->status);
		goto resubmit;
	}
/*---------------------------------------------------------------------------*/
/*
 *  PROCEED HERE WHEN NO ERROR
 */
/*---------------------------------------------------------------------------*/
#ifdef UPSAMPLE
	oldaudio = peasycap->oldaudio;
#endif /*UPSAMPLE*/

	for (i = 0;  i < purb->number_of_packets; i++) {
		if (!purb->iso_frame_desc[i].status) {

			SAM("-%s\n", strerror(purb->iso_frame_desc[i].status));

			more = purb->iso_frame_desc[i].actual_length;

			if (!more)
				peasycap->audio_mt++;
			else {
				if (peasycap->audio_mt) {
					JOM(12, "%4i empty audio urb frames\n",
					    peasycap->audio_mt);
					peasycap->audio_mt = 0;
				}

				p1 = (u8 *)(purb->transfer_buffer + purb->iso_frame_desc[i].offset);

				leap = 0;
				p1 += leap;
				more -= leap;
				/*
				 *  COPY more BYTES FROM ISOC BUFFER
				 *  TO AUDIO BUFFER, CONVERTING
				 *  8-BIT MONO TO 16-BIT SIGNED
				 *  LITTLE-ENDIAN SAMPLES IF NECESSARY
				 */
				while (more) {
					if (0 > more) {
						SAM("MISTAKE: more is negative\n");
						return;
					}
					if (peasycap->audio_buffer_page_many <= peasycap->audio_fill) {
						SAM("ERROR: bad peasycap->audio_fill\n");
						return;
					}

					paudio_buffer = &peasycap->audio_buffer[peasycap->audio_fill];
					if (PAGE_SIZE < (paudio_buffer->pto - paudio_buffer->pgo)) {
						SAM("ERROR: bad paudio_buffer->pto\n");
						return;
					}
					if (PAGE_SIZE == (paudio_buffer->pto - paudio_buffer->pgo)) {

						paudio_buffer->pto = paudio_buffer->pgo;
						(peasycap->audio_fill)++;
						if (peasycap->audio_buffer_page_many <= peasycap->audio_fill)
							peasycap->audio_fill = 0;

						JOM(8, "bumped peasycap->"
						    "audio_fill to %i\n",
						    peasycap->audio_fill);

						paudio_buffer = &peasycap->audio_buffer[peasycap->audio_fill];
						paudio_buffer->pto = paudio_buffer->pgo;

						if (!(peasycap->audio_fill % peasycap->audio_pages_per_fragment)) {
							JOM(12, "wakeup call on wq_audio, %i=frag reading  %i=fragment fill\n",
							    (peasycap->audio_read / peasycap->audio_pages_per_fragment),
							    (peasycap->audio_fill / peasycap->audio_pages_per_fragment));
							wake_up_interruptible(&(peasycap->wq_audio));
						}
					}

					much = PAGE_SIZE - (int)(paudio_buffer->pto - paudio_buffer->pgo);

					if (!peasycap->microphone) {
						if (much > more)
							much = more;

						memcpy(paudio_buffer->pto, p1, much);
						p1 += much;
						more -= much;
					} else {
#ifdef UPSAMPLE
						if (much % 16)
							JOM(8, "MISTAKE? much"
							    " is not divisible by 16\n");
						if (much > (16 * more))
							much = 16 * more;
						p2 = (u8 *)paudio_buffer->pto;

						for (j = 0;  j < (much/16);  j++) {
							newaudio =  ((int) *p1) - 128;
							newaudio = 128 * newaudio;

							delta = (newaudio - oldaudio) / 4;
							tmp = oldaudio + delta;

							for (k = 0;  k < 4;  k++) {
								*p2 = (0x00FF & tmp);
								*(p2 + 1) = (0xFF00 & tmp) >> 8;
								p2 += 2;
								*p2 = (0x00FF & tmp);
								*(p2 + 1) = (0xFF00 & tmp) >> 8;
								p2 += 2;

								tmp += delta;
							}
							p1++;
							more--;
							oldaudio = tmp;
						}
#else /*!UPSAMPLE*/
						if (much > (2 * more))
							much = 2 * more;
						p2 = (u8 *)paudio_buffer->pto;

						for (j = 0;  j < (much / 2);  j++) {
							tmp =  ((int) *p1) - 128;
							tmp = 128 * tmp;
							*p2 = (0x00FF & tmp);
							*(p2 + 1) = (0xFF00 & tmp) >> 8;
							p1++;
							p2 += 2;
							more--;
						}
#endif /*UPSAMPLE*/
					}
					(paudio_buffer->pto) += much;
				}
			}
		} else {
			JOM(12, "discarding audio samples because "
			    "%i=purb->iso_frame_desc[i].status\n",
			    purb->iso_frame_desc[i].status);
		}

#ifdef UPSAMPLE
		peasycap->oldaudio = oldaudio;
#endif /*UPSAMPLE*/

	}
/*---------------------------------------------------------------------------*/
/*
 *  RESUBMIT THIS URB
 */
/*---------------------------------------------------------------------------*/
resubmit:
	if (peasycap->audio_isoc_streaming) {
		rc = usb_submit_urb(purb, GFP_ATOMIC);
		if (rc) {
			if (-ENODEV != rc && -ENOENT != rc) {
				SAM("ERROR: while %i=audio_idle, "
				    "usb_submit_urb() failed "
				    "with rc: -%s: %d\n", peasycap->audio_idle,
				    strerror(rc), rc);
			}
		}
	}
	return;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  THE AUDIO URBS ARE SUBMITTED AT THIS EARLY STAGE SO THAT IT IS POSSIBLE TO
 *  STREAM FROM /dev/easyoss1 WITH SIMPLE PROGRAMS SUCH AS cat WHICH DO NOT
 *  HAVE AN IOCTL INTERFACE.
 */
/*---------------------------------------------------------------------------*/
static int easyoss_open(struct inode *inode, struct file *file)
{
	struct usb_interface *pusb_interface;
	struct easycap *peasycap;
	int subminor;
	struct v4l2_device *pv4l2_device;

	JOT(4, "begins\n");

	subminor = iminor(inode);

	pusb_interface = usb_find_interface(&easycap_usb_driver, subminor);
	if (!pusb_interface) {
		SAY("ERROR: pusb_interface is NULL\n");
		SAY("ending unsuccessfully\n");
		return -1;
	}
	peasycap = usb_get_intfdata(pusb_interface);
	if (!peasycap) {
		SAY("ERROR: peasycap is NULL\n");
		SAY("ending unsuccessfully\n");
		return -1;
	}
/*---------------------------------------------------------------------------*/
/*
 *  SOME VERSIONS OF THE videodev MODULE OVERWRITE THE DATA WHICH HAS
 *  BEEN WRITTEN BY THE CALL TO usb_set_intfdata() IN easycap_usb_probe(),
 *  REPLACING IT WITH A POINTER TO THE EMBEDDED v4l2_device STRUCTURE.
 *  TO DETECT THIS, THE STRING IN THE easycap.telltale[] BUFFER IS CHECKED.
*/
/*---------------------------------------------------------------------------*/
	if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
		pv4l2_device = usb_get_intfdata(pusb_interface);
		if (!pv4l2_device) {
			SAY("ERROR: pv4l2_device is NULL\n");
			return -EFAULT;
		}
		peasycap = container_of(pv4l2_device,
				struct easycap, v4l2_device);
	}
/*---------------------------------------------------------------------------*/
	if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
		SAY("ERROR: bad peasycap: %p\n", peasycap);
		return -EFAULT;
	}
/*---------------------------------------------------------------------------*/

	file->private_data = peasycap;

	if (0 != easycap_sound_setup(peasycap)) {
		;
		;
	}
	return 0;
}
/*****************************************************************************/
static int easyoss_release(struct inode *inode, struct file *file)
{
	struct easycap *peasycap;

	JOT(4, "begins\n");

	peasycap = file->private_data;
	if (!peasycap) {
		SAY("ERROR:  peasycap is NULL.\n");
		return -EFAULT;
	}
	if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
		SAY("ERROR: bad peasycap: %p\n", peasycap);
		return -EFAULT;
	}
	if (0 != kill_audio_urbs(peasycap)) {
		SAM("ERROR: kill_audio_urbs() failed\n");
		return -EFAULT;
	}
	JOM(4, "ending successfully\n");
	return 0;
}
/*****************************************************************************/
static ssize_t easyoss_read(struct file *file, char __user *puserspacebuffer,
			    size_t kount, loff_t *poff)
{
	struct timeval timeval;
	long long int above, below, mean;
	struct signed_div_result sdr;
	unsigned char *p0;
	long int kount1, more, rc, l0, lm;
	int fragment, kd;
	struct easycap *peasycap;
	struct data_buffer *pdata_buffer;
	size_t szret;

/*---------------------------------------------------------------------------*/
/*
 *  DO A BLOCKING READ TO TRANSFER DATA TO USER SPACE.
 *
 ******************************************************************************
 *****  N.B.  IF THIS FUNCTION RETURNS 0, NOTHING IS SEEN IN USER SPACE. ******
 *****        THIS CONDITION SIGNIFIES END-OF-FILE.                      ******
 ******************************************************************************
 */
/*---------------------------------------------------------------------------*/

	JOT(8, "%5zd=kount  %5lld=*poff\n", kount, *poff);

	if (!file) {
		SAY("ERROR:  file is NULL\n");
		return -ERESTARTSYS;
	}
	peasycap = file->private_data;
	if (!peasycap) {
		SAY("ERROR in easyoss_read(): peasycap is NULL\n");
		return -EFAULT;
	}
	if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
		SAY("ERROR: bad peasycap: %p\n", peasycap);
		return -EFAULT;
	}
	if (!peasycap->pusb_device) {
		SAY("ERROR: peasycap->pusb_device is NULL\n");
		return -EFAULT;
	}
	kd = isdongle(peasycap);
	if (0 <= kd && DONGLE_MANY > kd) {
		if (mutex_lock_interruptible(&(easycapdc60_dongle[kd].mutex_audio))) {
			SAY("ERROR: "
			    "cannot lock dongle[%i].mutex_audio\n", kd);
			return -ERESTARTSYS;
		}
		JOM(4, "locked dongle[%i].mutex_audio\n", kd);
		/*
		 *  MEANWHILE, easycap_usb_disconnect()
		 *  MAY HAVE FREED POINTER peasycap,
		 *  IN WHICH CASE A REPEAT CALL TO isdongle() WILL FAIL.
		 *  IF NECESSARY, BAIL OUT.
		 */
		if (kd != isdongle(peasycap))
			return -ERESTARTSYS;
		if (!file) {
			SAY("ERROR:  file is NULL\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -ERESTARTSYS;
		}
		peasycap = file->private_data;
		if (!peasycap) {
			SAY("ERROR:  peasycap is NULL\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -ERESTARTSYS;
		}
		if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
			SAY("ERROR: bad peasycap: %p\n", peasycap);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -ERESTARTSYS;
		}
		if (!peasycap->pusb_device) {
			SAM("ERROR: peasycap->pusb_device is NULL\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -ERESTARTSYS;
		}
	} else {
		/*
		 *  IF easycap_usb_disconnect()
		 *  HAS ALREADY FREED POINTER peasycap BEFORE THE
		 *  ATTEMPT TO ACQUIRE THE SEMAPHORE,
		 *  isdongle() WILL HAVE FAILED.  BAIL OUT.
		 */
		return -ERESTARTSYS;
	}
/*---------------------------------------------------------------------------*/
	JOT(16, "%sBLOCKING kount=%zd, *poff=%lld\n",
		(file->f_flags & O_NONBLOCK) ? "NON" : "", kount, *poff);

	if ((0 > peasycap->audio_read) ||
	    (peasycap->audio_buffer_page_many <= peasycap->audio_read)) {
		SAM("ERROR: peasycap->audio_read out of range\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
		return -EFAULT;
	}
	pdata_buffer = &peasycap->audio_buffer[peasycap->audio_read];
	if (!pdata_buffer) {
		SAM("ERROR: pdata_buffer is NULL\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
		return -EFAULT;
	}
	JOM(12, "before wait, %i=frag read  %i=frag fill\n",
	    (peasycap->audio_read / peasycap->audio_pages_per_fragment),
	    (peasycap->audio_fill / peasycap->audio_pages_per_fragment));
	fragment = (peasycap->audio_read / peasycap->audio_pages_per_fragment);
	while ((fragment == (peasycap->audio_fill / peasycap->audio_pages_per_fragment)) ||
		(0 == (PAGE_SIZE - (pdata_buffer->pto - pdata_buffer->pgo)))) {
		if (file->f_flags & O_NONBLOCK) {
			JOM(16, "returning -EAGAIN as instructed\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EAGAIN;
		}
		rc = wait_event_interruptible(peasycap->wq_audio,
				(peasycap->audio_idle  || peasycap->audio_eof ||
				((fragment !=
					(peasycap->audio_fill / peasycap->audio_pages_per_fragment)) &&
				(0 < (PAGE_SIZE - (pdata_buffer->pto - pdata_buffer->pgo))))));
		if (rc) {
			SAM("aborted by signal\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -ERESTARTSYS;
		}
		if (peasycap->audio_eof) {
			JOM(8, "returning 0 because  %i=audio_eof\n",
			    peasycap->audio_eof);
			kill_audio_urbs(peasycap);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return 0;
		}
		if (peasycap->audio_idle) {
			JOM(16, "returning 0 because  %i=audio_idle\n",
			    peasycap->audio_idle);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return 0;
		}
		if (!peasycap->audio_isoc_streaming) {
			JOM(16, "returning 0 because audio urbs not streaming\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return 0;
		}
	}
	JOM(12, "after  wait, %i=frag read  %i=frag fill\n",
	    (peasycap->audio_read / peasycap->audio_pages_per_fragment),
	    (peasycap->audio_fill / peasycap->audio_pages_per_fragment));
	szret = (size_t)0;
	fragment = (peasycap->audio_read / peasycap->audio_pages_per_fragment);
	while (fragment == (peasycap->audio_read / peasycap->audio_pages_per_fragment)) {
		if (!pdata_buffer->pgo) {
			SAM("ERROR: pdata_buffer->pgo is NULL\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		if (!pdata_buffer->pto) {
			SAM("ERROR: pdata_buffer->pto is NULL\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		kount1 = PAGE_SIZE - (pdata_buffer->pto - pdata_buffer->pgo);
		if (0 > kount1) {
			SAM("MISTAKE: kount1 is negative\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -ERESTARTSYS;
		}
		if (!kount1) {
			peasycap->audio_read++;
			if (peasycap->audio_buffer_page_many <= peasycap->audio_read)
				peasycap->audio_read = 0;
			JOM(12, "bumped peasycap->audio_read to %i\n",
			    peasycap->audio_read);

			if (fragment != (peasycap->audio_read / peasycap->audio_pages_per_fragment))
				break;

			if ((0 > peasycap->audio_read) ||
			    (peasycap->audio_buffer_page_many <= peasycap->audio_read)) {
				SAM("ERROR: peasycap->audio_read out of range\n");
				mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
				return -EFAULT;
			}
			pdata_buffer = &peasycap->audio_buffer[peasycap->audio_read];
			if (!pdata_buffer) {
				SAM("ERROR: pdata_buffer is NULL\n");
				mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
				return -EFAULT;
			}
			if (!pdata_buffer->pgo) {
				SAM("ERROR: pdata_buffer->pgo is NULL\n");
				mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
				return -EFAULT;
			}
			if (!pdata_buffer->pto) {
				SAM("ERROR: pdata_buffer->pto is NULL\n");
				mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
				return -EFAULT;
			}
			kount1 = PAGE_SIZE - (pdata_buffer->pto - pdata_buffer->pgo);
		}
		JOM(12, "ready  to send %zd bytes\n", kount1);
		JOM(12, "still  to send %li bytes\n", (long int) kount);
		more = kount1;
		if (more > kount)
			more = kount;
		JOM(12, "agreed to send %li bytes from page %i\n",
		    more, peasycap->audio_read);
		if (!more)
			break;

		/*
		 *  ACCUMULATE DYNAMIC-RANGE INFORMATION
		 */
		p0 = (unsigned char *)pdata_buffer->pgo;
		l0 = 0;
		lm = more/2;
		while (l0 < lm) {
			SUMMER(p0, &peasycap->audio_sample,
				&peasycap->audio_niveau,
				&peasycap->audio_square);
			l0++;
			p0 += 2;
		}
		/*-----------------------------------------------------------*/
		rc = copy_to_user(puserspacebuffer, pdata_buffer->pto, more);
		if (rc) {
			SAM("ERROR: copy_to_user() returned %li\n", rc);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		*poff += (loff_t)more;
		szret += (size_t)more;
		pdata_buffer->pto += more;
		puserspacebuffer += more;
		kount -= (size_t)more;
	}
	JOM(12, "after  read, %i=frag read  %i=frag fill\n",
	    (peasycap->audio_read / peasycap->audio_pages_per_fragment),
	    (peasycap->audio_fill / peasycap->audio_pages_per_fragment));
	if (kount < 0) {
		SAM("MISTAKE:  %li=kount  %li=szret\n",
		    (long int)kount, (long int)szret);
	}
/*---------------------------------------------------------------------------*/
/*
 *  CALCULATE DYNAMIC RANGE FOR (VAPOURWARE) AUTOMATIC VOLUME CONTROL
 */
/*---------------------------------------------------------------------------*/
	if (peasycap->audio_sample) {
		below = peasycap->audio_sample;
		above = peasycap->audio_square;
		sdr = signed_div(above, below);
		above = sdr.quotient;
		mean = peasycap->audio_niveau;
		sdr = signed_div(mean, peasycap->audio_sample);

		JOM(8, "%8lli=mean  %8lli=meansquare after %lli samples, =>\n",
		    sdr.quotient, above, peasycap->audio_sample);

		sdr = signed_div(above, 32768);
		JOM(8, "audio dynamic range is roughly %lli\n", sdr.quotient);
	}
/*---------------------------------------------------------------------------*/
/*
 *  UPDATE THE AUDIO CLOCK
 */
/*---------------------------------------------------------------------------*/
	do_gettimeofday(&timeval);
	if (!peasycap->timeval1.tv_sec) {
		peasycap->audio_bytes = 0;
		peasycap->timeval3 = timeval;
		peasycap->timeval1 = peasycap->timeval3;
		sdr.quotient = 192000;
	} else {
		peasycap->audio_bytes += (long long int) szret;
		below = ((long long int)(1000000)) *
			((long long int)(timeval.tv_sec  - peasycap->timeval3.tv_sec)) +
			(long long int)(timeval.tv_usec - peasycap->timeval3.tv_usec);
		above = 1000000 * ((long long int) peasycap->audio_bytes);

		if (below)
			sdr = signed_div(above, below);
		else
			sdr.quotient = 192000;
	}
	JOM(8, "audio streaming at %lli bytes/second\n", sdr.quotient);
	peasycap->dnbydt = sdr.quotient;

	mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
	JOM(4, "unlocked easycapdc60_dongle[%i].mutex_audio\n", kd);
	JOM(8, "returning %li\n", (long int)szret);
	return szret;

}
/*---------------------------------------------------------------------------*/
static long easyoss_unlocked_ioctl(struct file *file,
				   unsigned int cmd, unsigned long arg)
{
	struct easycap *peasycap;
	struct usb_device *p;
	int kd;

	if (!file) {
		SAY("ERROR:  file is NULL\n");
		return -ERESTARTSYS;
	}
	peasycap = file->private_data;
	if (!peasycap) {
		SAY("ERROR:  peasycap is NULL.\n");
		return -EFAULT;
	}
	if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
		SAY("ERROR: bad peasycap\n");
		return -EFAULT;
	}
	p = peasycap->pusb_device;
	if (!p) {
		SAM("ERROR: peasycap->pusb_device is NULL\n");
		return -EFAULT;
	}
	kd = isdongle(peasycap);
	if (0 <= kd && DONGLE_MANY > kd) {
		if (mutex_lock_interruptible(&easycapdc60_dongle[kd].mutex_audio)) {
			SAY("ERROR: cannot lock "
			    "easycapdc60_dongle[%i].mutex_audio\n", kd);
			return -ERESTARTSYS;
		}
		JOM(4, "locked easycapdc60_dongle[%i].mutex_audio\n", kd);
		/*
		 *  MEANWHILE, easycap_usb_disconnect()
		 *  MAY HAVE FREED POINTER peasycap,
		 *  IN WHICH CASE A REPEAT CALL TO isdongle() WILL FAIL.
		 *  IF NECESSARY, BAIL OUT.
		*/
		if (kd != isdongle(peasycap))
			return -ERESTARTSYS;
		if (!file) {
			SAY("ERROR:  file is NULL\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -ERESTARTSYS;
		}
		peasycap = file->private_data;
		if (!peasycap) {
			SAY("ERROR:  peasycap is NULL\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -ERESTARTSYS;
		}
		if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
			SAY("ERROR: bad peasycap\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		p = peasycap->pusb_device;
		if (!peasycap->pusb_device) {
			SAM("ERROR: peasycap->pusb_device is NULL\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -ERESTARTSYS;
		}
	} else {
		/*
		 *  IF easycap_usb_disconnect()
		 *  HAS ALREADY FREED POINTER peasycap BEFORE THE
		 *  ATTEMPT TO ACQUIRE THE SEMAPHORE,
		 *  isdongle() WILL HAVE FAILED.  BAIL OUT.
		 */
		return -ERESTARTSYS;
	}
/*---------------------------------------------------------------------------*/
	switch (cmd) {
	case SNDCTL_DSP_GETCAPS: {
		int caps;
		JOM(8, "SNDCTL_DSP_GETCAPS\n");

#ifdef UPSAMPLE
		if (peasycap->microphone)
			caps = 0x04400000;
		else
			caps = 0x04400000;
#else
		if (peasycap->microphone)
			caps = 0x02400000;
		else
			caps = 0x04400000;
#endif /*UPSAMPLE*/

		if (copy_to_user((void __user *)arg, &caps, sizeof(int))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		break;
	}
	case SNDCTL_DSP_GETFMTS: {
		int incoming;
		JOM(8, "SNDCTL_DSP_GETFMTS\n");

#ifdef UPSAMPLE
		if (peasycap->microphone)
			incoming = AFMT_S16_LE;
		else
			incoming = AFMT_S16_LE;
#else
		if (peasycap->microphone)
			incoming = AFMT_S16_LE;
		else
			incoming = AFMT_S16_LE;
#endif /*UPSAMPLE*/

		if (copy_to_user((void __user *)arg, &incoming, sizeof(int))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		break;
	}
	case SNDCTL_DSP_SETFMT: {
		int incoming, outgoing;
		JOM(8, "SNDCTL_DSP_SETFMT\n");
		if (copy_from_user(&incoming, (void __user *)arg, sizeof(int))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		JOM(8, "........... %i=incoming\n", incoming);

#ifdef UPSAMPLE
		if (peasycap->microphone)
			outgoing = AFMT_S16_LE;
		else
			outgoing = AFMT_S16_LE;
#else
		if (peasycap->microphone)
			outgoing = AFMT_S16_LE;
		else
			outgoing = AFMT_S16_LE;
#endif /*UPSAMPLE*/

		if (incoming != outgoing) {
			JOM(8, "........... %i=outgoing\n", outgoing);
			JOM(8, "        cf. %i=AFMT_S16_LE\n", AFMT_S16_LE);
			JOM(8, "        cf. %i=AFMT_U8\n", AFMT_U8);
			if (copy_to_user((void __user *)arg, &outgoing, sizeof(int))) {
				mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
				return -EFAULT;
			}
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EINVAL ;
		}
		break;
	}
	case SNDCTL_DSP_STEREO: {
		int incoming;
		JOM(8, "SNDCTL_DSP_STEREO\n");
		if (copy_from_user(&incoming, (void __user *)arg, sizeof(int))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		JOM(8, "........... %i=incoming\n", incoming);

#ifdef UPSAMPLE
		if (peasycap->microphone)
			incoming = 1;
		else
			incoming = 1;
#else
		if (peasycap->microphone)
			incoming = 0;
		else
			incoming = 1;
#endif /*UPSAMPLE*/

		if (copy_to_user((void __user *)arg, &incoming, sizeof(int))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		break;
	}
	case SNDCTL_DSP_SPEED: {
		int incoming;
		JOM(8, "SNDCTL_DSP_SPEED\n");
		if (copy_from_user(&incoming, (void __user *)arg, sizeof(int))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		JOM(8, "........... %i=incoming\n", incoming);

#ifdef UPSAMPLE
		if (peasycap->microphone)
			incoming = 32000;
		else
			incoming = 48000;
#else
		if (peasycap->microphone)
			incoming = 8000;
		else
			incoming = 48000;
#endif /*UPSAMPLE*/

		if (copy_to_user((void __user *)arg, &incoming, sizeof(int))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		break;
	}
	case SNDCTL_DSP_GETTRIGGER: {
		int incoming;
		JOM(8, "SNDCTL_DSP_GETTRIGGER\n");
		if (copy_from_user(&incoming, (void __user *)arg, sizeof(int))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		JOM(8, "........... %i=incoming\n", incoming);

		incoming = PCM_ENABLE_INPUT;
		if (copy_to_user((void __user *)arg, &incoming, sizeof(int))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		break;
	}
	case SNDCTL_DSP_SETTRIGGER: {
		int incoming;
		JOM(8, "SNDCTL_DSP_SETTRIGGER\n");
		if (copy_from_user(&incoming, (void __user *)arg, sizeof(int))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		JOM(8, "........... %i=incoming\n", incoming);
		JOM(8, "........... cf 0x%x=PCM_ENABLE_INPUT "
		    "0x%x=PCM_ENABLE_OUTPUT\n",
		    PCM_ENABLE_INPUT, PCM_ENABLE_OUTPUT);
		;
		;
		;
		;
		break;
	}
	case SNDCTL_DSP_GETBLKSIZE: {
		int incoming;
		JOM(8, "SNDCTL_DSP_GETBLKSIZE\n");
		if (copy_from_user(&incoming, (void __user *)arg, sizeof(int))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		JOM(8, "........... %i=incoming\n", incoming);
		incoming = peasycap->audio_bytes_per_fragment;
		if (copy_to_user((void __user *)arg, &incoming, sizeof(int))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		break;
	}
	case SNDCTL_DSP_GETISPACE: {
		struct audio_buf_info audio_buf_info;

		JOM(8, "SNDCTL_DSP_GETISPACE\n");

		audio_buf_info.bytes      = peasycap->audio_bytes_per_fragment;
		audio_buf_info.fragments  = 1;
		audio_buf_info.fragsize   = 0;
		audio_buf_info.fragstotal = 0;

		if (copy_to_user((void __user *)arg, &audio_buf_info, sizeof(int))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		break;
	}
	case 0x00005401:
	case 0x00005402:
	case 0x00005403:
	case 0x00005404:
	case 0x00005405:
	case 0x00005406: {
		JOM(8, "SNDCTL_TMR_...: 0x%08X unsupported\n", cmd);
		mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
		return -ENOIOCTLCMD;
	}
	default: {
		JOM(8, "ERROR: unrecognized DSP IOCTL command: 0x%08X\n", cmd);
		mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
		return -ENOIOCTLCMD;
	}
	}
	mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
	return 0;
}
/*****************************************************************************/

static const struct file_operations easyoss_fops = {
	.owner		= THIS_MODULE,
	.open		= easyoss_open,
	.release	= easyoss_release,
	.unlocked_ioctl	= easyoss_unlocked_ioctl,
	.read		= easyoss_read,
	.llseek		= no_llseek,
};
struct usb_class_driver easyoss_class = {
	.name = "usb/easyoss%d",
	.fops = &easyoss_fops,
	.minor_base = USB_SKEL_MINOR_BASE,
};
/*****************************************************************************/
