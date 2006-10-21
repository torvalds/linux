/*
 *	WaveLAN ISA driver
 *
 *		Jean II - HPLB '96
 *
 * Reorganisation and extension of the driver.
 *
 * This file contains all definitions and declarations necessary for the
 * WaveLAN ISA driver.  This file is a private header, so it should
 * be included only in wavelan.c!
 */

#ifndef WAVELAN_P_H
#define WAVELAN_P_H

/************************** DOCUMENTATION ***************************/
/*
 * This driver provides a Linux interface to the WaveLAN ISA hardware.
 * The WaveLAN is a product of Lucent (http://www.wavelan.com/).
 * This division was formerly part of NCR and then AT&T.
 * WaveLANs are also distributed by DEC (RoamAbout DS) and Digital Ocean.
 *
 * To learn how to use this driver, read the NET3 HOWTO.
 * If you want to exploit the many other functionalities, read the comments
 * in the code.
 *
 * This driver is the result of the effort of many people (see below).
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
 *	o SET_PSA_CRC, to have your card correctly recognised by
 *	  an access point and the Point-to-Point diagnostic tool.
 *	o USE_PSA_CONFIG, to read configuration from the PSA (EEprom)
 *	  (otherwise we always start afresh with some defaults)
 *
 * wavelan.o is too darned big
 * ---------------------------
 *	That's true!  There is a very simple way to reduce the driver
 *	object by 33%!  Comment out the following line:
 *		#include <linux/wireless.h>
 *	Other compile options can also reduce the size of it...
 *
 * MAC address and hardware detection:
 * -----------------------------------
 *	The detection code for the WaveLAN checks that the first three
 *	octets of the MAC address fit the company code.  This type of
 *	detection works well for AT&T cards (because the AT&T code is
 *	hardcoded in wavelan.h), but of course will fail for other
 *	manufacturers.
 *
 *	If you are sure that your card is derived from the WaveLAN,
 *	here is the way to configure it:
 *	1) Get your MAC address
 *		a) With your card utilities (wfreqsel, instconf, etc.)
 *		b) With the driver:
 *			o compile the kernel with DEBUG_CONFIG_INFO enabled
 *			o Boot and look the card messages
 *	2) Set your MAC code (3 octets) in MAC_ADDRESSES[][3] (wavelan.h)
 *	3) Compile and verify
 *	4) Send me the MAC code.  I will include it in the next version.
 *
 */

/* --------------------- WIRELESS EXTENSIONS --------------------- */
/*
 * This driver is the first to support "wireless extensions".
 * This set of extensions provides a standard way to control the wireless
 * characteristics of the hardware.  Applications such as mobile IP may
 * take advantage of it.
 *
 * It might be a good idea as well to fetch the wireless tools to
 * configure the device and play a bit.
 */

/* ---------------------------- FILES ---------------------------- */
/*
 * wavelan.c:		actual code for the driver:  C functions
 *
 * wavelan.p.h:		private header:  local types and variables for driver
 *
 * wavelan.h:		description of the hardware interface and structs
 *
 * i82586.h:		description of the Ethernet controller
 */

/* --------------------------- HISTORY --------------------------- */
/*
 * This is based on information in the drivers' headers. It may not be
 * accurate, and I guarantee only my best effort.
 *
 * The history of the WaveLAN drivers is as complicated as the history of
 * the WaveLAN itself (NCR -> AT&T -> Lucent).
 *
 * It all started with Anders Klemets <klemets@paul.rutgers.edu>
 * writing a WaveLAN ISA driver for the Mach microkernel.  Girish
 * Welling <welling@paul.rutgers.edu> had also worked on it.
 * Keith Moore modified this for the PCMCIA hardware.
 * 
 * Robert Morris <rtm@das.harvard.edu> ported these two drivers to BSDI
 * and added specific PCMCIA support (there is currently no equivalent
 * of the PCMCIA package under BSD).
 *
 * Jim Binkley <jrb@cs.pdx.edu> ported both BSDI drivers to FreeBSD.
 *
 * Bruce Janson <bruce@cs.usyd.edu.au> ported the BSDI ISA driver to Linux.
 *
 * Anthony D. Joseph <adj@lcs.mit.edu> started to modify Bruce's driver
 * (with help of the BSDI PCMCIA driver) for PCMCIA.
 * Yunzhou Li <yunzhou@strat.iol.unh.edu> finished this work.
 * Joe Finney <joe@comp.lancs.ac.uk> patched the driver to start
 * 2.00 cards correctly (2.4 GHz with frequency selection).
 * David Hinds <dahinds@users.sourceforge.net> integrated the whole in his
 * PCMCIA package (and bug corrections).
 *
 * I (Jean Tourrilhes - jt@hplb.hpl.hp.com) then started to make some
 * patches to the PCMCIA driver.  Later, I added code in the ISA driver
 * for Wireless Extensions and full support of frequency selection
 * cards.  Then, I did the same to the PCMCIA driver, and did some
 * reorganisation.  Finally, I came back to the ISA driver to
 * upgrade it at the same level as the PCMCIA one and reorganise
 * the code.
 * Loeke Brederveld <lbrederv@wavelan.com> from Lucent has given me
 * much needed information on the WaveLAN hardware.
 */

/* The original copyrights and literature mention others' names and
 * credits.  I don't know what their part in this development was.
 */

/* By the way, for the copyright and legal stuff:
 * almost everybody wrote code under the GNU or BSD license (or similar),
 * and want their original copyright to remain somewhere in the
 * code (for myself, I go with the GPL).
 * Nobody wants to take responsibility for anything, except the fame.
 */

/* --------------------------- CREDITS --------------------------- */
/*
 * This software was developed as a component of the
 * Linux operating system.
 * It is based on other device drivers and information
 * either written or supplied by:
 *	Ajay Bakre <bakre@paul.rutgers.edu>,
 *	Donald Becker <becker@cesdis.gsfc.nasa.gov>,
 *	Loeke Brederveld <Loeke.Brederveld@Utrecht.NCR.com>,
 *	Brent Elphick <belphick@uwaterloo.ca>,
 *	Anders Klemets <klemets@it.kth.se>,
 *	Vladimir V. Kolpakov <w@stier.koenig.ru>,
 *	Marc Meertens <Marc.Meertens@Utrecht.NCR.com>,
 *	Pauline Middelink <middelin@polyware.iaf.nl>,
 *	Robert Morris <rtm@das.harvard.edu>,
 *	Jean Tourrilhes <jt@hpl.hp.com>,
 *	Girish Welling <welling@paul.rutgers.edu>,
 *	Clark Woodworth <clark@hiway1.exit109.com>
 *	Yongguang Zhang <ygz@isl.hrl.hac.com>
 *
 * Thanks go also to:
 *	James Ashton <jaa101@syseng.anu.edu.au>,
 *	Alan Cox <alan@redhat.com>,
 *	Allan Creighton <allanc@cs.usyd.edu.au>,
 *	Matthew Geier <matthew@cs.usyd.edu.au>,
 *	Remo di Giovanni <remo@cs.usyd.edu.au>,
 *	Eckhard Grah <grah@wrcs1.urz.uni-wuppertal.de>,
 *	Vipul Gupta <vgupta@cs.binghamton.edu>,
 *	Mark Hagan <mhagan@wtcpost.daytonoh.NCR.COM>,
 *	Tim Nicholson <tim@cs.usyd.edu.au>,
 *	Ian Parkin <ian@cs.usyd.edu.au>,
 *	John Rosenberg <johnr@cs.usyd.edu.au>,
 *	George Rossi <george@phm.gov.au>,
 *	Arthur Scott <arthur@cs.usyd.edu.au>,
 *	Stanislav Sinyagin <stas@isf.ru>
 *	and Peter Storey for their assistance and advice.
 *
 * Additional Credits:
 *
 *	My development has been done initially under Debian 1.1 (Linux 2.0.x)
 *	and now	under Debian 2.2, initially with an HP Vectra XP/60, and now
 *	an HP Vectra XP/90.
 *
 */

/* ------------------------- IMPROVEMENTS ------------------------- */
/*
 * I proudly present:
 *
 * Changes made in first pre-release:
 * ----------------------------------
 *	- reorganisation of the code, function name change
 *	- creation of private header (wavelan.p.h)
 *	- reorganised debug messages
 *	- more comments, history, etc.
 *	- mmc_init:  configure the PSA if not done
 *	- mmc_init:  correct default value of level threshold for PCMCIA
 *	- mmc_init:  2.00 detection better code for 2.00 initialization
 *	- better info at startup
 *	- IRQ setting (note:  this setting is permanent)
 *	- watchdog:  change strategy (and solve module removal problems)
 *	- add wireless extensions (ioctl and get_wireless_stats)
 *	  get/set nwid/frequency on fly, info for /proc/net/wireless
 *	- more wireless extensions:  SETSPY and GETSPY
 *	- make wireless extensions optional
 *	- private ioctl to set/get quality and level threshold, histogram
 *	- remove /proc/net/wavelan
 *	- suppress useless stuff from lp (net_local)
 *	- kernel 2.1 support (copy_to/from_user instead of memcpy_to/fromfs)
 *	- add message level (debug stuff in /var/adm/debug and errors not
 *	  displayed at console and still in /var/adm/messages)
 *	- multi device support
 *	- start fixing the probe (init code)
 *	- more inlines
 *	- man page
 *	- many other minor details and cleanups
 *
 * Changes made in second pre-release:
 * -----------------------------------
 *	- clean up init code (probe and module init)
 *	- better multiple device support (module)
 *	- name assignment (module)
 *
 * Changes made in third pre-release:
 * ----------------------------------
 *	- be more conservative on timers
 *	- preliminary support for multicast (I still lack some details)
 *
 * Changes made in fourth pre-release:
 * -----------------------------------
 *	- multicast (revisited and finished)
 *	- avoid reset in set_multicast_list (a really big hack)
 *	  if somebody could apply this code for other i82586 based drivers
 *	- share onboard memory 75% RU and 25% CU (instead of 50/50)
 *
 * Changes made for release in 2.1.15:
 * -----------------------------------
 *	- change the detection code for multi manufacturer code support
 *
 * Changes made for release in 2.1.17:
 * -----------------------------------
 *	- update to wireless extensions changes
 *	- silly bug in card initial configuration (psa_conf_status)
 *
 * Changes made for release in 2.1.27 & 2.0.30:
 * --------------------------------------------
 *	- small bug in debug code (probably not the last one...)
 *	- remove extern keyword for wavelan_probe()
 *	- level threshold is now a standard wireless extension (version 4 !)
 *	- modules parameters types (new module interface)
 *
 * Changes made for release in 2.1.36:
 * -----------------------------------
 *	- byte count stats (courtesy of David Hinds)
 *	- remove dev_tint stuff (courtesy of David Hinds)
 *	- encryption setting from Brent Elphick (thanks a lot!)
 *	- 'ioaddr' to 'u_long' for the Alpha (thanks to Stanislav Sinyagin)
 *
 * Other changes (not by me) :
 * -------------------------
 *	- Spelling and gramar "rectification".
 *
 * Changes made for release in 2.0.37 & 2.2.2 :
 * ------------------------------------------
 *	- Correct status in /proc/net/wireless
 *	- Set PSA CRC to make PtP diagnostic tool happy (Bob Gray)
 *	- Module init code don't fail if we found at least one card in
 *	  the address list (Karlis Peisenieks)
 *	- Missing parenthesis (Christopher Peterson)
 *	- Correct i82586 configuration parameters
 *	- Encryption initialisation bug (Robert McCormack)
 *	- New mac addresses detected in the probe
 *	- Increase watchdog for busy environments
 *
 * Changes made for release in 2.0.38 & 2.2.7 :
 * ------------------------------------------
 *	- Correct the reception logic to better report errors and avoid
 *	  sending bogus packet up the stack
 *	- Delay RU config to avoid corrupting first received packet
 *	- Change config completion code (to actually check something)
 *	- Avoid reading out of bound in skbuf to transmit
 *	- Rectify a lot of (useless) debugging code
 *	- Change the way to `#ifdef SET_PSA_CRC'
 *
 * Changes made for release in 2.2.11 & 2.3.13 :
 * -------------------------------------------
 *	- Change e-mail and web page addresses
 *	- Watchdog timer is now correctly expressed in HZ, not in jiffies
 *	- Add channel number to the list of frequencies in range
 *	- Add the (short) list of bit-rates in range
 *	- Developp a new sensitivity... (sens.value & sens.fixed)
 *
 * Changes made for release in 2.2.14 & 2.3.23 :
 * -------------------------------------------
 *	- Fix check for root permission (break instead of exit)
 *	- New nwid & encoding setting (Wireless Extension 9)
 *
 * Changes made for release in 2.3.49 :
 * ----------------------------------
 *	- Indentation reformating (Alan)
 *	- Update to new network API (softnet - 2.3.43) :
 *		o replace dev->tbusy (Alan)
 *		o replace dev->tstart (Alan)
 *		o remove dev->interrupt (Alan)
 *		o add SMP locking via spinlock in splxx (me)
 *		o add spinlock in interrupt handler (me)
 *		o use kernel watchdog instead of ours (me)
 *		o increase watchdog timeout (kernel is more sensitive) (me)
 *		o verify that all the changes make sense and work (me)
 *	- Fixup a potential gotcha when reconfiguring and thighten a bit
 *		the interactions with Tx queue.
 *
 * Changes made for release in 2.4.0 :
 * ---------------------------------
 *	- Fix spinlock stupid bugs that I left in. The driver is now SMP
 *		compliant and doesn't lockup at startup.
 *
 * Changes made for release in 2.5.2 :
 * ---------------------------------
 *	- Use new driver API for Wireless Extensions :
 *		o got rid of wavelan_ioctl()
 *		o use a bunch of iw_handler instead
 *
 * Changes made for release in 2.5.35 :
 * ----------------------------------
 *	- Set dev->trans_start to avoid filling the logs
 *	- Handle better spurious/bogus interrupt
 *	- Avoid deadlocks in mmc_out()/mmc_in()
 *
 * Wishes & dreams:
 * ----------------
 *	- roaming (see Pcmcia driver)
 */

/***************************** INCLUDES *****************************/

#include	<linux/module.h>

#include	<linux/kernel.h>
#include	<linux/sched.h>
#include	<linux/types.h>
#include	<linux/fcntl.h>
#include	<linux/interrupt.h>
#include	<linux/stat.h>
#include	<linux/ptrace.h>
#include	<linux/ioport.h>
#include	<linux/in.h>
#include	<linux/string.h>
#include	<linux/delay.h>
#include	<linux/bitops.h>
#include	<asm/system.h>
#include	<asm/io.h>
#include	<asm/dma.h>
#include	<asm/uaccess.h>
#include	<linux/errno.h>
#include	<linux/netdevice.h>
#include	<linux/etherdevice.h>
#include	<linux/skbuff.h>
#include	<linux/slab.h>
#include	<linux/timer.h>
#include	<linux/init.h>

#include <linux/wireless.h>		/* Wireless extensions */
#include <net/iw_handler.h>		/* Wireless handlers */

/* WaveLAN declarations */
#include	"i82586.h"
#include	"wavelan.h"

/************************** DRIVER OPTIONS **************************/
/*
 * `#define' or `#undef' the following constant to change the behaviour
 * of the driver...
 */
#undef SET_PSA_CRC		/* Calculate and set the CRC on PSA (slower) */
#define USE_PSA_CONFIG		/* Use info from the PSA. */
#undef STRUCT_CHECK		/* Verify padding of structures. */
#undef EEPROM_IS_PROTECTED	/* doesn't seem to be necessary */
#define MULTICAST_AVOID		/* Avoid extra multicast (I'm sceptical). */
#undef SET_MAC_ADDRESS		/* Experimental */

/* Warning:  this stuff will slow down the driver. */
#define WIRELESS_SPY		/* Enable spying addresses. */
#undef HISTOGRAM		/* Enable histogram of signal level. */

/****************************** DEBUG ******************************/

#undef DEBUG_MODULE_TRACE	/* module insertion/removal */
#undef DEBUG_CALLBACK_TRACE	/* calls made by Linux */
#undef DEBUG_INTERRUPT_TRACE	/* calls to handler */
#undef DEBUG_INTERRUPT_INFO	/* type of interrupt and so on */
#define DEBUG_INTERRUPT_ERROR	/* problems */
#undef DEBUG_CONFIG_TRACE	/* Trace the config functions. */
#undef DEBUG_CONFIG_INFO	/* what's going on */
#define DEBUG_CONFIG_ERROR	/* errors on configuration */
#undef DEBUG_TX_TRACE		/* transmission calls */
#undef DEBUG_TX_INFO		/* header of the transmitted packet */
#undef DEBUG_TX_FAIL		/* Normal failure conditions */
#define DEBUG_TX_ERROR		/* Unexpected conditions */
#undef DEBUG_RX_TRACE		/* transmission calls */
#undef DEBUG_RX_INFO		/* header of the received packet */
#undef DEBUG_RX_FAIL		/* Normal failure conditions */
#define DEBUG_RX_ERROR		/* Unexpected conditions */

#undef DEBUG_PACKET_DUMP	/* Dump packet on the screen if defined to 32. */
#undef DEBUG_IOCTL_TRACE	/* misc. call by Linux */
#undef DEBUG_IOCTL_INFO		/* various debugging info */
#define DEBUG_IOCTL_ERROR	/* what's going wrong */
#define DEBUG_BASIC_SHOW	/* Show basic startup info. */
#undef DEBUG_VERSION_SHOW	/* Print version info. */
#undef DEBUG_PSA_SHOW		/* Dump PSA to screen. */
#undef DEBUG_MMC_SHOW		/* Dump mmc to screen. */
#undef DEBUG_SHOW_UNUSED	/* Show unused fields too. */
#undef DEBUG_I82586_SHOW	/* Show i82586 status. */
#undef DEBUG_DEVICE_SHOW	/* Show device parameters. */

/************************ CONSTANTS & MACROS ************************/

#ifdef DEBUG_VERSION_SHOW
static const char	*version	= "wavelan.c : v24 (SMP + wireless extensions) 11/12/01\n";
#endif

/* Watchdog temporisation */
#define	WATCHDOG_JIFFIES	(512*HZ/100)

/* Macro to get the number of elements in an array */
#define	NELS(a)				(sizeof(a) / sizeof(a[0]))

/* ------------------------ PRIVATE IOCTL ------------------------ */

#define SIOCSIPQTHR	SIOCIWFIRSTPRIV		/* Set quality threshold */
#define SIOCGIPQTHR	SIOCIWFIRSTPRIV + 1	/* Get quality threshold */

#define SIOCSIPHISTO	SIOCIWFIRSTPRIV + 2	/* Set histogram ranges */
#define SIOCGIPHISTO	SIOCIWFIRSTPRIV + 3	/* Get histogram values */

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
 * For each network interface, Linux keeps data in two structures:  "device"
 * keeps the generic data (same format for everybody) and "net_local" keeps
 * additional specific data.
 * Note that some of this specific data is in fact generic (en_stats, for
 * example).
 */
struct net_local
{
  net_local *	next;		/* linked list of the devices */
  struct net_device *	dev;		/* reverse link */
  spinlock_t	spinlock;	/* Serialize access to the hardware (SMP) */
  en_stats	stats;		/* Ethernet interface statistics */
  int		nresets;	/* number of hardware resets */
  u_char	reconfig_82586;	/* We need to reconfigure the controller. */
  u_char	promiscuous;	/* promiscuous mode */
  int		mc_count;	/* number of multicast addresses */
  u_short	hacr;		/* current host interface state */

  int		tx_n_in_use;
  u_short	rx_head;
  u_short	rx_last;
  u_short	tx_first_free;
  u_short	tx_first_in_use;

  iw_stats	wstats;		/* Wireless-specific statistics */

  struct iw_spy_data	spy_data;
  struct iw_public_data	wireless_data;

#ifdef HISTOGRAM
  int		his_number;		/* number of intervals */
  u_char	his_range[16];		/* boundaries of interval ]n-1; n] */
  u_long	his_sum[16];		/* sum in interval */
#endif	/* HISTOGRAM */
};

/**************************** PROTOTYPES ****************************/

/* ----------------------- MISC. SUBROUTINES ------------------------ */
static u_char
	wv_irq_to_psa(int);
static int
	wv_psa_to_irq(u_char);
/* ------------------- HOST ADAPTER SUBROUTINES ------------------- */
static inline u_short		/* data */
	hasr_read(u_long);	/* Read the host interface:  base address */
static inline void
	hacr_write(u_long,	/* Write to host interface:  base address */
		   u_short),	/* data */
	hacr_write_slow(u_long,
		   u_short),
	set_chan_attn(u_long,	/* ioaddr */
		      u_short),	/* hacr   */
	wv_hacr_reset(u_long),	/* ioaddr */
	wv_16_off(u_long,	/* ioaddr */
		  u_short),	/* hacr   */
	wv_16_on(u_long,	/* ioaddr */
		 u_short),	/* hacr   */
	wv_ints_off(struct net_device *),
	wv_ints_on(struct net_device *);
/* ----------------- MODEM MANAGEMENT SUBROUTINES ----------------- */
static void
	psa_read(u_long,	/* Read the Parameter Storage Area. */
		 u_short,	/* hacr */
		 int,		/* offset in PSA */
		 u_char *,	/* buffer to fill */
		 int),		/* size to read */
	psa_write(u_long, 	/* Write to the PSA. */
		  u_short,	/* hacr */
		  int,		/* offset in PSA */
		  u_char *,	/* buffer in memory */
		  int);		/* length of buffer */
static inline void
	mmc_out(u_long,		/* Write 1 byte to the Modem Manag Control. */
		u_short,
		u_char),
	mmc_write(u_long,	/* Write n bytes to the MMC. */
		  u_char,
		  u_char *,
		  int);
static inline u_char		/* Read 1 byte from the MMC. */
	mmc_in(u_long,
	       u_short);
static inline void
	mmc_read(u_long,	/* Read n bytes from the MMC. */
		 u_char,
		 u_char *,
		 int),
	fee_wait(u_long,	/* Wait for frequency EEPROM:  base address */
		 int,		/* base delay to wait for */
		 int);		/* time to wait */
static void
	fee_read(u_long,	/* Read the frequency EEPROM:  base address */
		 u_short,	/* destination offset */
		 u_short *,	/* data buffer */
		 int);		/* number of registers */
/* ---------------------- I82586 SUBROUTINES ----------------------- */
static /*inline*/ void
	obram_read(u_long,	/* ioaddr */
		   u_short,	/* o */
		   u_char *,	/* b */
		   int);	/* n */
static inline void
	obram_write(u_long,	/* ioaddr */
		    u_short,	/* o */
		    u_char *,	/* b */
		    int);	/* n */
static void
	wv_ack(struct net_device *);
static inline int
	wv_synchronous_cmd(struct net_device *,
			   const char *),
	wv_config_complete(struct net_device *,
			   u_long,
			   net_local *);
static int
	wv_complete(struct net_device *,
		    u_long,
		    net_local *);
static inline void
	wv_82586_reconfig(struct net_device *);
/* ------------------- DEBUG & INFO SUBROUTINES ------------------- */
#ifdef DEBUG_I82586_SHOW
static void
	wv_scb_show(unsigned short);
#endif
static inline void
	wv_init_info(struct net_device *);	/* display startup info */
/* ------------------- IOCTL, STATS & RECONFIG ------------------- */
static en_stats	*
	wavelan_get_stats(struct net_device *);	/* Give stats /proc/net/dev */
static iw_stats *
	wavelan_get_wireless_stats(struct net_device *);
static void
	wavelan_set_multicast_list(struct net_device *);
/* ----------------------- PACKET RECEPTION ----------------------- */
static inline void
	wv_packet_read(struct net_device *,	/* Read a packet from a frame. */
		       u_short,
		       int),
	wv_receive(struct net_device *);	/* Read all packets waiting. */
/* --------------------- PACKET TRANSMISSION --------------------- */
static inline int
	wv_packet_write(struct net_device *,	/* Write a packet to the Tx buffer. */
			void *,
			short);
static int
	wavelan_packet_xmit(struct sk_buff *,	/* Send a packet. */
			    struct net_device *);
/* -------------------- HARDWARE CONFIGURATION -------------------- */
static inline int
	wv_mmc_init(struct net_device *),	/* Initialize the modem. */
	wv_ru_start(struct net_device *),	/* Start the i82586 receiver unit. */
	wv_cu_start(struct net_device *),	/* Start the i82586 command unit. */
	wv_82586_start(struct net_device *);	/* Start the i82586. */
static void
	wv_82586_config(struct net_device *);	/* Configure the i82586. */
static inline void
	wv_82586_stop(struct net_device *);
static int
	wv_hw_reset(struct net_device *),	/* Reset the WaveLAN hardware. */
	wv_check_ioaddr(u_long,		/* ioaddr */
			u_char *);	/* mac address (read) */
/* ---------------------- INTERRUPT HANDLING ---------------------- */
static irqreturn_t
	wavelan_interrupt(int,		/* interrupt handler */
			  void *);
static void
	wavelan_watchdog(struct net_device *);	/* transmission watchdog */
/* ------------------- CONFIGURATION CALLBACKS ------------------- */
static int
	wavelan_open(struct net_device *),	/* Open the device. */
	wavelan_close(struct net_device *),	/* Close the device. */
	wavelan_config(struct net_device *, unsigned short);/* Configure one device. */
extern struct net_device *wavelan_probe(int unit);	/* See Space.c. */

/**************************** VARIABLES ****************************/

/*
 * This is the root of the linked list of WaveLAN drivers
 * It is use to verify that we don't reuse the same base address
 * for two different drivers and to clean up when removing the module.
 */
static net_local *	wavelan_list	= (net_local *) NULL;

/*
 * This table is used to translate the PSA value to IRQ number
 * and vice versa.
 */
static u_char	irqvals[]	=
{
	   0,    0,    0, 0x01,
	0x02, 0x04,    0, 0x08,
	   0,    0, 0x10, 0x20,
	0x40,    0,    0, 0x80,
};

/*
 * Table of the available I/O addresses (base addresses) for WaveLAN
 */
static unsigned short	iobase[]	=
{
#if	0
  /* Leave out 0x3C0 for now -- seems to clash with some video
   * controllers.
   * Leave out the others too -- we will always use 0x390 and leave
   * 0x300 for the Ethernet device.
   * Jean II:  0x3E0 is fine as well.
   */
  0x300, 0x390, 0x3E0, 0x3C0
#endif	/* 0 */
  0x390, 0x3E0
};

#ifdef	MODULE
/* Parameters set by insmod */
static int	io[4];
static int	irq[4];
static char	*name[4];
module_param_array(io, int, NULL, 0);
module_param_array(irq, int, NULL, 0);
module_param_array(name, charp, NULL, 0);

MODULE_PARM_DESC(io, "WaveLAN I/O base address(es),required");
MODULE_PARM_DESC(irq, "WaveLAN IRQ number(s)");
MODULE_PARM_DESC(name, "WaveLAN interface neme(s)");
#endif	/* MODULE */

#endif	/* WAVELAN_P_H */
