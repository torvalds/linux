/*
 *	Wavelan Pcmcia driver
 *
 *		Jean II - HPLB '96
 *
 * Reorganisation and extension of the driver.
 *
 * This file contain all definition and declarations necessary for the
 * wavelan pcmcia driver. This file is a private header, so it should
 * be included only on wavelan_cs.c !!!
 */

#ifndef WAVELAN_CS_P_H
#define WAVELAN_CS_P_H

/************************** DOCUMENTATION **************************/
/*
 * This driver provide a Linux interface to the Wavelan Pcmcia hardware
 * The Wavelan is a product of Lucent (http://www.wavelan.com/).
 * This division was formerly part of NCR and then AT&T.
 * Wavelan are also distributed by DEC (RoamAbout DS)...
 *
 * To know how to use this driver, read the PCMCIA HOWTO.
 * If you want to exploit the many other fonctionalities, look comments
 * in the code...
 *
 * This driver is the result of the effort of many peoples (see below).
 */

/* ------------------------ SPECIFIC NOTES ------------------------ */
/*
 * Web page
 * --------
 *	I try to maintain a web page with the Wireless LAN Howto at :
 *	    http://www.hpl.hp.com/personal/Jean_Tourrilhes/Linux/Wavelan.html
 *
 * SMP
 * ---
 *	We now are SMP compliant (I eventually fixed the remaining bugs).
 *	The driver has been tested on a dual P6-150 and survived my usual
 *	set of torture tests.
 *	Anyway, I spent enough time chasing interrupt re-entrancy during
 *	errors or reconfigure, and I designed the locked/unlocked sections
 *	of the driver with great care, and with the recent addition of
 *	the spinlock (thanks to the new API), we should be quite close to
 *	the truth.
 *	The SMP/IRQ locking is quite coarse and conservative (i.e. not fast),
 *	but better safe than sorry (especially at 2 Mb/s ;-).
 *
 *	I have also looked into disabling only our interrupt on the card
 *	(via HACR) instead of all interrupts in the processor (via cli),
 *	so that other driver are not impacted, and it look like it's
 *	possible, but it's very tricky to do right (full of races). As
 *	the gain would be mostly for SMP systems, it can wait...
 *
 * Debugging and options
 * ---------------------
 *	You will find below a set of '#define" allowing a very fine control
 *	on the driver behaviour and the debug messages printed.
 *	The main options are :
 *	o WAVELAN_ROAMING, for the experimental roaming support.
 *	o SET_PSA_CRC, to have your card correctly recognised by
 *	  an access point and the Point-to-Point diagnostic tool.
 *	o USE_PSA_CONFIG, to read configuration from the PSA (EEprom)
 *	  (otherwise we always start afresh with some defaults)
 *
 * wavelan_cs.o is darn too big
 * -------------------------
 *	That's true ! There is a very simple way to reduce the driver
 *	object by 33% (yes !). Comment out the following line :
 *		#include <linux/wireless.h>
 *	Other compile options can also reduce the size of it...
 *
 * MAC address and hardware detection :
 * ----------------------------------
 *	The detection code of the wavelan chech that the first 3
 *	octets of the MAC address fit the company code. This type of
 *	detection work well for AT&T cards (because the AT&T code is
 *	hardcoded in wavelan_cs.h), but of course will fail for other
 *	manufacturer.
 *
 *	If you are sure that your card is derived from the wavelan,
 *	here is the way to configure it :
 *	1) Get your MAC address
 *		a) With your card utilities (wfreqsel, instconf, ...)
 *		b) With the driver :
 *			o compile the kernel with DEBUG_CONFIG_INFO enabled
 *			o Boot and look the card messages
 *	2) Set your MAC code (3 octets) in MAC_ADDRESSES[][3] (wavelan_cs.h)
 *	3) Compile & verify
 *	4) Send me the MAC code - I will include it in the next version...
 *
 */

/* --------------------- WIRELESS EXTENSIONS --------------------- */
/*
 * This driver is the first one to support "wireless extensions".
 * This set of extensions provide you some way to control the wireless
 * caracteristics of the hardware in a standard way and support for
 * applications for taking advantage of it (like Mobile IP).
 *
 * It might be a good idea as well to fetch the wireless tools to
 * configure the device and play a bit.
 */

/* ---------------------------- FILES ---------------------------- */
/*
 * wavelan_cs.c :	The actual code for the driver - C functions
 *
 * wavelan_cs.p.h :	Private header : local types / vars for the driver
 *
 * wavelan_cs.h :	Description of the hardware interface & structs
 *
 * i82593.h :		Description if the Ethernet controller
 */

/* --------------------------- HISTORY --------------------------- */
/*
 * The history of the Wavelan drivers is as complicated as history of
 * the Wavelan itself (NCR -> AT&T -> Lucent).
 *
 * All started with Anders Klemets <klemets@paul.rutgers.edu>,
 * writing a Wavelan ISA driver for the MACH microkernel. Girish
 * Welling <welling@paul.rutgers.edu> had also worked on it.
 * Keith Moore modify this for the Pcmcia hardware.
 * 
 * Robert Morris <rtm@das.harvard.edu> port these two drivers to BSDI
 * and add specific Pcmcia support (there is currently no equivalent
 * of the PCMCIA package under BSD...).
 *
 * Jim Binkley <jrb@cs.pdx.edu> port both BSDI drivers to FreeBSD.
 *
 * Bruce Janson <bruce@cs.usyd.edu.au> port the BSDI ISA driver to Linux.
 *
 * Anthony D. Joseph <adj@lcs.mit.edu> started modify Bruce driver
 * (with help of the BSDI PCMCIA driver) for PCMCIA.
 * Yunzhou Li <yunzhou@strat.iol.unh.edu> finished is work.
 * Joe Finney <joe@comp.lancs.ac.uk> patched the driver to start
 * correctly 2.00 cards (2.4 GHz with frequency selection).
 * David Hinds <dahinds@users.sourceforge.net> integrated the whole in his
 * Pcmcia package (+ bug corrections).
 *
 * I (Jean Tourrilhes - jt@hplb.hpl.hp.com) then started to make some
 * patchs to the Pcmcia driver. After, I added code in the ISA driver
 * for Wireless Extensions and full support of frequency selection
 * cards. Now, I'm doing the same to the Pcmcia driver + some
 * reorganisation.
 * Loeke Brederveld <lbrederv@wavelan.com> from Lucent has given me
 * much needed informations on the Wavelan hardware.
 */

/* By the way : for the copyright & legal stuff :
 * Almost everybody wrote code under GNU or BSD license (or alike),
 * and want that their original copyright remain somewhere in the
 * code (for myself, I go with the GPL).
 * Nobody want to take responsibility for anything, except the fame...
 */

/* --------------------------- CREDITS --------------------------- */
/*
 * Credits:
 *    Special thanks to Jan Hoogendoorn of AT&T GIS Utrecht and
 *	Loeke Brederveld of Lucent for providing extremely useful
 *	information about WaveLAN PCMCIA hardware
 *
 *    This driver is based upon several other drivers, in particular:
 *	David Hinds' Linux driver for the PCMCIA 3c589 ethernet adapter
 *	Bruce Janson's Linux driver for the AT-bus WaveLAN adapter
 *	Anders Klemets' PCMCIA WaveLAN adapter driver
 *	Robert Morris' BSDI driver for the PCMCIA WaveLAN adapter
 *
 * Additional Credits:
 *
 *    This software was originally developed under Linux 1.2.3
 *	(Slackware 2.0 distribution).
 *    And then under Linux 2.0.x (Debian 1.1 -> 2.2 - pcmcia 2.8.18+)
 *	with an HP OmniBook 4000 and then a 5500.
 *
 *    It is based on other device drivers and information either written
 *    or supplied by:
 *	James Ashton (jaa101@syseng.anu.edu.au),
 *	Ajay Bakre (bakre@paul.rutgers.edu),
 *	Donald Becker (becker@super.org),
 *	Jim Binkley <jrb@cs.pdx.edu>,
 *	Loeke Brederveld <lbrederv@wavelan.com>,
 *	Allan Creighton (allanc@cs.su.oz.au),
 *	Brent Elphick <belphick@uwaterloo.ca>,
 *	Joe Finney <joe@comp.lancs.ac.uk>,
 *	Matthew Geier (matthew@cs.su.oz.au),
 *	Remo di Giovanni (remo@cs.su.oz.au),
 *	Mark Hagan (mhagan@wtcpost.daytonoh.NCR.COM),
 *	David Hinds <dahinds@users.sourceforge.net>,
 *	Jan Hoogendoorn (c/o marteijn@lucent.com),
 *      Bruce Janson <bruce@cs.usyd.edu.au>,
 *	Anthony D. Joseph <adj@lcs.mit.edu>,
 *	Anders Klemets (klemets@paul.rutgers.edu),
 *	Yunzhou Li <yunzhou@strat.iol.unh.edu>,
 *	Marc Meertens (mmeertens@lucent.com),
 *	Keith Moore,
 *	Robert Morris (rtm@das.harvard.edu),
 *	Ian Parkin (ian@cs.su.oz.au),
 *	John Rosenberg (johnr@cs.su.oz.au),
 *	George Rossi (george@phm.gov.au),
 *	Arthur Scott (arthur@cs.su.oz.au),
 *	Stanislav Sinyagin <stas@isf.ru>
 *	Peter Storey,
 *	Jean Tourrilhes <jt@hpl.hp.com>,
 *	Girish Welling (welling@paul.rutgers.edu)
 *	Clark Woodworth <clark@hiway1.exit109.com>
 *	Yongguang Zhang <ygz@isl.hrl.hac.com>...
 */

/* ------------------------- IMPROVEMENTS ------------------------- */
/*
 * I proudly present :
 *
 * Changes made in 2.8.22 :
 * ----------------------
 *	- improved wv_set_multicast_list
 *	- catch spurious interrupt
 *	- correct release of the device
 *
 * Changes mades in release :
 * ------------------------
 *	- Reorganisation of the code, function name change
 *	- Creation of private header (wavelan_cs.h)
 *	- Reorganised debug messages
 *	- More comments, history, ...
 *	- Configure earlier (in "insert" instead of "open")
 *        and do things only once
 *	- mmc_init : configure the PSA if not done
 *	- mmc_init : 2.00 detection better code for 2.00 init
 *	- better info at startup
 *	- Correct a HUGE bug (volatile & uncalibrated busy loop)
 *	  in wv_82593_cmd => config speedup
 *	- Stop receiving & power down on close (and power up on open)
 *	  use "ifconfig down" & "ifconfig up ; route add -net ..."
 *	- Send packets : add watchdog instead of pooling
 *	- Receive : check frame wrap around & try to recover some frames
 *	- wavelan_set_multicast_list : avoid reset
 *	- add wireless extensions (ioctl & get_wireless_stats)
 *	  get/set nwid/frequency on fly, info for /proc/net/wireless
 *	- Suppress useless stuff from lp (net_local), but add link
 *	- More inlines
 *	- Lot of others minor details & cleanups
 *
 * Changes made in second release :
 * ------------------------------
 *	- Optimise wv_85893_reconfig stuff, fix potential problems
 *	- Change error values for ioctl
 *	- Non blocking wv_ru_stop() + call wv_reset() in case of problems
 *	- Remove development printk from wavelan_watchdog()
 *	- Remove of the watchdog to wavelan_close instead of wavelan_release
 *	  fix potential problems...
 *	- Start debugging suspend stuff (but it's still a bit weird)
 *	- Debug & optimize dump header/packet in Rx & Tx (debug)
 *	- Use "readb" and "writeb" to be kernel 2.1 compliant
 *	- Better handling of bogus interrupts
 *	- Wireless extension : SETSPY and GETSPY
 *	- Remove old stuff (stats - for those needing it, just ask me...)
 *	- Make wireless extensions optional
 *
 * Changes made in third release :
 * -----------------------------
 *	- cleanups & typos
 *	- modif wireless ext (spy -> only one pointer)
 *	- new private ioctl to set/get quality & level threshold
 *	- Init : correct default value of level threshold for pcmcia
 *	- kill watchdog in hw_reset
 *	- more 2.1 support (copy_to/from_user instead of memcpy_to/fromfs)
 *	- Add message level (debug stuff in /var/adm/debug & errors not
 *	  displayed at console and still in /var/adm/messages)
 *
 * Changes made in fourth release :
 * ------------------------------
 *	- multicast support (yes !) thanks to Yongguang Zhang.
 *
 * Changes made in fifth release (2.9.0) :
 * -------------------------------------
 *	- Revisited multicast code (it was mostly wrong).
 *	- protect code in wv_82593_reconfig with dev->tbusy (oups !)
 *
 * Changes made in sixth release (2.9.1a) :
 * --------------------------------------
 *	- Change the detection code for multi manufacturer code support
 *	- Correct bug (hang kernel) in init when we were "rejecting" a card 
 *
 * Changes made in seventh release (2.9.1b) :
 * ----------------------------------------
 *	- Update to wireless extensions changes
 *	- Silly bug in card initial configuration (psa_conf_status)
 *
 * Changes made in eigth release :
 * -----------------------------
 *	- Small bug in debug code (probably not the last one...)
 *	- 1.2.13 support (thanks to Clark Woodworth)
 *
 * Changes made for release in 2.9.2b :
 * ----------------------------------
 *	- Level threshold is now a standard wireless extension (version 4 !)
 *	- modules parameters types for kernel > 2.1.17
 *	- updated man page
 *	- Others cleanup from David Hinds
 *
 * Changes made for release in 2.9.5 :
 * ---------------------------------
 *	- byte count stats (courtesy of David Hinds)
 *	- Remove dev_tint stuff (courtesy of David Hinds)
 *	- Others cleanup from David Hinds
 *	- Encryption setting from Brent Elphick (thanks a lot !)
 *	- 'base' to 'u_long' for the Alpha (thanks to Stanislav Sinyagin)
 *
 * Changes made for release in 2.9.6 :
 * ---------------------------------
 *	- fix bug : no longuer disable watchdog in case of bogus interrupt
 *	- increase timeout in config code for picky hardware
 *	- mask unused bits in status (Wireless Extensions)
 *
 * Changes integrated by Justin Seger <jseger@MIT.EDU> & David Hinds :
 * -----------------------------------------------------------------
 *	- Roaming "hack" from Joe Finney <joe@comp.lancs.ac.uk>
 *	- PSA CRC code from Bob Gray <rgray@bald.cs.dartmouth.edu>
 *	- Better initialisation of the i82593 controller
 *	  from Joseph K. O'Sullivan <josullvn+@cs.cmu.edu>
 *
 * Changes made for release in 3.0.10 :
 * ----------------------------------
 *	- Fix eject "hang" of the driver under 2.2.X :
 *		o create wv_flush_stale_links()
 *		o Rename wavelan_release to wv_pcmcia_release & move up
 *		o move unregister_netdev to wavelan_detach()
 *		o wavelan_release() no longer call wavelan_detach()
 *		o Suppress "release" timer
 *		o Other cleanups & fixes
 *	- New MAC address in the probe
 *	- Reorg PSA_CRC code (endian neutral & cleaner)
 *	- Correct initialisation of the i82593 from Lucent manual
 *	- Put back the watchdog, with larger timeout
 *	- TRANSMIT_NO_CRC is a "normal" error, so recover from it
 *	  from Derrick J Brashear <shadow@dementia.org>
 *	- Better handling of TX and RX normal failure conditions
 *	- #ifdef out all the roaming code
 *	- Add ESSID & "AP current address" ioctl stubs
 *	- General cleanup of the code
 *
 * Changes made for release in 3.0.13 :
 * ----------------------------------
 *	- Re-enable compilation of roaming code by default, but with
 *	  do_roaming = 0
 *	- Nuke `nwid=nwid^ntohs(beacon->domain_id)' in wl_roam_gather
 *	  at the demand of John Carol Langford <jcl@gs176.sp.cs.cmu.edu>
 *	- Introduced WAVELAN_ROAMING_EXT for incomplete ESSID stuff.
 *
 * Changes made for release in 3.0.15 :
 * ----------------------------------
 *	- Change e-mail and web page addresses
 *	- Watchdog timer is now correctly expressed in HZ, not in jiffies
 *	- Add channel number to the list of frequencies in range
 *	- Add the (short) list of bit-rates in range
 *	- Developp a new sensitivity... (sens.value & sens.fixed)
 *
 * Changes made for release in 3.1.2 :
 * ---------------------------------
 *	- Fix check for root permission (break instead of exit)
 *	- New nwid & encoding setting (Wireless Extension 9)
 *
 * Changes made for release in 3.1.12 :
 * ----------------------------------
 *	- reworked wv_82593_cmd to avoid using the IRQ handler and doing
 *	  ugly things with interrupts.
 *	- Add IRQ protection in 82593_config/ru_start/ru_stop/watchdog
 *	- Update to new network API (softnet - 2.3.43) :
 *		o replace dev->tbusy (David + me)
 *		o replace dev->tstart (David + me)
 *		o remove dev->interrupt (David)
 *		o add SMP locking via spinlock in splxx (me)
 *		o add spinlock in interrupt handler (me)
 *		o use kernel watchdog instead of ours (me)
 *		o verify that all the changes make sense and work (me)
 *	- Re-sync kernel/pcmcia versions (not much actually)
 *	- A few other cleanups (David & me)...
 *
 * Changes made for release in 3.1.22 :
 * ----------------------------------
 *	- Check that SMP works, remove annoying log message
 *
 * Changes made for release in 3.1.24 :
 * ----------------------------------
 *	- Fix unfrequent card lockup when watchdog was reseting the hardware :
 *		o control first busy loop in wv_82593_cmd()
 *		o Extend spinlock protection in wv_hw_config()
 *
 * Changes made for release in 3.1.33 :
 * ----------------------------------
 *	- Optional use new driver API for Wireless Extensions :
 *		o got rid of wavelan_ioctl()
 *		o use a bunch of iw_handler instead
 *
 * Changes made for release in 3.2.1 :
 * ---------------------------------
 *	- Set dev->trans_start to avoid filling the logs
 *		(and generating useless abort commands)
 *	- Avoid deadlocks in mmc_out()/mmc_in()
 *
 * Wishes & dreams:
 * ----------------
 *	- Cleanup and integrate the roaming code
 *	  (std debug, set DomainID, decay avg and co...)
 */

/***************************** INCLUDES *****************************/

/* Linux headers that we need */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/system.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/ethtool.h>
#include <linux/wireless.h>		/* Wireless extensions */
#include <net/iw_handler.h>		/* New driver API */

/* Pcmcia headers that we need */
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

/* Wavelan declarations */
#include "i82593.h"	/* Definitions for the Intel chip */

#include "wavelan_cs.h"	/* Others bits of the hardware */

/************************** DRIVER OPTIONS **************************/
/*
 * `#define' or `#undef' the following constant to change the behaviour
 * of the driver...
 */
#define WAVELAN_ROAMING		/* Include experimental roaming code */
#undef WAVELAN_ROAMING_EXT	/* Enable roaming wireless extensions */
#undef SET_PSA_CRC		/* Set the CRC in PSA (slower) */
#define USE_PSA_CONFIG		/* Use info from the PSA */
#undef EEPROM_IS_PROTECTED	/* Doesn't seem to be necessary */
#define MULTICAST_AVOID		/* Avoid extra multicast (I'm sceptical) */
#undef SET_MAC_ADDRESS		/* Experimental */

/* Warning : these stuff will slow down the driver... */
#define WIRELESS_SPY		/* Enable spying addresses */
#undef HISTOGRAM		/* Enable histogram of sig level... */

/****************************** DEBUG ******************************/

#undef DEBUG_MODULE_TRACE	/* Module insertion/removal */
#undef DEBUG_CALLBACK_TRACE	/* Calls made by Linux */
#undef DEBUG_INTERRUPT_TRACE	/* Calls to handler */
#undef DEBUG_INTERRUPT_INFO	/* type of interrupt & so on */
#define DEBUG_INTERRUPT_ERROR	/* problems */
#undef DEBUG_CONFIG_TRACE	/* Trace the config functions */
#undef DEBUG_CONFIG_INFO	/* What's going on... */
#define DEBUG_CONFIG_ERRORS	/* Errors on configuration */
#undef DEBUG_TX_TRACE		/* Transmission calls */
#undef DEBUG_TX_INFO		/* Header of the transmitted packet */
#undef DEBUG_TX_FAIL		/* Normal failure conditions */
#define DEBUG_TX_ERROR		/* Unexpected conditions */
#undef DEBUG_RX_TRACE		/* Transmission calls */
#undef DEBUG_RX_INFO		/* Header of the transmitted packet */
#undef DEBUG_RX_FAIL		/* Normal failure conditions */
#define DEBUG_RX_ERROR		/* Unexpected conditions */
#undef DEBUG_PACKET_DUMP	/* Dump packet on the screen */
#undef DEBUG_IOCTL_TRACE	/* Misc call by Linux */
#undef DEBUG_IOCTL_INFO		/* Various debug info */
#define DEBUG_IOCTL_ERROR	/* What's going wrong */
#define DEBUG_BASIC_SHOW	/* Show basic startup info */
#undef DEBUG_VERSION_SHOW	/* Print version info */
#undef DEBUG_PSA_SHOW		/* Dump psa to screen */
#undef DEBUG_MMC_SHOW		/* Dump mmc to screen */
#undef DEBUG_SHOW_UNUSED	/* Show also unused fields */
#undef DEBUG_I82593_SHOW	/* Show i82593 status */
#undef DEBUG_DEVICE_SHOW	/* Show device parameters */

/************************ CONSTANTS & MACROS ************************/

#ifdef DEBUG_VERSION_SHOW
static const char *version = "wavelan_cs.c : v24 (SMP + wireless extensions) 11/1/02\n";
#endif

/* Watchdog temporisation */
#define	WATCHDOG_JIFFIES	(256*HZ/100)

/* Fix a bug in some old wireless extension definitions */
#ifndef IW_ESSID_MAX_SIZE
#define IW_ESSID_MAX_SIZE	32
#endif

/* ------------------------ PRIVATE IOCTL ------------------------ */

#define SIOCSIPQTHR	SIOCIWFIRSTPRIV		/* Set quality threshold */
#define SIOCGIPQTHR	SIOCIWFIRSTPRIV + 1	/* Get quality threshold */
#define SIOCSIPROAM     SIOCIWFIRSTPRIV + 2	/* Set roaming state */
#define SIOCGIPROAM     SIOCIWFIRSTPRIV + 3	/* Get roaming state */

#define SIOCSIPHISTO	SIOCIWFIRSTPRIV + 4	/* Set histogram ranges */
#define SIOCGIPHISTO	SIOCIWFIRSTPRIV + 5	/* Get histogram values */

/*************************** WaveLAN Roaming  **************************/
#ifdef WAVELAN_ROAMING		/* Conditional compile, see above in options */

#define WAVELAN_ROAMING_DEBUG	 0	/* 1 = Trace of handover decisions */
					/* 2 = Info on each beacon rcvd... */
#define MAX_WAVEPOINTS		7	/* Max visible at one time */
#define WAVEPOINT_HISTORY	5	/* SNR sample history slow search */
#define WAVEPOINT_FAST_HISTORY	2	/* SNR sample history fast search */
#define SEARCH_THRESH_LOW	10	/* SNR to enter cell search */
#define SEARCH_THRESH_HIGH	13	/* SNR to leave cell search */
#define WAVELAN_ROAMING_DELTA	1	/* Hysteresis value (+/- SNR) */
#define CELL_TIMEOUT		2*HZ	/* in jiffies */

#define FAST_CELL_SEARCH	1	/* Boolean values... */
#define NWID_PROMISC		1	/* for code clarity. */

typedef struct wavepoint_beacon
{
  unsigned char		dsap,		/* Unused */
			ssap,		/* Unused */
			ctrl,		/* Unused */
			O,U,I,		/* Unused */
			spec_id1,	/* Unused */
			spec_id2,	/* Unused */
			pdu_type,	/* Unused */
			seq;		/* WavePoint beacon sequence number */
  __be16		domain_id,	/* WavePoint Domain ID */
			nwid;		/* WavePoint NWID */
} wavepoint_beacon;

typedef struct wavepoint_history
{
  unsigned short	nwid;		/* WavePoint's NWID */
  int			average_slow;	/* SNR running average */
  int			average_fast;	/* SNR running average */
  unsigned char	  sigqual[WAVEPOINT_HISTORY]; /* Ringbuffer of recent SNR's */
  unsigned char		qualptr;	/* Index into ringbuffer */
  unsigned char		last_seq;	/* Last seq. no seen for WavePoint */
  struct wavepoint_history *next;	/* Next WavePoint in table */
  struct wavepoint_history *prev;	/* Previous WavePoint in table */
  unsigned long		last_seen;	/* Time of last beacon recvd, jiffies */
} wavepoint_history;

struct wavepoint_table
{
  wavepoint_history	*head;		/* Start of ringbuffer */
  int			num_wavepoints;	/* No. of WavePoints visible */
  unsigned char		locked;		/* Table lock */
};

#endif	/* WAVELAN_ROAMING */

/****************************** TYPES ******************************/

/* Shortcuts */
typedef struct net_device_stats	en_stats;
typedef struct iw_statistics	iw_stats;
typedef struct iw_quality	iw_qual;
typedef struct iw_freq		iw_freq;
typedef struct net_local	net_local;
typedef struct timer_list	timer_list;

/* Basic types */
typedef u_char		mac_addr[WAVELAN_ADDR_SIZE];	/* Hardware address */

/*
 * Static specific data for the interface.
 *
 * For each network interface, Linux keep data in two structure. "device"
 * keep the generic data (same format for everybody) and "net_local" keep
 * the additional specific data.
 * Note that some of this specific data is in fact generic (en_stats, for
 * example).
 */
struct net_local
{
  dev_node_t 	node;		/* ???? What is this stuff ???? */
  struct net_device *	dev;		/* Reverse link... */
  spinlock_t	spinlock;	/* Serialize access to the hardware (SMP) */
  struct pcmcia_device *	link;		/* pcmcia structure */
  en_stats	stats;		/* Ethernet interface statistics */
  int		nresets;	/* Number of hw resets */
  u_char	configured;	/* If it is configured */
  u_char	reconfig_82593;	/* Need to reconfigure the controller */
  u_char	promiscuous;	/* Promiscuous mode */
  u_char	allmulticast;	/* All Multicast mode */
  int		mc_count;	/* Number of multicast addresses */

  int   	stop;		/* Current i82593 Stop Hit Register */
  int   	rfp;		/* Last DMA machine receive pointer */
  int		overrunning;	/* Receiver overrun flag */

  iw_stats	wstats;		/* Wireless specific stats */

  struct iw_spy_data	spy_data;
  struct iw_public_data	wireless_data;

#ifdef HISTOGRAM
  int		his_number;		/* Number of intervals */
  u_char	his_range[16];		/* Boundaries of interval ]n-1; n] */
  u_long	his_sum[16];		/* Sum in interval */
#endif	/* HISTOGRAM */
#ifdef WAVELAN_ROAMING
  u_long	domain_id;	/* Domain ID we lock on for roaming */
  int		filter_domains;	/* Check Domain ID of beacon found */
 struct wavepoint_table	wavepoint_table;	/* Table of visible WavePoints*/
  wavepoint_history *	curr_point;		/* Current wavepoint */
  int			cell_search;		/* Searching for new cell? */
  struct timer_list	cell_timer;		/* Garbage collection */
#endif	/* WAVELAN_ROAMING */
  void __iomem *mem;
};

/* ----------------- MODEM MANAGEMENT SUBROUTINES ----------------- */
static inline u_char		/* data */
	hasr_read(u_long);	/* Read the host interface : base address */
static inline void
	hacr_write(u_long,	/* Write to host interface : base address */
		   u_char),	/* data */
	hacr_write_slow(u_long,
		   u_char);
static void
	psa_read(struct net_device *,	/* Read the Parameter Storage Area */
		 int,		/* offset in PSA */
		 u_char *,	/* buffer to fill */
		 int),		/* size to read */
	psa_write(struct net_device *,	/* Write to the PSA */
		  int,		/* Offset in psa */
		  u_char *,	/* Buffer in memory */
		  int);		/* Length of buffer */
static inline void
	mmc_out(u_long,		/* Write 1 byte to the Modem Manag Control */
		u_short,
		u_char),
	mmc_write(u_long,	/* Write n bytes to the MMC */
		  u_char,
		  u_char *,
		  int);
static inline u_char		/* Read 1 byte from the MMC */
	mmc_in(u_long,
	       u_short);
static inline void
	mmc_read(u_long,	/* Read n bytes from the MMC */
		 u_char,
		 u_char *,
		 int),
	fee_wait(u_long,	/* Wait for frequency EEprom : base address */
		 int,		/* Base delay to wait for */
		 int);		/* Number of time to wait */
static void
	fee_read(u_long,	/* Read the frequency EEprom : base address */
		 u_short,	/* destination offset */
		 u_short *,	/* data buffer */
		 int);		/* number of registers */
/* ---------------------- I82593 SUBROUTINES ----------------------- */
static int
	wv_82593_cmd(struct net_device *,	/* synchronously send a command to i82593 */ 
		     char *,
		     int,
		     int);
static inline int
	wv_diag(struct net_device *);	/* Diagnostique the i82593 */
static int
	read_ringbuf(struct net_device *,	/* Read a receive buffer */
		     int,
		     char *,
		     int);
static inline void
	wv_82593_reconfig(struct net_device *);	/* Reconfigure the controller */
/* ------------------- DEBUG & INFO SUBROUTINES ------------------- */
static inline void
	wv_init_info(struct net_device *);	/* display startup info */
/* ------------------- IOCTL, STATS & RECONFIG ------------------- */
static en_stats	*
	wavelan_get_stats(struct net_device *);	/* Give stats /proc/net/dev */
static iw_stats *
	wavelan_get_wireless_stats(struct net_device *);
/* ----------------------- PACKET RECEPTION ----------------------- */
static inline int
	wv_start_of_frame(struct net_device *,	/* Seek beggining of current frame */
			  int,	/* end of frame */
			  int);	/* start of buffer */
static inline void
	wv_packet_read(struct net_device *,	/* Read a packet from a frame */
		       int,
		       int),
	wv_packet_rcv(struct net_device *);	/* Read all packets waiting */
/* --------------------- PACKET TRANSMISSION --------------------- */
static inline void
	wv_packet_write(struct net_device *,	/* Write a packet to the Tx buffer */
			void *,
			short);
static int
	wavelan_packet_xmit(struct sk_buff *,	/* Send a packet */
			    struct net_device *);
/* -------------------- HARDWARE CONFIGURATION -------------------- */
static inline int
	wv_mmc_init(struct net_device *);	/* Initialize the modem */
static int
	wv_ru_stop(struct net_device *),	/* Stop the i82593 receiver unit */
	wv_ru_start(struct net_device *);	/* Start the i82593 receiver unit */
static int
	wv_82593_config(struct net_device *);	/* Configure the i82593 */
static inline int
	wv_pcmcia_reset(struct net_device *);	/* Reset the pcmcia interface */
static int
	wv_hw_config(struct net_device *);	/* Reset & configure the whole hardware */
static inline void
	wv_hw_reset(struct net_device *);	/* Same, + start receiver unit */
static inline int
	wv_pcmcia_config(struct pcmcia_device *);	/* Configure the pcmcia interface */
static void
	wv_pcmcia_release(struct pcmcia_device *);/* Remove a device */
/* ---------------------- INTERRUPT HANDLING ---------------------- */
static irqreturn_t
	wavelan_interrupt(int,	/* Interrupt handler */
			  void *);
static void
	wavelan_watchdog(struct net_device *);	/* Transmission watchdog */
/* ------------------- CONFIGURATION CALLBACKS ------------------- */
static int
	wavelan_open(struct net_device *),		/* Open the device */
	wavelan_close(struct net_device *);	/* Close the device */
static void
	wavelan_detach(struct pcmcia_device *p_dev);	/* Destroy a removed device */

/**************************** VARIABLES ****************************/

/*
 * Parameters that can be set with 'insmod'
 * The exact syntax is 'insmod wavelan_cs.o <var>=<value>'
 */

/* Shared memory speed, in ns */
static int	mem_speed = 0;

/* New module interface */
module_param(mem_speed, int, 0);

#ifdef WAVELAN_ROAMING		/* Conditional compile, see above in options */
/* Enable roaming mode ? No ! Please keep this to 0 */
static int	do_roaming = 0;
module_param(do_roaming, bool, 0);
#endif	/* WAVELAN_ROAMING */

MODULE_LICENSE("GPL");

#endif	/* WAVELAN_CS_P_H */

