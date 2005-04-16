/* orinoco.c - (formerly known as dldwd_cs.c and orinoco_cs.c)
 *
 * A driver for Hermes or Prism 2 chipset based PCMCIA wireless
 * adaptors, with Lucent/Agere, Intersil or Symbol firmware.
 *
 * Current maintainers (as of 29 September 2003) are:
 * 	Pavel Roskin <proski AT gnu.org>
 * and	David Gibson <hermes AT gibson.dropbear.id.au>
 *
 * (C) Copyright David Gibson, IBM Corporation 2001-2003.
 * Copyright (C) 2000 David Gibson, Linuxcare Australia.
 *	With some help from :
 * Copyright (C) 2001 Jean Tourrilhes, HP Labs
 * Copyright (C) 2001 Benjamin Herrenschmidt
 *
 * Based on dummy_cs.c 1.27 2000/06/12 21:27:25
 *
 * Portions based on wvlan_cs.c 1.0.6, Copyright Andreas Neuhaus <andy
 * AT fasta.fh-dortmund.de>
 *      http://www.stud.fh-dortmund.de/~andy/wvlan/
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License.
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds AT users.sourceforge.net>.  Portions created by David
 * A. Hinds are Copyright (C) 1999 David A. Hinds.  All Rights
 * Reserved.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in
 * which case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use your
 * version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.  */

/*
 * v0.01 -> v0.02 - 21/3/2001 - Jean II
 *	o Allow to use regular ethX device name instead of dldwdX
 *	o Warning on IBSS with ESSID=any for firmware 6.06
 *	o Put proper range.throughput values (optimistic)
 *	o IWSPY support (IOCTL and stat gather in Rx path)
 *	o Allow setting frequency in Ad-Hoc mode
 *	o Disable WEP setting if !has_wep to work on old firmware
 *	o Fix txpower range
 *	o Start adding support for Samsung/Compaq firmware
 *
 * v0.02 -> v0.03 - 23/3/2001 - Jean II
 *	o Start adding Symbol support - need to check all that
 *	o Fix Prism2/Symbol WEP to accept 128 bits keys
 *	o Add Symbol WEP (add authentication type)
 *	o Add Prism2/Symbol rate
 *	o Add PM timeout (holdover duration)
 *	o Enable "iwconfig eth0 key off" and friends (toggle flags)
 *	o Enable "iwconfig eth0 power unicast/all" (toggle flags)
 *	o Try with an Intel card. It report firmware 1.01, behave like
 *	  an antiquated firmware, however on windows it says 2.00. Yuck !
 *	o Workaround firmware bug in allocate buffer (Intel 1.01)
 *	o Finish external renaming to orinoco...
 *	o Testing with various Wavelan firmwares
 *
 * v0.03 -> v0.04 - 30/3/2001 - Jean II
 *	o Update to Wireless 11 -> add retry limit/lifetime support
 *	o Tested with a D-Link DWL 650 card, fill in firmware support
 *	o Warning on Vcc mismatch (D-Link 3.3v card in Lucent 5v only slot)
 *	o Fixed the Prism2 WEP bugs that I introduced in v0.03 :-(
 *	  It works on D-Link *only* after a tcpdump. Weird...
 *	  And still doesn't work on Intel card. Grrrr...
 *	o Update the mode after a setport3
 *	o Add preamble setting for Symbol cards (not yet enabled)
 *	o Don't complain as much about Symbol cards...
 *
 * v0.04 -> v0.04b - 22/4/2001 - David Gibson
 *      o Removed the 'eth' parameter - always use ethXX as the
 *        interface name instead of dldwdXX.  The other was racy
 *        anyway.
 *	o Clean up RID definitions in hermes.h, other cleanups
 *
 * v0.04b -> v0.04c - 24/4/2001 - Jean II
 *	o Tim Hurley <timster AT seiki.bliztech.com> reported a D-Link card
 *	  with vendor 02 and firmware 0.08. Added in the capabilities...
 *	o Tested Lucent firmware 7.28, everything works...
 *
 * v0.04c -> v0.05 - 3/5/2001 - Benjamin Herrenschmidt
 *	o Spin-off Pcmcia code. This file is renamed orinoco.c,
 *	  and orinoco_cs.c now contains only the Pcmcia specific stuff
 *	o Add Airport driver support on top of orinoco.c (see airport.c)
 *
 * v0.05 -> v0.05a - 4/5/2001 - Jean II
 *	o Revert to old Pcmcia code to fix breakage of Ben's changes...
 *
 * v0.05a -> v0.05b - 4/5/2001 - Jean II
 *	o add module parameter 'ignore_cis_vcc' for D-Link @ 5V
 *	o D-Link firmware doesn't support multicast. We just print a few
 *	  error messages, but otherwise everything works...
 *	o For David : set/getport3 works fine, just upgrade iwpriv...
 *
 * v0.05b -> v0.05c - 5/5/2001 - Benjamin Herrenschmidt
 *	o Adapt airport.c to latest changes in orinoco.c
 *	o Remove deferred power enabling code
 *
 * v0.05c -> v0.05d - 5/5/2001 - Jean II
 *	o Workaround to SNAP decapsulate frame from Linksys AP
 *	  original patch from : Dong Liu <dliu AT research.bell-labs.com>
 *	  (note : the memcmp bug was mine - fixed)
 *	o Remove set_retry stuff, no firmware support it (bloat--).
 *
 * v0.05d -> v0.06 - 25/5/2001 - Jean II
 *		Original patch from "Hong Lin" <alin AT redhat.com>,
 *		"Ian Kinner" <ikinner AT redhat.com>
 *		and "David Smith" <dsmith AT redhat.com>
 *	o Init of priv->tx_rate_ctrl in firmware specific section.
 *	o Prism2/Symbol rate, upto should be 0xF and not 0x15. Doh !
 *	o Spectrum card always need cor_reset (for every reset)
 *	o Fix cor_reset to not lose bit 7 in the register
 *	o flush_stale_links to remove zombie Pcmcia instances
 *	o Ack previous hermes event before reset
 *		Me (with my little hands)
 *	o Allow orinoco.c to call cor_reset via priv->card_reset_handler
 *	o Add priv->need_card_reset to toggle this feature
 *	o Fix various buglets when setting WEP in Symbol firmware
 *	  Now, encryption is fully functional on Symbol cards. Youpi !
 *
 * v0.06 -> v0.06b - 25/5/2001 - Jean II
 *	o IBSS on Symbol use port_mode = 4. Please don't ask...
 *
 * v0.06b -> v0.06c - 29/5/2001 - Jean II
 *	o Show first spy address in /proc/net/wireless for IBSS mode as well
 *
 * v0.06c -> v0.06d - 6/7/2001 - David Gibson
 *      o Change a bunch of KERN_INFO messages to KERN_DEBUG, as per Linus'
 *        wishes to reduce the number of unnecessary messages.
 *	o Removed bogus message on CRC error.
 *	o Merged fixes for v0.08 Prism 2 firmware from William Waghorn
 *	  <willwaghorn AT yahoo.co.uk>
 *	o Slight cleanup/re-arrangement of firmware detection code.
 *
 * v0.06d -> v0.06e - 1/8/2001 - David Gibson
 *	o Removed some redundant global initializers (orinoco_cs.c).
 *	o Added some module metadata
 *
 * v0.06e -> v0.06f - 14/8/2001 - David Gibson
 *	o Wording fix to license
 *	o Added a 'use_alternate_encaps' module parameter for APs which need an
 *	  oui of 00:00:00.  We really need a better way of handling this, but
 *	  the module flag is better than nothing for now.
 *
 * v0.06f -> v0.07 - 20/8/2001 - David Gibson
 *	o Removed BAP error retries from hermes_bap_seek().  For Tx we now
 *	  let the upper layers handle the retry, we retry explicitly in the
 *	  Rx path, but don't make as much noise about it.
 *	o Firmware detection cleanups.
 *
 * v0.07 -> v0.07a - 1/10/3001 - Jean II
 *	o Add code to read Symbol firmware revision, inspired by latest code
 *	  in Spectrum24 by Lee John Keyser-Allen - Thanks Lee !
 *	o Thanks to Jared Valentine <hidden AT xmission.com> for "providing" me
 *	  a 3Com card with a recent firmware, fill out Symbol firmware
 *	  capabilities of latest rev (2.20), as well as older Symbol cards.
 *	o Disable Power Management in newer Symbol firmware, the API 
 *	  has changed (documentation needed).
 *
 * v0.07a -> v0.08 - 3/10/2001 - David Gibson
 *	o Fixed a possible buffer overrun found by the Stanford checker (in
 *	  dldwd_ioctl_setiwencode()).  Can only be called by root anyway, so not
 *	  a big problem.
 *	o Turned has_big_wep on for Intersil cards.  That's not true for all of
 *	  them but we should at least let the capable ones try.
 *	o Wait for BUSY to clear at the beginning of hermes_bap_seek().  I
 *	  realized that my assumption that the driver's serialization
 *	  would prevent the BAP being busy on entry was possibly false, because
 *	  things other than seeks may make the BAP busy.
 *	o Use "alternate" (oui 00:00:00) encapsulation by default.
 *	  Setting use_old_encaps will mimic the old behaviour, but I think we
 *	  will be able to eliminate this.
 *	o Don't try to make __initdata const (the version string).  This can't
 *	  work because of the way the __initdata sectioning works.
 *	o Added MODULE_LICENSE tags.
 *	o Support for PLX (transparent PCMCIA->PCI bridge) cards.
 *	o Changed to using the new type-fascist min/max.
 *
 * v0.08 -> v0.08a - 9/10/2001 - David Gibson
 *	o Inserted some missing acknowledgements/info into the Changelog.
 *	o Fixed some bugs in the normalization of signal level reporting.
 *	o Fixed bad bug in WEP key handling on Intersil and Symbol firmware,
 *	  which led to an instant crash on big-endian machines.
 *
 * v0.08a -> v0.08b - 20/11/2001 - David Gibson
 *	o Lots of cleanup and bugfixes in orinoco_plx.c
 *	o Cleanup to handling of Tx rate setting.
 *	o Removed support for old encapsulation method.
 *	o Removed old "dldwd" names.
 *	o Split RID constants into a new file hermes_rid.h
 *	o Renamed RID constants to match linux-wlan-ng and prism2.o
 *	o Bugfixes in hermes.c
 *	o Poke the PLX's INTCSR register, so it actually starts
 *	  generating interrupts.  These cards might actually work now.
 *	o Update to wireless extensions v12 (Jean II)
 *	o Support for tallies and inquire command (Jean II)
 *	o Airport updates for newer PPC kernels (BenH)
 *
 * v0.08b -> v0.09 - 21/12/2001 - David Gibson
 *	o Some new PCI IDs for PLX cards.
 *	o Removed broken attempt to do ALLMULTI reception.  Just use
 *	  promiscuous mode instead
 *	o Preliminary work for list-AP (Jean II)
 *	o Airport updates from (BenH)
 *	o Eliminated racy hw_ready stuff
 *	o Fixed generation of fake events in irq handler.  This should
 *	  finally kill the EIO problems (Jean II & dgibson)
 *	o Fixed breakage of bitrate set/get on Agere firmware (Jean II)
 *
 * v0.09 -> v0.09a - 2/1/2002 - David Gibson
 *	o Fixed stupid mistake in multicast list handling, triggering
 *	  a BUG()
 *
 * v0.09a -> v0.09b - 16/1/2002 - David Gibson
 *	o Fixed even stupider mistake in new interrupt handling, which
 *	  seriously broke things on big-endian machines.
 *	o Removed a bunch of redundant includes and exports.
 *	o Removed a redundant MOD_{INC,DEC}_USE_COUNT pair in airport.c
 *	o Don't attempt to do hardware level multicast reception on
 *	  Intersil firmware, just go promisc instead.
 *	o Typo fixed in hermes_issue_cmd()
 *	o Eliminated WIRELESS_SPY #ifdefs
 *	o Status code reported on Tx exceptions
 *	o Moved netif_wake_queue() from ALLOC interrupts to TX and TXEXC
 *	  interrupts, which should fix the timeouts we're seeing.
 *
 * v0.09b -> v0.10 - 25 Feb 2002 - David Gibson
 *	o Removed nested structures used for header parsing, so the
 *	  driver should now work without hackery on ARM
 *	o Fix for WEP handling on Intersil (Hawk Newton)
 *	o Eliminated the /proc/hermes/ethXX/regs debugging file.  It
 *	  was never very useful.
 *	o Make Rx errors less noisy.
 *
 * v0.10 -> v0.11 - 5 Apr 2002 - David Gibson
 *	o Laid the groundwork in hermes.[ch] for devices which map
 *	  into PCI memory space rather than IO space.
 *	o Fixed bug in multicast handling (cleared multicast list when
 *	  leaving promiscuous mode).
 *	o Relegated Tx error messages to debug.
 *	o Cleaned up / corrected handling of allocation lengths.
 *	o Set OWNSSID in IBSS mode for WinXP interoperability (jimc).
 *	o Change to using alloc_etherdev() for structure allocations. 
 *	o Check for and drop undersized packets.
 *	o Fixed a race in stopping/waking the queue.  This should fix
 *	  the timeout problems (Pavel Roskin)
 *	o Reverted to netif_wake_queue() on the ALLOC event.
 *	o Fixes for recent Symbol firmwares which lack AP density
 *	  (Pavel Roskin).
 *
 * v0.11 -> v0.11a - 29 Apr 2002 - David Gibson
 *	o Handle different register spacing, necessary for Prism 2.5
 *	  PCI adaptors (Steve Hill).
 *	o Cleaned up initialization of card structures in orinoco_cs
 *	  and airport.  Removed card->priv field.
 *	o Make response structure optional for hermes_docmd_wait()
 *	  Pavel Roskin)
 *	o Added PCI id for Nortel emobility to orinoco_plx.c.
 *	o Cleanup to handling of Symbol's allocation bug. (Pavel Roskin)
 *	o Cleanups to firmware capability detection.
 *	o Arrange for orinoco_pci.c to override firmware detection.
 *	  We should be able to support the PCI Intersil cards now.
 *	o Cleanup handling of reset_cor and hard_reset (Pavel Roskin).
 *	o Remove erroneous use of USER_BAP in the TxExc handler (Jouni
 *	  Malinen).
 *	o Makefile changes for better integration into David Hinds
 *	  pcmcia-cs package.
 *
 * v0.11a -> v0.11b - 1 May 2002 - David Gibson
 *	o Better error reporting in orinoco_plx_init_one()
 *	o Fixed multiple bad kfree() bugs introduced by the
 *	  alloc_orinocodev() changes.
 *
 * v0.11b -> v0.12 - 19 Jun 2002 - David Gibson
 *	o Support changing the MAC address.
 *	o Correct display of Intersil firmware revision numbers.
 *	o Entirely revised locking scheme.  Should be both simpler and
 *	   better.
 *	o Merged some common code in orinoco_plx, orinoco_pci and
 *	  airport by creating orinoco_default_{open,stop,reset}()
 *	  which are used as the dev->open, dev->stop, priv->reset
 *	  callbacks if none are specified when alloc_orinocodev() is
 *	  called.
 *	o Removed orinoco_plx_interrupt() and orinoco_pci_interrupt().
 *	  They didn't do anything.
 *
 * v0.12 -> v0.12a - 4 Jul 2002 - David Gibson
 *	o Some rearrangement of code.
 *	o Numerous fixups to locking and rest handling, particularly
 *	  for PCMCIA.
 *	o This allows open and stop net_device methods to be in
 *	  orinoco.c now, rather than in the init modules.
 *	o In orinoco_cs.c link->priv now points to the struct
 *	  net_device not to the struct orinoco_private.
 *	o Added a check for undersized SNAP frames, which could cause
 *	  crashes.
 *
 * v0.12a -> v0.12b - 11 Jul 2002 - David Gibson
 *	o Fix hw->num_init testing code, so num_init is actually
 *	  incremented.
 *	o Fix very stupid bug in orinoco_cs which broke compile with
 *	  CONFIG_SMP.
 *	o Squashed a warning.
 *
 * v0.12b -> v0.12c - 26 Jul 2002 - David Gibson
 *	o Change to C9X style designated initializers.
 *	o Add support for 3Com AirConnect PCI.
 *	o No longer ignore the hard_reset argument to
 *	  alloc_orinocodev().  Oops.
 *
 * v0.12c -> v0.13beta1 - 13 Sep 2002 - David Gibson
 *	o Revert the broken 0.12* locking scheme and go to a new yet
 *	  simpler scheme.
 *	o Do firmware resets only in orinoco_init() and when waking
 *	  the card from hard sleep.
 *
 * v0.13beta1 -> v0.13 - 27 Sep 2002 - David Gibson
 *	o Re-introduced full resets (via schedule_task()) on Tx
 *	  timeout.
 *
 * v0.13 -> v0.13a - 30 Sep 2002 - David Gibson
 *	o Minor cleanups to info frame handling.  Add basic support
 *	  for linkstatus info frames.
 *	o Include required kernel headers in orinoco.h, to avoid
 *	  compile problems.
 *
 * v0.13a -> v0.13b - 10 Feb 2003 - David Gibson
 *	o Implemented hard reset for Airport cards
 *	o Experimental suspend/resume implementation for orinoco_pci
 *	o Abolished /proc debugging support, replaced with a debugging
 *	  iwpriv.  Now it's ugly and simple instead of ugly and complex.
 *	o Bugfix in hermes.c if the firmware returned a record length
 *	  of 0, we could go clobbering memory.
 *	o Bugfix in orinoco_stop() - it used to fail if hw_unavailable
 *	  was set, which was usually true on PCMCIA hot removes.
 * 	o Track LINKSTATUS messages, silently drop Tx packets before
 * 	  we are connected (avoids confusing the firmware), and only
 * 	  give LINKSTATUS printk()s if the status has changed.
 *
 * v0.13b -> v0.13c - 11 Mar 2003 - David Gibson
 *	o Cleanup: use dev instead of priv in various places.
 *	o Bug fix: Don't ReleaseConfiguration on RESET_PHYSICAL event
 *	  if we're in the middle of a (driver initiated) hard reset.
 *	o Bug fix: ETH_ZLEN is supposed to include the header
 *	  (Dionysus Blazakis & Manish Karir)
 *	o Convert to using workqueues instead of taskqueues (and
 *	  backwards compatibility macros for pre 2.5.41 kernels).
 *	o Drop redundant (I think...) MOD_{INC,DEC}_USE_COUNT in
 *	  airport.c
 *	o New orinoco_tmd.c init module from Joerg Dorchain for
 *	  TMD7160 based PCI to PCMCIA bridges (similar to
 *	  orinoco_plx.c).
 *
 * v0.13c -> v0.13d - 22 Apr 2003 - David Gibson
 *	o Make hw_unavailable a counter, rather than just a flag, this
 *	  is necessary to avoid some races (such as a card being
 *	  removed in the middle of orinoco_reset().
 *	o Restore Release/RequestConfiguration in the PCMCIA event handler
 *	  when dealing with a driver initiated hard reset.  This is
 *	  necessary to prevent hangs due to a spurious interrupt while
 *	  the reset is in progress.
 *	o Clear the 802.11 header when transmitting, even though we
 *	  don't use it.  This fixes a long standing bug on some
 *	  firmwares, which seem to get confused if that isn't done.
 *	o Be less eager to de-encapsulate SNAP frames, only do so if
 *	  the OUI is 00:00:00 or 00:00:f8, leave others alone.  The old
 *	  behaviour broke CDP (Cisco Discovery Protocol).
 *	o Use dev instead of priv for free_irq() as well as
 *	  request_irq() (oops).
 *	o Attempt to reset rather than giving up if we get too many
 *	  IRQs.
 *	o Changed semantics of __orinoco_down() so it can be called
 *	  safely with hw_unavailable set.  It also now clears the
 *	  linkstatus (since we're going to have to reassociate).
 *
 * v0.13d -> v0.13e - 12 May 2003 - David Gibson
 *	o Support for post-2.5.68 return values from irq handler.
 *	o Fixed bug where underlength packets would be double counted
 *	  in the rx_dropped statistics.
 *	o Provided a module parameter to suppress linkstatus messages.
 *
 * v0.13e -> v0.14alpha1 - 30 Sep 2003 - David Gibson
 *	o Replaced priv->connected logic with netif_carrier_on/off()
 *	  calls.
 *	o Remove has_ibss_any and never set the CREATEIBSS RID when
 *	  the ESSID is empty.  Too many firmwares break if we do.
 *	o 2.6 merges: Replace pdev->slot_name with pci_name(), remove
 *	  __devinitdata from PCI ID tables, use free_netdev().
 *	o Enabled shared-key authentication for Agere firmware (from
 *	  Robert J. Moore <Robert.J.Moore AT allanbank.com>
 *	o Move netif_wake_queue() (back) to the Tx completion from the
 *	  ALLOC event.  This seems to prevent/mitigate the rolling
 *	  error -110 problems at least on some Intersil firmwares.
 *	  Theoretically reduces performance, but I can't measure it.
 *	  Patch from Andrew Tridgell <tridge AT samba.org>
 *
 * v0.14alpha1 -> v0.14alpha2 - 20 Oct 2003 - David Gibson
 *	o Correctly turn off shared-key authentication when requested
 *	  (bugfix from Robert J. Moore).
 *	o Correct airport sleep interfaces for current 2.6 kernels.
 *	o Add code for key change without disabling/enabling the MAC
 *	  port.  This is supposed to allow 802.1x to work sanely, but
 *	  doesn't seem to yet.
 *
 * TODO
 *	o New wireless extensions API (patch from Moustafa
 *	  Youssef, updated by Jim Carter and Pavel Roskin).
 *	o Handle de-encapsulation within network layer, provide 802.11
 *	  headers (patch from Thomas 'Dent' Mirlacher)
 *	o RF monitor mode support
 *	o Fix possible races in SPY handling.
 *	o Disconnect wireless extensions from fundamental configuration.
 *	o (maybe) Software WEP support (patch from Stano Meduna).
 *	o (maybe) Use multiple Tx buffers - driver handling queue
 *	  rather than firmware.
 */

/* Locking and synchronization:
 *
 * The basic principle is that everything is serialized through a
 * single spinlock, priv->lock.  The lock is used in user, bh and irq
 * context, so when taken outside hardirq context it should always be
 * taken with interrupts disabled.  The lock protects both the
 * hardware and the struct orinoco_private.
 *
 * Another flag, priv->hw_unavailable indicates that the hardware is
 * unavailable for an extended period of time (e.g. suspended, or in
 * the middle of a hard reset).  This flag is protected by the
 * spinlock.  All code which touches the hardware should check the
 * flag after taking the lock, and if it is set, give up on whatever
 * they are doing and drop the lock again.  The orinoco_lock()
 * function handles this (it unlocks and returns -EBUSY if
 * hw_unavailable is non-zero).
 */

#define DRIVER_NAME "orinoco"

#include <linux/config.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/system.h>

#include "hermes.h"
#include "hermes_rid.h"
#include "orinoco.h"
#include "ieee802_11.h"

/********************************************************************/
/* Module information                                               */
/********************************************************************/

MODULE_AUTHOR("Pavel Roskin <proski@gnu.org> & David Gibson <hermes@gibson.dropbear.id.au>");
MODULE_DESCRIPTION("Driver for Lucent Orinoco, Prism II based and similar wireless cards");
MODULE_LICENSE("Dual MPL/GPL");

/* Level of debugging. Used in the macros in orinoco.h */
#ifdef ORINOCO_DEBUG
int orinoco_debug = ORINOCO_DEBUG;
module_param(orinoco_debug, int, 0644);
MODULE_PARM_DESC(orinoco_debug, "Debug level");
EXPORT_SYMBOL(orinoco_debug);
#endif

static int suppress_linkstatus; /* = 0 */
module_param(suppress_linkstatus, bool, 0644);
MODULE_PARM_DESC(suppress_linkstatus, "Don't log link status changes");

/********************************************************************/
/* Compile time configuration and compatibility stuff               */
/********************************************************************/

/* We do this this way to avoid ifdefs in the actual code */
#ifdef WIRELESS_SPY
#define SPY_NUMBER(priv)	(priv->spy_number)
#else
#define SPY_NUMBER(priv)	0
#endif /* WIRELESS_SPY */

/********************************************************************/
/* Internal constants                                               */
/********************************************************************/

#define ORINOCO_MIN_MTU		256
#define ORINOCO_MAX_MTU		(IEEE802_11_DATA_LEN - ENCAPS_OVERHEAD)

#define SYMBOL_MAX_VER_LEN	(14)
#define USER_BAP		0
#define IRQ_BAP			1
#define MAX_IRQLOOPS_PER_IRQ	10
#define MAX_IRQLOOPS_PER_JIFFY	(20000/HZ) /* Based on a guestimate of
					    * how many events the
					    * device could
					    * legitimately generate */
#define SMALL_KEY_SIZE		5
#define LARGE_KEY_SIZE		13
#define TX_NICBUF_SIZE_BUG	1585		/* Bug in Symbol firmware */

#define DUMMY_FID		0xFFFF

/*#define MAX_MULTICAST(priv)	(priv->firmware_type == FIRMWARE_TYPE_AGERE ? \
  HERMES_MAX_MULTICAST : 0)*/
#define MAX_MULTICAST(priv)	(HERMES_MAX_MULTICAST)

#define ORINOCO_INTEN	 	(HERMES_EV_RX | HERMES_EV_ALLOC \
				 | HERMES_EV_TX | HERMES_EV_TXEXC \
				 | HERMES_EV_WTERR | HERMES_EV_INFO \
				 | HERMES_EV_INFDROP )

/********************************************************************/
/* Data tables                                                      */
/********************************************************************/

/* The frequency of each channel in MHz */
static const long channel_frequency[] = {
	2412, 2417, 2422, 2427, 2432, 2437, 2442,
	2447, 2452, 2457, 2462, 2467, 2472, 2484
};
#define NUM_CHANNELS ARRAY_SIZE(channel_frequency)

/* This tables gives the actual meanings of the bitrate IDs returned
 * by the firmware. */
static struct {
	int bitrate; /* in 100s of kilobits */
	int automatic;
	u16 agere_txratectrl;
	u16 intersil_txratectrl;
} bitrate_table[] = {
	{110, 1,  3, 15}, /* Entry 0 is the default */
	{10,  0,  1,  1},
	{10,  1,  1,  1},
	{20,  0,  2,  2},
	{20,  1,  6,  3},
	{55,  0,  4,  4},
	{55,  1,  7,  7},
	{110, 0,  5,  8},
};
#define BITRATE_TABLE_SIZE ARRAY_SIZE(bitrate_table)

/********************************************************************/
/* Data types                                                       */
/********************************************************************/

struct header_struct {
	/* 802.3 */
	u8 dest[ETH_ALEN];
	u8 src[ETH_ALEN];
	u16 len;
	/* 802.2 */
	u8 dsap;
	u8 ssap;
	u8 ctrl;
	/* SNAP */
	u8 oui[3];
	u16 ethertype;
} __attribute__ ((packed));

/* 802.2 LLC/SNAP header used for Ethernet encapsulation over 802.11 */
u8 encaps_hdr[] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00};

#define ENCAPS_OVERHEAD		(sizeof(encaps_hdr) + 2)

struct hermes_rx_descriptor {
	u16 status;
	u32 time;
	u8 silence;
	u8 signal;
	u8 rate;
	u8 rxflow;
	u32 reserved;
} __attribute__ ((packed));

/********************************************************************/
/* Function prototypes                                              */
/********************************************************************/

static int orinoco_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int __orinoco_program_rids(struct net_device *dev);
static void __orinoco_set_multicast_list(struct net_device *dev);
static int orinoco_debug_dump_recs(struct net_device *dev);

/********************************************************************/
/* Internal helper functions                                        */
/********************************************************************/

static inline void set_port_type(struct orinoco_private *priv)
{
	switch (priv->iw_mode) {
	case IW_MODE_INFRA:
		priv->port_type = 1;
		priv->createibss = 0;
		break;
	case IW_MODE_ADHOC:
		if (priv->prefer_port3) {
			priv->port_type = 3;
			priv->createibss = 0;
		} else {
			priv->port_type = priv->ibss_port;
			priv->createibss = 1;
		}
		break;
	default:
		printk(KERN_ERR "%s: Invalid priv->iw_mode in set_port_type()\n",
		       priv->ndev->name);
	}
}

/********************************************************************/
/* Device methods                                                   */
/********************************************************************/

static int orinoco_open(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	unsigned long flags;
	int err;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	err = __orinoco_up(dev);

	if (! err)
		priv->open = 1;

	orinoco_unlock(priv, &flags);

	return err;
}

int orinoco_stop(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int err = 0;

	/* We mustn't use orinoco_lock() here, because we need to be
	   able to close the interface even if hw_unavailable is set
	   (e.g. as we're released after a PC Card removal) */
	spin_lock_irq(&priv->lock);

	priv->open = 0;

	err = __orinoco_down(dev);

	spin_unlock_irq(&priv->lock);

	return err;
}

static struct net_device_stats *orinoco_get_stats(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	
	return &priv->stats;
}

static struct iw_statistics *orinoco_get_wireless_stats(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	struct iw_statistics *wstats = &priv->wstats;
	int err = 0;
	unsigned long flags;

	if (! netif_device_present(dev)) {
		printk(KERN_WARNING "%s: get_wireless_stats() called while device not present\n",
		       dev->name);
		return NULL; /* FIXME: Can we do better than this? */
	}

	if (orinoco_lock(priv, &flags) != 0)
		return NULL;  /* FIXME: Erg, we've been signalled, how
			       * do we propagate this back up? */

	if (priv->iw_mode == IW_MODE_ADHOC) {
		memset(&wstats->qual, 0, sizeof(wstats->qual));
		/* If a spy address is defined, we report stats of the
		 * first spy address - Jean II */
		if (SPY_NUMBER(priv)) {
			wstats->qual.qual = priv->spy_stat[0].qual;
			wstats->qual.level = priv->spy_stat[0].level;
			wstats->qual.noise = priv->spy_stat[0].noise;
			wstats->qual.updated = priv->spy_stat[0].updated;
		}
	} else {
		struct {
			u16 qual, signal, noise;
		} __attribute__ ((packed)) cq;

		err = HERMES_READ_RECORD(hw, USER_BAP,
					 HERMES_RID_COMMSQUALITY, &cq);
		
		wstats->qual.qual = (int)le16_to_cpu(cq.qual);
		wstats->qual.level = (int)le16_to_cpu(cq.signal) - 0x95;
		wstats->qual.noise = (int)le16_to_cpu(cq.noise) - 0x95;
		wstats->qual.updated = 7;
	}

	/* We can't really wait for the tallies inquiry command to
	 * complete, so we just use the previous results and trigger
	 * a new tallies inquiry command for next time - Jean II */
	/* FIXME: We're in user context (I think?), so we should just
           wait for the tallies to come through */
	err = hermes_inquire(hw, HERMES_INQ_TALLIES);
               
	orinoco_unlock(priv, &flags);

	if (err)
		return NULL;
		
	return wstats;
}

static void orinoco_set_multicast_list(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0) {
		printk(KERN_DEBUG "%s: orinoco_set_multicast_list() "
		       "called when hw_unavailable\n", dev->name);
		return;
	}

	__orinoco_set_multicast_list(dev);
	orinoco_unlock(priv, &flags);
}

static int orinoco_change_mtu(struct net_device *dev, int new_mtu)
{
	struct orinoco_private *priv = netdev_priv(dev);

	if ( (new_mtu < ORINOCO_MIN_MTU) || (new_mtu > ORINOCO_MAX_MTU) )
		return -EINVAL;

	if ( (new_mtu + ENCAPS_OVERHEAD + IEEE802_11_HLEN) >
	     (priv->nicbuf_size - ETH_HLEN) )
		return -EINVAL;

	dev->mtu = new_mtu;

	return 0;
}

/********************************************************************/
/* Tx path                                                          */
/********************************************************************/

static int orinoco_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct net_device_stats *stats = &priv->stats;
	hermes_t *hw = &priv->hw;
	int err = 0;
	u16 txfid = priv->txfid;
	char *p;
	struct ethhdr *eh;
	int len, data_len, data_off;
	struct hermes_tx_descriptor desc;
	unsigned long flags;

	TRACE_ENTER(dev->name);

	if (! netif_running(dev)) {
		printk(KERN_ERR "%s: Tx on stopped device!\n",
		       dev->name);
		TRACE_EXIT(dev->name);
		return 1;
	}
	
	if (netif_queue_stopped(dev)) {
		printk(KERN_DEBUG "%s: Tx while transmitter busy!\n", 
		       dev->name);
		TRACE_EXIT(dev->name);
		return 1;
	}
	
	if (orinoco_lock(priv, &flags) != 0) {
		printk(KERN_ERR "%s: orinoco_xmit() called while hw_unavailable\n",
		       dev->name);
		TRACE_EXIT(dev->name);
		return 1;
	}

	if (! netif_carrier_ok(dev)) {
		/* Oops, the firmware hasn't established a connection,
                   silently drop the packet (this seems to be the
                   safest approach). */
		stats->tx_errors++;
		orinoco_unlock(priv, &flags);
		dev_kfree_skb(skb);
		TRACE_EXIT(dev->name);
		return 0;
	}

	/* Length of the packet body */
	/* FIXME: what if the skb is smaller than this? */
	len = max_t(int,skb->len - ETH_HLEN, ETH_ZLEN - ETH_HLEN);

	eh = (struct ethhdr *)skb->data;

	memset(&desc, 0, sizeof(desc));
 	desc.tx_control = cpu_to_le16(HERMES_TXCTRL_TX_OK | HERMES_TXCTRL_TX_EX);
	err = hermes_bap_pwrite(hw, USER_BAP, &desc, sizeof(desc), txfid, 0);
	if (err) {
		if (net_ratelimit())
			printk(KERN_ERR "%s: Error %d writing Tx descriptor "
			       "to BAP\n", dev->name, err);
		stats->tx_errors++;
		goto fail;
	}

	/* Clear the 802.11 header and data length fields - some
	 * firmwares (e.g. Lucent/Agere 8.xx) appear to get confused
	 * if this isn't done. */
	hermes_clear_words(hw, HERMES_DATA0,
			   HERMES_802_3_OFFSET - HERMES_802_11_OFFSET);

	/* Encapsulate Ethernet-II frames */
	if (ntohs(eh->h_proto) > ETH_DATA_LEN) { /* Ethernet-II frame */
		struct header_struct hdr;
		data_len = len;
		data_off = HERMES_802_3_OFFSET + sizeof(hdr);
		p = skb->data + ETH_HLEN;

		/* 802.3 header */
		memcpy(hdr.dest, eh->h_dest, ETH_ALEN);
		memcpy(hdr.src, eh->h_source, ETH_ALEN);
		hdr.len = htons(data_len + ENCAPS_OVERHEAD);
		
		/* 802.2 header */
		memcpy(&hdr.dsap, &encaps_hdr, sizeof(encaps_hdr));
			
		hdr.ethertype = eh->h_proto;
		err  = hermes_bap_pwrite(hw, USER_BAP, &hdr, sizeof(hdr),
					 txfid, HERMES_802_3_OFFSET);
		if (err) {
			if (net_ratelimit())
				printk(KERN_ERR "%s: Error %d writing packet "
				       "header to BAP\n", dev->name, err);
			stats->tx_errors++;
			goto fail;
		}
	} else { /* IEEE 802.3 frame */
		data_len = len + ETH_HLEN;
		data_off = HERMES_802_3_OFFSET;
		p = skb->data;
	}

	/* Round up for odd length packets */
	err = hermes_bap_pwrite(hw, USER_BAP, p, ALIGN(data_len, 2),
				txfid, data_off);
	if (err) {
		printk(KERN_ERR "%s: Error %d writing packet to BAP\n",
		       dev->name, err);
		stats->tx_errors++;
		goto fail;
	}

	/* Finally, we actually initiate the send */
	netif_stop_queue(dev);

	err = hermes_docmd_wait(hw, HERMES_CMD_TX | HERMES_CMD_RECL,
				txfid, NULL);
	if (err) {
		netif_start_queue(dev);
		printk(KERN_ERR "%s: Error %d transmitting packet\n",
		       dev->name, err);
		stats->tx_errors++;
		goto fail;
	}

	dev->trans_start = jiffies;
	stats->tx_bytes += data_off + data_len;

	orinoco_unlock(priv, &flags);

	dev_kfree_skb(skb);

	TRACE_EXIT(dev->name);

	return 0;
 fail:
	TRACE_EXIT(dev->name);

	orinoco_unlock(priv, &flags);
	return err;
}

static void __orinoco_ev_alloc(struct net_device *dev, hermes_t *hw)
{
	struct orinoco_private *priv = netdev_priv(dev);
	u16 fid = hermes_read_regn(hw, ALLOCFID);

	if (fid != priv->txfid) {
		if (fid != DUMMY_FID)
			printk(KERN_WARNING "%s: Allocate event on unexpected fid (%04X)\n",
			       dev->name, fid);
		return;
	}

	hermes_write_regn(hw, ALLOCFID, DUMMY_FID);
}

static void __orinoco_ev_tx(struct net_device *dev, hermes_t *hw)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct net_device_stats *stats = &priv->stats;

	stats->tx_packets++;

	netif_wake_queue(dev);

	hermes_write_regn(hw, TXCOMPLFID, DUMMY_FID);
}

static void __orinoco_ev_txexc(struct net_device *dev, hermes_t *hw)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct net_device_stats *stats = &priv->stats;
	u16 fid = hermes_read_regn(hw, TXCOMPLFID);
	struct hermes_tx_descriptor desc;
	int err = 0;

	if (fid == DUMMY_FID)
		return; /* Nothing's really happened */

	err = hermes_bap_pread(hw, IRQ_BAP, &desc, sizeof(desc), fid, 0);
	if (err) {
		printk(KERN_WARNING "%s: Unable to read descriptor on Tx error "
		       "(FID=%04X error %d)\n",
		       dev->name, fid, err);
	} else {
		DEBUG(1, "%s: Tx error, status %d\n",
		      dev->name, le16_to_cpu(desc.status));
	}
	
	stats->tx_errors++;

	netif_wake_queue(dev);
	hermes_write_regn(hw, TXCOMPLFID, DUMMY_FID);
}

static void orinoco_tx_timeout(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct net_device_stats *stats = &priv->stats;
	struct hermes *hw = &priv->hw;

	printk(KERN_WARNING "%s: Tx timeout! "
	       "ALLOCFID=%04x, TXCOMPLFID=%04x, EVSTAT=%04x\n",
	       dev->name, hermes_read_regn(hw, ALLOCFID),
	       hermes_read_regn(hw, TXCOMPLFID), hermes_read_regn(hw, EVSTAT));

	stats->tx_errors++;

	schedule_work(&priv->reset_work);
}

/********************************************************************/
/* Rx path (data frames)                                            */
/********************************************************************/

/* Does the frame have a SNAP header indicating it should be
 * de-encapsulated to Ethernet-II? */
static inline int is_ethersnap(void *_hdr)
{
	u8 *hdr = _hdr;

	/* We de-encapsulate all packets which, a) have SNAP headers
	 * (i.e. SSAP=DSAP=0xaa and CTRL=0x3 in the 802.2 LLC header
	 * and where b) the OUI of the SNAP header is 00:00:00 or
	 * 00:00:f8 - we need both because different APs appear to use
	 * different OUIs for some reason */
	return (memcmp(hdr, &encaps_hdr, 5) == 0)
		&& ( (hdr[5] == 0x00) || (hdr[5] == 0xf8) );
}

static inline void orinoco_spy_gather(struct net_device *dev, u_char *mac,
				      int level, int noise)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int i;

	/* Gather wireless spy statistics: for each packet, compare the
	 * source address with out list, and if match, get the stats... */
	for (i = 0; i < priv->spy_number; i++)
		if (!memcmp(mac, priv->spy_address[i], ETH_ALEN)) {
			priv->spy_stat[i].level = level - 0x95;
			priv->spy_stat[i].noise = noise - 0x95;
			priv->spy_stat[i].qual = (level > noise) ? (level - noise) : 0;
			priv->spy_stat[i].updated = 7;
		}
}

static void orinoco_stat_gather(struct net_device *dev,
				struct sk_buff *skb,
				struct hermes_rx_descriptor *desc)
{
	struct orinoco_private *priv = netdev_priv(dev);

	/* Using spy support with lots of Rx packets, like in an
	 * infrastructure (AP), will really slow down everything, because
	 * the MAC address must be compared to each entry of the spy list.
	 * If the user really asks for it (set some address in the
	 * spy list), we do it, but he will pay the price.
	 * Note that to get here, you need both WIRELESS_SPY
	 * compiled in AND some addresses in the list !!!
	 */
	/* Note : gcc will optimise the whole section away if
	 * WIRELESS_SPY is not defined... - Jean II */
	if (SPY_NUMBER(priv)) {
		orinoco_spy_gather(dev, skb->mac.raw + ETH_ALEN,
				   desc->signal, desc->silence);
	}
}

static void __orinoco_ev_rx(struct net_device *dev, hermes_t *hw)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct net_device_stats *stats = &priv->stats;
	struct iw_statistics *wstats = &priv->wstats;
	struct sk_buff *skb = NULL;
	u16 rxfid, status;
	int length, data_len, data_off;
	char *p;
	struct hermes_rx_descriptor desc;
	struct header_struct hdr;
	struct ethhdr *eh;
	int err;

	rxfid = hermes_read_regn(hw, RXFID);

	err = hermes_bap_pread(hw, IRQ_BAP, &desc, sizeof(desc),
			       rxfid, 0);
	if (err) {
		printk(KERN_ERR "%s: error %d reading Rx descriptor. "
		       "Frame dropped.\n", dev->name, err);
		stats->rx_errors++;
		goto drop;
	}

	status = le16_to_cpu(desc.status);

	if (status & HERMES_RXSTAT_ERR) {
		if (status & HERMES_RXSTAT_UNDECRYPTABLE) {
			wstats->discard.code++;
			DEBUG(1, "%s: Undecryptable frame on Rx. Frame dropped.\n",
			       dev->name);
		} else {
			stats->rx_crc_errors++;
			DEBUG(1, "%s: Bad CRC on Rx. Frame dropped.\n", dev->name);
		}
		stats->rx_errors++;
		goto drop;
	}

	/* For now we ignore the 802.11 header completely, assuming
           that the card's firmware has handled anything vital */

	err = hermes_bap_pread(hw, IRQ_BAP, &hdr, sizeof(hdr),
			       rxfid, HERMES_802_3_OFFSET);
	if (err) {
		printk(KERN_ERR "%s: error %d reading frame header. "
		       "Frame dropped.\n", dev->name, err);
		stats->rx_errors++;
		goto drop;
	}

	length = ntohs(hdr.len);
	
	/* Sanity checks */
	if (length < 3) { /* No for even an 802.2 LLC header */
		/* At least on Symbol firmware with PCF we get quite a
                   lot of these legitimately - Poll frames with no
                   data. */
		stats->rx_dropped++;
		goto drop;
	}
	if (length > IEEE802_11_DATA_LEN) {
		printk(KERN_WARNING "%s: Oversized frame received (%d bytes)\n",
		       dev->name, length);
		stats->rx_length_errors++;
		stats->rx_errors++;
		goto drop;
	}

	/* We need space for the packet data itself, plus an ethernet
	   header, plus 2 bytes so we can align the IP header on a
	   32bit boundary, plus 1 byte so we can read in odd length
	   packets from the card, which has an IO granularity of 16
	   bits */  
	skb = dev_alloc_skb(length+ETH_HLEN+2+1);
	if (!skb) {
		printk(KERN_WARNING "%s: Can't allocate skb for Rx\n",
		       dev->name);
		goto drop;
	}

	skb_reserve(skb, 2); /* This way the IP header is aligned */

	/* Handle decapsulation
	 * In most cases, the firmware tell us about SNAP frames.
	 * For some reason, the SNAP frames sent by LinkSys APs
	 * are not properly recognised by most firmwares.
	 * So, check ourselves */
	if (((status & HERMES_RXSTAT_MSGTYPE) == HERMES_RXSTAT_1042) ||
	    ((status & HERMES_RXSTAT_MSGTYPE) == HERMES_RXSTAT_TUNNEL) ||
	    is_ethersnap(&hdr)) {
		/* These indicate a SNAP within 802.2 LLC within
		   802.11 frame which we'll need to de-encapsulate to
		   the original EthernetII frame. */

		if (length < ENCAPS_OVERHEAD) { /* No room for full LLC+SNAP */
			stats->rx_length_errors++;
			goto drop;
		}

		/* Remove SNAP header, reconstruct EthernetII frame */
		data_len = length - ENCAPS_OVERHEAD;
		data_off = HERMES_802_3_OFFSET + sizeof(hdr);

		eh = (struct ethhdr *)skb_put(skb, ETH_HLEN);

		memcpy(eh, &hdr, 2 * ETH_ALEN);
		eh->h_proto = hdr.ethertype;
	} else {
		/* All other cases indicate a genuine 802.3 frame.  No
		   decapsulation needed.  We just throw the whole
		   thing in, and hope the protocol layer can deal with
		   it as 802.3 */
		data_len = length;
		data_off = HERMES_802_3_OFFSET;
		/* FIXME: we re-read from the card data we already read here */
	}

	p = skb_put(skb, data_len);
	err = hermes_bap_pread(hw, IRQ_BAP, p, ALIGN(data_len, 2),
			       rxfid, data_off);
	if (err) {
		printk(KERN_ERR "%s: error %d reading frame. "
		       "Frame dropped.\n", dev->name, err);
		stats->rx_errors++;
		goto drop;
	}

	dev->last_rx = jiffies;
	skb->dev = dev;
	skb->protocol = eth_type_trans(skb, dev);
	skb->ip_summed = CHECKSUM_NONE;
	
	/* Process the wireless stats if needed */
	orinoco_stat_gather(dev, skb, &desc);

	/* Pass the packet to the networking stack */
	netif_rx(skb);
	stats->rx_packets++;
	stats->rx_bytes += length;

	return;

 drop:	
	stats->rx_dropped++;

	if (skb)
		dev_kfree_skb_irq(skb);
	return;
}

/********************************************************************/
/* Rx path (info frames)                                            */
/********************************************************************/

static void print_linkstatus(struct net_device *dev, u16 status)
{
	char * s;

	if (suppress_linkstatus)
		return;

	switch (status) {
	case HERMES_LINKSTATUS_NOT_CONNECTED:
		s = "Not Connected";
		break;
	case HERMES_LINKSTATUS_CONNECTED:
		s = "Connected";
		break;
	case HERMES_LINKSTATUS_DISCONNECTED:
		s = "Disconnected";
		break;
	case HERMES_LINKSTATUS_AP_CHANGE:
		s = "AP Changed";
		break;
	case HERMES_LINKSTATUS_AP_OUT_OF_RANGE:
		s = "AP Out of Range";
		break;
	case HERMES_LINKSTATUS_AP_IN_RANGE:
		s = "AP In Range";
		break;
	case HERMES_LINKSTATUS_ASSOC_FAILED:
		s = "Association Failed";
		break;
	default:
		s = "UNKNOWN";
	}
	
	printk(KERN_INFO "%s: New link status: %s (%04x)\n",
	       dev->name, s, status);
}

static void __orinoco_ev_info(struct net_device *dev, hermes_t *hw)
{
	struct orinoco_private *priv = netdev_priv(dev);
	u16 infofid;
	struct {
		u16 len;
		u16 type;
	} __attribute__ ((packed)) info;
	int len, type;
	int err;

	/* This is an answer to an INQUIRE command that we did earlier,
	 * or an information "event" generated by the card
	 * The controller return to us a pseudo frame containing
	 * the information in question - Jean II */
	infofid = hermes_read_regn(hw, INFOFID);

	/* Read the info frame header - don't try too hard */
	err = hermes_bap_pread(hw, IRQ_BAP, &info, sizeof(info),
			       infofid, 0);
	if (err) {
		printk(KERN_ERR "%s: error %d reading info frame. "
		       "Frame dropped.\n", dev->name, err);
		return;
	}
	
	len = HERMES_RECLEN_TO_BYTES(le16_to_cpu(info.len));
	type = le16_to_cpu(info.type);

	switch (type) {
	case HERMES_INQ_TALLIES: {
		struct hermes_tallies_frame tallies;
		struct iw_statistics *wstats = &priv->wstats;
		
		if (len > sizeof(tallies)) {
			printk(KERN_WARNING "%s: Tallies frame too long (%d bytes)\n",
			       dev->name, len);
			len = sizeof(tallies);
		}
		
		/* Read directly the data (no seek) */
		hermes_read_words(hw, HERMES_DATA1, (void *) &tallies,
				  len / 2); /* FIXME: blech! */
		
		/* Increment our various counters */
		/* wstats->discard.nwid - no wrong BSSID stuff */
		wstats->discard.code +=
			le16_to_cpu(tallies.RxWEPUndecryptable);
		if (len == sizeof(tallies))  
			wstats->discard.code +=
				le16_to_cpu(tallies.RxDiscards_WEPICVError) +
				le16_to_cpu(tallies.RxDiscards_WEPExcluded);
		wstats->discard.misc +=
			le16_to_cpu(tallies.TxDiscardsWrongSA);
		wstats->discard.fragment +=
			le16_to_cpu(tallies.RxMsgInBadMsgFragments);
		wstats->discard.retries +=
			le16_to_cpu(tallies.TxRetryLimitExceeded);
		/* wstats->miss.beacon - no match */
	}
	break;
	case HERMES_INQ_LINKSTATUS: {
		struct hermes_linkstatus linkstatus;
		u16 newstatus;
		int connected;

		if (len != sizeof(linkstatus)) {
			printk(KERN_WARNING "%s: Unexpected size for linkstatus frame (%d bytes)\n",
			       dev->name, len);
			break;
		}

		hermes_read_words(hw, HERMES_DATA1, (void *) &linkstatus,
				  len / 2);
		newstatus = le16_to_cpu(linkstatus.linkstatus);

		connected = (newstatus == HERMES_LINKSTATUS_CONNECTED)
			|| (newstatus == HERMES_LINKSTATUS_AP_CHANGE)
			|| (newstatus == HERMES_LINKSTATUS_AP_IN_RANGE);

		if (connected)
			netif_carrier_on(dev);
		else
			netif_carrier_off(dev);

		if (newstatus != priv->last_linkstatus)
			print_linkstatus(dev, newstatus);

		priv->last_linkstatus = newstatus;
	}
	break;
	default:
		printk(KERN_DEBUG "%s: Unknown information frame received: "
		       "type 0x%04x, length %d\n", dev->name, type, len);
		/* We don't actually do anything about it */
		break;
	}
}

static void __orinoco_ev_infdrop(struct net_device *dev, hermes_t *hw)
{
	if (net_ratelimit())
		printk(KERN_DEBUG "%s: Information frame lost.\n", dev->name);
}

/********************************************************************/
/* Internal hardware control routines                               */
/********************************************************************/

int __orinoco_up(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct hermes *hw = &priv->hw;
	int err;

	err = __orinoco_program_rids(dev);
	if (err) {
		printk(KERN_ERR "%s: Error %d configuring card\n",
		       dev->name, err);
		return err;
	}

	/* Fire things up again */
	hermes_set_irqmask(hw, ORINOCO_INTEN);
	err = hermes_enable_port(hw, 0);
	if (err) {
		printk(KERN_ERR "%s: Error %d enabling MAC port\n",
		       dev->name, err);
		return err;
	}

	netif_start_queue(dev);

	return 0;
}

int __orinoco_down(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct hermes *hw = &priv->hw;
	int err;

	netif_stop_queue(dev);

	if (! priv->hw_unavailable) {
		if (! priv->broken_disableport) {
			err = hermes_disable_port(hw, 0);
			if (err) {
				/* Some firmwares (e.g. Intersil 1.3.x) seem
				 * to have problems disabling the port, oh
				 * well, too bad. */
				printk(KERN_WARNING "%s: Error %d disabling MAC port\n",
				       dev->name, err);
				priv->broken_disableport = 1;
			}
		}
		hermes_set_irqmask(hw, 0);
		hermes_write_regn(hw, EVACK, 0xffff);
	}
	
	/* firmware will have to reassociate */
	netif_carrier_off(dev);
	priv->last_linkstatus = 0xffff;

	return 0;
}

int orinoco_reinit_firmware(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct hermes *hw = &priv->hw;
	int err;

	err = hermes_init(hw);
	if (err)
		return err;

	err = hermes_allocate(hw, priv->nicbuf_size, &priv->txfid);
	if (err == -EIO) {
		/* Try workaround for old Symbol firmware bug */
		printk(KERN_WARNING "%s: firmware ALLOC bug detected "
		       "(old Symbol firmware?). Trying to work around... ",
		       dev->name);
		
		priv->nicbuf_size = TX_NICBUF_SIZE_BUG;
		err = hermes_allocate(hw, priv->nicbuf_size, &priv->txfid);
		if (err)
			printk("failed!\n");
		else
			printk("ok.\n");
	}

	return err;
}

static int __orinoco_hw_set_bitrate(struct orinoco_private *priv)
{
	hermes_t *hw = &priv->hw;
	int err = 0;

	if (priv->bitratemode >= BITRATE_TABLE_SIZE) {
		printk(KERN_ERR "%s: BUG: Invalid bitrate mode %d\n",
		       priv->ndev->name, priv->bitratemode);
		return -EINVAL;
	}

	switch (priv->firmware_type) {
	case FIRMWARE_TYPE_AGERE:
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFTXRATECONTROL,
					   bitrate_table[priv->bitratemode].agere_txratectrl);
		break;
	case FIRMWARE_TYPE_INTERSIL:
	case FIRMWARE_TYPE_SYMBOL:
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFTXRATECONTROL,
					   bitrate_table[priv->bitratemode].intersil_txratectrl);
		break;
	default:
		BUG();
	}

	return err;
}

/* Change the WEP keys and/or the current keys.  Can be called
 * either from __orinoco_hw_setup_wep() or directly from
 * orinoco_ioctl_setiwencode().  In the later case the association
 * with the AP is not broken (if the firmware can handle it),
 * which is needed for 802.1x implementations. */
static int __orinoco_hw_setup_wepkeys(struct orinoco_private *priv)
{
	hermes_t *hw = &priv->hw;
	int err = 0;

	switch (priv->firmware_type) {
	case FIRMWARE_TYPE_AGERE:
		err = HERMES_WRITE_RECORD(hw, USER_BAP,
					  HERMES_RID_CNFWEPKEYS_AGERE,
					  &priv->keys);
		if (err)
			return err;
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFTXKEY_AGERE,
					   priv->tx_key);
		if (err)
			return err;
		break;
	case FIRMWARE_TYPE_INTERSIL:
	case FIRMWARE_TYPE_SYMBOL:
		{
			int keylen;
			int i;

			/* Force uniform key length to work around firmware bugs */
			keylen = le16_to_cpu(priv->keys[priv->tx_key].len);
			
			if (keylen > LARGE_KEY_SIZE) {
				printk(KERN_ERR "%s: BUG: Key %d has oversize length %d.\n",
				       priv->ndev->name, priv->tx_key, keylen);
				return -E2BIG;
			}

			/* Write all 4 keys */
			for(i = 0; i < ORINOCO_MAX_KEYS; i++) {
				err = hermes_write_ltv(hw, USER_BAP,
						       HERMES_RID_CNFDEFAULTKEY0 + i,
						       HERMES_BYTES_TO_RECLEN(keylen),
						       priv->keys[i].data);
				if (err)
					return err;
			}

			/* Write the index of the key used in transmission */
			err = hermes_write_wordrec(hw, USER_BAP,
						   HERMES_RID_CNFWEPDEFAULTKEYID,
						   priv->tx_key);
			if (err)
				return err;
		}
		break;
	}

	return 0;
}

static int __orinoco_hw_setup_wep(struct orinoco_private *priv)
{
	hermes_t *hw = &priv->hw;
	int err = 0;
	int master_wep_flag;
	int auth_flag;

	if (priv->wep_on)
		__orinoco_hw_setup_wepkeys(priv);

	if (priv->wep_restrict)
		auth_flag = HERMES_AUTH_SHARED_KEY;
	else
		auth_flag = HERMES_AUTH_OPEN;

	switch (priv->firmware_type) {
	case FIRMWARE_TYPE_AGERE: /* Agere style WEP */
		if (priv->wep_on) {
			/* Enable the shared-key authentication. */
			err = hermes_write_wordrec(hw, USER_BAP,
						   HERMES_RID_CNFAUTHENTICATION_AGERE,
						   auth_flag);
		}
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFWEPENABLED_AGERE,
					   priv->wep_on);
		if (err)
			return err;
		break;

	case FIRMWARE_TYPE_INTERSIL: /* Intersil style WEP */
	case FIRMWARE_TYPE_SYMBOL: /* Symbol style WEP */
		if (priv->wep_on) {
			if (priv->wep_restrict ||
			    (priv->firmware_type == FIRMWARE_TYPE_SYMBOL))
				master_wep_flag = HERMES_WEP_PRIVACY_INVOKED |
						  HERMES_WEP_EXCL_UNENCRYPTED;
			else
				master_wep_flag = HERMES_WEP_PRIVACY_INVOKED;

			err = hermes_write_wordrec(hw, USER_BAP,
						   HERMES_RID_CNFAUTHENTICATION,
						   auth_flag);
			if (err)
				return err;
		} else
			master_wep_flag = 0;

		if (priv->iw_mode == IW_MODE_MONITOR)
			master_wep_flag |= HERMES_WEP_HOST_DECRYPT;

		/* Master WEP setting : on/off */
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFWEPFLAGS_INTERSIL,
					   master_wep_flag);
		if (err)
			return err;	

		break;
	}

	return 0;
}

static int __orinoco_program_rids(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int err;
	struct hermes_idstring idbuf;

	/* Set the MAC address */
	err = hermes_write_ltv(hw, USER_BAP, HERMES_RID_CNFOWNMACADDR,
			       HERMES_BYTES_TO_RECLEN(ETH_ALEN), dev->dev_addr);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting MAC address\n",
		       dev->name, err);
		return err;
	}

	/* Set up the link mode */
	err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNFPORTTYPE,
				   priv->port_type);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting port type\n",
		       dev->name, err);
		return err;
	}
	/* Set the channel/frequency */
	if (priv->channel == 0) {
		printk(KERN_DEBUG "%s: Channel is 0 in __orinoco_program_rids()\n", dev->name);
		if (priv->createibss)
			priv->channel = 10;
	}
	err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNFOWNCHANNEL,
				   priv->channel);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting channel\n",
		       dev->name, err);
		return err;
	}

	if (priv->has_ibss) {
		u16 createibss;

		if ((strlen(priv->desired_essid) == 0) && (priv->createibss)) {
			printk(KERN_WARNING "%s: This firmware requires an "
			       "ESSID in IBSS-Ad-Hoc mode.\n", dev->name);
			/* With wvlan_cs, in this case, we would crash.
			 * hopefully, this driver will behave better...
			 * Jean II */
			createibss = 0;
		} else {
			createibss = priv->createibss;
		}
		
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFCREATEIBSS,
					   createibss);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting CREATEIBSS\n",
			       dev->name, err);
			return err;
		}
	}

	/* Set the desired ESSID */
	idbuf.len = cpu_to_le16(strlen(priv->desired_essid));
	memcpy(&idbuf.val, priv->desired_essid, sizeof(idbuf.val));
	/* WinXP wants partner to configure OWNSSID even in IBSS mode. (jimc) */
	err = hermes_write_ltv(hw, USER_BAP, HERMES_RID_CNFOWNSSID,
			       HERMES_BYTES_TO_RECLEN(strlen(priv->desired_essid)+2),
			       &idbuf);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting OWNSSID\n",
		       dev->name, err);
		return err;
	}
	err = hermes_write_ltv(hw, USER_BAP, HERMES_RID_CNFDESIREDSSID,
			       HERMES_BYTES_TO_RECLEN(strlen(priv->desired_essid)+2),
			       &idbuf);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting DESIREDSSID\n",
		       dev->name, err);
		return err;
	}

	/* Set the station name */
	idbuf.len = cpu_to_le16(strlen(priv->nick));
	memcpy(&idbuf.val, priv->nick, sizeof(idbuf.val));
	err = hermes_write_ltv(hw, USER_BAP, HERMES_RID_CNFOWNNAME,
			       HERMES_BYTES_TO_RECLEN(strlen(priv->nick)+2),
			       &idbuf);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting nickname\n",
		       dev->name, err);
		return err;
	}

	/* Set AP density */
	if (priv->has_sensitivity) {
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFSYSTEMSCALE,
					   priv->ap_density);
		if (err) {
			printk(KERN_WARNING "%s: Error %d setting SYSTEMSCALE.  "
			       "Disabling sensitivity control\n",
			       dev->name, err);

			priv->has_sensitivity = 0;
		}
	}

	/* Set RTS threshold */
	err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNFRTSTHRESHOLD,
				   priv->rts_thresh);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting RTS threshold\n",
		       dev->name, err);
		return err;
	}

	/* Set fragmentation threshold or MWO robustness */
	if (priv->has_mwo)
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFMWOROBUST_AGERE,
					   priv->mwo_robust);
	else
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFFRAGMENTATIONTHRESHOLD,
					   priv->frag_thresh);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting fragmentation\n",
		       dev->name, err);
		return err;
	}

	/* Set bitrate */
	err = __orinoco_hw_set_bitrate(priv);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting bitrate\n",
		       dev->name, err);
		return err;
	}

	/* Set power management */
	if (priv->has_pm) {
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFPMENABLED,
					   priv->pm_on);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting up PM\n",
			       dev->name, err);
			return err;
		}

		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFMULTICASTRECEIVE,
					   priv->pm_mcast);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting up PM\n",
			       dev->name, err);
			return err;
		}
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFMAXSLEEPDURATION,
					   priv->pm_period);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting up PM\n",
			       dev->name, err);
			return err;
		}
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFPMHOLDOVERDURATION,
					   priv->pm_timeout);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting up PM\n",
			       dev->name, err);
			return err;
		}
	}

	/* Set preamble - only for Symbol so far... */
	if (priv->has_preamble) {
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFPREAMBLE_SYMBOL,
					   priv->preamble);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting preamble\n",
			       dev->name, err);
			return err;
		}
	}

	/* Set up encryption */
	if (priv->has_wep) {
		err = __orinoco_hw_setup_wep(priv);
		if (err) {
			printk(KERN_ERR "%s: Error %d activating WEP\n",
			       dev->name, err);
			return err;
		}
	}

	/* Set promiscuity / multicast*/
	priv->promiscuous = 0;
	priv->mc_count = 0;
	__orinoco_set_multicast_list(dev); /* FIXME: what about the xmit_lock */

	return 0;
}

/* FIXME: return int? */
static void
__orinoco_set_multicast_list(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int err = 0;
	int promisc, mc_count;

	/* The Hermes doesn't seem to have an allmulti mode, so we go
	 * into promiscuous mode and let the upper levels deal. */
	if ( (dev->flags & IFF_PROMISC) || (dev->flags & IFF_ALLMULTI) ||
	     (dev->mc_count > MAX_MULTICAST(priv)) ) {
		promisc = 1;
		mc_count = 0;
	} else {
		promisc = 0;
		mc_count = dev->mc_count;
	}

	if (promisc != priv->promiscuous) {
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFPROMISCUOUSMODE,
					   promisc);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting PROMISCUOUSMODE to 1.\n",
			       dev->name, err);
		} else 
			priv->promiscuous = promisc;
	}

	if (! promisc && (mc_count || priv->mc_count) ) {
		struct dev_mc_list *p = dev->mc_list;
		struct hermes_multicast mclist;
		int i;

		for (i = 0; i < mc_count; i++) {
			/* paranoia: is list shorter than mc_count? */
			BUG_ON(! p);
			/* paranoia: bad address size in list? */
			BUG_ON(p->dmi_addrlen != ETH_ALEN);
			
			memcpy(mclist.addr[i], p->dmi_addr, ETH_ALEN);
			p = p->next;
		}
		
		if (p)
			printk(KERN_WARNING "%s: Multicast list is "
			       "longer than mc_count\n", dev->name);

		err = hermes_write_ltv(hw, USER_BAP, HERMES_RID_CNFGROUPADDRESSES,
				       HERMES_BYTES_TO_RECLEN(priv->mc_count * ETH_ALEN),
				       &mclist);
		if (err)
			printk(KERN_ERR "%s: Error %d setting multicast list.\n",
			       dev->name, err);
		else
			priv->mc_count = mc_count;
	}

	/* Since we can set the promiscuous flag when it wasn't asked
	   for, make sure the net_device knows about it. */
	if (priv->promiscuous)
		dev->flags |= IFF_PROMISC;
	else
		dev->flags &= ~IFF_PROMISC;
}

static int orinoco_reconfigure(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct hermes *hw = &priv->hw;
	unsigned long flags;
	int err = 0;

	if (priv->broken_disableport) {
		schedule_work(&priv->reset_work);
		return 0;
	}

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;
		
	err = hermes_disable_port(hw, 0);
	if (err) {
		printk(KERN_WARNING "%s: Unable to disable port while reconfiguring card\n",
		       dev->name);
		priv->broken_disableport = 1;
		goto out;
	}

	err = __orinoco_program_rids(dev);
	if (err) {
		printk(KERN_WARNING "%s: Unable to reconfigure card\n",
		       dev->name);
		goto out;
	}

	err = hermes_enable_port(hw, 0);
	if (err) {
		printk(KERN_WARNING "%s: Unable to enable port while reconfiguring card\n",
		       dev->name);
		goto out;
	}

 out:
	if (err) {
		printk(KERN_WARNING "%s: Resetting instead...\n", dev->name);
		schedule_work(&priv->reset_work);
		err = 0;
	}

	orinoco_unlock(priv, &flags);
	return err;

}

/* This must be called from user context, without locks held - use
 * schedule_work() */
static void orinoco_reset(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct hermes *hw = &priv->hw;
	int err = 0;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		/* When the hardware becomes available again, whatever
		 * detects that is responsible for re-initializing
		 * it. So no need for anything further */
		return;

	netif_stop_queue(dev);

	/* Shut off interrupts.  Depending on what state the hardware
	 * is in, this might not work, but we'll try anyway */
	hermes_set_irqmask(hw, 0);
	hermes_write_regn(hw, EVACK, 0xffff);

	priv->hw_unavailable++;
	priv->last_linkstatus = 0xffff; /* firmware will have to reassociate */
	netif_carrier_off(dev);

	orinoco_unlock(priv, &flags);

	if (priv->hard_reset)
		err = (*priv->hard_reset)(priv);
	if (err) {
		printk(KERN_ERR "%s: orinoco_reset: Error %d "
		       "performing  hard reset\n", dev->name, err);
		/* FIXME: shutdown of some sort */
		return;
	}

	err = orinoco_reinit_firmware(dev);
	if (err) {
		printk(KERN_ERR "%s: orinoco_reset: Error %d re-initializing firmware\n",
		       dev->name, err);
		return;
	}

	spin_lock_irq(&priv->lock); /* This has to be called from user context */

	priv->hw_unavailable--;

	/* priv->open or priv->hw_unavailable might have changed while
	 * we dropped the lock */
	if (priv->open && (! priv->hw_unavailable)) {
		err = __orinoco_up(dev);
		if (err) {
			printk(KERN_ERR "%s: orinoco_reset: Error %d reenabling card\n",
			       dev->name, err);
		} else
			dev->trans_start = jiffies;
	}

	spin_unlock_irq(&priv->lock);

	return;
}

/********************************************************************/
/* Interrupt handler                                                */
/********************************************************************/

static void __orinoco_ev_tick(struct net_device *dev, hermes_t *hw)
{
	printk(KERN_DEBUG "%s: TICK\n", dev->name);
}

static void __orinoco_ev_wterr(struct net_device *dev, hermes_t *hw)
{
	/* This seems to happen a fair bit under load, but ignoring it
	   seems to work fine...*/
	printk(KERN_DEBUG "%s: MAC controller error (WTERR). Ignoring.\n",
	       dev->name);
}

irqreturn_t orinoco_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int count = MAX_IRQLOOPS_PER_IRQ;
	u16 evstat, events;
	/* These are used to detect a runaway interrupt situation */
	/* If we get more than MAX_IRQLOOPS_PER_JIFFY iterations in a jiffy,
	 * we panic and shut down the hardware */
	static int last_irq_jiffy = 0; /* jiffies value the last time
					* we were called */
	static int loops_this_jiffy = 0;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0) {
		/* If hw is unavailable - we don't know if the irq was
		 * for us or not */
		return IRQ_HANDLED;
	}

	evstat = hermes_read_regn(hw, EVSTAT);
	events = evstat & hw->inten;
	if (! events) {
		orinoco_unlock(priv, &flags);
		return IRQ_NONE;
	}
	
	if (jiffies != last_irq_jiffy)
		loops_this_jiffy = 0;
	last_irq_jiffy = jiffies;

	while (events && count--) {
		if (++loops_this_jiffy > MAX_IRQLOOPS_PER_JIFFY) {
			printk(KERN_WARNING "%s: IRQ handler is looping too "
			       "much! Resetting.\n", dev->name);
			/* Disable interrupts for now */
			hermes_set_irqmask(hw, 0);
			schedule_work(&priv->reset_work);
			break;
		}

		/* Check the card hasn't been removed */
		if (! hermes_present(hw)) {
			DEBUG(0, "orinoco_interrupt(): card removed\n");
			break;
		}

		if (events & HERMES_EV_TICK)
			__orinoco_ev_tick(dev, hw);
		if (events & HERMES_EV_WTERR)
			__orinoco_ev_wterr(dev, hw);
		if (events & HERMES_EV_INFDROP)
			__orinoco_ev_infdrop(dev, hw);
		if (events & HERMES_EV_INFO)
			__orinoco_ev_info(dev, hw);
		if (events & HERMES_EV_RX)
			__orinoco_ev_rx(dev, hw);
		if (events & HERMES_EV_TXEXC)
			__orinoco_ev_txexc(dev, hw);
		if (events & HERMES_EV_TX)
			__orinoco_ev_tx(dev, hw);
		if (events & HERMES_EV_ALLOC)
			__orinoco_ev_alloc(dev, hw);
		
		hermes_write_regn(hw, EVACK, events);

		evstat = hermes_read_regn(hw, EVSTAT);
		events = evstat & hw->inten;
	};

	orinoco_unlock(priv, &flags);
	return IRQ_HANDLED;
}

/********************************************************************/
/* Initialization                                                   */
/********************************************************************/

struct comp_id {
	u16 id, variant, major, minor;
} __attribute__ ((packed));

static inline fwtype_t determine_firmware_type(struct comp_id *nic_id)
{
	if (nic_id->id < 0x8000)
		return FIRMWARE_TYPE_AGERE;
	else if (nic_id->id == 0x8000 && nic_id->major == 0)
		return FIRMWARE_TYPE_SYMBOL;
	else
		return FIRMWARE_TYPE_INTERSIL;
}

/* Set priv->firmware type, determine firmware properties */
static int determine_firmware(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int err;
	struct comp_id nic_id, sta_id;
	unsigned int firmver;
	char tmp[SYMBOL_MAX_VER_LEN+1];

	/* Get the hardware version */
	err = HERMES_READ_RECORD(hw, USER_BAP, HERMES_RID_NICID, &nic_id);
	if (err) {
		printk(KERN_ERR "%s: Cannot read hardware identity: error %d\n",
		       dev->name, err);
		return err;
	}

	le16_to_cpus(&nic_id.id);
	le16_to_cpus(&nic_id.variant);
	le16_to_cpus(&nic_id.major);
	le16_to_cpus(&nic_id.minor);
	printk(KERN_DEBUG "%s: Hardware identity %04x:%04x:%04x:%04x\n",
	       dev->name, nic_id.id, nic_id.variant,
	       nic_id.major, nic_id.minor);

	priv->firmware_type = determine_firmware_type(&nic_id);

	/* Get the firmware version */
	err = HERMES_READ_RECORD(hw, USER_BAP, HERMES_RID_STAID, &sta_id);
	if (err) {
		printk(KERN_ERR "%s: Cannot read station identity: error %d\n",
		       dev->name, err);
		return err;
	}

	le16_to_cpus(&sta_id.id);
	le16_to_cpus(&sta_id.variant);
	le16_to_cpus(&sta_id.major);
	le16_to_cpus(&sta_id.minor);
	printk(KERN_DEBUG "%s: Station identity  %04x:%04x:%04x:%04x\n",
	       dev->name, sta_id.id, sta_id.variant,
	       sta_id.major, sta_id.minor);

	switch (sta_id.id) {
	case 0x15:
		printk(KERN_ERR "%s: Primary firmware is active\n",
		       dev->name);
		return -ENODEV;
	case 0x14b:
		printk(KERN_ERR "%s: Tertiary firmware is active\n",
		       dev->name);
		return -ENODEV;
	case 0x1f:	/* Intersil, Agere, Symbol Spectrum24 */
	case 0x21:	/* Symbol Spectrum24 Trilogy */
		break;
	default:
		printk(KERN_NOTICE "%s: Unknown station ID, please report\n",
		       dev->name);
		break;
	}

	/* Default capabilities */
	priv->has_sensitivity = 1;
	priv->has_mwo = 0;
	priv->has_preamble = 0;
	priv->has_port3 = 1;
	priv->has_ibss = 1;
	priv->has_wep = 0;
	priv->has_big_wep = 0;

	/* Determine capabilities from the firmware version */
	switch (priv->firmware_type) {
	case FIRMWARE_TYPE_AGERE:
		/* Lucent Wavelan IEEE, Lucent Orinoco, Cabletron RoamAbout,
		   ELSA, Melco, HP, IBM, Dell 1150, Compaq 110/210 */
		snprintf(priv->fw_name, sizeof(priv->fw_name) - 1,
			 "Lucent/Agere %d.%02d", sta_id.major, sta_id.minor);

		firmver = ((unsigned long)sta_id.major << 16) | sta_id.minor;

		priv->has_ibss = (firmver >= 0x60006);
		priv->has_wep = (firmver >= 0x40020);
		priv->has_big_wep = 1; /* FIXME: this is wrong - how do we tell
					  Gold cards from the others? */
		priv->has_mwo = (firmver >= 0x60000);
		priv->has_pm = (firmver >= 0x40020); /* Don't work in 7.52 ? */
		priv->ibss_port = 1;

		/* Tested with Agere firmware :
		 *	1.16 ; 4.08 ; 4.52 ; 6.04 ; 6.16 ; 7.28 => Jean II
		 * Tested CableTron firmware : 4.32 => Anton */
		break;
	case FIRMWARE_TYPE_SYMBOL:
		/* Symbol , 3Com AirConnect, Intel, Ericsson WLAN */
		/* Intel MAC : 00:02:B3:* */
		/* 3Com MAC : 00:50:DA:* */
		memset(tmp, 0, sizeof(tmp));
		/* Get the Symbol firmware version */
		err = hermes_read_ltv(hw, USER_BAP,
				      HERMES_RID_SECONDARYVERSION_SYMBOL,
				      SYMBOL_MAX_VER_LEN, NULL, &tmp);
		if (err) {
			printk(KERN_WARNING
			       "%s: Error %d reading Symbol firmware info. Wildly guessing capabilities...\n",
			       dev->name, err);
			firmver = 0;
			tmp[0] = '\0';
		} else {
			/* The firmware revision is a string, the format is
			 * something like : "V2.20-01".
			 * Quick and dirty parsing... - Jean II
			 */
			firmver = ((tmp[1] - '0') << 16) | ((tmp[3] - '0') << 12)
				| ((tmp[4] - '0') << 8) | ((tmp[6] - '0') << 4)
				| (tmp[7] - '0');

			tmp[SYMBOL_MAX_VER_LEN] = '\0';
		}

		snprintf(priv->fw_name, sizeof(priv->fw_name) - 1,
			 "Symbol %s", tmp);

		priv->has_ibss = (firmver >= 0x20000);
		priv->has_wep = (firmver >= 0x15012);
		priv->has_big_wep = (firmver >= 0x20000);
		priv->has_pm = (firmver >= 0x20000 && firmver < 0x22000) || 
			       (firmver >= 0x29000 && firmver < 0x30000) ||
			       firmver >= 0x31000;
		priv->has_preamble = (firmver >= 0x20000);
		priv->ibss_port = 4;
		/* Tested with Intel firmware : 0x20015 => Jean II */
		/* Tested with 3Com firmware : 0x15012 & 0x22001 => Jean II */
		break;
	case FIRMWARE_TYPE_INTERSIL:
		/* D-Link, Linksys, Adtron, ZoomAir, and many others...
		 * Samsung, Compaq 100/200 and Proxim are slightly
		 * different and less well tested */
		/* D-Link MAC : 00:40:05:* */
		/* Addtron MAC : 00:90:D1:* */
		snprintf(priv->fw_name, sizeof(priv->fw_name) - 1,
			 "Intersil %d.%d.%d", sta_id.major, sta_id.minor,
			 sta_id.variant);

		firmver = ((unsigned long)sta_id.major << 16) |
			((unsigned long)sta_id.minor << 8) | sta_id.variant;

		priv->has_ibss = (firmver >= 0x000700); /* FIXME */
		priv->has_big_wep = priv->has_wep = (firmver >= 0x000800);
		priv->has_pm = (firmver >= 0x000700);

		if (firmver >= 0x000800)
			priv->ibss_port = 0;
		else {
			printk(KERN_NOTICE "%s: Intersil firmware earlier "
			       "than v0.8.x - several features not supported\n",
			       dev->name);
			priv->ibss_port = 1;
		}
		break;
	}
	printk(KERN_DEBUG "%s: Firmware determined as %s\n", dev->name,
	       priv->fw_name);

	return 0;
}

static int orinoco_init(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int err = 0;
	struct hermes_idstring nickbuf;
	u16 reclen;
	int len;

	TRACE_ENTER(dev->name);

	/* No need to lock, the hw_unavailable flag is already set in
	 * alloc_orinocodev() */
	priv->nicbuf_size = IEEE802_11_FRAME_LEN + ETH_HLEN;

	/* Initialize the firmware */
	err = hermes_init(hw);
	if (err != 0) {
		printk(KERN_ERR "%s: failed to initialize firmware (err = %d)\n",
		       dev->name, err);
		goto out;
	}

	err = determine_firmware(dev);
	if (err != 0) {
		printk(KERN_ERR "%s: Incompatible firmware, aborting\n",
		       dev->name);
		goto out;
	}

	if (priv->has_port3)
		printk(KERN_DEBUG "%s: Ad-hoc demo mode supported\n", dev->name);
	if (priv->has_ibss)
		printk(KERN_DEBUG "%s: IEEE standard IBSS ad-hoc mode supported\n",
		       dev->name);
	if (priv->has_wep) {
		printk(KERN_DEBUG "%s: WEP supported, ", dev->name);
		if (priv->has_big_wep)
			printk("104-bit key\n");
		else
			printk("40-bit key\n");
	}

	/* Get the MAC address */
	err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_CNFOWNMACADDR,
			      ETH_ALEN, NULL, dev->dev_addr);
	if (err) {
		printk(KERN_WARNING "%s: failed to read MAC address!\n",
		       dev->name);
		goto out;
	}

	printk(KERN_DEBUG "%s: MAC address %02X:%02X:%02X:%02X:%02X:%02X\n",
	       dev->name, dev->dev_addr[0], dev->dev_addr[1],
	       dev->dev_addr[2], dev->dev_addr[3], dev->dev_addr[4],
	       dev->dev_addr[5]);

	/* Get the station name */
	err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_CNFOWNNAME,
			      sizeof(nickbuf), &reclen, &nickbuf);
	if (err) {
		printk(KERN_ERR "%s: failed to read station name\n",
		       dev->name);
		goto out;
	}
	if (nickbuf.len)
		len = min(IW_ESSID_MAX_SIZE, (int)le16_to_cpu(nickbuf.len));
	else
		len = min(IW_ESSID_MAX_SIZE, 2 * reclen);
	memcpy(priv->nick, &nickbuf.val, len);
	priv->nick[len] = '\0';

	printk(KERN_DEBUG "%s: Station name \"%s\"\n", dev->name, priv->nick);

	/* Get allowed channels */
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CHANNELLIST,
				  &priv->channel_mask);
	if (err) {
		printk(KERN_ERR "%s: failed to read channel list!\n",
		       dev->name);
		goto out;
	}

	/* Get initial AP density */
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNFSYSTEMSCALE,
				  &priv->ap_density);
	if (err || priv->ap_density < 1 || priv->ap_density > 3) {
		priv->has_sensitivity = 0;
	}

	/* Get initial RTS threshold */
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNFRTSTHRESHOLD,
				  &priv->rts_thresh);
	if (err) {
		printk(KERN_ERR "%s: failed to read RTS threshold!\n",
		       dev->name);
		goto out;
	}

	/* Get initial fragmentation settings */
	if (priv->has_mwo)
		err = hermes_read_wordrec(hw, USER_BAP,
					  HERMES_RID_CNFMWOROBUST_AGERE,
					  &priv->mwo_robust);
	else
		err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNFFRAGMENTATIONTHRESHOLD,
					  &priv->frag_thresh);
	if (err) {
		printk(KERN_ERR "%s: failed to read fragmentation settings!\n",
		       dev->name);
		goto out;
	}

	/* Power management setup */
	if (priv->has_pm) {
		priv->pm_on = 0;
		priv->pm_mcast = 1;
		err = hermes_read_wordrec(hw, USER_BAP,
					  HERMES_RID_CNFMAXSLEEPDURATION,
					  &priv->pm_period);
		if (err) {
			printk(KERN_ERR "%s: failed to read power management period!\n",
			       dev->name);
			goto out;
		}
		err = hermes_read_wordrec(hw, USER_BAP,
					  HERMES_RID_CNFPMHOLDOVERDURATION,
					  &priv->pm_timeout);
		if (err) {
			printk(KERN_ERR "%s: failed to read power management timeout!\n",
			       dev->name);
			goto out;
		}
	}

	/* Preamble setup */
	if (priv->has_preamble) {
		err = hermes_read_wordrec(hw, USER_BAP,
					  HERMES_RID_CNFPREAMBLE_SYMBOL,
					  &priv->preamble);
		if (err)
			goto out;
	}
		
	/* Set up the default configuration */
	priv->iw_mode = IW_MODE_INFRA;
	/* By default use IEEE/IBSS ad-hoc mode if we have it */
	priv->prefer_port3 = priv->has_port3 && (! priv->has_ibss);
	set_port_type(priv);
	priv->channel = 10; /* default channel, more-or-less arbitrary */

	priv->promiscuous = 0;
	priv->wep_on = 0;
	priv->tx_key = 0;

	err = hermes_allocate(hw, priv->nicbuf_size, &priv->txfid);
	if (err == -EIO) {
		/* Try workaround for old Symbol firmware bug */
		printk(KERN_WARNING "%s: firmware ALLOC bug detected "
		       "(old Symbol firmware?). Trying to work around... ",
		       dev->name);
		
		priv->nicbuf_size = TX_NICBUF_SIZE_BUG;
		err = hermes_allocate(hw, priv->nicbuf_size, &priv->txfid);
		if (err)
			printk("failed!\n");
		else
			printk("ok.\n");
	}
	if (err) {
		printk("%s: Error %d allocating Tx buffer\n", dev->name, err);
		goto out;
	}

	/* Make the hardware available, as long as it hasn't been
	 * removed elsewhere (e.g. by PCMCIA hot unplug) */
	spin_lock_irq(&priv->lock);
	priv->hw_unavailable--;
	spin_unlock_irq(&priv->lock);

	printk(KERN_DEBUG "%s: ready\n", dev->name);

 out:
	TRACE_EXIT(dev->name);
	return err;
}

struct net_device *alloc_orinocodev(int sizeof_card,
				    int (*hard_reset)(struct orinoco_private *))
{
	struct net_device *dev;
	struct orinoco_private *priv;

	dev = alloc_etherdev(sizeof(struct orinoco_private) + sizeof_card);
	if (! dev)
		return NULL;
	priv = netdev_priv(dev);
	priv->ndev = dev;
	if (sizeof_card)
		priv->card = (void *)((unsigned long)netdev_priv(dev)
				      + sizeof(struct orinoco_private));
	else
		priv->card = NULL;

	/* Setup / override net_device fields */
	dev->init = orinoco_init;
	dev->hard_start_xmit = orinoco_xmit;
	dev->tx_timeout = orinoco_tx_timeout;
	dev->watchdog_timeo = HZ; /* 1 second timeout */
	dev->get_stats = orinoco_get_stats;
	dev->get_wireless_stats = orinoco_get_wireless_stats;
	dev->do_ioctl = orinoco_ioctl;
	dev->change_mtu = orinoco_change_mtu;
	dev->set_multicast_list = orinoco_set_multicast_list;
	/* we use the default eth_mac_addr for setting the MAC addr */

	/* Set up default callbacks */
	dev->open = orinoco_open;
	dev->stop = orinoco_stop;
	priv->hard_reset = hard_reset;

	spin_lock_init(&priv->lock);
	priv->open = 0;
	priv->hw_unavailable = 1; /* orinoco_init() must clear this
				   * before anything else touches the
				   * hardware */
	INIT_WORK(&priv->reset_work, (void (*)(void *))orinoco_reset, dev);

	netif_carrier_off(dev);
	priv->last_linkstatus = 0xffff;

	return dev;

}

void free_orinocodev(struct net_device *dev)
{
	free_netdev(dev);
}

/********************************************************************/
/* Wireless extensions                                              */
/********************************************************************/

static int orinoco_hw_get_bssid(struct orinoco_private *priv,
				char buf[ETH_ALEN])
{
	hermes_t *hw = &priv->hw;
	int err = 0;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_CURRENTBSSID,
			      ETH_ALEN, NULL, buf);

	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_hw_get_essid(struct orinoco_private *priv, int *active,
				char buf[IW_ESSID_MAX_SIZE+1])
{
	hermes_t *hw = &priv->hw;
	int err = 0;
	struct hermes_idstring essidbuf;
	char *p = (char *)(&essidbuf.val);
	int len;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	if (strlen(priv->desired_essid) > 0) {
		/* We read the desired SSID from the hardware rather
		   than from priv->desired_essid, just in case the
		   firmware is allowed to change it on us. I'm not
		   sure about this */
		/* My guess is that the OWNSSID should always be whatever
		 * we set to the card, whereas CURRENT_SSID is the one that
		 * may change... - Jean II */
		u16 rid;

		*active = 1;

		rid = (priv->port_type == 3) ? HERMES_RID_CNFOWNSSID :
			HERMES_RID_CNFDESIREDSSID;
		
		err = hermes_read_ltv(hw, USER_BAP, rid, sizeof(essidbuf),
				      NULL, &essidbuf);
		if (err)
			goto fail_unlock;
	} else {
		*active = 0;

		err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_CURRENTSSID,
				      sizeof(essidbuf), NULL, &essidbuf);
		if (err)
			goto fail_unlock;
	}

	len = le16_to_cpu(essidbuf.len);

	memset(buf, 0, IW_ESSID_MAX_SIZE+1);
	memcpy(buf, p, len);
	buf[len] = '\0';

 fail_unlock:
	orinoco_unlock(priv, &flags);

	return err;       
}

static long orinoco_hw_get_freq(struct orinoco_private *priv)
{
	
	hermes_t *hw = &priv->hw;
	int err = 0;
	u16 channel;
	long freq = 0;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;
	
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CURRENTCHANNEL, &channel);
	if (err)
		goto out;

	/* Intersil firmware 1.3.5 returns 0 when the interface is down */
	if (channel == 0) {
		err = -EBUSY;
		goto out;
	}

	if ( (channel < 1) || (channel > NUM_CHANNELS) ) {
		printk(KERN_WARNING "%s: Channel out of range (%d)!\n",
		       priv->ndev->name, channel);
		err = -EBUSY;
		goto out;

	}
	freq = channel_frequency[channel-1] * 100000;

 out:
	orinoco_unlock(priv, &flags);

	if (err > 0)
		err = -EBUSY;
	return err ? err : freq;
}

static int orinoco_hw_get_bitratelist(struct orinoco_private *priv,
				      int *numrates, s32 *rates, int max)
{
	hermes_t *hw = &priv->hw;
	struct hermes_idstring list;
	unsigned char *p = (unsigned char *)&list.val;
	int err = 0;
	int num;
	int i;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_SUPPORTEDDATARATES,
			      sizeof(list), NULL, &list);
	orinoco_unlock(priv, &flags);

	if (err)
		return err;
	
	num = le16_to_cpu(list.len);
	*numrates = num;
	num = min(num, max);

	for (i = 0; i < num; i++) {
		rates[i] = (p[i] & 0x7f) * 500000; /* convert to bps */
	}

	return 0;
}

static int orinoco_ioctl_getiwrange(struct net_device *dev, struct iw_point *rrq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int err = 0;
	int mode;
	struct iw_range range;
	int numrates;
	int i, k;
	unsigned long flags;

	TRACE_ENTER(dev->name);

	if (!access_ok(VERIFY_WRITE, rrq->pointer, sizeof(range)))
		return -EFAULT;

	rrq->length = sizeof(range);

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	mode = priv->iw_mode;
	orinoco_unlock(priv, &flags);

	memset(&range, 0, sizeof(range));

	/* Much of this shamelessly taken from wvlan_cs.c. No idea
	 * what it all means -dgibson */
	range.we_version_compiled = WIRELESS_EXT;
	range.we_version_source = 11;

	range.min_nwid = range.max_nwid = 0; /* We don't use nwids */

	/* Set available channels/frequencies */
	range.num_channels = NUM_CHANNELS;
	k = 0;
	for (i = 0; i < NUM_CHANNELS; i++) {
		if (priv->channel_mask & (1 << i)) {
			range.freq[k].i = i + 1;
			range.freq[k].m = channel_frequency[i] * 100000;
			range.freq[k].e = 1;
			k++;
		}
		
		if (k >= IW_MAX_FREQUENCIES)
			break;
	}
	range.num_frequency = k;

	range.sensitivity = 3;

	if ((mode == IW_MODE_ADHOC) && (priv->spy_number == 0)){
		/* Quality stats meaningless in ad-hoc mode */
		range.max_qual.qual = 0;
		range.max_qual.level = 0;
		range.max_qual.noise = 0;
		range.avg_qual.qual = 0;
		range.avg_qual.level = 0;
		range.avg_qual.noise = 0;
	} else {
		range.max_qual.qual = 0x8b - 0x2f;
		range.max_qual.level = 0x2f - 0x95 - 1;
		range.max_qual.noise = 0x2f - 0x95 - 1;
		/* Need to get better values */
		range.avg_qual.qual = 0x24;
		range.avg_qual.level = 0xC2;
		range.avg_qual.noise = 0x9E;
	}

	err = orinoco_hw_get_bitratelist(priv, &numrates,
					 range.bitrate, IW_MAX_BITRATES);
	if (err)
		return err;
	range.num_bitrates = numrates;
	
	/* Set an indication of the max TCP throughput in bit/s that we can
	 * expect using this interface. May be use for QoS stuff...
	 * Jean II */
	if(numrates > 2)
		range.throughput = 5 * 1000 * 1000;	/* ~5 Mb/s */
	else
		range.throughput = 1.5 * 1000 * 1000;	/* ~1.5 Mb/s */

	range.min_rts = 0;
	range.max_rts = 2347;
	range.min_frag = 256;
	range.max_frag = 2346;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;
	if (priv->has_wep) {
		range.max_encoding_tokens = ORINOCO_MAX_KEYS;

		range.encoding_size[0] = SMALL_KEY_SIZE;
		range.num_encoding_sizes = 1;

		if (priv->has_big_wep) {
			range.encoding_size[1] = LARGE_KEY_SIZE;
			range.num_encoding_sizes = 2;
		}
	} else {
		range.num_encoding_sizes = 0;
		range.max_encoding_tokens = 0;
	}
	orinoco_unlock(priv, &flags);
		
	range.min_pmp = 0;
	range.max_pmp = 65535000;
	range.min_pmt = 0;
	range.max_pmt = 65535 * 1000;	/* ??? */
	range.pmp_flags = IW_POWER_PERIOD;
	range.pmt_flags = IW_POWER_TIMEOUT;
	range.pm_capa = IW_POWER_PERIOD | IW_POWER_TIMEOUT | IW_POWER_UNICAST_R;

	range.num_txpower = 1;
	range.txpower[0] = 15; /* 15dBm */
	range.txpower_capa = IW_TXPOW_DBM;

	range.retry_capa = IW_RETRY_LIMIT | IW_RETRY_LIFETIME;
	range.retry_flags = IW_RETRY_LIMIT;
	range.r_time_flags = IW_RETRY_LIFETIME;
	range.min_retry = 0;
	range.max_retry = 65535;	/* ??? */
	range.min_r_time = 0;
	range.max_r_time = 65535 * 1000;	/* ??? */

	if (copy_to_user(rrq->pointer, &range, sizeof(range)))
		return -EFAULT;

	TRACE_EXIT(dev->name);

	return 0;
}

static int orinoco_ioctl_setiwencode(struct net_device *dev, struct iw_point *erq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int index = (erq->flags & IW_ENCODE_INDEX) - 1;
	int setindex = priv->tx_key;
	int enable = priv->wep_on;
	int restricted = priv->wep_restrict;
	u16 xlen = 0;
	int err = 0;
	char keybuf[ORINOCO_MAX_KEY_SIZE];
	unsigned long flags;

	if (! priv->has_wep)
		return -EOPNOTSUPP;

	if (erq->pointer) {
		/* We actually have a key to set - check its length */
		if (erq->length > LARGE_KEY_SIZE)
			return -E2BIG;

		if ( (erq->length > SMALL_KEY_SIZE) && !priv->has_big_wep )
			return -E2BIG;
		
		if (copy_from_user(keybuf, erq->pointer, erq->length))
			return -EFAULT;
	}

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	if (erq->pointer) {
		if ((index < 0) || (index >= ORINOCO_MAX_KEYS))
			index = priv->tx_key;

		/* Adjust key length to a supported value */
		if (erq->length > SMALL_KEY_SIZE) {
			xlen = LARGE_KEY_SIZE;
		} else if (erq->length > 0) {
			xlen = SMALL_KEY_SIZE;
		} else
			xlen = 0;

		/* Switch on WEP if off */
		if ((!enable) && (xlen > 0)) {
			setindex = index;
			enable = 1;
		}
	} else {
		/* Important note : if the user do "iwconfig eth0 enc off",
		 * we will arrive there with an index of -1. This is valid
		 * but need to be taken care off... Jean II */
		if ((index < 0) || (index >= ORINOCO_MAX_KEYS)) {
			if((index != -1) || (erq->flags == 0)) {
				err = -EINVAL;
				goto out;
			}
		} else {
			/* Set the index : Check that the key is valid */
			if(priv->keys[index].len == 0) {
				err = -EINVAL;
				goto out;
			}
			setindex = index;
		}
	}

	if (erq->flags & IW_ENCODE_DISABLED)
		enable = 0;
	if (erq->flags & IW_ENCODE_OPEN)
		restricted = 0;
	if (erq->flags & IW_ENCODE_RESTRICTED)
		restricted = 1;

	if (erq->pointer) {
		priv->keys[index].len = cpu_to_le16(xlen);
		memset(priv->keys[index].data, 0,
		       sizeof(priv->keys[index].data));
		memcpy(priv->keys[index].data, keybuf, erq->length);
	}
	priv->tx_key = setindex;

	/* Try fast key change if connected and only keys are changed */
	if (priv->wep_on && enable && (priv->wep_restrict == restricted) &&
	    netif_carrier_ok(dev)) {
		err = __orinoco_hw_setup_wepkeys(priv);
		/* No need to commit if successful */
		goto out;
	}

	priv->wep_on = enable;
	priv->wep_restrict = restricted;

 out:
	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_getiwencode(struct net_device *dev, struct iw_point *erq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int index = (erq->flags & IW_ENCODE_INDEX) - 1;
	u16 xlen = 0;
	char keybuf[ORINOCO_MAX_KEY_SIZE];
	unsigned long flags;

	if (! priv->has_wep)
		return -EOPNOTSUPP;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	if ((index < 0) || (index >= ORINOCO_MAX_KEYS))
		index = priv->tx_key;

	erq->flags = 0;
	if (! priv->wep_on)
		erq->flags |= IW_ENCODE_DISABLED;
	erq->flags |= index + 1;

	if (priv->wep_restrict)
		erq->flags |= IW_ENCODE_RESTRICTED;
	else
		erq->flags |= IW_ENCODE_OPEN;

	xlen = le16_to_cpu(priv->keys[index].len);

	erq->length = xlen;

	memcpy(keybuf, priv->keys[index].data, ORINOCO_MAX_KEY_SIZE);

	orinoco_unlock(priv, &flags);

	if (erq->pointer) {
		if (copy_to_user(erq->pointer, keybuf, xlen))
			return -EFAULT;
	}

	return 0;
}

static int orinoco_ioctl_setessid(struct net_device *dev, struct iw_point *erq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	char essidbuf[IW_ESSID_MAX_SIZE+1];
	unsigned long flags;

	/* Note : ESSID is ignored in Ad-Hoc demo mode, but we can set it
	 * anyway... - Jean II */

	memset(&essidbuf, 0, sizeof(essidbuf));

	if (erq->flags) {
		if (erq->length > IW_ESSID_MAX_SIZE)
			return -E2BIG;
		
		if (copy_from_user(&essidbuf, erq->pointer, erq->length))
			return -EFAULT;

		essidbuf[erq->length] = '\0';
	}

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	memcpy(priv->desired_essid, essidbuf, sizeof(priv->desired_essid));

	orinoco_unlock(priv, &flags);

	return 0;
}

static int orinoco_ioctl_getessid(struct net_device *dev, struct iw_point *erq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	char essidbuf[IW_ESSID_MAX_SIZE+1];
	int active;
	int err = 0;
	unsigned long flags;

	TRACE_ENTER(dev->name);

	if (netif_running(dev)) {
		err = orinoco_hw_get_essid(priv, &active, essidbuf);
		if (err)
			return err;
	} else {
		if (orinoco_lock(priv, &flags) != 0)
			return -EBUSY;
		memcpy(essidbuf, priv->desired_essid, sizeof(essidbuf));
		orinoco_unlock(priv, &flags);
	}

	erq->flags = 1;
	erq->length = strlen(essidbuf) + 1;
	if (erq->pointer)
		if (copy_to_user(erq->pointer, essidbuf, erq->length))
			return -EFAULT;

	TRACE_EXIT(dev->name);
	
	return 0;
}

static int orinoco_ioctl_setnick(struct net_device *dev, struct iw_point *nrq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	char nickbuf[IW_ESSID_MAX_SIZE+1];
	unsigned long flags;

	if (nrq->length > IW_ESSID_MAX_SIZE)
		return -E2BIG;

	memset(nickbuf, 0, sizeof(nickbuf));

	if (copy_from_user(nickbuf, nrq->pointer, nrq->length))
		return -EFAULT;

	nickbuf[nrq->length] = '\0';
	
	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	memcpy(priv->nick, nickbuf, sizeof(priv->nick));

	orinoco_unlock(priv, &flags);

	return 0;
}

static int orinoco_ioctl_getnick(struct net_device *dev, struct iw_point *nrq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	char nickbuf[IW_ESSID_MAX_SIZE+1];
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	memcpy(nickbuf, priv->nick, IW_ESSID_MAX_SIZE+1);
	orinoco_unlock(priv, &flags);

	nrq->length = strlen(nickbuf)+1;

	if (copy_to_user(nrq->pointer, nickbuf, sizeof(nickbuf)))
		return -EFAULT;

	return 0;
}

static int orinoco_ioctl_setfreq(struct net_device *dev, struct iw_freq *frq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int chan = -1;
	unsigned long flags;

	/* We can only use this in Ad-Hoc demo mode to set the operating
	 * frequency, or in IBSS mode to set the frequency where the IBSS
	 * will be created - Jean II */
	if (priv->iw_mode != IW_MODE_ADHOC)
		return -EOPNOTSUPP;

	if ( (frq->e == 0) && (frq->m <= 1000) ) {
		/* Setting by channel number */
		chan = frq->m;
	} else {
		/* Setting by frequency - search the table */
		int mult = 1;
		int i;

		for (i = 0; i < (6 - frq->e); i++)
			mult *= 10;

		for (i = 0; i < NUM_CHANNELS; i++)
			if (frq->m == (channel_frequency[i] * mult))
				chan = i+1;
	}

	if ( (chan < 1) || (chan > NUM_CHANNELS) ||
	     ! (priv->channel_mask & (1 << (chan-1)) ) )
		return -EINVAL;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;
	priv->channel = chan;
	orinoco_unlock(priv, &flags);

	return 0;
}

static int orinoco_ioctl_getsens(struct net_device *dev, struct iw_param *srq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	u16 val;
	int err;
	unsigned long flags;

	if (!priv->has_sensitivity)
		return -EOPNOTSUPP;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;
	err = hermes_read_wordrec(hw, USER_BAP,
				  HERMES_RID_CNFSYSTEMSCALE, &val);
	orinoco_unlock(priv, &flags);

	if (err)
		return err;

	srq->value = val;
	srq->fixed = 0; /* auto */

	return 0;
}

static int orinoco_ioctl_setsens(struct net_device *dev, struct iw_param *srq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int val = srq->value;
	unsigned long flags;

	if (!priv->has_sensitivity)
		return -EOPNOTSUPP;

	if ((val < 1) || (val > 3))
		return -EINVAL;
	
	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;
	priv->ap_density = val;
	orinoco_unlock(priv, &flags);

	return 0;
}

static int orinoco_ioctl_setrts(struct net_device *dev, struct iw_param *rrq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int val = rrq->value;
	unsigned long flags;

	if (rrq->disabled)
		val = 2347;

	if ( (val < 0) || (val > 2347) )
		return -EINVAL;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	priv->rts_thresh = val;
	orinoco_unlock(priv, &flags);

	return 0;
}

static int orinoco_ioctl_setfrag(struct net_device *dev, struct iw_param *frq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int err = 0;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	if (priv->has_mwo) {
		if (frq->disabled)
			priv->mwo_robust = 0;
		else {
			if (frq->fixed)
				printk(KERN_WARNING "%s: Fixed fragmentation is "
				       "not supported on this firmware. "
				       "Using MWO robust instead.\n", dev->name);
			priv->mwo_robust = 1;
		}
	} else {
		if (frq->disabled)
			priv->frag_thresh = 2346;
		else {
			if ( (frq->value < 256) || (frq->value > 2346) )
				err = -EINVAL;
			else
				priv->frag_thresh = frq->value & ~0x1; /* must be even */
		}
	}

	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_getfrag(struct net_device *dev, struct iw_param *frq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int err = 0;
	u16 val;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;
	
	if (priv->has_mwo) {
		err = hermes_read_wordrec(hw, USER_BAP,
					  HERMES_RID_CNFMWOROBUST_AGERE,
					  &val);
		if (err)
			val = 0;

		frq->value = val ? 2347 : 0;
		frq->disabled = ! val;
		frq->fixed = 0;
	} else {
		err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNFFRAGMENTATIONTHRESHOLD,
					  &val);
		if (err)
			val = 0;

		frq->value = val;
		frq->disabled = (val >= 2346);
		frq->fixed = 1;
	}

	orinoco_unlock(priv, &flags);
	
	return err;
}

static int orinoco_ioctl_setrate(struct net_device *dev, struct iw_param *rrq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int err = 0;
	int ratemode = -1;
	int bitrate; /* 100s of kilobits */
	int i;
	unsigned long flags;
	
	/* As the user space doesn't know our highest rate, it uses -1
	 * to ask us to set the highest rate.  Test it using "iwconfig
	 * ethX rate auto" - Jean II */
	if (rrq->value == -1)
		bitrate = 110;
	else {
		if (rrq->value % 100000)
			return -EINVAL;
		bitrate = rrq->value / 100000;
	}

	if ( (bitrate != 10) && (bitrate != 20) &&
	     (bitrate != 55) && (bitrate != 110) )
		return -EINVAL;

	for (i = 0; i < BITRATE_TABLE_SIZE; i++)
		if ( (bitrate_table[i].bitrate == bitrate) &&
		     (bitrate_table[i].automatic == ! rrq->fixed) ) {
			ratemode = i;
			break;
		}
	
	if (ratemode == -1)
		return -EINVAL;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;
	priv->bitratemode = ratemode;
	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_getrate(struct net_device *dev, struct iw_param *rrq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int err = 0;
	int ratemode;
	int i;
	u16 val;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	ratemode = priv->bitratemode;

	BUG_ON((ratemode < 0) || (ratemode >= BITRATE_TABLE_SIZE));

	rrq->value = bitrate_table[ratemode].bitrate * 100000;
	rrq->fixed = ! bitrate_table[ratemode].automatic;
	rrq->disabled = 0;

	/* If the interface is running we try to find more about the
	   current mode */
	if (netif_running(dev)) {
		err = hermes_read_wordrec(hw, USER_BAP,
					  HERMES_RID_CURRENTTXRATE, &val);
		if (err)
			goto out;
		
		switch (priv->firmware_type) {
		case FIRMWARE_TYPE_AGERE: /* Lucent style rate */
			/* Note : in Lucent firmware, the return value of
			 * HERMES_RID_CURRENTTXRATE is the bitrate in Mb/s,
			 * and therefore is totally different from the
			 * encoding of HERMES_RID_CNFTXRATECONTROL.
			 * Don't forget that 6Mb/s is really 5.5Mb/s */
			if (val == 6)
				rrq->value = 5500000;
			else
				rrq->value = val * 1000000;
			break;
		case FIRMWARE_TYPE_INTERSIL: /* Intersil style rate */
		case FIRMWARE_TYPE_SYMBOL: /* Symbol style rate */
			for (i = 0; i < BITRATE_TABLE_SIZE; i++)
				if (bitrate_table[i].intersil_txratectrl == val) {
					ratemode = i;
					break;
				}
			if (i >= BITRATE_TABLE_SIZE)
				printk(KERN_INFO "%s: Unable to determine current bitrate (0x%04hx)\n",
				       dev->name, val);

			rrq->value = bitrate_table[ratemode].bitrate * 100000;
			break;
		default:
			BUG();
		}
	}

 out:
	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_setpower(struct net_device *dev, struct iw_param *prq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int err = 0;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	if (prq->disabled) {
		priv->pm_on = 0;
	} else {
		switch (prq->flags & IW_POWER_MODE) {
		case IW_POWER_UNICAST_R:
			priv->pm_mcast = 0;
			priv->pm_on = 1;
			break;
		case IW_POWER_ALL_R:
			priv->pm_mcast = 1;
			priv->pm_on = 1;
			break;
		case IW_POWER_ON:
			/* No flags : but we may have a value - Jean II */
			break;
		default:
			err = -EINVAL;
		}
		if (err)
			goto out;
		
		if (prq->flags & IW_POWER_TIMEOUT) {
			priv->pm_on = 1;
			priv->pm_timeout = prq->value / 1000;
		}
		if (prq->flags & IW_POWER_PERIOD) {
			priv->pm_on = 1;
			priv->pm_period = prq->value / 1000;
		}
		/* It's valid to not have a value if we are just toggling
		 * the flags... Jean II */
		if(!priv->pm_on) {
			err = -EINVAL;
			goto out;
		}			
	}

 out:
	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_getpower(struct net_device *dev, struct iw_param *prq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int err = 0;
	u16 enable, period, timeout, mcast;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;
	
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNFPMENABLED, &enable);
	if (err)
		goto out;

	err = hermes_read_wordrec(hw, USER_BAP,
				  HERMES_RID_CNFMAXSLEEPDURATION, &period);
	if (err)
		goto out;

	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNFPMHOLDOVERDURATION, &timeout);
	if (err)
		goto out;

	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNFMULTICASTRECEIVE, &mcast);
	if (err)
		goto out;

	prq->disabled = !enable;
	/* Note : by default, display the period */
	if ((prq->flags & IW_POWER_TYPE) == IW_POWER_TIMEOUT) {
		prq->flags = IW_POWER_TIMEOUT;
		prq->value = timeout * 1000;
	} else {
		prq->flags = IW_POWER_PERIOD;
		prq->value = period * 1000;
	}
	if (mcast)
		prq->flags |= IW_POWER_ALL_R;
	else
		prq->flags |= IW_POWER_UNICAST_R;

 out:
	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_getretry(struct net_device *dev, struct iw_param *rrq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int err = 0;
	u16 short_limit, long_limit, lifetime;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;
	
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_SHORTRETRYLIMIT,
				  &short_limit);
	if (err)
		goto out;

	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_LONGRETRYLIMIT,
				  &long_limit);
	if (err)
		goto out;

	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_MAXTRANSMITLIFETIME,
				  &lifetime);
	if (err)
		goto out;

	rrq->disabled = 0;		/* Can't be disabled */

	/* Note : by default, display the retry number */
	if ((rrq->flags & IW_RETRY_TYPE) == IW_RETRY_LIFETIME) {
		rrq->flags = IW_RETRY_LIFETIME;
		rrq->value = lifetime * 1000;	/* ??? */
	} else {
		/* By default, display the min number */
		if ((rrq->flags & IW_RETRY_MAX)) {
			rrq->flags = IW_RETRY_LIMIT | IW_RETRY_MAX;
			rrq->value = long_limit;
		} else {
			rrq->flags = IW_RETRY_LIMIT;
			rrq->value = short_limit;
			if(short_limit != long_limit)
				rrq->flags |= IW_RETRY_MIN;
		}
	}

 out:
	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_setibssport(struct net_device *dev, struct iwreq *wrq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int val = *( (int *) wrq->u.name );
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	priv->ibss_port = val ;

	/* Actually update the mode we are using */
	set_port_type(priv);

	orinoco_unlock(priv, &flags);
	return 0;
}

static int orinoco_ioctl_getibssport(struct net_device *dev, struct iwreq *wrq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int *val = (int *)wrq->u.name;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	*val = priv->ibss_port;
	orinoco_unlock(priv, &flags);

	return 0;
}

static int orinoco_ioctl_setport3(struct net_device *dev, struct iwreq *wrq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int val = *( (int *) wrq->u.name );
	int err = 0;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	switch (val) {
	case 0: /* Try to do IEEE ad-hoc mode */
		if (! priv->has_ibss) {
			err = -EINVAL;
			break;
		}
		priv->prefer_port3 = 0;
			
		break;

	case 1: /* Try to do Lucent proprietary ad-hoc mode */
		if (! priv->has_port3) {
			err = -EINVAL;
			break;
		}
		priv->prefer_port3 = 1;
		break;

	default:
		err = -EINVAL;
	}

	if (! err)
		/* Actually update the mode we are using */
		set_port_type(priv);

	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_getport3(struct net_device *dev, struct iwreq *wrq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int *val = (int *)wrq->u.name;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	*val = priv->prefer_port3;
	orinoco_unlock(priv, &flags);
	return 0;
}

/* Spy is used for link quality/strength measurements in Ad-Hoc mode
 * Jean II */
static int orinoco_ioctl_setspy(struct net_device *dev, struct iw_point *srq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct sockaddr address[IW_MAX_SPY];
	int number = srq->length;
	int i;
	int err = 0;
	unsigned long flags;

	/* Check the number of addresses */
	if (number > IW_MAX_SPY)
		return -E2BIG;

	/* Get the data in the driver */
	if (srq->pointer) {
		if (copy_from_user(address, srq->pointer,
				   sizeof(struct sockaddr) * number))
			return -EFAULT;
	}

	/* Make sure nobody mess with the structure while we do */
	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	/* orinoco_lock() doesn't disable interrupts, so make sure the
	 * interrupt rx path don't get confused while we copy */
	priv->spy_number = 0;

	if (number > 0) {
		/* Extract the addresses */
		for (i = 0; i < number; i++)
			memcpy(priv->spy_address[i], address[i].sa_data,
			       ETH_ALEN);
		/* Reset stats */
		memset(priv->spy_stat, 0,
		       sizeof(struct iw_quality) * IW_MAX_SPY);
		/* Set number of addresses */
		priv->spy_number = number;
	}

	/* Now, let the others play */
	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_getspy(struct net_device *dev, struct iw_point *srq)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct sockaddr address[IW_MAX_SPY];
	struct iw_quality spy_stat[IW_MAX_SPY];
	int number;
	int i;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	number = priv->spy_number;
	if ((number > 0) && (srq->pointer)) {
		/* Create address struct */
		for (i = 0; i < number; i++) {
			memcpy(address[i].sa_data, priv->spy_address[i],
			       ETH_ALEN);
			address[i].sa_family = AF_UNIX;
		}
		/* Copy stats */
		/* In theory, we should disable irqs while copying the stats
		 * because the rx path might update it in the middle...
		 * Bah, who care ? - Jean II */
		memcpy(&spy_stat, priv->spy_stat,
		       sizeof(struct iw_quality) * IW_MAX_SPY);
		for (i=0; i < number; i++)
			priv->spy_stat[i].updated = 0;
	}

	orinoco_unlock(priv, &flags);

	/* Push stuff to user space */
	srq->length = number;
	if(copy_to_user(srq->pointer, address,
			 sizeof(struct sockaddr) * number))
		return -EFAULT;
	if(copy_to_user(srq->pointer + (sizeof(struct sockaddr)*number),
			&spy_stat, sizeof(struct iw_quality) * number))
		return -EFAULT;

	return 0;
}

static int
orinoco_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct iwreq *wrq = (struct iwreq *)rq;
	int err = 0;
	int tmp;
	int changed = 0;
	unsigned long flags;

	TRACE_ENTER(dev->name);

	/* In theory, we could allow most of the the SET stuff to be
	 * done. In practice, the lapse of time at startup when the
	 * card is not ready is very short, so why bother...  Note
	 * that netif_device_present is different from up/down
	 * (ifconfig), when the device is not yet up, it is usually
	 * already ready...  Jean II */
	if (! netif_device_present(dev))
		return -ENODEV;

	switch (cmd) {
	case SIOCGIWNAME:
		strcpy(wrq->u.name, "IEEE 802.11-DS");
		break;
		
	case SIOCGIWAP:
		wrq->u.ap_addr.sa_family = ARPHRD_ETHER;
		err = orinoco_hw_get_bssid(priv, wrq->u.ap_addr.sa_data);
		break;

	case SIOCGIWRANGE:
		err = orinoco_ioctl_getiwrange(dev, &wrq->u.data);
		break;

	case SIOCSIWMODE:
		if (orinoco_lock(priv, &flags) != 0)
			return -EBUSY;
		switch (wrq->u.mode) {
		case IW_MODE_ADHOC:
			if (! (priv->has_ibss || priv->has_port3) )
				err = -EINVAL;
			else {
				priv->iw_mode = IW_MODE_ADHOC;
				changed = 1;
			}
			break;

		case IW_MODE_INFRA:
			priv->iw_mode = IW_MODE_INFRA;
			changed = 1;
			break;

		default:
			err = -EINVAL;
			break;
		}
		set_port_type(priv);
		orinoco_unlock(priv, &flags);
		break;

	case SIOCGIWMODE:
		if (orinoco_lock(priv, &flags) != 0)
			return -EBUSY;
		wrq->u.mode = priv->iw_mode;
		orinoco_unlock(priv, &flags);
		break;

	case SIOCSIWENCODE:
		err = orinoco_ioctl_setiwencode(dev, &wrq->u.encoding);
		if (! err)
			changed = 1;
		break;

	case SIOCGIWENCODE:
		if (! capable(CAP_NET_ADMIN)) {
			err = -EPERM;
			break;
		}

		err = orinoco_ioctl_getiwencode(dev, &wrq->u.encoding);
		break;

	case SIOCSIWESSID:
		err = orinoco_ioctl_setessid(dev, &wrq->u.essid);
		if (! err)
			changed = 1;
		break;

	case SIOCGIWESSID:
		err = orinoco_ioctl_getessid(dev, &wrq->u.essid);
		break;

	case SIOCSIWNICKN:
		err = orinoco_ioctl_setnick(dev, &wrq->u.data);
		if (! err)
			changed = 1;
		break;

	case SIOCGIWNICKN:
		err = orinoco_ioctl_getnick(dev, &wrq->u.data);
		break;

	case SIOCGIWFREQ:
		tmp = orinoco_hw_get_freq(priv);
		if (tmp < 0) {
			err = tmp;
		} else {
			wrq->u.freq.m = tmp;
			wrq->u.freq.e = 1;
		}
		break;

	case SIOCSIWFREQ:
		err = orinoco_ioctl_setfreq(dev, &wrq->u.freq);
		if (! err)
			changed = 1;
		break;

	case SIOCGIWSENS:
		err = orinoco_ioctl_getsens(dev, &wrq->u.sens);
		break;

	case SIOCSIWSENS:
		err = orinoco_ioctl_setsens(dev, &wrq->u.sens);
		if (! err)
			changed = 1;
		break;

	case SIOCGIWRTS:
		wrq->u.rts.value = priv->rts_thresh;
		wrq->u.rts.disabled = (wrq->u.rts.value == 2347);
		wrq->u.rts.fixed = 1;
		break;

	case SIOCSIWRTS:
		err = orinoco_ioctl_setrts(dev, &wrq->u.rts);
		if (! err)
			changed = 1;
		break;

	case SIOCSIWFRAG:
		err = orinoco_ioctl_setfrag(dev, &wrq->u.frag);
		if (! err)
			changed = 1;
		break;

	case SIOCGIWFRAG:
		err = orinoco_ioctl_getfrag(dev, &wrq->u.frag);
		break;

	case SIOCSIWRATE:
		err = orinoco_ioctl_setrate(dev, &wrq->u.bitrate);
		if (! err)
			changed = 1;
		break;

	case SIOCGIWRATE:
		err = orinoco_ioctl_getrate(dev, &wrq->u.bitrate);
		break;

	case SIOCSIWPOWER:
		err = orinoco_ioctl_setpower(dev, &wrq->u.power);
		if (! err)
			changed = 1;
		break;

	case SIOCGIWPOWER:
		err = orinoco_ioctl_getpower(dev, &wrq->u.power);
		break;

	case SIOCGIWTXPOW:
		/* The card only supports one tx power, so this is easy */
		wrq->u.txpower.value = 15; /* dBm */
		wrq->u.txpower.fixed = 1;
		wrq->u.txpower.disabled = 0;
		wrq->u.txpower.flags = IW_TXPOW_DBM;
		break;

	case SIOCSIWRETRY:
		err = -EOPNOTSUPP;
		break;

	case SIOCGIWRETRY:
		err = orinoco_ioctl_getretry(dev, &wrq->u.retry);
		break;

	case SIOCSIWSPY:
		err = orinoco_ioctl_setspy(dev, &wrq->u.data);
		break;

	case SIOCGIWSPY:
		err = orinoco_ioctl_getspy(dev, &wrq->u.data);
		break;

	case SIOCGIWPRIV:
		if (wrq->u.data.pointer) {
			struct iw_priv_args privtab[] = {
				{ SIOCIWFIRSTPRIV + 0x0, 0, 0, "force_reset" },
				{ SIOCIWFIRSTPRIV + 0x1, 0, 0, "card_reset" },
				{ SIOCIWFIRSTPRIV + 0x2,
				  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
				  0, "set_port3" },
				{ SIOCIWFIRSTPRIV + 0x3, 0,
				  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
				  "get_port3" },
				{ SIOCIWFIRSTPRIV + 0x4,
				  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
				  0, "set_preamble" },
				{ SIOCIWFIRSTPRIV + 0x5, 0,
				  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
				  "get_preamble" },
				{ SIOCIWFIRSTPRIV + 0x6,
				  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
				  0, "set_ibssport" },
				{ SIOCIWFIRSTPRIV + 0x7, 0,
				  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
				  "get_ibssport" },
				{ SIOCIWLASTPRIV, 0, 0, "dump_recs" },
			};

			wrq->u.data.length = sizeof(privtab) / sizeof(privtab[0]);
			if (copy_to_user(wrq->u.data.pointer, privtab, sizeof(privtab)))
				err = -EFAULT;
		}
		break;
	       
	case SIOCIWFIRSTPRIV + 0x0: /* force_reset */
	case SIOCIWFIRSTPRIV + 0x1: /* card_reset */
		if (! capable(CAP_NET_ADMIN)) {
			err = -EPERM;
			break;
		}
		
		printk(KERN_DEBUG "%s: Force scheduling reset!\n", dev->name);

		schedule_work(&priv->reset_work);
		break;

	case SIOCIWFIRSTPRIV + 0x2: /* set_port3 */
		if (! capable(CAP_NET_ADMIN)) {
			err = -EPERM;
			break;
		}

		err = orinoco_ioctl_setport3(dev, wrq);
		if (! err)
			changed = 1;
		break;

	case SIOCIWFIRSTPRIV + 0x3: /* get_port3 */
		err = orinoco_ioctl_getport3(dev, wrq);
		break;

	case SIOCIWFIRSTPRIV + 0x4: /* set_preamble */
		if (! capable(CAP_NET_ADMIN)) {
			err = -EPERM;
			break;
		}

		/* 802.11b has recently defined some short preamble.
		 * Basically, the Phy header has been reduced in size.
		 * This increase performance, especially at high rates
		 * (the preamble is transmitted at 1Mb/s), unfortunately
		 * this give compatibility troubles... - Jean II */
		if(priv->has_preamble) {
			int val = *( (int *) wrq->u.name );

			if (orinoco_lock(priv, &flags) != 0)
				return -EBUSY;
			if (val)
				priv->preamble = 1;
			else
				priv->preamble = 0;
			orinoco_unlock(priv, &flags);
			changed = 1;
		} else
			err = -EOPNOTSUPP;
		break;

	case SIOCIWFIRSTPRIV + 0x5: /* get_preamble */
		if(priv->has_preamble) {
			int *val = (int *)wrq->u.name;

			if (orinoco_lock(priv, &flags) != 0)
				return -EBUSY;
			*val = priv->preamble;
			orinoco_unlock(priv, &flags);
		} else
			err = -EOPNOTSUPP;
		break;
	case SIOCIWFIRSTPRIV + 0x6: /* set_ibssport */
		if (! capable(CAP_NET_ADMIN)) {
			err = -EPERM;
			break;
		}

		err = orinoco_ioctl_setibssport(dev, wrq);
		if (! err)
			changed = 1;
		break;

	case SIOCIWFIRSTPRIV + 0x7: /* get_ibssport */
		err = orinoco_ioctl_getibssport(dev, wrq);
		break;

	case SIOCIWLASTPRIV:
		err = orinoco_debug_dump_recs(dev);
		if (err)
			printk(KERN_ERR "%s: Unable to dump records (%d)\n",
			       dev->name, err);
		break;


	default:
		err = -EOPNOTSUPP;
	}
	
	if (! err && changed && netif_running(dev)) {
		err = orinoco_reconfigure(dev);
	}		

	TRACE_EXIT(dev->name);

	return err;
}

struct {
	u16 rid;
	char *name;
	int displaytype;
#define DISPLAY_WORDS	0
#define DISPLAY_BYTES	1
#define DISPLAY_STRING	2
#define DISPLAY_XSTRING	3
} record_table[] = {
#define DEBUG_REC(name,type) { HERMES_RID_##name, #name, DISPLAY_##type }
	DEBUG_REC(CNFPORTTYPE,WORDS),
	DEBUG_REC(CNFOWNMACADDR,BYTES),
	DEBUG_REC(CNFDESIREDSSID,STRING),
	DEBUG_REC(CNFOWNCHANNEL,WORDS),
	DEBUG_REC(CNFOWNSSID,STRING),
	DEBUG_REC(CNFOWNATIMWINDOW,WORDS),
	DEBUG_REC(CNFSYSTEMSCALE,WORDS),
	DEBUG_REC(CNFMAXDATALEN,WORDS),
	DEBUG_REC(CNFPMENABLED,WORDS),
	DEBUG_REC(CNFPMEPS,WORDS),
	DEBUG_REC(CNFMULTICASTRECEIVE,WORDS),
	DEBUG_REC(CNFMAXSLEEPDURATION,WORDS),
	DEBUG_REC(CNFPMHOLDOVERDURATION,WORDS),
	DEBUG_REC(CNFOWNNAME,STRING),
	DEBUG_REC(CNFOWNDTIMPERIOD,WORDS),
	DEBUG_REC(CNFMULTICASTPMBUFFERING,WORDS),
	DEBUG_REC(CNFWEPENABLED_AGERE,WORDS),
	DEBUG_REC(CNFMANDATORYBSSID_SYMBOL,WORDS),
	DEBUG_REC(CNFWEPDEFAULTKEYID,WORDS),
	DEBUG_REC(CNFDEFAULTKEY0,BYTES),
	DEBUG_REC(CNFDEFAULTKEY1,BYTES),
	DEBUG_REC(CNFMWOROBUST_AGERE,WORDS),
	DEBUG_REC(CNFDEFAULTKEY2,BYTES),
	DEBUG_REC(CNFDEFAULTKEY3,BYTES),
	DEBUG_REC(CNFWEPFLAGS_INTERSIL,WORDS),
	DEBUG_REC(CNFWEPKEYMAPPINGTABLE,WORDS),
	DEBUG_REC(CNFAUTHENTICATION,WORDS),
	DEBUG_REC(CNFMAXASSOCSTA,WORDS),
	DEBUG_REC(CNFKEYLENGTH_SYMBOL,WORDS),
	DEBUG_REC(CNFTXCONTROL,WORDS),
	DEBUG_REC(CNFROAMINGMODE,WORDS),
	DEBUG_REC(CNFHOSTAUTHENTICATION,WORDS),
	DEBUG_REC(CNFRCVCRCERROR,WORDS),
	DEBUG_REC(CNFMMLIFE,WORDS),
	DEBUG_REC(CNFALTRETRYCOUNT,WORDS),
	DEBUG_REC(CNFBEACONINT,WORDS),
	DEBUG_REC(CNFAPPCFINFO,WORDS),
	DEBUG_REC(CNFSTAPCFINFO,WORDS),
	DEBUG_REC(CNFPRIORITYQUSAGE,WORDS),
	DEBUG_REC(CNFTIMCTRL,WORDS),
	DEBUG_REC(CNFTHIRTY2TALLY,WORDS),
	DEBUG_REC(CNFENHSECURITY,WORDS),
	DEBUG_REC(CNFGROUPADDRESSES,BYTES),
	DEBUG_REC(CNFCREATEIBSS,WORDS),
	DEBUG_REC(CNFFRAGMENTATIONTHRESHOLD,WORDS),
	DEBUG_REC(CNFRTSTHRESHOLD,WORDS),
	DEBUG_REC(CNFTXRATECONTROL,WORDS),
	DEBUG_REC(CNFPROMISCUOUSMODE,WORDS),
	DEBUG_REC(CNFBASICRATES_SYMBOL,WORDS),
	DEBUG_REC(CNFPREAMBLE_SYMBOL,WORDS),
	DEBUG_REC(CNFSHORTPREAMBLE,WORDS),
	DEBUG_REC(CNFWEPKEYS_AGERE,BYTES),
	DEBUG_REC(CNFEXCLUDELONGPREAMBLE,WORDS),
	DEBUG_REC(CNFTXKEY_AGERE,WORDS),
	DEBUG_REC(CNFAUTHENTICATIONRSPTO,WORDS),
	DEBUG_REC(CNFBASICRATES,WORDS),
	DEBUG_REC(CNFSUPPORTEDRATES,WORDS),
	DEBUG_REC(CNFTICKTIME,WORDS),
	DEBUG_REC(CNFSCANREQUEST,WORDS),
	DEBUG_REC(CNFJOINREQUEST,WORDS),
	DEBUG_REC(CNFAUTHENTICATESTATION,WORDS),
	DEBUG_REC(CNFCHANNELINFOREQUEST,WORDS),
	DEBUG_REC(MAXLOADTIME,WORDS),
	DEBUG_REC(DOWNLOADBUFFER,WORDS),
	DEBUG_REC(PRIID,WORDS),
	DEBUG_REC(PRISUPRANGE,WORDS),
	DEBUG_REC(CFIACTRANGES,WORDS),
	DEBUG_REC(NICSERNUM,XSTRING),
	DEBUG_REC(NICID,WORDS),
	DEBUG_REC(MFISUPRANGE,WORDS),
	DEBUG_REC(CFISUPRANGE,WORDS),
	DEBUG_REC(CHANNELLIST,WORDS),
	DEBUG_REC(REGULATORYDOMAINS,WORDS),
	DEBUG_REC(TEMPTYPE,WORDS),
/*  	DEBUG_REC(CIS,BYTES), */
	DEBUG_REC(STAID,WORDS),
	DEBUG_REC(CURRENTSSID,STRING),
	DEBUG_REC(CURRENTBSSID,BYTES),
	DEBUG_REC(COMMSQUALITY,WORDS),
	DEBUG_REC(CURRENTTXRATE,WORDS),
	DEBUG_REC(CURRENTBEACONINTERVAL,WORDS),
	DEBUG_REC(CURRENTSCALETHRESHOLDS,WORDS),
	DEBUG_REC(PROTOCOLRSPTIME,WORDS),
	DEBUG_REC(SHORTRETRYLIMIT,WORDS),
	DEBUG_REC(LONGRETRYLIMIT,WORDS),
	DEBUG_REC(MAXTRANSMITLIFETIME,WORDS),
	DEBUG_REC(MAXRECEIVELIFETIME,WORDS),
	DEBUG_REC(CFPOLLABLE,WORDS),
	DEBUG_REC(AUTHENTICATIONALGORITHMS,WORDS),
	DEBUG_REC(PRIVACYOPTIONIMPLEMENTED,WORDS),
	DEBUG_REC(OWNMACADDR,BYTES),
	DEBUG_REC(SCANRESULTSTABLE,WORDS),
	DEBUG_REC(PHYTYPE,WORDS),
	DEBUG_REC(CURRENTCHANNEL,WORDS),
	DEBUG_REC(CURRENTPOWERSTATE,WORDS),
	DEBUG_REC(CCAMODE,WORDS),
	DEBUG_REC(SUPPORTEDDATARATES,WORDS),
	DEBUG_REC(BUILDSEQ,BYTES),
	DEBUG_REC(FWID,XSTRING)
#undef DEBUG_REC
};

#define DEBUG_LTV_SIZE		128

static int orinoco_debug_dump_recs(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	u8 *val8;
	u16 *val16;
	int i,j;
	u16 length;
	int err;

	/* I'm not sure: we might have a lock here, so we'd better go
           atomic, just in case. */
	val8 = kmalloc(DEBUG_LTV_SIZE + 2, GFP_ATOMIC);
	if (! val8)
		return -ENOMEM;
	val16 = (u16 *)val8;

	for (i = 0; i < ARRAY_SIZE(record_table); i++) {
		u16 rid = record_table[i].rid;
		int len;

		memset(val8, 0, DEBUG_LTV_SIZE + 2);

		err = hermes_read_ltv(hw, USER_BAP, rid, DEBUG_LTV_SIZE,
				      &length, val8);
		if (err) {
			DEBUG(0, "Error %d reading RID 0x%04x\n", err, rid);
			continue;
		}
		val16 = (u16 *)val8;
		if (length == 0)
			continue;

		printk(KERN_DEBUG "%-15s (0x%04x): length=%d (%d bytes)\tvalue=",
		       record_table[i].name,
		       rid, length, (length-1)*2);
		len = min(((int)length-1)*2, DEBUG_LTV_SIZE);

		switch (record_table[i].displaytype) {
		case DISPLAY_WORDS:
			for (j = 0; j < len / 2; j++)
				printk("%04X-", le16_to_cpu(val16[j]));
			break;

		case DISPLAY_BYTES:
		default:
			for (j = 0; j < len; j++)
				printk("%02X:", val8[j]);
			break;

		case DISPLAY_STRING:
			len = min(len, le16_to_cpu(val16[0])+2);
			val8[len] = '\0';
			printk("\"%s\"", (char *)&val16[1]);
			break;

		case DISPLAY_XSTRING:
			printk("'%s'", (char *)val8);
		}

		printk("\n");
	}

	kfree(val8);

	return 0;
}

/********************************************************************/
/* Debugging                                                        */
/********************************************************************/

#if 0
static void show_rx_frame(struct orinoco_rxframe_hdr *frame)
{
	printk(KERN_DEBUG "RX descriptor:\n");
	printk(KERN_DEBUG "  status      = 0x%04x\n", frame->desc.status);
	printk(KERN_DEBUG "  time        = 0x%08x\n", frame->desc.time);
	printk(KERN_DEBUG "  silence     = 0x%02x\n", frame->desc.silence);
	printk(KERN_DEBUG "  signal      = 0x%02x\n", frame->desc.signal);
	printk(KERN_DEBUG "  rate        = 0x%02x\n", frame->desc.rate);
	printk(KERN_DEBUG "  rxflow      = 0x%02x\n", frame->desc.rxflow);
	printk(KERN_DEBUG "  reserved    = 0x%08x\n", frame->desc.reserved);

	printk(KERN_DEBUG "IEEE 802.11 header:\n");
	printk(KERN_DEBUG "  frame_ctl   = 0x%04x\n",
	       frame->p80211.frame_ctl);
	printk(KERN_DEBUG "  duration_id = 0x%04x\n",
	       frame->p80211.duration_id);
	printk(KERN_DEBUG "  addr1       = %02x:%02x:%02x:%02x:%02x:%02x\n",
	       frame->p80211.addr1[0], frame->p80211.addr1[1],
	       frame->p80211.addr1[2], frame->p80211.addr1[3],
	       frame->p80211.addr1[4], frame->p80211.addr1[5]);
	printk(KERN_DEBUG "  addr2       = %02x:%02x:%02x:%02x:%02x:%02x\n",
	       frame->p80211.addr2[0], frame->p80211.addr2[1],
	       frame->p80211.addr2[2], frame->p80211.addr2[3],
	       frame->p80211.addr2[4], frame->p80211.addr2[5]);
	printk(KERN_DEBUG "  addr3       = %02x:%02x:%02x:%02x:%02x:%02x\n",
	       frame->p80211.addr3[0], frame->p80211.addr3[1],
	       frame->p80211.addr3[2], frame->p80211.addr3[3],
	       frame->p80211.addr3[4], frame->p80211.addr3[5]);
	printk(KERN_DEBUG "  seq_ctl     = 0x%04x\n",
	       frame->p80211.seq_ctl);
	printk(KERN_DEBUG "  addr4       = %02x:%02x:%02x:%02x:%02x:%02x\n",
	       frame->p80211.addr4[0], frame->p80211.addr4[1],
	       frame->p80211.addr4[2], frame->p80211.addr4[3],
	       frame->p80211.addr4[4], frame->p80211.addr4[5]);
	printk(KERN_DEBUG "  data_len    = 0x%04x\n",
	       frame->p80211.data_len);

	printk(KERN_DEBUG "IEEE 802.3 header:\n");
	printk(KERN_DEBUG "  dest        = %02x:%02x:%02x:%02x:%02x:%02x\n",
	       frame->p8023.h_dest[0], frame->p8023.h_dest[1],
	       frame->p8023.h_dest[2], frame->p8023.h_dest[3],
	       frame->p8023.h_dest[4], frame->p8023.h_dest[5]);
	printk(KERN_DEBUG "  src         = %02x:%02x:%02x:%02x:%02x:%02x\n",
	       frame->p8023.h_source[0], frame->p8023.h_source[1],
	       frame->p8023.h_source[2], frame->p8023.h_source[3],
	       frame->p8023.h_source[4], frame->p8023.h_source[5]);
	printk(KERN_DEBUG "  len         = 0x%04x\n", frame->p8023.h_proto);

	printk(KERN_DEBUG "IEEE 802.2 LLC/SNAP header:\n");
	printk(KERN_DEBUG "  DSAP        = 0x%02x\n", frame->p8022.dsap);
	printk(KERN_DEBUG "  SSAP        = 0x%02x\n", frame->p8022.ssap);
	printk(KERN_DEBUG "  ctrl        = 0x%02x\n", frame->p8022.ctrl);
	printk(KERN_DEBUG "  OUI         = %02x:%02x:%02x\n",
	       frame->p8022.oui[0], frame->p8022.oui[1], frame->p8022.oui[2]);
	printk(KERN_DEBUG "  ethertype  = 0x%04x\n", frame->ethertype);
}
#endif /* 0 */

/********************************************************************/
/* Module initialization                                            */
/********************************************************************/

EXPORT_SYMBOL(alloc_orinocodev);
EXPORT_SYMBOL(free_orinocodev);

EXPORT_SYMBOL(__orinoco_up);
EXPORT_SYMBOL(__orinoco_down);
EXPORT_SYMBOL(orinoco_stop);
EXPORT_SYMBOL(orinoco_reinit_firmware);

EXPORT_SYMBOL(orinoco_interrupt);

/* Can't be declared "const" or the whole __initdata section will
 * become const */
static char version[] __initdata = DRIVER_NAME " " DRIVER_VERSION
	" (David Gibson <hermes@gibson.dropbear.id.au>, "
	"Pavel Roskin <proski@gnu.org>, et al)";

static int __init init_orinoco(void)
{
	printk(KERN_DEBUG "%s\n", version);
	return 0;
}

static void __exit exit_orinoco(void)
{
}

module_init(init_orinoco);
module_exit(exit_orinoco);
