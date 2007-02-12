/****************************************************************************
 *    ixj.c
 *
 * Device Driver for Quicknet Technologies, Inc.'s Telephony cards
 * including the Internet PhoneJACK, Internet PhoneJACK Lite,
 * Internet PhoneJACK PCI, Internet LineJACK, Internet PhoneCARD and
 * SmartCABLE
 *
 *    (c) Copyright 1999-2001  Quicknet Technologies, Inc.
 *
 *    This program is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU General Public License
 *    as published by the Free Software Foundation; either version
 *    2 of the License, or (at your option) any later version.
 *
 * Author:          Ed Okerson, <eokerson@quicknet.net>
 *
 * Contributors:    Greg Herlein, <gherlein@quicknet.net>
 *                  David W. Erhart, <derhart@quicknet.net>
 *                  John Sellers, <jsellers@quicknet.net>
 *                  Mike Preston, <mpreston@quicknet.net>
 *    
 * Fixes:           David Huggins-Daines, <dhd@cepstral.com>
 *                  Fabio Ferrari, <fabio.ferrari@digitro.com.br>
 *                  Artis Kugevics, <artis@mt.lv>
 *                  Daniele Bellucci, <bellucda@tiscali.it>
 *
 * More information about the hardware related to this driver can be found  
 * at our website:    http://www.quicknet.net
 *
 * IN NO EVENT SHALL QUICKNET TECHNOLOGIES, INC. BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
 * OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF QUICKNET
 * TECHNOLOGIES, INC. HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *    
 * QUICKNET TECHNOLOGIES, INC. SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND QUICKNET TECHNOLOGIES, INC. HAS NO OBLIGATION
 * TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 ***************************************************************************/

/*
 * $Log: ixj.c,v $
 *
 * Revision 4.8  2003/07/09 19:39:00  Daniele Bellucci
 * Audit some copy_*_user and minor cleanup.
 *
 * Revision 4.7  2001/08/13 06:19:33  craigs
 * Added additional changes from Alan Cox and John Anderson for
 * 2.2 to 2.4 cleanup and bounds checking
 *
 * Revision 4.6  2001/08/13 01:05:05  craigs
 * Really fixed PHONE_QUERY_CODEC problem this time
 *
 * Revision 4.5  2001/08/13 00:11:03  craigs
 * Fixed problem in handling of PHONE_QUERY_CODEC, thanks to Shane Anderson
 *
 * Revision 4.4  2001/08/07 07:58:12  craigs
 * Changed back to three digit version numbers
 * Added tagbuild target to allow automatic and easy tagging of versions
 *
 * Revision 4.3  2001/08/07 07:24:47  craigs
 * Added ixj-ver.h to allow easy configuration management of driver
 * Added display of version number in /prox/ixj
 *
 * Revision 4.2  2001/08/06 07:07:19  craigs
 * Reverted IXJCTL_DSP_TYPE and IXJCTL_DSP_VERSION files to original
 * behaviour of returning int rather than short *
 *
 * Revision 4.1  2001/08/05 00:17:37  craigs
 * More changes for correct PCMCIA installation
 * Start of changes for backward Linux compatibility
 *
 * Revision 4.0  2001/08/04 12:33:12  craigs
 * New version using GNU autoconf
 *
 * Revision 3.105  2001/07/20 23:14:32  eokerson
 * More work on CallerID generation when using ring cadences.
 *
 * Revision 3.104  2001/07/06 01:33:55  eokerson
 * Some bugfixes from Robert Vojta <vojta@ipex.cz> and a few mods to the Makefile.
 *
 * Revision 3.103  2001/07/05 19:20:16  eokerson
 * Updated HOWTO
 * Changed mic gain to 30dB on Internet LineJACK mic/speaker port.
 *
 * Revision 3.102  2001/07/03 23:51:21  eokerson
 * Un-mute mic on Internet LineJACK when in speakerphone mode.
 *
 * Revision 3.101  2001/07/02 19:26:56  eokerson
 * Removed initialiazation of ixjdebug and ixj_convert_loaded so they will go in the .bss instead of the .data
 *
 * Revision 3.100  2001/07/02 19:18:27  eokerson
 * Changed driver to make dynamic allocation possible.  We now pass IXJ * between functions instead of array indexes.
 * Fixed the way the POTS and PSTN ports interact during a PSTN call to allow local answering.
 * Fixed speaker mode on Internet LineJACK.
 *
 * Revision 3.99  2001/05/09 14:11:16  eokerson
 * Fixed kmalloc error in ixj_build_filter_cadence.  Thanks David Chan <cat@waulogy.stanford.edu>.
 *
 * Revision 3.98  2001/05/08 19:55:33  eokerson
 * Fixed POTS hookstate detection while it is connected to PSTN port.
 *
 * Revision 3.97  2001/05/08 00:01:04  eokerson
 * Fixed kernel oops when sending caller ID data.
 *
 * Revision 3.96  2001/05/04 23:09:30  eokerson
 * Now uses one kernel timer for each card, instead of one for the entire driver.
 *
 * Revision 3.95  2001/04/25 22:06:47  eokerson
 * Fixed squawking at beginning of some G.723.1 calls.
 *
 * Revision 3.94  2001/04/03 23:42:00  eokerson
 * Added linear volume ioctls
 * Added raw filter load ioctl
 *
 * Revision 3.93  2001/02/27 01:00:06  eokerson
 * Fixed blocking in CallerID.
 * Reduced size of ixj structure for smaller driver footprint.
 *
 * Revision 3.92  2001/02/20 22:02:59  eokerson
 * Fixed isapnp and pcmcia module compatibility for 2.4.x kernels.
 * Improved PSTN ring detection.
 * Fixed wink generation on POTS ports.
 *
 * Revision 3.91  2001/02/13 00:55:44  eokerson
 * Turn AEC back on after changing frame sizes.
 *
 * Revision 3.90  2001/02/12 16:42:00  eokerson
 * Added ALAW codec, thanks to Fabio Ferrari for the table based converters to make ALAW from ULAW.
 *
 * Revision 3.89  2001/02/12 15:41:16  eokerson
 * Fix from Artis Kugevics - Tone gains were not being set correctly.
 *
 * Revision 3.88  2001/02/05 23:25:42  eokerson
 * Fixed lockup bugs with deregister.
 *
 * Revision 3.87  2001/01/29 21:00:39  eokerson
 * Fix from Fabio Ferrari <fabio.ferrari@digitro.com.br> to properly handle EAGAIN and EINTR during non-blocking write.
 * Updated copyright date.
 *
 * Revision 3.86  2001/01/23 23:53:46  eokerson
 * Fixes to G.729 compatibility.
 *
 * Revision 3.85  2001/01/23 21:30:36  eokerson
 * Added verbage about cards supported.
 * Removed commands that put the card in low power mode at some times that it should not be in low power mode.
 *
 * Revision 3.84  2001/01/22 23:32:10  eokerson
 * Some bugfixes from David Huggins-Daines, <dhd@cepstral.com> and other cleanups.
 *
 * Revision 3.83  2001/01/19 14:51:41  eokerson
 * Fixed ixj_WriteDSPCommand to decrement usage counter when command fails.
 *
 * Revision 3.82  2001/01/19 00:34:49  eokerson
 * Added verbosity to write overlap errors.
 *
 * Revision 3.81  2001/01/18 23:56:54  eokerson
 * Fixed PSTN line test functions.
 *
 * Revision 3.80  2001/01/18 22:29:27  eokerson
 * Updated AEC/AGC values for different cards.
 *
 * Revision 3.79  2001/01/17 02:58:54  eokerson
 * Fixed AEC reset after Caller ID.
 * Fixed Codec lockup after Caller ID on Call Waiting when not using 30ms frames.
 *
 * Revision 3.78  2001/01/16 19:43:09  eokerson
 * Added support for Linux 2.4.x kernels.
 *
 * Revision 3.77  2001/01/09 04:00:52  eokerson
 * Linetest will now test the line, even if it has previously succeded.
 *
 * Revision 3.76  2001/01/08 19:27:00  eokerson
 * Fixed problem with standard cable on Internet PhoneCARD.
 *
 * Revision 3.75  2000/12/22 16:52:14  eokerson
 * Modified to allow hookstate detection on the POTS port when the PSTN port is selected.
 *
 * Revision 3.74  2000/12/08 22:41:50  eokerson
 * Added capability for G729B.
 *
 * Revision 3.73  2000/12/07 23:35:16  eokerson
 * Added capability to have different ring pattern before CallerID data.
 * Added hookstate checks in CallerID routines to stop FSK.
 *
 * Revision 3.72  2000/12/06 19:31:31  eokerson
 * Modified signal behavior to only send one signal per event.
 *
 * Revision 3.71  2000/12/06 03:23:08  eokerson
 * Fixed CallerID on Call Waiting.
 *
 * Revision 3.70  2000/12/04 21:29:37  eokerson
 * Added checking to Smart Cable gain functions.
 *
 * Revision 3.69  2000/12/04 21:05:20  eokerson
 * Changed ixjdebug levels.
 * Added ioctls to change gains in Internet Phone CARD Smart Cable.
 *
 * Revision 3.68  2000/12/04 00:17:21  craigs
 * Changed mixer voice gain to +6dB rather than 0dB
 *
 * Revision 3.67  2000/11/30 21:25:51  eokerson
 * Fixed write signal errors.
 *
 * Revision 3.66  2000/11/29 22:42:44  eokerson
 * Fixed PSTN ring detect problems.
 *
 * Revision 3.65  2000/11/29 07:31:55  craigs
 * Added new 425Hz filter co-efficients
 * Added card-specific DTMF prescaler initialisation
 *
 * Revision 3.64  2000/11/28 14:03:32  craigs
 * Changed certain mixer initialisations to be 0dB rather than 12dB
 * Added additional information to /proc/ixj
 *
 * Revision 3.63  2000/11/28 11:38:41  craigs
 * Added display of AEC modes in AUTO and AGC mode
 *
 * Revision 3.62  2000/11/28 04:05:44  eokerson
 * Improved PSTN ring detection routine.
 *
 * Revision 3.61  2000/11/27 21:53:12  eokerson
 * Fixed flash detection.
 *
 * Revision 3.60  2000/11/27 15:57:29  eokerson
 * More work on G.729 load routines.
 *
 * Revision 3.59  2000/11/25 21:55:12  eokerson
 * Fixed errors in G.729 load routine.
 *
 * Revision 3.58  2000/11/25 04:08:29  eokerson
 * Added board locks around G.729 and TS85 load routines.
 *
 * Revision 3.57  2000/11/24 05:35:17  craigs
 * Added ability to retrieve mixer values on LineJACK
 * Added complete initialisation of all mixer values at startup
 * Fixed spelling mistake
 *
 * Revision 3.56  2000/11/23 02:52:11  robertj
 * Added cvs change log keyword.
 * Fixed bug in capabilities list when using G.729 module.
 *
 */

#include "ixj-ver.h"

#define PERFMON_STATS
#define IXJDEBUG 0
#define MAXRINGS 5

#include <linux/module.h>

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include <linux/isapnp.h>

#include "ixj.h"

#define TYPE(inode) (iminor(inode) >> 4)
#define NUM(inode) (iminor(inode) & 0xf)

static int ixjdebug;
static int hertz = HZ;
static int samplerate = 100;

module_param(ixjdebug, int, 0);

static struct pci_device_id ixj_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_QUICKNET, PCI_DEVICE_ID_QUICKNET_XJ,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ }
};

MODULE_DEVICE_TABLE(pci, ixj_pci_tbl);

/************************************************************************
*
* ixjdebug meanings are now bit mapped instead of level based
* Values can be or'ed together to turn on multiple messages
*
* bit  0 (0x0001) = any failure
* bit  1 (0x0002) = general messages
* bit  2 (0x0004) = POTS ringing related
* bit  3 (0x0008) = PSTN events
* bit  4 (0x0010) = PSTN Cadence state details
* bit  5 (0x0020) = Tone detection triggers
* bit  6 (0x0040) = Tone detection cadence details
* bit  7 (0x0080) = ioctl tracking
* bit  8 (0x0100) = signal tracking
* bit  9 (0x0200) = CallerID generation details
*
************************************************************************/

#ifdef IXJ_DYN_ALLOC

static IXJ *ixj[IXJMAX];
#define	get_ixj(b)	ixj[(b)]

/*
 *	Allocate a free IXJ device
 */
 
static IXJ *ixj_alloc()
{
	for(cnt=0; cnt<IXJMAX; cnt++)
	{
		if(ixj[cnt] == NULL || !ixj[cnt]->DSPbase)
		{
			j = kmalloc(sizeof(IXJ), GFP_KERNEL);
			if (j == NULL)
				return NULL;
			ixj[cnt] = j;
			return j;
		}
	}
	return NULL;
}

static void ixj_fsk_free(IXJ *j)
{
	kfree(j->fskdata);
	j->fskdata = NULL;
}

static void ixj_fsk_alloc(IXJ *j)
{
	if(!j->fskdata) {
		j->fskdata = kmalloc(8000, GFP_KERNEL);
		if (!j->fskdata) {
			if(ixjdebug & 0x0200) {
				printk("IXJ phone%d - allocate failed\n", j->board);
			}
			return;
		} else {
			j->fsksize = 8000;
			if(ixjdebug & 0x0200) {
				printk("IXJ phone%d - allocate succeded\n", j->board);
			}
		}
	}
}

#else

static IXJ ixj[IXJMAX];
#define	get_ixj(b)	(&ixj[(b)])

/*
 *	Allocate a free IXJ device
 */
 
static IXJ *ixj_alloc(void)
{
	int cnt;
	for(cnt=0; cnt<IXJMAX; cnt++) {
		if(!ixj[cnt].DSPbase)
			return &ixj[cnt];
	}
	return NULL;
}

static inline void ixj_fsk_free(IXJ *j) {;}

static inline void ixj_fsk_alloc(IXJ *j)
{
	j->fsksize = 8000;
}

#endif

#ifdef PERFMON_STATS
#define ixj_perfmon(x)	((x)++)
#else
#define ixj_perfmon(x)	do { } while(0)
#endif

static int ixj_convert_loaded;

static int ixj_WriteDSPCommand(unsigned short, IXJ *j);

/************************************************************************
*
* These are function definitions to allow external modules to register
* enhanced functionality call backs.
*
************************************************************************/

static int Stub(IXJ * J, unsigned long arg)
{
	return 0;
}

static IXJ_REGFUNC ixj_PreRead = &Stub;
static IXJ_REGFUNC ixj_PostRead = &Stub;
static IXJ_REGFUNC ixj_PreWrite = &Stub;
static IXJ_REGFUNC ixj_PostWrite = &Stub;

static void ixj_read_frame(IXJ *j);
static void ixj_write_frame(IXJ *j);
static void ixj_init_timer(IXJ *j);
static void ixj_add_timer(IXJ *	j);
static void ixj_timeout(unsigned long ptr);
static int read_filters(IXJ *j);
static int LineMonitor(IXJ *j);
static int ixj_fasync(int fd, struct file *, int mode);
static int ixj_set_port(IXJ *j, int arg);
static int ixj_set_pots(IXJ *j, int arg);
static int ixj_hookstate(IXJ *j);
static int ixj_record_start(IXJ *j);
static void ixj_record_stop(IXJ *j);
static void set_rec_volume(IXJ *j, int volume);
static int get_rec_volume(IXJ *j);
static int set_rec_codec(IXJ *j, int rate);
static void ixj_vad(IXJ *j, int arg);
static int ixj_play_start(IXJ *j);
static void ixj_play_stop(IXJ *j);
static int ixj_set_tone_on(unsigned short arg, IXJ *j);
static int ixj_set_tone_off(unsigned short, IXJ *j);
static int ixj_play_tone(IXJ *j, char tone);
static void ixj_aec_start(IXJ *j, int level);
static int idle(IXJ *j);
static void ixj_ring_on(IXJ *j);
static void ixj_ring_off(IXJ *j);
static void aec_stop(IXJ *j);
static void ixj_ringback(IXJ *j);
static void ixj_busytone(IXJ *j);
static void ixj_dialtone(IXJ *j);
static void ixj_cpt_stop(IXJ *j);
static char daa_int_read(IXJ *j);
static char daa_CR_read(IXJ *j, int cr);
static int daa_set_mode(IXJ *j, int mode);
static int ixj_linetest(IXJ *j);
static int ixj_daa_write(IXJ *j);
static int ixj_daa_cid_read(IXJ *j);
static void DAA_Coeff_US(IXJ *j);
static void DAA_Coeff_UK(IXJ *j);
static void DAA_Coeff_France(IXJ *j);
static void DAA_Coeff_Germany(IXJ *j);
static void DAA_Coeff_Australia(IXJ *j);
static void DAA_Coeff_Japan(IXJ *j);
static int ixj_init_filter(IXJ *j, IXJ_FILTER * jf);
static int ixj_init_filter_raw(IXJ *j, IXJ_FILTER_RAW * jfr);
static int ixj_init_tone(IXJ *j, IXJ_TONE * ti);
static int ixj_build_cadence(IXJ *j, IXJ_CADENCE __user * cp);
static int ixj_build_filter_cadence(IXJ *j, IXJ_FILTER_CADENCE __user * cp);
/* Serial Control Interface funtions */
static int SCI_Control(IXJ *j, int control);
static int SCI_Prepare(IXJ *j);
static int SCI_WaitHighSCI(IXJ *j);
static int SCI_WaitLowSCI(IXJ *j);
static DWORD PCIEE_GetSerialNumber(WORD wAddress);
static int ixj_PCcontrol_wait(IXJ *j);
static void ixj_pre_cid(IXJ *j);
static void ixj_write_cid(IXJ *j);
static void ixj_write_cid_bit(IXJ *j, int bit);
static int set_base_frame(IXJ *j, int size);
static int set_play_codec(IXJ *j, int rate);
static void set_rec_depth(IXJ *j, int depth);
static int ixj_mixer(long val, IXJ *j);

/************************************************************************
CT8020/CT8021 Host Programmers Model
Host address	Function					Access
DSPbase +
0-1		Aux Software Status Register (reserved)		Read Only
2-3		Software Status Register			Read Only
4-5		Aux Software Control Register (reserved)	Read Write
6-7		Software Control Register			Read Write
8-9		Hardware Status Register			Read Only
A-B		Hardware Control Register			Read Write
C-D Host Transmit (Write) Data Buffer Access Port (buffer input)Write Only
E-F Host Recieve (Read) Data Buffer Access Port (buffer input)	Read Only
************************************************************************/

static inline void ixj_read_HSR(IXJ *j)
{
	j->hsr.bytes.low = inb_p(j->DSPbase + 8);
	j->hsr.bytes.high = inb_p(j->DSPbase + 9);
}

static inline int IsControlReady(IXJ *j)
{
	ixj_read_HSR(j);
	return j->hsr.bits.controlrdy ? 1 : 0;
}

static inline int IsPCControlReady(IXJ *j)
{
	j->pccr1.byte = inb_p(j->XILINXbase + 3);
	return j->pccr1.bits.crr ? 1 : 0;
}

static inline int IsStatusReady(IXJ *j)
{
	ixj_read_HSR(j);
	return j->hsr.bits.statusrdy ? 1 : 0;
}

static inline int IsRxReady(IXJ *j)
{
	ixj_read_HSR(j);
	ixj_perfmon(j->rxreadycheck);
	return j->hsr.bits.rxrdy ? 1 : 0;
}

static inline int IsTxReady(IXJ *j)
{
	ixj_read_HSR(j);
	ixj_perfmon(j->txreadycheck);
	return j->hsr.bits.txrdy ? 1 : 0;
}

static inline void set_play_volume(IXJ *j, int volume)
{
	if (ixjdebug & 0x0002)
		printk(KERN_INFO "IXJ: /dev/phone%d Setting Play Volume to 0x%4.4x\n", j->board, volume);
	ixj_WriteDSPCommand(0xCF02, j);
	ixj_WriteDSPCommand(volume, j);
}

static int set_play_volume_linear(IXJ *j, int volume)
{
	int newvolume, dspplaymax;

	if (ixjdebug & 0x0002)
		printk(KERN_INFO "IXJ: /dev/phone %d Setting Linear Play Volume to 0x%4.4x\n", j->board, volume);
	if(volume > 100 || volume < 0) {
		return -1;
	}

	/* This should normalize the perceived volumes between the different cards caused by differences in the hardware */
	switch (j->cardtype) {
	case QTI_PHONEJACK:
		dspplaymax = 0x380;
		break;
	case QTI_LINEJACK:
		if(j->port == PORT_PSTN) {
			dspplaymax = 0x48;
		} else {
			dspplaymax = 0x100;
		}
		break;
	case QTI_PHONEJACK_LITE:
		dspplaymax = 0x380;
		break;
	case QTI_PHONEJACK_PCI:
		dspplaymax = 0x6C;
		break;
	case QTI_PHONECARD:
		dspplaymax = 0x50;
		break;
	default:
		return -1;
	}
	newvolume = (dspplaymax * volume) / 100;
	set_play_volume(j, newvolume);
	return 0;
}

static inline void set_play_depth(IXJ *j, int depth)
{
	if (depth > 60)
		depth = 60;
	if (depth < 0)
		depth = 0;
	ixj_WriteDSPCommand(0x5280 + depth, j);
}

static inline int get_play_volume(IXJ *j)
{
	ixj_WriteDSPCommand(0xCF00, j);
	return j->ssr.high << 8 | j->ssr.low;
}

static int get_play_volume_linear(IXJ *j)
{
	int volume, newvolume, dspplaymax;

	/* This should normalize the perceived volumes between the different cards caused by differences in the hardware */
	switch (j->cardtype) {
	case QTI_PHONEJACK:
		dspplaymax = 0x380;
		break;
	case QTI_LINEJACK:
		if(j->port == PORT_PSTN) {
			dspplaymax = 0x48;
		} else {
			dspplaymax = 0x100;
		}
		break;
	case QTI_PHONEJACK_LITE:
		dspplaymax = 0x380;
		break;
	case QTI_PHONEJACK_PCI:
		dspplaymax = 0x6C;
		break;
	case QTI_PHONECARD:
		dspplaymax = 100;
		break;
	default:
		return -1;
	}
	volume = get_play_volume(j);
	newvolume = (volume * 100) / dspplaymax;
	if(newvolume > 100)
		newvolume = 100;
	return newvolume;
}

static inline BYTE SLIC_GetState(IXJ *j)
{
	if (j->cardtype == QTI_PHONECARD) {
		j->pccr1.byte = 0;
		j->psccr.bits.dev = 3;
		j->psccr.bits.rw = 1;
		outw_p(j->psccr.byte << 8, j->XILINXbase + 0x00);
		ixj_PCcontrol_wait(j);
		j->pslic.byte = inw_p(j->XILINXbase + 0x00) & 0xFF;
		ixj_PCcontrol_wait(j);
		if (j->pslic.bits.powerdown)
			return PLD_SLIC_STATE_OC;
		else if (!j->pslic.bits.ring0 && !j->pslic.bits.ring1)
			return PLD_SLIC_STATE_ACTIVE;
		else
			return PLD_SLIC_STATE_RINGING;
	} else {
		j->pld_slicr.byte = inb_p(j->XILINXbase + 0x01);
	}
	return j->pld_slicr.bits.state;
}

static bool SLIC_SetState(BYTE byState, IXJ *j)
{
	bool fRetVal = false;

	if (j->cardtype == QTI_PHONECARD) {
		if (j->flags.pcmciasct) {
			switch (byState) {
			case PLD_SLIC_STATE_TIPOPEN:
			case PLD_SLIC_STATE_OC:
				j->pslic.bits.powerdown = 1;
				j->pslic.bits.ring0 = j->pslic.bits.ring1 = 0;
				fRetVal = true;
				break;
			case PLD_SLIC_STATE_RINGING:
				if (j->readers || j->writers) {
					j->pslic.bits.powerdown = 0;
					j->pslic.bits.ring0 = 1;
					j->pslic.bits.ring1 = 0;
					fRetVal = true;
				}
				break;
			case PLD_SLIC_STATE_OHT:	/* On-hook transmit */

			case PLD_SLIC_STATE_STANDBY:
			case PLD_SLIC_STATE_ACTIVE:
				if (j->readers || j->writers) {
					j->pslic.bits.powerdown = 0;
				} else {
					j->pslic.bits.powerdown = 1;
				}
				j->pslic.bits.ring0 = j->pslic.bits.ring1 = 0;
				fRetVal = true;
				break;
			case PLD_SLIC_STATE_APR:	/* Active polarity reversal */

			case PLD_SLIC_STATE_OHTPR:	/* OHT polarity reversal */

			default:
				fRetVal = false;
				break;
			}
			j->psccr.bits.dev = 3;
			j->psccr.bits.rw = 0;
			outw_p(j->psccr.byte << 8 | j->pslic.byte, j->XILINXbase + 0x00);
			ixj_PCcontrol_wait(j);
		}
	} else {
		/* Set the C1, C2, C3 & B2EN signals. */
		switch (byState) {
		case PLD_SLIC_STATE_OC:
			j->pld_slicw.bits.c1 = 0;
			j->pld_slicw.bits.c2 = 0;
			j->pld_slicw.bits.c3 = 0;
			j->pld_slicw.bits.b2en = 0;
			outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
			fRetVal = true;
			break;
		case PLD_SLIC_STATE_RINGING:
			j->pld_slicw.bits.c1 = 1;
			j->pld_slicw.bits.c2 = 0;
			j->pld_slicw.bits.c3 = 0;
			j->pld_slicw.bits.b2en = 1;
			outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
			fRetVal = true;
			break;
		case PLD_SLIC_STATE_ACTIVE:
			j->pld_slicw.bits.c1 = 0;
			j->pld_slicw.bits.c2 = 1;
			j->pld_slicw.bits.c3 = 0;
			j->pld_slicw.bits.b2en = 0;
			outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
			fRetVal = true;
			break;
		case PLD_SLIC_STATE_OHT:	/* On-hook transmit */

			j->pld_slicw.bits.c1 = 1;
			j->pld_slicw.bits.c2 = 1;
			j->pld_slicw.bits.c3 = 0;
			j->pld_slicw.bits.b2en = 0;
			outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
			fRetVal = true;
			break;
		case PLD_SLIC_STATE_TIPOPEN:
			j->pld_slicw.bits.c1 = 0;
			j->pld_slicw.bits.c2 = 0;
			j->pld_slicw.bits.c3 = 1;
			j->pld_slicw.bits.b2en = 0;
			outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
			fRetVal = true;
			break;
		case PLD_SLIC_STATE_STANDBY:
			j->pld_slicw.bits.c1 = 1;
			j->pld_slicw.bits.c2 = 0;
			j->pld_slicw.bits.c3 = 1;
			j->pld_slicw.bits.b2en = 1;
			outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
			fRetVal = true;
			break;
		case PLD_SLIC_STATE_APR:	/* Active polarity reversal */

			j->pld_slicw.bits.c1 = 0;
			j->pld_slicw.bits.c2 = 1;
			j->pld_slicw.bits.c3 = 1;
			j->pld_slicw.bits.b2en = 0;
			outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
			fRetVal = true;
			break;
		case PLD_SLIC_STATE_OHTPR:	/* OHT polarity reversal */

			j->pld_slicw.bits.c1 = 1;
			j->pld_slicw.bits.c2 = 1;
			j->pld_slicw.bits.c3 = 1;
			j->pld_slicw.bits.b2en = 0;
			outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
			fRetVal = true;
			break;
		default:
			fRetVal = false;
			break;
		}
	}

	return fRetVal;
}

static int ixj_wink(IXJ *j)
{
	BYTE slicnow;

	slicnow = SLIC_GetState(j);

	j->pots_winkstart = jiffies;
	SLIC_SetState(PLD_SLIC_STATE_OC, j);

	msleep(jiffies_to_msecs(j->winktime));

	SLIC_SetState(slicnow, j);
	return 0;
}

static void ixj_init_timer(IXJ *j)
{
	init_timer(&j->timer);
	j->timer.function = ixj_timeout;
	j->timer.data = (unsigned long)j;
}

static void ixj_add_timer(IXJ *j)
{
	j->timer.expires = jiffies + (hertz / samplerate);
	add_timer(&j->timer);
}

static void ixj_tone_timeout(IXJ *j)
{
	IXJ_TONE ti;

	j->tone_state++;
	if (j->tone_state == 3) {
		j->tone_state = 0;
		if (j->cadence_t) {
			j->tone_cadence_state++;
			if (j->tone_cadence_state >= j->cadence_t->elements_used) {
				switch (j->cadence_t->termination) {
				case PLAY_ONCE:
					ixj_cpt_stop(j);
					break;
				case REPEAT_LAST_ELEMENT:
					j->tone_cadence_state--;
					ixj_play_tone(j, j->cadence_t->ce[j->tone_cadence_state].index);
					break;
				case REPEAT_ALL:
					j->tone_cadence_state = 0;
					if (j->cadence_t->ce[j->tone_cadence_state].freq0) {
						ti.tone_index = j->cadence_t->ce[j->tone_cadence_state].index;
						ti.freq0 = j->cadence_t->ce[j->tone_cadence_state].freq0;
						ti.gain0 = j->cadence_t->ce[j->tone_cadence_state].gain0;
						ti.freq1 = j->cadence_t->ce[j->tone_cadence_state].freq1;
						ti.gain1 = j->cadence_t->ce[j->tone_cadence_state].gain1;
						ixj_init_tone(j, &ti);
					}
					ixj_set_tone_on(j->cadence_t->ce[0].tone_on_time, j);
					ixj_set_tone_off(j->cadence_t->ce[0].tone_off_time, j);
					ixj_play_tone(j, j->cadence_t->ce[0].index);
					break;
				}
			} else {
				if (j->cadence_t->ce[j->tone_cadence_state].gain0) {
					ti.tone_index = j->cadence_t->ce[j->tone_cadence_state].index;
					ti.freq0 = j->cadence_t->ce[j->tone_cadence_state].freq0;
					ti.gain0 = j->cadence_t->ce[j->tone_cadence_state].gain0;
					ti.freq1 = j->cadence_t->ce[j->tone_cadence_state].freq1;
					ti.gain1 = j->cadence_t->ce[j->tone_cadence_state].gain1;
					ixj_init_tone(j, &ti);
				}
				ixj_set_tone_on(j->cadence_t->ce[j->tone_cadence_state].tone_on_time, j);
				ixj_set_tone_off(j->cadence_t->ce[j->tone_cadence_state].tone_off_time, j);
				ixj_play_tone(j, j->cadence_t->ce[j->tone_cadence_state].index);
			}
		}
	}
}

static inline void ixj_kill_fasync(IXJ *j, IXJ_SIGEVENT event, int dir)
{
	if(j->ixj_signals[event]) {
		if(ixjdebug & 0x0100)
			printk("Sending signal for event %d\n", event);
			/* Send apps notice of change */
		/* see config.h for macro definition */
		kill_fasync(&(j->async_queue), j->ixj_signals[event], dir);
	}
}

static void ixj_pstn_state(IXJ *j)
{
	int var;
	union XOPXR0 XR0, daaint;

	var = 10;

	XR0.reg = j->m_DAAShadowRegs.XOP_REGS.XOP.xr0.reg;
	daaint.reg = 0;
	XR0.bitreg.RMR = j->m_DAAShadowRegs.SOP_REGS.SOP.cr1.bitreg.RMR;

	j->pld_scrr.byte = inb_p(j->XILINXbase);
	if (j->pld_scrr.bits.daaflag) {
		daa_int_read(j);
		if(j->m_DAAShadowRegs.XOP_REGS.XOP.xr0.bitreg.RING) {
			if(time_after(jiffies, j->pstn_sleeptil) && !(j->flags.pots_pstn && j->hookstate)) {
				daaint.bitreg.RING = 1;
				if(ixjdebug & 0x0008) {
					printk(KERN_INFO "IXJ DAA Ring Interrupt /dev/phone%d at %ld\n", j->board, jiffies);
				}
			} else {
				daa_set_mode(j, SOP_PU_RESET);
			}
		}
		if(j->m_DAAShadowRegs.XOP_REGS.XOP.xr0.bitreg.Caller_ID) {
			daaint.bitreg.Caller_ID = 1;
			j->pstn_cid_intr = 1;
			j->pstn_cid_received = jiffies;
			if(ixjdebug & 0x0008) {
				printk(KERN_INFO "IXJ DAA Caller_ID Interrupt /dev/phone%d at %ld\n", j->board, jiffies);
			}
		}
		if(j->m_DAAShadowRegs.XOP_REGS.XOP.xr0.bitreg.Cadence) {
			daaint.bitreg.Cadence = 1;
			if(ixjdebug & 0x0008) {
				printk(KERN_INFO "IXJ DAA Cadence Interrupt /dev/phone%d at %ld\n", j->board, jiffies);
			}
		}
		if(j->m_DAAShadowRegs.XOP_REGS.XOP.xr0.bitreg.VDD_OK != XR0.bitreg.VDD_OK) {
			daaint.bitreg.VDD_OK = 1;
			daaint.bitreg.SI_0 = j->m_DAAShadowRegs.XOP_REGS.XOP.xr0.bitreg.VDD_OK;
		}
	}
	daa_CR_read(j, 1);
	if(j->m_DAAShadowRegs.SOP_REGS.SOP.cr1.bitreg.RMR != XR0.bitreg.RMR && time_after(jiffies, j->pstn_sleeptil) && !(j->flags.pots_pstn && j->hookstate)) {
		daaint.bitreg.RMR = 1;
		daaint.bitreg.SI_1 = j->m_DAAShadowRegs.SOP_REGS.SOP.cr1.bitreg.RMR;
		if(ixjdebug & 0x0008) {
                        printk(KERN_INFO "IXJ DAA RMR /dev/phone%d was %s for %ld\n", j->board, XR0.bitreg.RMR?"on":"off", jiffies - j->pstn_last_rmr);
		}
		j->pstn_prev_rmr = j->pstn_last_rmr;
		j->pstn_last_rmr = jiffies;
	}
	switch(j->daa_mode) {
		case SOP_PU_SLEEP:
			if (daaint.bitreg.RING) {
				if (!j->flags.pstn_ringing) {
					if (j->daa_mode != SOP_PU_RINGING) {
						j->pstn_ring_int = jiffies;
						daa_set_mode(j, SOP_PU_RINGING);
					}
				}
			}
			break;
		case SOP_PU_RINGING:
			if (daaint.bitreg.RMR) {
				if (ixjdebug & 0x0008) {
					printk(KERN_INFO "IXJ Ring Cadence a state = %d /dev/phone%d at %ld\n", j->cadence_f[4].state, j->board, jiffies);
				}
				if (daaint.bitreg.SI_1) {                /* Rising edge of RMR */
					j->flags.pstn_rmr = 1;
					j->pstn_ring_start = jiffies;
					j->pstn_ring_stop = 0;
					j->ex.bits.pstn_ring = 0;
					if (j->cadence_f[4].state == 0) {
						j->cadence_f[4].state = 1;
						j->cadence_f[4].on1min = jiffies + (long)((j->cadence_f[4].on1 * hertz * (100 - var)) / 10000);
						j->cadence_f[4].on1dot = jiffies + (long)((j->cadence_f[4].on1 * hertz * (100)) / 10000);
						j->cadence_f[4].on1max = jiffies + (long)((j->cadence_f[4].on1 * hertz * (100 + var)) / 10000);
					} else if (j->cadence_f[4].state == 2) {
						if((time_after(jiffies, j->cadence_f[4].off1min) &&
						    time_before(jiffies, j->cadence_f[4].off1max))) {
							if (j->cadence_f[4].on2) {
								j->cadence_f[4].state = 3;
								j->cadence_f[4].on2min = jiffies + (long)((j->cadence_f[4].on2 * (hertz * (100 - var)) / 10000));
								j->cadence_f[4].on2dot = jiffies + (long)((j->cadence_f[4].on2 * (hertz * (100)) / 10000));
								j->cadence_f[4].on2max = jiffies + (long)((j->cadence_f[4].on2 * (hertz * (100 + var)) / 10000));
							} else {
								j->cadence_f[4].state = 7;
							}
						} else {
							if (ixjdebug & 0x0008) {
								printk(KERN_INFO "IXJ Ring Cadence fail state = %d /dev/phone%d at %ld should be %d\n",
										j->cadence_f[4].state, j->board, jiffies - j->pstn_prev_rmr,
										j->cadence_f[4].off1);
							}
							j->cadence_f[4].state = 0;
						}
					} else if (j->cadence_f[4].state == 4) {
						if((time_after(jiffies, j->cadence_f[4].off2min) &&
						    time_before(jiffies, j->cadence_f[4].off2max))) {
							if (j->cadence_f[4].on3) {
								j->cadence_f[4].state = 5;
								j->cadence_f[4].on3min = jiffies + (long)((j->cadence_f[4].on3 * (hertz * (100 - var)) / 10000));
								j->cadence_f[4].on3dot = jiffies + (long)((j->cadence_f[4].on3 * (hertz * (100)) / 10000));
								j->cadence_f[4].on3max = jiffies + (long)((j->cadence_f[4].on3 * (hertz * (100 + var)) / 10000));
							} else {
								j->cadence_f[4].state = 7;
							}
						} else {
							if (ixjdebug & 0x0008) {
								printk(KERN_INFO "IXJ Ring Cadence fail state = %d /dev/phone%d at %ld should be %d\n",
										j->cadence_f[4].state, j->board, jiffies - j->pstn_prev_rmr,
										j->cadence_f[4].off2);
							}
							j->cadence_f[4].state = 0;
						}
					} else if (j->cadence_f[4].state == 6) {
						if((time_after(jiffies, j->cadence_f[4].off3min) &&
						    time_before(jiffies, j->cadence_f[4].off3max))) {
							j->cadence_f[4].state = 7;
						} else {
							if (ixjdebug & 0x0008) {
								printk(KERN_INFO "IXJ Ring Cadence fail state = %d /dev/phone%d at %ld should be %d\n",
										j->cadence_f[4].state, j->board, jiffies - j->pstn_prev_rmr,
										j->cadence_f[4].off3);
							}
							j->cadence_f[4].state = 0;
						}
					} else {
						j->cadence_f[4].state = 0;
					}
				} else {                                /* Falling edge of RMR */
					j->pstn_ring_start = 0;
					j->pstn_ring_stop = jiffies;
					if (j->cadence_f[4].state == 1) {
						if(!j->cadence_f[4].on1) {
							j->cadence_f[4].state = 7;
						} else if((time_after(jiffies, j->cadence_f[4].on1min) &&
					          time_before(jiffies, j->cadence_f[4].on1max))) {
							if (j->cadence_f[4].off1) {
								j->cadence_f[4].state = 2;
								j->cadence_f[4].off1min = jiffies + (long)((j->cadence_f[4].off1 * (hertz * (100 - var)) / 10000));
								j->cadence_f[4].off1dot = jiffies + (long)((j->cadence_f[4].off1 * (hertz * (100)) / 10000));
								j->cadence_f[4].off1max = jiffies + (long)((j->cadence_f[4].off1 * (hertz * (100 + var)) / 10000));
							} else {
								j->cadence_f[4].state = 7;
							}
						} else {
							if (ixjdebug & 0x0008) {
								printk(KERN_INFO "IXJ Ring Cadence fail state = %d /dev/phone%d at %ld should be %d\n",
										j->cadence_f[4].state, j->board, jiffies - j->pstn_prev_rmr,
										j->cadence_f[4].on1);
							}
							j->cadence_f[4].state = 0;
						}
					} else if (j->cadence_f[4].state == 3) {
						if((time_after(jiffies, j->cadence_f[4].on2min) &&
						    time_before(jiffies, j->cadence_f[4].on2max))) {
							if (j->cadence_f[4].off2) {
								j->cadence_f[4].state = 4;
								j->cadence_f[4].off2min = jiffies + (long)((j->cadence_f[4].off2 * (hertz * (100 - var)) / 10000));
								j->cadence_f[4].off2dot = jiffies + (long)((j->cadence_f[4].off2 * (hertz * (100)) / 10000));
								j->cadence_f[4].off2max = jiffies + (long)((j->cadence_f[4].off2 * (hertz * (100 + var)) / 10000));
							} else {
								j->cadence_f[4].state = 7;
							}
						} else {
							if (ixjdebug & 0x0008) {
								printk(KERN_INFO "IXJ Ring Cadence fail state = %d /dev/phone%d at %ld should be %d\n",
										j->cadence_f[4].state, j->board, jiffies - j->pstn_prev_rmr,
										j->cadence_f[4].on2);
							}
							j->cadence_f[4].state = 0;
						}
					} else if (j->cadence_f[4].state == 5) {
						if((time_after(jiffies, j->cadence_f[4].on3min) &&
						    time_before(jiffies, j->cadence_f[4].on3max))) {
							if (j->cadence_f[4].off3) {
								j->cadence_f[4].state = 6;
								j->cadence_f[4].off3min = jiffies + (long)((j->cadence_f[4].off3 * (hertz * (100 - var)) / 10000));
								j->cadence_f[4].off3dot = jiffies + (long)((j->cadence_f[4].off3 * (hertz * (100)) / 10000));
								j->cadence_f[4].off3max = jiffies + (long)((j->cadence_f[4].off3 * (hertz * (100 + var)) / 10000));
							} else {
								j->cadence_f[4].state = 7;
							}
						} else {
							j->cadence_f[4].state = 0;
						}
					} else {
						if (ixjdebug & 0x0008) {
							printk(KERN_INFO "IXJ Ring Cadence fail state = %d /dev/phone%d at %ld should be %d\n",
									j->cadence_f[4].state, j->board, jiffies - j->pstn_prev_rmr,
									j->cadence_f[4].on3);
						}
						j->cadence_f[4].state = 0;
					}
				}
				if (ixjdebug & 0x0010) {
					printk(KERN_INFO "IXJ Ring Cadence b state = %d /dev/phone%d at %ld\n", j->cadence_f[4].state, j->board, jiffies);
				}
				if (ixjdebug & 0x0010) {
					switch(j->cadence_f[4].state) {
						case 1:
							printk(KERN_INFO "IXJ /dev/phone%d Next Ring Cadence state at %u min %ld - %ld - max %ld\n", j->board,
						j->cadence_f[4].on1, j->cadence_f[4].on1min, j->cadence_f[4].on1dot, j->cadence_f[4].on1max);
							break;
						case 2:
							printk(KERN_INFO "IXJ /dev/phone%d Next Ring Cadence state at %u min %ld - %ld - max %ld\n", j->board,
						j->cadence_f[4].off1, j->cadence_f[4].off1min, j->cadence_f[4].off1dot, j->cadence_f[4].off1max);
							break;
						case 3:
							printk(KERN_INFO "IXJ /dev/phone%d Next Ring Cadence state at %u min %ld - %ld - max %ld\n", j->board,
						j->cadence_f[4].on2, j->cadence_f[4].on2min, j->cadence_f[4].on2dot, j->cadence_f[4].on2max);
							break;
						case 4:
							printk(KERN_INFO "IXJ /dev/phone%d Next Ring Cadence state at %u min %ld - %ld - max %ld\n", j->board,
						j->cadence_f[4].off2, j->cadence_f[4].off2min, j->cadence_f[4].off2dot, j->cadence_f[4].off2max);
							break;
						case 5:
							printk(KERN_INFO "IXJ /dev/phone%d Next Ring Cadence state at %u min %ld - %ld - max %ld\n", j->board,
						j->cadence_f[4].on3, j->cadence_f[4].on3min, j->cadence_f[4].on3dot, j->cadence_f[4].on3max);
							break;
						case 6:	
							printk(KERN_INFO "IXJ /dev/phone%d Next Ring Cadence state at %u min %ld - %ld - max %ld\n", j->board,
						j->cadence_f[4].off3, j->cadence_f[4].off3min, j->cadence_f[4].off3dot, j->cadence_f[4].off3max);
							break;
					}
				}
			}
			if (j->cadence_f[4].state == 7) {
				j->cadence_f[4].state = 0;
				j->pstn_ring_stop = jiffies;
				j->ex.bits.pstn_ring = 1;
				ixj_kill_fasync(j, SIG_PSTN_RING, POLL_IN);
				if(ixjdebug & 0x0008) {
					printk(KERN_INFO "IXJ Ring int set /dev/phone%d at %ld\n", j->board, jiffies);
				}
			}
			if((j->pstn_ring_int != 0 && time_after(jiffies, j->pstn_ring_int + (hertz * 5)) && !j->flags.pstn_rmr) ||
			   (j->pstn_ring_stop != 0 && time_after(jiffies, j->pstn_ring_stop + (hertz * 5)))) {
				if(ixjdebug & 0x0008) {
					printk("IXJ DAA no ring in 5 seconds /dev/phone%d at %ld\n", j->board, jiffies);
					printk("IXJ DAA pstn ring int /dev/phone%d at %ld\n", j->board, j->pstn_ring_int);
					printk("IXJ DAA pstn ring stop /dev/phone%d at %ld\n", j->board, j->pstn_ring_stop);
				}
				j->pstn_ring_stop = j->pstn_ring_int = 0;
				daa_set_mode(j, SOP_PU_SLEEP);
			} 
			outb_p(j->pld_scrw.byte, j->XILINXbase);
			if (j->pstn_cid_intr && time_after(jiffies, j->pstn_cid_received + hertz)) {
				ixj_daa_cid_read(j);
				j->ex.bits.caller_id = 1;
				ixj_kill_fasync(j, SIG_CALLER_ID, POLL_IN);
				j->pstn_cid_intr = 0;
			}
			if (daaint.bitreg.Cadence) {
				if(ixjdebug & 0x0008) {
					printk("IXJ DAA Cadence interrupt going to sleep /dev/phone%d\n", j->board);
				}
				daa_set_mode(j, SOP_PU_SLEEP);
				j->ex.bits.pstn_ring = 0;
			}
			break;
		case SOP_PU_CONVERSATION:
			if (daaint.bitreg.VDD_OK) {
				if(!daaint.bitreg.SI_0) {
					if (!j->pstn_winkstart) {
						if(ixjdebug & 0x0008) {
							printk("IXJ DAA possible wink /dev/phone%d %ld\n", j->board, jiffies);
						}
						j->pstn_winkstart = jiffies;
					} 
				} else {
					if (j->pstn_winkstart) {
						if(ixjdebug & 0x0008) {
							printk("IXJ DAA possible wink end /dev/phone%d %ld\n", j->board, jiffies);
						}
						j->pstn_winkstart = 0;
					}
				}
			}
			if (j->pstn_winkstart && time_after(jiffies, j->pstn_winkstart + ((hertz * j->winktime) / 1000))) {
				if(ixjdebug & 0x0008) {
					printk("IXJ DAA wink detected going to sleep /dev/phone%d %ld\n", j->board, jiffies);
				}
				daa_set_mode(j, SOP_PU_SLEEP);
				j->pstn_winkstart = 0;
				j->ex.bits.pstn_wink = 1;
				ixj_kill_fasync(j, SIG_PSTN_WINK, POLL_IN);
			}
			break;
	}
}

static void ixj_timeout(unsigned long ptr)
{
	int board;
	unsigned long jifon;
	IXJ *j = (IXJ *)ptr;
	board = j->board;

	if (j->DSPbase && atomic_read(&j->DSPWrite) == 0 && test_and_set_bit(board, (void *)&j->busyflags) == 0) {
		ixj_perfmon(j->timerchecks);
		j->hookstate = ixj_hookstate(j);
		if (j->tone_state) {
			if (!(j->hookstate)) {
				ixj_cpt_stop(j);
				if (j->m_hook) {
					j->m_hook = 0;
					j->ex.bits.hookstate = 1;
					ixj_kill_fasync(j, SIG_HOOKSTATE, POLL_IN);
				}
				clear_bit(board, &j->busyflags);
				ixj_add_timer(j);
				return;
			}
			if (j->tone_state == 1)
				jifon = ((hertz * j->tone_on_time) * 25 / 100000);
			else
				jifon = ((hertz * j->tone_on_time) * 25 / 100000) + ((hertz * j->tone_off_time) * 25 / 100000);
			if (time_before(jiffies, j->tone_start_jif + jifon)) {
				if (j->tone_state == 1) {
					ixj_play_tone(j, j->tone_index);
					if (j->dsp.low == 0x20) {
						clear_bit(board, &j->busyflags);
						ixj_add_timer(j);
						return;
					}
				} else {
					ixj_play_tone(j, 0);
					if (j->dsp.low == 0x20) {
						clear_bit(board, &j->busyflags);
						ixj_add_timer(j);
						return;
					}
				}
			} else {
				ixj_tone_timeout(j);
				if (j->flags.dialtone) {
					ixj_dialtone(j);
				}
				if (j->flags.busytone) {
					ixj_busytone(j);
					if (j->dsp.low == 0x20) {
						clear_bit(board, &j->busyflags);
						ixj_add_timer(j);
						return;
					}
				}
				if (j->flags.ringback) {
					ixj_ringback(j);
					if (j->dsp.low == 0x20) {
						clear_bit(board, &j->busyflags);
						ixj_add_timer(j);
						return;
					}
				}
				if (!j->tone_state) {
					ixj_cpt_stop(j);
				}
			}
		}
		if (!(j->tone_state && j->dsp.low == 0x20)) {
			if (IsRxReady(j)) {
				ixj_read_frame(j);
			}
			if (IsTxReady(j)) {
				ixj_write_frame(j);
			}
		}
		if (j->flags.cringing) {
			if (j->hookstate & 1) {
				j->flags.cringing = 0;
				ixj_ring_off(j);
			} else if(j->cadence_f[5].enable && ((!j->cadence_f[5].en_filter) || (j->cadence_f[5].en_filter && j->flags.firstring))) {
				switch(j->cadence_f[5].state) {
					case 0:
						j->cadence_f[5].on1dot = jiffies + (long)((j->cadence_f[5].on1 * (hertz * 100) / 10000));
						if (time_before(jiffies, j->cadence_f[5].on1dot)) {
							if(ixjdebug & 0x0004) {
								printk("Ringing cadence state = %d - %ld\n", j->cadence_f[5].state, jiffies);
							}
							ixj_ring_on(j);
						}
						j->cadence_f[5].state = 1;
						break;
					case 1:
						if (time_after(jiffies, j->cadence_f[5].on1dot)) {
							j->cadence_f[5].off1dot = jiffies + (long)((j->cadence_f[5].off1 * (hertz * 100) / 10000));
							if(ixjdebug & 0x0004) {
								printk("Ringing cadence state = %d - %ld\n", j->cadence_f[5].state, jiffies);
							}
							ixj_ring_off(j);
							j->cadence_f[5].state = 2;
						}
						break;
					case 2:
						if (time_after(jiffies, j->cadence_f[5].off1dot)) {
							if(ixjdebug & 0x0004) {
								printk("Ringing cadence state = %d - %ld\n", j->cadence_f[5].state, jiffies);
							}
							ixj_ring_on(j);
							if (j->cadence_f[5].on2) {
								j->cadence_f[5].on2dot = jiffies + (long)((j->cadence_f[5].on2 * (hertz * 100) / 10000));
								j->cadence_f[5].state = 3;
							} else {
								j->cadence_f[5].state = 7;
							}
						}
						break;
					case 3:
						if (time_after(jiffies, j->cadence_f[5].on2dot)) {
							if(ixjdebug & 0x0004) {
								printk("Ringing cadence state = %d - %ld\n", j->cadence_f[5].state, jiffies);
							}
							ixj_ring_off(j);
							if (j->cadence_f[5].off2) {
								j->cadence_f[5].off2dot = jiffies + (long)((j->cadence_f[5].off2 * (hertz * 100) / 10000));
								j->cadence_f[5].state = 4;
							} else {
								j->cadence_f[5].state = 7;
							}
						}
						break;
					case 4:
						if (time_after(jiffies, j->cadence_f[5].off2dot)) {
							if(ixjdebug & 0x0004) {
								printk("Ringing cadence state = %d - %ld\n", j->cadence_f[5].state, jiffies);
							}
							ixj_ring_on(j);
							if (j->cadence_f[5].on3) {
								j->cadence_f[5].on3dot = jiffies + (long)((j->cadence_f[5].on3 * (hertz * 100) / 10000));
								j->cadence_f[5].state = 5;
							} else {
								j->cadence_f[5].state = 7;
							}
						}
						break;
					case 5:
						if (time_after(jiffies, j->cadence_f[5].on3dot)) {
							if(ixjdebug & 0x0004) {
								printk("Ringing cadence state = %d - %ld\n", j->cadence_f[5].state, jiffies);
							}
							ixj_ring_off(j);
							if (j->cadence_f[5].off3) {
								j->cadence_f[5].off3dot = jiffies + (long)((j->cadence_f[5].off3 * (hertz * 100) / 10000));
								j->cadence_f[5].state = 6;
							} else {
								j->cadence_f[5].state = 7;
							}
						}
						break;
					case 6:
						if (time_after(jiffies, j->cadence_f[5].off3dot)) {
							if(ixjdebug & 0x0004) {
								printk("Ringing cadence state = %d - %ld\n", j->cadence_f[5].state, jiffies);
							}
							j->cadence_f[5].state = 7;
						}
						break;
					case 7:
						if(ixjdebug & 0x0004) {
							printk("Ringing cadence state = %d - %ld\n", j->cadence_f[5].state, jiffies);
						}
						j->flags.cidring = 1;
						j->cadence_f[5].state = 0;
						break;
				}
				if (j->flags.cidring && !j->flags.cidsent) {
					j->flags.cidsent = 1;
					if(j->fskdcnt) {
						SLIC_SetState(PLD_SLIC_STATE_OHT, j);
						ixj_pre_cid(j);
					}
					j->flags.cidring = 0;
				}
				clear_bit(board, &j->busyflags);
				ixj_add_timer(j);
				return;
			} else {
				if (time_after(jiffies, j->ring_cadence_jif + (hertz / 2))) {
					if (j->flags.cidring && !j->flags.cidsent) {
						j->flags.cidsent = 1;
						if(j->fskdcnt) {
							SLIC_SetState(PLD_SLIC_STATE_OHT, j);
							ixj_pre_cid(j);
						}
						j->flags.cidring = 0;
					}
					j->ring_cadence_t--;
					if (j->ring_cadence_t == -1)
						j->ring_cadence_t = 15;
					j->ring_cadence_jif = jiffies;

					if (j->ring_cadence & 1 << j->ring_cadence_t) {
						if(j->flags.cidsent && j->cadence_f[5].en_filter)
							j->flags.firstring = 1;
						else
							ixj_ring_on(j);
					} else {
						ixj_ring_off(j);
						if(!j->flags.cidsent)
							j->flags.cidring = 1;
					}
				}
				clear_bit(board, &j->busyflags);
				ixj_add_timer(j);
				return;
			}
		}
		if (!j->flags.ringing) {
			if (j->hookstate) { /* & 1) { */
				if (j->dsp.low != 0x20 &&
				    SLIC_GetState(j) != PLD_SLIC_STATE_ACTIVE) {
					SLIC_SetState(PLD_SLIC_STATE_ACTIVE, j);
				}
				LineMonitor(j);
				read_filters(j);
				ixj_WriteDSPCommand(0x511B, j);
				j->proc_load = j->ssr.high << 8 | j->ssr.low;
				if (!j->m_hook && (j->hookstate & 1)) {
					j->m_hook = j->ex.bits.hookstate = 1;
					ixj_kill_fasync(j, SIG_HOOKSTATE, POLL_IN);
				}
			} else {
				if (j->ex.bits.dtmf_ready) {
					j->dtmf_wp = j->dtmf_rp = j->ex.bits.dtmf_ready = 0;
				}
				if (j->m_hook) {
					j->m_hook = 0;
					j->ex.bits.hookstate = 1;
					ixj_kill_fasync(j, SIG_HOOKSTATE, POLL_IN);
				}
			}
		}
		if (j->cardtype == QTI_LINEJACK && !j->flags.pstncheck && j->flags.pstn_present) {
			ixj_pstn_state(j);
		}
		if (j->ex.bytes) {
			wake_up_interruptible(&j->poll_q);	/* Wake any blocked selects */
		}
		clear_bit(board, &j->busyflags);
	}
	ixj_add_timer(j);
}

static int ixj_status_wait(IXJ *j)
{
	unsigned long jif;

	jif = jiffies + ((60 * hertz) / 100);
	while (!IsStatusReady(j)) {
		ixj_perfmon(j->statuswait);
		if (time_after(jiffies, jif)) {
			ixj_perfmon(j->statuswaitfail);
			return -1;
		}
	}
	return 0;
}

static int ixj_PCcontrol_wait(IXJ *j)
{
	unsigned long jif;

	jif = jiffies + ((60 * hertz) / 100);
	while (!IsPCControlReady(j)) {
		ixj_perfmon(j->pcontrolwait);
		if (time_after(jiffies, jif)) {
			ixj_perfmon(j->pcontrolwaitfail);
			return -1;
		}
	}
	return 0;
}

static int ixj_WriteDSPCommand(unsigned short cmd, IXJ *j)
{
	BYTES bytes;
	unsigned long jif;

	atomic_inc(&j->DSPWrite);
	if(atomic_read(&j->DSPWrite) > 1) {
		printk("IXJ %d DSP write overlap attempting command 0x%4.4x\n", j->board, cmd);
		return -1;
	}
	bytes.high = (cmd & 0xFF00) >> 8;
	bytes.low = cmd & 0x00FF;
	jif = jiffies + ((60 * hertz) / 100);
	while (!IsControlReady(j)) {
		ixj_perfmon(j->iscontrolready);
		if (time_after(jiffies, jif)) {
			ixj_perfmon(j->iscontrolreadyfail);
			atomic_dec(&j->DSPWrite);
			if(atomic_read(&j->DSPWrite) > 0) {
				printk("IXJ %d DSP overlaped command 0x%4.4x during control ready failure.\n", j->board, cmd);
				while(atomic_read(&j->DSPWrite) > 0) {
					atomic_dec(&j->DSPWrite);
				}
			}
			return -1;
		}
	}
	outb(bytes.low, j->DSPbase + 6);
	outb(bytes.high, j->DSPbase + 7);

	if (ixj_status_wait(j)) {
		j->ssr.low = 0xFF;
		j->ssr.high = 0xFF;
		atomic_dec(&j->DSPWrite);
		if(atomic_read(&j->DSPWrite) > 0) {
			printk("IXJ %d DSP overlaped command 0x%4.4x during status wait failure.\n", j->board, cmd);
			while(atomic_read(&j->DSPWrite) > 0) {
				atomic_dec(&j->DSPWrite);
			}
		}
		return -1;
	}
/* Read Software Status Register */
	j->ssr.low = inb_p(j->DSPbase + 2);
	j->ssr.high = inb_p(j->DSPbase + 3);
	atomic_dec(&j->DSPWrite);
	if(atomic_read(&j->DSPWrite) > 0) {
		printk("IXJ %d DSP overlaped command 0x%4.4x\n", j->board, cmd);
		while(atomic_read(&j->DSPWrite) > 0) {
			atomic_dec(&j->DSPWrite);
		}
	}
	return 0;
}

/***************************************************************************
*
*  General Purpose IO Register read routine
*
***************************************************************************/
static inline int ixj_gpio_read(IXJ *j)
{
	if (ixj_WriteDSPCommand(0x5143, j))
		return -1;

	j->gpio.bytes.low = j->ssr.low;
	j->gpio.bytes.high = j->ssr.high;

	return 0;
}

static inline void LED_SetState(int state, IXJ *j)
{
	if (j->cardtype == QTI_LINEJACK) {
		j->pld_scrw.bits.led1 = state & 0x1 ? 1 : 0;
		j->pld_scrw.bits.led2 = state & 0x2 ? 1 : 0;
		j->pld_scrw.bits.led3 = state & 0x4 ? 1 : 0;
		j->pld_scrw.bits.led4 = state & 0x8 ? 1 : 0;

		outb(j->pld_scrw.byte, j->XILINXbase);
	}
}

/*********************************************************************
*  GPIO Pins are configured as follows on the Quicknet Internet
*  PhoneJACK Telephony Cards
* 
* POTS Select        GPIO_6=0 GPIO_7=0
* Mic/Speaker Select GPIO_6=0 GPIO_7=1
* Handset Select     GPIO_6=1 GPIO_7=0
*
* SLIC Active        GPIO_1=0 GPIO_2=1 GPIO_5=0
* SLIC Ringing       GPIO_1=1 GPIO_2=1 GPIO_5=0
* SLIC Open Circuit  GPIO_1=0 GPIO_2=0 GPIO_5=0
*
* Hook Switch changes reported on GPIO_3
*********************************************************************/
static int ixj_set_port(IXJ *j, int arg)
{
	if (j->cardtype == QTI_PHONEJACK_LITE) {
		if (arg != PORT_POTS)
			return 10;
		else
			return 0;
	}
	switch (arg) {
	case PORT_POTS:
		j->port = PORT_POTS;
		switch (j->cardtype) {
		case QTI_PHONECARD:
			if (j->flags.pcmciasct == 1)
				SLIC_SetState(PLD_SLIC_STATE_ACTIVE, j);
			else
				return 11;
			break;
		case QTI_PHONEJACK_PCI:
			j->pld_slicw.pcib.mic = 0;
			j->pld_slicw.pcib.spk = 0;
			outb(j->pld_slicw.byte, j->XILINXbase + 0x01);
			break;
		case QTI_LINEJACK:
			ixj_set_pots(j, 0);			/* Disconnect POTS/PSTN relay */
			if (ixj_WriteDSPCommand(0xC528, j))		/* Write CODEC config to
									   Software Control Register */
				return 2;
			j->pld_scrw.bits.daafsyncen = 0;	/* Turn off DAA Frame Sync */

			outb(j->pld_scrw.byte, j->XILINXbase);
			j->pld_clock.byte = 0;
			outb(j->pld_clock.byte, j->XILINXbase + 0x04);
			j->pld_slicw.bits.rly1 = 1;
			j->pld_slicw.bits.spken = 0;
			outb(j->pld_slicw.byte, j->XILINXbase + 0x01);
			ixj_mixer(0x1200, j);	/* Turn Off MIC switch on mixer left */
			ixj_mixer(0x1401, j);	/* Turn On Mono1 switch on mixer left */
			ixj_mixer(0x1300, j);       /* Turn Off MIC switch on mixer right */
			ixj_mixer(0x1501, j);       /* Turn On Mono1 switch on mixer right */
			ixj_mixer(0x0E80, j);	/*Mic mute */
			ixj_mixer(0x0F00, j);	/* Set mono out (SLIC) to 0dB */
			ixj_mixer(0x0080, j);	/* Mute Master Left volume */
			ixj_mixer(0x0180, j);	/* Mute Master Right volume */
			SLIC_SetState(PLD_SLIC_STATE_STANDBY, j);
/*			SLIC_SetState(PLD_SLIC_STATE_ACTIVE, j); */
			break;
		case QTI_PHONEJACK:
			j->gpio.bytes.high = 0x0B;
			j->gpio.bits.gpio6 = 0;
			j->gpio.bits.gpio7 = 0;
			ixj_WriteDSPCommand(j->gpio.word, j);
			break;
		}
		break;
	case PORT_PSTN:
		if (j->cardtype == QTI_LINEJACK) {
			ixj_WriteDSPCommand(0xC534, j);	/* Write CODEC config to Software Control Register */

			j->pld_slicw.bits.rly3 = 0;
			j->pld_slicw.bits.rly1 = 1;
			j->pld_slicw.bits.spken = 0;
			outb(j->pld_slicw.byte, j->XILINXbase + 0x01);
			j->port = PORT_PSTN;
		} else {
			return 4;
		}
		break;
	case PORT_SPEAKER:
		j->port = PORT_SPEAKER;
		switch (j->cardtype) {
		case QTI_PHONECARD:
			if (j->flags.pcmciasct) {
				SLIC_SetState(PLD_SLIC_STATE_OC, j);
			}
			break;
		case QTI_PHONEJACK_PCI:
			j->pld_slicw.pcib.mic = 1;
			j->pld_slicw.pcib.spk = 1;
			outb(j->pld_slicw.byte, j->XILINXbase + 0x01);
			break;
		case QTI_LINEJACK:
			ixj_set_pots(j, 0);			/* Disconnect POTS/PSTN relay */
			if (ixj_WriteDSPCommand(0xC528, j))		/* Write CODEC config to
									   Software Control Register */
				return 2;
			j->pld_scrw.bits.daafsyncen = 0;	/* Turn off DAA Frame Sync */

			outb(j->pld_scrw.byte, j->XILINXbase);
			j->pld_clock.byte = 0;
			outb(j->pld_clock.byte, j->XILINXbase + 0x04);
			j->pld_slicw.bits.rly1 = 1;
			j->pld_slicw.bits.spken = 1;
			outb(j->pld_slicw.byte, j->XILINXbase + 0x01);
			ixj_mixer(0x1201, j);	/* Turn On MIC switch on mixer left */
			ixj_mixer(0x1400, j);	/* Turn Off Mono1 switch on mixer left */
			ixj_mixer(0x1301, j);       /* Turn On MIC switch on mixer right */
			ixj_mixer(0x1500, j);       /* Turn Off Mono1 switch on mixer right */
			ixj_mixer(0x0E06, j);	/*Mic un-mute 0dB */
			ixj_mixer(0x0F80, j);	/* Mute mono out (SLIC) */
			ixj_mixer(0x0000, j);	/* Set Master Left volume to 0dB */
			ixj_mixer(0x0100, j);	/* Set Master Right volume to 0dB */
			break;
		case QTI_PHONEJACK:
			j->gpio.bytes.high = 0x0B;
			j->gpio.bits.gpio6 = 0;
			j->gpio.bits.gpio7 = 1;
			ixj_WriteDSPCommand(j->gpio.word, j);
			break;
		}
		break;
	case PORT_HANDSET:
		if (j->cardtype != QTI_PHONEJACK) {
			return 5;
		} else {
			j->gpio.bytes.high = 0x0B;
			j->gpio.bits.gpio6 = 1;
			j->gpio.bits.gpio7 = 0;
			ixj_WriteDSPCommand(j->gpio.word, j);
			j->port = PORT_HANDSET;
		}
		break;
	default:
		return 6;
		break;
	}
	return 0;
}

static int ixj_set_pots(IXJ *j, int arg)
{
	if (j->cardtype == QTI_LINEJACK) {
		if (arg) {
			if (j->port == PORT_PSTN) {
				j->pld_slicw.bits.rly1 = 0;
				outb(j->pld_slicw.byte, j->XILINXbase + 0x01);
				j->flags.pots_pstn = 1;
				return 1;
			} else {
				j->flags.pots_pstn = 0;
				return 0;
			}
		} else {
			j->pld_slicw.bits.rly1 = 1;
			outb(j->pld_slicw.byte, j->XILINXbase + 0x01);
			j->flags.pots_pstn = 0;
			return 1;
		}
	} else {
		return 0;
	}
}

static void ixj_ring_on(IXJ *j)
{
	if (j->dsp.low == 0x20)	/* Internet PhoneJACK */
	 {
		if (ixjdebug & 0x0004)
			printk(KERN_INFO "IXJ Ring On /dev/phone%d\n", 	j->board);

		j->gpio.bytes.high = 0x0B;
		j->gpio.bytes.low = 0x00;
		j->gpio.bits.gpio1 = 1;
		j->gpio.bits.gpio2 = 1;
		j->gpio.bits.gpio5 = 0;
		ixj_WriteDSPCommand(j->gpio.word, j);	/* send the ring signal */
	} else			/* Internet LineJACK, Internet PhoneJACK Lite or Internet PhoneJACK PCI */
	{
		if (ixjdebug & 0x0004)
			printk(KERN_INFO "IXJ Ring On /dev/phone%d\n", j->board);

		SLIC_SetState(PLD_SLIC_STATE_RINGING, j);
	}
}

static int ixj_siadc(IXJ *j, int val)
{
	if(j->cardtype == QTI_PHONECARD){
		if(j->flags.pcmciascp){
			if(val == -1)
				return j->siadc.bits.rxg;

			if(val < 0 || val > 0x1F)
				return -1;

			j->siadc.bits.hom = 0;				/* Handset Out Mute */
			j->siadc.bits.lom = 0;				/* Line Out Mute */
			j->siadc.bits.rxg = val;			/*(0xC000 - 0x41C8) / 0x4EF;    RX PGA Gain */
			j->psccr.bits.addr = 6;				/* R/W Smart Cable Register Address */
			j->psccr.bits.rw = 0;				/* Read / Write flag */
			j->psccr.bits.dev = 0;
			outb(j->siadc.byte, j->XILINXbase + 0x00);
			outb(j->psccr.byte, j->XILINXbase + 0x01);
			ixj_PCcontrol_wait(j);
			return j->siadc.bits.rxg;
		}
	}
	return -1;
}

static int ixj_sidac(IXJ *j, int val)
{
	if(j->cardtype == QTI_PHONECARD){
		if(j->flags.pcmciascp){
			if(val == -1)
				return j->sidac.bits.txg;

			if(val < 0 || val > 0x1F)
				return -1;

			j->sidac.bits.srm = 1;				/* Speaker Right Mute */
			j->sidac.bits.slm = 1;				/* Speaker Left Mute */
			j->sidac.bits.txg = val;			/* (0xC000 - 0x45E4) / 0x5D3;	 TX PGA Gain */
			j->psccr.bits.addr = 7;				/* R/W Smart Cable Register Address */
			j->psccr.bits.rw = 0;				/* Read / Write flag */
			j->psccr.bits.dev = 0;
			outb(j->sidac.byte, j->XILINXbase + 0x00);
			outb(j->psccr.byte, j->XILINXbase + 0x01);
			ixj_PCcontrol_wait(j);
			return j->sidac.bits.txg;
		}
	}
	return -1;
}

static int ixj_pcmcia_cable_check(IXJ *j)
{
	j->pccr1.byte = inb_p(j->XILINXbase + 0x03);
	if (!j->flags.pcmciastate) {
		j->pccr2.byte = inb_p(j->XILINXbase + 0x02);
		if (j->pccr1.bits.drf || j->pccr2.bits.rstc) {
			j->flags.pcmciastate = 4;
			return 0;
		}
		if (j->pccr1.bits.ed) {
			j->pccr1.bits.ed = 0;
			j->psccr.bits.dev = 3;
			j->psccr.bits.rw = 1;
			outw_p(j->psccr.byte << 8, j->XILINXbase + 0x00);
			ixj_PCcontrol_wait(j);
			j->pslic.byte = inw_p(j->XILINXbase + 0x00) & 0xFF;
			j->pslic.bits.led2 = j->pslic.bits.det ? 1 : 0;
			j->psccr.bits.dev = 3;
			j->psccr.bits.rw = 0;
			outw_p(j->psccr.byte << 8 | j->pslic.byte, j->XILINXbase + 0x00);
			ixj_PCcontrol_wait(j);
			return j->pslic.bits.led2 ? 1 : 0;
		} else if (j->flags.pcmciasct) {
			return j->r_hook;
		} else {
			return 1;
		}
	} else if (j->flags.pcmciastate == 4) {
		if (!j->pccr1.bits.drf) {
			j->flags.pcmciastate = 3;
		}
		return 0;
	} else if (j->flags.pcmciastate == 3) {
		j->pccr2.bits.pwr = 0;
		j->pccr2.bits.rstc = 1;
		outb(j->pccr2.byte, j->XILINXbase + 0x02);
		j->checkwait = jiffies + (hertz * 2);
		j->flags.incheck = 1;
		j->flags.pcmciastate = 2;
		return 0;
	} else if (j->flags.pcmciastate == 2) {
		if (j->flags.incheck) {
			if (time_before(jiffies, j->checkwait)) {
				return 0;
			} else {
				j->flags.incheck = 0;
			}
		}
		j->pccr2.bits.pwr = 0;
		j->pccr2.bits.rstc = 0;
		outb_p(j->pccr2.byte, j->XILINXbase + 0x02);
		j->flags.pcmciastate = 1;
		return 0;
	} else if (j->flags.pcmciastate == 1) {
		j->flags.pcmciastate = 0;
		if (!j->pccr1.bits.drf) {
			j->psccr.bits.dev = 3;
			j->psccr.bits.rw = 1;
			outb_p(j->psccr.byte, j->XILINXbase + 0x01);
			ixj_PCcontrol_wait(j);
			j->flags.pcmciascp = 1;		/* Set Cable Present Flag */

			j->flags.pcmciasct = (inw_p(j->XILINXbase + 0x00) >> 8) & 0x03;		/* Get Cable Type */

			if (j->flags.pcmciasct == 3) {
				j->flags.pcmciastate = 4;
				return 0;
			} else if (j->flags.pcmciasct == 0) {
				j->pccr2.bits.pwr = 1;
				j->pccr2.bits.rstc = 0;
				outb_p(j->pccr2.byte, j->XILINXbase + 0x02);
				j->port = PORT_SPEAKER;
			} else {
				j->port = PORT_POTS;
			}
			j->sic1.bits.cpd = 0;				/* Chip Power Down */
			j->sic1.bits.mpd = 0;				/* MIC Bias Power Down */
			j->sic1.bits.hpd = 0;				/* Handset Bias Power Down */
			j->sic1.bits.lpd = 0;				/* Line Bias Power Down */
			j->sic1.bits.spd = 1;				/* Speaker Drive Power Down */
			j->psccr.bits.addr = 1;				/* R/W Smart Cable Register Address */
			j->psccr.bits.rw = 0;				/* Read / Write flag */
			j->psccr.bits.dev = 0;
			outb(j->sic1.byte, j->XILINXbase + 0x00);
			outb(j->psccr.byte, j->XILINXbase + 0x01);
			ixj_PCcontrol_wait(j);

			j->sic2.bits.al = 0;				/* Analog Loopback DAC analog -> ADC analog */
			j->sic2.bits.dl2 = 0;				/* Digital Loopback DAC -> ADC one bit */
			j->sic2.bits.dl1 = 0;				/* Digital Loopback ADC -> DAC one bit */
			j->sic2.bits.pll = 0;				/* 1 = div 10, 0 = div 5 */
			j->sic2.bits.hpd = 0;				/* HPF disable */
			j->psccr.bits.addr = 2;				/* R/W Smart Cable Register Address */
			j->psccr.bits.rw = 0;				/* Read / Write flag */
			j->psccr.bits.dev = 0;
			outb(j->sic2.byte, j->XILINXbase + 0x00);
			outb(j->psccr.byte, j->XILINXbase + 0x01);
			ixj_PCcontrol_wait(j);

			j->psccr.bits.addr = 3;				/* R/W Smart Cable Register Address */
			j->psccr.bits.rw = 0;				/* Read / Write flag */
			j->psccr.bits.dev = 0;
			outb(0x00, j->XILINXbase + 0x00);		/* PLL Divide N1 */
			outb(j->psccr.byte, j->XILINXbase + 0x01);
			ixj_PCcontrol_wait(j);

			j->psccr.bits.addr = 4;				/* R/W Smart Cable Register Address */
			j->psccr.bits.rw = 0;				/* Read / Write flag */
			j->psccr.bits.dev = 0;
			outb(0x09, j->XILINXbase + 0x00);		/* PLL Multiply M1 */
			outb(j->psccr.byte, j->XILINXbase + 0x01);
			ixj_PCcontrol_wait(j);

			j->sirxg.bits.lig = 1;				/* Line In Gain */
			j->sirxg.bits.lim = 1;				/* Line In Mute */
			j->sirxg.bits.mcg = 0;				/* MIC In Gain was 3 */
			j->sirxg.bits.mcm = 0;				/* MIC In Mute */
			j->sirxg.bits.him = 0;				/* Handset In Mute */
			j->sirxg.bits.iir = 1;				/* IIR */
			j->psccr.bits.addr = 5;				/* R/W Smart Cable Register Address */
			j->psccr.bits.rw = 0;				/* Read / Write flag */
			j->psccr.bits.dev = 0;
			outb(j->sirxg.byte, j->XILINXbase + 0x00);
			outb(j->psccr.byte, j->XILINXbase + 0x01);
			ixj_PCcontrol_wait(j);

			ixj_siadc(j, 0x17);
			ixj_sidac(j, 0x1D);

			j->siaatt.bits.sot = 0;
			j->psccr.bits.addr = 9;				/* R/W Smart Cable Register Address */
			j->psccr.bits.rw = 0;				/* Read / Write flag */
			j->psccr.bits.dev = 0;
			outb(j->siaatt.byte, j->XILINXbase + 0x00);
			outb(j->psccr.byte, j->XILINXbase + 0x01);
			ixj_PCcontrol_wait(j);

			if (j->flags.pcmciasct == 1 && !j->readers && !j->writers) {
				j->psccr.byte = j->pslic.byte = 0;
				j->pslic.bits.powerdown = 1;
				j->psccr.bits.dev = 3;
				j->psccr.bits.rw = 0;
				outw_p(j->psccr.byte << 8 | j->pslic.byte, j->XILINXbase + 0x00);
				ixj_PCcontrol_wait(j);
			}
		}
		return 0;
	} else {
		j->flags.pcmciascp = 0;
		return 0;
	}
	return 0;
}

static int ixj_hookstate(IXJ *j)
{
	int fOffHook = 0;

	switch (j->cardtype) {
	case QTI_PHONEJACK:
		ixj_gpio_read(j);
		fOffHook = j->gpio.bits.gpio3read ? 1 : 0;
		break;
	case QTI_LINEJACK:
	case QTI_PHONEJACK_LITE:
	case QTI_PHONEJACK_PCI:
		SLIC_GetState(j);
		if(j->cardtype == QTI_LINEJACK && j->flags.pots_pstn == 1 && (j->readers || j->writers)) {
			fOffHook = j->pld_slicr.bits.potspstn ? 1 : 0;
			if(fOffHook != j->p_hook) {
				if(!j->checkwait) {
					j->checkwait = jiffies;
				} 
				if(time_before(jiffies, j->checkwait + 2)) {
					fOffHook ^= 1;
				} else {
					j->checkwait = 0;
				}
				j->p_hook = fOffHook;
	 			printk("IXJ : /dev/phone%d pots-pstn hookstate check %d at %ld\n", j->board, fOffHook, jiffies);
			}
		} else {
			if (j->pld_slicr.bits.state == PLD_SLIC_STATE_ACTIVE ||
			    j->pld_slicr.bits.state == PLD_SLIC_STATE_STANDBY) {
				if (j->flags.ringing || j->flags.cringing) {
					if (!in_interrupt()) {
						msleep(20);
					}
					SLIC_GetState(j);
					if (j->pld_slicr.bits.state == PLD_SLIC_STATE_RINGING) {
						ixj_ring_on(j);
					}
				}
				if (j->cardtype == QTI_PHONEJACK_PCI) {
					j->pld_scrr.byte = inb_p(j->XILINXbase);
					fOffHook = j->pld_scrr.pcib.det ? 1 : 0;
				} else
					fOffHook = j->pld_slicr.bits.det ? 1 : 0;
			}
		}
		break;
	case QTI_PHONECARD:
		fOffHook = ixj_pcmcia_cable_check(j);
		break;
	}
	if (j->r_hook != fOffHook) {
		j->r_hook = fOffHook;
		if (j->port == PORT_SPEAKER || j->port == PORT_HANDSET) { // || (j->port == PORT_PSTN && j->flags.pots_pstn == 0)) {
			j->ex.bits.hookstate = 1;
			ixj_kill_fasync(j, SIG_HOOKSTATE, POLL_IN);
		} else if (!fOffHook) {
			j->flash_end = jiffies + ((60 * hertz) / 100);
		}
	}
	if (fOffHook) {
		if(time_before(jiffies, j->flash_end)) {
			j->ex.bits.flash = 1;
			j->flash_end = 0;
			ixj_kill_fasync(j, SIG_FLASH, POLL_IN);
		}
	} else {
		if(time_before(jiffies, j->flash_end)) {
			fOffHook = 1;
		}
	}

	if (j->port == PORT_PSTN && j->daa_mode == SOP_PU_CONVERSATION)
		fOffHook |= 2;

	if (j->port == PORT_SPEAKER) {
		if(j->cardtype == QTI_PHONECARD) {
			if(j->flags.pcmciascp && j->flags.pcmciasct) {
				fOffHook |= 2;
			}
		} else {
			fOffHook |= 2;
		}
	}

	if (j->port == PORT_HANDSET)
		fOffHook |= 2;

	return fOffHook;
}

static void ixj_ring_off(IXJ *j)
{
	if (j->dsp.low == 0x20)	/* Internet PhoneJACK */
	 {
		if (ixjdebug & 0x0004)
			printk(KERN_INFO "IXJ Ring Off\n");
		j->gpio.bytes.high = 0x0B;
		j->gpio.bytes.low = 0x00;
		j->gpio.bits.gpio1 = 0;
		j->gpio.bits.gpio2 = 1;
		j->gpio.bits.gpio5 = 0;
		ixj_WriteDSPCommand(j->gpio.word, j);
	} else			/* Internet LineJACK */
	{
		if (ixjdebug & 0x0004)
			printk(KERN_INFO "IXJ Ring Off\n");

		if(!j->flags.cidplay)
			SLIC_SetState(PLD_SLIC_STATE_STANDBY, j);

		SLIC_GetState(j);
	}
}

static void ixj_ring_start(IXJ *j)
{
	j->flags.cringing = 1;
	if (ixjdebug & 0x0004)
		printk(KERN_INFO "IXJ Cadence Ringing Start /dev/phone%d\n", j->board);
	if (ixj_hookstate(j) & 1) {
		if (j->port == PORT_POTS)
			ixj_ring_off(j);
		j->flags.cringing = 0;
		if (ixjdebug & 0x0004)
			printk(KERN_INFO "IXJ Cadence Ringing Stopped /dev/phone%d off hook\n", j->board);
	} else if(j->cadence_f[5].enable && (!j->cadence_f[5].en_filter)) {
		j->ring_cadence_jif = jiffies;
		j->flags.cidsent = j->flags.cidring = 0;
		j->cadence_f[5].state = 0;
		if(j->cadence_f[5].on1)
			ixj_ring_on(j);
	} else {
		j->ring_cadence_jif = jiffies;
		j->ring_cadence_t = 15;
		if (j->ring_cadence & 1 << j->ring_cadence_t) {
			ixj_ring_on(j);
		} else {
			ixj_ring_off(j);
		}
		j->flags.cidsent = j->flags.cidring = j->flags.firstring = 0;
	}
}

static int ixj_ring(IXJ *j)
{
	char cntr;
	unsigned long jif;

	j->flags.ringing = 1;
	if (ixj_hookstate(j) & 1) {
		ixj_ring_off(j);
		j->flags.ringing = 0;
		return 1;
	}
	for (cntr = 0; cntr < j->maxrings; cntr++) {
		jif = jiffies + (1 * hertz);
		ixj_ring_on(j);
		while (time_before(jiffies, jif)) {
			if (ixj_hookstate(j) & 1) {
				ixj_ring_off(j);
				j->flags.ringing = 0;
				return 1;
			}
			schedule_timeout_interruptible(1);
			if (signal_pending(current))
				break;
		}
		jif = jiffies + (3 * hertz);
		ixj_ring_off(j);
		while (time_before(jiffies, jif)) {
			if (ixj_hookstate(j) & 1) {
				msleep(10);
				if (ixj_hookstate(j) & 1) {
					j->flags.ringing = 0;
					return 1;
				}
			}
			schedule_timeout_interruptible(1);
			if (signal_pending(current))
				break;
		}
	}
	ixj_ring_off(j);
	j->flags.ringing = 0;
	return 0;
}

static int ixj_open(struct phone_device *p, struct file *file_p)
{
	IXJ *j = get_ixj(p->board);
	file_p->private_data = j;

	if (!j->DSPbase)
		return -ENODEV;

        if (file_p->f_mode & FMODE_READ) {
		if(!j->readers) {
	                j->readers++;
        	} else {
                	return -EBUSY;
		}
        }

	if (file_p->f_mode & FMODE_WRITE) {
		if(!j->writers) {
			j->writers++;
		} else {
			if (file_p->f_mode & FMODE_READ){
				j->readers--;
			}
			return -EBUSY;
		}
	}

	if (j->cardtype == QTI_PHONECARD) {
		j->pslic.bits.powerdown = 0;
		j->psccr.bits.dev = 3;
		j->psccr.bits.rw = 0;
		outw_p(j->psccr.byte << 8 | j->pslic.byte, j->XILINXbase + 0x00);
		ixj_PCcontrol_wait(j);
	}

	j->flags.cidplay = 0;
	j->flags.cidcw_ack = 0;

	if (ixjdebug & 0x0002)
		printk(KERN_INFO "Opening board %d\n", p->board);

	j->framesread = j->frameswritten = 0;
	return 0;
}

static int ixj_release(struct inode *inode, struct file *file_p)
{
	IXJ_TONE ti;
	int cnt;
	IXJ *j = file_p->private_data;
	int board = j->p.board;

	/*
	 *    Set up locks to ensure that only one process is talking to the DSP at a time.
	 *    This is necessary to keep the DSP from locking up.
	 */
	while(test_and_set_bit(board, (void *)&j->busyflags) != 0)
		schedule_timeout_interruptible(1);
	if (ixjdebug & 0x0002)
		printk(KERN_INFO "Closing board %d\n", NUM(inode));

	if (j->cardtype == QTI_PHONECARD)
		ixj_set_port(j, PORT_SPEAKER);
	else
		ixj_set_port(j, PORT_POTS);

	aec_stop(j);
	ixj_play_stop(j);
	ixj_record_stop(j);
	set_play_volume(j, 0x100);
	set_rec_volume(j, 0x100);
	ixj_ring_off(j);

	/* Restore the tone table to default settings. */
	ti.tone_index = 10;
	ti.gain0 = 1;
	ti.freq0 = hz941;
	ti.gain1 = 0;
	ti.freq1 = hz1209;
	ixj_init_tone(j, &ti);
	ti.tone_index = 11;
	ti.gain0 = 1;
	ti.freq0 = hz941;
	ti.gain1 = 0;
	ti.freq1 = hz1336;
	ixj_init_tone(j, &ti);
	ti.tone_index = 12;
	ti.gain0 = 1;
	ti.freq0 = hz941;
	ti.gain1 = 0;
	ti.freq1 = hz1477;
	ixj_init_tone(j, &ti);
	ti.tone_index = 13;
	ti.gain0 = 1;
	ti.freq0 = hz800;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(j, &ti);
	ti.tone_index = 14;
	ti.gain0 = 1;
	ti.freq0 = hz1000;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(j, &ti);
	ti.tone_index = 15;
	ti.gain0 = 1;
	ti.freq0 = hz1250;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(j, &ti);
	ti.tone_index = 16;
	ti.gain0 = 1;
	ti.freq0 = hz950;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(j, &ti);
	ti.tone_index = 17;
	ti.gain0 = 1;
	ti.freq0 = hz1100;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(j, &ti);
	ti.tone_index = 18;
	ti.gain0 = 1;
	ti.freq0 = hz1400;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(j, &ti);
	ti.tone_index = 19;
	ti.gain0 = 1;
	ti.freq0 = hz1500;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(j, &ti);
	ti.tone_index = 20;
	ti.gain0 = 1;
	ti.freq0 = hz1600;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(j, &ti);
	ti.tone_index = 21;
	ti.gain0 = 1;
	ti.freq0 = hz1800;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(j, &ti);
	ti.tone_index = 22;
	ti.gain0 = 1;
	ti.freq0 = hz2100;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(j, &ti);
	ti.tone_index = 23;
	ti.gain0 = 1;
	ti.freq0 = hz1300;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(j, &ti);
	ti.tone_index = 24;
	ti.gain0 = 1;
	ti.freq0 = hz2450;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(j, &ti);
	ti.tone_index = 25;
	ti.gain0 = 1;
	ti.freq0 = hz350;
	ti.gain1 = 0;
	ti.freq1 = hz440;
	ixj_init_tone(j, &ti);
	ti.tone_index = 26;
	ti.gain0 = 1;
	ti.freq0 = hz440;
	ti.gain1 = 0;
	ti.freq1 = hz480;
	ixj_init_tone(j, &ti);
	ti.tone_index = 27;
	ti.gain0 = 1;
	ti.freq0 = hz480;
	ti.gain1 = 0;
	ti.freq1 = hz620;
	ixj_init_tone(j, &ti);

	set_rec_depth(j, 2);	/* Set Record Channel Limit to 2 frames */

	set_play_depth(j, 2);	/* Set Playback Channel Limit to 2 frames */

	j->ex.bits.dtmf_ready = 0;
	j->dtmf_state = 0;
	j->dtmf_wp = j->dtmf_rp = 0;
	j->rec_mode = j->play_mode = -1;
	j->flags.ringing = 0;
	j->maxrings = MAXRINGS;
	j->ring_cadence = USA_RING_CADENCE;
	if(j->cadence_f[5].enable) {
		j->cadence_f[5].enable = j->cadence_f[5].en_filter = j->cadence_f[5].state = 0;
	}
	j->drybuffer = 0;
	j->winktime = 320;
	j->flags.dtmf_oob = 0;
	for (cnt = 0; cnt < 4; cnt++)
		j->cadence_f[cnt].enable = 0;

	idle(j);

	if(j->cardtype == QTI_PHONECARD) {
		SLIC_SetState(PLD_SLIC_STATE_OC, j);
	}

	if (file_p->f_mode & FMODE_READ)
		j->readers--;
	if (file_p->f_mode & FMODE_WRITE)
		j->writers--;

	if (j->read_buffer && !j->readers) {
		kfree(j->read_buffer);
		j->read_buffer = NULL;
		j->read_buffer_size = 0;
	}
	if (j->write_buffer && !j->writers) {
		kfree(j->write_buffer);
		j->write_buffer = NULL;
		j->write_buffer_size = 0;
	}
	j->rec_codec = j->play_codec = 0;
	j->rec_frame_size = j->play_frame_size = 0;
	j->flags.cidsent = j->flags.cidring = 0;
	ixj_fasync(-1, file_p, 0);	/* remove from list of async notification */

	if(j->cardtype == QTI_LINEJACK && !j->readers && !j->writers) {
		ixj_set_port(j, PORT_PSTN);
		daa_set_mode(j, SOP_PU_SLEEP);
		ixj_set_pots(j, 1);
	}
	ixj_WriteDSPCommand(0x0FE3, j);	/* Put the DSP in 1/5 power mode. */

	/* Set up the default signals for events */
	for (cnt = 0; cnt < 35; cnt++)
		j->ixj_signals[cnt] = SIGIO;

	/* Set the excetion signal enable flags */
	j->ex_sig.bits.dtmf_ready = j->ex_sig.bits.hookstate = j->ex_sig.bits.flash = j->ex_sig.bits.pstn_ring = 
	j->ex_sig.bits.caller_id = j->ex_sig.bits.pstn_wink = j->ex_sig.bits.f0 = j->ex_sig.bits.f1 = j->ex_sig.bits.f2 = 
	j->ex_sig.bits.f3 = j->ex_sig.bits.fc0 = j->ex_sig.bits.fc1 = j->ex_sig.bits.fc2 = j->ex_sig.bits.fc3 = 1;

	file_p->private_data = NULL;
	clear_bit(board, &j->busyflags);
	return 0;
}

static int read_filters(IXJ *j)
{
	unsigned short fc, cnt, trg;
	int var;

	trg = 0;
	if (ixj_WriteDSPCommand(0x5144, j)) {
		if(ixjdebug & 0x0001) {
			printk(KERN_INFO "Read Frame Counter failed!\n");
		}
		return -1;
	}
	fc = j->ssr.high << 8 | j->ssr.low;
	if (fc == j->frame_count)
		return 1;

	j->frame_count = fc;

	if (j->dtmf_proc)
		return 1;

	var = 10;

	for (cnt = 0; cnt < 4; cnt++) {
		if (ixj_WriteDSPCommand(0x5154 + cnt, j)) {
			if(ixjdebug & 0x0001) {
				printk(KERN_INFO "Select Filter %d failed!\n", cnt);
			}
			return -1;
		}
		if (ixj_WriteDSPCommand(0x515C, j)) {
			if(ixjdebug & 0x0001) {
				printk(KERN_INFO "Read Filter History %d failed!\n", cnt);
			}
			return -1;
		}
		j->filter_hist[cnt] = j->ssr.high << 8 | j->ssr.low;

		if (j->cadence_f[cnt].enable) {
			if (j->filter_hist[cnt] & 3 && !(j->filter_hist[cnt] & 12)) {
				if (j->cadence_f[cnt].state == 0) {
					j->cadence_f[cnt].state = 1;
					j->cadence_f[cnt].on1min = jiffies + (long)((j->cadence_f[cnt].on1 * (hertz * (100 - var)) / 10000));
					j->cadence_f[cnt].on1dot = jiffies + (long)((j->cadence_f[cnt].on1 * (hertz * (100)) / 10000));
					j->cadence_f[cnt].on1max = jiffies + (long)((j->cadence_f[cnt].on1 * (hertz * (100 + var)) / 10000));
				} else if (j->cadence_f[cnt].state == 2 &&
					   (time_after(jiffies, j->cadence_f[cnt].off1min) &&
					    time_before(jiffies, j->cadence_f[cnt].off1max))) {
					if (j->cadence_f[cnt].on2) {
						j->cadence_f[cnt].state = 3;
						j->cadence_f[cnt].on2min = jiffies + (long)((j->cadence_f[cnt].on2 * (hertz * (100 - var)) / 10000));
						j->cadence_f[cnt].on2dot = jiffies + (long)((j->cadence_f[cnt].on2 * (hertz * (100)) / 10000));
						j->cadence_f[cnt].on2max = jiffies + (long)((j->cadence_f[cnt].on2 * (hertz * (100 + var)) / 10000));
					} else {
						j->cadence_f[cnt].state = 7;
					}
				} else if (j->cadence_f[cnt].state == 4 &&
					   (time_after(jiffies, j->cadence_f[cnt].off2min) &&
					    time_before(jiffies, j->cadence_f[cnt].off2max))) {
					if (j->cadence_f[cnt].on3) {
						j->cadence_f[cnt].state = 5;
						j->cadence_f[cnt].on3min = jiffies + (long)((j->cadence_f[cnt].on3 * (hertz * (100 - var)) / 10000));
						j->cadence_f[cnt].on3dot = jiffies + (long)((j->cadence_f[cnt].on3 * (hertz * (100)) / 10000));
						j->cadence_f[cnt].on3max = jiffies + (long)((j->cadence_f[cnt].on3 * (hertz * (100 + var)) / 10000));
					} else {
						j->cadence_f[cnt].state = 7;
					}
				} else {
					j->cadence_f[cnt].state = 0;
				}
			} else if (j->filter_hist[cnt] & 12 && !(j->filter_hist[cnt] & 3)) {
				if (j->cadence_f[cnt].state == 1) {
					if(!j->cadence_f[cnt].on1) {
						j->cadence_f[cnt].state = 7;
					} else if((time_after(jiffies, j->cadence_f[cnt].on1min) &&
					  time_before(jiffies, j->cadence_f[cnt].on1max))) {
						if(j->cadence_f[cnt].off1) {
							j->cadence_f[cnt].state = 2;
							j->cadence_f[cnt].off1min = jiffies + (long)((j->cadence_f[cnt].off1 * (hertz * (100 - var)) / 10000));
							j->cadence_f[cnt].off1dot = jiffies + (long)((j->cadence_f[cnt].off1 * (hertz * (100)) / 10000));
							j->cadence_f[cnt].off1max = jiffies + (long)((j->cadence_f[cnt].off1 * (hertz * (100 + var)) / 10000));
						} else {
							j->cadence_f[cnt].state = 7;
						}
					} else {
						j->cadence_f[cnt].state = 0;
					}
				} else if (j->cadence_f[cnt].state == 3) {
					if((time_after(jiffies, j->cadence_f[cnt].on2min) &&
					    time_before(jiffies, j->cadence_f[cnt].on2max))) {
						if(j->cadence_f[cnt].off2) {
							j->cadence_f[cnt].state = 4;
							j->cadence_f[cnt].off2min = jiffies + (long)((j->cadence_f[cnt].off2 * (hertz * (100 - var)) / 10000));
							j->cadence_f[cnt].off2dot = jiffies + (long)((j->cadence_f[cnt].off2 * (hertz * (100)) / 10000));
							j->cadence_f[cnt].off2max = jiffies + (long)((j->cadence_f[cnt].off2 * (hertz * (100 + var)) / 10000));
						} else {
							j->cadence_f[cnt].state = 7;
						}
					} else {
						j->cadence_f[cnt].state = 0;
					}
				} else if (j->cadence_f[cnt].state == 5) {
					if ((time_after(jiffies, j->cadence_f[cnt].on3min) &&
					    time_before(jiffies, j->cadence_f[cnt].on3max))) {
						if(j->cadence_f[cnt].off3) {
							j->cadence_f[cnt].state = 6;
							j->cadence_f[cnt].off3min = jiffies + (long)((j->cadence_f[cnt].off3 * (hertz * (100 - var)) / 10000));
							j->cadence_f[cnt].off3dot = jiffies + (long)((j->cadence_f[cnt].off3 * (hertz * (100)) / 10000));
							j->cadence_f[cnt].off3max = jiffies + (long)((j->cadence_f[cnt].off3 * (hertz * (100 + var)) / 10000));
						} else {
							j->cadence_f[cnt].state = 7;
						}
					} else {
						j->cadence_f[cnt].state = 0;
					}
				} else {
					j->cadence_f[cnt].state = 0;
				}
			} else {
				switch(j->cadence_f[cnt].state) {
					case 1:
						if(time_after(jiffies, j->cadence_f[cnt].on1dot) &&
						   !j->cadence_f[cnt].off1 &&
						   !j->cadence_f[cnt].on2 && !j->cadence_f[cnt].off2 &&
						   !j->cadence_f[cnt].on3 && !j->cadence_f[cnt].off3) {
							j->cadence_f[cnt].state = 7;
						}
						break;
					case 3:
						if(time_after(jiffies, j->cadence_f[cnt].on2dot) &&
						   !j->cadence_f[cnt].off2 &&
						   !j->cadence_f[cnt].on3 && !j->cadence_f[cnt].off3) {
							j->cadence_f[cnt].state = 7;
						}
						break;
					case 5:
						if(time_after(jiffies, j->cadence_f[cnt].on3dot) &&
						   !j->cadence_f[cnt].off3) {
							j->cadence_f[cnt].state = 7;
						}
						break;
				}
			}

			if (ixjdebug & 0x0040) {
				printk(KERN_INFO "IXJ Tone Cadence state = %d /dev/phone%d at %ld\n", j->cadence_f[cnt].state, j->board, jiffies);
				switch(j->cadence_f[cnt].state) {
					case 0:
						printk(KERN_INFO "IXJ /dev/phone%d No Tone detected\n", j->board);
						break;
					case 1:
						printk(KERN_INFO "IXJ /dev/phone%d Next Tone Cadence state at %u %ld - %ld - %ld\n", j->board,
					j->cadence_f[cnt].on1, j->cadence_f[cnt].on1min, j->cadence_f[cnt].on1dot, j->cadence_f[cnt].on1max);
						break;
					case 2:
						printk(KERN_INFO "IXJ /dev/phone%d Next Tone Cadence state at %ld - %ld\n", j->board, j->cadence_f[cnt].off1min, 
															j->cadence_f[cnt].off1max);
						break;
					case 3:
						printk(KERN_INFO "IXJ /dev/phone%d Next Tone Cadence state at %ld - %ld\n", j->board, j->cadence_f[cnt].on2min,
															j->cadence_f[cnt].on2max);
						break;
					case 4:
						printk(KERN_INFO "IXJ /dev/phone%d Next Tone Cadence state at %ld - %ld\n", j->board, j->cadence_f[cnt].off2min,
															j->cadence_f[cnt].off2max);
						break;
					case 5:
						printk(KERN_INFO "IXJ /dev/phone%d Next Tone Cadence state at %ld - %ld\n", j->board, j->cadence_f[cnt].on3min,
															j->cadence_f[cnt].on3max);
						break;
					case 6:	
						printk(KERN_INFO "IXJ /dev/phone%d Next Tone Cadence state at %ld - %ld\n", j->board, j->cadence_f[cnt].off3min,
															j->cadence_f[cnt].off3max);
						break;
				}
			} 
		}
		if (j->cadence_f[cnt].state == 7) {
			j->cadence_f[cnt].state = 0;
			if (j->cadence_f[cnt].enable == 1)
				j->cadence_f[cnt].enable = 0;
			switch (cnt) {
			case 0:
				if(ixjdebug & 0x0020) {
					printk(KERN_INFO "Filter Cadence 0 triggered %ld\n", jiffies);
				}
				j->ex.bits.fc0 = 1;
				ixj_kill_fasync(j, SIG_FC0, POLL_IN);
				break;
			case 1:
				if(ixjdebug & 0x0020) {
					printk(KERN_INFO "Filter Cadence 1 triggered %ld\n", jiffies);
				}
				j->ex.bits.fc1 = 1;
				ixj_kill_fasync(j, SIG_FC1, POLL_IN);
				break;
			case 2:
				if(ixjdebug & 0x0020) {
					printk(KERN_INFO "Filter Cadence 2 triggered %ld\n", jiffies);
				}
				j->ex.bits.fc2 = 1;
				ixj_kill_fasync(j, SIG_FC2, POLL_IN);
				break;
			case 3:
				if(ixjdebug & 0x0020) {
					printk(KERN_INFO "Filter Cadence 3 triggered %ld\n", jiffies);
				}
				j->ex.bits.fc3 = 1;
				ixj_kill_fasync(j, SIG_FC3, POLL_IN);
				break;
			}
		}
		if (j->filter_en[cnt] && ((j->filter_hist[cnt] & 3 && !(j->filter_hist[cnt] & 12)) ||
					  (j->filter_hist[cnt] & 12 && !(j->filter_hist[cnt] & 3)))) {
			if((j->filter_hist[cnt] & 3 && !(j->filter_hist[cnt] & 12))) {
				trg = 1;
			} else if((j->filter_hist[cnt] & 12 && !(j->filter_hist[cnt] & 3))) {
				trg = 0;
			}
			switch (cnt) {
			case 0:
				if(ixjdebug & 0x0020) {
					printk(KERN_INFO "Filter 0 triggered %d at %ld\n", trg, jiffies);
				}
				j->ex.bits.f0 = 1;
				ixj_kill_fasync(j, SIG_F0, POLL_IN);
				break;
			case 1:
				if(ixjdebug & 0x0020) {
					printk(KERN_INFO "Filter 1 triggered %d at %ld\n", trg, jiffies);
				}
				j->ex.bits.f1 = 1;
				ixj_kill_fasync(j, SIG_F1, POLL_IN);
				break;
			case 2:
				if(ixjdebug & 0x0020) {
					printk(KERN_INFO "Filter 2 triggered %d at %ld\n", trg, jiffies);
				}
				j->ex.bits.f2 = 1;
				ixj_kill_fasync(j, SIG_F2, POLL_IN);
				break;
			case 3:
				if(ixjdebug & 0x0020) {
					printk(KERN_INFO "Filter 3 triggered %d at %ld\n", trg, jiffies);
				}
				j->ex.bits.f3 = 1;
				ixj_kill_fasync(j, SIG_F3, POLL_IN);
				break;
			}
		}
	}
	return 0;
}

static int LineMonitor(IXJ *j)
{
	if (j->dtmf_proc) {
		return -1;
	}
	j->dtmf_proc = 1;

	if (ixj_WriteDSPCommand(0x7000, j))		/* Line Monitor */
		return -1;

	j->dtmf.bytes.high = j->ssr.high;
	j->dtmf.bytes.low = j->ssr.low;
	if (!j->dtmf_state && j->dtmf.bits.dtmf_valid) {
		j->dtmf_state = 1;
		j->dtmf_current = j->dtmf.bits.digit;
	}
	if (j->dtmf_state && !j->dtmf.bits.dtmf_valid)	/* && j->dtmf_wp != j->dtmf_rp) */
	 {
		if(!j->cidcw_wait) {
			j->dtmfbuffer[j->dtmf_wp] = j->dtmf_current;
			j->dtmf_wp++;
			if (j->dtmf_wp == 79)
				j->dtmf_wp = 0;
			j->ex.bits.dtmf_ready = 1;
			if(j->ex_sig.bits.dtmf_ready) {
				ixj_kill_fasync(j, SIG_DTMF_READY, POLL_IN);
			}
		}
		else if(j->dtmf_current == 0x00 || j->dtmf_current == 0x0D) {
			if(ixjdebug & 0x0020) {
				printk("IXJ phone%d saw CIDCW Ack DTMF %d from display at %ld\n", j->board, j->dtmf_current, jiffies);
			}
			j->flags.cidcw_ack = 1;
		}
		j->dtmf_state = 0;
	}
	j->dtmf_proc = 0;

	return 0;
}

/************************************************************************
*
* Functions to allow alaw <-> ulaw conversions.
*
************************************************************************/

static void ulaw2alaw(unsigned char *buff, unsigned long len)
{
	static unsigned char table_ulaw2alaw[] =
	{
		0x2A, 0x2B, 0x28, 0x29, 0x2E, 0x2F, 0x2C, 0x2D, 
		0x22, 0x23, 0x20, 0x21, 0x26, 0x27, 0x24, 0x25, 
		0x3A, 0x3B, 0x38, 0x39, 0x3E, 0x3F, 0x3C, 0x3D, 
		0x32, 0x33, 0x30, 0x31, 0x36, 0x37, 0x34, 0x35, 
		0x0B, 0x08, 0x09, 0x0E, 0x0F, 0x0C, 0x0D, 0x02, 
		0x03, 0x00, 0x01, 0x06, 0x07, 0x04, 0x05, 0x1A, 
		0x1B, 0x18, 0x19, 0x1E, 0x1F, 0x1C, 0x1D, 0x12, 
		0x13, 0x10, 0x11, 0x16, 0x17, 0x14, 0x15, 0x6B, 
		0x68, 0x69, 0x6E, 0x6F, 0x6C, 0x6D, 0x62, 0x63, 
		0x60, 0x61, 0x66, 0x67, 0x64, 0x65, 0x7B, 0x79, 
		0x7E, 0x7F, 0x7C, 0x7D, 0x72, 0x73, 0x70, 0x71, 
		0x76, 0x77, 0x74, 0x75, 0x4B, 0x49, 0x4F, 0x4D, 
		0x42, 0x43, 0x40, 0x41, 0x46, 0x47, 0x44, 0x45, 
		0x5A, 0x5B, 0x58, 0x59, 0x5E, 0x5F, 0x5C, 0x5D, 
		0x52, 0x52, 0x53, 0x53, 0x50, 0x50, 0x51, 0x51, 
		0x56, 0x56, 0x57, 0x57, 0x54, 0x54, 0x55, 0xD5, 
		0xAA, 0xAB, 0xA8, 0xA9, 0xAE, 0xAF, 0xAC, 0xAD, 
		0xA2, 0xA3, 0xA0, 0xA1, 0xA6, 0xA7, 0xA4, 0xA5, 
		0xBA, 0xBB, 0xB8, 0xB9, 0xBE, 0xBF, 0xBC, 0xBD, 
		0xB2, 0xB3, 0xB0, 0xB1, 0xB6, 0xB7, 0xB4, 0xB5, 
		0x8B, 0x88, 0x89, 0x8E, 0x8F, 0x8C, 0x8D, 0x82, 
		0x83, 0x80, 0x81, 0x86, 0x87, 0x84, 0x85, 0x9A, 
		0x9B, 0x98, 0x99, 0x9E, 0x9F, 0x9C, 0x9D, 0x92, 
		0x93, 0x90, 0x91, 0x96, 0x97, 0x94, 0x95, 0xEB, 
		0xE8, 0xE9, 0xEE, 0xEF, 0xEC, 0xED, 0xE2, 0xE3, 
		0xE0, 0xE1, 0xE6, 0xE7, 0xE4, 0xE5, 0xFB, 0xF9, 
		0xFE, 0xFF, 0xFC, 0xFD, 0xF2, 0xF3, 0xF0, 0xF1, 
		0xF6, 0xF7, 0xF4, 0xF5, 0xCB, 0xC9, 0xCF, 0xCD, 
		0xC2, 0xC3, 0xC0, 0xC1, 0xC6, 0xC7, 0xC4, 0xC5, 
		0xDA, 0xDB, 0xD8, 0xD9, 0xDE, 0xDF, 0xDC, 0xDD, 
		0xD2, 0xD2, 0xD3, 0xD3, 0xD0, 0xD0, 0xD1, 0xD1, 
		0xD6, 0xD6, 0xD7, 0xD7, 0xD4, 0xD4, 0xD5, 0xD5
	};

	while (len--)
	{
		*buff = table_ulaw2alaw[*(unsigned char *)buff];
		buff++;
	}
}

static void alaw2ulaw(unsigned char *buff, unsigned long len)
{
	static unsigned char table_alaw2ulaw[] =
	{
		0x29, 0x2A, 0x27, 0x28, 0x2D, 0x2E, 0x2B, 0x2C, 
		0x21, 0x22, 0x1F, 0x20, 0x25, 0x26, 0x23, 0x24, 
		0x39, 0x3A, 0x37, 0x38, 0x3D, 0x3E, 0x3B, 0x3C, 
		0x31, 0x32, 0x2F, 0x30, 0x35, 0x36, 0x33, 0x34, 
		0x0A, 0x0B, 0x08, 0x09, 0x0E, 0x0F, 0x0C, 0x0D, 
		0x02, 0x03, 0x00, 0x01, 0x06, 0x07, 0x04, 0x05, 
		0x1A, 0x1B, 0x18, 0x19, 0x1E, 0x1F, 0x1C, 0x1D, 
		0x12, 0x13, 0x10, 0x11, 0x16, 0x17, 0x14, 0x15, 
		0x62, 0x63, 0x60, 0x61, 0x66, 0x67, 0x64, 0x65, 
		0x5D, 0x5D, 0x5C, 0x5C, 0x5F, 0x5F, 0x5E, 0x5E, 
		0x74, 0x76, 0x70, 0x72, 0x7C, 0x7E, 0x78, 0x7A, 
		0x6A, 0x6B, 0x68, 0x69, 0x6E, 0x6F, 0x6C, 0x6D, 
		0x48, 0x49, 0x46, 0x47, 0x4C, 0x4D, 0x4A, 0x4B, 
		0x40, 0x41, 0x3F, 0x3F, 0x44, 0x45, 0x42, 0x43, 
		0x56, 0x57, 0x54, 0x55, 0x5A, 0x5B, 0x58, 0x59, 
		0x4F, 0x4F, 0x4E, 0x4E, 0x52, 0x53, 0x50, 0x51, 
		0xA9, 0xAA, 0xA7, 0xA8, 0xAD, 0xAE, 0xAB, 0xAC, 
		0xA1, 0xA2, 0x9F, 0xA0, 0xA5, 0xA6, 0xA3, 0xA4, 
		0xB9, 0xBA, 0xB7, 0xB8, 0xBD, 0xBE, 0xBB, 0xBC, 
		0xB1, 0xB2, 0xAF, 0xB0, 0xB5, 0xB6, 0xB3, 0xB4, 
		0x8A, 0x8B, 0x88, 0x89, 0x8E, 0x8F, 0x8C, 0x8D, 
		0x82, 0x83, 0x80, 0x81, 0x86, 0x87, 0x84, 0x85, 
		0x9A, 0x9B, 0x98, 0x99, 0x9E, 0x9F, 0x9C, 0x9D, 
		0x92, 0x93, 0x90, 0x91, 0x96, 0x97, 0x94, 0x95, 
		0xE2, 0xE3, 0xE0, 0xE1, 0xE6, 0xE7, 0xE4, 0xE5, 
		0xDD, 0xDD, 0xDC, 0xDC, 0xDF, 0xDF, 0xDE, 0xDE, 
		0xF4, 0xF6, 0xF0, 0xF2, 0xFC, 0xFE, 0xF8, 0xFA, 
		0xEA, 0xEB, 0xE8, 0xE9, 0xEE, 0xEF, 0xEC, 0xED, 
		0xC8, 0xC9, 0xC6, 0xC7, 0xCC, 0xCD, 0xCA, 0xCB, 
		0xC0, 0xC1, 0xBF, 0xBF, 0xC4, 0xC5, 0xC2, 0xC3, 
		0xD6, 0xD7, 0xD4, 0xD5, 0xDA, 0xDB, 0xD8, 0xD9, 
		0xCF, 0xCF, 0xCE, 0xCE, 0xD2, 0xD3, 0xD0, 0xD1
	};

        while (len--)
        {
                *buff = table_alaw2ulaw[*(unsigned char *)buff];
                buff++;
	}
}

static ssize_t ixj_read(struct file * file_p, char __user *buf, size_t length, loff_t * ppos)
{
	unsigned long i = *ppos;
	IXJ * j = get_ixj(NUM(file_p->f_path.dentry->d_inode));

	DECLARE_WAITQUEUE(wait, current);

	if (j->flags.inread)
		return -EALREADY;

	j->flags.inread = 1;

	add_wait_queue(&j->read_q, &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	mb();

	while (!j->read_buffer_ready || (j->dtmf_state && j->flags.dtmf_oob)) {
		++j->read_wait;
		if (file_p->f_flags & O_NONBLOCK) {
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&j->read_q, &wait);
			j->flags.inread = 0;
			return -EAGAIN;
		}
		if (!ixj_hookstate(j)) {
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&j->read_q, &wait);
			j->flags.inread = 0;
			return 0;
		}
		interruptible_sleep_on(&j->read_q);
		if (signal_pending(current)) {
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&j->read_q, &wait);
			j->flags.inread = 0;
			return -EINTR;
		}
	}

	remove_wait_queue(&j->read_q, &wait);
	set_current_state(TASK_RUNNING);
	/* Don't ever copy more than the user asks */
	if(j->rec_codec == ALAW)
		ulaw2alaw(j->read_buffer, min(length, j->read_buffer_size));
	i = copy_to_user(buf, j->read_buffer, min(length, j->read_buffer_size));
	j->read_buffer_ready = 0;
	if (i) {
		j->flags.inread = 0;
		return -EFAULT;
	} else {
		j->flags.inread = 0;
		return min(length, j->read_buffer_size);
	}
}

static ssize_t ixj_enhanced_read(struct file * file_p, char __user *buf, size_t length,
			  loff_t * ppos)
{
	int pre_retval;
	ssize_t read_retval = 0;
	IXJ *j = get_ixj(NUM(file_p->f_path.dentry->d_inode));

	pre_retval = ixj_PreRead(j, 0L);
	switch (pre_retval) {
	case NORMAL:
		read_retval = ixj_read(file_p, buf, length, ppos);
		ixj_PostRead(j, 0L);
		break;
	case NOPOST:
		read_retval = ixj_read(file_p, buf, length, ppos);
		break;
	case POSTONLY:
		ixj_PostRead(j, 0L);
		break;
	default:
		read_retval = pre_retval;
	}
	return read_retval;
}

static ssize_t ixj_write(struct file *file_p, const char __user *buf, size_t count, loff_t * ppos)
{
	unsigned long i = *ppos;
	IXJ *j = file_p->private_data;

	DECLARE_WAITQUEUE(wait, current);

	if (j->flags.inwrite)
		return -EALREADY;

	j->flags.inwrite = 1;

	add_wait_queue(&j->write_q, &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	mb();


	while (!j->write_buffers_empty) {
		++j->write_wait;
		if (file_p->f_flags & O_NONBLOCK) {
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&j->write_q, &wait);
			j->flags.inwrite = 0;
			return -EAGAIN;
		}
		if (!ixj_hookstate(j)) {
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&j->write_q, &wait);
			j->flags.inwrite = 0;
			return 0;
		}
		interruptible_sleep_on(&j->write_q);
		if (signal_pending(current)) {
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&j->write_q, &wait);
			j->flags.inwrite = 0;
			return -EINTR;
		}
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&j->write_q, &wait);
	if (j->write_buffer_wp + count >= j->write_buffer_end)
		j->write_buffer_wp = j->write_buffer;
	i = copy_from_user(j->write_buffer_wp, buf, min(count, j->write_buffer_size));
	if (i) {
		j->flags.inwrite = 0;
		return -EFAULT;
	}
       if(j->play_codec == ALAW)
               alaw2ulaw(j->write_buffer_wp, min(count, j->write_buffer_size));
	j->flags.inwrite = 0;
	return min(count, j->write_buffer_size);
}

static ssize_t ixj_enhanced_write(struct file * file_p, const char __user *buf, size_t count, loff_t * ppos)
{
	int pre_retval;
	ssize_t write_retval = 0;

	IXJ *j = get_ixj(NUM(file_p->f_path.dentry->d_inode));

	pre_retval = ixj_PreWrite(j, 0L);
	switch (pre_retval) {
	case NORMAL:
		write_retval = ixj_write(file_p, buf, count, ppos);
		if (write_retval > 0) {
			ixj_PostWrite(j, 0L);
			j->write_buffer_wp += write_retval;
			j->write_buffers_empty--;
		}
		break;
	case NOPOST:
		write_retval = ixj_write(file_p, buf, count, ppos);
		if (write_retval > 0) {
			j->write_buffer_wp += write_retval;
			j->write_buffers_empty--;
		}
		break;
	case POSTONLY:
		ixj_PostWrite(j, 0L);
		break;
	default:
		write_retval = pre_retval;
	}
	return write_retval;
}

static void ixj_read_frame(IXJ *j)
{
	int cnt, dly;

	if (j->read_buffer) {
		for (cnt = 0; cnt < j->rec_frame_size * 2; cnt += 2) {
			if (!(cnt % 16) && !IsRxReady(j)) {
				dly = 0;
				while (!IsRxReady(j)) {
					if (dly++ > 5) {
						dly = 0;
						break;
					}
					udelay(10);
				}
			}
			/* Throw away word 0 of the 8021 compressed format to get standard G.729. */
			if (j->rec_codec == G729 && (cnt == 0 || cnt == 10 || cnt == 20)) {
				inb_p(j->DSPbase + 0x0E);
				inb_p(j->DSPbase + 0x0F);
			}
			*(j->read_buffer + cnt) = inb_p(j->DSPbase + 0x0E);
			*(j->read_buffer + cnt + 1) = inb_p(j->DSPbase + 0x0F);
		}
		++j->framesread;
		if (j->intercom != -1) {
			if (IsTxReady(get_ixj(j->intercom))) {
				for (cnt = 0; cnt < j->rec_frame_size * 2; cnt += 2) {
					if (!(cnt % 16) && !IsTxReady(j)) {
						dly = 0;
						while (!IsTxReady(j)) {
							if (dly++ > 5) {
								dly = 0;
								break;
							}
							udelay(10);
						}
					}
					outb_p(*(j->read_buffer + cnt), get_ixj(j->intercom)->DSPbase + 0x0C);
					outb_p(*(j->read_buffer + cnt + 1), get_ixj(j->intercom)->DSPbase + 0x0D);
				}
				get_ixj(j->intercom)->frameswritten++;
			}
		} else {
			j->read_buffer_ready = 1;
			wake_up_interruptible(&j->read_q);	/* Wake any blocked readers */

			wake_up_interruptible(&j->poll_q);	/* Wake any blocked selects */

			if(j->ixj_signals[SIG_READ_READY])
				ixj_kill_fasync(j, SIG_READ_READY, POLL_OUT);
		}
	}
}

static short fsk[][6][20] =
{
	{
		{
			0, 17846, 29934, 32364, 24351, 8481, -10126, -25465, -32587, -29196,
			-16384, 1715, 19260, 30591, 32051, 23170, 6813, -11743, -26509, -32722
		},
		{
			-28377, -14876, 3425, 20621, 31163, 31650, 21925, 5126, -13328, -27481,
			-32767, -27481, -13328, 5126, 21925, 31650, 31163, 20621, 3425, -14876
		},
		{
			-28377, -32722, -26509, -11743, 6813, 23170, 32051, 30591, 19260, 1715,
			-16384, -29196, -32587, -25465, -10126, 8481, 24351, 32364, 29934, 17846
		},
		{
			0, -17846, -29934, -32364, -24351, -8481, 10126, 25465, 32587, 29196,
			16384, -1715, -19260, -30591, -32051, -23170, -6813, 11743, 26509, 32722
		},
		{
			28377, 14876, -3425, -20621, -31163, -31650, -21925, -5126, 13328, 27481,
			32767, 27481, 13328, -5126, -21925, -31650, -31163, -20621, -3425, 14876
		},
		{
			28377, 32722, 26509, 11743, -6813, -23170, -32051, -30591, -19260, -1715,
			16384, 29196, 32587, 25465, 10126, -8481, -24351, -32364, -29934, -17846
		}
	},
	{
		{
			0, 10126, 19260, 26509, 31163, 32767, 31163, 26509, 19260, 10126,
			0, -10126, -19260, -26509, -31163, -32767, -31163, -26509, -19260, -10126
		},
		{
			-28377, -21925, -13328, -3425, 6813, 16384, 24351, 29934, 32587, 32051,
			28377, 21925, 13328, 3425, -6813, -16384, -24351, -29934, -32587, -32051
		},
		{
			-28377, -32051, -32587, -29934, -24351, -16384, -6813, 3425, 13328, 21925,
			28377, 32051, 32587, 29934, 24351, 16384, 6813, -3425, -13328, -21925
		},
		{
			0, -10126, -19260, -26509, -31163, -32767, -31163, -26509, -19260, -10126,
			0, 10126, 19260, 26509, 31163, 32767, 31163, 26509, 19260, 10126
		},
		{
			28377, 21925, 13328, 3425, -6813, -16383, -24351, -29934, -32587, -32051,
			-28377, -21925, -13328, -3425, 6813, 16383, 24351, 29934, 32587, 32051
		},
		{
			28377, 32051, 32587, 29934, 24351, 16384, 6813, -3425, -13328, -21925,
			-28377, -32051, -32587, -29934, -24351, -16384, -6813, 3425, 13328, 21925
		}
	}
};


static void ixj_write_cid_bit(IXJ *j, int bit)
{
	while (j->fskcnt < 20) {
		if(j->fskdcnt < (j->fsksize - 1))
			j->fskdata[j->fskdcnt++] = fsk[bit][j->fskz][j->fskcnt];

		j->fskcnt += 3;
	}
	j->fskcnt %= 20;

	if (!bit)
		j->fskz++;
	if (j->fskz >= 6)
		j->fskz = 0;

}

static void ixj_write_cid_byte(IXJ *j, char byte)
{
	IXJ_CBYTE cb;

		cb.cbyte = byte;
		ixj_write_cid_bit(j, 0);
		ixj_write_cid_bit(j, cb.cbits.b0 ? 1 : 0);
		ixj_write_cid_bit(j, cb.cbits.b1 ? 1 : 0);
		ixj_write_cid_bit(j, cb.cbits.b2 ? 1 : 0);
		ixj_write_cid_bit(j, cb.cbits.b3 ? 1 : 0);
		ixj_write_cid_bit(j, cb.cbits.b4 ? 1 : 0);
		ixj_write_cid_bit(j, cb.cbits.b5 ? 1 : 0);
		ixj_write_cid_bit(j, cb.cbits.b6 ? 1 : 0);
		ixj_write_cid_bit(j, cb.cbits.b7 ? 1 : 0);
		ixj_write_cid_bit(j, 1);
}

static void ixj_write_cid_seize(IXJ *j)
{
	int cnt;

	for (cnt = 0; cnt < 150; cnt++) {
		ixj_write_cid_bit(j, 0);
		ixj_write_cid_bit(j, 1);
	}
	for (cnt = 0; cnt < 180; cnt++) {
		ixj_write_cid_bit(j, 1);
	}
}

static void ixj_write_cidcw_seize(IXJ *j)
{
	int cnt;

	for (cnt = 0; cnt < 80; cnt++) {
		ixj_write_cid_bit(j, 1);
	}
}

static int ixj_write_cid_string(IXJ *j, char *s, int checksum)
{
	int cnt;

	for (cnt = 0; cnt < strlen(s); cnt++) {
		ixj_write_cid_byte(j, s[cnt]);
		checksum = (checksum + s[cnt]);
	}
	return checksum;
}

static void ixj_pad_fsk(IXJ *j, int pad)
{
	int cnt; 

	for (cnt = 0; cnt < pad; cnt++) {
		if(j->fskdcnt < (j->fsksize - 1))
			j->fskdata[j->fskdcnt++] = 0x0000;
	}
	for (cnt = 0; cnt < 720; cnt++) {
		if(j->fskdcnt < (j->fsksize - 1))
			j->fskdata[j->fskdcnt++] = 0x0000;
	}
}

static void ixj_pre_cid(IXJ *j)
{
	j->cid_play_codec = j->play_codec;
	j->cid_play_frame_size = j->play_frame_size;
	j->cid_play_volume = get_play_volume(j);
	j->cid_play_flag = j->flags.playing;

	j->cid_rec_codec = j->rec_codec;
	j->cid_rec_volume = get_rec_volume(j);
	j->cid_rec_flag = j->flags.recording;

	j->cid_play_aec_level = j->aec_level;

	switch(j->baseframe.low) {
		case 0xA0:
			j->cid_base_frame_size = 20;
			break;
		case 0x50:
			j->cid_base_frame_size = 10;
			break;
		case 0xF0:
			j->cid_base_frame_size = 30;
			break;
	}

	ixj_play_stop(j);
	ixj_cpt_stop(j);

	j->flags.cidplay = 1;

	set_base_frame(j, 30);
	set_play_codec(j, LINEAR16);
	set_play_volume(j, 0x1B);
	ixj_play_start(j);
}

static void ixj_post_cid(IXJ *j)
{
	ixj_play_stop(j);

	if(j->cidsize > 5000) {
		SLIC_SetState(PLD_SLIC_STATE_STANDBY, j);
	}
	j->flags.cidplay = 0;
	if(ixjdebug & 0x0200) {
		printk("IXJ phone%d Finished Playing CallerID data %ld\n", j->board, jiffies);
	}

	ixj_fsk_free(j);

	j->fskdcnt = 0;
	set_base_frame(j, j->cid_base_frame_size);
	set_play_codec(j, j->cid_play_codec);
	ixj_aec_start(j, j->cid_play_aec_level);
	set_play_volume(j, j->cid_play_volume);

	set_rec_codec(j, j->cid_rec_codec);
	set_rec_volume(j, j->cid_rec_volume);

	if(j->cid_rec_flag)
		ixj_record_start(j);

	if(j->cid_play_flag)
		ixj_play_start(j);

	if(j->cid_play_flag) {
		wake_up_interruptible(&j->write_q);	/* Wake any blocked writers */
	}
}

static void ixj_write_cid(IXJ *j)
{
	char sdmf1[50];
	char sdmf2[50];
	char sdmf3[80];
	char mdmflen, len1, len2, len3;
	int pad;

	int checksum = 0;

	if (j->dsp.low == 0x20 || j->flags.cidplay)
		return;

	j->fskz = j->fskphase = j->fskcnt = j->fskdcnt = 0;
	j->cidsize = j->cidcnt = 0;

	ixj_fsk_alloc(j);

	strcpy(sdmf1, j->cid_send.month);
	strcat(sdmf1, j->cid_send.day);
	strcat(sdmf1, j->cid_send.hour);
	strcat(sdmf1, j->cid_send.min);
	strcpy(sdmf2, j->cid_send.number);
	strcpy(sdmf3, j->cid_send.name);

	len1 = strlen(sdmf1);
	len2 = strlen(sdmf2);
	len3 = strlen(sdmf3);
	mdmflen = len1 + len2 + len3 + 6;

	while(1){
		ixj_write_cid_seize(j);

		ixj_write_cid_byte(j, 0x80);
		checksum = 0x80;
		ixj_write_cid_byte(j, mdmflen);
		checksum = checksum + mdmflen;

		ixj_write_cid_byte(j, 0x01);
		checksum = checksum + 0x01;
		ixj_write_cid_byte(j, len1);
		checksum = checksum + len1;
		checksum = ixj_write_cid_string(j, sdmf1, checksum);
		if(ixj_hookstate(j) & 1)
			break;

		ixj_write_cid_byte(j, 0x02);
		checksum = checksum + 0x02;
		ixj_write_cid_byte(j, len2);
		checksum = checksum + len2;
		checksum = ixj_write_cid_string(j, sdmf2, checksum);
		if(ixj_hookstate(j) & 1)
			break;

		ixj_write_cid_byte(j, 0x07);
		checksum = checksum + 0x07;
		ixj_write_cid_byte(j, len3);
		checksum = checksum + len3;
		checksum = ixj_write_cid_string(j, sdmf3, checksum);
		if(ixj_hookstate(j) & 1)
			break;

		checksum %= 256;
		checksum ^= 0xFF;
		checksum += 1;

		ixj_write_cid_byte(j, (char) checksum);

		pad = j->fskdcnt % 240;
		if (pad) {
			pad = 240 - pad;
		}
		ixj_pad_fsk(j, pad);
		break;
	}

	ixj_write_frame(j);
}

static void ixj_write_cidcw(IXJ *j)
{
	IXJ_TONE ti;

	char sdmf1[50];
	char sdmf2[50];
	char sdmf3[80];
	char mdmflen, len1, len2, len3;
	int pad;

	int checksum = 0;

	if (j->dsp.low == 0x20 || j->flags.cidplay)
		return;

	j->fskz = j->fskphase = j->fskcnt = j->fskdcnt = 0;
	j->cidsize = j->cidcnt = 0;

	ixj_fsk_alloc(j);

	j->flags.cidcw_ack = 0;

	ti.tone_index = 23;
	ti.gain0 = 1;
	ti.freq0 = hz440;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(j, &ti);

	ixj_set_tone_on(1500, j);
	ixj_set_tone_off(32, j);
	if(ixjdebug & 0x0200) {
		printk("IXJ cidcw phone%d first tone start at %ld\n", j->board, jiffies);
	}
	ixj_play_tone(j, 23);

	clear_bit(j->board, &j->busyflags);
	while(j->tone_state)
		schedule_timeout_interruptible(1);
	while(test_and_set_bit(j->board, (void *)&j->busyflags) != 0)
		schedule_timeout_interruptible(1);
	if(ixjdebug & 0x0200) {
		printk("IXJ cidcw phone%d first tone end at %ld\n", j->board, jiffies);
	}

	ti.tone_index = 24;
	ti.gain0 = 1;
	ti.freq0 = hz2130;
	ti.gain1 = 0;
	ti.freq1 = hz2750;
	ixj_init_tone(j, &ti);

	ixj_set_tone_off(10, j);
	ixj_set_tone_on(600, j);
	if(ixjdebug & 0x0200) {
		printk("IXJ cidcw phone%d second tone start at %ld\n", j->board, jiffies);
	}
	ixj_play_tone(j, 24);

	clear_bit(j->board, &j->busyflags);
	while(j->tone_state)
		schedule_timeout_interruptible(1);
	while(test_and_set_bit(j->board, (void *)&j->busyflags) != 0)
		schedule_timeout_interruptible(1);
	if(ixjdebug & 0x0200) {
		printk("IXJ cidcw phone%d sent second tone at %ld\n", j->board, jiffies);
	}

	j->cidcw_wait = jiffies + ((50 * hertz) / 100);

	clear_bit(j->board, &j->busyflags);
	while(!j->flags.cidcw_ack && time_before(jiffies, j->cidcw_wait))
		schedule_timeout_interruptible(1);
	while(test_and_set_bit(j->board, (void *)&j->busyflags) != 0)
		schedule_timeout_interruptible(1);
	j->cidcw_wait = 0;
	if(!j->flags.cidcw_ack) {
		if(ixjdebug & 0x0200) {
			printk("IXJ cidcw phone%d did not receive ACK from display %ld\n", j->board, jiffies);
		}
		ixj_post_cid(j);
		if(j->cid_play_flag) {
			wake_up_interruptible(&j->write_q);	/* Wake any blocked readers */
		}
		return;
	} else {
		ixj_pre_cid(j);
	}
	j->flags.cidcw_ack = 0;
	strcpy(sdmf1, j->cid_send.month);
	strcat(sdmf1, j->cid_send.day);
	strcat(sdmf1, j->cid_send.hour);
	strcat(sdmf1, j->cid_send.min);
	strcpy(sdmf2, j->cid_send.number);
	strcpy(sdmf3, j->cid_send.name);

	len1 = strlen(sdmf1);
	len2 = strlen(sdmf2);
	len3 = strlen(sdmf3);
	mdmflen = len1 + len2 + len3 + 6;

	ixj_write_cidcw_seize(j);

	ixj_write_cid_byte(j, 0x80);
	checksum = 0x80;
	ixj_write_cid_byte(j, mdmflen);
	checksum = checksum + mdmflen;

	ixj_write_cid_byte(j, 0x01);
	checksum = checksum + 0x01;
	ixj_write_cid_byte(j, len1);
	checksum = checksum + len1;
	checksum = ixj_write_cid_string(j, sdmf1, checksum);

	ixj_write_cid_byte(j, 0x02);
	checksum = checksum + 0x02;
	ixj_write_cid_byte(j, len2);
	checksum = checksum + len2;
	checksum = ixj_write_cid_string(j, sdmf2, checksum);

	ixj_write_cid_byte(j, 0x07);
	checksum = checksum + 0x07;
	ixj_write_cid_byte(j, len3);
	checksum = checksum + len3;
	checksum = ixj_write_cid_string(j, sdmf3, checksum);

	checksum %= 256;
	checksum ^= 0xFF;
	checksum += 1;

	ixj_write_cid_byte(j, (char) checksum);

	pad = j->fskdcnt % 240;
	if (pad) {
		pad = 240 - pad;
	}
	ixj_pad_fsk(j, pad);
	if(ixjdebug & 0x0200) {
		printk("IXJ cidcw phone%d sent FSK data at %ld\n", j->board, jiffies);
	}
}

static void ixj_write_vmwi(IXJ *j, int msg)
{
	char mdmflen;
	int pad;

	int checksum = 0;

	if (j->dsp.low == 0x20 || j->flags.cidplay)
		return;

	j->fskz = j->fskphase = j->fskcnt = j->fskdcnt = 0;
	j->cidsize = j->cidcnt = 0;

	ixj_fsk_alloc(j);

	mdmflen = 3;

	if (j->port == PORT_POTS)
		SLIC_SetState(PLD_SLIC_STATE_OHT, j);

	ixj_write_cid_seize(j);

	ixj_write_cid_byte(j, 0x82);
	checksum = 0x82;
	ixj_write_cid_byte(j, mdmflen);
	checksum = checksum + mdmflen;

	ixj_write_cid_byte(j, 0x0B);
	checksum = checksum + 0x0B;
	ixj_write_cid_byte(j, 1);
	checksum = checksum + 1;

	if(msg) {
		ixj_write_cid_byte(j, 0xFF);
		checksum = checksum + 0xFF;
	}
	else {
		ixj_write_cid_byte(j, 0x00);
		checksum = checksum + 0x00;
	}

	checksum %= 256;
	checksum ^= 0xFF;
	checksum += 1;

	ixj_write_cid_byte(j, (char) checksum);

	pad = j->fskdcnt % 240;
	if (pad) {
		pad = 240 - pad;
	}
	ixj_pad_fsk(j, pad);
}

static void ixj_write_frame(IXJ *j)
{
	int cnt, frame_count, dly;
	IXJ_WORD dat;
	BYTES blankword;

	frame_count = 0;
	if(j->flags.cidplay) {
		for(cnt = 0; cnt < 480; cnt++) {
			if (!(cnt % 16) && !IsTxReady(j)) {
				dly = 0;
				while (!IsTxReady(j)) {
					if (dly++ > 5) {
						dly = 0;
						break;
					}
					udelay(10);
				}
			}
			dat.word = j->fskdata[j->cidcnt++];
			outb_p(dat.bytes.low, j->DSPbase + 0x0C);
			outb_p(dat.bytes.high, j->DSPbase + 0x0D);
			cnt++;
		}
		if(j->cidcnt >= j->fskdcnt) {
			ixj_post_cid(j);
		}
		/* This may seem rude, but if we just played one frame of FSK data for CallerID
		   and there is real audio data in the buffer, we need to throw it away because 
		   we just used it's time slot */
		if (j->write_buffer_rp > j->write_buffer_wp) {
			j->write_buffer_rp += j->cid_play_frame_size * 2;
			if (j->write_buffer_rp >= j->write_buffer_end) {
				j->write_buffer_rp = j->write_buffer;
			}
			j->write_buffers_empty++;
			wake_up_interruptible(&j->write_q);	/* Wake any blocked writers */

			wake_up_interruptible(&j->poll_q);	/* Wake any blocked selects */
		}
	} else if (j->write_buffer && j->write_buffers_empty < 1) { 
		if (j->write_buffer_wp > j->write_buffer_rp) {
			frame_count =
			    (j->write_buffer_wp - j->write_buffer_rp) / (j->play_frame_size * 2);
		}
		if (j->write_buffer_rp > j->write_buffer_wp) {
			frame_count =
			    (j->write_buffer_wp - j->write_buffer) / (j->play_frame_size * 2) +
			    (j->write_buffer_end - j->write_buffer_rp) / (j->play_frame_size * 2);
		}
		if (frame_count >= 1) {
			if (j->ver.low == 0x12 && j->play_mode && j->flags.play_first_frame) {
				switch (j->play_mode) {
				case PLAYBACK_MODE_ULAW:
				case PLAYBACK_MODE_ALAW:
					blankword.low = blankword.high = 0xFF;
					break;
				case PLAYBACK_MODE_8LINEAR:
				case PLAYBACK_MODE_16LINEAR:
					blankword.low = blankword.high = 0x00;
					break;
				case PLAYBACK_MODE_8LINEAR_WSS:
					blankword.low = blankword.high = 0x80;
					break;
				}
				for (cnt = 0; cnt < 16; cnt++) {
					if (!(cnt % 16) && !IsTxReady(j)) {
						dly = 0;
						while (!IsTxReady(j)) {
							if (dly++ > 5) {
								dly = 0;
								break;
							}
							udelay(10);
						}
					}
					outb_p((blankword.low), j->DSPbase + 0x0C);
					outb_p((blankword.high), j->DSPbase + 0x0D);
				}
				j->flags.play_first_frame = 0;
			} else	if (j->play_codec == G723_63 && j->flags.play_first_frame) {
				for (cnt = 0; cnt < 24; cnt++) {
					if(cnt == 12) {
						blankword.low = 0x02;
						blankword.high = 0x00;
					}
					else {
						blankword.low = blankword.high = 0x00;
					}
					if (!(cnt % 16) && !IsTxReady(j)) {
						dly = 0;
						while (!IsTxReady(j)) {
							if (dly++ > 5) {
								dly = 0;
								break;
							}
							udelay(10);
						}
					}
					outb_p((blankword.low), j->DSPbase + 0x0C);
					outb_p((blankword.high), j->DSPbase + 0x0D);
				}
				j->flags.play_first_frame = 0;
			}
			for (cnt = 0; cnt < j->play_frame_size * 2; cnt += 2) {
				if (!(cnt % 16) && !IsTxReady(j)) {
					dly = 0;
					while (!IsTxReady(j)) {
						if (dly++ > 5) {
							dly = 0;
							break;
						}
						udelay(10);
					}
				}
			/* Add word 0 to G.729 frames for the 8021.  Right now we don't do VAD/CNG  */
				if (j->play_codec == G729 && (cnt == 0 || cnt == 10 || cnt == 20)) {
					if (j->write_buffer_rp[cnt] == 0 &&
					    j->write_buffer_rp[cnt + 1] == 0 &&
					    j->write_buffer_rp[cnt + 2] == 0 &&
					    j->write_buffer_rp[cnt + 3] == 0 &&
					    j->write_buffer_rp[cnt + 4] == 0 &&
					    j->write_buffer_rp[cnt + 5] == 0 &&
					    j->write_buffer_rp[cnt + 6] == 0 &&
					    j->write_buffer_rp[cnt + 7] == 0 &&
					    j->write_buffer_rp[cnt + 8] == 0 &&
					    j->write_buffer_rp[cnt + 9] == 0) {
					/* someone is trying to write silence lets make this a type 0 frame. */
						outb_p(0x00, j->DSPbase + 0x0C);
						outb_p(0x00, j->DSPbase + 0x0D);
					} else {
					/* so all other frames are type 1. */
						outb_p(0x01, j->DSPbase + 0x0C);
						outb_p(0x00, j->DSPbase + 0x0D);
					}
				}
				outb_p(*(j->write_buffer_rp + cnt), j->DSPbase + 0x0C);
				outb_p(*(j->write_buffer_rp + cnt + 1), j->DSPbase + 0x0D);
				*(j->write_buffer_rp + cnt) = 0;
				*(j->write_buffer_rp + cnt + 1) = 0;
			}
			j->write_buffer_rp += j->play_frame_size * 2;
			if (j->write_buffer_rp >= j->write_buffer_end) {
				j->write_buffer_rp = j->write_buffer;
			}
			j->write_buffers_empty++;
			wake_up_interruptible(&j->write_q);	/* Wake any blocked writers */

			wake_up_interruptible(&j->poll_q);	/* Wake any blocked selects */

			++j->frameswritten;
		}
	} else {
		j->drybuffer++;
	}
	if(j->ixj_signals[SIG_WRITE_READY]) {
		ixj_kill_fasync(j, SIG_WRITE_READY, POLL_OUT);
	}
}

static int idle(IXJ *j)
{
	if (ixj_WriteDSPCommand(0x0000, j))		/* DSP Idle */

		return 0;

	if (j->ssr.high || j->ssr.low) {
		return 0;
	} else {
		j->play_mode = -1;
		j->flags.playing = 0;
		j->rec_mode = -1;
		j->flags.recording = 0;
		return 1;
        }
}

static int set_base_frame(IXJ *j, int size)
{
	unsigned short cmd;
	int cnt;

	idle(j);
	j->cid_play_aec_level = j->aec_level;
	aec_stop(j);
	for (cnt = 0; cnt < 10; cnt++) {
		if (idle(j))
			break;
	}
	if (j->ssr.high || j->ssr.low)
		return -1;
	if (j->dsp.low != 0x20) {
		switch (size) {
		case 30:
			cmd = 0x07F0;
			/* Set Base Frame Size to 240 pg9-10 8021 */
			break;
		case 20:
			cmd = 0x07A0;
			/* Set Base Frame Size to 160 pg9-10 8021 */
			break;
		case 10:
			cmd = 0x0750;
			/* Set Base Frame Size to 80 pg9-10 8021 */
			break;
		default:
			return -1;
		}
	} else {
		if (size == 30)
			return size;
		else
			return -1;
	}
	if (ixj_WriteDSPCommand(cmd, j)) {
		j->baseframe.high = j->baseframe.low = 0xFF;
		return -1;
	} else {
		j->baseframe.high = j->ssr.high;
		j->baseframe.low = j->ssr.low;
		/* If the status returned is 0x0000 (pg9-9 8021) the call failed */
		if(j->baseframe.high == 0x00 && j->baseframe.low == 0x00) {
			return -1;
		}
	}
	ixj_aec_start(j, j->cid_play_aec_level);
	return size;
}

static int set_rec_codec(IXJ *j, int rate)
{
	int retval = 0;

	j->rec_codec = rate;

	switch (rate) {
	case G723_63:
		if (j->ver.low != 0x12 || ixj_convert_loaded) {
			j->rec_frame_size = 12;
			j->rec_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case G723_53:
		if (j->ver.low != 0x12 || ixj_convert_loaded) {
			j->rec_frame_size = 10;
			j->rec_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case TS85:
		if (j->dsp.low == 0x20 || j->flags.ts85_loaded) {
			j->rec_frame_size = 16;
			j->rec_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case TS48:
		if (j->ver.low != 0x12 || ixj_convert_loaded) {
			j->rec_frame_size = 9;
			j->rec_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case TS41:
		if (j->ver.low != 0x12 || ixj_convert_loaded) {
			j->rec_frame_size = 8;
			j->rec_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case G728:
		if (j->dsp.low != 0x20) {
			j->rec_frame_size = 48;
			j->rec_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case G729:
		if (j->dsp.low != 0x20) {
			if (!j->flags.g729_loaded) {
				retval = 1;
				break;
			}
			switch (j->baseframe.low) {
			case 0xA0:
				j->rec_frame_size = 10;
				break;
			case 0x50:
				j->rec_frame_size = 5;
				break;
			default:
				j->rec_frame_size = 15;
				break;
			}
			j->rec_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case G729B:
		if (j->dsp.low != 0x20) {
			if (!j->flags.g729_loaded) {
				retval = 1;
				break;
			}
			switch (j->baseframe.low) {
			case 0xA0:
				j->rec_frame_size = 12;
				break;
			case 0x50:
				j->rec_frame_size = 6;
				break;
			default:
				j->rec_frame_size = 18;
				break;
			}
			j->rec_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case ULAW:
		switch (j->baseframe.low) {
		case 0xA0:
			j->rec_frame_size = 80;
			break;
		case 0x50:
			j->rec_frame_size = 40;
			break;
		default:
			j->rec_frame_size = 120;
			break;
		}
		j->rec_mode = 4;
		break;
	case ALAW:
		switch (j->baseframe.low) {
		case 0xA0:
			j->rec_frame_size = 80;
			break;
		case 0x50:
			j->rec_frame_size = 40;
			break;
		default:
			j->rec_frame_size = 120;
			break;
		}
		j->rec_mode = 4;
		break;
	case LINEAR16:
		switch (j->baseframe.low) {
		case 0xA0:
			j->rec_frame_size = 160;
			break;
		case 0x50:
			j->rec_frame_size = 80;
			break;
		default:
			j->rec_frame_size = 240;
			break;
		}
		j->rec_mode = 5;
		break;
	case LINEAR8:
		switch (j->baseframe.low) {
		case 0xA0:
			j->rec_frame_size = 80;
			break;
		case 0x50:
			j->rec_frame_size = 40;
			break;
		default:
			j->rec_frame_size = 120;
			break;
		}
		j->rec_mode = 6;
		break;
	case WSS:
		switch (j->baseframe.low) {
		case 0xA0:
			j->rec_frame_size = 80;
			break;
		case 0x50:
			j->rec_frame_size = 40;
			break;
		default:
			j->rec_frame_size = 120;
			break;
		}
		j->rec_mode = 7;
		break;
	default:
		kfree(j->read_buffer);
		j->rec_frame_size = 0;
		j->rec_mode = -1;
		j->read_buffer = NULL;
		j->read_buffer_size = 0;
		retval = 1;
		break;
	}
	return retval;
}

static int ixj_record_start(IXJ *j)
{
	unsigned short cmd = 0x0000;

	if (j->read_buffer) {
		ixj_record_stop(j);
	}
	j->flags.recording = 1;
	ixj_WriteDSPCommand(0x0FE0, j);	/* Put the DSP in full power mode. */

	if(ixjdebug & 0x0002)
		printk("IXJ %d Starting Record Codec %d at %ld\n", j->board, j->rec_codec, jiffies);

	if (!j->rec_mode) {
		switch (j->rec_codec) {
		case G723_63:
			cmd = 0x5131;
			break;
		case G723_53:
			cmd = 0x5132;
			break;
		case TS85:
			cmd = 0x5130;	/* TrueSpeech 8.5 */

			break;
		case TS48:
			cmd = 0x5133;	/* TrueSpeech 4.8 */

			break;
		case TS41:
			cmd = 0x5134;	/* TrueSpeech 4.1 */

			break;
		case G728:
			cmd = 0x5135;
			break;
		case G729:
		case G729B:
			cmd = 0x5136;
			break;
		default:
			return 1;
		}
		if (ixj_WriteDSPCommand(cmd, j))
			return -1;
	}
	if (!j->read_buffer) {
		if (!j->read_buffer)
			j->read_buffer = kmalloc(j->rec_frame_size * 2, GFP_ATOMIC);
		if (!j->read_buffer) {
			printk("Read buffer allocation for ixj board %d failed!\n", j->board);
			return -ENOMEM;
		}
	}
	j->read_buffer_size = j->rec_frame_size * 2;

	if (ixj_WriteDSPCommand(0x5102, j))		/* Set Poll sync mode */

		return -1;

	switch (j->rec_mode) {
	case 0:
		cmd = 0x1C03;	/* Record C1 */

		break;
	case 4:
		if (j->ver.low == 0x12) {
			cmd = 0x1E03;	/* Record C1 */

		} else {
			cmd = 0x1E01;	/* Record C1 */

		}
		break;
	case 5:
		if (j->ver.low == 0x12) {
			cmd = 0x1E83;	/* Record C1 */

		} else {
			cmd = 0x1E81;	/* Record C1 */

		}
		break;
	case 6:
		if (j->ver.low == 0x12) {
			cmd = 0x1F03;	/* Record C1 */

		} else {
			cmd = 0x1F01;	/* Record C1 */

		}
		break;
	case 7:
		if (j->ver.low == 0x12) {
			cmd = 0x1F83;	/* Record C1 */
		} else {
			cmd = 0x1F81;	/* Record C1 */
		}
		break;
	}
	if (ixj_WriteDSPCommand(cmd, j))
		return -1;

	if (j->flags.playing) {
		ixj_aec_start(j, j->aec_level);
	}
	return 0;
}

static void ixj_record_stop(IXJ *j)
{
	if (ixjdebug & 0x0002)
		printk("IXJ %d Stopping Record Codec %d at %ld\n", j->board, j->rec_codec, jiffies);

	kfree(j->read_buffer);
	j->read_buffer = NULL;
	j->read_buffer_size = 0;
	if (j->rec_mode > -1) {
		ixj_WriteDSPCommand(0x5120, j);
		j->rec_mode = -1;
	}
	j->flags.recording = 0;
}
static void ixj_vad(IXJ *j, int arg)
{
	if (arg)
		ixj_WriteDSPCommand(0x513F, j);
	else
		ixj_WriteDSPCommand(0x513E, j);
}

static void set_rec_depth(IXJ *j, int depth)
{
	if (depth > 60)
		depth = 60;
	if (depth < 0)
		depth = 0;
	ixj_WriteDSPCommand(0x5180 + depth, j);
}

static void set_dtmf_prescale(IXJ *j, int volume)
{
	ixj_WriteDSPCommand(0xCF07, j);
	ixj_WriteDSPCommand(volume, j);
}

static int get_dtmf_prescale(IXJ *j)
{
	ixj_WriteDSPCommand(0xCF05, j);
	return j->ssr.high << 8 | j->ssr.low;
}

static void set_rec_volume(IXJ *j, int volume)
{
	if(j->aec_level == AEC_AGC) {
		if (ixjdebug & 0x0002)
			printk(KERN_INFO "IXJ: /dev/phone%d Setting AGC Threshold to 0x%4.4x\n", j->board, volume);
		ixj_WriteDSPCommand(0xCF96, j);
		ixj_WriteDSPCommand(volume, j);
	} else {
		if (ixjdebug & 0x0002)
			printk(KERN_INFO "IXJ: /dev/phone %d Setting Record Volume to 0x%4.4x\n", j->board, volume);
		ixj_WriteDSPCommand(0xCF03, j);
		ixj_WriteDSPCommand(volume, j);
	}
}

static int set_rec_volume_linear(IXJ *j, int volume)
{
	int newvolume, dsprecmax;

	if (ixjdebug & 0x0002)
		printk(KERN_INFO "IXJ: /dev/phone %d Setting Linear Record Volume to 0x%4.4x\n", j->board, volume);
	if(volume > 100 || volume < 0) {
	  return -1;
	}

	/* This should normalize the perceived volumes between the different cards caused by differences in the hardware */
	switch (j->cardtype) {
	case QTI_PHONEJACK:
		dsprecmax = 0x440;
		break;
	case QTI_LINEJACK:
		dsprecmax = 0x180;
		ixj_mixer(0x0203, j);	/*Voice Left Volume unmute 6db */
		ixj_mixer(0x0303, j);	/*Voice Right Volume unmute 6db */
		ixj_mixer(0x0C00, j);	/*Mono1 unmute 12db */
		break;
	case QTI_PHONEJACK_LITE:
		dsprecmax = 0x4C0;
		break;
	case QTI_PHONEJACK_PCI:
		dsprecmax = 0x100;
		break;
	case QTI_PHONECARD:
		dsprecmax = 0x400;
		break;
	default:
		return -1;
	}
	newvolume = (dsprecmax * volume) / 100;
	set_rec_volume(j, newvolume);
	return 0;
}

static int get_rec_volume(IXJ *j)
{
	if(j->aec_level == AEC_AGC) {
		if (ixjdebug & 0x0002)
			printk(KERN_INFO "Getting AGC Threshold\n");
		ixj_WriteDSPCommand(0xCF86, j);
		if (ixjdebug & 0x0002)
			printk(KERN_INFO "AGC Threshold is 0x%2.2x%2.2x\n", j->ssr.high, j->ssr.low);
		return j->ssr.high << 8 | j->ssr.low;
	} else {
		if (ixjdebug & 0x0002)
			printk(KERN_INFO "Getting Record Volume\n");
		ixj_WriteDSPCommand(0xCF01, j);
		return j->ssr.high << 8 | j->ssr.low;
	}
}

static int get_rec_volume_linear(IXJ *j)
{
	int volume, newvolume, dsprecmax;

	switch (j->cardtype) {
	case QTI_PHONEJACK:
		dsprecmax = 0x440;
		break;
	case QTI_LINEJACK:
		dsprecmax = 0x180;
		break;
	case QTI_PHONEJACK_LITE:
		dsprecmax = 0x4C0;
		break;
	case QTI_PHONEJACK_PCI:
		dsprecmax = 0x100;
		break;
	case QTI_PHONECARD:
		dsprecmax = 0x400;
		break;
	default:
		return -1;
	}
	volume = get_rec_volume(j);
	newvolume = (volume * 100) / dsprecmax;
	if(newvolume > 100)
		newvolume = 100;
	return newvolume;
}

static int get_rec_level(IXJ *j)
{
	int retval;

	ixj_WriteDSPCommand(0xCF88, j);

	retval = j->ssr.high << 8 | j->ssr.low;
	retval = (retval * 256) / 240;
	return retval;
}

static void ixj_aec_start(IXJ *j, int level)
{
	j->aec_level = level;
	if (ixjdebug & 0x0002)
		printk(KERN_INFO "AGC set = 0x%2.2x\n", j->aec_level);
	if (!level) {
		aec_stop(j);
	} else {
		if (j->rec_codec == G729 || j->play_codec == G729 || j->rec_codec == G729B || j->play_codec == G729B) {
			ixj_WriteDSPCommand(0xE022, j);	/* Move AEC filter buffer */

			ixj_WriteDSPCommand(0x0300, j);
		}
		ixj_WriteDSPCommand(0xB001, j);	/* AEC On */

		ixj_WriteDSPCommand(0xE013, j);	/* Advanced AEC C1 */

		switch (level) {
		case AEC_LOW:
			ixj_WriteDSPCommand(0x0000, j);	/* Advanced AEC C2 = off */

			ixj_WriteDSPCommand(0xE011, j);
			ixj_WriteDSPCommand(0xFFFF, j);

			ixj_WriteDSPCommand(0xCF97, j);	/* Set AGC Enable */
			ixj_WriteDSPCommand(0x0000, j);	/* to off */
			
			break;

		case AEC_MED:
			ixj_WriteDSPCommand(0x0600, j);	/* Advanced AEC C2 = on medium */

			ixj_WriteDSPCommand(0xE011, j);
			ixj_WriteDSPCommand(0x0080, j);

			ixj_WriteDSPCommand(0xCF97, j);	/* Set AGC Enable */
			ixj_WriteDSPCommand(0x0000, j);	/* to off */
			
			break;

		case AEC_HIGH:
			ixj_WriteDSPCommand(0x0C00, j);	/* Advanced AEC C2 = on high */

			ixj_WriteDSPCommand(0xE011, j);
			ixj_WriteDSPCommand(0x0080, j);

			ixj_WriteDSPCommand(0xCF97, j);	/* Set AGC Enable */
			ixj_WriteDSPCommand(0x0000, j);	/* to off */
			
			break;

		case AEC_AGC:
                        /* First we have to put the AEC into advance auto mode so that AGC will not conflict with it */
			ixj_WriteDSPCommand(0x0002, j);	/* Attenuation scaling factor of 2 */

			ixj_WriteDSPCommand(0xE011, j);
			ixj_WriteDSPCommand(0x0100, j);	/* Higher Threshold Floor */

			ixj_WriteDSPCommand(0xE012, j);	/* Set Train and Lock */

			if(j->cardtype == QTI_LINEJACK || j->cardtype == QTI_PHONECARD)
				ixj_WriteDSPCommand(0x0224, j);
			else
				ixj_WriteDSPCommand(0x1224, j);

			ixj_WriteDSPCommand(0xE014, j);
			ixj_WriteDSPCommand(0x0003, j);	/* Lock threashold at 3dB */

			ixj_WriteDSPCommand(0xE338, j);	/* Set Echo Suppresser Attenuation to 0dB */

			/* Now we can set the AGC initial parameters and turn it on */
			ixj_WriteDSPCommand(0xCF90, j);	/* Set AGC Minumum gain */
			ixj_WriteDSPCommand(0x0020, j);	/* to 0.125 (-18dB) */
	
			ixj_WriteDSPCommand(0xCF91, j);	/* Set AGC Maximum gain */
			ixj_WriteDSPCommand(0x1000, j);	/* to 16 (24dB) */
			
			ixj_WriteDSPCommand(0xCF92, j);	/* Set AGC start gain */
			ixj_WriteDSPCommand(0x0800, j);	/* to 8 (+18dB) */
		
			ixj_WriteDSPCommand(0xCF93, j);	/* Set AGC hold time */
			ixj_WriteDSPCommand(0x1F40, j);	/* to 2 seconds (units are 250us) */
			
			ixj_WriteDSPCommand(0xCF94, j);	/* Set AGC Attack Time Constant */
			ixj_WriteDSPCommand(0x0005, j);	/* to 8ms */
			
			ixj_WriteDSPCommand(0xCF95, j);	/* Set AGC Decay Time Constant */
			ixj_WriteDSPCommand(0x000D, j);	/* to 4096ms */
			
			ixj_WriteDSPCommand(0xCF96, j);	/* Set AGC Attack Threshold */
			ixj_WriteDSPCommand(0x1200, j);	/* to 25% */
			
			ixj_WriteDSPCommand(0xCF97, j);	/* Set AGC Enable */
			ixj_WriteDSPCommand(0x0001, j);	/* to on */
			
			break;

		case AEC_AUTO:
			ixj_WriteDSPCommand(0x0002, j);	/* Attenuation scaling factor of 2 */

			ixj_WriteDSPCommand(0xE011, j);
			ixj_WriteDSPCommand(0x0100, j);	/* Higher Threshold Floor */

			ixj_WriteDSPCommand(0xE012, j);	/* Set Train and Lock */

			if(j->cardtype == QTI_LINEJACK || j->cardtype == QTI_PHONECARD)
				ixj_WriteDSPCommand(0x0224, j);
			else
				ixj_WriteDSPCommand(0x1224, j);

			ixj_WriteDSPCommand(0xE014, j);
			ixj_WriteDSPCommand(0x0003, j);	/* Lock threashold at 3dB */

			ixj_WriteDSPCommand(0xE338, j);	/* Set Echo Suppresser Attenuation to 0dB */

			break;
		}
	}
}

static void aec_stop(IXJ *j)
{
	j->aec_level = AEC_OFF;
	if (j->rec_codec == G729 || j->play_codec == G729 || j->rec_codec == G729B || j->play_codec == G729B) {
		ixj_WriteDSPCommand(0xE022, j);	/* Move AEC filter buffer back */

		ixj_WriteDSPCommand(0x0700, j);
	}
	if (j->play_mode != -1 && j->rec_mode != -1)
	{
		ixj_WriteDSPCommand(0xB002, j);	/* AEC Stop */
	}
}

static int set_play_codec(IXJ *j, int rate)
{
	int retval = 0;

	j->play_codec = rate;

	switch (rate) {
	case G723_63:
		if (j->ver.low != 0x12 || ixj_convert_loaded) {
			j->play_frame_size = 12;
			j->play_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case G723_53:
		if (j->ver.low != 0x12 || ixj_convert_loaded) {
			j->play_frame_size = 10;
			j->play_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case TS85:
		if (j->dsp.low == 0x20 || j->flags.ts85_loaded) {
			j->play_frame_size = 16;
			j->play_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case TS48:
		if (j->ver.low != 0x12 || ixj_convert_loaded) {
			j->play_frame_size = 9;
			j->play_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case TS41:
		if (j->ver.low != 0x12 || ixj_convert_loaded) {
			j->play_frame_size = 8;
			j->play_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case G728:
		if (j->dsp.low != 0x20) {
			j->play_frame_size = 48;
			j->play_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case G729:
		if (j->dsp.low != 0x20) {
			if (!j->flags.g729_loaded) {
				retval = 1;
				break;
			}
			switch (j->baseframe.low) {
			case 0xA0:
				j->play_frame_size = 10;
				break;
			case 0x50:
				j->play_frame_size = 5;
				break;
			default:
				j->play_frame_size = 15;
				break;
			}
			j->play_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case G729B:
		if (j->dsp.low != 0x20) {
			if (!j->flags.g729_loaded) {
				retval = 1;
				break;
			}
			switch (j->baseframe.low) {
			case 0xA0:
				j->play_frame_size = 12;
				break;
			case 0x50:
				j->play_frame_size = 6;
				break;
			default:
				j->play_frame_size = 18;
				break;
			}
			j->play_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case ULAW:
		switch (j->baseframe.low) {
		case 0xA0:
			j->play_frame_size = 80;
			break;
		case 0x50:
			j->play_frame_size = 40;
			break;
		default:
			j->play_frame_size = 120;
			break;
		}
		j->play_mode = 2;
		break;
	case ALAW:
		switch (j->baseframe.low) {
		case 0xA0:
			j->play_frame_size = 80;
			break;
		case 0x50:
			j->play_frame_size = 40;
			break;
		default:
			j->play_frame_size = 120;
			break;
		}
		j->play_mode = 2;
		break;
	case LINEAR16:
		switch (j->baseframe.low) {
		case 0xA0:
			j->play_frame_size = 160;
			break;
		case 0x50:
			j->play_frame_size = 80;
			break;
		default:
			j->play_frame_size = 240;
			break;
		}
		j->play_mode = 6;
		break;
	case LINEAR8:
		switch (j->baseframe.low) {
		case 0xA0:
			j->play_frame_size = 80;
			break;
		case 0x50:
			j->play_frame_size = 40;
			break;
		default:
			j->play_frame_size = 120;
			break;
		}
		j->play_mode = 4;
		break;
	case WSS:
		switch (j->baseframe.low) {
		case 0xA0:
			j->play_frame_size = 80;
			break;
		case 0x50:
			j->play_frame_size = 40;
			break;
		default:
			j->play_frame_size = 120;
			break;
		}
		j->play_mode = 5;
		break;
	default:
		kfree(j->write_buffer);
		j->play_frame_size = 0;
		j->play_mode = -1;
		j->write_buffer = NULL;
		j->write_buffer_size = 0;
		retval = 1;
		break;
	}
	return retval;
}

static int ixj_play_start(IXJ *j)
{
	unsigned short cmd = 0x0000;

	if (j->write_buffer) {
		ixj_play_stop(j);
	}

	if(ixjdebug & 0x0002)
		printk("IXJ %d Starting Play Codec %d at %ld\n", j->board, j->play_codec, jiffies);

	j->flags.playing = 1;
	ixj_WriteDSPCommand(0x0FE0, j);	/* Put the DSP in full power mode. */

	j->flags.play_first_frame = 1;
	j->drybuffer = 0;

	if (!j->play_mode) {
		switch (j->play_codec) {
		case G723_63:
			cmd = 0x5231;
			break;
		case G723_53:
			cmd = 0x5232;
			break;
		case TS85:
			cmd = 0x5230;	/* TrueSpeech 8.5 */

			break;
		case TS48:
			cmd = 0x5233;	/* TrueSpeech 4.8 */

			break;
		case TS41:
			cmd = 0x5234;	/* TrueSpeech 4.1 */

			break;
		case G728:
			cmd = 0x5235;
			break;
		case G729:
		case G729B:
			cmd = 0x5236;
			break;
		default:
			return 1;
		}
		if (ixj_WriteDSPCommand(cmd, j))
			return -1;
	}
	j->write_buffer = kmalloc(j->play_frame_size * 2, GFP_ATOMIC);
	if (!j->write_buffer) {
		printk("Write buffer allocation for ixj board %d failed!\n", j->board);
		return -ENOMEM;
	}
/*	j->write_buffers_empty = 2; */
	j->write_buffers_empty = 1; 
	j->write_buffer_size = j->play_frame_size * 2;
	j->write_buffer_end = j->write_buffer + j->play_frame_size * 2;
	j->write_buffer_rp = j->write_buffer_wp = j->write_buffer;

	if (ixj_WriteDSPCommand(0x5202, j))		/* Set Poll sync mode */

		return -1;

	switch (j->play_mode) {
	case 0:
		cmd = 0x2C03;
		break;
	case 2:
		if (j->ver.low == 0x12) {
			cmd = 0x2C23;
		} else {
			cmd = 0x2C21;
		}
		break;
	case 4:
		if (j->ver.low == 0x12) {
			cmd = 0x2C43;
		} else {
			cmd = 0x2C41;
		}
		break;
	case 5:
		if (j->ver.low == 0x12) {
			cmd = 0x2C53;
		} else {
			cmd = 0x2C51;
		}
		break;
	case 6:
		if (j->ver.low == 0x12) {
			cmd = 0x2C63;
		} else {
			cmd = 0x2C61;
		}
		break;
	}
	if (ixj_WriteDSPCommand(cmd, j))
		return -1;

	if (ixj_WriteDSPCommand(0x2000, j))		/* Playback C2 */
		return -1;

	if (ixj_WriteDSPCommand(0x2000 + j->play_frame_size, j))	/* Playback C3 */
		return -1;

	if (j->flags.recording) {
		ixj_aec_start(j, j->aec_level);
	}

	return 0;
}

static void ixj_play_stop(IXJ *j)
{
	if (ixjdebug & 0x0002)
		printk("IXJ %d Stopping Play Codec %d at %ld\n", j->board, j->play_codec, jiffies);

	kfree(j->write_buffer);
	j->write_buffer = NULL;
	j->write_buffer_size = 0;
	if (j->play_mode > -1) {
		ixj_WriteDSPCommand(0x5221, j);	/* Stop playback and flush buffers.  8022 reference page 9-40 */

		j->play_mode = -1;
	}
	j->flags.playing = 0;
}

static inline int get_play_level(IXJ *j)
{
	int retval;

	ixj_WriteDSPCommand(0xCF8F, j); /* 8022 Reference page 9-38 */
	return j->ssr.high << 8 | j->ssr.low;
	retval = j->ssr.high << 8 | j->ssr.low;
	retval = (retval * 256) / 240;
	return retval;
}

static unsigned int ixj_poll(struct file *file_p, poll_table * wait)
{
	unsigned int mask = 0;

	IXJ *j = get_ixj(NUM(file_p->f_path.dentry->d_inode));

	poll_wait(file_p, &(j->poll_q), wait);
	if (j->read_buffer_ready > 0)
		mask |= POLLIN | POLLRDNORM;	/* readable */
	if (j->write_buffers_empty > 0)
		mask |= POLLOUT | POLLWRNORM;	/* writable */
	if (j->ex.bytes)
		mask |= POLLPRI;
	return mask;
}

static int ixj_play_tone(IXJ *j, char tone)
{
	if (!j->tone_state) {
		if(ixjdebug & 0x0002) {
			printk("IXJ %d starting tone %d at %ld\n", j->board, tone, jiffies);
		}
		if (j->dsp.low == 0x20) {
			idle(j);
		}
		j->tone_start_jif = jiffies;

		j->tone_state = 1;
	}

	j->tone_index = tone;
	if (ixj_WriteDSPCommand(0x6000 + j->tone_index, j))
		return -1;

	return 0;
}

static int ixj_set_tone_on(unsigned short arg, IXJ *j)
{
	j->tone_on_time = arg;

	if (ixj_WriteDSPCommand(0x6E04, j))		/* Set Tone On Period */

		return -1;

	if (ixj_WriteDSPCommand(arg, j))
		return -1;

	return 0;
}

static int SCI_WaitHighSCI(IXJ *j)
{
	int cnt;

	j->pld_scrr.byte = inb_p(j->XILINXbase);
	if (!j->pld_scrr.bits.sci) {
		for (cnt = 0; cnt < 10; cnt++) {
			udelay(32);
			j->pld_scrr.byte = inb_p(j->XILINXbase);

			if ((j->pld_scrr.bits.sci))
				return 1;
		}
		if (ixjdebug & 0x0001)
			printk(KERN_INFO "SCI Wait High failed %x\n", j->pld_scrr.byte);
		return 0;
	} else
		return 1;
}

static int SCI_WaitLowSCI(IXJ *j)
{
	int cnt;

	j->pld_scrr.byte = inb_p(j->XILINXbase);
	if (j->pld_scrr.bits.sci) {
		for (cnt = 0; cnt < 10; cnt++) {
			udelay(32);
			j->pld_scrr.byte = inb_p(j->XILINXbase);

			if (!(j->pld_scrr.bits.sci))
				return 1;
		}
		if (ixjdebug & 0x0001)
			printk(KERN_INFO "SCI Wait Low failed %x\n", j->pld_scrr.byte);
		return 0;
	} else
		return 1;
}

static int SCI_Control(IXJ *j, int control)
{
	switch (control) {
	case SCI_End:
		j->pld_scrw.bits.c0 = 0;	/* Set PLD Serial control interface */

		j->pld_scrw.bits.c1 = 0;	/* to no selection */

		break;
	case SCI_Enable_DAA:
		j->pld_scrw.bits.c0 = 1;	/* Set PLD Serial control interface */

		j->pld_scrw.bits.c1 = 0;	/* to write to DAA */

		break;
	case SCI_Enable_Mixer:
		j->pld_scrw.bits.c0 = 0;	/* Set PLD Serial control interface */

		j->pld_scrw.bits.c1 = 1;	/* to write to mixer */

		break;
	case SCI_Enable_EEPROM:
		j->pld_scrw.bits.c0 = 1;	/* Set PLD Serial control interface */

		j->pld_scrw.bits.c1 = 1;	/* to write to EEPROM */

		break;
	default:
		return 0;
		break;
	}
	outb_p(j->pld_scrw.byte, j->XILINXbase);

	switch (control) {
	case SCI_End:
		return 1;
		break;
	case SCI_Enable_DAA:
	case SCI_Enable_Mixer:
	case SCI_Enable_EEPROM:
		if (!SCI_WaitHighSCI(j))
			return 0;
		break;
	default:
		return 0;
		break;
	}
	return 1;
}

static int SCI_Prepare(IXJ *j)
{
	if (!SCI_Control(j, SCI_End))
		return 0;

	if (!SCI_WaitLowSCI(j))
		return 0;

	return 1;
}

static int ixj_get_mixer(long val, IXJ *j)
{
	int reg = (val & 0x1F00) >> 8;
        return j->mix.vol[reg];
}

static int ixj_mixer(long val, IXJ *j)
{
	BYTES bytes;

	bytes.high = (val & 0x1F00) >> 8;
	bytes.low = val & 0x00FF;

        /* save mixer value so we can get back later on */
        j->mix.vol[bytes.high] = bytes.low;

	outb_p(bytes.high & 0x1F, j->XILINXbase + 0x03);	/* Load Mixer Address */

	outb_p(bytes.low, j->XILINXbase + 0x02);	/* Load Mixer Data */

	SCI_Control(j, SCI_Enable_Mixer);

	SCI_Control(j, SCI_End);

	return 0;
}

static int daa_load(BYTES * p_bytes, IXJ *j)
{
	outb_p(p_bytes->high, j->XILINXbase + 0x03);
	outb_p(p_bytes->low, j->XILINXbase + 0x02);
	if (!SCI_Control(j, SCI_Enable_DAA))
		return 0;
	else
		return 1;
}

static int ixj_daa_cr4(IXJ *j, char reg)
{
	BYTES bytes;

	switch (j->daa_mode) {
	case SOP_PU_SLEEP:
		bytes.high = 0x14;
		break;
	case SOP_PU_RINGING:
		bytes.high = 0x54;
		break;
	case SOP_PU_CONVERSATION:
		bytes.high = 0x94;
		break;
	case SOP_PU_PULSEDIALING:
		bytes.high = 0xD4;
		break;
	}

	j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.reg = reg;

	switch (j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.bitreg.AGX) {
	case 0:
		j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.bitreg.AGR_Z = 0;
		break;
	case 1:
		j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.bitreg.AGR_Z = 2;
		break;
	case 2:
		j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.bitreg.AGR_Z = 1;
		break;
	case 3:
		j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.bitreg.AGR_Z = 3;
		break;
	}

	bytes.low = j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.reg;

	if (!daa_load(&bytes, j))
		return 0;

	if (!SCI_Prepare(j))
		return 0;

	return 1;
}

static char daa_int_read(IXJ *j)
{
	BYTES bytes;

	if (!SCI_Prepare(j))
		return 0;

	bytes.high = 0x38;
	bytes.low = 0x00;
	outb_p(bytes.high, j->XILINXbase + 0x03);
	outb_p(bytes.low, j->XILINXbase + 0x02);

	if (!SCI_Control(j, SCI_Enable_DAA))
		return 0;

	bytes.high = inb_p(j->XILINXbase + 0x03);
	bytes.low = inb_p(j->XILINXbase + 0x02);
	if (bytes.low != ALISDAA_ID_BYTE) {
		if (ixjdebug & 0x0001)
			printk("Cannot read DAA ID Byte high = %d low = %d\n", bytes.high, bytes.low);
		return 0;
	}
	if (!SCI_Control(j, SCI_Enable_DAA))
		return 0;
	if (!SCI_Control(j, SCI_End))
		return 0;

	bytes.high = inb_p(j->XILINXbase + 0x03);
	bytes.low = inb_p(j->XILINXbase + 0x02);

	j->m_DAAShadowRegs.XOP_REGS.XOP.xr0.reg = bytes.high;

	return 1;
}

static char daa_CR_read(IXJ *j, int cr)
{
	IXJ_WORD wdata;
	BYTES bytes;

	if (!SCI_Prepare(j))
		return 0;

	switch (j->daa_mode) {
	case SOP_PU_SLEEP:
		bytes.high = 0x30 + cr;
		break;
	case SOP_PU_RINGING:
		bytes.high = 0x70 + cr;
		break;
	case SOP_PU_CONVERSATION:
		bytes.high = 0xB0 + cr;
		break;
	case SOP_PU_PULSEDIALING:
		bytes.high = 0xF0 + cr;
		break;
	}

	bytes.low = 0x00;

	outb_p(bytes.high, j->XILINXbase + 0x03);
	outb_p(bytes.low, j->XILINXbase + 0x02);

	if (!SCI_Control(j, SCI_Enable_DAA))
		return 0;

	bytes.high = inb_p(j->XILINXbase + 0x03);
	bytes.low = inb_p(j->XILINXbase + 0x02);
	if (bytes.low != ALISDAA_ID_BYTE) {
		if (ixjdebug & 0x0001)
			printk("Cannot read DAA ID Byte high = %d low = %d\n", bytes.high, bytes.low);
		return 0;
	}
	if (!SCI_Control(j, SCI_Enable_DAA))
		return 0;
	if (!SCI_Control(j, SCI_End))
		return 0;

	wdata.word = inw_p(j->XILINXbase + 0x02);

	switch(cr){
		case 5:
			j->m_DAAShadowRegs.SOP_REGS.SOP.cr5.reg = wdata.bytes.high;
			break;
		case 4:
			j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.reg = wdata.bytes.high;
			break;
		case 3:
			j->m_DAAShadowRegs.SOP_REGS.SOP.cr3.reg = wdata.bytes.high;
			break;
		case 2:
			j->m_DAAShadowRegs.SOP_REGS.SOP.cr2.reg = wdata.bytes.high;
			break;
		case 1:
			j->m_DAAShadowRegs.SOP_REGS.SOP.cr1.reg = wdata.bytes.high;
			break;
		case 0:
			j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg = wdata.bytes.high;
			break;
		default:
			return 0;
	}
	return 1;
}

static int ixj_daa_cid_reset(IXJ *j)
{
	int i;
	BYTES bytes;

	if (ixjdebug & 0x0002)
		printk("DAA Clearing CID ram\n");

	if (!SCI_Prepare(j))
		return 0;

	bytes.high = 0x58;
	bytes.low = 0x00;
	outb_p(bytes.high, j->XILINXbase + 0x03);
	outb_p(bytes.low, j->XILINXbase + 0x02);

	if (!SCI_Control(j, SCI_Enable_DAA))
		return 0;

	if (!SCI_WaitHighSCI(j))
		return 0;

	for (i = 0; i < ALISDAA_CALLERID_SIZE - 1; i += 2) {
		bytes.high = bytes.low = 0x00;
		outb_p(bytes.high, j->XILINXbase + 0x03);

		if (i < ALISDAA_CALLERID_SIZE - 1)
			outb_p(bytes.low, j->XILINXbase + 0x02);

		if (!SCI_Control(j, SCI_Enable_DAA))
			return 0;

		if (!SCI_WaitHighSCI(j))
			return 0;

	}

	if (!SCI_Control(j, SCI_End))
		return 0;

	if (ixjdebug & 0x0002)
		printk("DAA CID ram cleared\n");

	return 1;
}

static int ixj_daa_cid_read(IXJ *j)
{
	int i;
	BYTES bytes;
	char CID[ALISDAA_CALLERID_SIZE];
	bool mContinue;
	char *pIn, *pOut;

	if (!SCI_Prepare(j))
		return 0;

	bytes.high = 0x78;
	bytes.low = 0x00;
	outb_p(bytes.high, j->XILINXbase + 0x03);
	outb_p(bytes.low, j->XILINXbase + 0x02);

	if (!SCI_Control(j, SCI_Enable_DAA))
		return 0;

	if (!SCI_WaitHighSCI(j))
		return 0;

	bytes.high = inb_p(j->XILINXbase + 0x03);
	bytes.low = inb_p(j->XILINXbase + 0x02);
	if (bytes.low != ALISDAA_ID_BYTE) {
		if (ixjdebug & 0x0001)
			printk("DAA Get Version Cannot read DAA ID Byte high = %d low = %d\n", bytes.high, bytes.low);
		return 0;
	}
	for (i = 0; i < ALISDAA_CALLERID_SIZE; i += 2) {
		bytes.high = bytes.low = 0x00;
		outb_p(bytes.high, j->XILINXbase + 0x03);
		outb_p(bytes.low, j->XILINXbase + 0x02);

		if (!SCI_Control(j, SCI_Enable_DAA))
			return 0;

		if (!SCI_WaitHighSCI(j))
			return 0;

		CID[i + 0] = inb_p(j->XILINXbase + 0x03);
		CID[i + 1] = inb_p(j->XILINXbase + 0x02);
	}

	if (!SCI_Control(j, SCI_End))
		return 0;

	pIn = CID;
	pOut = j->m_DAAShadowRegs.CAO_REGS.CAO.CallerID;
	mContinue = true;
	while (mContinue) {
		if ((pIn[1] & 0x03) == 0x01) {
			pOut[0] = pIn[0];
		}
		if ((pIn[2] & 0x0c) == 0x04) {
			pOut[1] = ((pIn[2] & 0x03) << 6) | ((pIn[1] & 0xfc) >> 2);
		}
		if ((pIn[3] & 0x30) == 0x10) {
			pOut[2] = ((pIn[3] & 0x0f) << 4) | ((pIn[2] & 0xf0) >> 4);
		}
		if ((pIn[4] & 0xc0) == 0x40) {
			pOut[3] = ((pIn[4] & 0x3f) << 2) | ((pIn[3] & 0xc0) >> 6);
		} else {
			mContinue = false;
		}
		pIn += 5, pOut += 4;
	}
	memset(&j->cid, 0, sizeof(PHONE_CID));
	pOut = j->m_DAAShadowRegs.CAO_REGS.CAO.CallerID;
	pOut += 4;
	strncpy(j->cid.month, pOut, 2);
	pOut += 2;
	strncpy(j->cid.day, pOut, 2);
	pOut += 2;
	strncpy(j->cid.hour, pOut, 2);
	pOut += 2;
	strncpy(j->cid.min, pOut, 2);
	pOut += 3;
	j->cid.numlen = *pOut;
	pOut += 1;
	strncpy(j->cid.number, pOut, j->cid.numlen);
	pOut += j->cid.numlen + 1;
	j->cid.namelen = *pOut;
	pOut += 1;
	strncpy(j->cid.name, pOut, j->cid.namelen);

	ixj_daa_cid_reset(j);
	return 1;
}

static char daa_get_version(IXJ *j)
{
	BYTES bytes;

	if (!SCI_Prepare(j))
		return 0;

	bytes.high = 0x35;
	bytes.low = 0x00;
	outb_p(bytes.high, j->XILINXbase + 0x03);
	outb_p(bytes.low, j->XILINXbase + 0x02);

	if (!SCI_Control(j, SCI_Enable_DAA))
		return 0;

	bytes.high = inb_p(j->XILINXbase + 0x03);
	bytes.low = inb_p(j->XILINXbase + 0x02);
	if (bytes.low != ALISDAA_ID_BYTE) {
		if (ixjdebug & 0x0001)
			printk("DAA Get Version Cannot read DAA ID Byte high = %d low = %d\n", bytes.high, bytes.low);
		return 0;
	}
	if (!SCI_Control(j, SCI_Enable_DAA))
		return 0;

	if (!SCI_Control(j, SCI_End))
		return 0;

	bytes.high = inb_p(j->XILINXbase + 0x03);
	bytes.low = inb_p(j->XILINXbase + 0x02);
	if (ixjdebug & 0x0002)
		printk("DAA CR5 Byte high = 0x%x low = 0x%x\n", bytes.high, bytes.low);
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr5.reg = bytes.high;
	return bytes.high;
}

static int daa_set_mode(IXJ *j, int mode)
{
	/* NOTE:
	      The DAA *MUST* be in the conversation mode if the
	      PSTN line is to be seized (PSTN line off-hook).
	      Taking the PSTN line off-hook while the DAA is in
	      a mode other than conversation mode will cause a
	      hardware failure of the ALIS-A part.

	   NOTE:
	      The DAA can only go to SLEEP, RINGING or PULSEDIALING modes
	      if the PSTN line is on-hook.  Failure to have the PSTN line
	      in the on-hook state WILL CAUSE A HARDWARE FAILURE OF THE
	      ALIS-A part.
	*/

	BYTES bytes;

	j->flags.pstn_rmr = 0;

	if (!SCI_Prepare(j))
		return 0;

	switch (mode) {
	case SOP_PU_RESET:
		j->pld_scrw.bits.daafsyncen = 0;	/* Turn off DAA Frame Sync */

		outb_p(j->pld_scrw.byte, j->XILINXbase);
		j->pld_slicw.bits.rly2 = 0;
		outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		bytes.high = 0x10;
		bytes.low = j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg;
		daa_load(&bytes, j);
		if (!SCI_Prepare(j))
			return 0;

		j->daa_mode = SOP_PU_SLEEP;
		break;
	case SOP_PU_SLEEP:
		if(j->daa_mode == SOP_PU_SLEEP)
		{
			break;
		}
		if (ixjdebug & 0x0008)
			printk(KERN_INFO "phone DAA: SOP_PU_SLEEP at %ld\n", jiffies);
/*		if(j->daa_mode == SOP_PU_CONVERSATION) */
		{
			j->pld_scrw.bits.daafsyncen = 0;	/* Turn off DAA Frame Sync */

			outb_p(j->pld_scrw.byte, j->XILINXbase);
			j->pld_slicw.bits.rly2 = 0;
			outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
			bytes.high = 0x10;
			bytes.low = j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg;
			daa_load(&bytes, j);
			if (!SCI_Prepare(j))
				return 0;
		}
		j->pld_scrw.bits.daafsyncen = 0;	/* Turn off DAA Frame Sync */

		outb_p(j->pld_scrw.byte, j->XILINXbase);
		j->pld_slicw.bits.rly2 = 0;
		outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		bytes.high = 0x10;
		bytes.low = j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg;
		daa_load(&bytes, j);
		if (!SCI_Prepare(j))
			return 0;

		j->daa_mode = SOP_PU_SLEEP;
		j->flags.pstn_ringing = 0;
		j->ex.bits.pstn_ring = 0;
		j->pstn_sleeptil = jiffies + (hertz / 4);
		wake_up_interruptible(&j->read_q);      /* Wake any blocked readers */
		wake_up_interruptible(&j->write_q);     /* Wake any blocked writers */
		wake_up_interruptible(&j->poll_q);      /* Wake any blocked selects */
 		break;
	case SOP_PU_RINGING:
		if (ixjdebug & 0x0008)
			printk(KERN_INFO "phone DAA: SOP_PU_RINGING at %ld\n", jiffies);
		j->pld_scrw.bits.daafsyncen = 0;	/* Turn off DAA Frame Sync */

		outb_p(j->pld_scrw.byte, j->XILINXbase);
		j->pld_slicw.bits.rly2 = 0;
		outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		bytes.high = 0x50;
		bytes.low = j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg;
		daa_load(&bytes, j);
		if (!SCI_Prepare(j))
			return 0;
		j->daa_mode = SOP_PU_RINGING;
		break;
	case SOP_PU_CONVERSATION:
		if (ixjdebug & 0x0008)
			printk(KERN_INFO "phone DAA: SOP_PU_CONVERSATION at %ld\n", jiffies);
		bytes.high = 0x90;
		bytes.low = j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg;
		daa_load(&bytes, j);
		if (!SCI_Prepare(j))
			return 0;
		j->pld_slicw.bits.rly2 = 1;
		outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		j->pld_scrw.bits.daafsyncen = 1;	/* Turn on DAA Frame Sync */

		outb_p(j->pld_scrw.byte, j->XILINXbase);
		j->daa_mode = SOP_PU_CONVERSATION;
		j->flags.pstn_ringing = 0;
		j->ex.bits.pstn_ring = 0;
		j->pstn_sleeptil = jiffies;
		j->pstn_ring_start = j->pstn_ring_stop = j->pstn_ring_int = 0;
		break;
	case SOP_PU_PULSEDIALING:
		if (ixjdebug & 0x0008)
			printk(KERN_INFO "phone DAA: SOP_PU_PULSEDIALING at %ld\n", jiffies);
		j->pld_scrw.bits.daafsyncen = 0;	/* Turn off DAA Frame Sync */

		outb_p(j->pld_scrw.byte, j->XILINXbase);
		j->pld_slicw.bits.rly2 = 0;
		outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		bytes.high = 0xD0;
		bytes.low = j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg;
		daa_load(&bytes, j);
		if (!SCI_Prepare(j))
			return 0;
		j->daa_mode = SOP_PU_PULSEDIALING;
		break;
	default:
		break;
	}
	return 1;
}

static int ixj_daa_write(IXJ *j)
{
	BYTES bytes;

	j->flags.pstncheck = 1;

	daa_set_mode(j, SOP_PU_SLEEP);

	if (!SCI_Prepare(j))
		return 0;

	outb_p(j->pld_scrw.byte, j->XILINXbase);

	bytes.high = 0x14;
	bytes.low = j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.reg;
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.SOP_REGS.SOP.cr3.reg;
	bytes.low = j->m_DAAShadowRegs.SOP_REGS.SOP.cr2.reg;
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.SOP_REGS.SOP.cr1.reg;
	bytes.low = j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg;
	if (!daa_load(&bytes, j))
		return 0;

	if (!SCI_Prepare(j))
		return 0;

	bytes.high = 0x1F;
	bytes.low = j->m_DAAShadowRegs.XOP_REGS.XOP.xr7.reg;
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.XOP_xr6_W.reg;
	bytes.low = j->m_DAAShadowRegs.XOP_REGS.XOP.xr5.reg;
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.XOP_REGS.XOP.xr4.reg;
	bytes.low = j->m_DAAShadowRegs.XOP_REGS.XOP.xr3.reg;
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.XOP_REGS.XOP.xr2.reg;
	bytes.low = j->m_DAAShadowRegs.XOP_REGS.XOP.xr1.reg;
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.XOP_xr0_W.reg;
	bytes.low = 0x00;
	if (!daa_load(&bytes, j))
		return 0;

	if (!SCI_Prepare(j))
		return 0;

	bytes.high = 0x00;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[7];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[5];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[3];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[1];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, j))
		return 0;

	if (!SCI_Control(j, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(j))
		return 0;

	bytes.high = 0x01;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[7];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[5];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[3];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[1];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, j))
		return 0;

	if (!SCI_Control(j, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(j))
		return 0;

	bytes.high = 0x02;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[7];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[5];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[3];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[1];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, j))
		return 0;

	if (!SCI_Control(j, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(j))
		return 0;

	bytes.high = 0x03;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[7];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[5];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[3];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[1];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, j))
		return 0;

	if (!SCI_Control(j, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(j))
		return 0;

	bytes.high = 0x04;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[7];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[5];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[3];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[1];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, j))
		return 0;

	if (!SCI_Control(j, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(j))
		return 0;

	bytes.high = 0x05;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[7];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[5];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[3];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[1];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, j))
		return 0;

	if (!SCI_Control(j, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(j))
		return 0;

	bytes.high = 0x06;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[7];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[5];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[3];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[1];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, j))
		return 0;

	if (!SCI_Control(j, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(j))
		return 0;

	bytes.high = 0x07;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[7];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[5];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[3];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[1];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, j))
		return 0;

	if (!SCI_Control(j, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(j))
		return 0;

	bytes.high = 0x08;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[7];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[5];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[3];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[1];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, j))
		return 0;

	if (!SCI_Control(j, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(j))
		return 0;

	bytes.high = 0x09;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[3];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[1];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, j))
		return 0;

	if (!SCI_Control(j, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(j))
		return 0;

	bytes.high = 0x0A;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[3];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[1];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, j))
		return 0;

	if (!SCI_Control(j, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(j))
		return 0;

	bytes.high = 0x0B;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[3];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[1];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, j))
		return 0;

	if (!SCI_Control(j, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(j))
		return 0;

	bytes.high = 0x0C;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[3];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[1];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, j))
		return 0;

	if (!SCI_Control(j, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(j))
		return 0;

	bytes.high = 0x0D;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[3];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[1];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, j))
		return 0;

	if (!SCI_Control(j, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(j))
		return 0;

	bytes.high = 0x0E;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[7];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[5];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[3];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[1];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, j))
		return 0;

	if (!SCI_Control(j, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(j))
		return 0;

	bytes.high = 0x0F;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[7];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[5];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[3];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[1];
	if (!daa_load(&bytes, j))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, j))
		return 0;

	udelay(32);
	j->pld_scrr.byte = inb_p(j->XILINXbase);
	if (!SCI_Control(j, SCI_End))
		return 0;

	outb_p(j->pld_scrw.byte, j->XILINXbase);

	if (ixjdebug & 0x0002)
		printk("DAA Coefficients Loaded\n");

	j->flags.pstncheck = 0;
	return 1;
}

static int ixj_set_tone_off(unsigned short arg, IXJ *j)
{
	j->tone_off_time = arg;
	if (ixj_WriteDSPCommand(0x6E05, j))		/* Set Tone Off Period */

		return -1;
	if (ixj_WriteDSPCommand(arg, j))
		return -1;
	return 0;
}

static int ixj_get_tone_on(IXJ *j)
{
	if (ixj_WriteDSPCommand(0x6E06, j))		/* Get Tone On Period */

		return -1;
	return 0;
}

static int ixj_get_tone_off(IXJ *j)
{
	if (ixj_WriteDSPCommand(0x6E07, j))		/* Get Tone Off Period */

		return -1;
	return 0;
}

static void ixj_busytone(IXJ *j)
{
	j->flags.ringback = 0;
	j->flags.dialtone = 0;
	j->flags.busytone = 1;
	ixj_set_tone_on(0x07D0, j);
	ixj_set_tone_off(0x07D0, j);
	ixj_play_tone(j, 27);
}

static void ixj_dialtone(IXJ *j)
{
	j->flags.ringback = 0;
	j->flags.dialtone = 1;
	j->flags.busytone = 0;
	if (j->dsp.low == 0x20) {
		return;
	} else {
		ixj_set_tone_on(0xFFFF, j);
		ixj_set_tone_off(0x0000, j);
		ixj_play_tone(j, 25);
	}
}

static void ixj_cpt_stop(IXJ *j)
{
	if(j->tone_state || j->tone_cadence_state)
	{
		j->flags.dialtone = 0;
		j->flags.busytone = 0;
		j->flags.ringback = 0;
		ixj_set_tone_on(0x0001, j);
		ixj_set_tone_off(0x0000, j);
		ixj_play_tone(j, 0);
		j->tone_state = j->tone_cadence_state = 0;
		if (j->cadence_t) {
			kfree(j->cadence_t->ce);
			kfree(j->cadence_t);
			j->cadence_t = NULL;
		}
	}
	if (j->play_mode == -1 && j->rec_mode == -1)
		idle(j);
	if (j->play_mode != -1 && j->dsp.low == 0x20)
		ixj_play_start(j);
	if (j->rec_mode != -1 && j->dsp.low == 0x20)
		ixj_record_start(j);
}

static void ixj_ringback(IXJ *j)
{
	j->flags.busytone = 0;
	j->flags.dialtone = 0;
	j->flags.ringback = 1;
	ixj_set_tone_on(0x0FA0, j);
	ixj_set_tone_off(0x2EE0, j);
	ixj_play_tone(j, 26);
}

static void ixj_testram(IXJ *j)
{
	ixj_WriteDSPCommand(0x3001, j);	/* Test External SRAM */
}

static int ixj_build_cadence(IXJ *j, IXJ_CADENCE __user * cp)
{
	ixj_cadence *lcp;
	IXJ_CADENCE_ELEMENT __user *cep;
	IXJ_CADENCE_ELEMENT *lcep;
	IXJ_TONE ti;
	int err;

	lcp = kmalloc(sizeof(ixj_cadence), GFP_KERNEL);
	if (lcp == NULL)
		return -ENOMEM;

	err = -EFAULT;
	if (copy_from_user(&lcp->elements_used,
			   &cp->elements_used, sizeof(int)))
		goto out;
	if (copy_from_user(&lcp->termination,
			   &cp->termination, sizeof(IXJ_CADENCE_TERM)))
		goto out;
	if (get_user(cep, &cp->ce))
		goto out;

	err = -EINVAL;
	if ((unsigned)lcp->elements_used >= ~0U/sizeof(IXJ_CADENCE_ELEMENT))
		goto out;

	err = -ENOMEM;
	lcep = kmalloc(sizeof(IXJ_CADENCE_ELEMENT) * lcp->elements_used, GFP_KERNEL);
	if (!lcep)
		goto out;

	err = -EFAULT;
	if (copy_from_user(lcep, cep, sizeof(IXJ_CADENCE_ELEMENT) * lcp->elements_used))
		goto out1;

	if (j->cadence_t) {
		kfree(j->cadence_t->ce);
		kfree(j->cadence_t);
	}
	lcp->ce = (void *) lcep;
	j->cadence_t = lcp;
	j->tone_cadence_state = 0;
	ixj_set_tone_on(lcp->ce[0].tone_on_time, j);
	ixj_set_tone_off(lcp->ce[0].tone_off_time, j);
	if (j->cadence_t->ce[j->tone_cadence_state].freq0) {
		ti.tone_index = j->cadence_t->ce[j->tone_cadence_state].index;
		ti.freq0 = j->cadence_t->ce[j->tone_cadence_state].freq0;
		ti.gain0 = j->cadence_t->ce[j->tone_cadence_state].gain0;
		ti.freq1 = j->cadence_t->ce[j->tone_cadence_state].freq1;
		ti.gain1 = j->cadence_t->ce[j->tone_cadence_state].gain1;
		ixj_init_tone(j, &ti);
	}
	ixj_play_tone(j, lcp->ce[0].index);
	return 1;
out1:
	kfree(lcep);
out:
	kfree(lcp);
	return err;
}

static int ixj_build_filter_cadence(IXJ *j, IXJ_FILTER_CADENCE __user * cp)
{
	IXJ_FILTER_CADENCE *lcp;
	lcp = kmalloc(sizeof(IXJ_FILTER_CADENCE), GFP_KERNEL);
	if (lcp == NULL) {
		if(ixjdebug & 0x0001) {
			printk(KERN_INFO "Could not allocate memory for cadence\n");
		}
		return -ENOMEM;
        }
	if (copy_from_user(lcp, cp, sizeof(IXJ_FILTER_CADENCE))) {
		if(ixjdebug & 0x0001) {
			printk(KERN_INFO "Could not copy cadence to kernel\n");
		}
		kfree(lcp);
		return -EFAULT;
	}
	if (lcp->filter > 5) {
		if(ixjdebug & 0x0001) {
			printk(KERN_INFO "Cadence out of range\n");
		}
		kfree(lcp);
		return -1;
	}
	j->cadence_f[lcp->filter].state = 0;
	j->cadence_f[lcp->filter].enable = lcp->enable;
	j->filter_en[lcp->filter] = j->cadence_f[lcp->filter].en_filter = lcp->en_filter;
	j->cadence_f[lcp->filter].on1 = lcp->on1;
	j->cadence_f[lcp->filter].on1min = 0;
	j->cadence_f[lcp->filter].on1max = 0;
	j->cadence_f[lcp->filter].off1 = lcp->off1;
	j->cadence_f[lcp->filter].off1min = 0;
	j->cadence_f[lcp->filter].off1max = 0;
	j->cadence_f[lcp->filter].on2 = lcp->on2;
	j->cadence_f[lcp->filter].on2min = 0;
	j->cadence_f[lcp->filter].on2max = 0;
	j->cadence_f[lcp->filter].off2 = lcp->off2;
	j->cadence_f[lcp->filter].off2min = 0;
	j->cadence_f[lcp->filter].off2max = 0;
	j->cadence_f[lcp->filter].on3 = lcp->on3;
	j->cadence_f[lcp->filter].on3min = 0;
	j->cadence_f[lcp->filter].on3max = 0;
	j->cadence_f[lcp->filter].off3 = lcp->off3;
	j->cadence_f[lcp->filter].off3min = 0;
	j->cadence_f[lcp->filter].off3max = 0;
	if(ixjdebug & 0x0002) {
		printk(KERN_INFO "Cadence %d loaded\n", lcp->filter);
	}
	kfree(lcp);
	return 0;
}

static void add_caps(IXJ *j)
{
	j->caps = 0;
	j->caplist[j->caps].cap = PHONE_VENDOR_QUICKNET;
	strcpy(j->caplist[j->caps].desc, "Quicknet Technologies, Inc. (www.quicknet.net)");
	j->caplist[j->caps].captype = vendor;
	j->caplist[j->caps].handle = j->caps++;
	j->caplist[j->caps].captype = device;
	switch (j->cardtype) {
	case QTI_PHONEJACK:
		strcpy(j->caplist[j->caps].desc, "Quicknet Internet PhoneJACK");
		break;
	case QTI_LINEJACK:
		strcpy(j->caplist[j->caps].desc, "Quicknet Internet LineJACK");
		break;
	case QTI_PHONEJACK_LITE:
		strcpy(j->caplist[j->caps].desc, "Quicknet Internet PhoneJACK Lite");
		break;
	case QTI_PHONEJACK_PCI:
		strcpy(j->caplist[j->caps].desc, "Quicknet Internet PhoneJACK PCI");
		break;
	case QTI_PHONECARD:
		strcpy(j->caplist[j->caps].desc, "Quicknet Internet PhoneCARD");
		break;
	}
	j->caplist[j->caps].cap = j->cardtype;
	j->caplist[j->caps].handle = j->caps++;
	strcpy(j->caplist[j->caps].desc, "POTS");
	j->caplist[j->caps].captype = port;
	j->caplist[j->caps].cap = pots;
	j->caplist[j->caps].handle = j->caps++;

 	/* add devices that can do speaker/mic */
	switch (j->cardtype) {
	case QTI_PHONEJACK:
	case QTI_LINEJACK:
	case QTI_PHONEJACK_PCI:
	case QTI_PHONECARD:
		strcpy(j->caplist[j->caps].desc, "SPEAKER");
		j->caplist[j->caps].captype = port;
		j->caplist[j->caps].cap = speaker;
		j->caplist[j->caps].handle = j->caps++;
        default:
     		break;
	}

 	/* add devices that can do handset */
	switch (j->cardtype) {
	case QTI_PHONEJACK:
		strcpy(j->caplist[j->caps].desc, "HANDSET");
		j->caplist[j->caps].captype = port;
		j->caplist[j->caps].cap = handset;
		j->caplist[j->caps].handle = j->caps++;
		break;
        default:
     		break;
	}

 	/* add devices that can do PSTN */
	switch (j->cardtype) {
	case QTI_LINEJACK:
		strcpy(j->caplist[j->caps].desc, "PSTN");
		j->caplist[j->caps].captype = port;
		j->caplist[j->caps].cap = pstn;
		j->caplist[j->caps].handle = j->caps++;
		break;
        default:
     		break;
	}

	/* add codecs - all cards can do uLaw, linear 8/16, and Windows sound system */
	strcpy(j->caplist[j->caps].desc, "ULAW");
	j->caplist[j->caps].captype = codec;
	j->caplist[j->caps].cap = ULAW;
	j->caplist[j->caps].handle = j->caps++;

	strcpy(j->caplist[j->caps].desc, "LINEAR 16 bit");
	j->caplist[j->caps].captype = codec;
	j->caplist[j->caps].cap = LINEAR16;
	j->caplist[j->caps].handle = j->caps++;

	strcpy(j->caplist[j->caps].desc, "LINEAR 8 bit");
	j->caplist[j->caps].captype = codec;
	j->caplist[j->caps].cap = LINEAR8;
	j->caplist[j->caps].handle = j->caps++;

	strcpy(j->caplist[j->caps].desc, "Windows Sound System");
	j->caplist[j->caps].captype = codec;
	j->caplist[j->caps].cap = WSS;
	j->caplist[j->caps].handle = j->caps++;

	/* software ALAW codec, made from ULAW */
	strcpy(j->caplist[j->caps].desc, "ALAW");
	j->caplist[j->caps].captype = codec;
	j->caplist[j->caps].cap = ALAW;
	j->caplist[j->caps].handle = j->caps++;

	/* version 12 of the 8020 does the following codecs in a broken way */
	if (j->dsp.low != 0x20 || j->ver.low != 0x12) {
		strcpy(j->caplist[j->caps].desc, "G.723.1 6.3kbps");
		j->caplist[j->caps].captype = codec;
		j->caplist[j->caps].cap = G723_63;
		j->caplist[j->caps].handle = j->caps++;

		strcpy(j->caplist[j->caps].desc, "G.723.1 5.3kbps");
		j->caplist[j->caps].captype = codec;
		j->caplist[j->caps].cap = G723_53;
		j->caplist[j->caps].handle = j->caps++;

		strcpy(j->caplist[j->caps].desc, "TrueSpeech 4.8kbps");
		j->caplist[j->caps].captype = codec;
		j->caplist[j->caps].cap = TS48;
		j->caplist[j->caps].handle = j->caps++;

		strcpy(j->caplist[j->caps].desc, "TrueSpeech 4.1kbps");
		j->caplist[j->caps].captype = codec;
		j->caplist[j->caps].cap = TS41;
		j->caplist[j->caps].handle = j->caps++;
	}

	/* 8020 chips can do TS8.5 native, and 8021/8022 can load it */
	if (j->dsp.low == 0x20 || j->flags.ts85_loaded) {
		strcpy(j->caplist[j->caps].desc, "TrueSpeech 8.5kbps");
		j->caplist[j->caps].captype = codec;
		j->caplist[j->caps].cap = TS85;
		j->caplist[j->caps].handle = j->caps++;
	}

	/* 8021 chips can do G728 */
	if (j->dsp.low == 0x21) {
		strcpy(j->caplist[j->caps].desc, "G.728 16kbps");
		j->caplist[j->caps].captype = codec;
		j->caplist[j->caps].cap = G728;
		j->caplist[j->caps].handle = j->caps++;
	}

	/* 8021/8022 chips can do G729 if loaded */
	if (j->dsp.low != 0x20 && j->flags.g729_loaded) {
		strcpy(j->caplist[j->caps].desc, "G.729A 8kbps");
		j->caplist[j->caps].captype = codec;
		j->caplist[j->caps].cap = G729;
		j->caplist[j->caps].handle = j->caps++;
	}
	if (j->dsp.low != 0x20 && j->flags.g729_loaded) {
		strcpy(j->caplist[j->caps].desc, "G.729B 8kbps");
		j->caplist[j->caps].captype = codec;
		j->caplist[j->caps].cap = G729B;
		j->caplist[j->caps].handle = j->caps++;
	}
}

static int capabilities_check(IXJ *j, struct phone_capability *pcreq)
{
	int cnt;
	int retval = 0;
	for (cnt = 0; cnt < j->caps; cnt++) {
		if (pcreq->captype == j->caplist[cnt].captype
		    && pcreq->cap == j->caplist[cnt].cap) {
			retval = 1;
			break;
		}
	}
	return retval;
}

static int ixj_ioctl(struct inode *inode, struct file *file_p, unsigned int cmd, unsigned long arg)
{
	IXJ_TONE ti;
	IXJ_FILTER jf;
	IXJ_FILTER_RAW jfr;
	void __user *argp = (void __user *)arg;

	unsigned int raise, mant;
	unsigned int minor = iminor(inode);
	int board = NUM(inode);

	IXJ *j = get_ixj(NUM(inode));

	int retval = 0;

	/*
	 *    Set up locks to ensure that only one process is talking to the DSP at a time.
	 *    This is necessary to keep the DSP from locking up.
	 */
	while(test_and_set_bit(board, (void *)&j->busyflags) != 0)
		schedule_timeout_interruptible(1);
	if (ixjdebug & 0x0040)
		printk("phone%d ioctl, cmd: 0x%x, arg: 0x%lx\n", minor, cmd, arg);
	if (minor >= IXJMAX) {
		clear_bit(board, &j->busyflags);
		return -ENODEV;
	}
	/*
	 *    Check ioctls only root can use.
	 */
	if (!capable(CAP_SYS_ADMIN)) {
		switch (cmd) {
		case IXJCTL_TESTRAM:
		case IXJCTL_HZ:
			retval = -EPERM;
		}
	}
	switch (cmd) {
	case IXJCTL_TESTRAM:
		ixj_testram(j);
		retval = (j->ssr.high << 8) + j->ssr.low;
		break;
	case IXJCTL_CARDTYPE:
		retval = j->cardtype;
		break;
	case IXJCTL_SERIAL:
		retval = j->serial;
		break;
	case IXJCTL_VERSION:
		{
			char arg_str[100];
			snprintf(arg_str, sizeof(arg_str),
				"\nDriver version %i.%i.%i", IXJ_VER_MAJOR,
				IXJ_VER_MINOR, IXJ_BLD_VER);
			if (copy_to_user(argp, arg_str, strlen(arg_str)))
				retval = -EFAULT;
		}
		break;
	case PHONE_RING_CADENCE:
		j->ring_cadence = arg;
		break;
	case IXJCTL_CIDCW:
		if(arg) {
			if (copy_from_user(&j->cid_send, argp, sizeof(PHONE_CID))) {
				retval = -EFAULT;
				break;
			}
		} else {
			memset(&j->cid_send, 0, sizeof(PHONE_CID));
		}
		ixj_write_cidcw(j);
		break;
        /* Binary compatbility */
        case OLD_PHONE_RING_START:
                arg = 0;
                /* Fall through */
 	case PHONE_RING_START:
		if(arg) {
			if (copy_from_user(&j->cid_send, argp, sizeof(PHONE_CID))) {
				retval = -EFAULT;
				break;
			}
			ixj_write_cid(j);
		} else {
			memset(&j->cid_send, 0, sizeof(PHONE_CID));
		}
		ixj_ring_start(j);
		break;
	case PHONE_RING_STOP:
		j->flags.cringing = 0;
		if(j->cadence_f[5].enable) {
			j->cadence_f[5].state = 0;
		}
		ixj_ring_off(j);
		break;
	case PHONE_RING:
		retval = ixj_ring(j);
		break;
	case PHONE_EXCEPTION:
		retval = j->ex.bytes;
		if(j->ex.bits.flash) {
			j->flash_end = 0;
			j->ex.bits.flash = 0;
		}
		j->ex.bits.pstn_ring = 0;
		j->ex.bits.caller_id = 0;
		j->ex.bits.pstn_wink = 0;
		j->ex.bits.f0 = 0;
		j->ex.bits.f1 = 0;
		j->ex.bits.f2 = 0;
		j->ex.bits.f3 = 0;
		j->ex.bits.fc0 = 0;
		j->ex.bits.fc1 = 0;
		j->ex.bits.fc2 = 0;
		j->ex.bits.fc3 = 0;
		j->ex.bits.reserved = 0;
		break;
	case PHONE_HOOKSTATE:
		j->ex.bits.hookstate = 0;
		retval = j->hookstate;  //j->r_hook;
		break;
	case IXJCTL_SET_LED:
		LED_SetState(arg, j);
		break;
	case PHONE_FRAME:
		retval = set_base_frame(j, arg);
		break;
	case PHONE_REC_CODEC:
		retval = set_rec_codec(j, arg);
		break;
	case PHONE_VAD:
		ixj_vad(j, arg);
		break;
	case PHONE_REC_START:
		ixj_record_start(j);
		break;
	case PHONE_REC_STOP:
		ixj_record_stop(j);
		break;
	case PHONE_REC_DEPTH:
		set_rec_depth(j, arg);
		break;
	case PHONE_REC_VOLUME:
		if(arg == -1) {
			retval = get_rec_volume(j);
		}
		else {
			set_rec_volume(j, arg);
			retval = arg;
		}
		break;
	case PHONE_REC_VOLUME_LINEAR:
		if(arg == -1) {
			retval = get_rec_volume_linear(j);
		}
		else {
			set_rec_volume_linear(j, arg);
			retval = arg;
		}
		break;
	case IXJCTL_DTMF_PRESCALE:
		if(arg == -1) {
			retval = get_dtmf_prescale(j);
		}
		else {
			set_dtmf_prescale(j, arg);
			retval = arg;
		}
		break;
	case PHONE_REC_LEVEL:
		retval = get_rec_level(j);
		break;
	case IXJCTL_SC_RXG:
		retval = ixj_siadc(j, arg);
		break;
	case IXJCTL_SC_TXG:
		retval = ixj_sidac(j, arg);
		break;
	case IXJCTL_AEC_START:
		ixj_aec_start(j, arg);
		break;
	case IXJCTL_AEC_STOP:
		aec_stop(j);
		break;
	case IXJCTL_AEC_GET_LEVEL:
		retval = j->aec_level;
		break;
	case PHONE_PLAY_CODEC:
		retval = set_play_codec(j, arg);
		break;
	case PHONE_PLAY_START:
		retval = ixj_play_start(j);
		break;
	case PHONE_PLAY_STOP:
		ixj_play_stop(j);
		break;
	case PHONE_PLAY_DEPTH:
		set_play_depth(j, arg);
		break;
	case PHONE_PLAY_VOLUME:
		if(arg == -1) {
			retval = get_play_volume(j);
		}
		else {
			set_play_volume(j, arg);
			retval = arg;
		}
		break;
	case PHONE_PLAY_VOLUME_LINEAR:
		if(arg == -1) {
			retval = get_play_volume_linear(j);
		}
		else {
			set_play_volume_linear(j, arg);
			retval = arg;
		}
		break;
	case PHONE_PLAY_LEVEL:
		retval = get_play_level(j);
		break;
	case IXJCTL_DSP_TYPE:
		retval = (j->dsp.high << 8) + j->dsp.low;
		break;
	case IXJCTL_DSP_VERSION:
		retval = (j->ver.high << 8) + j->ver.low;
		break;
	case IXJCTL_HZ:
		hertz = arg;
		break;
	case IXJCTL_RATE:
		if (arg > hertz)
			retval = -1;
		else
			samplerate = arg;
		break;
	case IXJCTL_DRYBUFFER_READ:
		put_user(j->drybuffer, (unsigned long __user *) argp);
		break;
	case IXJCTL_DRYBUFFER_CLEAR:
		j->drybuffer = 0;
		break;
	case IXJCTL_FRAMES_READ:
		put_user(j->framesread, (unsigned long __user *) argp);
		break;
	case IXJCTL_FRAMES_WRITTEN:
		put_user(j->frameswritten, (unsigned long __user *) argp);
		break;
	case IXJCTL_READ_WAIT:
		put_user(j->read_wait, (unsigned long __user *) argp);
		break;
	case IXJCTL_WRITE_WAIT:
		put_user(j->write_wait, (unsigned long __user *) argp);
		break;
	case PHONE_MAXRINGS:
		j->maxrings = arg;
		break;
	case PHONE_SET_TONE_ON_TIME:
		ixj_set_tone_on(arg, j);
		break;
	case PHONE_SET_TONE_OFF_TIME:
		ixj_set_tone_off(arg, j);
		break;
	case PHONE_GET_TONE_ON_TIME:
		if (ixj_get_tone_on(j)) {
			retval = -1;
		} else {
			retval = (j->ssr.high << 8) + j->ssr.low;
		}
		break;
	case PHONE_GET_TONE_OFF_TIME:
		if (ixj_get_tone_off(j)) {
			retval = -1;
		} else {
			retval = (j->ssr.high << 8) + j->ssr.low;
		}
		break;
	case PHONE_PLAY_TONE:
		if (!j->tone_state)
			retval = ixj_play_tone(j, arg);
		else
			retval = -1;
		break;
	case PHONE_GET_TONE_STATE:
		retval = j->tone_state;
		break;
	case PHONE_DTMF_READY:
		retval = j->ex.bits.dtmf_ready;
		break;
	case PHONE_GET_DTMF:
		if (ixj_hookstate(j)) {
			if (j->dtmf_rp != j->dtmf_wp) {
				retval = j->dtmfbuffer[j->dtmf_rp];
				j->dtmf_rp++;
				if (j->dtmf_rp == 79)
					j->dtmf_rp = 0;
				if (j->dtmf_rp == j->dtmf_wp) {
					j->ex.bits.dtmf_ready = j->dtmf_rp = j->dtmf_wp = 0;
				}
			}
		}
		break;
	case PHONE_GET_DTMF_ASCII:
		if (ixj_hookstate(j)) {
			if (j->dtmf_rp != j->dtmf_wp) {
				switch (j->dtmfbuffer[j->dtmf_rp]) {
				case 10:
					retval = 42;	/* '*'; */

					break;
				case 11:
					retval = 48;	/*'0'; */

					break;
				case 12:
					retval = 35;	/*'#'; */

					break;
				case 28:
					retval = 65;	/*'A'; */

					break;
				case 29:
					retval = 66;	/*'B'; */

					break;
				case 30:
					retval = 67;	/*'C'; */

					break;
				case 31:
					retval = 68;	/*'D'; */

					break;
				default:
					retval = 48 + j->dtmfbuffer[j->dtmf_rp];
					break;
				}
				j->dtmf_rp++;
				if (j->dtmf_rp == 79)
					j->dtmf_rp = 0;
				if(j->dtmf_rp == j->dtmf_wp)
				{
					j->ex.bits.dtmf_ready = j->dtmf_rp = j->dtmf_wp = 0;
				}
			}
		}
		break;
	case PHONE_DTMF_OOB:
		j->flags.dtmf_oob = arg;
		break;
	case PHONE_DIALTONE:
		ixj_dialtone(j);
		break;
	case PHONE_BUSY:
		ixj_busytone(j);
		break;
	case PHONE_RINGBACK:
		ixj_ringback(j);
		break;
	case PHONE_WINK:
		if(j->cardtype == QTI_PHONEJACK) 
			retval = -1;
		else 
			retval = ixj_wink(j);
		break;
	case PHONE_CPT_STOP:
		ixj_cpt_stop(j);
		break;
        case PHONE_QUERY_CODEC:
        {
                struct phone_codec_data pd;
                int val;
                int proto_size[] = {
                        -1,
                        12, 10, 16, 9, 8, 48, 5,
                        40, 40, 80, 40, 40, 6
                };
                if(copy_from_user(&pd, argp, sizeof(pd))) {
                        retval = -EFAULT;
			break;
		}
                if(pd.type<1 || pd.type>13) {
                        retval = -EPROTONOSUPPORT;
			break;
		}
                if(pd.type<G729)
                        val=proto_size[pd.type];
                else switch(j->baseframe.low)
                {
                        case 0xA0:val=2*proto_size[pd.type];break;
                        case 0x50:val=proto_size[pd.type];break;
                        default:val=proto_size[pd.type]*3;break;
                }
                pd.buf_min=pd.buf_max=pd.buf_opt=val;
                if(copy_to_user(argp, &pd, sizeof(pd)))
                        retval = -EFAULT;
        	break;
        }
	case IXJCTL_DSP_IDLE:
		idle(j);
		break;
	case IXJCTL_MIXER:
                if ((arg & 0xff) == 0xff)
			retval = ixj_get_mixer(arg, j);
                else
			ixj_mixer(arg, j);
		break;
	case IXJCTL_DAA_COEFF_SET:
		switch (arg) {
		case DAA_US:
			DAA_Coeff_US(j);
			retval = ixj_daa_write(j);
			break;
		case DAA_UK:
			DAA_Coeff_UK(j);
			retval = ixj_daa_write(j);
			break;
		case DAA_FRANCE:
			DAA_Coeff_France(j);
			retval = ixj_daa_write(j);
			break;
		case DAA_GERMANY:
			DAA_Coeff_Germany(j);
			retval = ixj_daa_write(j);
			break;
		case DAA_AUSTRALIA:
			DAA_Coeff_Australia(j);
			retval = ixj_daa_write(j);
			break;
		case DAA_JAPAN:
			DAA_Coeff_Japan(j);
			retval = ixj_daa_write(j);
			break;
		default:
			retval = 1;
			break;
		}
		break;
	case IXJCTL_DAA_AGAIN:
		ixj_daa_cr4(j, arg | 0x02);
		break;
	case IXJCTL_PSTN_LINETEST:
		retval = ixj_linetest(j);
		break;
	case IXJCTL_VMWI:
		ixj_write_vmwi(j, arg);
		break;
	case IXJCTL_CID:
		if (copy_to_user(argp, &j->cid, sizeof(PHONE_CID))) 
			retval = -EFAULT;
		j->ex.bits.caller_id = 0;
		break;
	case IXJCTL_WINK_DURATION:
		j->winktime = arg;
		break;
	case IXJCTL_PORT:
		if (arg)
			retval = ixj_set_port(j, arg);
		else
			retval = j->port;
		break;
	case IXJCTL_POTS_PSTN:
		retval = ixj_set_pots(j, arg);
		break;
	case PHONE_CAPABILITIES:
		add_caps(j);
		retval = j->caps;
		break;
	case PHONE_CAPABILITIES_LIST:
		add_caps(j);
		if (copy_to_user(argp, j->caplist, sizeof(struct phone_capability) * j->caps)) 
			retval = -EFAULT;
		break;
	case PHONE_CAPABILITIES_CHECK:
		{
			struct phone_capability cap;
			if (copy_from_user(&cap, argp, sizeof(cap))) 
				retval = -EFAULT;
			else {
				add_caps(j);
				retval = capabilities_check(j, &cap);
			}
		}
		break;
	case PHONE_PSTN_SET_STATE:
		daa_set_mode(j, arg);
		break;
	case PHONE_PSTN_GET_STATE:
		retval = j->daa_mode;
		j->ex.bits.pstn_ring = 0;
		break;
	case IXJCTL_SET_FILTER:
		if (copy_from_user(&jf, argp, sizeof(jf))) 
			retval = -EFAULT;
		retval = ixj_init_filter(j, &jf);
		break;
	case IXJCTL_SET_FILTER_RAW:
		if (copy_from_user(&jfr, argp, sizeof(jfr))) 
			retval = -EFAULT;
		else
			retval = ixj_init_filter_raw(j, &jfr);
		break;
	case IXJCTL_GET_FILTER_HIST:
		if(arg<0||arg>3)
			retval = -EINVAL;
		else
			retval = j->filter_hist[arg];
		break;
	case IXJCTL_INIT_TONE:
		if (copy_from_user(&ti, argp, sizeof(ti)))
			retval = -EFAULT;
		else
			retval = ixj_init_tone(j, &ti);
		break;
	case IXJCTL_TONE_CADENCE:
		retval = ixj_build_cadence(j, argp);
		break;
	case IXJCTL_FILTER_CADENCE:
		retval = ixj_build_filter_cadence(j, argp);
		break;
	case IXJCTL_SIGCTL:
		if (copy_from_user(&j->sigdef, argp, sizeof(IXJ_SIGDEF))) {
			retval = -EFAULT;
			break;
		}
		j->ixj_signals[j->sigdef.event] = j->sigdef.signal;
		if(j->sigdef.event < 33) {
			raise = 1;
			for(mant = 0; mant < j->sigdef.event; mant++){
				raise *= 2;
			}
			if(j->sigdef.signal)
				j->ex_sig.bytes |= raise; 
			else
				j->ex_sig.bytes &= (raise^0xffff); 
		}
		break;
	case IXJCTL_INTERCOM_STOP:
		if(arg < 0 || arg >= IXJMAX)
			return -EINVAL;
		j->intercom = -1;
		ixj_record_stop(j);
		ixj_play_stop(j);
		idle(j);
		get_ixj(arg)->intercom = -1;
		ixj_record_stop(get_ixj(arg));
		ixj_play_stop(get_ixj(arg));
		idle(get_ixj(arg));
		break;
	case IXJCTL_INTERCOM_START:
		if(arg < 0 || arg >= IXJMAX)
			return -EINVAL;
		j->intercom = arg;
		ixj_record_start(j);
		ixj_play_start(j);
		get_ixj(arg)->intercom = board;
		ixj_play_start(get_ixj(arg));
		ixj_record_start(get_ixj(arg));
		break;
	}
	if (ixjdebug & 0x0040)
		printk("phone%d ioctl end, cmd: 0x%x, arg: 0x%lx\n", minor, cmd, arg);
	clear_bit(board, &j->busyflags);
	return retval;
}

static int ixj_fasync(int fd, struct file *file_p, int mode)
{
	IXJ *j = get_ixj(NUM(file_p->f_path.dentry->d_inode));

	return fasync_helper(fd, file_p, mode, &j->async_queue);
}

static const struct file_operations ixj_fops =
{
        .owner          = THIS_MODULE,
        .read           = ixj_enhanced_read,
        .write          = ixj_enhanced_write,
        .poll           = ixj_poll,
        .ioctl          = ixj_ioctl,
        .release        = ixj_release,
        .fasync         = ixj_fasync
};

static int ixj_linetest(IXJ *j)
{
	j->flags.pstncheck = 1;	/* Testing */
	j->flags.pstn_present = 0; /* Assume the line is not there */

	daa_int_read(j);	/*Clear DAA Interrupt flags */
	/* */
	/* Hold all relays in the normally de-energized position. */
	/* */

	j->pld_slicw.bits.rly1 = 0;
	j->pld_slicw.bits.rly2 = 0;
	j->pld_slicw.bits.rly3 = 0;
	outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
	j->pld_scrw.bits.daafsyncen = 0;	/* Turn off DAA Frame Sync */

	outb_p(j->pld_scrw.byte, j->XILINXbase);
	j->pld_slicr.byte = inb_p(j->XILINXbase + 0x01);
	if (j->pld_slicr.bits.potspstn) {
		j->flags.pots_pstn = 1;
		j->flags.pots_correct = 0;
		LED_SetState(0x4, j);
	} else {
		j->flags.pots_pstn = 0;
		j->pld_slicw.bits.rly1 = 0;
		j->pld_slicw.bits.rly2 = 0;
		j->pld_slicw.bits.rly3 = 1;
		outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		j->pld_scrw.bits.daafsyncen = 0;	/* Turn off DAA Frame Sync */

		outb_p(j->pld_scrw.byte, j->XILINXbase);
		daa_set_mode(j, SOP_PU_CONVERSATION);
		msleep(1000);
		daa_int_read(j);
		daa_set_mode(j, SOP_PU_RESET);
		if (j->m_DAAShadowRegs.XOP_REGS.XOP.xr0.bitreg.VDD_OK) {
			j->flags.pots_correct = 0;	/* Should not be line voltage on POTS port. */
			LED_SetState(0x4, j);
			j->pld_slicw.bits.rly3 = 0;
			outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		} else {
			j->flags.pots_correct = 1;
			LED_SetState(0x8, j);
			j->pld_slicw.bits.rly1 = 1;
			j->pld_slicw.bits.rly2 = 0;
			j->pld_slicw.bits.rly3 = 0;
			outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		}
	}
	j->pld_slicw.bits.rly3 = 0;
	outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
	daa_set_mode(j, SOP_PU_CONVERSATION);
	msleep(1000);
	daa_int_read(j);
	daa_set_mode(j, SOP_PU_RESET);
	if (j->m_DAAShadowRegs.XOP_REGS.XOP.xr0.bitreg.VDD_OK) {
		j->pstn_sleeptil = jiffies + (hertz / 4);
		j->flags.pstn_present = 1;
	} else {
		j->flags.pstn_present = 0;
	}
	if (j->flags.pstn_present) {
		if (j->flags.pots_correct) {
			LED_SetState(0xA, j);
		} else {
			LED_SetState(0x6, j);
		}
	} else {
		if (j->flags.pots_correct) {
			LED_SetState(0x9, j);
		} else {
			LED_SetState(0x5, j);
		}
	}
	j->flags.pstncheck = 0;	/* Testing */
	return j->flags.pstn_present;
}

static int ixj_selfprobe(IXJ *j)
{
	unsigned short cmd;
	int cnt;
	BYTES bytes;

        init_waitqueue_head(&j->poll_q);
        init_waitqueue_head(&j->read_q);
        init_waitqueue_head(&j->write_q);

	while(atomic_read(&j->DSPWrite) > 0)
		atomic_dec(&j->DSPWrite);
	if (ixjdebug & 0x0002)
		printk(KERN_INFO "Write IDLE to Software Control Register\n");
	ixj_WriteDSPCommand(0x0FE0, j);	/* Put the DSP in full power mode. */

	if (ixj_WriteDSPCommand(0x0000, j))		/* Write IDLE to Software Control Register */
		return -1;
/* The read values of the SSR should be 0x00 for the IDLE command */
	if (j->ssr.low || j->ssr.high)
		return -1;
	if (ixjdebug & 0x0002)
		printk(KERN_INFO "Get Device ID Code\n");
	if (ixj_WriteDSPCommand(0x3400, j))		/* Get Device ID Code */
		return -1;
	j->dsp.low = j->ssr.low;
	j->dsp.high = j->ssr.high;
	if (ixjdebug & 0x0002)
		printk(KERN_INFO "Get Device Version Code\n");
	if (ixj_WriteDSPCommand(0x3800, j))		/* Get Device Version Code */
		return -1;
	j->ver.low = j->ssr.low;
	j->ver.high = j->ssr.high;
	if (!j->cardtype) {
		if (j->dsp.low == 0x21) {
			bytes.high = bytes.low = inb_p(j->XILINXbase + 0x02);
			outb_p(bytes.low ^ 0xFF, j->XILINXbase + 0x02);
/* Test for Internet LineJACK or Internet PhoneJACK Lite */
			bytes.low = inb_p(j->XILINXbase + 0x02);
			if (bytes.low == bytes.high)	/*  Register is read only on */
				/*  Internet PhoneJack Lite */
			 {
				j->cardtype = QTI_PHONEJACK_LITE;
				if (!request_region(j->XILINXbase, 4, "ixj control")) {
					printk(KERN_INFO "ixj: can't get I/O address 0x%x\n", j->XILINXbase);
					return -1;
				}
				j->pld_slicw.pcib.e1 = 1;
				outb_p(j->pld_slicw.byte, j->XILINXbase);
			} else {
				j->cardtype = QTI_LINEJACK;

				if (!request_region(j->XILINXbase, 8, "ixj control")) {
					printk(KERN_INFO "ixj: can't get I/O address 0x%x\n", j->XILINXbase);
					return -1;
				}
			}
		} else if (j->dsp.low == 0x22) {
			j->cardtype = QTI_PHONEJACK_PCI;
			request_region(j->XILINXbase, 4, "ixj control");
			j->pld_slicw.pcib.e1 = 1;
			outb_p(j->pld_slicw.byte, j->XILINXbase);
		} else
			j->cardtype = QTI_PHONEJACK;
	} else {
		switch (j->cardtype) {
		case QTI_PHONEJACK:
			if (!j->dsp.low != 0x20) {
				j->dsp.high = 0x80;
				j->dsp.low = 0x20;
				ixj_WriteDSPCommand(0x3800, j);
				j->ver.low = j->ssr.low;
				j->ver.high = j->ssr.high;
			}
			break;
		case QTI_LINEJACK:
			if (!request_region(j->XILINXbase, 8, "ixj control")) {
				printk(KERN_INFO "ixj: can't get I/O address 0x%x\n", j->XILINXbase);
				return -1;
			}
			break;
		case QTI_PHONEJACK_LITE:
		case QTI_PHONEJACK_PCI:
			if (!request_region(j->XILINXbase, 4, "ixj control")) {
				printk(KERN_INFO "ixj: can't get I/O address 0x%x\n", j->XILINXbase);
				return -1;
			}
			j->pld_slicw.pcib.e1 = 1;
			outb_p(j->pld_slicw.byte, j->XILINXbase);
			break;
		case QTI_PHONECARD:
			break;
		}
	}
	if (j->dsp.low == 0x20 || j->cardtype == QTI_PHONEJACK_LITE || j->cardtype == QTI_PHONEJACK_PCI) {
		if (ixjdebug & 0x0002)
			printk(KERN_INFO "Write CODEC config to Software Control Register\n");
		if (ixj_WriteDSPCommand(0xC462, j))		/* Write CODEC config to Software Control Register */
			return -1;
		if (ixjdebug & 0x0002)
			printk(KERN_INFO "Write CODEC timing to Software Control Register\n");
		if (j->cardtype == QTI_PHONEJACK) {
			cmd = 0x9FF2;
		} else {
			cmd = 0x9FF5;
		}
		if (ixj_WriteDSPCommand(cmd, j))	/* Write CODEC timing to Software Control Register */
			return -1;
	} else {
		if (set_base_frame(j, 30) != 30)
			return -1;
		if (ixjdebug & 0x0002)
			printk(KERN_INFO "Write CODEC config to Software Control Register\n");
		if (j->cardtype == QTI_PHONECARD) {
			if (ixj_WriteDSPCommand(0xC528, j))		/* Write CODEC config to Software Control Register */
				return -1;
		}
		if (j->cardtype == QTI_LINEJACK) {
			if (ixj_WriteDSPCommand(0xC528, j))		/* Write CODEC config to Software Control Register */
				return -1;
			if (ixjdebug & 0x0002)
				printk(KERN_INFO "Turn on the PLD Clock at 8Khz\n");
			j->pld_clock.byte = 0;
			outb_p(j->pld_clock.byte, j->XILINXbase + 0x04);
		}
	}

	if (j->dsp.low == 0x20) {
		if (ixjdebug & 0x0002)
			printk(KERN_INFO "Configure GPIO pins\n");
		j->gpio.bytes.high = 0x09;
/*  bytes.low = 0xEF;  0xF7 */
		j->gpio.bits.gpio1 = 1;
		j->gpio.bits.gpio2 = 1;
		j->gpio.bits.gpio3 = 0;
		j->gpio.bits.gpio4 = 1;
		j->gpio.bits.gpio5 = 1;
		j->gpio.bits.gpio6 = 1;
		j->gpio.bits.gpio7 = 1;
		ixj_WriteDSPCommand(j->gpio.word, j);	/* Set GPIO pin directions */
		if (ixjdebug & 0x0002)
			printk(KERN_INFO "Enable SLIC\n");
		j->gpio.bytes.high = 0x0B;
		j->gpio.bytes.low = 0x00;
		j->gpio.bits.gpio1 = 0;
		j->gpio.bits.gpio2 = 1;
		j->gpio.bits.gpio5 = 0;
		ixj_WriteDSPCommand(j->gpio.word, j);	/* send the ring stop signal */
		j->port = PORT_POTS;
	} else {
		if (j->cardtype == QTI_LINEJACK) {
			LED_SetState(0x1, j);
			msleep(100);
			LED_SetState(0x2, j);
			msleep(100);
			LED_SetState(0x4, j);
			msleep(100);
			LED_SetState(0x8, j);
			msleep(100);
			LED_SetState(0x0, j);
			daa_get_version(j);
			if (ixjdebug & 0x0002)
				printk("Loading DAA Coefficients\n");
			DAA_Coeff_US(j);
			if (!ixj_daa_write(j)) {
				printk("DAA write failed on board %d\n", j->board);
				return -1;
			}
			if(!ixj_daa_cid_reset(j)) {
				printk("DAA CID reset failed on board %d\n", j->board);
				return -1;
			}
			j->flags.pots_correct = 0;
			j->flags.pstn_present = 0;
			ixj_linetest(j);
			if (j->flags.pots_correct) {
				j->pld_scrw.bits.daafsyncen = 0;	/* Turn off DAA Frame Sync */

				outb_p(j->pld_scrw.byte, j->XILINXbase);
				j->pld_slicw.bits.rly1 = 1;
				j->pld_slicw.bits.spken = 1;
				outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
				SLIC_SetState(PLD_SLIC_STATE_STANDBY, j);
/*				SLIC_SetState(PLD_SLIC_STATE_ACTIVE, j); */
				j->port = PORT_POTS;
			}
			ixj_set_port(j, PORT_PSTN);
			ixj_set_pots(j, 1);
			if (ixjdebug & 0x0002)
				printk(KERN_INFO "Enable Mixer\n");
			ixj_mixer(0x0000, j);	/*Master Volume Left unmute 0db */
			ixj_mixer(0x0100, j);	/*Master Volume Right unmute 0db */

			ixj_mixer(0x0203, j);	/*Voice Left Volume unmute 6db */
			ixj_mixer(0x0303, j);	/*Voice Right Volume unmute 6db */

			ixj_mixer(0x0480, j);	/*FM Left mute */
			ixj_mixer(0x0580, j);	/*FM Right mute */

			ixj_mixer(0x0680, j);	/*CD Left mute */
			ixj_mixer(0x0780, j);	/*CD Right mute */

			ixj_mixer(0x0880, j);	/*Line Left mute */
			ixj_mixer(0x0980, j);	/*Line Right mute */

			ixj_mixer(0x0A80, j);	/*Aux left mute  */
			ixj_mixer(0x0B80, j);	/*Aux right mute */

			ixj_mixer(0x0C00, j);	/*Mono1 unmute 12db */
			ixj_mixer(0x0D80, j);	/*Mono2 mute */

			ixj_mixer(0x0E80, j);	/*Mic mute */

			ixj_mixer(0x0F00, j);	/*Mono Out Volume unmute 0db */

			ixj_mixer(0x1000, j);	/*Voice Left and Right out only */
			ixj_mixer(0x110C, j);


			ixj_mixer(0x1200, j);	/*Mono1 switch on mixer left */
			ixj_mixer(0x1401, j);

			ixj_mixer(0x1300, j);       /*Mono1 switch on mixer right */
			ixj_mixer(0x1501, j);

			ixj_mixer(0x1700, j);	/*Clock select */

			ixj_mixer(0x1800, j);	/*ADC input from mixer */

			ixj_mixer(0x1901, j);	/*Mic gain 30db */

			if (ixjdebug & 0x0002)
				printk(KERN_INFO "Setting Default US Ring Cadence Detection\n");
			j->cadence_f[4].state = 0;
			j->cadence_f[4].on1 = 0;	/*Cadence Filter 4 is used for PSTN ring cadence */
			j->cadence_f[4].off1 = 0;
			j->cadence_f[4].on2 = 0;
			j->cadence_f[4].off2 = 0;
			j->cadence_f[4].on3 = 0;
			j->cadence_f[4].off3 = 0;	/* These should represent standard US ring pulse. */
			j->pstn_last_rmr = jiffies;

		} else {
			if (j->cardtype == QTI_PHONECARD) {
				ixj_WriteDSPCommand(0xCF07, j);
				ixj_WriteDSPCommand(0x00B0, j);
				ixj_set_port(j, PORT_SPEAKER);
			} else {
				ixj_set_port(j, PORT_POTS);
				SLIC_SetState(PLD_SLIC_STATE_STANDBY, j);
/*				SLIC_SetState(PLD_SLIC_STATE_ACTIVE, j); */
			}
		}
	}

	j->intercom = -1;
	j->framesread = j->frameswritten = 0;
	j->read_wait = j->write_wait = 0;
	j->rxreadycheck = j->txreadycheck = 0;

	/* initialise the DTMF prescale to a sensible value */
	if (j->cardtype == QTI_LINEJACK) {
		set_dtmf_prescale(j, 0x10); 
	} else {
		set_dtmf_prescale(j, 0x40); 
	}
	set_play_volume(j, 0x100);
	set_rec_volume(j, 0x100);

	if (ixj_WriteDSPCommand(0x0000, j))		/* Write IDLE to Software Control Register */
		return -1;
/* The read values of the SSR should be 0x00 for the IDLE command */
	if (j->ssr.low || j->ssr.high)
		return -1;

	if (ixjdebug & 0x0002)
		printk(KERN_INFO "Enable Line Monitor\n");

	if (ixjdebug & 0x0002)
		printk(KERN_INFO "Set Line Monitor to Asyncronous Mode\n");

	if (ixj_WriteDSPCommand(0x7E01, j))		/* Asynchronous Line Monitor */
		return -1;

	if (ixjdebug & 0x002)
		printk(KERN_INFO "Enable DTMF Detectors\n");

	if (ixj_WriteDSPCommand(0x5151, j))		/* Enable DTMF detection */
		return -1;

	if (ixj_WriteDSPCommand(0x6E01, j))		/* Set Asyncronous Tone Generation */
		return -1;

	set_rec_depth(j, 2);	/* Set Record Channel Limit to 2 frames */

	set_play_depth(j, 2);	/* Set Playback Channel Limit to 2 frames */

	j->ex.bits.dtmf_ready = 0;
	j->dtmf_state = 0;
	j->dtmf_wp = j->dtmf_rp = 0;
	j->rec_mode = j->play_mode = -1;
	j->flags.ringing = 0;
	j->maxrings = MAXRINGS;
	j->ring_cadence = USA_RING_CADENCE;
	j->drybuffer = 0;
	j->winktime = 320;
	j->flags.dtmf_oob = 0;
	for (cnt = 0; cnt < 4; cnt++)
		j->cadence_f[cnt].enable = 0;
	/* must be a device on the specified address */
	ixj_WriteDSPCommand(0x0FE3, j);	/* Put the DSP in 1/5 power mode. */

	/* Set up the default signals for events */
	for (cnt = 0; cnt < 35; cnt++)
		j->ixj_signals[cnt] = SIGIO;

	/* Set the excetion signal enable flags */
	j->ex_sig.bits.dtmf_ready = j->ex_sig.bits.hookstate = j->ex_sig.bits.flash = j->ex_sig.bits.pstn_ring = 
	j->ex_sig.bits.caller_id = j->ex_sig.bits.pstn_wink = j->ex_sig.bits.f0 = j->ex_sig.bits.f1 = j->ex_sig.bits.f2 = 
	j->ex_sig.bits.f3 = j->ex_sig.bits.fc0 = j->ex_sig.bits.fc1 = j->ex_sig.bits.fc2 = j->ex_sig.bits.fc3 = 1;
#ifdef IXJ_DYN_ALLOC
	j->fskdata = NULL;
#endif
	j->fskdcnt = 0;
	j->cidcw_wait = 0;
 
	/* Register with the Telephony for Linux subsystem */
	j->p.f_op = &ixj_fops;
	j->p.open = ixj_open;
	j->p.board = j->board;
	phone_register_device(&j->p, PHONE_UNIT_ANY);

	ixj_init_timer(j);
	ixj_add_timer(j);
	return 0;
}

/*
 *	Exported service for pcmcia card handling
 */
 
IXJ *ixj_pcmcia_probe(unsigned long dsp, unsigned long xilinx)
{
	IXJ *j = ixj_alloc();

	j->board = 0;

	j->DSPbase = dsp;
	j->XILINXbase = xilinx;
	j->cardtype = QTI_PHONECARD;
	ixj_selfprobe(j);
	return j;
}

EXPORT_SYMBOL(ixj_pcmcia_probe);		/* Fpr PCMCIA */

static int ixj_get_status_proc(char *buf)
{
	int len;
	int cnt;
	IXJ *j;
	len = 0;
	len += sprintf(buf + len, "\nDriver version %i.%i.%i", IXJ_VER_MAJOR, IXJ_VER_MINOR, IXJ_BLD_VER);
	len += sprintf(buf + len, "\nsizeof IXJ struct %Zd bytes", sizeof(IXJ));
	len += sprintf(buf + len, "\nsizeof DAA struct %Zd bytes", sizeof(DAA_REGS));
	len += sprintf(buf + len, "\nUsing old telephony API");
	len += sprintf(buf + len, "\nDebug Level %d\n", ixjdebug);

	for (cnt = 0; cnt < IXJMAX; cnt++) {
		j = get_ixj(cnt);
		if(j==NULL)
			continue;
		if (j->DSPbase) {
			len += sprintf(buf + len, "\nCard Num %d", cnt);
			len += sprintf(buf + len, "\nDSP Base Address 0x%4.4x", j->DSPbase);
			if (j->cardtype != QTI_PHONEJACK)
				len += sprintf(buf + len, "\nXILINX Base Address 0x%4.4x", j->XILINXbase);
			len += sprintf(buf + len, "\nDSP Type %2.2x%2.2x", j->dsp.high, j->dsp.low);
			len += sprintf(buf + len, "\nDSP Version %2.2x.%2.2x", j->ver.high, j->ver.low);
			len += sprintf(buf + len, "\nSerial Number %8.8x", j->serial);
			switch (j->cardtype) {
			case (QTI_PHONEJACK):
				len += sprintf(buf + len, "\nCard Type = Internet PhoneJACK");
				break;
			case (QTI_LINEJACK):
				len += sprintf(buf + len, "\nCard Type = Internet LineJACK");
				if (j->flags.g729_loaded)
					len += sprintf(buf + len, " w/G.729 A/B");
				len += sprintf(buf + len, " Country = %d", j->daa_country);
				break;
			case (QTI_PHONEJACK_LITE):
				len += sprintf(buf + len, "\nCard Type = Internet PhoneJACK Lite");
				if (j->flags.g729_loaded)
					len += sprintf(buf + len, " w/G.729 A/B");
				break;
			case (QTI_PHONEJACK_PCI):
				len += sprintf(buf + len, "\nCard Type = Internet PhoneJACK PCI");
				if (j->flags.g729_loaded)
					len += sprintf(buf + len, " w/G.729 A/B");
				break;
			case (QTI_PHONECARD):
				len += sprintf(buf + len, "\nCard Type = Internet PhoneCARD");
				if (j->flags.g729_loaded)
					len += sprintf(buf + len, " w/G.729 A/B");
				len += sprintf(buf + len, "\nSmart Cable %spresent", j->pccr1.bits.drf ? "not " : "");
				if (!j->pccr1.bits.drf)
					len += sprintf(buf + len, "\nSmart Cable type %d", j->flags.pcmciasct);
				len += sprintf(buf + len, "\nSmart Cable state %d", j->flags.pcmciastate);
				break;
			default:
				len += sprintf(buf + len, "\nCard Type = %d", j->cardtype);
				break;
			}
			len += sprintf(buf + len, "\nReaders %d", j->readers);
			len += sprintf(buf + len, "\nWriters %d", j->writers);
			add_caps(j);
			len += sprintf(buf + len, "\nCapabilities %d", j->caps);
			if (j->dsp.low != 0x20)
				len += sprintf(buf + len, "\nDSP Processor load %d", j->proc_load);
			if (j->flags.cidsent)
				len += sprintf(buf + len, "\nCaller ID data sent");
			else
				len += sprintf(buf + len, "\nCaller ID data not sent");

			len += sprintf(buf + len, "\nPlay CODEC ");
			switch (j->play_codec) {
			case G723_63:
				len += sprintf(buf + len, "G.723.1 6.3");
				break;
			case G723_53:
				len += sprintf(buf + len, "G.723.1 5.3");
				break;
			case TS85:
				len += sprintf(buf + len, "TrueSpeech 8.5");
				break;
			case TS48:
				len += sprintf(buf + len, "TrueSpeech 4.8");
				break;
			case TS41:
				len += sprintf(buf + len, "TrueSpeech 4.1");
				break;
			case G728:
				len += sprintf(buf + len, "G.728");
				break;
			case G729:
				len += sprintf(buf + len, "G.729");
				break;
			case G729B:
				len += sprintf(buf + len, "G.729B");
				break;
			case ULAW:
				len += sprintf(buf + len, "uLaw");
				break;
			case ALAW:
				len += sprintf(buf + len, "aLaw");
				break;
			case LINEAR16:
				len += sprintf(buf + len, "16 bit Linear");
				break;
			case LINEAR8:
				len += sprintf(buf + len, "8 bit Linear");
				break;
			case WSS:
				len += sprintf(buf + len, "Windows Sound System");
				break;
			default:
				len += sprintf(buf + len, "NO CODEC CHOSEN");
				break;
			}
			len += sprintf(buf + len, "\nRecord CODEC ");
			switch (j->rec_codec) {
			case G723_63:
				len += sprintf(buf + len, "G.723.1 6.3");
				break;
			case G723_53:
				len += sprintf(buf + len, "G.723.1 5.3");
				break;
			case TS85:
				len += sprintf(buf + len, "TrueSpeech 8.5");
				break;
			case TS48:
				len += sprintf(buf + len, "TrueSpeech 4.8");
				break;
			case TS41:
				len += sprintf(buf + len, "TrueSpeech 4.1");
				break;
			case G728:
				len += sprintf(buf + len, "G.728");
				break;
			case G729:
				len += sprintf(buf + len, "G.729");
				break;
			case G729B:
				len += sprintf(buf + len, "G.729B");
				break;
			case ULAW:
				len += sprintf(buf + len, "uLaw");
				break;
			case ALAW:
				len += sprintf(buf + len, "aLaw");
				break;
			case LINEAR16:
				len += sprintf(buf + len, "16 bit Linear");
				break;
			case LINEAR8:
				len += sprintf(buf + len, "8 bit Linear");
				break;
			case WSS:
				len += sprintf(buf + len, "Windows Sound System");
				break;
			default:
				len += sprintf(buf + len, "NO CODEC CHOSEN");
				break;
			}
			len += sprintf(buf + len, "\nAEC ");
			switch (j->aec_level) {
			case AEC_OFF:
				len += sprintf(buf + len, "Off");
				break;
			case AEC_LOW:
				len += sprintf(buf + len, "Low");
				break;
			case AEC_MED:
				len += sprintf(buf + len, "Med");
				break;
			case AEC_HIGH:
				len += sprintf(buf + len, "High");
				break;
			case AEC_AUTO:
				len += sprintf(buf + len, "Auto");
				break;
			case AEC_AGC:
				len += sprintf(buf + len, "AEC/AGC");
				break;
			default:
				len += sprintf(buf + len, "unknown(%i)", j->aec_level);
				break;
			}

			len += sprintf(buf + len, "\nRec volume 0x%x", get_rec_volume(j));
			len += sprintf(buf + len, "\nPlay volume 0x%x", get_play_volume(j));
			len += sprintf(buf + len, "\nDTMF prescale 0x%x", get_dtmf_prescale(j));
			
			len += sprintf(buf + len, "\nHook state %d", j->hookstate); /* j->r_hook);	*/

			if (j->cardtype == QTI_LINEJACK) {
				len += sprintf(buf + len, "\nPOTS Correct %d", j->flags.pots_correct);
				len += sprintf(buf + len, "\nPSTN Present %d", j->flags.pstn_present);
				len += sprintf(buf + len, "\nPSTN Check %d", j->flags.pstncheck);
				len += sprintf(buf + len, "\nPOTS to PSTN %d", j->flags.pots_pstn);
				switch (j->daa_mode) {
				case SOP_PU_SLEEP:
					len += sprintf(buf + len, "\nDAA PSTN On Hook");
					break;
				case SOP_PU_RINGING:
					len += sprintf(buf + len, "\nDAA PSTN Ringing");
					len += sprintf(buf + len, "\nRinging state = %d", j->cadence_f[4].state);
					break;
				case SOP_PU_CONVERSATION:
					len += sprintf(buf + len, "\nDAA PSTN Off Hook");
					break;
				case SOP_PU_PULSEDIALING:
					len += sprintf(buf + len, "\nDAA PSTN Pulse Dialing");
					break;
				}
				len += sprintf(buf + len, "\nDAA RMR = %d", j->m_DAAShadowRegs.SOP_REGS.SOP.cr1.bitreg.RMR);
				len += sprintf(buf + len, "\nDAA VDD OK = %d", j->m_DAAShadowRegs.XOP_REGS.XOP.xr0.bitreg.VDD_OK);
				len += sprintf(buf + len, "\nDAA CR0 = 0x%02x", j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg);
				len += sprintf(buf + len, "\nDAA CR1 = 0x%02x", j->m_DAAShadowRegs.SOP_REGS.SOP.cr1.reg);
				len += sprintf(buf + len, "\nDAA CR2 = 0x%02x", j->m_DAAShadowRegs.SOP_REGS.SOP.cr2.reg);
				len += sprintf(buf + len, "\nDAA CR3 = 0x%02x", j->m_DAAShadowRegs.SOP_REGS.SOP.cr3.reg);
				len += sprintf(buf + len, "\nDAA CR4 = 0x%02x", j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.reg);
				len += sprintf(buf + len, "\nDAA CR5 = 0x%02x", j->m_DAAShadowRegs.SOP_REGS.SOP.cr5.reg);
				len += sprintf(buf + len, "\nDAA XR0 = 0x%02x", j->m_DAAShadowRegs.XOP_REGS.XOP.xr0.reg);
				len += sprintf(buf + len, "\nDAA ringstop %ld - jiffies %ld", j->pstn_ring_stop, jiffies);
			}
			switch (j->port) {
			case PORT_POTS:
				len += sprintf(buf + len, "\nPort POTS");
				break;
			case PORT_PSTN:
				len += sprintf(buf + len, "\nPort PSTN");
				break;
			case PORT_SPEAKER:
				len += sprintf(buf + len, "\nPort SPEAKER/MIC");
				break;
			case PORT_HANDSET:
				len += sprintf(buf + len, "\nPort HANDSET");
				break;
			}
			if (j->dsp.low == 0x21 || j->dsp.low == 0x22) {
				len += sprintf(buf + len, "\nSLIC state ");
				switch (SLIC_GetState(j)) {
				case PLD_SLIC_STATE_OC:
					len += sprintf(buf + len, "OC");
					break;
				case PLD_SLIC_STATE_RINGING:
					len += sprintf(buf + len, "RINGING");
					break;
				case PLD_SLIC_STATE_ACTIVE:
					len += sprintf(buf + len, "ACTIVE");
					break;
				case PLD_SLIC_STATE_OHT:	/* On-hook transmit */
					len += sprintf(buf + len, "OHT");
					break;
				case PLD_SLIC_STATE_TIPOPEN:
					len += sprintf(buf + len, "TIPOPEN");
					break;
				case PLD_SLIC_STATE_STANDBY:
					len += sprintf(buf + len, "STANDBY");
					break;
				case PLD_SLIC_STATE_APR:	/* Active polarity reversal */
					len += sprintf(buf + len, "APR");
					break;
				case PLD_SLIC_STATE_OHTPR:	/* OHT polarity reversal */
					len += sprintf(buf + len, "OHTPR");
					break;
				default:
					len += sprintf(buf + len, "%d", SLIC_GetState(j));
					break;
				}
			}
			len += sprintf(buf + len, "\nBase Frame %2.2x.%2.2x", j->baseframe.high, j->baseframe.low);
			len += sprintf(buf + len, "\nCID Base Frame %2d", j->cid_base_frame_size);
#ifdef PERFMON_STATS
			len += sprintf(buf + len, "\nTimer Checks %ld", j->timerchecks);
			len += sprintf(buf + len, "\nRX Ready Checks %ld", j->rxreadycheck);
			len += sprintf(buf + len, "\nTX Ready Checks %ld", j->txreadycheck);
			len += sprintf(buf + len, "\nFrames Read %ld", j->framesread);
			len += sprintf(buf + len, "\nFrames Written %ld", j->frameswritten);
			len += sprintf(buf + len, "\nDry Buffer %ld", j->drybuffer);
			len += sprintf(buf + len, "\nRead Waits %ld", j->read_wait);
			len += sprintf(buf + len, "\nWrite Waits %ld", j->write_wait);
                        len += sprintf(buf + len, "\nStatus Waits %ld", j->statuswait);
                        len += sprintf(buf + len, "\nStatus Wait Fails %ld", j->statuswaitfail);
                        len += sprintf(buf + len, "\nPControl Waits %ld", j->pcontrolwait);
                        len += sprintf(buf + len, "\nPControl Wait Fails %ld", j->pcontrolwaitfail);
                        len += sprintf(buf + len, "\nIs Control Ready Checks %ld", j->iscontrolready);
                        len += sprintf(buf + len, "\nIs Control Ready Check failures %ld", j->iscontrolreadyfail);
 
#endif
			len += sprintf(buf + len, "\n");
		}
	}
	return len;
}

static int ixj_read_proc(char *page, char **start, off_t off,
                              int count, int *eof, void *data)
{
        int len = ixj_get_status_proc(page);
        if (len <= off+count) *eof = 1;
        *start = page + off;
        len -= off;
        if (len>count) len = count;
        if (len<0) len = 0;
        return len;
}


static void cleanup(void)
{
	int cnt;
	IXJ *j;

	for (cnt = 0; cnt < IXJMAX; cnt++) {
		j = get_ixj(cnt);
		if(j != NULL && j->DSPbase) {
			if (ixjdebug & 0x0002)
				printk(KERN_INFO "IXJ: Deleting timer for /dev/phone%d\n", cnt);
			del_timer(&j->timer);
			if (j->cardtype == QTI_LINEJACK) {
				j->pld_scrw.bits.daafsyncen = 0;	/* Turn off DAA Frame Sync */

				outb_p(j->pld_scrw.byte, j->XILINXbase);
				j->pld_slicw.bits.rly1 = 0;
				j->pld_slicw.bits.rly2 = 0;
				j->pld_slicw.bits.rly3 = 0;
				outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
				LED_SetState(0x0, j);
				if (ixjdebug & 0x0002)
					printk(KERN_INFO "IXJ: Releasing XILINX address for /dev/phone%d\n", cnt);
				release_region(j->XILINXbase, 8);
			} else if (j->cardtype == QTI_PHONEJACK_LITE || j->cardtype == QTI_PHONEJACK_PCI) {
				if (ixjdebug & 0x0002)
					printk(KERN_INFO "IXJ: Releasing XILINX address for /dev/phone%d\n", cnt);
				release_region(j->XILINXbase, 4);
			}
			kfree(j->read_buffer);
			kfree(j->write_buffer);
			if (j->dev)
				pnp_device_detach(j->dev);
			if (ixjdebug & 0x0002)
				printk(KERN_INFO "IXJ: Unregistering /dev/phone%d from LTAPI\n", cnt);
			phone_unregister_device(&j->p);
			if (ixjdebug & 0x0002)
				printk(KERN_INFO "IXJ: Releasing DSP address for /dev/phone%d\n", cnt);
			release_region(j->DSPbase, 16);
#ifdef IXJ_DYN_ALLOC
			if (ixjdebug & 0x0002)
				printk(KERN_INFO "IXJ: Freeing memory for /dev/phone%d\n", cnt);
			kfree(j);
			ixj[cnt] = NULL;
#endif
		}
	}
	if (ixjdebug & 0x0002)
		printk(KERN_INFO "IXJ: Removing /proc/ixj\n");
	remove_proc_entry ("ixj", NULL);
}

/* Typedefs */
typedef struct {
	BYTE length;
	DWORD bits;
} DATABLOCK;

static void PCIEE_WriteBit(WORD wEEPROMAddress, BYTE lastLCC, BYTE byData)
{
	lastLCC = lastLCC & 0xfb;
	lastLCC = lastLCC | (byData ? 4 : 0);
	outb(lastLCC, wEEPROMAddress);	/*set data out bit as appropriate */

	mdelay(1);
	lastLCC = lastLCC | 0x01;
	outb(lastLCC, wEEPROMAddress);	/*SK rising edge */

	byData = byData << 1;
	lastLCC = lastLCC & 0xfe;
	mdelay(1);
	outb(lastLCC, wEEPROMAddress);	/*after delay, SK falling edge */

}

static BYTE PCIEE_ReadBit(WORD wEEPROMAddress, BYTE lastLCC)
{
	mdelay(1);
	lastLCC = lastLCC | 0x01;
	outb(lastLCC, wEEPROMAddress);	/*SK rising edge */

	lastLCC = lastLCC & 0xfe;
	mdelay(1);
	outb(lastLCC, wEEPROMAddress);	/*after delay, SK falling edge */

	return ((inb(wEEPROMAddress) >> 3) & 1);
}

static bool PCIEE_ReadWord(WORD wAddress, WORD wLoc, WORD * pwResult)
{
	BYTE lastLCC;
	WORD wEEPROMAddress = wAddress + 3;
	DWORD i;
	BYTE byResult;
	*pwResult = 0;
	lastLCC = inb(wEEPROMAddress);
	lastLCC = lastLCC | 0x02;
	lastLCC = lastLCC & 0xfe;
	outb(lastLCC, wEEPROMAddress);	/* CS hi, SK lo */

	mdelay(1);		/* delay */

	PCIEE_WriteBit(wEEPROMAddress, lastLCC, 1);
	PCIEE_WriteBit(wEEPROMAddress, lastLCC, 1);
	PCIEE_WriteBit(wEEPROMAddress, lastLCC, 0);
	for (i = 0; i < 8; i++) {
		PCIEE_WriteBit(wEEPROMAddress, lastLCC, wLoc & 0x80 ? 1 : 0);
		wLoc <<= 1;
	}

	for (i = 0; i < 16; i++) {
		byResult = PCIEE_ReadBit(wEEPROMAddress, lastLCC);
		*pwResult = (*pwResult << 1) | byResult;
	}

	mdelay(1);		/* another delay */

	lastLCC = lastLCC & 0xfd;
	outb(lastLCC, wEEPROMAddress);	/* negate CS */

	return 0;
}

static DWORD PCIEE_GetSerialNumber(WORD wAddress)
{
	WORD wLo, wHi;
	if (PCIEE_ReadWord(wAddress, 62, &wLo))
		return 0;
	if (PCIEE_ReadWord(wAddress, 63, &wHi))
		return 0;
	return (((DWORD) wHi << 16) | wLo);
}

static int dspio[IXJMAX + 1] =
{
	0,
};
static int xio[IXJMAX + 1] =
{
	0,
};

module_param_array(dspio, int, NULL, 0);
module_param_array(xio, int, NULL, 0);
MODULE_DESCRIPTION("Quicknet VoIP Telephony card module - www.quicknet.net");
MODULE_AUTHOR("Ed Okerson <eokerson@quicknet.net>");
MODULE_LICENSE("GPL");

static void __exit ixj_exit(void)
{
        cleanup();
}

static IXJ *new_ixj(unsigned long port)
{
	IXJ *res;
	if (!request_region(port, 16, "ixj DSP")) {
		printk(KERN_INFO "ixj: can't get I/O address 0x%lx\n", port);
		return NULL;
	}
	res = ixj_alloc();
	if (!res) {
		release_region(port, 16);
		printk(KERN_INFO "ixj: out of memory\n");
		return NULL;
	}
	res->DSPbase = port;
	return res;
}

static int __init ixj_probe_isapnp(int *cnt)
{               
	int probe = 0;
	int func = 0x110;
        struct pnp_dev *dev = NULL, *old_dev = NULL;

	while (1) {
		do {
			IXJ *j;
			int result;

			old_dev = dev;
			dev = pnp_find_dev(NULL, ISAPNP_VENDOR('Q', 'T', 'I'),
					 ISAPNP_FUNCTION(func), old_dev);
			if (!dev || !dev->card)
				break;
			result = pnp_device_attach(dev);
			if (result < 0) {
				printk("pnp attach failed %d \n", result);
				break;
			}
			if (pnp_activate_dev(dev) < 0) {
				printk("pnp activate failed (out of resources?)\n");
				pnp_device_detach(dev);
				return -ENOMEM;
			}

			if (!pnp_port_valid(dev, 0)) {
				pnp_device_detach(dev);
				return -ENODEV;
			}

			j = new_ixj(pnp_port_start(dev, 0));
			if (!j)
				break;

			if (func != 0x110)
				j->XILINXbase = pnp_port_start(dev, 1);	/* get real port */

			switch (func) {
			case (0x110):
				j->cardtype = QTI_PHONEJACK;
				break;
			case (0x310):
				j->cardtype = QTI_LINEJACK;
				break;
			case (0x410):
				j->cardtype = QTI_PHONEJACK_LITE;
				break;
			}
			j->board = *cnt;
			probe = ixj_selfprobe(j);
			if(!probe) {
				j->serial = dev->card->serial;
				j->dev = dev;
				switch (func) {
				case 0x110:
					printk(KERN_INFO "ixj: found Internet PhoneJACK at 0x%x\n", j->DSPbase);
					break;
				case 0x310:
					printk(KERN_INFO "ixj: found Internet LineJACK at 0x%x\n", j->DSPbase);
					break;
				case 0x410:
					printk(KERN_INFO "ixj: found Internet PhoneJACK Lite at 0x%x\n", j->DSPbase);
					break;
				}
			}
			++*cnt;
		} while (dev);
		if (func == 0x410)
			break;
		if (func == 0x310)
			func = 0x410;
		if (func == 0x110)
			func = 0x310;
		dev = NULL;
	}
	return probe;
}
                        
static int __init ixj_probe_isa(int *cnt)
{
	int i, probe;

	/* Use passed parameters for older kernels without PnP */
	for (i = 0; i < IXJMAX; i++) {
		if (dspio[i]) {
			IXJ *j = new_ixj(dspio[i]);

			if (!j)
				break;

			j->XILINXbase = xio[i];
			j->cardtype = 0;

			j->board = *cnt;
			probe = ixj_selfprobe(j);
			j->dev = NULL;
			++*cnt;
		}
	}
	return 0;
}

static int __init ixj_probe_pci(int *cnt)
{
	struct pci_dev *pci = NULL;   
	int i, probe = 0;
	IXJ *j = NULL;

	for (i = 0; i < IXJMAX - *cnt; i++) {
		pci = pci_find_device(PCI_VENDOR_ID_QUICKNET,
				      PCI_DEVICE_ID_QUICKNET_XJ, pci);
		if (!pci)
			break;

		if (pci_enable_device(pci))
			break;
		j = new_ixj(pci_resource_start(pci, 0));
		if (!j)
			break;

		j->serial = (PCIEE_GetSerialNumber)pci_resource_start(pci, 2);
		j->XILINXbase = j->DSPbase + 0x10;
		j->cardtype = QTI_PHONEJACK_PCI;
		j->board = *cnt;
		probe = ixj_selfprobe(j);
		if (!probe)
			printk(KERN_INFO "ixj: found Internet PhoneJACK PCI at 0x%x\n", j->DSPbase);
		++*cnt;
	}
	return probe;
}

static int __init ixj_init(void)
{
	int cnt = 0;
	int probe = 0;   

	cnt = 0;

	/* These might be no-ops, see above. */
	if ((probe = ixj_probe_isapnp(&cnt)) < 0) {
		return probe;
	}
	if ((probe = ixj_probe_isa(&cnt)) < 0) {
		return probe;
	}
	if ((probe = ixj_probe_pci(&cnt)) < 0) {
		return probe;
	}
	printk(KERN_INFO "ixj driver initialized.\n");
	create_proc_read_entry ("ixj", 0, NULL, ixj_read_proc, NULL);
	return probe;
}

module_init(ixj_init);
module_exit(ixj_exit);

static void DAA_Coeff_US(IXJ *j)
{
	int i;

	j->daa_country = DAA_US;
	/*----------------------------------------------- */
	/* CAO */
	for (i = 0; i < ALISDAA_CALLERID_SIZE; i++) {
		j->m_DAAShadowRegs.CAO_REGS.CAO.CallerID[i] = 0;
	}

/* Bytes for IM-filter part 1 (04): 0E,32,E2,2F,C2,5A,C0,00 */
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[7] = 0x03;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[6] = 0x4B;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[5] = 0x5D;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[4] = 0xCD;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[3] = 0x24;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[2] = 0xC5;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[1] = 0xA0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[0] = 0x00;
/* Bytes for IM-filter part 2 (05): 72,85,00,0E,2B,3A,D0,08 */
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[7] = 0x71;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[6] = 0x1A;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[5] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[4] = 0x0A;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[3] = 0xB5;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[2] = 0x33;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[1] = 0xE0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[0] = 0x08;
/* Bytes for FRX-filter       (08): 03,8F,48,F2,8F,48,70,08 */
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[7] = 0x05;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[6] = 0xA3;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[5] = 0x72;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[4] = 0x34;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[3] = 0x3F;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[2] = 0x3B;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[1] = 0x30;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[0] = 0x08;
/* Bytes for FRR-filter       (07): 04,8F,38,7F,9B,EA,B0,08 */
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[7] = 0x05;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[6] = 0x87;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[5] = 0xF9;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[4] = 0x3E;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[2] = 0xDA;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[1] = 0xB0;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[0] = 0x08;
/* Bytes for AX-filter        (0A): 16,55,DD,CA */
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[3] = 0x41;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[2] = 0xB5;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[1] = 0xDD;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[0] = 0xCA;
/* Bytes for AR-filter        (09): 52,D3,11,42 */
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[3] = 0x25;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[2] = 0xC7;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[1] = 0x10;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[0] = 0xD6;
/* Bytes for TH-filter part 1 (00): 00,42,48,81,B3,80,00,98 */
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[6] = 0x42;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[5] = 0x48;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[4] = 0x81;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[3] = 0xA5;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[2] = 0x80;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[0] = 0x98;
/* Bytes for TH-filter part 2 (01): 02,F2,33,A0,68,AB,8A,AD */
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[7] = 0x02;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[6] = 0xA2;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[5] = 0x2B;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[4] = 0xB0;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[3] = 0xE8;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[2] = 0xAB;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[1] = 0x81;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[0] = 0xCC;
/* Bytes for TH-filter part 3 (02): 00,88,DA,54,A4,BA,2D,BB */
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[6] = 0x88;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[5] = 0xD2;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[4] = 0x24;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[3] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[2] = 0xA9;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[1] = 0x3B;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[0] = 0xA6;
/* ;  (10K, 0.68uF) */
	/*  */
	/* Bytes for Ringing part 1 (03):1B,3B,9B,BA,D4,1C,B3,23 */
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[7] = 0x1B;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[6] = 0x3C;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[5] = 0x93;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[4] = 0x3A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[2] = 0x12;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[1] = 0xA3;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[0] = 0x23;
	/* Bytes for Ringing part 2 (06):13,42,A6,BA,D4,73,CA,D5 */
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[7] = 0x12;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[6] = 0xA2;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[5] = 0xA6;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[4] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[2] = 0x7A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[1] = 0x0A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[0] = 0xD5;

	/* Levelmetering Ringing        (0D):B2,45,0F,8E       */
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[3] = 0xAA;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[2] = 0x35;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[1] = 0x0F;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[0] = 0x8E;

	/* Bytes for Ringing part 1 (03):1B,3B,9B,BA,D4,1C,B3,23 */
/*	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[7] = 0x1C; */
/*	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[6] = 0xB3; */
/*	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[5] = 0xAB; */
/*	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[4] = 0xAB; */
/*	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[3] = 0x54; */
/*	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[2] = 0x2D; */
/*	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[1] = 0x62; */
/*	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[0] = 0x2D; */
	/* Bytes for Ringing part 2 (06):13,42,A6,BA,D4,73,CA,D5 */ 
/*	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[7] = 0x2D; */
/*	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[6] = 0x62; */
/*	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[5] = 0xA6; */
/*	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[4] = 0xBB; */
/*	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[3] = 0x2A; */
/*	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[2] = 0x7D; */
/*	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[1] = 0x0A; */
/*	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[0] = 0xD4; */
/* */
	/* Levelmetering Ringing        (0D):B2,45,0F,8E       */
/*	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[3] = 0xAA; */
/*	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[2] = 0x05; */
/*	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[1] = 0x0F; */
/*	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[0] = 0x8E; */

	/* Caller ID 1st Tone           (0E):CA,0E,CA,09,99,99,99,99 */
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[7] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[6] = 0x0E;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[5] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[4] = 0x09;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[3] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[2] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[1] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[0] = 0x99;
/* Caller ID 2nd Tone           (0F):FD,B5,BA,07,DA,00,00,00 */
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[7] = 0xFD;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[6] = 0xB5;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[5] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[4] = 0x07;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[3] = 0xDA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[2] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[0] = 0x00;
/*  */
	/* ;CR Registers */
	/* Config. Reg. 0 (filters)       (cr0):FE ; CLK gen. by crystal */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg = 0xFF;
/* Config. Reg. 1 (dialing)       (cr1):05 */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr1.reg = 0x05;
/* Config. Reg. 2 (caller ID)     (cr2):04 */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr2.reg = 0x04;
/* Config. Reg. 3 (testloops)     (cr3):03 ; SEL Bit==0, HP-disabled */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr3.reg = 0x00;
/* Config. Reg. 4 (analog gain)   (cr4):02 */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.reg = 0x02;
	/* Config. Reg. 5 (Version)       (cr5):02 */
	/* Config. Reg. 6 (Reserved)      (cr6):00 */
	/* Config. Reg. 7 (Reserved)      (cr7):00 */
	/*  */
	/* ;xr Registers */
	/* Ext. Reg. 0 (Interrupt Reg.)   (xr0):02 */

	j->m_DAAShadowRegs.XOP_xr0_W.reg = 0x02;	/* SO_1 set to '1' because it is inverted. */
	/* Ext. Reg. 1 (Interrupt enable) (xr1):3C Cadence, RING, Caller ID, VDD_OK */

	j->m_DAAShadowRegs.XOP_REGS.XOP.xr1.reg = 0x3C;
/* Ext. Reg. 2 (Cadence Time Out) (xr2):7D */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr2.reg = 0x7D;
/* Ext. Reg. 3 (DC Char)          (xr3):32 ; B-Filter Off == 1 */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr3.reg = 0x3B;		/*0x32; */
	/* Ext. Reg. 4 (Cadence)          (xr4):00 */

	j->m_DAAShadowRegs.XOP_REGS.XOP.xr4.reg = 0x00;
/* Ext. Reg. 5 (Ring timer)       (xr5):22 */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr5.reg = 0x22;
/* Ext. Reg. 6 (Power State)      (xr6):00 */
	j->m_DAAShadowRegs.XOP_xr6_W.reg = 0x00;
/* Ext. Reg. 7 (Vdd)              (xr7):40 */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr7.reg = 0x40;		/* 0x40 ??? Should it be 0x00? */
	/*  */
	/* DTMF Tone 1                     (0B): 11,B3,5A,2C ;   697 Hz   */
	/*                                       12,33,5A,C3 ;  770 Hz   */
	/*                                       13,3C,5B,32 ;  852 Hz   */
	/*                                       1D,1B,5C,CC ;  941 Hz   */

	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[3] = 0x11;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[2] = 0xB3;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[1] = 0x5A;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[0] = 0x2C;
/* DTMF Tone 2                     (0C): 32,32,52,B3 ;  1209 Hz   */
	/*                                       EC,1D,52,22 ;  1336 Hz   */
	/*                                       AA,AC,51,D2 ;  1477 Hz   */
	/*                                       9B,3B,51,25 ;  1633 Hz   */
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[2] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[1] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[0] = 0xB3;
}

static void DAA_Coeff_UK(IXJ *j)
{
	int i;

	j->daa_country = DAA_UK;
	/*----------------------------------------------- */
	/* CAO */
	for (i = 0; i < ALISDAA_CALLERID_SIZE; i++) {
		j->m_DAAShadowRegs.CAO_REGS.CAO.CallerID[i] = 0;
	}

/*  Bytes for IM-filter part 1 (04): 00,C2,BB,A8,CB,81,A0,00 */
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[6] = 0xC2;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[5] = 0xBB;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[4] = 0xA8;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[3] = 0xCB;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[2] = 0x81;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[1] = 0xA0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[0] = 0x00;
/* Bytes for IM-filter part 2 (05): 40,00,00,0A,A4,33,E0,08 */
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[7] = 0x40;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[6] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[5] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[4] = 0x0A;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[3] = 0xA4;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[2] = 0x33;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[1] = 0xE0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[0] = 0x08;
/* Bytes for FRX-filter       (08): 07,9B,ED,24,B2,A2,A0,08 */
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[7] = 0x07;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[6] = 0x9B;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[5] = 0xED;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[4] = 0x24;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[3] = 0xB2;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[2] = 0xA2;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[1] = 0xA0;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[0] = 0x08;
/* Bytes for FRR-filter       (07): 0F,92,F2,B2,87,D2,30,08 */
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[7] = 0x0F;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[6] = 0x92;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[5] = 0xF2;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[4] = 0xB2;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[3] = 0x87;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[2] = 0xD2;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[1] = 0x30;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[0] = 0x08;
/* Bytes for AX-filter        (0A): 1B,A5,DD,CA */
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[3] = 0x1B;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[2] = 0xA5;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[1] = 0xDD;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[0] = 0xCA;
/* Bytes for AR-filter        (09): E2,27,10,D6 */
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[3] = 0xE2;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[2] = 0x27;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[1] = 0x10;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[0] = 0xD6;
/* Bytes for TH-filter part 1 (00): 80,2D,38,8B,D0,00,00,98 */
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[7] = 0x80;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[6] = 0x2D;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[5] = 0x38;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[4] = 0x8B;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[3] = 0xD0;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[2] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[0] = 0x98;
/* Bytes for TH-filter part 2 (01): 02,5A,53,F0,0B,5F,84,D4 */
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[7] = 0x02;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[6] = 0x5A;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[5] = 0x53;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[4] = 0xF0;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[3] = 0x0B;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[2] = 0x5F;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[1] = 0x84;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[0] = 0xD4;
/* Bytes for TH-filter part 3 (02): 00,88,6A,A4,8F,52,F5,32 */
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[6] = 0x88;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[5] = 0x6A;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[4] = 0xA4;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[3] = 0x8F;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[2] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[1] = 0xF5;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[0] = 0x32;
/* ; idle */
	/* Bytes for Ringing part 1 (03):1B,3C,93,3A,22,12,A3,23 */
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[7] = 0x1B;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[6] = 0x3C;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[5] = 0x93;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[4] = 0x3A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[2] = 0x12;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[1] = 0xA3;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[0] = 0x23;
/* Bytes for Ringing part 2 (06):12,A2,A6,BA,22,7A,0A,D5 */
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[7] = 0x12;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[6] = 0xA2;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[5] = 0xA6;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[4] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[2] = 0x7A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[1] = 0x0A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[0] = 0xD5;
/* Levelmetering Ringing           (0D):AA,35,0F,8E     ; 25Hz 30V less possible? */
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[3] = 0xAA;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[2] = 0x35;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[1] = 0x0F;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[0] = 0x8E;
/* Caller ID 1st Tone              (0E):CA,0E,CA,09,99,99,99,99 */
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[7] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[6] = 0x0E;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[5] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[4] = 0x09;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[3] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[2] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[1] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[0] = 0x99;
/* Caller ID 2nd Tone              (0F):FD,B5,BA,07,DA,00,00,00 */
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[7] = 0xFD;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[6] = 0xB5;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[5] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[4] = 0x07;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[3] = 0xDA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[2] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[0] = 0x00;
/* ;CR Registers */
	/* Config. Reg. 0 (filters)        (cr0):FF */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg = 0xFF;
/* Config. Reg. 1 (dialing)        (cr1):05 */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr1.reg = 0x05;
/* Config. Reg. 2 (caller ID)      (cr2):04 */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr2.reg = 0x04;
/* Config. Reg. 3 (testloops)      (cr3):00        ;  */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr3.reg = 0x00;
/* Config. Reg. 4 (analog gain)    (cr4):02 */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.reg = 0x02;
	/* Config. Reg. 5 (Version)        (cr5):02 */
	/* Config. Reg. 6 (Reserved)       (cr6):00 */
	/* Config. Reg. 7 (Reserved)       (cr7):00 */
	/* ;xr Registers */
	/* Ext. Reg. 0 (Interrupt Reg.)    (xr0):02 */

	j->m_DAAShadowRegs.XOP_xr0_W.reg = 0x02;	/* SO_1 set to '1' because it is inverted. */
	/* Ext. Reg. 1 (Interrupt enable)  (xr1):1C */

	j->m_DAAShadowRegs.XOP_REGS.XOP.xr1.reg = 0x1C;		/* RING, Caller ID, VDD_OK */
	/* Ext. Reg. 2 (Cadence Time Out)  (xr2):7D */

	j->m_DAAShadowRegs.XOP_REGS.XOP.xr2.reg = 0x7D;
/* Ext. Reg. 3 (DC Char)           (xr3):36        ;  */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr3.reg = 0x36;
/* Ext. Reg. 4 (Cadence)           (xr4):00 */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr4.reg = 0x00;
/* Ext. Reg. 5 (Ring timer)        (xr5):22 */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr5.reg = 0x22;
/* Ext. Reg. 6 (Power State)       (xr6):00 */
	j->m_DAAShadowRegs.XOP_xr6_W.reg = 0x00;
/* Ext. Reg. 7 (Vdd)               (xr7):46 */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr7.reg = 0x46;		/* 0x46 ??? Should it be 0x00? */
	/* DTMF Tone 1                     (0B): 11,B3,5A,2C    ;   697 Hz   */
	/*                                       12,33,5A,C3    ;  770 Hz   */
	/*                                       13,3C,5B,32    ;  852 Hz   */
	/*                                       1D,1B,5C,CC    ;  941 Hz   */

	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[3] = 0x11;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[2] = 0xB3;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[1] = 0x5A;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[0] = 0x2C;
/* DTMF Tone 2                     (0C): 32,32,52,B3    ;  1209 Hz   */
	/*                                       EC,1D,52,22    ;  1336 Hz   */
	/*                                       AA,AC,51,D2    ;  1477 Hz   */
	/*                                       9B,3B,51,25    ;  1633 Hz   */
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[2] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[1] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[0] = 0xB3;
}


static void DAA_Coeff_France(IXJ *j)
{
	int i;

	j->daa_country = DAA_FRANCE;
	/*----------------------------------------------- */
	/* CAO */
	for (i = 0; i < ALISDAA_CALLERID_SIZE; i++) {
		j->m_DAAShadowRegs.CAO_REGS.CAO.CallerID[i] = 0;
	}

/* Bytes for IM-filter part 1 (04): 02,A2,43,2C,22,AF,A0,00 */
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[7] = 0x02;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[6] = 0xA2;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[5] = 0x43;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[4] = 0x2C;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[2] = 0xAF;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[1] = 0xA0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[0] = 0x00;
/* Bytes for IM-filter part 2 (05): 67,CE,00,0C,22,33,E0,08 */
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[7] = 0x67;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[6] = 0xCE;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[5] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[4] = 0x2C;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[2] = 0x33;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[1] = 0xE0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[0] = 0x08;
/* Bytes for FRX-filter       (08): 07,9A,28,F6,23,4A,B0,08 */
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[7] = 0x07;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[6] = 0x9A;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[5] = 0x28;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[4] = 0xF6;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[3] = 0x23;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[2] = 0x4A;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[1] = 0xB0;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[0] = 0x08;
/* Bytes for FRR-filter       (07): 03,8F,F9,2F,9E,FA,20,08 */
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[7] = 0x03;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[6] = 0x8F;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[5] = 0xF9;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[4] = 0x2F;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[3] = 0x9E;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[2] = 0xFA;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[1] = 0x20;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[0] = 0x08;
/* Bytes for AX-filter        (0A): 16,B5,DD,CA */
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[3] = 0x16;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[2] = 0xB5;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[1] = 0xDD;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[0] = 0xCA;
/* Bytes for AR-filter        (09): 52,C7,10,D6 */
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[3] = 0xE2;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[2] = 0xC7;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[1] = 0x10;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[0] = 0xD6;
/* Bytes for TH-filter part 1 (00): 00,42,48,81,A6,80,00,98 */
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[6] = 0x42;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[5] = 0x48;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[4] = 0x81;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[3] = 0xA6;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[2] = 0x80;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[0] = 0x98;
/* Bytes for TH-filter part 2 (01): 02,AC,2A,30,78,AC,8A,2C */
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[7] = 0x02;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[6] = 0xAC;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[5] = 0x2A;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[4] = 0x30;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[3] = 0x78;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[2] = 0xAC;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[1] = 0x8A;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[0] = 0x2C;
/* Bytes for TH-filter part 3 (02): 00,88,DA,A5,22,BA,2C,45 */
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[6] = 0x88;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[5] = 0xDA;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[4] = 0xA5;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[2] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[1] = 0x2C;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[0] = 0x45;
/* ; idle */
	/* Bytes for Ringing part 1 (03):1B,3C,93,3A,22,12,A3,23 */
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[7] = 0x1B;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[6] = 0x3C;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[5] = 0x93;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[4] = 0x3A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[2] = 0x12;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[1] = 0xA3;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[0] = 0x23;
/* Bytes for Ringing part 2 (06):12,A2,A6,BA,22,7A,0A,D5 */
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[7] = 0x12;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[6] = 0xA2;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[5] = 0xA6;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[4] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[2] = 0x7A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[1] = 0x0A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[0] = 0xD5;
/* Levelmetering Ringing           (0D):32,45,B5,84     ; 50Hz 20V */
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[2] = 0x45;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[1] = 0xB5;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[0] = 0x84;
/* Caller ID 1st Tone              (0E):CA,0E,CA,09,99,99,99,99 */
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[7] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[6] = 0x0E;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[5] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[4] = 0x09;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[3] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[2] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[1] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[0] = 0x99;
/* Caller ID 2nd Tone              (0F):FD,B5,BA,07,DA,00,00,00 */
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[7] = 0xFD;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[6] = 0xB5;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[5] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[4] = 0x07;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[3] = 0xDA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[2] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[0] = 0x00;
/* ;CR Registers */
	/* Config. Reg. 0 (filters)        (cr0):FF */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg = 0xFF;
/* Config. Reg. 1 (dialing)        (cr1):05 */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr1.reg = 0x05;
/* Config. Reg. 2 (caller ID)      (cr2):04 */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr2.reg = 0x04;
/* Config. Reg. 3 (testloops)      (cr3):00        ;  */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr3.reg = 0x00;
/* Config. Reg. 4 (analog gain)    (cr4):02 */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.reg = 0x02;
	/* Config. Reg. 5 (Version)        (cr5):02 */
	/* Config. Reg. 6 (Reserved)       (cr6):00 */
	/* Config. Reg. 7 (Reserved)       (cr7):00 */
	/* ;xr Registers */
	/* Ext. Reg. 0 (Interrupt Reg.)    (xr0):02 */

	j->m_DAAShadowRegs.XOP_xr0_W.reg = 0x02;	/* SO_1 set to '1' because it is inverted. */
	/* Ext. Reg. 1 (Interrupt enable)  (xr1):1C */

	j->m_DAAShadowRegs.XOP_REGS.XOP.xr1.reg = 0x1C;		/* RING, Caller ID, VDD_OK */
	/* Ext. Reg. 2 (Cadence Time Out)  (xr2):7D */

	j->m_DAAShadowRegs.XOP_REGS.XOP.xr2.reg = 0x7D;
/* Ext. Reg. 3 (DC Char)           (xr3):36        ;  */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr3.reg = 0x36;
/* Ext. Reg. 4 (Cadence)           (xr4):00 */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr4.reg = 0x00;
/* Ext. Reg. 5 (Ring timer)        (xr5):22 */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr5.reg = 0x22;
/* Ext. Reg. 6 (Power State)       (xr6):00 */
	j->m_DAAShadowRegs.XOP_xr6_W.reg = 0x00;
/* Ext. Reg. 7 (Vdd)               (xr7):46 */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr7.reg = 0x46;		/* 0x46 ??? Should it be 0x00? */
	/* DTMF Tone 1                     (0B): 11,B3,5A,2C    ;   697 Hz   */
	/*                                       12,33,5A,C3    ;  770 Hz   */
	/*                                       13,3C,5B,32    ;  852 Hz   */
	/*                                       1D,1B,5C,CC    ;  941 Hz   */

	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[3] = 0x11;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[2] = 0xB3;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[1] = 0x5A;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[0] = 0x2C;
/* DTMF Tone 2                     (0C): 32,32,52,B3    ;  1209 Hz   */
	/*                                       EC,1D,52,22    ;  1336 Hz   */
	/*                                       AA,AC,51,D2    ;  1477 Hz   */
	/*                                       9B,3B,51,25    ;  1633 Hz   */
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[2] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[1] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[0] = 0xB3;
}


static void DAA_Coeff_Germany(IXJ *j)
{
	int i;

	j->daa_country = DAA_GERMANY;
	/*----------------------------------------------- */
	/* CAO */
	for (i = 0; i < ALISDAA_CALLERID_SIZE; i++) {
		j->m_DAAShadowRegs.CAO_REGS.CAO.CallerID[i] = 0;
	}

/* Bytes for IM-filter part 1 (04): 00,CE,BB,B8,D2,81,B0,00 */
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[6] = 0xCE;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[5] = 0xBB;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[4] = 0xB8;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[3] = 0xD2;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[2] = 0x81;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[1] = 0xB0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[0] = 0x00;
/* Bytes for IM-filter part 2 (05): 45,8F,00,0C,D2,3A,D0,08 */
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[7] = 0x45;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[6] = 0x8F;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[5] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[4] = 0x0C;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[3] = 0xD2;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[2] = 0x3A;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[1] = 0xD0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[0] = 0x08;
/* Bytes for FRX-filter       (08): 07,AA,E2,34,24,89,20,08 */
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[7] = 0x07;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[6] = 0xAA;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[5] = 0xE2;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[4] = 0x34;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[3] = 0x24;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[2] = 0x89;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[1] = 0x20;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[0] = 0x08;
/* Bytes for FRR-filter       (07): 02,87,FA,37,9A,CA,B0,08 */
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[7] = 0x02;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[6] = 0x87;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[5] = 0xFA;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[4] = 0x37;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[3] = 0x9A;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[2] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[1] = 0xB0;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[0] = 0x08;
/* Bytes for AX-filter        (0A): 72,D5,DD,CA */
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[3] = 0x72;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[2] = 0xD5;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[1] = 0xDD;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[0] = 0xCA;
/* Bytes for AR-filter        (09): 72,42,13,4B */
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[3] = 0x72;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[2] = 0x42;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[1] = 0x13;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[0] = 0x4B;
/* Bytes for TH-filter part 1 (00): 80,52,48,81,AD,80,00,98 */
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[7] = 0x80;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[6] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[5] = 0x48;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[4] = 0x81;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[3] = 0xAD;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[2] = 0x80;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[0] = 0x98;
/* Bytes for TH-filter part 2 (01): 02,42,5A,20,E8,1A,81,27 */
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[7] = 0x02;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[6] = 0x42;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[5] = 0x5A;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[4] = 0x20;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[3] = 0xE8;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[2] = 0x1A;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[1] = 0x81;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[0] = 0x27;
/* Bytes for TH-filter part 3 (02): 00,88,63,26,BD,4B,A3,C2 */
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[6] = 0x88;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[5] = 0x63;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[4] = 0x26;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[3] = 0xBD;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[2] = 0x4B;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[1] = 0xA3;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[0] = 0xC2;
/* ;  (10K, 0.68uF) */
	/* Bytes for Ringing part 1 (03):1B,3B,9B,BA,D4,1C,B3,23 */
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[7] = 0x1B;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[6] = 0x3B;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[5] = 0x9B;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[4] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[3] = 0xD4;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[2] = 0x1C;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[1] = 0xB3;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[0] = 0x23;
/* Bytes for Ringing part 2 (06):13,42,A6,BA,D4,73,CA,D5 */
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[7] = 0x13;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[6] = 0x42;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[5] = 0xA6;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[4] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[3] = 0xD4;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[2] = 0x73;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[1] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[0] = 0xD5;
/* Levelmetering Ringing        (0D):B2,45,0F,8E       */
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[3] = 0xB2;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[2] = 0x45;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[1] = 0x0F;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[0] = 0x8E;
/* Caller ID 1st Tone           (0E):CA,0E,CA,09,99,99,99,99 */
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[7] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[6] = 0x0E;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[5] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[4] = 0x09;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[3] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[2] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[1] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[0] = 0x99;
/* Caller ID 2nd Tone           (0F):FD,B5,BA,07,DA,00,00,00 */
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[7] = 0xFD;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[6] = 0xB5;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[5] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[4] = 0x07;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[3] = 0xDA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[2] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[0] = 0x00;
/* ;CR Registers */
	/* Config. Reg. 0 (filters)        (cr0):FF ; all Filters enabled, CLK from ext. source */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg = 0xFF;
/* Config. Reg. 1 (dialing)        (cr1):05 ; Manual Ring, Ring metering enabled */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr1.reg = 0x05;
/* Config. Reg. 2 (caller ID)      (cr2):04 ; Analog Gain 0dB, FSC internal */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr2.reg = 0x04;
/* Config. Reg. 3 (testloops)      (cr3):00 ; SEL Bit==0, HP-enabled */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr3.reg = 0x00;
/* Config. Reg. 4 (analog gain)    (cr4):02 */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.reg = 0x02;
	/* Config. Reg. 5 (Version)        (cr5):02 */
	/* Config. Reg. 6 (Reserved)       (cr6):00 */
	/* Config. Reg. 7 (Reserved)       (cr7):00 */
	/* ;xr Registers */
	/* Ext. Reg. 0 (Interrupt Reg.)    (xr0):02 */

	j->m_DAAShadowRegs.XOP_xr0_W.reg = 0x02;	/* SO_1 set to '1' because it is inverted. */
	/* Ext. Reg. 1 (Interrupt enable)  (xr1):1C ; Ring, CID, VDDOK Interrupts enabled */

	j->m_DAAShadowRegs.XOP_REGS.XOP.xr1.reg = 0x1C;		/* RING, Caller ID, VDD_OK */
	/* Ext. Reg. 2 (Cadence Time Out)  (xr2):7D */

	j->m_DAAShadowRegs.XOP_REGS.XOP.xr2.reg = 0x7D;
/* Ext. Reg. 3 (DC Char)           (xr3):32 ; B-Filter Off==1, U0=3.5V, R=200Ohm */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr3.reg = 0x32;
/* Ext. Reg. 4 (Cadence)           (xr4):00 */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr4.reg = 0x00;
/* Ext. Reg. 5 (Ring timer)        (xr5):22 */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr5.reg = 0x22;
/* Ext. Reg. 6 (Power State)       (xr6):00 */
	j->m_DAAShadowRegs.XOP_xr6_W.reg = 0x00;
/* Ext. Reg. 7 (Vdd)               (xr7):40 ; VDD=4.25 V */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr7.reg = 0x40;		/* 0x40 ??? Should it be 0x00? */
	/* DTMF Tone 1                     (0B): 11,B3,5A,2C    ;   697 Hz   */
	/*                                       12,33,5A,C3    ;  770 Hz   */
	/*                                       13,3C,5B,32    ;  852 Hz   */
	/*                                       1D,1B,5C,CC    ;  941 Hz   */

	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[3] = 0x11;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[2] = 0xB3;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[1] = 0x5A;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[0] = 0x2C;
/* DTMF Tone 2                     (0C): 32,32,52,B3    ;  1209 Hz   */
	/*                                       EC,1D,52,22    ;  1336 Hz   */
	/*                                       AA,AC,51,D2    ;  1477 Hz   */
	/*                                       9B,3B,51,25    ;  1633 Hz   */
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[2] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[1] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[0] = 0xB3;
}


static void DAA_Coeff_Australia(IXJ *j)
{
	int i;

	j->daa_country = DAA_AUSTRALIA;
	/*----------------------------------------------- */
	/* CAO */
	for (i = 0; i < ALISDAA_CALLERID_SIZE; i++) {
		j->m_DAAShadowRegs.CAO_REGS.CAO.CallerID[i] = 0;
	}

/* Bytes for IM-filter part 1 (04): 00,A3,AA,28,B3,82,D0,00 */
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[6] = 0xA3;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[5] = 0xAA;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[4] = 0x28;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[3] = 0xB3;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[2] = 0x82;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[1] = 0xD0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[0] = 0x00;
/* Bytes for IM-filter part 2 (05): 70,96,00,09,32,6B,C0,08 */
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[7] = 0x70;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[6] = 0x96;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[5] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[4] = 0x09;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[2] = 0x6B;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[1] = 0xC0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[0] = 0x08;
/* Bytes for FRX-filter       (08): 07,96,E2,34,32,9B,30,08 */
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[7] = 0x07;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[6] = 0x96;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[5] = 0xE2;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[4] = 0x34;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[2] = 0x9B;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[1] = 0x30;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[0] = 0x08;
/* Bytes for FRR-filter       (07): 0F,9A,E9,2F,22,CC,A0,08 */
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[7] = 0x0F;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[6] = 0x9A;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[5] = 0xE9;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[4] = 0x2F;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[2] = 0xCC;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[1] = 0xA0;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[0] = 0x08;
/* Bytes for AX-filter        (0A): CB,45,DD,CA */
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[3] = 0xCB;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[2] = 0x45;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[1] = 0xDD;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[0] = 0xCA;
/* Bytes for AR-filter        (09): 1B,67,10,D6 */
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[3] = 0x1B;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[2] = 0x67;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[1] = 0x10;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[0] = 0xD6;
/* Bytes for TH-filter part 1 (00): 80,52,48,81,AF,80,00,98 */
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[7] = 0x80;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[6] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[5] = 0x48;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[4] = 0x81;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[3] = 0xAF;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[2] = 0x80;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[0] = 0x98;
/* Bytes for TH-filter part 2 (01): 02,DB,52,B0,38,01,82,AC */
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[7] = 0x02;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[6] = 0xDB;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[5] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[4] = 0xB0;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[3] = 0x38;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[2] = 0x01;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[1] = 0x82;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[0] = 0xAC;
/* Bytes for TH-filter part 3 (02): 00,88,4A,3E,2C,3B,24,46 */
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[6] = 0x88;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[5] = 0x4A;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[4] = 0x3E;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[3] = 0x2C;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[2] = 0x3B;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[1] = 0x24;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[0] = 0x46;
/* ;  idle */
	/* Bytes for Ringing part 1 (03):1B,3C,93,3A,22,12,A3,23 */
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[7] = 0x1B;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[6] = 0x3C;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[5] = 0x93;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[4] = 0x3A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[2] = 0x12;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[1] = 0xA3;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[0] = 0x23;
/* Bytes for Ringing part 2 (06):12,A2,A6,BA,22,7A,0A,D5 */
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[7] = 0x12;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[6] = 0xA2;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[5] = 0xA6;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[4] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[2] = 0x7A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[1] = 0x0A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[0] = 0xD5;
/* Levelmetering Ringing           (0D):32,45,B5,84   ; 50Hz 20V */
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[2] = 0x45;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[1] = 0xB5;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[0] = 0x84;
/* Caller ID 1st Tone              (0E):CA,0E,CA,09,99,99,99,99 */
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[7] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[6] = 0x0E;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[5] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[4] = 0x09;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[3] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[2] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[1] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[0] = 0x99;
/* Caller ID 2nd Tone              (0F):FD,B5,BA,07,DA,00,00,00 */
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[7] = 0xFD;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[6] = 0xB5;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[5] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[4] = 0x07;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[3] = 0xDA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[2] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[0] = 0x00;
/* ;CR Registers */
	/* Config. Reg. 0 (filters)        (cr0):FF */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg = 0xFF;
/* Config. Reg. 1 (dialing)        (cr1):05 */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr1.reg = 0x05;
/* Config. Reg. 2 (caller ID)      (cr2):04 */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr2.reg = 0x04;
/* Config. Reg. 3 (testloops)      (cr3):00        ;  */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr3.reg = 0x00;
/* Config. Reg. 4 (analog gain)    (cr4):02 */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.reg = 0x02;
	/* Config. Reg. 5 (Version)        (cr5):02 */
	/* Config. Reg. 6 (Reserved)       (cr6):00 */
	/* Config. Reg. 7 (Reserved)       (cr7):00 */
	/* ;xr Registers */
	/* Ext. Reg. 0 (Interrupt Reg.)    (xr0):02 */

	j->m_DAAShadowRegs.XOP_xr0_W.reg = 0x02;	/* SO_1 set to '1' because it is inverted. */
	/* Ext. Reg. 1 (Interrupt enable)  (xr1):1C */

	j->m_DAAShadowRegs.XOP_REGS.XOP.xr1.reg = 0x1C;		/* RING, Caller ID, VDD_OK */
	/* Ext. Reg. 2 (Cadence Time Out)  (xr2):7D */

	j->m_DAAShadowRegs.XOP_REGS.XOP.xr2.reg = 0x7D;
/* Ext. Reg. 3 (DC Char)           (xr3):2B      ;  */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr3.reg = 0x2B;
/* Ext. Reg. 4 (Cadence)           (xr4):00 */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr4.reg = 0x00;
/* Ext. Reg. 5 (Ring timer)        (xr5):22 */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr5.reg = 0x22;
/* Ext. Reg. 6 (Power State)       (xr6):00 */
	j->m_DAAShadowRegs.XOP_xr6_W.reg = 0x00;
/* Ext. Reg. 7 (Vdd)               (xr7):40 */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr7.reg = 0x40;		/* 0x40 ??? Should it be 0x00? */

	/* DTMF Tone 1                     (0B): 11,B3,5A,2C    ;  697 Hz   */
	/*                                       12,33,5A,C3    ;  770 Hz   */
	/*                                       13,3C,5B,32    ;  852 Hz   */
	/*                                       1D,1B,5C,CC    ;  941 Hz   */
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[3] = 0x11;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[2] = 0xB3;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[1] = 0x5A;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[0] = 0x2C;

	/* DTMF Tone 2                     (0C): 32,32,52,B3    ;  1209 Hz   */
	/*                                       EC,1D,52,22    ;  1336 Hz   */
	/*                                       AA,AC,51,D2    ;  1477 Hz   */
	/*                                       9B,3B,51,25    ;  1633 Hz   */
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[2] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[1] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[0] = 0xB3;
}

static void DAA_Coeff_Japan(IXJ *j)
{
	int i;

	j->daa_country = DAA_JAPAN;
	/*----------------------------------------------- */
	/* CAO */
	for (i = 0; i < ALISDAA_CALLERID_SIZE; i++) {
		j->m_DAAShadowRegs.CAO_REGS.CAO.CallerID[i] = 0;
	}

/* Bytes for IM-filter part 1 (04): 06,BD,E2,2D,BA,F9,A0,00 */
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[7] = 0x06;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[6] = 0xBD;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[5] = 0xE2;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[4] = 0x2D;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[3] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[2] = 0xF9;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[1] = 0xA0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[0] = 0x00;
/* Bytes for IM-filter part 2 (05): 6F,F7,00,0E,34,33,E0,08 */
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[7] = 0x6F;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[6] = 0xF7;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[5] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[4] = 0x0E;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[3] = 0x34;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[2] = 0x33;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[1] = 0xE0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[0] = 0x08;
/* Bytes for FRX-filter       (08): 02,8F,68,77,9C,58,F0,08 */
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[7] = 0x02;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[6] = 0x8F;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[5] = 0x68;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[4] = 0x77;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[3] = 0x9C;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[2] = 0x58;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[1] = 0xF0;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[0] = 0x08;
/* Bytes for FRR-filter       (07): 03,8F,38,73,87,EA,20,08 */
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[7] = 0x03;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[6] = 0x8F;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[5] = 0x38;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[4] = 0x73;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[3] = 0x87;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[2] = 0xEA;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[1] = 0x20;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[0] = 0x08;
/* Bytes for AX-filter        (0A): 51,C5,DD,CA */
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[3] = 0x51;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[2] = 0xC5;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[1] = 0xDD;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[0] = 0xCA;
/* Bytes for AR-filter        (09): 25,A7,10,D6 */
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[3] = 0x25;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[2] = 0xA7;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[1] = 0x10;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[0] = 0xD6;
/* Bytes for TH-filter part 1 (00): 00,42,48,81,AE,80,00,98 */
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[6] = 0x42;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[5] = 0x48;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[4] = 0x81;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[3] = 0xAE;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[2] = 0x80;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[0] = 0x98;
/* Bytes for TH-filter part 2 (01): 02,AB,2A,20,99,5B,89,28 */
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[7] = 0x02;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[6] = 0xAB;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[5] = 0x2A;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[4] = 0x20;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[3] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[2] = 0x5B;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[1] = 0x89;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[0] = 0x28;
/* Bytes for TH-filter part 3 (02): 00,88,DA,25,34,C5,4C,BA */
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[6] = 0x88;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[5] = 0xDA;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[4] = 0x25;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[3] = 0x34;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[2] = 0xC5;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[1] = 0x4C;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[0] = 0xBA;
/* ;  idle */
	/* Bytes for Ringing part 1 (03):1B,3C,93,3A,22,12,A3,23 */
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[7] = 0x1B;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[6] = 0x3C;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[5] = 0x93;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[4] = 0x3A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[2] = 0x12;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[1] = 0xA3;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[0] = 0x23;
/* Bytes for Ringing part 2 (06):12,A2,A6,BA,22,7A,0A,D5 */
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[7] = 0x12;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[6] = 0xA2;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[5] = 0xA6;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[4] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[2] = 0x7A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[1] = 0x0A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[0] = 0xD5;
/* Levelmetering Ringing           (0D):AA,35,0F,8E    ; 25Hz 30V ????????? */
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[3] = 0xAA;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[2] = 0x35;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[1] = 0x0F;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[0] = 0x8E;
/* Caller ID 1st Tone              (0E):CA,0E,CA,09,99,99,99,99 */
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[7] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[6] = 0x0E;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[5] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[4] = 0x09;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[3] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[2] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[1] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[0] = 0x99;
/* Caller ID 2nd Tone              (0F):FD,B5,BA,07,DA,00,00,00 */
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[7] = 0xFD;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[6] = 0xB5;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[5] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[4] = 0x07;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[3] = 0xDA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[2] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[0] = 0x00;
/* ;CR Registers */
	/* Config. Reg. 0 (filters)        (cr0):FF */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg = 0xFF;
/* Config. Reg. 1 (dialing)        (cr1):05 */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr1.reg = 0x05;
/* Config. Reg. 2 (caller ID)      (cr2):04 */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr2.reg = 0x04;
/* Config. Reg. 3 (testloops)      (cr3):00        ;  */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr3.reg = 0x00;
/* Config. Reg. 4 (analog gain)    (cr4):02 */
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.reg = 0x02;
	/* Config. Reg. 5 (Version)        (cr5):02 */
	/* Config. Reg. 6 (Reserved)       (cr6):00 */
	/* Config. Reg. 7 (Reserved)       (cr7):00 */
	/* ;xr Registers */
	/* Ext. Reg. 0 (Interrupt Reg.)    (xr0):02 */

	j->m_DAAShadowRegs.XOP_xr0_W.reg = 0x02;	/* SO_1 set to '1' because it is inverted. */
	/* Ext. Reg. 1 (Interrupt enable)  (xr1):1C */

	j->m_DAAShadowRegs.XOP_REGS.XOP.xr1.reg = 0x1C;		/* RING, Caller ID, VDD_OK */
	/* Ext. Reg. 2 (Cadence Time Out)  (xr2):7D */

	j->m_DAAShadowRegs.XOP_REGS.XOP.xr2.reg = 0x7D;
/* Ext. Reg. 3 (DC Char)           (xr3):22        ;  */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr3.reg = 0x22;
/* Ext. Reg. 4 (Cadence)           (xr4):00 */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr4.reg = 0x00;
/* Ext. Reg. 5 (Ring timer)        (xr5):22 */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr5.reg = 0x22;
/* Ext. Reg. 6 (Power State)       (xr6):00 */
	j->m_DAAShadowRegs.XOP_xr6_W.reg = 0x00;
/* Ext. Reg. 7 (Vdd)               (xr7):40 */
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr7.reg = 0x40;		/* 0x40 ??? Should it be 0x00? */
	/* DTMF Tone 1                     (0B): 11,B3,5A,2C    ;   697 Hz   */
	/*                                       12,33,5A,C3    ;  770 Hz   */
	/*                                       13,3C,5B,32    ;  852 Hz   */
	/*                                       1D,1B,5C,CC    ;  941 Hz   */

	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[3] = 0x11;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[2] = 0xB3;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[1] = 0x5A;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[0] = 0x2C;
/* DTMF Tone 2                     (0C): 32,32,52,B3    ;  1209 Hz   */
	/*                                       EC,1D,52,22    ;  1336 Hz   */
	/*                                       AA,AC,51,D2    ;  1477 Hz   */
	/*                                       9B,3B,51,25    ;  1633 Hz   */
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[2] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[1] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[0] = 0xB3;
}

static s16 tone_table[][19] =
{
	{			/* f20_50[] 11 */
		32538,		/* A1 = 1.985962 */
		 -32325,	/* A2 = -0.986511 */
		 -343,		/* B2 = -0.010493 */
		 0,		/* B1 = 0 */
		 343,		/* B0 = 0.010493 */
		 32619,		/* A1 = 1.990906 */
		 -32520,	/* A2 = -0.992462 */
		 19179,		/* B2 = 0.585327 */
		 -19178,	/* B1 = -1.170593 */
		 19179,		/* B0 = 0.585327 */
		 32723,		/* A1 = 1.997314 */
		 -32686,	/* A2 = -0.997528 */
		 9973,		/* B2 = 0.304352 */
		 -9955,		/* B1 = -0.607605 */
		 9973,		/* B0 = 0.304352 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f133_200[] 12 */
		32072,		/* A1 = 1.95752 */
		 -31896,	/* A2 = -0.973419 */
		 -435,		/* B2 = -0.013294 */
		 0,		/* B1 = 0 */
		 435,		/* B0 = 0.013294 */
		 32188,		/* A1 = 1.9646 */
		 -32400,	/* A2 = -0.98877 */
		 15139,		/* B2 = 0.462036 */
		 -14882,	/* B1 = -0.908356 */
		 15139,		/* B0 = 0.462036 */
		 32473,		/* A1 = 1.981995 */
		 -32524,	/* A2 = -0.992584 */
		 23200,		/* B2 = 0.708008 */
		 -23113,	/* B1 = -1.410706 */
		 23200,		/* B0 = 0.708008 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f300 13 */
		31769,		/* A1 = -1.939026 */
		 -32584,	/* A2 = 0.994385 */
		 -475,		/* B2 = -0.014522 */
		 0,		/* B1 = 0.000000 */
		 475,		/* B0 = 0.014522 */
		 31789,		/* A1 = -1.940247 */
		 -32679,	/* A2 = 0.997284 */
		 17280,		/* B2 = 0.527344 */
		 -16865,	/* B1 = -1.029358 */
		 17280,		/* B0 = 0.527344 */
		 31841,		/* A1 = -1.943481 */
		 -32681,	/* A2 = 0.997345 */
		 543,		/* B2 = 0.016579 */
		 -525,		/* B1 = -0.032097 */
		 543,		/* B0 = 0.016579 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f300_420[] 14 */
		30750,		/* A1 = 1.876892 */
		 -31212,	/* A2 = -0.952515 */
		 -804,		/* B2 = -0.024541 */
		 0,		/* B1 = 0 */
		 804,		/* B0 = 0.024541 */
		 30686,		/* A1 = 1.872925 */
		 -32145,	/* A2 = -0.980988 */
		 14747,		/* B2 = 0.450043 */
		 -13703,	/* B1 = -0.836395 */
		 14747,		/* B0 = 0.450043 */
		 31651,		/* A1 = 1.931824 */
		 -32321,	/* A2 = -0.986389 */
		 24425,		/* B2 = 0.745422 */
		 -23914,	/* B1 = -1.459595 */
		 24427,		/* B0 = 0.745483 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f330 15 */
		31613,		/* A1 = -1.929565 */
		 -32646,	/* A2 = 0.996277 */
		 -185,		/* B2 = -0.005657 */
		 0,		/* B1 = 0.000000 */
		 185,		/* B0 = 0.005657 */
		 31620,		/* A1 = -1.929932 */
		 -32713,	/* A2 = 0.998352 */
		 19253,		/* B2 = 0.587585 */
		 -18566,	/* B1 = -1.133179 */
		 19253,		/* B0 = 0.587585 */
		 31674,		/* A1 = -1.933228 */
		 -32715,	/* A2 = 0.998413 */
		 2575,		/* B2 = 0.078590 */
		 -2495,		/* B1 = -0.152283 */
		 2575,		/* B0 = 0.078590 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f300_425[] 16 */
		30741,		/* A1 = 1.876282 */
		 -31475,	/* A2 = -0.960541 */
		 -703,		/* B2 = -0.021484 */
		 0,		/* B1 = 0 */
		 703,		/* B0 = 0.021484 */
		 30688,		/* A1 = 1.873047 */
		 -32248,	/* A2 = -0.984161 */
		 14542,		/* B2 = 0.443787 */
		 -13523,	/* B1 = -0.825439 */
		 14542,		/* B0 = 0.443817 */
		 31494,		/* A1 = 1.922302 */
		 -32366,	/* A2 = -0.987762 */
		 21577,		/* B2 = 0.658508 */
		 -21013,	/* B1 = -1.282532 */
		 21577,		/* B0 = 0.658508 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f330_440[] 17 */
		30627,		/* A1 = 1.869324 */
		 -31338,	/* A2 = -0.95636 */
		 -843,		/* B2 = -0.025749 */
		 0,		/* B1 = 0 */
		 843,		/* B0 = 0.025749 */
		 30550,		/* A1 = 1.864685 */
		 -32221,	/* A2 = -0.983337 */
		 13594,		/* B2 = 0.414886 */
		 -12589,	/* B1 = -0.768402 */
		 13594,		/* B0 = 0.414886 */
		 31488,		/* A1 = 1.921936 */
		 -32358,	/* A2 = -0.987518 */
		 24684,		/* B2 = 0.753296 */
		 -24029,	/* B1 = -1.466614 */
		 24684,		/* B0 = 0.753296 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f340 18 */
		31546,		/* A1 = -1.925476 */
		 -32646,	/* A2 = 0.996277 */
		 -445,		/* B2 = -0.013588 */
		 0,		/* B1 = 0.000000 */
		 445,		/* B0 = 0.013588 */
		 31551,		/* A1 = -1.925781 */
		 -32713,	/* A2 = 0.998352 */
		 23884,		/* B2 = 0.728882 */
		 -22979,	/* B1 = -1.402527 */
		 23884,		/* B0 = 0.728882 */
		 31606,		/* A1 = -1.929138 */
		 -32715,	/* A2 = 0.998413 */
		 863,		/* B2 = 0.026367 */
		 -835,		/* B1 = -0.050985 */
		 863,		/* B0 = 0.026367 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f350_400[] 19 */
		31006,		/* A1 = 1.892517 */
		 -32029,	/* A2 = -0.977448 */
		 -461,		/* B2 = -0.014096 */
		 0,		/* B1 = 0 */
		 461,		/* B0 = 0.014096 */
		 30999,		/* A1 = 1.892029 */
		 -32487,	/* A2 = -0.991455 */
		 11325,		/* B2 = 0.345612 */
		 -10682,	/* B1 = -0.651978 */
		 11325,		/* B0 = 0.345612 */
		 31441,		/* A1 = 1.919067 */
		 -32526,	/* A2 = -0.992615 */
		 24324,		/* B2 = 0.74231 */
		 -23535,	/* B1 = -1.436523 */
		 24324,		/* B0 = 0.74231 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f350_440[] */
		30634,		/* A1 = 1.869751 */
		 -31533,	/* A2 = -0.962341 */
		 -680,		/* B2 = -0.020782 */
		 0,		/* B1 = 0 */
		 680,		/* B0 = 0.020782 */
		 30571,		/* A1 = 1.865906 */
		 -32277,	/* A2 = -0.985016 */
		 12894,		/* B2 = 0.393524 */
		 -11945,	/* B1 = -0.729065 */
		 12894,		/* B0 = 0.393524 */
		 31367,		/* A1 = 1.91449 */
		 -32379,	/* A2 = -0.988129 */
		 23820,		/* B2 = 0.726929 */
		 -23104,	/* B1 = -1.410217 */
		 23820,		/* B0 = 0.726929 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f350_450[] */
		30552,		/* A1 = 1.864807 */
		 -31434,	/* A2 = -0.95929 */
		 -690,		/* B2 = -0.021066 */
		 0,		/* B1 = 0 */
		 690,		/* B0 = 0.021066 */
		 30472,		/* A1 = 1.859924 */
		 -32248,	/* A2 = -0.984161 */
		 13385,		/* B2 = 0.408478 */
		 -12357,	/* B1 = -0.754242 */
		 13385,		/* B0 = 0.408478 */
		 31358,		/* A1 = 1.914001 */
		 -32366,	/* A2 = -0.987732 */
		 26488,		/* B2 = 0.80835 */
		 -25692,	/* B1 = -1.568176 */
		 26490,		/* B0 = 0.808411 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f360 */
		31397,		/* A1 = -1.916321 */
		 -32623,	/* A2 = 0.995605 */
		 -117,		/* B2 = -0.003598 */
		 0,		/* B1 = 0.000000 */
		 117,		/* B0 = 0.003598 */
		 31403,		/* A1 = -1.916687 */
		 -32700,	/* A2 = 0.997925 */
		 3388,		/* B2 = 0.103401 */
		 -3240,		/* B1 = -0.197784 */
		 3388,		/* B0 = 0.103401 */
		 31463,		/* A1 = -1.920410 */
		 -32702,	/* A2 = 0.997986 */
		 13346,		/* B2 = 0.407288 */
		 -12863,	/* B1 = -0.785126 */
		 13346,		/* B0 = 0.407288 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f380_420[] */
		30831,		/* A1 = 1.881775 */
		 -32064,	/* A2 = -0.978546 */
		 -367,		/* B2 = -0.01122 */
		 0,		/* B1 = 0 */
		 367,		/* B0 = 0.01122 */
		 30813,		/* A1 = 1.880737 */
		 -32456,	/* A2 = -0.990509 */
		 11068,		/* B2 = 0.337769 */
		 -10338,	/* B1 = -0.631042 */
		 11068,		/* B0 = 0.337769 */
		 31214,		/* A1 = 1.905212 */
		 -32491,	/* A2 = -0.991577 */
		 16374,		/* B2 = 0.499695 */
		 -15781,	/* B1 = -0.963196 */
		 16374,		/* B0 = 0.499695 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f392 */
		31152,		/* A1 = -1.901428 */
		 -32613,	/* A2 = 0.995300 */
		 -314,		/* B2 = -0.009605 */
		 0,		/* B1 = 0.000000 */
		 314,		/* B0 = 0.009605 */
		 31156,		/* A1 = -1.901672 */
		 -32694,	/* A2 = 0.997742 */
		 28847,		/* B2 = 0.880371 */
		 -2734,		/* B1 = -0.166901 */
		 28847,		/* B0 = 0.880371 */
		 31225,		/* A1 = -1.905823 */
		 -32696,	/* A2 = 0.997803 */
		 462,		/* B2 = 0.014108 */
		 -442,		/* B1 = -0.027019 */
		 462,		/* B0 = 0.014108 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f400_425[] */
		30836,		/* A1 = 1.882141 */
		 -32296,	/* A2 = -0.985596 */
		 -324,		/* B2 = -0.009903 */
		 0,		/* B1 = 0 */
		 324,		/* B0 = 0.009903 */
		 30825,		/* A1 = 1.881409 */
		 -32570,	/* A2 = -0.993958 */
		 16847,		/* B2 = 0.51416 */
		 -15792,	/* B1 = -0.963898 */
		 16847,		/* B0 = 0.51416 */
		 31106,		/* A1 = 1.89856 */
		 -32584,	/* A2 = -0.994415 */
		 9579,		/* B2 = 0.292328 */
		 -9164,		/* B1 = -0.559357 */
		 9579,		/* B0 = 0.292328 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f400_440[] */
		30702,		/* A1 = 1.873962 */
		 -32134,	/* A2 = -0.980682 */
		 -517,		/* B2 = -0.015793 */
		 0,		/* B1 = 0 */
		 517,		/* B0 = 0.015793 */
		 30676,		/* A1 = 1.872375 */
		 -32520,	/* A2 = -0.992462 */
		 8144,		/* B2 = 0.24855 */
		 -7596,		/* B1 = -0.463684 */
		 8144,		/* B0 = 0.24855 */
		 31084,		/* A1 = 1.897217 */
		 -32547,	/* A2 = -0.993256 */
		 22713,		/* B2 = 0.693176 */
		 -21734,	/* B1 = -1.326599 */
		 22713,		/* B0 = 0.693176 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f400_450[] */
		30613,		/* A1 = 1.86853 */
		 -32031,	/* A2 = -0.977509 */
		 -618,		/* B2 = -0.018866 */
		 0,		/* B1 = 0 */
		 618,		/* B0 = 0.018866 */
		 30577,		/* A1 = 1.866272 */
		 -32491,	/* A2 = -0.991577 */
		 9612,		/* B2 = 0.293335 */
		 -8935,		/* B1 = -0.54541 */
		 9612,		/* B0 = 0.293335 */
		 31071,		/* A1 = 1.896484 */
		 -32524,	/* A2 = -0.992584 */
		 21596,		/* B2 = 0.659058 */
		 -20667,	/* B1 = -1.261414 */
		 21596,		/* B0 = 0.659058 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f420 */
		30914,		/* A1 = -1.886841 */
		 -32584,	/* A2 = 0.994385 */
		 -426,		/* B2 = -0.013020 */
		 0,		/* B1 = 0.000000 */
		 426,		/* B0 = 0.013020 */
		 30914,		/* A1 = -1.886841 */
		 -32679,	/* A2 = 0.997314 */
		 17520,		/* B2 = 0.534668 */
		 -16471,	/* B1 = -1.005310 */
		 17520,		/* B0 = 0.534668 */
		 31004,		/* A1 = -1.892334 */
		 -32683,	/* A2 = 0.997406 */
		 819,		/* B2 = 0.025023 */
		 -780,		/* B1 = -0.047619 */
		 819,		/* B0 = 0.025023 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
#if 0
	{			/* f425 */
		30881,		/* A1 = -1.884827 */
		 -32603,	/* A2 = 0.994965 */
		 -496,		/* B2 = -0.015144 */
		 0,		/* B1 = 0.000000 */
		 496,		/* B0 = 0.015144 */
		 30880,		/* A1 = -1.884766 */
		 -32692,	/* A2 = 0.997711 */
		 24767,		/* B2 = 0.755859 */
		 -23290,	/* B1 = -1.421509 */
		 24767,		/* B0 = 0.755859 */
		 30967,		/* A1 = -1.890076 */
		 -32694,	/* A2 = 0.997772 */
		 728,		/* B2 = 0.022232 */
		 -691,		/* B1 = -0.042194 */
		 728,		/* B0 = 0.022232 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
#else
	{
		30850,
		-32534,
		-504,
		0,
		504,
		30831,
		-32669,
		24303,
		-22080,
		24303,
		30994,
		-32673,
		1905,
		-1811,
		1905,
		5,
		129,
		17,
		0xff5
	},
#endif
	{			/* f425_450[] */
		30646,		/* A1 = 1.870544 */
		 -32327,	/* A2 = -0.986572 */
		 -287,		/* B2 = -0.008769 */
		 0,		/* B1 = 0 */
		 287,		/* B0 = 0.008769 */
		 30627,		/* A1 = 1.869324 */
		 -32607,	/* A2 = -0.995087 */
		 13269,		/* B2 = 0.404968 */
		 -12376,	/* B1 = -0.755432 */
		 13269,		/* B0 = 0.404968 */
		 30924,		/* A1 = 1.887512 */
		 -32619,	/* A2 = -0.995453 */
		 19950,		/* B2 = 0.608826 */
		 -18940,	/* B1 = -1.156006 */
		 19950,		/* B0 = 0.608826 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f425_475[] */
		30396,		/* A1 = 1.855225 */
		 -32014,	/* A2 = -0.97699 */
		 -395,		/* B2 = -0.012055 */
		 0,		/* B1 = 0 */
		 395,		/* B0 = 0.012055 */
		 30343,		/* A1 = 1.85199 */
		 -32482,	/* A2 = -0.991302 */
		 17823,		/* B2 = 0.543945 */
		 -16431,	/* B1 = -1.002869 */
		 17823,		/* B0 = 0.543945 */
		 30872,		/* A1 = 1.884338 */
		 -32516,	/* A2 = -0.99231 */
		 18124,		/* B2 = 0.553101 */
		 -17246,	/* B1 = -1.052673 */
		 18124,		/* B0 = 0.553101 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f435 */
		30796,		/* A1 = -1.879639 */
		 -32603,	/* A2 = 0.994965 */
		 -254,		/* B2 = -0.007762 */
		 0,		/* B1 = 0.000000 */
		 254,		/* B0 = 0.007762 */
		 30793,		/* A1 = -1.879456 */
		 -32692,	/* A2 = 0.997711 */
		 18934,		/* B2 = 0.577820 */
		 -17751,	/* B1 = -1.083496 */
		 18934,		/* B0 = 0.577820 */
		 30882,		/* A1 = -1.884888 */
		 -32694,	/* A2 = 0.997772 */
		 1858,		/* B2 = 0.056713 */
		 -1758,		/* B1 = -0.107357 */
		 1858,		/* B0 = 0.056713 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f440_450[] */
		30641,		/* A1 = 1.870239 */
		 -32458,	/* A2 = -0.99057 */
		 -155,		/* B2 = -0.004735 */
		 0,		/* B1 = 0 */
		 155,		/* B0 = 0.004735 */
		 30631,		/* A1 = 1.869568 */
		 -32630,	/* A2 = -0.995789 */
		 11453,		/* B2 = 0.349548 */
		 -10666,	/* B1 = -0.651001 */
		 11453,		/* B0 = 0.349548 */
		 30810,		/* A1 = 1.880554 */
		 -32634,	/* A2 = -0.995941 */
		 12237,		/* B2 = 0.373474 */
		 -11588,	/* B1 = -0.707336 */
		 12237,		/* B0 = 0.373474 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f440_480[] */
		30367,		/* A1 = 1.853455 */
		 -32147,	/* A2 = -0.981079 */
		 -495,		/* B2 = -0.015113 */
		 0,		/* B1 = 0 */
		 495,		/* B0 = 0.015113 */
		 30322,		/* A1 = 1.850769 */
		 -32543,	/* A2 = -0.993134 */
		 10031,		/* B2 = 0.306152 */
		 -9252,		/* B1 = -0.564728 */
		 10031,		/* B0 = 0.306152 */
		 30770,		/* A1 = 1.878052 */
		 -32563,	/* A2 = -0.993774 */
		 22674,		/* B2 = 0.691956 */
		 -21465,	/* B1 = -1.31012 */
		 22674,		/* B0 = 0.691956 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f445 */
		30709,		/* A1 = -1.874329 */
		 -32603,	/* A2 = 0.994965 */
		 -83,		/* B2 = -0.002545 */
		 0,		/* B1 = 0.000000 */
		 83,		/* B0 = 0.002545 */
		 30704,		/* A1 = -1.874084 */
		 -32692,	/* A2 = 0.997711 */
		 10641,		/* B2 = 0.324738 */
		 -9947,		/* B1 = -0.607147 */
		 10641,		/* B0 = 0.324738 */
		 30796,		/* A1 = -1.879639 */
		 -32694,	/* A2 = 0.997772 */
		 10079,		/* B2 = 0.307587 */
		 9513,		/* B1 = 0.580688 */
		 10079,		/* B0 = 0.307587 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f450 */
		30664,		/* A1 = -1.871643 */
		 -32603,	/* A2 = 0.994965 */
		 -164,		/* B2 = -0.005029 */
		 0,		/* B1 = 0.000000 */
		 164,		/* B0 = 0.005029 */
		 30661,		/* A1 = -1.871399 */
		 -32692,	/* A2 = 0.997711 */
		 15294,		/* B2 = 0.466736 */
		 -14275,	/* B1 = -0.871307 */
		 15294,		/* B0 = 0.466736 */
		 30751,		/* A1 = -1.876953 */
		 -32694,	/* A2 = 0.997772 */
		 3548,		/* B2 = 0.108284 */
		 -3344,		/* B1 = -0.204155 */
		 3548,		/* B0 = 0.108284 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f452 */
		30653,		/* A1 = -1.870911 */
		 -32615,	/* A2 = 0.995361 */
		 -209,		/* B2 = -0.006382 */
		 0,		/* B1 = 0.000000 */
		 209,		/* B0 = 0.006382 */
		 30647,		/* A1 = -1.870605 */
		 -32702,	/* A2 = 0.997986 */
		 18971,		/* B2 = 0.578979 */
		 -17716,	/* B1 = -1.081299 */
		 18971,		/* B0 = 0.578979 */
		 30738,		/* A1 = -1.876099 */
		 -32702,	/* A2 = 0.998016 */
		 2967,		/* B2 = 0.090561 */
		 -2793,		/* B1 = -0.170502 */
		 2967,		/* B0 = 0.090561 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f475 */
		30437,		/* A1 = -1.857727 */
		 -32603,	/* A2 = 0.994965 */
		 -264,		/* B2 = -0.008062 */
		 0,		/* B1 = 0.000000 */
		 264,		/* B0 = 0.008062 */
		 30430,		/* A1 = -1.857300 */
		 -32692,	/* A2 = 0.997711 */
		 21681,		/* B2 = 0.661682 */
		 -20082,	/* B1 = -1.225708 */
		 21681,		/* B0 = 0.661682 */
		 30526,		/* A1 = -1.863220 */
		 -32694,	/* A2 = 0.997742 */
		 1559,		/* B2 = 0.047600 */
		 -1459,		/* B1 = -0.089096 */
		 1559,		/* B0 = 0.047600 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f480_620[] */
		28975,		/* A1 = 1.768494 */
		 -30955,	/* A2 = -0.944672 */
		 -1026,		/* B2 = -0.03133 */
		 0,		/* B1 = 0 */
		 1026,		/* B0 = 0.03133 */
		 28613,		/* A1 = 1.746399 */
		 -32089,	/* A2 = -0.979309 */
		 14214,		/* B2 = 0.433807 */
		 -12202,	/* B1 = -0.744812 */
		 14214,		/* B0 = 0.433807 */
		 30243,		/* A1 = 1.845947 */
		 -32238,	/* A2 = -0.983856 */
		 24825,		/* B2 = 0.757629 */
		 -23402,	/* B1 = -1.428345 */
		 24825,		/* B0 = 0.757629 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f494 */
		30257,		/* A1 = -1.846741 */
		 -32605,	/* A2 = 0.995056 */
		 -249,		/* B2 = -0.007625 */
		 0,		/* B1 = 0.000000 */
		 249,		/* B0 = 0.007625 */
		 30247,		/* A1 = -1.846191 */
		 -32694,	/* A2 = 0.997772 */
		 18088,		/* B2 = 0.552002 */
		 -16652,	/* B1 = -1.016418 */
		 18088,		/* B0 = 0.552002 */
		 30348,		/* A1 = -1.852295 */
		 -32696,	/* A2 = 0.997803 */
		 2099,		/* B2 = 0.064064 */
		 -1953,		/* B1 = -0.119202 */
		 2099,		/* B0 = 0.064064 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f500 */
		30202,		/* A1 = -1.843431 */
		 -32624,	/* A2 = 0.995622 */
		 -413,		/* B2 = -0.012622 */
		 0,		/* B1 = 0.000000 */
		 413,		/* B0 = 0.012622 */
		 30191,		/* A1 = -1.842721 */
		 -32714,	/* A2 = 0.998364 */
		 25954,		/* B2 = 0.792057 */
		 -23890,	/* B1 = -1.458131 */
		 25954,		/* B0 = 0.792057 */
		 30296,		/* A1 = -1.849172 */
		 -32715,	/* A2 = 0.998397 */
		 2007,		/* B2 = 0.061264 */
		 -1860,		/* B1 = -0.113568 */
		 2007,		/* B0 = 0.061264 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f520 */
		30001,		/* A1 = -1.831116 */
		 -32613,	/* A2 = 0.995270 */
		 -155,		/* B2 = -0.004750 */
		 0,		/* B1 = 0.000000 */
		 155,		/* B0 = 0.004750 */
		 29985,		/* A1 = -1.830200 */
		 -32710,	/* A2 = 0.998260 */
		 6584,		/* B2 = 0.200928 */
		 -6018,		/* B1 = -0.367355 */
		 6584,		/* B0 = 0.200928 */
		 30105,		/* A1 = -1.837524 */
		 -32712,	/* A2 = 0.998291 */
		 23812,		/* B2 = 0.726685 */
		 -21936,	/* B1 = -1.338928 */
		 23812,		/* B0 = 0.726685 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f523 */
		29964,		/* A1 = -1.828918 */
		 -32601,	/* A2 = 0.994904 */
		 -101,		/* B2 = -0.003110 */
		 0,		/* B1 = 0.000000 */
		 101,		/* B0 = 0.003110 */
		 29949,		/* A1 = -1.827942 */
		 -32700,	/* A2 = 0.997925 */
		 11041,		/* B2 = 0.336975 */
		 -10075,	/* B1 = -0.614960 */
		 11041,		/* B0 = 0.336975 */
		 30070,		/* A1 = -1.835388 */
		 -32702,	/* A2 = 0.997986 */
		 16762,		/* B2 = 0.511536 */
		 -15437,	/* B1 = -0.942230 */
		 16762,		/* B0 = 0.511536 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f525 */
		29936,		/* A1 = -1.827209 */
		 -32584,	/* A2 = 0.994415 */
		 -91,		/* B2 = -0.002806 */
		 0,		/* B1 = 0.000000 */
		 91,		/* B0 = 0.002806 */
		 29921,		/* A1 = -1.826233 */
		 -32688,	/* A2 = 0.997559 */
		 11449,		/* B2 = 0.349396 */
		 -10426,	/* B1 = -0.636383 */
		 11449,		/* B0 = 0.349396 */
		 30045,		/* A1 = -1.833862 */
		 -32688,	/* A2 = 0.997589 */
		 13055,		/* B2 = 0.398407 */
		 -12028,	/* B1 = -0.734161 */
		 13055,		/* B0 = 0.398407 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f540_660[] */
		28499,		/* A1 = 1.739441 */
		 -31129,	/* A2 = -0.949982 */
		 -849,		/* B2 = -0.025922 */
		 0,		/* B1 = 0 */
		 849,		/* B0 = 0.025922 */
		 28128,		/* A1 = 1.716797 */
		 -32130,	/* A2 = -0.98056 */
		 14556,		/* B2 = 0.444214 */
		 -12251,	/* B1 = -0.747772 */
		 14556,		/* B0 = 0.444244 */
		 29667,		/* A1 = 1.81073 */
		 -32244,	/* A2 = -0.984039 */
		 23038,		/* B2 = 0.703064 */
		 -21358,	/* B1 = -1.303589 */
		 23040,		/* B0 = 0.703125 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f587 */
		29271,		/* A1 = -1.786560 */
		 -32599,	/* A2 = 0.994873 */
		 -490,		/* B2 = -0.014957 */
		 0,		/* B1 = 0.000000 */
		 490,		/* B0 = 0.014957 */
		 29246,		/* A1 = -1.785095 */
		 -32700,	/* A2 = 0.997925 */
		 28961,		/* B2 = 0.883850 */
		 -25796,	/* B1 = -1.574463 */
		 28961,		/* B0 = 0.883850 */
		 29383,		/* A1 = -1.793396 */
		 -32700,	/* A2 = 0.997955 */
		 1299,		/* B2 = 0.039650 */
		 -1169,		/* B1 = -0.071396 */
		 1299,		/* B0 = 0.039650 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f590 */
		29230,		/* A1 = -1.784058 */
		 -32584,	/* A2 = 0.994415 */
		 -418,		/* B2 = -0.012757 */
		 0,		/* B1 = 0.000000 */
		 418,		/* B0 = 0.012757 */
		 29206,		/* A1 = -1.782593 */
		 -32688,	/* A2 = 0.997559 */
		 36556,		/* B2 = 1.115601 */
		 -32478,	/* B1 = -1.982300 */
		 36556,		/* B0 = 1.115601 */
		 29345,		/* A1 = -1.791077 */
		 -32688,	/* A2 = 0.997589 */
		 897,		/* B2 = 0.027397 */
		 -808,		/* B1 = -0.049334 */
		 897,		/* B0 = 0.027397 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f600 */
		29116,		/* A1 = -1.777100 */
		 -32603,	/* A2 = 0.994965 */
		 -165,		/* B2 = -0.005039 */
		 0,		/* B1 = 0.000000 */
		 165,		/* B0 = 0.005039 */
		 29089,		/* A1 = -1.775452 */
		 -32708,	/* A2 = 0.998199 */
		 6963,		/* B2 = 0.212494 */
		 -6172,		/* B1 = -0.376770 */
		 6963,		/* B0 = 0.212494 */
		 29237,		/* A1 = -1.784485 */
		 -32710,	/* A2 = 0.998230 */
		 24197,		/* B2 = 0.738464 */
		 -21657,	/* B1 = -1.321899 */
		 24197,		/* B0 = 0.738464 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f660 */
		28376,		/* A1 = -1.731934 */
		 -32567,	/* A2 = 0.993896 */
		 -363,		/* B2 = -0.011102 */
		 0,		/* B1 = 0.000000 */
		 363,		/* B0 = 0.011102 */
		 28337,		/* A1 = -1.729614 */
		 -32683,	/* A2 = 0.997434 */
		 21766,		/* B2 = 0.664246 */
		 -18761,	/* B1 = -1.145081 */
		 21766,		/* B0 = 0.664246 */
		 28513,		/* A1 = -1.740356 */
		 -32686,	/* A2 = 0.997498 */
		 2509,		/* B2 = 0.076584 */
		 -2196,		/* B1 = -0.134041 */
		 2509,		/* B0 = 0.076584 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f700 */
		27844,		/* A1 = -1.699463 */
		 -32563,	/* A2 = 0.993744 */
		 -366,		/* B2 = -0.011187 */
		 0,		/* B1 = 0.000000 */
		 366,		/* B0 = 0.011187 */
		 27797,		/* A1 = -1.696655 */
		 -32686,	/* A2 = 0.997498 */
		 22748,		/* B2 = 0.694214 */
		 -19235,	/* B1 = -1.174072 */
		 22748,		/* B0 = 0.694214 */
		 27995,		/* A1 = -1.708740 */
		 -32688,	/* A2 = 0.997559 */
		 2964,		/* B2 = 0.090477 */
		 -2546,		/* B1 = -0.155449 */
		 2964,		/* B0 = 0.090477 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f740 */
		27297,		/* A1 = -1.666077 */
		 -32551,	/* A2 = 0.993408 */
		 -345,		/* B2 = -0.010540 */
		 0,		/* B1 = 0.000000 */
		 345,		/* B0 = 0.010540 */
		 27240,		/* A1 = -1.662598 */
		 -32683,	/* A2 = 0.997406 */
		 22560,		/* B2 = 0.688477 */
		 -18688,	/* B1 = -1.140625 */
		 22560,		/* B0 = 0.688477 */
		 27461,		/* A1 = -1.676147 */
		 -32684,	/* A2 = 0.997467 */
		 3541,		/* B2 = 0.108086 */
		 -2985,		/* B1 = -0.182220 */
		 3541,		/* B0 = 0.108086 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f750 */
		27155,		/* A1 = -1.657410 */
		 -32551,	/* A2 = 0.993408 */
		 -462,		/* B2 = -0.014117 */
		 0,		/* B1 = 0.000000 */
		 462,		/* B0 = 0.014117 */
		 27097,		/* A1 = -1.653870 */
		 -32683,	/* A2 = 0.997406 */
		 32495,		/* B2 = 0.991699 */
		 -26776,	/* B1 = -1.634338 */
		 32495,		/* B0 = 0.991699 */
		 27321,		/* A1 = -1.667542 */
		 -32684,	/* A2 = 0.997467 */
		 1835,		/* B2 = 0.056007 */
		 -1539,		/* B1 = -0.093948 */
		 1835,		/* B0 = 0.056007 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f750_1450[] */
		19298,		/* A1 = 1.177917 */
		 -24471,	/* A2 = -0.746796 */
		 -4152,		/* B2 = -0.126709 */
		 0,		/* B1 = 0 */
		 4152,		/* B0 = 0.126709 */
		 12902,		/* A1 = 0.787476 */
		 -29091,	/* A2 = -0.887817 */
		 12491,		/* B2 = 0.38121 */
		 -1794,		/* B1 = -0.109528 */
		 12494,		/* B0 = 0.381317 */
		 26291,		/* A1 = 1.604736 */
		 -30470,	/* A2 = -0.929901 */
		 28859,		/* B2 = 0.880737 */
		 -26084,	/* B1 = -1.592102 */
		 28861,		/* B0 = 0.880798 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f770 */
		26867,		/* A1 = -1.639832 */
		 -32551,	/* A2 = 0.993408 */
		 -123,		/* B2 = -0.003755 */
		 0,		/* B1 = 0.000000 */
		 123,		/* B0 = 0.003755 */
		 26805,		/* A1 = -1.636108 */
		 -32683,	/* A2 = 0.997406 */
		 17297,		/* B2 = 0.527863 */
		 -14096,	/* B1 = -0.860382 */
		 17297,		/* B0 = 0.527863 */
		 27034,		/* A1 = -1.650085 */
		 -32684,	/* A2 = 0.997467 */
		 12958,		/* B2 = 0.395477 */
		 -10756,	/* B1 = -0.656525 */
		 12958,		/* B0 = 0.395477 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f800 */
		26413,		/* A1 = -1.612122 */
		 -32547,	/* A2 = 0.993286 */
		 -223,		/* B2 = -0.006825 */
		 0,		/* B1 = 0.000000 */
		 223,		/* B0 = 0.006825 */
		 26342,		/* A1 = -1.607849 */
		 -32686,	/* A2 = 0.997498 */
		 6391,		/* B2 = 0.195053 */
		 -5120,		/* B1 = -0.312531 */
		 6391,		/* B0 = 0.195053 */
		 26593,		/* A1 = -1.623108 */
		 -32688,	/* A2 = 0.997559 */
		 23681,		/* B2 = 0.722717 */
		 -19328,	/* B1 = -1.179688 */
		 23681,		/* B0 = 0.722717 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f816 */
		26168,		/* A1 = -1.597209 */
		 -32528,	/* A2 = 0.992706 */
		 -235,		/* B2 = -0.007182 */
		 0,		/* B1 = 0.000000 */
		 235,		/* B0 = 0.007182 */
		 26092,		/* A1 = -1.592590 */
		 -32675,	/* A2 = 0.997192 */
		 20823,		/* B2 = 0.635498 */
		 -16510,	/* B1 = -1.007751 */
		 20823,		/* B0 = 0.635498 */
		 26363,		/* A1 = -1.609070 */
		 -32677,	/* A2 = 0.997253 */
		 6739,		/* B2 = 0.205688 */
		 -5459,		/* B1 = -0.333206 */
		 6739,		/* B0 = 0.205688 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f850 */
		25641,		/* A1 = -1.565063 */
		 -32536,	/* A2 = 0.992950 */
		 -121,		/* B2 = -0.003707 */
		 0,		/* B1 = 0.000000 */
		 121,		/* B0 = 0.003707 */
		 25560,		/* A1 = -1.560059 */
		 -32684,	/* A2 = 0.997437 */
		 18341,		/* B2 = 0.559753 */
		 -14252,	/* B1 = -0.869904 */
		 18341,		/* B0 = 0.559753 */
		 25837,		/* A1 = -1.577026 */
		 -32684,	/* A2 = 0.997467 */
		 16679,		/* B2 = 0.509003 */
		 -13232,	/* B1 = -0.807648 */
		 16679,		/* B0 = 0.509003 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f857_1645[] */
		16415,		/* A1 = 1.001953 */
		 -23669,	/* A2 = -0.722321 */
		 -4549,		/* B2 = -0.138847 */
		 0,		/* B1 = 0 */
		 4549,		/* B0 = 0.138847 */
		 8456,		/* A1 = 0.516174 */
		 -28996,	/* A2 = -0.884918 */
		 13753,		/* B2 = 0.419724 */
		 -12,		/* B1 = -0.000763 */
		 13757,		/* B0 = 0.419846 */
		 24632,		/* A1 = 1.503418 */
		 -30271,	/* A2 = -0.923828 */
		 29070,		/* B2 = 0.887146 */
		 -25265,	/* B1 = -1.542114 */
		 29073,		/* B0 = 0.887268 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f900 */
		24806,		/* A1 = -1.514099 */
		 -32501,	/* A2 = 0.991852 */
		 -326,		/* B2 = -0.009969 */
		 0,		/* B1 = 0.000000 */
		 326,		/* B0 = 0.009969 */
		 24709,		/* A1 = -1.508118 */
		 -32659,	/* A2 = 0.996674 */
		 20277,		/* B2 = 0.618835 */
		 -15182,	/* B1 = -0.926636 */
		 20277,		/* B0 = 0.618835 */
		 25022,		/* A1 = -1.527222 */
		 -32661,	/* A2 = 0.996735 */
		 4320,		/* B2 = 0.131836 */
		 -3331,		/* B1 = -0.203339 */
		 4320,		/* B0 = 0.131836 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f900_1300[] */
		19776,		/* A1 = 1.207092 */
		 -27437,	/* A2 = -0.837341 */
		 -2666,		/* B2 = -0.081371 */
		 0,		/* B1 = 0 */
		 2666,		/* B0 = 0.081371 */
		 16302,		/* A1 = 0.995026 */
		 -30354,	/* A2 = -0.926361 */
		 10389,		/* B2 = 0.317062 */
		 -3327,		/* B1 = -0.203064 */
		 10389,		/* B0 = 0.317062 */
		 24299,		/* A1 = 1.483154 */
		 -30930,	/* A2 = -0.943909 */
		 25016,		/* B2 = 0.763428 */
		 -21171,	/* B1 = -1.292236 */
		 25016,		/* B0 = 0.763428 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f935_1215[] */
		20554,		/* A1 = 1.254517 */
		 -28764,	/* A2 = -0.877838 */
		 -2048,		/* B2 = -0.062515 */
		 0,		/* B1 = 0 */
		 2048,		/* B0 = 0.062515 */
		 18209,		/* A1 = 1.11145 */
		 -30951,	/* A2 = -0.94458 */
		 9390,		/* B2 = 0.286575 */
		 -3955,		/* B1 = -0.241455 */
		 9390,		/* B0 = 0.286575 */
		 23902,		/* A1 = 1.458923 */
		 -31286,	/* A2 = -0.954803 */
		 23252,		/* B2 = 0.709595 */
		 -19132,	/* B1 = -1.167725 */
		 23252,		/* B0 = 0.709595 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f941_1477[] */
		17543,		/* A1 = 1.07074 */
		 -26220,	/* A2 = -0.800201 */
		 -3298,		/* B2 = -0.100647 */
		 0,		/* B1 = 0 */
		 3298,		/* B0 = 0.100647 */
		 12423,		/* A1 = 0.75827 */
		 -30036,	/* A2 = -0.916626 */
		 12651,		/* B2 = 0.386078 */
		 -2444,		/* B1 = -0.14917 */
		 12653,		/* B0 = 0.386154 */
		 23518,		/* A1 = 1.435425 */
		 -30745,	/* A2 = -0.938293 */
		 27282,		/* B2 = 0.832581 */
		 -22529,	/* B1 = -1.375122 */
		 27286,		/* B0 = 0.832703 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f942 */
		24104,		/* A1 = -1.471252 */
		 -32507,	/* A2 = 0.992065 */
		 -351,		/* B2 = -0.010722 */
		 0,		/* B1 = 0.000000 */
		 351,		/* B0 = 0.010722 */
		 23996,		/* A1 = -1.464600 */
		 -32671,	/* A2 = 0.997040 */
		 22848,		/* B2 = 0.697266 */
		 -16639,	/* B1 = -1.015564 */
		 22848,		/* B0 = 0.697266 */
		 24332,		/* A1 = -1.485168 */
		 -32673,	/* A2 = 0.997101 */
		 4906,		/* B2 = 0.149727 */
		 -3672,		/* B1 = -0.224174 */
		 4906,		/* B0 = 0.149727 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f950 */
		23967,		/* A1 = -1.462830 */
		 -32507,	/* A2 = 0.992065 */
		 -518,		/* B2 = -0.015821 */
		 0,		/* B1 = 0.000000 */
		 518,		/* B0 = 0.015821 */
		 23856,		/* A1 = -1.456055 */
		 -32671,	/* A2 = 0.997040 */
		 26287,		/* B2 = 0.802246 */
		 -19031,	/* B1 = -1.161560 */
		 26287,		/* B0 = 0.802246 */
		 24195,		/* A1 = -1.476746 */
		 -32673,	/* A2 = 0.997101 */
		 2890,		/* B2 = 0.088196 */
		 -2151,		/* B1 = -0.131317 */
		 2890,		/* B0 = 0.088196 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f950_1400[] */
		18294,		/* A1 = 1.116638 */
		 -26962,	/* A2 = -0.822845 */
		 -2914,		/* B2 = -0.088936 */
		 0,		/* B1 = 0 */
		 2914,		/* B0 = 0.088936 */
		 14119,		/* A1 = 0.861786 */
		 -30227,	/* A2 = -0.922455 */
		 11466,		/* B2 = 0.349945 */
		 -2833,		/* B1 = -0.172943 */
		 11466,		/* B0 = 0.349945 */
		 23431,		/* A1 = 1.430115 */
		 -30828,	/* A2 = -0.940796 */
		 25331,		/* B2 = 0.773071 */
		 -20911,	/* B1 = -1.276367 */
		 25331,		/* B0 = 0.773071 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f975 */
		23521,		/* A1 = -1.435608 */
		 -32489,	/* A2 = 0.991516 */
		 -193,		/* B2 = -0.005915 */
		 0,		/* B1 = 0.000000 */
		 193,		/* B0 = 0.005915 */
		 23404,		/* A1 = -1.428467 */
		 -32655,	/* A2 = 0.996582 */
		 17740,		/* B2 = 0.541412 */
		 -12567,	/* B1 = -0.767029 */
		 17740,		/* B0 = 0.541412 */
		 23753,		/* A1 = -1.449829 */
		 -32657,	/* A2 = 0.996613 */
		 9090,		/* B2 = 0.277405 */
		 -6662,		/* B1 = -0.406647 */
		 9090,		/* B0 = 0.277405 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f1000 */
		23071,		/* A1 = -1.408203 */
		 -32489,	/* A2 = 0.991516 */
		 -293,		/* B2 = -0.008965 */
		 0,		/* B1 = 0.000000 */
		 293,		/* B0 = 0.008965 */
		 22951,		/* A1 = -1.400818 */
		 -32655,	/* A2 = 0.996582 */
		 5689,		/* B2 = 0.173645 */
		 -3951,		/* B1 = -0.241150 */
		 5689,		/* B0 = 0.173645 */
		 23307,		/* A1 = -1.422607 */
		 -32657,	/* A2 = 0.996613 */
		 18692,		/* B2 = 0.570435 */
		 -13447,	/* B1 = -0.820770 */
		 18692,		/* B0 = 0.570435 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f1020 */
		22701,		/* A1 = -1.385620 */
		 -32474,	/* A2 = 0.991058 */
		 -292,		/* B2 = -0.008933 */
		 0,		/*163840      , B1 = 10.000000 */
		 292,		/* B0 = 0.008933 */
		 22564,		/* A1 = -1.377258 */
		 -32655,	/* A2 = 0.996552 */
		 20756,		/* B2 = 0.633423 */
		 -14176,	/* B1 = -0.865295 */
		 20756,		/* B0 = 0.633423 */
		 22960,		/* A1 = -1.401428 */
		 -32657,	/* A2 = 0.996613 */
		 6520,		/* B2 = 0.198990 */
		 -4619,		/* B1 = -0.281937 */
		 6520,		/* B0 = 0.198990 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f1050 */
		22142,		/* A1 = -1.351501 */
		 -32474,	/* A2 = 0.991058 */
		 -147,		/* B2 = -0.004493 */
		 0,		/* B1 = 0.000000 */
		 147,		/* B0 = 0.004493 */
		 22000,		/* A1 = -1.342834 */
		 -32655,	/* A2 = 0.996552 */
		 15379,		/* B2 = 0.469360 */
		 -10237,	/* B1 = -0.624847 */
		 15379,		/* B0 = 0.469360 */
		 22406,		/* A1 = -1.367554 */
		 -32657,	/* A2 = 0.996613 */
		 17491,		/* B2 = 0.533783 */
		 -12096,	/* B1 = -0.738312 */
		 17491,		/* B0 = 0.533783 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f1100_1750[] */
		12973,		/* A1 = 0.79184 */
		 -24916,	/* A2 = -0.760376 */
		 6655,		/* B2 = 0.203102 */
		 367,		/* B1 = 0.0224 */
		 6657,		/* B0 = 0.203171 */
		 5915,		/* A1 = 0.361053 */
		 -29560,	/* A2 = -0.90213 */
		 -7777,		/* B2 = -0.23735 */
		 0,		/* B1 = 0 */
		 7777,		/* B0 = 0.23735 */
		 20510,		/* A1 = 1.251892 */
		 -30260,	/* A2 = -0.923462 */
		 26662,		/* B2 = 0.81366 */
		 -20573,	/* B1 = -1.255737 */
		 26668,		/* B0 = 0.813843 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f1140 */
		20392,		/* A1 = -1.244629 */
		 -32460,	/* A2 = 0.990601 */
		 -270,		/* B2 = -0.008240 */
		 0,		/* B1 = 0.000000 */
		 270,		/* B0 = 0.008240 */
		 20218,		/* A1 = -1.234009 */
		 -32655,	/* A2 = 0.996582 */
		 21337,		/* B2 = 0.651154 */
		 -13044,	/* B1 = -0.796143 */
		 21337,		/* B0 = 0.651154 */
		 20684,		/* A1 = -1.262512 */
		 -32657,	/* A2 = 0.996643 */
		 8572,		/* B2 = 0.261612 */
		 -5476,		/* B1 = -0.334244 */
		 8572,		/* B0 = 0.261612 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f1200 */
		19159,		/* A1 = -1.169373 */
		 -32456,	/* A2 = 0.990509 */
		 -335,		/* B2 = -0.010252 */
		 0,		/* B1 = 0.000000 */
		 335,		/* B0 = 0.010252 */
		 18966,		/* A1 = -1.157593 */
		 -32661,	/* A2 = 0.996735 */
		 6802,		/* B2 = 0.207588 */
		 -3900,		/* B1 = -0.238098 */
		 6802,		/* B0 = 0.207588 */
		 19467,		/* A1 = -1.188232 */
		 -32661,	/* A2 = 0.996765 */
		 25035,		/* B2 = 0.764008 */
		 -15049,	/* B1 = -0.918579 */
		 25035,		/* B0 = 0.764008 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f1209 */
		18976,		/* A1 = -1.158264 */
		 -32439,	/* A2 = 0.989990 */
		 -183,		/* B2 = -0.005588 */
		 0,		/* B1 = 0.000000 */
		 183,		/* B0 = 0.005588 */
		 18774,		/* A1 = -1.145874 */
		 -32650,	/* A2 = 0.996429 */
		 15468,		/* B2 = 0.472076 */
		 -8768,		/* B1 = -0.535217 */
		 15468,		/* B0 = 0.472076 */
		 19300,		/* A1 = -1.177979 */
		 -32652,	/* A2 = 0.996490 */
		 19840,		/* B2 = 0.605499 */
		 -11842,	/* B1 = -0.722809 */
		 19840,		/* B0 = 0.605499 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f1330 */
		16357,		/* A1 = -0.998413 */
		 -32368,	/* A2 = 0.987793 */
		 -217,		/* B2 = -0.006652 */
		 0,		/* B1 = 0.000000 */
		 217,		/* B0 = 0.006652 */
		 16107,		/* A1 = -0.983126 */
		 -32601,	/* A2 = 0.994904 */
		 11602,		/* B2 = 0.354065 */
		 -5555,		/* B1 = -0.339111 */
		 11602,		/* B0 = 0.354065 */
		 16722,		/* A1 = -1.020630 */
		 -32603,	/* A2 = 0.994965 */
		 15574,		/* B2 = 0.475311 */
		 -8176,		/* B1 = -0.499069 */
		 15574,		/* B0 = 0.475311 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f1336 */
		16234,		/* A1 = -0.990875 */
		 32404,		/* A2 = -0.988922 */
		 -193,		/* B2 = -0.005908 */
		 0,		/* B1 = 0.000000 */
		 193,		/* B0 = 0.005908 */
		 15986,		/* A1 = -0.975769 */
		 -32632,	/* A2 = 0.995880 */
		 18051,		/* B2 = 0.550903 */
		 -8658,		/* B1 = -0.528473 */
		 18051,		/* B0 = 0.550903 */
		 16591,		/* A1 = -1.012695 */
		 -32634,	/* A2 = 0.995941 */
		 15736,		/* B2 = 0.480240 */
		 -8125,		/* B1 = -0.495926 */
		 15736,		/* B0 = 0.480240 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f1366 */
		15564,		/* A1 = -0.949982 */
		 -32404,	/* A2 = 0.988922 */
		 -269,		/* B2 = -0.008216 */
		 0,		/* B1 = 0.000000 */
		 269,		/* B0 = 0.008216 */
		 15310,		/* A1 = -0.934479 */
		 -32632,	/* A2 = 0.995880 */
		 10815,		/* B2 = 0.330063 */
		 -4962,		/* B1 = -0.302887 */
		 10815,		/* B0 = 0.330063 */
		 15924,		/* A1 = -0.971924 */
		 -32634,	/* A2 = 0.995941 */
		 18880,		/* B2 = 0.576172 */
		 -9364,		/* B1 = -0.571594 */
		 18880,		/* B0 = 0.576172 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f1380 */
		15247,		/* A1 = -0.930603 */
		 -32397,	/* A2 = 0.988708 */
		 -244,		/* B2 = -0.007451 */
		 0,		/* B1 = 0.000000 */
		 244,		/* B0 = 0.007451 */
		 14989,		/* A1 = -0.914886 */
		 -32627,	/* A2 = 0.995697 */
		 18961,		/* B2 = 0.578644 */
		 -8498,		/* B1 = -0.518707 */
		 18961,		/* B0 = 0.578644 */
		 15608,		/* A1 = -0.952667 */
		 -32628,	/* A2 = 0.995758 */
		 11145,		/* B2 = 0.340134 */
		 -5430,		/* B1 = -0.331467 */
		 11145,		/* B0 = 0.340134 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f1400 */
		14780,		/* A1 = -0.902130 */
		 -32393,	/* A2 = 0.988586 */
		 -396,		/* B2 = -0.012086 */
		 0,		/* B1 = 0.000000 */
		 396,		/* B0 = 0.012086 */
		 14510,		/* A1 = -0.885651 */
		 -32630,	/* A2 = 0.995819 */
		 6326,		/* B2 = 0.193069 */
		 -2747,		/* B1 = -0.167671 */
		 6326,		/* B0 = 0.193069 */
		 15154,		/* A1 = -0.924957 */
		 -32632,	/* A2 = 0.995850 */
		 23235,		/* B2 = 0.709076 */
		 -10983,	/* B1 = -0.670380 */
		 23235,		/* B0 = 0.709076 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f1477 */
		13005,		/* A1 = -0.793793 */
		 -32368,	/* A2 = 0.987823 */
		 -500,		/* B2 = -0.015265 */
		 0,		/* B1 = 0.000000 */
		 500,		/* B0 = 0.015265 */
		 12708,		/* A1 = -0.775665 */
		 -32615,	/* A2 = 0.995331 */
		 11420,		/* B2 = 0.348526 */
		 -4306,		/* B1 = -0.262833 */
		 11420,		/* B0 = 0.348526 */
		 13397,		/* A1 = -0.817688 */
		 -32615,	/* A2 = 0.995361 */
		 9454,		/* B2 = 0.288528 */
		 -3981,		/* B1 = -0.243027 */
		 9454,		/* B0 = 0.288528 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f1600 */
		10046,		/* A1 = -0.613190 */
		 -32331,	/* A2 = 0.986694 */
		 -455,		/* B2 = -0.013915 */
		 0,		/* B1 = 0.000000 */
		 455,		/* B0 = 0.013915 */
		 9694,		/* A1 = -0.591705 */
		 -32601,	/* A2 = 0.994934 */
		 6023,		/* B2 = 0.183815 */
		 -1708,		/* B1 = -0.104279 */
		 6023,		/* B0 = 0.183815 */
		 10478,		/* A1 = -0.639587 */
		 -32603,	/* A2 = 0.994965 */
		 22031,		/* B2 = 0.672333 */
		 -7342,		/* B1 = -0.448151 */
		 22031,		/* B0 = 0.672333 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f1633_1638[] */
		9181,		/* A1 = 0.560394 */
		 -32256,	/* A2 = -0.984375 */
		 -556,		/* B2 = -0.016975 */
		 0,		/* B1 = 0 */
		 556,		/* B0 = 0.016975 */
		 8757,		/* A1 = 0.534515 */
		 -32574,	/* A2 = -0.99408 */
		 8443,		/* B2 = 0.25769 */
		 -2135,		/* B1 = -0.130341 */
		 8443,		/* B0 = 0.25769 */
		 9691,		/* A1 = 0.591522 */
		 -32574,	/* A2 = -0.99411 */
		 15446,		/* B2 = 0.471375 */
		 -4809,		/* B1 = -0.293579 */
		 15446,		/* B0 = 0.471375 */
		 7,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f1800 */
		5076,		/* A1 = -0.309875 */
		 -32304,	/* A2 = 0.985840 */
		 -508,		/* B2 = -0.015503 */
		 0,		/* B1 = 0.000000 */
		 508,		/* B0 = 0.015503 */
		 4646,		/* A1 = -0.283600 */
		 -32605,	/* A2 = 0.995026 */
		 6742,		/* B2 = 0.205780 */
		 -878,		/* B1 = -0.053635 */
		 6742,		/* B0 = 0.205780 */
		 5552,		/* A1 = -0.338928 */
		 -32605,	/* A2 = 0.995056 */
		 23667,		/* B2 = 0.722260 */
		 -4297,		/* B1 = -0.262329 */
		 23667,		/* B0 = 0.722260 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
	{			/* f1860 */
		3569,		/* A1 = -0.217865 */
		 -32292,	/* A2 = 0.985504 */
		 -239,		/* B2 = -0.007322 */
		 0,		/* B1 = 0.000000 */
		 239,		/* B0 = 0.007322 */
		 3117,		/* A1 = -0.190277 */
		 -32603,	/* A2 = 0.994965 */
		 18658,		/* B2 = 0.569427 */
		 -1557,		/* B1 = -0.095032 */
		 18658,		/* B0 = 0.569427 */
		 4054,		/* A1 = -0.247437 */
		 -32603,	/* A2 = 0.994965 */
		 18886,		/* B2 = 0.576385 */
		 -2566,		/* B1 = -0.156647 */
		 18886,		/* B0 = 0.576385 */
		 5,		/* Internal filter scaling */
		 159,		/* Minimum in-band energy threshold */
		 21,		/* 21/32 in-band to broad-band ratio */
		 0x0FF5		/* shift-mask 0x0FF (look at 16 half-frames) bit count = 5 */
	},
};
static int ixj_init_filter(IXJ *j, IXJ_FILTER * jf)
{
	unsigned short cmd;
	int cnt, max;

	if (jf->filter > 3) {
		return -1;
	}
	if (ixj_WriteDSPCommand(0x5154 + jf->filter, j))	/* Select Filter */

		return -1;
	if (!jf->enable) {
		if (ixj_WriteDSPCommand(0x5152, j))		/* Disable Filter */

			return -1;
		else
			return 0;
	} else {
		if (ixj_WriteDSPCommand(0x5153, j))		/* Enable Filter */

			return -1;
		/* Select the filter (f0 - f3) to use. */
		if (ixj_WriteDSPCommand(0x5154 + jf->filter, j))
			return -1;
	}
	if (jf->freq < 12 && jf->freq > 3) {
		/* Select the frequency for the selected filter. */
		if (ixj_WriteDSPCommand(0x5170 + jf->freq, j))
			return -1;
	} else if (jf->freq > 11) {
		/* We need to load a programmable filter set for undefined */
		/* frequencies.  So we will point the filter to a programmable set. */
		/* Since there are only 4 filters and 4 programmable sets, we will */
		/* just point the filter to the same number set and program it for the */
		/* frequency we want. */
		if (ixj_WriteDSPCommand(0x5170 + jf->filter, j))
			return -1;
		if (j->ver.low != 0x12) {
			cmd = 0x515B;
			max = 19;
		} else {
			cmd = 0x515E;
			max = 15;
		}
		if (ixj_WriteDSPCommand(cmd, j))
			return -1;
		for (cnt = 0; cnt < max; cnt++) {
			if (ixj_WriteDSPCommand(tone_table[jf->freq - 12][cnt], j))
				return -1;
		}
	}
	j->filter_en[jf->filter] = jf->enable;
	return 0;
}

static int ixj_init_filter_raw(IXJ *j, IXJ_FILTER_RAW * jfr)
{
	unsigned short cmd;
	int cnt, max;
	if (jfr->filter > 3) {
		return -1;
	}
	if (ixj_WriteDSPCommand(0x5154 + jfr->filter, j))	/* Select Filter */
		return -1;

	if (!jfr->enable) {
		if (ixj_WriteDSPCommand(0x5152, j))		/* Disable Filter */
			return -1;
		else
			return 0;
	} else {
		if (ixj_WriteDSPCommand(0x5153, j))		/* Enable Filter */
			return -1;
		/* Select the filter (f0 - f3) to use. */
		if (ixj_WriteDSPCommand(0x5154 + jfr->filter, j))
			return -1;
	}
	/* We need to load a programmable filter set for undefined */
	/* frequencies.  So we will point the filter to a programmable set. */
	/* Since there are only 4 filters and 4 programmable sets, we will */
	/* just point the filter to the same number set and program it for the */
	/* frequency we want. */
	if (ixj_WriteDSPCommand(0x5170 + jfr->filter, j))
		return -1;
	if (j->ver.low != 0x12) {
		cmd = 0x515B;
		max = 19;
	} else {
		cmd = 0x515E;
		max = 15;
	}
	if (ixj_WriteDSPCommand(cmd, j))
		return -1;
	for (cnt = 0; cnt < max; cnt++) {
		if (ixj_WriteDSPCommand(jfr->coeff[cnt], j))
			return -1;
	}
	j->filter_en[jfr->filter] = jfr->enable;
	return 0;
}

static int ixj_init_tone(IXJ *j, IXJ_TONE * ti)
{
	int freq0, freq1;
	unsigned short data;
	if (ti->freq0) {
		freq0 = ti->freq0;
	} else {
		freq0 = 0x7FFF;
	}

	if (ti->freq1) {
		freq1 = ti->freq1;
	} else {
		freq1 = 0x7FFF;
	}

	if(ti->tone_index > 12 && ti->tone_index < 28)
	{
		if (ixj_WriteDSPCommand(0x6800 + ti->tone_index, j))
			return -1;
		if (ixj_WriteDSPCommand(0x6000 + (ti->gain1 << 4) + ti->gain0, j))
			return -1;
		data = freq0;
		if (ixj_WriteDSPCommand(data, j))
			return -1;
		data = freq1;
		if (ixj_WriteDSPCommand(data, j))
			return -1;
	}
	return freq0;
}

