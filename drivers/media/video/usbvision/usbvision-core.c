/*
 * USB USBVISION Video device driver 0.9.8.3cvs (For Kernel 2.4.19-2.4.32 + 2.6.0-2.6.16)
 *
 *
 *
 * Copyright (c) 1999-2005 Joerg Heckenbach <joerg@heckenbach-aw.de>
 *
 * This module is part of usbvision driver project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Let's call the version 0.... until compression decoding is completely
 * implemented.
 *
 * This driver is written by Jose Ignacio Gijon and Joerg Heckenbach.
 * It was based on USB CPiA driver written by Peter Pregler,
 * Scott J. Bertin and Johannes Erdfelt
 * Ideas are taken from bttv driver by Ralph Metzler, Marcus Metzler &
 * Gerd Knorr and zoran 36120/36125 driver by Pauline Middelink
 * Updates to driver completed by Dwaine P. Garden
 *
 * History:
 *
 * Mar. 2000 - 15.12.2000:  (0.0.0 - 0.2.0)
 *     Several alpha drivers and the first beta.
 *
 * Since Dec. 2000:  (0.2.1) or (v2.1)
 *     Code changes or updates by Dwaine Garden and every other person.
 *
 *     Added: New Hauppauge TV device  Vendor  ID: 0x0573
 *                                     Product ID: 0x4D01
 *            (Thanks to Giovanni Garberoglio)
 *
 *     Added: UK Hauppauge WinTV-USB   Vendor  ID: 0x0573
 *                                     Product ID: 0x4D02
 *            (Thanks to Derek Freeman-Jones)
 *
 * Feb, 2001 - Apr 08, 2001:  (0.3.0)
 *     - Some fixes. Driver is now more stable.
 *     - Scratch is organized as ring-buffer now for better performance
 *     - DGA (overlay) is now supported.
 *       !!!!Danger!!!! Clipping is not yet implemented. Your system will
 *       crash if your video window leaves the screen!!!
 *     - Max. Framesize is set to 320x240. There isn't more memory on the
 *       nt1003 video device for the FIFO.
 *     - Supported video palettes: RGB565, RGB555, RGB24, RGB32
 *
 *
 * Apr 15, 2001:  (0.3.1-test...)
 *     - Clipping is implemented
 *     - NTSC is now coloured (Thanks to Dwaine Garden)
 *     - Added SECAM colour detection in saa7111-new
 *     - Added: French Hauppauge WinTV USB  Vendor ID: 0x0573
 *                                          Product ID: 0x4D03
 *              (Thanks to Julius Hrivnac)
 *     - Added: US Hauppauge WINTV USB  Vendor ID: 0x0573
 *                                      Product ID: 0x4D00
 *              (Thanks to  Derrick J Brashear)
 *     - Changes for adding new devices. There's now a table in usbvision.h.
 *       Adding your devices data to the usbvision_device_data table is all
 *       you need to add a new device.
 *
 * May 11, 2001: (0.3.2-test...) (Thanks to Derek Freeman-Jones)
 *     - Support YUV422 raw format for people with hardware scaling.
 *     - Only power on the device when opened (use option PowerOnAtOpen=0 to disable it).
 *     - Turn off audio so we can listen to Line In.
 *
 * July 5, 2001 - (Patch the driver to run with Kernel 2.4.6)
 *     - Fixed a problem with the number of parameters passed to video_register_device.
 *
 * July 6, 2001 - Added: HAUPPAUGE WINTV-USB FM USA Vendor  ID: 0x0573
 *                                              	Product ID: 0x4D10
 *       (Thanks to Braddock Gaskill)
 *                Added: USBGear USBG-V1 resp. HAMA USB
 *                                      Vendor  ID: 0x0573
 *                                      Product ID: 0x0003
 *       (Thanks to Bradley A. Singletary and Juergen Weigert)
 *
 * Jan 24, 2002 - (0.3.3-test...)
 *     - Moved all global variables that are device specific the usb_usbvision struct
 *     - Fixed the 64x48 unchangable image in xawtv when starting it with overlay
 *     - Add VideoNorm and TunerType to the usb_device_data table
 *     - Checked the audio channels and mute for HAUPPAUGE WinTV USB FM
 *     - Implemented the power on when opening the device. But some software opens
 *       the device several times when starting. So the i2c parts are just registered
 *       by an open, when they become deregistered by the next close. You can speed
 *       up tuner detection, when adding "options tuner addr=your_addr" to /etc/modules.conf
 *     - Begin to resize the frame in width and height. So it will be possible to watch i.e.
 *       384x288 pixels at 23 fps.
 *
 * Feb 10, 2002
 *     - Added radio device
 *
 *
 * Jul 30, 2002 - (Thanks Cameron Maxwell)
 *     - Changes to usbvision.h --fixed usbvision device data structure, incorrectly had (0x0573, 0x4d21) for WinTV-USB II, should be 0x4d20.
 *     - Changes for device WinTV-USB II (0x0573. 0x4D21).  It does not have a FM tuner.
 *     - Added the real device HAUPPAUGE WINTV-USB II (PAL) to the device structure in usbvision.h.
 *     - Changes to saa7113-new, the video is 8 bit data for the Phillips SAA7113 not 16bit like SAA7111.
 *     - Tuned lots of setup registers for the Phillips SAA7113 video chipset.
 *     - Changes to the supplied makefile. (Dwaine Garden) Still needs to be fixed so it will compile modules on different distrubutions.
 *
 *
 * Aug 10, 2002 - (Thanks Mike Klinke)
 *     - Changes to usbvision.txt -- Fixed instructions on the location to copy the contents of the tgz file.
 *     - Added device WinTV-USB FM Model 621 (0x0573. 0x4D30). There is another device which carries the same name. Kept that device in the device structure.
 *
 * Aug 12, 2002 - Dwaine Garden
 *     - Added the ability to read the NT100x chip for the MaxISOPacketLength and USB Bandwidth
 *       Setting of the video device.
 *     - Adjustments to the SAA7113H code for proper video output.
 *     - Changes to usbvision.h, so all the devices with FM tuners are working.
 *
 * Feb 10, 2003 - Joerg Heckenbach
 *     - fixed endian bug for Motorola PPC
 *
 * Feb 13, 2003 - Joerg Heckenbach
 *     - fixed Vin_Reg setting and presetting from usbvision_device_data()
 *
 * Apr 19, 2003 - Dwaine Garden
 *     - Fixed compiling errors under RedHat v9.0. from uvirt_to_kva and usbvision_mmap. (Thanks Cameron Maxwell)
 *     - Changed pte_offset to pte_offset_kernel.
 *     - Changed remap_page_range and added additional parameter to function.
 *     - Change setup parameters for the D-Link V100 USB device
 *     - Added a new device to the usbvision driver.  Pinnacle Studio PCTV USB (PAL) 0x2304 0x0110
 *     - Screwed up the sourceforge.net cvs respository!  8*)
 *
 * Apr 22, 2002 - Dwaine Garden
 *     - Added a new device to the usbvision driver. Dazzle DVC-80 (PAL) 0x07d0 0x0004. (Thanks Carl Anderson)
 *     - Changes to some of the comments.
 *
 * June 06, 2002 - Ivan, Dwaine Garden
 *     - Ivan updates for fine tuning device parameters without driver recompiling. (Ivan)
 *     - Changes to some of the comments. (Dwaine Garden)
 *     - Changes to the makefile - Better CPU settings. (Ivan)
 *     - Changes to device Hauppauge WinTv-USB III (PAL) FM Model 568 - Fine tuning parameters (Ivan)
 *
 *
 * Oct 16, 2003 - 0.9.0 - Joerg Heckenbach
 *     - Implementation of the first part of the decompression algorithm for intra frames.
 *       The resolution has to be 320x240. A dynamic adaption of compression deepth is
 *       missing yet.
 *
 * Oct 22, 2003 - 0.9.1 - Joerg Heckenbach
 *     - Implementation of the decompression algorithm for inter frames.
 *       The resolution still has to be 320x240.
 *
 * Nov 2003 - Feb 2004 - 0.9.2 to 0.9.3 - Joerg Heckenbach
 *     - Implement last unknown compressed block type. But color is still noisy.
 *     - Finding criteria for adaptive compression adjustment.
 *     - Porting to 2.6 kernels, but still working under 2.4
 *
 * Feb 04, 2004 - 0.9.4 Joerg Heckenbach
 *     - Found bug in color decompression. Color is OK now.
 *
 * Feb 09, 2004 - 0.9.5 Joerg Heckenbach
 *     - Add auto-recognition of chip type NT1003 or NT1004.
 *     - Add adaptive compression adjustment.
 *     - Patched saa7113 multiplexer switching (Thanks to Orlando F.S. Bordoni)
 *
 * Feb 24, 2004 - 0.9.6 Joerg Heckenbach
 *     - Add a timer to wait before poweroff at close, to save start time in
 *       some video applications
 *
 * Mar 4, 2004 - 0.9.6 Dwaine Garden
 *     - Added device Global Village GV-007 (NTSC) to usbvision.h (Thanks to Abe Skolnik)
 *     - Forgot to add this device to the driver. 8*)
 *
 * June 2, 2004 - 0.9.6 Dwaine Garden
 *     - Fixed sourceforge.net cvs repository.
 *     - Added #if LINUX_VERSION_CODE > KERNEL_VERSION(2,4,26) for .owner to help compiling under kernels 2.4.x which do not have the i2c v2.8.x updates.
 *     - Added device Hauppauge WinTv-USB III (PAL) FM Model 597 to usbvision.h
 *
 * July 1, 2004 -0.9.6 Dwaine Garden
 *     - Patch was submitted by Hal Finkel to fix the problem with the tuner not working under kernel 2.6.7.
 *     - Thanks Hal.....
 *
 * July 30, 2004 - 0.9.6 Dwaine Garden
 *     - Patch was submitted by Tobias Diaz to fix Model ID mismatch in usbvision.h.
 *     - Thanks.....
 *
 * August 12, 2004 - 0.9.6 Dwaine Garden
 *     - Updated the readme file so people could install the driver under the configuration file for kernel 2.6.x recompiles.  Now people can use make xconfig!
 *     - Added new device "Camtel Technology Corp TVB330-USB FM".
 *     - Sourceforge.net CVS has been updated with all the changes.
 *
 * August 20, 2004 - 0.9.7 Dwaine Garden
 *     - Added Device "Hauppauge USB Live Model 600"
 *     - Fixed up all the devices which did not have a default tuner type in usbvision.h.  It's best guess, at least until someone with the device tells me otherwise.
 *     - Sourceforge.net CVS has been updated with all the changes.
 *     - Clean up the driver.
 *
 * September 13, 2004 - 0.9.8 Dwaine Garden
 *     - Changed usbvision_muxsel to address the problem with black & white s-video output for NT1004 devices with saa7114 video decoder.  Thanks to Emmanuel for the patch and testing.
 *     - Fixed up SECAM devices which could not properly output video.  Changes to usbmuxsel.  Thanks to Emmanuel for the patch and everyone with a SECAM device which help test.
 *     - Removed some commented out code.  Clean up.
 *     - Tried to fix up the annoying empty directories in the sourceforge.net cvs.   Fuck it up again.  8*(
 *
 * November 15, 2004 - 0.9.8 Dwaine Garden
 *     - Release new tar - 0.9.8 on sourceforge.net
 *     - Added some new devices to usbvision.h WinTV USB Model 602 40201 Rev B282, Hauppague WinTV USB Model 602 40201 Rev B285
 *     - Added better compatibility for 2.6.x kernels.
 *     - Hardware full screen scaling in grabdisplay mode.
 *     - Better support for sysfs.  More code to follow for both video device and radio device.	Device information is located at /sys/class/video4linux/video0
 *     - Added module_param so loaded module parameters are displayed in sysfs.  Driver parameters should show up in /sys/module/usbvision
 *     - Adjusted the SAA7111 registers to match the 2.6.x kernel SAA7111 code. Thanks to the person which helped test.
 *     - Changed to wait_event_interruptible. For all the people running Fedora 2.
 *     - Added some screenshots of actual video captures on sourceforge.net.
 *
 * November 24, 2004 - 0.9.8.1cvs Dwaine Garden
 *     - Added patch to check for palette and format in VIDIOCSPICT.  Helix Producer should work fine with the driver now.  Thanks Jason Simpson
 *     - Two device description changes and two additions for the maintainer of usb.ids.
 *
 * December 2, 2004 - 0.9.8.1cvs Dwaine Garden
 *     - Added patch for YUV420P and YUV422P video output.  Thanks to Alex Smith.
 *     - Better support for mythtv.
 *
 * January 2, 2005 - 0.9.8.1cvs Dwaine Garden
 *     - Setup that you can specify which device is used for video.  Default is auto detect next available device number eg.  /dev/videoX
 *     - Setup that you can specify which device is used for radio.  Default is auto detect next available device number eg.  /dev/radioX
 *     - usb_unlink_urb() is deprecated for synchronous unlinks.  Using usb_kill_urb instead.
 *     - usbvision_kvirt_to_pa is deprecated.  Removed.
 *     - Changes are related to kernel changes for 2.6.10. (Fedora 4)
 *
 * February 2, 2005 - 0.9.8.1cvs Dwaine Garden
 *     - Added a new device to usbvision.h Dazzle DVC 50.  Thanks to Luiz S.
 *
 * March 29, 2005 - 0.9.8.1cvs Dwaine Garden
 *     - Fixed compile error with saa7113 under kernels 2.6.11+
 *     - Added module parameter to help people with Black and White output with using s-video input.  Some cables and input device are wired differently.
 *     - Removed the .id from the i2c usbvision template.  There was a change to the i2c with kernels 2.6.11+.
 *
 * April 9, 2005 - 0.9.8.1cvs Dwaine Garden
 *     - Added in the 2.4 and 2.6 readme files the SwitchSVideoInput parameter information.  This will help people setup the right values for the parameter.
 *       If your device experiences Black and White images with the S-Video Input.  Set this parameter to 1 when loading the module.
 *     - Replaced the wrong 2.6 readme file.  I lost the right version.  Someone sent me the right version by e-mail.  Thanks.
 *     - Released new module version on sourceforge.net.  So everyone can enjoy all the fixes and additional device support.
 *
 * April 20, 2005 - 0.9.8.2cvs Dwaine Garden
 *     - Release lock in usbvision_v4l_read_done.  -Thanks to nplanel for the patch.
 *     - Additional comments to the driver.
 *     - Fixed some spelling mistakes.  8*)
 *
 * April 23, 2005 - 0.9.8.2cvs Joerg Heckenbach
 *     - Found bug in usbvision line counting. Now there should be no spurious lines in the image any longer.
 *     - Swapped usbvision_register_video and usbvision_configure_video to fix problem with PowerOnAtOpen=0.
 *       Thanks to Erwan Velu
 *
 * April 26, 2005 - 0.9.8.2cvs Joerg Heckenbach
 *     - Fixed problem with rmmod module and oppses. Replaced vfree(usbvision->overlay_base) with iounmap(usbvision->overlay_base).
 *     - Added function usb_get_dev(dev) and ; To help with unloading the module multiple times without crashing.
 *       (Keep the reference count in kobjects correct)
 *
 * June 14, 2005 - 0.9.8.2cvs Dwaine
 *     - Missed a change in saa7113.c for checking kernel version.  Added conditional if's.
 *
 * June 15, 2005 - 0.9.8.2cvs Dwaine
 *     - Added new device WinTV device VendorId 0573 and ProductId 4d29.
 *     - Hacked some support for newer NT1005 devices.  This devices only seem to have one configuration, not multiple configurations like the NT1004.
 *
 * June 29, 2005 - 0.9.8.2cvs Dwaine
 *     - Added new device WinTV device VendorId 0573 and ProductId 4d37.
 *     - Because of the first empty entry in usbvision_table, modutils failed to create the necessary entries for modules.usbmap.
 *       This means hotplug won't work for usbvision.  Thanks to Gary Ng.
 *     - Sent an e-mail to the maintainer of usb.ids.  New devices identified need to be added.
 *     - Fixed compile error with saa7113 under kernel 2.6.12.
 *
 * July 6, 2005 - 0.9.8.2cvs Dwaine
 *     - Patch submitted by Gary Ng for two additional procfs entries.  Device Input and Frequency setting.
 *
 * July 12, 2005 - 0.9.8.2cvs Dwaine
 *     - New tuner identified for some devices it's called TCL_MFPE05.  This tuner uses the same API as tuner 38 in tuner.c.
 *     - Thanks to lynx31 for contacting Hauppage and asking them.
 *     - I have no clue as to which devices use this new tuner, so people will have to contact me and tell me.
 *
 * July 21, 2005 - 0.9.8.2cvs Dwaine
 *     - Patched usbvision.c with missing ifdef kernversion statement so the module will compile with older kernels and v4l.
 *     - Thanks to cipe007......
 *
 * May 19, 2006 - 0.9.8.3cvs Dwaine
 *     - Patched usbvision.c and i2c-algo.c so they will compile with kernel 2.6.16
 *     - Adjust device "Pinnacle Studio PCTV USB (PAL) FM" values in usbvision.h
 *
 * May 24, 2006 - 0.9.8.3cvs Dwaine
 *     -Pinnacle Studio PCTV USB (NTSC) FM uses saa7111, not saa7113 like first thought.
 *     -Updated usbvision.h
 *
 * Aug 15, 2006 - 0.9.8.3cvs Dwaine
 *     -Added saa711x module into cvs, since the newer saa7115 module in newer kernels is v4l2.  The usbvision driver is only v4l.
 *     -Updated makefile to put compiled modules into correct location.
 *
 * Aug 21, 2006 - 0.9.8.3cvs Dwaine
 *     -Changed number of bytes for i2c write to 4 as per the NT100X spec sheet.  Thanks to Merlum for finding it.
 *     -Remove the radio option for device Hauppauge WinTV USB device Model 40219 Rev E189.  This device does not have a FM radio.  Thanks to Shadwell.
 *     -Added radio option for device Hauppauge WinTV USB device Model 40219 Rev E189 again.  Just got an e-mail indicating their device has one.  8*)
 *
 * Aug 27, 2006 - 0.9.8.3cvs Dwaine
 *     -Changed ifdef statement so the usbvision driver will compile with kernels at 2.6.12.
 *     -Updated readme files for new updated tuner list for v4l devices.
 *
 *
 *
 * TODO:
 *     - use submit_urb for all setup packets
 *     - Fix memory settings for nt1004. It is 4 times as big as the
 *       nt1003 memory.
 *     - Add audio on endpoint 3 for nt1004 chip.  Seems impossible, needs a codec interface.  Which one?
 *     - Clean up the driver.
 *     - optimization for performance.
 *     - Add Videotext capability (VBI).  Working on it.....
 *     - Check audio for other devices
 *     - Add v4l2 interface
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/utsname.h>
#include <linux/highmem.h>
#include <linux/smp_lock.h>
#include <linux/videodev.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include <linux/videodev2.h>
#include <linux/video_decoder.h>
#include <linux/i2c.h>

#define USBVISION_DRIVER_VERSION_MAJOR 0
#define USBVISION_DRIVER_VERSION_MINOR 8
#define USBVISION_DRIVER_VERSION_PATCHLEVEL 0

#define USBVISION_VERSION __stringify(USBVISION_DRIVER_VERSION_MAJOR) "." __stringify(USBVISION_DRIVER_VERSION_MINOR) "." __stringify(USBVISION_DRIVER_VERSION_PATCHLEVEL) " " USBVISION_DRIVER_VERSION_COMMENT
#define USBVISION_DRIVER_VERSION KERNEL_VERSION(USBVISION_DRIVER_VERSION_MAJOR,USBVISION_DRIVER_VERSION_MINOR,USBVISION_DRIVER_VERSION_PATCHLEVEL)

#include <media/saa7115.h>
#include <media/v4l2-common.h>
#include <media/tuner.h>
#include <media/audiochip.h>

	#include <linux/moduleparam.h>
	#include <linux/workqueue.h>

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

#include "usbvision.h"

#define DRIVER_VERSION "0.9.8.3cvs for Linux kernels 2.4.19-2.4.32 + 2.6.0-2.6.17, compiled at "__DATE__", "__TIME__
#define EMAIL "joerg@heckenbach-aw.de"
#define DRIVER_AUTHOR "Joerg Heckenbach <joerg@heckenbach-aw.de>, Dwaine Garden <DwaineGarden@rogers.com>"
#define DRIVER_DESC "USBVision USB Video Device Driver for Linux"
#define DRIVER_LICENSE "GPL"
#define DRIVER_ALIAS "USBVision"

#define	ENABLE_HEXDUMP	0	/* Enable if you need it */


#define USBVISION_DEBUG		/* Turn on debug messages */

#ifdef USBVISION_DEBUG
	#define PDEBUG(level, fmt, args...) \
		if (debug & (level)) info("[%s:%d] " fmt, __PRETTY_FUNCTION__, __LINE__ , ## args)
#else
	#define PDEBUG(level, fmt, args...) do {} while(0)
#endif

#define DBG_IOCTL	1<<3
#define DBG_IO		1<<4
#define DBG_RIO		1<<5
#define DBG_HEADER	1<<7
#define DBG_PROBE	1<<8
#define DBG_IRQ		1<<9
#define DBG_ISOC	1<<10
#define DBG_PARSE	1<<11
#define DBG_SCRATCH	1<<12
#define DBG_FUNC	1<<13
#define DBG_I2C		1<<14

#define DEBUG(x...) 								/* General Debug */
#define IODEBUG(x...)								/* Debug IO */
#define OVDEBUG(x...) 								/* Debug overlay */
#define MDEBUG(x...)								/* Debug memory management */

//String operations
#define rmspace(str)	while(*str==' ') str++;
#define goto2next(str)	while(*str!=' ') str++; while(*str==' ') str++;


static int usbvision_nr = 0;			// sequential number of usbvision device
static unsigned long usbvision_timestamp = 0;		// timestamp in jiffies of a hundred frame
static unsigned long usbvision_counter = 0;		// frame counter

static const int max_imgwidth = MAX_FRAME_WIDTH;
static const int max_imgheight = MAX_FRAME_HEIGHT;
static const int min_imgwidth = MIN_FRAME_WIDTH;
static const int min_imgheight = MIN_FRAME_HEIGHT;

#define FRAMERATE_MIN	0
#define FRAMERATE_MAX	31


enum {
	ISOC_MODE_YUV422 = 0x03,
	ISOC_MODE_YUV420 = 0x14,
	ISOC_MODE_COMPRESS = 0x60,
};

static struct usbvision_v4l2_format_st usbvision_v4l2_format[] = {
	{ 1, 1,  8, V4L2_PIX_FMT_GREY    , "GREY" },
	{ 1, 2, 16, V4L2_PIX_FMT_RGB565  , "RGB565" },
	{ 1, 3, 24, V4L2_PIX_FMT_RGB24   , "RGB24" },
	{ 1, 4, 32, V4L2_PIX_FMT_RGB32   , "RGB32" },
	{ 1, 2, 16, V4L2_PIX_FMT_RGB555  , "RGB555" },
	{ 1, 2, 16, V4L2_PIX_FMT_YUYV    , "YUV422" },
	{ 1, 2, 12, V4L2_PIX_FMT_YVU420  , "YUV420P" }, // 1.5 !
	{ 1, 2, 16, V4L2_PIX_FMT_YUV422P , "YUV422P" }
};

/* supported tv norms */
static struct usbvision_tvnorm tvnorms[] = {
	{
		.name = "PAL",
		.id = V4L2_STD_PAL,
	}, {
		.name = "NTSC",
		.id = V4L2_STD_NTSC,
	}, {
		 .name = "SECAM",
		 .id = V4L2_STD_SECAM,
	}, {
		.name = "PAL-M",
		.id = V4L2_STD_PAL_M,
	}
};

#define TVNORMS ARRAY_SIZE(tvnorms)


/*
 * The value of 'scratch_buf_size' affects quality of the picture
 * in many ways. Shorter buffers may cause loss of data when client
 * is too slow. Larger buffers are memory-consuming and take longer
 * to work with. This setting can be adjusted, but the default value
 * should be OK for most desktop users.
 */
#define DEFAULT_SCRATCH_BUF_SIZE	(0x20000)		// 128kB memory scratch buffer
static const int scratch_buf_size = DEFAULT_SCRATCH_BUF_SIZE;

// Function prototypes
static int usbvision_restart_isoc(struct usb_usbvision *usbvision);
static int usbvision_begin_streaming(struct usb_usbvision *usbvision);
static int usbvision_muxsel(struct usb_usbvision *usbvision, int channel);
static int usbvision_i2c_write(void *data, unsigned char addr, char *buf, short len);
static int usbvision_i2c_read(void *data, unsigned char addr, char *buf, short len);
static int usbvision_read_reg(struct usb_usbvision *usbvision, unsigned char reg);
static int usbvision_write_reg(struct usb_usbvision *usbvision, unsigned char reg, unsigned char value);
static int usbvision_request_intra (struct usb_usbvision *usbvision);
static int usbvision_unrequest_intra (struct usb_usbvision *usbvision);
static int usbvision_adjust_compression (struct usb_usbvision *usbvision);
static int usbvision_measure_bandwidth (struct usb_usbvision *usbvision);
static void usbvision_release(struct usb_usbvision *usbvision);
static int usbvision_set_input(struct usb_usbvision *usbvision);
static int usbvision_set_output(struct usb_usbvision *usbvision, int width, int height);
static void usbvision_empty_framequeues(struct usb_usbvision *dev);
static int usbvision_stream_interrupt(struct usb_usbvision *dev);
static void call_i2c_clients(struct usb_usbvision *usbvision, unsigned int cmd, void *arg);


// Bit flags (options)
#define FLAGS_RETRY_VIDIOCSYNC		(1 << 0)
#define	FLAGS_MONOCHROME		(1 << 1)
#define FLAGS_DISPLAY_HINTS		(1 << 2)
#define FLAGS_OSD_STATS			(1 << 3)
#define FLAGS_FORCE_TESTPATTERN		(1 << 4)
#define FLAGS_SEPARATE_FRAMES		(1 << 5)
#define FLAGS_CLEAN_FRAMES		(1 << 6)

// Default initalization of device driver parameters
static int flags = 0;					// Set the default Overlay Display mode of the device driver
static int debug = 0;					// Set the default Debug Mode of the device driver
static int isocMode = ISOC_MODE_COMPRESS;		// Set the default format for ISOC endpoint
static int adjustCompression = 1;			// Set the compression to be adaptive
static int dga = 1;					// Set the default Direct Graphic Access
static int PowerOnAtOpen = 1;				// Set the default device to power on at startup
static int SwitchSVideoInput = 0;			// To help people with Black and White output with using s-video input.  Some cables and input device are wired differently.
static int video_nr = -1;				// Sequential Number of Video Device
static int radio_nr = -1;				// Sequential Number of Radio Device
static int vbi_nr = -1;					// Sequential Number of VBI Device
static char *CustomDevice=NULL;				// Set as nothing....

// Grab parameters for the device driver

#if defined(module_param)                               // Showing parameters under SYSFS
module_param(flags, int, 0444);
module_param(debug, int, 0444);
module_param(isocMode, int, 0444);
module_param(adjustCompression, int, 0444);
module_param(dga, int, 0444);
module_param(PowerOnAtOpen, int, 0444);
module_param(SwitchSVideoInput, int, 0444);
module_param(video_nr, int, 0444);
module_param(radio_nr, int, 0444);
module_param(vbi_nr, int, 0444);
module_param(CustomDevice, charp, 0444);
#else							// Old Style
MODULE_PARM(flags, "i");				// Grab the Overlay Display mode of the device driver
MODULE_PARM(debug, "i");				// Grab the Debug Mode of the device driver
MODULE_PARM(isocMode, "i");				// Grab the video format of the video device
MODULE_PARM(adjustCompression, "i");			// Grab the compression to be adaptive
MODULE_PARM(dga, "i");					// Grab the Direct Graphic Access
MODULE_PARM(PowerOnAtOpen, "i");			// Grab the device to power on at startup
MODULE_PARM(SwitchSVideoInput, "i");			// To help people with Black and White output with using s-video input.  Some cables and input device are wired differently.
MODULE_PARM(video_nr, "i");				// video_nr option allows to specify a certain /dev/videoX device (like /dev/video0 or /dev/video1 ...)
MODULE_PARM(radio_nr, "i");				// radio_nr option allows to specify a certain /dev/radioX device (like /dev/radio0 or /dev/radio1 ...)
MODULE_PARM(vbi_nr, "i");				// vbi_nr option allows to specify a certain /dev/vbiX device (like /dev/vbi0 or /dev/vbi1 ...)
MODULE_PARM(CustomDevice, "s");				// .... CustomDevice
#endif

MODULE_PARM_DESC(flags,	" Set the default Overlay Display mode of the device driver.  Default: 0 (Off)");
MODULE_PARM_DESC(debug, " Set the default Debug Mode of the device driver.  Default: 0 (Off)");
MODULE_PARM_DESC(isocMode, " Set the default format for ISOC endpoint.  Default: 0x60 (Compression On)");
MODULE_PARM_DESC(adjustCompression, " Set the ADPCM compression for the device.  Default: 1 (On)");
MODULE_PARM_DESC(dga, " Set the Direct Graphic Access for the device.  Default: 1 (On)");
MODULE_PARM_DESC(PowerOnAtOpen, " Set the default device to power on when device is opened.  Default: 1 (On)");
MODULE_PARM_DESC(SwitchSVideoInput, " Set the S-Video input.  Some cables and input device are wired differently. Default: 0 (Off)");
MODULE_PARM_DESC(video_nr, "Set video device number (/dev/videoX).  Default: -1 (autodetect)");
MODULE_PARM_DESC(radio_nr, "Set radio device number (/dev/radioX).  Default: -1 (autodetect)");
MODULE_PARM_DESC(vbi_nr, "Set vbi device number (/dev/vbiX).  Default: -1 (autodetect)");
MODULE_PARM_DESC(CustomDevice, " Define the fine tuning parameters for the device.  Default: null");


// Misc stuff
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);
	MODULE_VERSION(DRIVER_VERSION);
	MODULE_ALIAS(DRIVER_ALIAS);

#ifdef MODULE
static unsigned int autoload = 1;
#else
static unsigned int autoload = 0;
#endif


/****************************************************************************************/
/* SYSFS Code - Copied from the stv680.c usb module.					*/
/* Device information is located at /sys/class/video4linux/video0			*/
/* Device parameters information is located at /sys/module/usbvision                    */
/* Device USB Information is located at /sys/bus/usb/drivers/USBVision Video Grabber    */
/****************************************************************************************/


#define YES_NO(x) ((x) ? "Yes" : "No")

static inline struct usb_usbvision *cd_to_usbvision(struct class_device *cd)
{
	struct video_device *vdev = to_video_device(cd);
	return video_get_drvdata(vdev);
}

static ssize_t show_version(struct class_device *cd, char *buf)
{
	return sprintf(buf, "%s\n", DRIVER_VERSION);
}
static CLASS_DEVICE_ATTR(version, S_IRUGO, show_version, NULL);

static ssize_t show_model(struct class_device *class_dev, char *buf)
{
	struct video_device *vdev = to_video_device(class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	return sprintf(buf, "%s\n", usbvision_device_data[usbvision->DevModel].ModelString);
}
static CLASS_DEVICE_ATTR(model, S_IRUGO, show_model, NULL);

static ssize_t show_hue(struct class_device *class_dev, char *buf)
{
	struct video_device *vdev = to_video_device(class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	return sprintf(buf, "%d\n", usbvision->hue >> 8);
}
static CLASS_DEVICE_ATTR(hue, S_IRUGO, show_hue, NULL);

static ssize_t show_contrast(struct class_device *class_dev, char *buf)
{
	struct video_device *vdev = to_video_device(class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	return sprintf(buf, "%d\n", usbvision->contrast >> 8);
}
static CLASS_DEVICE_ATTR(contrast, S_IRUGO, show_contrast, NULL);

static ssize_t show_brightness(struct class_device *class_dev, char *buf)
{
	struct video_device *vdev = to_video_device(class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	return sprintf(buf, "%d\n", usbvision->brightness >> 8);
}
static CLASS_DEVICE_ATTR(brightness, S_IRUGO, show_brightness, NULL);

static ssize_t show_saturation(struct class_device *class_dev, char *buf)
{
	struct video_device *vdev = to_video_device(class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	return sprintf(buf, "%d\n", usbvision->saturation >> 8);
}
static CLASS_DEVICE_ATTR(saturation, S_IRUGO, show_saturation, NULL);

static ssize_t show_streaming(struct class_device *class_dev, char *buf)
{
	struct video_device *vdev = to_video_device(class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	return sprintf(buf, "%s\n", YES_NO(usbvision->streaming==Stream_On?1:0));
}
static CLASS_DEVICE_ATTR(streaming, S_IRUGO, show_streaming, NULL);

static ssize_t show_overlay(struct class_device *class_dev, char *buf)
{
	struct video_device *vdev = to_video_device(class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	return sprintf(buf, "%s\n", YES_NO(usbvision->overlay));
}
static CLASS_DEVICE_ATTR(overlay, S_IRUGO, show_overlay, NULL);

static ssize_t show_compression(struct class_device *class_dev, char *buf)
{
	struct video_device *vdev = to_video_device(class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	return sprintf(buf, "%s\n", YES_NO(usbvision->isocMode==ISOC_MODE_COMPRESS));
}
static CLASS_DEVICE_ATTR(compression, S_IRUGO, show_compression, NULL);

static ssize_t show_device_bridge(struct class_device *class_dev, char *buf)
{
	struct video_device *vdev = to_video_device(class_dev);
	struct usb_usbvision *usbvision = video_get_drvdata(vdev);
	return sprintf(buf, "%d\n", usbvision->bridgeType);
}
static CLASS_DEVICE_ATTR(bridge, S_IRUGO, show_device_bridge, NULL);

static void usbvision_create_sysfs(struct video_device *vdev)
{
	int res;
	if (vdev) {
		res=video_device_create_file(vdev, &class_device_attr_version);
		res=video_device_create_file(vdev, &class_device_attr_model);
		res=video_device_create_file(vdev, &class_device_attr_hue);
		res=video_device_create_file(vdev, &class_device_attr_contrast);
		res=video_device_create_file(vdev, &class_device_attr_brightness);
		res=video_device_create_file(vdev, &class_device_attr_saturation);
		res=video_device_create_file(vdev, &class_device_attr_streaming);
		res=video_device_create_file(vdev, &class_device_attr_overlay);
		res=video_device_create_file(vdev, &class_device_attr_compression);
		res=video_device_create_file(vdev, &class_device_attr_bridge);
	}
}

static void usbvision_remove_sysfs(struct video_device *vdev)
{
	if (vdev) {
		video_device_remove_file(vdev, &class_device_attr_version);
		video_device_remove_file(vdev, &class_device_attr_model);
		video_device_remove_file(vdev, &class_device_attr_hue);
		video_device_remove_file(vdev, &class_device_attr_contrast);
		video_device_remove_file(vdev, &class_device_attr_brightness);
		video_device_remove_file(vdev, &class_device_attr_saturation);
		video_device_remove_file(vdev, &class_device_attr_streaming);
		video_device_remove_file(vdev, &class_device_attr_overlay);
		video_device_remove_file(vdev, &class_device_attr_compression);
		video_device_remove_file(vdev, &class_device_attr_bridge);
	}
}


/*******************************/
/* Memory management functions */
/*******************************/

/*
 * Here we want the physical address of the memory.
 * This is used when initializing the contents of the area.
 */


void *usbvision_rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr;

	size = PAGE_ALIGN(size);
	mem = vmalloc_32(size);
	if (!mem)
		return NULL;

	memset(mem, 0, size); /* Clear the ram out, no junk to the user */
 adr = (unsigned long) mem;
 while (size > 0) {
  #if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
   mem_map_reserve(vmalloc_to_page((void *)adr));
  #else
   SetPageReserved(vmalloc_to_page((void *)adr));
  #endif
  adr += PAGE_SIZE;
  size -= PAGE_SIZE;
 }

 return mem;
}

void usbvision_rvfree(void *mem, unsigned long size)
{
 unsigned long adr;

 if (!mem)
  return;

 adr = (unsigned long) mem;
 while ((long) size > 0) {
  #if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
   mem_map_unreserve(vmalloc_to_page((void *)adr));
  #else
   ClearPageReserved(vmalloc_to_page((void *)adr));
  #endif
  adr += PAGE_SIZE;
  size -= PAGE_SIZE;
 }
 vfree(mem);
}






#if ENABLE_HEXDUMP
static void usbvision_hexdump(const unsigned char *data, int len)
{
 char tmp[80];
 int i, k;

 for (i = k = 0; len > 0; i++, len--) {
  if (i > 0 && (i % 16 == 0)) {
   printk("%s\n", tmp);
   k = 0;
  }
  k += sprintf(&tmp[k], "%02x ", data[i]);
 }
 if (k > 0)
  printk("%s\n", tmp);
}
#endif


/* These procedures handle the scratch ring buffer */
int scratch_len(struct usb_usbvision *usbvision)    /*This returns the amount of data actually in the buffer */
{
	int len = usbvision->scratch_write_ptr - usbvision->scratch_read_ptr;
	if (len < 0) {
		len += scratch_buf_size;
	}
	PDEBUG(DBG_SCRATCH, "scratch_len() = %d\n", len);

	return len;
}


/* This returns the free space left in the buffer */
int scratch_free(struct usb_usbvision *usbvision)
{
	int free = usbvision->scratch_read_ptr - usbvision->scratch_write_ptr;
	if (free <= 0) {
		free += scratch_buf_size;
	}
	if (free) {
		free -= 1;							/* at least one byte in the buffer must */
										/* left blank, otherwise there is no chance to differ between full and empty */
	}
	PDEBUG(DBG_SCRATCH, "return %d\n", free);

	return free;
}


void *debug_memcpy(void *dest, void *src, size_t len)
{
	printk(KERN_DEBUG "memcpy(%p, %p, %d);\n", dest, src, len);
	return memcpy(dest, src, len);
}


/* This puts data into the buffer */
int scratch_put(struct usb_usbvision *usbvision, unsigned char *data, int len)
{
	int len_part;

	if (usbvision->scratch_write_ptr + len < scratch_buf_size) {
		memcpy(usbvision->scratch + usbvision->scratch_write_ptr, data, len);
		usbvision->scratch_write_ptr += len;
	}
	else {
		len_part = scratch_buf_size - usbvision->scratch_write_ptr;
		memcpy(usbvision->scratch + usbvision->scratch_write_ptr, data, len_part);
		if (len == len_part) {
			usbvision->scratch_write_ptr = 0;			/* just set write_ptr to zero */
		}
		else {
			memcpy(usbvision->scratch, data + len_part, len - len_part);
			usbvision->scratch_write_ptr = len - len_part;
		}
	}

	PDEBUG(DBG_SCRATCH, "len=%d, new write_ptr=%d\n", len, usbvision->scratch_write_ptr);

	return len;
}

/* This marks the write_ptr as position of new frame header */
void scratch_mark_header(struct usb_usbvision *usbvision)
{
	PDEBUG(DBG_SCRATCH, "header at write_ptr=%d\n", usbvision->scratch_headermarker_write_ptr);

	usbvision->scratch_headermarker[usbvision->scratch_headermarker_write_ptr] =
				usbvision->scratch_write_ptr;
	usbvision->scratch_headermarker_write_ptr += 1;
	usbvision->scratch_headermarker_write_ptr %= USBVISION_NUM_HEADERMARKER;
}

/* This gets data from the buffer at the given "ptr" position */
int scratch_get_extra(struct usb_usbvision *usbvision, unsigned char *data, int *ptr, int len)
{
	int len_part;
	if (*ptr + len < scratch_buf_size) {
		memcpy(data, usbvision->scratch + *ptr, len);
		*ptr += len;
	}
	else {
		len_part = scratch_buf_size - *ptr;
		memcpy(data, usbvision->scratch + *ptr, len_part);
		if (len == len_part) {
			*ptr = 0;							/* just set the y_ptr to zero */
		}
		else {
			memcpy(data + len_part, usbvision->scratch, len - len_part);
			*ptr = len - len_part;
		}
	}

	PDEBUG(DBG_SCRATCH, "len=%d, new ptr=%d\n", len, *ptr);

	return len;
}


/* This sets the scratch extra read pointer */
void scratch_set_extra_ptr(struct usb_usbvision *usbvision, int *ptr, int len)
{
	*ptr = (usbvision->scratch_read_ptr + len)%scratch_buf_size;

	PDEBUG(DBG_SCRATCH, "ptr=%d\n", *ptr);
}


/*This increments the scratch extra read pointer */
void scratch_inc_extra_ptr(int *ptr, int len)
{
	*ptr = (*ptr + len) % scratch_buf_size;

	PDEBUG(DBG_SCRATCH, "ptr=%d\n", *ptr);
}


/* This gets data from the buffer */
int scratch_get(struct usb_usbvision *usbvision, unsigned char *data, int len)
{
	int len_part;
	if (usbvision->scratch_read_ptr + len < scratch_buf_size) {
		memcpy(data, usbvision->scratch + usbvision->scratch_read_ptr, len);
		usbvision->scratch_read_ptr += len;
	}
	else {
		len_part = scratch_buf_size - usbvision->scratch_read_ptr;
		memcpy(data, usbvision->scratch + usbvision->scratch_read_ptr, len_part);
		if (len == len_part) {
			usbvision->scratch_read_ptr = 0;				/* just set the read_ptr to zero */
		}
		else {
			memcpy(data + len_part, usbvision->scratch, len - len_part);
			usbvision->scratch_read_ptr = len - len_part;
		}
	}

	PDEBUG(DBG_SCRATCH, "len=%d, new read_ptr=%d\n", len, usbvision->scratch_read_ptr);

	return len;
}


/* This sets read pointer to next header and returns it */
int scratch_get_header(struct usb_usbvision *usbvision,struct usbvision_frame_header *header)
{
	int errCode = 0;

	PDEBUG(DBG_SCRATCH, "from read_ptr=%d", usbvision->scratch_headermarker_read_ptr);

	while (usbvision->scratch_headermarker_write_ptr -
		usbvision->scratch_headermarker_read_ptr != 0) {
		usbvision->scratch_read_ptr =
			usbvision->scratch_headermarker[usbvision->scratch_headermarker_read_ptr];
		usbvision->scratch_headermarker_read_ptr += 1;
		usbvision->scratch_headermarker_read_ptr %= USBVISION_NUM_HEADERMARKER;
		scratch_get(usbvision, (unsigned char *)header, USBVISION_HEADER_LENGTH);
		if ((header->magic_1 == USBVISION_MAGIC_1)
			 && (header->magic_2 == USBVISION_MAGIC_2)
			 && (header->headerLength == USBVISION_HEADER_LENGTH)) {
			errCode = USBVISION_HEADER_LENGTH;
			header->frameWidth  = header->frameWidthLo  + (header->frameWidthHi << 8);
			header->frameHeight = header->frameHeightLo + (header->frameHeightHi << 8);
			break;
		}
	}

	return errCode;
}


/*This removes len bytes of old data from the buffer */
void scratch_rm_old(struct usb_usbvision *usbvision, int len)
{

	usbvision->scratch_read_ptr += len;
	usbvision->scratch_read_ptr %= scratch_buf_size;
	PDEBUG(DBG_SCRATCH, "read_ptr is now %d\n", usbvision->scratch_read_ptr);
}


/*This resets the buffer - kills all data in it too */
void scratch_reset(struct usb_usbvision *usbvision)
{
	PDEBUG(DBG_SCRATCH, "\n");

	usbvision->scratch_read_ptr = 0;
	usbvision->scratch_write_ptr = 0;
	usbvision->scratch_headermarker_read_ptr = 0;
	usbvision->scratch_headermarker_write_ptr = 0;
	usbvision->isocstate = IsocState_NoFrame;
}



/* Here comes the OVERLAY stuff */

/* Tell the interrupt handler what to to.  */
static
void usbvision_cap(struct usb_usbvision* usbvision, int on)
{
	DEBUG(printk(KERN_DEBUG "usbvision_cap: overlay was %d, set it to %d\n", usbvision->overlay, on);)

	if (on) {
		usbvision->overlay = 1;
	}
	else {
		usbvision->overlay = 0;
	}
}




/* append a new clipregion to the vector of video_clips */
static
void usbvision_new_clip(struct v4l2_format* vf, struct v4l2_clip* vcp, int x, int y, int w, int h)
{
	vcp[vf->fmt.win.clipcount].c.left = x;
	vcp[vf->fmt.win.clipcount].c.top = y;
	vcp[vf->fmt.win.clipcount].c.width = w;
	vcp[vf->fmt.win.clipcount].c.height = h;
	vf->fmt.win.clipcount++;
}


#define mark_pixel(x,y)  usbvision->clipmask[((x) + (y) * MAX_FRAME_WIDTH)/32] |= 0x00000001<<((x)%32)
#define clipped_pixel(index) usbvision->clipmask[(index)/32] & (0x00000001<<((index)%32))

static
void usbvision_built_overlay(struct usb_usbvision* usbvision, int count, struct v4l2_clip *vcp)
{
	usbvision->overlay_win = usbvision->overlay_base +
		(signed int)usbvision->vid_win.fmt.win.w.left * usbvision->depth / 8 +
		(signed int)usbvision->vid_win.fmt.win.w.top * usbvision->vid_buf.fmt.bytesperline;

		IODEBUG(printk(KERN_DEBUG "built_overlay base=%p, win=%p, bpl=%d, clips=%d, size=%dx%d\n",
					usbvision->overlay_base, usbvision->overlay_win,
					usbvision->vid_buf.fmt.bytesperline, count,
					usbvision->vid_win.fmt.win.w.width, usbvision->vid_win.fmt.win.w.height);)


	/* Add here generation of clipping mask */
{
	int x_start, x_end, y_start, y_end;
	int clip_index, x, y;

	memset(usbvision->clipmask, 0, USBVISION_CLIPMASK_SIZE);

	OVDEBUG(printk(KERN_DEBUG "clips = %d\n", count);)

	for(clip_index = 0; clip_index < count; clip_index++) {
		OVDEBUG(printk(KERN_DEBUG "clip: %d,%d,%d,%d\n", vcp[clip_index].x,
				vcp[clip_index].y,
				vcp[clip_index].width,
				vcp[clip_index].height);)

		x_start = vcp[clip_index].c.left;
		if(x_start >= (int)usbvision->vid_win.fmt.win.w.width) {
			OVDEBUG(printk(KERN_DEBUG "x_start=%d\n", x_start);)
			continue; //clipping window is right of overlay window
		}
		x_end	= x_start + vcp[clip_index].c.width;
		if(x_end <= 0) {
			OVDEBUG(printk(KERN_DEBUG "x_end=%d\n", x_end);)
			continue; //clipping window is left of overlay window
		}

		y_start = vcp[clip_index].c.top;
		if(y_start >= (int)usbvision->vid_win.fmt.win.w.height) {
			OVDEBUG(printk(KERN_DEBUG "y_start=%d\n", y_start);)
			continue; //clipping window is below overlay window
		}
		y_end   = y_start + vcp[clip_index].c.height;
		if(y_end <= 0) {
			OVDEBUG(printk(KERN_DEBUG "y_end=%d\n", y_end);)
			continue; //clipping window is above overlay window
		}

		//clip the clipping window
		if (x_start < 0) {
			x_start = 0;
		}
		if (x_end > (int)usbvision->vid_win.fmt.win.w.width) {
			x_end = (int)usbvision->vid_win.fmt.win.w.width;
		}
		if (y_start < 0) {
			y_start = 0;
		}
		if (y_end > (int)usbvision->vid_win.fmt.win.w.height) {
			y_end = (int)usbvision->vid_win.fmt.win.w.height;
		}

		OVDEBUG(printk(KERN_DEBUG "clip_o: %d,%d,%d,%d\n", x_start,	y_start, x_end, y_end);)



		for(y = y_start; y < y_end; y++) {
			for(x = x_start; x < x_end; x++) {
				mark_pixel(x,y);
			}
		}
	}
}

}



void usbvision_osd_char(struct usb_usbvision *usbvision,
			struct usbvision_frame *frame, int x, int y, int ch)
{
	static const unsigned short digits[16] = {
		0xF6DE,		/* 0 */
		0x2492,		/* 1 */
		0xE7CE,		/* 2 */
		0xE79E,		/* 3 */
		0xB792,		/* 4 */
		0xF39E,		/* 5 */
		0xF3DE,		/* 6 */
		0xF492,		/* 7 */
		0xF7DE,		/* 8 */
		0xF79E,		/* 9 */
		0x77DA,		/* a */
		0xD75C,		/* b */
		0xF24E,		/* c */
		0xD6DC,		/* d */
		0xF34E,		/* e */
		0xF348		/* f */
	};
	unsigned short digit;
	int ix, iy;

	if ((usbvision == NULL) || (frame == NULL))
		return;

	if (ch >= '0' && ch <= '9')
		ch -= '0';
	else if (ch >= 'A' && ch <= 'F')
		ch = 10 + (ch - 'A');
	else if (ch >= 'a' && ch <= 'f')
		ch = 10 + (ch - 'a');
	else
		return;
	digit = digits[ch];

	for (iy = 0; iy < 5; iy++) {
		for (ix = 0; ix < 3; ix++) {
			if (digit & 0x8000) {
			//	USBVISION_PUTPIXEL(frame, x + ix, y + iy,
			//			0xFF, 0xFF, 0xFF);
			}
			digit = digit << 1;
		}
	}
}


void usbvision_osd_string(struct usb_usbvision *usbvision,
			  struct usbvision_frame *frame,
			  int x, int y, const char *str)
{
	while (*str) {
		usbvision_osd_char(usbvision, frame, x, y, *str);
		str++;
		x += 4;		/* 3 pixels character + 1 space */
	}
}

/*
 * usb_usbvision_osd_stats()
 *
 * On screen display of important debugging information.
 *
 */
void usbvision_osd_stats(struct usb_usbvision *usbvision,
			 struct usbvision_frame *frame)
{
	const int y_diff = 8;
	char tmp[16];
	int x = 10;
	int y = 10;

	sprintf(tmp, "%8x", usbvision->frame_num);
	usbvision_osd_string(usbvision, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", usbvision->isocUrbCount);
	usbvision_osd_string(usbvision, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", usbvision->urb_length);
	usbvision_osd_string(usbvision, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", usbvision->isocDataCount);
	usbvision_osd_string(usbvision, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", usbvision->header_count);
	usbvision_osd_string(usbvision, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", usbvision->scratch_ovf_count);
	usbvision_osd_string(usbvision, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", usbvision->isocSkipCount);
	usbvision_osd_string(usbvision, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", usbvision->isocErrCount);
	usbvision_osd_string(usbvision, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8x", usbvision->saturation);
	usbvision_osd_string(usbvision, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8x", usbvision->hue);
	usbvision_osd_string(usbvision, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8x", usbvision->brightness >> 8);
	usbvision_osd_string(usbvision, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8x", usbvision->contrast >> 12);
	usbvision_osd_string(usbvision, frame, x, y, tmp);
	y += y_diff;

}

/*
 * usbvision_testpattern()
 *
 * Procedure forms a test pattern (yellow grid on blue background).
 *
 * Parameters:
 * fullframe:   if TRUE then entire frame is filled, otherwise the procedure
 *		continues from the current scanline.
 * pmode	0: fill the frame with solid blue color (like on VCR or TV)
 *		1: Draw a colored grid
 *
 */
void usbvision_testpattern(struct usb_usbvision *usbvision, int fullframe,
			int pmode)
{
	static const char proc[] = "usbvision_testpattern";
	struct usbvision_frame *frame;
	unsigned char *f;
	int num_cell = 0;
	int scan_length = 0;
	static int num_pass = 0;

	if (usbvision == NULL) {
		printk(KERN_ERR "%s: usbvision == NULL\n", proc);
		return;
	}
	if (usbvision->curFrame == NULL) {
		printk(KERN_ERR "%s: usbvision->curFrame is NULL.\n", proc);
		return;
	}

	/* Grab the current frame */
	frame = usbvision->curFrame;

	/* Optionally start at the beginning */
	if (fullframe) {
		frame->curline = 0;
		frame->scanlength = 0;
	}

	/* Form every scan line */
	for (; frame->curline < frame->frmheight; frame->curline++) {
		int i;

		f = frame->data + (usbvision->curwidth * 3 * frame->curline);
		for (i = 0; i < usbvision->curwidth; i++) {
			unsigned char cb = 0x80;
			unsigned char cg = 0;
			unsigned char cr = 0;

			if (pmode == 1) {
				if (frame->curline % 32 == 0)
					cb = 0, cg = cr = 0xFF;
				else if (i % 32 == 0) {
					if (frame->curline % 32 == 1)
						num_cell++;
					cb = 0, cg = cr = 0xFF;
				} else {
					cb =
					    ((num_cell * 7) +
					     num_pass) & 0xFF;
					cg =
					    ((num_cell * 5) +
					     num_pass * 2) & 0xFF;
					cr =
					    ((num_cell * 3) +
					     num_pass * 3) & 0xFF;
				}
			} else {
				/* Just the blue screen */
			}

			*f++ = cb;
			*f++ = cg;
			*f++ = cr;
			scan_length += 3;
		}
	}

	frame->grabstate = FrameState_Done;
	frame->scanlength += scan_length;
	++num_pass;

	/* We do this unconditionally, regardless of FLAGS_OSD_STATS */
	usbvision_osd_stats(usbvision, frame);
}

/*
 * Here comes the data parsing stuff that is run as interrupt
 */

/*
 * usbvision_find_header()
 *
 * Locate one of supported header markers in the scratch buffer.
 */
static enum ParseState usbvision_find_header(struct usb_usbvision *usbvision)
{
	struct usbvision_frame *frame;
	int foundHeader = 0;

	if (usbvision->overlay) {
		frame = &usbvision->overlay_frame;
	}
	else {
		frame = usbvision->curFrame;
	}

	while (scratch_get_header(usbvision, &frame->isocHeader) == USBVISION_HEADER_LENGTH) {
		// found header in scratch
		PDEBUG(DBG_HEADER, "found header: 0x%02x%02x %d %d %d %d %#x 0x%02x %u %u",
				frame->isocHeader.magic_2,
				frame->isocHeader.magic_1,
				frame->isocHeader.headerLength,
				frame->isocHeader.frameNum,
				frame->isocHeader.framePhase,
				frame->isocHeader.frameLatency,
				frame->isocHeader.dataFormat,
				frame->isocHeader.formatParam,
				frame->isocHeader.frameWidth,
				frame->isocHeader.frameHeight);

		if (usbvision->requestIntra) {
			if (frame->isocHeader.formatParam & 0x80) {
				foundHeader = 1;
				usbvision->lastIsocFrameNum = -1; // do not check for lost frames this time
				usbvision_unrequest_intra(usbvision);
				break;
			}
		}
		else {
			foundHeader = 1;
			break;
		}
	}

	if (foundHeader) {
		frame->frmwidth = frame->isocHeader.frameWidth * usbvision->stretch_width;
		frame->frmheight = frame->isocHeader.frameHeight * usbvision->stretch_height;
		frame->v4l2_linesize = (frame->frmwidth * frame->v4l2_format.depth)>> 3;
	}
	else { // no header found
		PDEBUG(DBG_HEADER, "skipping scratch data, no header");
		scratch_reset(usbvision);
		return ParseState_EndParse;
	}

	// found header
	if (frame->isocHeader.dataFormat==ISOC_MODE_COMPRESS) {
		//check isocHeader.frameNum for lost frames
		if (usbvision->lastIsocFrameNum >= 0) {
			if (((usbvision->lastIsocFrameNum + 1) % 32) != frame->isocHeader.frameNum) {
				// unexpected frame drop: need to request new intra frame
				PDEBUG(DBG_HEADER, "Lost frame before %d on USB", frame->isocHeader.frameNum);
				usbvision_request_intra(usbvision);
				return ParseState_NextFrame;
			}
		}
		usbvision->lastIsocFrameNum = frame->isocHeader.frameNum;
	}
	usbvision->header_count++;
	frame->scanstate = ScanState_Lines;
	frame->curline = 0;

	if (flags & FLAGS_FORCE_TESTPATTERN) {
		usbvision_testpattern(usbvision, 1, 1);
		return ParseState_NextFrame;
	}
	return ParseState_Continue;
}

static enum ParseState usbvision_parse_lines_422(struct usb_usbvision *usbvision,
					   long *pcopylen)
{
	volatile struct usbvision_frame *frame;
	unsigned char *f;
	int len;
	int i;
	unsigned char yuyv[4]={180, 128, 10, 128}; // YUV components
	unsigned char rv, gv, bv;	// RGB components
	int clipmask_index, bytes_per_pixel;
	int overlay = usbvision->overlay;
	int stretch_bytes, clipmask_add;

	if (overlay) {
		frame  = &usbvision->overlay_frame;
		if (usbvision->overlay_base == NULL) {
			//video_buffer is not set yet
			return ParseState_NextFrame;
		}
		f = usbvision->overlay_win + frame->curline *
			usbvision->vid_buf.fmt.bytesperline;
	}
	else {
		frame  = usbvision->curFrame;
		f = frame->data + (frame->v4l2_linesize * frame->curline);
	}

	/* Make sure there's enough data for the entire line */
	len = (frame->isocHeader.frameWidth * 2)+5;
	if (scratch_len(usbvision) < len) {
		PDEBUG(DBG_PARSE, "out of data in line %d, need %u.\n", frame->curline, len);
		return ParseState_Out;
	}

	if ((frame->curline + 1) >= frame->frmheight) {
		return ParseState_NextFrame;
	}

	bytes_per_pixel = frame->v4l2_format.bytes_per_pixel;
	stretch_bytes = (usbvision->stretch_width - 1) * bytes_per_pixel;
	clipmask_index = frame->curline * MAX_FRAME_WIDTH;
	clipmask_add = usbvision->stretch_width;

	for (i = 0; i < frame->frmwidth; i+=(2 * usbvision->stretch_width)) {

		scratch_get(usbvision, &yuyv[0], 4);

		if((overlay) && (clipped_pixel(clipmask_index))) {
			f += bytes_per_pixel;
		}
		else if (frame->v4l2_format.format == V4L2_PIX_FMT_YUYV) {
			*f++ = yuyv[0]; // Y
			*f++ = yuyv[3]; // U
		}
		else {

			YUV_TO_RGB_BY_THE_BOOK(yuyv[0], yuyv[1], yuyv[3], rv, gv, bv);
			switch (frame->v4l2_format.format) {
				case V4L2_PIX_FMT_RGB565:
					*f++ = (0x1F & (bv >> 3)) | (0xE0 & (gv << 3));
					*f++ = (0x07 & (gv >> 5)) | (0xF8 &  rv);
					break;
				case V4L2_PIX_FMT_RGB24:
					*f++ = bv;
					*f++ = gv;
					*f++ = rv;
					break;
				case V4L2_PIX_FMT_RGB32:
					*f++ = bv;
					*f++ = gv;
					*f++ = rv;
					f++;
					break;
				case V4L2_PIX_FMT_RGB555:
					*f++ = (0x1F & (bv >> 3)) | (0xE0 & (gv << 2));
					*f++ = (0x03 & (gv >> 6)) | (0x7C & (rv >> 1));
					break;
			}
		}
		clipmask_index += clipmask_add;
		f += stretch_bytes;

		if((overlay) && (clipped_pixel(clipmask_index))) {
			f += bytes_per_pixel;
		}
		else if (frame->v4l2_format.format == V4L2_PIX_FMT_YUYV) {
			*f++ = yuyv[2]; // Y
			*f++ = yuyv[1]; // V
		}
		else {

			YUV_TO_RGB_BY_THE_BOOK(yuyv[2], yuyv[1], yuyv[3], rv, gv, bv);
			switch (frame->v4l2_format.format) {
				case V4L2_PIX_FMT_RGB565:
					*f++ = (0x1F & (bv >> 3)) | (0xE0 & (gv << 3));
					*f++ = (0x07 & (gv >> 5)) | (0xF8 &  rv);
					break;
				case V4L2_PIX_FMT_RGB24:
					*f++ = bv;
					*f++ = gv;
					*f++ = rv;
					break;
				case V4L2_PIX_FMT_RGB32:
					*f++ = bv;
					*f++ = gv;
					*f++ = rv;
					f++;
					break;
				case V4L2_PIX_FMT_RGB555:
					*f++ = (0x1F & (bv >> 3)) | (0xE0 & (gv << 2));
					*f++ = (0x03 & (gv >> 6)) | (0x7C & (rv >> 1));
					break;
			}
		}
		clipmask_index += clipmask_add;
		f += stretch_bytes;
	}

	frame->curline += usbvision->stretch_height;
	*pcopylen += frame->v4l2_linesize * usbvision->stretch_height;

	if (frame->curline >= frame->frmheight) {
		return ParseState_NextFrame;
	}
	else {
		return ParseState_Continue;
	}
}


static int usbvision_decompress(struct usb_usbvision *usbvision,unsigned char *Compressed,
								unsigned char *Decompressed, int *StartPos,
								int *BlockTypeStartPos, int Len)
{
	int RestPixel, Idx, MaxPos, Pos, ExtraPos, BlockLen, BlockTypePos, BlockTypeLen;
	unsigned char BlockByte, BlockCode, BlockType, BlockTypeByte, Integrator;

	Integrator = 0;
	Pos = *StartPos;
	BlockTypePos = *BlockTypeStartPos;
	MaxPos = 396; //Pos + Len;
	ExtraPos = Pos;
	BlockLen = 0;
	BlockByte = 0;
	BlockCode = 0;
	BlockType = 0;
	BlockTypeByte = 0;
	BlockTypeLen = 0;
	RestPixel = Len;

	for (Idx = 0; Idx < Len; Idx++) {

		if (BlockLen == 0) {
			if (BlockTypeLen==0) {
				BlockTypeByte = Compressed[BlockTypePos];
				BlockTypePos++;
				BlockTypeLen = 4;
			}
			BlockType = (BlockTypeByte & 0xC0) >> 6;

			//statistic:
			usbvision->ComprBlockTypes[BlockType]++;

			Pos = ExtraPos;
			if (BlockType == 0) {
				if(RestPixel >= 24) {
					Idx += 23;
					RestPixel -= 24;
					Integrator = Decompressed[Idx];
				} else {
					Idx += RestPixel - 1;
					RestPixel = 0;
				}
			} else {
				BlockCode = Compressed[Pos];
				Pos++;
				if (RestPixel >= 24) {
					BlockLen  = 24;
				} else {
					BlockLen = RestPixel;
				}
				RestPixel -= BlockLen;
				ExtraPos = Pos + (BlockLen / 4);
			}
			BlockTypeByte <<= 2;
			BlockTypeLen -= 1;
		}
		if (BlockLen > 0) {
			if ((BlockLen%4) == 0) {
				BlockByte = Compressed[Pos];
				Pos++;
			}
			if (BlockType == 1) { //inter Block
				Integrator = Decompressed[Idx];
			}
			switch (BlockByte & 0xC0) {
				case 0x03<<6:
					Integrator += Compressed[ExtraPos];
					ExtraPos++;
					break;
				case 0x02<<6:
					Integrator += BlockCode;
					break;
				case 0x00:
					Integrator -= BlockCode;
					break;
			}
			Decompressed[Idx] = Integrator;
			BlockByte <<= 2;
			BlockLen -= 1;
		}
	}
	*StartPos = ExtraPos;
	*BlockTypeStartPos = BlockTypePos;
	return Idx;
}


/*
 * usbvision_parse_compress()
 *
 * Parse compressed frame from the scratch buffer, put
 * decoded RGB value into the current frame buffer and add the written
 * number of bytes (RGB) to the *pcopylen.
 *
 */
static enum ParseState usbvision_parse_compress(struct usb_usbvision *usbvision,
					   long *pcopylen)
{
#define USBVISION_STRIP_MAGIC		0x5A
#define USBVISION_STRIP_LEN_MAX		400
#define USBVISION_STRIP_HEADER_LEN	3

	struct usbvision_frame *frame;
	unsigned char *f,*u = NULL ,*v = NULL;
	unsigned char StripData[USBVISION_STRIP_LEN_MAX];
	unsigned char StripHeader[USBVISION_STRIP_HEADER_LEN];
	int Idx, IdxEnd, StripLen, StripPtr, StartBlockPos, BlockPos, BlockTypePos;
	int clipmask_index, bytes_per_pixel, rc;
	int overlay = usbvision->overlay;
	int imageSize;
	unsigned char rv, gv, bv;
	static unsigned char *Y, *U, *V;

	if (overlay) {
		frame  = &usbvision->overlay_frame;
		imageSize = frame->frmwidth * frame->frmheight;
		if (usbvision->overlay_base == NULL) {
			//video_buffer is not set yet
			return ParseState_NextFrame;
		}
		f = usbvision->overlay_win + frame->curline *
			usbvision->vid_buf.fmt.bytesperline;
	}
	else {
		frame  = usbvision->curFrame;
		imageSize = frame->frmwidth * frame->frmheight;
		if ( (frame->v4l2_format.format == V4L2_PIX_FMT_YUV422P) ||
		     (frame->v4l2_format.format == V4L2_PIX_FMT_YVU420) )
		{       // this is a planar format
								       //... v4l2_linesize not used here.
			f = frame->data + (frame->width * frame->curline);
		} else
			f = frame->data + (frame->v4l2_linesize * frame->curline);

		if (frame->v4l2_format.format == V4L2_PIX_FMT_YUYV){ //initialise u and v pointers
								 // get base of u and b planes add halfoffset

			u = frame->data
				+ imageSize
				+ (frame->frmwidth >>1) * frame->curline ;
			v = u + (imageSize >>1 );

		} else if (frame->v4l2_format.format == V4L2_PIX_FMT_YVU420){

			v = frame->data + imageSize + ((frame->curline* (frame->width))>>2) ;
			u = v + (imageSize >>2) ;
		}
	}

	if (frame->curline == 0) {
		usbvision_adjust_compression(usbvision);
	}

	if (scratch_len(usbvision) < USBVISION_STRIP_HEADER_LEN) {
		return ParseState_Out;
	}

	//get strip header without changing the scratch_read_ptr
	scratch_set_extra_ptr(usbvision, &StripPtr, 0);
	scratch_get_extra(usbvision, &StripHeader[0], &StripPtr,
				USBVISION_STRIP_HEADER_LEN);

	if (StripHeader[0] != USBVISION_STRIP_MAGIC) {
		// wrong strip magic
		usbvision->stripMagicErrors++;
		return ParseState_NextFrame;
	}

	if (frame->curline != (int)StripHeader[2]) {
		//line number missmatch error
		usbvision->stripLineNumberErrors++;
	}

	StripLen = 2 * (unsigned int)StripHeader[1];
	if (StripLen > USBVISION_STRIP_LEN_MAX) {
		// strip overrun
		// I think this never happens
		usbvision_request_intra(usbvision);
	}

	if (scratch_len(usbvision) < StripLen) {
		//there is not enough data for the strip
		return ParseState_Out;
	}

	if (usbvision->IntraFrameBuffer) {
		Y = usbvision->IntraFrameBuffer + frame->frmwidth * frame->curline;
		U = usbvision->IntraFrameBuffer + imageSize + (frame->frmwidth / 2) * (frame->curline / 2);
		V = usbvision->IntraFrameBuffer + imageSize / 4 * 5 + (frame->frmwidth / 2) * (frame->curline / 2);
	}
	else {
		return ParseState_NextFrame;
	}

	bytes_per_pixel = frame->v4l2_format.bytes_per_pixel;
	clipmask_index = frame->curline * MAX_FRAME_WIDTH;

	scratch_get(usbvision, StripData, StripLen);

	IdxEnd = frame->frmwidth;
	BlockTypePos = USBVISION_STRIP_HEADER_LEN;
	StartBlockPos = BlockTypePos + (IdxEnd - 1) / 96 + (IdxEnd / 2 - 1) / 96 + 2;
	BlockPos = StartBlockPos;

	usbvision->BlockPos = BlockPos;

	if ((rc = usbvision_decompress(usbvision, StripData, Y, &BlockPos, &BlockTypePos, IdxEnd)) != IdxEnd) {
		//return ParseState_Continue;
	}
	if (StripLen > usbvision->maxStripLen) {
		usbvision->maxStripLen = StripLen;
	}

	if (frame->curline%2) {
		if ((rc = usbvision_decompress(usbvision, StripData, V, &BlockPos, &BlockTypePos, IdxEnd/2)) != IdxEnd/2) {
		//return ParseState_Continue;
		}
	}
	else {
		if ((rc = usbvision_decompress(usbvision, StripData, U, &BlockPos, &BlockTypePos, IdxEnd/2)) != IdxEnd/2) {
			//return ParseState_Continue;
		}
	}

	if (BlockPos > usbvision->comprBlockPos) {
		usbvision->comprBlockPos = BlockPos;
	}
	if (BlockPos > StripLen) {
		usbvision->stripLenErrors++;
	}

	for (Idx = 0; Idx < IdxEnd; Idx++) {
		if((overlay) && (clipped_pixel(clipmask_index))) {
			f += bytes_per_pixel;
		}
		else if(frame->v4l2_format.format == V4L2_PIX_FMT_YUYV) {
			*f++ = Y[Idx];
			*f++ = Idx & 0x01 ? U[Idx/2] : V[Idx/2];
		}
		else if(frame->v4l2_format.format == V4L2_PIX_FMT_YUV422P) {
			*f++ = Y[Idx];
			if ( Idx & 0x01)
				*u++ = U[Idx>>1] ;
			else
				*v++ = V[Idx>>1];
		}
		else if (frame->v4l2_format.format == V4L2_PIX_FMT_YVU420) {
			*f++ = Y [Idx];
			if ( !((  Idx & 0x01  ) | (  frame->curline & 0x01  )) ){

/* 				 only need do this for 1 in 4 pixels */
/* 				 intraframe buffer is YUV420 format */

				*u++ = U[Idx >>1];
				*v++ = V[Idx >>1];
			}

		}
		else {
			YUV_TO_RGB_BY_THE_BOOK(Y[Idx], U[Idx/2], V[Idx/2], rv, gv, bv);
			switch (frame->v4l2_format.format) {
				case V4L2_PIX_FMT_GREY:
					*f++ = Y[Idx];
					break;
				case V4L2_PIX_FMT_RGB555:
					*f++ = (0x1F & (bv >> 3)) | (0xE0 & (gv << 2));
					*f++ = (0x03 & (gv >> 6)) | (0x7C & (rv >> 1));
					break;
				case V4L2_PIX_FMT_RGB565:
					*f++ = (0x1F & (bv >> 3)) | (0xE0 & (gv << 3));
					*f++ = (0x07 & (gv >> 5)) | (0xF8 &  rv);
					break;
				case V4L2_PIX_FMT_RGB24:
					*f++ = bv;
					*f++ = gv;
					*f++ = rv;
					break;
				case V4L2_PIX_FMT_RGB32:
					*f++ = bv;
					*f++ = gv;
					*f++ = rv;
					f++;
					break;
			}
		}
		clipmask_index++;
	}
	/* Deal with non-integer no. of bytes for YUV420P */
	if (frame->v4l2_format.format != V4L2_PIX_FMT_YVU420 )
		*pcopylen += frame->v4l2_linesize;
	else
		*pcopylen += frame->curline & 0x01 ? frame->v4l2_linesize : frame->v4l2_linesize << 1;

	frame->curline += 1;

	if (frame->curline >= frame->frmheight) {
		return ParseState_NextFrame;
	}
	else {
		return ParseState_Continue;
	}

}


/*
 * usbvision_parse_lines_420()
 *
 * Parse two lines from the scratch buffer, put
 * decoded RGB value into the current frame buffer and add the written
 * number of bytes (RGB) to the *pcopylen.
 *
 */
static enum ParseState usbvision_parse_lines_420(struct usb_usbvision *usbvision,
					   long *pcopylen)
{
	struct usbvision_frame *frame;
	unsigned char *f_even = NULL, *f_odd = NULL;
	unsigned int pixel_per_line, block;
	int pixel, block_split;
	int y_ptr, u_ptr, v_ptr, y_odd_offset;
	const int   y_block_size = 128;
	const int  uv_block_size = 64;
	const int sub_block_size = 32;
	const int y_step[] = { 0, 0, 0, 2 },  y_step_size = 4;
	const int uv_step[]= { 0, 0, 0, 4 }, uv_step_size = 4;
	unsigned char y[2], u, v;	/* YUV components */
	int y_, u_, v_, vb, uvg, ur;
	int r_, g_, b_;			/* RGB components */
	unsigned char g;
	int clipmask_even_index, clipmask_odd_index, bytes_per_pixel;
	int clipmask_add, stretch_bytes;
	int overlay = usbvision->overlay;

	if (overlay) {
		frame  = &usbvision->overlay_frame;
		if (usbvision->overlay_base == NULL) {
			//video_buffer is not set yet
			return ParseState_NextFrame;
		}
		f_even = usbvision->overlay_win + frame->curline *
			 usbvision->vid_buf.fmt.bytesperline;
		f_odd  = f_even + usbvision->vid_buf.fmt.bytesperline * usbvision->stretch_height;
	}
	else {
		frame  = usbvision->curFrame;
		f_even = frame->data + (frame->v4l2_linesize * frame->curline);
		f_odd  = f_even + frame->v4l2_linesize * usbvision->stretch_height;
	}

	/* Make sure there's enough data for the entire line */
	/* In this mode usbvision transfer 3 bytes for every 2 pixels */
	/* I need two lines to decode the color */
	bytes_per_pixel = frame->v4l2_format.bytes_per_pixel;
	stretch_bytes = (usbvision->stretch_width - 1) * bytes_per_pixel;
	clipmask_even_index = frame->curline * MAX_FRAME_WIDTH;
	clipmask_odd_index  = clipmask_even_index + MAX_FRAME_WIDTH;
	clipmask_add = usbvision->stretch_width;
	pixel_per_line = frame->isocHeader.frameWidth;

	if (scratch_len(usbvision) < (int)pixel_per_line * 3) {
		//printk(KERN_DEBUG "out of data, need %d\n", len);
		return ParseState_Out;
	}

	if ((frame->curline + 1) >= frame->frmheight) {
		return ParseState_NextFrame;
	}

	block_split = (pixel_per_line%y_block_size) ? 1 : 0;	//are some blocks splitted into different lines?

	y_odd_offset = (pixel_per_line / y_block_size) * (y_block_size + uv_block_size)
			+ block_split * uv_block_size;

	scratch_set_extra_ptr(usbvision, &y_ptr, y_odd_offset);
	scratch_set_extra_ptr(usbvision, &u_ptr, y_block_size);
	scratch_set_extra_ptr(usbvision, &v_ptr, y_odd_offset
			+ (4 - block_split) * sub_block_size);

	for (block = 0; block < (pixel_per_line / sub_block_size);
	     block++) {


		for (pixel = 0; pixel < sub_block_size; pixel +=2) {
			scratch_get(usbvision, &y[0], 2);
			scratch_get_extra(usbvision, &u, &u_ptr, 1);
			scratch_get_extra(usbvision, &v, &v_ptr, 1);

			//I don't use the YUV_TO_RGB macro for better performance
			v_ = v - 128;
			u_ = u - 128;
			vb =              132252 * v_;
			uvg= -53281 * u_ - 25625 * v_;
			ur = 104595 * u_;

			if((overlay) && (clipped_pixel(clipmask_even_index))) {
				f_even += bytes_per_pixel;
			}
			else if(frame->v4l2_format.format == V4L2_PIX_FMT_YUYV) {
				*f_even++ = y[0];
				*f_even++ = v;
			}
			else {
				y_ = 76284 * (y[0] - 16);

				b_ = (y_ + vb) >> 16;
				g_ = (y_ + uvg)>> 16;
				r_ = (y_ + ur) >> 16;

				switch (frame->v4l2_format.format) {
					case V4L2_PIX_FMT_RGB565:
						g = LIMIT_RGB(g_);
						*f_even++ = (0x1F & (LIMIT_RGB(b_) >> 3)) | (0xE0 & (g << 3));
						*f_even++ = (0x07 & (          g   >> 5)) | (0xF8 & LIMIT_RGB(r_));
						break;
					case V4L2_PIX_FMT_RGB24:
						*f_even++ = LIMIT_RGB(b_);
						*f_even++ = LIMIT_RGB(g_);
						*f_even++ = LIMIT_RGB(r_);
						break;
					case V4L2_PIX_FMT_RGB32:
						*f_even++ = LIMIT_RGB(b_);
						*f_even++ = LIMIT_RGB(g_);
						*f_even++ = LIMIT_RGB(r_);
						f_even++;
						break;
					case V4L2_PIX_FMT_RGB555:
						g = LIMIT_RGB(g_);
						*f_even++ = (0x1F & (LIMIT_RGB(b_) >> 3)) | (0xE0 & (g << 2));
						*f_even++ = (0x03 & (          g   >> 6)) |
							    (0x7C & (LIMIT_RGB(r_) >> 1));
						break;
				}
			}
			clipmask_even_index += clipmask_add;
			f_even += stretch_bytes;

			if((overlay) && (clipped_pixel(clipmask_even_index))) {
				f_even += bytes_per_pixel;
			}
			else if(frame->v4l2_format.format == V4L2_PIX_FMT_YUYV) {
				*f_even++ = y[1];
				*f_even++ = u;
			}
			else {
				y_ = 76284 * (y[1] - 16);

				b_ = (y_ + vb) >> 16;
				g_ = (y_ + uvg)>> 16;
				r_ = (y_ + ur) >> 16;

				switch (frame->v4l2_format.format) {
					case V4L2_PIX_FMT_RGB565:
						g = LIMIT_RGB(g_);
						*f_even++ = (0x1F & (LIMIT_RGB(b_) >> 3)) | (0xE0 & (g << 3));
						*f_even++ = (0x07 & (          g   >> 5)) | (0xF8 & LIMIT_RGB(r_));
						break;
					case V4L2_PIX_FMT_RGB24:
						*f_even++ = LIMIT_RGB(b_);
						*f_even++ = LIMIT_RGB(g_);
						*f_even++ = LIMIT_RGB(r_);
						break;
					case V4L2_PIX_FMT_RGB32:
						*f_even++ = LIMIT_RGB(b_);
						*f_even++ = LIMIT_RGB(g_);
						*f_even++ = LIMIT_RGB(r_);
						f_even++;
						break;
					case V4L2_PIX_FMT_RGB555:
						g = LIMIT_RGB(g_);
						*f_even++ = (0x1F & (LIMIT_RGB(b_) >> 3)) | (0xE0 & (g << 2));
						*f_even++ = (0x03 & (          g   >> 6)) |
							    (0x7C & (LIMIT_RGB(r_) >> 1));
						break;
				}
			}
			clipmask_even_index += clipmask_add;
			f_even += stretch_bytes;

			scratch_get_extra(usbvision, &y[0], &y_ptr, 2);

			if ((overlay) && (clipped_pixel(clipmask_odd_index))) {
				f_odd += bytes_per_pixel;
			}
			else if(frame->v4l2_format.format == V4L2_PIX_FMT_YUYV) {
				*f_odd++ = y[0];
				*f_odd++ = v;
			}
			else {
				y_ = 76284 * (y[0] - 16);

				b_ = (y_ + vb) >> 16;
				g_ = (y_ + uvg)>> 16;
				r_ = (y_ + ur) >> 16;

				switch (frame->v4l2_format.format) {
					case V4L2_PIX_FMT_RGB565:
						g = LIMIT_RGB(g_);
						*f_odd++ = (0x1F & (LIMIT_RGB(b_) >> 3)) | (0xE0 & (g << 3));
						*f_odd++ = (0x07 & (          g   >> 5)) | (0xF8 & LIMIT_RGB(r_));
						break;
					case V4L2_PIX_FMT_RGB24:
						*f_odd++ = LIMIT_RGB(b_);
						*f_odd++ = LIMIT_RGB(g_);
						*f_odd++ = LIMIT_RGB(r_);
						break;
					case V4L2_PIX_FMT_RGB32:
						*f_odd++ = LIMIT_RGB(b_);
						*f_odd++ = LIMIT_RGB(g_);
						*f_odd++ = LIMIT_RGB(r_);
						f_odd++;
						break;
					case V4L2_PIX_FMT_RGB555:
						g = LIMIT_RGB(g_);
						*f_odd++ = (0x1F & (LIMIT_RGB(b_) >> 3)) | (0xE0 & (g << 2));
						*f_odd++ = (0x03 & (          g   >> 6)) |
							   (0x7C & (LIMIT_RGB(r_) >> 1));
						break;
				}
			}
			clipmask_odd_index += clipmask_add;
			f_odd += stretch_bytes;

			if((overlay) && (clipped_pixel(clipmask_odd_index))) {
				f_odd += bytes_per_pixel;
			}
			else if(frame->v4l2_format.format == V4L2_PIX_FMT_YUYV) {
				*f_odd++ = y[1];
				*f_odd++ = u;
			}
			else {
				y_ = 76284 * (y[1] - 16);

				b_ = (y_ + vb) >> 16;
				g_ = (y_ + uvg)>> 16;
				r_ = (y_ + ur) >> 16;

				switch (frame->v4l2_format.format) {
					case V4L2_PIX_FMT_RGB565:
						g = LIMIT_RGB(g_);
						*f_odd++ = (0x1F & (LIMIT_RGB(b_) >> 3)) | (0xE0 & (g << 3));
						*f_odd++ = (0x07 & (          g   >> 5)) | (0xF8 & LIMIT_RGB(r_));
						break;
					case V4L2_PIX_FMT_RGB24:
						*f_odd++ = LIMIT_RGB(b_);
						*f_odd++ = LIMIT_RGB(g_);
						*f_odd++ = LIMIT_RGB(r_);
						break;
					case V4L2_PIX_FMT_RGB32:
						*f_odd++ = LIMIT_RGB(b_);
						*f_odd++ = LIMIT_RGB(g_);
						*f_odd++ = LIMIT_RGB(r_);
						f_odd++;
						break;
					case V4L2_PIX_FMT_RGB555:
						g = LIMIT_RGB(g_);
						*f_odd++ = (0x1F & (LIMIT_RGB(b_) >> 3)) | (0xE0 & (g << 2));
						*f_odd++ = (0x03 & (          g   >> 6)) |
							   (0x7C & (LIMIT_RGB(r_) >> 1));
						break;
				}
			}
			clipmask_odd_index += clipmask_add;
			f_odd += stretch_bytes;
		}

		scratch_rm_old(usbvision,y_step[block % y_step_size] * sub_block_size);
		scratch_inc_extra_ptr(&y_ptr, y_step[(block + 2 * block_split) % y_step_size]
				* sub_block_size);
		scratch_inc_extra_ptr(&u_ptr, uv_step[block % uv_step_size]
				* sub_block_size);
		scratch_inc_extra_ptr(&v_ptr, uv_step[(block + 2 * block_split) % uv_step_size]
				* sub_block_size);
	}

	scratch_rm_old(usbvision, pixel_per_line * 3 / 2
			+ block_split * sub_block_size);

	frame->curline += 2 * usbvision->stretch_height;
	*pcopylen += frame->v4l2_linesize * 2 * usbvision->stretch_height;

	if (frame->curline >= frame->frmheight)
		return ParseState_NextFrame;
	else
		return ParseState_Continue;
}

/*
 * usbvision_parse_data()
 *
 * Generic routine to parse the scratch buffer. It employs either
 * usbvision_find_header() or usbvision_parse_lines() to do most
 * of work.
 *
 */
static void usbvision_parse_data(struct usb_usbvision *usbvision)
{
	struct usbvision_frame *frame;
	enum ParseState newstate;
	long copylen = 0;
	unsigned long lock_flags;

	if (usbvision->overlay) {
		frame = &usbvision->overlay_frame;
	}
	else {
		frame = usbvision->curFrame;
	}

	PDEBUG(DBG_PARSE, "parsing len=%d\n", scratch_len(usbvision));

	while (1) {

		newstate = ParseState_Out;
		if (scratch_len(usbvision)) {
			if (frame->scanstate == ScanState_Scanning) {
				newstate = usbvision_find_header(usbvision);
			}
			else if (frame->scanstate == ScanState_Lines) {
				if (usbvision->isocMode == ISOC_MODE_YUV420) {
					newstate = usbvision_parse_lines_420(usbvision, &copylen);
				}
				else if (usbvision->isocMode == ISOC_MODE_YUV422) {
					newstate = usbvision_parse_lines_422(usbvision, &copylen);
				}
				else if (usbvision->isocMode == ISOC_MODE_COMPRESS) {
					newstate = usbvision_parse_compress(usbvision, &copylen);
				}

			}
		}
		if (newstate == ParseState_Continue) {
			continue;
		}
		else if ((newstate == ParseState_NextFrame) || (newstate == ParseState_Out)) {
			break;
		}
		else {
			return;	/* ParseState_EndParse */
		}
	}

	if (newstate == ParseState_NextFrame) {
		frame->grabstate = FrameState_Done;
		do_gettimeofday(&(frame->timestamp));
		frame->sequence = usbvision->frame_num;
		if (usbvision->overlay) {
			frame->grabstate = FrameState_Grabbing;
			frame->scanstate = ScanState_Scanning;
			frame->scanlength = 0;
			copylen = 0;
		}
		else {
			spin_lock_irqsave(&usbvision->queue_lock, lock_flags);
			list_move_tail(&(frame->frame), &usbvision->outqueue);
			usbvision->curFrame = NULL;
			spin_unlock_irqrestore(&usbvision->queue_lock, lock_flags);
		}
		usbvision->frame_num++;

		/* Optionally display statistics on the screen */
		if (flags & FLAGS_OSD_STATS)
			usbvision_osd_stats(usbvision, frame);

		/* This will cause the process to request another frame. */
		if (waitqueue_active(&usbvision->wait_frame)) {
			PDEBUG(DBG_PARSE, "Wake up !");
			wake_up_interruptible(&usbvision->wait_frame);
		}
	}
	else
		frame->grabstate = FrameState_Grabbing;


	/* Update the frame's uncompressed length. */
	frame->scanlength += copylen;
}


/*
 * Make all of the blocks of data contiguous
 */
static int usbvision_compress_isochronous(struct usb_usbvision *usbvision,
					  struct urb *urb)
{
	unsigned char *packet_data;
	int i, totlen = 0;

	for (i = 0; i < urb->number_of_packets; i++) {
		int packet_len = urb->iso_frame_desc[i].actual_length;
		int packet_stat = urb->iso_frame_desc[i].status;

		packet_data = urb->transfer_buffer + urb->iso_frame_desc[i].offset;

		/* Detect and ignore errored packets */
		if (packet_stat) {	// packet_stat != 0 ?????????????
			PDEBUG(DBG_ISOC, "data error: [%d] len=%d, status=%X", i, packet_len, packet_stat);
			usbvision->isocErrCount++;
			continue;
		}

		/* Detect and ignore empty packets */
		if (packet_len < 0) {
			PDEBUG(DBG_ISOC, "error packet [%d]", i);
			usbvision->isocSkipCount++;
			continue;
		}
		else if (packet_len == 0) {	/* Frame end ????? */
			PDEBUG(DBG_ISOC, "null packet [%d]", i);
			usbvision->isocstate=IsocState_NoFrame;
			usbvision->isocSkipCount++;
			continue;
		}
		else if (packet_len > usbvision->isocPacketSize) {
			PDEBUG(DBG_ISOC, "packet[%d] > isocPacketSize", i);
			usbvision->isocSkipCount++;
			continue;
		}

		PDEBUG(DBG_ISOC, "packet ok [%d] len=%d", i, packet_len);

		if (usbvision->isocstate==IsocState_NoFrame) { //new frame begins
			usbvision->isocstate=IsocState_InFrame;
			scratch_mark_header(usbvision);
			usbvision_measure_bandwidth(usbvision);
			PDEBUG(DBG_ISOC, "packet with header");
		}

		/*
		 * If usbvision continues to feed us with data but there is no
		 * consumption (if, for example, V4L client fell asleep) we
		 * may overflow the buffer. We have to move old data over to
		 * free room for new data. This is bad for old data. If we
		 * just drop new data then it's bad for new data... choose
		 * your favorite evil here.
		 */
		if (scratch_free(usbvision) < packet_len) {

			usbvision->scratch_ovf_count++;
			PDEBUG(DBG_ISOC, "scratch buf overflow! scr_len: %d, n: %d",
			       scratch_len(usbvision), packet_len);
			scratch_rm_old(usbvision, packet_len - scratch_free(usbvision));
		}

		/* Now we know that there is enough room in scratch buffer */
		scratch_put(usbvision, packet_data, packet_len);
		totlen += packet_len;
		usbvision->isocDataCount += packet_len;
		usbvision->isocPacketCount++;
	}
#if ENABLE_HEXDUMP
	if (totlen > 0) {
		static int foo = 0;
		if (foo < 1) {
			printk(KERN_DEBUG "+%d.\n", usbvision->scratchlen);
			usbvision_hexdump(data0, (totlen > 64) ? 64 : totlen);
			++foo;
		}
	}
#endif
 return totlen;
}

static void usbvision_isocIrq(struct urb *urb, struct pt_regs *regs)
{
	int errCode = 0;
	int len;
	struct usb_usbvision *usbvision = urb->context;
	int i;
	unsigned long startTime = jiffies;
	struct usbvision_frame **f;

	/* We don't want to do anything if we are about to be removed! */
	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return;

	f = &usbvision->curFrame;

	/* Manage streaming interruption */
	if (usbvision->streaming == Stream_Interrupt) {
		usbvision->streaming = Stream_Idle;
		if ((*f)) {
			(*f)->grabstate = FrameState_Ready;
			(*f)->scanstate = ScanState_Scanning;
		}
		PDEBUG(DBG_IRQ, "stream interrupted");
		wake_up_interruptible(&usbvision->wait_stream);
	}

	/* Copy the data received into our scratch buffer */
	len = usbvision_compress_isochronous(usbvision, urb);

	usbvision->isocUrbCount++;
	usbvision->urb_length = len;

	if (usbvision->streaming == Stream_On) {

		/* If we collected enough data let's parse! */
		if (scratch_len(usbvision) > USBVISION_HEADER_LENGTH) {	/* 12 == header_length */
			/*If we don't have a frame we're current working on, complain */
			if((!list_empty(&(usbvision->inqueue))) || (usbvision->overlay)) {
				if (!(*f)) {
					(*f) = list_entry(usbvision->inqueue.next,struct usbvision_frame, frame);
				}
				usbvision_parse_data(usbvision);
			}
			else {
				PDEBUG(DBG_IRQ, "received data, but no one needs it");
				scratch_reset(usbvision);
			}
		}
	}
	else {
		PDEBUG(DBG_IRQ, "received data, but no one needs it");
		scratch_reset(usbvision);
	}

	usbvision->timeInIrq += jiffies - startTime;

	for (i = 0; i < USBVISION_URB_FRAMES; i++) {
		urb->iso_frame_desc[i].status = 0;
		urb->iso_frame_desc[i].actual_length = 0;
	}

	urb->status = 0;
	urb->dev = usbvision->dev;
	errCode = usb_submit_urb (urb, GFP_ATOMIC);

	/* Disable this warning.  By design of the driver. */
	//	if(errCode) {
	//		err("%s: usb_submit_urb failed: error %d", __FUNCTION__, errCode);
	//	}

	return;
}

/*************************************/
/* Low level usbvision access functions */
/*************************************/

/*
 * usbvision_read_reg()
 *
 * return  < 0 -> Error
 *        >= 0 -> Data
 */

static int usbvision_read_reg(struct usb_usbvision *usbvision, unsigned char reg)
{
	int errCode = 0;
	unsigned char buffer[1];

	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return -1;

	errCode = usb_control_msg(usbvision->dev, usb_rcvctrlpipe(usbvision->dev, 1),
				USBVISION_OP_CODE,
				USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT,
				0, (__u16) reg, buffer, 1, HZ);

	if (errCode < 0) {
		err("%s: failed: error %d", __FUNCTION__, errCode);
		return errCode;
	}
	return buffer[0];
}

/*
 * usbvision_write_reg()
 *
 * return 1 -> Reg written
 *        0 -> usbvision is not yet ready
 *       -1 -> Something went wrong
 */

static int usbvision_write_reg(struct usb_usbvision *usbvision, unsigned char reg,
			    unsigned char value)
{
	int errCode = 0;

	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return 0;

	errCode = usb_control_msg(usbvision->dev, usb_sndctrlpipe(usbvision->dev, 1),
				USBVISION_OP_CODE,
				USB_DIR_OUT | USB_TYPE_VENDOR |
				USB_RECIP_ENDPOINT, 0, (__u16) reg, &value, 1, HZ);

	if (errCode < 0) {
		err("%s: failed: error %d", __FUNCTION__, errCode);
	}
	return errCode;
}


static void usbvision_ctrlUrb_complete(struct urb *urb, struct pt_regs *regs)
{
	struct usb_usbvision *usbvision = (struct usb_usbvision *)urb->context;

	PDEBUG(DBG_IRQ, "");
	usbvision->ctrlUrbBusy = 0;
	if (waitqueue_active(&usbvision->ctrlUrb_wq)) {
		wake_up_interruptible(&usbvision->ctrlUrb_wq);
	}
}


static int usbvision_write_reg_irq(struct usb_usbvision *usbvision,int address,
									unsigned char *data, int len)
{
	int errCode = 0;

	PDEBUG(DBG_IRQ, "");
	if (len > 8) {
		return -EFAULT;
	}
//	down(&usbvision->ctrlUrbLock);
	if (usbvision->ctrlUrbBusy) {
//		up(&usbvision->ctrlUrbLock);
		return -EBUSY;
	}
	usbvision->ctrlUrbBusy = 1;
//	up(&usbvision->ctrlUrbLock);

	usbvision->ctrlUrbSetup.bRequestType = USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT;
	usbvision->ctrlUrbSetup.bRequest     = USBVISION_OP_CODE;
	usbvision->ctrlUrbSetup.wValue       = 0;
	usbvision->ctrlUrbSetup.wIndex       = cpu_to_le16(address);
	usbvision->ctrlUrbSetup.wLength      = cpu_to_le16(len);
	usb_fill_control_urb (usbvision->ctrlUrb, usbvision->dev,
							usb_sndctrlpipe(usbvision->dev, 1),
							(unsigned char *)&usbvision->ctrlUrbSetup,
							(void *)usbvision->ctrlUrbBuffer, len,
							usbvision_ctrlUrb_complete,
							(void *)usbvision);

	memcpy(usbvision->ctrlUrbBuffer, data, len);

	errCode = usb_submit_urb(usbvision->ctrlUrb, GFP_ATOMIC);
	if (errCode < 0) {
		// error in usb_submit_urb()
		usbvision->ctrlUrbBusy = 0;
	}
	PDEBUG(DBG_IRQ, "submit %d byte: error %d", len, errCode);
	return errCode;
}




static int usbvision_init_compression(struct usb_usbvision *usbvision)
{
	int errCode = 0;

	usbvision->lastIsocFrameNum = -1;
	usbvision->isocDataCount = 0;
	usbvision->isocPacketCount = 0;
	usbvision->isocSkipCount = 0;
	usbvision->comprLevel = 50;
	usbvision->lastComprLevel = -1;
	usbvision->isocUrbCount = 0;
	usbvision->requestIntra = 1;
	usbvision->isocMeasureBandwidthCount = 0;

	return errCode;
}

/* this function measures the used bandwidth since last call
 * return:    0 : no error
 * sets usedBandwidth to 1-100 : 1-100% of full bandwidth resp. to isocPacketSize
 */
static int usbvision_measure_bandwidth (struct usb_usbvision *usbvision)
{
	int errCode = 0;

	if (usbvision->isocMeasureBandwidthCount < 2) { // this gives an average bandwidth of 3 frames
		usbvision->isocMeasureBandwidthCount++;
		return errCode;
	}
	if ((usbvision->isocPacketSize > 0) && (usbvision->isocPacketCount > 0)) {
		usbvision->usedBandwidth = usbvision->isocDataCount /
					(usbvision->isocPacketCount + usbvision->isocSkipCount) *
					100 / usbvision->isocPacketSize;
	}
	usbvision->isocMeasureBandwidthCount = 0;
	usbvision->isocDataCount = 0;
	usbvision->isocPacketCount = 0;
	usbvision->isocSkipCount = 0;
	return errCode;
}

static int usbvision_adjust_compression (struct usb_usbvision *usbvision)
{
	int errCode = 0;
	unsigned char buffer[6];

	PDEBUG(DBG_IRQ, "");
	if ((adjustCompression) && (usbvision->usedBandwidth > 0)) {
		usbvision->comprLevel += (usbvision->usedBandwidth - 90) / 2;
		RESTRICT_TO_RANGE(usbvision->comprLevel, 0, 100);
		if (usbvision->comprLevel != usbvision->lastComprLevel) {
			int distorsion;
			if (usbvision->bridgeType == BRIDGE_NT1004 || usbvision->bridgeType == BRIDGE_NT1005) {
				buffer[0] = (unsigned char)(4 + 16 * usbvision->comprLevel / 100);	// PCM Threshold 1
				buffer[1] = (unsigned char)(4 + 8 * usbvision->comprLevel / 100);	// PCM Threshold 2
				distorsion = 7 + 248 * usbvision->comprLevel / 100;
				buffer[2] = (unsigned char)(distorsion & 0xFF);				// Average distorsion Threshold (inter)
				buffer[3] = (unsigned char)(distorsion & 0xFF);				// Average distorsion Threshold (intra)
				distorsion = 1 + 42 * usbvision->comprLevel / 100;
				buffer[4] = (unsigned char)(distorsion & 0xFF);				// Maximum distorsion Threshold (inter)
				buffer[5] = (unsigned char)(distorsion & 0xFF);				// Maximum distorsion Threshold (intra)
			}
			else { //BRIDGE_NT1003
				buffer[0] = (unsigned char)(4 + 16 * usbvision->comprLevel / 100);	// PCM threshold 1
				buffer[1] = (unsigned char)(4 + 8 * usbvision->comprLevel / 100);	// PCM threshold 2
				distorsion = 2 + 253 * usbvision->comprLevel / 100;
				buffer[2] = (unsigned char)(distorsion & 0xFF);				// distorsion threshold bit0-7
				buffer[3] = 0; 	//(unsigned char)((distorsion >> 8) & 0x0F);		// distorsion threshold bit 8-11
				distorsion = 0 + 43 * usbvision->comprLevel / 100;
				buffer[4] = (unsigned char)(distorsion & 0xFF);				// maximum distorsion bit0-7
				buffer[5] = 0; //(unsigned char)((distorsion >> 8) & 0x01);		// maximum distorsion bit 8
			}
			errCode = usbvision_write_reg_irq(usbvision, USBVISION_PCM_THR1, buffer, 6);
			if (errCode == 0){
				PDEBUG(DBG_IRQ, "new compr params %#02x %#02x %#02x %#02x %#02x %#02x", buffer[0],
								buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);
				usbvision->lastComprLevel = usbvision->comprLevel;
			}
		}
	}
	return errCode;
}

static int usbvision_request_intra (struct usb_usbvision *usbvision)
{
	int errCode = 0;
	unsigned char buffer[1];

	PDEBUG(DBG_IRQ, "");
	usbvision->requestIntra = 1;
	buffer[0] = 1;
	usbvision_write_reg_irq(usbvision, USBVISION_FORCE_INTRA, buffer, 1);
	return errCode;
}

static int usbvision_unrequest_intra (struct usb_usbvision *usbvision)
{
	int errCode = 0;
	unsigned char buffer[1];

	PDEBUG(DBG_IRQ, "");
	usbvision->requestIntra = 0;
	buffer[0] = 0;
	usbvision_write_reg_irq(usbvision, USBVISION_FORCE_INTRA, buffer, 1);
	return errCode;
}

/* ----------------------------------------------------------------------- */
/* I2C functions                                                           */
/* ----------------------------------------------------------------------- */

static void call_i2c_clients(struct usb_usbvision *usbvision, unsigned int cmd,
			     void *arg)
{
	BUG_ON(NULL == usbvision->i2c_adap.algo_data);
	i2c_clients_command(&usbvision->i2c_adap, cmd, arg);
}

static int attach_inform(struct i2c_client *client)
{
	struct usb_usbvision *usbvision;

	usbvision = (struct usb_usbvision *)i2c_get_adapdata(client->adapter);

	switch (client->addr << 1) {
		case 0x43:
		case 0x4b:
		{
			struct tuner_setup tun_setup;

			tun_setup.mode_mask = T_ANALOG_TV | T_RADIO;
			tun_setup.type = TUNER_TDA9887;
			tun_setup.addr = client->addr;

			call_i2c_clients(usbvision, TUNER_SET_TYPE_ADDR, &tun_setup);

			break;
		}
		case 0x42:
			PDEBUG(DBG_I2C,"attach_inform: saa7114 detected.\n");
			break;
		case 0x4a:
			PDEBUG(DBG_I2C,"attach_inform: saa7113 detected.\n");
			break;
		case 0xa0:
			PDEBUG(DBG_I2C,"attach_inform: eeprom detected.\n");
			break;

		default:
			PDEBUG(DBG_I2C,"attach inform: detected I2C address %x\n", client->addr << 1);
			{
				struct tuner_setup tun_setup;

				usbvision->tuner_addr = client->addr;

				if ((usbvision->have_tuner) && (usbvision->tuner_type != -1)) {
					tun_setup.mode_mask = T_ANALOG_TV | T_RADIO;
					tun_setup.type = usbvision->tuner_type;
					tun_setup.addr = usbvision->tuner_addr;
					call_i2c_clients(usbvision, TUNER_SET_TYPE_ADDR, &tun_setup);
				}
			}
			break;
	}
	return 0;
}

static int detach_inform(struct i2c_client *client)
{
	struct usb_usbvision *usbvision;

	#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
		usbvision = (struct usb_usbvision *)client->adapter->data;
	#else
		usbvision = (struct usb_usbvision *)i2c_get_adapdata(client->adapter);
	#endif

	PDEBUG(DBG_I2C, "usbvision[%d] detaches %s", usbvision->nr, client->name);
	return 0;
}

static int
usbvision_i2c_read_max4(struct usb_usbvision *usbvision, unsigned char addr,
		     char *buf, short len)
{
	int rc, retries;

	for (retries = 5;;) {
		rc = usbvision_write_reg(usbvision, USBVISION_SER_ADRS, addr);
		if (rc < 0)
			return rc;

		/* Initiate byte read cycle                    */
		/* USBVISION_SER_CONT <- d0-d2 n. of bytes to r/w */
		/*                    d3 0=Wr 1=Rd             */
		rc = usbvision_write_reg(usbvision, USBVISION_SER_CONT,
				      (len & 0x07) | 0x18);
		if (rc < 0)
			return rc;

		/* Test for Busy and ACK */
		do {
			/* USBVISION_SER_CONT -> d4 == 0 busy */
			rc = usbvision_read_reg(usbvision, USBVISION_SER_CONT);
		} while (rc > 0 && ((rc & 0x10) != 0));	/* Retry while busy */
		if (rc < 0)
			return rc;

		/* USBVISION_SER_CONT -> d5 == 1 Not ack */
		if ((rc & 0x20) == 0)	/* Ack? */
			break;

		/* I2C abort */
		rc = usbvision_write_reg(usbvision, USBVISION_SER_CONT, 0x00);
		if (rc < 0)
			return rc;

		if (--retries < 0)
			return -1;
	}

	switch (len) {
	case 4:
		buf[3] = usbvision_read_reg(usbvision, USBVISION_SER_DAT4);
	case 3:
		buf[2] = usbvision_read_reg(usbvision, USBVISION_SER_DAT3);
	case 2:
		buf[1] = usbvision_read_reg(usbvision, USBVISION_SER_DAT2);
	case 1:
		buf[0] = usbvision_read_reg(usbvision, USBVISION_SER_DAT1);
		break;
	default:
		printk(KERN_ERR
		       "usbvision_i2c_read_max4: buffer length > 4\n");
	}

	if (debug & DBG_I2C) {
		int idx;
		for (idx = 0; idx < len; idx++) {
			PDEBUG(DBG_I2C, "read %x from address %x", (unsigned char)buf[idx], addr);
		}
	}
	return len;
}


static int usbvision_i2c_write_max4(struct usb_usbvision *usbvision,
				 unsigned char addr, const char *buf,
				 short len)
{
	int rc, retries;
	int i;
	unsigned char value[6];
	unsigned char ser_cont;

	ser_cont = (len & 0x07) | 0x10;

	value[0] = addr;
	value[1] = ser_cont;
	for (i = 0; i < len; i++)
		value[i + 2] = buf[i];

	for (retries = 5;;) {
		rc = usb_control_msg(usbvision->dev,
				     usb_sndctrlpipe(usbvision->dev, 1),
				     USBVISION_OP_CODE,
				     USB_DIR_OUT | USB_TYPE_VENDOR |
				     USB_RECIP_ENDPOINT, 0,
				     (__u16) USBVISION_SER_ADRS, value,
				     len + 2, HZ);

		if (rc < 0)
			return rc;

		rc = usbvision_write_reg(usbvision, USBVISION_SER_CONT,
				      (len & 0x07) | 0x10);
		if (rc < 0)
			return rc;

		/* Test for Busy and ACK */
		do {
			rc = usbvision_read_reg(usbvision, USBVISION_SER_CONT);
		} while (rc > 0 && ((rc & 0x10) != 0));	/* Retry while busy */
		if (rc < 0)
			return rc;

		if ((rc & 0x20) == 0)	/* Ack? */
			break;

		/* I2C abort */
		usbvision_write_reg(usbvision, USBVISION_SER_CONT, 0x00);

		if (--retries < 0)
			return -1;

	}

	if (debug & DBG_I2C) {
		int idx;
		for (idx = 0; idx < len; idx++) {
			PDEBUG(DBG_I2C, "wrote %x at address %x", (unsigned char)buf[idx], addr);
		}
	}
	return len;
}

static int usbvision_i2c_write(void *data, unsigned char addr, char *buf,
			    short len)
{
	char *bufPtr = buf;
	int retval;
	int wrcount = 0;
	int count;
	int maxLen = 4;
	struct usb_usbvision *usbvision = (struct usb_usbvision *) data;

	while (len > 0) {
		count = (len > maxLen) ? maxLen : len;
		retval = usbvision_i2c_write_max4(usbvision, addr, bufPtr, count);
		if (retval > 0) {
			len -= count;
			bufPtr += count;
			wrcount += count;
		} else
			return (retval < 0) ? retval : -EFAULT;
	}
	return wrcount;
}

static int usbvision_i2c_read(void *data, unsigned char addr, char *buf,
			   short len)
{
	char temp[4];
	int retval, i;
	int rdcount = 0;
	int count;
	struct usb_usbvision *usbvision = (struct usb_usbvision *) data;

	while (len > 0) {
		count = (len > 3) ? 4 : len;
		retval = usbvision_i2c_read_max4(usbvision, addr, temp, count);
		if (retval > 0) {
			for (i = 0; i < len; i++)
				buf[rdcount + i] = temp[i];
			len -= count;
			rdcount += count;
		} else
			return (retval < 0) ? retval : -EFAULT;
	}
	return rdcount;
}

static struct i2c_algo_usb_data i2c_algo_template = {
	.data		= NULL,
	.inb		= usbvision_i2c_read,
	.outb		= usbvision_i2c_write,
	.udelay		= 10,
	.mdelay		= 10,
	.timeout	= 100,
};

static struct i2c_adapter i2c_adap_template = {
	.owner             = THIS_MODULE,
	.name              = "usbvision",
	.id                = I2C_HW_B_BT848, /* FIXME */
	.algo              = NULL,
	.algo_data         = NULL,
	.client_register   = attach_inform,
	.client_unregister = detach_inform,
#if defined (I2C_ADAP_CLASS_TV_ANALOG)
	.class             = I2C_ADAP_CLASS_TV_ANALOG,
#elif defined (I2C_CLASS_TV_ANALOG)
	.class		   = I2C_CLASS_TV_ANALOG,
#endif
};

static struct i2c_client i2c_client_template = {
	.name		= "usbvision internal",
	.flags		= 0,
	.addr		= 0,
	.adapter	= NULL,
	.driver		= NULL,
};

static int usbvision_init_i2c(struct usb_usbvision *usbvision)
{
	memcpy(&usbvision->i2c_adap, &i2c_adap_template,
	       sizeof(struct i2c_adapter));
	memcpy(&usbvision->i2c_algo, &i2c_algo_template,
	       sizeof(struct i2c_algo_usb_data));
	memcpy(&usbvision->i2c_client, &i2c_client_template,
	       sizeof(struct i2c_client));

	sprintf(usbvision->i2c_adap.name + strlen(usbvision->i2c_adap.name),
		" #%d", usbvision->vdev->minor & 0x1f);
	PDEBUG(DBG_I2C, "Adaptername: %s", usbvision->i2c_adap.name);

	i2c_set_adapdata(&usbvision->i2c_adap, usbvision);
	i2c_set_clientdata(&usbvision->i2c_client, usbvision);
	i2c_set_algo_usb_data(&usbvision->i2c_algo, usbvision);

	usbvision->i2c_adap.algo_data = &usbvision->i2c_algo;
	usbvision->i2c_client.adapter = &usbvision->i2c_adap;

	if (usbvision_write_reg(usbvision, USBVISION_SER_MODE, USBVISION_IIC_LRNACK) < 0) {
		printk(KERN_ERR "usbvision_init_i2c: can't wirte reg\n");
		return -EBUSY;
	}

#ifdef CONFIG_KMOD
	/* Request the load of the i2c modules we need */
	if (autoload) {
		switch (usbvision_device_data[usbvision->DevModel].Codec) {
			case CODEC_SAA7113:
				request_module("saa7115");
				break;
			case CODEC_SAA7111:
				request_module("saa7115");
				break;
		}
		if (usbvision_device_data[usbvision->DevModel].Tuner == 1) {
			request_module("tuner");
		}
	}
#endif

	return usbvision_i2c_usb_add_bus(&usbvision->i2c_adap);
}


/****************************/
/* usbvision utility functions */
/****************************/

static int usbvision_power_off(struct usb_usbvision *usbvision)
{
	int errCode = 0;

	PDEBUG(DBG_FUNC, "");

	errCode = usbvision_write_reg(usbvision, USBVISION_PWR_REG, USBVISION_SSPND_EN);
	if (errCode == 1) {
		usbvision->power = 0;
	}
	PDEBUG(DBG_FUNC, "%s: errCode %d", (errCode!=1)?"ERROR":"power is off", errCode);
	return errCode;
}


// to call usbvision_power_off from task queue
static void call_usbvision_power_off(void *_usbvision)
{
	struct usb_usbvision *usbvision = _usbvision;

	PDEBUG(DBG_FUNC, "");
	down_interruptible(&usbvision->lock);
	if(usbvision->user == 0) {
		usbvision_i2c_usb_del_bus(&usbvision->i2c_adap);
		usbvision_power_off(usbvision);
		usbvision->initialized = 0;
	}
	up(&usbvision->lock);
}


/*
 * usbvision_set_video_format()
 *
 */
static int usbvision_set_video_format(struct usb_usbvision *usbvision, int format)
{
	static const char proc[] = "usbvision_set_video_format";
	int rc;
	unsigned char value[2];

	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return 0;

	PDEBUG(DBG_FUNC, "isocMode %#02x", format);

	if ((format != ISOC_MODE_YUV422)
	    && (format != ISOC_MODE_YUV420)
	    && (format != ISOC_MODE_COMPRESS)) {
		printk(KERN_ERR "usbvision: unknown video format %02x, using default YUV420",
		       format);
		format = ISOC_MODE_YUV420;
	}
	value[0] = 0x0A;  //TODO: See the effect of the filter
	value[1] = format;
	rc = usb_control_msg(usbvision->dev, usb_sndctrlpipe(usbvision->dev, 1),
			     USBVISION_OP_CODE,
			     USB_DIR_OUT | USB_TYPE_VENDOR |
			     USB_RECIP_ENDPOINT, 0,
			     (__u16) USBVISION_FILT_CONT, value, 2, HZ);

	if (rc < 0) {
		printk(KERN_ERR "%s: ERROR=%d. USBVISION stopped - "
		       "reconnect or reload driver.\n", proc, rc);
	}
	usbvision->isocMode = format;
	return rc;
}

/*
 * usbvision_set_output()
 *
 */

static int usbvision_set_output(struct usb_usbvision *usbvision, int width,
				  int height)
{
	int errCode = 0;
	int UsbWidth, UsbHeight;
	unsigned int frameRate=0, frameDrop=0;
	unsigned char value[4];

	if (!USBVISION_IS_OPERATIONAL(usbvision)) {
		return 0;
	}

	if (width > MAX_USB_WIDTH) {
		UsbWidth = width / 2;
		usbvision->stretch_width = 2;
	}
	else {
		UsbWidth = width;
		usbvision->stretch_width = 1;
	}

	if (height > MAX_USB_HEIGHT) {
		UsbHeight = height / 2;
		usbvision->stretch_height = 2;
	}
	else {
		UsbHeight = height;
		usbvision->stretch_height = 1;
	}

	RESTRICT_TO_RANGE(UsbWidth, MIN_FRAME_WIDTH, MAX_USB_WIDTH);
	UsbWidth &= ~(MIN_FRAME_WIDTH-1);
	RESTRICT_TO_RANGE(UsbHeight, MIN_FRAME_HEIGHT, MAX_USB_HEIGHT);
	UsbHeight &= ~(1);

	PDEBUG(DBG_FUNC, "usb %dx%d; screen %dx%d; stretch %dx%d",
						UsbWidth, UsbHeight, width, height,
						usbvision->stretch_width, usbvision->stretch_height);

	/* I'll not rewrite the same values */
	if ((UsbWidth != usbvision->curwidth) || (UsbHeight != usbvision->curheight)) {
		value[0] = UsbWidth & 0xff;		//LSB
		value[1] = (UsbWidth >> 8) & 0x03;	//MSB
		value[2] = UsbHeight & 0xff;		//LSB
		value[3] = (UsbHeight >> 8) & 0x03;	//MSB

		errCode = usb_control_msg(usbvision->dev, usb_sndctrlpipe(usbvision->dev, 1),
			     USBVISION_OP_CODE,
			     USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT,
				 0, (__u16) USBVISION_LXSIZE_O, value, 4, HZ);

		if (errCode < 0) {
			err("%s failed: error %d", __FUNCTION__, errCode);
			return errCode;
		}
		usbvision->curwidth = usbvision->stretch_width * UsbWidth;
		usbvision->curheight = usbvision->stretch_height * UsbHeight;
	}

	if (usbvision->isocMode == ISOC_MODE_YUV422) {
		frameRate = (usbvision->isocPacketSize * 1000) / (UsbWidth * UsbHeight * 2);
	}
	else if (usbvision->isocMode == ISOC_MODE_YUV420) {
		frameRate = (usbvision->isocPacketSize * 1000) / ((UsbWidth * UsbHeight * 12) / 8);
	}
	else {
		frameRate = FRAMERATE_MAX;
	}

	if (usbvision->tvnorm->id & V4L2_STD_625_50) {
		frameDrop = frameRate * 32 / 25 - 1;
	}
	else if (usbvision->tvnorm->id & V4L2_STD_525_60) {
		frameDrop = frameRate * 32 / 30 - 1;
	}

	RESTRICT_TO_RANGE(frameDrop, FRAMERATE_MIN, FRAMERATE_MAX);

	PDEBUG(DBG_FUNC, "frameRate %d fps, frameDrop %d", frameRate, frameDrop);

	frameDrop = FRAMERATE_MAX; 	// We can allow the maximum here, because dropping is controlled

	/* frameDrop = 7; => framePhase = 1, 5, 9, 13, 17, 21, 25, 0, 4, 8, ...
		=> frameSkip = 4;
		=> frameRate = (7 + 1) * 25 / 32 = 200 / 32 = 6.25;

	   frameDrop = 9; => framePhase = 1, 5, 8, 11, 14, 17, 21, 24, 27, 1, 4, 8, ...
	    => frameSkip = 4, 3, 3, 3, 3, 4, 3, 3, 3, 3, 4, ...
		=> frameRate = (9 + 1) * 25 / 32 = 250 / 32 = 7.8125;
	*/
	errCode = usbvision_write_reg(usbvision, USBVISION_FRM_RATE, frameDrop);
	return errCode;
}


/*
 * usbvision_empty_framequeues()
 * prepare queues for incoming and outgoing frames
 */
static void usbvision_empty_framequeues(struct usb_usbvision *usbvision)
{
	u32 i;

	INIT_LIST_HEAD(&(usbvision->inqueue));
	INIT_LIST_HEAD(&(usbvision->outqueue));

	for (i = 0; i < USBVISION_NUMFRAMES; i++) {
		usbvision->frame[i].grabstate = FrameState_Unused;
		usbvision->frame[i].bytes_read = 0;
	}
}

/*
 * usbvision_stream_interrupt()
 * stops streaming
 */
static int usbvision_stream_interrupt(struct usb_usbvision *usbvision)
{
	int ret = 0;

	/* stop reading from the device */

	usbvision->streaming = Stream_Interrupt;
	ret = wait_event_timeout(usbvision->wait_stream,
				 (usbvision->streaming == Stream_Idle),
				 msecs_to_jiffies(USBVISION_NUMSBUF*USBVISION_URB_FRAMES));
	return ret;
}

/*
 * usbvision_set_compress_params()
 *
 */

static int usbvision_set_compress_params(struct usb_usbvision *usbvision)
{
	static const char proc[] = "usbvision_set_compresion_params: ";
	int rc;
	unsigned char value[6];

	value[0] = 0x0F;    // Intra-Compression cycle
	value[1] = 0x01;    // Reg.45 one line per strip
	value[2] = 0x00;    // Reg.46 Force intra mode on all new frames
	value[3] = 0x00;    // Reg.47 FORCE_UP <- 0 normal operation (not force)
	value[4] = 0xA2;    // Reg.48 BUF_THR I'm not sure if this does something in not compressed mode.
	value[5] = 0x00;    // Reg.49 DVI_YUV This has nothing to do with compression

	//catched values for NT1004
	// value[0] = 0xFF; // Never apply intra mode automatically
	// value[1] = 0xF1; // Use full frame height for virtual strip width; One line per strip
	// value[2] = 0x01; // Force intra mode on all new frames
	// value[3] = 0x00; // Strip size 400 Bytes; do not force up
	// value[4] = 0xA2; //
	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return 0;

	rc = usb_control_msg(usbvision->dev, usb_sndctrlpipe(usbvision->dev, 1),
			     USBVISION_OP_CODE,
			     USB_DIR_OUT | USB_TYPE_VENDOR |
			     USB_RECIP_ENDPOINT, 0,
			     (__u16) USBVISION_INTRA_CYC, value, 5, HZ);

	if (rc < 0) {
		printk(KERN_ERR "%sERROR=%d. USBVISION stopped - "
		       "reconnect or reload driver.\n", proc, rc);
		return rc;
	}

	if (usbvision->bridgeType == BRIDGE_NT1004) {
		value[0] =  20; // PCM Threshold 1
		value[1] =  12; // PCM Threshold 2
		value[2] = 255; // Distorsion Threshold inter
		value[3] = 255; // Distorsion Threshold intra
		value[4] =  43; // Max Distorsion inter
		value[5] =  43; // Max Distorsion intra
	}
	else {
		value[0] =  20; // PCM Threshold 1
		value[1] =  12; // PCM Threshold 2
		value[2] = 255; // Distorsion Threshold d7-d0
		value[3] =   0; // Distorsion Threshold d11-d8
		value[4] =  43; // Max Distorsion d7-d0
		value[5] =   0; // Max Distorsion d8
	}

	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return 0;

	rc = usb_control_msg(usbvision->dev, usb_sndctrlpipe(usbvision->dev, 1),
			     USBVISION_OP_CODE,
			     USB_DIR_OUT | USB_TYPE_VENDOR |
			     USB_RECIP_ENDPOINT, 0,
			     (__u16) USBVISION_PCM_THR1, value, 6, HZ);

	if (rc < 0) {
		printk(KERN_ERR "%sERROR=%d. USBVISION stopped - "
		       "reconnect or reload driver.\n", proc, rc);
		return rc;
	}


	return rc;
}


/*
 * usbvision_set_input()
 *
 * Set the input (saa711x, ...) size x y and other misc input params
 * I've no idea if this parameters are right
 *
 */
static int usbvision_set_input(struct usb_usbvision *usbvision)
{
	static const char proc[] = "usbvision_set_input: ";
	int rc;
	unsigned char value[8];
	unsigned char dvi_yuv_value;

	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return 0;

	/* Set input format expected from decoder*/
	if (usbvision_device_data[usbvision->DevModel].Vin_Reg1 >= 0) {
		value[0] = usbvision_device_data[usbvision->DevModel].Vin_Reg1 & 0xff;
	} else if(usbvision_device_data[usbvision->DevModel].Codec == CODEC_SAA7113) {
		/* SAA7113 uses 8 bit output */
		value[0] = USBVISION_8_422_SYNC;
	} else {
		/* I'm sure only about d2-d0 [010] 16 bit 4:2:2 usin sync pulses
		 * as that is how saa7111 is configured */
		value[0] = USBVISION_16_422_SYNC;
		/* | USBVISION_VSNC_POL | USBVISION_VCLK_POL);*/
	}

	rc = usbvision_write_reg(usbvision, USBVISION_VIN_REG1, value[0]);
	if (rc < 0) {
		printk(KERN_ERR "%sERROR=%d. USBVISION stopped - "
		       "reconnect or reload driver.\n", proc, rc);
		return rc;
	}


	if (usbvision->tvnorm->id & V4L2_STD_PAL) {
		value[0] = 0xC0;
		value[1] = 0x02;	//0x02C0 -> 704 Input video line length
		value[2] = 0x20;
		value[3] = 0x01;	//0x0120 -> 288 Input video n. of lines
		value[4] = 0x60;
		value[5] = 0x00;	//0x0060 -> 96 Input video h offset
		value[6] = 0x16;
		value[7] = 0x00;	//0x0016 -> 22 Input video v offset
	} else if (usbvision->tvnorm->id & V4L2_STD_SECAM) {
		value[0] = 0xC0;
		value[1] = 0x02;	//0x02C0 -> 704 Input video line length
		value[2] = 0x20;
		value[3] = 0x01;	//0x0120 -> 288 Input video n. of lines
		value[4] = 0x01;
		value[5] = 0x00;	//0x0001 -> 01 Input video h offset
		value[6] = 0x01;
		value[7] = 0x00;	//0x0001 -> 01 Input video v offset
	} else {	/* V4L2_STD_NTSC */
		value[0] = 0xD0;
		value[1] = 0x02;	//0x02D0 -> 720 Input video line length
		value[2] = 0xF0;
		value[3] = 0x00;	//0x00F0 -> 240 Input video number of lines
		value[4] = 0x50;
		value[5] = 0x00;	//0x0050 -> 80 Input video h offset
		value[6] = 0x10;
		value[7] = 0x00;	//0x0010 -> 16 Input video v offset
	}

	if (usbvision_device_data[usbvision->DevModel].X_Offset >= 0) {
		value[4]=usbvision_device_data[usbvision->DevModel].X_Offset & 0xff;
		value[5]=(usbvision_device_data[usbvision->DevModel].X_Offset & 0x0300) >> 8;
	}

	if (usbvision_device_data[usbvision->DevModel].Y_Offset >= 0) {
		value[6]=usbvision_device_data[usbvision->DevModel].Y_Offset & 0xff;
		value[7]=(usbvision_device_data[usbvision->DevModel].Y_Offset & 0x0300) >> 8;
	}

	rc = usb_control_msg(usbvision->dev, usb_sndctrlpipe(usbvision->dev, 1),
			     USBVISION_OP_CODE,	/* USBVISION specific code */
			     USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT, 0,
			     (__u16) USBVISION_LXSIZE_I, value, 8, HZ);
	if (rc < 0) {
		printk(KERN_ERR "%sERROR=%d. USBVISION stopped - "
		       "reconnect or reload driver.\n", proc, rc);
		return rc;
	}


	dvi_yuv_value = 0x00;	/* U comes after V, Ya comes after U/V, Yb comes after Yb */

	if(usbvision_device_data[usbvision->DevModel].Dvi_yuv >= 0){
		dvi_yuv_value = usbvision_device_data[usbvision->DevModel].Dvi_yuv & 0xff;
	}
	else if(usbvision_device_data[usbvision->DevModel].Codec == CODEC_SAA7113) {
	/* This changes as the fine sync control changes. Further investigation necessary */
		dvi_yuv_value = 0x06;
	}

	return (usbvision_write_reg(usbvision, USBVISION_DVI_YUV, dvi_yuv_value));
}


/*
 * usbvision_set_dram_settings()
 *
 * Set the buffer address needed by the usbvision dram to operate
 * This values has been taken with usbsnoop.
 *
 */

static int usbvision_set_dram_settings(struct usb_usbvision *usbvision)
{
	int rc;
	unsigned char value[8];

	if (usbvision->isocMode == ISOC_MODE_COMPRESS) {
		value[0] = 0x42;
		value[1] = 0x71;
		value[2] = 0xff;
		value[3] = 0x00;
		value[4] = 0x98;
		value[5] = 0xe0;
		value[6] = 0x71;
		value[7] = 0xff;
		// UR:  0x0E200-0x3FFFF = 204288 Words (1 Word = 2 Byte)
		// FDL: 0x00000-0x0E099 =  57498 Words
		// VDW: 0x0E3FF-0x3FFFF
	}
	else {
		value[0] = 0x42;
		value[1] = 0x00;
		value[2] = 0xff;
		value[3] = 0x00;
		value[4] = 0x00;
		value[5] = 0x00;
		value[6] = 0x00;
		value[7] = 0xff;
	}
	/* These are the values of the address of the video buffer,
	 * they have to be loaded into the USBVISION_DRM_PRM1-8
	 *
	 * Start address of video output buffer for read: 	drm_prm1-2 -> 0x00000
	 * End address of video output buffer for read: 	drm_prm1-3 -> 0x1ffff
	 * Start address of video frame delay buffer: 		drm_prm1-4 -> 0x20000
	 *    Only used in compressed mode
	 * End address of video frame delay buffer: 		drm_prm1-5-6 -> 0x3ffff
	 *    Only used in compressed mode
	 * Start address of video output buffer for write: 	drm_prm1-7 -> 0x00000
	 * End address of video output buffer for write: 	drm_prm1-8 -> 0x1ffff
	 */

	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return 0;

	rc = usb_control_msg(usbvision->dev, usb_sndctrlpipe(usbvision->dev, 1),
			     USBVISION_OP_CODE,	/* USBVISION specific code */
			     USB_DIR_OUT | USB_TYPE_VENDOR |
			     USB_RECIP_ENDPOINT, 0,
			     (__u16) USBVISION_DRM_PRM1, value, 8, HZ);

	if (rc < 0) {
		err("%sERROR=%d", __FUNCTION__, rc);
		return rc;
	}

	/* Restart the video buffer logic */
	if ((rc = usbvision_write_reg(usbvision, USBVISION_DRM_CONT, USBVISION_RES_UR |
				   USBVISION_RES_FDL | USBVISION_RES_VDW)) < 0)
		return rc;
	rc = usbvision_write_reg(usbvision, USBVISION_DRM_CONT, 0x00);

	return rc;
}

/*
 * ()
 *
 * Power on the device, enables suspend-resume logic
 * &  reset the isoc End-Point
 *
 */

static int usbvision_power_on(struct usb_usbvision *usbvision)
{
	int errCode = 0;

	PDEBUG(DBG_FUNC, "");

	usbvision_write_reg(usbvision, USBVISION_PWR_REG, USBVISION_SSPND_EN);
	usbvision_write_reg(usbvision, USBVISION_PWR_REG,
			 USBVISION_SSPND_EN | USBVISION_RES2);
	usbvision_write_reg(usbvision, USBVISION_PWR_REG,
			 USBVISION_SSPND_EN | USBVISION_PWR_VID);
	errCode = usbvision_write_reg(usbvision, USBVISION_PWR_REG,
						USBVISION_SSPND_EN | USBVISION_PWR_VID | USBVISION_RES2);
	if (errCode == 1) {
		usbvision->power = 1;
	}
	PDEBUG(DBG_FUNC, "%s: errCode %d", (errCode<0)?"ERROR":"power is on", errCode);
	return errCode;
}


static void usbvision_powerOffTimer(unsigned long data)
{
	struct usb_usbvision *usbvision = (void *) data;

	PDEBUG(DBG_FUNC, "");
	del_timer(&usbvision->powerOffTimer);
	INIT_WORK(&usbvision->powerOffWork, call_usbvision_power_off, usbvision);
	(void) schedule_work(&usbvision->powerOffWork);

}


/*
 * usbvision_begin_streaming()
 * Sure you have to put bit 7 to 0, if not incoming frames are droped, but no
 * idea about the rest
 */
static int usbvision_begin_streaming(struct usb_usbvision *usbvision)
{
	int errCode = 0;

	if (usbvision->isocMode == ISOC_MODE_COMPRESS) {
		usbvision_init_compression(usbvision);
	}
	errCode = usbvision_write_reg(usbvision, USBVISION_VIN_REG2, USBVISION_NOHVALID |
										usbvision->Vin_Reg2_Preset);
	return errCode;
}

/*
 * usbvision_restart_isoc()
 * Not sure yet if touching here PWR_REG make loose the config
 */

static int usbvision_restart_isoc(struct usb_usbvision *usbvision)
{
	int ret;

	if (
	    (ret =
	     usbvision_write_reg(usbvision, USBVISION_PWR_REG,
			      USBVISION_SSPND_EN | USBVISION_PWR_VID)) < 0)
		return ret;
	if (
	    (ret =
	     usbvision_write_reg(usbvision, USBVISION_PWR_REG,
			      USBVISION_SSPND_EN | USBVISION_PWR_VID |
			      USBVISION_RES2)) < 0)
		return ret;
	if (
	    (ret =
	     usbvision_write_reg(usbvision, USBVISION_VIN_REG2,
			      USBVISION_KEEP_BLANK | USBVISION_NOHVALID |
				  usbvision->Vin_Reg2_Preset)) < 0) return ret;

	/* TODO: schedule timeout */
	while ((usbvision_read_reg(usbvision, USBVISION_STATUS_REG) && 0x01) != 1);

	return 0;
}

static int usbvision_audio_on(struct usb_usbvision *usbvision)
{
	if (usbvision_write_reg(usbvision, USBVISION_IOPIN_REG, usbvision->AudioChannel) < 0) {
		printk(KERN_ERR "usbvision_audio_on: can't wirte reg\n");
		return -1;
	}
	DEBUG(printk(KERN_DEBUG "usbvision_audio_on: channel %d\n", usbvision->AudioChannel));
	usbvision->AudioMute = 0;
	return 0;
}

static int usbvision_audio_mute(struct usb_usbvision *usbvision)
{
	if (usbvision_write_reg(usbvision, USBVISION_IOPIN_REG, 0x03) < 0) {
		printk(KERN_ERR "usbvision_audio_mute: can't wirte reg\n");
		return -1;
	}
	DEBUG(printk(KERN_DEBUG "usbvision_audio_mute: audio mute\n"));
	usbvision->AudioMute = 1;
	return 0;
}

static int usbvision_audio_off(struct usb_usbvision *usbvision)
{
	if (usbvision_write_reg(usbvision, USBVISION_IOPIN_REG, USBVISION_AUDIO_MUTE) < 0) {
		printk(KERN_ERR "usbvision_audio_off: can't wirte reg\n");
		return -1;
	}
	DEBUG(printk(KERN_DEBUG "usbvision_audio_off: audio off\n"));
	usbvision->AudioMute = 0;
	usbvision->AudioChannel = USBVISION_AUDIO_MUTE;
	return 0;
}

static int usbvision_set_audio(struct usb_usbvision *usbvision, int AudioChannel)
{
	if (!usbvision->AudioMute) {
		if (usbvision_write_reg(usbvision, USBVISION_IOPIN_REG, AudioChannel) < 0) {
			printk(KERN_ERR "usbvision_set_audio: can't write iopin register for audio switching\n");
			return -1;
		}
	}
	DEBUG(printk(KERN_DEBUG "usbvision_set_audio: channel %d\n", AudioChannel));
	usbvision->AudioChannel = AudioChannel;
	return 0;
}

static int usbvision_setup(struct usb_usbvision *usbvision)
{
	usbvision_set_video_format(usbvision, isocMode);
	usbvision_set_dram_settings(usbvision);
	usbvision_set_compress_params(usbvision);
	usbvision_set_input(usbvision);
	usbvision_set_output(usbvision, MAX_USB_WIDTH, MAX_USB_HEIGHT);
	usbvision_restart_isoc(usbvision);

	/* cosas del PCM */
	return USBVISION_IS_OPERATIONAL(usbvision);
}


/*
 * usbvision_init_isoc()
 *
 */
static int usbvision_init_isoc(struct usb_usbvision *usbvision)
{
	struct usb_device *dev = usbvision->dev;
	int bufIdx, errCode, regValue;

	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return -EFAULT;

	usbvision->curFrame = NULL;
	scratch_reset(usbvision);

	/* Alternate interface 1 is is the biggest frame size */
	errCode = usb_set_interface(dev, usbvision->iface, usbvision->ifaceAltActive);
	if (errCode < 0) {
		usbvision->last_error = errCode;
		return -EBUSY;
	}

	regValue = (16 - usbvision_read_reg(usbvision, USBVISION_ALTER_REG)) & 0x0F;
	usbvision->isocPacketSize = (regValue == 0) ? 0 : (regValue * 64) - 1;
	PDEBUG(DBG_ISOC, "ISO Packet Length:%d", usbvision->isocPacketSize);

	usbvision->usb_bandwidth = regValue >> 1;
	PDEBUG(DBG_ISOC, "USB Bandwidth Usage: %dMbit/Sec", usbvision->usb_bandwidth);



	/* We double buffer the Iso lists */

	for (bufIdx = 0; bufIdx < USBVISION_NUMSBUF; bufIdx++) {
		int j, k;
		struct urb *urb;

		#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
			urb = usb_alloc_urb(USBVISION_URB_FRAMES);
		#else
			urb = usb_alloc_urb(USBVISION_URB_FRAMES, GFP_KERNEL);
		#endif
		if (urb == NULL) {
			err("%s: usb_alloc_urb() failed", __FUNCTION__);
			return -ENOMEM;
		}
		usbvision->sbuf[bufIdx].urb = urb;
		urb->dev = dev;
		urb->context = usbvision;
		urb->pipe = usb_rcvisocpipe(dev, usbvision->video_endp);
	#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
		urb->transfer_flags = USB_ISO_ASAP;
	#else
		urb->transfer_flags = URB_ISO_ASAP;
		urb->interval = 1;
	#endif
		urb->transfer_buffer = usbvision->sbuf[bufIdx].data;
		urb->complete = usbvision_isocIrq;
		urb->number_of_packets = USBVISION_URB_FRAMES;
		urb->transfer_buffer_length =
		    usbvision->isocPacketSize * USBVISION_URB_FRAMES;
		for (j = k = 0; j < USBVISION_URB_FRAMES; j++,
		     k += usbvision->isocPacketSize) {
			urb->iso_frame_desc[j].offset = k;
			urb->iso_frame_desc[j].length = usbvision->isocPacketSize;
		}
	}


	/* Submit all URBs */
	for (bufIdx = 0; bufIdx < USBVISION_NUMSBUF; bufIdx++) {
		#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
			errCode = usb_submit_urb(usbvision->sbuf[bufIdx].urb);
		#else
			errCode = usb_submit_urb(usbvision->sbuf[bufIdx].urb, GFP_KERNEL);
		#endif
		if (errCode) {
			err("%s: usb_submit_urb(%d) failed: error %d", __FUNCTION__, bufIdx, errCode);
		}
	}

	usbvision->streaming = Stream_Idle;
	PDEBUG(DBG_ISOC, "%s: streaming=1 usbvision->video_endp=$%02x", __FUNCTION__, usbvision->video_endp);
	return 0;
}

/*
 * usbvision_stop_isoc()
 *
 * This procedure stops streaming and deallocates URBs. Then it
 * activates zero-bandwidth alt. setting of the video interface.
 *
 */
static void usbvision_stop_isoc(struct usb_usbvision *usbvision)
{
	int bufIdx, errCode, regValue;

	if ((usbvision->streaming == Stream_Off) || (usbvision->dev == NULL))
		return;

	/* Unschedule all of the iso td's */
	for (bufIdx = 0; bufIdx < USBVISION_NUMSBUF; bufIdx++) {
		usb_kill_urb(usbvision->sbuf[bufIdx].urb);
		usb_free_urb(usbvision->sbuf[bufIdx].urb);
		usbvision->sbuf[bufIdx].urb = NULL;
	}


	PDEBUG(DBG_ISOC, "%s: streaming=Stream_Off\n", __FUNCTION__);
	usbvision->streaming = Stream_Off;

	if (!usbvision->remove_pending) {

		/* Set packet size to 0 */
		errCode = usb_set_interface(usbvision->dev, usbvision->iface,
				      usbvision->ifaceAltInactive);
		if (errCode < 0) {
			err("%s: usb_set_interface() failed: error %d", __FUNCTION__, errCode);
			usbvision->last_error = errCode;
		}
		regValue = (16 - usbvision_read_reg(usbvision, USBVISION_ALTER_REG)) & 0x0F;
		usbvision->isocPacketSize = (regValue == 0) ? 0 : (regValue * 64) - 1;
		PDEBUG(DBG_ISOC, "ISO Packet Length:%d", usbvision->isocPacketSize);

		usbvision->usb_bandwidth = regValue >> 1;
		PDEBUG(DBG_ISOC, "USB Bandwidth Usage: %dMbit/Sec", usbvision->usb_bandwidth);
	}
}

static int usbvision_muxsel(struct usb_usbvision *usbvision, int channel)
{
	int mode[4];
	int audio[]= {1, 0, 0, 0};
	struct v4l2_routing route;
	//channel 0 is TV with audiochannel 1 (tuner mono)
	//channel 1 is Composite with audio channel 0 (line in)
	//channel 2 is S-Video with audio channel 0 (line in)
	//channel 3 is additional video inputs to the device with audio channel 0 (line in)

	RESTRICT_TO_RANGE(channel, 0, usbvision->video_inputs);
	usbvision->ctl_input = channel;
	  route.input = SAA7115_COMPOSITE1;
	  call_i2c_clients(usbvision, VIDIOC_INT_S_VIDEO_ROUTING,&route);
	  call_i2c_clients(usbvision, VIDIOC_S_INPUT, &usbvision->ctl_input);

	// set the new channel
	// Regular USB TV Tuners -> channel: 0 = Television, 1 = Composite, 2 = S-Video
	// Four video input devices -> channel: 0 = Chan White, 1 = Chan Green, 2 = Chan Yellow, 3 = Chan Red

	switch (usbvision_device_data[usbvision->DevModel].Codec) {
		case CODEC_SAA7113:
			if (SwitchSVideoInput) { // To handle problems with S-Video Input for some devices.  Use SwitchSVideoInput parameter when loading the module.
				mode[2] = 1;
			}
			else {
				mode[2] = 7;
			}
			if (usbvision_device_data[usbvision->DevModel].VideoChannels == 4) {
				mode[0] = 0; mode[1] = 2; mode[3] = 3;  // Special for four input devices
			}
			else {
				mode[0] = 0; mode[1] = 2; //modes for regular saa7113 devices
			}
			break;
		case CODEC_SAA7111:
			mode[0] = 0; mode[1] = 1; mode[2] = 7; //modes for saa7111
			break;
		default:
			mode[0] = 0; mode[1] = 1; mode[2] = 7; //default modes
	}
	route.input = mode[channel];
	call_i2c_clients(usbvision, VIDIOC_INT_S_VIDEO_ROUTING,&route);
	usbvision->channel = channel;
	usbvision_set_audio(usbvision, audio[channel]);
	return 0;
}


/*
 * usbvision_open()
 *
 * This is part of Video 4 Linux API. The driver can be opened by one
 * client only (checks internal counter 'usbvision->user'). The procedure
 * then allocates buffers needed for video processing.
 *
 */
static int usbvision_v4l2_open(struct inode *inode, struct file *file)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision = (struct usb_usbvision *) video_get_drvdata(dev);
	const int sb_size = USBVISION_URB_FRAMES * USBVISION_MAX_ISOC_PACKET_SIZE;
	int i, errCode = 0;

	PDEBUG(DBG_IO, "open");


	if (timer_pending(&usbvision->powerOffTimer)) {
		del_timer(&usbvision->powerOffTimer);
	}

	if (usbvision->user)
		errCode = -EBUSY;
	else {
		/* Clean pointers so we know if we allocated something */
		for (i = 0; i < USBVISION_NUMSBUF; i++)
			usbvision->sbuf[i].data = NULL;

		/* Allocate memory for the frame buffers */
		usbvision->max_frame_size = MAX_FRAME_SIZE;
		usbvision->fbuf_size = USBVISION_NUMFRAMES * usbvision->max_frame_size;
		usbvision->fbuf = usbvision_rvmalloc(usbvision->fbuf_size);
		usbvision->scratch = vmalloc(scratch_buf_size);
		scratch_reset(usbvision);
		if ((usbvision->fbuf == NULL) || (usbvision->scratch == NULL)) {
			err("%s: unable to allocate %d bytes for fbuf and %d bytes for scratch",
					__FUNCTION__, usbvision->fbuf_size, scratch_buf_size);
			errCode = -ENOMEM;
		}
		else {
			spin_lock_init(&usbvision->queue_lock);
			init_waitqueue_head(&usbvision->wait_frame);
			init_waitqueue_head(&usbvision->wait_stream);

			/* Allocate all buffers */
			for (i = 0; i < USBVISION_NUMFRAMES; i++) {
				usbvision->frame[i].index = i;
				usbvision->frame[i].grabstate = FrameState_Unused;
				usbvision->frame[i].data = usbvision->fbuf +
				    i * MAX_FRAME_SIZE;
				/*
				 * Set default sizes in case IOCTL
				 * (VIDIOCMCAPTURE)
				 * is not used (using read() instead).
				 */
				usbvision->stretch_width = 1;
				usbvision->stretch_height = 1;
				usbvision->frame[i].width = usbvision->curwidth;
				usbvision->frame[i].height = usbvision->curheight;
				usbvision->frame[i].bytes_read = 0;
			}
			if (dga) { //set default for DGA
				usbvision->overlay_frame.grabstate = FrameState_Unused;
				usbvision->overlay_frame.scanstate = ScanState_Scanning;
				usbvision->overlay_frame.data = NULL;
				usbvision->overlay_frame.width = usbvision->curwidth;
				usbvision->overlay_frame.height = usbvision->curheight;
				usbvision->overlay_frame.bytes_read = 0;
			}
			for (i = 0; i < USBVISION_NUMSBUF; i++) {
				usbvision->sbuf[i].data = kzalloc(sb_size, GFP_KERNEL);
				if (usbvision->sbuf[i].data == NULL) {
					err("%s: unable to allocate %d bytes for sbuf", __FUNCTION__, sb_size);
					errCode = -ENOMEM;
					break;
				}
			}
		}
		if ((!errCode) && (usbvision->isocMode==ISOC_MODE_COMPRESS)) {
			int IFB_size = MAX_FRAME_WIDTH * MAX_FRAME_HEIGHT * 3 / 2;
			usbvision->IntraFrameBuffer = vmalloc(IFB_size);
			if (usbvision->IntraFrameBuffer == NULL) {
				err("%s: unable to allocate %d for compr. frame buffer", __FUNCTION__, IFB_size);
				errCode = -ENOMEM;
			}

		}
		if (errCode) {
			/* Have to free all that memory */
			if (usbvision->fbuf != NULL) {
				usbvision_rvfree(usbvision->fbuf, usbvision->fbuf_size);
				usbvision->fbuf = NULL;
			}
			if (usbvision->scratch != NULL) {
				vfree(usbvision->scratch);
				usbvision->scratch = NULL;
			}
			for (i = 0; i < USBVISION_NUMSBUF; i++) {
				if (usbvision->sbuf[i].data != NULL) {
					kfree(usbvision->sbuf[i].data);
					usbvision->sbuf[i].data = NULL;
				}
			}
			if (usbvision->IntraFrameBuffer != NULL) {
				vfree(usbvision->IntraFrameBuffer);
				usbvision->IntraFrameBuffer = NULL;
			}
		}
	}

	/* If so far no errors then we shall start the camera */
	if (!errCode) {
		down(&usbvision->lock);
		if (usbvision->power == 0) {
			usbvision_power_on(usbvision);
			usbvision_init_i2c(usbvision);
		}

		/* Send init sequence only once, it's large! */
		if (!usbvision->initialized) {
			int setup_ok = 0;
			setup_ok = usbvision_setup(usbvision);
			if (setup_ok)
				usbvision->initialized = 1;
			else
				errCode = -EBUSY;
		}

		if (!errCode) {
			usbvision_begin_streaming(usbvision);
			errCode = usbvision_init_isoc(usbvision);
			/* device needs to be initialized before isoc transfer */
			usbvision_muxsel(usbvision,0);
			usbvision->user++;
		}
		else {
			if (PowerOnAtOpen) {
				usbvision_i2c_usb_del_bus(&usbvision->i2c_adap);
				usbvision_power_off(usbvision);
				usbvision->initialized = 0;
			}
		}
		up(&usbvision->lock);
	}

	if (errCode) {
	}

	/* prepare queues */
	usbvision_empty_framequeues(usbvision);

	PDEBUG(DBG_IO, "success");
	return errCode;
}

/*
 * usbvision_v4l2_close()
 *
 * This is part of Video 4 Linux API. The procedure
 * stops streaming and deallocates all buffers that were earlier
 * allocated in usbvision_v4l2_open().
 *
 */
static int usbvision_v4l2_close(struct inode *inode, struct file *file)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision = (struct usb_usbvision *) video_get_drvdata(dev);
	int i;

	PDEBUG(DBG_IO, "close");
	down(&usbvision->lock);

	usbvision_audio_off(usbvision);
	usbvision_restart_isoc(usbvision);
	usbvision_stop_isoc(usbvision);

	if (usbvision->IntraFrameBuffer != NULL) {
		vfree(usbvision->IntraFrameBuffer);
		usbvision->IntraFrameBuffer = NULL;
	}

	usbvision_rvfree(usbvision->fbuf, usbvision->fbuf_size);
	vfree(usbvision->scratch);
	for (i = 0; i < USBVISION_NUMSBUF; i++)
		kfree(usbvision->sbuf[i].data);

	usbvision->user--;

	if (PowerOnAtOpen) {
		mod_timer(&usbvision->powerOffTimer, jiffies + USBVISION_POWEROFF_TIME);
		usbvision->initialized = 0;
	}

	up(&usbvision->lock);

	if (usbvision->remove_pending) {
		info("%s: Final disconnect", __FUNCTION__);
		usbvision_release(usbvision);
	}

	PDEBUG(DBG_IO, "success");


	return 0;
}


/*
 * usbvision_ioctl()
 *
 * This is part of Video 4 Linux API. The procedure handles ioctl() calls.
 *
 */
static int usbvision_v4l2_do_ioctl(struct inode *inode, struct file *file,
				 unsigned int cmd, void *arg)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision = (struct usb_usbvision *) video_get_drvdata(dev);

	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return -EFAULT;

	//	if (debug & DBG_IOCTL) v4l_printk_ioctl(cmd);

	switch (cmd) {

#ifdef CONFIG_VIDEO_ADV_DEBUG
		/* ioctls to allow direct acces to the NT100x registers */
		case VIDIOC_INT_G_REGISTER:
		{
			struct v4l2_register *reg = arg;
			int errCode;

			if (reg->i2c_id != 0)
				return -EINVAL;
			/* NT100x has a 8-bit register space */
			errCode = usbvision_read_reg(usbvision, reg->reg&0xff);
			if (errCode < 0) {
				err("%s: VIDIOC_INT_G_REGISTER failed: error %d", __FUNCTION__, errCode);
			}
			else {
				reg->val=(unsigned char)errCode;
				PDEBUG(DBG_IOCTL, "VIDIOC_INT_G_REGISTER reg=0x%02X, value=0x%02X",
							(unsigned int)reg->reg, reg->val);
				errCode = 0; // No error
			}
			return errCode;
		}
		case VIDIOC_INT_S_REGISTER:
		{
			struct v4l2_register *reg = arg;
			int errCode;

			if (reg->i2c_id != 0)
				return -EINVAL;
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
			errCode = usbvision_write_reg(usbvision, reg->reg&0xff, reg->val);
			if (errCode < 0) {
				err("%s: VIDIOC_INT_S_REGISTER failed: error %d", __FUNCTION__, errCode);
			}
			else {
				PDEBUG(DBG_IOCTL, "VIDIOC_INT_S_REGISTER reg=0x%02X, value=0x%02X",
							(unsigned int)reg->reg, reg->val);
				errCode = 0;
			}
			return 0;
		}
#endif
		case VIDIOC_QUERYCAP:
		{
			struct v4l2_capability *vc=arg;

			memset(vc, 0, sizeof(*vc));
			strlcpy(vc->driver, "USBVision", sizeof(vc->driver));
			strlcpy(vc->card, usbvision_device_data[usbvision->DevModel].ModelString,
				sizeof(vc->card));
			strlcpy(vc->bus_info, usbvision->dev->dev.bus_id,
				sizeof(vc->bus_info));
			vc->version = USBVISION_DRIVER_VERSION;
			vc->capabilities = V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_AUDIO |
				V4L2_CAP_READWRITE |
				V4L2_CAP_STREAMING |
				(dga ? (V4L2_FBUF_CAP_LIST_CLIPPING | V4L2_CAP_VIDEO_OVERLAY) : 0) |
				(usbvision->have_tuner ? V4L2_CAP_TUNER : 0);
			PDEBUG(DBG_IOCTL, "VIDIOC_QUERYCAP");
			return 0;
		}
		case VIDIOC_ENUMINPUT:
		{
			struct v4l2_input *vi = arg;
			int chan;

			if ((vi->index >= usbvision->video_inputs) || (vi->index < 0) )
				return -EINVAL;
			if (usbvision->have_tuner) {
				chan = vi->index;
			}
			else {
				chan = vi->index + 1; //skip Television string
			}
			switch(chan) {
				case 0:
					if (usbvision_device_data[usbvision->DevModel].VideoChannels == 4) {
						strcpy(vi->name, "White Video Input");
					}
					else {
						strcpy(vi->name, "Television");
						vi->type = V4L2_INPUT_TYPE_TUNER;
						vi->audioset = 1;
						vi->tuner = chan;
						vi->std = V4L2_STD_PAL | V4L2_STD_NTSC | V4L2_STD_SECAM;
					}
					break;
				case 1:
					vi->type = V4L2_INPUT_TYPE_CAMERA;
					if (usbvision_device_data[usbvision->DevModel].VideoChannels == 4) {
						strcpy(vi->name, "Green Video Input");
					}
					else {
						strcpy(vi->name, "Composite Video Input");
					}
					vi->std = V4L2_STD_PAL;
					break;
				case 2:
					vi->type = V4L2_INPUT_TYPE_CAMERA;
					if (usbvision_device_data[usbvision->DevModel].VideoChannels == 4) {
						strcpy(vi->name, "Yellow Video Input");
					}
					else {
					strcpy(vi->name, "S-Video Input");
					}
					vi->std = V4L2_STD_PAL;
					break;
				case 3:
					vi->type = V4L2_INPUT_TYPE_CAMERA;
					strcpy(vi->name, "Red Video Input");
					vi->std = V4L2_STD_PAL;
					break;
			}
			PDEBUG(DBG_IOCTL, "VIDIOC_ENUMINPUT name=%s:%d tuners=%d type=%d norm=%x", vi->name, vi->index, vi->tuner,vi->type,(int)vi->std);
			return 0;
		}
		case VIDIOC_ENUMSTD:
		{
			struct v4l2_standard *e = arg;
			unsigned int i;
			int ret;

			i = e->index;
			if (i >= TVNORMS)
				return -EINVAL;
			ret = v4l2_video_std_construct(e, tvnorms[e->index].id,
						       tvnorms[e->index].name);
			e->index = i;
			if (ret < 0)
				return ret;
			return 0;
		}
		case VIDIOC_G_INPUT:
		{
			int *input = arg;
			*input = usbvision->ctl_input;
			return 0;
		}
		case VIDIOC_S_INPUT:
		{
			int *input = arg;
			if ((*input >= usbvision->video_inputs) || (*input < 0) )
				return -EINVAL;
			usbvision->ctl_input = *input;

			down(&usbvision->lock);
			usbvision_muxsel(usbvision, usbvision->ctl_input);
			usbvision_set_input(usbvision);
			usbvision_set_output(usbvision, usbvision->curwidth, usbvision->curheight);
			up(&usbvision->lock);
			return 0;
		}
		case VIDIOC_G_STD:
		{
			v4l2_std_id *id = arg;

			*id = usbvision->tvnorm->id;

			PDEBUG(DBG_IOCTL, "VIDIOC_G_STD std_id=%s", usbvision->tvnorm->name);
			return 0;
		}
		case VIDIOC_S_STD:
		{
			v4l2_std_id *id = arg;
			unsigned int i;

			for (i = 0; i < TVNORMS; i++)
				if (*id == tvnorms[i].id)
					break;
			if (i == TVNORMS)
				for (i = 0; i < TVNORMS; i++)
					if (*id & tvnorms[i].id)
						break;
			if (i == TVNORMS)
				return -EINVAL;

			down(&usbvision->lock);
			usbvision->tvnorm = &tvnorms[i];

			call_i2c_clients(usbvision, VIDIOC_S_STD,
					 &usbvision->tvnorm->id);

			up(&usbvision->lock);

			PDEBUG(DBG_IOCTL, "VIDIOC_S_STD std_id=%s", usbvision->tvnorm->name);
			return 0;
		}
		case VIDIOC_G_TUNER:
		{
			struct v4l2_tuner *vt = arg;

			if (!usbvision->have_tuner || vt->index)	// Only tuner 0
				return -EINVAL;
			strcpy(vt->name, "Television");
			/* Let clients fill in the remainder of this struct */
			call_i2c_clients(usbvision,VIDIOC_G_TUNER,vt);

			PDEBUG(DBG_IOCTL, "VIDIOC_G_TUNER signal=%x, afc=%x",vt->signal,vt->afc);
			return 0;
		}
		case VIDIOC_S_TUNER:
		{
			struct v4l2_tuner *vt = arg;

			// Only no or one tuner for now
			if (!usbvision->have_tuner || vt->index)
				return -EINVAL;
			/* let clients handle this */
			call_i2c_clients(usbvision,VIDIOC_S_TUNER,vt);

			PDEBUG(DBG_IOCTL, "VIDIOC_S_TUNER");
			return 0;
		}
		case VIDIOC_G_FREQUENCY:
		{
			struct v4l2_frequency *freq = arg;

			freq->tuner = 0; // Only one tuner
			freq->type = V4L2_TUNER_ANALOG_TV;
			freq->frequency = usbvision->freq;
			PDEBUG(DBG_IOCTL, "VIDIOC_G_FREQUENCY freq=0x%X", (unsigned)freq->frequency);
			return 0;
		}
		case VIDIOC_S_FREQUENCY:
		{
			struct v4l2_frequency *freq = arg;

			// Only no or one tuner for now
			if (!usbvision->have_tuner || freq->tuner)
				return -EINVAL;

			usbvision->freq = freq->frequency;
			call_i2c_clients(usbvision, cmd, freq);
			PDEBUG(DBG_IOCTL, "VIDIOC_S_FREQUENCY freq=0x%X", (unsigned)freq->frequency);
			return 0;
		}
		case VIDIOC_G_AUDIO:
		{
			struct v4l2_audio *v = arg;
			memset(v,0, sizeof(v));
			strcpy(v->name, "TV");
			PDEBUG(DBG_IOCTL, "VIDIOC_G_AUDIO");
			// FIXME: no more processings ???
			return 0;
		}
		case VIDIOC_S_AUDIO:
		{
			struct v4l2_audio *v = arg;
			if(v->index) {
				return -EINVAL;
			}
			PDEBUG(DBG_IOCTL, "VIDIOC_S_AUDIO");
			// FIXME: void function ???
			return 0;
		}
		case VIDIOC_QUERYCTRL:
		{
			struct v4l2_queryctrl *ctrl = arg;
			int id=ctrl->id;

			memset(ctrl,0,sizeof(*ctrl));
			ctrl->id=id;

			call_i2c_clients(usbvision, cmd, arg);

			if (ctrl->type)
				return 0;
			else
				return -EINVAL;

			PDEBUG(DBG_IOCTL,"VIDIOC_QUERYCTRL id=%x value=%x",ctrl->id,ctrl->type);
		}
		case VIDIOC_G_CTRL:
		{
			struct v4l2_control *ctrl = arg;

			PDEBUG(DBG_IOCTL,"VIDIOC_G_CTRL id=%x value=%x",ctrl->id,ctrl->value);
			call_i2c_clients(usbvision, VIDIOC_G_CTRL, ctrl);
			return 0;
		}
		case VIDIOC_S_CTRL:
		{
			struct v4l2_control *ctrl = arg;

			PDEBUG(DBG_IOCTL, "VIDIOC_S_CTRL id=%x value=%x",ctrl->id,ctrl->value);
			call_i2c_clients(usbvision, VIDIOC_S_CTRL, ctrl);
			return 0;
		}
		case VIDIOC_REQBUFS:
		{
			struct v4l2_requestbuffers *vr = arg;
			int ret;

			RESTRICT_TO_RANGE(vr->count,1,USBVISION_NUMFRAMES);

			// Check input validity : the user must do a VIDEO CAPTURE and MMAP method.
			if((vr->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) ||
			   (vr->memory != V4L2_MEMORY_MMAP))
				return -EINVAL;

			// FIXME : before this, we must control if buffers are still mapped.
			// Then interrupt streaming if so...
			if(usbvision->streaming == Stream_On) {
				if ((ret = usbvision_stream_interrupt(usbvision)))
				    return ret;
			}

			usbvision_empty_framequeues(usbvision);

			usbvision->curFrame = NULL;

			PDEBUG(DBG_IOCTL, "VIDIOC_REQBUFS count=%d",vr->count);
			return 0;
		}
		case VIDIOC_QUERYBUF:
		{
			struct v4l2_buffer *vb = arg;
			struct usbvision_frame *frame;

			// FIXME : must control that buffers are mapped (VIDIOC_REQBUFS has been called)

			if(vb->type != V4L2_CAP_VIDEO_CAPTURE) {
				return -EINVAL;
			}
			if(vb->index>=USBVISION_NUMFRAMES)  {
				return -EINVAL;
			}
			// Updating the corresponding frame state
			vb->flags = 0;
			frame = &usbvision->frame[vb->index];
			if(frame->grabstate >= FrameState_Ready)
				vb->flags |= V4L2_BUF_FLAG_QUEUED;
			if(frame->grabstate >= FrameState_Done)
				vb->flags |= V4L2_BUF_FLAG_DONE;
			if(frame->grabstate == FrameState_Unused)
				vb->flags |= V4L2_BUF_FLAG_MAPPED;
			vb->memory = V4L2_MEMORY_MMAP;

			vb->m.offset = vb->index*MAX_FRAME_SIZE;

			vb->memory = V4L2_MEMORY_MMAP;
			vb->field = V4L2_FIELD_NONE;
			vb->length = MAX_FRAME_SIZE;
			vb->timestamp = usbvision->frame[vb->index].timestamp;
			vb->sequence = usbvision->frame[vb->index].sequence;
			return 0;
		}
		case VIDIOC_QBUF:
		{
			struct v4l2_buffer *vb = arg;
			struct usbvision_frame *frame;
			unsigned long lock_flags;

			// FIXME : works only on VIDEO_CAPTURE MODE, MMAP.
			if(vb->type != V4L2_CAP_VIDEO_CAPTURE) {
				return -EINVAL;
			}
			if(vb->index>=USBVISION_NUMFRAMES)  {
				return -EINVAL;
			}

			frame = &usbvision->frame[vb->index];

			if (frame->grabstate != FrameState_Unused) {
				return -EAGAIN;
			}

			/* Mark it as ready and enqueue frame */
			frame->grabstate = FrameState_Ready;
			frame->scanstate = ScanState_Scanning;
			frame->scanlength = 0;	/* Accumulated in usbvision_parse_data() */

			vb->flags &= ~V4L2_BUF_FLAG_DONE;

			/* set v4l2_format index */
			frame->v4l2_format = usbvision->palette;

			spin_lock_irqsave(&usbvision->queue_lock, lock_flags);
			list_add_tail(&usbvision->frame[vb->index].frame, &usbvision->inqueue);
			spin_unlock_irqrestore(&usbvision->queue_lock, lock_flags);

			PDEBUG(DBG_IOCTL, "VIDIOC_QBUF frame #%d",vb->index);
			return 0;
		}
		case VIDIOC_DQBUF:
		{
			struct v4l2_buffer *vb = arg;
			int ret;
			struct usbvision_frame *f;
			unsigned long lock_flags;

			if (vb->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
				return -EINVAL;

			if (list_empty(&(usbvision->outqueue))) {
				if (usbvision->streaming == Stream_Idle)
					return -EINVAL;
				ret = wait_event_interruptible
					(usbvision->wait_frame,
					 !list_empty(&(usbvision->outqueue)));
				if (ret)
					return ret;
			}

			spin_lock_irqsave(&usbvision->queue_lock, lock_flags);
			f = list_entry(usbvision->outqueue.next,
				       struct usbvision_frame, frame);
			list_del(usbvision->outqueue.next);
			spin_unlock_irqrestore(&usbvision->queue_lock, lock_flags);

			f->grabstate = FrameState_Unused;

			vb->memory = V4L2_MEMORY_MMAP;
			vb->flags = V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE;
			vb->index = f->index;
			vb->sequence = f->sequence;
			vb->timestamp = f->timestamp;
			vb->field = V4L2_FIELD_NONE;
			vb->bytesused = f->scanlength;

			if(debug & DBG_IOCTL) { // do not spend computing time for debug stuff if not needed !
				if(usbvision_counter == 100) {
					PDEBUG(DBG_IOCTL, "VIDIOC_DQBUF delta=%d",(unsigned)(jiffies-usbvision_timestamp));
					usbvision_counter = 0;
					usbvision_timestamp = jiffies;
				}
				else {
					usbvision_counter++;
				}
				PDEBUG(DBG_IOCTL, "VIDIOC_DQBUF frame #%d",vb->index);
			}
			return 0;
		}
		case VIDIOC_STREAMON:
		{
			int b=V4L2_BUF_TYPE_VIDEO_CAPTURE;

			usbvision->streaming = Stream_On;

			if(debug & DBG_IOCTL) usbvision_timestamp = jiffies;

			call_i2c_clients(usbvision,VIDIOC_STREAMON , &b);

			PDEBUG(DBG_IOCTL, "VIDIOC_STREAMON");

			return 0;
		}
		case VIDIOC_STREAMOFF:
		{
			int *type = arg;
			int b=V4L2_BUF_TYPE_VIDEO_CAPTURE;

			if (*type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
				return -EINVAL;

			if(usbvision->streaming == Stream_On) {
				usbvision_stream_interrupt(usbvision);
				// Stop all video streamings
				call_i2c_clients(usbvision,VIDIOC_STREAMOFF , &b);
			}
			usbvision_empty_framequeues(usbvision);

			PDEBUG(DBG_IOCTL, "VIDIOC_STREAMOFF");
			return 0;
		}
		case VIDIOC_G_FBUF:
		{
			struct v4l2_framebuffer *vb = arg;

			if (dga) {
				*vb = usbvision->vid_buf;
			}
			else {
				memset(vb, 0, sizeof(vb)); //dga not supported, not used
			}
			PDEBUG(DBG_IOCTL, "VIDIOC_G_FBUF base=%p, width=%d, height=%d, pixelformat=%d, bpl=%d",
			       vb->base, vb->fmt.width, vb->fmt.height, vb->fmt.pixelformat,vb->fmt.bytesperline);
			return 0;
		}
		case VIDIOC_S_FBUF:
		{
			struct v4l2_framebuffer *vb = arg;
			int formatIdx;

			if (dga == 0) {
				return -EINVAL;
			}

			if(!capable(CAP_SYS_ADMIN) && !capable(CAP_SYS_ADMIN)) {
				return -EPERM;
			}

			PDEBUG(DBG_IOCTL, "VIDIOC_S_FBUF base=%p, width=%d, height=%d, pixelformat=%d, bpl=%d",
			       vb->base, vb->fmt.width, vb->fmt.height, vb->fmt.pixelformat,vb->fmt.bytesperline);

			for (formatIdx=0; formatIdx <= USBVISION_SUPPORTED_PALETTES; formatIdx++) {
				if (formatIdx == USBVISION_SUPPORTED_PALETTES) {
					return -EINVAL; // no matching video_format
				}
				if ((vb->fmt.pixelformat == usbvision_v4l2_format[formatIdx].format) &&
				    (usbvision_v4l2_format[formatIdx].supported)) {
					break; //found matching video_format
				}
			}

			if (vb->fmt.bytesperline<1) {
				return -EINVAL;
			}
			if (usbvision->overlay) {
				return -EBUSY;
			}
			down(&usbvision->lock);
			if (usbvision->overlay_base) {
				iounmap(usbvision->overlay_base);
				usbvision->vid_buf_valid = 0;
			}
			usbvision->overlay_base = ioremap((ulong)vb->base, vb->fmt.height * vb->fmt.bytesperline);
			if (usbvision->overlay_base) {
				usbvision->vid_buf_valid = 1;
			}
			usbvision->vid_buf = *vb;
			usbvision->overlay_frame.v4l2_format = usbvision_v4l2_format[formatIdx];
			up(&usbvision->lock);
			return 0;
		}
		case VIDIOC_ENUM_FMT:
		{
			struct v4l2_fmtdesc *vfd = arg;

			if(vfd->index>=USBVISION_SUPPORTED_PALETTES-1) {
				return -EINVAL;
			}
			vfd->flags = 0;
			vfd->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			strcpy(vfd->description,usbvision_v4l2_format[vfd->index].desc);
			vfd->pixelformat = usbvision_v4l2_format[vfd->index].format;
			memset(vfd->reserved, 0, sizeof(vfd->reserved));
			return 0;
		}
		case VIDIOC_G_FMT:
		{
			struct v4l2_format *vf = arg;

			switch (vf->type) {
				case V4L2_BUF_TYPE_VIDEO_CAPTURE:
				{
					vf->fmt.pix.width = usbvision->curwidth;
					vf->fmt.pix.height = usbvision->curheight;
					vf->fmt.pix.pixelformat = usbvision->palette.format;
					vf->fmt.pix.bytesperline =  usbvision->curwidth*usbvision->palette.bytes_per_pixel;
					vf->fmt.pix.sizeimage = vf->fmt.pix.bytesperline*usbvision->curheight;
					vf->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
					vf->fmt.pix.field = V4L2_FIELD_NONE; /* Always progressive image */
				}
				return 0;
				default:
					PDEBUG(DBG_IOCTL, "VIDIOC_G_FMT invalid type %d",vf->type);
					return -EINVAL;
			}
			PDEBUG(DBG_IOCTL, "VIDIOC_G_FMT w=%d, h=%d",vf->fmt.win.w.width, vf->fmt.win.w.height);
			return 0;
		}
		case VIDIOC_TRY_FMT:
		case VIDIOC_S_FMT:
		{
			struct v4l2_format *vf = arg;
			struct v4l2_clip *vc=NULL;
			int on,formatIdx;

			switch(vf->type) {
				case V4L2_BUF_TYPE_VIDEO_OVERLAY:
				{
					if (vf->fmt.win.clipcount>256) {
						return -EDOM;   /* Too many clips! */
					}
					// Do every clips.
					vc = vmalloc(sizeof(struct v4l2_clip)*(vf->fmt.win.clipcount+4));
					if (vc == NULL) {
						return -ENOMEM;
					}
					if (vf->fmt.win.clipcount && copy_from_user(vc,vf->fmt.win.clips,sizeof(struct v4l2_clip)*vf->fmt.win.clipcount)) {
						return -EFAULT;
					}
					on = usbvision->overlay;	// Save overlay state
					if (on) {
						usbvision_cap(usbvision, 0);
					}

					// strange, it seems xawtv sometimes calls us with 0
					// width and/or height. Ignore these values
					if (vf->fmt.win.w.left == 0) {
						vf->fmt.win.w.left = usbvision->vid_win.fmt.win.w.left;
					}
					if (vf->fmt.win.w.top == 0) {
						vf->fmt.win.w.top = usbvision->vid_win.fmt.win.w.top;
					}

					// by now we are committed to the new data...
					down(&usbvision->lock);
					RESTRICT_TO_RANGE(vf->fmt.win.w.width, MIN_FRAME_WIDTH, MAX_FRAME_WIDTH);
					RESTRICT_TO_RANGE(vf->fmt.win.w.height, MIN_FRAME_HEIGHT, MAX_FRAME_HEIGHT);
					usbvision->vid_win = *vf;
					usbvision->overlay_frame.width = vf->fmt.win.w.width;
					usbvision->overlay_frame.height = vf->fmt.win.w.height;
					usbvision_set_output(usbvision, vf->fmt.win.w.width, vf->fmt.win.w.height);
					up(&usbvision->lock);

					// Impose display clips
					if (vf->fmt.win.w.left+vf->fmt.win.w.width > (unsigned int)usbvision->vid_buf.fmt.width) {
						usbvision_new_clip(vf, vc, usbvision->vid_buf.fmt.width-vf->fmt.win.w.left, 0, vf->fmt.win.w.width-1, vf->fmt.win.w.height-1);
					}
					if (vf->fmt.win.w.top+vf->fmt.win.w.height > (unsigned int)usbvision->vid_buf.fmt.height) {
						usbvision_new_clip(vf, vc, 0, usbvision->vid_buf.fmt.height-vf->fmt.win.w.top, vf->fmt.win.w.width-1, vf->fmt.win.w.height-1);
					}

					// built the requested clipping zones
					usbvision_built_overlay(usbvision, vf->fmt.win.clipcount, vc);
					vfree(vc);

					// restore overlay state
					if (on) {
						usbvision_cap(usbvision, 1);
					}
					usbvision->vid_win_valid = 1;
					PDEBUG(DBG_IOCTL, "VIDIOC_S_FMT overlay x=%d, y=%d, w=%d, h=%d, chroma=%x, clips=%d",
					       vf->fmt.win.w.left, vf->fmt.win.w.top, vf->fmt.win.w.width, vf->fmt.win.w.height, vf->fmt.win.chromakey, vf->fmt.win.clipcount);
					return 0;
				}
				case V4L2_BUF_TYPE_VIDEO_CAPTURE:
				{
					/* Find requested format in available ones */
					for(formatIdx=0;formatIdx<USBVISION_SUPPORTED_PALETTES;formatIdx++) {
						if(vf->fmt.pix.pixelformat == usbvision_v4l2_format[formatIdx].format) {
							usbvision->palette = usbvision_v4l2_format[formatIdx];
							break;
						}
					}
					/* robustness */
					if(formatIdx == USBVISION_SUPPORTED_PALETTES) {
						return -EINVAL;
					}
					RESTRICT_TO_RANGE(vf->fmt.pix.width, MIN_FRAME_WIDTH, MAX_FRAME_WIDTH);
					RESTRICT_TO_RANGE(vf->fmt.pix.height, MIN_FRAME_HEIGHT, MAX_FRAME_HEIGHT);
					// by now we are committed to the new data...
					down(&usbvision->lock);
					usbvision_set_output(usbvision, vf->fmt.pix.width, vf->fmt.pix.height);
					up(&usbvision->lock);

					PDEBUG(DBG_IOCTL, "VIDIOC_S_FMT grabdisplay w=%d, h=%d, format=%s",
					       vf->fmt.pix.width, vf->fmt.pix.height,usbvision->palette.desc);
					return 0;
				}
				default:
					return -EINVAL;
			}
		}
		case VIDIOC_OVERLAY:
		{
			int *v = arg;

			if ( (dga == 0) &&
			     (usbvision->palette.format != V4L2_PIX_FMT_YVU420) &&
			     (usbvision->palette.format != V4L2_PIX_FMT_YUV422P) ) {
				PDEBUG(DBG_IOCTL, "VIDIOC_OVERLAY  DGA disabled");
				return -EINVAL;
			}

			if (*v == 0) {
				usbvision_cap(usbvision, 0);
			}
			else {
				// are VIDIOCSFBUF and VIDIOCSWIN done?
				if ((usbvision->vid_buf_valid == 0) || (usbvision->vid_win_valid == 0)) {
					PDEBUG(DBG_IOCTL, "VIDIOC_OVERLAY  vid_buf_valid %d; vid_win_valid %d",
					       usbvision->vid_buf_valid, usbvision->vid_win_valid);
					return -EINVAL;
				}
				usbvision_cap(usbvision, 1);
			}
			PDEBUG(DBG_IOCTL, "VIDIOC_OVERLAY %s", (*v)?"on":"off");
			return 0;
		}
		default:
			return -ENOIOCTLCMD;
	}
	return 0;
}

static int usbvision_v4l2_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, usbvision_v4l2_do_ioctl);
}


static ssize_t usbvision_v4l2_read(struct file *file, char *buf,
		      size_t count, loff_t *ppos)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision = (struct usb_usbvision *) video_get_drvdata(dev);
	int noblock = file->f_flags & O_NONBLOCK;
	unsigned long lock_flags;

	int frmx = -1;
	int ret,i;
	struct usbvision_frame *frame;

	PDEBUG(DBG_IO, "%s: %ld bytes, noblock=%d", __FUNCTION__, (unsigned long)count, noblock);

	if (!USBVISION_IS_OPERATIONAL(usbvision) || (buf == NULL))
		return -EFAULT;

	/* no stream is running, make it running ! */
	usbvision->streaming = Stream_On;
	call_i2c_clients(usbvision,VIDIOC_STREAMON , NULL);

	/* First, enqueue as many frames as possible (like a user of VIDIOC_QBUF would do) */
	for(i=0;i<USBVISION_NUMFRAMES;i++) {
		frame = &usbvision->frame[i];
		if(frame->grabstate == FrameState_Unused) {
			/* Mark it as ready and enqueue frame */
			frame->grabstate = FrameState_Ready;
			frame->scanstate = ScanState_Scanning;
			frame->scanlength = 0;	/* Accumulated in usbvision_parse_data() */

			/* set v4l2_format index */
			frame->v4l2_format = usbvision->palette;

			spin_lock_irqsave(&usbvision->queue_lock, lock_flags);
			list_add_tail(&frame->frame, &usbvision->inqueue);
			spin_unlock_irqrestore(&usbvision->queue_lock, lock_flags);
		}
	}

	/* Then try to steal a frame (like a VIDIOC_DQBUF would do) */
	if (list_empty(&(usbvision->outqueue))) {
		if(noblock)
			return -EAGAIN;

		ret = wait_event_interruptible
			(usbvision->wait_frame,
			 !list_empty(&(usbvision->outqueue)));
		if (ret)
			return ret;
	}

	spin_lock_irqsave(&usbvision->queue_lock, lock_flags);
	frame = list_entry(usbvision->outqueue.next,
			   struct usbvision_frame, frame);
	list_del(usbvision->outqueue.next);
	spin_unlock_irqrestore(&usbvision->queue_lock, lock_flags);

	if(debug & DBG_IOCTL) { // do not spend computing time for debug stuff if not needed !
		if(usbvision_counter == 100) {
			PDEBUG(DBG_IOCTL, "VIDIOC_DQBUF delta=%d",(unsigned)(jiffies-usbvision_timestamp));
			usbvision_counter = 0;
			usbvision_timestamp = jiffies;
		}
		else {
			usbvision_counter++;
		}
	}

	/* An error returns an empty frame */
	if (frame->grabstate == FrameState_Error) {
		frame->bytes_read = 0;
		return 0;
	}

	PDEBUG(DBG_IO, "%s: frmx=%d, bytes_read=%ld, scanlength=%ld", __FUNCTION__,
		       frame->index, frame->bytes_read, frame->scanlength);

	/* copy bytes to user space; we allow for partials reads */
	if ((count + frame->bytes_read) > (unsigned long)frame->scanlength)
		count = frame->scanlength - frame->bytes_read;

	if (copy_to_user(buf, frame->data + frame->bytes_read, count)) {
		return -EFAULT;
	}

	frame->bytes_read += count;
	PDEBUG(DBG_IO, "%s: {copy} count used=%ld, new bytes_read=%ld", __FUNCTION__,
		       (unsigned long)count, frame->bytes_read);

	// For now, forget the frame if it has not been read in one shot.
/* 	if (frame->bytes_read >= frame->scanlength) {// All data has been read */
		frame->bytes_read = 0;

		/* Mark it as available to be used again. */
		usbvision->frame[frmx].grabstate = FrameState_Unused;
/* 	} */

	return count;
}

static int usbvision_v4l2_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start,
		start = vma->vm_start;
	void *pos;
	u32 i;

	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision = (struct usb_usbvision *) video_get_drvdata(dev);

	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return -EFAULT;

	if (!(vma->vm_flags & VM_WRITE) ||
	    size != PAGE_ALIGN(usbvision->max_frame_size)) {
		return -EINVAL;
	}

	for (i = 0; i < USBVISION_NUMFRAMES; i++) {
		if (((usbvision->max_frame_size*i) >> PAGE_SHIFT) == vma->vm_pgoff)
			break;
	}
	if (i == USBVISION_NUMFRAMES) {
		PDEBUG(DBG_FUNC, "mmap: user supplied mapping address is out of range");
		return -EINVAL;
	}

	/* VM_IO is eventually going to replace PageReserved altogether */
	vma->vm_flags |= VM_IO;
	vma->vm_flags |= VM_RESERVED;	/* avoid to swap out this VMA */

	pos = usbvision->frame[i].data;
	while (size > 0) {

		if (vm_insert_page(vma, start, vmalloc_to_page(pos))) {
			PDEBUG(DBG_FUNC, "mmap: vm_insert_page failed");
			return -EAGAIN;
		}
		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	return 0;
}


/*
 * Here comes the stuff for radio on usbvision based devices
 *
 */
static int usbvision_radio_open(struct inode *inode, struct file *file)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision = (struct usb_usbvision *) video_get_drvdata(dev);
	struct v4l2_frequency freq;
	int errCode = 0;

	PDEBUG(DBG_RIO, "%s:", __FUNCTION__);

	down(&usbvision->lock);

	if (usbvision->user) {
		err("%s: Someone tried to open an already opened USBVision Radio!", __FUNCTION__);
		errCode = -EBUSY;
	}
	else {
		if(PowerOnAtOpen) {
			if (timer_pending(&usbvision->powerOffTimer)) {
				del_timer(&usbvision->powerOffTimer);
			}
			if (usbvision->power == 0) {
				usbvision_power_on(usbvision);
				usbvision_init_i2c(usbvision);
			}
		}

		// If so far no errors then we shall start the radio
		usbvision->radio = 1;
		call_i2c_clients(usbvision,AUDC_SET_RADIO,&usbvision->tuner_type);
		freq.frequency = 1517; //SWR3 @ 94.8MHz
		call_i2c_clients(usbvision, VIDIOC_S_FREQUENCY, &freq);
		usbvision_set_audio(usbvision, USBVISION_AUDIO_RADIO);
		usbvision->user++;
	}

	if (errCode) {
		if (PowerOnAtOpen) {
			usbvision_i2c_usb_del_bus(&usbvision->i2c_adap);
			usbvision_power_off(usbvision);
			usbvision->initialized = 0;
		}
	}
	up(&usbvision->lock);
	return errCode;
}


static int usbvision_radio_close(struct inode *inode, struct file *file)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision = (struct usb_usbvision *) video_get_drvdata(dev);
	int errCode = 0;

	PDEBUG(DBG_RIO, "");

	down(&usbvision->lock);

	usbvision_audio_off(usbvision);
	usbvision->radio=0;
	usbvision->user--;

	if (PowerOnAtOpen) {
		mod_timer(&usbvision->powerOffTimer, jiffies + USBVISION_POWEROFF_TIME);
		usbvision->initialized = 0;
	}

	up(&usbvision->lock);

	if (usbvision->remove_pending) {
		info("%s: Final disconnect", __FUNCTION__);
		usbvision_release(usbvision);
	}


	PDEBUG(DBG_RIO, "success");

	return errCode;
}

static int usbvision_do_radio_ioctl(struct inode *inode, struct file *file,
				 unsigned int cmd, void *arg)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision = (struct usb_usbvision *) video_get_drvdata(dev);

	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return -EIO;

	switch (cmd) {
		case VIDIOC_QUERYCAP:
		{
			struct v4l2_capability *vc=arg;

			memset(vc, 0, sizeof(*vc));
			strlcpy(vc->driver, "USBVision", sizeof(vc->driver));
			strlcpy(vc->card, usbvision_device_data[usbvision->DevModel].ModelString,
				sizeof(vc->card));
			strlcpy(vc->bus_info, usbvision->dev->dev.bus_id,
				sizeof(vc->bus_info));
			vc->version = USBVISION_DRIVER_VERSION;
			vc->capabilities = (usbvision->have_tuner ? V4L2_CAP_TUNER : 0);
			PDEBUG(DBG_RIO, "VIDIOC_QUERYCAP");
			return 0;
		}
		case VIDIOC_QUERYCTRL:
		{
			struct v4l2_queryctrl *ctrl = arg;
			int id=ctrl->id;

			memset(ctrl,0,sizeof(*ctrl));
			ctrl->id=id;

			call_i2c_clients(usbvision, cmd, arg);
			PDEBUG(DBG_RIO,"VIDIOC_QUERYCTRL id=%x value=%x",ctrl->id,ctrl->type);

			if (ctrl->type)
				return 0;
			else
				return -EINVAL;

		}
		case VIDIOC_G_CTRL:
		{
			struct v4l2_control *ctrl = arg;

			call_i2c_clients(usbvision, VIDIOC_G_CTRL, ctrl);
			PDEBUG(DBG_RIO,"VIDIOC_G_CTRL id=%x value=%x",ctrl->id,ctrl->value);
			return 0;
		}
		case VIDIOC_S_CTRL:
		{
			struct v4l2_control *ctrl = arg;

			call_i2c_clients(usbvision, VIDIOC_S_CTRL, ctrl);
			PDEBUG(DBG_RIO, "VIDIOC_S_CTRL id=%x value=%x",ctrl->id,ctrl->value);
			return 0;
		}
		case VIDIOC_G_TUNER:
		{
			struct v4l2_tuner *t = arg;

			if (t->index > 0)
				return -EINVAL;

			memset(t,0,sizeof(*t));
			strcpy(t->name, "Radio");
			t->type = V4L2_TUNER_RADIO;

			/* Let clients fill in the remainder of this struct */
			call_i2c_clients(usbvision,VIDIOC_G_TUNER,t);
			PDEBUG(DBG_RIO, "VIDIOC_G_TUNER signal=%x, afc=%x",t->signal,t->afc);
			return 0;
		}
		case VIDIOC_S_TUNER:
		{
			struct v4l2_tuner *vt = arg;

			// Only no or one tuner for now
			if (!usbvision->have_tuner || vt->index)
				return -EINVAL;
			/* let clients handle this */
			call_i2c_clients(usbvision,VIDIOC_S_TUNER,vt);

			PDEBUG(DBG_RIO, "VIDIOC_S_TUNER");
			return 0;
		}
		case VIDIOC_G_AUDIO:
		{
			struct v4l2_audio *a = arg;

			memset(a,0,sizeof(*a));
			strcpy(a->name,"Radio");
			PDEBUG(DBG_RIO, "VIDIOC_G_AUDIO");
			return 0;
		}
		case VIDIOC_S_AUDIO:
		case VIDIOC_S_INPUT:
		case VIDIOC_S_STD:
		return 0;

		case VIDIOC_G_FREQUENCY:
		{
			struct v4l2_frequency *f = arg;

			memset(f,0,sizeof(*f));

			f->type = V4L2_TUNER_RADIO;
			f->frequency = usbvision->freq;
			call_i2c_clients(usbvision, cmd, f);
			PDEBUG(DBG_RIO, "VIDIOC_G_FREQUENCY freq=0x%X", (unsigned)f->frequency);

			return 0;
		}
		case VIDIOC_S_FREQUENCY:
		{
			struct v4l2_frequency *f = arg;

			if (f->tuner != 0)
				return -EINVAL;
			usbvision->freq = f->frequency;
			call_i2c_clients(usbvision, cmd, f);
			PDEBUG(DBG_RIO, "VIDIOC_S_FREQUENCY freq=0x%X", (unsigned)f->frequency);

			return 0;
		}
		default:
		{
			PDEBUG(DBG_RIO, "%s: Unknown command %x", __FUNCTION__, cmd);
			return -ENOIOCTLCMD;
		}
	}
	return 0;
}


static int usbvision_radio_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, usbvision_do_radio_ioctl);
}


/*
 * Here comes the stuff for vbi on usbvision based devices
 *
 */
static int usbvision_vbi_open(struct inode *inode, struct file *file)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision = (struct usb_usbvision *) video_get_drvdata(dev);
	unsigned long freq;
	int errCode = 0;

	PDEBUG(DBG_RIO, "%s:", __FUNCTION__);

	down(&usbvision->lock);

	if (usbvision->user) {
		err("%s: Someone tried to open an already opened USBVision VBI!", __FUNCTION__);
		errCode = -EBUSY;
	}
	else {
		if(PowerOnAtOpen) {
			if (timer_pending(&usbvision->powerOffTimer)) {
				del_timer(&usbvision->powerOffTimer);
			}
			if (usbvision->power == 0) {
				usbvision_power_on(usbvision);
				usbvision_init_i2c(usbvision);
			}
		}

		// If so far no errors then we shall start the vbi device
		//usbvision->vbi = 1;
		call_i2c_clients(usbvision,AUDC_SET_RADIO,&usbvision->tuner_type);
		freq = 1517; //SWR3 @ 94.8MHz
		call_i2c_clients(usbvision, VIDIOC_S_FREQUENCY, &freq);
		usbvision_set_audio(usbvision, USBVISION_AUDIO_RADIO);
		usbvision->user++;
	}

	if (errCode) {
		if (PowerOnAtOpen) {
			usbvision_i2c_usb_del_bus(&usbvision->i2c_adap);
			usbvision_power_off(usbvision);
			usbvision->initialized = 0;
		}
	}
	up(&usbvision->lock);
	return errCode;
}

static int usbvision_vbi_close(struct inode *inode, struct file *file)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision = (struct usb_usbvision *) video_get_drvdata(dev);
	int errCode = 0;

	PDEBUG(DBG_RIO, "");

	down(&usbvision->lock);

	usbvision_audio_off(usbvision);
	usbvision->vbi=0;
	usbvision->user--;

	if (PowerOnAtOpen) {
		mod_timer(&usbvision->powerOffTimer, jiffies + USBVISION_POWEROFF_TIME);
		usbvision->initialized = 0;
	}

	up(&usbvision->lock);

	if (usbvision->remove_pending) {
		info("%s: Final disconnect", __FUNCTION__);
		usbvision_release(usbvision);
	}


	PDEBUG(DBG_RIO, "success");

	return errCode;
}

static int usbvision_do_vbi_ioctl(struct inode *inode, struct file *file,
				 unsigned int cmd, void *arg)
{
	struct video_device *dev = video_devdata(file);
	struct usb_usbvision *usbvision = (struct usb_usbvision *) video_get_drvdata(dev);

	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return -EIO;

	switch (cmd) {
		case VIDIOC_QUERYCAP:
		{
			struct v4l2_capability *vc=arg;
			memset(vc, 0, sizeof(struct v4l2_capability));
			strcpy(vc->driver,"usbvision vbi");
			strcpy(vc->card,usbvision->vcap.card);
			strcpy(vc->bus_info,"usb");
			vc->version = USBVISION_DRIVER_VERSION; 	    /* version */
			vc->capabilities = V4L2_CAP_VBI_CAPTURE; 	    /* capabilities */
			PDEBUG(DBG_RIO, "%s: VIDIOC_QUERYCAP", __FUNCTION__);
			return 0;
		}
		case VIDIOCGTUNER:
		{
			struct video_tuner *vt = arg;

			if((vt->tuner) || (usbvision->channel)) {	/* Only tuner 0 */
				return -EINVAL;
			}
			strcpy(vt->name, "vbi");
			// japan:          76.0 MHz -  89.9 MHz
			// western europe: 87.5 MHz - 108.0 MHz
			// russia:         65.0 MHz - 108.0 MHz
			vt->rangelow=(int)(65*16);
			vt->rangehigh=(int)(108*16);
			vt->flags= 0;
			vt->mode = 0;
			call_i2c_clients(usbvision,cmd,vt);
			PDEBUG(DBG_RIO, "%s: VIDIOCGTUNER signal=%d", __FUNCTION__, vt->signal);
			return 0;
		}
		case VIDIOCSTUNER:
		{
			struct video_tuner *vt = arg;

			// Only channel 0 has a tuner
			if((vt->tuner) || (usbvision->channel)) {
				return -EINVAL;
			}
			PDEBUG(DBG_RIO, "%s: VIDIOCSTUNER", __FUNCTION__);
			return 0;
		}
		case VIDIOCGAUDIO:
		{
			struct video_audio *va = arg;
			memset(va,0, sizeof(struct video_audio));
			call_i2c_clients(usbvision, cmd, va);
			va->flags|=VIDEO_AUDIO_MUTABLE;
			va->volume=1;
			va->step=1;
			strcpy(va->name, "vbi");
			PDEBUG(DBG_RIO, "%s: VIDIOCGAUDIO", __FUNCTION__);
			return 0;
		}
		case VIDIOCSAUDIO:
		{
			struct video_audio *va = arg;
			if(va->audio) {
				return -EINVAL;
			}

			if(va->flags & VIDEO_AUDIO_MUTE) {
				if (usbvision_audio_mute(usbvision)) {
					return -EFAULT;
				}
			}
			else {
				if (usbvision_audio_on(usbvision)) {
					return -EFAULT;
				}
			}
			PDEBUG(DBG_RIO, "%s: VIDIOCSAUDIO flags=0x%x)", __FUNCTION__, va->flags);
			return 0;
		}
		case VIDIOCGFREQ:
		{
			unsigned long *freq = arg;

			*freq = usbvision->freq;
			PDEBUG(DBG_RIO, "%s: VIDIOCGFREQ freq = %ld00 kHz", __FUNCTION__, (*freq * 10)>>4);
			return 0;
		}
		case VIDIOCSFREQ:
		{
			unsigned long *freq = arg;

			usbvision->freq = *freq;
			call_i2c_clients(usbvision, cmd, freq);
			PDEBUG(DBG_RIO, "%s: VIDIOCSFREQ freq = %ld00 kHz", __FUNCTION__, (*freq * 10)>>4);
			return 0;
		}
		default:
		{
			PDEBUG(DBG_RIO, "%s: Unknown command %d", __FUNCTION__, cmd);
			return -ENOIOCTLCMD;
		}
	}
	return 0;
}

static int usbvision_vbi_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, usbvision_do_vbi_ioctl);
}



static void usbvision_configure_video(struct usb_usbvision *usbvision)
{
	int model,i;

	if (usbvision == NULL)
		return;

	model = usbvision->DevModel;
	usbvision->depth = 24;
	usbvision->palette = usbvision_v4l2_format[2]; // V4L2_PIX_FMT_RGB24;

	if (usbvision_device_data[usbvision->DevModel].Vin_Reg2 >= 0) {
		usbvision->Vin_Reg2_Preset = usbvision_device_data[usbvision->DevModel].Vin_Reg2 & 0xff;
	} else {
		usbvision->Vin_Reg2_Preset = 0;
	}

	memset(&usbvision->vcap, 0, sizeof(usbvision->vcap));
	strcpy(usbvision->vcap.driver, "USBVision");
	strlcpy(usbvision->vcap.bus_info, usbvision->dev->dev.bus_id,
		sizeof(usbvision->vcap.bus_info));
	usbvision->vcap.capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_AUDIO | V4L2_CAP_READWRITE | V4L2_CAP_STREAMING |
		(dga ? (V4L2_FBUF_CAP_LIST_CLIPPING | V4L2_CAP_VIDEO_OVERLAY) : 0) |
		(usbvision->have_tuner ? V4L2_CAP_TUNER : 0);
	usbvision->vcap.version = USBVISION_DRIVER_VERSION; 	    /* version */

	for (i = 0; i < TVNORMS; i++)
		if (usbvision_device_data[model].VideoNorm == tvnorms[i].mode)
			break;
	if (i == TVNORMS)
		i = 0;
	usbvision->tvnorm = &tvnorms[i];        /* set default norm */

	usbvision->video_inputs = usbvision_device_data[model].VideoChannels;
	usbvision->ctl_input = 0;

	/* This should be here to make i2c clients to be able to register */
	usbvision_audio_off(usbvision);	//first switch off audio
	if (!PowerOnAtOpen) {
		usbvision_power_on(usbvision);	//and then power up the noisy tuner
		usbvision_init_i2c(usbvision);
	}
}

//
// Video registration stuff
//

// Video template
static struct file_operations usbvision_fops = {
  #if LINUX_VERSION_CODE > KERNEL_VERSION(2,4,31)
	.owner             = THIS_MODULE,
  #endif
	.open		= usbvision_v4l2_open,
	.release	= usbvision_v4l2_close,
	.read		= usbvision_v4l2_read,
	.mmap		= usbvision_v4l2_mmap,
	.ioctl		= usbvision_v4l2_ioctl,
	.llseek		= no_llseek,
};
static struct video_device usbvision_video_template = {
  #if LINUX_VERSION_CODE > KERNEL_VERSION(2,4,31)
	.owner             = THIS_MODULE,
  #endif
	.type		= VID_TYPE_TUNER | VID_TYPE_CAPTURE,
	.hardware	= VID_HARDWARE_USBVISION,
	.fops		= &usbvision_fops,
  #if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	.name           = "usbvision-video",
	.release	= video_device_release,
  #endif
	.minor		= -1,
};


// Radio template
static struct file_operations usbvision_radio_fops = {
  #if LINUX_VERSION_CODE > KERNEL_VERSION(2,4,31)
	.owner             = THIS_MODULE,
  #endif
	.open		= usbvision_radio_open,
	.release	= usbvision_radio_close,
	.ioctl		= usbvision_radio_ioctl,
	.llseek		= no_llseek,
};

static struct video_device usbvision_radio_template=
{
  #if LINUX_VERSION_CODE > KERNEL_VERSION(2,4,31)
	.owner             = THIS_MODULE,
  #endif
	.type		= VID_TYPE_TUNER,
	.hardware	= VID_HARDWARE_USBVISION,
	.fops		= &usbvision_radio_fops,
  #if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	.release	= video_device_release,
	.name           = "usbvision-radio",
  #endif
	.minor		= -1,
};


// vbi template
static struct file_operations usbvision_vbi_fops = {
  #if LINUX_VERSION_CODE > KERNEL_VERSION(2,4,31)
	.owner             = THIS_MODULE,
  #endif
	.open		= usbvision_vbi_open,
	.release	= usbvision_vbi_close,
	.ioctl		= usbvision_vbi_ioctl,
	.llseek		= no_llseek,
};

static struct video_device usbvision_vbi_template=
{
  #if LINUX_VERSION_CODE > KERNEL_VERSION(2,4,31)
	.owner             = THIS_MODULE,
  #endif
	.type		= VID_TYPE_TUNER,
	.hardware	= VID_HARDWARE_USBVISION,
	.fops		= &usbvision_vbi_fops,
  #if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	.release	= video_device_release,
	.name           = "usbvision-vbi",
  #endif
	.minor		= -1,
};


static struct video_device *usbvision_vdev_init(struct usb_usbvision *usbvision,
					struct video_device *vdev_template,
					char *name)
{
	struct usb_device *usb_dev = usbvision->dev;
	struct video_device *vdev;

	if (usb_dev == NULL) {
		err("%s: usbvision->dev is not set", __FUNCTION__);
		return NULL;
	}

	vdev = video_device_alloc();
	if (NULL == vdev) {
		return NULL;
	}
	*vdev = *vdev_template;
//	vdev->minor   = -1;
	vdev->dev     = &usb_dev->dev;
	snprintf(vdev->name, sizeof(vdev->name), "%s", name);
	video_set_drvdata(vdev, usbvision);
	return vdev;
}

// unregister video4linux devices
static void usbvision_unregister_video(struct usb_usbvision *usbvision)
{
	// vbi Device:
	if (usbvision->vbi) {
		PDEBUG(DBG_PROBE, "unregister /dev/vbi%d [v4l2]", usbvision->vbi->minor & 0x1f);
		if (usbvision->vbi->minor != -1) {
			video_unregister_device(usbvision->vbi);
		}
		else {
			video_device_release(usbvision->vbi);
		}
		usbvision->vbi = NULL;
	}

	// Radio Device:
	if (usbvision->rdev) {
		PDEBUG(DBG_PROBE, "unregister /dev/radio%d [v4l2]", usbvision->rdev->minor & 0x1f);
		if (usbvision->rdev->minor != -1) {
			video_unregister_device(usbvision->rdev);
		}
		else {
			video_device_release(usbvision->rdev);
		}
		usbvision->rdev = NULL;
	}

	// Video Device:
	if (usbvision->vdev) {
		PDEBUG(DBG_PROBE, "unregister /dev/video%d [v4l2]", usbvision->vdev->minor & 0x1f);
		if (usbvision->vdev->minor != -1) {
			video_unregister_device(usbvision->vdev);
		}
		else {
			video_device_release(usbvision->vdev);
		}
		usbvision->vdev = NULL;
	}
}

// register video4linux devices
static int __devinit usbvision_register_video(struct usb_usbvision *usbvision)
{
	// Video Device:
	usbvision->vdev = usbvision_vdev_init(usbvision, &usbvision_video_template, "USBVision Video");
	if (usbvision->vdev == NULL) {
		goto err_exit;
	}
	if (video_register_device(usbvision->vdev, VFL_TYPE_GRABBER, video_nr)<0) {
		goto err_exit;
	}
	info("USBVision[%d]: registered USBVision Video device /dev/video%d [v4l2]", usbvision->nr,usbvision->vdev->minor & 0x1f);

	// Radio Device:
	if (usbvision_device_data[usbvision->DevModel].Radio) {
		// usbvision has radio
		usbvision->rdev = usbvision_vdev_init(usbvision, &usbvision_radio_template, "USBVision Radio");
		if (usbvision->rdev == NULL) {
			goto err_exit;
		}
		if (video_register_device(usbvision->rdev, VFL_TYPE_RADIO, radio_nr)<0) {
			goto err_exit;
		}
		info("USBVision[%d]: registered USBVision Radio device /dev/radio%d [v4l2]", usbvision->nr, usbvision->rdev->minor & 0x1f);
	}
	// vbi Device:
	if (usbvision_device_data[usbvision->DevModel].vbi) {
		usbvision->vbi = usbvision_vdev_init(usbvision, &usbvision_vbi_template, "USBVision VBI");
		if (usbvision->vdev == NULL) {
			goto err_exit;
		}
		if (video_register_device(usbvision->vbi, VFL_TYPE_VBI, vbi_nr)<0) {
			goto err_exit;
		}
		info("USBVision[%d]: registered USBVision VBI device /dev/vbi%d [v4l2] (Not Working Yet!)", usbvision->nr,usbvision->vbi->minor & 0x1f);
	}
	// all done
	return 0;

 err_exit:
	err("USBVision[%d]: video_register_device() failed", usbvision->nr);
	usbvision_unregister_video(usbvision);
	return -1;
}

/*
 * usbvision_alloc()
 *
 * This code allocates the struct usb_usbvision. It is filled with default values.
 *
 * Returns NULL on error, a pointer to usb_usbvision else.
 *
 */
static struct usb_usbvision *usbvision_alloc(struct usb_device *dev)
{
	struct usb_usbvision *usbvision;

	if ((usbvision = kzalloc(sizeof(struct usb_usbvision), GFP_KERNEL)) == NULL) {
		goto err_exit;
	}

	usbvision->dev = dev;

	init_MUTEX(&usbvision->lock);	/* to 1 == available */

	// prepare control urb for control messages during interrupts
	usbvision->ctrlUrb = usb_alloc_urb(USBVISION_URB_FRAMES, GFP_KERNEL);
	if (usbvision->ctrlUrb == NULL) {
		goto err_exit;
	}
	init_waitqueue_head(&usbvision->ctrlUrb_wq);
	init_MUTEX(&usbvision->ctrlUrbLock);	/* to 1 == available */

	init_timer(&usbvision->powerOffTimer);
	usbvision->powerOffTimer.data = (long) usbvision;
	usbvision->powerOffTimer.function = usbvision_powerOffTimer;

	return usbvision;

err_exit:
	if (usbvision && usbvision->ctrlUrb) {
		usb_free_urb(usbvision->ctrlUrb);
	}
	if (usbvision) {
		kfree(usbvision);
	}
	return NULL;
}

/*
 * usbvision_release()
 *
 * This code does final release of struct usb_usbvision. This happens
 * after the device is disconnected -and- all clients closed their files.
 *
 */
static void usbvision_release(struct usb_usbvision *usbvision)
{
	PDEBUG(DBG_PROBE, "");

	down(&usbvision->lock);

	if (timer_pending(&usbvision->powerOffTimer)) {
		del_timer(&usbvision->powerOffTimer);
	}

	usbvision->usbvision_used = 0;
	usbvision->initialized = 0;

	up(&usbvision->lock);

	usbvision_remove_sysfs(usbvision->vdev);
	usbvision_unregister_video(usbvision);
	if(dga) {
		if (usbvision->overlay_base) {
			iounmap(usbvision->overlay_base);
		}
	}

	if (usbvision->ctrlUrb) {
		usb_free_urb(usbvision->ctrlUrb);
	}

	kfree(usbvision);

	PDEBUG(DBG_PROBE, "success");
}


/*
 * usbvision_probe()
 *
 * This procedure queries device descriptor and accepts the interface
 * if it looks like USBVISION video device
 *
 */
static int __devinit usbvision_probe(struct usb_interface *intf, const struct usb_device_id *devid)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	__u8 ifnum = intf->altsetting->desc.bInterfaceNumber;
	const struct usb_host_interface *interface;
	struct usb_usbvision *usbvision = NULL;
	const struct usb_endpoint_descriptor *endpoint;
	int model;

	PDEBUG(DBG_PROBE, "VID=%#04x, PID=%#04x, ifnum=%u",
					dev->descriptor.idVendor, dev->descriptor.idProduct, ifnum);
	/* Is it an USBVISION video dev? */
	model = 0;
	for(model = 0; usbvision_device_data[model].idVendor; model++) {
		if (le16_to_cpu(dev->descriptor.idVendor) != usbvision_device_data[model].idVendor) {
			continue;
		}
		if (le16_to_cpu(dev->descriptor.idProduct) != usbvision_device_data[model].idProduct) {
			continue;
		}

		info("%s: %s found", __FUNCTION__, usbvision_device_data[model].ModelString);
		break;
	}

	if (usbvision_device_data[model].idVendor == 0) {
		return -ENODEV; //no matching device
	}
	if (usbvision_device_data[model].Interface >= 0) {
		interface = &dev->actconfig->interface[usbvision_device_data[model].Interface]->altsetting[0];
	}
	else {
		interface = &dev->actconfig->interface[ifnum]->altsetting[0];
	}
	endpoint = &interface->endpoint[1].desc;
	if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_ISOC) {
		err("%s: interface %d. has non-ISO endpoint!", __FUNCTION__, ifnum);
		err("%s: Endpoint attribures %d", __FUNCTION__, endpoint->bmAttributes);
		return -ENODEV;
	}
	if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT) {
		err("%s: interface %d. has ISO OUT endpoint!", __FUNCTION__, ifnum);
		return -ENODEV;
	}

	usb_get_dev(dev);

	if ((usbvision = usbvision_alloc(dev)) == NULL) {
		err("%s: couldn't allocate USBVision struct", __FUNCTION__);
		return -ENOMEM;
	}
	if (dev->descriptor.bNumConfigurations > 1) {
		usbvision->bridgeType = BRIDGE_NT1004;
	}
	else if (usbvision_device_data[model].ModelString == "Dazzle Fusion Model DVC-90 Rev 1 (SECAM)") {
		usbvision->bridgeType = BRIDGE_NT1005;
	}
	else {
		usbvision->bridgeType = BRIDGE_NT1003;
	}
	PDEBUG(DBG_PROBE, "bridgeType %d", usbvision->bridgeType);

	down(&usbvision->lock);

	usbvision->nr = usbvision_nr++;

	usbvision->have_tuner = usbvision_device_data[model].Tuner;
	if (usbvision->have_tuner) {
		usbvision->tuner_type = usbvision_device_data[model].TunerType;
	}

	usbvision->tuner_addr = ADDR_UNSET;

	usbvision->DevModel = model;
	usbvision->remove_pending = 0;
	usbvision->last_error = 0;
	usbvision->iface = ifnum;
	usbvision->ifaceAltInactive = 0;
	usbvision->ifaceAltActive = 1;
	usbvision->video_endp = endpoint->bEndpointAddress;
	usbvision->isocPacketSize = 0;
	usbvision->usb_bandwidth = 0;
	usbvision->user = 0;
	usbvision->streaming = Stream_Off;
	usbvision_register_video(usbvision);
	usbvision_configure_video(usbvision);
	up(&usbvision->lock);


	usb_set_intfdata (intf, usbvision);
	usbvision_create_sysfs(usbvision->vdev);

	PDEBUG(DBG_PROBE, "success");
	return 0;
}


/*
 * usbvision_disconnect()
 *
 * This procedure stops all driver activity, deallocates interface-private
 * structure (pointed by 'ptr') and after that driver should be removable
 * with no ill consequences.
 *
 */
static void __devexit usbvision_disconnect(struct usb_interface *intf)
{
	struct usb_usbvision *usbvision = usb_get_intfdata(intf);

	PDEBUG(DBG_PROBE, "");

	if (usbvision == NULL) {
		err("%s: usb_get_intfdata() failed", __FUNCTION__);
		return;
	}
	usb_set_intfdata (intf, NULL);

	down(&usbvision->lock);

	// At this time we ask to cancel outstanding URBs
	usbvision_stop_isoc(usbvision);

	if (usbvision->power) {
		usbvision_i2c_usb_del_bus(&usbvision->i2c_adap);
		usbvision_power_off(usbvision);
	}
	usbvision->remove_pending = 1;	// Now all ISO data will be ignored

	usb_put_dev(usbvision->dev);
	usbvision->dev = NULL;	// USB device is no more

	up(&usbvision->lock);

	if (usbvision->user) {
		info("%s: In use, disconnect pending", __FUNCTION__);
		wake_up_interruptible(&usbvision->wait_frame);
		wake_up_interruptible(&usbvision->wait_stream);
	}
	else {
		usbvision_release(usbvision);
	}

	PDEBUG(DBG_PROBE, "success");

}

static struct usb_driver usbvision_driver = {
	.name		= "usbvision",
	.id_table	= usbvision_table,
	.probe		= usbvision_probe,
	.disconnect	= usbvision_disconnect
};

/*
 * customdevice_process()
 *
 * This procedure preprocesses CustomDevice parameter if any
 *
 */
void customdevice_process(void)
{
	usbvision_device_data[0]=usbvision_device_data[1];
	usbvision_table[0]=usbvision_table[1];

	if(CustomDevice)
	{
		char *parse=CustomDevice;

		PDEBUG(DBG_PROBE, "CustomDevide=%s", CustomDevice);

		/*format is CustomDevice="0x0573 0x4D31 0 7113 3 PAL 1 1 1 5 -1 -1 -1 -1 -1"
		usbvision_device_data[0].idVendor;
		usbvision_device_data[0].idProduct;
		usbvision_device_data[0].Interface;
		usbvision_device_data[0].Codec;
		usbvision_device_data[0].VideoChannels;
		usbvision_device_data[0].VideoNorm;
		usbvision_device_data[0].AudioChannels;
		usbvision_device_data[0].Radio;
		usbvision_device_data[0].Tuner;
		usbvision_device_data[0].TunerType;
		usbvision_device_data[0].Vin_Reg1;
		usbvision_device_data[0].Vin_Reg2;
		usbvision_device_data[0].X_Offset;
		usbvision_device_data[0].Y_Offset;
		usbvision_device_data[0].Dvi_yuv;
		usbvision_device_data[0].ModelString;
		*/

		rmspace(parse);
		usbvision_device_data[0].ModelString="USBVISION Custom Device";

		parse+=2;
		sscanf(parse,"%x",&usbvision_device_data[0].idVendor);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "idVendor=0x%.4X", usbvision_device_data[0].idVendor);
		parse+=2;
		sscanf(parse,"%x",&usbvision_device_data[0].idProduct);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "idProduct=0x%.4X", usbvision_device_data[0].idProduct);
		sscanf(parse,"%d",&usbvision_device_data[0].Interface);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "Interface=%d", usbvision_device_data[0].Interface);
		sscanf(parse,"%d",&usbvision_device_data[0].Codec);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "Codec=%d", usbvision_device_data[0].Codec);
		sscanf(parse,"%d",&usbvision_device_data[0].VideoChannels);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "VideoChannels=%d", usbvision_device_data[0].VideoChannels);

		switch(*parse)
		{
			case 'P':
				PDEBUG(DBG_PROBE, "VideoNorm=PAL");
				usbvision_device_data[0].VideoNorm=VIDEO_MODE_PAL;
				break;

			case 'S':
				PDEBUG(DBG_PROBE, "VideoNorm=SECAM");
				usbvision_device_data[0].VideoNorm=VIDEO_MODE_SECAM;
				break;

			case 'N':
				PDEBUG(DBG_PROBE, "VideoNorm=NTSC");
				usbvision_device_data[0].VideoNorm=VIDEO_MODE_NTSC;
				break;

			default:
				PDEBUG(DBG_PROBE, "VideoNorm=PAL (by default)");
				usbvision_device_data[0].VideoNorm=VIDEO_MODE_PAL;
				break;
		}
		goto2next(parse);

		sscanf(parse,"%d",&usbvision_device_data[0].AudioChannels);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "AudioChannels=%d", usbvision_device_data[0].AudioChannels);
		sscanf(parse,"%d",&usbvision_device_data[0].Radio);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "Radio=%d", usbvision_device_data[0].Radio);
		sscanf(parse,"%d",&usbvision_device_data[0].Tuner);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "Tuner=%d", usbvision_device_data[0].Tuner);
		sscanf(parse,"%d",&usbvision_device_data[0].TunerType);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "TunerType=%d", usbvision_device_data[0].TunerType);
		sscanf(parse,"%d",&usbvision_device_data[0].Vin_Reg1);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "Vin_Reg1=%d", usbvision_device_data[0].Vin_Reg1);
		sscanf(parse,"%d",&usbvision_device_data[0].Vin_Reg2);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "Vin_Reg2=%d", usbvision_device_data[0].Vin_Reg2);
		sscanf(parse,"%d",&usbvision_device_data[0].X_Offset);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "X_Offset=%d", usbvision_device_data[0].X_Offset);
		sscanf(parse,"%d",&usbvision_device_data[0].Y_Offset);
		goto2next(parse);
		PDEBUG(DBG_PROBE, "Y_Offset=%d", usbvision_device_data[0].Y_Offset);
		sscanf(parse,"%d",&usbvision_device_data[0].Dvi_yuv);
		PDEBUG(DBG_PROBE, "Dvi_yuv=%d", usbvision_device_data[0].Dvi_yuv);

		//add to usbvision_table also
		usbvision_table[0].match_flags=USB_DEVICE_ID_MATCH_DEVICE;
		usbvision_table[0].idVendor=usbvision_device_data[0].idVendor;
		usbvision_table[0].idProduct=usbvision_device_data[0].idProduct;

	}
}



/*
 * usbvision_init()
 *
 * This code is run to initialize the driver.
 *
 */
static int __init usbvision_init(void)
{
	int errCode;

	PDEBUG(DBG_PROBE, "");

	PDEBUG(DBG_IOCTL, "IOCTL   debugging is enabled");
	PDEBUG(DBG_IO,  "IO      debugging is enabled");
	PDEBUG(DBG_RIO,  "RIO     debugging is enabled");
	PDEBUG(DBG_HEADER, "HEADER  debugging is enabled");
	PDEBUG(DBG_PROBE, "PROBE   debugging is enabled");
	PDEBUG(DBG_IRQ,  "IRQ     debugging is enabled");
	PDEBUG(DBG_ISOC, "ISOC    debugging is enabled");
	PDEBUG(DBG_PARSE, "PARSE   debugging is enabled");
	PDEBUG(DBG_SCRATCH, "SCRATCH debugging is enabled");
	PDEBUG(DBG_FUNC, "FUNC    debugging is enabled");
	PDEBUG(DBG_I2C,  "I2C     debugging is enabled");

	/* disable planar mode support unless compression enabled */
	if (isocMode != ISOC_MODE_COMPRESS ) {
		// FIXME : not the right way to set supported flag
		usbvision_v4l2_format[6].supported = 0; // V4L2_PIX_FMT_YVU420
		usbvision_v4l2_format[7].supported = 0; // V4L2_PIX_FMT_YUV422P
	}

	customdevice_process();

	errCode = usb_register(&usbvision_driver);

	if (errCode == 0) {
		info(DRIVER_DESC " : " DRIVER_VERSION);
		PDEBUG(DBG_PROBE, "success");
	}
	return errCode;
}

static void __exit usbvision_exit(void)
{
 PDEBUG(DBG_PROBE, "");

 usb_deregister(&usbvision_driver);
 PDEBUG(DBG_PROBE, "success");
}

module_init(usbvision_init);
module_exit(usbvision_exit);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
