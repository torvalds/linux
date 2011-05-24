/*+M*************************************************************************
 * Adaptec AIC7xxx device driver for Linux.
 *
 * Copyright (c) 1994 John Aycock
 *   The University of Calgary Department of Computer Science.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Sources include the Adaptec 1740 driver (aha1740.c), the Ultrastor 24F
 * driver (ultrastor.c), various Linux kernel source, the Adaptec EISA
 * config file (!adp7771.cfg), the Adaptec AHA-2740A Series User's Guide,
 * the Linux Kernel Hacker's Guide, Writing a SCSI Device Driver for Linux,
 * the Adaptec 1542 driver (aha1542.c), the Adaptec EISA overlay file
 * (adp7770.ovl), the Adaptec AHA-2740 Series Technical Reference Manual,
 * the Adaptec AIC-7770 Data Book, the ANSI SCSI specification, the
 * ANSI SCSI-2 specification (draft 10c), ...
 *
 * --------------------------------------------------------------------------
 *
 *  Modifications by Daniel M. Eischen (deischen@iworks.InterWorks.org):
 *
 *  Substantially modified to include support for wide and twin bus
 *  adapters, DMAing of SCBs, tagged queueing, IRQ sharing, bug fixes,
 *  SCB paging, and other rework of the code.
 *
 *  Parts of this driver were also based on the FreeBSD driver by
 *  Justin T. Gibbs.  His copyright follows:
 *
 * --------------------------------------------------------------------------  
 * Copyright (c) 1994-1997 Justin Gibbs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Where this Software is combined with software released under the terms of 
 * the GNU General Public License ("GPL") and the terms of the GPL would require the 
 * combined work to also be released under the terms of the GPL, the terms
 * and conditions of this License will apply in addition to those of the
 * GPL with the exception of any terms or conditions of this License that
 * conflict with, or are expressly prohibited by, the GPL.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      $Id: aic7xxx.c,v 1.119 1997/06/27 19:39:18 gibbs Exp $
 *---------------------------------------------------------------------------
 *
 *  Thanks also go to (in alphabetical order) the following:
 *
 *    Rory Bolt     - Sequencer bug fixes
 *    Jay Estabrook - Initial DEC Alpha support
 *    Doug Ledford  - Much needed abort/reset bug fixes
 *    Kai Makisara  - DMAing of SCBs
 *
 *  A Boot time option was also added for not resetting the scsi bus.
 *
 *    Form:  aic7xxx=extended
 *           aic7xxx=no_reset
 *           aic7xxx=ultra
 *           aic7xxx=irq_trigger:[0,1]  # 0 edge, 1 level
 *           aic7xxx=verbose
 *
 *  Daniel M. Eischen, deischen@iworks.InterWorks.org, 1/23/97
 *
 *  $Id: aic7xxx.c,v 4.1 1997/06/12 08:23:42 deang Exp $
 *-M*************************************************************************/

/*+M**************************************************************************
 *
 * Further driver modifications made by Doug Ledford <dledford@redhat.com>
 *
 * Copyright (c) 1997-1999 Doug Ledford
 *
 * These changes are released under the same licensing terms as the FreeBSD
 * driver written by Justin Gibbs.  Please see his Copyright notice above
 * for the exact terms and conditions covering my changes as well as the
 * warranty statement.
 *
 * Modifications made to the aic7xxx.c,v 4.1 driver from Dan Eischen include
 * but are not limited to:
 *
 *  1: Import of the latest FreeBSD sequencer code for this driver
 *  2: Modification of kernel code to accommodate different sequencer semantics
 *  3: Extensive changes throughout kernel portion of driver to improve
 *     abort/reset processing and error hanndling
 *  4: Other work contributed by various people on the Internet
 *  5: Changes to printk information and verbosity selection code
 *  6: General reliability related changes, especially in IRQ management
 *  7: Modifications to the default probe/attach order for supported cards
 *  8: SMP friendliness has been improved
 *
 * Overall, this driver represents a significant departure from the official
 * aic7xxx driver released by Dan Eischen in two ways.  First, in the code
 * itself.  A diff between the two version of the driver is now a several
 * thousand line diff.  Second, in approach to solving the same problem.  The
 * problem is importing the FreeBSD aic7xxx driver code to linux can be a
 * difficult and time consuming process, that also can be error prone.  Dan
 * Eischen's official driver uses the approach that the linux and FreeBSD
 * drivers should be as identical as possible.  To that end, his next version
 * of this driver will be using a mid-layer code library that he is developing
 * to moderate communications between the linux mid-level SCSI code and the
 * low level FreeBSD driver.  He intends to be able to essentially drop the
 * FreeBSD driver into the linux kernel with only a few minor tweaks to some
 * include files and the like and get things working, making for fast easy
 * imports of the FreeBSD code into linux.
 *
 * I disagree with Dan's approach.  Not that I don't think his way of doing
 * things would be nice, easy to maintain, and create a more uniform driver
 * between FreeBSD and Linux.  I have no objection to those issues.  My
 * disagreement is on the needed functionality.  There simply are certain
 * things that are done differently in FreeBSD than linux that will cause
 * problems for this driver regardless of any middle ware Dan implements.
 * The biggest example of this at the moment is interrupt semantics.  Linux
 * doesn't provide the same protection techniques as FreeBSD does, nor can
 * they be easily implemented in any middle ware code since they would truly
 * belong in the kernel proper and would effect all drivers.  For the time
 * being, I see issues such as these as major stumbling blocks to the 
 * reliability of code based upon such middle ware.  Therefore, I choose to
 * use a different approach to importing the FreeBSD code that doesn't
 * involve any middle ware type code.  My approach is to import the sequencer
 * code from FreeBSD wholesale.  Then, to only make changes in the kernel
 * portion of the driver as they are needed for the new sequencer semantics.
 * In this way, the portion of the driver that speaks to the rest of the
 * linux kernel is fairly static and can be changed/modified to solve
 * any problems one might encounter without concern for the FreeBSD driver.
 *
 * Note: If time and experience should prove me wrong that the middle ware
 * code Dan writes is reliable in its operation, then I'll retract my above
 * statements.  But, for those that don't know, I'm from Missouri (in the US)
 * and our state motto is "The Show-Me State".  Well, before I will put
 * faith into it, you'll have to show me that it works :)
 *
 *_M*************************************************************************/

/*
 * The next three defines are user configurable.  These should be the only
 * defines a user might need to get in here and change.  There are other
 * defines buried deeper in the code, but those really shouldn't need touched
 * under normal conditions.
 */

/*
 * AIC7XXX_STRICT_PCI_SETUP
 *   Should we assume the PCI config options on our controllers are set with
 *   sane and proper values, or should we be anal about our PCI config
 *   registers and force them to what we want?  The main advantage to
 *   defining this option is on non-Intel hardware where the BIOS may not
 *   have been run to set things up, or if you have one of the BIOSless
 *   Adaptec controllers, such as a 2910, that don't get set up by the
 *   BIOS.  However, keep in mind that we really do set the most important
 *   items in the driver regardless of this setting, this only controls some
 *   of the more esoteric PCI options on these cards.  In that sense, I
 *   would default to leaving this off.  However, if people wish to try
 *   things both ways, that would also help me to know if there are some
 *   machines where it works one way but not another.
 *
 *   -- July 7, 17:09
 *     OK...I need this on my machine for testing, so the default is to
 *     leave it defined.
 *
 *   -- July 7, 18:49
 *     I needed it for testing, but it didn't make any difference, so back
 *     off she goes.
 *
 *   -- July 16, 23:04
 *     I turned it back on to try and compensate for the 2.1.x PCI code
 *     which no longer relies solely on the BIOS and now tries to set
 *     things itself.
 */

#define AIC7XXX_STRICT_PCI_SETUP

/*
 * AIC7XXX_VERBOSE_DEBUGGING
 *   This option enables a lot of extra printk();s in the code, surrounded
 *   by if (aic7xxx_verbose ...) statements.  Executing all of those if
 *   statements and the extra checks can get to where it actually does have
 *   an impact on CPU usage and such, as well as code size.  Disabling this
 *   define will keep some of those from becoming part of the code.
 *
 *   NOTE:  Currently, this option has no real effect, I will be adding the
 *   various #ifdef's in the code later when I've decided a section is
 *   complete and no longer needs debugging.  OK...a lot of things are now
 *   surrounded by this define, so turning this off does have an impact.
 */
 
/*
 * #define AIC7XXX_VERBOSE_DEBUGGING
 */
 
#include <linux/module.h>
#include <stdarg.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/byteorder.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include "scsi.h"
#include <scsi/scsi_host.h>
#include "aic7xxx_old/aic7xxx.h"

#include "aic7xxx_old/sequencer.h"
#include "aic7xxx_old/scsi_message.h"
#include "aic7xxx_old/aic7xxx_reg.h"
#include <scsi/scsicam.h>

#include <linux/stat.h>
#include <linux/slab.h>        /* for kmalloc() */

#define AIC7XXX_C_VERSION  "5.2.6"

#define ALL_TARGETS -1
#define ALL_CHANNELS -1
#define ALL_LUNS -1
#define MAX_TARGETS  16
#define MAX_LUNS     8
#ifndef TRUE
#  define TRUE 1
#endif
#ifndef FALSE
#  define FALSE 0
#endif

#if defined(__powerpc__) || defined(__i386__) || defined(__x86_64__)
#  define MMAPIO
#endif

/*
 * You can try raising me for better performance or lowering me if you have
 * flaky devices that go off the scsi bus when hit with too many tagged
 * commands (like some IBM SCSI-3 LVD drives).
 */
#define AIC7XXX_CMDS_PER_DEVICE 32

typedef struct
{
  unsigned char tag_commands[16];   /* Allow for wide/twin adapters. */
} adapter_tag_info_t;

/*
 * Make a define that will tell the driver not to the default tag depth
 * everywhere.
 */
#define DEFAULT_TAG_COMMANDS {0, 0, 0, 0, 0, 0, 0, 0,\
                              0, 0, 0, 0, 0, 0, 0, 0}

/*
 * Modify this as you see fit for your system.  By setting tag_commands
 * to 0, the driver will use it's own algorithm for determining the
 * number of commands to use (see above).  When 255, the driver will
 * not enable tagged queueing for that particular device.  When positive
 * (> 0) and (< 255) the values in the array are used for the queue_depth.
 * Note that the maximum value for an entry is 254, but you're insane if
 * you try to use that many commands on one device.
 *
 * In this example, the first line will disable tagged queueing for all
 * the devices on the first probed aic7xxx adapter.
 *
 * The second line enables tagged queueing with 4 commands/LUN for IDs
 * (1, 2-11, 13-15), disables tagged queueing for ID 12, and tells the
 * driver to use its own algorithm for ID 1.
 *
 * The third line is the same as the first line.
 *
 * The fourth line disables tagged queueing for devices 0 and 3.  It
 * enables tagged queueing for the other IDs, with 16 commands/LUN
 * for IDs 1 and 4, 127 commands/LUN for ID 8, and 4 commands/LUN for
 * IDs 2, 5-7, and 9-15.
 */

/*
 * NOTE: The below structure is for reference only, the actual structure
 *       to modify in order to change things is found after this fake one.
 *
adapter_tag_info_t aic7xxx_tag_info[] =
{
  {DEFAULT_TAG_COMMANDS},
  {{4, 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 255, 4, 4, 4}},
  {DEFAULT_TAG_COMMANDS},
  {{255, 16, 4, 255, 16, 4, 4, 4, 127, 4, 4, 4, 4, 4, 4, 4}}
};
*/

static adapter_tag_info_t aic7xxx_tag_info[] =
{
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS},
  {DEFAULT_TAG_COMMANDS}
};


/*
 * Define an array of board names that can be indexed by aha_type.
 * Don't forget to change this when changing the types!
 */
static const char *board_names[] = {
  "AIC-7xxx Unknown",                                   /* AIC_NONE */
  "Adaptec AIC-7810 Hardware RAID Controller",          /* AIC_7810 */
  "Adaptec AIC-7770 SCSI host adapter",                 /* AIC_7770 */
  "Adaptec AHA-274X SCSI host adapter",                 /* AIC_7771 */
  "Adaptec AHA-284X SCSI host adapter",                 /* AIC_284x */
  "Adaptec AIC-7850 SCSI host adapter",                 /* AIC_7850 */
  "Adaptec AIC-7855 SCSI host adapter",                 /* AIC_7855 */
  "Adaptec AIC-7860 Ultra SCSI host adapter",           /* AIC_7860 */
  "Adaptec AHA-2940A Ultra SCSI host adapter",          /* AIC_7861 */
  "Adaptec AIC-7870 SCSI host adapter",                 /* AIC_7870 */
  "Adaptec AHA-294X SCSI host adapter",                 /* AIC_7871 */
  "Adaptec AHA-394X SCSI host adapter",                 /* AIC_7872 */
  "Adaptec AHA-398X SCSI host adapter",                 /* AIC_7873 */
  "Adaptec AHA-2944 SCSI host adapter",                 /* AIC_7874 */
  "Adaptec AIC-7880 Ultra SCSI host adapter",           /* AIC_7880 */
  "Adaptec AHA-294X Ultra SCSI host adapter",           /* AIC_7881 */
  "Adaptec AHA-394X Ultra SCSI host adapter",           /* AIC_7882 */
  "Adaptec AHA-398X Ultra SCSI host adapter",           /* AIC_7883 */
  "Adaptec AHA-2944 Ultra SCSI host adapter",           /* AIC_7884 */
  "Adaptec AHA-2940UW Pro Ultra SCSI host adapter",     /* AIC_7887 */
  "Adaptec AIC-7895 Ultra SCSI host adapter",           /* AIC_7895 */
  "Adaptec AIC-7890/1 Ultra2 SCSI host adapter",        /* AIC_7890 */
  "Adaptec AHA-293X Ultra2 SCSI host adapter",          /* AIC_7890 */
  "Adaptec AHA-294X Ultra2 SCSI host adapter",          /* AIC_7890 */
  "Adaptec AIC-7896/7 Ultra2 SCSI host adapter",        /* AIC_7896 */
  "Adaptec AHA-394X Ultra2 SCSI host adapter",          /* AIC_7897 */
  "Adaptec AHA-395X Ultra2 SCSI host adapter",          /* AIC_7897 */
  "Adaptec PCMCIA SCSI controller",                     /* card bus stuff */
  "Adaptec AIC-7892 Ultra 160/m SCSI host adapter",     /* AIC_7892 */
  "Adaptec AIC-7899 Ultra 160/m SCSI host adapter",     /* AIC_7899 */
};

/*
 * There should be a specific return value for this in scsi.h, but
 * it seems that most drivers ignore it.
 */
#define DID_UNDERFLOW   DID_ERROR

/*
 *  What we want to do is have the higher level scsi driver requeue
 *  the command to us. There is no specific driver status for this
 *  condition, but the higher level scsi driver will requeue the
 *  command on a DID_BUS_BUSY error.
 *
 *  Upon further inspection and testing, it seems that DID_BUS_BUSY
 *  will *always* retry the command.  We can get into an infinite loop
 *  if this happens when we really want some sort of counter that
 *  will automatically abort/reset the command after so many retries.
 *  Using DID_ERROR will do just that.  (Made by a suggestion by
 *  Doug Ledford 8/1/96)
 */
#define DID_RETRY_COMMAND DID_ERROR

#define HSCSIID        0x07
#define SCSI_RESET     0x040

/*
 * EISA/VL-bus stuff
 */
#define MINSLOT                1
#define MAXSLOT                15
#define SLOTBASE(x)        ((x) << 12)
#define BASE_TO_SLOT(x) ((x) >> 12)

/*
 * Standard EISA Host ID regs  (Offset from slot base)
 */
#define AHC_HID0              0x80   /* 0,1: msb of ID2, 2-7: ID1      */
#define AHC_HID1              0x81   /* 0-4: ID3, 5-7: LSB ID2         */
#define AHC_HID2              0x82   /* product                        */
#define AHC_HID3              0x83   /* firmware revision              */

/*
 * AIC-7770 I/O range to reserve for a card
 */
#define MINREG                0xC00
#define MAXREG                0xCFF

#define INTDEF                0x5C      /* Interrupt Definition Register */

/*
 * AIC-78X0 PCI registers
 */
#define        CLASS_PROGIF_REVID        0x08
#define                DEVREVID        0x000000FFul
#define                PROGINFC        0x0000FF00ul
#define                SUBCLASS        0x00FF0000ul
#define                BASECLASS        0xFF000000ul

#define        CSIZE_LATTIME                0x0C
#define                CACHESIZE        0x0000003Ful        /* only 5 bits */
#define                LATTIME                0x0000FF00ul

#define        DEVCONFIG                0x40
#define                SCBSIZE32        0x00010000ul        /* aic789X only */
#define                MPORTMODE        0x00000400ul        /* aic7870 only */
#define                RAMPSM           0x00000200ul        /* aic7870 only */
#define                RAMPSM_ULTRA2    0x00000004
#define                VOLSENSE         0x00000100ul
#define                SCBRAMSEL        0x00000080ul
#define                SCBRAMSEL_ULTRA2 0x00000008
#define                MRDCEN           0x00000040ul
#define                EXTSCBTIME       0x00000020ul        /* aic7870 only */
#define                EXTSCBPEN        0x00000010ul        /* aic7870 only */
#define                BERREN           0x00000008ul
#define                DACEN            0x00000004ul
#define                STPWLEVEL        0x00000002ul
#define                DIFACTNEGEN      0x00000001ul        /* aic7870 only */

#define        SCAMCTL                  0x1a                /* Ultra2 only  */
#define        CCSCBBADDR               0xf0                /* aic7895/6/7  */

/*
 * Define the different types of SEEPROMs on aic7xxx adapters
 * and make it also represent the address size used in accessing
 * its registers.  The 93C46 chips have 1024 bits organized into
 * 64 16-bit words, while the 93C56 chips have 2048 bits organized
 * into 128 16-bit words.  The C46 chips use 6 bits to address
 * each word, while the C56 and C66 (4096 bits) use 8 bits to
 * address each word.
 */
typedef enum {C46 = 6, C56_66 = 8} seeprom_chip_type;

/*
 *
 * Define the format of the SEEPROM registers (16 bits).
 *
 */
struct seeprom_config {

/*
 * SCSI ID Configuration Flags
 */
#define CFXFER                0x0007      /* synchronous transfer rate */
#define CFSYNCH               0x0008      /* enable synchronous transfer */
#define CFDISC                0x0010      /* enable disconnection */
#define CFWIDEB               0x0020      /* wide bus device (wide card) */
#define CFSYNCHISULTRA        0x0040      /* CFSYNC is an ultra offset */
#define CFNEWULTRAFORMAT      0x0080      /* Use the Ultra2 SEEPROM format */
#define CFSTART               0x0100      /* send start unit SCSI command */
#define CFINCBIOS             0x0200      /* include in BIOS scan */
#define CFRNFOUND             0x0400      /* report even if not found */
#define CFMULTILUN            0x0800      /* probe mult luns in BIOS scan */
#define CFWBCACHEYES          0x4000      /* Enable W-Behind Cache on drive */
#define CFWBCACHENC           0xc000      /* Don't change W-Behind Cache */
/* UNUSED                0x3000 */
  unsigned short device_flags[16];        /* words 0-15 */

/*
 * BIOS Control Bits
 */
#define CFSUPREM        0x0001  /* support all removable drives */
#define CFSUPREMB       0x0002  /* support removable drives for boot only */
#define CFBIOSEN        0x0004  /* BIOS enabled */
/* UNUSED                0x0008 */
#define CFSM2DRV        0x0010  /* support more than two drives */
#define CF284XEXTEND    0x0020  /* extended translation (284x cards) */
/* UNUSED                0x0040 */
#define CFEXTEND        0x0080  /* extended translation enabled */
/* UNUSED                0xFF00 */
  unsigned short bios_control;  /* word 16 */

/*
 * Host Adapter Control Bits
 */
#define CFAUTOTERM      0x0001  /* Perform Auto termination */
#define CFULTRAEN       0x0002  /* Ultra SCSI speed enable (Ultra cards) */
#define CF284XSELTO     0x0003  /* Selection timeout (284x cards) */
#define CF284XFIFO      0x000C  /* FIFO Threshold (284x cards) */
#define CFSTERM         0x0004  /* SCSI low byte termination */
#define CFWSTERM        0x0008  /* SCSI high byte termination (wide card) */
#define CFSPARITY       0x0010  /* SCSI parity */
#define CF284XSTERM     0x0020  /* SCSI low byte termination (284x cards) */
#define CFRESETB        0x0040  /* reset SCSI bus at boot */
#define CFBPRIMARY      0x0100  /* Channel B primary on 7895 chipsets */
#define CFSEAUTOTERM    0x0400  /* aic7890 Perform SE Auto Term */
#define CFLVDSTERM      0x0800  /* aic7890 LVD Termination */
/* UNUSED                0xF280 */
  unsigned short adapter_control;        /* word 17 */

/*
 * Bus Release, Host Adapter ID
 */
#define CFSCSIID        0x000F                /* host adapter SCSI ID */
/* UNUSED                0x00F0 */
#define CFBRTIME        0xFF00                /* bus release time */
  unsigned short brtime_id;                /* word 18 */

/*
 * Maximum targets
 */
#define CFMAXTARG        0x00FF        /* maximum targets */
/* UNUSED                0xFF00 */
  unsigned short max_targets;                /* word 19 */

  unsigned short res_1[11];                /* words 20-30 */
  unsigned short checksum;                /* word 31 */
};

#define SELBUS_MASK                0x0a
#define         SELNARROW        0x00
#define         SELBUSB                0x08
#define SINGLE_BUS                0x00

#define SCB_TARGET(scb)         \
       (((scb)->hscb->target_channel_lun & TID) >> 4)
#define SCB_LUN(scb)            \
       ((scb)->hscb->target_channel_lun & LID)
#define SCB_IS_SCSIBUS_B(scb)   \
       (((scb)->hscb->target_channel_lun & SELBUSB) != 0)

/*
 * If an error occurs during a data transfer phase, run the command
 * to completion - it's easier that way - making a note of the error
 * condition in this location. This then will modify a DID_OK status
 * into an appropriate error for the higher-level SCSI code.
 */
#define aic7xxx_error(cmd)        ((cmd)->SCp.Status)

/*
 * Keep track of the targets returned status.
 */
#define aic7xxx_status(cmd)        ((cmd)->SCp.sent_command)

/*
 * The position of the SCSI commands scb within the scb array.
 */
#define aic7xxx_position(cmd)        ((cmd)->SCp.have_data_in)

/*
 * The stored DMA mapping for single-buffer data transfers.
 */
#define aic7xxx_mapping(cmd)	     ((cmd)->SCp.phase)

/*
 * Get out private data area from a scsi cmd pointer
 */
#define AIC_DEV(cmd)	((struct aic_dev_data *)(cmd)->device->hostdata)

/*
 * So we can keep track of our host structs
 */
static struct aic7xxx_host *first_aic7xxx = NULL;

/*
 * As of Linux 2.1, the mid-level SCSI code uses virtual addresses
 * in the scatter-gather lists.  We need to convert the virtual
 * addresses to physical addresses.
 */
struct hw_scatterlist {
  unsigned int address;
  unsigned int length;
};

/*
 * Maximum number of SG segments these cards can support.
 */
#define        AIC7XXX_MAX_SG 128

/*
 * The maximum number of SCBs we could have for ANY type
 * of card. DON'T FORGET TO CHANGE THE SCB MASK IN THE
 * SEQUENCER CODE IF THIS IS MODIFIED!
 */
#define AIC7XXX_MAXSCB        255


struct aic7xxx_hwscb {
/* ------------    Begin hardware supported fields    ---------------- */
/* 0*/  unsigned char control;
/* 1*/  unsigned char target_channel_lun;       /* 4/1/3 bits */
/* 2*/  unsigned char target_status;
/* 3*/  unsigned char SG_segment_count;
/* 4*/  unsigned int  SG_list_pointer;
/* 8*/  unsigned char residual_SG_segment_count;
/* 9*/  unsigned char residual_data_count[3];
/*12*/  unsigned int  data_pointer;
/*16*/  unsigned int  data_count;
/*20*/  unsigned int  SCSI_cmd_pointer;
/*24*/  unsigned char SCSI_cmd_length;
/*25*/  unsigned char tag;          /* Index into our kernel SCB array.
                                     * Also used as the tag for tagged I/O
                                     */
#define SCB_PIO_TRANSFER_SIZE  26   /* amount we need to upload/download
                                     * via PIO to initialize a transaction.
                                     */
/*26*/  unsigned char next;         /* Used to thread SCBs awaiting selection
                                     * or disconnected down in the sequencer.
                                     */
/*27*/  unsigned char prev;
/*28*/  unsigned int pad;           /*
                                     * Unused by the kernel, but we require
                                     * the padding so that the array of
                                     * hardware SCBs is aligned on 32 byte
                                     * boundaries so the sequencer can index
                                     */
};

typedef enum {
        SCB_FREE                = 0x0000,
        SCB_DTR_SCB             = 0x0001,
        SCB_WAITINGQ            = 0x0002,
        SCB_ACTIVE              = 0x0004,
        SCB_SENSE               = 0x0008,
        SCB_ABORT               = 0x0010,
        SCB_DEVICE_RESET        = 0x0020,
        SCB_RESET               = 0x0040,
        SCB_RECOVERY_SCB        = 0x0080,
        SCB_MSGOUT_PPR          = 0x0100,
        SCB_MSGOUT_SENT         = 0x0200,
        SCB_MSGOUT_SDTR         = 0x0400,
        SCB_MSGOUT_WDTR         = 0x0800,
        SCB_MSGOUT_BITS         = SCB_MSGOUT_PPR |
                                  SCB_MSGOUT_SENT | 
                                  SCB_MSGOUT_SDTR |
                                  SCB_MSGOUT_WDTR,
        SCB_QUEUED_ABORT        = 0x1000,
        SCB_QUEUED_FOR_DONE     = 0x2000,
        SCB_WAS_BUSY            = 0x4000,
	SCB_QUEUE_FULL		= 0x8000
} scb_flag_type;

typedef enum {
        AHC_FNONE                 = 0x00000000,
        AHC_PAGESCBS              = 0x00000001,
        AHC_CHANNEL_B_PRIMARY     = 0x00000002,
        AHC_USEDEFAULTS           = 0x00000004,
        AHC_INDIRECT_PAGING       = 0x00000008,
        AHC_CHNLB                 = 0x00000020,
        AHC_CHNLC                 = 0x00000040,
        AHC_EXTEND_TRANS_A        = 0x00000100,
        AHC_EXTEND_TRANS_B        = 0x00000200,
        AHC_TERM_ENB_A            = 0x00000400,
        AHC_TERM_ENB_SE_LOW       = 0x00000400,
        AHC_TERM_ENB_B            = 0x00000800,
        AHC_TERM_ENB_SE_HIGH      = 0x00000800,
        AHC_HANDLING_REQINITS     = 0x00001000,
        AHC_TARGETMODE            = 0x00002000,
        AHC_NEWEEPROM_FMT         = 0x00004000,
 /*
  *  Here ends the FreeBSD defined flags and here begins the linux defined
  *  flags.  NOTE: I did not preserve the old flag name during this change
  *  specifically to force me to evaluate what flags were being used properly
  *  and what flags weren't.  This way, I could clean up the flag usage on
  *  a use by use basis.  Doug Ledford
  */
        AHC_MOTHERBOARD           = 0x00020000,
        AHC_NO_STPWEN             = 0x00040000,
        AHC_RESET_DELAY           = 0x00080000,
        AHC_A_SCANNED             = 0x00100000,
        AHC_B_SCANNED             = 0x00200000,
        AHC_MULTI_CHANNEL         = 0x00400000,
        AHC_BIOS_ENABLED          = 0x00800000,
        AHC_SEEPROM_FOUND         = 0x01000000,
        AHC_TERM_ENB_LVD          = 0x02000000,
        AHC_ABORT_PENDING         = 0x04000000,
        AHC_RESET_PENDING         = 0x08000000,
#define AHC_IN_ISR_BIT              28
        AHC_IN_ISR                = 0x10000000,
        AHC_IN_ABORT              = 0x20000000,
        AHC_IN_RESET              = 0x40000000,
        AHC_EXTERNAL_SRAM         = 0x80000000
} ahc_flag_type;

typedef enum {
  AHC_NONE             = 0x0000,
  AHC_CHIPID_MASK      = 0x00ff,
  AHC_AIC7770          = 0x0001,
  AHC_AIC7850          = 0x0002,
  AHC_AIC7860          = 0x0003,
  AHC_AIC7870          = 0x0004,
  AHC_AIC7880          = 0x0005,
  AHC_AIC7890          = 0x0006,
  AHC_AIC7895          = 0x0007,
  AHC_AIC7896          = 0x0008,
  AHC_AIC7892          = 0x0009,
  AHC_AIC7899          = 0x000a,
  AHC_VL               = 0x0100,
  AHC_EISA             = 0x0200,
  AHC_PCI              = 0x0400,
} ahc_chip;

typedef enum {
  AHC_FENONE           = 0x0000,
  AHC_ULTRA            = 0x0001,
  AHC_ULTRA2           = 0x0002,
  AHC_WIDE             = 0x0004,
  AHC_TWIN             = 0x0008,
  AHC_MORE_SRAM        = 0x0010,
  AHC_CMD_CHAN         = 0x0020,
  AHC_QUEUE_REGS       = 0x0040,
  AHC_SG_PRELOAD       = 0x0080,
  AHC_SPIOCAP          = 0x0100,
  AHC_ULTRA3           = 0x0200,
  AHC_NEW_AUTOTERM     = 0x0400,
  AHC_AIC7770_FE       = AHC_FENONE,
  AHC_AIC7850_FE       = AHC_SPIOCAP,
  AHC_AIC7860_FE       = AHC_ULTRA|AHC_SPIOCAP,
  AHC_AIC7870_FE       = AHC_FENONE,
  AHC_AIC7880_FE       = AHC_ULTRA,
  AHC_AIC7890_FE       = AHC_MORE_SRAM|AHC_CMD_CHAN|AHC_ULTRA2|
                         AHC_QUEUE_REGS|AHC_SG_PRELOAD|AHC_NEW_AUTOTERM,
  AHC_AIC7895_FE       = AHC_MORE_SRAM|AHC_CMD_CHAN|AHC_ULTRA,
  AHC_AIC7896_FE       = AHC_AIC7890_FE,
  AHC_AIC7892_FE       = AHC_AIC7890_FE|AHC_ULTRA3,
  AHC_AIC7899_FE       = AHC_AIC7890_FE|AHC_ULTRA3,
} ahc_feature;

#define SCB_DMA_ADDR(scb, addr) ((unsigned long)(addr) + (scb)->scb_dma->dma_offset)

struct aic7xxx_scb_dma {
	unsigned long	       dma_offset;    /* Correction you have to add
					       * to virtual address to get
					       * dma handle in this region */
	dma_addr_t	       dma_address;   /* DMA handle of the start,
					       * for unmap */
	unsigned int	       dma_len;	      /* DMA length */
};

typedef enum {
  AHC_BUG_NONE            = 0x0000,
  AHC_BUG_TMODE_WIDEODD   = 0x0001,
  AHC_BUG_AUTOFLUSH       = 0x0002,
  AHC_BUG_CACHETHEN       = 0x0004,
  AHC_BUG_CACHETHEN_DIS   = 0x0008,
  AHC_BUG_PCI_2_1_RETRY   = 0x0010,
  AHC_BUG_PCI_MWI         = 0x0020,
  AHC_BUG_SCBCHAN_UPLOAD  = 0x0040,
} ahc_bugs;

struct aic7xxx_scb {
	struct aic7xxx_hwscb	*hscb;		/* corresponding hardware scb */
	struct scsi_cmnd	*cmd;		/* scsi_cmnd for this scb */
	struct aic7xxx_scb	*q_next;        /* next scb in queue */
	volatile scb_flag_type	flags;		/* current state of scb */
	struct hw_scatterlist	*sg_list;	/* SG list in adapter format */
	unsigned char		tag_action;
	unsigned char		sg_count;
	unsigned char		*sense_cmd;	/*
						 * Allocate 6 characters for
						 * sense command.
						 */
	unsigned char		*cmnd;
	unsigned int		sg_length;	/*
						 * We init this during
						 * buildscb so we don't have
						 * to calculate anything during
						 * underflow/overflow/stat code
						 */
	void			*kmalloc_ptr;
	struct aic7xxx_scb_dma	*scb_dma;
};

/*
 * Define a linked list of SCBs.
 */
typedef struct {
  struct aic7xxx_scb *head;
  struct aic7xxx_scb *tail;
} scb_queue_type;

static struct {
  unsigned char errno;
  const char *errmesg;
} hard_error[] = {
  { ILLHADDR,  "Illegal Host Access" },
  { ILLSADDR,  "Illegal Sequencer Address referenced" },
  { ILLOPCODE, "Illegal Opcode in sequencer program" },
  { SQPARERR,  "Sequencer Ram Parity Error" },
  { DPARERR,   "Data-Path Ram Parity Error" },
  { MPARERR,   "Scratch Ram/SCB Array Ram Parity Error" },
  { PCIERRSTAT,"PCI Error detected" },
  { CIOPARERR, "CIOBUS Parity Error" }
};

static unsigned char
generic_sense[] = { REQUEST_SENSE, 0, 0, 0, 255, 0 };

typedef struct {
  scb_queue_type free_scbs;        /*
                                    * SCBs assigned to free slot on
                                    * card (no paging required)
                                    */
  struct aic7xxx_scb   *scb_array[AIC7XXX_MAXSCB];
  struct aic7xxx_hwscb *hscbs;
  unsigned char  numscbs;          /* current number of scbs */
  unsigned char  maxhscbs;         /* hardware scbs */
  unsigned char  maxscbs;          /* max scbs including pageable scbs */
  dma_addr_t	 hscbs_dma;	   /* DMA handle to hscbs */
  unsigned int   hscbs_dma_len;    /* length of the above DMA area */
  void          *hscb_kmalloc_ptr;
} scb_data_type;

struct target_cmd {
  unsigned char mesg_bytes[4];
  unsigned char command[28];
};

#define AHC_TRANS_CUR    0x0001
#define AHC_TRANS_ACTIVE 0x0002
#define AHC_TRANS_GOAL   0x0004
#define AHC_TRANS_USER   0x0008
#define AHC_TRANS_QUITE  0x0010
typedef struct {
  unsigned char width;
  unsigned char period;
  unsigned char offset;
  unsigned char options;
} transinfo_type;

struct aic_dev_data {
  volatile scb_queue_type  delayed_scbs;
  volatile unsigned short  temp_q_depth;
  unsigned short           max_q_depth;
  volatile unsigned char   active_cmds;
  /*
   * Statistics Kept:
   *
   * Total Xfers (count for each command that has a data xfer),
   * broken down by reads && writes.
   *
   * Further sorted into a few bins for keeping tabs on how many commands
   * we get of various sizes.
   *
   */
  long w_total;                          /* total writes */
  long r_total;                          /* total reads */
  long barrier_total;			 /* total num of REQ_BARRIER commands */
  long ordered_total;			 /* How many REQ_BARRIER commands we
					    used ordered tags to satisfy */
  long w_bins[6];                       /* binned write */
  long r_bins[6];                       /* binned reads */
  transinfo_type	cur;
  transinfo_type	goal;
#define  BUS_DEVICE_RESET_PENDING       0x01
#define  DEVICE_RESET_DELAY             0x02
#define  DEVICE_PRINT_DTR               0x04
#define  DEVICE_WAS_BUSY                0x08
#define  DEVICE_DTR_SCANNED		0x10
#define  DEVICE_SCSI_3			0x20
  volatile unsigned char   flags;
  unsigned needppr:1;
  unsigned needppr_copy:1;
  unsigned needsdtr:1;
  unsigned needsdtr_copy:1;
  unsigned needwdtr:1;
  unsigned needwdtr_copy:1;
  unsigned dtr_pending:1;
  struct scsi_device *SDptr;
  struct list_head list;
};

/*
 * Define a structure used for each host adapter.  Note, in order to avoid
 * problems with architectures I can't test on (because I don't have one,
 * such as the Alpha based systems) which happen to give faults for
 * non-aligned memory accesses, care was taken to align this structure
 * in a way that guaranteed all accesses larger than 8 bits were aligned
 * on the appropriate boundary.  It's also organized to try and be more
 * cache line efficient.  Be careful when changing this lest you might hurt
 * overall performance and bring down the wrath of the masses.
 */
struct aic7xxx_host {
  /*
   *  This is the first 64 bytes in the host struct
   */

  /*
   * We are grouping things here....first, items that get either read or
   * written with nearly every interrupt
   */
	volatile long	flags;
	ahc_feature	features;	/* chip features */
	unsigned long	base;		/* card base address */
	volatile unsigned char  __iomem *maddr;	/* memory mapped address */
	unsigned long	isr_count;	/* Interrupt count */
	unsigned long	spurious_int;
	scb_data_type	*scb_data;
	struct aic7xxx_cmd_queue {
		struct scsi_cmnd *head;
		struct scsi_cmnd *tail;
	} completeq;

	/*
	* Things read/written on nearly every entry into aic7xxx_queue()
	*/
	volatile scb_queue_type	waiting_scbs;
	unsigned char	unpause;	/* unpause value for HCNTRL */
	unsigned char	pause;		/* pause value for HCNTRL */
	volatile unsigned char	qoutfifonext;
	volatile unsigned char	activescbs;	/* active scbs */
	volatile unsigned char	max_activescbs;
	volatile unsigned char	qinfifonext;
	volatile unsigned char	*untagged_scbs;
	volatile unsigned char	*qoutfifo;
	volatile unsigned char	*qinfifo;

	unsigned char	dev_last_queue_full[MAX_TARGETS];
	unsigned char	dev_last_queue_full_count[MAX_TARGETS];
	unsigned short	ultraenb; /* Gets downloaded to card as a bitmap */
	unsigned short	discenable; /* Gets downloaded to card as a bitmap */
	transinfo_type	user[MAX_TARGETS];

	unsigned char	msg_buf[13];	/* The message for the target */
	unsigned char	msg_type;
#define MSG_TYPE_NONE              0x00
#define MSG_TYPE_INITIATOR_MSGOUT  0x01
#define MSG_TYPE_INITIATOR_MSGIN   0x02
	unsigned char	msg_len;	/* Length of message */
	unsigned char	msg_index;	/* Index into msg_buf array */


	/*
	 * We put the less frequently used host structure items
	 * after the more frequently used items to try and ease
	 * the burden on the cache subsystem.
	 * These entries are not *commonly* accessed, whereas
	 * the preceding entries are accessed very often.
	 */

	unsigned int	irq;		/* IRQ for this adapter */
	int		instance;	/* aic7xxx instance number */
	int		scsi_id;	/* host adapter SCSI ID */
	int		scsi_id_b;	/* channel B for twin adapters */
	unsigned int	bios_address;
	int		board_name_index;
	unsigned short	bios_control;		/* bios control - SEEPROM */
	unsigned short	adapter_control;	/* adapter control - SEEPROM */
	struct pci_dev	*pdev;
	unsigned char	pci_bus;
	unsigned char	pci_device_fn;
	struct seeprom_config	sc;
	unsigned short	sc_type;
	unsigned short	sc_size;
	struct aic7xxx_host	*next;	/* allow for multiple IRQs */
	struct Scsi_Host	*host;	/* pointer to scsi host */
	struct list_head	 aic_devs; /* all aic_dev structs on host */
	int		host_no;	/* SCSI host number */
	unsigned long	mbase;		/* I/O memory address */
	ahc_chip	chip;		/* chip type */
	ahc_bugs	bugs;
	dma_addr_t	fifo_dma;	/* DMA handle for fifo arrays */
};

/*
 * Valid SCSIRATE values. (p. 3-17)
 * Provides a mapping of transfer periods in ns/4 to the proper value to
 * stick in the SCSIRATE reg to use that transfer rate.
 */
#define AHC_SYNCRATE_ULTRA3 0
#define AHC_SYNCRATE_ULTRA2 1
#define AHC_SYNCRATE_ULTRA  3
#define AHC_SYNCRATE_FAST   6
#define AHC_SYNCRATE_CRC 0x40
#define AHC_SYNCRATE_SE  0x10
static struct aic7xxx_syncrate {
  /* Rates in Ultra mode have bit 8 of sxfr set */
#define                ULTRA_SXFR 0x100
  int sxfr_ultra2;
  int sxfr;
  unsigned char period;
  const char *rate[2];
} aic7xxx_syncrates[] = {
  { 0x42,  0x000,   9,  {"80.0", "160.0"} },
  { 0x13,  0x000,  10,  {"40.0", "80.0"} },
  { 0x14,  0x000,  11,  {"33.0", "66.6"} },
  { 0x15,  0x100,  12,  {"20.0", "40.0"} },
  { 0x16,  0x110,  15,  {"16.0", "32.0"} },
  { 0x17,  0x120,  18,  {"13.4", "26.8"} },
  { 0x18,  0x000,  25,  {"10.0", "20.0"} },
  { 0x19,  0x010,  31,  {"8.0",  "16.0"} },
  { 0x1a,  0x020,  37,  {"6.67", "13.3"} },
  { 0x1b,  0x030,  43,  {"5.7",  "11.4"} },
  { 0x10,  0x040,  50,  {"5.0",  "10.0"} },
  { 0x00,  0x050,  56,  {"4.4",  "8.8" } },
  { 0x00,  0x060,  62,  {"4.0",  "8.0" } },
  { 0x00,  0x070,  68,  {"3.6",  "7.2" } },
  { 0x00,  0x000,  0,   {NULL, NULL}   },
};

#define CTL_OF_SCB(scb) (((scb->hscb)->target_channel_lun >> 3) & 0x1),  \
                        (((scb->hscb)->target_channel_lun >> 4) & 0xf), \
                        ((scb->hscb)->target_channel_lun & 0x07)

#define CTL_OF_CMD(cmd) ((cmd->device->channel) & 0x01),  \
                        ((cmd->device->id) & 0x0f), \
                        ((cmd->device->lun) & 0x07)

#define TARGET_INDEX(cmd)  ((cmd)->device->id | ((cmd)->device->channel << 3))

/*
 * A nice little define to make doing our printks a little easier
 */

#define WARN_LEAD KERN_WARNING "(scsi%d:%d:%d:%d) "
#define INFO_LEAD KERN_INFO "(scsi%d:%d:%d:%d) "

/*
 * XXX - these options apply unilaterally to _all_ 274x/284x/294x
 *       cards in the system.  This should be fixed.  Exceptions to this
 *       rule are noted in the comments.
 */

/*
 * Use this as the default queue depth when setting tagged queueing on.
 */
static unsigned int aic7xxx_default_queue_depth = AIC7XXX_CMDS_PER_DEVICE;

/*
 * Skip the scsi bus reset.  Non 0 make us skip the reset at startup.  This
 * has no effect on any later resets that might occur due to things like
 * SCSI bus timeouts.
 */
static unsigned int aic7xxx_no_reset = 0;
/*
 * Certain PCI motherboards will scan PCI devices from highest to lowest,
 * others scan from lowest to highest, and they tend to do all kinds of
 * strange things when they come into contact with PCI bridge chips.  The
 * net result of all this is that the PCI card that is actually used to boot
 * the machine is very hard to detect.  Most motherboards go from lowest
 * PCI slot number to highest, and the first SCSI controller found is the
 * one you boot from.  The only exceptions to this are when a controller
 * has its BIOS disabled.  So, we by default sort all of our SCSI controllers
 * from lowest PCI slot number to highest PCI slot number.  We also force
 * all controllers with their BIOS disabled to the end of the list.  This
 * works on *almost* all computers.  Where it doesn't work, we have this
 * option.  Setting this option to non-0 will reverse the order of the sort
 * to highest first, then lowest, but will still leave cards with their BIOS
 * disabled at the very end.  That should fix everyone up unless there are
 * really strange cirumstances.
 */
static int aic7xxx_reverse_scan = 0;
/*
 * Should we force EXTENDED translation on a controller.
 *     0 == Use whatever is in the SEEPROM or default to off
 *     1 == Use whatever is in the SEEPROM or default to on
 */
static unsigned int aic7xxx_extended = 0;
/*
 * The IRQ trigger method used on EISA controllers. Does not effect PCI cards.
 *   -1 = Use detected settings.
 *    0 = Force Edge triggered mode.
 *    1 = Force Level triggered mode.
 */
static int aic7xxx_irq_trigger = -1;
/*
 * This variable is used to override the termination settings on a controller.
 * This should not be used under normal conditions.  However, in the case
 * that a controller does not have a readable SEEPROM (so that we can't
 * read the SEEPROM settings directly) and that a controller has a buggered
 * version of the cable detection logic, this can be used to force the 
 * correct termination.  It is preferable to use the manual termination
 * settings in the BIOS if possible, but some motherboard controllers store
 * those settings in a format we can't read.  In other cases, auto term
 * should also work, but the chipset was put together with no auto term
 * logic (common on motherboard controllers).  In those cases, we have
 * 32 bits here to work with.  That's good for 8 controllers/channels.  The
 * bits are organized as 4 bits per channel, with scsi0 getting the lowest
 * 4 bits in the int.  A 1 in a bit position indicates the termination setting
 * that corresponds to that bit should be enabled, a 0 is disabled.
 * It looks something like this:
 *
 *    0x0f =  1111-Single Ended Low Byte Termination on/off
 *            ||\-Single Ended High Byte Termination on/off
 *            |\-LVD Low Byte Termination on/off
 *            \-LVD High Byte Termination on/off
 *
 * For non-Ultra2 controllers, the upper 2 bits are not important.  So, to
 * enable both high byte and low byte termination on scsi0, I would need to
 * make sure that the override_term variable was set to 0x03 (bits 0011).
 * To make sure that all termination is enabled on an Ultra2 controller at
 * scsi2 and only high byte termination on scsi1 and high and low byte
 * termination on scsi0, I would set override_term=0xf23 (bits 1111 0010 0011)
 *
 * For the most part, users should never have to use this, that's why I
 * left it fairly cryptic instead of easy to understand.  If you need it,
 * most likely someone will be telling you what your's needs to be set to.
 */
static int aic7xxx_override_term = -1;
/*
 * Certain motherboard chipset controllers tend to screw
 * up the polarity of the term enable output pin.  Use this variable
 * to force the correct polarity for your system.  This is a bitfield variable
 * similar to the previous one, but this one has one bit per channel instead
 * of four.
 *    0 = Force the setting to active low.
 *    1 = Force setting to active high.
 * Most Adaptec cards are active high, several motherboards are active low.
 * To force a 2940 card at SCSI 0 to active high and a motherboard 7895
 * controller at scsi1 and scsi2 to active low, and a 2910 card at scsi3
 * to active high, you would need to set stpwlev=0x9 (bits 1001).
 *
 * People shouldn't need to use this, but if you are experiencing lots of
 * SCSI timeout problems, this may help.  There is one sure way to test what
 * this option needs to be.  Using a boot floppy to boot the system, configure
 * your system to enable all SCSI termination (in the Adaptec SCSI BIOS) and
 * if needed then also pass a value to override_term to make sure that the
 * driver is enabling SCSI termination, then set this variable to either 0
 * or 1.  When the driver boots, make sure there are *NO* SCSI cables
 * connected to your controller.  If it finds and inits the controller
 * without problem, then the setting you passed to stpwlev was correct.  If
 * the driver goes into a reset loop and hangs the system, then you need the
 * other setting for this variable.  If neither setting lets the machine
 * boot then you have definite termination problems that may not be fixable.
 */
static int aic7xxx_stpwlev = -1;
/*
 * Set this to non-0 in order to force the driver to panic the kernel
 * and print out debugging info on a SCSI abort or reset cycle.
 */
static int aic7xxx_panic_on_abort = 0;
/*
 * PCI bus parity checking of the Adaptec controllers.  This is somewhat
 * dubious at best.  To my knowledge, this option has never actually
 * solved a PCI parity problem, but on certain machines with broken PCI
 * chipset configurations, it can generate tons of false error messages.
 * It's included in the driver for completeness.
 *   0 = Shut off PCI parity check
 *  -1 = Normal polarity pci parity checking
 *   1 = reverse polarity pci parity checking
 *
 * NOTE: you can't actually pass -1 on the lilo prompt.  So, to set this
 * variable to -1 you would actually want to simply pass the variable
 * name without a number.  That will invert the 0 which will result in
 * -1.
 */
static int aic7xxx_pci_parity = 0;
/*
 * Set this to any non-0 value to cause us to dump the contents of all
 * the card's registers in a hex dump format tailored to each model of
 * controller.
 * 
 * NOTE: THE CONTROLLER IS LEFT IN AN UNUSABLE STATE BY THIS OPTION.
 *       YOU CANNOT BOOT UP WITH THIS OPTION, IT IS FOR DEBUGGING PURPOSES
 *       ONLY
 */
static int aic7xxx_dump_card = 0;
/*
 * Set this to a non-0 value to make us dump out the 32 bit instruction
 * registers on the card after completing the sequencer download.  This
 * allows the actual sequencer download to be verified.  It is possible
 * to use this option and still boot up and run your system.  This is
 * only intended for debugging purposes.
 */
static int aic7xxx_dump_sequencer = 0;
/*
 * Certain newer motherboards have put new PCI based devices into the
 * IO spaces that used to typically be occupied by VLB or EISA cards.
 * This overlap can cause these newer motherboards to lock up when scanned
 * for older EISA and VLB devices.  Setting this option to non-0 will
 * cause the driver to skip scanning for any VLB or EISA controllers and
 * only support the PCI controllers.  NOTE: this means that if the kernel
 * os compiled with PCI support disabled, then setting this to non-0
 * would result in never finding any devices :)
 */
static int aic7xxx_no_probe = 0;
/*
 * On some machines, enabling the external SCB RAM isn't reliable yet.  I
 * haven't had time to make test patches for things like changing the
 * timing mode on that external RAM either.  Some of those changes may
 * fix the problem.  Until then though, we default to external SCB RAM
 * off and give a command line option to enable it.
 */
static int aic7xxx_scbram = 0;
/*
 * So that we can set how long each device is given as a selection timeout.
 * The table of values goes like this:
 *   0 - 256ms
 *   1 - 128ms
 *   2 - 64ms
 *   3 - 32ms
 * We default to 64ms because it's fast.  Some old SCSI-I devices need a
 * longer time.  The final value has to be left shifted by 3, hence 0x10
 * is the final value.
 */
static int aic7xxx_seltime = 0x10;
/*
 * So that insmod can find the variable and make it point to something
 */
#ifdef MODULE
static char * aic7xxx = NULL;
module_param(aic7xxx, charp, 0);
#endif

#define VERBOSE_NORMAL         0x0000
#define VERBOSE_NEGOTIATION    0x0001
#define VERBOSE_SEQINT         0x0002
#define VERBOSE_SCSIINT        0x0004
#define VERBOSE_PROBE          0x0008
#define VERBOSE_PROBE2         0x0010
#define VERBOSE_NEGOTIATION2   0x0020
#define VERBOSE_MINOR_ERROR    0x0040
#define VERBOSE_TRACING        0x0080
#define VERBOSE_ABORT          0x0f00
#define VERBOSE_ABORT_MID      0x0100
#define VERBOSE_ABORT_FIND     0x0200
#define VERBOSE_ABORT_PROCESS  0x0400
#define VERBOSE_ABORT_RETURN   0x0800
#define VERBOSE_RESET          0xf000
#define VERBOSE_RESET_MID      0x1000
#define VERBOSE_RESET_FIND     0x2000
#define VERBOSE_RESET_PROCESS  0x4000
#define VERBOSE_RESET_RETURN   0x8000
static int aic7xxx_verbose = VERBOSE_NORMAL | VERBOSE_NEGOTIATION |
           VERBOSE_PROBE;                     /* verbose messages */


/****************************************************************************
 *
 * We're going to start putting in function declarations so that order of
 * functions is no longer important.  As needed, they are added here.
 *
 ***************************************************************************/

static int aic7xxx_release(struct Scsi_Host *host);
static void aic7xxx_set_syncrate(struct aic7xxx_host *p, 
		struct aic7xxx_syncrate *syncrate, int target, int channel,
		unsigned int period, unsigned int offset, unsigned char options,
		unsigned int type, struct aic_dev_data *aic_dev);
static void aic7xxx_set_width(struct aic7xxx_host *p, int target, int channel,
		int lun, unsigned int width, unsigned int type,
		struct aic_dev_data *aic_dev);
static void aic7xxx_panic_abort(struct aic7xxx_host *p, struct scsi_cmnd *cmd);
static void aic7xxx_print_card(struct aic7xxx_host *p);
static void aic7xxx_print_scratch_ram(struct aic7xxx_host *p);
static void aic7xxx_print_sequencer(struct aic7xxx_host *p, int downloaded);
#ifdef AIC7XXX_VERBOSE_DEBUGGING
static void aic7xxx_check_scbs(struct aic7xxx_host *p, char *buffer);
#endif

/****************************************************************************
 *
 * These functions are now used.  They happen to be wrapped in useless
 * inb/outb port read/writes around the real reads and writes because it
 * seems that certain very fast CPUs have a problem dealing with us when
 * going at full speed.
 *
 ***************************************************************************/

static unsigned char
aic_inb(struct aic7xxx_host *p, long port)
{
#ifdef MMAPIO
  unsigned char x;
  if(p->maddr)
  {
    x = readb(p->maddr + port);
  }
  else
  {
    x = inb(p->base + port);
  }
  return(x);
#else
  return(inb(p->base + port));
#endif
}

static void
aic_outb(struct aic7xxx_host *p, unsigned char val, long port)
{
#ifdef MMAPIO
  if(p->maddr)
  {
    writeb(val, p->maddr + port);
    mb(); /* locked operation in order to force CPU ordering */
    readb(p->maddr + HCNTRL); /* dummy read to flush the PCI write */
  }
  else
  {
    outb(val, p->base + port);
    mb(); /* locked operation in order to force CPU ordering */
  }
#else
  outb(val, p->base + port);
  mb(); /* locked operation in order to force CPU ordering */
#endif
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_setup
 *
 * Description:
 *   Handle Linux boot parameters. This routine allows for assigning a value
 *   to a parameter with a ':' between the parameter and the value.
 *   ie. aic7xxx=unpause:0x0A,extended
 *-F*************************************************************************/
static int
aic7xxx_setup(char *s)
{
  int   i, n;
  char *p;
  char *end;

  static struct {
    const char *name;
    unsigned int *flag;
  } options[] = {
    { "extended",    &aic7xxx_extended },
    { "no_reset",    &aic7xxx_no_reset },
    { "irq_trigger", &aic7xxx_irq_trigger },
    { "verbose",     &aic7xxx_verbose },
    { "reverse_scan",&aic7xxx_reverse_scan },
    { "override_term", &aic7xxx_override_term },
    { "stpwlev", &aic7xxx_stpwlev },
    { "no_probe", &aic7xxx_no_probe },
    { "panic_on_abort", &aic7xxx_panic_on_abort },
    { "pci_parity", &aic7xxx_pci_parity },
    { "dump_card", &aic7xxx_dump_card },
    { "dump_sequencer", &aic7xxx_dump_sequencer },
    { "default_queue_depth", &aic7xxx_default_queue_depth },
    { "scbram", &aic7xxx_scbram },
    { "seltime", &aic7xxx_seltime },
    { "tag_info",    NULL }
  };

  end = strchr(s, '\0');

  while ((p = strsep(&s, ",.")) != NULL)
  {
    for (i = 0; i < ARRAY_SIZE(options); i++)
    {
      n = strlen(options[i].name);
      if (!strncmp(options[i].name, p, n))
      {
        if (!strncmp(p, "tag_info", n))
        {
          if (p[n] == ':')
          {
            char *base;
            char *tok, *tok_end, *tok_end2;
            char tok_list[] = { '.', ',', '{', '}', '\0' };
            int i, instance = -1, device = -1;
            unsigned char done = FALSE;

            base = p;
            tok = base + n + 1;  /* Forward us just past the ':' */
            tok_end = strchr(tok, '\0');
            if (tok_end < end)
              *tok_end = ',';
            while(!done)
            {
              switch(*tok)
              {
                case '{':
                  if (instance == -1)
                    instance = 0;
                  else if (device == -1)
                    device = 0;
                  tok++;
                  break;
                case '}':
                  if (device != -1)
                    device = -1;
                  else if (instance != -1)
                    instance = -1;
                  tok++;
                  break;
                case ',':
                case '.':
                  if (instance == -1)
                    done = TRUE;
                  else if (device >= 0)
                    device++;
                  else if (instance >= 0)
                    instance++;
                  if ( (device >= MAX_TARGETS) || 
                       (instance >= ARRAY_SIZE(aic7xxx_tag_info)) )
                    done = TRUE;
                  tok++;
                  if (!done)
                  {
                    base = tok;
                  }
                  break;
                case '\0':
                  done = TRUE;
                  break;
                default:
                  done = TRUE;
                  tok_end = strchr(tok, '\0');
                  for(i=0; tok_list[i]; i++)
                  {
                    tok_end2 = strchr(tok, tok_list[i]);
                    if ( (tok_end2) && (tok_end2 < tok_end) )
                    {
                      tok_end = tok_end2;
                      done = FALSE;
                    }
                  }
                  if ( (instance >= 0) && (device >= 0) &&
                       (instance < ARRAY_SIZE(aic7xxx_tag_info)) &&
                       (device < MAX_TARGETS) )
                    aic7xxx_tag_info[instance].tag_commands[device] =
                      simple_strtoul(tok, NULL, 0) & 0xff;
                  tok = tok_end;
                  break;
              }
            }
            while((p != base) && (p != NULL))
              p = strsep(&s, ",.");
          }
        }
        else if (p[n] == ':')
        {
          *(options[i].flag) = simple_strtoul(p + n + 1, NULL, 0);
          if(!strncmp(p, "seltime", n))
          {
            *(options[i].flag) = (*(options[i].flag) % 4) << 3;
          }
        }
        else if (!strncmp(p, "verbose", n))
        {
          *(options[i].flag) = 0xff29;
        }
        else
        {
          *(options[i].flag) = ~(*(options[i].flag));
          if(!strncmp(p, "seltime", n))
          {
            *(options[i].flag) = (*(options[i].flag) % 4) << 3;
          }
        }
      }
    }
  }
  return 1;
}

__setup("aic7xxx=", aic7xxx_setup);

/*+F*************************************************************************
 * Function:
 *   pause_sequencer
 *
 * Description:
 *   Pause the sequencer and wait for it to actually stop - this
 *   is important since the sequencer can disable pausing for critical
 *   sections.
 *-F*************************************************************************/
static void
pause_sequencer(struct aic7xxx_host *p)
{
  aic_outb(p, p->pause, HCNTRL);
  while ((aic_inb(p, HCNTRL) & PAUSE) == 0)
  {
    ;
  }
  if(p->features & AHC_ULTRA2)
  {
    aic_inb(p, CCSCBCTL);
  }
}

/*+F*************************************************************************
 * Function:
 *   unpause_sequencer
 *
 * Description:
 *   Unpause the sequencer. Unremarkable, yet done often enough to
 *   warrant an easy way to do it.
 *-F*************************************************************************/
static void
unpause_sequencer(struct aic7xxx_host *p, int unpause_always)
{
  if (unpause_always ||
      ( !(aic_inb(p, INTSTAT) & (SCSIINT | SEQINT | BRKADRINT)) &&
        !(p->flags & AHC_HANDLING_REQINITS) ) )
  {
    aic_outb(p, p->unpause, HCNTRL);
  }
}

/*+F*************************************************************************
 * Function:
 *   restart_sequencer
 *
 * Description:
 *   Restart the sequencer program from address zero.  This assumes
 *   that the sequencer is already paused.
 *-F*************************************************************************/
static void
restart_sequencer(struct aic7xxx_host *p)
{
  aic_outb(p, 0, SEQADDR0);
  aic_outb(p, 0, SEQADDR1);
  aic_outb(p, FASTMODE, SEQCTL);
}

/*
 * We include the aic7xxx_seq.c file here so that the other defines have
 * already been made, and so that it comes before the code that actually
 * downloads the instructions (since we don't typically use function
 * prototype, our code has to be ordered that way, it's a left-over from
 * the original driver days.....I should fix it some time DL).
 */
#include "aic7xxx_old/aic7xxx_seq.c"

/*+F*************************************************************************
 * Function:
 *   aic7xxx_check_patch
 *
 * Description:
 *   See if the next patch to download should be downloaded.
 *-F*************************************************************************/
static int
aic7xxx_check_patch(struct aic7xxx_host *p,
  struct sequencer_patch **start_patch, int start_instr, int *skip_addr)
{
  struct sequencer_patch *cur_patch;
  struct sequencer_patch *last_patch;
  int num_patches;

  num_patches = ARRAY_SIZE(sequencer_patches);
  last_patch = &sequencer_patches[num_patches];
  cur_patch = *start_patch;

  while ((cur_patch < last_patch) && (start_instr == cur_patch->begin))
  {
    if (cur_patch->patch_func(p) == 0)
    {
      /*
       * Start rejecting code.
       */
      *skip_addr = start_instr + cur_patch->skip_instr;
      cur_patch += cur_patch->skip_patch;
    }
    else
    {
      /*
       * Found an OK patch.  Advance the patch pointer to the next patch
       * and wait for our instruction pointer to get here.
       */
      cur_patch++;
    }
  }

  *start_patch = cur_patch;
  if (start_instr < *skip_addr)
    /*
     * Still skipping
     */
    return (0);
  return(1);
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_download_instr
 *
 * Description:
 *   Find the next patch to download.
 *-F*************************************************************************/
static void
aic7xxx_download_instr(struct aic7xxx_host *p, int instrptr,
  unsigned char *dconsts)
{
  union ins_formats instr;
  struct ins_format1 *fmt1_ins;
  struct ins_format3 *fmt3_ins;
  unsigned char opcode;

  instr = *(union ins_formats*) &seqprog[instrptr * 4];

  instr.integer = le32_to_cpu(instr.integer);
  
  fmt1_ins = &instr.format1;
  fmt3_ins = NULL;

  /* Pull the opcode */
  opcode = instr.format1.opcode;
  switch (opcode)
  {
    case AIC_OP_JMP:
    case AIC_OP_JC:
    case AIC_OP_JNC:
    case AIC_OP_CALL:
    case AIC_OP_JNE:
    case AIC_OP_JNZ:
    case AIC_OP_JE:
    case AIC_OP_JZ:
    {
      struct sequencer_patch *cur_patch;
      int address_offset;
      unsigned int address;
      int skip_addr;
      int i;

      fmt3_ins = &instr.format3;
      address_offset = 0;
      address = fmt3_ins->address;
      cur_patch = sequencer_patches;
      skip_addr = 0;

      for (i = 0; i < address;)
      {
        aic7xxx_check_patch(p, &cur_patch, i, &skip_addr);
        if (skip_addr > i)
        {
          int end_addr;

          end_addr = min_t(int, address, skip_addr);
          address_offset += end_addr - i;
          i = skip_addr;
        }
        else
        {
          i++;
        }
      }
      address -= address_offset;
      fmt3_ins->address = address;
      /* Fall Through to the next code section */
    }
    case AIC_OP_OR:
    case AIC_OP_AND:
    case AIC_OP_XOR:
    case AIC_OP_ADD:
    case AIC_OP_ADC:
    case AIC_OP_BMOV:
      if (fmt1_ins->parity != 0)
      {
        fmt1_ins->immediate = dconsts[fmt1_ins->immediate];
      }
      fmt1_ins->parity = 0;
      /* Fall Through to the next code section */
    case AIC_OP_ROL:
      if ((p->features & AHC_ULTRA2) != 0)
      {
        int i, count;

        /* Calculate odd parity for the instruction */
        for ( i=0, count=0; i < 31; i++)
        {
          unsigned int mask;

          mask = 0x01 << i;
          if ((instr.integer & mask) != 0)
            count++;
        }
        if (!(count & 0x01))
          instr.format1.parity = 1;
      }
      else
      {
        if (fmt3_ins != NULL)
        {
          instr.integer =  fmt3_ins->immediate |
                          (fmt3_ins->source << 8) |
                          (fmt3_ins->address << 16) |
                          (fmt3_ins->opcode << 25);
        }
        else
        {
          instr.integer =  fmt1_ins->immediate |
                          (fmt1_ins->source << 8) |
                          (fmt1_ins->destination << 16) |
                          (fmt1_ins->ret << 24) |
                          (fmt1_ins->opcode << 25);
        }
      }
      aic_outb(p, (instr.integer & 0xff), SEQRAM);
      aic_outb(p, ((instr.integer >> 8) & 0xff), SEQRAM);
      aic_outb(p, ((instr.integer >> 16) & 0xff), SEQRAM);
      aic_outb(p, ((instr.integer >> 24) & 0xff), SEQRAM);
      udelay(10);
      break;

    default:
      panic("aic7xxx: Unknown opcode encountered in sequencer program.");
      break;
  }
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_loadseq
 *
 * Description:
 *   Load the sequencer code into the controller memory.
 *-F*************************************************************************/
static void
aic7xxx_loadseq(struct aic7xxx_host *p)
{
  struct sequencer_patch *cur_patch;
  int i;
  int downloaded;
  int skip_addr;
  unsigned char download_consts[4] = {0, 0, 0, 0};

  if (aic7xxx_verbose & VERBOSE_PROBE)
  {
    printk(KERN_INFO "(scsi%d) Downloading sequencer code...", p->host_no);
  }
#if 0
  download_consts[TMODE_NUMCMDS] = p->num_targetcmds;
#endif
  download_consts[TMODE_NUMCMDS] = 0;
  cur_patch = &sequencer_patches[0];
  downloaded = 0;
  skip_addr = 0;

  aic_outb(p, PERRORDIS|LOADRAM|FAILDIS|FASTMODE, SEQCTL);
  aic_outb(p, 0, SEQADDR0);
  aic_outb(p, 0, SEQADDR1);

  for (i = 0; i < sizeof(seqprog) / 4;  i++)
  {
    if (aic7xxx_check_patch(p, &cur_patch, i, &skip_addr) == 0)
    {
      /* Skip this instruction for this configuration. */
      continue;
    }
    aic7xxx_download_instr(p, i, &download_consts[0]);
    downloaded++;
  }

  aic_outb(p, 0, SEQADDR0);
  aic_outb(p, 0, SEQADDR1);
  aic_outb(p, FASTMODE | FAILDIS, SEQCTL);
  unpause_sequencer(p, TRUE);
  mdelay(1);
  pause_sequencer(p);
  aic_outb(p, FASTMODE, SEQCTL);
  if (aic7xxx_verbose & VERBOSE_PROBE)
  {
    printk(" %d instructions downloaded\n", downloaded);
  }
  if (aic7xxx_dump_sequencer)
    aic7xxx_print_sequencer(p, downloaded);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_print_sequencer
 *
 * Description:
 *   Print the contents of the sequencer memory to the screen.
 *-F*************************************************************************/
static void
aic7xxx_print_sequencer(struct aic7xxx_host *p, int downloaded)
{
  int i, k, temp;
  
  aic_outb(p, PERRORDIS|LOADRAM|FAILDIS|FASTMODE, SEQCTL);
  aic_outb(p, 0, SEQADDR0);
  aic_outb(p, 0, SEQADDR1);

  k = 0;
  for (i=0; i < downloaded; i++)
  {
    if ( k == 0 )
      printk("%03x: ", i);
    temp = aic_inb(p, SEQRAM);
    temp |= (aic_inb(p, SEQRAM) << 8);
    temp |= (aic_inb(p, SEQRAM) << 16);
    temp |= (aic_inb(p, SEQRAM) << 24);
    printk("%08x", temp);
    if ( ++k == 8 )
    {
      printk("\n");
      k = 0;
    }
    else
      printk(" ");
  }
  aic_outb(p, 0, SEQADDR0);
  aic_outb(p, 0, SEQADDR1);
  aic_outb(p, FASTMODE | FAILDIS, SEQCTL);
  unpause_sequencer(p, TRUE);
  mdelay(1);
  pause_sequencer(p);
  aic_outb(p, FASTMODE, SEQCTL);
  printk("\n");
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_info
 *
 * Description:
 *   Return a string describing the driver.
 *-F*************************************************************************/
static const char *
aic7xxx_info(struct Scsi_Host *dooh)
{
  static char buffer[256];
  char *bp;
  struct aic7xxx_host *p;

  bp = &buffer[0];
  p = (struct aic7xxx_host *)dooh->hostdata;
  memset(bp, 0, sizeof(buffer));
  strcpy(bp, "Adaptec AHA274x/284x/294x (EISA/VLB/PCI-Fast SCSI) ");
  strcat(bp, AIC7XXX_C_VERSION);
  strcat(bp, "/");
  strcat(bp, AIC7XXX_H_VERSION);
  strcat(bp, "\n");
  strcat(bp, "       <");
  strcat(bp, board_names[p->board_name_index]);
  strcat(bp, ">");

  return(bp);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_find_syncrate
 *
 * Description:
 *   Look up the valid period to SCSIRATE conversion in our table
 *-F*************************************************************************/
static struct aic7xxx_syncrate *
aic7xxx_find_syncrate(struct aic7xxx_host *p, unsigned int *period,
  unsigned int maxsync, unsigned char *options)
{
  struct aic7xxx_syncrate *syncrate;
  int done = FALSE;

  switch(*options)
  {
    case MSG_EXT_PPR_OPTION_DT_CRC:
    case MSG_EXT_PPR_OPTION_DT_UNITS:
      if(!(p->features & AHC_ULTRA3))
      {
        *options = 0;
        maxsync = max_t(unsigned int, maxsync, AHC_SYNCRATE_ULTRA2);
      }
      break;
    case MSG_EXT_PPR_OPTION_DT_CRC_QUICK:
    case MSG_EXT_PPR_OPTION_DT_UNITS_QUICK:
      if(!(p->features & AHC_ULTRA3))
      {
        *options = 0;
        maxsync = max_t(unsigned int, maxsync, AHC_SYNCRATE_ULTRA2);
      }
      else
      {
        /*
         * we don't support the Quick Arbitration variants of dual edge
         * clocking.  As it turns out, we want to send back the
         * same basic option, but without the QA attribute.
         * We know that we are responding because we would never set
         * these options ourself, we would only respond to them.
         */
        switch(*options)
        {
          case MSG_EXT_PPR_OPTION_DT_CRC_QUICK:
            *options = MSG_EXT_PPR_OPTION_DT_CRC;
            break;
          case MSG_EXT_PPR_OPTION_DT_UNITS_QUICK:
            *options = MSG_EXT_PPR_OPTION_DT_UNITS;
            break;
        }
      }
      break;
    default:
      *options = 0;
      maxsync = max_t(unsigned int, maxsync, AHC_SYNCRATE_ULTRA2);
      break;
  }
  syncrate = &aic7xxx_syncrates[maxsync];
  while ( (syncrate->rate[0] != NULL) &&
         (!(p->features & AHC_ULTRA2) || syncrate->sxfr_ultra2) )
  {
    if (*period <= syncrate->period) 
    {
      switch(*options)
      {
        case MSG_EXT_PPR_OPTION_DT_CRC:
        case MSG_EXT_PPR_OPTION_DT_UNITS:
          if(!(syncrate->sxfr_ultra2 & AHC_SYNCRATE_CRC))
          {
            done = TRUE;
            /*
             * oops, we went too low for the CRC/DualEdge signalling, so
             * clear the options byte
             */
            *options = 0;
            /*
             * We'll be sending a reply to this packet to set the options
             * properly, so unilaterally set the period as well.
             */
            *period = syncrate->period;
          }
          else
          {
            done = TRUE;
            if(syncrate == &aic7xxx_syncrates[maxsync])
            {
              *period = syncrate->period;
            }
          }
          break;
        default:
          if(!(syncrate->sxfr_ultra2 & AHC_SYNCRATE_CRC))
          {
            done = TRUE;
            if(syncrate == &aic7xxx_syncrates[maxsync])
            {
              *period = syncrate->period;
            }
          }
          break;
      }
      if(done)
      {
        break;
      }
    }
    syncrate++;
  }
  if ( (*period == 0) || (syncrate->rate[0] == NULL) ||
       ((p->features & AHC_ULTRA2) && (syncrate->sxfr_ultra2 == 0)) )
  {
    /*
     * Use async transfers for this target
     */
    *options = 0;
    *period = 255;
    syncrate = NULL;
  }
  return (syncrate);
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_find_period
 *
 * Description:
 *   Look up the valid SCSIRATE to period conversion in our table
 *-F*************************************************************************/
static unsigned int
aic7xxx_find_period(struct aic7xxx_host *p, unsigned int scsirate,
  unsigned int maxsync)
{
  struct aic7xxx_syncrate *syncrate;

  if (p->features & AHC_ULTRA2)
  {
    scsirate &= SXFR_ULTRA2;
  }
  else
  {
    scsirate &= SXFR;
  }

  syncrate = &aic7xxx_syncrates[maxsync];
  while (syncrate->rate[0] != NULL)
  {
    if (p->features & AHC_ULTRA2)
    {
      if (syncrate->sxfr_ultra2 == 0)
        break;
      else if (scsirate == syncrate->sxfr_ultra2)
        return (syncrate->period);
      else if (scsirate == (syncrate->sxfr_ultra2 & ~AHC_SYNCRATE_CRC))
        return (syncrate->period);
    }
    else if (scsirate == (syncrate->sxfr & ~ULTRA_SXFR))
    {
      return (syncrate->period);
    }
    syncrate++;
  }
  return (0); /* async */
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_validate_offset
 *
 * Description:
 *   Set a valid offset value for a particular card in use and transfer
 *   settings in use.
 *-F*************************************************************************/
static void
aic7xxx_validate_offset(struct aic7xxx_host *p,
  struct aic7xxx_syncrate *syncrate, unsigned int *offset, int wide)
{
  unsigned int maxoffset;

  /* Limit offset to what the card (and device) can do */
  if (syncrate == NULL)
  {
    maxoffset = 0;
  }
  else if (p->features & AHC_ULTRA2)
  {
    maxoffset = MAX_OFFSET_ULTRA2;
  }
  else
  {
    if (wide)
      maxoffset = MAX_OFFSET_16BIT;
    else
      maxoffset = MAX_OFFSET_8BIT;
  }
  *offset = min(*offset, maxoffset);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_set_syncrate
 *
 * Description:
 *   Set the actual syncrate down in the card and in our host structs
 *-F*************************************************************************/
static void
aic7xxx_set_syncrate(struct aic7xxx_host *p, struct aic7xxx_syncrate *syncrate,
    int target, int channel, unsigned int period, unsigned int offset,
    unsigned char options, unsigned int type, struct aic_dev_data *aic_dev)
{
  unsigned char tindex;
  unsigned short target_mask;
  unsigned char lun, old_options;
  unsigned int old_period, old_offset;

  tindex = target | (channel << 3);
  target_mask = 0x01 << tindex;
  lun = aic_inb(p, SCB_TCL) & 0x07;

  if (syncrate == NULL)
  {
    period = 0;
    offset = 0;
  }

  old_period = aic_dev->cur.period;
  old_offset = aic_dev->cur.offset;
  old_options = aic_dev->cur.options;

  
  if (type & AHC_TRANS_CUR)
  {
    unsigned int scsirate;

    scsirate = aic_inb(p, TARG_SCSIRATE + tindex);
    if (p->features & AHC_ULTRA2)
    {
      scsirate &= ~SXFR_ULTRA2;
      if (syncrate != NULL)
      {
        switch(options)
        {
          case MSG_EXT_PPR_OPTION_DT_UNITS:
            /*
             * mask off the CRC bit in the xfer settings
             */
            scsirate |= (syncrate->sxfr_ultra2 & ~AHC_SYNCRATE_CRC);
            break;
          default:
            scsirate |= syncrate->sxfr_ultra2;
            break;
        }
      }
      if (type & AHC_TRANS_ACTIVE)
      {
        aic_outb(p, offset, SCSIOFFSET);
      }
      aic_outb(p, offset, TARG_OFFSET + tindex);
    }
    else /* Not an Ultra2 controller */
    {
      scsirate &= ~(SXFR|SOFS);
      p->ultraenb &= ~target_mask;
      if (syncrate != NULL)
      {
        if (syncrate->sxfr & ULTRA_SXFR)
        {
          p->ultraenb |= target_mask;
        }
        scsirate |= (syncrate->sxfr & SXFR);
        scsirate |= (offset & SOFS);
      }
      if (type & AHC_TRANS_ACTIVE)
      {
        unsigned char sxfrctl0;

        sxfrctl0 = aic_inb(p, SXFRCTL0);
        sxfrctl0 &= ~FAST20;
        if (p->ultraenb & target_mask)
          sxfrctl0 |= FAST20;
        aic_outb(p, sxfrctl0, SXFRCTL0);
      }
      aic_outb(p, p->ultraenb & 0xff, ULTRA_ENB);
      aic_outb(p, (p->ultraenb >> 8) & 0xff, ULTRA_ENB + 1 );
    }
    if (type & AHC_TRANS_ACTIVE)
    {
      aic_outb(p, scsirate, SCSIRATE);
    }
    aic_outb(p, scsirate, TARG_SCSIRATE + tindex);
    aic_dev->cur.period = period;
    aic_dev->cur.offset = offset;
    aic_dev->cur.options = options;
    if ( !(type & AHC_TRANS_QUITE) &&
         (aic7xxx_verbose & VERBOSE_NEGOTIATION) &&
         (aic_dev->flags & DEVICE_PRINT_DTR) )
    {
      if (offset)
      {
        int rate_mod = (scsirate & WIDEXFER) ? 1 : 0;
      
        printk(INFO_LEAD "Synchronous at %s Mbyte/sec, "
               "offset %d.\n", p->host_no, channel, target, lun,
               syncrate->rate[rate_mod], offset);
      }
      else
      {
        printk(INFO_LEAD "Using asynchronous transfers.\n",
               p->host_no, channel, target, lun);
      }
      aic_dev->flags &= ~DEVICE_PRINT_DTR;
    }
  }

  if (type & AHC_TRANS_GOAL)
  {
    aic_dev->goal.period = period;
    aic_dev->goal.offset = offset;
    aic_dev->goal.options = options;
  }

  if (type & AHC_TRANS_USER)
  {
    p->user[tindex].period = period;
    p->user[tindex].offset = offset;
    p->user[tindex].options = options;
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_set_width
 *
 * Description:
 *   Set the actual width down in the card and in our host structs
 *-F*************************************************************************/
static void
aic7xxx_set_width(struct aic7xxx_host *p, int target, int channel, int lun,
    unsigned int width, unsigned int type, struct aic_dev_data *aic_dev)
{
  unsigned char tindex;
  unsigned short target_mask;
  unsigned int old_width;

  tindex = target | (channel << 3);
  target_mask = 1 << tindex;
  
  old_width = aic_dev->cur.width;

  if (type & AHC_TRANS_CUR) 
  {
    unsigned char scsirate;

    scsirate = aic_inb(p, TARG_SCSIRATE + tindex);

    scsirate &= ~WIDEXFER;
    if (width == MSG_EXT_WDTR_BUS_16_BIT)
      scsirate |= WIDEXFER;

    aic_outb(p, scsirate, TARG_SCSIRATE + tindex);

    if (type & AHC_TRANS_ACTIVE)
      aic_outb(p, scsirate, SCSIRATE);

    aic_dev->cur.width = width;

    if ( !(type & AHC_TRANS_QUITE) &&
          (aic7xxx_verbose & VERBOSE_NEGOTIATION2) && 
          (aic_dev->flags & DEVICE_PRINT_DTR) )
    {
      printk(INFO_LEAD "Using %s transfers\n", p->host_no, channel, target,
        lun, (scsirate & WIDEXFER) ? "Wide(16bit)" : "Narrow(8bit)" );
    }
  }

  if (type & AHC_TRANS_GOAL)
    aic_dev->goal.width = width;
  if (type & AHC_TRANS_USER)
    p->user[tindex].width = width;

  if (aic_dev->goal.offset)
  {
    if (p->features & AHC_ULTRA2)
    {
      aic_dev->goal.offset = MAX_OFFSET_ULTRA2;
    }
    else if (width == MSG_EXT_WDTR_BUS_16_BIT)
    {
      aic_dev->goal.offset = MAX_OFFSET_16BIT;
    }
    else
    {
      aic_dev->goal.offset = MAX_OFFSET_8BIT;
    }
  }
}
      
/*+F*************************************************************************
 * Function:
 *   scbq_init
 *
 * Description:
 *   SCB queue initialization.
 *
 *-F*************************************************************************/
static void
scbq_init(volatile scb_queue_type *queue)
{
  queue->head = NULL;
  queue->tail = NULL;
}

/*+F*************************************************************************
 * Function:
 *   scbq_insert_head
 *
 * Description:
 *   Add an SCB to the head of the list.
 *
 *-F*************************************************************************/
static inline void
scbq_insert_head(volatile scb_queue_type *queue, struct aic7xxx_scb *scb)
{
  scb->q_next = queue->head;
  queue->head = scb;
  if (queue->tail == NULL)       /* If list was empty, update tail. */
    queue->tail = queue->head;
}

/*+F*************************************************************************
 * Function:
 *   scbq_remove_head
 *
 * Description:
 *   Remove an SCB from the head of the list.
 *
 *-F*************************************************************************/
static inline struct aic7xxx_scb *
scbq_remove_head(volatile scb_queue_type *queue)
{
  struct aic7xxx_scb * scbp;

  scbp = queue->head;
  if (queue->head != NULL)
    queue->head = queue->head->q_next;
  if (queue->head == NULL)       /* If list is now empty, update tail. */
    queue->tail = NULL;
  return(scbp);
}

/*+F*************************************************************************
 * Function:
 *   scbq_remove
 *
 * Description:
 *   Removes an SCB from the list.
 *
 *-F*************************************************************************/
static inline void
scbq_remove(volatile scb_queue_type *queue, struct aic7xxx_scb *scb)
{
  if (queue->head == scb)
  {
    /* At beginning of queue, remove from head. */
    scbq_remove_head(queue);
  }
  else
  {
    struct aic7xxx_scb *curscb = queue->head;

    /*
     * Search until the next scb is the one we're looking for, or
     * we run out of queue.
     */
    while ((curscb != NULL) && (curscb->q_next != scb))
    {
      curscb = curscb->q_next;
    }
    if (curscb != NULL)
    {
      /* Found it. */
      curscb->q_next = scb->q_next;
      if (scb->q_next == NULL)
      {
        /* Update the tail when removing the tail. */
        queue->tail = curscb;
      }
    }
  }
}

/*+F*************************************************************************
 * Function:
 *   scbq_insert_tail
 *
 * Description:
 *   Add an SCB at the tail of the list.
 *
 *-F*************************************************************************/
static inline void
scbq_insert_tail(volatile scb_queue_type *queue, struct aic7xxx_scb *scb)
{
  scb->q_next = NULL;
  if (queue->tail != NULL)       /* Add the scb at the end of the list. */
    queue->tail->q_next = scb;
  queue->tail = scb;             /* Update the tail. */
  if (queue->head == NULL)       /* If list was empty, update head. */
    queue->head = queue->tail;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_match_scb
 *
 * Description:
 *   Checks to see if an scb matches the target/channel as specified.
 *   If target is ALL_TARGETS (-1), then we're looking for any device
 *   on the specified channel; this happens when a channel is going
 *   to be reset and all devices on that channel must be aborted.
 *-F*************************************************************************/
static int
aic7xxx_match_scb(struct aic7xxx_host *p, struct aic7xxx_scb *scb,
    int target, int channel, int lun, unsigned char tag)
{
  int targ = (scb->hscb->target_channel_lun >> 4) & 0x0F;
  int chan = (scb->hscb->target_channel_lun >> 3) & 0x01;
  int slun = scb->hscb->target_channel_lun & 0x07;
  int match;

  match = ((chan == channel) || (channel == ALL_CHANNELS));
  if (match != 0)
    match = ((targ == target) || (target == ALL_TARGETS));
  if (match != 0)
    match = ((lun == slun) || (lun == ALL_LUNS));
  if (match != 0)
    match = ((tag == scb->hscb->tag) || (tag == SCB_LIST_NULL));

  return (match);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_add_curscb_to_free_list
 *
 * Description:
 *   Adds the current scb (in SCBPTR) to the list of free SCBs.
 *-F*************************************************************************/
static void
aic7xxx_add_curscb_to_free_list(struct aic7xxx_host *p)
{
  /*
   * Invalidate the tag so that aic7xxx_find_scb doesn't think
   * it's active
   */
  aic_outb(p, SCB_LIST_NULL, SCB_TAG);
  aic_outb(p, 0, SCB_CONTROL);

  aic_outb(p, aic_inb(p, FREE_SCBH), SCB_NEXT);
  aic_outb(p, aic_inb(p, SCBPTR), FREE_SCBH);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_rem_scb_from_disc_list
 *
 * Description:
 *   Removes the current SCB from the disconnected list and adds it
 *   to the free list.
 *-F*************************************************************************/
static unsigned char
aic7xxx_rem_scb_from_disc_list(struct aic7xxx_host *p, unsigned char scbptr,
                               unsigned char prev)
{
  unsigned char next;

  aic_outb(p, scbptr, SCBPTR);
  next = aic_inb(p, SCB_NEXT);
  aic7xxx_add_curscb_to_free_list(p);

  if (prev != SCB_LIST_NULL)
  {
    aic_outb(p, prev, SCBPTR);
    aic_outb(p, next, SCB_NEXT);
  }
  else
  {
    aic_outb(p, next, DISCONNECTED_SCBH);
  }

  return next;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_busy_target
 *
 * Description:
 *   Set the specified target busy.
 *-F*************************************************************************/
static inline void
aic7xxx_busy_target(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  p->untagged_scbs[scb->hscb->target_channel_lun] = scb->hscb->tag;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_index_busy_target
 *
 * Description:
 *   Returns the index of the busy target, and optionally sets the
 *   target inactive.
 *-F*************************************************************************/
static inline unsigned char
aic7xxx_index_busy_target(struct aic7xxx_host *p, unsigned char tcl,
    int unbusy)
{
  unsigned char busy_scbid;

  busy_scbid = p->untagged_scbs[tcl];
  if (unbusy)
  {
    p->untagged_scbs[tcl] = SCB_LIST_NULL;
  }
  return (busy_scbid);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_find_scb
 *
 * Description:
 *   Look through the SCB array of the card and attempt to find the
 *   hardware SCB that corresponds to the passed in SCB.  Return
 *   SCB_LIST_NULL if unsuccessful.  This routine assumes that the
 *   card is already paused.
 *-F*************************************************************************/
static unsigned char
aic7xxx_find_scb(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  unsigned char saved_scbptr;
  unsigned char curindex;

  saved_scbptr = aic_inb(p, SCBPTR);
  curindex = 0;
  for (curindex = 0; curindex < p->scb_data->maxhscbs; curindex++)
  {
    aic_outb(p, curindex, SCBPTR);
    if (aic_inb(p, SCB_TAG) == scb->hscb->tag)
    {
      break;
    }
  }
  aic_outb(p, saved_scbptr, SCBPTR);
  if (curindex >= p->scb_data->maxhscbs)
  {
    curindex = SCB_LIST_NULL;
  }

  return (curindex);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_allocate_scb
 *
 * Description:
 *   Get an SCB from the free list or by allocating a new one.
 *-F*************************************************************************/
static int
aic7xxx_allocate_scb(struct aic7xxx_host *p)
{
  struct aic7xxx_scb   *scbp = NULL;
  int scb_size = (sizeof (struct hw_scatterlist) * AIC7XXX_MAX_SG) + 12 + 6;
  int i;
  int step = PAGE_SIZE / 1024;
  unsigned long scb_count = 0;
  struct hw_scatterlist *hsgp;
  struct aic7xxx_scb *scb_ap;
  struct aic7xxx_scb_dma *scb_dma;
  unsigned char *bufs;

  if (p->scb_data->numscbs < p->scb_data->maxscbs)
  {
    /*
     * Calculate the optimal number of SCBs to allocate.
     *
     * NOTE: This formula works because the sizeof(sg_array) is always
     * 1024.  Therefore, scb_size * i would always be > PAGE_SIZE *
     * (i/step).  The (i-1) allows the left hand side of the equation
     * to grow into the right hand side to a point of near perfect
     * efficiency since scb_size * (i -1) is growing slightly faster
     * than the right hand side.  If the number of SG array elements
     * is changed, this function may not be near so efficient any more.
     *
     * Since the DMA'able buffers are now allocated in a separate
     * chunk this algorithm has been modified to match.  The '12'
     * and '6' factors in scb_size are for the DMA'able command byte
     * and sensebuffers respectively.  -DaveM
     */
    for ( i=step;; i *= 2 )
    {
      if ( (scb_size * (i-1)) >= ( (PAGE_SIZE * (i/step)) - 64 ) )
      {
        i /= 2;
        break;
      }
    }
    scb_count = min( (i-1), p->scb_data->maxscbs - p->scb_data->numscbs);
    scb_ap = kmalloc(sizeof (struct aic7xxx_scb) * scb_count
					   + sizeof(struct aic7xxx_scb_dma), GFP_ATOMIC);
    if (scb_ap == NULL)
      return(0);
    scb_dma = (struct aic7xxx_scb_dma *)&scb_ap[scb_count];
    hsgp = (struct hw_scatterlist *)
      pci_alloc_consistent(p->pdev, scb_size * scb_count,
			   &scb_dma->dma_address);
    if (hsgp == NULL)
    {
      kfree(scb_ap);
      return(0);
    }
    bufs = (unsigned char *)&hsgp[scb_count * AIC7XXX_MAX_SG];
#ifdef AIC7XXX_VERBOSE_DEBUGGING
    if (aic7xxx_verbose > 0xffff)
    {
      if (p->scb_data->numscbs == 0)
	printk(INFO_LEAD "Allocating initial %ld SCB structures.\n",
	  p->host_no, -1, -1, -1, scb_count);
      else
	printk(INFO_LEAD "Allocating %ld additional SCB structures.\n",
	  p->host_no, -1, -1, -1, scb_count);
    }
#endif
    memset(scb_ap, 0, sizeof (struct aic7xxx_scb) * scb_count);
    scb_dma->dma_offset = (unsigned long)scb_dma->dma_address
			  - (unsigned long)hsgp;
    scb_dma->dma_len = scb_size * scb_count;
    for (i=0; i < scb_count; i++)
    {
      scbp = &scb_ap[i];
      scbp->hscb = &p->scb_data->hscbs[p->scb_data->numscbs];
      scbp->sg_list = &hsgp[i * AIC7XXX_MAX_SG];
      scbp->sense_cmd = bufs;
      scbp->cmnd = bufs + 6;
      bufs += 12 + 6;
      scbp->scb_dma = scb_dma;
      memset(scbp->hscb, 0, sizeof(struct aic7xxx_hwscb));
      scbp->hscb->tag = p->scb_data->numscbs;
      /*
       * Place in the scb array; never is removed
       */
      p->scb_data->scb_array[p->scb_data->numscbs++] = scbp;
      scbq_insert_tail(&p->scb_data->free_scbs, scbp);
    }
    scbp->kmalloc_ptr = scb_ap;
  }
  return(scb_count);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_queue_cmd_complete
 *
 * Description:
 *   Due to race conditions present in the SCSI subsystem, it is easier
 *   to queue completed commands, then call scsi_done() on them when
 *   we're finished.  This function queues the completed commands.
 *-F*************************************************************************/
static void
aic7xxx_queue_cmd_complete(struct aic7xxx_host *p, struct scsi_cmnd *cmd)
{
  aic7xxx_position(cmd) = SCB_LIST_NULL;
  cmd->host_scribble = (char *)p->completeq.head;
  p->completeq.head = cmd;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_done_cmds_complete
 *
 * Description:
 *   Process the completed command queue.
 *-F*************************************************************************/
static void aic7xxx_done_cmds_complete(struct aic7xxx_host *p)
{
	struct scsi_cmnd *cmd;

	while (p->completeq.head != NULL) {
		cmd = p->completeq.head;
		p->completeq.head = (struct scsi_cmnd *) cmd->host_scribble;
		cmd->host_scribble = NULL;
		cmd->scsi_done(cmd);
	}
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_free_scb
 *
 * Description:
 *   Free the scb and insert into the free scb list.
 *-F*************************************************************************/
static void
aic7xxx_free_scb(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{

  scb->flags = SCB_FREE;
  scb->cmd = NULL;
  scb->sg_count = 0;
  scb->sg_length = 0;
  scb->tag_action = 0;
  scb->hscb->control = 0;
  scb->hscb->target_status = 0;
  scb->hscb->target_channel_lun = SCB_LIST_NULL;

  scbq_insert_head(&p->scb_data->free_scbs, scb);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_done
 *
 * Description:
 *   Calls the higher level scsi done function and frees the scb.
 *-F*************************************************************************/
static void
aic7xxx_done(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
	struct scsi_cmnd *cmd = scb->cmd;
	struct aic_dev_data *aic_dev = cmd->device->hostdata;
	int tindex = TARGET_INDEX(cmd);
	struct aic7xxx_scb *scbp;
	unsigned char queue_depth;

        scsi_dma_unmap(cmd);

  if (scb->flags & SCB_SENSE)
  {
    pci_unmap_single(p->pdev,
                     le32_to_cpu(scb->sg_list[0].address),
                     SCSI_SENSE_BUFFERSIZE,
                     PCI_DMA_FROMDEVICE);
  }
  if (scb->flags & SCB_RECOVERY_SCB)
  {
    p->flags &= ~AHC_ABORT_PENDING;
  }
  if (scb->flags & (SCB_RESET|SCB_ABORT))
  {
    cmd->result |= (DID_RESET << 16);
  }

  if ((scb->flags & SCB_MSGOUT_BITS) != 0)
  {
    unsigned short mask;
    int message_error = FALSE;

    mask = 0x01 << tindex;
 
    /*
     * Check to see if we get an invalid message or a message error
     * after failing to negotiate a wide or sync transfer message.
     */
    if ((scb->flags & SCB_SENSE) && 
          ((scb->cmd->sense_buffer[12] == 0x43) ||  /* INVALID_MESSAGE */
          (scb->cmd->sense_buffer[12] == 0x49))) /* MESSAGE_ERROR  */
    {
      message_error = TRUE;
    }

    if (scb->flags & SCB_MSGOUT_WDTR)
    {
      if (message_error)
      {
        if ( (aic7xxx_verbose & VERBOSE_NEGOTIATION2) &&
             (aic_dev->flags & DEVICE_PRINT_DTR) )
        {
          printk(INFO_LEAD "Device failed to complete Wide Negotiation "
            "processing and\n", p->host_no, CTL_OF_SCB(scb));
          printk(INFO_LEAD "returned a sense error code for invalid message, "
            "disabling future\n", p->host_no, CTL_OF_SCB(scb));
          printk(INFO_LEAD "Wide negotiation to this device.\n", p->host_no,
            CTL_OF_SCB(scb));
        }
        aic_dev->needwdtr = aic_dev->needwdtr_copy = 0;
      }
    }
    if (scb->flags & SCB_MSGOUT_SDTR)
    {
      if (message_error)
      {
        if ( (aic7xxx_verbose & VERBOSE_NEGOTIATION2) &&
             (aic_dev->flags & DEVICE_PRINT_DTR) )
        {
          printk(INFO_LEAD "Device failed to complete Sync Negotiation "
            "processing and\n", p->host_no, CTL_OF_SCB(scb));
          printk(INFO_LEAD "returned a sense error code for invalid message, "
            "disabling future\n", p->host_no, CTL_OF_SCB(scb));
          printk(INFO_LEAD "Sync negotiation to this device.\n", p->host_no,
            CTL_OF_SCB(scb));
          aic_dev->flags &= ~DEVICE_PRINT_DTR;
        }
        aic_dev->needsdtr = aic_dev->needsdtr_copy = 0;
      }
    }
    if (scb->flags & SCB_MSGOUT_PPR)
    {
      if(message_error)
      {
        if ( (aic7xxx_verbose & VERBOSE_NEGOTIATION2) &&
             (aic_dev->flags & DEVICE_PRINT_DTR) )
        {
          printk(INFO_LEAD "Device failed to complete Parallel Protocol "
            "Request processing and\n", p->host_no, CTL_OF_SCB(scb));
          printk(INFO_LEAD "returned a sense error code for invalid message, "
            "disabling future\n", p->host_no, CTL_OF_SCB(scb));
          printk(INFO_LEAD "Parallel Protocol Request negotiation to this "
            "device.\n", p->host_no, CTL_OF_SCB(scb));
        }
        /*
         * Disable PPR negotiation and revert back to WDTR and SDTR setup
         */
        aic_dev->needppr = aic_dev->needppr_copy = 0;
        aic_dev->needsdtr = aic_dev->needsdtr_copy = 1;
        aic_dev->needwdtr = aic_dev->needwdtr_copy = 1;
      }
    }
  }

  queue_depth = aic_dev->temp_q_depth;
  if (queue_depth >= aic_dev->active_cmds)
  {
    scbp = scbq_remove_head(&aic_dev->delayed_scbs);
    if (scbp)
    {
      if (queue_depth == 1)
      {
        /*
         * Give extra preference to untagged devices, such as CD-R devices
         * This makes it more likely that a drive *won't* stuff up while
         * waiting on data at a critical time, such as CD-R writing and
         * audio CD ripping operations.  Should also benefit tape drives.
         */
        scbq_insert_head(&p->waiting_scbs, scbp);
      }
      else
      {
        scbq_insert_tail(&p->waiting_scbs, scbp);
      }
#ifdef AIC7XXX_VERBOSE_DEBUGGING
      if (aic7xxx_verbose > 0xffff)
        printk(INFO_LEAD "Moving SCB from delayed to waiting queue.\n",
               p->host_no, CTL_OF_SCB(scbp));
#endif
      if (queue_depth > aic_dev->active_cmds)
      {
        scbp = scbq_remove_head(&aic_dev->delayed_scbs);
        if (scbp)
          scbq_insert_tail(&p->waiting_scbs, scbp);
      }
    }
  }
  if (!(scb->tag_action))
  {
    aic7xxx_index_busy_target(p, scb->hscb->target_channel_lun,
                              /* unbusy */ TRUE);
    if (cmd->device->simple_tags)
    {
      aic_dev->temp_q_depth = aic_dev->max_q_depth;
    }
  }
  if(scb->flags & SCB_DTR_SCB)
  {
    aic_dev->dtr_pending = 0;
  }
  aic_dev->active_cmds--;
  p->activescbs--;

  if ((scb->sg_length >= 512) && (((cmd->result >> 16) & 0xf) == DID_OK))
  {
    long *ptr;
    int x, i;


    if (rq_data_dir(cmd->request) == WRITE)
    {
      aic_dev->w_total++;
      ptr = aic_dev->w_bins;
    }
    else
    {
      aic_dev->r_total++;
      ptr = aic_dev->r_bins;
    }
    x = scb->sg_length;
    x >>= 10;
    for(i=0; i<6; i++)
    {
      x >>= 2;
      if(!x) {
        ptr[i]++;
	break;
      }
    }
    if(i == 6 && x)
      ptr[5]++;
  }
  aic7xxx_free_scb(p, scb);
  aic7xxx_queue_cmd_complete(p, cmd);

}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_run_done_queue
 *
 * Description:
 *   Calls the aic7xxx_done() for the scsi_cmnd of each scb in the
 *   aborted list, and adds each scb to the free list.  If complete
 *   is TRUE, we also process the commands complete list.
 *-F*************************************************************************/
static void
aic7xxx_run_done_queue(struct aic7xxx_host *p, /*complete*/ int complete)
{
  struct aic7xxx_scb *scb;
  int i, found = 0;

  for (i = 0; i < p->scb_data->numscbs; i++)
  {
    scb = p->scb_data->scb_array[i];
    if (scb->flags & SCB_QUEUED_FOR_DONE)
    {
      if (scb->flags & SCB_QUEUE_FULL)
      {
	scb->cmd->result = QUEUE_FULL << 1;
      }
      else
      {
        if (aic7xxx_verbose & (VERBOSE_ABORT_PROCESS | VERBOSE_RESET_PROCESS))
          printk(INFO_LEAD "Aborting scb %d\n",
               p->host_no, CTL_OF_SCB(scb), scb->hscb->tag);
        /*
         * Clear any residual information since the normal aic7xxx_done() path
         * doesn't touch the residuals.
         */
        scb->hscb->residual_SG_segment_count = 0;
        scb->hscb->residual_data_count[0] = 0;
        scb->hscb->residual_data_count[1] = 0;
        scb->hscb->residual_data_count[2] = 0;
      }
      found++;
      aic7xxx_done(p, scb);
    }
  }
  if (aic7xxx_verbose & (VERBOSE_ABORT_RETURN | VERBOSE_RESET_RETURN))
  {
    printk(INFO_LEAD "%d commands found and queued for "
        "completion.\n", p->host_no, -1, -1, -1, found);
  }
  if (complete)
  {
    aic7xxx_done_cmds_complete(p);
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_abort_waiting_scb
 *
 * Description:
 *   Manipulate the waiting for selection list and return the
 *   scb that follows the one that we remove.
 *-F*************************************************************************/
static unsigned char
aic7xxx_abort_waiting_scb(struct aic7xxx_host *p, struct aic7xxx_scb *scb,
    unsigned char scbpos, unsigned char prev)
{
  unsigned char curscb, next;

  /*
   * Select the SCB we want to abort and pull the next pointer out of it.
   */
  curscb = aic_inb(p, SCBPTR);
  aic_outb(p, scbpos, SCBPTR);
  next = aic_inb(p, SCB_NEXT);

  aic7xxx_add_curscb_to_free_list(p);

  /*
   * Update the waiting list
   */
  if (prev == SCB_LIST_NULL)
  {
    /*
     * First in the list
     */
    aic_outb(p, next, WAITING_SCBH);
  }
  else
  {
    /*
     * Select the scb that pointed to us and update its next pointer.
     */
    aic_outb(p, prev, SCBPTR);
    aic_outb(p, next, SCB_NEXT);
  }
  /*
   * Point us back at the original scb position and inform the SCSI
   * system that the command has been aborted.
   */
  aic_outb(p, curscb, SCBPTR);
  return (next);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_search_qinfifo
 *
 * Description:
 *   Search the queue-in FIFO for matching SCBs and conditionally
 *   requeue.  Returns the number of matching SCBs.
 *-F*************************************************************************/
static int
aic7xxx_search_qinfifo(struct aic7xxx_host *p, int target, int channel,
    int lun, unsigned char tag, int flags, int requeue,
    volatile scb_queue_type *queue)
{
  int      found;
  unsigned char qinpos, qintail;
  struct aic7xxx_scb *scbp;

  found = 0;
  qinpos = aic_inb(p, QINPOS);
  qintail = p->qinfifonext;

  p->qinfifonext = qinpos;

  while (qinpos != qintail)
  {
    scbp = p->scb_data->scb_array[p->qinfifo[qinpos++]];
    if (aic7xxx_match_scb(p, scbp, target, channel, lun, tag))
    {
       /*
        * We found an scb that needs to be removed.
        */
       if (requeue && (queue != NULL))
       {
         if (scbp->flags & SCB_WAITINGQ)
         {
           scbq_remove(queue, scbp);
           scbq_remove(&p->waiting_scbs, scbp);
           scbq_remove(&AIC_DEV(scbp->cmd)->delayed_scbs, scbp);
           AIC_DEV(scbp->cmd)->active_cmds++;
           p->activescbs++;
         }
         scbq_insert_tail(queue, scbp);
         AIC_DEV(scbp->cmd)->active_cmds--;
         p->activescbs--;
         scbp->flags |= SCB_WAITINGQ;
         if ( !(scbp->tag_action & TAG_ENB) )
         {
           aic7xxx_index_busy_target(p, scbp->hscb->target_channel_lun,
             TRUE);
         }
       }
       else if (requeue)
       {
         p->qinfifo[p->qinfifonext++] = scbp->hscb->tag;
       }
       else
       {
        /*
         * Preserve any SCB_RECOVERY_SCB flags on this scb then set the
         * flags we were called with, presumeably so aic7xxx_run_done_queue
         * can find this scb
         */
         scbp->flags = flags | (scbp->flags & SCB_RECOVERY_SCB);
         if (aic7xxx_index_busy_target(p, scbp->hscb->target_channel_lun,
                                       FALSE) == scbp->hscb->tag)
         {
           aic7xxx_index_busy_target(p, scbp->hscb->target_channel_lun,
             TRUE);
         }
       }
       found++;
    }
    else
    {
      p->qinfifo[p->qinfifonext++] = scbp->hscb->tag;
    }
  }
  /*
   * Now that we've done the work, clear out any left over commands in the
   * qinfifo and update the KERNEL_QINPOS down on the card.
   *
   *  NOTE: This routine expect the sequencer to already be paused when
   *        it is run....make sure it's that way!
   */
  qinpos = p->qinfifonext;
  while(qinpos != qintail)
  {
    p->qinfifo[qinpos++] = SCB_LIST_NULL;
  }
  if (p->features & AHC_QUEUE_REGS)
    aic_outb(p, p->qinfifonext, HNSCB_QOFF);
  else
    aic_outb(p, p->qinfifonext, KERNEL_QINPOS);

  return (found);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_scb_on_qoutfifo
 *
 * Description:
 *   Is the scb that was passed to us currently on the qoutfifo?
 *-F*************************************************************************/
static int
aic7xxx_scb_on_qoutfifo(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  int i=0;

  while(p->qoutfifo[(p->qoutfifonext + i) & 0xff ] != SCB_LIST_NULL)
  {
    if(p->qoutfifo[(p->qoutfifonext + i) & 0xff ] == scb->hscb->tag)
      return TRUE;
    else
      i++;
  }
  return FALSE;
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_reset_device
 *
 * Description:
 *   The device at the given target/channel has been reset.  Abort
 *   all active and queued scbs for that target/channel.  This function
 *   need not worry about linked next pointers because if was a MSG_ABORT_TAG
 *   then we had a tagged command (no linked next), if it was MSG_ABORT or
 *   MSG_BUS_DEV_RESET then the device won't know about any commands any more
 *   and no busy commands will exist, and if it was a bus reset, then nothing
 *   knows about any linked next commands any more.  In all cases, we don't
 *   need to worry about the linked next or busy scb, we just need to clear
 *   them.
 *-F*************************************************************************/
static void
aic7xxx_reset_device(struct aic7xxx_host *p, int target, int channel,
                     int lun, unsigned char tag)
{
  struct aic7xxx_scb *scbp, *prev_scbp;
  struct scsi_device *sd;
  unsigned char active_scb, tcl, scb_tag;
  int i = 0, init_lists = FALSE;
  struct aic_dev_data *aic_dev;

  /*
   * Restore this when we're done
   */
  active_scb = aic_inb(p, SCBPTR);
  scb_tag = aic_inb(p, SCB_TAG);

  if (aic7xxx_verbose & (VERBOSE_RESET_PROCESS | VERBOSE_ABORT_PROCESS))
  {
    printk(INFO_LEAD "Reset device, hardware_scb %d,\n",
         p->host_no, channel, target, lun, active_scb);
    printk(INFO_LEAD "Current scb %d, SEQADDR 0x%x, LASTPHASE "
           "0x%x\n",
         p->host_no, channel, target, lun, scb_tag,
         aic_inb(p, SEQADDR0) | (aic_inb(p, SEQADDR1) << 8),
         aic_inb(p, LASTPHASE));
    printk(INFO_LEAD "SG_CACHEPTR 0x%x, SG_COUNT %d, SCSISIGI 0x%x\n",
         p->host_no, channel, target, lun,
         (p->features & AHC_ULTRA2) ?  aic_inb(p, SG_CACHEPTR) : 0,
         aic_inb(p, SG_COUNT), aic_inb(p, SCSISIGI));
    printk(INFO_LEAD "SSTAT0 0x%x, SSTAT1 0x%x, SSTAT2 0x%x\n",
         p->host_no, channel, target, lun, aic_inb(p, SSTAT0),
         aic_inb(p, SSTAT1), aic_inb(p, SSTAT2));
  }

  /*
   * Deal with the busy target and linked next issues.
   */
  list_for_each_entry(aic_dev, &p->aic_devs, list)
  {
    if (aic7xxx_verbose & (VERBOSE_RESET_PROCESS | VERBOSE_ABORT_PROCESS))
      printk(INFO_LEAD "processing aic_dev %p\n", p->host_no, channel, target,
		    lun, aic_dev);
    sd = aic_dev->SDptr;

    if((target != ALL_TARGETS && target != sd->id) ||
       (channel != ALL_CHANNELS && channel != sd->channel))
      continue;
    if (aic7xxx_verbose & (VERBOSE_ABORT_PROCESS | VERBOSE_RESET_PROCESS))
        printk(INFO_LEAD "Cleaning up status information "
          "and delayed_scbs.\n", p->host_no, sd->channel, sd->id, sd->lun);
    aic_dev->flags &= ~BUS_DEVICE_RESET_PENDING;
    if ( tag == SCB_LIST_NULL )
    {
      aic_dev->dtr_pending = 0;
      aic_dev->needppr = aic_dev->needppr_copy;
      aic_dev->needsdtr = aic_dev->needsdtr_copy;
      aic_dev->needwdtr = aic_dev->needwdtr_copy;
      aic_dev->flags = DEVICE_PRINT_DTR;
      aic_dev->temp_q_depth = aic_dev->max_q_depth;
    }
    tcl = (sd->id << 4) | (sd->channel << 3) | sd->lun;
    if ( (aic7xxx_index_busy_target(p, tcl, FALSE) == tag) ||
         (tag == SCB_LIST_NULL) )
      aic7xxx_index_busy_target(p, tcl, /* unbusy */ TRUE);
    prev_scbp = NULL; 
    scbp = aic_dev->delayed_scbs.head;
    while (scbp != NULL)
    {
      prev_scbp = scbp;
      scbp = scbp->q_next;
      if (aic7xxx_match_scb(p, prev_scbp, target, channel, lun, tag))
      {
        scbq_remove(&aic_dev->delayed_scbs, prev_scbp);
        if (prev_scbp->flags & SCB_WAITINGQ)
        {
          aic_dev->active_cmds++;
          p->activescbs++;
        }
        prev_scbp->flags &= ~(SCB_ACTIVE | SCB_WAITINGQ);
        prev_scbp->flags |= SCB_RESET | SCB_QUEUED_FOR_DONE;
      }
    }
  }

  if (aic7xxx_verbose & (VERBOSE_ABORT_PROCESS | VERBOSE_RESET_PROCESS))
    printk(INFO_LEAD "Cleaning QINFIFO.\n", p->host_no, channel, target, lun );
  aic7xxx_search_qinfifo(p, target, channel, lun, tag,
      SCB_RESET | SCB_QUEUED_FOR_DONE, /* requeue */ FALSE, NULL);

/*
 *  Search the waiting_scbs queue for matches, this catches any SCB_QUEUED
 *  ABORT/RESET commands.
 */
  if (aic7xxx_verbose & (VERBOSE_ABORT_PROCESS | VERBOSE_RESET_PROCESS))
    printk(INFO_LEAD "Cleaning waiting_scbs.\n", p->host_no, channel,
      target, lun );
  {
    struct aic7xxx_scb *scbp, *prev_scbp;

    prev_scbp = NULL; 
    scbp = p->waiting_scbs.head;
    while (scbp != NULL)
    {
      prev_scbp = scbp;
      scbp = scbp->q_next;
      if (aic7xxx_match_scb(p, prev_scbp, target, channel, lun, tag))
      {
        scbq_remove(&p->waiting_scbs, prev_scbp);
        if (prev_scbp->flags & SCB_WAITINGQ)
        {
          AIC_DEV(prev_scbp->cmd)->active_cmds++;
          p->activescbs++;
        }
        prev_scbp->flags &= ~(SCB_ACTIVE | SCB_WAITINGQ);
        prev_scbp->flags |= SCB_RESET | SCB_QUEUED_FOR_DONE;
      }
    }
  }


  /*
   * Search waiting for selection list.
   */
  if (aic7xxx_verbose & (VERBOSE_ABORT_PROCESS | VERBOSE_RESET_PROCESS))
    printk(INFO_LEAD "Cleaning waiting for selection "
      "list.\n", p->host_no, channel, target, lun);
  {
    unsigned char next, prev, scb_index;

    next = aic_inb(p, WAITING_SCBH);  /* Start at head of list. */
    prev = SCB_LIST_NULL;
    while (next != SCB_LIST_NULL)
    {
      aic_outb(p, next, SCBPTR);
      scb_index = aic_inb(p, SCB_TAG);
      if (scb_index >= p->scb_data->numscbs)
      {
       /*
        * No aic7xxx_verbose check here.....we want to see this since it
        * means either the kernel driver or the sequencer screwed things up
        */
        printk(WARN_LEAD "Waiting List inconsistency; SCB index=%d, "
          "numscbs=%d\n", p->host_no, channel, target, lun, scb_index,
          p->scb_data->numscbs);
        next = aic_inb(p, SCB_NEXT);
        aic7xxx_add_curscb_to_free_list(p);
      }
      else
      {
        scbp = p->scb_data->scb_array[scb_index];
        if (aic7xxx_match_scb(p, scbp, target, channel, lun, tag))
        {
          next = aic7xxx_abort_waiting_scb(p, scbp, next, prev);
          if (scbp->flags & SCB_WAITINGQ)
          {
            AIC_DEV(scbp->cmd)->active_cmds++;
            p->activescbs++;
          }
          scbp->flags &= ~(SCB_ACTIVE | SCB_WAITINGQ);
          scbp->flags |= SCB_RESET | SCB_QUEUED_FOR_DONE;
          if (prev == SCB_LIST_NULL)
          {
            /*
             * This is either the first scb on the waiting list, or we
             * have already yanked the first and haven't left any behind.
             * Either way, we need to turn off the selection hardware if
             * it isn't already off.
             */
            aic_outb(p, aic_inb(p, SCSISEQ) & ~ENSELO, SCSISEQ);
            aic_outb(p, CLRSELTIMEO, CLRSINT1);
          }
        }
        else
        {
          prev = next;
          next = aic_inb(p, SCB_NEXT);
        }
      }
    }
  }

  /*
   * Go through disconnected list and remove any entries we have queued
   * for completion, zeroing their control byte too.
   */
  if (aic7xxx_verbose & (VERBOSE_ABORT_PROCESS | VERBOSE_RESET_PROCESS))
    printk(INFO_LEAD "Cleaning disconnected scbs "
      "list.\n", p->host_no, channel, target, lun);
  if (p->flags & AHC_PAGESCBS)
  {
    unsigned char next, prev, scb_index;

    next = aic_inb(p, DISCONNECTED_SCBH);
    prev = SCB_LIST_NULL;
    while (next != SCB_LIST_NULL)
    {
      aic_outb(p, next, SCBPTR);
      scb_index = aic_inb(p, SCB_TAG);
      if (scb_index > p->scb_data->numscbs)
      {
        printk(WARN_LEAD "Disconnected List inconsistency; SCB index=%d, "
          "numscbs=%d\n", p->host_no, channel, target, lun, scb_index,
          p->scb_data->numscbs);
        next = aic7xxx_rem_scb_from_disc_list(p, next, prev);
      }
      else
      {
        scbp = p->scb_data->scb_array[scb_index];
        if (aic7xxx_match_scb(p, scbp, target, channel, lun, tag))
        {
          next = aic7xxx_rem_scb_from_disc_list(p, next, prev);
          if (scbp->flags & SCB_WAITINGQ)
          {
            AIC_DEV(scbp->cmd)->active_cmds++;
            p->activescbs++;
          }
          scbp->flags &= ~(SCB_ACTIVE | SCB_WAITINGQ);
          scbp->flags |= SCB_RESET | SCB_QUEUED_FOR_DONE;
          scbp->hscb->control = 0;
        }
        else
        {
          prev = next;
          next = aic_inb(p, SCB_NEXT);
        }
      }
    }
  }

  /*
   * Walk the free list making sure no entries on the free list have
   * a valid SCB_TAG value or SCB_CONTROL byte.
   */
  if (p->flags & AHC_PAGESCBS)
  {
    unsigned char next;

    next = aic_inb(p, FREE_SCBH);
    while (next != SCB_LIST_NULL)
    {
      aic_outb(p, next, SCBPTR);
      if (aic_inb(p, SCB_TAG) < p->scb_data->numscbs)
      {
        printk(WARN_LEAD "Free list inconsistency!.\n", p->host_no, channel,
          target, lun);
        init_lists = TRUE;
        next = SCB_LIST_NULL;
      }
      else
      {
        aic_outb(p, SCB_LIST_NULL, SCB_TAG);
        aic_outb(p, 0, SCB_CONTROL);
        next = aic_inb(p, SCB_NEXT);
      }
    }
  }

  /*
   * Go through the hardware SCB array looking for commands that
   * were active but not on any list.
   */
  if (init_lists)
  {
    aic_outb(p, SCB_LIST_NULL, FREE_SCBH);
    aic_outb(p, SCB_LIST_NULL, WAITING_SCBH);
    aic_outb(p, SCB_LIST_NULL, DISCONNECTED_SCBH);
  }
  for (i = p->scb_data->maxhscbs - 1; i >= 0; i--)
  {
    unsigned char scbid;

    aic_outb(p, i, SCBPTR);
    if (init_lists)
    {
      aic_outb(p, SCB_LIST_NULL, SCB_TAG);
      aic_outb(p, SCB_LIST_NULL, SCB_NEXT);
      aic_outb(p, 0, SCB_CONTROL);
      aic7xxx_add_curscb_to_free_list(p);
    }
    else
    {
      scbid = aic_inb(p, SCB_TAG);
      if (scbid < p->scb_data->numscbs)
      {
        scbp = p->scb_data->scb_array[scbid];
        if (aic7xxx_match_scb(p, scbp, target, channel, lun, tag))
        {
          aic_outb(p, 0, SCB_CONTROL);
          aic_outb(p, SCB_LIST_NULL, SCB_TAG);
          aic7xxx_add_curscb_to_free_list(p);
        }
      }
    }
  }

  /*
   * Go through the entire SCB array now and look for commands for
   * for this target that are stillactive.  These are other (most likely
   * tagged) commands that were disconnected when the reset occurred.
   * Any commands we find here we know this about, it wasn't on any queue,
   * it wasn't in the qinfifo, it wasn't in the disconnected or waiting
   * lists, so it really must have been a paged out SCB.  In that case,
   * we shouldn't need to bother with updating any counters, just mark
   * the correct flags and go on.
   */
  for (i = 0; i < p->scb_data->numscbs; i++)
  {
    scbp = p->scb_data->scb_array[i];
    if ((scbp->flags & SCB_ACTIVE) &&
        aic7xxx_match_scb(p, scbp, target, channel, lun, tag) &&
        !aic7xxx_scb_on_qoutfifo(p, scbp))
    {
      if (scbp->flags & SCB_WAITINGQ)
      {
        scbq_remove(&p->waiting_scbs, scbp);
        scbq_remove(&AIC_DEV(scbp->cmd)->delayed_scbs, scbp);
        AIC_DEV(scbp->cmd)->active_cmds++;
        p->activescbs++;
      }
      scbp->flags |= SCB_RESET | SCB_QUEUED_FOR_DONE;
      scbp->flags &= ~(SCB_ACTIVE | SCB_WAITINGQ);
    }
  }

  aic_outb(p, active_scb, SCBPTR);
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_clear_intstat
 *
 * Description:
 *   Clears the interrupt status.
 *-F*************************************************************************/
static void
aic7xxx_clear_intstat(struct aic7xxx_host *p)
{
  /* Clear any interrupt conditions this may have caused. */
  aic_outb(p, CLRSELDO | CLRSELDI | CLRSELINGO, CLRSINT0);
  aic_outb(p, CLRSELTIMEO | CLRATNO | CLRSCSIRSTI | CLRBUSFREE | CLRSCSIPERR |
       CLRPHASECHG | CLRREQINIT, CLRSINT1);
  aic_outb(p, CLRSCSIINT | CLRSEQINT | CLRBRKADRINT | CLRPARERR, CLRINT);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_reset_current_bus
 *
 * Description:
 *   Reset the current SCSI bus.
 *-F*************************************************************************/
static void
aic7xxx_reset_current_bus(struct aic7xxx_host *p)
{

  /* Disable reset interrupts. */
  aic_outb(p, aic_inb(p, SIMODE1) & ~ENSCSIRST, SIMODE1);

  /* Turn off the bus' current operations, after all, we shouldn't have any
   * valid commands left to cause a RSELI and SELO once we've tossed the
   * bus away with this reset, so we might as well shut down the sequencer
   * until the bus is restarted as opposed to saving the current settings
   * and restoring them (which makes no sense to me). */

  /* Turn on the bus reset. */
  aic_outb(p, aic_inb(p, SCSISEQ) | SCSIRSTO, SCSISEQ);
  while ( (aic_inb(p, SCSISEQ) & SCSIRSTO) == 0)
    mdelay(5);

  /*
   * Some of the new Ultra2 chipsets need a longer delay after a chip
   * reset than just the init setup creates, so we have to delay here
   * before we go into a reset in order to make the chips happy.
   */
  if (p->features & AHC_ULTRA2)
    mdelay(250);
  else
    mdelay(50);

  /* Turn off the bus reset. */
  aic_outb(p, 0, SCSISEQ);
  mdelay(10);

  aic7xxx_clear_intstat(p);
  /* Re-enable reset interrupts. */
  aic_outb(p, aic_inb(p, SIMODE1) | ENSCSIRST, SIMODE1);

}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_reset_channel
 *
 * Description:
 *   Reset the channel.
 *-F*************************************************************************/
static void
aic7xxx_reset_channel(struct aic7xxx_host *p, int channel, int initiate_reset)
{
  unsigned long offset_min, offset_max;
  unsigned char sblkctl;
  int cur_channel;

  if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
    printk(INFO_LEAD "Reset channel called, %s initiate reset.\n",
      p->host_no, channel, -1, -1, (initiate_reset==TRUE) ? "will" : "won't" );


  if (channel == 1)
  {
    offset_min = 8;
    offset_max = 16;
  }
  else
  {
    if (p->features & AHC_TWIN)
    {
      /* Channel A */
      offset_min = 0;
      offset_max = 8;
    }
    else
    {
      offset_min = 0;
      if (p->features & AHC_WIDE)
      {
        offset_max = 16;
      }
      else
      {
        offset_max = 8;
      }
    }
  }

  while (offset_min < offset_max)
  {
    /*
     * Revert to async/narrow transfers until we renegotiate.
     */
    aic_outb(p, 0, TARG_SCSIRATE + offset_min);
    if (p->features & AHC_ULTRA2)
    {
      aic_outb(p, 0, TARG_OFFSET + offset_min);
    }
    offset_min++;
  }

  /*
   * Reset the bus and unpause/restart the controller
   */
  sblkctl = aic_inb(p, SBLKCTL);
  if ( (p->chip & AHC_CHIPID_MASK) == AHC_AIC7770 )
    cur_channel = (sblkctl & SELBUSB) >> 3;
  else
    cur_channel = 0;
  if ( (cur_channel != channel) && (p->features & AHC_TWIN) )
  {
    /*
     * Case 1: Command for another bus is active
     */
    if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
      printk(INFO_LEAD "Stealthily resetting idle channel.\n", p->host_no,
        channel, -1, -1);
    /*
     * Stealthily reset the other bus without upsetting the current bus.
     */
    aic_outb(p, sblkctl ^ SELBUSB, SBLKCTL);
    aic_outb(p, aic_inb(p, SIMODE1) & ~ENBUSFREE, SIMODE1);
    if (initiate_reset)
    {
      aic7xxx_reset_current_bus(p);
    }
    aic_outb(p, aic_inb(p, SCSISEQ) & (ENSELI|ENRSELI|ENAUTOATNP), SCSISEQ);
    aic7xxx_clear_intstat(p);
    aic_outb(p, sblkctl, SBLKCTL);
  }
  else
  {
    /*
     * Case 2: A command from this bus is active or we're idle.
     */
    if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
      printk(INFO_LEAD "Resetting currently active channel.\n", p->host_no,
        channel, -1, -1);
    aic_outb(p, aic_inb(p, SIMODE1) & ~(ENBUSFREE|ENREQINIT),
      SIMODE1);
    p->flags &= ~AHC_HANDLING_REQINITS;
    p->msg_type = MSG_TYPE_NONE;
    p->msg_len = 0;
    if (initiate_reset)
    {
      aic7xxx_reset_current_bus(p);
    }
    aic_outb(p, aic_inb(p, SCSISEQ) & (ENSELI|ENRSELI|ENAUTOATNP), SCSISEQ);
    aic7xxx_clear_intstat(p);
  }
  if (aic7xxx_verbose & VERBOSE_RESET_RETURN)
    printk(INFO_LEAD "Channel reset\n", p->host_no, channel, -1, -1);
  /*
   * Clean up all the state information for the pending transactions
   * on this bus.
   */
  aic7xxx_reset_device(p, ALL_TARGETS, channel, ALL_LUNS, SCB_LIST_NULL);

  if ( !(p->features & AHC_TWIN) )
  {
    restart_sequencer(p);
  }

  return;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_run_waiting_queues
 *
 * Description:
 *   Scan the awaiting_scbs queue downloading and starting as many
 *   scbs as we can.
 *-F*************************************************************************/
static void
aic7xxx_run_waiting_queues(struct aic7xxx_host *p)
{
  struct aic7xxx_scb *scb;
  struct aic_dev_data *aic_dev;
  int sent;


  if (p->waiting_scbs.head == NULL)
    return;

  sent = 0;

  /*
   * First handle SCBs that are waiting but have been assigned a slot.
   */
  while ((scb = scbq_remove_head(&p->waiting_scbs)) != NULL)
  {
    aic_dev = scb->cmd->device->hostdata;
    if ( !scb->tag_action )
    {
      aic_dev->temp_q_depth = 1;
    }
    if ( aic_dev->active_cmds >= aic_dev->temp_q_depth)
    {
      scbq_insert_tail(&aic_dev->delayed_scbs, scb);
    }
    else
    {
        scb->flags &= ~SCB_WAITINGQ;
        aic_dev->active_cmds++;
        p->activescbs++;
        if ( !(scb->tag_action) )
        {
          aic7xxx_busy_target(p, scb);
        }
        p->qinfifo[p->qinfifonext++] = scb->hscb->tag;
        sent++;
    }
  }
  if (sent)
  {
    if (p->features & AHC_QUEUE_REGS)
      aic_outb(p, p->qinfifonext, HNSCB_QOFF);
    else
    {
      pause_sequencer(p);
      aic_outb(p, p->qinfifonext, KERNEL_QINPOS);
      unpause_sequencer(p, FALSE);
    }
    if (p->activescbs > p->max_activescbs)
      p->max_activescbs = p->activescbs;
  }
}

#ifdef CONFIG_PCI

#define  DPE 0x80
#define  SSE 0x40
#define  RMA 0x20
#define  RTA 0x10
#define  STA 0x08
#define  DPR 0x01

/*+F*************************************************************************
 * Function:
 *   aic7xxx_pci_intr
 *
 * Description:
 *   Check the scsi card for PCI errors and clear the interrupt
 *
 *   NOTE: If you don't have this function and a 2940 card encounters
 *         a PCI error condition, the machine will end up locked as the
 *         interrupt handler gets slammed with non-stop PCI error interrupts
 *-F*************************************************************************/
static void
aic7xxx_pci_intr(struct aic7xxx_host *p)
{
  unsigned char status1;

  pci_read_config_byte(p->pdev, PCI_STATUS + 1, &status1);

  if ( (status1 & DPE) && (aic7xxx_verbose & VERBOSE_MINOR_ERROR) )
    printk(WARN_LEAD "Data Parity Error during PCI address or PCI write"
      "phase.\n", p->host_no, -1, -1, -1);
  if ( (status1 & SSE) && (aic7xxx_verbose & VERBOSE_MINOR_ERROR) )
    printk(WARN_LEAD "Signal System Error Detected\n", p->host_no,
      -1, -1, -1);
  if ( (status1 & RMA) && (aic7xxx_verbose & VERBOSE_MINOR_ERROR) )
    printk(WARN_LEAD "Received a PCI Master Abort\n", p->host_no,
      -1, -1, -1);
  if ( (status1 & RTA) && (aic7xxx_verbose & VERBOSE_MINOR_ERROR) )
    printk(WARN_LEAD "Received a PCI Target Abort\n", p->host_no,
      -1, -1, -1);
  if ( (status1 & STA) && (aic7xxx_verbose & VERBOSE_MINOR_ERROR) )
    printk(WARN_LEAD "Signaled a PCI Target Abort\n", p->host_no,
      -1, -1, -1);
  if ( (status1 & DPR) && (aic7xxx_verbose & VERBOSE_MINOR_ERROR) )
    printk(WARN_LEAD "Data Parity Error has been reported via PCI pin "
      "PERR#\n", p->host_no, -1, -1, -1);
  
  pci_write_config_byte(p->pdev, PCI_STATUS + 1, status1);
  if (status1 & (DPR|RMA|RTA))
    aic_outb(p,  CLRPARERR, CLRINT);

  if ( (aic7xxx_panic_on_abort) && (p->spurious_int > 500) )
    aic7xxx_panic_abort(p, NULL);

}
#endif /* CONFIG_PCI */

/*+F*************************************************************************
 * Function:
 *   aic7xxx_construct_ppr
 *
 * Description:
 *   Build up a Parallel Protocol Request message for use with SCSI-3
 *   devices.
 *-F*************************************************************************/
static void
aic7xxx_construct_ppr(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  p->msg_buf[p->msg_index++] = MSG_EXTENDED;
  p->msg_buf[p->msg_index++] = MSG_EXT_PPR_LEN;
  p->msg_buf[p->msg_index++] = MSG_EXT_PPR;
  p->msg_buf[p->msg_index++] = AIC_DEV(scb->cmd)->goal.period;
  p->msg_buf[p->msg_index++] = 0;
  p->msg_buf[p->msg_index++] = AIC_DEV(scb->cmd)->goal.offset;
  p->msg_buf[p->msg_index++] = AIC_DEV(scb->cmd)->goal.width;
  p->msg_buf[p->msg_index++] = AIC_DEV(scb->cmd)->goal.options;
  p->msg_len += 8;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_construct_sdtr
 *
 * Description:
 *   Constucts a synchronous data transfer message in the message
 *   buffer on the sequencer.
 *-F*************************************************************************/
static void
aic7xxx_construct_sdtr(struct aic7xxx_host *p, unsigned char period,
        unsigned char offset)
{
  p->msg_buf[p->msg_index++] = MSG_EXTENDED;
  p->msg_buf[p->msg_index++] = MSG_EXT_SDTR_LEN;
  p->msg_buf[p->msg_index++] = MSG_EXT_SDTR;
  p->msg_buf[p->msg_index++] = period;
  p->msg_buf[p->msg_index++] = offset;
  p->msg_len += 5;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_construct_wdtr
 *
 * Description:
 *   Constucts a wide data transfer message in the message buffer
 *   on the sequencer.
 *-F*************************************************************************/
static void
aic7xxx_construct_wdtr(struct aic7xxx_host *p, unsigned char bus_width)
{
  p->msg_buf[p->msg_index++] = MSG_EXTENDED;
  p->msg_buf[p->msg_index++] = MSG_EXT_WDTR_LEN;
  p->msg_buf[p->msg_index++] = MSG_EXT_WDTR;
  p->msg_buf[p->msg_index++] = bus_width;
  p->msg_len += 4;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_calc_residual
 *
 * Description:
 *   Calculate the residual data not yet transferred.
 *-F*************************************************************************/
static void
aic7xxx_calculate_residual (struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
	struct aic7xxx_hwscb *hscb;
	struct scsi_cmnd *cmd;
	int actual, i;

  cmd = scb->cmd;
  hscb = scb->hscb;

  /*
   *  Don't destroy valid residual information with
   *  residual coming from a check sense operation.
   */
  if (((scb->hscb->control & DISCONNECTED) == 0) &&
      (scb->flags & SCB_SENSE) == 0)
  {
    /*
     *  We had an underflow. At this time, there's only
     *  one other driver that bothers to check for this,
     *  and cmd->underflow seems to be set rather half-
     *  heartedly in the higher-level SCSI code.
     */
    actual = scb->sg_length;
    for (i=1; i < hscb->residual_SG_segment_count; i++)
    {
      actual -= scb->sg_list[scb->sg_count - i].length;
    }
    actual -= (hscb->residual_data_count[2] << 16) |
              (hscb->residual_data_count[1] <<  8) |
              hscb->residual_data_count[0];

    if (actual < cmd->underflow)
    {
      if (aic7xxx_verbose & VERBOSE_MINOR_ERROR)
      {
        printk(INFO_LEAD "Underflow - Wanted %u, %s %u, residual SG "
          "count %d.\n", p->host_no, CTL_OF_SCB(scb), cmd->underflow,
          (rq_data_dir(cmd->request) == WRITE) ? "wrote" : "read", actual,
          hscb->residual_SG_segment_count);
        printk(INFO_LEAD "status 0x%x.\n", p->host_no, CTL_OF_SCB(scb),
          hscb->target_status);
      }
      /*
       * In 2.4, only send back the residual information, don't flag this
       * as an error.  Before 2.4 we had to flag this as an error because
       * the mid layer didn't check residual data counts to see if the
       * command needs retried.
       */
      scsi_set_resid(cmd, scb->sg_length - actual);
      aic7xxx_status(cmd) = hscb->target_status;
    }
  }

  /*
   * Clean out the residual information in the SCB for the
   * next consumer.
   */
  hscb->residual_data_count[2] = 0;
  hscb->residual_data_count[1] = 0;
  hscb->residual_data_count[0] = 0;
  hscb->residual_SG_segment_count = 0;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_handle_device_reset
 *
 * Description:
 *   Interrupt handler for sequencer interrupts (SEQINT).
 *-F*************************************************************************/
static void
aic7xxx_handle_device_reset(struct aic7xxx_host *p, int target, int channel)
{
  unsigned char tindex = target;

  tindex |= ((channel & 0x01) << 3);

  /*
   * Go back to async/narrow transfers and renegotiate.
   */
  aic_outb(p, 0, TARG_SCSIRATE + tindex);
  if (p->features & AHC_ULTRA2)
    aic_outb(p, 0, TARG_OFFSET + tindex);
  aic7xxx_reset_device(p, target, channel, ALL_LUNS, SCB_LIST_NULL);
  if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
    printk(INFO_LEAD "Bus Device Reset delivered.\n", p->host_no, channel,
      target, -1);
  aic7xxx_run_done_queue(p, /*complete*/ TRUE);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_handle_seqint
 *
 * Description:
 *   Interrupt handler for sequencer interrupts (SEQINT).
 *-F*************************************************************************/
static void
aic7xxx_handle_seqint(struct aic7xxx_host *p, unsigned char intstat)
{
  struct aic7xxx_scb *scb;
  struct aic_dev_data *aic_dev;
  unsigned short target_mask;
  unsigned char target, lun, tindex;
  unsigned char queue_flag = FALSE;
  char channel;
  int result;

  target = ((aic_inb(p, SAVED_TCL) >> 4) & 0x0f);
  if ( (p->chip & AHC_CHIPID_MASK) == AHC_AIC7770 )
    channel = (aic_inb(p, SBLKCTL) & SELBUSB) >> 3;
  else
    channel = 0;
  tindex = target + (channel << 3);
  lun = aic_inb(p, SAVED_TCL) & 0x07;
  target_mask = (0x01 << tindex);

  /*
   * Go ahead and clear the SEQINT now, that avoids any interrupt race
   * conditions later on in case we enable some other interrupt.
   */
  aic_outb(p, CLRSEQINT, CLRINT);
  switch (intstat & SEQINT_MASK)
  {
    case NO_MATCH:
      {
        aic_outb(p, aic_inb(p, SCSISEQ) & (ENSELI|ENRSELI|ENAUTOATNP),
                 SCSISEQ);
        printk(WARN_LEAD "No active SCB for reconnecting target - Issuing "
               "BUS DEVICE RESET.\n", p->host_no, channel, target, lun);
        printk(WARN_LEAD "      SAVED_TCL=0x%x, ARG_1=0x%x, SEQADDR=0x%x\n",
               p->host_no, channel, target, lun,
               aic_inb(p, SAVED_TCL), aic_inb(p, ARG_1),
               (aic_inb(p, SEQADDR1) << 8) | aic_inb(p, SEQADDR0));
        if (aic7xxx_panic_on_abort)
          aic7xxx_panic_abort(p, NULL);
      }
      break;

    case SEND_REJECT:
      {
        if (aic7xxx_verbose & VERBOSE_MINOR_ERROR)
          printk(INFO_LEAD "Rejecting unknown message (0x%x) received from "
            "target, SEQ_FLAGS=0x%x\n", p->host_no, channel, target, lun,
            aic_inb(p, ACCUM), aic_inb(p, SEQ_FLAGS));
      }
      break;

    case NO_IDENT:
      {
        /*
         * The reconnecting target either did not send an identify
         * message, or did, but we didn't find an SCB to match and
         * before it could respond to our ATN/abort, it hit a dataphase.
         * The only safe thing to do is to blow it away with a bus
         * reset.
         */
        if (aic7xxx_verbose & (VERBOSE_SEQINT | VERBOSE_RESET_MID))
          printk(INFO_LEAD "Target did not send an IDENTIFY message; "
            "LASTPHASE 0x%x, SAVED_TCL 0x%x\n", p->host_no, channel, target,
            lun, aic_inb(p, LASTPHASE), aic_inb(p, SAVED_TCL));

        aic7xxx_reset_channel(p, channel, /*initiate reset*/ TRUE);
        aic7xxx_run_done_queue(p, TRUE);

      }
      break;

    case BAD_PHASE:
      if (aic_inb(p, LASTPHASE) == P_BUSFREE)
      {
        if (aic7xxx_verbose & VERBOSE_SEQINT)
          printk(INFO_LEAD "Missed busfree.\n", p->host_no, channel,
            target, lun);
        restart_sequencer(p);
      }
      else
      {
        if (aic7xxx_verbose & VERBOSE_SEQINT)
          printk(INFO_LEAD "Unknown scsi bus phase, continuing\n", p->host_no,
            channel, target, lun);
      }
      break;

    case EXTENDED_MSG:
      {
        p->msg_type = MSG_TYPE_INITIATOR_MSGIN;
        p->msg_len = 0;
        p->msg_index = 0;

#ifdef AIC7XXX_VERBOSE_DEBUGGING
        if (aic7xxx_verbose > 0xffff)
          printk(INFO_LEAD "Enabling REQINITs for MSG_IN\n", p->host_no,
                 channel, target, lun);
#endif

       /*      
        * To actually receive the message, simply turn on
        * REQINIT interrupts and let our interrupt handler
        * do the rest (REQINIT should already be true).
        */
        p->flags |= AHC_HANDLING_REQINITS;
        aic_outb(p, aic_inb(p, SIMODE1) | ENREQINIT, SIMODE1);

       /*
        * We don't want the sequencer unpaused yet so we return early
        */
        return;
      }

    case REJECT_MSG:
      {
        /*
         * What we care about here is if we had an outstanding SDTR
         * or WDTR message for this target. If we did, this is a
         * signal that the target is refusing negotiation.
         */
        unsigned char scb_index;
        unsigned char last_msg;

        scb_index = aic_inb(p, SCB_TAG);
        scb = p->scb_data->scb_array[scb_index];
	aic_dev = AIC_DEV(scb->cmd);
        last_msg = aic_inb(p, LAST_MSG);

        if ( (last_msg == MSG_IDENTIFYFLAG) &&
             (scb->tag_action) &&
            !(scb->flags & SCB_MSGOUT_BITS) )
        {
          if (scb->tag_action == MSG_ORDERED_Q_TAG)
          {
            /*
             * OK...the device seems able to accept tagged commands, but
             * not ordered tag commands, only simple tag commands.  So, we
             * disable ordered tag commands and go on with life just like
             * normal.
             */
	    scsi_adjust_queue_depth(scb->cmd->device, MSG_SIMPLE_TAG,
			    scb->cmd->device->queue_depth);
            scb->tag_action = MSG_SIMPLE_Q_TAG;
            scb->hscb->control &= ~SCB_TAG_TYPE;
            scb->hscb->control |= MSG_SIMPLE_Q_TAG;
            aic_outb(p, scb->hscb->control, SCB_CONTROL);
            /*
             * OK..we set the tag type to simple tag command, now we re-assert
             * ATNO and hope this will take us into the identify phase again
             * so we can resend the tag type and info to the device.
             */
            aic_outb(p, MSG_IDENTIFYFLAG, MSG_OUT);
            aic_outb(p, aic_inb(p, SCSISIGI) | ATNO, SCSISIGO);
          }
          else if (scb->tag_action == MSG_SIMPLE_Q_TAG)
          {
            unsigned char i;
            struct aic7xxx_scb *scbp;
            int old_verbose;
            /*
             * Hmmmm....the device is flaking out on tagged commands.
             */
	    scsi_adjust_queue_depth(scb->cmd->device, 0 /* untagged */,
			    p->host->cmd_per_lun);
            aic_dev->max_q_depth = aic_dev->temp_q_depth = 1;
            /*
             * We set this command up as a bus device reset.  However, we have
             * to clear the tag type as it's causing us problems.  We shouldn't
             * have to worry about any other commands being active, since if
             * the device is refusing tagged commands, this should be the
             * first tagged command sent to the device, however, we do have
             * to worry about any other tagged commands that may already be
             * in the qinfifo.  The easiest way to do this, is to issue a BDR,
             * send all the commands back to the mid level code, then let them
             * come back and get rebuilt as untagged commands.
             */
            scb->tag_action = 0;
            scb->hscb->control &= ~(TAG_ENB | SCB_TAG_TYPE);
            aic_outb(p,  scb->hscb->control, SCB_CONTROL);

            old_verbose = aic7xxx_verbose;
            aic7xxx_verbose &= ~(VERBOSE_RESET|VERBOSE_ABORT);
            for (i=0; i < p->scb_data->numscbs; i++)
            {
              scbp = p->scb_data->scb_array[i];
              if ((scbp->flags & SCB_ACTIVE) && (scbp != scb))
              {
                if (aic7xxx_match_scb(p, scbp, target, channel, lun, i))
                {
                  aic7xxx_reset_device(p, target, channel, lun, i);
                }
              }
            }
            aic7xxx_run_done_queue(p, TRUE);
            aic7xxx_verbose = old_verbose;
            /*
             * Wait until after the for loop to set the busy index since
             * aic7xxx_reset_device will clear the busy index during its
             * operation.
             */
            aic7xxx_busy_target(p, scb);
            printk(INFO_LEAD "Device is refusing tagged commands, using "
              "untagged I/O.\n", p->host_no, channel, target, lun);
            aic_outb(p, MSG_IDENTIFYFLAG, MSG_OUT);
            aic_outb(p, aic_inb(p, SCSISIGI) | ATNO, SCSISIGO);
          }
        }
        else if (scb->flags & SCB_MSGOUT_PPR)
        {
          /*
           * As per the draft specs, any device capable of supporting any of
           * the option values other than 0 are not allowed to reject the
           * PPR message.  Instead, they must negotiate out what they do
           * support instead of rejecting our offering or else they cause
           * a parity error during msg_out phase to signal that they don't
           * like our settings.
           */
          aic_dev->needppr = aic_dev->needppr_copy = 0;
          aic7xxx_set_width(p, target, channel, lun, MSG_EXT_WDTR_BUS_8_BIT,
            (AHC_TRANS_ACTIVE|AHC_TRANS_CUR|AHC_TRANS_QUITE), aic_dev);
          aic7xxx_set_syncrate(p, NULL, target, channel, 0, 0, 0,
                               AHC_TRANS_ACTIVE|AHC_TRANS_CUR|AHC_TRANS_QUITE,
			       aic_dev);
          aic_dev->goal.options = aic_dev->dtr_pending = 0;
          scb->flags &= ~SCB_MSGOUT_BITS;
          if(aic7xxx_verbose & VERBOSE_NEGOTIATION2)
          {
            printk(INFO_LEAD "Device is rejecting PPR messages, falling "
              "back.\n", p->host_no, channel, target, lun);
          }
          if ( aic_dev->goal.width )
          {
            aic_dev->needwdtr = aic_dev->needwdtr_copy = 1;
            aic_dev->dtr_pending = 1;
            scb->flags |= SCB_MSGOUT_WDTR;
          }
          if ( aic_dev->goal.offset )
          {
            aic_dev->needsdtr = aic_dev->needsdtr_copy = 1;
            if( !aic_dev->dtr_pending )
            {
              aic_dev->dtr_pending = 1;
              scb->flags |= SCB_MSGOUT_SDTR;
            }
          }
          if ( aic_dev->dtr_pending )
          {
            aic_outb(p, HOST_MSG, MSG_OUT);
            aic_outb(p, aic_inb(p, SCSISIGI) | ATNO, SCSISIGO);
          }
        }
        else if (scb->flags & SCB_MSGOUT_WDTR)
        {
          /*
           * note 8bit xfers and clear flag
           */
          aic_dev->needwdtr = aic_dev->needwdtr_copy = 0;
          scb->flags &= ~SCB_MSGOUT_BITS;
          aic7xxx_set_width(p, target, channel, lun, MSG_EXT_WDTR_BUS_8_BIT,
            (AHC_TRANS_ACTIVE|AHC_TRANS_GOAL|AHC_TRANS_CUR), aic_dev);
          aic7xxx_set_syncrate(p, NULL, target, channel, 0, 0, 0,
                               AHC_TRANS_ACTIVE|AHC_TRANS_CUR|AHC_TRANS_QUITE,
			       aic_dev);
          if(aic7xxx_verbose & VERBOSE_NEGOTIATION2)
          {
            printk(INFO_LEAD "Device is rejecting WDTR messages, using "
              "narrow transfers.\n", p->host_no, channel, target, lun);
          }
          aic_dev->needsdtr = aic_dev->needsdtr_copy;
        }
        else if (scb->flags & SCB_MSGOUT_SDTR)
        {
         /*
          * note asynch xfers and clear flag
          */
          aic_dev->needsdtr = aic_dev->needsdtr_copy = 0;
          scb->flags &= ~SCB_MSGOUT_BITS;
          aic7xxx_set_syncrate(p, NULL, target, channel, 0, 0, 0,
            (AHC_TRANS_CUR|AHC_TRANS_ACTIVE|AHC_TRANS_GOAL), aic_dev);
          if(aic7xxx_verbose & VERBOSE_NEGOTIATION2)
          {
            printk(INFO_LEAD "Device is rejecting SDTR messages, using "
              "async transfers.\n", p->host_no, channel, target, lun);
          }
        }
        else if (aic7xxx_verbose & VERBOSE_SEQINT)
        {
          /*
           * Otherwise, we ignore it.
           */
          printk(INFO_LEAD "Received MESSAGE_REJECT for unknown cause.  "
            "Ignoring.\n", p->host_no, channel, target, lun);
        }
      }
      break;

    case BAD_STATUS:
      {
	unsigned char scb_index;
	struct aic7xxx_hwscb *hscb;
	struct scsi_cmnd *cmd;

	/* The sequencer will notify us when a command has an error that
	 * would be of interest to the kernel.  This allows us to leave
	 * the sequencer running in the common case of command completes
	 * without error.  The sequencer will have DMA'd the SCB back
	 * up to us, so we can reference the drivers SCB array.
	 *
	 * Set the default return value to 0 indicating not to send
	 * sense.  The sense code will change this if needed and this
	 * reduces code duplication.
	 */
        aic_outb(p, 0, RETURN_1);
        scb_index = aic_inb(p, SCB_TAG);
        if (scb_index > p->scb_data->numscbs)
        {
          printk(WARN_LEAD "Invalid SCB during SEQINT 0x%02x, SCB_TAG %d.\n",
            p->host_no, channel, target, lun, intstat, scb_index);
          break;
        }
        scb = p->scb_data->scb_array[scb_index];
        hscb = scb->hscb;

        if (!(scb->flags & SCB_ACTIVE) || (scb->cmd == NULL))
        {
          printk(WARN_LEAD "Invalid SCB during SEQINT 0x%x, scb %d, flags 0x%x,"
            " cmd 0x%lx.\n", p->host_no, channel, target, lun, intstat,
            scb_index, scb->flags, (unsigned long) scb->cmd);
        }
        else
        {
          cmd = scb->cmd;
	  aic_dev = AIC_DEV(scb->cmd);
          hscb->target_status = aic_inb(p, SCB_TARGET_STATUS);
          aic7xxx_status(cmd) = hscb->target_status;

          cmd->result = hscb->target_status;

          switch (status_byte(hscb->target_status))
          {
            case GOOD:
              if (aic7xxx_verbose & VERBOSE_SEQINT)
                printk(INFO_LEAD "Interrupted for status of GOOD???\n",
                  p->host_no, CTL_OF_SCB(scb));
              break;

            case COMMAND_TERMINATED:
            case CHECK_CONDITION:
              if ( !(scb->flags & SCB_SENSE) )
              {
                /*
                 * Send a sense command to the requesting target.
                 * XXX - revisit this and get rid of the memcopys.
                 */
                memcpy(scb->sense_cmd, &generic_sense[0],
                       sizeof(generic_sense));

                scb->sense_cmd[1] = (cmd->device->lun << 5);
                scb->sense_cmd[4] = SCSI_SENSE_BUFFERSIZE;

                scb->sg_list[0].length = 
                  cpu_to_le32(SCSI_SENSE_BUFFERSIZE);
		scb->sg_list[0].address =
                        cpu_to_le32(pci_map_single(p->pdev, cmd->sense_buffer,
                                                   SCSI_SENSE_BUFFERSIZE,
                                                   PCI_DMA_FROMDEVICE));

                /*
                 * XXX - We should allow disconnection, but can't as it
                 * might allow overlapped tagged commands.
                 */
                /* hscb->control &= DISCENB; */
                hscb->control = 0;
                hscb->target_status = 0;
                hscb->SG_list_pointer = 
		  cpu_to_le32(SCB_DMA_ADDR(scb, scb->sg_list));
                hscb->SCSI_cmd_pointer = 
                  cpu_to_le32(SCB_DMA_ADDR(scb, scb->sense_cmd));
                hscb->data_count = scb->sg_list[0].length;
                hscb->data_pointer = scb->sg_list[0].address;
                hscb->SCSI_cmd_length = COMMAND_SIZE(scb->sense_cmd[0]);
                hscb->residual_SG_segment_count = 0;
                hscb->residual_data_count[0] = 0;
                hscb->residual_data_count[1] = 0;
                hscb->residual_data_count[2] = 0;

                scb->sg_count = hscb->SG_segment_count = 1;
                scb->sg_length = SCSI_SENSE_BUFFERSIZE;
                scb->tag_action = 0;
                scb->flags |= SCB_SENSE;
                /*
                 * Ensure the target is busy since this will be an
                 * an untagged request.
                 */
#ifdef AIC7XXX_VERBOSE_DEBUGGING
                if (aic7xxx_verbose & VERBOSE_NEGOTIATION2)
                {
                  if (scb->flags & SCB_MSGOUT_BITS)
                    printk(INFO_LEAD "Requesting SENSE with %s\n", p->host_no,
                           CTL_OF_SCB(scb), (scb->flags & SCB_MSGOUT_SDTR) ?
                           "SDTR" : "WDTR");
                  else
                    printk(INFO_LEAD "Requesting SENSE, no MSG\n", p->host_no,
                           CTL_OF_SCB(scb));
                }
#endif
                aic7xxx_busy_target(p, scb);
                aic_outb(p, SEND_SENSE, RETURN_1);
                aic7xxx_error(cmd) = DID_OK;
                break;
              }  /* first time sense, no errors */
              printk(INFO_LEAD "CHECK_CONDITION on REQUEST_SENSE, returning "
                     "an error.\n", p->host_no, CTL_OF_SCB(scb));
              aic7xxx_error(cmd) = DID_ERROR;
              scb->flags &= ~SCB_SENSE;
              break;

            case QUEUE_FULL:
              queue_flag = TRUE;    /* Mark that this is a QUEUE_FULL and */
            case BUSY:              /* drop through to here */
            {
              struct aic7xxx_scb *next_scbp, *prev_scbp;
              unsigned char active_hscb, next_hscb, prev_hscb, scb_index;
              /*
               * We have to look three places for queued commands:
               *  1: p->waiting_scbs queue
               *  2: QINFIFO
               *  3: WAITING_SCBS list on card (for commands that are started
               *     but haven't yet made it to the device)
	       *
	       * Of special note here is that commands on 2 or 3 above will
	       * have already been marked as active, while commands on 1 will
	       * not.  The aic7xxx_done() function will want to unmark them
	       * from active, so any commands we pull off of 1 need to
	       * up the active count.
               */
              next_scbp = p->waiting_scbs.head;
              while ( next_scbp != NULL )
              {
                prev_scbp = next_scbp;
                next_scbp = next_scbp->q_next;
                if ( aic7xxx_match_scb(p, prev_scbp, target, channel, lun,
                     SCB_LIST_NULL) )
                {
                  scbq_remove(&p->waiting_scbs, prev_scbp);
		  scb->flags = SCB_QUEUED_FOR_DONE | SCB_QUEUE_FULL;
		  p->activescbs++;
		  aic_dev->active_cmds++;
                }
              }
              aic7xxx_search_qinfifo(p, target, channel, lun,
                SCB_LIST_NULL, SCB_QUEUED_FOR_DONE | SCB_QUEUE_FULL,
	       	FALSE, NULL);
              next_scbp = NULL;
              active_hscb = aic_inb(p, SCBPTR);
              prev_hscb = next_hscb = scb_index = SCB_LIST_NULL;
              next_hscb = aic_inb(p, WAITING_SCBH);
              while (next_hscb != SCB_LIST_NULL)
              {
                aic_outb(p, next_hscb, SCBPTR);
                scb_index = aic_inb(p, SCB_TAG);
                if (scb_index < p->scb_data->numscbs)
                {
                  next_scbp = p->scb_data->scb_array[scb_index];
                  if (aic7xxx_match_scb(p, next_scbp, target, channel, lun,
                      SCB_LIST_NULL) )
                  {
		    next_scbp->flags = SCB_QUEUED_FOR_DONE | SCB_QUEUE_FULL;
                    next_hscb = aic_inb(p, SCB_NEXT);
                    aic_outb(p, 0, SCB_CONTROL);
                    aic_outb(p, SCB_LIST_NULL, SCB_TAG);
                    aic7xxx_add_curscb_to_free_list(p);
                    if (prev_hscb == SCB_LIST_NULL)
                    {
                      /* We were first on the list,
                       * so we kill the selection
                       * hardware.  Let the sequencer
                       * re-init the hardware itself
                       */
                      aic_outb(p, aic_inb(p, SCSISEQ) & ~ENSELO, SCSISEQ);
                      aic_outb(p, CLRSELTIMEO, CLRSINT1);
                      aic_outb(p, next_hscb, WAITING_SCBH);
                    }
                    else
                    {
                      aic_outb(p, prev_hscb, SCBPTR);
                      aic_outb(p, next_hscb, SCB_NEXT);
                    }
                  }
                  else
                  {
                    prev_hscb = next_hscb;
                    next_hscb = aic_inb(p, SCB_NEXT);
                  }
                } /* scb_index >= p->scb_data->numscbs */
              }
              aic_outb(p, active_hscb, SCBPTR);
	      aic7xxx_run_done_queue(p, FALSE);
                  
#ifdef AIC7XXX_VERBOSE_DEBUGGING
              if( (aic7xxx_verbose & VERBOSE_MINOR_ERROR) ||
                  (aic7xxx_verbose > 0xffff) )
              {
                if (queue_flag)
                  printk(INFO_LEAD "Queue full received; queue depth %d, "
                    "active %d\n", p->host_no, CTL_OF_SCB(scb),
                    aic_dev->max_q_depth, aic_dev->active_cmds);
                else
                  printk(INFO_LEAD "Target busy\n", p->host_no, CTL_OF_SCB(scb));
              }
#endif
              if (queue_flag)
              {
		int diff;
		result = scsi_track_queue_full(cmd->device,
			       	aic_dev->active_cmds);
		if ( result < 0 )
		{
                  if (aic7xxx_verbose & VERBOSE_NEGOTIATION2)
                    printk(INFO_LEAD "Tagged Command Queueing disabled.\n",
			p->host_no, CTL_OF_SCB(scb));
		  diff = aic_dev->max_q_depth - p->host->cmd_per_lun;
		  aic_dev->temp_q_depth = 1;
		  aic_dev->max_q_depth = 1;
		}
		else if ( result > 0 )
		{
                  if (aic7xxx_verbose & VERBOSE_NEGOTIATION2)
                    printk(INFO_LEAD "Queue depth reduced to %d\n", p->host_no,
                      CTL_OF_SCB(scb), result);
		  diff = aic_dev->max_q_depth - result;
		  aic_dev->max_q_depth = result;
		  /* temp_q_depth could have been dropped to 1 for an untagged
		   * command that might be coming up */
		  if(aic_dev->temp_q_depth > result)
		    aic_dev->temp_q_depth = result;
		}
		/* We should free up the no unused SCB entries.  But, that's
		 * a difficult thing to do because we use a direct indexed
		 * array, so we can't just take any entries and free them,
		 * we *have* to free the ones at the end of the array, and
		 * they very well could be in use right now, which means
		 * in order to do this right, we have to add a delayed
		 * freeing mechanism tied into the scb_free() code area.
		 * We'll add that later.
		 */
	      }
              break;
            }
            
            default:
              if (aic7xxx_verbose & VERBOSE_SEQINT)
                printk(INFO_LEAD "Unexpected target status 0x%x.\n", p->host_no,
                     CTL_OF_SCB(scb), scb->hscb->target_status);
              if (!aic7xxx_error(cmd))
              {
                aic7xxx_error(cmd) = DID_RETRY_COMMAND;
              }
              break;
          }  /* end switch */
        }  /* end else of */
      }
      break;

    case AWAITING_MSG:
      {
        unsigned char scb_index, msg_out;

        scb_index = aic_inb(p, SCB_TAG);
        msg_out = aic_inb(p, MSG_OUT);
        scb = p->scb_data->scb_array[scb_index];
	aic_dev = AIC_DEV(scb->cmd);
        p->msg_index = p->msg_len = 0;
        /*
         * This SCB had a MK_MESSAGE set in its control byte informing
         * the sequencer that we wanted to send a special message to
         * this target.
         */

        if ( !(scb->flags & SCB_DEVICE_RESET) &&
              (msg_out == MSG_IDENTIFYFLAG) &&
              (scb->hscb->control & TAG_ENB) )
        {
          p->msg_buf[p->msg_index++] = scb->tag_action;
          p->msg_buf[p->msg_index++] = scb->hscb->tag;
          p->msg_len += 2;
        }

        if (scb->flags & SCB_DEVICE_RESET)
        {
          p->msg_buf[p->msg_index++] = MSG_BUS_DEV_RESET;
          p->msg_len++;
          if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
            printk(INFO_LEAD "Bus device reset mailed.\n",
                 p->host_no, CTL_OF_SCB(scb));
        }
        else if (scb->flags & SCB_ABORT)
        {
          if (scb->tag_action)
          {
            p->msg_buf[p->msg_index++] = MSG_ABORT_TAG;
          }
          else
          {
            p->msg_buf[p->msg_index++] = MSG_ABORT;
          }
          p->msg_len++;
          if (aic7xxx_verbose & VERBOSE_ABORT_PROCESS)
            printk(INFO_LEAD "Abort message mailed.\n", p->host_no,
              CTL_OF_SCB(scb));
        }
        else if (scb->flags & SCB_MSGOUT_PPR)
        {
          if (aic7xxx_verbose & VERBOSE_NEGOTIATION2)
          {
            printk(INFO_LEAD "Sending PPR (%d/%d/%d/%d) message.\n",
                   p->host_no, CTL_OF_SCB(scb),
                   aic_dev->goal.period,
                   aic_dev->goal.offset,
                   aic_dev->goal.width,
                   aic_dev->goal.options);
          }
          aic7xxx_construct_ppr(p, scb);
        }
        else if (scb->flags & SCB_MSGOUT_WDTR)
        {
          if (aic7xxx_verbose & VERBOSE_NEGOTIATION2)
          {
            printk(INFO_LEAD "Sending WDTR message.\n", p->host_no,
                   CTL_OF_SCB(scb));
          }
          aic7xxx_construct_wdtr(p, aic_dev->goal.width);
        }
        else if (scb->flags & SCB_MSGOUT_SDTR)
        {
          unsigned int max_sync, period;
          unsigned char options = 0;
          /*
           * Now that the device is selected, use the bits in SBLKCTL and
           * SSTAT2 to determine the max sync rate for this device.
           */
          if (p->features & AHC_ULTRA2)
          {
            if ( (aic_inb(p, SBLKCTL) & ENAB40) &&
                !(aic_inb(p, SSTAT2) & EXP_ACTIVE) )
            {
              max_sync = AHC_SYNCRATE_ULTRA2;
            }
            else
            {
              max_sync = AHC_SYNCRATE_ULTRA;
            }
          }
          else if (p->features & AHC_ULTRA)
          {
            max_sync = AHC_SYNCRATE_ULTRA;
          }
          else
          {
            max_sync = AHC_SYNCRATE_FAST;
          }
          period = aic_dev->goal.period;
          aic7xxx_find_syncrate(p, &period, max_sync, &options);
          if (aic7xxx_verbose & VERBOSE_NEGOTIATION2)
          {
            printk(INFO_LEAD "Sending SDTR %d/%d message.\n", p->host_no,
                   CTL_OF_SCB(scb), period,
                   aic_dev->goal.offset);
          }
          aic7xxx_construct_sdtr(p, period, aic_dev->goal.offset);
        }
        else 
        {
          panic("aic7xxx: AWAITING_MSG for an SCB that does "
                "not have a waiting message.\n");
        }
        /*
         * We've set everything up to send our message, now to actually do
         * so we need to enable reqinit interrupts and let the interrupt
         * handler do the rest.  We don't want to unpause the sequencer yet
         * though so we'll return early.  We also have to make sure that
         * we clear the SEQINT *BEFORE* we set the REQINIT handler active
         * or else it's possible on VLB cards to lose the first REQINIT
         * interrupt.  Edge triggered EISA cards could also lose this
         * interrupt, although PCI and level triggered cards should not
         * have this problem since they continually interrupt the kernel
         * until we take care of the situation.
         */
        scb->flags |= SCB_MSGOUT_SENT;
        p->msg_index = 0;
        p->msg_type = MSG_TYPE_INITIATOR_MSGOUT;
        p->flags |= AHC_HANDLING_REQINITS;
        aic_outb(p, aic_inb(p, SIMODE1) | ENREQINIT, SIMODE1);
        return;
      }
      break;

    case DATA_OVERRUN:
      {
        unsigned char scb_index = aic_inb(p, SCB_TAG);
        unsigned char lastphase = aic_inb(p, LASTPHASE);
        unsigned int i;

        scb = (p->scb_data->scb_array[scb_index]);
        /*
         * XXX - What do we really want to do on an overrun?  The
         *       mid-level SCSI code should handle this, but for now,
         *       we'll just indicate that the command should retried.
         *    If we retrieved sense info on this target, then the 
         *    base SENSE info should have been saved prior to the
         *    overrun error.  In that case, we return DID_OK and let
         *    the mid level code pick up on the sense info.  Otherwise
         *    we return DID_ERROR so the command will get retried.
         */
        if ( !(scb->flags & SCB_SENSE) )
        {
          printk(WARN_LEAD "Data overrun detected in %s phase, tag %d;\n",
            p->host_no, CTL_OF_SCB(scb), 
            (lastphase == P_DATAIN) ? "Data-In" : "Data-Out", scb->hscb->tag);
          printk(KERN_WARNING "  %s seen Data Phase. Length=%d, NumSGs=%d.\n",
            (aic_inb(p, SEQ_FLAGS) & DPHASE) ? "Have" : "Haven't",
            scb->sg_length, scb->sg_count);
          printk(KERN_WARNING "  Raw SCSI Command: 0x");
          for (i = 0; i < scb->hscb->SCSI_cmd_length; i++)
          {
            printk("%02x ", scb->cmd->cmnd[i]);
          }
          printk("\n");
          if(aic7xxx_verbose > 0xffff)
          {
            for (i = 0; i < scb->sg_count; i++)
            {
              printk(KERN_WARNING "     sg[%d] - Addr 0x%x : Length %d\n",
                 i, 
                 le32_to_cpu(scb->sg_list[i].address),
                 le32_to_cpu(scb->sg_list[i].length) );
            }
          }
          aic7xxx_error(scb->cmd) = DID_ERROR;
        }
        else
          printk(INFO_LEAD "Data Overrun during SEND_SENSE operation.\n",
            p->host_no, CTL_OF_SCB(scb));
      }
      break;

    case WIDE_RESIDUE:
      {
        unsigned char resid_sgcnt, index;
        unsigned char scb_index = aic_inb(p, SCB_TAG);
        unsigned int cur_addr, resid_dcnt;
        unsigned int native_addr, native_length, sg_addr;
        int i;

        if(scb_index > p->scb_data->numscbs)
        {
          printk(WARN_LEAD "invalid scb_index during WIDE_RESIDUE.\n",
            p->host_no, -1, -1, -1);
          /*
           * XXX: Add error handling here
           */
          break;
        }
        scb = p->scb_data->scb_array[scb_index];
        if(!(scb->flags & SCB_ACTIVE) || (scb->cmd == NULL))
        {
          printk(WARN_LEAD "invalid scb during WIDE_RESIDUE flags:0x%x "
                 "scb->cmd:0x%lx\n", p->host_no, CTL_OF_SCB(scb),
                 scb->flags, (unsigned long)scb->cmd);
          break;
        }
        if(aic7xxx_verbose & VERBOSE_MINOR_ERROR)
          printk(INFO_LEAD "Got WIDE_RESIDUE message, patching up data "
                 "pointer.\n", p->host_no, CTL_OF_SCB(scb));

        /*
         * We have a valid scb to use on this WIDE_RESIDUE message, so
         * we need to walk the sg list looking for this particular sg
         * segment, then see if we happen to be at the very beginning of
         * the segment.  If we are, then we have to back things up to
         * the previous segment.  If not, then we simply need to remove
         * one byte from this segments address and add one to the byte
         * count.
         */
        cur_addr = aic_inb(p, SHADDR) | (aic_inb(p, SHADDR + 1) << 8) |
          (aic_inb(p, SHADDR + 2) << 16) | (aic_inb(p, SHADDR + 3) << 24);
        sg_addr = aic_inb(p, SG_COUNT + 1) | (aic_inb(p, SG_COUNT + 2) << 8) |
          (aic_inb(p, SG_COUNT + 3) << 16) | (aic_inb(p, SG_COUNT + 4) << 24);
        resid_sgcnt = aic_inb(p, SCB_RESID_SGCNT);
        resid_dcnt = aic_inb(p, SCB_RESID_DCNT) |
          (aic_inb(p, SCB_RESID_DCNT + 1) << 8) |
          (aic_inb(p, SCB_RESID_DCNT + 2) << 16);
        index = scb->sg_count - ((resid_sgcnt) ? resid_sgcnt : 1);
        native_addr = le32_to_cpu(scb->sg_list[index].address);
        native_length = le32_to_cpu(scb->sg_list[index].length);
        /*
         * If resid_dcnt == native_length, then we just loaded this SG
         * segment and we need to back it up one...
         */
        if(resid_dcnt == native_length)
        {
          if(index == 0)
          {
            /*
             * Oops, this isn't right, we can't back up to before the
             * beginning.  This must be a bogus message, ignore it.
             */
            break;
          }
          resid_dcnt = 1;
          resid_sgcnt += 1;
          native_addr = le32_to_cpu(scb->sg_list[index - 1].address);
          native_length = le32_to_cpu(scb->sg_list[index - 1].length);
          cur_addr = native_addr + (native_length - 1);
          sg_addr -= sizeof(struct hw_scatterlist);
        }
        else
        {
          /*
           * resid_dcnt != native_length, so we are in the middle of a SG
           * element.  Back it up one byte and leave the rest alone.
           */
          resid_dcnt += 1;
          cur_addr -= 1;
        }
        
        /*
         * Output the new addresses and counts to the right places on the
         * card.
         */
        aic_outb(p, resid_sgcnt, SG_COUNT);
        aic_outb(p, resid_sgcnt, SCB_RESID_SGCNT);
        aic_outb(p, sg_addr & 0xff, SG_COUNT + 1);
        aic_outb(p, (sg_addr >> 8) & 0xff, SG_COUNT + 2);
        aic_outb(p, (sg_addr >> 16) & 0xff, SG_COUNT + 3);
        aic_outb(p, (sg_addr >> 24) & 0xff, SG_COUNT + 4);
        aic_outb(p, resid_dcnt & 0xff, SCB_RESID_DCNT);
        aic_outb(p, (resid_dcnt >> 8) & 0xff, SCB_RESID_DCNT + 1);
        aic_outb(p, (resid_dcnt >> 16) & 0xff, SCB_RESID_DCNT + 2);

        /*
         * The sequencer actually wants to find the new address
         * in the SHADDR register set.  On the Ultra2 and later controllers
         * this register set is readonly.  In order to get the right number
         * into the register, you actually have to enter it in HADDR and then
         * use the PRELOADEN bit of DFCNTRL to drop it through from the
         * HADDR register to the SHADDR register.  On non-Ultra2 controllers,
         * we simply write it direct.
         */
        if(p->features & AHC_ULTRA2)
        {
          /*
           * We might as well be accurate and drop both the resid_dcnt and
           * cur_addr into HCNT and HADDR and have both of them drop
           * through to the shadow layer together.
           */
          aic_outb(p, resid_dcnt & 0xff, HCNT);
          aic_outb(p, (resid_dcnt >> 8) & 0xff, HCNT + 1);
          aic_outb(p, (resid_dcnt >> 16) & 0xff, HCNT + 2);
          aic_outb(p, cur_addr & 0xff, HADDR);
          aic_outb(p, (cur_addr >> 8) & 0xff, HADDR + 1);
          aic_outb(p, (cur_addr >> 16) & 0xff, HADDR + 2);
          aic_outb(p, (cur_addr >> 24) & 0xff, HADDR + 3);
          aic_outb(p, aic_inb(p, DMAPARAMS) | PRELOADEN, DFCNTRL);
          udelay(1);
          aic_outb(p, aic_inb(p, DMAPARAMS) & ~(SCSIEN|HDMAEN), DFCNTRL);
          i=0;
          while(((aic_inb(p, DFCNTRL) & (SCSIEN|HDMAEN)) != 0) && (i++ < 1000))
          {
            udelay(1);
          }
        }
        else
        {
          aic_outb(p, cur_addr & 0xff, SHADDR);
          aic_outb(p, (cur_addr >> 8) & 0xff, SHADDR + 1);
          aic_outb(p, (cur_addr >> 16) & 0xff, SHADDR + 2);
          aic_outb(p, (cur_addr >> 24) & 0xff, SHADDR + 3);
        }
      }
      break;

    case SEQ_SG_FIXUP:
    {
      unsigned char scb_index, tmp;
      int sg_addr, sg_length;

      scb_index = aic_inb(p, SCB_TAG);

      if(scb_index > p->scb_data->numscbs)
      {
        printk(WARN_LEAD "invalid scb_index during SEQ_SG_FIXUP.\n",
          p->host_no, -1, -1, -1);
        printk(INFO_LEAD "SCSISIGI 0x%x, SEQADDR 0x%x, SSTAT0 0x%x, SSTAT1 "
           "0x%x\n", p->host_no, -1, -1, -1,
           aic_inb(p, SCSISIGI),
           aic_inb(p, SEQADDR0) | (aic_inb(p, SEQADDR1) << 8),
           aic_inb(p, SSTAT0), aic_inb(p, SSTAT1));
        printk(INFO_LEAD "SG_CACHEPTR 0x%x, SSTAT2 0x%x, STCNT 0x%x\n",
           p->host_no, -1, -1, -1, aic_inb(p, SG_CACHEPTR),
           aic_inb(p, SSTAT2), aic_inb(p, STCNT + 2) << 16 |
           aic_inb(p, STCNT + 1) << 8 | aic_inb(p, STCNT));
        /*
         * XXX: Add error handling here
         */
        break;
      }
      scb = p->scb_data->scb_array[scb_index];
      if(!(scb->flags & SCB_ACTIVE) || (scb->cmd == NULL))
      {
        printk(WARN_LEAD "invalid scb during SEQ_SG_FIXUP flags:0x%x "
               "scb->cmd:0x%p\n", p->host_no, CTL_OF_SCB(scb),
               scb->flags, scb->cmd);
        printk(INFO_LEAD "SCSISIGI 0x%x, SEQADDR 0x%x, SSTAT0 0x%x, SSTAT1 "
           "0x%x\n", p->host_no, CTL_OF_SCB(scb),
           aic_inb(p, SCSISIGI),
           aic_inb(p, SEQADDR0) | (aic_inb(p, SEQADDR1) << 8),
           aic_inb(p, SSTAT0), aic_inb(p, SSTAT1));
        printk(INFO_LEAD "SG_CACHEPTR 0x%x, SSTAT2 0x%x, STCNT 0x%x\n",
           p->host_no, CTL_OF_SCB(scb), aic_inb(p, SG_CACHEPTR),
           aic_inb(p, SSTAT2), aic_inb(p, STCNT + 2) << 16 |
           aic_inb(p, STCNT + 1) << 8 | aic_inb(p, STCNT));
        break;
      }
      if(aic7xxx_verbose & VERBOSE_MINOR_ERROR)
        printk(INFO_LEAD "Fixing up SG address for sequencer.\n", p->host_no,
               CTL_OF_SCB(scb));
      /*
       * Advance the SG pointer to the next element in the list
       */
      tmp = aic_inb(p, SG_NEXT);
      tmp += SG_SIZEOF;
      aic_outb(p, tmp, SG_NEXT);
      if( tmp < SG_SIZEOF )
        aic_outb(p, aic_inb(p, SG_NEXT + 1) + 1, SG_NEXT + 1);
      tmp = aic_inb(p, SG_COUNT) - 1;
      aic_outb(p, tmp, SG_COUNT);
      sg_addr = le32_to_cpu(scb->sg_list[scb->sg_count - tmp].address);
      sg_length = le32_to_cpu(scb->sg_list[scb->sg_count - tmp].length);
      /*
       * Now stuff the element we just advanced past down onto the
       * card so it can be stored in the residual area.
       */
      aic_outb(p, sg_addr & 0xff, HADDR);
      aic_outb(p, (sg_addr >> 8) & 0xff, HADDR + 1);
      aic_outb(p, (sg_addr >> 16) & 0xff, HADDR + 2);
      aic_outb(p, (sg_addr >> 24) & 0xff, HADDR + 3);
      aic_outb(p, sg_length & 0xff, HCNT);
      aic_outb(p, (sg_length >> 8) & 0xff, HCNT + 1);
      aic_outb(p, (sg_length >> 16) & 0xff, HCNT + 2);
      aic_outb(p, (tmp << 2) | ((tmp == 1) ? LAST_SEG : 0), SG_CACHEPTR);
      aic_outb(p, aic_inb(p, DMAPARAMS), DFCNTRL);
      while(aic_inb(p, SSTAT0) & SDONE) udelay(1);
      while(aic_inb(p, DFCNTRL) & (HDMAEN|SCSIEN)) aic_outb(p, 0, DFCNTRL);
    }
    break;

#ifdef AIC7XXX_NOT_YET 
    case TRACEPOINT2:
      {
        printk(INFO_LEAD "Tracepoint #2 reached.\n", p->host_no,
               channel, target, lun);
      }
      break;

    /* XXX Fill these in later */
    case MSG_BUFFER_BUSY:
      printk("aic7xxx: Message buffer busy.\n");
      break;
    case MSGIN_PHASEMIS:
      printk("aic7xxx: Message-in phasemis.\n");
      break;
#endif

    default:                   /* unknown */
      printk(WARN_LEAD "Unknown SEQINT, INTSTAT 0x%x, SCSISIGI 0x%x.\n",
             p->host_no, channel, target, lun, intstat,
             aic_inb(p, SCSISIGI));
      break;
  }

  /*
   * Clear the sequencer interrupt and unpause the sequencer.
   */
  unpause_sequencer(p, /* unpause always */ TRUE);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_parse_msg
 *
 * Description:
 *   Parses incoming messages into actions on behalf of
 *   aic7xxx_handle_reqinit
 *_F*************************************************************************/
static int
aic7xxx_parse_msg(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  int reject, reply, done;
  unsigned char target_scsirate, tindex;
  unsigned short target_mask;
  unsigned char target, channel, lun;
  unsigned char bus_width, new_bus_width;
  unsigned char trans_options, new_trans_options;
  unsigned int period, new_period, offset, new_offset, maxsync;
  struct aic7xxx_syncrate *syncrate;
  struct aic_dev_data *aic_dev;

  target = scb->cmd->device->id;
  channel = scb->cmd->device->channel;
  lun = scb->cmd->device->lun;
  reply = reject = done = FALSE;
  tindex = TARGET_INDEX(scb->cmd);
  aic_dev = AIC_DEV(scb->cmd);
  target_scsirate = aic_inb(p, TARG_SCSIRATE + tindex);
  target_mask = (0x01 << tindex);

  /*
   * Parse as much of the message as is available,
   * rejecting it if we don't support it.  When
   * the entire message is available and has been
   * handled, return TRUE indicating that we have
   * parsed an entire message.
   */

  if (p->msg_buf[0] != MSG_EXTENDED)
  {
    reject = TRUE;
  }

  /*
   * Even if we are an Ultra3 card, don't allow Ultra3 sync rates when
   * using the SDTR messages.  We need the PPR messages to enable the
   * higher speeds that include things like Dual Edge clocking.
   */
  if (p->features & AHC_ULTRA2)
  {
    if ( (aic_inb(p, SBLKCTL) & ENAB40) &&
         !(aic_inb(p, SSTAT2) & EXP_ACTIVE) )
    {
      if (p->features & AHC_ULTRA3)
        maxsync = AHC_SYNCRATE_ULTRA3;
      else
        maxsync = AHC_SYNCRATE_ULTRA2;
    }
    else
    {
      maxsync = AHC_SYNCRATE_ULTRA;
    }
  }
  else if (p->features & AHC_ULTRA)
  {
    maxsync = AHC_SYNCRATE_ULTRA;
  }
  else
  {
    maxsync = AHC_SYNCRATE_FAST;
  }

  /*
   * Just accept the length byte outright and perform
   * more checking once we know the message type.
   */

  if ( !reject && (p->msg_len > 2) )
  {
    switch(p->msg_buf[2])
    {
      case MSG_EXT_SDTR:
      {
        
        if (p->msg_buf[1] != MSG_EXT_SDTR_LEN)
        {
          reject = TRUE;
          break;
        }

        if (p->msg_len < (MSG_EXT_SDTR_LEN + 2))
        {
          break;
        }

        period = new_period = p->msg_buf[3];
        offset = new_offset = p->msg_buf[4];
        trans_options = new_trans_options = 0;
        bus_width = new_bus_width = target_scsirate & WIDEXFER;

        /*
         * If our current max syncrate is in the Ultra3 range, bump it back
         * down to Ultra2 since we can't negotiate DT transfers using SDTR
         */
        if(maxsync == AHC_SYNCRATE_ULTRA3)
          maxsync = AHC_SYNCRATE_ULTRA2;

        /*
         * We might have a device that is starting negotiation with us
         * before we can start up negotiation with it....be prepared to
         * have a device ask for a higher speed then we want to give it
         * in that case
         */
        if ( (scb->flags & (SCB_MSGOUT_SENT|SCB_MSGOUT_SDTR)) !=
             (SCB_MSGOUT_SENT|SCB_MSGOUT_SDTR) )
        {
          if (!(aic_dev->flags & DEVICE_DTR_SCANNED))
          {
            /*
             * We shouldn't get here unless this is a narrow drive, wide
             * devices should trigger this same section of code in the WDTR
             * handler first instead.
             */
            aic_dev->goal.width = MSG_EXT_WDTR_BUS_8_BIT;
            aic_dev->goal.options = 0;
            if(p->user[tindex].offset)
            {
              aic_dev->needsdtr_copy = 1;
              aic_dev->goal.period = max_t(unsigned char, 10,p->user[tindex].period);
              if(p->features & AHC_ULTRA2)
              {
                aic_dev->goal.offset = MAX_OFFSET_ULTRA2;
              }
              else
              {
                aic_dev->goal.offset = MAX_OFFSET_8BIT;
              }
            }
            else
            {
              aic_dev->needsdtr_copy = 0;
              aic_dev->goal.period = 255;
              aic_dev->goal.offset = 0;
            }
            aic_dev->flags |= DEVICE_DTR_SCANNED | DEVICE_PRINT_DTR;
          }
          else if (aic_dev->needsdtr_copy == 0)
          {
            /*
             * This is a preemptive message from the target, we've already
             * scanned this target and set our options for it, and we
             * don't need a SDTR with this target (for whatever reason),
             * so reject this incoming SDTR
             */
            reject = TRUE;
            break;
          }

          /* The device is sending this message first and we have to reply */
          reply = TRUE;
          
          if (aic7xxx_verbose & VERBOSE_NEGOTIATION2)
          {
            printk(INFO_LEAD "Received pre-emptive SDTR message from "
                   "target.\n", p->host_no, CTL_OF_SCB(scb));
          }
          /*
           * Validate the values the device passed to us against our SEEPROM
           * settings.  We don't have to do this if we aren't replying since
           * the device isn't allowed to send values greater than the ones
           * we first sent to it.
           */
          new_period = max_t(unsigned int, period, aic_dev->goal.period);
          new_offset = min_t(unsigned int, offset, aic_dev->goal.offset);
        }
 
        /*
         * Use our new_period, new_offset, bus_width, and card options
         * to determine the actual syncrate settings
         */
        syncrate = aic7xxx_find_syncrate(p, &new_period, maxsync,
                                         &trans_options);
        aic7xxx_validate_offset(p, syncrate, &new_offset, bus_width);

        /*
         * Did we drop to async?  If so, send a reply regardless of whether
         * or not we initiated this negotiation.
         */
        if ((new_offset == 0) && (new_offset != offset))
        {
          aic_dev->needsdtr_copy = 0;
          reply = TRUE;
        }
        
        /*
         * Did we start this, if not, or if we went too low and had to
         * go async, then send an SDTR back to the target
         */
        if(reply)
        {
          /* when sending a reply, make sure that the goal settings are
           * updated along with current and active since the code that
           * will actually build the message for the sequencer uses the
           * goal settings as its guidelines.
           */
          aic7xxx_set_syncrate(p, syncrate, target, channel, new_period,
                               new_offset, trans_options,
                               AHC_TRANS_GOAL|AHC_TRANS_ACTIVE|AHC_TRANS_CUR,
			       aic_dev);
          scb->flags &= ~SCB_MSGOUT_BITS;
          scb->flags |= SCB_MSGOUT_SDTR;
          aic_outb(p, HOST_MSG, MSG_OUT);
          aic_outb(p, aic_inb(p, SCSISIGO) | ATNO, SCSISIGO);
        }
        else
        {
          aic7xxx_set_syncrate(p, syncrate, target, channel, new_period,
                               new_offset, trans_options,
                               AHC_TRANS_ACTIVE|AHC_TRANS_CUR, aic_dev);
          aic_dev->needsdtr = 0;
        }
        done = TRUE;
        break;
      }
      case MSG_EXT_WDTR:
      {
          
        if (p->msg_buf[1] != MSG_EXT_WDTR_LEN)
        {
          reject = TRUE;
          break;
        }

        if (p->msg_len < (MSG_EXT_WDTR_LEN + 2))
        {
          break;
        }

        bus_width = new_bus_width = p->msg_buf[3];

        if ( (scb->flags & (SCB_MSGOUT_SENT|SCB_MSGOUT_WDTR)) ==
             (SCB_MSGOUT_SENT|SCB_MSGOUT_WDTR) )
        {
          switch(bus_width)
          {
            default:
            {
              reject = TRUE;
              if ( (aic7xxx_verbose & VERBOSE_NEGOTIATION2) &&
                   ((aic_dev->flags & DEVICE_PRINT_DTR) ||
                    (aic7xxx_verbose > 0xffff)) )
              {
                printk(INFO_LEAD "Requesting %d bit transfers, rejecting.\n",
                  p->host_no, CTL_OF_SCB(scb), 8 * (0x01 << bus_width));
              }
            } /* We fall through on purpose */
            case MSG_EXT_WDTR_BUS_8_BIT:
            {
              aic_dev->goal.width = MSG_EXT_WDTR_BUS_8_BIT;
              aic_dev->needwdtr_copy &= ~target_mask;
              break;
            }
            case MSG_EXT_WDTR_BUS_16_BIT:
            {
              break;
            }
          }
          aic_dev->needwdtr = 0;
          aic7xxx_set_width(p, target, channel, lun, new_bus_width,
                            AHC_TRANS_ACTIVE|AHC_TRANS_CUR, aic_dev);
        }
        else
        {
          if ( !(aic_dev->flags & DEVICE_DTR_SCANNED) )
          {
            /* 
             * Well, we now know the WDTR and SYNC caps of this device since
             * it contacted us first, mark it as such and copy the user stuff
             * over to the goal stuff.
             */
            if( (p->features & AHC_WIDE) && p->user[tindex].width )
            {
              aic_dev->goal.width = MSG_EXT_WDTR_BUS_16_BIT;
              aic_dev->needwdtr_copy = 1;
            }
            
            /*
             * Devices that support DT transfers don't start WDTR requests
             */
            aic_dev->goal.options = 0;

            if(p->user[tindex].offset)
            {
              aic_dev->needsdtr_copy = 1;
              aic_dev->goal.period = max_t(unsigned char, 10, p->user[tindex].period);
              if(p->features & AHC_ULTRA2)
              {
                aic_dev->goal.offset = MAX_OFFSET_ULTRA2;
              }
              else if( aic_dev->goal.width )
              {
                aic_dev->goal.offset = MAX_OFFSET_16BIT;
              }
              else
              {
                aic_dev->goal.offset = MAX_OFFSET_8BIT;
              }
            } else {
              aic_dev->needsdtr_copy = 0;
              aic_dev->goal.period = 255;
              aic_dev->goal.offset = 0;
            }
            
            aic_dev->flags |= DEVICE_DTR_SCANNED | DEVICE_PRINT_DTR;
          }
          else if (aic_dev->needwdtr_copy == 0)
          {
            /*
             * This is a preemptive message from the target, we've already
             * scanned this target and set our options for it, and we
             * don't need a WDTR with this target (for whatever reason),
             * so reject this incoming WDTR
             */
            reject = TRUE;
            break;
          }

          /* The device is sending this message first and we have to reply */
          reply = TRUE;

          if (aic7xxx_verbose & VERBOSE_NEGOTIATION2)
          {
            printk(INFO_LEAD "Received pre-emptive WDTR message from "
                   "target.\n", p->host_no, CTL_OF_SCB(scb));
          }
          switch(bus_width)
          {
            case MSG_EXT_WDTR_BUS_16_BIT:
            {
              if ( (p->features & AHC_WIDE) &&
                   (aic_dev->goal.width == MSG_EXT_WDTR_BUS_16_BIT) )
              {
                new_bus_width = MSG_EXT_WDTR_BUS_16_BIT;
                break;
              }
            } /* Fall through if we aren't a wide card */
            default:
            case MSG_EXT_WDTR_BUS_8_BIT:
            {
              aic_dev->needwdtr_copy = 0;
              new_bus_width = MSG_EXT_WDTR_BUS_8_BIT;
              break;
            }
          }
          scb->flags &= ~SCB_MSGOUT_BITS;
          scb->flags |= SCB_MSGOUT_WDTR;
          aic_dev->needwdtr = 0;
          if(aic_dev->dtr_pending == 0)
          {
            /* there is no other command with SCB_DTR_SCB already set that will
             * trigger the release of the dtr_pending bit.  Both set the bit
             * and set scb->flags |= SCB_DTR_SCB
             */
            aic_dev->dtr_pending = 1;
            scb->flags |= SCB_DTR_SCB;
          }
          aic_outb(p, HOST_MSG, MSG_OUT);
          aic_outb(p, aic_inb(p, SCSISIGO) | ATNO, SCSISIGO);
          /* when sending a reply, make sure that the goal settings are
           * updated along with current and active since the code that
           * will actually build the message for the sequencer uses the
           * goal settings as its guidelines.
           */
          aic7xxx_set_width(p, target, channel, lun, new_bus_width,
                          AHC_TRANS_GOAL|AHC_TRANS_ACTIVE|AHC_TRANS_CUR,
			  aic_dev);
        }
        
        /*
         * By virtue of the SCSI spec, a WDTR message negates any existing
         * SDTR negotiations.  So, even if needsdtr isn't marked for this
         * device, we still have to do a new SDTR message if the device
         * supports SDTR at all.  Therefore, we check needsdtr_copy instead
         * of needstr.
         */
        aic7xxx_set_syncrate(p, NULL, target, channel, 0, 0, 0,
                             AHC_TRANS_ACTIVE|AHC_TRANS_CUR|AHC_TRANS_QUITE,
			     aic_dev);
        aic_dev->needsdtr = aic_dev->needsdtr_copy;
        done = TRUE;
        break;
      }
      case MSG_EXT_PPR:
      {
        
        if (p->msg_buf[1] != MSG_EXT_PPR_LEN)
        {
          reject = TRUE;
          break;
        }

        if (p->msg_len < (MSG_EXT_PPR_LEN + 2))
        {
          break;
        }

        period = new_period = p->msg_buf[3];
        offset = new_offset = p->msg_buf[5];
        bus_width = new_bus_width = p->msg_buf[6];
        trans_options = new_trans_options = p->msg_buf[7] & 0xf;

        if(aic7xxx_verbose & VERBOSE_NEGOTIATION2)
        {
          printk(INFO_LEAD "Parsing PPR message (%d/%d/%d/%d)\n",
                 p->host_no, CTL_OF_SCB(scb), period, offset, bus_width,
                 trans_options);
        }

        /*
         * We might have a device that is starting negotiation with us
         * before we can start up negotiation with it....be prepared to
         * have a device ask for a higher speed then we want to give it
         * in that case
         */
        if ( (scb->flags & (SCB_MSGOUT_SENT|SCB_MSGOUT_PPR)) !=
             (SCB_MSGOUT_SENT|SCB_MSGOUT_PPR) )
        { 
          /* Have we scanned the device yet? */
          if (!(aic_dev->flags & DEVICE_DTR_SCANNED))
          {
            /* The device is electing to use PPR messages, so we will too until
             * we know better */
            aic_dev->needppr = aic_dev->needppr_copy = 1;
            aic_dev->needsdtr = aic_dev->needsdtr_copy = 0;
            aic_dev->needwdtr = aic_dev->needwdtr_copy = 0;
          
            /* We know the device is SCSI-3 compliant due to PPR */
            aic_dev->flags |= DEVICE_SCSI_3;
          
            /*
             * Not only is the device starting this up, but it also hasn't
             * been scanned yet, so this would likely be our TUR or our
             * INQUIRY command at scan time, so we need to use the
             * settings from the SEEPROM if they existed.  Of course, even
             * if we didn't find a SEEPROM, we stuffed default values into
             * the user settings anyway, so use those in all cases.
             */
            aic_dev->goal.width = p->user[tindex].width;
            if(p->user[tindex].offset)
            {
              aic_dev->goal.period = p->user[tindex].period;
              aic_dev->goal.options = p->user[tindex].options;
              if(p->features & AHC_ULTRA2)
              {
                aic_dev->goal.offset = MAX_OFFSET_ULTRA2;
              }
              else if( aic_dev->goal.width &&
                       (bus_width == MSG_EXT_WDTR_BUS_16_BIT) &&
                       p->features & AHC_WIDE )
              {
                aic_dev->goal.offset = MAX_OFFSET_16BIT;
              }
              else
              {
                aic_dev->goal.offset = MAX_OFFSET_8BIT;
              }
            }
            else
            {
              aic_dev->goal.period = 255;
              aic_dev->goal.offset = 0;
              aic_dev->goal.options = 0;
            }
            aic_dev->flags |= DEVICE_DTR_SCANNED | DEVICE_PRINT_DTR;
          }
          else if (aic_dev->needppr_copy == 0)
          {
            /*
             * This is a preemptive message from the target, we've already
             * scanned this target and set our options for it, and we
             * don't need a PPR with this target (for whatever reason),
             * so reject this incoming PPR
             */
            reject = TRUE;
            break;
          }

          /* The device is sending this message first and we have to reply */
          reply = TRUE;
          
          if (aic7xxx_verbose & VERBOSE_NEGOTIATION2)
          {
            printk(INFO_LEAD "Received pre-emptive PPR message from "
                   "target.\n", p->host_no, CTL_OF_SCB(scb));
          }

        }

        switch(bus_width)
        {
          case MSG_EXT_WDTR_BUS_16_BIT:
          {
            if ( (aic_dev->goal.width == MSG_EXT_WDTR_BUS_16_BIT) &&
			    p->features & AHC_WIDE)
            {
              break;
            }
          }
          default:
          {
            if ( (aic7xxx_verbose & VERBOSE_NEGOTIATION2) &&
                 ((aic_dev->flags & DEVICE_PRINT_DTR) ||
                  (aic7xxx_verbose > 0xffff)) )
            {
              reply = TRUE;
              printk(INFO_LEAD "Requesting %d bit transfers, rejecting.\n",
                p->host_no, CTL_OF_SCB(scb), 8 * (0x01 << bus_width));
            }
          } /* We fall through on purpose */
          case MSG_EXT_WDTR_BUS_8_BIT:
          {
            /*
             * According to the spec, if we aren't wide, we also can't be
             * Dual Edge so clear the options byte
             */
            new_trans_options = 0;
            new_bus_width = MSG_EXT_WDTR_BUS_8_BIT;
            break;
          }
        }

        if(reply)
        {
          /* when sending a reply, make sure that the goal settings are
           * updated along with current and active since the code that
           * will actually build the message for the sequencer uses the
           * goal settings as its guidelines.
           */
          aic7xxx_set_width(p, target, channel, lun, new_bus_width,
                            AHC_TRANS_GOAL|AHC_TRANS_ACTIVE|AHC_TRANS_CUR,
			    aic_dev);
          syncrate = aic7xxx_find_syncrate(p, &new_period, maxsync,
                                           &new_trans_options);
          aic7xxx_validate_offset(p, syncrate, &new_offset, new_bus_width);
          aic7xxx_set_syncrate(p, syncrate, target, channel, new_period,
                               new_offset, new_trans_options,
                               AHC_TRANS_GOAL|AHC_TRANS_ACTIVE|AHC_TRANS_CUR,
			       aic_dev);
        }
        else
        {
          aic7xxx_set_width(p, target, channel, lun, new_bus_width,
                            AHC_TRANS_ACTIVE|AHC_TRANS_CUR, aic_dev);
          syncrate = aic7xxx_find_syncrate(p, &new_period, maxsync,
                                           &new_trans_options);
          aic7xxx_validate_offset(p, syncrate, &new_offset, new_bus_width);
          aic7xxx_set_syncrate(p, syncrate, target, channel, new_period,
                               new_offset, new_trans_options,
                               AHC_TRANS_ACTIVE|AHC_TRANS_CUR, aic_dev);
        }

        /*
         * As it turns out, if we don't *have* to have PPR messages, then
         * configure ourselves not to use them since that makes some
         * external drive chassis work (those chassis can't parse PPR
         * messages and they mangle the SCSI bus until you send a WDTR
         * and SDTR that they can understand).
         */
        if(new_trans_options == 0)
        {
          aic_dev->needppr = aic_dev->needppr_copy = 0;
          if(new_offset)
          {
            aic_dev->needsdtr = aic_dev->needsdtr_copy = 1;
          }
          if (new_bus_width)
          {
            aic_dev->needwdtr = aic_dev->needwdtr_copy = 1;
          }
        }

        if((new_offset == 0) && (offset != 0))
        {
          /*
           * Oops, the syncrate went to low for this card and we fell off
           * to async (should never happen with a device that uses PPR
           * messages, but have to be complete)
           */
          reply = TRUE;
        }

        if(reply)
        {
          scb->flags &= ~SCB_MSGOUT_BITS;
          scb->flags |= SCB_MSGOUT_PPR;
          aic_outb(p, HOST_MSG, MSG_OUT);
          aic_outb(p, aic_inb(p, SCSISIGO) | ATNO, SCSISIGO);
        }
        else
        {
          aic_dev->needppr = 0;
        }
        done = TRUE;
        break;
      }
      default:
      {
        reject = TRUE;
        break;
      }
    } /* end of switch(p->msg_type) */
  } /* end of if (!reject && (p->msg_len > 2)) */

  if (!reply && reject)
  {
    aic_outb(p, MSG_MESSAGE_REJECT, MSG_OUT);
    aic_outb(p, aic_inb(p, SCSISIGO) | ATNO, SCSISIGO);
    done = TRUE;
  }
  return(done);
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_handle_reqinit
 *
 * Description:
 *   Interrupt handler for REQINIT interrupts (used to transfer messages to
 *    and from devices).
 *_F*************************************************************************/
static void
aic7xxx_handle_reqinit(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  unsigned char lastbyte;
  unsigned char phasemis;
  int done = FALSE;

  switch(p->msg_type)
  {
    case MSG_TYPE_INITIATOR_MSGOUT:
      {
        if (p->msg_len == 0)
          panic("aic7xxx: REQINIT with no active message!\n");

        lastbyte = (p->msg_index == (p->msg_len - 1));
        phasemis = ( aic_inb(p, SCSISIGI) & PHASE_MASK) != P_MESGOUT;

        if (lastbyte || phasemis)
        {
          /* Time to end the message */
          p->msg_len = 0;
          p->msg_type = MSG_TYPE_NONE;
          /*
           * NOTE-TO-MYSELF: If you clear the REQINIT after you
           * disable REQINITs, then cases of REJECT_MSG stop working
           * and hang the bus
           */
          aic_outb(p, aic_inb(p, SIMODE1) & ~ENREQINIT, SIMODE1);
          aic_outb(p, CLRSCSIINT, CLRINT);
          p->flags &= ~AHC_HANDLING_REQINITS;

          if (phasemis == 0)
          {
            aic_outb(p, p->msg_buf[p->msg_index], SINDEX);
            aic_outb(p, 0, RETURN_1);
#ifdef AIC7XXX_VERBOSE_DEBUGGING
            if (aic7xxx_verbose > 0xffff)
              printk(INFO_LEAD "Completed sending of REQINIT message.\n",
                     p->host_no, CTL_OF_SCB(scb));
#endif
          }
          else
          {
            aic_outb(p, MSGOUT_PHASEMIS, RETURN_1);
#ifdef AIC7XXX_VERBOSE_DEBUGGING
            if (aic7xxx_verbose > 0xffff)
              printk(INFO_LEAD "PHASEMIS while sending REQINIT message.\n",
                     p->host_no, CTL_OF_SCB(scb));
#endif
          }
          unpause_sequencer(p, TRUE);
        }
        else
        {
          /*
           * Present the byte on the bus (clearing REQINIT) but don't
           * unpause the sequencer.
           */
          aic_outb(p, CLRREQINIT, CLRSINT1);
          aic_outb(p, CLRSCSIINT, CLRINT);
          aic_outb(p,  p->msg_buf[p->msg_index++], SCSIDATL);
        }
        break;
      }
    case MSG_TYPE_INITIATOR_MSGIN:
      {
        phasemis = ( aic_inb(p, SCSISIGI) & PHASE_MASK ) != P_MESGIN;

        if (phasemis == 0)
        {
          p->msg_len++;
          /* Pull the byte in without acking it */
          p->msg_buf[p->msg_index] = aic_inb(p, SCSIBUSL);
          done = aic7xxx_parse_msg(p, scb);
          /* Ack the byte */
          aic_outb(p, CLRREQINIT, CLRSINT1);
          aic_outb(p, CLRSCSIINT, CLRINT);
          aic_inb(p, SCSIDATL);
          p->msg_index++;
        }
        if (phasemis || done)
        {
#ifdef AIC7XXX_VERBOSE_DEBUGGING
          if (aic7xxx_verbose > 0xffff)
          {
            if (phasemis)
              printk(INFO_LEAD "PHASEMIS while receiving REQINIT message.\n",
                     p->host_no, CTL_OF_SCB(scb));
            else
              printk(INFO_LEAD "Completed receipt of REQINIT message.\n",
                     p->host_no, CTL_OF_SCB(scb));
          }
#endif
          /* Time to end our message session */
          p->msg_len = 0;
          p->msg_type = MSG_TYPE_NONE;
          aic_outb(p, aic_inb(p, SIMODE1) & ~ENREQINIT, SIMODE1);
          aic_outb(p, CLRSCSIINT, CLRINT);
          p->flags &= ~AHC_HANDLING_REQINITS;
          unpause_sequencer(p, TRUE);
        }
        break;
      }
    default:
      {
        panic("aic7xxx: Unknown REQINIT message type.\n");
        break;
      }
  } /* End of switch(p->msg_type) */
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_handle_scsiint
 *
 * Description:
 *   Interrupt handler for SCSI interrupts (SCSIINT).
 *-F*************************************************************************/
static void
aic7xxx_handle_scsiint(struct aic7xxx_host *p, unsigned char intstat)
{
  unsigned char scb_index;
  unsigned char status;
  struct aic7xxx_scb *scb;
  struct aic_dev_data *aic_dev;

  scb_index = aic_inb(p, SCB_TAG);
  status = aic_inb(p, SSTAT1);

  if (scb_index < p->scb_data->numscbs)
  {
    scb = p->scb_data->scb_array[scb_index];
    if ((scb->flags & SCB_ACTIVE) == 0)
    {
      scb = NULL;
    }
  }
  else
  {
    scb = NULL;
  }


  if ((status & SCSIRSTI) != 0)
  {
    int channel;

    if ( (p->chip & AHC_CHIPID_MASK) == AHC_AIC7770 )
      channel = (aic_inb(p, SBLKCTL) & SELBUSB) >> 3;
    else
      channel = 0;

    if (aic7xxx_verbose & VERBOSE_RESET)
      printk(WARN_LEAD "Someone else reset the channel!!\n",
           p->host_no, channel, -1, -1);
    if (aic7xxx_panic_on_abort)
      aic7xxx_panic_abort(p, NULL);
    /*
     * Go through and abort all commands for the channel, but do not
     * reset the channel again.
     */
    aic7xxx_reset_channel(p, channel, /* Initiate Reset */ FALSE);
    aic7xxx_run_done_queue(p, TRUE);
    scb = NULL;
  }
  else if ( ((status & BUSFREE) != 0) && ((status & SELTO) == 0) )
  {
    /*
     * First look at what phase we were last in.  If it's message-out,
     * chances are pretty good that the bus free was in response to
     * one of our abort requests.
     */
    unsigned char lastphase = aic_inb(p, LASTPHASE);
    unsigned char saved_tcl = aic_inb(p, SAVED_TCL);
    unsigned char target = (saved_tcl >> 4) & 0x0F;
    int channel;
    int printerror = TRUE;

    if ( (p->chip & AHC_CHIPID_MASK) == AHC_AIC7770 )
      channel = (aic_inb(p, SBLKCTL) & SELBUSB) >> 3;
    else
      channel = 0;

    aic_outb(p, aic_inb(p, SCSISEQ) & (ENSELI|ENRSELI|ENAUTOATNP),
             SCSISEQ);
    if (lastphase == P_MESGOUT)
    {
      unsigned char message;

      message = aic_inb(p, SINDEX);

      if ((message == MSG_ABORT) || (message == MSG_ABORT_TAG))
      {
        if (aic7xxx_verbose & VERBOSE_ABORT_PROCESS)
          printk(INFO_LEAD "SCB %d abort delivered.\n", p->host_no,
            CTL_OF_SCB(scb), scb->hscb->tag);
        aic7xxx_reset_device(p, target, channel, ALL_LUNS,
                (message == MSG_ABORT) ? SCB_LIST_NULL : scb->hscb->tag );
        aic7xxx_run_done_queue(p, TRUE);
        scb = NULL;
        printerror = 0;
      }
      else if (message == MSG_BUS_DEV_RESET)
      {
        aic7xxx_handle_device_reset(p, target, channel);
        scb = NULL;
        printerror = 0;
      }
    }
    if ( (scb != NULL) && (scb->flags & SCB_DTR_SCB) ) 
    {
      /*
       * Hmmm...error during a negotiation command.  Either we have a
       * borken bus, or the device doesn't like our negotiation message.
       * Since we check the INQUIRY data of a device before sending it
       * negotiation messages, assume the bus is borken for whatever
       * reason.  Complete the command.
       */
      printerror = 0;
      aic7xxx_reset_device(p, target, channel, ALL_LUNS, scb->hscb->tag);
      aic7xxx_run_done_queue(p, TRUE);
      scb = NULL;
    }
    if (printerror != 0)
    {
      if (scb != NULL)
      {
        unsigned char tag;

        if ((scb->hscb->control & TAG_ENB) != 0)
        {
          tag = scb->hscb->tag;
        }
        else
        {
          tag = SCB_LIST_NULL;
        }
        aic7xxx_reset_device(p, target, channel, ALL_LUNS, tag);
        aic7xxx_run_done_queue(p, TRUE);
      }
      else
      {
        aic7xxx_reset_device(p, target, channel, ALL_LUNS, SCB_LIST_NULL);
        aic7xxx_run_done_queue(p, TRUE);
      }
      printk(INFO_LEAD "Unexpected busfree, LASTPHASE = 0x%x, "
             "SEQADDR = 0x%x\n", p->host_no, channel, target, -1, lastphase,
             (aic_inb(p, SEQADDR1) << 8) | aic_inb(p, SEQADDR0));
      scb = NULL;
    }
    aic_outb(p, MSG_NOOP, MSG_OUT);
    aic_outb(p, aic_inb(p, SIMODE1) & ~(ENBUSFREE|ENREQINIT),
      SIMODE1);
    p->flags &= ~AHC_HANDLING_REQINITS;
    aic_outb(p, CLRBUSFREE, CLRSINT1);
    aic_outb(p, CLRSCSIINT, CLRINT);
    restart_sequencer(p);
    unpause_sequencer(p, TRUE);
  }
  else if ((status & SELTO) != 0)
  {
	unsigned char scbptr;
	unsigned char nextscb;
	struct scsi_cmnd *cmd;

    scbptr = aic_inb(p, WAITING_SCBH);
    if (scbptr > p->scb_data->maxhscbs)
    {
      /*
       * I'm still trying to track down exactly how this happens, but until
       * I find it, this code will make sure we aren't passing bogus values
       * into the SCBPTR register, even if that register will just wrap
       * things around, we still don't like having out of range variables.
       *
       * NOTE: Don't check the aic7xxx_verbose variable, I want this message
       * to always be displayed.
       */
      printk(INFO_LEAD "Invalid WAITING_SCBH value %d, improvising.\n",
             p->host_no, -1, -1, -1, scbptr);
      if (p->scb_data->maxhscbs > 4)
        scbptr &= (p->scb_data->maxhscbs - 1);
      else
        scbptr &= 0x03;
    }
    aic_outb(p, scbptr, SCBPTR);
    scb_index = aic_inb(p, SCB_TAG);

    scb = NULL;
    if (scb_index < p->scb_data->numscbs)
    {
      scb = p->scb_data->scb_array[scb_index];
      if ((scb->flags & SCB_ACTIVE) == 0)
      {
        scb = NULL;
      }
    }
    if (scb == NULL)
    {
      printk(WARN_LEAD "Referenced SCB %d not valid during SELTO.\n",
             p->host_no, -1, -1, -1, scb_index);
      printk(KERN_WARNING "        SCSISEQ = 0x%x SEQADDR = 0x%x SSTAT0 = 0x%x "
             "SSTAT1 = 0x%x\n", aic_inb(p, SCSISEQ),
             aic_inb(p, SEQADDR0) | (aic_inb(p, SEQADDR1) << 8),
             aic_inb(p, SSTAT0), aic_inb(p, SSTAT1));
      if (aic7xxx_panic_on_abort)
        aic7xxx_panic_abort(p, NULL);
    }
    else
    {
      cmd = scb->cmd;
      cmd->result = (DID_TIME_OUT << 16);

      /*
       * Clear out this hardware SCB
       */
      aic_outb(p, 0, SCB_CONTROL);

      /*
       * Clear out a few values in the card that are in an undetermined
       * state.
       */
      aic_outb(p, MSG_NOOP, MSG_OUT);

      /*
       * Shift the waiting for selection queue forward
       */
      nextscb = aic_inb(p, SCB_NEXT);
      aic_outb(p, nextscb, WAITING_SCBH);

      /*
       * Put this SCB back on the free list.
       */
      aic7xxx_add_curscb_to_free_list(p);
#ifdef AIC7XXX_VERBOSE_DEBUGGING
      if (aic7xxx_verbose > 0xffff)
        printk(INFO_LEAD "Selection Timeout.\n", p->host_no, CTL_OF_SCB(scb));
#endif
      if (scb->flags & SCB_QUEUED_ABORT)
      {
        /*
         * We know that this particular SCB had to be the queued abort since
         * the disconnected SCB would have gotten a reconnect instead.
         * What we need to do then is to let the command timeout again so
         * we get a reset since this abort just failed.
         */
        cmd->result = 0;
        scb = NULL;
      }
    }
    /*
     * Keep the sequencer from trying to restart any selections
     */
    aic_outb(p, aic_inb(p, SCSISEQ) & ~ENSELO, SCSISEQ);
    /*
     * Make sure the data bits on the bus are released
     * Don't do this on 7770 chipsets, it makes them give us
     * a BRKADDRINT and kills the card.
     */
    if( (p->chip & ~AHC_CHIPID_MASK) == AHC_PCI )
      aic_outb(p, 0, SCSIBUSL);

    /*
     * Delay for the selection timeout delay period then stop the selection
     */
    udelay(301);
    aic_outb(p, CLRSELINGO, CLRSINT0);
    /*
     * Clear out all the interrupt status bits
     */
    aic_outb(p, aic_inb(p, SIMODE1) & ~(ENREQINIT|ENBUSFREE), SIMODE1);
    p->flags &= ~AHC_HANDLING_REQINITS;
    aic_outb(p, CLRSELTIMEO | CLRBUSFREE, CLRSINT1);
    aic_outb(p, CLRSCSIINT, CLRINT);
    /*
     * Restarting the sequencer will stop the selection and make sure devices
     * are allowed to reselect in.
     */
    restart_sequencer(p);
    unpause_sequencer(p, TRUE);
  }
  else if (scb == NULL)
  {
    printk(WARN_LEAD "aic7xxx_isr - referenced scb not valid "
           "during scsiint 0x%x scb(%d)\n"
           "      SIMODE0 0x%x, SIMODE1 0x%x, SSTAT0 0x%x, SEQADDR 0x%x\n",
           p->host_no, -1, -1, -1, status, scb_index, aic_inb(p, SIMODE0),
           aic_inb(p, SIMODE1), aic_inb(p, SSTAT0),
           (aic_inb(p, SEQADDR1) << 8) | aic_inb(p, SEQADDR0));
    /*
     * Turn off the interrupt and set status to zero, so that it
     * falls through the rest of the SCSIINT code.
     */
    aic_outb(p, status, CLRSINT1);
    aic_outb(p, CLRSCSIINT, CLRINT);
    unpause_sequencer(p, /* unpause always */ TRUE);
    scb = NULL;
  }
  else if (status & SCSIPERR)
  {
    /*
     * Determine the bus phase and queue an appropriate message.
     */
	char  *phase;
	struct scsi_cmnd *cmd;
	unsigned char mesg_out = MSG_NOOP;
	unsigned char lastphase = aic_inb(p, LASTPHASE);
	unsigned char sstat2 = aic_inb(p, SSTAT2);

    cmd = scb->cmd;
    switch (lastphase)
    {
      case P_DATAOUT:
        phase = "Data-Out";
        break;
      case P_DATAIN:
        phase = "Data-In";
        mesg_out = MSG_INITIATOR_DET_ERR;
        break;
      case P_COMMAND:
        phase = "Command";
        break;
      case P_MESGOUT:
        phase = "Message-Out";
        break;
      case P_STATUS:
        phase = "Status";
        mesg_out = MSG_INITIATOR_DET_ERR;
        break;
      case P_MESGIN:
        phase = "Message-In";
        mesg_out = MSG_PARITY_ERROR;
        break;
      default:
        phase = "unknown";
        break;
    }

    /*
     * A parity error has occurred during a data
     * transfer phase. Flag it and continue.
     */
    if( (p->features & AHC_ULTRA3) && 
        (aic_inb(p, SCSIRATE) & AHC_SYNCRATE_CRC) &&
        (lastphase == P_DATAIN) )
    {
      printk(WARN_LEAD "CRC error during %s phase.\n",
             p->host_no, CTL_OF_SCB(scb), phase);
      if(sstat2 & CRCVALERR)
      {
        printk(WARN_LEAD "  CRC error in intermediate CRC packet.\n",
               p->host_no, CTL_OF_SCB(scb));
      }
      if(sstat2 & CRCENDERR)
      {
        printk(WARN_LEAD "  CRC error in ending CRC packet.\n",
               p->host_no, CTL_OF_SCB(scb));
      }
      if(sstat2 & CRCREQERR)
      {
        printk(WARN_LEAD "  Target incorrectly requested a CRC packet.\n",
               p->host_no, CTL_OF_SCB(scb));
      }
      if(sstat2 & DUAL_EDGE_ERROR)
      {
        printk(WARN_LEAD "  Dual Edge transmission error.\n",
               p->host_no, CTL_OF_SCB(scb));
      }
    }
    else if( (lastphase == P_MESGOUT) &&
             (scb->flags & SCB_MSGOUT_PPR) )
    {
      /*
       * As per the draft specs, any device capable of supporting any of
       * the option values other than 0 are not allowed to reject the
       * PPR message.  Instead, they must negotiate out what they do
       * support instead of rejecting our offering or else they cause
       * a parity error during msg_out phase to signal that they don't
       * like our settings.
       */
      aic_dev = AIC_DEV(scb->cmd);
      aic_dev->needppr = aic_dev->needppr_copy = 0;
      aic7xxx_set_width(p, scb->cmd->device->id, scb->cmd->device->channel, scb->cmd->device->lun,
                        MSG_EXT_WDTR_BUS_8_BIT,
                        (AHC_TRANS_ACTIVE|AHC_TRANS_CUR|AHC_TRANS_QUITE),
			aic_dev);
      aic7xxx_set_syncrate(p, NULL, scb->cmd->device->id, scb->cmd->device->channel, 0, 0,
                           0, AHC_TRANS_ACTIVE|AHC_TRANS_CUR|AHC_TRANS_QUITE,
			   aic_dev);
      aic_dev->goal.options = 0;
      scb->flags &= ~SCB_MSGOUT_BITS;
      if(aic7xxx_verbose & VERBOSE_NEGOTIATION2)
      {
        printk(INFO_LEAD "parity error during PPR message, reverting "
               "to WDTR/SDTR\n", p->host_no, CTL_OF_SCB(scb));
      }
      if ( aic_dev->goal.width )
      {
        aic_dev->needwdtr = aic_dev->needwdtr_copy = 1;
      }
      if ( aic_dev->goal.offset )
      {
        if( aic_dev->goal.period <= 9 )
        {
          aic_dev->goal.period = 10;
        }
        aic_dev->needsdtr = aic_dev->needsdtr_copy = 1;
      }
      scb = NULL;
    }

    /*
     * We've set the hardware to assert ATN if we get a parity
     * error on "in" phases, so all we need to do is stuff the
     * message buffer with the appropriate message.  "In" phases
     * have set mesg_out to something other than MSG_NOP.
     */
    if (mesg_out != MSG_NOOP)
    {
      aic_outb(p, mesg_out, MSG_OUT);
      aic_outb(p, aic_inb(p, SCSISIGI) | ATNO, SCSISIGO);
      scb = NULL;
    }
    aic_outb(p, CLRSCSIPERR, CLRSINT1);
    aic_outb(p, CLRSCSIINT, CLRINT);
    unpause_sequencer(p, /* unpause_always */ TRUE);
  }
  else if ( (status & REQINIT) &&
            (p->flags & AHC_HANDLING_REQINITS) )
  {
#ifdef AIC7XXX_VERBOSE_DEBUGGING
    if (aic7xxx_verbose > 0xffff)
      printk(INFO_LEAD "Handling REQINIT, SSTAT1=0x%x.\n", p->host_no,
             CTL_OF_SCB(scb), aic_inb(p, SSTAT1));
#endif
    aic7xxx_handle_reqinit(p, scb);
    return;
  }
  else
  {
    /*
     * We don't know what's going on. Turn off the
     * interrupt source and try to continue.
     */
    if (aic7xxx_verbose & VERBOSE_SCSIINT)
      printk(INFO_LEAD "Unknown SCSIINT status, SSTAT1(0x%x).\n",
        p->host_no, -1, -1, -1, status);
    aic_outb(p, status, CLRSINT1);
    aic_outb(p, CLRSCSIINT, CLRINT);
    unpause_sequencer(p, /* unpause always */ TRUE);
    scb = NULL;
  }
  if (scb != NULL)
  {
    aic7xxx_done(p, scb);
  }
}

#ifdef AIC7XXX_VERBOSE_DEBUGGING
static void
aic7xxx_check_scbs(struct aic7xxx_host *p, char *buffer)
{
  unsigned char saved_scbptr, free_scbh, dis_scbh, wait_scbh, temp;
  int i, bogus, lost;
  static unsigned char scb_status[AIC7XXX_MAXSCB];

#define SCB_NO_LIST 0
#define SCB_FREE_LIST 1
#define SCB_WAITING_LIST 2
#define SCB_DISCONNECTED_LIST 4
#define SCB_CURRENTLY_ACTIVE 8

  /*
   * Note, these checks will fail on a regular basis once the machine moves
   * beyond the bus scan phase.  The problem is race conditions concerning
   * the scbs and where they are linked in.  When you have 30 or so commands
   * outstanding on the bus, and run this twice with every interrupt, the
   * chances get pretty good that you'll catch the sequencer with an SCB
   * only partially linked in.  Therefore, once we pass the scan phase
   * of the bus, we really should disable this function.
   */
  bogus = FALSE;
  memset(&scb_status[0], 0, sizeof(scb_status));
  pause_sequencer(p);
  saved_scbptr = aic_inb(p, SCBPTR);
  if (saved_scbptr >= p->scb_data->maxhscbs)
  {
    printk("Bogus SCBPTR %d\n", saved_scbptr);
    bogus = TRUE;
  }
  scb_status[saved_scbptr] = SCB_CURRENTLY_ACTIVE;
  free_scbh = aic_inb(p, FREE_SCBH);
  if ( (free_scbh != SCB_LIST_NULL) &&
       (free_scbh >= p->scb_data->maxhscbs) )
  {
    printk("Bogus FREE_SCBH %d\n", free_scbh);
    bogus = TRUE;
  }
  else
  {
    temp = free_scbh;
    while( (temp != SCB_LIST_NULL) && (temp < p->scb_data->maxhscbs) )
    {
      if(scb_status[temp] & 0x07)
      {
        printk("HSCB %d on multiple lists, status 0x%02x", temp,
               scb_status[temp] | SCB_FREE_LIST);
        bogus = TRUE;
      }
      scb_status[temp] |= SCB_FREE_LIST;
      aic_outb(p, temp, SCBPTR);
      temp = aic_inb(p, SCB_NEXT);
    }
  }

  dis_scbh = aic_inb(p, DISCONNECTED_SCBH);
  if ( (dis_scbh != SCB_LIST_NULL) &&
       (dis_scbh >= p->scb_data->maxhscbs) )
  {
    printk("Bogus DISCONNECTED_SCBH %d\n", dis_scbh);
    bogus = TRUE;
  }
  else
  {
    temp = dis_scbh;
    while( (temp != SCB_LIST_NULL) && (temp < p->scb_data->maxhscbs) )
    {
      if(scb_status[temp] & 0x07)
      {
        printk("HSCB %d on multiple lists, status 0x%02x", temp,
               scb_status[temp] | SCB_DISCONNECTED_LIST);
        bogus = TRUE;
      }
      scb_status[temp] |= SCB_DISCONNECTED_LIST;
      aic_outb(p, temp, SCBPTR);
      temp = aic_inb(p, SCB_NEXT);
    }
  }
  
  wait_scbh = aic_inb(p, WAITING_SCBH);
  if ( (wait_scbh != SCB_LIST_NULL) &&
       (wait_scbh >= p->scb_data->maxhscbs) )
  {
    printk("Bogus WAITING_SCBH %d\n", wait_scbh);
    bogus = TRUE;
  }
  else
  {
    temp = wait_scbh;
    while( (temp != SCB_LIST_NULL) && (temp < p->scb_data->maxhscbs) )
    {
      if(scb_status[temp] & 0x07)
      {
        printk("HSCB %d on multiple lists, status 0x%02x", temp,
               scb_status[temp] | SCB_WAITING_LIST);
        bogus = TRUE;
      }
      scb_status[temp] |= SCB_WAITING_LIST;
      aic_outb(p, temp, SCBPTR);
      temp = aic_inb(p, SCB_NEXT);
    }
  }

  lost=0;
  for(i=0; i < p->scb_data->maxhscbs; i++)
  {
    aic_outb(p, i, SCBPTR);
    temp = aic_inb(p, SCB_NEXT);
    if ( ((temp != SCB_LIST_NULL) &&
          (temp >= p->scb_data->maxhscbs)) )
    {
      printk("HSCB %d bad, SCB_NEXT invalid(%d).\n", i, temp);
      bogus = TRUE;
    }
    if ( temp == i )
    {
      printk("HSCB %d bad, SCB_NEXT points to self.\n", i);
      bogus = TRUE;
    }
    if (scb_status[i] == 0)
      lost++;
    if (lost > 1)
    {
      printk("Too many lost scbs.\n");
      bogus=TRUE;
    }
  }
  aic_outb(p, saved_scbptr, SCBPTR);
  unpause_sequencer(p, FALSE);
  if (bogus)
  {
    printk("Bogus parameters found in card SCB array structures.\n");
    printk("%s\n", buffer);
    aic7xxx_panic_abort(p, NULL);
  }
  return;
}
#endif


/*+F*************************************************************************
 * Function:
 *   aic7xxx_handle_command_completion_intr
 *
 * Description:
 *   SCSI command completion interrupt handler.
 *-F*************************************************************************/
static void
aic7xxx_handle_command_completion_intr(struct aic7xxx_host *p)
{
	struct aic7xxx_scb *scb = NULL;
	struct aic_dev_data *aic_dev;
	struct scsi_cmnd *cmd;
	unsigned char scb_index, tindex;

#ifdef AIC7XXX_VERBOSE_DEBUGGING
  if( (p->isr_count < 16) && (aic7xxx_verbose > 0xffff) )
    printk(INFO_LEAD "Command Complete Int.\n", p->host_no, -1, -1, -1);
#endif
    
  /*
   * Read the INTSTAT location after clearing the CMDINT bit.  This forces
   * any posted PCI writes to flush to memory.  Gerard Roudier suggested
   * this fix to the possible race of clearing the CMDINT bit but not
   * having all command bytes flushed onto the qoutfifo.
   */
  aic_outb(p, CLRCMDINT, CLRINT);
  aic_inb(p, INTSTAT);
  /*
   * The sequencer will continue running when it
   * issues this interrupt. There may be >1 commands
   * finished, so loop until we've processed them all.
   */

  while (p->qoutfifo[p->qoutfifonext] != SCB_LIST_NULL)
  {
    scb_index = p->qoutfifo[p->qoutfifonext];
    p->qoutfifo[p->qoutfifonext++] = SCB_LIST_NULL;
    if ( scb_index >= p->scb_data->numscbs )
    {
      printk(WARN_LEAD "CMDCMPLT with invalid SCB index %d\n", p->host_no,
        -1, -1, -1, scb_index);
      continue;
    }
    scb = p->scb_data->scb_array[scb_index];
    if (!(scb->flags & SCB_ACTIVE) || (scb->cmd == NULL))
    {
      printk(WARN_LEAD "CMDCMPLT without command for SCB %d, SCB flags "
        "0x%x, cmd 0x%lx\n", p->host_no, -1, -1, -1, scb_index, scb->flags,
        (unsigned long) scb->cmd);
      continue;
    }
    tindex = TARGET_INDEX(scb->cmd);
    aic_dev = AIC_DEV(scb->cmd);
    if (scb->flags & SCB_QUEUED_ABORT)
    {
      pause_sequencer(p);
      if ( ((aic_inb(p, LASTPHASE) & PHASE_MASK) != P_BUSFREE) &&
           (aic_inb(p, SCB_TAG) == scb->hscb->tag) )
      {
        unpause_sequencer(p, FALSE);
        continue;
      }
      aic7xxx_reset_device(p, scb->cmd->device->id, scb->cmd->device->channel,
        scb->cmd->device->lun, scb->hscb->tag);
      scb->flags &= ~(SCB_QUEUED_FOR_DONE | SCB_RESET | SCB_ABORT |
        SCB_QUEUED_ABORT);
      unpause_sequencer(p, FALSE);
    }
    else if (scb->flags & SCB_ABORT)
    {
      /*
       * We started to abort this, but it completed on us, let it
       * through as successful
       */
      scb->flags &= ~(SCB_ABORT|SCB_RESET);
    }
    else if (scb->flags & SCB_SENSE)
    {
      char *buffer = &scb->cmd->sense_buffer[0];

      if (buffer[12] == 0x47 || buffer[12] == 0x54)
      {
        /*
         * Signal that we need to re-negotiate things.
         */
        aic_dev->needppr = aic_dev->needppr_copy;
        aic_dev->needsdtr = aic_dev->needsdtr_copy;
        aic_dev->needwdtr = aic_dev->needwdtr_copy;
      }
    }
    cmd = scb->cmd;
    if (scb->hscb->residual_SG_segment_count != 0)
    {
      aic7xxx_calculate_residual(p, scb);
    }
    cmd->result |= (aic7xxx_error(cmd) << 16);
    aic7xxx_done(p, scb);
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_isr
 *
 * Description:
 *   SCSI controller interrupt handler.
 *-F*************************************************************************/
static void
aic7xxx_isr(void *dev_id)
{
  struct aic7xxx_host *p;
  unsigned char intstat;

  p = dev_id;

  /*
   * Just a few sanity checks.  Make sure that we have an int pending.
   * Also, if PCI, then we are going to check for a PCI bus error status
   * should we get too many spurious interrupts.
   */
  if (!((intstat = aic_inb(p, INTSTAT)) & INT_PEND))
  {
#ifdef CONFIG_PCI
    if ( (p->chip & AHC_PCI) && (p->spurious_int > 500) &&
        !(p->flags & AHC_HANDLING_REQINITS) )
    {
      if ( aic_inb(p, ERROR) & PCIERRSTAT )
      {
        aic7xxx_pci_intr(p);
      }
      p->spurious_int = 0;
    }
    else if ( !(p->flags & AHC_HANDLING_REQINITS) )
    {
      p->spurious_int++;
    }
#endif
    return;
  }

  p->spurious_int = 0;

  /*
   * Keep track of interrupts for /proc/scsi
   */
  p->isr_count++;

#ifdef AIC7XXX_VERBOSE_DEBUGGING
  if ( (p->isr_count < 16) && (aic7xxx_verbose > 0xffff) &&
       (aic7xxx_panic_on_abort) && (p->flags & AHC_PAGESCBS) )
    aic7xxx_check_scbs(p, "Bogus settings at start of interrupt.");
#endif

  /*
   * Handle all the interrupt sources - especially for SCSI
   * interrupts, we won't get a second chance at them.
   */
  if (intstat & CMDCMPLT)
  {
    aic7xxx_handle_command_completion_intr(p);
  }

  if (intstat & BRKADRINT)
  {
    int i;
    unsigned char errno = aic_inb(p, ERROR);

    printk(KERN_ERR "(scsi%d) BRKADRINT error(0x%x):\n", p->host_no, errno);
    for (i = 0; i < ARRAY_SIZE(hard_error); i++)
    {
      if (errno & hard_error[i].errno)
      {
        printk(KERN_ERR "  %s\n", hard_error[i].errmesg);
      }
    }
    printk(KERN_ERR "(scsi%d)   SEQADDR=0x%x\n", p->host_no,
      (((aic_inb(p, SEQADDR1) << 8) & 0x100) | aic_inb(p, SEQADDR0)));
    if (aic7xxx_panic_on_abort)
      aic7xxx_panic_abort(p, NULL);
#ifdef CONFIG_PCI
    if (errno & PCIERRSTAT)
      aic7xxx_pci_intr(p);
#endif
    if (errno & (SQPARERR | ILLOPCODE | ILLSADDR))
    {
      panic("aic7xxx: unrecoverable BRKADRINT.\n");
    }
    if (errno & ILLHADDR)
    {
      printk(KERN_ERR "(scsi%d) BUG! Driver accessed chip without first "
             "pausing controller!\n", p->host_no);
    }
#ifdef AIC7XXX_VERBOSE_DEBUGGING
    if (errno & DPARERR)
    {
      if (aic_inb(p, DMAPARAMS) & DIRECTION)
        printk("(scsi%d) while DMAing SCB from host to card.\n", p->host_no);
      else
        printk("(scsi%d) while DMAing SCB from card to host.\n", p->host_no);
    }
#endif
    aic_outb(p, CLRPARERR | CLRBRKADRINT, CLRINT);
    unpause_sequencer(p, FALSE);
  }

  if (intstat & SEQINT)
  {
    /*
     * Read the CCSCBCTL register to work around a bug in the Ultra2 cards
     */
    if(p->features & AHC_ULTRA2)
    {
      aic_inb(p, CCSCBCTL);
    }
    aic7xxx_handle_seqint(p, intstat);
  }

  if (intstat & SCSIINT)
  {
    aic7xxx_handle_scsiint(p, intstat);
  }

#ifdef AIC7XXX_VERBOSE_DEBUGGING
  if ( (p->isr_count < 16) && (aic7xxx_verbose > 0xffff) &&
       (aic7xxx_panic_on_abort) && (p->flags & AHC_PAGESCBS) )
    aic7xxx_check_scbs(p, "Bogus settings at end of interrupt.");
#endif

}

/*+F*************************************************************************
 * Function:
 *   do_aic7xxx_isr
 *
 * Description:
 *   This is a gross hack to solve a problem in linux kernels 2.1.85 and
 *   above.  Please, children, do not try this at home, and if you ever see
 *   anything like it, please inform the Gross Hack Police immediately
 *-F*************************************************************************/
static irqreturn_t
do_aic7xxx_isr(int irq, void *dev_id)
{
  unsigned long cpu_flags;
  struct aic7xxx_host *p;
  
  p = dev_id;
  if(!p)
    return IRQ_NONE;
  spin_lock_irqsave(p->host->host_lock, cpu_flags);
  p->flags |= AHC_IN_ISR;
  do
  {
    aic7xxx_isr(dev_id);
  } while ( (aic_inb(p, INTSTAT) & INT_PEND) );
  aic7xxx_done_cmds_complete(p);
  aic7xxx_run_waiting_queues(p);
  p->flags &= ~AHC_IN_ISR;
  spin_unlock_irqrestore(p->host->host_lock, cpu_flags);

  return IRQ_HANDLED;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_init_transinfo
 *
 * Description:
 *   Set up the initial aic_dev values from the BIOS settings and from
 *   INQUIRY results
 *-F*************************************************************************/
static void
aic7xxx_init_transinfo(struct aic7xxx_host *p, struct aic_dev_data *aic_dev)
{
  struct scsi_device *sdpnt = aic_dev->SDptr;
  unsigned char tindex;

  tindex = sdpnt->id | (sdpnt->channel << 3);
  if (!(aic_dev->flags & DEVICE_DTR_SCANNED))
  {
    aic_dev->flags |= DEVICE_DTR_SCANNED;

    if ( sdpnt->wdtr && (p->features & AHC_WIDE) )
    {
      aic_dev->needwdtr = aic_dev->needwdtr_copy = 1;
      aic_dev->goal.width = p->user[tindex].width;
    }
    else
    {
      aic_dev->needwdtr = aic_dev->needwdtr_copy = 0;
      pause_sequencer(p);
      aic7xxx_set_width(p, sdpnt->id, sdpnt->channel, sdpnt->lun,
                        MSG_EXT_WDTR_BUS_8_BIT, (AHC_TRANS_ACTIVE |
                                                 AHC_TRANS_GOAL |
                                                 AHC_TRANS_CUR), aic_dev );
      unpause_sequencer(p, FALSE);
    }
    if ( sdpnt->sdtr && p->user[tindex].offset )
    {
      aic_dev->goal.period = p->user[tindex].period;
      aic_dev->goal.options = p->user[tindex].options;
      if (p->features & AHC_ULTRA2)
        aic_dev->goal.offset = MAX_OFFSET_ULTRA2;
      else if (aic_dev->goal.width == MSG_EXT_WDTR_BUS_16_BIT)
        aic_dev->goal.offset = MAX_OFFSET_16BIT;
      else
        aic_dev->goal.offset = MAX_OFFSET_8BIT;
      if ( sdpnt->ppr && p->user[tindex].period <= 9 &&
             p->user[tindex].options )
      {
        aic_dev->needppr = aic_dev->needppr_copy = 1;
        aic_dev->needsdtr = aic_dev->needsdtr_copy = 0;
        aic_dev->needwdtr = aic_dev->needwdtr_copy = 0;
        aic_dev->flags |= DEVICE_SCSI_3;
      }
      else
      {
        aic_dev->needsdtr = aic_dev->needsdtr_copy = 1;
        aic_dev->goal.period = max_t(unsigned char, 10, aic_dev->goal.period);
        aic_dev->goal.options = 0;
      }
    }
    else
    {
      aic_dev->needsdtr = aic_dev->needsdtr_copy = 0;
      aic_dev->goal.period = 255;
      aic_dev->goal.offset = 0;
      aic_dev->goal.options = 0;
    }
    aic_dev->flags |= DEVICE_PRINT_DTR;
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_slave_alloc
 *
 * Description:
 *   Set up the initial aic_dev struct pointers
 *-F*************************************************************************/
static int
aic7xxx_slave_alloc(struct scsi_device *SDptr)
{
  struct aic7xxx_host *p = (struct aic7xxx_host *)SDptr->host->hostdata;
  struct aic_dev_data *aic_dev;

  aic_dev = kmalloc(sizeof(struct aic_dev_data), GFP_KERNEL);
  if(!aic_dev)
    return 1;
  /*
   * Check to see if channel was scanned.
   */
  
  if (!(p->flags & AHC_A_SCANNED) && (SDptr->channel == 0))
  {
    if (aic7xxx_verbose & VERBOSE_PROBE2)
      printk(INFO_LEAD "Scanning channel for devices.\n",
        p->host_no, 0, -1, -1);
    p->flags |= AHC_A_SCANNED;
  }
  else
  {
    if (!(p->flags & AHC_B_SCANNED) && (SDptr->channel == 1))
    {
      if (aic7xxx_verbose & VERBOSE_PROBE2)
        printk(INFO_LEAD "Scanning channel for devices.\n",
          p->host_no, 1, -1, -1);
      p->flags |= AHC_B_SCANNED;
    }
  }

  memset(aic_dev, 0, sizeof(struct aic_dev_data));
  SDptr->hostdata = aic_dev;
  aic_dev->SDptr = SDptr;
  aic_dev->max_q_depth = 1;
  aic_dev->temp_q_depth = 1;
  scbq_init(&aic_dev->delayed_scbs);
  INIT_LIST_HEAD(&aic_dev->list);
  list_add_tail(&aic_dev->list, &p->aic_devs);
  return 0;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_device_queue_depth
 *
 * Description:
 *   Determines the queue depth for a given device.  There are two ways
 *   a queue depth can be obtained for a tagged queueing device.  One
 *   way is the default queue depth which is determined by whether
 *   aic7xxx_default_queue_depth.  The other is by the aic7xxx_tag_info
 *   array.
 *
 *   If tagged queueing isn't supported on the device, then we set the
 *   depth to p->host->hostt->cmd_per_lun for internal driver queueing.
 *   as the default queue depth.  Otherwise, we use either 4 or 8 as the
 *   default queue depth (dependent on the number of hardware SCBs).
 *   The other way we determine queue depth is through the use of the
 *   aic7xxx_tag_info array which is enabled by defining
 *   AIC7XXX_TAGGED_QUEUEING_BY_DEVICE.  This array can be initialized
 *   with queue depths for individual devices.  It also allows tagged
 *   queueing to be [en|dis]abled for a specific adapter.
 *-F*************************************************************************/
static void
aic7xxx_device_queue_depth(struct aic7xxx_host *p, struct scsi_device *device)
{
  int tag_enabled = FALSE;
  struct aic_dev_data *aic_dev = device->hostdata;
  unsigned char tindex;

  tindex = device->id | (device->channel << 3);

  if (device->simple_tags)
    return; // We've already enabled this device

  if (device->tagged_supported)
  {
    tag_enabled = TRUE;

    if (!(p->discenable & (1 << tindex)))
    {
      if (aic7xxx_verbose & VERBOSE_NEGOTIATION2)
        printk(INFO_LEAD "Disconnection disabled, unable to "
             "enable tagged queueing.\n",
             p->host_no, device->channel, device->id, device->lun);
      tag_enabled = FALSE;
    }
    else
    {
      if (p->instance >= ARRAY_SIZE(aic7xxx_tag_info))
      {
        static int print_warning = TRUE;
        if(print_warning)
        {
          printk(KERN_INFO "aic7xxx: WARNING, insufficient tag_info instances for"
                           " installed controllers.\n");
          printk(KERN_INFO "aic7xxx: Please update the aic7xxx_tag_info array in"
                           " the aic7xxx.c source file.\n");
          print_warning = FALSE;
        }
        aic_dev->max_q_depth = aic_dev->temp_q_depth =
		aic7xxx_default_queue_depth;
      }
      else
      {

        if (aic7xxx_tag_info[p->instance].tag_commands[tindex] == 255)
        {
          tag_enabled = FALSE;
        }
        else if (aic7xxx_tag_info[p->instance].tag_commands[tindex] == 0)
        {
          aic_dev->max_q_depth = aic_dev->temp_q_depth =
		  aic7xxx_default_queue_depth;
        }
        else
        {
          aic_dev->max_q_depth = aic_dev->temp_q_depth = 
            aic7xxx_tag_info[p->instance].tag_commands[tindex];
        }
      }
    }
  }
  if (tag_enabled)
  {
    if (aic7xxx_verbose & VERBOSE_NEGOTIATION2)
    {
          printk(INFO_LEAD "Tagged queuing enabled, queue depth %d.\n",
            p->host_no, device->channel, device->id,
            device->lun, aic_dev->max_q_depth);
    }
    scsi_adjust_queue_depth(device, MSG_ORDERED_TAG, aic_dev->max_q_depth);
  }
  else
  {
    if (aic7xxx_verbose & VERBOSE_NEGOTIATION2)
    {
          printk(INFO_LEAD "Tagged queuing disabled, queue depth %d.\n",
            p->host_no, device->channel, device->id,
            device->lun, device->host->cmd_per_lun);
    }
    scsi_adjust_queue_depth(device, 0, device->host->cmd_per_lun);
  }
  return;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_slave_destroy
 *
 * Description:
 *   prepare for this device to go away
 *-F*************************************************************************/
static void
aic7xxx_slave_destroy(struct scsi_device *SDptr)
{
  struct aic_dev_data *aic_dev = SDptr->hostdata;

  list_del(&aic_dev->list);
  SDptr->hostdata = NULL;
  kfree(aic_dev);
  return;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_slave_configure
 *
 * Description:
 *   Configure the device we are attaching to the controller.  This is
 *   where we get to do things like scan the INQUIRY data, set queue
 *   depths, allocate command structs, etc.
 *-F*************************************************************************/
static int
aic7xxx_slave_configure(struct scsi_device *SDptr)
{
  struct aic7xxx_host *p = (struct aic7xxx_host *) SDptr->host->hostdata;
  struct aic_dev_data *aic_dev;
  int scbnum;

  aic_dev = (struct aic_dev_data *)SDptr->hostdata;

  aic7xxx_init_transinfo(p, aic_dev);
  aic7xxx_device_queue_depth(p, SDptr);
  if(list_empty(&aic_dev->list))
    list_add_tail(&aic_dev->list, &p->aic_devs);

  scbnum = 0;
  list_for_each_entry(aic_dev, &p->aic_devs, list) {
    scbnum += aic_dev->max_q_depth;
  }
  while (scbnum > p->scb_data->numscbs)
  {
    /*
     * Pre-allocate the needed SCBs to get around the possibility of having
     * to allocate some when memory is more or less exhausted and we need
     * the SCB in order to perform a swap operation (possible deadlock)
     */
    if ( aic7xxx_allocate_scb(p) == 0 )
      break;
  }


  return(0);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_probe
 *
 * Description:
 *   Probing for EISA boards: it looks like the first two bytes
 *   are a manufacturer code - three characters, five bits each:
 *
 *               BYTE 0   BYTE 1   BYTE 2   BYTE 3
 *              ?1111122 22233333 PPPPPPPP RRRRRRRR
 *
 *   The characters are baselined off ASCII '@', so add that value
 *   to each to get the real ASCII code for it. The next two bytes
 *   appear to be a product and revision number, probably vendor-
 *   specific. This is what is being searched for at each port,
 *   and what should probably correspond to the ID= field in the
 *   ECU's .cfg file for the card - if your card is not detected,
 *   make sure your signature is listed in the array.
 *
 *   The fourth byte's lowest bit seems to be an enabled/disabled
 *   flag (rest of the bits are reserved?).
 *
 * NOTE:  This function is only needed on Intel and Alpha platforms,
 *   the other platforms we support don't have EISA/VLB busses.  So,
 *   we #ifdef this entire function to avoid compiler warnings about
 *   an unused function.
 *-F*************************************************************************/
#if defined(__i386__) || defined(__alpha__)
static int
aic7xxx_probe(int slot, int base, ahc_flag_type *flags)
{
  int i;
  unsigned char buf[4];

  static struct {
    int n;
    unsigned char signature[sizeof(buf)];
    ahc_chip type;
    int bios_disabled;
  } AIC7xxx[] = {
    { 4, { 0x04, 0x90, 0x77, 0x70 },
      AHC_AIC7770|AHC_EISA, FALSE },  /* mb 7770  */
    { 4, { 0x04, 0x90, 0x77, 0x71 },
      AHC_AIC7770|AHC_EISA, FALSE }, /* host adapter 274x */
    { 4, { 0x04, 0x90, 0x77, 0x56 },
      AHC_AIC7770|AHC_VL, FALSE }, /* 284x BIOS enabled */
    { 4, { 0x04, 0x90, 0x77, 0x57 },
      AHC_AIC7770|AHC_VL, TRUE }   /* 284x BIOS disabled */
  };

  /*
   * The VL-bus cards need to be primed by
   * writing before a signature check.
   */
  for (i = 0; i < sizeof(buf); i++)
  {
    outb(0x80 + i, base);
    buf[i] = inb(base + i);
  }

  for (i = 0; i < ARRAY_SIZE(AIC7xxx); i++)
  {
    /*
     * Signature match on enabled card?
     */
    if (!memcmp(buf, AIC7xxx[i].signature, AIC7xxx[i].n))
    {
      if (inb(base + 4) & 1)
      {
        if (AIC7xxx[i].bios_disabled)
        {
          *flags |= AHC_USEDEFAULTS;
        }
        else
        {
          *flags |= AHC_BIOS_ENABLED;
        }
        return (i);
      }

      printk("aic7xxx: <Adaptec 7770 SCSI Host Adapter> "
             "disabled at slot %d, ignored.\n", slot);
    }
  }

  return (-1);
}
#endif /* (__i386__) || (__alpha__) */


/*+F*************************************************************************
 * Function:
 *   read_2840_seeprom
 *
 * Description:
 *   Reads the 2840 serial EEPROM and returns 1 if successful and 0 if
 *   not successful.
 *
 *   See read_seeprom (for the 2940) for the instruction set of the 93C46
 *   chip.
 *
 *   The 2840 interface to the 93C46 serial EEPROM is through the
 *   STATUS_2840 and SEECTL_2840 registers.  The CS_2840, CK_2840, and
 *   DO_2840 bits of the SEECTL_2840 register are connected to the chip
 *   select, clock, and data out lines respectively of the serial EEPROM.
 *   The DI_2840 bit of the STATUS_2840 is connected to the data in line
 *   of the serial EEPROM.  The EEPROM_TF bit of STATUS_2840 register is
 *   useful in that it gives us an 800 nsec timer.  After a read from the
 *   SEECTL_2840 register the timing flag is cleared and goes high 800 nsec
 *   later.
 *-F*************************************************************************/
static int
read_284x_seeprom(struct aic7xxx_host *p, struct seeprom_config *sc)
{
  int i = 0, k = 0;
  unsigned char temp;
  unsigned short checksum = 0;
  unsigned short *seeprom = (unsigned short *) sc;
  struct seeprom_cmd {
    unsigned char len;
    unsigned char bits[3];
  };
  struct seeprom_cmd seeprom_read = {3, {1, 1, 0}};

#define CLOCK_PULSE(p) \
  while ((aic_inb(p, STATUS_2840) & EEPROM_TF) == 0)        \
  {                                                \
    ;  /* Do nothing */                                \
  }                                                \
  (void) aic_inb(p, SEECTL_2840);

  /*
   * Read the first 32 registers of the seeprom.  For the 2840,
   * the 93C46 SEEPROM is a 1024-bit device with 64 16-bit registers
   * but only the first 32 are used by Adaptec BIOS.  The loop
   * will range from 0 to 31.
   */
  for (k = 0; k < (sizeof(*sc) / 2); k++)
  {
    /*
     * Send chip select for one clock cycle.
     */
    aic_outb(p, CK_2840 | CS_2840, SEECTL_2840);
    CLOCK_PULSE(p);

    /*
     * Now we're ready to send the read command followed by the
     * address of the 16-bit register we want to read.
     */
    for (i = 0; i < seeprom_read.len; i++)
    {
      temp = CS_2840 | seeprom_read.bits[i];
      aic_outb(p, temp, SEECTL_2840);
      CLOCK_PULSE(p);
      temp = temp ^ CK_2840;
      aic_outb(p, temp, SEECTL_2840);
      CLOCK_PULSE(p);
    }
    /*
     * Send the 6 bit address (MSB first, LSB last).
     */
    for (i = 5; i >= 0; i--)
    {
      temp = k;
      temp = (temp >> i) & 1;  /* Mask out all but lower bit. */
      temp = CS_2840 | temp;
      aic_outb(p, temp, SEECTL_2840);
      CLOCK_PULSE(p);
      temp = temp ^ CK_2840;
      aic_outb(p, temp, SEECTL_2840);
      CLOCK_PULSE(p);
    }

    /*
     * Now read the 16 bit register.  An initial 0 precedes the
     * register contents which begins with bit 15 (MSB) and ends
     * with bit 0 (LSB).  The initial 0 will be shifted off the
     * top of our word as we let the loop run from 0 to 16.
     */
    for (i = 0; i <= 16; i++)
    {
      temp = CS_2840;
      aic_outb(p, temp, SEECTL_2840);
      CLOCK_PULSE(p);
      temp = temp ^ CK_2840;
      seeprom[k] = (seeprom[k] << 1) | (aic_inb(p, STATUS_2840) & DI_2840);
      aic_outb(p, temp, SEECTL_2840);
      CLOCK_PULSE(p);
    }
    /*
     * The serial EEPROM has a checksum in the last word.  Keep a
     * running checksum for all words read except for the last
     * word.  We'll verify the checksum after all words have been
     * read.
     */
    if (k < (sizeof(*sc) / 2) - 1)
    {
      checksum = checksum + seeprom[k];
    }

    /*
     * Reset the chip select for the next command cycle.
     */
    aic_outb(p, 0, SEECTL_2840);
    CLOCK_PULSE(p);
    aic_outb(p, CK_2840, SEECTL_2840);
    CLOCK_PULSE(p);
    aic_outb(p, 0, SEECTL_2840);
    CLOCK_PULSE(p);
  }

#if 0
  printk("Computed checksum 0x%x, checksum read 0x%x\n", checksum, sc->checksum);
  printk("Serial EEPROM:");
  for (k = 0; k < (sizeof(*sc) / 2); k++)
  {
    if (((k % 8) == 0) && (k != 0))
    {
      printk("\n              ");
    }
    printk(" 0x%x", seeprom[k]);
  }
  printk("\n");
#endif

  if (checksum != sc->checksum)
  {
    printk("aic7xxx: SEEPROM checksum error, ignoring SEEPROM settings.\n");
    return (0);
  }

  return (1);
#undef CLOCK_PULSE
}

#define CLOCK_PULSE(p)                                               \
  do {                                                               \
    int limit = 0;                                                   \
    do {                                                             \
      mb();                                                          \
      pause_sequencer(p);  /* This is just to generate some PCI */   \
                           /* traffic so the PCI read is flushed */  \
                           /* it shouldn't be needed, but some */    \
                           /* chipsets do indeed appear to need */   \
                           /* something to force PCI reads to get */ \
                           /* flushed */                             \
      udelay(1);           /* Do nothing */                          \
    } while (((aic_inb(p, SEECTL) & SEERDY) == 0) && (++limit < 1000)); \
  } while(0)

/*+F*************************************************************************
 * Function:
 *   acquire_seeprom
 *
 * Description:
 *   Acquires access to the memory port on PCI controllers.
 *-F*************************************************************************/
static int
acquire_seeprom(struct aic7xxx_host *p)
{

  /*
   * Request access of the memory port.  When access is
   * granted, SEERDY will go high.  We use a 1 second
   * timeout which should be near 1 second more than
   * is needed.  Reason: after the 7870 chip reset, there
   * should be no contention.
   */
  aic_outb(p, SEEMS, SEECTL);
  CLOCK_PULSE(p);
  if ((aic_inb(p, SEECTL) & SEERDY) == 0)
  {
    aic_outb(p, 0, SEECTL);
    return (0);
  }
  return (1);
}

/*+F*************************************************************************
 * Function:
 *   release_seeprom
 *
 * Description:
 *   Releases access to the memory port on PCI controllers.
 *-F*************************************************************************/
static void
release_seeprom(struct aic7xxx_host *p)
{
  /*
   * Make sure the SEEPROM is ready before we release it.
   */
  CLOCK_PULSE(p);
  aic_outb(p, 0, SEECTL);
}

/*+F*************************************************************************
 * Function:
 *   read_seeprom
 *
 * Description:
 *   Reads the serial EEPROM and returns 1 if successful and 0 if
 *   not successful.
 *
 *   The instruction set of the 93C46/56/66 chips is as follows:
 *
 *               Start  OP
 *     Function   Bit  Code  Address    Data     Description
 *     -------------------------------------------------------------------
 *     READ        1    10   A5 - A0             Reads data stored in memory,
 *                                               starting at specified address
 *     EWEN        1    00   11XXXX              Write enable must precede
 *                                               all programming modes
 *     ERASE       1    11   A5 - A0             Erase register A5A4A3A2A1A0
 *     WRITE       1    01   A5 - A0   D15 - D0  Writes register
 *     ERAL        1    00   10XXXX              Erase all registers
 *     WRAL        1    00   01XXXX    D15 - D0  Writes to all registers
 *     EWDS        1    00   00XXXX              Disables all programming
 *                                               instructions
 *     *Note: A value of X for address is a don't care condition.
 *     *Note: The 93C56 and 93C66 have 8 address bits.
 * 
 *
 *   The 93C46 has a four wire interface: clock, chip select, data in, and
 *   data out.  In order to perform one of the above functions, you need
 *   to enable the chip select for a clock period (typically a minimum of
 *   1 usec, with the clock high and low a minimum of 750 and 250 nsec
 *   respectively.  While the chip select remains high, you can clock in
 *   the instructions (above) starting with the start bit, followed by the
 *   OP code, Address, and Data (if needed).  For the READ instruction, the
 *   requested 16-bit register contents is read from the data out line but
 *   is preceded by an initial zero (leading 0, followed by 16-bits, MSB
 *   first).  The clock cycling from low to high initiates the next data
 *   bit to be sent from the chip.
 *
 *   The 78xx interface to the 93C46 serial EEPROM is through the SEECTL
 *   register.  After successful arbitration for the memory port, the
 *   SEECS bit of the SEECTL register is connected to the chip select.
 *   The SEECK, SEEDO, and SEEDI are connected to the clock, data out,
 *   and data in lines respectively.  The SEERDY bit of SEECTL is useful
 *   in that it gives us an 800 nsec timer.  After a write to the SEECTL
 *   register, the SEERDY goes high 800 nsec later.  The one exception
 *   to this is when we first request access to the memory port.  The
 *   SEERDY goes high to signify that access has been granted and, for
 *   this case, has no implied timing.
 *-F*************************************************************************/
static int
read_seeprom(struct aic7xxx_host *p, int offset, 
    unsigned short *scarray, unsigned int len, seeprom_chip_type chip)
{
  int i = 0, k;
  unsigned char temp;
  unsigned short checksum = 0;
  struct seeprom_cmd {
    unsigned char len;
    unsigned char bits[3];
  };
  struct seeprom_cmd seeprom_read = {3, {1, 1, 0}};

  /*
   * Request access of the memory port.
   */
  if (acquire_seeprom(p) == 0)
  {
    return (0);
  }

  /*
   * Read 'len' registers of the seeprom.  For the 7870, the 93C46
   * SEEPROM is a 1024-bit device with 64 16-bit registers but only
   * the first 32 are used by Adaptec BIOS.  Some adapters use the
   * 93C56 SEEPROM which is a 2048-bit device.  The loop will range
   * from 0 to 'len' - 1.
   */
  for (k = 0; k < len; k++)
  {
    /*
     * Send chip select for one clock cycle.
     */
    aic_outb(p, SEEMS | SEECK | SEECS, SEECTL);
    CLOCK_PULSE(p);

    /*
     * Now we're ready to send the read command followed by the
     * address of the 16-bit register we want to read.
     */
    for (i = 0; i < seeprom_read.len; i++)
    {
      temp = SEEMS | SEECS | (seeprom_read.bits[i] << 1);
      aic_outb(p, temp, SEECTL);
      CLOCK_PULSE(p);
      temp = temp ^ SEECK;
      aic_outb(p, temp, SEECTL);
      CLOCK_PULSE(p);
    }
    /*
     * Send the 6 or 8 bit address (MSB first, LSB last).
     */
    for (i = ((int) chip - 1); i >= 0; i--)
    {
      temp = k + offset;
      temp = (temp >> i) & 1;  /* Mask out all but lower bit. */
      temp = SEEMS | SEECS | (temp << 1);
      aic_outb(p, temp, SEECTL);
      CLOCK_PULSE(p);
      temp = temp ^ SEECK;
      aic_outb(p, temp, SEECTL);
      CLOCK_PULSE(p);
    }

    /*
     * Now read the 16 bit register.  An initial 0 precedes the
     * register contents which begins with bit 15 (MSB) and ends
     * with bit 0 (LSB).  The initial 0 will be shifted off the
     * top of our word as we let the loop run from 0 to 16.
     */
    for (i = 0; i <= 16; i++)
    {
      temp = SEEMS | SEECS;
      aic_outb(p, temp, SEECTL);
      CLOCK_PULSE(p);
      temp = temp ^ SEECK;
      scarray[k] = (scarray[k] << 1) | (aic_inb(p, SEECTL) & SEEDI);
      aic_outb(p, temp, SEECTL);
      CLOCK_PULSE(p);
    }

    /*
     * The serial EEPROM should have a checksum in the last word.
     * Keep a running checksum for all words read except for the
     * last word.  We'll verify the checksum after all words have
     * been read.
     */
    if (k < (len - 1))
    {
      checksum = checksum + scarray[k];
    }

    /*
     * Reset the chip select for the next command cycle.
     */
    aic_outb(p, SEEMS, SEECTL);
    CLOCK_PULSE(p);
    aic_outb(p, SEEMS | SEECK, SEECTL);
    CLOCK_PULSE(p);
    aic_outb(p, SEEMS, SEECTL);
    CLOCK_PULSE(p);
  }

  /*
   * Release access to the memory port and the serial EEPROM.
   */
  release_seeprom(p);

#if 0
  printk("Computed checksum 0x%x, checksum read 0x%x\n",
         checksum, scarray[len - 1]);
  printk("Serial EEPROM:");
  for (k = 0; k < len; k++)
  {
    if (((k % 8) == 0) && (k != 0))
    {
      printk("\n              ");
    }
    printk(" 0x%x", scarray[k]);
  }
  printk("\n");
#endif
  if ( (checksum != scarray[len - 1]) || (checksum == 0) )
  {
    return (0);
  }

  return (1);
}

/*+F*************************************************************************
 * Function:
 *   read_brdctl
 *
 * Description:
 *   Reads the BRDCTL register.
 *-F*************************************************************************/
static unsigned char
read_brdctl(struct aic7xxx_host *p)
{
  unsigned char brdctl, value;

  /*
   * Make sure the SEEPROM is ready before we access it
   */
  CLOCK_PULSE(p);
  if (p->features & AHC_ULTRA2)
  {
    brdctl = BRDRW_ULTRA2;
    aic_outb(p, brdctl, BRDCTL);
    CLOCK_PULSE(p);
    value = aic_inb(p, BRDCTL);
    CLOCK_PULSE(p);
    return(value);
  }
  brdctl = BRDRW;
  if ( !((p->chip & AHC_CHIPID_MASK) == AHC_AIC7895) ||
        (p->flags & AHC_CHNLB) )
  {
    brdctl |= BRDCS;
  }
  aic_outb(p, brdctl, BRDCTL);
  CLOCK_PULSE(p);
  value = aic_inb(p, BRDCTL);
  CLOCK_PULSE(p);
  aic_outb(p, 0, BRDCTL);
  CLOCK_PULSE(p);
  return (value);
}

/*+F*************************************************************************
 * Function:
 *   write_brdctl
 *
 * Description:
 *   Writes a value to the BRDCTL register.
 *-F*************************************************************************/
static void
write_brdctl(struct aic7xxx_host *p, unsigned char value)
{
  unsigned char brdctl;

  /*
   * Make sure the SEEPROM is ready before we access it
   */
  CLOCK_PULSE(p);
  if (p->features & AHC_ULTRA2)
  {
    brdctl = value;
    aic_outb(p, brdctl, BRDCTL);
    CLOCK_PULSE(p);
    brdctl |= BRDSTB_ULTRA2;
    aic_outb(p, brdctl, BRDCTL);
    CLOCK_PULSE(p);
    brdctl &= ~BRDSTB_ULTRA2;
    aic_outb(p, brdctl, BRDCTL);
    CLOCK_PULSE(p);
    read_brdctl(p);
    CLOCK_PULSE(p);
  }
  else
  {
    brdctl = BRDSTB;
    if ( !((p->chip & AHC_CHIPID_MASK) == AHC_AIC7895) ||
          (p->flags & AHC_CHNLB) )
    {
      brdctl |= BRDCS;
    }
    brdctl = BRDSTB | BRDCS;
    aic_outb(p, brdctl, BRDCTL);
    CLOCK_PULSE(p);
    brdctl |= value;
    aic_outb(p, brdctl, BRDCTL);
    CLOCK_PULSE(p);
    brdctl &= ~BRDSTB;
    aic_outb(p, brdctl, BRDCTL);
    CLOCK_PULSE(p);
    brdctl &= ~BRDCS;
    aic_outb(p, brdctl, BRDCTL);
    CLOCK_PULSE(p);
  }
}

/*+F*************************************************************************
 * Function:
 *   aic785x_cable_detect
 *
 * Description:
 *   Detect the cables that are present on aic785x class controller chips
 *-F*************************************************************************/
static void
aic785x_cable_detect(struct aic7xxx_host *p, int *int_50,
    int *ext_present, int *eeprom)
{
  unsigned char brdctl;

  aic_outb(p, BRDRW | BRDCS, BRDCTL);
  CLOCK_PULSE(p);
  aic_outb(p, 0, BRDCTL);
  CLOCK_PULSE(p);
  brdctl = aic_inb(p, BRDCTL);
  CLOCK_PULSE(p);
  *int_50 = !(brdctl & BRDDAT5);
  *ext_present = !(brdctl & BRDDAT6);
  *eeprom = (aic_inb(p, SPIOCAP) & EEPROM);
}

#undef CLOCK_PULSE

/*+F*************************************************************************
 * Function:
 *   aic2940_uwpro_cable_detect
 *
 * Description:
 *   Detect the cables that are present on the 2940-UWPro cards
 *
 * NOTE: This function assumes the SEEPROM will have already been acquired
 *       prior to invocation of this function.
 *-F*************************************************************************/
static void
aic2940_uwpro_wide_cable_detect(struct aic7xxx_host *p, int *int_68,
    int *ext_68, int *eeprom)
{
  unsigned char brdctl;

  /*
   * First read the status of our cables.  Set the rom bank to
   * 0 since the bank setting serves as a multiplexor for the
   * cable detection logic.  BRDDAT5 controls the bank switch.
   */
  write_brdctl(p, 0);

  /*
   * Now we read the state of the internal 68 connector.  BRDDAT6
   * is don't care, BRDDAT7 is internal 68.  The cable is
   * present if the bit is 0
   */
  brdctl = read_brdctl(p);
  *int_68 = !(brdctl & BRDDAT7);

  /*
   * Set the bank bit in brdctl and then read the external cable state
   * and the EEPROM status
   */
  write_brdctl(p, BRDDAT5);
  brdctl = read_brdctl(p);

  *ext_68 = !(brdctl & BRDDAT6);
  *eeprom = !(brdctl & BRDDAT7);

  /*
   * We're done, the calling function will release the SEEPROM for us
   */
}

/*+F*************************************************************************
 * Function:
 *   aic787x_cable_detect
 *
 * Description:
 *   Detect the cables that are present on aic787x class controller chips
 *
 * NOTE: This function assumes the SEEPROM will have already been acquired
 *       prior to invocation of this function.
 *-F*************************************************************************/
static void
aic787x_cable_detect(struct aic7xxx_host *p, int *int_50, int *int_68,
    int *ext_present, int *eeprom)
{
  unsigned char brdctl;

  /*
   * First read the status of our cables.  Set the rom bank to
   * 0 since the bank setting serves as a multiplexor for the
   * cable detection logic.  BRDDAT5 controls the bank switch.
   */
  write_brdctl(p, 0);

  /*
   * Now we read the state of the two internal connectors.  BRDDAT6
   * is internal 50, BRDDAT7 is internal 68.  For each, the cable is
   * present if the bit is 0
   */
  brdctl = read_brdctl(p);
  *int_50 = !(brdctl & BRDDAT6);
  *int_68 = !(brdctl & BRDDAT7);

  /*
   * Set the bank bit in brdctl and then read the external cable state
   * and the EEPROM status
   */
  write_brdctl(p, BRDDAT5);
  brdctl = read_brdctl(p);

  *ext_present = !(brdctl & BRDDAT6);
  *eeprom = !(brdctl & BRDDAT7);

  /*
   * We're done, the calling function will release the SEEPROM for us
   */
}

/*+F*************************************************************************
 * Function:
 *   aic787x_ultra2_term_detect
 *
 * Description:
 *   Detect the termination settings present on ultra2 class controllers
 *
 * NOTE: This function assumes the SEEPROM will have already been acquired
 *       prior to invocation of this function.
 *-F*************************************************************************/
static void
aic7xxx_ultra2_term_detect(struct aic7xxx_host *p, int *enableSE_low,
                           int *enableSE_high, int *enableLVD_low,
                           int *enableLVD_high, int *eprom_present)
{
  unsigned char brdctl;

  brdctl = read_brdctl(p);

  *eprom_present  = (brdctl & BRDDAT7);
  *enableSE_high  = (brdctl & BRDDAT6);
  *enableSE_low   = (brdctl & BRDDAT5);
  *enableLVD_high = (brdctl & BRDDAT4);
  *enableLVD_low  = (brdctl & BRDDAT3);
}

/*+F*************************************************************************
 * Function:
 *   configure_termination
 *
 * Description:
 *   Configures the termination settings on PCI adapters that have
 *   SEEPROMs available.
 *-F*************************************************************************/
static void
configure_termination(struct aic7xxx_host *p)
{
  int internal50_present = 0;
  int internal68_present = 0;
  int external_present = 0;
  int eprom_present = 0;
  int enableSE_low = 0;
  int enableSE_high = 0;
  int enableLVD_low = 0;
  int enableLVD_high = 0;
  unsigned char brddat = 0;
  unsigned char max_target = 0;
  unsigned char sxfrctl1 = aic_inb(p, SXFRCTL1);

  if (acquire_seeprom(p))
  {
    if (p->features & (AHC_WIDE|AHC_TWIN))
      max_target = 16;
    else
      max_target = 8;
    aic_outb(p, SEEMS | SEECS, SEECTL);
    sxfrctl1 &= ~STPWEN;
    /*
     * The termination/cable detection logic is split into three distinct
     * groups.  Ultra2 and later controllers, 2940UW-Pro controllers, and
     * older 7850, 7860, 7870, 7880, and 7895 controllers.  Each has its
     * own unique way of detecting their cables and writing the results
     * back to the card.
     */
    if (p->features & AHC_ULTRA2)
    {
      /*
       * As long as user hasn't overridden term settings, always check the
       * cable detection logic
       */
      if (aic7xxx_override_term == -1)
      {
        aic7xxx_ultra2_term_detect(p, &enableSE_low, &enableSE_high,
                                   &enableLVD_low, &enableLVD_high,
                                   &eprom_present);
      }
      
      /*
       * If the user is overriding settings, then they have been preserved
       * to here as fake adapter_control entries.  Parse them and allow
       * them to override the detected settings (if we even did detection).
       */
      if (!(p->adapter_control & CFSEAUTOTERM))
      {
        enableSE_low = (p->adapter_control & CFSTERM);
        enableSE_high = (p->adapter_control & CFWSTERM);
      }
      if (!(p->adapter_control & CFAUTOTERM))
      {
        enableLVD_low = enableLVD_high = (p->adapter_control & CFLVDSTERM);
      }

      /*
       * Now take those settings that we have and translate them into the
       * values that must be written into the registers.
       *
       * Flash Enable = BRDDAT7
       * Secondary High Term Enable = BRDDAT6
       * Secondary Low Term Enable = BRDDAT5
       * LVD/Primary High Term Enable = BRDDAT4
       * LVD/Primary Low Term Enable = STPWEN bit in SXFRCTL1
       */
      if (enableLVD_low != 0)
      {
        sxfrctl1 |= STPWEN;
        p->flags |= AHC_TERM_ENB_LVD;
        if (aic7xxx_verbose & VERBOSE_PROBE2)
          printk(KERN_INFO "(scsi%d) LVD/Primary Low byte termination "
                 "Enabled\n", p->host_no);
      }
          
      if (enableLVD_high != 0)
      {
        brddat |= BRDDAT4;
        if (aic7xxx_verbose & VERBOSE_PROBE2)
          printk(KERN_INFO "(scsi%d) LVD/Primary High byte termination "
                 "Enabled\n", p->host_no);
      }

      if (enableSE_low != 0)
      {
        brddat |= BRDDAT5;
        if (aic7xxx_verbose & VERBOSE_PROBE2)
          printk(KERN_INFO "(scsi%d) Secondary Low byte termination "
                 "Enabled\n", p->host_no);
      }

      if (enableSE_high != 0)
      {
        brddat |= BRDDAT6;
        if (aic7xxx_verbose & VERBOSE_PROBE2)
          printk(KERN_INFO "(scsi%d) Secondary High byte termination "
                 "Enabled\n", p->host_no);
      }
    }
    else if (p->features & AHC_NEW_AUTOTERM)
    {
      /*
       * The 50 pin connector termination is controlled by STPWEN in the
       * SXFRCTL1 register.  Since the Adaptec docs typically say the
       * controller is not allowed to be in the middle of a cable and
       * this is the only connection on that stub of the bus, there is
       * no need to even check for narrow termination, it's simply
       * always on.
       */
      sxfrctl1 |= STPWEN;
      if (aic7xxx_verbose & VERBOSE_PROBE2)
        printk(KERN_INFO "(scsi%d) Narrow channel termination Enabled\n",
               p->host_no);

      if (p->adapter_control & CFAUTOTERM)
      {
        aic2940_uwpro_wide_cable_detect(p, &internal68_present,
                                        &external_present,
                                        &eprom_present);
        printk(KERN_INFO "(scsi%d) Cables present (Int-50 %s, Int-68 %s, "
               "Ext-68 %s)\n", p->host_no,
               "Don't Care",
               internal68_present ? "YES" : "NO",
               external_present ? "YES" : "NO");
        if (aic7xxx_verbose & VERBOSE_PROBE2)
          printk(KERN_INFO "(scsi%d) EEPROM %s present.\n", p->host_no,
               eprom_present ? "is" : "is not");
        if (internal68_present && external_present)
        {
          brddat = 0;
          p->flags &= ~AHC_TERM_ENB_SE_HIGH;
          if (aic7xxx_verbose & VERBOSE_PROBE2)
            printk(KERN_INFO "(scsi%d) Wide channel termination Disabled\n",
                   p->host_no);
        }
        else
        {
          brddat = BRDDAT6;
          p->flags |= AHC_TERM_ENB_SE_HIGH;
          if (aic7xxx_verbose & VERBOSE_PROBE2)
            printk(KERN_INFO "(scsi%d) Wide channel termination Enabled\n",
                   p->host_no);
        }
      }
      else
      {
        /*
         * The termination of the Wide channel is done more like normal
         * though, and the setting of this termination is done by writing
         * either a 0 or 1 to BRDDAT6 of the BRDDAT register
         */
        if (p->adapter_control & CFWSTERM)
        {
          brddat = BRDDAT6;
          p->flags |= AHC_TERM_ENB_SE_HIGH;
          if (aic7xxx_verbose & VERBOSE_PROBE2)
            printk(KERN_INFO "(scsi%d) Wide channel termination Enabled\n",
                   p->host_no);
        }
        else
        {
          brddat = 0;
        }
      }
    }
    else
    {
      if (p->adapter_control & CFAUTOTERM)
      {
        if (p->flags & AHC_MOTHERBOARD)
        {
          printk(KERN_INFO "(scsi%d) Warning - detected auto-termination\n",
                 p->host_no);
          printk(KERN_INFO "(scsi%d) Please verify driver detected settings "
            "are correct.\n", p->host_no);
          printk(KERN_INFO "(scsi%d) If not, then please properly set the "
            "device termination\n", p->host_no);
          printk(KERN_INFO "(scsi%d) in the Adaptec SCSI BIOS by hitting "
            "CTRL-A when prompted\n", p->host_no);
          printk(KERN_INFO "(scsi%d) during machine bootup.\n", p->host_no);
        }
        /* Configure auto termination. */

        if ( (p->chip & AHC_CHIPID_MASK) >= AHC_AIC7870 )
        {
          aic787x_cable_detect(p, &internal50_present, &internal68_present,
            &external_present, &eprom_present);
        }
        else
        {
          aic785x_cable_detect(p, &internal50_present, &external_present,
            &eprom_present);
        }

        if (max_target <= 8)
          internal68_present = 0;

        if (max_target > 8)
        {
          printk(KERN_INFO "(scsi%d) Cables present (Int-50 %s, Int-68 %s, "
                 "Ext-68 %s)\n", p->host_no,
                 internal50_present ? "YES" : "NO",
                 internal68_present ? "YES" : "NO",
                 external_present ? "YES" : "NO");
        }
        else
        {
          printk(KERN_INFO "(scsi%d) Cables present (Int-50 %s, Ext-50 %s)\n",
                 p->host_no,
                 internal50_present ? "YES" : "NO",
                 external_present ? "YES" : "NO");
        }
        if (aic7xxx_verbose & VERBOSE_PROBE2)
          printk(KERN_INFO "(scsi%d) EEPROM %s present.\n", p->host_no,
               eprom_present ? "is" : "is not");

        /*
         * Now set the termination based on what we found.  BRDDAT6
         * controls wide termination enable.
         * Flash Enable = BRDDAT7
         * SE High Term Enable = BRDDAT6
         */
        if (internal50_present && internal68_present && external_present)
        {
          printk(KERN_INFO "(scsi%d) Illegal cable configuration!!  Only two\n",
                 p->host_no);
          printk(KERN_INFO "(scsi%d) connectors on the SCSI controller may be "
                 "in use at a time!\n", p->host_no);
          /*
           * Force termination (low and high byte) on.  This is safer than
           * leaving it completely off, especially since this message comes
           * most often from motherboard controllers that don't even have 3
           * connectors, but instead are failing the cable detection.
           */
          internal50_present = external_present = 0;
          enableSE_high = enableSE_low = 1;
        }

        if ((max_target > 8) &&
            ((external_present == 0) || (internal68_present == 0)) )
        {
          brddat |= BRDDAT6;
          p->flags |= AHC_TERM_ENB_SE_HIGH;
          if (aic7xxx_verbose & VERBOSE_PROBE2)
            printk(KERN_INFO "(scsi%d) SE High byte termination Enabled\n",
                   p->host_no);
        }

        if ( ((internal50_present ? 1 : 0) +
              (internal68_present ? 1 : 0) +
              (external_present   ? 1 : 0)) <= 1 )
        {
          sxfrctl1 |= STPWEN;
          p->flags |= AHC_TERM_ENB_SE_LOW;
          if (aic7xxx_verbose & VERBOSE_PROBE2)
            printk(KERN_INFO "(scsi%d) SE Low byte termination Enabled\n",
                   p->host_no);
        }
      }
      else /* p->adapter_control & CFAUTOTERM */
      {
        if (p->adapter_control & CFSTERM)
        {
          sxfrctl1 |= STPWEN;
          if (aic7xxx_verbose & VERBOSE_PROBE2)
            printk(KERN_INFO "(scsi%d) SE Low byte termination Enabled\n",
                   p->host_no);
        }

        if (p->adapter_control & CFWSTERM)
        {
          brddat |= BRDDAT6;
          if (aic7xxx_verbose & VERBOSE_PROBE2)
            printk(KERN_INFO "(scsi%d) SE High byte termination Enabled\n",
                   p->host_no);
        }
      }
    }

    aic_outb(p, sxfrctl1, SXFRCTL1);
    write_brdctl(p, brddat);
    release_seeprom(p);
  }
}

/*+F*************************************************************************
 * Function:
 *   detect_maxscb
 *
 * Description:
 *   Detects the maximum number of SCBs for the controller and returns
 *   the count and a mask in p (p->maxscbs, p->qcntmask).
 *-F*************************************************************************/
static void
detect_maxscb(struct aic7xxx_host *p)
{
  int i;

  /*
   * It's possible that we've already done this for multichannel
   * adapters.
   */
  if (p->scb_data->maxhscbs == 0)
  {
    /*
     * We haven't initialized the SCB settings yet.  Walk the SCBs to
     * determince how many there are.
     */
    aic_outb(p, 0, FREE_SCBH);

    for (i = 0; i < AIC7XXX_MAXSCB; i++)
    {
      aic_outb(p, i, SCBPTR);
      aic_outb(p, i, SCB_CONTROL);
      if (aic_inb(p, SCB_CONTROL) != i)
        break;
      aic_outb(p, 0, SCBPTR);
      if (aic_inb(p, SCB_CONTROL) != 0)
        break;

      aic_outb(p, i, SCBPTR);
      aic_outb(p, 0, SCB_CONTROL);   /* Clear the control byte. */
      aic_outb(p, i + 1, SCB_NEXT);  /* Set the next pointer. */
      aic_outb(p, SCB_LIST_NULL, SCB_TAG);  /* Make the tag invalid. */
      aic_outb(p, SCB_LIST_NULL, SCB_BUSYTARGETS);  /* no busy untagged */
      aic_outb(p, SCB_LIST_NULL, SCB_BUSYTARGETS+1);/* targets active yet */
      aic_outb(p, SCB_LIST_NULL, SCB_BUSYTARGETS+2);
      aic_outb(p, SCB_LIST_NULL, SCB_BUSYTARGETS+3);
    }

    /* Make sure the last SCB terminates the free list. */
    aic_outb(p, i - 1, SCBPTR);
    aic_outb(p, SCB_LIST_NULL, SCB_NEXT);

    /* Ensure we clear the first (0) SCBs control byte. */
    aic_outb(p, 0, SCBPTR);
    aic_outb(p, 0, SCB_CONTROL);

    p->scb_data->maxhscbs = i;
    /*
     * Use direct indexing instead for speed
     */
    if ( i == AIC7XXX_MAXSCB )
      p->flags &= ~AHC_PAGESCBS;
  }

}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_register
 *
 * Description:
 *   Register a Adaptec aic7xxx chip SCSI controller with the kernel.
 *-F*************************************************************************/
static int
aic7xxx_register(struct scsi_host_template *template, struct aic7xxx_host *p,
  int reset_delay)
{
  int i, result;
  int max_targets;
  int found = 1;
  unsigned char term, scsi_conf;
  struct Scsi_Host *host;

  host = p->host;

  p->scb_data->maxscbs = AIC7XXX_MAXSCB;
  host->can_queue = AIC7XXX_MAXSCB;
  host->cmd_per_lun = 3;
  host->sg_tablesize = AIC7XXX_MAX_SG;
  host->this_id = p->scsi_id;
  host->io_port = p->base;
  host->n_io_port = 0xFF;
  host->base = p->mbase;
  host->irq = p->irq;
  if (p->features & AHC_WIDE)
  {
    host->max_id = 16;
  }
  if (p->features & AHC_TWIN)
  {
    host->max_channel = 1;
  }

  p->host = host;
  p->host_no = host->host_no;
  host->unique_id = p->instance;
  p->isr_count = 0;
  p->next = NULL;
  p->completeq.head = NULL;
  p->completeq.tail = NULL;
  scbq_init(&p->scb_data->free_scbs);
  scbq_init(&p->waiting_scbs);
  INIT_LIST_HEAD(&p->aic_devs);

  /*
   * We currently have no commands of any type
   */
  p->qinfifonext = 0;
  p->qoutfifonext = 0;

  printk(KERN_INFO "(scsi%d) <%s> found at ", p->host_no,
    board_names[p->board_name_index]);
  switch(p->chip)
  {
    case (AHC_AIC7770|AHC_EISA):
      printk("EISA slot %d\n", p->pci_device_fn);
      break;
    case (AHC_AIC7770|AHC_VL):
      printk("VLB slot %d\n", p->pci_device_fn);
      break;
    default:
      printk("PCI %d/%d/%d\n", p->pci_bus, PCI_SLOT(p->pci_device_fn),
        PCI_FUNC(p->pci_device_fn));
      break;
  }
  if (p->features & AHC_TWIN)
  {
    printk(KERN_INFO "(scsi%d) Twin Channel, A SCSI ID %d, B SCSI ID %d, ",
           p->host_no, p->scsi_id, p->scsi_id_b);
  }
  else
  {
    char *channel;

    channel = "";

    if ((p->flags & AHC_MULTI_CHANNEL) != 0)
    {
      channel = " A";

      if ( (p->flags & (AHC_CHNLB|AHC_CHNLC)) != 0 )
      {
        channel = (p->flags & AHC_CHNLB) ? " B" : " C";
      }
    }
    if (p->features & AHC_WIDE)
    {
      printk(KERN_INFO "(scsi%d) Wide ", p->host_no);
    }
    else
    {
      printk(KERN_INFO "(scsi%d) Narrow ", p->host_no);
    }
    printk("Channel%s, SCSI ID=%d, ", channel, p->scsi_id);
  }
  aic_outb(p, 0, SEQ_FLAGS);

  detect_maxscb(p);

  printk("%d/%d SCBs\n", p->scb_data->maxhscbs, p->scb_data->maxscbs);
  if (aic7xxx_verbose & VERBOSE_PROBE2)
  {
    printk(KERN_INFO "(scsi%d) BIOS %sabled, IO Port 0x%lx, IRQ %d\n",
      p->host_no, (p->flags & AHC_BIOS_ENABLED) ? "en" : "dis",
      p->base, p->irq);
    printk(KERN_INFO "(scsi%d) IO Memory at 0x%lx, MMAP Memory at %p\n",
      p->host_no, p->mbase, p->maddr);
  }

#ifdef CONFIG_PCI
  /*
   * Now that we know our instance number, we can set the flags we need to
   * force termination if need be.
   */
  if (aic7xxx_stpwlev != -1)
  {
    /*
     * This option only applies to PCI controllers.
     */
    if ( (p->chip & ~AHC_CHIPID_MASK) == AHC_PCI)
    {
      unsigned char devconfig;

      pci_read_config_byte(p->pdev, DEVCONFIG, &devconfig);
      if ( (aic7xxx_stpwlev >> p->instance) & 0x01 )
      {
        devconfig |= STPWLEVEL;
        if (aic7xxx_verbose & VERBOSE_PROBE2)
          printk("(scsi%d) Force setting STPWLEVEL bit\n", p->host_no);
      }
      else
      {
        devconfig &= ~STPWLEVEL;
        if (aic7xxx_verbose & VERBOSE_PROBE2)
          printk("(scsi%d) Force clearing STPWLEVEL bit\n", p->host_no);
      }
      pci_write_config_byte(p->pdev, DEVCONFIG, devconfig);
    }
  }
#endif

  /*
   * That took care of devconfig and stpwlev, now for the actual termination
   * settings.
   */
  if (aic7xxx_override_term != -1)
  {
    /*
     * Again, this only applies to PCI controllers.  We don't have problems
     * with the termination on 274x controllers to the best of my knowledge.
     */
    if ( (p->chip & ~AHC_CHIPID_MASK) == AHC_PCI)
    {
      unsigned char term_override;

      term_override = ( (aic7xxx_override_term >> (p->instance * 4)) & 0x0f);
      p->adapter_control &= 
        ~(CFSTERM|CFWSTERM|CFLVDSTERM|CFAUTOTERM|CFSEAUTOTERM);
      if ( (p->features & AHC_ULTRA2) && (term_override & 0x0c) )
      {
        p->adapter_control |= CFLVDSTERM;
      }
      if (term_override & 0x02)
      {
        p->adapter_control |= CFWSTERM;
      }
      if (term_override & 0x01)
      {
        p->adapter_control |= CFSTERM;
      }
    }
  }

  if ( (p->flags & AHC_SEEPROM_FOUND) || (aic7xxx_override_term != -1) )
  {
    if (p->features & AHC_SPIOCAP)
    {
      if ( aic_inb(p, SPIOCAP) & SSPIOCPS )
      /*
       * Update the settings in sxfrctl1 to match the termination
       * settings.
       */
        configure_termination(p);
    }
    else if ((p->chip & AHC_CHIPID_MASK) >= AHC_AIC7870)
    {
      configure_termination(p);
    }
  }

  /*
   * Set the SCSI Id, SXFRCTL0, SXFRCTL1, and SIMODE1, for both channels
   */
  if (p->features & AHC_TWIN)
  {
    /* Select channel B */
    aic_outb(p, aic_inb(p, SBLKCTL) | SELBUSB, SBLKCTL);

    if ((p->flags & AHC_SEEPROM_FOUND) || (aic7xxx_override_term != -1))
      term = (aic_inb(p, SXFRCTL1) & STPWEN);
    else
      term = ((p->flags & AHC_TERM_ENB_B) ? STPWEN : 0);

    aic_outb(p, p->scsi_id_b, SCSIID);
    scsi_conf = aic_inb(p, SCSICONF + 1);
    aic_outb(p, DFON | SPIOEN, SXFRCTL0);
    aic_outb(p, (scsi_conf & ENSPCHK) | aic7xxx_seltime | term | 
         ENSTIMER | ACTNEGEN, SXFRCTL1);
    aic_outb(p, 0, SIMODE0);
    aic_outb(p, ENSELTIMO | ENSCSIRST | ENSCSIPERR, SIMODE1);
    aic_outb(p, 0, SCSIRATE);

    /* Select channel A */
    aic_outb(p, aic_inb(p, SBLKCTL) & ~SELBUSB, SBLKCTL);
  }

  if (p->features & AHC_ULTRA2)
  {
    aic_outb(p, p->scsi_id, SCSIID_ULTRA2);
  }
  else
  {
    aic_outb(p, p->scsi_id, SCSIID);
  }
  if ((p->flags & AHC_SEEPROM_FOUND) || (aic7xxx_override_term != -1))
    term = (aic_inb(p, SXFRCTL1) & STPWEN);
  else
    term = ((p->flags & (AHC_TERM_ENB_A|AHC_TERM_ENB_LVD)) ? STPWEN : 0);
  scsi_conf = aic_inb(p, SCSICONF);
  aic_outb(p, DFON | SPIOEN, SXFRCTL0);
  aic_outb(p, (scsi_conf & ENSPCHK) | aic7xxx_seltime | term | 
       ENSTIMER | ACTNEGEN, SXFRCTL1);
  aic_outb(p, 0, SIMODE0);
  /*
   * If we are a cardbus adapter then don't enable SCSI reset detection.
   * We shouldn't likely be sharing SCSI busses with someone else, and
   * if we don't have a cable currently plugged into the controller then
   * we won't have a power source for the SCSI termination, which means
   * we'll see infinite incoming bus resets.
   */
  if(p->flags & AHC_NO_STPWEN)
    aic_outb(p, ENSELTIMO | ENSCSIPERR, SIMODE1);
  else
    aic_outb(p, ENSELTIMO | ENSCSIRST | ENSCSIPERR, SIMODE1);
  aic_outb(p, 0, SCSIRATE);
  if ( p->features & AHC_ULTRA2)
    aic_outb(p, 0, SCSIOFFSET);

  /*
   * Look at the information that board initialization or the board
   * BIOS has left us. In the lower four bits of each target's
   * scratch space any value other than 0 indicates that we should
   * initiate synchronous transfers. If it's zero, the user or the
   * BIOS has decided to disable synchronous negotiation to that
   * target so we don't activate the needsdtr flag.
   */
  if ((p->features & (AHC_TWIN|AHC_WIDE)) == 0)
  {
    max_targets = 8;
  }
  else
  {
    max_targets = 16;
  }

  if (!(aic7xxx_no_reset))
  {
    /*
     * If we reset the bus, then clear the transfer settings, else leave
     * them be.
     */
    aic_outb(p, 0, ULTRA_ENB);
    aic_outb(p, 0, ULTRA_ENB + 1);
    p->ultraenb = 0;
  }

  /*
   * Allocate enough hardware scbs to handle the maximum number of
   * concurrent transactions we can have.  We have to make sure that
   * the allocated memory is contiguous memory.  The Linux kmalloc
   * routine should only allocate contiguous memory, but note that
   * this could be a problem if kmalloc() is changed.
   */
  {
    size_t array_size;
    unsigned int hscb_physaddr;

    array_size = p->scb_data->maxscbs * sizeof(struct aic7xxx_hwscb);
    if (p->scb_data->hscbs == NULL)
    {
      /* pci_alloc_consistent enforces the alignment already and
       * clears the area as well.
       */
      p->scb_data->hscbs = pci_alloc_consistent(p->pdev, array_size,
						&p->scb_data->hscbs_dma);
      /* We have to use pci_free_consistent, not kfree */
      p->scb_data->hscb_kmalloc_ptr = NULL;
      p->scb_data->hscbs_dma_len = array_size;
    }
    if (p->scb_data->hscbs == NULL)
    {
      printk("(scsi%d) Unable to allocate hardware SCB array; "
             "failing detection.\n", p->host_no);
      aic_outb(p, 0, SIMODE1);
      p->irq = 0;
      return(0);
    }

    hscb_physaddr = p->scb_data->hscbs_dma;
    aic_outb(p, hscb_physaddr & 0xFF, HSCB_ADDR);
    aic_outb(p, (hscb_physaddr >> 8) & 0xFF, HSCB_ADDR + 1);
    aic_outb(p, (hscb_physaddr >> 16) & 0xFF, HSCB_ADDR + 2);
    aic_outb(p, (hscb_physaddr >> 24) & 0xFF, HSCB_ADDR + 3);

    /* Set up the fifo areas at the same time */
    p->untagged_scbs = pci_alloc_consistent(p->pdev, 3*256, &p->fifo_dma);
    if (p->untagged_scbs == NULL)
    {
      printk("(scsi%d) Unable to allocate hardware FIFO arrays; "
             "failing detection.\n", p->host_no);
      p->irq = 0;
      return(0);
    }

    p->qoutfifo = p->untagged_scbs + 256;
    p->qinfifo = p->qoutfifo + 256;
    for (i = 0; i < 256; i++)
    {
      p->untagged_scbs[i] = SCB_LIST_NULL;
      p->qinfifo[i] = SCB_LIST_NULL;
      p->qoutfifo[i] = SCB_LIST_NULL;
    }

    hscb_physaddr = p->fifo_dma;
    aic_outb(p, hscb_physaddr & 0xFF, SCBID_ADDR);
    aic_outb(p, (hscb_physaddr >> 8) & 0xFF, SCBID_ADDR + 1);
    aic_outb(p, (hscb_physaddr >> 16) & 0xFF, SCBID_ADDR + 2);
    aic_outb(p, (hscb_physaddr >> 24) & 0xFF, SCBID_ADDR + 3);
  }

  /* The Q-FIFOs we just set up are all empty */
  aic_outb(p, 0, QINPOS);
  aic_outb(p, 0, KERNEL_QINPOS);
  aic_outb(p, 0, QOUTPOS);

  if(p->features & AHC_QUEUE_REGS)
  {
    aic_outb(p, SCB_QSIZE_256, QOFF_CTLSTA);
    aic_outb(p, 0, SDSCB_QOFF);
    aic_outb(p, 0, SNSCB_QOFF);
    aic_outb(p, 0, HNSCB_QOFF);
  }

  /*
   * We don't have any waiting selections or disconnected SCBs.
   */
  aic_outb(p, SCB_LIST_NULL, WAITING_SCBH);
  aic_outb(p, SCB_LIST_NULL, DISCONNECTED_SCBH);

  /*
   * Message out buffer starts empty
   */
  aic_outb(p, MSG_NOOP, MSG_OUT);
  aic_outb(p, MSG_NOOP, LAST_MSG);

  /*
   * Set all the other asundry items that haven't been set yet.
   * This includes just dumping init values to a lot of registers simply
   * to make sure they've been touched and are ready for use parity wise
   * speaking.
   */
  aic_outb(p, 0, TMODE_CMDADDR);
  aic_outb(p, 0, TMODE_CMDADDR + 1);
  aic_outb(p, 0, TMODE_CMDADDR + 2);
  aic_outb(p, 0, TMODE_CMDADDR + 3);
  aic_outb(p, 0, TMODE_CMDADDR_NEXT);

  /*
   * Link us into the list of valid hosts
   */
  p->next = first_aic7xxx;
  first_aic7xxx = p;

  /*
   * Allocate the first set of scbs for this controller.  This is to stream-
   * line code elsewhere in the driver.  If we have to check for the existence
   * of scbs in certain code sections, it slows things down.  However, as
   * soon as we register the IRQ for this card, we could get an interrupt that
   * includes possibly the SCSI_RSTI interrupt.  If we catch that interrupt
   * then we are likely to segfault if we don't have at least one chunk of
   * SCBs allocated or add checks all through the reset code to make sure
   * that the SCBs have been allocated which is an invalid running condition
   * and therefore I think it's preferable to simply pre-allocate the first
   * chunk of SCBs.
   */
  aic7xxx_allocate_scb(p);

  /*
   * Load the sequencer program, then re-enable the board -
   * resetting the AIC-7770 disables it, leaving the lights
   * on with nobody home.
   */
  aic7xxx_loadseq(p);

  /*
   * Make sure the AUTOFLUSHDIS bit is *not* set in the SBLKCTL register
   */
  aic_outb(p, aic_inb(p, SBLKCTL) & ~AUTOFLUSHDIS, SBLKCTL);

  if ( (p->chip & AHC_CHIPID_MASK) == AHC_AIC7770 )
  {
    aic_outb(p, ENABLE, BCTL);  /* Enable the boards BUS drivers. */
  }

  if ( !(aic7xxx_no_reset) )
  {
    if (p->features & AHC_TWIN)
    {
      if (aic7xxx_verbose & VERBOSE_PROBE2)
        printk(KERN_INFO "(scsi%d) Resetting channel B\n", p->host_no);
      aic_outb(p, aic_inb(p, SBLKCTL) | SELBUSB, SBLKCTL);
      aic7xxx_reset_current_bus(p);
      aic_outb(p, aic_inb(p, SBLKCTL) & ~SELBUSB, SBLKCTL);
    }
    /* Reset SCSI bus A. */
    if (aic7xxx_verbose & VERBOSE_PROBE2)
    {  /* In case we are a 3940, 3985, or 7895, print the right channel */
      char *channel = "";
      if (p->flags & AHC_MULTI_CHANNEL)
      {
        channel = " A";
        if (p->flags & (AHC_CHNLB|AHC_CHNLC))
          channel = (p->flags & AHC_CHNLB) ? " B" : " C";
      }
      printk(KERN_INFO "(scsi%d) Resetting channel%s\n", p->host_no, channel);
    }
    
    aic7xxx_reset_current_bus(p);

  }
  else
  {
    if (!reset_delay)
    {
      printk(KERN_INFO "(scsi%d) Not resetting SCSI bus.  Note: Don't use "
             "the no_reset\n", p->host_no);
      printk(KERN_INFO "(scsi%d) option unless you have a verifiable need "
             "for it.\n", p->host_no);
    }
  }
  
  /*
   * Register IRQ with the kernel.  Only allow sharing IRQs with
   * PCI devices.
   */
  if (!(p->chip & AHC_PCI))
  {
    result = (request_irq(p->irq, do_aic7xxx_isr, 0, "aic7xxx", p));
  }
  else
  {
    result = (request_irq(p->irq, do_aic7xxx_isr, IRQF_SHARED,
              "aic7xxx", p));
    if (result < 0)
    {
      result = (request_irq(p->irq, do_aic7xxx_isr, IRQF_DISABLED | IRQF_SHARED,
              "aic7xxx", p));
    }
  }
  if (result < 0)
  {
    printk(KERN_WARNING "(scsi%d) Couldn't register IRQ %d, ignoring "
           "controller.\n", p->host_no, p->irq);
    aic_outb(p, 0, SIMODE1);
    p->irq = 0;
    return (0);
  }

  if(aic_inb(p, INTSTAT) & INT_PEND)
    printk(INFO_LEAD "spurious interrupt during configuration, cleared.\n",
      p->host_no, -1, -1 , -1);
  aic7xxx_clear_intstat(p);

  unpause_sequencer(p, /* unpause_always */ TRUE);

  return (found);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_chip_reset
 *
 * Description:
 *   Perform a chip reset on the aic7xxx SCSI controller.  The controller
 *   is paused upon return.
 *-F*************************************************************************/
static int
aic7xxx_chip_reset(struct aic7xxx_host *p)
{
  unsigned char sblkctl;
  int wait;

  /*
   * For some 274x boards, we must clear the CHIPRST bit and pause
   * the sequencer. For some reason, this makes the driver work.
   */
  aic_outb(p, PAUSE | CHIPRST, HCNTRL);

  /*
   * In the future, we may call this function as a last resort for
   * error handling.  Let's be nice and not do any unnecessary delays.
   */
  wait = 1000;  /* 1 msec (1000 * 1 msec) */
  while (--wait && !(aic_inb(p, HCNTRL) & CHIPRSTACK))
  {
    udelay(1);  /* 1 usec */
  }

  pause_sequencer(p);

  sblkctl = aic_inb(p, SBLKCTL) & (SELBUSB|SELWIDE);
  if (p->chip & AHC_PCI)
    sblkctl &= ~SELBUSB;
  switch( sblkctl )
  {
    case 0:  /* normal narrow card */
      break;
    case 2:  /* Wide card */
      p->features |= AHC_WIDE;
      break;
    case 8:  /* Twin card */
      p->features |= AHC_TWIN;
      p->flags |= AHC_MULTI_CHANNEL;
      break;
    default: /* hmmm...we don't know what this is */
      printk(KERN_WARNING "aic7xxx: Unsupported adapter type %d, ignoring.\n",
        aic_inb(p, SBLKCTL) & 0x0a);
      return(-1);
  }
  return(0);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_alloc
 *
 * Description:
 *   Allocate and initialize a host structure.  Returns NULL upon error
 *   and a pointer to a aic7xxx_host struct upon success.
 *-F*************************************************************************/
static struct aic7xxx_host *
aic7xxx_alloc(struct scsi_host_template *sht, struct aic7xxx_host *temp)
{
  struct aic7xxx_host *p = NULL;
  struct Scsi_Host *host;

  /*
   * Allocate a storage area by registering us with the mid-level
   * SCSI layer.
   */
  host = scsi_register(sht, sizeof(struct aic7xxx_host));

  if (host != NULL)
  {
    p = (struct aic7xxx_host *) host->hostdata;
    memset(p, 0, sizeof(struct aic7xxx_host));
    *p = *temp;
    p->host = host;

    p->scb_data = kzalloc(sizeof(scb_data_type), GFP_ATOMIC);
    if (p->scb_data)
    {
      scbq_init (&p->scb_data->free_scbs);
    }
    else
    {
      /*
       * For some reason we don't have enough memory.  Free the
       * allocated memory for the aic7xxx_host struct, and return NULL.
       */
      release_region(p->base, MAXREG - MINREG);
      scsi_unregister(host);
      return(NULL);
    }
    p->host_no = host->host_no;
  }
  return (p);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_free
 *
 * Description:
 *   Frees and releases all resources associated with an instance of
 *   the driver (struct aic7xxx_host *).
 *-F*************************************************************************/
static void
aic7xxx_free(struct aic7xxx_host *p)
{
  int i;

  /*
   * Free the allocated hardware SCB space.
   */
  if (p->scb_data != NULL)
  {
    struct aic7xxx_scb_dma *scb_dma = NULL;
    if (p->scb_data->hscbs != NULL)
    {
      pci_free_consistent(p->pdev, p->scb_data->hscbs_dma_len,
			  p->scb_data->hscbs, p->scb_data->hscbs_dma);
      p->scb_data->hscbs = p->scb_data->hscb_kmalloc_ptr = NULL;
    }
    /*
     * Free the driver SCBs.  These were allocated on an as-need
     * basis.  We allocated these in groups depending on how many
     * we could fit into a given amount of RAM.  The tail SCB for
     * these allocations has a pointer to the alloced area.
     */
    for (i = 0; i < p->scb_data->numscbs; i++)
    {
      if (p->scb_data->scb_array[i]->scb_dma != scb_dma)
      {
	scb_dma = p->scb_data->scb_array[i]->scb_dma;
	pci_free_consistent(p->pdev, scb_dma->dma_len,
			    (void *)((unsigned long)scb_dma->dma_address
                                     - scb_dma->dma_offset),
			    scb_dma->dma_address);
      }
      kfree(p->scb_data->scb_array[i]->kmalloc_ptr);
      p->scb_data->scb_array[i] = NULL;
    }
  
    /*
     * Free the SCB data area.
     */
    kfree(p->scb_data);
  }

  pci_free_consistent(p->pdev, 3*256, (void *)p->untagged_scbs, p->fifo_dma);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_load_seeprom
 *
 * Description:
 *   Load the seeprom and configure adapter and target settings.
 *   Returns 1 if the load was successful and 0 otherwise.
 *-F*************************************************************************/
static void
aic7xxx_load_seeprom(struct aic7xxx_host *p, unsigned char *sxfrctl1)
{
  int have_seeprom = 0;
  int i, max_targets, mask;
  unsigned char scsirate, scsi_conf;
  unsigned short scarray[128];
  struct seeprom_config *sc = (struct seeprom_config *) scarray;

  if (aic7xxx_verbose & VERBOSE_PROBE2)
  {
    printk(KERN_INFO "aic7xxx: Loading serial EEPROM...");
  }
  switch (p->chip)
  {
    case (AHC_AIC7770|AHC_EISA):  /* None of these adapters have seeproms. */
      if (aic_inb(p, SCSICONF) & TERM_ENB)
        p->flags |= AHC_TERM_ENB_A;
      if ( (p->features & AHC_TWIN) && (aic_inb(p, SCSICONF + 1) & TERM_ENB) )
        p->flags |= AHC_TERM_ENB_B;
      break;

    case (AHC_AIC7770|AHC_VL):
      have_seeprom = read_284x_seeprom(p, (struct seeprom_config *) scarray);
      break;

    default:
      have_seeprom = read_seeprom(p, (p->flags & (AHC_CHNLB|AHC_CHNLC)),
                                  scarray, p->sc_size, p->sc_type);
      if (!have_seeprom)
      {
        if(p->sc_type == C46)
          have_seeprom = read_seeprom(p, (p->flags & (AHC_CHNLB|AHC_CHNLC)),
                                      scarray, p->sc_size, C56_66);
        else
          have_seeprom = read_seeprom(p, (p->flags & (AHC_CHNLB|AHC_CHNLC)),
                                      scarray, p->sc_size, C46);
      }
      if (!have_seeprom)
      {
        p->sc_size = 128;
        have_seeprom = read_seeprom(p, 4*(p->flags & (AHC_CHNLB|AHC_CHNLC)),
                                    scarray, p->sc_size, p->sc_type);
        if (!have_seeprom)
        {
          if(p->sc_type == C46)
            have_seeprom = read_seeprom(p, 4*(p->flags & (AHC_CHNLB|AHC_CHNLC)),
                                        scarray, p->sc_size, C56_66);
          else
            have_seeprom = read_seeprom(p, 4*(p->flags & (AHC_CHNLB|AHC_CHNLC)),
                                        scarray, p->sc_size, C46);
        }
      }
      break;
  }

  if (!have_seeprom)
  {
    if (aic7xxx_verbose & VERBOSE_PROBE2)
    {
      printk("\naic7xxx: No SEEPROM available.\n");
    }
    p->flags |= AHC_NEWEEPROM_FMT;
    if (aic_inb(p, SCSISEQ) == 0)
    {
      p->flags |= AHC_USEDEFAULTS;
      p->flags &= ~AHC_BIOS_ENABLED;
      p->scsi_id = p->scsi_id_b = 7;
      *sxfrctl1 |= STPWEN;
      if (aic7xxx_verbose & VERBOSE_PROBE2)
      {
        printk("aic7xxx: Using default values.\n");
      }
    }
    else if (aic7xxx_verbose & VERBOSE_PROBE2)
    {
      printk("aic7xxx: Using leftover BIOS values.\n");
    }
    if ( ((p->chip & ~AHC_CHIPID_MASK) == AHC_PCI) && (*sxfrctl1 & STPWEN) )
    {
      p->flags |= AHC_TERM_ENB_SE_LOW | AHC_TERM_ENB_SE_HIGH;
      sc->adapter_control &= ~CFAUTOTERM;
      sc->adapter_control |= CFSTERM | CFWSTERM | CFLVDSTERM;
    }
    if (aic7xxx_extended)
      p->flags |= (AHC_EXTEND_TRANS_A | AHC_EXTEND_TRANS_B);
    else
      p->flags &= ~(AHC_EXTEND_TRANS_A | AHC_EXTEND_TRANS_B);
  }
  else
  {
    if (aic7xxx_verbose & VERBOSE_PROBE2)
    {
      printk("done\n");
    }

    /*
     * Note things in our flags
     */
    p->flags |= AHC_SEEPROM_FOUND;

    /*
     * Update the settings in sxfrctl1 to match the termination settings.
     */
    *sxfrctl1 = 0;

    /*
     * Get our SCSI ID from the SEEPROM setting...
     */
    p->scsi_id = (sc->brtime_id & CFSCSIID);

    /*
     * First process the settings that are different between the VLB
     * and PCI adapter seeproms.
     */
    if ((p->chip & AHC_CHIPID_MASK) == AHC_AIC7770)
    {
      /* VLB adapter seeproms */
      if (sc->bios_control & CF284XEXTEND)
        p->flags |= AHC_EXTEND_TRANS_A;

      if (sc->adapter_control & CF284XSTERM)
      {
        *sxfrctl1 |= STPWEN;
        p->flags |= AHC_TERM_ENB_SE_LOW | AHC_TERM_ENB_SE_HIGH;
      }
    }
    else
    {
      /* PCI adapter seeproms */
      if (sc->bios_control & CFEXTEND)
        p->flags |= AHC_EXTEND_TRANS_A;
      if (sc->bios_control & CFBIOSEN)
        p->flags |= AHC_BIOS_ENABLED;
      else
        p->flags &= ~AHC_BIOS_ENABLED;

      if (sc->adapter_control & CFSTERM)
      {
        *sxfrctl1 |= STPWEN;
        p->flags |= AHC_TERM_ENB_SE_LOW | AHC_TERM_ENB_SE_HIGH;
      }
    }
    memcpy(&p->sc, sc, sizeof(struct seeprom_config));
  }

  p->discenable = 0;

  /*
   * Limit to 16 targets just in case.  The 2842 for one is known to
   * blow the max_targets setting, future cards might also.
   */
  max_targets = ((p->features & (AHC_TWIN | AHC_WIDE)) ? 16 : 8);

  if (have_seeprom)
  {
    for (i = 0; i < max_targets; i++)
    {
      if( ((p->features & AHC_ULTRA) &&
          !(sc->adapter_control & CFULTRAEN) &&
           (sc->device_flags[i] & CFSYNCHISULTRA)) ||
          (sc->device_flags[i] & CFNEWULTRAFORMAT) )
      {
        p->flags |= AHC_NEWEEPROM_FMT;
        break;
      }
    }
  }

  for (i = 0; i < max_targets; i++)
  {
    mask = (0x01 << i);
    if (!have_seeprom)
    {
      if (aic_inb(p, SCSISEQ) != 0)
      {
        /*
         * OK...the BIOS set things up and left behind the settings we need.
         * Just make our sc->device_flags[i] entry match what the card has
         * set for this device.
         */
	p->discenable =
	  ~(aic_inb(p, DISC_DSB) | (aic_inb(p, DISC_DSB + 1) << 8) );
        p->ultraenb =
          (aic_inb(p, ULTRA_ENB) | (aic_inb(p, ULTRA_ENB + 1) << 8) );
	sc->device_flags[i] = (p->discenable & mask) ? CFDISC : 0;
        if (aic_inb(p, TARG_SCSIRATE + i) & WIDEXFER)
          sc->device_flags[i] |= CFWIDEB;
        if (p->features & AHC_ULTRA2)
        {
          if (aic_inb(p, TARG_OFFSET + i))
          {
            sc->device_flags[i] |= CFSYNCH;
            sc->device_flags[i] |= (aic_inb(p, TARG_SCSIRATE + i) & 0x07);
            if ( (aic_inb(p, TARG_SCSIRATE + i) & 0x18) == 0x18 )
              sc->device_flags[i] |= CFSYNCHISULTRA;
          }
        }
        else
        {
          if (aic_inb(p, TARG_SCSIRATE + i) & ~WIDEXFER)
          {
            sc->device_flags[i] |= CFSYNCH;
            if (p->features & AHC_ULTRA)
              sc->device_flags[i] |= ((p->ultraenb & mask) ?
                                      CFSYNCHISULTRA : 0);
          }
        }
      }
      else
      {
        /*
         * Assume the BIOS has NOT been run on this card and nothing between
         * the card and the devices is configured yet.
         */
        sc->device_flags[i] = CFDISC;
        if (p->features & AHC_WIDE)
          sc->device_flags[i] |= CFWIDEB;
        if (p->features & AHC_ULTRA3)
          sc->device_flags[i] |= 2;
        else if (p->features & AHC_ULTRA2)
          sc->device_flags[i] |= 3;
        else if (p->features & AHC_ULTRA)
          sc->device_flags[i] |= CFSYNCHISULTRA;
        sc->device_flags[i] |= CFSYNCH;
        aic_outb(p, 0, TARG_SCSIRATE + i);
        if (p->features & AHC_ULTRA2)
          aic_outb(p, 0, TARG_OFFSET + i);
      }
    }
    if (sc->device_flags[i] & CFDISC)
    {
      p->discenable |= mask;
    }
    if (p->flags & AHC_NEWEEPROM_FMT)
    {
      if ( !(p->features & AHC_ULTRA2) )
      {
        /*
         * I know of two different Ultra BIOSes that do this differently.
         * One on the Gigabyte 6BXU mb that wants flags[i] & CFXFER to
         * be == to 0x03 and SYNCHISULTRA to be true to mean 40MByte/s
         * while on the IBM Netfinity 5000 they want the same thing
         * to be something else, while flags[i] & CFXFER == 0x03 and
         * SYNCHISULTRA false should be 40MByte/s.  So, we set both to
         * 40MByte/s and the lower speeds be damned.  People will have
         * to select around the conversely mapped lower speeds in order
         * to select lower speeds on these boards.
         */
        if ( (sc->device_flags[i] & CFNEWULTRAFORMAT) &&
            ((sc->device_flags[i] & CFXFER) == 0x03) )
        {
          sc->device_flags[i] &= ~CFXFER;
          sc->device_flags[i] |= CFSYNCHISULTRA;
        }
        if (sc->device_flags[i] & CFSYNCHISULTRA)
        {
          p->ultraenb |= mask;
        }
      }
      else if ( !(sc->device_flags[i] & CFNEWULTRAFORMAT) &&
                 (p->features & AHC_ULTRA2) &&
		 (sc->device_flags[i] & CFSYNCHISULTRA) )
      {
        p->ultraenb |= mask;
      }
    }
    else if (sc->adapter_control & CFULTRAEN)
    {
      p->ultraenb |= mask;
    }
    if ( (sc->device_flags[i] & CFSYNCH) == 0)
    {
      sc->device_flags[i] &= ~CFXFER;
      p->ultraenb &= ~mask;
      p->user[i].offset = 0;
      p->user[i].period = 0;
      p->user[i].options = 0;
    }
    else
    {
      if (p->features & AHC_ULTRA3)
      {
        p->user[i].offset = MAX_OFFSET_ULTRA2;
        if( (sc->device_flags[i] & CFXFER) < 0x03 )
        {
          scsirate = (sc->device_flags[i] & CFXFER);
          p->user[i].options = MSG_EXT_PPR_OPTION_DT_CRC;
        }
        else
        {
          scsirate = (sc->device_flags[i] & CFXFER) |
                     ((p->ultraenb & mask) ? 0x18 : 0x10);
          p->user[i].options = 0;
        }
        p->user[i].period = aic7xxx_find_period(p, scsirate,
                                       AHC_SYNCRATE_ULTRA3);
      }
      else if (p->features & AHC_ULTRA2)
      {
        p->user[i].offset = MAX_OFFSET_ULTRA2;
        scsirate = (sc->device_flags[i] & CFXFER) |
                   ((p->ultraenb & mask) ? 0x18 : 0x10);
        p->user[i].options = 0;
        p->user[i].period = aic7xxx_find_period(p, scsirate,
                                       AHC_SYNCRATE_ULTRA2);
      }
      else
      {
        scsirate = (sc->device_flags[i] & CFXFER) << 4;
        p->user[i].options = 0;
        p->user[i].offset = MAX_OFFSET_8BIT;
        if (p->features & AHC_ULTRA)
        {
          short ultraenb;
          ultraenb = aic_inb(p, ULTRA_ENB) |
            (aic_inb(p, ULTRA_ENB + 1) << 8);
          p->user[i].period = aic7xxx_find_period(p, scsirate,
                                          (p->ultraenb & mask) ?
                                          AHC_SYNCRATE_ULTRA :
                                          AHC_SYNCRATE_FAST);
        }
        else
          p->user[i].period = aic7xxx_find_period(p, scsirate,
			  		  AHC_SYNCRATE_FAST);
      }
    }
    if ( (sc->device_flags[i] & CFWIDEB) && (p->features & AHC_WIDE) )
    {
      p->user[i].width = MSG_EXT_WDTR_BUS_16_BIT;
    }
    else
    {
      p->user[i].width = MSG_EXT_WDTR_BUS_8_BIT;
    }
  }
  aic_outb(p, ~(p->discenable & 0xFF), DISC_DSB);
  aic_outb(p, ~((p->discenable >> 8) & 0xFF), DISC_DSB + 1);

  /*
   * We set the p->ultraenb from the SEEPROM to begin with, but now we make
   * it match what is already down in the card.  If we are doing a reset
   * on the card then this will get put back to a default state anyway.
   * This allows us to not have to pre-emptively negotiate when using the
   * no_reset option.
   */
  if (p->features & AHC_ULTRA)
    p->ultraenb = aic_inb(p, ULTRA_ENB) | (aic_inb(p, ULTRA_ENB + 1) << 8);

  
  scsi_conf = (p->scsi_id & HSCSIID);

  if(have_seeprom)
  {
    p->adapter_control = sc->adapter_control;
    p->bios_control = sc->bios_control;

    switch (p->chip & AHC_CHIPID_MASK)
    {
      case AHC_AIC7895:
      case AHC_AIC7896:
      case AHC_AIC7899:
        if (p->adapter_control & CFBPRIMARY)
          p->flags |= AHC_CHANNEL_B_PRIMARY;
      default:
        break;
    }

    if (sc->adapter_control & CFSPARITY)
      scsi_conf |= ENSPCHK;
  }
  else
  {
    scsi_conf |= ENSPCHK | RESET_SCSI;
  }

  /*
   * Only set the SCSICONF and SCSICONF + 1 registers if we are a PCI card.
   * The 2842 and 2742 cards already have these registers set and we don't
   * want to muck with them since we don't set all the bits they do.
   */
  if ( (p->chip & ~AHC_CHIPID_MASK) == AHC_PCI )
  {
    /* Set the host ID */
    aic_outb(p, scsi_conf, SCSICONF);
    /* In case we are a wide card */
    aic_outb(p, p->scsi_id, SCSICONF + 1);
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_configure_bugs
 *
 * Description:
 *   Take the card passed in and set the appropriate bug flags based upon
 *   the card model.  Also make any changes needed to device registers or
 *   PCI registers while we are here.
 *-F*************************************************************************/
static void
aic7xxx_configure_bugs(struct aic7xxx_host *p)
{
  unsigned short tmp_word;
 
  switch(p->chip & AHC_CHIPID_MASK)
  {
    case AHC_AIC7860:
      p->bugs |= AHC_BUG_PCI_2_1_RETRY;
      /* fall through */
    case AHC_AIC7850:
    case AHC_AIC7870:
      p->bugs |= AHC_BUG_TMODE_WIDEODD | AHC_BUG_CACHETHEN | AHC_BUG_PCI_MWI;
      break;
    case AHC_AIC7880:
      p->bugs |= AHC_BUG_TMODE_WIDEODD | AHC_BUG_PCI_2_1_RETRY |
                 AHC_BUG_CACHETHEN | AHC_BUG_PCI_MWI;
      break;
    case AHC_AIC7890:
      p->bugs |= AHC_BUG_AUTOFLUSH | AHC_BUG_CACHETHEN;
      break;
    case AHC_AIC7892:
      p->bugs |= AHC_BUG_SCBCHAN_UPLOAD;
      break;
    case AHC_AIC7895:
      p->bugs |= AHC_BUG_TMODE_WIDEODD | AHC_BUG_PCI_2_1_RETRY |
                 AHC_BUG_CACHETHEN | AHC_BUG_PCI_MWI;
      break;
    case AHC_AIC7896:
      p->bugs |= AHC_BUG_CACHETHEN_DIS;
      break;
    case AHC_AIC7899:
      p->bugs |= AHC_BUG_SCBCHAN_UPLOAD;
      break;
    default:
      /* Nothing to do */
      break;
  }

  /*
   * Now handle the bugs that require PCI register or card register tweaks
   */
  pci_read_config_word(p->pdev, PCI_COMMAND, &tmp_word);
  if(p->bugs & AHC_BUG_PCI_MWI)
  {
    tmp_word &= ~PCI_COMMAND_INVALIDATE;
  }
  else
  {
    tmp_word |= PCI_COMMAND_INVALIDATE;
  }
  pci_write_config_word(p->pdev, PCI_COMMAND, tmp_word);

  if(p->bugs & AHC_BUG_CACHETHEN)
  {
    aic_outb(p, aic_inb(p, DSCOMMAND0) & ~CACHETHEN, DSCOMMAND0);
  }
  else if (p->bugs & AHC_BUG_CACHETHEN_DIS)
  {
    aic_outb(p, aic_inb(p, DSCOMMAND0) | CACHETHEN, DSCOMMAND0);
  }

  return;
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_detect
 *
 * Description:
 *   Try to detect and register an Adaptec 7770 or 7870 SCSI controller.
 *
 * XXX - This should really be called aic7xxx_probe().  A sequence of
 *       probe(), attach()/detach(), and init() makes more sense than
 *       one do-it-all function.  This may be useful when (and if) the
 *       mid-level SCSI code is overhauled.
 *-F*************************************************************************/
static int
aic7xxx_detect(struct scsi_host_template *template)
{
  struct aic7xxx_host *temp_p = NULL;
  struct aic7xxx_host *current_p = NULL;
  struct aic7xxx_host *list_p = NULL;
  int found = 0;
#if defined(__i386__) || defined(__alpha__)
  ahc_flag_type flags = 0;
  int type;
#endif
  unsigned char sxfrctl1;
#if defined(__i386__) || defined(__alpha__)
  unsigned char hcntrl, hostconf;
  unsigned int slot, base;
#endif

#ifdef MODULE
  /*
   * If we are called as a module, the aic7xxx pointer may not be null
   * and it would point to our bootup string, just like on the lilo
   * command line.  IF not NULL, then process this config string with
   * aic7xxx_setup
   */
  if(aic7xxx)
    aic7xxx_setup(aic7xxx);
#endif

  template->proc_name = "aic7xxx";
  template->sg_tablesize = AIC7XXX_MAX_SG;


#ifdef CONFIG_PCI
  /*
   * PCI-bus probe.
   */
  {
    static struct
    {
      unsigned short      vendor_id;
      unsigned short      device_id;
      ahc_chip            chip;
      ahc_flag_type       flags;
      ahc_feature         features;
      int                 board_name_index;
      unsigned short      seeprom_size;
      unsigned short      seeprom_type;
    } const aic_pdevs[] = {
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7810, AHC_NONE,
       AHC_FNONE, AHC_FENONE,                                1,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7850, AHC_AIC7850,
       AHC_PAGESCBS, AHC_AIC7850_FE,                         5,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7855, AHC_AIC7850,
       AHC_PAGESCBS, AHC_AIC7850_FE,                         6,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7821, AHC_AIC7860,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED,
       AHC_AIC7860_FE,                                       7,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_3860, AHC_AIC7860,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED,
       AHC_AIC7860_FE,                                       7,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_38602, AHC_AIC7860,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED,
       AHC_AIC7860_FE,                                       7,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_38602, AHC_AIC7860,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED,
       AHC_AIC7860_FE,                                       7,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7860, AHC_AIC7860,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED | AHC_MOTHERBOARD,
       AHC_AIC7860_FE,                                       7,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7861, AHC_AIC7860,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED,
       AHC_AIC7860_FE,                                       8,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7870, AHC_AIC7870,
       AHC_PAGESCBS | AHC_BIOS_ENABLED | AHC_MOTHERBOARD,
       AHC_AIC7870_FE,                                       9,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7871, AHC_AIC7870,
       AHC_PAGESCBS | AHC_BIOS_ENABLED, AHC_AIC7870_FE,     10,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7872, AHC_AIC7870,
       AHC_PAGESCBS | AHC_BIOS_ENABLED | AHC_MULTI_CHANNEL,
       AHC_AIC7870_FE,                                      11,
       32, C56_66 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7873, AHC_AIC7870,
       AHC_PAGESCBS | AHC_BIOS_ENABLED | AHC_MULTI_CHANNEL,
       AHC_AIC7870_FE,                                      12,
       32, C56_66 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7874, AHC_AIC7870,
       AHC_PAGESCBS | AHC_BIOS_ENABLED, AHC_AIC7870_FE,     13,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7880, AHC_AIC7880,
       AHC_PAGESCBS | AHC_BIOS_ENABLED | AHC_MOTHERBOARD,
       AHC_AIC7880_FE,                                      14,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7881, AHC_AIC7880,
       AHC_PAGESCBS | AHC_BIOS_ENABLED, AHC_AIC7880_FE,     15,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7882, AHC_AIC7880,
       AHC_PAGESCBS | AHC_BIOS_ENABLED | AHC_MULTI_CHANNEL,
       AHC_AIC7880_FE,                                      16,
       32, C56_66 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7883, AHC_AIC7880,
       AHC_PAGESCBS | AHC_BIOS_ENABLED | AHC_MULTI_CHANNEL,
       AHC_AIC7880_FE,                                      17,
       32, C56_66 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7884, AHC_AIC7880,
       AHC_PAGESCBS | AHC_BIOS_ENABLED, AHC_AIC7880_FE,     18,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7885, AHC_AIC7880,
       AHC_PAGESCBS | AHC_BIOS_ENABLED, AHC_AIC7880_FE,     18,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7886, AHC_AIC7880,
       AHC_PAGESCBS | AHC_BIOS_ENABLED, AHC_AIC7880_FE,     18,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7887, AHC_AIC7880,
       AHC_PAGESCBS | AHC_BIOS_ENABLED, AHC_AIC7880_FE | AHC_NEW_AUTOTERM, 19,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7888, AHC_AIC7880,
       AHC_PAGESCBS | AHC_BIOS_ENABLED, AHC_AIC7880_FE,     18,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7895, AHC_AIC7895,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED | AHC_MULTI_CHANNEL,
       AHC_AIC7895_FE,                                      20,
       32, C56_66 },
      {PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_7890, AHC_AIC7890,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED,
       AHC_AIC7890_FE,                                      21,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_7890B, AHC_AIC7890,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED,
       AHC_AIC7890_FE,                                      21,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_2930U2, AHC_AIC7890,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED,
       AHC_AIC7890_FE,                                      22,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_2940U2, AHC_AIC7890,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED,
       AHC_AIC7890_FE,                                      23,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_7896, AHC_AIC7896,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED | AHC_MULTI_CHANNEL,
       AHC_AIC7896_FE,                                      24,
       32, C56_66 },
      {PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_3940U2, AHC_AIC7896,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED | AHC_MULTI_CHANNEL,
       AHC_AIC7896_FE,                                      25,
       32, C56_66 },
      {PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_3950U2D, AHC_AIC7896,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED | AHC_MULTI_CHANNEL,
       AHC_AIC7896_FE,                                      26,
       32, C56_66 },
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_1480A, AHC_AIC7860,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED | AHC_NO_STPWEN,
       AHC_AIC7860_FE,                                      27,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_7892A, AHC_AIC7892,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED,
       AHC_AIC7892_FE,                                      28,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_7892B, AHC_AIC7892,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED,
       AHC_AIC7892_FE,                                      28,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_7892D, AHC_AIC7892,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED,
       AHC_AIC7892_FE,                                      28,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_7892P, AHC_AIC7892,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED,
       AHC_AIC7892_FE,                                      28,
       32, C46 },
      {PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_7899A, AHC_AIC7899,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED | AHC_MULTI_CHANNEL,
       AHC_AIC7899_FE,                                      29,
       32, C56_66 },
      {PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_7899B, AHC_AIC7899,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED | AHC_MULTI_CHANNEL,
       AHC_AIC7899_FE,                                      29,
       32, C56_66 },
      {PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_7899D, AHC_AIC7899,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED | AHC_MULTI_CHANNEL,
       AHC_AIC7899_FE,                                      29,
       32, C56_66 },
      {PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_7899P, AHC_AIC7899,
       AHC_PAGESCBS | AHC_NEWEEPROM_FMT | AHC_BIOS_ENABLED | AHC_MULTI_CHANNEL,
       AHC_AIC7899_FE,                                      29,
       32, C56_66 },
    };

    unsigned short command;
    unsigned int  devconfig, i, oldverbose;
    struct pci_dev *pdev = NULL;

    for (i = 0; i < ARRAY_SIZE(aic_pdevs); i++)
    {
      pdev = NULL;
      while ((pdev = pci_get_device(aic_pdevs[i].vendor_id,
                                     aic_pdevs[i].device_id,
                                     pdev))) {
	if (pci_enable_device(pdev))
		continue;
        if ( i == 0 ) /* We found one, but it's the 7810 RAID cont. */
        {
          if (aic7xxx_verbose & (VERBOSE_PROBE|VERBOSE_PROBE2))
          {
            printk(KERN_INFO "aic7xxx: The 7810 RAID controller is not "
              "supported by\n");
            printk(KERN_INFO "         this driver, we are ignoring it.\n");
          }
        }
        else if ( (temp_p = kzalloc(sizeof(struct aic7xxx_host),
                                    GFP_ATOMIC)) != NULL )
        {
          temp_p->chip = aic_pdevs[i].chip | AHC_PCI;
          temp_p->flags = aic_pdevs[i].flags;
          temp_p->features = aic_pdevs[i].features;
          temp_p->board_name_index = aic_pdevs[i].board_name_index;
          temp_p->sc_size = aic_pdevs[i].seeprom_size;
          temp_p->sc_type = aic_pdevs[i].seeprom_type;

          /*
           * Read sundry information from PCI BIOS.
           */
          temp_p->irq = pdev->irq;
          temp_p->pdev = pdev;
          temp_p->pci_bus = pdev->bus->number;
          temp_p->pci_device_fn = pdev->devfn;
          temp_p->base = pci_resource_start(pdev, 0);
          temp_p->mbase = pci_resource_start(pdev, 1);
          current_p = list_p;
	  while(current_p && temp_p)
	  {
	    if ( ((current_p->pci_bus == temp_p->pci_bus) &&
	          (current_p->pci_device_fn == temp_p->pci_device_fn)) ||
                 (temp_p->base && (current_p->base == temp_p->base)) ||
                 (temp_p->mbase && (current_p->mbase == temp_p->mbase)) )
	    {
              /* duplicate PCI entry, skip it */
	      kfree(temp_p);
	      temp_p = NULL;
              continue;
	    }
	    current_p = current_p->next;
	  }
          if(pci_request_regions(temp_p->pdev, "aic7xxx"))
          {
            printk("aic7xxx: <%s> at PCI %d/%d/%d\n", 
              board_names[aic_pdevs[i].board_name_index],
              temp_p->pci_bus,
              PCI_SLOT(temp_p->pci_device_fn),
              PCI_FUNC(temp_p->pci_device_fn));
            printk("aic7xxx: I/O ports already in use, ignoring.\n");
            kfree(temp_p);
            continue;
          }

          if (aic7xxx_verbose & VERBOSE_PROBE2)
            printk("aic7xxx: <%s> at PCI %d/%d\n", 
              board_names[aic_pdevs[i].board_name_index],
              PCI_SLOT(pdev->devfn),
              PCI_FUNC(pdev->devfn));
          pci_read_config_word(pdev, PCI_COMMAND, &command);
          if (aic7xxx_verbose & VERBOSE_PROBE2)
          {
            printk("aic7xxx: Initial PCI_COMMAND value was 0x%x\n",
              (int)command);
          }
#ifdef AIC7XXX_STRICT_PCI_SETUP
          command |= PCI_COMMAND_SERR | PCI_COMMAND_PARITY |
            PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY | PCI_COMMAND_IO;
#else
          command |= PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY | PCI_COMMAND_IO;
#endif
          command &= ~PCI_COMMAND_INVALIDATE;
          if (aic7xxx_pci_parity == 0)
            command &= ~(PCI_COMMAND_SERR | PCI_COMMAND_PARITY);
          pci_write_config_word(pdev, PCI_COMMAND, command);
#ifdef AIC7XXX_STRICT_PCI_SETUP
          pci_read_config_dword(pdev, DEVCONFIG, &devconfig);
          if (aic7xxx_verbose & VERBOSE_PROBE2)
          {
            printk("aic7xxx: Initial DEVCONFIG value was 0x%x\n", devconfig);
          }
          devconfig |= 0x80000040;
          pci_write_config_dword(pdev, DEVCONFIG, devconfig);
#endif /* AIC7XXX_STRICT_PCI_SETUP */

          temp_p->unpause = INTEN;
          temp_p->pause = temp_p->unpause | PAUSE;
          if ( ((temp_p->base == 0) &&
                (temp_p->mbase == 0)) ||
               (temp_p->irq == 0) )
          {
            printk("aic7xxx: <%s> at PCI %d/%d/%d\n", 
              board_names[aic_pdevs[i].board_name_index],
              temp_p->pci_bus,
              PCI_SLOT(temp_p->pci_device_fn),
              PCI_FUNC(temp_p->pci_device_fn));
            printk("aic7xxx: Controller disabled by BIOS, ignoring.\n");
            goto skip_pci_controller;
          }

#ifdef MMAPIO
          if ( !(temp_p->base) || !(temp_p->flags & AHC_MULTI_CHANNEL) ||
               ((temp_p->chip != (AHC_AIC7870 | AHC_PCI)) &&
                (temp_p->chip != (AHC_AIC7880 | AHC_PCI))) )
          {
            temp_p->maddr = ioremap_nocache(temp_p->mbase, 256);
            if(temp_p->maddr)
            {
              /*
               * We need to check the I/O with the MMAPed address.  Some machines
               * simply fail to work with MMAPed I/O and certain controllers.
               */
              if(aic_inb(temp_p, HCNTRL) == 0xff)
              {
                /*
                 * OK.....we failed our test....go back to programmed I/O
                 */
                printk(KERN_INFO "aic7xxx: <%s> at PCI %d/%d/%d\n", 
                  board_names[aic_pdevs[i].board_name_index],
                  temp_p->pci_bus,
                  PCI_SLOT(temp_p->pci_device_fn),
                  PCI_FUNC(temp_p->pci_device_fn));
                printk(KERN_INFO "aic7xxx: MMAPed I/O failed, reverting to "
                                 "Programmed I/O.\n");
                iounmap(temp_p->maddr);
                temp_p->maddr = NULL;
                if(temp_p->base == 0)
                {
                  printk("aic7xxx: <%s> at PCI %d/%d/%d\n", 
                    board_names[aic_pdevs[i].board_name_index],
                    temp_p->pci_bus,
                    PCI_SLOT(temp_p->pci_device_fn),
                    PCI_FUNC(temp_p->pci_device_fn));
                  printk("aic7xxx: Controller disabled by BIOS, ignoring.\n");
                  goto skip_pci_controller;
                }
              }
            }
          }
#endif

          /*
           * We HAVE to make sure the first pause_sequencer() and all other
           * subsequent I/O that isn't PCI config space I/O takes place
           * after the MMAPed I/O region is configured and tested.  The
           * problem is the PowerPC architecture that doesn't support
           * programmed I/O at all, so we have to have the MMAP I/O set up
           * for this pause to even work on those machines.
           */
          pause_sequencer(temp_p);

          /*
           * Clear out any pending PCI error status messages.  Also set
           * verbose to 0 so that we don't emit strange PCI error messages
           * while cleaning out the current status bits.
           */
          oldverbose = aic7xxx_verbose;
          aic7xxx_verbose = 0;
          aic7xxx_pci_intr(temp_p);
          aic7xxx_verbose = oldverbose;

          temp_p->bios_address = 0;

          /*
           * Remember how the card was setup in case there is no seeprom.
           */
          if (temp_p->features & AHC_ULTRA2)
            temp_p->scsi_id = aic_inb(temp_p, SCSIID_ULTRA2) & OID;
          else
            temp_p->scsi_id = aic_inb(temp_p, SCSIID) & OID;
          /*
           * Get current termination setting
           */
          sxfrctl1 = aic_inb(temp_p, SXFRCTL1);

          if (aic7xxx_chip_reset(temp_p) == -1)
          {
            goto skip_pci_controller;
          }
          /*
           * Very quickly put the term setting back into the register since
           * the chip reset may cause odd things to happen.  This is to keep
           * LVD busses with lots of drives from draining the power out of
           * the diffsense line before we get around to running the
           * configure_termination() function.  Also restore the STPWLEVEL
           * bit of DEVCONFIG
           */
          aic_outb(temp_p, sxfrctl1, SXFRCTL1);
          pci_write_config_dword(temp_p->pdev, DEVCONFIG, devconfig);
          sxfrctl1 &= STPWEN;

          /*
           * We need to set the CHNL? assignments before loading the SEEPROM
           * The 3940 and 3985 cards (original stuff, not any of the later
           * stuff) are 7870 and 7880 class chips.  The Ultra2 stuff falls
           * under 7896 and 7897.  The 7895 is in a class by itself :)
           */
          switch (temp_p->chip & AHC_CHIPID_MASK)
          {
            case AHC_AIC7870: /* 3840 / 3985 */
            case AHC_AIC7880: /* 3840 UW / 3985 UW */
              if(temp_p->flags & AHC_MULTI_CHANNEL)
              {
                switch(PCI_SLOT(temp_p->pci_device_fn))
                {
                  case 5:
                    temp_p->flags |= AHC_CHNLB;
                    break;
                  case 8:
                    temp_p->flags |= AHC_CHNLB;
                    break;
                  case 12:
                    temp_p->flags |= AHC_CHNLC;
                    break;
                  default:
                    break;
                }
              }
              break;

            case AHC_AIC7895: /* 7895 */
            case AHC_AIC7896: /* 7896/7 */
            case AHC_AIC7899: /* 7899 */
              if (PCI_FUNC(pdev->devfn) != 0)
              {
                temp_p->flags |= AHC_CHNLB;
              }
              /*
               * The 7895 is the only chipset that sets the SCBSIZE32 param
               * in the DEVCONFIG register.  The Ultra2 chipsets use
               * the DSCOMMAND0 register instead.
               */
              if ((temp_p->chip & AHC_CHIPID_MASK) == AHC_AIC7895)
              {
                pci_read_config_dword(pdev, DEVCONFIG, &devconfig);
                devconfig |= SCBSIZE32;
                pci_write_config_dword(pdev, DEVCONFIG, devconfig);
              }
              break;
            default:
              break;
          }

          /*
           * Loading of the SEEPROM needs to come after we've set the flags
           * to indicate possible CHNLB and CHNLC assigments.  Otherwise,
           * on 394x and 398x cards we'll end up reading the wrong settings
           * for channels B and C
           */
          switch (temp_p->chip & AHC_CHIPID_MASK)
          {
            case AHC_AIC7892:
            case AHC_AIC7899:
              aic_outb(temp_p, 0, SCAMCTL);
              /*
               * Switch to the alt mode of the chip...
               */
              aic_outb(temp_p, aic_inb(temp_p, SFUNCT) | ALT_MODE, SFUNCT);
              /*
               * Set our options...the last two items set our CRC after x byte
	       * count in target mode...
               */
              aic_outb(temp_p, AUTO_MSGOUT_DE | DIS_MSGIN_DUALEDGE, OPTIONMODE);
	      aic_outb(temp_p, 0x00, 0x0b);
	      aic_outb(temp_p, 0x10, 0x0a);
              /*
               * switch back to normal mode...
               */
              aic_outb(temp_p, aic_inb(temp_p, SFUNCT) & ~ALT_MODE, SFUNCT);
              aic_outb(temp_p, CRCVALCHKEN | CRCENDCHKEN | CRCREQCHKEN |
			       TARGCRCENDEN | TARGCRCCNTEN,
                       CRCCONTROL1);
              aic_outb(temp_p, ((aic_inb(temp_p, DSCOMMAND0) | USCBSIZE32 |
                                 MPARCKEN | CIOPARCKEN | CACHETHEN) & 
                               ~DPARCKEN), DSCOMMAND0);
              aic7xxx_load_seeprom(temp_p, &sxfrctl1);
              break;
            case AHC_AIC7890:
            case AHC_AIC7896:
              aic_outb(temp_p, 0, SCAMCTL);
              aic_outb(temp_p, (aic_inb(temp_p, DSCOMMAND0) |
                                CACHETHEN | MPARCKEN | USCBSIZE32 |
                                CIOPARCKEN) & ~DPARCKEN, DSCOMMAND0);
              aic7xxx_load_seeprom(temp_p, &sxfrctl1);
              break;
            case AHC_AIC7850:
            case AHC_AIC7860:
              /*
               * Set the DSCOMMAND0 register on these cards different from
               * on the 789x cards.  Also, read the SEEPROM as well.
               */
              aic_outb(temp_p, (aic_inb(temp_p, DSCOMMAND0) |
                                CACHETHEN | MPARCKEN) & ~DPARCKEN,
                       DSCOMMAND0);
              /* FALLTHROUGH */
            default:
              aic7xxx_load_seeprom(temp_p, &sxfrctl1);
              break;
            case AHC_AIC7880:
              /*
               * Check the rev of the chipset before we change DSCOMMAND0
               */
              pci_read_config_dword(pdev, DEVCONFIG, &devconfig);
              if ((devconfig & 0xff) >= 1)
              {
                aic_outb(temp_p, (aic_inb(temp_p, DSCOMMAND0) |
                                  CACHETHEN | MPARCKEN) & ~DPARCKEN,
                         DSCOMMAND0);
              }
              aic7xxx_load_seeprom(temp_p, &sxfrctl1);
              break;
          }
          

          /*
           * and then we need another switch based on the type in order to
           * make sure the channel B primary flag is set properly on 7895
           * controllers....Arrrgggghhh!!!  We also have to catch the fact
           * that when you disable the BIOS on the 7895 on the Intel DK440LX
           * motherboard, and possibly others, it only sets the BIOS disabled
           * bit on the A channel...I think I'm starting to lean towards
           * going postal....
           */
          switch(temp_p->chip & AHC_CHIPID_MASK)
          {
            case AHC_AIC7895:
            case AHC_AIC7896:
            case AHC_AIC7899:
              current_p = list_p;
              while(current_p != NULL)
              {
                if ( (current_p->pci_bus == temp_p->pci_bus) &&
                     (PCI_SLOT(current_p->pci_device_fn) ==
                      PCI_SLOT(temp_p->pci_device_fn)) )
                {
                  if ( PCI_FUNC(current_p->pci_device_fn) == 0 )
                  {
                    temp_p->flags |= 
                      (current_p->flags & AHC_CHANNEL_B_PRIMARY);
                    temp_p->flags &= ~(AHC_BIOS_ENABLED|AHC_USEDEFAULTS);
                    temp_p->flags |=
                      (current_p->flags & (AHC_BIOS_ENABLED|AHC_USEDEFAULTS));
                  }
                  else
                  {
                    current_p->flags |=
                      (temp_p->flags & AHC_CHANNEL_B_PRIMARY);
                    current_p->flags &= ~(AHC_BIOS_ENABLED|AHC_USEDEFAULTS);
                    current_p->flags |=
                      (temp_p->flags & (AHC_BIOS_ENABLED|AHC_USEDEFAULTS));
                  }
                }
                current_p = current_p->next;
              }
              break;
            default:
              break;
          }

          /*
           * We only support external SCB RAM on the 7895/6/7 chipsets.
           * We could support it on the 7890/1 easy enough, but I don't
           * know of any 7890/1 based cards that have it.  I do know
           * of 7895/6/7 cards that have it and they work properly.
           */
          switch(temp_p->chip & AHC_CHIPID_MASK)
          {
            default:
              break;
            case AHC_AIC7895:
            case AHC_AIC7896:
            case AHC_AIC7899:
              pci_read_config_dword(pdev, DEVCONFIG, &devconfig);
              if (temp_p->features & AHC_ULTRA2)
              {
                if ( (aic_inb(temp_p, DSCOMMAND0) & RAMPSM_ULTRA2) &&
                     (aic7xxx_scbram) )
                {
                  aic_outb(temp_p,
                           aic_inb(temp_p, DSCOMMAND0) & ~SCBRAMSEL_ULTRA2,
                           DSCOMMAND0);
                  temp_p->flags |= AHC_EXTERNAL_SRAM;
                  devconfig |= EXTSCBPEN;
                }
                else if (aic_inb(temp_p, DSCOMMAND0) & RAMPSM_ULTRA2)
                {
                  printk(KERN_INFO "aic7xxx: <%s> at PCI %d/%d/%d\n", 
                    board_names[aic_pdevs[i].board_name_index],
                    temp_p->pci_bus,
                    PCI_SLOT(temp_p->pci_device_fn),
                    PCI_FUNC(temp_p->pci_device_fn));
                  printk("aic7xxx: external SCB RAM detected, "
                         "but not enabled\n");
                }
              }
              else
              {
                if ((devconfig & RAMPSM) && (aic7xxx_scbram))
                {
                  devconfig &= ~SCBRAMSEL;
                  devconfig |= EXTSCBPEN;
                  temp_p->flags |= AHC_EXTERNAL_SRAM;
                }
                else if (devconfig & RAMPSM)
                {
                  printk(KERN_INFO "aic7xxx: <%s> at PCI %d/%d/%d\n", 
                    board_names[aic_pdevs[i].board_name_index],
                    temp_p->pci_bus,
                    PCI_SLOT(temp_p->pci_device_fn),
                    PCI_FUNC(temp_p->pci_device_fn));
                  printk("aic7xxx: external SCB RAM detected, "
                         "but not enabled\n");
                }
              }
              pci_write_config_dword(pdev, DEVCONFIG, devconfig);
              if ( (temp_p->flags & AHC_EXTERNAL_SRAM) &&
                   (temp_p->flags & AHC_CHNLB) )
                aic_outb(temp_p, 1, CCSCBBADDR);
              break;
          }

          /*
           * Take the LED out of diagnostic mode
           */
          aic_outb(temp_p, 
            (aic_inb(temp_p, SBLKCTL) & ~(DIAGLEDEN | DIAGLEDON)),
            SBLKCTL);

          /*
           * We don't know where this is set in the SEEPROM or by the
           * BIOS, so we default to 100%.  On Ultra2 controllers, use 75%
           * instead.
           */
          if (temp_p->features & AHC_ULTRA2)
          {
            aic_outb(temp_p, RD_DFTHRSH_MAX | WR_DFTHRSH_MAX, DFF_THRSH);
          }
          else
          {
            aic_outb(temp_p, DFTHRSH_100, DSPCISTATUS);
          }

          /*
           * Call our function to fixup any bugs that exist on this chipset.
           * This may muck with PCI settings and other device settings, so
           * make sure it's after all the other PCI and device register
           * tweaks so it can back out bad settings on specific broken cards.
           */
          aic7xxx_configure_bugs(temp_p);

          /* Hold a pci device reference */
          pci_dev_get(temp_p->pdev);

          if ( list_p == NULL )
          {
            list_p = current_p = temp_p;
          }
          else
          {
            current_p = list_p;
            while(current_p->next != NULL)
              current_p = current_p->next;
            current_p->next = temp_p;
          }
          temp_p->next = NULL;
          found++;
	  continue;
skip_pci_controller:
#ifdef CONFIG_PCI
	  pci_release_regions(temp_p->pdev);
#endif
	  kfree(temp_p);
        }  /* Found an Adaptec PCI device. */
        else /* Well, we found one, but we couldn't get any memory */
        {
          printk("aic7xxx: Found <%s>\n", 
            board_names[aic_pdevs[i].board_name_index]);
          printk(KERN_INFO "aic7xxx: Unable to allocate device memory, "
            "skipping.\n");
        }
      } /* while(pdev=....) */
    } /* for PCI_DEVICES */
  }
#endif /* CONFIG_PCI */

#if defined(__i386__) || defined(__alpha__)
  /*
   * EISA/VL-bus card signature probe.
   */
  slot = MINSLOT;
  while ( (slot <= MAXSLOT) &&
         !(aic7xxx_no_probe) )
  {
    base = SLOTBASE(slot) + MINREG;

    if (!request_region(base, MAXREG - MINREG, "aic7xxx"))
    {
      /*
       * Some other driver has staked a
       * claim to this i/o region already.
       */
      slot++;
      continue; /* back to the beginning of the for loop */
    }
    flags = 0;
    type = aic7xxx_probe(slot, base + AHC_HID0, &flags);
    if (type == -1)
    {
      release_region(base, MAXREG - MINREG);
      slot++;
      continue;
    }
    temp_p = kmalloc(sizeof(struct aic7xxx_host), GFP_ATOMIC);
    if (temp_p == NULL)
    {
      printk(KERN_WARNING "aic7xxx: Unable to allocate device space.\n");
      release_region(base, MAXREG - MINREG);
      slot++;
      continue; /* back to the beginning of the while loop */
    }

    /*
     * Pause the card preserving the IRQ type.  Allow the operator
     * to override the IRQ trigger.
     */
    if (aic7xxx_irq_trigger == 1)
      hcntrl = IRQMS;  /* Level */
    else if (aic7xxx_irq_trigger == 0)
      hcntrl = 0;  /* Edge */
    else
      hcntrl = inb(base + HCNTRL) & IRQMS;  /* Default */
    memset(temp_p, 0, sizeof(struct aic7xxx_host));
    temp_p->unpause = hcntrl | INTEN;
    temp_p->pause = hcntrl | PAUSE | INTEN;
    temp_p->base = base;
    temp_p->mbase = 0;
    temp_p->maddr = NULL;
    temp_p->pci_bus = 0;
    temp_p->pci_device_fn = slot;
    aic_outb(temp_p, hcntrl | PAUSE, HCNTRL);
    while( (aic_inb(temp_p, HCNTRL) & PAUSE) == 0 ) ;
    if (aic7xxx_chip_reset(temp_p) == -1)
      temp_p->irq = 0;
    else
      temp_p->irq = aic_inb(temp_p, INTDEF) & 0x0F;
    temp_p->flags |= AHC_PAGESCBS;

    switch (temp_p->irq)
    {
      case 9:
      case 10:
      case 11:
      case 12:
      case 14:
      case 15:
        break;

      default:
        printk(KERN_WARNING "aic7xxx: Host adapter uses unsupported IRQ "
          "level %d, ignoring.\n", temp_p->irq);
        kfree(temp_p);
        release_region(base, MAXREG - MINREG);
        slot++;
        continue; /* back to the beginning of the while loop */
    }

    /*
     * We are committed now, everything has been checked and this card
     * has been found, now we just set it up
     */

    /*
     * Insert our new struct into the list at the end
     */
    if (list_p == NULL)
    {
      list_p = current_p = temp_p;
    }
    else
    {
      current_p = list_p;
      while (current_p->next != NULL)
        current_p = current_p->next;
      current_p->next = temp_p;
    }

    switch (type)
    {
      case 0:
        temp_p->board_name_index = 2;
        if (aic7xxx_verbose & VERBOSE_PROBE2)
          printk("aic7xxx: <%s> at EISA %d\n",
               board_names[2], slot);
        /* FALLTHROUGH */
      case 1:
      {
        temp_p->chip = AHC_AIC7770 | AHC_EISA;
        temp_p->features |= AHC_AIC7770_FE;
        temp_p->bios_control = aic_inb(temp_p, HA_274_BIOSCTRL);

        /*
         * Get the primary channel information.  Right now we don't
         * do anything with this, but someday we will be able to inform
         * the mid-level SCSI code which channel is primary.
         */
        if (temp_p->board_name_index == 0)
        {
          temp_p->board_name_index = 3;
          if (aic7xxx_verbose & VERBOSE_PROBE2)
            printk("aic7xxx: <%s> at EISA %d\n",
                 board_names[3], slot);
        }
        if (temp_p->bios_control & CHANNEL_B_PRIMARY)
        {
          temp_p->flags |= AHC_CHANNEL_B_PRIMARY;
        }

        if ((temp_p->bios_control & BIOSMODE) == BIOSDISABLED)
        {
          temp_p->flags &= ~AHC_BIOS_ENABLED;
        }
        else
        {
          temp_p->flags &= ~AHC_USEDEFAULTS;
          temp_p->flags |= AHC_BIOS_ENABLED;
          if ( (temp_p->bios_control & 0x20) == 0 )
          {
            temp_p->bios_address = 0xcc000;
            temp_p->bios_address += (0x4000 * (temp_p->bios_control & 0x07));
          }
          else
          {
            temp_p->bios_address = 0xd0000;
            temp_p->bios_address += (0x8000 * (temp_p->bios_control & 0x06));
          }
        }
        temp_p->adapter_control = aic_inb(temp_p, SCSICONF) << 8;
        temp_p->adapter_control |= aic_inb(temp_p, SCSICONF + 1);
        if (temp_p->features & AHC_WIDE)
        {
          temp_p->scsi_id = temp_p->adapter_control & HWSCSIID;
          temp_p->scsi_id_b = temp_p->scsi_id;
        }
        else
        {
          temp_p->scsi_id = (temp_p->adapter_control >> 8) & HSCSIID;
          temp_p->scsi_id_b = temp_p->adapter_control & HSCSIID;
        }
        aic7xxx_load_seeprom(temp_p, &sxfrctl1);
        break;
      }

      case 2:
      case 3:
        temp_p->chip = AHC_AIC7770 | AHC_VL;
        temp_p->features |= AHC_AIC7770_FE;
        if (type == 2)
          temp_p->flags |= AHC_BIOS_ENABLED;
        else
          temp_p->flags &= ~AHC_BIOS_ENABLED;
        if (aic_inb(temp_p, SCSICONF) & TERM_ENB)
          sxfrctl1 = STPWEN;
        aic7xxx_load_seeprom(temp_p, &sxfrctl1);
        temp_p->board_name_index = 4;
        if (aic7xxx_verbose & VERBOSE_PROBE2)
          printk("aic7xxx: <%s> at VLB %d\n",
               board_names[2], slot);
        switch( aic_inb(temp_p, STATUS_2840) & BIOS_SEL )
        {
          case 0x00:
            temp_p->bios_address = 0xe0000;
            break;
          case 0x20:
            temp_p->bios_address = 0xc8000;
            break;
          case 0x40:
            temp_p->bios_address = 0xd0000;
            break;
          case 0x60:
            temp_p->bios_address = 0xd8000;
            break;
          default:
            break; /* can't get here */
        }
        break;

      default:  /* Won't get here. */
        break;
    }
    if (aic7xxx_verbose & VERBOSE_PROBE2)
    {
      printk(KERN_INFO "aic7xxx: BIOS %sabled, IO Port 0x%lx, IRQ %d (%s)\n",
        (temp_p->flags & AHC_USEDEFAULTS) ? "dis" : "en", temp_p->base,
        temp_p->irq,
        (temp_p->pause & IRQMS) ? "level sensitive" : "edge triggered");
      printk(KERN_INFO "aic7xxx: Extended translation %sabled.\n",
             (temp_p->flags & AHC_EXTEND_TRANS_A) ? "en" : "dis");
    }

    /*
     * All the 7770 based chipsets have this bug
     */
    temp_p->bugs |= AHC_BUG_TMODE_WIDEODD;

    /*
     * Set the FIFO threshold and the bus off time.
     */
    hostconf = aic_inb(temp_p, HOSTCONF);
    aic_outb(temp_p, hostconf & DFTHRSH, BUSSPD);
    aic_outb(temp_p, (hostconf << 2) & BOFF, BUSTIME);
    slot++;
    found++;
  }

#endif /* defined(__i386__) || defined(__alpha__) */

  /*
   * Now, we re-order the probed devices by BIOS address and BUS class.
   * In general, we follow this algorithm to make the adapters show up
   * in the same order under linux that the computer finds them.
   *  1: All VLB/EISA cards with BIOS_ENABLED first, according to BIOS
   *     address, going from lowest to highest.
   *  2: All PCI controllers with BIOS_ENABLED next, according to BIOS
   *     address, going from lowest to highest.
   *  3: Remaining VLB/EISA controllers going in slot order.
   *  4: Remaining PCI controllers, going in PCI device order (reversible)
   */

  {
    struct aic7xxx_host *sort_list[4] = { NULL, NULL, NULL, NULL };
    struct aic7xxx_host *vlb, *pci;
    struct aic7xxx_host *prev_p;
    struct aic7xxx_host *p;
    unsigned char left;

    prev_p = vlb = pci = NULL;

    temp_p = list_p;
    while (temp_p != NULL)
    {
      switch(temp_p->chip & ~AHC_CHIPID_MASK)
      {
        case AHC_EISA:
        case AHC_VL:
        {
          p = temp_p;
          if (p->flags & AHC_BIOS_ENABLED)
            vlb = sort_list[0];
          else
            vlb = sort_list[2];

          if (vlb == NULL)
          {
            vlb = temp_p;
            temp_p = temp_p->next;
            vlb->next = NULL;
          }
          else
          {
            current_p = vlb;
            prev_p = NULL;
            while ( (current_p != NULL) &&
                    (current_p->bios_address < temp_p->bios_address))
            {
              prev_p = current_p;
              current_p = current_p->next;
            }
            if (prev_p != NULL)
            {
              prev_p->next = temp_p;
              temp_p = temp_p->next;
              prev_p->next->next = current_p;
            }
            else
            {
              vlb = temp_p;
              temp_p = temp_p->next;
              vlb->next = current_p;
            }
          }
          
          if (p->flags & AHC_BIOS_ENABLED)
            sort_list[0] = vlb;
          else
            sort_list[2] = vlb;
          
          break;
        }
        default:  /* All PCI controllers fall through to default */
        {

          p = temp_p;
          if (p->flags & AHC_BIOS_ENABLED) 
            pci = sort_list[1];
          else
            pci = sort_list[3];

          if (pci == NULL)
          {
            pci = temp_p;
            temp_p = temp_p->next;
            pci->next = NULL;
          }
          else
          {
            current_p = pci;
            prev_p = NULL;
            if (!aic7xxx_reverse_scan)
            {
              while ( (current_p != NULL) &&
                      ( (PCI_SLOT(current_p->pci_device_fn) |
                        (current_p->pci_bus << 8)) < 
                        (PCI_SLOT(temp_p->pci_device_fn) |
                        (temp_p->pci_bus << 8)) ) )
              {
                prev_p = current_p;
                current_p = current_p->next;
              }
            }
            else
            {
              while ( (current_p != NULL) &&
                      ( (PCI_SLOT(current_p->pci_device_fn) |
                        (current_p->pci_bus << 8)) > 
                        (PCI_SLOT(temp_p->pci_device_fn) |
                        (temp_p->pci_bus << 8)) ) )
              {
                prev_p = current_p;
                current_p = current_p->next;
              }
            }
            /*
             * Are we dealing with a 7895/6/7/9 where we need to sort the
             * channels as well, if so, the bios_address values should
             * be the same
             */
            if ( (current_p) && (temp_p->flags & AHC_MULTI_CHANNEL) &&
                 (temp_p->pci_bus == current_p->pci_bus) &&
                 (PCI_SLOT(temp_p->pci_device_fn) ==
                  PCI_SLOT(current_p->pci_device_fn)) )
            {
              if (temp_p->flags & AHC_CHNLB)
              {
                if ( !(temp_p->flags & AHC_CHANNEL_B_PRIMARY) )
                {
                  prev_p = current_p;
                  current_p = current_p->next;
                }
              }
              else
              {
                if (temp_p->flags & AHC_CHANNEL_B_PRIMARY)
                {
                  prev_p = current_p;
                  current_p = current_p->next;
                }
              }
            }
            if (prev_p != NULL)
            {
              prev_p->next = temp_p;
              temp_p = temp_p->next;
              prev_p->next->next = current_p;
            }
            else
            {
              pci = temp_p;
              temp_p = temp_p->next;
              pci->next = current_p;
            }
          }

          if (p->flags & AHC_BIOS_ENABLED)
            sort_list[1] = pci;
          else
            sort_list[3] = pci;

          break;
        }
      }  /* End of switch(temp_p->type) */
    } /* End of while (temp_p != NULL) */
    /*
     * At this point, the cards have been broken into 4 sorted lists, now
     * we run through the lists in order and register each controller
     */
    {
      int i;
      
      left = found;
      for (i=0; i<ARRAY_SIZE(sort_list); i++)
      {
        temp_p = sort_list[i];
        while(temp_p != NULL)
        {
          template->name = board_names[temp_p->board_name_index];
          p = aic7xxx_alloc(template, temp_p);
          if (p != NULL)
          {
            p->instance = found - left;
            if (aic7xxx_register(template, p, (--left)) == 0)
            {
              found--;
              aic7xxx_release(p->host);
              scsi_unregister(p->host);
            }
            else if (aic7xxx_dump_card)
            {
              pause_sequencer(p);
              aic7xxx_print_card(p);
              aic7xxx_print_scratch_ram(p);
              unpause_sequencer(p, TRUE);
            }
          }
          current_p = temp_p;
          temp_p = (struct aic7xxx_host *)temp_p->next;
          kfree(current_p);
        }
      }
    }
  }
  return (found);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_buildscb
 *
 * Description:
 *   Build a SCB.
 *-F*************************************************************************/
static void aic7xxx_buildscb(struct aic7xxx_host *p, struct scsi_cmnd *cmd,
			     struct aic7xxx_scb *scb)
{
  unsigned short mask;
  struct aic7xxx_hwscb *hscb;
  struct aic_dev_data *aic_dev = cmd->device->hostdata;
  struct scsi_device *sdptr = cmd->device;
  unsigned char tindex = TARGET_INDEX(cmd);
  int use_sg;

  mask = (0x01 << tindex);
  hscb = scb->hscb;

  /*
   * Setup the control byte if we need negotiation and have not
   * already requested it.
   */
  hscb->control = 0;
  scb->tag_action = 0;

  if (p->discenable & mask)
  {
    hscb->control |= DISCENB;
    /* We always force TEST_UNIT_READY to untagged */
    if (cmd->cmnd[0] != TEST_UNIT_READY && sdptr->simple_tags)
    {
      hscb->control |= MSG_SIMPLE_Q_TAG;
      scb->tag_action = MSG_SIMPLE_Q_TAG;
    }
  }
  if ( !(aic_dev->dtr_pending) &&
        (aic_dev->needppr || aic_dev->needwdtr || aic_dev->needsdtr) &&
        (aic_dev->flags & DEVICE_DTR_SCANNED) )
  {
    aic_dev->dtr_pending = 1;
    scb->tag_action = 0;
    hscb->control &= DISCENB;
    hscb->control |= MK_MESSAGE;
    if(aic_dev->needppr)
    {
      scb->flags |= SCB_MSGOUT_PPR;
    }
    else if(aic_dev->needwdtr)
    {
      scb->flags |= SCB_MSGOUT_WDTR;
    }
    else if(aic_dev->needsdtr)
    {
      scb->flags |= SCB_MSGOUT_SDTR;
    }
    scb->flags |= SCB_DTR_SCB;
  }
  hscb->target_channel_lun = ((cmd->device->id << 4) & 0xF0) |
        ((cmd->device->channel & 0x01) << 3) | (cmd->device->lun & 0x07);

  /*
   * The interpretation of request_buffer and request_bufflen
   * changes depending on whether or not use_sg is zero; a
   * non-zero use_sg indicates the number of elements in the
   * scatter-gather array.
   */

  /*
   * XXX - this relies on the host data being stored in a
   *       little-endian format.
   */
  hscb->SCSI_cmd_length = cmd->cmd_len;
  memcpy(scb->cmnd, cmd->cmnd, cmd->cmd_len);
  hscb->SCSI_cmd_pointer = cpu_to_le32(SCB_DMA_ADDR(scb, scb->cmnd));

  use_sg = scsi_dma_map(cmd);
  BUG_ON(use_sg < 0);

  if (use_sg) {
    struct scatterlist *sg;  /* Must be mid-level SCSI code scatterlist */

    /*
     * We must build an SG list in adapter format, as the kernel's SG list
     * cannot be used directly because of data field size (__alpha__)
     * differences and the kernel SG list uses virtual addresses where
     * we need physical addresses.
     */
    int i;

    scb->sg_length = 0;


    /*
     * Copy the segments into the SG array.  NOTE!!! - We used to
     * have the first entry both in the data_pointer area and the first
     * SG element.  That has changed somewhat.  We still have the first
     * entry in both places, but now we download the address of
     * scb->sg_list[1] instead of 0 to the sg pointer in the hscb.
     */
    scsi_for_each_sg(cmd, sg, use_sg, i) {
      unsigned int len = sg_dma_len(sg);
      scb->sg_list[i].address = cpu_to_le32(sg_dma_address(sg));
      scb->sg_list[i].length = cpu_to_le32(len);
      scb->sg_length += len;
    }
    /* Copy the first SG into the data pointer area. */
    hscb->data_pointer = scb->sg_list[0].address;
    hscb->data_count = scb->sg_list[0].length;
    scb->sg_count = i;
    hscb->SG_segment_count = i;
    hscb->SG_list_pointer = cpu_to_le32(SCB_DMA_ADDR(scb, &scb->sg_list[1]));
  } else {
      scb->sg_count = 0;
      scb->sg_length = 0;
      hscb->SG_segment_count = 0;
      hscb->SG_list_pointer = 0;
      hscb->data_count = 0;
      hscb->data_pointer = 0;
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_queue
 *
 * Description:
 *   Queue a SCB to the controller.
 *-F*************************************************************************/
static int aic7xxx_queue_lck(struct scsi_cmnd *cmd, void (*fn)(struct scsi_cmnd *))
{
  struct aic7xxx_host *p;
  struct aic7xxx_scb *scb;
  struct aic_dev_data *aic_dev;

  p = (struct aic7xxx_host *) cmd->device->host->hostdata;

  aic_dev = cmd->device->hostdata;  
#ifdef AIC7XXX_VERBOSE_DEBUGGING
  if (aic_dev->active_cmds > aic_dev->max_q_depth)
  {
    printk(WARN_LEAD "Commands queued exceeds queue "
           "depth, active=%d\n",
           p->host_no, CTL_OF_CMD(cmd), 
           aic_dev->active_cmds);
  }
#endif

  scb = scbq_remove_head(&p->scb_data->free_scbs);
  if (scb == NULL)
  {
    aic7xxx_allocate_scb(p);
    scb = scbq_remove_head(&p->scb_data->free_scbs);
    if(scb == NULL)
    {
      printk(WARN_LEAD "Couldn't get a free SCB.\n", p->host_no,
             CTL_OF_CMD(cmd));
      return 1;
    }
  }
  scb->cmd = cmd;

	/*
	* Make sure the scsi_cmnd pointer is saved, the struct it points to
	* is set up properly, and the parity error flag is reset, then send
	* the SCB to the sequencer and watch the fun begin.
	*/
  aic7xxx_position(cmd) = scb->hscb->tag;
  cmd->scsi_done = fn;
  cmd->result = DID_OK;
  aic7xxx_error(cmd) = DID_OK;
  aic7xxx_status(cmd) = 0;
  cmd->host_scribble = NULL;

  /*
   * Construct the SCB beforehand, so the sequencer is
   * paused a minimal amount of time.
   */
  aic7xxx_buildscb(p, cmd, scb);

  scb->flags |= SCB_ACTIVE | SCB_WAITINGQ;

  scbq_insert_tail(&p->waiting_scbs, scb);
  aic7xxx_run_waiting_queues(p);
  return (0);
}

static DEF_SCSI_QCMD(aic7xxx_queue)

/*+F*************************************************************************
 * Function:
 *   aic7xxx_bus_device_reset
 *
 * Description:
 *   Abort or reset the current SCSI command(s).  If the scb has not
 *   previously been aborted, then we attempt to send a BUS_DEVICE_RESET
 *   message to the target.  If the scb has previously been unsuccessfully
 *   aborted, then we will reset the channel and have all devices renegotiate.
 *   Returns an enumerated type that indicates the status of the operation.
 *-F*************************************************************************/
static int __aic7xxx_bus_device_reset(struct scsi_cmnd *cmd)
{
  struct aic7xxx_host  *p;
  struct aic7xxx_scb   *scb;
  struct aic7xxx_hwscb *hscb;
  int channel;
  unsigned char saved_scbptr, lastphase;
  unsigned char hscb_index;
  int disconnected;
  struct aic_dev_data *aic_dev;

  if(cmd == NULL)
  {
    printk(KERN_ERR "aic7xxx_bus_device_reset: called with NULL cmd!\n");
    return FAILED;
  }
  p = (struct aic7xxx_host *)cmd->device->host->hostdata;
  aic_dev = AIC_DEV(cmd);
  if(aic7xxx_position(cmd) < p->scb_data->numscbs)
    scb = (p->scb_data->scb_array[aic7xxx_position(cmd)]);
  else
    return FAILED;

  hscb = scb->hscb;

  aic7xxx_isr(p);
  aic7xxx_done_cmds_complete(p);
  /* If the command was already complete or just completed, then we didn't
   * do a reset, return FAILED */
  if(!(scb->flags & SCB_ACTIVE))
    return FAILED;

  pause_sequencer(p);
  lastphase = aic_inb(p, LASTPHASE);
  if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
  {
    printk(INFO_LEAD "Bus Device reset, scb flags 0x%x, ",
         p->host_no, CTL_OF_SCB(scb), scb->flags);
    switch (lastphase)
    {
      case P_DATAOUT:
        printk("Data-Out phase\n");
        break;
      case P_DATAIN:
        printk("Data-In phase\n");
        break;
      case P_COMMAND:
        printk("Command phase\n");
        break;
      case P_MESGOUT:
        printk("Message-Out phase\n");
        break;
      case P_STATUS:
        printk("Status phase\n");
        break;
      case P_MESGIN:
        printk("Message-In phase\n");
        break;
      default:
      /*
       * We're not in a valid phase, so assume we're idle.
       */
        printk("while idle, LASTPHASE = 0x%x\n", lastphase);
        break;
    }
    printk(INFO_LEAD "SCSISIGI 0x%x, SEQADDR 0x%x, SSTAT0 0x%x, SSTAT1 "
         "0x%x\n", p->host_no, CTL_OF_SCB(scb),
         aic_inb(p, SCSISIGI),
         aic_inb(p, SEQADDR0) | (aic_inb(p, SEQADDR1) << 8),
         aic_inb(p, SSTAT0), aic_inb(p, SSTAT1));
    printk(INFO_LEAD "SG_CACHEPTR 0x%x, SSTAT2 0x%x, STCNT 0x%x\n", p->host_no,
         CTL_OF_SCB(scb),
         (p->features & AHC_ULTRA2) ? aic_inb(p, SG_CACHEPTR) : 0,
         aic_inb(p, SSTAT2),
         aic_inb(p, STCNT + 2) << 16 | aic_inb(p, STCNT + 1) << 8 |
         aic_inb(p, STCNT));
  }

  channel = cmd->device->channel;

    /*
     * Send a Device Reset Message:
     * The target that is holding up the bus may not be the same as
     * the one that triggered this timeout (different commands have
     * different timeout lengths).  Our strategy here is to queue an
     * abort message to the timed out target if it is disconnected.
     * Otherwise, if we have an active target we stuff the message buffer
     * with an abort message and assert ATN in the hopes that the target
     * will let go of the bus and go to the mesgout phase.  If this
     * fails, we'll get another timeout a few seconds later which will
     * attempt a bus reset.
     */
  saved_scbptr = aic_inb(p, SCBPTR);
  disconnected = FALSE;

  if (lastphase != P_BUSFREE)
  {
    if (aic_inb(p, SCB_TAG) >= p->scb_data->numscbs)
    {
      printk(WARN_LEAD "Invalid SCB ID %d is active, "
             "SCB flags = 0x%x.\n", p->host_no,
            CTL_OF_CMD(cmd), scb->hscb->tag, scb->flags);
      unpause_sequencer(p, FALSE);
      return FAILED;
    }
    if (scb->hscb->tag == aic_inb(p, SCB_TAG))
    { 
      if ( (lastphase == P_MESGOUT) || (lastphase == P_MESGIN) )
      {
        printk(WARN_LEAD "Device reset, Message buffer "
                "in use\n", p->host_no, CTL_OF_SCB(scb));
        unpause_sequencer(p, FALSE);
	return FAILED;
      }
	
      if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
        printk(INFO_LEAD "Device reset message in "
              "message buffer\n", p->host_no, CTL_OF_SCB(scb));
      scb->flags |= SCB_RESET | SCB_DEVICE_RESET;
      aic7xxx_error(cmd) = DID_RESET;
      aic_dev->flags |= BUS_DEVICE_RESET_PENDING;
      /* Send the abort message to the active SCB. */
      aic_outb(p, HOST_MSG, MSG_OUT);
      aic_outb(p, lastphase | ATNO, SCSISIGO);
      unpause_sequencer(p, FALSE);
      spin_unlock_irq(p->host->host_lock);
      ssleep(1);
      spin_lock_irq(p->host->host_lock);
      if(aic_dev->flags & BUS_DEVICE_RESET_PENDING)
        return FAILED;
      else
        return SUCCESS;
    }
  } /* if (last_phase != P_BUSFREE).....indicates we are idle and can work */
  /*
   * Simply set the MK_MESSAGE flag and the SEQINT handler will do
   * the rest on a reconnect/connect.
   */
  scb->hscb->control |= MK_MESSAGE;
  scb->flags |= SCB_RESET | SCB_DEVICE_RESET;
  aic_dev->flags |= BUS_DEVICE_RESET_PENDING;
  /*
   * Check to see if the command is on the qinfifo.  If it is, then we will
   * not need to queue the command again since the card should start it soon
   */
  if (aic7xxx_search_qinfifo(p, cmd->device->channel, cmd->device->id, cmd->device->lun, hscb->tag,
			  0, TRUE, NULL) == 0)
  {
    disconnected = TRUE;
    if ((hscb_index = aic7xxx_find_scb(p, scb)) != SCB_LIST_NULL)
    {
      unsigned char scb_control;

      aic_outb(p, hscb_index, SCBPTR);
      scb_control = aic_inb(p, SCB_CONTROL);
      /*
       * If the DISCONNECTED bit is not set in SCB_CONTROL, then we are
       * actually on the waiting list, not disconnected, and we don't
       * need to requeue the command.
       */
      disconnected = (scb_control & DISCONNECTED);
      aic_outb(p, scb_control | MK_MESSAGE, SCB_CONTROL);
    }
    if (disconnected)
    {
      /*
       * Actually requeue this SCB in case we can select the
       * device before it reconnects.  This can result in the command
       * being on the qinfifo twice, but we don't care because it will
       * all get cleaned up if/when the reset takes place.
       */
      if (aic7xxx_verbose & VERBOSE_RESET_PROCESS)
        printk(INFO_LEAD "Queueing device reset command.\n", p->host_no,
		      CTL_OF_SCB(scb));
      p->qinfifo[p->qinfifonext++] = scb->hscb->tag;
      if (p->features & AHC_QUEUE_REGS)
        aic_outb(p, p->qinfifonext, HNSCB_QOFF);
      else
        aic_outb(p, p->qinfifonext, KERNEL_QINPOS);
      scb->flags |= SCB_QUEUED_ABORT;
    }
  }
  aic_outb(p, saved_scbptr, SCBPTR);
  unpause_sequencer(p, FALSE);
  spin_unlock_irq(p->host->host_lock);
  msleep(1000/4);
  spin_lock_irq(p->host->host_lock);
  if(aic_dev->flags & BUS_DEVICE_RESET_PENDING)
    return FAILED;
  else
    return SUCCESS;
}

static int aic7xxx_bus_device_reset(struct scsi_cmnd *cmd)
{
      int rc;

      spin_lock_irq(cmd->device->host->host_lock);
      rc = __aic7xxx_bus_device_reset(cmd);
      spin_unlock_irq(cmd->device->host->host_lock);

      return rc;
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_panic_abort
 *
 * Description:
 *   Abort the current SCSI command(s).
 *-F*************************************************************************/
static void aic7xxx_panic_abort(struct aic7xxx_host *p, struct scsi_cmnd *cmd)
{

  printk("aic7xxx driver version %s\n", AIC7XXX_C_VERSION);
  printk("Controller type:\n    %s\n", board_names[p->board_name_index]);
  printk("p->flags=0x%lx, p->chip=0x%x, p->features=0x%x, "
         "sequencer %s paused\n",
     p->flags, p->chip, p->features,
    (aic_inb(p, HCNTRL) & PAUSE) ? "is" : "isn't" );
  pause_sequencer(p);
  disable_irq(p->irq);
  aic7xxx_print_card(p);
  aic7xxx_print_scratch_ram(p);
  spin_unlock_irq(p->host->host_lock);
  for(;;) barrier();
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_abort
 *
 * Description:
 *   Abort the current SCSI command(s).
 *-F*************************************************************************/
static int __aic7xxx_abort(struct scsi_cmnd *cmd)
{
  struct aic7xxx_scb  *scb = NULL;
  struct aic7xxx_host *p;
  int    found=0, disconnected;
  unsigned char saved_hscbptr, hscbptr, scb_control;
  struct aic_dev_data *aic_dev;

  if(cmd == NULL)
  {
    printk(KERN_ERR "aic7xxx_abort: called with NULL cmd!\n");
    return FAILED;
  }
  p = (struct aic7xxx_host *)cmd->device->host->hostdata;
  aic_dev = AIC_DEV(cmd);
  if(aic7xxx_position(cmd) < p->scb_data->numscbs)
    scb = (p->scb_data->scb_array[aic7xxx_position(cmd)]);
  else
    return FAILED;

  aic7xxx_isr(p);
  aic7xxx_done_cmds_complete(p);
  /* If the command was already complete or just completed, then we didn't
   * do a reset, return FAILED */
  if(!(scb->flags & SCB_ACTIVE))
    return FAILED;

  pause_sequencer(p);

  /*
   * I added a new config option to the driver: "panic_on_abort" that will
   * cause the driver to panic and the machine to stop on the first abort
   * or reset call into the driver.  At that point, it prints out a lot of
   * useful information for me which I can then use to try and debug the
   * problem.  Simply enable the boot time prompt in order to activate this
   * code.
   */
  if (aic7xxx_panic_on_abort)
    aic7xxx_panic_abort(p, cmd);

  if (aic7xxx_verbose & VERBOSE_ABORT)
  {
    printk(INFO_LEAD "Aborting scb %d, flags 0x%x, SEQADDR 0x%x, LASTPHASE "
           "0x%x\n",
         p->host_no, CTL_OF_SCB(scb), scb->hscb->tag, scb->flags,
         aic_inb(p, SEQADDR0) | (aic_inb(p, SEQADDR1) << 8),
         aic_inb(p, LASTPHASE));
    printk(INFO_LEAD "SG_CACHEPTR 0x%x, SG_COUNT %d, SCSISIGI 0x%x\n",
         p->host_no, CTL_OF_SCB(scb), (p->features & AHC_ULTRA2) ?
         aic_inb(p, SG_CACHEPTR) : 0, aic_inb(p, SG_COUNT),
         aic_inb(p, SCSISIGI));
    printk(INFO_LEAD "SSTAT0 0x%x, SSTAT1 0x%x, SSTAT2 0x%x\n",
         p->host_no, CTL_OF_SCB(scb), aic_inb(p, SSTAT0),
         aic_inb(p, SSTAT1), aic_inb(p, SSTAT2));
  }

  if (scb->flags & SCB_WAITINGQ)
  {
    if (aic7xxx_verbose & VERBOSE_ABORT_PROCESS) 
      printk(INFO_LEAD "SCB found on waiting list and "
          "aborted.\n", p->host_no, CTL_OF_SCB(scb));
    scbq_remove(&p->waiting_scbs, scb);
    scbq_remove(&aic_dev->delayed_scbs, scb);
    aic_dev->active_cmds++;
    p->activescbs++;
    scb->flags &= ~(SCB_WAITINGQ | SCB_ACTIVE);
    scb->flags |= SCB_ABORT | SCB_QUEUED_FOR_DONE;
    goto success;
  }

/*
 *  We just checked the waiting_q, now for the QINFIFO
 */
  if ( ((found = aic7xxx_search_qinfifo(p, cmd->device->id, cmd->device->channel,
                     cmd->device->lun, scb->hscb->tag, SCB_ABORT | SCB_QUEUED_FOR_DONE,
                     FALSE, NULL)) != 0) &&
                    (aic7xxx_verbose & VERBOSE_ABORT_PROCESS))
  {
    printk(INFO_LEAD "SCB found in QINFIFO and aborted.\n", p->host_no,
		    CTL_OF_SCB(scb));
    goto success;
  }

/*
 *  QINFIFO, waitingq, completeq done.  Next, check WAITING_SCB list in card
 */

  saved_hscbptr = aic_inb(p, SCBPTR);
  if ((hscbptr = aic7xxx_find_scb(p, scb)) != SCB_LIST_NULL)
  {
    aic_outb(p, hscbptr, SCBPTR);
    scb_control = aic_inb(p, SCB_CONTROL);
    disconnected = scb_control & DISCONNECTED;
    /*
     * If the DISCONNECTED bit is not set in SCB_CONTROL, then we are
     * either currently active or on the waiting list.
     */
    if(!disconnected && aic_inb(p, LASTPHASE) == P_BUSFREE) {
      if (aic7xxx_verbose & VERBOSE_ABORT_PROCESS)
        printk(INFO_LEAD "SCB found on hardware waiting"
          " list and aborted.\n", p->host_no, CTL_OF_SCB(scb));
      /* If we are the only waiting command, stop the selection engine */
      if (aic_inb(p, WAITING_SCBH) == hscbptr && aic_inb(p, SCB_NEXT) ==
			SCB_LIST_NULL)
      {
        aic_outb(p, aic_inb(p, SCSISEQ) & ~ENSELO, SCSISEQ);
        aic_outb(p, CLRSELTIMEO, CLRSINT1);
	aic_outb(p, SCB_LIST_NULL, WAITING_SCBH);
      }
      else
      {
	unsigned char prev, next;
	prev = SCB_LIST_NULL;
	next = aic_inb(p, WAITING_SCBH);
	while(next != SCB_LIST_NULL)
	{
	  aic_outb(p, next, SCBPTR);
	  if (next == hscbptr)
	  {
	    next = aic_inb(p, SCB_NEXT);
	    if (prev != SCB_LIST_NULL)
	    {
	      aic_outb(p, prev, SCBPTR);
	      aic_outb(p, next, SCB_NEXT);
	    }
	    else
	      aic_outb(p, next, WAITING_SCBH);
	    aic_outb(p, hscbptr, SCBPTR);
	    next = SCB_LIST_NULL;
	  }
	  else
	  {
	    prev = next;
	    next = aic_inb(p, SCB_NEXT);
	  }
	}
      }
      aic_outb(p, SCB_LIST_NULL, SCB_TAG);
      aic_outb(p, 0, SCB_CONTROL);
      aic7xxx_add_curscb_to_free_list(p);
      scb->flags = SCB_ABORT | SCB_QUEUED_FOR_DONE;
      goto success;
    }
    else if (!disconnected)
    {
      /*
       * We are the currently active command
       */
      if((aic_inb(p, LASTPHASE) == P_MESGIN) ||
	 (aic_inb(p, LASTPHASE) == P_MESGOUT))
      {
	/*
	 * Message buffer busy, unable to abort
	 */
	printk(INFO_LEAD "message buffer busy, unable to abort.\n",
			  p->host_no, CTL_OF_SCB(scb));
	unpause_sequencer(p, FALSE);
	return FAILED;
      }
      /* Fallthrough to below, set ATNO after we set SCB_CONTROL */
    } 
    aic_outb(p,  scb_control | MK_MESSAGE, SCB_CONTROL);
    if(!disconnected)
    {
      aic_outb(p, HOST_MSG, MSG_OUT);
      aic_outb(p, aic_inb(p, SCSISIGI) | ATNO, SCSISIGO);
    }
    aic_outb(p, saved_hscbptr, SCBPTR);
  } 
  else
  {
    /*
     * The scb isn't in the card at all and it is active and it isn't in
     * any of the queues, so it must be disconnected and paged out.  Fall
     * through to the code below.
     */
    disconnected = 1;
  }
        
  p->flags |= AHC_ABORT_PENDING;
  scb->flags |= SCB_QUEUED_ABORT | SCB_ABORT | SCB_RECOVERY_SCB;
  scb->hscb->control |= MK_MESSAGE;
  if(disconnected)
  {
    if (aic7xxx_verbose & VERBOSE_ABORT_PROCESS)
      printk(INFO_LEAD "SCB disconnected.  Queueing Abort"
        " SCB.\n", p->host_no, CTL_OF_SCB(scb));
    p->qinfifo[p->qinfifonext++] = scb->hscb->tag;
    if (p->features & AHC_QUEUE_REGS)
      aic_outb(p, p->qinfifonext, HNSCB_QOFF);
    else
      aic_outb(p, p->qinfifonext, KERNEL_QINPOS);
  }
  unpause_sequencer(p, FALSE);
  spin_unlock_irq(p->host->host_lock);
  msleep(1000/4);
  spin_lock_irq(p->host->host_lock);
  if (p->flags & AHC_ABORT_PENDING)
  {
    if (aic7xxx_verbose & VERBOSE_ABORT_RETURN)
      printk(INFO_LEAD "Abort never delivered, returning FAILED\n", p->host_no,
		    CTL_OF_CMD(cmd));
    p->flags &= ~AHC_ABORT_PENDING;
    return FAILED;
  }
  if (aic7xxx_verbose & VERBOSE_ABORT_RETURN)
    printk(INFO_LEAD "Abort successful.\n", p->host_no, CTL_OF_CMD(cmd));
  return SUCCESS;

success:
  if (aic7xxx_verbose & VERBOSE_ABORT_RETURN)
    printk(INFO_LEAD "Abort successful.\n", p->host_no, CTL_OF_CMD(cmd));
  aic7xxx_run_done_queue(p, TRUE);
  unpause_sequencer(p, FALSE);
  return SUCCESS;
}

static int aic7xxx_abort(struct scsi_cmnd *cmd)
{
	int rc;

	spin_lock_irq(cmd->device->host->host_lock);
	rc = __aic7xxx_abort(cmd);
	spin_unlock_irq(cmd->device->host->host_lock);

	return rc;
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_reset
 *
 * Description:
 *   Resetting the bus always succeeds - is has to, otherwise the
 *   kernel will panic! Try a surgical technique - sending a BUS
 *   DEVICE RESET message - on the offending target before pulling
 *   the SCSI bus reset line.
 *-F*************************************************************************/
static int aic7xxx_reset(struct scsi_cmnd *cmd)
{
  struct aic7xxx_scb *scb;
  struct aic7xxx_host *p;
  struct aic_dev_data *aic_dev;

  p = (struct aic7xxx_host *) cmd->device->host->hostdata;
  spin_lock_irq(p->host->host_lock);

  aic_dev = AIC_DEV(cmd);
  if(aic7xxx_position(cmd) < p->scb_data->numscbs)
  {
    scb = (p->scb_data->scb_array[aic7xxx_position(cmd)]);
    if (scb->cmd != cmd)
      scb = NULL;
  }
  else
  {
    scb = NULL;
  }

  /*
   * I added a new config option to the driver: "panic_on_abort" that will
   * cause the driver to panic and the machine to stop on the first abort
   * or reset call into the driver.  At that point, it prints out a lot of
   * useful information for me which I can then use to try and debug the
   * problem.  Simply enable the boot time prompt in order to activate this
   * code.
   */
  if (aic7xxx_panic_on_abort)
    aic7xxx_panic_abort(p, cmd);

  pause_sequencer(p);

  while((aic_inb(p, INTSTAT) & INT_PEND) && !(p->flags & AHC_IN_ISR))
  {
    aic7xxx_isr(p);
    pause_sequencer(p);
  }
  aic7xxx_done_cmds_complete(p);

  if(scb && (scb->cmd == NULL))
  {
    /*
     * We just completed the command when we ran the isr stuff, so we no
     * longer have it.
     */
    unpause_sequencer(p, FALSE);
    spin_unlock_irq(p->host->host_lock);
    return SUCCESS;
  }
    
/*
 *  By this point, we want to already know what we are going to do and
 *  only have the following code implement our course of action.
 */
  aic7xxx_reset_channel(p, cmd->device->channel, TRUE);
  if (p->features & AHC_TWIN)
  {
    aic7xxx_reset_channel(p, cmd->device->channel ^ 0x01, TRUE);
    restart_sequencer(p);
  }
  aic_outb(p,  aic_inb(p, SIMODE1) & ~(ENREQINIT|ENBUSFREE), SIMODE1);
  aic7xxx_clear_intstat(p);
  p->flags &= ~AHC_HANDLING_REQINITS;
  p->msg_type = MSG_TYPE_NONE;
  p->msg_index = 0;
  p->msg_len = 0;
  aic7xxx_run_done_queue(p, TRUE);
  unpause_sequencer(p, FALSE);
  spin_unlock_irq(p->host->host_lock);
  ssleep(2);
  return SUCCESS;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_biosparam
 *
 * Description:
 *   Return the disk geometry for the given SCSI device.
 *
 * Note:
 *   This function is broken for today's really large drives and needs
 *   fixed.
 *-F*************************************************************************/
static int
aic7xxx_biosparam(struct scsi_device *sdev, struct block_device *bdev,
		sector_t capacity, int geom[])
{
  sector_t heads, sectors, cylinders;
  int ret;
  struct aic7xxx_host *p;
  unsigned char *buf;

  p = (struct aic7xxx_host *) sdev->host->hostdata;
  buf = scsi_bios_ptable(bdev);

  if ( buf )
  {
    ret = scsi_partsize(buf, capacity, &geom[2], &geom[0], &geom[1]);
    kfree(buf);
    if ( ret != -1 )
      return(ret);
  }
  
  heads = 64;
  sectors = 32;
  cylinders = capacity >> 11;

  if ((p->flags & AHC_EXTEND_TRANS_A) && (cylinders > 1024))
  {
    heads = 255;
    sectors = 63;
    cylinders = capacity >> 14;
    if(capacity > (65535 * heads * sectors))
      cylinders = 65535;
    else
      cylinders = ((unsigned int)capacity) / (unsigned int)(heads * sectors);
  }

  geom[0] = (int)heads;
  geom[1] = (int)sectors;
  geom[2] = (int)cylinders;

  return (0);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_release
 *
 * Description:
 *   Free the passed in Scsi_Host memory structures prior to unloading the
 *   module.
 *-F*************************************************************************/
static int
aic7xxx_release(struct Scsi_Host *host)
{
  struct aic7xxx_host *p = (struct aic7xxx_host *) host->hostdata;
  struct aic7xxx_host *next, *prev;

  if(p->irq)
    free_irq(p->irq, p);
#ifdef MMAPIO
  if(p->maddr)
  {
    iounmap(p->maddr);
  }
#endif /* MMAPIO */
  if(!p->pdev)
    release_region(p->base, MAXREG - MINREG);
#ifdef CONFIG_PCI
  else {
    pci_release_regions(p->pdev);
    pci_dev_put(p->pdev);
  }
#endif
  prev = NULL;
  next = first_aic7xxx;
  while(next != NULL)
  {
    if(next == p)
    {
      if(prev == NULL)
        first_aic7xxx = next->next;
      else
        prev->next = next->next;
    }
    else
    {
      prev = next;
    }
    next = next->next;
  }
  aic7xxx_free(p);
  return(0);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_print_card
 *
 * Description:
 *   Print out all of the control registers on the card
 *
 *   NOTE: This function is not yet safe for use on the VLB and EISA
 *   controllers, so it isn't used on those controllers at all.
 *-F*************************************************************************/
static void
aic7xxx_print_card(struct aic7xxx_host *p)
{
  int i, j, k, chip;
  static struct register_ranges {
    int num_ranges;
    int range_val[32];
  } cards_ds[] = {
    { 0, {0,} }, /* none */
    {10, {0x00, 0x05, 0x08, 0x11, 0x18, 0x19, 0x1f, 0x1f, 0x60, 0x60, /*7771*/
          0x62, 0x66, 0x80, 0x8e, 0x90, 0x95, 0x97, 0x97, 0x9b, 0x9f} },
    { 9, {0x00, 0x05, 0x08, 0x11, 0x18, 0x1f, 0x60, 0x60, 0x62, 0x66, /*7850*/
          0x80, 0x8e, 0x90, 0x95, 0x97, 0x97, 0x9a, 0x9f} },
    { 9, {0x00, 0x05, 0x08, 0x11, 0x18, 0x1f, 0x60, 0x60, 0x62, 0x66, /*7860*/
          0x80, 0x8e, 0x90, 0x95, 0x97, 0x97, 0x9a, 0x9f} },
    {10, {0x00, 0x05, 0x08, 0x11, 0x18, 0x19, 0x1c, 0x1f, 0x60, 0x60, /*7870*/
          0x62, 0x66, 0x80, 0x8e, 0x90, 0x95, 0x97, 0x97, 0x9a, 0x9f} },
    {10, {0x00, 0x05, 0x08, 0x11, 0x18, 0x1a, 0x1c, 0x1f, 0x60, 0x60, /*7880*/
          0x62, 0x66, 0x80, 0x8e, 0x90, 0x95, 0x97, 0x97, 0x9a, 0x9f} },
    {16, {0x00, 0x05, 0x08, 0x11, 0x18, 0x1f, 0x60, 0x60, 0x62, 0x66, /*7890*/
          0x84, 0x8e, 0x90, 0x95, 0x97, 0x97, 0x9a, 0x9a, 0x9f, 0x9f,
          0xe0, 0xf1, 0xf4, 0xf4, 0xf6, 0xf6, 0xf8, 0xf8, 0xfa, 0xfc,
          0xfe, 0xff} },
    {12, {0x00, 0x05, 0x08, 0x11, 0x18, 0x19, 0x1b, 0x1f, 0x60, 0x60, /*7895*/
          0x62, 0x66, 0x80, 0x8e, 0x90, 0x95, 0x97, 0x97, 0x9a, 0x9a,
          0x9f, 0x9f, 0xe0, 0xf1} },
    {16, {0x00, 0x05, 0x08, 0x11, 0x18, 0x1f, 0x60, 0x60, 0x62, 0x66, /*7896*/
          0x84, 0x8e, 0x90, 0x95, 0x97, 0x97, 0x9a, 0x9a, 0x9f, 0x9f,
          0xe0, 0xf1, 0xf4, 0xf4, 0xf6, 0xf6, 0xf8, 0xf8, 0xfa, 0xfc,
          0xfe, 0xff} },
    {12, {0x00, 0x05, 0x08, 0x11, 0x18, 0x1f, 0x60, 0x60, 0x62, 0x66, /*7892*/
          0x84, 0x8e, 0x90, 0x95, 0x97, 0x97, 0x9a, 0x9a, 0x9c, 0x9f,
          0xe0, 0xf1, 0xf4, 0xfc} },
    {12, {0x00, 0x05, 0x08, 0x11, 0x18, 0x1f, 0x60, 0x60, 0x62, 0x66, /*7899*/
          0x84, 0x8e, 0x90, 0x95, 0x97, 0x97, 0x9a, 0x9a, 0x9c, 0x9f,
          0xe0, 0xf1, 0xf4, 0xfc} },
  };
  chip = p->chip & AHC_CHIPID_MASK;
  printk("%s at ",
         board_names[p->board_name_index]);
  switch(p->chip & ~AHC_CHIPID_MASK)
  {
    case AHC_VL:
      printk("VLB Slot %d.\n", p->pci_device_fn);
      break;
    case AHC_EISA:
      printk("EISA Slot %d.\n", p->pci_device_fn);
      break;
    case AHC_PCI:
    default:
      printk("PCI %d/%d/%d.\n", p->pci_bus, PCI_SLOT(p->pci_device_fn),
             PCI_FUNC(p->pci_device_fn));
      break;
  }

  /*
   * the registers on the card....
   */
  printk("Card Dump:\n");
  k = 0;
  for(i=0; i<cards_ds[chip].num_ranges; i++)
  {
    for(j  = cards_ds[chip].range_val[ i * 2 ];
        j <= cards_ds[chip].range_val[ i * 2 + 1 ] ;
        j++)
    {
      printk("%02x:%02x ", j, aic_inb(p, j));
      if(++k == 13)
      {
        printk("\n");
        k=0;
      }
    }
  }
  if(k != 0)
    printk("\n");

  /*
   * If this was an Ultra2 controller, then we just hosed the card in terms
   * of the QUEUE REGS.  This function is only called at init time or by
   * the panic_abort function, so it's safe to assume a generic init time
   * setting here
   */

  if(p->features & AHC_QUEUE_REGS)
  {
    aic_outb(p, 0, SDSCB_QOFF);
    aic_outb(p, 0, SNSCB_QOFF);
    aic_outb(p, 0, HNSCB_QOFF);
  }

}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_print_scratch_ram
 *
 * Description:
 *   Print out the scratch RAM values on the card.
 *-F*************************************************************************/
static void
aic7xxx_print_scratch_ram(struct aic7xxx_host *p)
{
  int i, k;

  k = 0;
  printk("Scratch RAM:\n");
  for(i = SRAM_BASE; i < SEQCTL; i++)
  {
    printk("%02x:%02x ", i, aic_inb(p, i));
    if(++k == 13)
    {
      printk("\n");
      k=0;
    }
  }
  if (p->features & AHC_MORE_SRAM)
  {
    for(i = TARG_OFFSET; i < 0x80; i++)
    {
      printk("%02x:%02x ", i, aic_inb(p, i));
      if(++k == 13)
      {
        printk("\n");
        k=0;
      }
    }
  }
  printk("\n");
}


#include "aic7xxx_old/aic7xxx_proc.c"

MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(AIC7XXX_H_VERSION);


static struct scsi_host_template driver_template = {
	.proc_info		= aic7xxx_proc_info,
	.detect			= aic7xxx_detect,
	.release		= aic7xxx_release,
	.info			= aic7xxx_info,	
	.queuecommand		= aic7xxx_queue,
	.slave_alloc		= aic7xxx_slave_alloc,
	.slave_configure	= aic7xxx_slave_configure,
	.slave_destroy		= aic7xxx_slave_destroy,
	.bios_param		= aic7xxx_biosparam,
	.eh_abort_handler	= aic7xxx_abort,
	.eh_device_reset_handler	= aic7xxx_bus_device_reset,
	.eh_host_reset_handler	= aic7xxx_reset,
	.can_queue		= 255,
	.this_id		= -1,
	.max_sectors		= 2048,
	.cmd_per_lun		= 3,
	.use_clustering		= ENABLE_CLUSTERING,
};

#include "scsi_module.c"

/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 2
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -2
 * c-argdecl-indent: 2
 * c-label-offset: -2
 * c-continued-statement-offset: 2
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
